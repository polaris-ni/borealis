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
#include <optional>
#include <string>
#include <vector>

#include "aurora/app/scheduler.h"
#include "aurora/core/font.h"
#include "aurora/widget/widget.h"
#include "borealis/grid/row.h"
#include "borealis/session/connection.h"
#include "borealis/session/screen_mirror.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/selection.h"

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
    };

    /// @brief 组装视口。
    /// @param session 权威会话，**非拥有**：装配层的局部对象析构次序须保证它活得比本控件久。
    /// @param palette 调色板（含光标色与选区色，缺省时分别回落默认前景与 `basic[8]`）。
    /// @param ref_font 参考字体：整格度量只由它取一次，粗体仅换字重不改格宽（架构 §9.2）。
    /// @param padding_dp 视口四周内边距（裁决 7.25②），可配为 0。
    /// @param blink_period 光标闪烁周期；本棒内置，可配项随设置面板那一棒接（`SPEC.FEAT.PREF.02`）。
    /// @param options 选区与复制的可配置行为；由装配层从 `config::TerminalSettings` 搬值。
    TerminalView(session::Session &session, PaletteSpec palette, aurora::Font ref_font, float padding_dp,
                 std::chrono::milliseconds blink_period, InteractionOptions options);

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
    ///        （`SPEC.FEAT.INTERACT.02`，裁决 7.38① / 7.40）。
    ///
    /// 不委派基类实现：那条路径只服务 Clickable / Gesture / ContextMenu 修饰链，本控件一样没有。
    /// 右键不归本入口（`config::RightClickAction` 的三态另立一棒），故不消费、留着上冒。
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

    /// @brief 取（并按需重取）给定缩放下的物理像素整格度量。
    [[nodiscard]] auto cell_metrics(float scale) -> const CellPixels &;

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

    session::Session *session_ = nullptr;  ///< 非拥有。
    PaletteSpec spec_{};
    aurora::Font ref_font_{};
    float padding_dp_ = 0.0F;
    std::chrono::milliseconds blink_period_{500};
    InteractionOptions options_{};

    session::ScreenMirror mirror_;
    GridGeometry geometry_{};
    CellPixels cell_px_{};
    float metrics_scale_ = -1.0F;  ///< `cell_px_` 所属的缩放；与入参不等即重取度量。

    CursorState cursor_{};
    CursorState painted_cursor_{};  ///< 上次标脏时的光标，用于判断「本帧到底有没有可画的东西」。
    std::size_t total_lines_ = 0;   ///< scrollback + 视口的总行数，指示条与回看窗口都要用。

    bool blink_on_ = true;
    aurora::TimerHandle blink_timer_;
    session::Size requested_size_{};  ///< 上次下发的行列，避免每次布局都重发。
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
};

}  // namespace borealis::ui
