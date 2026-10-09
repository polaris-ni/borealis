// ============================================================
// 凭据询问对话框实现（src/ui/credential_prompt.cpp）
// ------------------------------------------------------------
// 形态照 settings_panel 的键位绑定对话框（Dialog + TextInput + 双按钮，挂宿主）：
// 掩码输入（set_obscure_text）保证旁人看不清屏幕；明文只在 on_secret 回调栈里
// 存活，本件不留副本（CONN.09 / 裁决 7.93）。
// ============================================================

#include "credential_prompt.h"

#include <utility>

#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/text.h"

#include "settings_i18n.h"     // settings_label
#include "settings_panel.h"    // settings_chrome

namespace borealis::ui {

namespace {

using aurora::Color;

/// @brief 按询问档取标题词条 key（文案进 i18n 表，不写死在控件里）。
[[nodiscard]] auto title_key_for(conn::SecretAsk kind) -> std::string_view {
    switch (kind) {
        case conn::SecretAsk::Password:
            return "connections.credential.password_title";
        case conn::SecretAsk::Passphrase:
            return "connections.credential.passphrase_title";
        case conn::SecretAsk::Interactive:
            return "connections.credential.interactive_title";
        case conn::SecretAsk::None:
            break;
    }
    return "connections.credential.password_title";
}

}  // namespace

CredentialPrompt::CredentialPrompt(aurora::OverlayHost &host, Hooks hooks)
    : host_{host}, hooks_{std::move(hooks)} {}

CredentialPrompt::~CredentialPrompt() {
    close();
}

auto CredentialPrompt::input_value() const -> std::string {
    return input_ != nullptr ? input_->value() : std::string{};
}

auto CredentialPrompt::ask(conn::SecretAsk kind, const std::string &profile_name) -> void {
    close();  // 重复询问先收旧框：浮层序号与输入框现场都按新建算，不留孤儿句柄。
    kind_ = kind;

    const auto chrome = settings_chrome();
    input_ = std::make_shared<aurora::TextInput>();
    input_->set_background(chrome.control_bg);
    input_->set_border_color(chrome.card_line);
    input_->set_focused_border_color(chrome.accent);
    input_->set_text_color(chrome.text);
    input_->set_obscure_text(true);  // 掩码：肩后窥屏看不到材料。
    input_->modifier.set(aurora::Modifier{}.width(280.0F));

    auto title = std::make_shared<aurora::Text>(aurora::TextProps{
        .content = settings_label(title_key_for(kind), {aurora::LocalizedString{profile_name}}),
        .text_color = chrome.text,
    });
    title->modifier.set(aurora::Modifier{}.fill_max_width());

    auto ok_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("connections.action.confirm"),
        .color = chrome.accent,
        .on_color = chrome.window_bg,
        .min_width = 80.0F,
    });
    ok_btn->set_on_click([this]() -> void {
        auto value = input_ != nullptr ? input_->value() : std::string{};
        close();
        if (hooks_.on_secret != nullptr) {
            // 明文经局部变量走回调即弃：本件没有第二个持有点。
            hooks_.on_secret(value);
        }
    });

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

    auto button_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(ok_btn))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(cancel_btn))},
        },
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 12.0F,
    });

    auto content = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(input_)},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(button_row))},
        },
        .gap = 8.0F,
    });
    // 底色 + 空点击吸收：照键位绑定对话框的实测口径（内容盒外的击中要被吸收，
    // 否则穿透模态层打到侧栏行区）。
    content->modifier.set(aurora::Modifier{}
                              .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                              .background(chrome.card_bg, 8.0F)
                              .border(1.0F, chrome.card_line)
                              .clickable([]() -> void {}));

    dialog_ = std::make_shared<aurora::Dialog>(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(content))});
    overlay_index_ = host_.add_overlay(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    dialog_->show();
    open_ = true;
}

auto CredentialPrompt::close() -> void {
    if (!open_) {
        return;
    }
    if (dialog_ != nullptr) {
        dialog_->close();
    }
    clear_state();
}

auto CredentialPrompt::clear_state() -> void {
    // 降序摘除不适用（本件一次只有一层浮层），但「先摘层再清句柄」的次序同款。
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    input_.reset();
    kind_ = conn::SecretAsk::None;
    open_ = false;
}

}  // namespace borealis::ui
