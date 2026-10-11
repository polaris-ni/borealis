// ============================================================
// 串口连接腿实现（src/conn/serial_connection.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 的串口传输腿：只负责「打开一条串口、把字节双向泵起来、掉了按档重拨」。
// 平台知识（termios / COMM）一律经 `platform/serial.h` 的抽象调用，本文件不含任何平台头
// （AGENTS §4.5 第 23 条）。三条继承自 SSH 腿的纪律同样适用：
//   1. 打开与置参会阻塞，全部在读线程上做，`start()` 立即返回；
//   2. `close()` 先置终止位再 join 读线程，之后才销毁端口，回调不悬空；
//   3. `alive()` 只读原子量，免锁（UI 线程随时会问）。
//
// 帧参数每次尝试都重新折一份（`conn::fold_serial_frame`），故热改过的波特率不会跟着重拨走——
// 需求那句「断开重连后按档案参数重开」在这里是天然形态，不是一处特殊处理。
// ============================================================

#include "serial_connection.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "aurora/core/log.h"

namespace borealis::conn {

namespace {

/// @brief 读循环单块上限：与 SSH / forkpty 腿同款，原样整块投递给会话层。
constexpr std::size_t kReadChunkBytes = 32768;

/// @brief 空闲链路的一个等待片：`close()` 的 join 延迟上界，也是空闲时一轮「探测→再探」的间隔。
///        等待本身在锁外，故 UI 线程取 `port_mutex_` 的代价与它无关（探测非阻塞，与 SSH 腿的
///        `read_nonblocking` + 空闲休眠同款纪律）。
constexpr int kReadSliceMs = 20;

/// @brief 锁内探测的超时档：0＝只问一次立刻回，绝不把定时等待带进 `port_mutex_`。
constexpr int kProbeTimeoutMs = 0;

/// @brief 退避休眠的分片长度：`close()` 与两枚用户动作的叫停延迟上界（裁决 7.99 D1 代价 (b)）。
constexpr int kBackoffSliceMs = 200;

}  // namespace

auto serial_close_reason(platform::SerialFailure failure) -> session::CloseReason {
    switch (failure) {
    case platform::SerialFailure::Denied:
    case platform::SerialFailure::NoNode:
    case platform::SerialFailure::Io:
    case platform::SerialFailure::ConfigRejected:
        // 事实类失败：权限、节点根本不存在、写不下去的参数——重拨只会再撞同一堵墙。落 `Unknown`
        // 即按裁决 7.99 D2① 的保守档不重拨（裁决 7.110 D7① 的第 2 条代价口径）。
        return session::CloseReason::Unknown;
    case platform::SerialFailure::Hangup:
    case platform::SerialFailure::Busy:
        // 状态类失败：节点暂缺（USB 口拔掉但目录未消失）与被别的程序占用——下一回可能就没了。
        return session::CloseReason::DialNetwork;
    case platform::SerialFailure::None:
        break;
    }
    // `None` 是成功档，不该走到归因；保守按不可重拨，免得一次记账错误换来无限重拨。
    return session::CloseReason::Unknown;
}

SerialConnection::SerialConnection(SerialProfile profile, RetryPolicy reconnect_policy)
    : profile_{std::move(profile)}, reconnect_policy_{reconnect_policy} {}

SerialConnection::~SerialConnection() { close(); }

auto SerialConnection::start(session::ConnectionEvents &events) -> void {
    if (started_.exchange(true) || closing_.load()) {
        return;
    }
    events_ = &events;
    // 打开设备与 tcsetattr/GetCommState 都会阻塞，全部推到读线程（forkpty 与 SSH 同款语义）。
    reader_ = std::thread([this] { run_loop(); });
}

auto SerialConnection::run_loop() -> void {
    auto redials = 0;        // 本次掉线以来的重拨回数（首拨不计）。
    auto exhausted = false;  // 是否因「次数用尽」而停——终态档位要分这一格。
    auto attempt = open_and_serve();

    // 退避环：连上就一直读到掉线为止，失败或掉线后按分类决定要不要再来一回。
    while (!closing_.load() && attempt.reason != session::CloseReason::LocalClose) {
        if (attempt.linked) {
            // 重拨成功后又掉线＝新的一次事故：回数与退避档重新起算（口径同 SSH 腿）。
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
        attempt = open_and_serve();
    }

    const auto reason = closing_.load() ? session::CloseReason::LocalClose : attempt.reason;
    post_progress(session::ReconnectProgress{.attempt = redials,
                                             .total = reconnect_policy_.max_attempts,
                                             .delay_ms = 0,
                                             .stop = reconnect_stop(reason, exhausted)});
    events_->on_closed(reason);
}

auto SerialConnection::open_and_serve() -> Attempt {
    // 每次尝试重折一份参数：档位表与取值域只有 `conn::serial_model` 一处（裁决 7.110 D4①）。
    const auto frame = fold_serial_frame(profile_);
    if (!frame) {
        AURORA_LOG_ERROR("conn", "serial: profile is not foldable, device=", profile_.device_path);
        return Attempt{.linked = false, .reason = session::CloseReason::Unknown};
    }
    retry_now_.store(false);  // 残留的「立即重试」不该截断这一次的打开。
    auto opened = platform::serial_open(profile_.device_path, *frame);
    if (!opened.ok()) {
        AURORA_LOG_WARN("conn", "serial: open failed, device=", profile_.device_path,
                        " failure=", static_cast<int>(opened.failure));
        return Attempt{.linked = false, .reason = serial_close_reason(opened.failure)};
    }
    {
        const std::lock_guard lock{port_mutex_};
        port_ = std::move(opened.port);
    }
    port_open_.store(true);
    post_progress(session::ReconnectProgress{});  // 连上了：把快照擦回「不在环里」。

    const auto reason = read_loop();
    port_open_.store(false);
    teardown();  // 本回的端口先关掉：下一回打开的是新句柄，退避期间也不能留着占设备。
    return Attempt{.linked = true, .reason = reason};
}

auto SerialConnection::read_loop() -> session::CloseReason {
    auto buffer = std::vector<std::uint8_t>(kReadChunkBytes);
    for (;;) {
        if (closing_.load()) {
            return session::CloseReason::LocalClose;
        }
        auto outcome = platform::SerialRead{};
        {
            // 锁内只做一次「超时 0 档」的非阻塞探测，空闲等待留在锁外：把 20 ms 的定时读留在
            // `port_mutex_` 里，UI 线程的每一次 `write`/`set_baud`/`current_baud` 都要排在一个读
            // 分片之后（架构 §3.4「锁内不做 IO」，SSH 腿 `read_nonblocking` + 空闲休眠同款纪律）。
            const std::lock_guard lock{port_mutex_};
            if (port_ == nullptr) {
                return session::CloseReason::LinkLost;  // 本端已收口：不再触碰端口。
            }
            outcome = port_->read({buffer.data(), buffer.size()}, kProbeTimeoutMs);
        }
        switch (outcome.outcome) {
        case platform::SerialReadOutcome::Data:
            // 出锁再投递：喂进状态机的临界区不该把设备端口算进去（架构 §3.4「锁内不做 IO」）。
            events_->on_bytes({reinterpret_cast<const std::byte *>(buffer.data()), outcome.bytes});
            continue;  // 线上还有字节就立刻再探，不白等一个空闲片。
        case platform::SerialReadOutcome::Timeout:
            std::this_thread::sleep_for(std::chrono::milliseconds{kReadSliceMs});
            continue;
        case platform::SerialReadOutcome::Closed:
        case platform::SerialReadOutcome::Error:
            // 串口没有「远端进程正常结束」：EOF 只代表设备没了（F9 的替身形态），读错同理
            // （裁决 7.110 D7① 的第 1 条代价口径，`RemoteExit` 在本腿永不出现）。
            AURORA_LOG_INFO("conn", "serial: read side ended, device=", profile_.device_path,
                            " outcome=", static_cast<int>(outcome.outcome));
            return session::CloseReason::LinkLost;
        }
    }
}

auto SerialConnection::sleep_backoff(int delay_ms) -> bool {
    auto left = std::max(delay_ms, 0);
    while (left > 0) {
        if (closing_.load()) {
            return false;  // 本端要关：环就此收口，归因落 LocalClose。
        }
        if (stop_reconnect_.load()) {
            return true;  // 交回环里判终止位，好让终态档位落「用户停止」而不是「本端关闭」。
        }
        if (retry_now_.exchange(false)) {
            return true;  // 「立即重试」截断剩余等待，最坏比预定晚一个分片。
        }
        const auto slice = std::min(left, kBackoffSliceMs);
        std::this_thread::sleep_for(std::chrono::milliseconds{slice});
        left -= slice;
    }
    return !closing_.load();
}

auto SerialConnection::post_progress(const session::ReconnectProgress &progress) -> void {
    if (events_ != nullptr) {
        events_->on_reconnect_progress(progress);
    }
}

auto SerialConnection::write(std::span<const std::byte> bytes) -> void {
    if (closing_.load() || !port_open_.load()) {
        return;  // 未连上（含退避期间）：与 SSH 腿同语义，键入丢弃。
    }
    const std::lock_guard lock{port_mutex_};
    if (port_ == nullptr) {
        return;
    }
    static_cast<void>(port_->write(
        {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()}));
}

auto SerialConnection::set_baud(int baud) -> bool {
    if (closing_.load() || !port_open_.load()) {
        return false;  // 没连着就谈不上热改：调用侧界面保持原档（裁决 7.110 D6②）。
    }
    const std::lock_guard lock{port_mutex_};
    if (port_ == nullptr) {
        return false;
    }
    return port_->set_baud(baud);
}

auto SerialConnection::current_baud() const -> int {
    const std::lock_guard lock{port_mutex_};
    // 未连上时回档案档：退避期间「会话当前跑什么波特率」没有真值，档案档是最不撒谎的读数。
    return port_ != nullptr ? port_->current_baud() : profile_.baud;
}

auto SerialConnection::resize(session::Size) -> void {
    // 有意空操作：串口是裸字节管道，设备侧不重排（见文件头）。本地网格的换行归会话层。
}

auto SerialConnection::close() -> void {
    if (closing_.exchange(true)) {
        if (reader_.joinable()) {
            reader_.join();
        }
        return;
    }
    if (reader_.joinable()) {
        // 读线程每片读（20 ms）与每片退避（200 ms）都查 closing_，这里至多等一个片长。
        reader_.join();
    }
    teardown();
}

auto SerialConnection::teardown() -> void {
    const std::lock_guard lock{port_mutex_};
    if (port_ != nullptr) {
        port_->close();
        port_.reset();
    }
    port_open_.store(false);
}

auto SerialConnection::alive() const noexcept -> bool {
    return started_.load() && !closing_.load() && port_open_.load();
}

}  // namespace borealis::conn
