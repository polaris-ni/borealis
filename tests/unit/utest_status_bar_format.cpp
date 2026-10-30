/// 测试类型: unit
/// 目标单元: src/ui/status_bar_format.{h,cpp}
/// 测试说明: `codespec/UI_OVERVIEW.draft.md` §4 的 N3（并入裁决 7.25）那一句的可执行形态——「缺省全开；
///           窗口不足以容纳已开条目时按注册次序从右向左省略，不做字号压缩」。本件守的四条：
///           ① 上屏集合永远是注册次序的**前缀**而不是任意子集（窄的中间格不得替宽格跳位）；
///           ② 「放得下」算的是文本 + 该格前缀 + 与左邻的格间距 + 右端固定件与右内缩，一个都不能漏；
///           ③ 右端固定件与数据格相争时**锚点先倒**，且只在「让出锚点就能多上一格」时倒；
///           ④ 注册次序与设置页那十枚开关的行序同源（双向核对，次序漂了就转红）。
///           外加两把纯式子：千分位分组与字号显示形（稿屏 1 逐字画的 `10,000` 与 `14 pt`）。
///           宽度由调用方测好交进来，本件不调字体，因此全例无头、无 GUI（AGENTS §4.4 第 20 条）。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "framework/aurora_test.h"

#include "borealis/ui/settings_catalog.h"

#include "ui/status_bar_format.h"

namespace borealis::test_cases::utest_status_bar_format {

namespace {

namespace ui = borealis::ui;

using ui::StatusBarGeometry;
using ui::StatusBarSlotMeasure;

/// @brief 造一格：文本宽与前缀各自给定，身份用可读的 leaf 名。
[[nodiscard]] auto slot(std::string_view id, double text_dp, double prefix_dp = 0.0)
    -> StatusBarSlotMeasure {
    return StatusBarSlotMeasure{.id = id, .text_width_dp = text_dp, .prefix_dp = prefix_dp};
}

/// @brief 「数据格实占 content_dp」时那条状态栏应有的整条宽度（缺省几何：左右内缩各 16、无右端固定件）。
[[nodiscard]] auto strip_for(double content_dp) -> double {
    return 16.0 + content_dp + 16.0;
}

/// @brief 取一份测量表：count 格各宽 text_dp，身份取自一张静态表（串的生命周期要活过比较）。
[[nodiscard]] auto slots(std::size_t count, double text_dp) -> std::vector<StatusBarSlotMeasure> {
    static const std::vector<std::string_view> kIds{"a", "b", "c", "d", "e"};
    std::vector<StatusBarSlotMeasure> out;
    for (std::size_t index = 0; index < count; ++index) {
        out.push_back(slot(kIds[index % kIds.size()], text_dp));
    }
    return out;
}

}  // namespace

AURORA_TEST_CASE(a_wide_strip_shows_every_slot_in_registration_order) {
    // 三格各 50、两枚格间距各 20 ⇒ 数据格实占 190。整条给足 190+16+16，就是全上屏。
    const auto plan = ui::plan_status_bar(slots(3, 50.0), strip_for(50.0 * 3.0 + 20.0 * 2.0));
    AURORA_TEST_CHECK_EQ(plan.shown.size(), 3U);
    AURORA_TEST_CHECK_EQ(plan.omitted.size(), 0U);
    AURORA_TEST_CHECK_MSG(plan.content_width_dp == 16.0 + 50.0 * 3.0 + 20.0 * 2.0,
                          "content width counts gaps between slots, not after the last one");
    AURORA_TEST_CHECK_FALSE(plan.right_anchor_dropped);
}

AURORA_TEST_CASE(the_first_slot_that_does_not_fit_ends_the_prefix) {
    // 同一张表只给到「两格半」的宽度：第三格整个不放，而不是截短、也不是压字号（N3 那句）。
    const auto plan = ui::plan_status_bar(slots(3, 50.0), strip_for(130.0));
    AURORA_TEST_CHECK_EQ(plan.shown.size(), 2U);
    AURORA_TEST_CHECK_EQ(plan.omitted.size(), 1U);
    AURORA_TEST_CHECK_MSG(plan.content_width_dp == 16.0 + 50.0 * 2.0 + 20.0,
                          "the width actually taken stops at the last shown slot");
}

AURORA_TEST_CASE(a_narrow_middle_slot_does_not_jump_ahead_of_a_wide_one) {
    // 判据 ①：前缀而不是子集。第二格宽到放不下，后面那枚窄格**也不许**补位——
    // 否则同一格会随左邻文案的长短忽现忽隐，而状态栏的读数靠的就是「它一直在老位置」。
    const std::vector<StatusBarSlotMeasure> table{slot("a", 40.0), slot("b", 400.0), slot("c", 10.0)};
    const auto plan = ui::plan_status_bar(table, strip_for(100.0));
    AURORA_TEST_CHECK_EQ(plan.shown.size(), 1U);
    AURORA_TEST_CHECK_MSG(plan.shown.front() == "a", "the shown set is the leading run");
    AURORA_TEST_CHECK_EQ(plan.omitted.size(), 2U);
    AURORA_TEST_CHECK_MSG(plan.omitted[0] == "b" && plan.omitted[1] == "c",
                          "omitted keeps registration order too (the context menu reads this list)");
}

AURORA_TEST_CASE(prefix_and_gap_are_part_of_what_must_fit) {
    // 判据 ②：文本自身放得下，加上前缀或格间距就放不下——那一格不得上屏。
    const double available = strip_for(100.0);
    const auto with_prefix = ui::plan_status_bar({slot("a", 100.0, 8.0)}, available);
    AURORA_TEST_CHECK_EQ(with_prefix.shown.size(), 0U);
    // 首格没有左邻，因此不吃格间距：同一份文本在首格位置正好放得下。
    const auto first_slot = ui::plan_status_bar({slot("a", 100.0)}, available);
    AURORA_TEST_CHECK_EQ(first_slot.shown.size(), 1U);
    // 第二格吃一次格间距 20，于是同样的文本宽放不下。
    const auto second_slot = ui::plan_status_bar({slot("a", 20.0), slot("b", 100.0)}, available);
    AURORA_TEST_CHECK_EQ(second_slot.shown.size(), 1U);
    AURORA_TEST_CHECK_EQ(second_slot.omitted.size(), 1U);
}

AURORA_TEST_CASE(the_right_anchor_is_reserved_before_the_data_slots_are_placed) {
    // 判据 ② 的右半边：锚点占的那一段宽度不参加数据格的计算，也不计进实占宽度。
    const double available = strip_for(60.0 * 2.0 + 20.0);  // 无锚点时两格刚好放下
    const auto with_anchor = ui::plan_status_bar(slots(2, 60.0), available,
                                                 StatusBarGeometry{.reserved_right_dp = 30.0});
    AURORA_TEST_CHECK_EQ(with_anchor.shown.size(), 1U);
    AURORA_TEST_CHECK_FALSE(with_anchor.right_anchor_dropped);  // 让的是数据格，不是锚点
    AURORA_TEST_CHECK_MSG(with_anchor.content_width_dp == 16.0 + 60.0,
                          "the anchor's reservation is not part of the content width");
}

AURORA_TEST_CASE(the_anchor_yields_only_when_no_data_slot_would_otherwise_fit) {
    // 判据 ③：锚点与数据格相争时，只有在「让出锚点就能上屏至少一格」时锚点才倒。
    const double available = strip_for(100.0);
    const std::vector<StatusBarSlotMeasure> table{slot("a", 100.0)};
    const auto yielded = ui::plan_status_bar(table, available,
                                             StatusBarGeometry{.reserved_right_dp = 20.0});
    AURORA_TEST_CHECK_TRUE(yielded.right_anchor_dropped);
    AURORA_TEST_CHECK_EQ(yielded.shown.size(), 1U);
    // 连让出锚点都放不下时，不许「既丢了锚点又空着一条」：那一帧整条空着，锚点也不画。
    const auto hopeless = ui::plan_status_bar({slot("a", 400.0)}, available,
                                              StatusBarGeometry{.reserved_right_dp = 20.0});
    AURORA_TEST_CHECK_FALSE(hopeless.right_anchor_dropped);
    AURORA_TEST_CHECK_EQ(hopeless.shown.size(), 0U);
    AURORA_TEST_CHECK_EQ(hopeless.content_width_dp, 0.0);
}

AURORA_TEST_CASE(empty_slot_table_degrades_to_an_empty_plan) {
    // 装配层把「配置关掉的花名册」与「文本为空的格」都滤掉，全滤光就是空表——本件不得因此崩或造出一格。
    const auto plan = ui::plan_status_bar({}, 1000.0, StatusBarGeometry{.reserved_right_dp = 80.0});
    AURORA_TEST_CHECK_EQ(plan.shown.size(), 0U);
    AURORA_TEST_CHECK_EQ(plan.omitted.size(), 0U);
    AURORA_TEST_CHECK_FALSE(plan.right_anchor_dropped);
}

AURORA_TEST_CASE(negative_widths_are_clamped_instead_of_breaking_monotonicity) {
    // 负宽是调用方的测量 bug，但本件的单调性判据不能靠调用方守：负值按 0 用，于是「加一格」
    // 永远让上屏数量不减，而负值若按原样进算式就会让游标倒退、放回本来放不下的一格。
    const std::vector<StatusBarSlotMeasure> table{slot("a", -500.0, -20.0), slot("b", 10.0)};
    const auto plan = ui::plan_status_bar(table, strip_for(30.0));
    AURORA_TEST_CHECK_EQ(plan.shown.size(), 2U);
    AURORA_TEST_CHECK_MSG(plan.content_width_dp == 16.0 + 0.0 + 20.0 + 10.0,
                          "a clamped slot still advances the cursor by its gap");
}

AURORA_TEST_CASE(geometry_is_part_of_the_contract_not_a_hardcoded_number) {
    // 内缩与间距都参与算式：把间距调大就少上一格，把两端内缩归零就多上一格。
    const double available = strip_for(50.0 * 2.0 + 20.0);
    AURORA_TEST_CHECK_EQ(ui::plan_status_bar(slots(2, 50.0), available).shown.size(), 2U);
    const auto wider_gap = ui::plan_status_bar(slots(2, 50.0), available,
                                               StatusBarGeometry{.gap_dp = 60.0});
    AURORA_TEST_CHECK_EQ(wider_gap.shown.size(), 1U);
    const auto no_inset = ui::plan_status_bar(slots(2, 50.0), available,
                                              StatusBarGeometry{.left_inset_dp = 0.0,
                                                                .right_inset_dp = 0.0});
    AURORA_TEST_CHECK_EQ(no_inset.shown.size(), 2U);
    AURORA_TEST_CHECK_MSG(no_inset.content_width_dp == 50.0 * 2.0 + 20.0,
                          "the inset is charged once, not also inside the content width");
}

AURORA_TEST_CASE(the_registration_order_mirrors_the_status_bar_toggles_in_the_catalog) {
    // 判据 ④：次序的两个来源在这里对一次账。设置页那十枚开关的**行序**就是面板排版序，
    // 而本表的次序是省略序——两者分叉的后果是「面板从上往下数第三枚」与「状态栏省略到第三格」
    // 不是同一枚开关，用户没法从设置页推出此刻屏上少的是哪几格。
    const auto &order = ui::status_bar_registration_order();
    static constexpr std::string_view kPrefix = "appearance.status_bar.show_";
    std::vector<std::string_view> catalog_leaves;
    for (const auto &row : ui::settings_catalog()) {
        if (row.key.rfind(kPrefix, 0U) != 0U) {
            continue;
        }
        catalog_leaves.push_back(std::string_view{row.key}.substr(kPrefix.size()));
    }
    AURORA_TEST_CHECK_EQ(order.size(), 10U);
    AURORA_TEST_CHECK_MSG(order.size() == catalog_leaves.size(),
                          "every toggle the panel registers has a place in the order");
    for (std::size_t index = 0; index < order.size() && index < catalog_leaves.size(); ++index) {
        AURORA_TEST_CHECK_MSG(order[index] == catalog_leaves[index],
                              "the omission order and the panel's row order must be one list");
    }
}

AURORA_TEST_CASE(thousands_group_only_where_a_group_boundary_exists) {
    AURORA_TEST_CHECK_EQ(ui::group_thousands(0ULL), std::string{"0"});
    AURORA_TEST_CHECK_EQ(ui::group_thousands(999ULL), std::string{"999"});
    AURORA_TEST_CHECK_EQ(ui::group_thousands(1000ULL), std::string{"1,000"});
    AURORA_TEST_CHECK_EQ(ui::group_thousands(10000ULL), std::string{"10,000"});  // 稿屏 1 那一格的读数
    AURORA_TEST_CHECK_EQ(ui::group_thousands(1000000ULL), std::string{"1,000,000"});
}

AURORA_TEST_CASE(the_font_size_reading_drops_a_zero_fraction) {
    // 稿屏 1 画的是 `14 pt`（无小数），而配置里的字号是 `double`——往返后可能是 14.0000000001。
    AURORA_TEST_CHECK_EQ(ui::format_font_size(14.0), std::string{"14"});
    AURORA_TEST_CHECK_EQ(ui::format_font_size(14.0000000001), std::string{"14"});
    AURORA_TEST_CHECK_EQ(ui::format_font_size(13.5), std::string{"13.5"});
    AURORA_TEST_CHECK_EQ(ui::format_font_size(6.0), std::string{"6"});
    AURORA_TEST_CHECK_EQ(ui::format_font_size(71.9), std::string{"71.9"});
}

}  // namespace borealis::test_cases::utest_status_bar_format
