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

#include "proxy_jump.h"      // parse/render_proxy_jump_list：向导那一行的唯一解析点
#include "serial_model.h"  // is_supported_baud：向导的速率判据取那一份唯一档位表
#include "ssh_dial.h"      // normalized_auth_method：与拨号腿同一份认证方式归一

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
        // 跳板链那一行坏值就禁止保存（稿 D9）：**部分接受等于替用户造了一条他不知道的直连**，
        // 与 D6 那条「丢了跳板就等于绕过它」同一条理由，故整串拒绝而不是留好跳。
        if (!parse_proxy_jump_list(draft.proxy_jumps_text).has_value()) {
            return DraftIssue::BadProxyJump;
        }
    }
    if (draft.type == ConnectionType::Telnet) {
        // Telnet 型**零新增问题码**（裁决 7.112 D2①）：主机必填与端口区间都复用 SSH 那两档，
        // 向导的字段集只有三行（展示名/主机/端口），没有认证字段可校验。
        if (draft.host.empty()) {
            return DraftIssue::EmptyHost;
        }
        if (draft.port < 1 || draft.port > 65535) {
            return DraftIssue::PortOutOfRange;
        }
    }
    if (draft.type == ConnectionType::Serial) {
        // 串口四档（稿 §2 标号 4）：判据来自 `conn/serial_model.h` 的那一份档位表与取值区间，
        // 不在这里现编数字。
        if (draft.serial.device_path.empty()) {
            return DraftIssue::EmptyDevice;
        }
        if (!is_supported_baud(draft.serial.baud)) {
            return DraftIssue::BaudNotInTable;
        }
        if (draft.serial.data_bits < 5 || draft.serial.data_bits > 8) {
            return DraftIssue::DataBitsOutOfRange;
        }
        if (draft.serial.stop_bits < 1 || draft.serial.stop_bits > 2) {
            return DraftIssue::StopBitsOutOfRange;
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
    } else if (draft.type == ConnectionType::Serial) {
        profile.serial = draft.serial;
    } else if (draft.type == ConnectionType::Telnet) {
        profile.telnet.host = draft.host;
        profile.telnet.port = draft.port;
    } else {
        profile.ssh.host = draft.host;
        profile.ssh.port = draft.port;
        profile.ssh.user = draft.user;
        profile.ssh.auth_method = draft.auth_method;
        profile.ssh.identity_file = draft.identity_file;
        profile.ssh.agent_forwarding = draft.agent_forwarding;
        profile.ssh.known_hosts_policy = draft.known_hosts_policy;
        // 串形 → 跳链：只在校验通过后到这里（`validate_draft` 已保证解析得出来）。解不出来是
        // 调用方漏了校验，那种情况下**留空链路**——它比半截链路更接近事实，但要紧的是本仓唯一
        // 的调用点 `ConnectionWizard::save()` 先校验、不过校验就不落档案。
        if (const auto hops = parse_proxy_jump_list(draft.proxy_jumps_text)) {
            profile.ssh.proxy_jumps = *hops;
        }
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
    } else if (profile.type == ConnectionType::Serial) {
        draft.serial = profile.serial;
    } else if (profile.type == ConnectionType::Telnet) {
        draft.host = profile.telnet.host;
        draft.port = profile.telnet.port;
    } else {
        draft.host = profile.ssh.host;
        draft.port = profile.ssh.port;
        draft.user = profile.ssh.user;
        draft.auth_method = profile.ssh.auth_method;
        draft.identity_file = profile.ssh.identity_file;
        draft.agent_forwarding = profile.ssh.agent_forwarding;
        draft.known_hosts_policy = profile.ssh.known_hosts_policy;
        // 回填走规范化串形（总是带端口）：编辑一次不该把 `:22` 从档案里「改没」。
        draft.proxy_jumps_text = render_proxy_jump_list(profile.ssh.proxy_jumps);
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

auto plan_credential(const SshProfile &ssh, const CredentialStore &store) -> CredentialPlan {
    const auto ask = secret_ask_for(normalized_auth_method(ssh));
    if (ask == SecretAsk::None) {
        return CredentialPlan{.route = CredentialRoute::None, .ask = SecretAsk::None};
    }
    const bool usable_reference = ssh.credential.kind == SecretKind::Reference &&
                                  !ssh.credential.vault_id.empty() && store.available();
    return CredentialPlan{
        .route = usable_reference ? CredentialRoute::Stored : CredentialRoute::Ask,
        .ask = ask,
        .handle = ssh.credential,
    };
}

auto hop_profile_of(const SshHop &hop) -> SshProfile {
    auto profile = SshProfile{};
    profile.host = hop.host;
    profile.port = hop.port;
    profile.user = hop.user;
    profile.auth_method = hop.auth_method;
    profile.identity_file = hop.identity_file;
    profile.agent_forwarding = false;
    profile.known_hosts_policy = hop.known_hosts_policy;
    profile.credential = hop.credential;
    return profile;
}

auto plan_hop_credential(const SshHop &hop, const CredentialStore &store) -> CredentialPlan {
    return plan_credential(hop_profile_of(hop), store);
}

namespace {

/// @brief 键位一段的转义：`%` 与 `#` 是本层唯一的两种分隔符（单趟扫描，故 `%23` 不会被
///        二次转义）。规范化跳形本身的白名单不含这两枚字符，转义主要防的是手改档案 id。
[[nodiscard]] auto escape_vault_segment(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '%') {
            out += "%25";
        } else if (c == '#') {
            out += "%23";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

auto hop_vault_key(std::string_view profile_id, const SshHop &hop) -> std::string {
    if (profile_id.empty() || profile_id.find('\0') != std::string_view::npos) {
        return {};
    }
    // 坏跳不生成键位：库里的条目必须能反推回一个合法身份，否则下次连接对不上。
    if (!validate_proxy_hops({hop})) {
        return {};
    }
    auto token = render_proxy_jump_list({hop});
    if (hop.user.empty()) {
        token.insert(token.begin(), '@');
    }
    return escape_vault_segment(profile_id) + "#" + escape_vault_segment(token);
}

auto plan_hop_vault_keys(const SshProfile &ssh, const CredentialStore &store,
                         std::string_view profile_id) -> std::vector<std::string> {
    std::vector<std::string> keys(ssh.proxy_jumps.size());
    if (profile_id.empty()) {
        return keys;
    }
    for (std::size_t index = 0; index < ssh.proxy_jumps.size(); ++index) {
        if (plan_hop_credential(ssh.proxy_jumps[index], store).route == CredentialRoute::Ask) {
            keys[index] = hop_vault_key(profile_id, ssh.proxy_jumps[index]);
        }
    }
    return keys;
}

auto plan_hop_writeback(const Profile &profile, const CredentialStore &store,
                        const std::vector<std::pair<std::size_t, std::string>> &answered,
                        const std::vector<std::optional<std::string>> &hop_secrets)
    -> std::vector<HopWriteback> {
    auto out = std::vector<HopWriteback>{};
    if (profile.type != ConnectionType::Ssh) {
        return out;
    }
    for (const auto &[index, key] : answered) {
        if (key.empty() || index >= profile.ssh.proxy_jumps.size() ||
            index >= hop_secrets.size() || !hop_secrets[index].has_value()) {
            continue;
        }
        const auto &hop = profile.ssh.proxy_jumps[index];
        // 询问期间链可能被改（下标错位）：按当前跳重算键位，对不上就不写。
        if (hop_vault_key(profile.id, hop) != key) {
            continue;
        }
        // 已经拿到可用句柄的跳不再覆写：答话来自旧计划，库里的那条才是用户的现状。
        if (plan_hop_credential(hop, store).route != CredentialRoute::Ask) {
            continue;
        }
        out.push_back(HopWriteback{.index = index,
                                   .key = key,
                                   .label = profile.name + " / " + render_proxy_jump_list({hop}),
                                   .secret = *hop_secrets[index]});
    }
    return out;
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

auto quick_connect_profile(std::string host, int port, std::vector<SshHop> jumps) -> Profile {
    auto profile = Profile{};
    profile.id = "quick:" + host + ":" + std::to_string(port);
    profile.name = host + ":" + std::to_string(port);
    profile.type = ConnectionType::Ssh;
    profile.ssh.host = std::move(host);
    profile.ssh.port = port;
    profile.ssh.auth_method = "agent";  // 临时通道缺省 agent：无档案即无凭据可引。
    // 跳表原样进档案（批 7 稿 D5）：解析侧折出的跳一律 agent 认证，所以 quick connect 全程
    // 不会触发逐跳询问——口令跳板走档案/向导那条路，不是这条临时通道的事。
    profile.ssh.proxy_jumps = std::move(jumps);
    return profile;
}

namespace {

/// @brief 从两端剥空白与制表（与 `parse_quick_connect` 同口径；不碰中间字符）。
[[nodiscard]] auto trim_edges(std::string_view text) -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

auto parse_quick_connect_line(std::string_view text)
    -> std::pair<std::optional<QuickConnect>, QuickJumpIssue> {
    // 找最后一个空白分隔的 `-J`／`--jump` 裸 token（大小写敏感——「不猜」）。逐段扫而不是
    // 子串搜：子串会误中 `my-Jhost` 这类合法主机名片段。
    std::size_t marker_pos = std::string_view::npos;
    std::size_t marker_end = 0;
    for (std::size_t cursor = 0; cursor < text.size();) {
        while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t')) {
            ++cursor;
        }
        const std::size_t start = cursor;
        while (cursor < text.size() && text[cursor] != ' ' && text[cursor] != '\t') {
            ++cursor;
        }
        if (start == cursor) {
            break;
        }
        const auto token = text.substr(start, cursor - start);
        if (token == "-J" || token == "--jump") {
            marker_pos = start;
            marker_end = cursor;
        }
    }

    if (marker_pos == std::string_view::npos) {
        // 没有 `-J` 段：整行交既有解析，宽容口径逐帧继承（旧行为回归网就钉在这）。
        const auto parsed = parse_quick_connect(text);
        if (!parsed.has_value()) {
            return {std::nullopt, QuickJumpIssue::None};
        }
        return {QuickConnect{parsed->first, parsed->second, {}}, QuickJumpIssue::None};
    }

    const auto jump_text = trim_edges(text.substr(marker_end));
    // 跳表段整交语法层唯一主人；`host -J`（段为空）按语法层折成空链路＝直连，与 none 同义。
    const auto jumps = parse_proxy_jump_list(jump_text);
    if (!jumps.has_value()) {
        return {std::nullopt, QuickJumpIssue::BadJumpList};
    }
    const auto parsed = parse_quick_connect(text.substr(0, marker_pos));
    if (!parsed.has_value()) {
        return {std::nullopt, QuickJumpIssue::None};  // 空主机沿用静默档（坏跳表已在上面先拒）。
    }
    return {QuickConnect{parsed->first, parsed->second, *jumps}, QuickJumpIssue::None};
}

auto hop_secrets_silent(const SshProfile &ssh, const CredentialStore &store) -> bool {
    for (const auto &hop : ssh.proxy_jumps) {
        if (plan_hop_credential(hop, store).route == CredentialRoute::Ask) {
            return false;
        }
    }
    return true;
}

}  // namespace borealis::conn
