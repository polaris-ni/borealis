// ============================================================
// 调色板与 SGR 颜色合成（src/ui/palette.cpp）
// ------------------------------------------------------------
// 需求 SPEC.FEAT.RENDER.03：色值与来源分开存（`grid::Cell`），到这一层才套主题重映射、
// 亮色档与最小对比度。全部是整数与浮点的确定性算式，不含框架类型，故可全量单测。
// ============================================================

#include "borealis/ui/palette.h"

#include <algorithm>
#include <cmath>

namespace borealis::ui {
namespace {

/// @brief 256 色索引的三段边界（xterm 兼容）：0..15 主题档、16..231 立方、232..255 灰阶。
constexpr std::uint8_t kBasicColorCount = 16U;
constexpr std::uint8_t kCubeLevelCount = 6U;
constexpr std::uint8_t kCubeStep = 40U;
constexpr std::uint8_t kCubeOffset = 55U;
constexpr std::uint8_t kGrayFirst = 232U;
constexpr std::uint8_t kGrayStep = 10U;
constexpr std::uint8_t kGrayFirstLevel = 8U;

/// @brief sRGB 线性化的分段拐点与偏移（IEC 61966-2-1）。
constexpr double kSrgbLinearThreshold = 0.03928;
constexpr double kSrgbOffset = 0.055;

/// @brief WCAG 相对亮度的通道权重与对比度公式的常数项。
constexpr double kLuminanceRed = 0.2126;
constexpr double kLuminanceGreen = 0.7152;
constexpr double kLuminanceBlue = 0.0722;
constexpr double kLuminanceBias = 0.05;

/// @brief 前景与目标色按 `[0, kContrastSearchSteps]` 档位的整数插值。
constexpr int kContrastSearchSteps = 256;

[[nodiscard]] auto channel_from_rgb(std::uint32_t packed, std::uint32_t shift) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>((packed >> shift) & 0xFFU);
}

/// @brief 把一格按来源解析成颜色：索引查表，真彩色拆位，未指定取主题默认值。
[[nodiscard]] auto cell_foreground(const grid::Cell &cell, const PaletteSpec &spec) noexcept -> RgbaColor {
    switch (cell.foreground_source) {
        case grid::ColorSource::Palette:
            return palette_color(static_cast<std::uint8_t>(cell.foreground & 0xFFU), spec);
        case grid::ColorSource::Rgb:
            return RgbaColor{channel_from_rgb(cell.foreground, 16U), channel_from_rgb(cell.foreground, 8U),
                             channel_from_rgb(cell.foreground, 0U)};
        case grid::ColorSource::Default:
            break;
    }
    return spec.default_foreground;
}

[[nodiscard]] auto cell_background(const grid::Cell &cell, const PaletteSpec &spec) noexcept -> RgbaColor {
    switch (cell.background_source) {
        case grid::ColorSource::Palette:
            return palette_color(static_cast<std::uint8_t>(cell.background & 0xFFU), spec);
        case grid::ColorSource::Rgb:
            return RgbaColor{channel_from_rgb(cell.background, 16U), channel_from_rgb(cell.background, 8U),
                             channel_from_rgb(cell.background, 0U)};
        case grid::ColorSource::Default:
            break;
    }
    return spec.default_background;
}

/// @brief 立方档的单通道值：档位 0 是纯黑，其后按 xterm 的 55 + 40·档 步进。
[[nodiscard]] auto cube_channel(std::uint8_t level) noexcept -> std::uint8_t {
    if (level == 0U) {
        return 0U;
    }
    return static_cast<std::uint8_t>(kCubeOffset + level * kCubeStep);
}

/// @brief 通道向目标色插值，四舍五入到 8 位整数。
[[nodiscard]] auto mix_channel(std::uint8_t from, std::uint8_t to, int weight) noexcept -> std::uint8_t {
    const auto span = static_cast<double>(to) - static_cast<double>(from);
    const auto stepped = static_cast<double>(from) + span * static_cast<double>(weight) /
                                                static_cast<double>(kContrastSearchSteps);
    return static_cast<std::uint8_t>(std::lround(stepped));
}

[[nodiscard]] auto mix(const RgbaColor &from, const RgbaColor &to, int weight) noexcept -> RgbaColor {
    return RgbaColor{mix_channel(from.red, to.red, weight), mix_channel(from.green, to.green, weight),
                     mix_channel(from.blue, to.blue, weight), from.alpha};
}

/// @brief 通道的 sRGB 相对亮度分量（线性化后除以 255 的幂已在内层做）。
[[nodiscard]] auto linear_channel(std::uint8_t channel) noexcept -> double {
    const auto normalized = static_cast<double>(channel) / 255.0;
    if (normalized <= kSrgbLinearThreshold) {
        return normalized / 12.92;
    }
    return std::pow((normalized + kSrgbOffset) / (1.0 + kSrgbOffset), 2.4);
}

[[nodiscard]] auto relative_luminance(const RgbaColor &color) noexcept -> double {
    return kLuminanceRed * linear_channel(color.red) + kLuminanceGreen * linear_channel(color.green) +
           kLuminanceBlue * linear_channel(color.blue);
}

/// @brief 亮色档：粗体且前景取自前 8 色时改取其后 8 色的亮色档（同条需求的可配开关）。
[[nodiscard]] auto brightened(std::uint8_t index) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(index + kBasicColorCount / 2U);
}

}  // namespace

auto palette_color(std::uint8_t index, const PaletteSpec &spec) noexcept -> RgbaColor {
    if (index < kBasicColorCount) {
        return spec.basic[index];
    }
    if (index < kGrayFirst) {
        const auto ordinal = static_cast<std::uint8_t>(index - kBasicColorCount);
        const auto red_level = static_cast<std::uint8_t>(ordinal / (kCubeLevelCount * kCubeLevelCount));
        const auto green_level = static_cast<std::uint8_t>(ordinal / kCubeLevelCount % kCubeLevelCount);
        const auto blue_level = static_cast<std::uint8_t>(ordinal % kCubeLevelCount);
        return RgbaColor{cube_channel(red_level), cube_channel(green_level), cube_channel(blue_level)};
    }
    const auto gray = static_cast<std::uint8_t>(kGrayFirstLevel + (index - kGrayFirst) * kGrayStep);
    return RgbaColor{gray, gray, gray};
}

auto resolve(const grid::Cell &cell, const PaletteSpec &spec) -> CellPaint {
    CellPaint paint{};
    paint.bold = (cell.flags & grid::kFlagBold) != 0U;
    paint.italic = (cell.flags & grid::kFlagItalic) != 0U;
    paint.underline = cell.underline;
    paint.strike = (cell.flags & grid::kFlagStrike) != 0U;
    paint.hidden = (cell.flags & grid::kFlagHidden) != 0U;

    auto foreground = cell_foreground(cell, spec);
    auto background = cell_background(cell, spec);

    // 次序是有意的：亮色档要在反色之前（反色后拿到的底色无从判断原色是否属于前 8 色档），
    // 最小对比度要在最后（前三步都可能把颜色拉近背景）。
    if (paint.bold && spec.bold_is_bright && cell.foreground_source == grid::ColorSource::Palette &&
        (cell.foreground & 0xFFU) < kBasicColorCount / 2U) {
        foreground = palette_color(brightened(static_cast<std::uint8_t>(cell.foreground & 0xFFU)), spec);
    }
    if ((cell.flags & grid::kFlagDim) != 0U) {
        // 暗淡按「向底色靠拢一半」而非减亮度：底色不受本仓控制（主题与真彩色背景都可能比
        // 前景更暗），减半亮度在浅色主题上会把暗淡字推成高对比的深色字，语义正好相反。
        foreground = mix(foreground, background, kContrastSearchSteps / 2);
    }
    if ((cell.flags & grid::kFlagReverse) != 0U) {
        std::swap(foreground, background);
    }
    if (spec.min_contrast_enabled) {
        foreground = enforce_contrast(foreground, background, spec.min_contrast);
    }

    paint.foreground = foreground;
    paint.background = background;
    return paint;
}

auto contrast_ratio(const RgbaColor &a, const RgbaColor &b) noexcept -> double {
    const auto first = relative_luminance(a);
    const auto second = relative_luminance(b);
    const auto lighter = std::max(first, second);
    const auto darker = std::min(first, second);
    return (lighter + kLuminanceBias) / (darker + kLuminanceBias);
}

auto enforce_contrast(const RgbaColor &foreground, const RgbaColor &background, double ratio) noexcept -> RgbaColor {
    if (ratio <= 1.0 || contrast_ratio(foreground, background) >= ratio) {
        return foreground;
    }
    const RgbaColor white{255U, 255U, 255U, foreground.alpha};
    const RgbaColor black{0U, 0U, 0U, foreground.alpha};
    const auto to_white = contrast_ratio(white, background);
    const auto to_black = contrast_ratio(black, background);
    const auto target = to_white >= to_black ? white : black;
    if (std::max(to_white, to_black) < ratio) {
        // 底色本身把两个极端的对比都压到了阈值下：取更强的一侧，不为凑数去动背景（背景是整屏
        // 色带的基准，改它会让相邻格的底色互不一致）。
        return target;
    }

    // 插值路径上的对比度随 weight 单调（每个通道都朝同一极值单向移动，相对亮度随之单调），
    // 故整数二分能取到「达标的最浅插值」，少走纯白/纯黑那种突兀的跳变。
    int low = 0;
    int high = kContrastSearchSteps;
    while (high - low > 1) {
        const auto middle = (low + high) / 2;
        if (contrast_ratio(mix(foreground, target, middle), background) >= ratio) {
            high = middle;
        } else {
            low = middle;
        }
    }
    return mix(foreground, target, high);
}

}  // namespace borealis::ui
