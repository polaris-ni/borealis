#pragma once

// ============================================================
// 命令块的界面折算（include/borealis/ui/command_block.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.01` 的三层界面消费（复制输出、失败底色、悬停分隔线）共用的那半「算得清」：
// 把块的**绝对行**折算成权威网格的存储行、屏幕行与逐行文本区间。块表本身存绝对行
// （`term::CommandBlock` 的坐标约定，`storage_index + dropped_lines`），而存储行号随
// scrollback 溢出整体前移、屏幕行号随回看偏移改变，故每次消费都现折算一次——任何一档行号
// 都不进块表，也就不存在「两份行号需要同步」的分叉。
//
// 不含 Aurora / `config` 类型（AGENTS §4.4 第 20 条）：输出区间、失败行与复制文本都能脱开
// 界面全量单测。绘制与指针只负责把 `ScreenMirror` 带出的 `dropped_lines` / `window_top`
// 喂进来，再按返回的行号落笔或命中。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <vector>

#include "borealis/grid/storage.h"
#include "borealis/term/command_block.h"
#include "borealis/ui/selection.h"

namespace borealis::ui {

/// @brief 一段连续的屏幕行（闭开区间 `[first_row, last_row)`），0 = 可见窗顶。
///
/// 只有 `failed_output_spans` 产出本类型：失败底色要的是「画哪几行」，而复制用的是逐行列区间
/// （`RowSpan`）。两者分开是为了让行号空间在类型上就不混：存储行与屏幕行的换算只在产出点发生。
struct BlockRowRange {
    std::size_t first_row = 0;
    std::size_t last_row = 0;  ///< 不含：空区间即 `last_row <= first_row`。

    /// @brief 逐字段全等比较。
    [[nodiscard]] auto operator==(const BlockRowRange &) const noexcept -> bool = default;
};

/// @brief 把块的 `C → D` 输出区间折成按行号升序的存储行区间表（复制用）。
///
/// 三条边界（裁决 7.123 D4①）：① 首行从 `C` 的列起算，中间行整行；② `D` 在列 0 时该行不算
/// 输出（命令以换行结尾的常态），列 > 0 时取 `[0, end_column)` 的半行；③ 已滚出存储顶端的
/// 行跳过，越界的列端点按 @p columns 截断。输出为空的块（`cd` 一类）回空表。
/// @param block 命令块。
/// @param dropped_lines 权威网格的顶边位移读数。
/// @param total_lines 权威网格的总行数。
/// @param columns 网格列数。
/// @return 存储行区间表；无 `C`、无输出或 `columns == 0` 时为空。
[[nodiscard]] auto command_output_spans(const term::CommandBlock &block, std::int64_t dropped_lines,
                                        std::size_t total_lines, std::size_t columns) -> std::vector<RowSpan>;

/// @brief 失败块的输出行在**可见窗内**的屏幕行区间表（失败底色用）。
///
/// 判据是 `CommandBlock::failed()`（已完成、带退出码且非 0）；提示符行与命令行回显行不在
/// 返回表里——`C` 已给出唯一的输出起点，不按 `A/B` 反推。区间按可见窗 `[window_top,
/// window_top + visible_rows)` 裁剪，块整体在窗外即空表；被窗沿切掉的部分只留可见那截，
/// 于是绘制侧不需要再做一次夹取。
/// @param block 命令块。
/// @param dropped_lines 权威网格的顶边位移读数。
/// @param window_top 可见窗顶的存储行号（`ScreenMirror::window_top()`）。
/// @param visible_rows 可见窗行数（`ScreenMirror::rows()`）。
/// @return 屏幕行区间表（0 = 窗顶）；非失败块、无输出或不可见时为空。
[[nodiscard]] auto failed_output_spans(const term::CommandBlock &block, std::int64_t dropped_lines,
                                       std::size_t window_top, std::size_t visible_rows)
    -> std::vector<BlockRowRange>;

/// @brief 取命令块输出的文本（「复制上一命令的全部输出」的取数腿）。
///
/// 逐行裁掉行尾**网格填充空格**再拼 LF：块输出没有 `Selection` 的列端点语义，整行末尾的空格
/// 无法与命令显式打印的空格区分（裁决 7.123 D4① 的在册代价），故一律裁掉。三项 `CopyOptions`
/// 照常生效，次序仍是「剥 tmux 边框 → 裁行尾 → 合并续行」；不合成结尾换行。
/// @param storage 权威网格（调用方须持会话锁访问）。
/// @param block 命令块（行号是绝对行）。
/// @param dropped_lines 权威网格的顶边位移读数。
/// @param options 三项复制变换。
/// @return 输出文本；无输出或整段滚出存储时为空串。
[[nodiscard]] auto copy_command_output(const grid::Storage &storage, const term::CommandBlock &block,
                                       std::int64_t dropped_lines, const CopyOptions &options) -> std::string;

}  // namespace borealis::ui
