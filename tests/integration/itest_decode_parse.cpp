/// 测试类型: integration
/// 目标单元: include/borealis/term/utf8.h + include/borealis/vt/parser.h
/// 测试说明: 打通架构 §3.3 的前半链路「会话字节流 → 解码 → VT 解析」：以一段真实形态的
///           分屏 + 提示符输出（tests/fixtures/vt/scene_tmux_frame.txt）回放，断言解码与解析
///           接缝处不丢事件、DEC 字符集切换与 OSC/SGR 均被结构化出来，且混入非法字节后
///           链路继续（不中断、不污染后续）。
///
///           注意：语义解释（把 `ESC ( 0` 之后的 l/q/k 画成线条）归终端状态机，本测试只
///           断言「结构正确」——语义尚未落地，越界断言会在语义层返工时变成负担。

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/term/utf8.h"
#include "borealis/vt/parser.h"
#include "borealis/vt/sequence.h"
#include "framework/aurora_test.h"
#include "support/fixture_text.h"
#include "support/paths.h"

namespace borealis::test_cases::itest_decode_parse {

namespace {

using borealis::testing::fixtures::unescape;
using borealis::term::CodePointSink;
using borealis::term::Utf8Decoder;
using borealis::vt::Parser;
using borealis::vt::Sequence;
using borealis::vt::SequenceKind;
using borealis::vt::SequenceSink;

/// @brief 解析出的事件快照（Sequence 内视图仅回调期有效，故先拷成值）。
struct Event {
    SequenceKind kind = SequenceKind::Print;
    char32_t code_point = 0;
    std::u32string intermediates;
    std::u32string data;
    char32_t final_byte = 0;
    std::vector<std::vector<std::int32_t>> params;
    bool has_private_prefix = false;
};

/// @brief 把码点流转成解析事件的接收端：解码器产出即喂入解析器。
class ParserFeeder final : public CodePointSink, public SequenceSink {
  public:
    auto on_code_point(char32_t cp) -> void override {
        const std::u32string_view slice{&cp, 1};
        parser.feed(slice, *this);
    }

    auto on_sequence(const Sequence &seq) -> void override {
        Event event{};
        event.kind = seq.kind;
        event.code_point = seq.code_point;
        event.intermediates = std::u32string{seq.intermediates};
        event.data = std::u32string{seq.data};
        event.final_byte = seq.final_byte;
        for (const auto &param : seq.params) {
            event.params.push_back(param.sub);
        }
        event.has_private_prefix = !seq.intermediates.empty() && seq.intermediates.front() == U'?';
        events.push_back(std::move(event));
    }

    Parser parser;
    std::vector<Event> events;
};

/// @brief 统计满足谓词的事件数。
template <typename Predicate>
auto count_if(const std::vector<Event> &events, Predicate predicate) -> std::size_t {
    std::size_t total = 0;
    for (const auto &event : events) {
        if (predicate(event)) {
            ++total;
        }
    }
    return total;
}

}  // namespace

AURORA_TEST_CASE(decoded_scene_yields_expected_structure) {
    const std::string path = aurora::testing::paths::under_repo("tests/fixtures/vt/scene_tmux_frame.txt");
    std::ifstream in(path);
    AURORA_TEST_REQUIRE_MSG(in.is_open(), "fixture not found: " + path);
    std::string line;
    AURORA_TEST_REQUIRE(static_cast<bool>(std::getline(in, line)));
    const auto bytes = unescape(line);

    ParserFeeder feeder;
    Utf8Decoder decoder;
    std::vector<std::byte> raw;
    for (const char c : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    decoder.feed(raw, feeder);
    decoder.finish(feeder);

    // 备屏进入/退出、清屏、光标归位：终端程序开场的标准动作都得被结构化出来。
    AURORA_TEST_CHECK(count_if(feeder.events, [](const Event &e) {
                          return e.kind == SequenceKind::Csi && e.final_byte == U'h' && e.has_private_prefix;
                      }) == std::size_t{2});
    AURORA_TEST_CHECK(count_if(feeder.events, [](const Event &e) {
                          return e.kind == SequenceKind::Csi && e.final_byte == U'J';
                      }) == std::size_t{1});

    // DEC Special Graphics：进入与退出成对出现（`ESC ( 0` / `ESC ( B`），
    // 且二者之间的边框字符以 Print 事件透传——验收线要求这些字符不得显示为乱码字母。
    const auto charset_enters = count_if(feeder.events, [](const Event &e) {
        return e.kind == SequenceKind::Escape && e.intermediates == U"(" && e.final_byte == U'0';
    });
    const auto charset_exits = count_if(feeder.events, [](const Event &e) {
        return e.kind == SequenceKind::Escape && e.intermediates == U"(" && e.final_byte == U'B';
    });
    AURORA_TEST_CHECK_EQ(charset_enters, std::size_t{4});
    AURORA_TEST_CHECK_EQ(charset_exits, std::size_t{4});

    // OSC 标题、真彩色 SGR、bracketed paste 开启：现代提示符生态的三件套。
    AURORA_TEST_CHECK(count_if(feeder.events, [](const Event &e) {
                          return e.kind == SequenceKind::Osc && e.data.rfind(U"0;", 0) == 0;
                      }) == std::size_t{1});
    AURORA_TEST_CHECK(count_if(feeder.events, [](const Event &e) {
                          return e.kind == SequenceKind::Csi && e.final_byte == U'm' && !e.params.empty() &&
                                 e.params.front().size() >= std::size_t{5};
                      }) >= std::size_t{1});

    // 混入的非法字节（0xC0 0xAF）被替换而不是中断链路：其后仍有正常的 SGR 事件。
    AURORA_TEST_CHECK_EQ(decoder.stats().replaced, std::uint64_t{2});
    const auto tail = feeder.events.back();
    AURORA_TEST_CHECK(tail.kind == SequenceKind::Csi);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(tail.final_byte), std::uint32_t{U'l'});
}

}  // namespace borealis::test_cases::itest_decode_parse
