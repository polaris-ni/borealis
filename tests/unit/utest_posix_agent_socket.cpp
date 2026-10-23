/// 测试类型: unit
/// 目标单元: src/platform/posix/agent_socket.cpp + src/platform/agent_socket.h
/// 测试说明: `SPEC.FEAT.CONN.02` 的 agent 转发在本机一侧的字节管道（沙箱内可跑，不起任何
///           sshd）：环境变量缺失、路径过长或连不上都交 nullptr；连上后写进去的字节原样从
///           替身 agent 回来（中继不解析协议）。真 agent 的签署往返与远端反向通道要有 sshd
///           才能验，见裁决 7.145 的证人边界。
///
///           本文件只在非 Win32 编译（`cmake/BorealisTests.cmake` 按文件名把平台专属套件挡在
///           另一侧的构建面之外）；Win32 命名管道腿未经 MSVC 编译验证。

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "framework/aurora_test.h"
#include "platform/agent_socket.h"

namespace borealis::test_cases::utest_posix_agent_socket {

namespace {

using borealis::platform::AgentReadOutcome;

/// @brief 在作用域内改写 `SSH_AUTH_SOCK`，析构时原样归还（含「原本没有」那一档）。
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

/// @brief 一个只回显一次的替身 agent 监听点。
struct EchoAgent {
    int listener{-1};
    std::string path;
    std::thread worker;

    EchoAgent() = default;
    ~EchoAgent() {
        if (worker.joinable()) {
            worker.join();
        }
        if (listener >= 0) {
            ::close(listener);
        }
        if (!path.empty()) {
            static_cast<void>(::unlink(path.c_str()));
        }
    }

    EchoAgent(const EchoAgent &) = delete;
    auto operator=(const EchoAgent &) -> EchoAgent & = delete;
};

/// @brief 建监听点并起一条「收一块、原样回一块、然后关」的服务线程。
[[nodiscard]] auto start_echo_agent(EchoAgent &agent, const std::string &path) -> bool {
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
    if (::listen(agent.listener, 1) != 0) {
        return false;
    }
    agent.worker = std::thread([&agent] {
        const int peer = ::accept(agent.listener, nullptr, nullptr);
        if (peer < 0) {
            return;
        }
        std::array<std::uint8_t, 64> buffer{};
        const auto got = ::read(peer, buffer.data(), buffer.size());
        if (got > 0) {
            static_cast<void>(::write(peer, buffer.data(), static_cast<std::size_t>(got)));
        }
        ::close(peer);
    });
    return true;
}

[[nodiscard]] auto auth_sock_path() -> std::string {
    return "/tmp/borealis-agent-" + std::to_string(static_cast<long>(::getpid())) + ".sock";
}

}  // namespace

AURORA_TEST_CASE(missing_auth_sock_yields_no_connection) {
    const ScopedAuthSock env{nullptr};
    AURORA_TEST_CHECK(borealis::platform::agent_connect() == nullptr);
}

AURORA_TEST_CASE(an_unreachable_socket_yields_no_connection) {
    const ScopedAuthSock env{"/tmp/borealis-no-agent-here.sock"};
    AURORA_TEST_CHECK(borealis::platform::agent_connect() == nullptr);
}

AURORA_TEST_CASE(an_overlong_auth_sock_is_refused) {
    // unix socket 路径上限是 `sun_path` 长度；超长路径不能截断后去连（会连到别的 socket）。
    const std::string too_long(200, 'a');
    const ScopedAuthSock env{too_long.c_str()};
    AURORA_TEST_CHECK(borealis::platform::agent_connect() == nullptr);
}

AURORA_TEST_CASE(bytes_round_trip_through_the_agent_pipe) {
    const auto path = auth_sock_path();
    EchoAgent agent;
    AURORA_TEST_REQUIRE_MSG(start_echo_agent(agent, path), "cannot create the fake agent socket");
    const ScopedAuthSock env{path.c_str()};

    auto socket = borealis::platform::agent_connect();
    AURORA_TEST_REQUIRE_MSG(socket != nullptr, "agent_connect did not reach the fake agent");

    const std::array<std::uint8_t, 5> request{'h', 'e', 'l', 'l', 'o'};
    AURORA_TEST_REQUIRE(socket->write(request));

    std::array<std::uint8_t, 64> response{};
    auto read = borealis::platform::AgentRead{};
    for (int attempt = 0; attempt < 50 && read.outcome != AgentReadOutcome::Data; ++attempt) {
        read = socket->read(response, 100);
    }
    AURORA_TEST_REQUIRE(read.outcome == AgentReadOutcome::Data);
    AURORA_TEST_REQUIRE_EQ(read.bytes, request.size());
    for (std::size_t i = 0; i < request.size(); ++i) {
        AURORA_TEST_CHECK_EQ(response[i], request[i]);
    }

    socket->close();
}

}  // namespace borealis::test_cases::utest_posix_agent_socket
