/// 测试类型: unit
/// 目标单元: include/borealis/ui/palette.h + src/ui/palette.cpp
/// 测试说明: 调色板三段索引（主题档 / 6×6×6 立方 / 24 阶灰）、色值与来源的合成、
///           bold-is-bright 只作用于前 8 色档、暗淡向底色靠拢、反色与合成次序、
///           不可见保留底色、光标色不参与格合成（裁决 7.25③）、下划线档位原样抵达绘制意图
///           且不参与色合成（裁决 7.28），
///           以及 WCAG 对比度与最小对比度的整数插值口径
///           （SPEC.FEAT.RENDER.03，架构 §9.2）。

#include <cstddef>
#include <cstdint>
#include <string>

#include "borealis/grid/cell.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"

// 颜色是四个通道的组合，框架默认分支只会报 «unprintable»；本套件的断言几乎都在比颜色，
// 拿不到实际值就没法看出是哪一段的位序或哪一档的式子错了。
namespace aurora::testing {

template <>
struct ValuePrinter<borealis::ui::RgbaColor> {
    static auto print(const borealis::ui::RgbaColor &value) -> std::string {
        return "(" + std::to_string(value.red) + ", " + std::to_string(value.green) + ", " +
               std::to_string(value.blue) + ", a" + std::to_string(value.alpha) + ")";
    }
};

}  // namespace aurora::testing

namespace borealis::test_cases::utest_palette {

namespace {

using borealis::grid::Cell;
using borealis::grid::ColorSource;
using borealis::grid::kFlagBold;
using borealis::grid::kFlagDim;
using borealis::grid::kFlagHidden;
using borealis::grid::kFlagNone;
using borealis::grid::kFlagReverse;
using borealis::grid::UnderlineStyle;
using borealis::ui::contrast_ratio;
using borealis::ui::enforce_contrast;
using borealis::ui::palette_color;
using borealis::ui::PaletteSpec;
using borealis::ui::resolve;
using borealis::ui::RgbaColor;

/// @brief 一套可预测的主题：16 基本色按索引递增，默认前景白、默认背景黑。
auto themed() -> PaletteSpec {
    PaletteSpec spec{};
    for (std::size_t index = 0; index < spec.basic.size(); ++index) {
        spec.basic[index] = RgbaColor{static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(index),
                                      static_cast<std::uint8_t>(index)};
    }
    spec.default_foreground = RgbaColor{255U, 255U, 255U};
    spec.default_background = RgbaColor{0U, 0U, 0U};
    return spec;
}

/// @brief 造一格：只给码点，其余按默认色。
auto glyph(char32_t code_point) -> Cell {
    Cell cell{};
    cell.code_point = code_point;
    return cell;
}

auto foreground(ColorSource source, std::uint32_t value) -> Cell {
    auto cell = glyph(U'a');
    cell.foreground = value;
    cell.foreground_source = source;
    return cell;
}

auto background(ColorSource source, std::uint32_t value) -> Cell {
    auto cell = glyph(U'a');
    cell.background = value;
    cell.background_source = source;
    return cell;
}

}  // namespace

AURORA_TEST_CASE(basic_indices_come_from_the_theme) {
    const auto spec = themed();
    AURORA_TEST_CHECK_EQ(palette_color(0U, spec), (RgbaColor{0U, 0U, 0U}));
    AURORA_TEST_CHECK_EQ(palette_color(15U, spec), (RgbaColor{15U, 15U, 15U}));
}

AURORA_TEST_CASE(cube_and_gray_rungs_use_the_xterm_formula) {
    const auto spec = themed();
    AURORA_TEST_CHECK_EQ(palette_color(16U, spec), (RgbaColor{0U, 0U, 0U}));       // 立方原点
    AURORA_TEST_CHECK_EQ(palette_color(196U, spec), (RgbaColor{255U, 0U, 0U}));    // 档位 (5,0,0)
    AURORA_TEST_CHECK_EQ(palette_color(21U, spec), (RgbaColor{0U, 0U, 255U}));     // 档位 (0,0,1)
    AURORA_TEST_CHECK_EQ(palette_color(231U, spec), (RgbaColor{255U, 255U, 255U}));  // 立方终点
    AURORA_TEST_CHECK_EQ(palette_color(232U, spec), (RgbaColor{8U, 8U, 8U}));       // 灰阶起点
    AURORA_TEST_CHECK_EQ(palette_color(255U, spec), (RgbaColor{238U, 238U, 238U}));  // 灰阶终点
}

AURORA_TEST_CASE(color_sources_resolve_by_origin) {
    const auto spec = themed();

    const auto themed_cell = resolve(glyph(U'a'), spec);
    AURORA_TEST_CHECK_EQ(themed_cell.foreground, spec.default_foreground);
    AURORA_TEST_CHECK_EQ(themed_cell.background, spec.default_background);

    const auto indexed = resolve(foreground(ColorSource::Palette, 4U), spec);
    AURORA_TEST_CHECK_EQ(indexed.foreground, (RgbaColor{4U, 4U, 4U}));

    // 真彩色是 0xRRGGBB：位序搞错就是把红蓝互换，故用三个互不相同的字节。
    const auto truecolor = resolve(foreground(ColorSource::Rgb, 0x112233U), spec);
    AURORA_TEST_CHECK_EQ(truecolor.foreground, (RgbaColor{0x11U, 0x22U, 0x33U}));
}

AURORA_TEST_CASE(bold_bright_only_lifts_the_low_palette_rungs) {
    auto spec = themed();
    spec.bold_is_bright = true;

    auto bold_cell = foreground(ColorSource::Palette, 1U);
    bold_cell.flags = kFlagBold;
    AURORA_TEST_CHECK_EQ(resolve(bold_cell, spec).foreground, (RgbaColor{9U, 9U, 9U}));

    // 已在亮色档、未带粗体、或前景来自真彩色与默认色，都不该被再推一档。
    bold_cell.foreground = 9U;
    AURORA_TEST_CHECK_EQ(resolve(bold_cell, spec).foreground, (RgbaColor{9U, 9U, 9U}));
    bold_cell.foreground = 1U;
    bold_cell.flags = kFlagNone;
    AURORA_TEST_CHECK_EQ(resolve(bold_cell, spec).foreground, (RgbaColor{1U, 1U, 1U}));

    auto bold_default = glyph(U'a');
    bold_default.flags = kFlagBold;
    AURORA_TEST_CHECK_EQ(resolve(bold_default, spec).foreground, spec.default_foreground);

    spec.bold_is_bright = false;
    bold_default.flags = kFlagNone;
    bold_cell.flags = kFlagBold;
    bold_cell.foreground = 1U;
    bold_cell.foreground_source = ColorSource::Palette;
    AURORA_TEST_CHECK_EQ(resolve(bold_cell, spec).foreground, (RgbaColor{1U, 1U, 1U}));
}

AURORA_TEST_CASE(dim_mixes_the_foreground_toward_the_background) {
    const auto spec = themed();
    auto cell = foreground(ColorSource::Rgb, 0xC86432U);  // (200,100,50)
    cell.flags = kFlagDim;
    const auto painted = resolve(cell, spec);
    AURORA_TEST_CHECK_EQ(painted.foreground, (RgbaColor{100U, 50U, 25U}));
    AURORA_TEST_CHECK_EQ(painted.background, spec.default_background);
}

AURORA_TEST_CASE(reverse_happens_after_dim_and_bright) {
    const auto spec = themed();
    auto cell = foreground(ColorSource::Rgb, 0xC86432U);  // (200,100,50)
    cell.flags = static_cast<borealis::grid::CellFlags>(kFlagDim | borealis::grid::kFlagReverse);
    const auto painted = resolve(cell, spec);
    // 暗淡先落在前景（→100,50,25），反色随后把它换成底色一侧。
    AURORA_TEST_CHECK_EQ(painted.foreground, spec.default_background);
    AURORA_TEST_CHECK_EQ(painted.background, (RgbaColor{100U, 50U, 25U}));
}

AURORA_TEST_CASE(hidden_keeps_the_background_and_reports_itself) {
    const auto spec = themed();
    auto cell = background(ColorSource::Palette, 4U);
    cell.flags = kFlagHidden;
    const auto painted = resolve(cell, spec);
    AURORA_TEST_CHECK_TRUE(painted.hidden);
    AURORA_TEST_CHECK_EQ(painted.background, (RgbaColor{4U, 4U, 4U}));
}

AURORA_TEST_CASE(contrast_ratio_follows_wcag_luminance) {
    constexpr RgbaColor white{255U, 255U, 255U};
    constexpr RgbaColor black{0U, 0U, 0U};
    constexpr RgbaColor mid_gray{118U, 118U, 118U};  // WCAG 上落在白底刚好过 AA 的灰

    const auto extreme = contrast_ratio(white, black);
    AURORA_TEST_CHECK_NEAR(extreme, 21.0, 0.01);
    AURORA_TEST_CHECK_NEAR(contrast_ratio(black, white), extreme, 0.01);  // 与顺序无关
    AURORA_TEST_CHECK_NEAR(contrast_ratio(white, white), 1.0, 0.01);
    AURORA_TEST_CHECK_NEAR(contrast_ratio(mid_gray, white), 4.54, 0.02);
}

AURORA_TEST_CASE(min_contrast_lifts_only_as_far_as_needed) {
    constexpr RgbaColor black{0U, 0U, 0U};
    constexpr RgbaColor dark_gray{40U, 40U, 40U};

    const auto lifted = enforce_contrast(dark_gray, black, 7.0);
    const auto ratio = contrast_ratio(lifted, black);
    AURORA_TEST_CHECK_GE(ratio, 7.0);
    AURORA_TEST_CHECK_LT(ratio, 7.6);  // 达标即止，不一路推到纯白
    AURORA_TEST_CHECK_EQ(lifted.red, lifted.green);
    AURORA_TEST_CHECK_GT(lifted.red, dark_gray.red);

    constexpr RgbaColor already_readable{200U, 200U, 200U};
    AURORA_TEST_CHECK_EQ(enforce_contrast(already_readable, black, 7.0), already_readable);
    AURORA_TEST_CHECK_EQ(enforce_contrast(dark_gray, black, 1.0), dark_gray);
}

AURORA_TEST_CASE(unreachable_ratio_takes_the_stronger_extreme) {
    constexpr RgbaColor mid_gray{128U, 128U, 128U};
    constexpr RgbaColor faint{200U, 100U, 50U};
    // 底色是中等灰：它的相对亮度 0.216 离黑更近，故黑色的对比（≈5.3）强过白色（≈3.95），
    // 而两者都到不了 8 —— 取更强的黑色而不是硬凑。
    const auto forced = enforce_contrast(faint, mid_gray, 8.0);
    AURORA_TEST_CHECK_EQ(forced, (RgbaColor{0U, 0U, 0U}));

    // alpha 属于前景，调整时不得改动。
    constexpr RgbaColor same_as_background{128U, 128U, 128U, 128U};
    const auto kept = enforce_contrast(same_as_background, mid_gray, 2.0);
    AURORA_TEST_CHECK_EQ(kept.alpha, 128U);
    AURORA_TEST_CHECK_LT(kept.red, same_as_background.red);
}

AURORA_TEST_CASE(min_contrast_switch_gates_the_lift) {
    auto spec = themed();
    auto cell = foreground(ColorSource::Rgb, 0x282828U);  // 近黑的灰，落在黑底上几乎不可读

    AURORA_TEST_CHECK_EQ(resolve(cell, spec).foreground, (RgbaColor{40U, 40U, 40U}));

    spec.min_contrast_enabled = true;
    spec.min_contrast = 7.0;
    const auto lifted = resolve(cell, spec);
    AURORA_TEST_CHECK_GE(contrast_ratio(lifted.foreground, lifted.background), 7.0);
}

AURORA_TEST_CASE(cursor_color_never_reaches_cell_paint) {
    auto spec = themed();
    const auto cell = glyph(U'a');
    const auto baseline = resolve(cell, spec);

    // 光标色是整屏取用、不属任何一格（裁决 7.25③）：接进 resolve 就等于让一格的内容决定光标颜色。
    spec.cursor_color = RgbaColor{255U, 0U, 128U};
    AURORA_TEST_CHECK_EQ(resolve(cell, spec), baseline);

    // 反向守卫：同样的位置换成前景色就必须改变绘制意图，否则上一条断言是空转。
    spec.default_foreground = *spec.cursor_color;
    AURORA_TEST_CHECK_NE(resolve(cell, spec).foreground, baseline.foreground);
}

AURORA_TEST_CASE(underline_style_passes_through_without_touching_colors) {
    const auto spec = themed();
    auto cell = glyph(U'a');
    const auto plain = resolve(cell, spec);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(plain.underline),
                         static_cast<std::uint32_t>(UnderlineStyle::None));

    // 档位只决定笔形、不参与色合成；它必须原样抵达绘制意图，否则三档在屏上不可辨（裁决 7.28）。
    cell.underline = UnderlineStyle::Double;
    const auto ruled = resolve(cell, spec);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(ruled.underline),
                         static_cast<std::uint32_t>(UnderlineStyle::Double));
    AURORA_TEST_CHECK_EQ(ruled.foreground, plain.foreground);
    AURORA_TEST_CHECK_EQ(ruled.background, plain.background);
}

}  // namespace borealis::test_cases::utest_palette
