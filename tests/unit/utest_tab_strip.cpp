/// 测试类型: unit
/// 目标单元: include/borealis/ui/tab_strip.h，src/ui/tab_strip.cpp
/// 测试说明: `SPEC.FEAT.WS.01` 的「新建 / 关闭 / 切换 / 重排 / 重命名」里与绘制无关的那半
///           （裁决 7.43）：空表与选中位、末位不可关、选中交接按**移除前**的次序、
///           循环切换在两端都绕、重排的下标基准是「其余标签」（往上拖与往下拖差一格的那处
///           经典错位）、三个名字来源按优先级折且**空串即让位**，以及配置侧的枚举别名同型。

#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "borealis/config/settings.h"
#include "borealis/ui/tab_strip.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_tab_strip {

using borealis::config::AppearanceSettings;
using borealis::config::TabNamePriority;
using borealis::ui::resolve_tab_name;
using borealis::ui::Tab;
using borealis::ui::TabNames;
using borealis::ui::TabStrip;

namespace {

constexpr borealis::ui::TabId kA = 10;
constexpr borealis::ui::TabId kB = 20;
constexpr borealis::ui::TabId kC = 30;

/// @brief 三个来源按位填进 `TabNames`（空指针字面量即「该来源未设置」）。
[[nodiscard]] auto make_names(const char32_t *def, const char32_t *osc, const char32_t *manual) -> TabNames {
    return TabNames{.default_name = def, .osc_title = osc, .manual_name = manual};
}

/// @brief 表里的身份次序（判据的核心就是次序，逐格比整个 `Tab` 会让失败信息淹没在名字里）。
[[nodiscard]] auto ids(const TabStrip &strip) -> std::vector<borealis::ui::TabId> {
    auto out = std::vector<borealis::ui::TabId>{};
    for (const auto &tab : strip.tabs()) {
        out.push_back(tab.id);
    }
    return out;
}

/// @brief 三格起步的表（A、B、C），并可选中指定的一格。
[[nodiscard]] auto make_three(borealis::ui::TabId selected) -> TabStrip {
    auto strip = TabStrip{};
    strip.add(kA, U"a");
    strip.add(kB, U"b");
    strip.add(kC, U"c");
    strip.select(selected);
    return strip;
}

}  // namespace

AURORA_TEST_CASE(an_empty_strip_has_no_selection) {
    const auto strip = TabStrip{};
    AURORA_TEST_CHECK_EQ(strip.count(), std::size_t{0});
    AURORA_TEST_CHECK_FALSE(strip.selected().has_value());
    AURORA_TEST_CHECK_TRUE(strip.tabs().empty());
    AURORA_TEST_CHECK_FALSE(strip.has_tab(kA));
}

AURORA_TEST_CASE(adding_a_tab_appends_it_and_selects_it) {
    auto strip = TabStrip{};
    AURORA_TEST_REQUIRE_TRUE(strip.add(kA, U"powershell"));
    AURORA_TEST_REQUIRE_TRUE(strip.add(kB, U"ssh"));

    AURORA_TEST_CHECK(ids(strip) == std::vector{kA, kB});
    // 新建即切换：不等调用方补一句 select，否则两种手感会随哪一处忘了补而分叉。
    AURORA_TEST_CHECK(strip.selected() == std::optional{kB});
    AURORA_TEST_CHECK(strip.index_of(kB) == std::optional<std::size_t>{1});
}

AURORA_TEST_CASE(add_rejects_a_reused_identity) {
    auto strip = TabStrip{};
    AURORA_TEST_REQUIRE_TRUE(strip.add(kA, U"a"));
    AURORA_TEST_CHECK_FALSE(strip.add(kA, U"other"));
    AURORA_TEST_CHECK_EQ(strip.count(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(ids(strip)[0], kA);
    AURORA_TEST_CHECK(strip.tabs()[0].names.default_name == std::u32string{U"a"});  // 名字没被第二次的覆盖
}

AURORA_TEST_CASE(closing_the_selected_middle_tab_selects_the_next_one) {
    auto strip = make_three(kB);
    AURORA_TEST_REQUIRE_TRUE(strip.close(kB));
    AURORA_TEST_CHECK(strip.selected() == std::optional{kC});
    AURORA_TEST_CHECK(ids(strip) == std::vector{kA, kC});
}

AURORA_TEST_CASE(closing_the_selected_last_tab_selects_the_previous_one) {
    auto strip = make_three(kC);
    AURORA_TEST_REQUIRE_TRUE(strip.close(kC));
    AURORA_TEST_CHECK(strip.selected() == std::optional{kB});
}

AURORA_TEST_CASE(closing_an_earlier_unselected_tab_keeps_the_selected_identity) {
    auto strip = make_three(kC);
    AURORA_TEST_REQUIRE_TRUE(strip.close(kA));
    // 选中的还是 C，只是下标因前面少一格而左移——绘制侧按下标铺栏位，故这里必须跟着挪。
    AURORA_TEST_CHECK(strip.selected() == std::optional{kC});
    AURORA_TEST_CHECK(strip.index_of(kC) == std::optional<std::size_t>{1});
}

AURORA_TEST_CASE(closing_a_later_unselected_tab_leaves_the_selection_alone) {
    // 与上一例成对：只有**在选中格之前**的移除才会挪选中位，否则首位的选中会退到表外。
    auto strip = make_three(kA);
    AURORA_TEST_REQUIRE_TRUE(strip.close(kC));
    AURORA_TEST_CHECK(strip.selected() == std::optional{kA});
    AURORA_TEST_CHECK(ids(strip) == std::vector{kA, kB});
}

AURORA_TEST_CASE(the_last_tab_in_the_strip_is_not_closable_here) {
    auto strip = TabStrip{};
    strip.add(kA, U"a");
    AURORA_TEST_CHECK_FALSE(strip.close(kA));  // 关掉最后一个标签是关窗口，属 SPEC.FEAT.WS.03。
    AURORA_TEST_CHECK_EQ(strip.count(), std::size_t{1});
    AURORA_TEST_CHECK(strip.selected() == std::optional{kA});
}

AURORA_TEST_CASE(close_rejects_an_absent_identity) {
    auto strip = make_three(kA);
    AURORA_TEST_CHECK_FALSE(strip.close(999));
    AURORA_TEST_CHECK_EQ(strip.count(), std::size_t{3});
}

AURORA_TEST_CASE(select_targets_a_tab_by_identity_and_rejects_absent) {
    auto strip = make_three(kC);
    AURORA_TEST_REQUIRE_TRUE(strip.select(kA));
    AURORA_TEST_CHECK(strip.selected() == std::optional{kA});
    AURORA_TEST_CHECK_FALSE(strip.select(999));
    AURORA_TEST_CHECK(strip.selected() == std::optional{kA});  // 不存在的目标不改动选中
}

AURORA_TEST_CASE(relative_selection_wraps_at_both_ends) {
    auto forward = make_three(kC);
    AURORA_TEST_REQUIRE_TRUE(forward.select_relative(1));
    AURORA_TEST_CHECK(forward.selected() == std::optional{kA});  // 末位往前绕到首位

    auto backward = make_three(kA);
    AURORA_TEST_REQUIRE_TRUE(backward.select_relative(-1));
    AURORA_TEST_CHECK(backward.selected() == std::optional{kC});  // 首位往后绕到末位
}

AURORA_TEST_CASE(relative_selection_takes_multiple_steps_and_full_laps) {
    auto strip = make_three(kA);
    AURORA_TEST_REQUIRE_TRUE(strip.select_relative(4));  // 一整圈再加一格
    AURORA_TEST_CHECK(strip.selected() == std::optional{kB});
    AURORA_TEST_REQUIRE_TRUE(strip.select_relative(-5));  // 反向整圈加两格，含跨零
    AURORA_TEST_CHECK(strip.selected() == std::optional{kC});
    AURORA_TEST_CHECK_FALSE(strip.select_relative(0));
    AURORA_TEST_CHECK(strip.selected() == std::optional{kC});
}

AURORA_TEST_CASE(relative_selection_on_an_empty_strip_does_nothing) {
    auto strip = TabStrip{};
    AURORA_TEST_CHECK_FALSE(strip.select_relative(1));
    AURORA_TEST_CHECK_FALSE(strip.selected().has_value());
}

AURORA_TEST_CASE(a_single_tab_strip_selects_itself_but_moves_nowhere) {
    // 单格时「切换」这件事没有可去之处：返回值是「是否发生了切换」，原地即 false，选中位不动。
    auto strip = TabStrip{};
    strip.add(kA, U"a");
    AURORA_TEST_REQUIRE_TRUE(strip.select(kA));
    AURORA_TEST_CHECK_FALSE(strip.select_relative(1));
    AURORA_TEST_CHECK_FALSE(strip.select_relative(-1));
    AURORA_TEST_CHECK(strip.selected() == std::optional{kA});
}

AURORA_TEST_CASE(reorder_moves_a_tab_to_the_index_among_the_others) {
    auto strip = make_three(kA);
    // 基准是「移除被拖那格之后」的次序：把 A 交给其余两格 [B, C] 的第 1 格，即落在 B 与 C 之间。
    AURORA_TEST_REQUIRE_TRUE(strip.move(kA, 1));
    AURORA_TEST_CHECK(ids(strip) == std::vector{kB, kA, kC});
}

AURORA_TEST_CASE(reorder_uses_the_same_basis_in_both_drag_directions) {
    // 与上一例同一下标、相反的方向：把末位 C 交给 [A, B] 的第 1 格，落在 A 与 B 之间。
    // 若基准取「原表下标」，这两个方向就会差一格（往下拖看似成功、往上拖错开一格）。
    auto strip = make_three(kA);
    AURORA_TEST_REQUIRE_TRUE(strip.move(kC, 1));
    AURORA_TEST_CHECK(ids(strip) == std::vector{kA, kC, kB});
}

AURORA_TEST_CASE(reorder_to_the_ends_and_in_place) {
    auto to_front = make_three(kA);
    AURORA_TEST_REQUIRE_TRUE(to_front.move(kC, 0));
    AURORA_TEST_CHECK(ids(to_front) == std::vector{kC, kA, kB});

    auto to_back = make_three(kA);
    AURORA_TEST_REQUIRE_TRUE(to_back.move(kA, 2));  // count-1 即落到表尾
    AURORA_TEST_CHECK(ids(to_back) == std::vector{kB, kC, kA});

    auto in_place = make_three(kA);
    AURORA_TEST_REQUIRE_TRUE(in_place.move(kB, 1));  // 原地落点：算完成，表不变
    AURORA_TEST_CHECK(ids(in_place) == std::vector{kA, kB, kC});
}

AURORA_TEST_CASE(reorder_keeps_the_selection_on_the_same_tab) {
    auto strip = make_three(kB);
    AURORA_TEST_REQUIRE_TRUE(strip.move(kA, 2));  // 被移动的不是选中格，但它的下标因位移而变
    AURORA_TEST_CHECK(ids(strip) == std::vector{kB, kC, kA});
    AURORA_TEST_CHECK(strip.selected() == std::optional{kB});
    AURORA_TEST_CHECK(strip.index_of(kB) == std::optional<std::size_t>{0});
}

AURORA_TEST_CASE(reorder_rejects_out_of_range_and_absent) {
    auto strip = make_three(kA);
    AURORA_TEST_CHECK_FALSE(strip.move(kA, 3));  // 上界是 count-1
    AURORA_TEST_CHECK_FALSE(strip.move(999, 0));
    AURORA_TEST_CHECK(ids(strip) == std::vector{kA, kB, kC});
}

AURORA_TEST_CASE(manual_rename_wins_over_the_osc_title_by_default) {
    const auto resolved = resolve_tab_name(make_names(U"profile", U"osc", U"manual"), TabNamePriority::ManualWins);
    AURORA_TEST_CHECK(resolved == std::u32string{U"manual"});
    AURORA_TEST_CHECK_EQ(AppearanceSettings{}.tab_name_priority, TabNamePriority::ManualWins);  // 缺省档
}

AURORA_TEST_CASE(the_osc_wins_policy_puts_the_title_first) {
    const auto resolved = resolve_tab_name(make_names(U"profile", U"osc", U"manual"), TabNamePriority::OscWins);
    AURORA_TEST_CHECK(resolved == std::u32string{U"osc"});
}

AURORA_TEST_CASE(an_empty_source_yields_to_the_next_one) {
    // 空串是「该来源未设置」而不是「名字就是空」，故撤销重命名与 `OSC` 设回空是同一句让位。
    AURORA_TEST_CHECK(resolve_tab_name(make_names(U"profile", U"osc", U""), TabNamePriority::ManualWins) ==
                      std::u32string{U"osc"});
    AURORA_TEST_CHECK(resolve_tab_name(make_names(U"profile", U"", U"manual"), TabNamePriority::OscWins) ==
                      std::u32string{U"manual"});
    AURORA_TEST_CHECK(resolve_tab_name(make_names(U"profile", U"", U""), TabNamePriority::OscWins) ==
                      std::u32string{U"profile"});
    AURORA_TEST_CHECK(resolve_tab_name(make_names(U"", U"", U""), TabNamePriority::ManualWins) == std::u32string{});
}

AURORA_TEST_CASE(rename_and_osc_title_write_through_the_strip) {
    auto strip = TabStrip{};
    strip.add(kA, U"profile");
    AURORA_TEST_REQUIRE_TRUE(strip.set_osc_title(kA, U"title"));
    AURORA_TEST_CHECK(strip.tabs()[0].names.osc_title == std::u32string{U"title"});
    AURORA_TEST_REQUIRE_TRUE(strip.rename(kA, U"mine"));
    AURORA_TEST_CHECK(resolve_tab_name(strip.tabs()[0].names, TabNamePriority::ManualWins) ==
                      std::u32string{U"mine"});

    AURORA_TEST_REQUIRE_TRUE(strip.rename(kA, U""));  // 撤销重命名即让位给 `OSC`
    AURORA_TEST_CHECK(resolve_tab_name(strip.tabs()[0].names, TabNamePriority::ManualWins) ==
                      std::u32string{U"title"});
}

AURORA_TEST_CASE(name_writers_reject_an_absent_identity) {
    auto strip = make_three(kA);
    AURORA_TEST_CHECK_FALSE(strip.rename(999, U"x"));
    AURORA_TEST_CHECK_FALSE(strip.set_osc_title(999, U"x"));
    for (const auto &tab : strip.tabs()) {
        AURORA_TEST_CHECK_TRUE(tab.names.manual_name.empty());
        AURORA_TEST_CHECK_TRUE(tab.names.osc_title.empty());
    }
}

AURORA_TEST_CASE(config_field_is_an_alias_of_the_priority_enum_not_a_second_table) {
    const auto appearance = AppearanceSettings{};
    AURORA_TEST_CHECK((std::is_same_v<decltype(appearance.tab_name_priority), borealis::ui::TabNamePriority>));
}

}  // namespace borealis::test_cases::utest_tab_strip
