// 搜索浮层的界面腿证人（SPEC.FEAT.INTERACT.04，判据入册为裁决 7.81）。
//
// 本套件覆盖判据文 `codespec/UI_SEARCH.draft.md` 里与浮层本体有关的条目：A1-a/b/c/d（锚点、档位、
// 条体上屏与像素、两枚开关档位的字面与可读名）、A2-a/b（计数占位的措辞与两枚开关的真控），B0
// （空查询的占位档）、B2（零匹配）、B3（上限即 `+` 后缀）、B4（非法表达式保留旧表）、B5（按
// Enter 应用的重开提示）、D1-a（打开即把焦点交给输入框）、D4（两枚开关翻转即提交；正则档的每键
// 不扫）、D7（关闭清高亮而保留查询文本与回看位置）、E1~E3（三档宽度与两行档的降级形态）、
// F1-c（重开保留文本而不自动重扫）、F2-a/b（关闭按钮与点条内空白）、F3-a（条内点击不得动选区）。
//
// 装配层的 `search.open` + `Ctrl+F` 登记不在本套件范围内（`src/main.cpp` 不链进测试目标），
// 由 `SPEC.FEAT.INTERACT.04` 的装配腿自证；真机走查照旧未做（AGENTS.md §4.6 第 33 条）。
// 断言一律经真实派发（`EventDispatcher`）与真实命中链（`hit_test_chain`），像素腿走
// `HeadlessSurface` 的 RGBA8 帧缓冲（AGENTS.md §4.4 第 19 条）。

#include <algorithm>
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

#include "borealis/grid/storage.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/search.h"

#include "aurora/aurora.h"
#include "aurora/app/shortcuts.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/render/font_engine.h"
#include "aurora/widget/button.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text_input.h"

#include "framework/aurora_test.h"

// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/search_overlay.h"
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_search_overlay {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using aurora::BuildContext;
using aurora::Button;
using aurora::EventDispatcher;
using aurora::FocusManager;
using aurora::KeyCode;
using aurora::KeyCombo;
using aurora::KeyEvent;
using aurora::ModifierKey;
using aurora::MouseAction;
using aurora::MouseButton;
using aurora::MouseEvent;
using aurora::Node;
using aurora::OverlayHost;
using aurora::Point;
using aurora::Rect;
using aurora::ShortcutRegistry;
using aurora::ShortcutScope;
using aurora::TextInput;
using aurora::TextInputEvent;
namespace au = aurora;

using CellPixels = borealis::ui::CellPixels;
using Connection = borealis::session::Connection;
using ConnectionEvents = borealis::session::ConnectionEvents;
using GridGeometry = borealis::ui::GridGeometry;
using LogicalSize = borealis::ui::LogicalSize;
using PaletteSpec = borealis::ui::PaletteSpec;
using SearchTier = ui::SearchTier;
using Session = borealis::session::Session;
using Size = borealis::session::Size;
using TerminalView = borealis::ui::TerminalView;
using UnicodeWidthPolicy = borealis::term::UnicodeWidthPolicy;

constexpr Size kNominalSize{80U, 24U};
constexpr float kPaddingDp = 4.0F;

/// @brief 驱动台专用连接替身：写出字节落 `written` 供逐字节断言，喂入经 `deliver` 走真实解码链。
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

    std::vector<std::byte> written;

    auto deliver(std::string_view text) -> void {
        std::vector<std::byte> raw;
        raw.reserve(text.size());
        for (const char ch : text) {
            raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
        events_->on_bytes(std::span<const std::byte>(raw.data(), raw.size()));
    }

    [[nodiscard]] auto written_text() const -> std::string {
        return std::string(reinterpret_cast<const char *>(written.data()), written.size());
    }

private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

[[nodiscard]] auto test_font() -> au::Font {
    return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
}

/// @brief 单个用例的整套现场：连接替身 + 会话 + 视口 + 浮层宿主 + 派发器与几何度量。
///
/// `window_` 最先声明、最后析构（帧缓冲须活到取样结束）；`host_` 与 `view_` 的生命周期靠析构体
/// 先摘子节点再做 `view_.reset()` 保证（`~SearchOverlay` 会向宿主摘自己那一层）。
class Harness final {
public:
    explicit Harness(int width_dp = 520, int height_dp = 260, std::size_t scrollback = 120)
        : window_width_(width_dp), window_height_(height_dp),
          session_(std::make_unique<Session>(own_connection(), kNominalSize, scrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, PaletteSpec{}, test_font(), ui::Typography{},
                                               kPaddingDp, std::chrono::milliseconds{500},
                                               TerminalView::InteractionOptions{})),
          host_(std::make_shared<OverlayHost>(Node{std::static_pointer_cast<au::Widget>(view_)})),
          root_(std::static_pointer_cast<au::Widget>(host_)) {
        borealis::ui::install_settings_strings();
        view_->set_overlay_host(*host_);
        view_->set_search_dependencies(shortcuts_, focus_);
        session_->start();
        focus_.set_root(&root_.widget());
        focus_.set_focus(view_.get());
        render();
        prime_geometry();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    ~Harness() {
        // 成员逆序析构会让 `host_` 先走，而 `~SearchOverlay` 还要 host_.remove_overlay(...)：
        // 先清掉宿主全部子节点再放 view_，那一次宿主调用就成了空转。
        if (host_ != nullptr) {
            host_->adopt_children(std::vector<Node>{});
        }
        view_.reset();
    }

    [[nodiscard]] auto view() -> TerminalView * { return view_.get(); }
    [[nodiscard]] auto overlay() const -> const ui::SearchOverlay * { return view_->search_overlay(); }
    [[nodiscard]] auto focused() -> au::Widget * { return focus_.focused(); }
    [[nodiscard]] auto shortcuts() -> ShortcutRegistry & { return shortcuts_; }
    [[nodiscard]] auto written() const -> std::string { return connection_->written_text(); }
    auto clear_written() -> void { connection_->written.clear(); }
    [[nodiscard]] auto overlay_count() const -> std::size_t { return host_->overlay_count(); }

    [[nodiscard]] auto query() const -> ui::SearchQuery { return view_->search_query(); }
    [[nodiscard]] auto matches() const -> const ui::SearchMatches * { return view_->search_matches(); }
    [[nodiscard]] auto scans() const -> std::size_t { return view_->search_scan_count(); }
    [[nodiscard]] auto pattern_invalid() const -> bool { return view_->search_pattern_invalid(); }
    [[nodiscard]] auto pending_submit() const -> bool { return view_->search_pending_submit(); }
    [[nodiscard]] auto scrollback() const -> std::size_t {
        return view_->scrollback_rows_from_bottom();
    }
    [[nodiscard]] auto selected_text() -> std::string { return view_->selected_text(); }
    [[nodiscard]] auto count_text() const -> std::string {
        const ui::SearchOverlay *bar = overlay();
        return bar != nullptr ? bar->count_text() : std::string{};
    }

    auto feed(std::string_view text) -> void {
        connection_->deliver(text);
        render();
    }

    /// @brief 把会话与视口推到静止：每轮 `on_frame` 结算（扫描、几何、状态）后经场景根排一轮布局。
    auto render() -> void {
        do {
            view_->on_frame();
            (void)window_.present_root(root_);
        } while (session_->has_damage());
    }

    [[nodiscard]] auto type(std::string_view text) -> bool {
        bool handled = false;
        for (const char ch : text) {
            TextInputEvent event;
            event.text = std::string(1U, ch);
            handled = EventDispatcher::dispatch(root_.widget(), event, focus_) || handled;
        }
        return handled;
    }

    [[nodiscard]] auto press(KeyCode key) -> bool {
        KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        return EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 走快捷键登记表派发一次 `Escape`（与真机路径同源，`KeyCombo::matches` 只认 Down）。
    [[nodiscard]] auto escape_via_shortcut() -> bool {
        KeyEvent event;
        event.key = static_cast<int>(KeyCode::Escape);
        event.action = au::KeyAction::Down;
        return shortcuts_.handle(event, focus_.focused() != nullptr);
    }

    /// @brief 真实命中链的最深认领者（`hit_test_chain` 与派发同源；兼容入口 `hit_test()` 不判此）。
    [[nodiscard]] auto hit(float x, float y) -> au::Widget * {
        const std::vector<au::HitNode> chain = root_.widget().hit_test_chain(
            Point{x, y},
            Rect{.origin = Point{0.0F, 0.0F},
                 .size = au::Size{.width = static_cast<float>(window_width_),
                                  .height = static_cast<float>(window_height_)}},
            BuildContext{});
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    auto click(float x, float y) -> void {
        MouseEvent move;
        move.position = Point{x, y};
        move.action = MouseAction::Move;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), move, &focus_);
        MouseEvent down;
        down.position = Point{x, y};
        down.button = MouseButton::Left;
        down.action = MouseAction::Press;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), down, &focus_);
        MouseEvent up;
        up.position = Point{x, y};
        up.button = MouseButton::Left;
        up.action = MouseAction::Release;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), up, &focus_);
    }

    /// @brief 在浮层内容盒上以 2 dp 步长扫出第一个被该控件认领的点（真命中链，非几何推算）。
    [[nodiscard]] auto spot_of(au::Widget *target) -> std::optional<Point> {
        const ui::SearchOverlay *bar = overlay();
        if (bar == nullptr || bar->popup() == nullptr || target == nullptr) {
            return std::nullopt;
        }
        const Rect box = bar->popup()->content_bounds();
        for (float y = box.origin.y + 1.0F; y < box.origin.y + box.size.height; y += 2.0F) {
            for (float x = box.origin.x + 1.0F; x < box.origin.x + box.size.width; x += 2.0F) {
                if (hit(x, y) == target) {
                    return Point{x, y};
                }
            }
        }
        return std::nullopt;
    }

    /// @brief 在浮层内容盒上扫出**最后一个**被该控件认领的点（最底行最右列，供右缘点击用）。
    [[nodiscard]] auto right_spot_of(au::Widget *target) -> std::optional<Point> {
        const ui::SearchOverlay *bar = overlay();
        if (bar == nullptr || bar->popup() == nullptr || target == nullptr) {
            return std::nullopt;
        }
        const Rect box = bar->popup()->content_bounds();
        std::optional<Point> found;
        for (float y = box.origin.y + 1.0F; y < box.origin.y + box.size.height; y += 2.0F) {
            for (float x = box.origin.x + 1.0F; x < box.origin.x + box.size.width; x += 2.0F) {
                if (hit(x, y) == target) {
                    found = Point{x, y};
                }
            }
        }
        return found;
    }

    /// @brief 真点控件的右缘：`TextInput` 的可打印插入口在右端，避免点在已有字形上。
    ///
    /// 返回值以「落点确实把焦点交给该控件」为凭：`type()` 的基类缺省即便无人接收也返
    /// true，不断焦点就会把「字没进输入框」测成绿（本驱动台首轮正是这么把计数读成 "1"）。
    [[nodiscard]] auto right_edge_click(au::Widget *widget) -> bool {
        const std::optional<Point> spot = right_spot_of(widget);
        if (!spot.has_value()) {
            return false;
        }
        click(spot->x, spot->y);
        return focused() == widget;
    }

    /// @brief 在网格上按下拖到另一端（Press/Move/Release 实发，多击判定由本驱动台的派发器自记）。
    auto drag_select(std::size_t row, std::size_t from_column, std::size_t to_column) -> void {
        auto at = [&](std::size_t column) -> Point {
            const borealis::ui::Rect band = borealis::ui::rect_for(geometry_, row, column, column + 1U);
            return Point{static_cast<float>(origin_x_ + band.x + band.width * 0.5),
                         static_cast<float>(origin_y_ + band.y + band.height * 0.5)};
        };
        MouseEvent down;
        down.position = at(from_column);
        down.button = MouseButton::Left;
        down.action = MouseAction::Press;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), down, &focus_);
        MouseEvent move;
        move.position = at(to_column);
        move.action = MouseAction::Move;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), move, &focus_);
        MouseEvent up;
        up.position = at(to_column);
        up.button = MouseButton::Left;
        up.action = MouseAction::Release;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), up, &focus_);
        render();
    }

    [[nodiscard]] auto pixels() -> std::vector<std::uint8_t> {
        std::vector<std::uint8_t> out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    /// @brief 两帧逐像素差集的大小（RGBA 四位全比）。
    [[nodiscard]] auto count_diff(const std::vector<std::uint8_t> &a,
                                  const std::vector<std::uint8_t> &b) const -> std::size_t {
        const std::size_t n = std::min(a.size(), b.size());
        std::size_t count = 0U;
        for (std::size_t i = 0U; i + 3U < n; i += 4U) {
            if (a[i] != b[i] || a[i + 1U] != b[i + 1U] || a[i + 2U] != b[i + 2U] ||
                a[i + 3U] != b[i + 3U]) {
                ++count;
            }
        }
        return count;
    }

    /// @brief 只统计落进 `region` 的差异像素（判「浮层上屏的影响面被钳在条体自带阴影之内」）。
    [[nodiscard]] auto count_region_diff(const std::vector<std::uint8_t> &a,
                                         const std::vector<std::uint8_t> &b,
                                         const Rect &region) const -> std::size_t {
        const std::size_t width = buffer_width();
        const std::size_t height = buffer_height();
        const std::size_t x0 = static_cast<std::size_t>(std::max(0.0F, region.origin.x));
        const std::size_t y0 = static_cast<std::size_t>(std::max(0.0F, region.origin.y));
        const std::size_t x1 = std::min(width, static_cast<std::size_t>(
                                                 std::max(0.0F, region.right())));
        const std::size_t y1 = std::min(height, static_cast<std::size_t>(
                                                    std::max(0.0F, region.bottom())));
        const std::size_t n = std::min(a.size(), b.size());
        std::size_t count = 0U;
        for (std::size_t y = y0; y < y1; ++y) {
            for (std::size_t x = x0; x < x1; ++x) {
                const std::size_t i = (y * width + x) * 4U;
                if (i + 3U >= n) {
                    continue;
                }
                if (a[i] != b[i] || a[i + 1U] != b[i + 1U] || a[i + 2U] != b[i + 2U] ||
                    a[i + 3U] != b[i + 3U]) {
                    ++count;
                }
            }
        }
        return count;
    }

    [[nodiscard]] static auto band_around(const Rect &box, float margin) -> Rect {
        return Rect{.origin = Point{box.origin.x - margin, box.origin.y - margin},
                    .size = au::Size{box.size.width + 2.0F * margin,
                                     box.size.height + 2.0F * margin}};
    }

    [[nodiscard]] auto preflight() const -> bool {
        return scale_ == 1.0F && geometry_.rows > 8U && geometry_.columns > 6U;
    }

private:
    [[nodiscard]] auto buffer_width() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(window_width_) * scale_);
    }
    [[nodiscard]] auto buffer_height() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(window_height_) * scale_);
    }

    auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(static_cast<float>(window_width_),
                                   static_cast<float>(window_height_));
        return au::Window{std::move(surface)};
    }

    /// @brief 造替身连接：所有权交给会话，裸指针留在本类驱动投递与观测。
    [[nodiscard]] auto own_connection() -> std::unique_ptr<Connection> {
        auto owned = std::make_unique<FakeConnection>();
        connection_ = owned.get();
        return owned;
    }

    /// @brief 帧 0 之后重取格网几何（左键落点要按格子换算，`ui::rect_for` 与控件同一坐标空间）。
    auto prime_geometry() -> void {
        const aurora::Rect box = view_->paint_bounds();
        scale_ = window_.surface().scale_factor();
        origin_x_ = static_cast<double>(box.origin.x);
        origin_y_ = static_cast<double>(box.origin.y);
        const auto metrics = au::render::FontEngine::monospace_cell(test_font(), scale_);
        geometry_ = borealis::ui::make_geometry(
            CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px}, scale_,
            LogicalSize{static_cast<double>(box.size.width), static_cast<double>(box.size.height)}, kPaddingDp);
    }

    int window_width_ = 520;
    int window_height_ = 260;
    au::Window window_ = make_window();
    std::shared_ptr<borealis::term::UnicodeWidthPolicy> width_policy =
        std::make_shared<borealis::term::UnicodeWidthPolicy>();
    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    std::shared_ptr<OverlayHost> host_;
    Node root_;
    FocusManager focus_;
    ShortcutRegistry shortcuts_;
    au::EventDispatcher pointer_dispatcher_;
    GridGeometry geometry_{};
    float scale_ = 1.0F;
    double origin_x_ = 0.0;
    double origin_y_ = 0.0;
};

}  // namespace

AURORA_TEST_CASE(opening_the_bar_focuses_the_input_and_sends_no_bytes) {
    Harness harness;
    AURORA_TEST_REQUIRE(harness.overlay() == nullptr);
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.overlay()->is_open());
    AURORA_TEST_REQUIRE(harness.focused() == harness.overlay()->input());
    AURORA_TEST_CHECK_EQ(harness.written(), std::string{});
    AURORA_TEST_CHECK_EQ(harness.overlay_count(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(harness.overlay()->tier(), SearchTier::Wide);
    // A1-a：条体落位与宽度都是 `tier_for` 钳位式的解，不是观测量——520 dp 窗 → 内容盒 512 →
    // `clamp(512 − 16, 496, 560) = 496`，锚点取右上角往内 8 dp 即 {12, 12}。
    AURORA_TEST_REQUIRE(harness.overlay()->popup() != nullptr);
    const Rect box = harness.overlay()->popup()->content_bounds();
    AURORA_TEST_CHECK_NEAR(box.origin.x, 12.0F, 0.5F);
    AURORA_TEST_CHECK_NEAR(box.origin.y, 12.0F, 0.5F);
    AURORA_TEST_CHECK_NEAR(box.size.width, 496.0F, 0.5F);
    AURORA_TEST_CHECK_NEAR(box.size.height, 44.0F, 0.5F);
    AURORA_TEST_CHECK_NEAR(harness.overlay()->bar_width(), 496.0F, 0.5F);
}

AURORA_TEST_CASE(typing_several_characters_scans_once_in_one_frame) {
    Harness harness;
    harness.feed("\x1b[2J\x1b[Hone: qz7k\r\ntwo: other");
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.type("qz7k"));
    AURORA_TEST_REQUIRE_STREQ(harness.overlay()->input()->value(), "qz7k");
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{0});
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(harness.matches()->count(), std::size_t{1});
    // 打字只进浮层与匹配表：一个字都不许发往会话，而计数那一格当场换成刚扫出的那份结果。
    AURORA_TEST_CHECK_EQ(harness.written(), std::string{});
    AURORA_TEST_CHECK_EQ(harness.count_text(), std::string{"1"});
}

AURORA_TEST_CASE(the_regex_tier_scans_only_at_submit) {
    Harness harness;
    harness.feed("\x1b[2J\x1b[Hqz7kx tail\r\nother line");
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.type("qz7k"));
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{1});
    AURORA_TEST_REQUIRE_EQ(harness.matches()->count(), std::size_t{1});

    const std::optional<Point> chip = harness.spot_of(harness.overlay()->regex_toggle());
    AURORA_TEST_REQUIRE(chip.has_value());
    harness.click(chip->x, chip->y);
    harness.render();
    AURORA_TEST_REQUIRE(harness.query().regex);
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(harness.matches()->count(), std::size_t{1});

    AURORA_TEST_REQUIRE(harness.right_edge_click(harness.overlay()->input()));
    AURORA_TEST_REQUIRE_STREQ(harness.overlay()->input()->value(), "qz7k");
    AURORA_TEST_REQUIRE(harness.type("x"));
    harness.render();
    AURORA_TEST_CHECK_EQ(harness.scans(), std::size_t{2});
    AURORA_TEST_REQUIRE_STREQ(harness.count_text(),
                              borealis::ui::settings_label("search.pending_enter"));

    AURORA_TEST_REQUIRE(harness.press(KeyCode::Enter));
    harness.render();
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{3});
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(harness.matches()->count(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(harness.count_text(), std::string{"1"});
}

AURORA_TEST_CASE(escape_closes_unbinds_and_returns_the_key_to_the_session) {
    Harness harness;
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE_EQ(harness.shortcuts().count(), std::size_t{1});
    const auto bindings = harness.shortcuts().bindings();
    AURORA_TEST_REQUIRE_EQ(bindings.size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(bindings.front().combo.to_string(), std::string{"Escape"});
    AURORA_TEST_CHECK_EQ(bindings.front().scope, ShortcutScope::Global);

    AURORA_TEST_REQUIRE(harness.escape_via_shortcut());
    harness.render();
    AURORA_TEST_CHECK_EQ(harness.shortcuts().count(), std::size_t{0});
    AURORA_TEST_CHECK_FALSE(harness.overlay()->is_open());
    AURORA_TEST_CHECK_FALSE(harness.escape_via_shortcut());

    // 重开即重新登记（F2-a 的两侧都走一遍）：登记或解绑任缺其一，这里的计数都会错。
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay()->is_open());
    AURORA_TEST_REQUIRE_EQ(harness.shortcuts().count(), std::size_t{1});
    AURORA_TEST_REQUIRE(harness.escape_via_shortcut());
    harness.render();
    AURORA_TEST_CHECK_EQ(harness.shortcuts().count(), std::size_t{0});
    AURORA_TEST_CHECK_FALSE(harness.overlay()->is_open());

    // 解绑之后 Esc 原样回到会话：焦点经 pop_scope 交还视口，走键盘编码即一个 ESC 字节。
    harness.clear_written();
    (void)harness.press(KeyCode::Escape);
    AURORA_TEST_CHECK_EQ(harness.written(), std::string{"\x1b"});
}

AURORA_TEST_CASE(close_button_closes_the_bar_and_keeps_the_query) {
    Harness harness;
    std::string scene = "\x1b[2J\x1b[H";
    for (std::size_t i = 0; i < 120U; ++i) {
        if (i == 5U) {
            scene += "hit-A needle\r\n";  // 针远在 24 行窗口之上：一次跳转必须真滚动
        } else {
            scene += "fill " + std::to_string(i) + "\r\n";
        }
    }
    harness.feed(scene);
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.type("hit-A"));
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_REQUIRE_EQ(harness.matches()->count(), std::size_t{1});
    AURORA_TEST_REQUIRE(harness.overlay()->next_button()->enabled);

    AURORA_TEST_REQUIRE_EQ(harness.scrollback(), std::size_t{0});
    const std::optional<Point> next = harness.spot_of(harness.overlay()->next_button());
    AURORA_TEST_REQUIRE(next.has_value());
    harness.click(next->x, next->y);
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches()->cursor().has_value());
    AURORA_TEST_REQUIRE_GT(harness.scrollback(), 0U);
    AURORA_TEST_REQUIRE_STREQ(harness.count_text(), "1/1");

    // D7：关闭只清高亮，查询文本与回看位置都要留着。
    const std::size_t scrolled = harness.scrollback();
    const std::optional<Point> close = harness.spot_of(harness.overlay()->close_button());
    AURORA_TEST_REQUIRE(close.has_value());
    harness.click(close->x, close->y);
    harness.render();
    AURORA_TEST_CHECK_FALSE(harness.overlay()->is_open());
    AURORA_TEST_CHECK_EQ(harness.shortcuts().count(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(harness.scrollback(), scrolled);
    AURORA_TEST_CHECK(harness.query().text == U"hit-A");
    AURORA_TEST_CHECK(harness.matches() == nullptr);
}

AURORA_TEST_CASE(a_click_outside_the_bar_closes_it_and_leaves_the_selection_alone) {
    Harness harness;
    harness.feed("\x1b[?25l\x1b[3;1HABCD");
    harness.drag_select(2U, 0U, 3U);
    AURORA_TEST_REQUIRE_STREQ(harness.selected_text(), "ABCD");
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.hit(260.0F, 200.0F) == harness.view());
    harness.click(260.0F, 200.0F);
    harness.render();
    AURORA_TEST_CHECK_FALSE(harness.overlay()->is_open());
    AURORA_TEST_CHECK_STREQ(harness.selected_text(), "ABCD");
}

AURORA_TEST_CASE(enter_on_an_empty_query_neither_closes_nor_scans) {
    Harness harness;
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    (void)harness.press(KeyCode::Enter);
    harness.render();
    AURORA_TEST_CHECK(harness.overlay()->is_open());
    AURORA_TEST_CHECK_EQ(harness.scans(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(harness.written(), std::string{});
}

AURORA_TEST_CASE(a_truncated_table_shows_the_cap_with_a_plus) {
    Harness harness(520, 260, 500);
    std::string scene = "\x1b[2J\x1b[H";
    for (std::size_t i = 0; i < 400U; ++i) {
        scene += std::string(30U, 'a');
        scene += "\r\n";
    }
    harness.feed(scene);
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.type("a"));
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_REQUIRE(harness.matches()->truncated());
    AURORA_TEST_CHECK_GE(harness.matches()->count(), borealis::ui::kMaxSearchMatches);
    AURORA_TEST_CHECK_EQ(harness.count_text(),
                         std::to_string(borealis::ui::kMaxSearchMatches) + "+");
}

AURORA_TEST_CASE(an_invalid_pattern_keeps_the_previous_table) {
    Harness harness;
    harness.feed("\x1b[2J\x1b[Hqz7k tail\r\nother line");
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.type("qz7k"));
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{1});
    AURORA_TEST_REQUIRE_EQ(harness.matches()->count(), std::size_t{1});

    const std::optional<Point> chip = harness.spot_of(harness.overlay()->regex_toggle());
    AURORA_TEST_REQUIRE(chip.has_value());
    harness.click(chip->x, chip->y);
    harness.render();
    AURORA_TEST_REQUIRE(harness.query().regex);
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{2});
    AURORA_TEST_REQUIRE(harness.right_edge_click(harness.overlay()->input()));
    AURORA_TEST_REQUIRE(harness.type("["));
    harness.render();
    AURORA_TEST_REQUIRE_STREQ(harness.count_text(),
                              borealis::ui::settings_label("search.pending_enter"));
    AURORA_TEST_REQUIRE(harness.press(KeyCode::Enter));
    harness.render();
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{3});
    AURORA_TEST_REQUIRE(harness.pattern_invalid());
    AURORA_TEST_CHECK_FALSE(harness.pending_submit());
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(harness.matches()->count(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(harness.count_text(),
                         borealis::ui::settings_label("search.count_invalid"));
}

AURORA_TEST_CASE(the_bar_degrades_through_three_tiers) {
    {
        Harness harness(520, 260, 120);
        harness.view()->open_search();
        harness.render();
        AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
        AURORA_TEST_CHECK_EQ(harness.overlay()->tier(), SearchTier::Wide);
        AURORA_TEST_REQUIRE(harness.overlay()->popup() != nullptr);
        const Rect box = harness.overlay()->popup()->content_bounds();
        AURORA_TEST_CHECK_NEAR(box.origin.x, 12.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.origin.y, 12.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.size.width, 496.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.size.height, 44.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(harness.overlay()->bar_width(), 496.0F, 0.5F);
        AURORA_TEST_REQUIRE(harness.overlay()->prev_button() != nullptr);
        AURORA_TEST_REQUIRE(harness.overlay()->next_button() != nullptr);
        // 朗读名一律读**显式声明**那一口：`Button` 覆写 `accessibility_label()` 为可见标签
        // （宽档可见串恰与词条同字，读它就成了同义反复；紧凑档读到的则是 "↑"）。
        AURORA_TEST_CHECK_STREQ(harness.overlay()->prev_button()->explicit_accessibility_label(),
                                borealis::ui::settings_label("search.prev"));
        AURORA_TEST_CHECK_STREQ(harness.overlay()->next_button()->explicit_accessibility_label(),
                                borealis::ui::settings_label("search.next"));
    }
    {
        Harness harness(480, 260, 120);
        harness.view()->open_search();
        harness.render();
        AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
        AURORA_TEST_CHECK_EQ(harness.overlay()->tier(), SearchTier::Compact);
        AURORA_TEST_REQUIRE(harness.overlay()->popup() != nullptr);
        const Rect box = harness.overlay()->popup()->content_bounds();
        AURORA_TEST_CHECK_NEAR(box.origin.x, 12.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.origin.y, 12.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.size.width, 456.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.size.height, 44.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(harness.overlay()->bar_width(), 456.0F, 0.5F);
        AURORA_TEST_REQUIRE(harness.overlay()->prev_button() != nullptr);
        AURORA_TEST_REQUIRE(harness.overlay()->next_button() != nullptr);
        // CJK-LITERAL: locale-output - 紧凑档的两枚跳转是符号档，逐字断言是本条判据的全部内容。
        AURORA_TEST_CHECK_STREQ(harness.overlay()->prev_button()->label.get().text, "↑");
        AURORA_TEST_CHECK_STREQ(harness.overlay()->next_button()->label.get().text, "↓");
        AURORA_TEST_CHECK_STREQ(harness.overlay()->prev_button()->explicit_accessibility_label(),
                                borealis::ui::settings_label("search.prev"));
        AURORA_TEST_CHECK_STREQ(harness.overlay()->next_button()->explicit_accessibility_label(),
                                borealis::ui::settings_label("search.next"));
    }
    {
        Harness harness(400, 260, 120);
        harness.view()->open_search();
        harness.render();
        AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
        AURORA_TEST_CHECK_EQ(harness.overlay()->tier(), SearchTier::TwoRow);
        AURORA_TEST_REQUIRE(harness.overlay()->popup() != nullptr);
        const Rect box = harness.overlay()->popup()->content_bounds();
        AURORA_TEST_CHECK_NEAR(box.origin.x, 12.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.origin.y, 12.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.size.width, 376.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(box.size.height, 66.0F, 0.5F);
        AURORA_TEST_CHECK_NEAR(harness.overlay()->bar_width(), 376.0F, 0.5F);
        AURORA_TEST_CHECK(harness.overlay()->prev_button() == nullptr);
        AURORA_TEST_CHECK(harness.overlay()->next_button() == nullptr);
        AURORA_TEST_REQUIRE(harness.overlay()->input() != nullptr);
        AURORA_TEST_REQUIRE(harness.overlay()->case_toggle() != nullptr);
        AURORA_TEST_REQUIRE(harness.overlay()->close_button() != nullptr);
    }
}

AURORA_TEST_CASE(an_empty_query_shows_the_dash_and_disables_the_jumps) {
    Harness harness;
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    // CJK-LITERAL: locale-output - B0 的占位档是一枚全角破折号，词条表里没有它的位置。
    AURORA_TEST_REQUIRE_STREQ(harness.count_text(), "—");
    AURORA_TEST_REQUIRE(harness.overlay()->next_button() != nullptr);
    AURORA_TEST_CHECK_FALSE(harness.overlay()->next_button()->enabled);
    AURORA_TEST_CHECK_FALSE(harness.overlay()->prev_button()->enabled);

    AURORA_TEST_REQUIRE(harness.type("zz9"));
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_REQUIRE_EQ(harness.matches()->count(), std::size_t{0});
    AURORA_TEST_CHECK_STREQ(harness.count_text(),
                            borealis::ui::settings_label("search.count_none"));
    AURORA_TEST_CHECK_FALSE(harness.overlay()->next_button()->enabled);
}

AURORA_TEST_CASE(the_chips_show_symbols_and_read_as_chinese) {
    Harness harness;
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.overlay()->case_toggle() != nullptr);
    AURORA_TEST_REQUIRE(harness.overlay()->regex_toggle() != nullptr);
    AURORA_TEST_CHECK_STREQ(harness.overlay()->case_toggle()->label.get().text, "Aa");
    AURORA_TEST_CHECK_STREQ(harness.overlay()->regex_toggle()->label.get().text, ".*");
    // 朗读名读**显式声明**那一口：`Button::accessibility_label()` 覆写为可见标签（即 "Aa" /
    // ".*"），拿它比中文词条测的是可见串而不是朗读名。
    AURORA_TEST_CHECK_STREQ(harness.overlay()->case_toggle()->explicit_accessibility_label(),
                            borealis::ui::settings_label("search.toggle_case"));
    AURORA_TEST_CHECK_STREQ(harness.overlay()->regex_toggle()->explicit_accessibility_label(),
                            borealis::ui::settings_label("search.toggle_regex"));

    AURORA_TEST_REQUIRE_FALSE(harness.query().case_sensitive);
    const std::optional<Point> chip = harness.spot_of(harness.overlay()->case_toggle());
    AURORA_TEST_REQUIRE(chip.has_value());
    harness.click(chip->x, chip->y);
    harness.render();
    AURORA_TEST_CHECK(harness.query().case_sensitive);
}

AURORA_TEST_CASE(reopening_keeps_the_text_and_hints_at_enter) {
    Harness harness;
    harness.feed("\x1b[2J\x1b[Hqz7kx tail\r\n");
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    AURORA_TEST_REQUIRE(harness.type("qz7k"));
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{1});

    AURORA_TEST_REQUIRE(harness.escape_via_shortcut());
    harness.render();
    AURORA_TEST_REQUIRE(harness.matches() == nullptr);

    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay()->is_open());
    AURORA_TEST_CHECK_STREQ(harness.overlay()->input()->value(), "qz7k");
    AURORA_TEST_CHECK_STREQ(harness.count_text(),
                            borealis::ui::settings_label("search.reopen_hint"));
    AURORA_TEST_CHECK_EQ(harness.scans(), std::size_t{1});
    AURORA_TEST_REQUIRE(harness.matches() == nullptr);

    AURORA_TEST_REQUIRE(harness.press(KeyCode::Enter));
    harness.render();
    AURORA_TEST_REQUIRE_EQ(harness.scans(), std::size_t{2});
    AURORA_TEST_REQUIRE(harness.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(harness.matches()->count(), std::size_t{1});
}

AURORA_TEST_CASE(a_click_inside_the_bar_does_not_move_the_selection) {
    Harness harness;
    harness.feed("\x1b[?25l\x1b[3;1HABCD");
    harness.drag_select(2U, 0U, 3U);
    AURORA_TEST_REQUIRE_STREQ(harness.selected_text(), "ABCD");
    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    const std::optional<Point> spot = harness.spot_of(harness.overlay()->input());
    AURORA_TEST_REQUIRE(spot.has_value());
    harness.click(spot->x, spot->y);
    harness.render();
    AURORA_TEST_CHECK(harness.overlay()->is_open());
    AURORA_TEST_CHECK_STREQ(harness.selected_text(), "ABCD");
}

AURORA_TEST_CASE(opening_and_closing_paints_and_clears_the_bar) {
    Harness harness;
    AURORA_TEST_REQUIRE(harness.preflight());
    harness.feed("\x1b[?25l\x1b[2J\x1b[Hhello");
    const std::vector<std::uint8_t> reference = harness.pixels();

    harness.view()->open_search();
    harness.render();
    AURORA_TEST_REQUIRE(harness.overlay() != nullptr);
    const std::vector<std::uint8_t> opened = harness.pixels();
    const Rect box = harness.overlay()->popup()->content_bounds();
    const std::size_t total = harness.count_diff(reference, opened);
    AURORA_TEST_REQUIRE(total > 0U);
    AURORA_TEST_CHECK_EQ(harness.count_region_diff(reference, opened,
                                                   Harness::band_around(box, 20.0F)),
                         total);
    AURORA_TEST_REQUIRE(harness.hit(60.0F, 34.0F) != nullptr);
    AURORA_TEST_CHECK(harness.hit(60.0F, 34.0F) != harness.view());
    AURORA_TEST_REQUIRE(harness.hit(260.0F, 200.0F) == harness.view());

    AURORA_TEST_REQUIRE(harness.escape_via_shortcut());
    harness.render();
    AURORA_TEST_REQUIRE_FALSE(harness.overlay()->is_open());
    AURORA_TEST_CHECK_EQ(harness.count_diff(reference, harness.pixels()), std::size_t{0});
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_search_overlay
