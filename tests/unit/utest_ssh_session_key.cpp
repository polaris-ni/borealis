/// 测试类型: unit
/// 目标单元: src/conn/ssh_shared.h 的共享判据（`SshSessionKey` 与 `same_secret`）
/// 测试说明: `SPEC.FEAT.CONN.02` 连接复用的纯逻辑面——键的归一口径与逐字段区分、
///           秘密材料的全等判定。共享会话本体与 I/O 线程属真协议腿，由
///           `itest_posix_ssh_shared_connection` 出证人；本套件不碰网络。
///           `SPEC.FEAT.CONN.03` 把跳链折成归一串形入键、并把逐跳秘密加进秘密判据，
///           两案（`key_carries_the_normalized_hop_chain` /
///           `secret_bundle_compares_hops_elementwise`）见证这一层的分键口径。

#include <string>

#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1① 的先例）：连接复用是传输腿内部形态。
#include "conn/proxy_jump.h"
#include "conn/ssh_dial.h"
#include "conn/ssh_shared.h"

namespace borealis::test_cases::utest_ssh_session_key {

namespace {

using borealis::conn::KnownHostsPolicy;
using borealis::conn::same_secret;
using borealis::conn::SshAlgorithmOverrides;
using borealis::conn::SshSecretBundle;
using borealis::conn::SshProfile;
using borealis::conn::SshSessionKey;

/// @brief 一条字段齐备的 SSH 档案样板（与 `utest_ssh` 同口径）。
[[nodiscard]] auto ssh_profile() -> SshProfile {
    auto profile = SshProfile{};
    profile.host = "gateway.example.com";
    profile.port = 22;
    profile.user = "deploy";
    profile.auth_method = "password";
    profile.identity_file = "/home/deploy/.ssh/id_ed25519";
    return profile;
}

/// @brief 一条与样板全等的键（逐字段可直接改，便于差异用例）。
[[nodiscard]] auto base_key() -> SshSessionKey {
    return SshSessionKey::of(ssh_profile(), SshAlgorithmOverrides{});
}

/// @brief 一枚跳板项：端口留空即 `SshHop` 的初值（生效端口折 22 由 `render` 那一步做）。
[[nodiscard]] auto make_hop(const std::string &host, int port, const std::string &user = "")
    -> borealis::conn::SshHop {
    auto hop = borealis::conn::SshHop{};
    hop.host = host;
    hop.port = port;
    hop.user = user;
    return hop;
}

}  // namespace

AURORA_TEST_CASE(key_normalizes_port_and_auth_method) {
    auto implicit_profile = ssh_profile();
    implicit_profile.port = 0;      // 留空：生效端口折 22。
    implicit_profile.auth_method = "";  // 留空：生效认证方式折 agent。

    auto explicit_profile = ssh_profile();
    explicit_profile.port = 22;
    explicit_profile.auth_method = "agent";

    const auto implicit_key = SshSessionKey::of(implicit_profile, SshAlgorithmOverrides{});
    const auto explicit_key = SshSessionKey::of(explicit_profile, SshAlgorithmOverrides{});
    AURORA_TEST_CHECK_TRUE(implicit_key == explicit_key);
    AURORA_TEST_CHECK_EQ(implicit_key.port, 22);
    AURORA_TEST_CHECK_EQ(implicit_key.auth_method, std::string{"agent"});
}

AURORA_TEST_CASE(key_differs_field_by_field) {
    const auto base = base_key();

    auto other = ssh_profile();
    other.host = "other.example.com";
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(other, SshAlgorithmOverrides{}));

    other = ssh_profile();
    other.port = 2222;
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(other, SshAlgorithmOverrides{}));

    other = ssh_profile();
    other.user = "root";
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(other, SshAlgorithmOverrides{}));

    other = ssh_profile();
    other.auth_method = "privatekey";
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(other, SshAlgorithmOverrides{}));

    other = ssh_profile();
    other.identity_file = "/home/deploy/.ssh/id_rsa";
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(other, SshAlgorithmOverrides{}));

    other = ssh_profile();
    other.agent_forwarding = true;
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(other, SshAlgorithmOverrides{}));

    other = ssh_profile();
    other.known_hosts_policy = KnownHostsPolicy::Yes;
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(other, SshAlgorithmOverrides{}));

    // 原样重建的键必须全等：防止差异判据把「同一条档案」也判成不同。
    AURORA_TEST_CHECK_TRUE(base == SshSessionKey::of(ssh_profile(), SshAlgorithmOverrides{}));
}

AURORA_TEST_CASE(key_separates_algorithm_tables) {
    const auto base = base_key();

    auto kex_only = SshAlgorithmOverrides{.kex = "diffie-hellman-group14-sha1"};
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(ssh_profile(), kex_only));

    auto host_key_only = SshAlgorithmOverrides{.host_key = "ssh-rsa"};
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(ssh_profile(), host_key_only));

    auto both = SshAlgorithmOverrides{.kex = "diffie-hellman-group14-sha1", .host_key = "ssh-rsa"};
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(ssh_profile(), both));

    auto other_host_key = SshAlgorithmOverrides{.kex = "diffie-hellman-group14-sha1",
                                                .host_key = "ssh-ed25519"};
    AURORA_TEST_CHECK_FALSE(SshSessionKey::of(ssh_profile(), both) ==
                            SshSessionKey::of(ssh_profile(), other_host_key));
    // 同一份覆盖项两次折键必须全等（原文串不排序、不改写）。
    AURORA_TEST_CHECK_TRUE(SshSessionKey::of(ssh_profile(), both) ==
                           SshSessionKey::of(ssh_profile(), both));
}

AURORA_TEST_CASE(secret_material_must_match_exactly) {
    const auto none = std::optional<std::string>{};
    const auto alpha = std::optional<std::string>{std::string{"alpha"}};
    const auto beta = std::optional<std::string>{std::string{"beta"}};

    AURORA_TEST_CHECK_TRUE(same_secret(none, none));
    AURORA_TEST_CHECK_TRUE(same_secret(alpha, std::optional<std::string>{std::string{"alpha"}}));
    AURORA_TEST_CHECK_FALSE(same_secret(alpha, beta));
    AURORA_TEST_CHECK_FALSE(same_secret(alpha, none));
    AURORA_TEST_CHECK_FALSE(same_secret(none, beta));
}

AURORA_TEST_CASE(key_carries_the_normalized_hop_chain) {
    const auto base = base_key();

    // 跳链入键：空链路与「一跳」必然分键——合并它们就是把一格标签送进另一条链路。
    auto one_hop = ssh_profile();
    one_hop.proxy_jumps = {make_hop("jump-a.example.com", 22)};
    AURORA_TEST_CHECK_FALSE(base == SshSessionKey::of(one_hop, SshAlgorithmOverrides{}));
    AURORA_TEST_CHECK_EQ(SshSessionKey::of(one_hop, SshAlgorithmOverrides{}).proxy_jumps,
                          std::string{"jump-a.example.com:22"});

    // 端口折叠同 D3：串形里留空与 `:22` 解析成同一个值，串形也就折回同一个串，
    // 于是两种写法共线而不是各拨一条链。这里走解析器（`SshHop::port` 按定义已是生效端口，
    // 手工填 0 不是合法形态，折叠的事实只存在于解析那一步）。
    const auto parsed_plain = borealis::conn::parse_proxy_jump_list("jump-a.example.com");
    const auto parsed_explicit = borealis::conn::parse_proxy_jump_list("jump-a.example.com:22");
    AURORA_TEST_REQUIRE(parsed_plain.has_value());
    AURORA_TEST_REQUIRE(parsed_explicit.has_value());
    auto implicit_port = ssh_profile();
    implicit_port.proxy_jumps = *parsed_plain;
    auto explicit_port = ssh_profile();
    explicit_port.proxy_jumps = *parsed_explicit;
    AURORA_TEST_CHECK_TRUE(SshSessionKey::of(implicit_port, SshAlgorithmOverrides{}) ==
                           SshSessionKey::of(explicit_port, SshAlgorithmOverrides{}));
    AURORA_TEST_CHECK_TRUE(SshSessionKey::of(implicit_port, SshAlgorithmOverrides{}) ==
                           SshSessionKey::of(one_hop, SshAlgorithmOverrides{}));

    // 跳主机、跳端口、跳数、跳序各分一键（顺序写反就是两条不同的链路，不许撞键）。
    auto other_host = ssh_profile();
    other_host.proxy_jumps = {make_hop("jump-b.example.com", 22)};
    AURORA_TEST_CHECK_FALSE(SshSessionKey::of(one_hop, SshAlgorithmOverrides{}) ==
                            SshSessionKey::of(other_host, SshAlgorithmOverrides{}));

    auto other_port = ssh_profile();
    other_port.proxy_jumps = {make_hop("jump-a.example.com", 2222)};
    AURORA_TEST_CHECK_FALSE(SshSessionKey::of(one_hop, SshAlgorithmOverrides{}) ==
                            SshSessionKey::of(other_port, SshAlgorithmOverrides{}));

    auto two_hops = ssh_profile();
    two_hops.proxy_jumps = {make_hop("jump-a.example.com", 22), make_hop("jump-b.example.com", 22)};
    auto reversed = ssh_profile();
    reversed.proxy_jumps = {make_hop("jump-b.example.com", 22), make_hop("jump-a.example.com", 22)};
    AURORA_TEST_CHECK_FALSE(SshSessionKey::of(one_hop, SshAlgorithmOverrides{}) ==
                            SshSessionKey::of(two_hops, SshAlgorithmOverrides{}));
    AURORA_TEST_CHECK_FALSE(SshSessionKey::of(two_hops, SshAlgorithmOverrides{}) ==
                            SshSessionKey::of(reversed, SshAlgorithmOverrides{}));

    // 用户也参与串形：同一台跳板、不同身份不该并成一条链。
    auto other_user = ssh_profile();
    other_user.proxy_jumps = {make_hop("jump-a.example.com", 22, "ops")};
    AURORA_TEST_CHECK_FALSE(SshSessionKey::of(one_hop, SshAlgorithmOverrides{}) ==
                            SshSessionKey::of(other_user, SshAlgorithmOverrides{}));
}

AURORA_TEST_CASE(secret_bundle_compares_hops_elementwise) {
    const auto target = std::optional<std::string>{std::string{"target-secret"}};
    const auto hop_a = std::optional<std::string>{std::string{"hop-a"}};
    const auto hop_b = std::optional<std::string>{std::string{"hop-b"}};

    // 隐式转换（旧形态只带目标材料）与显式空跳链同判。
    AURORA_TEST_CHECK_TRUE(same_secret(SshSecretBundle{target}, SshSecretBundle{target}));
    AURORA_TEST_CHECK_TRUE(same_secret(SshSecretBundle{target}, target));

    auto lhs = SshSecretBundle{target};
    auto rhs = SshSecretBundle{target};
    lhs.hops = {hop_a};
    rhs.hops = {hop_a};
    AURORA_TEST_CHECK_TRUE(same_secret(lhs, rhs));

    // 同目标、同跳链，但跳板口令不同 ⇒ 两条不该合并的链路。
    rhs.hops = {hop_b};
    AURORA_TEST_CHECK_FALSE(same_secret(lhs, rhs));

    // 缺省与「显式没有材料」在逐跳上不是同一件事：一份填了 nullopt（agent 认证）、
    // 一份压根没填（跳数不匹配）分开判，装配侧的形态错因此不会被判成「可以共享」。
    rhs.hops = {std::optional<std::string>{}};
    AURORA_TEST_CHECK_FALSE(same_secret(lhs, rhs));
    auto agent_only = SshSecretBundle{target};
    agent_only.hops = {std::optional<std::string>{}};
    AURORA_TEST_CHECK_TRUE(same_secret(rhs, agent_only));

    // 逐跳数量不同即不同材料集。
    auto two = SshSecretBundle{target};
    two.hops = {hop_a, hop_b};
    lhs.hops = {hop_a};
    AURORA_TEST_CHECK_FALSE(same_secret(lhs, two));
}

}  // namespace borealis::test_cases::utest_ssh_session_key
