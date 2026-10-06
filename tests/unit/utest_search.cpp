/// 测试类型: unit
/// 目标单元: include/borealis/ui/search.h + src/ui/search.cpp
/// 测试说明: 字面量档的逐行匹配（按 (行, 列) 升序、大小写开关只折 ASCII 字母、双宽字符整字符入选
///           且不产半格、行尾连续空格填充不算内容、不产重叠匹配）与正则档的同一批口径（区间整格
///           对齐、icase 随开关、表达式非法回空值而不是「0 个匹配」、零宽匹配不产区间、星象字符
///           在代理对的两个单元上只算一段）；两档对同一查询给出同一批区间；匹配表触到上限即停止
///           扫描并把 `truncated` 说出来（此时条数是下限）；游标的前后跳转首尾相连；结果表随存储
///           顶边位移折算（逐条丢弃出界匹配、游标跟着它当初指着的那一段、该段被丢弃时游标归空）。
///           （SPEC.FEAT.INTERACT.04）

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "borealis/ui/search.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_search {

namespace {

using borealis::grid::Cell;
using borealis::grid::Storage;
using borealis::ui::kMaxSearchMatches;
using borealis::ui::RowSpan;
using borealis::ui::SearchDirection;
using borealis::ui::SearchMatches;
using borealis::ui::SearchQuery;

/// @brief 造一块小网格（scrollback 显式给小值，用例只测覆盖范围而不测容量）。
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

/// @brief 在第 @p row 行、@p column 列写一个指定码点（单宽）。
auto put(Storage &storage, std::size_t row, std::size_t column, char32_t code_point) -> void {
    Cell cell{};
    cell.code_point = code_point;
    storage.line(row).set(column, cell);
}

/// @brief 写一个双宽字符：基础格 width=2，右一格是延续格。
///
/// 延续格的码点是 0 而非空格——状态机就是这么写的（`src/term/terminal.cpp` 的 put_wide 腿）。
/// 留成空格会让「行末那个汉字不算填充」这条口径测不到：空格本来就被当填充剥掉。
auto put_wide(Storage &storage, std::size_t row, std::size_t column, char32_t code_point) -> void {
    Cell base{};
    base.code_point = code_point;
    base.width = 2;
    storage.line(row).set(column, base);

    Cell continuation{};
    continuation.code_point = 0;
    continuation.width = 0;
    continuation.flags = borealis::grid::kFlagWideContinuation;
    storage.line(row).set(column + 1U, continuation);
}

/// @brief 字面量查询（大小写档由入参给出）。
auto literal(std::u32string_view text, bool case_sensitive = false) -> SearchQuery {
    return SearchQuery{std::u32string{text}, case_sensitive, false};
}

/// @brief 正则查询（大小写档由入参给出）。
auto pattern(std::u32string_view text, bool case_sensitive = false) -> SearchQuery {
    return SearchQuery{std::u32string{text}, case_sensitive, true};
}

/// @brief 把匹配表取成可逐格比对的 vector（视图本身没有 == 的对照物）。
[[nodiscard]] auto spans_of(const SearchMatches &matches) -> std::vector<RowSpan> {
    return std::vector<RowSpan>{matches.spans().begin(), matches.spans().end()};
}

/// @brief 拆扫描结果；「没搜到」与「表达式非法」是两件事，故用例都得先确认拆得开。
[[nodiscard]] auto searched(const Storage &storage, const SearchQuery &query) -> std::optional<SearchMatches> {
    return borealis::ui::search(storage, query);
}

}  // namespace

AURORA_TEST_CASE(literal_search_reports_every_occurrence_in_row_and_column_order) {
    auto storage = make_storage(10U, 2U);
    put_text(storage, 0, 0, "abcabc");
    put_text(storage, 1, 2, "abc");

    auto matches = searched(storage, literal(U"abc"));
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 0, 3}, {0, 3, 6}, {1, 2, 5}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(literal_search_is_case_insensitive_by_default) {
    auto storage = make_storage(10U, 2U);
    put_text(storage, 0, 0, "Alpha");
    put_text(storage, 1, 0, "ALPHA");

    auto matches = searched(storage, literal(U"alpha"));
    AURORA_TEST_REQUIRE(matches.has_value());
    AURORA_TEST_CHECK_EQ(matches->count(), std::size_t{2});
}

AURORA_TEST_CASE(case_sensitive_literal_search_sees_only_the_written_case) {
    auto storage = make_storage(10U, 2U);
    put_text(storage, 0, 0, "Alpha");
    put_text(storage, 1, 0, "ALPHA");

    auto matches = searched(storage, literal(U"Alpha", true));
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 0, 5}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(case_folding_covers_ascii_letters_only) {
    // 判据边界（裁决 7.78②）：两档都只折 ASCII 字母，故 `Ä` 与 `ä` 在大小写不敏感档也**不**互认。
    auto storage = make_storage(10U, 1U);
    put(storage, 0, 0, U'\xC4');  // CJK-LITERAL: 断言素材 - 非 ASCII 字母的折叠不在本件承诺范围内
    AURORA_TEST_REQUIRE(storage.line(0).cell(0).code_point == U'\xC4');

    auto folded = searched(storage, literal(U"\xE4"));  // CJK-LITERAL: 断言素材 - 同上
    AURORA_TEST_REQUIRE(folded.has_value());
    AURORA_TEST_CHECK_EQ(folded->count(), std::size_t{0});

    auto exact = searched(storage, literal(U"\xC4"));  // CJK-LITERAL: 断言素材 - 同上
    AURORA_TEST_REQUIRE(exact.has_value());
    AURORA_TEST_CHECK_EQ(exact->count(), std::size_t{1});
}

AURORA_TEST_CASE(a_double_width_character_matches_wholly_and_occupies_both_cells) {
    // 匹配文本只在基础格出现一次，故命中区间必须把延续格算进来（高亮不能把一个字切成半格）。
    auto storage = make_storage(10U, 1U);
    put_wide(storage, 0, 0, U'\x4E2D');  // CJK-LITERAL: cjk-fixture - 双宽整字符区间就是被测事实
    put_text(storage, 0, 2, "x");

    auto matches = searched(storage, literal(U"\x4E2D"));  // CJK-LITERAL: cjk-fixture - 同上
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 0, 2}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(a_double_width_character_never_matches_from_its_continuation_cell) {
    // 延续格不进匹配文本，否则同一个字会在两格上各产一段高亮、计数也跟着翻倍。
    auto storage = make_storage(10U, 1U);
    put_wide(storage, 0, 0, U'\x4E2D');  // CJK-LITERAL: cjk-fixture - 同上
    put_wide(storage, 0, 2, U'\x4E2D');  // CJK-LITERAL: cjk-fixture - 同上

    auto matches = searched(storage, literal(U"\x4E2D"));  // CJK-LITERAL: cjk-fixture - 同上
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 0, 2}, {0, 2, 4}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);

    // 另一半：不匹配的双宽字符**之后**那一格才是左界。延续格不承载字符，把它当左界会让高亮
    // 压住整个字的盒子。
    auto trailing = make_storage(10U, 1U);
    put_wide(trailing, 0, 0, U'\x4E2D');  // CJK-LITERAL: cjk-fixture - 同上
    put_text(trailing, 0, 2, "ab");
    auto after = searched(trailing, literal(U"a"));
    AURORA_TEST_REQUIRE(after.has_value());
    const std::vector<RowSpan> starts_on_the_base_cell{{0, 2, 3}};
    AURORA_TEST_CHECK(spans_of(*after) == starts_on_the_base_cell);
}

AURORA_TEST_CASE(trailing_padding_spaces_are_not_content_but_an_internal_space_is) {
    // 网格每行都是定宽、未写入的列全是 U+0020；把填充当内容会让「匹配计数」在每条输出上爆出一串
    // 无意义的高亮。行中写进去的空格照旧算内容。
    auto padded = make_storage(10U, 1U);
    put_text(padded, 0, 0, "ab");
    auto blank_query = searched(padded, literal(U" "));
    AURORA_TEST_REQUIRE(blank_query.has_value());
    AURORA_TEST_CHECK_EQ(blank_query->count(), std::size_t{0});

    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "a b");
    auto matches = searched(storage, literal(U" "));
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 1, 2}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(a_row_final_double_width_character_is_not_stripped_as_padding) {
    // 延续格的码点是 0 而不是空格，故行末的汉字不会被「剥行尾填充」那一趟吃掉而少算一格。
    auto storage = make_storage(10U, 1U);
    put_wide(storage, 0, 8, U'\x4E2D');  // CJK-LITERAL: cjk-fixture - 行末双宽格的右界就是被测事实

    auto matches = searched(storage, literal(U"\x4E2D"));  // CJK-LITERAL: cjk-fixture - 同上
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 8, 10}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(matches_never_overlap) {
    // 与正则档 `regex_iterator` 的非重叠次序一致，两档的计数才能对得上。
    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "aaa");

    auto matches = searched(storage, literal(U"aa"));
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 0, 2}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(an_empty_query_is_a_valid_result_with_no_matches) {
    // 「还没打字」既不是错误也不该报「表达式非法」，浮层据此什么都不画。
    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "abc");

    auto matches = searched(storage, literal(U""));
    AURORA_TEST_REQUIRE(matches.has_value());
    AURORA_TEST_CHECK_EQ(matches->count(), std::size_t{0});
    AURORA_TEST_CHECK_FALSE(matches->truncated());
    AURORA_TEST_CHECK_FALSE(matches->advance(SearchDirection::Forward).has_value());
}

AURORA_TEST_CASE(a_query_that_matches_nothing_is_not_an_error) {
    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "abc");

    auto matches = searched(storage, literal(U"zzz"));
    AURORA_TEST_REQUIRE(matches.has_value());
    AURORA_TEST_CHECK_EQ(matches->count(), std::size_t{0});
}

AURORA_TEST_CASE(the_scan_covers_scrollback_lines_above_the_viewport) {
    // 高亮天生跨可见区：滚动历史时匹配不能只落在视口那几行里。
    auto storage = make_storage(8U, 2U);
    put_text(storage, 0, 0, "hit");
    storage.scroll_up(1U);
    AURORA_TEST_REQUIRE_EQ(storage.total_lines(), std::size_t{3});
    AURORA_TEST_REQUIRE_LT(storage.visible_rows(), storage.total_lines());

    auto matches = searched(storage, literal(U"hit"));
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 0, 3}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(a_pattern_at_the_limit_stops_scanning_and_marks_the_table_truncated) {
    // 会话字节流是不可信输入：宽匹配式的表必须有上界，且触顶时不再往下扫（时间门禁同理）。
    Storage storage{20U, 600U, 0U};
    for (std::size_t row = 0U; row < storage.total_lines(); ++row) {
        put_text(storage, row, 0, "aaaaaaaaaaaaaaaaaaaa");
    }

    auto matches = searched(storage, literal(U"a"));
    AURORA_TEST_REQUIRE(matches.has_value());
    AURORA_TEST_CHECK_EQ(matches->count(), kMaxSearchMatches);
    AURORA_TEST_CHECK_TRUE(matches->truncated());
    // 末条在第 499 行而网格有 600 行：截断是「提前停」而不是「刚好扫完」。
    AURORA_TEST_CHECK_EQ(matches->spans().back().row, std::size_t{499});
    AURORA_TEST_CHECK_LT(matches->spans().back().row, storage.total_lines() - 1U);
}

AURORA_TEST_CASE(a_table_below_the_limit_is_not_marked_truncated) {
    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "abcabcabc");

    auto matches = searched(storage, literal(U"abc"));
    AURORA_TEST_REQUIRE(matches.has_value());
    AURORA_TEST_CHECK_EQ(matches->count(), std::size_t{3});
    AURORA_TEST_CHECK_FALSE(matches->truncated());
}

AURORA_TEST_CASE(regex_matches_align_to_whole_cells) {
    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "lt et");

    auto matches = searched(storage, pattern(U"[le]t"));
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 0, 2}, {0, 3, 5}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(regex_case_insensitivity_follows_the_same_switch) {
    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "ABC");

    auto loose = searched(storage, pattern(U"abc"));
    AURORA_TEST_REQUIRE(loose.has_value());
    AURORA_TEST_CHECK_EQ(loose->count(), std::size_t{1});

    auto strict = searched(storage, pattern(U"abc", true));
    AURORA_TEST_REQUIRE(strict.has_value());
    AURORA_TEST_CHECK_EQ(strict->count(), std::size_t{0});
}

AURORA_TEST_CASE(an_invalid_expression_is_reported_as_such_rather_than_zero_matches) {
    // 浮层据此报「表达式非法」并保留上一份结果；字面量档里同一个串是普通字符。
    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "(x)");

    AURORA_TEST_CHECK_FALSE(searched(storage, pattern(U"(")).has_value());
    auto literal_leg = searched(storage, literal(U"("));
    AURORA_TEST_REQUIRE(literal_leg.has_value());
    AURORA_TEST_CHECK_EQ(literal_leg->count(), std::size_t{1});
}

AURORA_TEST_CASE(zero_width_matches_produce_no_spans) {
    // `^` 与 `a*` 之类处处成立，产出的区间全是零宽：高亮会画出一片没有字符的格子。
    auto anchored = make_storage(10U, 1U);
    put_text(anchored, 0, 0, "abc");
    auto anchor = searched(anchored, pattern(U"^"));
    AURORA_TEST_REQUIRE(anchor.has_value());
    AURORA_TEST_CHECK_EQ(anchor->count(), std::size_t{0});

    auto absent = make_storage(10U, 1U);
    put_text(absent, 0, 0, "bcd");
    auto star = searched(absent, pattern(U"a*"));
    AURORA_TEST_REQUIRE(star.has_value());
    AURORA_TEST_CHECK_EQ(star->count(), std::size_t{0});
}

AURORA_TEST_CASE(an_astral_character_is_one_match_rather_than_two_surrogate_units) {
    // 正则档经 UTF-16：`.` 在代理对的两个单元上各成立一次，而两次都折算到同一个基础格。
    auto storage = make_storage(10U, 1U);
    put(storage, 0, 0, U'\U0001F600');  // CJK-LITERAL: cjk-fixture - 星象码点的代理对折叠就是被测事实
    put_text(storage, 0, 1, "x");

    auto matches = searched(storage, pattern(U"."));
    AURORA_TEST_REQUIRE(matches.has_value());
    const std::vector<RowSpan> expected{{0, 0, 1}, {0, 1, 2}};
    AURORA_TEST_CHECK(spans_of(*matches) == expected);
}

AURORA_TEST_CASE(regex_over_a_double_width_character_gives_the_same_span_as_the_literal_leg) {
    auto storage = make_storage(10U, 1U);
    put_wide(storage, 0, 0, U'\x4E2D');  // CJK-LITERAL: cjk-fixture - 两档同口径的整格对齐
    put_text(storage, 0, 2, "y");

    const std::vector<RowSpan> expected{{0, 0, 2}};
    auto regex_leg = searched(storage, pattern(U"\x4E2D"));  // CJK-LITERAL: cjk-fixture - 同上
    AURORA_TEST_REQUIRE(regex_leg.has_value());
    AURORA_TEST_CHECK(spans_of(*regex_leg) == expected);

    auto literal_leg = searched(storage, literal(U"\x4E2D"));  // CJK-LITERAL: cjk-fixture - 同上
    AURORA_TEST_REQUIRE(literal_leg.has_value());
    AURORA_TEST_CHECK(spans_of(*literal_leg) == expected);

    // 跨格的式子也一样：`中.` 在字面量档吃两个基础格（中 + `y`），正则档得给出同一段区间。
    // 延续格进了 UTF-16 缓冲的话，`.` 会把它当成一格而只覆盖到双宽字符自己。
    const std::vector<RowSpan> both_cells{{0, 0, 3}};
    auto crossing = searched(storage, pattern(U"\x4E2D."));  // CJK-LITERAL: cjk-fixture - 同上
    AURORA_TEST_REQUIRE(crossing.has_value());
    AURORA_TEST_CHECK(spans_of(*crossing) == both_cells);

    auto crossing_literal = searched(storage, literal(U"\x4E2Dy"));  // CJK-LITERAL: cjk-fixture - 同上
    AURORA_TEST_REQUIRE(crossing_literal.has_value());
    AURORA_TEST_CHECK(spans_of(*crossing_literal) == both_cells);
}

AURORA_TEST_CASE(advance_walks_the_table_and_wraps_at_both_ends) {
    // 首尾相连：`Enter` 连按找下一个是搜索框的基本手感，停在末位就再也绕不回前面那几个。
    auto storage = make_storage(10U, 2U);
    put_text(storage, 0, 0, "hit");
    put_text(storage, 1, 0, "hit hit");

    auto found = searched(storage, literal(U"hit"));
    AURORA_TEST_REQUIRE(found.has_value());
    SearchMatches matches{std::move(*found)};
    AURORA_TEST_REQUIRE_EQ(matches.count(), std::size_t{3});

    // 第一次 Enter 之前没有游标：`current()` 为空，浮层因此不会自作主张滚到第一处。
    AURORA_TEST_CHECK_FALSE(matches.current().has_value());

    const auto first = matches.advance(SearchDirection::Forward);
    AURORA_TEST_REQUIRE(first.has_value());
    AURORA_TEST_CHECK_EQ(first->row, std::size_t{0});
    const auto second = matches.advance(SearchDirection::Forward);
    AURORA_TEST_REQUIRE(second.has_value());
    const auto third = matches.advance(SearchDirection::Forward);
    AURORA_TEST_REQUIRE(third.has_value());
    AURORA_TEST_CHECK_EQ(third->row, std::size_t{1});

    const auto wrapped = matches.advance(SearchDirection::Forward);
    AURORA_TEST_CHECK(wrapped == first);
    const auto back_at_the_end = matches.advance(SearchDirection::Backward);
    AURORA_TEST_CHECK(back_at_the_end == third);

    auto other = searched(storage, literal(U"hit"));
    AURORA_TEST_REQUIRE(other.has_value());
    AURORA_TEST_CHECK(other->advance(SearchDirection::Backward) == third);
}

AURORA_TEST_CASE(translate_rows_keeps_the_cursor_on_the_match_it_pointed_at) {
    // 游标是**表内下标**，故折算丢掉头几段之后它必须跟着减；不减就指到别的内容上（用户并没选它）。
    auto storage = make_storage(10U, 5U);
    for (std::size_t row = 0U; row < storage.total_lines(); ++row) {
        put_text(storage, row, 0, "hit");
    }

    auto found = searched(storage, literal(U"hit"));
    AURORA_TEST_REQUIRE(found.has_value());
    SearchMatches matches{std::move(*found)};
    AURORA_TEST_REQUIRE_EQ(matches.count(), std::size_t{5});
    for (std::size_t step = 0U; step < 3U; ++step) {
        (void)matches.advance(SearchDirection::Forward);  // 游标停在第 2 行那一段上
    }

    matches.translate_rows(1);
    AURORA_TEST_CHECK_EQ(matches.count(), std::size_t{4});
    AURORA_TEST_CHECK(matches.cursor() == std::optional<std::size_t>{1});
    const auto current = matches.current();
    AURORA_TEST_REQUIRE(current.has_value());
    AURORA_TEST_CHECK_EQ(current->row, std::size_t{1});  // 内容上移一行，那一段随之从行 2 到行 1
}

AURORA_TEST_CASE(translate_rows_clears_the_cursor_when_its_own_match_is_dropped) {
    // 那一段用户并没选中，硬指下一段会把游标挪到别的内容上。
    auto storage = make_storage(10U, 3U);
    put_text(storage, 0, 0, "hit");
    put_text(storage, 1, 0, "hit");
    put_text(storage, 2, 0, "hit");

    auto found = searched(storage, literal(U"hit"));
    AURORA_TEST_REQUIRE(found.has_value());
    SearchMatches matches{std::move(*found)};
    (void)matches.advance(SearchDirection::Forward);  // 游标在第 0 行那一段上

    matches.translate_rows(2);
    AURORA_TEST_CHECK_FALSE(matches.cursor().has_value());
    AURORA_TEST_CHECK_FALSE(matches.current().has_value());
}

AURORA_TEST_CASE(translate_rows_moves_every_span_down_on_a_negative_shift) {
    // 顶部补空白与无历史的上滚让内容整体下移，顶边计数可为负。
    auto storage = make_storage(10U, 1U);
    put_text(storage, 0, 0, "hit");

    auto found = searched(storage, literal(U"hit"));
    AURORA_TEST_REQUIRE(found.has_value());
    SearchMatches matches{std::move(*found)};
    matches.translate_rows(-3);

    const std::vector<RowSpan> expected{{3, 0, 3}};
    AURORA_TEST_CHECK(spans_of(matches) == expected);
}

AURORA_TEST_CASE(translate_rows_by_zero_leaves_the_table_alone) {
    // 界面腿每帧都调这一句，顶边没动时不该重排整表。
    auto storage = make_storage(10U, 2U);
    put_text(storage, 0, 0, "hit");
    put_text(storage, 1, 0, "hit");

    auto found = searched(storage, literal(U"hit"));
    AURORA_TEST_REQUIRE(found.has_value());
    SearchMatches matches{std::move(*found)};
    const auto before = spans_of(matches);
    matches.translate_rows(0);
    AURORA_TEST_CHECK(spans_of(matches) == before);
}

}  // namespace borealis::test_cases::utest_search
