// ============================================================
// POSIX 跳链本地字节对（src/platform/posix/hop_pipe.cpp）
// ------------------------------------------------------------
// platform::make_hop_pair 的 POSIX 腿（`SPEC.FEAT.CONN.03` 跳板机，稿 D10）：
// `socketpair(AF_UNIX, SOCK_STREAM)` 是探针 P1 实测跑通的形态（稿 F11），两端的
// 读/写语义与一条流式 socket 一致。Win32 腿见 platform/win/hop_pipe.cpp。
// ============================================================

#include "platform/hop_pipe.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <utility>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief 写方向的等待上限：跳链两端各是一根本地管道，正常一拍就写得进去；
///        超时即视为对端（SSH 会话那一侧）卡死，中继据此收本跳。
constexpr int kWriteTimeoutMs = 1000;

/// @brief RAII 句柄：-1 表示未持有（同 posix/agent_socket.cpp 的形态）。
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
    auto reset(int fd = -1) -> void {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_{-1};
};

class PosixHopPipeEnd final : public HopPipeEnd {
  public:
    explicit PosixHopPipeEnd(SocketFd fd) : fd_(std::move(fd)) {}

    auto read(std::span<std::uint8_t> out, int timeout_ms) -> HopPipeRead override {
        if (!fd_.valid() || out.empty()) {
            return {HopPipeReadOutcome::Timeout, 0};
        }
        pollfd events{fd_.get(), POLLIN, 0};
        const int ready = ::poll(&events, 1, timeout_ms);
        if (ready == 0) {
            return {HopPipeReadOutcome::Timeout, 0};
        }
        if (ready < 0) {
            if (errno == EINTR) {
                return {HopPipeReadOutcome::Timeout, 0};
            }
            AURORA_LOG_WARN("platform", "hop pipe poll failed: ", std::strerror(errno));
            return {HopPipeReadOutcome::Error, 0};
        }
        return recv_into(out);
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
                AURORA_LOG_WARN("platform", "hop pipe poll(POLLOUT) failed: ",
                                std::strerror(errno));
                return false;
            }
            if (ready == 0) {
                AURORA_LOG_WARN("platform", "hop pipe write timed out");
                return false;
            }
            const auto sent = ::send(fd_.get(), remaining.data(), remaining.size(), MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;
                }
                AURORA_LOG_WARN("platform", "hop pipe write failed: ", std::strerror(errno));
                return false;
            }
            remaining = remaining.subspan(static_cast<std::size_t>(sent));
        }
        return true;
    }

    auto close() -> void override { fd_.reset(); }

    [[nodiscard]] auto handle() const noexcept -> std::uintptr_t override {
        return fd_.valid() ? static_cast<std::uintptr_t>(fd_.get()) : kInvalidHopPipeHandle;
    }

  private:
    /// @brief poll 已判定可读；这里的 0/负值各自有档（EINTR 与 EAGAIN 回 Timeout）。
    [[nodiscard]] auto recv_into(std::span<std::uint8_t> out) -> HopPipeRead {
        const auto got = ::recv(fd_.get(), out.data(), out.size(), 0);
        if (got == 0) {
            return {HopPipeReadOutcome::Closed, 0};  // 对端已关：这一跳的字节流到尽头。
        }
        if (got < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                return {HopPipeReadOutcome::Timeout, 0};
            }
            AURORA_LOG_WARN("platform", "hop pipe read failed: ", std::strerror(errno));
            return {HopPipeReadOutcome::Error, 0};
        }
        return {HopPipeReadOutcome::Data, static_cast<std::size_t>(got)};
    }

    SocketFd fd_;
};

}  // namespace

auto make_hop_pair() -> std::optional<HopPair> {
    auto raw = std::array<int, 2>{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, raw.data()) != 0) {
        AURORA_LOG_WARN("platform", "cannot create hop socketpair: ", std::strerror(errno));
        return std::nullopt;
    }
    auto first = SocketFd{raw[0]};
    auto second = SocketFd{raw[1]};
    // 0 是「不走 seed」的哨兵（见 hop_pipe.h 头部）：真拿到 0 就当造对失败，
    // 绝不让调用方把哨兵当成句柄递给 libssh。
    if (raw[0] == 0 || raw[1] == 0) {
        AURORA_LOG_ERROR("platform", "hop socketpair took over file descriptor 0; refused");
        return std::nullopt;
    }
    HopPair pair;
    pair.session_end = std::make_unique<PosixHopPipeEnd>(std::move(first));
    pair.relay_end = std::make_unique<PosixHopPipeEnd>(std::move(second));
    return pair;
}

}  // namespace borealis::platform
