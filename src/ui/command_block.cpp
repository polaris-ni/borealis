// ============================================================
// 命令块的界面折算实现（src/ui/command_block.cpp）
// ------------------------------------------------------------
// 全部是行号算术：绝对行减顶边位移得存储行，存储行减窗顶得屏幕行。没有第二份状态，
// 故每帧、每次右键都现算，不在块表或控件里缓存任何一档行号。
// ============================================================

#include "borealis/ui/command_block.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace borealis::ui {

auto command_output_spans(const term::CommandBlock &block, const std::int64_t dropped_lines,
                          const std::size_t total_lines, const std::size_t columns) -> std::vector<RowSpan> {
    std::vector<RowSpan> spans;
    if (!block.has_copyable_output() || columns == 0U || total_lines == 0U) {
        return spans;
    }
    const std::int64_t first_abs = block.output_start_row;
    // D 在列 0：那一行是下一条命令的提示符（裁决 7.123 D4①）；列 > 0 时该行前半段是输出。
    const std::int64_t last_abs =
        block.output_end_column == 0 ? block.output_end_row - 1 : block.output_end_row;
    if (last_abs < first_abs) {
        return spans;
    }
    for (std::int64_t absolute = first_abs; absolute <= last_abs; ++absolute) {
        const std::int64_t storage_row = absolute - dropped_lines;
        if (storage_row < 0) {
            continue;  // 这一行已被挤出存储顶端：跳过的只是它，后面的行可能还在
        }
        if (static_cast<std::size_t>(storage_row) >= total_lines) {
            break;  // 行号已越过存储末尾：后续只会更大
        }
        const std::size_t first_column =
            absolute == first_abs ? std::min(block.output_start_column, columns) : 0U;
        const std::size_t last_column =
            absolute == last_abs && block.output_end_column != 0
                ? std::min(block.output_end_column, columns)
                : columns;
        if (first_column >= last_column) {
            continue;  // 半行起点不早于终点：这一行不产文本
        }
        spans.push_back(RowSpan{.row = static_cast<std::size_t>(storage_row),
                                .first_column = first_column,
                                .last_column = last_column});
    }
    return spans;
}

auto failed_output_spans(const term::CommandBlock &block, const std::int64_t dropped_lines,
                         const std::size_t window_top, const std::size_t visible_rows)
    -> std::vector<BlockRowRange> {
    if (!block.failed() || !block.has_output_start || visible_rows == 0U) {
        return {};
    }
    const std::int64_t first_abs = block.output_start_row;
    const std::int64_t last_abs =
        block.output_end_column == 0 ? block.output_end_row - 1 : block.output_end_row;
    if (last_abs < first_abs) {
        return {};
    }
    // 提示符与命令行回显不在 C → D 之间，故这里天然不铺它们。
    const std::int64_t first_storage = std::max<std::int64_t>(first_abs - dropped_lines, 0);
    const std::int64_t last_exclusive = last_abs - dropped_lines + 1;
    if (last_exclusive <= 0) {
        return {};
    }
    const auto window_end = window_top + visible_rows;
    const auto visible_first = std::max(static_cast<std::size_t>(first_storage), window_top);
    const auto visible_last =
        std::min(static_cast<std::size_t>(last_exclusive), window_end);
    if (visible_first >= visible_last) {
        return {};
    }
    return {BlockRowRange{visible_first - window_top, visible_last - window_top}};
}

auto copy_command_output(const grid::Storage &storage, const term::CommandBlock &block,
                         const std::int64_t dropped_lines, const CopyOptions &options) -> std::string {
    const auto spans = command_output_spans(block, dropped_lines, storage.total_lines(), storage.columns());
    return copy_spans(storage, spans, options, /*trim_grid_fill=*/true);
}

}  // namespace borealis::ui
