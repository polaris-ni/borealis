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
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <pty.h>   // forkpty
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief Single read chunk upper bound: read thread hands blocks verbatim to the session,
///        no secondary splitting.
constexpr std::size_t kReadChunkBytes = 32768;

/// @brief Upper limit to wait for a child to actually exit after sending SIGTERM.
constexpr int kProcessExitWaitMs = 2000;

/// @brief Read thread poll timeout: close() does not interrupt a blocked poll, so the loop
///        re-checks the shutdown flag on every timeout. Deliberately far above the 1 ms spin
///        of the first cut, which kept a core busy while a session sat idle (SPEC.NF.PERF.05).
constexpr int kReadPollTimeoutMs = 50;

/// @brief `winsize` members are `unsigned short`: an out-of-range grid would wrap into a
///        nonsense size. Same bound as the Win32 leg's `COORD` clamp so both legs hand the
///        child the same dimensions for the same grid.
constexpr std::size_t kMaxPtyDimension = 32767;

/// @brief Clamp one dimension the way the Win32 leg does (never below 1, never above 32767).
[[nodiscard]] auto clamp_dimension(std::size_t value) noexcept -> unsigned short {
    return static_cast<unsigned short>(std::clamp(value, std::size_t{1}, kMaxPtyDimension));
}

/// @brief winsize from session dimensions.
[[nodiscard]] auto make_winsize(session::Size size) noexcept -> ::winsize {
    return {clamp_dimension(size.rows), clamp_dimension(size.columns), 0, 0};
}

/// @brief Is @p c a field separator for the command line split.
[[nodiscard]] auto is_separator(char c) noexcept -> bool {
    return c == ' ' || c == '\t';
}

/// @brief Split a command line into argv with the three quoting forms this app can be handed:
///        single quotes (fully literal), double quotes (backslash escapes `"` and `\`), and a
///        backslash outside quotes (escapes the next character).
///
///        The Win32 leg does not need this: it hands the whole string to `CreateProcessW` and
///        takes that parser's argv. Keeping the same shape here is what makes one configured
///        command line mean one argument on both platforms (SPEC.NF.PLAT.01) -- with only
///        double quotes honoured, `ssh host 'a b'` split into two arguments on Linux.
///        An unterminated quote simply ends at the string, and a quoted empty token is still
///        an argument (`echo ""` prints a blank line on either platform).
[[nodiscard]] auto tokenize_command_line(std::string_view command_line)
    -> std::vector<std::string> {
    auto args = std::vector<std::string>{};
    std::size_t cursor = 0U;
    while (cursor < command_line.size()) {
        if (is_separator(command_line[cursor])) {
            ++cursor;
            continue;
        }
        auto token = std::string{};
        auto token_started = false;
        while (cursor < command_line.size() && !is_separator(command_line[cursor])) {
            const auto c = command_line[cursor];
            token_started = true;
            ++cursor;
            if (c == '\'' || c == '"') {
                while (cursor < command_line.size() && command_line[cursor] != c) {
                    const auto inner = command_line[cursor];
                    const auto escaped = c == '"' && inner == '\\' &&
                                         cursor + 1U < command_line.size() &&
                                         (command_line[cursor + 1U] == '"' ||
                                          command_line[cursor + 1U] == '\\');
                    if (escaped) {
                        token.push_back(command_line[cursor + 1U]);
                        cursor += 2U;
                    } else {
                        token.push_back(inner);
                        ++cursor;
                    }
                }
                if (cursor < command_line.size()) {
                    ++cursor;  // consume the closing quote
                }
            } else if (c == '\\' && cursor < command_line.size()) {
                token.push_back(command_line[cursor]);
                ++cursor;
            } else {
                token.push_back(c);
            }
        }
        if (token_started) {
            args.push_back(std::move(token));
        }
    }
    return args;
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

    // Sort by key so the handed-over block is stable (the child installs it as `environ`).
    std::sort(entries.begin(), entries.end());

    return entries;
}

/// @brief Convert environment vector to the nullptr-terminated char* array `environ` expects.
[[nodiscard]] auto to_envp(const std::vector<std::string> &entries)
    -> std::vector<char*> {
    auto envp = std::vector<char*>{};
    envp.reserve(entries.size() + 1);
    for (auto &entry : entries) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the environ block is char*, but
        // nothing here writes through these pointers.
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

    const auto args = tokenize_command_line(command_line);
    if (args.empty()) {
        AURORA_LOG_ERROR("platform", "empty command line after parsing: ", command_line);
        return false;
    }

    // Convert args to char* array for execvp.
    auto argv = std::vector<char*>{};
    argv.reserve(args.size() + 1);
    for (const auto &arg : args) {
        argv.push_back(const_cast<char*>(arg.data()));  // NOLINT: execvp takes char* but never writes
    }
    argv.push_back(nullptr);

    // Resolve the working directory *before* the fork. `CreateProcessW` fails the whole launch
    // on a bad lpCurrentDirectory, and a child that silently stayed in $HOME would leave the
    // user looking at a shell that ignored their configured startup directory with no trace.
    // O_CLOEXEC closes the descriptor across exec, so only the parent has to release it.
    auto working_directory_fd = int{-1};
    if (!spec_.working_directory.empty()) {
        working_directory_fd =
            ::open(spec_.working_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (working_directory_fd < 0) {
            AURORA_LOG_ERROR("platform", "cannot open working directory '",
                             spec_.working_directory, "': ", std::strerror(errno));
            return false;
        }
    }

    auto winsz = make_winsize(size_);
    int pty_master = -1;

    const auto pid = forkpty(&pty_master, nullptr, nullptr, &winsz);
    if (pid < 0) {
        if (working_directory_fd >= 0) {
            static_cast<void>(::close(working_directory_fd));
        }
        AURORA_LOG_ERROR("platform", "forkpty failed: ", std::strerror(errno));
        return false;
    }

    if (pid == 0) {
        // Child process: become session leader, set controlling terminal.
        ::setsid();

        // The parent already opened and validated the directory, so the only way fchdir fails is
        // a race that no error report from a half-spawned child could improve on; exec closes the
        // descriptor either way (O_CLOEXEC).
        static_cast<void>(::fchdir(working_directory_fd));

        // Hand `execvp` the constructed environment. POSIX lets a process replace the `environ`
        // array, and doing it here (rather than a `setenv` loop) is what also honours *removing*
        // an inherited entry; `execvp` still searches PATH, which `execve` would not.
        // The child is single-threaded from fork until exec, so nothing else reads it meanwhile.
        ::environ = envp.data();

        // Execute the command.
        // NOLINTNEXTLINE(concurrency-mt-unsafe): exec replaces the process image, no concurrency.
        ::execvp(argv[0], argv.data());

        // If exec fails, log and exit.
        AURORA_LOG_ERROR("platform", "exec failed for '", argv[0], "': ", std::strerror(errno));
        ::_exit(127);
    }

    if (working_directory_fd >= 0) {
        static_cast<void>(::close(working_directory_fd));
    }

    // Parent process.
    pty_master_ = pty_master;
    child_pid_ = pid;
    events_ = &events;

    // The PTY is deliberately left in the kernel's default cooked mode. On Linux the master and
    // the slave share ONE termios struct, so a `cfmakeraw` here was measured to clear `-icanon`,
    // `-icrnl` and `-echo` on the child's terminal too: Enter stopped meaning "submit the line"
    // for every program that relies on the line discipline (dash, python's input(), a `read`
    // loop) while bash hid it by doing its own editing. Real terminals pass bytes through and let
    // the child's line discipline decide, which is also the shape the ConPTY leg has.

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
        // Take the descriptor under the same lock close() uses, and re-check shutdown on every
        // poll timeout: closing an fd does not reliably wake a blocked poll on another thread.
        auto fd = int{-1};
        {
            const std::lock_guard lock{state_mutex_};
            if (closing_.load() || pty_master_ < 0) {
                break;
            }
            fd = pty_master_;
        }
        auto waiter = ::pollfd{};
        waiter.fd = fd;
        waiter.events = POLLIN;
        const auto ready = ::poll(&waiter, 1, kReadPollTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;  // timeout: nothing arrived, loop re-checks shutdown
        }
        const auto bytes_read = ::read(fd, buffer.data(), buffer.size());
        if (bytes_read > 0) {
            events_->on_bytes({buffer.data(), static_cast<std::size_t>(bytes_read)});
            continue;
        }
        if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;  // spurious wakeup: the fd is draining, nothing to hand over
        }
        break;  // EOF (child exited) or a real error
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
