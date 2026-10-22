// ============================================================
// quick select 的行扫描与标签分配（src/ui/quick_select.cpp）
// ------------------------------------------------------------
// 三档扫描共用一条骨架：先按本档字符表取极大游程，再在游程内判起点/尾部，最后按
// URL → 路径 → 哈希的优先级去重叠。字符表与尾修口径对齐 `src/ui/url.cpp`（那边的私有表
// 不导出，这里按同一口径重写一份），差别只有两条：URL 不查协议白名单、路径多一条「裸相对
// 至少两段斜杠」的闸。两条都是 quick select 的语义（复制而非打开）决定的，见设计稿 D2/D8。
// ============================================================

#include "borealis/ui/quick_select.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/term/utf8.h"

namespace borealis::ui {
namespace {

constexpr std::string_view kSchemeSeparator{"://"};
constexpr std::string_view kWwwPrefix{"www."};
constexpr std::string_view kUrlPunctuation{"-._~:/?#[]@!$&'()*+,;=%"};
constexpr std::string_view kPathPunctuation{"-._~:/\\@+%$"};
constexpr std::string_view kTrailingUrlPunctuation{".,;:!?'\""};

/// @brief 非 ASCII 里断开游程的码点区段（与 `ui::url` 同一张表）。
///
/// 默认放行 + 区段排除，于是 CJK 文件名/路径可以整段进候选，而 CJK 句号与全角标点会把
/// 游程断开；不必维护一张无限长的放行表。
struct CodePointRange {
    char32_t first = 0;
    char32_t last = 0;
};

constexpr std::array<CodePointRange, 8> kBreakingRanges{
    CodePointRange{0x0080U, 0x009FU},  // C1 控制
    CodePointRange{0x00A0U, 0x00A0U},  // 不换行空格
    CodePointRange{0x2000U, 0x206FU},  // 通用标点（含各类破折号、引号、零宽类）
    CodePointRange{0x3000U, 0x303FU},  // CJK 标点与占位（句号 `。` 在此）
    CodePointRange{0xFF01U, 0xFF0FU},  // 全角 ASCII 标点（`！` 到 `／`）
    CodePointRange{0xFF1AU, 0xFF20U},  // 全角 `：；＜＝＞？＠`
    CodePointRange{0xFF3BU, 0xFF40U},  // 全角 `［＼］＾＿｀`
    CodePointRange{0xFF5BU, 0xFF65U},  // 全角 `｛｜｝～` 与半角片假名标点
};

constexpr std::array<std::pair<char32_t, char32_t>, 3> kCloserPairs{
    std::pair<char32_t, char32_t>{U')', U'('},
    std::pair<char32_t, char32_t>{U']', U'['},
    std::pair<char32_t, char32_t>{U'}', U'{'},
};

[[nodiscard]] auto is_ascii_letter(char32_t code_point) noexcept -> bool {
    return (code_point >= U'a' && code_point <= U'z') || (code_point >= U'A' && code_point <= U'Z');
}

[[nodiscard]] auto is_ascii_digit(char32_t code_point) noexcept -> bool {
    return code_point >= U'0' && code_point <= U'9';
}

[[nodiscard]] auto is_ascii_alnum(char32_t code_point) noexcept -> bool {
    return is_ascii_letter(code_point) || is_ascii_digit(code_point);
}

[[nodiscard]] auto is_hex_digit(char32_t code_point) noexcept -> bool {
    return is_ascii_digit(code_point) || (code_point >= U'a' && code_point <= U'f') ||
           (code_point >= U'A' && code_point <= U'F');
}

[[nodiscard]] auto is_hex_letter(char32_t code_point) noexcept -> bool {
    return (code_point >= U'a' && code_point <= U'f') || (code_point >= U'A' && code_point <= U'F');
}

[[nodiscard]] auto to_ascii_lower(char32_t code_point) noexcept -> char32_t {
    return code_point >= U'A' && code_point <= U'Z' ? code_point + 32U : code_point;
}

[[nodiscard]] auto in_breaking_range(char32_t code_point) noexcept -> bool {
    return std::ranges::any_of(kBreakingRanges, [code_point](const CodePointRange &range) {
        return code_point >= range.first && code_point <= range.last;
    });
}

/// @brief URL 组成字符（断游程者除外），口径同 `ui::url` 的 `is_url_code_point`。
[[nodiscard]] auto is_url_code_point(char32_t code_point) noexcept -> bool {
    if (code_point < 0x21U) {
        return false;
    }
    if (code_point < 0x80U) {
        return is_ascii_alnum(code_point) ||
               kUrlPunctuation.find(static_cast<char>(code_point)) != std::string_view::npos;
    }
    return !in_breaking_range(code_point);
}

/// @brief 路径组成字符：比 URL 多了反斜杠与 `~`/`$`，少了 `#?[]&` 等会把路径截断的符。
[[nodiscard]] auto is_path_code_point(char32_t code_point) noexcept -> bool {
    if (code_point < 0x21U) {
        return false;
    }
    if (code_point < 0x80U) {
        return is_ascii_alnum(code_point) ||
               kPathPunctuation.find(static_cast<char>(code_point)) != std::string_view::npos;
    }
    return !in_breaking_range(code_point);
}

[[nodiscard]] auto is_scheme_body(char32_t code_point) noexcept -> bool {
    return is_ascii_alnum(code_point) || code_point == U'+' || code_point == U'-' || code_point == U'.';
}

[[nodiscard]] auto is_home_user_char(char32_t code_point) noexcept -> bool {
    return is_ascii_alnum(code_point) || code_point == U'_' || code_point == U'-' || code_point == U'.';
}

[[nodiscard]] auto is_trailing_url_punctuation(char32_t code_point) noexcept -> bool {
    return code_point < 0x80U &&
           kTrailingUrlPunctuation.find(static_cast<char>(code_point)) != std::string_view::npos;
}

/// @brief 落点列折回基础格列：双宽字符的延续格不承载字符。
[[nodiscard]] auto base_column(const grid::Row &row, std::size_t column) noexcept -> std::size_t {
    while (column > 0U && row.cell(column).is_wide_continuation()) {
        --column;
    }
    return column;
}

/// @brief 某列承载的码点（延续格按基础格取）。
[[nodiscard]] auto code_point_at(const grid::Row &row, std::size_t column) noexcept -> char32_t {
    return row.cell(base_column(row, column)).code_point;
}

/// @brief 取一列区间的原文，口径同 `ui::copy_text` 的行提取：跳过延续格、组合符号随基础格。
[[nodiscard]] auto text_of(const grid::Row &row, std::size_t first, std::size_t last) -> std::string {
    std::string text;
    for (std::size_t column = first; column < last; ++column) {
        if (row.cell(column).is_wide_continuation()) {
            continue;
        }
        static_cast<void>(term::append_utf8(row.cell(column).code_point, text));
        for (const auto &mark : row.combining(column)) {
            static_cast<void>(term::append_utf8(mark.code_point, text));
        }
    }
    return text;
}

/// @brief 列 @p column 起是否逐字符等于 @p ascii（大小写不敏感，且不得越出 @p limit）。
[[nodiscard]] auto matches_prefix(const grid::Row &row, std::size_t column, std::size_t limit,
                                  std::string_view ascii) -> bool {
    for (std::size_t offset = 0U; offset < ascii.size(); ++offset) {
        const auto at = column + offset;
        if (at >= limit) {
            return false;
        }
        const auto code_point = code_point_at(row, at);
        if (code_point >= 0x80U || to_ascii_lower(code_point) != static_cast<char32_t>(ascii[offset])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto count_in_run(const grid::Row &row, std::size_t first, std::size_t last, char32_t code_point)
    -> std::size_t {
    std::size_t seen = 0U;
    for (std::size_t column = first; column < last; ++column) {
        seen += code_point_at(row, column) == code_point ? 1U : 0U;
    }
    return seen;
}

/// @brief URL 尾部修剪：交替剥尾部标点与不配对的右括号，直到稳定（口径同 `ui::url`）。
[[nodiscard]] auto trim_url_tail(const grid::Row &row, std::size_t first, std::size_t last) -> std::size_t {
    auto end = last;
    for (;;) {
        const auto before = end;
        while (end > first + 1U && is_trailing_url_punctuation(code_point_at(row, end - 1U))) {
            --end;
        }
        for (const auto &[closer, opener] : kCloserPairs) {
            if (end <= first + 1U || code_point_at(row, end - 1U) != closer) {
                continue;
            }
            if (count_in_run(row, first, end, closer) > count_in_run(row, first, end, opener)) {
                --end;
                break;
            }
        }
        if (end == before) {
            return end;
        }
    }
}

struct UrlStart {
    std::size_t column = 0;
    std::size_t prefix_end = 0;  ///< `scheme://` 或 `www.` 之后的第一列；修剪后须还有字符。
};

/// @brief 在 `[first, limit)` 游程内找最左的 URL 起点。
///
/// 与 `ui::url` 的 `find_url_start` 同一条边界规则（起点前不得是字母数字），但不查协议
/// 白名单：quick select 只复制，白名单是「打开」那道闸。
[[nodiscard]] auto find_url_start(const grid::Row &row, std::size_t first, std::size_t limit)
    -> std::optional<UrlStart> {
    std::optional<UrlStart> best;
    for (std::size_t column = first; column < limit; ++column) {
        if (code_point_at(row, column) == U':' && column + 2U < limit &&
            code_point_at(row, column + 1U) == U'/' && code_point_at(row, column + 2U) == U'/') {
            std::size_t scheme_begin = column;
            while (scheme_begin > first && is_scheme_body(code_point_at(row, scheme_begin - 1U))) {
                --scheme_begin;
            }
            const bool has_scheme = scheme_begin < column &&
                                    is_ascii_letter(code_point_at(row, scheme_begin)) &&
                                    (scheme_begin == first ||
                                     !is_ascii_alnum(code_point_at(row, scheme_begin - 1U)));
            const std::size_t prefix_end = column + 3U;
            if (has_scheme && prefix_end < limit && (!best.has_value() || scheme_begin < best->column)) {
                best = UrlStart{.column = scheme_begin, .prefix_end = prefix_end};
            }
        }
        if (matches_prefix(row, column, limit, kWwwPrefix) && column + 4U < limit &&
            is_ascii_alnum(code_point_at(row, column + 4U)) &&
            (column == first || !is_ascii_alnum(code_point_at(row, column - 1U)))) {
            if (!best.has_value() || column < best->column) {
                best = UrlStart{.column = column, .prefix_end = column + 4U};
            }
        }
    }
    return best;
}

[[nodiscard]] auto scan_urls(const grid::Row &row) -> std::vector<RowSpan> {
    std::vector<RowSpan> spans;
    const std::size_t columns = row.columns();
    for (std::size_t column = 0U; column < columns;) {
        if (!is_url_code_point(code_point_at(row, column))) {
            ++column;
            continue;
        }
        std::size_t end = column;
        while (end < columns && is_url_code_point(code_point_at(row, end))) {
            ++end;
        }
        if (const auto start = find_url_start(row, column, end); start.has_value()) {
            const auto trimmed = trim_url_tail(row, start->column, end);
            if (trimmed > start->prefix_end) {
                spans.push_back(RowSpan{.row = 0U,
                                        .first_column = start->column,
                                        .last_column = trimmed});
            }
        }
        column = end;
    }
    return spans;
}

/// @brief 游程内是否含 `://`：含者整段归 URL 档，路径档不再从中切子路径。
[[nodiscard]] auto has_scheme_separator(const grid::Row &row, std::size_t first, std::size_t last) noexcept
    -> bool {
    for (std::size_t column = first; column + 2U < last; ++column) {
        if (code_point_at(row, column) == U':' && code_point_at(row, column + 1U) == U'/' &&
            code_point_at(row, column + 2U) == U'/') {
            return true;
        }
    }
    return false;
}

/// @brief 前缀之后是否还有非分隔字符（`/` 本身、`//`、`./` 不算一条路径）。
[[nodiscard]] auto has_path_content(const grid::Row &row, std::size_t first, std::size_t limit) noexcept
    -> bool {
    for (std::size_t column = first; column < limit; ++column) {
        const auto code_point = code_point_at(row, column);
        if (!row.cell(column).is_wide_continuation() && code_point != U'/' && code_point != U'\\') {
            return true;
        }
    }
    return false;
}

/// @brief 路径起点前不得是「词内字符」：`and/or` 的 `/` 与 `fooC:\` 的 `C` 都不算起点。
[[nodiscard]] auto path_boundary_before(const grid::Row &row, std::size_t column, std::size_t first) noexcept
    -> bool {
    if (column == first) {
        return true;
    }
    const auto previous = code_point_at(row, column - 1U);
    return !is_ascii_alnum(previous) && previous != U'_' && previous != U'-' && previous != U'.' &&
           previous != U'~';
}

/// @brief 若 @p column 处是一个显式路径前缀，返回前缀之后的第一列；否则回空。
[[nodiscard]] auto path_prefix_end(const grid::Row &row, std::size_t column, std::size_t limit) noexcept
    -> std::optional<std::size_t> {
    const auto code_point = code_point_at(row, column);
    if (code_point == U'/') {
        return has_path_content(row, column + 1U, limit) ? std::optional<std::size_t>{column + 1U}
                                                         : std::nullopt;
    }
    if (code_point == U'.' && column + 1U < limit && code_point_at(row, column + 1U) == U'/') {
        return has_path_content(row, column + 2U, limit) ? std::optional<std::size_t>{column + 2U}
                                                         : std::nullopt;
    }
    if (code_point == U'.' && column + 1U < limit && code_point_at(row, column + 1U) == U'\\') {
        return has_path_content(row, column + 2U, limit) ? std::optional<std::size_t>{column + 2U}
                                                         : std::nullopt;
    }
    if (code_point == U'.' && column + 2U < limit && code_point_at(row, column + 1U) == U'.' &&
        (code_point_at(row, column + 2U) == U'/' || code_point_at(row, column + 2U) == U'\\')) {
        return has_path_content(row, column + 3U, limit) ? std::optional<std::size_t>{column + 3U}
                                                         : std::nullopt;
    }
    if (code_point == U'~') {
        std::size_t at = column + 1U;
        while (at < limit && is_home_user_char(code_point_at(row, at))) {
            ++at;
        }
        if (at < limit && (code_point_at(row, at) == U'/' || code_point_at(row, at) == U'\\')) {
            return has_path_content(row, at + 1U, limit) ? std::optional<std::size_t>{at + 1U}
                                                         : std::nullopt;
        }
        return std::nullopt;
    }
    if (is_ascii_letter(code_point) && column + 1U < limit && code_point_at(row, column + 1U) == U':' &&
        column + 2U < limit &&
        (code_point_at(row, column + 2U) == U'/' || code_point_at(row, column + 2U) == U'\\')) {
        return column + 3U;  // 盘符根 `C:\` 也接受：前缀之后没有名字仍是合法路径。
    }
    if (code_point == U'\\' && column + 1U < limit && code_point_at(row, column + 1U) == U'\\') {
        return has_path_content(row, column + 2U, limit) ? std::optional<std::size_t>{column + 2U}
                                                         : std::nullopt;
    }
    return std::nullopt;
}

/// @brief 裸相对路径：游程起点、至少两段分隔、且不全是数字/分隔符（日期不标）。
[[nodiscard]] auto bare_relative_ok(const grid::Row &row, std::size_t first, std::size_t last) noexcept
    -> bool {
    const auto first_code_point = code_point_at(row, first);
    if (!(is_ascii_alnum(first_code_point) || first_code_point == U'_' || first_code_point == U'.')) {
        return false;
    }
    std::size_t separators = 0U;
    bool has_name_char = false;
    for (std::size_t column = first; column < last; ++column) {
        if (row.cell(column).is_wide_continuation()) {
            continue;
        }
        const auto code_point = code_point_at(row, column);
        if (code_point == U'/' || code_point == U'\\') {
            ++separators;
            continue;
        }
        if (!is_ascii_digit(code_point) && code_point != U'.') {
            has_name_char = true;
        }
    }
    return separators >= 2U && has_name_char;
}

[[nodiscard]] auto trim_path_tail(const grid::Row &row, std::size_t first, std::size_t last) noexcept
    -> std::size_t {
    auto end = last;
    while (end > first) {
        const auto code_point = code_point_at(row, end - 1U);
        if (code_point != U'.' && code_point != U':') {
            break;
        }
        --end;
    }
    return end;
}

[[nodiscard]] auto scan_paths(const grid::Row &row) -> std::vector<RowSpan> {
    std::vector<RowSpan> spans;
    const std::size_t columns = row.columns();
    for (std::size_t column = 0U; column < columns;) {
        if (!is_path_code_point(code_point_at(row, column))) {
            ++column;
            continue;
        }
        std::size_t end = column;
        while (end < columns && is_path_code_point(code_point_at(row, end))) {
            ++end;
        }
        if (!has_scheme_separator(row, column, end)) {
            const auto trimmed = trim_path_tail(row, column, end);
            std::optional<std::size_t> start;
            for (std::size_t at = column; at < trimmed; ++at) {
                if (path_boundary_before(row, at, column) && path_prefix_end(row, at, trimmed).has_value()) {
                    start = at;
                    break;
                }
            }
            if (!start.has_value() && bare_relative_ok(row, column, trimmed)) {
                start = column;
            }
            if (start.has_value() && trimmed > *start) {
                spans.push_back(RowSpan{.row = 0U, .first_column = *start, .last_column = trimmed});
            }
        }
        column = end;
    }
    return spans;
}

[[nodiscard]] auto hash_boundary_before(const grid::Row &row, std::size_t column) noexcept -> bool {
    if (column == 0U) {
        return true;
    }
    const auto previous = code_point_at(row, column - 1U);
    return !is_ascii_alnum(previous) && previous != U'_' && previous != U'-';
}

[[nodiscard]] auto hash_boundary_after(const grid::Row &row, std::size_t column) noexcept -> bool {
    if (column >= row.columns()) {
        return true;
    }
    const auto next = code_point_at(row, column);
    return !is_ascii_alnum(next) && next != U'_' && next != U'-';
}

[[nodiscard]] auto scan_hashes(const grid::Row &row) -> std::vector<RowSpan> {
    std::vector<RowSpan> spans;
    const std::size_t columns = row.columns();
    for (std::size_t column = 0U; column < columns;) {
        if (!is_hex_digit(code_point_at(row, column))) {
            ++column;
            continue;
        }
        std::size_t end = column;
        bool has_letter = false;
        while (end < columns && is_hex_digit(code_point_at(row, end))) {
            has_letter = has_letter || is_hex_letter(code_point_at(row, end));
            ++end;
        }
        const std::size_t length = end - column;
        if (length >= 7U && length <= 64U && has_letter && hash_boundary_before(row, column) &&
            hash_boundary_after(row, end)) {
            spans.push_back(RowSpan{.row = 0U, .first_column = column, .last_column = end});
        }
        column = end;
    }
    return spans;
}

[[nodiscard]] auto overlaps(const RowSpan &a, const RowSpan &b) noexcept -> bool {
    return a.first_column < b.last_column && b.first_column < a.last_column;
}

struct SpanWithKind {
    RowSpan span;
    QuickSelectKind kind = QuickSelectKind::Url;
};

}  // namespace

auto scan_quick_select_row(const grid::Row &row, std::size_t storage_row)
    -> std::vector<QuickSelectCandidate> {
    std::vector<SpanWithKind> accepted;
    const auto add_new = [&accepted](const std::vector<RowSpan> &spans, QuickSelectKind kind) -> void {
        for (auto span : spans) {
            span.row = 0U;
            const bool conflicts = std::ranges::any_of(accepted, [&span](const SpanWithKind &existing) {
                return overlaps(existing.span, span);
            });
            if (!conflicts) {
                accepted.push_back(SpanWithKind{.span = span, .kind = kind});
            }
        }
    };
    add_new(scan_urls(row), QuickSelectKind::Url);
    add_new(scan_paths(row), QuickSelectKind::Path);
    add_new(scan_hashes(row), QuickSelectKind::Hash);

    std::ranges::sort(accepted, {}, [](const SpanWithKind &item) { return item.span.first_column; });

    std::vector<QuickSelectCandidate> candidates;
    candidates.reserve(accepted.size());
    for (const auto &item : accepted) {
        QuickSelectCandidate candidate;
        candidate.span = RowSpan{.row = storage_row,
                                 .first_column = item.span.first_column,
                                 .last_column = item.span.last_column};
        candidate.kind = item.kind;
        candidate.text = text_of(row, item.span.first_column, item.span.last_column);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

auto assign_quick_select_labels(std::vector<QuickSelectCandidate> &candidates, std::string_view alphabet,
                                std::size_t limit) -> void {
    if (alphabet.empty()) {
        for (auto &candidate : candidates) {
            candidate.label.clear();
        }
        return;
    }
    const std::size_t capacity = alphabet.size() * alphabet.size();
    const std::size_t wanted = std::min(limit, capacity);
    if (candidates.size() > wanted) {
        candidates.resize(wanted);
    }
    const bool two_char = candidates.size() > alphabet.size();
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        auto &label = candidates[index].label;
        label.clear();
        if (two_char) {
            label.push_back(alphabet[index / alphabet.size()]);
            label.push_back(alphabet[index % alphabet.size()]);
        } else {
            label.push_back(alphabet[index]);
        }
    }
}

auto quick_select_label_matches(std::string_view label, std::string_view typed) noexcept -> bool {
    return label.starts_with(typed);
}

auto quick_select_lookup(const std::vector<QuickSelectCandidate> &candidates, std::string_view typed) noexcept
    -> const QuickSelectCandidate * {
    if (typed.empty()) {
        return nullptr;
    }
    const auto found = std::ranges::find(candidates, typed, &QuickSelectCandidate::label);
    return found == candidates.end() ? nullptr : &*found;
}

}  // namespace borealis::ui
