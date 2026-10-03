/// 测试类型: unit
/// 目标单元: include/borealis/ui/grid_size_debounce.h + src/ui/grid_size_debounce.cpp
/// 测试说明: 「期望行列 → 去抖后一次下发」的合并表（`SPEC.FEAT.XFER.01` 的去抖腿，裁决 7.47⑩ /
///           7.48③）。时刻一律作入参，故静默窗口的三条行为（尾沿重排、到点一次发全部 pane、净
///           变化为零不发）可在无调度器环境逐条断言；`next_deadline` 是调度侧唯一需要读的数。

#include <chrono>
#include <utility>
#include <vector>

#include "borealis/ui/grid_size_debounce.h"
#include "borealis/ui/pane_tree.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_grid_size_debounce {

namespace {

using borealis::ui::GridSize;
using borealis::ui::GridSizeDebounce;
using borealis::ui::Moment;
using borealis::ui::PaneId;
using borealis::ui::kGridResizeQuietPeriod;
using namespace std::chrono_literals;

/// @brief 起算点取时钟零点：本件只做差值算术，绝对时刻无关。
constexpr Moment kNow{};

[[nodiscard]] auto at(std::chrono::milliseconds offset) -> Moment { return kNow + offset; }

constexpr PaneId kLeft = 1;
constexpr PaneId kRight = 2;

}  // namespace

AURORA_TEST_CASE(nothing_is_emitted_before_the_quiet_window_closes) {
    GridSizeDebounce debounce;
    debounce.request(kLeft, GridSize{120U, 30U}, at(0ms));

    AURORA_TEST_CHECK_TRUE(debounce.due(at(49ms)).empty());
    AURORA_TEST_CHECK_TRUE(debounce.has_pending());  // 未结算：期望值还得留着，否则这次改动就丢了

    const auto dispatched = debounce.due(at(50ms));
    AURORA_TEST_REQUIRE_EQ(dispatched.size(), 1U);
    AURORA_TEST_CHECK_EQ(dispatched[0].first, kLeft);
    AURORA_TEST_CHECK(dispatched[0].second == GridSize{120U, 30U});
    AURORA_TEST_CHECK_FALSE(debounce.has_pending());
}

AURORA_TEST_CASE(the_window_is_pushed_by_every_later_request) {
    // 尾沿去抖：连续拖拽期间每次请求都把窗口推到「此刻 + 50 ms」，于是不停手就不下发。
    GridSizeDebounce debounce;
    debounce.request(kLeft, GridSize{100U, 30U}, at(0ms));
    AURORA_TEST_CHECK_TRUE(debounce.due(at(40ms)).empty());

    debounce.request(kLeft, GridSize{90U, 30U}, at(40ms));
    AURORA_TEST_CHECK_TRUE(debounce.due(at(80ms)).empty());  // 距最后一次请求才 40 ms

    const auto dispatched = debounce.due(at(90ms));
    AURORA_TEST_REQUIRE_EQ(dispatched.size(), 1U);
    AURORA_TEST_CHECK(dispatched[0].second == GridSize{90U, 30U});  // 后到的覆盖先到的
}

AURORA_TEST_CASE(one_window_dispatches_every_changed_pane_at_once) {
    // 同一帧里所有 pane 一起变（裁决 7.47⑩）：产出按 pane 标识升序，断言因此不依赖插入次序。
    GridSizeDebounce debounce;
    debounce.request(kRight, GridSize{40U, 20U}, at(0ms));
    debounce.request(kLeft, GridSize{80U, 24U}, at(0ms));
    debounce.request(kLeft, GridSize{82U, 24U}, at(10ms));

    const auto dispatched = debounce.due(at(100ms));
    AURORA_TEST_REQUIRE_EQ(dispatched.size(), 2U);
    AURORA_TEST_CHECK_EQ(dispatched[0].first, kLeft);
    AURORA_TEST_CHECK(dispatched[0].second == GridSize{82U, 24U});
    AURORA_TEST_CHECK_EQ(dispatched[1].first, kRight);
    AURORA_TEST_CHECK(dispatched[1].second == GridSize{40U, 20U});
}

AURORA_TEST_CASE(a_size_that_ends_back_where_it_started_dispatches_nothing) {
    // A → B → A 一趟净变化为零：来回拖把手不该让对端重排整屏（本件存两张表的全部理由）。
    GridSizeDebounce debounce;
    debounce.request(kLeft, GridSize{100U, 30U}, at(0ms));
    AURORA_TEST_CHECK_FALSE(debounce.due(at(50ms)).empty());

    debounce.request(kLeft, GridSize{80U, 30U}, at(100ms));
    debounce.request(kLeft, GridSize{100U, 30U}, at(140ms));
    AURORA_TEST_CHECK_TRUE(debounce.due(at(190ms)).empty());
    AURORA_TEST_CHECK_FALSE(debounce.has_pending());  // 期望表照样结算清空，不留待下一窗
}

AURORA_TEST_CASE(an_unchanged_pane_does_not_rider_on_the_dispatched_one) {
    // 同窗两 pane，其中一个绕回原值：只发真变了的那一个。
    GridSizeDebounce debounce;
    debounce.request(kLeft, GridSize{100U, 30U}, at(0ms));
    debounce.request(kRight, GridSize{60U, 24U}, at(0ms));
    AURORA_TEST_REQUIRE_EQ(debounce.due(at(50ms)).size(), 2U);

    debounce.request(kLeft, GridSize{90U, 30U}, at(100ms));
    debounce.request(kRight, GridSize{60U, 24U}, at(110ms));  // 布局重算又给了一次相同的期望值
    const auto dispatched = debounce.due(at(160ms));
    AURORA_TEST_REQUIRE_EQ(dispatched.size(), 1U);
    AURORA_TEST_CHECK_EQ(dispatched[0].first, kLeft);
}

AURORA_TEST_CASE(a_new_pane_gets_its_initial_size_once) {
    // `SPEC.FEAT.XFER.01` 的「分屏初始挂载下发一次」：没有已下发记录的 pane 恒算变化。
    GridSizeDebounce debounce;
    debounce.request(kLeft, GridSize{80U, 24U}, at(0ms));
    AURORA_TEST_REQUIRE_EQ(debounce.due(at(50ms)).size(), 1U);

    debounce.request(kLeft, GridSize{80U, 24U}, at(100ms));
    AURORA_TEST_CHECK_TRUE(debounce.due(at(150ms)).empty());
}

AURORA_TEST_CASE(forgetting_a_pane_clears_both_records) {
    // pane 标识被复用时，残留的「已下发」会让新 pane 少发一次初始尺寸。
    GridSizeDebounce debounce;
    debounce.request(kLeft, GridSize{80U, 24U}, at(0ms));
    AURORA_TEST_REQUIRE_EQ(debounce.due(at(50ms)).size(), 1U);

    debounce.forget(kLeft);
    debounce.request(kLeft, GridSize{80U, 24U}, at(100ms));
    AURORA_TEST_REQUIRE_EQ(debounce.due(at(150ms)).size(), 1U);
}

AURORA_TEST_CASE(only_pending_requests_have_a_deadline) {
    GridSizeDebounce debounce;
    AURORA_TEST_CHECK_FALSE(debounce.next_deadline().has_value());

    debounce.request(kLeft, GridSize{80U, 24U}, at(10ms));
    AURORA_TEST_REQUIRE(debounce.next_deadline().has_value());
    AURORA_TEST_CHECK(*debounce.next_deadline() == at(10ms + kGridResizeQuietPeriod));

    static_cast<void>(debounce.due(at(60ms)));
    AURORA_TEST_CHECK_FALSE(debounce.next_deadline().has_value());
}

}  // namespace borealis::test_cases::utest_grid_size_debounce
