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
#include "search_overlay.h"
#include "settings_i18n.h"
#include "settings_panel.h"

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

/// @brief dead-session 浮层的几何（`SPEC.FEAT.WS.05`，逻辑 dp）。
///
/// 卡片宽度按视口宽度取「min(320, box − 24)」——固定 320 在最小合法 pane（228 dp，`UI_SEARCH.draft`
/// 那条边界）上就伸出可视区，`min` 保证「窄 pane 里按钮还在画面里」。按钮尺寸取自与搜索浮层 chip
/// 同一档，让面板打开时两处的可点感是同一档。
constexpr double kRestartCardWidthDp = 320.0;
constexpr double kRestartCardHeightDp = 88.0;
constexpr double kRestartCardHorizontalInsetDp = 12.0;
constexpr double kRestartButtonWidthDp = 96.0;
constexpr double kRestartButtonHeightDp = 32.0;
constexpr double kRestartButtonGapDp = 8.0;       ///< 提示行与按钮行之间的纵向留白。
constexpr double kRestartCardPaddingDp = 14.0;    ///< 卡片文字距卡片边的留白。
constexpr double kRestartHintLineHeightDp = 20.0; ///< 提示行行高，配合按钮档使卡片总高 == 88 dp。
constexpr double kRestartStrokeDp = 1.0;          ///< 卡片描边宽（与搜索浮层条体同档）。

/// @brief 重连中态的卡片高（`SPEC.FEAT.WS.05` 的 SSH 腿，裁决 7.99 D5）：多一行倒计时
///        （14 + 20 × 2 + 8 + 32 + 14）。草图给的 380 × 132 卡片按既有 320 dp 上限收拢——
///        固定 380 在最小合法 pane 上就伸出可视区，而宽了并不会让文案少截一次。
constexpr double kReconnectCardHeightDp = 108.0;
/// @brief 终态那枚按钮的宽：标签「重启（按档案）」是七个全角位，96 dp 档装不下（14 pt 下约 131 dp）。
constexpr double kWideRestartButtonWidthDp = 150.0;

/// @brief 发送侧一次性提示卡的几何（`SPEC.FEAT.TERM.09` 的提示腿，裁决 7.104 的 D2①）：卡宽、横向
///        内缩、描边、留白与行高**全部沿用上面那一档**，本卡只有两行文字，故总高
///        14 + 20 × 2 + 14 == 68 dp。贴视口底、纵向内缩取同一枚 12 dp。
constexpr double kNoticeCardHeightDp = 68.0;
/// @brief 卡片常驻时长（裁决 7.104 的 D3①）：5 s 后自收，此外只有会话退出清卡。
///        明确不是「任意键入即刻收」——触发者就是键入，下一个字符即抹掉＝几乎不可见。
constexpr std::chrono::milliseconds kNoticeVisibleFor{5000};

/// @brief 把滚轮增量折成「档」：一 notch 即一档（Win32 与 X11 给 ±1，高分屏与 GLFW 可给小数）。
///        非零增量至少算一档——发零条上报等于吞掉这一事件而不告诉任何人。
[[nodiscard]] auto wheel_notches(float delta_rows) -> int {
    const long rounded = std::lround(delta_rows);
    if (rounded != 0L) {
        return static_cast<int>(rounded);
    }
    return delta_rows > 0.0F ? 1 : (delta_rows < 0.0F ? -1 : 0);
}

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

TerminalView::TerminalView(session::Session &session, Appearance appearance, InteractionOptions options)
    : session_(&session),
      spec_(std::move(appearance.palette)),
      ref_font_(std::move(appearance.ref_font)),
      typography_(appearance.typography),
      padding_dp_(appearance.padding_dp),
      blink_period_(appearance.blink_period),
      options_(std::move(options)) {
    // 撑满父级是控件自身的意图，写在这里以免每个装配点（含测试）都要重述一遍。
    width(aurora::fill());
    height(aurora::fill());
    scroll_viewport_.step = kRowStep;
    install_fallback_chain(std::move(appearance.font_fallback_chain));
}

auto TerminalView::install_fallback_chain(std::vector<std::string> chain) -> void {
    layout_opts_ = aurora::render::TextLayoutOpts::with_fallback_chain(chain);
    if (chain.size() > aurora::render::AURORA_TEXT_FALLBACK_CHAIN_MAX) {
        // 截断发生在框架的构造入口里（保留前 N 项、顺序不变），对用户是静默的，故留痕一次；
        // 上限不写在本仓的常量里：链的承载形态属框架，抄一个数过来就是第二个真值源。
        AURORA_LOG_WARN("ui", "font fallback chain truncated: requested ", chain.size(),
                        " entries, using the first ", aurora::render::AURORA_TEXT_FALLBACK_CHAIN_MAX);
    }
}

TerminalView::~TerminalView() {
    blink_timer_.cancel();
    notice_timer_.cancel();  // 那条 5 s 自收的回调捕获了 `this`。
    // 在途的粘贴块回调都捕获了 `this`；句柄只置取消标志，故调度器触发前会跳过它们。
    for (const aurora::TimerHandle &handle : paste_timers_) {
        handle.cancel();
    }
}

TerminalView::TerminalView(session::Session &session, PaletteSpec palette, aurora::Font ref_font,
                           Typography typography, float padding_dp, std::chrono::milliseconds blink_period,
                           InteractionOptions options, std::vector<std::string> font_fallback_chain)
    : TerminalView(session,
                   Appearance{.palette = std::move(palette),
                              .ref_font = std::move(ref_font),
                              .typography = typography,
                              .padding_dp = padding_dp,
                              .blink_period = blink_period,
                              .font_fallback_chain = std::move(font_fallback_chain)},
                   std::move(options)) {}

auto TerminalView::apply_appearance(Appearance appearance) -> void {
    // 只有闪烁周期要单独问：其余五项都靠下一次取度量与下一次绘制自然跟上，而周期是注册进调度器
    // 的既有句柄，不改它就没有任何动作可做。
    const bool blink_changed = appearance.blink_period != blink_period_;
    spec_ = std::move(appearance.palette);
    ref_font_ = std::move(appearance.ref_font);
    typography_ = appearance.typography;
    padding_dp_ = appearance.padding_dp;
    blink_period_ = appearance.blink_period;
    // 链没有 per-field 的 setter，装上即把整份排版选项连量化字距与固定格推进一起换掉，故必须
    // 紧接重取度量——三者由 `cell_metrics` 在同一处写回同一份 `layout_opts_`（裁决 7.50 的同源不变量）。
    install_fallback_chain(std::move(appearance.font_fallback_chain));
    metrics_stale_ = true;
    if (blink_changed) {
        reregister_blink_timer();
    }
    // 内边距与字号改的是行列数：这里只标脏，让下一次 `on_layout` 走既有的 `request_grid_size`
    // （去抖在工作区层，裁决 7.47⑩），在此直发会把一个中间值塞进会话并绕开去抖。
    mark_needs_layout();
    mark_needs_paint();
}

auto TerminalView::apply_interaction_options(InteractionOptions options) -> void {
    options_ = std::move(options);
}

auto TerminalView::set_overlay_host(aurora::OverlayHost &host) -> void { host_ = &host; }

auto TerminalView::set_search_dependencies(aurora::ShortcutRegistry &shortcuts, aurora::FocusManager &focus)
    -> void {
    shortcuts_ = &shortcuts;
    focus_ = &focus;
}

auto TerminalView::context_menu() const noexcept -> const aurora::Popup * { return menu_.get(); }

auto TerminalView::multiline_warning() const noexcept -> const aurora::Dialog * { return multiline_warning_.get(); }

auto TerminalView::set_presentation(Presentation presentation) -> void {
    presentation_ = std::move(presentation);
}

auto TerminalView::set_grid_size_sink(GridSizeSink sink) -> void { grid_size_sink_ = std::move(sink); }

auto TerminalView::set_key_pre_filter(KeyPreFilter filter) -> void { key_pre_filter_ = std::move(filter); }

auto TerminalView::set_restart_hook(std::function<void()> hook) -> void {
    restart_hook_ = std::move(hook);
    // 装/拆钩子都是一次可观测的绘制变更：装钩那一刻若会话已退（装配顺序不保证，重启闭包可能晚于
    // `on_closed`），浮层要立刻显形；拆钩那一刻反之要立刻消失。两种情形都不问 alive()，一次无条件
    // 标脏，让下一帧重新判一次——条件判断留在绘制侧，接缝只负责把「值得重看一眼」传出去。
    mark_needs_paint();
}

auto TerminalView::session_dead() const -> bool { return !session_->alive(); }

auto TerminalView::set_search_query(SearchQuery query) -> void {
    if (query == search_query_) {
        return;
    }
    search_query_ = std::move(query);
    // 字面量档每键置脏、每帧至多扫一次（结算在 `on_frame`）；正则档只在 `submit_search_query()` 置脏。
    // 空文本是唯一的例外：两档都要在这一帧把高亮清掉，而清表本身不算一次扫描。
    if (!search_query_.regex || search_query_.text.empty()) {
        search_dirty_ = true;
    }
}

auto TerminalView::submit_search_query() -> void { search_dirty_ = true; }

auto TerminalView::advance_search(SearchDirection direction) -> void {
    // 没有表就没有「下一个」：空输入与零匹配两档由浮层按 B0 / B2 显示，本入口不另发一次滚动。
    if (!search_matches_.has_value()) {
        return;
    }
    const auto landed = search_matches_->advance(direction);
    if (!landed.has_value()) {
        return;
    }
    scroll_row_into_view(landed->row);
    mark_needs_paint();
}

auto TerminalView::close_search() -> void {
    search_matches_.reset();
    search_invalid_ = false;
    search_dirty_ = false;
    // 已扫条件记回空而不是记回当前文本：F1-c 拍的是「重开保留文本但不自动重扫」，而「此刻画面上的
    // 高亮不属于输入框里那串」必须有一句话可说——于是重开那一帧 `search_pending_submit()` 为真，
    // 浮层显示 B5 而不是上一份结果的计数。查询文本本身留着（判据 F1-c 的前半句）。
    search_scanned_ = SearchQuery{};
    mark_needs_paint();  // 判据 D7：可见区每行都可能带着高亮，关闭是一次全帧重画
}

auto TerminalView::open_search() -> void {
    if (host_ == nullptr || shortcuts_ == nullptr || focus_ == nullptr) {
        return;
    }
    // 懒建一次、之后常驻：重开时逐字读回查询文本要求条体与输入框不被销毁（判据 F1-c）。
    if (search_overlay_ == nullptr) {
        search_overlay_ = std::make_unique<SearchOverlay>(*this, *host_, *shortcuts_, *focus_);
    }
    search_overlay_->open();
}

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
            // 扫描的调用点在锁内、且在副本并入之后：`ui::search` 吃的是权威网格，出锁就没有第二个一致
            // 快照可用；位移基准的读数又由副本带出，故两者必须在这一次临界区里前后相接（裁决 7.78⑥）。
            // 「前后相接」在重扫与新输出同帧时是承重的：取早一格就会把同一次位移既算进扫描结果、
            // 又算进折算增量（其证人是 `itest_search_scan` 的第九例）。
            if (search_dirty_) {
                run_search_scan(grid);
            }
        });
    // 选区的端点是存储行序，而存储顶边会随输出溢出、随 resize 与 `clear()` 位移：折算必须发生在
    // 副本并入之后（读数由副本带出），否则高亮与复制文本会跟着旧的行号指着别的内容（裁决 7.38⑤）。
    compensate_selection_drift();
    compensate_search_drift();
    flush_copy_request();
    flush_paste_request();
    // 发送侧一次性提示在**帧边界**取走 latch（裁决 7.104 的 D5①）：取走是消费动作，放进绘制路径就
    // 破坏「同一帧重绘画出同一张卡」那条幂等前提；会话已退时取走了也不挂卡——那时 dead-session 那一层
    // 已经在同一片居中区域落笔（本件排在它下面一层），而提示的对象是一个活着的会话。
    if (auto notice = session_->take_unrepresentable_notice(); notice.has_value() && session_->alive()) {
        show_unrepresentable_notice(std::move(*notice));
    }
    // 清卡的第二条路（D3①）：会话退出即撤，不等那枚 5 s 定时器——换绑会话即新建控件，状态天然从零开始。
    if (unrepresentable_notice_.has_value() && !session_->alive()) {
        clear_unrepresentable_notice();
    }
    // 回看换源与光标移动都不在提交里：前者由距底行数变化指认（`on_scroll` 在帧序里先于本函数，
    // 故同帧就能换源），后者直接比较光标快照。
    if (!frame.empty() || previous_back != mirror_.back_rows() || !(cursor_ == painted_cursor_)) {
        painted_cursor_ = cursor_;
        mark_needs_paint();
    }
    // 浮层的两次同步排在帧尾：`sync_state` 读的是本帧刚结算完的查询、匹配表与非法标志（早一拍就会把
    // 上一帧的计数画上计数槽），而 `sync_geometry` 现算本控件的窗口盒以重落锚点。两者都在没建浮层
    // 时不发生任何事，故单次读 `window_bounds()` 的成本只随「打开过搜索」而来（判据文 §4 第 5 条）。
    if (search_overlay_ != nullptr) {
        search_overlay_->sync_geometry();
        search_overlay_->sync_state();
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

auto TerminalView::reregister_blink_timer() -> void {
    // 句柄按「取消-重排」整条换掉：`TimerHandle::cancel()` 对未注册句柄是幂等空操作（析构里也照调），
    // 故挂载时没有调度器（无头帧）而后来才有的场景，在这里补注册一次即可让闪烁跟上。
    auto *scheduler = aurora::Scheduler::current();
    blink_timer_.cancel();
    if (scheduler != nullptr) {
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
    // 发送侧一次性提示卡在 dead-session 那一层**之前**画：本卡非模态、不铺带，两者同框时让位于
    // 「会话已退出」那条更重的状态（裁决 7.104 的 D2①，判据文 §1 事实 3）。
    paint_unrepresentable_notice(p, bounds);
    // dead-session 浮层排在最后一层（`SPEC.FEAT.WS.05`）：它是「会话已退出」这一状态上的模态提示，
    // 盖在回看内容与指示条之上都不为过。判据现取 `Session::alive()` 而不是缓存的翻转标志——
    // `on_closed` 那一次唤醒会带着最后一批 damage 排上帧，于是浮层与「最后一段输出」同一帧落定。
    paint_restart_overlay(p, bounds);
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
    // 上报与备屏翻页都优先于本地回看（`SPEC.FEAT.TERM.06`，裁决 7.77③）：开着上报档的 `vim` 里
    // 滚轮归 vim，而 Shift 把它让回本地——那一条是本地历史唯一的可达入口（裁决 7.77①），因为
    // 远端程序不会「把滚轮退还给终端」，让位只能发生在事件入口。
    const term::TermModes modes = modes_snapshot();
    const int notches = wheel_notches(e.delta_y);
    const bool shift_held = (e.modifiers & aurora::ModifierKey::Shift) != 0U;
    if (notches != 0 && !shift_held && term::mouse_tracking(modes) != term::MouseTracking::Off) {
        if (report_wheel(e, modes, notches > 0 ? term::MouseButton::WheelUp : term::MouseButton::WheelDown,
                         notches > 0 ? notches : -notches)) {
            e.remaining_y = 0.0F;  // 上报吃掉这一档，不再冒泡给回看与祖先
            e.is_handled = true;
            return;
        }
    }
    // 备屏里的滚轮转方向键（`?1007`，缺省开着）：`less`/`more` 一类程序不开上报档，翻页靠方向键，
    // 而它们只在备屏里跑——没有这一条腿，滚轮在备屏里就是一条都不发（本地回看在备屏无历史可回）。
    if (notches != 0 && modes.alternate_screen && modes.alternate_scroll &&
        page_alternate_screen(modes, notches > 0, notches > 0 ? notches : -notches)) {
        e.remaining_y = 0.0F;
        e.is_handled = true;
        return;
    }
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
    // dead-session 浮层的按钮（`SPEC.FEAT.WS.05`：本地腿一枚「重启」、SSH 腿重连中两枚「立即重试」
    // 「停止重连」）在上报档之前判定：会话已经退出，上报模式是陈旧的远端状态、不再有任何进程会读到
    // 它；而落点若是按钮就是本层自己的一次动作，与选区无关。命中按钮之外仍照旧落到选区分支——需求
    // 那句「保留终端内容供回看」正要求内容仍可选可复制（草图判据 4，命中序沿用裁决 7.86②）。
    if (e.action == aurora::MouseAction::Press && !session_->alive()) {
        const auto hits = [&](const std::optional<Rect> &box) -> bool {
            if (!box.has_value()) {
                return false;
            }
            const double x = static_cast<double>(e.local_position.x);
            const double y = static_cast<double>(e.local_position.y);
            return x >= box->x && x < box->x + box->width && y >= box->y && y < box->y + box->height;
        };
        // 三个盒一帧只有一档有值（`paint_restart_overlay` 每帧先全清再按档位填），故这里的先后只是
        // 可读次序，不是优先级：不存在两档同帧落笔，也就没有「抢同一击」。
        if (restart_hook_ && hits(restart_button_box_)) {
            restart_hook_();
            e.is_handled = true;
            return;
        }
        // 两枚重连按钮直达连接对象的控制位（裁决 7.99 D5）：截断当前退避与让环落终态都不重建会话，
        // 装配层没有需要它代劳的动作，故这里不穿一层闭包。控制位在则这一击必被收下，免得点按钮
        // 顺带在浮层底下推出一段选区。
        if (auto *control = session_->reconnect_control(); control != nullptr) {
            if (hits(retry_now_button_box_)) {
                control->retry_now();
                e.is_handled = true;
                return;
            }
            if (hits(stop_reconnect_button_box_)) {
                control->stop_reconnect();
                e.is_handled = true;
                return;
            }
        }
    }
    // 上报档优先于本地选区（`SPEC.FEAT.TERM.06`，裁决 7.77①②）：分流判据在选区之前，故一次
    // 让位是整笔手势让位而不是半笔——见 `takes_pointer_by_report` 的那三条短路。
    const term::TermModes modes = modes_snapshot();
    if (takes_pointer_by_report(e, term::mouse_tracking(modes))) {
        report_pointer(e, modes);
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

auto TerminalView::send_report(const std::optional<std::string> &bytes) -> void {
    if (!bytes) {
        return;  // 编码层判过「本层不发」：层级不够，或该事件在当前层级下不报
    }
    session_->send_bytes(std::as_bytes(std::span<const char>{bytes->data(), bytes->size()}));
}

auto TerminalView::report_cell(aurora::Point window_point) const -> std::optional<GridCellPos> {
    if (mirror_.rows() == 0U || geometry_.columns == 0U) {
        return std::nullopt;  // 字体未就绪或窗口最小化：没有格子可报
    }
    // 滚轮事件只带窗口坐标（派发器不平移 `position`，与指针事件的 `local_position` 不同源），
    // 故这里现算自身窗口盒再把点折回局部，然后交给选区那条路径同一个换算件。
    const auto box = window_bounds();
    if (!box) {
        return std::nullopt;  // 从未成功布局过：没有可信的窗口盒可减
    }
    return cell_at_point(geometry_, window_point.x - box->origin.x, window_point.y - box->origin.y);
}

auto TerminalView::takes_pointer_by_report(const aurora::MouseEvent &e, term::MouseTracking level) const
    -> bool {
    if (e.button != aurora::MouseButton::Left) {
        return false;  // 右键归本地三态、中键不在首版交付面（裁决 7.77②）：编码层因此没有按钮 2 那一档
    }
    if (reported_press_.has_value()) {
        return true;  // 整笔手势跟着那一次按下的归属，中途改道会把一次拖拽切成两半
    }
    if (level == term::MouseTracking::Off || (e.modifiers & aurora::ModifierKey::Shift) != 0U) {
        return false;  // Shift 是「这一次让位本地」的覆盖键（裁决 7.77①）：上报开着时它是本地选区唯一的入口
    }
    if (e.action == aurora::MouseAction::Press) {
        return true;
    }
    // 没有在途按下的移动只在 `?1003` 这一档归上报（悬停事件）；抬起在任何档都不归——那可能是 Shift
    // 覆盖期间的本地按下松了手，补一条孤儿松开会让远端以为某个键还按着。
    return e.action == aurora::MouseAction::Move && level == term::MouseTracking::AnyEvents;
}

auto TerminalView::report_pointer(aurora::MouseEvent &e, const term::TermModes &modes) -> void {
    const auto hit = cell_at_point(geometry_, e.local_position.x, e.local_position.y);
    if (!hit) {
        return;  // 没有格子可报：与本地那条路径同一早退，事件继续冒泡给祖先
    }
    e.is_handled = true;  // 认领与「本档真的发了字节」无关：`?1000` 档不报 Move，但也不能让它去推选区
    const bool control = (e.modifiers & aurora::ModifierKey::Control) != 0U;
    const bool alt = (e.modifiers & (aurora::ModifierKey::Alt | aurora::ModifierKey::Meta)) != 0U;
    const auto at = [column = hit->column, row = hit->row, control,
                     alt](term::MousePhase phase, term::MouseButton button) -> term::MouseEvent {
        // 上报协议吃的是**可见区**行列（`cell_at_point` 给的绘制行号），不是选区用的存储行序：
        // 远端不知道本地滚到了哪一屏，它的坐标系就是当前这一屏。
        return term::MouseEvent{
            .phase = phase, .button = button, .column = column, .row = row, .control = control, .alt = alt};
    };
    switch (e.action) {
        case aurora::MouseAction::Press: {
            reported_press_ = term::MouseButton::Left;
            reported_motion_ = *hit;
            send_report(term::encode_mouse(at(term::MousePhase::Press, term::MouseButton::Left), modes));
            break;
        }
        case aurora::MouseAction::Move: {
            // 按住的键由本层自己的在途态定，不问事件携带的 `button`：四后端都把 Move 盖成左键，
            // 照它编码就把「无键悬停」报成了拖动（少了 `?1003` 与 `?1002` 的区别）。
            const auto button = reported_press_.value_or(term::MouseButton::None);
            if (reported_motion_ == *hit) {
                break;  // 同一格不重报（裁决 7.77④）
            }
            reported_motion_ = *hit;
            send_report(term::encode_mouse(at(term::MousePhase::Drag, button), modes));
            break;
        }
        case aurora::MouseAction::Release: {
            // 分流判据只在有在途按下时才把抬起交进来，故这里的按键编号恒在：一条孤儿松开（按下
            // 归本地、松开却归上报）会让远端以为某个键还按着，`vim` 因此停在 visual 选择态。
            const auto button = *reported_press_;
            reported_press_ = std::nullopt;
            reported_motion_ = std::nullopt;
            send_report(term::encode_mouse(at(term::MousePhase::Release, button), modes));
            break;
        }
    }
}

auto TerminalView::report_wheel(const aurora::ScrollEvent &e, const term::TermModes &modes,
                                term::MouseButton button, int notches) -> bool {
    const auto cell = report_cell(e.position);
    if (!cell) {
        return false;  // 没有落点：让位下一档（备屏翻页或本地回看），而不是把这一档吞掉
    }
    const term::MouseEvent event{
        .phase = term::MousePhase::Press,
        .button = button,
        .column = cell->column,
        .row = cell->row,
        .control = (e.modifiers & aurora::ModifierKey::Control) != 0U,
        .alt = (e.modifiers & (aurora::ModifierKey::Alt | aurora::ModifierKey::Meta)) != 0U,
    };
    // 一 notch 一条：滚轮在两档协议里都是「按下即松开」的瞬时事件（没有滚轮的松开形态，也不带
    // 增量参数），故重复发档位那么多次而不是把增量塞进某处。
    for (int i = 0; i < notches; ++i) {
        send_report(term::encode_mouse(event, modes));
    }
    return true;
}

auto TerminalView::page_alternate_screen(const term::TermModes &modes, bool up, int notches) -> bool {
    // 形态与真实方向键同源（`DECCKM` 决定 CSI 还是 SS3），故复用按键那条编码入口而不是另写一串字节。
    // 修饰态不参与：xterm 的 alternate scroll 发的是「有人按了方向键」，且 Ctrl 档在本入口之前已被
    // 字号缩放吃掉，只有到界让位时才带着 Ctrl 落到这里，按 plain 发即可。
    const term::KeyPress press{.sym = up ? term::KeySym::ArrowUp : term::KeySym::ArrowDown};
    const auto bytes = term::encode_key(press, modes);
    if (!bytes) {
        return false;
    }
    for (int i = 0; i < notches; ++i) {
        send_report(bytes);
    }
    return true;
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
    // 三层区间一次交出、一次换底：底色替换发生在 `resolve` 之后，故高亮段与选中段各因底色不同而
    // 自成一跑，色带、文本与装饰都跟着这张 run 表走（裁决 7.38① D1①，视觉稿 A1-c 的「字形不被染色」）。
    // 无区间时带形态逐字段等于两参形态，于是这里只有一条绘制路径而不是「有选区走 A、有命中走 B」。
    const auto runs = layout_row(row, spec_, bands_for_screen_row(screen_row));
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
    // 视觉稿 C2-a：光标层在两层区间之上，而第三段取的是**该格最终的底色**——它可能同时属一段命中
    // 与被选区盖住的那几格。折色只在 `ui::background_at` 一处算（与 ② 色带层同一条算式），故 C1-a 的
    // 层序不会在这一格被重述成第二份；漏掉这一句就会让当前命中格用未命中底色重画字形（C2-b）。
    if (const auto ink = background_at(bands_for_screen_row(screen_row), cursor_.column); ink.has_value()) {
        paint.background = *ink;
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

auto TerminalView::paint_restart_overlay(aurora::Painter &p, const aurora::Rect &bounds) -> void {
    // 三枚按钮盒每帧先清再按当帧档位填：读取者拿到的永远是当前帧的形态，而不是上一帧的陈旧矩形
    //（本件把观测点从一枚扩到三枚，「哪一档落帧就只交出哪一档的盒」这条不变量由这里守住）。
    restart_button_box_.reset();
    retry_now_button_box_.reset();
    stop_reconnect_button_box_.reset();
    // 会话进程已退出才画这一层。`session_->alive()` 每帧现取，而不是缓一份
    // 标志——`Session::on_closed` 那一次唤醒与最后一批 damage 同帧抵达，缓存标志就要在事件回调里
    // 翻，而那是 IO 边界之外的一条通道（AGENTS.md §4.5 第 25 条）。重拨期间它恒为 false（裁决
    // 7.99 D7③：远端确实没有活进程，浮层的可见性只走状态快照这一条 latest-value 通道）。
    if (session_->alive()) {
        return;
    }
    const auto progress = session_->reconnect_progress();
    auto *control = session_->reconnect_control();
    // 「重连中」＝退避环里还挂着下一回：档位为 None 且仍有等待毫秒数。腿在重拨成功后擦回的那份全零
    // 快照就靠延迟位与这一档分开。两枚按钮直达连接对象的控制位，故这一档不看重启钩子——那是
    // 「重建会话」那条腿的出口，与「截断这次等待」是两件事。
    const bool redialing = control != nullptr && progress.has_value() &&
                           progress->stop == session::ReconnectStop::None && progress->delay_ms > 0;
    // 「有终态档位」＝环已收口并留下了档位：首行文案与那枚按钮的标签都按档位分档（草图屏 B 三档）。
    const bool terminal = progress.has_value() && progress->stop != session::ReconnectStop::None;
    if (!redialing && !terminal && !restart_hook_) {
        return;  // 装配层没接重启这条腿，会话又没有重连状态：无可画的出口
    }

    const double box_w = static_cast<double>(bounds.size.width);
    const double box_h = static_cast<double>(bounds.size.height);
    // 覆盖整可见区的半透明带：向纯黑各半混合。固定 alpha 在浅色主题上会把提示吞掉，而「哪一档主题」
    // 是运行期决定的（`apply_appearance`），故把降级量交给 `mix_half` 而不是写一个十六进制数。
    const auto band = mix_half(spec_.default_background, RgbaColor{0U, 0U, 0U, 255U});
    p.fill_rect(bounds, to_color(band));

    const auto chrome = settings_chrome();
    const double card_w = std::min(kRestartCardWidthDp, box_w - 2.0 * kRestartCardHorizontalInsetDp);
    const double card_h = redialing ? kReconnectCardHeightDp : kRestartCardHeightDp;
    const double card_x = (box_w - card_w) / 2.0;
    const double card_y = (box_h - card_h) / 2.0;
    const Rect card{card_x, card_y, card_w, card_h};
    p.fill_rect(to_rect(card, bounds.origin), chrome.card_bg);
    // 四边描线：框架 `Painter` 无 stroke-rect 入口，与 `search_overlay` 里条体描边同款四条 `fill_rect`。
    const double inner_h = card_h - 2.0 * kRestartStrokeDp;
    p.fill_rect(to_rect(Rect{card_x, card_y, card_w, kRestartStrokeDp}, bounds.origin), chrome.card_line);
    p.fill_rect(to_rect(Rect{card_x, card_y + card_h - kRestartStrokeDp, card_w, kRestartStrokeDp}, bounds.origin),
                chrome.card_line);
    p.fill_rect(to_rect(Rect{card_x, card_y + kRestartStrokeDp, kRestartStrokeDp, inner_h}, bounds.origin),
                chrome.card_line);
    p.fill_rect(to_rect(Rect{card_x + card_w - kRestartStrokeDp, card_y + kRestartStrokeDp, kRestartStrokeDp,
                             inner_h},
                        bounds.origin),
                chrome.card_line);

    const double text_x = card_x + kRestartCardPaddingDp;
    double text_top = card_y + kRestartCardPaddingDp;
    const double text_w = card_w - 2.0 * kRestartCardPaddingDp;
    if (redialing) {
        p.draw_text(to_rect(Rect{text_x, text_top, text_w, kRestartHintLineHeightDp}, bounds.origin),
                    settings_label("session.reconnect.line",
                                   {aurora::LocalizedString{std::to_string(progress->attempt)},
                                    aurora::LocalizedString{std::to_string(progress->total)}}),
                    ref_font_, chrome.text);
        text_top += kRestartHintLineHeightDp;
        // 第二行只报当下这一档还要等多久：草图那句括注（首次 1 s、逐次翻倍、钳上限）说的是退避**算式**，
        // 而快照里没有 base/cap 两个参数（D3 只外传档位与这一次的量），故此处不抄那句。秒数向上取整，
        // 于是 1000 ms 报「1 秒」而不是「0 秒」。
        p.draw_text(to_rect(Rect{text_x, text_top, text_w, kRestartHintLineHeightDp}, bounds.origin),
                    settings_label("session.reconnect.countdown",
                                   {aurora::LocalizedString{std::to_string((progress->delay_ms + 999) / 1000)}}),
                    ref_font_, chrome.text);
        text_top += kRestartHintLineHeightDp;
    } else {
        std::string hint = settings_label("session.restart.hint");
        if (terminal) {
            switch (progress->stop) {
            case session::ReconnectStop::AttemptsExhausted:
                hint = settings_label("session.reconnect.exhausted",
                                      {aurora::LocalizedString{std::to_string(progress->attempt)}});
                break;
            case session::ReconnectStop::NotReconnectable:
                hint = settings_label("session.reconnect.not_reconnectable");
                break;
            case session::ReconnectStop::UserStopped:
                hint = settings_label("session.reconnect.user_stopped");
                break;
            case session::ReconnectStop::RemoteExit:
            case session::ReconnectStop::None:
                // 远端 shell 正常退出与「快照里没有档位」（本地 PTY / ConPTY 两腿）同形：那句「会话已退出」。
                break;
            }
        }
        p.draw_text(to_rect(Rect{text_x, text_top, text_w, kRestartHintLineHeightDp}, bounds.origin), hint,
                    ref_font_, chrome.text);
        text_top += kRestartHintLineHeightDp;
    }

    const double button_top = text_top + kRestartButtonGapDp;
    if (redialing) {
        const double pair_w = 2.0 * kRestartButtonWidthDp + kRestartButtonGapDp;
        const double pair_left = card_x + (card_w - pair_w) / 2.0;
        const Rect retry_box{pair_left, button_top, kRestartButtonWidthDp, kRestartButtonHeightDp};
        const Rect stop_box{pair_left + kRestartButtonWidthDp + kRestartButtonGapDp, button_top,
                            kRestartButtonWidthDp, kRestartButtonHeightDp};
        // 「立即重试」取 accent 档、「停止重连」取描边线档的中性底：两枚同色就分不出哪一枚是就此收手，
        // 而这两枚的后果正好相反（前者多拨一回，后者落到终态）。
        p.fill_rect(to_rect(retry_box, bounds.origin), chrome.accent);
        p.draw_text(to_rect(retry_box, bounds.origin), settings_label("session.reconnect.retry_now"), ref_font_,
                    chrome.window_bg);
        p.fill_rect(to_rect(stop_box, bounds.origin), chrome.card_line);
        p.draw_text(to_rect(stop_box, bounds.origin), settings_label("session.reconnect.stop"), ref_font_,
                    chrome.text);
        retry_now_button_box_ = retry_box;
        stop_reconnect_button_box_ = stop_box;
        return;
    }
    if (!restart_hook_) {
        return;  // 终态但没有重启腿：只报这一句状态，不画一个不响的按钮
    }
    // 按钮标签按档位分：有终态档位的都是 SSH 腿，那里的重启动作由装配层按档案重开（裁决 7.99 D6），
    // 标签要说清「按档案」；认证类那一档额外说明这一回会重新问凭据。无快照的本地腿保持 7.86 那句「重启」。
    std::string_view button_key = "session.restart.button";
    double button_w = kRestartButtonWidthDp;
    if (terminal) {
        button_key =
            progress->stop == session::ReconnectStop::NotReconnectable ? "session.restart.retry_credential"
                                                                       : "session.restart.by_profile";
        button_w = kWideRestartButtonWidthDp;
    }
    const double button_left = card_x + (card_w - button_w) / 2.0;
    const Rect button_box{button_left, button_top, button_w, kRestartButtonHeightDp};
    p.fill_rect(to_rect(button_box, bounds.origin), chrome.accent);
    p.draw_text(to_rect(button_box, bounds.origin), settings_label(button_key), ref_font_, chrome.window_bg);
    // 交进 `restart_button_box_` 的是**控件本地 dp**，与 `e.local_position` 同一坐标空间，
    // 于是指针入口不需要再补 `bounds.origin`（框架派发那条原点已在 G31 回货里对齐到本控件的绘制盒）。
    restart_button_box_ = button_box;
}

auto TerminalView::show_unrepresentable_notice(session::UnrepresentableNotice notice) -> void {
    unrepresentable_notice_ = std::move(notice);
    // 定时器一律「先撤旧再排新」（口径同 `reregister_blink_timer`：`cancel()` 对未注册句柄是幂等空
    // 操作）。同一会话对象按 D1① 只上弦一次，本不该有第二次重排，但「不叠两枚定时器」这件事不该
    // 依赖上游那条不变量——它归会话，而这一句归本控件。
    notice_timer_.cancel();
    if (auto *scheduler = aurora::Scheduler::current(); scheduler != nullptr) {
        notice_timer_ =
            scheduler->set_timeout(kNoticeVisibleFor, [this]() -> void { clear_unrepresentable_notice(); });
    }
    // 没有调度器（无头帧）时卡片常驻：与光标闪烁在无头帧不跳是同一条既定形态，判据文 §5 已登记。
    mark_needs_paint();
}

auto TerminalView::clear_unrepresentable_notice() -> void {
    if (!unrepresentable_notice_.has_value()) {
        return;  // 定时器与帧边界都会走到这里，本来就没挂着就别白标一次脏
    }
    notice_timer_.cancel();
    unrepresentable_notice_.reset();
    mark_needs_paint();
}

auto TerminalView::paint_unrepresentable_notice(aurora::Painter &p, const aurora::Rect &bounds) -> void {
    if (!unrepresentable_notice_.has_value()) {
        return;
    }
    const double box_w = static_cast<double>(bounds.size.width);
    const double box_h = static_cast<double>(bounds.size.height);
    const double card_w = std::min(kRestartCardWidthDp, box_w - 2.0 * kRestartCardHorizontalInsetDp);
    // 让位不画（判据 6）：纵向装不下「卡高 + 上下内缩」时整张不画，而不是压掉回看内容或截到只剩一行。
    // 卡宽那一侧已由 `min` 收进可视区，非正值意味着这一格连卡都放不住，同样不画。
    if (card_w <= 0.0 || box_h < kNoticeCardHeightDp + 2.0 * kRestartCardHorizontalInsetDp) {
        return;
    }

    const auto chrome = settings_chrome();
    const double card_h = kNoticeCardHeightDp;
    const double card_x = (box_w - card_w) / 2.0;
    const double card_y = box_h - card_h - kRestartCardHorizontalInsetDp;
    // 非模态三不（D2①）：不铺覆盖整可见区的半透明带、不吃焦点、不写任何命中盒——本函数一个成员都不
    // 往 `*_button_box_` 那组里写，指针入口因此看不见这张卡，卡下的旧内容照常可读、可选。
    p.fill_rect(to_rect(Rect{card_x, card_y, card_w, card_h}, bounds.origin), chrome.card_bg);
    const double inner_h = card_h - 2.0 * kRestartStrokeDp;
    p.fill_rect(to_rect(Rect{card_x, card_y, card_w, kRestartStrokeDp}, bounds.origin), chrome.card_line);
    p.fill_rect(to_rect(Rect{card_x, card_y + card_h - kRestartStrokeDp, card_w, kRestartStrokeDp}, bounds.origin),
                chrome.card_line);
    p.fill_rect(to_rect(Rect{card_x, card_y + kRestartStrokeDp, kRestartStrokeDp, inner_h}, bounds.origin),
                chrome.card_line);
    p.fill_rect(to_rect(Rect{card_x + card_w - kRestartStrokeDp, card_y + kRestartStrokeDp, kRestartStrokeDp,
                             inner_h},
                        bounds.origin),
                chrome.card_line);

    const double text_x = card_x + kRestartCardPaddingDp;
    const double text_w = card_w - 2.0 * kRestartCardPaddingDp;
    // 第一行报名字与实际生效腿（D6②：`Session` 构造期折一次的那份，与真正跑的解码器同源）。
    p.draw_text(to_rect(Rect{text_x, card_y + kRestartCardPaddingDp, text_w, kRestartHintLineHeightDp},
                        bounds.origin),
                settings_label("terminal.notice.unrepresentable.line",
                               {aurora::LocalizedString{unrepresentable_notice_->leg},
                                aurora::LocalizedString{std::to_string(unrepresentable_notice_->count)}}),
                ref_font_, chrome.text);
    // 第二行只指路径、不报当前档位名（D4①）：设置页那枚下拉显示的是 ASCII 档名本身，卡片报中文档名
    // 会对不上、报 ASCII 又违背界面语言。副色档与「出口」这句话的分量相称。
    p.draw_text(to_rect(Rect{text_x, card_y + kRestartCardPaddingDp + kRestartHintLineHeightDp, text_w,
                             kRestartHintLineHeightDp},
                        bounds.origin),
                settings_label("terminal.notice.unrepresentable.action"), ref_font_, chrome.text_dim);
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

auto TerminalView::hit_slice(std::size_t storage_row) const noexcept -> std::span<const RowSpan> {
    if (!search_matches_.has_value()) {
        return {};
    }
    const auto spans = search_matches_->spans();
    // 表按 (行, 列) 升序，故同一行的条目连着排：两次二分即得本行那一段。上限档一屏可达 10,000 条
    // ≈ 240 KB，逐行整份拷进绘制入参就把一次扫描的成本摊到了每帧每行（判据文 §4 第 1 条）。
    const auto begin = spans.begin();
    const auto first = std::lower_bound(begin, spans.end(), storage_row,
                                        [](const RowSpan &span, std::size_t row) { return span.row < row; });
    const auto last = std::upper_bound(first, spans.end(), storage_row,
                                       [](std::size_t row, const RowSpan &span) { return row < span.row; });
    return spans.subspan(static_cast<std::size_t>(first - begin), static_cast<std::size_t>(last - first));
}

auto TerminalView::bands_for_screen_row(std::size_t screen_row) const -> RowBands {
    const std::size_t storage_row = mirror_.window_top() + screen_row;
    return RowBands{
        .hits = hit_slice(storage_row),
        .current_hit = search_matches_.has_value() ? search_matches_->current() : std::nullopt,
        .selection = band_at(storage_row),
        // 两档命中色都不看焦点（判据 A1-f）：浮层一打开键盘焦点就归输入框，视口必然处于失焦态，
        // 若命中也跟着降级，「正在搜的这一次」反而成了画面最弱的一档。
        .hit_background = mix_half(spec_.basic[11], spec_.default_background),
        .current_hit_background = spec_.basic[11],
        .selected_background = selection_ink(),
    };
}

auto TerminalView::scroll_row_into_view(std::size_t storage_row) -> void {
    const std::size_t rows = mirror_.rows();
    if (rows == 0U) {
        return;
    }
    const std::size_t top = mirror_.window_top();
    if (storage_row >= top && storage_row - top < rows) {
        return;  // 判据 D1-b：已在可见窗内就完全不动画面，居中只在窗外那一档发生
    }
    // 居中算式只有一个输入（判据 D2-b）。内核的 `offset_y` 与 `window_top()` 是同一个量：距底 ＝
    // `max_offset − offset_y`，而 `window_top` ＝ 总行数 − rows − 距底，两式相减即 `offset_y` ＝
    // `window_top`。故「把窗口顶边写成目标行 − ⌊rows/2⌋」就是直接写 `offset_y`，不必先换算距底；
    // 写的是内核那份状态而不是临时偏移，`reproject` 因此在下一帧把「距底」按新值保持下去（D2-a）。
    const std::size_t half = rows / 2U;
    const float target = static_cast<float>(storage_row >= half ? storage_row - half : 0U);
    scroll_viewport_.offset_y = aurora::ScrollViewport::clamp_offset(
        target, 0.0F, kRowStep, scroll_viewport_.content_h, scroll_viewport_.viewport_h);
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

auto TerminalView::run_search_scan(grid::Storage &grid) -> void {
    search_dirty_ = false;
    // 空串即「还没打字」：不发扫描、不产高亮，也**不进扫描计数**（判据 A2-a）——否则「一轮只扫一次」
    // 那条以计数为据的判据就测成了「有没有人按过键」。
    if (search_query_.text.empty()) {
        search_matches_.reset();
        search_invalid_ = false;
        search_scanned_ = search_query_;
        mark_needs_paint();  // 撤掉上一轮的高亮同样是画面变化
        return;
    }
    ++search_scans_;
    auto matches = search(grid, search_query_);
    // 无论成败都记下「这份条件已经被试过」，于是非法档显示的是「表达式非法」而不是「按 Enter 应用」：
    // 用户已经按过 Enter 了，再提示他按一次就是说谎（判据 B4 与 B5 必须分成两句的物理根据）。
    search_scanned_ = search_query_;
    if (!matches.has_value()) {
        search_invalid_ = true;  // 编译失败：旧表与游标原样留着，「表达式错」与「搜不到」不同档
        return;
    }
    search_invalid_ = false;
    search_matches_ = std::move(matches);
    // 新表的行号以「扫描这一刻的存储顶边」为基准，故基准在这里对齐；`compensate_search_drift` 同帧
    // 读到同一个读数，于是刚扫完的表位移增量为零、不会被折算两次。
    search_dropped_baseline_ = mirror_.dropped_lines();
    // 换表就是换画面：不等别的脏源替它标脏。漏掉这一句的后果是「打了字却什么都没变」——只有
    // 光标移动、选区或滚动才恰好顺带标脏，而纯输入那一帧是干净的（像素证人见 A1-a）。
    mark_needs_paint();
}

auto TerminalView::compensate_search_drift() -> void {
    const std::int64_t dropped = mirror_.dropped_lines();
    const std::int64_t rows_up = dropped - search_dropped_baseline_;
    search_dropped_baseline_ = dropped;
    // 新输出把内容顶走时**不重扫**：表按位移增量折算，与选区跟走同一条路径、同一份快照（裁决 7.78③）。
    if (search_matches_.has_value() && rows_up != 0) {
        search_matches_->translate_rows(rows_up);
    }
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
