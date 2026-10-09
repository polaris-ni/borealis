// ============================================================
// 连接管理器侧栏实现（src/ui/connection_sidebar.cpp）
// ------------------------------------------------------------
// 形态要点（对照设计稿判据标号）：左侧 320dp 停靠卡片，标题行 + quick connect
// 行（标号 2）+ 搜索框 + 三视图 chips（D7）+ 滚动行区（标号 1：名字 + 类型图标
// + 星标）+ 底部动作行（新建/导入）。最近连接（标号 3）是行区上方独立小节，
// 与 WS.10 撤销关闭栈无关（数据源都是装配层搬的 connection.recent）。
// 「一个点了没反应的按钮」口径：Hooks 缺席的入口整枚不画（7.38⑥ F-b 同款）。
// ============================================================

#include "connection_sidebar.h"

#include <algorithm>
#include <utility>

#include "aurora/widget/button.h"
#include "aurora/widget/alignment.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/stack.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/text.h"

#include "settings_i18n.h"
#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

[[nodiscard]] auto type_icon(conn::ConnectionType type) -> std::string {
    return type == conn::ConnectionType::Ssh ? "#" : ">";  // ASCII 图标：不引图标库的第一步。
}

}  // namespace

ConnectionSidebar::ConnectionSidebar(aurora::OverlayHost &host, Hooks hooks)
    : host_{host}, hooks_{std::move(hooks)} {}

ConnectionSidebar::~ConnectionSidebar() {
    close();
}

auto ConnectionSidebar::search_text() const -> std::string {
    return search_input_ != nullptr ? search_input_->value() : query_;
}

auto ConnectionSidebar::set_view_mode(conn::SidebarViewMode mode) -> void {
    mode_ = mode;
    refresh_rows();
}

auto ConnectionSidebar::visible_rows() const -> std::vector<conn::SidebarRow> {
    return conn::sidebar_rows(store_, mode_, query_);
}

auto ConnectionSidebar::open() -> void {
    if (open_) {
        refresh_rows();
        return;
    }
    store_ = conn::ProfileStore{hooks_.load != nullptr ? hooks_.load()
                                                       : std::vector<conn::Profile>{}};
    query_.clear();
    notice_.clear();
    // 最近连接行源里只保留仍存在的档案（档案删除后 recent 里的悬空 id 不上屏）。
    recent_ids_.clear();
    if (hooks_.recent_ids != nullptr) {
        for (const auto &id : hooks_.recent_ids()) {
            if (store_.find(id) != nullptr) {
                recent_ids_.push_back(id);
            }
        }
    }

    auto mask = std::make_shared<aurora::Canvas>(
        [chrome = settings_chrome()](aurora::Painter &painter, const aurora::Rect &box) -> void {
            painter.fill_rect(box, chrome.window_bg);
        });
    mask->modifier.set(
        aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));

    auto card = build_overlay();
    auto layer = std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(mask))},
                                  aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card))}},
        aurora::Alignment::CenterLeft);

    dialog_ = std::make_shared<aurora::Dialog>(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(layer))});
    overlay_index_ = host_.add_overlay(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    dialog_->show();
    open_ = true;
}

auto ConnectionSidebar::close() -> void {
    if (!open_) {
        return;
    }
    if (dialog_ != nullptr) {
        dialog_->close();
    }
    clear_state();
}

auto ConnectionSidebar::refresh_rows() -> void {
    if (!open_) {
        return;
    }
    query_ = search_input_ != nullptr ? search_input_->value() : query_;
    // 整块重建（同 SettingsPanel::select_page 的口径：行序变了就地改计数会错位）。
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    auto mask = std::make_shared<aurora::Canvas>(
        [chrome = settings_chrome()](aurora::Painter &painter, const aurora::Rect &box) -> void {
            painter.fill_rect(box, chrome.window_bg);
        });
    mask->modifier.set(
        aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));
    auto card = build_overlay();
    auto layer = std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(mask))},
                                  aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card))}},
        aurora::Alignment::CenterLeft);
    dialog_ = std::make_shared<aurora::Dialog>(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(layer))});
    overlay_index_ = host_.add_overlay(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    dialog_->show();
}

auto ConnectionSidebar::toggle_favorite(const std::string &id) -> void {
    const conn::Profile *profile = store_.find(id);
    if (profile == nullptr) {
        return;
    }
    auto next = *profile;
    next.favorite = !next.favorite;
    if (!store_.update(std::move(next))) {
        return;
    }
    if (hooks_.persist != nullptr) {
        hooks_.persist(store_.profiles());  // 失败留内存态：侧栏里继续可用，落盘原因归日志。
    }
    refresh_rows();
}

auto ConnectionSidebar::connect_profile(const std::string &id) -> void {
    const conn::Profile *profile = store_.find(id);
    if (profile == nullptr || hooks_.connect == nullptr) {
        return;
    }
    hooks_.connect(*profile);  // 关闭交给装配层（连接动作可能需要先弹询问框，框要浮在页上）。
}

auto ConnectionSidebar::connect_quick(const std::string &text) -> void {
    const auto parsed = conn::parse_quick_connect(text);
    if (!parsed.has_value() || hooks_.connect == nullptr) {
        return;
    }
    hooks_.connect(conn::quick_connect_profile(parsed->first, parsed->second));
}

auto ConnectionSidebar::run_import() -> void {
    if (hooks_.import_ssh_config == nullptr) {
        return;
    }
    const auto count = hooks_.import_ssh_config();
    store_ = conn::ProfileStore{hooks_.load != nullptr ? hooks_.load()
                                                       : store_.profiles()};
    notice_ = settings_label("connections.notice.imported",
                             {aurora::LocalizedString{std::to_string(count)}});
    refresh_rows();
}

auto ConnectionSidebar::build_row(const conn::SidebarRow &row) -> aurora::Node {
    const auto chrome = settings_chrome();
    const conn::Profile *profile = store_.find(row.id);

    auto name_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = type_icon(row.type) + "  " + row.name,
        .color = chrome.control_bg,
        .on_color = chrome.text,
        .border_color = chrome.card_line,
        .border_width = 0.0F,
        .min_width = 200.0F,
    });
    name_btn->set_on_click([this, id = row.id]() -> void { connect_profile(id); });
    row_buttons_.push_back(name_btn);

    auto children = std::vector<aurora::Node>{
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(name_btn))}};

    auto star = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = row.favorite ? "*" : "-",
        .color = chrome.control_bg,
        .on_color = row.favorite ? chrome.accent : chrome.text_dim,
        .min_width = 28.0F,
    });
    star->set_on_click([this, id = row.id]() -> void { toggle_favorite(id); });
    star_buttons_.push_back(star);
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(star))});

    if (profile != nullptr && hooks_.edit != nullptr) {
        auto edit_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = settings_label("connections.action.edit"),
            .color = chrome.control_bg,
            .on_color = chrome.text_dim,
            .min_width = 28.0F,
        });
        edit_btn->set_on_click([this, profile]() -> void { hooks_.edit(*profile); });
        children.push_back(
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(edit_btn))});
    }

    auto row_widget = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 4.0F,
    });
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row_widget))};
}

auto ConnectionSidebar::build_overlay() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    star_buttons_.clear();
    row_buttons_.clear();
    auto children = std::vector<aurora::Node>{};

    // ---- 标题行 + 关闭 ----
    auto title = std::make_shared<aurora::Text>(
        aurora::TextProps{.content = settings_label("connections.title"), .text_color = chrome.text});
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    auto close_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("connections.action.close"),
        .color = chrome.control_bg,
        .on_color = chrome.text_dim,
        .min_width = 48.0F,
    });
    close_btn->set_on_click([this]() -> void { close(); });
    auto title_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title))},
                     aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(close_btn))}},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    });
    children.push_back(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title_row))});

    // ---- quick connect 行（标号 2）----
    quick_input_ = std::make_shared<aurora::TextInput>();
    quick_input_->set_background(chrome.control_bg);
    quick_input_->set_border_color(chrome.card_line);
    quick_input_->set_focused_border_color(chrome.accent);
    quick_input_->set_text_color(chrome.text);
    quick_input_->set_placeholder(settings_label("connections.quick.placeholder"));
    quick_input_->modifier.set(aurora::Modifier{}.width(200.0F));
    auto quick_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("connections.action.connect"),
        .color = chrome.accent,
        .on_color = chrome.window_bg,
        .min_width = 64.0F,
    });
    quick_btn->set_on_click([this]() -> void { connect_quick(quick_input_->value()); });
    auto quick_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {aurora::Node{std::static_pointer_cast<aurora::Widget>(quick_input_)},
                     aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(quick_btn))}},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    });
    children.push_back(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(quick_row))});

    // ---- 搜索框 ----
    search_input_ = std::make_shared<aurora::TextInput>();
    search_input_->set_background(chrome.control_bg);
    search_input_->set_border_color(chrome.card_line);
    search_input_->set_focused_border_color(chrome.accent);
    search_input_->set_text_color(chrome.text);
    search_input_->set_placeholder(settings_label("connections.search.placeholder"));
    search_input_->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(search_input_)});

    // ---- 三视图 chips（D7）----
    auto make_chip = [&](conn::SidebarViewMode mode) -> aurora::Node {
        auto chip = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = settings_label(mode == conn::SidebarViewMode::All     ? "connections.view.all"
                                    : mode == conn::SidebarViewMode::Favorites
                                        ? "connections.view.favorites"
                                        : "connections.view.groups"),
            .color = mode_ == mode ? chrome.accent : chrome.control_bg,
            .on_color = mode_ == mode ? chrome.window_bg : chrome.text,
            .border_color = chrome.card_line,
            .border_width = 1.0F,
            .min_width = 64.0F,
        });
        chip->set_on_click([this, mode]() -> void { set_view_mode(mode); });
        return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(chip))};
    };
    auto chips = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {make_chip(conn::SidebarViewMode::All),
                     make_chip(conn::SidebarViewMode::Favorites),
                     make_chip(conn::SidebarViewMode::Groups)},
        .gap = 8.0F,
    });
    children.push_back(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(chips))});

    // ---- 最近连接小节（标号 3；行源为空整节约过——不画空标题）----
    if (!recent_ids_.empty()) {
        auto recent_title = std::make_shared<aurora::Text>(aurora::TextProps{
            .content = settings_label("connections.recent.title"), .text_color = chrome.text_dim});
        children.push_back(aurora::Node{
            std::static_pointer_cast<aurora::Widget>(std::move(recent_title))});
        for (const auto &id : recent_ids_) {
            const conn::Profile *profile = store_.find(id);
            if (profile == nullptr) {
                continue;  // 行源与档案表不同步时少画一行，绝不画悬空条目。
            }
            auto recent_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
                .label = type_icon(profile->type) + "  " + profile->name,
                .color = chrome.control_bg,
                .on_color = chrome.text_dim,
                .border_color = chrome.card_line,
                .border_width = 0.0F,
                .min_width = 280.0F,
            });
            const auto captured_id = id;
            recent_btn->set_on_click(
                [this, captured_id]() -> void { connect_profile(captured_id); });
            children.push_back(aurora::Node{
                std::static_pointer_cast<aurora::Widget>(std::move(recent_btn))});
        }
    }

    // ---- 留痕行（导入条数等；空则不占）----
    if (!notice_.empty()) {
        auto notice_text = std::make_shared<aurora::Text>(
            aurora::TextProps{.content = notice_, .text_color = chrome.text_dim});
        notice_text->modifier.set(aurora::Modifier{}.fill_max_width());
        children.push_back(aurora::Node{
            std::static_pointer_cast<aurora::Widget>(std::move(notice_text))});
    }

    // ---- 滚动行区（标号 1）----
    auto rows_column = std::make_shared<aurora::Column>(aurora::ColumnProps{.gap = 4.0F});
    for (const auto &row : visible_rows()) {
        rows_column->add(build_row(row));
    }
    auto rows_scroll = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
        .child = aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(rows_column))},
        .step = 32.0F,
    });
    rows_scroll->modifier.set(aurora::Modifier{}.fill_max_width().expand());
    children.push_back(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(rows_scroll))});

    // ---- 底部动作行 ----
    auto action_children = std::vector<aurora::Node>{};
    if (hooks_.create_new != nullptr) {
        auto new_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = settings_label("connections.action.new"),
            .color = chrome.control_bg,
            .on_color = chrome.text,
            .border_color = chrome.card_line,
            .border_width = 1.0F,
            .min_width = 72.0F,
        });
        new_btn->set_on_click([this]() -> void { hooks_.create_new(); });
        action_children.push_back(
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(new_btn))});
    }
    if (hooks_.import_ssh_config != nullptr) {
        auto import_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = settings_label("connections.action.import"),
            .color = chrome.control_bg,
            .on_color = chrome.text,
            .border_color = chrome.card_line,
            .border_width = 1.0F,
            .min_width = 72.0F,
        });
        import_btn->set_on_click([this]() -> void { run_import(); });
        action_children.push_back(
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(import_btn))});
    }
    auto actions = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(action_children),
        .gap = 8.0F,
    });
    children.push_back(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(actions))});

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(320.0F)
                           .fill_max_height()
                           .padding(aurora::EdgeInsets{12.0F, 12.0F, 12.0F, 12.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto ConnectionSidebar::clear_state() -> void {
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    search_input_.reset();
    quick_input_.reset();
    star_buttons_.clear();
    row_buttons_.clear();
    open_ = false;
}

}  // namespace borealis::ui
