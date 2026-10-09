#pragma once

// ============================================================
// SFTP 浏览器面板（src/ui/sftp_panel.h）
// ------------------------------------------------------------
// `codespec/UI_SFTP.draft.md` §4 的落地（D1–D9 已裁决收口，7.95）：SSH 标签内
// **右侧**停靠双栏面板（本地 + 远程），与左侧连接侧栏同族（CenterRight 对齐，
// 先例 connection_sidebar 的 CenterLeft 镜像）。入口 `sftp.toggle`（装配层注册，
// 同 `connections.toggle` 的 Q4/Q5 形态）。
//
// 线程纪律（D9① / AGENTS §4.5 第 25 条 / SPEC.NF.PERF.06）：所有 SftpClient
// 与 std::filesystem 调用都在**单工作线程**上串行执行（D7①，UI 线程零阻塞
// IO）；worker 结果整表投递进有界收件箱（超限丢最旧保最新），传输进度只保留
// 最新快照（worker 覆写、UI 每帧取走一次——tick 粒度天然满足 ≤100 ms 节流）。
// UI 侧在装配层的帧回调里调 tick() 取件并重建行。
//
// 会话生命周期（D8①）：open() 即拨号（同档案、独立 SSH 会话，凭据经 Hooks
// 解析，本件不碰明文存储）；ssh_tab_alive() 为 false 即自动 close+disconnect。
//
// 本件不认识 libssh：conn::SftpClient 是唯一接缝；排序/路径/权限渲染全部取
// conn/sftp_model 与 ui/sftp_format 的算好的结果。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 include/borealis/。
// ============================================================

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "aurora/widget/dialog.h"
#include "aurora/widget/progress.h"
#include "aurora/widget/stack.h"
#include "aurora/widget/text_input.h"

#include "borealis/conn/profile.h"
#include "conn/sftp_client.h"  // conn::SftpClient（libssh 只在其实现内出现）
#include "conn/sftp_model.h"

namespace aurora {
class OverlayHost;
}

namespace borealis::ui {

class SftpPanel {
public:
    /// @brief 本地栏条目（std::filesystem 列目录的一行，worker 侧排好序整表投递）。
    ///
    /// 与 conn::SftpEntry 平行：本地腿没有 libssh 的 kind 四分，用目录/软链两个
    /// 布尔表达同一条呈现口径（软链灰置不跟随；目录可进入）。
    struct LocalEntry {
        std::string name;           ///< 仅名字，不含路径。
        bool is_directory{false};   ///< 目录（排序置顶、可进入、不可传输）。
        bool is_symlink{false};     ///< 符号链接（灰置，不跟随）。
        std::uint64_t size_bytes{0};
        std::int64_t mtime{0};      ///< 秒级 Unix 时间（ui::format_mtime_local 渲染）。

        [[nodiscard]] auto operator==(const LocalEntry &) const noexcept -> bool = default;
    };

    /// @brief 与装配层的全部接缝（struct-of-回调，同 ConnectionSidebar::Hooks 形态）。
    struct Hooks {
        /// @brief 活动 SSH 标签的档案；nullopt＝当前标签不是 SSH 会话。
        std::function<std::optional<conn::SshProfile>()> current_ssh_profile;
        /// @brief CONN.09 凭据链解析（装配层询问/句柄拆包；本件不经手明文存储）。
        std::function<std::optional<std::string>()> resolve_secret;
        /// @brief 活动标签是否仍是 SSH 会话（UI tick 检查；false 即自动 close+disconnect）。
        std::function<bool()> ssh_tab_alive;
    };

    SftpPanel(class aurora::OverlayHost &host, Hooks hooks);
    SftpPanel(const SftpPanel &) = delete;
    auto operator=(const SftpPanel &) -> SftpPanel & = delete;
    ~SftpPanel();

    auto open() -> void;
    auto close() -> void;
    auto toggle() -> void {
        open_ ? close() : open();
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 每帧取件：收件箱排空 + ssh_tab_alive 检查 + 进度就地刷新。
    ///
    /// 装配层在 app 的帧回调里调用（同 SettingsPanel::pump_preview 的位置）；
    /// 面板关着即空操作。
    auto tick() -> void;

private:
    /// @brief 连接态（NotSsh＝打开时活动标签不是 SSH 会话，D8① 的空态）。
    enum class ConnState : std::uint8_t {
        Connecting,
        Connected,
        Failed,
        NotSsh,
    };

    /// @brief worker 请求（串行队列的一节；字段语义随 kind，见各投递点注释）。
    struct Request {
        enum class Kind : std::uint8_t {
            Connect,
            Disconnect,
            ListRemote,
            ListLocal,
            Mkdir,
            CreateFile,
            RenameRemote,
            RemoveRemote,
            Download,
            Upload,
            Shutdown,
        };
        Kind kind{};
        std::string a;                ///< 路径 / from / 列目录目标（语义随 kind）。
        std::string b;                ///< to / 操作后复列的目录（语义随 kind）。
        std::filesystem::path local;  ///< Download＝本地目标目录；Upload＝本地源文件。
        bool is_directory{false};     ///< RemoveRemote：条目是否目录（remove_directory 腿）。
        conn::SshProfile profile{};   ///< Connect：连接档案。
        std::optional<std::string> secret;  ///< Connect：凭据链解析出的秘密材料。
    };

    /// @brief worker → UI 的收件箱消息（UI tick 取走后按需重建行）。
    struct InboxMessage {
        enum class Kind : std::uint8_t {
            ConnectDone,
            RemoteListing,
            LocalListing,
            OpDone,
            TransferBegin,
            TransferDone,
        };
        Kind kind{};
        bool ok{false};
        bool is_download{false};  ///< TransferBegin：当前传输的方向（标签文案用）。
        conn::SftpError error{conn::SftpError::None};
        std::string path{};  ///< Listing＝目录；TransferBegin＝传输显示名。
        std::vector<conn::SftpEntry> remote_entries{};
        std::vector<LocalEntry> local_entries{};
    };

    // ---- worker 侧（单线程，串行消费）----
    auto run_worker() -> void;
    auto post(Request req) -> void;
    auto push_inbox(InboxMessage msg) -> void;
    auto push_remote_listing(const std::string &dir) -> void;
    auto push_local_listing(const std::filesystem::path &dir) -> void;
    auto op_and_relist(bool ok, const std::string &relist_dir) -> void;
    auto create_remote_file(const std::string &remote_path, const std::string &relist_dir) -> void;
    auto store_progress(const conn::SftpProgress &progress) -> void;

    // ---- UI 侧 ----
    auto connect_now() -> void;
    [[nodiscard]] auto build_root() -> std::shared_ptr<aurora::Stack>;
    [[nodiscard]] auto build_card() -> std::shared_ptr<aurora::Column>;
    [[nodiscard]] auto build_pane(bool remote) -> std::shared_ptr<aurora::Column>;
    [[nodiscard]] auto build_remote_row(const conn::SftpEntry &entry) -> aurora::Node;
    [[nodiscard]] auto build_local_row(const LocalEntry &entry) -> aurora::Node;
    [[nodiscard]] auto build_remote_actions() -> std::shared_ptr<aurora::Row>;
    [[nodiscard]] auto build_transfer_bar() -> std::shared_ptr<aurora::Row>;
    [[nodiscard]] auto status_display_text() const -> std::string;
    auto rebuild_overlay() -> void;
    auto clear_state() -> void;
    auto apply_message(const InboxMessage &msg) -> bool;
    /// @brief 操作期掉线类失败的一回自动重拨（`SPEC.FEAT.CONN.04`，裁决 7.99 D7④）。
    ///
    /// 命中判据是 `conn::sftp_error_drops_link`：列目录 / 增删改 / 传输任一回 Network 或
    /// NotConnected，都说明手上这条会话已经死了——今天不重拨，此后每次操作都对着死会话再发一遍、
    /// 逐条回同一个错，而「重试」按钮永远不出现（设计稿 §1 第 8 条）。本件自己爬一回：投一条
    /// Connect（同档案、`resolve_secret()` 现取），成功即由既有的 ConnectDone 腿复列断线前那个
    /// `remote_path_`。一回掉线只重拨一回（旗标随连接成功复位），重拨再失败落既有 Failed 与
    /// 那枚「重试」——这里没有持续读循环，退避环无对象可退。
    /// @param error 那一手操作返回的错误（判据 `conn::sftp_error_drops_link` 吃它）。
    /// @return 是否已投出那一回 Connect（调用方据此不再重复重建浮层）。
    auto redial_once(conn::SftpError error) -> bool;
    auto refresh_progress_in_place() -> void;

    auto on_remote_clicked(const std::string &name, conn::SftpEntryKind kind) -> void;
    auto on_local_clicked(const std::string &name, bool is_directory) -> void;
    auto up_one(bool remote) -> void;
    auto refresh_pane(bool remote) -> void;
    auto start_transfer(bool download) -> void;
    auto request_create(bool is_folder) -> void;
    auto request_rename() -> void;
    auto request_delete() -> void;
    auto show_name_prompt(const std::string &title_key, const std::string &initial,
                          const std::function<void(const std::string &)> &on_confirm) -> void;
    auto show_delete_confirm(const std::string &name, bool is_directory) -> void;
    auto close_dialog() -> void;
    [[nodiscard]] auto find_remote(const std::string &name) const -> const conn::SftpEntry *;
    [[nodiscard]] auto find_local(const std::string &name) const -> const LocalEntry *;

    class aurora::OverlayHost &host_;
    Hooks hooks_{};

    // ---- worker 线程与请求队列（D7①：同时至多 1 个操作在执行）----
    std::thread worker_;
    std::mutex requests_mutex_{};
    std::condition_variable requests_cv_{};
    std::deque<Request> requests_{};
    bool worker_stop_{false};  // requests_mutex_ 守护；析构置位后 join。

    // ---- 收件箱（有界，超限丢最旧保最新）+ 进度快照（只留最新一份）----
    std::mutex inbox_mutex_{};
    std::deque<InboxMessage> inbox_{};
    conn::SftpProgress progress_{};
    bool progress_active_{false};

    conn::SftpClient client_{};       // 只在 worker 线程触碰。
    std::atomic<bool> cancel_flag_{false};  // 传输取消旗标（传输腿按引用轮询）。
    int temp_seq_{0};                 // 新建空文件的临时件序号（仅 worker 触碰）。

    // ---- UI 态（仅 UI 线程读写）----
    ConnState state_ = ConnState::Connecting;
    conn::SftpError last_error_ = conn::SftpError::None;
    std::string remote_path_ = "/";
    std::string local_path_{};  // 空＝首列由 worker 现解析 current_path（IO 不进 UI 线程）。
    std::vector<conn::SftpEntry> remote_entries_{};
    std::vector<LocalEntry> local_entries_{};
    std::string selected_remote_{};
    std::string selected_local_{};
    std::string transfer_label_{};    // 当前传输显示名（TransferBegin 给出）。
    bool transfer_is_download_{false};
    int transfers_posted_{0};         // 本次打开累计投递的传输数（排队计数＝posted−done）。
    int transfers_done_{0};
    /// @brief 掉线自动重拨的一回旗标（裁决 7.99 D7④）：连接成功与重新打开各复位一次。
    bool redial_armed_{true};

    // 双击模拟：框架按钮只有单击回调，500 ms 内同条目二击按 D3① 的「双击进入」。
    std::string last_click_pane_{};
    std::string last_click_name_{};
    std::chrono::steady_clock::time_point last_click_time_{};

    // ---- 浮层与控件柄（rebuild 时重建并重取）----
    std::optional<std::size_t> overlay_index_{};
    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> dialog_index_{};
    std::shared_ptr<aurora::TextInput> prompt_input_{};
    std::string prompt_text_{};  // 输入框现值镜像（TextInput 无公开取值腿，on_changed 同步）。
    std::shared_ptr<aurora::Text> transfer_text_{};
    std::shared_ptr<aurora::ProgressIndicator> progress_bar_{};
    bool open_ = false;
};

}  // namespace borealis::ui
