#pragma once

// ============================================================
// ui/window_close.h — 关窗询问策略（纯逻辑）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.03` 稿 D3／D4 的那一句「关窗有两条入口」在本仓长期只落成了一条（OS 关闭钮），
// 原因是框架侧一直没有「关闭请求可否决」的公共入口（稿 §0 的 F18）。G44 回货后闸门具备了，
// 命令面板这一条也随之可落——但它一落就带来一个必须当场定死的问题：**同一个动作换了发起方式，
// 后果不能变**。用户在点 × 时会被问一句，在同一条命令上却静默带走全部会话，是把「哪块像素按下去」
// 当成了安全属性。业界同档：Windows Terminal 的文档把「clicking the X button」与「using the
// `closeWindow` command」并列成同一件事（`customize-settings/startup`，布局记忆那一节），
// 本件据此把两条用户入口折进同一条判据。
//
// 反过来，**本层自己发起**的关闭绝不能问：确认框的「关」那一支、退出序遍历全部窗那一支，
// 走的都是程序化路径，拿闸门挡它们会把退出卡在一个没人能答、甚至可能已经没有窗口可挂的框上
// （`WS03_WINDOW_CLOSE_CONFIRM.draft.md` §2 的 D1 与任务书 m3）。框架侧的「策略连带」同档。
//
// 本件只回答「该不该问」，不回答「问几个」：一扇窗名下**任一**标签有活会话即算活，那一步折法
// 在装配层（复用关标签那枚 `has_live_session`，判据不复制第二份），本件收一个 bool。
// ============================================================

#include <cstdint>

namespace borealis::ui {

/// @brief 一次关窗请求的发起方（`SPEC.FEAT.WS.03` 稿 D3）。
///
/// 三档而不是两档的理由：闸门拿到的来源是三类（OS／程序化／策略连带），而命令面板那条入口在
/// 框架看来同样是程序化——**在用户看来不是**。所以「用户发起」必须自己占一档，否则命令与
/// 确认后重试两条会共用一个值，判据就没法把它们分开。
enum class WindowCloseInitiator : std::uint8_t {
    OsCloseButton,  ///< OS 标题栏关闭钮或窗口管理器的关闭请求。
    UserCommand,    ///< 用户在命令面板或（未来的）键位上发起的「关闭此窗」。
    AppInitiated,   ///< 本层自己发起：确认框答「关」之后的重试、退出序、框架策略连带。
};

/// @brief 一次关窗请求的处置。
enum class WindowCloseDisposition : std::uint8_t {
    CloseNow,  ///< 直接关：不弹框。
    AskUser,   ///< 先问一句；窗这一帧继续活着。
};

/// @brief 把发起方与「本窗是否还有活会话」折成一次处置（`SPEC.FEAT.WS.03`，裁决 7.161）。
///
/// 全表只有两格 `AskUser`：两条**用户**入口且窗内有活会话。无活会话时两条用户入口都直接关——
/// 框里没有可损失的东西，问了只是让「关窗」多一次点击。`AppInitiated` 恒直接关，与活会话与否无关。
///
/// @note Thread: 主线程（闸门回调与命令动作）
/// @note Side-effects: none
/// @note Rebuildable: yes
[[nodiscard]] auto window_close_disposition(WindowCloseInitiator initiator, bool has_live_session)
    -> WindowCloseDisposition;

}  // namespace borealis::ui
