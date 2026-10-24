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
    Profile c;
    c.id = "c";
    c.name = "Board console";
    c.type = ConnectionType::Telnet;
    c.telnet.host = "10.0.0.9";
    store.add(a);
    store.add(b);
    store.add(c);

    AURORA_TEST_CHECK_EQ(store.search("gate").size(), 1u);
    AURORA_TEST_CHECK_EQ(store.search("INFRA").size(), 1u);   // 标签，大小写不敏感
    AURORA_TEST_CHECK_EQ(store.search("web.internal").size(), 1u);  // 主机
    // Telnet 档案按主机可搜（裁决 7.112 D1①：进模型就要和 SSH 一样能被侧栏找到）。
    AURORA_TEST_CHECK_EQ(store.search("10.0.0.9").size(), 1u);
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

AURORA_TEST_CASE(parse_ssh_config_imports_a_proxy_jump_chain) {
    // `SPEC.FEAT.CONN.03` 延后子项的导入腿（稿 D7）：跳序＝串形次序，且逐跳的默认策略跟随
    // **本块**的 `StrictHostKeyChecking`——写在 `ProxyJump` 之前或之后同判（值留到 emit 才解析）。
    constexpr std::string_view config = R"(
Host via-bastion
    HostName target.internal
    User dev
    ProxyJump ops@bastion1.example.com:2222,bastion2.example.com
    StrictHostKeyChecking ask

Host late-policy
    ProxyJump b.example.com
    StrictHostKeyChecking no

Host explicit-none
    HostName direct.internal
    ProxyJump none
)";
    const auto profiles = parse_ssh_config(config);
    AURORA_TEST_CHECK_EQ(profiles.size(), 3u);

    const auto find = [&profiles](std::string_view id) -> const Profile * {
        for (const auto &profile : profiles) {
            if (profile.id == id) {
                return &profile;
            }
        }
        return nullptr;
    };

    const auto *via = find("ssh-import:via-bastion");
    AURORA_TEST_REQUIRE(via != nullptr);
    AURORA_TEST_REQUIRE_EQ(via->ssh.proxy_jumps.size(), 2u);
    const auto &first = via->ssh.proxy_jumps[0];
    AURORA_TEST_CHECK_EQ(first.host, std::string{"bastion1.example.com"});
    AURORA_TEST_CHECK_EQ(first.user, std::string{"ops"});
    AURORA_TEST_CHECK_EQ(first.port, 2222);
    // 串形只带主机／用户／端口：认证方式与私钥留待装配侧，句柄恒空引用（CONN.09）。
    AURORA_TEST_CHECK_EQ(first.auth_method, std::string{"agent"});
    AURORA_TEST_CHECK_EQ(first.identity_file, std::string{});
    AURORA_TEST_CHECK_EQ(first.credential.kind, SecretKind::Reference);
    AURORA_TEST_CHECK_EQ(first.known_hosts_policy, KnownHostsPolicy::Ask);
    const auto &second = via->ssh.proxy_jumps[1];
    AURORA_TEST_CHECK_EQ(second.host, std::string{"bastion2.example.com"});
    AURORA_TEST_CHECK_TRUE(second.user.empty());
    AURORA_TEST_CHECK_EQ(second.port, 22);  // 串形没写端口即折 22

    const auto *late = find("ssh-import:late-policy");
    AURORA_TEST_REQUIRE(late != nullptr);
    AURORA_TEST_REQUIRE_EQ(late->ssh.proxy_jumps.size(), 1u);
    AURORA_TEST_CHECK_EQ(late->ssh.proxy_jumps[0].known_hosts_policy, KnownHostsPolicy::No);

    // `none` 是显式关闭、不是坏值：档案照常导入，只是链路为空。
    const auto *direct = find("ssh-import:explicit-none");
    AURORA_TEST_REQUIRE(direct != nullptr);
    AURORA_TEST_CHECK_TRUE(direct->ssh.proxy_jumps.empty());
}

AURORA_TEST_CASE(parse_ssh_config_drops_the_whole_block_on_a_bad_proxy_jump) {
    // 坏链「只丢这一键」会产出一条「用户以为走跳板、实则直连」的档案——正是稿 D6 要防的
    // fail-open。本件是无日志的纯逻辑件，于是取更保守的那一档：整块不导入，少一条档案
    // 如实反映在导入条数上（用户看得见「这条没进来」，看不见「这条偷偷不跳板」）。
    constexpr std::string_view config = R"(
Host good-first
    HostName a.example.com

Host bad-macro
    HostName b.example.com
    ProxyJump %h.jump.example.com

Host good-last
    HostName c.example.com
    ProxyJump ok.example.com
)";
    const auto profiles = parse_ssh_config(config);
    AURORA_TEST_CHECK_EQ(profiles.size(), 2u);

    const auto find = [&profiles](std::string_view id) -> const Profile * {
        for (const auto &profile : profiles) {
            if (profile.id == id) {
                return &profile;
            }
        }
        return nullptr;
    };
    AURORA_TEST_CHECK_TRUE(find("ssh-import:bad-macro") == nullptr);

    // 反空转：坏块前后的好块都在，且最后那条的链真折出来了——只数条数看不出「跳过没断」。
    const auto *last = find("ssh-import:good-last");
    AURORA_TEST_REQUIRE(last != nullptr);
    AURORA_TEST_REQUIRE_EQ(last->ssh.proxy_jumps.size(), 1u);
    AURORA_TEST_CHECK_EQ(last->ssh.proxy_jumps[0].host, std::string{"ok.example.com"});
    AURORA_TEST_CHECK_TRUE(find("ssh-import:good-first") != nullptr);
}

AURORA_TEST_CASE(parse_ssh_config_empty_yields_nothing) {
    AURORA_TEST_CHECK_TRUE(parse_ssh_config("").empty());
    AURORA_TEST_CHECK_TRUE(parse_ssh_config("# only comments\n\n").empty());
}

}  // namespace borealis::test_cases::utest_profile
