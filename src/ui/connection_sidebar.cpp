// ============================================================
// 连接管理器侧栏实现（src/ui/connection_sidebar.cpp）
// ------------------------------------------------------------
// 形态要点（对照设计稿判据标号）：屏 1 那条**常驻列**（裁决 7.180 改判，旧形态是浮层卡片）——
// 折叠＝34 dp 图标轨，展开＝同一条列变宽到 320 dp：标题行 + quick connect 行（标号 2）+ 搜索框 +
// 三视图 chips（D7）+ 滚动行区（标号 1：名字 + 类型图标 + 星标）+ 底部动作行（新建/导入）。
// 最近连接（标号 3）是行区上方独立小节，与 WS.10 撤销关闭栈无关（数据源都是装配层搬的 connection.recent）。
// 两态互斥地占同一个槽位，开合只在 `slot_` 内换那一枚子节点——槽位本身从建窗起不换（理由见头文件）。
// 「一个点了没反应的按钮」口径：Hooks 缺席的入口整枚不画（7.38⑥ F-b 同款）。
// ============================================================

#include "connection_sidebar.h"

#include <algorithm>
#include <utility>

#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/text.h"

#include "settings_i18n.h"
#include "settings_panel.h"  // settings_chrome
#include "connection_glyph.h"  // 类型图标的唯一字形表（`SPEC.FEAT.WS.04`，与标签栏同一处）

namespace borealis::ui {

namespace {

/// @brief 展开态那一列的宽（屏 2 与 §2.2 那行都是 320 dp；折叠态的 34 dp 住在 `sidebar_rail.cpp`）。
constexpr float kPanelWidthDp = 320.0F;

}  // namespace

// 三件一次建齐：槽位（不换）、折叠态那枚轨、展开态那一列。缺省折叠（§4 N1），于是新窗口先看见那条轨，
// 面板内容直到第一次展开才建——`rebuild_panel()` 里的控件色值是构建期取定的，晚报一次就少一次换肤欠账。
ConnectionSidebar::ConnectionSidebar(Hooks hooks) : hooks_{std::move(hooks)} {
    slot_ = std::make_shared<aurora::Column>();
    slot_->set_modifier(aurora::Modifier{}.fill_max_height());
    rail_ = std::make_shared<SidebarRail>(SidebarRailHooks{
        .select_view = [this](conn::SidebarViewMode mode) -> void { expand_to(mode); },
        .note_press = [this]() -> void {
            if (hooks_.note_window_press != nullptr) {
                hooks_.note_window_press();
            }
        },
    });
    panel_ = std::make_shared<aurora::Column>(aurora::ColumnProps{.gap = 8.0F});
    mount_slot();
}

ConnectionSidebar::~ConnectionSidebar() = default;

/// @brief 两态互斥占同一格的唯一机制：按开合档把槽里那枚子节点换成轨或面板。
auto ConnectionSidebar::mount_slot() -> void {
    // 两枚都**copy** 进槽：本件按生命周期持有它们（`rebuild_panel` 与 `set_active_view` 都要再碰），
    // 搬进 `Node` 就把成员掏空了，下一次展开会对着空指针建内容。
    const std::shared_ptr<aurora::Widget> current =
        open_ ? std::static_pointer_cast<aurora::Widget>(panel_)
              : std::static_pointer_cast<aurora::Widget>(rail_);
    auto &nodes = slot_->child_nodes_mut();
    nodes.clear();
    nodes.push_back(aurora::Node{current});
    // 34 dp ↔ 320 dp 是「同一条列变宽」，根 Row 的第一格宽度因此要重算：只标脏绘制会把分栏列
    // 留在旧宽度上（面板压住终端或轨身后留一条空缝）。
    slot_->mark_needs_layout();
    slot_->mark_needs_paint();
}

auto ConnectionSidebar::search_text() const -> std::string {
    return search_input_ != nullptr ? search_input_->value() : query_;
}

auto ConnectionSidebar::set_view_mode(conn::SidebarViewMode mode) -> void {
    mode_ = mode;
    rail_->set_active_view(mode);  // 两态指向同一个视图档：面板的 chips 与轨上亮的那一枚不许错位
    refresh_rows();
}

auto ConnectionSidebar::rail_active_view() const noexcept -> conn::SidebarViewMode {
    return rail_->active_view();
}

auto ConnectionSidebar::visible_rows() const -> std::vector<conn::SidebarRow> {
    return conn::sidebar_rows(store_, mode_, query_);
}

auto ConnectionSidebar::expand_to(conn::SidebarViewMode mode) -> void {
    if (open_ && mode_ == mode) {
        return;  // 已经是那一档而且已经展开：再点同一枚图标不该把行区重建一次
    }
    mode_ = mode;
    rail_->set_active_view(mode);
    if (!open_) {
        apply_open();
        if (hooks_.persist_collapsed != nullptr) {
            hooks_.persist_collapsed(false);
        }
    } else {
        refresh_rows();
    }
}

auto ConnectionSidebar::open() -> void {
    if (open_) {
        refresh_rows();
        return;
    }
    apply_open();
    if (hooks_.persist_collapsed != nullptr) {
        hooks_.persist_collapsed(false);
    }
}

auto ConnectionSidebar::close() -> void {
    if (!open_) {
        return;
    }
    apply_close();
    if (hooks_.persist_collapsed != nullptr) {
        hooks_.persist_collapsed(true);
    }
}

auto ConnectionSidebar::set_collapsed(bool collapsed) -> void {
    // 配置是**来向**而不是去向：这条腿不回写 `persist_collapsed`，否则广播一进来就又发一次广播。
    if (collapsed == !open_) {
        return;
    }
    if (collapsed) {
        apply_close();
    } else {
        apply_open();
    }
}

auto ConnectionSidebar::apply_open() -> void {
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
    open_ = true;
    rail_->set_active_view(mode_);
    rebuild_panel();
    mount_slot();
}

auto ConnectionSidebar::apply_close() -> void {
    clear_state();
    mount_slot();
}

auto ConnectionSidebar::refresh_rows() -> void {
    if (!open_) {
        return;
    }
    query_ = search_input_ != nullptr ? search_input_->value() : query_;
    // 整块重建（同 SettingsPanel::select_page 的口径：行序变了就地改计数会错位），槽位与面板本体都不换。
    rebuild_panel();
}

auto ConnectionSidebar::refresh_chrome() -> void {
    if (!open_) {
        return;  // 折叠态那枚轨的色值每次重绘现读 `settings_chrome()`，没有重建的必要
    }
    rebuild_panel();
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
    // `-J` 尾段与坏跳表留痕（`SPEC.FEAT.CONN.03` 批 7 D5/D6）：文法只在纯逻辑层有一份答案，
    // 本件只搬「不连 + 一句话」这条反馈——留痕行重开页即清，机制同导入条数那一支。
    const auto [parsed, issue] = conn::parse_quick_connect_line(text);
    if (issue == conn::QuickJumpIssue::BadJumpList) {
        notice_ = settings_label("connections.notice.quick_jump_bad");
        refresh_rows();  // 留痕随整块重建上屏（行序未变，改的是留痕行）。
        return;
    }
    if (!parsed.has_value() || hooks_.connect == nullptr) {
        return;
    }
    hooks_.connect(
        conn::quick_connect_profile(parsed->host, parsed->port, parsed->jumps));
}

auto ConnectionSidebar::run_import() -> void {
    if (hooks_.import_ssh_config == nullptr) {
        return;
    }
    const auto count = hooks_.import_ssh_config();
    store_ = conn::ProfileStore{hooks_.load != nullptr ? hooks_.load()
                                                       : store_.profiles()};
    notice_ = settings_label("connections.notice.imported",
                             {{"count", static_cast<std::int64_t>(count)}});
    refresh_rows();
}

auto ConnectionSidebar::build_row(const conn::SidebarRow &row) -> aurora::Node {
    const auto chrome = settings_chrome();
    const conn::Profile *profile = store_.find(row.id);

    auto name_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = std::string{connection_type_glyph(row.type)} + "  " + row.name,
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

auto ConnectionSidebar::rebuild_panel() -> void {
    const auto chrome = settings_chrome();
    star_buttons_.clear();
    row_buttons_.clear();
    auto children = std::vector<aurora::Node>{};

    // ---- 标题行 + 收起（词条仍是 `connections.action.close`：屏 2 画的就是「关闭」，而改判后它关掉的是
    //      这一列的展开态，折回那条 34 dp 轨——文案不在本轮改判范围内，裁决 7.180 记这一处口径转移）----
    auto title = std::make_shared<aurora::Text>(
        aurora::TextProps{.content = settings_label("connections.title"), .text_color = chrome.text});
    // 主轴权重而不是 `fill_max_width()`：行内那一枚「宽轴 min=max=父级上限」会把标题顶成整幅 296 dp，
    // 于是紧随其后的收起钮落在 308 dp 之外、被 320 dp 的列裁掉（旧浮层形态下同一处没有证人，
    // 常驻列的宽度判据把它抓了出来——钮必须点得到，因为收起只剩这一枚）。
    title->set_modifier(aurora::Modifier{}.expand(1.0F));
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
    quick_input_->set_modifier(aurora::Modifier{}.width(200.0F));
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
    search_input_->set_modifier(aurora::Modifier{}.fill_max_width());
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
                .label = std::string{connection_type_glyph(profile->type)} + "  " + profile->name,
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
        notice_text->set_modifier(aurora::Modifier{}.fill_max_width());
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
    rows_scroll->set_modifier(aurora::Modifier{}.fill_max_width().expand());
    children.push_back(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(rows_scroll))});

    // ---- 底部动作行 ----
    auto action_children = std::vector<aurora::Node>{};
    if (hooks_.create_new != nullptr) {
        auto new_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = settings_label("connections.action.create"),
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

    auto &nodes = panel_->child_nodes_mut();
    // 那一列的本体修饰（宽 320、通高、卡片底与一条描边）每次重建时现取活动档：色值是构建期定下的，
    // 换肤那条腿就走在这里（`refresh_chrome()` ⇒ `rebuild_panel()`），而不是刻在建窗那一刻。
    panel_->set_modifier(aurora::Modifier{}
                             .width(kPanelWidthDp)
                             .fill_max_height()
                             .padding(aurora::EdgeInsets{12.0F, 12.0F, 12.0F, 12.0F})
                             .background(chrome.card_bg, 0.0F)
                             .border(1.0F, chrome.card_line)
                             .clickable([]() -> void {}));
    // 就地换子里那一批：`panel_` 这一枚从建窗起就钉在槽里，宽度与底/描边那层修饰也留在它身上
    // （旧形态是整张浮层重挂，改判后没有可摘的浮层了）。
    nodes = std::move(children);
    panel_->mark_needs_layout();
    panel_->mark_needs_paint();
}

auto ConnectionSidebar::clear_state() -> void {
    open_ = false;
    search_input_.reset();
    quick_input_.reset();
    star_buttons_.clear();
    row_buttons_.clear();
    panel_->child_nodes_mut().clear();
}

}  // namespace borealis::ui
