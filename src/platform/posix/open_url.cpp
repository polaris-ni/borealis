// ============================================================
// 打开外部 URI 的平台腿：POSIX（src/platform/posix/open_url.cpp）
// ------------------------------------------------------------
// platform::open_url 的 POSIX 腿（SPEC.FEAT.INTERACT.05，裁决 7.119 D10①）。
// 形态取自 §0 三条一手读数：F13（双 fork 不留尸，176.97 µs）、F14（exec 不经 shell ⇒
// 注入面封住）、F15（本机 `xdg-open` 1.2.1 可用且真的会开窗口 ⇒ 本腿只由装配层装进
// `Presentation::open_url`，自动化一律走替身）。
//
// 为什么是**双** fork：单 fork 不 wait 会留僵尸（F13 的对照组 200 ms 后 `ps` 显示 `Z`），
// 而本仓的 `waitpid` 只呼具名 `child_pid_`（`src/platform/posix/forkpty_connection.cpp` 的
// `ForkptyConnection::close` 与 `::alive` 两处），没有任何腿会顺手替我们收掉这条孙。双 fork 让
// 中间父成为本进程的直接子、且它 `_exit` 得极快，本进程 `waitpid(中间父)` 即收清；孙由 init
// 收养，与本进程再无隶属关系。
//
// 线程口径：见 `platform/open_url.h` 头注⑴——它不等子进程、不等 IO 完成，故**可以**从
// UI 事件回调直接调（与 `file_secure.h` 那句「须 worker 线程」不同，那条的前提是阻塞 IO）。
// ============================================================

#include "platform/open_url.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief 桌面环境的通用打开桥（本期不做 `gio` / `kde-open` / `dbus-send` 回落，D10① 的代价 ⑵）。
constexpr const char *kOpenBridge = "xdg-open";

/// @brief 入参是否不可交出去：空串，或含任何 C0 控制字符 / DEL。
///
/// 链接字符表本就不产这样的串（`ui::find_link_at` 的白名单表），收到它们意味着调用方越过了
/// 识别件；这一档**显式失败而不清洗**，因为「猜一个安全形态」正是把注入面重新打开的方式。
[[nodiscard]] auto is_unacceptable(std::string_view target) -> bool {
    if (target.empty()) {
        return true;
    }
    for (const char byte : target) {
        const auto code = static_cast<unsigned char>(byte);
        if (code < 0x20U || code == 0x7FU) {
            return true;
        }
    }
    return false;
}

/// @brief 在 `PATH` 上找那枚可执行的桥，交出它的绝对路径；找不到交回空串。
///
/// 刻意**先找后 fork**：`NotFound` 因此是一条读点而不是猜——它由 `access(X_OK)` 给出，
/// 与孙进程 `execv` 失败（`_exit(127)`，父侧收不到）不是同一件事。空项不当作 cwd（那是
/// `execvp` 自带的语义，本腿不复用），`PATH` 缺失即「没有桥」。
[[nodiscard]] auto locate_bridge(std::array<char, 4096> &buffer) -> std::string_view {
    const char *path = std::getenv("PATH");
    if (path == nullptr) {
        return {};
    }
    const std::string_view entries{path};
    std::size_t from = 0U;
    while (from <= entries.size()) {
        const std::size_t sep = entries.find(':', from);
        const std::size_t to = (sep == std::string_view::npos) ? entries.size() : sep;
        const std::string_view dir = entries.substr(from, to - from);
        from = (sep == std::string_view::npos) ? entries.size() + 1U : sep + 1U;
        if (dir.empty()) {
            continue;
        }
        const std::size_t needed = dir.size() + 1U + std::strlen(kOpenBridge);
        if (needed >= buffer.size()) {
            continue;  // 异常长的目录项：跳过而不是截断成另一枚路径
        }
        std::memcpy(buffer.data(), dir.data(), dir.size());
        buffer[dir.size()] = '/';
        std::memcpy(buffer.data() + dir.size() + 1U, kOpenBridge, std::strlen(kOpenBridge) + 1U);
        if (::access(buffer.data(), X_OK) == 0) {
            return std::string_view{buffer.data()};
        }
    }
    return {};
}

/// @brief fork 失败时子侧的收尾：`_exit` 而非 `exit`，不跑 atexit、不冲 stdio 缓冲。
auto child_fail() -> void {
    ::_exit(1);
}

}  // namespace

auto open_url(std::string_view utf8_target) -> OpenUrlOutcome {
    if (is_unacceptable(utf8_target)) {
        AURORA_LOG_ERROR("platform", "open_url refused: target is empty or holds a control character");
        return OpenUrlOutcome::Error;
    }
    std::array<char, 4096> bridge_path{};
    const std::string_view bridge = locate_bridge(bridge_path);
    if (bridge.empty()) {
        AURORA_LOG_WARN("platform", "open_url: no ", kOpenBridge, " on PATH, nothing was asked of the desktop");
        return OpenUrlOutcome::NotFound;
    }

    // URI 只以 argv 的一枚参数交出：argv 里没有任何元字符需要转义，因为压根没有 shell 参与（F14）。
    // 这里的 `target` 必须是一份**活到本函数返回**的拷贝：argv 存的是指针，孙进程在 `execv` 之前
    // 只组 argv 不做别的，而一份临时串会在整表达式结束时先析构，交出去的就是悬垂指针。
    const std::string target{utf8_target};
    const pid_t middle = ::fork();
    if (middle < 0) {
        AURORA_LOG_ERROR("platform", "open_url fork failed: ", std::strerror(errno));
        return OpenUrlOutcome::Error;
    }
    if (middle == 0) {
        // 中间父：只做一件事——把孙推给 init 收养，然后立刻退出，好让本进程那一次 `waitpid` 收得干净。
        const pid_t grandchild = ::fork();
        if (grandchild < 0) {
            child_fail();
        }
        if (grandchild == 0) {
            (void)::setsid();  // 脱离本进程组：终端的 Ctrl-C 与作业控制不该打到人家的浏览器上
            const std::array<char *, 3> argv{const_cast<char *>(kOpenBridge),
                                             const_cast<char *>(target.c_str()), nullptr};
            (void)::execv(bridge.data(), const_cast<char *const *>(argv.data()));
            ::_exit(127);  // execv 失败：父侧收不到这一档（头注⑵），故只在日志面之外
        }
        ::_exit(0);
    }

    int status = 0;
    do {
        const pid_t reaped = ::waitpid(middle, &status, 0);
        if (reaped == middle) {
            break;
        }
        if (reaped < 0 && errno != EINTR) {
            AURORA_LOG_ERROR("platform", "open_url waitpid failed: ", std::strerror(errno));
            return OpenUrlOutcome::Error;
        }
    } while (true);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        // 这里能判出的只有「中间父没能把孙 fork 出来」；孙自己的成败按头注⑵ 不承诺。
        AURORA_LOG_ERROR("platform", "open_url: the bridge process was not handed off");
        return OpenUrlOutcome::Error;
    }
    return OpenUrlOutcome::Opened;
}

}  // namespace borealis::platform
