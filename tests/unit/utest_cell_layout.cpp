/// 测试类型: unit
/// 目标单元: include/borealis/ui/cell_layout.h + src/ui/cell_layout.cpp
/// 测试说明: 整格像素度量按 scale 换算成 dp 步长（含非整除缩放的取整容差、「装不满也给一格」与
///           不可用度量的空网格）、行列数即尺寸的 UI 侧来源且先扣视口内边距（SPEC.FEAT.XFER.01、
///           裁决 7.25②）、指针 dp 落点折回格子序号（`SPEC.FEAT.INTERACT.02` 的鼠标换算腿：格边界
///           归右下一格、内边距带与越界一律钳到边界格、网格不可用时不定位）、run 按样式全等合并
///           且色带与文本共用同一批切分边界（裁决 7.23②）、
///           双宽延续格不进文本但保留列宽、combining 随基础码点并进同段文本（裁决 7.23ⓑ）、
///           空格与不可见段的丢弃与底色保留、下划线空格段仍需交给绘制侧
///           （SPEC.FEAT.RENDER.01、SPEC.FEAT.TERM.08）、三档下划线与删除线的落笔矩形吸附物理像素
///           且波浪相位跨 run 连续（SPEC.FEAT.RENDER.03、裁决 7.28④）、
///           选中列区间把底色换成传入色并按 `min_contrast` 重合成前景、空区间与二参形态逐字段相同、
///           行尾空白与双宽延续格都随所在区间上底色（SPEC.FEAT.INTERACT.02、裁决 7.38① D1①）。

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
using borealis::grid::kFlagWideContinuation;
using borealis::grid::Row;
using borealis::grid::UnderlineStyle;
using borealis::ui::CellPixels;
using borealis::ui::cell_at_point;
using borealis::ui::contrast_ratio;
using borealis::ui::decoration_rects;
using borealis::ui::GridGeometry;
using borealis::ui::layout_row;
using borealis::ui::LogicalSize;
using borealis::ui::make_geometry;
using borealis::ui::PaletteSpec;
using borealis::ui::rect_for;
using borealis::ui::Rect;
using borealis::ui::RgbaColor;
using borealis::ui::RowSpan;
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

/// @brief 取第 @p index 个矩形；越界返回空矩形，同理让失败落在断言上。
auto rect_at(const std::vector<Rect> &rects, std::size_t index) -> Rect {
    return index < rects.size() ? rects[index] : Rect{};
}

/// @brief 造一段只带下划线档位的 run（装饰线判据只需列区间与笔形）。
auto ruled_run(std::size_t first, std::size_t last, UnderlineStyle style) -> StyleRun {
    StyleRun run{};
    run.first_column = first;
    run.last_column = last;
    run.paint.underline = style;
    return run;
}

/// @brief 本行的选中列区间；行号不参与本件的算术（配对哪一行由调用方决定）。
auto span(std::size_t first, std::size_t last) -> RowSpan {
    return RowSpan{.row = 0U, .first_column = first, .last_column = last};
}

/// @brief 选区底色槽的取值（与主题的 `basic[8]` 拉开一档即可，本件不关心它从哪来）。
constexpr RgbaColor kSelected{40U, 40U, 120U};

/// @brief 8 px × 16 px、基线 12 px 的格在 2× 缩放下即 4 dp × 8 dp、基线 6 dp，1 px = 0.5 dp。
auto square_geometry() -> GridGeometry {
    return make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{800.0, 800.0}, 0.0);
}

/// @brief 同一格度量但带 6 dp 内边距：可画区从 (6, 6) 起，行列数各少一格余量。
auto padded_geometry() -> GridGeometry {
    return make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{800.0, 800.0}, 6.0);
}

}  // namespace

AURORA_TEST_CASE(geometry_divides_pixel_metrics_by_scale) {
    const auto geometry = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{80.0, 80.0}, 0.0);
    AURORA_TEST_CHECK_NEAR(geometry.cell_width, 4.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(geometry.cell_height, 8.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(geometry.ascent, 6.0, 1.0e-9);
    AURORA_TEST_CHECK_EQ(geometry.columns, 20U);
    AURORA_TEST_CHECK_EQ(geometry.rows, 10U);
}

AURORA_TEST_CASE(geometry_counts_whole_cells_and_keeps_at_least_one) {
    const auto geometry = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{85.0, 8.0}, 0.0);
    AURORA_TEST_CHECK_EQ(geometry.columns, 21U);  // 85 dp 装 21 个 4 dp 格，余数不成格
    AURORA_TEST_CHECK_EQ(geometry.rows, 1U);      // 不足一格高仍给一格，否则整屏无处可画
}

AURORA_TEST_CASE(geometry_tolerates_non_terminating_scale) {
    // 7 px 与 14 px 在 1.5× 下都是无限二进制小数，可视宽由同一次除法反乘回来，
    // 比值会差出 1e-15 量级；无容差就少算一整列一整行。
    const auto side = 30.0 * (7.0 / 1.5);
    const auto geometry = make_geometry(CellPixels{7, 14, 11}, 1.5, LogicalSize{side, side}, 0.0);
    AURORA_TEST_CHECK_EQ(geometry.columns, 30U);
    AURORA_TEST_CHECK_EQ(geometry.rows, 15U);
}

AURORA_TEST_CASE(geometry_is_empty_when_metrics_or_viewport_are_unusable) {
    const auto no_font = make_geometry(CellPixels{0, 0, 0}, 1.0, LogicalSize{800.0, 600.0}, 4.0);
    AURORA_TEST_CHECK_EQ(no_font.columns, 0U);
    AURORA_TEST_CHECK_EQ(no_font.rows, 0U);
    AURORA_TEST_CHECK_NEAR(no_font.padding, 4.0, 1.0e-9);  // 无效度量时内边距与缩放仍要留住供排障

    const auto minimized = make_geometry(CellPixels{8, 16, 12}, 1.0, LogicalSize{0.0, 0.0}, 0.0);
    AURORA_TEST_CHECK_EQ(minimized.columns, 0U);
    AURORA_TEST_CHECK_EQ(minimized.rows, 0U);
}

AURORA_TEST_CASE(rect_maps_row_and_column_bounds_to_dp) {
    const auto geometry = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{80.0, 80.0}, 0.0);
    const auto rect = rect_for(geometry, 2U, 1U, 4U);
    AURORA_TEST_CHECK_NEAR(rect.x, 4.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect.y, 16.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect.width, 12.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect.height, 8.0, 1.0e-9);
}

AURORA_TEST_CASE(viewport_padding_shrinks_the_grid_and_shifts_the_origin) {
    // 4 dp × 8 dp 的格放进 80 dp × 80 dp：无内边距是 20×10，四周各扣 8 dp 即 64×64 → 16×8。
    const auto geometry = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{80.0, 80.0}, 8.0);
    AURORA_TEST_CHECK_EQ(geometry.columns, 16U);
    AURORA_TEST_CHECK_EQ(geometry.rows, 8U);
    AURORA_TEST_CHECK_NEAR(geometry.cell_width, 4.0, 1.0e-9);  // 步长不含内边距，只原点含

    const auto rect = rect_for(geometry, 0U, 0U, 1U);
    AURORA_TEST_CHECK_NEAR(rect.x, 8.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect.y, 8.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect.width, 4.0, 1.0e-9);
}

AURORA_TEST_CASE(padding_that_leaves_no_room_gives_an_empty_grid) {
    // 内边距吃掉整块可视区时不硬塞一格：那一格会画到窗口外，与「窗口最小化」同形。
    const auto geometry = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{80.0, 80.0}, 40.0);
    AURORA_TEST_CHECK_EQ(geometry.columns, 0U);
    AURORA_TEST_CHECK_EQ(geometry.rows, 0U);
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
    ruled.underline = UnderlineStyle::Curly;  // 空格加下划线：无字形但有装饰，段必须留下
    put(row, 0U, ruled);

    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_TRUE(at(runs, 0).text.empty());
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(at(runs, 0).paint.underline),
                         static_cast<std::uint32_t>(UnderlineStyle::Curly));
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 0U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 1U);
}

AURORA_TEST_CASE(underline_style_change_splits_the_row) {
    const auto spec = themed();
    Row row{3U};
    Cell single{};
    single.code_point = U'a';
    single.underline = UnderlineStyle::Single;
    Cell doubled{};
    doubled.code_point = U'b';
    doubled.underline = UnderlineStyle::Double;
    put(row, 0U, single);
    put(row, 1U, single);
    put(row, 2U, doubled);

    // 三档可辨的前提是档位参与合并判据：单线与双线同色同字体也不得并进一段（裁决 7.28）。
    const auto runs = layout_row(row, spec);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 2U);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(at(runs, 0).paint.underline),
                         static_cast<std::uint32_t>(UnderlineStyle::Single));
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 2U);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(at(runs, 1).paint.underline),
                         static_cast<std::uint32_t>(UnderlineStyle::Double));
    AURORA_TEST_CHECK_EQ(at(runs, 1).first_column, 2U);
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

AURORA_TEST_CASE(selected_span_recolors_its_columns_and_splits_the_run) {
    const auto spec = themed();
    Row row{5U};
    put(row, 0U, U'a');
    put(row, 1U, U'b');
    put(row, 2U, red_background(U'c'));  // SGR 底色在选中段被选区槽整体盖掉（裁决 7.38① D1①）
    put(row, 3U, red_background(U'd'));
    put(row, 4U, U'e');

    const auto runs = layout_row(row, spec, span(1U, 4U), kSelected);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 3U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 1).first_column, 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 1).last_column, 4U);  // 三段同底色并成一跑，切分边界仍是列区间
    AURORA_TEST_CHECK_EQ(at(runs, 2).first_column, 4U);
    AURORA_TEST_CHECK_EQ(at(runs, 2).last_column, 5U);

    AURORA_TEST_CHECK_EQ(at(runs, 1).paint.background, kSelected);
    AURORA_TEST_CHECK_EQ(at(runs, 0).paint.background, spec.default_background);
    AURORA_TEST_CHECK_EQ(at(runs, 2).paint.background, spec.default_background);
    AURORA_TEST_CHECK_EQ(at(runs, 0).text, std::string{"a"});
    AURORA_TEST_CHECK_EQ(at(runs, 1).text, std::string{"bcd"});  // 文本不受底色替换影响
    AURORA_TEST_CHECK_EQ(at(runs, 2).text, std::string{"e"});
}

AURORA_TEST_CASE(empty_selection_span_is_the_unselected_row) {
    // 末行区间 `[0, 0)` 是真实形态：焦点落在下一行第 0 列时该行没有选中格。
    const auto spec = themed();
    Row row{4U};
    put(row, 0U, U'a');
    put(row, 1U, red_background(U'b'));
    put(row, 2U, U'c');
    put(row, 3U, U'd');

    const auto plain = layout_row(row, spec);
    const auto selected = layout_row(row, spec, span(2U, 2U), kSelected);
    AURORA_TEST_REQUIRE_EQ(selected.size(), plain.size());
    for (std::size_t index = 0; index < plain.size(); ++index) {
        AURORA_TEST_CHECK_EQ(at(selected, index).first_column, at(plain, index).first_column);
        AURORA_TEST_CHECK_EQ(at(selected, index).last_column, at(plain, index).last_column);
        AURORA_TEST_CHECK_EQ(at(selected, index).text, at(plain, index).text);
        AURORA_TEST_CHECK_EQ(at(selected, index).paint, at(plain, index).paint);
    }
}

AURORA_TEST_CASE(selection_band_covers_the_trailing_blanks_of_the_row) {
    // 中间行的区间是整行，行尾空白格也要上底色——高亮是一块矩形而不是逐字形点缀。
    const auto spec = themed();
    Row row{4U};
    put(row, 0U, U'a');

    const auto runs = layout_row(row, spec, span(0U, 4U), kSelected);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).first_column, 0U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 4U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).paint.background, kSelected);
    AURORA_TEST_CHECK_EQ(at(runs, 0).text, std::string{"a   "});
}

AURORA_TEST_CASE(min_contrast_recomposes_the_foreground_only_inside_the_selection) {
    auto spec = themed();
    const RgbaColor same_as_text{255U, 255U, 255U};  // 选中底 == 默认前景：不重合成就看不见
    Row row{2U};
    put(row, 0U, U'a');
    put(row, 1U, U'b');

    spec.min_contrast_enabled = false;
    const auto off = layout_row(row, spec, span(0U, 2U), same_as_text);
    AURORA_TEST_REQUIRE_EQ(off.size(), 1U);
    AURORA_TEST_CHECK_EQ(at(off, 0).paint.background, same_as_text);
    AURORA_TEST_CHECK_EQ(at(off, 0).paint.foreground, spec.default_foreground);  // 开关关着时前景不动

    spec.min_contrast_enabled = true;
    spec.min_contrast = 4.5;
    const auto on = layout_row(row, spec, span(0U, 1U), same_as_text);
    AURORA_TEST_REQUIRE_EQ(on.size(), 2U);  // 只有第 0 列被重合成，两段前景不同色故切开
    AURORA_TEST_CHECK_NE(at(on, 0).paint.foreground, spec.default_foreground);
    AURORA_TEST_CHECK_GE(contrast_ratio(at(on, 0).paint.foreground, same_as_text), 4.5 - 1.0e-9);
    AURORA_TEST_CHECK_EQ(at(on, 1).paint.foreground, spec.default_foreground);  // 未选段仍是默认前景
}

AURORA_TEST_CASE(wide_base_and_continuation_inside_a_selection_stay_one_run) {
    const auto spec = themed();
    Row row{3U};

    Cell base{};
    base.code_point = U'\x4E2D';
    base.width = 2U;
    put(row, 0U, base);

    Cell continuation{};
    continuation.flags = static_cast<CellFlags>(kFlagWideContinuation);
    continuation.width = 0U;
    put(row, 1U, continuation);

    put(row, 2U, U'x');

    const auto runs = layout_row(row, spec, span(0U, 2U), kSelected);
    AURORA_TEST_REQUIRE_EQ(runs.size(), 2U);
    AURORA_TEST_CHECK_EQ(at(runs, 0).last_column, 2U);  // 延续格跟基础格同段，双宽字形整块上底色
    AURORA_TEST_CHECK_EQ(at(runs, 0).paint.background, kSelected);
    AURORA_TEST_CHECK_EQ(at(runs, 0).text, std::string{"\xE4\xB8\xAD"});
    AURORA_TEST_CHECK_EQ(at(runs, 1).paint.background, spec.default_background);
}

AURORA_TEST_CASE(single_underline_is_one_physical_pixel_below_the_baseline) {
    const auto geometry = square_geometry();  // 基线 6 dp、1 px = 0.5 dp
    const auto rects = decoration_rects(geometry, 0U, ruled_run(1U, 3U, UnderlineStyle::Single));
    AURORA_TEST_REQUIRE_EQ(rects.size(), 1U);
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 0).x, 4.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 0).y, 6.5, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 0).width, 8.0, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 0).height, 0.5, 1.0e-9);
}

AURORA_TEST_CASE(double_underline_is_two_one_pixel_lines_with_a_one_pixel_gap) {
    const auto geometry = square_geometry();
    const auto rects = decoration_rects(geometry, 0U, ruled_run(0U, 2U, UnderlineStyle::Double));
    AURORA_TEST_REQUIRE_EQ(rects.size(), 2U);
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 0).y, 6.5, 1.0e-9);  // 基线 +1px
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 1).y, 7.5, 1.0e-9);  // 基线 +3px，间距与线宽同为 1px
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 0).height, 0.5, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 1).height, 0.5, 1.0e-9);
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 1).y + rect_at(rects, 1).height, 8.0, 1.0e-9);  // 不越出格底
}

AURORA_TEST_CASE(curly_underline_oscillates_once_per_four_pixels) {
    const auto geometry = square_geometry();
    const auto rects = decoration_rects(geometry, 0U, ruled_run(0U, 1U, UnderlineStyle::Curly));
    AURORA_TEST_REQUIRE_EQ(rects.size(), 8U);  // 一格 8 物理像素，逐像素列一段
    const std::array<double, 4> expected{7.0, 7.5, 7.0, 6.5};
    for (std::size_t index = 0; index < rects.size(); ++index) {
        AURORA_TEST_CHECK_NEAR(rect_at(rects, index).x, static_cast<double>(index) * 0.5, 1.0e-9);
        AURORA_TEST_CHECK_NEAR(rect_at(rects, index).width, 0.5, 1.0e-9);
        AURORA_TEST_CHECK_NEAR(rect_at(rects, index).height, 0.5, 1.0e-9);
        AURORA_TEST_CHECK_NEAR(rect_at(rects, index).y, expected[index % 4U], 1.0e-9);
    }
}

AURORA_TEST_CASE(curly_phase_anchored_to_the_row_not_the_run) {
    // 7 px 宽的格在 1.5× 下不是 4 的整数倍，列边界处相位会推进；锚在 run 左沿就让
    // 波形在样式切换处抖动一下（判据见裁决 7.28④）。
    const auto geometry = make_geometry(CellPixels{7, 14, 11}, 1.5, LogicalSize{800.0, 800.0}, 0.0);
    const auto first = decoration_rects(geometry, 0U, ruled_run(0U, 1U, UnderlineStyle::Curly));
    const auto second = decoration_rects(geometry, 0U, ruled_run(1U, 2U, UnderlineStyle::Curly));
    AURORA_TEST_REQUIRE_EQ(first.size(), 7U);
    AURORA_TEST_REQUIRE_EQ(second.size(), 7U);
    // 基线 = 11 px；波浪中心 = 基线 +2px，相位 0..3 的偏移为 0、+1、0、−1。
    // 基线 11 px，波浪中心 = 基线 +2px = 13 px。
    AURORA_TEST_CHECK_NEAR(rect_at(first, 0).y, 13.0 / 1.5, 1.0e-9);   // 相位 0 → 中心
    AURORA_TEST_CHECK_NEAR(rect_at(first, 6).y, 13.0 / 1.5, 1.0e-9);   // 相位 2 → 中心
    AURORA_TEST_CHECK_NEAR(rect_at(second, 0).y, 12.0 / 1.5, 1.0e-9);  // 相位 3 → 中心 −1px，接着上一段推进
    AURORA_TEST_CHECK_NEAR(rect_at(second, 1).y, 13.0 / 1.5, 1.0e-9);  // 回相位 0
}

AURORA_TEST_CASE(decoration_edges_snap_to_physical_pixels) {
    const auto geometry = make_geometry(CellPixels{7, 14, 11}, 1.5, LogicalSize{800.0, 800.0}, 4.0);
    std::vector<StyleRun> runs{ruled_run(1U, 4U, UnderlineStyle::Single),
                               ruled_run(1U, 4U, UnderlineStyle::Curly)};
    runs[1].paint.strike = true;
    for (const auto &run : runs) {
        for (const auto &rect : decoration_rects(geometry, 3U, run)) {
            // 1px 规则落在小数 dp 上会被抗锯齿糊成发灰的 2px，故四边必须整像素（视觉稿 D4①）。
            AURORA_TEST_CHECK_NEAR(rect.x * 1.5, std::round(rect.x * 1.5), 1.0e-9);
            AURORA_TEST_CHECK_NEAR(rect.y * 1.5, std::round(rect.y * 1.5), 1.0e-9);
            AURORA_TEST_CHECK_NEAR(rect.width * 1.5, std::round(rect.width * 1.5), 1.0e-9);
            AURORA_TEST_CHECK_NEAR(rect.height * 1.5, 1.0, 1.0e-9);
        }
    }
}

AURORA_TEST_CASE(strike_runs_through_the_middle_of_the_line_box) {
    const auto geometry = square_geometry();
    auto run = ruled_run(0U, 2U, UnderlineStyle::Single);
    run.paint.strike = true;
    const auto rects = decoration_rects(geometry, 1U, run);
    AURORA_TEST_REQUIRE_EQ(rects.size(), 2U);  // 一行同时有下划线与删除线：两段各一笔
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 0).y, 14.5, 1.0e-9);  // 行顶 8 + 基线 6 + 1px
    AURORA_TEST_CHECK_NEAR(rect_at(rects, 1).y, 11.0, 1.0e-9);  // 行顶 8 + 基线 × 0.5
}

AURORA_TEST_CASE(plain_and_hidden_runs_produce_no_decoration) {
    const auto geometry = square_geometry();
    AURORA_TEST_CHECK_TRUE(decoration_rects(geometry, 0U, ruled_run(0U, 2U, UnderlineStyle::None)).empty());

    auto concealed = ruled_run(0U, 1U, UnderlineStyle::Single);
    concealed.paint.strike = true;
    concealed.paint.hidden = true;  // `SGR 8` 只隐去字形与装饰，底色色带另由绘制侧铺
    AURORA_TEST_CHECK_TRUE(decoration_rects(geometry, 0U, concealed).empty());
}

AURORA_TEST_CASE(pointer_hits_the_cell_that_covers_it) {
    const auto geometry = square_geometry();  // 4 dp × 8 dp，无内边距

    const auto first = cell_at_point(geometry, 3.99, 7.99);
    AURORA_TEST_REQUIRE_TRUE(first.has_value());
    AURORA_TEST_CHECK_EQ(first->row, 0U);
    AURORA_TEST_CHECK_EQ(first->column, 0U);

    // 格边界归右/下那一格：它是那一格的第一个像素，不是上一格的最后一个。
    const auto boundary = cell_at_point(geometry, 4.0, 8.0);
    AURORA_TEST_REQUIRE_TRUE(boundary.has_value());
    AURORA_TEST_CHECK_EQ(boundary->row, 1U);
    AURORA_TEST_CHECK_EQ(boundary->column, 1U);

    const auto inside = cell_at_point(geometry, 13.5, 27.0);
    AURORA_TEST_REQUIRE_TRUE(inside.has_value());
    AURORA_TEST_CHECK_EQ(inside->row, 3U);
    AURORA_TEST_CHECK_EQ(inside->column, 3U);
}

AURORA_TEST_CASE(pointer_in_the_padding_band_snaps_to_the_origin_cell) {
    const auto geometry = padded_geometry();  // 原点 (6, 6)，格仍是 4 dp × 8 dp

    const auto corner = cell_at_point(geometry, 2.0, 2.0);
    AURORA_TEST_REQUIRE_TRUE(corner.has_value());
    AURORA_TEST_CHECK_EQ(corner->row, 0U);
    AURORA_TEST_CHECK_EQ(corner->column, 0U);

    // 内边距只移原点，不折进格子步长（裁决 7.25②），故 (10, 14) 已在第 1 格内。
    const auto next = cell_at_point(geometry, 10.0, 14.0);
    AURORA_TEST_REQUIRE_TRUE(next.has_value());
    AURORA_TEST_CHECK_EQ(next->row, 1U);
    AURORA_TEST_CHECK_EQ(next->column, 1U);
}

AURORA_TEST_CASE(pointer_outside_the_grid_extends_the_hit_to_the_edge_cell) {
    // 指针捕获下拖出窗口仍持续收到 Move 事件（框架在 Press 时 `SetCapture`），此刻要的是
    // 「选区继续长到边界」，故四个方向一律钳位而非丢事件。
    const auto geometry = square_geometry();  // 200 列 × 100 行

    const auto beyond_left = cell_at_point(geometry, -40.0, -120.0);
    AURORA_TEST_REQUIRE_TRUE(beyond_left.has_value());
    AURORA_TEST_CHECK_EQ(beyond_left->row, 0U);
    AURORA_TEST_CHECK_EQ(beyond_left->column, 0U);

    const auto beyond_right = cell_at_point(geometry, 1000000.0, 1000000.0);
    AURORA_TEST_REQUIRE_TRUE(beyond_right.has_value());
    AURORA_TEST_CHECK_EQ(beyond_right->row, geometry.rows - 1U);
    AURORA_TEST_CHECK_EQ(beyond_right->column, geometry.columns - 1U);
}

AURORA_TEST_CASE(unusable_grid_hits_no_cell) {
    // 度量为 0（字体未就绪）与可视区为 0（窗口最小化）都是真实输入，此刻没有可落点的网格。
    const auto no_metrics = make_geometry(CellPixels{0, 0, 0}, 2.0, LogicalSize{800.0, 800.0}, 0.0);
    AURORA_TEST_CHECK_FALSE(cell_at_point(no_metrics, 10.0, 10.0).has_value());

    const auto no_viewport = make_geometry(CellPixels{8, 16, 12}, 2.0, LogicalSize{0.0, 0.0}, 0.0);
    AURORA_TEST_CHECK_FALSE(cell_at_point(no_viewport, 10.0, 10.0).has_value());
}

}  // namespace borealis::test_cases::utest_cell_layout
