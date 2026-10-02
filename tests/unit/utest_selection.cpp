/// 测试类型: unit
/// 目标单元: include/borealis/ui/selection.h + src/ui/selection.cpp
/// 测试说明: 选区端点归一（流式按字典序定首尾、拖拽方向无关、中间行整行入选、列模式逐行同区间、
///           单击不成选区、列端点按网格宽度截断）与选中文本取值（行以 LF 分隔、未开启变换时行尾
///           填充空格原样保留、越界行不产文本也不补空行、左界落在双宽延续格上时整字符纳入且只出
///           一次、组合标记随基础格并进同一段文本）、三项默认关闭的复制变换各自口径（剥行尾空白
///           只认 U+0020、续行合并只对奇数个行尾反斜杠生效、tmux 细线制表符逐格剥离且纯边框行整行
///           丢弃）与其固定生效次序（裁决 7.32③）。
///           （SPEC.FEAT.INTERACT.02、SPEC.FEAT.INTERACT.03）

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "borealis/ui/selection.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_selection {

namespace {

using borealis::grid::Cell;
using borealis::grid::Storage;
using borealis::ui::copy_text;
using borealis::ui::CopyOptions;
using borealis::ui::GridCellPos;
using borealis::ui::row_spans;
using borealis::ui::RowSpan;
using borealis::ui::Selection;
using borealis::ui::SelectionShape;

/// @brief 造一块小网格（scrollback 显式给小值，用例不测历史溢出前的容量）。
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

/// @brief 在第 @p row 行、@p column 列写一个指定码点。
auto put(Storage &storage, std::size_t row, std::size_t column, char32_t code_point) -> void {
    Cell cell{};
    cell.code_point = code_point;
    storage.line(row).set(column, cell);
}

/// @brief 写一个双宽字符：基础格 width=2，其右一格置延续标记（口径见 cell.h）。
auto put_wide(Storage &storage, std::size_t row, std::size_t column, char32_t code_point) -> void {
    Cell base{};
    base.code_point = code_point;
    base.width = 2;
    storage.line(row).set(column, base);

    Cell continuation{};
    continuation.width = 0;
    continuation.flags = borealis::grid::kFlagWideContinuation;
    storage.line(row).set(column + 1U, continuation);
}

/// @brief 向下向右拖出的流式选区。
auto stream(std::size_t from_row, std::size_t from_column, std::size_t to_row, std::size_t to_column)
    -> Selection {
    return Selection{GridCellPos{from_row, from_column}, GridCellPos{to_row, to_column},
                     SelectionShape::Stream};
}

/// @brief 矩形块选区。
auto block(std::size_t from_row, std::size_t from_column, std::size_t to_row, std::size_t to_column)
    -> Selection {
    return Selection{GridCellPos{from_row, from_column}, GridCellPos{to_row, to_column},
                     SelectionShape::Block};
}

/// @brief U+2500（tmux 水平边框）与 U+2502（垂直分隔）的 UTF-8 字节。
constexpr std::string_view kLightHorizontalUtf8{"\xE2\x94\x80"};
constexpr std::string_view kLightVerticalUtf8{"\xE2\x94\x82"};

}  // namespace

AURORA_TEST_CASE(row_spans_normalizes_stream_selection_and_covers_middle_rows) {
    // 首行从起点列一直到行尾、末行从行首截到焦点列，中间行整行入选。
    const auto spans = row_spans(stream(0, 3, 2, 5), 10U);
    const std::vector<RowSpan> expected{{0, 3, 10}, {1, 0, 10}, {2, 0, 6}};
    AURORA_TEST_CHECK(spans == expected);
}

AURORA_TEST_CASE(row_spans_treats_reverse_drag_identically) {
    // 端点谁先谁后只由 (行, 列) 字典序决定：从右下往左上拖与反向拖同解。
    const auto forward = row_spans(stream(0, 3, 2, 5), 10U);
    const auto backward = row_spans(stream(2, 5, 0, 3), 10U);
    AURORA_TEST_CHECK(forward == backward);

    const std::vector<RowSpan> same_row{{1, 2, 6}};
    AURORA_TEST_CHECK(row_spans(stream(1, 5, 1, 2), 10U) == same_row);
}

AURORA_TEST_CASE(row_spans_block_selection_gives_every_row_the_same_column_range) {
    const auto spans = row_spans(block(2, 7, 0, 1), 10U);
    const std::vector<RowSpan> expected{{0, 1, 8}, {1, 1, 8}, {2, 1, 8}};
    AURORA_TEST_CHECK(spans == expected);
}

AURORA_TEST_CASE(row_spans_is_empty_on_single_click_or_zero_columns) {
    AURORA_TEST_CHECK(row_spans(stream(1, 2, 1, 2), 80U).empty());
    AURORA_TEST_CHECK(row_spans(block(1, 2, 1, 2), 80U).empty());
    AURORA_TEST_CHECK(row_spans(stream(0, 0, 0, 1), 0U).empty());
}

AURORA_TEST_CASE(row_spans_clamps_columns_to_the_grid_width) {
    // 改窄后的旧选区是真实输入：列端点截到最后一格，右界因此等于列数。
    const auto spans = row_spans(stream(0, 70, 0, 90), 80U);
    const std::vector<RowSpan> expected{{0, 70, 80}};
    AURORA_TEST_CHECK(spans == expected);
}

AURORA_TEST_CASE(copy_text_joins_selected_rows_with_lf_and_keeps_padding_verbatim) {
    auto storage = make_storage(12, 4);
    put_text(storage, 1, 0, "alpha");
    put_text(storage, 2, 0, "beta");

    const auto text = copy_text(storage, stream(1, 2, 2, 3), CopyOptions{});
    // 首行取到行尾（默认变换全关，行尾的填充空格属「原样」），末行取到焦点列。
    AURORA_TEST_CHECK_STREQ(text, std::string{"pha       \nbeta"});
}

AURORA_TEST_CASE(copy_text_skips_rows_beyond_the_buffer_without_padding_empty_ones) {
    auto storage = make_storage(8, 2);
    put_text(storage, 0, 0, "top");
    // 选到 total_lines() 之外：越界行不产文本，也不补空行；界内的空白行仍按原样出空格。
    const auto text = copy_text(storage, stream(0, 0, 5, 3), CopyOptions{});
    AURORA_TEST_CHECK_STREQ(text, std::string{"top     \n        "});
}

AURORA_TEST_CASE(copy_text_snaps_a_left_boundary_off_a_wide_continuation) {
    auto storage = make_storage(8, 1);
    put_text(storage, 0, 0, "ab");
    put_wide(storage, 0, 2, U'W');
    put_text(storage, 0, 4, "cd");

    // 左界落在第 3 列（'W' 的延续格）：整字符纳入，且延续格不再产第二个字符。
    AURORA_TEST_CHECK_STREQ(copy_text(storage, stream(0, 3, 0, 4), CopyOptions{}), std::string{"Wc"});
    AURORA_TEST_CHECK_STREQ(copy_text(storage, stream(0, 2, 0, 3), CopyOptions{}), std::string{"W"});
}

AURORA_TEST_CASE(copy_text_appends_combining_marks_to_their_base_cell) {
    auto storage = make_storage(8, 1);
    put_text(storage, 0, 0, "e");
    storage.line(0).attach_combining(0, char32_t{0x0301});  // 组合尖音符并入基础格
    put_text(storage, 0, 1, "x");

    const auto text = copy_text(storage, stream(0, 0, 0, 1), CopyOptions{});
    AURORA_TEST_CHECK_STREQ(text, std::string{"e\xCC\x81" "x"});
}

AURORA_TEST_CASE(trim_trailing_space_option_strips_only_trailing_spaces) {
    auto storage = make_storage(12, 2);
    put_text(storage, 0, 0, "abc   ");
    put_text(storage, 1, 0, "d  e");  // 行中空白不剥

    CopyOptions options{};
    options.trim_trailing_space = true;
    AURORA_TEST_CHECK_STREQ(copy_text(storage, stream(0, 0, 1, 11), options), std::string{"abc\nd  e"});
}

AURORA_TEST_CASE(smart_line_join_option_merges_odd_trailing_backslash_only) {
    auto storage = make_storage(12, 4);
    put_text(storage, 0, 0, "make \\");
    put_text(storage, 1, 0, "clean");
    put_text(storage, 2, 0, "path\\\\");  // 偶数个：转义出来的字面反斜杠，不是续行
    put_text(storage, 3, 0, "next");

    CopyOptions options{};
    options.trim_trailing_space = true;  // 续行判定在剥空白之后，否则行尾填充会挡住它
    options.smart_line_join = true;
    AURORA_TEST_CHECK_STREQ(copy_text(storage, stream(0, 0, 1, 4), options), std::string{"make clean"});
    AURORA_TEST_CHECK_STREQ(copy_text(storage, stream(2, 0, 3, 4), options), std::string{"path\\\\"
                                                                                          "\nnext"});
}

AURORA_TEST_CASE(strip_tmux_border_chars_option_removes_borders_and_drops_border_only_rows) {
    auto storage = make_storage(12, 2);
    for (std::size_t column = 0; column < 12U; ++column) {
        put(storage, 0, column, char32_t{0x2500});  // 纯水平边框行
    }
    put_text(storage, 1, 0, "left");
    put(storage, 1, 4, char32_t{0x2502});  // 两栏之间的垂直分隔
    put_text(storage, 1, 5, "right");

    std::string plain_expected;
    for (std::size_t i = 0; i < 12U; ++i) {
        plain_expected.append(kLightHorizontalUtf8);
    }
    plain_expected += "\nleft";
    plain_expected.append(kLightVerticalUtf8);
    plain_expected += "right";
    AURORA_TEST_CHECK_STREQ(copy_text(storage, stream(0, 0, 1, 9), CopyOptions{}), plain_expected);

    CopyOptions options{};
    options.strip_tmux_border_chars = true;
    // 纯边框行整行丢弃（连同换行），竖线只删字符故两侧文本并成一行。
    AURORA_TEST_CHECK_STREQ(copy_text(storage, stream(0, 0, 1, 9), options), std::string{"leftright"});
}

AURORA_TEST_CASE(copy_transforms_apply_in_the_documented_order) {
    auto storage = make_storage(8, 2);
    put_text(storage, 0, 0, "a\\");
    put(storage, 0, 2, char32_t{0x2502});  // 行尾是「反斜杠 + 边框字符」
    put_text(storage, 1, 0, "b");

    CopyOptions options{};
    options.trim_trailing_space = true;
    options.smart_line_join = true;
    options.strip_tmux_border_chars = true;
    // 边框先剥（否则它挡在行尾，后两条都判不到），再剥空白，最后合续行。
    AURORA_TEST_CHECK_STREQ(copy_text(storage, stream(0, 0, 1, 2), options), std::string{"ab"});
}

}  // namespace borealis::test_cases::utest_selection
