#include "borealis/ui/workspace_keys.h"

#include <algorithm>

namespace borealis::ui {

namespace {

/// @brief 一条键位绑定：四个可按修饰位**逐位相等**再加主键，锁定态位不在其列（见公共头）。
struct Binding {
    term::KeySym sym;
    bool shift;
    bool control;
    bool alt;
    bool meta;
    WorkspaceCommand command;
};

/// @brief 主键盘数字行的 `0`：小键盘的 `KP_0` 刻意不在表内，它在 NumLock 开时归文本通道、关时
///        与主键盘同形（`term::is_keypad` 的两档口径），一条绑定会随锁定态变成两个键位。
constexpr term::KeySym kDigit0 = term::KeySym::D0;

constexpr Binding kBindings[] = {
    {term::KeySym::D, true, true, false, false, WorkspaceCommand::SplitRight},
    {term::KeySym::E, true, true, false, false, WorkspaceCommand::SplitDown},
    {term::KeySym::W, true, true, false, false, WorkspaceCommand::ClosePane},
    {kDigit0, true, true, false, false, WorkspaceCommand::EqualizeLayer},

    {term::KeySym::ArrowLeft, false, false, true, false, WorkspaceCommand::FocusLeft},
    {term::KeySym::ArrowRight, false, false, true, false, WorkspaceCommand::FocusRight},
    {term::KeySym::ArrowUp, false, false, true, false, WorkspaceCommand::FocusUp},
    {term::KeySym::ArrowDown, false, false, true, false, WorkspaceCommand::FocusDown},

    {term::KeySym::ArrowLeft, false, true, true, false, WorkspaceCommand::StepLeft},
    {term::KeySym::ArrowRight, false, true, true, false, WorkspaceCommand::StepRight},
    {term::KeySym::ArrowUp, false, true, true, false, WorkspaceCommand::StepUp},
    {term::KeySym::ArrowDown, false, true, true, false, WorkspaceCommand::StepDown},
};

/// @brief 步进的 dp 下界：窄层上「沿轴 5%」会小到一步跨不过一格，故取一个绝对下界。
constexpr double kMinStepDp = 24.0;

[[nodiscard]] auto matches(const Binding &binding, const term::KeyPress &press) -> bool {
    return binding.sym == press.sym && binding.shift == press.shift && binding.control == press.control &&
           binding.alt == press.alt && binding.meta == press.meta;
}

}  // namespace

auto workspace_command(const term::KeyPress &press) -> std::optional<WorkspaceCommand> {
    for (const Binding &binding : kBindings) {
        if (matches(binding, press)) {
            return binding.command;
        }
    }
    return std::nullopt;
}

auto workspace_key_bindings() -> std::vector<WorkspaceKeyBinding> {
    std::vector<WorkspaceKeyBinding> out;
    out.reserve(std::size(kBindings));
    for (const Binding &binding : kBindings) {
        out.push_back(WorkspaceKeyBinding{
            .command = binding.command,
            .press = term::KeyPress{
                .sym = binding.sym, .shift = binding.shift, .control = binding.control, .alt = binding.alt,
                .meta = binding.meta, .num_lock = false},
        });
    }
    return out;
}

auto divider_step(WorkspaceCommand command, std::size_t focused_slot, std::size_t child_count,
                  PaneAxis axis, double extent_dp) -> std::optional<DividerStep> {
    // 符号即「焦点 pane 变大」的方向：正向把手在焦点格的下游（slot == focused_slot），负向在
    // 上游（slot == focused_slot - 1），于是 `move_divider` 的「正数把第一个子推大」直接成立。
    int sign = 0;
    switch (command) {
        case WorkspaceCommand::StepRight:
            sign = axis == PaneAxis::Horizontal ? 1 : 0;
            break;
        case WorkspaceCommand::StepLeft:
            sign = axis == PaneAxis::Horizontal ? -1 : 0;
            break;
        case WorkspaceCommand::StepDown:
            sign = axis == PaneAxis::Vertical ? 1 : 0;
            break;
        case WorkspaceCommand::StepUp:
            sign = axis == PaneAxis::Vertical ? -1 : 0;
            break;
        default:
            return std::nullopt;
    }
    if (sign == 0) {
        return std::nullopt;  // 命令方向与该层排布轴垂直：这一方向上没有兄弟边界
    }

    std::size_t slot = focused_slot;
    if (sign < 0) {
        if (focused_slot == 0U) {
            return std::nullopt;  // 焦点 pane 已在层的这一头：上游没有把手
        }
        slot = focused_slot - 1U;
    }
    // 把手只有 `child_count - 1` 条（槽位落在「第 slot 个子与其下一子之间」），故末位子的下游
    // 没有把手可推。
    if (slot + 1U >= child_count) {
        return std::nullopt;
    }
    return DividerStep{.slot = slot, .delta_dp = sign * std::max(extent_dp * 0.05, kMinStepDp)};
}

}  // namespace borealis::ui
