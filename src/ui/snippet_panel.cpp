// ============================================================
// 快捷片段面板实现（src/ui/snippet_panel.cpp）
// ------------------------------------------------------------
// 形态对照设计稿 `codespec/UI_SNIPPETS.draft.md`：520 dp 左停靠卡片（D1①），
// 搜索 + 分组过滤（D4/D6），行内发送/编辑/删除（D5①），占位符填充框（D4①）。
//
// 主体卡片只建一次：键入搜索词时只换行区子节点，搜索框的焦点与编辑态因此不丢；
// 片段表变化（保存/删除）才整卡重装，让分组下拉的候选同步。面板不认识配置文件、
// 不认识 `Session`：整表与正文都经 Hooks 交回装配层（头注同款边界）。
// ============================================================

#include "snippet_panel.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "aurora/core/color.h"
#include "aurora/widget/alignment.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/stack.h"

#include "settings_i18n.h"
#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

namespace conn = borealis::conn;

/// 搜索行的固定占位带高（容纳输入框自然高，并给下拉面板留出下沿）。
constexpr float kSearchBand = 52.0F;

[[nodiscard]] auto make_text(std::string content, aurora::Color color)
    -> std::shared_ptr<aurora::Text> {
    return std::make_shared<aurora::Text>(
        aurora::TextProps{.content = std::move(content), .text_color = color});
}

[[nodiscard]] auto make_button(std::string label, aurora::Color bg, aurora::Color fg,
                               float min_width, std::function<void()> on_click,
                               aurora::Color line) -> std::shared_ptr<aurora::Button> {
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

[[nodiscard]] auto input_node(const std::shared_ptr<aurora::TextInput> &input) -> aurora::Node {
    return node(std::static_pointer_cast<aurora::Widget>(input));
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

/// @brief 「标签 + 控件」一行（同密钥/隧道面板的字段形态）。
[[nodiscard]] auto field_row(std::string label, aurora::Node control) -> aurora::Node {
    const auto chrome = settings_chrome();
    auto tag = make_text(std::move(label), chrome.text_dim);
    tag->modifier.set(aurora::Modifier{}.width(84.0F));
    return node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::static_pointer_cast<aurora::Widget>(std::move(tag))),
                     std::move(control)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    }));
}

/// @brief 校验问题的红提示文案（`None` 回空串，调用方据此放行）。
[[nodiscard]] auto issue_text(conn::SnippetIssue issue) -> std::string {
    switch (issue) {
        case conn::SnippetIssue::EmptyName:
            return settings_label("snippets.issue.empty_name");
        case conn::SnippetIssue::NameTooLong:
            return settings_label("snippets.issue.name_too_long",
                                  {{"limit", static_cast<std::int64_t>(conn::kSnippetNameMaxBytes)}});
        case conn::SnippetIssue::NameControlChar:
            return settings_label("snippets.issue.name_control_char");
        case conn::SnippetIssue::GroupTooLong:
            return settings_label("snippets.issue.group_too_long",
                                  {{"limit", static_cast<std::int64_t>(conn::kSnippetGroupMaxBytes)}});
        case conn::SnippetIssue::GroupControlChar:
            return settings_label("snippets.issue.group_control_char");
        case conn::SnippetIssue::EmptyBody:
            return settings_label("snippets.issue.empty_body");
        case conn::SnippetIssue::BodyTooLong:
            return settings_label("snippets.issue.body_too_long",
                                  {{"limit", static_cast<std::int64_t>(conn::kSnippetBodyMaxBytes)}});
        case conn::SnippetIssue::None:
            break;
    }
    return {};
}

/// @brief 正文预览：换行按空格上屏（手改文件塞进来的换行仍原样发送，只改这一处显示）。
[[nodiscard]] auto preview_line(const std::string &body) -> std::string {
    std::string out = body;
    for (char &ch : out) {
        if (ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    return out;
}

}  // namespace

SnippetPanel::SnippetPanel(aurora::OverlayHost &host, Hooks hooks)
    : host_{&host}, hooks_{std::move(hooks)} {}

auto SnippetPanel::set_overlay_host(aurora::OverlayHost &host) -> void {
    if (host_ == &host) {
        return;
    }
    close();  // 先从旧宿主收掉全部浮层，再换指针（序号不能跨宿主用）。
    host_ = &host;
}

SnippetPanel::~SnippetPanel() {
    close_editor();
    close_fill();
    close_delete_confirm();
    clear_state();
}

auto SnippetPanel::open() -> void {
    if (open_) {
        return;
    }
    snippets_ = hooks_.load != nullptr ? hooks_.load() : std::vector<conn::Snippet>{};
    query_.clear();
    group_.clear();
    notice_.clear();
    open_ = true;
    install_overlay();
}

auto SnippetPanel::close() -> void {
    if (!open_) {
        return;
    }
    close_editor();
    close_fill();
    close_delete_confirm();
    if (dialog_ != nullptr) {
        dialog_->close();
    }
    clear_state();
}

auto SnippetPanel::visible_rows() const -> std::vector<conn::Snippet> {
    std::vector<conn::Snippet> out;
    for (const auto index : conn::snippet_rows(snippets_, query_, group_)) {
        out.push_back(snippets_[index]);
    }
    return out;
}

auto SnippetPanel::find_snippet(const std::string &id) const -> const conn::Snippet * {
    const auto it =
        std::find_if(snippets_.begin(), snippets_.end(),
                     [&id](const conn::Snippet &snippet) { return snippet.id == id; });
    return it == snippets_.end() ? nullptr : &*it;
}

auto SnippetPanel::index_of(const std::string &id) const -> std::optional<std::size_t> {
    const auto it =
        std::find_if(snippets_.begin(), snippets_.end(),
                     [&id](const conn::Snippet &snippet) { return snippet.id == id; });
    if (it == snippets_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(snippets_.begin(), it));
}

auto SnippetPanel::install_overlay() -> void {
    if (overlay_index_.has_value()) {
        host_->remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    if (dialog_ != nullptr) {
        // `show()` 压过焦点作用域；整卡重装（保存/删除后）必须先弹出，否则作用域留在栈上。
        dialog_->close();
    }
    // 先放掉旧卡内的成员引用：否则 `dialog_.reset()` 拆树时这些子控件仍活着，
    // 框架会报「布局父已拆而子控件仍存活」。引用于 `build_overlay()` 重建时立即补回。
    search_input_.reset();
    group_dropdown_.reset();
    rows_column_.reset();
    count_text_.reset();
    notice_slot_.reset();
    notice_text_.reset();
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
    overlay_index_ = host_->add_overlay(node(std::static_pointer_cast<aurora::Widget>(dialog_)));
    dialog_->show();
    // 打开即落在搜索框：`snippets.open` 唤起后可以直接打字、回车发送（稿 §2 的快捷键路径）。
    if (search_input_ != nullptr) {
        search_input_->request_focus();
    }
}

[[nodiscard]] auto SnippetPanel::build_overlay() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};

    // ---- 标题行：标题 + 条数 + 新建 + 关闭 ----
    count_text_ = make_text(settings_label("snippets.header.count",
                                           {{"count", static_cast<std::int64_t>(snippets_.size())}}),
                            chrome.text_dim);
    auto header_children = std::vector<aurora::Node>{
        node(make_text(settings_label("snippets.title"), chrome.text)),
        node(std::static_pointer_cast<aurora::Widget>(count_text_)),
        node(make_button(settings_label("snippets.action.create"), chrome.accent, chrome.window_bg,
                         56.0F, [this]() -> void { open_editor(std::nullopt); }, chrome.card_line)),
        node(make_button(settings_label("snippets.action.close"), chrome.control_bg, chrome.text_dim,
                         48.0F, [this]() -> void { close(); }, chrome.card_line)),
    };
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(header_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    })));

    // ---- 留痕槽（空＝不占行；`set_notice` 换这一槽的子节点）----
    notice_slot_ = std::make_shared<aurora::Column>(aurora::ColumnProps{.gap = 0.0F});
    notice_text_.reset();
    if (!notice_.empty()) {
        notice_text_ = make_text(notice_, chrome.text_dim);
        notice_text_->modifier.set(aurora::Modifier{}.fill_max_width());
        notice_slot_->add(node(std::static_pointer_cast<aurora::Widget>(notice_text_)));
    }
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(notice_slot_)));

    // ---- 搜索行 + 分组过滤 ----
    search_input_ = make_input(300.0F);
    search_input_->set_placeholder(settings_label("snippets.search.placeholder"));
    search_input_->set_value(query_);
    search_input_->set_on_changed([this](const std::string &value) -> void {
        query_ = value;
        refresh_rows();
    });
    search_input_->set_on_submit([this](const std::string &) -> void { send_unique_candidate(); });

    auto groups = conn::snippet_groups(snippets_);
    if (!group_.empty() && std::ranges::find(groups, group_) == groups.end()) {
        group_.clear();  // 该分组最后一条被删掉后，过滤档回落「全部分组」。
    }
    auto options = std::vector<std::string>{settings_label("snippets.group.all")};
    std::size_t selected = 0;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        options.push_back(groups[index]);
        if (groups[index] == group_) {
            selected = index + 1;
        }
    }
    group_dropdown_ = std::make_shared<aurora::Dropdown>(options, static_cast<int>(selected));
    group_dropdown_->set_on_change([this, groups = std::move(groups)](int index) -> void {
        const auto position = static_cast<std::size_t>(index);
        group_ = (position == 0U || position > groups.size()) ? std::string{} : groups[position - 1U];
        refresh_rows();
    });
    group_dropdown_->modifier.set(aurora::Modifier{}.width(160.0F));

    // ---- 行区（Stack 的下层）----
    rows_column_ = std::make_shared<aurora::Column>(aurora::ColumnProps{.gap = 8.0F});
    auto scroll = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
        .child = node(std::static_pointer_cast<aurora::Widget>(rows_column_)),
        .step = 32.0F,
    });
    scroll->modifier.set(aurora::Modifier{}.fill_max_width().expand());
    // 顶部让出搜索带：搜索行叠在行区之上，行内容从它下沿开始。
    auto body = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {node(std::static_pointer_cast<aurora::Widget>(std::move(scroll)))},
        .gap = 0.0F,
    });
    body->modifier.set(aurora::Modifier{}
                           .fill_max_width()
                           .fill_max_height()
                           .padding(aurora::EdgeInsets{.top = kSearchBand}));

    // ---- 搜索行（Stack 的上层，后声明＝后绘制、先命中）----
    // Dropdown 的面板画在自身布局盒外；若与行区平铺在同级 Column，后声明的 Scroll 会在
    // 命中链里先认领（`Scroll::wants_scroll` 恒真），面板既被行区盖住、点也落到 Scroll 上。
    // 叠成 Stack 后，搜索行连同它的下拉面板整体压在行区之上。
    //
    // 外层用 Column 而非直接给 Row 定高：Row 会把父约束的交叉轴下限原样发给子项，
    // 定高带会把输入框与下拉主框一起拉成 52 高（下拉面板因此从看不见的布局盒下沿
    // 而不是画出来的 30 高主框下沿起算，面板与主框之间凭空多出一段死区）。
    // Column 的主轴下限是 0，子行按自然高测量，控件仍保持各自高度。
    auto search_bar = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {input_node(search_input_),
                     node(std::static_pointer_cast<aurora::Widget>(group_dropdown_))},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    });
    auto search_row = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {node(std::static_pointer_cast<aurora::Widget>(std::move(search_bar)))},
        .gap = 0.0F,
    });
    search_row->modifier.set(aurora::Modifier{}
                                 .height(kSearchBand)
                                 .fill_max_width()
                                 .background(chrome.card_bg, 0.0F)
                                 .clickable([]() -> void {}));
    auto stack = std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{
            node(std::static_pointer_cast<aurora::Widget>(std::move(body))),
            node(std::static_pointer_cast<aurora::Widget>(std::move(search_row)))},
        aurora::Alignment::TopLeft);
    stack->modifier.set(aurora::Modifier{}.fill_max_width().expand());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(stack))));

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
    refresh_rows();
    return card;
}

auto SnippetPanel::refresh_rows() -> void {
    if (rows_column_ == nullptr) {
        return;
    }
    const auto chrome = settings_chrome();
    const auto rows = conn::snippet_rows(snippets_, query_, group_);
    std::vector<aurora::Node> kids;
    kids.reserve(rows.size() + 1U);
    for (const auto index : rows) {
        kids.push_back(build_row(snippets_[index]));
    }
    if (kids.empty()) {
        kids.push_back(node(make_text(settings_label(snippets_.empty() ? "snippets.empty_hint"
                                                                       : "snippets.empty.filtered"),
                                      chrome.text_dim)));
    }
    rows_column_->adopt_children(std::move(kids));
    rows_column_->mark_needs_layout();  // `adopt_children` 不标脏，这里换的是可见行序。
    if (count_text_ != nullptr) {
        count_text_->set_content(settings_label("snippets.header.count",
                                                 {{"count", static_cast<std::int64_t>(snippets_.size())}}));
    }
}

[[nodiscard]] auto SnippetPanel::build_row(const conn::Snippet &snippet) -> aurora::Node {
    const auto chrome = settings_chrome();
    const auto placeholder_count = conn::snippet_placeholder_names(snippet.body).size();

    auto line1_children = std::vector<aurora::Node>{};
    auto group_text = make_text(snippet.group.empty() ? std::string{"--"} : snippet.group,
                                chrome.text_dim);
    group_text->modifier.set(aurora::Modifier{}.width(84.0F));
    line1_children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(group_text))));
    auto name_text = make_text(snippet.name, chrome.text);
    name_text->modifier.set(aurora::Modifier{}.width(150.0F));
    line1_children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(name_text))));
    if (placeholder_count > 0U) {
        // 纯 ASCII 徽标：占位符语法本身就是 `{{name}}`，这里只是它的个数。
        auto badge = make_text("{{" + std::to_string(placeholder_count) + "}}", chrome.accent);
        badge->modifier.set(aurora::Modifier{}.width(48.0F));
        line1_children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(badge))));
    }
    line1_children.push_back(node(make_button(
        settings_label("snippets.action.send"), chrome.accent, chrome.window_bg, 48.0F,
        [this, snippet]() -> void { send_snippet(snippet); }, chrome.card_line)));
    line1_children.push_back(node(make_button(
        settings_label("snippets.action.edit"), chrome.control_bg, chrome.text_dim, 44.0F,
        [this, id = snippet.id]() -> void { open_editor(id); }, chrome.card_line)));
    line1_children.push_back(node(make_button(
        settings_label("snippets.action.remove"), chrome.control_bg, chrome.text_dim, 44.0F,
        [this, id = snippet.id]() -> void { open_delete_confirm(id); }, chrome.card_line)));

    auto preview = make_text(preview_line(snippet.body), chrome.text_dim);
    preview->modifier.set(aurora::Modifier{}.width(440.0F));
    auto line1 = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(line1_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 6.0F,
    });
    auto line2 = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::static_pointer_cast<aurora::Widget>(std::move(preview)))},
        .gap = 8.0F,
    });
    auto stacked = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {node(std::static_pointer_cast<aurora::Widget>(std::move(line1))),
                     node(std::static_pointer_cast<aurora::Widget>(std::move(line2)))},
        .gap = 2.0F,
    });
    return node(std::static_pointer_cast<aurora::Widget>(std::move(stacked)));
}

// ============================================================
// 发送（D3/D4/D6）
// ============================================================

auto SnippetPanel::send_snippet(const conn::Snippet &snippet) -> void {
    if (!conn::snippet_placeholder_names(snippet.body).empty()) {
        open_fill(snippet);
        return;
    }
    deliver(snippet, snippet.body);
}

auto SnippetPanel::deliver(const conn::Snippet &snippet, const std::string &text) -> void {
    std::string payload = text;
    if (snippet.enter) {
        payload.push_back('\n');  // 行尾折算归 `Session::send_text`，这里只负责这一枚换行。
    }
    const bool sent = hooks_.send != nullptr && hooks_.send(payload);
    set_notice(sent ? settings_label("snippets.notice.sent",
                                     {{"name", snippet.name}})
                    : settings_label("snippets.notice.no_session"));
}

auto SnippetPanel::send_unique_candidate() -> void {
    const auto rows = visible_rows();
    if (rows.size() == 1U) {
        send_snippet(rows.front());
        return;
    }
    set_notice(settings_label("snippets.notice.candidates",
                              {{"count", static_cast<std::int64_t>(rows.size())}}));
}

auto SnippetPanel::set_notice(std::string text) -> void {
    notice_ = std::move(text);
    if (notice_.empty()) {
        if (notice_text_ != nullptr && notice_slot_ != nullptr) {
            notice_slot_->adopt_children({});
            notice_slot_->mark_needs_layout();
            notice_text_.reset();
        }
        return;
    }
    if (notice_text_ != nullptr) {
        notice_text_->set_content(notice_);
        // `set_content` 只换响应式值，不标脏；不重排则 `display_text_`（无障碍名与绘制缓存源）
        // 仍停在上一句留痕上。
        notice_text_->mark_needs_layout();
        return;
    }
    if (notice_slot_ == nullptr) {
        return;
    }
    notice_text_ = make_text(notice_, settings_chrome().text_dim);
    notice_text_->modifier.set(aurora::Modifier{}.fill_max_width());
    notice_slot_->adopt_children(std::vector<aurora::Node>{node(
        std::static_pointer_cast<aurora::Widget>(notice_text_))});
    notice_slot_->mark_needs_layout();
}

// ============================================================
// 新建 / 编辑（D5①）
// ============================================================

auto SnippetPanel::open_editor(const std::optional<std::string> &id) -> void {
    close_editor();
    editing_id_ = id;
    auto editor = build_editor();
    editor_dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(editor))));
    editor_overlay_ =
        host_->add_overlay(node(std::static_pointer_cast<aurora::Widget>(editor_dialog_)));
    editor_dialog_->show();
    editor_open_ = true;
}

auto SnippetPanel::close_editor() -> void {
    if (!editor_open_) {
        return;
    }
    if (editor_dialog_ != nullptr) {
        editor_dialog_->close();
    }
    if (editor_overlay_.has_value()) {
        host_->remove_overlay(*editor_overlay_);
        editor_overlay_.reset();
    }
    name_input_.reset();
    group_input_.reset();
    body_input_.reset();
    enter_switch_.reset();
    editor_notice_.reset();
    editor_dialog_.reset();
    editing_id_.reset();
    editor_open_ = false;
}

[[nodiscard]] auto SnippetPanel::build_editor() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    const conn::Snippet *existing =
        editing_id_.has_value() ? find_snippet(*editing_id_) : nullptr;
    auto children = std::vector<aurora::Node>{};

    auto title = make_text(settings_label(existing != nullptr ? "snippets.editor.title.edit"
                                                            : "snippets.editor.title_create"),
                           chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(title))));

    name_input_ = make_input(300.0F);
    name_input_->set_value(existing != nullptr ? existing->name : std::string{});
    children.push_back(field_row(settings_label("snippets.editor.field.name"),
                                 input_node(name_input_)));

    group_input_ = make_input(300.0F);
    group_input_->set_value(existing != nullptr ? existing->group : std::string{});
    children.push_back(field_row(settings_label("snippets.editor.field.group"),
                                 input_node(group_input_)));

    body_input_ = make_input(300.0F);
    body_input_->set_value(existing != nullptr ? existing->body : std::string{});
    children.push_back(field_row(settings_label("snippets.editor.field.body"),
                                 input_node(body_input_)));

    enter_switch_ = std::make_shared<aurora::Switch>();
    enter_switch_->set_value(existing != nullptr && existing->enter);
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(make_text(settings_label("snippets.editor.enter"), chrome.text)),
                     node(std::static_pointer_cast<aurora::Widget>(enter_switch_))},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    })));

    // 片段正文按纯文本进配置文件：编辑框固定把这条代价说出来（稿 F13/§6 第 6 条）。
    auto warning = make_text(settings_label("snippets.editor.plain_text_warning"),
                             chrome.text_dim);
    warning->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(warning))));

    editor_notice_ = make_text(std::string{}, aurora::colors::AURORA_RED);
    editor_notice_->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(editor_notice_)));

    auto save = make_button(settings_label("snippets.editor.action.save"), chrome.accent,
                            chrome.window_bg, 64.0F, [this]() -> void { save_editor(); },
                            chrome.card_line);
    auto cancel = make_button(settings_label("snippets.editor.action.cancel"), chrome.control_bg,
                              chrome.text, 64.0F, [this]() -> void { close_editor(); },
                              chrome.card_line);
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::move(save)), node(std::move(cancel))},
        .gap = 8.0F,
    })));

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(440.0F)
                           .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto SnippetPanel::show_editor_notice(const std::string &text) -> void {
    if (editor_notice_ != nullptr) {
        editor_notice_->set_content(text);
        editor_notice_->mark_needs_layout();  // 同 `set_notice`：换内容须重排才会重算显示缓存。
    }
}

auto SnippetPanel::save_editor() -> void {
    const std::string name = name_input_ != nullptr ? name_input_->value() : std::string{};
    const std::string group = group_input_ != nullptr ? group_input_->value() : std::string{};
    const std::string body = body_input_ != nullptr ? body_input_->value() : std::string{};
    const auto issue = conn::validate_snippet(name, group, body);
    if (issue != conn::SnippetIssue::None) {
        show_editor_notice(issue_text(issue));
        return;  // 无效草稿不落盘、不关框（就地改）。
    }

    conn::Snippet edited;
    edited.name = name;
    edited.group = group;
    edited.body = body;
    edited.enter = enter_switch_ != nullptr && enter_switch_->value();

    auto candidate = snippets_;
    if (editing_id_.has_value()) {
        const auto index = index_of(*editing_id_);
        if (!index.has_value()) {
            show_editor_notice(settings_label("snippets.notice.save_failed"));
            return;
        }
        edited.id = *editing_id_;
        candidate[*index] = std::move(edited);
    } else {
        edited.id = conn::next_snippet_id(snippets_);
        candidate.push_back(std::move(edited));
    }

    if (hooks_.persist == nullptr || !hooks_.persist(candidate)) {
        show_editor_notice(settings_label("snippets.notice.save_failed"));
        return;  // 内存副本不动：失败后看到的仍是落盘的那一版。
    }
    snippets_ = std::move(candidate);
    close_editor();
    set_notice(std::string{});
    install_overlay();  // 分组候选与条数都变了，整卡重装。
}

// ============================================================
// 占位符填充（D4①）
// ============================================================

auto SnippetPanel::open_fill(const conn::Snippet &snippet) -> void {
    close_fill();
    fill_snippet_ = snippet;
    fill_names_ = conn::snippet_placeholder_names(snippet.body);
    if (fill_names_.empty()) {
        deliver(snippet, snippet.body);
        return;
    }
    auto card = build_fill();
    fill_dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(card))));
    fill_overlay_ =
        host_->add_overlay(node(std::static_pointer_cast<aurora::Widget>(fill_dialog_)));
    fill_dialog_->show();
    fill_open_ = true;
}

auto SnippetPanel::close_fill() -> void {
    if (!fill_open_) {
        return;
    }
    if (fill_dialog_ != nullptr) {
        fill_dialog_->close();
    }
    if (fill_overlay_.has_value()) {
        host_->remove_overlay(*fill_overlay_);
        fill_overlay_.reset();
    }
    fill_inputs_.clear();
    fill_dialog_.reset();
    fill_snippet_ = conn::Snippet{};
    fill_names_.clear();
    fill_open_ = false;
}

[[nodiscard]] auto SnippetPanel::build_fill() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    fill_inputs_.clear();
    auto children = std::vector<aurora::Node>{};
    auto title = make_text(settings_label("snippets.fill.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(title))));

    for (const auto &name : fill_names_) {
        auto input = make_input(300.0F);
        fill_inputs_.push_back(input);
        children.push_back(field_row(name, input_node(input)));
    }

    auto send = make_button(settings_label("snippets.fill.action.send"), chrome.accent,
                            chrome.window_bg, 64.0F, [this]() -> void { confirm_fill(); },
                            chrome.card_line);
    auto cancel = make_button(settings_label("snippets.fill.action.cancel"), chrome.control_bg,
                              chrome.text, 64.0F, [this]() -> void { close_fill(); },
                              chrome.card_line);
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::move(send)), node(std::move(cancel))},
        .gap = 8.0F,
    })));

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(440.0F)
                           .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto SnippetPanel::confirm_fill() -> void {
    std::vector<conn::SnippetFill> fills;
    fills.reserve(fill_names_.size());
    for (std::size_t index = 0; index < fill_names_.size(); ++index) {
        const auto &input = fill_inputs_[index];
        fills.push_back(conn::SnippetFill{.name = fill_names_[index],
                                          .value = input != nullptr ? input->value()
                                                                    : std::string{}});
    }
    const auto snippet = fill_snippet_;
    const auto text = conn::render_snippet(snippet.body, fills);
    close_fill();
    deliver(snippet, text);
}

// ============================================================
// 删除两段式确认（D5①）
// ============================================================

auto SnippetPanel::open_delete_confirm(const std::string &id) -> void {
    close_delete_confirm();
    pending_delete_id_ = id;
    auto card = build_delete_confirm();
    confirm_dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(card))));
    confirm_overlay_ =
        host_->add_overlay(node(std::static_pointer_cast<aurora::Widget>(confirm_dialog_)));
    confirm_dialog_->show();
    delete_confirm_open_ = true;
}

auto SnippetPanel::close_delete_confirm() -> void {
    if (!delete_confirm_open_) {
        return;
    }
    if (confirm_dialog_ != nullptr) {
        confirm_dialog_->close();
    }
    if (confirm_overlay_.has_value()) {
        host_->remove_overlay(*confirm_overlay_);
        confirm_overlay_.reset();
    }
    confirm_dialog_.reset();
    pending_delete_id_.clear();
    delete_confirm_open_ = false;
}

[[nodiscard]] auto SnippetPanel::build_delete_confirm() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};
    auto title = make_text(settings_label("snippets.remove.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(title))));

    const conn::Snippet *snippet = find_snippet(pending_delete_id_);
    auto body = make_text(settings_label("snippets.remove.body",
                                         {{"name", snippet != nullptr ? snippet->name
                                                                             : pending_delete_id_}}),
                          chrome.text);
    body->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(body))));

    auto confirm = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("snippets.remove.action.confirm"),
        .color = aurora::colors::AURORA_RED,
        .on_color = chrome.window_bg,
        .min_width = 72.0F,
    });
    confirm->set_on_click([this]() -> void { confirm_delete(); });
    auto cancel = make_button(settings_label("snippets.remove.action.cancel"), chrome.control_bg,
                              chrome.text, 64.0F, [this]() -> void { close_delete_confirm(); },
                              chrome.card_line);
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::move(confirm)), node(std::move(cancel))},
        .gap = 8.0F,
    })));

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(420.0F)
                           .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto SnippetPanel::confirm_delete() -> void {
    const auto index = index_of(pending_delete_id_);
    if (!index.has_value()) {
        close_delete_confirm();
        return;
    }
    auto candidate = snippets_;
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(*index));
    if (hooks_.persist == nullptr || !hooks_.persist(candidate)) {
        close_delete_confirm();
        set_notice(settings_label("snippets.notice.save_failed"));
        return;
    }
    snippets_ = std::move(candidate);
    close_delete_confirm();
    set_notice(std::string{});
    install_overlay();
}

auto SnippetPanel::clear_state() -> void {
    if (overlay_index_.has_value()) {
        host_->remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    // 子控件引用先放：先拆树会让仍被本件持有的子控件成为「父已亡而子仍活」。
    search_input_.reset();
    group_dropdown_.reset();
    rows_column_.reset();
    count_text_.reset();
    notice_slot_.reset();
    notice_text_.reset();
    dialog_.reset();
    open_ = false;
}

}  // namespace borealis::ui
