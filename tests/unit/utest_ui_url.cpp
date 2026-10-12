/// 测试类型: unit
/// 目标单元: include/borealis/ui/url.h + src/ui/url.cpp
/// 测试说明: `SPEC.FEAT.INTERACT.05` 的识别与决策腿（设计稿 `codespec/UI_URL.draft.md` §4 批 1，
///           裁决 7.119）：OSC 8 优先取「同标识连续游程」并以声明目标为准（稿 D1①／F2），标识淘汰
///           落回启发式而白名单外**不落回**；启发式自带字符表——双宽字符不截断游程、`|jq` 尾巴与
///           全角句号断在外、行尾标点与不配对右括号逐档修剪、起点须在非字母数字边界（稿 D3①／判据 2）；
///           协议白名单两来源同闸（判据 5，含 `www.` 按 http 前缀参与判定，稿 D2①）；`link_host_of`
///           的大小写归一、缺省端口折叠、userinfo 剔除与 IPv6 保括号（稿 D9①）；两来源 × 三档策略 ×
///           名单内外的处置矩阵与「同 host 不同端口算不同域」；解析不出 host 一律拒绝（含 `Never` 档）；
///           名单件的幂等、有序快照与回调次数。全程手工构造 `grid::Row`，不需要界面。

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/config/settings.h"
#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/term/utf8.h"
#include "borealis/ui/selection.h"
#include "borealis/ui/url.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_ui_url {

namespace {

using borealis::config::TerminalSettings;
using borealis::grid::Cell;
using borealis::grid::HyperlinkId;
using borealis::grid::kFlagWideContinuation;
using borealis::grid::Row;
using borealis::term::append_utf8;
using borealis::ui::find_link_at;
using borealis::ui::GridCellPos;
using borealis::ui::link_host_of;
using borealis::ui::LinkConfirmPolicy;
using borealis::ui::LinkHit;
using borealis::ui::LinkOpenIntent;
using borealis::ui::LinkOpenPlan;
using borealis::ui::LinkOpenReason;
using borealis::ui::LinkScanOptions;
using borealis::ui::LinkSource;
using borealis::ui::parse_link_schemes;
using borealis::ui::plan_link_open;
using borealis::ui::RowSpan;
using borealis::ui::TrustedLinkHosts;
using borealis::ui::word_span_at;

constexpr std::size_t kColumns = 100U;

/// @brief 缺省白名单照稿 D8① 的落盘缺省串取；本件不再复制一份缺省值（真值源在配置侧）。
auto default_options() -> LinkScanOptions {
    return LinkScanOptions{parse_link_schemes("http,https")};
}

/// @brief 写一行 ASCII 原文（每码点一格）。
auto row_with(std::string_view text) -> Row {
    Row row{kColumns};
    for (std::size_t index = 0; index < text.size(); ++index) {
        Cell cell{};
        cell.code_point = static_cast<char32_t>(static_cast<unsigned char>(text[index]));
        row.set(index, cell);
    }
    return row;
}

/// @brief 逐码点写入：非 ASCII 一律按双宽两格落（基础格 + 延续格，口径见 cell.h）。
/// @return 写入后的下一列。
auto put_line(Row &row, std::size_t column, std::u32string_view text) -> std::size_t {
    for (const auto code_point : text) {
        const bool wide = code_point >= 0x80U;
        Cell base{};
        base.code_point = code_point;
        base.width = wide ? 2U : 1U;
        row.set(column, base);
        ++column;
        if (wide) {
            Cell continuation{};
            continuation.width = 0;
            continuation.flags = kFlagWideContinuation;
            row.set(column, continuation);
            ++column;
        }
    }
    return column;
}

/// @brief 给 `[first, last)` 逐列挂同一枚标识（侧表按列存，故「一条链接」即同标识的连续游程）。
auto link_run(Row &row, std::size_t first, std::size_t last, HyperlinkId id) -> void {
    for (std::size_t column = first; column < last; ++column) {
        row.set_hyperlink(column, id);
    }
}

/// @brief 码点串的 UTF-8 形态（`LinkHit::target` 按 UTF-8 交，故期望值同口径构造）。
auto utf8(std::u32string_view text) -> std::string {
    std::string out;
    for (const auto code_point : text) {
        static_cast<void>(append_utf8(code_point, out));
    }
    return out;
}

/// @brief 只关心启发式那一腿的扫描：落点在第 @p column 列，OSC 8 目标恒为「无标识」。
auto heuristic_hit(const Row &row, std::size_t column, const LinkScanOptions &options = default_options())
    -> std::optional<LinkHit> {
    return find_link_at(row, GridCellPos{0U, column}, options, std::nullopt);
}

/// @brief 期望命中：区间与目标一起核，区间左界必须是 scheme（或 `www.`）的起点。
auto expect_hit(const std::optional<LinkHit> &hit, std::size_t first, std::size_t last,
                std::string_view target) -> bool {
    return hit.has_value() && hit->span == (RowSpan{0U, first, last}) && hit->target == target;
}

}  // namespace

AURORA_TEST_CASE(osc8_hit_uses_the_declared_target_and_owns_the_whole_id_run) {
    // 稿判据 1 的安全含义：屏上写「CI build log」而目标是另一串；游程按标识划，故空格也在区间内。
    Row row = row_with("  CI build log  ");
    link_run(row, 2U, 14U, 3U);
    const auto target = std::optional<std::u32string>{U"https://ci.example.com/builds/4821/logs"};

    const auto hit = find_link_at(row, GridCellPos{0U, 6U}, default_options(), target);
    AURORA_TEST_REQUIRE(hit.has_value());
    AURORA_TEST_CHECK(hit->source == LinkSource::Osc8);
    AURORA_TEST_CHECK_EQ(hit->span, (RowSpan{0U, 2U, 14U}));
    AURORA_TEST_CHECK_EQ(hit->target, std::string{"https://ci.example.com/builds/4821/logs"});
}

AURORA_TEST_CASE(osc8_run_stops_where_the_identifier_changes) {
    // 相邻两枚标识不得并成一条：侧表是一列至多一条的游程，不是区间表（稿 F2）。
    Row row = row_with("abcd");
    link_run(row, 0U, 2U, 5U);
    link_run(row, 2U, 4U, 6U);

    const auto hit = find_link_at(row, GridCellPos{0U, 3U}, default_options(),
                                  std::optional<std::u32string>{U"https://example.com"});
    AURORA_TEST_REQUIRE(hit.has_value());
    AURORA_TEST_CHECK_EQ(hit->span, (RowSpan{0U, 2U, 4U}));
}

AURORA_TEST_CASE(an_evicted_identifier_falls_back_to_the_heuristic) {
    // 裁决 7.21② 那一档：标识还挂在侧表、目标已解不出（nullopt）⇒ 屏幕上那串本身仍可能是条 URL。
    Row row = row_with("https://example.com/a");
    link_run(row, 0U, row.columns(), 7U);

    AURORA_TEST_CHECK_MSG(expect_hit(heuristic_hit(row, 8U), 0U, 21U, "https://example.com/a"),
                          "the evicted id must fall back to the shape scan");
    const auto hit = heuristic_hit(row, 8U);
    AURORA_TEST_REQUIRE(hit.has_value());
    AURORA_TEST_CHECK(hit->source == LinkSource::Heuristic);
}

AURORA_TEST_CASE(osc8_target_outside_the_whitelist_is_not_clickable_and_does_not_fall_back) {
    // 判据 5 + D1①：不过闸即「不可点」，**不得**改按屏上文字猜一条——那正是 D1② 要防的攻击面。
    Row plain = row_with("read the manual");
    link_run(plain, 0U, plain.columns(), 1U);
    AURORA_TEST_CHECK_TRUE(!find_link_at(plain, GridCellPos{0U, 3U}, default_options(),
                                         std::optional<std::u32string>{U"ftp://host/file"})
                                 .has_value());

    Row shaped = row_with("http://good.example");  // 屏上这段本身是一条能过闸的 http URL
    link_run(shaped, 0U, shaped.columns(), 2U);
    AURORA_TEST_CHECK_TRUE(!find_link_at(shaped, GridCellPos{0U, 4U}, default_options(),
                                         std::optional<std::u32string>{U"javascript:alert(1)"})
                                 .has_value());
}

AURORA_TEST_CASE(heuristic_recognizes_only_the_url_inside_a_command) {
    // 稿判据 2：`curl …|jq` 里可点段止于竖线，竖线与两侧空白都不进气。
    Row row = row_with("curl https://example.com/a?b=1|jq");
    AURORA_TEST_CHECK_TRUE(expect_hit(heuristic_hit(row, 10U), 5U, 30U, "https://example.com/a?b=1"));

    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row, 1U).has_value());    // curl 那一词
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row, 4U).has_value());     // 落在那一枚空格上
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row, 31U).has_value());    // jq 那一词
}

AURORA_TEST_CASE(heuristic_keeps_every_character_the_word_delimiters_would_break) {
    // 稿 D3①：本件**不吃** `terminal.word_delimiters`——那枚键用户可改，一改就静默改掉链接边界。
    // 于是含 `/?&=%#-.,` 的整串仍是一条，而同一落点上的双击选词确实只选到一个字母（两处刻意不一致）。
    constexpr std::string_view kUrl{"https://a.example/x?b=1&c=%20#seg-ment."};
    Row row = row_with(kUrl);

    const auto hit = heuristic_hit(row, 8U);
    AURORA_TEST_REQUIRE(hit.has_value());
    // 行尾那枚句号按稿 D3① 修剪，其后整串都在区间内。
    AURORA_TEST_CHECK_EQ(hit->target, std::string{"https://a.example/x?b=1&c=%20#seg-ment"});
    AURORA_TEST_CHECK_EQ(hit->span.last_column - hit->span.first_column, kUrl.size() - 1U);

    const auto doubled = word_span_at(row, GridCellPos{0U, 8U}, TerminalSettings{}.word_delimiters);
    AURORA_TEST_REQUIRE(doubled.has_value());
    AURORA_TEST_CHECK_LT(doubled->last_column - doubled->first_column,
                         hit->span.last_column - hit->span.first_column);
}

AURORA_TEST_CASE(heuristic_run_crosses_double_width_cells) {
    // CJK-LITERAL: cjk-fixture - 双宽延续格截断游程、全角句号断游程就是本条被测事实（稿判据 2、变异 M-E）
    Row row{kColumns};
    std::size_t column = put_line(row, 0U, U"\u89C1 ");  // 「见」+ 空格
    const std::size_t first = column;                     // URL 的起点列
    column = put_line(row, column, U"https://a.cn/");
    const std::size_t path_first = column;
    column = put_line(row, column, U"\u4E2D\u6587\u8DEF\u5F84");  // 「中文路径」四枚双宽
    put_line(row, column, U"\u3002");                             // 「。」：断游程

    const auto hit = heuristic_hit(row, path_first - 2U);
    AURORA_TEST_REQUIRE(hit.has_value());
    AURORA_TEST_CHECK_EQ(hit->span.first_column, first);
    AURORA_TEST_CHECK_EQ(hit->span.last_column, column);  // 句号不入区间
    AURORA_TEST_CHECK_EQ(hit->target, utf8(U"https://a.cn/\u4E2D\u6587\u8DEF\u5F84"));

    // 落点在双宽字符的右半格上仍按基础格判定：那一格是「见」，不是链接。
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row, 1U).has_value());
    // 落在全角句号上：断游程，什么都不给。
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row, column).has_value());
}

AURORA_TEST_CASE(trailing_punctuation_and_unbalanced_closers_are_trimmed) {
    // 稿 D3① 的修剪档：逐档钉，含「配对的括号留下、不配对的剔出」与两规则交替那一条。
    struct Case {
        std::string_view text;
        std::string_view expected;
    };
    constexpr std::array<Case, 8> kCases{
        Case{"https://a.cn/x.", "https://a.cn/x"},
        Case{"https://a.cn/x,", "https://a.cn/x"},
        Case{"https://a.cn/x;", "https://a.cn/x"},
        Case{"https://a.cn/x!", "https://a.cn/x"},
        Case{"https://a.cn/x'", "https://a.cn/x"},
        Case{"https://a.cn/wiki/C_(programming)", "https://a.cn/wiki/C_(programming)"},
        Case{"https://a.cn/x)", "https://a.cn/x"},
        Case{"https://a.cn/(b))", "https://a.cn/(b)"},
    };
    for (const auto &test_case : kCases) {
        Row row = row_with(test_case.text);
        const auto hit = heuristic_hit(row, 8U);
        AURORA_TEST_REQUIRE(hit.has_value());
        AURORA_TEST_CHECK_MSG(hit->target == test_case.expected, std::string{test_case.text}.c_str());
    }

    // 句号先剔才轮到那枚孤立右括号：两规则交替跑的腿（单次遍历会漏）。
    AURORA_TEST_CHECK_TRUE(expect_hit(heuristic_hit(row_with("https://a.cn/x)."), 8U), 0U, 14U,
                              "https://a.cn/x"));
}

AURORA_TEST_CASE(a_url_start_must_follow_a_non_alnum_boundary) {
    // 前导是字母数字即「这只是某个词的一部分」：`xhttps://` 不误报，`URL:https://` 从 `https` 起算。
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row_with("xhttps://a/b"), 3U).has_value());
    AURORA_TEST_CHECK_TRUE(expect_hit(heuristic_hit(row_with("URL:https://a/b"), 7U), 4U, 15U,
                                      "https://a/b"));
}

AURORA_TEST_CASE(a_scheme_without_a_host_is_not_a_link) {
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row_with("log https:// here"), 6U).has_value());
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row_with("https://."), 4U).has_value());  // 修剪后只剩分隔符
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row_with("httpx://a/b"), 3U).has_value());
}

AURORA_TEST_CASE(www_prefix_is_guessed_as_http_and_follows_the_whitelist) {
    // 稿 D2①：`www.` 档是**猜**前缀，故它按 `http://` 参与白名单判定，确认框另须报出来源。
    const auto hit = heuristic_hit(row_with("see www.example.org/docs"), 6U);
    AURORA_TEST_REQUIRE(hit.has_value());
    AURORA_TEST_CHECK(hit->source == LinkSource::Heuristic);
    AURORA_TEST_CHECK_EQ(hit->span, (RowSpan{0U, 4U, 24U}));
    AURORA_TEST_CHECK_EQ(hit->target, std::string{"http://www.example.org/docs"});

    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row_with("www.example.org/docs"), 6U,
                                         LinkScanOptions{parse_link_schemes("https")})
                                 .has_value());
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row_with("www. x"), 2U).has_value());
}

AURORA_TEST_CASE(only_whitelisted_schemes_become_clickable) {
    // 稿判据 5 的纯件证人：白名单外的串**不产 LinkHit**（不画线、不进菜单、Ctrl+点击无效）。
    for (std::string_view text : {"ftp://host/file", "ssh://host", "file:///etc/passwd"}) {
        AURORA_TEST_CHECK_MSG(!heuristic_hit(row_with(text), 4U).has_value(),
                              std::string{text}.c_str());
    }

    const LinkScanOptions with_ftp{parse_link_schemes("http,https,ftp")};
    AURORA_TEST_CHECK_TRUE(expect_hit(heuristic_hit(row_with("ftp://host/file"), 4U, with_ftp), 0U, 15U,
                                      "ftp://host/file"));

    // 该键被清空即「什么都不可以点」（稿 D8① 登记的那档表现，由设置页旁注解释它）。
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row_with("https://a/b"), 4U, LinkScanOptions{}).has_value());
}

AURORA_TEST_CASE(scheme_matching_is_case_insensitive) {
    // 白名单判定不看大小写，目标原文一律不动（提示条与确认框显示的是屏上那串的逐字节目标）。
    const auto hit = heuristic_hit(row_with("HTTPS://EXAMPLE.COM/A"), 4U);
    AURORA_TEST_REQUIRE(hit.has_value());
    AURORA_TEST_CHECK_EQ(hit->target, std::string{"HTTPS://EXAMPLE.COM/A"});
}

AURORA_TEST_CASE(whitelist_csv_splits_trims_and_lowercases) {
    AURORA_TEST_CHECK(parse_link_schemes(" HTTP, https , ,ftp ,, \t") ==
                      (std::vector<std::string>{"http", "https", "ftp"}));
    AURORA_TEST_CHECK_TRUE(parse_link_schemes("").empty());
    AURORA_TEST_CHECK_EQ(parse_link_schemes("http").size(), 1U);
    // 面板不拒收非法项（稿 D8①）：拆得出来即可，永不匹配就是它唯一的后果。
    AURORA_TEST_CHECK(parse_link_schemes("http,://,h_ttp") ==
                      (std::vector<std::string>{"http", "://", "h_ttp"}));
}

AURORA_TEST_CASE(host_of_folds_default_ports_and_lowercases) {
    struct Case {
        std::string_view target;
        std::string_view host;
    };
    constexpr std::array<Case, 6> kCases{
        Case{"https://Example.COM:443/a?b", "https://example.com"},
        Case{"http://x:80", "http://x"},
        Case{"http://x:8080", "http://x:8080"},
        Case{"HTTP://Host/P", "http://host"},
        Case{"https://a.cn/path?x=1#frag", "https://a.cn"},
        Case{"http://user:pw@Host:8443/p", "http://host:8443"},  // 信任按机器判，不按「以谁的身份登上去」判
    };
    for (const auto &test_case : kCases) {
        const auto host = link_host_of(test_case.target);
        AURORA_TEST_REQUIRE(host.has_value());
        AURORA_TEST_CHECK_MSG(*host == test_case.host, std::string{test_case.target}.c_str());
    }
}

AURORA_TEST_CASE(host_of_keeps_ipv6_brackets_and_refuses_shapes_it_cannot_name) {
    const auto bracketed = link_host_of("http://[::1]:3000/x");
    AURORA_TEST_REQUIRE(bracketed.has_value());
    AURORA_TEST_CHECK_EQ(*bracketed, std::string{"http://[::1]:3000"});
    const auto implicit = link_host_of("http://[::1]/x");
    AURORA_TEST_REQUIRE(implicit.has_value());
    AURORA_TEST_CHECK_EQ(*implicit, std::string{"http://[::1]"});

    for (std::string_view text :
         {"mailto:a@b.com", "https://", "https:///x", "http://a:8o/x", "http://[::1/x", "ftp:a/b"}) {
        AURORA_TEST_CHECK_MSG(!link_host_of(text).has_value(), std::string{text}.c_str());
    }
}

AURORA_TEST_CASE(the_two_sources_read_their_own_policy_key) {
    // 稿 D8① 的「两档分开配置」：六组取值（两来源 × 三档）在空名单上的形态。
    const TrustedLinkHosts empty;
    constexpr std::string_view kTarget{"https://example.com/a"};
    const std::string host{"https://example.com"};
    for (const LinkSource source : {LinkSource::Osc8, LinkSource::Heuristic}) {
        AURORA_TEST_CHECK(plan_link_open(source, LinkConfirmPolicy::Never, LinkConfirmPolicy::Never, empty,
                                         kTarget) ==
                          (LinkOpenPlan{LinkOpenIntent::Open, host, LinkOpenReason::None}));
        AURORA_TEST_CHECK(plan_link_open(source, LinkConfirmPolicy::AskOnce, LinkConfirmPolicy::AskOnce,
                                         empty, kTarget) ==
                          (LinkOpenPlan{LinkOpenIntent::Confirm, host, LinkOpenReason::None}));
        AURORA_TEST_CHECK(plan_link_open(source, LinkConfirmPolicy::AskAlways, LinkConfirmPolicy::AskAlways,
                                         empty, kTarget) ==
                          (LinkOpenPlan{LinkOpenIntent::Confirm, host, LinkOpenReason::None}));
    }
}

AURORA_TEST_CASE(the_two_policies_are_chosen_per_source) {
    // osc8=never ∧ heuristic=ask_always 与反向那一组：各问各的，不得互相顶替。
    const TrustedLinkHosts empty;
    constexpr std::string_view kTarget{"https://example.com"};
    AURORA_TEST_CHECK_EQ(plan_link_open(LinkSource::Osc8, LinkConfirmPolicy::Never,
                                         LinkConfirmPolicy::AskAlways, empty, kTarget)
                             .intent,
                         LinkOpenIntent::Open);
    AURORA_TEST_CHECK_EQ(plan_link_open(LinkSource::Heuristic, LinkConfirmPolicy::Never,
                                         LinkConfirmPolicy::AskAlways, empty, kTarget)
                             .intent,
                         LinkOpenIntent::Confirm);
    AURORA_TEST_CHECK_EQ(plan_link_open(LinkSource::Osc8, LinkConfirmPolicy::AskAlways,
                                         LinkConfirmPolicy::Never, empty, kTarget)
                             .intent,
                         LinkOpenIntent::Confirm);
    AURORA_TEST_CHECK_EQ(plan_link_open(LinkSource::Heuristic, LinkConfirmPolicy::AskAlways,
                                         LinkConfirmPolicy::Never, empty, kTarget)
                             .intent,
                         LinkOpenIntent::Open);
}

AURORA_TEST_CASE(ask_once_remembers_the_domain_and_stops_asking) {
    // 稿判据 4 + 变异 M-C 的证人腿：记住之后再点同域不再问，而 `AskAlways` 读都不读名单。
    TrustedLinkHosts trusted;
    constexpr std::string_view kTarget{"https://example.com/a"};
    AURORA_TEST_CHECK_EQ(plan_link_open(LinkSource::Heuristic, LinkConfirmPolicy::AskOnce,
                                        LinkConfirmPolicy::AskOnce, trusted, kTarget)
                             .intent,
                         LinkOpenIntent::Confirm);
    AURORA_TEST_CHECK_TRUE(trusted.add(link_host_of(kTarget).value()));

    AURORA_TEST_CHECK_EQ(plan_link_open(LinkSource::Heuristic, LinkConfirmPolicy::AskOnce,
                                        LinkConfirmPolicy::AskOnce, trusted, kTarget)
                             .intent,
                         LinkOpenIntent::Open);
    AURORA_TEST_CHECK_EQ(plan_link_open(LinkSource::Heuristic, LinkConfirmPolicy::AskAlways,
                                        LinkConfirmPolicy::AskAlways, trusted, kTarget)
                             .intent,
                         LinkOpenIntent::Confirm);
    // 同 host 不同端口是另一个域（稿 D9①：只记 host 会把一次「记住」铺满整台机器的所有端口）。
    AURORA_TEST_CHECK_EQ(plan_link_open(LinkSource::Heuristic, LinkConfirmPolicy::AskOnce,
                                        LinkConfirmPolicy::AskOnce, trusted, "https://example.com:8443/a")
                             .intent,
                         LinkOpenIntent::Confirm);
}

AURORA_TEST_CASE(an_unnameable_host_is_refused_under_every_policy) {
    // fail-closed（稿 D9① 的代价那条）：解析不出 host[:port] 就既不打开也不问——「记住该域」无从落笔。
    const TrustedLinkHosts trusted;
    for (const LinkSource source : {LinkSource::Osc8, LinkSource::Heuristic}) {
        for (const LinkConfirmPolicy policy :
             {LinkConfirmPolicy::Never, LinkConfirmPolicy::AskOnce, LinkConfirmPolicy::AskAlways}) {
            const LinkOpenPlan plan = plan_link_open(source, policy, policy, trusted, "https:///no-host");
            AURORA_TEST_CHECK(plan == (LinkOpenPlan{LinkOpenIntent::Refuse, {}, LinkOpenReason::HostUnparsable}));
        }
    }
}

AURORA_TEST_CASE(the_ledger_is_idempotent_and_snapshots_sorted) {
    // 名单件：回调只在**新增**时给整表快照；`load` 是灌入不是变化（装配层靠这一条免写一次盘）。
    TrustedLinkHosts trusted;
    std::vector<std::string> seen;
    std::size_t calls = 0U;
    trusted.set_change_sink([&](std::span<const std::string> hosts) {
        ++calls;
        seen.assign(hosts.begin(), hosts.end());
    });

    AURORA_TEST_CHECK_TRUE(trusted.add("https://b.example"));
    AURORA_TEST_CHECK_EQ(calls, 1U);
    AURORA_TEST_CHECK_FALSE(trusted.add("https://b.example"));
    AURORA_TEST_CHECK_EQ(calls, 1U);
    AURORA_TEST_CHECK_TRUE(trusted.add("https://a.example"));
    AURORA_TEST_CHECK_EQ(calls, 2U);
    AURORA_TEST_CHECK(seen == (std::vector<std::string>{"https://a.example", "https://b.example"}));
    AURORA_TEST_CHECK_TRUE(trusted.contains("https://a.example"));
    AURORA_TEST_CHECK_FALSE(trusted.contains("https://c.example"));
    AURORA_TEST_CHECK_FALSE(trusted.add(""));  // 空域不收：那正是 fail-closed 要挡的事本身

    trusted.load({"https://z.example", "https://a.example", "https://z.example"});
    AURORA_TEST_CHECK_EQ(calls, 2U);  // 灌入不回调
    AURORA_TEST_CHECK_FALSE(trusted.contains("https://b.example"));
    AURORA_TEST_CHECK(std::vector<std::string>(trusted.hosts().begin(), trusted.hosts().end()) ==
                      (std::vector<std::string>{"https://a.example", "https://z.example"}));
}

AURORA_TEST_CASE(finding_a_link_on_an_empty_row_or_outside_the_row_is_nothing) {
    const Row empty{0U};
    AURORA_TEST_CHECK_TRUE(
        !find_link_at(empty, GridCellPos{0U, 0U}, default_options(), std::nullopt).has_value());
    const Row row = row_with("https://a/b");
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row, kColumns).has_value());
    AURORA_TEST_CHECK_TRUE(!heuristic_hit(row, kColumns + 9U).has_value());
}

}  // namespace borealis::test_cases::utest_ui_url
