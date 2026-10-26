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

// 读方向的授权档（`SPEC.FEAT.CONN.12`）：三档的名字就是需求判据 ①② 里那三个词，而 `deny` 排在
// 首位不是枚举顺序的巧合——落盘名是对外契约，改档名等于让老配置里那一行读不回来。
inline constexpr std::array<EnumName, 3> kClipboardReadNames{{
    {"deny", static_cast<std::int64_t>(term::ClipboardReadPolicy::Deny)},
    {"allow", static_cast<std::int64_t>(term::ClipboardReadPolicy::Allow)},
    {"ask", static_cast<std::int64_t>(term::ClipboardReadPolicy::Ask)},
}};

// 打开一条链接前的确认档（`SPEC.FEAT.INTERACT.05`，两枚键共用）：`ask_once` 排第二就是需求那句
// 「首次确认并记住同域」的缺省档，而落盘名一旦写下即对外契约——改名等于让老配置里那一行读不回来。
inline constexpr std::array<EnumName, 3> kLinkConfirmNames{{
    {"never", static_cast<std::int64_t>(LinkConfirmPolicy::Never)},
    {"ask_once", static_cast<std::int64_t>(LinkConfirmPolicy::AskOnce)},
    {"ask_always", static_cast<std::int64_t>(LinkConfirmPolicy::AskAlways)},
}};

inline constexpr std::array<EnumName, 2> kTabNamePriorityNames{{
    {"manual_wins", static_cast<std::int64_t>(TabNamePriority::ManualWins)},
    {"osc_wins", static_cast<std::int64_t>(TabNamePriority::OscWins)},
}};

// 界面语言档（`SPEC.FEAT.PREF.05`）：这两枚落盘名**同时是 i18n 的区域标签**，故装填侧只经下面
// `language_tag()` 取值，不自列第二份名字。
inline constexpr std::array<EnumName, 2> kInterfaceLanguageNames{{
    {"zh", static_cast<std::int64_t>(InterfaceLanguage::Zh)},
    {"en", static_cast<std::int64_t>(InterfaceLanguage::En)},
}};

// 界面外壳明暗档（`SPEC.FEAT.PREF.01`，`codespec/UI_CHROME.draft.md` D1）：`system` 排首即缺省，
// 落盘名一旦写下即对外契约——改名等于让老配置里那一行读不回来。
inline constexpr std::array<EnumName, 3> kChromeThemeNames{{
    {"system", static_cast<std::int64_t>(ChromeTheme::System)},
    {"dark", static_cast<std::int64_t>(ChromeTheme::Dark)},
    {"light", static_cast<std::int64_t>(ChromeTheme::Light)},
}};

inline constexpr std::array<EnumName, 2> kAmbiguousWidthNames{{
    {"narrow", static_cast<std::int64_t>(term::AmbiguousWidth::Narrow)},
    {"wide", static_cast<std::int64_t>(term::AmbiguousWidth::Wide)},
}};

inline constexpr std::array<EnumName, 3> kUnrepresentableNames{{
    {"replace", static_cast<std::int64_t>(term::UnrepresentablePolicy::Replace)},
    {"drop", static_cast<std::int64_t>(term::UnrepresentablePolicy::DropWithNotice)},
    {"pass_through_utf8", static_cast<std::int64_t>(term::UnrepresentablePolicy::PassThroughUtf8)},
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

/// @brief 界面语言档 → i18n 区域标签（`SPEC.FEAT.PREF.05`）。
///
/// 落盘名与区域标签**刻意同源**，且本函数是那条同源关系的唯一读点：装填侧若自己再列一遍
/// `{Zh → "zh"}`，「配置里写 `zh`」与「词条表住在 `zh` 档」就会分叉，而这个分叉在中文档下
/// 看不见（查表与回落都照常命中），到英文档补齐那天才现形。
/// @param language 语言档。
/// @return 该档的区域标签，逐字等于它的落盘名；表里没有时为空串（两档都在表上，故取不到空）。
[[nodiscard]] inline auto language_tag(InterfaceLanguage language) -> std::string_view {
    return name_of(kInterfaceLanguageNames, static_cast<std::int64_t>(language));
}

}  // namespace borealis::config
