// ============================================================
// OSC 7 本机目录解析与继承取舍（src/ui/osc7.cpp）
// ------------------------------------------------------------
// 全部是字符串折算：先按 URI 规则拆 scheme / host / path，再严格百分号解码，最后按目标平台的
// 绝对路径形态收口。没有任何文件系统调用，故每条拒绝档都能脱开界面与会话进单测
//（AGENTS §4.4 第 20 条）。
// ============================================================

#include "borealis/ui/osc7.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "borealis/term/utf8.h"

namespace borealis::ui {
namespace {

constexpr std::string_view kFileScheme{"file"};
constexpr std::string_view kSchemeSeparator{"://"};
constexpr std::string_view kLocalhost{"localhost"};

[[nodiscard]] auto to_ascii_lower(char byte) noexcept -> char {
    return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + ('a' - 'A')) : byte;
}

[[nodiscard]] auto is_ascii_letter(char byte) noexcept -> bool {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

[[nodiscard]] auto ascii_iequals(std::string_view left, std::string_view right) noexcept -> bool {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (to_ascii_lower(left[index]) != to_ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

/// @brief 十六进制数位；非数位给 -1。
[[nodiscard]] auto hex_digit(char byte) noexcept -> int {
    if (byte >= '0' && byte <= '9') {
        return byte - '0';
    }
    if (byte >= 'a' && byte <= 'f') {
        return byte - 'a' + 10;
    }
    if (byte >= 'A' && byte <= 'F') {
        return byte - 'A' + 10;
    }
    return -1;
}

/// @brief 严格百分号解码：`%` 后必须是两位十六进制，否则整条不认（不跳过、不猜）。
[[nodiscard]] auto percent_decode(std::string_view encoded, std::string &out) -> bool {
    out.clear();
    out.reserve(encoded.size());
    for (std::size_t index = 0U; index < encoded.size(); ++index) {
        const char byte = encoded[index];
        if (byte != '%') {
            out.push_back(byte);
            continue;
        }
        if (index + 2U >= encoded.size()) {
            return false;
        }
        const int high = hex_digit(encoded[index + 1U]);
        const int low = hex_digit(encoded[index + 2U]);
        if (high < 0 || low < 0) {
            return false;
        }
        out.push_back(static_cast<char>((high << 4) | low));
        index += 2U;
    }
    return true;
}

/// @brief path 是否落在 `file://` 之下（scheme 大小写不敏感，分隔符须逐字一致）。
[[nodiscard]] auto has_file_scheme(std::string_view uri) noexcept -> bool {
    if (uri.size() < kFileScheme.size() + kSchemeSeparator.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < kFileScheme.size(); ++index) {
        if (to_ascii_lower(uri[index]) != kFileScheme[index]) {
            return false;
        }
    }
    return uri.substr(kFileScheme.size(), kSchemeSeparator.size()) == kSchemeSeparator;
}

/// @brief Windows 的 URI 路径 `/C:/x` 折成 `C:/x`；盘符、冒号与冒号后的分隔符缺一不可。
///
/// 反斜杠一并收：某些 shell 直接报 `%5C` 分隔的路径，形态仍是同一台机器的绝对路径。
[[nodiscard]] auto windows_path(std::string_view decoded) -> std::optional<std::string> {
    if (decoded.size() < 4U || decoded.front() != '/' || !is_ascii_letter(decoded[1]) ||
        decoded[2] != ':' || (decoded[3] != '/' && decoded[3] != '\\')) {
        return std::nullopt;
    }
    return std::string{decoded.substr(1U)};
}

}  // namespace

auto osc7_path_style() -> Osc7PathStyle {
#if defined(_WIN32)
    return Osc7PathStyle::Windows;
#else
    return Osc7PathStyle::Posix;
#endif
}

auto local_directory_from_osc7(const std::u32string_view raw, const Osc7PathStyle style)
    -> std::optional<std::string> {
    if (raw.empty()) {
        return std::nullopt;
    }
    std::string uri;
    if (term::encode_utf8(raw, uri) != 0U) {
        return std::nullopt;  // 码点不可表示：原文已不是可信的 URI
    }
    if (!has_file_scheme(uri)) {
        return std::nullopt;
    }
    const auto rest = std::string_view{uri}.substr(kFileScheme.size() + kSchemeSeparator.size());
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;  // `file://host` 没有路径：报的是「哪台机器」而不是目录
    }
    const auto host = rest.substr(0U, slash);
    if (!host.empty() && !ascii_iequals(host, kLocalhost)) {
        return std::nullopt;  // 远端 host（含嵌套 ssh 报上来的机器名）不猜成本机目录
    }
    // 问号与井号在解码前截断：`%3F` / `%23` 是文件名的一部分，不能跟着被切掉。
    auto path = rest.substr(slash);
    if (const auto suffix = path.find_first_of("?#"); suffix != std::string_view::npos) {
        path = path.substr(0U, suffix);
    }

    std::string decoded;
    if (!percent_decode(path, decoded)) {
        return std::nullopt;
    }
    if (decoded.empty() || decoded.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    if (style == Osc7PathStyle::Windows) {
        return windows_path(decoded);
    }
    // POSIX：URI 路径本就从 `/` 起，故形态天然是绝对路径；`file:///` 收成根目录。
    return decoded;
}

auto choose_start_directory(const bool local_session, const std::u32string_view osc7,
                            const std::string_view configured, const Osc7PathStyle style)
    -> StartDirectory {
    if (local_session) {
        if (auto inherited = local_directory_from_osc7(osc7, style)) {
            return StartDirectory{.path = std::move(*inherited), .inherited = true};
        }
    }
    return StartDirectory{.path = std::string{configured}, .inherited = false};
}

}  // namespace borealis::ui
