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
#include "borealis/term/codec.h"
#include "borealis/term/terminal.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"

namespace borealis::session {

/// @brief 发送侧「不可表示字符」一次性提示的载荷（`SPEC.FEAT.TERM.09` 的提示腿，裁决 7.104）。
///
/// 一份**合成后的值聚合体**而不是事件流水（AGENTS 第 25 条）：跨线程只交换「这一批几个码点、
/// 实际跑的哪条腿」，视图侧拿到就能把两行文案填满。
struct UnrepresentableNotice {
    std::size_t count{};  ///< 上弦那一批被处置掉的码点数（D1：每会话对象只在首次一批上弦）。
    std::string leg;      ///< 实际生效腿的展示名（D6②，`term::resolve_encoding_name` 的折算结果）。
};

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
    /// @param encoding 会话编解码档（`terminal.encoding` 与 `terminal.unrepresentable`，
    ///                 `SPEC.FEAT.TERM.09`）。与 `defaults` 同一条生效口径：**建会话这一刻**取一次，
    ///                 解码器与发送策略都是构造期值；缺省即 UTF-8 + 替换，故既有构造点不改一字。
    Session(std::unique_ptr<Connection> connection, Size size, std::size_t scrollback_limit,
            std::shared_ptr<const term::WidthPolicy> width_policy, term::TerminalDefaults defaults = {},
            term::SessionEncoding encoding = {});

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
    ///
    /// 重拨期间同样回 false：那时远端确实没有活进程，退出角标与关闭前确认照今天的语义走，
    /// 不因「正在自动重连」而撒谎（裁决 7.99 D7③）。重连的可见性只走下面那条快照。
    [[nodiscard]] auto alive() const -> bool;

    /// @brief 最近一份自动重连进度快照（`SPEC.FEAT.WS.05` 判据 2 的取数入口）。
    ///
    /// latest-value：连接腿每档退避投一份，UI 每帧读最近一份，丢帧不影响下一帧的正确性
    /// （AGENTS 第 25 条的「交换合成后的最终值」）。回 `std::nullopt`＝这条腿从不重连
    /// （本地 PTY 腿、或 SSH 腿尚未掉线），视口浮层因此维持裁决 7.86 那一档形态。
    [[nodiscard]] auto reconnect_progress() const -> std::optional<ReconnectProgress>;

    /// @brief 连接腿的重连控制面（「立即重试」「停止重连」两枚动作的落点）。
    ///
    /// 不具该能力的腿回 `nullptr`（架构 §7.2：能力接口只由有的腿实现，会话层与 UI 都不
    /// 认得具体连接类型）。返回值与 `alive()` 同生死，不越过本会话。
    [[nodiscard]] auto reconnect_control() const noexcept -> ReconnectControl *;

    /// @brief 取走本帧要重读的提交，最多到单帧预算条数（`SPEC.NF.PERF.06`）。
    [[nodiscard]] auto drain_damage() -> std::vector<Damage> { return damage_queue_.drain(); }

    /// @brief 是否还有待消费提交（决定要不要再排一帧）。
    [[nodiscard]] auto has_damage() const -> bool { return damage_queue_.pending(); }

    /// @brief 背压水位与合并/让出计数（`SPEC.NF.RELI.01` 调试面板）。
    [[nodiscard]] auto queue_stats() const -> QueueStats { return damage_queue_.stats(); }

    /// @brief 非法字节序列计数（`SPEC.NF.RELI.01`：与背压水位同面板）。
    [[nodiscard]] auto decode_stats() const -> term::DecodeStats;

    /// @brief 发送方向被策略处置掉的码点累计数（`SPEC.FEAT.TERM.09` 判据 3 的「可统计」）。
    ///
    /// 与 `decode_stats` 同一取数节奏：计数在发送调用点推进，主线程只在面板打开那一刻取一次。
    /// 与下面的 `take_unrepresentable_notice()` 是**两条独立账**——本条单调累计（调试面板第四行读
    /// 它），那条一次消费（视口卡片读它），互不影响。
    [[nodiscard]] auto unrepresentable_count() const -> std::size_t;

    /// @brief 取走发送侧一次性提示（裁决 7.104 的 D1①/D5①，帧边界调用）。
    ///
    /// 取走即空，且**每个会话对象只上弦一次**：首次有码点被处置时在锁内上一份 latch，此后再有
    /// 处置只累计数、不再上弦（需求那句「一次性提示」的落点）。会话重启＝新对象，latch 从零开始，
    /// 与「新会话再提示一次」的语义一致。
    /// @return 待提示的那一批（个数 + 实际生效腿名）；无待提示内容时为 `std::nullopt`。
    auto take_unrepresentable_notice() -> std::optional<UnrepresentableNotice>;

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

    /// @brief 重连进度：存 latest-value 快照并叫主线程看一眼（状态变更即唤醒，口径同
    ///        `on_closed()` 那条例外——它没有网格提交，靠的正是同一次唤醒）。
    auto on_reconnect_progress(const ReconnectProgress &progress) -> void override;

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
    /// 构造期一次 `dynamic_cast` 取到的能力接口；与会话同生死，故用裸指针表达「不拥有」。
    ReconnectControl *reconnect_control_ = nullptr;
    /// 重连进度的 latest-value 快照，由 `mutex_` 护住（读线程投、主线程每帧读）。
    std::optional<ReconnectProgress> reconnect_progress_;
    term::Terminal terminal_;
    /// 构造期按编码档取一次的解码器（`SPEC.FEAT.TERM.09`）：会话只认 `SessionDecoder` 接缝，
    /// 不认识 UTF-8 还是 GB18030。`mutex_` 护住——读线程喂入与主线程取统计都走它。
    std::unique_ptr<term::SessionDecoder> decoder_;
    term::SessionEncoding encoding_;  ///< 发送方向的编码名与策略档，与 `decoder_` 同世代。
    /// 构造期折一次的**实际生效腿**名（裁决 7.104 的 D6②）：提示卡片报的就是它，故与 `decoder_`
    /// 走的是同一张别名表、不会与真正跑的解码器分叉。指向 `codec.cpp` 里的表项与字面量，静态
    /// 存储期，不随 `encoding_.name` 生死。
    std::string_view encoding_leg_;
    /// 发送侧被策略处置掉的码点累计数（`encoding_` 的代价账目），随 `flush` 在锁外累加。
    std::size_t unrepresentable_total_ = 0;
    /// 一次性提示的 latch（D1①）：首次有码点被处置时上弦，`take_unrepresentable_notice()` 取走即空。
    /// 与 `unrepresentable_total_` 同由 `mutex_` 护住，二者在同一次锁内一起推进。
    std::optional<UnrepresentableNotice> notice_;
    /// 本会话对象**是否已上过一次弦**（D1 的「一次性」落点）：置起后永不再上，故「取走之后又来了
    /// 一批」不会重弹。不复用 `notice_.has_value()`——取走会把 latch 清空，那判据就退化成「每批次
    /// 都上弦」，而粘贴分块一次就产出若干批。
    bool notice_armed_ = false;
    DamageQueue damage_queue_;
    std::u32string pending_responses_;
    std::u32string decode_buffer_;  ///< 解码产物缓冲：只由读线程触达，留容量免得逐块重新分配。
};

}  // namespace borealis::session
