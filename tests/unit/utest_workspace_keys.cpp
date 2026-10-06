/// 测试类型: unit
/// 目标单元: include/borealis/ui/workspace_keys.h + src/ui/workspace_keys.cpp
/// 测试说明: 分屏层的键位绑定表与键盘步进折算（`SPEC.FEAT.WS.02` ③，裁决 7.47①②④ / 7.48）。
///           键位侧断四类命令各自命中、认领集**恰为**那十二个键位（多余的认领会把可打印键与
///           功能键从会话那里抢走）、修饰多一位即不命中、NumLock 位不参与判定；步进侧断「推哪条
///           把手、推多远」的全部三条算式：符号恒为「焦点 pane 变大」、步长 `max(5%, 24 dp)`、
///           以及三类无把手可推的拒绝路径。另断 `workspace_key_bindings()` 与派发函数同源（裁决
///           7.72 的③：快捷键表要把这十二条当保留位比对，两处各写一张表就会分叉）。

#include <cstddef>
#include <optional>
#include <vector>

#include "borealis/term/keymap.h"
#include "borealis/ui/workspace_keys.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_workspace_keys {

namespace {

using borealis::term::KeyPress;
using borealis::term::KeySym;
using borealis::ui::divider_step;
using borealis::ui::DividerStep;
using borealis::ui::PaneAxis;
using borealis::ui::WorkspaceCommand;
using borealis::ui::workspace_command;
using borealis::ui::WorkspaceKeyBinding;
using borealis::ui::workspace_key_bindings;

/// @brief 一条键位的简写：修饰缺省即「没按」，`num_lock` 只在专设那一例里给值。
[[nodiscard]] constexpr auto key(KeySym sym, bool shift = false, bool control = false, bool alt = false,
                                 bool meta = false, bool num_lock = false) -> KeyPress {
    return KeyPress{.sym = sym,
                    .shift = shift,
                    .control = control,
                    .alt = alt,
                    .meta = meta,
                    .num_lock = num_lock};
}

/// @brief 本件认领的全部键位（裁决 7.48②）：认领集的形状本身就是一条判据，多一条即误吞会话输入。
constexpr KeyPress kClaimed[] = {
    key(KeySym::D, true, true),
    key(KeySym::E, true, true),
    key(KeySym::W, true, true),
    key(KeySym::D0, true, true),

    key(KeySym::ArrowLeft, false, false, true),
    key(KeySym::ArrowRight, false, false, true),
    key(KeySym::ArrowUp, false, false, true),
    key(KeySym::ArrowDown, false, false, true),

    key(KeySym::ArrowLeft, false, true, true),
    key(KeySym::ArrowRight, false, true, true),
    key(KeySym::ArrowUp, false, true, true),
    key(KeySym::ArrowDown, false, true, true),
};

/// @brief 该被会话收下、本件不得认领的键位样本：三类最容易误吞的形态各取几个。
[[nodiscard]] auto forwarded_keys() -> std::vector<KeyPress> {
    return {
        key(KeySym::ArrowLeft),   // 裸方向键：`vim` / `tmux` 的导航（SPEC.FEAT.INTERACT.01）
        key(KeySym::ArrowRight, false, true),  // Ctrl+→：shell 的词跳转
        key(KeySym::ArrowUp, true),            // Shift+↑：滚动回看归视口，不是一条命令
        key(KeySym::ArrowDown, false, false, true, true),  // Meta+Alt+↓
        key(KeySym::D),                // 打字
        key(KeySym::D, true),          // Shift+D 即大写 D
        key(KeySym::E, false, true),   // 少一位 Shift 就不是向下切分
        key(KeySym::W, false, false, true),
        key(KeySym::D0),
        key(KeySym::KP_0, false, true),  // 小键盘 0 随 NumLock 两档归文本通道
        key(KeySym::F1),
        key(KeySym::Enter),
    };
}

}  // namespace

AURORA_TEST_CASE(pane_commands_match_their_documented_bindings) {
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::D, true, true)).value(),
                         WorkspaceCommand::SplitRight);
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::E, true, true)).value(),
                         WorkspaceCommand::SplitDown);
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::W, true, true)).value(),
                         WorkspaceCommand::ClosePane);
    // 等分取主键盘行的 0（裁决 7.48①）：小键盘那一位随锁定态有两种归属，不登记。
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::D0, true, true)).value(),
                         WorkspaceCommand::EqualizeLayer);
}

AURORA_TEST_CASE(focus_routing_covers_all_four_alt_arrows) {
    // 触发键取 `Alt+方向键`（裁决 7.47①）：四个方向都有值，缺一即那一侧跨不去。
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowLeft, false, false, true)).value(),
                         WorkspaceCommand::FocusLeft);
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowRight, false, false, true)).value(),
                         WorkspaceCommand::FocusRight);
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowUp, false, false, true)).value(),
                         WorkspaceCommand::FocusUp);
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowDown, false, false, true)).value(),
                         WorkspaceCommand::FocusDown);
}

AURORA_TEST_CASE(step_covers_all_four_ctrl_alt_arrows) {
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowLeft, false, true, true)).value(),
                         WorkspaceCommand::StepLeft);
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowRight, false, true, true)).value(),
                         WorkspaceCommand::StepRight);
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowUp, false, true, true)).value(),
                         WorkspaceCommand::StepUp);
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowDown, false, true, true)).value(),
                         WorkspaceCommand::StepDown);
}

AURORA_TEST_CASE(the_claimed_set_is_exactly_those_twelve_keys) {
    for (const KeyPress &press : kClaimed) {
        AURORA_TEST_CHECK(workspace_command(press).has_value());
    }
    for (const KeyPress &press : forwarded_keys()) {
        AURORA_TEST_CHECK_FALSE(workspace_command(press).has_value());
    }
}

AURORA_TEST_CASE(one_extra_modifier_is_not_a_hit) {
    // 修饰逐位相等而非「包含」（裁决 7.48②）：带着多余修饰的三键组合不能被两键绑定吞掉。
    AURORA_TEST_CHECK_FALSE(workspace_command(key(KeySym::D, false, true, true)).has_value());
    AURORA_TEST_CHECK_FALSE(workspace_command(key(KeySym::D, true, true, true)).has_value());
    AURORA_TEST_CHECK_FALSE(workspace_command(key(KeySym::ArrowLeft, false, true, true, true)).has_value());
    // 反向同理：少一位修饰就不是这条命令（`Ctrl+Alt+←` 与 `Alt+←` 是两个动作）。
    AURORA_TEST_CHECK_EQ(workspace_command(key(KeySym::ArrowLeft, false, false, true)).value(),
                         WorkspaceCommand::FocusLeft);
}

AURORA_TEST_CASE(num_lock_state_changes_no_verdict) {
    // 本条是 G25 那条缺陷的正面证人：锁定态位不参与判定，故 NumLock 开与关两态逐条相同。
    for (const KeyPress &off : kClaimed) {
        KeyPress on = off;
        on.num_lock = true;
        AURORA_TEST_CHECK_EQ(workspace_command(on), workspace_command(off));
        AURORA_TEST_CHECK(workspace_command(on).has_value());
    }
}

AURORA_TEST_CASE(listed_bindings_are_exactly_the_claimed_keys) {
    // 快捷键表要把这十二条当**保留位**去比对（裁决 7.72 的③），故列表必须与认领集逐条同形：
    // 少一条就是有一个保留位没被守住，多一条则是把会话的键也圈成了保留位。
    const auto listed = workspace_key_bindings();
    AURORA_TEST_REQUIRE_EQ(listed.size(), std::size(kClaimed));
    for (std::size_t index = 0; index < listed.size(); ++index) {
        AURORA_TEST_CHECK_EQ(listed[index].press.sym, kClaimed[index].sym);
        AURORA_TEST_CHECK_EQ(listed[index].press.shift, kClaimed[index].shift);
        AURORA_TEST_CHECK_EQ(listed[index].press.control, kClaimed[index].control);
        AURORA_TEST_CHECK_EQ(listed[index].press.alt, kClaimed[index].alt);
        AURORA_TEST_CHECK_EQ(listed[index].press.meta, kClaimed[index].meta);
    }
}

AURORA_TEST_CASE(listed_bindings_agree_with_the_dispatching_function) {
    // 同源判据：列表里每一条按其 press 再问一次 `workspace_command`，必须回到它自己那一格的命令。
    // 若两处各写一张表（列表抄一份、派发用另一份），这条就会在两个方向上分叉。
    for (const WorkspaceKeyBinding &binding : workspace_key_bindings()) {
        AURORA_TEST_CHECK_EQ(workspace_command(binding.press).value(), binding.command);
        // 锁定态位不参与判定，故列表里那一格恒假——面板据此比对时不会因 NumLock 而漏判。
        AURORA_TEST_CHECK_FALSE(binding.press.num_lock);
    }
}

AURORA_TEST_CASE(step_target_and_sign_follow_the_pressed_direction) {
    // 正向落在焦点格下游的把手（slot == focused_slot）、负向落在上游那条（slot == focused_slot-1），
    // 而位移符号固定「把 slot 那个子推大」，于是焦点 pane 恒在按下方向上变大。取三栏居中的那一格
    // 当焦点：它两侧各有把手，两个方向都该有落点。
    const auto right = divider_step(WorkspaceCommand::StepRight, 1U, 3U, PaneAxis::Horizontal, 1000.0);
    AURORA_TEST_REQUIRE(right.has_value());
    AURORA_TEST_CHECK_EQ(right->slot, 1U);
    AURORA_TEST_CHECK_GT(right->delta_dp, 0.0);

    const auto left = divider_step(WorkspaceCommand::StepLeft, 1U, 3U, PaneAxis::Horizontal, 1000.0);
    AURORA_TEST_REQUIRE(left.has_value());
    AURORA_TEST_CHECK_EQ(left->slot, 0U);
    AURORA_TEST_CHECK_LT(left->delta_dp, 0.0);

    // 纵向层里上下是沿轴方向，左右反而成了跨轴。
    const auto down = divider_step(WorkspaceCommand::StepDown, 1U, 3U, PaneAxis::Vertical, 1200.0);
    AURORA_TEST_REQUIRE(down.has_value());
    AURORA_TEST_CHECK_EQ(down->slot, 1U);
    AURORA_TEST_CHECK_GT(down->delta_dp, 0.0);

    const auto up = divider_step(WorkspaceCommand::StepUp, 1U, 3U, PaneAxis::Vertical, 1200.0);
    AURORA_TEST_REQUIRE(up.has_value());
    AURORA_TEST_CHECK_EQ(up->slot, 0U);
}

AURORA_TEST_CASE(step_is_five_percent_of_the_layer_but_never_below_24_dp) {
    const auto wide = divider_step(WorkspaceCommand::StepRight, 0U, 2U, PaneAxis::Horizontal, 2000.0);
    AURORA_TEST_REQUIRE(wide.has_value());
    AURORA_TEST_CHECK_NEAR(wide->delta_dp, 100.0, 1e-9);  // 2000 × 5%

    const auto mid = divider_step(WorkspaceCommand::StepRight, 0U, 2U, PaneAxis::Horizontal, 500.0);
    AURORA_TEST_REQUIRE(mid.has_value());
    AURORA_TEST_CHECK_NEAR(mid->delta_dp, 25.0, 1e-9);  // 500 × 5%，仍高于下界

    // 窄层上 5% 只有 5 dp，一步跨不过一格，故取下界 24 dp（裁决 7.47④）。
    const auto narrow = divider_step(WorkspaceCommand::StepRight, 0U, 2U, PaneAxis::Horizontal, 100.0);
    AURORA_TEST_REQUIRE(narrow.has_value());
    AURORA_TEST_CHECK_NEAR(narrow->delta_dp, 24.0, 1e-9);
}

AURORA_TEST_CASE(step_is_rejected_when_no_sibling_boundary_exists) {
    // ① 命令方向与该层排布轴垂直：这一方向上没有兄弟边界（跨层不在本件射程，裁决 7.48①）。
    AURORA_TEST_CHECK_FALSE(
        divider_step(WorkspaceCommand::StepUp, 0U, 2U, PaneAxis::Horizontal, 1000.0).has_value());
    AURORA_TEST_CHECK_FALSE(
        divider_step(WorkspaceCommand::StepRight, 0U, 2U, PaneAxis::Vertical, 1000.0).has_value());
    // ② 焦点格已在层的这一头：上游没有把手。
    AURORA_TEST_CHECK_FALSE(
        divider_step(WorkspaceCommand::StepLeft, 0U, 3U, PaneAxis::Horizontal, 1000.0).has_value());
    // ③ 该槽位不存在：焦点格是末位，其下游没有把手（把手只有 `child_count - 1` 条）。
    AURORA_TEST_CHECK_FALSE(
        divider_step(WorkspaceCommand::StepRight, 2U, 3U, PaneAxis::Horizontal, 1000.0).has_value());
    // ④ 非步进命令不该被折成位移。
    for (const auto command : {WorkspaceCommand::SplitRight, WorkspaceCommand::ClosePane,
                               WorkspaceCommand::EqualizeLayer, WorkspaceCommand::FocusLeft}) {
        AURORA_TEST_CHECK_FALSE(divider_step(command, 1U, 3U, PaneAxis::Horizontal, 1000.0).has_value());
    }
}

}  // namespace borealis::test_cases::utest_workspace_keys
