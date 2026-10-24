// ============================================================
// 串口十六进制收发监视面板实现（src/ui/serial_monitor_panel.cpp）
// ------------------------------------------------------------
// 形态对照设计稿 `codespec/UI_SERIAL_MONITOR.draft.md` 的标号（§2 那张屏面表）：640 dp 停靠卡片
// （D5①，54 字符/行）、两枚面板局部开关（D6①／D7①）、清空与关闭（D7①：清空是本期唯一的止血动作）、
// 丢弃留痕（D4①）、不落盘声明（§1 第一条安全口径）、等宽行区（F8／F9）。
//
// 三条动作腿（时间戳档、暂停档、清空）**只改状态、不当场重建**：重建唯一发生在 `tick()`，于是
// 「事件回调里不做长计算」（AGENTS §4.5 第 25 条）在本件是结构性的而不是约定——一帧的代价上限就是
// F5 那一句（24 行 ≈ 6 µs），而它落在帧边界而不是点击派发里。代价：拨档到看见最多差一帧（面板族里
// `TunnelPanel::tick()` 同形）。
//
// 行文本在这里只存在一瞬间：环里存的是原始 run（D3①），本件收的是**截过尾**的那一段（头注
// `kMonitorTailBytes`），折出的行表只用于 diff 与绘制；关掉浮层那些字符串就没了，不落盘、不进日志。
// ============================================================

#include "serial_monitor_panel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "aurora/widget/alignment.h"
#include "aurora/widget/button.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/scroll.h"
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
[[nodiscard]] auto count_arg(std::size_t value) -> std::vector<aurora::LocalizedString> {
    return {aurora::LocalizedString{std::to_string(value)}};
}

/// @brief 从环尾取至多 `budget` 个字节，**整条 run 为单位**，只可能切开最旧那一条。
///
/// 截在 run 边界之内而不是之外，是为了让预算这件事有上界：串口一次突发可以是 32 KiB 一个 run
/// （稿 F3／F10），只按条取就会把 32 KiB 全折成 4096 行再丢掉 4072 行。被切开的那条头部字节本来
/// 就落在「最近若干行」之外，方向与时刻仍取该 run 的那一个点（D6① 的 run 级单点时间戳）。
[[nodiscard]] auto tail_runs(const std::vector<session::RawRun> &runs, std::size_t budget)
    -> std::vector<session::RawRun> {
    std::size_t held = 0U;
    for (const auto &run : runs) {
        held += run.bytes.size();
    }
    if (held <= budget) {
        return runs;
    }
    std::size_t skip = held - budget;
    auto kept = std::vector<session::RawRun>{};
    kept.reserve(runs.size());
    for (const auto &run : runs) {
        if (skip >= run.bytes.size()) {
            skip -= run.bytes.size();
            continue;
        }
        if (skip > 0U) {
            auto sliced = session::RawRun{
                .direction = run.direction,
                .timestamp_ms = run.timestamp_ms,
                .bytes = std::vector<std::byte>(run.bytes.begin() +
                                                    static_cast<std::ptrdiff_t>(skip),
                                                run.bytes.end()),
            };
            skip = 0U;
            kept.push_back(std::move(sliced));
            continue;
        }
        kept.push_back(run);
    }
    return kept;
}

}  // namespace

SerialMonitorPanel::SerialMonitorPanel(aurora::OverlayHost &host, Hooks hooks)
    : host_{&host}, hooks_{std::move(hooks)} {}

// 先摘后换（头文件的判据）：`close()` 摘的是**旧宿主**上自己记的那枚序号，之后浮层归零。环不动——
// 留痕活在那条会话的 `Session` 里，本件只是它的一份投影，所以搬窗既不停采也不清屏（重开看到的是历史）。
auto SerialMonitorPanel::set_overlay_host(aurora::OverlayHost &host) -> void {
    if (host_ == &host) {
        return;
    }
    close();
    host_ = &host;
}

SerialMonitorPanel::~SerialMonitorPanel() {
    close();
}

auto SerialMonitorPanel::open() -> void {
    if (open_) {
        // 已开着：只跟一帧数。命令重复唤起不该把浮层拆了重挂（面板族同款口径，`TunnelPanel::open()`）。
        apply(read_snapshot());
        return;
    }
    apply(read_snapshot());  // 先折一次，让首帧就有内容与计数，而不是空态加一句「等一下」。
    open_ = true;
    install_overlay();
}

auto SerialMonitorPanel::close() -> void {
    if (!open_) {
        return;
    }
    clear_state();
}

auto SerialMonitorPanel::tick() -> void {
    if (!open_) {
        return;  // 没开就没有泵的事：环照人写，本件不在场时零成本（D7① 的常驻代价在会话侧）。
    }
    if (!paused_) {
        apply(read_snapshot());
        return;
    }
    if (repaint_pending_) {
        maybe_refresh();  // 暂停中改了显示档或清了环：按已有的那段字节重画，不读新数据。
    }
}

auto SerialMonitorPanel::read_snapshot() const -> Snapshot {
    return hooks_.snapshot != nullptr ? hooks_.snapshot() : Snapshot{};
}

auto SerialMonitorPanel::apply(const Snapshot &snapshot) -> void {
    source_ = tail_runs(snapshot.runs, kMonitorTailBytes);
    ring_bytes_ = 0U;
    for (const auto &run : snapshot.runs) {
        ring_bytes_ += run.bytes.size();  // 标题那枚计数说的是**环里现存**的字节，不是画出来的那 192 个。
    }
    dropped_bytes_ = snapshot.dropped_bytes;
    binding_ = hooks_.binding_label != nullptr ? hooks_.binding_label() : std::string{};
    maybe_refresh();
}

auto SerialMonitorPanel::maybe_refresh() -> void {
    repaint_pending_ = false;
    const auto rows = fold_source();
    if (rows == painted_rows_ && binding_ == painted_binding_ && ring_bytes_ == painted_ring_bytes_ &&
        dropped_bytes_ == painted_dropped_bytes_) {
        return;  // 逐字段相等就不重建（F14 那条状态泵纪律）。
    }
    painted_rows_ = rows;
    painted_binding_ = binding_;
    painted_ring_bytes_ = ring_bytes_;
    painted_dropped_bytes_ = dropped_bytes_;
    refresh_overlay();
}

auto SerialMonitorPanel::fold_source() const -> std::vector<MonitorRow> {
    const auto options = MonitorRowOptions{
        .bytes_per_row = kMonitorBytesPerRow,
        .timestamp_prefix = timestamp_prefix_,
    };
    auto rows = monitor_rows(source_, options);
    if (rows.size() > kMonitorVisibleRowCap) {
        // 留最后若干行：行区是跟尾视图，最旧那些行画了也在屏外（稿 §7 的「滚动回溯在册」）。
        rows.erase(rows.begin(),
                   rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - kMonitorVisibleRowCap));
    }
    return rows;
}

auto SerialMonitorPanel::visible_rows() const -> std::vector<MonitorRow> {
    return fold_source();
}

auto SerialMonitorPanel::notice() const -> std::string {
    if (dropped_bytes_ == 0U) {
        return {};
    }
    // 丢弃必须上屏：不报这一句，「环里最早那行」就会被当成「设备最早发的那行」（D4①）。
    return settings_label("serial.monitor.notice.dropped",
                          {aurora::LocalizedString{std::to_string(dropped_bytes_)},
                           aurora::LocalizedString{
                               std::to_string(session::kMaxRawTapBytes / 1024U)}});
}

auto SerialMonitorPanel::install_overlay() -> void {
    if (!open_ || host_ == nullptr) {
        return;
    }
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

auto SerialMonitorPanel::refresh_overlay() -> void {
    if (!open_ || !overlay_index_.has_value() || host_ == nullptr) {
        return;
    }
    host_->remove_overlay(*overlay_index_);
    overlay_index_.reset();
    dialog_.reset();
    install_overlay();
}

[[nodiscard]] auto SerialMonitorPanel::build_row(const MonitorRow &row) -> aurora::Node {
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

[[nodiscard]] auto SerialMonitorPanel::build_overlay() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};

    // ---- 标题行：串口监视 + 绑定（档案名与设备路径）+ 字节计数 + 清空 + 关闭 ----
    // 与隧道面板同一条实测教训：标题不抓 fill_max_width，否则计数与两枚按钮整组被挤出卡宽（派发链上
    // 点不到）。绑定失去身份（标签散了／非串口）就整格不画，而不是画一句「未知」。
    auto header_children = std::vector<aurora::Node>{
        node(make_text(settings_label("serial.monitor.title"), chrome.text))};
    if (!painted_binding_.empty()) {
        header_children.push_back(node(make_text(painted_binding_, chrome.text_dim)));
    }
    header_children.push_back(
        node(make_text(settings_label("serial.monitor.bytes", count_arg(painted_ring_bytes_)),
                       chrome.text_dim)));
    if (hooks_.clear != nullptr) {
        header_children.push_back(node(make_button(settings_label("serial.monitor.action.clear"),
                                                   chrome.control_bg, chrome.text_dim, 48.0F,
                                                   [this]() -> void { on_clear(); },
                                                   chrome.card_line)));
    }
    header_children.push_back(node(make_button(settings_label("serial.monitor.action.close"),
                                               chrome.control_bg, chrome.text_dim, 48.0F,
                                               [this]() -> void { close(); }, chrome.card_line)));
    children.push_back(row_of(std::move(header_children), 8.0F));

    // ---- 开关行：时间戳前缀（缺省开）+ 暂停刷新（缺省关）----
    auto timestamp_switch = std::make_shared<aurora::Switch>(aurora::Reactive<bool>{timestamp_prefix_});
    timestamp_switch->set_on_changed(
        [this](bool next) -> void { on_timestamp_changed(next); });
    auto pause_switch = std::make_shared<aurora::Switch>(aurora::Reactive<bool>{paused_});
    pause_switch->set_on_changed([this](bool next) -> void { on_pause_changed(next); });
    for (auto &sw : {timestamp_switch, pause_switch}) {
        // 锁高的理由与设置面板开关同款：滑块直径按自身盒高算，行交叉轴居中给的是紧约束。
        sw->modifier.set(aurora::Modifier{}.height(24.0F));
        sw->set_active_color(chrome.accent);
        sw->set_inactive_color(chrome.control_bg);
        sw->set_border(chrome.card_line, 1.0F);
        sw->set_thumb_color(chrome.text);
    }
    children.push_back(row_of(std::vector<aurora::Node>{
                             node(make_text(settings_label("serial.monitor.switch.timestamp"),
                                            chrome.text_dim)),
                             node(std::static_pointer_cast<aurora::Widget>(std::move(timestamp_switch))),
                             node(make_text(settings_label("serial.monitor.switch.pause"),
                                            chrome.text_dim)),
                             node(std::static_pointer_cast<aurora::Widget>(std::move(pause_switch)))},
                         8.0F));

    // ---- 留痕行：丢弃那句（只在 >0 时）+「不落盘」那句（常驻）----
    const auto dropped = notice();
    if (!dropped.empty()) {
        auto line = make_text(dropped, chrome.text_dim);
        line->modifier.set(aurora::Modifier{}.fill_max_width());
        children.push_back(node(std::move(line)));
    }
    auto privacy = make_text(settings_label("serial.monitor.notice.privacy"), chrome.text_dim);
    privacy->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::move(privacy)));

    // ---- 行区：每行一条 8 字节转储（等宽族，稿 F8／F9）----
    auto rows_column = std::make_shared<aurora::Column>(aurora::ColumnProps{.gap = 2.0F});
    for (const auto &row : painted_rows_) {
        rows_column->add(build_row(row));
    }
    if (painted_rows_.empty()) {
        rows_column->add(node(make_text(settings_label("serial.monitor.empty"), chrome.text_dim)));
    }
    auto scroll = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
        .child = node(std::static_pointer_cast<aurora::Widget>(std::move(rows_column))),
        .step = 32.0F,
    });
    scroll->modifier.set(aurora::Modifier{}.fill_max_width().expand());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(scroll))));

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

auto SerialMonitorPanel::on_timestamp_changed(bool next) -> void {
    timestamp_prefix_ = next;
    repaint_pending_ = true;  // 重建排在下一帧；`visible_rows()` 已经吃到新档（头注那条代价）。
}

auto SerialMonitorPanel::on_pause_changed(bool next) -> void {
    paused_ = next;  // 不重建：画面停在按下那一刻，行区本来就是它该停的样子。
}

auto SerialMonitorPanel::on_clear() -> void {
    if (hooks_.clear != nullptr) {
        hooks_.clear();
    }
    // 就地把手里的副本一起归零，而不是等下一帧重读环：`RawTap::clear()` 连丢弃累计一起清（那条
    // 口径在这里成立——清完那一屏就是新开始，再挂着「已丢弃 N 字节」是在说一屏看不到的数据）。
    // 暂停态也立刻看得见屏空了，故这一条腿不走 `read_snapshot()`。
    source_.clear();
    ring_bytes_ = 0U;
    dropped_bytes_ = 0U;
    repaint_pending_ = true;
}

auto SerialMonitorPanel::clear_state() -> void {
    if (overlay_index_.has_value() && host_ != nullptr) {
        host_->remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    painted_rows_.clear();
    painted_binding_.clear();
    painted_ring_bytes_ = 0U;
    painted_dropped_bytes_ = 0U;
    source_.clear();
    ring_bytes_ = 0U;
    dropped_bytes_ = 0U;
    binding_.clear();
    repaint_pending_ = false;
    open_ = false;
}

}  // namespace borealis::ui
