/// 测试类型: unit
/// 目标单元: include/borealis/term/paste.h + src/term/paste.cpp
/// 测试说明: 粘贴处置计划（`SPEC.FEAT.INTERACT.03` 的发送侧）：换行三策略的块切分与行尾形态、
///           CR / LF / CRLF 的断点判定（CRLF 只算一个换行）、bracketed paste 的原样包裹与
///           「策略与节流一并让位」、多行警告判据取自待发结果、逐行节流的块间隔从第二块起生效，
///           以及 `config::PasteNewlinePolicy` 是 `term::PasteNewlinePolicy` 的别名而非第二份取值表。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

#include "borealis/config/settings.h"
#include "borealis/term/paste.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_paste {

namespace {

using borealis::config::TerminalSettings;
using borealis::term::LineEnding;
using borealis::term::PasteChunk;
using borealis::term::PasteNewlinePolicy;
using borealis::term::PasteOptions;
using borealis::term::PastePlan;
using borealis::term::plan_paste;

constexpr auto kIntervalMs = 10;

/// @brief 只给换行策略的口径：间隔 10 ms，行尾按需另给。
[[nodiscard]] auto options(PasteNewlinePolicy newline = PasteNewlinePolicy::AsIs,
                           LineEnding ending = LineEnding::Lf) -> PasteOptions {
    return PasteOptions{.newline = newline,
                        .line_ending = ending,
                        .line_interval = std::chrono::milliseconds{kIntervalMs}};
}

/// @brief 块文本逐块相等。
[[nodiscard]] auto chunks_are(const PastePlan &plan, std::vector<std::u32string> expected) -> bool {
    if (plan.chunks.size() != expected.size()) {
        return false;
    }
    return std::equal(plan.chunks.begin(), plan.chunks.end(), expected.begin(),
                      [](const PasteChunk &chunk, const std::u32string &text) { return chunk.text == text; });
}

/// @brief 把块序列折成 ASCII 描述（CR/LF/ESC 写成转义名），失败信息落在内容而不是下标上。
[[nodiscard]] auto describe(const PastePlan &plan) -> std::string {
    auto out = std::string{};
    for (const auto &chunk : plan.chunks) {
        if (!out.empty()) {
            out += " | ";
        }
        for (const auto cp : chunk.text) {
            switch (cp) {
            case U'\r':
                out += "\\r";
                break;
            case U'\n':
                out += "\\n";
                break;
            case U'\x1B':
                out += "ESC";
                break;
            default:
                out += cp < 0x7FU ? std::string(1, static_cast<char>(cp)) : std::string{"?"};
            }
        }
    }
    return out;
}

}  // namespace

AURORA_TEST_CASE(verbatim_policy_splits_per_line_and_keeps_the_original_separators) {
    const auto plan = plan_paste(U"a\r\nb\nc", false, options());
    AURORA_TEST_CHECK(chunks_are(plan, {U"a\r\n", U"b\n", U"c"}));
    AURORA_TEST_CHECK_EQ(plan.line_breaks, 2U);
    AURORA_TEST_CHECK(plan.multiline);
    AURORA_TEST_CHECK_FALSE(plan.bracketed);
}

AURORA_TEST_CASE(single_line_paste_warns_nobody_and_sends_one_chunk) {
    const auto plan = plan_paste(U"ls -la", false, options());
    AURORA_TEST_CHECK(chunks_are(plan, {U"ls -la"}));
    AURORA_TEST_CHECK_EQ(plan.line_breaks, 0U);
    AURORA_TEST_CHECK_FALSE(plan.multiline);
}

AURORA_TEST_CASE(trailing_newline_is_a_break_and_still_warns) {
    // 尾随换行意味着粘完即提交执行，即使只有一行内容也要警告。
    const auto plan = plan_paste(U"rm -rf /\n", false, options());
    AURORA_TEST_CHECK(chunks_are(plan, {U"rm -rf /\n"}));
    AURORA_TEST_CHECK_EQ(plan.chunks.size(), 1U);
    AURORA_TEST_CHECK_EQ(plan.line_breaks, 1U);
    AURORA_TEST_CHECK(plan.multiline);
}

AURORA_TEST_CASE(blank_lines_between_content_stay_their_own_chunks) {
    // 中间的空行是内容的一部分，切块后独立成一块，不得与相邻行合并。
    const auto plan = plan_paste(U"a\n\nb", false, options());
    AURORA_TEST_CHECK(chunks_are(plan, {U"a\n", U"\n", U"b"}));
    AURORA_TEST_CHECK_EQ(plan.line_breaks, 2U);
}

AURORA_TEST_CASE(bracketed_paste_wraps_the_text_untouched_and_throttles_nothing) {
    const auto plan = plan_paste(U"a\nb", true, options(PasteNewlinePolicy::Filter));
    AURORA_TEST_CHECK(chunks_are(plan, {U"\x1B[200~a\nb\x1B[201~"}));
    AURORA_TEST_CHECK(plan.bracketed);
    AURORA_TEST_CHECK_FALSE(plan.multiline);
    AURORA_TEST_CHECK_EQ(plan.chunks.front().delay.count(), 0);
    AURORA_TEST_CHECK_EQ(describe(plan), std::string{"ESC[200~a\\nbESC[201~"});
}

AURORA_TEST_CASE(filter_policy_collapses_every_line_and_clears_the_warning) {
    // 待发结果里没有行尾了，警告判据随之为假——它看的是要发出去什么，不是剪贴板里有什么。
    const auto plan = plan_paste(U"a\r\nb\rc\n", false, options(PasteNewlinePolicy::Filter));
    AURORA_TEST_CHECK(chunks_are(plan, {U"abc"}));
    AURORA_TEST_CHECK_EQ(plan.line_breaks, 3U);
    AURORA_TEST_CHECK_FALSE(plan.multiline);
}

AURORA_TEST_CASE(convert_policy_rewrites_each_break_to_the_chosen_ending) {
    const auto crlf = plan_paste(U"a\nb\rc\r\nd", false, options(PasteNewlinePolicy::Convert, LineEnding::Crlf));
    AURORA_TEST_CHECK(chunks_are(crlf, {U"a\r\n", U"b\r\n", U"c\r\n", U"d"}));

    const auto cr = plan_paste(U"a\nb", false, options(PasteNewlinePolicy::Convert, LineEnding::Cr));
    AURORA_TEST_CHECK(chunks_are(cr, {U"a\r", U"b"}));

    // 尾随换行也换：那一行仍会被执行，只是行尾形态由设备决定。
    const auto lf = plan_paste(U"a\r\n", false, options(PasteNewlinePolicy::Convert, LineEnding::Lf));
    AURORA_TEST_CHECK(chunks_are(lf, {U"a\n"}));
    AURORA_TEST_CHECK(lf.multiline);
}

AURORA_TEST_CASE(throttle_awaits_before_every_chunk_but_the_first) {
    const auto plan = plan_paste(U"a\nb\nc", false, options());
    AURORA_TEST_CHECK_EQ(plan.chunks.size(), 3U);
    AURORA_TEST_CHECK_EQ(plan.chunks.at(0).delay.count(), 0);
    AURORA_TEST_CHECK_EQ(plan.chunks.at(1).delay.count(), kIntervalMs);
    AURORA_TEST_CHECK_EQ(plan.chunks.at(2).delay.count(), kIntervalMs);

    const auto unpaced = plan_paste(U"a\nb\nc", false,
                                   PasteOptions{.newline = PasteNewlinePolicy::AsIs,
                                                .line_ending = LineEnding::Lf,
                                                .line_interval = std::chrono::milliseconds{0}});
    AURORA_TEST_CHECK(chunks_are(unpaced, {U"a\n", U"b\n", U"c"}));
    AURORA_TEST_CHECK(std::all_of(unpaced.chunks.begin(), unpaced.chunks.end(),
                                  [](const PasteChunk &chunk) { return chunk.delay.count() == 0; }));
}

AURORA_TEST_CASE(empty_text_sends_nothing_even_when_bracketed) {
    const auto plan = plan_paste(U"", true, options());
    AURORA_TEST_CHECK(plan.chunks.empty());
    AURORA_TEST_CHECK(plan.bracketed);
    AURORA_TEST_CHECK_FALSE(plan.multiline);
    AURORA_TEST_CHECK_EQ(plan.line_breaks, 0U);
}

AURORA_TEST_CASE(non_ascii_text_and_unicode_separators_are_not_breaks) {
    // 断点只认 CR / LF：U+0085、U+2028 一类 Unicode 行分隔符在终端网格里不是换行，
    // 把它们当断点就会把一行命令拆成两条依次执行。
    const auto text = std::u32string{U"head\x1C\x85\u2028tail"};
    const auto plan = plan_paste(text, false, options());
    AURORA_TEST_CHECK(chunks_are(plan, {text}));
    AURORA_TEST_CHECK_EQ(plan.line_breaks, 0U);
    AURORA_TEST_CHECK_FALSE(plan.multiline);
}

AURORA_TEST_CASE(config_field_is_an_alias_of_the_paste_enum_not_a_second_table) {
    const auto terminal = TerminalSettings{};
    AURORA_TEST_CHECK((std::is_same_v<decltype(terminal.paste_newlines), borealis::term::PasteNewlinePolicy>));
    AURORA_TEST_CHECK_EQ(terminal.paste_newlines, PasteNewlinePolicy::AsIs);
}

}  // namespace borealis::test_cases::utest_paste
