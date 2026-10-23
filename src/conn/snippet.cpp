// ============================================================
// 快捷片段纯逻辑件实现（src/conn/snippet.cpp）
// ------------------------------------------------------------
// 设计稿 `codespec/UI_SNIPPETS.draft.md` D2/D4/D5：扫描、渲染、过滤与校验都在
// 这一层定死，面板与配置装载只消费结果。字面量一律 ASCII（`AGENTS.md` §4.3）。
// ============================================================

#include "borealis/conn/snippet.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace borealis::conn {

namespace {

constexpr std::string_view kIdPrefix = "snip-";
constexpr std::uint64_t kIdCeiling = 1'000'000'000'000'000ULL;

[[nodiscard]] constexpr auto is_placeholder_char(char ch) noexcept -> bool {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '.' || ch == '-';
}

[[nodiscard]] constexpr auto is_control_char(char ch) noexcept -> bool {
    const auto value = static_cast<unsigned char>(ch);
    return value < 0x20U || value == 0x7FU;
}

[[nodiscard]] constexpr auto is_name_space(char ch) noexcept -> bool {
    return ch == ' ' || ch == '\t';
}

[[nodiscard]] auto valid_placeholder_name(std::string_view name) noexcept -> bool {
    if (name.empty() || name.size() > kSnippetPlaceholderNameMaxBytes) {
        return false;
    }
    return std::ranges::all_of(name, is_placeholder_char);
}

/// @brief 一次线性扫描，产出**合法**占位符；非法/未闭合只前进不产出。
[[nodiscard]] auto scan_placeholders(std::string_view body) -> std::vector<SnippetPlaceholder> {
    std::vector<SnippetPlaceholder> out;
    std::size_t cursor = 0;
    while (cursor + 2U <= body.size()) {
        const auto open = body.find("{{", cursor);
        if (open == std::string_view::npos) {
            break;
        }
        const auto close = body.find("}}", open + 2U);
        if (close == std::string_view::npos) {
            break;  // 未闭合：其余文本按字面，不再产候选。
        }
        const auto name = body.substr(open + 2U, close - (open + 2U));
        if (valid_placeholder_name(name)) {
            out.push_back(SnippetPlaceholder{.name = std::string{name}, .begin = open, .end = close + 2U});
            cursor = close + 2U;
            continue;
        }
        // 非法名之后仍可能有合法 token（`{{bad {{good}}`）：只跳过这一对花括号的起点。
        cursor = open + 2U;
    }
    return out;
}

[[nodiscard]] constexpr auto ascii_lower(char ch) noexcept -> char {
    return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

/// @brief ASCII 大小写不敏感的包含匹配；非 ASCII 字节原样比较（稿 §6 第 5 条）。
[[nodiscard]] auto contains_folded(std::string_view haystack, std::string_view needle) noexcept -> bool {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool matched = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            if (ascii_lower(haystack[start + offset]) != ascii_lower(needle[offset])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto matches(const Snippet &snippet, std::string_view query) noexcept -> bool {
    return contains_folded(snippet.name, query) || contains_folded(snippet.group, query) ||
           contains_folded(snippet.body, query);
}

/// @brief 解析 `snip-<十进制>` 的号；不符合形态或为 0 时回 0。
[[nodiscard]] auto id_number(std::string_view id) noexcept -> std::uint64_t {
    if (!id.starts_with(kIdPrefix)) {
        return 0;
    }
    const auto digits = id.substr(kIdPrefix.size());
    if (digits.empty()) {
        return 0;
    }
    std::uint64_t value = 0;
    for (const char ch : digits) {
        if (ch < '0' || ch > '9') {
            return 0;
        }
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (kIdCeiling - digit) / 10U) {
            return 0;  // 手改出来的超长号不参与 max 计算，免得回绕后撞号。
        }
        value = value * 10U + digit;
    }
    return value;
}

}  // namespace

auto snippet_placeholders(std::string_view body) -> std::vector<SnippetPlaceholder> {
    return scan_placeholders(body);
}

auto snippet_placeholder_names(std::string_view body) -> std::vector<std::string> {
    std::vector<std::string> names;
    for (auto &placeholder : scan_placeholders(body)) {
        if (std::ranges::find(names, placeholder.name) == names.end()) {
            names.push_back(std::move(placeholder.name));
        }
    }
    return names;
}

auto render_snippet(std::string_view body, const std::vector<SnippetFill> &fills) -> std::string {
    const auto placeholders = scan_placeholders(body);
    if (placeholders.empty()) {
        return std::string{body};
    }
    std::string out;
    out.reserve(body.size());
    std::size_t cursor = 0;
    for (const auto &placeholder : placeholders) {
        out.append(body.substr(cursor, placeholder.begin - cursor));
        const auto found = std::find_if(
            fills.begin(), fills.end(),
            [&placeholder](const SnippetFill &fill) { return fill.name == placeholder.name; });
        if (found != fills.end()) {
            out.append(found->value);
        } else {
            out.append(body.substr(placeholder.begin, placeholder.end - placeholder.begin));
        }
        cursor = placeholder.end;
    }
    out.append(body.substr(cursor));
    return out;
}

auto snippet_groups(const std::vector<Snippet> &snippets) -> std::vector<std::string> {
    std::vector<std::string> groups;
    for (const auto &snippet : snippets) {
        if (snippet.group.empty()) {
            continue;
        }
        if (std::ranges::find(groups, snippet.group) == groups.end()) {
            groups.push_back(snippet.group);
        }
    }
    return groups;
}

auto snippet_rows(const std::vector<Snippet> &snippets, std::string_view query, std::string_view group)
    -> std::vector<std::size_t> {
    std::vector<std::size_t> rows;
    for (std::size_t index = 0; index < snippets.size(); ++index) {
        const auto &snippet = snippets[index];
        if (!group.empty() && snippet.group != group) {
            continue;
        }
        if (!matches(snippet, query)) {
            continue;
        }
        rows.push_back(index);
    }
    return rows;
}

auto next_snippet_id(const std::vector<Snippet> &snippets) -> std::string {
    std::uint64_t highest = 0;
    for (const auto &snippet : snippets) {
        highest = std::max(highest, id_number(snippet.id));
    }
    // `highest` 覆盖了所有能解析成 `snip-<n>` 的既有 id，故 max+1 必不撞号；形态不合法
    // 的 id 不参与 max，也不会等于新造出来的规范形态（`snip-+7` 一类）。
    return std::string{kIdPrefix} + std::to_string(highest + 1U);
}

auto validate_snippet(std::string_view name, std::string_view group, std::string_view body)
    -> SnippetIssue {
    const bool blank_name = !name.empty() && std::ranges::all_of(name, is_name_space);
    if (name.empty() || blank_name) {
        return SnippetIssue::EmptyName;
    }
    if (name.size() > kSnippetNameMaxBytes) {
        return SnippetIssue::NameTooLong;
    }
    if (std::ranges::any_of(name, is_control_char)) {
        return SnippetIssue::NameControlChar;
    }
    if (group.size() > kSnippetGroupMaxBytes) {
        return SnippetIssue::GroupTooLong;
    }
    if (std::ranges::any_of(group, is_control_char)) {
        return SnippetIssue::GroupControlChar;
    }
    if (body.empty()) {
        return SnippetIssue::EmptyBody;
    }
    if (body.size() > kSnippetBodyMaxBytes) {
        return SnippetIssue::BodyTooLong;
    }
    return SnippetIssue::None;
}

}  // namespace borealis::conn
