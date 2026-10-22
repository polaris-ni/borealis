#pragma once

// ============================================================
// quick select 的行扫描与标签分配（include/borealis/ui/quick_select.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.02` 的 quick select 延后子项里「与绘制无关、纯算得清」的那半：
// 在**一行**内找出 URL / 路径 / 哈希三类可复制区间、按优先级去重叠、给候选分配字母标签，
// 以及按已输入前缀查候选。视口只负责「扫哪几行、画在哪、什么时候退模」。
//
// 不含 Aurora 类型（AGENTS.md §4.4 第 20 条）：字符表、优先级、标签枚举与截断都能脱开界面
// 全量单测。行号沿用 `ui::RowSpan` 的**存储行序**（`grid::Storage::line` 的索引），本件不换算
// 可见窗——副本行序随回看偏移漂移，存副本行号会在滚动后指错内容。
//
// 与 `ui::url` 的分工：URL 档复用同一套字符表与尾部修剪口径，但**不查 `terminal.link_schemes`
// 白名单**。白名单是「打开」的安全闸；quick select 只把屏上文字写剪贴板，不发起任何打开动作，
// 套用白名单会让 `ftp://` 一类看得见的串反而取不到。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/grid/row.h"
#include "borealis/ui/selection.h"

namespace borealis::ui {

/// @brief 候选的档位。三档按 URL → Path → Hash 的次序参与去重叠。
enum class QuickSelectKind : std::uint8_t {
    Url,   ///< 带 `scheme://` 的串，或 `www.` 起头的串。
    Path,  ///< 根/家目录/显式相对/盘符/UNC，以及至少两段斜杠的裸相对路径。
    Hash,  ///< 7–64 位十六进制段（至少含一个 a–f，故纯十进制数不算）。
};

/// @brief 一条候选：范围、档位、开模时冻结的文本与标签。
///
/// `text` 在扫描时提取，不在按下标签时重读网格：快照语义让「标签指向的那段文字」与「最终
/// 写进剪贴板的文字」恒等，即使程序在开模后改写了那一行。`label` 由
/// `assign_quick_select_labels()` 统一分配；扫描产物为空串。
struct QuickSelectCandidate {
    RowSpan span{};                                   ///< 存储行序的闭开列区间。
    QuickSelectKind kind = QuickSelectKind::Url;      ///< 档位（优先级与去重叠用）。
    std::string text;                                 ///< 复制目标（UTF-8，双宽/组合已按行口径提取）。
    std::string label;                                ///< 字母标签；未分配时为空。

    [[nodiscard]] auto operator==(const QuickSelectCandidate &other) const noexcept -> bool = default;
};

/// @brief 默认标签字母集与候选上限（9 个字母 ⇒ 81 个双字符标签）。
inline constexpr std::string_view kQuickSelectAlphabet{"asdfghjkl"};
inline constexpr std::size_t kQuickSelectMaxCandidates =
    std::string_view{kQuickSelectAlphabet}.size() * std::string_view{kQuickSelectAlphabet}.size();

/// @brief 扫描一行，按 URL → 路径 → 哈希的优先级产出互不重叠的候选（行内从左到右）。
///
/// 只认单行内成立的范围：折行的 URL/路径不拼接，行首的续行片段也不会继承上一行的 scheme。
/// `label` 留空；跨行汇总后由 `assign_quick_select_labels()` 一次分配。
/// @param row 一行网格内容（只读）。
/// @param storage_row 该行在权威网格里的绝对行号，原样写进候选。
/// @return 按起始列升序的候选；没有可识别的串时为空。
[[nodiscard]] auto scan_quick_select_row(const grid::Row &row, std::size_t storage_row)
    -> std::vector<QuickSelectCandidate>;

/// @brief 给候选按阅读序分配标签，超过上限即截断。
///
/// `N <= alphabet.size()` 时用单字符；否则**全部**用双字符（首位分组、次位枚举）。两档之间不混用：
/// 混用会让「输入一位是完成还是前缀」取决于候选总数，状态机无法用一条规则说清。
/// @param candidates 待标候选；就地写入 `label` 并按上限截断长度。
/// @param alphabet 标签字母集（须非空且无重复；默认 `asdfghjkl`）。
/// @param limit 候选上限；实际上限还会被 `alphabet.size()²` 收窄。
auto assign_quick_select_labels(std::vector<QuickSelectCandidate> &candidates,
                                std::string_view alphabet = kQuickSelectAlphabet,
                                std::size_t limit = kQuickSelectMaxCandidates) -> void;

/// @brief 标签是否仍可能由当前输入前缀完成（`typed` 为空恒真）。
[[nodiscard]] auto quick_select_label_matches(std::string_view label, std::string_view typed) noexcept -> bool;

/// @brief 按当前输入前缀找**完整命中**的候选；没有完整命中（含前缀阶段）回空。
[[nodiscard]] auto quick_select_lookup(const std::vector<QuickSelectCandidate> &candidates,
                                       std::string_view typed) noexcept -> const QuickSelectCandidate *;

}  // namespace borealis::ui
