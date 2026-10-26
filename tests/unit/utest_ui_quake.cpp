/// 测试类型: unit
/// 目标单元: include/borealis/ui/quake.h + src/ui/quake.cpp（无 au:: 类型）
/// 测试说明: 守住 Quick Terminal 的两条纯逻辑口径（`SPEC.FEAT.WS.12`，裁决 7.160）——
///           ① 贴边矩形按**工作区**（不是整屏）折算、跨边居中落下贴边、尺寸按 scale 反算成
///              逻辑 dp 而原点保持物理像素（框架两口单位不同，稿 D2）、越界与怪值降级；
///           ② 显隐状态机的意图幂等与「热键 / 失焦」竞争：本件自己发起显隐后的过渡窗内必须
///              吞掉落焦事件（Tilix quake 模式记录在案的「隐藏后自己弹回」故障形态），过期后
///              失焦照常隐、开关关掉则永不隐。时刻全部固定喂入，不睡表。

#include <chrono>
#include <cmath>

#include "borealis/ui/quake.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_ui_quake {

namespace {

using borealis::ui::QuakeAnchor;
using borealis::ui::QuakeGeometryRequest;
using borealis::ui::QuakeIntent;
using borealis::ui::QuakeState;
using borealis::ui::QuakeToggle;
using borealis::ui::QuakeWorkArea;
using borealis::ui::quake_target_rect;

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr auto kStart = Clock::time_point{};

[[nodiscard]] auto at(int ms) -> Clock::time_point {
    return kStart + milliseconds{ms};
}

/// @brief 一块 1920×1080、原点 (0,0) 的工作区（缺省入参，个别用例改单字段以便读差分）。
[[nodiscard]] auto primary_area() -> QuakeGeometryRequest {
    return QuakeGeometryRequest{
        .work_area = QuakeWorkArea{.origin_x = 0.0, .origin_y = 0.0, .width = 1920.0, .height = 1080.0},
        .scale_factor = 1.0,
    };
}

[[nodiscard]] auto near(double actual, double expected) -> bool {
    return std::fabs(actual - expected) < 1e-9;
}

}  // namespace

AURORA_TEST_CASE(top_default_fills_the_width_and_halves_the_height) {
    const auto target = quake_target_rect(primary_area());
    // 缺省档＝下拉帘子：横向占满、纵深一半、贴着工作区上沿（稿 D3／D4）。
    AURORA_TEST_CHECK_TRUE(near(target.width_dp, 1920.0));
    AURORA_TEST_CHECK_TRUE(near(target.height_dp, 540.0));
    AURORA_TEST_CHECK_TRUE(near(target.origin_x, 0.0));
    AURORA_TEST_CHECK_TRUE(near(target.origin_y, 0.0));
}

AURORA_TEST_CASE(top_cents_crosswise_and_hugs_the_edge_alongwise) {
    auto request = primary_area();
    request.cross_ratio = 0.5;
    const auto target = quake_target_rect(request);
    // 帘子变窄时两侧各留一半余量（居中），纵向仍贴顶——不是「往右下缩」。
    AURORA_TEST_CHECK_TRUE(near(target.width_dp, 960.0));
    AURORA_TEST_CHECK_TRUE(near(target.height_dp, 540.0));
    AURORA_TEST_CHECK_TRUE(near(target.origin_x, 480.0));
    AURORA_TEST_CHECK_TRUE(near(target.origin_y, 0.0));
}

AURORA_TEST_CASE(bottom_anchors_to_the_lower_edge) {
    auto request = primary_area();
    request.anchor = QuakeAnchor::Bottom;
    const auto target = quake_target_rect(request);
    // 下沿档：原点纵坐标是「工作区底 - 帘子高」，与顶档差整整一个帘子高。
    AURORA_TEST_CHECK_TRUE(near(target.origin_y, 540.0));
    AURORA_TEST_CHECK_TRUE(near(target.height_dp, 540.0));
    AURORA_TEST_CHECK_TRUE(near(target.origin_x, 0.0));
}

AURORA_TEST_CASE(left_and_right_swap_the_two_axes) {
    auto left = primary_area();
    left.anchor = QuakeAnchor::Left;
    const auto left_target = quake_target_rect(left);
    // 侧挂档：`along_ratio` 管**宽**、`cross_ratio` 管**高**——与顶/底档正交，不能沿用同一字段序。
    AURORA_TEST_CHECK_TRUE(near(left_target.width_dp, 960.0));
    AURORA_TEST_CHECK_TRUE(near(left_target.height_dp, 1080.0));
    AURORA_TEST_CHECK_TRUE(near(left_target.origin_x, 0.0));
    AURORA_TEST_CHECK_TRUE(near(left_target.origin_y, 0.0));

    auto right = primary_area();
    right.anchor = QuakeAnchor::Right;
    const auto right_target = quake_target_rect(right);
    AURORA_TEST_CHECK_TRUE(near(right_target.origin_x, 960.0));
    AURORA_TEST_CHECK_TRUE(near(right_target.width_dp, 960.0));
    AURORA_TEST_CHECK_TRUE(near(right_target.origin_y, 0.0));
}

AURORA_TEST_CASE(size_is_logical_dp_while_origin_stays_physical) {
    auto request = primary_area();
    request.scale_factor = 1.5;
    const auto target = quake_target_rect(request);
    // 150% 屏：尺寸 ÷1.5 交给框架 `set_size`（它自己再乘回来），原点**不除**——`set_position`
    // 收的是物理像素。这一条是整个件里最容易写错、也最贵的一处（稿 D2）。
    AURORA_TEST_CHECK_TRUE(near(target.width_dp, 1280.0));
    AURORA_TEST_CHECK_TRUE(near(target.height_dp, 360.0));
    request.cross_ratio = 0.5;
    const auto narrower = quake_target_rect(request);
    AURORA_TEST_CHECK_TRUE(near(narrower.origin_x, 480.0));  // 物理像素：(1920 - 960) / 2
    AURORA_TEST_CHECK_TRUE(near(narrower.width_dp, 640.0));  // 逻辑 dp：960 / 1.5
}

AURORA_TEST_CASE(work_area_origin_is_kept_not_rebased) {
    auto request = primary_area();
    // 副屏在主屏左侧时工作区原点为负（框架 `Display::work_area` 的既有口径），本件不许归一化。
    request.work_area.origin_x = -1920.0;
    request.cross_ratio = 0.5;
    const auto target = quake_target_rect(request);
    AURORA_TEST_CHECK_TRUE(near(target.origin_x, -1440.0));
    AURORA_TEST_CHECK_TRUE(near(target.origin_y, 0.0));
}

AURORA_TEST_CASE(out_of_domain_values_degrade_instead_of_collapsing) {
    auto request = primary_area();
    request.cross_ratio = 0.0;  // 塌成零宽是「热键按了没反应」的形态，必须夹到下界。
    request.along_ratio = 5.0;  // 越上界按整幅工作区解。
    const auto clamped = quake_target_rect(request);
    AURORA_TEST_CHECK_TRUE(near(clamped.width_dp, 96.0));
    AURORA_TEST_CHECK_TRUE(near(clamped.height_dp, 1080.0));

    auto garbage = primary_area();
    garbage.cross_ratio = std::nan("");  // 非有限值一律取上界：不能让怪值把帘子折没。
    garbage.scale_factor = 0.0;          // 非正缩放回落 1.0：否则 dp 尺寸除零。
    const auto degraded = quake_target_rect(garbage);
    AURORA_TEST_CHECK_TRUE(near(degraded.width_dp, 1920.0));
    AURORA_TEST_CHECK_TRUE(near(degraded.height_dp, 540.0));

    auto empty = primary_area();
    empty.work_area.width = -1.0;  // 负尺寸归零——本件不猜屏，也不许翻成「从右往左长」。
    const auto zeroed = quake_target_rect(empty);
    AURORA_TEST_CHECK_TRUE(near(zeroed.width_dp, 0.0));
    AURORA_TEST_CHECK_TRUE(near(zeroed.origin_x, 0.0));
}

AURORA_TEST_CASE(hotkey_toggles_both_ways_and_is_idempotent_on_state) {
    QuakeToggle toggle;
    AURORA_TEST_CHECK_TRUE(toggle.state() == QuakeState::Hidden);
    AURORA_TEST_CHECK_TRUE(toggle.on_hotkey(at(0)) == QuakeIntent::Show);
    AURORA_TEST_CHECK_TRUE(toggle.state() == QuakeState::Shown);
    AURORA_TEST_CHECK_TRUE(toggle.on_hotkey(at(1000)) == QuakeIntent::Hide);
    AURORA_TEST_CHECK_TRUE(toggle.state() == QuakeState::Hidden);
    // 收回后再按一次仍是 Show：意图跟着状态走，不是布尔翻转。
    AURORA_TEST_CHECK_TRUE(toggle.on_hotkey(at(2000)) == QuakeIntent::Show);
}

AURORA_TEST_CASE(focus_lost_inside_the_settle_window_is_swallowed) {
    QuakeToggle toggle;
    AURORA_TEST_REQUIRE_TRUE(toggle.on_hotkey(at(0)) == QuakeIntent::Show);
    // Tilix 那类故障的正面判据：呼出时的激活抖动、隐藏时必然发生的失焦，都不许在本件刚发起显隐
    // 之后立刻再改状态（稿 D6）。
    AURORA_TEST_CHECK_TRUE(toggle.on_focus_lost(at(1)) == QuakeIntent::None);
    AURORA_TEST_CHECK_TRUE(toggle.on_focus_lost(at(249)) == QuakeIntent::None);
    AURORA_TEST_CHECK_TRUE(toggle.state() == QuakeState::Shown);
    // 边界：恰好到上沿即放行（与铃脚节流同一比较口径，严格小于才吞）。
    AURORA_TEST_CHECK_TRUE(toggle.on_focus_lost(at(250)) == QuakeIntent::Hide);
    AURORA_TEST_CHECK_TRUE(toggle.state() == QuakeState::Hidden);
}

AURORA_TEST_CASE(hiding_by_hotkey_arms_the_same_settle_window) {
    QuakeToggle toggle;
    AURORA_TEST_REQUIRE_TRUE(toggle.on_hotkey(at(0)) == QuakeIntent::Show);
    AURORA_TEST_REQUIRE_TRUE(toggle.on_hotkey(at(1000)) == QuakeIntent::Hide);
    // 主动收回之后紧接着来的失焦事件同样要吞——否则「已隐」状态会被再置一次，
    // 且过渡窗被无谓地续期。
    AURORA_TEST_CHECK_TRUE(toggle.on_focus_lost(at(1100)) == QuakeIntent::None);
    AURORA_TEST_CHECK_TRUE(toggle.state() == QuakeState::Hidden);
}

AURORA_TEST_CASE(auto_hide_off_never_hides_on_focus_loss) {
    QuakeToggle toggle{/*auto_hide_on_focus_lost=*/false};
    AURORA_TEST_REQUIRE_TRUE(toggle.on_hotkey(at(0)) == QuakeIntent::Show);
    AURORA_TEST_CHECK_TRUE(toggle.on_focus_lost(at(10'000)) == QuakeIntent::None);
    AURORA_TEST_CHECK_TRUE(toggle.state() == QuakeState::Shown);
    // 开关是运行期可改的（装配层从配置灌入）：改开后下一帧的失焦就生效。
    toggle.set_auto_hide_on_focus_lost(true);
    AURORA_TEST_CHECK_TRUE(toggle.on_focus_lost(at(20'000)) == QuakeIntent::Hide);
}

AURORA_TEST_CASE(focus_lost_while_hidden_is_a_no_op) {
    QuakeToggle toggle;
    // 未呼出时的失焦（别的窗口切换）不许凭空把帘子弹出来。
    AURORA_TEST_CHECK_TRUE(toggle.on_focus_lost(at(0)) == QuakeIntent::None);
    AURORA_TEST_CHECK_TRUE(toggle.state() == QuakeState::Hidden);
}

AURORA_TEST_CASE(non_positive_settle_degrades_to_zero) {
    QuakeToggle toggle{true, milliseconds{-5}};
    AURORA_TEST_REQUIRE_TRUE(toggle.on_hotkey(at(0)) == QuakeIntent::Show);
    // 负时长归零＝不吞任何失焦事件（装配层若把配置读成负值，行为退化成「立即自动隐藏」，
    // 仍然可预期，而不是永远不隐）。
    AURORA_TEST_CHECK_TRUE(toggle.on_focus_lost(at(0)) == QuakeIntent::Hide);
}

}  // namespace borealis::test_cases::utest_ui_quake
