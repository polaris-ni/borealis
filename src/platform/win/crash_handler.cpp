// Win32 崩溃处理腿（`SPEC.NF.RELI.01`，裁决 7.142）。
//
// 形态是 `SetUnhandledExceptionFilter`：异常过滤器在崩溃线程上跑，只做「拼路径 → CreateFileW →
// 写固定文本 → FlushFileBuffers → 放行」。不用堆分配（pid 用栈上十进制展开），不再吞掉异常——
// 返回 `EXCEPTION_CONTINUE_SEARCH` 让 Windows 自己的终止与 WER 流程照旧发生。
//
// **本腿未在 Windows/MSVC 上编译验证**（推断 / 未验证）：Linux 主通道只验 POSIX 腿；
// `abort()` 走的 CRT 路径不经过 UEF，这一档在 Windows 上留白（与裁决 7.142⑤ 同口径）。

#include "platform/crash_handler.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <string>
#include <system_error>

namespace borealis::platform {
namespace {

constexpr std::size_t kPrefixCapacity = 4000;

wchar_t g_prefix[kPrefixCapacity] = {};
std::size_t g_prefix_size = 0;
const diag::CrashFactsStore *g_facts = nullptr;

void emit_to_handle(void *context, const char *data, std::size_t size) noexcept {
    const HANDLE handle = *static_cast<const HANDLE *>(context);
    DWORD written = 0;
    ::WriteFile(handle, data, static_cast<DWORD>(size), &written, nullptr);
}

[[nodiscard]] auto exception_name(DWORD code) noexcept -> const char * {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW:
            return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:
            return "EXCEPTION_IN_PAGE_ERROR";
        default:
            return "EXCEPTION";
    }
}

void append_u64(wchar_t *target, std::size_t &position, std::uint64_t value) noexcept {
    wchar_t reversed[20] = {};
    std::size_t count = 0;
    do {
        reversed[count++] = static_cast<wchar_t>(L'0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    for (std::size_t i = 0; i < count; ++i) {
        target[position++] = reversed[count - 1U - i];
    }
}

LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS *info) noexcept {
    wchar_t path[sizeof(g_prefix) / sizeof(wchar_t) + 32] = {};
    std::size_t position = 0;
    for (; position < g_prefix_size; ++position) {
        path[position] = g_prefix[position];
    }
    append_u64(path, position, static_cast<std::uint64_t>(::GetCurrentProcessId()));
    for (const wchar_t *suffix = L".txt"; *suffix != L'\0'; ++suffix) {
        path[position++] = *suffix;
    }

    const HANDLE handle = ::CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        const DWORD code = info == nullptr ? 0U : info->ExceptionRecord->ExceptionCode;
        const diag::CrashFactsSnapshot facts =
            g_facts == nullptr ? diag::CrashFactsSnapshot{} : g_facts->latest();
        diag::emit_crash_report(&emit_to_handle, &handle, facts, exception_name(code),
                                static_cast<std::uint64_t>(code),
                                static_cast<std::uint64_t>(std::time(nullptr)));
        static_cast<void>(::FlushFileBuffers(handle));
        static_cast<void>(::CloseHandle(handle));
    }
    return EXCEPTION_CONTINUE_SEARCH;
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
    const std::wstring prefix = (config.directory / L"crash-").wstring();
    if (prefix.size() + 32U > kPrefixCapacity) {
        return false;
    }
    std::wmemcpy(g_prefix, prefix.c_str(), prefix.size());
    g_prefix[prefix.size()] = L'\0';
    g_prefix_size = prefix.size();
    g_facts = config.facts;
    static_cast<void>(::SetUnhandledExceptionFilter(&on_unhandled_exception));
    return true;
}

}  // namespace borealis::platform
