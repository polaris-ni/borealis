#pragma once

// ============================================================
// 串口连接腿（src/conn/serial_connection.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 的串口传输腿：`session::Connection` 在本地 PTY / SSH 之后的第三个实现。
// 本头是**私有头**（裁决 7.110 D1①，口径同 `conn/ssh_connection.h`）：termios 与 Win32 COMM
// 的知识全在 `platform/serial.h` 那一侧，这里只见抽象类与两个工厂函数（AGENTS §4.5 第 23 条）。
//
// 线程模型沿用 SSH 腿（架构 §3.1）：打开与置参、读循环都在**读线程**上，`start()` 立即返回；
// `close()` 先置终止位再 join，join 上界＝一个读分片或一个退避分片。设备句柄只由读线程建立与
// 销毁（换血在环内、终态在 join 之后），故 `on_bytes` 不会撞上悬垂端口。读循环在 `port_mutex_`
// 内只做**非阻塞探测**，空闲等待留在锁外——键入走的是 UI 线程的 `write`，不该排在一个读分片之后。
//
// 自动重连复用 SSH 腿那一条退避环（`conn/reconnect.h`，裁决 7.99 D1 的「不复制第二份」）。
// 差别全在**归因**：串口没有「远端进程正常结束」这个概念，读侧 EOF 与读错一律落 `LinkLost`
// （裁决 7.110 D7① 第 1 条代价口径），打开失败按 errno 分档映射——那张表就是本头的
// `serial_close_reason()`，纯函数、可无头断言。
//
// `resize()` 是**有意的空操作**：串口是一条裸字节管道，设备侧不重排、也没有窗口尺寸可下发
// （`SPEC.FEAT.XFER.01` 的尺寸同步属 PTY 语义）。本地网格的换行仍由会话层按视口行列做。
// ============================================================

#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <thread>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "conn/reconnect.h"
#include "platform/serial.h"

namespace borealis::conn {

/// @brief 打开/置参失败的归因映射（裁决 7.110 D7① 的那张表，一处一份）。
///
/// 分档判据是「重拨有没有可能把它救回来」：节点暂缺、被占用属**状态类**，落到可重拨的
/// `DialNetwork`；权限不足、节点根本不存在、参数写不下去属**事实类**，重拨只会再撞同一堵墙，
/// 故落 `Unknown`——按裁决 7.99 D2① 的「未知即保守不可重拨」，不给 `EACCES` 无限重拨的机会
/// （D7① 第 2 条代价口径）。
/// @param failure 平台腿给出的归因档。
[[nodiscard]] auto serial_close_reason(platform::SerialFailure failure) -> session::CloseReason;

/// @brief 串口传输实现：平台端口 + 每会话一个读线程 + 复用既有退避环。
class SerialConnection final : public session::Connection, public session::ReconnectControl {
  public:
    /// @param profile 串口档案（设备节点 + 帧格式 + 流控）。**行尾与编码不在本腿**：那是会话侧
    ///                写出前的折算（裁决 7.110 D11①/D10），由装配层交进 `Session` 的构造入参。
    /// @param reconnect_policy 退避档：装配层把重连三键搬进来，省略即 `default_reconnect_policy()`
    ///                （裁决 7.99 D4① 那份，串口与 SSH 同一口径）。
    explicit SerialConnection(SerialProfile profile,
                              RetryPolicy reconnect_policy = default_reconnect_policy());

    SerialConnection(const SerialConnection &) = delete;
    SerialConnection(SerialConnection &&) = delete;
    auto operator=(const SerialConnection &) -> SerialConnection & = delete;
    auto operator=(SerialConnection &&) -> SerialConnection & = delete;

    ~SerialConnection() override;

    auto start(session::ConnectionEvents &events) -> void override;

    auto write(std::span<const std::byte> bytes) -> void override;

    /// @brief 空操作（见文件头：串口没有可下发的窗口尺寸）。
    auto resize(session::Size size) -> void override;

    auto close() -> void override;

    [[nodiscard]] auto alive() const noexcept -> bool override;

    /// @brief 「立即重试」：截断当前退避休眠，下一回拨不等到点（口径同 SSH 腿）。
    auto retry_now() -> void override { retry_now_.store(true); }

    /// @brief 「停止重连」：置终止位让环自己落终态，不硬杀读线程。
    auto stop_reconnect() -> void override { stop_reconnect_.store(true); }

    /// @brief 连接中热改波特率（`SPEC.FEAT.CONN.05` 的「连接中热改」判据，裁决 7.110 D6②）。
    ///
    /// 生效判据是**平台腿内的 setattr 后 getattr 回读比对**：回读不符或系统调用报错一律回 false，
    /// 且「会话当前波特率」保持原档——本函数不会把没落地的档位报成已生效。
    /// @return 是否真的改成了；未连接（含退避期间）恒 false。
    auto set_baud(int baud) -> bool;

    /// @brief 会话**当前**实际跑的波特率：最后一次成功的 `set_baud` 或打开时的档案档。
    ///
    /// 与档案里的 `baud` 是两个量（后者是下次连接的缺省，裁决 7.110 D4① 的代价面），故面板读
    /// 本函数而不是读档案。取用是「加锁 + 返回一个 int」，不含 IO，可在绘制路径每帧调用
    /// （AGENTS §4.5 第 25 条）；读循环在锁内只做非阻塞探测，故这里等到的是一把几乎空闲的锁。
    [[nodiscard]] auto current_baud() const -> int;

    /// @brief 档案里的设备节点名（诊断面板与热改对话框的标题用；不随重拨变化）。
    [[nodiscard]] auto device_path() const -> const std::string & { return profile_.device_path; }

  private:
    /// @brief 一次完整尝试的产出：是否进到过读循环、以及这次结束归到哪条原因（口径同 SSH 腿）。
    struct Attempt {
        bool linked;
        session::CloseReason reason;
    };

    /// @brief 读线程主体：首拨 + 退避环，直到不可重连 / 次数用尽 / 用户停止 / 本端关闭。
    auto run_loop() -> void;

    /// @brief 单次尝试：折参数→`platform::serial_open`→读循环（首拨与重拨共用同一段）。
    ///
    /// 每次尝试都重新折一份 `SerialFrameBits`，故「按档案重开并重置参」是这条路的天然形态
    /// （稿 §6 的断开重连判据）：热改过的波特率不会跟随重拨，回到的是档案档。
    [[nodiscard]] auto open_and_serve() -> Attempt;

    /// @brief 读循环：锁内非阻塞探测 + 锁外空闲等待，直到本端关闭 / 读到 EOF / 读错。
    /// @return 结束归因（LocalClose / LinkLost）。
    [[nodiscard]] auto read_loop() -> session::CloseReason;

    /// @brief 分段退避休眠：每片查 closing_ / stop_reconnect_ / retry_now_。
    [[nodiscard]] auto sleep_backoff(int delay_ms) -> bool;

    /// @brief 投一份进度快照（latest-value，UI 每帧读最近一份）。
    auto post_progress(const session::ReconnectProgress &progress) -> void;

    /// @brief 关掉当前端口（换血与收口用）；只在读线程或 join 之后调用。
    auto teardown() -> void;

    SerialProfile profile_;
    RetryPolicy reconnect_policy_;

    /// 守护 `port_` 的建立/销毁与每次系统调用：读线程的非阻塞探测、UI 线程的 write/set_baud 都要过它。
    /// 读侧在锁内不做定时等待（见 `read_loop`），故锁的常驻持有者只可能是一次 `poll(超时 0)` 或一次
    /// `write`——后者本就是设备侧的事，与本仓既有的 `resize()` ioctl 同档（稿 §5「UI 线程零目录 IO」）。
    mutable std::mutex port_mutex_;

    session::ConnectionEvents *events_ = nullptr;
    std::unique_ptr<platform::SerialPort> port_;
    std::thread reader_;

    std::atomic<bool> closing_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> retry_now_{false};
    std::atomic<bool> stop_reconnect_{false};
    /// 端口是否已进到可读状态（打开成功置位、结束清位）；`alive()` 与 `write()` 免锁早退用。
    std::atomic<bool> port_open_{false};
};

}  // namespace borealis::conn
