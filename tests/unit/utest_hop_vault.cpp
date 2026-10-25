/// 测试类型: unit
/// 目标单元: src/conn/profile_views.h 的逐跳 vault 键位折算（`SPEC.FEAT.CONN.03` 批 8）
/// 测试说明: 锁键形（user 空补 `@`、IPv6 方括号、档案 id 的 `#`/`%` 转义）、坏输入拒编，
///           以及「只有 Ask 跳才有键位」与串问泵同源。回存的写盘动作在装配层，本层只出键。

#include "conn/profile_views.h"

#include <string>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_hop_vault {

namespace {

[[nodiscard]] auto make_hop(std::string host, int port, std::string user,
                            std::string auth = "password") -> conn::SshHop {
    auto hop = conn::SshHop{};
    hop.host = std::move(host);
    hop.port = port;
    hop.user = std::move(user);
    hop.auth_method = std::move(auth);
    return hop;
}

}  // namespace

AURORA_TEST_CASE(hop_vault_key_is_addressed_by_identity_not_by_index) {
    using conn::hop_vault_key;

    const auto named = make_hop("gw.example.com", 22, "ops");
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", named) == "p-1#ops@gw.example.com:22");

    // 无名用户保留前导 `@`：键形不靠「省略 user@」表达身份，改 user 就是换键。
    auto unnamed = named;
    unnamed.user.clear();
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", unnamed) == "p-1#@gw.example.com:22");
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", named) != hop_vault_key("p-1", unnamed));

    // 端口显式写出来：留空折 22 与 `:22` 是同一个身份，不能因写法不同分键。
    auto implicit_port = named;
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", implicit_port) ==
                           hop_vault_key("p-1", named));
    implicit_port.port = 22;
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", implicit_port) == "p-1#ops@gw.example.com:22");

    // IPv6 走 render 的唯一方括号写法（裸写下一轮解析会被拒）。
    const auto v6 = make_hop("::1", 2200, "ops");
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", v6) == "p-1#ops@[::1]:2200");

    // 幂等：同一跳同一档案恒得同一键（写回与下次寻址靠这条对齐）。
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", named) == hop_vault_key("p-1", named));
}

AURORA_TEST_CASE(hop_vault_key_refuses_unkeyable_inputs) {
    using conn::hop_vault_key;

    const auto good = make_hop("gw.example.com", 22, "ops");
    AURORA_TEST_CHECK_TRUE(hop_vault_key("", good).empty());

    // NUL 进不了 OS 库的 C 字符串查询面：宁可这一跳不存，也不折出半截键。
    const std::string nul_id{"p\0x", 3};
    AURORA_TEST_CHECK_TRUE(hop_vault_key(nul_id, good).empty());

    auto bad = good;
    bad.host.clear();
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", bad).empty());
    bad = good;
    bad.port = 0;
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", bad).empty());
    bad = good;
    bad.user = "o ps";
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", bad).empty());
    // `%` 是 OpenSSH 宏的写法，语法层就拒；键位层不替它展开。
    bad = good;
    bad.host = "%h.example.com";
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p-1", bad).empty());
}

AURORA_TEST_CASE(hop_vault_key_escapes_the_separator_in_the_profile_id) {
    using conn::hop_vault_key;

    const auto hop = make_hop("gw.example.com", 22, "ops");
    // `#` 只作键位分隔符：档案 id 自带的 `#` 转义后不会把键劈成两段。
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p#1", hop) == "p%231#ops@gw.example.com:22");
    // `%` 也转义，故字面的 `p%231` 与 `p#1` 不撞键（转义是单趟且可逆）。
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p%231", hop) == "p%25231#ops@gw.example.com:22");
    AURORA_TEST_CHECK_TRUE(hop_vault_key("p#1", hop) != hop_vault_key("p%231", hop));
}

AURORA_TEST_CASE(plan_hop_vault_keys_marks_only_ask_hops) {
    using conn::plan_hop_vault_keys;

    conn::InMemoryCredentialStore memory;

    auto profile = conn::SshProfile{};
    profile.proxy_jumps.push_back(make_hop("agent.example.com", 22, "ops", "agent"));
    auto ask = make_hop("ask.example.com", 2222, "ops", "password");
    profile.proxy_jumps.push_back(ask);
    auto stored = make_hop("stored.example.com", 22, "ops", "password");
    stored.credential = conn::SecretHandle::reference("libsecret:stored");
    profile.proxy_jumps.push_back(stored);
    auto sentinel = make_hop("sentinel.example.com", 22, "ops", "privatekey");
    sentinel.credential = conn::SecretHandle::ask_every_time("p-1", "sentinel");
    profile.proxy_jumps.push_back(sentinel);

    const auto keys = plan_hop_vault_keys(profile, memory, "p-1");
    AURORA_TEST_REQUIRE(keys.size() == profile.proxy_jumps.size());

    // 与串问泵同一判据：None/Stored 不占键位，Ask 才有；键位逐字节等于折算件。
    AURORA_TEST_CHECK_TRUE(keys[0].empty());
    AURORA_TEST_CHECK_TRUE(keys[1] == "p-1#ops@ask.example.com:2222");
    AURORA_TEST_CHECK_TRUE(keys[2].empty());
    AURORA_TEST_CHECK_TRUE(keys[3] == "p-1#ops@sentinel.example.com:22");

    // 临时档案（quick connect）没有档案 id ⇒ 整表空、调用方无须再判。
    const auto no_id = plan_hop_vault_keys(profile, memory, "");
    AURORA_TEST_REQUIRE(no_id.size() == profile.proxy_jumps.size());
    for (const auto &key : no_id) {
        AURORA_TEST_CHECK_TRUE(key.empty());
    }

    // 库不可用时 Stored 档按既有判据落 Ask（取不了就得问），键位照样可算；
    // 「库不可用整段不回存」是装配层的闸（稿 D5②），不在键位层重复判。
    conn::UnavailableCredentialStore unavailable;
    const auto degraded = plan_hop_vault_keys(profile, unavailable, "p-1");
    AURORA_TEST_REQUIRE(degraded.size() == profile.proxy_jumps.size());
    AURORA_TEST_CHECK_TRUE(degraded[2] == "p-1#ops@stored.example.com:22");
}

AURORA_TEST_CASE(hop_writeback_takes_still_ask_hops_with_matching_keys) {
    using conn::plan_hop_writeback;

    conn::InMemoryCredentialStore memory;
    auto profile = conn::Profile{};
    profile.id = "p-1";
    profile.name = "Prod gateway";
    profile.type = conn::ConnectionType::Ssh;
    auto ask = make_hop("ask.example.com", 2222, "ops");
    auto stored = make_hop("stored.example.com", 22, "ops");
    stored.credential = conn::SecretHandle::reference("libsecret:stored");
    profile.ssh.proxy_jumps = {ask, stored};

    const auto ask_key = conn::hop_vault_key("p-1", ask);
    const auto stored_key = conn::hop_vault_key("p-1", stored);
    const auto ask_handle_before = profile.ssh.proxy_jumps[0].credential;
    const auto stored_handle_before = profile.ssh.proxy_jumps[1].credential;
    // 混入一枚对不上的旧键位、一枚空键位、一枚越界下标与一枚无答案的跳：
    // 回存只认「键对得上 ∧ 当前仍 Ask ∧ 有明文」这一档。
    const auto answered = std::vector<std::pair<std::size_t, std::string>>{
        {0, ask_key}, {1, stored_key}, {2, "p-1#ops@ghost:22"}, {0, ""}};
    // stored 跳也备一枚明文：若「当前仍 Ask」这道闸失效，旧答话会把库里那条覆盖掉；
    // 只给 nullopt 的话该条目在「有明文」闸就已被拦下，这一刃便测不到 Ask 闸。
    const auto secrets = std::vector<std::optional<std::string>>{"pw-ask", "pw-stored"};
    const auto out = plan_hop_writeback(profile, memory, answered, secrets);

    AURORA_TEST_REQUIRE(out.size() == 1U);
    AURORA_TEST_CHECK_EQ(out[0].index, 0U);
    AURORA_TEST_CHECK_TRUE(out[0].key == ask_key);
    AURORA_TEST_CHECK_TRUE(out[0].secret == "pw-ask");
    AURORA_TEST_CHECK_TRUE(out[0].label == "Prod gateway / ops@ask.example.com:2222");
    // 计划件不改档案：两枚句柄原样不动，置句柄是装配层那一步搬运。
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[0].credential == ask_handle_before);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[1].credential == stored_handle_before);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[1].credential.kind ==
                           conn::SecretKind::Reference);
}

AURORA_TEST_CASE(hop_writeback_refuses_a_chain_edited_while_asking) {
    using conn::plan_hop_writeback;

    conn::InMemoryCredentialStore memory;
    auto profile = conn::Profile{};
    profile.id = "p-1";
    profile.name = "Prod gateway";
    profile.type = conn::ConnectionType::Ssh;
    profile.ssh.proxy_jumps = {make_hop("new-gw.example.com", 22, "ops")};
    const auto old_hop = make_hop("old-gw.example.com", 22, "ops");
    const auto answered = std::vector<std::pair<std::size_t, std::string>>{
        {0, conn::hop_vault_key("p-1", old_hop)}};
    const auto secrets = std::vector<std::optional<std::string>>{"pw-old"};

    AURORA_TEST_CHECK_TRUE(plan_hop_writeback(profile, memory, answered, secrets).empty());

    // 链改成「user 相同、主机相同、端口不同」也是换了身份：旧键位同样不得复用。
    profile.ssh.proxy_jumps[0].port = 2222;
    AURORA_TEST_CHECK_TRUE(plan_hop_writeback(profile, memory, answered, secrets).empty());
}

AURORA_TEST_CASE(hop_writeback_refuses_non_ssh_and_unanswerable_entries) {
    using conn::plan_hop_writeback;

    conn::InMemoryCredentialStore memory;
    auto local = conn::Profile{};
    local.id = "local-1";
    local.name = "Local";
    local.type = conn::ConnectionType::Local;
    // 残留的合法 SSH 跳（手改档案／旧版迁移的坏形状）：类型闸必须独立于键位复核生效，
    // 不能让一份 Local 档案因残料把谜底写进 ssh 域。
    local.ssh.proxy_jumps = {make_hop("gw.example.com", 22, "ops")};
    const auto local_key = conn::hop_vault_key("local-1", local.ssh.proxy_jumps[0]);
    const auto answered = std::vector<std::pair<std::size_t, std::string>>{{0, local_key}};
    const auto secrets = std::vector<std::optional<std::string>>{"pw"};
    AURORA_TEST_CHECK_TRUE(plan_hop_writeback(local, memory, answered, secrets).empty());

    // SSH 档案但 id 为空（临时档案）：hop_vault_key 回空串，键位复核必然对不上。
    auto temporary = conn::Profile{};
    temporary.name = "Quick";
    temporary.type = conn::ConnectionType::Ssh;
    temporary.ssh.proxy_jumps = {make_hop("gw.example.com", 22, "")};
    const auto temp_key = conn::hop_vault_key("", temporary.ssh.proxy_jumps[0]);
    AURORA_TEST_CHECK_TRUE(temp_key.empty());
    const auto temp_answered = std::vector<std::pair<std::size_t, std::string>>{{0, temp_key}};
    AURORA_TEST_CHECK_TRUE(plan_hop_writeback(temporary, memory, temp_answered, secrets).empty());

    // 档案正常但答案表短于下标 / 没有答案：跳过，不是越界。
    auto profile = conn::Profile{};
    profile.id = "p-1";
    profile.name = "Prod gateway";
    profile.type = conn::ConnectionType::Ssh;
    profile.ssh.proxy_jumps = {make_hop("gw.example.com", 22, "ops")};
    const auto key = conn::hop_vault_key("p-1", profile.ssh.proxy_jumps[0]);
    AURORA_TEST_CHECK_TRUE(
        plan_hop_writeback(profile, memory, {{0, key}}, std::vector<std::optional<std::string>>{})
            .empty());
    AURORA_TEST_CHECK_TRUE(
        plan_hop_writeback(profile, memory, {{1, key}}, secrets).empty());
}

}  // namespace borealis::test_cases::utest_hop_vault
