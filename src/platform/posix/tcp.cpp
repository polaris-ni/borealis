// ============================================================
// POSIX TCP 转发腿实现（src/platform/posix/tcp.cpp）
// ------------------------------------------------------------
// platform::tcp_listen / tcp_connect 的 POSIX 腿（SPEC.FEAT.CONN.08）。
// 裸 socket 知识不外溢：接口见 platform/tcp.h（AGENTS.md §4.5 第 23 条）。
// ============================================================

#include "platform/tcp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief RAII 句柄：-1 表示未持有。
class SocketFd {
  public:
    explicit SocketFd(int fd = -1) : fd_(fd) {}
    ~SocketFd() { reset(); }

    SocketFd(const SocketFd &) = delete;
    auto operator=(const SocketFd &) -> SocketFd & = delete;
    SocketFd(SocketFd &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    auto operator=(SocketFd &&) -> SocketFd & = delete;

    [[nodiscard]] auto get() const -> int { return fd_; }
    [[nodiscard]] auto valid() const -> bool { return fd_ >= 0; }
    auto release() -> int {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }
    auto reset(int fd = -1) -> void {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_{-1};
};

/// @brief 把点分 IPv4 写进 sockaddr_in；非法字面量回 false。
[[nodiscard]] auto fill_ipv4(const std::string &address, sockaddr_in &out) -> bool {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    const auto *text = address.empty() ? "0.0.0.0" : address.c_str();
    return inet_pton(AF_INET, text, &out.sin_addr) == 1;
}

class PosixTcpStream final : public TcpStream {
  public:
    explicit PosixTcpStream(SocketFd fd) : fd_(std::move(fd)) {}

    auto read(std::span<std::uint8_t> out, int timeout_ms) -> TcpRead override {
        if (!fd_.valid() || out.empty()) {
            return {TcpReadOutcome::Timeout, 0};
        }
        pollfd events{fd_.get(), POLLIN, 0};
        const int ready = ::poll(&events, 1, timeout_ms);
        if (ready == 0) {
            return {TcpReadOutcome::Timeout, 0};
        }
        if (ready < 0) {
            if (errno == EINTR) {
                return {TcpReadOutcome::Timeout, 0};
            }
            AURORA_LOG_ERROR("platform", "tcp poll failed: ", std::strerror(errno));
            return {TcpReadOutcome::Error, 0};
        }
        const ssize_t n = ::recv(fd_.get(), out.data(), out.size(), MSG_DONTWAIT);
        if (n > 0) {
            return {TcpReadOutcome::Data, static_cast<std::size_t>(n)};
        }
        if (n == 0) {
            return {TcpReadOutcome::Closed, 0};
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return {TcpReadOutcome::Timeout, 0};
        }
        AURORA_LOG_ERROR("platform", "tcp recv failed: ", std::strerror(errno));
        return {TcpReadOutcome::Error, 0};
    }

    auto write(std::span<const std::uint8_t> bytes) -> bool override {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            if (!fd_.valid()) {
                return false;
            }
            const ssize_t n = ::send(fd_.get(), bytes.data() + sent, bytes.size() - sent,
                                     MSG_NOSIGNAL);
            if (n > 0) {
                sent += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                // 发送缓冲满：等可写再续（对 poll 的超时留给内核默认节奏）。
                pollfd events{fd_.get(), POLLOUT, 0};
                if (::poll(&events, 1, 5000) <= 0) {
                    AURORA_LOG_ERROR("platform", "tcp send blocked out");
                    return false;
                }
                continue;
            }
            AURORA_LOG_ERROR("platform", "tcp send failed: ", std::strerror(errno));
            return false;
        }
        return true;
    }

    auto close() -> void override { fd_.reset(); }

    [[nodiscard]] auto peer_address() const -> std::string override {
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        if (!fd_.valid() || ::getpeername(fd_.get(), reinterpret_cast<sockaddr *>(&peer),
                                         &length) != 0) {
            return {};
        }
        char text[INET_ADDRSTRLEN] = {};
        if (::inet_ntop(AF_INET, &peer.sin_addr, text, sizeof(text)) == nullptr) {
            return {};
        }
        return text;
    }

    [[nodiscard]] auto peer_port() const -> int override {
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        if (!fd_.valid() || ::getpeername(fd_.get(), reinterpret_cast<sockaddr *>(&peer),
                                         &length) != 0) {
            return 0;
        }
        return ntohs(peer.sin_port);
    }

  private:
    SocketFd fd_;
};

class PosixTcpListener final : public TcpListener {
  public:
    PosixTcpListener(SocketFd fd, int bound_port) : fd_(std::move(fd)), bound_port_{bound_port} {}

    auto accept(int timeout_ms) -> std::unique_ptr<TcpStream> override {
        if (!fd_.valid()) {
            return nullptr;
        }
        pollfd events{fd_.get(), POLLIN, 0};
        if (::poll(&events, 1, timeout_ms) <= 0) {
            return nullptr;
        }
        const int client = ::accept4(fd_.get(), nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            return nullptr;
        }
        return std::make_unique<PosixTcpStream>(SocketFd{client});
    }

    [[nodiscard]] auto bound_port() const -> int override { return bound_port_; }

    auto close() -> void override { fd_.reset(); }

  private:
    SocketFd fd_;
    int bound_port_{0};
};

}  // namespace

auto tcp_listen(const std::string &bind_address, int port) -> std::unique_ptr<TcpListener> {
    sockaddr_in addr{};
    if (!fill_ipv4(bind_address, addr)) {
        AURORA_LOG_ERROR("platform", "tcp listen bad bind address: ", bind_address);
        return nullptr;
    }
    addr.sin_port = htons(static_cast<std::uint16_t>(port));

    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        AURORA_LOG_ERROR("platform", "tcp socket failed: ", std::strerror(errno));
        return nullptr;
    }
    const int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    SocketFd guard{fd};
    if (::bind(guard.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        AURORA_LOG_ERROR("platform", "tcp bind failed on port: ", std::to_string(port),
                         " (", std::strerror(errno), ")");
        return nullptr;
    }
    if (::listen(guard.get(), 16) != 0) {
        AURORA_LOG_ERROR("platform", "tcp listen failed: ", std::strerror(errno));
        return nullptr;
    }
    int bound_port = port;
    if (port == 0) {
        sockaddr_in actual{};
        socklen_t length = sizeof(actual);
        if (::getsockname(guard.get(), reinterpret_cast<sockaddr *>(&actual), &length) == 0) {
            bound_port = ntohs(actual.sin_port);
        }
    }
    return std::make_unique<PosixTcpListener>(std::move(guard), bound_port);
}

auto tcp_connect(const std::string &address, int port, int timeout_ms)
    -> std::unique_ptr<TcpStream> {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *results = nullptr;
    const int code = ::getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints,
                                   &results);
    if (code != 0 || results == nullptr) {
        AURORA_LOG_ERROR("platform", "tcp resolve failed for: ", address);
        return nullptr;
    }
    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> list{results, &::freeaddrinfo};

    for (addrinfo *item = list.get(); item != nullptr; item = item->ai_next) {
        int fd = ::socket(item->ai_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0) {
            continue;
        }
        SocketFd guard{fd};
        const int rc = ::connect(guard.get(), item->ai_addr, item->ai_addrlen);
        if (rc == 0) {
            return std::make_unique<PosixTcpStream>(std::move(guard));
        }
        if (errno != EINPROGRESS) {
            continue;
        }
        pollfd events{guard.get(), POLLOUT, 0};
        if (::poll(&events, 1, timeout_ms) <= 0) {
            continue;
        }
        int so_error = 0;
        socklen_t length = sizeof(so_error);
        if (::getsockopt(guard.get(), SOL_SOCKET, SO_ERROR, &so_error, &length) != 0 ||
            so_error != 0) {
            continue;
        }
        // 连上后回到阻塞语义：转发泵的读用 poll+MSG_DONTWAIT，写靠 EAGAIN 续写，
        // 保持非阻塞 fd 反而让 send 的部分写路径更早触发，这里显式清回阻塞。
        const int flags = ::fcntl(guard.get(), F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(guard.get(), F_SETFL, flags & ~O_NONBLOCK);
        }
        return std::make_unique<PosixTcpStream>(std::move(guard));
    }
    AURORA_LOG_ERROR("platform", "tcp connect failed for: ", address,
                     ":", std::to_string(port));
    return nullptr;
}

}  // namespace borealis::platform
