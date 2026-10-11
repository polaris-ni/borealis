/// 测试类型: integration
/// 目标单元: include/borealis/session/session.h 的会话日志挂钩（`SPEC.FEAT.CONN.11`，裁决 7.116）
/// 测试说明: 接线层的落盘现场——替身连接喂真字节，断**真实文件逐字节**：入向码点与状态机同源
///           （D2①，同一片字节喂给网格和喂给日志，两处不可能分叉）、未开启的会话一寸不落
///           （需求那句「默认关闭」的会话侧半段）、A 开 B 不开的逐会话边界（判据 1）、
///           `close()`（标签关闭）把末段补成一行落盘、停笔之后不再落新内容、`AlreadyActive`
///           幂等、父目录缺失 ⇒ `CannotOpen` 且不建文件、首建走 0600 且同路径续写不截断、
///           写失败的一次性提示经 `take_logging_notice()` 交回主线程。
///           现场证人用真 PTY 抓下来的彩色 `ls` 回放夹具（tests/fixtures/vt/session_log_ls_color.txt，
///           AGENTS 第 20 条），断「一个 ESC 字节都不留、逐行成行、可打印率 ≈ 1、与纯函数的折算
///           逐字节相同」。
///
///           风险提示的 confirm 与能力闸的现读点在 `src/main.cpp`，那半段没有自动化证人
///           （裁决 7.83：装配不编入 runner）；本件锁的是它们的会话侧同形断言——
///           「没人呼 `enable_logging` 就什么都不发生」。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/session/session_log.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"
#include "support/fixture_text.h"
#include "support/paths.h"

namespace borealis::test_cases::itest_session_logging {

namespace {

using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::LogOutcome;
using borealis::session::LogSpec;
using borealis::session::Session;
using borealis::session::Size;

auto width_policy = std::make_shared<borealis::term::SingleWidthPolicy>();

/// @brief 传输连接替身：只负责把「读线程投字节」这件事交给调用方驱动。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte>) -> void override {}
    auto resize(Size) -> void override {}
    auto close() -> void override { alive_ = false; }
    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    /// @brief 模拟读线程投递一段原始字节（本件的每一次落盘都由它驱动）。
    auto deliver(std::string_view bytes) -> void {
        std::vector<std::byte> buffer;
        buffer.reserve(bytes.size());
        for (const char c : bytes) {
            buffer.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        events_->on_bytes(buffer);
    }

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

/// @brief 会话与其替身连接的联合 fixture（会话独占连接所有权，fixture 留裸指针驱动回调）。
struct Fixture {
    FakeConnection *connection = nullptr;
    std::unique_ptr<Session> session;
};

[[nodiscard]] auto make_fixture() -> Fixture {
    auto owned = std::make_unique<FakeConnection>();
    auto *connection = owned.get();
    auto session = std::make_unique<Session>(std::move(owned), Size{10, 3}, 5, width_policy);
    session->start();
    return {connection, std::move(session)};
}

/// @brief 本轮用例专属的日志路径（`isolation::temp_dir()` 每用例独享）。
[[nodiscard]] auto log_path(std::string_view name) -> std::string {
    return (std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name})
        .string();
}

[[nodiscard]] auto read_all(const std::string &path) -> std::string {
    std::ifstream in{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// @brief 按 `\n` 切行。
[[nodiscard]] auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t start = 0U;
    while (start < text.size()) {
        const auto at = text.find('\n', start);
        if (at == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, at - start));
        start = at + 1U;
    }
    return lines;
}

/// @brief ASCII 字节流折成码点流（夹具已是纯 ASCII，逐字节 1:1）。
[[nodiscard]] auto u32(std::string_view text) -> std::u32string {
    std::u32string out;
    for (const char c : text) {
        out.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 起一份日志入参：时间戳与打码都由别的用例锁，这里一律关。
[[nodiscard]] auto plain_spec(std::string_view name) -> LogSpec {
    LogSpec spec;
    spec.path = log_path(name);
    spec.timestamp = false;
    return spec;
}

}  // namespace

AURORA_TEST_CASE(bytes_from_the_connection_land_in_the_log_byte_for_byte) {
    auto fixture = make_fixture();
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("wired.log"))),
                         static_cast<int>(LogOutcome::Started));
    fixture.connection->deliver("\x1B[01;34mfirst\x1B[0m\r\nsecond \x1B[0mline\r\n");

    // 逐字节：与纯函数的折算同一份产物——同一条链路上「喂给网格的」与「喂给日志的」是同一份码点。
    AURORA_TEST_CHECK_EQ(read_all(log_path("wired.log")),
                         std::string("first\nsecond line\n"));
    AURORA_TEST_CHECK_TRUE(fixture.session->logging_enabled());
}

AURORA_TEST_CASE(a_session_that_never_enabled_logging_creates_nothing) {
    // 需求那句「默认关闭」的会话侧半段：没人呼 enable_logging ⇒ 路径上连文件都没有，
    // 更不是「建了个零字节文件放着」。装配层的能力闸与 confirm 那两腿在 main.cpp，无证人（7.83）。
    auto fixture = make_fixture();
    fixture.connection->deliver("nobody is watching\r\n");
    fixture.connection->deliver("\x1B[31mred\x1B[0m output\r\n");
    fixture.session->close();

    AURORA_TEST_CHECK_FALSE(std::filesystem::exists(std::filesystem::path{log_path("silent.log")}));
    AURORA_TEST_CHECK_FALSE(fixture.session->logging_enabled());
    AURORA_TEST_CHECK_FALSE(fixture.session->take_logging_notice().has_value());
}

AURORA_TEST_CASE(only_the_session_the_user_turned_on_writes_a_file) {
    // 判据 1 的「按会话开关」：A 开 B 不开 ⇒ 只有 A 有文件，B 的内容一寸不落。
    auto a = make_fixture();
    auto b = make_fixture();
    const auto path_b = log_path("b.log");
    LogSpec spec_b = plain_spec("b.log");

    AURORA_TEST_CHECK_EQ(static_cast<int>(a.session->enable_logging(plain_spec("a.log"))),
                         static_cast<int>(LogOutcome::Started));
    a.connection->deliver("alpha\r\n");
    b.connection->deliver("beta\r\n");

    AURORA_TEST_CHECK_EQ(read_all(log_path("a.log")), std::string("alpha\n"));
    AURORA_TEST_CHECK_FALSE(std::filesystem::exists(std::filesystem::path{path_b}));

    // B 随后开启：它自己的历史不回补（日志记的是开启之后到达的输出）。
    spec_b.timestamp = false;
    AURORA_TEST_CHECK_EQ(static_cast<int>(b.session->enable_logging(std::move(spec_b))),
                         static_cast<int>(LogOutcome::Started));
    b.connection->deliver("gamma\r\n");
    AURORA_TEST_CHECK_EQ(read_all(path_b), std::string("gamma\n"));
}

AURORA_TEST_CASE(disabling_stops_new_content_and_flushes_the_partial_tail) {
    auto fixture = make_fixture();
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("stop.log"))),
                         static_cast<int>(LogOutcome::Started));
    fixture.connection->deliver("kept\r\n");
    fixture.session->disable_logging();
    AURORA_TEST_CHECK_FALSE(fixture.session->logging_enabled());

    fixture.connection->deliver("dropped\r\n");
    AURORA_TEST_CHECK_EQ(read_all(log_path("stop.log")), std::string("kept\n"));

    // 停笔之后重开＝换一份新文件（一次开启一个文件，D13）。
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("again.log"))),
                         static_cast<int>(LogOutcome::Started));
    fixture.connection->deliver("fresh\r\n");
    AURORA_TEST_CHECK_EQ(read_all(log_path("again.log")), std::string("fresh\n"));
}

AURORA_TEST_CASE(closing_the_tab_flushes_the_last_unterminated_line) {
    // 标签关闭走 `Session::close()`：先 join 读线程再停笔，末段那半行必须补成一行落盘。
    auto fixture = make_fixture();
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("close.log"))),
                         static_cast<int>(LogOutcome::Started));
    fixture.connection->deliver("tail with no newline");
    fixture.session->close();
    AURORA_TEST_CHECK_EQ(read_all(log_path("close.log")), std::string("tail with no newline\n"));
    AURORA_TEST_CHECK_FALSE(fixture.session->logging_enabled());
}

AURORA_TEST_CASE(a_second_enable_is_idempotent_and_touches_no_new_file) {
    auto fixture = make_fixture();
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("twice.log"))),
                         static_cast<int>(LogOutcome::Started));
    fixture.connection->deliver("first\r\n");

    // 第二次开启既不入锁内的 AlreadyActive 分支也不碰文件系统：多余的点按不留新文件、不换路径，
    // 先前那份记录也不受扰动（若实现把「已在记录中」做成「重来一份」，这里会看到两条或换文件）。
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("other.log"))),
                         static_cast<int>(LogOutcome::AlreadyActive));
    AURORA_TEST_CHECK_FALSE(std::filesystem::exists(std::filesystem::path{log_path("other.log")}));
    fixture.connection->deliver("second\r\n");
    AURORA_TEST_CHECK_EQ(read_all(log_path("twice.log")), std::string("first\nsecond\n"));
    AURORA_TEST_CHECK_TRUE(fixture.session->logging_enabled());
}

AURORA_TEST_CASE(a_refused_open_leaves_the_session_unlogged_and_creates_no_file) {
    // 父目录不存在 ⇒ CannotOpen（`create_private_file` 不代建目录，稿 §0 的 F9）。断三条：
    // 回值、界面据以显示两态的开关态、以及「会话没被拖进记录态」——故 no new file + 无内容。
    auto fixture = make_fixture();
    const auto orphan =
        (std::filesystem::path{aurora::testing::isolation::temp_dir()} / "absent_dir" / "orphan.log")
            .string();
    LogSpec spec = plain_spec("orphan.log");
    spec.path = orphan;
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(std::move(spec))),
                         static_cast<int>(LogOutcome::CannotOpen));
    AURORA_TEST_CHECK_FALSE(fixture.session->logging_enabled());

    fixture.connection->deliver("nothing lands\r\n");
    AURORA_TEST_CHECK_FALSE(std::filesystem::exists(std::filesystem::path{orphan}));
    AURORA_TEST_CHECK_FALSE(std::filesystem::exists(
        std::filesystem::path{aurora::testing::isolation::temp_dir()} / "absent_dir"));
    AURORA_TEST_CHECK_FALSE(fixture.session->take_logging_notice().has_value());

    // 一次失败的开启不烧掉这个会话：随后正常开启仍然可用（界面据此可再次提示用户）。
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("retry.log"))),
                         static_cast<int>(LogOutcome::Started));
    fixture.connection->deliver("works now\r\n");
    AURORA_TEST_CHECK_EQ(read_all(log_path("retry.log")), std::string("works now\n"));
}

#if !defined(_WIN32)
AURORA_TEST_CASE(the_first_file_is_owner_only_and_a_reopen_appends_instead_of_truncating) {
    // 0600 与不覆盖（稿 §6）：POSIX 腿实测；Win 腿的 ACL 语义本机无 MSVC，标推断／未验证。
    const auto path = std::filesystem::path{log_path("private.log")};
    {
        auto fixture = make_fixture();
        AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("private.log"))),
                             static_cast<int>(LogOutcome::Started));
        fixture.connection->deliver("secret-free content\r\n");
        fixture.session->close();
    }
    const auto permissions = std::filesystem::status(path).permissions();
    AURORA_TEST_CHECK_MSG((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none,
                          "owner can read");
    AURORA_TEST_CHECK_MSG((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none,
                          "owner can write");
    const auto world_bits =
        permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all);
    AURORA_TEST_CHECK_MSG(world_bits == std::filesystem::perms::none, "no group/other bits at all");

    // 同路径第二次是续写：`create_private_file` 交回 AlreadyExists ⇒ 原字节必须在。
    auto second = make_fixture();
    AURORA_TEST_CHECK_EQ(static_cast<int>(second.session->enable_logging(plain_spec("private.log"))),
                         static_cast<int>(LogOutcome::Started));
    second.connection->deliver("appended\r\n");
    second.session->close();
    AURORA_TEST_CHECK_EQ(read_all(path.string()), std::string("secret-free content\nappended\n"));
}
#endif

AURORA_TEST_CASE(a_write_failure_disables_logging_and_the_notice_reaches_the_main_thread) {
    // 造法同 utest 那一则：把文件摘掉，越限那次「读后半」必然失败。
    // 这里多证一段接线：禁用发生在读线程那一侧，而提示由主线程在帧边界经 `take_logging_notice` 取走。
    auto fixture = make_fixture();
    LogSpec spec = plain_spec("broken.log");
    spec.max_bytes = 40U;
    const auto path = std::filesystem::path{spec.path};
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(std::move(spec))),
                         static_cast<int>(LogOutcome::Started));
    std::error_code ec;
    std::filesystem::remove(path, ec);

    for (int index = 0; index < 20; ++index) {
        fixture.connection->deliver(std::string("line-") + std::to_string(index) + "\r\n");
    }
    const auto notice = fixture.session->take_logging_notice();
    AURORA_TEST_REQUIRE_TRUE(notice.has_value());
    if (notice) {
        AURORA_TEST_CHECK_EQ(*notice, path.string());
    }
    AURORA_TEST_CHECK_FALSE(fixture.session->take_logging_notice().has_value());  // 一次性，不重弹
    AURORA_TEST_CHECK_FALSE(fixture.session->logging_enabled());                  // 自我禁用已反映到开关态

    fixture.connection->deliver("after disable\r\n");
    AURORA_TEST_CHECK_EQ(read_all(path.string()).find("after disable"), std::string::npos);
}

AURORA_TEST_CASE(a_real_pty_capture_replays_into_clean_text_with_line_structure) {
    // 现场证人（AGENTS 第 20 条：转义序列以 tests/fixtures/ 回放夹具驱动）：稿 §0 的 F10——
    // 真 PTY 上 `ls --color=auto /etc` 的原始捕获，7149 字节、309 条 ESC、CR 与 LF 成对。
    const std::string captured = borealis::testing::fixtures::read_first_line(
        aurora::testing::paths::under_repo("tests/fixtures/vt/session_log_ls_color.txt"));
    AURORA_TEST_REQUIRE_MSG(!captured.empty(),
                            "fixture unreadable: tests/fixtures/vt/session_log_ls_color.txt");
    AURORA_TEST_REQUIRE_TRUE(captured.find('\x1B') != std::string::npos);  // 夹具本身必须带转义

    auto fixture = make_fixture();
    AURORA_TEST_CHECK_EQ(static_cast<int>(fixture.session->enable_logging(plain_spec("replay.log"))),
                         static_cast<int>(LogOutcome::Started));
    constexpr std::size_t kChunk = 211U;  // 质数分片：让分片边界落在各种位置，包括半条序列里
    for (std::size_t offset = 0U; offset < captured.size(); offset += kChunk) {
        fixture.connection->deliver(captured.substr(offset, kChunk));
    }
    fixture.session->close();

    const std::string logged = read_all(log_path("replay.log"));
    // ⑴ 一个 ESC 都不留、SGR 的尾巴也不留。
    AURORA_TEST_CHECK_EQ(logged.find('\x1B'), std::string::npos);
    AURORA_TEST_CHECK_EQ(logged.find("[0m"), std::string::npos);
    AURORA_TEST_CHECK_EQ(logged.find("[01;34m"), std::string::npos);
    // ⑵ 与纯函数的折算逐字节相同（同一条链路的两个出口不许分叉）。
    AURORA_TEST_CHECK_EQ(logged, borealis::session::fold_to_plain_text(u32(captured)));
    // ⑶ 逐行成行、可打印率 ≈ 1（判据 5 的「保留换行结构」+ 稿 §0 的 F11）。
    const auto lines = split_lines(logged);
    AURORA_TEST_CHECK_GE(lines.size(), 40U);
    AURORA_TEST_CHECK_TRUE(lines.front().find("ImageMagick-7") != std::string::npos);
    std::size_t printable = 0U;
    for (const char c : logged) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20U || c == '\n' || c == '\t') {
            ++printable;
        }
    }
    AURORA_TEST_REQUIRE_FALSE(logged.empty());
    AURORA_TEST_CHECK_GE(static_cast<double>(printable) / static_cast<double>(logged.size()), 0.99);
}

}  // namespace borealis::test_cases::itest_session_logging
