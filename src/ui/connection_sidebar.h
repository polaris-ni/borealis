#pragma once

// ============================================================
// 连接管理器侧栏（src/ui/connection_sidebar.h）
// ------------------------------------------------------------
// `codespec/UI_CONNECTIONS.draft.md` 的落地（D1–D8 已裁决收口）：档案树（搜索 +
// 三视图 D7 + 星标收藏）+ Quick connect（标号 2）+ 最近连接（标号 3）+ 新建/
// 编辑/导入入口。
//
// **挂载形态（2026-10-30 改判，裁决 7.180）**：本件不再是「挂在场景根浮层宿主上的左侧停靠卡片、
// 命令唤起（旧 Q5）」，而是屏 1 那条**常驻列**——折叠态是 34 dp 图标轨（`src/ui/sidebar_rail.h`），
// 展开态是同一条列变宽到 320 dp（`codespec/UI_OVERVIEW.draft.md` §2.2＋§4 N1）。两态互斥地占同一个
// 槽位，本件把那个槽位（`slot_widget()`）交回装配层挂进根 Row 的第一格，开合只在槽内换子节点。
// 由此带出三条旧口径的改判：① 浮层与整窗遮罩（mask）那一跳撤销——窗外内容不再被压暗，也没有
// 「点窗外即关」那条腿了（收起走面板标题行那一枚动作，语义仍是「关掉这一列」）；② 每扇窗各有一份
// 实例（一枚控件节点只容得下一个父节点，常驻列搬不动，稿 D6 那条「面板搬窗」对本件失效）；③ 展开态
// 入配置并被记住（`appearance.sidebar_collapsed`，N1 缺省折叠），`connections.toggle` 开合的是
// **发起命令那一扇窗**自己的列。
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

#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/text_input.h"

#include "borealis/conn/profile.h"
#include "conn/profile_views.h"  // SidebarViewMode / SidebarRow
#include "sidebar_rail.h"        // 折叠态那一枚（本件持有它，两态共用同一个槽位）

namespace borealis::ui {

class ConnectionSidebar {
public:
    /// @brief 与装配层的全部接缝（struct-of-回调，同 SettingsPanel::Hooks 形态）。
    struct Hooks {
        /// @brief 取档案整表（展开时现取；装配层从 config::Store 搬值）。
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
        /// @brief 折叠态落盘（`appearance.sidebar_collapsed`，裁决 7.180）：开合各报一次，本件不写盘。
        std::function<void(bool collapsed)> persist_collapsed;
        /// @brief 轨上收到一次左键 `Press`（`SPEC.FEAT.WS.03` 稿 D7 的按窗上报，与横幅同一条腿）。
        std::function<void()> note_window_press;
    };

    /// @brief 建一扇窗的那一条常驻列（槽位、轨、面板三件在此建齐，缺省是折叠态）。
    /// @param hooks 与装配层的接缝；把 `slot_widget()` 挂进根 Row 是装配层的事。
    explicit ConnectionSidebar(Hooks hooks);
    ConnectionSidebar(const ConnectionSidebar &) = delete;
    auto operator=(const ConnectionSidebar &) -> ConnectionSidebar & = delete;
    ~ConnectionSidebar();

    /// @brief 常驻槽位本体：根 Row 的第一格（34 dp 或 320 dp，随开合在槽内换那一枚子节点）。
    ///
    /// 交出去的是**同一枚**容器节点，整个生命周期不换。槽位本身若随每次切标签重建，就是在掀掉
    /// 常驻列内部正被派发的控件（G34 那一族时序），故装配层只在建窗时把这一格摆进根列一次。
    /// 按值交回（`Column → Widget` 的向上转型是新建一枚 `shared_ptr`，返引用即返临时量的地址，C4172）。
    [[nodiscard]] auto slot_widget() const -> std::shared_ptr<aurora::Widget> { return slot_; }

    /// @brief 展开这一列：装载档案表与最近连接，按当前视图建行区，并把槽内换成 320 dp 面板。
    auto open() -> void;

    /// @brief 折回 34 dp 图标轨：面板内容整棵散掉（下次展开重建），槽里继续留着那条轨。
    auto close() -> void;

    auto toggle() -> void {
        open_ ? close() : open();
    }

    /// @brief 当前是否展开（观测面；折叠＝这一列只画那条轨）。
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 按配置档设定开合（启动解算与广播即时生效两条腿共用）。
    /// @param collapsed true＝折回轨，false＝展开。与 `open()`/`close()` 的差别只有一条：不回写
    ///                  `persist_collapsed`——配置是来向而不是去向，否则广播腿会把自己再喂回去一次。
    auto set_collapsed(bool collapsed) -> void;

    /// @brief 轨上三枚视图直达的落点：切到该视图并展开（人已裁的「三个视图直达」，裁决 7.180）。
    /// @param mode 三档之一；已是该档且已展开时空操作。
    auto expand_to(conn::SidebarViewMode mode) -> void;

    /// @brief 当前视图模式（观测面；切换会重建行区，并同步轨上亮的那一枚）。
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

    /// @brief 换肤之后重建面板内容（控件色值是构建期取定的，与 `SettingsPanel::refresh_chrome()` 同一条腿）。
    ///
    /// 折叠态空操作——轨的色值每次重绘现读 `settings_chrome()`，本来就不需要重建。
    auto refresh_chrome() -> void;

    /// @brief 轨那三枚的当前亮档（观测面转发；与 `view_mode()` 同源，两态必须指向同一个视图档）。
    [[nodiscard]] auto rail_active_view() const noexcept -> conn::SidebarViewMode;

  private:
    /// @brief 按开合档把槽内换成轨或面板（两态互斥占同一格，是这次改判的全部机制）。
    auto mount_slot() -> void;

    /// @brief 展开的实体腿：取档案表与最近连接、建面板、换槽（`open()` 与 `set_collapsed()` 共用）。
    auto apply_open() -> void;

    /// @brief 折叠的实体腿：散掉面板内容、换槽（同上，落盘那一跳留给调用方）。
    auto apply_close() -> void;

    /// @brief 建 320 dp 面板的内容（旧 `build_overlay()` 的展开态本体；改判后就地写进 `panel_`）。
    auto rebuild_panel() -> void;

    [[nodiscard]] auto build_row(const conn::SidebarRow &row) -> aurora::Node;
    auto toggle_favorite(const std::string &id) -> void;
    auto connect_profile(const std::string &id) -> void;
    auto connect_quick(const std::string &text) -> void;
    auto run_import() -> void;
    auto clear_state() -> void;

    Hooks hooks_{};
    std::shared_ptr<aurora::Column> slot_{};    ///< 常驻槽位（根 Row 第一格），生命周期内不换。
    std::shared_ptr<SidebarRail> rail_{};       ///< 折叠态：34 dp 图标列。
    std::shared_ptr<aurora::Column> panel_{};   ///< 展开态：320 dp 那一列，内容随 `rebuild_panel()` 换。
    conn::ProfileStore store_{};
    conn::SidebarViewMode mode_ = conn::SidebarViewMode::All;
    std::string query_{};
    std::vector<std::string> recent_ids_{};
    std::string notice_{};  ///< 导入/落盘留痕（一次一句话，空＝无）。

    std::shared_ptr<aurora::TextInput> search_input_{};
    std::shared_ptr<aurora::TextInput> quick_input_{};
    std::vector<std::shared_ptr<aurora::Button>> star_buttons_{};
    std::vector<std::shared_ptr<aurora::Button>> row_buttons_{};
    bool open_ = false;
};

}  // namespace borealis::ui
