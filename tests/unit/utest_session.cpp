/// 测试类型: unit
/// 目标单元: include/borealis/session/session.h
/// 测试说明: 会话层的读侧接线（字节 → 解码 → 状态机 → 网格 → 脏行提交）与写侧通道
///           （文本编码下发、**行尾折算**、尺寸下发、DSR/DA1 应答回写、OSC 52 读方向应答）、
///           **原始字节采集的两笔记痕**（RX 取解码前的原始批、TX 取折算后的那一份、两条线程共一环、
///           未武装即没有环）、OSC 消费产物
///           的会话侧取值（标题 / 工作目录 / 超链接 / 剪贴板待写合并 / 命令块快照）、帧唤醒句柄的
///           注入与唤醒时机（含「连接生死那一次唤醒独立于批量输入有没有产出提交」这一条
///           `SPEC.FEAT.WS.05` 的例外，与「块表变化也唤醒」，以及 `SPEC.FEAT.WS.03` 唤醒腿
///           判据「close ⇒ join 返回后永久静默、join 返回前的末路边沿仍可达」），以及对端退出时的解码收尾与非法字节替换
///           （SPEC.FEAT.TERM.01 查询响应、SPEC.FEAT.TERM.07 OSC 消费、
///           SPEC.FEAT.INTEG.01 命令块来源、
///           SPEC.FEAT.TERM.09 双向编码、SPEC.FEAT.CONN.05 发送行尾折算与十六进制视图采集、
///           SPEC.FEAT.XFER.01 尺寸同步、
///           SPEC.FEAT.WS.01 存活判定、架构 §3.2/§3.3/§3.4/§7.2）。

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "borealis/grid/storage.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/session/session.h"
#include "borealis/term/keymap.h"
#include "borealis/term/paste.h"
#include "borealis/term/terminal.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_session {

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::CloseReason;
using borealis::session::Damage;
using borealis::session::RawDirection;
using borealis::session::ReconnectControl;
using borealis::session::ReconnectProgress;
using borealis::session::ReconnectStop;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::Cursor;
using borealis::term::kReplacementCharacter;
using borealis::term::SingleWidthPolicy;
using borealis::term::TermModes;

auto width_policy = std::make_shared<borealis::term::SingleWidthPolicy>();

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

    auto close() -> void override {
        alive_ = false;
        // 一次性钩子先取后落再调：模拟真读线程在 close 内、join 返回前投出的末路边沿；析构
        // 那第二跳 `~Session` 也会 close 一次，自废保证不再回调半死的会话对象。
        if (on_close) {
            const std::function<void()> last{std::move(on_close)};
            on_close = nullptr;
            last();
        }
    }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    /// @brief 模拟读线程投递一段原始字节。
    auto deliver(std::string_view bytes) -> void { events_->on_bytes(to_bytes(bytes)); }

    /// @brief 模拟对端进程退出。
    auto deliver_closed() -> void { events_->on_closed(); }

    /// @brief 模拟带归因的退出（SSH 腿那条路；替身不覆写重载，走基类默认转调）。
    auto deliver_closed(CloseReason reason) -> void { events_->on_closed(reason); }

    /// @brief 模拟退避环投一份进度快照。
    auto deliver_progress(const ReconnectProgress &progress) -> void {
        events_->on_reconnect_progress(progress);
    }

    std::vector<std::byte> written;   ///< 会话下发的字节（含状态机应答）。
    std::vector<Size> resizes;        ///< 会话下发的尺寸。
    std::function<void()> on_close{}; ///< close 内、置死之前的一次性钩子（末路边沿用例用）。

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

/// @brief 会自愈的连接替身：多实现一条能力接口，验证会话侧「有则交出、无则空」的分派。
class SelfHealingConnection final : public Connection, public ReconnectControl {
  public:
    auto start(ConnectionEvents &events) -> void override { events_ = &events; }
    auto write(std::span<const std::byte>) -> void override {}
    auto resize(Size) -> void override {}
    auto close() -> void override {}
    [[nodiscard]] auto alive() const noexcept -> bool override { return false; }

    auto retry_now() -> void override { ++retries; }
    auto stop_reconnect() -> void override { ++stops; }

    int retries = 0;   ///< 「立即重试」被按了几回。
    int stops = 0;     ///< 「停止重连」被按了几回。

  private:
    ConnectionEvents *events_ = nullptr;
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

/// @brief 此刻的 Unix epoch 毫秒（采集时刻的那条上下界证人，与会话侧 `now_ms()` 同一把钟）。
[[nodiscard]] auto epoch_ms_now() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
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

/// @brief 起一条指定编码档的会话（`SPEC.FEAT.TERM.09` 的生效现场：两个方向都过会话）。
[[nodiscard]] auto make_encoded_session(const borealis::term::SessionEncoding &encoding,
                                        Size size = Size{10, 3}) -> Fixture {
    auto owned = std::make_unique<FakeConnection>();
    auto *connection = owned.get();
    auto session = std::make_unique<Session>(std::move(owned), size, 5, width_policy,
                                             borealis::term::TerminalDefaults{}, encoding);
    session->start();
    return {connection, std::move(session)};
}

/// @brief 起一条给了「本连接行尾」的会话——串口形态（裁决 7.110 D11①，`SPEC.FEAT.CONN.05`）。
[[nodiscard]] auto make_line_ending_session(borealis::term::LineEnding ending) -> Fixture {
    auto owned = std::make_unique<FakeConnection>();
    auto *connection = owned.get();
    auto session = std::make_unique<Session>(
        std::move(owned), Size{10, 3}, 5, width_policy, borealis::term::TerminalDefaults{},
        borealis::term::SessionEncoding{}, std::optional<borealis::term::LineEnding>{ending});
    session->start();
    return {connection, std::move(session)};
}

/// @brief 把文本折成发送字节（`send_bytes` 那条原样通道的入口形态）。
[[nodiscard]] auto as_bytes(std::string_view text)
    -> std::span<const std::byte> {
    return std::as_bytes(std::span<const char>{text.data(), text.size()});
}

/// @brief 起一个挂「会自愈连接」的会话，用来验能力接口的分派。
[[nodiscard]] auto make_self_healing_session() -> std::pair<SelfHealingConnection *,
                                                            std::unique_ptr<Session>> {
    auto owned = std::make_unique<SelfHealingConnection>();
    auto *connection = owned.get();
    auto session = std::make_unique<Session>(std::move(owned), Size{10, 3}, 5, width_policy);
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

AURORA_TEST_CASE(lone_cr_folds_to_the_session_line_ending) {
    // 裁决 7.110 D11①：折算只在会话写出侧这一处，LF 档把**单独一个 CR** 折成 LF，LF 本身不动。
    auto fixture = make_line_ending_session(borealis::term::LineEnding::Lf);
    fixture.session->send_text(U"a\rb\nc");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("a\nb\nc"));

    // 键入那条通道同样过这一处：`send_bytes` 里 Enter 产的正是单个 CR（F8：设备规程不替本仓改写）。
    fixture.connection->written.clear();
    fixture.session->send_bytes(as_bytes("\r"));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\n"));

    // 转义序列里没有 CR，故 CSI 一份字节不改（方向键与鼠标上报都在这条路上）。
    fixture.connection->written.clear();
    fixture.session->send_bytes(as_bytes("\x1B[A"));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B[A"));
}

AURORA_TEST_CASE(crlf_target_leaves_an_existing_pair_alone) {
    auto fixture = make_line_ending_session(borealis::term::LineEnding::Crlf);
    // 已成对的 CRLF 原样留着：否则 Windows 剪贴板与 bracketed 粘贴里每行多一个行尾，而
    // `term::plan_paste` 明写那两种形态逐字节不动（D11① 的边界就在这条配对判据上）。
    fixture.session->send_text(U"a\r\nb\rc");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("a\r\nb\r\nc"));

    // 连续两个 CR（老设备的一空行形态）各自折开。
    fixture.connection->written.clear();
    fixture.session->send_bytes(as_bytes("\r\r"));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\r\n\r\n"));
}

AURORA_TEST_CASE(a_session_without_a_line_ending_writes_bytes_unchanged) {
    // 本地/SSH 两腿不给这一参 ⇒ 折算点对它们不存在（既有构造点与收到的字节都不动一字）。
    auto fixture = make_session();
    fixture.session->send_text(U"a\rb\nc");
    fixture.session->send_bytes(as_bytes("\r"));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("a\rb\nc\r"));

    // CR 档是「不折」的另一半：串口选 CR 时与不给这一参完全一致，键表产的就是 CR。
    auto cr = make_line_ending_session(borealis::term::LineEnding::Cr);
    cr.session->send_bytes(as_bytes("\r\n\r"));
    AURORA_TEST_CHECK_EQ(text_of(cr.connection->written), std::string("\r\n\r"));
}

AURORA_TEST_CASE(keymap_enter_still_produces_cr_while_the_fold_lands_crlf) {
    // D11① 要锁的两段事实：键表逐字节不动（仍产 CR），落地形态由会话的行尾档决定。
    const auto encoded = aurora::testing::require_value(
        borealis::term::encode_key(borealis::term::KeyPress{.sym = borealis::term::KeySym::Enter},
                                   TermModes{}));
    AURORA_TEST_CHECK_EQ(encoded, std::string("\r"));

    auto fixture = make_line_ending_session(borealis::term::LineEnding::Crlf);
    fixture.session->send_bytes(as_bytes(encoded));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\r\n"));
}

AURORA_TEST_CASE(an_unarmed_session_has_no_ring_and_records_nothing) {
    // 稿 D7①／§5 那句「未武装零成本」的唯一可观测形态：连环对象都不存在，记痕点只判一次空指针。
    auto armed = make_session();
    auto other = make_session();
    other.connection->deliver("abc");
    other.session->send_bytes(as_bytes("\r"));
    AURORA_TEST_CHECK_TRUE(other.session->raw_tap() == nullptr);  // 记痕不会自己长出环

    // 逐会话武装：给 A 开监视卡不动 B 的采集（面板挂在哪个标签上就采哪一个）。
    const auto tap = armed.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(tap != nullptr);
    AURORA_TEST_CHECK_TRUE(armed.session->raw_tap() != nullptr);
    AURORA_TEST_CHECK_TRUE(other.session->raw_tap() == nullptr);
}

AURORA_TEST_CASE(arming_a_second_time_hands_back_the_same_ring_and_its_history) {
    auto fixture = make_session();
    const auto first = fixture.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(first != nullptr);
    AURORA_TEST_CHECK_EQ(first.get(), fixture.session->raw_tap().get());

    fixture.connection->deliver("ab");
    AURORA_TEST_CHECK_EQ(first->run_count(), 1U);

    // 幂等的两条判据：同一份件、且不清空。面板关了又开看到的应当是历史，「清空」是面板那枚按钮的
    // 动作（稿 D7①），与会话重开＝新 `Session` 对象＝从零采集分得清。
    const auto second = fixture.session->enable_raw_tap();
    AURORA_TEST_CHECK_EQ(first.get(), second.get());
    AURORA_TEST_CHECK_EQ(second->run_count(), 1U);
}

AURORA_TEST_CASE(received_runs_carry_the_raw_bytes_and_one_moment) {
    auto fixture = make_session();
    const auto tap = fixture.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(tap != nullptr);
    const std::string payload = "\x1B[2J\xE4\xB8\xAD";  // 转义族 + 一枚三字节 UTF-8 字符一起投
    const auto before = epoch_ms_now();
    fixture.connection->deliver(payload);
    const auto after = epoch_ms_now();

    const auto runs = tap->snapshot();
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_TRUE(runs[0].direction == RawDirection::Received);
    // 采的是**解码前**那一份：转义族没被吞、CJK 仍是它在线上的三字节形态（稿 D2①，与日志吃码点相反）。
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), payload);
    AURORA_TEST_CHECK_TRUE(runs[0].timestamp_ms >= before && runs[0].timestamp_ms <= after);
    AURORA_TEST_CHECK_EQ(tap->total_bytes(), payload.size());
}

AURORA_TEST_CASE(the_query_answer_is_recorded_after_the_batch_that_asked_for_it) {
    // 环只承诺一条顺序：到达序（稿 D3①）。RX 记痕点一旦挪到应答回写之后——甚至挪进临界区之前那些
    // 位置，本例就成了「我方先答、设备后问」，一条排障用的收发视图自相矛盾。
    auto fixture = make_session();
    const auto tap = fixture.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(tap != nullptr);
    fixture.connection->deliver("\x1B[5n");  // DSR：状态机当场产应答，且在同一次 ingest 里回写

    const auto runs = tap->snapshot();
    AURORA_TEST_REQUIRE_EQ(runs.size(), 2U);
    AURORA_TEST_CHECK_TRUE(runs[0].direction == RawDirection::Received);
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), std::string("\x1B[5n"));
    AURORA_TEST_CHECK_TRUE(runs[1].direction == RawDirection::Transmitted);
    AURORA_TEST_CHECK_EQ(text_of(runs[1].bytes), std::string("\x1B[0n"));
}

AURORA_TEST_CASE(the_closing_round_records_no_empty_run) {
    auto fixture = make_session();
    const auto tap = fixture.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(tap != nullptr);
    fixture.connection->deliver("ab");
    fixture.connection->deliver_closed();  // 收尾一轮喂的是空批（挂起序列按替换字符收尾）
    AURORA_TEST_CHECK_EQ(tap->run_count(), 1U);
}

AURORA_TEST_CASE(the_lf_leg_records_the_folded_bytes_not_the_keytable_ones) {
    auto fixture = make_line_ending_session(borealis::term::LineEnding::Lf);
    const auto tap = fixture.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(tap != nullptr);
    fixture.session->send_text(U"a\rb\nc");

    const auto runs = tap->snapshot();
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_TRUE(runs[0].direction == RawDirection::Transmitted);
    // 视图要说的是设备真收到的字节：单个 CR 折成 LF，本来就是 LF 的那一枚不动（裁决 7.110 D11①）。
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), std::string("a\nb\nc"));
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), text_of(fixture.connection->written));
}

AURORA_TEST_CASE(the_crlf_leg_records_a_lone_cr_as_the_pair) {
    auto fixture = make_line_ending_session(borealis::term::LineEnding::Crlf);
    const auto tap = fixture.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(tap != nullptr);
    fixture.session->send_text(U"a\rb\nc");  // 已成对的那一枚不双折：它的 CR 属于一对 CRLF

    const auto runs = tap->snapshot();
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), std::string("a\r\nb\nc"));
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), text_of(fixture.connection->written));
}

AURORA_TEST_CASE(both_write_out_wings_record_their_own_folded_batch) {
    // CR 档走 `write_out` 的早退那一翼，「一批里没有可折的 CR」走折算那一翼的零分配尾巴。两翼合到
    // `emit_to_connection` 一处，采样点才不需要各写一遍——只记其中一翼时，另一翼那案即红。
    auto cr = make_line_ending_session(borealis::term::LineEnding::Cr);
    const auto cr_tap = cr.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(cr_tap != nullptr);
    cr.session->send_bytes(as_bytes("\r\n\r"));
    auto runs = cr_tap->snapshot();
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), std::string("\r\n\r"));  // CR 档即原样
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), text_of(cr.connection->written));

    auto crlf = make_line_ending_session(borealis::term::LineEnding::Crlf);
    const auto crlf_tap = crlf.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(crlf_tap != nullptr);
    crlf.session->send_bytes(as_bytes("\x1B[A"));  // 方向键：CSI 里没有 CR，一份字节不改
    runs = crlf_tap->snapshot();
    AURORA_TEST_REQUIRE_EQ(runs.size(), 1U);
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), std::string("\x1B[A"));
    AURORA_TEST_CHECK_EQ(text_of(runs[0].bytes), text_of(crlf.connection->written));
}

AURORA_TEST_CASE(both_threads_record_into_one_ring) {
    AURORA_TEST_REQUIRE_THREADS();
    // 读线程在 `ingest` 出锁后记 RX，主线程在 `emit_to_connection` 里记 TX；环自有 mutex_，与
    // `Session::mutex_` 不相交（稿 §5 第一条）。两方向交错的条数与字节数都必须精确，否则面板上的
    // 「已丢弃」那句留痕与行表计数都会说谎。
    auto fixture = make_session();
    const auto tap = fixture.session->enable_raw_tap();
    AURORA_TEST_REQUIRE_TRUE(tap != nullptr);
    constexpr std::uint64_t kBatches = 200U;
    const auto incoming = std::string{"ab"};
    const auto reader = [&fixture, &incoming]() -> void {
        for (std::uint64_t index = 0; index < kBatches; ++index) {
            fixture.connection->deliver(incoming);
        }
    };
    auto thread = std::thread{reader};
    for (std::uint64_t index = 0; index < kBatches; ++index) {
        fixture.session->send_bytes(as_bytes("cd"));
    }
    thread.join();
    AURORA_TEST_CHECK_EQ(tap->run_count(), 2U * kBatches);
    AURORA_TEST_CHECK_EQ(tap->total_bytes(), 4U * kBatches);
    AURORA_TEST_CHECK_EQ(tap->dropped_bytes(), 0U);
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

AURORA_TEST_CASE(command_blocks_travel_through_the_session_snapshot) {
    // `SPEC.FEAT.INTEG.01` 的取数入口：与 `osc_state()` 同形的锁内副本，供视口每帧取一次。
    auto fixture = make_session();
    fixture.connection->deliver("\x1B]133;A\x07" "p" "\x1B]133;B\x07" "cmd\r\n" "\x1B]133;C\x07");
    fixture.connection->deliver("out\r\n" "\x1B]133;D;7\x07");

    const auto blocks = fixture.session->command_blocks();
    AURORA_TEST_REQUIRE_EQ(blocks.size(), 1U);
    AURORA_TEST_CHECK_EQ(blocks.front().prompt_row, std::int64_t{0});
    AURORA_TEST_CHECK_EQ(blocks.front().output_start_row, std::int64_t{1});
    AURORA_TEST_CHECK_EQ(blocks.front().output_end_row, std::int64_t{2});
    AURORA_TEST_CHECK(blocks.front().completed);
    AURORA_TEST_CHECK(blocks.front().failed());
    AURORA_TEST_CHECK_EQ(blocks.front().exit_code, std::int32_t{7});
}

AURORA_TEST_CASE(command_block_change_wakes_the_frame_without_grid_damage) {
    // `D` 标记通常不改任何格子；块表自身的变化也要排一帧，否则菜单可用性与失败高亮要等下一次
    // 输出才更新（裁决 7.123 D7①）。
    auto fixture = make_session();
    int wakes = 0;
    fixture.session->set_frame_wake([&wakes]() { ++wakes; });
    static_cast<void>(drain_all(*fixture.session));
    fixture.connection->deliver("\x1B]133;A\x07\x1B]133;C\x07\x1B]133;D;0\x07");

    AURORA_TEST_CHECK_FALSE(fixture.session->has_damage());
    AURORA_TEST_CHECK_EQ(wakes, 1);
    const auto blocks = fixture.session->command_blocks();
    AURORA_TEST_REQUIRE_EQ(blocks.size(), 1U);
    AURORA_TEST_CHECK(blocks.front().completed);
    AURORA_TEST_CHECK_FALSE(blocks.front().failed());
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

AURORA_TEST_CASE(allowed_osc_52_reads_are_queued_and_answered_on_the_same_channel) {
    // 闸门接上 `Allow` 之后，读查询不再当场回空响应：一个字节都不写，改为把请求留给主线程
    // （`SPEC.FEAT.CONN.12` 的判据 ②——远端等的是内容，不是一句「不给」）。
    auto fixture = make_session();
    const borealis::term::ClipboardGate gate{borealis::term::ClipboardReadPolicy::Allow};
    fixture.session->set_clipboard_gate(&gate);

    fixture.connection->deliver("\x1B]52;p;?\x07\x1B]52;c;?\x07");
    AURORA_TEST_CHECK(fixture.connection->written.empty());

    const auto pending = fixture.session->take_clipboard_reads();
    AURORA_TEST_REQUIRE_EQ(pending.size(), 2U);
    AURORA_TEST_CHECK(pending[0].pc == U"p");  // Pc 原样留下（D8① 的回显口径）
    AURORA_TEST_CHECK(pending[1].pc == U"c");
    AURORA_TEST_CHECK(fixture.session->take_clipboard_reads().empty());  // 取走即清空

    // 应答走的是与查询应答同一条出口：编码腿与行尾折算都不另开一份。
    fixture.session->answer_clipboard_read(U"\x1B]52;p;aGVsbG8=\x07");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B]52;p;aGVsbG8=\x07"));
}

AURORA_TEST_CASE(the_outbox_turns_one_read_request_into_exactly_one_answer) {
    // 一条一答（D4①）与三档降级：正文、空内容、`nullopt`、超限四条各有自己的回写，而三个降级档
    // 回的都是既有那条空响应——逐字节不变是判据 ③ 的底线，也是 `utest_terminal_osc` 那条旧证人。
    auto fixture = make_session();
    const borealis::term::ClipboardGate gate{borealis::term::ClipboardReadPolicy::Allow};
    fixture.session->set_clipboard_gate(&gate);
    fixture.connection->deliver("\x1B]52;c;?\x07");
    const auto pending = fixture.session->take_clipboard_reads();
    AURORA_TEST_REQUIRE_EQ(pending.size(), 1U);
    auto &target = *fixture.session;

    AURORA_TEST_CHECK(session::ClipboardOutbox::answer_read(target, pending[0], "hello"));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B]52;c;aGVsbG8=\x07"));

    fixture.connection->written.clear();
    AURORA_TEST_CHECK_FALSE(
        session::ClipboardOutbox::answer_read(target, pending[0], std::optional<std::string_view>{}));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B]52;c;\x07"));

    fixture.connection->written.clear();
    AURORA_TEST_CHECK_FALSE(
        session::ClipboardOutbox::answer_read(target, pending[0], std::string_view{""}));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B]52;c;\x07"));

    // 超限降级只留一条诊断（D7①）：几 MB 的剪贴板不该变成一条会话写请求。
    fixture.connection->written.clear();
    const std::string oversize(session::kMaxClipboardResponseBytes + 1U, 'a');
    AURORA_TEST_CHECK_FALSE(
        session::ClipboardOutbox::answer_read(target, pending[0], std::string_view{oversize}));
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\x1B]52;c;\x07"));
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

AURORA_TEST_CASE(close_silences_the_frame_wake_leg_afterwards) {
    // `SPEC.FEAT.WS.03` 唤醒腿随关静默：装配层残留档（重启的 `retired`、回滚归还那一条）都
    // 先 close 再离手，close 返回后这条腿必须永不再点亮帧。
    auto fixture = make_session();
    int wakes = 0;
    fixture.session->set_frame_wake([&wakes]() { ++wakes; });
    fixture.connection->deliver("ab");
    AURORA_TEST_CHECK_EQ(wakes, 1);  // 关之前腿是活的

    fixture.session->close();
    fixture.session->resize(Size{12, 4});        // 主线程侧唯一还能点火的通路（脏照排、帧不叫）
    fixture.connection->deliver_closed();        // 迟到的末路边沿：真产品 join 后不会发生，替身
                                                 // 直调以钉死「清腿」而非「连接已死」判的正是它
    AURORA_TEST_CHECK_EQ(wakes, 1);
    AURORA_TEST_CHECK_TRUE(fixture.session->has_damage());  // 清腿只停叫醒，不吞已排的脏
}

AURORA_TEST_CASE(the_last_wake_inside_close_still_gets_through) {
    // 清腿排在 join **之后**：连接关断过程中读线程投出的末路 `on_closed`（`SPEC.FEAT.WS.05`
    // dead-session 浮层要的那一眼）仍须叫得醒主线程。排在之前就会把这一记吞掉。
    auto fixture = make_session();
    int wakes = 0;
    fixture.session->set_frame_wake([&wakes]() { ++wakes; });
    fixture.connection->on_close = [connection = fixture.connection]() {
        connection->deliver_closed();  // join 返回前的最后一记边沿
    };

    fixture.session->close();
    AURORA_TEST_CHECK_EQ(wakes, 1);    // 末路唤醒赶上了清腿前的班车
    fixture.session->resize(Size{12, 4});
    AURORA_TEST_CHECK_EQ(wakes, 1);    // 此后同一条腿彻底沉默
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
    // 输入有没有产出提交」。`set_frame_wake` 那句「无提交、无块表变化即不唤醒」讲的是 `on_bytes`
    // 那一路；`on_closed` 是其
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

AURORA_TEST_CASE(reconnect_snapshot_is_absent_until_the_leg_posts_one) {
    // 本地腿从不投快照 ⇒ 浮层维持裁决 7.86 那一档形态；它也没有重连控制面。
    auto fixture = make_session();
    AURORA_TEST_CHECK_FALSE(fixture.session->reconnect_progress().has_value());
    AURORA_TEST_CHECK_TRUE(fixture.session->reconnect_control() == nullptr);
}

AURORA_TEST_CASE(reconnect_snapshot_keeps_only_the_latest_value) {
    auto fixture = make_session();
    fixture.connection->deliver_progress(
        ReconnectProgress{.attempt = 1, .total = 3, .delay_ms = 1000, .stop = ReconnectStop::None});
    fixture.connection->deliver_progress(ReconnectProgress{.attempt = 2,
                                                            .total = 3,
                                                            .delay_ms = 2000,
                                                            .stop = ReconnectStop::None});

    const auto latest = fixture.session->reconnect_progress();
    AURORA_TEST_REQUIRE(latest.has_value());
    // 不是事件流水：UI 每帧只读最近一份，前一份不必排帧也不会「补放」。
    AURORA_TEST_CHECK_EQ(latest->attempt, 2);
    AURORA_TEST_CHECK_EQ(latest->delay_ms, 2000);

    // 重拨成功那一份是全零：快照回到「不在环里」，下一次掉线才重新计数。
    fixture.connection->deliver_progress(ReconnectProgress{});
    const auto cleared = fixture.session->reconnect_progress();
    AURORA_TEST_REQUIRE(cleared.has_value());
    AURORA_TEST_CHECK_EQ(cleared->attempt, 0);
    AURORA_TEST_CHECK_TRUE(cleared->stop == ReconnectStop::None);
}

AURORA_TEST_CASE(a_progress_event_wakes_the_frame_though_it_carries_no_damage) {
    // 与 on_closed 同条例外口径（裁决 7.99 判据 2）：退避进度没有网格提交，
    // 靠的正是「状态变更即唤醒」，否则「第 i 回 / X 秒后」要等下一次别的唤醒才更新。
    auto fixture = make_session();
    int wakes = 0;
    fixture.session->set_frame_wake([&wakes]() { ++wakes; });
    static_cast<void>(drain_all(*fixture.session));
    AURORA_TEST_REQUIRE_FALSE(fixture.session->has_damage());

    fixture.connection->deliver_progress(
        ReconnectProgress{.attempt = 1, .total = 3, .delay_ms = 1000, .stop = ReconnectStop::None});
    AURORA_TEST_CHECK_EQ(wakes, 1);
    AURORA_TEST_CHECK_FALSE(fixture.session->has_damage());
}

AURORA_TEST_CASE(the_reason_bearing_close_finishes_the_pending_sequence) {
    // 带归因那条重载由基类默认实现转调无参版 ⇒ 收尾路径与无归因那条完全同一条
    //（替身零改动即成立，裁决 7.99 D3① 的 additive 纪律在这里落地）。
    auto fixture = make_session();
    fixture.connection->deliver("\xE4\xB8");  // 三字节序列被切断：挂起中，尚未产字符
    AURORA_TEST_CHECK_FALSE(fixture.session->has_damage());
    fixture.connection->deliver_closed(CloseReason::LinkLost);
    AURORA_TEST_CHECK_EQ(drain_all(*fixture.session).size(), 1U);
    AURORA_TEST_CHECK_EQ(code_point_at(fixture, 0, 0), kReplacementCharacter);
}

AURORA_TEST_CASE(a_self_healing_leg_hands_out_its_reconnect_control) {
    auto [connection, session] = make_self_healing_session();    auto *control = session->reconnect_control();
    AURORA_TEST_REQUIRE(control != nullptr);

    // 会话层只认能力接口：两枚动作原样落到腿上，装配层不需要知道它是哪一种连接。
    control->retry_now();
    control->stop_reconnect();
    AURORA_TEST_CHECK_EQ(connection->retries, 1);
    AURORA_TEST_CHECK_EQ(connection->stops, 1);
}

AURORA_TEST_CASE(session_decodes_incoming_bytes_with_its_encoding_leg) {
    // 会话侧生效现场：同一串字节在 GB18030 档解成汉字、在 UTF-8 档解成替换字符（档由构造期定）。
    auto gb = make_encoded_session(borealis::term::SessionEncoding{.name = "GB18030"});
    gb.connection->deliver("\xD6\xD0");
    static_cast<void>(drain_all(*gb.session));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(code_point_at(gb, 0, 0)), std::uint32_t{U'\x4E2D'});

    auto utf8 = make_session();
    utf8.connection->deliver("\xD6\xD0");
    static_cast<void>(drain_all(*utf8.session));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(code_point_at(utf8, 0, 0)),
                         std::uint32_t{static_cast<std::uint32_t>(kReplacementCharacter)});
}

AURORA_TEST_CASE(session_encodes_typed_text_for_target_encoding) {
    // 发送方向同样按会话档成形：GB18030 会话发出的是 D6 D0，不是 UTF-8 的三字节。
    auto fixture = make_encoded_session(borealis::term::SessionEncoding{.name = "GB18030"});
    fixture.session->send_text(U"\x4E2D");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\xD6\xD0", 2));
    AURORA_TEST_CHECK_EQ(fixture.session->unrepresentable_count(), std::size_t{0});
}

AURORA_TEST_CASE(unrepresentable_send_is_counted_and_policy_applied) {
    // Big5 没有 é：替换档退到 `?`、丢弃档一个字节不发，两档都留下计数（一次性提示的取数口）。
    borealis::term::SessionEncoding big5{.name = "Big5"};
    auto replace = make_encoded_session(big5);
    replace.session->send_text(U"A\x00E9" U"B");
    AURORA_TEST_CHECK_EQ(text_of(replace.connection->written), std::string("A?B"));
    AURORA_TEST_CHECK_EQ(replace.session->unrepresentable_count(), std::size_t{1});

    big5.unrepresentable = borealis::term::UnrepresentablePolicy::DropWithNotice;
    auto drop = make_encoded_session(big5);
    drop.session->send_text(U"A\x00E9" U"B");
    AURORA_TEST_CHECK_EQ(text_of(drop.connection->written), std::string("AB"));
    AURORA_TEST_CHECK_EQ(drop.session->unrepresentable_count(), std::size_t{1});
}

AURORA_TEST_CASE(unknown_encoding_session_survives_on_utf8_leg) {
    // 配置里的编码名拼错时会话不失明：回落 UTF-8 腿，收发两侧都按 UTF-8 走（`term/codec.h` 的回落口径）。
    auto fixture = make_encoded_session(borealis::term::SessionEncoding{.name = "NOT-A-CODEC"});
    fixture.connection->deliver("\xE4\xB8\xAD");
    static_cast<void>(drain_all(*fixture.session));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(code_point_at(fixture, 0, 0)), std::uint32_t{U'\x4E2D'});

    fixture.session->send_text(U"\x4E2D");
    AURORA_TEST_CHECK_EQ(text_of(fixture.connection->written), std::string("\xE4\xB8\xAD", 3));
}

AURORA_TEST_CASE(first_unrepresentable_batch_latches_the_notice) {
    // 裁决 7.104 的 D1①/D5①：需求那句「一次性提示」在会话侧的形状——上弦一次、取走即空。
    auto fixture = make_encoded_session(borealis::term::SessionEncoding{.name = "Big5"});
    AURORA_TEST_CHECK(!fixture.session->take_unrepresentable_notice().has_value());

    fixture.session->send_text(U"A\x00E9" U"B");
    const auto notice = fixture.session->take_unrepresentable_notice();
    AURORA_TEST_REQUIRE(notice.has_value());
    AURORA_TEST_CHECK_EQ(notice->count, std::size_t{1});
    AURORA_TEST_CHECK_EQ(notice->leg, "Big5");

    // 取走即空：第二次取没有内容，卡片因此不会在同一会话上重弹。
    AURORA_TEST_CHECK(!fixture.session->take_unrepresentable_notice().has_value());
}

AURORA_TEST_CASE(the_notice_never_latches_again_after_one_take) {
    // D1 的「每会话对象只上弦一次」：取走之后的批次只累计数、不再上弦。粘贴分块一次产出若干批，
    // 每批都上弦就退化成刷屏（判据文 §1 事实 9）。
    auto fixture = make_encoded_session(borealis::term::SessionEncoding{.name = "Big5"});
    fixture.session->send_text(U"A\x00E9" U"B");
    AURORA_TEST_REQUIRE(fixture.session->take_unrepresentable_notice().has_value());

    fixture.session->send_text(U"C\x00E9" U"D");
    AURORA_TEST_CHECK(!fixture.session->take_unrepresentable_notice().has_value());
    AURORA_TEST_CHECK_EQ(fixture.session->unrepresentable_count(), std::size_t{2});
}

AURORA_TEST_CASE(a_pending_notice_is_not_overwritten_by_later_batches) {
    // 视图还没取走时，第二批次不改写已在弦上的那份：卡片报的是「第一次触发的那一批」，
    // 与「本对象只提示一次」同一条口径；全部代价另有面板第四行读累计数。
    auto fixture = make_encoded_session(borealis::term::SessionEncoding{.name = "Big5"});
    fixture.session->send_text(U"A\x00E9" U"B");
    fixture.session->send_text(U"C\x00E9\x00E9" U"D");

    const auto notice = fixture.session->take_unrepresentable_notice();
    AURORA_TEST_REQUIRE(notice.has_value());
    AURORA_TEST_CHECK_EQ(notice->count, std::size_t{1});
    AURORA_TEST_CHECK_EQ(fixture.session->unrepresentable_count(), std::size_t{3});
}

AURORA_TEST_CASE(the_notice_carries_the_effective_leg_not_the_configured_name) {
    // 裁决 7.104 的 D6②：填 GB2312 的会话实际跑的是 UTF-8 腿，卡片若照抄配置名就改了个不相干的字段。
    auto fixture = make_encoded_session(borealis::term::SessionEncoding{.name = "GB2312"});
    // 未配对代理在**所有**腿上都失败（判据文 §1 事实 10），它是 UTF-8 腿上也能触发提示的输入形态。
    fixture.session->send_text(std::u32string{static_cast<char32_t>(0xD800)});

    const auto notice = fixture.session->take_unrepresentable_notice();
    AURORA_TEST_REQUIRE(notice.has_value());
    AURORA_TEST_CHECK_EQ(notice->leg, "UTF-8");
}

}  // namespace borealis::test_cases::utest_session
