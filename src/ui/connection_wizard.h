#pragma once

// ============================================================
// 连接新建/编辑向导（src/ui/connection_wizard.h）
// ------------------------------------------------------------
// 设计稿 §2 标号 5 的分步表单：步骤 0 选类型卡（Local / SSH），步骤 1 填字段集
// （类型卡决定字段集，D6：串口/Telnet 未到货不出现）。校验走纯逻辑
// `validate_draft`（conn/profile_views.h），本件只把问题码折成可读文案。
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

#include "borealis/conn/profile.h"
#include "conn/profile_views.h"  // ProfileDraft / DraftIssue

namespace borealis::ui {

class ConnectionWizard {
public:
    /// @brief 保存/取消接缝。on_save 收完整档案（校验已过、id 已定）。
    struct Hooks {
        std::function<void(const conn::Profile &)> on_save;
        std::function<void()> on_cancel;
    };

    ConnectionWizard(class aurora::OverlayHost &host, Hooks hooks);
    ConnectionWizard(const ConnectionWizard &) = delete;
    auto operator=(const ConnectionWizard &) -> ConnectionWizard & = delete;
    ~ConnectionWizard();

    /// @brief 新建：清空草稿、进步骤 0。
    auto open_new() -> void;

    /// @brief 编辑：回填草稿（profile_to_draft）、直进字段步骤。
    auto open_edit(const conn::Profile &profile) -> void;

    auto close() -> void;

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 当前步骤（0＝类型卡，1＝字段集；观测面）。
    [[nodiscard]] auto step() const noexcept -> int {
        return step_;
    }

    /// @brief 当前草稿（观测面；用例判「表单里的值进了草稿」）。
    [[nodiscard]] auto draft() const -> const conn::ProfileDraft & {
        return draft_;
    }

    /// @brief 正在编辑的档案 id；新建时为空（观测面）。
    [[nodiscard]] auto editing_id() const -> const std::string & {
        return editing_id_;
    }

private:
    auto build_and_show() -> void;
    [[nodiscard]] auto build_content() -> std::shared_ptr<aurora::Column>;
    auto collect_draft() -> void;
    auto save() -> void;
    auto clear_state() -> void;

    class aurora::OverlayHost &host_;
    Hooks hooks_{};
    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};

    conn::ProfileDraft draft_{};
    std::string editing_id_{};  ///< 空＝新建。
    int step_ = 0;

    // 字段控件（每步重建时新建；句柄只在两次重建之间存活）。
    std::shared_ptr<aurora::TextInput> name_input_{};
    std::shared_ptr<aurora::TextInput> host_input_{};
    std::shared_ptr<aurora::TextInput> port_input_{};
    std::shared_ptr<aurora::TextInput> user_input_{};
    std::shared_ptr<aurora::TextInput> identity_input_{};
    std::shared_ptr<aurora::TextInput> command_input_{};
    std::shared_ptr<aurora::TextInput> workdir_input_{};
    std::shared_ptr<aurora::Button> auth_button_{};   // 认证方式循环按钮（四档，避开行内下拉）。
    std::shared_ptr<aurora::Text> notice_{};          // 校验失败的可读文案。
    bool open_ = false;
};

}  // namespace borealis::ui
