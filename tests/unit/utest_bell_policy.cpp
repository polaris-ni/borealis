/// 测试类型: unit
/// 目标单元: include/borealis/session/bell.h + src/session/bell.cpp（无 au:: 类型）
/// 测试说明: 守住 BEL 呈现策略（`SPEC.FEAT.WS.04`，裁决 7.159）——三档语义（Off 忽略、Visual
///           只点角标、Audible 角标 + 铃声）、200 ms 全局节流、节流锚点只随**真正响过**的那一声
///           推进（被吞的 BEL 不得把窗口再往后推）、非正间隔每次都响。时刻全部用固定
///           `steady_clock::time_point` 喂入，不睡表。

#include <chrono>

#include "borealis/config/settings.h"
#include "borealis/session/bell.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_bell_policy {

namespace {

using borealis::config::BellMode;
using borealis::session::BellAction;
using borealis::session::BellPolicy;

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr auto kStart = Clock::time_point{};

[[nodiscard]] auto at(int ms) -> Clock::time_point {
    return kStart + milliseconds{ms};
}

}  // namespace

AURORA_TEST_CASE(off_ignores_every_bell) {
    BellPolicy policy;
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Off, at(0)) == BellAction::None);
    // Off 档连角标都不点：设置面板上「完全忽略」的文案就是这条。
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Off, at(1000)) == BellAction::None);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Off, at(5000)) == BellAction::None);
}

AURORA_TEST_CASE(visual_only_badges_and_leaves_the_tone_window_untouched) {
    BellPolicy policy;
    // 视觉档永不响铃，也不该占用节流窗口——用户从视觉改到可听时，第一条 BEL 就该有声。
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Visual, at(0)) == BellAction::Badge);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Visual, at(1)) == BellAction::Badge);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(2)) ==
                          BellAction::BadgeAndTone);
}

AURORA_TEST_CASE(audible_tones_once_then_throttles_for_the_window) {
    BellPolicy policy;
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(0)) ==
                          BellAction::BadgeAndTone);
    // 边界：恰好等于最小间隔即放行，只严格小于才吞（否则 200 ms 的常数在整数毫秒上永远差一）。
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(199)) == BellAction::Badge);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(200)) ==
                          BellAction::BadgeAndTone);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(399)) == BellAction::Badge);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(400)) ==
                          BellAction::BadgeAndTone);
}

AURORA_TEST_CASE(throttled_bells_do_not_push_the_anchor_forward) {
    BellPolicy policy;
    AURORA_TEST_REQUIRE_TRUE(policy.decide(BellMode::Audible, at(0)) ==
                             BellAction::BadgeAndTone);
    // 窗口内连打三条：锚点仍是 0，于是 200 ms 一到就放行，而不是被喂成 500 ms 之后。
    for (const int ms : {50, 100, 150}) {
        AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(ms)) == BellAction::Badge);
    }
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(200)) ==
                          BellAction::BadgeAndTone);
}

AURORA_TEST_CASE(volatile_mode_changes_take_effect_immediately) {
    BellPolicy policy;
    AURORA_TEST_REQUIRE_TRUE(policy.decide(BellMode::Audible, at(0)) ==
                             BellAction::BadgeAndTone);
    // 改到 Visual 后角标照点、铃声停止；改回 Audible 仍在窗口内，故先只点角标。
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Visual, at(50)) == BellAction::Badge);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(100)) == BellAction::Badge);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Off, at(150)) == BellAction::None);
    AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(200)) ==
                          BellAction::BadgeAndTone);
}

AURORA_TEST_CASE(non_positive_interval_rings_every_time) {
    // 0 与负值都按「不节流」解：`now - last < interval` 对非正 interval 恒假。
    for (const auto interval : {milliseconds{0}, milliseconds{-1}}) {
        BellPolicy policy{interval};
        AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(0)) ==
                              BellAction::BadgeAndTone);
        AURORA_TEST_CHECK_TRUE(policy.decide(BellMode::Audible, at(0)) ==
                              BellAction::BadgeAndTone);
    }
}

}  // namespace borealis::test_cases::utest_bell_policy
