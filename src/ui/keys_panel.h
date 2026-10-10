#pragma once

// ============================================================
// SSH 密钥管理面板（src/ui/keys_panel.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.10` 批 3 的界面腿（稿 §2 判据 1–7，裁决 7.106 D1–D12）：
// 左侧停靠卡片（520 dp，`keys.open` 命令唤起，D1①）+ 生成对话框（D4①/D5①）
// + 推送对话框（D7①/D8① 的步骤阶梯）+ 删除两段式确认（D9②/D12）+ 目录腿
// （D2③/D11②：面板只经 Hook 交回装配层，自己不认识 `au::file_dialog`）。
//
// **本件不认识 libssh，也不认识文件系统**（7.97 D5① 同一条纪律）：扫盘、生成、导出、
// 推送、删除五件事全部经 Hooks 交回装配层的 worker；界面侧只消费 `KeysSnapshot`
// 这一个 latest-value（每帧取一份，行表有变才重建，D6①）。因此本件 include 的面是
// `conn/key_model.h`（纯逻辑、标准类型）与 `ui/keys_format.h`，**不是** `key_store.h`
// /`key_push.h`——摸到传输腿签名就等于给「动作在 UI 线程直跑」开了一道门。
//
// 措辞一律经 `settings_label(key)`，key 由 `keys_format` 那批映射函数给出（裁决 7.25⑬）；
// 「Hooks 缺席的入口整枚不画」同侧栏与隧道面板（7.38⑥ F-b）。
//
// 面板关闭不等 worker（稿 §5）：关面板只是收浮层，在途任务照跑完并写快照，回来
// 看见的就是终态。析构时若确认框／对话框在场，先收它们再收卡片，与隧道面板同款。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 include/borealis/。
// ============================================================

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/widget/button.h"
#include "aurora/widget/dialog.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text_input.h"

#include "borealis/conn/profile.h"
#include "conn/key_model.h"
#include "ui/keys_format.h"

namespace borealis::ui {

class KeysPanel {
  public:
    /// @brief 生成对话框的草稿（面板侧类型，**不是** `conn::GenerateRequest`）。
    ///
    /// 刻意不直接复用传输腿那个结构体：那会要本件 include `key_store.h`，而装配层才是
    /// 把「位数下拉的第几档」折成 `rsa_bits`、把「两栏口令」折成 `optional<string>` 的地方。
    /// 口令在这里就是明文串，只活在对话框控件与本草稿的内存里，随提交进装配层即不再被本件持有
    /// （CONN.09 的明文纪律；本件不落盘、不进日志、不留副本）。
    struct GenerateDraft {
        bool rsa{};                ///< 卡片选择：false＝ed25519（缺省，需求原文「优先」那档）。
        int rsa_bits{3072};        ///< 仅 `rsa` 用；下拉三档之一（2048/3072/4096）。
        std::string directory;     ///< 目标目录，缺省＝目录表首条（`~/.ssh`，D2③/D10①）。
        std::string basename;      ///< 私钥文件名（不含 `.pub`）。
        std::string comment;       ///< `.pub` 第三字段，可空。
        std::string passphrase;    ///< 口令栏一。
        std::string passphrase_again;  ///< 口令栏二（判据 5 的双栏；两栏不一致即红提示不提交）。
    };

    /// @brief 与装配层的全部接缝（struct-of-回调，同 `TunnelPanel::Hooks` 形态）。
    struct Hooks {
        /// @brief 取最新值快照（行表 + 目录表 + 在途任务 + 留痕；worker 覆写、本件逐帧读）。
        std::function<KeysSnapshot()> snapshot;
        /// @brief 请装配层扫一次盘（`open()` 时呼一次；之后的重扫由动作的尾巴带着，
        ///        所以本件不需要「重扫」按钮——稿 §5「启动不扫盘」的那半）。
        std::function<void()> request_scan;
        /// @brief 平台目录选择器（装配层调 `au::file_dialog::open_folder()`，D11②）。
        ///        空串＝取消**或**平台起不来，两档同途（`settings_panel` 的导出腿口径）。
        std::function<std::string()> pick_key_dir;
        /// @brief 把选来的目录交回装配层：规范化＋落第七域＋重扫（空串整条不跑，判据 §6）。
        std::function<void(const std::string &)> add_dir;
        /// @brief 档案表（推送对话框的目标下拉；装配层交全表，本件只挑 SSH 子集）。
        std::function<std::vector<conn::Profile>()> profiles;
        /// @brief 交出公钥单行进剪贴板（判据 4；装配层落 `session::ClipboardOutbox::write`）。
        std::function<void(const std::string &)> copy_line;
        /// @brief 提交生成请求（判据 5；本件已把名称/注释/同名三闸判过，见 `save_generate`）。
        std::function<void(const GenerateDraft &)> generate;
        /// @brief 导出某行的公钥（判据 3 的「公钥缺失」出口；口令要不要问由装配层按行的
        ///        `encrypted` 决定，本件只交路径）。
        std::function<void(const std::string &)> export_public;
        /// @brief 把某行的公钥推到某档案的主机（判据 6）。
        std::function<void(const std::string &path, const std::string &profile_id)> push;
        /// @brief 删除某行那把钥匙（判据 7 的第二段；一次清一对在传输腿里，本件不知其细节）。
        std::function<void(const std::string &)> delete_key;
    };

    KeysPanel(class aurora::OverlayHost &host, Hooks hooks);
    KeysPanel(const KeysPanel &) = delete;
    auto operator=(const KeysPanel &) -> KeysPanel & = delete;
    ~KeysPanel();

    auto open() -> void;
    auto close() -> void;
    auto toggle() -> void {
        open_ ? close() : open();
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 帧驱动（装配层 on_frame 调用，D6①）：读快照、行表有变才重建。
    ///        未打开、或对话框/确认框在场时只记账不重建浮层（避免打断输入与选点）。
    auto tick() -> void;

    /// @brief 当前画出的行（观测面：判据比这份而不读浮层树）。
    [[nodiscard]] auto visible_rows() const -> std::vector<KeyRow>;

    /// @brief 卡片顶行当前那句话（已解析成显示串；空＝无）。观测面。
    [[nodiscard]] auto notice() const -> std::string {
        return notice_text_;
    }

    /// @brief 生成对话框是否在场 / 其当前草稿（观测面）。
    [[nodiscard]] auto generate_open() const noexcept -> bool {
        return generate_open_;
    }

    [[nodiscard]] auto generate_draft() const -> const GenerateDraft & {
        return draft_;
    }

    /// @brief 推送对话框是否在场 / 其目标行与当前格（观测面；格序来自快照）。
    [[nodiscard]] auto push_open() const noexcept -> bool {
        return push_open_;
    }

    [[nodiscard]] auto push_path() const -> const std::string & {
        return push_path_;
    }

    [[nodiscard]] auto push_profile_id() const -> const std::string & {
        return push_profile_id_;
    }

    /// @brief 删除确认是否在场 / 其目标行路径（观测面；两段式的第一段就是「在场」这件事）。
    [[nodiscard]] auto delete_confirm_open() const noexcept -> bool {
        return delete_confirm_open_;
    }

    [[nodiscard]] auto pending_delete_path() const -> const std::string & {
        return pending_delete_path_;
    }

  private:
    /// @brief 装（或重装）主卡片浮层：先摘旧的一层再挂新的，蒙版与卡片一起造。
    auto install_overlay() -> void;
    auto refresh_overlay() -> void;
    [[nodiscard]] auto build_overlay() -> std::shared_ptr<aurora::Column>;
    [[nodiscard]] auto build_row(const KeyRow &row) -> aurora::Node;

    [[nodiscard]] auto default_directory() const -> std::string;
    auto on_add_dir() -> void;
    auto on_copy(const KeyRow &row) -> void;
    auto on_export(const std::string &path) -> void;
    auto on_delete(const std::string &path) -> void;

    auto open_generate() -> void;
    auto rebuild_generate() -> void;
    auto close_generate() -> void;
    [[nodiscard]] auto build_generate() -> std::shared_ptr<aurora::Column>;
    auto collect_draft() -> void;
    auto save_generate() -> void;

    auto open_push(std::string path) -> void;
    auto rebuild_push() -> void;
    auto close_push() -> void;
    [[nodiscard]] auto build_push() -> std::shared_ptr<aurora::Column>;

    auto open_delete_confirm(std::string path) -> void;
    auto close_delete_confirm() -> void;

    auto clear_state() -> void;

    /// @brief 把快照折成行表并记下（tick 与几条动作尾巴共用）。
    auto apply_snapshot(const KeysSnapshot &snapshot) -> void;

    class aurora::OverlayHost &host_;
    Hooks hooks_{};
    KeysSnapshot snapshot_{};
    std::vector<KeyRow> painted_rows_{};  ///< 上一帧实际画出的行（diff 基准）。
    std::string notice_text_{};           ///< 顶行那句话的显示串（`key_headline_key` 解析结果）。
    std::string painted_notice_key_{};    ///< 主卡上次画下的留痕 key/arg（顶行重建判据：
    std::string painted_notice_arg_{};    ///<  动作不改行表只改留痕，行 diff 看不见它）。
    bool open_ = false;

    // ---- 主卡片 ----
    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};

    // ---- 生成对话框（判据 5）----
    std::shared_ptr<aurora::Dialog> generate_dialog_{};
    std::optional<std::size_t> generate_overlay_{};
    bool generate_open_ = false;
    bool awaiting_generate_ = false;  ///< 已提交、等快照离开 Generate 那一档时由 tick 收框。
    GenerateDraft draft_{};
    std::size_t type_index_ = 0;  ///< 0＝ed25519 卡、1＝RSA 卡（卡片定字段集，同隧道三式）。
    std::size_t bits_index_ = 1;  ///< 位数下拉当前下标（缺省 3072，判据 5）。
    std::shared_ptr<aurora::TextInput> name_input_{};
    std::shared_ptr<aurora::TextInput> comment_input_{};
    std::shared_ptr<aurora::TextInput> dir_input_{};
    std::shared_ptr<aurora::TextInput> passphrase_input_{};
    std::shared_ptr<aurora::TextInput> passphrase_again_input_{};
    std::shared_ptr<aurora::Text> generate_notice_{};
    std::shared_ptr<aurora::Button> generate_submit_{};  ///< 提交后置灰（判据 5）。

    // ---- 推送对话框（判据 6）----
    std::shared_ptr<aurora::Dialog> push_dialog_{};
    std::optional<std::size_t> push_overlay_{};
    bool push_open_ = false;
    std::string push_path_{};
    std::string push_profile_id_{};
    std::size_t push_profile_index_ = 0;
    std::vector<conn::Profile> push_profiles_{};  ///< 每次重建现取的 SSH 子集。
    std::shared_ptr<aurora::Text> push_notice_{};
    KeysSnapshot painted_push_state_{};  ///< 框上次画下时的快照投影：阶梯格与留痕变了
                                         ///<  就地重建（框里没有输入草稿，重建不打断什么）。

    // ---- 删除确认（判据 7 / D12⑴ 两段式）----
    std::shared_ptr<aurora::Dialog> confirm_dialog_{};
    std::optional<std::size_t> confirm_overlay_{};
    bool delete_confirm_open_ = false;
    std::string pending_delete_path_{};
};

}  // namespace borealis::ui
