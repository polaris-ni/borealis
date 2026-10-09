/// 测试类型: integration
/// 目标单元: src/conn/tunnel_client.h 隧道传输腿（失败路径）
/// 测试说明: 沙箱无 sshd，真转发不可测；本用例走确定可判的失败腿——对
///           127.0.0.1:1 拨号必被 TCP 拒，守住「Dialing→Backoff→…→Failed」
///           事件序、退避重试计数、非法定义拒绝启动与 stop() 幂等。
///           真连成功腿（-L/-R/-D 转发）待 sshd 环境补 etest（裁决 7.96 登记）。

#include "conn/tunnel_client.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::itest_tunnel_failure {

namespace {

using conn::TunnelError;
using conn::TunnelState;

/// @brief 状态事件记录器（工作线程回调落这里，主线程快照读取）。
class Recorder final : public conn::TunnelEvents {
  public:
    auto on_tunnel_state(TunnelState state, TunnelError error, int bound_port) -> void override {
        static_cast<void>(bound_port);
        std::lock_guard<std::mutex> guard(mutex_);
        log_.push_back(state);
        if (error == TunnelError::DialFailed && state == TunnelState::Failed) {
            last_error_dial_failed_ = true;
        }
    }

    [[nodiscard]] auto snapshot() const -> std::vector<TunnelState> {
        std::lock_guard<std::mutex> guard(mutex_);
        return log_;
    }

    [[nodiscard]] auto saw_dial_failed_terminal() const -> bool {
        std::lock_guard<std::mutex> guard(mutex_);
        return last_error_dial_failed_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<TunnelState> log_;
    bool last_error_dial_failed_{false};
};

/// @brief 指向本机必拒端口（1）的 SSH 档案：TCP 层即刻 ECONNREFUSED。
[[nodiscard]] auto refused_profile() -> conn::SshProfile {
    auto profile = conn::SshProfile{};
    profile.host = "127.0.0.1";
    profile.port = 1;
    profile.user = "nobody";
    profile.auth_method = "password";
    return profile;
}

[[nodiscard]] auto local_spec(int listen_port) -> conn::TunnelSpec {
    auto spec = conn::TunnelSpec{};
    spec.id = "itest";
    spec.name = "itest";
    spec.kind = conn::TunnelKind::Local;
    spec.listen_port = listen_port;
    spec.target_host = "127.0.0.1";
    spec.target_port = 80;
    return spec;
}

template <typename Predicate>
[[nodiscard]] auto wait_until(const Predicate &predicate, int timeout_ms) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

}  // namespace

AURORA_TEST_CASE(dial_refused_retries_then_fails) {
    conn::RetryPolicy policy;
    policy.base_ms = 10;
    policy.cap_ms = 20;
    policy.max_attempts = 3;

    Recorder recorder;
    conn::Tunnel tunnel(refused_profile(), std::optional<std::string>{"wrong"}, local_spec(28080),
                        policy);
    tunnel.start(recorder);

    const auto reached = wait_until([&] { return tunnel.state() == TunnelState::Failed; }, 5000);
    AURORA_TEST_CHECK_TRUE(reached);
    AURORA_TEST_CHECK_TRUE(recorder.saw_dial_failed_terminal());

    const auto log = recorder.snapshot();
    AURORA_TEST_CHECK_TRUE(!log.empty());
    if (!log.empty()) {
        AURORA_TEST_CHECK_EQ(static_cast<int>(log.front()),
                             static_cast<int>(TunnelState::Dialing));
        AURORA_TEST_CHECK_EQ(static_cast<int>(log.back()),
                             static_cast<int>(TunnelState::Failed));
    }
    int backoffs = 0;
    for (const auto state : log) {
        if (state == TunnelState::Backoff) {
            ++backoffs;
        }
    }
    // max_attempts=3：前两次失败进 Backoff 重拨，第三次失败直接 Failed。
    AURORA_TEST_CHECK_EQ(backoffs, 2);

    tunnel.stop();  // 已终态：join 已结束的线程，幂等。
    tunnel.stop();
    AURORA_TEST_CHECK_EQ(static_cast<int>(tunnel.state()),
                         static_cast<int>(TunnelState::Failed));
}

AURORA_TEST_CASE(invalid_spec_refused_before_thread) {
    Recorder recorder;
    // Local 且监听端口 0：非法定义，start() 不得起线程，直接 Failed。
    conn::Tunnel tunnel(refused_profile(), std::nullopt, local_spec(0), conn::RetryPolicy{});
    tunnel.start(recorder);
    AURORA_TEST_CHECK_EQ(static_cast<int>(tunnel.state()),
                         static_cast<int>(TunnelState::Failed));
    const auto log = recorder.snapshot();
    AURORA_TEST_CHECK_EQ(log.size(), 1U);  // 只有 Failed 一枚，未起线程无 Dialing。
    if (!log.empty()) {
        AURORA_TEST_CHECK_EQ(static_cast<int>(log.front()),
                             static_cast<int>(TunnelState::Failed));
    }
    tunnel.stop();
}

}  // namespace borealis::test_cases::itest_tunnel_failure
