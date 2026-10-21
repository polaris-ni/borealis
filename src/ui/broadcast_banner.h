#pragma once

// ============================================================
// 广播横幅（src/ui/broadcast_banner.h，私有头）
// ------------------------------------------------------------
// 类声明含 Aurora 类型，故按裁决 D1① 留在 `src/`。本件是 `SPEC.FEAT.WS.09` 的全局标识：
// 标签条与工作区之间一条 26 dp 全宽横幅（组空时高 0），列出成员会话名与去重后的会话数，
// 右侧常驻「退出广播」。点击横幅任意处 = 清空整组（装配层接线后调 `WorkspaceView::
// restore_keyboard_focus()` 把焦点还给最后持焦的 pane，F10）。
//
// 三条形态口径（`codespec/UI_WORKSPACE_BROADCAST.draft.md` §2／§3）：
// - 高 0/26 由本控件按 `State::active` 自报，常驻根 Column 的 index 1 不增删节点（D10）；
// - 绘制只用默认 sans 字体与稿面固定 token，不接终端主题与 `TerminalView::ref_font_`（F11）；
// - 不可获焦（否则点击后键盘进了横幅）、`wants_click()` 为真、整条 26 dp 都是命中区。
// ============================================================

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "aurora/core/color.h"
#include "aurora/render/painter.h"
#include "aurora/widget/widget.h"

namespace au = aurora;

namespace borealis::ui {

/// @brief 横幅的呈现状态：装配层每次成员集合变化时重算一次。
///
/// 名字由装配层按 `resolve_tab_name` 现取（F12）：同一标签的多个成员追加「窗格 N」，横幅不
/// 自己缓存标签名，标签改名时经既有的 `TabStrip::set_changed_hook` 再交一份新状态过来。
struct BroadcastBannerState {
    bool active = false;                     ///< 组是否非空（决定 0/26 与命中）。
    std::vector<std::u32string> names;       ///< 成员会话名（去重后按加入序）。
    std::size_t session_count = 0;           ///< 去重后的会话数（与 `names.size()` 同源）。

    [[nodiscard]] auto operator==(const BroadcastBannerState &other) const noexcept -> bool = default;
};

/// @brief 横幅与装配层的一条接缝。
struct BroadcastBannerHooks {
    /// @brief 点击横幅任意处：清空整组（装配层负责再刷横幅、pill 与焦点）。
    std::function<void()> exit_all;
    /// @brief 本横幅收到一次左键 `Press`（先于「是否在广播态」的判定，故与清组成功与否无关）。
    ///
    /// `SPEC.FEAT.WS.03` 稿 D7 的第三处上报点：横幅常驻每窗根 Column 的 index 1，用户点它一下
    /// 就是碰过这一扇窗，命令落点因此不该停在别的窗。空即不报。
    std::function<void()> note_press;
};

/// @brief 广播横幅控件（不可获焦的叶子）。
///
/// 继承扩展点：无（本件是叶子控件，不再被子类化）。
/// @note Thread: main-thread only
/// @note Rebuildable: yes, via `set_state`
class BroadcastBanner final : public au::Widget {
  public:
    /// @param hooks 退出接缝（必须填，否则点击只被消费、不清组）。
    explicit BroadcastBanner(BroadcastBannerHooks hooks);

    [[nodiscard]] auto type_name() const -> const char * override { return "BroadcastBanner"; }

    /// @brief 换一份状态；值变才标脏（判据文 §4 第 5 条）。
    /// @param state 新状态（与旧值相等时整条空操作）。
    auto set_state(BroadcastBannerState state) -> void;

    [[nodiscard]] auto state() const noexcept -> const BroadcastBannerState & { return state_; }

    /// @brief 最近一次绘制时的退出按钮盒（dp，本控件局部系）；未激活时为空盒。
    ///
    /// 用例读「按钮画没画、画在哪」用；命中不按这个盒分档——整条横幅都可点（稿 §2.2）。
    [[nodiscard]] auto exit_button_box() const noexcept -> const au::Rect & { return exit_button_box_; }

  private:
    auto on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size override;
    auto on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void override;
    auto on_pointer_event(au::MouseEvent &e) -> void override;
    [[nodiscard]] auto wants_click() const -> bool override { return state_.active; }
    [[nodiscard]] auto cursor_shape() const -> std::optional<au::CursorShape> override;

    BroadcastBannerHooks hooks_;
    BroadcastBannerState state_{};
    au::Rect exit_button_box_{};  ///< 最近一帧的按钮盒（dp，本控件局部系）。
};

}  // namespace borealis::ui
