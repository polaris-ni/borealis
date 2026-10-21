/// 测试类型: integration
/// 目标单元: src/session/session.cpp 的 `take_notification` 转发腿 + src/session/notification_outbox.cpp
///           + src/main.cpp 那条排帧调用的形态（不在本文件可证，见稿 §8 末条）
/// 测试说明: 以替身连接驱动「远端字节 → 状态机留存 → 会话取走 → 落地件升格与组 tag → 框架通知面」
///           这条链（`SPEC.FEAT.INTEG.03`，稿 §2 判据 ①②③④⑤⑥ 的链路半区与 §5 的三条不变量）。
///           切分、截断与计数在 `utest_term_notification` / `utest_terminal_osc` 逐档证过，本文件只证
///           链路上才会出错的四件事：① 一次排帧恰好投一条，且 `Notification` 的字段各归其位
///           （tag 由 `(标签号, 格号)` 组成、紧急度与驻留取平台默认那档）；② 正文以 UTF-8 字节到达
///           通知面而不是码点摊平（变异 m6 的证人）；③ 同标签的两格各拿自己的 tag，跨会话不互相
///           覆盖；④ 会话带着待发通知析构即了结，不补投（稿 D8①）。
///           全程装「仅记录」后端（F6）：不触达任何系统通知服务，断言只走 `last_notification()`。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/app/notification.h"
#include "borealis/session/connection.h"
#include "borealis/session/notification_outbox.h"
#include "borealis/session/session.h"
#include "borealis/term/notification.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::itest_notifications {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using aurora::Notification;
using aurora::NotificationCenter;
using aurora::NotificationUrgency;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::NotificationOutbox;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::kMaxNotificationBodyCodePoints;
using borealis::term::SingleWidthPolicy;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 40;
constexpr std::uint64_t kTab = 7U;
constexpr std::uint64_t kPane = 2U;

auto width_policy = std::make_shared<SingleWidthPolicy>();

/// @brief 传输连接替身：本条链只用它的投递方向，写方向无需记账。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override {
        sent_.insert(sent_.end(), bytes.begin(), bytes.end());
    }

    auto resize(Size) -> void override {}

    auto close() -> void override { alive_ = false; }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    /// @brief 把一段「远端输出」喂给会话：读线程回调在这里由本线程代走（形态与真实腿一致）。
    auto deliver(std::string_view bytes) -> void {
        std::vector<std::byte> raw;
        raw.reserve(bytes.size());
        for (const char c : bytes) {
            raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        events_->on_bytes(raw);
    }

    std::vector<std::byte> sent_;  ///< 本条链不产出会话侧应答，留着只为看清「一条都没写」。

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

/// @brief 通知面替身：装「仅记录」后端并在离开时卸载、清账。
///
/// 后端是进程级开关（F6），故必须成对：装了不卸，同一 runner 进程里后面的套件也会失去平台实现。
class RecordingChannel {
  public:
    RecordingChannel() {
        AURORA_TEST_REQUIRE(NotificationCenter::install_recording_backend());
        NotificationCenter::clear_last_notification();
    }

    RecordingChannel(const RecordingChannel &) = delete;
    auto operator=(const RecordingChannel &) -> RecordingChannel & = delete;
    RecordingChannel(RecordingChannel &&) = delete;
    auto operator=(RecordingChannel &&) -> RecordingChannel & = delete;

    ~RecordingChannel() {
        (void)NotificationCenter::remove_recording_backend();
        NotificationCenter::clear_last_notification();
    }

    /// @brief 读最近一条「试图投递」的通知并清账（下一案的起点是「从未通知过」）。
    [[nodiscard]] auto take_record() -> std::optional<Notification> {
        auto record = NotificationCenter::last_notification();
        NotificationCenter::clear_last_notification();
        return record;
    }

    [[nodiscard]] auto records() -> std::size_t {
        return NotificationCenter::last_notification().has_value() ? 1U : 0U;
    }
};

/// @brief 一台会话：替身连接 + 真实状态机（与 `itest_clipboard_read` 同一份构造形态）。
class Harness {
  public:
    Harness() {
        auto owned = std::make_unique<FakeConnection>();
        connection_ = owned.get();
        session_ = std::make_unique<Session>(std::move(owned), kNominalSize, kScrollback,
                                            width_policy);
        // 接线一次：`start()` 之前连接替身手里没有事件面，投递会踩空指针。
        session_->start();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投一串 OSC 并排一次帧（落地件的排帧点就是装配层那一格循环）。
    auto post(std::string_view osc_body) -> std::size_t {
        connection_->deliver(std::string{"\x1B]"} + std::string{osc_body} + "\x07");
        return NotificationOutbox::drain(*session_, kTab, kPane);
    }

    /// @brief 只投字节不排帧：通知已在会话里待发，而这一帧还没轮到它。
    auto feed(std::string_view osc_body) -> void {
        connection_->deliver(std::string{"\x1B]"} + std::string{osc_body} + "\x07");
    }

    auto drain() -> std::size_t { return NotificationOutbox::drain(*session_, kTab, kPane); }

    [[nodiscard]] auto session() -> Session & { return *session_; }

    /// @brief 会话往连接写了多少字节（本条链应当一个字节都不写）。
    [[nodiscard]] auto written_bytes() const -> std::size_t { return connection_->sent_.size(); }

    /// @brief 撤掉这一格会话（稿 D8 那一档：随会话析构撤账，不补投）。
    auto retire_session() -> void { session_.reset(); }

  private:
    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
};

}  // namespace

AURORA_TEST_CASE(the_first_frame_after_an_osc_9_posts_one_notification) {
    RecordingChannel channel;
    Harness harness;

    AURORA_TEST_CHECK_EQ(harness.post("9;build finished"), std::size_t{1});

    const auto record = channel.take_record();
    AURORA_TEST_REQUIRE(record.has_value());
    AURORA_TEST_CHECK(record->body == std::string{"build finished"});
    // 会话标题没设过（`OscState::title` 空即未设，`Session` 不 seed）⇒ 三级兜底链落到 ASCII
    // 那一档（稿 D2①）；第二级「取会话标题」由下一案单独证。
    AURORA_TEST_CHECK(record->title == std::string{"Borealis"});
    AURORA_TEST_CHECK(record->tag == std::string{"borealis-session-7.2"});
    AURORA_TEST_CHECK(record->urgency == NotificationUrgency::Normal);
    AURORA_TEST_CHECK_EQ(record->timeout_ms, std::uint32_t{0});
}

AURORA_TEST_CASE(the_session_title_reaches_the_notification_title) {
    RecordingChannel channel;
    Harness harness;

    harness.feed("0;ubuntu: ~/work");
    AURORA_TEST_CHECK_EQ(harness.post("9;tests passed"), std::size_t{1});

    const auto record = channel.take_record();
    AURORA_TEST_REQUIRE(record.has_value());
    AURORA_TEST_CHECK(record->title == std::string{"ubuntu: ~/work"});
    AURORA_TEST_CHECK(record->body == std::string{"tests passed"});
}

AURORA_TEST_CASE(a_777_notification_lands_its_title_and_body_separately) {
    RecordingChannel channel;
    Harness harness;

    AURORA_TEST_CHECK_EQ(harness.post("777;notify;Backup done;3 files; 0 errors"), std::size_t{1});

    const auto record = channel.take_record();
    AURORA_TEST_REQUIRE(record.has_value());
    AURORA_TEST_CHECK(record->title == std::string{"Backup done"});
    // 判据 ②：正文里的分号整段带出，落地件不再切第二遍。
    AURORA_TEST_CHECK(record->body == std::string{"3 files; 0 errors"});
}

AURORA_TEST_CASE(a_non_notify_777_subcommand_never_reaches_the_notification_channel) {
    RecordingChannel channel;
    Harness harness;

    AURORA_TEST_CHECK_EQ(harness.post("777;popup;do-not-show-me"), std::size_t{0});
    AURORA_TEST_CHECK_EQ(channel.records(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(harness.session().osc_state().unhandled_count, std::size_t{1});
}

AURORA_TEST_CASE(only_the_last_notification_of_a_frame_is_posted) {
    RecordingChannel channel;
    Harness harness;

    harness.feed("9;first");
    harness.feed("9;second");
    AURORA_TEST_CHECK_EQ(harness.drain(), std::size_t{1});

    const auto record = channel.take_record();
    AURORA_TEST_REQUIRE(record.has_value());
    AURORA_TEST_CHECK(record->body == std::string{"second"});
    // 稿 D3① 的代价在账上看得见：同一格这一帧窗口内的第一条被后到的覆盖了。
    AURORA_TEST_CHECK_EQ(harness.session().osc_state().notification_superseded, std::size_t{1});
    AURORA_TEST_CHECK_EQ(harness.session().osc_state().notification_requests, std::size_t{2});
}

AURORA_TEST_CASE(an_idle_session_does_not_touch_the_notification_channel) {
    RecordingChannel channel;
    Harness harness;

    // 判据 ⑤：无待发即 0，且一次 `notify` 都不调（空转的排帧不该摸 OS）。
    AURORA_TEST_CHECK_EQ(harness.drain(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(channel.records(), std::size_t{0});

    harness.feed("9;now there is one");
    AURORA_TEST_CHECK_EQ(harness.drain(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(harness.drain(), std::size_t{0});
    const auto record = channel.take_record();
    AURORA_TEST_REQUIRE(record.has_value());
    AURORA_TEST_CHECK(record->body == std::string{"now there is one"});
}

AURORA_TEST_CASE(an_empty_body_produces_no_toast_but_leaves_a_ledger) {
    RecordingChannel channel;
    Harness harness;

    AURORA_TEST_CHECK_EQ(harness.post("9;"), std::size_t{0});
    AURORA_TEST_CHECK_EQ(channel.records(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(harness.session().osc_state().notification_requests, std::size_t{1});
    AURORA_TEST_CHECK_EQ(harness.session().osc_state().unhandled_count, std::size_t{0});
}

AURORA_TEST_CASE(the_body_reaches_the_channel_as_utf8_bytes) {
    // 变异 m6 的证人：把码点串按字节摊进 `std::string` 时，U+1F600 会变成一个字节 0x1F600 的截断
    // 值；这里是四字节 `F0 9F 98 80`。字面量按 AGENTS.md §4.3 第 14 条写成十六进制转义。
    RecordingChannel channel;
    Harness harness;

    harness.feed("777;notify;Done;\xF0\x9F\x98\x80 done");
    AURORA_TEST_CHECK_EQ(harness.drain(), std::size_t{1});

    const auto record = channel.take_record();
    AURORA_TEST_REQUIRE(record.has_value());
    AURORA_TEST_CHECK(record->body == std::string{"\xF0\x9F\x98\x80 done"});
}

AURORA_TEST_CASE(two_sessions_on_the_same_tab_get_different_tags) {
    // §5 第 2 条的跨会话那一半：tag 恒定会漏掉「别的格也被覆盖」。
    RecordingChannel channel;
    Harness left;
    Harness right;

    AURORA_TEST_CHECK_EQ(NotificationOutbox::drain(left.session(), kTab, 1U), std::size_t{0});
    left.feed("9;left pane");
    right.feed("9;right pane");
    AURORA_TEST_CHECK_EQ(NotificationOutbox::drain(left.session(), kTab, 1U), std::size_t{1});

    const auto first = channel.take_record();
    AURORA_TEST_REQUIRE(first.has_value());
    AURORA_TEST_CHECK(first->tag == std::string{"borealis-session-7.1"});

    AURORA_TEST_CHECK_EQ(NotificationOutbox::drain(right.session(), kTab, 2U), std::size_t{1});
    const auto second = channel.take_record();
    AURORA_TEST_REQUIRE(second.has_value());
    AURORA_TEST_CHECK(second->tag == std::string{"borealis-session-7.2"});
    AURORA_TEST_CHECK(second->body == std::string{"right pane"});
}

AURORA_TEST_CASE(a_long_body_is_clipped_before_it_reaches_the_channel) {
    RecordingChannel channel;
    Harness harness;

    harness.feed("9;" + std::string(5000U, 'a'));
    AURORA_TEST_CHECK_EQ(harness.drain(), std::size_t{1});

    const auto record = channel.take_record();
    AURORA_TEST_REQUIRE(record.has_value());
    // 纯 ASCII 素材 ⇒ 码点数与字节数同值，故这里直接按长度判（截断本身的判据在 utest 那侧）。
    AURORA_TEST_CHECK_EQ(record->body.size(), kMaxNotificationBodyCodePoints);
    AURORA_TEST_CHECK(record->body.compare(record->body.size() - 3U, 3U, "...") == 0);
}

AURORA_TEST_CASE(a_pending_notification_dies_with_the_session) {
    // 稿 D8①：会话没了，那条提醒也不该补投——它指向的正是一格已经不存在的会话。
    RecordingChannel channel;
    Harness harness;

    harness.feed("9;never shown");
    harness.retire_session();
    AURORA_TEST_CHECK_EQ(channel.records(), std::size_t{0});
}

AURORA_TEST_CASE(the_notification_leg_writes_nothing_back_to_the_connection) {
    // 9 / 777 都是「单向告知」：本条链不产出任何应答字节（`OSC 52` 读方向才有回写）。
    RecordingChannel channel;
    Harness harness;

    harness.feed("9;a one-way notice");
    AURORA_TEST_CHECK_EQ(harness.drain(), std::size_t{1});
    (void)channel.take_record();

    harness.feed("777;notify;t;b");
    AURORA_TEST_CHECK_EQ(harness.drain(), std::size_t{1});
    (void)channel.take_record();

    AURORA_TEST_CHECK_EQ(harness.session().osc_state().notification_requests, std::size_t{2});
    AURORA_TEST_CHECK_EQ(harness.written_bytes(), std::size_t{0});
}

}  // namespace borealis::test_cases::itest_notifications

#else

// 非无头后端（Windows 主通道）下本文件不成立：通知面在真平台上会弹到桌面上（稿 §6 走查负责那条）。

#endif  // AURORA_BACKEND_HEADLESS
