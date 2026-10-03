#include "workspace_view.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
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

auto WorkspaceView::on_layout(const aurora::Constraints &c, const aurora::BuildContext &ctx) -> aurora::Size {
    // 无限 max 与视口同因：Flex 主轴给非加权子项的「按需上限」不是可信的可视宽度，沿用上一次的盒。
    const aurora::Size box =
        c.max.is_finite() ? c.constrain(aurora::Size{.width = c.max.width, .height = c.max.height}) : size_;
    const double scale = ctx.scale_factor > 0.0F ? static_cast<double>(ctx.scale_factor) : 1.0;
    spec_ = LayoutSpec{
        .divider_dp = kDividerHitDp, .min_pane_dp = min_pane_dp(), .scale = scale};
    layout_ = tree_.layout(Rect{.x = 0.0, .y = 0.0,
                                .width = static_cast<double>(box.width), .height = static_cast<double>(box.height)},
                           spec_);
    for (const PaneBox &pane_box : layout_.boxes) {
        TerminalView *view = view_of(pane_box.pane);
        if (view == nullptr) {
            continue;
        }
        const aurora::Rect frame = to_frame(pane_box.box);
        // 紧约束（min == max）：树给的矩形就是这一格的最终尺寸，视口不得按需收缩——否则它返回的
        // 尺寸与节点 bounds 不一致，绘制落点与命中落点会分叉。
        view->layout(aurora::Constraints{.min = frame.size, .max = frame.size}, ctx);
    }
    for (aurora::Node &node : children_) {
        const auto it = std::ranges::find_if(layout_.boxes, [this, &node](const PaneBox &b) -> bool {
            return view_of(b.pane) == &node.widget();
        });
        if (it != layout_.boxes.end()) {
            node.set_bounds(to_frame(it->box));
        }
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
    Container::on_paint(p, bounds, ctx);
    const std::optional<DividerKey> lit = dragging_.has_value() ? dragging_ : hovered_;
    for (const PaneDivider &divider : layout_.dividers) {
        const DividerKey key{.container = divider.container, .slot = divider.slot};
        paint_handle(p, bounds, divider, (lit.has_value() && *lit == key) ? kAccent : kLine2);
    }
    // 只有分屏态画描边：单叶工作区没有「哪一格是焦点」这个问题（判据 11）。
    if (pane_count() > 1U) {
        const PaneId active = focused_pane();
        for (const PaneBox &box : layout_.boxes) {
            if (box.pane == active) {
                paint_focus_ring(p, bounds, box.box);
                break;
            }
        }
    }
}

auto WorkspaceView::on_hit_test(const aurora::Point &local, const aurora::Rect &bounds,
                                const aurora::BuildContext &ctx) -> aurora::Widget * {
    return divider_at(local).has_value() ? this : Container::on_hit_test(local, bounds, ctx);
}

auto WorkspaceView::on_hit_test_chain(const aurora::Point &local, const aurora::Rect &bounds,
                                      const aurora::BuildContext &ctx) -> std::vector<aurora::HitNode> {
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

auto WorkspaceView::on_mount(const aurora::BuildContext &ctx) -> void {
    mounted_ = true;
    mount_ctx_ = ctx;
    Container::on_mount(ctx);
}

auto WorkspaceView::cursor_shape() const -> std::optional<aurora::CursorShape> {
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
}

auto WorkspaceView::on_key_press(PaneId pane, const term::KeyPress &press) -> bool {
    const std::optional<WorkspaceCommand> command = workspace_command(press);
    if (!command.has_value()) {
        return false;  // 本层不认领：该键照原路编码发会话（`SPEC.FEAT.INTERACT.01` 的转发腿不可让渡）
    }
    // 树里的焦点只是框架焦点的投影：指针点过 B 而树仍记 A 时，拿投影当命令对象会把焦点弹回 A。
    last_focused_ = pane;  // 收到该键的视图就是当前持焦者——按键是框架焦点的直接证词
    static_cast<void>(tree_.set_focus(pane));
    run_command(*command, pane);
    return true;  // 命中键位但落点不存在（单叶按方向键）也算认领——它不是发给会话的输入
}

auto WorkspaceView::run_command(WorkspaceCommand command, PaneId pane) -> void {
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
            const std::optional<PaneId> target = route_focus(pane, *direction, layout_);
            if (target.has_value()) {
                apply_focus(*target, aurora::FocusArrival::Keyboard);
            }
            return;  // 无候选即焦点原地不动，不绕到对角（判据 13）
        }
        case WorkspaceCommand::StepLeft:
        case WorkspaceCommand::StepRight:
        case WorkspaceCommand::StepUp:
        case WorkspaceCommand::StepDown:
            step_divider(command, pane);
            return;
    }
}

auto WorkspaceView::split_pane(PaneId target, PaneAxis axis) -> void {
    if (!hooks_.make_pane) {
        AURORA_LOG_WARN("workspace", "split requested but no pane factory is wired");
        return;
    }
    const PaneId pane = next_pane_++;  // 标识只增不复用：回收掉的号不留给下一格
    std::shared_ptr<TerminalView> created = hooks_.make_pane(pane);
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
    if (mounted_) {
        // 根只在场景切换时挂载一次，运行期新加的子节点不在那次遍历里；不补 mount 则该格的闪烁档
        // 与主题订阅永不注册（症状是新 pane 有画面但光标不闪）。
        view_of(pane)->mount(mount_ctx_);
    }
    apply_focus(pane, aurora::FocusArrival::Keyboard);  // 新格当场选中（判据 1）
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
    hovered_.reset();
    dragging_.reset();
    if (hooks_.teardown_pane) {
        hooks_.teardown_pane(pane);
    }
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
