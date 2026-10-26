// ============================================================
// Windows 跳链本地字节对（src/platform/win/hop_pipe.cpp）
// ------------------------------------------------------------
// platform::make_hop_pair 的 Winsock2 腿（`SPEC.FEAT.CONN.03` 跳板机，稿 D10）。
// 取「回环 TCP 一对」而不是 AF_UNIX：Win10 之前的 AF_UNIX 支持面不完整，而本仓主通道
// 要覆盖到没有 unix domain socket 的构建机（稿 D10 的选型理由）。
//
// **本腿已编译并实测**（2026-10-27，MSVC＋Win32 主通道）：`itest_ssh_proxy_jump` 的一跳／
// 两跳／归因／主机密钥／SFTP／隧道／密钥推送与共享链路各案都跑在这对回环句柄上，稿
// `codespec/CONN03_PROXYJUMP.draft.md` §6 里「Win32 全链未编译」那条在册边界就此收口。
// 裸 socket 知识不外溢：接口见 platform/hop_pipe.h（AGENTS.md §4.5 第 23 条）。
// ============================================================

#include "platform/hop_pipe.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <windows.h>

#include <ws2tcpip.h>

#include <array>
#include <cstring>
#include <string>
#include <utility>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief 写方向的等待上限（与 POSIX 腿同档：本地管道一拍写得进去）。
constexpr int kWriteTimeoutMs = 1000;

/// @brief 进程级 WSAStartup 守卫（同 platform/win/tcp.cpp 的形态；WSACleanup 不调）。
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
    /// @brief 交还所有权而不关闭：调用方接手的是活的套接字。
    [[nodiscard]] auto release() -> SOCKET {
        const auto socket = socket_;
        socket_ = INVALID_SOCKET;
        return socket;
    }
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

class WinHopPipeEnd final : public HopPipeEnd {
  public:
    explicit WinHopPipeEnd(SocketHandle socket) : socket_(std::move(socket)) {}

    auto read(std::span<std::uint8_t> out, int timeout_ms) -> HopPipeRead override {
        if (!socket_.valid() || out.empty()) {
            return {HopPipeReadOutcome::Timeout, 0};
        }
        WSAPOLLFD events{socket_.get(), static_cast<SHORT>(POLLRDNORM), 0};
        const int ready = ::WSAPoll(&events, 1, timeout_ms);
        if (ready == 0) {
            return {HopPipeReadOutcome::Timeout, 0};
        }
        if (ready < 0) {
            AURORA_LOG_WARN("platform", "hop pipe poll failed: ", last_error_text());
            return {HopPipeReadOutcome::Error, 0};
        }
        const auto got =
            ::recv(socket_.get(), reinterpret_cast<char *>(out.data()),
                   static_cast<int>(out.size()), 0);
        if (got > 0) {
            return {HopPipeReadOutcome::Data, static_cast<std::size_t>(got)};
        }
        if (got == 0) {
            return {HopPipeReadOutcome::Closed, 0};
        }
        AURORA_LOG_WARN("platform", "hop pipe read failed: ", last_error_text());
        return {HopPipeReadOutcome::Error, 0};
    }

    auto write(std::span<const std::uint8_t> bytes) -> bool override {
        if (!socket_.valid()) {
            return false;
        }
        auto remaining = bytes;
        while (!remaining.empty()) {
            WSAPOLLFD events{socket_.get(), static_cast<SHORT>(POLLWRNORM), 0};
            const int ready = ::WSAPoll(&events, 1, kWriteTimeoutMs);
            if (ready < 0) {
                AURORA_LOG_WARN("platform", "hop pipe poll(POLLWRNORM) failed: ",
                                last_error_text());
                return false;
            }
            if (ready == 0) {
                AURORA_LOG_WARN("platform", "hop pipe write timed out");
                return false;
            }
            const auto sent = ::send(socket_.get(),
                                     reinterpret_cast<const char *>(remaining.data()),
                                     static_cast<int>(remaining.size()), 0);
            if (sent == SOCKET_ERROR) {
                AURORA_LOG_WARN("platform", "hop pipe write failed: ", last_error_text());
                return false;
            }
            remaining = remaining.subspan(static_cast<std::size_t>(sent));
        }
        return true;
    }

    auto close() -> void override { socket_.reset(); }

    [[nodiscard]] auto handle() const noexcept -> std::uintptr_t override {
        return socket_.valid() ? static_cast<std::uintptr_t>(socket_.get())
                               : kInvalidHopPipeHandle;
    }

  private:
    SocketHandle socket_;
};

/// @brief 回环监听点：端口 0 由内核择定，只为拿到一对已连上的句柄。
[[nodiscard]] auto make_loopback_pair() -> std::optional<std::array<SOCKET, 2>> {
    SocketHandle listener{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!listener.valid()) {
        AURORA_LOG_WARN("platform", "cannot create hop pipe listener: ", last_error_text());
        return std::nullopt;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener.get(), reinterpret_cast<const sockaddr *>(&address),
               static_cast<int>(sizeof(address))) == SOCKET_ERROR) {
        AURORA_LOG_WARN("platform", "cannot bind hop pipe listener: ", last_error_text());
        return std::nullopt;
    }
    if (::listen(listener.get(), 1) == SOCKET_ERROR) {
        AURORA_LOG_WARN("platform", "cannot listen hop pipe: ", last_error_text());
        return std::nullopt;
    }
    auto length = static_cast<int>(sizeof(address));
    if (::getsockname(listener.get(), reinterpret_cast<sockaddr *>(&address), &length) ==
        SOCKET_ERROR) {
        AURORA_LOG_WARN("platform", "cannot read hop pipe listener port: ", last_error_text());
        return std::nullopt;
    }
    SocketHandle client{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!client.valid()) {
        AURORA_LOG_WARN("platform", "cannot create hop pipe client: ", last_error_text());
        return std::nullopt;
    }
    // 回环 connect 立即完成（同机、无中间路由）；非阻塞 connect 的等待面本期不需要。
    if (::connect(client.get(), reinterpret_cast<const sockaddr *>(&address),
                  static_cast<int>(sizeof(address))) == SOCKET_ERROR) {
        AURORA_LOG_WARN("platform", "cannot connect hop pipe to itself: ", last_error_text());
        return std::nullopt;
    }
    auto accepted = SocketHandle{::accept(listener.get(), nullptr, nullptr)};
    if (!accepted.valid()) {
        AURORA_LOG_WARN("platform", "cannot accept hop pipe peer: ", last_error_text());
        return std::nullopt;
    }
    // 监听点使命完成：`listener` 出作用域即关（留会占着一枚端口，且本件只有一对连接）。
    // 两端按**移交**交出去——`reset()` 是先关再置空，那样交出去的是一双已关的句柄，
    // 对端 `WSAPoll` 立刻 `WSAENOTSOCK`（10038），整条跳链停在 `ssh_connect` 之前。
    auto out = std::array<SOCKET, 2>{accepted.release(), client.release()};
    return out;
}

}  // namespace

auto make_hop_pair() -> std::optional<HopPair> {
    if (!winsock_ready()) {
        AURORA_LOG_WARN("platform", "winsock is not ready; hop pipe refused");
        return std::nullopt;
    }
    const auto sockets = make_loopback_pair();
    if (!sockets) {
        return std::nullopt;
    }
    // 0 是「不走 seed」的哨兵（见 hop_pipe.h 头部）：Winsock 句柄不会是 0，
    // 仍按 POSIX 腿同一条判据兜住，免得库把哨兵当成句柄。
    if ((*sockets)[0] == 0 || (*sockets)[1] == 0) {
        AURORA_LOG_ERROR("platform", "hop pipe handle collides with the seed sentinel; refused");
        return std::nullopt;
    }
    HopPair pair;
    pair.session_end = std::make_unique<WinHopPipeEnd>(SocketHandle{(*sockets)[0]});
    pair.relay_end = std::make_unique<WinHopPipeEnd>(SocketHandle{(*sockets)[1]});
    return pair;
}

}  // namespace borealis::platform
