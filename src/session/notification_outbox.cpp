// ============================================================
// 系统通知落地实现（src/session/notification_outbox.cpp）
// ------------------------------------------------------------
// 全仓唯一触达 `aurora::NotificationCenter` 的编译单元：取走、升格 UTF-8、组 tag、投递都在这
// 一文件，框架头不出这里（架构 §2.3）。
// ============================================================

#include "borealis/session/notification_outbox.h"

#include <string>

#include "aurora/app/notification.h"
#include "aurora/core/log.h"
#include "borealis/session/session.h"
#include "borealis/term/utf8.h"

namespace borealis::session {

auto notification_tag(std::uint64_t tab_id, std::uint64_t pane_id) -> std::string {
    // 分隔符用 `.` 而不是再一个 `-`：tag 是可读的诊断线索，`3.1` 一眼是「标签 3 的第 1 格」，
    // 而 `3-1` 与前缀里的连字符混在一起，肉眼分不出哪一段是号。
    return std::string{kNotificationTagPrefix} + std::to_string(tab_id) + '.' +
           std::to_string(pane_id);
}

namespace {

/// @brief 码点串升格成 UTF-8（框架的 `Notification` 收 `std::string`，口径是 UTF-8）。
[[nodiscard]] auto utf8_of(const std::u32string &text) -> std::string {
    std::string out;
    out.reserve(text.size());
    static_cast<void>(term::encode_utf8(text, out));
    return out;
}

}  // namespace

auto NotificationOutbox::drain(Session &session, std::uint64_t tab_id, std::uint64_t pane_id)
    -> std::size_t {
    const auto pending = session.take_notification();
    if (!pending.has_value()) {
        return 0U;
    }

    aurora::Notification notification;
    notification.title = utf8_of(pending->title);
    notification.body = utf8_of(pending->body);
    notification.tag = notification_tag(tab_id, pane_id);
    // 紧急度与驻留都取平台默认那一档（稿 D5①）：9 / 777 的公开形态里没有紧急度字段，而按正文
    // 关键词升级就是拿不可信输入做语义解释——`Critical` 明确不受专注时段抑制，误判的代价是抢走
    // 用户的专注时段。
    notification.urgency = aurora::NotificationUrgency::Normal;
    notification.timeout_ms = 0U;

    const auto result = aurora::NotificationCenter::notify(notification);
    if (!result.ok()) {
        // 一次投不出去即了结：框架把失败定成「永久失败、不可重试」，故不回填待发槽、不改排帧，
        // 也不在界面上另起提示（稿 D6①）。诊断留着 `detail`，无桌面环境与 macOS 都走这一条。
        AURORA_LOG_WARN("session", "notification post failed: ", result.error().message);
        return 0U;
    }
    return 1U;
}

}  // namespace borealis::session
