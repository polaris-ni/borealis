/// 测试类型: unit
/// 目标单元: src/ui/active_window_tracker.{h,cpp}
/// 测试说明: `SPEC.FEAT.WS.03` 的 D7 那一件（稿 §6 批 3、§7 给这条新套件的四则判据）：多窗上报
///           次序、无上报时回主窗、窗关闭后该号不再被返回、交叉上报只留最后一次。本件是纯记录器，
///           不含 widget、不含 IO、不判断号是否活着（「这扇窗还在不在」由装配层现查宿主），故四则
///           判据全部落在「记的是派发事实」这一条口径上：
///           ①无记录即回构造期主窗号——命令永远有一个明确落点，不是「取不到就谁也不做」；
///           ②最近者胜，A→B→A 之后落点是 A 而不是「出现次数最多的窗」；
///           ③无效号（0＝尚未登记进宿主）不改动任何记录，宁可留着上一个真实落点；
///           ④撤账只清与自己同号的那一格，兜底落点被撤即置无效（调用方据此按空落点处理，
///             本件不自己挑下一扇窗当兜底）；
///           ⑤（批 4-B 加的接缝）主窗易主后由装配层把新兜底交进来：只换兜底、不动派发事实，
///             无效号不安装——关一扇窗之后命令不必等一次点击才有落点。

#include "ui/active_window_tracker.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_active_window_tracker {

namespace {

using borealis::ui::ActiveWindowTracker;

constexpr au::WindowId kMain = 1U;      ///< 构造期那枚主窗号（框架自增分配，主窗恒是 1）。
constexpr au::WindowId kSecond = 2U;    ///< 之后开的第二扇窗。
constexpr au::WindowId kThird = 3U;     ///< 第三扇，只用来验「最近者胜」。
constexpr au::WindowId kFourth = 4U;    ///< 第四扇：主窗易主后接任兜底的那一扇（`set_fallback`）。

}  // namespace

AURORA_TEST_CASE(without_a_single_report_the_fallback_is_the_main_window) {
    ActiveWindowTracker tracker{kMain};
    AURORA_TEST_CHECK_EQ(tracker.active(), kMain);
    // 兜底不是猜测：即便别的窗早就上报过、又都撤了账，回到的是主窗号而不是「最近上报过但已关的那扇」。
    tracker.report(kSecond);
    tracker.forget(kSecond);
    AURORA_TEST_CHECK_EQ(tracker.active(), kMain);
}

AURORA_TEST_CASE(the_last_window_that_received_a_dispatch_wins) {
    ActiveWindowTracker tracker{kMain};
    tracker.report(kSecond);
    AURORA_TEST_CHECK_EQ(tracker.active(), kSecond);
    tracker.report(kThird);
    AURORA_TEST_CHECK_EQ(tracker.active(), kThird);
    // 交叉上报回到第二扇：记录是「最近一次」，不是计数、不是栈、更不是首次。
    tracker.report(kSecond);
    AURORA_TEST_CHECK_EQ(tracker.active(), kSecond);
}

AURORA_TEST_CASE(an_unregistered_window_id_changes_nothing) {
    ActiveWindowTracker tracker{kMain};
    tracker.report(au::AURORA_INVALID_WINDOW_ID);
    AURORA_TEST_CHECK_EQ(tracker.active(), kMain);  // 空上报不能把落点抹成无效号
    tracker.report(kSecond);
    tracker.report(au::AURORA_INVALID_WINDOW_ID);
    AURORA_TEST_CHECK_EQ(tracker.active(), kSecond);  // 已有真实落点时同样不被无效号覆盖
}

AURORA_TEST_CASE(a_closed_window_is_never_returned_again) {
    ActiveWindowTracker tracker{kMain};
    tracker.report(kSecond);
    tracker.report(kThird);
    // 撤「不是当前落点」那一格的账：当前落点不动。本件只记最后一次，没有历史栈，故这一撤不会影响 kThird。
    tracker.forget(kSecond);
    AURORA_TEST_CHECK_EQ(tracker.active(), kThird);
    // 撤掉当前落点即回主窗——**不是**回「上一个上报过的窗」，那一格在本件里没有记录（不做栈是稿 §4 D7
    // 的形态：落点要的是「最近谁被碰过」，不是「谁被碰过几次」）。
    tracker.forget(kThird);
    AURORA_TEST_CHECK_EQ(tracker.active(), kMain);
    // 已被撤账的号此后再怎么撤都保持回主窗，不会「复活」。
    tracker.forget(kSecond);
    AURORA_TEST_CHECK_EQ(tracker.active(), kMain);
}

AURORA_TEST_CASE(closing_the_fallback_itself_answers_the_invalid_id) {
    ActiveWindowTracker tracker{kMain};
    tracker.forget(kMain);
    // 主窗自己关了且没有别的上报：回无效号，由调用方经宿主查询取空后按「无落点」处理。
    // 「谁接任兜底」是关窗那一棒的账目（稿 §6 批 4），本件不自己挑下一扇。
    AURORA_TEST_CHECK_EQ(tracker.active(), au::AURORA_INVALID_WINDOW_ID);
    // 兜底失效不妨碍重新记录：下一次真实派发就是新的落点。
    tracker.report(kSecond);
    AURORA_TEST_CHECK_EQ(tracker.active(), kSecond);
}

AURORA_TEST_CASE(the_assembly_can_hand_the_fallback_to_a_surviving_window) {
    // 批 4-B 那一跳的接缝（稿 §6）：框架在宿主回收后把主窗位交给登记序里还活着的那一扇，而本件构造期
    // 记的号已经消散。不换兜底，「关掉主窗、在剩下的那扇里按 Ctrl+F（不经点击）」就永远没有落点。
    ActiveWindowTracker tracker{kMain};
    tracker.forget(kMain);
    AURORA_TEST_CHECK_EQ(tracker.active(), au::AURORA_INVALID_WINDOW_ID);
    tracker.set_fallback(kSecond);
    // 换兜底之后立刻又有落点，且**不需要**先有一次派发——事实与兜底是两件事。
    AURORA_TEST_CHECK_EQ(tracker.active(), kSecond);
    // 换兜底不动派发事实：已有真实落点时它仍是落点，撤账之后才回到新兜底。
    tracker.report(kThird);
    tracker.set_fallback(kFourth);
    AURORA_TEST_CHECK_EQ(tracker.active(), kThird);
    tracker.forget(kThird);
    AURORA_TEST_CHECK_EQ(tracker.active(), kFourth);
}

AURORA_TEST_CASE(an_invalid_fallback_is_never_installed) {
    ActiveWindowTracker tracker{kMain};
    // 无效号即「这一扇还没登记进宿主」，与 `report` 同一条档：宁留旧兜底，也不亲手把落点抹成空。
    tracker.set_fallback(au::AURORA_INVALID_WINDOW_ID);
    AURORA_TEST_CHECK_EQ(tracker.active(), kMain);
    tracker.forget(kMain);
    tracker.set_fallback(au::AURORA_INVALID_WINDOW_ID);
    AURORA_TEST_CHECK_EQ(tracker.active(), au::AURORA_INVALID_WINDOW_ID);
}

}  // namespace borealis::test_cases::utest_active_window_tracker
