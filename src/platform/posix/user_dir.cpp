// ============================================================
// 用户目录来源腿：POSIX（src/platform/posix/user_dir.cpp）
// ------------------------------------------------------------
// platform::ssh_user_dir 的 POSIX 腿（SPEC.FEAT.CONN.10，裁决 7.106 D10①）。
// 只读 `HOME` 并原样交出：裁尾分隔符、拼 `/.ssh`、「恒在表首」都是纯逻辑层
// `conn::normalize_key_dirs()` 的事（那边有 13 例无头证人，这里不该有第二套）。
// ============================================================

#include "platform/user_dir.h"

#include <cstdlib>
#include <string>

namespace borealis::platform {

auto ssh_user_dir() -> std::string {
    const char *raw = std::getenv("HOME");
    if (raw == nullptr) {
        return {};  // 容器/服务账户没有 HOME：回空串，让上层「不扫」而不是「猜一个」
    }
    return std::string{raw};
}

}  // namespace borealis::platform
