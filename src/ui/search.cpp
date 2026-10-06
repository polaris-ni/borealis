// ============================================================
// 终端内搜索的匹配表（src/ui/search.cpp）
// ------------------------------------------------------------
// 逐行读格子的算术，不触框架类型，故本件的每条口径都能进单测（AGENTS.md §4.4 第 20 条）。
//
// 三处决定形态的口径（细则见裁决 7.78）：
// - **匹配文本与复制腿同源**：双宽字符只在基础格出现一次，延续格不进匹配文本；行尾连续空格是
//   填充不是内容。于是产出的区间天生整格对齐，高亮不会把一个字切成半格。
// - **两档分途**：字面量档在码点空间直扫、零分配；正则档经 UTF-16 缓冲（本工具链的 `std::regex`
//   只为 `char` 与 `wchar_t` 特化了 `regex_traits`，`basic_regex<char32_t>` 是未定义类型）。
//   实测两者在同一份 100,000 行网格上差两个数量级，故 200 ms 那条判据只钉字面量档。
// - **上限即止损**：匹配表有上界，到顶停止扫描并置 `truncated`——会话字节流是不可信输入，
//   一个宽匹配式能让这张表无限膨胀，而时间门禁同时被破。
// ============================================================

#include "borealis/ui/search.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "borealis/ui/selection.h"

namespace borealis::ui {
namespace {

/// @brief 大小写折叠：只折 ASCII 字母。
///
/// 实测（裁决 7.78②）本工具链 `std::wregex` 带 `icase` 也不折非 ASCII（`Ä` 与 `ä` 不匹配），
/// 故字面量档跟着做同一档，让「同一份查询只换大小写开关」在两档给出同一批匹配，而不是一档
/// 比另一档更强。
[[nodiscard]] auto fold_ascii(char32_t code_point) noexcept -> char32_t {
    return (code_point >= U'A' && code_point <= U'Z') ? code_point + (U'a' - U'A') : code_point;
}

[[nodiscard]] auto code_point_equal(char32_t cell, char32_t needle, bool case_sensitive) noexcept -> bool {
    if (case_sensitive) {
        return cell == needle;
    }
    return fold_ascii(cell) == fold_ascii(needle);
}

/// @brief 一行参与匹配的右界：行尾的连续空格填充不算内容。
///
/// 只认 `U+0020`，与 `copy_text` 的剥空白同口径（全角空格 `U+3000` 是内容）。双宽字符的延续格
/// 在生产形态里码点是 `0` 而不是空格，故行末那个汉字不会被误当填充剥掉而少算一格。
/// 不剥的后果是实打实的：网格每行都是定宽，未写入的列全是空格，` *` 这类式子会在每条输出上
/// 爆出一串无意义的高亮，「匹配计数」就没有意义了。
[[nodiscard]] auto content_right(const grid::Row &row) noexcept -> std::size_t {
    std::size_t right = row.columns();
    while (right > 0U && row.cell(right - 1U).code_point == U' ') {
        --right;
    }
    return right;
}

/// @brief 把匹配右端从最后一个基础格扩展到整字符之后（延续格一并算进这一段）。
[[nodiscard]] auto span_right(const grid::Row &row, std::size_t last_base_column) noexcept -> std::size_t {
    std::size_t right = last_base_column + 1U;
    while (right < row.columns() && row.cell(right).is_wide_continuation()) {
        ++right;
    }
    return right;
}

/// @brief 字面量档的逐行扫描：在格子的码点空间直接比，不建任何中间文本。
///
/// 起点逐个基础格试，比对时跳过延续格（与 `copy_text` 取文本同一趟读法）；命中后整段跳过，
/// 故不产重叠匹配——与正则档 `regex_iterator` 的非重叠次序一致，两档的计数才能对得上。
auto scan_literal(const grid::Row &row,
                  std::size_t row_index,
                  const std::u32string &needle,
                  bool case_sensitive,
                  std::vector<RowSpan> &spans) -> void {
    const auto right = content_right(row);
    std::size_t column = 0U;
    while (column < right) {
        if (row.cell(column).is_wide_continuation()) {
            ++column;
            continue;
        }
        std::size_t cursor = column;
        std::size_t last_base = column;
        std::size_t index = 0U;
        while (index < needle.size() && cursor < right) {
            const auto &cell = row.cell(cursor);
            if (cell.is_wide_continuation()) {
                ++cursor;
                continue;
            }
            if (!code_point_equal(cell.code_point, needle[index], case_sensitive)) {
                break;
            }
            last_base = cursor;
            ++index;
            ++cursor;
        }
        if (index == needle.size()) {
            spans.push_back(RowSpan{row_index, column, span_right(row, last_base)});
            column = spans.back().last_column;
            continue;
        }
        ++column;
    }
}

/// @brief 把一个码点按 UTF-16 收进缓冲：`wchar_t` 为两字节时大于 `U+FFFF` 的折成代理对。
///
/// 分支是编译期的量而不是平台判定：`wchar_t` 的宽度就是本工具链的编码宽度，POSIX 侧四字节时
/// 码点直通，不需要第二条腿。
auto append_utf16(std::wstring &text, char32_t code_point) -> void {
    if constexpr (sizeof(wchar_t) == 2U) {
        if (code_point > 0xFFFFU) {
            const char32_t scratch = code_point - 0x10000U;
            text.push_back(static_cast<wchar_t>(0xD800U + (scratch >> 10U)));
            text.push_back(static_cast<wchar_t>(0xDC00U + (scratch & 0x3FFU)));
            return;
        }
    }
    text.push_back(static_cast<wchar_t>(code_point));
}

/// @brief 正则档的单行缓冲：引擎可吃的 UTF-16 序列，加「每个单元属于哪一基础格」的映射。
///
/// 映射是整格对齐的唯一根据：代理对的两个单元同属一个基础格，于是匹配落在一个表情符号上时
/// 区间仍覆盖它的两格，而不会出现「只选中半个字」。缓冲逐行复用——上限档的网格有 8,000,000 格，
/// 每行一次分配就要以毫秒计（实测纯转换那一趟 11.5 ms）。
auto fill_utf16_row(const grid::Row &row, std::wstring &text, std::vector<std::size_t> &unit_column) -> void {
    text.clear();
    unit_column.clear();
    const auto right = content_right(row);
    for (std::size_t column = 0U; column < right; ++column) {
        const auto &cell = row.cell(column);
        if (cell.is_wide_continuation()) {
            continue;  // 与字面量档同一匹配文本
        }
        // 代理对的两个单元同属这一基础格，故新增的每个单元都补同一列号。
        append_utf16(text, cell.code_point);
        unit_column.resize(text.size(), column);
    }
}

/// @brief 正则档的逐行扫描。
auto scan_regex(const grid::Row &row,
                std::size_t row_index,
                const std::wregex &pattern,
                std::wstring &text,
                std::vector<std::size_t> &unit_column,
                std::vector<RowSpan> &spans) -> void {
    fill_utf16_row(row, text, unit_column);
    using cursor_type = std::regex_iterator<std::wstring::const_iterator>;
    const cursor_type end;
    for (cursor_type cursor(text.cbegin(), text.cend(), pattern); cursor != end; ++cursor) {
        const auto &match = *cursor;
        if (match.length() == 0) {
            continue;  // 零宽匹配处处成立（`^`、`a*`），产不出任何一段可高亮的字符
        }
        const auto first = static_cast<std::size_t>(match.position());
        // 落在代理对后半截上的匹配不另算一段：`.` 与 `\w` 在 UTF-16 序列上逐单元成立，一个星象
        // 字符因此能匹配两次，而两次都折算到同一个基础格（同一条高亮画两遍、计数多算一倍）。
        // 相邻两单元同属一基础格只可能是代理对，故以「起始单元不是码点起点」判掉——四字节
        // `wchar_t` 档上不存在这种单元，判据在两档都成立。
        if (first > 0U && unit_column[first] == unit_column[first - 1U]) {
            continue;
        }
        const auto last = static_cast<std::size_t>(match.position() + match.length() - 1);
        spans.push_back(RowSpan{row_index, unit_column[first], span_right(row, unit_column[last])});
    }
}

/// @brief 逐行扫描并把匹配收进表；到 @ref kMaxSearchMatches 即停。
///
/// 上限按**行**判（一次行扫描内部不查），故触顶时表长可超出上限一格行的量——`truncated` 说的
/// 是「外面还有没有没人知道」，条数仍是下限，多算这一档不比少算一档更误导，而每格都查一次
/// 会让热路径多一次分支。
template <typename ScanRow>
auto scan_all(const grid::Storage &storage, ScanRow scan_row) -> std::pair<std::vector<RowSpan>, bool> {
    std::vector<RowSpan> spans;
    for (std::size_t row = 0U; row < storage.total_lines(); ++row) {
        scan_row(storage.line(row), row, spans);
        if (spans.size() >= kMaxSearchMatches) {
            return {std::move(spans), true};
        }
    }
    return {std::move(spans), false};
}

}  // namespace

SearchMatches::SearchMatches(std::vector<RowSpan> spans, bool truncated) noexcept
    : spans_{std::move(spans)}, truncated_{truncated} {}

auto SearchMatches::current() const noexcept -> std::optional<RowSpan> {
    if (!cursor_.has_value()) {
        return std::nullopt;
    }
    return spans_[*cursor_];
}

auto SearchMatches::advance(SearchDirection direction) -> std::optional<RowSpan> {
    const std::size_t size = spans_.size();
    if (size == 0U) {
        return std::nullopt;
    }
    if (!cursor_.has_value()) {
        cursor_ = (direction == SearchDirection::Forward) ? 0U : size - 1U;
    } else if (direction == SearchDirection::Forward) {
        cursor_ = (*cursor_ + 1U) % size;
    } else {
        cursor_ = (*cursor_ + size - 1U) % size;
    }
    return current();
}

auto SearchMatches::translate_rows(std::int64_t rows_up) -> void {
    if (rows_up == 0) {
        return;  // 顶边没动：界面腿每帧都调，这一档先挡掉整表重排
    }
    std::vector<RowSpan> kept;
    kept.reserve(spans_.size());
    for (const auto &span : spans_) {
        const std::int64_t moved = static_cast<std::int64_t>(span.row) - rows_up;
        if (moved < 0) {
            continue;  // 该匹配已随内容被推出存储顶端：留着它只会指着另一行内容高亮
        }
        auto shifted = span;
        shifted.row = static_cast<std::size_t>(moved);
        kept.push_back(shifted);
    }
    if (cursor_.has_value()) {
        // 表按行升序且位移对每行相同，故被丢弃的恰是表头那一段连续前缀；游标减掉这段长度就
        // 仍指着它当初的那一段匹配，落在前缀里则归空（那一段用户并没选中）。
        const std::size_t dropped = spans_.size() - kept.size();
        if (*cursor_ >= dropped) {
            cursor_ = *cursor_ - dropped;
        } else {
            cursor_ = std::nullopt;
        }
    }
    spans_ = std::move(kept);
}

auto search(const grid::Storage &storage, const SearchQuery &query) -> std::optional<SearchMatches> {
    if (query.text.empty()) {
        return SearchMatches{{}, false};  // 还没打字：既不产匹配，也不是表达式非法
    }
    if (query.regex) {
        auto flags = std::regex_constants::ECMAScript;
        if (!query.case_sensitive) {
            flags |= std::regex_constants::icase;
        }
        std::wstring pattern_text;
        for (const char32_t code_point : query.text) {
            append_utf16(pattern_text, code_point);
        }
        std::wregex pattern;
        try {
            pattern.assign(pattern_text, flags);
        } catch (const std::regex_error &) {
            return std::nullopt;  // 表达式非法：与「0 个匹配」是两件事，浮层须保留上一份结果
        }
        std::wstring text;
        std::vector<std::size_t> unit_column;
        auto [spans, truncated] =
            scan_all(storage, [&](const grid::Row &row, std::size_t row_index, std::vector<RowSpan> &out) {
                scan_regex(row, row_index, pattern, text, unit_column, out);
            });
        return SearchMatches{std::move(spans), truncated};
    }
    auto [spans, truncated] =
        scan_all(storage, [&](const grid::Row &row, std::size_t row_index, std::vector<RowSpan> &out) {
            scan_literal(row, row_index, query.text, query.case_sensitive, out);
        });
    return SearchMatches{std::move(spans), truncated};
}

}  // namespace borealis::ui
