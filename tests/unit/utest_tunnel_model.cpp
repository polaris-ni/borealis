/// 测试类型: unit
/// 目标单元: src/conn/tunnel_model.h（私有头）纯逻辑裁决函数
/// 测试说明: 守住 CONN.08 的纯逻辑矩阵——形态校验、缺省监听地址、同侧端口
///           冲突判定、状态机迁移表、指数退避与重试穷尽裁决。真实拨号与转发
///           泵属网络 I/O，不在无头单测范围（沙箱无 sshd）。

#include "conn/tunnel_model.h"

#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_tunnel_model {

namespace {

/// @brief 一条字段齐备的 -L 样板。
[[nodiscard]] auto local_spec() -> conn::TunnelSpec {
    auto spec = conn::TunnelSpec{};
    spec.id = "t1";
    spec.name = "web";
    spec.kind = conn::TunnelKind::Local;
    spec.listen_port = 8080;
    spec.target_host = "127.0.0.1";
    spec.target_port = 80;
    return spec;
}

}  // namespace

AURORA_TEST_CASE(validate_spec_matrix_covers_three_kinds) {
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_tunnel_spec(local_spec())),
                         static_cast<int>(conn::TunnelSpecIssue::None));

    // Local/Dynamic 监听端口必须 >=1；Remote 允许 0＝服务器择定。
    auto zero = local_spec();
    zero.listen_port = 0;
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_tunnel_spec(zero)),
                         static_cast<int>(conn::TunnelSpecIssue::ListenPortInvalid));
    auto remote = zero;
    remote.kind = conn::TunnelKind::Remote;
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_tunnel_spec(remote)),
                         static_cast<int>(conn::TunnelSpecIssue::None));

    // 越界端口两侧都拦。
    auto over = local_spec();
    over.listen_port = 70000;
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_tunnel_spec(over)),
                         static_cast<int>(conn::TunnelSpecIssue::ListenPortInvalid));
    over.listen_port = 1024;
    over.target_port = -1;
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_tunnel_spec(over)),
                         static_cast<int>(conn::TunnelSpecIssue::TargetPortInvalid));

    // Local/Remote 缺目标主机；Dynamic 无 target 也合法、有 target 也不拦（残值忽略）。
    auto no_host = local_spec();
    no_host.target_host.clear();
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_tunnel_spec(no_host)),
                         static_cast<int>(conn::TunnelSpecIssue::TargetHostEmpty));
    auto dynamic = local_spec();
    dynamic.kind = conn::TunnelKind::Dynamic;
    dynamic.target_host.clear();
    dynamic.target_port = 0;
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::validate_tunnel_spec(dynamic)),
                         static_cast<int>(conn::TunnelSpecIssue::None));
}

AURORA_TEST_CASE(effective_listen_address_defaults_per_side) {
    auto local = local_spec();
    AURORA_TEST_CHECK_MSG(conn::effective_listen_address(local) == "127.0.0.1",
                          "local defaults to loopback");
    local.listen_address = "0.0.0.0";
    AURORA_TEST_CHECK_MSG(conn::effective_listen_address(local) == "0.0.0.0",
                          "explicit address wins");
    auto remote = local_spec();
    remote.kind = conn::TunnelKind::Remote;
    remote.listen_address.clear();
    AURORA_TEST_CHECK_TRUE(conn::effective_listen_address(remote).empty());
}

AURORA_TEST_CASE(specs_conflict_compares_same_side_only) {
    auto a = local_spec();
    auto b = local_spec();
    b.id = "t2";
    AURORA_TEST_CHECK_TRUE(conn::specs_conflict(a, b));

    // 同端口不同生效地址不撞。
    b.listen_address = "0.0.0.0";
    AURORA_TEST_CHECK_FALSE(conn::specs_conflict(a, b));

    // Local 与 Dynamic 同侧同端口也算撞（都在本机 listen）。
    auto dynamic = local_spec();
    dynamic.kind = conn::TunnelKind::Dynamic;
    AURORA_TEST_CHECK_TRUE(conn::specs_conflict(a, dynamic));

    // Remote 与本地式分属两端，不撞；Remote 0 端口无法预知，不撞。
    auto remote = local_spec();
    remote.kind = conn::TunnelKind::Remote;
    remote.listen_address.clear();
    AURORA_TEST_CHECK_FALSE(conn::specs_conflict(a, remote));
    auto remote2 = remote;
    remote2.listen_port = 0;
    AURORA_TEST_CHECK_FALSE(conn::specs_conflict(remote, remote2));

    // 同侧 Remote 全等端口才撞。
    auto remote3 = remote;
    AURORA_TEST_CHECK_TRUE(conn::specs_conflict(remote, remote3));

    // 整体闸：三只里藏一对即报。
    AURORA_TEST_CHECK_TRUE(conn::has_conflict(std::vector{a, dynamic, remote}));
    AURORA_TEST_CHECK_FALSE(conn::has_conflict(std::vector{a, remote}));
    AURORA_TEST_CHECK_FALSE(conn::has_conflict(std::vector<conn::TunnelSpec>{}));
}

AURORA_TEST_CASE(state_machine_table_and_idempotent_hold) {
    using conn::TunnelSignal;
    using conn::TunnelState;

    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(TunnelState::Stopped,
                                                           TunnelSignal::Start)),
                         static_cast<int>(TunnelState::Dialing));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(TunnelState::Dialing,
                                                           TunnelSignal::DialSucceeded)),
                         static_cast<int>(TunnelState::Active));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(TunnelState::Active,
                                                           TunnelSignal::RecoverableFailure)),
                         static_cast<int>(TunnelState::Backoff));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(TunnelState::Backoff,
                                                           TunnelSignal::Start)),
                         static_cast<int>(TunnelState::Dialing));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(TunnelState::Dialing,
                                                           TunnelSignal::FatalFailure)),
                         static_cast<int>(TunnelState::Failed));
    // Failed 只被用户再启唤醒。
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(TunnelState::Failed,
                                                           TunnelSignal::Start)),
                         static_cast<int>(TunnelState::Dialing));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(TunnelState::Failed,
                                                           TunnelSignal::RecoverableFailure)),
                         static_cast<int>(TunnelState::Failed));
    // Stop 对任何状态（含 Failed）都回 Stopped；非法组合保持原态。
    for (const auto state : {TunnelState::Stopped, TunnelState::Dialing, TunnelState::Active,
                             TunnelState::Backoff, TunnelState::Failed}) {
        AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(state, TunnelSignal::Stop)),
                             static_cast<int>(TunnelState::Stopped));
    }
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::next_state(TunnelState::Stopped,
                                                           TunnelSignal::DialSucceeded)),
                         static_cast<int>(TunnelState::Stopped));

    AURORA_TEST_CHECK_TRUE(conn::state_is_running(TunnelState::Backoff));
    AURORA_TEST_CHECK_FALSE(conn::state_is_running(TunnelState::Failed));
    AURORA_TEST_CHECK_FALSE(conn::state_is_running(TunnelState::Stopped));
}

AURORA_TEST_CASE(retry_backoff_doubles_and_clamps) {
    conn::RetryPolicy policy;
    policy.base_ms = 1000;
    policy.cap_ms = 30000;
    AURORA_TEST_CHECK_EQ(conn::retry_delay_ms(1, policy), 1000);
    AURORA_TEST_CHECK_EQ(conn::retry_delay_ms(2, policy), 2000);
    AURORA_TEST_CHECK_EQ(conn::retry_delay_ms(5, policy), 16000);
    AURORA_TEST_CHECK_EQ(conn::retry_delay_ms(6, policy), 30000);  // 钳住
    AURORA_TEST_CHECK_EQ(conn::retry_delay_ms(64, policy), 30000); // 大指数不溢出
    AURORA_TEST_CHECK_EQ(conn::retry_delay_ms(0, policy), 1000);   // 非法尝试号按 1

    AURORA_TEST_CHECK_TRUE(conn::should_retry(9999, policy));  // max_attempts==0 不限
    policy.max_attempts = 3;
    AURORA_TEST_CHECK_TRUE(conn::should_retry(2, policy));
    AURORA_TEST_CHECK_FALSE(conn::should_retry(3, policy));
}

AURORA_TEST_CASE(spec_default_fields_reproduce_the_pre_ruling_behavior) {
    // 裁决 7.97 D8②/D9 的两枚新成员：缺省值必须逐位复现改判前的旧行为，
    // 否则旧配置文件（无这两键）读回就与「新建一条」不相等。
    auto spec = local_spec();
    AURORA_TEST_CHECK_FALSE(spec.autostart);
    AURORA_TEST_CHECK_TRUE(spec.retry == conn::RetryPolicy{});
    AURORA_TEST_CHECK_EQ(conn::retry_delay_ms(3, spec.retry), 4000);
    AURORA_TEST_CHECK_TRUE(conn::should_retry(9999, spec.retry));  // 旧全局档＝不限次

    // operator== 覆盖新字段：行表判「这条定义改没改」靠它。
    auto autostarted = local_spec();
    autostarted.autostart = true;
    AURORA_TEST_CHECK_FALSE(spec == autostarted);
    auto capped = local_spec();
    capped.retry.max_attempts = 5;
    AURORA_TEST_CHECK_FALSE(spec == capped);
}

AURORA_TEST_CASE(signal_for_error_splits_fatal_from_recoverable) {
    using conn::TunnelError;
    using conn::TunnelSignal;
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::signal_for_error(TunnelError::DialFailed)),
                         static_cast<int>(TunnelSignal::RecoverableFailure));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::signal_for_error(TunnelError::ChannelLost)),
                         static_cast<int>(TunnelSignal::RecoverableFailure));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::signal_for_error(TunnelError::BindFailed)),
                         static_cast<int>(TunnelSignal::FatalFailure));
    AURORA_TEST_CHECK_EQ(static_cast<int>(conn::signal_for_error(TunnelError::RemoteRefused)),
                         static_cast<int>(TunnelSignal::FatalFailure));
}

}  // namespace borealis::test_cases::utest_tunnel_model
