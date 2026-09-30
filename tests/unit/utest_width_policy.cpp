/// 测试类型: unit
/// 目标单元: include/borealis/term/width.h
/// 测试说明: 宽度判定接缝的取值契约——生产实现按 Unicode East Asian Width 与 General_Category
///           给出 0/1/2，Ambiguous 类随注入口径取 1 或 2，零宽优先于宽度；
///           `SingleWidthPolicy` 恒为 1（SPEC.FEAT.TERM.08、裁决 7.20）。

#include <cstdint>

#include "borealis/term/width.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_width_policy {

using borealis::term::AmbiguousWidth;
using borealis::term::SingleWidthPolicy;
using borealis::term::UnicodeWidthPolicy;

AURORA_TEST_CASE(unicode_policy_gives_two_cells_for_wide_and_fullwidth) {
    UnicodeWidthPolicy policy;
    // W 类（CJK 表意、emoji）与 F 类（全角）都占两格，且与 Ambiguous 口径无关。
    for (const char32_t code_point : {U'\x4E2D', U'\xAC00', U'\x3000', U'\xFF21'}) {
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Narrow), 2U);
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Wide), 2U);
    }
}

AURORA_TEST_CASE(unicode_policy_maps_ambiguous_through_the_injected_mode) {
    UnicodeWidthPolicy policy;
    // 度符号、箱线字符、拉丁重音字母：同一码点在两种口径下分别是 1 与 2（裁决 7.15 的 profile 覆盖）。
    for (const char32_t code_point : {U'\x00B0', U'\x2500', U'\x00E9', U'\x2192'}) {
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Narrow), 1U);
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Wide), 2U);
    }
}

AURORA_TEST_CASE(unicode_policy_gives_zero_before_checking_width) {
    UnicodeWidthPolicy policy;
    // 组合符号与格式字符在 UCD 里同时带 Ambiguous 标记：零宽判定必须先于宽度判定，
    // 否则双宽口径下的重音符会占 2 格并把基础格挤成半格。
    for (const char32_t code_point : {U'\x0301', U'\xFE00', U'\x200B', U'\x00AD'}) {
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Narrow), 0U);
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Wide), 0U);
    }
    // Mc（占格组合符）不属零宽类，仍占一格。
    AURORA_TEST_CHECK_EQ(policy.width_of(U'\x0903', AmbiguousWidth::Wide), 1U);
}

AURORA_TEST_CASE(unicode_policy_gives_one_cell_for_neutral_text) {
    UnicodeWidthPolicy policy;
    for (const char32_t code_point : {U'A', U'~', U'\x0041', U'\x1160', U'\x00A9'}) {
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Narrow), 1U);
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Wide), 1U);
    }
}

AURORA_TEST_CASE(single_policy_is_the_constant_injection_value) {
    SingleWidthPolicy policy;
    for (const char32_t code_point : {U'A', U'\x4E2D', U'\x0301'}) {
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Narrow), 1U);
        AURORA_TEST_CHECK_EQ(policy.width_of(code_point, AmbiguousWidth::Wide), 1U);
    }
}

}  // namespace borealis::test_cases::utest_width_policy
