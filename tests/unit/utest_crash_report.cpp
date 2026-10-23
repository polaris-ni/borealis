/// 测试类型: unit
/// 目标单元: include/borealis/diag/crash_report.h + src/diag/crash_report.cpp
/// 测试说明: 崩溃留存腿的纯逻辑面（SPEC.NF.RELI.01，裁决 7.142）——固定文本的头部/计数/会话行、
///           空快照不吐会话行、四档连接类型与存活位的拼写、发布侧把会话条数夹到上限且翻双缓冲、
///           文本只含白名单键与 ASCII（不许自由文本混进来）、缺触发器名回落 unknown。
///           信号上下文本身（平台腿）不在此套件内：它靠源码审查与真机走查，见裁决 7.142⑤⑥。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "borealis/conn/profile.h"
#include "borealis/diag/crash_report.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_crash_report {

namespace {

using borealis::conn::ConnectionType;
using borealis::diag::CrashFactsSnapshot;
using borealis::diag::CrashFactsStore;
using borealis::diag::CrashSessionFact;
using borealis::diag::kMaxCrashSessions;

struct Collected {
    std::string text;
    std::size_t calls = 0;
};

void collect(void *context, const char *data, std::size_t size) {
    auto &out = *static_cast<Collected *>(context);
    out.text.append(data, size);
    ++out.calls;
}

}  // namespace

AURORA_TEST_CASE(report_writes_header_counts_and_sessions) {
    CrashFactsSnapshot facts;
    facts.windows = 2;
    facts.tabs = 3;
    facts.panes = 4;
    facts.last_publish_epoch = 1699999990;
    facts.session_count = 2;
    facts.sessions[0] = CrashSessionFact{
        .tab = 1, .pane = 1, .kind = ConnectionType::Local, .alive = true};
    facts.sessions[1] = CrashSessionFact{
        .tab = 2, .pane = 3, .kind = ConnectionType::Ssh, .alive = false};

    Collected out;
    borealis::diag::emit_crash_report(&collect, &out, facts, "SIGSEGV", 11, 1700000000);

    const std::string expected = "borealis crash report v1\n" "version=" +
                                 std::string{borealis::diag::product_version()} +
                                 "\n"
                                 "trigger=SIGSEGV\n"
                                 "trigger_code=11\n"
                                 "crashed_at_epoch=1700000000\n"
                                 "last_publish_epoch=1699999990\n"
                                 "windows=2\n"
                                 "tabs=3\n"
                                 "panes=4\n"
                                 "sessions=2\n"
                                 "session tab=1 pane=1 kind=local alive=1\n"
                                 "session tab=2 pane=3 kind=ssh alive=0\n";
    AURORA_TEST_CHECK_EQ(out.text, expected);
    // 写入是分块的（处理函数一帧一帧交给 write），不是攒成一大段再发。
    AURORA_TEST_CHECK_GT(out.calls, 1U);
}

AURORA_TEST_CASE(empty_snapshot_reports_zeros_without_session_lines) {
    const CrashFactsSnapshot facts;
    Collected out;
    borealis::diag::emit_crash_report(&collect, &out, facts, "SIGABRT", 6, 42);

    AURORA_TEST_CHECK_NE(out.text.find("windows=0\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(out.text.find("tabs=0\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(out.text.find("panes=0\n"), std::string::npos);
    AURORA_TEST_CHECK_NE(out.text.find("sessions=0\n"), std::string::npos);
    AURORA_TEST_CHECK_EQ(out.text.find("session tab="), std::string::npos);
}

AURORA_TEST_CASE(publish_round_trips_and_clamps_at_capacity) {
    CrashFactsStore store;
    CrashFactsSnapshot facts;
    facts.windows = 1;
    facts.tabs = 1;
    facts.panes = 200;      // 会话总数超过记录上限：计数不截断
    facts.session_count = 200;  // 发布侧须把条数夹回容量
    for (std::size_t i = 0; i < kMaxCrashSessions; ++i) {
        facts.sessions[i] = CrashSessionFact{
            .tab = i + 1U, .pane = i + 1U, .kind = ConnectionType::Serial, .alive = true};
    }
    store.publish(facts);

    const CrashFactsSnapshot latest = store.latest();
    AURORA_TEST_CHECK_EQ(latest.session_count, kMaxCrashSessions);
    AURORA_TEST_CHECK_EQ(latest.panes, 200U);
    AURORA_TEST_CHECK_EQ(latest.sessions[0].tab, 1U);
    AURORA_TEST_CHECK_EQ(latest.sessions[kMaxCrashSessions - 1].tab,
                          static_cast<std::uint64_t>(kMaxCrashSessions));

    // 第二次发布整体替换：双缓冲翻到另一份，不是只改几个字段。
    CrashFactsSnapshot second;
    second.windows = 9;
    store.publish(second);
    AURORA_TEST_CHECK_EQ(store.latest().windows, 9U);
    AURORA_TEST_CHECK_EQ(store.latest().session_count, 0U);
    AURORA_TEST_CHECK_EQ(store.latest().sessions[0].tab, 0U);
}

AURORA_TEST_CASE(kind_names_and_liveness_are_spelled_out) {
    CrashFactsSnapshot facts;
    facts.session_count = 4;
    facts.sessions[0] =
        CrashSessionFact{.tab = 7, .pane = 1, .kind = ConnectionType::Local, .alive = true};
    facts.sessions[1] =
        CrashSessionFact{.tab = 7, .pane = 2, .kind = ConnectionType::Ssh, .alive = false};
    facts.sessions[2] =
        CrashSessionFact{.tab = 8, .pane = 1, .kind = ConnectionType::Serial, .alive = true};
    facts.sessions[3] =
        CrashSessionFact{.tab = 9, .pane = 1, .kind = ConnectionType::Telnet, .alive = false};

    Collected out;
    borealis::diag::emit_crash_report(&collect, &out, facts, "SIGBUS", 7, 1);

    AURORA_TEST_CHECK_NE(out.text.find("session tab=7 pane=1 kind=local alive=1\n"),
                         std::string::npos);
    AURORA_TEST_CHECK_NE(out.text.find("session tab=7 pane=2 kind=ssh alive=0\n"),
                         std::string::npos);
    AURORA_TEST_CHECK_NE(out.text.find("session tab=8 pane=1 kind=serial alive=1\n"),
                         std::string::npos);
    AURORA_TEST_CHECK_NE(out.text.find("session tab=9 pane=1 kind=telnet alive=0\n"),
                         std::string::npos);
}

AURORA_TEST_CASE(report_is_ascii_key_value_lines_only) {
    CrashFactsSnapshot facts;
    facts.windows = 1;
    facts.tabs = 1;
    facts.panes = 1;
    facts.session_count = 1;
    facts.sessions[0] =
        CrashSessionFact{.tab = 5, .pane = 2, .kind = ConnectionType::Telnet, .alive = true};

    Collected out;
    borealis::diag::emit_crash_report(&collect, &out, facts, "SIGSEGV", 11, 1700000000);

    static constexpr const char *kAllowed[] = {
        "version=",       "trigger=",        "trigger_code=",  "crashed_at_epoch=",
        "last_publish_epoch=", "windows=",   "tabs=",          "panes=",
        "sessions=",      "session tab=",
    };
    std::size_t offset = 0;
    bool first = true;
    while (offset < out.text.size()) {
        const std::size_t newline = out.text.find('\n', offset);
        AURORA_TEST_REQUIRE_NE(newline, std::string::npos);
        const std::string_view line{out.text.data() + offset, newline - offset};
        if (first) {
            AURORA_TEST_CHECK_EQ(line, std::string_view{"borealis crash report v1"});
            first = false;
        } else {
            bool matched = false;
            for (const char *prefix : kAllowed) {
                if (line.starts_with(prefix)) {
                    matched = true;
                    break;
                }
            }
            AURORA_TEST_CHECK(matched);
        }
        for (const char character : line) {
            const auto byte = static_cast<unsigned char>(character);
            AURORA_TEST_CHECK(byte >= 0x20U && byte <= 0x7EU);
        }
        offset = newline + 1U;
    }
}

AURORA_TEST_CASE(missing_trigger_name_renders_unknown) {
    const CrashFactsSnapshot facts;
    Collected out;
    borealis::diag::emit_crash_report(&collect, &out, facts, nullptr, 0, 0);
    AURORA_TEST_CHECK_NE(out.text.find("trigger=unknown\n"), std::string::npos);
}

}  // namespace borealis::test_cases::utest_crash_report
