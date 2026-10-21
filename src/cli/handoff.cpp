// ============================================================
// 单实例转交帧编解码（src/cli/handoff.cpp）
// ------------------------------------------------------------
// 与 `include/borealis/cli/handoff.h` 的线格式逐字节对应。全部检查先于构造：
// 本件宁可回空让调用方报错，也不交出半截请求。
// ============================================================

#include "borealis/cli/handoff.h"

#include <array>
#include <cstring>

namespace borealis::cli {

namespace {

constexpr std::size_t kRequestHeaderBytes = 12;
constexpr std::size_t kReplyHeaderBytes = 8;
constexpr std::array<std::uint8_t, 4> kRequestMagic{'B', 'O', 'R', 'I'};
constexpr std::array<std::uint8_t, 4> kReplyMagic{'B', 'O', 'R', 'R'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kFlagNewWindow = 0x01;

[[nodiscard]] auto has_nul(std::string_view text) -> bool {
    return text.find('\0') != std::string_view::npos;
}

[[nodiscard]] auto value_fits(std::string_view text) -> bool {
    return text.size() <= kMaxStartupValueBytes && !has_nul(text);
}

auto append_u16(std::vector<std::uint8_t> &out, std::size_t value) -> void {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

[[nodiscard]] auto read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) -> std::size_t {
    return static_cast<std::size_t>(bytes[offset]) |
           (static_cast<std::size_t>(bytes[offset + 1]) << 8U);
}

[[nodiscard]] auto magic_matches(std::span<const std::uint8_t> bytes,
                                 const std::array<std::uint8_t, 4> &magic) -> bool {
    return bytes.size() >= magic.size() &&
           std::memcmp(bytes.data(), magic.data(), magic.size()) == 0;
}

}  // namespace

auto encode_startup_request(const StartupRequest &request)
    -> std::optional<std::vector<std::uint8_t>> {
    if (request.action != StartupAction::NewTab && request.action != StartupAction::SplitPane) {
        return std::nullopt;
    }
    if (!value_fits(request.profile) || !value_fits(request.cwd)) {
        return std::nullopt;
    }
    const std::size_t total = kRequestHeaderBytes + request.profile.size() + request.cwd.size();
    if (total > kMaxHandoffRequestBytes) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> out;
    out.reserve(total);
    out.insert(out.end(), kRequestMagic.begin(), kRequestMagic.end());
    out.push_back(kVersion);
    out.push_back(request.action == StartupAction::SplitPane ? 1U : 0U);
    out.push_back(request.new_window ? kFlagNewWindow : 0U);
    out.push_back(0U);  // reserved
    append_u16(out, request.profile.size());
    append_u16(out, request.cwd.size());
    out.insert(out.end(), request.profile.begin(), request.profile.end());
    out.insert(out.end(), request.cwd.begin(), request.cwd.end());
    return out;
}

auto decode_startup_request(std::span<const std::uint8_t> bytes)
    -> std::optional<StartupRequest> {
    if (bytes.size() < kRequestHeaderBytes || !magic_matches(bytes, kRequestMagic)) {
        return std::nullopt;
    }
    if (bytes[4] != kVersion || (bytes[6] & ~kFlagNewWindow) != 0U || bytes[7] != 0U) {
        return std::nullopt;
    }
    if (bytes[5] > 1U) {
        return std::nullopt;
    }
    const std::size_t profile_len = read_u16(bytes, 8);
    const std::size_t cwd_len = read_u16(bytes, 10);
    if (profile_len > kMaxStartupValueBytes || cwd_len > kMaxStartupValueBytes) {
        return std::nullopt;
    }
    if (bytes.size() != kRequestHeaderBytes + profile_len + cwd_len) {
        return std::nullopt;  // 截断与尾随字节同判。
    }
    const auto *profile_data = reinterpret_cast<const char *>(bytes.data() + kRequestHeaderBytes);
    const auto *cwd_data = profile_data + profile_len;
    std::string profile{profile_data, profile_len};
    std::string cwd{cwd_data, cwd_len};
    if (has_nul(profile) || has_nul(cwd)) {
        return std::nullopt;
    }
    return StartupRequest{
        .action = bytes[5] == 1U ? StartupAction::SplitPane : StartupAction::NewTab,
        .profile = std::move(profile),
        .cwd = std::move(cwd),
        .new_window = (bytes[6] & kFlagNewWindow) != 0U,
    };
}

auto encode_startup_reply(const StartupReply &reply)
    -> std::optional<std::vector<std::uint8_t>> {
    if (reply.message.size() > kMaxHandoffReplyBytes || has_nul(reply.message)) {
        return std::nullopt;
    }
    const std::size_t total = kReplyHeaderBytes + reply.message.size();
    if (total > kMaxHandoffReplyBytes) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> out;
    out.reserve(total);
    out.insert(out.end(), kReplyMagic.begin(), kReplyMagic.end());
    out.push_back(kVersion);
    out.push_back(reply.ok ? 0U : 1U);
    append_u16(out, reply.message.size());
    out.insert(out.end(), reply.message.begin(), reply.message.end());
    return out;
}

auto decode_startup_reply(std::span<const std::uint8_t> bytes)
    -> std::optional<StartupReply> {
    if (bytes.size() < kReplyHeaderBytes || !magic_matches(bytes, kReplyMagic)) {
        return std::nullopt;
    }
    if (bytes[4] != kVersion || bytes[5] > 1U) {
        return std::nullopt;
    }
    const std::size_t message_len = read_u16(bytes, 6);
    if (message_len > kMaxHandoffReplyBytes) {
        return std::nullopt;
    }
    if (bytes.size() != kReplyHeaderBytes + message_len) {
        return std::nullopt;
    }
    const auto *message_data = reinterpret_cast<const char *>(bytes.data() + kReplyHeaderBytes);
    std::string message{message_data, message_len};
    if (has_nul(message)) {
        return std::nullopt;
    }
    return StartupReply{.ok = bytes[5] == 0U, .message = std::move(message)};
}

}  // namespace borealis::cli
