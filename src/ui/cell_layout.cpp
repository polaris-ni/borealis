// ============================================================
// 整格几何与行内 run 切分（src/ui/cell_layout.cpp）
// ------------------------------------------------------------
// 本文件只做四件算得清的事：像素格度量 ÷ scale 换成 dp 步长、行高与字距量化成整数物理像素、
// 指针 dp 落点折回格子序号，以及按样式全等把一行切成 run。不含框架类型，故 dp/px 换算与切分
// 边界都能全量单测（架构 §9.2）。
// ============================================================

#include "borealis/ui/cell_layout.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include "borealis/term/utf8.h"
#include "borealis/ui/palette.h"

namespace borealis::ui {
namespace {

/// @brief 除法取整的容差：`viewport_width / (px / scale)` 在 1.5× 这类不可精确表示的缩放下
///        会差出 1e-14 量级，直接 floor 会少算一整列。
constexpr double kDivisionTolerance = 1.0e-9;

/// @brief 可视尺寸能装下的整格数：装不满一格也给一格，尺寸为 0 才是 0。
[[nodiscard]] auto cell_count(double available, double step) noexcept -> std::size_t {
    if (available <= 0.0 || step <= 0.0) {
        return 0U;
    }
    const auto fitted = static_cast<std::size_t>(std::floor(available / step + kDivisionTolerance));
    return fitted == 0U ? 1U : fitted;
}

/// @brief 这段 run 是否需要画笔：文本、装饰、色带三者至少占一样。
///
/// 默认底色不必铺色带——绘制侧的清屏已经把整屏刷成它了，再铺一遍是纯浪费。
[[nodiscard]] auto worth_painting(const StyleRun &run, bool has_glyph, const PaletteSpec &spec) noexcept -> bool {
    if (run.paint.hidden) {
        return run.paint.background != spec.default_background;
    }
    if (run.paint.underline != grid::UnderlineStyle::None || run.paint.strike) {
        return true;
    }
    return has_glyph || run.paint.background != spec.default_background;
}

/// @brief 波浪线一个周期（4 物理像素）内的纵向偏移，以「基线 +2px」为中心上下各偏 1px。
constexpr std::array<int, 4> kCurlyOffsets{0, 1, 0, -1};

/// @brief 把 dp 折成物理像素并取整，作为落笔的像素格坐标。
[[nodiscard]] auto to_pixel(double value_dp, double scale) noexcept -> std::int64_t {
    return std::lround(value_dp * scale);
}

/// @brief 由物理像素格坐标还原矩形（逻辑 dp），四边因此严格落在像素边界上。
[[nodiscard]] auto rect_from_pixel(std::int64_t x_px, std::int64_t y_px, std::int64_t width_px,
                                   std::int64_t height_px, double scale) noexcept -> Rect {
    return Rect{static_cast<double>(x_px) / scale, static_cast<double>(y_px) / scale,
                static_cast<double>(width_px) / scale, static_cast<double>(height_px) / scale};
}

/// @brief 沿一个轴取格子序号：先 floor，再钳进 `[0, count - 1]`。
/// @note 钳位必须发生在转成无符号**之前**：拖出窗口左/上沿给的是负 dp，负浮点转 `std::size_t`
///       是未定义行为，而不是「回绕成一个很大的列号」那种看似能跑的巧合。
[[nodiscard]] auto cell_index(double offset_dp, double step_dp, std::size_t count) noexcept -> std::size_t {
    const auto raw = std::floor(offset_dp / step_dp);
    if (raw < 0.0) {
        return 0U;
    }
    const auto last = static_cast<double>(count - 1U);
    return raw > last ? count - 1U : static_cast<std::size_t>(raw);
}

/// @brief run 切分的唯一实现；@p selection 有值时该列区间的底色换成 @p selected_background。
///
/// 底色替换发生在 `resolve` **之后**：一格属不属于选区是区间级事实而非该格的事实（裁决 7.32②），
/// 而色值合成链（亮色档 → 暗淡 → 反色 → 最小对比度）只认该格自己。于是选中段的前景只在开了
/// 最小对比度时才按新底色重合成，未选中段逐位等于无选区形态。
std::vector<StyleRun> layout_selected(const grid::Row &row, const PaletteSpec &spec,
                                      const std::optional<RowSpan> &selection,
                                      const RgbaColor &selected_background) {
    std::vector<StyleRun> runs;
    StyleRun current{};
    bool building = false;
    bool has_glyph = false;  ///< 区间内出现过非空白码点：全空白的段不必交文本给绘制侧。

    const auto flush = [&]() -> void {
        if (!building) {
            return;
        }
        if (!has_glyph || current.paint.hidden) {
            current.text.clear();
        }
        if (worth_painting(current, has_glyph, spec)) {
            runs.push_back(std::move(current));
        }
        current = StyleRun{};
        building = false;
        has_glyph = false;
    };

    for (std::size_t column = 0; column < row.columns(); ++column) {
        const auto &cell = row.cell(column);
        auto paint = resolve(cell, spec);
        if (selection.has_value() && column >= selection->first_column && column < selection->last_column) {
            paint.background = selected_background;
            if (spec.min_contrast_enabled) {
                paint.foreground = enforce_contrast(paint.foreground, paint.background, spec.min_contrast);
            }
        }
        if (building && paint == current.paint) {
            current.last_column = column + 1U;
        } else {
            flush();
            current.paint = paint;
            current.first_column = column;
            current.last_column = column + 1U;
            building = true;
        }
        if (cell.is_wide_continuation()) {
            continue;  // 延续格不承载字符：文本里既没有它也不画豆腐块，宽度由基础格那一段占住
        }
        static_cast<void>(term::append_utf8(cell.code_point, current.text));
        has_glyph = has_glyph || cell.code_point != U' ';
        for (const auto &mark : row.combining(column)) {
            static_cast<void>(term::append_utf8(mark.code_point, current.text));
        }
    }
    flush();
    return runs;
}

}  // namespace

auto make_geometry(const CellPixels &metrics, double scale, const LogicalSize &viewport,
                   double padding_dp) noexcept -> GridGeometry {
    GridGeometry geometry{};
    geometry.scale = scale;
    geometry.padding = padding_dp;
    if (metrics.width_px <= 0 || metrics.height_px <= 0 || scale <= 0.0) {
        return geometry;  // 度量取不到（字体未就绪）时整屏无格可画，行列数 0 让调用方跳过这一帧
    }
    geometry.cell_width = static_cast<double>(metrics.width_px) / scale;
    geometry.cell_height = static_cast<double>(metrics.height_px) / scale;
    geometry.ascent = static_cast<double>(metrics.ascent_px) / scale;
    // 内边距占掉的边带不能折成行列数，否则最外一格会画到窗口之外（裁决 7.25②）。
    geometry.columns = cell_count(viewport.width - 2.0 * padding_dp, geometry.cell_width);
    geometry.rows = cell_count(viewport.height - 2.0 * padding_dp, geometry.cell_height);
    return geometry;
}

auto apply_typography(const CellPixels &font, double scale, const Typography &typography) noexcept -> TypedMetrics {
    TypedMetrics typed{};
    if (scale <= 0.0) {
        return typed;  // 缩放不可用则整格度量为全零，与「字体未就绪」同形（`make_geometry` 据此出空网格）
    }
    // 字距进列步长前先吸附整数物理像素：框架按 `dp × scale` 在每对相邻字形之间加一次，
    // 非整数就会让第 k 个字形偏离第 k 列左沿（回填值因此是整数像素 ÷ scale 而非配置原值）。
    const auto spacing_px = static_cast<std::int32_t>(to_pixel(typography.letter_spacing_dp, scale));
    typed.letter_spacing_dp = static_cast<double>(spacing_px) / scale;
    typed.cells.width_px = font.width_px + spacing_px;

    const auto row_step_px = static_cast<std::int32_t>(std::lround(static_cast<double>(font.height_px) *
                                                                   typography.line_height));
    const auto leading_px = row_step_px - font.height_px;
    typed.glyph_top_px = leading_px / 2;  // 余数归行盒下沿：色带铺满整行，下沿多一像素不露缝
    typed.cells.height_px = row_step_px;
    typed.cells.ascent_px = font.ascent_px + typed.glyph_top_px;
    return typed;
}

auto rect_for(const GridGeometry &geometry, std::size_t row, std::size_t first_column,
              std::size_t last_column) noexcept -> Rect {
    const auto left = geometry.padding + static_cast<double>(first_column) * geometry.cell_width;
    const auto right = geometry.padding + static_cast<double>(last_column) * geometry.cell_width;
    return Rect{left, geometry.padding + static_cast<double>(row) * geometry.cell_height, right - left,
                geometry.cell_height};
}

auto cell_at_point(const GridGeometry &geometry, double x, double y) noexcept -> std::optional<GridCellPos> {
    // 行列数与步长任一为 0 就没有可定位的格子：这既是窗口最小化的真实形态，也让下面的除法不成立。
    if (geometry.columns == 0U || geometry.rows == 0U || geometry.cell_width <= 0.0 || geometry.cell_height <= 0.0) {
        return std::nullopt;
    }
    return GridCellPos{.row = cell_index(y - geometry.padding, geometry.cell_height, geometry.rows),
                       .column = cell_index(x - geometry.padding, geometry.cell_width, geometry.columns)};
}

auto decoration_rects(const GridGeometry &geometry, std::size_t row, const StyleRun &run) -> std::vector<Rect> {
    std::vector<Rect> rects;
    if (run.paint.hidden || run.last_column <= run.first_column) {
        return rects;  // `SGR 8` 只隐去字形与装饰，底色色带仍由绘制侧铺
    }
    const auto scale = geometry.scale;
    const auto band = rect_for(geometry, row, run.first_column, run.last_column);
    const auto left_px = to_pixel(band.x, scale);
    const auto right_px = to_pixel(band.x + band.width, scale);
    const auto width_px = right_px - left_px;
    if (width_px <= 0) {
        return rects;
    }
    const auto baseline_px = to_pixel(band.y + geometry.ascent, scale);

    switch (run.paint.underline) {
        case grid::UnderlineStyle::None:
            break;
        case grid::UnderlineStyle::Single:
            rects.push_back(rect_from_pixel(left_px, baseline_px + 1, width_px, 1, scale));
            break;
        case grid::UnderlineStyle::Double:
            rects.push_back(rect_from_pixel(left_px, baseline_px + 1, width_px, 1, scale));
            rects.push_back(rect_from_pixel(left_px, baseline_px + 3, width_px, 1, scale));
            break;
        case grid::UnderlineStyle::Curly:
            // 相位锚在**不含内边距**的网格像素 x：同一行里相邻 run 因此波形连续，
            // 而 run 的切分边界随样式而变，锚在 run 左沿就会让波浪在边界处抖动。
            const auto phase_base = to_pixel(static_cast<double>(run.first_column) * geometry.cell_width, scale);
            for (std::int64_t offset = 0; offset < width_px; ++offset) {
                const auto phase = static_cast<std::size_t>((phase_base + offset) % 4);
                rects.push_back(
                    rect_from_pixel(left_px + offset, baseline_px + 2 + kCurlyOffsets[phase], 1, 1, scale));
            }
            break;
    }
    if (run.paint.strike) {
        const auto strike_px = to_pixel(band.y + geometry.ascent * 0.5, scale);
        rects.push_back(rect_from_pixel(left_px, strike_px, width_px, 1, scale));
    }
    return rects;
}

auto layout_row(const grid::Row &row, const PaletteSpec &spec) -> std::vector<StyleRun> {
    return layout_selected(row, spec, std::nullopt, RgbaColor{});
}

auto layout_row(const grid::Row &row, const PaletteSpec &spec, const RowSpan &selection,
                const RgbaColor &selected_background) -> std::vector<StyleRun> {
    return layout_selected(row, spec, selection, selected_background);
}

}  // namespace borealis::ui
