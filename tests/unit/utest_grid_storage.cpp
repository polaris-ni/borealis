/// 测试类型: unit
/// 目标单元: include/borealis/grid/storage.h + row.h + cell.h
/// 测试说明: 环形缓冲存储的滚动语义（不搬移行数据、溢出覆盖最旧）、容量上下限、列宽变更不
///           reflow、行的脏区间与占用上界、清空复位（架构 §4.2 / §4.3 / §4.5 / §4.6）。

#include <cstddef>
#include <cstdint>
#include <string>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_grid_storage {

namespace {

using borealis::grid::Cell;
using borealis::grid::kFlagBold;
using borealis::grid::kFlagWideContinuation;
using borealis::grid::kMaxScrollbackLimit;
using borealis::grid::Row;
using borealis::grid::Storage;

/// @brief 记号 cell：码点即列号，便于断言行内容是否被搬动。
auto marker(char32_t cp) -> Cell {
    Cell cell{};
    cell.code_point = cp;
    return cell;
}

/// @brief 把一行写成可识别的记号序列（第 i 列写 U+0041 + i）。
auto fill_marker_row(Row &row) -> void {
    for (std::size_t i = 0; i < row.columns(); ++i) {
        row.set(i, marker(static_cast<char32_t>(U'A' + i)));
    }
}

/// @brief 行的码点串，供失败消息定位。
auto text_of(const Row &row) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < row.columns(); ++i) {
        out += static_cast<char>(row.cell(i).code_point);
    }
    return out;
}

}  // namespace

AURORA_TEST_CASE(fresh_storage_has_visible_rows_only) {
    // 初始只有视口行；scrollback 是「溢出才产生」的，不预占行数。
    Storage storage(8, 3, 5);
    AURORA_TEST_CHECK_EQ(storage.total_lines(), std::size_t{3});
    AURORA_TEST_CHECK_EQ(storage.visible_rows(), std::size_t{3});
    AURORA_TEST_CHECK_EQ(storage.capacity(), std::size_t{8});
    AURORA_TEST_CHECK_EQ(storage.line(0).columns(), std::size_t{8});
}

AURORA_TEST_CASE(scroll_advances_window_without_moving_rows) {
    // 环形的核心收益：滚动只推进 zero，行数据留在原物理槽位，代价与行数无关。
    Storage storage(4, 2, 4);
    fill_marker_row(storage.visible_line(0));
    fill_marker_row(storage.visible_line(1));
    const auto before = text_of(storage.visible_line(0));
    const auto *address_before = &storage.visible_line(1);

    storage.scroll_up(1);

    // 原视口第二行整体上移到视口顶，内容不变（未被搬移、未被重写）。
    AURORA_TEST_CHECK_EQ(text_of(storage.visible_line(0)), before);
    AURORA_TEST_CHECK(&storage.visible_line(0) == address_before);
    // 新落到视口底的是空白行。
    AURORA_TEST_CHECK_EQ(text_of(storage.visible_line(1)), std::string("    "));
    AURORA_TEST_CHECK_EQ(storage.total_lines(), std::size_t{3});
}

AURORA_TEST_CASE(scrollback_overflows_by_discarding_oldest) {
    // 超出容量后覆盖最旧行，总行数停在「视口 + scrollback」。
    Storage storage(2, 1, 2);
    fill_marker_row(storage.visible_line(0));
    storage.scroll_up(10);
    AURORA_TEST_CHECK_EQ(storage.total_lines(), std::size_t{3});
    AURORA_TEST_CHECK_EQ(storage.capacity(), std::size_t{3});
    // 溢出 10 次后，最初那行早已不在缓冲里。
    AURORA_TEST_CHECK(text_of(storage.line(0)) != std::string("AB"));
}

AURORA_TEST_CASE(scrollback_limit_is_clamped) {
    // 容量上限 100,000 行（SPEC.FEAT.TERM.04）：零列构造只为验证钳制，不占实际内存。
    Storage storage(0, 2, kMaxScrollbackLimit * 2);
    AURORA_TEST_CHECK_EQ(storage.scrollback_limit(), kMaxScrollbackLimit);
    AURORA_TEST_CHECK_EQ(storage.capacity(), kMaxScrollbackLimit + std::size_t{2});
}

AURORA_TEST_CASE(set_columns_does_not_reflow) {
    // 裁决 7.5：已有行不重排，变窄即截断、变宽补空格。
    Storage storage(4, 1, 0);
    fill_marker_row(storage.visible_line(0));
    AURORA_TEST_CHECK_EQ(text_of(storage.visible_line(0)), std::string("ABCD"));

    storage.set_columns(2);
    AURORA_TEST_CHECK_EQ(storage.columns(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(text_of(storage.visible_line(0)), std::string("AB"));

    storage.set_columns(4);
    AURORA_TEST_CHECK_EQ(text_of(storage.visible_line(0)), std::string("AB  "));
}

AURORA_TEST_CASE(row_tracks_dirty_range_and_occupancy) {
    // 脏区间与占用上界是「跳过干净列 / 干净行」的依据（§4.2、§4.6）。
    Row row(10);
    AURORA_TEST_CHECK_FALSE(row.dirty());

    row.set(5, marker(U'X'));
    AURORA_TEST_CHECK(row.dirty());
    AURORA_TEST_CHECK_EQ(row.dirty_left(), std::size_t{5});
    AURORA_TEST_CHECK_EQ(row.dirty_right(), std::size_t{6});
    AURORA_TEST_CHECK_EQ(row.occupancy(), std::size_t{6});

    row.set(2, marker(U'Y'));
    AURORA_TEST_CHECK_EQ(row.dirty_left(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(row.dirty_right(), std::size_t{6});
    AURORA_TEST_CHECK_EQ(row.occupancy(), std::size_t{6});

    row.clear_dirty();
    AURORA_TEST_CHECK_FALSE(row.dirty());
    row.set(8, marker(U'Z'));
    AURORA_TEST_CHECK_EQ(row.dirty_left(), std::size_t{8});
    AURORA_TEST_CHECK_EQ(row.occupancy(), std::size_t{9});
}

AURORA_TEST_CASE(row_reset_clears_content_and_marks) {
    Row row(3);
    row.set(1, marker(U'X'));
    row.reset();
    AURORA_TEST_CHECK_FALSE(row.dirty());
    AURORA_TEST_CHECK_EQ(row.occupancy(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(row.cell(1).code_point), std::uint32_t{U' '});
}

AURORA_TEST_CASE(wide_cell_continuation_is_flagged) {
    // 双宽占位由写入方表达：延续格必须能被光标与复制识别并跳过。
    Cell head{};
    head.code_point = U'\x4E2D';
    head.width = 2;
    Cell tail{};
    tail.flags = kFlagWideContinuation;
    tail.width = 0;

    AURORA_TEST_CHECK_FALSE(head.is_wide_continuation());
    AURORA_TEST_CHECK(tail.is_wide_continuation());
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(head.width), std::uint32_t{2});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(tail.width), std::uint32_t{0});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(head.flags & kFlagBold), std::uint32_t{0});
}

AURORA_TEST_CASE(clear_returns_to_initial_state) {
    Storage storage(3, 2, 2);
    fill_marker_row(storage.visible_line(0));
    storage.scroll_up(3);
    AURORA_TEST_CHECK_EQ(storage.total_lines(), std::size_t{4});

    storage.clear();
    AURORA_TEST_CHECK_EQ(storage.total_lines(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(text_of(storage.visible_line(0)), std::string("   "));
    AURORA_TEST_CHECK_FALSE(storage.visible_line(0).dirty());
}

}  // namespace borealis::test_cases::utest_grid_storage
