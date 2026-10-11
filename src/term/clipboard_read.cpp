// ============================================================
// OSC 52 读方向授权闸门与 base64 编码实现（src/term/clipboard_read.cpp）
// ============================================================

#include "borealis/term/clipboard_read.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace borealis::term {

auto ClipboardGate::policy() const noexcept -> ClipboardReadPolicy {
    // 单一独立字，只要求「改档迟早可见」，不要求与别的写入排序；这里取默认 seq_cst，
    // 省掉一档需要注释来担保的 relaxed。
    return static_cast<ClipboardReadPolicy>(policy_.load());
}

auto ClipboardGate::set_policy(ClipboardReadPolicy policy) noexcept -> void {
    policy_.store(static_cast<std::uint8_t>(policy));
}

namespace {

constexpr std::array<char, 65> kBase64Alphabet{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};

/// @brief 把三个字节写成一个四字符组（不足三字节由 @p octets 决定补几个 `=`）。
auto emit_group(std::uint32_t triple, std::size_t octets, std::string &out) -> void {
    static constexpr std::array<std::uint32_t, 4> kShifts{18U, 12U, 6U, 0U};
    for (std::size_t index = 0; index < 4U; ++index) {
        if (index >= octets + 1U) {
            out.push_back('=');  // 该 6 位组没有对应的输入字节
            continue;
        }
        out.push_back(kBase64Alphabet[(triple >> kShifts[index]) & 0x3FU]);
    }
}

[[nodiscard]] auto byte_at(std::span<const std::byte> bytes, std::size_t index) noexcept {
    return static_cast<std::uint32_t>(bytes[index]);
}

/// @brief 把从 @p index 起的 @p count（≤ 3）个字节拼进一个 24 位组，高位在前。
///
/// 不足三字节时低位天然是 0，正是 base64 要求的「补零后再截断成 6 位组」。
[[nodiscard]] auto pack_group(std::span<const std::byte> bytes, std::size_t index,
                            std::size_t count) noexcept {
    std::uint32_t triple = 0U;
    for (std::size_t offset = 0; offset < count; ++offset) {
        triple |= byte_at(bytes, index + offset) << (8U * (2U - offset));
    }
    return triple;
}

}  // namespace

auto encode_base64(std::span<const std::byte> bytes) -> std::string {
    std::string out;
    out.reserve((bytes.size() + 2U) / 3U * 4U);
    for (std::size_t index = 0; index < bytes.size(); index += 3U) {
        const std::size_t octets = std::min<std::size_t>(3U, bytes.size() - index);
        emit_group(pack_group(bytes, index, octets), octets, out);
    }
    return out;
}

}  // namespace borealis::term
