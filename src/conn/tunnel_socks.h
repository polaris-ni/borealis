#pragma once

// ============================================================
// SOCKS5 握手纯解析器（src/conn/tunnel_socks.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.08 动态式（-D）的应用层协议腿：把 RFC 1928/1929 的
// 握手字节流解析成「目标 host:port + 该回给客户端的字节」，纯逻辑、
// 无 socket（utest_tunnel_socks 逐字节喂夹具即可回归）。
//
// 支持面（本期口径，裁决 7.96）：仅 CONNECT（CMD=1）、仅无认证
// （METHODS 含 0x00）；IPv4/域名/IPv6 三种 ATYP 都解析（回给浏览器一类
// 常见客户端够用）。分片到达由 feed() 的增量缓冲承接。
// ============================================================

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace borealis::conn {

/// @brief 一次 feed() 的裁决。
enum class SocksVerdict : std::uint8_t {
    NeedMore,               ///< 字节不够，继续收。
    ReadyToConnect,         ///< 请求解析完毕，target() 可拨。
    VersionUnsupported,     ///< VER 非 5（已备好拒绝应答）。
    NoAcceptableMethod,     ///< METHODS 里没有 0x00（已备 0x05 0xFF 应答）。
    CommandUnsupported,     ///< CMD 非 CONNECT（已备 0x07 应答）。
    AddressTypeUnsupported, ///< ATYP 非 1/3/4（已备 0x08 应答）。
};

/// @brief 解析出的 CONNECT 目标。
struct SocksTarget {
    std::string host;  ///< 点分 IPv4 / 字面 IPv6 / 域名原样。
    int port{0};       ///< [1,65535]。
};

/// @brief 单连接一次性握手状态机（greeting → request）。
class Socks5Negotiator {
  public:
    /// @brief 喂入新到字节；返回当前裁决。终态（非 NeedMore）后再喂不再改变结论。
    auto feed(std::span<const std::uint8_t> bytes) -> SocksVerdict;

    /// @brief 取走累计待发送的应答字节（greeting 应答 + 失败/成功应答）。
    [[nodiscard]] auto take_pending_reply() -> std::vector<std::uint8_t>;

    /// @brief 成功拨通后追加「连接建立」应答（X'05 00 00 01' + 0.0.0.0:0）。
    ///        传输腿在 direct-tcpip 通道开成功后调用。
    auto append_connect_ok() -> void;

    [[nodiscard]] auto target() const noexcept -> const SocksTarget &;

  private:
    enum class Phase : std::uint8_t { Greeting, Request, Done };

    Phase phase_{Phase::Greeting};
    std::vector<std::uint8_t> buffer_;       ///< 分片累积。
    std::vector<std::uint8_t> pending_reply_;  ///< 待发送的协议应答。
    SocksTarget target_{};
    SocksVerdict verdict_{SocksVerdict::NeedMore};
};

}  // namespace borealis::conn
