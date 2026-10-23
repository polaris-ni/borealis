/// 测试类型: unit
/// 目标单元: src/conn/ssh_connection.h（私有头）纯逻辑裁决函数
/// 测试说明: 守住 SSH 传输抽出来的纯逻辑矩阵——主机密钥策略裁决、端口/认证方式
///           归一、认证材料需求判定；另含算法表的本地写入证人（`ssh_options_get`
///           回读，无网络）。真实 ssh_connect/认证属网络 I/O，不在无头单测范围
///           （沙箱无 sshd）；传输体的线程纪律与 forkpty 腿同构。

#include "conn/ssh_connection.h"

#include <string>

#include <libssh/libssh.h>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_ssh {

namespace {

/// @brief 一条主机名/端口/用户/认证方式齐备的 SSH 档案样板。
[[nodiscard]] auto ssh_profile() -> conn::SshProfile {
    auto profile = conn::SshProfile{};
    profile.host = "gateway.example.com";
    profile.port = 22;
    profile.user = "deploy";
    profile.auth_method = "agent";
    return profile;
}

}  // namespace

AURORA_TEST_CASE(policy_accepts_matrix_covers_all_four_policies) {
    using conn::HostKeyState;
    using conn::KnownHostsPolicy;

    // AcceptNew：Known/New 放行，Changed/Error 拒绝。
    AURORA_TEST_CHECK_TRUE(conn::policy_accepts(KnownHostsPolicy::AcceptNew, HostKeyState::Known));
    AURORA_TEST_CHECK_TRUE(conn::policy_accepts(KnownHostsPolicy::AcceptNew, HostKeyState::New));
    AURORA_TEST_CHECK_FALSE(conn::policy_accepts(KnownHostsPolicy::AcceptNew,
                                                 HostKeyState::Changed));
    AURORA_TEST_CHECK_FALSE(conn::policy_accepts(KnownHostsPolicy::AcceptNew,
                                                 HostKeyState::Error));

    // Yes：仅 Known 放行（首次连接也拒）。
    AURORA_TEST_CHECK_TRUE(conn::policy_accepts(KnownHostsPolicy::Yes, HostKeyState::Known));
    AURORA_TEST_CHECK_FALSE(conn::policy_accepts(KnownHostsPolicy::Yes, HostKeyState::New));
    AURORA_TEST_CHECK_FALSE(conn::policy_accepts(KnownHostsPolicy::Yes, HostKeyState::Changed));

    // No：不核对，一律放行。
    AURORA_TEST_CHECK_TRUE(conn::policy_accepts(KnownHostsPolicy::No, HostKeyState::Changed));
    AURORA_TEST_CHECK_TRUE(conn::policy_accepts(KnownHostsPolicy::No, HostKeyState::Error));

    // Ask：本期保守＝Yes 语义，未知绝不静默放行。
    AURORA_TEST_CHECK_TRUE(conn::policy_accepts(KnownHostsPolicy::Ask, HostKeyState::Known));
    AURORA_TEST_CHECK_FALSE(conn::policy_accepts(KnownHostsPolicy::Ask, HostKeyState::New));
    AURORA_TEST_CHECK_FALSE(conn::policy_accepts(KnownHostsPolicy::Ask, HostKeyState::Changed));
}

AURORA_TEST_CASE(effective_port_falls_back_to_22_when_unset) {
    auto profile = ssh_profile();
    AURORA_TEST_CHECK_EQ(conn::effective_port(profile), 22);

    profile.port = 2222;
    AURORA_TEST_CHECK_EQ(conn::effective_port(profile), 2222);

    profile.port = 0;  // 序列化还原侧不会出现，但结构上要兜住。
    AURORA_TEST_CHECK_EQ(conn::effective_port(profile), 22);
    profile.port = -1;
    AURORA_TEST_CHECK_EQ(conn::effective_port(profile), 22);
}

AURORA_TEST_CASE(normalized_auth_method_falls_back_to_agent) {
    auto profile = ssh_profile();

    for (std::string method : {"password", "privatekey", "agent", "keyboard-interactive"}) {
        profile.auth_method = method;
        AURORA_TEST_CHECK_MSG(conn::normalized_auth_method(profile) == method, method);
    }

    profile.auth_method = "";
    AURORA_TEST_CHECK_TRUE(conn::normalized_auth_method(profile) == "agent");
    profile.auth_method = "gssapi";  // 未识别值不猜测，回落模型缺省。
    AURORA_TEST_CHECK_TRUE(conn::normalized_auth_method(profile) == "agent");
}

AURORA_TEST_CASE(auth_requires_secret_only_for_password) {
    AURORA_TEST_CHECK_TRUE(conn::auth_requires_secret("password"));
    AURORA_TEST_CHECK_FALSE(conn::auth_requires_secret("privatekey"));  // passphrase 可选
    AURORA_TEST_CHECK_FALSE(conn::auth_requires_secret("agent"));
    AURORA_TEST_CHECK_FALSE(conn::auth_requires_secret("keyboard-interactive"));
}

// ---- 算法协商面（`SPEC.FEAT.CONN.02` 延后子项，裁决 7.144）----
// `ssh_new()` 之后、`ssh_connect()` 之前的写入是纯本地状态，`ssh_options_get` 能原样回读——
// 这是「设置页那两行真的接管了协商面」在无 sshd 沙箱里能拿到的最强证人。

namespace {

/// @brief 读回一个算法表并释放 libssh 分配的串（`ssh_options_get` 返回 strdup 串）。
[[nodiscard]] auto read_algorithms(ssh_session_struct *session, ssh_options_e option)
    -> std::string {
    char *value = nullptr;
    if (ssh_options_get(session, option, &value) != SSH_OK || value == nullptr) {
        return {};
    }
    std::string out{value};
    ssh_string_free_char(value);
    return out;
}

}  // namespace

AURORA_TEST_CASE(algorithm_overrides_are_written_into_the_allocated_session) {
    ssh_session_struct *session = ssh_new();
    AURORA_TEST_REQUIRE(session != nullptr);

    const auto overrides = conn::SshAlgorithmOverrides{
        .kex = "diffie-hellman-group14-sha1",
        .host_key = "ssh-rsa",
    };
    AURORA_TEST_CHECK_FALSE(overrides.empty());
    AURORA_TEST_CHECK_TRUE(conn::apply_ssh_algorithm_overrides(session, overrides));
    // 回读逐字相等：两串全由库认识的算法名组成，库原样返回请求表（不补缺省、不排序）。
    AURORA_TEST_CHECK_EQ(read_algorithms(session, SSH_OPTIONS_KEY_EXCHANGE), overrides.kex);
    AURORA_TEST_CHECK_EQ(read_algorithms(session, SSH_OPTIONS_HOSTKEYS), overrides.host_key);

    ssh_free(session);
}

AURORA_TEST_CASE(a_list_with_no_known_algorithm_is_refused) {
    ssh_session_struct *session = ssh_new();
    AURORA_TEST_REQUIRE(session != nullptr);

    // 整表没有一个库认识的名字：`ssh_options_set` 回错误，调用方据此落 `DialOutcome::Options`
    // （不静默回退缺省——用户显式写的协商面没生效，必须报出来）。
    AURORA_TEST_CHECK_FALSE(conn::apply_ssh_algorithm_overrides(
        session, conn::SshAlgorithmOverrides{.kex = "not-a-real-kex", .host_key = {}}));
    // 主机密钥那一侧同样有一道；KEX 为空时它才轮到。
    AURORA_TEST_CHECK_FALSE(conn::apply_ssh_algorithm_overrides(
        session, conn::SshAlgorithmOverrides{.kex = {}, .host_key = "not-a-real-hostkey"}));

    ssh_free(session);
}

AURORA_TEST_CASE(empty_overrides_leave_the_library_defaults_untouched) {
    ssh_session_struct *session = ssh_new();
    AURORA_TEST_REQUIRE(session != nullptr);

    const auto before_kex = read_algorithms(session, SSH_OPTIONS_KEY_EXCHANGE);
    const auto before_host_key = read_algorithms(session, SSH_OPTIONS_HOSTKEYS);
    AURORA_TEST_CHECK_TRUE(conn::SshAlgorithmOverrides{}.empty());
    // 空覆盖项＝什么都不写：库缺省表逐字不变（这一步是「没配的人行为零变化」的证人）。
    AURORA_TEST_CHECK_TRUE(
        conn::apply_ssh_algorithm_overrides(session, conn::SshAlgorithmOverrides{}));
    AURORA_TEST_CHECK_EQ(read_algorithms(session, SSH_OPTIONS_KEY_EXCHANGE), before_kex);
    AURORA_TEST_CHECK_EQ(read_algorithms(session, SSH_OPTIONS_HOSTKEYS), before_host_key);

    ssh_free(session);
}

}  // namespace borealis::test_cases::utest_ssh
