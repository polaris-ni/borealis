/// 测试类型: unit
/// 目标单元: include/borealis/conn/profile.h + src/conn/profile.cpp
/// 测试说明: M3 `SPEC.FEAT.CONN.03`（档案模型与查询、~/.ssh/config 只读导入）与
///           `SPEC.FEAT.CONN.09`（凭据句柄不变量、内存/降级凭据库替身）的纯逻辑单测。
///           落盘往返由 `utest_config` 的 `every_value_survives_a_write_read_round_trip` 守（其
///           `non_default()` 已含一条非缺省 SSH 档案），本件只验模型本身，不碰文件系统。

#include <string>
#include <string_view>
#include <vector>

#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_profile {

using borealis::conn::ConnectionType;
using borealis::conn::InMemoryCredentialStore;
using borealis::conn::KnownHostsPolicy;
using borealis::conn::parse_ssh_config;
using borealis::conn::Profile;
using borealis::conn::ProfileStore;
using borealis::conn::SecretHandle;
using borealis::conn::SecretKind;
using borealis::conn::UnavailableCredentialStore;

AURORA_TEST_CASE(secret_handle_never_carries_plaintext) {
    const auto ref = SecretHandle::reference("vault://x");
    AURORA_TEST_CHECK_FALSE(ref.is_plaintext());
    AURORA_TEST_CHECK_EQ(ref.kind, SecretKind::Reference);
    AURORA_TEST_CHECK_EQ(ref.vault_id, std::string{"vault://x"});

    const auto ask = SecretHandle::ask_every_time("p1", "label");
    AURORA_TEST_CHECK_FALSE(ask.is_plaintext());
    AURORA_TEST_CHECK_EQ(ask.kind, SecretKind::AskEveryTime);
    AURORA_TEST_CHECK_EQ(ask.profile_id, std::string{"p1"});
    AURORA_TEST_CHECK_EQ(ask.label, std::string{"label"});

    // 同内容引用相等：往返等值依赖此不变性。
    AURORA_TEST_CHECK_TRUE(SecretHandle::reference("v") == SecretHandle::reference("v"));
    AURORA_TEST_CHECK_TRUE(SecretHandle::ask_every_time("p", "l") == SecretHandle::ask_every_time("p", "l"));
}

AURORA_TEST_CASE(in_memory_credential_store_round_trips) {
    InMemoryCredentialStore store;
    AURORA_TEST_CHECK_TRUE(store.available());
    const auto handle = store.store("p1", "Gateway password", "hunter2");
    AURORA_TEST_CHECK_EQ(handle.kind, SecretKind::Reference);
    const auto secret = store.retrieve(handle);
    AURORA_TEST_CHECK_TRUE(secret.has_value());
    AURORA_TEST_CHECK_EQ(*secret, std::string{"hunter2"});
    // 不存在的 vault id 取不到。
    AURORA_TEST_CHECK_FALSE(store.retrieve(SecretHandle::reference("absent")).has_value());
}

AURORA_TEST_CASE(unavailable_credential_store_degrades_to_ask) {
    UnavailableCredentialStore store;
    AURORA_TEST_CHECK_FALSE(store.available());
    const auto handle = store.store("p1", "label", "hunter2");
    AURORA_TEST_CHECK_EQ(handle.kind, SecretKind::AskEveryTime);
    AURORA_TEST_CHECK_EQ(handle.profile_id, std::string{"p1"});
    // 降级路径绝不保存秘密：取回为空。
    AURORA_TEST_CHECK_FALSE(store.retrieve(handle).has_value());
}

AURORA_TEST_CASE(profile_store_add_remove_update) {
    ProfileStore store;
    Profile a;
    a.id = "a";
    a.name = "Alpha";
    a.groups = {"g1"};
    a.favorite = true;
    Profile b;
    b.id = "b";
    b.name = "Beta";
    b.groups = {"g1", "g2"};

    AURORA_TEST_CHECK_TRUE(store.add(a));
    AURORA_TEST_CHECK_TRUE(store.add(b));
    AURORA_TEST_CHECK_FALSE(store.add(a));  // id 冲突不改集合
    AURORA_TEST_CHECK_EQ(store.size(), 2u);

    AURORA_TEST_CHECK_TRUE(store.find("a") != nullptr);
    AURORA_TEST_CHECK_TRUE(store.find("z") == nullptr);

    AURORA_TEST_CHECK_EQ(store.by_group("g1").size(), 2u);
    AURORA_TEST_CHECK_EQ(store.by_group("g2").size(), 1u);
    AURORA_TEST_CHECK_EQ(store.favorites().size(), 1u);

    b.name = "Beta-renamed";
    AURORA_TEST_CHECK_TRUE(store.update(b));
    AURORA_TEST_CHECK_EQ(store.find("b")->name, std::string{"Beta-renamed"});
    Profile missing;
    missing.id = "nope";
    AURORA_TEST_CHECK_FALSE(store.update(missing));  // 不存在则失败

    AURORA_TEST_CHECK_TRUE(store.remove("a"));
    AURORA_TEST_CHECK_FALSE(store.remove("a"));
    AURORA_TEST_CHECK_EQ(store.size(), 1u);
}

AURORA_TEST_CASE(profile_store_search_is_case_insensitive) {
    ProfileStore store;
    Profile a;
    a.id = "a";
    a.name = "Gateway";
    a.tags = {"infra"};
    Profile b;
    b.id = "b";
    b.name = "Web";
    b.type = ConnectionType::Ssh;
    b.ssh.host = "web.internal";
    store.add(a);
    store.add(b);

    AURORA_TEST_CHECK_EQ(store.search("gate").size(), 1u);
    AURORA_TEST_CHECK_EQ(store.search("INFRA").size(), 1u);   // 标签，大小写不敏感
    AURORA_TEST_CHECK_EQ(store.search("web.internal").size(), 1u);  // 主机
    AURORA_TEST_CHECK_EQ(store.search("").size(), 0u);        // 空查询不命中
    AURORA_TEST_CHECK_EQ(store.search("zzz").size(), 0u);
}

AURORA_TEST_CASE(parse_ssh_config_imports_concrete_hosts) {
    constexpr std::string_view config = R"(
# 注释行
Host gateway
    HostName gw.example.com
    User admin
    Port 2222
    IdentityFile ~/.ssh/gw
    ForwardAgent yes
    StrictHostKeyChecking ask

Host web1 web2
    HostName web.internal
    User deploy

Host *
    ServerAliveInterval 30

Match all
    Compression yes
)";
    const auto profiles = parse_ssh_config(config);
    // gateway + web1 + web2 = 3 条；通配符 `*` 与 Match 块不导入。
    AURORA_TEST_CHECK_EQ(profiles.size(), 3u);

    const auto find = [&profiles](std::string_view id) -> const Profile * {
        for (const auto &profile : profiles) {
            if (profile.id == id) {
                return &profile;
            }
        }
        return nullptr;
    };

    const auto *gateway = find("ssh-import:gateway");
    AURORA_TEST_CHECK_TRUE(gateway != nullptr);
    AURORA_TEST_CHECK_EQ(gateway->type, ConnectionType::Ssh);
    AURORA_TEST_CHECK_EQ(gateway->ssh.host, std::string{"gw.example.com"});
    AURORA_TEST_CHECK_EQ(gateway->ssh.user, std::string{"admin"});
    AURORA_TEST_CHECK_EQ(gateway->ssh.port, 2222);
    AURORA_TEST_CHECK_EQ(gateway->ssh.identity_file, std::string{"~/.ssh/gw"});
    AURORA_TEST_CHECK_TRUE(gateway->ssh.agent_forwarding);
    AURORA_TEST_CHECK_EQ(gateway->ssh.known_hosts_policy, KnownHostsPolicy::Ask);
    AURORA_TEST_CHECK_EQ(gateway->ssh.credential.kind, SecretKind::Reference);  // 默认空引用，无明文

    const auto *web1 = find("ssh-import:web1");
    AURORA_TEST_CHECK_TRUE(web1 != nullptr);
    AURORA_TEST_CHECK_EQ(web1->ssh.host, std::string{"web.internal"});
    AURORA_TEST_CHECK_EQ(web1->ssh.user, std::string{"deploy"});
    AURORA_TEST_CHECK_EQ(web1->ssh.port, 22);  // 未指定则默认

    const auto *web2 = find("ssh-import:web2");
    AURORA_TEST_CHECK_TRUE(web2 != nullptr);
    AURORA_TEST_CHECK_EQ(web2->ssh.host, std::string{"web.internal"});

    AURORA_TEST_CHECK_TRUE(find("ssh-import:*") == nullptr);  // 通配符模板不导入
}

AURORA_TEST_CASE(parse_ssh_config_empty_yields_nothing) {
    AURORA_TEST_CHECK_TRUE(parse_ssh_config("").empty());
    AURORA_TEST_CHECK_TRUE(parse_ssh_config("# only comments\n\n").empty());
}

}  // namespace borealis::test_cases::utest_profile
