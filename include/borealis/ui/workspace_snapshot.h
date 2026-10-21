#pragma once

// ============================================================
// 会话恢复的快照模型与编解码（include/borealis/ui/workspace_snapshot.h）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.11` 的数据载体（稿 `codespec/WS11_SESSION_RESTORE.draft.md` §4 第 2 件）：
// 一份额描述「上次那套标签布局」的值语义模型，与它的 JSON 文本编解码。落盘归
// `src/config/workspace_store.{h,cpp}` 那枚 `Preferences` 接缝，本件不碰文件系统，于是判据 ②
// 的往返等值与判据 ⑥⑦ 的两道闸都能无头单测（AGENTS.md §4.4 第 19 条）。
//
// 四条决定形态的口径，细则见稿 §3 与 §0：
// - **不含凭据**：SSH 腿只搬 `conn::SecretHandle` 那四项（`is_plaintext()` 恒 false 是它的对外
//   承诺），恢复时重新走 `connect_profile` → `ask_secret` 问一次（稿 F9）。明文凭据进不了这份
//   文档由判据 ⑥ 守着——那是本棒对 AGENTS.md §4.5 第 24 条负的那一格责任。
// - **档案块与 `config::Store` 的落盘形态逐字相同**（同键名、同次序、同枚举文本）：`store.cpp`
//   的 `*_to_json` 全在匿名命名空间，把它们搬出成可共用的形态会同时动写盘与导出件两条既有判据面
//   （稿 F12），故本件自写一份，代价是「同一份 `SshProfile` 有两处 JSON 形态」，由判据 ⑦ 的等式案
//   钉住不分叉。串口那三张取值表仍引用 `conn/serial_model.h` 里那一份（裁决 7.110 D4① 的「只有一
//   处定义」），本件不复制档位表。
// - **读侧 fail-closed**：坏文档整份拒绝并给一句拒因，与装载 `borealis.json` 那套「逐键回落 +
//   留痕」的宽容刻意相反（同 `Store::import_settings` 与 `cli::handoff` 的外部文档纪律）。宽容是
//   给自家损坏文件的，给一份手改过的布局就是把「布局没了」读成「上次只开过一格」。写侧因此
//   **全字段无条件写**，读侧可以要求每个键都在。本件只管**文档**合法性：一棵形状自身合不合法
//   （单子层、长度域外、序位不是排列）归 `PaneTree::restore` 的六道自检，两道闸叠加才是判据 ②
//   的完整形态（稿 F25）。
// - **身份记序位不记句柄**：`PaneId` 与 `ContainerId` 都是本进程里发的号，跨进程存它必然漂（稿
//   F23），故分屏结构存 `ui::PaneShape` 的阅读序序位、选中格存 `focused_order`，重放时由装配层
//   重新发号并重新取一次 `container_of`。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/conn/local_terminal.h"
#include "borealis/conn/profile.h"
#include "borealis/ui/pane_tree.h"

namespace borealis::ui {

/// @brief 本件写出的 `schema_version`，也是读侧接受的最高版本（高于它整份拒绝）。
///
/// 与 `borealis.json` 的 `config::kSupportedSchemaVersion` 刻意各走各的：那份是设置域、跟着导出件
/// 跨机（裁决 7.87④ 的同源设计），这份只描述本机布局（稿 D1①）。两者绑成一枚就会在「设置域加
/// 一列」的那天把用户上一次的好快照一起判成版本过高。
inline constexpr std::int64_t kWorkspaceSnapshotSchemaVersion = 1;

/// @brief 读侧的窗数上界（稿 D9①：写侧不设限，读侧任一计数超界即整份拒绝）。
///
/// 界取「远大于人手会开的窗数、远小于卡死启动的规模」这一档：一份手改过的文件若声称有一百万扇
/// 窗，恢复腿就会在启动时建一百万个视口，而启动路径上没有第二道闸。超界的文件等于废掉直到用户
/// 自己清——这条与 `connection.trusted_url_hosts` 那条「要清只能改 JSON」的在册代价同类。
inline constexpr std::size_t kMaxSnapshotWindows = 32;
/// @brief 读侧的每窗标签上界。
inline constexpr std::size_t kMaxSnapshotTabsPerWindow = 128;
/// @brief 读侧的每标签 pane 上界（同时是形状叶子数的上界）。
inline constexpr std::size_t kMaxSnapshotPanesPerTab = 128;
/// @brief 读侧的形状递归深度上界。
///
/// 树本身不设深度上限（`PaneTree` 的不变量只到最小 pane 尺寸），但读侧面对的是不可信文档：
/// 没有这一界，一串「容器套容器」的嵌套能把启动路径写成栈溢出。
inline constexpr std::size_t kMaxSnapshotShapeDepth = 64;
/// @brief 读侧的一棵形状最多访问的节点数。
///
/// 合法形状是满二叉式的：L 枚叶子 ⟹ 2L-1 个节点，故「两倍格上界」对自家写出的文档恒宽松，
/// 而「子表里全塞空容器」那种把叶子数藏住的形状同样在这一界外被拒。
inline constexpr std::size_t kMaxSnapshotShapeNodes = 2 * kMaxSnapshotPanesPerTab;

struct SnapshotDecodeResult;

/// @brief 一个标签的恢复料：需求括号里「连接类型 / profile / 各标签标题」三项在本仓的落点。
///
/// 三块档案按 `kind` 恰有一块有值（`Local` 那一档没有标签级档案块——本地腿的料是 per-pane 的，
/// 见 `panes`）。这不是省字节的取舍而是 F8 的读数：SSH／串口／Telnet 的规格本来就挂在 `TabId`
/// 那三张表上，本地腿的目录与命令行则每格可以不同（分屏继承 OSC 7）。
struct SnapshotTab {
    conn::ConnectionType kind{conn::ConnectionType::Local};  ///< 唯一来源是装配层的 `tab_kind`（WS.04）。
    std::string profile_id;               ///< 档案 id；quick connect 那份不落表的临时 id 照存（稿 F10）。
    std::string title;                    ///< **手动重命名**那一项，空＝没有；OSC 标题属会话内容，不恢复（稿 D10①）。
    std::optional<conn::SshProfile> ssh;  ///< 仅 kind==Ssh 有值；内含 `credential` 句柄而非明文（稿 F11）。
    std::optional<conn::SerialProfile> serial;  ///< 仅 kind==Serial 有值；串口无凭据。
    std::optional<conn::TelnetProfile> telnet;  ///< 仅 kind==Telnet 有值。
    /// @brief 仅 kind==Local 有内容：与 `shape` 的叶子**按序位同长**的一份有效料。
    ///
    /// 存的是**有效值**而不是引用（同 WS.10 那枚 `ClosedTabSpec` 的字段口径，稿 F14）：空命令行＝
    /// 平台默认 shell 探测、空目录＝继承，恢复侧照旧走「CLI > 档案 > 继承 > 配置」那条算式的后两档。
    std::vector<conn::LocalTerminalSpec> panes;
    PaneShape shape{};            ///< 分屏结构：稿 D5① 的树自描述件，比例与拓扑一并带上。
    std::size_t focused_order = 0;  ///< 选中格的阅读序序位（不是 `PaneId`，理由见文件头第四条）。

    /// @brief 逐字段全等比较（快照往返判据用）。
    [[nodiscard]] auto operator==(const SnapshotTab &) const noexcept -> bool = default;
};

/// @brief 一扇窗的恢复料。
///
/// 窗身份不入库：窗序即创建序，与 `windows.json` 那枚 `window_persist_id(creation_index)` 键按同
/// 一条算式对齐（稿 D6①、裁决 7.126）。位置与尺寸由框架那条几何链恢复，本件一个字都不写。
struct SnapshotWindow {
    std::vector<SnapshotTab> tabs;  ///< 标签条内序；至少一个标签——空窗的 `selected_tab` 无处可取，读侧拒。
    std::size_t selected_tab = 0;   ///< 选中标签在 `tabs` 里的下标。

    /// @brief 逐字段全等比较。
    [[nodiscard]] auto operator==(const SnapshotWindow &) const noexcept -> bool = default;
};

/// @brief 上次那套标签布局：本棒的全部产出就是这一份文档。
struct WorkspaceSnapshot {
    std::vector<SnapshotWindow> windows;  ///< 按创建序；空表＝「没有可恢复的布局」，读侧照样接受。

    /// @brief 逐字段全等比较（判据 ② 的「模型 → JSON → 模型 全等」就是这一句）。
    [[nodiscard]] auto operator==(const WorkspaceSnapshot &) const noexcept -> bool = default;

    /// @brief 序列化为 JSON 文本：全字段无条件写，读侧因此可以要求每个键都在。
    /// @return 文本；空值只在该文档里的相对长度含 NaN／Inf 时出现（框架拒写）。树经 `describe()`
    ///         产出的形状不会有这一档，手工拼出来的模型才有——故调用方须把空值当失败而非当空串。
    [[nodiscard]] auto to_json() const -> std::optional<std::string>;

    /// @brief 解析一份快照文档：任一判据不合即整份拒绝，不回落半个（稿 D9①／判据 ②）。
    /// @param text JSON 全文。
    /// @return 快照与拒因二者恰有其一。
    [[nodiscard]] static auto from_json(std::string_view text) -> SnapshotDecodeResult;
};

/// @brief 一次解码的结论：`snapshot` 与 `reason` 恰有一个有值。
///
/// 拒因是 ASCII 英文诊断，只进日志、不上界面（稿 D8① 与 F17：布局文件坏了不弹框，
/// 而 `ui::StartupNotice` 的判据本体就是 `config::LoadReport` 那一枚类型）。
struct SnapshotDecodeResult {
    std::optional<WorkspaceSnapshot> snapshot;  ///< 成功时才有值。
    std::string reason;                         ///< 失败时才有值：一句拒因，供调用方落日志。
};

}  // namespace borealis::ui
