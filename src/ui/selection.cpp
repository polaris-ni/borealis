// ============================================================
// 选区归一与选中文本（src/ui/selection.cpp）
// ------------------------------------------------------------
// 归一只做坐标算术，取文本只做逐格读，两者都不触框架类型，故本件的每条口径都能进单测。
// ============================================================

#include "borealis/ui/selection.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/term/utf8.h"

namespace borealis::ui {
namespace {

/// @brief tmux 分屏边框所用的「细线」制表符（`CopyOptions::strip_tmux_border_chars`）：
///        U+2500/2502 的横线与竖线，加四角、三叉与十字。
///
/// 只收单线档：tmux 的 pane border 与 status line 一律用这一套，双线档（U+2550 一类）不在其
/// 默认形态里。同一批码点也会出现在正常输出中（表格、进度条），开启后一并被剥是本选项的既有
/// 代价，故它默认关闭、只在用户显式开启的会话上生效（裁决 7.32③）。以码点数组建表而非字面量，
/// 因为字面量要把非 ASCII 字符写进源文件（AGENTS.md §4.4 第 14 条），而这张表就是字符本身。
constexpr std::array<std::uint32_t, 11> kTmuxBorderCodePoints{0x2500U, 0x2502U, 0x250CU, 0x2510U,
                                                              0x2514U, 0x2518U, 0x251CU, 0x2524U,
                                                              0x252CU, 0x2534U, 0x253CU};

[[nodiscard]] auto is_tmux_border(char32_t code_point) noexcept -> bool {
    return std::ranges::find(kTmuxBorderCodePoints, static_cast<std::uint32_t>(code_point)) !=
           kTmuxBorderCodePoints.end();
}

/// @brief 取一行的选中文本；整行只由边框字符与空白构成时返回空（该行连同其换行一起丢弃）。
[[nodiscard]] auto row_text(const grid::Row &row, RowSpan span, const CopyOptions &options)
    -> std::optional<std::string> {
    const auto columns = row.columns();
    // 左界吸附：延续格自己没有字符，纳入其基础格才不会把双宽字符切成半个（裁决 7.32②）。
    while (span.first_column > 0U && span.first_column < columns &&
           row.cell(span.first_column).is_wide_continuation()) {
        --span.first_column;
    }
    span.last_column = std::min(span.last_column, columns);

    std::string text;
    bool had_border = false;
    bool has_content = false;  ///< 边框与空白之外还有码点
    for (std::size_t column = span.first_column; column < span.last_column; ++column) {
        const auto &cell = row.cell(column);
        if (cell.is_wide_continuation()) {
            continue;  // 字符已由基础格那一次 append_utf8 写出
        }
        if (options.strip_tmux_border_chars && is_tmux_border(cell.code_point)) {
            had_border = true;
            continue;
        }
        has_content = has_content || cell.code_point != U' ';
        static_cast<void>(term::append_utf8(cell.code_point, text));
        for (const auto &mark : row.combining(column)) {
            static_cast<void>(term::append_utf8(mark.code_point, text));
        }
    }
    if (had_border && !has_content) {
        return std::nullopt;
    }
    return text;
}

/// @brief 剥离行尾空白：只认 U+0020——制表位在网格里已展开成空格，而 UTF-8 续字节恒 ≥ 0x80，
///        故末字节的比较不会截断多字节序列。全角空格（U+3000）是**内容**不是填充，不剥。
auto trim_trailing_space(std::string &line) noexcept -> void {
    while (!line.empty() && line.back() == ' ') {
        line.pop_back();
    }
}

/// @brief 行尾连续反斜杠的个数（`smart_line_join` 的判据）。
[[nodiscard]] auto trailing_backslashes(const std::string &line) noexcept -> std::size_t {
    const auto at = line.find_last_not_of('\\');
    return at == std::string::npos ? line.size() : line.size() - at - 1U;
}

/// @brief 该行是否以「续行反斜杠」结尾：奇数个才算。偶数个（`foo\\`）是转义出来的字面反斜杠，
///        合并它就把一条完整命令切开了。
[[nodiscard]] auto continues_line(const std::string &line) noexcept -> bool {
    return (trailing_backslashes(line) % 2U) == 1U;
}

}  // namespace

auto row_spans(const Selection &selection, const std::size_t columns) -> std::vector<RowSpan> {
    std::vector<RowSpan> spans;
    if (columns == 0U || selection.anchor == selection.focus) {
        return spans;  // 单击不成选区：拖出的第一个像素才算选中一格
    }
    const auto clamp = [columns](std::size_t column) noexcept { return std::min(column, columns - 1U); };

    if (selection.shape == SelectionShape::Block) {
        const auto first = std::min(selection.anchor.row, selection.focus.row);
        const auto last = std::max(selection.anchor.row, selection.focus.row);
        const auto left = clamp(std::min(selection.anchor.column, selection.focus.column));
        const auto right = clamp(std::max(selection.anchor.column, selection.focus.column));
        for (std::size_t row = first; row <= last; ++row) {
            spans.push_back(RowSpan{row, left, right + 1U});
        }
        return spans;
    }

    // 流式按 (行, 列) 字典序定首尾：同一行内两个端点谁先谁后由列号决定，与拖拽方向无关。
    const auto backward = selection.focus.row < selection.anchor.row ||
                          (selection.focus.row == selection.anchor.row &&
                           selection.focus.column < selection.anchor.column);
    const auto &lo = backward ? selection.focus : selection.anchor;
    const auto &hi = backward ? selection.anchor : selection.focus;
    for (std::size_t row = lo.row; row <= hi.row; ++row) {
        const auto first = row == lo.row ? clamp(lo.column) : 0U;
        const auto last = row == hi.row ? clamp(hi.column) : columns - 1U;
        spans.push_back(RowSpan{row, first, last + 1U});
    }
    return spans;
}

auto copy_text(const grid::Storage &storage, const Selection &selection, const CopyOptions &options)
    -> std::string {
    std::vector<std::string> lines;
    for (const auto &span : row_spans(selection, storage.columns())) {
        if (span.row >= storage.total_lines()) {
            continue;  // 历史已溢出：这一号行不再是选区当初指着的内容，也不补空行
        }
        auto line = row_text(storage.line(span.row), span, options);
        if (!line.has_value()) {
            continue;
        }
        if (options.trim_trailing_space) {
            trim_trailing_space(*line);
        }
        if (options.smart_line_join && !lines.empty() && continues_line(lines.back())) {
            lines.back().pop_back();  // 去掉续行反斜杠，直接接上下行，两行之间不留换行
            lines.back().append(*line);
            continue;
        }
        lines.push_back(std::move(*line));
    }

    std::string text;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0U) {
            text.push_back('\n');
        }
        text.append(lines[i]);
    }
    return text;
}

}  // namespace borealis::ui
