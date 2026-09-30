/// 测试类型: e2e
/// 目标单元: src/session/clipboard_outbox.cpp（经真机 ConPTY 会话链路）
/// 测试说明: `OSC 52` 写方向的真机落地（SPEC.FEAT.TERM.07 的剪贴板腿、SPEC.FEAT.CONN.12 的
///           「写方向默认允许」档）：shell 输出 OSC 52 → 解码 → 状态机在锁内留存 → 主线程取走
///           并落系统剪贴板 → 回读校验内容与「取走即清空」。
///
///           断言素材是 base64 而非明文：屏幕上的那串载荷解码前不含标记文本，回读到的明文
///           即证明整条链路（含 base64 解码）走通了，而不是把 shell 打印的一行搬了个位置。
///
///           剪贴板是用户共享状态：用例先取回既有文本、结束原样归还（空剪贴板则留空）。
///           本文件依赖真实 ConPTY 与桌面会话的剪贴板，当前只有 Win32 侧有实现
///           （posix 等价待建，见 codespec/PLAN.md §8）。

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>

#include "aurora/app/clipboard.h"

#include "borealis/conn/local_terminal.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/session/session.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::etest_osc_clipboard {

namespace {

using borealis::conn::LocalTerminalSpec;
using borealis::session::ClipboardOutbox;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::SingleWidthPolicy;

/// 等一条输出上屏的上限：powershell 启动本身是秒级，留足余量以免偶发超时。
constexpr auto kSettleTimeout = std::chrono::milliseconds{30000};

constexpr auto kPollInterval = std::chrono::milliseconds{20};

constexpr std::size_t kColumns = 120U;
constexpr std::size_t kRows = 30U;

/// 剪贴板标记：只以 base64 形态经过会话，明文不会出现在屏幕上。
constexpr std::string_view kMarker = "borealis-osc52-clip";

SingleWidthPolicy width_policy;

/// @brief 让 powershell 自己算 base64 并发出 `OSC 52 ; c ; <base64> BEL`。
[[nodiscard]] auto osc_52_command_line() -> std::string {
    return "powershell.exe -NoProfile -Command \"$b=[Convert]::ToBase64String("
           "[Text.Encoding]::UTF8.GetBytes('borealis-osc52-clip')); $e=[char]27; "
           "Write-Host ($e + ']52;c;' + $b + [char]7)\"";
}

[[nodiscard]] auto make_session(const std::string &command_line) -> std::unique_ptr<Session> {
    auto spec = LocalTerminalSpec{};
    spec.command_line = command_line;
    auto connection = conn::make_local_terminal_connection(spec, Size{kColumns, kRows});
    auto session =
        std::make_unique<Session>(std::move(connection), Size{kColumns, kRows}, 20U, width_policy);
    session->start();
    return session;
}

/// @brief 系统剪贴板当前文本；不可访问时返回空串（无文本同样是空串，二者都按留空归还）。
[[nodiscard]] auto clipboard_text() -> std::string {
    const auto current = aurora::Clipboard::get_text();
    return current.ok() ? current.value() : std::string{};
}

}  // namespace

AURORA_TEST_CASE(osc_52_from_a_real_shell_lands_in_the_system_clipboard) {
    const auto saved = clipboard_text();  // 归还用户的既有内容，用例不留痕
    ClipboardOutbox outbox;

    const auto session = make_session(osc_52_command_line());
    const auto deadline = std::chrono::steady_clock::now() + kSettleTimeout;
    std::size_t drained = 0;
    while (std::chrono::steady_clock::now() < deadline && drained == 0U) {
        static_cast<void>(session->drain_damage());  // 脏区照常排走：本用例断言的是剪贴板腿
        drained = outbox.drain(*session);
        std::this_thread::sleep_for(kPollInterval);
    }
    AURORA_TEST_REQUIRE_MSG(drained == 1U, "OSC 52 never reached the clipboard outbox");
    AURORA_TEST_CHECK_EQ(session->osc_state().clipboard_write_requests, 1U);

    const auto text = clipboard_text();
    AURORA_TEST_CHECK(text.find(std::string{kMarker}) != std::string::npos);

    // 取走即清空：同一请求不会在后续帧里重复写用户剪贴板。
    AURORA_TEST_CHECK_EQ(outbox.drain(*session), 0U);

    static_cast<void>(aurora::Clipboard::set_text(saved));  // 归还
    session->close();
}

}  // namespace borealis::test_cases::etest_osc_clipboard
