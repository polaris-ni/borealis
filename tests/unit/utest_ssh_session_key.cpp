/// 测试类型: unit
/// 目标单元: src/conn/ssh_shared.h 的共享判据（`SshSessionKey` 与 `same_secret`）
/// 测试说明: `SPEC.FEAT.CONN.02` 连接复用的纯逻辑面——键的归一口径与逐字段区分、
///           秘密材料的全等判定。共享会话本体与 I/O 线程属真协议腿，由
///           `itest_posix_ssh_shared_connection` 出证人；本套件不碰网络。

#include <string>

#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1① 的先例）：连接复用是传输腿内部形态。
#include "conn/ssh_dial.h"
#include "conn/ssh_shared.h"

namespace borealis::test_cases::utest_ssh_session_key {

namespace {

using borealis::conn::KnownHostsPolicy;
using borealis::conn::same_secret;
using borealis::conn::SshAlgorithmOverrides;
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

}  // namespace borealis::test_cases::utest_ssh_session_key
