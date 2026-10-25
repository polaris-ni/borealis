// ============================================================
// 首败决策闸实现（src/ui/sftp_batch.cpp）
// ------------------------------------------------------------
// 全部动作在「置 pending → 等 → 消费」三步序里做，临界区只碰内存
// （AGENTS §4.5 第 25 条：闸上没有 IO，等待只发生在 worker 线程）。
// ============================================================

#include "ui/sftp_batch.h"

#include <chrono>

namespace borealis::ui {

auto BatchDecisionLatch::wait_for_decision(int time_limit_ms) -> BatchDecision {
    {
        std::lock_guard<std::mutex> lock{mutex_};
        // 过期投递先清后置：上一问没人答而 UI 补投的答话不得串到本问。
        pending_ = true;
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock{mutex_};
    const auto arrived = [this] { return !pending_; };
    const unsigned limit = time_limit_ms > 0 ? static_cast<unsigned>(time_limit_ms) : 1u;
    if (!cv_.wait_for(lock, std::chrono::milliseconds{limit}, arrived)) {
        // 超时＝无人应答，按缺省焦点档「中止」自收（D7①），并把 pending 撤掉，
        // 免得迟到的答话被误认成对下一问的回答。
        pending_ = false;
        return BatchDecision::Abort;
    }
    return decision_;
}

auto BatchDecisionLatch::post_decision(BatchDecision decision) -> void {
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!pending_) {
            return;  // 没有未答之问——迟到的答话直接丢弃，不污染下一问。
        }
        decision_ = decision;
        pending_ = false;
    }
    cv_.notify_all();
}

auto BatchDecisionLatch::abort() -> void {
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!pending_) {
            return;
        }
        decision_ = BatchDecision::Abort;
        pending_ = false;
    }
    cv_.notify_all();
}

[[nodiscard]] auto BatchDecisionLatch::has_pending() const -> bool {
    std::lock_guard<std::mutex> lock{mutex_};
    return pending_;
}

}  // namespace borealis::ui
