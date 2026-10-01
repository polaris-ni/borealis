#pragma once

// ============================================================
// 内置预置配色表（include/borealis/config/themes.h）
// ------------------------------------------------------------
// `SPEC.FEAT.PREF.01` 的「内置 ≥8 套常用配色」按裁决 7.26② 随配置层首版落地、缺省 Dracula：
// 上屏第一棒的像素判据需要具体色值，`ui::PaletteSpec` 的全零初始化无法验收。
//
// 本表是**色值的唯一来源**：`AppearanceSettings` 的默认调色板由它派生（见
// `src/config/settings.cpp`），换主题名即换整套色，两处不会各存一份而随时间漂移。
// 256 色的其余档位由 `ui::palette_color` 按 xterm 固定式子派生、不随主题，故不在表内。
//
// 主题名是**存储键**（一律 ASCII 小写、连字符分词），设置面板显示的中文名另经 `StringTable`
// 映射（裁决 7.25⑬ 同口径），因此改显示文案不动已存配置。
//
// 本头刻意不含 Aurora 类型（AGENTS.md §4.4 第 20 条）。
// ============================================================

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "borealis/ui/palette.h"

namespace borealis::config {

/// @brief 缺省主题名（裁决 7.26②）。
inline constexpr std::string_view kDefaultThemeName{"dracula"};

/// @brief UI chrome 的覆盖色：主题可选携带（裁决 7.25⑪ 的预留字段）。
///
/// 字段名与 `codespec/UI_OVERVIEW.draft.md` §2.1 的 token 表逐一对应，首版**无消费方、
/// 八套预置全为空**：chrome 固定一套 token，切主题的视觉反馈只落在终端区（裁决 7.26② 明写
/// chrome 覆盖不在首版）。字段先于入口存在，是为了让后补覆盖入口时不必翻改已存配置。
struct ChromeOverride {
    std::optional<ui::RgbaColor> window;  ///< 窗口底色。
    std::optional<ui::RgbaColor> chrome;  ///< 标题栏、标签条底、侧栏、导航、菜单底、分隔条。
    std::optional<ui::RgbaColor> raised;  ///< 活动标签、卡片、悬停态。
    std::optional<ui::RgbaColor> input;   ///< 输入框、键位胶囊、终端格底。
    std::optional<ui::RgbaColor> sel;     ///< 选中行与当前项底。
    std::optional<ui::RgbaColor> line;    ///< 分隔线。
    std::optional<ui::RgbaColor> line2;   ///< 控件描边。
    std::optional<ui::RgbaColor> text;    ///< 主文本。
    std::optional<ui::RgbaColor> mut;     ///< 次文本。
    std::optional<ui::RgbaColor> dim;     ///< 弱化与占位文本。
    std::optional<ui::RgbaColor> accent;  ///< 焦点、活动项、主按钮。
    std::optional<ui::RgbaColor> cyan;    ///< 链接与提示。
    std::optional<ui::RgbaColor> green;   ///< 正常与在线。
    std::optional<ui::RgbaColor> yellow;  ///< 警告与延后。
    std::optional<ui::RgbaColor> red;     ///< 错误与危险动作。
    std::optional<ui::RgbaColor> pink;    ///< 强调符号。

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const ChromeOverride &other) const noexcept -> bool = default;
};

/// @brief 一套预置主题：存储键名 + 派生调色板 + 可选 chrome 覆盖。
struct BuiltinTheme {
    std::string_view name;   ///< 存储键名（小写连字符）。
    ui::PaletteSpec palette; ///< 该主题的 16 基本色、默认前景/背景与光标色。
    std::optional<ChromeOverride> chrome;
};

/// @brief 全部预置主题（`SPEC.FEAT.PREF.01` 的「内置 ≥8 套」）。
[[nodiscard]] auto builtin_themes() noexcept -> std::span<const BuiltinTheme>;

/// @brief 按存储键名查预置主题。
/// @param name 主题名；大小写敏感（名字即存储键，显示文案另有词条表）。
/// @return 命中的主题；未收录的名字返回 `nullptr`，由设置面板据此提示，不做模糊匹配。
[[nodiscard]] auto find_builtin_theme(std::string_view name) noexcept -> const BuiltinTheme *;

/// @brief 取主题色值，未收录的名字回落缺省主题。
///
/// 回落而不是返回全零：配置里的主题名可能来自更高版本或手工编辑，色值缺失会让整屏
/// 变成黑底黑字——比「主题不是用户点名的那套」更难排查。「名字无效」本身由调用方经
/// `find_builtin_theme` 单独取得，不经本函数。
/// @param name 主题名。
/// @return 该主题的调色板，未收录时为 `kDefaultThemeName` 的那套。
[[nodiscard]] auto theme_palette(std::string_view name) noexcept -> ui::PaletteSpec;

}  // namespace borealis::config
