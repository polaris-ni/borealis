// ============================================================
// 用户目录来源腿：Win32（src/platform/win/user_dir.cpp）
// ------------------------------------------------------------
// platform::ssh_user_dir 的 Win32 腿（SPEC.FEAT.CONN.10，裁决 7.106 D10①）。
// **本沙箱无 MSVC，该腿未经编译验证**（与其余 win 腿同档，属推断/未验证）。
//
// 为什么走宽字符：用户名可以是非 ASCII，`std::getenv` 在 MSVC 上给的是 ANSI 码页
// 那一版，中文用户名会被折成问号，拼出来的 `~/.ssh` 就是一个不存在的目录。
// ============================================================

#include "platform/user_dir.h"

#include <wchar.h>

#include <string>

#include "platform/win/win_text.h"

namespace borealis::platform {

auto ssh_user_dir() -> std::string {
    // OpenSSH for Windows 认 `%USERPROFILE%\.ssh`。`HOMEDRIVE`+`HOMEPATH` 是登录脚本
    // 时代的老变体，本期不采：采两套就得再定「两者不一致时听谁的」，那是凭空多一个真值源。
    const wchar_t *raw = ::_wgetenv(L"USERPROFILE");
    if (raw == nullptr) {
        return {};
    }
    return to_utf8(std::wstring_view{raw});
}

}  // namespace borealis::platform
