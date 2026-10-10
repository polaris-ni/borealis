// ============================================================
// 私钥文件创建腿：POSIX（src/platform/posix/file_secure.cpp）
// ------------------------------------------------------------
// platform::create_private_file 的 POSIX 腿（SPEC.FEAT.CONN.10，裁决 7.106 D5①）。
// 裸 open/fchmod 知识不外溢：接口见 platform/file_secure.h（AGENTS.md §4.5 第 23 条）。
//
// 三道闸收在一次 open 里：
//   ⑴ `O_CREAT|O_EXCL` ⇒ 路径已存在（含符号链接）即 `EEXIST`，库侧的静默改写（F5）拿不到机会；
//   ⑵ mode 实参 `0600` 只是**起点**——它受 umask 支配（本机 umask 002，F6 实测 libssh 落成 0664），
//      故紧接一次 `fchmod(fd, 0600)` 无条件钉死，与调用方 umask 取值彻底无关；
//   ⑶ 成功后立刻关闭：本腿只负责「让这份路径以 0600 存在且此前不存在」，内容由传输腿
//      以截断方式写入（`open` 对已存在文件忽略 mode 实参，⑵ 的权限因此保住），写完再 rename 就位。
// ============================================================

#include "platform/file_secure.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief 关掉可能持有的 fd（-1 时什么都不做）。
auto close_if_open(int fd) -> void {
    if (fd >= 0) {
        static_cast<void>(::close(fd));
    }
}

}  // namespace

auto create_private_file(std::string_view path) -> PrivateFileOutcome {
    // std::string_view 不保证 NUL 结尾，内嵌 NUL 还会让 open 只看见前半截：两者都归「名字非法」。
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        AURORA_LOG_ERROR("platform", "create_private_file refused: the path is empty or contains a NUL");
        return PrivateFileOutcome::Error;
    }

    const std::string target{path};
    const int fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        // EEXIST 是「已存在」的**唯一**合法解释：O_EXCL 下 EACCES/EISDIR 等都可能来自路径上的
        // 某一环（父目录不存在即 ENOENT），那些情况不回退成 AlreadyExists，否则调用方会
        // 把「建不了」误读成「别覆盖」。
        if (errno == EEXIST) {
            return PrivateFileOutcome::AlreadyExists;
        }
        AURORA_LOG_ERROR("platform", "create_private_file open failed: ", target, " - ", std::strerror(errno));
        return PrivateFileOutcome::Error;
    }

    if (::fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        const auto saved = errno;
        AURORA_LOG_ERROR("platform", "create_private_file chmod failed: ", target, " - ", std::strerror(saved));
        close_if_open(fd);
        // 权限钉不死就把它删掉：留下一个同组可读的私钥空壳，比「建失败」更糟。
        static_cast<void>(::unlink(target.c_str()));
        return PrivateFileOutcome::Error;
    }

    close_if_open(fd);
    return PrivateFileOutcome::Created;
}

}  // namespace borealis::platform
