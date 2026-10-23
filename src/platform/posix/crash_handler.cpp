// POSIX 崩溃处理腿（`SPEC.NF.RELI.01`，裁决 7.142）。
//
// 只注册五个致命信号（段错误 / 中止 / 总线 / 浮点 / 非法指令）；处理函数只做三件事：
// 写一份固定文本 → 恢复缺省动作 → 把同一个信号再抛一次（保留系统的 core dump 与退出码
// 语义，不在本仓另造一套）。写路径一律 async-signal-safe：open/write/fsync/close/getpid/
// time/raise/_exit，加上 `CrashFactsStore` 的无锁原子读——不分配、不取锁、不碰 stdio。
// 64 KiB 备用信号栈让栈溢出型的 SIGSEGV 也有地方跑处理函数。

#include "platform/crash_handler.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <system_error>

namespace borealis::platform {
namespace {

/// 目录前缀（`<dir>/crash-`）的容量；处理函数再拼 pid 与 `.txt`，总长有富余。
constexpr std::size_t kPrefixCapacity = 4000;
constexpr std::size_t kAltStackBytes = 64U * 1024U;

char g_prefix[kPrefixCapacity] = {};
std::size_t g_prefix_size = 0;
const diag::CrashFactsStore *g_facts = nullptr;
std::array<char, kAltStackBytes> g_alt_stack{};

void emit_to_fd(void *context, const char *data, std::size_t size) noexcept {
    const int fd = *static_cast<const int *>(context);
    std::size_t written = 0;
    while (written < size) {
        const ssize_t step = ::write(fd, data + written, size - written);
        if (step < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (step == 0) {
            return;
        }
        written += static_cast<std::size_t>(step);
    }
}

[[nodiscard]] auto signal_name(int sig) noexcept -> const char * {
    switch (sig) {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGABRT:
            return "SIGABRT";
        case SIGBUS:
            return "SIGBUS";
        case SIGFPE:
            return "SIGFPE";
        case SIGILL:
            return "SIGILL";
        default:
            return "signal";
    }
}

void append_u64(char *target, std::size_t &position, std::uint64_t value) noexcept {
    char reversed[20] = {};
    std::size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    for (std::size_t i = 0; i < count; ++i) {
        target[position++] = reversed[count - 1U - i];
    }
}

void on_fatal_signal(int sig) noexcept {
    // `sizeof(g_prefix)` 的余量够 pid（≤20 位）与 ".txt"（4 字节）再加一个终止符。
    char path[sizeof(g_prefix) + 32] = {};
    std::size_t position = 0;
    for (; position < g_prefix_size; ++position) {
        path[position] = g_prefix[position];
    }
    append_u64(path, position, static_cast<std::uint64_t>(::getpid()));
    for (const char *suffix = ".txt"; *suffix != '\0'; ++suffix) {
        path[position++] = *suffix;
    }

    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
        const diag::CrashFactsSnapshot facts =
            g_facts == nullptr ? diag::CrashFactsSnapshot{} : g_facts->latest();
        diag::emit_crash_report(
            &emit_to_fd, &fd, facts, signal_name(sig), static_cast<std::uint64_t>(sig),
            static_cast<std::uint64_t>(::time(nullptr)));
        static_cast<void>(::fsync(fd));
        static_cast<void>(::close(fd));
    }

    // 恢复缺省动作并把同一个信号再抛一次：进程仍按「该信号的缺省结局」终止（含 core dump），
    // 本腿只多留一份诊断，不改退出语义。信号在进入处理器时被屏蔽，故先在本线程解除屏蔽再抛——
    // 否则再抛的那一枚只会挂起，`raise` 一返回就走到兜底的 `_exit`，core dump 与 WIFSIGNALED 都丢了。
    struct sigaction fallback {};
    fallback.sa_handler = SIG_DFL;
    static_cast<void>(::sigemptyset(&fallback.sa_mask));
    static_cast<void>(::sigaction(sig, &fallback, nullptr));
    sigset_t unblock {};
    static_cast<void>(::sigemptyset(&unblock));
    static_cast<void>(::sigaddset(&unblock, sig));
    static_cast<void>(::sigprocmask(SIG_UNBLOCK, &unblock, nullptr));
    static_cast<void>(::raise(sig));
    ::_exit(128 + sig);
}

}  // namespace

auto install_crash_handler(const CrashHandlerConfig &config) -> bool {
    if (config.directory.empty() || config.facts == nullptr) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(config.directory, error);
    if (error && !std::filesystem::is_directory(config.directory)) {
        return false;
    }
    const std::string prefix = (config.directory / "crash-").string();
    if (prefix.size() + 32U > sizeof(g_prefix)) {
        return false;
    }
    std::memcpy(g_prefix, prefix.data(), prefix.size());
    g_prefix[prefix.size()] = '\0';
    g_prefix_size = prefix.size();
    g_facts = config.facts;

    // 栈溢出型的 SIGSEGV 在缺省栈上跑不了处理函数；备用栈让诊断仍写得出来。
    stack_t alternate {};
    alternate.ss_sp = g_alt_stack.data();
    alternate.ss_size = g_alt_stack.size();
    alternate.ss_flags = 0;
    static_cast<void>(::sigaltstack(&alternate, nullptr));

    struct sigaction action {};
    action.sa_handler = &on_fatal_signal;
    action.sa_flags = SA_ONSTACK;
    static_cast<void>(::sigemptyset(&action.sa_mask));
    const int handled[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
    for (const int sig : handled) {
        if (::sigaction(sig, &action, nullptr) != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace borealis::platform
