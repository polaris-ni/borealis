#pragma once

// ============================================================
// 快捷片段面板（src/ui/snippet_panel.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.07` 延后子项的界面腿（稿 `codespec/UI_SNIPPETS.draft.md` D1–D6）：
// 左侧 520 dp 停靠卡片（D1①）＋ 新建/编辑框（D5①）＋ 占位符填充框（D4①）＋
// 删除两段式确认（D5①）。搜索与分组过滤走 `conn::snippet_*` 纯逻辑（D4/D6）。
//
// **本件不持有 `Session`／`TerminalView`，也不碰配置文件**：片段表经 `load` 取、
// 整表经 `persist` 交回装配层落 `snippets` 域、发送经 `send` 交出一段**已渲染的
// UTF-8 文本**（装配层决定发往哪个 pane，面板不广播、不猜格）。`enter` 只是给正文
// 尾追加一个 `\n`，编码与行尾折算归 `Session::send_text`（稿 F3）。
//
// 主体卡片只建一次：键入搜索词时只换行区子节点，不重建搜索框本身（焦点与编辑态
// 因此不丢）；片段表变化（保存/删除）才整卡重装，好让分组下拉的候选同步。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 `include/borealis/`。
// ============================================================

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/dialog.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/popup.h"  // OverlayHost
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"

#include "borealis/conn/snippet.h"

namespace borealis::ui {

class SnippetPanel {
  public:
    /// @brief 与装配层的全部接缝（struct-of-回调，同面板族口径）。
    struct Hooks {
        /// @brief 取当前片段表（`open()` 时读一次，之后以本件内存副本为准）。
        std::function<std::vector<conn::Snippet>()> load;
        /// @brief 整表落盘；成功回 true、失败回 false（失败时本件不改内存副本）。
        std::function<bool(const std::vector<conn::Snippet> &)> persist;
        /// @brief 把一段已渲染文本发到活动标签的焦点 pane；没有可发目标回 false。
        std::function<bool(const std::string &)> send;
    };

    SnippetPanel(class aurora::OverlayHost &host, Hooks hooks);
    SnippetPanel(const SnippetPanel &) = delete;
    auto operator=(const SnippetPanel &) -> SnippetPanel & = delete;
    ~SnippetPanel();

    /// @brief 换浮层宿主（面板搬窗，同 `KeysPanel::set_overlay_host`）：已开则先从旧宿主整层收掉，
    ///        再改指针；二级浮层里未提交的输入不保留。
    /// @param host 新增浮层宿主，**非拥有**：析构次序须晚于本件。
    auto set_overlay_host(aurora::OverlayHost &host) -> void;

    /// @brief 现在挂在谁的宿主上（nullptr＝从未挂过）。
    [[nodiscard]] auto overlay_host() const noexcept -> aurora::OverlayHost * { return host_; }

    auto open() -> void;
    auto close() -> void;
    auto toggle() -> void {
        open_ ? close() : open();
    }

    [[nodiscard]] auto is_open() const noexcept -> bool { return open_; }

    /// @brief 当前画出的行（过滤后的片段副本，保持存储序；观测面）。
    [[nodiscard]] auto visible_rows() const -> std::vector<conn::Snippet>;

    /// @brief 顶行留痕（空＝无）。观测面。
    [[nodiscard]] auto notice() const -> const std::string & { return notice_; }

    /// @brief 当前搜索词 / 分组过滤（空串＝全部分组）。观测面。
    [[nodiscard]] auto query() const -> const std::string & { return query_; }

    [[nodiscard]] auto group() const -> const std::string & { return group_; }

    /// @brief 新建/编辑框是否在场。观测面。
    [[nodiscard]] auto editor_open() const noexcept -> bool { return editor_open_; }

    /// @brief 占位符填充框是否在场；在场时逐行输入的名字（首现序）。观测面。
    [[nodiscard]] auto fill_open() const noexcept -> bool { return fill_open_; }

    [[nodiscard]] auto pending_fill_names() const -> const std::vector<std::string> & {
        return fill_names_;
    }

    /// @brief 删除确认是否在场；其目标片段 id（两段式的第一段就是「在场」这件事）。观测面。
    [[nodiscard]] auto delete_confirm_open() const noexcept -> bool { return delete_confirm_open_; }

    [[nodiscard]] auto pending_delete_id() const -> const std::string & { return pending_delete_id_; }

  private:
    auto install_overlay() -> void;
    [[nodiscard]] auto build_overlay() -> std::shared_ptr<aurora::Column>;
    [[nodiscard]] auto build_row(const conn::Snippet &snippet) -> aurora::Node;

    /// @brief 换行区子节点与计数/留痕文本；搜索与分组变化走这里（不重装主卡）。
    auto refresh_rows() -> void;

    [[nodiscard]] auto find_snippet(const std::string &id) const -> const conn::Snippet *;
    [[nodiscard]] auto index_of(const std::string &id) const -> std::optional<std::size_t>;

    /// @brief 一条片段的完整发送动作：有占位符先开填充框，否则直接交回 Hook。
    auto send_snippet(const conn::Snippet &snippet) -> void;
    /// @brief 已渲染正文的最终交付（`enter` 的换行在此追加）。
    auto deliver(const conn::Snippet &snippet, const std::string &text) -> void;
    /// @brief 搜索框回车：恰一条候选时发送，否则留痕条数。
    auto send_unique_candidate() -> void;

    auto open_editor(const std::optional<std::string> &id) -> void;
    auto close_editor() -> void;
    [[nodiscard]] auto build_editor() -> std::shared_ptr<aurora::Column>;
    auto save_editor() -> void;
    auto show_editor_notice(const std::string &text) -> void;

    auto open_fill(const conn::Snippet &snippet) -> void;
    auto close_fill() -> void;
    [[nodiscard]] auto build_fill() -> std::shared_ptr<aurora::Column>;
    auto confirm_fill() -> void;

    auto open_delete_confirm(const std::string &id) -> void;
    auto close_delete_confirm() -> void;
    [[nodiscard]] auto build_delete_confirm() -> std::shared_ptr<aurora::Column>;
    auto confirm_delete() -> void;

    auto set_notice(std::string text) -> void;
    auto clear_state() -> void;

    class aurora::OverlayHost *host_ = nullptr;  ///< 非拥有：当前挂在谁的宿主上。
    Hooks hooks_{};
    std::vector<conn::Snippet> snippets_{};  ///< 内存副本：落盘成功才替换。
    std::string query_{};
    std::string group_{};   ///< 空串＝全部分组；否则精确匹配该分组名。
    std::string notice_{};
    bool open_ = false;

    // ---- 主卡片 ----
    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};
    std::shared_ptr<aurora::TextInput> search_input_{};
    std::shared_ptr<aurora::Dropdown> group_dropdown_{};
    std::shared_ptr<aurora::Column> rows_column_{};
    std::shared_ptr<aurora::Text> count_text_{};
    /// @brief 留痕槽：0 或 1 个子节点。空留痕不占一行（`adopt_children` 换子后标脏）。
    std::shared_ptr<aurora::Column> notice_slot_{};
    std::shared_ptr<aurora::Text> notice_text_{};

    // ---- 新建/编辑框（D5①）----
    std::shared_ptr<aurora::Dialog> editor_dialog_{};
    std::optional<std::size_t> editor_overlay_{};
    bool editor_open_ = false;
    std::optional<std::string> editing_id_{};  ///< 空＝新建；有值＝编辑并保留 id。
    std::shared_ptr<aurora::TextInput> name_input_{};
    std::shared_ptr<aurora::TextInput> group_input_{};
    std::shared_ptr<aurora::TextInput> body_input_{};
    std::shared_ptr<aurora::Switch> enter_switch_{};
    std::shared_ptr<aurora::Text> editor_notice_{};

    // ---- 占位符填充框（D4①）----
    std::shared_ptr<aurora::Dialog> fill_dialog_{};
    std::optional<std::size_t> fill_overlay_{};
    bool fill_open_ = false;
    conn::Snippet fill_snippet_{};
    std::vector<std::string> fill_names_{};
    std::vector<std::shared_ptr<aurora::TextInput>> fill_inputs_{};

    // ---- 删除确认（D5① 两段式）----
    std::shared_ptr<aurora::Dialog> confirm_dialog_{};
    std::optional<std::size_t> confirm_overlay_{};
    bool delete_confirm_open_ = false;
    std::string pending_delete_id_{};
};

}  // namespace borealis::ui
