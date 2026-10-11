#pragma once

// ============================================================
// 连接新建/编辑向导（src/ui/connection_wizard.h）
// ------------------------------------------------------------
// 设计稿 §2 标号 5 的分步表单：步骤 0 选类型卡（Local / SSH / Serial / Telnet），步骤 1 填字段集
// （类型卡决定字段集；串口九行见 UI_SERIAL.draft.md §2 标号 2，Telnet 三行见
// UI_TELNET.draft.md §2 与裁决 7.112 D2①）。校验走纯逻辑
// `validate_draft`（conn/profile_views.h），本件只把问题码折成可读文案。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 include/borealis/。
// ============================================================

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/widget/dialog.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text_input.h"

#include "borealis/conn/profile.h"
#include "conn/profile_views.h"   // ProfileDraft / DraftIssue
#include "conn/serial_model.h"    // SerialPortInfo（设备下拉的行与可达性）

namespace borealis::ui {

class ConnectionWizard {
public:
    /// @brief 保存/取消接缝。on_save 收完整档案（校验已过、id 已定）。
    ///
    /// 串口三条钩子（裁决 7.110 D2①/D4①/D8①）：设备枚举只经装配层的 latest-value 快照
    /// 读（`current_ports`），本件不碰目录 IO；「刷新」把取数交回装配层的 worker，结果
    /// 落地由装配层呼 `reload_ports()` 通知；新建串口档案的字段初值由 `serial_defaults`
    /// 在建草稿那一刻从设置域灌进来（档位表仍只有 `conn::serial_model` 那一份）。
    struct Hooks {
        std::function<void(const conn::Profile &)> on_save;
        std::function<void()> on_cancel;
        std::function<std::vector<conn::SerialPortInfo>()> current_ports;
        std::function<void()> refresh_ports;
        std::function<conn::SerialProfile()> serial_defaults;
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

    /// @brief 设备枚举结果落地通知（装配层在主线程呼）：向导正开着串口字段集时重建下拉。
    ///
    /// 未开或不在串口步骤时为空操作——枚举是后台腿，什么时候回来不由 UI 决定。
    auto reload_ports() -> void;

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
    /// @brief 向装配层发起一次设备枚举（D8①：只在「开串口字段集」与「点刷新」两处呼）。
    ///
    /// 就地重建（切档、枚举回投）都不呼本件——重建只读 `current_ports` 的那份快照。
    auto request_port_scan() -> void;

    class aurora::OverlayHost &host_;
    Hooks hooks_{};
    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};

    conn::ProfileDraft draft_{};
    std::string editing_id_{};  ///< 空＝新建。
    int step_ = 0;

    // 设备下拉的数据：装配层快照的一份副本（UI 线程零目录 IO，AGENTS §4.5 第 25 条）。
    std::vector<conn::SerialPortInfo> ports_{};
    bool custom_device_ = false;   ///< true＝设备行是手输框（D2① 的「自定义…」档）。
    bool vetoing_port_choice_ = false;  ///< 置灰行被点中时的回退防抖（on_change 会再进一次）。
    int last_selectable_port_ = -1;     ///< 回退目标：上一次真正可选的行号。

    // 字段控件（每步重建时新建；句柄只在两次重建之间存活）。
    std::shared_ptr<aurora::TextInput> name_input_{};
    std::shared_ptr<aurora::TextInput> host_input_{};
    std::shared_ptr<aurora::TextInput> port_input_{};
    std::shared_ptr<aurora::TextInput> user_input_{};
    std::shared_ptr<aurora::TextInput> identity_input_{};
    std::shared_ptr<aurora::TextInput> command_input_{};
    std::shared_ptr<aurora::TextInput> workdir_input_{};
    std::shared_ptr<aurora::TextInput> device_input_{};      ///< 「自定义…」档的手输框。
    std::shared_ptr<aurora::Dropdown> device_dropdown_{};    ///< 设备下拉（枚举快照）。
    std::shared_ptr<aurora::Button> auth_button_{};   // 认证方式循环按钮（四档，避开行内下拉）。
    std::shared_ptr<aurora::Button> baud_button_{};
    std::shared_ptr<aurora::Button> data_bits_button_{};
    std::shared_ptr<aurora::Button> stop_bits_button_{};
    std::shared_ptr<aurora::Button> parity_button_{};
    std::shared_ptr<aurora::Button> flow_button_{};
    std::shared_ptr<aurora::Button> line_ending_button_{};
    std::shared_ptr<aurora::Button> encoding_button_{};
    std::shared_ptr<aurora::Text> notice_{};          // 校验失败的可读文案。
    bool open_ = false;
};

}  // namespace borealis::ui
