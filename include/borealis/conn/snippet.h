#pragma once

// ============================================================
// 快捷片段纯逻辑件（include/borealis/conn/snippet.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.07` 的 snippets 延后子项（设计稿 `codespec/UI_SNIPPETS.draft.md`
// D2/D4/D5）。本头不含 Aurora 类型、不碰文件系统，故可独立单测（`AGENTS.md`
// §4.4 第 20 条）：存储层、面板与装配层共用这一份语义，不各自再解释一遍。
//
// 顺序即语义：`std::vector<Snippet>` 的存储序就是面板显示序，也是搜索过滤后的
// 相对序（`snippet_rows` 回下标而不是重排）。本期不做排序与拖拽（稿 §6）。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace borealis::conn {

/// @brief 名称的字节上限（UTF-8 按字节计；界面侧不额外截断）。
constexpr std::size_t kSnippetNameMaxBytes = 64;
/// @brief 分组名的字节上限；空串表示未分组。
constexpr std::size_t kSnippetGroupMaxBytes = 32;
/// @brief 正文的字节上限。
constexpr std::size_t kSnippetBodyMaxBytes = 8192;
/// @brief `{{name}}` 里 `name` 的字节上限。
constexpr std::size_t kSnippetPlaceholderNameMaxBytes = 32;

/// @brief 一条命令片段；`id` 是稳定句柄（`snip-N`），名称可重名。
struct Snippet {
    std::string id{};
    std::string name{};
    std::string group{};  ///< 空＝未分组。
    std::string body{};   ///< UTF-8 单行文本；可含 `{{name}}`。
    bool enter{false};    ///< 发送后是否再追加一个换行（缺省关，片段不自动执行）。

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const Snippet &other) const noexcept -> bool = default;
};

/// @brief 正文里一个**合法**占位符的位置（字节偏移）与名字。
struct SnippetPlaceholder {
    std::string name{};
    std::size_t begin{0};  ///< `{{` 的起始字节。
    std::size_t end{0};    ///< `}}` 之后一字节（替换区间为 `[begin, end)`）。

    [[nodiscard]] auto operator==(const SnippetPlaceholder &other) const noexcept -> bool = default;
};

/// @brief 填充框交回的一格：占位名与用户输入的原文。
struct SnippetFill {
    std::string name{};
    std::string value{};

    [[nodiscard]] auto operator==(const SnippetFill &other) const noexcept -> bool = default;
};

/// @brief 保存前就地校验的三档之一（名称 → 分组 → 正文，先命中先回）。
enum class SnippetIssue : std::uint8_t {
    None,
    EmptyName,
    NameTooLong,
    NameControlChar,
    GroupTooLong,
    GroupControlChar,
    EmptyBody,
    BodyTooLong,
};

/// @brief 扫描正文里的全部**合法**占位符出现（按出现序；同名可出现多次）。
///
/// 语法：`{{` + 1..32 字节的 `[A-Za-z0-9_.-]` + `}}`。非法名或未闭合的 `{{`
/// 按字面保留，不报错；非法名之后的文本继续扫描，故 `{{bad {{good}}` 仍能找到 `good`。
[[nodiscard]] auto snippet_placeholders(std::string_view body) -> std::vector<SnippetPlaceholder>;

/// @brief 只取占位名（首现序去重），供填充框逐行造输入框。
[[nodiscard]] auto snippet_placeholder_names(std::string_view body) -> std::vector<std::string>;

/// @brief 按名替换**全部**出现；同名填充以第一条为准，缺值的占位符原样保留。
///
/// 纯文本替换：不转义、不加引号、不改写空白——填进去的字节就是发出去的字节（稿 D4）。
[[nodiscard]] auto render_snippet(std::string_view body, const std::vector<SnippetFill> &fills)
    -> std::string;

/// @brief 现存分组名，按首现序去重且排除空串（未分组不占下拉一格）。
[[nodiscard]] auto snippet_groups(const std::vector<Snippet> &snippets) -> std::vector<std::string>;

/// @brief 过滤后的行下标（存储序）；`group` 为空＝不按分组过滤。
///
/// 搜索命中名称、分组、正文三处，ASCII 大小写不敏感；`query` 为空＝全中。
[[nodiscard]] auto snippet_rows(const std::vector<Snippet> &snippets, std::string_view query,
                                std::string_view group) -> std::vector<std::size_t>;

/// @brief 分配下一个稳定 id：`snip-<最大已用号 + 1>`，已占用则继续加一。
[[nodiscard]] auto next_snippet_id(const std::vector<Snippet> &snippets) -> std::string;

/// @brief 保存前校验（名称 → 分组 → 正文，先命中先回）。
[[nodiscard]] auto validate_snippet(std::string_view name, std::string_view group, std::string_view body)
    -> SnippetIssue;

}  // namespace borealis::conn
