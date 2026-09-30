/// 测试类型: unit
/// 目标单元: include/borealis/grid/storage.h + row.h + cell.h
/// 测试说明: 环形缓冲存储的滚动语义（不搬移行数据、溢出覆盖最旧）、区域滚动只动带内行、
///           容量上下限、列宽变更不 reflow、行的脏区间与占用上界、清空复位（架构 §4.2 / §4.3 /
///           §4.5 / §4.6）。

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

/// @brief 视口某行行首的记号字符，供「某行有没有被搬动」这类断言。
auto row_head(const Storage &storage, std::size_t row) -> char {
    return static_cast<char>(storage.visible_line(row).cell(0).code_point);
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

AURORA_TEST_CASE(scroll_region_up_shifts_band_only) {
    Storage storage(4, 4, 0);
    for (std::size_t row = 0; row < 4; ++row) {
        storage.visible_line(row).set(0, marker(static_cast<char32_t>(U'A' + row)));
        storage.visible_line(row).clear_dirty();  // 让脏标记只反映本次滚动的结果
    }

    storage.scroll_region_up(1, 2, 1);

    AURORA_TEST_CHECK_EQ(row_head(storage, 0), 'A');  // 带外行不动
    AURORA_TEST_CHECK_EQ(row_head(storage, 1), 'C');  // 带底内容上移到带顶
    AURORA_TEST_CHECK_EQ(row_head(storage, 2), ' ');  // 带底腾空
    AURORA_TEST_CHECK_EQ(row_head(storage, 3), 'D');
    // 整带都要标脏：行内容整体搬移后，逐列脏区间表达不出「全部变了」。
    AURORA_TEST_CHECK(storage.visible_line(1).dirty());
    AURORA_TEST_CHECK_EQ(storage.visible_line(1).dirty_left(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(storage.visible_line(1).dirty_right(), storage.columns());
    AURORA_TEST_CHECK_FALSE(storage.visible_line(0).dirty());
}

AURORA_TEST_CASE(scroll_region_down_drops_band_bottom) {
    Storage storage(4, 4, 0);
    for (std::size_t row = 0; row < 4; ++row) {
        storage.visible_line(row).set(0, marker(static_cast<char32_t>(U'A' + row)));
    }

    storage.scroll_region_down(1, 3, 2);

    AURORA_TEST_CHECK_EQ(row_head(storage, 0), 'A');
    AURORA_TEST_CHECK_EQ(row_head(storage, 1), ' ');  // 带顶插两行空
    AURORA_TEST_CHECK_EQ(row_head(storage, 2), ' ');
    AURORA_TEST_CHECK_EQ(row_head(storage, 3), 'B');  // 带底的 B 之上内容整体下移，C/D 被推出
}

AURORA_TEST_CASE(scrolled_out_band_row_stays_writable) {
    // 搬移后腾空的行必须仍是「满宽空白行」：行容器被移走内容后若留下零宽，
    // 下一次写入该行的列即越界（真实缺陷的回归线）。
    Storage storage(6, 3, 2);
    storage.scroll_region_up(0, 1, 1);
    storage.scroll_region_down(1, 2, 2);
    for (std::size_t row = 0; row < storage.visible_rows(); ++row) {
        AURORA_TEST_CHECK_EQ(storage.visible_line(row).columns(), std::size_t{6});
    }
    storage.visible_line(2).set(5, marker(U'Z'));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(storage.visible_line(2).cell(5).code_point),
                         std::uint32_t{U'Z'});
}

AURORA_TEST_CASE(scroll_down_pulls_history_back_without_moving) {
    Storage storage(4, 2, 3);
    storage.visible_line(0).set(0, marker(U'A'));
    const auto *marked_row = &storage.visible_line(0);  // 记下 A 行所占的物理槽位
    storage.scroll_up(1);                               // A 行进入 scrollback
    AURORA_TEST_CHECK_EQ(storage.total_lines(), std::size_t{3});

    storage.scroll_down(1);  // 收回一行历史：视口上界回退，行数据一格也不动

    AURORA_TEST_CHECK_EQ(storage.total_lines(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(row_head(storage, 0), 'A');
    AURORA_TEST_CHECK(&storage.visible_line(0) == marked_row);
}

AURORA_TEST_CASE(scroll_down_without_history_shifts_rows_down) {
    Storage storage(4, 3, 2);
    storage.visible_line(0).set(0, marker(U'A'));
    storage.visible_line(1).set(0, marker(U'B'));

    storage.scroll_down(1);

    AURORA_TEST_CHECK_EQ(storage.total_lines(), std::size_t{3});
    AURORA_TEST_CHECK_EQ(row_head(storage, 0), ' ');  // 顶部腾出的行仍可整行写入
    AURORA_TEST_CHECK_EQ(storage.visible_line(0).columns(), std::size_t{4});
    AURORA_TEST_CHECK_EQ(row_head(storage, 1), 'A');
    AURORA_TEST_CHECK_EQ(row_head(storage, 2), 'B');
}

}  // namespace borealis::test_cases::utest_grid_storage
