// ============================================================
// 连接新建/编辑向导实现（src/ui/connection_wizard.cpp）
// ------------------------------------------------------------
// 步骤 0＝两张类型卡（Local / SSH；D6 灰置未到货类型——不画即不承诺），步骤 1＝
// 该类型的字段集。认证方式与主机密钥策略用循环按钮（行内不用覆盖绘制不占布局
// 的控件，settings_panel 文件头那条形态约束同源）。校验失败只在 notice 行给
// 文案，不弹二级对话框。
// ============================================================

#include "connection_wizard.h"

#include <array>
#include <utility>

#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/text.h"

#include "settings_i18n.h"
#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

/// @brief 四档认证方式的循环序（模型注释的口径：password / privatekey / agent /
///        keyboard-interactive）。
constexpr std::array<std::string_view, 4> kAuthCycle{"agent", "password", "privatekey",
                                                     "keyboard-interactive"};

/// @brief 主机密钥策略的循环序（AcceptNew 为模型缺省）。
constexpr std::array<conn::KnownHostsPolicy, 4> kPolicyCycle{
    conn::KnownHostsPolicy::AcceptNew, conn::KnownHostsPolicy::Yes, conn::KnownHostsPolicy::No,
    conn::KnownHostsPolicy::Ask};

[[nodiscard]] auto auth_display(const std::string &method) -> std::string {
    return settings_label("connections.auth." + method);
}

[[nodiscard]] auto policy_display(conn::KnownHostsPolicy policy) -> std::string {
    switch (policy) {
        case conn::KnownHostsPolicy::AcceptNew:
            return settings_label("connections.policy.accept_new");
        case conn::KnownHostsPolicy::Yes:
            return settings_label("connections.policy.yes");
        case conn::KnownHostsPolicy::No:
            return settings_label("connections.policy.no");
        case conn::KnownHostsPolicy::Ask:
            return settings_label("connections.policy.ask");
    }
    return {};
}

[[nodiscard]] auto issue_key(conn::DraftIssue issue) -> std::string_view {
    switch (issue) {
        case conn::DraftIssue::EmptyName:
            return "connections.wizard.issue_empty_name";
        case conn::DraftIssue::EmptyHost:
            return "connections.wizard.issue_empty_host";
        case conn::DraftIssue::PortOutOfRange:
            return "connections.wizard.issue_port";
        case conn::DraftIssue::UnknownAuthMethod:
            return "connections.wizard.issue_auth";
        case conn::DraftIssue::None:
            break;
    }
    return "connections.wizard.issue_empty_name";
}

/// @brief 「标签 + 控件」一行的装配（标签走词条表；控件可以是输入框或循环按钮）。
[[nodiscard]] auto make_field_row(const std::string &label, aurora::Node control)
    -> aurora::Node {
    const auto chrome = settings_chrome();
    auto label_text = std::make_shared<aurora::Text>(
        aurora::TextProps{.content = label, .text_color = chrome.text_dim});
    label_text->modifier.set(aurora::Modifier{}.width(96.0F));
    auto row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(label_text))},
            std::move(control),
        },
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    });
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row))};
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

}  // namespace

ConnectionWizard::ConnectionWizard(aurora::OverlayHost &host, Hooks hooks)
    : host_{host}, hooks_{std::move(hooks)} {}

ConnectionWizard::~ConnectionWizard() {
    close();
}

auto ConnectionWizard::open_new() -> void {
    close();
    draft_ = conn::ProfileDraft{};
    editing_id_.clear();
    step_ = 0;
    build_and_show();
}

auto ConnectionWizard::open_edit(const conn::Profile &profile) -> void {
    close();
    draft_ = conn::profile_to_draft(profile);
    editing_id_ = profile.id;
    step_ = 1;  // 编辑直进字段步骤：类型不再改（改类型＝删了重建，语义更清晰）。
    build_and_show();
}

auto ConnectionWizard::collect_draft() -> void {
    draft_.name = name_input_ != nullptr ? name_input_->value() : draft_.name;
    if (draft_.type == conn::ConnectionType::Ssh) {
        draft_.host = host_input_ != nullptr ? host_input_->value() : draft_.host;
        draft_.user = user_input_ != nullptr ? user_input_->value() : draft_.user;
        draft_.identity_file = identity_input_ != nullptr ? identity_input_->value()
                                                          : draft_.identity_file;
        if (port_input_ != nullptr) {
            const auto text = port_input_->value();
            const auto parsed = std::strtol(text.c_str(), nullptr, 10);  // NOLINT: 界面口径宽容
            draft_.port = text.empty() ? 22 : static_cast<int>(parsed);
        }
    } else {
        draft_.command_line = command_input_ != nullptr ? command_input_->value()
                                                        : draft_.command_line;
        draft_.working_directory =
            workdir_input_ != nullptr ? workdir_input_->value() : draft_.working_directory;
    }
}

auto ConnectionWizard::save() -> void {
    collect_draft();
    const auto issue = conn::validate_draft(draft_);
    if (issue != conn::DraftIssue::None) {
        if (notice_ != nullptr) {
            const auto chrome = settings_chrome();
            notice_->set_content(settings_label(issue_key(issue)));
            notice_->text_color = chrome.accent;
            notice_->mark_needs_paint();
        }
        return;  // 不关框：用户就地改。
    }
    const auto id = editing_id_.empty() ? "profile:" + draft_.name : editing_id_;
    auto profile = conn::draft_to_profile(draft_, id);
    close();
    if (hooks_.on_save != nullptr) {
        hooks_.on_save(profile);
    }
}

auto ConnectionWizard::build_content() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};

    auto title = std::make_shared<aurora::Text>(aurora::TextProps{
        .content = settings_label(editing_id_.empty() ? "connections.wizard.title_new"
                                                      : "connections.wizard.title_edit"),
        .text_color = chrome.text,
    });
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title))});

    if (step_ == 0) {
        // 步骤 0：类型卡（D6：只有 Local / SSH 两张，串口/Telnet 未到货不占位）。
        auto make_type_card = [&](conn::ConnectionType type) -> aurora::Node {
            const auto is_ssh = type == conn::ConnectionType::Ssh;
            auto card = std::make_shared<aurora::Button>(aurora::ButtonProps{
                .label = settings_label(is_ssh ? "connections.type.ssh" : "connections.type.local"),
                .color = chrome.control_bg,
                .on_color = chrome.text,
                .border_color = chrome.accent,
                .border_width = 1.0F,
                .min_width = 120.0F,
                .min_height = 64.0F,
            });
            card->set_on_click([this, is_ssh]() -> void {
                draft_.type = is_ssh ? conn::ConnectionType::Ssh : conn::ConnectionType::Local;
                step_ = 1;
                build_and_show();  // 换步重建：字段集随类型卡变（设计稿标号 5）。
            });
            return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card))};
        };
        auto card_row = std::make_shared<aurora::Row>(aurora::RowProps{
            .children = {make_type_card(conn::ConnectionType::Local),
                         make_type_card(conn::ConnectionType::Ssh)},
            .gap = 12.0F,
        });
        children.push_back(
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card_row))});
    } else {
        // 步骤 1：字段集。名称两型共用；SSH/Local 各自的字段集按类型卡决定。
        name_input_ = make_input(240.0F);
        name_input_->set_value(draft_.name);
        children.push_back(make_field_row(settings_label("connections.field.name"), aurora::Node{std::static_pointer_cast<aurora::Widget>(name_input_)}));

        if (draft_.type == conn::ConnectionType::Ssh) {
            host_input_ = make_input(240.0F);
            host_input_->set_value(draft_.host);
            children.push_back(make_field_row(settings_label("connections.field.host"), aurora::Node{std::static_pointer_cast<aurora::Widget>(host_input_)}));

            port_input_ = make_input(240.0F);
            port_input_->set_value(std::to_string(draft_.port));
            children.push_back(make_field_row(settings_label("connections.field.port"), aurora::Node{std::static_pointer_cast<aurora::Widget>(port_input_)}));

            user_input_ = make_input(240.0F);
            user_input_->set_value(draft_.user);
            children.push_back(make_field_row(settings_label("connections.field.user"), aurora::Node{std::static_pointer_cast<aurora::Widget>(user_input_)}));

            // 认证方式：循环按钮（标签即当前档；行内下拉违形态约束，settings_panel 文件头）。
            auth_button_ = std::make_shared<aurora::Button>(aurora::ButtonProps{
                .label = auth_display(draft_.auth_method),
                .color = chrome.control_bg,
                .on_color = chrome.text,
                .border_color = chrome.card_line,
                .border_width = 1.0F,
                .min_width = 160.0F,
            });
            auth_button_->set_on_click([this]() -> void {
                collect_draft();  // 先收文本字段，别让循环按钮丢掉未提交的输入。
                for (std::size_t i = 0; i < kAuthCycle.size(); ++i) {
                    if (kAuthCycle[i] == draft_.auth_method) {
                        draft_.auth_method = std::string{kAuthCycle[(i + 1) % kAuthCycle.size()]};
                        break;
                    }
                }
                auth_button_->set_label(auth_display(draft_.auth_method));
                auth_button_->mark_needs_paint();
            });
            children.push_back(
                make_field_row(settings_label("connections.field.auth"), aurora::Node{std::static_pointer_cast<aurora::Widget>(auth_button_)}));

            identity_input_ = make_input(240.0F);
            identity_input_->set_value(draft_.identity_file);
            children.push_back(
                make_field_row(settings_label("connections.field.identity"), aurora::Node{std::static_pointer_cast<aurora::Widget>(identity_input_)}));
        } else {
            command_input_ = make_input(240.0F);
            command_input_->set_value(draft_.command_line);
            children.push_back(
                make_field_row(settings_label("connections.field.command"), aurora::Node{std::static_pointer_cast<aurora::Widget>(command_input_)}));

            workdir_input_ = make_input(240.0F);
            workdir_input_->set_value(draft_.working_directory);
            children.push_back(
                make_field_row(settings_label("connections.field.workdir"), aurora::Node{std::static_pointer_cast<aurora::Widget>(workdir_input_)}));
        }
    }

    // 校验留痕行（常驻：通过时为空，失败时给可读文案）。
    notice_ = std::make_shared<aurora::Text>(aurora::TextProps{.content = {}, .text_color = chrome.accent});
    notice_->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(notice_)});

    // 按钮行：步骤 0 只有「取消」；步骤 1 是「保存 + 取消」。
    auto cancel_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("connections.action.cancel"),
        .color = chrome.control_bg,
        .on_color = chrome.text,
        .border_color = chrome.card_line,
        .border_width = 1.0F,
        .min_width = 80.0F,
    });
    cancel_btn->set_on_click([this]() -> void {
        close();
        if (hooks_.on_cancel != nullptr) {
            hooks_.on_cancel();
        }
    });

    auto row_children = std::vector<aurora::Node>{
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(cancel_btn))}};
    if (step_ == 1) {
        auto save_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = settings_label("connections.action.save"),
            .color = chrome.accent,
            .on_color = chrome.window_bg,
            .min_width = 80.0F,
        });
        save_btn->set_on_click([this]() -> void { save(); });
        row_children.push_back(
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(save_btn))});
    }
    auto button_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(row_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 12.0F,
    });
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(button_row))});

    auto content = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    content->modifier.set(aurora::Modifier{}
                              .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                              .background(chrome.card_bg, 8.0F)
                              .border(1.0F, chrome.card_line)
                              .clickable([]() -> void {}));
    return content;
}

auto ConnectionWizard::build_and_show() -> void {
    if (dialog_ != nullptr) {
        // 换步/换值重建：先摘旧层再挂新层（序号按新建算，不留旧句柄）。
        if (overlay_index_.has_value()) {
            host_.remove_overlay(*overlay_index_);
            overlay_index_.reset();
        }
        dialog_.reset();
    }
    dialog_ = std::make_shared<aurora::Dialog>(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(build_content())});
    overlay_index_ = host_.add_overlay(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    dialog_->show();
    open_ = true;
}

auto ConnectionWizard::close() -> void {
    if (!open_) {
        return;
    }
    if (dialog_ != nullptr) {
        dialog_->close();
    }
    clear_state();
}

auto ConnectionWizard::clear_state() -> void {
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    name_input_.reset();
    host_input_.reset();
    port_input_.reset();
    user_input_.reset();
    identity_input_.reset();
    command_input_.reset();
    workdir_input_.reset();
    auth_button_.reset();
    notice_.reset();
    step_ = 0;
    open_ = false;
}

}  // namespace borealis::ui
