#pragma once

// ============================================================
// 测试公共设施（tests/support/fixture_text.h）—— 转义序列夹具的还原
// ------------------------------------------------------------
// 夹具文件里的控制字节写成 `\e` `\xNN` 这类可读形式（二进制转义序列写进文本夹具会
// 被编辑器与 diff 吞掉），回放前须还原成原始字节。集成与 E2E 用例共用同一份还原口径，
// 避免两处写法漂移。
// ============================================================

#include <fstream>
#include <string>
#include <string_view>

namespace borealis::testing::fixtures {

/// @brief 夹具转义还原：`\e` `\a` `\n` `\r` `\t` `\\` `\xNN`。
/// @param text 夹具文本（全 ASCII）。
/// @return 原始字节。
[[nodiscard]] inline auto unescape(std::string_view text) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        const char esc = text[i + 1];
        ++i;
        switch (esc) {
            case 'e':
                out += '\x1B';
                break;
            case 'a':
                out += '\x07';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case '\\':
                out += '\\';
                break;
            case 'x': {
                out += static_cast<char>(std::stoul(std::string{text.substr(i + 1, 2)}, nullptr, 16));
                i += 2;
                break;
            }
            default:
                out += esc;
                break;
        }
    }
    return out;
}

/// @brief 读夹具首行并还原转义。
/// @param absolute_path 夹具绝对路径。
/// @return 原始字节；文件不可读时返回空串（由用例自行断言）。
[[nodiscard]] inline auto read_first_line(std::string_view absolute_path) -> std::string {
    std::ifstream in{std::string{absolute_path}};
    std::string line;
    if (!in.is_open() || !std::getline(in, line)) {
        return {};
    }
    return unescape(line);
}

}  // namespace borealis::testing::fixtures
