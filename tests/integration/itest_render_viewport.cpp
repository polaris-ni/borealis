/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（绘制主路径）
/// 测试说明: 用无头帧缓冲对终端视口做**帧间像素差分**，验收 `SPEC.FEAT.RENDER.01` / `03` / `04`
///           的绘制契约：行列数由控件自身可视尺寸派生并下发（`SPEC.FEAT.XFER.01` 的 UI 取值腿）、
///           单帧像素差集只落在本帧提交行与光标行（含「上一帧光标行必须被重绘干净」）、SGR 色带
///           与装饰线的落笔矩形、斜体经批量入口的整批 opts 送出（不得静默退化成正体）、光标三形态
///           与失焦降级、滚轮回看的整窗换源与回到底的逐位复现、
///           回看态的「距底恒定」（裁决 D6①）、备屏不可滚、双宽与零宽的列位对齐。
///           像素一律比 RGBA 四通道含 alpha：Headless 帧底色是全透明黑 (0,0,0,0)，只比 RGB 会让
///           「画了个纯黑」与「什么都没画」混为一谈。
///
///           「裁剪盒外不画」**不**在此断言：脏区上报走非虚的 `dirty_bounds()`（恒等于控件自身
///           盒），控件无法把子矩形报成脏，故像素差分观察不到那条纪律（设计稿 §9 已按实测改口径）。

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

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/render/font_engine.h"
#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①：类声明刻意不进 `include/borealis/`，含它即含框架头），故按相对路径取用，
// 而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_render_viewport {

namespace {

#ifdef AURORA_BACKEND_HEADLESS

using borealis::grid::Row;
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
using borealis::ui::RgbaColor;
using borealis::ui::TerminalView;

constexpr int kWindowWidth = 420;
constexpr int kWindowHeight = 260;
constexpr float kPaddingDp = 4.0F;
constexpr std::size_t kScrollback = 40;
constexpr int kBlinkPeriodMs = 500;
constexpr Size kNominalSize{80U, 24U};

UnicodeWidthPolicy width_policy;

/// @brief 传输连接替身：记录会话下发的尺寸，并可主动投递字节（测试里投递线程即读线程）。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override {
        written.insert(written.end(), bytes.begin(), bytes.end());
    }

    auto resize(Size size) -> void override { resizes.push_back(size); }

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

    std::vector<std::byte> written;
    std::vector<Size> resizes;  ///< 会话下发的行列尺寸，按序（尺寸来源腿的唯一观测点）。

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

/// @brief 手工搭一份调色板：16 基本色两两互异，且都不同于默认前景/底色与光标色。
///
/// 颜色可唯一指认是像素断言的前提——若两档同色，「这块画的是哪一档」就无法从帧缓冲反推。
/// 对比度强制与 bold-is-bright 一律关掉：它们会把合成色挪离表色，而本文件要比的是逐位相等。
[[nodiscard]] auto test_palette() -> PaletteSpec {
    auto spec = PaletteSpec{};
    for (std::size_t index = 0; index < spec.basic.size(); ++index) {
        spec.basic[index] = RgbaColor{static_cast<std::uint8_t>(20U + index * 13U),
                                      static_cast<std::uint8_t>(200U - index * 11U),
                                      static_cast<std::uint8_t>(60U + index * 7U), 255U};
    }
    spec.default_foreground = RgbaColor{220U, 221U, 222U, 255U};
    spec.default_background = RgbaColor{17U, 18U, 19U, 255U};
    spec.cursor_color = RgbaColor{250U, 200U, 20U, 255U};
    return spec;
}

[[nodiscard]] auto test_font() -> au::Font {
    return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
}

/// @brief 一帧帧缓冲的 RGBA 副本。
using Pixels = std::vector<std::uint8_t>;

/// @brief 驱动台：会话 + 替身连接 + 视口控件 + 无头窗口 + 根节点 + 焦点管理器。
///
/// 成员声明次序即析构次序的倒序：焦点管理器与根节点须在会话之前消散——控件持会话裸引用。
class Harness {
  public:
    explicit Harness(const PaletteSpec &palette = test_palette())
        : palette_(palette),
          font_(test_font()),
          session_(std::make_unique<Session>(own_connection(), kNominalSize, kScrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, palette_, font_, kPaddingDp,
                                               std::chrono::milliseconds{kBlinkPeriodMs})),
          root_(std::static_pointer_cast<au::Widget>(view_)) {
        session_->start();
        render();  // 帧 0：布局发生在这里，控件据此把行列尺寸下发给会话
        const au::Rect box = view_->paint_bounds();
        scale_ = window_.surface().scale_factor();
        origin_x_ = static_cast<double>(box.origin.x);
        origin_y_ = static_cast<double>(box.origin.y);
        const auto metrics = au::render::FontEngine::monospace_cell(font_, scale_);
        geometry_ = borealis::ui::make_geometry(
            CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px}, scale_,
            LogicalSize{static_cast<double>(box.size.width), static_cast<double>(box.size.height)}, kPaddingDp);
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投递一段字节后排帧到队列见底（单帧预算 8 条，突发输出要几帧才吃完）。
    auto feed(std::string_view bytes) -> void {
        connection_->deliver(bytes);
        render();
    }

    /// @brief 只排帧不投字节（光标形态、焦点与滚轮都要靠它把画面推到帧缓冲）。
    auto render() -> void {
        do {
            view_->on_frame();
            (void)window_.present_root(root_);
        } while (session_->has_damage());
    }

    /// @brief 取当前帧缓冲（RGBA8，物理像素）。
    [[nodiscard]] auto pixels() const -> Pixels {
        Pixels out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    /// @brief 某格中心的像素色（行列是**屏幕**坐标，0 基）。
    [[nodiscard]] auto cell_center(const Pixels &px, std::size_t row, std::size_t column) const -> RgbaColor {
        return cell_probe(px, row, column, 0.5);
    }

    /// @brief 某格水平居中、垂直取 @p fy 分数处的像素色。
    ///
    /// 格腰正是字形所在，只有近顶/近底处能纯读到底色：色带用 `rect_for` 的整格高矩形（② 层），
    /// 故同一行内任一分数都还在带内。拿格心比底色会把抗锯齿的字形像素误判成「底色不对」——
    /// `B` 的中横正压在格心上。
    [[nodiscard]] auto cell_probe(const Pixels &px, std::size_t row, std::size_t column, double fy) const
        -> RgbaColor {
        return sample(px, borealis::ui::rect_for(geometry_, row, column, column + 1U), 0.5, fy);
    }

    /// @brief 取一个「相对绘制盒」矩形内指定分数处的像素色。
    [[nodiscard]] auto sample(const Pixels &px, const Rect &local, double fx, double fy) const -> RgbaColor {
        const double x = origin_x_ + local.x + (local.width * fx);
        const double y = origin_y_ + local.y + (local.height * fy);
        const std::size_t index =
            ((static_cast<std::size_t>(y) * buffer_width()) + static_cast<std::size_t>(x)) * 4U;
        return RgbaColor{px[index], px[index + 1U], px[index + 2U], px[index + 3U]};
    }

    /// @brief 焦点开关：终端以光标形态表达焦点态，故持焦与否须经真实焦点管理器驱动。
    auto set_focused(bool focused) -> void {
        if (focused) {
            focus_.set_root(&root_.widget());
            focus_.set_focus(view_.get());
        } else {
            focus_.clear();
        }
    }

    /// @brief 滚轮回看：@p rows 为正向上看更早的行，负向反之。
    auto scroll(int rows) -> void {
        au::ScrollEvent event;
        const auto center_x = static_cast<float>(origin_x_ + (geometry_.cell_width *
                                                              static_cast<double>(geometry_.columns) / 2.0));
        const auto center_y = static_cast<float>(origin_y_ + (geometry_.cell_height *
                                                              static_cast<double>(geometry_.rows) / 2.0));
        event.position = au::Point{.x = center_x, .y = center_y};
        event.delta_y = static_cast<float>(rows);
        (void)au::EventDispatcher::dispatch(root_.widget(), event);
    }

    /// @brief 权威网格某视口行的整行副本（绘制取的是主线程副本，本用例里两者内容恒等）。
    [[nodiscard]] auto visible_line(std::size_t row) -> Row {
        Row copy{1U};
        session_->read(
            [this, &copy, row](Storage &grid, const Cursor &, const TermModes &) -> void {
                if (row < grid.visible_rows()) {
                    copy = grid.visible_line(row);
                }
            });
        return copy;
    }

    [[nodiscard]] auto visible_rows() -> std::size_t {
        std::size_t rows = 0;
        session_->read(
            [&rows](Storage &grid, const Cursor &, const TermModes &) -> void { rows = grid.visible_rows(); });
        return rows;
    }

    [[nodiscard]] auto geometry() const noexcept -> const GridGeometry & { return geometry_; }
    [[nodiscard]] auto palette() const noexcept -> const PaletteSpec & { return palette_; }
    [[nodiscard]] auto scale() const noexcept -> float { return scale_; }

    [[nodiscard]] auto last_resize() const -> std::optional<Size> {
        if (connection_->resizes.empty()) {
            return std::nullopt;
        }
        return connection_->resizes.back();
    }

    [[nodiscard]] auto resize_count() const -> std::size_t { return connection_->resizes.size(); }

    /// @brief 前置条件：Headless 缩放为 1（dp 即像素），且网格容得下几行几列。
    ///
    /// 格宽须为整数：`cell_band` 靠「相邻两格的带子差一个整格宽」来保证亚像素相位一致，
    /// 非整数格宽会让两格落在不同的取样相位上，两格对照就不只剩斜切这一个变量了。
    [[nodiscard]] auto preflight() const noexcept -> bool {
        const auto integral_cell_width =
            geometry_.cell_width == static_cast<double>(static_cast<std::size_t>(geometry_.cell_width));
        return scale_ == 1.0F && geometry_.rows > 3U && geometry_.columns > 6U &&
               geometry_.cell_width >= 2.0 && geometry_.cell_height >= 3.0 && integral_cell_width;
    }

    /// @brief 一行的整数像素带（行内容比较用），右缘剔掉回看指示条所占的边带。
    [[nodiscard]] auto row_band(const Pixels &px, std::size_t row) const -> Pixels {
        Pixels out;
        const std::size_t top =
            static_cast<std::size_t>(origin_y_ + geometry_.padding + static_cast<double>(row) * geometry_.cell_height);
        const std::size_t right = buffer_width() > 10U ? buffer_width() - 10U : buffer_width();
        for (std::size_t y = top; y < top + static_cast<std::size_t>(geometry_.cell_height) && y < buffer_height();
             ++y) {
            for (std::size_t x = 0; x < right; ++x) {
                const std::size_t index = (y * buffer_width() + x) * 4U;
                out.insert(out.end(), px.begin() + static_cast<Pixels::difference_type>(index),
                           px.begin() + static_cast<Pixels::difference_type>(index + 4U));
            }
        }
        return out;
    }

    /// @brief 单个格的整数像素带（同底色下两格做对照用）。左上角与宽高都按格网取整，
    ///        使相邻两格的带子处于同一亚像素相位——否则「格位置不同」本身就成了差异来源。
    [[nodiscard]] auto cell_band(const Pixels &px, std::size_t row, std::size_t column) const -> Pixels {
        Pixels out;
        const std::size_t top = static_cast<std::size_t>(
            origin_y_ + geometry_.padding + static_cast<double>(row) * geometry_.cell_height);
        const std::size_t left = static_cast<std::size_t>(
            origin_x_ + geometry_.padding + static_cast<double>(column) * geometry_.cell_width);
        const std::size_t width = static_cast<std::size_t>(geometry_.cell_width);
        const std::size_t height = static_cast<std::size_t>(geometry_.cell_height);
        for (std::size_t y = top; y < top + height && y < buffer_height(); ++y) {
            for (std::size_t x = left; x < left + width && x < buffer_width(); ++x) {
                const std::size_t index = (y * buffer_width() + x) * 4U;
                out.insert(out.end(), px.begin() + static_cast<Pixels::difference_type>(index),
                           px.begin() + static_cast<Pixels::difference_type>(index + 4U));
            }
        }
        return out;
    }

    /// @brief 两帧差分落到了哪些绘制行；不在任何行带内的差异只记一次（`outside_grid()`）。
    [[nodiscard]] auto changed_rows(const Pixels &before, const Pixels &after) const -> std::vector<std::size_t> {
        std::vector<std::size_t> rows;
        const auto push = [&rows](std::size_t row) -> void {
            if (std::find(rows.begin(), rows.end(), row) == rows.end()) {
                rows.push_back(row);
            }
        };
        for (std::size_t y = 0; y < buffer_height(); ++y) {
            for (std::size_t x = 0; x < buffer_width(); ++x) {
                const std::size_t index = (y * buffer_width() + x) * 4U;
                if (before[index] == after[index] && before[index + 1U] == after[index + 1U] &&
                    before[index + 2U] == after[index + 2U] && before[index + 3U] == after[index + 3U]) {
                    continue;
                }
                const double offset = static_cast<double>(y) - origin_y_ - geometry_.padding;
                if (offset < 0.0 || offset >= static_cast<double>(geometry_.rows) * geometry_.cell_height) {
                    push(outside_grid());
                    continue;
                }
                push(static_cast<std::size_t>(offset / geometry_.cell_height));
            }
        }
        return rows;
    }

    [[nodiscard]] static auto outside_grid() noexcept -> std::size_t { return static_cast<std::size_t>(-1); }

    /// @brief 两帧不同的像素数（RGBA 任一通道不同即算一个）。
    [[nodiscard]] static auto count_diff(const Pixels &a, const Pixels &b) -> std::size_t {
        std::size_t d = 0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); i += 4U) {
            if (a[i] != b[i] || a[i + 1U] != b[i + 1U] || a[i + 2U] != b[i + 2U] || a[i + 3U] != b[i + 3U]) {
                ++d;
            }
        }
        return d;
    }

  private:
    /// @brief 造替身连接：所有权交给会话，裸指针留在本类驱动投递。
    [[nodiscard]] auto own_connection() -> std::unique_ptr<Connection> {
        auto owned = std::make_unique<FakeConnection>();
        connection_ = owned.get();
        return owned;
    }

    [[nodiscard]] auto buffer_width() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowWidth) * scale_);
    }

    [[nodiscard]] auto buffer_height() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowHeight) * scale_);
    }

    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    PaletteSpec palette_{};
    au::Font font_{};
    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    au::Node root_;
    au::FocusManager focus_;
    GridGeometry geometry_{};
    float scale_ = 1.0F;
    double origin_x_ = 0.0;
    double origin_y_ = 0.0;
};

#endif  // 无头后端缺席时上面的驱动台与替身都不参与编译

}  // namespace

AURORA_TEST_CASE(grid_size_downloads_from_the_viewport_box) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    // 尺寸的 UI 侧来源（SPEC.FEAT.XFER.01）：控件首次布局即按自身可视 dp ÷ 整格 dp 派生行列，
    // 且只下发一次——重复下发会让连接侧无谓地重设伪终端尺寸。
    const auto expected = Size{h.geometry().columns, h.geometry().rows};
    AURORA_TEST_CHECK_EQ(h.resize_count(), 1U);
    AURORA_TEST_REQUIRE(h.last_resize().has_value());
    const auto sent = h.last_resize().value();
    AURORA_TEST_CHECK(sent.columns == expected.columns && sent.rows == expected.rows);
    AURORA_TEST_CHECK_EQ(h.visible_rows(), h.geometry().rows);

    const auto px = h.pixels();
    // ① 层：整盒铺默认底色，连内边距边带也是它。
    AURORA_TEST_CHECK(h.cell_center(px, h.geometry().rows - 1U, h.geometry().columns - 1U) ==
                      h.palette().default_background);
    AURORA_TEST_CHECK(h.sample(px, Rect{1.0, 1.0, 2.0, 2.0}, 0.5, 0.5) == h.palette().default_background);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(frame_diff_stays_inside_dirty_rows_and_cursor) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1b[1;1Hfirst\x1b[5;1Hsecond");
    const auto before = h.pixels();
    h.feed("\x1b[9;1Hthird");
    const auto after = h.pixels();

    AURORA_TEST_REQUIRE_MSG(Harness::count_diff(before, after) > 0U, "the new row must reach the buffer");
    // 未变行重画逐位相同：差集只能落在本帧提交行（第 9 行，0 基 8）与光标行。上一帧的光标行
    // （0 基 4）也在其中——它要被重绘回无光标态，光标残影就暴露在这里。
    std::vector<std::size_t> expected{4U, 8U};
    auto changed = h.changed_rows(before, after);
    std::sort(changed.begin(), changed.end());
    std::sort(expected.begin(), expected.end());
    AURORA_TEST_CHECK(changed == expected);

    // 上一帧光标所在的那几格必须干净：整格回到默认底色，无光标色残留（"second" 占 0..5 列，
    // 第 6 列即当时的光标位）。
    const auto &ink = *h.palette().cursor_color;
    for (std::size_t column = 6U; column < 9U; ++column) {
        AURORA_TEST_CHECK_MSG(h.cell_center(after, 4U, column) != ink,
                              "stale cursor ink survived on the previous cursor row");
        AURORA_TEST_CHECK(h.cell_center(after, 4U, column) == h.palette().default_background);
    }
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(sgr_background_band_covers_exactly_its_cells) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    // 第 3 行（0 基 2）前两格 SGR 41（基本色索引 1），第三格起回默认底色。
    h.feed("\x1b[3;1H\x1b[41mAB\x1b[49mC");
    const auto px = h.pixels();
    const auto &red = h.palette().basic[1];
    // 两格的近顶与近底都是红：既是「色带覆盖这两格」，也是「色带占满整格高」（② 层用
    // `rect_for` 的整格高矩形）。格心留给字形，不在这里比色。
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 0U, 0.1) == red);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 0U, 0.9) == red);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 1U, 0.1) == red);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 1U, 0.9) == red);
    // 越界一格都不许带上色：同行第四格与上下相邻行都保持默认底色。
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 3U, 0.1) == h.palette().default_background);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 3U, 0.9) == h.palette().default_background);
    AURORA_TEST_CHECK(h.cell_center(px, 1U, 0U) == h.palette().default_background);
    AURORA_TEST_CHECK(h.cell_center(px, 3U, 0U) == h.palette().default_background);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(underline_decoration_lands_on_the_shared_rects) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed("\x1b[4;1H\x1b[4mABC\x1b[24m");
    const auto px = h.pixels();
    std::size_t checked = 0U;
    for (const auto &run : borealis::ui::layout_row(h.visible_line(3U), h.palette())) {
        for (const auto &rect : borealis::ui::decoration_rects(h.geometry(), 3U, run)) {
            // 装饰色取该 run 的前景（裁决 7.28④），且画在文本之后（层 ④），故中心必为该色。
            AURORA_TEST_CHECK_MSG(h.sample(px, rect, 0.5, 0.5) == run.paint.foreground,
                                  "a decoration rect was not painted with the run foreground");
            ++checked;
        }
    }
    AURORA_TEST_REQUIRE_MSG(checked > 0U, "an underlined run must emit decoration rects");
    // 线是 1 物理像素的细线，不是整格填充：取一段「只有下划线的空格」，装饰上方三像素仍是底色。
    h.feed("\x1b[7;1H\x1b[4m   \x1b[24m");
    const auto blanked = h.pixels();
    bool thin = false;
    for (const auto &run : borealis::ui::layout_row(h.visible_line(6U), h.palette())) {
        for (const auto &rect : borealis::ui::decoration_rects(h.geometry(), 6U, run)) {
            AURORA_TEST_CHECK(h.sample(blanked, rect, 0.5, 0.5) == run.paint.foreground);
            AURORA_TEST_CHECK(h.sample(blanked, Rect{rect.x, rect.y - 3.0, rect.width, 1.0}, 0.5, 0.5) ==
                              h.palette().default_background);
            thin = true;
        }
    }
    AURORA_TEST_REQUIRE_MSG(thin, "an underline-only run of spaces must still be decorated");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(italic_runs_go_through_the_batch_entry) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    // 同一行两格、同一底色、同一字重，只差 `SGR 3`：底色让两格各自成一段（`layout_row` 按样式全等
    // 切分），于是两格像素带之间只剩「有没有斜切」这一个变量。
    //
    // 刻意**不**用「同一格改两次、比两帧」的写法：加入斜体本身会改变 run 的切分（原本与空白并成一
    // 段的一格独立成段），那种对照即便批量入口整个丢掉 opts 也仍给出非零差分——实测如此，判据空转。
    h.feed("\x1b[?25l\x1b[3;2H\x1b[46mA\x1b[3mA\x1b[23m\x1b[49m");
    const auto px = h.pixels();
    // 两格都得有墨（文本是抗锯齿灰度，只断言「与格底色不同的像素存在」，不比具体色值）。
    const auto cyan = h.palette().basic[6];
    const auto count_ink = [&cyan](const Pixels &band) -> std::size_t {
        std::size_t ink = 0U;
        for (std::size_t i = 0; i + 3U < band.size(); i += 4U) {
            if (band[i] != cyan.red || band[i + 1U] != cyan.green || band[i + 2U] != cyan.blue ||
                band[i + 3U] != cyan.alpha) {
                ++ink;
            }
        }
        return ink;
    };
    const auto upright = h.cell_band(px, 2U, 1U);
    const auto italic = h.cell_band(px, 2U, 2U);
    AURORA_TEST_REQUIRE_MSG(count_ink(upright) > 0U, "the upright cell painted no glyph ink");
    AURORA_TEST_REQUIRE_MSG(count_ink(italic) > 0U, "the italic cell painted no glyph ink");
    // 斜切确实发生（批量入口收到了整批 opts）：两格的像素带不得逐位相同。丢掉 opts 时这里必为 0。
    AURORA_TEST_CHECK_MSG(Harness::count_diff(upright, italic) > 0U,
                          "the italic cell rendered byte-identical to the upright one (opts lost at the batch entry)");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(cursor_shapes_and_focus_state_land_on_their_cells) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const auto &ink = *h.palette().cursor_color;
    const auto &background = h.palette().default_background;
    h.feed("\x1b[6;8H");  // 光标停在空格上：三形态的落点判据不受字形像素干扰
    constexpr std::size_t row = 5U;
    constexpr std::size_t column = 7U;
    const auto cell = borealis::ui::rect_for(h.geometry(), row, column, column + 1U);
    const double stroke = 1.0 / h.geometry().scale;
    const auto bottom_edge = Rect{cell.x, cell.y + cell.height - stroke, cell.width, stroke};
    const auto top_edge = Rect{cell.x, cell.y, cell.width, stroke};
    const auto left_edge = Rect{cell.x, cell.y, stroke, cell.height};

    // 失焦：块形降级为空心描边，格心保持底色。
    h.render();
    auto px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, cell, 0.5, 0.5) == background);
    AURORA_TEST_CHECK(h.sample(px, top_edge, 0.5, 0.5) == ink);
    AURORA_TEST_CHECK(h.sample(px, bottom_edge, 0.5, 0.5) == ink);

    // 持焦块形：整格光标色。
    h.set_focused(true);
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, cell, 0.5, 0.5) == ink);
    AURORA_TEST_CHECK(h.sample(px, cell, 0.9, 0.9) == ink);

    // 下划线档（DECSCUSR 3）：格底一条横线，格心回到底色。
    h.feed("\x1b[3 q");
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, cell, 0.5, 0.5) == background);
    AURORA_TEST_CHECK(h.sample(px, bottom_edge, 0.5, 0.5) == ink);

    // 竖线档（DECSCUSR 5）：格左沿一条竖线，格心回到底色。
    h.feed("\x1b[5 q");
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, cell, 0.5, 0.5) == background);
    AURORA_TEST_CHECK(h.sample(px, left_edge, 0.5, 0.5) == ink);

    // 关闭光标（DECTCEM）：整格干净，连描边都不留。
    h.feed("\x1b[?25l");
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, cell, 0.5, 0.5) == background);
    AURORA_TEST_CHECK(h.sample(px, top_edge, 0.5, 0.5) == background);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(review_window_shifts_with_the_bottom_and_survives_new_output) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::size_t rows = h.geometry().rows;
    constexpr std::size_t kBack = 3U;

    // 每行一种底色 + 不同文本，于是「屏幕第 i 行画的是哪条历史行」在像素层唯一可指认。
    // 底色只在 41..47 间轮转：SGR 48 是「取后续参数」的引导码，单独出现不是颜色。
    std::string script;
    for (std::size_t index = 0; index < rows + 6U; ++index) {
        script += "\x1b[4";
        script += static_cast<char>('0' + static_cast<int>(index % 7U) + 1);
        script += "mline ";
        script += static_cast<char>('A' + static_cast<int>(index % 26U));
        script += "\x1b[0m\r\n";
    }
    h.feed(script);
    const auto bottom = h.pixels();

    h.scroll(static_cast<int>(kBack));
    h.render();
    const auto backed = h.pixels();
    AURORA_TEST_REQUIRE_MSG(Harness::count_diff(bottom, backed) > 0U, "wheel-up must change the screen");
    // 内容整体下移 kBack 行：回看帧第 i 行与贴底帧第 i-kBack 行逐位相同。光标行两端都不取它
    // （贴底帧的光标在最末行，回看帧的光标已滚出可见窗）。
    for (std::size_t row = kBack; row + 1U < rows; ++row) {
        AURORA_TEST_CHECK_MSG(h.row_band(backed, row) == h.row_band(bottom, row - kBack),
                              "review window must show the row that sits kBack rows lower at the bottom");
    }

    // 回到底须逐位复现贴底帧（整窗换源不许引入累积漂移）。
    h.scroll(-static_cast<int>(kBack));
    h.render();
    AURORA_TEST_CHECK_EQ(Harness::count_diff(h.pixels(), bottom), 0U);

    // 再上滚同样的行数须回到同一画面。
    h.scroll(static_cast<int>(kBack));
    h.render();
    const auto again = h.pixels();
    AURORA_TEST_CHECK_EQ(Harness::count_diff(again, backed), 0U);

    // 裁决 D6①「距底恒定」：新输出把可见窗整体上推一行——第 i 行变成上一帧的第 i+1 行，
    // 于是回看深度不随输出被拉回底部。
    h.feed("appended\r\n");
    const auto grown = h.pixels();
    AURORA_TEST_REQUIRE_MSG(Harness::count_diff(again, grown) > 0U, "new output must move the review window");
    for (std::size_t row = 0U; row + 1U < rows; ++row) {
        AURORA_TEST_CHECK_MSG(h.row_band(grown, row) == h.row_band(again, row + 1U),
                              "the review window must keep its distance from the bottom");
    }
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(alternate_screen_cannot_be_scrolled_back) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    // 主屏先攒下可回看的历史，再切备屏：备屏无 scrollback（`alt_` 容量 0），滚不动也不该画指示条。
    std::string script;
    for (std::size_t index = 0; index < h.geometry().rows + 6U; ++index) {
        script += "main row ";
        script += static_cast<char>('0' + static_cast<int>(index % 10U));
        script += "\r\n";
    }
    h.feed(script);
    h.feed("\x1b[?1049h\x1b[H\x1b[2Jalt screen content");
    const auto alt = h.pixels();
    h.scroll(5);
    h.render();
    AURORA_TEST_CHECK_EQ(Harness::count_diff(h.pixels(), alt), 0U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(wide_and_zero_width_cells_keep_their_column_slots) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const auto &green = h.palette().basic[2];
    const auto &background = h.palette().default_background;
    // CJK-LITERAL: cjk-fixture - 双宽占位的列位对齐就是被测事实，换成英文断言即消失
    h.feed("\x1b[3;1H\x1b[42m\xe4\xb8\xad\x1b[49mX");
    auto px = h.pixels();
    // 基础格与延续格同笔形，故色带宽两格；第三格起回到默认底色（延续格不另占一格）。
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 0U, 0.06) == green);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 1U, 0.06) == green);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 1U, 0.94) == green);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 2U, 0.06) == background);

    // 零宽码点并入基础格、不另占一格：'e' + U+0301 之后紧跟的 'Y' 落在第 2 列。
    // CJK-LITERAL: cjk-fixture - combining mark 的并入次序是断言素材，转 ASCII 即失去被测事实
    h.feed("\x1b[5;1H\x1b[42me\xcc\x81\x1b[49mY");
    px = h.pixels();
    AURORA_TEST_CHECK(h.cell_probe(px, 4U, 0U, 0.06) == green);
    AURORA_TEST_CHECK(h.cell_probe(px, 4U, 1U, 0.06) == background);
    AURORA_TEST_CHECK(h.cell_probe(px, 4U, 1U, 0.94) == background);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

}  // namespace borealis::test_cases::itest_render_viewport
