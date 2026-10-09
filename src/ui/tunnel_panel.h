#pragma once

// ============================================================
// SSH 隧道管理面板（src/ui/tunnel_panel.h）
// ------------------------------------------------------------
// `codespec/UI_TUNNEL.draft.md` 的落地（D1–D9 已裁决收口，裁决 7.97）：
// 独立左侧停靠卡片（520 dp，`tunnels.open` 命令唤起，D1①），两行堆叠行
// （D2①：首行 名称+形态徽标+状态徽标+启停，次行 监听点→目标+承载档案），
// 新建/编辑独立对话框（D4①，connection_wizard 同族：类型卡定字段集 +
// 自动启动开关 + 重试三字段），删除两段式（运行中先呼确认，判据 6）。
//
// 生命周期口径（D5①）：本件不认识 libssh、不认识 conn::Tunnel——隧道对象
// 与运行态快照都在装配层，本件经 Hooks 进出（启停交回装配层，快照逐帧自取）。
// **关闭面板不停隧道**；`stop()` 的 join 上界在装配层回调里消化（稿 §5 首版
// 取舍，与 SftpPanel 断连同档）。
//
// 状态泵（D6①）：`tick()` 由装配层在既有 `on_frame` 里调用，读 snapshot Hook
// 合成行表；行表与上一帧逐字段相等就不重建（状态是可覆盖的量，只留最新值）。
// 文案一律经 settings_label(key)，本件不 switch 出第二套措辞（tunnel_format
// 是唯一枚举→词条 key 的地方）。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 include/borealis/。
// ============================================================

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/widget/dialog.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text_input.h"

#include "borealis/conn/profile.h"
#include "borealis/conn/tunnel.h"
#include "ui/tunnel_format.h"

namespace borealis::ui {

class TunnelPanel {
  public:
    /// @brief 与装配层的全部接缝（struct-of-回调，同 ConnectionSidebar::Hooks 形态）。
    struct Hooks {
        /// @brief 取隧道定义整表（open 时现取；装配层从 config 第六域搬值）。
        std::function<std::vector<conn::TunnelSpec>()> load;
        /// @brief 定义整表写回存储（增/删/改都走整表，同侧栏 persist 口径）。
        std::function<bool(const std::vector<conn::TunnelSpec> &)> persist;
        /// @brief 承载档案表（编辑对话框的「承载档案」下拉行源；装配层从
        ///        config 第五域搬 SSH 档案子集）。
        std::function<std::vector<conn::Profile>()> profiles;
        /// @brief 启动一条隧道（装配层解析凭据、构造 conn::Tunnel 并拉起工作线程，
        ///        D7；面板不等待、不同步取运行态——下一帧快照自然反映）。
        std::function<void(const conn::TunnelSpec &)> start;
        /// @brief 停止一条隧道（按 id；装配层 `Tunnel::stop()` + join）。
        std::function<void(const std::string &)> stop;
        /// @brief 每隧道运行态快照（id → 最新值；无活动的 id 缺席＝Stopped 行）。
        std::function<std::map<std::string, TunnelRuntime>()> snapshot;
    };

    TunnelPanel(class aurora::OverlayHost &host, Hooks hooks);
    TunnelPanel(const TunnelPanel &) = delete;
    auto operator=(const TunnelPanel &) -> TunnelPanel & = delete;
    ~TunnelPanel();

    auto open() -> void;
    auto close() -> void;
    auto toggle() -> void {
        open_ ? close() : open();
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 帧驱动（装配层 on_frame 调用，D6①）：读快照、行表有变才重建。
    ///        未打开或编辑器/确认框在场时只记账不重建浮层（避免打断输入）。
    auto tick() -> void;

    /// @brief 当前画出的行（观测面：定义序 + 快照合成，判据比这份而不读浮层树）。
    [[nodiscard]] auto visible_rows() const -> std::vector<TunnelRow>;

    /// @brief 留痕行当前内容（导入/落盘失败一类，一次一句；空＝无）。
    [[nodiscard]] auto notice() const -> const std::string & {
        return notice_;
    }

    /// @brief 编辑对话框是否在场（观测面）。
    [[nodiscard]] auto editor_open() const noexcept -> bool {
        return editor_open_;
    }

    /// @brief 编辑对话框当前草稿（观测面：判据比这份，不读控件树）。
    [[nodiscard]] auto editor_draft() const -> const conn::TunnelSpec & {
        return draft_;
    }

    /// @brief 删除确认是否在场（观测面；运行中删除的两段式第一段）。
    [[nodiscard]] auto delete_confirm_open() const noexcept -> bool {
        return delete_confirm_open_;
    }

  private:
    auto refresh_overlay() -> void;
    [[nodiscard]] auto build_overlay() -> std::shared_ptr<aurora::Column>;
    [[nodiscard]] auto build_row(const TunnelRow &row) -> aurora::Node;

    auto on_start_stop(const std::string &id) -> void;
    auto on_start(const conn::TunnelSpec &spec) -> void;
    auto on_stop(const std::string &id) -> void;
    auto on_delete(const std::string &id) -> void;
    auto remove_spec(const std::string &id) -> void;

    auto open_editor(std::optional<std::string> id) -> void;
    auto rebuild_editor() -> void;
    auto close_editor() -> void;
    [[nodiscard]] auto build_editor() -> std::shared_ptr<aurora::Column>;
    auto collect_draft() -> void;
    auto save_draft() -> void;

    auto open_delete_confirm(std::string id) -> void;
    auto close_delete_confirm() -> void;

    auto clear_state() -> void;

    class aurora::OverlayHost &host_;
    Hooks hooks_{};
    std::vector<conn::TunnelSpec> specs_{};
    std::map<std::string, TunnelRuntime> runtimes_{};
    std::vector<TunnelRow> painted_rows_{};  ///< 上一帧实际画出的行（diff 基准）。
    std::string notice_{};

    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};
    bool open_ = false;

    // ---- 编辑对话框（D4①：本件自建浮层，同 connection_wizard 形态）----
    std::shared_ptr<aurora::Dialog> editor_dialog_{};
    std::optional<std::size_t> editor_overlay_{};
    bool editor_open_ = false;
    conn::TunnelSpec draft_{};
    std::string editing_id_{};  ///< 空＝新建（id 落盘时按 "tunnel:"+name 生成）。
    std::size_t kind_index_ = 0;
    std::size_t profile_index_ = 0;  ///< 承载档案下拉的当前下标（行源现取后回填）。
    std::vector<conn::Profile> profiles_cache_{};  ///< 每次重建对话框现取（SSH 子集）。
    std::shared_ptr<aurora::TextInput> name_input_{};
    std::shared_ptr<aurora::TextInput> listen_addr_input_{};
    std::shared_ptr<aurora::TextInput> listen_port_input_{};
    std::shared_ptr<aurora::TextInput> target_host_input_{};
    std::shared_ptr<aurora::TextInput> target_port_input_{};
    std::shared_ptr<aurora::TextInput> retry_base_input_{};
    std::shared_ptr<aurora::TextInput> retry_cap_input_{};
    std::shared_ptr<aurora::TextInput> retry_attempts_input_{};
    std::shared_ptr<aurora::Text> editor_notice_{};

    // ---- 删除确认（判据 6：运行中隧道的第一段）----
    std::shared_ptr<aurora::Dialog> confirm_dialog_{};
    std::optional<std::size_t> confirm_overlay_{};
    bool delete_confirm_open_ = false;
    std::string pending_delete_id_{};
};

}  // namespace borealis::ui
