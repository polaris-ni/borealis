#pragma once

// ============================================================
// OSC 9 / OSC 777 的正文形态与切分（include/borealis/term/notification.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.03` 里与会话字节、与投递都无关的那半：把「一条 OSC 串的载荷」折成
// 「要不要发、发给谁看、发什么字」三件事。真正的投递在主线程（`session::NotificationOutbox`），
// 留存与取走在那枚动作型槽（`term::Terminal`），本文件只做纯切分与折算，不触任何 IO。
//
// 四条决定形态的 WHY（口径与代价见 `codespec/UI_INTEG_NOTIFY.draft.md` §3）：
//
//   1. **两家来源一套形态**：`OSC 9 ; <text>` 是「只有正文」的老约定，`OSC 777 ; notify ;
//      <title> ; <body>` 是「带子命令」的扩展。二者都折成同一个 `PendingNotification`，
//      差别只记在 `kind` 上——用例与诊断要能分辨来源，而投递侧不需要。
//   2. **777 的首段必须逐字是 `notify`**（大小写不敏感的 ASCII 比较）：777 下面还挂着别的子命令，
//      把它们一律当通知发就是让「一条我们没实现的串」变成屏幕上的一条横幅。拒绝档出
//      `NotificationDrop::NotNotify`，由状态机按未识别序列留痕（架构 §5.5）。
//   3. **正文为空即不发**：空白 toast 是纯噪音，而它比不弹更糟——用户会以为程序在胡言。
//      这条仍然计数（`NotificationDrop::EmptyBody`），因为「远端确实发了而屏上什么都没有」
//      是需要一条账可查的那种状态。
//   4. **长度在本层截，不等通知服务**：解析器给单条 OSC 串的容量是 1 MiB 码点，那是「不炸内存」
//      的上限而不是「适合当横幅」的上限。截断是对外体量的唯一约束点，故必须发生在留存之前
//      ——一旦把整段交出去，长度就由不得本仓了。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace borealis::term {

/// @brief 通知的来源形态（只服务诊断与用例，投递侧不据此分支）。
enum class NotificationKind : std::uint8_t {
    TerminalMessage,  ///< `OSC 9`：只有正文的老约定（xterm / iTerm2 语义）。
    NotifyExtension,  ///< `OSC 777 ; notify ; …`：带标题的扩展形态。
};

/// @brief 一条通知被丢弃的原因。
enum class NotificationDrop : std::uint8_t {
    None,       ///< 未丢弃：`text` 就是要发的内容。
    NotNotify,  ///< `777` 的首段不是 `notify`：整串按未识别序列处理。
    EmptyBody,  ///< 正文为空：不发空白横幅，但仍计入请求数。
};

/// @brief 一条待发通知的内容（`OSC 9` 无标题字段，其 `title` 由会话标题兜底而来）。
struct PendingNotification {
    NotificationKind kind = NotificationKind::TerminalMessage;  ///< 来源形态。
    std::u32string title;  ///< 标题：恒非空（三级兜底链见 `draft_notification`），已按上限截断。
    std::u32string body;   ///< 正文：恒非空（空正文走 `NotificationDrop::EmptyBody`），已按上限截断。
};

/// @brief 折算结果：内容 + 丢弃原因。
struct NotificationDraft {
    PendingNotification text{};               ///< `drop` 为 `None` 时即待发内容。
    NotificationDrop drop = NotificationDrop::None;  ///< 丢弃原因；`None` 即照发。
};

/// @brief 正文的码点上限（稿 D10①）。
///
/// 取这一档只为「有界」而不为「显示得下」：toast 的可见长度本来就远小于它。看不全的长正文
/// 不该走通知，那是会话日志（`SPEC.FEAT.CONN.11`）的射程。
inline constexpr std::size_t kMaxNotificationBodyCodePoints = 512U;

/// @brief 标题的码点上限（稿 D10①，与正文同一条理由、小一档取值）。
inline constexpr std::size_t kMaxNotificationTitleCodePoints = 128U;

/// @brief 会话标题也为空时的最后兜底（ASCII 字面量；AGENTS.md §4.3 第 14 条）。
inline constexpr const char *kNotificationFallbackTitle = "Borealis";

/// @brief 把码点串截到 @p limit 个码点，超出时在末尾补 ASCII 三点省略号（占掉三个码点额度）。
/// @param text 待截文本。
/// @param limit 上限码点数（含省略号）；小于省略号长度时按省略号自身退化为纯截断。
/// @return 截断结果；未超限者原样返回。
[[nodiscard]] auto fit_code_points(std::u32string_view text, std::size_t limit) -> std::u32string;

/// @brief 把一条 OSC 串的载荷折成待发通知（稿 §2 判据 ①②③⑥ 的语义面）。
///
/// 标题的三级兜底链（稿 D2①）：`777` 自带的标题 → @p session_title（`OscState::title`，
/// `OSC 0/2` 的最近一次值）→ `kNotificationFallbackTitle`。三级都取完才截断，故兜底串
/// 过长时同样受 @c kMaxNotificationTitleCodePoints 约束。
/// @param code OSC 命令号；本件只解释 9 与 777，其余命令号即调用方接错。
/// @param args 首个 `;` 之后的整段参数（状态机侧已切出，本件不再切命令号）。
/// @param session_title 该会话最近一次的标题，可为空（空即跳过第二级兜底）。
/// @return 折算结果；`drop` 非 `None` 时调用方不得留存 `text`。
[[nodiscard]] auto draft_notification(std::int32_t code, std::u32string_view args,
                                      std::u32string_view session_title) -> NotificationDraft;

}  // namespace borealis::term
