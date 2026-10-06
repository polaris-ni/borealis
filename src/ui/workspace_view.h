#pragma once

// ============================================================
// 工作区分屏容器（src/ui/workspace_view.h，私有头）
// ------------------------------------------------------------
// 裁决 D1①：类声明刻意留在 `src/`——含此头即含框架头，`include/borealis/` 的「公共头不含 Aurora
// 类型」纪律会因此破洞。消费方只有 `src/ui/workspace_view.cpp` 与装配层（`src/main.cpp`）、用例。
//
// 本层是 `SPEC.FEAT.WS.02` 界面腿的宿主，也是 `SPEC.FEAT.XFER.01` 去抖腿的落点（裁决 7.47 / 7.48）。
// 它**只做接线**，三段算式都不在这里重述：拓扑与矩形归 `ui::PaneTree`，键位与步进归
// `ui::workspace_command` / `ui::divider_step`，格子与行列数归 `ui::TerminalView`。
//
// 三条决定形态的实测事实：
// - **框架焦点是焦点的唯一权威**：`TerminalView` 在自己的 Press 分支置 `is_handled`，祖先收不到
//   pane 上的点击，故模型焦点无法靠指针事件同步。本层的读焦点一律遍历视图问 `is_focused()`，
//   `PaneTree::focused()` 只是它的投影，改焦点经 `apply_focus` 两处一并写。
// - **把手区必须显式入命中链**：基类只在「后代链非空 / 可点击 / 可滚动 / 有 Input 修饰」时把自身
//   入链，缝隙里既无后代也无修饰，不显式返回就被当点击空白丢弃（与框架 `Splitter` 同法）。
// - **新 pane 的视图不用本层挂载**：`Container::add` 只登记待补挂，真正的 `mount(ctx)` 由框架在
//   下一次布局入口以父侧 ctx 完成（Aurora 9202ec46 闭合缺口 G35）。本层若自备一份 ctx 去 `mount`，
//   就是把框架的生命周期动作搬进应用侧（裁决 7.49④ 的补偿据此撤除）。
// ============================================================

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "aurora/app/scheduler.h"
#include "aurora/environment/build_context.h"
#include "aurora/widget/widget.h"
#include "borealis/ui/grid_size_debounce.h"
#include "borealis/ui/pane_tree.h"
#include "borealis/ui/workspace_keys.h"
#include "terminal_view.h"

namespace borealis::ui {

/// @brief 分屏工作区容器：按 pane 树落子矩形、拖把手、路由焦点、合并逐 pane 的行列下发。
class WorkspaceView final : public aurora::Container {
  public:
    /// @brief 切分新 pane 时造一份视图（连它自己的会话）；返回空即工厂失败，树因此不动。
    using PaneViewFactory = std::function<std::shared_ptr<TerminalView>(PaneId pane)>;
    /// @brief 去抖尾沿到点：把某 pane 的最新期望行列交出去（生产侧即 `Session::resize`）。
    using GridSizeDispatch = std::function<void(PaneId pane, GridSize size)>;
    /// @brief 视图已从容器摘除后回收该 pane 的会话；与工厂成对，故生命周期归属只在装配层。
    using PaneTeardown = std::function<void(PaneId pane)>;

    /// @brief 本层与装配层的三条接缝（与 `TerminalView::Presentation` 同形态的 struct of 回调）。
    struct Hooks {
        PaneViewFactory make_pane;
        GridSizeDispatch dispatch_grid;
        PaneTeardown teardown_pane;
    };

    /// @brief 以单叶工作区起步：@p first_view 挂在 pane 标识 1 上。
    /// @param first_view 首个 pane 的视图，本容器接管其所有权。
    /// @param hooks 三条接缝。
    WorkspaceView(std::shared_ptr<TerminalView> first_view, Hooks hooks);

    /// @brief 注销去抖定时器：句柄只置取消标志，故析构期安全（与 `TerminalView` 同形态）。
    ~WorkspaceView() override;

    WorkspaceView(const WorkspaceView &) = delete;
    auto operator=(const WorkspaceView &) -> WorkspaceView & = delete;
    WorkspaceView(WorkspaceView &&) = delete;
    auto operator=(WorkspaceView &&) -> WorkspaceView & = delete;

    // ---- 观测面：用例的判据入口，装配层不需要 ----

    /// @brief 当前 pane 数。
    [[nodiscard]] auto pane_count() const -> std::size_t;

    /// @brief 当前活动的 pane（框架焦点优先，见文件头第一条）。
    [[nodiscard]] auto focused_pane() const -> PaneId;

    /// @brief 某 pane 的视图；该 pane 不存在时回空指针。
    /// @param pane 目标 pane。
    [[nodiscard]] auto view_of(PaneId pane) const -> TerminalView *;

    /// @brief 最近一次折算出的矩形表（把手命中区与各 pane 盒）。
    [[nodiscard]] auto current_layout() const noexcept -> const PaneLayout & { return layout_; }

    /// @brief 立刻结算去抖表：把到期的期望行列逐条交出（无调度器时用例靠它推进尾沿）。
    /// @param now 当前时刻。
    auto flush_grid_sizes(Moment now) -> void;

  protected:
    /// @brief 折算树、按矩形以紧约束排每个视图，并把矩形写回节点 bounds。
    [[nodiscard]] auto on_layout(const aurora::Constraints &c, const aurora::BuildContext &ctx)
        -> aurora::Size override;

    /// @brief chrome 底 → 各 pane 自身 → 把手中段 → 焦点 pane 内描边。
    auto on_paint(aurora::Painter &p, const aurora::Rect &bounds, const aurora::BuildContext &ctx)
        -> void override;

    /// @brief 把手区命中自身，其余转 `Container::on_hit_test`。
    [[nodiscard]] auto on_hit_test(const aurora::Point &local, const aurora::Rect &bounds,
                                   const aurora::BuildContext &ctx) -> aurora::Widget * override;

    /// @brief 把手区返回「仅自身」的单元素链；否则转 `Container::on_hit_test_chain`。
    [[nodiscard]] auto on_hit_test_chain(const aurora::Point &local, const aurora::Rect &bounds,
                                         const aurora::BuildContext &ctx) -> std::vector<aurora::HitNode> override;

    /// @brief 把手的 Press / 拖拽 Move / Release，与非拖拽 Move 的 hover 态。
    auto on_pointer_event(aurora::MouseEvent &e) -> void override;

    /// @brief 把手上的悬停光标：沿该层排布轴给双向箭头。
    [[nodiscard]] auto cursor_shape() const -> std::optional<aurora::CursorShape> override;

    [[nodiscard]] auto type_name() const -> const char * override;

  private:
    /// @brief 一条把手的身份：容器层 + 该层内槽位（与 `PaneDivider` 的同两名一致）。
    ///
    /// 只存身份而不存 `PaneDivider` 副本：矩形每帧重算，副本会在两次拖拽 Move 之间变陈旧，而陈旧
    /// 的 `extent_dp` 会让钳位算式对不上（裁决 7.42⑦ 要求两处共用同一次折算）。
    struct DividerKey {
        ContainerId container{};
        std::size_t slot = 0;

        [[nodiscard]] auto operator==(const DividerKey &other) const noexcept -> bool = default;
    };

    /// @brief 给一个视图装上期望行列与按键认领两条闭包（切分与构造共用）。
    auto wire_view(TerminalView &view, PaneId pane) -> void;

    /// @brief 按键认领：命中键位表即改模型焦点并处置命令，返回 true 表示该键不再发会话。
    [[nodiscard]] auto on_key_press(PaneId pane, const term::KeyPress &press) -> bool;

    /// @brief 处置一条分屏命令（@p pane 是收到该键的那个视图，而非树的投影）。
    auto run_command(WorkspaceCommand command, PaneId pane) -> void;

    /// @brief 切分：先要会话与视图，再动树；树拒绝时把刚造的 pane 立刻回收。
    auto split_pane(PaneId target, PaneAxis axis) -> void;

    /// @brief 关闭一个 pane：先交接焦点，再摘视图，最后回收会话。
    auto close_pane(PaneId pane) -> void;

    /// @brief 键盘步进：折出该层的落点与位移，交 `move_divider`（与拖拽同一条路径）。
    auto step_divider(WorkspaceCommand command, PaneId pane) -> void;

    /// @brief 改焦点：树的投影与框架焦点一并写（后者只在派发栈内有管理器可寻）。
    /// @param pane 目标 pane。
    /// @param arrival 到达方式：指针点选记 `Pointer`，键盘路由与切分/关闭的交接记 `Keyboard`。视口
    ///        不画框架的统一焦点环（`wants_focus_ring()` 为 false），故这里只影响框架内部状态。
    auto apply_focus(PaneId pane, aurora::FocusArrival arrival = aurora::FocusArrival::Programmatic) -> void;

    /// @brief 把手落点：@p local 在本容器局部坐标；命中即该条把手。
    [[nodiscard]] auto divider_at(const aurora::Point &local) const -> std::optional<DividerKey>;

    /// @brief 按身份找回最近一次布局里的那条把手；层已塌缩或槽位越界时回空。
    [[nodiscard]] auto divider_of(const DividerKey &key) const -> const PaneDivider *;

    /// @brief 该 pane 的直接父层里，焦点格之前有几条把手（即其直接子槽位）。
    [[nodiscard]] auto focused_slot_in(ContainerId container, PaneId pane,
                                        const PaneDivider *&layer_sample) const -> std::size_t;

    /// @brief 指针落点在该把手所在层排布轴上的坐标（dp，本容器局部系）；该层已塌缩时回空值。
    ///
    /// 拖拽按**增量**折算：每次 Move 都拿当前读数减上一次的读数，故矩形在两次派发之间被重算过
    /// 也不影响跟手（拿绝对差值会因陈旧矩形算错）。
    [[nodiscard]] auto along_axis(const DividerKey &key, const aurora::Point &local) const
        -> std::optional<double>;

    /// @brief 把「框架此刻认为持焦的那个 pane」记进 `last_focused_`（只有真有视图报告持焦才记）。
    ///
    /// 派发器在把手 Press **之前**就把焦点清成空（链上只有不可获焦的本层），那一刻全员 `is_focused()`
    /// 为假，活动 pane 只能取最后一次真实读数。补记的两条事件都真实存在：指针从格子里走到缝隙必然
    /// 经过一次落在本层的 hover Move，而每次按键认领都发生在当前持焦视图上。
    auto note_framework_focus() -> void;

    /// @brief 期望入表后排一次尾沿定时器：取消-重排，故静默窗口随每次请求往后推。
    auto arm_debounce() -> void;

    /// @brief 本帧生效的最小 pane 边长（dp）：`max(20 列 × 格宽, 3 行 × 格高)` 再加内边距。
    [[nodiscard]] auto min_pane_dp() const -> double;

    Hooks hooks_;
    PaneTree tree_{1U};
    std::map<PaneId, TerminalView *> views_;  ///< 非拥有：节点持 `shared_ptr`，本表只供按 pane 取视图。
    PaneLayout layout_{};
    LayoutSpec spec_{};
    std::optional<DividerKey> hovered_;
    std::optional<DividerKey> dragging_;
    double drag_axis_dp_ = 0.0;  ///< 上一次拖拽 Move 的沿轴读数（增量式，故陈旧矩形不影响跟手）。
    GridSizeDebounce debounce_;
    aurora::TimerHandle debounce_timer_;
    PaneId next_pane_ = 2U;  ///< 首个 pane 取 1，故自增从 2 起；标识只增不复用。
    PaneId last_focused_ = 1U;  ///< 最后一次真实读到的持焦格（把手 Press 那一瞬框架读数为空）。
};

}  // namespace borealis::ui
