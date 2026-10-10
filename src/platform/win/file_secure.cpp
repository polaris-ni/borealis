// ============================================================
// 私钥文件创建腿：Win32（src/platform/win/file_secure.cpp）
// ------------------------------------------------------------
// platform::create_private_file 的 Win32 腿（SPEC.FEAT.CONN.10，裁决 7.106 D5②）。
//
// Win32 没有 POSIX 的 mode 实参：文件的可读性由**目录 ACL 继承**决定，CRT 的 `_S_IREAD|_S_IWRITE`
// 只映射成「是否带只读属性位」。本腿做到能与不能做的分界如下——
//   ⑴ 「不覆盖既有文件」能等价做到：`_O_CREAT|_O_EXCL` 在路径已存在时失败（`EEXIST`），
//      目录与符号链接另有属性判断兜底（见下）；
//   ⑵ 「只我可读」在 Windows 的常规家用/开发机上**本就成立**：用户 Profile 目录的继承 ACL
//      一般只授给当前用户与 Administrators，故 0600 的语义目标已达成；
//   ⑶ 本腿**不主动改写 ACL**——设 ACL 要走 `SetNamedSecurityInfoW` + 安全描述符，一旦设错
//      会让用户自己的备份/杀软读不到私钥，代价远大于收益。故按裁决 7.106 D5② 只留痕不改：
//      每次成功建文件记一条 WARN，说明「权限由目录继承决定、本腿未收紧」，把判断交给人。
// 因此 Windows 腿的验收口径是「不覆盖 + 能建」，**不是**「等价 0600」；稿 §7 的走查项把这条列成
// 需要在真机确认的边界（本沙箱无 MSVC，本文件未经编译验证）。
// ============================================================

#include "platform/file_secure.h"

#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <cerrno>
#include <string>
#include <string_view>

#include "aurora/core/log.h"
#include "win_text.h"

namespace borealis::platform {

auto create_private_file(std::string_view path) -> PrivateFileOutcome {
    // 与 POSIX 腿同一条前置：空串与内嵌 NUL 都是非法名字（to_wide 会静默丢掉那段，
    // 于是可能建到另一份路径上——那是比失败更糟的结果）。
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        AURORA_LOG_ERROR("platform", "create_private_file refused: the path is empty or contains a NUL");
        return PrivateFileOutcome::Error;
    }
    const auto wide = to_wide(path);
    if (wide.empty()) {
        AURORA_LOG_ERROR("platform", "create_private_file refused: the path is not valid UTF-8");
        return PrivateFileOutcome::Error;
    }

    // 先 open 再判属性：成功路径上没有 TOCTOU 窗口，属性检查只用来给**失败**分类。
    int fd = _wopen(wide.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd >= 0) {
        static_cast<void>(_close(fd));
        AURORA_LOG_WARN("platform",
                        "create_private_file created a key file without tightening its ACL: "
                        "readability follows the parent directory on Windows");
        return PrivateFileOutcome::Created;
    }
    if (errno == EEXIST) {
        return PrivateFileOutcome::AlreadyExists;
    }
    // CRT 在「路径已存在但是目录/重解析点」时报的是 EACCES 而非 EEXIST，
    // 按接口契约（符号链接与目录都算已存在）用属性判定归位。
    const auto attributes = GetFileAttributesW(wide.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return PrivateFileOutcome::AlreadyExists;
    }
    AURORA_LOG_ERROR("platform", "create_private_file open failed on Windows, errno: ", errno);
    return PrivateFileOutcome::Error;
}

}  // namespace borealis::platform
