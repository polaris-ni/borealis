// ============================================================
// 主线程可见区副本实现（src/session/screen_mirror.cpp）
// ------------------------------------------------------------
// 本文件的全部算术就是行号换算：权威网格的绝对行号 → 屏幕行号，加上「哪些提交在可见窗
// 之外」。把这三段留在纯逻辑层，是为了让回看偏移与脏行并入能在无框架环境里逐条断言。
// ============================================================

#include "borealis/session/screen_mirror.h"

#include <algorithm>
#include <span>

#include "borealis/grid/row.h"

namespace borealis::session {
namespace {

/// @brief 本帧提交里是否含整屏脏（含即说明行号与内容的对应关系整体变了）。
[[nodiscard]] auto has_full_screen(std::span<const Damage> frame) -> bool {
    return std::any_of(frame.begin(), frame.end(), [](const Damage &damage) { return damage.full_screen; });
}

/// @brief 整行取一份副本供绘制：脏标记属于权威网格的重读依据，抄进副本就成了无用状态。
[[nodiscard]] auto take_copy(const grid::Row &source) -> grid::Row {
    auto copy = source;
    copy.clear_dirty();
    return copy;
}

}  // namespace

auto ScreenMirror::apply(grid::Storage &authoritative, std::span<const Damage> frame, std::size_t back_rows) -> void {
    const std::size_t rows = authoritative.visible_rows();
    const std::size_t total = authoritative.total_lines();
    // 顶边位移随本帧一起快照：整窗重建与逐行并入两条路都要带给绘制侧，否则下一次没有基准可比。
    dropped_lines_ = authoritative.dropped_lines();
    // 可回看范围上限 = 视口之上的历史行数；越界按上限截断（滚动状态与网格之间本就容许一帧的偏差）。
    const std::size_t max_back = total > rows ? total - rows : 0U;
    const std::size_t back = std::min(back_rows, max_back);
    const std::size_t window_top = total > rows + back ? total - rows - back : 0U;
    if (rows != lines_.size() || columns_ != authoritative.columns() || window_top != window_top_ ||
        has_full_screen(frame)) {
        rebuild(authoritative, back, window_top);
        return;
    }
    for (const Damage &damage : frame) {
        for (std::size_t viewport_row = damage.first_row; viewport_row <= damage.last_row; ++viewport_row) {
            consume(authoritative, viewport_row);
        }
    }
}

auto ScreenMirror::line(std::size_t row) const noexcept -> const grid::Row & { return lines_[row]; }

auto ScreenMirror::rebuild(grid::Storage &authoritative, std::size_t back_rows, std::size_t window_top) -> void {
    columns_ = authoritative.columns();
    back_rows_ = back_rows;
    window_top_ = window_top;
    const std::size_t rows = authoritative.visible_rows();
    lines_.clear();
    lines_.reserve(rows);
    for (std::size_t index = 0; index < rows; ++index) {
        lines_.push_back(take_copy(authoritative.line(window_top + index)));
    }
    for (std::size_t viewport_row = 0; viewport_row < rows; ++viewport_row) {
        authoritative.visible_line(viewport_row).clear_dirty();
    }
}

auto ScreenMirror::consume(grid::Storage &authoritative, std::size_t viewport_row) -> void {
    const std::size_t screen_row = viewport_row + back_rows_;
    if (screen_row < lines_.size()) {
        lines_[screen_row] = take_copy(authoritative.visible_line(viewport_row));
    }
    // 滚出可见窗的行也要消费：脏标记不清就被 `collect_damage` 反复报成新的提交。
    authoritative.visible_line(viewport_row).clear_dirty();
}

}  // namespace borealis::session
