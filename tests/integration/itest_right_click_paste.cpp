/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（右键三态与粘贴排期腿）+ src/ui/right_click.cpp
/// 测试说明: 以无头窗口 + 真实指针派发驱动「右键 → `plan_right_click` → 菜单 / 直接动作 →
///           剪贴板接缝 → `term::plan_paste` → 会话字节」这条链（`SPEC.FEAT.INTERACT.03` 的
///           右键与粘贴腿，裁决 7.41）。断言的是**链路**而非决策件与计划件本身（两者各有
///           `utest_right_click` / `utest_paste` 逐条覆盖），值得单独证的有五件事：
///           ① 三态各自的分流与「无选区不覆盖剪贴板」；② 菜单由 `au::Popup` 挂在
///           `au::OverlayHost` 上，条目落点经真实命中测试，置灰的那一项点下去不产生任何动作；
///           ③ 剪贴板读写都发生在帧边界而非事件回调里（AGENTS.md §4.5 第 25 条），故
///           「点击之后、排帧之前什么都还没发生」是一条要显式断言的中间态；
///           ④ 多行粘贴先确认再发送，未经确认（含没有浮层宿主可问）一个字节也不发；
///           ⑤ 运行期换三态（裁决 7.52 的 S4① 第二条入口）只改「下一次右键」的处置，既不丢既有
///           选区，也不把上一次的动作补发一遍。
///           节流的那一腿需要运行中的 `Scheduler`，此处按「无调度器即按次序一次发完」的退化
///           分支断言块序与内容（裁决 7.41⑥）。
///
///           浮层内容盒用 `context_menu()` 取，条目落点按其高度分数算：两条目等高，故 1/4 处
///           是「复制」、3/4 处是「粘贴」——比照按钮内边距反推尺寸可靠。

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
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
#include "borealis/term/paste.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/right_click.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_right_click_paste {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::Cursor;
using borealis::term::PastePlan;
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

UnicodeWidthPolicy width_policy;

/// @brief 传输连接替身：记录会话写出的字节，并可主动投递字节（本用例里投递线程即读线程）。
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
        std::vector<std::byte> raw;
        raw.reserve(bytes.size());
        for (const char c : bytes) {
            raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        events_->on_bytes(raw);
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

[[nodiscard]] auto test_font() -> au::Font {
    return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
}

/// @brief 驱动台：会话 + 替身连接 + 视口控件 + 浮层宿主 + 无头窗口 + 焦点管理器。
///
/// 三条外部接缝（剪贴板读 / 写 / 多行确认）默认全部换成替身，于是「右键 → 连接字节」这条链
/// 能在无窗口站、无真实剪贴板的环境里跑完；`stub_confirm` 置假才走生产的模态对话框，那用来
/// 证「没有宿主就问不了，问不了就不粘贴」。
class Harness {
  public:
    explicit Harness(const TerminalView::InteractionOptions &options = {}, bool with_host = true,
                     bool stub_confirm = true)
        : session_(std::make_unique<Session>(own_connection(), kNominalSize, kScrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, PaletteSpec{}, test_font(), ui::Typography{}, kPaddingDp,
                                               std::chrono::milliseconds{500}, options)),
          host_(std::make_shared<au::OverlayHost>(au::Node{std::static_pointer_cast<au::Widget>(view_)})),
          root_(std::static_pointer_cast<au::Widget>(host_)) {
        if (with_host) {
            view_->set_overlay_host(*host_);
        }
        auto presentation = TerminalView::Presentation{};
        presentation.clipboard_read = [this]() -> std::string {
            ++read_calls;
            return clipboard;
        };
        presentation.clipboard_write = [this](std::string_view text) -> void {
            ++write_calls;
            copied.emplace_back(text);
        };
        if (stub_confirm) {
            presentation.confirm_multiline = [this](const PastePlan &plan,
                                                    std::function<void(bool)> answer) -> void {
                ++confirm_calls;
                warned_chunks = plan.chunks.size();
                held_answer = std::move(answer);
            };
        }
        view_->set_presentation(std::move(presentation));
        session_->start();
        focus_.set_root(&root_.widget());
        focus_.set_focus(view_.get());
        render();         // 帧 0：布局发生在这里，浮层内容盒据此才有尺寸
        prime_geometry();  // 左键落点要按真实格网换算，而格网只有布局之后才知
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投字节并排帧到队列见底。
    auto feed(std::string_view bytes) -> void {
        connection_->deliver(bytes);
        render();
    }

    /// @brief 排帧：`on_frame` 落地攒下的剪贴板 IO 与粘贴请求，再走一遍真实布局与绘制。
    auto render() -> void {
        do {
            view_->on_frame();
            (void)window_.present_root(root_);
        } while (session_->has_damage());
    }

    /// @brief 发一个指针事件（落点取该格中心；行列是**屏幕**坐标，0 基）。
    auto pointer(au::MouseAction action, au::MouseButton button, std::size_t row, std::size_t column) -> void {
        au::MouseEvent event;
        const Rect band = borealis::ui::rect_for(geometry_, row, column, column + 1U);
        event.position = au::Point{.x = static_cast<float>(origin_x_ + band.x + band.width * 0.5),
                                   .y = static_cast<float>(origin_y_ + band.y + band.height * 0.5)};
        event.button = button;
        event.action = action;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 右键单击一格（按下即抬起）：三态的动作都在按下那一刻定。
    auto right_click(std::size_t row, std::size_t column) -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Right, row, column);
        pointer(au::MouseAction::Release, au::MouseButton::Right, row, column);
    }

    /// @brief 左键拖拽选一格区间（行内从左列到右列）。
    auto drag_select(std::size_t row, std::size_t from_column, std::size_t to_column) -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Left, row, from_column);
        pointer(au::MouseAction::Move, au::MouseButton::Left, row, to_column);
        pointer(au::MouseAction::Release, au::MouseButton::Left, row, to_column);
        render();
    }

    /// @brief 运行期换交互口径（裁决 7.52 的 S4① 第二条入口）：交进控件后排一帧。
    auto apply_options(TerminalView::InteractionOptions options) -> void {
        view_->apply_interaction_options(std::move(options));
        render();
    }

    /// @brief 在菜单内容盒的竖直分数处单击一次（须经 `render()` 打开菜单后才能调）。
    auto click_menu_item(double fy) -> void {
        const au::Rect box = view_->context_menu()->content_bounds();
        au::MouseEvent event;
        event.position = au::Point{.x = box.origin.x + box.size.width * 0.5F,
                                   .y = static_cast<float>(box.origin.y + box.size.height * fy)};
        event.button = au::MouseButton::Left;
        event.action = au::MouseAction::Press;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
        event.action = au::MouseAction::Release;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    [[nodiscard]] auto menu_presented() const noexcept -> bool { return view_->context_menu() != nullptr; }
    [[nodiscard]] auto menu_open() const noexcept -> bool {
        return view_->context_menu() != nullptr && view_->context_menu()->is_open();
    }
    [[nodiscard]] auto overlay_count() const -> std::size_t { return host_->overlay_count(); }
    [[nodiscard]] auto written() const -> std::string { return connection_->written_text(); }
    [[nodiscard]] auto selected_text() -> std::string { return view_->selected_text(); }

    auto clear_written() -> void { connection_->written.clear(); }

    /// @brief 前置条件：Headless 缩放为 1（dp 即像素），且格网容得下本文件用到的行与列。
    ///
    /// 条目落点按菜单内容盒的高度分数算，与格网无关，但左键落点要经 `ui::rect_for` 换算，
    /// 格数不够时那些点会掉到钳位边界上，选区就不是用例所设的那一行了。
    [[nodiscard]] auto preflight() const noexcept -> bool {
        return scale_ == 1.0F && geometry_.rows > 8U && geometry_.columns > 6U;
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。

    std::string clipboard{};             ///< 替身给出的剪贴板内容。
    std::vector<std::string> copied{};   ///< 写进剪贴板的文本，按序。
    std::size_t read_calls = 0;
    std::size_t write_calls = 0;
    std::size_t confirm_calls = 0;
    std::size_t warned_chunks = 0;       ///< 警告里报给用户的待发块数。
    std::function<void(bool)> held_answer;  ///< 替身扣下的确认回调，用例决定何时放行。

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

    /// @brief 帧 0 之后重取格网几何（左键落点要按格子换算，`ui::rect_for` 与控件同一坐标空间）。
    auto prime_geometry() -> void {
        const au::Rect box = view_->paint_bounds();
        scale_ = window_.surface().scale_factor();
        origin_x_ = static_cast<double>(box.origin.x);
        origin_y_ = static_cast<double>(box.origin.y);
        const auto metrics = au::render::FontEngine::monospace_cell(test_font(), scale_);
        geometry_ = borealis::ui::make_geometry(
            CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px}, scale_,
            LogicalSize{static_cast<double>(box.size.width), static_cast<double>(box.size.height)}, kPaddingDp);
    }
};

}  // namespace

AURORA_TEST_CASE(menu_state_opens_the_popup_without_touching_the_clipboard) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "clip";
    h.right_click(1U, 2U);
    h.render();
    // 菜单态把动作留给用户点，因此右键那一刻既不读剪贴板也不写字节；浮层恰好多出一个（菜单）。
    AURORA_TEST_REQUIRE(h.menu_open());
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);
    AURORA_TEST_CHECK_EQ(h.read_calls, 0U);
    AURORA_TEST_CHECK_EQ(h.write_calls, 0U);
    AURORA_TEST_CHECK(h.written().empty());
}

AURORA_TEST_CASE(menu_paste_item_reads_the_clipboard_at_the_next_frame) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "echo hi";
    h.right_click(1U, 2U);
    h.render();
    h.click_menu_item(0.75);  // 内容盒下半是「粘贴」
    // 点击回调里只做「攒下请求」：剪贴板是 IO，不在事件回调里做（AGENTS.md §4.5 第 25 条）。
    AURORA_TEST_CHECK_EQ(h.read_calls, 0U);
    AURORA_TEST_CHECK(h.written().empty());
    h.render();
    AURORA_TEST_CHECK_EQ(h.read_calls, 1U);
    AURORA_TEST_CHECK_EQ(h.written(), "echo hi");
    // 点到即收菜单，且不残留第二个浮层（菜单浮层常驻，只开关）。
    AURORA_TEST_REQUIRE(!h.menu_open());
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);
}

AURORA_TEST_CASE(menu_copy_item_writes_only_after_a_frame) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1b[?25l\x1b[3;1HABCD");
    h.drag_select(2U, 0U, 3U);
    h.right_click(2U, 1U);
    h.render();
    h.click_menu_item(0.25);  // 内容盒上半是「复制」
    AURORA_TEST_CHECK_EQ(h.write_calls, 0U);
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.copied.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.copied[0], "ABCD");
}

AURORA_TEST_CASE(greyed_copy_item_takes_no_action_without_a_selection) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "clip";
    h.right_click(1U, 2U);
    h.render();
    // 置灰项就是不可点的项（裁决 7.38⑥ F-b）：点它既不复制也不粘贴，连菜单都还开着——
    // 「什么都不发生」才是这条判据，只比剪贴板计数会被「无选区时复制本就产出空文本」蒙过去。
    h.click_menu_item(0.25);
    h.render();
    AURORA_TEST_REQUIRE(h.menu_open());
    AURORA_TEST_CHECK_EQ(h.write_calls, 0U);
    AURORA_TEST_CHECK_EQ(h.read_calls, 0U);
    AURORA_TEST_CHECK(h.written().empty());
}

AURORA_TEST_CASE(outside_click_dismisses_the_menu_and_acts_on_nothing) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "clip";
    h.right_click(1U, 2U);
    h.render();
    // 外部点击由宿主判定、由本控件在指针入口驱动（框架不代劳派发）：这一击只用来关菜单。
    h.pointer(au::MouseAction::Press, au::MouseButton::Left, 6U, 5U);
    AURORA_TEST_REQUIRE(!h.menu_open());
    h.pointer(au::MouseAction::Release, au::MouseButton::Left, 6U, 5U);
    h.render();
    AURORA_TEST_CHECK_EQ(h.read_calls, 0U);
    AURORA_TEST_CHECK_EQ(h.write_calls, 0U);
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_CHECK(h.selected_text().empty());  // 关菜单那一击不该顺手起一个选区
}

AURORA_TEST_CASE(copy_on_select_state_copies_without_a_menu) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::CopyOnSelect;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1b[?25l\x1b[3;1HABCD");
    h.drag_select(2U, 0U, 3U);
    h.right_click(2U, 1U);
    // 本态不发菜单（裁决 7.38⑥）：一个浮层都不该挂上去。
    AURORA_TEST_REQUIRE(!h.menu_presented());
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.copied.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.copied[0], "ABCD");
}

AURORA_TEST_CASE(copy_on_select_state_without_selection_writes_nothing) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::CopyOnSelect;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "keep-me";
    h.right_click(1U, 2U);
    h.render();
    // 无选区时回 `None` 而不是发一次空复制：空文本会覆盖用户剪贴板里原有的内容。
    AURORA_TEST_CHECK_EQ(h.write_calls, 0U);
    AURORA_TEST_CHECK(h.copied.empty());
}

AURORA_TEST_CASE(paste_state_sends_the_clipboard_on_the_next_frame) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::Paste;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "ls -la";
    h.right_click(1U, 2U);
    AURORA_TEST_CHECK_EQ(h.read_calls, 0U);  // 读也在帧边界，不在事件回调
    h.render();
    AURORA_TEST_CHECK_EQ(h.written(), "ls -la");
    AURORA_TEST_CHECK_EQ(h.confirm_calls, 0U);  // 单行不警告
}

AURORA_TEST_CASE(a_runtime_right_click_switch_changes_the_next_click) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::CopyOnSelect;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1b[?25l\x1b[3;1HABCD");
    h.drag_select(2U, 0U, 3U);
    h.right_click(2U, 1U);
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.copied.size(), 1U);
    AURORA_TEST_REQUIRE(!h.menu_open());

    // 运行期换成菜单态：裁决 7.52 的 S4① 走的是「不重建视口」那条路，故既不丢既有选区，也不
    // 把上一次的动作补发一遍——变的只有「下一次右键」的处置。
    options.right_click = borealis::ui::RightClickAction::ContextMenu;
    h.apply_options(options);
    AURORA_TEST_CHECK_EQ(h.selected_text(), "ABCD");
    h.right_click(2U, 1U);
    h.render();
    AURORA_TEST_CHECK(h.menu_open());
    AURORA_TEST_CHECK_EQ(h.write_calls, 1U);
    AURORA_TEST_CHECK_EQ(h.copied.size(), 1U);
}

AURORA_TEST_CASE(bracketed_paste_wraps_the_text_verbatim_and_never_warns) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::Paste;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1b[?2004h");  // 须经真实状态机置位，而不是注入模式快照
    h.clipboard = "a\nb\nc";
    h.right_click(1U, 2U);
    h.render();
    // 三行原样包进一对包裹序列：既不分块节流也不弹警告，行尾归 shell 判定（裁决 7.33）。
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[200~a\nb\nc\x1B[201~");
    AURORA_TEST_CHECK_EQ(h.confirm_calls, 0U);
}

AURORA_TEST_CASE(multiline_paste_waits_for_confirmation_then_sends_every_chunk) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::Paste;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "one\ntwo\nthree";
    h.right_click(1U, 2U);
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.confirm_calls, 1U);
    AURORA_TEST_CHECK_EQ(h.written(), "");  // 未确认：一个字节也不发
    AURORA_TEST_CHECK_EQ(h.warned_chunks, 3U);

    // 先拒：什么都不发，且请求就此作废。
    AURORA_TEST_REQUIRE(h.held_answer != nullptr);
    h.held_answer(false);
    h.render();
    AURORA_TEST_CHECK(h.written().empty());

    // 再放一次粘贴请求：确认后按块序发完，拼起来正是剪贴板原文（无调度器时一次发完）。
    h.right_click(1U, 2U);
    h.render();
    AURORA_TEST_REQUIRE(h.held_answer != nullptr);
    h.held_answer(true);
    h.render();
    AURORA_TEST_CHECK_EQ(h.written(), "one\ntwo\nthree");
}

AURORA_TEST_CASE(confirmation_answer_is_only_honoured_once) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::Paste;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "x\ny";
    h.right_click(1U, 2U);
    h.render();
    AURORA_TEST_REQUIRE(h.held_answer != nullptr);
    // 对话框的按钮理论上可被点到两次（连点、或关闭动画期间的重复派发）：放行第二次不得重发。
    h.held_answer(true);
    h.held_answer(true);
    h.render();
    AURORA_TEST_CHECK_EQ(h.written(), "x\ny");
}

AURORA_TEST_CASE(empty_clipboard_sends_nothing_and_raises_no_warning) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::Paste;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.right_click(1U, 2U);
    h.render();
    AURORA_TEST_CHECK_EQ(h.read_calls, 1U);  // 读了，但空文本不往下走
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_CHECK_EQ(h.confirm_calls, 0U);
}

AURORA_TEST_CASE(filter_policy_collapses_the_text_and_skips_the_warning) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::Paste;
    options.paste.newline = borealis::term::PasteNewlinePolicy::Filter;
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "a\nb\nc";
    h.right_click(1U, 2U);
    h.render();
    // 判据取待发结果：剥成一行就不该警告（裁决 7.33③）。
    AURORA_TEST_CHECK_EQ(h.confirm_calls, 0U);
    AURORA_TEST_CHECK_EQ(h.written(), "abc");
}

AURORA_TEST_CASE(multiline_warning_holds_until_the_dialog_answers) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::Paste;
    Harness h(options, true, false);  // 走生产的模态对话框
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "rm -rf /\nagain";
    h.right_click(1U, 2U);
    h.render();
    // 生产的确认腿是浮层上的模态对话框：多挂一个浮层，且在用户答话之前不发字节。
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);
    AURORA_TEST_CHECK(h.written().empty());
}

AURORA_TEST_CASE(multiline_paste_without_a_host_sends_nothing) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::Paste;
    Harness h(options, false, false);  // 没有浮层宿主：问不了，也就不粘贴
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "a\nb\nc";
    h.right_click(1U, 2U);
    h.render();
    // 未经确认不替用户按下回车——这条链路上「什么都不发生」是正确结果（裁决 7.41④）。
    AURORA_TEST_CHECK(h.written().empty());
}

AURORA_TEST_CASE(menu_state_without_a_host_acts_on_nothing) {
    auto options = TerminalView::InteractionOptions{};
    options.right_click = borealis::ui::RightClickAction::ContextMenu;
    Harness h(options, false);
    AURORA_TEST_REQUIRE(h.preflight());
    h.clipboard = "clip";
    h.right_click(1U, 2U);
    h.render();
    AURORA_TEST_REQUIRE(!h.menu_presented());
    AURORA_TEST_CHECK_EQ(h.read_calls, 0U);
    AURORA_TEST_CHECK(h.written().empty());
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_right_click_paste
