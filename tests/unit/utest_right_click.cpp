/// 测试类型: unit
/// 目标单元: include/borealis/ui/right_click.h + src/ui/right_click.cpp
/// 测试说明: 右键三态的处置计划（`SPEC.FEAT.INTERACT.03`）：`ContextMenu` 态出菜单且无选区时
///           「复制」置灰而「粘贴」恒可用、`Paste` 与 `CopyOnSelect` 两态不发菜单、
///           `CopyOnSelect` 态无选区回 `None`（空选区复制会覆盖用户剪贴板）、菜单条目的次序即
///           自上而下的次序，以及 `config::RightClickAction` 是 `ui::RightClickAction` 的别名
///           而非第二份取值表、缺省取值仍是菜单态。
///           批 2 追加第三条目（`SPEC.FEAT.CONN.11` 裁决 7.116 D1①/D6①）：能力闸关＝条目**不出现**
///           而不是灰显，闸开＝两态都在且排在复制/粘贴之后；两态直接动作仍不发菜单。
///           本棒（`SPEC.FEAT.INTERACT.05` 裁决 7.119 D12①）追加第四枚入参 `LinkMenuState`：命中链接
///           ＝四条且次序为 复制/粘贴/打开链接/会话日志，未命中＝三条**逐字节不变**——「打开链接」
///           这一条同样是不出现而非灰显，而它把会话日志那条往后挤了一位，故挤位这件事要单独钉住。

#include <type_traits>
#include <vector>

#include "borealis/config/settings.h"
#include "borealis/ui/right_click.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_right_click {

namespace {

using borealis::config::TerminalSettings;
using borealis::ui::LinkMenuState;
using borealis::ui::LogMenuState;
using borealis::ui::MenuCommand;
using borealis::ui::MenuItem;
using borealis::ui::RightClickAction;
using borealis::ui::RightClickIntent;
using borealis::ui::RightClickPlan;
using borealis::ui::plan_right_click;

/// 既有判据一律在「能力闸关」的档位上跑：第三条目不出现时两条老用例一字不改（D6① 的代价面）。
constexpr LogMenuState log_off;
constexpr LogMenuState log_idle{.available = true, .logging = false};
constexpr LogMenuState log_running{.available = true, .logging = true};

/// 同理，「打开链接」那一条在本棒之前的全部判据上都取「没命中」档（裁决 7.119 D12① 的代价面）：
/// 命中与否由绘制侧现扫，判定不落在本件，故这里只喂两枚布尔。
constexpr LinkMenuState no_link;
constexpr LinkMenuState on_link{.on_link = true};

}  // namespace

AURORA_TEST_CASE(context_menu_with_selection_offers_both_commands_enabled) {
    const RightClickPlan plan = plan_right_click(RightClickAction::ContextMenu, true, log_off, no_link);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::Menu);
    AURORA_TEST_CHECK(plan.items == std::vector<MenuItem>{{MenuCommand::Copy, true}, {MenuCommand::Paste, true}});
}

AURORA_TEST_CASE(context_menu_without_selection_grays_out_only_the_copy_row) {
    const RightClickPlan plan = plan_right_click(RightClickAction::ContextMenu, false, log_off, no_link);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::Menu);
    // 置灰而非点击后静默失败（视觉稿 F-b）；粘贴不受选区影响。
    AURORA_TEST_CHECK(plan.items == std::vector<MenuItem>{{MenuCommand::Copy, false}, {MenuCommand::Paste, true}});
}

AURORA_TEST_CASE(direct_paste_action_sends_no_menu) {
    const RightClickPlan plan = plan_right_click(RightClickAction::Paste, true, log_off, no_link);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::Paste);
    AURORA_TEST_CHECK_TRUE(plan.items.empty());
}

AURORA_TEST_CASE(direct_paste_action_ignores_selection_presence) {
    // 有无选区都粘贴：本态的入参与结果只有意图，选区知识归复制那条腿。
    AURORA_TEST_CHECK(plan_right_click(RightClickAction::Paste, false, log_off, no_link) ==
                      plan_right_click(RightClickAction::Paste, true, log_off, no_link));
}

AURORA_TEST_CASE(copy_on_select_action_copies_without_a_menu) {
    const RightClickPlan plan = plan_right_click(RightClickAction::CopyOnSelect, true, log_off, no_link);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::Copy);
    AURORA_TEST_CHECK_TRUE(plan.items.empty());
}

AURORA_TEST_CASE(copy_on_select_action_with_empty_selection_does_nothing) {
    // 空选区复制出一条空文本会覆盖用户剪贴板里的既有内容，是负收益，故回 None 而不是 Copy。
    const RightClickPlan plan = plan_right_click(RightClickAction::CopyOnSelect, false, log_off, no_link);
    AURORA_TEST_CHECK_EQ(plan.intent, RightClickIntent::None);
    AURORA_TEST_CHECK_TRUE(plan.items.empty());
}

AURORA_TEST_CASE(the_three_actions_do_not_share_an_intent) {
    // 三态互不吞并：同一「有选区」入参下三种动作给出三种意图，缺省态才是菜单。
    const RightClickPlan menu = plan_right_click(RightClickAction::ContextMenu, true, log_off, no_link);
    const RightClickPlan paste = plan_right_click(RightClickAction::Paste, true, log_off, no_link);
    const RightClickPlan copy = plan_right_click(RightClickAction::CopyOnSelect, true, log_off, no_link);
    AURORA_TEST_CHECK_TRUE(menu != paste);
    AURORA_TEST_CHECK_TRUE(paste != copy);
    AURORA_TEST_CHECK_TRUE(menu != copy);
}

AURORA_TEST_CASE(the_log_row_is_absent_when_the_gate_is_closed_not_grayed_out) {
    // D1① + F17：闸关时条目**不进** items。灰显会把「这里本来有个功能」告诉用户，而裁决要的是
    // 「没有入口」；`enabled` 那一列因此不是这条判据的表达位，故本用例连条目数一起钉。
    const RightClickPlan closed = plan_right_click(RightClickAction::ContextMenu, true, log_off, no_link);
    AURORA_TEST_CHECK_EQ(closed.items.size(), 2U);
    const RightClickPlan open = plan_right_click(RightClickAction::ContextMenu, true, log_idle, no_link);
    AURORA_TEST_REQUIRE_EQ(open.items.size(), 3U);
    // 两态都可用（记录与否只是标签措辞的差别，见 `menu_label`），且排在复制/粘贴之后。
    AURORA_TEST_CHECK(open.items[2] == MenuItem{MenuCommand::ToggleSessionLog, true});
    AURORA_TEST_CHECK(open.items[0].command == MenuCommand::Copy);
    AURORA_TEST_CHECK(open.items[1].command == MenuCommand::Paste);
}

AURORA_TEST_CASE(the_log_state_does_not_reorder_the_menu) {
    // 四组组合（闸关/开 × 态关/开）里条目次序只有一档：有闸就是三条，没闸就是两条。
    const RightClickPlan idle = plan_right_click(RightClickAction::ContextMenu, true, log_idle, no_link);
    const RightClickPlan running = plan_right_click(RightClickAction::ContextMenu, true, log_running, no_link);
    AURORA_TEST_CHECK_EQ(idle.items.size(), running.items.size());
    for (std::size_t index = 0; index < idle.items.size(); ++index) {
        AURORA_TEST_CHECK_MSG(idle.items[index].command == running.items[index].command,
                              "the toggle row moved when the session started logging");
    }
}

AURORA_TEST_CASE(direct_right_click_actions_still_send_no_menu_with_the_gate_open) {
    // 既有判据「`Paste`/`CopyOnSelect` 两态不发菜单」不受新条目影响（稿 §6 那条改口的另一半）：
    // 这两态连菜单都没有，第三条目自然也进不去——它们正是命令面板那条入口存在的理由（D6①）。
    // 命中链接与否同样进不去：`intent` 不是 `Menu` 时 `items` 压根不填。
    for (const RightClickAction action : {RightClickAction::Paste, RightClickAction::CopyOnSelect}) {
        for (const LogMenuState log : {log_off, log_idle, log_running}) {
            for (const LinkMenuState link : {no_link, on_link}) {
                AURORA_TEST_CHECK_TRUE(plan_right_click(action, true, log, link).items.empty());
            }
        }
    }
}

AURORA_TEST_CASE(a_hit_link_adds_the_open_row_and_pushes_the_log_row_down) {
    // 判据 6（`codespec/UI_URL.draft.md` 的 D12①）：四条的次序是 复制 → 粘贴 → **打开链接** → 会话日志。
    // 恒可用（`enabled` 真）：白名单外的串早在 `find_link_at` 就不产链接，能出现即必能开。
    const RightClickPlan plan = plan_right_click(RightClickAction::ContextMenu, true, log_idle, on_link);
    AURORA_TEST_REQUIRE_EQ(plan.items.size(), 4U);
    AURORA_TEST_CHECK(plan.items[0] == MenuItem{MenuCommand::Copy, true});
    AURORA_TEST_CHECK(plan.items[1] == MenuItem{MenuCommand::Paste, true});
    AURORA_TEST_CHECK(plan.items[2] == MenuItem{MenuCommand::OpenLink, true});
    AURORA_TEST_CHECK(plan.items[3] == MenuItem{MenuCommand::ToggleSessionLog, true});
}

AURORA_TEST_CASE(an_unhit_right_click_sends_the_three_row_menu_byte_for_byte) {
    // 「未命中 ⇒ 三条逐字节不变」是本棒对既有判据唯一的改口面（稿 D12 的代价面）：闸开但没命中链接
    // 时，菜单与 `MenuCommand::OpenLink` 存在之前一模一样，故拿一份手写的三条表逐项比。
    const RightClickPlan plan = plan_right_click(RightClickAction::ContextMenu, true, log_idle, no_link);
    AURORA_TEST_CHECK(plan.items ==
                      std::vector<MenuItem>{{MenuCommand::Copy, true},
                                            {MenuCommand::Paste, true},
                                            {MenuCommand::ToggleSessionLog, true}});
    // 无选区那一档仍只置灰「复制」，命中链接不会让第二条以外的任何东西复活。
    const RightClickPlan without_selection = plan_right_click(RightClickAction::ContextMenu, false, log_idle, on_link);
    AURORA_TEST_REQUIRE_EQ(without_selection.items.size(), 4U);
    AURORA_TEST_CHECK_FALSE(without_selection.items[0].enabled);
    AURORA_TEST_CHECK_TRUE(without_selection.items[2].enabled);
    // 能力闸关 + 命中链接＝三条（复制/粘贴/打开链接）：两条闸各自只管自己那一条的出现与否。
    const RightClickPlan log_closed = plan_right_click(RightClickAction::ContextMenu, true, log_off, on_link);
    AURORA_TEST_REQUIRE_EQ(log_closed.items.size(), 3U);
    AURORA_TEST_CHECK_EQ(log_closed.items[2].command, MenuCommand::OpenLink);
}

AURORA_TEST_CASE(the_link_row_absence_does_not_depend_on_the_selection) {
    // 「打开链接」的入参是**指针落点**而不是选区（判据 6）：有无选区只动「复制」那一格。
    const RightClickPlan hit =
        plan_right_click(RightClickAction::ContextMenu, false, log_off, on_link);
    AURORA_TEST_CHECK_EQ(hit.items.size(), 3U);
    AURORA_TEST_CHECK_EQ(hit.items[2].command, MenuCommand::OpenLink);
}

AURORA_TEST_CASE(config_right_click_is_an_alias_not_a_second_value_table) {
    static_assert(std::is_same_v<borealis::config::RightClickAction, borealis::ui::RightClickAction>);
    AURORA_TEST_CHECK_EQ(TerminalSettings{}.right_click, RightClickAction::ContextMenu);
}

}  // namespace borealis::test_cases::utest_right_click
