#pragma once

// ============================================================
// 递归目录传输的首败决策闸（SPEC.FEAT.CONN.04 延后子项）
// ------------------------------------------------------------
// 稿 codespec/UI_SFTP_RECURSIVE.draft.md §2（裁决 7.151 D 表首败弹框）：
// worker 撞上一个非取消类失败 → 置 pending 并经收件箱问人 → 在闸上等待；
// UI 弹「跳过 / 中止」把答话投回来。三出口（答话 / 取消 / 面板析构收摊）
// 之外还有第四条**缺省**：人一直没答，定时限到按「中止」自收——面板 tick
// 与对话框都不是保证存在的对象，无限等待会把 worker join 不进去（析构即挂死）。
//
// 本件纯同步、无 IO、无 au:: 类型：它是批次接线腿（无 sshd 证人，稿 D8①）
// 之外唯一能被 utest 完整守住的跨线程件，三出口与超时档各有指名红案。
// ============================================================

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace borealis::ui {

/// @brief 首败问人的答话两档（D7①：「中止」是缺省焦点，也是超时与收摊档）。
enum class BatchDecision : std::uint8_t {
    Skip,   ///< 跳过这一件，批次继续。
    Abort,  ///< 中止整批（队列即止，当前件半成品清理已按既有腿走完）。
};

/// @brief 一次性决策闸（同一时刻至多一问，串行队列 D7① 保证单批次在途）。
///
/// 线程约定：wait_for_decision 只在 worker 线程调用；post_decision 与 abort
/// 任意线程可调（UI 答话、取消腿、析构收摊）。ask 置位即开一问，等待者凭
/// pred 谓词唤醒；出口由 wait 的返回值自明（决策值 + pending 是否已被消费）。
class BatchDecisionLatch {
public:
    /// @brief worker 侧：开一问并等待，限内无人答即 Abort。
    ///
    /// 置 pending 与检查旧答话同临界区：post/abort 若在 ask 之前落闸，那份答话
    /// 视为对**上一问**的过期投递、被本方法先清后置（不会串到下一问）。
    /// @param time_limit_ms 等待上限（毫秒）；<=0 视作不设限的 bug 档，钳到 1 ms。
    /// @return 答话档位；超时与收摊都是 Abort。
    auto wait_for_decision(int time_limit_ms) -> BatchDecision;

    /// @brief UI 侧：把「跳过 / 中止」投回闸上（仅对 pending 的那一问生效）。
    auto post_decision(BatchDecision decision) -> void;

    /// @brief 收摊腿：面板关闭 / Shutdown / 取消——pending 的那一问按 Abort 自答。
    auto abort() -> void;

    /// @brief 当前是否有未答的一问（UI tick 取收件箱 TransferAsk 时的对账位）。
    [[nodiscard]] auto has_pending() const -> bool;

private:
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    bool pending_{false};       ///< mutex_ 守护。
    BatchDecision decision_{BatchDecision::Abort};  ///< mutex_ 守护；pending 置位时值无意义。
};

}  // namespace borealis::ui
