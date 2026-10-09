// ============================================================
// SSH 真传输连接实现（src/conn/ssh_connection.cpp）
// ------------------------------------------------------------
// 本文件是 SPEC.FEAT.CONN.02 集成式 SSH 传输在 Linux 腿的通道与读循环知识：
// PTY 通道、shell 请求与字节流泵；建会话选项、连接、主机密钥核对（D3②/D8 裁决）
// 与认证在共用腿 conn/ssh_dial.cpp（SFTP 浏览器 SPEC.FEAT.CONN.04 同款消费）。
// 对照 forkpty 腿的三条纪律同样适用：
//   1. 连接/认证/读都在读线程上做——ssh_connect 会阻塞数秒，绝不占 UI 线程。
//   2. close() 先置 closing 再 join 读线程，最后才释放 libssh 句柄，回调不悬空。
//   3. alive() 只读原子量，免锁（UI 线程随时会问）。
// 凭据（CONN.09）：明文只在构造传入的 secret_ 里存活，每次尝试复制一份交给 dial 腿，
// 形参副本随该腿返回消失；成员副本驻留到 close()（裁决 7.99 D7②：重拨要重新认证）。
// 错误日志只打 libssh 的错误文本（不含口令），绝不回显秘密材料。
// ============================================================

#include "ssh_connection.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include <libssh/libssh.h>

#include "aurora/core/log.h"
#include "conn/ssh_dial.h"

namespace borealis::conn {

namespace {

/// @brief 读循环单块上限：与 forkpty 腿同款，原样整块投递给会话层。
constexpr std::size_t kReadChunkBytes = 32768;

/// @brief 非阻塞读空转的休眠步长：close() 靠 closing 标志叫停循环，步长即关闭延迟上界。
constexpr useconds_t kReadIdleUs = 30000;

/// @brief PTY 请求的 TERM 值：与本地腿 `kPtyDefaultEnvironment` 的 TERM 同源口径。
constexpr std::string_view kPtyTerm = "xterm-256color";

/// @brief 退避休眠的分片长度：close() 与两枚用户动作的叫停延迟上界（裁决 7.99 D1 代价 (b)）。
constexpr int kBackoffSliceMs = 200;

/// @brief 拨号档位 → 关闭归因：只有 `Network` 落进可重拨集（分类表在
///        `conn::should_reconnect`，裁决 7.99 D2①）。通道/PTY/shell 段的失败不在此列，
///       认证既然过了，那一步是服务器侧拒绝而非链路问题，重拨也只会再拒一回。
[[nodiscard]] auto reason_for(DialOutcome outcome) -> session::CloseReason {
    switch (outcome) {
        case DialOutcome::Network:
            return session::CloseReason::DialNetwork;
        case DialOutcome::Auth:
            return session::CloseReason::AuthFailed;
        case DialOutcome::HostKey:
            return session::CloseReason::HostKeyRejected;
        case DialOutcome::Unallocated:
        case DialOutcome::Ok:
            break;
    }
    // Unallocated＝本机句柄都开不出来，归不进任何一类网络/服务器原因；保守按不可重拨。
    return session::CloseReason::Unknown;
}

}  // namespace

SshConnection::SshConnection(const SshProfile &profile, std::optional<std::string> secret,
                             session::Size initial_size, RetryPolicy reconnect_policy)
    : profile_{profile},
      secret_{std::move(secret)},
      size_{initial_size},
      reconnect_policy_{reconnect_policy} {}

SshConnection::~SshConnection() {
    close();
    teardown();
}

auto SshConnection::start(session::ConnectionEvents &events) -> void {
    if (started_.exchange(true) || closing_.load()) {
        return;
    }
    events_ = &events;
    // 连接/认证会阻塞数秒，全部推到读线程：start() 立即返回（forkpty 同款语义）。
    reader_ = std::thread([this] { run_loop(); });
}

auto SshConnection::run_loop() -> void {
    auto redials = 0;        // 本次掉线以来的重拨回数（首拨不计）。
    auto exhausted = false;  // 是否因「次数用尽」而停——终态档位要分这一格。
    auto attempt = dial_and_serve();

    // 退避环：连上就一直读到掉线为止，失败或掉线后按分类决定要不要再来一回。
    // 本端 close() 与「本端主动关闭」的归因都让环直接收口，不投第二次尝试。
    while (!closing_.load() && attempt.reason != session::CloseReason::LocalClose) {
        if (attempt.linked) {
            // 重拨成功之后又掉线＝新的一次事故：回数与退避档位都重新起算，
            // 于是「抖一下连上又断」不会把三回配额一次吃光。
            redials = 0;
        }
        if (stop_reconnect_.load() || !should_reconnect(attempt.reason)) {
            break;
        }
        const auto step = reconnect_step(redials + 1, reconnect_policy_);
        if (!step) {
            exhausted = true;
            break;
        }
        post_progress(session::ReconnectProgress{.attempt = redials + 1,
                                                 .total = reconnect_policy_.max_attempts,
                                                 .delay_ms = *step,
                                                 .stop = session::ReconnectStop::None});
        if (!sleep_backoff(*step) || stop_reconnect_.load()) {
            break;
        }
        ++redials;
        teardown();  // 句柄换血：上一回的会话与通道先释放，再重跑同一段拨号序列。
        attempt = dial_and_serve();
    }

    const auto reason = closing_.load() ? session::CloseReason::LocalClose : attempt.reason;
    post_progress(session::ReconnectProgress{.attempt = redials,
                                             .total = reconnect_policy_.max_attempts,
                                             .delay_ms = 0,
                                             .stop = reconnect_stop(reason, exhausted)});
    events_->on_closed(reason);
}

auto SshConnection::dial_and_serve() -> Attempt {
    // ---- 建会话 ----
    {
        const std::lock_guard lock{state_mutex_};
        session_ = ssh_new();
    }
    if (session_ == nullptr) {
        AURORA_LOG_ERROR("conn", "ssh: cannot allocate session");
        return Attempt{.linked = false, .reason = session::CloseReason::Unknown};
    }

    // ---- 连接 → 主机密钥核对 → 认证（共用腿 ssh_dial，SFTP 浏览器同款）----
    // 每次尝试把成员里的明文**复制**一份按值交进去，形参副本随 dial 腿返回消失；
    // 成员副本要到 close() 才清（裁决 7.99 D7②）。
    retry_now_.store(false);  // 残留的「立即重试」不该截断这一次的退避。
    const auto outcome = ssh_dial_and_authenticate(profile_, secret_, session_);
    if (outcome != DialOutcome::Ok) {
        return Attempt{.linked = false, .reason = reason_for(outcome)};
    }

    // ---- 通道 + PTY + shell ----
    {
        const std::lock_guard lock{state_mutex_};
        channel_ = ssh_channel_new(session_);
    }
    if (channel_ == nullptr || ssh_channel_open_session(channel_) != SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: cannot open channel: ", ssh_get_error(session_));
        return Attempt{.linked = false, .reason = session::CloseReason::Unknown};
    }
    // PTY 取当下的 size_：断线期间用户改过窗口就以新尺寸拨（裁决 7.99 D7①）。
    const auto cols = static_cast<int>(std::clamp(std::max(size_.columns, std::size_t{1}),
                                                  std::size_t{1}, std::size_t{32767}));
    const auto rows = static_cast<int>(std::clamp(std::max(size_.rows, std::size_t{1}),
                                                  std::size_t{1}, std::size_t{32767}));
    // PTY 与首尺寸一次到位（0.12 的 request_pty 不再收 TERM 参数，走带尺寸的变体）。
    if (ssh_channel_request_pty_size(channel_, std::string{kPtyTerm}.c_str(), cols, rows) !=
        SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: pty request failed: ", ssh_get_error(session_));
        return Attempt{.linked = false, .reason = session::CloseReason::Unknown};
    }
    if (ssh_channel_request_shell(channel_) != SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: shell request failed: ", ssh_get_error(session_));
        return Attempt{.linked = false, .reason = session::CloseReason::Unknown};
    }
    channel_open_.store(true);
    post_progress(session::ReconnectProgress{});  // 连上了：把快照擦回「不在环里」。

    const auto reason = read_loop();
    channel_open_.store(false);
    return Attempt{.linked = true, .reason = reason};
}

auto SshConnection::read_loop() -> session::CloseReason {
    // ---- 读循环（非阻塞读 + 空转休眠；closing 标志是唯一停机令）----
    auto buffer = std::vector<std::byte>(kReadChunkBytes);
    for (;;) {
        if (closing_.load()) {
            return session::CloseReason::LocalClose;
        }
        const auto bytes_read = ssh_channel_read_nonblocking(channel_, buffer.data(),
                                                             static_cast<std::uint32_t>(
                                                                 buffer.size()),
                                                             0);
        if (bytes_read > 0) {
            events_->on_bytes({buffer.data(), static_cast<std::size_t>(bytes_read)});
            continue;
        }
        if (bytes_read == SSH_ERROR || ssh_channel_is_closed(channel_)) {
            return session::CloseReason::LinkLost;
        }
        if (bytes_read == 0 && ssh_channel_is_eof(channel_)) {
            return session::CloseReason::RemoteExit;
        }
        ::usleep(kReadIdleUs);
    }
}

auto SshConnection::sleep_backoff(int delay_ms) -> bool {
    auto left = std::max(delay_ms, 0);
    while (left > 0) {
        if (closing_.load()) {
            return false;  // 本端要关：环就此收口，归因落 LocalClose。
        }
        if (stop_reconnect_.load()) {
            return true;   // 交回环里判终止位，好让终态档位落「用户停止」而不是「本端关闭」。
        }
        if (retry_now_.exchange(false)) {
            return true;   // 「立即重试」截断剩余等待，最坏比预定晚一个分片。
        }
        const auto slice = std::min(left, kBackoffSliceMs);
        ::usleep(static_cast<useconds_t>(slice) * 1000U);
        left -= slice;
    }
    return !closing_.load();
}

auto SshConnection::post_progress(const session::ReconnectProgress &progress) -> void {
    if (events_ != nullptr) {
        events_->on_reconnect_progress(progress);
    }
}

auto SshConnection::write(std::span<const std::byte> bytes) -> void {
    const std::lock_guard lock{write_mutex_};
    if (closing_.load() || !channel_open_.load()) {
        return;
    }
    auto remaining = bytes;
    while (!remaining.empty()) {
        const auto written = ssh_channel_write(channel_, remaining.data(),
                                               static_cast<std::uint32_t>(remaining.size()));
        if (written <= 0) {
            return;  // 对端已走或缓冲异常：与 forkpty 腿同语义，键入丢弃。
        }
        remaining = remaining.subspan(static_cast<std::size_t>(written));
    }
}

auto SshConnection::resize(session::Size size) -> void {
    const std::lock_guard lock{state_mutex_};
    // 无通道时也要记下尺寸：退避环的下一次拨号取的就是这份最新值（裁决 7.99 D7①，
    // 修掉「断线期间改窗口，重拨仍按旧行列」）。
    size_ = size;
    if (closing_.load() || !channel_open_.load()) {
        return;
    }
    const auto cols = static_cast<int>(std::clamp(std::max(size.columns, std::size_t{1}),
                                                  std::size_t{1}, std::size_t{32767}));
    const auto rows = static_cast<int>(std::clamp(std::max(size.rows, std::size_t{1}),
                                                  std::size_t{1}, std::size_t{32767}));
    if (ssh_channel_change_pty_size(channel_, cols, rows) != SSH_OK) {
        AURORA_LOG_WARN("conn", "ssh: resize rejected: ", ssh_get_error(session_));
    }
}

auto SshConnection::close() -> void {
    if (closing_.exchange(true)) {
        if (reader_.joinable()) {
            reader_.join();
        }
        return;
    }
    if (reader_.joinable()) {
        reader_.join();  // 读线程每 30ms 查 closing、退避每片 200ms 查，这里至多等一个片长。
    }
    teardown();
    // 驻留期到此为止（裁决 7.99 D7②）：明文副本只活到 close() 返回。
    secret_.reset();
}

auto SshConnection::teardown() -> void {
    const std::lock_guard lock{state_mutex_};
    if (channel_ != nullptr) {
        ssh_channel_close(channel_);
        ssh_channel_free(channel_);
        channel_ = nullptr;
    }
    if (session_ != nullptr) {
        ssh_disconnect(session_);
        ssh_free(session_);
        session_ = nullptr;
    }
    channel_open_.store(false);
}

auto SshConnection::alive() const noexcept -> bool {
    return started_.load() && !closing_.load() && channel_open_.load();
}

}  // namespace borealis::conn
