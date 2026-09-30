/// 测试类型: unit
/// 目标单元: include/borealis/session/session.h
/// 测试说明: 会话层的读侧接线（字节 → 解码 → 状态机 → 网格 → 脏行提交）与写侧通道
///           （文本编码下发、尺寸下发、DSR/DA1 应答回写），以及对端退出时的解码收尾与
///           非法字节替换（SPEC.FEAT.TERM.01 查询响应、SPEC.FEAT.TERM.09 双向编码、
///            SPEC.FEAT.XFER.01 尺寸同步、SPEC.FEAT.WS.01 存活判定、架构 §3.3/§7.2）。

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

AURORA_TEST_CASE(alive_tracks_the_connection) {
    auto fixture = make_session();
    AURORA_TEST_CHECK_TRUE(fixture.session->alive());
    fixture.session->close();
    AURORA_TEST_CHECK_FALSE(fixture.session->alive());
}

}  // namespace borealis::test_cases::utest_session
