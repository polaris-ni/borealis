/// 测试类型: integration（POSIX 腿）
/// 目标单元: src/conn/ssh_shared.{h,cpp} + src/conn/ssh_connection.{h,cpp}
/// 测试说明: `SPEC.FEAT.CONN.02` 连接复用（多标签共享一条 TCP）的**真协议**端到端证人。
///           对端是 `tests/support/fake_ssh_server`（libssh server 腿：真 KEX/加密/通道
///           多路复用，一条连接上可有多条 session 通道）。证十件事：
///           ① 同档案同秘密的两个标签只拨一条 TCP、两条 session 通道，双方都收到 banner；
///           ② 两条通道互不串流（各自只见到自己的回显）；
///           ③ 每个标签的 PTY 尺寸各记各的，只改一方不影响另一方；
///           ④ 关一个标签不关链路与另一个标签；
///           ⑤ 链路丢失只重拨一次，两条通道一起恢复并继续可用；
///           ⑥ 远端结束只收该标签（两条都收 RemoteExit、不重拨）；
///           ⑦ 秘密材料不同不共享（第二个标签自己拨号并落 AuthFailed）；
///           ⑧ agent 转发开关不同不共享；
///           ⑨ `stop_reconnect` 是连接级：一份租约触发，两份都落 UserStopped；
///           ⑩ 收口后的共享会话不被复用（第三个标签拿到新 TCP）。
///
///           无 Win32 腿：假服务器用 POSIX select/getsockname/shutdown（文件名 `_posix_`
///           让 cmake/BorealisTests.cmake 在 Windows 构建里排除本套件）。

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1① 的先例）：传输腿与共享表都在 src/ 树内。
#include "conn/reconnect.h"
#include "conn/ssh_connection.h"
#include "conn/ssh_dial.h"
#include "conn/ssh_shared.h"
#include "support/fake_ssh_server.h"

namespace borealis::test_cases::itest_posix_ssh_shared_connection {

namespace {

using borealis::conn::RetryPolicy;
using borealis::conn::SshAlgorithmOverrides;
using borealis::conn::SshConnection;
using borealis::conn::SshSecretBundle;
using borealis::conn::SshSessionRegistry;
using borealis::session::CloseReason;
using borealis::session::ConnectionEvents;
using borealis::session::ReconnectProgress;
using borealis::session::ReconnectStop;
using borealis::session::Size;
using borealis::testing::FakeSshServer;
using borealis::testing::FakeSshServerOptions;
using borealis::testing::fake_ssh_profile;
using borealis::testing::wait_until;

/// @brief 短退避档：掉线到重拨之间只等 50 ms。
constexpr RetryPolicy kFastPolicy{.base_ms = 50, .cap_ms = 100, .max_attempts = 3};

/// @brief 慢退避档：给「退避中按停止重连」留出足够的动作窗口。
constexpr RetryPolicy kSlowPolicy{.base_ms = 1500, .cap_ms = 1500, .max_attempts = 3};

[[nodiscard]] auto bytes_of(std::string_view text) -> std::vector<std::byte> {
    auto out = std::vector<std::byte>{};
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 连接事件的记录替身（口径同 `itest_posix_ssh_connection`）。
///
/// **声明顺序是硬约束**：替身必须比连接活得久——I/O 线程在 `close()` 的 ack 之前
/// 还会投终态回调，连接持的是这里的裸指针；用例里先写 Recorder 再写连接。
class Recorder final : public ConnectionEvents {
  public:
    auto on_bytes(std::span<const std::byte> bytes) -> void override {
        const std::lock_guard lock{mutex_};
        for (const auto byte : bytes) {
            received_.push_back(static_cast<char>(static_cast<unsigned char>(byte)));
        }
    }

    auto on_closed() -> void override { record_close(CloseReason::Unknown); }

    auto on_closed(CloseReason reason) -> void override { record_close(reason); }

    auto on_reconnect_progress(const ReconnectProgress &progress) -> void override {
        const std::lock_guard lock{mutex_};
        progresses_.push_back(progress);
    }

    [[nodiscard]] auto text() -> std::string {
        const std::lock_guard lock{mutex_};
        return received_;
    }

    [[nodiscard]] auto closes() -> std::size_t {
        const std::lock_guard lock{mutex_};
        return reasons_.size();
    }

    [[nodiscard]] auto last_close_reason() -> CloseReason {
        const std::lock_guard lock{mutex_};
        return reasons_.empty() ? CloseReason::Unknown : reasons_.back();
    }

    [[nodiscard]] auto saw_redial() -> bool {
        const std::lock_guard lock{mutex_};
        for (const auto &progress : progresses_) {
            if (progress.attempt >= 1 && progress.stop == ReconnectStop::None) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto last_progress() -> ReconnectProgress {
        const std::lock_guard lock{mutex_};
        return progresses_.empty() ? ReconnectProgress{} : progresses_.back();
    }

  private:
    auto record_close(CloseReason reason) -> void {
        const std::lock_guard lock{mutex_};
        reasons_.push_back(reason);
    }

    std::mutex mutex_;
    std::string received_{};
    std::vector<CloseReason> reasons_{};
    std::vector<ReconnectProgress> progresses_{};
};

/// @brief 建一个挂在共享表上的标签（尺寸与秘密可按用例给）。
[[nodiscard]] auto make_tab(const FakeSshServer &server,
                            const std::shared_ptr<SshSessionRegistry> &registry, Size size,
                            std::optional<std::string> secret = std::string{"secret"},
                            const RetryPolicy &policy = kFastPolicy,
                            bool agent_forwarding = false) -> std::unique_ptr<SshConnection> {
    auto profile = fake_ssh_profile(server.port());
    profile.agent_forwarding = agent_forwarding;
    return std::make_unique<SshConnection>(profile, SshSecretBundle{std::move(secret)}, size,
                                           policy,
                                           SshAlgorithmOverrides{}, registry);
}

/// @brief 连上并等到 shell 就绪（含「共享表里已有会话」的挂接路径）。
[[nodiscard]] auto wait_shell(FakeSshServer &server, SshConnection &connection,
                              std::size_t expected) -> bool {
    return wait_until(
        [&] { return server.shell_requests() >= expected && connection.alive(); }, 8000);
}

}  // namespace

AURORA_TEST_CASE(two_tabs_share_one_tcp_and_one_channel_each) {
    auto options = FakeSshServerOptions{};
    options.banner = "welcome\r\n";
    FakeSshServer server{options};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    Recorder second;
    auto first_tab = make_tab(server, registry, Size{.columns = 100, .rows = 30});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");

    auto second_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *second_tab, 2), "second shell never came up");

    // 判据 ①：一条 TCP、两条 session 通道，双方各自收到 banner。
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(server.session_channels(), std::size_t{2});
    AURORA_TEST_CHECK_TRUE(first_tab->alive());
    AURORA_TEST_CHECK_TRUE(second_tab->alive());
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.text() == "welcome\r\n"; }, 3000),
                            first.text());
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return second.text() == "welcome\r\n"; }, 3000),
                            second.text());

    first_tab->close();
    second_tab->close();
}

AURORA_TEST_CASE(channels_do_not_cross_talk) {
    auto options = FakeSshServerOptions{};
    options.banner = "hello\r\n";
    FakeSshServer server{options};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    Recorder second;
    auto first_tab = make_tab(server, registry, Size{.columns = 100, .rows = 30});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");
    auto second_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *second_tab, 2), "second shell never came up");
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return second.text() == "hello\r\n"; }, 3000),
                            second.text());

    // 判据 ②：写进各自的通道，回显只回各自的记录器。
    first_tab->write(bytes_of("aaa"));
    second_tab->write(bytes_of("bbb"));
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return server.received_text().find("aaa") != std::string::npos; }, 3000),
        server.received_text());
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return server.received_text().find("bbb") != std::string::npos; }, 3000),
        server.received_text());
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.text() == "hello\r\naaa"; }, 3000),
                            first.text());
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return second.text() == "hello\r\nbbb"; }, 3000),
                            second.text());

    first_tab->close();
    second_tab->close();
}

AURORA_TEST_CASE(each_tab_keeps_its_own_pty_size) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    Recorder second;
    auto first_tab = make_tab(server, registry, Size{.columns = 100, .rows = 30});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");
    auto second_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *second_tab, 2), "second shell never came up");

    // 判据 ③：两条通道各记各的首尺寸（按下标＝打开次序）。
    AURORA_TEST_CHECK_EQ(server.pty_size_at(0).first, 100);
    AURORA_TEST_CHECK_EQ(server.pty_size_at(0).second, 30);
    AURORA_TEST_CHECK_EQ(server.pty_size_at(1).first, 80);
    AURORA_TEST_CHECK_EQ(server.pty_size_at(1).second, 24);

    // 只 resize 第一条：第二条的尺寸必须原样不动。
    first_tab->resize(Size{.columns = 120, .rows = 40});
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return server.pty_size_at(0) == std::pair<int, int>{120, 40}; }, 3000),
        "window-change did not reach the first channel");
    AURORA_TEST_CHECK_EQ(server.pty_size_at(1).first, 80);
    AURORA_TEST_CHECK_EQ(server.pty_size_at(1).second, 24);
    AURORA_TEST_CHECK_EQ(server.session_channels(), std::size_t{2});

    first_tab->close();
    second_tab->close();
}

AURORA_TEST_CASE(closing_one_tab_keeps_the_link_and_the_other_tab) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    Recorder second;
    auto first_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");
    auto second_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *second_tab, 2), "second shell never came up");

    // 判据 ④：关第一个标签只收这一条通道，链路与第二个标签继续。
    first_tab->close();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.closes() >= 1; }, 3000),
                            "first close was not reported");
    AURORA_TEST_CHECK(first.last_close_reason() == CloseReason::LocalClose);
    AURORA_TEST_CHECK_FALSE(first_tab->alive());
    AURORA_TEST_CHECK_TRUE(second_tab->alive());

    second_tab->write(bytes_of("still-here"));
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return second.text() == "still-here"; }, 3000), second.text());
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(server.session_channels(), std::size_t{2});

    second_tab->close();
}

AURORA_TEST_CASE(link_loss_redials_once_and_both_tabs_recover) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    Recorder second;
    auto first_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");
    auto second_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *second_tab, 2), "second shell never came up");

    server.drop_connections();
    // 判据 ⑤：两份租约都看到退避档（进度是连接级广播，不是只投第一份）。
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.saw_redial(); }, 5000),
                            "first tab saw no backoff step");
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return second.saw_redial(); }, 5000),
                            "second tab saw no backoff step");
    // 只重拨一条 TCP：两条通道一起回来。
    AURORA_TEST_REQUIRE_MSG(
        wait_until(
            [&] {
                return server.accepted_connections() == 2 && server.session_channels() >= 4 &&
                       first_tab->alive() && second_tab->alive();
            },
            8000),
        "redial did not restore both shells");

    first_tab->write(bytes_of("again-a"));
    second_tab->write(bytes_of("again-b"));
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return first.text() == "again-a"; }, 3000), first.text());
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return second.text() == "again-b"; }, 3000), second.text());

    first_tab->close();
    second_tab->close();
}

AURORA_TEST_CASE(remote_eof_finishes_both_tabs_without_redial) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    Recorder second;
    auto first_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");
    auto second_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *second_tab, 2), "second shell never came up");

    // 判据 ⑥：远端正常结束不是断线——两条都归 RemoteExit，不进退避环。
    server.end_shell();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.closes() >= 1; }, 5000),
                            "first remote eof was not reported");
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return second.closes() >= 1; }, 5000),
                            "second remote eof was not reported");
    AURORA_TEST_CHECK(first.last_close_reason() == CloseReason::RemoteExit);
    AURORA_TEST_CHECK(second.last_close_reason() == CloseReason::RemoteExit);
    AURORA_TEST_CHECK_FALSE(first.saw_redial());
    AURORA_TEST_CHECK_FALSE(second.saw_redial());
    AURORA_TEST_CHECK(first.last_progress().stop == ReconnectStop::RemoteExit);
    AURORA_TEST_CHECK(second.last_progress().stop == ReconnectStop::RemoteExit);
    static_cast<void>(wait_until([&] { return server.accepted_connections() > 1; }, 300));
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{1});
}

AURORA_TEST_CASE(different_secret_keeps_a_separate_connection) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    Recorder second;
    auto first_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");

    // 判据 ⑦：口令不同绝不共享——第二个标签自己拨号，被服务器拒，不影响第一个。
    auto second_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24},
                               std::string{"wrong"});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return second.closes() >= 1; }, 8000),
                            "second tab never reported a close");
    AURORA_TEST_CHECK(second.last_close_reason() == CloseReason::AuthFailed);
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{2});
    AURORA_TEST_CHECK_TRUE(first_tab->alive());

    first_tab->write(bytes_of("unaffected"));
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return first.text() == "unaffected"; }, 3000), first.text());
    first_tab->close();
    second_tab->close();
}

AURORA_TEST_CASE(agent_forwarding_flag_keeps_a_separate_connection) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder plain;
    Recorder forwarding;
    auto plain_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    plain_tab->start(plain);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *plain_tab, 1), "plain shell never came up");

    // 判据 ⑧：转发开关入共享键——借一条没开转发的链路等于静默关掉它，故必须各自拨号。
    auto forwarding_tab =
        make_tab(server, registry, Size{.columns = 80, .rows = 24}, std::string{"secret"},
                 kFastPolicy, true);
    forwarding_tab->start(forwarding);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *forwarding_tab, 2),
                            "forwarding shell never came up");
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{2});
    AURORA_TEST_CHECK_TRUE(plain_tab->alive());
    AURORA_TEST_CHECK_TRUE(forwarding_tab->alive());

    plain_tab->close();
    forwarding_tab->close();
}

AURORA_TEST_CASE(stop_reconnect_is_connection_level) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    Recorder second;
    auto first_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24},
                              std::string{"secret"}, kSlowPolicy);
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");
    auto second_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24},
                               std::string{"secret"}, kSlowPolicy);
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *second_tab, 2), "second shell never came up");

    server.drop_connections();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.saw_redial(); }, 5000),
                            "no backoff step was reported");

    // 判据 ⑨：任一份租约按「停止重连」⇒ 整条环落 UserStopped，两份都收终态。
    first_tab->stop_reconnect();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.closes() >= 1; }, 5000),
                            "first tab never reached a terminal state");
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return second.closes() >= 1; }, 5000),
                            "second tab never reached a terminal state");
    AURORA_TEST_CHECK(first.last_progress().stop == ReconnectStop::UserStopped);
    AURORA_TEST_CHECK(second.last_progress().stop == ReconnectStop::UserStopped);
    AURORA_TEST_CHECK(first.last_close_reason() == CloseReason::LinkLost);
    AURORA_TEST_CHECK(second.last_close_reason() == CloseReason::LinkLost);
    static_cast<void>(wait_until([&] { return server.accepted_connections() > 1; }, 2000));
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{1});

    first_tab->close();
    second_tab->close();
}

AURORA_TEST_CASE(a_finished_shared_session_is_not_reused) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");
    auto registry = std::make_shared<SshSessionRegistry>();

    Recorder first;
    auto first_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *first_tab, 1), "first shell never came up");
    first_tab->close();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.closes() >= 1; }, 3000),
                            "close was not reported");

    // 判据 ⑩：收口后的会话不再借出——第三个标签拿到一条新 TCP。
    Recorder third;
    auto third_tab = make_tab(server, registry, Size{.columns = 80, .rows = 24});
    third_tab->start(third);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, *third_tab, 2), "third shell never came up");
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(server.session_channels(), std::size_t{2});
    third_tab->close();
}

}  // namespace borealis::test_cases::itest_posix_ssh_shared_connection
