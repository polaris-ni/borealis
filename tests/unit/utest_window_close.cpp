/// 测试类型: unit
/// 目标单元: include/borealis/ui/window_close.h + src/ui/window_close.cpp（无 au:: 类型）
/// 测试说明: 守住关窗询问策略（`SPEC.FEAT.WS.03` 稿 D3／D4，裁决 7.161）——三档发起方与
///           「本窗有无活会话」折成的六行全表、两条**用户**入口的处置逐位相等（同一个动作
///           不能因为按下去的是鼠标还是回车就少问一句），以及 `AppInitiated` 在两档活会话下
///           都不得询问（确认后重试与退出序一旦被问就会卡死）。

#include <array>
#include <cstddef>

#include "borealis/ui/window_close.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_window_close {

namespace {

using borealis::ui::WindowCloseDisposition;
using borealis::ui::WindowCloseInitiator;

constexpr WindowCloseInitiator kInitiators[] = {
    WindowCloseInitiator::OsCloseButton,
    WindowCloseInitiator::UserCommand,
    WindowCloseInitiator::AppInitiated,
};

/// @brief 活会话在前、发起方在后的六行全表；`AskUser` 只允许落在前两行的 true 列。
constexpr WindowCloseDisposition kExpected[2][3] = {
    /* 无活会话 */ {WindowCloseDisposition::CloseNow, WindowCloseDisposition::CloseNow,
                    WindowCloseDisposition::CloseNow},
    /* 有活会话 */ {WindowCloseDisposition::AskUser, WindowCloseDisposition::AskUser,
                    WindowCloseDisposition::CloseNow},
};

}  // namespace

AURORA_TEST_CASE(the_whole_table_is_pinned) {
    for (const bool live : {false, true}) {
        const auto &row = kExpected[live ? 1 : 0];
        for (std::size_t i = 0; i < std::size(kInitiators); ++i) {
            const auto got = borealis::ui::window_close_disposition(kInitiators[i], live);
            AURORA_TEST_CHECK_MSG(got == row[i],
                                  live ? "live-session column disagrees" : "idle column disagrees");
        }
    }
}

AURORA_TEST_CASE(the_two_user_entries_get_the_same_answer) {
    // 本棒的承重判据：入口等价。OS 关闭钮与命令面板是同一次「关掉这扇窗」，问不问只由窗内
    // 有没有活会话决定；这一条转红就意味着键盘入口静默带走会话，而鼠标入口还会问一句。
    for (const bool live : {false, true}) {
        AURORA_TEST_CHECK_TRUE(window_close_disposition(WindowCloseInitiator::OsCloseButton, live) ==
                               window_close_disposition(WindowCloseInitiator::UserCommand, live));
    }
    AURORA_TEST_CHECK_TRUE(window_close_disposition(WindowCloseInitiator::UserCommand, true) ==
                           WindowCloseDisposition::AskUser);
}

AURORA_TEST_CASE(app_initiated_closes_without_asking_even_with_live_sessions) {
    // 确认框答「关」之后的那一跳，与退出序遍历全部窗那一跳，都归 `AppInitiated`。它们若被问，
    // 前者的框会二次弹出、后者会把进程卡在一个没有窗可挂的框上（稿 D1／任务书 m3）。
    for (const bool live : {false, true}) {
        AURORA_TEST_CHECK_TRUE(window_close_disposition(WindowCloseInitiator::AppInitiated, live) ==
                               WindowCloseDisposition::CloseNow);
    }
}

AURORA_TEST_CASE(an_idle_window_needs_no_answer_from_anyone) {
    // 没有活会话 ⇒ 三条入口一律直接关：框里没有可损失的东西，问了只是多一次点击。
    for (const WindowCloseInitiator initiator : kInitiators) {
        AURORA_TEST_CHECK_TRUE(window_close_disposition(initiator, false) ==
                               WindowCloseDisposition::CloseNow);
    }
}

}  // namespace borealis::test_cases::utest_window_close
