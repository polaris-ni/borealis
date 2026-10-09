/// 测试类型: e2e
/// 目标单元: src/session/clipboard_outbox.cpp（经真机 PTY 会话链路）
/// 测试说明: `OSC 52` 写方向的真机落地（SPEC.FEAT.TERM.07 的剪贴板腿、SPEC.FEAT.CONN.12 的
///           「写方向默认允许」档）：shell 输出 OSC 52 → 解码 → 状态机在锁内留存 → 主线程取走
///           并落系统剪贴板 → 回读校验内容与「取走即清空」。
///
///           断言素材是 base64 而非明文：屏幕上的那串载荷解码前不含标记文本，回读到的明文
///           即证明整条链路（含 base64 解码）走通了，而不是把 shell 打印的一行搬了个位置。
///
///           剪贴板是用户共享状态：用例先取回既有文本、结束原样归还（空剪贴板则留空）。
///
///           两条平台腿只差「谁算这串 base64」：Windows 交 powershell，POSIX 交 /bin/sh 加
///           coreutils 的 base64。链路判据逐字共用，正是 `SPEC.NF.PLAT.01` 要的等价核验
///           （2026-10-08 补上 POSIX 腿，裁决 7.89）。
///
///           最后一腿（真落进**系统**剪贴板）在本机没有 xsel / xclip 时结构上取不到读数，故用例
///           先显式探测该桥：可用则照旧比回读明文，不可用则把「写入可复现地失败」断成现场事实而
///           非静默跳过——余下三条腿（进 outbox、状态机计数、取走即清空）在两档下都照常断。
///
///           本文件依赖真实 PTY 与桌面会话的剪贴板，须在有窗口站/桌面的交互会话里跑（裁决 7.19⑤）。

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
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

/// 等一条输出上屏的上限：子进程（powershell / /bin/sh）启动本身是秒级，留足余量以免偶发超时。
constexpr auto kSettleTimeout = std::chrono::milliseconds{30000};

constexpr auto kPollInterval = std::chrono::milliseconds{20};

constexpr std::size_t kColumns = 120U;
constexpr std::size_t kRows = 30U;

/// 剪贴板标记：只以 base64 形态经过会话，明文不会出现在屏幕上。
constexpr std::string_view kMarker = "borealis-osc52-clip";

auto width_policy = std::make_shared<borealis::term::SingleWidthPolicy>();

/// @brief 让子进程自己算 base64 并发出 `OSC 52 ; c ; <base64> BEL`。
///
///        POSIX 那条用单引号包住整段脚本：本仓的命令行切分（`tokenize_command_line`）认单引号为
///        全字面，于是管道与 `$()` 都归 shell 解释而不是被拆成 argv，两侧各得一个 `-c` 参数。
[[nodiscard]] auto osc_52_command_line() -> std::string {
#if defined(_WIN32)
    return "powershell.exe -NoProfile -Command \"$b=[Convert]::ToBase64String("
           "[Text.Encoding]::UTF8.GetBytes('borealis-osc52-clip')); $e=[char]27; "
           "Write-Host ($e + ']52;c;' + $b + [char]7)\"";
#else
    return "/bin/sh -c 'b=$(printf %s borealis-osc52-clip | base64); "
           "printf \"\033]52;c;%s\a\" \"$b\"'";
#endif
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

/// @brief 本机系统剪贴板桥是否可用——**显式探测**而不是跳过。
///
///        POSIX 腿的框架实现是转调 xsel / xclip，两者都没装即写入必失败（实测退出码 32512）。
///        那一档下用例仍断其可判的三条腿（OSC 52 进 outbox、取走即清空、状态机计数），并把
///        「桥不可用」本身断成现场事实，于是环境问题不会被读成链路回归，也不会被静默当成已验证。
[[nodiscard]] auto clipboard_bridge_available() -> bool {
    static constexpr std::string_view kProbe = "borealis-clipboard-probe";
    const auto saved = clipboard_text();
    const auto written = aurora::Clipboard::set_text(std::string{kProbe});
    const auto readable = clipboard_text().find(std::string{kProbe}) != std::string::npos;
    static_cast<void>(aurora::Clipboard::set_text(saved));  // 归还，含探测失败那一档
    return written.ok() && readable;
}

}  // namespace

AURORA_TEST_CASE(osc_52_from_a_real_shell_lands_in_the_system_clipboard) {
    const auto bridge_available = clipboard_bridge_available();
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

    if (bridge_available) {
        const auto text = clipboard_text();
        AURORA_TEST_CHECK(text.find(std::string{kMarker}) != std::string::npos);
    } else {
        // 断的是「这一档确实是桥不可用」而不是「剪贴板恰好没有内容」：写入本身可复现地失败。
        AURORA_TEST_CHECK_FALSE(aurora::Clipboard::set_text(std::string{kMarker}).ok());
    }

    // 取走即清空：同一请求不会在后续帧里重复写用户剪贴板。
    AURORA_TEST_CHECK_EQ(outbox.drain(*session), 0U);

    static_cast<void>(aurora::Clipboard::set_text(saved));  // 归还
    session->close();
}

}  // namespace borealis::test_cases::etest_osc_clipboard
