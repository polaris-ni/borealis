#pragma once

// ============================================================
// 色值的文本形态（include/borealis/ui/color_text.h）
// ------------------------------------------------------------
// `#RRGGBB` 是色值**唯一**的落盘与输入形态。装载侧 `src/config/store.cpp` 与设置面板的色值
// 输入框都收这一个判定（裁决 7.52 的 S14：校验式必须与装载侧同一条），否则面板接受而存不回去
// 的形态（八位 `#RRGGBBAA`、`rgb()` 函数式）就会成为第二真值源。
//
// alpha 不参与本形态：终端色带不做透明混合（`ui::contrast_ratio` 同口径），故解析产物恒
// `alpha == 255`，格式化也不输出 alpha 位。将来要让透明度可配，先改的是 `PaletteSpec` 的落盘
// 形态而不是本件的位数。
//
// 本头刻意不含 Aurora 类型（AGENTS.md §4.4 第 20 条）。
// ============================================================

#include <optional>
#include <string>
#include <string_view>

#include "borealis/ui/palette.h"

namespace borealis::ui {

/// @brief 色值 → `#RRGGBB`（大写十六进制，`#` 前缀必带）。
/// @param color 待格式化的色值；`alpha` 不参与输出。
/// @return 恒为 7 字符，且必可被 `color_from_hex` 读回同一份 RGB。
[[nodiscard]] auto color_to_hex(const RgbaColor &color) -> std::string;

/// @brief `#RRGGBB` → 色值（大小写不敏感）。
/// @param text 待解析的文本；长度不是 7、缺 `#` 前缀、任一数字位非十六进制都算失败。
/// @return 解析成功为色值（`alpha` 取 255）；失败为空，由调用方回落默认并留痕。
[[nodiscard]] auto color_from_hex(std::string_view text) -> std::optional<RgbaColor>;

}  // namespace borealis::ui
