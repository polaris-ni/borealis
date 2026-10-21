#include "workspace_view.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <utility>

#include "aurora/core/log.h"
#include "aurora/event/focus.h"
#include "borealis/term/keymap.h"

namespace borealis::ui {

namespace {

/// @brief chrome 色是**固定 token** 而非主题档（裁决 7.25 N6）：主题只换 pane **内部**的底色与字色，
///        把手、缝隙与焦点描边恒为此三色。`PaletteSpec` 里没有 accent 槽，故这里是文件内常量而不是
///        色板的某一档。
constexpr aurora::Color kChrome{0x21, 0x22, 0x2C};  ///< 分屏底：缝隙与把手两侧的空带
constexpr aurora::Color kLine2{0x4D, 0x4F, 0x63};   ///< 把手静止态
constexpr aurora::Color kAccent{0xBD, 0x93, 0xF9};  ///< 把手 hover / 拖拽态与焦点 pane 描边

constexpr double kDividerHitDp = 8.0;      ///< 把手命中区宽，同时是布局留位（`LayoutSpec::divider_dp`）
constexpr double kHandleLengthDp = 32.0;   ///< 把手中段视觉长
constexpr double kHandleThicknessDp = 2.0;  ///< 把手中段视觉粗
constexpr double kFocusRingDp = 2.0;       ///< 焦点 pane 的**内**描边（裁决 7.47⑪：不改行列数）
constexpr double kMinPaneColumns = 20.0;   ///< 最小 pane 的列数（裁决 7.47③）
constexpr double kMinPaneRows = 3.0;       ///< 最小 pane 的行数

/// @brief 缩放角标的几何（`codespec/UI_WORKSPACE_ZOOM.draft.md` §2.2）：热区＝底板，距 pane 右边与
///        上边各 4 dp；字形盒 10×10 居中，1.5 dp 线段。
constexpr double kZoomBadgeDp = 20.0;
constexpr double kZoomBadgeInsetDp = 4.0;
constexpr double kZoomBadgeRadiusDp = 3.0;
constexpr double kZoomGlyphDp = 10.0;
constexpr float kZoomGlyphStrokeDp = 1.5F;
constexpr double kZoomArrowWingDp = 3.5;

/// @brief 本层的矩形都在「相对容器原点」的 dp 空间（`ui::Rect`），框架收的是 origin + size（float）。
///        两种形态没有互转 operator，字段名与精度都不同，故换算点集中在这一个函数里。
[[nodiscard]] auto to_frame(const Rect &r) -> aurora::Rect {
    return aurora::Rect{
        .origin = aurora::Point{.x = static_cast<float>(r.x), .y = static_cast<float>(r.y)},
        .size = aurora::Size{.width = static_cast<float>(r.width), .height = static_cast<float>(r.height)}};
}

/// @brief 全局 dp 盒：把「相对容器原点」的矩形平移到容器在窗口里的原点。
[[nodiscard]] auto global_frame(const aurora::Rect &bounds, const Rect &local) -> aurora::Rect {
    aurora::Rect out = to_frame(local);
    out.origin = bounds.origin + out.origin;
    return out;
}

/// @brief 画一条把手的中段：沿轴居中、垂直轴居中，窄于命中区（缝隙因此不吃 pane 内容）。
auto paint_handle(aurora::Painter &p, const aurora::Rect &bounds, const PaneDivider &divider,
                  const aurora::Color &ink) -> void {
    const bool along_x = divider.axis == PaneAxis::Horizontal;
    const double along = along_x ? divider.box.width : divider.box.height;
    const double cross = along_x ? divider.box.height : divider.box.width;
    const double length = std::min(kHandleLengthDp, std::max(0.0, along));
    if (length <= 0.0 || cross <= kHandleThicknessDp) {
        return;  // 该层被压到放不下把手：宁可不显形，也不画出越界的矩形
    }
    const Rect handle = along_x ? Rect{.x = divider.box.x + (divider.box.width - kHandleThicknessDp) / 2.0,
                                       .y = divider.box.y + (cross - length) / 2.0,
                                       .width = kHandleThicknessDp,
                                       .height = length}
                                : Rect{.x = divider.box.x + (cross - length) / 2.0,
                                       .y = divider.box.y + (divider.box.height - kHandleThicknessDp) / 2.0,
                                       .width = length,
                                       .height = kHandleThicknessDp};
    p.fill_rect(global_frame(bounds, handle), ink);
}

/// @brief 焦点 pane 的 2 dp 内描边（四条矩形压在格子上，不外加尺寸）。
auto paint_focus_ring(aurora::Painter &p, const aurora::Rect &bounds, const Rect &box) -> void {
    const Rect edges[] = {
        Rect{.x = box.x, .y = box.y, .width = box.width, .height = kFocusRingDp},
        Rect{.x = box.x, .y = box.y + box.height - kFocusRingDp, .width = box.width, .height = kFocusRingDp},
        Rect{.x = box.x, .y = box.y, .width = kFocusRingDp, .height = box.height},
        Rect{.x = box.x + box.width - kFocusRingDp, .y = box.y, .width = kFocusRingDp, .height = box.height},
    };
    for (const Rect &edge : edges) {
        p.fill_rect(global_frame(bounds, edge), kAccent);
    }
}

/// @brief 右上角的缩放角标：chrome 不透明底板 + accent 双箭头（展开＝外向 ⤢，收起＝内向 ⤡）。
///
/// 字形用线段拼，不引字体（`codespec/UI_WORKSPACE_ZOOM.draft.md` F8）：pane 容器没有字体依赖，
/// 20 dp 图标也不值得为它接一条字体回退链；无头像素判据因此稳定。
auto paint_zoom_badge(aurora::Painter &p, const aurora::Rect &bounds, const Rect &pane, bool collapse)
    -> void {
    const double x = pane.x + pane.width - kZoomBadgeInsetDp - kZoomBadgeDp;
    const double y = pane.y + kZoomBadgeInsetDp;
    const Rect plate{.x = x, .y = y, .width = kZoomBadgeDp, .height = kZoomBadgeDp};
    p.fill_rounded_rect(global_frame(bounds, plate), static_cast<float>(kZoomBadgeRadiusDp), kChrome);

    const double gx = x + (kZoomBadgeDp - kZoomGlyphDp) / 2.0;
    const double gy = y + (kZoomBadgeDp - kZoomGlyphDp) / 2.0;
    const auto pt = [gx, gy](double lx, double ly) -> aurora::Point {
        return aurora::Point{.x = static_cast<float>(gx + lx), .y = static_cast<float>(gy + ly)};
    };
    const auto line = [&p, &pt](double ax, double ay, double bx, double by) -> void {
        p.draw_line(pt(ax, ay), pt(bx, by), kZoomGlyphStrokeDp, kAccent);
    };
    const double g = kZoomGlyphDp;
    if (!collapse) {
        // 外向双箭头：中心 → 右上、中心 → 左下，箭头翼在各自的外角。
        line(g * 0.5, g * 0.5, g, 0.0);
        line(g, 0.0, g - kZoomArrowWingDp, 0.0);
        line(g, 0.0, g, kZoomArrowWingDp);
        line(g * 0.5, g * 0.5, 0.0, g);
        line(0.0, g, kZoomArrowWingDp, g);
        line(0.0, g, 0.0, g - kZoomArrowWingDp);
        return;
    }
    // 内向双箭头：左上角 → 中心、右下角 → 中心，箭头翼在中心一端。
    constexpr double kGap = 0.8;
    line(0.0, 0.0, g * 0.5 - kGap, g * 0.5 - kGap);
    line(g * 0.5 - kGap, g * 0.5 - kGap, g * 0.5 - kGap - kZoomArrowWingDp, g * 0.5 - kGap);
    line(g * 0.5 - kGap, g * 0.5 - kGap, g * 0.5 - kGap, g * 0.5 - kGap - kZoomArrowWingDp);
    line(g, g, g * 0.5 + kGap, g * 0.5 + kGap);
    line(g * 0.5 + kGap, g * 0.5 + kGap, g * 0.5 + kGap + kZoomArrowWingDp, g * 0.5 + kGap);
    line(g * 0.5 + kGap, g * 0.5 + kGap, g * 0.5 + kGap, g * 0.5 + kGap + kZoomArrowWingDp);
}

/// @brief 该命令是否改动拓扑或比例：缩放态下这些命令一律先还原再执行（D3），否则它们会读到
///        显示表里已被清空的把手与零盒。
[[nodiscard]] auto reshapes_layout(WorkspaceCommand command) -> bool {
    switch (command) {
        case WorkspaceCommand::SplitRight:
        case WorkspaceCommand::SplitDown:
        case WorkspaceCommand::ClosePane:
        case WorkspaceCommand::EqualizeLayer:
        case WorkspaceCommand::StepLeft:
        case WorkspaceCommand::StepRight:
        case WorkspaceCommand::StepUp:
        case WorkspaceCommand::StepDown:
            return true;
        case WorkspaceCommand::FocusLeft:
        case WorkspaceCommand::FocusRight:
        case WorkspaceCommand::FocusUp:
        case WorkspaceCommand::FocusDown:
        case WorkspaceCommand::ToggleZoom:
        case WorkspaceCommand::ToggleBroadcast:
            return false;
    }
    return false;
}

/// @brief 方向命令折成路由方向；非方向命令回空值。
[[nodiscard]] auto direction_of(WorkspaceCommand command) -> std::optional<PaneDirection> {
    switch (command) {
        case WorkspaceCommand::FocusLeft:
            return PaneDirection::Left;
        case WorkspaceCommand::FocusRight:
            return PaneDirection::Right;
        case WorkspaceCommand::FocusUp:
            return PaneDirection::Up;
        case WorkspaceCommand::FocusDown:
            return PaneDirection::Down;
        default:
            return std::nullopt;
    }
}

}  // namespace

WorkspaceView::WorkspaceView(std::shared_ptr<TerminalView> first_view, Hooks hooks)
    : hooks_(std::move(hooks)) {
    if (first_view == nullptr) {
        return;
    }
    // 本容器没有键盘输入，也不该是 Tab 停点：把手的 Press 因此被派发器判为「链上无可获焦者」，
    // 焦点的去留由本层在 Press 分支自己交代（见 on_pointer_event）。
    set_focusable(false);
    width(aurora::fill());
    height(aurora::fill());
    const PaneId first = tree_.focused();  // 单叶树的焦点即那唯一一格
    views_.emplace(first, first_view.get());
    wire_view(*first_view, first);
    add(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(first_view))});
    // 单叶工作区不画 pill（N4），但组可能是「上一轮留下的一个成员」：照现值刷一次，切分后
    // `split_pane` 再刷一次就会显形。
    refresh_broadcast_badges();
}

WorkspaceView::~WorkspaceView() { debounce_timer_.cancel(); }

auto WorkspaceView::pane_count() const -> std::size_t { return tree_.pane_count(); }

auto WorkspaceView::focused_pane() const -> PaneId {
    for (const auto &[pane, view] : views_) {
        if (view->is_focused()) {
            return pane;
        }
    }
    return last_focused_;
}

auto WorkspaceView::note_framework_focus() -> void {
    for (const auto &[pane, view] : views_) {
        if (view->is_focused()) {
            last_focused_ = pane;
            return;
        }
    }
}

auto WorkspaceView::view_of(PaneId pane) const -> TerminalView * {
    const auto it = views_.find(pane);
    return it == views_.end() ? nullptr : it->second;
}

auto WorkspaceView::flush_grid_sizes(Moment now) -> void {
    for (const auto &[pane, size] : debounce_.due(now)) {
        if (hooks_.dispatch_grid) {
            hooks_.dispatch_grid(pane, size);
        }
    }
}

auto WorkspaceView::refresh_broadcast_badges() -> void {
    const bool multi = pane_count() > 1U;
    // 按**视图**聚合「任一 pane 在组里」再落笔：装配层的分屏工厂今天仍可能让两个 pane 指向
    // 同一视图（F8），逐 pane 直写会让后一格的 false 抹掉前一格的 true——共享视图的角标因此
    // 变成「看遍历次序」的偶然结果。先聚合后写，每个视图只被写一次。
    std::map<TerminalView *, bool> member_of_view;
    for (const auto &[pane, view] : views_) {
        if (view == nullptr) {
            continue;
        }
        const bool member = hooks_.is_broadcast && hooks_.is_broadcast(pane);
        const auto [it, inserted] = member_of_view.try_emplace(view, member);
        if (!inserted) {
            it->second = it->second || member;
        }
    }
    for (const auto &[view, member] : member_of_view) {
        view->set_broadcast_badge(multi && member);
    }
}

auto WorkspaceView::restore_keyboard_focus() -> void {
    if (view_of(last_focused_) == nullptr) {
        return;  // 最后一次持焦的那一格已经关了：焦点去向由下一次点击或按键决定
    }
    apply_focus(last_focused_, aurora::FocusArrival::Keyboard);
}

auto WorkspaceView::split_focused(PaneAxis axis) -> void {
    unzoom();  // 与 `run_command` 的 `reshapes_layout` 同口径：结构命令先回到树的真相
    split_pane(tree_.focused(), axis);
}

auto WorkspaceView::replace_view(PaneId pane, std::shared_ptr<TerminalView> next) -> bool {
    if (next == nullptr) {
        return false;
    }
    TerminalView *old = view_of(pane);
    if (old == nullptr || old == next.get()) {
        return false;  // 该格不存在，或交进来的就是同一份视图（换了等于没换，别把它当成功）
    }
    aurora::Node *slot = nullptr;
    std::size_t holders = 0U;
    for (aurora::Node &child : child_nodes_mut()) {
        if (&child.widget() == old) {
            slot = &child;
            ++holders;
        }
    }
    if (holders == 0U) {
        AURORA_LOG_WARN("workspace", "replace_view: pane ", pane, " has no child node to swap");
        return false;
    }
    if (holders > 1U) {
        // 两格共用一份视图时「这一格的子节点」无从分辨，换下去必然让另一格指着已散的手柄。
        // 今天的生产工厂正是这副形态（`SPEC.FEAT.WS.02` 的多会话欠账），故宁可拒，不猜。
        AURORA_LOG_WARN("workspace", "replace_view: pane ", pane,
                        " shares its view with another pane, replacement is ambiguous");
        return false;
    }
    // ① 焦点先离格。旧视图若正持焦，它在本步之后即析构，而焦点表还指着它——下一次 `set_focus` 的
    //    blur 就会摸到已消散的控件。次序与 `close_pane` 的「先交接再摘子」是同一条纪律。
    const bool was_focused = old->is_focused();
    if (auto *manager = aurora::resolve_focus_manager(*this); manager != nullptr && was_focused) {
        manager->clear();
    }
    TerminalView *fresh = next.get();
    *slot = aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(next))};
    views_[pane] = fresh;
    // ② 新视图的挂载仍不归本层：与 `split_pane` 里那句 `add` 同一个待补挂机制，手写换子不会自动置位。
    note_pending_mount();
    // ③ 清掉这一格的去抖旧账：新会话此刻还在标称网格上，而表里若留着旧格已下发的那份行列，
    //    下一次折算值与之相等就会少发一次 `resize`，新格便永远停在 80×24（`close_pane` 同理）。
    debounce_.forget(pane);
    wire_view(*fresh, pane);
    if (was_focused) {
        apply_focus(pane, aurora::FocusArrival::Keyboard);
    }
    refresh_broadcast_badges();
    mark_needs_layout();
    mark_needs_paint();
    return true;
}

auto WorkspaceView::on_layout(const aurora::Constraints &c, const aurora::BuildContext &ctx) -> aurora::Size {
    // 无限 max 与视口同因：Flex 主轴给非加权子项的「按需上限」不是可信的可视宽度，沿用上一次的盒。
    const aurora::Size box =
        c.max.is_finite() ? c.constrain(aurora::Size{.width = c.max.width, .height = c.max.height}) : size_;
    const double scale = ctx.scale_factor > 0.0F ? static_cast<double>(ctx.scale_factor) : 1.0;
    spec_ = LayoutSpec{
        .divider_dp = kDividerHitDp, .min_pane_dp = min_pane_dp(), .scale = scale};
    area_ = Rect{.x = 0.0, .y = 0.0,
                 .width = static_cast<double>(box.width), .height = static_cast<double>(box.height)};
    base_layout_ = tree_.layout(area_, spec_);
    if (zoomed_.has_value() && std::ranges::none_of(base_layout_.boxes, [this](const PaneBox &pane_box) -> bool {
            return pane_box.pane == *zoomed_;
        })) {
        zoomed_.reset();  // 状态陈旧（树已无此格）：按未缩放处理，不让显示表留一格满盒
        hovered_zoom_.reset();
    }
    apply_display_layout();
    for (const PaneBox &pane_box : base_layout_.boxes) {
        TerminalView *view = view_of(pane_box.pane);
        if (view == nullptr) {
            continue;
        }
        // 缩放 pane 按满盒紧约束（行列数随之变大），**隐藏 pane 仍按树的原矩形**：会话网格因此
        // 一格不变，缩放与还原都不产生隐藏会话的 resize（D4 / F7）。
        const bool zoomed = zoomed_.has_value() && *zoomed_ == pane_box.pane;
        const aurora::Rect frame = to_frame(zoomed ? area_ : pane_box.box);
        // 紧约束（min == max）：树给的矩形就是这一格的最终尺寸，视口不得按需收缩——否则它返回的
        // 尺寸与节点 bounds 不一致，绘制落点与命中落点会分叉。
        view->layout(aurora::Constraints{.min = frame.size, .max = frame.size}, ctx);
    }
    for (aurora::Node &node : children_) {
        const auto it = std::ranges::find_if(base_layout_.boxes, [this, &node](const PaneBox &b) -> bool {
            return view_of(b.pane) == &node.widget();
        });
        if (it == base_layout_.boxes.end()) {
            continue;
        }
        const bool zoomed = zoomed_.has_value() && *zoomed_ == it->pane;
        // 隐藏 pane 的 bounds 留在原矩形而不是零盒：零 bounds 仍会被 `TerminalView::on_paint` 从
        // 容器原点画出来（F3），可见性因此由本层的绘制与命中原语显式跳过，不借几何表达。
        node.set_bounds(to_frame(zoomed ? area_ : it->box));
    }
    return box;
}

auto WorkspaceView::min_pane_dp() const -> double {
    double best = 0.0;
    for (const auto &[pane, view] : views_) {
        (void)pane;
        const GridGeometry &grid = view->grid_geometry();
        if (grid.cell_width <= 0.0 || grid.cell_height <= 0.0) {
            continue;  // 首帧度量未就绪：此时没有可信的格步长，算出的「最小」是 0
        }
        const double by_columns = kMinPaneColumns * grid.cell_width;
        const double by_rows = kMinPaneRows * grid.cell_height;
        best = std::max(best, std::max(by_columns, by_rows) + 2.0 * grid.padding);
    }
    // 一个视图都还没度量（首帧）时取库内保守下界，而不是 0——0 会让拖拽毫无钳位。
    return best > 0.0 ? best : LayoutSpec{}.min_pane_dp;
}

auto WorkspaceView::on_paint(aurora::Painter &p, const aurora::Rect &bounds, const aurora::BuildContext &ctx)
    -> void {
    p.fill_rect(bounds, kChrome);
    if (!zoomed_.has_value()) {
        Container::on_paint(p, bounds, ctx);
    } else {
        // 缩放态不能委托基类：隐藏 pane 的 bounds 仍在原矩形，`Container` 会照画不误（F3）。
        // 这里只画缩放那一格，其余跳过——它们是隐藏，不是「尺寸为零」。
        for (aurora::Node &node : children_) {
            const std::optional<PaneId> pane = pane_of(node.widget());
            if (!pane.has_value() || !pane_visible(*pane)) {
                continue;
            }
            const aurora::Rect cb = node.bounds();
            const aurora::Rect global{.origin = bounds.origin + cb.origin, .size = cb.size};
            node.widget().paint(p, global, ctx);
        }
    }
    const std::optional<DividerKey> lit = dragging_.has_value() ? dragging_ : hovered_;
    for (const PaneDivider &divider : layout_.dividers) {
        const DividerKey key{.container = divider.container, .slot = divider.slot};
        paint_handle(p, bounds, divider, (lit.has_value() && *lit == key) ? kAccent : kLine2);
    }
    // 只有分屏态画描边：单叶工作区没有「哪一格是焦点」这个问题（判据 11）；缩放 pane 的边界就是
    // 窗口边，描边不携带「哪一格是焦点」的信息（D6）。
    if (pane_count() > 1U && !zoomed_.has_value()) {
        const PaneId active = focused_pane();
        for (const PaneBox &box : layout_.boxes) {
            if (box.pane == active) {
                paint_focus_ring(p, bounds, box.box);
                break;
            }
        }
    }
    // 角标最后画，压在终端内容之上：缩放态常显（还原入口），未缩放态只在指针停进热区时显形。
    for (const PaneBox &box : layout_.boxes) {
        if (box.box.width <= 0.0 || box.box.height <= 0.0 || !pane_visible(box.pane)) {
            continue;
        }
        if (!zoomed_.has_value() && (!hovered_zoom_.has_value() || *hovered_zoom_ != box.pane)) {
            continue;
        }
        if (zoom_badge_box(box.box).has_value()) {
            paint_zoom_badge(p, bounds, box.box, zoomed_.has_value());
        }
    }
}

auto WorkspaceView::on_hit_test(const aurora::Point &local, const aurora::Rect &bounds,
                                const aurora::BuildContext &ctx) -> aurora::Widget * {
    // 角标叠在终端视图内部，必须由本层先接管（F4），否则 Press 会被视口吃掉。
    if (zoom_badge_at(local).has_value()) {
        return this;
    }
    if (zoomed_.has_value()) {
        // 缩放态不能委托基类：隐藏 pane 的旧 bounds 会挡住缩放那一格的点（F2/F4）。
        const auto display = std::ranges::find_if(layout_.boxes, [this](const PaneBox &box) -> bool {
            return box.pane == *zoomed_;
        });
        TerminalView *view = display == layout_.boxes.end() ? nullptr : view_of(*zoomed_);
        if (view == nullptr || display == layout_.boxes.end() || !to_frame(display->box).contains(local)) {
            return nullptr;
        }
        const aurora::Rect child = to_frame(display->box);
        return view->hit_test(local - child.origin,
                              aurora::Rect{.origin = bounds.origin + child.origin, .size = child.size}, ctx);
    }
    return divider_at(local).has_value() ? this : Container::on_hit_test(local, bounds, ctx);
}

auto WorkspaceView::on_hit_test_chain(const aurora::Point &local, const aurora::Rect &bounds,
                                      const aurora::BuildContext &ctx) -> std::vector<aurora::HitNode> {
    if (zoom_badge_at(local).has_value()) {
        return std::vector<aurora::HitNode>{aurora::HitNode{this, weak_from_this(), bounds.origin}};
    }
    if (zoomed_.has_value()) {
        const auto display = std::ranges::find_if(layout_.boxes, [this](const PaneBox &box) -> bool {
            return box.pane == *zoomed_;
        });
        TerminalView *view = display == layout_.boxes.end() ? nullptr : view_of(*zoomed_);
        if (view == nullptr || display == layout_.boxes.end() || !to_frame(display->box).contains(local)) {
            return {};
        }
        const aurora::Rect child = to_frame(display->box);
        return view->hit_test_chain(local - child.origin,
                                    aurora::Rect{.origin = bounds.origin + child.origin, .size = child.size},
                                    ctx);
    }
    // 缝隙里既无后代也无 Clickable / Input 修饰，基类因此不会把自身入链（点把手会被当点击空白丢弃）。
    if (divider_at(local).has_value()) {
        return std::vector<aurora::HitNode>{aurora::HitNode{this, weak_from_this(), bounds.origin}};
    }
    return Container::on_hit_test_chain(local, bounds, ctx);
}

auto WorkspaceView::on_pointer_event(aurora::MouseEvent &e) -> void {
    note_framework_focus();  // 读数须在 switch 之前取：Press 分支里框架焦点已被派发器清空
    switch (e.action) {
        case aurora::MouseAction::Press: {
            if (e.button != aurora::MouseButton::Left) {
                break;
            }
            // 角标先于把手：它叠在 pane 内部，本层的命中链已把它认领到这里（F4）。
            if (const std::optional<PaneId> badge = zoom_badge_at(e.local_position); badge.has_value()) {
                apply_focus(*badge, aurora::FocusArrival::Pointer);
                toggle_zoom(*badge);
                e.is_handled = true;
                return;
            }
            const std::optional<DividerKey> key = divider_at(e.local_position);
            if (!key.has_value()) {
                break;  // 落在 pane 上：命中链里根本没有本层，交给视口处理
            }
            dragging_ = key;
            drag_axis_dp_ = along_axis(*key, e.local_position).value_or(0.0);
            // 派发器在 Press 前已按「链上第一个可获焦者」定焦点，而本层不可获焦 ⇒ 焦点刚被清成空。
            // 拖把手不该让活动 pane 失焦（否则拖完那一格的键盘输入就发不进会话了）。
            apply_focus(focused_pane(), aurora::FocusArrival::Pointer);
            if (e.click_count >= 2) {
                static_cast<void>(tree_.equalize(key->container));  // 双击该把手 ⇒ 本层等分（判据 9）
                hovered_ = key;
                mark_needs_layout();
            }
            mark_needs_paint();
            e.is_handled = true;
            return;
        }
        case aurora::MouseAction::Move: {
            if (dragging_.has_value()) {
                const std::optional<double> now = along_axis(*dragging_, e.local_position);
                if (!now.has_value()) {
                    dragging_.reset();  // 该层在拖拽期间塌缩（会话侧关掉了兄弟 pane）
                    break;
                }
                if (const PaneDivider *divider = divider_of(*dragging_); divider != nullptr) {
                    static_cast<void>(tree_.move_divider(*divider, now.value() - drag_axis_dp_, spec_));
                }
                drag_axis_dp_ = now.value();
                mark_needs_layout();
                e.is_handled = true;
                return;
            }
            const std::optional<PaneId> badge = zoom_badge_at(e.local_position);
            if (badge != hovered_zoom_) {
                hovered_zoom_ = badge;
                mark_needs_paint();  // 角标一进一出只换绘制态，不重排
            }
            const std::optional<DividerKey> key = divider_at(e.local_position);
            if (key != hovered_) {
                hovered_ = key;
                mark_needs_paint();  // 命中区不变，只换把手颜色：一进一出会抖动（判据 6）
            }
            break;
        }
        case aurora::MouseAction::Release: {
            if (!dragging_.has_value()) {
                break;
            }
            dragging_.reset();  // 松手不再改比例；尺寸的下发由去抖尾沿负责（判据 7 与 24）
            e.is_handled = true;
            return;
        }
    }
    aurora::Widget::on_pointer_event(e);
}

auto WorkspaceView::cursor_shape() const -> std::optional<aurora::CursorShape> {
    if (hovered_zoom_.has_value()) {
        return aurora::CursorShape::PointingHand;
    }
    const std::optional<DividerKey> key = dragging_.has_value() ? dragging_ : hovered_;
    if (!key.has_value()) {
        return std::nullopt;
    }
    const PaneDivider *divider = divider_of(*key);
    if (divider == nullptr) {
        return std::nullopt;
    }
    return divider->axis == PaneAxis::Horizontal ? aurora::CursorShape::ResizeEW : aurora::CursorShape::ResizeNS;
}

auto WorkspaceView::type_name() const -> const char * { return "WorkspaceView"; }

auto WorkspaceView::wire_view(TerminalView &view, PaneId pane) -> void {
    view.set_grid_size_sink([this, pane](GridSize expected) -> void {
        debounce_.request(pane, expected, std::chrono::steady_clock::now());
        arm_debounce();
    });
    view.set_key_pre_filter([this, pane](const term::KeyPress &press) -> bool { return on_key_press(pane, press); });
    // 两条镜像与前置过滤同一处装：源格已发完自己那份，装配层按会话去重后扇出（D5/D7）。
    view.set_input_mirrors(
        [this, pane](const term::KeyPress &press) -> void {
            if (hooks_.mirror_key) {
                hooks_.mirror_key(pane, press);
            }
        },
        [this, pane](std::u32string_view text) -> void {
            if (hooks_.mirror_text) {
                hooks_.mirror_text(pane, text);
            }
        });
}

auto WorkspaceView::on_key_press(PaneId pane, const term::KeyPress &press) -> bool {
    const std::optional<WorkspaceCommand> command = workspace_command(press);
    if (!command.has_value()) {
        return false;  // 本层不认领：该键照原路编码发会话（`SPEC.FEAT.INTERACT.01` 的转发腿不可让渡）
    }
    // 树里的焦点只是框架焦点的投影：指针点过 B 而树仍记 A 时，拿投影当命令对象会把焦点弹回 A。
    last_focused_ = pane;  // 收到该键的视图就是当前持焦者——按键是框架焦点的直接证词
    static_cast<void>(tree_.set_focus(pane));
    if (zoomed_.has_value() && *zoomed_ != pane) {
        // 框架侧先改了焦点（标签条派焦点、浮层归还）：缩放跟到真正收到键的那一格（D2），
        // 否则输入会发进屏上看得见、却已不是持焦者的那一格。
        zoomed_ = pane;
        hovered_zoom_.reset();
        hovered_.reset();
        apply_display_layout();
        mark_needs_layout();
        mark_needs_paint();
    }
    run_command(*command, pane);
    return true;  // 命中键位但落点不存在（单叶按方向键）也算认领——它不是发给会话的输入
}

auto WorkspaceView::run_command(WorkspaceCommand command, PaneId pane) -> void {
    // 结构命令在缩放态下先还原：它们要读的把手与兄弟盒在显示表里已被清空/归零（D3）。
    if (reshapes_layout(command)) {
        unzoom();
    }
    switch (command) {
        case WorkspaceCommand::SplitRight:
            split_pane(pane, PaneAxis::Horizontal);
            return;
        case WorkspaceCommand::SplitDown:
            split_pane(pane, PaneAxis::Vertical);
            return;
        case WorkspaceCommand::ClosePane:
            close_pane(pane);
            return;
        case WorkspaceCommand::EqualizeLayer: {
            const std::optional<ContainerId> container = tree_.container_of(pane);
            if (container.has_value() && tree_.equalize(*container)) {
                mark_needs_layout();
                mark_needs_paint();
            }
            return;
        }
        case WorkspaceCommand::FocusLeft:
        case WorkspaceCommand::FocusRight:
        case WorkspaceCommand::FocusUp:
        case WorkspaceCommand::FocusDown: {
            const std::optional<PaneDirection> direction = direction_of(command);
            // 缩放态按**未缩放**的几何路由：显示表里其余 pane 全是零盒，拿它路由永远没有目标。
            const PaneLayout &routing = zoomed_.has_value() ? base_layout_ : layout_;
            const std::optional<PaneId> target = route_focus(pane, *direction, routing);
            if (target.has_value()) {
                apply_focus(*target, aurora::FocusArrival::Keyboard);
                if (zoomed_.has_value() && *zoomed_ != *target) {
                    // 缩放跟随焦点（D2）：屏上永远只有当前格，输入与可见内容因此不分叉。
                    zoomed_ = *target;
                    hovered_zoom_.reset();
                    hovered_.reset();
                    apply_display_layout();
                    mark_needs_layout();
                    mark_needs_paint();
                }
            }
            return;  // 无候选即焦点原地不动，不绕到对角（判据 13）
        }
        case WorkspaceCommand::StepLeft:
        case WorkspaceCommand::StepRight:
        case WorkspaceCommand::StepUp:
        case WorkspaceCommand::StepDown:
            step_divider(command, pane);
            return;
        case WorkspaceCommand::ToggleZoom:
            toggle_zoom(pane);
            return;
        case WorkspaceCommand::ToggleBroadcast: {
            // 组身份归装配层的路由器；本层只转交再按现读状态重画角标（`SPEC.FEAT.WS.09` D2/D9）。
            // 单 pane 也允许入组（D4）：组可以只有一个成员，只是 pill 要等分屏后才显形。
            if (hooks_.toggle_broadcast) {
                static_cast<void>(hooks_.toggle_broadcast(pane));
            }
            refresh_broadcast_badges();
            return;
        }
    }
}

auto WorkspaceView::split_pane(PaneId target, PaneAxis axis) -> void {
    if (!hooks_.make_pane) {
        AURORA_LOG_WARN("workspace", "split requested but no pane factory is wired");
        return;
    }
    const PaneId pane = next_pane_++;  // 标识只增不复用：回收掉的号不留给下一格
    std::shared_ptr<TerminalView> created = hooks_.make_pane(pane, target);
    if (created == nullptr) {
        AURORA_LOG_WARN("workspace", "pane factory returned null, tree unchanged for pane ", pane);
        return;
    }
    if (!tree_.split(target, axis, pane)) {
        if (hooks_.teardown_pane) {
            hooks_.teardown_pane(pane);
        }
        return;  // 被切的不是现存叶子：树不动，刚造的会话因此立刻回收
    }
    views_.emplace(pane, created.get());
    wire_view(*created, pane);
    add(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(created))});
    // 新视图的挂载不归本层：`Container::add` 登记待补挂，框架在下一次布局入口以父侧 ctx 挂上
    // （G35 回货；裁决 7.49④ 那处自备 ctx 的补偿据此撤除）。
    apply_focus(pane, aurora::FocusArrival::Keyboard);  // 新格当场选中（判据 1）
    refresh_broadcast_badges();  // 1 → 2 格：成员的 pill 从这一帧起显形（N4）
    mark_needs_paint();
}

auto WorkspaceView::close_pane(PaneId pane) -> void {
    if (pane_count() <= 1U) {
        // TODO(SPEC.FEAT.WS.01): 末位 pane 的关闭是「关标签 / 关窗口」，判据只在那一处（裁决 7.42⑤
        // 与 7.43④ 同口径），本层不代劳也不在此判定退出。
        return;
    }
    TerminalView *view = view_of(pane);
    if (view == nullptr || !tree_.close(pane)) {
        return;
    }
    // 焦点先交接再摘子：`remove_child` 释放节点的最后一份强引用即销毁视图，而框架的焦点表此刻
    // 还指着它——先销毁后交接会让持焦控件变成悬空指针。
    apply_focus(tree_.focused(), aurora::FocusArrival::Keyboard);
    static_cast<void>(remove_child(view));
    views_.erase(pane);
    debounce_.forget(pane);  // 残留的已下发值会让将来同号的 pane 少发一次 resize
    if (zoomed_.has_value() && *zoomed_ == pane) {
        zoomed_.reset();  // 关掉缩放 pane 即退出缩放；键盘路径已先还原，这里兜住其余调用方
        apply_display_layout();
        mark_needs_layout();
    }
    hovered_zoom_.reset();
    hovered_.reset();
    dragging_.reset();
    if (hooks_.teardown_pane) {
        hooks_.teardown_pane(pane);
    }
    refresh_broadcast_badges();  // 2 → 1 格：余下那格即使仍在组里也不再画 pill（N4）
    mark_needs_paint();
}

auto WorkspaceView::step_divider(WorkspaceCommand command, PaneId pane) -> void {
    const std::optional<ContainerId> container = tree_.container_of(pane);
    if (!container.has_value()) {
        return;  // 单叶工作区：没有层，也就没有把手可推
    }
    const PaneDivider *sample = nullptr;
    const std::size_t focused_slot = focused_slot_in(*container, pane, sample);
    if (sample == nullptr) {
        return;
    }
    std::size_t dividers_in_layer = 0U;
    for (const PaneDivider &divider : layout_.dividers) {
        if (divider.container == *container) {
            ++dividers_in_layer;
        }
    }
    const std::optional<DividerStep> step =
        divider_step(command, focused_slot, dividers_in_layer + 1U, sample->axis, sample->extent_dp);
    if (!step.has_value()) {
        return;
    }
    const PaneDivider *divider = divider_of(DividerKey{.container = *container, .slot = step->slot});
    if (divider == nullptr) {
        return;
    }
    // 与拖拽同一条路径（判据 9）：位移交给 `move_divider`，钳位与吸附都不在这里重算一遍。
    if (tree_.move_divider(*divider, step->delta_dp, spec_)) {
        mark_needs_layout();
        mark_needs_paint();
    }
}

auto WorkspaceView::toggle_zoom(PaneId pane) -> void {
    if (pane_count() <= 1U) {
        return;  // 没有可放大的对象；键仍由 `on_key_press` 认领，不转发给会话（D9）
    }
    if (zoomed_.has_value() && *zoomed_ == pane) {
        unzoom();
        return;
    }
    zoomed_ = pane;
    hovered_zoom_.reset();  // 角标随布局换位，旧悬停读数不再成立
    hovered_.reset();
    dragging_.reset();
    apply_display_layout();
    mark_needs_layout();
    mark_needs_paint();
}

auto WorkspaceView::unzoom() -> void {
    if (!zoomed_.has_value()) {
        return;
    }
    zoomed_.reset();
    hovered_zoom_.reset();
    hovered_.reset();
    dragging_.reset();
    // 显示表立刻回到树的真相：紧随其后的结构命令（切分/等分/步进）要在本帧读到把手与兄弟盒，
    // 只标脏等下一帧重排会把它们折在空表上。
    apply_display_layout();
    mark_needs_layout();
    mark_needs_paint();
}

auto WorkspaceView::apply_display_layout() -> void {
    layout_ = base_layout_;
    if (!zoomed_.has_value()) {
        return;
    }
    for (PaneBox &pane_box : layout_.boxes) {
        pane_box.box = pane_box.pane == *zoomed_ ? area_ : Rect{};
    }
    layout_.dividers.clear();  // 缩放态没有可见的缝隙，把手也不该留在命中表里
}

auto WorkspaceView::pane_visible(PaneId pane) const -> bool {
    return !zoomed_.has_value() || *zoomed_ == pane;
}

auto WorkspaceView::pane_of(const aurora::Widget &widget) const -> std::optional<PaneId> {
    for (const auto &[pane, view] : views_) {
        if (view == &widget) {
            return pane;
        }
    }
    return std::nullopt;
}

auto WorkspaceView::zoom_badge_box(const Rect &pane) const -> std::optional<Rect> {
    constexpr double kNeeded = kZoomBadgeDp + kZoomBadgeInsetDp;
    if (pane.width < kNeeded || pane.height < kNeeded) {
        return std::nullopt;  // 放不下就整枚不画也不给热区，不让它越出 pane
    }
    return Rect{.x = pane.x + pane.width - kNeeded, .y = pane.y + kZoomBadgeInsetDp,
                .width = kZoomBadgeDp, .height = kZoomBadgeDp};
}

auto WorkspaceView::zoom_badge_at(const aurora::Point &local) const -> std::optional<PaneId> {
    if (pane_count() <= 1U) {
        return std::nullopt;  // 单 pane 不提供角标（D9）
    }
    for (const PaneBox &pane_box : layout_.boxes) {
        if (pane_box.box.width <= 0.0 || pane_box.box.height <= 0.0) {
            continue;  // 缩放态下隐藏 pane 是零盒，不可达
        }
        const std::optional<Rect> badge = zoom_badge_box(pane_box.box);
        if (badge.has_value() && to_frame(*badge).contains(local)) {
            return pane_box.pane;
        }
    }
    return std::nullopt;
}

auto WorkspaceView::apply_focus(PaneId pane, aurora::FocusArrival arrival) -> void {
    last_focused_ = pane;
    static_cast<void>(tree_.set_focus(pane));
    TerminalView *view = view_of(pane);
    if (view != nullptr) {
        // 派发栈内取派发器登记的当前管理器；栈外（构造、装配阶段）退回按根登记的兜底实例。
        // 两处都拿不到时只改模型投影——框架焦点随后由装配层设初始焦点时补齐。
        if (aurora::FocusManager *manager = aurora::resolve_focus_manager(*this); manager != nullptr) {
            manager->set_focus(view, aurora::FocusDirection::Forward, arrival);
        }
    }
    mark_needs_paint();  // 焦点描边由本层绘制，改焦点的两端都要重画
}

auto WorkspaceView::divider_at(const aurora::Point &local) const -> std::optional<DividerKey> {
    for (const PaneDivider &divider : layout_.dividers) {
        if (to_frame(divider.box).contains(local)) {
            return DividerKey{.container = divider.container, .slot = divider.slot};
        }
    }
    return std::nullopt;
}

auto WorkspaceView::divider_of(const DividerKey &key) const -> const PaneDivider * {
    const auto it = std::ranges::find_if(layout_.dividers, [&key](const PaneDivider &divider) -> bool {
        return divider.container == key.container && divider.slot == key.slot;
    });
    return it == layout_.dividers.end() ? nullptr : &*it;
}

auto WorkspaceView::focused_slot_in(ContainerId container, PaneId pane,
                                    const PaneDivider *&layer_sample) const -> std::size_t {
    layer_sample = nullptr;
    for (const PaneDivider &divider : layout_.dividers) {
        if (divider.container == container) {
            layer_sample = &divider;
            break;
        }
    }
    if (layer_sample == nullptr) {
        return 0U;  // 该层已塌缩或标识陈旧：没有把手也就没有槽位可言
    }
    const auto box = std::ranges::find_if(layout_.boxes, [pane](const PaneBox &b) -> bool {
        return b.pane == pane;
    });
    if (box == layout_.boxes.end()) {
        return 0U;
    }
    const bool along_x = layer_sample->axis == PaneAxis::Horizontal;
    const double pane_start = along_x ? box->box.x : box->box.y;
    std::size_t slot = 0U;
    for (const PaneDivider &divider : layout_.dividers) {
        if (divider.container != container) {
            continue;
        }
        if ((along_x ? divider.box.x : divider.box.y) < pane_start) {
            ++slot;
        }
    }
    return slot;
}

auto WorkspaceView::along_axis(const DividerKey &key, const aurora::Point &local) const -> std::optional<double> {
    const PaneDivider *divider = divider_of(key);
    if (divider == nullptr) {
        return std::nullopt;
    }
    return divider->axis == PaneAxis::Horizontal ? static_cast<double>(local.x) : static_cast<double>(local.y);
}

auto WorkspaceView::arm_debounce() -> void {
    debounce_timer_.cancel();  // 取消-重排：静默窗口随每次请求往后推（尾沿去抖）
    const std::optional<Moment> deadline = debounce_.next_deadline();
    if (!deadline.has_value()) {
        return;
    }
    aurora::Scheduler *scheduler = aurora::Scheduler::current();
    if (scheduler == nullptr) {
        // 无运行中的调度器即无帧循环（无头用例与装配早期）：期望值留在表里，由调用方 `flush_grid_sizes`
        // 按给定时刻结算，本层不自造时钟。
        return;
    }
    const Moment now = std::chrono::steady_clock::now();
    const auto delay =
        std::max(aurora::Scheduler::Duration::zero(), std::chrono::duration_cast<aurora::Scheduler::Duration>(
                                                          *deadline - now));
    debounce_timer_ = scheduler->set_timeout(delay, [this]() -> void {
        flush_grid_sizes(std::chrono::steady_clock::now());
    });
}

}  // namespace borealis::ui
