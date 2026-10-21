// ============================================================
// CLI 启动参数解析（src/cli/startup.cpp）
// ------------------------------------------------------------
// `include/borealis/cli/startup.h` 的纯逻辑实现。本文件不碰文件系统与平台 API：
// 相对路径折算、目录是否存在、档案落盘与开标签都在装配层或平台腿。
// ============================================================

#include "borealis/cli/startup.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace borealis::cli {

namespace {

/// @brief ASCII 大小写折叠比较；非 ASCII 字节按原值比较（UTF-8 名称因此仍是精确匹配）。
[[nodiscard]] auto equals_folded(std::string_view lhs, std::string_view rhs) -> bool {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto left = static_cast<unsigned char>(lhs[i]);
        const auto right = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

/// @brief 参数值是否可进协议：非空、不超上限、无内嵌 NUL。
[[nodiscard]] auto valid_value(std::string_view value) -> bool {
    return !value.empty() && value.size() <= kMaxStartupValueBytes &&
           value.find('\0') == std::string_view::npos;
}

[[nodiscard]] auto parse_error(StartupParse &out, std::string message) -> StartupParse {
    out.request.reset();
    out.error = std::move(message);
    return out;
}

}  // namespace

auto startup_usage() -> std::string_view {
    return "Usage: Borealis [new-tab|split-pane] [--profile <name-or-id>] [--cwd <dir>]\n"
           "                [--new-window] [--help]\n"
           "\n"
           "  new-tab       open a tab in the current window (default)\n"
           "  split-pane    split the focused pane of the current tab\n"
           "  --profile     connect using the named profile (id or display name)\n"
           "  --cwd         starting directory for a local session\n"
           "  --new-window  open a new window instead of touching the running one\n"
           "  --help        print this text and exit\n";
}

auto parse_startup_args(std::span<const std::string> args) -> StartupParse {
    StartupParse out;
    StartupRequest request;
    bool action_seen = false;
    std::string action_spelling;
    bool profile_seen = false;
    bool cwd_seen = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];
        if (arg == "--help") {
            out.help = true;
            out.request.reset();
            out.error.clear();
            return out;
        }
        if (arg == "--new-window") {
            if (action_seen) {
                return parse_error(out, "--new-window cannot be combined with " + action_spelling);
            }
            request.new_window = true;  // 幂等：重复出现不改变语义。
            continue;
        }
        if (arg == "--profile" || arg == "--cwd") {
            const bool is_profile = arg == "--profile";
            bool &seen = is_profile ? profile_seen : cwd_seen;
            if (seen) {
                return parse_error(out, "duplicate option: " + arg);
            }
            seen = true;
            if (i + 1 >= args.size()) {
                return parse_error(out, "missing value for " + arg);
            }
            const std::string &value = args[++i];
            if (!valid_value(value)) {
                return parse_error(out, "invalid value for " + arg);
            }
            (is_profile ? request.profile : request.cwd) = value;
            continue;
        }
        if (arg == "new-tab" || arg == "split-pane") {
            if (action_seen) {
                return parse_error(out, "only one action may be specified");
            }
            if (request.new_window) {
                return parse_error(out, "--new-window cannot be combined with " + arg);
            }
            action_seen = true;
            action_spelling = arg;
            request.action = arg == "split-pane" ? StartupAction::SplitPane
                                                 : StartupAction::NewTab;
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            return parse_error(out, "unknown option: " + arg);
        }
        return parse_error(out, "unexpected argument: " + arg);
    }

    // `--new-window` 是第三个动作的写法而不是修饰符，故折进 `action`：请求与线格式各只表达一件事。
    // 旗标本身保留——旧版 CLI 转来的帧只有旗标（动作还是 `new-tab`），接收侧两种读法都要认。
    if (request.new_window) {
        request.action = StartupAction::NewWindow;
    }

    out.request = std::move(request);
    return out;
}

auto resolve_profile(const std::vector<conn::Profile> &profiles, std::string_view query)
    -> ProfileResolution {
    if (query.empty()) {
        return {.profile = nullptr, .error = "profile name is empty"};
    }

    const conn::Profile *exact_id = nullptr;
    std::size_t id_matches = 0;
    const conn::Profile *exact_name = nullptr;
    std::size_t name_matches = 0;
    const conn::Profile *folded_name = nullptr;
    std::size_t folded_matches = 0;
    for (const conn::Profile &profile : profiles) {
        if (profile.id == query) {
            exact_id = &profile;
            ++id_matches;
        }
        if (profile.name == query) {
            exact_name = &profile;
            ++name_matches;
        }
        if (equals_folded(profile.name, query)) {
            folded_name = &profile;
            ++folded_matches;
        }
    }

    // id 是稳定唯一键，优先；同名档案在不同分组里合法，故展示名只接受唯一命中。
    if (id_matches == 1) {
        return {.profile = exact_id, .error = {}};
    }
    if (id_matches > 1) {
        return {.profile = nullptr, .error = "ambiguous profile id: " + std::string{query}};
    }
    if (name_matches == 1) {
        return {.profile = exact_name, .error = {}};
    }
    if (name_matches > 1) {
        return {.profile = nullptr, .error = "ambiguous profile name: " + std::string{query}};
    }
    if (folded_matches == 1) {
        return {.profile = folded_name, .error = {}};
    }
    if (folded_matches > 1) {
        return {.profile = nullptr, .error = "ambiguous profile name: " + std::string{query}};
    }
    return {.profile = nullptr, .error = "profile not found: " + std::string{query}};
}

auto startup_policy_error(const StartupRequest &request,
                          std::optional<conn::ConnectionType> profile_type) -> std::string {
    if (request.action == StartupAction::SplitPane && !request.profile.empty()) {
        // 类型未知按「非本地」处理：调用方没解析就递进来，这一支给不出可靠判断，宁可拒错也不让新格
        // 在同步工厂里等一份弹框材料。
        const bool profile_is_local =
            profile_type.has_value() && profile_type.value() == conn::ConnectionType::Local;
        if (!profile_is_local) {
            return "split-pane --profile needs a per-pane credential prompt";
        }
    }
    if (!request.cwd.empty() && profile_type.has_value() &&
        profile_type.value() != conn::ConnectionType::Local) {
        return "--cwd is only supported for local profiles";
    }
    return {};
}

}  // namespace borealis::cli
