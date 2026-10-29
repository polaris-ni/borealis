// ============================================================
// 字节监视面板实现（src/ui/session_monitor_panel.cpp）
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

/// @brief 就地改写一枚已挂载 `Text` 的文本（原位刷新腿唯一的写入口）。
///
/// 走 `Text::set_content()` 而不是属性直写：展示属性在框架侧已收作私有来源，`set_content` 换源时
/// 会 `resubscribe_signals()` 把挂载期登记的观察边搬到新源上，并顺带两脏（布局 + 绘制）。
/// 旧形态（`content = s` 直写 `Reactive<T>` 属性字段）只换掉字段里那枚临时句柄的节点，挂载期
/// 登记的边仍留在旧节点上，写谁都不通知——那一格缺口（附录 A.2 的 G49）已由框架侧回货闭合。
auto set_cell_text(aurora::Text &cell, std::string content) -> void {
    cell.set_content(std::move(content));
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
    // 原位分支进不去（`list_` 还是空）——真正挂浮层的是下面这一句。
    frozen_ = false;
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
    // 这里**不再逐帧比几何**：冻结判据的唯一入口是行区落位通知（`on_scroll_changed()`，缺口 G46 回货
    // 后接上的腿）。原先每帧读 `scroll_offset()`/`max_scroll_offset()` 是因为没有通知面，而它带来的
    // 那一帧延迟——「滚到底那一帧立刻恢复」要等下一次泵（稿 §9.7 第 3 条登记的缺陷）——随之一起没了。
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

auto SessionMonitorPanel::on_scroll_changed() -> void {
    if (!open_) {
        return;  // 卡已收：旧列表残余的落位通知不驱动状态机（摘层与析构的次序不由本件担保）。
    }
    const bool now_frozen = compute_frozen();
    if (now_frozen == frozen_) {
        return;
    }
    frozen_ = now_frozen;
    rebuild_pending_ = true;  // 提示行与「回到最新」只在帧边界增删（D13①），回调里不建树
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
        // 第四枚同样是面板局部量（稿 §13 D46①）：它换的是**行种类**，环、采集、设置都不动。
        .boundary_rows = boundary_rows_,
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
    // 跟尾恒落底：交「末尾」这一份意图（`std::nullopt`），原位路径走 `scroll_to_end()`、重建路径在首次
    // 可滚动布局按当时的真末尾夹取（缺口 G46 回货后不必再自己算内容高塞给序列化面，见稿 §9.7 第 2 条）。
    paint_rows(std::move(rows), std::nullopt);
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
        paint_rows(std::move(tail), std::nullopt);  // 复位＝回到这份料源的末尾（D34：暂停不清，只是不读新环）
        return;
    }
    const auto available = total_rows_ > fold_first_row_ ? total_rows_ - fold_first_row_ : 0U;
    const auto window = std::min(available, kMonitorFoldWindowRows);
    auto rows = monitor_row_window(source_, options, fold_first_row_, window);
    // 冻结/暂停要保住用户当前读到的那一屏：同一只列表就地保位，重建时把旧偏移原样交给待定偏移。
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

auto SessionMonitorPanel::paint_rows(std::vector<MonitorRow> rows,
                                     std::optional<float> offset) -> void {
    painted_rows_ = std::move(rows);
    painted_binding_ = binding_;
    painted_ring_bytes_ = ring_bytes_;
    painted_dropped_bytes_ = dropped_bytes_;
    painted_cap_bytes_ = cap_bytes_;
    painted_frozen_ = frozen_;
    painted_feedback_ = feedback_;
    rebuild_pending_ = false;

    // 卡形状＝四枚条件行在不在 + 行区是列表还是那句空态。原位刷新只换**内容**，换不了节点，故先比形状：
    // 一样就原地走，不一样才拆卡重装（`build_overlay()` 那几处 `if` 与这里必须逐条对齐，否则形状判据
    // 与实际节点分叉——分叉的后果是「树上少一格而面板以为它在」，原位刷新会把文本写进已死的句柄）。
    const CardShape next_shape{
        .has_rows = !painted_rows_.empty(),
        .binding = !painted_binding_.empty(),
        .dropped = painted_dropped_bytes_ != 0U,
        .feedback = !painted_feedback_.empty(),
        .frozen = painted_frozen_,
    };
    if (painted_shape_.has_value() && *painted_shape_ == next_shape && list_ != nullptr &&
        rows_window_ != nullptr) {
        *rows_window_ = painted_rows_;  // 行区 builder 与面板共持这一份：换内容而不换节点
        const auto want = static_cast<int>(painted_rows_.size());
        if (list_->count() != want) {
            list_->set_count(want);  // 只改总数：范围内存活条目不重建，越界的当场回收（偏移随之夹取）
        }
        refresh_text_cells();          // 标题行与两枚留痕就地改写
        list_->invalidate_items();     // 数据变了而行还是旧的：显式失效，下一帧按当前窗口重建
        // 落位：`nullopt`＝这份窗口的末尾（跟尾），有值＝保住那个偏移（停更态滑窗）。两条都直接在
        // 已布局的列表上走，不再经序列化面。
        if (offset.has_value()) {
            list_->scroll_to(*offset, false);
        } else {
            list_->scroll_to_end(false);
        }
        return;
    }
    painted_shape_ = next_shape;
    pending_scroll_offset_ = offset;
    refresh_overlay();  // 形状变了（或卡还没建起来）：唯一的重装入口
}

auto SessionMonitorPanel::refresh_text_cells() -> void {
    // 四格都是「只换文本」：句柄在 `build_overlay()` 里随节点一起记下，缺席的那格句柄为空即跳过
    // （缺席＝这一格根本不在树上，形状判据已保证只有在场时才走到这里）。
    if (bytes_cell_ != nullptr) {
        set_cell_text(*bytes_cell_,
                      settings_label("session.monitor.bytes", count_arg(painted_ring_bytes_)));
    }
    if (binding_cell_ != nullptr) {
        set_cell_text(*binding_cell_, painted_binding_);
    }
    if (dropped_cell_ != nullptr) {
        set_cell_text(*dropped_cell_, notice());  // 现算：上限换档时这一句自己跟着走（D16①）
    }
    if (feedback_cell_ != nullptr) {
        set_cell_text(*feedback_cell_, painted_feedback_);
    }
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
    mask->set_modifier(aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));
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

auto SessionMonitorPanel::drop_card_handles() -> void {
    // 六枚句柄都指向**当前那张卡**里的节点：原位刷新靠它们就地改写，卡一拆它们就是悬垂的外部持有。
    list_.reset();
    rows_window_.reset();
    bytes_cell_.reset();
    binding_cell_.reset();
    dropped_cell_.reset();
    feedback_cell_.reset();
}

auto SessionMonitorPanel::refresh_overlay() -> void {
    if (!open_ || !overlay_index_.has_value() || host_ == nullptr) {
        return;
    }
    ++overlay_rebuilds_;  // 观测面：拆卡重装的次数——原位刷新接好后，它只该随**形状**变化而增长
    host_->remove_overlay(*overlay_index_);
    overlay_index_.reset();
    // 先松手再拆树：框架在容器析构时发现**子控件仍被外部持有**会 WARN（`layout parent detached while
    // the child widget is still alive`），而原位刷新留下的行区与四格句柄正是那份外部持有。次序＝
    // 句柄 → 树，拆树才回到「子节点随本容器即刻销毁」那条静默的正常路径。
    drop_card_handles();
    dialog_.reset();
    install_overlay();
}

[[nodiscard]] auto SessionMonitorPanel::build_row(const MonitorRow &row) -> aurora::Node {
    const auto chrome = settings_chrome();
    if (row.kind == MonitorRowKind::Boundary) {
        // 分隔行**整行**走次要色，且只有一格：`RX`／`TX` 已经在那句里，强调色这一列本期不给它（稿 §13
        // D47／F26「强调色只吃方向那一格」的既有形态不为新增行种类让路）。`row_text()` 在这里就等于
        // `dump`（前缀与方向格恒空），故观测面与显示串仍同源。
        return row_of(std::vector<aurora::Node>{node(mono_cell(row.dump, chrome.text_dim))}, 0.0F);
    }
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
    // 每次建树都重记一遍原位刷新的句柄：缺席的格必须落回 `nullptr`，否则上一张卡的句柄会在原位路径上
    // 被写进一棵已死的树（形状判据只看「在不在」，看不出「在的是哪一棵」）。
    bytes_cell_.reset();
    binding_cell_.reset();
    dropped_cell_.reset();
    feedback_cell_.reset();
    rows_window_.reset();

    // ---- 标题行：字节监视 + 绑定（档案名与连接目标）+ 字节计数 + 清空 + 关闭 ----
    // 与隧道面板同一条实测教训：标题不抓 fill_max_width，否则计数与两枚按钮整组被挤出卡宽（派发链上
    // 点不到）。绑定失去身份（标签或焦点格散了）就整格不画，而不是画一句「未知」。
    auto header_children = std::vector<aurora::Node>{
        node(make_text(settings_label("session.monitor.title"), chrome.text))};
    if (!painted_binding_.empty()) {
        auto binding = make_text(painted_binding_, chrome.text_dim);
        binding_cell_ = binding;
        header_children.push_back(node(binding));
    }
    auto bytes =
        make_text(settings_label("session.monitor.bytes", count_arg(painted_ring_bytes_)),
                  chrome.text_dim);
    bytes_cell_ = bytes;
    header_children.push_back(node(std::move(bytes)));
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

    // ---- 开关行：时间戳前缀（缺省开）+ 暂停刷新（缺省关）+ 文本视图（缺省关，稿 D26①）
    //      + 报文边界（缺省关，稿 §13 D46①）----
    // 新增的一枚**排在最后**：集成用例按 `find("Switch", N)` 的树序取件，前面几枚的序号不能因为新增而挪位。
    auto timestamp_switch = std::make_shared<aurora::Switch>(aurora::State<bool>{timestamp_prefix_});
    timestamp_switch->set_on_changed(
        [this](bool next) -> void { on_timestamp_changed(next); });
    auto pause_switch = std::make_shared<aurora::Switch>(aurora::State<bool>{paused_});
    pause_switch->set_on_changed([this](bool next) -> void { on_pause_changed(next); });
    auto text_switch = std::make_shared<aurora::Switch>(aurora::State<bool>{text_view_});
    text_switch->set_on_changed([this](bool next) -> void { on_text_view_changed(next); });
    auto boundary_switch = std::make_shared<aurora::Switch>(aurora::State<bool>{boundary_rows_});
    boundary_switch->set_on_changed([this](bool next) -> void { on_boundary_changed(next); });
    for (auto &sw : {timestamp_switch, pause_switch, text_switch, boundary_switch}) {
        // 锁高的理由与设置面板开关同款：滑块直径按自身盒高算，行交叉轴居中给的是紧约束。
        sw->set_modifier(aurora::Modifier{}.height(24.0F));
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
                             node(std::static_pointer_cast<aurora::Widget>(std::move(text_switch))),
                             node(make_text(settings_label("session.monitor.toggle.boundary"),
                                            chrome.text_dim)),
                             node(std::static_pointer_cast<aurora::Widget>(std::move(boundary_switch)))},
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
        line->set_modifier(aurora::Modifier{}.fill_max_width());
        dropped_cell_ = line;
        children.push_back(node(std::move(line)));
    }
    auto privacy = make_text(settings_label("session.monitor.notice.privacy"), chrome.text_dim);
    privacy->set_modifier(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::move(privacy)));

    // ---- 行区：`LazyList` 虚拟化（D10①），只实例化可见窗口；条目内容按折出的窗口快照闭包 ----
    if (painted_rows_.empty()) {
        auto empty = make_text(settings_label("session.monitor.empty"), chrome.text_dim);
        empty->set_modifier(aurora::Modifier{}.fill_max_width().expand());
        children.push_back(node(std::move(empty)));
    } else {
        // 条目 builder 捕获**这一份**窗口（与面板共持、可就地替换内容），而不是回头读 `painted_rows_`：
        // 原位刷新换的是这份向量的内容，节点与 builder 都不动（缺口 G46 回货后不必再靠整卡重建保证
        // 「后进来的条目与已实例化的条目是同一份数据」，见稿 §9.7 第 1 条）。
        rows_window_ = std::make_shared<std::vector<MonitorRow>>(painted_rows_);
        auto list = std::make_shared<aurora::LazyList>(
            static_cast<int>(painted_rows_.size()),
            [this, window = rows_window_](int index) -> aurora::Node {
                return build_row((*window)[static_cast<std::size_t>(index)]);
            },
            kMonitorRowExtent);
        list->set_cache_extent(200.0F);
        list->set_modifier(aurora::Modifier{}.fill_max_width().expand());
        // 冻结判据的入口：偏移一落定就问一次几何（原先靠 `tick()` 逐帧轮询，见 `on_scroll_changed()`）。
        list->set_on_scroll_changed([this](float) -> void { on_scroll_changed(); });
        // 落位：`nullopt`＝跟尾，向控件要「末尾」这一份意图（未布局时记进待定偏移，首次可滚动布局
        // 夹到当时的真末尾）；有值＝停更态保住原偏移，仍经序列化面的待定偏移交进去——框架只给了
        // 末尾锚定的具名入口，任意偏移的「布局前」通路仍只有 `deserialize_props`（稿 §9.7 第 2 条就此收窄）。
        if (pending_scroll_offset_.has_value()) {
            auto props = aurora::Json::object();
            props.set("scroll_offset", *pending_scroll_offset_);
            list->deserialize_props(props);
        } else {
            list->scroll_to_end();
        }
        list_ = list;
        children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(list))));
    }

    // ---- 动作留痕行（稿 §10.3）：排在卡末，只在最近一次「取出去」之后在场 ----
    // 画的是 `painted_feedback_` 而不是 `feedback_`：与其余各行同一口径——浮层那一份只由 `paint_rows()`
    // 记账，动作腿当场改的是状态量，差一帧上屏（D8①／§4.5 第 25 条要的次序）。
    if (!painted_feedback_.empty()) {
        auto line = make_text(painted_feedback_, chrome.text_dim);
        line->set_modifier(aurora::Modifier{}.fill_max_width());
        feedback_cell_ = line;
        children.push_back(node(std::move(line)));
    }

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->set_modifier(aurora::Modifier{}
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
        frozen_ = false;  // 解除冻结＝回到这份料源的末尾（`on_latest()` 同一条腿）：下一次落位由新
                          // 列表自己报回来，不需要「先跳过一帧几何」那类通融
    }
    if (paused_) {
        rebase_rows_ = true;  // 暂停说的是「别读新环」，不是「别换形状」：清一下也不读环
    }
    rebuild_pending_ = true;
}

auto SessionMonitorPanel::on_boundary_changed(bool next) -> void {
    boundary_rows_ = next;
    // 边界档换的是**行种类**，但同样换行号坐标系：分隔行进的是 `total_rows_` 与折带窗口，沿用旧行号
    // 指向的是别段历史（稿 §13 F24／二期 D34 那条口径的同族）。故走与文本视图那枚完全相同的重排腿，
    // 而不是时间戳那枚（前缀不进计数，行号不动）。回调里不折行，重建排在下一帧。
    if (frozen_) {
        frozen_ = false;  // 解除冻结＝回到这份料源的末尾（`on_latest()` 同一条腿）
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
    // 清空回到空态：行区换成那句空态就是形状变化，必然走重装；旧列表停在原偏移也不会把刚落底的画面
    // 判回冻结——判据只由落定通知驱动（D14① 的落地次序在 G46 回货后由「跳过一帧」改成「无轮询」）。
    frozen_ = false;
    rebuild_pending_ = true;
}

auto SessionMonitorPanel::on_latest() -> void {
    // 「回到最新」在语义上就是「我要看最新的」：顺手解暂停与冻结（D14①），并让下一帧直接重读环。
    // 这里不读环、不重建——回调里不做长计算的纪律与其余动作腿同。
    paused_ = false;
    frozen_ = false;
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
    // 原位刷新的那五枚句柄随卡一起失效，且**须在拆树前**还掉（同 `refresh_overlay` 那条次序）：留着
    // 它们，容器析构会报「子控件仍被外部持有」；而留着 `painted_shape_` 更坏——下一次开卡会把「上一张
    // 卡的格」当成本帧该改写的格子（判据与节点一起回到「卡不在场」才对）。
    drop_card_handles();
    painted_shape_.reset();
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
    // 会随新绑定现取。面板那几枚局部档不在这里复位——同一条口径（D9①：复位只发生在进程重启）。
    encoding_ = "UTF-8";
    seen_revision_ = 0U;
    cap_bytes_ = session::kMaxRawTapBytes;
    total_rows_ = 0U;
    fold_first_row_ = 0U;
    pending_scroll_offset_ = std::nullopt;
    frozen_ = false;
    rebuild_pending_ = false;
    rebase_rows_ = false;
    open_ = false;
}

}  // namespace borealis::ui
