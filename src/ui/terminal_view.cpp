// ============================================================
// 终端视口控件实现（src/ui/terminal_view.cpp）
// ------------------------------------------------------------
// 全仓唯一触达 `aurora::Painter` 绘制面的翻译单元（架构 §9.2 的收口纪律）：这里只有
// 「互转 + 按层叠顺序下调用」，判断全在纯逻辑件里（`ui::palette`、`ui::cell_layout`、
// `session::ScreenMirror`），于是最容易算错的行号与 dp 换算能留在无框架环境里全量单测。
// ============================================================

#include "terminal_view.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/core/dimension.h"
#include "aurora/environment/build_context.h"
#include "aurora/render/font_engine.h"
#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/term/keymap.h"
#include "borealis/term/utf8.h"

namespace borealis::ui {
namespace {

/// @brief 滚动内核的计量单位：1 个滚轮增量 = 1 行。存行数而非 dp，免得浮点残差让取整少算一行。
constexpr float kRowStep = 1.0F;

/// @brief 回看位置指示条的宽与最短长度（逻辑 dp，视觉稿 U1）。
constexpr double kIndicatorWidthDp = 2.0;
constexpr double kIndicatorMinLengthDp = 12.0;

/// @brief 互转点之一：`ui::RgbaColor` → 框架颜色（逐字段搬，alpha 参与混合）。
[[nodiscard]] auto to_color(const RgbaColor &color) noexcept -> aurora::Color {
    return aurora::Color{color.red, color.green, color.blue, color.alpha};
}

/// @brief 互转点之二：本层「相对绘制盒」的矩形 → 全局逻辑 dp 矩形。
///
/// `ui::cell_layout` 产出的矩形一律以绘制盒左上角为原点（含内边距），而 `Painter` 收的是
/// 全局坐标，故落笔前统一加一次 `bounds.origin`。
[[nodiscard]] auto to_rect(const Rect &rect, const aurora::Point &origin) noexcept -> aurora::Rect {
    return aurora::Rect{
        .origin = aurora::Point{.x = static_cast<float>(origin.x + rect.x),
                                .y = static_cast<float>(origin.y + rect.y)},
        .size = aurora::Size{.width = static_cast<float>(rect.width), .height = static_cast<float>(rect.height)},
    };
}

/// @brief 粗体只换字重：格宽由参考字体一次定死（架构 §9.2），族与字号不动。
[[nodiscard]] auto font_for(const aurora::Font &reference, const CellPaint &paint) -> aurora::Font {
    auto font = reference;
    font.weight = paint.bold ? 700 : reference.weight;
    return font;
}

/// @brief 一格的可绘文本：基础码点连同其组合标记拼成一段 UTF-8（叠字归框架 shaping）。
[[nodiscard]] auto cell_text(const grid::Row &row, std::size_t column) -> std::string {
    const auto &cell = row.cell(column);
    if (cell.is_wide_continuation()) {
        return {};  // 延续格不承载字符：宽度由基础格那一段占住
    }
    std::string text;
    static_cast<void>(term::append_utf8(cell.code_point, text));
    for (const auto &mark : row.combining(column)) {
        static_cast<void>(term::append_utf8(mark.code_point, text));
    }
    return text;
}

/// @brief 把浮点行号折成整数并夹到 `[0, rows - 1]`（调用方须已保证 rows > 0）。
[[nodiscard]] auto clamp_row(double value, std::size_t rows) noexcept -> std::size_t {
    if (!(value > 0.0)) {
        return 0U;
    }
    return std::min(static_cast<std::size_t>(value), rows - 1U);
}

/// @brief 互转点之三：框架按键事件 → 本层键位语义。
///
/// `term::KeySym` 的取值与 `aurora::KeyCode` 逐一对齐（对齐由单元用例逐条断言），故这里只需一次
/// `static_cast`；框架新增键位而本层未跟随时，未映射值落进编码表的「不编码」分支，不会译成别的键。
[[nodiscard]] auto to_key_press(const aurora::KeyEvent &e) -> term::KeyPress {
    return term::KeyPress{
        .sym = static_cast<term::KeySym>(e.key),
        .shift = (e.modifiers & aurora::ModifierKey::Shift) != 0U,
        .control = (e.modifiers & aurora::ModifierKey::Control) != 0U,
        .alt = (e.modifiers & aurora::ModifierKey::Alt) != 0U,
        .meta = (e.modifiers & aurora::ModifierKey::Meta) != 0U,
        .num_lock = (e.modifiers & aurora::ModifierKey::NumLock) != 0U,
    };
}

/// @brief 码点接收端：把解出的码点依次收进串里。
class StringCollector final : public term::CodePointSink {
  public:
    explicit StringCollector(std::u32string &into) : into_(&into) {}

    auto on_code_point(char32_t code_point) -> void override { into_->push_back(code_point); }

  private:
    std::u32string *into_ = nullptr;
};

/// @brief 把框架文本事件给的 UTF-8 片段解成码点流（框架按完整码点给出，故不留跨事件的挂起态）。
[[nodiscard]] auto to_code_points(std::string_view utf8) -> std::u32string {
    std::u32string out;
    out.reserve(utf8.size());  // 纯 ASCII 时即为上界
    StringCollector collector{out};
    term::Utf8Decoder decoder;
    decoder.feed(std::as_bytes(std::span<const char>{utf8.data(), utf8.size()}), collector);
    return out;
}

}  // namespace

TerminalView::TerminalView(session::Session &session, PaletteSpec palette, aurora::Font ref_font,
                           float padding_dp, std::chrono::milliseconds blink_period)
    : session_(&session),
      spec_(std::move(palette)),
      ref_font_(std::move(ref_font)),
      padding_dp_(padding_dp),
      blink_period_(blink_period) {
    // 撑满父级是控件自身的意图，写在这里以免每个装配点（含测试）都要重述一遍。
    width(aurora::fill());
    height(aurora::fill());
    scroll_viewport_.step = kRowStep;
}

TerminalView::~TerminalView() { blink_timer_.cancel(); }

auto TerminalView::on_frame() -> void {
    const std::vector<session::Damage> frame = session_->drain_damage();
    const std::size_t previous_back = mirror_.back_rows();
    session_->read(
        [this, &frame](grid::Storage &grid, const term::Cursor &cursor, const term::TermModes &modes) {
            total_lines_ = grid.total_lines();
            cursor_ = CursorState{
                .row = cursor.row,
                .column = cursor.column,
                .shape = modes.cursor_shape,
                .visible = modes.cursor_visible,
                .blinking = modes.cursor_blinking,
            };
            mirror_.apply(grid, frame, reproject(total_lines_, grid.visible_rows()));
        });
    // 回看换源与光标移动都不在提交里：前者由距底行数变化指认（`on_scroll` 在帧序里先于本函数，
    // 故同帧就能换源），后者直接比较光标快照。
    if (!frame.empty() || previous_back != mirror_.back_rows() || !(cursor_ == painted_cursor_)) {
        painted_cursor_ = cursor_;
        mark_needs_paint();
    }
}

auto TerminalView::on_mount(const aurora::BuildContext &ctx) -> void {
    (void)ctx;
    // 基类 `tick` 只在含手势的控件上被调用，空闲时不可靠；闪烁是纯周期任务，挂调度器（主线程、
    // present 之前触发，故相位翻动能赶上当帧）。
    if (auto *scheduler = aurora::Scheduler::current(); scheduler != nullptr) {
        blink_timer_ = scheduler->set_interval(blink_period_, [this]() -> void { on_blink_tick(); });
    }
}

auto TerminalView::on_layout(const aurora::Constraints &c, const aurora::BuildContext &ctx) -> aurora::Size {
    // 无限 max 是 Flex 主轴给非加权子项的「按需上限」：此刻没有可信的可视宽度，沿用上一次的盒。
    const aurora::Size box =
        c.max.is_finite() ? c.constrain(aurora::Size{.width = c.max.width, .height = c.max.height}) : size_;
    const float scale = ctx.scale_factor > 0.0F ? ctx.scale_factor : 1.0F;
    geometry_ = make_geometry(cell_metrics(scale), scale, LogicalSize{box.width, box.height}, padding_dp_);
    request_grid_size();
    return box;
}

auto TerminalView::on_paint(aurora::Painter &p, const aurora::Rect &bounds, const aurora::BuildContext &ctx)
    -> void {
    (void)ctx;
    // ①：整盒铺默认底色。部分裁剪帧里框架会先把裁剪区清回零基底，不铺这层就露出黑底——它与
    //    「本帧有没有脏」无关，每次进入绘制都要铺。
    p.fill_rect(bounds, to_color(spec_.default_background));
    const std::size_t rows = std::min(geometry_.rows, mirror_.rows());
    if (rows == 0U || geometry_.columns == 0U) {
        return;
    }
    std::size_t first = 0U;
    std::size_t last = rows - 1U;
    if (p.has_clip()) {
        // 框架给的是全量 bounds，裁剪盒只压在 `Painter` 的裁剪栈上（裁决 7.23ⓐ），故窗外行由
        // 本层自行跳过——画了也被裁掉，白付一次字形栅格。
        const aurora::Rect clip = p.clip_bounds();
        const double top = static_cast<double>(bounds.origin.y) + geometry_.padding;
        first = clamp_row(std::floor((static_cast<double>(clip.origin.y) - top) / geometry_.cell_height), rows);
        last = clamp_row(std::ceil((static_cast<double>(clip.bottom()) - top) / geometry_.cell_height) - 1.0,
                         rows);
        if (first > last) {
            return;  // 整个可见网格都在裁剪盒外（分屏里的极端裁剪）
        }
    }
    for (std::size_t screen_row = first; screen_row <= last; ++screen_row) {
        paint_row(p, bounds, screen_row);
    }
    paint_cursor(p, bounds, rows);
    paint_scroll_indicator(p, bounds, rows);
}

auto TerminalView::type_name() const -> const char * { return "TerminalView"; }

auto TerminalView::wants_focus() const -> bool { return true; }

auto TerminalView::wants_focus_ring() const -> bool { return false; }

auto TerminalView::wants_scroll() const -> bool { return true; }

auto TerminalView::can_cache_display_list() const -> bool { return false; }

auto TerminalView::can_cache_layout() const -> bool { return false; }

auto TerminalView::on_scroll(aurora::ScrollEvent &e) -> void {
    // TODO(SPEC.FEAT.TERM.06): 上报模式与备屏 alternate scroll 优先于本地回看
    const float before = scroll_viewport_.offset_y;
    scroll_viewport_.offset_y = aurora::ScrollViewport::clamp_offset(
        before, e.delta_y, kRowStep, scroll_viewport_.content_h, scroll_viewport_.viewport_h);
    // clamp 吃不尽的余量上冒给更浅的可滚动祖先（工作区分屏场景），框架的嵌套协调只认这个字段。
    e.remaining_y =
        aurora::ScrollViewport::remaining_offset(before, scroll_viewport_.offset_y, e.delta_y, kRowStep);
    e.is_handled = true;
    // 不在这里标脏：内容换源要等本帧随后的 `on_frame` 重取可见窗，它自会标脏。
}

auto TerminalView::wants_navigation_keys() const -> bool { return true; }

auto TerminalView::wants_activation_keys() const -> bool { return true; }

auto TerminalView::wants_tab_keys() const -> bool { return true; }

auto TerminalView::on_key_event(aurora::KeyEvent &e) -> void {
    if (e.action != aurora::KeyAction::Down) {
        return;  // 抬起无按键释放语义（kitty 协议属延后观察项），留着让宿主继续处理
    }
    const auto press = to_key_press(e);
    const auto bytes = term::encode_key(press, modes_snapshot());
    if (!bytes) {
        return;  // 本层不编码：可打印形态归文本通道，无遗留编码的组合归快捷键层
    }
    session_->send_bytes(std::as_bytes(std::span<const char>{bytes->data(), bytes->size()}));
    e.is_handled = true;
    // NumLock 开着的小键盘键必有紧随其后的 `WM_CHAR`（Windows 不看 `DECKPAM`），而本层已把它发成
    // SS3 数值族或导航形态——文本通道那一条是同一物理键的第二次落地，吞掉一次。
    if (term::is_keypad(press.sym) && press.num_lock) {
        swallow_next_text_ = true;
    }
}

auto TerminalView::on_text_input(aurora::TextInputEvent &e) -> void {
    // TODO(SPEC.FEAT.INTERACT.06): 输入法上屏走 `on_text_composition` 的 `committed`，本入口只覆盖
    // 直接按键产生的文本；preedit 就地绘制与候选窗定位随那一棒接。
    if (swallow_next_text_) {
        swallow_next_text_ = false;
        e.is_handled = true;  // 已由按键通道发过，此处消费但不写会话
        return;
    }
    session_->send_text(to_code_points(e.text));
    e.is_handled = true;
}

auto TerminalView::modes_snapshot() -> term::TermModes {
    term::TermModes snapshot{};
    session_->read([&snapshot](grid::Storage &, const term::Cursor &, const term::TermModes &modes) {
        snapshot = modes;
    });
    return snapshot;
}

auto TerminalView::cell_metrics(float scale) -> const CellPixels & {
    if (scale != metrics_scale_ || cell_px_.height_px <= 0) {
        const auto metrics = aurora::render::FontEngine::monospace_cell(ref_font_, scale);
        cell_px_ = CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px};
        metrics_scale_ = scale;
    }
    return cell_px_;
}

auto TerminalView::request_grid_size() -> void {
    const session::Size size{geometry_.columns, geometry_.rows};
    if (size.columns == 0U || size.rows == 0U ||
        (size.columns == requested_size_.columns && size.rows == requested_size_.rows)) {
        return;  // 0 行 0 列（窗口最小化）连下发都不该发生
    }
    requested_size_ = size;
    session_->resize(size);
}

auto TerminalView::reproject(std::size_t total_lines, std::size_t rows) -> std::size_t {
    // 裁决 D6①「距底恒定」：距底行数是用户意图，内核的 `offset_y` 只是它在当前总行数下的投影，
    // 于是贴底态随输出滚动、回看态画面整体上移而始终距底 N 行。
    const float kept_back = std::max(0.0F, scroll_viewport_.max_offset() - scroll_viewport_.offset_y);
    scroll_viewport_.content_h = static_cast<float>(total_lines);
    scroll_viewport_.viewport_h = static_cast<float>(rows);
    scroll_viewport_.offset_y = aurora::ScrollViewport::clamp_offset(
        scroll_viewport_.max_offset() - kept_back, 0.0F, kRowStep, scroll_viewport_.content_h,
        scroll_viewport_.viewport_h);
    return std::lround(scroll_viewport_.max_offset() - scroll_viewport_.offset_y);
}

auto TerminalView::on_blink_tick() -> void {
    if (!cursor_.blinking || !is_focused()) {
        return;
    }
    blink_on_ = !blink_on_;
    mark_needs_paint();
}

auto TerminalView::paint_row(aurora::Painter &p, const aurora::Rect &bounds, std::size_t screen_row) -> void {
    const auto runs = layout_row(mirror_.line(screen_row), spec_);
    std::vector<aurora::render::TextRun> batch;
    batch.reserve(runs.size());
    std::vector<aurora::render::TextRun> italic_batch;
    for (const auto &run : runs) {
        const auto band = rect_for(geometry_, screen_row, run.first_column, run.last_column);
        if (run.paint.background != spec_.default_background) {
            p.fill_rect(to_rect(band, bounds.origin), to_color(run.paint.background));  // ② 色带
        }
        if (run.text.empty()) {
            continue;  // 只有色带的一段：`layout_row` 已把全空白段的文本清空
        }
        (run.paint.italic ? italic_batch : batch)
            .push_back(aurora::render::TextRun{
                .text = run.text,
                .box = to_rect(band, bounds.origin),
                .font = font_for(ref_font_, run.paint),
                .color = to_color(run.paint.foreground),
            });
    }
    if (!batch.empty()) {
        p.draw_text_runs(batch);  // ③ 每行一次批量文本
    }
    if (!italic_batch.empty()) {
        // 批量入口的 opts 是**整批共用**（框架刻意不提供 per-run opts，以免与 `Font` 的样式语义
        // 重叠成两条矛盾来源），故斜体单独成一批——每行最多两次调用，仍拿得到批量化省下的派生量。
        auto opts = aurora::render::TextLayoutOpts{};
        opts.italic = true;
        p.draw_text_runs(italic_batch, opts);
    }
    for (const auto &run : runs) {
        for (const auto &decoration : decoration_rects(geometry_, screen_row, run)) {
            p.fill_rect(to_rect(decoration, bounds.origin), to_color(run.paint.foreground));  // ④ 装饰
        }
    }
}

auto TerminalView::paint_cursor(aurora::Painter &p, const aurora::Rect &bounds, std::size_t rows) -> void {
    if (!cursor_.visible) {
        return;
    }
    const std::size_t screen_row = cursor_.row + mirror_.back_rows();
    if (screen_row >= rows) {
        return;  // 光标被滚出可见窗：不是特例降级，就是它不在画面上
    }
    const bool focused = is_focused();
    if (focused && cursor_.blinking && !blink_on_) {
        return;  // off 相：块形不画块、字形照常（③ 已画过）
    }
    const auto &row = mirror_.line(screen_row);
    if (cursor_.column >= row.columns()) {
        return;
    }
    const auto &cell = row.cell(cursor_.column);
    const double stroke = 1.0 / geometry_.scale;  // 1 物理像素（裁决 7.28④）
    const auto box = rect_for(geometry_, screen_row, cursor_.column,
                              cursor_.column + (cell.width == 0U ? 1U : cell.width));
    const RgbaColor ink = spec_.cursor_color.value_or(spec_.default_foreground);  // 裁决 7.25③
    if (cursor_.shape != term::CursorShape::Block) {
        const Rect mark = cursor_.shape == term::CursorShape::Underline
                              ? Rect{box.x, box.y + box.height - stroke, box.width, stroke}
                              : Rect{box.x, box.y, stroke, box.height};
        p.fill_rect(to_rect(mark, bounds.origin), to_color(ink));
        return;
    }
    if (!focused) {
        // 失焦降级为空心描边，且静止（闪烁 off 相的判定已按持焦过滤）。
        const auto color = to_color(ink);
        p.fill_rect(to_rect(Rect{box.x, box.y, box.width, stroke}, bounds.origin), color);
        p.fill_rect(to_rect(Rect{box.x, box.y + box.height - stroke, box.width, stroke}, bounds.origin), color);
        p.fill_rect(to_rect(Rect{box.x, box.y, stroke, box.height}, bounds.origin), color);
        p.fill_rect(to_rect(Rect{box.x + box.width - stroke, box.y, stroke, box.height}, bounds.origin), color);
        return;
    }
    p.fill_rect(to_rect(box, bounds.origin), to_color(ink));
    // 三段式的第三段：块已盖住 ③ 的字形，改按该格合成后的**底色**作前景重画一次。不取「块在下、
    // 字在上」的次序，因为主题光标色可能与前景同色——那样叠上去字符会消失。
    const auto paint = resolve(cell, spec_);
    const std::string text = cell_text(row, cursor_.column);
    if (!paint.hidden && !text.empty()) {
        auto opts = aurora::render::TextLayoutOpts{};
        opts.italic = paint.italic;  // 与 ③ 的斜体判定同源，否则光标停在斜体格上会把那一格画成正体
        p.draw_text(to_rect(box, bounds.origin), text, font_for(ref_font_, paint), to_color(paint.background), opts);
    }
}

auto TerminalView::paint_scroll_indicator(aurora::Painter &p, const aurora::Rect &bounds, std::size_t rows)
    -> void {
    const std::size_t back = mirror_.back_rows();
    if (back == 0U || total_lines_ <= rows) {
        return;  // 贴底即正常跟随态，不必指位置
    }
    const double track_height = static_cast<double>(rows) * geometry_.cell_height;
    const double thumb_height = std::min(
        track_height, std::max(kIndicatorMinLengthDp,
                               track_height * static_cast<double>(rows) / static_cast<double>(total_lines_)));
    const double slack = static_cast<double>(total_lines_ - rows);
    const double progress = (slack - static_cast<double>(back)) / slack;  // 0 = 看到最旧，1 = 贴底
    const double thumb_top = geometry_.padding + (track_height - thumb_height) * progress;
    const Rect thumb{static_cast<double>(bounds.size.width) - kIndicatorWidthDp, thumb_top, kIndicatorWidthDp,
                     thumb_height};
    p.fill_rect(to_rect(thumb, bounds.origin), to_color(spec_.default_foreground));
}

}  // namespace borealis::ui
