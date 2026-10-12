// ============================================================
// 打开外部 URI 的平台腿：Win32（src/platform/win/open_url.cpp）
// ------------------------------------------------------------
// platform::open_url 的 Win32 腿（SPEC.FEAT.INTERACT.05，裁决 7.119 D10①）。
//
// **本沙箱无 MSVC，本腿未经编译验证：下面每一条 API 语义、返回档映射与链接需求
// 都属推断／未验证**（AGENTS.md §4.2 第 11 条要求就地标明，走查清单给它独立一条）。
// 与其余 win 腿同档（`platform/win/user_dir.cpp` 头注那一句）。
//
// 为什么是 `ShellExecuteW` 而不是自己找宿主程序：`open` 动词走的是注册表的
// `http`/`https` 协议关联，浏览器是什么、在哪、怎么起都由 Shell 决定——这正是
// POSIX 腿 `xdg-open` 扮演的角色，两腿的「不承诺打开成功」（头注⑵）因此同形。
// 宽字符入参的理由与 `to_wide` 同一条：URI 里的百分号编码与非 ASCII 主机名不该
// 被 ANSI 码页折平（裁决 7.11 把码页 API 挡在 `src/platform/` 内）。
// ============================================================

#include "platform/open_url.h"

#include <windows.h>

#include <shellapi.h>
#include <string>
#include <string_view>

#include "aurora/core/log.h"
#include "platform/win/win_text.h"

namespace borealis::platform {

namespace {

/// @brief 入参是否不可交出去：空串，或含任何 C0 控制字符 / DEL。
///
/// 与 POSIX 腿同一条判据、各腿各写一份：两腿之间没有共享的 `.cpp`（`src/CMakeLists.txt`
/// 的显式源列表按平台分列），为一条三行的判据新造一层共享件不值（AGENTS §4.1 第 3 条）。
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

}  // namespace

auto open_url(std::string_view utf8_target) -> OpenUrlOutcome {
    if (is_unacceptable(utf8_target)) {
        AURORA_LOG_ERROR("platform", "open_url refused: target is empty or holds a control character");
        return OpenUrlOutcome::Error;
    }
    const std::wstring target = to_wide(utf8_target);
    if (target.empty()) {
        // `to_wide` 的失败形态是空串（含非法 UTF-8 段被丢弃），空串交给 Shell 只会得到一个
        // 「打开我的文档」式的猜——那不是本函数的语义。
        AURORA_LOG_ERROR("platform", "open_url refused: target did not survive the UTF-16 conversion");
        return OpenUrlOutcome::Error;
    }

    // `SEE_MASK_FLAG_NO_UI` 一类的窗口标志本期不用：关联缺失时宁可回到这里记日志，
    // 也不要 Shell 弹它自己的对话框（那扇窗不在本产品的界面面里，无法断言也无法关闭）。
    const HINSTANCE result =
        ::ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<INT_PTR>(result);
    if (code > 32) {
        return OpenUrlOutcome::Opened;  // 文档口径：返回值 >32 即成功（**推断／未验证**）
    }
    if (code == static_cast<INT_PTR>(SE_ERR_NOASSOC) || code == static_cast<INT_PTR>(SE_ERR_DDEFAIL)) {
        // 没有协议关联＝POSIX 腿那句「PATH 上没有桥」，同一档语义。
        AURORA_LOG_WARN("platform", "open_url: the shell has no association for this URI");
        return OpenUrlOutcome::NotFound;
    }
    AURORA_LOG_ERROR("platform", "open_url ShellExecuteW failed, code=", static_cast<long>(code));
    return OpenUrlOutcome::Error;
}

}  // namespace borealis::platform
