// ============================================================
// 进程参数来源腿：Win32（src/platform/win/process_args.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.04`：窄 `argv` 在 MSVC 上按 ANSI 码页折过一次，`--profile 中文名`
// 会在进入解析器之前就已缺字；权威来源是 `GetCommandLineW` 的宽字符命令行。
// 宽字符只在平台层出现，公共面仍是 UTF-8（AGENTS.md §4.5 第 23 条）。
// **本沙箱无 MSVC，该腿未经编译验证**（与其余 win 腿同档，属推断/未验证）。
// ============================================================

#include "platform/process_args.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shellapi.h>

#include <iterator>
#include <string_view>

#include "aurora/core/log.h"
#include "platform/win/win_text.h"

namespace borealis::platform {

auto process_arguments(int argc, char **argv) -> std::vector<std::string> {
    const wchar_t *command_line = ::GetCommandLineW();
    if (command_line != nullptr) {
        int count = 0;
        LPWSTR *parts = ::CommandLineToArgvW(command_line, &count);
        if (parts != nullptr) {
            std::vector<std::string> out;
            if (count > 0) {
                out.reserve(static_cast<std::size_t>(count));
            }
            for (int i = 0; i < count; ++i) {
                out.push_back(to_utf8(std::wstring_view{parts[i]}));
            }
            static_cast<void>(::LocalFree(parts));
            return out;
        }
        AURORA_LOG_WARN("platform", "CommandLineToArgvW failed, falling back to the narrow argv (win32 ",
                        static_cast<long>(::GetLastError()), ")");
    }
    if (argc <= 0 || argv == nullptr) {
        return {};
    }
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        out.emplace_back(argv[i] == nullptr ? "" : argv[i]);
    }
    return out;
}

}  // namespace borealis::platform
