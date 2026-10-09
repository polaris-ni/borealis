// ============================================================
// 连接管理器纯逻辑视图模型实现（src/conn/profile_views.cpp）
// ------------------------------------------------------------
// 排序/过滤/校验的确定性都在这里锁死（utest_profile_views）；UI 只画算好的行。
// ============================================================

#include "profile_views.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <utility>

namespace borealis::conn {

namespace {

[[nodiscard]] auto by_name(const SidebarRow &a, const SidebarRow &b) -> bool {
    return a.name < b.name;
}

[[nodiscard]] auto row_of(const Profile &profile) -> SidebarRow {
    auto row = SidebarRow{};
    row.id = profile.id;
    row.name = profile.name;
    row.type = profile.type;
    row.favorite = profile.favorite;
    if (!profile.groups.empty()) {
        row.group_label = profile.groups.front();
    }
    return row;
}

}  // namespace

auto sidebar_rows(const ProfileStore &store, SidebarViewMode mode, std::string_view query)
    -> std::vector<SidebarRow> {
    auto rows = std::vector<SidebarRow>{};

    // 搜索词非空走模型层匹配（大小写不敏感子串）；否则按视图取全集/收藏。
    if (!query.empty()) {
        for (const Profile *profile : store.search(query)) {
            rows.push_back(row_of(*profile));
        }
    } else if (mode == SidebarViewMode::Favorites) {
        for (const Profile *profile : store.favorites()) {
            rows.push_back(row_of(*profile));
        }
    } else {
        for (const Profile &profile : store.profiles()) {
            rows.push_back(row_of(profile));
        }
    }

    if (mode == SidebarViewMode::Groups) {
        // 组名字典序 → 名字字典序；无分组（空串）殿后——空串按字节序最小，须显式排最后。
        const auto group_rank = [](const SidebarRow &row) -> std::size_t {
            return row.group_label.empty() ? 1U : 0U;
        };
        std::ranges::sort(rows, [&group_rank](const SidebarRow &a, const SidebarRow &b) {
            if (group_rank(a) != group_rank(b)) {
                return group_rank(a) < group_rank(b);
            }
            if (a.group_label != b.group_label) {
                return a.group_label < b.group_label;
            }
            return a.name < b.name;
        });
        return rows;
    }

    std::ranges::sort(rows, by_name);
    return rows;
}

auto validate_draft(const ProfileDraft &draft) -> DraftIssue {
    if (draft.name.empty()) {
        return DraftIssue::EmptyName;
    }
    if (draft.type == ConnectionType::Ssh) {
        if (draft.host.empty()) {
            return DraftIssue::EmptyHost;
        }
        if (draft.port < 1 || draft.port > 65535) {
            return DraftIssue::PortOutOfRange;
        }
        const std::string_view method{draft.auth_method};
        if (method != "password" && method != "privatekey" && method != "agent" &&
            method != "keyboard-interactive") {
            return DraftIssue::UnknownAuthMethod;
        }
    }
    return DraftIssue::None;
}

auto draft_to_profile(const ProfileDraft &draft, std::string id) -> Profile {
    auto profile = Profile{};
    profile.id = std::move(id);
    profile.name = draft.name;
    profile.type = draft.type;
    if (draft.type == ConnectionType::Local) {
        profile.local.command_line = draft.command_line;
        profile.local.working_directory = draft.working_directory;
    } else {
        profile.ssh.host = draft.host;
        profile.ssh.port = draft.port;
        profile.ssh.user = draft.user;
        profile.ssh.auth_method = draft.auth_method;
        profile.ssh.identity_file = draft.identity_file;
        profile.ssh.agent_forwarding = draft.agent_forwarding;
        profile.ssh.known_hosts_policy = draft.known_hosts_policy;
    }
    return profile;
}

auto profile_to_draft(const Profile &profile) -> ProfileDraft {
    auto draft = ProfileDraft{};
    draft.name = profile.name;
    draft.type = profile.type;
    if (profile.type == ConnectionType::Local) {
        draft.command_line = profile.local.command_line;
        draft.working_directory = profile.local.working_directory;
    } else {
        draft.host = profile.ssh.host;
        draft.port = profile.ssh.port;
        draft.user = profile.ssh.user;
        draft.auth_method = profile.ssh.auth_method;
        draft.identity_file = profile.ssh.identity_file;
        draft.agent_forwarding = profile.ssh.agent_forwarding;
        draft.known_hosts_policy = profile.ssh.known_hosts_policy;
    }
    return draft;
}

auto secret_ask_for(std::string_view auth_method) -> SecretAsk {
    if (auth_method == "password") {
        return SecretAsk::Password;
    }
    if (auth_method == "privatekey") {
        return SecretAsk::Passphrase;
    }
    if (auth_method == "keyboard-interactive") {
        return SecretAsk::Interactive;
    }
    return SecretAsk::None;  // agent（含未识别值归一后的回落档）。
}

auto parse_quick_connect(std::string_view text) -> std::optional<std::pair<std::string, int>> {
    // 剥两端空白。
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos) {
        return std::make_pair(std::string{text}, 22);
    }
    const auto port_text = text.substr(colon + 1);
    if (port_text.empty()) {
        return std::make_pair(std::string{text.substr(0, colon)}, 22);
    }
    // 端口段须全数字才当端口（不猜：`host:名称` 是合法的主机名写法的一部分）。
    if (!std::all_of(port_text.begin(), port_text.end(),
                     [](char c) { return c >= '0' && c <= '9'; })) {
        return std::make_pair(std::string{text}, 22);
    }
    const auto port = std::strtol(std::string{port_text}.c_str(), nullptr, 10);  // NOLINT
    if (port < 1 || port > 65535) {
        // 越界数字端口：整段按主机名收，绝不拆掉端口连错主机。
        return std::make_pair(std::string{text}, 22);
    }
    return std::make_pair(std::string{text.substr(0, colon)}, static_cast<int>(port));
}

auto quick_connect_profile(std::string host, int port) -> Profile {
    auto profile = Profile{};
    profile.id = "quick:" + host + ":" + std::to_string(port);
    profile.name = host + ":" + std::to_string(port);
    profile.type = ConnectionType::Ssh;
    profile.ssh.host = std::move(host);
    profile.ssh.port = port;
    profile.ssh.auth_method = "agent";  // 临时通道缺省 agent：无档案即无凭据可引。
    return profile;
}

}  // namespace borealis::conn
