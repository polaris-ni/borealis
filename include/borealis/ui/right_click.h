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
/// 会话日志那条是 2026-10-12 到货的消费方（`SPEC.FEAT.CONN.11`，裁决 7.116 D6），头注原先那句
/// 「随各自需求落地再入列」对它已经兑现。其余各项（全选 / 打开链接 / 分屏 / 重命名 / 广播组 /
/// 关闭）仍不在这里留「点了什么也不发生」的空位。
enum class MenuCommand : std::uint8_t {
    Copy,             ///< 复制选区；无选区时置灰。
    Paste,            ///< 读剪贴板并按粘贴处置计划发送。
    ToggleSessionLog, ///< 开启/停止本会话日志：一条命令两态，态由 `LogMenuState::logging` 给。
};

/// @brief 日志条目进菜单时要看的两件事（裁决 7.116 D1①＋D6①）。
///
/// 为什么是**两个字段**而不是一枚三值枚举：能力闸关与「闸开着但此刻没记录」在菜单上是同一张脸
/// （前者条目不出现、后者条目写「开启会话日志」），但判据要断的四组组合是 2×2，合成一枚就少一组
/// 证人。也不并进 `MenuItem::enabled`：灰显是「看得见但点不动」，而 D1① 要的形态是「这道功能在本
/// 产品里根本不存在」——F17 那条「钩子未注入即无入口」说的是**不出现**。
struct LogMenuState {
    bool available = false;  ///< 能力闸 `connection.session_logging` 且装配层已注入钩子。
    bool logging = false;    ///< 本会话此刻是否在记录（`Session::logging_enabled()`）。

    /// @brief 逐字段全等比较（计划断言用）。
    [[nodiscard]] auto operator==(const LogMenuState &) const noexcept -> bool = default;
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

/// @brief 把「三态配置 + 此刻有没有选区 + 日志现状」折算成右键处置计划。
///
/// 纯函数：同一入参恒得同一计划，故三态、选区与日志状态的组合全部可脱开界面断言。
/// @param action `config::RightClickAction`（本件 `RightClickAction`）的当前取值。
/// @param has_selection 当前是否存在非空选区（判据是 `ui::row_spans` 出不出区间，不是「端点是否重合」
///                      那种写法——两者等价，但调用方手里的是区间表）。
/// @param log 日志能力闸与该会话的记录态（判据 2 的两态文案之源）。**本棒唯一一处改既有签名**：
///            区分「开启/停止/此条不存在」必须要这一枚入参，而它又不能在纯函数里读会话——
///            读 `Session` 就把绘制侧的知识带进决策件了（代价与理由见裁决 7.116 D6 的代价列）。
/// @return 处置计划；`intent != Menu` 时 `items` 为空。菜单态下条目次序恒为
///         复制 → 粘贴 → 会话日志（`log.available` 为假时第三条**不出现**，前两条逐字节不变）。
[[nodiscard]] auto plan_right_click(RightClickAction action, bool has_selection, LogMenuState log) -> RightClickPlan;

}  // namespace borealis::ui
