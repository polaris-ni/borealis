/// 测试类型: unit
/// 目标单元: src/conn/telnet_negotiation.{h,cpp}
/// 测试说明: `SPEC.FEAT.CONN.06`（Telnet）协商纯逻辑层的无头单测（裁决 7.112 D11①/D12①）。
///           七类断言全都不需要 socket：上线发起序（D3①）、应答表逐档（判据 4）、
///           同一立场只答一次（D4①，含重拨清零）、`FF FF` 双向转义与单字节命令不吃
///           后一字节（判据 5，实测 F6/F7）、子协商载荷不进网格、跨分片喂法的等价性
///           （F5/F6 的形态锁）、NAWS 组包与同值去重（D10② 人改判，实测 F14 的字节序）。
///
///           真 socket 上的双向字节核对属 `itest_telnet_connection`（批 2）；真 telnetd
///           对客户端发起子协商的反应本沙箱无服务器可判（F1），留稿 §6 走查第 ⑷ 条。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "conn/telnet_negotiation.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_telnet_negotiation {

namespace {

using borealis::conn::TelnetAnswer;
using borealis::conn::TelnetNegotiation;
using borealis::conn::TelnetParsed;

using Bytes = std::vector<std::uint8_t>;

/// @brief 把可读的整数字面量写成字节流（测试里的字节序列按十进制/十六进制混排易错，统一走这里）。
[[nodiscard]] auto bytes(std::initializer_list<int> values) -> Bytes {
    Bytes out;
    out.reserve(values.size());
    for (const auto value : values) {
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}

/// @brief 喂一段并返回结果。
[[nodiscard]] auto feed(TelnetNegotiation &negotiation, const Bytes &chunk) -> TelnetParsed {
    return negotiation.on_bytes(chunk);
}

/// @brief 把字节流排成 `FF FA 1F …` 形态，失败信息里能直接读出协议形态。
[[nodiscard]] auto hex(const Bytes &values) -> std::string {
    static constexpr char kDigits[]{"0123456789ABCDEF"};
    std::string out;
    out.reserve(values.size() * 3);
    for (const auto value : values) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out.push_back(kDigits[value >> 4]);
        out.push_back(kDigits[value & 0x0F]);
    }
    return out;
}

[[nodiscard]] auto bytes_of(std::string_view text) -> Bytes {
    Bytes out;
    out.reserve(text.size());
    for (const char ch : text) {
        out.push_back(static_cast<std::uint8_t>(ch));
    }
    return out;
}

/// @brief 一条三字节协商命令。
[[nodiscard]] auto command(std::uint8_t verb, std::uint8_t option) -> Bytes {
    return bytes({0xFF, verb, option});
}

}  // namespace

AURORA_TEST_CASE(opening_asks_do_echo_and_do_sga_and_take_the_stance) {
    TelnetNegotiation negotiation;
    // 判据 3（D3①）：连上即发两条发起命令，各三字节，`TTYPE` 不在内。
    const auto opening = negotiation.opening_commands();
    AURORA_TEST_CHECK_TRUE(opening == bytes({0xFF, 0xFD, 0x01, 0xFF, 0xFD, 0x03}));

    // 呼用即记账（口径 ⑵）：服务端回 `WILL ECHO`/`WILL SGA` 不该换来第二条 `DO`。
    const auto echo = feed(negotiation, command(0xFB, 0x01));
    AURORA_TEST_CHECK_TRUE(echo.replies.empty());
    AURORA_TEST_CHECK_TRUE(echo.data.empty());
    const auto sga = feed(negotiation, command(0xFB, 0x03));
    AURORA_TEST_CHECK_TRUE(sga.replies.empty());
    AURORA_TEST_CHECK_TRUE(negotiation.stance().remote_echo == TelnetAnswer::Yes);
    AURORA_TEST_CHECK_TRUE(negotiation.stance().remote_suppress_ga == TelnetAnswer::Yes);

    // 重复呼不再重发（连接腿若误呼两次，线上字节仍是六条的一半都没有）。
    AURORA_TEST_CHECK_TRUE(negotiation.opening_commands().empty());
}

AURORA_TEST_CASE(answer_table_covers_every_rfc_row) {
    TelnetNegotiation negotiation;  // 未呼 opening_commands ⇒ 这一件考的是应答表本身

    // `WILL x`：支持则 `DO`（ECHO/SGA），表外一律 `DONT`（D4①）。
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFB, 0x01)).replies == command(0xFD, 0x01));
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFB, 0x03)).replies == command(0xFD, 0x03));
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFB, 0x0A)).replies == command(0xFE, 0x0A));

    // `DO x`：SGA 应 `WILL`；ECHO 明确拒绝＝D7① 的「不做本地回显」；表外拒绝。
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFD, 0x03)).replies == command(0xFB, 0x03));
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFD, 0x01)).replies == command(0xFC, 0x01));
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFD, 0x64)).replies == command(0xFC, 0x64));

    // `DO TTYPE` ⇒ `WILL TTYPE` 紧跟 `SB TTYPE IS "XTERM" SE`（判据 4 的那一条整串）。
    const auto ttype = feed(negotiation, command(0xFD, 0x18));
    const auto want_ttype = bytes({0xFF, 0xFB, 0x18,                            // IAC WILL TTYPE
                                   0xFF, 0xFA, 0x18, 0x00,                      // IAC SB TTYPE IS
                                   0x58, 0x54, 0x45, 0x52, 0x4D,                // "XTERM"
                                   0xFF, 0xF0});                                 // IAC SE
    AURORA_TEST_CHECK_MSG(ttype.replies == want_ttype, hex(ttype.replies));
    AURORA_TEST_CHECK_TRUE(ttype.data.empty());
    AURORA_TEST_CHECK_TRUE(negotiation.stance().reports_terminal_type == TelnetAnswer::Yes);

    // `DO NAWS` ⇒ `WILL NAWS`；`WONT`/`DONT` 是结论不是问题，一个字节都不回。
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFD, 0x1F)).replies == command(0xFB, 0x1F));
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFC, 0x01)).replies.empty());
    AURORA_TEST_CHECK_TRUE(negotiation.stance().remote_echo == TelnetAnswer::No);
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFE, 0x18)).replies.empty());
    AURORA_TEST_CHECK_TRUE(negotiation.stance().reports_terminal_type == TelnetAnswer::No);
}

AURORA_TEST_CASE(repeated_ask_gets_no_second_answer) {
    TelnetNegotiation negotiation;
    // 对端带重试逻辑时反复问同一项：第二次起不得再答（D4① 的防风暴）。
    const auto first = feed(negotiation, command(0xFD, 0x18));
    AURORA_TEST_CHECK_EQ(first.replies.size(), 14U);  // 3（WILL TTYPE）+ 11（SB TTYPE IS "XTERM" SE）
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto again = feed(negotiation, command(0xFD, 0x18));
        AURORA_TEST_CHECK_TRUE(again.replies.empty());
        AURORA_TEST_CHECK_TRUE(again.data.empty());
    }
    // 表外项同理：`WILL 10` 问两次只拒一次。
    AURORA_TEST_CHECK_EQ(feed(negotiation, command(0xFB, 0x0A)).replies.size(), 3U);
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFB, 0x0A)).replies.empty());
}

AURORA_TEST_CASE(data_escaping_and_one_byte_commands_never_eat_the_next_byte) {
    TelnetNegotiation negotiation;
    // F7：`FF FF` 在数据段里就是一个字面 0xFF，两侧都要还原/加上。
    const auto mixed = feed(negotiation, bytes({0x41, 0xFF, 0xFF, 0x42, 0xFF, 0xFF, 0xFF, 0xFF, 0x43}));
    AURORA_TEST_CHECK_TRUE(mixed.data == bytes({0x41, 0xFF, 0x42, 0xFF, 0xFF, 0x43}));
    AURORA_TEST_CHECK_TRUE(mixed.replies.empty());
    AURORA_TEST_CHECK_TRUE(TelnetNegotiation::escape_outbound(bytes({0x41, 0xFF, 0x42})) ==
                           bytes({0x41, 0xFF, 0xFF, 0x42}));
    AURORA_TEST_CHECK_TRUE(TelnetNegotiation::escape_outbound(bytes({})).empty());

    // 单字节命令（GA=0xF9、NOP=0xF1、表外 0xF4）吞掉它**本身**，后面紧跟的照常是数据。
    const auto after_ga = feed(negotiation, bytes({0xFF, 0xF9, 0x4C, 0xFF, 0xF1, 0x4F}));
    AURORA_TEST_CHECK_TRUE(after_ga.data == bytes({0x4C, 0x4F}));
    AURORA_TEST_CHECK_TRUE(after_ga.replies.empty());
}

AURORA_TEST_CASE(subnegotiation_payload_never_reaches_the_grid) {
    TelnetNegotiation negotiation;
    // F7 的另一半：载荷里可嵌 `FF FF` 转义，终止符才是 `IAC SE`；整段都不进网格。
    const auto in = feed(negotiation,
                         bytes({0xFF, 0xFA, 0x18, 0x00, 'X', 0xFF, 0xFF, 'Y', 0xFF, 0xF0, 'h', 'i'}));
    AURORA_TEST_CHECK_TRUE(in.data == bytes({'h', 'i'}));
    AURORA_TEST_CHECK_TRUE(in.replies.empty());

    // 未终止的子协商：一个字节都不漏（判据 5 的「未完成段绝不进 on_bytes」）。
    const auto unterminated = feed(negotiation, bytes({0xFF, 0xFA, 0x27, 'a', 'b', 0xFF, 0xFF}));
    AURORA_TEST_CHECK_TRUE(unterminated.data.empty());
    // 终止符终于到来：吞完载荷即回数据段。
    const auto closed = feed(negotiation, bytes({0xFF, 0xF0, 'z'}));
    AURORA_TEST_CHECK_TRUE(closed.data == bytes({'z'}));
}

AURORA_TEST_CASE(mixed_and_split_reads_follow_the_same_rules) {
    TelnetNegotiation negotiation;
    // F5：一次 recv 里混排三条命令 + 文本。
    const auto batch = feed(negotiation,
                            bytes({0xFF, 0xFB, 0x01, 0xFF, 0xFC, 0x03, 0xFF, 0xFD, 0x18, 'l', 'o',
                                   'g', 'i', 'n', ':', ' '}));
    AURORA_TEST_CHECK_TRUE(batch.data == bytes_of("login: "));
    // 三条命令里只有两条要答：`WILL ECHO`⇒`DO ECHO`、`DO TTYPE`⇒`WILL`+子协商；
    // `WONT SGA` 是结论不是问题（口径 ⑵）。
    const auto want_batch = bytes({0xFF, 0xFD, 0x01, 0xFF, 0xFB, 0x18, 0xFF, 0xFA, 0x18, 0x00, 'X',
                                   'T', 'E', 'R', 'M', 0xFF, 0xF0});
    AURORA_TEST_CHECK_MSG(batch.replies == want_batch, hex(batch.replies));

    // F6：一条命令断成三次读，每段都不许吐半个命令字节。
    TelnetNegotiation split;
    const auto first = feed(split, bytes({0xFF}));
    AURORA_TEST_CHECK_TRUE(first.data.empty());
    AURORA_TEST_CHECK_TRUE(first.replies.empty());
    const auto second = feed(split, bytes({0xFB}));
    AURORA_TEST_CHECK_TRUE(second.data.empty());
    AURORA_TEST_CHECK_TRUE(second.replies.empty());
    const auto third = feed(split, bytes({0x01}));
    AURORA_TEST_CHECK_TRUE(third.data.empty());
    AURORA_TEST_CHECK_TRUE(third.replies == command(0xFD, 0x01));
    AURORA_TEST_CHECK_TRUE(split.stance().remote_echo == TelnetAnswer::Yes);
}

AURORA_TEST_CASE(all_three_feeding_styles_are_byte_for_byte_equal) {
    // 判据 5 的形态锁（稿 §6 点名的等价性用例）：1 字节 / 随机分片 / 整段三种喂法，
    // 净数据与待回字节必须逐字节全等——这是拆成纯逻辑件（D11①）的全部理由。
    const auto stream = bytes({
        'W', 'e', 'l', 'c', 'o', 'm', 'e', 0xFF, 0xFF, '!',           // 数据段里的字面 0xFF
        0xFF, 0xFB, 0x01,                                             // WILL ECHO
        'l', 'o', 'g', 'i', 'n', ':', ' ',
        0xFF, 0xFD, 0x18,                                             // DO TTYPE
        0xFF, 0xFA, 0x27, 0x00, 0xFF, 0xFF, 0x7F, 0xFF, 0xF0,         // 表外子协商 + 载荷内转义
        0xFF, 0xF9,                                                   // GA（单字节命令）
        0x0D, 0x0A,
        0xFF, 0xFD, 0x1F,                                             // DO NAWS
        0xFF, 0xFE, 0x03,                                             // DONT SGA
        0xFF, 0xFD, 0x01,                                             // DO ECHO（第二次问同一立场）
        0xFF, 0xFB, 0x0A, 0xFF, 0xFB, 0x0A,                           // 重复的表外 WILL
        0xFF, 0xFF, 'P', 'w', 'd', ':',                               // 又一个字面 0xFF
    });

    TelnetNegotiation whole;
    const auto whole_out = feed(whole, stream);

    TelnetNegotiation by_one;
    Bytes by_one_data;
    Bytes by_one_replies;
    for (const auto single : stream) {
        const auto step = feed(by_one, bytes({single}));
        by_one_data.insert(by_one_data.end(), step.data.begin(), step.data.end());
        by_one_replies.insert(by_one_replies.end(), step.replies.begin(), step.replies.end());
    }

    TelnetNegotiation chunked;
    Bytes chunked_data;
    Bytes chunked_replies;
    std::uint32_t rng = 0x1234ABCDU;  // 定值种子：分片形态可复现，不引随机库
    std::size_t cursor = 0;
    while (cursor < stream.size()) {
        rng = rng * 1'664'525U + 1'013'904'223U;
        const auto size = static_cast<std::size_t>(1U + (rng % 5U));
        const Bytes chunk(stream.begin() + static_cast<std::ptrdiff_t>(cursor),
                          stream.begin() + static_cast<std::ptrdiff_t>(cursor + size > stream.size()
                                                                           ? stream.size()
                                                                           : cursor + size));
        const auto step = feed(chunked, chunk);
        chunked_data.insert(chunked_data.end(), step.data.begin(), step.data.end());
        chunked_replies.insert(chunked_replies.end(), step.replies.begin(), step.replies.end());
        cursor += size;
    }

    AURORA_TEST_CHECK_MSG(whole_out.data == by_one_data, hex(whole_out.data) + " vs " + hex(by_one_data));
    AURORA_TEST_CHECK_MSG(whole_out.data == chunked_data, hex(whole_out.data) + " vs " + hex(chunked_data));
    AURORA_TEST_CHECK_MSG(whole_out.replies == by_one_replies,
                          hex(whole_out.replies) + " vs " + hex(by_one_replies));
    AURORA_TEST_CHECK_MSG(whole_out.replies == chunked_replies,
                          hex(whole_out.replies) + " vs " + hex(chunked_replies));
    AURORA_TEST_CHECK_TRUE(whole_out.data == bytes_of("Welcome\xff!login: \r\n\xffPwd:"));
    AURORA_TEST_CHECK_MSG(whole_out.replies ==
                              bytes({0xFF, 0xFD, 0x01, 0xFF, 0xFB, 0x18, 0xFF, 0xFA, 0x18, 0x00, 'X',
                                     'T', 'E', 'R', 'M', 0xFF, 0xF0, 0xFF, 0xFB, 0x1F, 0xFF, 0xFC, 0x01,
                                     0xFF, 0xFE, 0x0A}),
                          hex(whole_out.replies));
    // 净数据里绝不出现命令字节对：0xFB/0xFA/0xF0 之类的形态只该在解析里被吃掉。
    for (const auto value : whole_out.data) {
        AURORA_TEST_CHECK_NE(value, 0xFB);
    }
}

AURORA_TEST_CASE(naws_payload_is_width_then_height_big_endian) {
    TelnetNegotiation negotiation;
    // RFC 1073 §6 的例子：80 列 × 24 行 ⇒ `255 250 31 0 80 0 24 255 240`；
    // 本机 GNU inetutils 2.7 客户端在 20 行 × 60 列的 pty 上实测同序（稿 §0 的 F14）。
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(24, 80) ==
                           bytes({0xFF, 0xFA, 0x1F, 0x00, 0x50, 0x00, 0x18, 0xFF, 0xF0}));
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(20, 60) ==
                           bytes({0xFF, 0xFA, 0x1F, 0x00, 0x3C, 0x00, 0x14, 0xFF, 0xF0}));
    // 40 行 × 130 列（稿 §6 点名的断言，次序按 F14 更正为宽在前）。
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(40, 130) ==
                           bytes({0xFF, 0xFA, 0x1F, 0x00, 0x82, 0x00, 0x28, 0xFF, 0xF0}));

    // 载荷里的 0xFF 按同一份转义 doubled（F7）：255 行 × 200 列 ⇒ 高字节 0x00、低字节 0xFF。
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(255, 200) ==
                           bytes({0xFF, 0xFA, 0x1F, 0x00, 0xC8, 0x00, 0xFF, 0xFF, 0xFF, 0xF0}));
}

AURORA_TEST_CASE(naws_same_value_is_deduped_and_redial_clears_everything) {
    TelnetNegotiation negotiation;
    AURORA_TEST_CHECK_EQ(negotiation.window_size_command(40, 100).size(), 9U);
    // 同值去重（D10② 代价 ⓑ）：拖动分屏不得形成协商风暴。
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(40, 100).empty());
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(40, 101) ==
                           bytes({0xFF, 0xFA, 0x1F, 0x00, 0x65, 0x00, 0x28, 0xFF, 0xF0}));
    // 去重只跟「上一次真的发出去的」比：拖回旧值仍要发，否则服务器停在 101 列。
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(40, 100) ==
                           bytes({0xFF, 0xFA, 0x1F, 0x00, 0x64, 0x00, 0x28, 0xFF, 0xF0}));

    // 重拨清零（D4① 与 D10② 代价 ⓒ）：已发尺寸、已答记账、状态位、半条命令全部复位。
    AURORA_TEST_CHECK_EQ(feed(negotiation, command(0xFD, 0x18)).replies.size(), 14U);
    const auto leftover = feed(negotiation, bytes({0xFF, 0xFB}));  // 故意停在半条命令上
    AURORA_TEST_CHECK_TRUE(leftover.data.empty());

    negotiation.reset_for_redial();
    AURORA_TEST_CHECK_EQ(negotiation.window_size_command(40, 100).size(), 9U);
    AURORA_TEST_CHECK_TRUE(negotiation.opening_commands().size() == 6U);
    AURORA_TEST_CHECK_TRUE(negotiation.stance() == conn::TelnetOptionStance{});
    // 重拨后发起序重新可得（同一台服务器可能改口，D4①），呼用即再记账，
    // 故对端回 `WILL ECHO` 依旧不再答第二遍。
    AURORA_TEST_CHECK_TRUE(feed(negotiation, command(0xFB, 0x01)).replies.empty());
    // 复位前那半条 `FF FB` 不得污染新连接：新流的第一字节重新按数据段的 IAC 处理。
    const auto fresh = feed(negotiation, bytes({0xFF, 0xFF, 'A'}));
    AURORA_TEST_CHECK_TRUE(fresh.data == bytes({0xFF, 'A'}));
}

AURORA_TEST_CASE(naws_out_of_range_values_are_not_sent_and_not_recorded) {
    TelnetNegotiation negotiation;
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(0, 80).empty());     // 尺寸未定
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(24, -1).empty());    // 非法
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(24, 65536).empty()); // RFC 1073 上限外
    // 越界不记账：下一次合法尺寸照样发得出去。
    AURORA_TEST_CHECK_EQ(negotiation.window_size_command(24, 80).size(), 9U);
    // 上界本身可发（65535 ⇒ 两个 0xFF 各转义成 `FF FF`，共四个字节）。
    AURORA_TEST_CHECK_TRUE(negotiation.window_size_command(24, 65535) ==
                           bytes({0xFF, 0xFA, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x18, 0xFF, 0xF0}));
}

}  // namespace borealis::test_cases::utest_telnet_negotiation
