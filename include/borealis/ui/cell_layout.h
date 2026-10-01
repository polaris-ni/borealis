#pragma once

// ============================================================
// 整格几何与行内 run 切分（include/borealis/ui/cell_layout.h）
// ------------------------------------------------------------
// 架构 §9.2 的取用形态细则：框架只收「合成后的最终值」，列起点、跨格与切分边界都在本层。
// 格宽取自 `render::FontEngine::monospace_cell`（物理像素），而 `Painter` 的几何是逻辑 dp，
// 故这里唯一的算术是 ÷ scale；把它放在无框架依赖的一层，是为了让 dp/px 这条最容易算错的
// 换算能进单测（AGENTS.md §4.4 第 20 条）。
//
// run 切分按裁决 7.23②「样式全等合并到行」：一行内相邻格的前景/背景/字体三者全等才并入同
// 一片段，**色带矩形与文本片段共用同一批切分边界**，故本层只交出一张 run 表，不再另存色带表。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "borealis/grid/row.h"
#include "borealis/ui/palette.h"

namespace borealis::ui {

/// @brief 单格的物理像素度量：直接取自框架 `render::CellMetrics` 的三个整数字段。
///
/// 刻意不引用框架类型：`include/borealis/` 的公共头不得含 Aurora 头，互转只发生在
/// `src/ui/terminal_view.cpp` 一个翻译单元内。
struct CellPixels {
    std::int32_t width_px = 0;   ///< 单格推进宽度。
    std::int32_t height_px = 0;  ///< 单格行高。
    std::int32_t ascent_px = 0;  ///< 行盒顶 → 基线。
};

/// @brief 可视区的逻辑尺寸（dp），由控件的绘制盒给出。
struct LogicalSize {
    double width = 0.0;
    double height = 0.0;
};

/// @brief 一帧有效的网格几何：dp 步长与由它们能装下的行列数。
struct GridGeometry {
    std::size_t columns = 0;
    std::size_t rows = 0;
    double cell_width = 0.0;   ///< 单格宽（dp）。
    double cell_height = 0.0;  ///< 单格高（dp）。
    double ascent = 0.0;       ///< 行盒顶 → 基线（dp），下划线与光标块的落笔基准。
};

/// @brief 一段矩形（逻辑 dp），与框架 `Rect` 逐字段对应。
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

/// @brief 由整格像素度量、缩放与可视区尺寸推出这一帧的网格几何。
///
/// 行列数按**向下取整**（装不下一整格就不画它），至少给 1×1；`SPEC.FEAT.XFER.01` 的
/// 「尺寸的 UI 侧来源」就是这个数——它交给 `Session::resize` 前须经工作区层的去抖合并。
/// @param metrics 单格物理像素度量。
/// @param scale device pixel ratio（物理像素 / 逻辑 dp）。
/// @param viewport 可视区逻辑尺寸（dp）。
/// @return 该帧的网格几何；度量为 0 或尺寸为 0（窗口最小化是真实输入）时行列数为 0。
[[nodiscard]] auto make_geometry(const CellPixels &metrics, double scale, const LogicalSize &viewport) noexcept
    -> GridGeometry;

/// @brief 第 @p row 行、`[first_column, last_column)` 列区间的矩形（逻辑 dp）。
///
/// 行号是**绘制行号**（视口内 0 基）：滚动偏移由调用方在取行时折算，几何本身不含历史行。
/// @param geometry 本帧网格几何。
/// @param row 绘制行号。
/// @param first_column 区间左界（含）。
/// @param last_column 区间右界（不含）。
/// @return 覆盖该行该列区间的矩形，高度为整格高。
[[nodiscard]] auto rect_for(const GridGeometry &geometry, std::size_t row, std::size_t first_column,
                            std::size_t last_column) noexcept -> Rect;

/// @brief 一段样式全等的行内区间：色带、字形与装饰共用它的边界。
///
/// `text` 是 UTF-8 原文（框架 `TextRun.text` 收的正是这个编码，故绘制侧不再二次编码）；
/// 空串表示这段没有可绘字形——可能只是色带，也可能整段被 `hidden` 吞掉。
/// 双宽字符的延续格不进文本，但**留在区间里**，字形因此占满两格的宽度。
struct StyleRun {
    std::size_t first_column = 0;
    std::size_t last_column = 0;  ///< 闭开区间的右界。
    std::string text;
    CellPaint paint{};
};

/// @brief 把一行切成按样式全等合并的 run 表（既供色带矩形，也供文本片段）。
///
/// 什么都不用画的区间不出现在结果里：文本全空白、无下划线与删除线、且底色等于主题默认底色。
/// 于是终端最常见的一屏（绝大多数格是默认底色的空格）在这里就收敛成极短的表。
/// @param row 网格中的一行。
/// @param spec 调色板配置。
/// @return 列号升序的 run 表。
[[nodiscard]] auto layout_row(const grid::Row &row, const PaletteSpec &spec) -> std::vector<StyleRun>;

}  // namespace borealis::ui
