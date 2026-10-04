#pragma once

// ============================================================
// 枚举落盘名的唯一表（src/config/schema_names.h）
// ------------------------------------------------------------
// 「枚举值 ↔ JSON 文本名」原先只住在 `store.cpp` 的匿名命名空间里。设置面板的搬运件
// （`config/form_transfer.cpp`）也要同一批名字：搬出配置时给表单一个文本档，搬回配置时把文本档
// 折成档位。留在原处就是让第二处自己再列一遍落盘名，而落盘名一旦分叉，面板里选中的档经
// `Store::replace()` 写回后再读就成了别的档——这正是裁决 7.46② 反复避开的「第二真值源」。
// 于是这张表搬到两个消费方都看得见的私有头：不进 `include/borealis/`，因为公共头上开一份
// 「枚举名表」等于把它变成第二份契约，而契约是 `settings.h` 的字段与落盘 JSON。
//
// 本表与 `ui::settings_catalog()` 各下拉行的 `choices` 是**同一批字符串**。那条等价关系不由
// 本头自证，由 `tests/unit/utest_form_transfer.cpp` 逐键验：该件把表里每个候选都喂给搬运件，
// 认不出的候选会给出「与基线逐位相同」的结果，于是拼写漂移必然落在某个证人上而不是静默生效。
// ============================================================

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "borealis/config/settings.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"

namespace borealis::config {

/// @brief 一个枚举值与它在 JSON 里的文本名（写与读共用同一张表，避免两处各列一遍名字）。
struct EnumName {
    std::string_view name;
    std::int64_t value;
};

inline constexpr std::array<EnumName, 3> kBellNames{{
    {"off", static_cast<std::int64_t>(BellMode::Off)},
    {"visual", static_cast<std::int64_t>(BellMode::Visual)},
    {"audible", static_cast<std::int64_t>(BellMode::Audible)},
}};

inline constexpr std::array<EnumName, 2> kLongLineNames{{
    {"truncate", static_cast<std::int64_t>(LongLinePolicy::Truncate)},
    {"wrap", static_cast<std::int64_t>(LongLinePolicy::Wrap)},
}};

inline constexpr std::array<EnumName, 3> kPasteNewlineNames{{
    {"as_is", static_cast<std::int64_t>(PasteNewlinePolicy::AsIs)},
    {"filter", static_cast<std::int64_t>(PasteNewlinePolicy::Filter)},
    {"convert", static_cast<std::int64_t>(PasteNewlinePolicy::Convert)},
}};

inline constexpr std::array<EnumName, 3> kRightClickNames{{
    {"context_menu", static_cast<std::int64_t>(RightClickAction::ContextMenu)},
    {"paste", static_cast<std::int64_t>(RightClickAction::Paste)},
    {"copy_on_select", static_cast<std::int64_t>(RightClickAction::CopyOnSelect)},
}};

inline constexpr std::array<EnumName, 2> kTabNamePriorityNames{{
    {"manual_wins", static_cast<std::int64_t>(TabNamePriority::ManualWins)},
    {"osc_wins", static_cast<std::int64_t>(TabNamePriority::OscWins)},
}};

inline constexpr std::array<EnumName, 2> kAmbiguousWidthNames{{
    {"narrow", static_cast<std::int64_t>(term::AmbiguousWidth::Narrow)},
    {"wide", static_cast<std::int64_t>(term::AmbiguousWidth::Wide)},
}};

inline constexpr std::array<EnumName, 3> kCursorShapeNames{{
    {"block", static_cast<std::int64_t>(term::CursorShape::Block)},
    {"underline", static_cast<std::int64_t>(term::CursorShape::Underline)},
    {"bar", static_cast<std::int64_t>(term::CursorShape::Bar)},
}};

/// @brief 按名取枚举值。
/// @param names 映射表。
/// @param name JSON 文本名。
/// @return 命中的底层值；表里没有时为空（由调用方回落默认并留痕）。
[[nodiscard]] inline auto value_of(std::span<const EnumName> names, std::string_view name)
    -> std::optional<std::int64_t> {
    for (const auto &entry : names) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return {};
}

/// @brief 按枚举值取名。
/// @param names 映射表。
/// @param value 枚举的底层值。
/// @return 文本名；表里没有时为空串，该键于是**不写盘**（读回时按缺键回落默认）。
[[nodiscard]] inline auto name_of(std::span<const EnumName> names, std::int64_t value) -> std::string_view {
    for (const auto &entry : names) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return {};
}

}  // namespace borealis::config
