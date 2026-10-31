/// 测试类型: unit
/// 目标单元: include/borealis/ui/input_latency.h + src/ui/input_latency.cpp
/// 测试说明: 「键入 → 首次上屏」的有界样本窗口与它的 P95 读数（`SPEC.NF.PERF.01` 的量测腿，
///           屏 1 状态栏那格 `输入延迟 P95 12 ms` 的数据源，裁决 7.186①②③④）。时刻一律作入参，
///           故四条口径都能在无时钟、无控件的环境里逐值断言：一笔键入只配**首个**上屏、起点取
///           **最近一次**键入、超过归因上限的配对丢弃且单独计数、窗口淘汰的是**最旧**而不是最小。
///           最近秩法（`rank = ceil(0.95 × N)`）另有一例按 100 笔喂到第 95 小，把「第几小」写成整数判据。

#include <chrono>
#include <optional>

#include "borealis/ui/input_latency.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_input_latency {

namespace {

using borealis::ui::InputLatency;
using namespace std::chrono_literals;

/// @brief 起算点取时钟零点：本件只做差值算术，绝对时刻无关。
constexpr auto kNow = std::chrono::steady_clock::time_point{};

[[nodiscard]] auto at(std::chrono::milliseconds offset) -> std::chrono::steady_clock::time_point {
    return kNow + offset;
}

/// @brief 喂一笔「键入 → 上屏」的完整配对，返回件是否结算成样本。
[[nodiscard]] auto feed(InputLatency &meter, std::chrono::milliseconds start, std::chrono::milliseconds end)
    -> bool {
    meter.note_input_issued(at(start));
    return meter.note_output(at(end));
}

}  // namespace

AURORA_TEST_CASE(nothing_is_read_until_a_pair_settles) {
    InputLatency meter;
    AURORA_TEST_CHECK_FALSE(meter.p95(at(0ms)).has_value());  // 空窗口

    meter.note_input_issued(at(0ms));
    AURORA_TEST_CHECK_TRUE(meter.has_pending_input());
    AURORA_TEST_CHECK_FALSE(meter.p95(at(10ms)).has_value());  // 欠着一个上屏，不拿起点当样本
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 0U);
}

AURORA_TEST_CASE(a_settled_pair_reads_back_its_own_elapsed) {
    InputLatency meter;
    AURORA_TEST_CHECK_TRUE(feed(meter, 0ms, 12ms));
    AURORA_TEST_CHECK_FALSE(meter.has_pending_input());
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 1U);
    // N=1 时 ceil(0.95 × 1) = 1 ⇒ 秩就是那一笔本身。
    AURORA_TEST_CHECK_EQ(meter.p95(at(12ms)).value_or(-1ms), 12ms);
}

AURORA_TEST_CASE(only_the_first_output_after_a_keystroke_becomes_a_sample) {
    InputLatency meter;
    AURORA_TEST_CHECK_TRUE(feed(meter, 0ms, 12ms));
    // 连续刷屏时上屏帧远多于键入笔数：第二次及以后的 `note_output` 不配起点、不灌样本，
    // 否则读数测的是「帧间隔」而不是「按键入的回应」。
    AURORA_TEST_CHECK_FALSE(meter.note_output(at(30ms)));
    AURORA_TEST_CHECK_FALSE(meter.note_output(at(40ms)));
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 1U);
    AURORA_TEST_CHECK_EQ(meter.p95(at(40ms)).value_or(-1ms), 12ms);
}

AURORA_TEST_CASE(the_latest_keystroke_moves_the_start_point) {
    InputLatency meter;
    meter.note_input_issued(at(0ms));
    meter.note_input_issued(at(8ms));  // 连打/自动重复：后到的覆盖先到的
    AURORA_TEST_CHECK_TRUE(meter.note_output(at(20ms)));
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 1U);  // 两笔键入只配出一笔样本
    AURORA_TEST_CHECK_EQ(meter.p95(at(20ms)).value_or(-1ms), 12ms);
}

AURORA_TEST_CASE(a_pair_beyond_the_attribution_cap_is_dropped_and_counted) {
    InputLatency meter;
    AURORA_TEST_CHECK_FALSE(feed(meter, 0ms, 1001ms));  // 缺省归因上限 1 s
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 0U);
    AURORA_TEST_CHECK_EQ(meter.dropped_count(), 1U);
    AURORA_TEST_CHECK_FALSE(meter.has_pending_input());  // 丢弃即把起点一并清掉，不留挂账
    AURORA_TEST_CHECK_FALSE(meter.note_output(at(1200ms)));
    // 恰在上限那一笔仍然成立（比较是 `>` 而不是 `>=`）。
    AURORA_TEST_CHECK_TRUE(feed(meter, 2000ms, 3000ms));
    AURORA_TEST_CHECK_EQ(meter.dropped_count(), 1U);
}

AURORA_TEST_CASE(the_window_evicts_the_oldest_sample_not_the_smallest) {
    InputLatency meter(/*sample_cap=*/3U);
    // 喂的是「键入时刻 → 上屏时刻」那一对**绝对**时刻，样本值 = 两者之差（不是第二个入参本身）。
    AURORA_TEST_CHECK_TRUE(feed(meter, 0ms, 30ms));     // 样本 30
    AURORA_TEST_CHECK_TRUE(feed(meter, 100ms, 101ms));  // 样本 1
    AURORA_TEST_CHECK_TRUE(feed(meter, 200ms, 220ms));  // 样本 20
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 3U);
    AURORA_TEST_CHECK_TRUE(feed(meter, 300ms, 310ms));  // 样本 10，进来即淘汰最旧的 30
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 3U);
    // 窗口自此是 {1, 10, 20}：N=3 ⇒ 秩 = ceil(2.85) = 3 ⇒ 第 3 小 = 20 ms。按最小淘汰会读回 30。
    AURORA_TEST_CHECK_EQ(meter.p95(at(310ms)).value_or(-1ms), 20ms);
}

AURORA_TEST_CASE(the_readout_is_the_95th_smallest_of_a_full_window) {
    InputLatency meter;  // 缺省 100 笔
    for (std::size_t i = 0U; i < 100U; ++i) {
        const auto value = std::chrono::milliseconds(static_cast<long long>(i) + 1LL);
        AURORA_TEST_CHECK_TRUE(feed(meter, static_cast<std::chrono::milliseconds>(0ms),
                                    static_cast<std::chrono::milliseconds>(0ms) + value));
    }
    AURORA_TEST_REQUIRE_EQ(meter.sample_count(), 100U);
    AURORA_TEST_CHECK_EQ(meter.p95(at(1ms)).value_or(-1ms), 95ms);  // ceil(0.95 × 100) = 95
    // 第 101 笔把最旧那一笔（1 ms）挤出去，窗口变成 2..100 ⇒ 第 95 小随之上移到 96。
    AURORA_TEST_CHECK_TRUE(feed(meter, 0ms, 101ms));
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 100U);
    AURORA_TEST_CHECK_EQ(meter.p95(at(102ms)).value_or(-1ms), 96ms);
}

AURORA_TEST_CASE(the_readout_goes_stale_after_the_quiet_window) {
    InputLatency meter;
    AURORA_TEST_CHECK_TRUE(feed(meter, 0ms, 12ms));
    AURORA_TEST_CHECK_EQ(meter.p95(at(5012ms)).value_or(-1ms), 12ms);    // 恰在边界仍算新鲜
    AURORA_TEST_CHECK_FALSE(meter.p95(at(5013ms)).has_value());          // 样本时刻 12ms + 5000ms
    AURORA_TEST_CHECK_EQ(meter.sample_count(), 1U);                      // 过期只撤读数，不清窗口
    AURORA_TEST_CHECK_TRUE(feed(meter, 6000ms, 6005ms));                 // 一笔新样本重新让读数上屏
    // 窗口是 {12, 5}——过期只撤读数、不清窗口，故旧样本仍参与排序：N=2 ⇒ 秩 = ceil(1.9) = 2 ⇒ 第 2 小。
    AURORA_TEST_CHECK_EQ(meter.p95(at(6005ms)).value_or(-1ms), 12ms);
}

AURORA_TEST_CASE(a_reversed_pair_clamps_to_zero_instead_of_going_negative) {
    InputLatency meter;
    meter.note_input_issued(at(10ms));
    AURORA_TEST_CHECK_TRUE(meter.note_output(at(0ms)));  // 倒序只可能来自调用方：夹到 0
    AURORA_TEST_CHECK_EQ(meter.p95(at(10ms)).value_or(-1ms), 0ms);
    AURORA_TEST_CHECK_EQ(meter.dropped_count(), 0U);  // 夹零不是丢弃，可观测面不该多计一笔
}

AURORA_TEST_CASE(a_zero_cap_still_holds_one_sample) {
    InputLatency meter(/*sample_cap=*/0U);
    AURORA_TEST_CHECK_TRUE(feed(meter, 0ms, 30ms));
    AURORA_TEST_CHECK_TRUE(feed(meter, 100ms, 107ms));
    AURORA_TEST_REQUIRE_EQ(meter.sample_count(), 1U);
    AURORA_TEST_CHECK_EQ(meter.p95(at(107ms)).value_or(-1ms), 7ms);  // 0 视作 1，而非把窗口关死
}

}  // namespace borealis::test_cases::utest_input_latency
