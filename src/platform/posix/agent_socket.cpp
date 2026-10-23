// ============================================================
// POSIX 本机 ssh-agent 字节通道（src/platform/posix/agent_socket.cpp）
// ------------------------------------------------------------
// platform::agent_connect 的 POSIX 腿（SPEC.FEAT.CONN.02 的 agent 转发）。
// unix socket 与 `SSH_AUTH_SOCK` 是平台假设，接口见 platform/agent_socket.h。
// ============================================================

#include "platform/agent_socket.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief 写方向的等待上限：agent 协议是「一问一答」的小消息，正常远小于此；
///        超时即视为 agent 卡死，中继据此先断这一条反向通道。
constexpr int kWriteTimeoutMs = 1000;

/// @brief RAII 句柄：-1 表示未持有。
class UnixFd {
  public:
    explicit UnixFd(int fd = -1) : fd_(fd) {}
    ~UnixFd() { reset(); }

    UnixFd(const UnixFd &) = delete;
    auto operator=(const UnixFd &) -> UnixFd & = delete;
    UnixFd(UnixFd &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    auto operator=(UnixFd &&) -> UnixFd & = delete;

    [[nodiscard]] auto get() const -> int { return fd_; }
    [[nodiscard]] auto valid() const -> bool { return fd_ >= 0; }
    auto reset(int fd = -1) -> void {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_{-1};
};

class PosixAgentSocket final : public AgentSocket {
  public:
    explicit PosixAgentSocket(UnixFd fd) : fd_(std::move(fd)) {}

    auto read(std::span<std::uint8_t> out, int timeout_ms) -> AgentRead override {
        if (!fd_.valid() || out.empty()) {
            return {AgentReadOutcome::Timeout, 0};
        }
        pollfd events{fd_.get(), POLLIN, 0};
        const int ready = ::poll(&events, 1, timeout_ms);
        if (ready == 0) {
            return {AgentReadOutcome::Timeout, 0};
        }
        if (ready < 0) {
            if (errno == EINTR) {
                return {AgentReadOutcome::Timeout, 0};
            }
            AURORA_LOG_WARN("platform", "agent socket poll failed: ", std::strerror(errno));
            return {AgentReadOutcome::Error, 0};
        }
        const auto got = ::recv(fd_.get(), out.data(), out.size(), 0);
        if (got == 0) {
            return {AgentReadOutcome::Closed, 0};
        }
        if (got < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                return {AgentReadOutcome::Timeout, 0};
            }
            AURORA_LOG_WARN("platform", "agent socket read failed: ", std::strerror(errno));
            return {AgentReadOutcome::Error, 0};
        }
        return {AgentReadOutcome::Data, static_cast<std::size_t>(got)};
    }

    auto write(std::span<const std::uint8_t> bytes) -> bool override {
        if (!fd_.valid()) {
            return false;
        }
        auto remaining = bytes;
        while (!remaining.empty()) {
            pollfd events{fd_.get(), POLLOUT, 0};
            const int ready = ::poll(&events, 1, kWriteTimeoutMs);
            if (ready < 0 && errno != EINTR) {
                AURORA_LOG_WARN("platform", "agent socket poll(POLLOUT) failed: ",
                                std::strerror(errno));
                return false;
            }
            if (ready == 0) {
                AURORA_LOG_WARN("platform", "agent socket write timed out");
                return false;
            }
            const auto sent = ::send(fd_.get(), remaining.data(), remaining.size(), MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;
                }
                AURORA_LOG_WARN("platform", "agent socket write failed: ", std::strerror(errno));
                return false;
            }
            remaining = remaining.subspan(static_cast<std::size_t>(sent));
        }
        return true;
    }

    auto close() -> void override { fd_.reset(); }

  private:
    UnixFd fd_;
};

}  // namespace

auto agent_connect() -> std::unique_ptr<AgentSocket> {
    const char *path = std::getenv("SSH_AUTH_SOCK");
    if (path == nullptr || *path == '\0') {
        return nullptr;  // 没有 agent 是常态（未开 ssh-agent），不记日志。
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto length = std::strlen(path);
    if (length >= sizeof(address.sun_path)) {
        AURORA_LOG_WARN("platform", "SSH_AUTH_SOCK path is too long: ", length, " bytes");
        return nullptr;
    }
    std::memcpy(address.sun_path, path, length + 1);

    UnixFd fd{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!fd.valid()) {
        AURORA_LOG_WARN("platform", "cannot create agent socket: ", std::strerror(errno));
        return nullptr;
    }
    if (::connect(fd.get(), reinterpret_cast<const sockaddr *>(&address),
                  static_cast<socklen_t>(sizeof(address))) != 0) {
        AURORA_LOG_WARN("platform", "cannot connect to agent at '", path,
                        "': ", std::strerror(errno));
        return nullptr;
    }
    return std::make_unique<PosixAgentSocket>(std::move(fd));
}

}  // namespace borealis::platform
