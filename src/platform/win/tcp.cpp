// ============================================================
// Windows TCP 转发腿实现（src/platform/win/tcp.cpp）
// ------------------------------------------------------------
// platform::tcp_listen / tcp_connect 的 Winsock2 腿（SPEC.FEAT.CONN.08）。
// 裸 socket 知识不外溢：接口见 platform/tcp.h（AGENTS.md §4.5 第 23 条）。
// 与 posix 腿同构；WSAStartup 由进程级守卫做一次，WSACleanup 不调（与
// ConPTY 腿一致：生命周期随进程，退出时由 OS 回收）。
// ============================================================

#include "platform/tcp.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <windows.h>

#include <ws2tcpip.h>

#include <cstring>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief 进程级 WSAStartup 守卫（首次构造生效，句柄版本随取随用）。
class WinsockInit {
  public:
    WinsockInit() {
        WSADATA data{};
        ok_ = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    [[nodiscard]] auto ok() const -> bool { return ok_; }

  private:
    bool ok_{false};
};

[[nodiscard]] auto winsock_ready() -> bool {
    static WinsockInit init;
    return init.ok();
}

/// @brief RAII 句柄：INVALID_SOCKET 表示未持有。
class SocketHandle {
  public:
    explicit SocketHandle(SOCKET socket = INVALID_SOCKET) : socket_(socket) {}
    ~SocketHandle() { reset(); }

    SocketHandle(const SocketHandle &) = delete;
    auto operator=(const SocketHandle &) -> SocketHandle & = delete;
    SocketHandle(SocketHandle &&other) noexcept : socket_(other.socket_) {
        other.socket_ = INVALID_SOCKET;
    }
    auto operator=(SocketHandle &&) -> SocketHandle & = delete;

    [[nodiscard]] auto get() const -> SOCKET { return socket_; }
    [[nodiscard]] auto valid() const -> bool { return socket_ != INVALID_SOCKET; }
    auto reset(SOCKET socket = INVALID_SOCKET) -> void {
        if (socket_ != INVALID_SOCKET) {
            ::closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
        socket_ = socket;
    }

  private:
    SOCKET socket_{INVALID_SOCKET};
};

/// @brief 把点分 IPv4 写进 sockaddr_in；非法字面量回 false（仅认字面量，域名
///        监听不在本期口径）。
[[nodiscard]] auto fill_ipv4(const std::string &address, sockaddr_in &out) -> bool {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    const auto *text = address.empty() ? "0.0.0.0" : address.c_str();
    return ::InetPtonA(AF_INET, text, &out.sin_addr) == 1;
}

/// @brief 最近一次 Winsock 错误码转文本。
[[nodiscard]] auto last_error_text() -> std::string {
    char buffer[256] = {};
    const DWORD code = static_cast<DWORD>(::WSAGetLastError());
    const DWORD length = ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                                              FORMAT_MESSAGE_IGNORE_INSERTS,
                                          nullptr, code, 0, buffer, sizeof(buffer) - 1, nullptr);
    if (length == 0) {
        return "winsock error " + std::to_string(code);
    }
    return std::string{buffer, length};
}

class WinTcpStream final : public TcpStream {
  public:
    explicit WinTcpStream(SocketHandle socket) : socket_(std::move(socket)) {}

    auto read(std::span<std::uint8_t> out, int timeout_ms) -> TcpRead override {
        if (!socket_.valid() || out.empty()) {
            return {TcpReadOutcome::Timeout, 0};
        }
        WSAPOLLFD events{socket_.get(), static_cast<SHORT>(POLLRDNORM), 0};
        const int ready = ::WSAPoll(&events, 1, timeout_ms);
        if (ready == 0) {
            return {TcpReadOutcome::Timeout, 0};
        }
        if (ready < 0) {
            AURORA_LOG_ERROR("platform", "tcp poll failed: ", last_error_text());
            return {TcpReadOutcome::Error, 0};
        }
        const int n = ::recv(socket_.get(), reinterpret_cast<char *>(out.data()),
                             static_cast<int>(out.size()), 0);
        if (n > 0) {
            return {TcpReadOutcome::Data, static_cast<std::size_t>(n)};
        }
        if (n == 0) {
            return {TcpReadOutcome::Closed, 0};
        }
        AURORA_LOG_ERROR("platform", "tcp recv failed: ", last_error_text());
        return {TcpReadOutcome::Error, 0};
    }

    auto write(std::span<const std::uint8_t> bytes) -> bool override {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            if (!socket_.valid()) {
                return false;
            }
            const int n = ::send(socket_.get(), reinterpret_cast<const char *>(bytes.data() + sent),
                                 static_cast<int>(bytes.size() - sent), 0);
            if (n > 0) {
                sent += static_cast<std::size_t>(n);
                continue;
            }
            const int code = ::WSAGetLastError();
            if (code == WSAEWOULDBLOCK || code == WSAEINTR) {
                WSAPOLLFD events{socket_.get(), static_cast<SHORT>(POLLWRNORM), 0};
                if (::WSAPoll(&events, 1, 5000) <= 0) {
                    AURORA_LOG_ERROR("platform", "tcp send blocked out");
                    return false;
                }
                continue;
            }
            AURORA_LOG_ERROR("platform", "tcp send failed: ", last_error_text());
            return false;
        }
        return true;
    }

    auto close() -> void override { socket_.reset(); }

    [[nodiscard]] auto peer_address() const -> std::string override {
        sockaddr_in peer{};
        int length = sizeof(peer);
        if (!socket_.valid() ||
            ::getpeername(socket_.get(), reinterpret_cast<sockaddr *>(&peer), &length) != 0) {
            return {};
        }
        char text[INET_ADDRSTRLEN] = {};
        if (::InetNtopA(AF_INET, &peer.sin_addr, text, sizeof(text)) == nullptr) {
            return {};
        }
        return text;
    }

    [[nodiscard]] auto peer_port() const -> int override {
        sockaddr_in peer{};
        int length = sizeof(peer);
        if (!socket_.valid() ||
            ::getpeername(socket_.get(), reinterpret_cast<sockaddr *>(&peer), &length) != 0) {
            return 0;
        }
        return ntohs(peer.sin_port);
    }

  private:
    SocketHandle socket_;
};

class WinTcpListener final : public TcpListener {
  public:
    WinTcpListener(SocketHandle socket, int bound_port)
        : socket_(std::move(socket)), bound_port_{bound_port} {}

    auto accept(int timeout_ms) -> std::unique_ptr<TcpStream> override {
        if (!socket_.valid()) {
            return nullptr;
        }
        WSAPOLLFD events{socket_.get(), static_cast<SHORT>(POLLRDNORM), 0};
        if (::WSAPoll(&events, 1, timeout_ms) <= 0) {
            return nullptr;
        }
        const SOCKET client = ::accept(socket_.get(), nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            return nullptr;
        }
        return std::make_unique<WinTcpStream>(SocketHandle{client});
    }

    [[nodiscard]] auto bound_port() const -> int override { return bound_port_; }

    auto close() -> void override { socket_.reset(); }

  private:
    SocketHandle socket_;
    int bound_port_{0};
};

}  // namespace

auto tcp_listen(const std::string &bind_address, int port) -> std::unique_ptr<TcpListener> {
    if (!winsock_ready()) {
        AURORA_LOG_ERROR("platform", "winsock startup failed");
        return nullptr;
    }
    sockaddr_in addr{};
    if (!fill_ipv4(bind_address, addr)) {
        AURORA_LOG_ERROR("platform", "tcp listen bad bind address: ", bind_address);
        return nullptr;
    }
    addr.sin_port = htons(static_cast<std::uint16_t>(port));

    SOCKET handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == INVALID_SOCKET) {
        AURORA_LOG_ERROR("platform", "tcp socket failed: ", last_error_text());
        return nullptr;
    }
    SocketHandle guard{handle};
    const BOOL reuse = TRUE;
    ::setsockopt(guard.get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse),
                 sizeof(reuse));
    if (::bind(guard.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
        AURORA_LOG_ERROR("platform", "tcp bind failed on port: ", std::to_string(port),
                         " (", last_error_text(), ")");
        return nullptr;
    }
    if (::listen(guard.get(), 16) == SOCKET_ERROR) {
        AURORA_LOG_ERROR("platform", "tcp listen failed: ", last_error_text());
        return nullptr;
    }
    int bound_port = port;
    if (port == 0) {
        sockaddr_in actual{};
        int length = sizeof(actual);
        if (::getsockname(guard.get(), reinterpret_cast<sockaddr *>(&actual), &length) == 0) {
            bound_port = ntohs(actual.sin_port);
        }
    }
    return std::make_unique<WinTcpListener>(std::move(guard), bound_port);
}

auto tcp_connect(const std::string &address, int port, int timeout_ms)
    -> std::unique_ptr<TcpStream> {
    if (!winsock_ready()) {
        AURORA_LOG_ERROR("platform", "winsock startup failed");
        return nullptr;
    }
    addrinfoA hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfoA *results = nullptr;
    const int code = ::getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints,
                                   &results);
    if (code != 0 || results == nullptr) {
        AURORA_LOG_ERROR("platform", "tcp resolve failed for: ", address);
        return nullptr;
    }
    std::unique_ptr<addrinfoA, decltype(&::freeaddrinfo)> list{results, &::freeaddrinfo};

    for (addrinfoA *item = list.get(); item != nullptr; item = item->ai_next) {
        SOCKET handle = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (handle == INVALID_SOCKET) {
            continue;
        }
        SocketHandle guard{handle};
        // Winsock 默认阻塞：超时 connect 需显式非阻塞进出（WSAEWOULDBLOCK 只
        // 会在非阻塞套接字上出现）。
        const u_long nonblocking = 1;
        ::ioctlsocket(guard.get(), FIONBIO, &nonblocking);
        const int rc = ::connect(guard.get(), item->ai_addr, static_cast<int>(item->ai_addrlen));
        if (rc != 0 && ::WSAGetLastError() != WSAEWOULDBLOCK) {
            continue;
        }
        if (rc != 0) {
            WSAPOLLFD events{guard.get(), static_cast<SHORT>(POLLWRNORM), 0};
            if (::WSAPoll(&events, 1, timeout_ms) <= 0) {
                continue;
            }
            int so_error = 0;
            int length = sizeof(so_error);
            if (::getsockopt(guard.get(), SOL_SOCKET, SO_ERROR,
                             reinterpret_cast<char *>(&so_error), &length) != 0 ||
                so_error != 0) {
                continue;
            }
        }
        const u_long blocking = 0;
        ::ioctlsocket(guard.get(), FIONBIO, &blocking);
        return std::make_unique<WinTcpStream>(std::move(guard));
    }
    AURORA_LOG_ERROR("platform", "tcp connect failed for: ", address,
                     ":", std::to_string(port));
    return nullptr;
}

}  // namespace borealis::platform
