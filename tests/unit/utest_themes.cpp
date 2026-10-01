/// 测试类型: unit
/// 目标单元: include/borealis/config/themes.h + src/config/themes.cpp
/// 测试说明: 预置配色表的形态（`SPEC.FEAT.PREF.01` 的「内置 ≥8 套」——名字唯一、缺省主题居首、
///           八套彼此不重复）、每套色值可用（前景与背景不得同色、光标色必须落定、chrome 首版
///           一律不覆盖），以及未收录名字回落缺省主题而不是全零（裁决 7.26②）。

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/config/themes.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_themes {

using borealis::config::builtin_themes;
using borealis::config::find_builtin_theme;
using borealis::config::kDefaultThemeName;
using borealis::config::theme_palette;
using borealis::ui::PaletteSpec;

namespace {

/// @brief 名字是否可作存储键：小写字母、数字与连字符（显示文案另有词条表，键名必须稳定）。
auto is_storage_key(std::string_view name) -> bool {
    if (name.empty()) {
        return false;
    }
    for (const char character : name) {
        const bool ok = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
                        character == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

AURORA_TEST_CASE(table_holds_eight_distinct_theme_keys) {
    const auto themes = builtin_themes();
    AURORA_TEST_CHECK_GE(themes.size(), 8U);

    std::vector<std::string> names;
    for (const auto &theme : themes) {
        names.emplace_back(theme.name);
        AURORA_TEST_CHECK_MSG(is_storage_key(theme.name), std::string{theme.name});
    }
    std::ranges::sort(names);
    AURORA_TEST_CHECK_EQ(names.size(), themes.size());
    AURORA_TEST_CHECK_TRUE(std::ranges::adjacent_find(names) == names.end());
}

AURORA_TEST_CASE(default_theme_leads_the_table) {
    const auto &themes = builtin_themes();
    AURORA_TEST_REQUIRE_FALSE(themes.empty());
    AURORA_TEST_CHECK_EQ(themes.front().name, kDefaultThemeName);

    const auto *found = find_builtin_theme(kDefaultThemeName);
    AURORA_TEST_REQUIRE(found != nullptr);
    AURORA_TEST_CHECK_TRUE(found == &themes.front());
}

AURORA_TEST_CASE(every_theme_carries_readable_colors) {
    for (const auto &theme : builtin_themes()) {
        const PaletteSpec &spec = theme.palette;
        const std::string name{theme.name};

        // 前景与背景同色＝整屏不可读；0 与 7 两档同色＝连默认文本都分不出亮暗。
        // 「黑」档与底色相同是各主题的常态（gruvbox / monokai / campbell 皆然），故不作判据。
        AURORA_TEST_CHECK_MSG(spec.default_foreground != spec.default_background, name);
        AURORA_TEST_CHECK_MSG(spec.basic[0] != spec.basic[7], name);

        // 光标色必须落定：主题表是色值唯一来源，「未配」只能由用户显式清空（裁决 7.25③）。
        AURORA_TEST_CHECK_MSG(spec.cursor_color.has_value(), name);

        // 开关不属色值，表里一律留缺省，避免主题表成为开关的第二真值源。
        AURORA_TEST_CHECK_MSG(!spec.bold_is_bright, name);
        AURORA_TEST_CHECK_MSG(!spec.min_contrast_enabled, name);

        // chrome 覆盖是预留字段，首版无消费方（裁决 7.25⑪）。
        AURORA_TEST_CHECK_MSG(!theme.chrome.has_value(), name);
    }
}

AURORA_TEST_CASE(palettes_are_pairwise_distinct) {
    const auto themes = builtin_themes();
    for (std::size_t first = 0; first < themes.size(); ++first) {
        for (std::size_t second = first + 1; second < themes.size(); ++second) {
            const bool same = themes[first].palette.basic == themes[second].palette.basic &&
                              themes[first].palette.default_foreground == themes[second].palette.default_foreground &&
                              themes[first].palette.default_background == themes[second].palette.default_background;
            AURORA_TEST_CHECK_MSG(!same, std::string{themes[first].name} + " vs " + std::string{themes[second].name});
        }
    }
}

AURORA_TEST_CASE(lookup_is_exact_and_unknown_names_fall_back) {
    AURORA_TEST_CHECK_TRUE(find_builtin_theme("Dracula") == nullptr);
    AURORA_TEST_CHECK_TRUE(find_builtin_theme("dracula ") == nullptr);
    AURORA_TEST_CHECK_TRUE(find_builtin_theme("") == nullptr);
    AURORA_TEST_CHECK_TRUE(find_builtin_theme("not-a-theme") == nullptr);

    // 回落随缺省主题而不是全零：全零＝黑底黑字，比「不是用户点名那套」更难排查。
    AURORA_TEST_CHECK_EQ(theme_palette("not-a-theme"), theme_palette(kDefaultThemeName));
    AURORA_TEST_CHECK_NE(theme_palette("not-a-theme").basic, theme_palette("nord").basic);
}

}  // namespace borealis::test_cases::utest_themes
