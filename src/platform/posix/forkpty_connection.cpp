// ============================================================
// POSIX forkpty connection implementation (src/platform/posix/forkpty_connection.cpp)
// ------------------------------------------------------------
// This file is the entire platform knowledge for SPEC.FEAT.CONN.01 on POSIX: pseudo-terminal
// creation, child process attachment, PTY I/O and shutdown. Three points worth recording:
//   1. The PTY master fd stays open in this process for I/O; the slave side becomes the child's
//      controlling terminal via setsid + ioctl(TIOCSCTTY).
//   2. Closing the PTY master causes EOF on the slave side, so the read thread exits cleanly.
//   3. Child PID is kept until destruction so alive() can stay lock-free (architecture §3.1
//      per-session read thread means UI threads need to read it at any time).
// ============================================================

#include "forkpty_connection.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <pty.h>   // forkpty
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief Single read chunk upper bound: read thread hands blocks verbatim to the session,
///        no secondary splitting.
constexpr std::size_t kReadChunkBytes = 32768;

/// @brief Upper limit to wait for a child to actually exit after sending SIGTERM.
constexpr int kProcessExitWaitMs = 2000;

/// @brief winsize from session dimensions.
[[nodiscard]] auto make_winsize(session::Size size) noexcept -> ::winsize {
    return {static_cast<unsigned short>(size.rows),
            static_cast<unsigned short>(size.columns),
            0, 0};
}

/// @brief Build environment block: inherited → PTY defaults → spec overrides, sorted by key.
[[nodiscard]] auto build_environment(const conn::LocalTerminalSpec &spec)
    -> std::vector<std::string> {
    auto entries = std::vector<std::string>{};

    // Inherit current process environment.
    if (environ != nullptr) {
        for (auto **env = environ; *env != nullptr; ++env) {
            entries.emplace_back(*env);
        }
    }

    // Apply PTY defaults first, then spec overrides (later wins).
    for (const auto &[key, value] : conn::kPtyDefaultEnvironment) {
        // Remove existing entry with same key.
        std::erase_if(entries, [&key](const std::string &entry) {
            const auto eq_pos = entry.find('=');
            return eq_pos != std::string::npos && entry.substr(0, eq_pos) == key;
        });
        entries.push_back(key + '=' + value);
    }
    for (const auto &[key, value] : spec.environment) {
        std::erase_if(entries, [&key](const std::string &entry) {
            const auto eq_pos = entry.find('=');
            return eq_pos != std::string::npos && entry.substr(0, eq_pos) == key;
        });
        entries.push_back(key + '=' + value);
    }

    // Sort by key (required by execve convention, though less strict than Windows CreateProcessW).
    std::sort(entries.begin(), entries.end());

    return entries;
}

/// @brief Convert environment vector to char* array for execve.
[[nodiscard]] auto to_envp(const std::vector<std::string> &entries)
    -> std::vector<char*> {
    auto envp = std::vector<char*>{};
    envp.reserve(entries.size() + 1);
    for (auto &entry : entries) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): execve requires char* argv/envp,
        // but we never modify the strings through these pointers.
        envp.push_back(const_cast<char*>(entry.data()));
    }
    envp.push_back(nullptr);
    return envp;
}

}  // namespace

ForkptyConnection::ForkptyConnection(conn::LocalTerminalSpec spec, session::Size initial_size)
    : spec_{std::move(spec)}, size_{initial_size} {}

ForkptyConnection::~ForkptyConnection() {
    close();
    // Handles are reclaimed in destructor: alive() reads child PID lock-free, closing it early
    // would leave it pointing at an invalid kernel object.
    if (pty_master_ >= 0) {
        static_cast<void>(::close(pty_master_));
    }
}

auto ForkptyConnection::start(session::ConnectionEvents &events) -> void {
    if (started_.load() || closing_.load()) {
        return;
    }
    started_ = spawn(events);
    if (!started_.load()) {
        // When spawn fails we must still let the session receive stream-end, otherwise the tab
        // stays on a blank screen and cannot be closed.
        events.on_closed();
    }
}

auto ForkptyConnection::spawn(session::ConnectionEvents &events) -> bool {
    const auto command_line =
        spec_.command_line.empty() ? conn::default_shell_command_line() : spec_.command_line;
    if (command_line.empty()) {
        AURORA_LOG_ERROR("platform", "no local terminal command line available");
        return false;
    }

    auto environment_entries = build_environment(spec_);
    auto envp = to_envp(environment_entries);

    // Parse command line: split on spaces, honouring quotes.
    // Simple split: first token is the executable, rest are arguments.
    auto args = std::vector<std::string>{};
    auto current = std::string{};
    auto in_quote = false;
    for (std::size_t i = 0; i < command_line.size(); ++i) {
        const char c = command_line[i];
        if (c == '"') {
            in_quote = !in_quote;
        } else if (c == ' ' && !in_quote) {
            if (!current.empty()) {
                args.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        args.push_back(std::move(current));
    }

    if (args.empty()) {
        AURORA_LOG_ERROR("platform", "empty command line after parsing");
        return false;
    }

    // Convert args to char* array for execvp.
    auto argv = std::vector<char*>{};
    argv.reserve(args.size() + 1);
    for (auto &arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    auto winsz = make_winsize(size_);
    int pty_master = -1;

    const auto pid = forkpty(&pty_master, nullptr, nullptr, &winsz);
    if (pid < 0) {
        AURORA_LOG_ERROR("platform", "forkpty failed: ", std::strerror(errno));
        return false;
    }

    if (pid == 0) {
        // Child process: become session leader, set controlling terminal.
        ::setsid();

        // Set working directory if specified.
        if (!spec_.working_directory.empty()) {
            if (::chdir(spec_.working_directory.c_str()) != 0) {
                // Ignore chdir failure, fall back to home directory behaviour.
            }
        }

        // Execute the command.
        // NOLINTNEXTLINE(concurrency-mt-unsafe): exec replaces the process image, no concurrency.
        ::execvp(argv[0], argv.data());

        // If exec fails, log and exit.
        AURORA_LOG_ERROR("platform", "exec failed for '", argv[0], "': ", std::strerror(errno));
        ::_exit(127);
    }

    // Parent process.
    pty_master_ = pty_master;
    child_pid_ = pid;
    events_ = &events;

    // Set PTY to raw mode on the master side.
    auto termios = ::termios{};
    if (::tcgetattr(pty_master_, &termios) == 0) {
        ::cfmakeraw(&termios);
        static_cast<void>(::tcsetattr(pty_master_, TCSANOW, &termios));
    }

    // Set non-blocking read on master fd.
    const auto flags = ::fcntl(pty_master_, F_GETFL, 0);
    if (flags >= 0) {
        static_cast<void>(::fcntl(pty_master_, F_SETFL, flags | O_NONBLOCK));
    }

    reader_ = std::thread([this] { read_loop(); });
    return true;
}

auto ForkptyConnection::read_loop() -> void {
    auto buffer = std::vector<std::byte>(kReadChunkBytes);
    for (;;) {
        const auto bytes_read = ::read(pty_master_, buffer.data(), buffer.size());
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Non-blocking: yield briefly and retry.
                ::usleep(1000);
                continue;
            }
            // Real error or EOF.
            break;
        }
        if (bytes_read == 0) {
            // EOF: child exited.
            break;
        }
        events_->on_bytes({buffer.data(), static_cast<std::size_t>(bytes_read)});
    }
    events_->on_closed();
}

auto ForkptyConnection::write(std::span<const std::byte> bytes) -> void {
    const std::lock_guard lock{write_mutex_};
    if (closing_.load() || pty_master_ < 0) {
        return;
    }
    auto remaining = bytes;
    while (!remaining.empty()) {
        const auto written = ::write(pty_master_, remaining.data(), remaining.size());
        if (written <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ::usleep(1000);
                continue;
            }
            return;  // Child has exited, keystrokes are discarded.
        }
        remaining = remaining.subspan(static_cast<std::size_t>(written));
    }
}

auto ForkptyConnection::resize(session::Size size) -> void {
    const std::lock_guard lock{state_mutex_};
    if (closing_.load() || pty_master_ < 0) {
        return;
    }
    size_ = size;
    auto winsz = make_winsize(size);
    if (::ioctl(pty_master_, TIOCSWINSZ, &winsz) != 0) {
        AURORA_LOG_WARN("platform", "ioctl TIOCSWINSZ failed: ", std::strerror(errno));
    }
}

auto ForkptyConnection::close() -> void {
    if (closing_.exchange(true)) {
        // Another closer is already in progress:约定 is that after return no more callbacks happen,
        // so just wait for the read thread to exit.
        if (reader_.joinable()) {
            reader_.join();
        }
        return;
    }
    {
        const std::lock_guard lock{state_mutex_};
        if (pty_master_ >= 0) {
            ::close(pty_master_);
            pty_master_ = -1;  // Read pipe EOFs, read thread hands remaining buffer to session then exits.
        }
    }
    if (reader_.joinable()) {
        reader_.join();
    }
    // Send SIGTERM to child process; wait briefly for clean exit.
    if (child_pid_ > 0) {
        ::kill(child_pid_, SIGTERM);
        auto status = int{0};
        for (int i = 0; i < kProcessExitWaitMs / 10; ++i) {
            const auto result = ::waitpid(child_pid_, &status, WNOHANG);
            if (result > 0 || (result < 0 && errno == ECHILD)) {
                break;
            }
            ::usleep(10000);
        }
        // Force kill if still alive.
        if (::waitpid(child_pid_, &status, WNOHANG) == 0) {
            ::kill(child_pid_, SIGKILL);
            static_cast<void>(::waitpid(child_pid_, &status, 0));
        }
    }
}

auto ForkptyConnection::alive() const noexcept -> bool {
    if (closing_.load()) {
        return false;
    }
    if (child_pid_ <= 0) {
        return false;
    }
    auto status = int{0};
    const auto result = ::waitpid(child_pid_, &status, WNOHANG);
    return result == 0;  // 0 means still running, >0 means exited, <0 means error/no child.
}

}  // namespace borealis::platform
