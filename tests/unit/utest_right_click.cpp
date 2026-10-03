/// 测试类型: unit
/// 目标单元: include/borealis/ui/right_click.h + src/ui/right_click.cpp
/// 测试说明: 右键三态的处置计划（`SPEC.FEAT.INTERACT.03`）：`ContextMenu` 态出菜单且无选区时
///           「复制」置灰而「粘贴」恒可用、`Paste` 与 `CopyOnSelect` 两态不发菜单、
///           `CopyOnSelect` 态无选区回 `None`（空选区复制会覆盖用户剪贴板）、菜单条目的次序即
///           自上而下的次序，以及 `config::RightClickAction` 是 `ui::RightClickAction` 的别名
///           而非第二份取值表、缺省取值仍是菜单态。

#include <type_traits>
#include <vector>

#include "borealis/config/settings.h"
#include "borealis/ui/right_click.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_right_click {

namespace {

using borealis::config::TerminalSettings;
using borealis::ui::MenuCommand;
using borealis::ui::MenuItem;
using borealis::ui::RightClickAction;
using borealis::ui::RightClickIntent;
using borealis::ui::RightClickPlan;
using borealis::ui::plan_right_click;

}  // namespace

AURORA_TEST_CASE(context_menu_with_selection_offers_both_commands_enabled) {
    const RightClickPlan plan = plan_right_click(RightClickAction::ContextMenu, true);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::Menu);
    AURORA_TEST_CHECK(plan.items == std::vector<MenuItem>{{MenuCommand::Copy, true}, {MenuCommand::Paste, true}});
}

AURORA_TEST_CASE(context_menu_without_selection_grays_out_only_the_copy_row) {
    const RightClickPlan plan = plan_right_click(RightClickAction::ContextMenu, false);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::Menu);
    // 置灰而非点击后静默失败（视觉稿 F-b）；粘贴不受选区影响。
    AURORA_TEST_CHECK(plan.items == std::vector<MenuItem>{{MenuCommand::Copy, false}, {MenuCommand::Paste, true}});
}

AURORA_TEST_CASE(direct_paste_action_sends_no_menu) {
    const RightClickPlan plan = plan_right_click(RightClickAction::Paste, true);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::Paste);
    AURORA_TEST_CHECK_TRUE(plan.items.empty());
}

AURORA_TEST_CASE(direct_paste_action_ignores_selection_presence) {
    // 有无选区都粘贴：本态的入参与结果只有意图，选区知识归复制那条腿。
    AURORA_TEST_CHECK(plan_right_click(RightClickAction::Paste, false) ==
                      plan_right_click(RightClickAction::Paste, true));
}

AURORA_TEST_CASE(copy_on_select_action_copies_without_a_menu) {
    const RightClickPlan plan = plan_right_click(RightClickAction::CopyOnSelect, true);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::Copy);
    AURORA_TEST_CHECK_TRUE(plan.items.empty());
}

AURORA_TEST_CASE(copy_on_select_action_with_empty_selection_does_nothing) {
    // 空选区复制出一条空文本会覆盖用户剪贴板里的既有内容，是负收益，故回 None 而不是 Copy。
    const RightClickPlan plan = plan_right_click(RightClickAction::CopyOnSelect, false);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::None);
    AURORA_TEST_CHECK_TRUE(plan.items.empty());
}

AURORA_TEST_CASE(the_three_actions_do_not_share_an_intent) {
    // 三态互不吞并：同一「有选区」入参下三种动作给出三种意图，缺省态才是菜单。
    const RightClickPlan menu = plan_right_click(RightClickAction::ContextMenu, true);
    const RightClickPlan paste = plan_right_click(RightClickAction::Paste, true);
    const RightClickPlan copy = plan_right_click(RightClickAction::CopyOnSelect, true);
    AURORA_TEST_CHECK_TRUE(menu != paste);
    AURORA_TEST_CHECK_TRUE(paste != copy);
    AURORA_TEST_CHECK_TRUE(menu != copy);
}

AURORA_TEST_CASE(config_right_click_is_an_alias_not_a_second_value_table) {
    static_assert(std::is_same_v<borealis::config::RightClickAction, borealis::ui::RightClickAction>);
    AURORA_TEST_CHECK_EQ(TerminalSettings{}.right_click, RightClickAction::ContextMenu);
}

}  // namespace borealis::test_cases::utest_right_click
