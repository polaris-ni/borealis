#pragma once

// ============================================================
// 「命令该落哪一扇窗」的记录器（src/ui/active_window_tracker.h，私有头）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.03` 的 D7 那一件：框架既不给「当前活动窗口」查询，也不把派发来源带给快捷键
// 闭包（稿 §0 的 F4／F5、稿 §3 的 G42），故本仓自持这一枚最近记录器。形态口径三条：
// - **上报的是事实而不是推断**：只有「本窗的视口/标签条/横幅真的收到过一次按键或指针 Press」
//   才上报；OS 层的激活态不由本件猜测，二者在无头与失焦场景下可能不同（稿 §9 第 2 条走查项）。
// - **回主窗是兜底不是猜测**：没有任何上报（刚开的第二扇窗还没被碰过、或全部记录随关窗撤账）
//   时回构造期那枚主窗号——命令因此永远有一个明确落点，而不是「取不到就谁也不做」。
// - **只留最后一次**：交叉上报（A→B→A）以最后一次为准，本件不做计数、不做栈。
//
// 线程：只在主线程用（事件派发与命令闭包都在 `Application` 的帧循环里，AGENTS.md §4.5 第 25 条
// 的另一侧），故本件不含原子量与锁；跨线程唤醒走的是 `startup_wake` 那枚原子指针，与这里无关。
// ============================================================

#include <optional>

#include "aurora/window/window.h"

namespace au = aurora;

namespace borealis::ui {

/// @brief 最近一次收到派发的那扇窗；无记录时回主窗。
///
/// 号就是 `au::WindowId`（框架 `Application` 登记宿主时自增分配，0 为无效哨兵）。本件**不**判断
/// 号是否还活着——它只记事实，「这扇窗还在不在」由装配层经 `window_host(id)` 现查，查不到即
/// 命令按空落点处理。
class ActiveWindowTracker {
  public:
    /// @brief 以主窗号为兜底落点。
    /// @param main_window 构造期读到的 `Application::main_window()`；此后主窗易主须由调用方换落点。
    explicit ActiveWindowTracker(au::WindowId main_window);

    /// @brief 记下一次派发：@p id 这一扇窗刚收到过按键或指针 Press。
    /// @param id 收到派发的那扇窗号；无效号（0）不改动任何记录。
    auto report(au::WindowId id) -> void;

    /// @brief 命令与快捷键的落点。
    /// @return 最近一次上报且尚未撤账的窗号；没有记录时回构造期那枚主窗号（可能仍是无效号，
    ///         即「主窗自己也没了」，调用方据此按空落点处理）。
    [[nodiscard]] auto active() const -> au::WindowId;

    /// @brief 撤账：该窗已关闭，它的号此后不再被返回。
    /// @param id 关闭的那扇窗号；命中最近记录即清除，命中兜底落点即置无效。
    auto forget(au::WindowId id) -> void;

    /// @brief 换兜底落点（主窗易主时由装配层调用，`SPEC.FEAT.WS.03` 批 4）。
    ///
    /// 框架在宿主回收之后会把主窗位交给「登记序里还活着的第一扇」（`reap_closed` 末段），而本件构造期
    /// 记下的那枚号届时已经消散——不换兜底，关一扇窗之后所有命令都要等用户在另一扇里点一下才有落点。
    /// **只改兜底、不动派发事实**：这一跳与 `report` 是两件事，`report` 说「这扇窗刚被碰过」，这里只说
    /// 「没人被碰过时该落哪一扇」。无效号（0＝还没登记进宿主）不安装，宁可与 `report` 同档——留旧兜底。
    /// @param id 新的兜底落点；与 @p forget 不同，本跳不影响最近记录。
    auto set_fallback(au::WindowId id) -> void;

  private:
    au::WindowId main_window_ = au::AURORA_INVALID_WINDOW_ID;  ///< 兜底落点（构造期传入）。
    std::optional<au::WindowId> last_reported_{};              ///< 最近一次上报；空即「没人被碰过」。
};

}  // namespace borealis::ui
