// ============================================================
// SSH 连接复用的共享会话实现（src/conn/ssh_shared.cpp）
// ------------------------------------------------------------
// 见同头注释与设计稿 `codespec/CONN02_REUSE.draft.md` §5–§6。
//
// 线程边界只有两处：
//   * 调用方线程（UI）：attach／detach／write／resize 只压命令或等 ack；
//     retry_now／stop_reconnect 只翻原子位并唤醒 I/O 线程。
//   * I/O 线程：拨号、认证、开通道、逐通道读、写、window-change、agent 中继、
//     退避与释放全在这一条线程上——libssh 非线程安全，旧单标签腿里 UI 线程
//     直写通道与读线程并发的潜在竞态就此收敛（设计稿 F2 的结构性收益）。
//
// 关闭序（设计稿 §6）：`Detach` 处理时先关该租约的通道、投终态回调、再摘租约，
// 最后置 ack；`detach` 返回后 I/O 线程不再碰该租约的 `ConnectionEvents`。末位
// 租约摘掉后 I/O 线程自行收口（关通道 → 断会话 → 释放回调表 → 清明文）。
// ============================================================

#include "ssh_shared.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>
#include <utility>

#include <libssh/callbacks.h>
#include <libssh/libssh.h>

#include "aurora/core/log.h"
#include "conn/ssh_dial.h"
#include "platform/agent_socket.h"

namespace borealis::conn {

namespace {

/// @brief 逐通道读的单块上限：与旧单标签腿同款，原样整块投给会话层。
constexpr std::size_t kReadChunkBytes = 32768;

/// @brief 反向 agent 通道的搬运块：agent 协议的消息都很小，8 KiB 足够一次装下。
constexpr std::size_t kAgentChunkBytes = 8192;

/// @brief 同时存在的反向 agent 通道上限（逐连接）：远端可以反复打开，本端不跟它无限扩表。
constexpr std::size_t kMaxForwardedAgentChannels = 8;

/// @brief 待发字节总量上限（设计稿 D3）：超限丢弃新来的一笔并记一条 WARN。
constexpr std::size_t kMaxPendingWriteBytes = 1024 * 1024;

/// @brief 退避休眠的分片长度：片间处理命令，也是 detach 的叫停延迟上界。
constexpr int kBackoffSliceMs = 200;

/// @brief 无字节可读时的空转节拍：决定关闭延迟上界与命令处理频率。
constexpr std::chrono::milliseconds kReadIdle{30};

/// @brief PTY 请求的 TERM 值：与本地腿 `kPtyDefaultEnvironment` 的 TERM 同源口径。
constexpr std::string_view kPtyTerm = "xterm-256color";

/// @brief 拨号档位 → 关闭归因（口径同旧单标签腿，分类表在 `conn::should_reconnect`）。
[[nodiscard]] auto reason_for(DialOutcome outcome) -> session::CloseReason {
    switch (outcome) {
        case DialOutcome::Network:
            return session::CloseReason::DialNetwork;
        case DialOutcome::Auth:
            return session::CloseReason::AuthFailed;
        case DialOutcome::HostKey:
            return session::CloseReason::HostKeyRejected;
        case DialOutcome::Options:
        case DialOutcome::Unallocated:
        // Hop＝某一跳失败（子档在 `DialFailure` 里）、HopUnwired＝这条腿不建链就拒拨（稿 D6）。
        // 本腿还不消费跳链（`SPEC.FEAT.CONN.03` 批 4 接线），两档现在都到不了这里；归不进
        // 任何一类网络/服务器原因，保守按不可重拨。
        case DialOutcome::Hop:
        case DialOutcome::HopUnwired:
        case DialOutcome::Ok:
            break;
    }
    // Options＝用户配的算法表被库拒收；Unallocated＝本机句柄都开不出来。两者都归不进
    // 任何一类网络/服务器原因，保守按不可重拨（配置错误重拨只会原样再失败）。
    return session::CloseReason::Unknown;
}

/// @brief PTY 尺寸折成 libssh 可接受的 1..32767（0 折 1，与旧单标签腿同款）。
[[nodiscard]] auto clamp_dimension(std::size_t value) -> int {
    return static_cast<int>(
        std::clamp(std::max(value, std::size_t{1}), std::size_t{1}, std::size_t{32767}));
}

}  // namespace

auto SshSessionKey::of(const SshProfile &profile, const SshAlgorithmOverrides &algorithms)
    -> SshSessionKey {
    auto key = SshSessionKey{};
    key.host = profile.host;
    key.port = effective_port(profile);
    key.user = profile.user;
    key.auth_method = std::string{normalized_auth_method(profile)};
    key.identity_file = profile.identity_file;
    key.agent_forwarding = profile.agent_forwarding;
    key.known_hosts_policy = profile.known_hosts_policy;
    key.kex_algorithms = algorithms.kex;
    key.host_key_algorithms = algorithms.host_key;
    return key;
}

auto same_secret(const std::optional<std::string> &lhs, const std::optional<std::string> &rhs)
    -> bool {
    if (!lhs.has_value() || !rhs.has_value()) {
        return lhs.has_value() == rhs.has_value();
    }
    return *lhs == *rhs;
}

auto SshSessionRegistry::acquire(const SshProfile &profile, std::optional<std::string> secret,
                                 const SshAlgorithmOverrides &algorithms, RetryPolicy policy)
    -> std::shared_ptr<SshSharedSession> {
    const auto key = SshSessionKey::of(profile, algorithms);
    const std::lock_guard lock{mutex_};
    for (auto it = entries_.begin(); it != entries_.end();) {
        auto existing = it->session.lock();
        if (!existing) {
            it = entries_.erase(it);  // 顺带剪枝：链路已收口，弱引用随之过期。
            continue;
        }
        // 先问「还接不接新租约」再比秘密：进入终态的会话即便秘密相符也不该被借出，
        // 而它的秘密可能已经随收口清掉，比较结果没有意义。
        if (existing->accepting() && existing->matches(key, secret)) {
            return existing;
        }
        ++it;
    }
    auto fresh = std::make_shared<SshSharedSession>(profile, std::move(secret), policy, algorithms);
    entries_.push_back(Entry{key, fresh});
    return fresh;
}

/// @brief 共享会话的实现体：队列、租约表、退避环与 libssh 句柄全在这里。
///
/// 只有 `queue_mutex` 保护的字段可被两个线程触碰；`leases` / `session` /
/// `agent` / `linked` / `latest_progress` 只由 I/O 线程读写（`attach` 只入队，
/// 不直接改租约表）。
struct SshSharedSession::Impl {
    /// @brief `Detach` 的完成信号：`close()` 等它，保证返回后不再有回调。
    struct Ack {
        std::mutex mutex;
        std::condition_variable cv;
        bool done{false};
    };

    enum class CommandKind : std::uint8_t { Attach, Detach, Write, Resize };

    struct Command {
        CommandKind kind{CommandKind::Attach};
        SshConnection *lease{nullptr};
        session::ConnectionEvents *events{nullptr};
        session::Size size{};
        std::vector<std::byte> bytes{};
        std::shared_ptr<Ack> ack{};
        std::function<void()> on_open{};
        std::function<void()> on_finished{};
    };

    /// @brief 一份租约的现场：一条 shell 通道 + 该标签的事件接收端与最新尺寸。
    struct Lease {
        SshConnection *owner{nullptr};
        session::ConnectionEvents *events{nullptr};
        session::Size size{};
        ssh_channel_struct *channel{nullptr};
        std::function<void()> on_open{};
        std::function<void()> on_finished{};
    };

    /// @brief 一条反向 agent 通道及其本机 agent 句柄。
    struct Forwarded {
        ssh_channel_struct *channel{nullptr};
        std::unique_ptr<platform::AgentSocket> agent;
        bool remote_eof{false};  ///< 远端半关：不再读通道，但 agent 的应答仍可回送。
    };

    /// @brief agent 转发的逐连接状态（回调表被 libssh 按指针持有，必须与本对象同寿）。
    struct AgentForward {
        ssh_callbacks_struct callbacks{};
        std::vector<Forwarded> channels;
        bool installed{false};
        bool available{false};  ///< 本机 agent 是否可达（每回拨号探一次）。
    };

    struct Attempt {
        bool linked{false};
        session::CloseReason reason{session::CloseReason::Unknown};
    };

    enum class Pump : std::uint8_t { Alive, LinkLost, RemoteExit };

    Impl(const SshProfile &source_profile, std::optional<std::string> source_secret,
         RetryPolicy source_policy, SshAlgorithmOverrides source_algorithms)
        : profile{source_profile},
          secret{std::move(source_secret)},
          policy{source_policy},
          algorithms{std::move(source_algorithms)},
          key{SshSessionKey::of(profile, algorithms)} {}

    ~Impl() {
        {
            const std::lock_guard lock{queue_mutex};
            terminated.store(true);
            accepting_flag.store(false);
        }
        queue_cv.notify_all();
        if (io.joinable()) {
            io.join();  // 正常路径上循环早已自行收口；这里只是线程对象的收尾。
        }
    }

    Impl(const Impl &) = delete;
    auto operator=(const Impl &) -> Impl & = delete;

    // ---- 构造后不再改的共享判据 ----
    SshProfile profile;
    std::optional<std::string> secret;
    RetryPolicy policy;
    SshAlgorithmOverrides algorithms;
    SshSessionKey key;
    mutable std::mutex secret_mutex;  ///< 只护 `secret`：收口时清、registry 比对时读。

    // ---- 跨线程：队列与入口状态 ----
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<Command> commands;
    std::size_t pending_write_bytes{0};
    bool write_overflow_warned{false};
    bool thread_started{false};
    std::atomic<bool> accepting_flag{true};
    std::atomic<bool> terminated{false};
    std::atomic<bool> retry_now_flag{false};
    std::atomic<bool> stop_reconnect_flag{false};

    // ---- I/O 线程独占 ----
    std::thread io;
    std::vector<std::unique_ptr<Lease>> leases;
    ssh_session_struct *session{nullptr};
    AgentForward agent;
    bool linked{false};
    session::ReconnectProgress latest_progress{};

    // ========================================================
    // 队列侧（两线程共用，调用方线程进入）
    // ========================================================
    auto enqueue_attach(SshConnection *lease, session::ConnectionEvents &events, session::Size size,
                        std::function<void()> on_open, std::function<void()> on_finished) -> bool {
        auto start_thread = false;
        {
            const std::lock_guard lock{queue_mutex};
            if (terminated.load() || !accepting_flag.load()) {
                return false;
            }
            auto command = Command{};
            command.kind = CommandKind::Attach;
            command.lease = lease;
            command.events = &events;
            command.size = size;
            command.on_open = std::move(on_open);
            command.on_finished = std::move(on_finished);
            commands.push_back(std::move(command));
            if (!thread_started) {
                thread_started = true;
                start_thread = true;
            }
        }
        if (start_thread) {
            io = std::thread([this] { io_loop(); });
        }
        return true;
    }

    auto enqueue_detach(SshConnection *lease) -> void {
        auto ack = std::make_shared<Ack>();
        {
            const std::lock_guard lock{queue_mutex};
            if (terminated.load() || !thread_started) {
                return;  // 循环已收口：该租约的终态回调早已投完。
            }
            auto command = Command{};
            command.kind = CommandKind::Detach;
            command.lease = lease;
            command.ack = ack;
            commands.push_back(std::move(command));
        }
        queue_cv.notify_all();
        auto lock = std::unique_lock{ack->mutex};
        ack->cv.wait(lock, [&ack] { return ack->done; });
    }

    auto enqueue_write(SshConnection *lease, std::span<const std::byte> bytes) -> void {
        if (bytes.empty()) {
            return;
        }
        {
            const std::lock_guard lock{queue_mutex};
            if (terminated.load()) {
                return;
            }
            if (pending_write_bytes + bytes.size() > kMaxPendingWriteBytes) {
                if (!write_overflow_warned) {
                    write_overflow_warned = true;
                    AURORA_LOG_WARN("conn", "ssh: shared session write queue full; input dropped");
                }
                return;
            }
            pending_write_bytes += bytes.size();
            auto command = Command{};
            command.kind = CommandKind::Write;
            command.lease = lease;
            command.bytes.assign(bytes.begin(), bytes.end());
            commands.push_back(std::move(command));
        }
        queue_cv.notify_all();
    }

    auto enqueue_resize(SshConnection *lease, session::Size size) -> void {
        {
            const std::lock_guard lock{queue_mutex};
            if (terminated.load()) {
                return;
            }
            auto command = Command{};
            command.kind = CommandKind::Resize;
            command.lease = lease;
            command.size = size;
            commands.push_back(std::move(command));
        }
        queue_cv.notify_all();
    }

    auto request_retry_now() -> void {
        retry_now_flag.store(true);
        queue_cv.notify_all();
    }

    auto request_stop_reconnect() -> void {
        stop_reconnect_flag.store(true);
        queue_cv.notify_all();
    }

    // ========================================================
    // I/O 线程
    // ========================================================
    auto io_loop() -> void {
        auto redials = 0;        // 本次掉线以来的重拨回数（首拨不计）。
        auto exhausted = false;  // 是否因「次数用尽」而停——终态档位要分这一格。
        auto attempt = dial_then_open_all();

        for (;;) {
            if (attempt.linked) {
                const auto reason = serve_until_incident();
                close_all_channels();
                attempt.reason = reason;
                if (reason == session::CloseReason::LocalClose) {
                    break;  // 末位标签关了：静默收口，不进退避。
                }
                // 重拨成功之后又掉线＝新的一次事故：回数重新起算（与旧单标签腿同口径）。
                redials = 0;
            }
            if (terminated.load() || stop_reconnect_flag.load()) {
                break;
            }
            if (!should_reconnect(attempt.reason)) {
                break;
            }
            const auto step = reconnect_step(redials + 1, policy);
            if (!step) {
                exhausted = true;
                break;
            }
            broadcast_progress(session::ReconnectProgress{.attempt = redials + 1,
                                                          .total = policy.max_attempts,
                                                          .delay_ms = *step,
                                                          .stop = session::ReconnectStop::None});
            if (!sleep_backoff(*step) || stop_reconnect_flag.load()) {
                break;
            }
            ++redials;
            teardown_attempt();
            attempt = dial_then_open_all();
        }

        finish_session(attempt.reason, exhausted, redials);
    }

    /// @brief 拨号 + 认证 + 给全部已挂接租约开通道；成功时把进度快照擦干净。
    auto dial_then_open_all() -> Attempt {
        linked = false;
        retry_now_flag.store(false);  // 残留的「立即重试」不该截断这一次的退避。
        session = ssh_new();
        if (session == nullptr) {
            AURORA_LOG_ERROR("conn", "ssh: cannot allocate session");
            return Attempt{.linked = false, .reason = session::CloseReason::Unknown};
        }
        // 回调表要在 connect 之前装好：连接期内服务器随时可能发反向通道请求。
        setup_agent_forwarding();

        auto secret_copy = std::optional<std::string>{};
        {
            const std::lock_guard lock{secret_mutex};
            secret_copy = secret;
        }
        const auto outcome =
            ssh_dial_and_authenticate(profile, std::move(secret_copy), session, algorithms);
        if (outcome != DialOutcome::Ok) {
            teardown_attempt();
            return Attempt{.linked = false, .reason = reason_for(outcome)};
        }

        linked = true;
        agent.available = false;
        if (profile.agent_forwarding && agent.installed) {
            // 先探本机 agent：没有 agent 还发请求，只会在远端每次连过来时被拒（裁决 7.145②）。
            auto probe = platform::agent_connect();
            if (probe != nullptr) {
                probe->close();
                agent.available = true;
            } else {
                AURORA_LOG_WARN("conn",
                                "ssh: agent forwarding requested but no local agent is reachable");
            }
        }

        for (auto index = std::size_t{0}; index < leases.size();) {
            if (leases[index]->channel == nullptr && !open_channel(*leases[index])) {
                leases.erase(leases.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }
        if (leases.empty()) {
            // 没有可服务的标签（全部开通道失败）：这一轮连上也没有意义，交给读循环收口。
            return Attempt{.linked = true, .reason = session::CloseReason::LocalClose};
        }
        broadcast_progress(session::ReconnectProgress{});  // 连上了：把浮层擦回「不在环里」。
        return Attempt{.linked = true, .reason = session::CloseReason::Unknown};
    }

    /// @brief 逐通道读 + 处理命令，直到链路事故、租约全空或本端收口。
    auto serve_until_incident() -> session::CloseReason {
        auto buffer = std::vector<std::byte>(kReadChunkBytes);
        for (;;) {
            if (process_commands()) {
                return session::CloseReason::LocalClose;
            }
            if (leases.empty()) {
                // 也许刚入队的 Attach 还没处理（与末位 Detach 的竞态）：finish_if_idle
                // 在队列锁下裁决，队列非空就不收口。
                finish_if_idle();
                if (terminated.load()) {
                    return session::CloseReason::LocalClose;
                }
                wait_tick();
                continue;
            }
            for (auto index = std::size_t{0}; index < leases.size();) {
                auto &lease = *leases[index];
                if (lease.channel == nullptr) {
                    ++index;
                    continue;
                }
                switch (pump_lease(lease, buffer)) {
                    case Pump::LinkLost:
                        return session::CloseReason::LinkLost;
                    case Pump::RemoteExit:
                        leases.erase(leases.begin() + static_cast<std::ptrdiff_t>(index));
                        continue;
                    case Pump::Alive:
                        ++index;
                        break;
                }
            }
            service_agent_forwarding();
            wait_tick();
        }
    }

    /// @brief 读一条通道；`bytes > 0` 只投给该租约。
    ///
    /// EOF 必须先于「通道已关」判：裁决 7.146 抓到的 `SSH_EOF` 缺陷在同一处次序，
    /// 多通道版本不能退回去（设计稿 D4）。
    auto pump_lease(Lease &lease, std::vector<std::byte> &buffer) -> Pump {
        for (;;) {
            const auto got = ssh_channel_read_nonblocking(
                lease.channel, buffer.data(), static_cast<std::uint32_t>(buffer.size()), 0);
            if (got > 0) {
                lease.events->on_bytes({buffer.data(), static_cast<std::size_t>(got)});
                continue;
            }
            if (got == SSH_EOF || (got == 0 && ssh_channel_is_eof(lease.channel) != 0)) {
                // 该标签的远端正常结束：只关这一条通道，链路与其余标签继续（D4/D8）。
                finish_lease(lease, session::CloseReason::RemoteExit);
                return Pump::RemoteExit;
            }
            if (got == SSH_ERROR || ssh_channel_is_closed(lease.channel) != 0) {
                // 通道级「半死」无法与链路级可靠区分，保守按整链事故处理（D4）。
                return Pump::LinkLost;
            }
            return Pump::Alive;
        }
    }

    /// @brief 开一条通道并请求 PTY/shell；失败只收这一份租约。
    auto open_channel(Lease &lease) -> bool {
        auto *channel = ssh_channel_new(session);
        if (channel == nullptr || ssh_channel_open_session(channel) != SSH_OK) {
            AURORA_LOG_ERROR("conn", "ssh: cannot open channel: ", ssh_get_error(session));
            if (channel != nullptr) {
                ssh_channel_free(channel);
            }
            finish_lease(lease, session::CloseReason::Unknown);
            return false;
        }
        // 转发请求要在 shell 之前发、且按通道各发一次（OpenSSH 按通道记这个请求）。
        if (profile.agent_forwarding && agent.available && agent.installed) {
            if (ssh_channel_request_auth_agent(channel) != SSH_OK) {
                AURORA_LOG_WARN("conn", "ssh: server refused agent forwarding: ",
                                ssh_get_error(session));
            }
        }
        if (ssh_channel_request_pty_size(channel, std::string{kPtyTerm}.c_str(),
                                         clamp_dimension(lease.size.columns),
                                         clamp_dimension(lease.size.rows)) != SSH_OK) {
            AURORA_LOG_ERROR("conn", "ssh: pty request failed: ", ssh_get_error(session));
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            finish_lease(lease, session::CloseReason::Unknown);
            return false;
        }
        if (ssh_channel_request_shell(channel) != SSH_OK) {
            AURORA_LOG_ERROR("conn", "ssh: shell request failed: ", ssh_get_error(session));
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            finish_lease(lease, session::CloseReason::Unknown);
            return false;
        }
        lease.channel = channel;
        lease.on_open();
        return true;
    }

    /// @brief 该租约的终态：关通道、投终态进度与 `on_closed`，不动其余租约。
    auto finish_lease(Lease &lease, session::CloseReason reason) -> void {
        close_channel(lease);
        lease.on_finished();
        lease.events->on_reconnect_progress(session::ReconnectProgress{
            .attempt = 0,
            .total = policy.max_attempts,
            .delay_ms = 0,
            .stop = reconnect_stop(reason, false),
        });
        lease.events->on_closed(reason);
    }

    auto close_channel(Lease &lease) -> void {
        if (lease.channel == nullptr) {
            return;
        }
        ssh_channel_close(lease.channel);
        ssh_channel_free(lease.channel);
        lease.channel = nullptr;
    }

    auto close_all_channels() -> void {
        for (auto &lease : leases) {
            close_channel(*lease);
            lease->on_finished();
        }
    }

    /// @brief 处理一批已入队的命令；返回是否已收口。
    auto process_commands() -> bool {
        auto batch = std::deque<Command>{};
        {
            const std::lock_guard lock{queue_mutex};
            batch.swap(commands);
        }
        for (auto &command : batch) {
            switch (command.kind) {
                case CommandKind::Attach:
                    handle_attach(command);
                    break;
                case CommandKind::Detach:
                    handle_detach(command);
                    break;
                case CommandKind::Write:
                    handle_write(command);
                    break;
                case CommandKind::Resize:
                    handle_resize(command);
                    break;
            }
        }
        return terminated.load();
    }

    auto handle_attach(Command &command) -> void {
        auto lease = std::make_unique<Lease>();
        lease->owner = command.lease;
        lease->events = command.events;
        lease->size = command.size;
        lease->on_open = std::move(command.on_open);
        lease->on_finished = std::move(command.on_finished);
        leases.push_back(std::move(lease));
        if (linked) {
            if (!open_channel(*leases.back())) {
                leases.pop_back();  // open_channel 已投终态回调。
                finish_if_idle();
            }
            return;
        }
        // 退避中：只登记租约并给最近一份进度，浮层文案从一开始就正确（D5）。
        if (latest_progress.attempt > 0 && command.events != nullptr) {
            command.events->on_reconnect_progress(latest_progress);
        }
    }

    auto handle_detach(Command &command) -> void {
        const auto index = find_lease(command.lease);
        if (index.has_value()) {
            auto &lease = *leases[*index];
            // 与旧单标签腿的观测面一致：close() 返回前一定投一份 LocalClose（既有
            // `itest_posix_ssh_connection` 三案的判据）。`on_finished` 先于它，alive() 立即可判。
            lease.on_finished();
            lease.events->on_reconnect_progress(session::ReconnectProgress{
                .attempt = 0,
                .total = policy.max_attempts,
                .delay_ms = 0,
                .stop = session::ReconnectStop::UserStopped,
            });
            lease.events->on_closed(session::CloseReason::LocalClose);
            close_channel(lease);
            leases.erase(leases.begin() + static_cast<std::ptrdiff_t>(*index));
        }
        finish_if_idle();
        if (command.ack != nullptr) {
            finish_ack(*command.ack);
        }
    }

    auto handle_write(Command &command) -> void {
        {
            const std::lock_guard lock{queue_mutex};
            pending_write_bytes -= std::min(pending_write_bytes, command.bytes.size());
        }
        const auto index = find_lease(command.lease);
        if (!index.has_value()) {
            return;
        }
        auto &lease = *leases[*index];
        if (lease.channel == nullptr) {
            return;  // 退避期或已收口：键入丢弃（与旧单标签腿同语义）。
        }
        auto remaining = std::span<const std::byte>{command.bytes};
        while (!remaining.empty()) {
            const auto written = ssh_channel_write(lease.channel, remaining.data(),
                                                   static_cast<std::uint32_t>(remaining.size()));
            if (written <= 0) {
                return;
            }
            remaining = remaining.subspan(static_cast<std::size_t>(written));
        }
    }

    auto handle_resize(Command &command) -> void {
        const auto index = find_lease(command.lease);
        if (!index.has_value()) {
            return;
        }
        auto &lease = *leases[*index];
        lease.size = command.size;  // 退避期也记账：重拨按新行列开 PTY（D9）。
        if (lease.channel == nullptr) {
            return;
        }
        if (ssh_channel_change_pty_size(lease.channel, clamp_dimension(command.size.columns),
                                        clamp_dimension(command.size.rows)) != SSH_OK) {
            AURORA_LOG_WARN("conn", "ssh: resize rejected: ", ssh_get_error(session));
        }
    }

    [[nodiscard]] auto find_lease(SshConnection *owner) const -> std::optional<std::size_t> {
        for (auto index = std::size_t{0}; index < leases.size(); ++index) {
            if (leases[index]->owner == owner) {
                return index;
            }
        }
        return std::nullopt;
    }

    /// @brief 队列锁下裁决「是否该收口」：无条件收口会让末位关闭与新标签挂接竞态（D6）。
    auto finish_if_idle() -> void {
        const std::lock_guard lock{queue_mutex};
        if (!leases.empty() || !commands.empty()) {
            return;
        }
        accepting_flag.store(false);
        terminated.store(true);
    }

    auto broadcast_progress(const session::ReconnectProgress &progress) -> void {
        latest_progress = progress;
        for (auto &lease : leases) {
            lease->events->on_reconnect_progress(progress);
        }
    }

    /// @brief 退避休眠：每片处理命令、查三枚原子位；回 false＝本端收口。
    auto sleep_backoff(int delay_ms) -> bool {
        auto left = std::max(delay_ms, 0);
        while (left > 0) {
            if (terminated.load()) {
                return false;
            }
            if (stop_reconnect_flag.load()) {
                return true;  // 交回环里判终止位，好让终态档落「用户停止」。
            }
            if (retry_now_flag.exchange(false)) {
                return true;  // 「立即重试」截断剩余等待，最坏比预定晚一个分片。
            }
            const auto slice = std::min(left, kBackoffSliceMs);
            {
                auto lock = std::unique_lock{queue_mutex};
                queue_cv.wait_for(lock, std::chrono::milliseconds(slice));
            }
            if (process_commands()) {
                return false;
            }
            left -= slice;
        }
        return !terminated.load();
    }

    auto wait_tick() -> void {
        auto lock = std::unique_lock{queue_mutex};
        queue_cv.wait_for(lock, kReadIdle);
    }

    /// @brief 终态收口：先给仍挂接的租约投终态回调，再关入口、摘队列、释放链路。
    auto finish_session(session::CloseReason reason, bool exhausted, int redials) -> void {
        const auto final_reason =
            terminated.load() ? session::CloseReason::LocalClose : reason;
        const auto progress = session::ReconnectProgress{
            .attempt = redials,
            .total = policy.max_attempts,
            .delay_ms = 0,
            .stop = reconnect_stop(final_reason, exhausted),
        };
        latest_progress = progress;

        // 1. 仍挂接的租约：终态进度 + on_closed。close() 在等 ack，所以回调一定先投完。
        for (auto &lease : leases) {
            close_channel(*lease);
            lease->on_finished();
            lease->events->on_reconnect_progress(progress);
            lease->events->on_closed(final_reason);
        }
        // 2. 关入口并摘走当时已入队的命令：之后的 attach 一律拒绝、detach 立即返回。
        auto pending = std::deque<Command>{};
        {
            const std::lock_guard lock{queue_mutex};
            accepting_flag.store(false);
            pending.swap(commands);
        }
        for (auto &command : pending) {
            if (command.kind == CommandKind::Detach) {
                if (command.ack != nullptr) {
                    finish_ack(*command.ack);
                }
                continue;
            }
            if (command.kind == CommandKind::Attach) {
                // 入队后、处理前链路就落了终态：按「刚连上就断」投一次，标签不会挂死。
                if (command.on_finished) {
                    command.on_finished();
                }
                if (command.events != nullptr) {
                    command.events->on_reconnect_progress(progress);
                    command.events->on_closed(final_reason);
                }
            }
            // Write / Resize：直接丢，字节账目在下一步清零。
        }
        // 3. 置终止位并收尾：第二步之后新入队的 Detach 在这里补 ack（回调均已投完）。
        auto late = std::deque<Command>{};
        {
            const std::lock_guard lock{queue_mutex};
            terminated.store(true);
            pending_write_bytes = 0;
            leases.clear();
            late.swap(commands);
        }
        for (auto &command : late) {
            if (command.kind == CommandKind::Detach && command.ack != nullptr) {
                finish_ack(*command.ack);
            }
        }
        teardown_attempt();
    }

    static auto finish_ack(Ack &ack) -> void {
        {
            const std::lock_guard lock{ack.mutex};
            ack.done = true;
        }
        ack.cv.notify_all();
    }

    // ========================================================
    // agent 转发（逐连接回调表 + 逐通道中继）
    // ========================================================
    auto setup_agent_forwarding() -> void {
        if (!profile.agent_forwarding || session == nullptr) {
            return;
        }
        ssh_callbacks_init(&agent.callbacks);
        agent.callbacks.userdata = this;
        agent.callbacks.channel_open_request_auth_agent_function = &Impl::handle_agent_channel_open;
        if (ssh_set_callbacks(session, &agent.callbacks) != SSH_OK) {
            AURORA_LOG_WARN("conn", "ssh: cannot install agent-forwarding callbacks: ",
                            ssh_get_error(session));
            return;
        }
        agent.installed = true;
    }

    static auto handle_agent_channel_open(ssh_session_struct *session, void *userdata)
        -> ssh_channel_struct * {
        auto *self = static_cast<Impl *>(userdata);
        if (self == nullptr || !self->agent.installed) {
            return nullptr;
        }
        auto agent_socket = platform::agent_connect();
        if (!agent_socket) {
            AURORA_LOG_WARN("conn",
                            "ssh: agent-forwarding channel refused: local agent unreachable");
            return nullptr;
        }
        auto *channel = ssh_channel_new(session);
        if (channel == nullptr) {
            return nullptr;
        }
        if (self->agent.channels.size() >= kMaxForwardedAgentChannels) {
            ssh_channel_free(channel);
            return nullptr;
        }
        self->agent.channels.push_back(
            Forwarded{channel, std::move(agent_socket), false});
        return channel;
    }

    auto service_agent_forwarding() -> void {
        if (agent.channels.empty()) {
            return;
        }
        auto buffer = std::array<std::uint8_t, kAgentChunkBytes>{};
        // 按下标走：循环里的 libssh 调用可能当场派发新的通道请求，回调会往同一张表
        // push_back，迭代器与元素引用都可能在半途失效。
        for (auto index = std::size_t{0}; index < agent.channels.size();) {
            auto failed = false;
            // 远端 → 本机 agent：一轮搬空当前可读字节。
            while (!failed && !agent.channels[index].remote_eof) {
                const auto got = ssh_channel_read_nonblocking(
                    agent.channels[index].channel, buffer.data(),
                    static_cast<std::uint32_t>(buffer.size()), 0);
                if (got == SSH_EOF) {
                    agent.channels[index].remote_eof = true;
                    break;
                }
                if (got < 0) {
                    failed = true;
                    break;
                }
                if (got == 0) {
                    break;
                }
                if (!agent.channels[index].agent->write(
                        {buffer.data(), static_cast<std::size_t>(got)})) {
                    failed = true;
                }
            }
            // 本机 agent → 远端：应答小，一轮读完。
            while (!failed) {
                const auto read = agent.channels[index].agent->read(buffer, 0);
                if (read.outcome == platform::AgentReadOutcome::Timeout) {
                    break;
                }
                if (read.outcome != platform::AgentReadOutcome::Data) {
                    failed = true;  // agent 收摊或出错：这条反向通道失效。
                    break;
                }
                if (ssh_channel_write(agent.channels[index].channel, buffer.data(),
                                      static_cast<std::uint32_t>(read.bytes)) !=
                    static_cast<int>(read.bytes)) {
                    failed = true;
                }
            }
            auto &forwarded = agent.channels[index];
            if (failed && ssh_channel_is_closed(forwarded.channel) == 0) {
                ssh_channel_send_eof(forwarded.channel);
            }
            if (failed || ssh_channel_is_closed(forwarded.channel) != 0) {
                forwarded.agent->close();
                ssh_channel_close(forwarded.channel);
                ssh_channel_free(forwarded.channel);
                agent.channels.erase(agent.channels.begin() +
                                     static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }
    }

    auto close_agent_forwarding() -> void {
        for (auto &forwarded : agent.channels) {
            if (forwarded.agent != nullptr) {
                forwarded.agent->close();
            }
            if (forwarded.channel != nullptr) {
                ssh_channel_close(forwarded.channel);
                ssh_channel_free(forwarded.channel);
            }
        }
        agent.channels.clear();
    }

    auto teardown_attempt() -> void {
        // 反向通道先收：它们挂在同一个 session 上，必须在 ssh_free 之前释放。
        close_agent_forwarding();
        close_all_channels();
        if (session != nullptr) {
            ssh_disconnect(session);
            ssh_free(session);
            session = nullptr;
        }
        // 回调表被 libssh 按指针持有，只能在 session 释放之后丢掉它的「已装」标记。
        agent.installed = false;
        agent.available = false;
        linked = false;
    }
};

SshSharedSession::SshSharedSession(const SshProfile &profile, std::optional<std::string> secret,
                                   RetryPolicy policy, SshAlgorithmOverrides algorithms)
    : impl_{std::make_unique<Impl>(profile, std::move(secret), policy, std::move(algorithms))} {}

SshSharedSession::~SshSharedSession() = default;

auto SshSharedSession::matches(const SshSessionKey &key,
                               const std::optional<std::string> &secret) const -> bool {
    if (!(impl_->key == key)) {
        return false;
    }
    const std::lock_guard lock{impl_->secret_mutex};
    return same_secret(impl_->secret, secret);
}

auto SshSharedSession::accepting() const noexcept -> bool {
    return impl_->accepting_flag.load();
}

auto SshSharedSession::attach(SshConnection *lease, session::ConnectionEvents &events,
                              session::Size size, std::function<void()> on_open,
                              std::function<void()> on_finished) -> bool {
    return impl_->enqueue_attach(lease, events, size, std::move(on_open), std::move(on_finished));
}

auto SshSharedSession::detach(SshConnection *lease) -> void {
    impl_->enqueue_detach(lease);
}

auto SshSharedSession::write(SshConnection *lease, std::span<const std::byte> bytes) -> void {
    impl_->enqueue_write(lease, bytes);
}

auto SshSharedSession::resize(SshConnection *lease, session::Size size) -> void {
    impl_->enqueue_resize(lease, size);
}

auto SshSharedSession::retry_now() -> void {
    impl_->request_retry_now();
}

auto SshSharedSession::stop_reconnect() -> void {
    impl_->request_stop_reconnect();
}

}  // namespace borealis::conn
