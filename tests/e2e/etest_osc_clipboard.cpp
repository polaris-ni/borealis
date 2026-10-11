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
///           （2026-10-08 补上 POSIX 腿，裁决 7.89）。**读方向那两案是 POSIX 独有**（稿 §6 的
///           D12③ 补腿）：脚本要 `stty` 的 VMIN/VTIME 与 `dd`/`sed`，
///           Windows 侧没有对应形态 ⇒ 整段以 `#if !defined(_WIN32)` 圈住，不写一份跑不了的脚本。
///
///           最后一腿（真落进**系统**剪贴板）在本机没有 xsel / xclip 时结构上取不到读数，故用例
///           先显式探测该桥：可用则照旧比回读明文，不可用则把「写入可复现地失败」断成现场事实而
///           非静默跳过——余下三条腿（进 outbox、状态机计数、取走即清空）在两档下都照常断。
///
///           本文件依赖真实 PTY 与桌面会话的剪贴板，须在有窗口站/桌面的交互会话里跑（裁决 7.19⑤）。

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aurora/app/clipboard.h"

#include "borealis/conn/local_terminal.h"
#include "borealis/grid/storage.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/session/session.h"
#include "borealis/term/clipboard_read.h"
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

/// @brief 起一条真本地会话；@p gate 非空时在 `start()` **之前**接上读方向闸门。
///
///        接线次序是这一手的关键：`start()` 之后 PTY 那头随时可能把查询喷过来，晚一拍接线
///        就是让状态机按「未接线即 Deny」那条缺省把查询当场吞掉（`SPEC.FEAT.CONN.12` 判据 ①
///        的形状）。集成用例里这条有替身证人，真 PTY 上只能靠接线次序本身。
[[nodiscard]] auto make_session(const std::string &command_line,
                                const borealis::term::ClipboardGate *gate = nullptr)
    -> std::unique_ptr<Session> {
    auto spec = LocalTerminalSpec{};
    spec.command_line = command_line;
    auto connection = conn::make_local_terminal_connection(spec, Size{kColumns, kRows});
    auto session =
        std::make_unique<Session>(std::move(connection), Size{kColumns, kRows}, 20U, width_policy);
    if (gate != nullptr) {
        session->set_clipboard_gate(gate);
    }
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

#if !defined(_WIN32)
// 读方向的两条现场腿（`SPEC.FEAT.CONN.12` 判据 ①③，稿 §6 的 D12③）。POSIX 独有：脚本要
// `stty raw -echo min 0 time N` 与 `dd`/`sed`，Windows 侧没有对应形态 ⇒ 整段不进编译。
namespace read_leg {

/// 子进程取完应答的空闲窗口，单位是十分之一秒（`stty time` 的口径）：2 s。
constexpr std::string_view kIdleTenths = "20";

/// @brief 让真 shell 发一条 `OSC 52 ; c ; ?` 查询，并把**我们回写进来的字节**按转义形态打印出来。
///
///        四个动作都承重，逐条实测于本沙箱（`$TMPDIR` 的 pty 探针，读数见稿 §8）：
///        ① `stty -echo`——不回显就不会让应答再进一遍自家解析器（应答本身就是一串 `OSC 52`，
///        回显等于自己给自己发一条写请求）；
///        ② `raw` 让读按字节交付，不必等换行（应答以 BEL 结尾，规范模式下永远凑不出一行）；
///        ③ `min 0 time 20` 而不是套一层 `timeout`——GNU `timeout` 会把被管命令放进**另一个进程组**，
///        那个组里的 tty 读是「后台读」，直接 SIGTTIN 停住（实测：套 timeout 时子进程一个字都不打印，
///        换成 VMIN/VTIME 后 2.05 s 出全串）；空闲 2 s 后 `read` 返 0，`dd` 自然收尾、`sed` 见 EOF 才刷；
///        ④ `sed` 把 ESC 与 BEL 折成 `[ESC]` / `[BEL]` 两段字面量——原样打印会让这串字节再次被
///        状态机吃掉，判据就无从落在网格上。
///        单引号包住整段：本仓的命令行切分认单引号为全字面，管道与分号都归 shell 解释。
[[nodiscard]] auto query_command_line() -> std::string {
    return "/bin/sh -c 'stty raw -echo min 0 time " + std::string{kIdleTenths} +
           " 2>/dev/null; printf \"\033]52;c;?\a\"; "
           "dd bs=1 count=64 2>/dev/null | sed -e \"s/\\x1b/[ESC]/g\" -e \"s/\\x07/[BEL]/g\"'";
}

/// @brief 整屏取成一串文本（行尾填充空格剥掉，行间以换行连接）。
///
///        判据取「网格里有这一段」而不是逐字节比整行：真 shell 的输出里夹着 tty 的换行与网格填充，
///        逐字节比对会在无害处失败（与 `etest_local_terminal` 同一口径）。
[[nodiscard]] auto screen_text(Session &session) -> std::string {
    static_cast<void>(session.drain_damage());
    auto out = std::string{};
    session.read([&](borealis::grid::Storage &grid, borealis::term::Cursor,
                      const borealis::term::TermModes &) {
        for (std::size_t row = 0; row < grid.visible_rows(); ++row) {
            const auto &line = grid.visible_line(row);
            auto text = std::string{};
            for (std::size_t column = 0; column < line.columns(); ++column) {
                const auto code_point = line.cell(column).code_point;
                text.push_back(code_point < 0x80U ? static_cast<char>(code_point) : ' ');
            }
            while (!text.empty() && text.back() == ' ') {
                text.pop_back();
            }
            out += text;
            out += '\n';
        }
    });
    return out;
}

/// @brief 轮询到转义后的应答出现在权威网格上；上限内取不到即返回 false（由调用方断红）。
[[nodiscard]] auto wait_for_response_on_screen(Session &session, std::string_view expected) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + kSettleTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (screen_text(session).find(std::string{expected}) != std::string::npos) {
            return true;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return false;
}

/// @brief 应答串的转义形态：`[ESC]]52;c;<载荷>[BEL]`（载荷为空就是那条空响应）。
[[nodiscard]] auto escaped_response(std::string_view payload) -> std::string {
    return "[ESC]]52;c;" + std::string{payload} + "[BEL]";
}

/// @brief 标记文本的 base64 载荷（读方向的应答体就是它，与写方向共用同一枚 kMarker）。
[[nodiscard]] auto expected_payload() -> std::string {
    const auto *bytes = reinterpret_cast<const std::byte *>(kMarker.data());
    return borealis::term::encode_base64(std::span<const std::byte>(bytes, kMarker.size()));
}

}  // namespace read_leg
#endif  // !defined(_WIN32)

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

#if !defined(_WIN32)
/// @brief 判据 ① 的现场腿：`Allow` 档下真 shell 的读查询被登记、被答出，应答从**同一条写通道**回到远端。
///
///        桥在位那档断的是「正文真的到了远端」——载荷就是 kMarker 的 base64；缺桥那档（G9：本机
///        xsel/xclip 双双不在位）`ClipboardOutbox::read()` 只能回空串，于是走那条空响应。两档都
///        要求「有回写」，差别只在内容，故环境问题不会被读成链路回归，也不会被当成已验证正文。
AURORA_TEST_CASE(an_osc_52_read_query_from_a_real_shell_is_answered_on_the_same_channel) {
    const auto bridge_available = clipboard_bridge_available();
    const auto saved = clipboard_text();
    if (bridge_available) {
        static_cast<void>(aurora::Clipboard::set_text(std::string{kMarker}));
    }

    const borealis::term::ClipboardGate gate{borealis::term::ClipboardReadPolicy::Allow};
    const auto session = make_session(read_leg::query_command_line(), &gate);

    const auto deadline = std::chrono::steady_clock::now() + kSettleTimeout;
    auto requests = std::vector<borealis::term::ClipboardReadRequest>{};
    while (std::chrono::steady_clock::now() < deadline && requests.empty()) {
        static_cast<void>(session->drain_damage());
        requests = session->take_clipboard_reads();
        std::this_thread::sleep_for(kPollInterval);
    }
    AURORA_TEST_REQUIRE_MSG(requests.size() == 1U, "the read query never reached the state machine");
    AURORA_TEST_CHECK_EQ(session->osc_state().clipboard_read_requests, 1U);
    AURORA_TEST_CHECK_EQ(session->osc_state().clipboard_read_denied, 0U);

    // 测试侧在这里扮演的就是装配层那一条腿：读剪贴板、把内容交给 `answer_read` 编码并写回连接。
    const auto content = ClipboardOutbox::read();
    const auto answered = ClipboardOutbox::answer_read(
        *session, requests.front(),
        content.empty() ? std::optional<std::string_view>{} : std::optional<std::string_view>{content});
    AURORA_TEST_CHECK_EQ(answered, bridge_available);

    const auto expected = read_leg::escaped_response(answered ? read_leg::expected_payload()
                                                              : std::string_view{});
    AURORA_TEST_CHECK_MSG(read_leg::wait_for_response_on_screen(*session, expected),
                          "the real shell never saw: " + expected);

    static_cast<void>(aurora::Clipboard::set_text(saved));  // 归还
    session->close();
}

/// @brief 判据 ③ 的现场腿：`Deny` 档下远端也**一定拿得到回写**，而不是永远等。
///
///        这一档不依赖剪贴板桥（缺桥机器同样成立），断的是三件事：状态机当场拒（计数 `denied` +1）、
///        待答队列里一条都不留（静默丢弃才是本判据要防的形状）、真 shell 打印出那条空响应。
AURORA_TEST_CASE(a_denied_osc_52_read_query_still_answers_the_real_shell) {
    const borealis::term::ClipboardGate gate{borealis::term::ClipboardReadPolicy::Deny};
    const auto session = make_session(read_leg::query_command_line(), &gate);

    const auto expected = read_leg::escaped_response(std::string_view{});
    // 应答是状态机在**同一轮喂入**里就发出的（`pending_responses_` 在 ingest 末尾整份冲出），
    // 所以这里不必扮演主线程答话——不答也正是本档要的形态。
    AURORA_TEST_CHECK_MSG(read_leg::wait_for_response_on_screen(*session, expected),
                          "the denied query left the real shell waiting: " + expected);
    AURORA_TEST_CHECK_EQ(session->osc_state().clipboard_read_denied, 1U);
    AURORA_TEST_CHECK_EQ(session->osc_state().clipboard_read_requests, 1U);
    AURORA_TEST_CHECK_TRUE(session->take_clipboard_reads().empty());

    session->close();
}
#endif  // !defined(_WIN32)

}  // namespace borealis::test_cases::etest_osc_clipboard
