/// 测试类型: unit
/// 目标单元: include/borealis/conn/snippet.h + src/conn/snippet.cpp
/// 测试说明: 快捷片段纯逻辑（`codespec/UI_SNIPPETS.draft.md` 批 1）：`{{name}}` 扫描与
///           首现序去重、非法/未闭合按字面、渲染替换全部出现且缺值保留、分组的首现序与
///           空组排除、搜索三字段与 ASCII 折叠及分组交集、`snip-N` 号分配、三档校验的
///           边界字节数。`SPEC.FEAT.CONN.07` 的 snippets 延后子项。

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/conn/snippet.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_snippet {

namespace {

using borealis::conn::next_snippet_id;
using borealis::conn::render_snippet;
using borealis::conn::Snippet;
using borealis::conn::SnippetFill;
using borealis::conn::snippet_groups;
using borealis::conn::snippet_placeholder_names;
using borealis::conn::snippet_placeholders;
using borealis::conn::snippet_rows;
using borealis::conn::SnippetIssue;
using borealis::conn::validate_snippet;

[[nodiscard]] auto make_snippet(std::string id, std::string name, std::string group, std::string body)
    -> Snippet {
    Snippet snippet;
    snippet.id = std::move(id);
    snippet.name = std::move(name);
    snippet.group = std::move(group);
    snippet.body = std::move(body);
    return snippet;
}

[[nodiscard]] auto join(const std::vector<std::size_t> &rows) -> std::string {
    std::string text;
    for (const auto row : rows) {
        if (!text.empty()) {
            text += ",";
        }
        text += std::to_string(row);
    }
    return text;
}

}  // namespace

AURORA_TEST_CASE(placeholder_names_follow_first_appearance_and_skip_malformed) {
    const std::string body = "ssh {{host}} -p {{port}} {{host}} {{bad name}} {{}} {{ok}}";
    const auto names = snippet_placeholder_names(body);
    const std::vector<std::string> expected{"host", "port", "ok"};
    AURORA_TEST_CHECK_TRUE(names == expected);

    // 全部出现都留在位置表里（渲染要逐处替换），同名不必重复登记。
    const auto occurrences = snippet_placeholders(body);
    AURORA_TEST_CHECK_EQ(occurrences.size(), 4U);
    AURORA_TEST_CHECK_EQ(occurrences[0].begin, body.find("{{host}}"));
    AURORA_TEST_CHECK_EQ(occurrences[1].begin, body.find("{{port}}"));
    AURORA_TEST_CHECK_EQ(occurrences[2].begin, body.find("{{host}}", occurrences[0].begin + 1U));
    AURORA_TEST_CHECK_EQ(occurrences[3].begin, body.find("{{ok}}"));
}

AURORA_TEST_CASE(unclosed_or_invalid_tokens_stay_literal) {
    AURORA_TEST_CHECK(snippet_placeholder_names("echo {{tail").empty());
    AURORA_TEST_CHECK(snippet_placeholders("echo {{tail").empty());
    // 非法名之后的合法 token 仍要找到。
    const auto names = snippet_placeholder_names("{{bad name}} {{good}}");
    AURORA_TEST_CHECK_EQ(names.size(), 1U);
    AURORA_TEST_CHECK_EQ(names.front(), "good");
    // 占位名只收 ASCII 白名单：空格、斜杠与超长名都按字面。
    AURORA_TEST_CHECK(snippet_placeholder_names("{{a/b}} {{ }}").empty());
    const std::string long_name(33U, 'a');
    AURORA_TEST_CHECK(snippet_placeholder_names("{{" + long_name + "}}").empty());
}

AURORA_TEST_CASE(render_replaces_every_occurrence_and_keeps_unknown_literal) {
    const std::vector<SnippetFill> fills{
        SnippetFill{.name = "a", .value = "one"},
        SnippetFill{.name = "b", .value = "two"},
    };
    AURORA_TEST_CHECK_EQ(render_snippet("{{a}}-{{b}}-{{a}}", fills), std::string{"one-two-one"});
    // 缺值的占位符原样保留；填进去的字节不转义、不加引号。
    AURORA_TEST_CHECK_EQ(render_snippet("x {{missing}} y", fills), std::string{"x {{missing}} y"});
    AURORA_TEST_CHECK_EQ(render_snippet("echo {{v}}", {SnippetFill{.name = "v", .value = "a b|c"}}),
                         std::string{"echo a b|c"});
    // 同名填充以第一条为准（第二真值源不进渲染）。
    AURORA_TEST_CHECK_EQ(render_snippet("{{a}}", {SnippetFill{.name = "a", .value = "first"},
                                                  SnippetFill{.name = "a", .value = "second"}}),
                         std::string{"first"});
    // 非法 token 不动，合法 token 照替。
    AURORA_TEST_CHECK_EQ(render_snippet("{{bad name}} {{ok}}", {SnippetFill{.name = "ok", .value = "yes"}}),
                         std::string{"{{bad name}} yes"});
    AURORA_TEST_CHECK_EQ(render_snippet("no tokens", fills), std::string{"no tokens"});
}

AURORA_TEST_CASE(groups_follow_first_appearance_and_skip_ungrouped) {
    const std::vector<Snippet> snippets{
        make_snippet("snip-1", "a", "ops", "a"),
        make_snippet("snip-2", "b", "", "b"),
        make_snippet("snip-3", "c", "dev", "c"),
        make_snippet("snip-4", "d", "ops", "d"),
    };
    const std::vector<std::string> expected{"ops", "dev"};
    AURORA_TEST_CHECK_TRUE(snippet_groups(snippets) == expected);
    AURORA_TEST_CHECK(snippet_groups({}).empty());
}

AURORA_TEST_CASE(rows_filter_by_group_and_ascii_folded_query) {
    const std::vector<Snippet> snippets{
        make_snippet("snip-1", "Deploy Prod", "ops", "kubectl apply -f x.yaml"),
        make_snippet("snip-2", "List", "", "ls -la"),
        make_snippet("snip-3", "SSH", "ops", "ssh {{host}}"),
    };
    AURORA_TEST_CHECK_EQ(join(snippet_rows(snippets, "", "")), std::string{"0,1,2"});
    AURORA_TEST_CHECK_EQ(join(snippet_rows(snippets, "DEPLOY", "")), std::string{"0"});
    AURORA_TEST_CHECK_EQ(join(snippet_rows(snippets, "kubectl", "")), std::string{"0"});
    AURORA_TEST_CHECK_EQ(join(snippet_rows(snippets, "ssh", "")), std::string{"2"});
    AURORA_TEST_CHECK_EQ(join(snippet_rows(snippets, "", "ops")), std::string{"0,2"});
    // 搜索与分组取交集：List 不在 ops 里。
    AURORA_TEST_CHECK_EQ(join(snippet_rows(snippets, "list", "ops")), std::string{});
    AURORA_TEST_CHECK_EQ(join(snippet_rows(snippets, "list", "")), std::string{"1"});
    // 空查询不折叠、不丢行。
    AURORA_TEST_CHECK_EQ(join(snippet_rows(snippets, "", "missing")), std::string{});
}

AURORA_TEST_CASE(next_id_takes_the_highest_number_plus_one) {
    AURORA_TEST_CHECK_EQ(next_snippet_id({}), std::string{"snip-1"});
    std::vector<Snippet> snippets{
        make_snippet("snip-1", "a", "", "a"),
        make_snippet("snip-7", "b", "", "b"),
        make_snippet("manual", "c", "", "c"),
    };
    AURORA_TEST_CHECK_EQ(next_snippet_id(snippets), std::string{"snip-8"});
    // 形态不合法的 id 不参与 max，也不挡住规范号。
    snippets.push_back(make_snippet("snip-X", "d", "", "d"));
    AURORA_TEST_CHECK_EQ(next_snippet_id(snippets), std::string{"snip-8"});
}

AURORA_TEST_CASE(validation_reports_the_first_problem_and_accepts_boundaries) {
    AURORA_TEST_CHECK(validate_snippet("", "", "ls") == SnippetIssue::EmptyName);
    AURORA_TEST_CHECK(validate_snippet("   ", "", "ls") == SnippetIssue::EmptyName);
    AURORA_TEST_CHECK(validate_snippet(std::string(65U, 'a'), "", "ls") == SnippetIssue::NameTooLong);
    AURORA_TEST_CHECK(validate_snippet(std::string{"a\nb"}, "", "ls") == SnippetIssue::NameControlChar);
    AURORA_TEST_CHECK(validate_snippet("ok", std::string(33U, 'g'), "ls") == SnippetIssue::GroupTooLong);
    AURORA_TEST_CHECK(validate_snippet("ok", std::string{"g\tg"}, "ls") ==
                      SnippetIssue::GroupControlChar);
    AURORA_TEST_CHECK(validate_snippet("ok", "", "") == SnippetIssue::EmptyBody);
    AURORA_TEST_CHECK(validate_snippet("ok", "", std::string(8193U, 'x')) == SnippetIssue::BodyTooLong);

    AURORA_TEST_CHECK(validate_snippet(std::string(64U, 'a'), std::string(32U, 'g'),
                                       std::string(8192U, 'x')) == SnippetIssue::None);
    AURORA_TEST_CHECK(validate_snippet("ok", "", "ls -la") == SnippetIssue::None);
    AURORA_TEST_CHECK(validate_snippet("ok", "", "line one\nline two") == SnippetIssue::None);
}

}  // namespace borealis::test_cases::utest_snippet
