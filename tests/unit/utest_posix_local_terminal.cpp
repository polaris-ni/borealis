/// 测试类型: unit
/// 目标单元: src/platform/posix/local_terminal.cpp + include/borealis/conn/local_terminal.h
/// 测试说明: `SPEC.FEAT.CONN.01` 的 POSIX 探测腿（沙箱内可跑，不起任何进程）：`$SHELL` 命中即取用、
///           指向不存在的文件或空值时逐级回落到 `/bin/bash` 再 `/bin/sh`，且返回值一律是存在的
///           绝对路径（不含空格，故工厂侧无需 Windows 那种加引号形态）。PTY 环境注入的两个缺省值
///           是两条腿共用的契约，一并在此核。
///
///           本文件只在非 Win32 编译（`cmake/BorealisTests.cmake` 按文件名把平台专属套件挡在
///           另一侧的构建面之外）；Windows 腿的探测链判据在 `etest_local_terminal` 与装配层。

#include <unistd.h>  // access, F_OK

#include <cstdlib>
#include <string>

#include "borealis/conn/local_terminal.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_posix_local_terminal {

namespace {

using borealis::conn::default_shell_command_line;
using borealis::conn::kPtyDefaultEnvironment;

/// @brief 在 @p shell_value 生效期间取探测结果，取完原样归还环境变量。
///
/// 探测读的是进程环境的 `SHELL`，用例必须把它恢复到进入前的状态（包括「原本没有该变量」那一档），
/// 否则同进程的后续用例会看到被污染的值。
[[nodiscard]] auto detected_with_shell(const char *shell_value) -> std::string {
    const auto *previous = std::getenv("SHELL");
    const auto previous_value = previous != nullptr ? std::string{previous} : std::string{};
    const auto had_shell = previous != nullptr;

    if (shell_value == nullptr) {
        static_cast<void>(::unsetenv("SHELL"));
    } else {
        static_cast<void>(::setenv("SHELL", shell_value, 1));
    }
    auto detected = default_shell_command_line();

    if (had_shell) {
        static_cast<void>(::setenv("SHELL", previous_value.c_str(), 1));
    } else {
        static_cast<void>(::unsetenv("SHELL"));
    }
    return detected;
}

[[nodiscard]] auto path_exists(const std::string &path) -> bool {
    return ::access(path.c_str(), F_OK) == 0;
}

}  // namespace

AURORA_TEST_CASE(a_usable_shell_variable_is_honoured_verbatim) {
    // 用户指定的 shell 优先：取用值逐字等于 $SHELL，而不是回落链上的那一个。
    const auto detected = detected_with_shell("/bin/sh");
    AURORA_TEST_CHECK_STREQ(detected.c_str(), "/bin/sh");
}

AURORA_TEST_CASE(a_bogus_shell_variable_falls_back_without_returning_it) {
    // $SHELL 指向不存在的文件时不得把那个坏值交出去——工厂侧只做存在性判断，交出去就是启动一个
    // 必然失败的会话。回落目标本身必须是存在的可执行文件。
    const auto detected = detected_with_shell("/no/such/shell-at-all");
    AURORA_TEST_CHECK_FALSE(detected.empty());
    AURORA_TEST_CHECK_NE(detected, std::string{"/no/such/shell-at-all"});
    AURORA_TEST_CHECK_MSG(path_exists(detected), "the fallback path does not exist");
}

AURORA_TEST_CASE(an_unset_or_empty_shell_variable_falls_back) {
    for (const auto *value : {static_cast<const char *>(nullptr), ""}) {
        const auto detected = detected_with_shell(value);
        AURORA_TEST_CHECK_MSG(!detected.empty(), "no shell found on a POSIX system at all");
        AURORA_TEST_CHECK(path_exists(detected));
    }
}

AURORA_TEST_CASE(the_fallback_prefers_bash_over_the_posix_baseline) {
    // 两级回落的**次序**是可判的：本机 `/bin/bash` 存在时结果就必须是它，而不是 `/bin/sh`。
    // 两个候选都存在是 Linux/macOS 的常态；若哪天在没有 bash 的容器里跑，这一句随前提一起退化成
    // 「至少给到 /bin/sh」，而上面那条存在性判据不受影响。
    AURORA_TEST_REQUIRE(path_exists("/bin/bash"));
    const auto detected = detected_with_shell(nullptr);
    AURORA_TEST_CHECK_STREQ(detected.c_str(), "/bin/bash");
}

AURORA_TEST_CASE(the_detected_command_line_needs_no_quoting) {
    // 判据的来处是 Windows 腿的对照：那侧候选路径可含空格故必须加引号，这侧三条候选都是绝对路径
    // 且已存在性判定通过，因此命令行切分（`tokenize_command_line`）拿到的恰是一个 token。
    const auto detected = detected_with_shell(nullptr);
    AURORA_TEST_CHECK(detected.find(' ') == std::string::npos);
}

AURORA_TEST_CASE(pty_environment_defaults_carry_truecolour_capability) {
    // 两条腿共用同一份注入表：终端类程序据此选色深，缺一项就会退化成 8 色。
    AURORA_TEST_CHECK_EQ(kPtyDefaultEnvironment.count("TERM"), std::size_t{1});
    AURORA_TEST_CHECK_EQ(kPtyDefaultEnvironment.count("COLORTERM"), std::size_t{1});
    AURORA_TEST_CHECK_STREQ(kPtyDefaultEnvironment.at("TERM").c_str(), "xterm-256color");
    AURORA_TEST_CHECK_STREQ(kPtyDefaultEnvironment.at("COLORTERM").c_str(), "truecolor");
}

}  // namespace borealis::test_cases::utest_posix_local_terminal
