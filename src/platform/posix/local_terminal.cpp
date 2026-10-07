// ============================================================
// POSIX local terminal factory (src/platform/posix/local_terminal.cpp)
// ------------------------------------------------------------
// Two legs for SPEC.FEAT.CONN.01 on POSIX: default shell detection and connection factory.
//
// Detection order $SHELL → /bin/bash → /bin/sh follows common Linux/macOS practice: honour the
// user's preferred shell first, fall back to bash if available, then to the POSIX baseline sh.
// Detection only checks existence, does not start any process, so it can be called on the UI
// thread without introducing latency.
// ============================================================

#include "borealis/conn/local_terminal.h"

#include <unistd.h>  // access, environ

#include <cstdlib>
#include <string>
#include <vector>

#include "forkpty_connection.h"

namespace borealis::conn {

namespace {

/// @brief Check if a path is an existing regular file (directories and non-existent return false).
[[nodiscard]] auto file_exists(const std::string &path) -> bool {
    return access(path.c_str(), F_OK) == 0;
}

/// @brief Read an environment variable value.
[[nodiscard]] auto environment_value(const char *name) -> std::string {
    const auto *value = std::getenv(name);
    return value != nullptr ? std::string{value} : std::string{};
}

}  // namespace

auto default_shell_command_line() -> std::string {
    // $SHELL: user's preferred shell, honoured first.
    if (auto shell = environment_value("SHELL"); !shell.empty()) {
        if (file_exists(shell)) {
            return shell;
        }
    }

    // /bin/bash: common default on most Linux distributions.
    if (file_exists("/bin/bash")) {
        return "/bin/bash";
    }

    // /bin/sh: POSIX baseline, always present on conforming systems.
    if (file_exists("/bin/sh")) {
        return "/bin/sh";
    }

    return {};
}

auto make_local_terminal_connection(const LocalTerminalSpec &spec, session::Size initial_size)
    -> std::unique_ptr<session::Connection> {
    return std::make_unique<platform::ForkptyConnection>(spec, initial_size);
}

}  // namespace borealis::conn
