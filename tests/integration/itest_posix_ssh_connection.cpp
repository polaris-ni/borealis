/// 测试类型: integration（POSIX 腿）
/// 目标单元: src/conn/ssh_connection.{h,cpp} + src/conn/ssh_dial.{h,cpp}
/// 测试说明: `SPEC.FEAT.CONN.02` 终端腿的**真协议**端到端证人。本沙箱没有 sshd，
///           故对端是 `tests/support/fake_ssh_server`（libssh server 腿，真 KEX/加密/
///           通道多路复用，loopback 临时端口 + 一次性主机密钥）。证的五件事：
///           ① password 认证 + session 通道 + PTY 首尺寸 + shell 请求真的到了对端
///              （判据 1/2 的线上现场，含档案里的初始尺寸 100x30）；
///           ② 会话读线程把对端 banner 投成 on_bytes，写出方向逐字节到达对端并被回显
///              （判据 3 的往返）；
///           ③ resize() 落到对端是 PTY window-change（判据 8 的现场版，120x40）；
///           ④ 认证失败归 AuthFailed 且**不**重拨（分类表：重拨只会再拒一回）；
///           ⑤ 通道 EOF 归 RemoteExit 且不重拨；链路丢失归 LinkLost 并按退避档在
///              **同一端口**重拨出第二条连接（判据 7 的两态与 `SPEC.FEAT.WS.05` 的环）；
///           ⑥ agent 转发：客户端发出 `auth-agent-req`，服务端反向开一条
///              `auth-agent@openssh.com` 通道，字节经本机 `SSH_AUTH_SOCK` 替身往返
///              （`SPEC.FEAT.CONN.02` 裁决 7.145⑦ 的真协议证人）。
///
///           无 Win32 腿：假服务器用 POSIX select/getsockname/shutdown（文件名 `_posix_`
///           让 cmake/BorealisTests.cmake 在 Windows 构建里排除本套件）。

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1① 的先例）：终端 SSH 腿在 src/ 树内。
#include "conn/ssh_connection.h"
#include "support/fake_ssh_server.h"

namespace borealis::test_cases::itest_posix_ssh_connection {

namespace {

using borealis::conn::RetryPolicy;
using borealis::conn::SshConnection;
using borealis::session::CloseReason;
using borealis::session::ConnectionEvents;
using borealis::session::ReconnectProgress;
using borealis::session::ReconnectStop;
using borealis::session::Size;
using borealis::testing::FakeSshServer;
using borealis::testing::FakeSshServerOptions;
using borealis::testing::fake_ssh_profile;
using borealis::testing::wait_until;

/// @brief 短退避档：掉线到重拨之间只等 50 ms，用例不必睡满缺省的一秒以上。
constexpr RetryPolicy kFastPolicy{.base_ms = 50, .cap_ms = 100, .max_attempts = 3};

[[nodiscard]] auto bytes_of(std::string_view text) -> std::vector<std::byte> {
    auto out = std::vector<std::byte>{};
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 连接事件的记录替身（口径同 Telnet 与串口两棒）。
///
/// **声明顺序是硬约束**：替身必须比连接活得久——读线程在 `close()` 的 join 之前还会投
/// `on_closed`／进度，连接持的是这里的裸指针；用例里先写 Recorder 再写连接。
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

    /// @brief 是否出现过「环里的一档」（attempt≥1 且未停）。
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

/// @brief 连上并等到 shell 就绪（四条用例共用的前置）。
[[nodiscard]] auto wait_shell(FakeSshServer &server, SshConnection &connection) -> bool {
    return wait_until(
        [&] { return server.shell_requests() >= 1 && connection.alive(); }, 8000);
}

/// @brief 在作用域内改写 `SSH_AUTH_SOCK`，析构时原样归还（口径同 `utest_posix_agent_socket`）。
class ScopedAuthSock {
  public:
    explicit ScopedAuthSock(const char *value) {
        const auto *previous = std::getenv("SSH_AUTH_SOCK");
        had_previous_ = previous != nullptr;
        if (had_previous_) {
            previous_ = previous;
        }
        if (value == nullptr) {
            static_cast<void>(::unsetenv("SSH_AUTH_SOCK"));
        } else {
            static_cast<void>(::setenv("SSH_AUTH_SOCK", value, 1));
        }
    }

    ~ScopedAuthSock() {
        if (had_previous_) {
            static_cast<void>(::setenv("SSH_AUTH_SOCK", previous_.c_str(), 1));
        } else {
            static_cast<void>(::unsetenv("SSH_AUTH_SOCK"));
        }
    }

    ScopedAuthSock(const ScopedAuthSock &) = delete;
    auto operator=(const ScopedAuthSock &) -> ScopedAuthSock & = delete;

  private:
    bool had_previous_{false};
    std::string previous_;
};

/// @brief 可接受多轮的替身 agent：每轮「收一块、原样回一块、关」，再回去等下一轮。
///
/// 必须多轮：客户端先连一次探活（立刻关），再为反向通道连一次真正中继。
struct MultiEchoAgent {
    int listener{-1};
    std::string path;
    std::atomic<bool> stopping{false};
    std::atomic<int> served{0};
    std::thread worker;

    MultiEchoAgent() = default;
    ~MultiEchoAgent() {
        stopping.store(true);
        if (worker.joinable()) {
            worker.join();  // 每轮 accept/read 都按 50 ms 分片查停止位。
        }
        if (listener >= 0) {
            ::close(listener);
        }
        if (!path.empty()) {
            static_cast<void>(::unlink(path.c_str()));
        }
    }

    MultiEchoAgent(const MultiEchoAgent &) = delete;
    auto operator=(const MultiEchoAgent &) -> MultiEchoAgent & = delete;
};

[[nodiscard]] auto start_multi_echo_agent(MultiEchoAgent &agent, const std::string &path) -> bool {
    agent.path = path;
    agent.listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (agent.listener < 0) {
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    static_cast<void>(::unlink(path.c_str()));
    if (::bind(agent.listener, reinterpret_cast<const sockaddr *>(&address),
               static_cast<socklen_t>(sizeof(address))) != 0) {
        return false;
    }
    if (::listen(agent.listener, 4) != 0) {
        return false;
    }
    agent.worker = std::thread([&agent] {
        while (!agent.stopping.load()) {
            auto listener_ready = pollfd{agent.listener, POLLIN, 0};
            if (::poll(&listener_ready, 1, 50) <= 0) {
                continue;
            }
            const int peer = ::accept(agent.listener, nullptr, nullptr);
            if (peer < 0) {
                continue;
            }
            agent.served.fetch_add(1);
            auto buffer = std::array<std::uint8_t, 64>{};
            while (!agent.stopping.load()) {
                auto peer_ready = pollfd{peer, POLLIN, 0};
                if (::poll(&peer_ready, 1, 50) <= 0) {
                    continue;
                }
                const auto got = ::read(peer, buffer.data(), buffer.size());
                if (got <= 0) {
                    break;
                }
                static_cast<void>(::write(peer, buffer.data(), static_cast<std::size_t>(got)));
            }
            ::close(peer);
        }
    });
    return true;
}

[[nodiscard]] auto agent_sock_path() -> std::string {
    return "/tmp/borealis-itest-agent-" + std::to_string(static_cast<long>(::getpid())) + ".sock";
}

}  // namespace

AURORA_TEST_CASE(connect_lands_pty_shell_banner_and_the_echo_round_trip) {
    auto options = FakeSshServerOptions{};
    options.banner = "welcome\r\n";
    FakeSshServer server{options};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");

    Recorder recorder;
    SshConnection connection{fake_ssh_profile(server.port()), std::string{"secret"},
                             Size{.columns = 100, .rows = 30}, kFastPolicy};
    connection.start(recorder);

    AURORA_TEST_REQUIRE_MSG(wait_shell(server, connection), "shell never came up");
    // 判据 1/2 的线上现场：一次拨号、认证走过、一条 session 通道、PTY 与初始尺寸原样落地。
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_TRUE(server.auth_attempts() >= 1);
    AURORA_TEST_CHECK_EQ(server.session_channels(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(server.pty_requests(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(server.last_pty_columns(), 100);
    AURORA_TEST_CHECK_EQ(server.last_pty_rows(), 30);
    // 对端 banner 经读线程投上来（on_bytes 的入方向证人）。
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return recorder.text() == "welcome\r\n"; }, 3000),
                            recorder.text());

    // 出方向：会话写出的字节逐字到达对端，并被对端原样回显。
    const auto payload = bytes_of("hello");
    connection.write(payload);
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return server.received_text() == "hello"; }, 3000),
                            server.received_text());
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return recorder.text() == "welcome\r\nhello"; }, 3000),
        recorder.text());

    // 判据 8 的现场版：resize 落到对端是 PTY window-change（不是重开通道）。
    connection.resize(Size{.columns = 120, .rows = 40});
    AURORA_TEST_REQUIRE_MSG(
        wait_until(
            [&] { return server.last_pty_columns() == 120 && server.last_pty_rows() == 40; },
            3000),
        "window-change did not reach the server");
    AURORA_TEST_CHECK_EQ(server.session_channels(), std::size_t{1});  // 同一通道上改尺寸。

    connection.close();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return recorder.closes() >= 1; }, 3000),
                            "close was not reported");
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::LocalClose);
    AURORA_TEST_CHECK_FALSE(connection.alive());
}

AURORA_TEST_CASE(a_wrong_password_lands_auth_failed_and_does_not_redial) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");

    Recorder recorder;
    SshConnection connection{fake_ssh_profile(server.port()), std::string{"wrong"},
                             Size{.columns = 80, .rows = 24}, kFastPolicy};
    connection.start(recorder);

    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return recorder.closes() >= 1; }, 8000),
                            "auth failure was not reported");
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::AuthFailed);
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(server.shell_requests(), std::size_t{0});
    // 认证失败属不可重拨档：再等两个退避档也不该出现第二条连接。
    static_cast<void>(wait_until([&] { return server.accepted_connections() > 1; }, 300));
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_FALSE(recorder.saw_redial());
    AURORA_TEST_CHECK(recorder.last_progress().stop == ReconnectStop::NotReconnectable);
}

AURORA_TEST_CASE(channel_eof_is_remote_exit_and_not_redialed) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");

    Recorder recorder;
    SshConnection connection{fake_ssh_profile(server.port()), std::string{"secret"},
                             Size{.columns = 80, .rows = 24}, kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, connection), "shell never came up");

    server.end_shell();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return recorder.closes() >= 1; }, 5000),
                            "remote eof was not reported");
    // 判据 7：远端正常结束不是断线——归 RemoteExit，不进重拨环。
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::RemoteExit);
    AURORA_TEST_CHECK_FALSE(recorder.saw_redial());
    static_cast<void>(wait_until([&] { return server.accepted_connections() > 1; }, 300));
    AURORA_TEST_CHECK_EQ(server.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK(recorder.last_progress().stop == ReconnectStop::RemoteExit);
}

AURORA_TEST_CASE(link_loss_redials_the_same_endpoint_and_gets_a_new_shell) {
    FakeSshServer server{};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");

    Recorder recorder;
    SshConnection connection{fake_ssh_profile(server.port()), std::string{"secret"},
                             Size{.columns = 80, .rows = 24}, kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, connection), "shell never came up");

    // 链路丢失：对端把 TCP 双向掐掉，客户端读线程应归 LinkLost 并进退避环。
    server.drop_connections();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return recorder.saw_redial(); }, 5000),
                            "no backoff step was reported");
    // 退避环真的在同一端口拨出了第二条连接，且第二条连接也走完了 PTY+shell。
    AURORA_TEST_REQUIRE_MSG(
        wait_until(
            [&] {
                return server.accepted_connections() >= 2 && server.shell_requests() >= 2 &&
                       connection.alive();
            },
            8000),
        "redial did not reach a new shell");
    AURORA_TEST_CHECK_TRUE(server.auth_attempts() >= 2);
    AURORA_TEST_CHECK_EQ(server.session_channels(), std::size_t{2});

    // 重拨成功后环擦干净：再写一笔仍走新通道。
    const auto payload = bytes_of("again");
    connection.write(payload);
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return server.received_text() == "again"; }, 3000),
                            server.received_text());

    connection.close();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return recorder.closes() >= 1; }, 3000),
                            "close was not reported");
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::LocalClose);
}

AURORA_TEST_CASE(agent_forwarding_relays_a_reverse_channel_to_the_local_agent) {
    auto options = FakeSshServerOptions{};
    options.open_agent_channel = true;
    options.agent_payload = "sign-request";
    FakeSshServer server{options};
    AURORA_TEST_REQUIRE_MSG(server.ok(), "fake ssh server could not listen");

    // 本机 agent 替身：先吃客户端那一次探活连接，再为中继连接回显字节。
    MultiEchoAgent agent;
    const auto path = agent_sock_path();
    AURORA_TEST_REQUIRE_MSG(start_multi_echo_agent(agent, path),
                            "cannot create the fake agent socket");
    const ScopedAuthSock env{path.c_str()};

    auto profile = fake_ssh_profile(server.port());
    profile.agent_forwarding = true;

    Recorder recorder;
    SshConnection connection{profile, std::string{"secret"}, Size{.columns = 80, .rows = 24},
                             kFastPolicy};
    connection.start(recorder);
    AURORA_TEST_REQUIRE_MSG(wait_shell(server, connection), "shell never came up");

    // 客户端把请求发到了对端（auth-agent-req@openssh.com 的线上现场）。
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return server.agent_forward_requests() >= 1; }, 5000),
        "client never requested agent forwarding");
    // 对端反向开了一条 auth-agent 通道，且客户端接受了它（否则通道数停在 0）。
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return server.agent_channels() >= 1; }, 5000),
        "server could not open the reverse agent channel");
    // 字节走完一整圈：对端 → 反向通道 → 本机 agent → 回显 → 反向通道 → 对端。
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return server.agent_received_text() == "sign-request"; }, 5000),
        server.agent_received_text());
    // 探活一轮 + 中继一轮都进了替身 agent（证明客户端确实连的是这个 socket）。
    AURORA_TEST_CHECK_TRUE(agent.served.load() >= 2);

    connection.close();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return recorder.closes() >= 1; }, 3000),
                            "close was not reported");
    AURORA_TEST_CHECK(recorder.last_close_reason() == CloseReason::LocalClose);
}

}  // namespace borealis::test_cases::itest_posix_ssh_connection
