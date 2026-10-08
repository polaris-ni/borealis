/// 测试类型: unit
/// 目标单元: include/borealis/session/session.h
/// 测试说明: 会话层的读侧接线（字节 → 解码 → 状态机 → 网格 → 脏行提交）与写侧通道
///           （文本编码下发、尺寸下发、DSR/DA1 应答回写、OSC 52 读方向应答）、OSC 消费产物
///           的会话侧取值（标题 / 工作目录 / 超链接 / 剪贴板待写合并）、帧唤醒句柄的注入与
///           唤醒时机（含「连接生死那一次唤醒独立于批量输入有没有产出提交」这一条 `SPEC.FEAT.WS.05`
///           的例外），以及对端退出时的解码收尾与非法字节替换
///           （SPEC.FEAT.TERM.01 查询响应、SPEC.FEAT.TERM.07 OSC 消费、
///           SPEC.FEAT.TERM.09 双向编码、SPEC.FEAT.XFER.01 尺寸同步、SPEC.FEAT.WS.01 存活判定、
///           架构 §3.2/§3.3/§7.2）。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/grid/storage.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_session {

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Damage;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::Cursor;
using borealis::term::kReplacementCharacter;
using borealis::term::SingleWidthPolicy;
using borealis::term::TermModes;

SingleWidthPolicy width_policy;

/// @brief 传输连接测试替身：记录会话下发的字节与尺寸，并可主动投递读线程回调。
///
/// 平台层（ConPTY）尚未落地，会话层的接线只能用替身驱动（裁决 7.11：平台假设不出共享层）。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override {
        written.insert(written.end(), bytes.begin(), bytes.end());
    }

    auto resize(Size size) -> void override { resizes.push_back(size); }

    auto close() -> void override { alive_ = false; }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    /// @brief 模拟读线程投递一段原始字节。
    auto deliver(std::string_view bytes) -> void { events_->on_bytes(to_bytes(bytes)); }

    /// @brief 模拟对端进程退出。
    auto deliver_closed() -> void { events_->on_closed(); }

    std::vector<std::byte> written;   ///< 会话下发的字节（含状态机应答）。
    std::vector<Size> resizes;        ///< 会话下发的尺寸。

  private:
    [[nodiscard]] static auto to_bytes(std::string_view text) -> std::vector<std::byte> {
        std::vector<std::byte> out;
        out.reserve(text.size());
        for (const char c : text) {
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        return out;
    }

    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

/// @brief 会话与其替身连接的联合 fixture（会话独占连接所有权，fixture 留裸指针驱动回调）。
///
/// `Session` 持 mutex 不可搬移，故以 `unique_ptr` 承载，让 fixture 本身仍可返回值。
struct Fixture {
    FakeConnection *connection = nullptr;
    std::unique_ptr<Session> session;
};

/// @brief 会话下发的字节还原成文本，供应答断言。
[[nodiscard]] auto text_of(const std::vector<std::byte> &bytes) -> std::string {
    std::string out;
    for (const auto b : bytes) {
        out.push_back(static_cast<char>(static_cast<unsigned char>(b)));
    }
    return out;
}

/// @brief 起一个 scrollback 5 行、已 start 的会话。
/// @param size 视口尺寸。
[[nodiscard]] auto make_session(Size size = Size{10, 3}) -> Fixture {
    auto owned = std::make_unique<FakeConnection>();
    auto *connection = owned.get();
    auto session = std::make_unique<Session>(std::move(owned), size, 5, width_policy);
    session->start();
    return {connection, std::move(session)};
}

/// @brief 取走本帧全部提交（测试里不受单帧预算截断）。
[[nodiscard]] auto drain_all(Session &session) -> std::vector<Damage> {
    std::vector<Damage> all;
    while (session.has_damage()) {
        const auto frame = session.drain_damage();
        all.insert(all.end(), frame.begin(), frame.end());
    }
    return all;
}

/// @brief 在临界区里读视口某行的文本（行尾空格剥掉）。
[[nodiscard]] auto row_text(Fixture &fixture, std::size_t row) -> std::string {
    std::string out;
    fixture.session->read([&out, row](Storage &grid, Cursor, const TermModes &) {
        const auto &line = grid.visible_line(row);
        for (std::size_t column = 0; column < line.columns(); ++column) {
            out.push_back(static_cast<char>(line.cell(column).code_point));
        }
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
    });
    return out;
}

/// @brief 在临界区里读视口某格码点。
[[nodiscard]] auto code_point_at(Fixture &fixture, std::size_t row, std::size_t column) -> char32_t {
    char32_t cp = 0;
    fixture.session->read([&cp, row, column](Storage &grid, Cursor, const TermModes &) {
        cp = grid.visible_line(row).cell(column).code_point;
    });
    return cp;
}

/// @brief 在临界区里读视口行数。
[[nodiscard]] auto visible_rows(Fixture &fixture) -> std::size_t {
    std::size_t rows = 0;
    fixture.session->read([&rows](Storage &grid, Cursor, const TermModes &) { rows = grid.visible_rows(); });
    return rows;
}

}  // namespace

AURORA_TEST_CASE(incoming_bytes_land_in_grid_and_queue_row_damage) {
    auto fixture = make_session();
    fixture.connection->deliver("ab\r\ncd");
    const auto damage = drain_all(*fixture.session);
    AURORA_TEST_CHECK_EQ(damage.size(), 1U);  // 行 0 与行 1 相邻，合并成一次提交
    AURORA_TEST_CHECK(damage[0].first_row == 0U && damage[0].last_row == 1U);
    AURORA_TEST_CHECK_FALSE(damage[0].full_screen);
    AURORA_TEST_CHECK_EQ(row_text(fixture, 0), std::string("ab"));
    AURORA_TEST_CHECK_EQ(row_text(fixture, 1), std::string("cd"));
    AURORA_TEST_CHECK_FALSE(fixture.session->has_damage());
}

AURORA_TEST_CASE(whole_viewport_scroll_reports_full_screen_damage) {
    auto fixture = make_session();
    fixture.connection->deliver("l1\r\nl2\r\nl3");
    static_cast<void>(drain_all(*fixture.session));
    fixture.connection->deliver("\r\n");  // 底行换行 → 整屏上滚，行号与内容的对应关系变了
    const auto damage = drain_all(*fixture.session);
    AURORA_TEST_CHECK_EQ(damage.size(), 1U);
    AURORA_TEST_CHECK_TRUE(damage[0].full_screen);
    AURORA_TEST_CHECK_EQ(row_text(fixture, 0), std::string("l2"));
}

AURORA_TEST_CASE(device_status_query_answers_through_write_channel) {
    auto fixture = make_session();
    fixture.connection->deliver("\x1B[5n");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B[0n"));
}

AURORA_TEST_CASE(cursor_position_query_reports_one_based_viewport) {
    auto fixture = make_session();
    fixture.connection->deliver("\x1B[2;3H\x1B[6n");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B[2;3R"));
}

AURORA_TEST_CASE(da1_reports_level_and_da2_is_ignored) {
    auto fixture = make_session();
    fixture.connection->deliver("\x1B[c");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B[?62c"));
    fixture.connection->written.clear();
    fixture.connection->deliver("\x1B[>c");  // DA2 不在 TERM.01 覆盖内：宁可不答也不答一份错格式
    AURORA_TEST_CHECK_TRUE(fixture.connection->written.empty());
}

AURORA_TEST_CASE(send_text_encodes_to_session_encoding) {
    auto fixture = make_session();
    fixture.session->send_text(U"a\x4E2D");  // UTF-8 是本地/SSH 默认编码（裁决 7.16）
    const auto written = text_of(fixture.connection->written);
    AURORA_TEST_CHECK_EQ(written, std::string("a\xE4\xB8\xAD", 4));
}

AURORA_TEST_CASE(resize_forwards_size_and_rebuilds_viewport) {
    auto fixture = make_session();
    fixture.connection->deliver("abc");
    static_cast<void>(drain_all(*fixture.session));
    fixture.session->resize(Size{6, 2});
    AURORA_TEST_CHECK_EQ(fixture.connection->resizes.size(), 1U);
    AURORA_TEST_CHECK(fixture.connection->resizes[0].columns == 6U && fixture.connection->resizes[0].rows == 2U);
    const auto damage = drain_all(*fixture.session);
    AURORA_TEST_CHECK_EQ(damage.size(), 1U);
    AURORA_TEST_CHECK_TRUE(damage[0].full_screen);
    AURORA_TEST_CHECK_EQ(visible_rows(fixture), 2U);
}

AURORA_TEST_CASE(invalid_size_is_not_forwarded) {
    auto fixture = make_session();
    fixture.session->resize(Size{0, 0});
    AURORA_TEST_CHECK_TRUE(fixture.connection->resizes.empty());
    AURORA_TEST_CHECK_FALSE(fixture.session->has_damage());
}

AURORA_TEST_CASE(process_exit_finishes_partial_sequence) {
    auto fixture = make_session();
    fixture.connection->deliver("\xE4\xB8");  // 三字节序列被切断：挂起中，尚未产字符
    AURORA_TEST_CHECK_FALSE(fixture.session->has_damage());
    fixture.connection->deliver_closed();
    AURORA_TEST_CHECK_EQ(drain_all(*fixture.session).size(), 1U);
    AURORA_TEST_CHECK_EQ(code_point_at(fixture, 0, 0), kReplacementCharacter);
}

AURORA_TEST_CASE(illegal_bytes_are_replaced_without_stalling_output) {
    auto fixture = make_session();
    fixture.connection->deliver("\xFF" "ok");  // 非法起始字节：替换后本行继续，不污染后续
    AURORA_TEST_CHECK_EQ(code_point_at(fixture, 0, 0), kReplacementCharacter);
    AURORA_TEST_CHECK_EQ(code_point_at(fixture, 0, 1), U'o');
    AURORA_TEST_CHECK_EQ(code_point_at(fixture, 0, 2), U'k');
    AURORA_TEST_CHECK_EQ(fixture.session->decode_stats().replaced, 1U);
}

AURORA_TEST_CASE(parse_stats_travel_the_chain_for_the_debug_panel) {
    // `SPEC.NF.RELI.01`：调试面板经 `Session::parse_stats()` 取解析期降级计数，
    // 断的是「读线程喂入 → 状态机 → 主线程取值」这条透传，而非计数算式本身（那在 utest_vt_parser）。
    auto fixture = make_session();
    fixture.connection->deliver("\x1B[1?m\x1B[2J\x1B[12\x18");
    static_cast<void>(drain_all(*fixture.session));
    const auto stats = fixture.session->parse_stats();
    AURORA_TEST_CHECK_EQ(stats.ignored, std::uint64_t{2});
    AURORA_TEST_CHECK_EQ(stats.cancelled, std::uint64_t{1});
}

AURORA_TEST_CASE(alive_tracks_the_connection) {
    auto fixture = make_session();
    AURORA_TEST_CHECK_TRUE(fixture.session->alive());
    fixture.session->close();
    AURORA_TEST_CHECK_FALSE(fixture.session->alive());
}

AURORA_TEST_CASE(osc_title_and_directory_readable_through_session) {
    auto fixture = make_session();
    fixture.connection->deliver("\x1B]0;window title\x07\x1B]7;file://hostA/srv\x07");
    const auto state = fixture.session->osc_state();
    AURORA_TEST_CHECK(state.title == U"window title");
    AURORA_TEST_CHECK(state.working_directory == U"file://hostA/srv");
}

AURORA_TEST_CASE(osc_hyperlink_travels_the_whole_chain_to_the_grid) {
    // 字节 → 解码 → 解析 → 状态机 → 网格侧表 → 会话取值：整条链都要能解析出目标。
    auto fixture = make_session();
    fixture.connection->deliver("\x1B]8;;https://example.test/a\x07link");
    grid::HyperlinkId link_id = 0;
    fixture.session->read([&link_id](Storage &grid, Cursor, const TermModes &) {
        link_id = grid.visible_line(0).hyperlink(0);
    });
    AURORA_TEST_CHECK_EQ(link_id, grid::HyperlinkId{1});
    const auto target = fixture.session->hyperlink_target(link_id);
    AURORA_TEST_CHECK(target.has_value());
    AURORA_TEST_CHECK(*target == U"https://example.test/a");
}

AURORA_TEST_CASE(osc_52_writes_in_one_batch_merge_before_the_main_thread_drains) {
    auto fixture = make_session();
    fixture.connection->deliver("\x1B]52;c;aGVsbG8=\x07\x1B]52;c;d29ybGQ=\x07");
    AURORA_TEST_CHECK_EQ(fixture.session->osc_state().clipboard_write_requests, 2U);

    const auto pending = fixture.session->take_clipboard_write();
    AURORA_TEST_CHECK(pending.has_value());
    AURORA_TEST_CHECK(*pending == U"world");  // 只把最终值交给主线程，旧值不再落地
    AURORA_TEST_CHECK_FALSE(fixture.session->take_clipboard_write().has_value());
}

AURORA_TEST_CASE(osc_52_read_request_answers_through_write_channel) {
    auto fixture = make_session();
    fixture.connection->deliver("\x1B]52;c;?\x07");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B]52;c;\x07"));
    AURORA_TEST_CHECK_FALSE(fixture.session->take_clipboard_write().has_value());
}

AURORA_TEST_CASE(frame_wake_fires_once_per_batch_whatever_the_commit_count) {
    auto fixture = make_session();
    int wakes = 0;
    fixture.session->set_frame_wake([&wakes]() { ++wakes; });

    // 行 0 与行 2 同时脏且不相邻 → 两条提交，但一轮批量输入只该唤醒一次。
    fixture.connection->deliver("ab\x1B[3;1Hcd");
    AURORA_TEST_REQUIRE_EQ(drain_all(*fixture.session).size(), 2U);
    AURORA_TEST_CHECK_EQ(wakes, 1);
}

AURORA_TEST_CASE(frame_wake_sees_a_queue_already_filled) {
    auto fixture = make_session();
    bool commits_visible = false;
    fixture.session->set_frame_wake([&fixture, &commits_visible]() {
        commits_visible = fixture.session->has_damage();  // 唤醒早于入队的那帧会白排一空帧
    });

    fixture.connection->deliver("ab");
    AURORA_TEST_CHECK_TRUE(commits_visible);
}

AURORA_TEST_CASE(frame_wake_stays_quiet_when_the_batch_touches_no_cell) {
    auto fixture = make_session();
    int wakes = 0;
    fixture.session->set_frame_wake([&wakes]() { ++wakes; });

    fixture.connection->deliver("");      // 空输入
    fixture.connection->deliver("\x1B[?25l");  // 只改模式，一格没写 → 无需排帧
    AURORA_TEST_CHECK_EQ(wakes, 0);
    AURORA_TEST_CHECK_FALSE(fixture.session->has_damage());
}

AURORA_TEST_CASE(resize_wakes_once_and_invalid_size_does_not) {
    auto fixture = make_session();
    int wakes = 0;
    fixture.session->set_frame_wake([&wakes]() { ++wakes; });

    fixture.session->resize(Size{12, 4});
    AURORA_TEST_CHECK_EQ(wakes, 1);
    AURORA_TEST_CHECK_TRUE(drain_all(*fixture.session)[0].full_screen);

    fixture.session->resize(Size{0, 0});
    AURORA_TEST_CHECK_EQ(wakes, 1);  // 无效尺寸整条路径都不走，含唤醒
}

AURORA_TEST_CASE(replacing_the_wake_handle_takes_effect_immediately) {
    auto fixture = make_session();
    int first = 0;
    int second = 0;
    fixture.session->set_frame_wake([&first]() { ++first; });
    fixture.session->set_frame_wake([&second]() { ++second; });

    fixture.connection->deliver("ab");
    AURORA_TEST_CHECK_EQ(first, 0);
    AURORA_TEST_CHECK_EQ(second, 1);  // 注入式接缝：装配换句柄不需要重启会话
}

AURORA_TEST_CASE(bell_flag_survives_the_drain_and_is_taken_once) {
    // `SPEC.FEAT.WS.04` 的会话侧：BEL 标记与脏行队列**分账**——装配层每帧先排脏行、后取标记，
    // 若排帧把标记一起消费掉，角标就永远亮不起来（取走语义的载体只有那一个 bool）。
    auto fixture = make_session();
    fixture.connection->deliver("\x07");
    static_cast<void>(drain_all(*fixture.session));
    AURORA_TEST_CHECK_FALSE(fixture.session->has_damage());

    AURORA_TEST_CHECK_TRUE(fixture.session->take_bell_triggered());
    AURORA_TEST_CHECK_FALSE(fixture.session->take_bell_triggered());

    fixture.connection->deliver("abc");
    static_cast<void>(drain_all(*fixture.session));
    AURORA_TEST_CHECK_FALSE(fixture.session->take_bell_triggered());  // 普通输出不点亮
}

AURORA_TEST_CASE(clean_close_wakes_the_frame_even_with_no_pending_damage) {
    // `SPEC.FEAT.WS.05` 的 dead-session 浮层判据挂在「连接生死」这一边沿上，而不是「最后一次批量
    // 输入有没有产出提交」。头注那句「无提交即不唤醒」讲的是 `on_bytes` 那一路；`on_closed` 是其
    // 例外——若把它塞进 `ingest({}, true)` 的返回值判定里，进程干净退出（残留半截序列在上一次 flush
    // 之后已被吞掉）就成了「无 damage ⇒ 无 wake」的静默档，浮层要等下一次别的唤醒才显形。
    auto fixture = make_session();
    int wakes = 0;
    fixture.session->set_frame_wake([&wakes]() { ++wakes; });
    // 先排掉 `start()` 那一路可能留下的任何提交，让「干净退出」现场真的干净。
    static_cast<void>(drain_all(*fixture.session));
    AURORA_TEST_REQUIRE_FALSE(fixture.session->has_damage());

    fixture.connection->deliver_closed();
    AURORA_TEST_CHECK_EQ(wakes, 1);
}

}  // namespace borealis::test_cases::utest_session
