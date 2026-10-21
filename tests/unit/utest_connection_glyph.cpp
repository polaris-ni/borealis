/// 测试类型: unit
/// 目标单元: src/ui/connection_glyph.{h,cpp}（无 au:: 类型）纯字形表
/// 测试说明: 守住 `SPEC.FEAT.WS.04` 那张「连接类型 → 图标字形」的唯一表——四档逐档读数、
///           四档两两不等（这条专挡「把串口折成本地」的退化，即裁决 7.110 D12① 的原样复活）、
///           每枚字形是一个可打印 ASCII 字符（AGENTS.md §4.3 第 14 条与不引图标库的既定形态）。
///           图标真上屏与槽位几何属 itest_tab_bar（稿 §2 判据 ②～⑤）。

#include "ui/connection_glyph.h"

#include <array>
#include <cstdint>
#include <string_view>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_connection_glyph {

namespace {

constexpr std::array<conn::ConnectionType, 4> kAllTypes{
    conn::ConnectionType::Local, conn::ConnectionType::Ssh, conn::ConnectionType::Serial,
    conn::ConnectionType::Telnet};

// 可打印 ASCII 区间（宏实参里不写带花括号的 cast：`#__VA_ARGS__` 那一支把它折成了非法记号）。
constexpr std::uint8_t kFirstPrintable{0x20};
constexpr std::uint8_t kLastPrintable{0x7E};

}  // namespace

AURORA_TEST_CASE(each_connection_type_lands_its_own_glyph) {
    // 这四行就是稿 §2 判据 ① 的表体：改任何一枚字形都要同批改这里与稿面（头注那条承诺）。
    AURORA_TEST_CHECK_TRUE(ui::connection_type_glyph(conn::ConnectionType::Local) == ">");
    AURORA_TEST_CHECK_TRUE(ui::connection_type_glyph(conn::ConnectionType::Ssh) == "#");
    AURORA_TEST_CHECK_TRUE(ui::connection_type_glyph(conn::ConnectionType::Serial) == "S");
    AURORA_TEST_CHECK_TRUE(ui::connection_type_glyph(conn::ConnectionType::Telnet) == "T");
}

AURORA_TEST_CASE(no_two_connection_types_share_a_glyph) {
    // 逐对：4×4 自反比较，只跳同档。侧栏那枚两值函数曾让 Serial/Telnet 落进 `">"`，这一例是
    // 那条口径不再复活的证人——表里任何两枚撞车即在此转红。
    for (const auto a : kAllTypes) {
        for (const auto b : kAllTypes) {
            if (a == b) {
                continue;
            }
            AURORA_TEST_CHECK_MSG(ui::connection_type_glyph(a) != ui::connection_type_glyph(b),
                                  "two connection types landed the same glyph");
        }
    }
}

AURORA_TEST_CASE(every_glyph_is_one_printable_ascii_character) {
    // 字形形态的上界：单码点、可打印 ASCII。标签栏左槽按 `measure_width` 定宽（稿 §0 的 F9），
    // 多字符或控制字符都会让槽宽与实绘宽度分叉。
    for (const auto type : kAllTypes) {
        const auto glyph = ui::connection_type_glyph(type);
        AURORA_TEST_CHECK_EQ(glyph.size(), std::size_t{1U});
        if (glyph.empty()) {
            continue;  // 长度已转红，别再读越界字符。
        }
        const auto byte = static_cast<std::uint8_t>(glyph.front());
        AURORA_TEST_CHECK_GE(byte, kFirstPrintable);
        AURORA_TEST_CHECK_LE(byte, kLastPrintable);
    }
}

}  // namespace borealis::test_cases::utest_connection_glyph
