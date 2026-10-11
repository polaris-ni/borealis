// ============================================================
// Telnet 连接腿实现（src/conn/telnet_connection.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.06` 的传输腿：只负责「连上一条 TCP、把字节双向泵起来、IAC 段交给协商件、
// 掉了按档重拨」。裸 socket 经 `platform/tcp.h` 的抽象调用，IAC 规程经
// `conn/telnet_negotiation` 调用，本文件不含平台头也不写一个命令字节（AGENTS §4.5 第 23 条、
// 裁决 7.112 D11①）。继承自串口腿的三条纪律同样适用：
//   1. 建连会阻塞，全部在读线程上做，`start()` 立即返回；
//   2. `close()` 先置终止位再 join 读线程，之后才销毁 socket，回调不悬空；
//   3. `alive()` 只读原子量，免锁（UI 线程随时会问）。
//
// 本腿独有的一格：**写出侧失败不改判归因**。实测 F4 说半关时写侧仍连续成功，故「写回去了」
// 不等于「对端收到了」，掉线一律从读侧判（稿 §5 末条）。
// ============================================================

#include "telnet_connection.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "aurora/core/log.h"

namespace borealis::conn {

namespace {

/// @brief 单块读上限：与 SSH / 串口腿同款，交协商件后原样整块投给会话层。
constexpr std::size_t kReadChunkBytes = 32768;

/// @brief 空闲链路的一个等待片：`close()` 的 join 延迟上界，也是尺寸槽两次取样的间隔。
constexpr int kReadSliceMs = 20;

/// @brief 锁内探测的超时档：0＝只问一次立刻回，绝不把定时等待带进 `stream_mutex_`。
constexpr int kProbeTimeoutMs = 0;

/// @brief 退避休眠的分片长度：`close()` 与两枚用户动作的叫停延迟上界（裁决 7.99 D1 代价 (b)）。
constexpr int kBackoffSliceMs = 200;

/// @brief 建连超时档：口径同 `tunnel_client.cpp` 的 `kLocalConnectTimeoutMs`（同一枚 5 s），
///        装配层不再另开一档（稿 §4 那句「取一常量」落在本文件）。
constexpr int kConnectTimeoutMs = 5000;

/// @brief 把 UI 侧的 `size_t` 维度折成协商件认的档：超过 65535 的折成 0。
///
/// 直接 `static_cast<int>` 会把 70000 变成负数——那既不是「不可发」也不是「未定」，而协商件
/// 的判据是「不在 [1, 65535] 即不发也不记账」（裁决 7.113④），钳成 0 才落在那一档上。
[[nodiscard]] auto to_window_value(std::size_t value) -> int {
    return value > static_cast<std::size_t>(kTelnetMaxWindowSize)
               ? 0
               : static_cast<int>(value);
}

}  // namespace

auto telnet_close_reason(TelnetLinkEnd end) -> session::CloseReason {
    switch (end) {
    case TelnetLinkEnd::DialFailed:
        // 建连阶段的网络类失败：端口下一回可能就开了 ⇒ 可重拨。
        return session::CloseReason::DialNetwork;
    case TelnetLinkEnd::RemoteEof:
        // 判据 7 与裁决 7.112 D9① 的落点：telnetd 在远端进程 exit 后关 TCP 是**正常结束**，
        // 落 RemoteExit 即按分类表不进重拨环（串口那棒把 EOF 落 LinkLost，分野在协议语义）。
        return session::CloseReason::RemoteExit;
    case TelnetLinkEnd::ReadError:
        // ECONNRESET 一类的读错（实测 F3 的另一态）：链路丢了，值得再拨。
        return session::CloseReason::LinkLost;
    case TelnetLinkEnd::LocalClose:
        return session::CloseReason::LocalClose;
    }
    // 档位只能是四者之一；走到这里说明状态被外部破坏，保守按不可重拨（裁决 7.99 D2①）。
    return session::CloseReason::Unknown;
}

TelnetConnection::TelnetConnection(TelnetProfile profile, RetryPolicy reconnect_policy)
    : profile_{std::move(profile)}, reconnect_policy_{reconnect_policy} {}

TelnetConnection::~TelnetConnection() { close(); }

auto TelnetConnection::start(session::ConnectionEvents &events) -> void {
    if (started_.exchange(true) || closing_.load()) {
        return;
    }
    events_ = &events;
    // 建连会阻塞（DNS + connect 超时），全部推到读线程（口径同 SSH 与串口腿）。
    reader_ = std::thread([this] { run_loop(); });
}

auto TelnetConnection::run_loop() -> void {
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

auto TelnetConnection::open_and_serve() -> Attempt {
    // 每次尝试先复位：解析态、已答记账、状态位、已发尺寸四样一起清零（D4①／D10② 代价 ⓒ），
    // 于是重连后同一台服务器可以改口，尺寸也会重新首发一次。
    negotiation_.reset_for_redial();
    retry_now_.store(false);  // 残留的「立即重试」不该截断这一次的建连。
    auto stream = platform::tcp_connect(profile_.host, profile_.port, kConnectTimeoutMs);
    if (stream == nullptr) {
        AURORA_LOG_WARN("conn", "telnet: dial failed, host=", profile_.host,
                        ":", std::to_string(profile_.port));
        return Attempt{.linked = false, .reason = telnet_close_reason(TelnetLinkEnd::DialFailed)};
    }
    // D6①：逐键往返的终端禁 Nagle；这枚选项只在这条腿呼，隧道腿一字不动。
    if (!stream->set_nodelay()) {
        AURORA_LOG_WARN("conn", "telnet: TCP_NODELAY not applied, host=", profile_.host);
    }
    {
        const std::lock_guard lock{stream_mutex_};
        stream_ = std::move(stream);
    }
    linked_.store(true);
    post_progress(session::ReconnectProgress{});  // 连上了：把快照擦回「不在环里」。

    send_raw(negotiation_.opening_commands());  // 判据 3：`DO ECHO` + `DO SGA`（呼用即记账）。
    pump_window_size();                         // 判据 8：建立后首发一次当前尺寸（槽空则不发）。

    const auto end = read_loop();
    linked_.store(false);
    teardown();  // 本回的流先关掉：下一回打开的是新句柄，退避期间也不能留着半条连接。
    return Attempt{.linked = true, .reason = telnet_close_reason(end)};
}

auto TelnetConnection::read_loop() -> TelnetLinkEnd {
    auto buffer = std::vector<std::uint8_t>(kReadChunkBytes);
    for (;;) {
        if (closing_.load()) {
            return TelnetLinkEnd::LocalClose;
        }
        // 尺寸槽每轮取一次：对端连续推字节时也不把新尺寸压到空闲档之后（判据 8 的「变化即发」）。
        pump_window_size();
        auto outcome = platform::TcpRead{};
        {
            // 锁内只做一次「超时 0 档」的非阻塞探测，空闲等待留在锁外：把 20 ms 的定时读留在
            // `stream_mutex_` 里，UI 线程的每一次 `write` 都要排在一个读分片之后（稿 §5）。
            const std::lock_guard lock{stream_mutex_};
            if (stream_ == nullptr) {
                return TelnetLinkEnd::ReadError;  // 本端已收口：不再触碰 socket。
            }
            outcome = stream_->read({buffer.data(), buffer.size()}, kProbeTimeoutMs);
        }
        switch (outcome.outcome) {
        case platform::TcpReadOutcome::Data:
            // 出锁再投递：喂进协商件的临界区不该把 socket 算进去（架构 §3.4「锁内不做 IO」）。
            serve_chunk({buffer.data(), outcome.bytes});
            continue;  // 线上还有字节就立刻再探，不白等一个空闲片。
        case platform::TcpReadOutcome::Timeout:
            std::this_thread::sleep_for(std::chrono::milliseconds{kReadSliceMs});
            continue;
        case platform::TcpReadOutcome::Closed:
            // 实测 F3：有序 FIN ⇒ recv 返回 0 字节。判据 7 把这一档读成「远端已退出」。
            AURORA_LOG_INFO("conn", "telnet: peer closed orderly, host=", profile_.host);
            return TelnetLinkEnd::RemoteEof;
        case platform::TcpReadOutcome::Error:
            // 实测 F3 的另一态：ECONNRESET(104)。两态读数分明 ⇒ 归因不新增平台档位。
            AURORA_LOG_INFO("conn", "telnet: read side error, host=", profile_.host);
            return TelnetLinkEnd::ReadError;
        }
    }
}

auto TelnetConnection::serve_chunk(std::span<const std::uint8_t> chunk) -> void {
    const auto parsed = negotiation_.on_bytes(chunk);
    if (!parsed.data.empty()) {
        // 净数据：`FF FF` 已还原、命令段与 SB 载荷已被吃掉（判据 5），网格不会看见半个命令。
        events_->on_bytes({reinterpret_cast<const std::byte *>(parsed.data.data()),
                           parsed.data.size()});
    }
    if (!parsed.replies.empty()) {
        send_raw(parsed.replies);  // 应答与子协商都在读线程写出（稿 §5：同一线程的 stream->write）
    }
}

auto TelnetConnection::send_raw(std::span<const std::uint8_t> bytes) -> void {
    if (bytes.empty()) {
        return;
    }
    const std::lock_guard lock{stream_mutex_};
    if (stream_ == nullptr || !stream_->write(bytes)) {
        // 只记日志、不改判归因：写成功不等于对端收到（F4），掉线一律从读侧判。
        AURORA_LOG_WARN("conn", "telnet: write side ended, host=", profile_.host);
    }
}

auto TelnetConnection::pump_window_size() -> void {
    auto pending = std::optional<TelnetConnection::PendingSize>{};
    {
        const std::lock_guard lock{size_mutex_};
        pending = pending_size_;  // latest-value：只取最近一份，不攒历史（AGENTS §4.5 第 25 条）
    }
    if (!pending) {
        return;  // 界面还没给过尺寸：没有可发的读数，也不猜一个 80×24。
    }
    // 该不该发由协商件判（同值去重、越界不发且不记账），这里只负责把产出的字节写上 socket。
    send_raw(negotiation_.window_size_command(pending->rows, pending->cols));
}

auto TelnetConnection::sleep_backoff(int delay_ms) -> bool {
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

auto TelnetConnection::post_progress(const session::ReconnectProgress &progress) -> void {
    if (events_ != nullptr) {
        events_->on_reconnect_progress(progress);
    }
}

auto TelnetConnection::write(std::span<const std::byte> bytes) -> void {
    if (closing_.load() || !linked_.load() || bytes.empty()) {
        return;  // 未连上（含退避期间）：与串口腿同语义，键入丢弃。
    }
    // 写出侧转义（口径 ⑶／F7）：数据里的每个 0xFF 上线前写成 `FF FF`。
    const auto escaped = TelnetNegotiation::escape_outbound(
        {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()});
    send_raw(escaped);
}

auto TelnetConnection::resize(session::Size size) -> void {
    // 只做一件事：把这对整数写进 latest-value 槽。不取 socket 锁、不做 IO、不排队列，
    // 同值直接覆盖（D10② 的代价 ⓑ：拖分屏不得形成协商风暴，去重在协商件那一侧）。
    const std::lock_guard lock{size_mutex_};
    pending_size_ =
        PendingSize{.rows = to_window_value(size.rows), .cols = to_window_value(size.columns)};
}

auto TelnetConnection::close() -> void {
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

auto TelnetConnection::teardown() -> void {
    const std::lock_guard lock{stream_mutex_};
    if (stream_ != nullptr) {
        stream_->close();
        stream_.reset();
    }
    linked_.store(false);
}

auto TelnetConnection::alive() const noexcept -> bool {
    return started_.load() && !closing_.load() && linked_.load();
}

}  // namespace borealis::conn
