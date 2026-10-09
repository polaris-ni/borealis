#pragma once

// ============================================================
// 会话（include/borealis/session/session.h）
// ------------------------------------------------------------
// 架构 §3.1/§3.3：一个会话 = 一个连接 + 一台终端状态机 + 一条背压队列。
// 读线程把字节喂进「解码 → 解析 → 状态机 → 网格」这条链，产出**脏行提交**交给队列；
// 主线程按帧取走提交、在短临界区内把权威网格的最新值并进本地副本（架构 §3.4）。
//
// 一把 `mutex_` 护住状态机与解码器：临界区窗口正比于本次输入的字节量，锁内**不做 IO**
// ——查询应答（DSR/DA1）先在锁内登记，出锁后才写连接；`OSC 52` 的剪贴板写同构，锁内只留存、
// 由主线程取走后落地。写队列同样在锁外：主线程的取用顺序
// 是「先取队列、再读网格」，反过来就会与读线程构成 ABBA 死锁。
//
// 主线程不会自己想起要排帧：提交入队后由装配层注入的帧唤醒句柄叫它一次（架构 §3.2），
// 该句柄同样在锁外调用——它是跨线程 post，属 IO。
// ============================================================

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/session/connection.h"
#include "borealis/session/damage_queue.h"
#include "borealis/term/terminal.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"

namespace borealis::session {

/// @brief 一个终端会话：连接字节流与权威网格之间的全部跨线程协调点。
///
/// 自身不加线程：读线程由连接实现持有（架构 §3.1「每会话一个读线程」），本对象只保证
/// 从任意线程按接口调用都安全。
class Session final : public ConnectionEvents, public term::ResponseSink {
  public:
    /// @brief 组装一个会话。
    /// @param connection 传输连接，所有权交给会话；构造即可用，尚未启动。
    /// @param size 初始视口尺寸：网格据此建立。连接侧的初始尺寸在创建连接时给出（见
    ///             `conn::make_local_terminal_connection`），两处必须由同一次布局决策派生
    ///             （`SPEC.FEAT.XFER.01` 的会话启动腿）。
    /// @param scrollback_limit 主屏 scrollback 容量（`SPEC.FEAT.TERM.04`）。
    /// @param width_policy 宽度判定接缝：本会话（及其内部终端）持有其 shared_ptr 副本，调用方无需再
    ///                     担保其生命周期（架构 §6.3）；运行期改配置不重放既有会话（判据文 §0 边界②）。
    /// @param defaults 状态机的初始档（`appearance.cursor_shape` / `cursor_blinking` 与
    ///                 `terminal.ambiguous_width` 三条）。缺省即库的缺省档，故既有构造点不改一字；
    ///                 取用时机是**建会话这一刻**，运行期改配置不重放既有会话（判据文 §0 边界②）。
    Session(std::unique_ptr<Connection> connection, Size size, std::size_t scrollback_limit,
            std::shared_ptr<const term::WidthPolicy> width_policy, term::TerminalDefaults defaults = {});

    Session(const Session &) = delete;
    auto operator=(const Session &) -> Session & = delete;
    Session(Session &&) = delete;
    auto operator=(Session &&) -> Session & = delete;

    /// @brief 析构即断开连接：读线程回调持有本对象指针，不得比它活得更久。
    ~Session() override;

    /// @brief 启动连接并开始接收（`SPEC.FEAT.CONN.01` 的打开标签路径）。
    auto start() -> void;

    /// @brief 主动结束连接，之后不再接收回调（关闭标签、退出会话）。
    auto close() -> void;

    /// @brief 注入帧唤醒句柄：本次确有网格提交入队后、在锁外调用一次（架构 §3.2）。
    ///
    /// 读线程由连接回调驱动，而排帧在主线程，故「唤醒主线程」这个跨线程动作必须由装配层交进来
    /// ——它调的是什么（`Surface::request_wake` 一类）本层不知道，也不该知道。一轮批量输入只唤醒
    /// 一次，不按提交条数唤醒；一轮没有产出任何提交就不唤醒。
    /// @param wake 唤醒动作，须线程安全且不得阻塞（它在读线程上被调用）。
    auto set_frame_wake(std::function<void()> wake) -> void;

    /// @brief 文本发送方向：按会话编码成字节后写连接（`SPEC.FEAT.TERM.09`）。
    ///
    /// 键入、输入法 commit（`SPEC.FEAT.INTERACT.06`）、粘贴（`SPEC.FEAT.INTERACT.03`）
    /// 与快捷片段都走这里，不得绕过去直接发字节。
    /// @param text 码点流。
    auto send_text(std::u32string_view text) -> void;

    /// @brief 原样发送字节（键映射生成的 CSI/SS3 转义序列一类，已过编码环节）。
    /// @param bytes 待发送字节。
    auto send_bytes(std::span<const std::byte> bytes) -> void;

    /// @brief 变更视口尺寸：状态机与连接都要收到，并以整屏脏通知副本重建（裁决 7.17）。
    /// @param size 新尺寸；行列任一为 0 时忽略（与 `Terminal::resize` 同口径）。
    auto resize(Size size) -> void;

    /// @brief 对端进程是否仍在（`SPEC.FEAT.WS.01` 关闭前确认；退出后内容仍保留供回看）。
    [[nodiscard]] auto alive() const -> bool;

    /// @brief 取走本帧要重读的提交，最多到单帧预算条数（`SPEC.NF.PERF.06`）。
    [[nodiscard]] auto drain_damage() -> std::vector<Damage> { return damage_queue_.drain(); }

    /// @brief 是否还有待消费提交（决定要不要再排一帧）。
    [[nodiscard]] auto has_damage() const -> bool { return damage_queue_.pending(); }

    /// @brief 背压水位与合并/让出计数（`SPEC.NF.RELI.01` 调试面板）。
    [[nodiscard]] auto queue_stats() const -> QueueStats { return damage_queue_.stats(); }

    /// @brief 非法字节序列计数（`SPEC.NF.RELI.01`：与背压水位同面板）。
    [[nodiscard]] auto decode_stats() const -> term::DecodeStats;

    /// @brief VT 解析器未知序列计数（`SPEC.NF.RELI.01` 调试面板）。
    ///
    /// 与 `decode_stats` 同走一次短临界区：计数在读线程喂入时推进，主线程只在面板打开那一刻取。
    [[nodiscard]] auto parse_stats() const -> vt::ParseStats;

    /// @brief OSC 消费留下的状态快照：标题、工作目录、命令块边界（`SPEC.FEAT.TERM.07`）。
    ///
    /// 标签名与窗口标题的优先级（OSC 标题覆盖标签名、手动重命名优先）归工作区层判定，会话只给来源。
    [[nodiscard]] auto osc_state() const -> term::OscState;

    /// @brief 解析网格格子上挂的超链接标识（`SPEC.FEAT.TERM.07` 的 OSC 8 腿）。
    /// @param link_id 标识，取自 `grid::Row::hyperlink`。
    /// @return URI 原文；已被淘汰或无链接时为 `std::nullopt`（按「不可点」处理）。
    [[nodiscard]] auto hyperlink_target(grid::HyperlinkId link_id) const
        -> std::optional<std::u32string>;

    /// @brief 取走待写入系统剪贴板的文本（`OSC 52` 写方向，`SPEC.FEAT.CONN.12` 默认允许档）。
    ///
    /// 取走语义与查询应答同构：状态机在锁内只留存，剪贴板是主线程的 IO，由 `ClipboardOutbox`
    /// 在帧边界取走并落地（架构 §3.2、§3.4）。
    auto take_clipboard_write() -> std::optional<std::u32string>;

    /// @brief 在短临界区内读权威网格、光标与模式（架构 §3.4：锁内只取值，不绘制）。
    ///
    /// 网格以**可变**引用交出：取用方（主线程）按本帧提交把脏列区间 `[dirty_left, dirty_right)`
    /// 的值并入本地副本，随后 `clear_dirty()` 消费该行脏标记——写侧只登记不消费，否则读线程
    /// 先把标记清掉，主线程就来不及取到列级增量（架构 §4.6 的行内左右界）。
    /// @tparam Reader 可调用体，形如 `void (grid::Storage &, term::Cursor, const term::TermModes &)`。
    /// @param reader 读取体，在本调用返回前完成取用。
    template <typename Reader>
    auto read(Reader &&reader) -> void {
        const std::lock_guard lock{mutex_};
        reader(terminal_.active_grid(), terminal_.cursor(), terminal_.modes());
    }

    /// @brief 状态机接缝：查询应答在这里登记，出锁后才写连接。
    ///
    /// 只在状态机 `feed` / `resize` 期间被回调，即**已持 `mutex_`** 的上下文；直接调用会破坏
    /// 应答缓冲的加锁前提。
    /// @param response 应答文本，仅在本调用期间有效。
    auto on_response(std::u32string_view response) -> void override;

    /// @brief 取走并重置 BEL 触发标记（帧边界调用）。
    ///
    /// 取走语义与剪贴板同构：状态机在锁内只留存，UI 是主线程的 IO，由装配层在帧边界取走并落地
    /// （架构 §3.2、§3.4）。
    /// @return 自上次调用以来是否触发过 BEL。
    auto take_bell_triggered() -> bool;

  private:
    /// @brief 连接读线程回调：解码 → 喂状态机 → 产出脏行提交。
    auto on_bytes(std::span<const std::byte> bytes) -> void override;

    /// @brief 对端退出：把解码器挂起的半截序列收尾进网格，之后不再产内容。
    auto on_closed() -> void override;

    /// @brief 走一轮「解码 → 状态机 → 提交交付」；两个回调的差别只在解码收尾。
    /// @param bytes 本次收到的原始字节；@c end_of_stream 为真时可空。
    /// @param end_of_stream 流是否已结束：真则按替换字符收尾挂起序列（`SPEC.FEAT.TERM.09`）。
    auto ingest(std::span<const std::byte> bytes, bool end_of_stream) -> void;

    /// @brief 扫描视口行脏标记并归合成提交（脏标记由主线程取用时清除，写侧只登记不消费）。
    [[nodiscard]] auto collect_damage() -> std::vector<Damage>;

    /// @brief 把码点流按会话编码成字节交给连接（调用方不得持锁：临界区内不做 IO）。
    auto flush(std::u32string_view text) -> void;

    /// @brief 唤醒主线程排帧：句柄在锁内取出、锁外调用（架构 §3.4 的「锁内不做 IO」）。
    auto wake_frame() -> void;

    mutable std::mutex mutex_;  ///< const 取值路径（`decode_stats`）也要加锁，故可变。
    std::function<void()> frame_wake_;  ///< 装配层注入的帧唤醒句柄，与 `mutex_` 同世代护住。
    std::unique_ptr<Connection> connection_;
    term::Terminal terminal_;
    term::Utf8Decoder decoder_;
    DamageQueue damage_queue_;
    std::u32string pending_responses_;
    std::u32string decode_buffer_;  ///< 解码产物缓冲：只由读线程触达，留容量免得逐块重新分配。
};

}  // namespace borealis::session
