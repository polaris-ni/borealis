/// 测试类型: unit
/// 目标单元: src/conn/ssh_connection.h（私有头）纯逻辑裁决函数
/// 测试说明: 守住 SSH 传输抽出来的纯逻辑矩阵——主机密钥策略裁决、端口/认证方式
///           归一、认证材料需求判定。真实 ssh_connect/认证属网络 I/O，不在无头
///           单测范围（沙箱无 sshd）；传输体的线程纪律与 forkpty 腿同构。

#include "conn/ssh_connection.h"

#include <string>

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

}  // namespace borealis::test_cases::utest_ssh
