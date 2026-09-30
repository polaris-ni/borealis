/// 测试类型: unit
/// 目标单元: include/borealis/term/charset.h
/// 测试说明: 字符集指派与「码点 → 呈现码点」映射——DEC Special Graphics 的 0x5F–0x7E 全表逐项断言、
///           区间外码点与 ASCII 槽位不被框线表误伤、以及指派终结符到字符集的查表（未支持返回 false）。

#include <cstddef>
#include <cstdint>
#include <vector>

#include "borealis/term/charset.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_charset {

namespace {

using borealis::term::Charset;
using borealis::term::charset_from_designator;
using borealis::term::map_charset;

/// @brief DEC Special Graphics 的重定义区间下界与上界（VT100 定义）。
constexpr char32_t kGraphicsFirst = U'\x5F';
constexpr char32_t kGraphicsLast = U'\x7E';

/// @brief 0x5F–0x7E 共 32 个码点在 DEC Special Graphics 下的呈现目标。
constexpr char32_t kDecGraphics[] = {
    U'\x00A0', U'\x25C6', U'\x2592', U'\x2409', U'\x240C', U'\x240D', U'\x240A', U'\x00B0',
    U'\x00B1', U'\x2424', U'\x240B', U'\x2518', U'\x2510', U'\x250C', U'\x2514', U'\x253C',
    U'\x23BA', U'\x23BB', U'\x2500', U'\x23BC', U'\x23BD', U'\x251C', U'\x2524', U'\x2534',
    U'\x252C', U'\x2502', U'\x2264', U'\x2265', U'\x03C0', U'\x2260', U'\x00A3', U'\x00B7',
};

}  // namespace

AURORA_TEST_CASE(dec_special_graphics_full_table) {
    // 框线字符集把 0x5F–0x7E 整段重定义：逐项断言，避免只抽查而漏掉个别错位。
    AURORA_TEST_CHECK_EQ(std::size(kDecGraphics),
                         static_cast<std::size_t>(kGraphicsLast - kGraphicsFirst + 1));
    for (char32_t cp = kGraphicsFirst; cp <= kGraphicsLast; ++cp) {
        const auto index = static_cast<std::size_t>(cp - kGraphicsFirst);
        AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(map_charset(Charset::DecSpecialGraphics, cp)),
                             static_cast<std::uint32_t>(kDecGraphics[index]));
    }
}

AURORA_TEST_CASE(out_of_range_code_points_pass_through) {
    // 区间外一律原样返回：整表替换会让字符集里的字母与 CJK 一起变形，那是错的。
    const std::vector<char32_t> samples{
        U'\x00',  U'\x41', U'\x5A', U'\x5E', U'\x7F',  U'\xE9',
        U'\x4E2D', static_cast<char32_t>(0x1F600), static_cast<char32_t>(0x10FFFF),
    };
    for (const char32_t cp : samples) {
        AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(map_charset(Charset::DecSpecialGraphics, cp)),
                             static_cast<std::uint32_t>(cp));
    }
}

AURORA_TEST_CASE(ascii_slot_is_identity) {
    // ASCII 槽位是恒等映射，即便码点落在框线表的重定义区间内也不改写。
    for (char32_t cp = U'\x20'; cp <= U'\x7E'; ++cp) {
        AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(map_charset(Charset::Ascii, cp)),
                             static_cast<std::uint32_t>(cp));
    }
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(map_charset(Charset::Ascii, U'\x4E2D')),
                         static_cast<std::uint32_t>(U'\x4E2D'));
}

AURORA_TEST_CASE(designator_lookup) {
    // 终结符 → 字符集：`0` 为 DEC 框线、`B` 为 ASCII，其余未实现故返回 false。
    Charset charset = Charset::Ascii;
    AURORA_TEST_CHECK(charset_from_designator(U'0', charset));
    AURORA_TEST_CHECK_EQ(static_cast<int>(charset), static_cast<int>(Charset::DecSpecialGraphics));

    AURORA_TEST_CHECK(charset_from_designator(U'B', charset));
    AURORA_TEST_CHECK_EQ(static_cast<int>(charset), static_cast<int>(Charset::Ascii));

    for (const char32_t final_byte : {U'A', U'1', U'<', U'%'}) {
        AURORA_TEST_CHECK_FALSE(charset_from_designator(final_byte, charset));
    }
}

}  // namespace borealis::test_cases::utest_charset
