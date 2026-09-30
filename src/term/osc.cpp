// ============================================================
// OSC 字段切分与 base64 载荷解码实现（src/term/osc.cpp）
// ============================================================

#include "osc.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "borealis/term/utf8.h"

namespace borealis::term::osc {

namespace {

/// @brief 命令号的上界：真实 OSC 命令号不超过三位数，越界即按非法处理（不给溢出留口子）。
constexpr std::int32_t kMaxOscCommand = 100000;

/// @brief 收集解码产物的码点缓冲（`Utf8Decoder` 的接收端）。
class BufferSink final : public CodePointSink {
  public:
    explicit BufferSink(std::u32string &buffer) : buffer_{buffer} {}

    auto on_code_point(char32_t cp) -> void override { buffer_.push_back(cp); }

  private:
    std::u32string &buffer_;
};

/// @brief base64 字母表的取值；非字母表字符返回空。
[[nodiscard]] auto base64_value(char32_t c) noexcept -> std::optional<std::uint32_t> {
    if (c >= U'A' && c <= U'Z') {
        return static_cast<std::uint32_t>(c - U'A');
    }
    if (c >= U'a' && c <= U'z') {
        return static_cast<std::uint32_t>(c - U'a') + 26U;
    }
    if (c >= U'0' && c <= U'9') {
        return static_cast<std::uint32_t>(c - U'0') + 52U;
    }
    if (c == U'+') {
        return 62U;
    }
    return c == U'/' ? std::optional<std::uint32_t>{63U} : std::optional<std::uint32_t>{};
}

}  // namespace

auto split_first(std::u32string_view text, char32_t separator) noexcept -> Fields {
    const auto at = text.find(separator);
    if (at == std::u32string_view::npos) {
        return Fields{text, {}, false};
    }
    return Fields{text.substr(0, at), text.substr(at + 1), true};
}

auto split_last(std::u32string_view text, char32_t separator) noexcept -> Fields {
    const auto at = text.rfind(separator);
    if (at == std::u32string_view::npos) {
        return Fields{{}, text, false};
    }
    return Fields{text.substr(0, at), text.substr(at + 1), true};
}

auto split_command(std::u32string_view data) noexcept
    -> std::optional<std::pair<std::int32_t, std::u32string_view>> {
    std::size_t digits = 0;
    std::int32_t command = 0;
    while (digits < data.size() && data[digits] >= U'0' && data[digits] <= U'9') {
        command = command * 10 + static_cast<std::int32_t>(data[digits] - U'0');
        if (command > kMaxOscCommand) {
            return std::nullopt;  // 荒谬的命令号不可能是我们要认的 OSC
        }
        ++digits;
    }
    if (digits == 0U) {
        return std::nullopt;  // OSC 负载必须以命令号开头
    }
    // 命令号后面不接分隔符就是没有参数（`ESC ] 8 BEL` 一类），按空参数处理而非判非法。
    const auto args = digits < data.size() && data[digits] == U';' ? data.substr(digits + 1)
                                                                   : std::u32string_view{};
    return std::optional<std::pair<std::int32_t, std::u32string_view>>{std::make_pair(command, args)};
}

auto decimal(std::u32string_view text, std::int32_t limit) noexcept -> std::int32_t {
    std::int32_t value = 0;
    std::size_t digits = 0;
    for (const char32_t c : text) {
        if (c < U'0' || c > U'9') {
            break;
        }
        value = value > (limit - 9) / 10 ? limit : value * 10 + static_cast<std::int32_t>(c - U'0');
        ++digits;
    }
    return digits == 0U ? 0 : value;
}

auto decode_base64(std::u32string_view payload) -> std::optional<std::u32string> {
    std::string bytes;
    bytes.reserve(payload.size() / 4U * 3U + 3U);
    std::uint32_t accumulator = 0;
    std::uint32_t bits = 0;
    bool padded = false;
    for (const char32_t c : payload) {
        if (c == U'=') {
            padded = true;  // 填充只做截断，其后不得再有载荷
            continue;
        }
        if (padded) {
            return std::nullopt;
        }
        const auto value = base64_value(c);
        if (!value.has_value()) {
            return std::nullopt;  // 空白与字母表外字符一律判非法：写错剪贴板比不写更糟
        }
        accumulator = (accumulator << 6U) | *value;
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            bytes.push_back(static_cast<char>((accumulator >> bits) & 0xFFU));
        }
    }
    if (bits == 6U) {
        return std::nullopt;  // 只剩一个 6 位组：载荷被截断
    }

    // 载荷是「再编码一层」的 UTF-8 字节，须再走一次解码才是文本；非法字节按替换字符处理。
    std::u32string text;
    Utf8Decoder decoder;
    BufferSink sink{text};
    decoder.feed({reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()}, sink);
    decoder.finish(sink);
    return std::optional<std::u32string>{std::move(text)};
}

}  // namespace borealis::term::osc
