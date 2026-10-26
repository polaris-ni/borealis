// ============================================================
// 串口十六进制收发监视面板实现（src/ui/session_monitor_panel.cpp）
// ------------------------------------------------------------
// 形态对照设计稿 `codespec/UI_SERIAL_MONITOR.draft.md` 的标号（§2 那张屏面表 + 二期 §9.2 的 D10～D18）：
// 640 dp 停靠卡片（D5①，54 字符/行）、两枚面板局部开关（D6①／D7①）、清空与关闭（D7①：清空是唯一的
// 止血动作）、丢弃留痕（D4①）、不落盘声明（§1 第一条安全口径）、等宽行区（F8／F9）。
//
// 二期把行区换成 `LazyList`（D10①）：环里存着整段历史，面板只折当前 96 行窗口（D11①）；只有贴底才
// 跟尾，离开底部即冻结且绝不自动吸回（D12①）。三条动作腿（时间戳档、暂停档、清空／回到最新）**只改
// 状态、不当场重建**：重建唯一发生在 `tick()`，于是「事件回调里不做长计算」（AGENTS §4.5 第 25 条）在
// 本件是结构性的而不是约定——一帧的代价上限是「数行一次 + 折 96 行」（≈ 23 µs，F5），而它落在帧边界
// 而不是点击派发里。代价：拨档到看见最多差一帧（面板族里 `TunnelPanel::tick()` 同形）。
//
// 行文本在这里只存在一瞬间：环里存的是原始 run（D3①），本件折出的窗口只用于绘制；关掉浮层那些字符串
// 就没了。三期之后「不落盘」这句话收窄成**本件不写盘**：只有用户在动作行点了「导出」并由装配腿落盘时才有
// 文件出现（稿 §10.2 D22①／D24①），常驻那句声明也按同一口径改了口。
// ============================================================

#include "session_monitor_panel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "aurora/core/json.h"
#include "aurora/core/log.h"
#include "aurora/widget/alignment.h"
#include "aurora/widget/button.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/lazy_list.h"
#include "aurora/widget/stack.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"

#include "borealis/ui/font_choice.h"
#include "settings_i18n.h"
#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

/// @brief 一行里的一格：等宽族 + 关掉换行。
///
/// WHY 关换行：54 字符在 14 pt 等宽里是 594 dp，卡内净宽 616 dp，缺省档放得下；但用户把窗拖窄时
/// 让它**截断**而不是折成两截——折行会把「一行一屏报文」这个行形态抹掉，而截断只是少看几个字节，
/// 且下一格还在（稿 §2 的行边界口径）。
[[nodiscard]] auto mono_cell(std::string content, aurora::Color color) -> std::shared_ptr<aurora::Text> {
    return std::make_shared<aurora::Text>(aurora::TextProps{
        .content = std::move(content),
        .font = aurora::Font{.family = std::string{kDefaultMonospaceFamily}},
        .text_color = color,
        .soft_wrap = false,
    });
}

[[nodiscard]] auto make_text(std::string content, aurora::Color color) -> std::shared_ptr<aurora::Text> {
    return std::make_shared<aurora::Text>(
        aurora::TextProps{.content = std::move(content), .text_color = color});
}

[[nodiscard]] auto make_button(std::string label, aurora::Color bg, aurora::Color fg, float min_width,
                               std::function<void()> on_click, aurora::Color line)
    -> std::shared_ptr<aurora::Button> {
    auto button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = std::move(label),
        .color = bg,
        .on_color = fg,
        .border_color = line,
        .border_width = 1.0F,
        .min_width = min_width,
    });
    button->set_on_click(std::move(on_click));
    return button;
}

[[nodiscard]] auto node(std::shared_ptr<aurora::Widget> widget) -> aurora::Node {
    return aurora::Node{std::move(widget)};
}

[[nodiscard]] auto row_of(std::vector<aurora::Node> children, float gap) -> aurora::Node {
    return node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = gap,
    }));
}

/// @brief 一枚计数词条的实参形态（`{0}` 位一律交现算的读数，不写死在模板里）。
[[nodiscard]] auto count_arg(std::size_t value) -> std::vector<aurora::MessageArg> {
    return {{"count", static_cast<std::int64_t>(value)}};
}

}  // namespace

SessionMonitorPanel::SessionMonitorPanel(aurora::OverlayHost &host, Hooks hooks)
    : host_{&host}, hooks_{std::move(hooks)} {}

// 先摘后换（头文件的判据）：`close()` 摘的是**旧宿主**上自己记的那枚序号，之后浮层归零。环不动——
// 留痕活在那条会话的 `Session` 里，本件只是它的一份投影，所以搬窗既不停采也不清屏（重开看到的是历史）。
auto SessionMonitorPanel::set_overlay_host(aurora::OverlayHost &host) -> void {
    if (host_ == &host) {
        return;
    }
    close();
    host_ = &host;
}

SessionMonitorPanel::~SessionMonitorPanel() {
    close();
}

auto SessionMonitorPanel::open() -> void {
    if (open_) {
        // 已开着：只跟一帧数。命令重复唤起不该把浮层拆了重挂（面板族同款口径，`TunnelPanel::open()`）。
        tick();
        return;
    }
    // 首帧就有内容与计数，而不是空态加一句「等一下」。`open_` 仍为 false，故 `paint_rows()` 里的
    // `refresh_overlay()` 空转，真正挂浮层的是下面这一句。
    frozen_ = false;
    skip_geometry_once_ = true;
    rebuild_pending_ = true;
    follow_frame();
    open_ = true;
    install_overlay();
}

auto SessionMonitorPanel::close() -> void {
    if (!open_) {
        return;
    }
    clear_state();
}

auto SessionMonitorPanel::tick() -> void {
    if (!open_) {
        return;  // 没开就没有泵的事：环照人写，本件不在场时零成本（D7① 的常驻代价在会话侧）。
    }
    // 「回到最新」/清空：旧列表还停在离底位置，直接判几何会把刚落到底的画面判回冻结（D14① 的落地次序）。
    const bool now_frozen = skip_geometry_once_ ? false : compute_frozen();
    skip_geometry_once_ = false;
    if (now_frozen != frozen_) {
        frozen_ = now_frozen;
        rebuild_pending_ = true;  // 提示行与「回到最新」只在帧边界增删（D13①）
    }
    if (paused_) {
        if (rebuild_pending_) {
            rebuild_current_view();  // 暂停中改了档或清了环：按手里的窗口重画，不读新数据。
        }
        return;
    }
    if (frozen_) {
        if (rebuild_pending_) {
            rebuild_current_view();
        } else {
            slide_frozen_window();
        }
        return;
    }
    follow_frame();
}

auto SessionMonitorPanel::read_snapshot() const -> Snapshot {
    return hooks_.snapshot != nullptr ? hooks_.snapshot() : Snapshot{};
}

auto SessionMonitorPanel::make_options() const -> MonitorRowOptions {
    return MonitorRowOptions{
        .bytes_per_row = kMonitorBytesPerRow,
        .timestamp_prefix = timestamp_prefix_,
        // 第三枚同样是面板局部量（稿 D26①）：切档只换行形态，环、采集、设置都不动。
        .view = text_view_ ? MonitorView::Text : MonitorView::Hex,
        // 腿名是跟绑定一起现取的那份副本，不是设置现值（稿 D28①）：换会话即换档，改设置不换。
        .encoding = encoding_,
    };
}

auto SessionMonitorPanel::compute_frozen() const -> bool {
    if (list_ == nullptr) {
        return false;
    }
    const float max = list_->max_scroll_offset();
    if (max <= 0.0F) {
        // 未布局或内容不足一屏：一律视为跟尾（D12① 的「max == 0」一条），否则每次重建后第一帧都会
        // 误判成「离底」。
        return false;
    }
    const bool at_bottom = list_->scroll_offset() >= max - kMonitorBottomTolerance;
    // 只有「窗口底就是环底」时，滚到窗口底才算回到最新；窗口还可以下滑时，滚到窗口底只是继续下翻。
    return !(at_bottom && window_covers_ring_end());
}

auto SessionMonitorPanel::window_covers_ring_end() const -> bool {
    return fold_first_row_ + painted_rows_.size() >= total_rows_;
}

auto SessionMonitorPanel::follow_frame() -> void {
    auto snapshot = read_snapshot();
    auto binding = hooks_.binding_label != nullptr ? hooks_.binding_label() : std::string{};
    // 编码腿名与绑定同帧现取（稿 D28①）：空串或 Hook 缺席都折成 `UTF-8`——与折算件那条既有回落同值，
    // 但在这里折掉而不是交给 `make_session_decoder()` 去 WARN，否则每次重建都在显示路径上刷一条日志。
    auto encoding = hooks_.encoding_name != nullptr ? hooks_.encoding_name() : std::string{};
    if (encoding.empty()) {
        encoding = "UTF-8";
    }
    const bool forced = rebuild_pending_;
    const bool ring_changed = snapshot.revision != seen_revision_;
    if (!forced && !ring_changed && snapshot.cap_bytes == cap_bytes_ &&
        snapshot.dropped_bytes == dropped_bytes_ && binding == binding_ && encoding == encoding_) {
        return;  // D17①：环没变、档没动就零工作量（快照本身仍是一次 ≤ 256 KiB 拷贝，一期已有）。
    }
    if (ring_changed) {
        // 稿 §10.3：「已复制到剪贴板／已导出到某路径」那句只跟到下一次数据变化为止——数据在往下走，
        // 上一句动作的余音不该继续占着留痕区（清空那一条腿另走 `on_clear()`，不看 revision）。
        feedback_.clear();
    }
    source_ = std::move(snapshot.runs);
    binding_ = std::move(binding);
    encoding_ = std::move(encoding);
    ring_bytes_ = 0U;
    for (const auto &run : source_) {
        ring_bytes_ += run.bytes.size();
    }
    dropped_bytes_ = snapshot.dropped_bytes;
    seen_revision_ = snapshot.revision;
    cap_bytes_ = snapshot.cap_bytes;

    const auto options = make_options();
    rebase_rows_ = false;  // 这条腿本来就按当前档重算了 total/fold，停更态欠的那次重排在这里一并兑现。
    total_rows_ = monitor_row_count(source_, options);
    const auto window = std::min(total_rows_, kMonitorFoldWindowRows);
    fold_first_row_ = total_rows_ - window;
    auto rows = monitor_row_window(source_, options, fold_first_row_, window);
    if (!forced && rows == painted_rows_ && binding_ == painted_binding_ &&
        ring_bytes_ == painted_ring_bytes_ && dropped_bytes_ == painted_dropped_bytes_ &&
        cap_bytes_ == painted_cap_bytes_ && feedback_ == painted_feedback_ && !painted_frozen_) {
        return;  // revision 变了但画出来的东西没变（例如整条拒收只推进了丢弃数之外的状态）。
    }
    // 跟尾恒落底：把窗口内容高交给 `LazyList` 的待定偏移，首次布局后夹到可滚范围末端（D10① 的
    // 「无锚定到末尾入口」替身，见稿 §9.7 第 2 条）。
    const auto offset = static_cast<float>(rows.size()) * kMonitorRowExtent;
    paint_rows(std::move(rows), offset);
}

auto SessionMonitorPanel::rebuild_current_view() -> void {
    const auto options = make_options();
    if (rebase_rows_) {
        // 换视图档就是换行号坐标系（hex 档 8 字节/行 ↔ 文本档 36 字节/行），冻结/暂停里沿用的
        // `fold_first_row_` 与旧偏移在另一档指向的是别的历史段——与其悄悄跳去另一段，不如复位到**手里
        // 这份料源的末尾**（稿 D34：暂停不清，只是不读新环）。计数不依赖解码（D27①），这里补一次便宜。
        rebase_rows_ = false;
        total_rows_ = monitor_row_count(source_, options);
        const auto tail_window = std::min(total_rows_, kMonitorFoldWindowRows);
        fold_first_row_ = total_rows_ - tail_window;
        auto tail = monitor_row_window(source_, options, fold_first_row_, tail_window);
        paint_rows(std::move(tail), static_cast<float>(tail.size()) * kMonitorRowExtent);
        return;
    }
    const auto available = total_rows_ > fold_first_row_ ? total_rows_ - fold_first_row_ : 0U;
    const auto window = std::min(available, kMonitorFoldWindowRows);
    auto rows = monitor_row_window(source_, options, fold_first_row_, window);
    // 冻结/暂停要保住用户当前读到的那一屏：新列表不继承旧偏移，故把旧偏移原样交给待定偏移。
    const auto offset = list_ != nullptr ? list_->scroll_offset() : 0.0F;
    paint_rows(std::move(rows), offset);
}

auto SessionMonitorPanel::slide_frozen_window() -> void {
    if (list_ == nullptr || total_rows_ == 0U || painted_rows_.empty()) {
        return;
    }
    const auto [first, last] = list_->visible_range();
    const auto first_row = static_cast<std::size_t>(std::max(first, 0));
    const auto last_row = static_cast<std::size_t>(std::max(last, 0));
    const auto window_rows = painted_rows_.size();
    const bool near_top = first_row < kMonitorFoldEdgeRows;
    const bool near_bottom = last_row + kMonitorFoldEdgeRows > window_rows;
    if (!near_top && !near_bottom) {
        return;  // 可见区还在折带中段：不重折（D12① 的「只在滚出折带时重折」）。
    }
    if (near_top && fold_first_row_ == 0U) {
        return;  // 已在环头，再往上没有料。
    }
    // 以可见首行为锚，窗口上方留 `kMonitorFoldMarginRows` 行缓存；窗口贴住环头/环尾时自然夹住。
    const auto anchor = fold_first_row_ + first_row;
    const auto want = anchor > kMonitorFoldMarginRows ? anchor - kMonitorFoldMarginRows : 0U;
    const auto max_first =
        total_rows_ > kMonitorFoldWindowRows ? total_rows_ - kMonitorFoldWindowRows : 0U;
    const auto next_first = std::min(want, max_first);
    if (next_first == fold_first_row_) {
        return;
    }
    const auto offset = list_->scroll_offset();
    const auto options = make_options();
    const auto next_window = std::min(total_rows_ - next_first, kMonitorFoldWindowRows);
    auto rows = monitor_row_window(source_, options, next_first, next_window);
    // 窗口首行变了 k 行，原偏移同向减去 k × 行高：屏上首行不动（D12① 的「保持偏移」）。
    const auto delta_rows = static_cast<float>(next_first) - static_cast<float>(fold_first_row_);
    const auto next_offset = std::max(0.0F, offset - (delta_rows * kMonitorRowExtent));
    fold_first_row_ = next_first;
    paint_rows(std::move(rows), next_offset);
}

auto SessionMonitorPanel::paint_rows(std::vector<MonitorRow> rows, float scroll_offset) -> void {
    painted_rows_ = std::move(rows);
    painted_binding_ = binding_;
    painted_ring_bytes_ = ring_bytes_;
    painted_dropped_bytes_ = dropped_bytes_;
    painted_cap_bytes_ = cap_bytes_;
    painted_frozen_ = frozen_;
    painted_feedback_ = feedback_;
    pending_scroll_offset_ = scroll_offset;
    rebuild_pending_ = false;
    refresh_overlay();
}

auto SessionMonitorPanel::visible_rows() const -> std::vector<MonitorRow> {
    return painted_rows_;
}

auto SessionMonitorPanel::notice() const -> std::string {
    if (dropped_bytes_ == 0U) {
        return {};
    }
    // 丢弃必须上屏：不报这一句，「环里最早那行」就会被当成「设备最早发的那行」（D4①）。上限读快照
    // 带出的现值（D16①）——换档后这一句自己跟着走，不在面板里写死第二份。
    const auto cap_kib = (cap_bytes_ + 1023U) / 1024U;
    return settings_label("session.monitor.notice.dropped",
                          {{"count", static_cast<std::int64_t>(dropped_bytes_)},
                           {"cap", static_cast<std::int64_t>(cap_kib)}});
}

auto SessionMonitorPanel::install_overlay() -> void {
    if (!open_ || host_ == nullptr) {
        return;
    }
    list_.reset();  // 旧行区随旧层一起死；这里只是把句柄交还给新一帧。
    const auto chrome = settings_chrome();
    auto mask = std::make_shared<aurora::Canvas>([c = chrome](aurora::Painter &painter,
                                                              const aurora::Rect &box) -> void {
        painter.fill_rect(box, c.window_bg);
    });
    mask->modifier.set(aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));
    auto card = build_overlay();
    auto layer = std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{node(std::static_pointer_cast<aurora::Widget>(std::move(mask))),
                                  node(std::static_pointer_cast<aurora::Widget>(std::move(card)))},
        aurora::Alignment::CenterLeft);
    dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(layer))));
    overlay_index_ = host_->add_overlay(node(std::static_pointer_cast<aurora::Widget>(dialog_)));
    dialog_->show();
}

auto SessionMonitorPanel::refresh_overlay() -> void {
    if (!open_ || !overlay_index_.has_value() || host_ == nullptr) {
        return;
    }
    host_->remove_overlay(*overlay_index_);
    overlay_index_.reset();
    list_.reset();  // 先松手再拆树：否则 LazyList 会在父节点已摘除后仍被别人持有（框架告警）。
    dialog_.reset();
    install_overlay();
}

[[nodiscard]] auto SessionMonitorPanel::build_row(const MonitorRow &row) -> aurora::Node {
    const auto chrome = settings_chrome();
    // 三格顺序拼接就是 `row_text()`；格间**不给 gap**——对齐这件事只由等宽族与折算件负责（稿 §2）。
    auto cells = std::vector<aurora::Node>{};
    if (!row.timestamp_prefix.empty()) {
        cells.push_back(node(mono_cell(row.timestamp_prefix, chrome.text_dim)));
    }
    cells.push_back(node(mono_cell(row.direction_label, chrome.accent)));
    cells.push_back(node(mono_cell(row.dump, chrome.text)));
    return row_of(std::move(cells), 0.0F);
}

[[nodiscard]] auto SessionMonitorPanel::build_overlay() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};

    // ---- 标题行：串口监视 + 绑定（档案名与设备路径）+ 字节计数 + 清空 + 关闭 ----
    // 与隧道面板同一条实测教训：标题不抓 fill_max_width，否则计数与两枚按钮整组被挤出卡宽（派发链上
    // 点不到）。绑定失去身份（标签散了／非串口）就整格不画，而不是画一句「未知」。
    auto header_children = std::vector<aurora::Node>{
        node(make_text(settings_label("session.monitor.title"), chrome.text))};
    if (!painted_binding_.empty()) {
        header_children.push_back(node(make_text(painted_binding_, chrome.text_dim)));
    }
    header_children.push_back(
        node(make_text(settings_label("session.monitor.bytes", count_arg(painted_ring_bytes_)),
                       chrome.text_dim)));
    if (hooks_.clear != nullptr) {
        header_children.push_back(node(make_button(settings_label("session.monitor.action.clear"),
                                                   chrome.control_bg, chrome.text_dim, 48.0F,
                                                   [this]() -> void { on_clear(); },
                                                   chrome.card_line)));
    }
    header_children.push_back(node(make_button(settings_label("session.monitor.action.close"),
                                               chrome.control_bg, chrome.text_dim, 48.0F,
                                               [this]() -> void { close(); }, chrome.card_line)));
    children.push_back(row_of(std::move(header_children), 8.0F));

    // ---- 开关行：时间戳前缀（缺省开）+ 暂停刷新（缺省关）+ 文本视图（缺省关，稿 D26①）----
    // 第三枚排在最后：集成用例按 `find("Switch", N)` 的树序取件，前两枚的序号不能因为新增而挪位。
    auto timestamp_switch = std::make_shared<aurora::Switch>(aurora::Reactive<bool>{timestamp_prefix_});
    timestamp_switch->set_on_changed(
        [this](bool next) -> void { on_timestamp_changed(next); });
    auto pause_switch = std::make_shared<aurora::Switch>(aurora::Reactive<bool>{paused_});
    pause_switch->set_on_changed([this](bool next) -> void { on_pause_changed(next); });
    auto text_switch = std::make_shared<aurora::Switch>(aurora::Reactive<bool>{text_view_});
    text_switch->set_on_changed([this](bool next) -> void { on_text_view_changed(next); });
    for (auto &sw : {timestamp_switch, pause_switch, text_switch}) {
        // 锁高的理由与设置面板开关同款：滑块直径按自身盒高算，行交叉轴居中给的是紧约束。
        sw->modifier.set(aurora::Modifier{}.height(24.0F));
        sw->set_active_color(chrome.accent);
        sw->set_inactive_color(chrome.control_bg);
        sw->set_border(chrome.card_line, 1.0F);
        sw->set_thumb_color(chrome.text);
    }
    children.push_back(row_of(std::vector<aurora::Node>{
                             node(make_text(settings_label("session.monitor.toggle.timestamp"),
                                            chrome.text_dim)),
                             node(std::static_pointer_cast<aurora::Widget>(std::move(timestamp_switch))),
                             node(make_text(settings_label("session.monitor.toggle.pause"),
                                            chrome.text_dim)),
                             node(std::static_pointer_cast<aurora::Widget>(std::move(pause_switch))),
                             node(make_text(settings_label("session.monitor.toggle.text_view"),
                                            chrome.text_dim)),
                             node(std::static_pointer_cast<aurora::Widget>(std::move(text_switch)))},
                         8.0F));

    // ---- 动作行：把这一屏取出去（三期，稿 §10.2 D22①／§10.3）----
    // 单开一行而不是挤进标题行：标题行已经载着标题、绑定、字节计数与清空／关闭，再加两枚会重犯隧道面板
    // 那条实测教训（整组被挤出卡宽 ⇒ 派发链上点不到）。两枚都只在对应 Hook 在场时画——面板族同一条纪律：
    // 不许因装配层没装某条腿而画出一枚点了没反应的钮。
    {
        auto actions = std::vector<aurora::Node>{};
        if (hooks_.copy_text != nullptr) {
            actions.push_back(node(make_button(settings_label("session.monitor.action.copy"),
                                               chrome.control_bg, chrome.text_dim, 48.0F,
                                               [this]() -> void { on_copy(); }, chrome.card_line)));
        }
        if (hooks_.pick_export_path != nullptr && hooks_.export_text != nullptr) {
            actions.push_back(node(make_button(settings_label("session.monitor.action.export_file"),
                                               chrome.control_bg, chrome.text_dim, 48.0F,
                                               [this]() -> void { on_export(); }, chrome.card_line)));
        }
        if (!actions.empty()) {
            children.push_back(row_of(std::move(actions), 8.0F));
        }
    }

    // ---- 冻结行：只有离底冻结时才在场（D13①：冻结必须可见，否则会被读成「设备停了」）----
    if (painted_frozen_) {
        children.push_back(row_of(std::vector<aurora::Node>{
                                     node(make_text(settings_label("session.monitor.notice.frozen"),
                                                    chrome.text_dim)),
                                     node(make_button(settings_label("session.monitor.action.latest"),
                                                      chrome.control_bg, chrome.text_dim, 72.0F,
                                                      [this]() -> void { on_latest(); },
                                                      chrome.card_line))},
                                 8.0F));
    }

    // ---- 留痕行：丢弃那句（只在 >0 时）+「不落盘」那句（常驻）----
    const auto dropped = notice();
    if (!dropped.empty()) {
        auto line = make_text(dropped, chrome.text_dim);
        line->modifier.set(aurora::Modifier{}.fill_max_width());
        children.push_back(node(std::move(line)));
    }
    auto privacy = make_text(settings_label("session.monitor.notice.privacy"), chrome.text_dim);
    privacy->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::move(privacy)));

    // ---- 行区：`LazyList` 虚拟化（D10①），只实例化可见窗口；条目内容按折出的窗口快照闭包 ----
    if (painted_rows_.empty()) {
        auto empty = make_text(settings_label("session.monitor.empty"), chrome.text_dim);
        empty->modifier.set(aurora::Modifier{}.fill_max_width().expand());
        children.push_back(node(std::move(empty)));
    } else {
        // 条目 builder 捕获**这一份**窗口（共享只读），而不是回头读 `painted_rows_`：浮层存活期间
        // 窗口若被下一页替换，已实例化条目与后进来条目才不会一半新一半旧（框架无条目刷新 API，
        // 见稿 §9.7 第 1 条）。
        auto window = std::make_shared<const std::vector<MonitorRow>>(painted_rows_);
        auto list = std::make_shared<aurora::LazyList>(
            static_cast<int>(painted_rows_.size()),
            [this, window](int index) -> aurora::Node {
                return build_row((*window)[static_cast<std::size_t>(index)]);
            },
            kMonitorRowExtent);
        list->set_cache_extent(200.0F);
        list->modifier.set(aurora::Modifier{}.fill_max_width().expand());
        // 落位：跟尾给内容高（布局后夹到末尾），冻结/暂停给原偏移（同一窗口坐标）。
        auto props = aurora::Json::object();
        props.set("scroll_offset", pending_scroll_offset_);
        list->deserialize_props(props);
        list_ = list;
        children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(list))));
    }

    // ---- 动作留痕行（稿 §10.3）：排在卡末，只在最近一次「取出去」之后在场 ----
    // 画的是 `painted_feedback_` 而不是 `feedback_`：与其余各行同一口径——浮层那一份只由 `paint_rows()`
    // 记账，动作腿当场改的是状态量，差一帧上屏（D8①／§4.5 第 25 条要的次序）。
    if (!painted_feedback_.empty()) {
        auto line = make_text(painted_feedback_, chrome.text_dim);
        line->modifier.set(aurora::Modifier{}.fill_max_width());
        children.push_back(node(std::move(line)));
    }

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(640.0F)  // D5①：54 字符 × 11 dp = 594 dp，加 12 dp 双侧内边距。
                           .fill_max_height()
                           .padding(aurora::EdgeInsets{12.0F, 12.0F, 12.0F, 12.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto SessionMonitorPanel::on_timestamp_changed(bool next) -> void {
    timestamp_prefix_ = next;
    rebuild_pending_ = true;  // 重建排在下一帧（头注那条代价）。
}

auto SessionMonitorPanel::on_pause_changed(bool next) -> void {
    paused_ = next;  // 不重建：画面停在按下那一刻，行区本来就是它该停的样子。
}

auto SessionMonitorPanel::on_text_view_changed(bool next) -> void {
    text_view_ = next;
    // 换视图档就是换行号坐标系（hex 档 8 字节/行 ↔ 文本档 36 字节/行）：跟尾态由 `follow_frame()` 按新档
    // 重算，停更态得显式重排（稿 D34）。与时间戳那枚同一条纪律——回调里不折行、不解码，重建排在下一帧。
    if (frozen_) {
        frozen_ = false;
        skip_geometry_once_ = true;  // 解除冻结＝回到这份料源的末尾（`on_latest()` 同一条腿）
    }
    if (paused_) {
        rebase_rows_ = true;  // 暂停说的是「别读新环」，不是「别换形状」：清一下也不读环
    }
    rebuild_pending_ = true;
}

auto SessionMonitorPanel::on_clear() -> void {
    if (hooks_.clear != nullptr) {
        hooks_.clear();
    }
    // 就地把手里的副本一起归零，而不是等下一帧重读环：`RawTap::clear()` 连丢弃累计一起清（那条
    // 口径在这里成立——清完那一屏就是新开始，再挂着「已丢弃 N 字节」是在说一屏看不到的数据）。
    // 暂停态也立刻看得见屏空了，故这一条腿不走 `read_snapshot()`。
    source_.clear();
    ring_bytes_ = 0U;
    dropped_bytes_ = 0U;
    feedback_.clear();  // 清了环，上一句「已导出/已复制」说的就不是这一屏了（稿 §10.3）。
    total_rows_ = 0U;
    fold_first_row_ = 0U;
    // 清空回到空态：离底判据在旧列表上必然为真（它还停在原偏移），首帧先跳过几何（D14① 的次序）。
    frozen_ = false;
    skip_geometry_once_ = true;
    rebuild_pending_ = true;
}

auto SessionMonitorPanel::on_latest() -> void {
    // 「回到最新」在语义上就是「我要看最新的」：顺手解暂停与冻结（D14①），并让下一帧直接重读环。
    // 这里不读环、不重建——回调里不做长计算的纪律与其余动作腿同。
    paused_ = false;
    frozen_ = false;
    skip_geometry_once_ = true;
    rebuild_pending_ = true;
}

auto SessionMonitorPanel::dump_text() const -> std::string {
    // 料源是手里的 `source_`（稿 §10.2 D20①）而不是重读环：暂停/冻结时屏面没变，交出去的东西也不该变。
    // 显示档取 `make_options()`——与画这一屏同一份，故文件/剪贴板里每一行都能在屏上逐字符找到（D19①）。
    return monitor_dump_text(source_, make_options());
}

auto SessionMonitorPanel::on_copy() -> void {
    const auto text = dump_text();
    if (text.empty()) {
        return;  // D21①：空环整条不跑——不碰剪贴板也不留痕，那句「等待数据」本来就在行区里说清了。
    }
    if (hooks_.copy_text != nullptr) {
        hooks_.copy_text(text);
    }
    feedback_ = settings_label("session.monitor.notice.copied", count_arg(total_rows_));
    rebuild_pending_ = true;  // 留痕上屏排在下一帧：本件唯一的重建入口是 `tick()`（D8① 的纪律）。
}

auto SessionMonitorPanel::on_export() -> void {
    // 三条接缝齐备是这枚按钮画出来的**前提**（`build_overlay()` 那侧的判据），故这里不再逐条问空——
    // 与 `SettingsPanel::run_export()` 同一条口径。
    const auto text = dump_text();
    if (text.empty()) {
        return;  // D21①：连对话框都不起——用户不该为一份空捕获挑文件名。
    }
    const std::string picked = hooks_.pick_export_path();
    if (picked.empty()) {
        return;  // 取消即整条不跑，**连留痕都不写**：上一句动作的话还在，说明这一次什么都没发生。
    }
    if (const auto reason = hooks_.export_text(picked, text); reason.has_value()) {
        // 存储侧的原因是 ASCII 诊断串，只进日志；上屏的永远是那一句「屏面未变」（同 S13② 的备份行）。
        AURORA_LOG_WARN("serial", "monitor export failed: ", *reason);
        feedback_ = settings_label("session.monitor.notice.export_failed");
        rebuild_pending_ = true;
        return;
    }
    // 路径是**取值**而不是字面量，故逐字上屏（`settings.config.exported` 同一条口径，裁决 7.76⑤）。
    feedback_ =
        settings_label("session.monitor.notice.exported",
                       {{"count", static_cast<std::int64_t>(total_rows_)}, {"path", picked}});
    rebuild_pending_ = true;
}

auto SessionMonitorPanel::clear_state() -> void {
    if (overlay_index_.has_value() && host_ != nullptr) {
        host_->remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    list_.reset();  // 先松手再拆树（与 `refresh_overlay` 同一条次序）。
    dialog_.reset();
    painted_rows_.clear();
    painted_binding_.clear();
    painted_ring_bytes_ = 0U;
    painted_dropped_bytes_ = 0U;
    painted_cap_bytes_ = 0U;
    painted_frozen_ = false;
    painted_feedback_.clear();
    feedback_.clear();
    source_.clear();
    ring_bytes_ = 0U;
    dropped_bytes_ = 0U;
    binding_.clear();
    // 编码腿名跟绑定同生死（稿 D28①：它是「那条会话」的属性，不是面板的档）；下一次 `follow_frame()`
    // 会随新绑定现取。三枚面板局部档不在这里复位——与另两枚同一条口径（D9①：复位只发生在进程重启）。
    encoding_ = "UTF-8";
    seen_revision_ = 0U;
    cap_bytes_ = session::kMaxRawTapBytes;
    total_rows_ = 0U;
    fold_first_row_ = 0U;
    pending_scroll_offset_ = 0.0F;
    frozen_ = false;
    rebuild_pending_ = false;
    rebase_rows_ = false;
    skip_geometry_once_ = false;
    open_ = false;
}

}  // namespace borealis::ui
