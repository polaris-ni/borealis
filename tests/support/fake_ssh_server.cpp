// ============================================================
// 进程内 libssh 假 SSH 服务器实现（tests/support/fake_ssh_server.cpp）
// ------------------------------------------------------------
// 为什么造它：本沙箱没有 sshd，`SPEC.FEAT.CONN.02`／CONN.04／CONN.08 的真协议
// 端到端证人一直缺位。libssh 的 server 腿在本机 vcpkg 构建档里是开着的
// （`WITH_SERVER`，见 SPECIFICATIONS §7.145②），故用同一份库在测试进程内起一台
// 最小服务器：真 KEX、真加密、真通道多路复用，测试只多一条 loopback 连接。
//
// 线程与生命周期：
//   - 受理线程按 100 ms 分片等监听句柄可读，`ssh_bind_accept_fd` 之后把 session 交给
//     新的工作线程（libssh 会话不跨线程）；
//   - 工作线程先 `ssh_handle_key_exchange`，再把 session 挂到自己的 `ssh_event` 上，
//     每 10 ms 一拍；认证/开通道/PTY/shell 全走 server callbacks；
//   - 回调表与通道回调表由工作线程持有，必须比会话/通道活得久（libssh 存的是指针）；
//   - 析构：置停止位 → join 受理线程 → shutdown 各连接 → join 工作线程（dopoll 每拍
//     查停止位）→ 释放 session/通道 → 释放 bind（bind 拥有导入的主机密钥）。
//
// 平台边界：裸 socket 只出现在文件内的**接缝**里（`select`/`WSAPoll`、`close`/
// `closesocket`、`fcntl(O_NONBLOCK)`/`ioctlsocket(FIONBIO)`、`MSG_NOSIGNAL` 一档 Winsock
// 根本没有），接缝之外的核心两通道编同一份。句柄类型取 libssh 自己的 `socket_t`
// （POSIX＝`int`、Win32＝`SOCKET`），无效哨兵取同一库的 `SSH_INVALID_SOCKET`（POSIX 档
// 就是 `-1`，与本文件登记时的写法逐位同值）。接缝留在本文件而不是用
// `platform::TcpListener`：后者按裁决 7.96 只给「受理后的流」，接口上没有 fd getter，
// 而 `ssh_bind_accept_fd` 收的正是裸句柄——测试设施不该为它扩生产 API 面。
// ============================================================

#include "fake_ssh_server.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <windows.h>

#include <ws2tcpip.h>

#else

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

#endif  // _WIN32

#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>

namespace borealis::testing {

namespace {

/// @brief 受理线程的分片（毫秒）：也是析构叫停延迟的上界。
constexpr int kAcceptSliceMs = 100;

/// @brief 工作线程的事件轮询分片：决定 `end_shell()`／析构叫停的延迟上界。
constexpr int kPollSliceMs = 10;

/// @brief 跳板腿：连转发目的地（永远是本机 loopback 上的另一台假服务器）的等待上界。
constexpr int kForwardConnectTimeoutMs = 2000;

/// @brief 监听点：句柄 + 内核择定的端口（`getsockname` 取不到时 port 记 `-1`，
///        `ok()` 因此为假——监听起了却报不出端口，对本设施就是失败）。
struct Listener {
    socket_t fd{SSH_INVALID_SOCKET};
    int port{0};
};

/// @brief `read_peer()` 的分档。None＝「这一拍没货」（含被信号打断与暂态无数据），
///        工作线程下一拍再看；Closed＝目的地有序收摊；Error＝硬错（复位、连不上）。
///        后两档在核心里同一段处理：给客户端的转发通道回 EOF 并收掉目的地句柄。
enum class PeerReadOutcome : std::uint8_t { Data, None, Closed, Error };

struct PeerRead {
    PeerReadOutcome outcome{PeerReadOutcome::Error};
    std::size_t bytes{0};
};

#ifdef _WIN32

// ---- Winsock 腿 --------------------------------------------------------------
// 与 `src/platform/win/tcp.cpp` 同形的那几处（WSAStartup 守卫、`closesocket`、
// `WSAPoll`、`ioctlsocket`）刻意照抄而非复用该 TU：那是生产代码的平台腿，
// 接缝在 `platform/` 之外没有可复用的公共入口（`TcpStream` 不吐句柄）。

/// @brief 进程级 WSAStartup 守卫。本设施不经装配层启动，生产腿的那次初始化可能压根
///        没跑到；`WSACleanup` 不调（随进程退出由 OS 回收），与生产腿口径一致。
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

auto close_socket(socket_t fd) -> void {
    if (fd != SSH_INVALID_SOCKET) {
        static_cast<void>(::closesocket(fd));
    }
}

auto shutdown_both(socket_t fd) -> void {
    if (fd != SSH_INVALID_SOCKET) {
        static_cast<void>(::shutdown(fd, SD_BOTH));
    }
}

[[nodiscard]] auto accept_one(socket_t listener) -> socket_t {
    return ::accept(listener, nullptr, nullptr);
}

/// @brief 分片等句柄可读。Winsock 的 `select` 只认 `fd_set`（要自己填），`WSAPoll` 直接
///        收毫秒且没有 `fd+1` 那个参数——本腿取后者，语义与 POSIX 腿的 `select` 一致。
[[nodiscard]] auto wait_readable(socket_t fd, int timeout_ms) -> bool {
    WSAPOLLFD event{fd, static_cast<SHORT>(POLLRDNORM), 0};
    const auto ready = ::WSAPoll(&event, 1, timeout_ms);
    if (ready > 0) {
        return true;
    }
    if (ready < 0 && ::WSAGetLastError() == WSAEINTR) {
        return false;  // 被中断＝这一拍没货，调用方回到停止位检查。
    }
    return false;
}

[[nodiscard]] auto wait_writable(socket_t fd, int timeout_ms) -> bool {
    WSAPOLLFD event{fd, static_cast<SHORT>(POLLWRNORM), 0};
    return ::WSAPoll(&event, 1, timeout_ms) > 0;
}

/// @brief 跳板腿：把一段字节全量写进目的地句柄（吞掉部分写；Winsock 没有 SIGPIPE，
///        故 POSIX 腿那枚 `MSG_NOSIGNAL` 在本腿不存在，标志位给 0）。
///        @return false＝目的地收摊或出错，这条转发通道到头了。
[[nodiscard]] auto send_all(socket_t fd, std::string_view bytes) -> bool {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = static_cast<int>(bytes.size() - offset);
        const auto sent = ::send(fd, bytes.data() + offset, remaining, 0);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && ::WSAGetLastError() == WSAEINTR) {
            continue;
        }
        return false;
    }
    return true;
}

/// @brief 跳板腿：连到客户端要求的目的地（仅 IPv4 字面量；转发目的在本仓证人里恒为
///        127.0.0.1 上的另一台假服务器）。失败回 `SSH_INVALID_SOCKET`。
[[nodiscard]] auto connect_to(const char *address, int port) -> socket_t {
    if (!winsock_ready()) {
        return SSH_INVALID_SOCKET;
    }
    const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SSH_INVALID_SOCKET) {
        return SSH_INVALID_SOCKET;
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(static_cast<u_short>(port));
    if (::InetPtonA(AF_INET, address, &target.sin_addr) != 1) {
        close_socket(fd);
        return SSH_INVALID_SOCKET;
    }
    // 非阻塞 connect + 写就绪：拿得到「连不上」而不是「卡住这台工作线程」。
    // Winsock 没有 `fcntl(O_NONBLOCK)`，等价入口是 `ioctlsocket(FIONBIO, 1)`。
    auto nonblocking = static_cast<unsigned long>(1);
    static_cast<void>(::ioctlsocket(fd, FIONBIO, &nonblocking));
    const int rc = ::connect(fd, reinterpret_cast<const sockaddr *>(&target),
                             static_cast<int>(sizeof(target)));
    auto connected = rc == 0;
    if (!connected && ::WSAGetLastError() != WSAEWOULDBLOCK) {
        close_socket(fd);
        return SSH_INVALID_SOCKET;
    }
    if (!connected) {
        connected = wait_writable(fd, kForwardConnectTimeoutMs);
        if (connected) {
            auto so_error = int{0};
            auto length = int{sizeof(so_error)};
            static_cast<void>(::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                                          reinterpret_cast<char *>(&so_error), &length));
            connected = so_error == 0;
        }
    }
    if (!connected) {
        close_socket(fd);
        return SSH_INVALID_SOCKET;
    }
    // 交回阻塞语义：`read_peer()` 用 `WSAPoll` 判可读之后才 `recv`（与 POSIX 腿同一档）。
    nonblocking = 0;
    static_cast<void>(::ioctlsocket(fd, FIONBIO, &nonblocking));
    return fd;
}

/// @brief 跳板腿：目的地 → 转发通道这一向的单拍读（先 0 超时探一下，绝不阻塞工作线程）。
[[nodiscard]] auto read_peer(socket_t fd, std::span<char> out) -> PeerRead {
    if (out.empty()) {
        return {PeerReadOutcome::None, 0};
    }
    WSAPOLLFD event{fd, static_cast<SHORT>(POLLRDNORM), 0};
    if (::WSAPoll(&event, 1, 0) <= 0) {
        return {PeerReadOutcome::None, 0};
    }
    const auto got = ::recv(fd, out.data(), static_cast<int>(out.size()), 0);
    if (got > 0) {
        return {PeerReadOutcome::Data, static_cast<std::size_t>(got)};
    }
    if (got == 0) {
        return {PeerReadOutcome::Closed, 0};
    }
    const auto code = ::WSAGetLastError();
    if (code == WSAEWOULDBLOCK || code == WSAEINTR) {
        return {PeerReadOutcome::None, 0};
    }
    return {PeerReadOutcome::Error, 0};
}

[[nodiscard]] auto open_loopback_listener() -> Listener {
    if (!winsock_ready()) {
        return {};
    }
    const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SSH_INVALID_SOCKET) {
        return {};
    }
    // 本仓 Win32 侧监听点的既有口径（`src/platform/win/tcp.cpp`）：`SO_EXCLUSIVEADDRUSE`
    // 而不是 `SO_REUSEADDR`——后者在 Windows 上允许别的进程抢走同一地址。端口 0 由内核
    // 择定，两台假服务器撞不上号；设不上也不拦，让 bind 自己给结论。
    const auto exclusive = int{1};
    static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                                   reinterpret_cast<const char *>(&exclusive),
                                   sizeof(exclusive)));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), static_cast<int>(sizeof(address))) !=
        0) {
        close_socket(fd);
        return {};
    }
    if (::listen(fd, 8) != 0) {
        close_socket(fd);
        return {};
    }
    auto length = int{sizeof(address)};
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        return {fd, -1};
    }
    return {fd, static_cast<int>(ntohs(address.sin_port))};
}

#else  // !_WIN32

// ---- POSIX 腿（登记时的原文，逐字节行为不变）--------------------------------

auto close_socket(socket_t fd) -> void {
    if (fd >= 0) {
        static_cast<void>(::close(fd));
    }
}

auto shutdown_both(socket_t fd) -> void {
    if (fd >= 0) {
        static_cast<void>(::shutdown(fd, SHUT_RDWR));
    }
}

[[nodiscard]] auto accept_one(socket_t listener) -> socket_t {
    return ::accept(listener, nullptr, nullptr);
}

[[nodiscard]] auto wait_readable(socket_t fd, int timeout_ms) -> bool {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    timeval slice{};
    slice.tv_sec = timeout_ms / 1000;
    slice.tv_usec = (timeout_ms % 1000) * 1000;
    return ::select(fd + 1, &readable, nullptr, nullptr, &slice) > 0;
}

[[nodiscard]] auto wait_writable(socket_t fd, int timeout_ms) -> bool {
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval slice{};
    slice.tv_sec = timeout_ms / 1000;
    slice.tv_usec = (timeout_ms % 1000) * 1000;
    return ::select(fd + 1, nullptr, &writable, nullptr, &slice) > 0;
}

/// @brief 跳板腿：把一段字节全量写进目的地句柄（吞掉部分写与 EINTR）。
///        @return false＝目的地收摊或出错，这条转发通道到头了。
[[nodiscard]] auto send_all(socket_t fd, std::string_view bytes) -> bool {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto sent = ::send(fd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

/// @brief 跳板腿：连到客户端要求的目的地（仅 IPv4 字面量；转发目的在本仓证人里恒为
///        127.0.0.1 上的另一台假服务器）。失败回 `SSH_INVALID_SOCKET`。
[[nodiscard]] auto connect_to(const char *address, int port) -> socket_t {
    const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return SSH_INVALID_SOCKET;
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, address, &target.sin_addr) != 1) {
        ::close(fd);
        return SSH_INVALID_SOCKET;
    }
    // 非阻塞 connect + `select` 写就绪：拿得到「连不上」而不是「卡住这台工作线程」。
    const auto flags = ::fcntl(fd, F_GETFL, 0);
    static_cast<void>(::fcntl(fd, F_SETFL, flags | O_NONBLOCK));
    const int rc =
        ::connect(fd, reinterpret_cast<const sockaddr *>(&target), static_cast<socklen_t>(sizeof(target)));
    auto connected = rc == 0;
    if (!connected && errno != EINPROGRESS) {
        ::close(fd);
        return SSH_INVALID_SOCKET;
    }
    if (!connected) {
        connected = wait_writable(fd, kForwardConnectTimeoutMs);
        if (connected) {
            auto so_error = int{0};
            auto length = static_cast<socklen_t>(sizeof(so_error));
            static_cast<void>(::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &length));
            connected = so_error == 0;
        }
    }
    if (!connected) {
        ::close(fd);
        return SSH_INVALID_SOCKET;
    }
    // 交回阻塞语义：`read_peer()` 用 `poll` 判可读之后才 `recv`。
    static_cast<void>(::fcntl(fd, F_SETFL, flags));
    return fd;
}

/// @brief 跳板腿：目的地 → 转发通道这一向的单拍读（先 0 超时探一下，绝不阻塞工作线程）。
[[nodiscard]] auto read_peer(socket_t fd, std::span<char> out) -> PeerRead {
    if (out.empty()) {
        return {PeerReadOutcome::None, 0};
    }
    pollfd events{fd, static_cast<short>(POLLIN), 0};
    const auto ready = ::poll(&events, 1, 0);
    if (ready <= 0 || (events.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return {PeerReadOutcome::None, 0};
    }
    const auto got = ::recv(fd, out.data(), out.size(), MSG_DONTWAIT);
    if (got > 0) {
        return {PeerReadOutcome::Data, static_cast<std::size_t>(got)};
    }
    if (got == 0) {
        return {PeerReadOutcome::Closed, 0};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {PeerReadOutcome::None, 0};
    }
    return {PeerReadOutcome::Error, 0};
}

[[nodiscard]] auto open_loopback_listener() -> Listener {
    const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return {};
    }
    const auto reuse = int{1};
    static_cast<void>(
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        ::close(fd);
        return {};
    }
    if (::listen(fd, 8) < 0) {
        ::close(fd);
        return {};
    }
    auto length = socklen_t{sizeof(address)};
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        return {fd, -1};
    }
    return {fd, static_cast<int>(ntohs(address.sin_port))};
}

#endif  // _WIN32

}  // namespace

struct FakeSshServer::Impl {
    struct ChannelState;
    struct Worker;

    /// @brief 一条 session 通道的现场：回调表必须与通道同寿。
    struct ChannelState {
        Impl *impl{nullptr};
        Worker *owner{nullptr};
        ssh_channel channel{nullptr};
        ssh_channel_callbacks_struct callbacks{};
        bool shell_open{false};
        bool agent{false};   ///< 反向 auth-agent 通道：只记录收到的应答，不回显。
        bool forward{false}; ///< `direct-tcpip` 通道：字节搬到 peer_fd，不记账、不回显。
        socket_t peer_fd{SSH_INVALID_SOCKET}; ///< forward 通道对上的真实 TCP 目的地句柄（跳板腿）。
        bool closed{false};  ///< 已收到关闭（判定「整条连接是否收摊」用）。
        int index{-1};       ///< session 通道的打开次序（逐通道 PTY 尺寸表的下标）。
    };

    /// @brief 一条已受理连接的现场（工作线程独占，故内部字段不加锁）。
    struct Worker {
        Impl *impl{nullptr};
        ssh_session session{nullptr};
        ssh_event event{nullptr};
        socket_t fd{SSH_INVALID_SOCKET};
        std::vector<std::unique_ptr<ChannelState>> channels{};
        std::size_t outbox_cursor{0};
        std::atomic<bool> end_requested{false};
        bool peer_closed{false};
        bool agent_requested{false};
        bool agent_opened{false};
    };

    explicit Impl(FakeSshServerOptions opts) : options{std::move(opts)} {
        static_cast<void>(start());
    }

    ~Impl() { stop(); }

    [[nodiscard]] auto start() -> bool;
    auto stop() -> void;
    auto accept_loop() -> void;
    auto serve(const std::shared_ptr<Worker> &worker) -> void;
    auto flush_outbox(Worker &worker) -> void;
    auto end_channels(Worker &worker) -> void;
    auto maybe_open_agent(Worker &worker) -> void;
    auto service_forwarded(Worker &worker) -> void;

    // ---- 事实计数（工作线程写、用例线程读）----
    auto note_connection() -> void;
    auto note_auth() -> void;
    auto note_channel() -> std::size_t;
    auto note_pty(int index, int columns, int rows) -> void;
    auto note_shell() -> void;
    auto note_agent_request() -> void;
    auto note_agent_channel() -> void;
    auto note_direct_tcpip_request(std::string destination) -> void;
    auto record(std::string_view bytes) -> void;
    auto record_agent(std::string_view bytes) -> void;

    // ---- libssh 回调（静态成员＝普通函数指针；userdata 约定见各实现）----
    static auto on_auth_password(ssh_session session, const char *user, const char *password,
                                 void *userdata) -> int;
    static auto on_auth_none(ssh_session session, const char *user, void *userdata) -> int;
    static auto on_data(ssh_session session, ssh_channel channel, void *data, uint32_t len,
                        int is_stderr, void *userdata) -> int;
    static auto on_pty_request(ssh_session session, ssh_channel channel, const char *term,
                               int width, int height, int pxwidth, int pxheight, void *userdata)
        -> int;
    static auto on_pty_window_change(ssh_session session, ssh_channel channel, int width,
                                     int height, int pxwidth, int pxheight, void *userdata) -> int;
    static auto on_shell_request(ssh_session session, ssh_channel channel, void *userdata) -> int;
    static auto on_channel_eof(ssh_session session, ssh_channel channel, void *userdata) -> void;
    static auto on_channel_close(ssh_session session, ssh_channel channel, void *userdata) -> void;
    static auto on_auth_agent_request(ssh_session session, ssh_channel channel, void *userdata)
        -> void;
    static auto on_channel_open(ssh_session session, void *userdata) -> ssh_channel;
    static auto on_direct_tcpip_open(ssh_session session, const char *destination_address,
                                     int destination_port, const char *originator_address,
                                     int originator_port, void *userdata) -> ssh_channel;

    FakeSshServerOptions options;

    ssh_bind bind{nullptr};
    socket_t listen_fd{SSH_INVALID_SOCKET};
    int port{0};

    std::atomic<bool> stopping{false};
    std::thread acceptor{};

    std::mutex workers_mutex;
    std::vector<std::shared_ptr<Worker>> workers{};
    std::vector<std::thread> worker_threads{};

    mutable std::mutex stats_mutex;
    std::size_t accepted{0};
    std::size_t auth_attempts{0};
    std::size_t channel_count{0};
    std::size_t pty_count{0};
    std::size_t shell_count{0};
    std::size_t agent_request_count{0};
    std::size_t agent_channel_count{0};
    std::size_t direct_tcpip_count{0};
    std::vector<std::string> forwarded{};
    int last_columns{0};
    int last_rows{0};
    std::vector<std::pair<int, int>> channel_sizes{};
    std::string received{};
    std::string agent_received{};

    std::mutex outbox_mutex;
    std::vector<std::string> outbox{};
};

auto FakeSshServer::Impl::on_auth_password(ssh_session /*session*/, const char *user,
                                           const char *password, void *userdata) -> int {
    auto &worker = *static_cast<Worker *>(userdata);
    worker.impl->note_auth();
    if (worker.impl->options.reject_auth) {
        return SSH_AUTH_DENIED;
    }
    if (user != nullptr && password != nullptr && worker.impl->options.user == user &&
        worker.impl->options.password == password) {
        return SSH_AUTH_SUCCESS;
    }
    return SSH_AUTH_DENIED;
}

auto FakeSshServer::Impl::on_auth_none(ssh_session session, const char * /*user*/,
                                       void * /*userdata*/) -> int {
    // 客户端先问「none 能不能过」：拒绝，并把可用的认证方式指回 password。
    ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD);
    return SSH_AUTH_DENIED;
}

auto FakeSshServer::Impl::on_data(ssh_session /*session*/, ssh_channel channel, void *data,
                                  uint32_t len, int /*is_stderr*/, void *userdata) -> int {
    auto &state = *static_cast<ChannelState *>(userdata);
    state.channel = channel;
    const auto text = std::string_view{static_cast<const char *>(data), len};
    if (state.forward) {
        // 跳板腿：客户端往转发通道写的字节就是下一跳（或目标）的 SSH 报文，原样送到对面。
        // 写不进去＝目的地收摊，通道就此到尽头（`service_forwarded()` 那侧对称处理）。
        if (state.peer_fd != SSH_INVALID_SOCKET) {
            static_cast<void>(send_all(state.peer_fd, text));
        }
        return static_cast<int>(len);
    }
    if (state.agent) {
        // 反向 agent 通道上回来的是本机 agent 的应答：只记账，不回显（回显会多出一条
        // 客户端不期待的 agent 消息）。
        state.impl->record_agent(text);
        return static_cast<int>(len);
    }
    state.impl->record(text);
    if (state.impl->options.echo) {
        static_cast<void>(ssh_channel_write(channel, data, len));
    }
    // 全部消费（即便不回显）：把数据收下才不会让通道窗口停在半截。
    return static_cast<int>(len);
}

auto FakeSshServer::Impl::on_pty_request(ssh_session /*session*/, ssh_channel channel,
                                         const char * /*term*/, int width, int height,
                                         int /*pxwidth*/, int /*pxheight*/, void *userdata) -> int {
    auto &state = *static_cast<ChannelState *>(userdata);
    state.channel = channel;
    state.impl->note_pty(state.index, width, height);
    return 0;
}

auto FakeSshServer::Impl::on_pty_window_change(ssh_session /*session*/, ssh_channel channel,
                                               int width, int height, int /*pxwidth*/,
                                               int /*pxheight*/, void *userdata) -> int {
    auto &state = *static_cast<ChannelState *>(userdata);
    state.channel = channel;
    state.impl->note_pty(state.index, width, height);
    return 0;
}

auto FakeSshServer::Impl::on_shell_request(ssh_session /*session*/, ssh_channel channel,
                                           void *userdata) -> int {
    auto &state = *static_cast<ChannelState *>(userdata);
    state.channel = channel;
    state.shell_open = true;
    state.impl->note_shell();
    const auto &banner = state.impl->options.banner;
    if (!banner.empty()) {
        static_cast<void>(ssh_channel_write(channel, banner.data(),
                                            static_cast<uint32_t>(banner.size())));
    }
    return 0;
}

auto FakeSshServer::Impl::on_channel_eof(ssh_session /*session*/, ssh_channel /*channel*/,
                                         void * /*userdata*/) -> void {}

auto FakeSshServer::Impl::on_channel_close(ssh_session /*session*/, ssh_channel /*channel*/,
                                           void *userdata) -> void {
    auto &state = *static_cast<ChannelState *>(userdata);
    // 反向 agent 通道与转发通道关掉都不代表这条连接的 session 收摊（客户端换拨时会先收火；
    // 跳板腿的转发通道更是「一条链路上的一根管子」，收它不等于收连接）。
    if (state.agent || state.forward) {
        if (state.peer_fd != SSH_INVALID_SOCKET) {
            close_socket(state.peer_fd);
            state.peer_fd = SSH_INVALID_SOCKET;
        }
        return;
    }
    state.closed = true;
    // 连接复用的证人（设计稿 §7.3）：只有**全部** session 通道都关了，整条连接才算收摊。
    // 旧行为「任一通道关闭即断整条连接」会让「关一个标签、另一个继续」的用例证不出来。
    auto all_closed = true;
    auto any_session = false;
    for (auto &other : state.owner->channels) {
        if (other->agent || other->forward) {
            continue;
        }
        any_session = true;
        if (!other->closed) {
            all_closed = false;
            break;
        }
    }
    if (any_session && all_closed) {
        state.owner->peer_closed = true;
    }
}

auto FakeSshServer::Impl::on_auth_agent_request(ssh_session /*session*/, ssh_channel /*channel*/,
                                                void *userdata) -> void {
    auto &state = *static_cast<ChannelState *>(userdata);
    state.owner->agent_requested = true;
    state.impl->note_agent_request();
}

auto FakeSshServer::Impl::on_channel_open(ssh_session session, void *userdata) -> ssh_channel {
    auto &worker = *static_cast<Worker *>(userdata);
    auto *channel = ssh_channel_new(session);
    if (channel == nullptr) {
        return nullptr;
    }

    auto state = std::make_unique<ChannelState>();
    state->impl = worker.impl;
    state->owner = &worker;
    state->channel = channel;
    state->index = static_cast<int>(worker.impl->note_channel());
    ssh_callbacks_init(&state->callbacks);
    state->callbacks.userdata = state.get();
    state->callbacks.channel_data_function = &Impl::on_data;
    state->callbacks.channel_pty_request_function = &Impl::on_pty_request;
    state->callbacks.channel_pty_window_change_function = &Impl::on_pty_window_change;
    state->callbacks.channel_shell_request_function = &Impl::on_shell_request;
    state->callbacks.channel_eof_function = &Impl::on_channel_eof;
    state->callbacks.channel_close_function = &Impl::on_channel_close;
    state->callbacks.channel_auth_agent_req_function = &Impl::on_auth_agent_request;
    ssh_set_channel_callbacks(channel, &state->callbacks);

    // unique_ptr 保证回调表地址稳定（libssh 存的是指针，vector 扩容不影响所指对象）。
    worker.channels.push_back(std::move(state));
    return channel;
}

auto FakeSshServer::Impl::on_direct_tcpip_open(ssh_session session,
                                               const char *destination_address,
                                               int destination_port,
                                               const char * /*originator_address*/,
                                               int /*originator_port*/, void *userdata)
    -> ssh_channel {
    auto &worker = *static_cast<Worker *>(userdata);
    const auto destination = destination_address == nullptr
                                 ? std::string{}
                                 : std::string{destination_address} + ":" +
                                       std::to_string(destination_port);
    // 目的地在拒绝之前也记：跳板腿的证人既要看「转发了哪儿」，也要数得到「被拒的请求」。
    worker.impl->note_direct_tcpip_request(destination);
    if (!worker.impl->options.allow_direct_tcpip || destination_address == nullptr) {
        return nullptr;  // 一台普通服务器不该代收转发；libssh 据此回 channel open failed。
    }
    const auto peer_fd = connect_to(destination_address, destination_port);
    if (peer_fd == SSH_INVALID_SOCKET) {
        return nullptr;  // 目的地连不上＝这台跳板拒绝这条转发（与真 sshd 同形）。
    }
    auto *channel = ssh_channel_new(session);
    if (channel == nullptr) {
        close_socket(peer_fd);
        return nullptr;
    }

    auto state = std::make_unique<ChannelState>();
    state->impl = worker.impl;
    state->owner = &worker;
    state->channel = channel;
    state->forward = true;
    state->peer_fd = peer_fd;
    ssh_callbacks_init(&state->callbacks);
    state->callbacks.userdata = state.get();
    state->callbacks.channel_data_function = &Impl::on_data;
    state->callbacks.channel_eof_function = &Impl::on_channel_eof;
    state->callbacks.channel_close_function = &Impl::on_channel_close;
    ssh_set_channel_callbacks(channel, &state->callbacks);

    worker.channels.push_back(std::move(state));
    return channel;
}

auto FakeSshServer::Impl::maybe_open_agent(Worker &worker) -> void {
    if (!options.open_agent_channel || !worker.agent_requested || worker.agent_opened) {
        return;
    }
    ssh_channel shell = nullptr;
    for (auto &state : worker.channels) {
        if (state->shell_open && !state->agent && state->channel != nullptr) {
            shell = state->channel;
            break;
        }
    }
    if (shell == nullptr) {
        return;  // 等 shell 起来再反向拨（请求与 shell 的到达次序不保证）。
    }
    // 只试一次：失败也不反复拨，免得把「客户端拒绝」变成每拍一条拒绝日志。
    worker.agent_opened = true;
    auto *channel = ssh_channel_new(worker.session);
    if (channel == nullptr) {
        return;
    }
    if (ssh_channel_open_auth_agent(channel) != SSH_OK) {
        ssh_channel_free(channel);
        return;
    }

    auto state = std::make_unique<ChannelState>();
    state->impl = this;
    state->owner = &worker;
    state->channel = channel;
    state->agent = true;
    ssh_callbacks_init(&state->callbacks);
    state->callbacks.userdata = state.get();
    state->callbacks.channel_data_function = &Impl::on_data;
    state->callbacks.channel_close_function = &Impl::on_channel_close;
    ssh_set_channel_callbacks(channel, &state->callbacks);

    worker.channels.push_back(std::move(state));
    note_agent_channel();

    const auto &payload = options.agent_payload;
    if (!payload.empty()) {
        static_cast<void>(ssh_channel_write(channel, payload.data(),
                                            static_cast<uint32_t>(payload.size())));
    }
}

auto FakeSshServer::Impl::service_forwarded(Worker &worker) -> void {
    // 目的地 → 转发通道这一向。`ssh_event` 只盯 libssh 的 socket，不认识我们替客户端
    // 连出去的第二条 TCP，故目的地侧的回流只能每拍自己扫一遍（0 超时探读，两腿同一档）。
    auto scratch = std::array<char, 4096>{};
    for (auto &state : worker.channels) {
        if (!state->forward || state->channel == nullptr || state->peer_fd == SSH_INVALID_SOCKET) {
            continue;
        }
        const auto got = read_peer(state->peer_fd, scratch);
        if (got.outcome == PeerReadOutcome::Data) {
            static_cast<void>(ssh_channel_write(state->channel, scratch.data(),
                                                static_cast<uint32_t>(got.bytes)));
            continue;
        }
        if (got.outcome == PeerReadOutcome::None) {
            continue;
        }
        // Closed＝目的地有序收摊，Error＝连不上或已复位：这条管子给客户端回 EOF，
        // 让对端的 libssh 会话按正常断链收尾（跳板腿的证人也靠这一步收敛）。
        static_cast<void>(ssh_channel_send_eof(state->channel));
        close_socket(state->peer_fd);
        state->peer_fd = SSH_INVALID_SOCKET;
    }
}

auto FakeSshServer::Impl::start() -> bool {
    // ---- 主机密钥：一次性生成，bind 接管所有权（ssh_bind_free 会释放）----
    ssh_key key = nullptr;
    if (ssh_pki_generate_key(SSH_KEYTYPE_ED25519, nullptr, &key) != SSH_OK) {
        // 极老/裁剪过的加密后端没有 ED25519：退到 RSA，仍然是一次性密钥。
        if (ssh_pki_generate_key(SSH_KEYTYPE_RSA, nullptr, &key) != SSH_OK) {
            return false;
        }
    }
    bind = ssh_bind_new();
    if (bind == nullptr) {
        ssh_key_free(key);
        return false;
    }
    if (ssh_bind_options_set(bind, SSH_BIND_OPTIONS_IMPORT_KEY, key) != SSH_OK) {
        ssh_key_free(key);
        ssh_bind_free(bind);
        bind = nullptr;
        return false;
    }
    // IMPORT_KEY 的语义是「交给你」：此后由 ssh_bind_free 释放，本处不再持有。
    return true;
}

auto FakeSshServer::Impl::accept_loop() -> void {
    while (!stopping.load()) {
        if (!wait_readable(listen_fd, kAcceptSliceMs)) {
            continue;  // 超时或被中断：回到停止位检查。
        }
        const auto fd = accept_one(listen_fd);
        if (fd == SSH_INVALID_SOCKET) {
            continue;
        }
        auto *session = ssh_new();
        if (session == nullptr || ssh_bind_accept_fd(bind, session, fd) != SSH_OK) {
            if (session != nullptr) {
                ssh_free(session);
            }
            close_socket(fd);
            continue;
        }
        note_connection();

        auto worker = std::make_shared<Worker>();
        worker->impl = this;
        worker->session = session;
        worker->fd = fd;
        const std::lock_guard lock{workers_mutex};
        workers.push_back(worker);
        worker_threads.emplace_back([this, worker] { serve(worker); });
    }
}

auto FakeSshServer::Impl::serve(const std::shared_ptr<Worker> &worker) -> void {
    ssh_server_callbacks_struct server_callbacks{};
    server_callbacks.userdata = worker.get();
    server_callbacks.auth_password_function = &Impl::on_auth_password;
    server_callbacks.auth_none_function = &Impl::on_auth_none;
    server_callbacks.channel_open_request_session_function = &Impl::on_channel_open;
    server_callbacks.channel_open_request_direct_tcpip_function = &Impl::on_direct_tcpip_open;
    ssh_callbacks_init(&server_callbacks);
    ssh_set_server_callbacks(worker->session, &server_callbacks);

    if (ssh_handle_key_exchange(worker->session) == SSH_OK) {
        ssh_set_auth_methods(worker->session, SSH_AUTH_METHOD_PASSWORD);
        worker->event = ssh_event_new();
        if (worker->event != nullptr) {
            ssh_event_add_session(worker->event, worker->session);
            while (!stopping.load() && ssh_is_connected(worker->session) != 0) {
                if (worker->end_requested.exchange(false)) {
                    end_channels(*worker);
                }
                flush_outbox(*worker);
                service_forwarded(*worker);
                if (ssh_event_dopoll(worker->event, kPollSliceMs) == SSH_ERROR) {
                    break;
                }
                maybe_open_agent(*worker);
                if (worker->peer_closed) {
                    break;
                }
            }
            ssh_event_free(worker->event);
            worker->event = nullptr;
        }
    }

    // 通道先于回调表释放（libssh 存的是回调表指针），会话最后释放。
    for (auto &state : worker->channels) {
        if (state->peer_fd != SSH_INVALID_SOCKET) {
            close_socket(state->peer_fd);  // 替客户端连出去的目的地句柄归本服务器收。
            state->peer_fd = SSH_INVALID_SOCKET;
        }
        if (state->channel != nullptr) {
            static_cast<void>(ssh_channel_close(state->channel));
            ssh_channel_free(state->channel);
            state->channel = nullptr;
        }
    }
    worker->channels.clear();
    if (worker->session != nullptr) {
        ssh_free(worker->session);
        worker->session = nullptr;
    }
}

auto FakeSshServer::Impl::end_channels(Worker &worker) -> void {
    for (auto &state : worker.channels) {
        // 转发通道不吃 `end_shell()`：它是「跳板替客户端开着的一根管子」，服务器主动收它
        // 会让正在用的跳链无故断掉，语义上和 session 收摊不是一回事。
        if (state->channel != nullptr && !state->agent && !state->forward) {
            static_cast<void>(ssh_channel_send_eof(state->channel));
        }
    }
}

auto FakeSshServer::Impl::flush_outbox(Worker &worker) -> void {
    auto pending = std::vector<std::string>{};
    {
        const std::lock_guard lock{outbox_mutex};
        for (; worker.outbox_cursor < outbox.size(); ++worker.outbox_cursor) {
            pending.push_back(outbox[worker.outbox_cursor]);
        }
    }
    for (const auto &bytes : pending) {
        for (auto &state : worker.channels) {
            if (state->shell_open && state->channel != nullptr) {
                static_cast<void>(ssh_channel_write(
                    state->channel, bytes.data(), static_cast<uint32_t>(bytes.size())));
            }
        }
    }
}

auto FakeSshServer::Impl::stop() -> void {
    if (stopping.exchange(true)) {
        return;
    }
    if (acceptor.joinable()) {
        acceptor.join();  // select 分片 100 ms，故最多等一片。
    }
    if (listen_fd != SSH_INVALID_SOCKET) {
        close_socket(listen_fd);
        listen_fd = SSH_INVALID_SOCKET;
    }
    {
        const std::lock_guard lock{workers_mutex};
        for (auto &worker : workers) {
            if (worker->fd != SSH_INVALID_SOCKET) {
                shutdown_both(worker->fd);  // 叫停还卡在 dopoll 上的工作线程。
            }
        }
    }
    for (auto &thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads.clear();
    {
        const std::lock_guard lock{workers_mutex};
        workers.clear();
    }
    if (bind != nullptr) {
        ssh_bind_free(bind);  // 连同导入的主机密钥一起释放。
        bind = nullptr;
    }
}

auto FakeSshServer::Impl::note_connection() -> void {
    const std::lock_guard lock{stats_mutex};
    ++accepted;
}

auto FakeSshServer::Impl::note_auth() -> void {
    const std::lock_guard lock{stats_mutex};
    ++auth_attempts;
}

auto FakeSshServer::Impl::note_channel() -> std::size_t {
    const std::lock_guard lock{stats_mutex};
    const auto index = channel_count;
    ++channel_count;
    return index;
}

auto FakeSshServer::Impl::note_pty(int index, int columns, int rows) -> void {
    const std::lock_guard lock{stats_mutex};
    ++pty_count;
    last_columns = columns;
    last_rows = rows;
    if (index >= 0) {
        const auto slot = static_cast<std::size_t>(index);
        if (slot >= channel_sizes.size()) {
            channel_sizes.resize(slot + 1, std::pair<int, int>{0, 0});
        }
        channel_sizes[slot] = std::pair<int, int>{columns, rows};
    }
}

auto FakeSshServer::Impl::note_shell() -> void {
    const std::lock_guard lock{stats_mutex};
    ++shell_count;
}

auto FakeSshServer::Impl::record(std::string_view bytes) -> void {
    const std::lock_guard lock{stats_mutex};
    received.append(bytes);
}

auto FakeSshServer::Impl::note_agent_request() -> void {
    const std::lock_guard lock{stats_mutex};
    ++agent_request_count;
}

auto FakeSshServer::Impl::note_agent_channel() -> void {
    const std::lock_guard lock{stats_mutex};
    ++agent_channel_count;
}

auto FakeSshServer::Impl::record_agent(std::string_view bytes) -> void {
    const std::lock_guard lock{stats_mutex};
    agent_received.append(bytes);
}

auto FakeSshServer::Impl::note_direct_tcpip_request(std::string destination) -> void {
    const std::lock_guard lock{stats_mutex};
    ++direct_tcpip_count;
    forwarded.push_back(std::move(destination));
}

FakeSshServer::FakeSshServer(FakeSshServerOptions options)
    : impl_{std::make_unique<Impl>(std::move(options))} {
    if (impl_->bind == nullptr) {
        return;
    }
    // ---- 监听点：自带 socket（端口 0 由内核择定），accept 后交给 ssh_bind ----
    const auto listener = open_loopback_listener();
    impl_->listen_fd = listener.fd;
    impl_->port = listener.port;
    if (impl_->listen_fd == SSH_INVALID_SOCKET) {
        return;
    }
    impl_->acceptor = std::thread([impl = impl_.get()] { impl->accept_loop(); });
}

FakeSshServer::~FakeSshServer() = default;

auto FakeSshServer::ok() const -> bool { return impl_->listen_fd != SSH_INVALID_SOCKET && impl_->port > 0; }

auto FakeSshServer::port() const -> int { return impl_->port; }

auto FakeSshServer::accepted_connections() const -> std::size_t {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->accepted;
}

auto FakeSshServer::session_channels() const -> std::size_t {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->channel_count;
}

auto FakeSshServer::auth_attempts() const -> std::size_t {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->auth_attempts;
}

auto FakeSshServer::pty_requests() const -> std::size_t {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->pty_count;
}

auto FakeSshServer::shell_requests() const -> std::size_t {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->shell_count;
}

auto FakeSshServer::last_pty_columns() const -> int {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->last_columns;
}

auto FakeSshServer::last_pty_rows() const -> int {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->last_rows;
}

auto FakeSshServer::pty_size_at(std::size_t index) const -> std::pair<int, int> {
    const std::lock_guard lock{impl_->stats_mutex};
    if (index >= impl_->channel_sizes.size()) {
        return std::pair<int, int>{0, 0};
    }
    return impl_->channel_sizes[index];
}

auto FakeSshServer::received_text() const -> std::string {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->received;
}

auto FakeSshServer::agent_forward_requests() const -> std::size_t {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->agent_request_count;
}

auto FakeSshServer::agent_channels() const -> std::size_t {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->agent_channel_count;
}

auto FakeSshServer::agent_received_text() const -> std::string {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->agent_received;
}

auto FakeSshServer::direct_tcpip_requests() const -> std::size_t {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->direct_tcpip_count;
}

auto FakeSshServer::forwarded_destinations() const -> std::vector<std::string> {
    const std::lock_guard lock{impl_->stats_mutex};
    return impl_->forwarded;
}

auto FakeSshServer::send_more(std::string_view bytes) -> void {
    const std::lock_guard lock{impl_->outbox_mutex};
    impl_->outbox.emplace_back(bytes);
}

auto FakeSshServer::end_shell() -> void {
    const std::lock_guard lock{impl_->workers_mutex};
    for (auto &worker : impl_->workers) {
        worker->end_requested.store(true);
    }
}

auto FakeSshServer::drop_connections() -> void {
    const std::lock_guard lock{impl_->workers_mutex};
    for (auto &worker : impl_->workers) {
        if (worker->fd != SSH_INVALID_SOCKET) {
            shutdown_both(worker->fd);
        }
    }
}

auto wait_until(const std::function<bool()> &predicate, int timeout_ms) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

auto fake_ssh_profile(int port, std::string user) -> borealis::conn::SshProfile {
    auto profile = borealis::conn::SshProfile{};
    profile.host = "127.0.0.1";
    profile.port = port;
    profile.user = std::move(user);
    profile.auth_method = "password";
    profile.known_hosts_policy = borealis::conn::KnownHostsPolicy::No;
    return profile;
}

}  // namespace borealis::testing
