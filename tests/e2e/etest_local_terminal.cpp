/// 测试类型: e2e
/// 目标单元: src/platform/win/conpty_connection.cpp（经 include/borealis/conn/local_terminal.h 的工厂）
/// 测试说明: 真机 ConPTY 上的本地终端（SPEC.FEAT.CONN.01）——默认 shell 探测链、自定义命令、
///           启动目录、PTY 环境注入、尺寸下发不中断输出（SPEC.FEAT.XFER.01）、高频输出下背压
///           有界且最终内容与 PTY 一致（SPEC.NF.PERF.06）、关停后进程终止而内容保留供回看
///           （SPEC.FEAT.WS.01、架构 §7.3）。
///
///           内容断言一律经会话读权威网格，而不是比对原始字节：ConPTY 的输出里夹着光标定位与
///           清屏序列，只有过完状态机才是「用户看到的那一行」。
///           两条平台腿只差「一次性命令的素材形态」（cmd.exe 的 `/c echo` 对 `/bin/sh -c 'echo'`），
///           判据一律共用——这正是 `SPEC.NF.PLAT.01` 要的等价核验（2026-10-08 补上 POSIX 腿，
///           裁决 7.89）。须在有 PTY 的会话里跑（Windows 侧另须窗口站/桌面，裁决 7.19⑤）。

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "borealis/conn/local_terminal.h"
#include "borealis/grid/storage.h"
#include "borealis/session/session.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"
#include "support/paths.h"

namespace borealis::test_cases::etest_local_terminal {

namespace {

using borealis::conn::LocalTerminalSpec;
using borealis::grid::Storage;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::SingleWidthPolicy;

/// 等一条输出上屏的上限：真机 shell 启动本身是秒级，留足余量以免偶发超时。
constexpr auto kSettleTimeout = std::chrono::milliseconds{20000};

/// 轮询间隔：会话的脏区消费是「按帧」的语义，测试侧以固定节拍代替帧循环。
constexpr auto kPollInterval = std::chrono::milliseconds{20};

/// 一屏可滚动的行数上限（用例内的视口都不超过它）。
constexpr std::size_t kColumns = 120U;
constexpr std::size_t kRows = 30U;

auto width_policy = std::make_shared<borealis::term::SingleWidthPolicy>();

/// @brief 以自定义命令打开一个会话并启动（`SPEC.FEAT.CONN.01` 的「自定义命令」模式）。
[[nodiscard]] auto make_session(const std::string &command_line,
                               const std::string &working_directory = {})
    -> std::unique_ptr<Session> {
    auto spec = LocalTerminalSpec{};
    spec.command_line = command_line;
    spec.working_directory = working_directory;
    auto connection = conn::make_local_terminal_connection(spec, Size{kColumns, kRows});
    auto session =
        std::make_unique<Session>(std::move(connection), Size{kColumns, kRows}, 200U, width_policy);
    session->start();
    return session;
}

/// @brief 把视口各行取成文本（行尾空白剥掉，右侧空格是网格填充不是内容）。
[[nodiscard]] auto screen_rows(Session &session) -> std::vector<std::string> {
    // 本用例每次取整屏而不按提交取行：排帧的语义是「取用即消费脏标记」，提交内容在此不需要。
    static_cast<void>(session.drain_damage());
    auto rows = std::vector<std::string>{};
    session.read([&](Storage &grid, term::Cursor, const term::TermModes &) {
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
            rows.push_back(text);
        }
    });
    return rows;
}

/// @brief 排帧直到某一行含 @p needle，返回那些行的拼接文本；超时返回空视图。
[[nodiscard]] auto wait_for_line(Session &session, std::string_view needle) -> std::optional<std::string> {
    const auto deadline = std::chrono::steady_clock::now() + kSettleTimeout;
    auto last_rows = std::vector<std::string>{};
    while (std::chrono::steady_clock::now() < deadline) {
        last_rows = screen_rows(session);
        for (const auto &row : last_rows) {
            if (row.find(needle) != std::string::npos) {
                return row;
            }
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return std::nullopt;
}

/// @brief 等子进程确实退场：拿到最后一行输出与句柄转为已终止之间有一段窗口，不能提前断言。
[[nodiscard]] auto wait_until_dead(Session &session) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + kSettleTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(screen_rows(session));
        if (!session.alive()) {
            return true;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return false;
}

/// @brief 路径的比较形态：先做词法归一（`repo_root()` 在本机上带尾部的 `/.`，`pwd` 报的没有），
///           再统一分隔符、去尾分隔符、转小写（cmd 报的是长路径，环境给的大小写不定）。
[[nodiscard]] auto normalize_path(std::string_view path) -> std::string {
    auto out = std::filesystem::path{path}.lexically_normal().string();
    std::replace(out.begin(), out.end(), '/', '\\');
    while (!out.empty() && out.back() == '\\') {
        out.pop_back();
    }
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return out;
}

/// @brief 素材一律 ASCII，逐字符升成码点交会话（`Session::send_text` 吃码点串）。
[[nodiscard]] auto to_code_points(std::string_view text) -> std::u32string {
    auto out = std::u32string{};
    out.reserve(text.size());
    for (const auto character : text) {
        out.push_back(static_cast<char32_t>(static_cast<unsigned char>(character)));
    }
    return out;
}

/// @brief 去掉命令行外层引号，得到可比对的文件路径。
[[nodiscard]] auto strip_quotes(std::string_view line) -> std::string {
    auto out = std::string{line};
    if (out.size() >= 2U && out.front() == '"' && out.back() == '"') {
        out.erase(out.begin());
        out.pop_back();
    }
    return out;
}

// ------------------------------------------------------------
// 平台素材：两条腿只有「跑一条一次性命令」的写法不同，判据一律共用。
// POSIX 侧的单引号形态同时是命令行切件那档单引号支持的消费证人（`SPEC.NF.PLAT.01`）。
// ------------------------------------------------------------
#if defined(_WIN32)

/// @brief 一次性命令：跑完即退出。
[[nodiscard]] auto once_command(std::string_view body) -> std::string {
    return std::string{"cmd.exe /c "} + std::string{body};
}

/// @brief 交互式 shell 的命令行。
constexpr std::string_view kInteractiveShell = "cmd.exe";

/// @brief 高频突发输出：cmd 的 `for /l` 循环。
[[nodiscard]] auto burst_command() -> std::string {
    return "cmd.exe /c \"for /l %i in (1,1,3000) do @echo line-%i\"";
}

/// @brief 报告当前目录的一次性命令。
[[nodiscard]] auto print_directory_command() -> std::string {
    return once_command("cd");
}

/// 键入腿的素材：cmd 的 echo 原样打印字面量，故「看到的那行」与「敲进去的那行」同源。
constexpr std::string_view kTypedCommand = "echo borealis-typed";
constexpr std::string_view kTypedOutput = "borealis-typed";
constexpr std::string_view kResizeCommand = "echo borealis-after-resize";
constexpr std::string_view kResizeOutput = "borealis-after-resize";
constexpr std::string_view kKeptCommand = "echo borealis-kept";
constexpr std::string_view kKeptOutput = "borealis-kept";
/// 环境变量的展开形态：cmd 用 `%VAR%`。
constexpr std::string_view kEnvProbeBody = "echo TERM=%TERM% COLORTERM=%COLORTERM%";
constexpr std::string_view kSingleEnvProbeBody = "echo TERM=%TERM%";

#else

/// @brief 一次性命令：交给 `/bin/sh -c`，单引号内一律字面量（含 `$`、`;`、`(`）。
[[nodiscard]] auto once_command(std::string_view body) -> std::string {
    return std::string{"/bin/sh -c '"} + std::string{body} + "'";
}

constexpr std::string_view kInteractiveShell = "/bin/sh";

[[nodiscard]] auto burst_command() -> std::string {
    return once_command("i=1; while [ $i -le 3000 ]; do echo line-$i; i=$((i+1)); done");
}

[[nodiscard]] auto print_directory_command() -> std::string {
    return once_command("pwd");
}

// POSIX 侧让**展开后的值**与敲进去的那行不同（`$((1+1))` 由 shell 算），于是这两例断的是
// 「子进程算出来的输出」而不是终端对键入的回显——回显在两条腿上都可能凑巧满足判据。
constexpr std::string_view kTypedCommand = "echo borealis-typed-$((1+1))";
constexpr std::string_view kTypedOutput = "borealis-typed-2";
constexpr std::string_view kResizeCommand = "echo borealis-after-resize-$((2+2))";
constexpr std::string_view kResizeOutput = "borealis-after-resize-4";
constexpr std::string_view kKeptCommand = "echo borealis-kept-$((3+3))";
constexpr std::string_view kKeptOutput = "borealis-kept-6";
/// 环境变量的展开形态：shell 用 `$VAR`。
constexpr std::string_view kEnvProbeBody = "echo TERM=$TERM COLORTERM=$COLORTERM";
constexpr std::string_view kSingleEnvProbeBody = "echo TERM=$TERM";

#endif

}  // namespace

AURORA_TEST_CASE(default_shell_probe_returns_an_existing_executable) {
    // SPEC.FEAT.CONN.01：探测只做存在性判断，不启动进程，结果必须是磁盘上真实的可执行文件。
    const auto command_line = conn::default_shell_command_line();
    AURORA_TEST_REQUIRE(!command_line.empty());
    const auto path = strip_quotes(command_line);
    AURORA_TEST_REQUIRE(std::filesystem::exists(std::filesystem::path{path}));
    AURORA_TEST_CHECK(std::filesystem::is_regular_file(std::filesystem::path{path}));
}

AURORA_TEST_CASE(custom_command_output_reaches_the_grid) {
    const auto session = make_session(once_command("echo borealis-e2e"));
    const auto line = wait_for_line(*session, "borealis-e2e");
    AURORA_TEST_REQUIRE_MSG(line.has_value(), "no output from the local PTY within timeout");
    // 逐行相等而不只是「含这段素材」：shell 解析失败时它把 $0（就是命令行的第二个 token）印进
    // 错误前缀，于是 `borealis-e2e': 1: Syntax error` 也满足「包含」，而它并不是命令的输出。
    AURORA_TEST_CHECK_STREQ(*line, "borealis-e2e");
    AURORA_TEST_CHECK(wait_until_dead(*session));  // 一次性命令跑完即退出
}

AURORA_TEST_CASE(pty_environment_is_injected_into_the_child) {
    // SPEC.FEAT.CONN.01：PTY 环境注入 TERM=xterm-256color 与 COLORTERM=truecolor。
    const auto session = make_session(once_command(kEnvProbeBody));
    const auto line = wait_for_line(*session, "TERM=xterm-256color");
    AURORA_TEST_REQUIRE_MSG(line.has_value(), "TERM was not injected into the child environment");
    AURORA_TEST_CHECK(line->find("COLORTERM=truecolor") != std::string::npos);
}

AURORA_TEST_CASE(profile_environment_overrides_the_default_term) {
    // 注入是默认值而非硬编码：规格里的同名变量覆盖它（`LocalTerminalSpec::environment`）。
    auto spec = LocalTerminalSpec{};
    spec.command_line = once_command(kSingleEnvProbeBody);
    spec.environment["TERM"] = "dumb";
    auto connection = conn::make_local_terminal_connection(spec, Size{kColumns, kRows});
    auto session =
        std::make_unique<Session>(std::move(connection), Size{kColumns, kRows}, 20U, width_policy);
    session->start();
    const auto line = wait_for_line(*session, "TERM=dumb");
    AURORA_TEST_REQUIRE_MSG(line.has_value(), "profile environment override did not reach the child");
    AURORA_TEST_CHECK(line->find("TERM=xterm-256color") == std::string::npos);
}

AURORA_TEST_CASE(working_directory_applies_at_startup) {
    // 请求的目录必须**不是测试进程自己的当前目录**：两者相同时「应用了启动目录」与「继承了进程
    // cwd」给出同一个读数，判据结构上抓不到子进程侧 fchdir 那一腿被删（变异实测八例全绿）。
    const auto requested =
        std::filesystem::path{aurora::testing::paths::repo_root()} / "codespec";
    AURORA_TEST_REQUIRE(std::filesystem::exists(requested));
    const auto expected = normalize_path(requested.string());
    AURORA_TEST_REQUIRE_MSG(
        expected != normalize_path(std::filesystem::current_path().string()),
        "the requested directory equals the test process working directory, so this case cannot tell "
        "'applied' from 'inherited'");
    const auto session = make_session(print_directory_command(), requested.string());
    const auto deadline = std::chrono::steady_clock::now() + kSettleTimeout;
    auto found = false;
    while (std::chrono::steady_clock::now() < deadline && !found) {
        for (const auto &row : screen_rows(*session)) {
            found = normalize_path(row) == expected;
            if (found) {
                break;
            }
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    AURORA_TEST_REQUIRE_MSG(found, "child process did not start in the requested directory");
}

AURORA_TEST_CASE(a_missing_startup_directory_fails_the_launch) {
    // 两条腿的错误等价：Windows 侧 `CreateProcessW` 对坏 `lpCurrentDirectory` 是整个启动失败，
    // POSIX 侧必须在 fork **之前**把目录打开判掉，而不是悄悄起一个停在继承目录里的 shell——
    // 那副样子是「配了启动目录却什么都没发生」且不留任何痕迹。
    const auto missing =
        std::filesystem::path{aurora::testing::paths::repo_root()} / "no-such-startup-dir";
    AURORA_TEST_REQUIRE_FALSE(std::filesystem::exists(missing));
    const auto session = make_session(std::string{kInteractiveShell}, missing.string());
    AURORA_TEST_CHECK_FALSE(session->alive());
    for (const auto &row : screen_rows(*session)) {
        AURORA_TEST_CHECK(row.empty());  // 没有任何 shell 提示符上过屏：启动确实没发生
    }
}

AURORA_TEST_CASE(typed_input_round_trips_and_size_change_keeps_the_stream) {
    // SPEC.FEAT.XFER.01 的平台腿：改尺寸之后子进程仍照常输出。
    const auto session = make_session(std::string{kInteractiveShell});
    session->send_text(to_code_points(kTypedCommand) + U"\r");
    AURORA_TEST_REQUIRE_MSG(wait_for_line(*session, kTypedOutput).has_value(),
                            "typed input never came back from the PTY");

    session->resize(Size{100U, 24U});
    AURORA_TEST_CHECK(session->alive());
    session->send_text(to_code_points(kResizeCommand) + U"\r");
    AURORA_TEST_REQUIRE_MSG(wait_for_line(*session, kResizeOutput).has_value(),
                            "output stalled after the size change");
}

AURORA_TEST_CASE(burst_output_stays_bounded_and_consistent) {
    // SPEC.NF.PERF.06 的真机腿：高频输出下不卡死、队列条目数有界、最终内容仍等于输出。
    const auto session = make_session(burst_command());
    const auto last = wait_for_line(*session, "line-3000");
    AURORA_TEST_REQUIRE_MSG(last.has_value(), "high-frequency output stalled before the last line");
    const auto stats = session->queue_stats();
    AURORA_TEST_CHECK(stats.peak_pending <= borealis::session::kDefaultDamageQueueCapacity);
    AURORA_TEST_CHECK(stats.pending <= borealis::session::kDefaultDamageQueueCapacity);
}

AURORA_TEST_CASE(close_terminates_the_process_and_keeps_the_grid) {
    const auto session = make_session(std::string{kInteractiveShell});
    session->send_text(to_code_points(kKeptCommand) + U"\r");
    AURORA_TEST_REQUIRE(wait_for_line(*session, kKeptOutput).has_value());
    AURORA_TEST_REQUIRE(session->alive());

    session->close();
    AURORA_TEST_CHECK_FALSE(session->alive());
    // 进程退出后网格内容仍可读，供回看与一键重启（架构 §7.3）。
    const auto rows = screen_rows(*session);
    auto kept = false;
    for (const auto &row : rows) {
        kept = kept || row.find(std::string{kKeptOutput}) != std::string::npos;
    }
    AURORA_TEST_CHECK(kept);
}

}  // namespace borealis::test_cases::etest_local_terminal
