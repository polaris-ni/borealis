/// 测试类型: unit
/// 目标单元: src/conn/reconnect.h（私有头）纯逻辑裁决函数
/// 测试说明: 守住裁决 7.99 的三张表——D2① 的「哪些关闭原因值得自动重拨」分类、
///           D4① 的退避档位与用尽判定（含「重拨回次」与模型「第 N 次失败即止」那格
///           换算）、D7④ 改判新增的 SFTP 掉线判据。真实拨号与真网络掉线属网络 I/O，
///           不在无头单测范围（沙箱无 sshd，待补 etest）。

#include "conn/reconnect.h"

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_reconnect {

namespace {

using conn::RetryPolicy;

[[nodiscard]] constexpr auto cast(auto value) -> int { return static_cast<int>(value); }

}  // namespace

AURORA_TEST_CASE(reconnectable_set_is_exactly_link_loss_and_dial_network) {
    // 可重连集：Active 期间掉线 + 建连阶段的网络类失败。
    AURORA_TEST_CHECK_TRUE(conn::should_reconnect(session::CloseReason::LinkLost));
    AURORA_TEST_CHECK_TRUE(conn::should_reconnect(session::CloseReason::DialNetwork));

    // 不可重连集：认证与主机密钥两档是**安全裁决**不是效率取舍——重拨只会重复失败，
    // 且对认证失败重试可能触发远端账号锁定（不破裁决 7.94④ 的保守取向）。
    AURORA_TEST_CHECK_FALSE(conn::should_reconnect(session::CloseReason::AuthFailed));
    AURORA_TEST_CHECK_FALSE(conn::should_reconnect(session::CloseReason::HostKeyRejected));
    // 远端 shell 正常结束不是断线；本端主动关闭更不该被重开。
    AURORA_TEST_CHECK_FALSE(conn::should_reconnect(session::CloseReason::RemoteExit));
    AURORA_TEST_CHECK_FALSE(conn::should_reconnect(session::CloseReason::LocalClose));
    // 归不出类的失败保守按不重拨。
    AURORA_TEST_CHECK_FALSE(conn::should_reconnect(session::CloseReason::Unknown));
}

AURORA_TEST_CASE(terminal_buckets_split_by_reason_then_exhaustion) {
    // 屏 B 三档：次数用尽 / 不可自动重连 / 远端退出（非断线）。
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::LinkLost, true)),
                         cast(conn::ReconnectStop::AttemptsExhausted));
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::DialNetwork, true)),
                         cast(conn::ReconnectStop::AttemptsExhausted));
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::AuthFailed, false)),
                         cast(conn::ReconnectStop::NotReconnectable));
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::HostKeyRejected, false)),
                         cast(conn::ReconnectStop::NotReconnectable));
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::Unknown, false)),
                         cast(conn::ReconnectStop::NotReconnectable));
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::RemoteExit, false)),
                         cast(conn::ReconnectStop::RemoteExit));
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::LocalClose, false)),
                         cast(conn::ReconnectStop::UserStopped));

    // 可重拨的原因停在终态只有两条路：用尽，或用户按「停止重连」让环自己落终态。
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::LinkLost, false)),
                         cast(conn::ReconnectStop::UserStopped));
    // 不可重连的原因：用没用尽都不改档（不静默重试这条不靠次数兜底）。
    AURORA_TEST_CHECK_EQ(cast(conn::reconnect_stop(session::CloseReason::AuthFailed, true)),
                         cast(conn::ReconnectStop::NotReconnectable));
}

AURORA_TEST_CASE(default_policy_redials_three_times_then_exhausts) {
    const auto policy = conn::default_reconnect_policy();

    // 缺省档 3＝重拨 3 回，退避 1 s→2 s→4 s，第 4 回不再拨（草图屏 A/B 的读数）。
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(1, policy).value_or(-1), 1000);
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(2, policy).value_or(-1), 2000);
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(3, policy).value_or(-1), 4000);
    AURORA_TEST_CHECK_FALSE(conn::reconnect_step(4, policy).has_value());

    // 序号下界钳到 1：0 或负数不会算出「第 0 次回拨」这种既不拨也不停的档位。
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(0, policy).value_or(-1), 1000);
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(-5, policy).value_or(-1), 1000);
}

AURORA_TEST_CASE(unlimited_policy_never_exhausts_and_doubling_clamps_at_cap) {
    const auto policy = RetryPolicy{.base_ms = 1000, .cap_ms = 30000, .max_attempts = 0};

    // 翻倍钳顶：第 6 回起就贴在 30 s 不动了。
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(5, policy).value_or(-1), 16000);
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(6, policy).value_or(-1), 30000);
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(7, policy).value_or(-1), 30000);
    // 不限次永不落终态，且极远序号不溢出（模型侧位移钳在 30 内）。
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(100000, policy).value_or(-1), 30000);
}

AURORA_TEST_CASE(custom_policy_fields_take_effect_per_call) {
    // 三键改档后现读现算：档位不缓存，下一次断线取的就是最新值（裁决 7.99 D4 的生效口径）。
    const auto tight = RetryPolicy{.base_ms = 500, .cap_ms = 1000, .max_attempts = 1};
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(1, tight).value_or(-1), 500);
    AURORA_TEST_CHECK_FALSE(conn::reconnect_step(2, tight).has_value());

    // 上限低于首次退避时按上限钳住，不会给出「比上限还长」的等待。
    const auto inverted = RetryPolicy{.base_ms = 5000, .cap_ms = 1000, .max_attempts = 3};
    AURORA_TEST_CHECK_EQ(conn::reconnect_step(1, inverted).value_or(-1), 1000);
}

AURORA_TEST_CASE(sftp_link_loss_excludes_server_and_local_failures) {
    // 掉线类：会话级失败与「会话已不在」。
    AURORA_TEST_CHECK_TRUE(conn::sftp_error_drops_link(conn::SftpError::Network));
    AURORA_TEST_CHECK_TRUE(conn::sftp_error_drops_link(conn::SftpError::NotConnected));

    // 非掉线类：重拨也解决不了，把它们当掉线会让面板对着没坏的会话反复重连。
    AURORA_TEST_CHECK_FALSE(conn::sftp_error_drops_link(conn::SftpError::None));
    AURORA_TEST_CHECK_FALSE(conn::sftp_error_drops_link(conn::SftpError::PermissionDenied));
    AURORA_TEST_CHECK_FALSE(conn::sftp_error_drops_link(conn::SftpError::NotFound));
    AURORA_TEST_CHECK_FALSE(conn::sftp_error_drops_link(conn::SftpError::LocalIo));
    AURORA_TEST_CHECK_FALSE(conn::sftp_error_drops_link(conn::SftpError::Protocol));
    // 取消更不是掉线：那是调用方自己要停。
    AURORA_TEST_CHECK_FALSE(conn::sftp_error_drops_link(conn::SftpError::Cancelled));
}

}  // namespace borealis::test_cases::utest_reconnect
