/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（quick select 模式）+ include/borealis/ui/quick_select.h
/// 测试说明: 以无头窗口 + 真实事件派发驱动「屏上字节 → 行扫描 → 标签块绘制 → 标签键 → 帧边界写
///           剪贴板」这条链（`SPEC.FEAT.INTERACT.02` 延后子项的界面腿，稿 §2／§5／§6／§8）。
///           断的是**链路**而非扫描件本身（后者由 `utest_quick_select` 逐条覆盖），值得单独证的有
///           六件事：
///           ① 三档候选（URL/路径/哈希）按阅读序进表，标签与冻结文本逐字相等；
///           ② 标签盒与候选起点同格、宽 = 标签字符数 × 格宽，块内取到 selection 底；
///           ③ 按完整标签后**下一帧**剪贴板替身才收到冻结文本，会话写出不增，同一物理键的文本
///              事件被吞掉一次（第二次文本照常进会话，证吞位是一次性的）；
///           ④ Escape／非法键／指针按下／视口失焦各退模；
///           ⑤ 滚轮退模但**照常滚动**（回看位置变化）；
///           ⑥ 输出推窗退模；双字符档 Backspace 退一位、空前缀不退模；无候选时开模为 no-op。
///           真实剪贴板**永不参与**：`Presentation::clipboard_write` 每一例都装替身。

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/render/font_engine.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/quick_select.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_quick_select {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::CellPixels;
using borealis::ui::GridGeometry;
using borealis::ui::LogicalSize;
using borealis::ui::PaletteSpec;
using borealis::ui::QuickSelectCandidate;
using borealis::ui::QuickSelectKind;
using borealis::ui::Rect;
using borealis::ui::RgbaColor;
using borealis::ui::TerminalView;
using borealis::ui::Typography;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 40;
constexpr float kPaddingDp = 4.0F;
constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 440;

auto width_policy = std::make_shared<UnicodeWidthPolicy>();

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

/// @brief 选区色与默认底刻意拉开：缺省色板的 `basic[8]` 与 `default_background` 都是黑色，
///        两档同色时「块内是 selection 底」的像素断言就成了空转。
[[nodiscard]] auto test_palette() -> PaletteSpec {
    PaletteSpec spec{};
    spec.basic[8] = RgbaColor{.red = 44, .green = 96, .blue = 200, .alpha = 255};
    spec.default_foreground = RgbaColor{.red = 232, .green = 232, .blue = 232, .alpha = 255};
    spec.default_background = RgbaColor{.red = 12, .green = 12, .blue = 12, .alpha = 255};
    return spec;
}

/// @brief 驱动台：会话 + 替身连接 + 视口控件 + 无头窗口 + 真焦点管理器 + 剪贴板替身。
class Harness {
  public:
    Harness()
        : window_(make_window()), palette_(test_palette()),
          session_(std::make_unique<Session>(own_connection(), kNominalSize, kScrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, palette_, test_font(), Typography{}, kPaddingDp,
                                               std::chrono::milliseconds{500}, TerminalView::InteractionOptions{})),
          root_(std::static_pointer_cast<au::Widget>(view_)) {
        auto presentation = TerminalView::Presentation{};
        presentation.clipboard_write = [this](std::string_view text) -> void {
            clipboard.emplace_back(text);
        };
        view_->set_presentation(std::move(presentation));
        session_->start();
        focus_.set_root(&root_.widget());
        focus_.set_focus(view_.get());
        render();          // 帧 0：布局发生在这里
        prime_geometry();  // 落点与像素取样要按真实格网换算
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

    /// @brief 把 @p text 放到网格第 @p row 行（0 基）再排帧。
    auto feed_at(std::size_t row, std::string_view text) -> void {
        feed("\x1b[" + std::to_string(row + 1U) + ";1H" + std::string{text});
    }

    /// @brief 排帧：`on_frame` 落地攒下的动作，再走一遍真实布局与绘制。
    auto render() -> void {
        do {
            view_->on_frame();
            (void)window_.present_root(root_);
        } while (session_->has_damage());
    }

    /// @brief 开模并排一帧（标签盒与像素都在这一帧里落定）。
    auto open() -> void {
        view_->open_quick_select();
        render();
    }

    /// @brief 派发一次按键按下，返回派发器给出的「是否被消费」；**不排帧**。
    ///
    /// 「复制留到帧边界」那条判据要求按键与 `flush_copy_request` 之间隔着一次 `on_frame`，
    /// 故这里刻意不替调用方排帧——顺序由用例自己写清。
    auto press(au::KeyCode key, au::ModifierKey modifiers = au::ModifierKey::None) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        event.modifiers = modifiers;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 派发一次文本输入，返回是否被消费；不排帧。
    auto type(std::string_view utf8) -> bool {
        au::TextInputEvent event;
        event.text = std::string{utf8};
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 发一个指针事件（落点取该格中心；行列是**屏幕**坐标，0 基）。
    auto pointer(au::MouseAction action, std::size_t row, std::size_t column) -> void {
        au::MouseEvent event;
        const Rect band = borealis::ui::rect_for(geometry_, row, column, column + 1U);
        event.position = au::Point{.x = static_cast<float>(origin_x_ + band.x + band.width * 0.5),
                                   .y = static_cast<float>(origin_y_ + band.y + band.height * 0.5)};
        event.button = au::MouseButton::Left;
        event.action = action;
        event.modifiers = au::ModifierKey::None;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 滚轮：@p rows 为正向上看更早的行，负向反之。
    auto scroll(int rows) -> void {
        au::ScrollEvent event;
        event.position = grid_center();
        event.delta_y = static_cast<float>(rows);
        (void)au::EventDispatcher::dispatch(root_.widget(), event);
        render();
    }

    /// @brief 焦点开关：经真实焦点管理器驱动，退模判据读的就是它。
    auto set_focused(bool focused) -> void {
        if (focused) {
            focus_.set_root(&root_.widget());
            focus_.set_focus(view_.get());
        } else {
            focus_.clear();
        }
    }

    [[nodiscard]] auto active() const -> bool { return view_->quick_select_active(); }
    [[nodiscard]] auto typed() const -> std::string { return std::string{view_->quick_select_typed()}; }
    [[nodiscard]] auto candidates() const -> std::span<const QuickSelectCandidate> {
        return view_->quick_select_candidates();
    }
    [[nodiscard]] auto label_boxes() const -> std::span<const TerminalView::QuickSelectLabelBox> {
        return view_->quick_select_label_boxes();
    }
    [[nodiscard]] auto written() const -> std::string { return connection_->written_text(); }
    [[nodiscard]] auto back_rows() const -> std::size_t { return view_->scrollback_rows_from_bottom(); }
    [[nodiscard]] auto geometry() const noexcept -> const GridGeometry & { return geometry_; }

    /// @brief 一份当前帧缓冲（RGBA8，物理像素）。
    [[nodiscard]] auto pixels() const -> std::vector<std::uint8_t> {
        std::vector<std::uint8_t> out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    /// @brief 控件局部 dp 盒内的分数点取色。
    [[nodiscard]] auto sample(const Rect &local, double fx, double fy) const -> RgbaColor {
        const auto px = pixels();
        const double x = origin_x_ + local.x + (local.width * fx);
        const double y = origin_y_ + local.y + (local.height * fy);
        const std::size_t index =
            ((static_cast<std::size_t>(y) * buffer_width()) + static_cast<std::size_t>(x)) * 4U;
        return RgbaColor{px[index], px[index + 1U], px[index + 2U], px[index + 3U]};
    }

    /// @brief 盒内等于 @p color 的像素数（标签块是实心底，字形只会占掉其中一部分）。
    [[nodiscard]] auto count_in(const Rect &local, RgbaColor color) const -> std::size_t {
        const auto px = pixels();
        const auto left = static_cast<int>(origin_x_ + local.x);
        const auto top = static_cast<int>(origin_y_ + local.y);
        std::size_t seen = 0U;
        for (int y = top; y < top + static_cast<int>(local.height); ++y) {
            for (int x = left; x < left + static_cast<int>(local.width); ++x) {
                const std::size_t index =
                    ((static_cast<std::size_t>(y) * buffer_width()) + static_cast<std::size_t>(x)) * 4U;
                if (px[index] == color.red && px[index + 1U] == color.green && px[index + 2U] == color.blue &&
                    px[index + 3U] == color.alpha) {
                    ++seen;
                }
            }
        }
        return seen;
    }

    /// @brief 空行格子取默认底色：不挑候选行，避开标签块与字形。
    [[nodiscard]] auto blank_cell_color() const -> RgbaColor {
        const Rect cell = borealis::ui::rect_for(geometry_, geometry_.rows - 1U, 0U, 1U);
        return sample(cell, 0.5, 0.5);
    }

    std::vector<std::string> clipboard;  ///< 剪贴板替身收到过的文本，按序。
    const PaletteSpec &palette() const noexcept { return palette_; }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    [[nodiscard]] auto buffer_width() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowWidth) * scale_);
    }

    [[nodiscard]] auto buffer_height() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowHeight) * scale_);
    }

    /// @brief 造替身连接：所有权交给会话，裸指针留在本类驱动投递与观测。
    [[nodiscard]] auto own_connection() -> std::unique_ptr<Connection> {
        auto owned = std::make_unique<FakeConnection>();
        connection_ = owned.get();
        return owned;
    }

    /// @brief 帧 0 之后重取格网几何（与 `itest_url_link` 的同一算式）。
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

    [[nodiscard]] auto grid_center() const -> au::Point {
        const Rect cell = borealis::ui::rect_for(geometry_, geometry_.rows / 2U, geometry_.columns / 2U,
                                                 geometry_.columns / 2U + 1U);
        return au::Point{.x = static_cast<float>(origin_x_ + cell.x + cell.width * 0.5),
                         .y = static_cast<float>(origin_y_ + cell.y + cell.height * 0.5)};
    }

    au::Window window_;
    PaletteSpec palette_{};
    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher pointer_dispatcher_;
    GridGeometry geometry_{};
    float scale_ = 1.0F;
    double origin_x_ = 0.0;
    double origin_y_ = 0.0;
};

[[nodiscard]] auto kind_of(const QuickSelectCandidate &candidate) -> int {
    return static_cast<int>(candidate.kind);
}

}  // namespace

// ============================================================
// 判据 1：三档候选进表
// ============================================================

AURORA_TEST_CASE(three_kinds_are_scanned_in_reading_order_with_labels_and_frozen_text) {
    Harness h;
    h.feed_at(2U, "https://example.com/x");
    h.feed_at(3U, "/usr/local/bin");
    h.feed_at(4U, "abc1234");
    h.open();

    AURORA_TEST_REQUIRE(h.active());
    const auto found = h.candidates();
    AURORA_TEST_REQUIRE(found.size() == 3U);
    AURORA_TEST_CHECK_EQ(kind_of(found[0]), static_cast<int>(QuickSelectKind::Url));
    AURORA_TEST_CHECK_EQ(found[0].text, "https://example.com/x");
    AURORA_TEST_CHECK_EQ(found[0].label, "a");
    // `span.row` 是**存储行序**：本驱动台的可见窗比 24 行标称网格矮，缩短视口时顶部的空白行
    // 进了历史（`Storage::set_rows` 的底部锚定），故屏幕第 2 行对应存储行 `窗顶 + 2`。若实现
    // 误把屏幕行存进候选表，这里会读到 2 而不是窗顶偏移后的值。
    const std::size_t window_top =
        h.geometry().rows < kNominalSize.rows ? kNominalSize.rows - h.geometry().rows : 0U;
    AURORA_TEST_CHECK_EQ(found[0].span.row, window_top + 2U);
    AURORA_TEST_CHECK_EQ(kind_of(found[1]), static_cast<int>(QuickSelectKind::Path));
    AURORA_TEST_CHECK_EQ(found[1].text, "/usr/local/bin");
    AURORA_TEST_CHECK_EQ(found[1].label, "s");
    AURORA_TEST_CHECK_EQ(found[1].span.row, found[0].span.row + 1U);
    AURORA_TEST_CHECK_EQ(kind_of(found[2]), static_cast<int>(QuickSelectKind::Hash));
    AURORA_TEST_CHECK_EQ(found[2].text, "abc1234");
    AURORA_TEST_CHECK_EQ(found[2].label, "d");
    AURORA_TEST_CHECK_EQ(found[2].span.row, found[1].span.row + 1U);
}

// ============================================================
// 判据 2：标签盒几何与配色
// ============================================================

AURORA_TEST_CASE(label_blocks_sit_on_the_candidate_start_and_use_the_selection_color) {
    Harness h;
    h.feed_at(2U, "https://example.com/x");
    h.open();
    AURORA_TEST_REQUIRE(h.active());

    const auto boxes = h.label_boxes();
    AURORA_TEST_REQUIRE(boxes.size() == 1U);
    AURORA_TEST_CHECK_EQ(boxes[0].candidate, std::size_t{0U});
    // 盒与候选起点同格、宽恰一格（单字符标签）、高恰一行。
    const Rect expected = borealis::ui::rect_for(h.geometry(), 2U, 0U, 1U);
    AURORA_TEST_CHECK_EQ(boxes[0].box.x, expected.x);
    AURORA_TEST_CHECK_EQ(boxes[0].box.y, expected.y);
    AURORA_TEST_CHECK_EQ(boxes[0].box.width, h.geometry().cell_width);
    AURORA_TEST_CHECK_EQ(boxes[0].box.height, h.geometry().cell_height);

    // 块内取到 selection 底；盒外的空白格是默认底。
    const RgbaColor selection = borealis::ui::selection_color(h.palette());
    AURORA_TEST_CHECK(h.count_in(boxes[0].box, selection) > 0U);
    AURORA_TEST_CHECK(h.blank_cell_color() == h.palette().default_background);
}

// ============================================================
// 判据 3：按标签复制
// ============================================================

AURORA_TEST_CASE(pressing_a_label_copies_the_frozen_text_at_the_frame_boundary) {
    Harness h;
    h.feed_at(2U, "https://example.com/x");
    h.open();
    AURORA_TEST_REQUIRE(h.active());
    const std::string written_before = h.written();

    // 按下那一刻只置位：剪贴板不动、会话也不动。
    AURORA_TEST_CHECK(h.press(au::KeyCode::A));
    AURORA_TEST_CHECK_FALSE(h.active());
    AURORA_TEST_CHECK(h.clipboard.empty());
    AURORA_TEST_CHECK_EQ(h.written(), written_before);

    // 下一帧落地：冻结文本进剪贴板，码点仍没有漏进会话。
    h.render();
    AURORA_TEST_REQUIRE(h.clipboard.size() == 1U);
    AURORA_TEST_CHECK_EQ(h.clipboard[0], "https://example.com/x");
    AURORA_TEST_CHECK_EQ(h.written(), written_before);

    // 同一物理键的文本事件被吞掉一次；第二次文本照常进会话（吞位是一次性的）。
    AURORA_TEST_CHECK(h.type("a"));
    AURORA_TEST_CHECK_EQ(h.written(), written_before);
    AURORA_TEST_CHECK(h.type("b"));
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"b"});
}

// ============================================================
// 判据 4：Escape／非法键／指针按下／失焦
// ============================================================

AURORA_TEST_CASE(escape_invalid_key_pointer_and_focus_loss_all_leave_the_mode) {
    Harness h;
    h.feed_at(2U, "https://example.com/x");

    h.open();
    AURORA_TEST_REQUIRE(h.active());
    AURORA_TEST_CHECK(h.press(au::KeyCode::Escape));
    AURORA_TEST_CHECK_FALSE(h.active());

    // 非法键：`z` 不在字母集里，前缀不可能完成任何标签。
    h.open();
    AURORA_TEST_REQUIRE(h.active());
    AURORA_TEST_CHECK(h.press(au::KeyCode::Z));
    AURORA_TEST_CHECK_FALSE(h.active());
    AURORA_TEST_CHECK(h.type("z"));  // 可打印档的同一物理键也不许漏进会话
    AURORA_TEST_CHECK(h.written().empty());

    // 指针按下：只退出，不顺势起选区或开链接。
    h.open();
    AURORA_TEST_REQUIRE(h.active());
    h.pointer(au::MouseAction::Press, 3U, 3U);
    AURORA_TEST_CHECK_FALSE(h.active());
    h.pointer(au::MouseAction::Release, 3U, 3U);

    // 视口失焦：键已经不会进来，标签没有意义。
    h.open();
    AURORA_TEST_REQUIRE(h.active());
    h.set_focused(false);
    h.render();
    AURORA_TEST_CHECK_FALSE(h.active());
}

// ============================================================
// 判据 4 的滚轮档：退模但照常滚动
// ============================================================

AURORA_TEST_CASE(wheel_leaves_the_mode_but_still_scrolls_the_viewport) {
    Harness h;
    std::string text;
    for (int index = 0; index < 30; ++index) {
        text += "row " + std::to_string(index) + "\r\n";
    }
    text += "https://example.com/x";
    h.feed(text);
    h.open();
    AURORA_TEST_REQUIRE(h.active());
    const std::size_t before = h.back_rows();

    h.scroll(1);
    AURORA_TEST_CHECK_FALSE(h.active());
    AURORA_TEST_CHECK(h.back_rows() > before);
}

// ============================================================
// 判据 4 的推窗档：输出把可见窗顶推走即退模
// ============================================================

AURORA_TEST_CASE(output_pushing_the_window_leaves_the_mode) {
    Harness h;
    std::string text;
    for (int index = 0; index < 30; ++index) {
        text += "row " + std::to_string(index) + "\r\n";
    }
    text += "https://example.com/x";
    h.feed(text);
    h.open();
    AURORA_TEST_REQUIRE(h.active());

    h.feed("\r\nnext");
    AURORA_TEST_CHECK_FALSE(h.active());
}

// ============================================================
// 判据 5：双字符档的 Backspace
// ============================================================

AURORA_TEST_CASE(backspace_steps_back_a_two_letter_prefix_without_leaving) {
    Harness h;
    std::string text;
    for (int index = 0; index < 10; ++index) {
        text += "https://e.com/" + std::to_string(index);
        if (index != 9) {
            text += "\r\n";
        }
    }
    h.feed(text);
    h.open();
    AURORA_TEST_REQUIRE(h.active());
    const auto found = h.candidates();
    AURORA_TEST_REQUIRE(found.size() == 10U);
    // 十个候选 ⇒ 全部双字符标签（稿 D3），首个是 `aa`。
    AURORA_TEST_CHECK_EQ(found[0].label, "aa");
    AURORA_TEST_CHECK_EQ(found[1].label, "as");
    // 双字符标签的块宽是两格。
    const auto boxes = h.label_boxes();
    AURORA_TEST_REQUIRE(boxes.size() == 10U);
    AURORA_TEST_CHECK_EQ(boxes[0].box.width, 2.0 * h.geometry().cell_width);

    AURORA_TEST_CHECK(h.press(au::KeyCode::A));
    AURORA_TEST_CHECK(h.active());
    AURORA_TEST_CHECK_EQ(h.typed(), "a");

    // 空前缀退格不退模：稿 §2 把「退一位」与「退模」分成两件事。
    AURORA_TEST_CHECK(h.press(au::KeyCode::Backspace));
    AURORA_TEST_CHECK(h.active());
    AURORA_TEST_CHECK_EQ(h.typed(), "");
    AURORA_TEST_CHECK(h.press(au::KeyCode::Backspace));
    AURORA_TEST_CHECK(h.active());
    AURORA_TEST_CHECK_EQ(h.typed(), "");

    // 退掉一位后仍可完成：`a` + `s` 命中第二条。
    AURORA_TEST_CHECK(h.press(au::KeyCode::A));
    AURORA_TEST_CHECK_EQ(h.typed(), "a");
    AURORA_TEST_CHECK(h.press(au::KeyCode::S));
    AURORA_TEST_CHECK_FALSE(h.active());
    h.render();
    AURORA_TEST_REQUIRE(h.clipboard.size() == 1U);
    AURORA_TEST_CHECK_EQ(h.clipboard[0], "https://e.com/1");
}

// ============================================================
// 判据 6：无候选时开模为 no-op
// ============================================================

AURORA_TEST_CASE(opening_without_candidates_is_a_no_op) {
    Harness h;
    h.feed_at(2U, "plain words only");
    h.open();
    AURORA_TEST_CHECK_FALSE(h.active());
    AURORA_TEST_CHECK(h.candidates().empty());
    AURORA_TEST_CHECK(h.label_boxes().empty());
    AURORA_TEST_CHECK(h.clipboard.empty());
}

#endif  // AURORA_BACKEND_HEADLESS

}  // namespace borealis::test_cases::itest_quick_select
