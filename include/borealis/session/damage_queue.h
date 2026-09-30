#pragma once

// ============================================================
// 网格更新提交的有界队列（include/borealis/session/damage_queue.h）
// ------------------------------------------------------------
// 架构 §3.6 / `SPEC.NF.PERF.06`：会话读线程与 UI 之间的背压队列。队列里流转的是
// **「哪些视口行需要重读」的提交**，不是字节也不是 cell 内容——权威 grid 由读线程持有
// （架构 §3.4），主线程据提交去网格取最新值，故「同一格被多次覆盖」只会被合并成一次重读，
// 最终内容恒等于 PTY 输出（需求里唯一许可的丢弃形态）。
//
// 有界是本模块存在的全部理由：UI 停摆时读线程仍在高速产出，条目数上限即内存上限。
// 满了不丢条目，而是把新提交与队尾提交**并成更宽的区间**——多读几行是冗余，不是错误。
// ============================================================

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace borealis::session {

/// @brief 队列容量默认上限（条）：视口行数远大于它时溢出是常态，合并路径须真实可达。
inline constexpr std::size_t kDefaultDamageQueueCapacity = 64;

/// @brief 单帧最多消费的合并提交数默认值（`SPEC.NF.PERF.06` 的 N）。
inline constexpr std::size_t kDefaultPerFrameBudget = 8;

/// @brief 一次网格更新提交：视口内的连续行区间（闭区间）。
struct Damage {
    std::size_t first_row = 0;
    std::size_t last_row = 0;
    /// @brief 整屏位移（全视口滚动、清屏、主备屏切换、尺寸变更）：行号与内容的对应关系变了。
    bool full_screen = false;
};

/// @brief 队列水位与合并/让出计数，供可观测面板读取（`SPEC.NF.RELI.01`）。
struct QueueStats {
    std::size_t pending = 0;      ///< 当前待消费条数。
    std::size_t peak_pending = 0; ///< 历史最高水位。
    std::size_t overloads = 0;    ///< 队列满而按合并处理的次数。
    std::size_t merges = 0;       ///< 合并次数（相邻区间归并与溢出合并都计入）。
    std::size_t yields = 0;       ///< 单帧消费到预算上限后主动让出的次数。
};

/// @brief 读线程推入、主线程按帧消费的有界队列；自身线程安全，不依赖调用方持锁。
class DamageQueue {
  public:
    /// @brief 建一条队列。
    /// @param capacity 条目数上限；非正值退化为「所有脏区恒并成一条」。
    /// @param per_frame_budget 单帧最多消费的条数（`SPEC.NF.PERF.06` 的 N）；0 视作 1。
    DamageQueue(std::size_t capacity = kDefaultDamageQueueCapacity,
                std::size_t per_frame_budget = kDefaultPerFrameBudget);

    /// @brief 推入一段视口行脏区（闭区间）。
    /// @param first_row 起始行号（视口内 0 基）。
    /// @param last_row 结束行号（含）。
    auto push_rows(std::size_t first_row, std::size_t last_row) -> void;

    /// @brief 推入整屏脏：已排队的所有提交都被它覆盖，一并作废。
    auto push_full_screen() -> void;

    /// @brief 消费一帧：取走不超过预算条数的提交，其余留待下一帧。
    ///
    /// 有剩余即记一次让出（`SPEC.NF.PERF.06` 的「主动让出，保证输入响应不被饿死」）。
    /// @return 本帧要重读的提交，按推入先后排列。
    auto drain() -> std::vector<Damage>;

    /// @brief 是否有待消费提交（主线程判断要不要再排一帧）。
    [[nodiscard]] auto pending() const -> bool;

    /// @brief 当前水位与累计计数。
    [[nodiscard]] auto stats() const -> QueueStats;

  private:
    /// @brief 把 @p damage 并入队尾提交（调用方已确认区间相交或相邻）。
    auto merge_into_back(const Damage &damage) -> void;

    mutable std::mutex mutex_;
    std::deque<Damage> queue_;
    std::size_t capacity_;
    std::size_t per_frame_budget_;
    QueueStats stats_{};
};

}  // namespace borealis::session
