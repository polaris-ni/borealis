/// 测试类型: unit
/// 目标单元: src/conn/tunnel_socks.h（私有头）SOCKS5 握手纯解析器
/// 测试说明: 按 RFC 1928 字节夹具逐段喂入——greeting 协商、CONNECT 请求三种
///           ATYP、分片增量到达、各类拒绝应答字节。纯字节逻辑，不碰 socket。

#include "conn/tunnel_socks.h"

#include <cstdint>
#include <span>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_tunnel_socks {

namespace {

using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] auto feed_all(conn::Socks5Negotiator &neg, const Bytes &bytes)
    -> conn::SocksVerdict {
    return neg.feed(std::span<const std::uint8_t>{bytes});
}

/// greeting（VER=5, 1 个方法=无认证）+ IPv4 CONNECT 到 127.0.0.1:80。
[[nodiscard]] auto greeting_v4_connect() -> Bytes {
    return {0x05, 0x01, 0x00,   // greeting: ver, nmethods, NO_AUTH
            0x05, 0x01, 0x00, 0x01, 127, 0, 0, 1, 0x00, 0x50};
}

}  // namespace

AURORA_TEST_CASE(greeting_and_v4_request_reach_ready) {
    conn::Socks5Negotiator neg;
    AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, greeting_v4_connect())),
                         static_cast<int>(conn::SocksVerdict::ReadyToConnect));
    AURORA_TEST_CHECK_MSG(neg.target().host == "127.0.0.1", "v4 dotted form");
    AURORA_TEST_CHECK_EQ(neg.target().port, 80);
    auto reply = neg.take_pending_reply();
    AURORA_TEST_CHECK_EQ(reply.size(), 2U);  // 只该有 greeting 应答，成功应答由拨号后补
    if (reply.size() == 2) {
        AURORA_TEST_CHECK_EQ(static_cast<int>(reply[0]), 5);
        AURORA_TEST_CHECK_EQ(static_cast<int>(reply[1]), 0);
    }

    neg.append_connect_ok();
    auto ok = neg.take_pending_reply();
    AURORA_TEST_CHECK_EQ(ok.size(), 10U);
    if (ok.size() == 10) {
        AURORA_TEST_CHECK_EQ(static_cast<int>(ok[1]), 0);  // REP=0 连接建立
        AURORA_TEST_CHECK_EQ(static_cast<int>(ok[3]), 1);  // BND.ADDR 走 0.0.0.0 零填充
    }
    // take 之后清空（幂等）。
    AURORA_TEST_CHECK_TRUE(neg.take_pending_reply().empty());
}

AURORA_TEST_CASE(domain_and_v6_targets_parse) {
    conn::Socks5Negotiator neg;
    Bytes stream{0x05, 0x01, 0x00, 0x05, 0x01, 0x00, 0x03};
    const std::string host = "db.internal.example";
    stream.push_back(static_cast<std::uint8_t>(host.size()));
    for (const char c : host) {
        stream.push_back(static_cast<std::uint8_t>(c));
    }
    stream.insert(stream.end(), {0x1F, 0x90});  // 8080 大端
    AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, stream)),
                         static_cast<int>(conn::SocksVerdict::ReadyToConnect));
    AURORA_TEST_CHECK_MSG(neg.target().host == host, "domain kept verbatim");
    AURORA_TEST_CHECK_EQ(neg.target().port, 8080);

    conn::Socks5Negotiator neg6;
    Bytes v6{0x05, 0x01, 0x00, 0x05, 0x01, 0x00, 0x04};
    for (int i = 0; i < 15; ++i) {
        v6.push_back(0);
    }
    v6.push_back(1);  // ::1
    v6.insert(v6.end(), {0x00, 0x16});
    AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg6, v6)),
                         static_cast<int>(conn::SocksVerdict::ReadyToConnect));
    AURORA_TEST_CHECK_MSG(neg6.target().host == "0:0:0:0:0:0:0:1", "v6 full text");
    AURORA_TEST_CHECK_EQ(neg6.target().port, 22);
}

AURORA_TEST_CASE(fragmented_arrival_needs_more_until_complete) {
    const Bytes all = greeting_v4_connect();
    conn::Socks5Negotiator neg;
    for (std::size_t i = 0; i + 1 < all.size(); ++i) {
        AURORA_TEST_CHECK_EQ(
            static_cast<int>(neg.feed(std::span<const std::uint8_t>{all.data() + i, 1})),
            static_cast<int>(conn::SocksVerdict::NeedMore));
    }
    AURORA_TEST_CHECK_EQ(static_cast<int>(neg.feed(std::span<const std::uint8_t>{&all.back(), 1})),
                         static_cast<int>(conn::SocksVerdict::ReadyToConnect));
    AURORA_TEST_CHECK_EQ(neg.target().port, 80);
}

AURORA_TEST_CASE(rejections_carry_protocol_reply_bytes) {
    {  // 版本非 5：无应答可用，直接判死。
        conn::Socks5Negotiator neg;
        AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, {0x04, 0x01, 0x00})),
                             static_cast<int>(conn::SocksVerdict::VersionUnsupported));
        AURORA_TEST_CHECK_TRUE(neg.take_pending_reply().empty());
    }
    {  // 客户端只提 GSSAPI（0x02）：回 05 FF。
        conn::Socks5Negotiator neg;
        AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, {0x05, 0x01, 0x02})),
                             static_cast<int>(conn::SocksVerdict::NoAcceptableMethod));
        auto reply = neg.take_pending_reply();
        AURORA_TEST_CHECK_EQ(reply.size(), 2U);
        if (reply.size() == 2) {
            AURORA_TEST_CHECK_EQ(static_cast<int>(reply[1]), 0xFF);
        }
    }
    {  // CMD=BIND 拒绝（REP 0x07）。
        conn::Socks5Negotiator neg;
        Bytes stream{0x05, 0x01, 0x00, 0x05, 0x02, 0x00, 0x01, 10, 0, 0, 1, 0x00, 0x15};
        AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, stream)),
                             static_cast<int>(conn::SocksVerdict::CommandUnsupported));
        auto reply = neg.take_pending_reply();
        AURORA_TEST_CHECK_EQ(reply.size(), 12U);  // 2 greeting + 10 failure
        if (reply.size() == 12) {
            AURORA_TEST_CHECK_EQ(static_cast<int>(reply[3]), 0x07);
        }
    }
    {  // ATYP=2（未分配）拒绝（REP 0x08）。
        conn::Socks5Negotiator neg;
        Bytes stream{0x05, 0x01, 0x00, 0x05, 0x01, 0x00, 0x02, 0x00, 0x15};
        AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, stream)),
                             static_cast<int>(conn::SocksVerdict::AddressTypeUnsupported));
        auto reply = neg.take_pending_reply();
        if (reply.size() == 12) {
            AURORA_TEST_CHECK_EQ(static_cast<int>(reply[3]), 0x08);
        }
    }
    {  // 端口 0：general failure（REP 0x01）。
        conn::Socks5Negotiator neg;
        Bytes stream{0x05, 0x01, 0x00, 0x05, 0x01, 0x00, 0x01, 127, 0, 0, 1, 0x00, 0x00};
        AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, stream)),
                             static_cast<int>(conn::SocksVerdict::CommandUnsupported));
        auto reply = neg.take_pending_reply();
        if (reply.size() == 12) {
            AURORA_TEST_CHECK_EQ(static_cast<int>(reply[3]), 0x01);
        }
    }
}

AURORA_TEST_CASE(terminal_state_holds_after_verdict) {
    conn::Socks5Negotiator neg;
    AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, {0x04, 0x01, 0x00})),
                         static_cast<int>(conn::SocksVerdict::VersionUnsupported));
    // 终态后再喂不改结论。
    AURORA_TEST_CHECK_EQ(static_cast<int>(feed_all(neg, greeting_v4_connect())),
                         static_cast<int>(conn::SocksVerdict::VersionUnsupported));
}

}  // namespace borealis::test_cases::utest_tunnel_socks
