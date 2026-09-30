/// 测试类型: unit
/// 目标单元: include/borealis/term/terminal.h 的 OSC 消费腿（含 include/borealis/term/osc.h 的形态）
/// 测试说明: OSC 命令号派发与消费产物——`0/2` 标题、`7` 工作目录原样留存、`8` 超链接区间
///           （含挂格、覆盖/擦除清链、双宽只挂前半格、有界表的淘汰语义）、`52` 写方向的 base64
///           解码与合并、`52` 读方向的空应答、`133` 命令块边界与退出码、未消费命令号的留痕、
///           RIS 清零（SPEC.FEAT.TERM.07，为 SPEC.FEAT.INTEG.01/.02 预埋来源）。

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "borealis/term/osc.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_terminal_osc {

namespace {

using borealis::grid::kNoHyperlink;
using borealis::term::kMaxHyperlinks;
using borealis::term::AmbiguousWidth;
using borealis::term::OscState;
using borealis::term::PromptMarker;
using borealis::term::ResponseSink;
using borealis::term::SingleWidthPolicy;
using borealis::term::Terminal;
using borealis::term::WidthPolicy;

SingleWidthPolicy narrow_only;

/// @brief 只认单个双宽码点的桩判定：宽度语义归 `utest_terminal`，此处只服务超链接挂格。
class StubWidePolicy final : public WidthPolicy {
  public:
    [[nodiscard]] auto width_of(char32_t code_point, AmbiguousWidth ambiguous) const noexcept
        -> std::uint8_t override {
        static_cast<void>(ambiguous);
        return code_point == U'\x4E2D' ? 2U : 1U;  // 「中」
    }
};

StubWidePolicy stub_wide;

/// @brief 应答收集端：只登记，不断言时序（`OSC 52` 读方向用）。
class CollectingSink final : public ResponseSink {
  public:
    auto on_response(std::u32string_view response) -> void override {
        responses_.push_back(std::u32string{response});
    }

    std::vector<std::u32string> responses_;
};

/// @brief 建一台 10 列 × 3 行、scrollback 5 行的终端。
[[nodiscard]] auto make_terminal(const WidthPolicy &policy) -> Terminal {
    return {10, 3, 5, policy};
}

/// @brief 某格当前挂着的超链接标识。
[[nodiscard]] auto link_at(Terminal &term, std::size_t row, std::size_t column) -> grid::HyperlinkId {
    return term.active_grid().visible_line(row).hyperlink(column);
}

/// @brief 取某标识的目标；不存在时返回空串，便于和码点字面量直接比。
[[nodiscard]] auto target_or_empty(Terminal &term, grid::HyperlinkId link_id) -> std::u32string {
    const auto target = term.hyperlink_target(link_id);
    return target.has_value() ? *target : std::u32string{};
}

/// @brief 把十进制数写成码点串（淘汰用例要造无界供给的 URI）。
[[nodiscard]] auto digits(std::size_t value) -> std::u32string {
    std::u32string out;
    for (const char c : std::to_string(value)) {
        out.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 以 BEL 收尾的 OSC 串。
[[nodiscard]] auto osc_bel(std::u32string_view body) -> std::u32string {
    std::u32string out{U"\x1B]"};
    out.append(body);
    out.push_back(U'\x07');
    return out;
}

}  // namespace

AURORA_TEST_CASE(osc_0_and_2_set_title_and_last_one_wins) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B]0;first\x07");
    AURORA_TEST_CHECK(term.osc_state().title == std::u32string{U"first"});

    // ST（`ESC \`）与 BEL 都是 OSC 的终结符，两种写法都要能落到同一状态。
    term.feed(U"\x1B]2;second\x1B\\");
    AURORA_TEST_CHECK(term.osc_state().title == std::u32string{U"second"});

    // 空标题是合法请求：清掉标题，而不是「保留上一个」。
    term.feed(osc_bel(U"2;"));
    AURORA_TEST_CHECK(term.osc_state().title.empty());
    AURORA_TEST_CHECK_EQ(term.osc_state().unhandled_count, std::size_t{0});
}

AURORA_TEST_CASE(osc_1_consumed_without_touching_title) {
    // 仅图标名的老 DEC 形态：消费掉、不留状态，也不能把图标名误当成标题显示。
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"0;title"));
    term.feed(osc_bel(U"1;icon-name"));
    AURORA_TEST_CHECK(term.osc_state().title == std::u32string{U"title"});
    AURORA_TEST_CHECK_EQ(term.osc_state().unhandled_count, std::size_t{0});
}

AURORA_TEST_CASE(osc_7_working_directory_kept_verbatim) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"7;file://hostA/srv/repo%20dir"));
    AURORA_TEST_CHECK(term.osc_state().working_directory ==
                      std::u32string{U"file://hostA/srv/repo%20dir"});

    term.feed(osc_bel(U"7;file://hostB/tmp"));
    AURORA_TEST_CHECK(term.osc_state().working_directory == std::u32string{U"file://hostB/tmp"});
}

AURORA_TEST_CASE(osc_8_open_and_close_bracket_the_written_cells) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"8;;https://example.test/a"));
    term.feed(U"abc");
    term.feed(osc_bel(U"8;;"));
    term.feed(U"xyz");

    // 区间内的每一格都挂同一个标识；关掉区间后写的格回到「无链接」。
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 0), grid::HyperlinkId{1});
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 1), grid::HyperlinkId{1});
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 2), grid::HyperlinkId{1});
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 3), kNoHyperlink);
    AURORA_TEST_CHECK(target_or_empty(term, 1) == std::u32string{U"https://example.test/a"});
    AURORA_TEST_CHECK_FALSE(term.hyperlink_target(kNoHyperlink).has_value());
}

AURORA_TEST_CASE(osc_8_params_field_excluded_from_uri) {
    // `8;id=1;uri`：只切第一刀，URI 自身含 `;` 与 `:` 都整段保留。
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"8;id=1;https://example.test/q?a=1;b=2"));
    term.feed(U"u");
    AURORA_TEST_CHECK(target_or_empty(term, link_at(term, 0, 0)) ==
                      std::u32string{U"https://example.test/q?a=1;b=2"});
}

AURORA_TEST_CASE(overwriting_and_erasing_a_cell_drops_its_hyperlink) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"8;;https://example.test/a"));
    term.feed(U"ab");
    term.feed(osc_bel(U"8;;"));

    term.feed(U"\x1B[1;1H");  // 回到 (0,0)
    term.feed(U"Z");          // 无链接状态下覆盖
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 0), kNoHyperlink);
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 1), grid::HyperlinkId{1});

    term.feed(U"\x1B[K");  // 擦行把剩下的带链接的格子一起清掉
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 1), kNoHyperlink);
}

AURORA_TEST_CASE(sgr_does_not_close_the_hyperlink_region) {
    // 链接挂在笔的区间上而不是 SGR 属性里：`CSI m` 复位属性不该截断链接。
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"8;;https://example.test/a"));
    term.feed(U"a\x1B[1mb\x1B[0mc");
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 0), link_at(term, 0, 1));
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 2), grid::HyperlinkId{1});
}

AURORA_TEST_CASE(wide_char_hyperlink_attaches_to_the_first_cell_only) {
    auto term = make_terminal(stub_wide);
    term.feed(osc_bel(U"8;;https://example.test/cjk"));
    term.feed(U"\x4E2D");  // 「中」：双宽，延续格不承载内容
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 0), grid::HyperlinkId{1});
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 1), kNoHyperlink);
}

AURORA_TEST_CASE(hyperlink_table_is_bounded_and_evicts_the_oldest) {
    // 不可信输入可无限供给 URI：满表淘汰最旧一条，被淘汰的标识解析不出目标（不可点），
    // 而绝不能指向另一条 URL。
    Terminal term{10, 3, 1200, narrow_only};  // scrollback 留够：第一条链接写下的格子要能在历史里查到
    const auto total = kMaxHyperlinks + 1U;
    for (std::size_t index = 1; index <= total; ++index) {
        term.feed(osc_bel(U"8;;https://example.test/" + digits(index)));
        term.feed(U"a");
    }

    AURORA_TEST_CHECK_FALSE(term.hyperlink_target(1).has_value());
    AURORA_TEST_CHECK(target_or_empty(term, 2) == std::u32string{U"https://example.test/2"});
    AURORA_TEST_CHECK(target_or_empty(term, total) ==
                      std::u32string{U"https://example.test/"} + digits(total));

    // 历史里的格子仍挂着已被淘汰的标识：读取方据此判「不可点」，内容本身不受影响。
    AURORA_TEST_CHECK_EQ(term.main_grid().line(0).hyperlink(0), grid::HyperlinkId{1});
}

AURORA_TEST_CASE(osc_52_write_stores_decoded_payload_once) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"52;c;aGVsbG8gd29ybGQ="));

    const auto pending = term.take_clipboard_write();
    AURORA_TEST_CHECK(pending.has_value());
    AURORA_TEST_CHECK(*pending == std::u32string{U"hello world"});
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_write_requests, std::size_t{1});

    // 取走即清空：主线程落地一次，不会被下一帧重复写进系统剪贴板。
    AURORA_TEST_CHECK_FALSE(term.take_clipboard_write().has_value());
}

AURORA_TEST_CASE(osc_52_writes_in_one_batch_merge_to_the_last) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"52;c;aGVsbG8=") + osc_bel(U"52;c;d29ybGQ="));
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_write_requests, std::size_t{2});

    const auto pending = term.take_clipboard_write();
    AURORA_TEST_CHECK(pending.has_value());
    AURORA_TEST_CHECK(*pending == std::u32string{U"world"});
}

AURORA_TEST_CASE(osc_52_multiple_clipboard_numbers_keep_last_payload) {
    // `52;c;p;<base64>`：剪贴板编号可以多段给出，载荷恒在最后一段（base64 表里没有 `;`）。
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"52;c;p;eA=="));
    const auto pending = term.take_clipboard_write();
    AURORA_TEST_CHECK(pending.has_value());
    AURORA_TEST_CHECK(*pending == std::u32string{U"x"});
}

AURORA_TEST_CASE(osc_52_empty_payload_is_a_valid_clear) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"52;c;"));
    const auto pending = term.take_clipboard_write();
    AURORA_TEST_CHECK(pending.has_value());  // 空文本是合法请求，须与「无待写请求」可区分
    AURORA_TEST_CHECK(pending->empty());
}

AURORA_TEST_CASE(osc_52_rejects_malformed_payloads) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"52;c;!!!!"));    // 字母表外字符
    term.feed(osc_bel(U"52;c;a"));       // 截断到只剩一个 6 位组
    term.feed(osc_bel(U"52;c;YQ==Z"));   // 填充之后仍有载荷
    AURORA_TEST_CHECK_FALSE(term.take_clipboard_write().has_value());
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_write_requests, std::size_t{0});

    // 非法载荷不污染后续：紧接着一段合法的写仍然生效。
    term.feed(osc_bel(U"52;c;eA=="));
    const auto pending = term.take_clipboard_write();
    AURORA_TEST_CHECK(pending.has_value());
    AURORA_TEST_CHECK(*pending == std::u32string{U"x"});
}

AURORA_TEST_CASE(osc_52_read_request_answers_empty_selection) {
    // 读方向默认禁止（SPEC.FEAT.CONN.12）：必须回写空响应，静默无响应会让远端程序挂着等。
    auto term = make_terminal(narrow_only);
    CollectingSink sink;
    term.set_response_sink(&sink);
    term.feed(osc_bel(U"52;c;?"));

    AURORA_TEST_CHECK_EQ(sink.responses_.size(), std::size_t{1});
    AURORA_TEST_CHECK(sink.responses_[0] == std::u32string{U"\x1B]52;c;\x07"});
    AURORA_TEST_CHECK_FALSE(term.take_clipboard_write().has_value());
}

AURORA_TEST_CASE(osc_133_records_prompt_boundaries_and_exit_code) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"133;A"));
    AURORA_TEST_CHECK(term.osc_state().prompt_marker == PromptMarker::PromptStart);
    term.feed(osc_bel(U"133;B"));
    AURORA_TEST_CHECK(term.osc_state().prompt_marker == PromptMarker::PromptEnd);
    term.feed(osc_bel(U"133;C"));
    AURORA_TEST_CHECK(term.osc_state().prompt_marker == PromptMarker::CommandStart);
    term.feed(osc_bel(U"133;D;1"));
    AURORA_TEST_CHECK(term.osc_state().prompt_marker == PromptMarker::CommandExecuted);
    AURORA_TEST_CHECK_EQ(term.osc_state().command_exit_code, std::int32_t{1});
    term.feed(osc_bel(U"133;P;Cwd=/srv"));
    AURORA_TEST_CHECK(term.osc_state().prompt_marker == PromptMarker::OutputStart);

    // 未知边界字母计入留痕，且不改写最近一次有效标记。
    term.feed(osc_bel(U"133;Q"));
    AURORA_TEST_CHECK(term.osc_state().prompt_marker == PromptMarker::OutputStart);
    AURORA_TEST_CHECK_EQ(term.osc_state().unhandled_count, std::size_t{1});
}

AURORA_TEST_CASE(osc_133_missing_exit_code_reads_as_zero) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"133;D;7") + osc_bel(U"133;D"));
    AURORA_TEST_CHECK_EQ(term.osc_state().command_exit_code, std::int32_t{0});
}

AURORA_TEST_CASE(unconsumed_osc_commands_are_counted_not_fatal) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"4;1;green"));   // 调色板族未消费
    term.feed(osc_bel(U"11;?"));        // 前景色查询未消费
    term.feed(U"\x1B]not-a-number\x07");  // 连命令号都拆不出来
    AURORA_TEST_CHECK_EQ(term.osc_state().unhandled_count, std::size_t{3});

    // 架构 §5.5 的降级口径：计完数继续解析，后面的标题设置照常生效。
    term.feed(osc_bel(U"0;still alive"));
    AURORA_TEST_CHECK(term.osc_state().title == std::u32string{U"still alive"});
}

AURORA_TEST_CASE(ris_clears_every_osc_product) {
    auto term = make_terminal(narrow_only);
    term.feed(osc_bel(U"0;title") + osc_bel(U"7;file://host/dir") + osc_bel(U"133;A"));
    term.feed(osc_bel(U"8;;https://example.test/a") + U"a");
    term.feed(osc_bel(U"52;c;eA=="));
    AURORA_TEST_CHECK_EQ(term.osc_state().unhandled_count, std::size_t{0});

    term.feed(U"\x1B" U"c");  // RIS（`\x1B` 后直接跟 `c` 会被十六进制转义吞成一个码点）

    const OscState &state = term.osc_state();
    AURORA_TEST_CHECK(state.title.empty());
    AURORA_TEST_CHECK(state.working_directory.empty());
    AURORA_TEST_CHECK(state.prompt_marker == PromptMarker::None);
    AURORA_TEST_CHECK_EQ(state.command_exit_code, std::int32_t{0});
    AURORA_TEST_CHECK_EQ(state.clipboard_write_requests, std::size_t{0});
    AURORA_TEST_CHECK_FALSE(term.take_clipboard_write().has_value());
    AURORA_TEST_CHECK_EQ(link_at(term, 0, 0), kNoHyperlink);
    AURORA_TEST_CHECK_FALSE(term.hyperlink_target(1).has_value());
}

}  // namespace borealis::test_cases::utest_terminal_osc
