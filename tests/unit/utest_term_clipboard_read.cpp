/// 测试类型: unit
/// 目标单元: include/borealis/term/clipboard_read.h + src/term/clipboard_read.cpp，以及
///           `Terminal::apply_clipboard` 的读方向三态分派腿
/// 测试说明: OSC 52 读方向的三态闸（SPEC.FEAT.CONN.12）——未接线与显式禁止都逐字节回既有空响应
///           （判据 ①③ 的回归底线）、允许与每次询问只在状态机侧登记不答话（架构 §3.4）、Pc 首段
///           归一为单个选择位、待答队列有界且满档当场回空响应（判据 ③ + 不可信输入）、改档即刻对
///           在途会话生效（D2① 现读）、RIS 清待答、`encode_base64` 逐档并与 `osc::decode_base64`
///           往返互逆。写方向的三条既有判据归 `utest_terminal_osc`，此处只锁「不被三态闸牵连」。

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/term/clipboard_read.h"
#include "borealis/term/osc.h"
#include "borealis/term/terminal.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"
#include "term/osc.h"

namespace borealis::test_cases::utest_term_clipboard_read {

namespace {

using borealis::term::osc::decode_base64;
using borealis::term::ClipboardGate;
using borealis::term::ClipboardReadPolicy;
using borealis::term::ClipboardReadRequest;
using borealis::term::encode_base64;
using borealis::term::encode_utf8;
using borealis::term::kMaxClipboardReadRequests;
using borealis::term::SingleWidthPolicy;
using borealis::term::Terminal;

auto narrow_only = std::make_shared<SingleWidthPolicy>();

/// @brief 应答收集端：读方向的空响应与（批 2 的）正文都从这里出。
class CollectingSink final : public borealis::term::ResponseSink {
  public:
    auto on_response(std::u32string_view response) -> void override {
        responses_.push_back(std::u32string{response});
    }

    std::vector<std::u32string> responses_;
};

[[nodiscard]] auto make_terminal() -> Terminal {
    return {10, 3, 5, narrow_only};
}

/// @brief 以 BEL 收尾的 OSC 串。
[[nodiscard]] auto osc_bel(std::u32string_view body) -> std::u32string {
    std::u32string out{U"\x1B]"};
    out.append(body);
    out.push_back(U'\x07');
    return out;
}

/// @brief 一条读查询的完整串（`52;<args>;?` 形态，@p args 可含多段编号）。
[[nodiscard]] auto read_query(std::u32string_view args) -> std::u32string {
    std::u32string body{U"52;"};
    if (!args.empty()) {
        body.append(args);
        body.push_back(U';');
    }
    body.push_back(U'?');
    return osc_bel(body);
}

[[nodiscard]] auto bytes_of(std::string_view text) -> std::span<const std::byte> {
    return std::as_bytes(std::span<const char>{text.data(), text.size()});
}

/// @brief ASCII 串升格成码点串（`decode_base64` 吃码点）。
[[nodiscard]] auto to_code_points(std::string_view text) -> std::u32string {
    std::u32string out;
    for (const char c : text) {
        out.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 文本 → UTF-8 字节 → base64（应答腿要拼的那一串的前半）。
[[nodiscard]] auto encode_text(std::u32string_view text) -> std::string {
    std::string utf8;
    static_cast<void>(encode_utf8(text, utf8));
    return encode_base64(bytes_of(utf8));
}

/// @brief 待答请求里的选择位写成 ASCII 便于比对。
[[nodiscard]] auto pc_text(const ClipboardReadRequest &request) -> std::string {
    std::string out;
    static_cast<void>(encode_utf8(request.pc, out));
    return out;
}

/// @brief 取下标处的应答；越界返回空串。
///
/// 期望的空响应本身非空，故「少一条」会以内容不符变红——直接 `[]` 越界则把整个进程判死，
/// 报告里看不到是哪条断言。
[[nodiscard]] auto response_at(const CollectingSink &sink, std::size_t index) -> std::u32string {
    return index < sink.responses_.size() ? sink.responses_[index] : std::u32string{};
}

/// @brief 取下标处待答请求的选择位；越界返回空串（同上，期望值恒非空）。
[[nodiscard]] auto pc_at(const std::vector<ClipboardReadRequest> &pending, std::size_t index)
    -> std::string {
    return index < pending.size() ? pc_text(pending[index]) : std::string{};
}

constexpr std::u32string_view kEmptyResponse{U"\x1B]52;c;\x07"};

}  // namespace

AURORA_TEST_CASE(read_without_a_gate_answers_the_legacy_empty_response) {
    // 未接线即 fail-closed 按禁止（D2①）：漏接线的后果是远端读不到剪贴板，而不是谁都读得到。
    auto term = make_terminal();
    CollectingSink sink;
    term.set_response_sink(&sink);
    term.feed(read_query(U"c"));

    AURORA_TEST_CHECK_EQ(sink.responses_.size(), std::size_t{1});
    AURORA_TEST_CHECK(response_at(sink, 0) == kEmptyResponse);
    AURORA_TEST_CHECK(term.take_clipboard_reads().empty());
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_requests, std::size_t{1});
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_denied, std::size_t{1});
}

AURORA_TEST_CASE(explicit_deny_keeps_the_empty_response_byte_for_byte) {
    ClipboardGate gate{ClipboardReadPolicy::Deny};
    auto term = make_terminal();
    term.set_clipboard_gate(&gate);
    CollectingSink sink;
    term.set_response_sink(&sink);

    term.feed(read_query(U"c"));
    term.feed(read_query(U"p"));
    term.feed(read_query({}));  // `52;?`：连编号段都没有

    AURORA_TEST_CHECK_EQ(sink.responses_.size(), std::size_t{3});
    for (const std::u32string &response : sink.responses_) {
        AURORA_TEST_CHECK(response == std::u32string{kEmptyResponse});
    }
    AURORA_TEST_CHECK(term.take_clipboard_reads().empty());
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_requests, std::size_t{3});
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_denied, std::size_t{3});
}

AURORA_TEST_CASE(allow_and_ask_only_register_in_the_state_machine) {
    // 状态机不做 IO：读剪贴板与弹窗询问都在主线程，故两档在这里都只是登记（D3①）。
    ClipboardGate gate{ClipboardReadPolicy::Allow};
    auto term = make_terminal();
    term.set_clipboard_gate(&gate);
    CollectingSink sink;
    term.set_response_sink(&sink);

    term.feed(read_query(U"c"));
    AURORA_TEST_CHECK_TRUE(sink.responses_.empty());
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_denied, std::size_t{0});

    gate.set_policy(ClipboardReadPolicy::Ask);
    term.feed(read_query(U"c"));

    const auto pending = term.take_clipboard_reads();
    AURORA_TEST_CHECK_EQ(pending.size(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_requests, std::size_t{2});
    // 取走即清空：下一帧不会把同一条再答一遍。
    AURORA_TEST_CHECK(term.take_clipboard_reads().empty());
}

AURORA_TEST_CASE(policy_change_takes_effect_on_a_live_session) {
    // D2① 的正面判据：闸门是 app 级一份、状态机现读 ⇒ 改档即刻对**在途**会话生效，
    // 不需要重建会话，也不受「装配层广播只到当前标签」的限制（G15）。
    ClipboardGate gate{ClipboardReadPolicy::Deny};
    auto term = make_terminal();
    term.set_clipboard_gate(&gate);
    CollectingSink sink;
    term.set_response_sink(&sink);

    term.feed(read_query(U"c"));
    AURORA_TEST_CHECK_EQ(sink.responses_.size(), std::size_t{1});

    gate.set_policy(ClipboardReadPolicy::Allow);
    term.feed(read_query(U"c"));
    AURORA_TEST_CHECK_EQ(sink.responses_.size(), std::size_t{1});  // 第二条没回空响应
    AURORA_TEST_CHECK_EQ(term.take_clipboard_reads().size(), std::size_t{1});

    gate.set_policy(ClipboardReadPolicy::Deny);
    term.feed(read_query(U"c"));
    AURORA_TEST_CHECK_EQ(sink.responses_.size(), std::size_t{2});
    AURORA_TEST_CHECK(term.take_clipboard_reads().empty());
}

AURORA_TEST_CASE(pc_of_the_first_segment_is_echoed_and_always_normalized) {
    ClipboardGate gate{ClipboardReadPolicy::Allow};
    auto term = make_terminal();
    term.set_clipboard_gate(&gate);

    // 回显首段 Pc（D8①）；非法、缺段、多字符都归一到 `c`——Pc 来自不可信输入，
    // 原样回写等于替远端把任意字节喷回它自己。
    term.feed(read_query(U"p"));
    term.feed(read_query(U"0"));
    term.feed(read_query(U"c;p"));
    term.feed(read_query({}));
    term.feed(read_query(U"zz"));
    term.feed(read_query(std::u32string{64U, U'q'}));
    term.feed(read_query(U"*"));

    const auto pending = term.take_clipboard_reads();
    AURORA_TEST_CHECK_EQ(pending.size(), std::size_t{7});
    const std::array<std::string_view, 7> expected{"p", "0", "c", "c", "c", "c", "*"};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        AURORA_TEST_CHECK(pc_at(pending, index) == expected[index]);
    }
}

AURORA_TEST_CASE(pending_queue_is_bounded_and_overflow_answers_immediately) {
    ClipboardGate gate{ClipboardReadPolicy::Allow};
    auto term = make_terminal();
    term.set_clipboard_gate(&gate);
    CollectingSink sink;
    term.set_response_sink(&sink);

    for (std::size_t index = 0; index <= kMaxClipboardReadRequests; ++index) {
        term.feed(read_query(U"c"));
    }

    // 满档当场回空响应而不是悄悄丢掉某一条：被拒也要有回写（判据 ③）。
    AURORA_TEST_CHECK_EQ(sink.responses_.size(), std::size_t{1});
    AURORA_TEST_CHECK(response_at(sink, 0) == std::u32string{kEmptyResponse});
    const auto pending = term.take_clipboard_reads();
    AURORA_TEST_CHECK_EQ(pending.size(), kMaxClipboardReadRequests);
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_requests, kMaxClipboardReadRequests + 1U);
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_denied, std::size_t{1});

    // 腾出位置后又能登记：上限封的是「同时挂起」的条数，不是会话一生的总额。
    term.feed(read_query(U"c"));
    AURORA_TEST_CHECK_EQ(term.take_clipboard_reads().size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(sink.responses_.size(), std::size_t{1});
}

AURORA_TEST_CASE(ris_clears_pending_read_requests) {
    // 与写方向同一对称：`ESC c` 之后不得留下待答请求，否则复位前的一条读会在复位后被答出去。
    ClipboardGate gate{ClipboardReadPolicy::Allow};
    auto term = make_terminal();
    term.set_clipboard_gate(&gate);
    term.feed(read_query(U"c"));
    AURORA_TEST_CHECK_EQ(term.take_clipboard_reads().size(), std::size_t{1});

    term.feed(read_query(U"c"));
    term.feed(U"\x1B" U"c");  // RIS：`\x1B` 后直接跟 `c` 会被十六进制转义吞成一个码点
    AURORA_TEST_CHECK(term.take_clipboard_reads().empty());
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_requests, std::size_t{0});
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_denied, std::size_t{0});
}

AURORA_TEST_CASE(the_read_gate_does_not_touch_the_write_direction) {
    // 判据「写方向默认允许」属 SPEC.FEAT.TERM.07（D10① 明确不并管）：闸门开到 Allow 也不该改它。
    ClipboardGate gate{ClipboardReadPolicy::Allow};
    auto term = make_terminal();
    term.set_clipboard_gate(&gate);
    CollectingSink sink;
    term.set_response_sink(&sink);

    term.feed(osc_bel(U"52;c;aGVsbG8="));
    const auto pending = term.take_clipboard_write();
    AURORA_TEST_CHECK(pending.has_value());
    AURORA_TEST_CHECK(*pending == std::u32string{U"hello"});
    AURORA_TEST_CHECK(term.take_clipboard_reads().empty());
    AURORA_TEST_CHECK_TRUE(sink.responses_.empty());
    AURORA_TEST_CHECK_EQ(term.osc_state().clipboard_read_requests, std::size_t{0});
}

AURORA_TEST_CASE(gate_defaults_to_deny_and_round_trips_all_three_states) {
    const ClipboardGate fresh;
    AURORA_TEST_CHECK(fresh.policy() == ClipboardReadPolicy::Deny);
    // 闸门可跨线程读写，故取 const 引用也读得到（Terminal 只持观察指针）。
    const ClipboardGate &as_const = fresh;
    AURORA_TEST_CHECK(as_const.policy() == ClipboardReadPolicy::Deny);

    ClipboardGate mutable_gate;
    mutable_gate.set_policy(ClipboardReadPolicy::Ask);
    AURORA_TEST_CHECK(mutable_gate.policy() == ClipboardReadPolicy::Ask);
    mutable_gate.set_policy(ClipboardReadPolicy::Allow);
    AURORA_TEST_CHECK(mutable_gate.policy() == ClipboardReadPolicy::Allow);
}

AURORA_TEST_CASE(encode_base64_matches_the_rfc_4648_vectors) {
    AURORA_TEST_CHECK(encode_base64(bytes_of("")).empty());
    AURORA_TEST_CHECK(encode_base64(bytes_of("f")) == "Zg==");
    AURORA_TEST_CHECK(encode_base64(bytes_of("fo")) == "Zm8=");
    AURORA_TEST_CHECK(encode_base64(bytes_of("foo")) == "Zm9v");
    AURORA_TEST_CHECK(encode_base64(bytes_of("foob")) == "Zm9vYg==");
    AURORA_TEST_CHECK(encode_base64(bytes_of("fooba")) == "Zm9vYmE=");
    AURORA_TEST_CHECK(encode_base64(bytes_of("foobar")) == "Zm9vYmFy");

    // 余数三档之外，还要锁住字母表本身：`+/` 写错一位，远端解出来的就是另一份内容。
    AURORA_TEST_CHECK(encode_base64(bytes_of(std::string{"\xFB\xFF", 2})) == "+/8=");
    AURORA_TEST_CHECK(encode_base64(bytes_of(std::string{"\0\x01\x02", 3})) == "AAEC");
}

AURORA_TEST_CASE(encode_base64_round_trips_with_the_decoder) {
    const std::vector<std::u32string> samples{
        U"", U"a", U"ab", U"abc", U"hello world", U"中", U"中文剪贴板", U"\U0001F600 emoji",
        U"line1\nline2\r\n",
    };
    for (const std::u32string &text : samples) {
        const std::string encoded = encode_text(text);
        const auto decoded = decode_base64(to_code_points(encoded));
        AURORA_TEST_CHECK(decoded.has_value());
        if (decoded.has_value()) {
            AURORA_TEST_CHECK(*decoded == text);
        }
    }
}

AURORA_TEST_CASE(encode_base64_output_stays_inside_the_alphabet) {
    // 应答串要原样喷进会话写通道：出现 `;`、BEL、CR 之外的控制字符都会把 OSC 串自身截断。
    const std::string encoded = encode_text(U"中文\ttab\x07 bell");
    for (const char c : encoded) {
        const bool printable_ascii = c >= 0x21 && c <= 0x7E;
        AURORA_TEST_CHECK(printable_ascii);
    }
    AURORA_TEST_CHECK(encoded.find(';') == std::string::npos);
    AURORA_TEST_CHECK(encoded.find('\x07') == std::string::npos);
}

}  // namespace borealis::test_cases::utest_term_clipboard_read
