// ============================================================
// 连接档案与凭据模型的纯逻辑实现（src/conn/profile.cpp）
// ------------------------------------------------------------
// `include/borealis/conn/profile.h` 的实现：凭据库替身、`ProfileStore` 查询侧，
// 以及 `~/.ssh/config` 的只读解析。全件只含标准库，不碰 Aurora 与平台 API
// （与头同口径：纯逻辑、可无头单测，AGENTS.md §4.4 第 20 条）。
//
// OS 凭据库的真实后端（libsecret / Keychain / CredMan）不在本切片；`store`/`retrieve`
// 的真实落盘由接 `CredentialStore` 的后端负责，档案模型与其无关。
// ============================================================

#include "borealis/conn/profile.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace borealis::conn {
namespace {

/// @brief 转小写（搜索与关键字匹配用）。
[[nodiscard]] auto to_lower(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

/// @brief 去首尾空白。
[[nodiscard]] auto trim(std::string_view text) -> std::string_view {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

/// @brief 截掉行内注释（`#` 起至行尾）。
[[nodiscard]] auto strip_comment(std::string_view line) -> std::string_view {
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '#') {
            return line.substr(0, index);
        }
    }
    return line;
}

/// @brief 去引号（配置值常见单/双引号包裹）。
[[nodiscard]] auto unquote(std::string_view value) -> std::string {
    const auto t = trim(value);
    if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front()) {
        return std::string(t.substr(1, t.size() - 2));
    }
    return std::string(t);
}

}  // namespace

// ------------------------------------------------------------
// 凭据库替身
// ------------------------------------------------------------

auto InMemoryCredentialStore::store(std::string_view /*profile_id*/, std::string_view /*label*/,
                                    std::string secret) -> SecretHandle {
    const std::string id = "mem:" + std::to_string(++seq_);
    vault_[id] = std::move(secret);
    return SecretHandle::reference(id);
}

auto InMemoryCredentialStore::retrieve(const SecretHandle &handle) -> std::optional<std::string> {
    if (handle.kind != SecretKind::Reference) {
        return std::nullopt;
    }
    const auto it = vault_.find(handle.vault_id);
    if (it == vault_.end()) {
        return std::nullopt;
    }
    return it->second;
}

// ------------------------------------------------------------
// ProfileStore：查询侧
// ------------------------------------------------------------

[[nodiscard]] auto ProfileStore::find(std::string_view id) const -> const Profile * {
    for (const auto &profile : items_) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

[[nodiscard]] auto ProfileStore::by_group(std::string_view group) const -> std::vector<const Profile *> {
    std::vector<const Profile *> out;
    for (const auto &profile : items_) {
        if (std::ranges::find(profile.groups, std::string{group}) != profile.groups.end()) {
            out.emplace_back(&profile);
        }
    }
    return out;
}

[[nodiscard]] auto ProfileStore::favorites() const -> std::vector<const Profile *> {
    std::vector<const Profile *> out;
    for (const auto &profile : items_) {
        if (profile.favorite) {
            out.emplace_back(&profile);
        }
    }
    return out;
}

[[nodiscard]] auto ProfileStore::search(std::string_view query) const -> std::vector<const Profile *> {
    const auto needle = to_lower(query);
    std::vector<const Profile *> out;
    if (needle.empty()) {
        return out;
    }
    for (const auto &profile : items_) {
        const bool hit_name = to_lower(profile.name).find(needle) != std::string::npos;
        const bool hit_host = (profile.type == ConnectionType::Ssh) &&
                              (to_lower(profile.ssh.host).find(needle) != std::string::npos);
        bool hit_tag = false;
        for (const auto &tag : profile.tags) {
            if (to_lower(tag).find(needle) != std::string::npos) {
                hit_tag = true;
                break;
            }
        }
        if (hit_name || hit_host || hit_tag) {
            out.emplace_back(&profile);
        }
    }
    return out;
}

auto ProfileStore::add(Profile profile) -> bool {
    if (find(profile.id) != nullptr) {
        return false;
    }
    items_.push_back(std::move(profile));
    return true;
}

auto ProfileStore::remove(std::string_view id) -> bool {
    const auto it = std::ranges::find_if(items_, [id](const Profile &profile) { return profile.id == id; });
    if (it == items_.end()) {
        return false;
    }
    items_.erase(it);
    return true;
}

auto ProfileStore::update(Profile profile) -> bool {
    const auto it = std::ranges::find_if(items_, [&profile](const Profile &p) { return p.id == profile.id; });
    if (it == items_.end()) {
        return false;
    }
    *it = std::move(profile);
    return true;
}

// ------------------------------------------------------------
// ~/.ssh/config 只读导入（CONN.03）
// ------------------------------------------------------------

[[nodiscard]] auto parse_ssh_config(std::string_view content) -> std::vector<Profile> {
    std::vector<Profile> out;

    // 一个 Host 块的累积参数；`Match`/全局段的参数不导入（只对具体 Host 别名建档案）。
    struct Block {
        std::vector<std::string> patterns;
        std::string host_name;
        std::string user;
        int port{0};
        std::string identity;
        bool forward_agent{false};
        KnownHostsPolicy policy{KnownHostsPolicy::AcceptNew};
    };

    // 把当前块按每个具体 pattern 折成一条 SSH 档案；含通配符（`*`/`?`）的模板不导入。
    const auto emit = [&out](const Block &block) {
        for (const auto &pattern : block.patterns) {
            if (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos) {
                continue;
            }
            Profile profile;
            profile.id = "ssh-import:" + pattern;
            profile.name = pattern;
            profile.type = ConnectionType::Ssh;
            profile.ssh.host = block.host_name.empty() ? pattern : block.host_name;
            if (block.port != 0) {
                profile.ssh.port = block.port;
            }
            profile.ssh.user = block.user;
            profile.ssh.identity_file = block.identity;
            profile.ssh.agent_forwarding = block.forward_agent;
            profile.ssh.known_hosts_policy = block.policy;
            out.push_back(std::move(profile));
        }
    };

    std::istringstream in(std::string{content});
    std::string raw;
    std::optional<Block> block;
    while (std::getline(in, raw)) {
        const std::string_view line = strip_comment(trim(raw));
        if (line.empty()) {
            continue;
        }
        const std::size_t split = line.find_first_of(" \t");
        const std::string_view keyword = trim(line.substr(0, split));
        const std::string_view value = split == std::string_view::npos ? std::string_view{}
                                                                       : trim(line.substr(split + 1));
        const std::string key = to_lower(keyword);

        if (key == "host" || key == "match") {
            if (block) {
                emit(*block);
            }
            block.reset();
            if (key == "host") {
                Block next;
                std::string_view rest = value;
                while (!rest.empty()) {
                    const std::size_t next_split = rest.find_first_of(" \t");
                    const std::string_view pattern = trim(rest.substr(0, next_split));
                    if (!pattern.empty()) {
                        next.patterns.push_back(std::string{pattern});
                    }
                    rest = next_split == std::string_view::npos ? std::string_view{}
                                                                : rest.substr(next_split + 1);
                }
                block = std::move(next);
            }
            continue;
        }

        if (!block) {
            continue;  // 全局段（无 Host 包裹）不导入。
        }

        if (key == "hostname") {
            block->host_name = unquote(value);
        } else if (key == "user") {
            block->user = unquote(value);
        } else if (key == "port") {
            int parsed{};
            const auto *begin = value.data();
            const auto *end = begin + value.size();
            if (const auto [next, error] = std::from_chars(begin, end, parsed);
                error == std::errc{} && next == end) {
                block->port = parsed;
            }
        } else if (key == "identityfile") {
            if (block->identity.empty()) {
                block->identity = unquote(value);
            }
        } else if (key == "forwardagent") {
            const auto lowered = to_lower(unquote(value));
            block->forward_agent = (lowered == "yes" || lowered == "true" || lowered == "on");
        } else if (key == "stricthostkeychecking") {
            const auto lowered = to_lower(unquote(value));
            if (lowered == "yes") {
                block->policy = KnownHostsPolicy::Yes;
            } else if (lowered == "ask") {
                block->policy = KnownHostsPolicy::Ask;
            } else if (lowered == "no") {
                block->policy = KnownHostsPolicy::No;
            } else {
                block->policy = KnownHostsPolicy::AcceptNew;
            }
        }
        // ProxyJump 等其余键本切片延后（CONN.03），忽略不导入。
    }
    if (block) {
        emit(*block);
    }
    return out;
}

}  // namespace borealis::conn
