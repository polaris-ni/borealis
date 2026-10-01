#pragma once

// ============================================================
// 调色板与 SGR 颜色合成（include/borealis/ui/palette.h）
// ------------------------------------------------------------
// 架构 §9.2：颜色合成（含 bold-is-bright 与最小对比度）留在应用侧，框架原语只收合成后的
// 最终值。`grid::Cell` 存的是「色值 + 来源」（`SPEC.FEAT.TERM.02`），来源必须保留到这一层
// 才还能套主题重映射与亮色档——在状态机里预解析成 RGB 就等于把主题切换的依据丢了。
//
// 本头刻意不含 Aurora 类型：`ui` 域公共头不得触达框架头，且这段逻辑是渲染层唯一能全量
// 单测的部分（AGENTS.md §4.4 第 20 条）。框架的 `Color` 只在 `src/ui/` 内与本仓 POD 互转。
// ============================================================

#include <array>
#include <cstdint>
#include <optional>

#include "borealis/grid/cell.h"

namespace borealis::ui {

/// @brief 一个 8 位 RGBA 颜色（alpha 255 为不透明）。
struct RgbaColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;

    /// @brief 逐通道全等比较（alpha 参与：半透明与不透明是两种可观察状态）。
    [[nodiscard]] constexpr auto operator==(const RgbaColor &other) const noexcept -> bool = default;
};

/// @brief 由配置/主题提供的调色板：16 基本色可重映射，256 色的其余档位按标准式子派生。
struct PaletteSpec {
    std::array<RgbaColor, 16> basic{};  ///< 16 基本色（索引 0..15），由主题给出。
    RgbaColor default_foreground{};     ///< 前景未指定时使用的颜色。
    RgbaColor default_background{};     ///< 背景未指定时使用的颜色。

    /// @brief 光标色；空表示未配，绘制侧回落 `default_foreground`（裁决 7.25③）。
    ///
    /// 用 `optional` 而非零值默认：黑色光标是合法配置，与「没配」必须可区分。
    /// `resolve` 不参与它——一格的光标色不取决于该格内容，故它只经视口层取用。
    std::optional<RgbaColor> cursor_color;

    /// @brief 「粗体渲染为亮色」（`SPEC.FEAT.RENDER.03` 的可配开关）。
    ///
    /// 开启后，带粗体标志且前景取自**前 8 色**时改取其后 8 色的亮色档（索引 +8）。
    bool bold_is_bright = false;

    /// @brief 「最小对比度强制」（同条需求的可配开关，避免深色主题下不可读）。
    bool min_contrast_enabled = false;

    /// @brief 最小对比度阈值（WCAG 对比度比值，配在 [1, 21]）。
    ///
    /// 只在 `min_contrast_enabled` 为真时参与合成。
    double min_contrast = 1.0;

    /// @brief 逐字段全等比较：配置往返（写盘再读回）的判据（`SPEC.FEAT.PREF.03`）。
    ///
    /// C++20 不会为类隐式声明 `==`，故此处必须显式 default——`config::Settings` 的默认比较
    /// 依赖它，缺了它整份配置的等值判定会被静默删除。
    [[nodiscard]] constexpr auto operator==(const PaletteSpec &other) const noexcept -> bool = default;
};

/// @brief 一格合成后的绘制意图：最终前景/背景与影响笔形的标志。
///
/// 「暗淡」已折进前景（见 `resolve` 的合成次序），故不另设标志——留给绘制侧一个可加的
/// 标志就等于允许多条暗淡路径。
struct CellPaint {
    RgbaColor foreground{};
    RgbaColor background{};
    bool bold = false;      ///< SGR 1：换字重（不改格宽，见架构 §9.2）。
    bool italic = false;    ///< SGR 3。
    grid::UnderlineStyle underline = grid::UnderlineStyle::None;  ///< SGR 4 及其子参数（裁决 7.28①）。
    bool strike = false;    ///< SGR 9。
    bool hidden = false;    ///< SGR 8：文本与装饰都不画，底色仍有效。

    /// @brief 逐字段全等比较：它就是 run 合并的判据（裁决 7.23②「样式全等合并到行」）。
    [[nodiscard]] constexpr auto operator==(const CellPaint &other) const noexcept -> bool = default;
};

/// @brief 把调色板索引解析成颜色。
///
/// 索引 0..15 取 `spec.basic`（主题可重映射的就是这 16 档）；16..231 是 6×6×6 立方，
/// 232..255 是 24 阶灰——这两段是 xterm 兼容的固定式子，不受主题影响，故不入库配置。
/// @param index 调色板索引。
/// @param spec 调色板配置。
/// @return 索引对应的颜色。
[[nodiscard]] auto palette_color(std::uint8_t index, const PaletteSpec &spec) noexcept -> RgbaColor;

/// @brief 按 `SPEC.FEAT.RENDER.03` 把一格的色值与标志合成为最终绘制意图。
///
/// 合成次序是有意的：**亮色档 → 暗淡 → 反色 → 最小对比度**。反色若在亮色档之前，粗体亮色
/// 档的格子反色后会把基色当亮色用；最小对比度放最后，因为前三步都可能把颜色拉近背景。
/// @param cell 网格中的一格。
/// @param spec 调色板配置。
/// @return 该格的绘制意图。
[[nodiscard]] auto resolve(const grid::Cell &cell, const PaletteSpec &spec) -> CellPaint;

/// @brief 两色的 WCAG 对比度比值（1..21，越大越易读）。
///
/// 相对亮度按 sRGB 线性化（IEC 61966-2-1 的分段式）与 0.2126/0.7152/0.0722 权重计算，
/// 忽略 alpha：终端色带本身不参与透明混合。
[[nodiscard]] auto contrast_ratio(const RgbaColor &a, const RgbaColor &b) noexcept -> double;

/// @brief 把前景调到与背景至少相差 `ratio` 对比度。
///
/// 朝「与背景对比更强的一侧」（纯白或纯黑）线性插值，直到达标；两侧都达不到时取对比更高的
/// 那一侧。改动只在前景一侧，背景是整屏色带的基准，动它会让相邻格底色互不一致。
/// @param foreground 待调整的前景。
/// @param background 该格的背景。
/// @param ratio 目标对比度。
/// @return 达标后的前景（已达标时按值返回原色）。
[[nodiscard]] auto enforce_contrast(const RgbaColor &foreground, const RgbaColor &background, double ratio) noexcept
    -> RgbaColor;

}  // namespace borealis::ui
