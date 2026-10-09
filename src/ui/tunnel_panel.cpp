// ============================================================
// SSH 隧道管理面板实现（src/ui/tunnel_panel.cpp）
// ------------------------------------------------------------
// 形态要点（对照设计稿判据标号，裁决 7.97）：左侧 520 dp 停靠卡片（标号 1），
// 两行堆叠行（标号 2：首行 徽标+名称+状态+启停/编辑/删除，次行 监听→目标+
// 承载档案），五态色与 Backoff/Failed 注记（标号 3），-R 0 端口注记（标号 4），
// 自建编辑对话框＝类型卡定字段集+autostart 开关+重试三字段（标号 5），删除
// 两段式（标号 6）。行表逐帧由 tunnel_rows() 合成，与上一帧相等就不重建
//（D6①；编辑器/确认框在场时同样不重建，避免打断输入）。
// 「Hooks 缺席的入口整枚不画」口径同侧栏（7.38⑥ F-b）。
// ============================================================

#include "tunnel_panel.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

#include "aurora/widget/alignment.h"
#include "aurora/widget/button.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/stack.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"

#include "conn/tunnel_model.h"  // validate_tunnel_spec / specs_conflict / state_is_running
#include "settings_i18n.h"
#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

constexpr std::array<conn::TunnelKind, 3> kKindCycle{
    conn::TunnelKind::Local, conn::TunnelKind::Remote, conn::TunnelKind::Dynamic};

[[nodiscard]] auto kind_key(conn::TunnelKind kind) -> std::string_view {
    switch (kind) {
        case conn::TunnelKind::Local:
            return "tunnel.kind.local";
        case conn::TunnelKind::Remote:
            return "tunnel.kind.remote";
        case conn::TunnelKind::Dynamic:
            return "tunnel.kind.dynamic";
    }
    return "tunnel.kind.local";
}

[[nodiscard]] auto state_color(conn::TunnelState state, const SettingsChrome &chrome)
    -> aurora::Color {
    switch (state) {
        case conn::TunnelState::Stopped:
            return chrome.text_dim;
        case conn::TunnelState::Dialing:
            return aurora::colors::AURORA_BLUE;
        case conn::TunnelState::Active:
            return aurora::colors::AURORA_GREEN;
        case conn::TunnelState::Backoff:
            return aurora::colors::AURORA_YELLOW;
        case conn::TunnelState::Failed:
            return aurora::colors::AURORA_RED;
    }
    return chrome.text_dim;
}

[[nodiscard]] auto parse_int(const std::string &text, int fallback) -> int {
    if (text.empty()) {
        return fallback;
    }
    const char *begin = text.c_str();
    char *end = nullptr;
    const long value = std::strtol(begin, &end, 10);
    if (end == begin || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(value);
}

[[nodiscard]] auto make_text(std::string content, aurora::Color color)
    -> std::shared_ptr<aurora::Text> {
    return std::make_shared<aurora::Text>(
        aurora::TextProps{.content = std::move(content), .text_color = color});
}

[[nodiscard]] auto make_button(std::string label, aurora::Color bg, aurora::Color fg,
                               float min_width, std::function<void()> on_click,
                               aurora::Color line, float line_width = 1.0F)
    -> std::shared_ptr<aurora::Button> {
    auto button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = std::move(label),
        .color = bg,
        .on_color = fg,
        .border_color = line,
        .border_width = line_width,
        .min_width = min_width,
    });
    button->set_on_click(std::move(on_click));
    return button;
}

[[nodiscard]] auto node(std::shared_ptr<aurora::Widget> widget) -> aurora::Node {
    return aurora::Node{std::move(widget)};
}

/// @brief 「标签 + 控件 + 标签 + 控件」一行（监听/目标两栏并排，控对话框总高）。
[[nodiscard]] auto pair_row(std::string label_a, aurora::Node control_a, std::string label_b,
                            aurora::Node control_b) -> aurora::Node {
    const auto chrome = settings_chrome();
    auto text_a = make_text(std::move(label_a), chrome.text_dim);
    auto text_b = make_text(std::move(label_b), chrome.text_dim);
    return node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::move(text_a)), std::move(control_a), node(std::move(text_b)),
                     std::move(control_b)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 6.0F,
    }));
}

[[nodiscard]] auto make_input(float width) -> std::shared_ptr<aurora::TextInput> {
    const auto chrome = settings_chrome();
    auto input = std::make_shared<aurora::TextInput>();
    input->set_background(chrome.control_bg);
    input->set_border_color(chrome.card_line);
    input->set_focused_border_color(chrome.accent);
    input->set_text_color(chrome.text);
    input->modifier.set(aurora::Modifier{}.width(width));
    return input;
}

/// @brief 「标签 + 控件」一行（同 wizard 的 make_field_row 形态；标签列宽一致）。
[[nodiscard]] auto field_row(std::string label, aurora::Node control) -> aurora::Node {
    auto label_text = make_text(std::move(label), settings_chrome().text_dim);
    label_text->modifier.set(aurora::Modifier{}.width(110.0F));
    return node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::move(label_text)), std::move(control)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    }));
}

/// @brief 新建隧道的稳定 id：按展示名派生，撞名就加后缀。id 是快照与启停的记账键，
///        两条同名定义共用一个 id 会让后保存的那条静默改写前一条（列表少一条、
///        运行态还会串台）。
[[nodiscard]] auto next_tunnel_id(const std::vector<conn::TunnelSpec> &specs,
                                  const std::string &name) -> std::string {
    const std::string base = "tunnel:" + name;
    std::string candidate = base;
    for (int suffix = 2;
         std::any_of(specs.begin(), specs.end(),
                     [&](const auto &spec) { return spec.id == candidate; });
         ++suffix) {
        candidate = base + "#" + std::to_string(suffix);
    }
    return candidate;
}

}  // namespace

TunnelPanel::TunnelPanel(aurora::OverlayHost &host, Hooks hooks)
    : host_{host}, hooks_{std::move(hooks)} {}

TunnelPanel::~TunnelPanel() {
    close_editor();
    close_delete_confirm();
    clear_state();
}

auto TunnelPanel::open() -> void {
    if (open_) {
        refresh_overlay();
        return;
    }
    specs_ = hooks_.load != nullptr ? hooks_.load() : std::vector<conn::TunnelSpec>{};
    runtimes_ = hooks_.snapshot != nullptr ? hooks_.snapshot()
                                           : std::map<std::string, TunnelRuntime>{};
    notice_.clear();
    painted_rows_ = tunnel_rows(specs_, runtimes_);

    auto chrome = settings_chrome();
    auto mask = std::make_shared<aurora::Canvas>(
        [c = chrome](aurora::Painter &painter, const aurora::Rect &box) -> void {
            painter.fill_rect(box, c.window_bg);
        });
    mask->modifier.set(
        aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));
    auto card = build_overlay();
    auto layer = std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{node(std::static_pointer_cast<aurora::Widget>(std::move(mask))),
                                  node(std::static_pointer_cast<aurora::Widget>(std::move(card)))},
        aurora::Alignment::CenterLeft);
    dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(layer))));
    overlay_index_ = host_.add_overlay(node(std::static_pointer_cast<aurora::Widget>(dialog_)));
    dialog_->show();
    open_ = true;
}

auto TunnelPanel::close() -> void {
    if (!open_) {
        return;
    }
    close_editor();
    close_delete_confirm();
    if (dialog_ != nullptr) {
        dialog_->close();
    }
    clear_state();
}

auto TunnelPanel::tick() -> void {
    if (!open_) {
        return;
    }
    if (hooks_.snapshot != nullptr) {
        runtimes_ = hooks_.snapshot();
    }
    const auto rows = tunnel_rows(specs_, runtimes_);
    if (rows == painted_rows_) {
        return;
    }
    // 编辑器/确认框在场时不重建主卡片：浮层次序会变、正在输入的草稿也会被打断。
    // painted_rows_ 留在旧值，下一对帧自然补重建（状态是可覆盖的量，不丢事实）。
    if (editor_open_ || delete_confirm_open_) {
        return;
    }
    painted_rows_ = rows;
    refresh_overlay();
}

auto TunnelPanel::visible_rows() const -> std::vector<TunnelRow> {
    return tunnel_rows(specs_, runtimes_);
}

auto TunnelPanel::refresh_overlay() -> void {
    if (!open_ || !overlay_index_.has_value()) {
        return;
    }
    host_.remove_overlay(*overlay_index_);
    overlay_index_.reset();
    dialog_.reset();
    const auto chrome = settings_chrome();
    auto mask = std::make_shared<aurora::Canvas>(
        [c = chrome](aurora::Painter &painter, const aurora::Rect &box) -> void {
            painter.fill_rect(box, c.window_bg);
        });
    mask->modifier.set(
        aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));
    auto card = build_overlay();
    auto layer = std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{node(std::static_pointer_cast<aurora::Widget>(std::move(mask))),
                                  node(std::static_pointer_cast<aurora::Widget>(std::move(card)))},
        aurora::Alignment::CenterLeft);
    dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(layer))));
    overlay_index_ = host_.add_overlay(node(std::static_pointer_cast<aurora::Widget>(dialog_)));
    dialog_->show();
}

auto TunnelPanel::build_row(const TunnelRow &row) -> aurora::Node {
    const auto chrome = settings_chrome();
    const auto running = conn::state_is_running(row.runtime.state);

    // ---- 首行：徽标 + 名称 + 状态 + 启停/编辑/删除 ----
    auto badge = make_text(std::string{tunnel_kind_badge(row.spec.kind)}, chrome.accent);
    auto name = make_text(row.spec.name, chrome.text);
    name->modifier.set(aurora::Modifier{}.width(110.0F));
    auto state = make_text(settings_label(tunnel_state_key(row.runtime.state)),
                           state_color(row.runtime.state, chrome));
    auto line1_children = std::vector<aurora::Node>{
        node(std::move(badge)), node(std::move(name)), node(std::move(state))};

    if (row.runtime.state == conn::TunnelState::Backoff) {
        // 标号 3：Backoff 行内「原因 + 第 N 次重试 · Xs 后」。（宽度钳住：注记不参与
        // 行首挤压，溢出交给 Text 自己的 Clip——行右侧还有四枚按钮要点得到。）
        auto reason = settings_label(tunnel_error_key(row.runtime.error));
        auto wait = settings_label(
            "tunnel.row.retry_wait",
            {aurora::LocalizedString{std::to_string(row.runtime.attempt)},
             aurora::LocalizedString{std::to_string(tunnel_retry_wait_s(row))}});
        auto note = make_text(reason + " " + wait, chrome.text_dim);
        note->modifier.set(aurora::Modifier{}.width(150.0F));
        line1_children.push_back(node(std::move(note)));
    } else if (row.runtime.state == conn::TunnelState::Failed &&
               row.runtime.error != conn::TunnelError::None) {
        auto note = make_text(settings_label(tunnel_error_key(row.runtime.error)),
                              chrome.text_dim);
        note->modifier.set(aurora::Modifier{}.width(150.0F));
        line1_children.push_back(node(std::move(note)));
    } else if (row.runtime.autostart_skipped) {
        // D8 细则：启动序跳过的 autostart 隧道行内提示（询问型凭据未静默可解）。
        auto note = make_text(settings_label("tunnel.row.autostart_skipped"), chrome.text_dim);
        note->modifier.set(aurora::Modifier{}.width(190.0F));
        line1_children.push_back(node(std::move(note)));
    }

    if (hooks_.start != nullptr && hooks_.stop != nullptr) {
        auto toggle = make_button(
            settings_label(running ? "tunnel.action.stop" : "tunnel.action.start"),
            running ? chrome.control_bg : chrome.accent,
            running ? chrome.text : chrome.window_bg, 48.0F,
            [this, id = row.spec.id]() -> void { on_start_stop(id); }, chrome.card_line);
        line1_children.push_back(node(std::move(toggle)));
    }
    // 编辑/删除与启停同排（判据 2 首行的按钮组）：次行只放数据段，行宽才守得住
    // 496 dp 的卡内净宽——按钮排在次行之外会被卡片裁掉，派发链上都摸不到。
    line1_children.push_back(node(make_button(settings_label("tunnel.action.edit"),
                                              chrome.control_bg, chrome.text_dim, 40.0F,
                                              [this, id = row.spec.id]() -> void {
                                                  open_editor(id);
                                              },
                                              chrome.card_line)));
    if (hooks_.persist != nullptr) {
        line1_children.push_back(node(make_button(settings_label("tunnel.action.delete"),
                                                  chrome.control_bg, chrome.text_dim, 40.0F,
                                                  [this, id = row.spec.id]() -> void {
                                                      on_delete(id);
                                                  },
                                                  chrome.card_line)));
    }

    // ---- 次行：监听点 → 目标 + 注记 + 承载档案 ----
    auto endpoint = make_text(tunnel_endpoint_line(row), chrome.text_dim);
    endpoint->modifier.set(aurora::Modifier{}.width(280.0F));
    auto line2_children = std::vector<aurora::Node>{node(std::move(endpoint))};
    const auto note_key = tunnel_endpoint_note_key(row);
    if (!note_key.empty()) {
        line2_children.push_back(node(make_text(settings_label(note_key), chrome.text_dim)));
    }
    auto profile_name = settings_label("tunnel.row.profile",
                                       {aurora::LocalizedString{row.spec.profile_id}});
    line2_children.push_back(node(make_text(std::move(profile_name), chrome.text_dim)));

    auto line1 = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(line1_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 6.0F,
    });
    auto line2 = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(line2_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    });
    auto stacked = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {node(std::move(line1)), node(std::move(line2))},
        .gap = 2.0F,
    });
    return node(std::static_pointer_cast<aurora::Widget>(std::move(stacked)));
}

[[nodiscard]] auto TunnelPanel::build_overlay() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};

    // ---- 标题行：标题 + 运行中计数 + 新建 + 关闭 ----
    int running = 0;
    for (const auto &row : painted_rows_) {
        running += conn::state_is_running(row.runtime.state) ? 1 : 0;
    }
    // 标题不抓 fill_max_width：那会把计数/新建/关闭整组挤出卡宽（派发链实测点不到）。
    auto title = make_text(settings_label("tunnel.title"), chrome.text);
    auto count =
        make_text(settings_label("tunnel.header.running", {aurora::LocalizedString{
                      std::to_string(running)}}),
                   chrome.text_dim);
    auto header_children =
        std::vector<aurora::Node>{node(std::move(title)), node(std::move(count))};
    if (hooks_.persist != nullptr) {
        header_children.push_back(node(make_button(settings_label("tunnel.action.new"),
                                                   chrome.accent, chrome.window_bg, 64.0F,
                                                   [this]() -> void { open_editor(std::nullopt); },
                                                   chrome.card_line)));
    }
    header_children.push_back(node(make_button(
        settings_label("tunnel.action.close"), chrome.control_bg, chrome.text_dim, 48.0F,
        [this]() -> void { close(); }, chrome.card_line)));
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(header_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    })));

    // ---- 留痕行（落盘失败等；空则不占）----
    if (!notice_.empty()) {
        auto notice = make_text(notice_, chrome.text_dim);
        notice->modifier.set(aurora::Modifier{}.fill_max_width());
        children.push_back(node(std::move(notice)));
    }

    // ---- 行区（标号 1/2）----
    auto rows_column = std::make_shared<aurora::Column>(aurora::ColumnProps{.gap = 8.0F});
    for (const auto &row : painted_rows_) {
        rows_column->add(build_row(row));
    }
    if (painted_rows_.empty()) {
        rows_column->add(node(make_text(settings_label("tunnel.empty"), chrome.text_dim)));
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
                           .width(520.0F)
                           .fill_max_height()
                           .padding(aurora::EdgeInsets{12.0F, 12.0F, 12.0F, 12.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto TunnelPanel::on_start_stop(const std::string &id) -> void {
    const auto it = std::find_if(specs_.begin(), specs_.end(),
                                 [&](const auto &spec) { return spec.id == id; });
    if (it == specs_.end()) {
        return;
    }
    const auto running = conn::state_is_running(runtimes_[id].state);
    if (running) {
        on_stop(id);
    } else {
        on_start(*it);
    }
}

auto TunnelPanel::on_start(const conn::TunnelSpec &spec) -> void {
    if (hooks_.start != nullptr) {
        hooks_.start(spec);  // 运行态由下一帧快照反映（D6①），本件不同步改行。
    }
}

auto TunnelPanel::on_stop(const std::string &id) -> void {
    if (hooks_.stop != nullptr) {
        hooks_.stop(id);
    }
}

auto TunnelPanel::on_delete(const std::string &id) -> void {
    const auto running = conn::state_is_running(runtimes_[id].state);
    if (running) {
        open_delete_confirm(id);  // 标号 6：运行中先呼「停止并删除」确认。
        return;
    }
    remove_spec(id);
}

auto TunnelPanel::remove_spec(const std::string &id) -> void {
    const auto before =
        std::remove_if(specs_.begin(), specs_.end(),
                       [&](const auto &spec) { return spec.id == id; });
    if (before == specs_.end()) {
        return;
    }
    specs_.erase(before, specs_.end());
    if (hooks_.persist != nullptr && !hooks_.persist(specs_)) {
        notice_ = settings_label("tunnel.notice.persist_failed");  // 失败留内存态（侧栏口径）。
    }
    painted_rows_ = tunnel_rows(specs_, runtimes_);
    refresh_overlay();
}

// ============================================================
// 编辑对话框（D4①：本件自建浮层，connection_wizard 同族形态）
// ============================================================

auto TunnelPanel::open_editor(std::optional<std::string> id) -> void {
    close_editor();
    editing_id_.clear();
    if (id.has_value()) {
        const auto it = std::find_if(specs_.begin(), specs_.end(),
                                     [&](const auto &spec) { return spec.id == *id; });
        if (it == specs_.end()) {
            return;
        }
        draft_ = *it;
        editing_id_ = *id;
    } else {
        draft_ = conn::TunnelSpec{};
        draft_.retry = conn::RetryPolicy{};
    }
    kind_index_ = static_cast<std::size_t>(draft_.kind);
    profile_index_ = 0;
    rebuild_editor();
}

auto TunnelPanel::rebuild_editor() -> void {
    close_editor();
    auto editor = build_editor();
    editor_dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(editor))));
    editor_overlay_ =
        host_.add_overlay(node(std::static_pointer_cast<aurora::Widget>(editor_dialog_)));
    editor_dialog_->show();
    editor_open_ = true;
}

auto TunnelPanel::close_editor() -> void {
    if (!editor_open_) {
        return;
    }
    if (editor_dialog_ != nullptr) {
        editor_dialog_->close();
    }
    if (editor_overlay_.has_value()) {
        host_.remove_overlay(*editor_overlay_);
        editor_overlay_.reset();
    }
    editor_dialog_.reset();
    name_input_.reset();
    listen_addr_input_.reset();
    listen_port_input_.reset();
    target_host_input_.reset();
    target_port_input_.reset();
    retry_base_input_.reset();
    retry_cap_input_.reset();
    retry_attempts_input_.reset();
    editor_notice_.reset();
    editor_open_ = false;
}

[[nodiscard]] auto TunnelPanel::build_editor() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};

    auto title = make_text(
        settings_label(editing_id_.empty() ? "tunnel.editor.title_new"
                                           : "tunnel.editor.title_edit"),
        chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::move(title)));

    // ---- 三式类型卡（标号 5：卡片决定字段集）----
    std::vector<aurora::Node> cards;
    for (std::size_t i = 0; i < kKindCycle.size(); ++i) {
        const auto selected = i == kind_index_;
        cards.push_back(node(make_button(settings_label(kind_key(kKindCycle[i])),
                                         selected ? chrome.accent : chrome.control_bg,
                                         selected ? chrome.window_bg : chrome.text, 96.0F,
                                         [this, i]() -> void {
                                             // 换形态＝就地重建对话框：先收当前控件值，改
                                             // kind 后重开（build_editor 逐字段从 draft 回填）。
                                             collect_draft();
                                             draft_.kind = kKindCycle[i];
                                             kind_index_ = i;
                                             rebuild_editor();
                                         },
                                         chrome.card_line)));
    }
    children.push_back(node(std::make_shared<aurora::Row>(
        aurora::RowProps{.children = std::move(cards), .gap = 6.0F})));

    name_input_ = make_input(240.0F);
    name_input_->set_value(draft_.name);
    children.push_back(field_row(settings_label("tunnel.editor.field.name"),
                                 node(std::static_pointer_cast<aurora::Widget>(name_input_))));

    // 监听两栏同行、目标两栏同行：对话框整体高度必须留在 600 dp 无头视口内，
    // 一栏一行会把「保存/取消」挤出可派发区（itest 实测）。
    listen_addr_input_ = make_input(150.0F);
    listen_addr_input_->set_value(draft_.listen_address);
    listen_port_input_ = make_input(70.0F);
    if (draft_.listen_port != 0) {
        listen_port_input_->set_value(std::to_string(draft_.listen_port));
    }
    children.push_back(pair_row(settings_label("tunnel.editor.field.listen_addr"),
                                node(std::static_pointer_cast<aurora::Widget>(
                                    listen_addr_input_)),
                                settings_label("tunnel.editor.field.listen_port"),
                                node(std::static_pointer_cast<aurora::Widget>(
                                    listen_port_input_))));

    // ---- 目标两栏：Dynamic 不画（隐去＝不用，模型容忍残值）----
    if (draft_.kind != conn::TunnelKind::Dynamic) {
        target_host_input_ = make_input(200.0F);
        target_host_input_->set_value(draft_.target_host);
        target_port_input_ = make_input(70.0F);
        if (draft_.target_port != 0) {
            target_port_input_->set_value(std::to_string(draft_.target_port));
        }
        children.push_back(pair_row(settings_label("tunnel.editor.field.target_host"),
                                    node(std::static_pointer_cast<aurora::Widget>(
                                        target_host_input_)),
                                    settings_label("tunnel.editor.field.target_port"),
                                    node(std::static_pointer_cast<aurora::Widget>(
                                        target_port_input_))));
    }

    // ---- 承载档案下拉（行源＝SSH 档案子集；每次重建现取，新档案即刻可选）----
    profiles_cache_.clear();
    if (hooks_.profiles != nullptr) {
        for (auto &profile : hooks_.profiles()) {
            if (profile.type == conn::ConnectionType::Ssh) {
                profiles_cache_.push_back(std::move(profile));
            }
        }
    }
    profile_index_ = 0;
    auto profile_names = std::vector<std::string>{};
    for (std::size_t i = 0; i < profiles_cache_.size(); ++i) {
        if (profiles_cache_[i].id == draft_.profile_id) {
            profile_index_ = i;
        }
        profile_names.push_back(profiles_cache_[i].name);
    }
    if (!profiles_cache_.empty()) {
        // 档案表里查不到的旧引用归位到首条——落盘绝不写回不存在的 profile_id。
        draft_.profile_id = profiles_cache_[profile_index_].id;
    }
    auto profile_box = std::make_shared<aurora::Dropdown>(
        std::move(profile_names), static_cast<int>(profile_index_));
    profile_box->set_box_color(chrome.control_bg);
    profile_box->set_border_color(chrome.card_line);
    profile_box->set_text_color(chrome.text);
    profile_box->set_arrow_color(chrome.text_dim);
    profile_box->set_accent_color(chrome.accent);
    profile_box->set_placeholder(settings_label("tunnel.editor.no_profile"));
    profile_box->set_enabled(!profiles_cache_.empty());
    profile_box->set_on_change([this](int index) -> void {
        if (index < 0 || static_cast<std::size_t>(index) >= profiles_cache_.size()) {
            return;
        }
        profile_index_ = static_cast<std::size_t>(index);
        draft_.profile_id = profiles_cache_[profile_index_].id;
    });
    profile_box->modifier.set(aurora::Modifier{}.width(240.0F));
    children.push_back(field_row(settings_label("tunnel.editor.field.profile"),
                                 node(std::static_pointer_cast<aurora::Widget>(
                                     std::move(profile_box)))));

    // ---- 自动启动开关（D8②）：拨动只改草稿，字段集不随它变，故不重建对话框 ----
    auto autostart_switch =
        std::make_shared<aurora::Switch>(aurora::Reactive<bool>{draft_.autostart});
    // 锁高的理由与设置面板开关同款：滑块直径按自身盒高算，行交叉轴居中给的是紧约束。
    autostart_switch->modifier.set(aurora::Modifier{}.height(24.0F));
    autostart_switch->set_active_color(chrome.accent);
    autostart_switch->set_inactive_color(chrome.control_bg);
    autostart_switch->set_border(chrome.card_line, 1.0F);
    autostart_switch->set_thumb_color(chrome.text);
    autostart_switch->set_on_changed([this](bool next) -> void {
        draft_.autostart = next;
    });
    children.push_back(field_row(settings_label("tunnel.editor.field.autostart"),
                                 node(std::static_pointer_cast<aurora::Widget>(
                                     std::move(autostart_switch)))));

    // ---- 重试三字段（D9②；留空＝旧全局档，一行排满，提示行释义）----
    retry_base_input_ = make_input(70.0F);
    retry_base_input_->set_value(std::to_string(draft_.retry.base_ms));
    retry_cap_input_ = make_input(70.0F);
    retry_cap_input_->set_value(std::to_string(draft_.retry.cap_ms));
    retry_attempts_input_ = make_input(70.0F);
    if (draft_.retry.max_attempts != 0) {
        retry_attempts_input_->set_value(std::to_string(draft_.retry.max_attempts));
    }
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children =
            {
                node(make_text(settings_label("tunnel.editor.retry.base"), chrome.text_dim)),
                node(std::static_pointer_cast<aurora::Widget>(retry_base_input_)),
                node(make_text(settings_label("tunnel.editor.retry.cap"), chrome.text_dim)),
                node(std::static_pointer_cast<aurora::Widget>(retry_cap_input_)),
                node(make_text(settings_label("tunnel.editor.retry.attempts"), chrome.text_dim)),
                node(std::static_pointer_cast<aurora::Widget>(retry_attempts_input_)),
            },
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 6.0F,
    })));
    auto hint = make_text(settings_label("tunnel.editor.retry.hint"), chrome.text_dim);
    hint->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::move(hint)));

    // ---- 校验/冲突红提示（空＝无）----
    editor_notice_ = make_text(std::string{}, chrome.text_dim);
    editor_notice_->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(editor_notice_)));

    // ---- 保存 / 取消 ----
    auto save_button = make_button(settings_label("tunnel.editor.action.save"), chrome.accent,
                                   chrome.window_bg, 64.0F, [this]() -> void { save_draft(); },
                                   chrome.card_line);
    auto cancel_button = make_button(settings_label("tunnel.editor.action.cancel"),
                                     chrome.control_bg, chrome.text, 64.0F,
                                     [this]() -> void { close_editor(); }, chrome.card_line);
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::move(save_button)), node(std::move(cancel_button))},
        .gap = 8.0F,
    })));

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(460.0F)
                           .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto TunnelPanel::collect_draft() -> void {
    draft_.name = name_input_ != nullptr ? name_input_->value() : draft_.name;
    draft_.listen_address =
        listen_addr_input_ != nullptr ? listen_addr_input_->value() : draft_.listen_address;
    draft_.listen_port =
        parse_int(listen_port_input_ != nullptr ? listen_port_input_->value() : std::string{}, 0);
    if (draft_.kind != conn::TunnelKind::Dynamic) {
        draft_.target_host =
            target_host_input_ != nullptr ? target_host_input_->value() : draft_.target_host;
        draft_.target_port =
            parse_int(target_port_input_ != nullptr ? target_port_input_->value()
                                                    : std::string{},
                      0);
    }
    // 留空/非数＝回全局档缺省值（稿判据 5 的「留空＝旧全局档」口径）。
    draft_.retry.base_ms =
        parse_int(retry_base_input_ != nullptr ? retry_base_input_->value() : std::string{},
                  conn::RetryPolicy{}.base_ms);
    draft_.retry.cap_ms =
        parse_int(retry_cap_input_ != nullptr ? retry_cap_input_->value() : std::string{},
                  conn::RetryPolicy{}.cap_ms);
    draft_.retry.max_attempts =
        parse_int(retry_attempts_input_ != nullptr ? retry_attempts_input_->value()
                                                   : std::string{},
                  0);
}

auto TunnelPanel::save_draft() -> void {
    collect_draft();
    std::string issue;
    if (draft_.name.empty()) {
        issue = settings_label("tunnel.editor.issue_name");
    } else if (draft_.profile_id.empty()) {
        issue = settings_label("tunnel.editor.issue_profile");
    } else if (const auto spec_issue = conn::validate_tunnel_spec(draft_);
               spec_issue != conn::TunnelSpecIssue::None) {
        issue = settings_label(spec_issue == conn::TunnelSpecIssue::ListenPortInvalid
                                   ? "tunnel.editor.issue_listen_port"
                                   : spec_issue == conn::TunnelSpecIssue::TargetHostEmpty
                                         ? "tunnel.editor.issue_target_host"
                                         : "tunnel.editor.issue_target_port");
    } else {
        for (const auto &other : specs_) {
            if (other.id != editing_id_ && conn::specs_conflict(draft_, other)) {
                issue = settings_label("tunnel.editor.issue_conflict");
                break;
            }
        }
    }
    if (!issue.empty()) {
        editor_notice_->set_content(issue);
        return;  // 无效草稿不落盘（校验在提交时做，稿 §5）。
    }

    auto next = draft_;
    next.id = editing_id_.empty() ? next_tunnel_id(specs_, next.name) : editing_id_;
    bool replaced = false;
    for (auto &existing : specs_) {
        if (existing.id == next.id) {
            existing = next;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        specs_.push_back(next);
    }
    if (hooks_.persist != nullptr && !hooks_.persist(specs_)) {
        notice_ = settings_label("tunnel.notice.persist_failed");
    }
    close_editor();
    painted_rows_ = tunnel_rows(specs_, runtimes_);
    refresh_overlay();
}

// ============================================================
// 删除确认（标号 6 两段式）
// ============================================================

auto TunnelPanel::open_delete_confirm(std::string id) -> void {
    close_delete_confirm();
    pending_delete_id_ = std::move(id);
    const auto chrome = settings_chrome();
    std::string name;
    for (const auto &spec : specs_) {
        if (spec.id == pending_delete_id_) {
            name = spec.name;
        }
    }

    auto title = make_text(settings_label("tunnel.confirm.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    auto body = make_text(settings_label("tunnel.confirm.body",
                                         {aurora::LocalizedString{name}}),
                          chrome.text);
    body->modifier.set(aurora::Modifier{}.fill_max_width());
    auto confirm_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("tunnel.confirm.action"),
        .color = aurora::colors::AURORA_RED,  // 危险红（sftp D6① 同口径）
        .on_color = chrome.window_bg,
        .min_width = 80.0F,
    });
    confirm_button->set_on_click([this]() -> void {
        const auto id_copy = pending_delete_id_;
        close_delete_confirm();
        on_stop(id_copy);  // 先停（装配层 join），再删。
        remove_spec(id_copy);
    });
    auto cancel_button = make_button(settings_label("tunnel.editor.action.cancel"),
                                     chrome.control_bg, chrome.text, 64.0F,
                                     [this]() -> void { close_delete_confirm(); },
                                     chrome.card_line);

    auto content = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {node(std::move(title)), node(std::move(body)),
                     node(std::make_shared<aurora::Row>(aurora::RowProps{
                         .children = {node(std::move(confirm_button)),
                                      node(std::move(cancel_button))},
                         .gap = 8.0F}))},
        .gap = 8.0F,
    });
    content->modifier.set(aurora::Modifier{}
                              .width(360.0F)
                              .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                              .background(chrome.card_bg, 0.0F)
                              .border(1.0F, chrome.card_line)
                              .clickable([]() -> void {}));
    confirm_dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(content))));
    confirm_overlay_ =
        host_.add_overlay(node(std::static_pointer_cast<aurora::Widget>(confirm_dialog_)));
    confirm_dialog_->show();
    delete_confirm_open_ = true;
}

auto TunnelPanel::close_delete_confirm() -> void {
    if (!delete_confirm_open_) {
        return;
    }
    if (confirm_dialog_ != nullptr) {
        confirm_dialog_->close();
    }
    if (confirm_overlay_.has_value()) {
        host_.remove_overlay(*confirm_overlay_);
        confirm_overlay_.reset();
    }
    confirm_dialog_.reset();
    pending_delete_id_.clear();
    delete_confirm_open_ = false;
}

auto TunnelPanel::clear_state() -> void {
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    painted_rows_.clear();
    open_ = false;
}

}  // namespace borealis::ui
