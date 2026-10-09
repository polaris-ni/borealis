/// 测试类型: unit
/// 目标单元: include/borealis/term/codec.h
/// 测试说明: `SPEC.FEAT.TERM.09` 的双向口径——五档编码（GB18030/GBK/Big5/Latin-1/CP437）加
///           UTF-8 基线的解码、发送与不可表示字符三档策略；非法字节不中断码点流且计数，
///           跨 feed 分片保状态，映射不到的名字回落 UTF-8。批量断言回放
///           tests/fixtures/term/encoding_cases.tsv。

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/term/codec.h"
#include "borealis/term/utf8.h"
#include "framework/aurora_test.h"
#include "support/paths.h"

namespace borealis::test_cases::utest_codec {

namespace {

using borealis::term::CodePointSink;
using borealis::term::DecodeStats;
using borealis::term::kReplacementCharacter;
using borealis::term::SessionDecoder;
using borealis::term::UnrepresentablePolicy;

/// @brief 收集解码产出的码点。
class Collector final : public CodePointSink {
  public:
    auto on_code_point(char32_t cp) -> void override { text += cp; }

    std::u32string text;
};

/// @brief 一段字节解码到底的产出。
struct DecodeResult {
    std::u32string text;
    DecodeStats stats;
};

/// @brief 十六进制字节串 → 字节（夹具输入列，空格分隔）。
auto parse_bytes(std::string_view text) -> std::vector<std::byte> {
    std::vector<std::byte> out;
    std::string digits;
    for (const char c : text) {
        if (c == ' ' || c == '\t') {
            if (!digits.empty()) {
                out.push_back(static_cast<std::byte>(std::stoul(digits, nullptr, 16)));
                digits.clear();
            }
            continue;
        }
        digits += c;
    }
    if (!digits.empty()) {
        out.push_back(static_cast<std::byte>(std::stoul(digits, nullptr, 16)));
    }
    return out;
}

/// @brief 期望码点串解析：`\uXXXX` / `\UXXXXXX` / `R`（替换字符）/ 其余按字面字符。
auto parse_expected(std::string_view text) -> std::u32string {
    std::u32string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == 'R') {  // 夹具约定的替换字符简写
            out += kReplacementCharacter;
            continue;
        }
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += static_cast<char32_t>(static_cast<unsigned char>(text[i]));
            continue;
        }
        const char esc = text[i + 1];
        ++i;
        if (esc == 'u') {
            out += static_cast<char32_t>(std::stoul(std::string{text.substr(i + 1, 4)}, nullptr, 16));
            i += 4;
        } else if (esc == 'U') {
            out += static_cast<char32_t>(std::stoul(std::string{text.substr(i + 1, 6)}, nullptr, 16));
            i += 6;
        } else {
            out += static_cast<char32_t>(static_cast<unsigned char>(esc));
        }
    }
    return out;
}

/// @brief 码点串的可读十六进制形式，供失败消息定位。
auto describe(const std::u32string &text) -> std::string {
    std::string out;
    for (const char32_t cp : text) {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "U+%04X ", static_cast<std::uint32_t>(cp));
        out += buffer;
    }
    return out;
}

/// @brief 字节串的可读十六进制形式，供失败消息定位。
auto describe(const std::string &bytes) -> std::string {
    std::string out;
    for (const char c : bytes) {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%02X ", static_cast<unsigned char>(c));
        out += buffer;
    }
    return out;
}

auto split(std::string_view text, char sep) -> std::vector<std::string_view> {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (;;) {
        const auto pos = text.find(sep, start);
        if (pos == std::string_view::npos) {
            out.push_back(text.substr(start));
            return out;
        }
        out.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
}

/// @brief 按编码名造解码器，一次喂到底并收尾。
auto decode(std::string_view encoding, std::span<const std::byte> bytes) -> DecodeResult {
    auto decoder = borealis::term::make_session_decoder(encoding);
    Collector collector;
    decoder->feed(bytes, collector);
    decoder->finish(collector);
    return {collector.text, decoder->stats()};
}

/// @brief 按编码名造解码器，逐字节喂入（最极端的块边界：每一字节一块）。
auto decode_one_byte_at_a_time(std::string_view encoding, std::span<const std::byte> bytes)
    -> DecodeResult {
    auto decoder = borealis::term::make_session_decoder(encoding);
    Collector collector;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        decoder->feed(bytes.subspan(i, 1), collector);
    }
    decoder->finish(collector);
    return {collector.text, decoder->stats()};
}

/// @brief 字节串 → 字节向量（解码夹具的输入即「对端发出的原始字节」）。
auto to_bytes(std::string_view text) -> std::vector<std::byte> {
    std::vector<std::byte> out;
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 某档位下恒可表示的抽样码点（取值来自 iconv 实测，不是表记忆）。
struct LegSamples {
    std::string_view encoding;
    std::u32string text;
};

// 每档都放一位「别的档位不可表示、这一档可表示」的取值，档位接错时断言会红：
// GB18030 有 U+1F600 与四字节区，GBK 有 U+00E9 的双字节位，Big5 有 U+2500 一族，
// Latin-1 与 CP437 各有自己上半区的单字节取值。
const std::array<LegSamples, 6> kRoundTripLegs{
    LegSamples{"UTF-8", U"A\x00E9\x4E2D\x6587\x1F600"},
    LegSamples{"GB18030", U"A\x00E9\x4E2D\x6587\x00A3\x2500\x1F600"},
    LegSamples{"GBK", U"A\x4F60\x597D\x00E9\x2502"},
    LegSamples{"Big5", U"A\x4E2D\x4F60\x2500\x2502"},
    LegSamples{"Latin-1", U"Ab\xA2\xA3\xFF"},
    LegSamples{"CP437", U"Ab\x00A2\x00E9\x2500\x2502"},
};

}  // namespace

AURORA_TEST_CASE(replay_encoding_fixture) {
    // 回放夹具：新增档位边界改数据即可，不必改用例代码。
    const std::string path = aurora::testing::paths::under_repo("tests/fixtures/term/encoding_cases.tsv");
    std::ifstream in(path);
    AURORA_TEST_REQUIRE_MSG(in.is_open(), "fixture not found: " + path);

    std::string line;
    std::size_t replayed = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);  // UTF-8 BOM：夹具首行可能带上
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto columns = split(line, '\t');
        AURORA_TEST_REQUIRE_MSG(columns.size() == 4, "malformed fixture line: " + line);
        const std::string name{columns[0]};
        const std::string_view encoding{columns[1]};
        const auto bytes = parse_bytes(columns[2]);
        const auto expected = parse_expected(columns[3]);
        const auto actual = decode(encoding, bytes);
        ++replayed;

        AURORA_TEST_CHECK_MSG(actual.text == expected,
                              name + ": " + std::string{encoding} + " decoded {" + describe(actual.text) +
                                  "} expected {" + describe(expected) + "}");
    }
    AURORA_TEST_CHECK_MSG(replayed >= 25, "fixture replayed too few cases: " + std::to_string(replayed));
}

AURORA_TEST_CASE(sequence_split_across_feeds_is_not_broken) {
    // 读线程的块边界由对端与传输层决定：半个序列切在任何位置都不该产出中间态或替换字符。
    const auto han = to_bytes("\xD6\xD0");  // GB18030 的「中」
    {
        const auto whole = decode("GB18030", han);
        const auto split = decode_one_byte_at_a_time("GB18030", han);
        AURORA_TEST_CHECK_EQ(split.text, whole.text);
        AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(split.text.front()), std::uint32_t{U'\x4E2D'});
        AURORA_TEST_CHECK_EQ(split.stats.replaced, std::uint64_t{0});
    }
    {
        // 四字节档（GB18030 的 £）切成 2+2：两半都不成序列，拼起来才是。
        const auto pound = to_bytes("\x81\x30\x84\x35");
        const std::span<const std::byte> head{pound.data(), 2};
        const std::span<const std::byte> tail{pound.data() + 2, 2};
        auto decoder = borealis::term::make_session_decoder("GB18030");
        Collector collector;
        decoder->feed(head, collector);
        AURORA_TEST_CHECK(collector.text.empty());
        decoder->feed(tail, collector);
        decoder->finish(collector);
        AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(collector.text.front()), std::uint32_t{U'\x00A3'});
        AURORA_TEST_CHECK_EQ(collector.text.size(), std::size_t{1});
        AURORA_TEST_CHECK_EQ(decoder->stats().replaced, std::uint64_t{0});
    }
    {
        const auto box = to_bytes("\xC4\xB3");  // CP437 的 ─ │
        const auto split = decode_one_byte_at_a_time("CP437", box);
        AURORA_TEST_CHECK(split.text == U"\x2500\x2502");
        AURORA_TEST_CHECK_EQ(split.stats.replaced, std::uint64_t{0});
    }
}

AURORA_TEST_CASE(output_chunk_overflow_still_decodes) {
    // 一次喂入的产物超过内部输出分块（4096 字节）时走 E2BIG 续转路径，不得截断或计数为非法。
    std::vector<std::byte> payload;
    constexpr std::size_t kPairs = 3000;
    payload.reserve(kPairs * 2U);
    for (std::size_t i = 0; i < kPairs; ++i) {
        payload.push_back(std::byte{0xD6});
        payload.push_back(std::byte{0xD0});
    }
    const auto result = decode("GB18030", payload);
    AURORA_TEST_CHECK_EQ(result.text.size(), kPairs);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(result.text.back()), std::uint32_t{U'\x4E2D'});
    AURORA_TEST_CHECK_EQ(result.stats.replaced, std::uint64_t{0});
    AURORA_TEST_CHECK_EQ(result.stats.code_points, static_cast<std::uint64_t>(kPairs));
}

AURORA_TEST_CASE(invalid_bytes_count_and_do_not_stop_the_stream) {
    // 非法字节序列降级为替换字符并计数（`SPEC.NF.RELI.01` 的可观测口径），后续内容照常解出。
    const auto mixed = to_bytes("\x41\x80\x41\xD6\xD0");  // GB18030：0x80 非法
    const auto result = decode("GB18030", mixed);
    AURORA_TEST_CHECK(result.text == U"A\xFFFD" U"A\x4E2D");
    AURORA_TEST_CHECK_EQ(result.stats.replaced, std::uint64_t{1});
    AURORA_TEST_CHECK_EQ(result.stats.code_points, std::uint64_t{4});

    const auto big5_ff = to_bytes("\x41\xFF\x41");
    const auto big5 = decode("Big5", big5_ff);
    AURORA_TEST_CHECK(big5.text == U"A\xFFFD" U"A");
    AURORA_TEST_CHECK_EQ(big5.stats.replaced, std::uint64_t{1});

    // 单字节档没有非法字节：上半区自有编码（ISO-8859-1 的 0xFF 是 ÿ）。
    const auto latin = decode("Latin-1", to_bytes("\xFF"));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(latin.text.front()), std::uint32_t{U'\x00FF'});
    AURORA_TEST_CHECK_EQ(latin.stats.replaced, std::uint64_t{0});
}

AURORA_TEST_CASE(reset_drops_pending_sequence) {
    // 编码切换时的复位：挂起的半个序列既不产出也不补替换字符（复位语义＝丢弃，而非收尾）。
    auto decoder = borealis::term::make_session_decoder("GB18030");
    Collector collector;
    decoder->feed(to_bytes("\xD6"), collector);
    decoder->reset();
    decoder->finish(collector);
    AURORA_TEST_CHECK(collector.text.empty());
    AURORA_TEST_CHECK_EQ(decoder->stats().replaced, std::uint64_t{0});

    // 复位之后仍能正常解下一条完整序列：句柄的移位状态没留脏。
    decoder->feed(to_bytes("\xD6\xD0"), collector);
    decoder->finish(collector);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(collector.text.front()), std::uint32_t{U'\x4E2D'});
}

AURORA_TEST_CASE(truncated_tail_replaced_at_finish) {
    // 对端在半个序列处收摊：补一个替换字符，而不是静默吞掉最后一格。
    const auto result = decode("GB18030", to_bytes("\x81\x30"));
    AURORA_TEST_CHECK_EQ(result.text.size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(result.text.front()),
                         std::uint32_t{static_cast<std::uint32_t>(kReplacementCharacter)});
    AURORA_TEST_CHECK_EQ(result.stats.replaced, std::uint64_t{1});
}

AURORA_TEST_CASE(encode_matches_measured_bytes_per_leg) {
    // 发送方向的字节事实（取自 iconv 实测）：全部可表示时整段一次转换，产物须逐字节相同。
    struct Case {
        std::string_view encoding;
        std::u32string_view text;
        std::string_view bytes;
    };
    const std::array<Case, 8> cases{
        Case{"GB18030", U"\x4E2D\x6587", "\xD6\xD0\xCE\xC4"},
        Case{"GB18030", U"\x00A3", "\x81\x30\x84\x35"},
        Case{"GBK", U"\x4F60\x597D", "\xC4\xE3\xBA\xC3"},
        Case{"Big5", U"\x4E2D", "\xA4\xA4"},
        Case{"Latin-1", U"\x00E9", "\xE9"},
        Case{"CP437", U"\x00E9", "\x82"},
        Case{"CP437", U"\x2500\x2502", "\xC4\xB3"},
        Case{"UTF-8", U"\x4E2D", "\xE4\xB8\xAD"},
    };
    for (const auto &c : cases) {
        const auto result = borealis::term::encode_for_encoding(c.text, c.encoding,
                                                                UnrepresentablePolicy::Replace);
        AURORA_TEST_CHECK_MSG(result.bytes == c.bytes,
                              std::string{c.encoding} + " encoded {" + describe(result.bytes) +
                                  "} expected {" + describe(std::string{c.bytes}) + "}");
        AURORA_TEST_CHECK_EQ(result.unrepresentable, std::size_t{0});
    }
}

AURORA_TEST_CASE(round_trip_every_leg) {
    // 双向一致：各档可表示的抽样码点「编码 → 解码」回到原值，且全程零替换。
    for (const auto &leg : kRoundTripLegs) {
        const auto encoded =
            borealis::term::encode_for_encoding(leg.text, leg.encoding, UnrepresentablePolicy::Replace);
        AURORA_TEST_CHECK_MSG(encoded.unrepresentable == 0U,
                              std::string{leg.encoding} + " lost code points: " +
                                  std::to_string(encoded.unrepresentable));
        const auto decoded = decode(leg.encoding, to_bytes(encoded.bytes));
        AURORA_TEST_CHECK_MSG(decoded.text == leg.text,
                              std::string{leg.encoding} + " round-tripped {" + describe(decoded.text) +
                                  "} from {" + describe(leg.text) + "}");
        AURORA_TEST_CHECK_EQ(decoded.stats.replaced, std::uint64_t{0});
    }
}

AURORA_TEST_CASE(unrepresentable_replace_policy) {
    // 缺省档：目标编码里若有替换字符的编码就用它（GB18030 的 U+FFFD 是四字节），没有则退到 `?`。
    const auto surrogate_in_gb18030 =
        borealis::term::encode_for_encoding(U"\xD800", "GB18030", UnrepresentablePolicy::Replace);
    AURORA_TEST_CHECK_EQ(surrogate_in_gb18030.unrepresentable, std::size_t{1});
    AURORA_TEST_CHECK_MSG(surrogate_in_gb18030.bytes == "\x84\x31\xA4\x37",
                          "GB18030 replacement {" + describe(surrogate_in_gb18030.bytes) + "}");

    const auto big5 = borealis::term::encode_for_encoding(U"A\x00E9" U"B", "Big5",
                                                          UnrepresentablePolicy::Replace);
    AURORA_TEST_CHECK_EQ(big5.unrepresentable, std::size_t{1});
    AURORA_TEST_CHECK_MSG(big5.bytes == "A?B", "Big5 fallback {" + describe(big5.bytes) + "}");

    // UTF-8 档位：代理项与越界值连 UTF-8 形态都没有，按替换字符出。
    const auto utf8 =
        borealis::term::encode_for_encoding(U"\xD800", "UTF-8", UnrepresentablePolicy::Replace);
    AURORA_TEST_CHECK_EQ(utf8.unrepresentable, std::size_t{1});
    AURORA_TEST_CHECK_EQ(utf8.bytes, "\xEF\xBF\xBD");
}

AURORA_TEST_CASE(unrepresentable_drop_policy_emits_no_bytes) {
    // 「丢弃 + 提示」档：不可表示的码点一个字节都不发，其余照常，计数留给一次性提示。
    const auto big5 = borealis::term::encode_for_encoding(U"A\x00E9" U"B", "Big5",
                                                          UnrepresentablePolicy::DropWithNotice);
    AURORA_TEST_CHECK_EQ(big5.unrepresentable, std::size_t{1});
    AURORA_TEST_CHECK_EQ(big5.bytes, "AB");

    const auto utf8 =
        borealis::term::encode_for_encoding(U"\xD800", "UTF-8", UnrepresentablePolicy::DropWithNotice);
    AURORA_TEST_CHECK_EQ(utf8.unrepresentable, std::size_t{1});
    AURORA_TEST_CHECK(utf8.bytes.empty());
}

AURORA_TEST_CASE(unrepresentable_pass_through_policy_sends_utf8) {
    // 「原样透传」档：该码点以其 UTF-8 字节发出，由对端自行处置；其余仍按目标编码。
    const auto big5 = borealis::term::encode_for_encoding(U"A\x00E9" U"B", "Big5",
                                                          UnrepresentablePolicy::PassThroughUtf8);
    AURORA_TEST_CHECK_EQ(big5.unrepresentable, std::size_t{1});
    AURORA_TEST_CHECK_EQ(big5.bytes, "A\xC3\xA9"
                                     "B");

    const auto latin = borealis::term::encode_for_encoding(U"\x4E2D", "Latin-1",
                                                           UnrepresentablePolicy::PassThroughUtf8);
    AURORA_TEST_CHECK_EQ(latin.unrepresentable, std::size_t{1});
    AURORA_TEST_CHECK_EQ(latin.bytes, "\xE4\xB8\xAD");

    // 越界值连 UTF-8 形态都不存在，透传档与替换档同归（不存在「原样」可发）。
    const std::u32string beyond_scalar_range{static_cast<char32_t>(0x110000)};
    const auto out_of_range = borealis::term::encode_for_encoding(
        beyond_scalar_range, "Big5", UnrepresentablePolicy::PassThroughUtf8);
    AURORA_TEST_CHECK_EQ(out_of_range.unrepresentable, std::size_t{1});
    AURORA_TEST_CHECK_EQ(out_of_range.bytes, "?");
}

AURORA_TEST_CASE(unknown_encoding_falls_back_to_utf8_on_send) {
    // 发送侧回落与解码侧同一条口径：映射不到的名字按 UTF-8 发，绝不静默发乱码字节。
    const auto result = borealis::term::encode_for_encoding(U"\x4E2D", "NOT-A-CODEC",
                                                            UnrepresentablePolicy::Replace);
    AURORA_TEST_CHECK_EQ(result.unrepresentable, std::size_t{0});
    AURORA_TEST_CHECK_EQ(result.bytes, "\xE4\xB8\xAD");
}

AURORA_TEST_CASE(empty_text_encodes_to_empty_bytes) {
    for (const auto &leg : kRoundTripLegs) {
        const auto result =
            borealis::term::encode_for_encoding(U"", leg.encoding, UnrepresentablePolicy::Replace);
        AURORA_TEST_CHECK(result.bytes.empty());
        AURORA_TEST_CHECK_EQ(result.unrepresentable, std::size_t{0});
    }
}

AURORA_TEST_CASE(decoder_factory_defaults_and_never_null) {
    // 装配层传空串/缺省档时必须是可用的 UTF-8 腿，而不是空指针或非法档位。
    for (const std::string_view name : {"", "UTF-8", "utf-8", "NOT-A-CODEC"}) {
        auto decoder = borealis::term::make_session_decoder(name);
        AURORA_TEST_REQUIRE(decoder != nullptr);
        Collector collector;
        decoder->feed(to_bytes("\xE4\xB8\xAD"), collector);
        decoder->finish(collector);
        AURORA_TEST_CHECK_MSG(collector.text == U"\x4E2D", std::string{name} + " fell off UTF-8 leg");
    }
    const borealis::term::SessionEncoding defaults{};
    AURORA_TEST_CHECK_EQ(defaults.name, "UTF-8");
    AURORA_TEST_CHECK(defaults.unrepresentable == UnrepresentablePolicy::Replace);
}

AURORA_TEST_CASE(resolve_encoding_name_reports_the_effective_leg) {
    // 裁决 7.104 的 D6②：提示卡片报的是实际生效腿，取的是与 make_session_decoder 同一张别名表
    // 的**配置名**一侧——设置页那枚下拉显示的就是这六个名字，报 iconv 线名（ISO-8859-1 一类）
    // 会和它对不上。
    for (const std::string_view name : {"UTF-8", "GB18030", "GBK", "Big5", "Latin-1", "CP437"}) {
        AURORA_TEST_CHECK_MSG(borealis::term::resolve_encoding_name(name) == name,
                              std::string{name} + " folded to a different leg name");
    }
    // ASCII 大小写不敏感，口径与工厂一致。
    AURORA_TEST_CHECK_EQ(borealis::term::resolve_encoding_name("big5"), "Big5");
    AURORA_TEST_CHECK_EQ(borealis::term::resolve_encoding_name("gb18030"), "GB18030");
    // 认不到的名字实际跑的是 UTF-8 腿，卡片不能说谎（填 GB2312 的会话报 UTF-8）。
    for (const std::string_view name : {"GB2312", "NOT-A-CODEC", ""}) {
        AURORA_TEST_CHECK_EQ(borealis::term::resolve_encoding_name(name), "UTF-8");
    }
}

}  // namespace borealis::test_cases::utest_codec
