// ============================================================
// SSH 跳板链纯逻辑件实现（src/conn/proxy_jump.cpp）
// ------------------------------------------------------------
// 判据全部收在本文件：语法见 conn/proxy_jump.h 的条款，证人见
// tests/unit/utest_proxy_jump.cpp。设计稿 codespec/CONN03_PROXYJUMP.draft.md D3。
// ============================================================

#include "proxy_jump.h"

#include <charconv>
#include <string>

namespace borealis::conn {

namespace {

/// @brief token 里允许出现的字符（主机名／IPv6／用户名的并集）。
///
/// 白名单而非黑名单：跳名会进日志与档案串形，控制字符与非 ASCII 一律当非法，
/// 顺带堵住「把换行粘进主机名」这类日志注入。
[[nodiscard]] auto allowed_char(char c) -> bool {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '-' || c == '_' || c == ':' || c == '[' || c == ']';
}

/// @brief 一段文本是否整段落在白名单里（主机名／IPv6／用户名共用）。
[[nodiscard]] auto allowed_text(std::string_view text) -> bool {
    for (const char c : text) {
        if (!allowed_char(c)) {
            return false;
        }
    }
    return true;
}

/// @brief 端口文本 → 数值（纯数字、1..65535；前导 '+'/'-'／空串／溢出都算非法）。
[[nodiscard]] auto parse_port(std::string_view text) -> std::optional<int> {
    if (text.empty() || text.size() > 5U) {
        return std::nullopt;
    }
    int value{};
    const auto *begin = text.data();
    const auto *end = begin + text.size();
    const auto [next, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || next != end || value < 1 || value > 65535) {
        return std::nullopt;
    }
    return value;
}

/// @brief 去掉两端空白（只认 ASCII 空白：串形里出现制表符同样该被拒）。
[[nodiscard]] auto trim(std::string_view text) -> std::string_view {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
                           text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

/// @brief `[user@]host[:port]` 的单枚 token；`%` 开头的宏与任何白名单外的字符即非法。
[[nodiscard]] auto parse_token(std::string_view token) -> std::optional<SshHop> {
    SshHop hop;
    hop.auth_method = "agent";

    std::string_view rest = token;
    if (const auto at = rest.find('@'); at != std::string_view::npos) {
        hop.user = std::string{rest.substr(0, at)};
        rest = rest.substr(at + 1);
        // 用户之后不许再有 '@'：`a@b@c` 不是任何一种 OpenSSH 写法。
        if (hop.user.empty() || rest.find('@') != std::string_view::npos) {
            return std::nullopt;
        }
    }

    std::string_view host = rest;
    std::string_view port_text;
    bool has_port = false;
    if (!rest.empty() && rest.front() == '[') {
        // IPv6：`[::1]` 或 `[::1]:22`——']' 之后只能结束或跟 ':端口'。
        const auto close = rest.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        host = rest.substr(1, close - 1);
        if (const auto tail = rest.substr(close + 1); !tail.empty()) {
            if (tail.front() != ':' || tail.size() < 2U) {
                return std::nullopt;
            }
            port_text = tail.substr(1);
            has_port = true;
        }
    } else {
        const auto colon = rest.find(':');
        if (colon != std::string_view::npos) {
            // 未加方括号就不许有两个 ':'：那是没写方括号的 IPv6，OpenSSH 也不认。
            if (rest.find(':', colon + 1) != std::string_view::npos) {
                return std::nullopt;
            }
            host = rest.substr(0, colon);
            port_text = rest.substr(colon + 1);
            has_port = true;
        }
    }

    if (host.empty() || !allowed_text(host) || !allowed_text(hop.user)) {
        return std::nullopt;
    }
    hop.port = has_port ? parse_port(port_text).value_or(0) : 22;
    if (hop.port == 0) {
        return std::nullopt;
    }
    hop.host = std::string{host};
    return hop;
}

/// @brief 主机在串形里的写法：含 ':' 就补方括号（IPv6 的裸写在下一轮解析时会被拒）。
[[nodiscard]] auto bracket_host(const std::string &host) -> std::string {
    if (host.find(':') == std::string::npos) {
        return host;
    }
    return "[" + host + "]";
}

/// @brief 结构化跳的逐跳合法性（与 parse_token 同判据，只是输入已是字段）。
[[nodiscard]] auto hop_is_well_formed(const SshHop &hop) -> bool {
    return !hop.host.empty() && hop.port >= 1 && hop.port <= 65535 && allowed_text(hop.host) &&
           allowed_text(hop.user);
}

}  // namespace

auto parse_proxy_jump_list(std::string_view text, KnownHostsPolicy default_policy)
    -> std::optional<std::vector<SshHop>> {
    const auto body = trim(text);
    if (body.empty()) {
        return std::vector<SshHop>{};
    }
    // `none` 是库与 OpenSSH 共用的「显式关闭」写法（大小写不敏感，同 strcasecmp 口径）。
    if (body.size() == 4U) {
        std::string lowered;
        for (const char c : body) {
            lowered.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
        }
        if (lowered == "none") {
            return std::vector<SshHop>{};
        }
    }

    std::vector<SshHop> hops;
    std::string_view rest = body;
    while (true) {
        const auto comma = rest.find(',');
        const auto token = trim(rest.substr(0, comma));
        auto hop = parse_token(token);
        if (!hop.has_value()) {
            return std::nullopt;  // 任一枚坏 token 即整串拒绝（头注释那条口径）。
        }
        hop->known_hosts_policy = default_policy;
        hops.push_back(std::move(*hop));
        if (comma == std::string_view::npos) {
            break;
        }
        rest = rest.substr(comma + 1);
    }

    if (hops.size() > kMaxProxyHops) {
        return std::nullopt;
    }
    return hops;
}

auto render_proxy_jump_list(const std::vector<SshHop> &hops) -> std::string {
    std::string out;
    for (const auto &hop : hops) {
        if (!out.empty()) {
            out += ',';
        }
        if (!hop.user.empty()) {
            out += hop.user;
            out += '@';
        }
        out += bracket_host(hop.host);
        out += ':';
        out += std::to_string(hop.port);
    }
    return out;
}

auto validate_proxy_hops(const std::vector<SshHop> &hops) -> bool {
    if (hops.size() > kMaxProxyHops) {
        return false;
    }
    for (const auto &hop : hops) {
        if (!hop_is_well_formed(hop)) {
            return false;
        }
    }
    return true;
}

}  // namespace borealis::conn
