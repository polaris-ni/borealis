/// 测试类型: unit
/// 目标单元: src/ui/settings_panel.h + src/ui/settings_panel.cpp（chrome 双色板与活动档）
/// 测试说明: `SPEC.FEAT.PREF.01` 亮/暗 chrome 的纯逻辑腿（`codespec/UI_CHROME.draft.md`）：
///           三档选择 × 系统读数的解析表（`system` 不表态回暗、域外值同 `system`）、
///           两套色板逐 token 不同（防「亮档漏改某一项」）、正文/次要文本/语义三色/accent 的
///           WCAG AA 对比度下界与 `text_faint` 的 3.0 弱化下界（稿 §2.1 的 `dim`，判据见
///           `codespec/UI_CHROME.draft.md` §1），以及活动档切换后 `settings_chrome()` 与 `settings_chrome_theme()`
///           同步且 `on_primary` 按档取 `window_bg` / `card_bg`（稿 D6）。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "aurora/core/color.h"
#include "aurora/theming/color_scheme.h"
#include "aurora/theming/theme.h"
#include "framework/aurora_test.h"

#include "../../src/ui/settings_panel.h"

namespace borealis::test_cases::utest_chrome_palette {

namespace {

namespace ui = borealis::ui;
using aurora::Color;
using aurora::ColorScheme;
using ui::ChromeScheme;
using ui::SettingsChrome;

[[nodiscard]] auto same(const Color &lhs, const Color &rhs) -> bool {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

/// @brief WCAG 2.1 的相对亮度：sRGB 通道先去 gamma 再按视见权重加权。
[[nodiscard]] auto luminance(const Color &color) -> double {
    const auto linear = [](std::uint8_t channel) -> double {
        const double value = static_cast<double>(channel) / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
}

[[nodiscard]] auto contrast(const Color &foreground, const Color &background) -> double {
    const double first = luminance(foreground);
    const double second = luminance(background);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

/// @brief 一档底色在「`control_bg` → `card_line`」这一段抬升里的位置（0＝与 `control_bg` 同档，1＝与描边同档）。
///
/// 两档的抬升方向是反的（暗档越亮越抬起、亮档越暗越抬起），但这一式子是**带符号**的差商，两档各自
/// 分子分母同号 ⇒ 比值天然同向，不必按档交换基准。`sel` 的亮档值就是按暗档那一档 0.7197 反推出来的
/// （亮档 `#DBDCE5` 实测 0.7200，裁决 7.196②）。
[[nodiscard]] auto elevation_slot(const Color &lower, const Color &reference, const Color &upper) -> double {
    const double base = luminance(reference);
    const double span = luminance(upper) - base;
    return span == 0.0 ? 0.0 : (luminance(lower) - base) / span;
}

}  // namespace

AURORA_TEST_CASE(the_three_choices_resolve_against_the_os_reading) {
    struct Row {
        std::string_view choice;
        ColorScheme os;
        ChromeScheme want;
    };
    constexpr Row kRows[] = {
        {"system", ColorScheme::Light, ChromeScheme::Light},
        {"system", ColorScheme::Dark, ChromeScheme::Dark},
        // 「暗色优先」的兜底：系统不表态时不跟着框架的「Unspecified ≡ 明色」走（稿 D1）。
        {"system", ColorScheme::Unspecified, ChromeScheme::Dark},
        {"dark", ColorScheme::Light, ChromeScheme::Dark},
        {"dark", ColorScheme::Unspecified, ChromeScheme::Dark},
        {"light", ColorScheme::Dark, ChromeScheme::Light},
        {"light", ColorScheme::Light, ChromeScheme::Light},
        // 缺键（空串）与域外值都按 `system` 处置（稿 D9）。
        {"", ColorScheme::Unspecified, ChromeScheme::Dark},
        {"purple", ColorScheme::Light, ChromeScheme::Light},
    };
    for (const Row &row : kRows) {
        AURORA_TEST_CHECK_MSG(ui::resolve_chrome_scheme(row.choice, row.os) == row.want,
                              std::string{row.choice});
    }
}

AURORA_TEST_CASE(the_two_palettes_differ_in_every_token) {
    // 逐 token 比而不是抽查：亮档漏改一项时，界面会在浅底上留一块深色，而对比度与解析表都抓不到。
    const SettingsChrome dark = ui::chrome_palette(ChromeScheme::Dark);
    const SettingsChrome light = ui::chrome_palette(ChromeScheme::Light);
    AURORA_TEST_CHECK_FALSE(same(dark.window_bg, light.window_bg));
    AURORA_TEST_CHECK_FALSE(same(dark.card_bg, light.card_bg));
    AURORA_TEST_CHECK_FALSE(same(dark.nav_bg, light.nav_bg));
    AURORA_TEST_CHECK_FALSE(same(dark.card_line, light.card_line));
    AURORA_TEST_CHECK_FALSE(same(dark.control_bg, light.control_bg));
    AURORA_TEST_CHECK_FALSE(same(dark.sel, light.sel));
    AURORA_TEST_CHECK_FALSE(same(dark.text, light.text));
    AURORA_TEST_CHECK_FALSE(same(dark.text_dim, light.text_dim));
    AURORA_TEST_CHECK_FALSE(same(dark.text_faint, light.text_faint));
    AURORA_TEST_CHECK_FALSE(same(dark.accent, light.accent));
    AURORA_TEST_CHECK_FALSE(same(dark.scrim, light.scrim));
    AURORA_TEST_CHECK_FALSE(same(dark.danger, light.danger));
    AURORA_TEST_CHECK_FALSE(same(dark.success, light.success));
    AURORA_TEST_CHECK_FALSE(same(dark.warning, light.warning));
}

AURORA_TEST_CASE(the_table_pins_the_sensitive_values) {
    // 只钉几处「设计稿点过名」的值：暗档四枚取自 `UI_OVERVIEW.draft.md` §2.1（`win` / `mut` / `dim` / `yellow`），
    // 亮档 accent 与两档窗口底仍按 `UI_CHROME.draft.md` §1。
    const SettingsChrome dark = ui::chrome_palette(ChromeScheme::Dark);
    const SettingsChrome light = ui::chrome_palette(ChromeScheme::Light);
    AURORA_TEST_CHECK_TRUE(same(dark.text_dim, Color{0x8B, 0x93, 0xA7}));
    AURORA_TEST_CHECK_TRUE(same(dark.text_faint, Color{0x62, 0x72, 0x8A}));
    AURORA_TEST_CHECK_TRUE(same(dark.warning, Color{0xF1, 0xFA, 0x8C}));
    AURORA_TEST_CHECK_TRUE(same(light.accent, Color{0x7C, 0x3A, 0xED}));
    AURORA_TEST_CHECK_TRUE(same(dark.window_bg, Color{0x1E, 0x1F, 0x29}));
    AURORA_TEST_CHECK_TRUE(same(light.window_bg, Color{0xF4, 0xF4, 0xF7}));
    // `sel`（第 14 枚，裁决 7.196）：暗档是 §2.1 直接给的那一格，亮档按同构位反推——两枚都得钉住，
    // 否则「亮档改一档浅色」这种漂移只会在真机上以「hover 几乎看不出来」的形式露出来。
    AURORA_TEST_CHECK_TRUE(same(dark.sel, Color{0x44, 0x47, 0x5A}));
    AURORA_TEST_CHECK_TRUE(same(light.sel, Color{0xDB, 0xDC, 0xE5}));
}

AURORA_TEST_CASE(the_sel_tier_sits_at_the_same_ladder_slot_in_both_palettes) {
    // `sel` 的落位判据（裁决 7.196②）：它必须**确实**落在 `control_bg` 与 `card_line` 之间——压在
    // `control_bg` 上则 hover 与活动分不开，越过 `card_line` 则 hover 那一格比描边还抢眼。两档还须落在
    // 同一段抬升的同一位（暗档实测 0.7197 ⇒ 亮档取 `#DBDCE5` 得 0.7200），否则「同构推导」只是句口号。
    const SettingsChrome dark = ui::chrome_palette(ChromeScheme::Dark);
    const SettingsChrome light = ui::chrome_palette(ChromeScheme::Light);
    for (const SettingsChrome &palette : {dark, light}) {
        const double slot = elevation_slot(palette.sel, palette.control_bg, palette.card_line);
        AURORA_TEST_CHECK_MSG(slot > 0.0 && slot < 1.0, "sel leaves the control_bg..card_line segment");
    }
    const double dark_slot = elevation_slot(dark.sel, dark.control_bg, dark.card_line);
    const double light_slot = elevation_slot(light.sel, light.control_bg, light.card_line);
    AURORA_TEST_CHECK_MSG(std::abs(dark_slot - light_slot) <= 0.05, "the two tiers sit at different ladder slots");

    // 名称落在这档上的两条：`text` ≥4.5（hover/活动那一档必须读得出），而暗档 `text_dim` 只有 2.98——
    // 这一句钉的是「不许把 `text_dim` 当成 `sel` 底上的正文色」，标签条 hover 提亮名称那条口径的根。
    for (const ChromeScheme scheme : {ChromeScheme::Dark, ChromeScheme::Light}) {
        const SettingsChrome palette = ui::chrome_palette(scheme);
        AURORA_TEST_CHECK_MSG(contrast(palette.text, palette.sel) >= 4.5, "text on sel");
    }
    AURORA_TEST_CHECK_MSG(contrast(dark.text_dim, dark.sel) < 3.0, "dark text_dim on sel is not a body tone");
    // `accent` 作前景（✕ 在 hover 档取 `accent`）走非正文下界 3.0。
    for (const ChromeScheme scheme : {ChromeScheme::Dark, ChromeScheme::Light}) {
        const SettingsChrome palette = ui::chrome_palette(scheme);
        AURORA_TEST_CHECK_MSG(contrast(palette.accent, palette.sel) >= 3.0, "accent on sel");
    }
}

AURORA_TEST_CASE(both_palettes_meet_wcag_aa_on_the_card_surface) {
    // 判据见稿 §1：正文 / 次要文本 / 语义三色 / accent 对卡片底都 ≥ 4.5；`card_line` 是装饰描边不在此列。
    for (const ChromeScheme scheme : {ChromeScheme::Dark, ChromeScheme::Light}) {
        const SettingsChrome palette = ui::chrome_palette(scheme);
        AURORA_TEST_CHECK_MSG(contrast(palette.text, palette.card_bg) >= 4.5, "text");
        AURORA_TEST_CHECK_MSG(contrast(palette.text_dim, palette.card_bg) >= 4.5, "text_dim");
        AURORA_TEST_CHECK_MSG(contrast(palette.accent, palette.card_bg) >= 4.5, "accent");
        AURORA_TEST_CHECK_MSG(contrast(palette.danger, palette.card_bg) >= 4.5, "danger");
        AURORA_TEST_CHECK_MSG(contrast(palette.success, palette.card_bg) >= 4.5, "success");
        AURORA_TEST_CHECK_MSG(contrast(palette.warning, palette.card_bg) >= 4.5, "warning");
    }
}

AURORA_TEST_CASE(the_faint_tone_sits_between_the_decorative_floor_and_text_dim) {
    // `text_faint` 的值是稿 §2.1 的 `dim`，不是本仓自算的数，所以它天然低于正文级 AA 下界——本条
    // 因此不要求 4.5，要求的是「还能读」与「确实更弱」：3.0 是 WCAG 对非正文前景的下界，而它必须比
    // `text_dim` 弱，否则「弱化档」在两档里的某一档成了强调，状态栏的层级就倒过来。
    for (const ChromeScheme scheme : {ChromeScheme::Dark, ChromeScheme::Light}) {
        const SettingsChrome palette = ui::chrome_palette(scheme);
        const double faint = contrast(palette.text_faint, palette.card_bg);
        AURORA_TEST_CHECK_MSG(faint >= 3.0, "text_faint floor");
        AURORA_TEST_CHECK_MSG(faint < contrast(palette.text_dim, palette.card_bg), "text_faint < text_dim");
        // 卡片底之外还有一块真正的落笔面是窗口底（状态栏按稿画在 `card_bg` 上，但同一档前景将来
        // 若上了窗口底不能悄悄低于下界）。
        AURORA_TEST_CHECK_MSG(contrast(palette.text_faint, palette.window_bg) >= 3.0, "text_faint on window");
    }
}

AURORA_TEST_CASE(setting_the_scheme_moves_every_reader) {
    // 活动档是进程级状态：析构前必须回暗，否则同一进程里后续套件读到的是上一条用例留下的档位。
    ui::set_chrome_scheme(ChromeScheme::Light);
    AURORA_TEST_CHECK_EQ(ui::chrome_scheme(), ChromeScheme::Light);
    const SettingsChrome light = ui::chrome_palette(ChromeScheme::Light);
    AURORA_TEST_CHECK_TRUE(same(ui::settings_chrome().window_bg, light.window_bg));
    const aurora::Theme light_theme = ui::settings_chrome_theme();
    AURORA_TEST_CHECK_EQ(light_theme.scheme, ColorScheme::Light);
    AURORA_TEST_CHECK_TRUE(same(light_theme.roles.on_primary, light.card_bg));
    AURORA_TEST_CHECK_TRUE(same(light_theme.roles.primary, light.accent));
    // 显式取档不读活动档：装配层建 `ThemeScope` 时按解析结果取，不依赖调用次序。
    AURORA_TEST_CHECK_EQ(ui::chrome_theme(ChromeScheme::Dark).scheme, ColorScheme::Dark);

    ui::set_chrome_scheme(ChromeScheme::Dark);
    AURORA_TEST_CHECK_EQ(ui::chrome_scheme(), ChromeScheme::Dark);
    const SettingsChrome dark = ui::chrome_palette(ChromeScheme::Dark);
    AURORA_TEST_CHECK_TRUE(same(ui::settings_chrome().window_bg, dark.window_bg));
    const aurora::Theme dark_theme = ui::settings_chrome_theme();
    AURORA_TEST_CHECK_EQ(dark_theme.scheme, ColorScheme::Dark);
    AURORA_TEST_CHECK_TRUE(same(dark_theme.roles.on_primary, dark.window_bg));
}

}  // namespace borealis::test_cases::utest_chrome_palette
