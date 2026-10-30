#pragma once

// ============================================================
// 侧栏折叠轨（src/ui/sidebar_rail.h，私有头）
// ------------------------------------------------------------
// 屏 1 判据 ③ 那条 34 dp 图标列（`codespec/UI_WORKSPACE.draft.svg`：`x=0 y=104 w=34 h=768`
// 填 `#21222C`＝色板 `card_bg`，右沿一条 1 dp 线）。展开态是**同一条列变宽到 320 dp**
// （`codespec/UI_OVERVIEW.draft.md` §2.2 那一行＋§4 N1），所以本件与展开后的档案树面板互斥地
// 占同一个槽位——两态各是一枚控件，装配层按 `appearance.sidebar_collapsed` 挑一枚上槽。
//
// 轨上三枚图标的身份按人已裁的口径取「三个视图直达」，与 `conn::SidebarViewMode` 的三档一一对应
// （裁决 7.180）：形状比对只能确证中间那枚在屏 2 挨着「最近连接」四字，余两枚无文字根据，故
// 身份不来自形状而来自消费方模型。点击＝展开到该视图，本件不认识 config、不认识档案表。
//
// 类声明含 Aurora 类型，按裁决 D1① 留 `src/`。
// ============================================================

#include <array>
#include <cstddef>
#include <functional>
#include <optional>

#include "aurora/render/painter.h"
#include "aurora/widget/widget.h"

#include "conn/profile_views.h"  // SidebarViewMode（本件的三枚身份源，纯逻辑层）

namespace au = aurora;

namespace borealis::ui {

/// @brief 折叠轨与装配层的接缝。
struct SidebarRailHooks {
    /// @brief 点上第 `index` 枚槽（次序＝`sidebar_rail_slot_order()`）。装配层的动作是「展开并切到该视图」。
    std::function<void(conn::SidebarViewMode mode)> select_view;
    /// @brief 轨收到一次左键 `Press`（先于命中哪一格的判定）。
    ///
    /// 与广播横幅同一条上报口径（`SPEC.FEAT.WS.03` 稿 D7 的第三处上报点）：轨常驻每窗布局，
    /// 用户点它一下就是碰过这一扇窗，命令落点因此不该停在别的窗。空即不报。
    std::function<void()> note_press;
};

/// @brief 折叠轨控件（不可获焦的叶子）。
///
/// 继承扩展点：无。
/// @note Thread: main-thread only
/// @note Rebuildable: yes, via `set_active_view`
class SidebarRail final : public au::Widget {
public:
    /// @param hooks 视图直达与上报接缝（`select_view` 必须填，否则点击只被消费、不展开）。
    explicit SidebarRail(SidebarRailHooks hooks);

    [[nodiscard]] auto type_name() const -> const char * override { return "SidebarRail"; }

    /// @brief 换当前视图档（哪一枚画亮）；值变才标脏。
    /// @param mode 三档之一（缺省 `All`，与 `ConnectionSidebar::mode_` 的缺省同档）。
    auto set_active_view(conn::SidebarViewMode mode) -> void;

    [[nodiscard]] auto active_view() const noexcept -> conn::SidebarViewMode { return active_; }

    /// @brief 最近一帧第 `index` 枚槽的命中盒（dp，**窗口系**＝与 `paint_bounds()` 同系）；越界返回空盒。
    ///
    /// 只作观测点：命中不分槽位画亮暗，而按同一台算式把点折回槽序（坐标系按裁决 7.179④ 的补记口径，
    /// 框架把容器下传的内容盒原点原样交给 `on_paint`，本件在其上摆内缩）。
    [[nodiscard]] auto slot_box(std::size_t index) const noexcept -> const au::Rect &;

    /// @brief 按局部 y 折出命中的槽序（越界＝没有这一格）。
    /// @param local_y 事件在本控件内的 y（`MouseEvent::local_position.y`）。
    [[nodiscard]] auto slot_at(double local_y) const noexcept -> std::size_t;

  private:
    auto on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size override;
    auto on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void override;
    auto on_pointer_event(au::MouseEvent &e) -> void override;
    [[nodiscard]] auto wants_click() const -> bool override { return true; }
    [[nodiscard]] auto cursor_shape() const -> std::optional<au::CursorShape> override;

    SidebarRailHooks hooks_;
    conn::SidebarViewMode active_ = conn::SidebarViewMode::All;
    std::array<au::Rect, 3> slots_{};  ///< 最近一帧的三枚槽盒（dp，窗口系）。
};

/// @brief 三枚槽的自上而下次序（唯一真源；用例拿它与 `SidebarViewMode` 的三档对账）。
///
/// 顺序不是随便排的：屏 1 那条上第一枚是档案树入口（folder），后两枚是它的两个过滤视图。
/// 换序就会让「亮的那一枚」与展开后面板里选中的视图错位。
[[nodiscard]] auto sidebar_rail_slot_order() -> const std::array<conn::SidebarViewMode, 3> &;

}  // namespace borealis::ui
