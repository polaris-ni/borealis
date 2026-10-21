#pragma once

// ============================================================
// 连接类型 → 图标的唯一字形表（src/ui/connection_glyph.h）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.04` 那条「逐标签连接类型图标」的判据源，同时替换裁决 7.110 留在侧栏的那枚两值
// 函数（`#`＝SSH、其余一律 `>`，稿 §0 的 F4）。侧栏与标签栏各调一次本件，两处不可能分叉
// （稿 §5 第 2 条），而 `src/conn/profile_views.h` 里那句「类型图标取这里」自此有一份真实的表可指。
//
// 本头只含标准类型 + `conn/profile.h`，不含 aurora/`au::` 类型（`serial_format` / `sftp_format` /
// `tunnel_format` / `keys_format` 同族先例，可无头单测）。
//
// 字形取 ASCII 单字而不是图形，由两条实测决定（稿 §0 的 F1／F9）：本仓没有图标资源也不打算引一枚
// （三枚状态角标至今是几何块正是同一件事）；而 13 pt 下四枚字形的实绘行带 12 px（y 21..32，Headless
// 下 dp＝px），栏内 6 dp 的角位物理放不下 ⇒ 图标只能落在名称左侧的槽里，那里画的是文本。
//
// 这张表**是判据而不是实现细节**：改任何一枚字形都要同批改 `utest_connection_glyph` 与稿 §2 判据 ①，
// 而「四档两两不等」那条承诺专门挡住了「把串口折成本地」这一类退化（裁决 7.110 D12① 的原样复活）。
// ============================================================

#include <string_view>

#include "borealis/conn/profile.h"  // ConnectionType（本域纯逻辑类型，非框架类型）

namespace borealis::ui {

/// @brief 连接类型在侧栏行与标签栏左槽上的字形（单枚 ASCII 字符，AGENTS.md §4.3 第 14 条）。
///
/// 四档的读数：本地 `>`（shell 提示符）、SSH `#`、串口 `S`、Telnet `T`。前两枚沿用侧栏既有读数
/// （裁决 7.110 那枚两值函数的两档），后两枚是 `SPEC.FEAT.WS.04` 本棒新增——串口与 Telnet 两档
/// 各自有图标，正是 7.110 D12① 与 7.112 那条「本期识别靠档案名」在册欠账的出账形态。
[[nodiscard]] auto connection_type_glyph(conn::ConnectionType type) -> std::string_view;

}  // namespace borealis::ui
