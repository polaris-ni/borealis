// ============================================================
// POSIX local terminal unit tests (tests/unit/utest_posix_local_terminal.cpp)
// ------------------------------------------------------------
// SPEC.FEAT.CONN.01: verify $SHELL detection on Linux works and returns an existing path.
// This suite runs in the sandbox (no real PTY spawn), just checks the probe logic.
// ============================================================

#include <unistd.h>  // access, F_OK

#include "borealis/conn/local_terminal.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases {

AURORA_TEST_CASE(default_shell_is_non_empty_on_linux) {
    const auto shell = conn::default_shell_command_line();
    // On a sane Linux system at least /bin/sh must exist.
    AURORA_TEST_REQUIRE(!shell.empty());
}

AURORA_TEST_CASE(default_shell_path_exists) {
    const auto shell = conn::default_shell_command_line();
    AURORA_TEST_REQUIRE(!shell.empty());
    // Quick existence check via access().
    AURORA_TEST_CHECK(access(shell.c_str(), F_OK) == 0);
}

AURORA_TEST_CASE(pty_environment_defaults_are_present) {
    // TERM and COLORTERM must be in the injection map.
    AURORA_TEST_CHECK_EQ(conn::kPtyDefaultEnvironment.count("TERM"), std::size_t{1});
    AURORA_TEST_CHECK_EQ(conn::kPtyDefaultEnvironment.count("COLORTERM"), std::size_t{1});
    AURORA_TEST_CHECK(conn::kPtyDefaultEnvironment.at("TERM") == "xterm-256color");
    AURORA_TEST_CHECK(conn::kPtyDefaultEnvironment.at("COLORTERM") == "truecolor");
}

}  // namespace borealis::test_cases
