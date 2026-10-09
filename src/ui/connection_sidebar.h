#pragma once

// ============================================================
// 连接管理器侧栏（src/ui/connection_sidebar.h）
// ------------------------------------------------------------
// `codespec/UI_CONNECTIONS.draft.md` 的落地（D1–D8 已裁决收口）：档案树（搜索 +
// 三视图 D7 + 星标收藏）+ Quick connect（标号 2）+ 最近连接（标号 3）+ 新建/
// 编辑/导入入口。挂在场景根浮层宿主上的**左侧停靠卡片**（Q5：默认隐藏，命令
// 唤起；Q4：`connections.toggle` 经 PREF.04 体系注册，快捷键由装配层登记）。
//
// 本件不认识 config::Store，也不认识 libssh：档案表经 Hooks::load/persist 整表
// 进出（装配层接 config::Store 第五域），连接动作只交出档案（装配层做凭据解析
// 与 SshConnection 装配）。行排序/过滤全部取 conn/profile_views 的算好的行（D7）。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 include/borealis/。
// ============================================================

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/text_input.h"

#include "borealis/conn/profile.h"
#include "conn/profile_views.h"  // SidebarViewMode / SidebarRow

namespace borealis::ui {

class ConnectionSidebar {
public:
    /// @brief 与装配层的全部接缝（struct-of-回调，同 SettingsPanel::Hooks 形态）。
    struct Hooks {
        /// @brief 取档案整表（open 时现取；装配层从 config::Store 搬值）。
        std::function<std::vector<conn::Profile>()> load;
        /// @brief 把档案整表写回存储。@return 成功为 true（失败时侧栏保留内存态并重开表）。
        std::function<bool(const std::vector<conn::Profile> &)> persist;
        /// @brief 连接一条档案（装配层做凭据解析、询问、SshConnection 装配与开标签）。
        std::function<void(const conn::Profile &)> connect;
        /// @brief 请求新建（装配层打开向导）。
        std::function<void()> create_new;
        /// @brief 请求编辑一条档案（装配层打开向导并回填）。
        std::function<void(const conn::Profile &)> edit;
        /// @brief 执行 `~/.ssh/config` 只读导入（装配层读盘 + parse_ssh_config + 去重）。
        /// @return 本次导入条数（0 也合法：无文件/无新条目）。
        std::function<std::size_t()> import_ssh_config;
        /// @brief 最近连接的档案 id 列源（新→旧，至多 5 条；装配层从 config 域的
        ///        RecentConnection 表搬值——本头不反向包含 config，模块环）。
        std::function<std::vector<std::string>()> recent_ids;
    };

    ConnectionSidebar(class aurora::OverlayHost &host, Hooks hooks);
    ConnectionSidebar(const ConnectionSidebar &) = delete;
    auto operator=(const ConnectionSidebar &) -> ConnectionSidebar & = delete;
    ~ConnectionSidebar();

    auto open() -> void;
    auto close() -> void;
    auto toggle() -> void {
        open_ ? close() : open();
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 当前视图模式（观测面；切换会重建行区）。
    [[nodiscard]] auto view_mode() const noexcept -> conn::SidebarViewMode {
        return mode_;
    }
    auto set_view_mode(conn::SidebarViewMode mode) -> void;

    /// @brief 当前画出的行（观测面：次序即排版次序，判据比这份而不读浮层树）。
    [[nodiscard]] auto visible_rows() const -> std::vector<conn::SidebarRow>;

    /// @brief 最近连接区段当前画出的档案 id（次序＝行源次序；区段未画为空表）。
    [[nodiscard]] auto visible_recent_ids() const -> std::vector<std::string> {
        return recent_ids_;
    }

    /// @brief 搜索框当前内容（观测面）。
    [[nodiscard]] auto search_text() const -> std::string;

    /// @brief 触发一次行区刷新（收藏切换/搜索提交后由本件自查调用；测试亦可直调）。
    auto refresh_rows() -> void;

private:
    [[nodiscard]] auto build_overlay() -> std::shared_ptr<aurora::Column>;
    [[nodiscard]] auto build_row(const conn::SidebarRow &row) -> aurora::Node;
    auto toggle_favorite(const std::string &id) -> void;
    auto connect_profile(const std::string &id) -> void;
    auto connect_quick(const std::string &text) -> void;
    auto run_import() -> void;
    auto clear_state() -> void;

    class aurora::OverlayHost &host_;
    Hooks hooks_{};
    conn::ProfileStore store_{};
    conn::SidebarViewMode mode_ = conn::SidebarViewMode::All;
    std::string query_{};
    std::vector<std::string> recent_ids_{};
    std::string notice_{};  ///< 导入/落盘留痕（一次一句话，空＝无）。

    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};
    std::shared_ptr<aurora::TextInput> search_input_{};
    std::shared_ptr<aurora::TextInput> quick_input_{};
    std::vector<std::shared_ptr<aurora::Button>> star_buttons_{};
    std::vector<std::shared_ptr<aurora::Button>> row_buttons_{};
    bool open_ = false;
};

}  // namespace borealis::ui
