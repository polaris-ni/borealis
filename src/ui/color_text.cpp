#include "borealis/ui/color_text.h"

namespace borealis::ui {

auto color_to_hex(const RgbaColor &color) -> std::string {
    static constexpr std::string_view kDigits = "0123456789ABCDEF";
    std::string text{'#'};
    for (const std::uint8_t channel : {color.red, color.green, color.blue}) {
        text.push_back(kDigits[(channel >> 4U) & 0x0FU]);
        text.push_back(kDigits[channel & 0x0FU]);
    }
    return text;
}

auto color_from_hex(std::string_view text) -> std::optional<RgbaColor> {
    static constexpr auto digit = [](char c) -> std::optional<std::uint8_t> {
        if (c >= '0' && c <= '9') {
            return static_cast<std::uint8_t>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<std::uint8_t>(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<std::uint8_t>(c - 'A' + 10);
        }
        return std::nullopt;
    };

    if (text.size() != 7U || text.front() != '#') {
        return std::nullopt;
    }
    std::uint8_t channels[3]{};
    for (std::size_t index = 0; index < 3U; ++index) {
        const auto high = digit(text[1 + index * 2]);
        const auto low = digit(text[2 + index * 2]);
        if (!high || !low) {
            return std::nullopt;
        }
        channels[index] = static_cast<std::uint8_t>((*high << 4U) | *low);
    }
    return RgbaColor{channels[0], channels[1], channels[2]};
}

}  // namespace borealis::ui
