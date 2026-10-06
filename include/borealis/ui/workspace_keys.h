#pragma once

// ============================================================
// 工作区键位：分屏层命令的判定与步进折算（include/borealis/ui/workspace_keys.h）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.02` 的键盘腿里与 widget、与会话无关的那半：一次按键该触发哪条分屏命令，以及
// 「键盘步进」落到哪条把手、推多远。键位总表见 `codespec/UI_WORKSPACE_INTERACT.draft.md` §2 的面板 4。
//
// 本件存在的**首要理由**是派发通道（裁决 7.47②）：这些键位不经框架 `ShortcutRegistry`，而由
// 焦点 pane 的按键入口前置过滤。三条读 Aurora 当日活动分支的实测结论里有两条到今天仍成立——
// 快捷键钩子先于任何控件看到事件（就地重命名的编辑器期间的键会被抢走，而 `ShortcutScope::Focus`
// 只判「本宿主有没有焦点控件」这个 bool、不判是哪个）、该钩子是 `std::function` 赋值即替换
// （装上就顶掉 `Application` 注入的那条）。第三条（修饰**整字节相等**而 `NumLock` 与四个可按位
// 同处一个掩码，附录 A.2 的 **G25**）已于 2026-10-04 回货闭合：框架 `KeyCombo::matches` 现只比
// 「可按住」的四位，与本件口径逐位一致。于是「回货后可原样迁回」这句要改口——迁移只是**通道
// 改道**而**不动任何判据**，但它另有跨仓前置（焦点作用域须判到控件身份），见裁决 7.51③。
//
// 三条决定形态的口径：
// - **只比 Shift / Ctrl / Alt / Meta 四位**：锁定态位（NumLock 等）不参与判定，故同一组合在
//   NumLock 开与关两态下必须给出同一条命令。入参因此复用 `term::KeyPress` 而不是新造一个修饰
//   结构——`term::encode_key` 收的也是它，视口只需一次转换就能同时喂两方。
// - **修饰逐位相等而非「包含」**：`Ctrl+Shift+Alt+D` 不匹配「向右切分」。留白是给未来的键位
//   覆盖（`SPEC.FEAT.PREF.04` 的可重绑），「带着多余修饰也算命中」会把三键组合静默吞成两键。
// - **步进的「推哪条把手」由本件判定**：位移量与目标槽位是一条算式的两半（`SPEC.FEAT.WS.02` ③
//   的「键盘步进」与「拖拽」共用 `move_divider`），拆给绘制侧就会与鼠标那条路径分叉。按下方向与
//   焦点 pane 所在层的排布轴垂直时**无把手可推**（该方向上没有兄弟边界），跨层的最近
//   把手不在本件射程内——树只给直接父层（`PaneTree::container_of`），两层各判必分叉。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "borealis/term/keymap.h"
#include "borealis/ui/pane_tree.h"

namespace borealis::ui {

/// @brief 焦点 pane 上的一条分屏命令。
///
/// 只有「命令」而没有参数：把手的目标与位移由 `divider_step` 折算，切分轴由命令本身携带，
/// 于是绘制侧拿到命令就是拿到全部决策，不必再判方向与修饰。
enum class WorkspaceCommand : std::uint8_t {
    SplitRight,   ///< `Ctrl+Shift+D`：向右切分（同轴并入父层，换轴则嵌套一层）。
    SplitDown,    ///< `Ctrl+Shift+E`：向下切分。
    ClosePane,    ///< `Ctrl+Shift+W`：关闭焦点 pane（树里只剩它时由调用方转为关标签）。
    FocusLeft,    ///< `Alt+←`：按几何路由把焦点交给左侧相邻 pane。
    FocusRight,   ///< `Alt+→`。
    FocusUp,      ///< `Alt+↑`。
    FocusDown,    ///< `Alt+↓`。
    StepLeft,     ///< `Ctrl+Alt+←`：推焦点 pane 左边的把手（焦点 pane 变大）。
    StepRight,    ///< `Ctrl+Alt+→`。
    StepUp,       ///< `Ctrl+Alt+↑`。
    StepDown,     ///< `Ctrl+Alt+↓`。
    EqualizeLayer,  ///< `Ctrl+Shift+0`：等分焦点 pane 所在层（把手双击是同一动作的鼠标档）。
};

/// @brief 一次按键是否触发某条分屏命令。
///
/// 纯函数。**返回空值不等于「无事发生」**：那是「本层不认领这个键」，视口据此把键照常交给会话
/// （`SPEC.FEAT.INTERACT.01` 的转发腿不可让渡，裸方向键与 `Ctrl+方向键` 因此都必须落在空值上）。
/// @param press 按键与修饰态；`num_lock` 字段不参与判定（见文件头第一条口径）。
/// @return 该键位对应的命令；无命中回空值。
[[nodiscard]] auto workspace_command(const term::KeyPress &press) -> std::optional<WorkspaceCommand>;

/// @brief 键位总表的一条：命令 + 触发它的那一次按键（修饰只填四个可按位）。
///
/// 与 `workspace_command` 吃的是同一张表，故本件的**唯一**存在理由是把「有哪些键位」这件事
/// 变成可枚举的：快捷键只读表要拿这十二条当**保留位**参与冲突比对（判据文 D2-a / 人已拍板）。
/// 那些键位不经框架 `ShortcutRegistry`（文件头），而同一个组合键在派发上是快捷键层先消费
/// （裁决 7.51③ 理由 (a)），于是一条注册进命令表的组合键若与这里某条同形，实际生效的是快捷键层
/// 而分屏命令按不到——界面上必须把这一格标成冲突，而不是让用户以为两个动作都能触发。
/// 反过来，本件不自己再列一份修饰常量：`kBindings` 是那张表的唯一真值源，本函数只是把它折成
/// 公共形态，新增键位时两侧同源，不可能分叉。
struct WorkspaceKeyBinding {
    WorkspaceCommand command{};  ///< 该键位触发的分屏命令。
    term::KeyPress press{};      ///< 触发它的按键；`num_lock` 恒假（该位不参与判定，见文件头第一条口径）。
};

/// @brief 键位总表的全部条目（次序＝表内次序）。
[[nodiscard]] auto workspace_key_bindings() -> std::vector<WorkspaceKeyBinding>;

/// @brief 一次键盘步进的落点：推该层的第几条把手、推多远。
struct DividerStep {
    std::size_t slot = 0;    ///< 把手槽位，即「第 @p slot 个子与其下一子之间」，与 `PaneDivider::slot` 同义。
    double delta_dp = 0.0;   ///< 沿轴位移（dp）；正数把 @p slot 那个子推大，故恒为「焦点 pane 变大」。
};

/// @brief 把步进命令折成该层的一条把手位移。
///
/// 步长是 `max(该层沿轴 5%, 24 dp)`（裁决 7.47④）：固定 dp 步长在宽层上是「按不动」、在窄层上
/// 一步过半；纯百分比则在极窄层上小到不足以跨过一格。取两者上界。
/// @param command 取自 `workspace_command` 的返回值；非步进命令回空值。
/// @param focused_slot 焦点 pane 在该层直接子里的槽位（由绘制侧从布局结果给出）。
/// @param child_count 该层直接子数——把手只有 `child_count - 1` 条，越出这个边界的槽位不存在。
/// @param axis 该层的排布轴——**与命令方向垂直时无把手可推**，回空值。
/// @param extent_dp 该层沿轴长度（dp，`PaneDivider::extent_dp` 即其来源）。
/// @return 位移落点；该方向上没有兄弟边界（焦点 pane 已在层的这一头，或那一侧的槽位不存在）时
///         同样回空值。
[[nodiscard]] auto divider_step(WorkspaceCommand command, std::size_t focused_slot,
                                std::size_t child_count, PaneAxis axis, double extent_dp)
    -> std::optional<DividerStep>;

}  // namespace borealis::ui
