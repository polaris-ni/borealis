/// 测试类型: unit
/// 目标单元: include/borealis/term/utf8.h
/// 测试说明: UTF-8 解码（严格口径：拒绝过长编码、代理项、越界值，非法按 U+FFFD 降级并计数、
///           跨 feed 分片保持状态、finish 收尾）与编码（不可表示码点替换、往返一致）；
///           并回放 tests/fixtures/term/utf8_cases.tsv 做批量断言。

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/term/utf8.h"
#include "framework/aurora_test.h"
#include "support/paths.h"

namespace borealis::test_cases::utest_utf8 {

namespace {

using borealis::term::CodePointSink;
using borealis::term::DecodeStats;
using borealis::term::kReplacementCharacter;
using borealis::term::Utf8Decoder;

/// @brief 收集解码产出的码点。
class Collector final : public CodePointSink {
  public:
    auto on_code_point(char32_t cp) -> void override { text += cp; }

    std::u32string text;
};

/// @brief 一次完整解码（喂入后 finish）的产出。
struct DecodeResult {
    std::u32string text;
    DecodeStats stats;
};

/// @brief 解码整段字节并收尾。
/// @param bytes 原始字节。
/// @return 码点串与诊断计数。
auto decode(std::span<const std::byte> bytes) -> DecodeResult {
    Utf8Decoder decoder;
    Collector collector;
    decoder.feed(bytes, collector);
    decoder.finish(collector);
    return {collector.text, decoder.stats()};
}

/// @brief 十六进制字节解析（夹具输入列）。
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
        if (text[i] == 'R') { // 夹具约定的替换字符简写
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

}  // namespace

AURORA_TEST_CASE(replay_fixture_cases) {
    // 回放夹具：新增编码边界改数据即可，不必改用例代码。
    const std::string path = aurora::testing::paths::under_repo("tests/fixtures/term/utf8_cases.tsv");
    std::ifstream in(path);
    AURORA_TEST_REQUIRE_MSG(in.is_open(), "fixture not found: " + path);

    std::string line;
    std::size_t replayed = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3); // UTF-8 BOM：夹具首行会带上
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto columns = split(line, '\t');
        AURORA_TEST_REQUIRE_MSG(columns.size() == 3, "malformed fixture line: " + line);
        const std::string name{columns[0]};
        const auto bytes = parse_bytes(columns[1]);
        const auto expected = parse_expected(columns[2]);
        const auto actual = decode(bytes);
        ++replayed;

        AURORA_TEST_CHECK_MSG(actual.text == expected,
                              name + ": decoded {" + describe(actual.text) + "} expected {" + describe(expected) + "}");
    }
    AURORA_TEST_CHECK_MSG(replayed >= 20, "fixture replayed too few cases: " + std::to_string(replayed));
}

AURORA_TEST_CASE(decoder_survives_chunk_boundary) {
    // 多字节序列被读缓冲切断时不得产出中间态：两次喂入与一次喂入结果相同。
    const std::vector<std::byte> bytes{std::byte{0xE4}, std::byte{0xB8}, std::byte{0xAD}};
    Utf8Decoder decoder;
    Collector collector;
    decoder.feed(std::span<const std::byte>{bytes.data(), 2}, collector);
    AURORA_TEST_CHECK(decoder.has_pending());
    AURORA_TEST_CHECK(collector.text.empty());
    decoder.feed(std::span<const std::byte>{bytes.data() + 2, 1}, collector);
    AURORA_TEST_CHECK_FALSE(decoder.has_pending());
    // CJK-LITERAL: cjk-fixture - the decoded Han code point must survive a chunk boundary
    AURORA_TEST_CHECK(collector.text == U"\x4E2D");
}

AURORA_TEST_CASE(finish_replaces_incomplete_sequence) {
    // 流结束时仍挂着的残缺序列按替换字符收尾，而不是静默丢弃一个字符。
    const std::vector<std::byte> bytes{std::byte{0x41}, std::byte{0xE4}, std::byte{0xB8}};
    const auto result = decode(bytes);
    AURORA_TEST_CHECK(result.text.size() == std::size_t{2});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(result.text[0]), std::uint32_t{U'A'});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(result.text[1]),
                         std::uint32_t{static_cast<std::uint32_t>(kReplacementCharacter)});
    AURORA_TEST_CHECK_EQ(result.stats.replaced, std::uint64_t{1});
}

AURORA_TEST_CASE(stats_count_replacements_and_code_points) {
    // 非法序列计数进可观测面板（SPEC.NF.RELI.01），且不得中断后续解析。
    const std::vector<std::byte> bytes{std::byte{0xC0}, std::byte{0x80}, std::byte{0x41}};
    const auto result = decode(bytes);
    AURORA_TEST_CHECK_EQ(result.stats.replaced, std::uint64_t{2});
    AURORA_TEST_CHECK_EQ(result.stats.code_points, std::uint64_t{3});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(result.text.back()), std::uint32_t{U'A'});
}

AURORA_TEST_CASE(encode_replaces_unrepresentable_code_points) {
    // 发送方向：代理项与越界值在 UTF-8 里没有合法编码，按替换字符处理（架构 §6.2）。
    std::string out;
    AURORA_TEST_CHECK_FALSE(borealis::term::append_utf8(U'A', out));
    AURORA_TEST_CHECK(out == "A");

    std::string replaced_out;
    AURORA_TEST_CHECK(borealis::term::append_utf8(U'\xD800', replaced_out));
    AURORA_TEST_CHECK(replaced_out == "\xEF\xBF\xBD");

    std::string beyond_out;
    AURORA_TEST_CHECK(borealis::term::append_utf8(static_cast<char32_t>(0x110000), beyond_out));
    AURORA_TEST_CHECK(beyond_out == "\xEF\xBF\xBD");

    // CJK-LITERAL: cjk-fixture - Han text must encode to the exact GB18030-independent UTF-8 bytes
    std::string cjk_out;
    AURORA_TEST_CHECK_EQ(borealis::term::encode_utf8(U"\x4E2D\x6587", cjk_out), std::size_t{0});
    AURORA_TEST_CHECK(cjk_out == "\xE4\xB8\xAD\xE6\x96\x87");
}

AURORA_TEST_CASE(round_trip_across_scalar_range) {
    // 编码再解码须回到原值：抽样覆盖各长度边界与若干平面内取值。
    const std::vector<char32_t> samples{
        U'\x0000', U'\x0001', U'\x007F', U'\x0080',   U'\x07FF',   U'\x0800', U'\xFFFD',
        U'\xFFFF', static_cast<char32_t>(0x10000), static_cast<char32_t>(0x1F600),
        static_cast<char32_t>(0x10FFFF), U'\x4E2D',
    };
    std::size_t checked = 0;
    for (const char32_t cp : samples) {
        std::string bytes;
        AURORA_TEST_CHECK_FALSE(borealis::term::append_utf8(cp, bytes));
        std::vector<std::byte> raw;
        for (const char c : bytes) {
            raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        const auto back = decode(raw);
        AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(back.text.front()), static_cast<std::uint32_t>(cp));
        AURORA_TEST_CHECK_EQ(back.stats.replaced, std::uint64_t{0});
        ++checked;
    }
    AURORA_TEST_CHECK_EQ(checked, samples.size());
}

}  // namespace borealis::test_cases::utest_utf8
