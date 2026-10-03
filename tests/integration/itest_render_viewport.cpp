/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（绘制主路径）
/// 测试说明: 用无头帧缓冲对终端视口做**帧间像素差分**，验收 `SPEC.FEAT.RENDER.01` / `03` / `04`
///           的绘制契约：行列数由控件自身可视尺寸派生并下发（`SPEC.FEAT.XFER.01` 的 UI 取值腿）、
///           单帧像素差集只落在本帧提交行与光标行（含「上一帧光标行必须被重绘干净」）、SGR 色带
///           与装饰线的落笔矩形、斜体经批量入口的整批 opts 送出（不得静默退化成正体）、光标三形态
///           与失焦降级、滚轮回看的整窗换源与回到底的逐位复现、
///           回看态的「距底恒定」（裁决 D6①）、备屏不可滚、双宽与零宽的列位对齐，以及选区界面腿
///           （`SPEC.FEAT.INTERACT.02`）：拖拽扫过的格子整格变 selection 色而区间外逐位不变、
///           流式首行到行尾、失焦各半混合、Alt 列模式矩形、双击选词（断点上回空）、三击整行与
///           向上拖的外沿、高亮跟着内容而非屏幕行走，以及复制腿的变换入参；另有排版选项的接线腿
///           （配置里的缺字回退链进到交进框架的那一份选项、且不随缩放丢失，固定格推进档位取
///           **未含字距**的原始格宽并随缩放重取）。
///           像素一律比 RGBA 四通道含 alpha：Headless 帧底色是全透明黑 (0,0,0,0)，只比 RGB 会让
///           「画了个纯黑」与「什么都没画」混为一谈。
///
///           「裁剪盒外不画」**不**在此断言：脏区上报走非虚的 `dirty_bounds()`（恒等于控件自身
///           盒），控件无法把子矩形报成脏，故像素差分观察不到那条纪律（设计稿 §9 已按实测改口径）。

#include <algorithm>
#include <array>
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
using borealis::ui::Typography;

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
    explicit Harness(const PaletteSpec &palette = test_palette(),
                     const TerminalView::InteractionOptions &options = {},
                     const Typography &typography = {}, std::vector<std::string> font_fallback_chain = {})
        : palette_(palette),
          options_(options),
          font_(test_font()),
          typography_(typography),
          session_(std::make_unique<Session>(own_connection(), kNominalSize, kScrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, palette_, font_, typography_, kPaddingDp,
                                               std::chrono::milliseconds{kBlinkPeriodMs}, options_,
                                               std::move(font_fallback_chain))),
          root_(std::static_pointer_cast<au::Widget>(view_)) {
        session_->start();
        render();  // 帧 0：布局发生在这里，控件据此把行列尺寸下发给会话
        const au::Rect box = view_->paint_bounds();
        scale_ = window_.surface().scale_factor();
        origin_x_ = static_cast<double>(box.origin.x);
        origin_y_ = static_cast<double>(box.origin.y);
        box_ = box;
        refresh_geometry();
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
        event.position = grid_center();
        event.delta_y = static_cast<float>(rows);
        (void)au::EventDispatcher::dispatch(root_.widget(), event);
    }

    /// @brief Ctrl+滚轮缩放 @p steps 档（正 = 放大，一档 ±1 pt），走真实滚轮事件派发。
    ///
    /// 缩放后把驱动台自己的字体同步成控件报告的字号再复算格网：字号算式（步长与钳位）由
    /// `font_size_pt()` 单独断言，格网断言因此只测「几何跟着字号重算且不裂」这一件事。
    auto zoom(int steps) -> void {
        au::ScrollEvent event;
        event.position = grid_center();
        event.modifiers = au::ModifierKey::Control;
        const int unit = steps < 0 ? -1 : 1;
        for (int i = 0; i < (steps < 0 ? -steps : steps); ++i) {
            event.delta_y = static_cast<float>(unit);
            (void)au::EventDispatcher::dispatch(root_.widget(), event);
        }
        render();
        font_.size_pt = view_->font_size_pt();
        refresh_geometry();
    }

    /// @brief 发一个指针事件：行列是**屏幕**坐标，落点取该格中心。
    ///
    /// 走实体派发器而非静态入口，为的是连击判定与指针捕获都隔离在本驱动台：静态那条共用进程内
    /// 单例，上一个用例的点击时刻与落点会让本次 Press 的 `click_count` 从 2 起算。
    /// `click_count` 由派发器在派发前覆写，故多击须按 Press/Release 交替实发，手写该字段无效。
    auto pointer(au::MouseAction action, std::size_t row, std::size_t column,
                 au::ModifierKey modifiers = au::ModifierKey::None) -> void {
        au::MouseEvent event;
        const Rect band = borealis::ui::rect_for(geometry_, row, column, column + 1U);
        event.position = au::Point{.x = static_cast<float>(origin_x_ + band.x + band.width * 0.5),
                                   .y = static_cast<float>(origin_y_ + band.y + band.height * 0.5)};
        event.button = au::MouseButton::Left;
        event.action = action;
        event.modifiers = modifiers;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 单击一次（按下即抬起）。连击序号由派发器累加，故本方法连续调用 N 次即 N 击。
    auto click(std::size_t row, std::size_t column,
               au::ModifierKey modifiers = au::ModifierKey::None) -> void {
        pointer(au::MouseAction::Press, row, column, modifiers);
        pointer(au::MouseAction::Release, row, column, modifiers);
    }

    /// @brief 选区文本（复制腿的产物，不经系统剪贴板）。
    [[nodiscard]] auto selected_text() -> std::string { return view_->selected_text(); }

    /// @brief 控件报告的当前字号：缩放算式（步长与钳位）的直接观测点。
    [[nodiscard]] auto font_size_pt() const -> float { return view_->font_size_pt(); }

    /// @brief 控件交进框架的排版选项：回退链与固定格档位的观测点（三项都反推不出像素）。
    [[nodiscard]] auto layout_options() const -> const au::render::TextLayoutOpts & {
        return view_->layout_options();
    }

    /// @brief 框架给出的**原始**整格宽（物理 px、已含 scale）——固定格推进档位的独立复算点。
    ///
    /// 与 `refresh_geometry` 同一条算式（取驱动台自己那份已同步字号的字体），故不是把实现的输出
    /// 当预期；档位若误取回填后的格宽（含字距），本值与观测值就正好差一个字距。
    [[nodiscard]] auto raw_cell_width_px() const {
        return au::render::FontEngine::monospace_cell(font_, scale_).cell_width_px;
    }

    /// @brief 控件当前是否持焦：指针 Press 的焦点归属由派发器决定，本用例要能证到它。
    [[nodiscard]] auto view_focused() const -> bool { return view_->is_focused(); }

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

    /// @brief 从某格**左上角**起的固定尺寸窗口（跨两套格网比字形相位用）。
    ///
    /// `cell_band` 的宽是各自那一格的宽，两套格网的带宽不同就没法逐位比；字距腿要问的是「字形
    /// 相对自己格左沿的落点」，故两边都锚在各自的格左沿上取同尺寸的一块。
    [[nodiscard]] auto cell_window(const Pixels &px, std::size_t row, std::size_t column, std::size_t width,
                                   std::size_t height) const -> Pixels {
        Pixels out;
        const std::size_t top = static_cast<std::size_t>(
            origin_y_ + geometry_.padding + static_cast<double>(row) * geometry_.cell_height);
        const std::size_t left = static_cast<std::size_t>(
            origin_x_ + geometry_.padding + static_cast<double>(column) * geometry_.cell_width);
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

    /// @brief 网格中心的逻辑坐标：滚轮事件要落在可命中链里，落窗外就成了「没人消费」。
    [[nodiscard]] auto grid_center() const -> au::Point {
        const auto center_x =
            static_cast<float>(origin_x_ + (geometry_.cell_width * static_cast<double>(geometry_.columns) / 2.0));
        const auto center_y =
            static_cast<float>(origin_y_ + (geometry_.cell_height * static_cast<double>(geometry_.rows) / 2.0));
        return au::Point{.x = center_x, .y = center_y};
    }

    /// @brief 独立复算一遍格网：绘制侧的几何必须能被测试侧重算出来，否则像素断言退化成
    ///        「拿实现自己的输出当预期」。字体与排版量取驱动台自己的副本。
    auto refresh_geometry() -> void {
        const auto metrics = au::render::FontEngine::monospace_cell(font_, scale_);
        const auto typed = borealis::ui::apply_typography(
            CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px},
            static_cast<double>(scale_), typography_);
        geometry_ = borealis::ui::make_geometry(
            typed.cells, scale_,
            LogicalSize{static_cast<double>(box_.size.width), static_cast<double>(box_.size.height)}, kPaddingDp);
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    PaletteSpec palette_{};
    TerminalView::InteractionOptions options_{};  ///< 选区行为的可配项，须在 view_ 之前就位。
    au::Font font_{};
    Typography typography_{};  ///< 排版可调量，须在 view_ 之前就位。
    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    au::Node root_;
    au::FocusManager focus_;
    au::EventDispatcher pointer_dispatcher_;  ///< 本驱动台私有的连击判定与指针捕获状态。
    GridGeometry geometry_{};
    au::Rect box_{};  ///< 控件的绘制盒，`refresh_geometry` 的可视尺寸来源。
    float scale_ = 1.0F;
    double origin_x_ = 0.0;
    double origin_y_ = 0.0;
};

/// @brief 一格带子里「有墨的横行」的首末行号（行高腿的观测手段）。
///
/// 只认与底色不同的像素：抗锯齿字形本就是前景与底色的混合，比色值比不出，但「这一行有没有墨」
/// 是稳的。首行号即字盒顶到第一笔的距离，正是上半 leading 的直接读数。
[[nodiscard]] auto ink_rows(const Pixels &band, std::size_t width, const RgbaColor &background)
    -> std::pair<std::size_t, std::size_t> {
    std::pair<std::size_t, std::size_t> span{0U, 0U};
    bool seen = false;
    const std::size_t height = width > 0U ? band.size() / (4U * width) : 0U;
    for (std::size_t y = 0; y < height; ++y) {
        bool ink = false;
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t index = (y * width + x) * 4U;
            if (band[index] != background.red || band[index + 1U] != background.green ||
                band[index + 2U] != background.blue || band[index + 3U] != background.alpha) {
                ink = true;
                break;
            }
        }
        if (!ink) {
            continue;
        }
        if (!seen) {
            span.first = y;
            seen = true;
        }
        span.second = y;
    }
    return span;
}

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

AURORA_TEST_CASE(block_cursor_redraw_keeps_the_italic_slant) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.set_focused(true);
    // 同一行相邻两格、同一字形、一正一斜，块形光标逐帧移到其上：两格带子的底色都是光标色、
    // 字形墨色都是该格的合成底色，相位只差一个整格宽，于是唯一变量是光标重画那一次有没有带上斜体
    // opts（不带则该格与正体那格逐位相同）。
    h.feed("\x1b[3;2HA\x1b[3mA\x1b[23m\x1b[3;2H");
    const auto on_upright = h.pixels();
    h.feed("\x1b[3;3H");
    const auto on_italic = h.pixels();

    // 块形确实落了笔：否则两格只剩 ③ 的底图，本例的判据会因另一条路径空转。
    const auto &cursor_ink = *h.palette().cursor_color;
    AURORA_TEST_REQUIRE_MSG(h.sample(on_upright, borealis::ui::rect_for(h.geometry(), 2U, 1U, 2U), 0.5, 0.9) ==
                                cursor_ink,
                            "the block cursor did not reach the upright cell");
    AURORA_TEST_REQUIRE_MSG(h.sample(on_italic, borealis::ui::rect_for(h.geometry(), 2U, 2U, 3U), 0.5, 0.9) ==
                                cursor_ink,
                            "the block cursor did not reach the italic cell");

    AURORA_TEST_CHECK_MSG(Harness::count_diff(h.cell_band(on_upright, 2U, 1U), h.cell_band(on_italic, 2U, 2U)) > 0U,
                          "the cell under the block cursor lost its italic slant (redraw dropped the layout opts)");
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

AURORA_TEST_CASE(drag_selection_paints_only_the_swept_cells) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const auto &background = h.palette().default_background;
    const auto ink = borealis::ui::selection_color(h.palette());  // 未配 selection 槽，回落 basic[8]
    h.feed("\x1b[?25l\x1b[3;1HABCDEFGH");
    const auto before = h.pixels();

    h.pointer(au::MouseAction::Press, 2U, 1U);
    h.pointer(au::MouseAction::Move, 2U, 4U);
    h.pointer(au::MouseAction::Release, 2U, 4U);
    h.render();
    const auto after = h.pixels();

    // 整帧只差落在被扫到的那一行：其余行连一个像素都不许动（高亮不越行）。
    const auto changed = h.changed_rows(before, after);
    AURORA_TEST_REQUIRE_MSG(changed.size() == 1U && changed[0] == 2U, "the highlight must stay on its row");
    // 选中区间整格变为 selection 色（格腰留给字形，故取近顶与近底两处）。
    for (std::size_t column = 1U; column <= 4U; ++column) {
        AURORA_TEST_CHECK(h.cell_probe(after, 2U, column, 0.06) == ink);
        AURORA_TEST_CHECK(h.cell_probe(after, 2U, column, 0.94) == ink);
    }
    // 区间外的**无字形**格逐位不变：高亮不外溢，也不牵动别处的落笔。带字形的未选格只断言带色回到
    // 底色——行文本按 run 起排，选中区间把该行切成三段后同一 run 里后续字形的像素覆盖会变（实测
    // 如此），那是框架 shaping 的性质而不是选区层的，逐位相等在这里判不出选区缺陷。
    for (const std::size_t column : {std::size_t{0U}, std::size_t{8U}, std::size_t{9U}, std::size_t{10U}}) {
        AURORA_TEST_CHECK(h.cell_probe(after, 2U, column, 0.06) == background);
        AURORA_TEST_CHECK(h.cell_probe(after, 2U, column, 0.94) == background);
    }
    for (const std::size_t column : {std::size_t{8U}, std::size_t{9U}, std::size_t{10U}}) {
        AURORA_TEST_CHECK_MSG(h.cell_band(before, 2U, column) == h.cell_band(after, 2U, column),
                              "a blank unselected cell changed while only columns 1..4 were swept");
    }
    AURORA_TEST_CHECK_MSG(h.cell_probe(after, 2U, 5U, 0.06) == background &&
                              h.cell_probe(after, 2U, 7U, 0.94) == background,
                          "the highlight leaked past the swept interval onto glyph cells");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(flow_selection_runs_to_the_row_end_and_stops_at_the_focus) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::size_t columns = h.geometry().columns;
    const auto &background = h.palette().default_background;
    const auto ink = borealis::ui::selection_color(h.palette());
    h.feed("\x1b[?25l\x1b[3;1HABCDEFGH");
    const auto before = h.pixels();

    h.pointer(au::MouseAction::Press, 2U, 3U);
    h.pointer(au::MouseAction::Move, 4U, 2U);
    h.pointer(au::MouseAction::Release, 4U, 2U);
    h.render();
    const auto after = h.pixels();

    // 首行从按下那一格起到**行尾**（含未写入的填充格），末行截到焦点列，中间行整行。
    AURORA_TEST_CHECK(h.cell_probe(after, 2U, 2U, 0.06) == background);
    AURORA_TEST_CHECK(h.cell_probe(after, 2U, 3U, 0.06) == ink);
    AURORA_TEST_CHECK(h.cell_probe(after, 2U, columns - 1U, 0.06) == ink);
    AURORA_TEST_CHECK(h.cell_probe(after, 3U, 0U, 0.06) == ink);
    AURORA_TEST_CHECK(h.cell_probe(after, 3U, columns - 1U, 0.06) == ink);
    AURORA_TEST_CHECK(h.cell_probe(after, 4U, 2U, 0.06) == ink);
    AURORA_TEST_CHECK(h.cell_probe(after, 4U, 3U, 0.06) == background);
    // 区间外两行整带不变（中间行的整行高亮不许外溢到相邻行）。
    AURORA_TEST_CHECK_EQ(Harness::count_diff(h.row_band(before, 1U), h.row_band(after, 1U)), 0U);
    AURORA_TEST_CHECK_EQ(Harness::count_diff(h.row_band(before, 5U), h.row_band(after, 5U)), 0U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(unfocused_selection_blends_half_toward_the_background) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const auto &background = h.palette().default_background;
    const auto ink = borealis::ui::selection_color(h.palette());
    h.feed("\x1b[?25l\x1b[3;2Hword");

    h.pointer(au::MouseAction::Press, 2U, 1U);
    h.pointer(au::MouseAction::Move, 2U, 4U);
    h.pointer(au::MouseAction::Release, 2U, 4U);
    h.render();
    // 指针 Press 经真实焦点序把焦点给了本控件，故此刻是持焦态：取 selection 槽本体。
    AURORA_TEST_REQUIRE(h.view_focused());
    auto px = h.pixels();
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 2U, 0.06) == ink);

    h.set_focused(false);
    h.render();
    px = h.pixels();
    const auto blended = borealis::ui::mix_half(ink, background);
    AURORA_TEST_REQUIRE_MSG(blended != ink, "the two colors must differ for this to be a witness");
    for (std::size_t column = 1U; column <= 4U; ++column) {
        AURORA_TEST_CHECK(h.cell_probe(px, 2U, column, 0.06) == blended);
    }
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 0U, 0.06) == background);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(alt_drag_selects_a_rectangular_block) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const auto &background = h.palette().default_background;
    const auto ink = borealis::ui::selection_color(h.palette());
    std::string script = "\x1b[?25l";
    for (std::size_t row = 2U; row <= 5U; ++row) {
        script += "\x1b[";
        script += std::to_string(row + 1U);
        script += ";1H0123456789";
    }
    h.feed(script);

    h.pointer(au::MouseAction::Press, 2U, 2U, au::ModifierKey::Alt);
    h.pointer(au::MouseAction::Move, 4U, 5U, au::ModifierKey::Alt);
    h.pointer(au::MouseAction::Release, 4U, 5U, au::ModifierKey::Alt);
    h.render();
    const auto px = h.pixels();

    // 列模式是矩形：每一行进选区的列区间都相同，且行尾填充格一律不入选区。
    for (std::size_t row = 2U; row <= 4U; ++row) {
        AURORA_TEST_CHECK(h.cell_probe(px, row, 1U, 0.06) == background);
        AURORA_TEST_CHECK(h.cell_probe(px, row, 2U, 0.06) == ink);
        AURORA_TEST_CHECK(h.cell_probe(px, row, 5U, 0.06) == ink);
        AURORA_TEST_CHECK(h.cell_probe(px, row, 6U, 0.06) == background);
        AURORA_TEST_CHECK(h.cell_probe(px, row, 10U, 0.06) == background);
    }
    AURORA_TEST_CHECK(h.cell_probe(px, 5U, 3U, 0.06) == background);
    // 复制腿随行：三行的同一列区间，行以 **LF** 分隔（裁决 7.32①）。
    AURORA_TEST_CHECK_EQ(h.selected_text(), "2345\n2345\n2345");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(double_click_selects_the_word_and_nothing_on_a_break) {
#ifdef AURORA_BACKEND_HEADLESS
    TerminalView::InteractionOptions options;
    options.word_delimiters = ";";
    Harness h(test_palette(), options);
    AURORA_TEST_REQUIRE(h.preflight());
    const auto &background = h.palette().default_background;
    const auto ink = borealis::ui::selection_color(h.palette());
    h.feed("\x1b[?25l\x1b[3;1Hfoo;bar");

    h.click(2U, 1U);  // 单击落在词内：逐格
    h.click(2U, 1U);  // 同一格第二次按下即双击
    h.render();
    auto px = h.pixels();
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 0U, 0.06) == ink);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 2U, 0.06) == ink);
    // 界定符本身不入选区，词后紧接的 `;` 保持在底色。
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 3U, 0.06) == background);
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 4U, 0.06) == background);
    AURORA_TEST_CHECK_EQ(h.selected_text(), "foo");

    // 双击落在断点上回空（裁决 7.39④）：既有选区就此作废，而不是扩成一串空白。
    h.click(2U, 3U);
    h.click(2U, 3U);
    h.render();
    px = h.pixels();
    for (std::size_t column = 0U; column <= 6U; ++column) {
        AURORA_TEST_CHECK(h.cell_probe(px, 2U, column, 0.06) == background);
    }
    AURORA_TEST_CHECK_EQ(h.selected_text(), std::string{});
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(triple_click_selects_the_row_and_dragging_up_extends_whole_rows) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::size_t columns = h.geometry().columns;
    const auto &background = h.palette().default_background;
    const auto ink = borealis::ui::selection_color(h.palette());
    h.feed("\x1b[?25l\x1b[2;1Htop\r\n\x1b[3;1HABC");

    // 三击落在行尾空白上也选整行（D5①）：行号是屏幕行，内容是上一行写下的 "top"。
    h.click(1U, 0U);
    h.click(1U, 0U);
    h.click(1U, 0U);
    h.render();
    auto px = h.pixels();
    for (std::size_t column = 0U; column < columns; ++column) {
        AURORA_TEST_CHECK(h.cell_probe(px, 1U, column, 0.06) == ink);
    }
    AURORA_TEST_CHECK(h.cell_probe(px, 2U, 0U, 0.06) == background);
    // 行粒度取的是**整行含行尾空白**（D5①），故复制结果带满列宽的填充空格。
    const std::string blanks(columns - 3U, ' ');
    AURORA_TEST_CHECK_EQ(h.selected_text(), "top" + blanks);

    // 连击上限是 3，故第四次按下仍是行粒度；向上拖时端点须折成区间**外沿**，
    // 否则只选中起始行的第一列（裁决 7.40③）。
    h.pointer(au::MouseAction::Press, 1U, 0U);
    h.pointer(au::MouseAction::Move, 2U, 1U);
    h.pointer(au::MouseAction::Release, 2U, 1U);
    h.render();
    px = h.pixels();
    for (std::size_t column = 0U; column < columns; ++column) {
        AURORA_TEST_CHECK(h.cell_probe(px, 1U, column, 0.06) == ink);
        AURORA_TEST_CHECK(h.cell_probe(px, 2U, column, 0.06) == ink);
    }
    AURORA_TEST_CHECK_EQ(h.selected_text(), "top" + blanks + "\nABC" + blanks);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(selection_highlight_follows_the_content_when_output_arrives) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::size_t rows = h.geometry().rows;
    const auto ink = borealis::ui::selection_color(h.palette());
    std::string script = "\x1b[?25l";
    for (std::size_t index = 0U; index < rows + 2U; ++index) {
        script += "row ";
        script += static_cast<char>('A' + static_cast<int>(index % 26U));
        script += "\r\n";
    }
    h.feed(script);
    const std::size_t row = rows - 3U;
    h.pointer(au::MouseAction::Press, row, 2U);
    h.pointer(au::MouseAction::Move, row, 3U);
    h.pointer(au::MouseAction::Release, row, 3U);
    h.render();
    auto px = h.pixels();
    AURORA_TEST_REQUIRE(h.cell_probe(px, row, 2U, 0.06) == ink);
    AURORA_TEST_REQUIRE_EQ(h.selected_text(), "w ");  // 每行都是 "row <字母>"，第 2~3 列即 "w "

    // 新输出把可见窗整体上推一行：高亮跟着内容走而不是跟着屏幕行走（端点存的是存储行序）。
    h.feed("appended\r\n");
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.cell_probe(px, row - 1U, 2U, 0.06) == ink);
    AURORA_TEST_CHECK(h.cell_probe(px, row, 2U, 0.06) != ink);
    AURORA_TEST_CHECK_EQ(h.selected_text(), "w ");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(copy_options_travel_with_the_interaction_options) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness plain;
    AURORA_TEST_REQUIRE(plain.preflight());
    plain.feed("\x1b[?25l\x1b[3;1HAB   \x1b[4;1Htail");
    plain.pointer(au::MouseAction::Press, 2U, 0U);
    plain.pointer(au::MouseAction::Move, 3U, 3U);
    plain.pointer(au::MouseAction::Release, 3U, 3U);
    plain.render();
    // 原样：首行选到**行尾**（含未写入的填充格），行以 LF 分隔（剪贴板侧不翻译，裁决 7.32①）。
    const std::string blanks(plain.geometry().columns - 2U, ' ');
    AURORA_TEST_CHECK_EQ(plain.selected_text(), "AB" + blanks + "\ntail");

    TerminalView::InteractionOptions options;
    options.copy.trim_trailing_space = true;
    Harness trimmed(test_palette(), options);
    AURORA_TEST_REQUIRE(trimmed.preflight());
    trimmed.feed("\x1b[?25l\x1b[3;1HAB   \x1b[4;1Htail");
    trimmed.pointer(au::MouseAction::Press, 2U, 0U);
    trimmed.pointer(au::MouseAction::Move, 3U, 3U);
    trimmed.pointer(au::MouseAction::Release, 3U, 3U);
    trimmed.render();
    // 同一份选区、只换一个变换键：配置值确实经 `InteractionOptions` 抵达复制腿。
    AURORA_TEST_CHECK_EQ(trimmed.selected_text(), "AB\ntail");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(line_height_moves_the_glyph_down_by_the_upper_half_leading) {
#ifdef AURORA_BACKEND_HEADLESS
    const auto raw = au::render::FontEngine::monospace_cell(test_font(), 1.0F);
    const auto leading = static_cast<std::size_t>(raw.cell_height_px) / 2U;  // 余数归行盒下沿
    const auto line_height = static_cast<double>(raw.cell_height_px + leading * 2U) /
                             static_cast<double>(raw.cell_height_px);
    const auto &background = test_palette().default_background;

    Harness tight;
    Harness tall(test_palette(), TerminalView::InteractionOptions{}, Typography{.line_height = line_height});
    AURORA_TEST_REQUIRE(tight.preflight() && tall.preflight());
    const auto width = static_cast<std::size_t>(tall.geometry().cell_width);
    AURORA_TEST_REQUIRE_MSG(leading > 0U && tall.geometry().cell_height ==
                                                 tight.geometry().cell_height + static_cast<double>(leading * 2U),
                            "the requested line height must add exactly two half-leadings to the row step");

    const std::string script = "\x1b[?25l\x1b[3;1HW";
    tight.feed(script);
    tall.feed(script);
    const auto flat = ink_rows(tight.cell_band(tight.pixels(), 2U, 0U), width, background);
    const auto dropped = ink_rows(tall.cell_band(tall.pixels(), 2U, 0U), width, background);
    AURORA_TEST_REQUIRE_MSG(flat.second > flat.first, "the tight row must be the one with ink to compare against");
    // 行高加大只把空白加在字盒**之上**：字形的墨高度不变，整块下移上半 leading。
    // 框架按「行盒顶 + 该字体 ascender」定位基线（GDI `TA_TOP` 遗留语义），少了视口那一下下移，
    // 文字就贴死行顶而行盒下方空一片——两处的首行号因此相同，本判据随即转红。
    AURORA_TEST_CHECK_EQ(dropped.first, flat.first + leading);
    AURORA_TEST_CHECK_EQ(dropped.second - dropped.first, flat.second - flat.first);

    // 色带吃的是重算后的整格高（② 层用 `rect_for`），故加高后的行顶与行底仍在带内。
    tall.feed("\x1b[4;1H\x1b[41mAB\x1b[49m");
    const auto px = tall.pixels();
    const auto &red = tall.palette().basic[1];
    AURORA_TEST_CHECK(tall.cell_probe(px, 3U, 0U, 0.02) == red);
    AURORA_TEST_CHECK(tall.cell_probe(px, 3U, 1U, 0.98) == red);
    AURORA_TEST_CHECK(tall.cell_probe(px, 3U, 2U, 0.98) == background);
    // 同一窗口的行数按新步长重排（`SPEC.FEAT.RENDER.05`：行高变了行列数必须跟着变）。
    AURORA_TEST_CHECK(tall.geometry().rows < tight.geometry().rows);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(letter_spacing_widens_the_column_step_without_drifting_the_glyphs) {
#ifdef AURORA_BACKEND_HEADLESS
    const auto raw = au::render::FontEngine::monospace_cell(test_font(), 1.0F);
    const auto spacing_px = static_cast<std::size_t>(raw.cell_width_px) / 3U;
    // 无头缩放恒为 1，故配置 dp 与吸附后的整像素同值；比例取自实测格宽，改字号也不破这条判据。
    Harness tight;
    Harness spread(test_palette(), TerminalView::InteractionOptions{},
                   Typography{.letter_spacing_dp = static_cast<double>(spacing_px)});
    AURORA_TEST_REQUIRE(tight.preflight() && spread.preflight());
    const auto narrow = static_cast<std::size_t>(tight.geometry().cell_width);
    const auto tall = static_cast<std::size_t>(tight.geometry().cell_height);
    AURORA_TEST_REQUIRE_MSG(spread.geometry().cell_width == tight.geometry().cell_width + spacing_px,
                            "the quantized spacing must enter the column step");

    // 每格的字形仍停在**自己那一格**的左沿：锚在各自格左沿的同尺寸窗口逐位相同。
    // 吸附后的字距是整数像素，故第 k 格的字形相对格左沿的亚像素相位与未加宽时一致；
    // 若绘制侧漏掉排版选项（或漏掉吸附而按小数 dp 加距），第 1、2 格会停在未加宽的推进位上
    // （左移一格距、两格距），窗口随即对不上——那正是长行按块累积漂移的那一档。
    const std::string script = "\x1b[?25l\x1b[3;1HABC";
    tight.feed(script);
    spread.feed(script);
    const auto tight_px = tight.pixels();
    const auto spread_px = spread.pixels();
    for (const std::size_t column : {std::size_t{0U}, std::size_t{1U}, std::size_t{2U}}) {
        AURORA_TEST_CHECK_MSG(spread.cell_window(spread_px, 2U, column, narrow, tall) ==
                                  tight.cell_window(tight_px, 2U, column, narrow, tall),
                              "a glyph slid off its own column once the cell got wider");
    }
    // 色带跟着新列步长走：两格红带止于第 2 格的右沿，第 3 格起回到默认底色。
    spread.feed("\x1b[5;1H\x1b[41mAB\x1b[49m");
    const auto banded = spread.pixels();
    const auto &red = spread.palette().basic[1];
    const auto &background = spread.palette().default_background;
    AURORA_TEST_CHECK(spread.cell_probe(banded, 4U, 1U, 0.06) == red);
    AURORA_TEST_CHECK(spread.cell_probe(banded, 4U, 2U, 0.06) == background);
    // 列数按新步长重排，行高不受字距影响。
    AURORA_TEST_CHECK(spread.geometry().columns < tight.geometry().columns);
    AURORA_TEST_CHECK(spread.geometry().cell_height == tight.geometry().cell_height);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(neutral_typography_reproduces_the_untouched_frame) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness plain;
    Harness neutral(test_palette(), TerminalView::InteractionOptions{},
                    Typography{.line_height = 1.0, .letter_spacing_dp = 0.0});
    AURORA_TEST_REQUIRE(plain.preflight() && neutral.preflight());
    // 装配层无条件下传一个 `Typography`，代价是缺省档必须与「没有这一档」逐位相同：倍数 1.0 取整
    // 回原行高、字距 0 吸附回 0 像素，两处任一带上舍入残差，整帧就会系统性错位。
    const std::string script = "\x1b[?25l\x1b[2;1H\x1b[4mABC 中文\x1b[24m\x1b[5;1H\x1b[43mXY\x1b[49mZ";
    plain.feed(script);
    neutral.feed(script);
    // CJK-LITERAL: cjk-fixture - 回退面的落位随排版量一起变，纯 ASCII 样本会放过双宽那一腿
    AURORA_TEST_CHECK_EQ(Harness::count_diff(plain.pixels(), neutral.pixels()), 0U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(ctrl_wheel_zoom_rebuilds_the_grid_and_redownloads_the_size) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const auto start = h.geometry();
    const auto size_before = h.font_size_pt();
    const auto resizes_before = h.resize_count();

    h.zoom(3);
    AURORA_TEST_REQUIRE(h.preflight());
    AURORA_TEST_CHECK_MSG(h.font_size_pt() == size_before + 3.0F, "one wheel notch is exactly one point");
    AURORA_TEST_CHECK(h.geometry().cell_width > start.cell_width);
    AURORA_TEST_CHECK(h.geometry().rows < start.rows);
    // 行列数真变了才下发（`SPEC.FEAT.XFER.01` 的 UI 取值腿在缩放后须再走一遍）。
    AURORA_TEST_CHECK_EQ(h.resize_count(), resizes_before + 1U);
    AURORA_TEST_REQUIRE(h.last_resize().has_value());
    const auto sent = h.last_resize().value();
    AURORA_TEST_CHECK(sent.columns == h.geometry().columns && sent.rows == h.geometry().rows);
    // 放大后色带仍逐格对齐：几何重算不裂（`SPEC.FEAT.RENDER.05` 的缩放档判据）。
    h.feed("\x1b[6;1H\x1b[41mAB\x1b[49mC");
    const auto zoomed = h.pixels();
    const auto &red = h.palette().basic[1];
    const auto &background = h.palette().default_background;
    AURORA_TEST_CHECK(h.cell_probe(zoomed, 5U, 0U, 0.06) == red);
    AURORA_TEST_CHECK(h.cell_probe(zoomed, 5U, 1U, 0.94) == red);
    AURORA_TEST_CHECK(h.cell_probe(zoomed, 5U, 2U, 0.06) == background);

    h.zoom(-3);
    AURORA_TEST_CHECK_MSG(h.font_size_pt() == size_before, "zooming back must return to the starting point size");
    const auto &back = h.geometry();
    AURORA_TEST_CHECK(back.cell_width == start.cell_width && back.cell_height == start.cell_height);
    AURORA_TEST_CHECK(back.columns == start.columns && back.rows == start.rows);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(wheel_zoom_clamps_at_the_bounds_and_yields_the_rest_to_the_review) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    // 到界值即停口，且**不吃事件**：越界那一档照旧冒泡给回看，否则顶/底档就再也滚不动了。
    h.zoom(200);
    AURORA_TEST_CHECK_MSG(h.font_size_pt() == 72.0F, "the upper bound is 72 pt");
    h.zoom(-200);
    AURORA_TEST_CHECK_MSG(h.font_size_pt() == 6.0F, "the lower bound is 6 pt");

    std::string script = "\x1b[?25l";
    for (std::size_t index = 0U; index < h.geometry().rows + 6U; ++index) {
        script += "line ";
        script += static_cast<char>('A' + static_cast<int>(index % 26U));
        script += "\r\n";
    }
    h.feed(script);
    h.scroll(2);
    h.render();
    const auto backed = h.pixels();
    h.scroll(-1);
    h.render();
    const auto one_row_lower = h.pixels();
    AURORA_TEST_REQUIRE_MSG(Harness::count_diff(backed, one_row_lower) > 0U,
                            "the review window must be able to move at all");

    // 回到同一处再发越界的那一档：结果须与「普通滚轮向下一档」逐位相同。
    h.scroll(1);
    h.render();
    AURORA_TEST_CHECK_EQ(Harness::count_diff(h.pixels(), backed), 0U);
    h.zoom(-1);
    AURORA_TEST_CHECK_MSG(h.font_size_pt() == 6.0F, "an out-of-range notch must not move the size");
    AURORA_TEST_CHECK_EQ(Harness::count_diff(h.pixels(), one_row_lower), 0U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(double_width_cells_do_not_shift_the_following_columns) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const auto width = static_cast<std::size_t>(h.geometry().cell_width);
    const auto height = static_cast<std::size_t>(h.geometry().cell_height);
    // 第 3 行以双宽汉字起头，第 5 行以两个空格起头，`AB` 在两行里都落在第 2、3 列。全空白段不产
    // 文本（`layout_row` 把它的文本清空），故第 5 行的 run 从第 2 列起排、pen 落在那一列左沿；
    // 第 3 行的 `AB` 却排在汉字那一段之后，落点全凭汉字自己的推进量。实测回退面的推进是 19 px
    // 而两格是 22 px，同一 run 内紧随其后的字形因此整体左挪——第 5 行就是同一列上的对照。
    // CJK-LITERAL: cjk-fixture - 回退面的推进量就是被测事实，转成 ASCII 这条判据即消失
    h.feed("\x1b[?25l\x1b[3;1H\xe4\xb8" "\xad" "AB\r\n\r\n  AB");
    const auto px = h.pixels();
    // 证人非空转：汉字必须真的把墨铺进延续格，否则「双宽那一腿」根本没被走到。
    const auto blank = h.cell_window(px, 8U, 8U, width, height);
    AURORA_TEST_REQUIRE_MSG(Harness::count_diff(h.cell_window(px, 2U, 1U, width, height), blank) > 0U,
                            "the witness needs the Han glyph to span its continuation cell");
    for (const std::size_t column : {std::size_t{2U}, std::size_t{3U}}) {
        AURORA_TEST_CHECK_MSG(h.cell_window(px, 2U, column, width, height) ==
                                  h.cell_window(px, 4U, column, width, height),
                              "a following glyph slid off its own column after a double-width cell");
    }
    // 断点须落在每个双宽字形之后而非只落在第一个：连续三个汉字（共六格）后跟 `AB`，落点仍须与
    // 「六个空格后跟 AB」逐位相同。只断第一个双宽格之后的话，剩余两个汉字的推进误差照旧累积。
    h.feed("\x1b[7;1H\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b"
           "AB\r\n\r\n      AB");
    const auto many = h.pixels();
    for (const std::size_t column : {std::size_t{6U}, std::size_t{7U}}) {
        AURORA_TEST_CHECK_MSG(h.cell_window(many, 6U, column, width, height) ==
                                  h.cell_window(many, 8U, column, width, height),
                              "the double-width advance error accumulated over consecutive Han glyphs");
    }
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(fallback_face_glyphs_do_not_shift_the_following_columns) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const auto width = static_cast<std::size_t>(h.geometry().cell_width);
    const auto height = static_cast<std::size_t>(h.geometry().cell_height);
    // 固定格推进档位的像素证人：主面缺字时选面回退，而**回退面的单宽推进量不由网格决定**。
    // 第 3 行的 ① 与第 5 行的空格同占第 1 列，`AB` 在两行里都落在第 2、3 列；档位没生效时
    // 第 3 行的 AB 会按 ① 自己的推进落点，与对照行逐位不同。
    // CJK-LITERAL: cjk-fixture - 回退面的推进量就是被测事实，转成 ASCII 这条判据即消失
    h.feed("\x1b[?25l\x1b[3;1H\xe2\x91\xa0" "AB\r\n\r\n AB");
    const auto px = h.pixels();
    AURORA_TEST_REQUIRE_MSG(Harness::count_diff(h.cell_window(px, 2U, 0U, width, height),
                                                h.cell_window(px, 4U, 0U, width, height)) > 0U,
                            "the witness needs the fallback glyph to actually put ink");
    // 只断第 2、3 列：① 的回退面字形比一格宽，其墨迹越出自己那格压进第 1 列（实测档位开合该列
    // 都非零），那属回退面字形自己的宽度而非推进误差；推进误差落在其**后面**的列上（无档位时
    // 第 2 列差 134、第 3 列差 74，有档位时两列均为 0）。
    for (const std::size_t column : {std::size_t{2U}, std::size_t{3U}}) {
        AURORA_TEST_CHECK_MSG(h.cell_window(px, 2U, column, width, height) ==
                                  h.cell_window(px, 4U, column, width, height),
                              "a glyph following the fallback-face cell slid off its own column");
    }
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(configured_fallback_chain_survives_the_metric_refresh) {
#ifdef AURORA_BACKEND_HEADLESS
    // 缺省档：空链 = 不注入按族链，只走框架的全局默认链。这一半是「装配层不给这一项时与 G23
    // 回货前逐位同形」的判据。
    Harness plain;
    AURORA_TEST_REQUIRE(plain.preflight());
    AURORA_TEST_CHECK_TRUE(plain.layout_options().fallback_chain_view().empty());

    // 两个互异族名：顺序就是这条链的语义，构造或写读两侧反了次序在这里才露得出来。
    Harness chained(test_palette(), TerminalView::InteractionOptions{}, Typography{},
                    {"Courier New", "MS Gothic"});
    AURORA_TEST_REQUIRE(chained.preflight());
    const std::array<std::string_view, 2> expected{"Courier New", "MS Gothic"};
    AURORA_TEST_CHECK_TRUE((std::ranges::equal(chained.layout_options().fallback_chain_view(), expected)));

    // Ctrl+滚轮缩放要重取整格度量并覆写同一份排版选项的字距，链必须原地留着：只在新建那份
    // 选项时填链，缩放一次就把用户配的链抹掉了。
    chained.zoom(-2);
    AURORA_TEST_CHECK_TRUE((std::ranges::equal(chained.layout_options().fallback_chain_view(), expected)));
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(fixed_cell_advance_is_the_raw_grid_width_and_follows_the_zoom) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness tight;
    AURORA_TEST_REQUIRE(tight.preflight());
    const auto raw = tight.raw_cell_width_px();
    AURORA_TEST_REQUIRE_MSG(raw > 0, "the framework must report a positive cell width to anchor the grid");
    const auto spacing_px = static_cast<double>(raw) / 3.0;

    // 档位取**未含字距**的原始格宽：框架把 `letter_spacing` 叠加在本档位之上，取回填后的格宽
    // （= 原始 + 字距）就等于把字距算两遍，列位会以每格一个字距的速度漂走。
    Harness spread(test_palette(), TerminalView::InteractionOptions{},
                   Typography{.letter_spacing_dp = spacing_px});
    AURORA_TEST_REQUIRE(spread.preflight());
    AURORA_TEST_REQUIRE(spread.layout_options().fixed_cell_advance_px.has_value());
    AURORA_TEST_CHECK_EQ(*spread.layout_options().fixed_cell_advance_px, static_cast<float>(raw));
    AURORA_TEST_CHECK_EQ(*spread.layout_options().fixed_cell_advance_px + spread.layout_options().letter_spacing,
                         static_cast<float>(spread.geometry().cell_width * spread.scale()));

    // 缩放重取整格度量：档位必须跟上新格宽。单位写成逻辑 dp 时本断言仍可能与旧值巧合相等，
    // 故判据取「与框架同一算式在缩放后现算的那一个」逐位相等，而非某个绝对数。
    tight.zoom(3);
    const auto zoomed_raw = tight.raw_cell_width_px();
    AURORA_TEST_REQUIRE_MSG(zoomed_raw != raw, "the zoom must actually change the raw cell width");
    AURORA_TEST_CHECK_EQ(*tight.layout_options().fixed_cell_advance_px, static_cast<float>(zoomed_raw));
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

}  // namespace borealis::test_cases::itest_render_viewport
