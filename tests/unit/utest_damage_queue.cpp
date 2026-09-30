/// 测试类型: unit
/// 目标单元: include/borealis/session/damage_queue.h
/// 测试说明: 有界背压队列的合并语义与预算让出——相邻/相交区间归并、整屏脏覆盖已排队提交、
///           队满按合并处理且新行不丢、单帧消费到预算上限后让出、水位与合并计数
///           （SPEC.NF.PERF.06，计数供 SPEC.NF.RELI.01 面板）。

#include <cstddef>

#include "borealis/session/damage_queue.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_damage_queue {

namespace {

using borealis::session::DamageQueue;

}  // namespace

AURORA_TEST_CASE(adjacent_ranges_merge_into_one_commit) {
    DamageQueue queue{8, 4};
    queue.push_rows(0, 0);
    queue.push_rows(1, 2);
    const auto frame = queue.drain();
    AURORA_TEST_CHECK_EQ(frame.size(), 1U);
    AURORA_TEST_CHECK(frame[0].first_row == 0U && frame[0].last_row == 2U);
    AURORA_TEST_CHECK_FALSE(frame[0].full_screen);
    AURORA_TEST_CHECK_EQ(queue.stats().merges, 1U);
}

AURORA_TEST_CASE(distant_ranges_stay_separate_commits) {
    DamageQueue queue{8, 4};
    queue.push_rows(0, 0);
    queue.push_rows(2, 2);  // 与队尾差一行才相接，隔一行就仍是两条
    const auto frame = queue.drain();
    AURORA_TEST_CHECK_EQ(frame.size(), 2U);
    AURORA_TEST_CHECK_EQ(queue.stats().merges, 0U);
}

AURORA_TEST_CASE(full_screen_subsumes_queued_rows) {
    DamageQueue queue{8, 4};
    queue.push_rows(1, 1);
    queue.push_rows(2, 2);
    queue.push_full_screen();
    queue.push_rows(0, 0);  // 整屏脏已在队中，行级提交不再新增信息
    const auto frame = queue.drain();
    AURORA_TEST_CHECK_EQ(frame.size(), 1U);
    AURORA_TEST_CHECK_TRUE(frame[0].full_screen);
    AURORA_TEST_CHECK_EQ(queue.stats().merges, 3U);
}

AURORA_TEST_CASE(overflow_merges_into_tail_without_losing_rows) {
    DamageQueue queue{2, 8};
    queue.push_rows(0, 0);
    queue.push_rows(5, 5);
    queue.push_rows(7, 9);  // 队满：并进球尾而不是丢弃
    const auto stats = queue.stats();
    AURORA_TEST_CHECK_EQ(stats.overloads, 1U);
    AURORA_TEST_CHECK_EQ(stats.pending, 2U);
    AURORA_TEST_CHECK_LE(stats.peak_pending, 2U);
    const auto frame = queue.drain();
    AURORA_TEST_CHECK_EQ(frame.size(), 2U);
    AURORA_TEST_CHECK(frame[1].first_row == 5U && frame[1].last_row == 9U);
}

AURORA_TEST_CASE(per_frame_budget_stops_and_yields) {
    DamageQueue queue{8, 2};
    queue.push_rows(0, 0);
    queue.push_rows(2, 2);
    queue.push_rows(4, 4);
    AURORA_TEST_CHECK_EQ(queue.drain().size(), 2U);
    AURORA_TEST_CHECK_TRUE(queue.pending());
    AURORA_TEST_CHECK_EQ(queue.stats().yields, 1U);
    AURORA_TEST_CHECK_EQ(queue.drain().size(), 1U);
    AURORA_TEST_CHECK_EQ(queue.stats().yields, 1U);  // 第二次取空了队列，不再让出
}

AURORA_TEST_CASE(zero_budget_still_consumes_one_commit) {
    DamageQueue queue{8, 0};
    queue.push_rows(0, 0);
    queue.push_rows(2, 2);
    AURORA_TEST_CHECK_EQ(queue.drain().size(), 1U);
}

AURORA_TEST_CASE(watermark_tracks_backlog) {
    DamageQueue queue{16, 32};
    queue.push_rows(0, 0);
    queue.push_rows(2, 2);
    queue.push_rows(4, 4);
    const auto stats = queue.stats();
    AURORA_TEST_CHECK_EQ(stats.pending, 3U);
    AURORA_TEST_CHECK_EQ(stats.peak_pending, 3U);
    queue.drain();
    AURORA_TEST_CHECK_EQ(queue.stats().pending, 0U);
    AURORA_TEST_CHECK_EQ(queue.stats().peak_pending, 3U);  // 水位是历史最高，不随消费回落
}

}  // namespace borealis::test_cases::utest_damage_queue
