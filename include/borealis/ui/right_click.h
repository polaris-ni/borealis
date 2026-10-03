#pragma once

// ============================================================
// 右键行为决策（include/borealis/ui/right_click.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.03` 的「右键行为三态可配」（Windows Terminal 三态，配置键
// `terminal.right_click`）里与绘制、IO 无关的那半：一次右键按下**该做什么**，以及菜单形态下
// 有哪几条、哪一条置灰。菜单文案与浮层都在绘制侧，本件只出命令标识与可用位——判定的全部
// 取值都能在无 UI 环境里逐条断言（AGENTS.md §4.4 第 20 条）。
//
// 不含 Aurora 也不含 `config` 类型（`config/settings.h` 已含 `ui::PaletteSpec`，反向依赖会成
// 模块环，架构 §2.3）。三态枚举以本件为**唯一定义处**，`config::RightClickAction` 是其别名
// （与 `term::PasteNewlinePolicy` 同一条先例，裁决 7.33 的单一真值源迁移形态），免得配置侧与
// 界面侧各持一份取值表在枚举值上分叉。
//
// 两条口径的由来（细则见裁决 7.41）：
// - **`ContextMenu` 态无选区时「复制」置灰而不是点了静默失败**（视觉稿 F-b）：置灰把「现在没有
//   可复制的东西」这件事在点击之前就说出来；灰显由 `enabled` 表达，绘制侧据此取灰字色。
// - **`Paste` 与 `CopyOnSelect` 两态不发菜单**（裁决 7.38⑥）：用户选这两态就是要一次点击办一件事，
//   菜单反而多一层。`CopyOnSelect` 态无选区时回 `None`——空选区复制出一条空文本会覆盖用户剪贴板，
//   是负收益。
// ============================================================

#include <cstdint>
#include <vector>

namespace borealis::ui {

/// @brief 右键行为三态（配置键 `terminal.right_click` 的落盘名 → 取值映射留在 `config::Store`）。
enum class RightClickAction : std::uint8_t {
    ContextMenu,   ///< 弹上下文菜单（缺省）。
    Paste,         ///< 直接粘贴，不发菜单。
    CopyOnSelect,  ///< 直接复制当前选区，不发菜单；无选区即什么都不做。
};

/// @brief 菜单里的一条命令标识（**不含文案**：标识 → 文案的映射属绘制侧）。
///
/// 本棒只出这两项——它们是当前已有真实行为支撑的条目（复制走 `ui::copy_text`，粘贴走
/// `term::plan_paste`）。视觉稿的其余各项（全选 / 打开链接 / 分屏 / 重命名 / 广播组 / 会话日志 /
/// 关闭）随各自需求落地再入列，不在这里留「点了什么也不发生」的空位。
enum class MenuCommand : std::uint8_t {
    Copy,  ///< 复制选区；无选区时置灰。
    Paste, ///< 读剪贴板并按粘贴处置计划发送。
};

/// @brief 菜单的一条条目：命令与可用位。
struct MenuItem {
    MenuCommand command{};
    bool enabled = true;  ///< false 即置灰（`enabled=false` 的框架语义：灰显且不响应点击）。

    /// @brief 逐字段全等比较（计划断言用）。
    [[nodiscard]] auto operator==(const MenuItem &other) const noexcept -> bool = default;
};

/// @brief 一次右键按下要做的事。
enum class RightClickIntent : std::uint8_t {
    None,   ///< 什么都不做（`CopyOnSelect` 态且无选区）。
    Copy,   ///< 直接复制选区。
    Paste,  ///< 直接粘贴。
    Menu,   ///< 弹菜单，条目见 `RightClickPlan::items`。
};

/// @brief 一次右键按下的完整处置计划。
struct RightClickPlan {
    RightClickIntent intent{};
    std::vector<MenuItem> items;  ///< 菜单条目，自上而下的次序即本表次序；仅 `Menu` 态非空。

    /// @brief 逐字段全等比较（计划断言用）。
    [[nodiscard]] auto operator==(const RightClickPlan &other) const noexcept -> bool = default;
};

/// @brief 把「三态配置 + 此刻有没有选区」折算成右键处置计划。
///
/// 纯函数：同一入参恒得同一计划，故三态与选区的四种组合全部可脱开界面断言。
/// @param action `config::RightClickAction`（本件 `RightClickAction`）的当前取值。
/// @param has_selection 当前是否存在非空选区（判据是 `ui::row_spans` 出不出区间，不是「端点是否重合」
///                      那种写法——两者等价，但调用方手里的是区间表）。
/// @return 处置计划；`intent != Menu` 时 `items` 为空。
[[nodiscard]] auto plan_right_click(RightClickAction action, bool has_selection) -> RightClickPlan;

}  // namespace borealis::ui
