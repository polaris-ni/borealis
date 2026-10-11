#pragma once

// ============================================================
// 串口向导与热改入口的纯格式化函数（src/ui/serial_format.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.05 的「最后一步」格式化：三个文本参数名（校验位 / 流控 / 发送行尾）→
// 词条 key、设备可达性 → 归因词条 key、设备下拉一行的拼接。全部只依赖标准类型与
// conn/serial_model 的枚举，不含 aurora/au:: 类型（sftp_format / tunnel_format /
// keys_format 同族先例，可无头单测）。
//
// 词条分工照 `tunnel_error_key`：本件是唯一把串口枚举与文本名翻成词条 key 的地方，向导与
// 装配层不再各自 switch 一套措辞；波特率、数据位这类**数据段**由调用方原样上屏（换英文即
// 让被测事实消失的那种除外，AGENTS §4.3 第 14 条）。
//
// 稿 §4 那句「CloseReason → 串口文案」在本件**不落**：掉线浮层是 `UI_RECONNECT.draft.md`
// 已裁的通用两态，其措辞按 `session::ReconnectStop` 分档而不按 `CloseReason` 分档，串口侧
// 没有它的消费方（2026-10-11 批 3 落地时按 AGENTS §4.2 第 12 条回写，代价登记在稿 §7）。
// ============================================================

#include <string>
#include <string_view>

#include "conn/serial_model.h"  // SerialPortAccess / SerialPortInfo（非框架类型）

namespace borealis::ui {

/// @brief 校验位名 → 词条 key（`connections.serial.parity.*`）。
///
/// 表外名回落 `none` 档而不另立「未知校验位」措辞：`connection.serial.parity` 是 Choice 域，
/// 装载侧已把表外名挡在落盘之外（`utest_settings_catalog` 的逐键域复核守那条），到这里的名字
/// 必在表内；回落只防「绕过装载侧直接构造档案」的调用方。
[[nodiscard]] auto serial_parity_key(std::string_view parity) -> std::string_view;

/// @brief 流控名 → 词条 key（`connections.serial.flow.*`）；表外名同口径回落 `none` 档。
[[nodiscard]] auto serial_flow_key(std::string_view flow) -> std::string_view;

/// @brief 发送行尾名 → 词条 key（`connections.serial.line_ending.*`）；表外名回落 `LF` 档
///        （设置域与档案的缺省档，裁决 7.33⑥ 的那一份取值口径）。
[[nodiscard]] auto serial_line_ending_key(std::string_view ending) -> std::string_view;

/// @brief 设备可达性 → 归因词条 key（`connections.serial.access.*`）。
///
/// `Openable` 回**空串**：可达项是列表里的常态，给它一句「可用」只会在每行末尾多出一截噪声
/// （稿 §2 标号 3 的判据是「不可达项带归因」，不是「每行带状态」）。
[[nodiscard]] auto serial_access_key(conn::SerialPortAccess access) -> std::string_view;

/// @brief 设备下拉一行的文本：`path`；有友好名缀 ` · 友好名`，归因非空再缀 ` · 归因`。
///
/// 友好名在本沙箱恒空（F3：无 USB 口、无 `/dev/serial/by-id`），故这一件的可证面是「空友好名
/// 与空归因都不多出分隔符」。归因文案（`serial_access_key` 折出的那个 key）由调用方经
/// `settings_label` 现取再传进来：本件不依赖词条表，保持「无 au:: 类型」的可无头形态。
[[nodiscard]] auto serial_device_line(const conn::SerialPortInfo &port, std::string_view attribution)
    -> std::string;

}  // namespace borealis::ui
