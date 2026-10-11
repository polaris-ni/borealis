#pragma once

// ============================================================
// Telnet 连接腿（src/conn/telnet_connection.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.06` 的传输腿：`session::Connection` 的第四个实现（本地 PTY / SSH /
// 串口之后）。本头是**私有头**（裁决 7.112 D11①，口径同 `conn/serial_connection.h`）：
// 裸 socket 的知识全在 `platform/tcp.h` 那一侧，这里只见抽象类与两个工厂函数
// （AGENTS §4.5 第 23 条）；IAC 的规程知识全在 `conn/telnet_negotiation` 那一侧，
// 这里只负责「什么时候把字节交给它、把它产出的字节写上 socket」（D11① 的分界）。
//
// 线程模型沿用串口腿（稿 §5，架构 §3.1）：建连与读循环都在**读线程**上，`start()`
// 立即返回；`close()` 先置终止位再 join，join 上界＝一个读分片或一个退避分片。socket
// 只由读线程建立与销毁（换血在环内、终态在 join 之后），故 `on_bytes` 不会撞上悬垂流。
// 读循环在 `stream_mutex_` 内只做**超时 0 档的非阻塞探测**，空闲等待留在锁外——键入走
// 的是 UI 线程的 `write`，不该排在一个读分片之后（稿 §5 的锁持有面判据）。
//
// 三条本腿独有的口径：
// ⑴ **协商状态只在读线程**：`negotiation_` 不加锁也不跨线程读，重拨前
//    `reset_for_redial()` 把解析态、已答记账、状态位、已发尺寸四样一起清零
//    （裁决 7.112 D4① 与 7.113③ 的落地处）。
// ⑵ **跨线程的唯一入口是尺寸槽**（D10②）：`resize()` 由 UI 线程呼，只把两个整数写进
//    latest-value 槽——不取 socket 锁、不做 IO、不排队列；组包与上线由读线程在下一轮
//    完成，同值去重由协商件判（比对源是「上一次真的发出去的」）。
// ⑶ **写出侧转义在本腿**：数据里的 0xFF 上线前写成 `FF FF`（F7）。发生在这一层而不是
//    `Session`：SSH／本地腿没有这层规程，把协议细节渗进会话层就违背分层。
//
// 归因与串口腿**故意分野**（裁决 7.112 D9①）：Telnet 的读侧 EOF 落 `RemoteExit` 且
// **不**重拨——telnetd 在远端进程 `exit` 后关 TCP 是正常结束，重拨会变成「登录退出后
// 无限重拨」；串口那棒把 EOF 落 `LinkLost` 是因为串口没有「远端进程」概念。分野是协议
// 语义，不是两处实现不一致。
// ============================================================

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "conn/reconnect.h"
#include "conn/telnet_negotiation.h"
#include "platform/tcp.h"

namespace borealis::conn {

/// @brief 一次尝试结束的那一档事实（建连失败与读侧两种结束分开给，归因才只有一处）。
enum class TelnetLinkEnd : std::uint8_t {
    DialFailed,   ///< 建连阶段失败：拒连、超时、解析不出地址。
    RemoteEof,    ///< 读侧收到有序 FIN（`recv` 返回 0 字节，实测 F3）。
    ReadError,    ///< 读侧 socket 错误（ECONNRESET 等，实测 F3 的另一态）。
    LocalClose,   ///< 本端 `close()` 或「停止重连」收的口。
};

/// @brief 结束档 → 关闭归因（判据 7 的那张表，一处一份，纯函数可无头断言）。
///
/// 三档落点各不相同，且每一档都对应一条**重拨语义**：`DialFailed` 落 `DialNetwork`
/// （可重拨——端口下一回可能就开了），`RemoteEof` 落 `RemoteExit`（按裁决 7.99 D2①
/// 的分类表**不**重拨），`ReadError` 落 `LinkLost`（重拨）。串口腿那条「EOF 也重拨」
/// 的口径在这里不适用，理由见文件头第 ⑶ 条的分野。
[[nodiscard]] auto telnet_close_reason(TelnetLinkEnd end) -> session::CloseReason;

/// @brief Telnet 传输实现：一条 TCP 流 + 每会话一个读线程 + 协商件 + 复用既有退避环。
class TelnetConnection final : public session::Connection, public session::ReconnectControl {
  public:
    /// @param profile Telnet 档案（主机 + 端口）。**编码与发送行尾不在本腿**：那是会话侧
    ///                写出前的折算（裁决 7.112 D5①／D8①），由装配层交进 `Session` 的构造入参。
    /// @param reconnect_policy 退避档：装配层把重连三键搬进来，省略即 `default_reconnect_policy()`
    ///                （口径同 SSH 与串口两腿，裁决 7.99 D4①）。
    explicit TelnetConnection(TelnetProfile profile,
                              RetryPolicy reconnect_policy = default_reconnect_policy());

    TelnetConnection(const TelnetConnection &) = delete;
    TelnetConnection(TelnetConnection &&) = delete;
    auto operator=(const TelnetConnection &) -> TelnetConnection & = delete;
    auto operator=(TelnetConnection &&) -> TelnetConnection & = delete;

    ~TelnetConnection() override;

    auto start(session::ConnectionEvents &events) -> void override;

    /// @brief 写出：先过 `escape_outbound`（口径 ⑶），再整串交给 socket；未连上即丢弃。
    auto write(std::span<const std::byte> bytes) -> void override;

    /// @brief 把尺寸写进 latest-value 槽（口径 ⑵）：UI 线程零 IO、零 socket 锁。
    auto resize(session::Size size) -> void override;

    auto close() -> void override;

    [[nodiscard]] auto alive() const noexcept -> bool override;

    /// @brief 「立即重试」：截断当前退避休眠，下一回拨不等到点（口径同 SSH 与串口腿）。
    auto retry_now() -> void override { retry_now_.store(true); }

    /// @brief 「停止重连」：置终止位让环自己落终态，不硬杀读线程。
    auto stop_reconnect() -> void override { stop_reconnect_.store(true); }

    /// @brief 档案里的主机名（诊断与日志用；不随重拨变化）。
    [[nodiscard]] auto host() const -> const std::string & { return profile_.host; }

    /// @brief 档案里的端口。
    [[nodiscard]] auto port() const -> int { return profile_.port; }

  private:
    /// @brief 一次完整尝试的产出：是否进到过读循环、以及这次结束归到哪条原因（口径同串口腿）。
    struct Attempt {
        bool linked;
        session::CloseReason reason;
    };

    /// @brief 尺寸槽里的一对整数（`resize()` 的产出，读线程每轮取一次）。
    struct PendingSize {
        int rows{0};
        int cols{0};
    };

    /// @brief 读线程主体：首拨 + 退避环，直到不可重连 / 次数用尽 / 用户停止 / 本端关闭。
    auto run_loop() -> void;

    /// @brief 单次尝试：`tcp_connect`→`set_nodelay`→发起序→读循环（首拨与重拨共用同一段）。
    ///
    /// 每次尝试都先 `reset_for_redial()`，故「已答」记账与「已发尺寸」记忆不会跟着重拨走
    /// （D4①／D10② 的代价 ⓒ）：重连后同一台服务器可以改口，尺寸也会重新首发一次。
    /// 读侧 EOF 归 `RemoteExit` 而不重拨，理由见文件头末段的分野。
    [[nodiscard]] auto open_and_serve() -> Attempt;

    /// @brief 读循环：锁内超时 0 档探测 + 锁外空闲等待，直到本端关闭 / EOF / 读错。
    [[nodiscard]] auto read_loop() -> TelnetLinkEnd;

    /// @brief 把一段收到的字节交给协商件，净数据投会话、待回字节写回 socket（都在读线程）。
    ///
    /// 写出失败只记日志并继续读：掉线一律从读侧归因（实测 F4——半关时写侧仍连续成功）。
    auto serve_chunk(std::span<const std::uint8_t> chunk) -> void;

    /// @brief 原样写出一段协议字节（发起序、应答、NAWS 组包产物——它们已过 IAC 成帧，不再转义）。
    auto send_raw(std::span<const std::uint8_t> bytes) -> void;

    /// @brief 取尺寸槽并组包：该发才发（同值去重与越界不发都由协商件判，口径 ⑵）。
    auto pump_window_size() -> void;

    /// @brief 分段退避休眠：每片查 closing_ / stop_reconnect_ / retry_now_。
    [[nodiscard]] auto sleep_backoff(int delay_ms) -> bool;

    /// @brief 投一份进度快照（latest-value，UI 每帧读最近一份）。
    auto post_progress(const session::ReconnectProgress &progress) -> void;

    /// @brief 关掉当前流（换血与收口用）；只在读线程或 join 之后调用。
    auto teardown() -> void;

    TelnetProfile profile_;
    RetryPolicy reconnect_policy_;

    /// 守护 `stream_` 的建立/销毁与每次读写：读线程的非阻塞探测与 UI 线程的 `write` 都要过它。
    /// 读侧在锁内不做定时等待（见 `read_loop`），故锁的常驻持有者只可能是一次超时 0 的探测或
    /// 一次写（AGENTS §4.5 第 25 条与架构 §3.4「锁内不做 IO」）。
    mutable std::mutex stream_mutex_;

    session::ConnectionEvents *events_ = nullptr;
    std::unique_ptr<platform::TcpStream> stream_;
    TelnetNegotiation negotiation_;  ///< 只在读线程触碰（口径 ⑴），故不加锁。
    std::thread reader_;

    std::atomic<bool> closing_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> retry_now_{false};
    std::atomic<bool> stop_reconnect_{false};
    /// 流是否已进到可读状态（建连成功置位、结束清位）；`alive()` 与 `write()` 免锁早退用。
    std::atomic<bool> linked_{false};

    /// 尺寸槽自己的小锁：与 socket 无关，故 `resize()` 不会排在任何一次读写之后。
    std::mutex size_mutex_;
    std::optional<PendingSize> pending_size_{};
};

}  // namespace borealis::conn
