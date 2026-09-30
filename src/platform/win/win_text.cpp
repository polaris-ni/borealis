// ============================================================
// Win32 文本转换实现（src/platform/win/win_text.cpp）
// ============================================================

#include "win_text.h"

#include <windows.h>

namespace borealis::platform {

auto to_wide(std::string_view utf8) -> std::wstring {
    if (utf8.empty()) {
        return {};
    }
    const auto count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                           nullptr, 0);
    if (count <= 0) {
        return {};
    }
    auto wide = std::wstring(static_cast<std::size_t>(count), L'\0');
    static_cast<void>(MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                          wide.data(), count));
    return wide;
}

auto to_utf8(std::wstring_view wide) -> std::string {
    if (wide.empty()) {
        return {};
    }
    const auto count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    auto utf8 = std::string(static_cast<std::size_t>(count), '\0');
    static_cast<void>(WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                          utf8.data(), count, nullptr, nullptr));
    return utf8;
}

}  // namespace borealis::platform
