#pragma once

// ============================================================
// 会话（include/borealis/session/session.h）
// ------------------------------------------------------------
// 架构 §3.1/§3.3：一个会话 = 一个连接 + 一台终端状态机 + 一条背压队列。
// 读线程把字节喂进「解码 → 解析 → 状态机 → 网格」这条链，产出**脏行提交**交给队列；
// 主线程按帧取走提交、在短临界区内把权威网格的最新值并进本地副本（架构 §3.4）。
//
// 一把 `mutex_` 护住状态机与解码器：临界区窗口正比于本次输入的字节量，锁内**不做 IO**
// ——查询应答（DSR/DA1）先在锁内登记，出锁后才写连接。写队列同样在锁外：主线程的取用顺序
// 是「先取队列、再读网格」，反过来就会与读线程构成 ABBA 死锁。
// ============================================================

#include <cstddef>
#include <memory>
#include <mutex>
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
    /// @param size 初始视口尺寸（会话启动与分屏挂载时各下发一次，`SPEC.FEAT.XFER.01`）。
    /// @param scrollback_limit 主屏 scrollback 容量（`SPEC.FEAT.TERM.04`）。
    /// @param width_policy 宽度判定接缝，生命周期须不短于本会话（架构 §6.3）。
    Session(std::unique_ptr<Connection> connection, Size size, std::size_t scrollback_limit,
            const term::WidthPolicy &width_policy);

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

    mutable std::mutex mutex_;  ///< const 取值路径（`decode_stats`）也要加锁，故可变。
    std::unique_ptr<Connection> connection_;
    term::Terminal terminal_;
    term::Utf8Decoder decoder_;
    DamageQueue damage_queue_;
    std::u32string pending_responses_;
    std::u32string decode_buffer_;  ///< 解码产物缓冲：只由读线程触达，留容量免得逐块重新分配。
};

}  // namespace borealis::session
