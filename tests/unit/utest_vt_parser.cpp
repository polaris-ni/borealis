/// 测试类型: unit
/// 目标单元: include/borealis/vt/parser.h + include/borealis/vt/sequence.h
/// 测试说明: 表驱动 VT 解析器的结构化输出——可打印字符与 C0 执行、ESC / CSI（参数、子参数、
///           私有前缀、中间字节）、OSC 字符串、DCS 三段式、取消与降级计数、跨 feed 分片与
///           reset；并回放 tests/fixtures/vt/parser_cases.tsv 做批量断言。

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/vt/parser.h"
#include "borealis/vt/sequence.h"
#include "framework/aurora_test.h"
#include "support/paths.h"

namespace borealis::test_cases::utest_vt_parser {

namespace {

using borealis::vt::Param;
using borealis::vt::Parser;
using borealis::vt::ParseStats;
using borealis::vt::Sequence;
using borealis::vt::SequenceKind;
using borealis::vt::State;

/// @brief 事件快照：Sequence 内视图仅回调期有效，故断言前先拷成值。
struct Record {
    SequenceKind kind = SequenceKind::Print;
    char32_t code_point = 0;
    std::vector<std::vector<std::int32_t>> params;
    std::u32string intermediates;
    std::u32string data;
    char32_t final_byte = 0;
};

/// @brief 收集全部事件的接收端。
class Recorder final : public borealis::vt::SequenceSink {
  public:
    auto on_sequence(const Sequence &seq) -> void override {
        Record r{};
        r.kind = seq.kind;
        r.code_point = seq.code_point;
        for (const auto &param : seq.params) {
            r.params.push_back(param.sub);
        }
        r.intermediates = std::u32string{seq.intermediates};
        r.data = std::u32string{seq.data};
        r.final_byte = seq.final_byte;
        records.push_back(std::move(r));
    }

    std::vector<Record> records;
};

/// @brief 一次解析的产出：事件序列 + 诊断计数。
struct ParseResult {
    std::vector<Record> records;
    ParseStats stats;
};

/// @brief 一次性喂入整段输入并收集结果。
/// @param input 已解码的码点流。
/// @return 事件与诊断计数。
auto parse(std::u32string_view input) -> ParseResult {
    Parser parser;
    Recorder recorder;
    parser.feed(input, recorder);
    return {recorder.records, parser.stats()};
}

/// @brief 事件逐字段相等判定（含参数、中间字节、负载与终结符）。
/// @param lhs 实际事件。
/// @param rhs 期望事件。
/// @return 是否完全一致。
auto same(const Record &lhs, const Record &rhs) -> bool {
    return lhs.kind == rhs.kind && lhs.code_point == rhs.code_point && lhs.params == rhs.params &&
           lhs.intermediates == rhs.intermediates && lhs.data == rhs.data && lhs.final_byte == rhs.final_byte;
}

/// @brief 按分隔符切分，空片段保留（`;31` → 空段 + `31`，对应用缺省参数）。
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

/// @brief 切分且限制段数，末段吞掉剩余（避免负载里的分隔符被切开）。
auto split_limited(std::string_view text, char sep, std::size_t max_parts) -> std::vector<std::string_view> {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (out.size() + 1 < max_parts) {
        const auto pos = text.find(sep, start);
        if (pos == std::string_view::npos) {
            break;
        }
        out.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    out.push_back(text.substr(start));
    return out;
}

/// @brief 夹具转义还原：`\e` `\a` `\s` `\n` `\r` `\t` `\\` `\uXXXX` `\xNN`。
/// @param text 夹具中的转义文本（全 ASCII）。
/// @return 还原出的码点流。
auto unescape(std::string_view text) -> std::u32string {
    std::u32string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += static_cast<char32_t>(static_cast<unsigned char>(text[i]));
            continue;
        }
        const char esc = text[i + 1];
        ++i;
        switch (esc) {
            case 'e':
                out += U'\x1B';
                break;
            case 'a':
                out += U'\x07';
                break;
            case 's':
                out += U'\x9C';
                break;
            case 'n':
                out += U'\x0A';
                break;
            case 'r':
                out += U'\x0D';
                break;
            case 't':
                out += U'\x09';
                break;
            case '\\':
                out += U'\\';
                break;
            case 'u':
                out += static_cast<char32_t>(std::stoul(std::string{text.substr(i + 1, 4)}, nullptr, 16));
                i += 4;
                break;
            case 'x':
                out += static_cast<char32_t>(std::stoul(std::string{text.substr(i + 1, 2)}, nullptr, 16));
                i += 2;
                break;
            default:
                out += static_cast<char32_t>(static_cast<unsigned char>(esc));
                break;
        }
    }
    return out;
}

/// @brief 解析夹具的 params 字段（`;` 分参数、`:` 分子参数，空段即缺省）。
auto parse_params(std::string_view text) -> std::vector<std::vector<std::int32_t>> {
    std::vector<std::vector<std::int32_t>> out;
    if (text.empty()) {
        return out;
    }
    for (const auto &param : split(text, ';')) {
        std::vector<std::int32_t> sub;
        if (!param.empty()) {
            for (const auto &value : split(param, ':')) {
                sub.push_back(value.empty() ? 0 : std::stoi(std::string{value}));
            }
        }
        out.push_back(sub);
    }
    return out;
}

/// @brief 安全取字段：夹具是外部数据，段数不足时不得越界。
auto field(const std::vector<std::string_view> &fields, std::size_t index) -> std::string_view {
    return index < fields.size() ? fields[index] : std::string_view{};
}

/// @brief 取字段还原后的首个码点；字段为空时给 0（越界比错值更糟）。
auto first_code_point(std::string_view text) -> char32_t {
    const auto decoded = unescape(text);
    return decoded.empty() ? 0 : decoded.front();
}

/// @brief 解析一条夹具期望事件；`PRINT` 可展开为多条，无法识别的写法返回空表。
auto parse_event(std::string_view spec) -> std::vector<Record> {
    const auto fields = split_limited(spec, ':', 4);
    const auto tag = fields.front();
    std::string_view rest{};
    if (spec.size() > tag.size()) {
        rest = spec.substr(tag.size() + 1);
    }
    std::vector<Record> out;

    if (tag == "PRINT") {
        for (const char32_t c : unescape(rest)) {
            Record r{};
            r.kind = SequenceKind::Print;
            r.code_point = c;
            out.push_back(r);
        }
        return out;
    }
    Record r{};
    if (tag == "EXEC") {
        r.kind = SequenceKind::Execute;
        r.code_point = first_code_point(rest);
    } else if (tag == "ESC") {
        r.kind = SequenceKind::Escape;
        r.intermediates = unescape(field(fields, 1));
        r.final_byte = first_code_point(field(fields, 2));
    } else if (tag == "CSI") {
        // 参数置于末段：其内部可含 `:`，只有末段能整段吞下。
        r.kind = SequenceKind::Csi;
        r.intermediates = unescape(field(fields, 1));
        r.final_byte = first_code_point(field(fields, 2));
        r.params = parse_params(field(fields, 3));
    } else if (tag == "OSC") {
        r.kind = SequenceKind::Osc;
        r.data = unescape(rest);
    } else if (tag == "DCSH") {
        r.kind = SequenceKind::DcsHook;
        r.intermediates = unescape(field(fields, 1));
        r.final_byte = first_code_point(field(fields, 2));
        r.params = parse_params(field(fields, 3));
    } else if (tag == "DCSP") {
        r.kind = SequenceKind::DcsPut;
        r.data = unescape(rest);
    } else if (tag == "DCSU") {
        r.kind = SequenceKind::DcsUnhook;
        r.data = unescape(rest);
    } else if (tag == "IGN") {
        r.kind = SequenceKind::Ignored;
        r.code_point = first_code_point(rest);
    } else {
        return out; // 未知写法：回空表，由用例按「期望解析失败」报错，避免静默拿到默认值
    }
    out.push_back(r);
    return out;
}

/// @brief 解析整条期望（`|` 分隔多条事件）。
auto parse_expected(std::string_view spec) -> std::vector<Record> {
    std::vector<Record> out;
    for (const auto &event : split(spec, '|')) {
        if (event.empty()) {
            continue;
        }
        for (auto &record : parse_event(event)) {
            out.push_back(std::move(record));
        }
    }
    return out;
}

/// @brief 事件的可读描述，供失败消息定位。
auto describe(const Record &r) -> std::string {
    std::string out = "kind=" + std::to_string(static_cast<int>(r.kind));
    out += " cp=" + std::to_string(static_cast<std::uint32_t>(r.code_point));
    out += " final=" + std::to_string(static_cast<std::uint32_t>(r.final_byte));
    out += " params=" + std::to_string(r.params.size());
    out += " inter=" + std::to_string(r.intermediates.size());
    out += " data=" + std::to_string(r.data.size());
    return out;
}

}  // namespace

AURORA_TEST_CASE(param_or_falls_back_on_default) {
    // 缺省参数（未给数字）按调用方给定的默认值取用，越界同样按缺省处理。
    const std::vector<Param> params = {Param{.sub = {31}}, Param{}};
    AURORA_TEST_CHECK_EQ(borealis::vt::param_or(params, 0, 1), std::int32_t{31});
    AURORA_TEST_CHECK_EQ(borealis::vt::param_or(params, 1, 1), std::int32_t{1});
    AURORA_TEST_CHECK_EQ(borealis::vt::param_or(params, 9, 7), std::int32_t{7});
}

AURORA_TEST_CASE(subparams_keep_colon_separated_values) {
    // 真彩色 `38:2::12:34:56`：空子参数段须保留为缺省 0，否则通道位整体错位。
    const auto result = parse(U"\x1B[38:2::12:34:56m");
    AURORA_TEST_REQUIRE_EQ(result.records.size(), std::size_t{1});
    const auto &event = result.records.front();
    AURORA_TEST_CHECK(event.kind == SequenceKind::Csi);
    AURORA_TEST_REQUIRE_EQ(event.params.size(), std::size_t{1});
    const std::vector<std::int32_t> expected{38, 2, 0, 12, 34, 56};
    AURORA_TEST_CHECK_MSG(event.params.front() == expected, "subparams mismatch: " + describe(event));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(event.final_byte), std::uint32_t{U'm'});
}

AURORA_TEST_CASE(cjk_code_points_pass_through_as_print) {
    // CJK-LITERAL: cjk-fixture - the parser must treat decoded Han code points as ordinary printable cells
    const auto result = parse(U"\x1B[1m中文\x1B[0m");
    AURORA_TEST_REQUIRE_EQ(result.records.size(), std::size_t{4});
    AURORA_TEST_CHECK(result.records[1].kind == SequenceKind::Print);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(result.records[1].code_point), std::uint32_t{0x4E2D});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(result.records[2].code_point), std::uint32_t{0x6587});
}

AURORA_TEST_CASE(sequence_survives_chunk_boundary) {
    // 序列跨读缓冲边界不得被切断：会话读线程按任意块大小喂入都应得到同一结果。
    Parser parser;
    Recorder recorder;
    parser.feed(U"\x1B[1", recorder);
    AURORA_TEST_CHECK(parser.state() == State::CsiParam);
    parser.feed(U"2mAB", recorder);
    AURORA_TEST_REQUIRE_EQ(recorder.records.size(), std::size_t{3});
    AURORA_TEST_CHECK(recorder.records[0].kind == SequenceKind::Csi);
    AURORA_TEST_CHECK(recorder.records[0].params.size() == std::size_t{1});
    AURORA_TEST_CHECK_EQ(recorder.records[0].params.front().front(), std::int32_t{12});
    AURORA_TEST_CHECK(recorder.records[1].kind == SequenceKind::Print);
    AURORA_TEST_CHECK(recorder.records[2].kind == SequenceKind::Print);
}

AURORA_TEST_CASE(reset_drops_partial_sequence) {
    // reset 后残留在解析器里的半个序列必须丢弃，其后的字符按 Ground 语义解释。
    Parser parser;
    Recorder recorder;
    parser.feed(U"\x1B[12", recorder);
    parser.reset();
    AURORA_TEST_CHECK(parser.state() == State::Ground);
    parser.feed(U"m", recorder);
    AURORA_TEST_REQUIRE_EQ(recorder.records.size(), std::size_t{1});
    AURORA_TEST_CHECK(recorder.records.front().kind == SequenceKind::Print);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(recorder.records.front().code_point), std::uint32_t{U'm'});
}

AURORA_TEST_CASE(degrade_counters_and_continued_parsing) {
    // 未识别序列只降级不中断：计数累计，后续序列照常解析。
    const auto result = parse(U"\x1B[1?m\x1B[2J");
    AURORA_TEST_CHECK_EQ(result.stats.ignored, std::uint64_t{2});
    AURORA_TEST_CHECK_EQ(result.stats.cancelled, std::uint64_t{0});
    const auto tail = result.records.back();
    AURORA_TEST_CHECK(tail.kind == SequenceKind::Csi);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(tail.final_byte), std::uint32_t{U'J'});
}

AURORA_TEST_CASE(cancel_counts_and_recovers_to_ground) {
    // CAN/SUB 打断序列后回到 Ground，后续输入不再被吞。
    // 注意：十六进制转义会尽可能多吃字符，故 CAN 与后续字符必须分成两段字面量。
    const auto result = parse(U"\x1B[1;2\x18" "A");
    AURORA_TEST_CHECK_EQ(result.stats.cancelled, std::uint64_t{1});
    AURORA_TEST_REQUIRE_EQ(result.records.size(), std::size_t{2});
    AURORA_TEST_CHECK(result.records[0].kind == SequenceKind::Ignored);
    AURORA_TEST_CHECK(result.records[1].kind == SequenceKind::Print);
}

AURORA_TEST_CASE(dcs_stream_reports_hook_put_unhook) {
    // DCS 三段式：起始带参数与终结符，负载成块透传，ST 收尾（收尾事件自身不带负载）。
    const auto result = parse(U"\x1BP1;2$ppayload\x1B\\");
    AURORA_TEST_REQUIRE_EQ(result.records.size(), std::size_t{3});
    AURORA_TEST_CHECK(result.records[0].kind == SequenceKind::DcsHook);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(result.records[0].final_byte), std::uint32_t{U'p'});
    AURORA_TEST_CHECK(result.records[0].intermediates == U"$");
    AURORA_TEST_CHECK(result.records[1].kind == SequenceKind::DcsPut);
    AURORA_TEST_CHECK(result.records[1].data == U"payload");
    AURORA_TEST_CHECK(result.records[2].kind == SequenceKind::DcsUnhook);
    AURORA_TEST_CHECK(result.records[2].data.empty());
}

AURORA_TEST_CASE(replay_fixture_cases) {
    // 回放夹具：新增边界只需改 tsv，不必改用例代码。
    const std::string path = aurora::testing::paths::under_repo("tests/fixtures/vt/parser_cases.tsv");
    std::ifstream in(path);
    AURORA_TEST_REQUIRE_MSG(in.is_open(), "fixture not found: " + path);

    std::string line;
    std::size_t replayed = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3); // UTF-8 BOM：夹具首行会带上，须剥掉再判注释
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto columns = split(line, '\t');
        AURORA_TEST_REQUIRE_MSG(columns.size() == 3, "malformed fixture line: " + line);
        const std::string name{columns[0]};
        const auto input = unescape(columns[1]);
        const auto expected = parse_expected(columns[2]);
        AURORA_TEST_REQUIRE_MSG(!expected.empty(), name + ": expectation could not be parsed");
        const auto actual = parse(input);
        ++replayed;

        AURORA_TEST_CHECK_MSG(actual.records.size() == expected.size(),
                              name + ": event count " + std::to_string(actual.records.size()) + " != " +
                                      std::to_string(expected.size()));
        const std::size_t count = actual.records.size() < expected.size() ? actual.records.size() : expected.size();
        for (std::size_t i = 0; i < count; ++i) {
            AURORA_TEST_CHECK_MSG(same(actual.records[i], expected[i]),
                                  name + "[" + std::to_string(i) + "] actual{" + describe(actual.records[i]) +
                                          "} expected{" + describe(expected[i]) + "}");
        }
    }
    AURORA_TEST_CHECK_MSG(replayed >= 20, "fixture replayed too few cases: " + std::to_string(replayed));
}

}  // namespace borealis::test_cases::utest_vt_parser
