/// 测试类型: unit
/// 目标单元: include/borealis/ui/quick_select.h + src/ui/quick_select.cpp
/// 测试说明: quick select 的行扫描（URL 不查协议白名单、尾部标点与不配对右括号修剪、平衡括号保留；
///           路径的根/家目录/显式相对/盘符/UNC/裸相对六种前缀、日期与单斜杠片段不产候选；十六进制
///           哈希的 7–64 位长度闸、必须含 a–f、词内边界；三档按 URL → 路径 → 哈希去重叠且结果按
///           起始列升序）、文本提取（双宽延续格跳过、组合符号随基础格）与标签分配（单字符/双字符
///           两档不混用、默认与自定义上限截断、空前缀与前缀/完整命中的判定）。
///           （SPEC.FEAT.INTERACT.02 的 quick select 延后子项）

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/ui/quick_select.h"
#include "borealis/ui/selection.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_quick_select {

namespace {

using borealis::grid::Cell;
using borealis::grid::Row;
using borealis::ui::assign_quick_select_labels;
using borealis::ui::kQuickSelectMaxCandidates;
using borealis::ui::QuickSelectCandidate;
using borealis::ui::QuickSelectKind;
using borealis::ui::quick_select_label_matches;
using borealis::ui::quick_select_lookup;
using borealis::ui::RowSpan;
using borealis::ui::scan_quick_select_row;

/// @brief 造一行 ASCII 文本，其余列留空白。
[[nodiscard]] auto text_row(std::string_view text, std::size_t columns) -> Row {
    Row row{columns};
    for (std::size_t i = 0; i < text.size(); ++i) {
        Cell cell{};
        cell.code_point = static_cast<char32_t>(static_cast<unsigned char>(text[i]));
        row.set(i, cell);
    }
    return row;
}

/// @brief 扫一段 ASCII 文本，列数按字节数给（只用于全 ASCII 的用例）。
[[nodiscard]] auto scan(std::string_view text, std::size_t storage_row = 0U)
    -> std::vector<QuickSelectCandidate> {
    return scan_quick_select_row(text_row(text, text.size()), storage_row);
}

/// @brief 在行的某列写一个双宽字符（基础格 + 延续格）。
auto put_wide(Row &row, std::size_t column, char32_t code_point) -> void {
    Cell base{};
    base.code_point = code_point;
    base.width = 2;
    row.set(column, base);

    Cell continuation{};
    continuation.code_point = 0;
    continuation.width = 0;
    continuation.flags = borealis::grid::kFlagWideContinuation;
    row.set(column + 1U, continuation);
}

/// @brief 造 @p count 条只关心标签的候选（文本即序号）。
[[nodiscard]] auto numbered_candidates(std::size_t count) -> std::vector<QuickSelectCandidate> {
    std::vector<QuickSelectCandidate> candidates;
    for (std::size_t index = 0; index < count; ++index) {
        QuickSelectCandidate candidate;
        candidate.span = RowSpan{0U, index * 2U, index * 2U + 1U};
        candidate.kind = QuickSelectKind::Url;
        candidate.text = std::to_string(index);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

/// @brief 把候选的标签拼成逗号分隔的一串，便于一次断言整张表。
[[nodiscard]] auto labels_of(const std::vector<QuickSelectCandidate> &candidates) -> std::string {
    std::string joined;
    for (const auto &candidate : candidates) {
        if (!joined.empty()) {
            joined.push_back(',');
        }
        joined += candidate.label;
    }
    return joined;
}

}  // namespace

AURORA_TEST_CASE(scan_finds_urls_with_any_scheme_and_the_www_prefix) {
    const auto candidates = scan("open ftp://files.example.net/pub and www.example.com now");
    AURORA_TEST_REQUIRE(candidates.size() == 2U);
    AURORA_TEST_CHECK(candidates[0].kind == QuickSelectKind::Url);
    AURORA_TEST_CHECK_STREQ(candidates[0].text, std::string{"ftp://files.example.net/pub"});
    AURORA_TEST_CHECK(candidates[1].kind == QuickSelectKind::Url);
    AURORA_TEST_CHECK_STREQ(candidates[1].text, std::string{"www.example.com"});

    // 自造 scheme 同样认（只复制、不打开，故不查协议白名单）：`+`、`-`、`.` 都在 scheme 体内。
    AURORA_TEST_CHECK_STREQ(scan("x-custom+v1.2://host/x")[0].text,
                            std::string{"x-custom+v1.2://host/x"});
}

AURORA_TEST_CASE(scan_trims_url_tail_punctuation_and_unbalanced_closers) {
    // 句尾句号不入候选；包着 URL 的括号不成对，故 `)` 剥掉；URL 自身的配对括号保留。
    AURORA_TEST_CHECK_STREQ(scan("visit https://example.com/a.")[0].text,
                            std::string{"https://example.com/a"});
    AURORA_TEST_CHECK_STREQ(scan("see (https://example.com/b)")[0].text,
                            std::string{"https://example.com/b"});
    AURORA_TEST_CHECK_STREQ(scan("https://en.wikipedia.org/wiki/Foo_(bar)")[0].text,
                            std::string{"https://en.wikipedia.org/wiki/Foo_(bar)"});
    // 尾部多层（标点叠在右括号之外）要剥到稳定，而不是只剥一层。
    AURORA_TEST_CHECK_STREQ(scan("go https://example.com/c).")[0].text,
                            std::string{"https://example.com/c"});
}

AURORA_TEST_CASE(scan_requires_a_lettered_scheme_and_the_double_slash) {
    // 协议名须以字母起头；`://` 之后须还有字符。两条都不产 URL 档候选。
    AURORA_TEST_CHECK(scan("1://example.com/x").empty());
    AURORA_TEST_CHECK(scan("http://").empty());

    // 单个斜杠不成立 URL，但冒号后的 `/example.com/x` 是一段显式根路径：按路径档收下，
    // 且不得冒充 URL（这是「残缺 URL 不降级」在单斜杠形态下的实际落点）。
    const auto split = scan("http:/example.com/x");
    AURORA_TEST_REQUIRE(split.size() == 1U);
    AURORA_TEST_CHECK(split[0].kind == QuickSelectKind::Path);
    AURORA_TEST_CHECK_STREQ(split[0].text, std::string{"/example.com/x"});
}

AURORA_TEST_CASE(scan_finds_explicit_paths_of_every_prefix_kind) {
    AURORA_TEST_CHECK_STREQ(scan("/usr/local/bin/tool")[0].text, std::string{"/usr/local/bin/tool"});
    AURORA_TEST_CHECK_STREQ(scan("./build/app")[0].text, std::string{"./build/app"});
    AURORA_TEST_CHECK_STREQ(scan("../src/main.cpp")[0].text, std::string{"../src/main.cpp"});
    AURORA_TEST_CHECK_STREQ(scan("~/notes/todo.txt")[0].text, std::string{"~/notes/todo.txt"});
    AURORA_TEST_CHECK_STREQ(scan("~alice/docs/x")[0].text, std::string{"~alice/docs/x"});
    AURORA_TEST_CHECK_STREQ(scan("C:\\Users\\me\\file.txt")[0].text, std::string{"C:\\Users\\me\\file.txt"});
    AURORA_TEST_CHECK_STREQ(scan("C:/Users/me")[0].text, std::string{"C:/Users/me"});
    AURORA_TEST_CHECK_STREQ(scan("\\\\server\\share\\dir")[0].text, std::string{"\\\\server\\share\\dir"});
}

AURORA_TEST_CASE(scan_accepts_bare_relative_paths_only_with_two_separators_and_a_name) {
    AURORA_TEST_CHECK_STREQ(scan("src/ui/quick_select.cpp")[0].text, std::string{"src/ui/quick_select.cpp"});
    // 日期形态全由数字与分隔符组成，不标；单斜杠片段与词内斜杠（and/or）也不标。
    AURORA_TEST_CHECK(scan("2026/10/22").empty());
    AURORA_TEST_CHECK(scan("a/b").empty());
    AURORA_TEST_CHECK(scan("and/or").empty());
}

AURORA_TEST_CASE(scan_finds_short_hex_hashes_and_rejects_lookalikes) {
    AURORA_TEST_CHECK_STREQ(scan("commit 4f3a9c1d2e")[0].text, std::string{"4f3a9c1d2e"});
    AURORA_TEST_CHECK_STREQ(scan("deadbeef")[0].text, std::string{"deadbeef"});
    AURORA_TEST_CHECK_STREQ(scan("hash 4f3a9c1d2.")[0].text, std::string{"4f3a9c1d2"});

    AURORA_TEST_CHECK(scan("short 4f3a9c").empty());          // 6 位太短
    AURORA_TEST_CHECK(scan("digits 1234567890").empty());     // 无 a–f
    AURORA_TEST_CHECK(scan("word x4f3a9c1d2").empty());       // 词内边界
    AURORA_TEST_CHECK(scan(std::string{"h "} + std::string(65U, 'a')).empty());  // 65 位太长
}

AURORA_TEST_CASE(scan_resolves_overlaps_by_url_then_path_then_hash) {
    // URL 覆盖其内的路径与哈希片段：一条候选，文本与 URL 全等。
    const auto url = scan("https://example.com/4f3a9c1d2e/path");
    AURORA_TEST_REQUIRE(url.size() == 1U);
    AURORA_TEST_CHECK(url[0].kind == QuickSelectKind::Url);
    AURORA_TEST_CHECK_STREQ(url[0].text, std::string{"https://example.com/4f3a9c1d2e/path"});

    // 路径覆盖其内的哈希段。
    const auto path = scan("/tmp/4f3a9c1d2e/file");
    AURORA_TEST_REQUIRE(path.size() == 1U);
    AURORA_TEST_CHECK(path[0].kind == QuickSelectKind::Path);
    AURORA_TEST_CHECK_STREQ(path[0].text, std::string{"/tmp/4f3a9c1d2e/file"});

    // 互不重叠的候选按起始列升序，而不是按档位分组。
    const auto mixed = scan("x 4f3a9c1d2e /etc/hosts");
    AURORA_TEST_REQUIRE(mixed.size() == 2U);
    AURORA_TEST_CHECK(mixed[0].kind == QuickSelectKind::Hash);
    AURORA_TEST_CHECK(mixed[1].kind == QuickSelectKind::Path);
}

AURORA_TEST_CASE(scan_extracts_text_through_wide_cells_and_combining_marks) {
    // `/中文/x`：两个双宽字符各占两格，提取只出一次且不带延续格的空码点。
    Row wide{8U};
    Cell slash{};
    slash.code_point = U'/';
    wide.set(0U, slash);
    put_wide(wide, 1U, char32_t{0x4E2D});
    put_wide(wide, 3U, char32_t{0x6587});
    Cell second_slash{};
    second_slash.code_point = U'/';
    wide.set(5U, second_slash);
    Cell letter{};
    letter.code_point = U'x';
    wide.set(6U, letter);

    const auto candidates = scan_quick_select_row(wide, 7U);
    AURORA_TEST_REQUIRE(candidates.size() == 1U);
    AURORA_TEST_CHECK_STREQ(candidates[0].text, std::string{"/\xE4\xB8\xAD\xE6\x96\x87/x"});
    AURORA_TEST_CHECK_EQ(candidates[0].span.row, std::size_t{7});
    AURORA_TEST_CHECK_EQ(candidates[0].span.first_column, std::size_t{0});
    AURORA_TEST_CHECK_EQ(candidates[0].span.last_column, std::size_t{7});
    AURORA_TEST_CHECK(candidates[0].label.empty());

    // `/cafe` + U+0301：组合尖音符跟基础格一起进文本，且只进一次。
    Row combining{8U};
    for (std::size_t i = 0; i < 5U; ++i) {
        Cell cell{};
        cell.code_point = static_cast<char32_t>(static_cast<unsigned char>(std::string_view{"/cafe"}[i]));
        combining.set(i, cell);
    }
    combining.attach_combining(4U, char32_t{0x0301});
    AURORA_TEST_CHECK_STREQ(scan_quick_select_row(combining, 0U)[0].text, std::string{"/cafe\xCC\x81"});
}

AURORA_TEST_CASE(scan_ignores_blank_rows_and_assigns_the_storage_row) {
    AURORA_TEST_CHECK(scan("").empty());
    AURORA_TEST_CHECK(scan("   ").empty());

    const auto candidates = scan_quick_select_row(text_row("/etc/hosts", 12U), 123U);
    AURORA_TEST_REQUIRE(candidates.size() == 1U);
    AURORA_TEST_CHECK_EQ(candidates[0].span.row, std::size_t{123});
    AURORA_TEST_CHECK_EQ(candidates[0].span.first_column, std::size_t{0});
    AURORA_TEST_CHECK_EQ(candidates[0].span.last_column, std::size_t{10});
}

AURORA_TEST_CASE(labels_switch_from_one_letter_to_two_letters_at_the_alphabet_size) {
    auto nine = numbered_candidates(9U);
    assign_quick_select_labels(nine);
    AURORA_TEST_CHECK_STREQ(labels_of(nine), std::string{"a,s,d,f,g,h,j,k,l"});

    // 第 10 条起全部改双字符：首字符分组、次字符枚举，两档不混用。
    auto ten = numbered_candidates(10U);
    assign_quick_select_labels(ten);
    AURORA_TEST_CHECK_STREQ(labels_of(ten), std::string{"aa,as,ad,af,ag,ah,aj,ak,al,sa"});
}

AURORA_TEST_CASE(labels_truncate_at_the_default_and_custom_caps) {
    auto many = numbered_candidates(90U);
    assign_quick_select_labels(many);
    AURORA_TEST_CHECK_EQ(many.size(), kQuickSelectMaxCandidates);
    AURORA_TEST_CHECK_EQ(many.size(), std::size_t{81});
    AURORA_TEST_CHECK_STREQ(many.back().label, std::string{"ll"});
    AURORA_TEST_CHECK_STREQ(many.front().label, std::string{"aa"});

    auto limited = numbered_candidates(10U);
    assign_quick_select_labels(limited, "asdfghjkl", 5U);
    AURORA_TEST_REQUIRE(limited.size() == 5U);
    AURORA_TEST_CHECK_STREQ(labels_of(limited), std::string{"a,s,d,f,g"});

    // 自定义字母集：容量是 size²，超出容量的部分连同 limit 一起截掉。
    auto narrow = numbered_candidates(6U);
    assign_quick_select_labels(narrow, "ab");
    AURORA_TEST_REQUIRE(narrow.size() == 4U);
    AURORA_TEST_CHECK_STREQ(labels_of(narrow), std::string{"aa,ab,ba,bb"});

    auto empty_alphabet = numbered_candidates(3U);
    for (auto &candidate : empty_alphabet) {
        candidate.label = "z";
    }
    assign_quick_select_labels(empty_alphabet, "");
    AURORA_TEST_REQUIRE(empty_alphabet.size() == 3U);
    AURORA_TEST_CHECK(empty_alphabet[0].label.empty());
    AURORA_TEST_CHECK(empty_alphabet[2].label.empty());
}

AURORA_TEST_CASE(label_matching_takes_prefixes_and_lookup_needs_the_whole_label) {
    auto candidates = numbered_candidates(3U);
    candidates[0].label = "aa";
    candidates[1].label = "as";
    candidates[2].label = "sa";

    AURORA_TEST_CHECK_TRUE(quick_select_label_matches("aa", ""));
    AURORA_TEST_CHECK_TRUE(quick_select_label_matches("aa", "a"));
    AURORA_TEST_CHECK_TRUE(quick_select_label_matches("aa", "aa"));
    AURORA_TEST_CHECK_FALSE(quick_select_label_matches("aa", "aas"));
    AURORA_TEST_CHECK_FALSE(quick_select_label_matches("sa", "a"));

    // 前缀阶段（含空串）没有完整命中，调用方据此继续等下一键。
    AURORA_TEST_CHECK(quick_select_lookup(candidates, "") == nullptr);
    AURORA_TEST_CHECK(quick_select_lookup(candidates, "a") == nullptr);
    AURORA_TEST_CHECK(quick_select_lookup(candidates, "zz") == nullptr);

    const auto *hit = quick_select_lookup(candidates, "as");
    AURORA_TEST_REQUIRE(hit != nullptr);
    AURORA_TEST_CHECK_STREQ(hit->text, std::string{"1"});
    AURORA_TEST_CHECK_STREQ(quick_select_lookup(candidates, "aa")->text, std::string{"0"});
}

}  // namespace borealis::test_cases::utest_quick_select
