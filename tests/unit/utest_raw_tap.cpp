/// 测试类型: unit
/// 目标单元: include/borealis/session/raw_tap.h + src/session/raw_tap.cpp（无 au:: 类型）
/// 测试说明: 守住串口监视采集腿的环口径（稿 codespec/UI_SERIAL_MONITOR.draft.md D3①／D4①）——
///           到达序、按**字节**逐出最旧（不按条数）、单段超上限整条拒收、丢弃累计、空段不入环、
///           clear 把环与留痕一起归零、快照交出量恒不超上限。
///           「武装态零成本」与 RX／TX 两笔记痕属 utest_session（批 2）。

#include "borealis/session/raw_tap.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_raw_tap {

namespace {

[[nodiscard]] auto bytes_of(std::string_view text) -> std::vector<std::byte> {
    auto out = std::vector<std::byte>{};
    out.reserve(text.size());
    for (const auto character : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return out;
}

/// @brief 记一笔的简写：把一段字面量按 `std::byte` 交进环里。
auto note(session::RawTap &tap, session::RawDirection direction, std::int64_t timestamp_ms,
          std::string_view text) -> void {
    const auto payload = bytes_of(text);
    tap.record(direction, timestamp_ms, {payload.data(), payload.size()});
}

[[nodiscard]] auto text_of(const std::vector<std::byte> &payload) -> std::string {
    auto out = std::string{};
    out.reserve(payload.size());
    for (const auto byte : payload) {
        out += static_cast<char>(std::to_integer<unsigned char>(byte));
    }
    return out;
}

}  // namespace

AURORA_TEST_CASE(runs_are_kept_in_arrival_order_with_their_direction_and_moment) {
    auto tap = session::RawTap{};
    note(tap, session::RawDirection::Transmitted, 1000, "AT");
    note(tap, session::RawDirection::Received, 1500, "OK");

    const auto snapshot = tap.snapshot();
    AURORA_TEST_REQUIRE_EQ(snapshot.size(), 2U);
    // 最旧在前：面板那侧的读序就是设备流量的到达序，本件不重排。
    AURORA_TEST_CHECK_TRUE(snapshot[0].direction == session::RawDirection::Transmitted);
    AURORA_TEST_CHECK_EQ(snapshot[0].timestamp_ms, 1000);
    AURORA_TEST_CHECK_EQ(text_of(snapshot[0].bytes), "AT");
    AURORA_TEST_CHECK_TRUE(snapshot[1].direction == session::RawDirection::Received);
    AURORA_TEST_CHECK_EQ(snapshot[1].timestamp_ms, 1500);
    AURORA_TEST_CHECK_EQ(text_of(snapshot[1].bytes), "OK");
    AURORA_TEST_CHECK_EQ(tap.total_bytes(), 4U);
    AURORA_TEST_CHECK_EQ(tap.run_count(), 2U);
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 0U);
}

AURORA_TEST_CASE(empty_runs_never_enter_the_ring) {
    auto tap = session::RawTap{};
    note(tap, session::RawDirection::Received, 1, "");
    AURORA_TEST_CHECK_EQ(tap.run_count(), 0U);
    AURORA_TEST_CHECK_EQ(tap.total_bytes(), 0U);
    // 空段既不占位也不计丢弃：留痕那句「已丢弃 N 字节」说的必须是真丢了的字节。
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 0U);
}

AURORA_TEST_CASE(eviction_is_measured_in_bytes_and_always_takes_the_oldest) {
    auto tap = session::RawTap{10};
    note(tap, session::RawDirection::Received, 1, "AAAA");   // 4
    note(tap, session::RawDirection::Received, 2, "BBBB");   // 8
    note(tap, session::RawDirection::Received, 3, "CCC");    // 11 → 逐出最旧那条（4 字节）

    const auto snapshot = tap.snapshot();
    AURORA_TEST_REQUIRE_EQ(snapshot.size(), 2U);
    AURORA_TEST_CHECK_EQ(text_of(snapshot[0].bytes), "BBBB");  // 最旧那条已让位
    AURORA_TEST_CHECK_EQ(snapshot[0].timestamp_ms, 2);
    AURORA_TEST_CHECK_EQ(text_of(snapshot[1].bytes), "CCC");
    AURORA_TEST_CHECK_EQ(tap.total_bytes(), 7U);
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 4U);  // 逐出的字节数即留痕那句「已丢弃 N 字节」的数
}

AURORA_TEST_CASE(eviction_ignores_run_count_so_device_rhythm_cannot_shrink_the_view) {
    // D4① 的另一半：按条数封顶就等于按设备节奏封顶（F3：一次突发可到 32 KiB，慢速设备 10 字节一批）。
    // 这里 10 条 1 字节的 run 挤不进 6 字节的上限，让位的必须是最早那 4 个字节。
    auto tap = session::RawTap{6};
    const auto single = bytes_of("x");
    for (std::int64_t index = 0; index < 10; ++index) {
        tap.record(session::RawDirection::Received, index, {single.data(), single.size()});
    }
    const auto snapshot = tap.snapshot();
    AURORA_TEST_CHECK_EQ(snapshot.size(), 6U);
    AURORA_TEST_REQUIRE_EQ(snapshot.front().timestamp_ms, 4);  // 0..3 已让位
    AURORA_TEST_CHECK_EQ(snapshot.back().timestamp_ms, 9);
    AURORA_TEST_CHECK_EQ(tap.total_bytes(), 6U);
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 4U);
}

AURORA_TEST_CASE(a_run_bigger_than_the_ring_is_refused_wholesale_and_counted) {
    auto tap = session::RawTap{8};
    note(tap, session::RawDirection::Received, 1, "ABCDEFG");     // 7，容得下
    note(tap, session::RawDirection::Received, 2, "0123456789");  // 10，整条超上限

    const auto snapshot = tap.snapshot();
    AURORA_TEST_REQUIRE_EQ(snapshot.size(), 1U);
    AURORA_TEST_CHECK_EQ(text_of(snapshot[0].bytes), "ABCDEFG");
    AURORA_TEST_CHECK_EQ(tap.total_bytes(), 7U);
    // 截一半留存就是伪造设备节奏，故整条拒收；但**全长**计入丢弃，留痕那句不轻描淡写。
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 10U);
}

AURORA_TEST_CASE(the_newest_run_survives_even_when_it_fills_the_ring_alone) {
    auto tap = session::RawTap{4};
    note(tap, session::RawDirection::Received, 1, "AAAA");
    note(tap, session::RawDirection::Transmitted, 2, "BBBB");
    // 逐出循环不得把刚进来这条自己也吃掉（`runs_.size() > 1` 那条守卫）。
    AURORA_TEST_CHECK_EQ(tap.run_count(), 1U);
    AURORA_TEST_CHECK_EQ(tap.total_bytes(), 4U);
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 4U);
    const auto snapshot = tap.snapshot();
    AURORA_TEST_REQUIRE_EQ(snapshot.size(), 1U);
    AURORA_TEST_CHECK_TRUE(snapshot[0].direction == session::RawDirection::Transmitted);
}

AURORA_TEST_CASE(clear_empties_both_the_ring_and_the_drop_notice) {
    auto tap = session::RawTap{4};
    note(tap, session::RawDirection::Received, 1, "AAAA");
    note(tap, session::RawDirection::Received, 2, "BBBB");  // 逐出上一条，留痕 4
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 4U);

    tap.clear();
    AURORA_TEST_CHECK_TRUE(tap.snapshot().empty());
    AURORA_TEST_CHECK_EQ(tap.run_count(), 0U);
    AURORA_TEST_CHECK_EQ(tap.total_bytes(), 0U);
    // 清空之后那一屏是新的开始：再挂着「已丢弃 4 字节」就是在说一屏已经看不到的数据。
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 0U);
}

AURORA_TEST_CASE(a_snapshot_never_exceeds_the_ring_bound) {
    auto tap = session::RawTap{32};
    std::size_t fed_bytes = 0;
    std::size_t held_bytes = 0;
    for (std::uint64_t index = 0; index < 200; ++index) {
        const auto payload = bytes_of(std::string(1 + index % 7, static_cast<char>('a' + index % 26)));
        fed_bytes += payload.size();
        tap.record(session::RawDirection::Received, static_cast<std::int64_t>(index),
                   {payload.data(), payload.size()});
        held_bytes = 0;
        for (const auto &run : tap.snapshot()) {
            held_bytes += run.bytes.size();
        }
        AURORA_TEST_CHECK_LE(held_bytes, tap.max_bytes());  // 交出量恒 ≤ 上限：不需要第二个数
        AURORA_TEST_CHECK_EQ(held_bytes, tap.total_bytes());  // 计数与实交出的字节恒等
    }
    // 累计丢弃 + 现存 == 全部进来过的字节：一个都不许悄悄消失。
    AURORA_TEST_CHECK_EQ(held_bytes + tap.dropped_bytes(), fed_bytes);
}

AURORA_TEST_CASE(two_threads_recording_land_every_run_exactly_once) {
    AURORA_TEST_REQUIRE_THREADS();
    // 采集腿自有 mutex_、不借 Session::mutex_（稿 §5 第一条）：读线程与主线程各记自己那方向，
    // 条数与字节数都必须精确，否则面板上的计数会说谎。
    auto tap = session::RawTap{4096};
    const auto worker = [&tap](session::RawDirection direction) {
        for (std::uint64_t index = 0; index < 200; ++index) {
            const auto payload = bytes_of("AB");
            tap.record(direction, static_cast<std::int64_t>(index), {payload.data(), payload.size()});
        }
    };
    auto reader = std::thread{worker, session::RawDirection::Received};
    auto writer = std::thread{worker, session::RawDirection::Transmitted};
    reader.join();
    writer.join();
    AURORA_TEST_CHECK_EQ(tap.run_count(), 400U);
    AURORA_TEST_CHECK_EQ(tap.total_bytes(), 800U);
    AURORA_TEST_CHECK_EQ(tap.dropped_bytes(), 0U);
}

}  // namespace borealis::test_cases::utest_raw_tap
