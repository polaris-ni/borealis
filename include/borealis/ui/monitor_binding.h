#pragma once

// ============================================================
// ui/monitor_binding.h — 字节监视卡的「连的是谁」折算（纯逻辑）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 五期（稿 §12 的 D38）：卡从串口一型扩到四型之后，标题那格的身份串不能再
// 只有 `device_path` 一种形态，而四型的显示口径**不许写进 `src/main.cpp`**——那段装配不编入
// runner（裁决 7.83），写进去就等于让「SSH 标签的绑定长什么样」这件事没有自动化证人。
//
// 三条决定形态的口径：
// - **只折 target 半串**，不折整句。「这一格叫什么」来自档案名／OSC 标题／手动重命名的优先级
//   （`appearance.tab_name_priority`），语义与「这一格连的是谁」是两件事，混进一件控件就把改名
//   与换设备画成同一个动作（稿 D38③ 被否的理由）。装配层拿本件的返回值去填既有那枚模板
//   `session.monitor.binding` 的 `{path}`，模板本身不动。
// - **缺省端口省略**，且缺省值取档案结构自己的那枚成员初值而不是自造一枚常量：`conn::SshProfile{}.port`
//   与 `conn::TelnetProfile{}.port` 就是「这条协议的缺省端口」在本仓的唯一真值源，写死 22／23 会
//   在档案缺省值改动那天悄悄变成第二份口径。省略而非显示也与跳链显示同源（`conn::render_proxy_jump_list`
//   那套 `[user@]host[:port]`，稿 F21）。
// - **料由调用方给**：本件不查任何表，也不看会话活没活。四型的料分别挂在 `TabId` 那三张表和
//   按 `PaneId` 存的本地规格上（稿 F19），「取哪一张表的哪一格」是装配层的账，判据才是本件的账。
//
// IPv6 字面量主机的方括号规则与 `conn::proxy_jump.cpp` 的 `bracket_host` 同源（主机串含 `:` 即加括），
// 那里的是链路渲染、这里的是卡片显示，两处同规则而不同层；出现第三处即提到公共面，不留第三份复制。
// ============================================================

#include <optional>
#include <string>

#include "borealis/conn/local_terminal.h"
#include "borealis/conn/profile.h"

namespace borealis::ui {

/// @brief 一次折算的输入料（稿 D38②：`kind` 恰有一块档案有值，取料纪律同 `ui::SnapshotTab`）。
///
/// 缺的那块一律留空而不是造一份默认档案：装配层「表里查不到」的两种原因（这一格散了／这一型本来
/// 不挂标签级档案）都由空 optional 表达，本件据此回空串，卡片于是整格不画而不是画出一个假身份。
struct MonitorBindingSource {
    conn::ConnectionType kind{conn::ConnectionType::Local};  ///< 唯一来源是装配层的 `tab_kind`。
    std::optional<conn::SerialProfile> serial{};   ///< 仅 kind==Serial 有值。
    std::optional<conn::SshProfile> ssh{};         ///< 仅 kind==Ssh 有值。
    std::optional<conn::TelnetProfile> telnet{};   ///< 仅 kind==Telnet 有值。
    std::optional<conn::LocalTerminalSpec> local{};  ///< 仅 kind==Local 有值，且是**焦点那一格**的有效料。

    [[nodiscard]] auto operator==(const MonitorBindingSource &) const noexcept -> bool = default;
};

/// @brief 把四型中某一型的有效料折成卡片 `{path}` 那一格的文本（`SPEC.FEAT.CONN.05` 稿 D38）。
///
/// 四档折法：Serial＝`device_path` 原样；SSH＝`[user@]host[:port]`，缺省端口与空 user 两段都省略；
/// Telnet＝`host[:port]`，缺省端口省略；Local＝`command_line`（有效值，平台默认 shell 已探测过）。
/// **回空串即整格不画**：料缺失、字段空、或 `kind` 与所给的档案块不匹配，一律不给假身份。
///
/// @note Thread: 主线程（面板每帧现读一次，本件无状态）
/// @note Side-effects: none
/// @note Rebuildable: yes
[[nodiscard]] auto monitor_binding_target(const MonitorBindingSource &source) -> std::string;

}  // namespace borealis::ui
