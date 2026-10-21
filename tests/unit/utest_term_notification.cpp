/// 测试类型: unit
/// 目标单元: include/borealis/term/notification.h + src/term/notification.cpp
/// 测试说明: OSC 9 / 777 折成待发通知的纯切分层（SPEC.FEAT.INTEG.03 的语义半区）——`9` 的正文
///           是首个 `;` 之后的**整段**（分号在内不截，稿 D1①）、`777` 的首段必须逐字是 `notify`
///           （大小写不敏感，其余子命令归未识别档）、标题三级兜底链（自带 → 会话标题 → ASCII
///           兜底，稿 D2①）、空正文只计数不发（判据 ⑥）、正文与标题各按码点上限截断并补省略号
///           （稿 D10①，且截的是码点不是字节）。留存与取走归 `utest_terminal_osc`，投递归
///           `itest_notifications`，本件不触状态机。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "borealis/term/notification.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_term_notification {

namespace {

using borealis::term::draft_notification;
using borealis::term::fit_code_points;
using borealis::term::kMaxNotificationBodyCodePoints;
using borealis::term::kMaxNotificationTitleCodePoints;
using borealis::term::kNotificationFallbackTitle;
using borealis::term::NotificationDrop;
using borealis::term::NotificationKind;

/// @brief 码点重复 @p count 次（截断案要能精确算长度，手写串做不到）。
[[nodiscard]] auto repeated(char32_t code_point, std::size_t count) -> std::u32string {
    return std::u32string(count, code_point);
}

/// @brief 末三枚码点是否为 ASCII 省略号。
[[nodiscard]] auto ends_with_ellipsis(const std::u32string &text) -> bool {
    return text.size() >= 3U && text.compare(text.size() - 3U, 3U, U"...") == 0;
}

/// @brief 省略号占掉的额度（截断案里「保留段」的长度都取上限减它）。
inline constexpr std::size_t kEllipsisCodePoints = 3U;

/// @brief 兜底标题常量升格成码点串：用例不重写第二遍字面量，改常量即改用例。
[[nodiscard]] auto fallback_title() -> std::u32string {
    std::u32string out;
    for (const char c : std::string_view{kNotificationFallbackTitle}) {
        out.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

AURORA_TEST_CASE(osc_9_keeps_every_semicolon_in_the_body) {
    // 判据 ①：`9` 没有字段结构，首个 `;` 之后的整段就是正文。再按 `;` 切会把一句日常输出截断
    // （`build; 42 passed; 0 failed`），故 D1② 被否掉的那一档在这里逐字锁住。
    const auto draft = draft_notification(9, U"build; 42 passed; 0 failed", U"demo@host");
    AURORA_TEST_CHECK(draft.drop == NotificationDrop::None);
    AURORA_TEST_CHECK(draft.text.kind == NotificationKind::TerminalMessage);
    AURORA_TEST_CHECK(draft.text.body == std::u32string{U"build; 42 passed; 0 failed"});
    AURORA_TEST_CHECK(draft.text.title == std::u32string{U"demo@host"});
}

AURORA_TEST_CASE(osc_9_title_chain_is_session_title_then_the_ascii_fallback) {
    // 判据 ②：`9` 本身没有标题字段，故标题只能来自会话；会话标题也没设过时才落到 ASCII 兜底。
    const auto with_session = draft_notification(9, U"done", U"ubuntu: ~/work");
    AURORA_TEST_CHECK(with_session.drop == NotificationDrop::None);
    AURORA_TEST_CHECK(with_session.text.title == std::u32string{U"ubuntu: ~/work"});

    const auto without_session = draft_notification(9, U"done", std::u32string_view{});
    AURORA_TEST_CHECK(without_session.drop == NotificationDrop::None);
    AURORA_TEST_CHECK(without_session.text.title == fallback_title());
}

AURORA_TEST_CASE(osc_777_title_field_wins_over_the_session_title) {
    const auto draft = draft_notification(777, U"notify;Backup finished;3 files copied", U"demo");
    AURORA_TEST_CHECK(draft.drop == NotificationDrop::None);
    AURORA_TEST_CHECK(draft.text.kind == NotificationKind::NotifyExtension);
    AURORA_TEST_CHECK(draft.text.title == std::u32string{U"Backup finished"});
    AURORA_TEST_CHECK(draft.text.body == std::u32string{U"3 files copied"});
}

AURORA_TEST_CASE(osc_777_body_keeps_every_semicolon_after_the_title) {
    // 判据 ① 的第二家形态：只切两刀，第二刀之后的**全部**内容是正文。
    const auto draft = draft_notification(777, U"notify;t;a;b;c", std::u32string_view{});
    AURORA_TEST_CHECK(draft.drop == NotificationDrop::None);
    AURORA_TEST_CHECK(draft.text.title == std::u32string{U"t"});
    AURORA_TEST_CHECK(draft.text.body == std::u32string{U"a;b;c"});
}

AURORA_TEST_CASE(osc_777_with_an_empty_title_falls_back_to_the_session_title) {
    const auto draft = draft_notification(777, U"notify;;long run finished", U"ssh: build");
    AURORA_TEST_CHECK(draft.drop == NotificationDrop::None);
    AURORA_TEST_CHECK(draft.text.title == std::u32string{U"ssh: build"});
    AURORA_TEST_CHECK(draft.text.body == std::u32string{U"long run finished"});
}

AURORA_TEST_CASE(osc_777_subcommand_matches_case_insensitively_in_ascii) {
    for (const std::u32string_view args :
         {U"notify;t;b", U"NOTIFY;t;b", U"Notify;t;b", U"nOtIfY;t;b"}) {
        const auto draft = draft_notification(777, args, std::u32string_view{});
        AURORA_TEST_CHECK(draft.drop == NotificationDrop::None);
        AURORA_TEST_CHECK(draft.text.title == std::u32string{U"t"});
        AURORA_TEST_CHECK(draft.text.body == std::u32string{U"b"});
    }
}

AURORA_TEST_CASE(osc_777_subcommands_other_than_notify_are_rejected) {
    // 变异 m2 的证人：首段不校验的话，`popup` 那一路也会弹出横幅——把没实现的串猜成通知。
    // `notfy` 与 `notifyx` 各守「长度须 6」与「逐字相等」两条，缺一档就是让近似串蒙混过关。
    for (const std::u32string_view args :
         {U"popup;b", U"testnotify;t;b", U"notify2;t;b", U"notifyx;t;b", U"notfy;t;b", U""}) {
        const auto draft = draft_notification(777, args, U"sess");
        AURORA_TEST_CHECK(draft.drop == NotificationDrop::NotNotify);
    }
}

AURORA_TEST_CASE(non_notification_command_numbers_are_not_interpreted) {
    // 本件只解释 9 与 777；接错命令号时出拒绝档而不是猜一份内容。
    for (const std::int32_t code : {0, 1, 7, 8, 10, 52, 133, 1337}) {
        const auto draft = draft_notification(code, U"anything", U"sess");
        AURORA_TEST_CHECK(draft.drop == NotificationDrop::NotNotify);
    }
}

AURORA_TEST_CASE(an_empty_body_is_dropped_not_delivered) {
    // 判据 ⑥（变异 m7）：空白横幅比不弹更糟。计数由状态机侧负责，本件只出「该丢」的结论。
    const auto bare = draft_notification(9, std::u32string_view{}, U"sess");
    AURORA_TEST_CHECK(bare.drop == NotificationDrop::EmptyBody);

    const auto title_without_body = draft_notification(777, U"notify;only a title", U"sess");
    AURORA_TEST_CHECK(title_without_body.drop == NotificationDrop::EmptyBody);

    const auto empty_title_and_body = draft_notification(777, U"notify;;", U"sess");
    AURORA_TEST_CHECK(empty_title_and_body.drop == NotificationDrop::EmptyBody);
}

AURORA_TEST_CASE(the_body_is_clipped_at_its_code_point_limit) {
    // 判据 ③（变异 m3）：解析器给的容量是 1 MiB 码点，那是「不炸内存」而不是「适合当横幅」。
    const auto draft = draft_notification(9, repeated(U'a', 600U), U"sess");
    AURORA_TEST_CHECK(draft.drop == NotificationDrop::None);
    AURORA_TEST_CHECK_EQ(draft.text.body.size(), kMaxNotificationBodyCodePoints);
    AURORA_TEST_CHECK(ends_with_ellipsis(draft.text.body));
    AURORA_TEST_CHECK(
        draft.text.body.substr(0U, kMaxNotificationBodyCodePoints - kEllipsisCodePoints) ==
        repeated(U'a', kMaxNotificationBodyCodePoints - kEllipsisCodePoints));
}

AURORA_TEST_CASE(the_title_is_clipped_at_its_own_smaller_limit) {
    const auto draft = draft_notification(9, U"done", repeated(U'b', 200U));
    AURORA_TEST_CHECK(draft.drop == NotificationDrop::None);
    AURORA_TEST_CHECK_EQ(draft.text.title.size(), kMaxNotificationTitleCodePoints);
    AURORA_TEST_CHECK(ends_with_ellipsis(draft.text.title));
}

AURORA_TEST_CASE(a_payload_exactly_at_the_limit_is_left_untouched) {
    // 截断只在「超出」时发生：恰好到上限的一条补了点就吃掉一个真实字符。
    const auto draft = draft_notification(9, repeated(U'x', kMaxNotificationBodyCodePoints), U"s");
    AURORA_TEST_CHECK_EQ(draft.text.body.size(), kMaxNotificationBodyCodePoints);
    AURORA_TEST_CHECK_FALSE(ends_with_ellipsis(draft.text.body));
}

AURORA_TEST_CASE(clipping_counts_code_points_and_never_splits_one) {
    // 判据 ③ 的另一半：按码点计数而不是按字节。CJK-LITERAL: 断言素材 - 多字节字形才是
    // 「按字节切会切出半个字」的唯一证人，纯 ASCII 行测不到这一条。
    const auto draft = draft_notification(9, repeated(U'\u6784', 600U), std::u32string_view{});
    AURORA_TEST_CHECK(draft.drop == NotificationDrop::None);
    AURORA_TEST_CHECK_EQ(draft.text.body.size(), kMaxNotificationBodyCodePoints);
    AURORA_TEST_CHECK(ends_with_ellipsis(draft.text.body));
    for (const char32_t code_point :
         draft.text.body.substr(0U, kMaxNotificationBodyCodePoints - kEllipsisCodePoints)) {
        AURORA_TEST_CHECK(code_point == U'\u6784');
    }
}

AURORA_TEST_CASE(fit_code_points_is_identity_under_the_limit) {
    AURORA_TEST_CHECK(fit_code_points(std::u32string_view{}, 10U) == std::u32string{});
    AURORA_TEST_CHECK(fit_code_points(U"short", 10U) == std::u32string{U"short"});
    AURORA_TEST_CHECK(fit_code_points(U"12345", 5U) == std::u32string{U"12345"});
    // 省略号占掉三枚额度：6 码点限到 5 就是留 2 枚 + 3 枚点。
    AURORA_TEST_CHECK(fit_code_points(U"123456", 5U) == std::u32string{U"12..."});
}

AURORA_TEST_CASE(fit_code_points_degrades_to_a_plain_cut_below_the_ellipsis_length) {
    // 上限不大于省略号时不再补点（补了就超上限）；这一档是常量被改小时的护栏。
    AURORA_TEST_CHECK(fit_code_points(U"1234567", 2U) == std::u32string{U"12"});
    AURORA_TEST_CHECK(fit_code_points(U"1234567", 3U) == std::u32string{U"123"});
    AURORA_TEST_CHECK(fit_code_points(U"1234567", 0U) == std::u32string{});
}

AURORA_TEST_CASE(the_fallback_title_is_ascii_and_non_empty) {
    // 兜底串一旦为空，三级链就断在最后一档；字面量本身按 AGENTS.md §4.3 第 14 条必须是 ASCII。
    AURORA_TEST_CHECK(std::string_view{kNotificationFallbackTitle} == std::string_view{"Borealis"});
    const auto draft = draft_notification(777, U"notify;;b", std::u32string_view{});
    AURORA_TEST_CHECK_FALSE(draft.text.title.empty());
    AURORA_TEST_CHECK(draft.text.title == std::u32string{U"Borealis"});
}

}  // namespace borealis::test_cases::utest_term_notification
