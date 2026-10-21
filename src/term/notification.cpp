// ============================================================
// OSC 9 / 777 的切分与折算实现（src/term/notification.cpp）
// ------------------------------------------------------------
// 纯逻辑：只吃「命令号 + 首个 `;` 之后的整段」，产出「发不发 + 发什么」。不触框架、不触 IO、
// 不读会话状态（会话标题由调用方作入参递进来，故本件可脱界面单测，AGENTS.md §4.4 第 20 条）。
// ============================================================

#include "borealis/term/notification.h"

#include <string>

#include "osc.h"

namespace borealis::term {

namespace {

/// @brief 省略号本体：ASCII 三点，不用 U'…'——后者在截断点上会再吃掉一个字宽且各桌面呈现不一。
constexpr std::u32string_view kEllipsis = U"...";

/// @brief 只看 ASCII 的小写化：非 ASCII 原样返回，于是「_notify」这类串必然匹配不上。
[[nodiscard]] constexpr auto lower_ascii(char32_t c) noexcept -> char32_t {
    if (c >= U'A' && c <= U'Z') {
        return c + (U'a' - U'A');
    }
    return c;
}

/// @brief 子命令是否逐字为 `notify`（大小写不敏感、只认 ASCII）。
[[nodiscard]] auto is_notify_subcommand(std::u32string_view head) noexcept -> bool {
    if (head.size() != 6U) {
        return false;
    }
    static constexpr std::u32string_view kNotify = U"notify";
    for (std::size_t i = 0; i < kNotify.size(); ++i) {
        if (lower_ascii(head[i]) != kNotify[i]) {
            return false;
        }
    }
    return true;
}

/// @brief 码点串的 ASCII 兜底标题（字面量在头文件里是 `const char *`，这里升格成码点）。
[[nodiscard]] auto widen_ascii(std::string_view ascii) -> std::u32string {
    std::u32string out;
    out.reserve(ascii.size());
    for (const char c : ascii) {
        out.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 标题三级兜底链的最后一级（稿 D2①）。
[[nodiscard]] auto fallback_title() -> std::u32string {
    return widen_ascii(kNotificationFallbackTitle);
}

}  // namespace

auto fit_code_points(std::u32string_view text, std::size_t limit) -> std::u32string {
    if (text.size() <= limit) {
        return std::u32string{text};
    }
    // 额度不够放省略号时宁可纯截断：省略号的意义是「后面还有」，而三个字符的尾巴既占满额度
    // 又没留下任何内容，读起来只是噪声。
    if (limit <= kEllipsis.size()) {
        return std::u32string{text.substr(0, limit)};
    }
    auto out = std::u32string{text.substr(0, limit - kEllipsis.size())};
    out.append(kEllipsis);
    return out;
}

auto draft_notification(std::int32_t code, std::u32string_view args,
                        std::u32string_view session_title) -> NotificationDraft {
    NotificationDraft draft{};

    std::u32string_view raw_title{};
    std::u32string_view raw_body{args};

    if (code == 777) {
        // 第一刀取子命令：`777` 下面还挂着别的子命令，首段不是 `notify` 即整串不归本件（稿 D1①）。
        const auto sub = osc::split_first(args, U';');
        if (!sub.found || !is_notify_subcommand(sub.head)) {
            draft.drop = NotificationDrop::NotNotify;
            return draft;
        }
        // 第二刀取标题：正文取「首个分号之后的**全部**剩余」，故正文里的分号不被截（判据 ②）。
        const auto title = osc::split_first(sub.tail, U';');
        raw_title = title.head;
        raw_body = title.found ? title.tail : std::u32string_view{};
        draft.text.kind = NotificationKind::NotifyExtension;
    } else if (code != 9) {
        // 其余命令号到这里就是调用方接错：按「不是通知」处理而不是猜一个语义。
        draft.drop = NotificationDrop::NotNotify;
        return draft;
    }

    if (raw_body.empty()) {
        draft.drop = NotificationDrop::EmptyBody;
        return draft;
    }

    // 标题三级链：自带 → 会话标题 → ASCII 兜底（空标题在桌面端跨平台分叉，稿 D2① 已否掉留空档）。
    std::u32string title{raw_title};
    if (title.empty()) {
        title = std::u32string{session_title};
    }
    if (title.empty()) {
        title = fallback_title();
    }

    draft.text.title = fit_code_points(title, kMaxNotificationTitleCodePoints);
    draft.text.body = fit_code_points(raw_body, kMaxNotificationBodyCodePoints);
    return draft;
}

}  // namespace borealis::term
