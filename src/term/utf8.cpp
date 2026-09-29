// ============================================================
// UTF-8 编解码实现（src/term/utf8.cpp）
// ------------------------------------------------------------
// 解码是严格 UTF-8：起始字节决定长度与第二字节的合法区间，越界即整段按替换字符处理。
// 第二字节的上下界是挡住三类伪造的关键——下界挡过长编码（`C0 80` 拼出 NUL），
// 上界挡代理项与超出 U+10FFFF 的值（`ED A0 80`、`F4 90 80 80`）。
//
// 非法续字节按 Unicode「最大子部分」口径：先为已累积的残缺序列补一个替换字符，
// 再把这个字节当作新序列的起点重新判定（`E0 80 80` 因此产出三个替换字符）。
// ============================================================

#include "borealis/term/utf8.h"

namespace borealis::term {
namespace {

constexpr std::uint8_t kContinuationMin = 0x80;
constexpr std::uint8_t kContinuationMax = 0xBF;

/// @brief 该字节是否为续字节（10xxxxxx）。
constexpr auto is_continuation(std::uint8_t b) noexcept -> bool {
    return b >= kContinuationMin && b <= kContinuationMax;
}

}  // namespace

auto Utf8Decoder::begin_sequence(std::uint8_t b, CodePointSink &sink) -> void {
    if (b < 0x80U) {
        ++stats_.code_points;
        sink.on_code_point(static_cast<char32_t>(b));
        return;
    }
    // 长度与第二字节区间的判定表（`pending_` 为还需的续字节数）
    if (b >= 0xC2U && b <= 0xDFU) {
        pending_ = 1;
        code_point_ = b & 0x1FU;
        second_min_ = kContinuationMin;
        second_max_ = kContinuationMax;
    } else if (b == 0xE0U) {
        pending_ = 2;
        code_point_ = 0;
        second_min_ = 0xA0U; // 挡 `E0 80 xx` 一类的过长编码
        second_max_ = kContinuationMax;
    } else if (b >= 0xE1U && b <= 0xECU) {
        pending_ = 2;
        code_point_ = b & 0x0FU;
        second_min_ = kContinuationMin;
        second_max_ = kContinuationMax;
    } else if (b == 0xEDU) {
        pending_ = 2;
        code_point_ = 0x0DU;
        second_min_ = kContinuationMin;
        second_max_ = 0x9FU; // 挡代理项（U+D800–DFFF）
    } else if (b >= 0xEEU && b <= 0xEFU) {
        pending_ = 2;
        code_point_ = b & 0x0FU;
        second_min_ = kContinuationMin;
        second_max_ = kContinuationMax;
    } else if (b == 0xF0U) {
        pending_ = 3;
        code_point_ = 0;
        second_min_ = 0x90U; // 挡 `F0 80 xx xx` 一类的过长编码
        second_max_ = kContinuationMax;
    } else if (b >= 0xF1U && b <= 0xF3U) {
        pending_ = 3;
        code_point_ = b & 0x07U;
        second_min_ = kContinuationMin;
        second_max_ = kContinuationMax;
    } else if (b == 0xF4U) {
        pending_ = 3;
        code_point_ = 0x04U;
        second_min_ = kContinuationMin;
        second_max_ = 0x8FU; // 挡超出 U+10FFFF 的值
    } else {
        // 孤立续字节（0x80–0xBF）、过长起始（0xC0/0xC1）与越界起始（0xF5–0xFF）
        emit_replacement(sink);
        return;
    }
    first_continuation_ = true;
}

auto Utf8Decoder::emit_replacement(CodePointSink &sink) -> void {
    pending_ = 0;
    first_continuation_ = false;
    ++stats_.replaced;
    ++stats_.code_points;
    sink.on_code_point(kReplacementCharacter);
}

auto Utf8Decoder::feed(std::span<const std::byte> bytes, CodePointSink &sink) -> void {
    for (const std::byte raw : bytes) {
        const auto b = static_cast<std::uint8_t>(raw);
        // 非法续字节要当作新序列的起点重新判定，故每个字节最多处理两轮。
        for (std::uint32_t round = 0; round < 2U; ++round) {
            if (pending_ == 0) {
                begin_sequence(b, sink);
                break;
            }
            const std::uint8_t low = first_continuation_ ? second_min_ : kContinuationMin;
            const std::uint8_t high = first_continuation_ ? second_max_ : kContinuationMax;
            if (!is_continuation(b) || b < low || b > high) {
                emit_replacement(sink);
                continue; // 该字节按起始字节重新走一轮
            }
            code_point_ = (code_point_ << 6) | (b & 0x3FU);
            first_continuation_ = false;
            --pending_;
            if (pending_ == 0) {
                ++stats_.code_points;
                sink.on_code_point(static_cast<char32_t>(code_point_));
            }
            break;
        }
    }
}

auto Utf8Decoder::finish(CodePointSink &sink) -> void {
    if (pending_ == 0) {
        return;
    }
    emit_replacement(sink);
}

auto Utf8Decoder::reset() noexcept -> void {
    pending_ = 0;
    code_point_ = 0;
    first_continuation_ = false;
}

auto append_utf8(char32_t cp, std::string &out) -> bool {
    // 代理项与超出 U+10FFFF 的值在 UTF-8 里没有合法编码（RFC 3629）。
    const auto value = static_cast<std::uint32_t>(cp);
    const bool representable = !(value >= 0xD800U && value <= 0xDFFFU) && value <= 0x10FFFFU;
    const auto emit = representable ? value : static_cast<std::uint32_t>(kReplacementCharacter);

    if (emit < 0x80U) {
        out += static_cast<char>(emit);
    } else if (emit < 0x800U) {
        out += static_cast<char>(0xC0U | (emit >> 6));
        out += static_cast<char>(0x80U | (emit & 0x3FU));
    } else if (emit < 0x10000U) {
        out += static_cast<char>(0xE0U | (emit >> 12));
        out += static_cast<char>(0x80U | ((emit >> 6) & 0x3FU));
        out += static_cast<char>(0x80U | (emit & 0x3FU));
    } else {
        out += static_cast<char>(0xF0U | (emit >> 18));
        out += static_cast<char>(0x80U | ((emit >> 12) & 0x3FU));
        out += static_cast<char>(0x80U | ((emit >> 6) & 0x3FU));
        out += static_cast<char>(0x80U | (emit & 0x3FU));
    }
    return !representable;
}

auto encode_utf8(std::u32string_view text, std::string &out) -> std::size_t {
    std::size_t replaced = 0;
    for (const char32_t cp : text) {
        if (append_utf8(cp, out)) {
            ++replaced;
        }
    }
    return replaced;
}

}  // namespace borealis::term
