// ============================================================
// 进程内 libssh 假 SSH 服务器实现（tests/support/fake_ssh_server.cpp）
// ------------------------------------------------------------
// 为什么造它：本沙箱没有 sshd，`SPEC.FEAT.CONN.02`／CONN.04／CONN.08 的真协议
// 端到端证人一直缺位。libssh 的 server 腿在本机 vcpkg 构建档里是开着的
// （`WITH_SERVER`，见 SPECIFICATIONS §7.145②），故用同一份库在测试进程内起一台
// 最小服务器：真 KEX、真加密、真通道多路复用，测试只多一条 loopback 连接。
//
// 线程与生命周期：
//   - 受理线程用 `select` 分片等监听 fd，`ssh_bind_accept_fd` 之后把 session 交给
//     新的工作线程（libssh 会话不跨线程）；
//   - 工作线程先 `ssh_handle_key_exchange`，再把 session 挂到自己的 `ssh_event` 上，
//     每 10 ms 一拍；认证/开通道/PTY/shell 全走 server callbacks；
//   - 回调表与通道回调表由工作线程持有，必须比会话/通道活得久（libssh 存的是指针）；
//   - 析构：置停止位 → join 受理线程 → shutdown 各连接 → join 工作线程（dopoll 每拍
//     查停止位）→ 释放 session/通道 → 释放 bind（bind 拥有导入的主机密钥）。
//
// 平台边界：POSIX 腿（select/getsockname/shutdown）。Windows 腿待真机——MSVC 构建档的
// WITH_SERVER 未验证，故本文件在 Win32 下只留空实现；消费它的用例按 `itest_posix_`
// 前缀只在 POSIX 构建（cmake/BorealisTests.cmake 的同名过滤）。
// ============================================================

#include "fake_ssh_server.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32

#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>

namespace borealis::testing {

namespace {

/// @brief 受理线程的 select 分片：也是析构叫停延迟的上界。
constexpr long kAcceptSliceUs = 100000;

/// @brief 工作线程的事件轮询分片：决定 `end_shell()`／析构叫停的延迟上界。
constexpr int kPollSliceMs = 10;

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
        bool agent{false};  ///< 反向 auth-agent 通道：只记录收到的应答，不回显。
        bool closed{false}; ///< 已收到关闭（判定「整条连接是否收摊」用）。
        int index{-1};      ///< session 通道的打开次序（逐通道 PTY 尺寸表的下标）。
    };

    /// @brief 一条已受理连接的现场（工作线程独占，故内部字段不加锁）。
    struct Worker {
        Impl *impl{nullptr};
        ssh_session session{nullptr};
        ssh_event event{nullptr};
        int fd{-1};
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

    // ---- 事实计数（工作线程写、用例线程读）----
    auto note_connection() -> void;
    auto note_auth() -> void;
    auto note_channel() -> std::size_t;
    auto note_pty(int index, int columns, int rows) -> void;
    auto note_shell() -> void;
    auto note_agent_request() -> void;
    auto note_agent_channel() -> void;
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

    FakeSshServerOptions options;

    ssh_bind bind{nullptr};
    int listen_fd{-1};
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
    // 反向 agent 通道关掉不代表这条连接的 session 收摊（客户端换拨时会先收火）。
    if (state.agent) {
        return;
    }
    state.closed = true;
    // 连接复用的证人（设计稿 §7.3）：只有**全部** session 通道都关了，整条连接才算收摊。
    // 旧行为「任一通道关闭即断整条连接」会让「关一个标签、另一个继续」的用例证不出来。
    auto all_closed = true;
    auto any_session = false;
    for (auto &other : state.owner->channels) {
        if (other->agent) {
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
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listen_fd, &readable);
        timeval slice{};
        slice.tv_usec = kAcceptSliceUs;
        const auto ready = ::select(listen_fd + 1, &readable, nullptr, nullptr, &slice);
        if (ready <= 0) {
            continue;  // 超时或被信号打断：回到停止位检查。
        }
        const auto fd = ::accept(listen_fd, nullptr, nullptr);
        if (fd < 0) {
            continue;
        }
        auto *session = ssh_new();
        if (session == nullptr || ssh_bind_accept_fd(bind, session, fd) != SSH_OK) {
            if (session != nullptr) {
                ssh_free(session);
            }
            ::close(fd);
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
        if (state->channel != nullptr && !state->agent) {
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
    if (listen_fd >= 0) {
        ::close(listen_fd);
        listen_fd = -1;
    }
    {
        const std::lock_guard lock{workers_mutex};
        for (auto &worker : workers) {
            if (worker->fd >= 0) {
                ::shutdown(worker->fd, SHUT_RDWR);  // 叫停还卡在 dopoll 上的工作线程。
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

FakeSshServer::FakeSshServer(FakeSshServerOptions options)
    : impl_{std::make_unique<Impl>(std::move(options))} {
    if (impl_->bind == nullptr) {
        return;
    }
    // ---- 监听点：自带 socket（端口 0 由内核择定），accept 后交给 ssh_bind ----
    impl_->listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->listen_fd < 0) {
        return;
    }
    const auto reuse = int{1};
    static_cast<void>(
        ::setsockopt(impl_->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(impl_->listen_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        ::close(impl_->listen_fd);
        impl_->listen_fd = -1;
        return;
    }
    if (::listen(impl_->listen_fd, 8) < 0) {
        ::close(impl_->listen_fd);
        impl_->listen_fd = -1;
        return;
    }
    auto length = socklen_t{sizeof(address)};
    if (::getsockname(impl_->listen_fd, reinterpret_cast<sockaddr *>(&address), &length) == 0) {
        impl_->port = ntohs(address.sin_port);
    } else {
        impl_->port = -1;
    }
    impl_->acceptor = std::thread([impl = impl_.get()] { impl->accept_loop(); });
}

FakeSshServer::~FakeSshServer() = default;

auto FakeSshServer::ok() const -> bool { return impl_->listen_fd >= 0 && impl_->port > 0; }

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
        if (worker->fd >= 0) {
            ::shutdown(worker->fd, SHUT_RDWR);
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

#else  // _WIN32

// TODO(SPEC.FEAT.CONN.02): 补 Win32 腿的进程内假 SSH 服务器。本设施用 POSIX
// select/getsockname/shutdown；MSVC 构建档的 libssh WITH_SERVER 与 Winsock 半截
// 均未验证，故先留空实现保证构建面完整，消费用例按 `itest_posix_` 前缀排除。

namespace borealis::testing {

struct FakeSshServer::Impl {};

FakeSshServer::FakeSshServer(FakeSshServerOptions) : impl_{std::make_unique<Impl>()} {}

FakeSshServer::~FakeSshServer() = default;

auto FakeSshServer::ok() const -> bool { return false; }

auto FakeSshServer::port() const -> int { return 0; }

auto FakeSshServer::accepted_connections() const -> std::size_t { return 0; }

auto FakeSshServer::session_channels() const -> std::size_t { return 0; }

auto FakeSshServer::auth_attempts() const -> std::size_t { return 0; }

auto FakeSshServer::pty_requests() const -> std::size_t { return 0; }

auto FakeSshServer::shell_requests() const -> std::size_t { return 0; }

auto FakeSshServer::last_pty_columns() const -> int { return 0; }

auto FakeSshServer::last_pty_rows() const -> int { return 0; }

auto FakeSshServer::received_text() const -> std::string { return {}; }

auto FakeSshServer::agent_forward_requests() const -> std::size_t { return 0; }

auto FakeSshServer::agent_channels() const -> std::size_t { return 0; }

auto FakeSshServer::agent_received_text() const -> std::string { return {}; }

auto FakeSshServer::send_more(std::string_view) -> void {}

auto FakeSshServer::end_shell() -> void {}

auto FakeSshServer::drop_connections() -> void {}

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

#endif  // !_WIN32
