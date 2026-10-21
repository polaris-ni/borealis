/// 测试类型: unit
/// 目标单元: include/borealis/ui/command_block.h + src/ui/command_block.cpp
/// 测试说明: 命令块界面折算的两条腿（`SPEC.FEAT.INTEG.01`，裁决 7.123 D4①/D5①）：
///           ① 输出区间只取 `C → D`——首行从 C 的列起算、中间行整行、D 在列 0 时末行不算输出、
///              列 > 0 时取半行、无输出与越界列端点各自成一档；
///           ② 失败底色区间只覆盖输出行（提示符与回显天然不在内）、只留可见窗那截、
///              且「已完成 + 有退出码 + 非零」三道判据缺一不可。
///           另钉命令块复制复用共享拼接腿时的两件事：行尾网格填充空格一律裁掉（不依赖
///           `CopyOptions::trim_trailing_space`），且裁剪发生在 `smart_line_join` 之前。
///           （SPEC.FEAT.INTEG.01）

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "borealis/term/command_block.h"
#include "borealis/ui/command_block.h"
#include "borealis/ui/selection.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_ui_command_block {

namespace {

using borealis::grid::Cell;
using borealis::grid::Storage;
using borealis::term::CommandBlock;
using borealis::ui::BlockRowRange;
using borealis::ui::command_output_spans;
using borealis::ui::copy_command_output;
using borealis::ui::CopyOptions;
using borealis::ui::failed_output_spans;
using borealis::ui::RowSpan;

/// @brief 造一块小网格（scrollback 显式给小值，用例只按参数喂位移读数）。
auto make_storage(std::size_t columns, std::size_t rows) -> Storage {
    return Storage{columns, rows, 4U};
}

/// @brief 在第 @p row 行、@p column 列起写入一段 ASCII 文本。
auto put_text(Storage &storage, std::size_t row, std::size_t column, std::string_view text) -> void {
    for (std::size_t i = 0; i < text.size(); ++i) {
        Cell cell{};
        cell.code_point = static_cast<char32_t>(static_cast<unsigned char>(text[i]));
        storage.line(row).set(column + i, cell);
    }
}

/// @brief 一条已完成、D 在列 0 的命令块（最常见的「命令以换行结尾」形态）。
auto completed_block(std::int64_t prompt_row, std::int64_t output_start_row, std::size_t output_start_column,
                     std::int64_t output_end_row, std::size_t output_end_column,
                     std::optional<std::int32_t> exit_code = std::nullopt) -> CommandBlock {
    CommandBlock block;
    block.serial = 1U;
    block.prompt_row = prompt_row;
    block.prompt_end_row = prompt_row;
    block.output_start_row = output_start_row;
    block.output_start_column = output_start_column;
    block.output_end_row = output_end_row;
    block.output_end_column = output_end_column;
    block.has_output_start = true;
    block.completed = true;
    if (exit_code.has_value()) {
        block.exit_code = *exit_code;
        block.has_exit_code = true;
    }
    return block;
}

}  // namespace

AURORA_TEST_CASE(copy_takes_only_the_output_between_c_and_d) {
    Storage storage = make_storage(10U, 6U);
    put_text(storage, 0U, 0U, "PS> ls");
    put_text(storage, 1U, 0U, "alpha");
    put_text(storage, 2U, 0U, "beta");
    put_text(storage, 3U, 0U, "PS> ");

    const auto block = completed_block(0, 1U, 0U, 3U, 0U);
    const auto spans = command_output_spans(block, storage.dropped_lines(), storage.total_lines(),
                                            storage.columns());
    // 提示符行（0）与下一条提示符行（3）都不进表；中间两行取满宽。
    AURORA_TEST_CHECK(spans == std::vector<RowSpan>{{1U, 0U, 10U}, {2U, 0U, 10U}});
    // 行尾的网格填充空格一律裁掉，行间用 LF，不合成结尾换行。
    AURORA_TEST_CHECK_EQ(copy_command_output(storage, block, storage.dropped_lines(), CopyOptions{}),
                         std::string{"alpha\nbeta"});
}

AURORA_TEST_CASE(a_zero_column_d_drops_that_row_and_a_positive_column_keeps_the_half_line) {
    Storage storage = make_storage(10U, 4U);
    put_text(storage, 1U, 0U, "abc");
    put_text(storage, 2U, 0U, "defGHI");

    // D 在列 0：末行是下一条提示符，不属于本块。
    const auto closed = completed_block(0, 1U, 0U, 2U, 0U);
    AURORA_TEST_CHECK_EQ(copy_command_output(storage, closed, 0, CopyOptions{}), std::string{"abc"});

    // D 在列 3：那一行前半段是输出。
    const auto half = completed_block(0, 1U, 0U, 2U, 3U);
    AURORA_TEST_CHECK_EQ(copy_command_output(storage, half, 0, CopyOptions{}), std::string{"abc\ndef"});

    // C 与 D 同行且 D 在列 0：一条零宽输出，不产文本，菜单条目也不该出现。
    const auto empty = completed_block(0, 1U, 0U, 1U, 0U);
    AURORA_TEST_CHECK_FALSE(empty.has_copyable_output());
    AURORA_TEST_CHECK_TRUE(command_output_spans(empty, 0, storage.total_lines(), storage.columns()).empty());
}

AURORA_TEST_CASE(the_first_line_starts_at_the_c_column) {
    Storage storage = make_storage(10U, 4U);
    put_text(storage, 1U, 0U, "abcdef");
    put_text(storage, 2U, 0U, "ghij");

    // C 落在第 2 列、D 落在末行第 2 列：首行从 C 起、末行到 D 止。
    const auto block = completed_block(0, 1U, 2U, 2U, 2U);
    const auto spans = command_output_spans(block, 0, storage.total_lines(), storage.columns());
    AURORA_TEST_CHECK(spans == std::vector<RowSpan>{{1U, 2U, 10U}, {2U, 0U, 2U}});
    AURORA_TEST_CHECK_EQ(copy_command_output(storage, block, 0, CopyOptions{}), std::string{"cdef\ngh"});
}

AURORA_TEST_CASE(output_rows_scrolled_out_of_storage_are_skipped_not_blanked) {
    Storage storage = make_storage(8U, 4U);
    put_text(storage, 0U, 0U, "kept");
    put_text(storage, 1U, 0U, "tail");
    // 绝对行 1..3 是输出（D 列 0 的 4 不进块），位移 2 行后三行落在存储行 -1/0/1：
    // 首行已滚出，后两行照常产出，且不为滚出的那行补空行。
    const auto block = completed_block(0, 1U, 0U, 4U, 0U);
    AURORA_TEST_CHECK_EQ(copy_command_output(storage, block, 2, CopyOptions{}), std::string{"kept\ntail"});
    // 整段滚出：不补空行，直接空串。
    AURORA_TEST_CHECK_EQ(copy_command_output(storage, block, 8, CopyOptions{}), std::string{});
}

AURORA_TEST_CASE(failed_spans_cover_only_the_output_and_clip_to_the_visible_window) {
    Storage storage = make_storage(10U, 5U);
    const auto block = completed_block(0, 2U, 0U, 4U, 0U, 1);
    AURORA_TEST_REQUIRE(block.failed());
    // 输出存储行 2..3；窗顶 1、可见 3 行 ⇒ 屏幕行 1..2。提示符/回显行 0..1 不在内。
    AURORA_TEST_CHECK(failed_output_spans(block, 0, 1U, 3U) ==
                      std::vector<BlockRowRange>{BlockRowRange{1U, 3U}});
    // 块整体在窗下或窗上：空表（不夹出一个贴边的假区间）。
    AURORA_TEST_CHECK(failed_output_spans(block, 0, 5U, 3U).empty());
    AURORA_TEST_CHECK(failed_output_spans(block, 0, 0U, 2U).empty());
}

AURORA_TEST_CASE(failure_needs_completed_numeric_and_nonzero) {
    Storage storage = make_storage(10U, 5U);
    // 退出码 0、无退出码、未完成三档都不画。
    AURORA_TEST_CHECK_FALSE(completed_block(0, 1U, 0U, 2U, 0U, 0).failed());
    AURORA_TEST_CHECK_FALSE(completed_block(0, 1U, 0U, 2U, 0U).failed());
    auto open = completed_block(0, 1U, 0U, 2U, 0U, 1);
    open.completed = false;
    AURORA_TEST_CHECK_FALSE(open.failed());
    AURORA_TEST_CHECK(failed_output_spans(open, 0, 0U, storage.visible_rows()).empty());
}

AURORA_TEST_CASE(trimming_happens_before_the_smart_line_join) {
    Storage storage = make_storage(8U, 2U);
    put_text(storage, 0U, 0U, "foo\\");
    put_text(storage, 1U, 0U, "bar");
    const auto block = completed_block(0, 0U, 0U, 2U, 0U);
    CopyOptions options;
    options.smart_line_join = true;

    // `foo\` 后的填充空格不裁掉时，反斜杠不在行尾，续行合不上。
    AURORA_TEST_CHECK_EQ(copy_command_output(storage, block, 0, CopyOptions{.smart_line_join = false}),
                         std::string{"foo\\\nbar"});
    // 先裁行尾再合并：反斜杠回到行尾，合并后连分隔换行也不留。
    AURORA_TEST_CHECK_EQ(copy_command_output(storage, block, 0, options), std::string{"foobar"});
}

}  // namespace borealis::test_cases::utest_ui_command_block
