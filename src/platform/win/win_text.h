#pragma once

// ============================================================
// Win32 文本转换（src/platform/win/win_text.h）
// ------------------------------------------------------------
// 平台层私有工具：公共头一律 UTF-8（裁决 7.11 把宽字符与码页 API 挡在 src/platform/ 内），
// 进出 Win32 API 时在此转换。
// ============================================================

#include <string>
#include <string_view>

namespace borealis::platform {

/// @brief UTF-8 转 UTF-16。
/// @param utf8 UTF-8 文本。
/// @return 宽文本；输入含非法 UTF-8 序列时该段被丢弃，转换失败时为空串。
[[nodiscard]] auto to_wide(std::string_view utf8) -> std::wstring;

/// @brief UTF-16 转 UTF-8。
/// @param wide 宽文本。
/// @return UTF-8 文本；转换失败时为空串。
[[nodiscard]] auto to_utf8(std::wstring_view wide) -> std::string;

}  // namespace borealis::platform
