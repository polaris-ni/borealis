// ============================================================
// SOCKS5 握手纯解析器实现（src/conn/tunnel_socks.cpp）
// ------------------------------------------------------------
// 字节级解析，无 socket、无异常（见 conn/tunnel_socks.h 头注释）。
// ============================================================

#include "conn/tunnel_socks.h"

#include <algorithm>
#include <cstdio>

namespace borealis::conn {

namespace {

/// @brief 失败应答骨架：X'05 REP 00 01' + 0.0.0.0:0（RFC 1928 允许的零填充）。
[[nodiscard]] auto failure_reply(std::uint8_t reason) -> std::vector<std::uint8_t> {
    return {0x05, reason, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

/// @brief ATYP 的地址段字节数；未知 ATYP 回 -1。
[[nodiscard]] auto address_length(std::uint8_t atyp) -> int {
    switch (atyp) {
    case 0x01:
        return 4;   // IPv4
    case 0x04:
        return 16;  // IPv6
    default:
        return -1;  // 0x03（域名）长度自描述，调用方单独处理
    }
}

/// @brief 16 字节 IPv6 转冒号十六进制（全写、小写；inet_pton 一类都能吃）。
[[nodiscard]] auto format_ipv6(const std::uint8_t *bytes) -> std::string {
    std::string text;
    text.reserve(39);
    for (int group = 0; group < 8; ++group) {
        if (group > 0) {
            text += ':';
        }
        const auto value = (static_cast<unsigned>(bytes[group * 2]) << 8) | bytes[group * 2 + 1];
        char piece[8];
        std::snprintf(piece, sizeof(piece), "%x", value);
        text += piece;
    }
    return text;
}

/// @brief 应答累加（greeting 应答与失败应答可同包先后到达）。
auto append_reply(std::vector<std::uint8_t> &into, std::vector<std::uint8_t> reply) -> void {
    into.insert(into.end(), reply.begin(), reply.end());
}

/// @brief 4 字节点分十进制。
[[nodiscard]] auto format_ipv4(const std::uint8_t *bytes) -> std::string {
    char piece[16];
    std::snprintf(piece, sizeof(piece), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    return std::string{piece};
}

}  // namespace

auto Socks5Negotiator::feed(std::span<const std::uint8_t> bytes) -> SocksVerdict {
    if (phase_ == Phase::Done) {
        return verdict_;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());

    if (phase_ == Phase::Greeting) {
        if (buffer_.size() < 2) {
            return SocksVerdict::NeedMore;
        }
        const auto method_count = buffer_[1];
        if (buffer_.size() < 2U + method_count) {
            return SocksVerdict::NeedMore;
        }
        if (buffer_[0] != 0x05) {
            verdict_ = SocksVerdict::VersionUnsupported;
            phase_ = Phase::Done;
            return verdict_;
        }
        const std::span<const std::uint8_t> methods{buffer_.data() + 2, method_count};
        const bool accepts_no_auth =
            std::find(methods.begin(), methods.end(), std::uint8_t{0x00}) != methods.end();
        if (!accepts_no_auth) {
            pending_reply_ = {0x05, 0xFF};
            verdict_ = SocksVerdict::NoAcceptableMethod;
            phase_ = Phase::Done;
            return verdict_;
        }
        pending_reply_ = {0x05, 0x00};
        buffer_.erase(buffer_.begin(), buffer_.begin() + 2 + method_count);
        phase_ = Phase::Request;
        // greeting 与 request 同包到达（客户端 Nagle 一类）：直接往下解析。
        verdict_ = SocksVerdict::NeedMore;
    }

    // Request: VER CMD RSV ATYP ADDR PORT(2)
    if (buffer_.size() < 4) {
        return SocksVerdict::NeedMore;
    }
    if (buffer_[0] != 0x05) {
        verdict_ = SocksVerdict::VersionUnsupported;
        phase_ = Phase::Done;
        return verdict_;
    }
    if (buffer_[1] != 0x01) {
        append_reply(pending_reply_, failure_reply(0x07));  // command not supported
        verdict_ = SocksVerdict::CommandUnsupported;
        phase_ = Phase::Done;
        return verdict_;
    }
    const auto atyp = buffer_[3];
    std::size_t total = 0;
    SocksTarget target;
    if (atyp == 0x03) {
        if (buffer_.size() < 5) {
            return SocksVerdict::NeedMore;
        }
        const auto name_length = buffer_[4];
        total = 5U + name_length + 2U;
        if (buffer_.size() < total) {
            return SocksVerdict::NeedMore;
        }
        target.host.assign(buffer_.begin() + 5, buffer_.begin() + 5 + name_length);
    } else {
        const auto length = address_length(atyp);
        if (length < 0) {
            append_reply(pending_reply_, failure_reply(0x08));  // address type not supported
            verdict_ = SocksVerdict::AddressTypeUnsupported;
            phase_ = Phase::Done;
            return verdict_;
        }
        total = 4U + static_cast<std::size_t>(length) + 2U;
        if (buffer_.size() < total) {
            return SocksVerdict::NeedMore;
        }
        const auto *address = buffer_.data() + 4;
        target.host = length == 4 ? format_ipv4(address) : format_ipv6(address);
    }
    target.port = (buffer_[total - 2U] << 8) | buffer_[total - 1U];
    if (target.port < 1 || target.port > 65535 || target.host.empty()) {
        append_reply(pending_reply_, failure_reply(0x01));  // general failure
        verdict_ = SocksVerdict::CommandUnsupported;
        phase_ = Phase::Done;
        return verdict_;
    }

    target_ = std::move(target);
    verdict_ = SocksVerdict::ReadyToConnect;
    // phase 停在 Request 前缘之外：成功即「拨号后由 append_connect_ok 收尾」。
    return verdict_;
}

auto Socks5Negotiator::take_pending_reply() -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> reply;
    reply.swap(pending_reply_);
    return reply;
}

auto Socks5Negotiator::append_connect_ok() -> void {
    auto reply = failure_reply(0x00);  // 骨架同形，REP=0 即成功
    pending_reply_.insert(pending_reply_.end(), reply.begin(), reply.end());
    phase_ = Phase::Done;
}

auto Socks5Negotiator::target() const noexcept -> const SocksTarget & {
    return target_;
}

}  // namespace borealis::conn
