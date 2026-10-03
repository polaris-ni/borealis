/// 测试类型: unit
/// 目标单元: include/borealis/ui/font_choice.h + src/ui/font_choice.cpp
/// 测试说明: 字体族目录的判定腿（`SPEC.FEAT.RENDER.02`）：命中且框架度量判等宽的族原样采用、
///           命中但非等宽与目录里查不到两档都回落内置族，且两档可由 `verdict` 区分（回落后的
///           族名与「用户本来就配了内置族」在字符串上同形，只有判定来源能分开）；
///           族名匹配逐字节精确、区分大小写（与框架 `resolve_faces` 同口径，不自造第二套宽松规则）；
///           空配置名与空目录也一定给出内置族名（装配层尚未枚举时不至无解）。

#include <string>
#include <vector>

#include "borealis/ui/font_choice.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_font_choice {

namespace {

using borealis::ui::FontFamilyChoice;
using borealis::ui::FontFamilyEntry;
using borealis::ui::FontFamilyVerdict;
using borealis::ui::choose_font_family;

/// @brief 目录替身：按框架枚举的既有形态（升序去重 + 度量判定的等宽位）手工构造。
const std::vector<FontFamilyEntry> kCatalog{
    FontFamilyEntry{.family = "Cascadia Code", .monospace = true},
    FontFamilyEntry{.family = "Consolas", .monospace = true},
    FontFamilyEntry{.family = "Courier New", .monospace = true},
    FontFamilyEntry{.family = "Microsoft YaHei", .monospace = false},
    FontFamilyEntry{.family = "Segoe UI", .monospace = false},
};

auto pick(std::string_view configured) -> FontFamilyChoice {
    return choose_font_family(configured, kCatalog);
}

}  // namespace

AURORA_TEST_CASE(configured_monospace_family_is_used_verbatim) {
    const FontFamilyChoice choice = pick("Consolas");
    AURORA_TEST_CHECK_EQ(choice.verdict, FontFamilyVerdict::Configured);
    AURORA_TEST_CHECK_EQ(choice.family, "Consolas");
}

AURORA_TEST_CASE(the_builtin_family_is_configured_not_fallback) {
    // 内置族本身就在目录里：配它时判 `Configured`，否则「回落留痕」会在首屏就谎报一次降级。
    const FontFamilyChoice choice = pick("Cascadia Code");
    AURORA_TEST_CHECK_EQ(choice.verdict, FontFamilyVerdict::Configured);
    AURORA_TEST_CHECK_EQ(choice.family, std::string{borealis::ui::kDefaultMonospaceFamily});
}

AURORA_TEST_CASE(proportional_family_falls_back_because_the_grid_needs_a_column_step) {
    // 名字在目录里、面也解析得到，但度量非等宽：列位与光标会脱钩，故宁可回落（验收线在需求原文）。
    const FontFamilyChoice choice = pick("Segoe UI");
    AURORA_TEST_CHECK_EQ(choice.verdict, FontFamilyVerdict::NotMonospace);
    AURORA_TEST_CHECK_EQ(choice.family, std::string{borealis::ui::kDefaultMonospaceFamily});
}

AURORA_TEST_CASE(uninstalled_family_falls_back_as_unlisted) {
    const FontFamilyChoice choice = pick("FiraCode Nerd Font");
    AURORA_TEST_CHECK_EQ(choice.verdict, FontFamilyVerdict::Unlisted);
    AURORA_TEST_CHECK_EQ(choice.family, std::string{borealis::ui::kDefaultMonospaceFamily});
}

AURORA_TEST_CASE(family_names_are_matched_byte_for_byte) {
    // 框架的族名匹配区分大小写，本件不放宽：放宽就会把 `consolas` 猜成 `Consolas`，
    // 而用户看到的行为与配置的字符串不一致——回落至少是诚实的降级。
    for (std::string_view misspelling : {"consolas", "CONSOLAS", "Consolas ", " Consolas"}) {
        const FontFamilyChoice choice = pick(misspelling);
        AURORA_TEST_CHECK_MSG(choice.verdict == FontFamilyVerdict::Unlisted, std::string{misspelling});
        AURORA_TEST_CHECK_MSG(choice.family == std::string{borealis::ui::kDefaultMonospaceFamily},
                              std::string{misspelling});
    }
}

AURORA_TEST_CASE(an_unset_family_name_falls_back) {
    // 配置缺键回落成空串（`ScopeReader` 的缺省形态），本件把它归 `Unlisted` 而不是特殊一档。
    const FontFamilyChoice choice = pick("");
    AURORA_TEST_CHECK_EQ(choice.verdict, FontFamilyVerdict::Unlisted);
    AURORA_TEST_CHECK_EQ(choice.family, std::string{borealis::ui::kDefaultMonospaceFamily});
}

AURORA_TEST_CASE(an_empty_catalog_still_names_the_builtin_family) {
    // 空目录是「装配层尚未枚举」的降级形态，不是「无可用字体」：内置族由框架内嵌注册，
    // 族名不随目录可得性变化，故回落值恒为内置族名。
    const std::vector<FontFamilyEntry> empty;
    const FontFamilyChoice choice = choose_font_family("Consolas", empty);
    AURORA_TEST_CHECK_EQ(choice.verdict, FontFamilyVerdict::Unlisted);
    AURORA_TEST_CHECK_EQ(choice.family, std::string{borealis::ui::kDefaultMonospaceFamily});
}

AURORA_TEST_CASE(the_verdict_keeps_a_fallback_apart_from_an_equal_explicit_choice) {
    // 两档降级各自留痕：诊断文案要能说清「装了但非等宽」和「压根没这个族」，
    // 而单看族名两者与「用户本就配了内置族」都是 Cascadia Code。
    AURORA_TEST_CHECK_EQ(pick("Cascadia Code").verdict, FontFamilyVerdict::Configured);
    AURORA_TEST_CHECK_EQ(pick("Microsoft YaHei").verdict, FontFamilyVerdict::NotMonospace);
    AURORA_TEST_CHECK_EQ(pick("Cascadia Mono").verdict, FontFamilyVerdict::Unlisted);
}

}  // namespace borealis::test_cases::utest_font_choice
