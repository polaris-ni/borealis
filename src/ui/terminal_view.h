#pragma once

// ============================================================
// 终端视口控件（src/ui/terminal_view.h，私有头）
// ------------------------------------------------------------
// 裁决 D1①：类声明留在 `src/` 而不进 `include/borealis/`——含此头即含框架头，架构 §2.3 的
// 「公共头不含 Aurora 类型」会因此破洞。消费方只有 `src/ui/terminal_view.cpp`（全仓唯一
// 触达 `aurora::Painter` 的翻译单元）与 `src/main.cpp`（装配点）。
//
// 本层只做翻译与落笔：颜色由 `ui::resolve`、run 由 `ui::layout_row`、dp 几何由
// `ui::make_geometry`、可见行内容由 `session::ScreenMirror` 判好、选区的逐行区间由
// `ui::row_spans` 与 `ui::word_span_at` 判好，这里按层叠顺序下调用。
// ============================================================

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/app/scheduler.h"
#include "aurora/core/font.h"
#include "aurora/widget/widget.h"
#include "borealis/grid/row.h"
#include "borealis/session/connection.h"
#include "borealis/session/screen_mirror.h"
#include "borealis/session/session.h"
#include "borealis/term/keymap.h"
#include "borealis/term/paste.h"
#include "borealis/term/terminal.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/grid_size_debounce.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/right_click.h"
#include "borealis/ui/selection.h"

namespace aurora {
class Dialog;
class OverlayHost;
class Popup;
}  // namespace aurora

namespace borealis::ui {

/// @brief 终端网格的自绘视口（`SPEC.FEAT.RENDER.01` / `03` / `04` 的绘制腿）。
///
/// 回看不借用框架的内容平移：基类 `scroll_viewport_` 只当「距顶行数」的容器（`step` 为 1
/// 行），绘制取的是**行偏移换算**，故本控件不声明 `overflow_strategy(Scroll)`——那会让框架
/// 把 `offset_y` 当 dp 平移量加进绘制盒，单位与语义都对不上（架构 §9.5）。
class TerminalView final : public aurora::LeafWidget {
  public:
    /// @brief 选区与复制行为的可配置项（裁决 7.38① 的界面腿入参）。
    ///
    /// 刻意不复用 `config::TerminalSettings`：`config` 含 `ui::PaletteSpec`，视口反向依赖它会
    /// 成模块环（架构 §2.3），与 `ui::CopyOptions` 由调用方搬值同一条理由。
    struct InteractionOptions {
        std::string word_delimiters;  ///< 双击选词的断点集（配置键 `terminal.word_delimiters` 原样搬入）。
        bool copy_on_select{false};   ///< 选中即复制（`SPEC.FEAT.INTERACT.03`，默认关闭）。
        CopyOptions copy{};           ///< 三项复制变换，均默认关闭以保留原样。
        RightClickAction right_click{RightClickAction::ContextMenu};  ///< 右键三态（配置键 `terminal.right_click`）。
        term::PasteOptions paste{};   ///< 粘贴处置口径（换行策略、行尾与块间隔）。
    };

    /// @brief 视口的三条外部接缝：剪贴板读写与多行粘贴的确认呈现。
    ///
    /// 默认（字段为空）即生产路径的实现；换成替身是为了让「右键 → 剪贴板 → 会话字节」这条链
    /// 能在无窗口站、无真实剪贴板的环境里断言（AGENTS.md §4.4 第 19 条）。字段留空即回落默认，
    /// 是为了让装配层只声明它真正替换的那一条。
    struct Presentation {
        /// @brief 读剪贴板文本；空即 `session::ClipboardOutbox::read`。
        std::function<std::string()> clipboard_read;
        /// @brief 写剪贴板文本；空即 `session::ClipboardOutbox::write`。
        std::function<void(std::string_view)> clipboard_write;
        /// @brief 多行粘贴的确认呈现；空即模态对话框。
        ///
        /// 是异步形态而非 `bool`：对话框要等用户点，放行与否经 `answer` 回调给出，未回调即取消。
        std::function<void(const term::PastePlan &, std::function<void(bool)> answer)> confirm_multiline;
    };

    /// @brief 组装视口。
    /// @param session 权威会话，**非拥有**：装配层的局部对象析构次序须保证它活得比本控件久。
    /// @param palette 调色板（含光标色与选区色，缺省时分别回落默认前景与 `basic[8]`）。
    /// @param ref_font 参考字体：整格度量只由它取一次，粗体仅换字重不改格宽（架构 §9.2）。
    /// @param typography 行高与字距（`SPEC.FEAT.RENDER.02`）；缺省档即字体自身的排布，故行列数与
    ///        不接本参数时逐位相同。取值域由配置侧把守，本层不夹取（裁决 7.46②）。
    /// @param padding_dp 视口四周内边距（裁决 7.25②），可配为 0。
    /// @param blink_period 光标闪烁周期；本棒内置，可配项随设置面板那一棒接（`SPEC.FEAT.PREF.02`）。
    /// @param options 选区与复制的可配置行为；由装配层从 `config::TerminalSettings` 搬值。
    TerminalView(session::Session &session, PaletteSpec palette, aurora::Font ref_font, Typography typography,
                 float padding_dp, std::chrono::milliseconds blink_period, InteractionOptions options);

    TerminalView(const TerminalView &) = delete;
    auto operator=(const TerminalView &) -> TerminalView & = delete;
    TerminalView(TerminalView &&) = delete;
    auto operator=(TerminalView &&) -> TerminalView & = delete;

    /// @brief 注销闪烁任务：句柄只置取消标志，故析构期安全（与框架 `Timer` 控件同形态）。
    ~TerminalView() override;

    /// @brief 帧边界排帧：先取脏行提交、再在临界区并入本地副本，最后按需标脏。
    ///
    /// 由 `Application::set_on_frame` 在 `present_root` 之前调用（架构 §3.2）。顺序固定为
    /// 「先取队列、再读网格」——反过来会与读线程构成 ABBA 死锁（`session.h` 头注释）。
    auto on_frame() -> void;

    /// @brief 取选区文本（`SPEC.FEAT.INTERACT.03` 的复制腿）：在既有短临界区内按存储行序取行，
    ///        出锁才返回。copy-on-select 与右键菜单的「复制」项共用本入口（裁决 7.40）。
    /// @return 选中文本，三项变换已按入参应用；无选区时为空串。
    [[nodiscard]] auto selected_text() -> std::string;

    /// @brief 挂上浮层宿主：右键菜单（`au::Popup`）与多行粘贴确认（`au::Dialog`）都作为它的浮层
    ///        子节点存在，本控件只在按需打开时向它 `add_overlay`（裁决 7.41③）。
    ///
    /// 装配层的场景根因此是宿主而非本控件；不设这一步则右键只走直接动作两态，菜单态不发任何东西。
    /// @param host 浮层宿主，**非拥有**：它持有本控件的节点，析构次序须晚于本控件。
    auto set_overlay_host(aurora::OverlayHost &host) -> void;

    /// @brief 替换剪贴板与确认的默认实现（用例注入替身；缺省不装即走生产路径）。
    /// @param presentation 三条接缝，字段留空即保持默认。
    auto set_presentation(Presentation presentation) -> void;

    /// @brief 工作区层的两条注入接缝：期望行列的去向、以及「这一键归谁」的判定。
    ///
    /// 各对应裁决 7.47 的一条判据：⑩ 的尺寸去抖落工作区层（同一帧里所有 pane 一起变，视口层
    /// 各自去抖会让下发次数变成 N 倍），② 的本棒键位不经框架快捷键层、改在焦点 pane 的按键入口
    /// 前置过滤。两条都是 `std::function` 而非虚函数：工作区层用闭包捕获 pane 标识，控件因此
    /// 不需要认识分屏树。
    using GridSizeSink = std::function<void(GridSize expected)>;
    using KeyPreFilter = std::function<bool(const term::KeyPress &press)>;

    /// @brief 换掉「期望行列」的去向；装上即不再直发 `Session::resize`。
    ///
    /// 缺省（未装）仍直发，故既有像素与集成用例零改动。0 行 0 列与「同值不重发」两条过滤留在
    /// 本层——它们的真值源是本控件的度量与内边距，工作区层判不了（裁决 7.48③）。
    /// @param sink 接收者。
    auto set_grid_size_sink(GridSizeSink sink) -> void;

    /// @brief 装上按键前置过滤；返回 true 即该键已被消费，不再编码发会话。
    ///
    /// 只问 `Down` 那一支：重复与抬起都不该被工作区层认领。命中即置 `is_handled`，未命中的键
    /// 照原路走 `term::encode_key`——`SPEC.FEAT.INTERACT.01` 的转发腿不可让渡。
    /// @param filter 判定者；入参与 `term::encode_key` 吃的是同一个 `term::KeyPress`。
    auto set_key_pre_filter(KeyPreFilter filter) -> void;

    /// @brief 右键菜单浮层的只读句柄，从未打开过菜单即空指针（用例的落点观测点）。
    ///
    /// 条目宽度由 `ButtonProps::min_width` 定死而非文字撑开，用例拿不到内容盒就只能自己复制一套
    /// 按钮尺寸算式去猜哪一行是「复制」，而算式猜错时「点到了哪一项」就成了未知量。
    [[nodiscard]] auto context_menu() const noexcept -> const aurora::Popup *;

    /// @brief 本帧的整格几何（dp 步长、行列数与内边距）——工作区层据此现算最小 pane 尺寸。
    ///
    /// 判据是 `max(20 列 × 格宽, 3 行 × 格高)`（裁决 7.47③），而格宽与内边距只有本控件知道；
    /// 让工作区层自己再算一遍度量就成了第二个真值源。
    [[nodiscard]] auto grid_geometry() const noexcept -> const GridGeometry & { return geometry_; }

    /// @brief 回看位置距底的行数（0 = 贴底）：焦点路由「滚动位置不动」判据的观测点。
    ///
    /// 取距底而非内核的 `offset_y`：前者是用户意图（裁决 D6①），随输出与 resize 都不变，
    /// 断言因此只测「路由没有碰它」这一件事。
    [[nodiscard]] auto scrollback_rows_from_bottom() const -> std::size_t;

    /// @brief 当前生效的字号（pt）：Ctrl+滚轮缩放的观测点。
    ///
    /// 缩放是运行期状态（持久化的入口随 `SPEC.FEAT.PREF.02` 的设置面板），用例要靠它把「步长与
    /// 钳位」这一条算式单独断言，而不是从像素反推字号。
    [[nodiscard]] auto font_size_pt() const noexcept -> float { return ref_font_.size_pt; }

  protected:
    /// @brief 撑满父级，并在此重取整格几何与下发行列尺寸（`SPEC.FEAT.XFER.01` 的 UI 取值腿）。
    [[nodiscard]] auto on_layout(const aurora::Constraints &c, const aurora::BuildContext &ctx)
        -> aurora::Size override;

    /// @brief 四层绘制序列：默认底色 → 色带 → 每行一次批量文本 → 装饰与光标。
    auto on_paint(aurora::Painter &p, const aurora::Rect &bounds, const aurora::BuildContext &ctx)
        -> void override;

    [[nodiscard]] auto type_name() const -> const char * override;

    /// @brief 终端视口自带键盘输入，显式留在 Tab 焦点序内（基类默认即 true）。
    [[nodiscard]] auto wants_focus() const -> bool override;

    /// @brief 不画框架的统一焦点环：终端用光标形态表达焦点态，环是 chrome 且会污染像素判据。
    [[nodiscard]] auto wants_focus_ring() const -> bool override;

    /// @brief 成为滚轮目标但不借用框架的内容平移绘制（见类注释）。
    [[nodiscard]] auto wants_scroll() const -> bool override;

    /// @brief 本地回看：驱动 `scroll_viewport_` 并把未吸收的余量上冒给更浅的可滚动祖先。
    auto on_scroll(aurora::ScrollEvent &e) -> void override;

    /// @brief 按键 → VT 字节 → 会话（`SPEC.FEAT.INTERACT.01` 的转发腿）。
    auto on_key_event(aurora::KeyEvent &e) -> void override;

    /// @brief 文本输入通道：框架给出的真实字符（含布局与大小写）按会话编码写入。
    auto on_text_input(aurora::TextInputEvent &e) -> void override;

    /// @brief 指针入口：按下/拖动/抬起 → `ui::cell_at_point` → 选区端点推进
    ///        （`SPEC.FEAT.INTERACT.02`，裁决 7.38① / 7.40）；右键按三态处置
    ///        （`SPEC.FEAT.INTERACT.03`，裁决 7.41）。
    ///
    /// 不委派基类实现：那条路径只服务 Clickable / Gesture / ContextMenu 修饰链，本控件一样没有。
    auto on_pointer_event(aurora::MouseEvent &e) -> void override;

    /// @brief 方向键归终端自己用：不声明则派发器把它们当几何焦点导航吃掉，光标无法移动。
    [[nodiscard]] auto wants_navigation_keys() const -> bool override;

    /// @brief Enter/Space 先给键盘入口：否则派发器直接调用 `activate()`，终端收不到回车。
    [[nodiscard]] auto wants_activation_keys() const -> bool override;

    /// @brief Tab 先给键盘入口：否则派发器把它当焦点遍历消费，终端永远收不到制表符（vim 一类靠它）。
    [[nodiscard]] auto wants_tab_keys() const -> bool override;

    /// @brief 挂载时向运行中的 `Scheduler` 注册闪烁周期（无 App 运行时不注册）。
    auto on_mount(const aurora::BuildContext &ctx) -> void override;

    /// @brief 关闭显示列表与布局缓存：内容每帧可变，缓存只会掩盖脏行并取的缺陷。
    [[nodiscard]] auto can_cache_display_list() const -> bool override;

    [[nodiscard]] auto can_cache_layout() const -> bool override;

  private:
    /// @brief 拖拽的取格粒度：单击逐格、双击按词、三击按行（裁决 7.38①③④）。
    enum class DragMode : std::uint8_t {
        Cell,
        Word,
        Line,
    };

    /// @brief 帧内取用的光标快照：只收绘制需要的那五个字段，且要可比（`term::Cursor` 无 `==`）。
    struct CursorState {
        std::size_t row = 0;
        std::size_t column = 0;
        term::CursorShape shape = term::CursorShape::Block;
        bool visible = true;
        bool blinking = true;

        [[nodiscard]] constexpr auto operator==(const CursorState &) const noexcept -> bool = default;
    };

    /// @brief 取（并按需重取）给定缩放下、叠上排版可调量之后的整格度量。
    ///
    /// 重取的触发有两处：缩放变了（框架那条），或字体与排版入参变过（`metrics_stale_`，Ctrl+滚轮
    /// 缩放是运行期唯一的改点）。度量只在 `on_layout` 取，故重取的代价不进口令路径。
    [[nodiscard]] auto cell_metrics(float scale) -> const TypedMetrics &;

    /// @brief 文本落笔相对行盒顶的下移量（dp）＝上半 leading。
    ///
    /// 框架按「行盒顶 + 该字体自身 ascender」定位基线，而行高调大后多出的空白在字盒之上，故文本盒
    /// 须整体下移这么多；色带与装饰不吃它（`apply_typography` 已把基线折进 `ascent_px`，两者自动跟随）。
    [[nodiscard]] auto glyph_top_dp() const noexcept -> double;

    /// @brief Ctrl+滚轮的字号缩放：一档 ±1 pt，落在 [6, 72] 内；到界值即不吃事件，让回看照常冒泡。
    /// @param delta_rows 滚轮增量的垂直分量（上为正 = 放大）。
    /// @return 是否改变了字号。
    [[nodiscard]] auto zoom_font_size(float delta_rows) -> bool;

    /// @brief 行列数真变了才下发给会话；0 行或 0 列（窗口最小化）不下发。
    auto request_grid_size() -> void;

    /// @brief 取模式快照（短临界区内只取值）：按键编码要按 DECCKM 定方向键走 SS3 还是 CSI，按
    ///        DECKPAM 定小键盘发数值族还是让位文本通道。
    [[nodiscard]] auto modes_snapshot() -> term::TermModes;

    /// @brief 每帧重投影（裁决 D6①「距底恒定」）：返回本次生效的距底行数。
    [[nodiscard]] auto reproject(std::size_t total_lines, std::size_t rows) -> std::size_t;

    /// @brief 闪烁到期：只有配了闪烁档且持焦时才翻相位并重绘。
    auto on_blink_tick() -> void;

    /// @brief 画一个屏幕行：色带、批量文本（含斜体分流）与装饰线。
    auto paint_row(aurora::Painter &p, const aurora::Rect &bounds, std::size_t screen_row) -> void;

    /// @brief 画光标（三形态 + 失焦降级 + 闪烁 off 相）。
    auto paint_cursor(aurora::Painter &p, const aurora::Rect &bounds, std::size_t rows) -> void;

    /// @brief 画回看位置的指示条（视觉稿 U1）：贴底时不画。
    auto paint_scroll_indicator(aurora::Painter &p, const aurora::Rect &bounds, std::size_t rows) -> void;

    /// @brief 取某存储行在当前可见窗里的那一行；已滚出可见窗（含被 scrollback 挤出顶端）时回空。
    [[nodiscard]] auto mirror_line_of(std::size_t storage_row) const noexcept -> const grid::Row *;

    /// @brief 一格在其拖拽粒度下对应的列区间（行号已折成存储行序）。
    ///
    /// Word 粒度落在空格、制表或界定符上时回空（裁决 7.39④）；Line 粒度取整行含行尾空白（D5①）。
    [[nodiscard]] auto span_of(const GridCellPos &storage_cell, DragMode mode) const -> std::optional<RowSpan>;

    /// @brief 把「按下那一格」与「当前那一格」按拖拽粒度折成选区并落库。
    ///
    /// Word / Line 粒度须先把两个区间折成阅读序的外沿：`row_spans` 的流式归一只认两个端点格，
    /// 照端点格存会让三击向上拖只选中起始行的第一列（裁决 7.40）。
    auto extend_selection(const GridCellPos &current_storage, aurora::ModifierKey modifiers) -> void;

    /// @brief 落选区：按区间表判定，两端重合（区间表为空）即清空；两种形态都标脏。
    auto set_selection(const Selection &selection) -> void;

    /// @brief 把选区端点重折成逐行区间表；区间表为空即作废选区（裁决 7.39② 的塌成单击）。
    ///
    /// 每帧重算一次而不是只在选区变化时算：列数会随 resize 变，而首行的选中右界正是列数（D5①）。
    auto refresh_selection_bands() -> void;

    /// @brief 存储行号对应的选中列区间；本行无选中格时回空（区间表按行号升序，故二分）。
    [[nodiscard]] auto band_at(std::size_t storage_row) const -> std::optional<RowSpan>;

    /// @brief 选区底色：持焦取 selection 槽，失焦按同色向默认底色各半混合（裁决 7.38① D3①）。
    [[nodiscard]] auto selection_ink() const noexcept -> RgbaColor;

    /// @brief 把顶边位移的增量折算进选区（裁决 7.38⑤ / 7.39①⑤：整段被推出顶端即作废）。
    auto compensate_selection_drift() -> void;

    /// @brief 落地攒下的 copy-on-select 请求：在帧边界做 IO，不在事件回调里做（AGENTS.md §4.5 第 25 条）。
    auto flush_copy_request() -> void;

    /// @brief 右键按下：按 `plan_right_click` 的意图分发到菜单 / 直接复制 / 直接粘贴（裁决 7.41）。
    auto on_right_click(aurora::MouseEvent &e) -> void;

    /// @brief 用公共 API 组合出上下文菜单（`au::Popup` 承载一列 `au::Button`）并挂到宿主上。
    ///
    /// 框架的 `Modifier::context_menu` 只有状态模型、没有渲染与点击派发，而这一套组合足以在应用侧
    /// 做出可断言的菜单，故按裁决 7.13② 留在本仓而不登记缺口（裁决 7.41③）。
    auto open_context_menu(aurora::Point where, const std::vector<MenuItem> &items) -> void;

    /// @brief 菜单条目被点到：先收菜单，再把动作攒到帧边界（与 copy-on-select 同一条 IO 纪律）。
    auto on_menu_command(MenuCommand command) -> void;

    /// @brief 帧边界落地粘贴：读剪贴板 → `term::plan_paste` → 多行先确认 → 按计划排期发送。
    auto flush_paste_request() -> void;

    /// @brief 多行粘贴的默认确认呈现：宿主的模态对话框（`au::Dialog` + `au::confirm`）。
    auto ask_multiline_warning(const term::PastePlan &plan, std::function<void(bool)> answer) -> void;

    /// @brief 按计划把各块交进会话：块间延迟经 `Scheduler::set_timeout` 累积排期。
    auto send_paste_plan(const term::PastePlan &plan) -> void;

    session::Session *session_ = nullptr;  ///< 非拥有。
    PaletteSpec spec_{};
    aurora::Font ref_font_{};
    Typography typography_{};
    float padding_dp_ = 0.0F;
    std::chrono::milliseconds blink_period_{500};
    InteractionOptions options_{};

    session::ScreenMirror mirror_;
    GridGeometry geometry_{};
    TypedMetrics typed_{};
    float metrics_scale_ = -1.0F;  ///< `typed_` 所属的缩放；与入参不等即重取度量。
    bool metrics_stale_ = true;    ///< 字体或排版入参变过（Ctrl+滚轮缩放是唯一改点）；取用即清。

    CursorState cursor_{};
    CursorState painted_cursor_{};  ///< 上次标脏时的光标，用于判断「本帧到底有没有可画的东西」。
    std::size_t total_lines_ = 0;   ///< scrollback + 视口的总行数，指示条与回看窗口都要用。

    bool blink_on_ = true;
    aurora::TimerHandle blink_timer_;
    session::Size requested_size_{};  ///< 上次交出去的行列（直发或交给 sink 都算），同值不重发。
    /// @brief 一次性吞掉紧随其后的文本事件：应用模式的小键盘数字已由按键通道发成 SS3，而 Windows
    ///        无论 `DECKPAM` 都给同一物理键再发一条 `WM_CHAR`（`term::is_keypad` 的判据）。
    bool swallow_next_text_ = false;

    std::optional<Selection> selection_;  ///< 端点是**存储行序**（`ui/selection.h` 的坐标约定）。
    std::vector<RowSpan> bands_{};        ///< `row_spans` 的产物，行号升序；绘制与复制共用这一张表。
    GridCellPos pressed_cell_{};          ///< 按下那一格（同为存储行序），拖拽的两端之一。
    DragMode drag_mode_ = DragMode::Cell;
    bool dragging_ = false;
    bool copy_pending_ = false;  ///< 抬起已发生、复制留到下一帧的 `on_frame` 落地。
    std::int64_t dropped_baseline_ = 0;  ///< 上次折算选区时的顶边位移读数（裁决 7.39⑤）。

    Presentation presentation_{};         ///< 剪贴板与多行确认的三条接缝（空字段即生产实现）。
    GridSizeSink grid_size_sink_;         ///< 空即直发 `Session::resize`（未挂工作区层的形态）。
    KeyPreFilter key_pre_filter_;         ///< 空即不认领任何键（单 pane 装配的形态）。
    aurora::OverlayHost *host_ = nullptr; ///< 非拥有：装配层给的浮层宿主，本控件的父节点。
    std::shared_ptr<aurora::Popup> menu_;                    ///< 懒建的菜单浮层（建成后一直是宿主的浮层子节点）。
    std::shared_ptr<aurora::Dialog> multiline_warning_;      ///< 懒建的多行粘贴确认对话框。
    bool paste_pending_ = false;  ///< 粘贴已请求、剪贴板读取留到下一帧的 `on_frame` 落地。
    std::optional<term::PastePlan> pending_paste_;  ///< 等用户确认的处置计划（对话框放行才发送）。
    std::vector<aurora::TimerHandle> paste_timers_; ///< 在途的逐块节流任务；析构时统一取消。
};

}  // namespace borealis::ui
