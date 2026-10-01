// ============================================================
// 内置预置配色表的色值（src/config/themes.cpp）
// ------------------------------------------------------------
// 色值按各主题的官方 ANSI 槽位序（0..7 常规、8..15 亮色）落表，槽位语义由主题自己决定、
// 与槽号无关：Dracula 的 blue 槽按其官方配色落成紫（0xBD93F9），Campbell 的 blue 槽是深蓝
// （0x0037DA）。照「blue 槽=天蓝」的直觉核对会误判成表写错了，故此处按官方值原样收录。
//
// alpha 不参与派生：终端色带不做透明混合（`ui::contrast_ratio` 同口径），落表即不透明。
// ============================================================

#include "borealis/config/themes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace borealis::config {

namespace {

/// @brief 从 `#RRGGBB` 整数取色。
/// @param hex 24 位色值（高位为红）。
/// @return alpha 恒为不透明的颜色。
constexpr auto rgb(std::uint32_t hex) noexcept -> ui::RgbaColor {
    return ui::RgbaColor{static_cast<std::uint8_t>((hex >> 16U) & 0xFFU),
                         static_cast<std::uint8_t>((hex >> 8U) & 0xFFU),
                         static_cast<std::uint8_t>(hex & 0xFFU),
                         static_cast<std::uint8_t>(0xFFU)};
}

/// @brief 一张表的原始色值：整数形态只为书写密度，语义在 `to_palette` 里合成。
struct ThemeColors {
    std::string_view name;
    std::array<std::uint32_t, 16> basic;
    std::uint32_t foreground;
    std::uint32_t background;
    std::uint32_t cursor;
};

/// @brief 原始色值 → 生效调色板：只填色，开关类字段留给配置层。
constexpr auto to_palette(const ThemeColors &colors) noexcept -> ui::PaletteSpec {
    ui::PaletteSpec spec{};
    for (std::size_t index = 0; index < colors.basic.size(); ++index) {
        spec.basic[index] = rgb(colors.basic[index]);
    }
    spec.default_foreground = rgb(colors.foreground);
    spec.default_background = rgb(colors.background);
    spec.cursor_color = rgb(colors.cursor);
    return spec;
}

constexpr std::array<ThemeColors, 8> kThemeColors{{
    ThemeColors{
        "dracula",
        {0x21222C, 0xFF5555, 0x50FA7B, 0xF1FA8C, 0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2,  //
         0x62728A, 0xFF6E6E, 0x69FF94, 0xFFFFA5, 0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF},
        0xF8F8F2,
        0x282A36,
        0xF8F8F2},
    ThemeColors{
        "nord",
        {0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xE5E9F0,  //
         0x4C566A, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x8FBCBB, 0xECEFF4},
        0xD8DEE9,
        0x2E3440,
        0xD8DEE9},
    ThemeColors{
        "solarized-dark",
        {0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,  //
         0x002B36, 0xCB4B16, 0x586E75, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3},
        0x839496,
        0x002B36,
        0x839496},
    ThemeColors{
        "one-dark",
        {0x3F4451, 0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xABB2BF,  //
         0x5C6370, 0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0xC678DD, 0x56B6C2, 0xFFFFFF},
        0xABB2BF,
        0x282C34,
        0x528BFF},
    ThemeColors{
        "gruvbox-dark",
        {0x282828, 0xCC241D, 0x98971A, 0xD79921, 0x458588, 0xB16286, 0x689D6A, 0xA89984,  //
         0x928374, 0xFB4934, 0xB8BB26, 0xFABD2F, 0x83A598, 0xD3869B, 0x8EC07C, 0xEBDBB2},
        0xEBDBB2,
        0x282828,
        0xEBDBB2},
    ThemeColors{
        "monokai",
        {0x272822, 0xF92672, 0xA6E22E, 0xF4BF75, 0x66D9EF, 0xAE81FF, 0x66D9EF, 0xF8F8F2,  //
         0x75715E, 0xF92672, 0xA6E22E, 0xF4BF75, 0x66D9EF, 0xAE81FF, 0x66D9EF, 0xF8F8F2},
        0xF8F8F2,
        0x272822,
        0xF8F8F2},
    ThemeColors{
        "campbell",
        {0x0C0C0C, 0xC50F1F, 0x13A10E, 0xC19C00, 0x0037DA, 0x881798, 0x3A96DD, 0xCCCCCC,  //
         0x767676, 0xE74856, 0x16C60C, 0xF9F1A5, 0x3B78FF, 0xB4009E, 0x61D6D6, 0xF2F2F2},
        0xCCCCCC,
        0x0C0C0C,
        0xFFFFFF},
    ThemeColors{
        "tokyo-night",
        {0x15161E, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xA9B1D6,  //
         0x414868, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xC0CAF5},
        0xC0CAF5,
        0x1A1B26,
        0xC0CAF5},
}};

/// @brief 表 → 主题数组：派生一次即常驻，避免每次取用都重算 128 个色值。
auto theme_table() -> const std::array<BuiltinTheme, kThemeColors.size()> & {
    static const auto kThemes = [] {
        std::array<BuiltinTheme, kThemeColors.size()> out{};
        for (std::size_t index = 0; index < kThemeColors.size(); ++index) {
            out[index] = BuiltinTheme{kThemeColors[index].name, to_palette(kThemeColors[index]), std::nullopt};
        }
        return out;
    }();
    return kThemes;
}

}  // namespace

auto builtin_themes() noexcept -> std::span<const BuiltinTheme> {
    return theme_table();
}

auto find_builtin_theme(std::string_view name) noexcept -> const BuiltinTheme * {
    const auto &table = theme_table();
    const auto found = std::ranges::find_if(table, [name](const BuiltinTheme &theme) { return theme.name == name; });
    return found == table.end() ? nullptr : &*found;
}

auto theme_palette(std::string_view name) noexcept -> ui::PaletteSpec {
    const auto *theme = find_builtin_theme(name);
    if (theme == nullptr) {
        theme = &theme_table().front();
    }
    return theme->palette;
}

}  // namespace borealis::config
