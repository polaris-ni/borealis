// ============================================================
// 终端视口控件实现（src/ui/terminal_view.cpp）
// ------------------------------------------------------------
// 本文件与 `workspace_view.cpp` 是触达 `aurora::Painter` 绘制面的两个翻译单元（架构 §9.2 的收口
// 纪律）：这里只有「互转 + 按层叠顺序下调用」，判断全在纯逻辑件里（`ui::palette`、
// `ui::cell_layout`、`ui::selection`、`session::ScreenMirror`），于是最容易算错的行号与 dp 换算
// 能留在无框架环境里全量单测。
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
#include "aurora/core/log.h"
#include "aurora/environment/build_context.h"
#include "aurora/render/font_engine.h"
#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"
#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/term/keymap.h"
#include "borealis/term/utf8.h"
#include "borealis/ui/selection.h"

namespace borealis::ui {
namespace {

/// @brief 滚动内核的计量单位：1 个滚轮增量 = 1 行。存行数而非 dp，免得浮点残差让取整少算一行。
constexpr float kRowStep = 1.0F;

/// @brief Ctrl+滚轮缩放的字号界值（pt）：一档 ±1 pt。下界取 6 是让最小档仍可辨认，上界 72 是
///        `SPEC.FEAT.RENDER.02` 给的调整范围；越界即不再吃事件，回看照常冒泡。
constexpr float kMinFontSizePt = 6.0F;
constexpr float kMaxFontSizePt = 72.0F;

/// @brief 回看位置指示条的宽与最短长度（逻辑 dp，视觉稿 U1）。
constexpr double kIndicatorWidthDp = 2.0;
constexpr double kIndicatorMinLengthDp = 12.0;

/// @brief 菜单条目的最小宽度（逻辑 dp）。需求与视觉稿都没规定数值，本仓取一档能让两字标签
///        与右侧留白看起来像一列而非两个孤立按钮的宽度。
constexpr float kMenuMinWidthDp = 168.0F;

/// @brief 菜单条目的内边距：横向留得比 `Button` 缺省窄一档，让整列宽度由条目宽度而非文字决定。
constexpr aurora::EdgeInsets kMenuPadding{.left = 10.0F, .top = 5.0F, .right = 10.0F, .bottom = 5.0F};

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

/// @brief 格子坐标的阅读序比较（先行后列）：流式选区的首尾就是按这个序定的（`ui::row_spans`）。
[[nodiscard]] auto before(const GridCellPos &a, const GridCellPos &b) noexcept -> bool {
    return a.row < b.row || (a.row == b.row && a.column < b.column);
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

/// @brief 菜单条目的显示文案。
///
/// CJK-LITERAL: 上屏文案 - 右键菜单是给用户看的界面文案，本仓尚无本地化层（词条表随
/// `SPEC.FEAT.PREF.02` 的设置面板那一棒再谈），故与视觉稿 F 面板一致用中文而非英文。
[[nodiscard]] auto menu_label(MenuCommand command) -> std::string_view {
    switch (command) {
        case MenuCommand::Copy:
            return "复制";
        case MenuCommand::Paste:
            return "粘贴";
    }
    return {};  // 枚举已穷尽：新增命令而忘跟上文案时，空标签比静默复用上一条更醒目
}

}  // namespace

TerminalView::TerminalView(session::Session &session, PaletteSpec palette, aurora::Font ref_font,
                           Typography typography, float padding_dp, std::chrono::milliseconds blink_period,
                           InteractionOptions options, std::vector<std::string> font_fallback_chain)
    : session_(&session),
      spec_(std::move(palette)),
      ref_font_(std::move(ref_font)),
      typography_(typography),
      padding_dp_(padding_dp),
      blink_period_(blink_period),
      options_(std::move(options)),
      layout_opts_(aurora::render::TextLayoutOpts::with_fallback_chain(font_fallback_chain)) {
    // 撑满父级是控件自身的意图，写在这里以免每个装配点（含测试）都要重述一遍。
    width(aurora::fill());
    height(aurora::fill());
    scroll_viewport_.step = kRowStep;
    if (font_fallback_chain.size() > aurora::render::AURORA_TEXT_FALLBACK_CHAIN_MAX) {
        // 截断发生在框架的构造入口里（保留前 N 项、顺序不变），对用户是静默的，故留痕一次；
        // 上限不写在本仓的常量里：链的承载形态属框架，抄一个数过来就是第二个真值源。
        AURORA_LOG_WARN("ui", "font fallback chain truncated: requested ", font_fallback_chain.size(),
                        " entries, using the first ", aurora::render::AURORA_TEXT_FALLBACK_CHAIN_MAX);
    }
}

TerminalView::~TerminalView() {
    blink_timer_.cancel();
    // 在途的粘贴块回调都捕获了 `this`；句柄只置取消标志，故调度器触发前会跳过它们。
    for (const aurora::TimerHandle &handle : paste_timers_) {
        handle.cancel();
    }
}

auto TerminalView::set_overlay_host(aurora::OverlayHost &host) -> void { host_ = &host; }

auto TerminalView::context_menu() const noexcept -> const aurora::Popup * { return menu_.get(); }

auto TerminalView::set_presentation(Presentation presentation) -> void {
    presentation_ = std::move(presentation);
}

auto TerminalView::set_grid_size_sink(GridSizeSink sink) -> void { grid_size_sink_ = std::move(sink); }

auto TerminalView::set_key_pre_filter(KeyPreFilter filter) -> void { key_pre_filter_ = std::move(filter); }

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
    // 选区的端点是存储行序，而存储顶边会随输出溢出、随 resize 与 `clear()` 位移：折算必须发生在
    // 副本并入之后（读数由副本带出），否则高亮与复制文本会跟着旧的行号指着别的内容（裁决 7.38⑤）。
    compensate_selection_drift();
    flush_copy_request();
    flush_paste_request();
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
    geometry_ = make_geometry(cell_metrics(scale).cells, scale, LogicalSize{box.width, box.height}, padding_dp_);
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
    // Ctrl+滚轮是字号缩放档（`SPEC.FEAT.RENDER.02`）：修饰态由后端在事件产生处盖章、派发器只透传，
    // 本层据此分流而不轮询物理按键态（G18 回货字段在选区之外的第二个消费点）。
    if ((e.modifiers & aurora::ModifierKey::Control) != 0U) {
        if (zoom_font_size(e.delta_y)) {
            e.remaining_y = 0.0F;  // 缩放吃掉这一档，不再冒泡给回看与祖先
            e.is_handled = true;
            return;
        }
    }
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
    // 工作区层的键位先于本层判定（裁决 7.47②：不经框架快捷键层，而在焦点 pane 的按键入口前置
    // 过滤）。未认领即照原路编码发会话——裸方向键与 `Ctrl+方向键` 恒归会话（`SPEC.FEAT.INTERACT.01`）。
    if (key_pre_filter_ && key_pre_filter_(press)) {
        e.is_handled = true;
        return;
    }
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

auto TerminalView::on_pointer_event(aurora::MouseEvent &e) -> void {
    // 菜单开着时，任何按下都先问宿主：落在菜单外即收下这一击并关菜单，不再让它去推进选区或
    // 触发第二个动作（`OverlayHost::handle_outside_click` 只由调用方在事件入口驱动，框架不代劳）。
    if (e.action == aurora::MouseAction::Press && host_ != nullptr && host_->handle_outside_click(e.position)) {
        e.is_handled = true;
        return;
    }
    if (e.button != aurora::MouseButton::Left) {
        if (e.button == aurora::MouseButton::Right) {
            on_right_click(e);
        }
        return;  // 中键粘贴不在首版交付面
    }
    if (mirror_.rows() == 0U || geometry_.columns == 0U) {
        return;  // 字体未就绪或窗口最小化：没有格子可选
    }
    const auto hit = cell_at_point(geometry_, e.local_position.x, e.local_position.y);
    if (!hit) {
        return;
    }
    // 落点是绘制行号，选区存的是存储行序（`ui/selection.h` 的坐标约定），换算量只有副本的窗顶。
    const GridCellPos cell{mirror_.window_top() + hit->row, hit->column};
    switch (e.action) {
        case aurora::MouseAction::Press: {
            dragging_ = true;
            // 连击序号由派发器集中判定并盖章（后端不参与），上限 3（裁决 7.29②）。
            drag_mode_ = e.click_count >= 3U   ? DragMode::Line
                         : e.click_count == 2U ? DragMode::Word
                                               : DragMode::Cell;
            pressed_cell_ = cell;
            extend_selection(cell, e.modifiers);  // 单击即两端重合，旧选区就此清空
            e.is_handled = true;
            break;
        }
        case aurora::MouseAction::Move: {
            if (!dragging_) {
                return;  // 悬停移动不改选区
            }
            extend_selection(cell, e.modifiers);
            e.is_handled = true;
            break;
        }
        case aurora::MouseAction::Release: {
            if (!dragging_) {
                return;
            }
            dragging_ = false;
            extend_selection(cell, e.modifiers);
            // copy-on-select 只在区间非空时触发（裁决 7.38⑥）；写剪贴板是 IO，落在下一帧的
            // `on_frame` 而不是本回调（AGENTS.md §4.5 第 25 条）。
            if (options_.copy_on_select && selection_.has_value()) {
                copy_pending_ = true;
            }
            e.is_handled = true;
            break;
        }
    }
}

auto TerminalView::selected_text() -> std::string {
    if (!selection_.has_value()) {
        return {};
    }
    const Selection selection = *selection_;
    const CopyOptions options = options_.copy;
    std::string text;
    // 取行须在会话锁内：可见区副本只有视口那几行，而选区可以横跨 scrollback（裁决 7.40）。
    session_->read([&text, &selection, &options](grid::Storage &grid, const term::Cursor &,
                                                const term::TermModes &) {
        text = copy_text(grid, selection, options);
    });
    return text;
}

auto TerminalView::modes_snapshot() -> term::TermModes {
    term::TermModes snapshot{};
    session_->read([&snapshot](grid::Storage &, const term::Cursor &, const term::TermModes &modes) {
        snapshot = modes;
    });
    return snapshot;
}

auto TerminalView::cell_metrics(float scale) -> const TypedMetrics & {
    if (metrics_stale_ || scale != metrics_scale_ || typed_.cells.height_px <= 0) {
        const auto metrics = aurora::render::FontEngine::monospace_cell(ref_font_, scale);
        typed_ = apply_typography(CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px},
                                  static_cast<double>(scale), typography_);
        // 量化字距、回退链与固定格推进同处一份选项：绘制侧两处落笔都必须取回填值而非配置原值（裁决 7.46①）。
        layout_opts_.letter_spacing = static_cast<float>(typed_.letter_spacing_dp);
        // 固定格推进取**未含字距**的原始格宽（物理 px 且已含 scale，与 `monospace_cell` 同源）：
        // 框架把 `letter_spacing` 叠加在本档位之上，取回填后的 `typed_.cells.width_px` 会把字距算两遍。
        layout_opts_.fixed_cell_advance_px = static_cast<float>(metrics.cell_width_px);
        metrics_scale_ = scale;
        metrics_stale_ = false;
    }
    return typed_;
}

auto TerminalView::glyph_top_dp() const noexcept -> double {
    // 排版关闭时恒 0，故文本盒与叠上排版量之前逐位相同；换算成 dp 是因为 `Painter` 收逻辑坐标。
    return geometry_.scale > 0.0 ? static_cast<double>(typed_.glyph_top_px) / geometry_.scale : 0.0;
}

auto TerminalView::zoom_font_size(float delta_rows) -> bool {
    const float step = delta_rows > 0.0F ? 1.0F : -1.0F;
    const float clamped = std::clamp(ref_font_.size_pt + step, kMinFontSizePt, kMaxFontSizePt);
    if (clamped == ref_font_.size_pt) {
        return false;  // 已在界值：这一档让回去看，缩放不该在边界吞掉滚轮
    }
    ref_font_.size_pt = clamped;
    // 字号是运行期唯一改 `ref_font_` 的地方，而它一改，格宽、行列数与下发的会话尺寸全都跟着变
    // （`SPEC.FEAT.RENDER.02` 的字号调整与 `RENDER.05` 的「缩放变更后重算且不裂」是同一条判据）。
    metrics_stale_ = true;
    mark_needs_layout();
    mark_needs_paint();
    return true;
}

auto TerminalView::request_grid_size() -> void {
    const GridSize size{geometry_.columns, geometry_.rows};
    if (size.columns == 0U || size.rows == 0U ||
        (size.columns == requested_size_.columns && size.rows == requested_size_.rows)) {
        return;  // 0 行 0 列（窗口最小化）连下发都不该发生
    }
    requested_size_ = session::Size{size.columns, size.rows};
    if (grid_size_sink_) {
        grid_size_sink_(size);  // 静默窗口与下发都归工作区层（裁决 7.47⑩）
        return;
    }
    session_->resize(requested_size_);
}

auto TerminalView::scrollback_rows_from_bottom() const -> std::size_t {
    return static_cast<std::size_t>(
        std::lround(std::max(0.0F, scroll_viewport_.max_offset() - scroll_viewport_.offset_y)));
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
    const auto &row = mirror_.line(screen_row);
    const auto selected = band_at(mirror_.window_top() + screen_row);
    // 有选中格的那一行走四参形态：底色替换发生在 `resolve` 之后，选中段因此自成一跑，而色带、
    // 文本与装饰都跟着这张 run 表走（裁决 7.38① D1①，视觉稿 A1-c 的「字形不被染色」）。
    const auto runs =
        selected ? layout_row(row, spec_, *selected, selection_ink()) : layout_row(row, spec_);
    std::vector<aurora::render::TextRun> batch;
    batch.reserve(runs.size());
    std::vector<aurora::render::TextRun> italic_batch;
    // 文本盒比色带盒低一个「上半 leading」，且字距取 `apply_typography` 回填的量化值而非配置原值：
    // 后者保证相邻字形的间距落在整数物理像素上，列步长与字形推进因此同源（裁决 7.46①）。
    const double glyph_top = glyph_top_dp();
    auto opts = layout_opts_;
    for (const auto &run : runs) {
        const auto band = rect_for(geometry_, screen_row, run.first_column, run.last_column);
        if (run.paint.background != spec_.default_background) {
            p.fill_rect(to_rect(band, bounds.origin), to_color(run.paint.background));  // ② 色带
        }
        if (run.text.empty()) {
            continue;  // 只有色带的一段：`layout_row` 已把全空白段的文本清空
        }
        const Rect text_box{band.x, band.y + glyph_top, band.width, band.height};
        (run.paint.italic ? italic_batch : batch)
            .push_back(aurora::render::TextRun{
                .text = run.text,
                .box = to_rect(text_box, bounds.origin),
                .font = font_for(ref_font_, run.paint),
                .color = to_color(run.paint.foreground),
            });
    }
    if (!batch.empty()) {
        p.draw_text_runs(batch, opts);  // ③ 每行一次批量文本
    }
    if (!italic_batch.empty()) {
        // 批量入口的 opts 是**整批共用**（框架刻意不提供 per-run opts，以免与 `Font` 的样式语义
        // 重叠成两条矛盾来源），故斜体单独成一批——每行最多两次调用，仍拿得到批量化省下的派生量。
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
    auto paint = resolve(cell, spec_);
    if (const auto selected = band_at(mirror_.window_top() + screen_row);
        selected && cursor_.column >= selected->first_column && cursor_.column < selected->last_column) {
        // 视觉稿 C2-a：光标层压在选区层之上，块形照旧，但第三段取的是「该格合成底色」——此刻它
        // 就是选区色。漏这一步会让被选中的光标格用未选中底色重画字形，与同行其余选中格不一致。
        paint.background = selection_ink();
    }
    const std::string text = cell_text(row, cursor_.column);
    if (!paint.hidden && !text.empty()) {
        auto opts = layout_opts_;
        opts.italic = paint.italic;  // 与 ③ 的斜体判定同源，否则光标停在斜体格上会把那一格画成正体
        // 块形的外沿是格子矩形，字却按 `paint_row` 的同一个偏移盒重画：两者共用盒会让行高调大后
        // 第三段的字浮在块上半，块内下沿空出一条带。
        const Rect text_box{box.x, box.y + glyph_top_dp(), box.width, box.height};
        p.draw_text(to_rect(text_box, bounds.origin), text, font_for(ref_font_, paint), to_color(paint.background),
                    opts);
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

auto TerminalView::mirror_line_of(std::size_t storage_row) const noexcept -> const grid::Row * {
    const std::size_t top = mirror_.window_top();
    if (storage_row < top || storage_row - top >= mirror_.rows()) {
        return nullptr;  // 已滚出可见窗：既画不到也不该按词判定
    }
    return &mirror_.line(storage_row - top);
}

auto TerminalView::span_of(const GridCellPos &storage_cell, DragMode mode) const -> std::optional<RowSpan> {
    const std::size_t columns = mirror_.columns();
    if (columns == 0U) {
        return std::nullopt;
    }
    if (mode == DragMode::Line) {
        // 三击取整行含行尾空白（裁决 7.38① D5② 同口径的 ④），右界是列数而非最后一个有字符的列。
        return RowSpan{.row = storage_cell.row, .first_column = 0U, .last_column = columns};
    }
    if (mode == DragMode::Cell) {
        return RowSpan{.row = storage_cell.row,
                       .first_column = storage_cell.column,
                       .last_column = storage_cell.column + 1U};
    }
    const auto *row = mirror_line_of(storage_cell.row);
    if (row == nullptr) {
        return std::nullopt;
    }
    // 落在空格、制表或界定符上回空，且不扩到相邻字（裁决 7.39④）。
    return word_span_at(*row, storage_cell, options_.word_delimiters);
}

auto TerminalView::extend_selection(const GridCellPos &current_storage, aurora::ModifierKey modifiers) -> void {
    const auto single = [](const GridCellPos &cell) -> RowSpan {
        return RowSpan{.row = cell.row, .first_column = cell.column, .last_column = cell.column + 1U};
    };
    // 取不到区间（Word 粒度落在断点上、或行已滚出可见窗）时退化为「那一格」：按下端退化保住
    // 「双击断点不成选区」（两端重合），拖拽端退化保住「从断点拖出去仍能扩」（裁决 7.39④）。
    const RowSpan from = span_of(pressed_cell_, drag_mode_).value_or(single(pressed_cell_));
    const RowSpan to = span_of(current_storage, drag_mode_).value_or(single(current_storage));
    // 折成阅读序的外沿：`row_spans` 只认两个端点格，直接拿按下格与当前格会让按词/按行的向后拖
    // 只选到起始行的第一列（裁决 7.40）。
    GridCellPos start{from.row, from.first_column};
    GridCellPos end{to.row, to.last_column - 1U};
    if (before(end, start)) {
        start = {to.row, to.first_column};
        end = {from.row, from.last_column - 1U};
    }
    const auto shape = (modifiers & aurora::ModifierKey::Alt) != 0U ? SelectionShape::Block : SelectionShape::Stream;
    set_selection(Selection{.anchor = start, .focus = end, .shape = shape});
}

auto TerminalView::set_selection(const Selection &selection) -> void {
    selection_ = selection;
    refresh_selection_bands();
    mark_needs_paint();  // 无论成不成都要重画：旧选区的像素得被清掉
}

auto TerminalView::refresh_selection_bands() -> void {
    if (!selection_.has_value()) {
        bands_.clear();
        return;
    }
    bands_ = row_spans(*selection_, mirror_.columns());
    if (bands_.empty()) {
        selection_.reset();  // 两端重合（单击）或整段被推出顶端：无区间即无选区（裁决 7.39②）
    }
}

auto TerminalView::band_at(std::size_t storage_row) const -> std::optional<RowSpan> {
    const auto it = std::lower_bound(
        bands_.begin(), bands_.end(), storage_row,
        [](const RowSpan &band, std::size_t row) { return band.row < row; });
    if (it == bands_.end() || it->row != storage_row) {
        return std::nullopt;
    }
    return *it;
}

auto TerminalView::selection_ink() const noexcept -> RgbaColor {
    const auto ink = selection_color(spec_);
    return is_focused() ? ink : mix_half(ink, spec_.default_background);  // 裁决 7.38① D3①
}

auto TerminalView::compensate_selection_drift() -> void {
    const std::int64_t dropped = mirror_.dropped_lines();
    const std::int64_t rows_up = dropped - dropped_baseline_;
    dropped_baseline_ = dropped;
    if (selection_.has_value() && rows_up != 0) {
        selection_ = translate_selection_rows(*selection_, rows_up);
    }
    refresh_selection_bands();  // 列数也可能随 resize 变，首行右界取的是列数
}

auto TerminalView::flush_copy_request() -> void {
    if (!copy_pending_) {
        return;
    }
    copy_pending_ = false;
    const std::string text = selected_text();
    if (text.empty()) {
        return;
    }
    if (presentation_.clipboard_write) {
        presentation_.clipboard_write(text);
        return;
    }
    session::ClipboardOutbox::write(text);
}

auto TerminalView::open_context_menu(aurora::Point where, const std::vector<MenuItem> &items) -> void {
    if (host_ == nullptr) {
        return;  // 没有浮层宿主就发不出菜单：装配层须把场景根换成 `au::OverlayHost`（裁决 7.41③）
    }
    const aurora::Color panel = to_color(spec_.default_background);
    // 置灰档取「前景向底色各半」，与失焦选区同一条降级思路（裁决 7.38① D3①）：框架的缺省灰
    // {130,130,130} 在深色主题下几乎比正文还亮，灰显反而成了强调。
    const aurora::Color dim = to_color(mix_half(spec_.default_foreground, spec_.default_background));
    std::vector<aurora::Node> rows;
    rows.reserve(items.size());
    for (const auto &item : items) {
        auto props = aurora::ButtonProps{};
        props.label = std::string{menu_label(item.command)};
        props.color = panel;
        props.on_color = to_color(spec_.default_foreground);
        props.font = ref_font_;
        props.padding = kMenuPadding;
        props.corner_radius = 0.0F;  // 相邻条目要拼成一块整板，圆角会在接缝处露出底色
        props.min_width = kMenuMinWidthDp;
        props.enabled = item.enabled;
        props.disabled_color = panel;
        props.disabled_text_color = dim;
        auto button = std::make_shared<aurora::Button>(std::move(props));
        if (item.enabled) {
            button->set_on_click([this, command = item.command]() -> void { on_menu_command(command); });
        }
        rows.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(button))});
    }
    if (!menu_) {
        menu_ = std::make_shared<aurora::Popup>();
        static_cast<void>(host_->add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(menu_)}));
    }
    menu_->set_content(aurora::Node{std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(rows),
        .gap = 0.0F,
    })});
    menu_->open_at(where);
}

auto TerminalView::on_menu_command(MenuCommand command) -> void {
    if (menu_) {
        menu_->close();
    }
    switch (command) {
        case MenuCommand::Copy:
            copy_pending_ = true;
            break;
        case MenuCommand::Paste:
            paste_pending_ = true;
            break;
    }
    // 菜单关掉本身就是画面变化（Popup::close 已标脏），但复制/粘贴要落地的那一帧仍由 `on_frame`
    // 承担：回调里读剪贴板会把一次剪贴板抖动压在输入链上（AGENTS.md §4.5 第 25 条）。
    mark_needs_paint();
}

auto TerminalView::flush_paste_request() -> void {
    if (!paste_pending_) {
        return;
    }
    paste_pending_ = false;
    const std::string utf8 =
        presentation_.clipboard_read ? presentation_.clipboard_read() : session::ClipboardOutbox::read();
    if (utf8.empty()) {
        return;  // 剪贴板没有文本：什么都不发，也不警告
    }
    const bool bracketed = modes_snapshot().bracketed_paste;
    const term::PastePlan plan = term::plan_paste(to_code_points(utf8), bracketed, options_.paste);
    if (!plan.multiline) {
        send_paste_plan(plan);
        return;
    }
    // 多行要先确认：判据是待发结果里仍有行尾（`term::plan_paste` 已按换行策略折过），
    // 于是 `Filter` 粘成一行时不会弹警告，而 `?2004` 激活时一定会弹——那是 shell 明令保留的判断。
    pending_paste_ = plan;
    auto answer = [this](bool accepted) -> void {
        if (!pending_paste_.has_value()) {
            return;  // 已经答过一次（或被别处作废），重复放行不再发第二遍
        }
        const term::PastePlan held = *pending_paste_;
        pending_paste_.reset();
        if (accepted) {
            send_paste_plan(held);
        }
    };
    if (presentation_.confirm_multiline) {
        presentation_.confirm_multiline(plan, std::move(answer));
        return;
    }
    ask_multiline_warning(plan, std::move(answer));
}

auto TerminalView::ask_multiline_warning(const term::PastePlan &plan, std::function<void(bool)> answer) -> void {
    if (host_ == nullptr) {
        pending_paste_.reset();  // 没有宿主就问不了，未经确认不替用户按下回车
        return;
    }
    if (!multiline_warning_) {
        multiline_warning_ = std::make_shared<aurora::Dialog>();
        static_cast<void>(
            host_->add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(multiline_warning_)}));
    }
    // 行数取待发块数而不是原文换行数：警告要说的是「会有几行被当成命令执行」。
    const std::string message =
        std::string{"剪贴板里的内容有 "} + std::to_string(plan.chunks.size()) +
        " 行，粘贴会把每一行都当成一条命令依次执行。确定粘贴吗？";  // CJK-LITERAL: 上屏文案 - 同 `menu_label`
    multiline_warning_->set_content(aurora::confirm(
        "多行粘贴",  // CJK-LITERAL: 上屏文案 - 同 `menu_label`
        message,
        [this, answer = std::move(answer)](bool accepted) -> void {
            multiline_warning_->close();
            answer(accepted);
        }));
    multiline_warning_->show();
    mark_needs_paint();
}

auto TerminalView::send_paste_plan(const term::PastePlan &plan) -> void {
    std::erase_if(paste_timers_, [](const aurora::TimerHandle &handle) -> bool { return !handle.active(); });
    aurora::Scheduler *scheduler = aurora::Scheduler::current();
    if (scheduler == nullptr) {
        // 没有运行中的调度器（用例里的无头帧）：节流无处排期，按次序一次发完，块边界与内容不变。
        for (const auto &chunk : plan.chunks) {
            session_->send_text(chunk.text);
        }
        return;
    }
    std::chrono::milliseconds elapsed{};
    for (const auto &chunk : plan.chunks) {
        elapsed += chunk.delay;  // 计划里的延迟是「相对上一块」，排期取累积值
        const std::u32string text = chunk.text;
        if (elapsed <= std::chrono::milliseconds::zero()) {
            session_->send_text(text);
            continue;
        }
        paste_timers_.push_back(scheduler->set_timeout(elapsed, [this, text]() -> void { session_->send_text(text); }));
    }
}

auto TerminalView::on_right_click(aurora::MouseEvent &e) -> void {
    if (e.action != aurora::MouseAction::Press) {
        return;  // 抬起不再处置一次：三态的动作都在按下那一刻定
    }
    // 有没有选区看的是区间表而不是端点是否重合：两端重合即空表（`ui::row_spans` 的口径），
    // 而区间表正是绘制与复制共用的那一张（裁决 7.40①）。
    const RightClickPlan plan = plan_right_click(options_.right_click, !bands_.empty());
    switch (plan.intent) {
        case RightClickIntent::None:
            return;  // 交还给宿主：右键本就没有可复制的东西
        case RightClickIntent::Copy:
            copy_pending_ = true;
            break;
        case RightClickIntent::Paste:
            paste_pending_ = true;
            break;
        case RightClickIntent::Menu:
            open_context_menu(e.position, plan.items);
            break;
    }
    e.is_handled = true;
    // 直接动作两态都不改画面内容，故本帧由这里点名（裁决 7.40⑥ 的「IO 落在帧边界」要有帧才落得下）。
    mark_needs_paint();
}

}  // namespace borealis::ui
