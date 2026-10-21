#pragma once

// ============================================================
// 系统通知落地件（include/borealis/session/notification_outbox.h）
// ------------------------------------------------------------
// 状态机在会话读线程的网格锁内只留存最近一条 `OSC 9` / `OSC 777`（架构 §3.4、§5.4）；投递是
// main-thread only 的 OS IO，故「取走 + 投递」单独成一件，形态照 `ClipboardOutbox`。
// 本类的实现文件是全仓唯一触达 `aurora::NotificationCenter` 的地方，框架头不出那个文件
// （架构 §2.3）。
//
// 为什么 tag 要由调用方把身份递进来：`Session` 自己没有标识（实测：其公共面只有连接、网格与
// 取走族），而 `ui::PaneId` 是**每棵 pane 树内**只增不复用、`ui::TabId` 才是进程级一支
// （`main.cpp` 的 `next_tab_id`）。所以「会话级恒定」这个要求只有 `(标签号, 格号)` 这一对能
// 表达，而这一对只在装配层那格循环里同时可得。稿 D4① 与 D9 的签名据此补入实测口径。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace borealis::session {

class Session;

/// @brief 通知 tag 的前缀：后面接 `(标签号, 格号)` 那一对进程内不撞的号（`notification_tag()`）。
inline constexpr std::string_view kNotificationTagPrefix = "borealis-session-";

/// @brief 一条会话通知的去重/替换标识（`aurora::Notification::tag`，稿 D4①）。
///
/// 会话级恒定而不是逐条递增：框架把同 tag 定义为「替换前一条」，于是「同一会话至多一条在册」
/// 由 OS 侧与本仓的单槽各守一层（稿 D3①）。跨会话取不同 tag，正相反，互不覆盖。
/// 拆成自由函数是因为落地件与用例都要用它，而 tag 的字面形态本身就是判据。
/// @param tab_id 该会话所在标签号（进程级只增不复用）。
/// @param pane_id 该会话所在格号（该标签内只增不复用）。
/// @return 形如 `borealis-session-3.1` 的 ASCII 串。
[[nodiscard]] auto notification_tag(std::uint64_t tab_id, std::uint64_t pane_id) -> std::string;

/// @brief 远端 `OSC 9` / `OSC 777` 到系统通知的落地件（`SPEC.FEAT.INTEG.03`）。
class NotificationOutbox {
  public:
    /// @brief 取走该会话的待发通知并投递一条；无待发即空转。
    ///
    /// 只可在主线程的帧边界调用（框架的 `notify` 是 main-thread only）。**取走即空**，失败也不
    /// 回填：框架把失败定义成「永久失败、不可重试」，回填就是替它造一条它没有的重试语义。
    /// 失败只留一条带 `detail` 的诊断，不在界面上另起提示（稿 D6①）。
    /// @param session 目标会话。
    /// @param tab_id 该会话所在标签号（进程级只增不复用）。
    /// @param pane_id 该会话所在格号（该标签内只增不复用）。
    /// @return 成功投递的条数（0 或 1）；0 既可能是「无待发」也可能是「投不出去」，两档在调用方
    ///         无需分辨——它拿不到回执，也不需要为一条远端发起的提醒改排帧。
    static auto drain(Session &session, std::uint64_t tab_id, std::uint64_t pane_id) -> std::size_t;
};

}  // namespace borealis::session
