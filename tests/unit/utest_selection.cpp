/// 测试类型: unit
/// 目标单元: include/borealis/ui/selection.h + src/ui/selection.cpp
/// 测试说明: 选区端点归一（流式按字典序定首尾、拖拽方向无关、中间行整行入选、列模式逐行同区间、
///           单击不成选区、列端点按网格宽度截断）与选中文本取值（行以 LF 分隔、未开启变换时行尾
///           填充空格原样保留、越界行不产文本也不补空行、左界落在双宽延续格上时整字符纳入且只出
///           一次、组合标记随基础格并进同一段文本）、三项默认关闭的复制变换各自口径（剥行尾空白
///           只认 U+0020、续行合并只对奇数个行尾反斜杠生效、tmux 细线制表符逐格剥离且纯边框行整行
///           丢弃）与其固定生效次序（裁决 7.32③）；双击选词的断点口径（空格与制表恒断点、界定符按
///           整码点而非字节判、落点在断点上不成选区、双宽字符整字符入选，裁决 7.38③）；
///           选区随存储顶边位移折算（钳位、整段推出即作废、饱和滚动后内容仍被同一选区取到，
///           裁决 7.38⑤ / 7.39）。
///           （SPEC.FEAT.INTERACT.02、SPEC.FEAT.INTERACT.03）

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/config/settings.h"
#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "borealis/ui/selection.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_selection {

namespace {

using borealis::grid::Cell;
using borealis::grid::Row;
using borealis::grid::Storage;
using borealis::ui::copy_text;
using borealis::ui::CopyOptions;
using borealis::ui::GridCellPos;
using borealis::ui::row_spans;
using borealis::ui::RowSpan;
using borealis::ui::Selection;
using borealis::ui::SelectionShape;
using borealis::ui::translate_selection_rows;
using borealis::ui::word_span_at;

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

/// @brief 在某列写一个码点（选词只看码点，不看样式）。
auto set_glyph(Row &row, std::size_t column, char32_t code_point) -> void {
    Cell cell{};
    cell.code_point = code_point;
    row.set(column, cell);
}

/// @brief 造一行 ASCII 文本，其余列留空白。
[[nodiscard]] auto text_row(std::string_view text, std::size_t columns) -> Row {
    Row row{columns};
    for (std::size_t i = 0; i < text.size(); ++i) {
        set_glyph(row, i, static_cast<char32_t>(static_cast<unsigned char>(text[i])));
    }
    return row;
}

/// @brief 在行的某列写一个双宽字符（基础格 + 延续格）。
///
/// 延续格的码点是 0 而非空格——状态机就是这么写的（`src/term/terminal.cpp` 的 put_wide 腿）。
/// 留成空格会让「延续格按其基础格判定」那条口径测不到：空格本来就断词。
auto put_wide_in(Row &row, std::size_t column, char32_t code_point) -> void {
    Cell base{};
    base.code_point = code_point;
    base.width = 2;
    row.set(column, base);

    Cell continuation{};
    continuation.code_point = 0;
    continuation.width = 0;
    continuation.flags = borealis::grid::kFlagWideContinuation;
    row.set(column + 1U, continuation);
}

/// @brief 界定符取配置键的默认值：测试另抄一张表就会与真实默认各走一边。
[[nodiscard]] auto default_delimiters() -> std::string_view {
    static const borealis::config::TerminalSettings kDefaults{};
    return kDefaults.word_delimiters;
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

AURORA_TEST_CASE(word_span_at_picks_the_whole_word_under_the_pointer) {
    const auto row = text_row("cd /home/user/file.txt", 24U);
    // 路径按「/」分节、文件名按「.」分节是缺省界定符集的行为（裁决 7.38③ 点名 : \ / . - _）。
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{3U, 5U}, default_delimiters()) == RowSpan{3U, 4U, 8U});
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{3U, 19U}, default_delimiters()) == RowSpan{3U, 19U, 22U});
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{3U, 0U}, default_delimiters()) == RowSpan{3U, 0U, 2U});
    // 同一行内落点在词首、词中、词尾都得到同一个区间：选词不认拖拽方向。
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{3U, 4U}, default_delimiters()) ==
                      word_span_at(row, GridCellPos{3U, 7U}, default_delimiters()));
}

AURORA_TEST_CASE(word_span_at_splits_on_underscore_and_joins_without_a_delimiter_set) {
    AURORA_TEST_CHECK(word_span_at(text_row("foo_bar", 8U), GridCellPos{0U, 2U}, default_delimiters()) ==
                      RowSpan{0U, 0U, 3U});  // `_` 随 xterm 取为断点，故只选中 foo

    // 界定符集为空 = 用户点名「标点算字组成分」，此时只有空格与制表断词（恒断点，裁决 7.38③）。
    AURORA_TEST_CHECK(word_span_at(text_row("a=b", 8U), GridCellPos{0U, 1U}, "") == RowSpan{0U, 0U, 3U});
    AURORA_TEST_CHECK(word_span_at(text_row("a\tb", 8U), GridCellPos{0U, 0U}, "") == RowSpan{0U, 0U, 1U});
    AURORA_TEST_CHECK(word_span_at(text_row("a\tb", 8U), GridCellPos{0U, 2U}, "") == RowSpan{0U, 2U, 3U});
}

AURORA_TEST_CASE(word_span_at_returns_nothing_on_a_break_or_out_of_range) {
    const auto row = text_row("ab cd", 8U);
    AURORA_TEST_CHECK_FALSE(word_span_at(row, GridCellPos{0U, 2U}, default_delimiters()).has_value());
    AURORA_TEST_CHECK_FALSE(word_span_at(row, GridCellPos{0U, 5U}, default_delimiters()).has_value());
    AURORA_TEST_CHECK_FALSE(word_span_at(row, GridCellPos{0U, 10U}, default_delimiters()).has_value());
    AURORA_TEST_CHECK_FALSE(word_span_at(row, GridCellPos{0U, 0U}, "abc").has_value());  // 落点自身是界定符
    AURORA_TEST_CHECK_FALSE(word_span_at(Row{0U}, GridCellPos{0U, 0U}, default_delimiters()).has_value());
}

AURORA_TEST_CASE(word_span_at_compares_whole_code_points_not_bytes) {
    // 界定符 é 的 UTF-8 续字节是 0xA9，与码点 U+00A9（©）的编码首字节撞车：按字节比就把 © 当断点。
    const std::string_view accent{"\xC3\xA9"};  // é 的 UTF-8 字节形态就是被测事实
    auto row = text_row("a", 4U);
    set_glyph(row, 1U, char32_t{0x00A9});
    set_glyph(row, 2U, U'b');
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{0U, 1U}, accent) == RowSpan{0U, 0U, 3U});

    auto broken = text_row("a", 4U);
    set_glyph(broken, 1U, char32_t{0x00E9});  // é 自己才是那个界定符
    set_glyph(broken, 2U, U'b');
    AURORA_TEST_CHECK(word_span_at(broken, GridCellPos{0U, 0U}, accent) == RowSpan{0U, 0U, 1U});
    AURORA_TEST_CHECK_FALSE(word_span_at(broken, GridCellPos{0U, 1U}, accent).has_value());
}

AURORA_TEST_CASE(word_span_at_does_not_lean_on_the_continuation_of_a_wide_break) {
    // 界定符也可以是双宽字符（、 U+3001）：它的延续格同样断词，否则选区会从半个字上起笔。
    const std::string_view ideographic_comma{"\xE3\x80\x81"};
    auto row = text_row("a", 6U);
    put_wide_in(row, 1U, char32_t{0x3001});
    set_glyph(row, 3U, U'b');

    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{0U, 3U}, ideographic_comma) == RowSpan{0U, 3U, 4U});
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{0U, 0U}, ideographic_comma) == RowSpan{0U, 0U, 1U});
    AURORA_TEST_CHECK_FALSE(word_span_at(row, GridCellPos{0U, 2U}, ideographic_comma).has_value());
}

AURORA_TEST_CASE(word_span_at_selects_a_whole_wide_glyph_from_either_half) {
    auto row = text_row("x", 6U);
    put_wide_in(row, 2U, char32_t{0x4E2D});  // 基础格 2、延续格 3，两侧各留一个空格
    set_glyph(row, 5U, U'y');

    // 指针只能指到双宽字符的左半格（第 3 列），落在那里与落在基础格同解，且整字符入选。
    const auto expected = RowSpan{0U, 2U, 4U};
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{0U, 2U}, default_delimiters()) == expected);
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{0U, 3U}, default_delimiters()) == expected);
    AURORA_TEST_CHECK(word_span_at(row, GridCellPos{0U, 5U}, default_delimiters()) == RowSpan{0U, 5U, 6U});

    // 与复制腿对齐：由 span 造出的选区取到的文本就是那个字，不多不少。
    auto storage = make_storage(6, 1);
    put_text(storage, 0, 0, "x");
    put_wide(storage, 0, 2, char32_t{0x4E2D});
    put_text(storage, 0, 5, "y");
    const auto span = word_span_at(storage.line(0), GridCellPos{0U, 3U}, default_delimiters());
    AURORA_TEST_REQUIRE(span.has_value());
    const auto selected = stream(0U, span->first_column, 0U, span->last_column - 1U);
    AURORA_TEST_CHECK_STREQ(copy_text(storage, selected, CopyOptions{}), std::string{"\xE4\xB8\xAD"});
}

AURORA_TEST_CASE(translate_moves_both_ends_by_the_top_edge_delta) {
    const auto selection = stream(5U, 2U, 9U, 7U);
    AURORA_TEST_CHECK(translate_selection_rows(selection, 3) == stream(2U, 2U, 6U, 7U));
    // 负读数＝内容整体下移（顶部补空白、无历史时的上滚），行号随之增大。
    AURORA_TEST_CHECK(translate_selection_rows(selection, -2) == stream(7U, 2U, 11U, 7U));
    AURORA_TEST_CHECK(translate_selection_rows(selection, 0) == selection);
    // 列号一律不动：顶边位移只发生在行方向。
    AURORA_TEST_CHECK_EQ(translate_selection_rows(selection, 3).anchor.column, 2U);
}

AURORA_TEST_CASE(translate_clamps_the_end_that_ran_off_the_top) {
    const auto moved = translate_selection_rows(stream(1U, 3U, 4U, 6U), 2);
    AURORA_TEST_CHECK(moved == stream(0U, 3U, 2U, 6U));
}

AURORA_TEST_CASE(translate_collapses_a_selection_that_drifted_entirely_off) {
    const auto moved = translate_selection_rows(stream(0U, 3U, 1U, 6U), 4);
    AURORA_TEST_CHECK(moved.anchor == moved.focus);
    // 「作废」由无选区表达（两端重合即单击），而不是钳到第 0 行指着空白继续高亮。
    AURORA_TEST_CHECK(row_spans(moved, 80U).empty());

    const auto backward = translate_selection_rows(stream(5U, 2U, 1U, 9U), 6);
    AURORA_TEST_CHECK(row_spans(backward, 80U).empty());
}

AURORA_TEST_CASE(translate_keeps_a_single_click_a_single_click) {
    const auto click = stream(3U, 4U, 3U, 4U);
    const auto moved = translate_selection_rows(click, 2U);
    AURORA_TEST_CHECK(moved.anchor == moved.focus);
    AURORA_TEST_CHECK_EQ(moved.anchor.row, std::size_t{1});
    AURORA_TEST_CHECK(row_spans(moved, 80U).empty());
}

AURORA_TEST_CASE(selected_content_survives_a_saturated_scroll) {
    // 容量 4 = 视口 2 + 历史 2：第三次上滚才第一次挤掉最旧行，故顶边位移从那时才开始计。
    Storage storage{4U, 2U, 2U};
    put_text(storage, 1U, 0U, "keep");
    const auto initial = stream(1U, 0U, 1U, 2U);
    AURORA_TEST_CHECK_STREQ(copy_text(storage, initial, CopyOptions{}), std::string{"kee"});

    storage.scroll_up(3U);
    AURORA_TEST_CHECK_EQ(storage.dropped_lines(), std::int64_t{1});
    // 不折算就指着空白：选区存的是存储行序，内容前移一行它也得跟着减一。
    AURORA_TEST_CHECK_STREQ(copy_text(storage, initial, CopyOptions{}), std::string{"   "});
    AURORA_TEST_CHECK_STREQ(
        copy_text(storage, translate_selection_rows(initial, storage.dropped_lines()), CopyOptions{}),
        std::string{"kee"});
}

}  // namespace borealis::test_cases::utest_selection
