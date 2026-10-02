#pragma once

// ============================================================
// 终端视口控件（src/ui/terminal_view.h，私有头）
// ------------------------------------------------------------
// 裁决 D1①：类声明留在 `src/` 而不进 `include/borealis/`——含此头即含框架头，架构 §2.3 的
// 「公共头不含 Aurora 类型」会因此破洞。消费方只有 `src/ui/terminal_view.cpp`（全仓唯一
// 触达 `aurora::Painter` 的翻译单元）与 `src/main.cpp`（装配点）。
//
// 本层只做翻译与落笔：颜色由 `ui::resolve`、run 由 `ui::layout_row`、dp 几何由
// `ui::make_geometry`、可见行内容由 `session::ScreenMirror` 判好，这里按层叠顺序下调用。
// ============================================================

#include <chrono>
#include <cstddef>

#include "aurora/app/scheduler.h"
#include "aurora/core/font.h"
#include "aurora/widget/widget.h"
#include "borealis/session/connection.h"
#include "borealis/session/screen_mirror.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"

namespace borealis::ui {

/// @brief 终端网格的自绘视口（`SPEC.FEAT.RENDER.01` / `03` / `04` 的绘制腿）。
///
/// 回看不借用框架的内容平移：基类 `scroll_viewport_` 只当「距顶行数」的容器（`step` 为 1
/// 行），绘制取的是**行偏移换算**，故本控件不声明 `overflow_strategy(Scroll)`——那会让框架
/// 把 `offset_y` 当 dp 平移量加进绘制盒，单位与语义都对不上（架构 §9.5）。
class TerminalView final : public aurora::LeafWidget {
  public:
    /// @brief 组装视口。
    /// @param session 权威会话，**非拥有**：装配层的局部对象析构次序须保证它活得比本控件久。
    /// @param palette 调色板（含光标色，缺省时回落默认前景）。
    /// @param ref_font 参考字体：整格度量只由它取一次，粗体仅换字重不改格宽（架构 §9.2）。
    /// @param padding_dp 视口四周内边距（裁决 7.25②），可配为 0。
    /// @param blink_period 光标闪烁周期；本棒内置，可配项随设置面板那一棒接（`SPEC.FEAT.PREF.02`）。
    TerminalView(session::Session &session, PaletteSpec palette, aurora::Font ref_font, float padding_dp,
                 std::chrono::milliseconds blink_period);

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

    /// @brief 方向键归终端自己用：不声明则派发器把它们当几何焦点导航吃掉，光标无法移动。
    [[nodiscard]] auto wants_navigation_keys() const -> bool override;

    /// @brief Enter/Space 先给键盘入口：否则派发器直接调用 `activate()`，终端收不到回车。
    [[nodiscard]] auto wants_activation_keys() const -> bool override;

    /// @brief 挂载时向运行中的 `Scheduler` 注册闪烁周期（无 App 运行时不注册）。
    auto on_mount(const aurora::BuildContext &ctx) -> void override;

    /// @brief 关闭显示列表与布局缓存：内容每帧可变，缓存只会掩盖脏行并取的缺陷。
    [[nodiscard]] auto can_cache_display_list() const -> bool override;

    [[nodiscard]] auto can_cache_layout() const -> bool override;

  private:
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

    /// @brief 取模式快照（短临界区内只取值）：按键编码要按 DECCKM 决定方向键走 SS3 还是 CSI。
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

    session::Session *session_ = nullptr;  ///< 非拥有。
    PaletteSpec spec_{};
    aurora::Font ref_font_{};
    float padding_dp_ = 0.0F;
    std::chrono::milliseconds blink_period_{500};

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
};

}  // namespace borealis::ui
