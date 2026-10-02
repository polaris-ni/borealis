#pragma once

// ============================================================
// 主线程可见区副本（include/borealis/session/screen_mirror.h）
// ------------------------------------------------------------
// 架构 §3.4：权威网格由后台读线程持有，绘制在锁外，故主线程须有自己的可见行副本。
// 本件只做两件事——「按本帧脏行提交并入」与「回看偏移的行号换算」，刻意不含框架类型，
// 于是最容易算错的行号映射能在全量单测里跑（AGENTS.md §4.4 第 20 条）。
//
// 换算只有一个输入：`back_rows`＝**距底行数**（裁决 7.23① 的回看形态，架构 §9.5）。
// 屏幕行 i 取绝对行 `total - rows - back_rows + i`，于是视口行 v 落在屏幕行 `v + back_rows`；
// 落在可见窗之外的提交只消费脏标记不并入，因为画面上没有它。
//
// 脏标记的消费也归本件：权威网格的脏标记是 `Session::collect_damage` 的扫描依据，写侧
// 只登记不消费，主线程取用若不把它清掉，同一脏区就会每帧重复上报。故 `apply` 收**可变**
// 网格引用（原设计稿写的 const 不成立，回写见架构 §9.2 与本文件实现）。
// ============================================================

#include <cstddef>
#include <span>
#include <vector>

#include "borealis/grid/storage.h"
#include "borealis/session/damage_queue.h"

namespace borealis::session {

/// @brief 一帧绘制要看的 rows 行内容：权威网格的按行副本，只由主线程触达。
class ScreenMirror final {
  public:
    ScreenMirror() = default;
    ScreenMirror(const ScreenMirror &) = delete;
    auto operator=(const ScreenMirror &) -> ScreenMirror & = delete;
    ScreenMirror(ScreenMirror &&) = default;
    auto operator=(ScreenMirror &&) -> ScreenMirror & = default;

    /// @brief 并入本帧提交：需要换源就整窗重建，否则只并脏行。
    ///
    /// 整窗重建的三个触发条件（架构 §9.5 的「偏移变化即整屏脏重建」）：网格尺寸变了、
    /// 可见窗的绝对起点变了（回看偏移变化或 D6① 的距底恒定推窗）、本帧含整屏脏提交。
    /// 视口行的脏标记一律在本调用内消费掉，含那些已被滚出可见窗的。
    /// @param authoritative 权威网格，在 `Session::read` 的临界区内传入。
    /// @param frame 本帧取到的脏行提交（`DamageQueue::drain` 的结果）。
    /// @param back_rows 距底行数；超过可回看范围时按可回看范围截断。
    auto apply(grid::Storage &authoritative, std::span<const Damage> frame, std::size_t back_rows) -> void;

    /// @brief 取屏幕行（0 为可见窗顶）。
    /// @param row 屏幕行号，须 < `rows()`。
    /// @return 该行的只读引用。
    [[nodiscard]] auto line(std::size_t row) const noexcept -> const grid::Row &;

    /// @brief 可见窗行数（等于权威网格的视口行数）。
    [[nodiscard]] auto rows() const noexcept -> std::size_t { return lines_.size(); }

    /// @brief 列数（等于权威网格列数）。
    [[nodiscard]] auto columns() const noexcept -> std::size_t { return columns_; }

    /// @brief 上次并入生效的距底行数：绘制侧的光标行换算要用同一个数。
    [[nodiscard]] auto back_rows() const noexcept -> std::size_t { return back_rows_; }

  private:
    /// @brief 按绝对行号整窗重取（尺寸、窗口起点或整屏脏变了就走这条，不做搬移）。
    auto rebuild(grid::Storage &authoritative, std::size_t back_rows, std::size_t window_top) -> void;

    /// @brief 并一个视口行到它所在的屏幕行，并消费该视口行的脏标记。
    auto consume(grid::Storage &authoritative, std::size_t viewport_row) -> void;

    std::vector<grid::Row> lines_;  ///< 可见窗的整行副本，屏幕行序。
    std::size_t columns_ = 0;
    std::size_t back_rows_ = 0;
    std::size_t window_top_ = 0;  ///< 上次重建时可见窗顶的绝对行号；变化即换源。
};

}  // namespace borealis::session
