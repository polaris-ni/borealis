/// 测试类型: unit
/// 目标单元: src/platform/tcp.h TCP 转发腿（当前跑的是 POSIX 实现）
/// 测试说明: 纯环回集成——listen/accept/connect/read/write 与关闭分档，不出本机、
///           不需要外网与特权端口。Windows 腿同接口，待 MSVC 真机回归（未验证口径
///           与其余 win 腿一致）。

#include "platform/tcp.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_tcp_loopback {

namespace {

using namespace borealis;

[[nodiscard]] auto bytes_of(const std::string &text) -> std::span<const std::uint8_t> {
    return std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t *>(text.data()),
                                         text.size()};
}

}  // namespace

AURORA_TEST_CASE(listen_accept_roundtrip_carries_bytes_both_ways) {
    auto listener = platform::tcp_listen("127.0.0.1", 0);
    AURORA_TEST_CHECK_TRUE(listener != nullptr);
    if (listener == nullptr) {
        return;
    }
    AURORA_TEST_CHECK_GT(listener->bound_port(), 0);

    auto client = platform::tcp_connect("127.0.0.1", listener->bound_port(), 2000);
    AURORA_TEST_CHECK_TRUE(client != nullptr);
    auto server_side = listener->accept(2000);
    AURORA_TEST_CHECK_TRUE(server_side != nullptr);
    if (client == nullptr || server_side == nullptr) {
        return;
    }

    AURORA_TEST_CHECK_TRUE(client->write(bytes_of("ping")));
    std::uint8_t buf[16] = {};
    auto read = server_side->read(std::span<std::uint8_t>{buf, 4}, 2000);
    AURORA_TEST_CHECK_EQ(static_cast<int>(read.outcome), static_cast<int>(platform::TcpReadOutcome::Data));
    AURORA_TEST_CHECK_EQ(read.bytes, 4U);
    AURORA_TEST_CHECK_MSG((std::string{reinterpret_cast<char *>(buf), read.bytes} == "ping"),
                          "payload intact");

    // 回路与对端信息。
    AURORA_TEST_CHECK_TRUE(server_side->write(bytes_of("pong!")));
    auto back = client->read(std::span<std::uint8_t>{buf, 5}, 2000);
    AURORA_TEST_CHECK_EQ(back.bytes, 5U);
    AURORA_TEST_CHECK_MSG(client->peer_address() == "127.0.0.1", "loopback peer");
    AURORA_TEST_CHECK_GT(client->peer_port(), 0);
}

AURORA_TEST_CASE(accept_times_out_and_second_bind_loses) {
    auto listener = platform::tcp_listen("127.0.0.1", 0);
    AURORA_TEST_CHECK_TRUE(listener != nullptr);
    if (listener == nullptr) {
        return;
    }
    // 没人连：accept 到点回 nullptr（工作线程按拍检查停止标志依赖这一语义）。
    AURORA_TEST_CHECK_TRUE(listener->accept(50) == nullptr);

    // 同一监听点二次 bind 必须失败（SO_REUSEADDR 不放行双活监听）。
    auto second = platform::tcp_listen("127.0.0.1", listener->bound_port());
    AURORA_TEST_CHECK_TRUE(second == nullptr);
}

AURORA_TEST_CASE(connect_refused_fails_fast) {
    auto probe = platform::tcp_listen("127.0.0.1", 0);
    AURORA_TEST_CHECK_TRUE(probe != nullptr);
    if (probe == nullptr) {
        return;
    }
    const int port = probe->bound_port();
    probe->close();
    AURORA_TEST_CHECK_TRUE(platform::tcp_connect("127.0.0.1", port, 1000) == nullptr);
}

AURORA_TEST_CASE(peer_close_surfaces_closed_outcome) {
    auto listener = platform::tcp_listen("127.0.0.1", 0);
    AURORA_TEST_CHECK_TRUE(listener != nullptr);
    if (listener == nullptr) {
        return;
    }
    auto client = platform::tcp_connect("127.0.0.1", listener->bound_port(), 2000);
    auto server_side = listener->accept(2000);
    AURORA_TEST_CHECK_TRUE(client != nullptr && server_side != nullptr);
    if (client == nullptr || server_side == nullptr) {
        return;
    }
    client->close();  // 有序 FIN。
    std::uint8_t buf[8] = {};
    auto outcome = platform::TcpReadOutcome::Timeout;
    for (int i = 0; i < 20 && outcome != platform::TcpReadOutcome::Closed; ++i) {
        const auto read = server_side->read(std::span<std::uint8_t>{buf, 4}, 100);
        outcome = read.outcome;
        AURORA_TEST_CHECK_NE(static_cast<int>(outcome),
                             static_cast<int>(platform::TcpReadOutcome::Error));
    }
    AURORA_TEST_CHECK_EQ(static_cast<int>(outcome),
                         static_cast<int>(platform::TcpReadOutcome::Closed));
}

}  // namespace borealis::test_cases::utest_tcp_loopback
