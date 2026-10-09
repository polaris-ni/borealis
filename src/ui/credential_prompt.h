#pragma once

// ============================================================
// 凭据询问对话框（src/ui/credential_prompt.h）
// ------------------------------------------------------------
// CONN.09 的「每次询问」降级腿在界面上的形态（设计稿 Q3 裁决）：ask_every_time
// 或凭据库取不到时弹出，取值只经 on_secret 回调交给**本次** libssh 认证，本件
// 不留副本、不落盘、不进日志（裁决 7.93）。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 include/borealis/。
// ============================================================

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text_input.h"

#include "conn/profile_views.h"  // SecretAsk

namespace borealis::ui {

class CredentialPrompt {
public:
    /// @brief 提交/取消接缝。on_secret 收到的明文只在回调栈内存活，实现方即取即用。
    struct Hooks {
        std::function<void(const std::string &)> on_secret;
        std::function<void()> on_cancel;
    };

    CredentialPrompt(class aurora::OverlayHost &host, Hooks hooks);
    CredentialPrompt(const CredentialPrompt &) = delete;
    auto operator=(const CredentialPrompt &) -> CredentialPrompt & = delete;
    ~CredentialPrompt();

    /// @brief 弹出询问。@param kind 要哪种材料 @param profile_name 展示名（进标题）。
    auto ask(conn::SecretAsk kind, const std::string &profile_name) -> void;

    auto close() -> void;

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 输入框当前内容（观测面；用例判「提交把框里的东西原样交出去」）。
    [[nodiscard]] auto input_value() const -> std::string;

private:
    auto build_dialog() -> void;
    auto clear_state() -> void;

    class aurora::OverlayHost &host_;
    Hooks hooks_{};
    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};
    std::shared_ptr<aurora::TextInput> input_{};
    conn::SecretAsk kind_ = conn::SecretAsk::None;
    bool open_ = false;
};

}  // namespace borealis::ui
