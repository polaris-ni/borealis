// ============================================================
// 整格几何与行内 run 切分（src/ui/cell_layout.cpp）
// ------------------------------------------------------------
// 本文件只做两件算得清的事：像素格度量 ÷ scale 换成 dp 步长，以及按样式全等把一行切成 run。
// 不含框架类型，故 dp/px 换算与切分边界都能全量单测（架构 §9.2）。
// ============================================================

#include "borealis/ui/cell_layout.h"

#include <cmath>
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
    if (run.paint.underline || run.paint.strike) {
        return true;
    }
    return has_glyph || run.paint.background != spec.default_background;
}

}  // namespace

auto make_geometry(const CellPixels &metrics, double scale, const LogicalSize &viewport) noexcept -> GridGeometry {
    GridGeometry geometry{};
    if (metrics.width_px <= 0 || metrics.height_px <= 0 || scale <= 0.0) {
        return geometry;  // 度量取不到（字体未就绪）时整屏无格可画，行列数 0 让调用方跳过这一帧
    }
    geometry.cell_width = static_cast<double>(metrics.width_px) / scale;
    geometry.cell_height = static_cast<double>(metrics.height_px) / scale;
    geometry.ascent = static_cast<double>(metrics.ascent_px) / scale;
    geometry.columns = cell_count(viewport.width, geometry.cell_width);
    geometry.rows = cell_count(viewport.height, geometry.cell_height);
    return geometry;
}

auto rect_for(const GridGeometry &geometry, std::size_t row, std::size_t first_column,
              std::size_t last_column) noexcept -> Rect {
    const auto left = static_cast<double>(first_column) * geometry.cell_width;
    const auto right = static_cast<double>(last_column) * geometry.cell_width;
    return Rect{left, static_cast<double>(row) * geometry.cell_height, right - left, geometry.cell_height};
}

auto layout_row(const grid::Row &row, const PaletteSpec &spec) -> std::vector<StyleRun> {
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
        const auto paint = resolve(cell, spec);
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

}  // namespace borealis::ui
