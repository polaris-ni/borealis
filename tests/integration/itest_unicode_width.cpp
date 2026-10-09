/// 测试类型: integration
/// 目标单元: include/borealis/term/width.h + include/borealis/term/terminal.h（宽度判定的端到端接缝）
/// 测试说明: 真实字节流经「UTF-8 编码 → 解码 → VT 解析 → 状态机 → 网格」验证 SPEC.FEAT.TERM.08 的
///           验收线：CJK 双宽占两格并带延续格、同一份含 Ambiguous 字符的输出在默认（窄）与覆盖
///           （宽）口径下分别按单宽/双宽排布且光标列位与占位一致、行末的双宽字符整体换行不留
///           半格、combining 并入前一个基础格且不占格不推进光标（含落在双宽前半格、覆盖即清除、
///           行首与行末的边界）。判定表在框架侧（aurora/core/unicode_width.h，裁决 7.20）。

#include <cstddef>
#include <cstdint>
#include <string>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/term/terminal.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"
#include "support/terminal_feed.h"
#include <memory>

namespace borealis::test_cases::itest_unicode_width {

namespace {

using borealis::grid::Cell;
using borealis::grid::Row;
using borealis::term::AmbiguousWidth;
using borealis::term::Terminal;
using borealis::term::UnicodeWidthPolicy;

/// CJK 汉字：East Asian Width 为 W，恒双宽（源码保持 ASCII，故写转义）。
constexpr char32_t kHan = U'\x4E2D';
/// `±`：East Asian Width 为 Ambiguous，占几格随 profile 口径变。
constexpr char32_t kAmbiguous = U'\x00B1';
/// 组合重音符：General_Category Mn，零宽。
constexpr char32_t kAcute = U'\x0301';

auto real_width = std::make_shared<borealis::term::UnicodeWidthPolicy>();

/// @brief 建一台 10 列 × 3 行、scrollback 5 行的终端。
[[nodiscard]] auto make_terminal(AmbiguousWidth ambiguous) -> Terminal {
    Terminal terminal{10, 3, 5, real_width};
    terminal.set_ambiguous_width(ambiguous);
    return terminal;
}

/// @brief 视口某格。
[[nodiscard]] auto cell_at(Terminal &term, std::size_t row, std::size_t column) -> const Cell & {
    return term.active_grid().visible_line(row).cell(column);
}

/// @brief 并入视口某格的零宽码点串。
[[nodiscard]] auto marks_of(Terminal &term, std::size_t row, std::size_t column) -> std::u32string {
    const Row &line = term.active_grid().visible_line(row);
    std::u32string out;
    for (const auto &mark : line.combining(column)) {
        out.push_back(mark.code_point);
    }
    return out;
}

/// @brief 视口某行前 @p count 列的 ASCII 文本（行尾空白剥掉）。
[[nodiscard]] auto row_text(Terminal &term, std::size_t row, std::size_t count) -> std::string {
    const auto &line = term.active_grid().visible_line(row);
    std::string out;
    for (std::size_t column = 0; column < count && column < line.columns(); ++column) {
        out.push_back(static_cast<char>(line.cell(column).code_point));
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

}  // namespace

AURORA_TEST_CASE(cjk_occupies_two_cells_through_the_byte_chain) {
    std::u32string_view text{&kHan, 1};
    auto term = make_terminal(AmbiguousWidth::Narrow);
    AURORA_TEST_CHECK_EQ(borealis::testing::feed_text(term, text), std::uint64_t{0});  // 字节必须原样抵达

    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).code_point), static_cast<std::uint32_t>(kHan));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).width), std::uint32_t{2});
    AURORA_TEST_CHECK_TRUE(cell_at(term, 0, 1).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{2});
}

AURORA_TEST_CASE(ambiguous_glyph_layout_follows_the_profile) {
    // 同一份输出的字节只编一次，两份终端喂的是同一串（验收线的「同一份」是字面意思）。
    std::string bytes;
    AURORA_TEST_CHECK_EQ(borealis::term::encode_utf8(std::u32string_view{&kAmbiguous, 1}, bytes), std::size_t{0});
    bytes += 'x';

    auto narrow = make_terminal(AmbiguousWidth::Narrow);
    AURORA_TEST_CHECK_EQ(borealis::testing::feed_bytes(narrow, bytes), std::uint64_t{0});
    AURORA_TEST_CHECK_FALSE(cell_at(narrow, 0, 1).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(narrow, 0, 1).code_point), std::uint32_t{U'x'});
    AURORA_TEST_CHECK_EQ(narrow.cursor().column, std::size_t{2});

    auto wide = make_terminal(AmbiguousWidth::Wide);
    AURORA_TEST_CHECK_EQ(borealis::testing::feed_bytes(wide, bytes), std::uint64_t{0});
    AURORA_TEST_CHECK_TRUE(cell_at(wide, 0, 1).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(wide, 0, 2).code_point), std::uint32_t{U'x'});
    AURORA_TEST_CHECK_EQ(wide.cursor().column, std::size_t{3});
}

AURORA_TEST_CASE(glyph_at_right_margin_keeps_cell_boundaries) {
    // 行末放不下的双宽字符整体换行：不得只占最后一列留半格。窄口径下它单宽，正好落满行末。
    std::string bytes(9, 'a');
    AURORA_TEST_CHECK_EQ(borealis::term::encode_utf8(std::u32string_view{&kAmbiguous, 1}, bytes), std::size_t{0});

    auto narrow = make_terminal(AmbiguousWidth::Narrow);
    AURORA_TEST_CHECK_EQ(borealis::testing::feed_bytes(narrow, bytes), std::uint64_t{0});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(narrow, 0, 9).code_point),
                         static_cast<std::uint32_t>(kAmbiguous));
    AURORA_TEST_CHECK_EQ(narrow.cursor().row, std::size_t{0});

    auto wide = make_terminal(AmbiguousWidth::Wide);
    AURORA_TEST_CHECK_EQ(borealis::testing::feed_bytes(wide, bytes), std::uint64_t{0});
    AURORA_TEST_CHECK_EQ(row_text(wide, 0, 10), std::string(9, 'a'));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(wide, 0, 9).code_point), std::uint32_t{U' '});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(wide, 1, 0).code_point),
                         static_cast<std::uint32_t>(kAmbiguous));
    AURORA_TEST_CHECK_TRUE(cell_at(wide, 1, 1).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(wide.cursor().row, std::size_t{1});
    AURORA_TEST_CHECK_EQ(wide.cursor().column, std::size_t{2});
}

AURORA_TEST_CASE(combining_mark_joins_previous_base_cell) {
    const std::u32string text{U'e', kAcute, U'x'};
    auto term = make_terminal(AmbiguousWidth::Narrow);
    AURORA_TEST_CHECK_EQ(borealis::testing::feed_text(term, text), std::uint64_t{0});

    // 零宽既不长出新格也不推进光标：三码点只占两格。
    AURORA_TEST_CHECK(marks_of(term, 0, 0) == std::u32string{kAcute});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 1).code_point), std::uint32_t{U'x'});
    AURORA_TEST_CHECK(marks_of(term, 0, 1) == std::u32string{});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{2});
    AURORA_TEST_CHECK_EQ(row_text(term, 0, 10), std::string("ex"));
}

AURORA_TEST_CASE(combining_after_wide_glyph_attaches_to_the_leader) {
    const std::u32string text{kHan, kAcute};
    auto term = make_terminal(AmbiguousWidth::Narrow);
    static_cast<void>(borealis::testing::feed_text(term, text));

    // 光标停在双宽字符的后半格上：标记要挂到前半格，否则随延续格一起被丢弃。
    AURORA_TEST_CHECK(marks_of(term, 0, 0) == std::u32string{kAcute});
    AURORA_TEST_CHECK_TRUE(cell_at(term, 0, 1).is_wide_continuation());
    AURORA_TEST_CHECK(marks_of(term, 0, 1) == std::u32string{});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{2});
}

AURORA_TEST_CASE(combining_with_no_base_cell_is_dropped) {
    // 行首左侧无基础格可并：丢弃，但链路必须继续（不得越界、也不得吞掉后续文本）。
    const std::u32string leading{kAcute};
    auto term = make_terminal(AmbiguousWidth::Narrow);
    static_cast<void>(borealis::testing::feed_text(term, leading));
    AURORA_TEST_CHECK(marks_of(term, 0, 0) == std::u32string{});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{0});

    static_cast<void>(borealis::testing::feed_text(term, std::u32string{U'a'}));
    AURORA_TEST_CHECK_EQ(row_text(term, 0, 10), std::string("a"));
    AURORA_TEST_CHECK(marks_of(term, 0, 0) == std::u32string{});
}

AURORA_TEST_CASE(combining_at_line_end_does_not_wrap) {
    // 写满行末后 pending_wrap 为真：零宽码点不得触发换行，仍并入行末那一格。
    std::u32string text(9, U'a');
    text += U'b';
    text += kAcute;
    auto term = make_terminal(AmbiguousWidth::Narrow);
    static_cast<void>(borealis::testing::feed_text(term, text));

    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{0});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{9});
    AURORA_TEST_CHECK(marks_of(term, 0, 9) == std::u32string{kAcute});
}

AURORA_TEST_CASE(combining_with_autowrap_off_attaches_to_margin_cell) {
    // DECAWM=off 时行末的写入不推进光标（下一个字符覆盖同格）：零宽码点并的仍是那一格，不得偏左。
    std::u32string text{U"\x1B[?7l"};
    text += std::u32string(9, U'a');
    text += U'b';
    text += kAcute;
    auto term = make_terminal(AmbiguousWidth::Narrow);
    static_cast<void>(borealis::testing::feed_text(term, text));

    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{9});
    AURORA_TEST_CHECK_EQ(marks_of(term, 0, 9), std::u32string{kAcute});
    AURORA_TEST_CHECK(marks_of(term, 0, 8).empty());
}

AURORA_TEST_CASE(overwriting_base_cell_clears_its_marks) {
    const std::u32string text{U'e', kAcute};
    auto term = make_terminal(AmbiguousWidth::Narrow);
    static_cast<void>(borealis::testing::feed_text(term, text));
    term.feed(U"\x1B[1;1Hz");  // 回左上角覆盖基础格

    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).code_point), std::uint32_t{U'z'});
    AURORA_TEST_CHECK(marks_of(term, 0, 0) == std::u32string{});
}

}  // namespace borealis::test_cases::itest_unicode_width
