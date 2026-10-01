/// 测试类型: unit
/// 目标单元: include/borealis/ui/cell_layout.h + src/ui/cell_layout.cpp
/// 测试说明: 整格像素度量按 scale 换算成 dp 步长（含非整除缩放的取整容差、「装不满也给一格」与
///           不可用度量的空网格）、行列数即尺寸的 UI 侧来源（SPEC.FEAT.XFER.01）、run 按样式全等
///           合并且色带与文本共用同一批切分边界（裁决 7.23②）、双宽延续格不进文本但保留列宽、
///           combining 随基础码点并进同段文本（裁决 7.23ⓑ）、空格与不可见段的丢弃与底色保留、
///           下划线空格段仍需交给绘制侧（SPEC.FEAT.RENDER.01、SPEC.FEAT.TERM.08）。

#include <cstddef>
#include <cstdint>
#include <string>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_cell_layout {

namespace {

using borealis::grid::Cell;
using borealis::grid::CellFlags;
using borealis::grid::ColorSource;
using borealis::grid::kFlagBold;
using borealis::grid::kFlagHidden;
using borealis::grid::kFlagUnderline;
using borealis::grid::kFlagWideContinuation;
using borealis::grid::Row;
using borealis::ui::CellPixels;
using borealis::ui::layout_row;
using borealis::ui::LogicalSize;
using borealis::ui::make_geometry;
using borealis::ui::PaletteSpec;
using borealis::ui::rect_for;
using borealis::ui::RgbaColor;
using borealis::ui::StyleRun;

/// @brief 默认前景白、默认背景黑（与 `themed` 用例里的调色板档区分开）。
auto themed() -> PaletteSpec {
    PaletteSpec spec{};
    spec.default_foreground = RgbaColor{255U, 255U, 255U};
    spec.default_background = RgbaColor{0U, 0U, 0U};
    return spec;
}

/// @brief 在第 @p column 列写一个码点（默认色）。
auto put(Row &row, std::size_t column, char32_t code_point) -> void {
    Cell cell{};
    cell.code_point = code_point;
    row.set(column, cell);
}

auto put(Row &row, std::size_t column, const Cell &cell) -> void { row.set(column, cell); }

/// @brief 造一个真彩色底色的格子（红底是选区与色带用例的常用素材）。
auto red_background(char32_t code_point) -> Cell {
    Cell cell{};
    cell.code_point = code_point;
    cell.background = 0xFF0000U;
    cell.background_source = ColorSource::Rgb;
    return cell;
}

/// @brief 取第 @p index 段；越界返回空段，让失败信息落在断言而不是崩溃上。
auto at(const std::vector<StyleRun> &runs, std::size_t index) -> const StyleRun & {
    static const StyleRun kEmpty{};
    return index < runs.size() ? runs[index] : kEmpty;
}

}  // namespace

AURORA_TEST_CASE(geometry_divides_pixel_metrics_by_scale) {
    const auto geometry = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{80.0, 80.0});
    AURORA_TEST_CHECK_NEAR(geometry.cell_width, 4.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(geometry.cell_height, 8.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(geometry.ascent, 6.0, 1.0e-9);
    AURORA_TEST_CHECK_EQ(geometry.columns, 20U);
    AURORA_TEST_CHECK_EQ(geometry.rows, 10U);
}

AURORA_TEST_CASE(geometry_counts_whole_cells_and_keeps_at_least_one) {
    const auto geometry = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{85.0, 8.0});
    AURORA_TEST_CHECK_EQ(geometry.columns, 21U);  // 85 dp 装 21 个 4 dp 格，余数不成格
    AURORA_TEST_CHECK_EQ(geometry.rows, 1U);      // 不足一格高仍给一格，否则整屏无处可画
}

AURORA_TEST_CASE(geometry_tolerates_non_terminating_scale) {
    // 7 px 与 14 px 在 1.5× 下都是无限二进制小数，可视宽由同一次除法反乘回来，
    // 比值会差出 1e-15 量级；无容差就少算一整列一整行。
    const auto side = 30.0 * (7.0 / 1.5);
    const auto geometry = make_geometry(CellPixels{7, 14, 11}, 1.5, LogicalSize{side, side});
    AURORA_TEST_CHECK_EQ(geometry.columns, 30U);
    AURORA_TEST_CHECK_EQ(geometry.rows, 15U);
}

AURORA_TEST_CASE(geometry_is_empty_when_metrics_or_viewport_are_unusable) {
    const auto no_font = make_geometry(CellPixels{0, 0, 0}, 1.0, LogicalSize{800.0, 600.0});
    AURORA_TEST_CHECK_EQ(no_font.columns, 0U);
    AURORA_TEST_CHECK_EQ(no_font.rows, 0U);

    const auto minimized = make_geometry(CellPixels{8, 16, 12}, 1.0, LogicalSize{0.0, 0.0});
    AURORA_TEST_CHECK_EQ(minimized.columns, 0U);
    AURORA_TEST_CHECK_EQ(minimized.rows, 0U);
}

AURORA_TEST_CASE(rect_maps_row_and_column_bounds_to_dp) {
    const auto geometry = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{80.0, 80.0});
    const auto rect = rect_for(geometry, 2U, 1U, 4U);
    AURORA_TEST_CHECK_NEAR(rect.x, 4.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect.y, 16.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect.width, 12.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect.height, 8.0, 1.0e-9);
}

AURORA_TEST_CASE(blank_row_produces_nothing) {
    const auto spec = themed();
    const Row row{10U};
    AURORA_TEST_CHECK_TRUE(layout_row(row, spec).empty());  // 一屏空格的常见形态：不产任何段
}

AURORA_TEST_CASE(contiguous_same_style_cells_merge_into_one_run) {
    const auto spec = themed();
    Row row{6U};
    put(row, 0U, U'a');
    put(row, 1U, U'b');
    put(row, 2U, U'c');
    put(row, 4U, U'd');  // 第 3 列是空格：同色同字体，与两侧并进同一段

    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 0U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 6U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).text, std::string{"abc d "});
}

AURORA_TEST_CASE(style_change_splits_and_the_band_shares_the_boundary) {
    const auto spec = themed();
    Row row{4U};
    put(row, 0U, U'a');
    put(row, 1U, U'b');
    put(row, 2U, red_background(U'c'));
    put(row, 3U, red_background(U'c'));

    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 2U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, at(runs, 1).first_column);  // 同一批切分边界
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 0U);
    AURORA_TEST_CHECK_EQ(at(runs, 1).last_column, 4U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).text, std::string{"ab"});
    AURORA_TEST_CHECK_EQ(at(runs, 1).text, std::string{"cc"});
    AURORA_TEST_CHECK_EQ(at(runs, 0).paint.background, spec.default_background);
    AURORA_TEST_CHECK_EQ(at(runs, 1).paint.background, (RgbaColor{255U, 0U, 0U}));
}

AURORA_TEST_CASE(font_weight_change_splits_the_row) {
    const auto spec = themed();
    Row row{3U};
    put(row, 0U, U'a');
    Cell emphasized{};
    emphasized.code_point = U'b';
    emphasized.flags = kFlagBold;
    put(row, 1U, emphasized);
    put(row, 2U, U'c');

    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 3U);
    AURORA_TEST_CHECK_FALSE(at(runs, 0).paint.bold);
    AURORA_TEST_CHECK_TRUE(at(runs, 1).paint.bold);
    AURORA_TEST_CHECK_EQ(at(runs, 1).first_column, 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 1).last_column, 2U);
    AURORA_TEST_CHECK_EQ(at(runs, 2).first_column, 2U);
}

AURORA_TEST_CASE(wide_continuation_keeps_columns_but_not_text) {
    const auto spec = themed();
    Row row{4U};

    Cell base{};
    base.code_point = U'\x4E2D';  // CJK 双宽码点（非字面量素材）
    base.width = 2U;
    put(row, 0U, base);

    Cell continuation{};
    continuation.flags = static_cast<CellFlags>(kFlagWideContinuation);
    continuation.width = 0U;
    put(row, 1U, continuation);

    put(row, 2U, U'x');

    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    // 延续格不进文本（也不画豆腐块），但列区间仍含它，字形才占得满两格宽。
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 0U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 4U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).text, std::string{"\xE4\xB8\xADx "});
}

AURORA_TEST_CASE(combining_marks_follow_their_base_in_the_same_run) {
    const auto spec = themed();
    Row row{2U};
    put(row, 0U, U'e');
    row.attach_combining(0U, U'\x0301');  // 组合锐音符
    put(row, 1U, U'x');

    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    // 零宽码点拼在基础码点之后随同一段文本送出，由框架 shaping 负责叠字（裁决 7.23ⓑ）。
    AURORA_TEST_CHECK_EQ(at(runs, 0).text, std::string{"e\xCC\x81x"});
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 0U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 2U);
}

AURORA_TEST_CASE(hidden_cells_keep_the_band_and_drop_the_glyphs) {
    auto spec = themed();
    spec.basic[4] = RgbaColor{64U, 64U, 64U};
    Row row{4U};

    Cell concealed{};
    concealed.code_point = U's';
    concealed.flags = kFlagHidden;
    put(row, 0U, concealed);

    Cell banded{};
    banded.code_point = U't';
    banded.flags = kFlagHidden;
    banded.background = 4U;  // 调色板索引 4：底色仍要画，字形不画
    banded.background_source = ColorSource::Palette;
    put(row, 1U, banded);
    put(row, 2U, banded);

    const auto runs = layout_row(row, spec);
    // 不可见 + 默认底色 = 无可画，整段消失；不可见 + 非默认底色只剩色带（文本为空）。
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 3U);
    AURORA_TEST_CHECK_TRUE(at(runs, 0).text.empty());
    AURORA_TEST_CHECK_TRUE(at(runs, 0).paint.hidden);
}

AURORA_TEST_CASE(bare_underline_on_blanks_still_reaches_the_painter) {
    const auto spec = themed();
    Row row{3U};
    Cell ruled{};
    ruled.flags = kFlagUnderline;  // 空格加下划线：无字形但有装饰，段必须留下
    put(row, 0U, ruled);

    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_TRUE(at(runs, 0).text.empty());
    AURORA_TEST_CHECK_TRUE(at(runs, 0).paint.underline);
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 0U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 1U);
}

AURORA_TEST_CASE(plain_background_band_survives_without_text) {
    const auto spec = themed();
    Row row{2U};
    put(row, 1U, red_background(U' '));  // 空格但底色非默认：只剩色带，正是选区高亮的形态

    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 2U);
    AURORA_TEST_CHECK_TRUE(at(runs, 0).text.empty());
}

}  // namespace borealis::test_cases::utest_cell_layout
