/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（指针与滚轮的上报分流腿）+ src/term/mouse.cpp
/// 测试说明: 以无头窗口 + 真实指针/滚轮派发驱动「模式位 → 状态机 → 视口分流 → 会话字节」这条链
///           （`SPEC.FEAT.TERM.06`，裁决 7.77）。断言的是**链路**而非编码表本身（字节形态由
///           `utest_mouse` 逐条覆盖、模式登记由 `utest_terminal` 覆盖），值得单独证的有六件事：
///           ① 档位决定报哪些阶段（`?9` 只按下、`?1000` 加松开、`?1002` 加拖动、`?1003` 加悬停）；
///           ② 让位是**整笔手势**让位而不是半笔（归属由按下那一瞬决定，中途改修饰态不切成两半，且上报
///              那一笔松开后本地选区必须回得来——需求那句「自动切换」判的是这个来回）；
///           ③ 上报开着时本地选区与本地回看都不被驱动（分流判据在选区之前，认领与发字节无关）；
///           ④ 滚轮的四档优先序「Ctrl 缩放 → 上报 → 备屏 alternate scroll → 本地回看」，含到界让位
///              那一腿——同一份滚轮在开档与关档两侧给出「发字节且回看不动」对「不发字节且回看动」；
///           ⑤ 运动上报按格子去重（同格多次 Move 只发一条）；
///           ⑥ 右键在上报档下仍走本地三态（编码层因此不设按钮 2 那一档，裁决 7.77②）。
///
///           模式位一律经**真实状态机**喂入（`CSI ? 1000 h` 这类字节），而不是往控件里塞快照，
///           否则测的是接线假象；`?1007` 缺省为开，故用例要显式关它时才喂 `l`。
///           落点换算走 `ui::rect_for` 的逆（取那一格中心），与既有的选区用例同一口径。

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/render/font_engine.h"
#include "borealis/grid/storage.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_mouse_report {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::Cursor;
using borealis::term::TermModes;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::CellPixels;
using borealis::ui::GridGeometry;
using borealis::ui::LogicalSize;
using borealis::ui::PaletteSpec;
using borealis::ui::Rect;
using borealis::ui::TerminalView;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 40;
constexpr float kPaddingDp = 4.0F;
constexpr int kWindowWidth = 420;
constexpr int kWindowHeight = 260;

auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();

/// @brief 将期望字节写成数值列表：上报协议的字节多是控制码与偏移量，字面量不可读且易错。
///
/// 遗留档的算式是「按钮 + 32」（拖动再 +32、Alt +8、Ctrl +16）、「列(1 基) + 32」、「行(1 基) + 32」，
/// 松开把按钮换成 3 号位；SGR 档则是 `<按钮;列;行>` 后跟 `M` / `m` 且松开保留原按钮号。下面每一处
/// 期望值都按这两条算式手工算出，而不是取实现的输出。
[[nodiscard]] auto raw(std::initializer_list<int> values) -> std::string {
    std::string out;
    out.reserve(values.size());
    for (const int value : values) {
        out.push_back(static_cast<char>(value));
    }
    return out;
}

/// @brief 一次传输连接替身：记录会话写出的字节，并可主动投递字节（本用例里投递线程即读线程）。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override {
        written.insert(written.end(), bytes.begin(), bytes.end());
    }

    auto resize(Size) -> void override {}

    auto close() -> void override { alive_ = false; }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    auto deliver(std::string_view bytes) -> void {
        std::vector<std::byte> raw_bytes;
        raw_bytes.reserve(bytes.size());
        for (const char c : bytes) {
            raw_bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        events_->on_bytes(raw_bytes);
    }

    [[nodiscard]] auto written_text() const -> std::string {
        std::string out;
        out.reserve(written.size());
        for (const std::byte byte : written) {
            out.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        }
        return out;
    }

    std::vector<std::byte> written;

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

/// @brief 驱动台：会话 + 替身连接 + 视口控件 + 浮层宿主 + 无头窗口 + 焦点管理器。
class Harness {
  public:
    explicit Harness(float font_pt = 14.0F)
        : font_({.family = "Cascadia Code", .size_pt = font_pt, .weight = 400}),
          session_(std::make_unique<Session>(own_connection(), kNominalSize, kScrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, PaletteSpec{}, font_, ui::Typography{}, kPaddingDp,
                                               std::chrono::milliseconds{500}, TerminalView::InteractionOptions{})),
          host_(std::make_shared<au::OverlayHost>(au::Node{std::static_pointer_cast<au::Widget>(view_)})),
          root_(std::static_pointer_cast<au::Widget>(host_)) {
        view_->set_overlay_host(*host_);
        auto presentation = TerminalView::Presentation{};
        // 剪贴板两条接缝换成替身：本文件不测粘贴，但右键三态那条用例会把菜单挂上浮层，
        // 真剪贴板在锁屏会话里会 `OpenClipboard GetLastError=5`（裁决 7.31① 的可用面判据）。
        presentation.clipboard_read = []() -> std::string { return std::string{}; };
        presentation.clipboard_write = [](std::string_view) -> void {};
        view_->set_presentation(std::move(presentation));
        session_->start();
        focus_.set_root(&root_.widget());
        focus_.set_focus(view_.get());
        render();
        prime_geometry();  // 落点要按真实格网换算，而格网只有布局之后才知
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投字节并排帧到队列见底（`CSI ? 1000 h` 这类模式位由此来，而不是注入快照）。
    auto feed(std::string_view bytes) -> void {
        connection_->deliver(bytes);
        render();
    }

    /// @brief 灌 @p count 行可辨认的文本，制造出足以回看的滚动历史。
    auto feed_lines(std::size_t count) -> void {
        std::string text{"\x1B[H"};
        for (std::size_t i = 0; i < count; ++i) {
            text += "line" + std::to_string(i) + "\r\n";
        }
        feed(text);
    }

    /// @brief 排帧：走完一次真实布局与绘制。
    auto render() -> void {
        do {
            view_->on_frame();
            (void)window_.present_root(root_);
        } while (session_->has_damage());
    }

    /// @brief 发一个指针事件（落点取该格中心；行列是**屏幕**坐标，0 基）。
    auto pointer(au::MouseAction action, au::MouseButton button, std::size_t row, std::size_t column,
                 au::ModifierKey modifiers = au::ModifierKey::None) -> void {
        au::MouseEvent event;
        const Rect band = borealis::ui::rect_for(geometry_, row, column, column + 1U);
        event.position = au::Point{.x = static_cast<float>(origin_x_ + band.x + band.width * 0.5),
                                   .y = static_cast<float>(origin_y_ + band.y + band.height * 0.5)};
        event.button = button;
        event.action = action;
        event.modifiers = modifiers;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 按下—移动—抬起一整笔左键（两端的格子各自给定）。
    auto gesture(std::size_t press_row, std::size_t press_column, std::size_t end_row, std::size_t end_column,
                 au::ModifierKey modifiers = au::ModifierKey::None) -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Left, press_row, press_column, modifiers);
        pointer(au::MouseAction::Move, au::MouseButton::Left, end_row, end_column, modifiers);
        pointer(au::MouseAction::Release, au::MouseButton::Left, end_row, end_column, modifiers);
    }

    /// @brief 右键单击一格（按下即抬起）。
    auto right_click(std::size_t row, std::size_t column) -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Right, row, column);
        pointer(au::MouseAction::Release, au::MouseButton::Right, row, column);
    }

    /// @brief 在某一格的中心发一次滚轮（@p notches 为正向上滚、为负向下滚）。
    auto scroll_at(std::size_t row, std::size_t column, int notches,
                   au::ModifierKey modifiers = au::ModifierKey::None) -> void {
        au::ScrollEvent event;
        const Rect band = borealis::ui::rect_for(geometry_, row, column, column + 1U);
        event.position = au::Point{.x = static_cast<float>(origin_x_ + band.x + band.width * 0.5),
                                   .y = static_cast<float>(origin_y_ + band.y + band.height * 0.5)};
        event.delta_y = static_cast<float>(notches);
        event.modifiers = modifiers;
        (void)au::EventDispatcher::dispatch(root_.widget(), event);
    }

    [[nodiscard]] auto written() const -> std::string { return connection_->written_text(); }
    auto clear_written() -> void { connection_->written.clear(); }
    [[nodiscard]] auto selected_text() -> std::string { return view_->selected_text(); }
    [[nodiscard]] auto backscroll_rows() const -> std::size_t { return view_->scrollback_rows_from_bottom(); }
    [[nodiscard]] auto font_size_pt() const -> float { return view_->font_size_pt(); }
    [[nodiscard]] auto menu_open() const -> bool {
        return view_->context_menu() != nullptr && view_->context_menu()->is_open();
    }

    /// @brief 权威模式快照（只读核对：本用例要确认状态机真的收到了喂入的模式位）。
    [[nodiscard]] auto modes() -> TermModes {
        TermModes snapshot{};
        session_->read(
            [&snapshot](Storage &, const Cursor &, const TermModes &modes) { snapshot = modes; });
        return snapshot;
    }

    /// @brief 前置条件：Headless 缩放为 1（dp 即像素），且格网容得下本文件用到的行与列。
    [[nodiscard]] auto preflight() const noexcept -> bool {
        return scale_ == 1.0F && geometry_.rows > 8U && geometry_.columns > 6U;
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    /// @brief 造替身连接：所有权交给会话，裸指针留在本类驱动投递与观测。
    [[nodiscard]] auto own_connection() -> std::unique_ptr<Connection> {
        auto owned = std::make_unique<FakeConnection>();
        connection_ = owned.get();
        return owned;
    }

    /// @brief 帧 0 之后重取格网几何（落点要按格子换算，`ui::rect_for` 与控件同一坐标空间）。
    auto prime_geometry() -> void {
        const au::Rect box = view_->paint_bounds();
        scale_ = window_.surface().scale_factor();
        origin_x_ = static_cast<double>(box.origin.x);
        origin_y_ = static_cast<double>(box.origin.y);
        const auto metrics = au::render::FontEngine::monospace_cell(font_, scale_);
        geometry_ = borealis::ui::make_geometry(
            CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px}, scale_,
            LogicalSize{static_cast<double>(box.size.width), static_cast<double>(box.size.height)}, kPaddingDp);
    }

    au::Font font_;
    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    std::shared_ptr<au::OverlayHost> host_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher pointer_dispatcher_;  ///< 本驱动台私有的连击判定与指针捕获状态。
    GridGeometry geometry_{};
    float scale_ = 1.0F;
    double origin_x_ = 0.0;
    double origin_y_ = 0.0;
};

}  // namespace

AURORA_TEST_CASE(normal_level_reports_press_and_release_but_never_a_move) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1000h");
    AURORA_TEST_REQUIRE(h.modes().mouse_normal);

    h.pointer(au::MouseAction::Press, au::MouseButton::Left, 2U, 3U);
    AURORA_TEST_CHECK_EQ(h.written(), raw({0x1B, '[', 'M', 0x20, 0x24, 0x23}));  // 按钮 0、列 4、行 3
    h.clear_written();

    // `?1000` 不报移动，但这一笔也不能掉回本地去推选区（认领与发字节是两件事，裁决 7.77⑥）。
    h.pointer(au::MouseAction::Move, au::MouseButton::Left, 2U, 5U);
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_CHECK(h.selected_text().empty());

    // 松开把按钮换成 3 号位，坐标跟着落点走。
    h.pointer(au::MouseAction::Release, au::MouseButton::Left, 2U, 5U);
    AURORA_TEST_CHECK_EQ(h.written(), raw({0x1B, '[', 'M', 0x23, 0x26, 0x23}));
    AURORA_TEST_CHECK(h.selected_text().empty());
}

AURORA_TEST_CASE(x10_level_reports_presses_only) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?9h");
    AURORA_TEST_REQUIRE(h.modes().mouse_x10);

    h.gesture(1U, 1U, 1U, 4U);
    // 只有一条例外的历史形态：按下即报，拖动与松开都不报（`?9` 是四档里最低的一档）。
    AURORA_TEST_CHECK_EQ(h.written(), raw({0x1B, '[', 'M', 0x20, 0x22, 0x22}));
}

AURORA_TEST_CASE(button_events_level_marks_the_drag_motion_bit) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1002h");
    AURORA_TEST_REQUIRE(h.modes().mouse_button_events);

    h.gesture(2U, 1U, 2U, 4U);
    const std::string expected = raw({0x1B, '[', 'M', 0x20, 0x22, 0x23}) +       // 按下
                                 raw({0x1B, '[', 'M', 0x40, 0x25, 0x23}) +       // 拖动：+32 运动位
                                 raw({0x1B, '[', 'M', 0x23, 0x25, 0x23});         // 松开：按钮 3 号位
    AURORA_TEST_CHECK_EQ(h.written(), expected);
    AURORA_TEST_CHECK(h.selected_text().empty());  // 一次整段拖拽全归上报
}

AURORA_TEST_CASE(any_events_level_reports_buttonless_hover_motion) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1003h");
    AURORA_TEST_REQUIRE(h.modes().mouse_any_events);

    // 没有在途按下的移动只有 `?1003` 才报：按钮 3 号位 + 运动位 32 = 67。
    h.pointer(au::MouseAction::Move, au::MouseButton::Left, 4U, 2U);
    AURORA_TEST_CHECK_EQ(h.written(), raw({0x1B, '[', 'M', 0x43, 0x23, 0x25}));
}

AURORA_TEST_CASE(motion_reports_deduplicate_per_cell) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1003h");

    // 高分屏与高频采样下框架一帧能给出多次同格 Move，逐次上报会让远端收到一串同格事件
    // （裁决 7.77④）：三次于同一格只发一条，换格才发第二条。
    h.pointer(au::MouseAction::Move, au::MouseButton::Left, 1U, 1U);
    h.pointer(au::MouseAction::Move, au::MouseButton::Left, 1U, 1U);
    h.pointer(au::MouseAction::Move, au::MouseButton::Left, 1U, 1U);
    const std::string first = raw({0x1B, '[', 'M', 0x43, 0x22, 0x22});
    AURORA_TEST_CHECK_EQ(h.written(), first);
    h.pointer(au::MouseAction::Move, au::MouseButton::Left, 1U, 2U);
    AURORA_TEST_CHECK_EQ(h.written(), first + raw({0x1B, '[', 'M', 0x43, 0x23, 0x22}));
}

AURORA_TEST_CASE(a_shift_held_gesture_selects_locally_and_sends_nothing) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1002h");
    h.feed("\x1B[?25l\x1B[3;1HABCDE");

    // Shift 是「这一次让位本地」的覆盖键（裁决 7.77①）：上报开着时它是本地选区唯一的入口。
    h.gesture(2U, 1U, 2U, 3U, au::ModifierKey::Shift);
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_CHECK_EQ(h.selected_text(), std::string{"BCD"});
}

AURORA_TEST_CASE(the_right_button_stays_local_while_reporting_is_on) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1002h");

    // 右键留给本地三态（裁决 7.77②），于是编码层根本没有按钮 2 那一档；开着上报也不能把它交出去。
    h.right_click(3U, 2U);
    h.render();
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_REQUIRE(h.menu_open());
}

// 归属由**按下**那一瞬间决定，之后修饰态怎么变都不把这笔切成两半（裁决 7.77⑦）。
AURORA_TEST_CASE(shift_pressed_midway_does_not_split_a_reported_drag) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1002h");
    h.feed("\x1B[?25l\x1B[3;1HABCDE");

    // 按下时没按 Shift 即整笔归上报：中途补上 Shift 也不能让后半笔掉回本地选区。
    h.pointer(au::MouseAction::Press, au::MouseButton::Left, 2U, 1U);
    h.pointer(au::MouseAction::Move, au::MouseButton::Left, 2U, 3U, au::ModifierKey::Shift);
    h.pointer(au::MouseAction::Release, au::MouseButton::Left, 2U, 3U, au::ModifierKey::Shift);
    const std::string expected = raw({0x1B, '[', 'M', 0x20, 0x22, 0x23}) +       // 按下
                                 raw({0x1B, '[', 'M', 0x40, 0x24, 0x23}) +       // 拖动：Shift 不进编码
                                 raw({0x1B, '[', 'M', 0x23, 0x24, 0x23});         // 松开
    AURORA_TEST_CHECK_EQ(h.written(), expected);
    AURORA_TEST_CHECK(h.selected_text().empty());
}

AURORA_TEST_CASE(lifting_shift_midway_does_not_split_a_local_selection) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1002h");
    h.feed("\x1B[?25l\x1B[3;1HABCDE");

    // 反方向同一条不变量：Shift 覆盖那一笔的按下已在途，中途松开 Shift 也不该把拖拽的后半切成上报。
    h.pointer(au::MouseAction::Press, au::MouseButton::Left, 2U, 1U, au::ModifierKey::Shift);
    h.pointer(au::MouseAction::Move, au::MouseButton::Left, 2U, 3U);
    h.pointer(au::MouseAction::Release, au::MouseButton::Left, 2U, 3U);
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_CHECK_EQ(h.selected_text(), std::string{"BCD"});
}

AURORA_TEST_CASE(a_local_selection_recovers_after_a_fully_reported_gesture) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1002h");
    h.feed("\x1B[?25l\x1B[3;1HABCDE");

    // 需求那句「上报模式与本地选择交互自动切换」判的是这个**来回**：上一笔整笔归上报（松开把在途
    // 按下清掉），下一笔按 Shift 才回得来。在途态若粘住，本地选区就此永久失效。
    h.gesture(2U, 1U, 2U, 3U);
    const std::string reported = raw({0x1B, '[', 'M', 0x20, 0x22, 0x23}) +       // 按下
                                 raw({0x1B, '[', 'M', 0x40, 0x24, 0x23}) +       // 拖动
                                 raw({0x1B, '[', 'M', 0x23, 0x24, 0x23});         // 松开
    AURORA_TEST_CHECK_EQ(h.written(), reported);
    AURORA_TEST_CHECK(h.selected_text().empty());
    h.clear_written();

    h.gesture(2U, 1U, 2U, 3U, au::ModifierKey::Shift);
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_CHECK_EQ(h.selected_text(), std::string{"BCD"});
}

AURORA_TEST_CASE(the_sgr_form_keeps_the_button_number_and_ends_with_lowercase_m) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1000h\x1B[?1006h");
    AURORA_TEST_REQUIRE(h.modes().mouse_sgr);

    h.pointer(au::MouseAction::Press, au::MouseButton::Left, 2U, 3U);
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[<0;4;3M");
    h.clear_written();
    // SGR 档的松开保留按键编号、靠终止字节 `m` 区分（遗留档那一条没有这个信息）。
    h.pointer(au::MouseAction::Release, au::MouseButton::Left, 2U, 3U);
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[<0;4;3m");
}

AURORA_TEST_CASE(the_wheel_reports_one_event_per_notch_without_backscrolling) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_lines(40U);
    h.feed("\x1B[?1000h");
    AURORA_TEST_REQUIRE(h.backscroll_rows() == 0U);

    // 按钮 64 = 上滚，一 notch 一条（两档协议都没有滚轮的「松开」形态，也不带增量参数）。
    h.scroll_at(3U, 4U, 2);
    const std::string one = raw({0x1B, '[', 'M', 0x60, 0x25, 0x24});
    AURORA_TEST_CHECK_EQ(h.written(), one + one);
    // 上报吃掉这两档：本地历史一步没动（与下面那条关档的对照用例成对）。
    AURORA_TEST_CHECK_EQ(h.backscroll_rows(), 0U);
}

AURORA_TEST_CASE(the_wheel_at_the_zoom_bound_falls_through_to_the_report) {
    Harness h{6.0F};  // 字号已在下界，Ctrl+滚轮那一档让位给后面的分流（裁决 7.46⑤）
    AURORA_TEST_REQUIRE(h.preflight());
    AURORA_TEST_REQUIRE(h.font_size_pt() == 6.0F);
    h.feed("\x1B[?1000h");

    h.scroll_at(5U, 2U, -1, au::ModifierKey::Control);
    // 按钮 65 = 下滚，Ctrl 位 +16 → 32 + 65 + 16 = 113。
    AURORA_TEST_CHECK_EQ(h.written(), raw({0x1B, '[', 'M', 0x71, 0x23, 0x26}));
    AURORA_TEST_CHECK_EQ(h.font_size_pt(), 6.0F);
}

AURORA_TEST_CASE(the_wheel_without_a_reporting_level_still_backscrolls) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_lines(40U);

    // 上报档全关（`Off`）时滚轮回到本地回看：与上一条同一份滚轮，字节与回看两列正好相反。
    h.scroll_at(3U, 4U, 2);
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_CHECK_EQ(h.backscroll_rows(), 2U);
}

AURORA_TEST_CASE(the_alternate_screen_wheel_pages_with_arrow_keys) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1049h");
    AURORA_TEST_REQUIRE(h.modes().alternate_screen);
    AURORA_TEST_REQUIRE(h.modes().alternate_scroll);  // `?1007` 缺省为开（裁决 7.77⑤）

    // `less`/`more` 不开上报档，翻页靠方向键：上滚 = 上方向键，下滚 = 下方向键，一档一条。
    h.scroll_at(3U, 4U, 3);
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\x1B[A"} + "\x1B[A" + "\x1B[A");
    h.clear_written();
    h.scroll_at(3U, 4U, -2);
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\x1B[B"} + "\x1B[B");
}

AURORA_TEST_CASE(alternate_screen_paging_follows_decckm) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1049h\x1B[?1h");
    AURORA_TEST_REQUIRE(h.modes().cursor_key_app);

    // 形态与真实方向键同源（SS3 vs CSI 由 `DECCKM` 定），故这条腿复用按键那条编码入口。
    h.scroll_at(3U, 4U, 1);
    AURORA_TEST_CHECK_EQ(h.written(), "\x1BOA");
}

AURORA_TEST_CASE(clearing_alternate_scroll_leaves_the_wheel_local) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1B[?1049h\x1B[?1007l");
    AURORA_TEST_REQUIRE(!h.modes().alternate_scroll);

    h.scroll_at(3U, 4U, 2);
    AURORA_TEST_CHECK(h.written().empty());
}

#else

AURORA_TEST_CASE(mouse_report_integration_requires_the_headless_backend) {
    AURORA_TEST_CHECK(true);  ///< 占位：指针与滚轮的真实派发需要无头后端（裁决 7.19⑤ 同口径）。
}

#endif  // AURORA_BACKEND_HEADLESS

}  // namespace borealis::test_cases::itest_mouse_report
