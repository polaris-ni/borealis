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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    ///
    /// `origin` 是被切那一格（`split_pane` 的 target），不是工厂正在造的 `pane`：装配层要靠它取回
    /// 源格的会话（本地腿的 OSC 7 继承、SSH 腿的档案与留档凭据都按那一格现读）。少这一个参数，
    /// 工厂就只能反过来猜「此刻框架焦点在谁」——而把手刚按下时那份读数是空的（见 `note_framework_focus`）。
    using PaneViewFactory =
        std::function<std::shared_ptr<TerminalView>(PaneId pane, PaneId origin)>;
    /// @brief 去抖尾沿到点：把某 pane 的最新期望行列交出去（生产侧即 `Session::resize`）。
    using GridSizeDispatch = std::function<void(PaneId pane, GridSize size)>;
    /// @brief 视图已从容器摘除后回收该 pane 的会话；与工厂成对，故生命周期归属只在装配层。
    using PaneTeardown = std::function<void(PaneId pane)>;

    /// @brief 本层与装配层的接缝（与 `TerminalView::Presentation` 同形态的 struct of 回调）。
    ///
    /// 广播那四条（`SPEC.FEAT.WS.09`，裁决 7.121 F6）与前三同形：本层仍不认识路由器、标签或
    /// 会话，只把「这一格在不在组里」与「这一击该不该扇出」转交装配层。
    struct Hooks {
        PaneViewFactory make_pane;
        GridSizeDispatch dispatch_grid;
        PaneTeardown teardown_pane;
        /// @brief 该 pane 现在是否在广播组里（画不画 pill）；缺省即永不在组内。
        std::function<bool(PaneId pane)> is_broadcast;
        /// @brief 把该 pane 加入/移出广播组，返回调用后是否在组内；缺省即该键只被认领、不改组。
        std::function<bool(PaneId pane)> toggle_broadcast;
        /// @brief 源格按键已发给自己之后转交：装配层按会话去重后扇出（D5/D7）。
        std::function<void(PaneId pane, const term::KeyPress &press)> mirror_key;
        /// @brief 源格已提交文本的转交（同上）。
        std::function<void(PaneId pane, std::u32string_view text)> mirror_text;
        /// @brief 树里只剩一格时按下关格键的转交（`Ctrl+Shift+W`，`SPEC.FEAT.WS.01`）。
        ///
        /// 末位 pane 的关闭就是「关标签」：判据、确认框与撤账只在装配层那一条（裁决 7.42⑤ 与
        /// 7.43⑤ 同口径），本层不造空树也不在此判定退出。缺省即该键仍被认领、什么都不做。
        std::function<void()> close_last_pane;
        /// @brief 开一扇新工作区窗（`Ctrl+Shift+N`，`SPEC.FEAT.WS.03` 稿 D2）；缺省即该键只被认领、
        ///        什么都不做。窗与标签都归装配层造：本层不认识框架的窗，也不持有窗口清单。
        std::function<void()> open_new_window;
        /// @brief 树形有任何一次**实际**变更之后调用（`SPEC.FEAT.WS.11` 批 5-B）：切分成功、关格成功各一次。
        ///
        /// 为什么需要这一支而不是让装配层在它自己的调用点上补：`Ctrl+Shift+D` 与关掉一格那两刀都发自
        /// 本层的按键派发，装配层唯一的观察点是 `make_pane`／`teardown_pane`——前者排在 `tree_.split`
        /// **之前**（工厂失败时树因此不动），在那里采集读到的是变更前的形状。把手步进改的是相对长度
        /// 而不是拓扑，也不在「结构变更」这一档，故不调本钩（比例随下一次真变更或退出提交）。
        std::function<void()> structure_changed;
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

    /// @brief 当前缩放的 pane（`SPEC.FEAT.WS.08`）；未缩放回空值。
    [[nodiscard]] auto zoomed_pane() const -> std::optional<PaneId> { return zoomed_; }

    /// @brief 立刻结算去抖表：把到期的期望行列逐条交出（无调度器时用例靠它推进尾沿）。
    /// @param now 当前时刻。
    auto flush_grid_sizes(Moment now) -> void;

    /// @brief 按装配层现读的成员资格刷新各视图的广播角标（`SPEC.FEAT.WS.09` D9）。
    ///
    /// 只在 `pane_count() > 1` 时为真（N4：单 pane 只靠横幅）；同一视图被多个 pane 指认时按「任一
    /// 成员」置真，避免后写者把前者的角标抹掉。构造 / 切分 / 关闭 / 第十键之后各调一次。
    auto refresh_broadcast_badges() -> void;

    /// @brief 焦点格右上角那枚「焦点 · 行列数」药丸的文案（`SPEC.FEAT.WS.02` 界面腿，裁决 7.184）。
    ///
    /// 它是「当前 pane 视觉标识」的第二个载体（第一个是 2 dp accent 内描边），稿面取自
    /// `codespec/UI_WORKSPACE.draft.svg` 屏 1。本层不认识行列，文案连前缀一起由装配层每帧现算下发——
    /// 与状态栏那格同一份 `TerminalView::grid_geometry()` 读数，于是不出现「条上 82×24、屏上 80×24」。
    ///
    /// 空串＝这一帧没有读数（首帧、窗最小化、焦点格外没有视图）；**值相等即空操作**，因为装配层跟着
    /// 帧走，无条件标脏会把这层画成永不停笔（`StatusBar::set_state` 同一条纪律）。
    /// @param text 药丸文案，含稿面那句「焦点 · 」前缀。
    auto set_focus_pill_text(std::u32string text) -> void;

    /// @brief 把键盘焦点还给最后一次真实持焦的 pane（横幅点击清组后调用，F10）。
    ///
    /// 横幅不可获焦，点它会让派发器把焦点清空；不补这一句，用户退出广播后第一下键入无处可去。
    auto restore_keyboard_focus() -> void;

    /// @brief 按模型焦点切分（装配层入口，`SPEC.FEAT.INTEG.04` 的 `split-pane`）。
    ///
    /// 与 `Ctrl+Shift+D` 同一条路径：先退出缩放（结构命令要读未缩放几何），再对焦点叶子切分。
    /// 目标取 `tree_.focused()` 而非 `focused_pane()`：后者是**框架焦点**的读数，只有派发栈内才与模型
    /// 投影同步——装配层的启动入口正走在栈外，那一读会落后一拍，切到的是被切格之外的另一格。
    /// @param axis `Horizontal` 为左右分（与 `Ctrl+Shift+D` 同轴），`Vertical` 为上下分。
    auto split_focused(PaneAxis axis) -> void;

    /// @brief 就地换掉某一格的视图（装配层的重启腿：`SPEC.FEAT.WS.05` 换会话而不换格）。
    ///
    /// 格的身份全程不变：`PaneTree` 的叶子、广播成员资格、缩放态与焦点归属都按原样跟上，只有视图
    /// 与它背后的会话换成新的。返回 false 即**什么都没动**，四种情形各对应一条拒绝：新视图为空、该格
    /// 不存在、交进来的就是当前那份视图（换了等于没换）、这一格与别格共用同一份视图（子节点身份靠
    /// 视图地址反查，共用时无法定位该换哪一个；每格各有一份视图之后生产走不到这一支，它是 fail-closed
    /// 守卫，证人取 `itest_workspace_broadcast` 的 `shared_view` 档）。
    ///
    /// 三条次序是承重的，不是排布偏好：① 先把框架焦点清掉再销毁旧视图——焦点管理器留着悬垂指针
    /// 的话，下一次 `set_focus` 会去 blur 一个已析构的控件（与 `close_pane` 的「先交接再摘子」同源）；
    /// ② 换 Node 之后须 `note_pending_mount()`，新视图的挂载仍交回框架在下一次布局做（文件头第三条
    /// 实测事实，`Container::add` 之外的手写换子同样适用；删掉这一句全量用例仍绿，故它是机制沿用而非
    /// 已证行为，见稿 `WS02_PANE_SESSION` §11 的变异账）；③ 去抖表按格清账，否则新会话会沿用旧格
    /// 已下发的那份行列而少发一次 `resize`（与 `close_pane` 里那句 `debounce_.forget` 同一条理由）。
    /// @param pane 目标格。
    /// @param next 新的视图，本容器自此持有它。
    auto replace_view(PaneId pane, std::shared_ptr<TerminalView> next) -> bool;

    // ---- 恢复腿（`SPEC.FEAT.WS.11`）：形状的描述与重建都在本层，树件只认树 ----

    /// @brief 当前分屏形状（采集腿）：叶子的序位与 `panes_in_order()` 同一条阅读序，故第 i 枚叶子
    ///        对得上装配层那份按序位排列的逐格料。
    ///
    /// 树的重建能力（`PaneTree::describe` / `restore`）在批 1 就落在树件里了，本层只补两件树给不出的
    /// 东西：**这一份读数是界面用的**（视图与挂载归本件），以及**形状落回界面**那一步。缺这一条接缝，
    /// 快照里的形状就只是屏幕上看着像、却没有任何一格真按它排的读数。
    [[nodiscard]] auto pane_shape() const -> PaneShape { return tree_.describe(); }

    /// @brief 全部 pane 标识，按深度优先、自左而右的阅读序（与 `PaneTree::panes()` 同序）。
    ///
    /// 标识只增不复用，切分与关闭之后**不连续**（`1、2、4` 是合法读数），故快照记的是序位不是它
    /// （`include/borealis/ui/workspace_snapshot.h` 文件头第四条）；装配层要按序位对料就得先取这一份表。
    /// 恢复出来的工作区没有这段历史，所以它的读数恰是 `1..N`（见 `restore`）。
    [[nodiscard]] auto panes_in_order() const -> std::vector<PaneId> { return tree_.panes(); }

    /// @brief 以「上次那套形状」起步造一份工作区（重放腿）：@p views 按序位给，第 i 份挂在标识
    ///        `i + 1` 上。
    ///
    /// 为什么是静态工厂而不是「改一扇现成的工作区」：重建一棵树必然要把旧格全部散掉，而新格与旧格
    /// 在同一段里重叠时，任何一步失败都留下一棵半棵树加上一批已散的手柄。工厂形态给了第三条出路
    /// ——什么都不成立，调用方手里的会话由它自己按既有次序回收。恢复只发生在启动那一刻，本层不需要
    /// 「运行中换形状」那一条腿（那条腿的既有形态是切分与关闭，各判各的）。
    ///
    /// 三道前置（形状自检、视图份数与空值、焦点序位）**全部跑在造对象之前**，故回空即「没有半个
    /// 工作区」，与 `PaneTree::restore` 的先自检后换根同一条取向。
    /// @param views 逐格的视图，第 i 份属序位 i 那一格；本容器自此持有它们。为空或含空指针即拒绝。
    /// @param shape 上次那套分屏形状（`pane_shape()` 的产物）。
    /// @param focused_order 选中格的阅读序序位（快照里的 `focused_order`）。
    /// @param hooks 三条接缝，与构造路径同一份。
    /// @return 建好的工作区；任一前置不合即回空值。
    [[nodiscard]] static auto restore(std::vector<std::shared_ptr<TerminalView>> views,
                                      const PaneShape &shape, std::size_t focused_order, Hooks hooks)
        -> std::shared_ptr<WorkspaceView>;

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

    /// @brief 切换一个 pane 的缩放（单叶工作区为 no-op，键仍由按键入口认领）。
    auto toggle_zoom(PaneId pane) -> void;

    /// @brief 退出缩放并立刻把显示表还原成树的真相（结构命令在本帧就要按未缩放几何执行）。
    auto unzoom() -> void;

    /// @brief 按 `zoomed_` 重算显示表：缩放 pane 占满可用区、其余零盒、把手为空。
    auto apply_display_layout() -> void;

    /// @brief 该 pane 在**显示表**里是否可见（缩放态只有缩放那一格）。
    [[nodiscard]] auto pane_visible(PaneId pane) const -> bool;

    /// @brief 由子控件反查 pane（绘制与命中都要按 pane 判可见性）。
    [[nodiscard]] auto pane_of(const aurora::Widget &widget) const -> std::optional<PaneId>;

    /// @brief 该 pane 的缩放角标盒（逻辑 dp，本容器局部系）；盒太小放不下时回空值。
    [[nodiscard]] auto zoom_badge_box(const Rect &pane) const -> std::optional<Rect>;

    /// @brief 指针落点是否在某个可见 pane 的缩放角标上（单叶工作区恒回空值）。
    [[nodiscard]] auto zoom_badge_at(const aurora::Point &local) const -> std::optional<PaneId>;

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
    PaneLayout base_layout_{};  ///< 树折算的真相：焦点路由、步进与隐藏 pane 的视图布局都读它。
    PaneLayout layout_{};       ///< 显示表：缩放态下只有缩放 pane 满盒，其余零盒且无把手。
    Rect area_{};               ///< 最近一次布局的可用区（`apply_display_layout` 要在两帧之间重算满盒）。
    LayoutSpec spec_{};
    std::optional<PaneId> zoomed_;        ///< 按标签独立、不落盘（`SPEC.FEAT.WS.08`，裁决 7.120 D7）。
    std::optional<PaneId> hovered_zoom_;  ///< 指针当前停在谁的角标上（只影响绘制与光标）。
    std::optional<DividerKey> hovered_;
    std::optional<DividerKey> dragging_;
    double drag_axis_dp_ = 0.0;  ///< 上一次拖拽 Move 的沿轴读数（增量式，故陈旧矩形不影响跟手）。
    GridSizeDebounce debounce_;
    aurora::TimerHandle debounce_timer_;
    PaneId next_pane_ = 2U;  ///< 首个 pane 取 1，故自增从 2 起；标识只增不复用。
    PaneId last_focused_ = 1U;  ///< 最后一次真实读到的持焦格（把手 Press 那一瞬框架读数为空）。
    std::u32string focus_pill_text_;  ///< 焦点药丸文案（空串＝不画）；装配层每帧下发，见 `set_focus_pill_text`。
};

}  // namespace borealis::ui
