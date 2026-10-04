/// 测试类型: unit
/// 目标单元: include/borealis/ui/color_text.h + src/ui/color_text.cpp
/// 测试说明: `#RRGGBB` 作为色值唯一的落盘与输入形态（裁决 7.52 的 S14：设置面板的色值输入框
///           与装载侧 `src/config/store.cpp` 收同一个判定）——大写字母、恰 7 字符、通道次序
///           r/g/b、alpha 两侧都不参与，以及全部畸形形态的拒绝。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "borealis/ui/color_text.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_color_text {

namespace {

using borealis::ui::RgbaColor;

/// @brief 只比 RGB：alpha 恒不进这条形态，故断言文本形态而不是整结构。
[[nodiscard]] auto hex_of(std::string_view text) -> std::string {
    const auto parsed = borealis::ui::color_from_hex(text);
    return parsed ? borealis::ui::color_to_hex(*parsed) : std::string{"<rejected>"};
}

}  // namespace

AURORA_TEST_CASE(formats_uppercase_seven_chars) {
    AURORA_TEST_CHECK_EQ(borealis::ui::color_to_hex((RgbaColor{0x12U, 0x34U, 0x56U})), std::string{"#123456"});
    AURORA_TEST_CHECK_EQ(borealis::ui::color_to_hex((RgbaColor{0xABU, 0xCDU, 0xEFU})), std::string{"#ABCDEF"});
    AURORA_TEST_CHECK_EQ(borealis::ui::color_to_hex(RgbaColor{}), std::string{"#000000"});
}

AURORA_TEST_CASE(channel_order_is_red_green_blue) {
    // 位移或通道次序写错只会换出一个「看着像」的色值，故三通道各取唯一档。
    AURORA_TEST_CHECK_EQ(borealis::ui::color_to_hex((RgbaColor{0xFFU, 0x00U, 0x00U})), std::string{"#FF0000"});
    AURORA_TEST_CHECK_EQ(borealis::ui::color_to_hex((RgbaColor{0x00U, 0xFFU, 0x00U})), std::string{"#00FF00"});
    AURORA_TEST_CHECK_EQ(borealis::ui::color_to_hex((RgbaColor{0x00U, 0x00U, 0xFFU})), std::string{"#0000FF"});
}

AURORA_TEST_CASE(alpha_is_not_formatted) {
    AURORA_TEST_CHECK_EQ(borealis::ui::color_to_hex((RgbaColor{1U, 2U, 3U, 0U})),
                         borealis::ui::color_to_hex((RgbaColor{1U, 2U, 3U, 128U})));
}

AURORA_TEST_CASE(parses_case_insensitively) {
    AURORA_TEST_CHECK_EQ(hex_of("#ff0000"), std::string{"#FF0000"});
    AURORA_TEST_CHECK_EQ(hex_of("#Ff00aA"), std::string{"#FF00AA"});
}

AURORA_TEST_CASE(round_trips_channel_edges) {
    // 半字节边界（0x0F / 0xF0）各自过一遍：高低位的取数次序写错时 0x0F 与 0xF0 会互换。
    for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0x0F}, std::uint8_t{0xF0}, std::uint8_t{0xFF}}) {
        const RgbaColor color{value, static_cast<std::uint8_t>(255U - value), value};
        const auto parsed = borealis::ui::color_from_hex(borealis::ui::color_to_hex(color));
        AURORA_TEST_REQUIRE(parsed.has_value());
        AURORA_TEST_CHECK_EQ(parsed->red, color.red);
        AURORA_TEST_CHECK_EQ(parsed->green, color.green);
        AURORA_TEST_CHECK_EQ(parsed->blue, color.blue);
        AURORA_TEST_CHECK_EQ(parsed->alpha, std::uint8_t{255});
    }
}

AURORA_TEST_CASE(rejects_missing_hash) {
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("FF00000"));
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("123456"));
}

AURORA_TEST_CASE(rejects_wrong_length) {
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("#FFF"));        // 缩写形态不收
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("#FFFFFFF"));    // 七位不收
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex(" #FF0000"));    // 前后空白不收
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("#FF0000 "));
}

AURORA_TEST_CASE(rejects_eight_digit_form) {
    // `#RRGGBBAA` 是「面板若自己收就会写出盘外形态」的那一格：装载侧不收，面板也就不该收。
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("#FF000080"));
    AURORA_TEST_CHECK_EQ(borealis::ui::color_to_hex((RgbaColor{255U, 0U, 0U, 128U})).size(), std::size_t{7});
}

AURORA_TEST_CASE(rejects_non_hex_digits) {
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("#GG0000"));
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("#12 456"));
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("#-12345"));
}

AURORA_TEST_CASE(rejects_functional_and_empty_forms) {
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("rgb(255,0,0)"));
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex(""));  // 空串不得触 front()
    AURORA_TEST_CHECK_FALSE(borealis::ui::color_from_hex("#"));
}

}  // namespace borealis::test_cases::utest_color_text
