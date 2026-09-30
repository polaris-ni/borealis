// ============================================================
// 网格更新提交的有界队列实现（src/session/damage_queue.cpp）
// ------------------------------------------------------------
// 语义解释的唯一落点在会话层，本文件只回答「脏区怎么在有上界的队列里流转」：
// 区间相交或相邻就并成一条，队满也把新的并进球尾那条——区间只会变宽不会变窄，
// 于是「合并而非丢弃」在结构上就是不可能丢的（架构 §3.6）。
// ============================================================

#include "borealis/session/damage_queue.h"

#include <algorithm>
#include <utility>

namespace borealis::session {

namespace {

/// @brief 两个行区间是否相交或首尾相接（相接即可并成一条，覆盖行数不变多）。
[[nodiscard]] auto touches(const Damage &left, const Damage &right) -> bool {
    return left.first_row <= right.last_row + 1U && right.first_row <= left.last_row + 1U;
}

}  // namespace

DamageQueue::DamageQueue(std::size_t capacity, std::size_t per_frame_budget)
    : capacity_(capacity), per_frame_budget_(per_frame_budget == 0 ? 1 : per_frame_budget) {}

auto DamageQueue::push_rows(std::size_t first_row, std::size_t last_row) -> void {
    const std::lock_guard lock{mutex_};
    if (!queue_.empty() && queue_.back().full_screen) {
        // 整屏脏已覆盖所有行，本条提交无新增信息。
        ++stats_.merges;
        return;
    }
    if (!queue_.empty() && touches(queue_.back(), Damage{first_row, last_row, false})) {
        merge_into_back(Damage{first_row, last_row, false});
        return;
    }
    if (queue_.empty() || queue_.size() < capacity_) {
        queue_.push_back(Damage{first_row, last_row, false});
        stats_.pending = queue_.size();
        stats_.peak_pending = std::max(stats_.peak_pending, queue_.size());
        return;
    }
    // 队满：把新脏区并进队尾那条，代价是那条覆盖的行变多（多读几行，不读漏）。
    ++stats_.overloads;
    merge_into_back(Damage{first_row, last_row, false});
}

auto DamageQueue::push_full_screen() -> void {
    const std::lock_guard lock{mutex_};
    stats_.merges += queue_.size();
    queue_.clear();
    queue_.push_back(Damage{0, 0, true});
    stats_.pending = queue_.size();
    stats_.peak_pending = std::max(stats_.peak_pending, queue_.size());
}

auto DamageQueue::drain() -> std::vector<Damage> {
    const std::lock_guard lock{mutex_};
    const std::size_t taken = std::min(per_frame_budget_, queue_.size());
    std::vector<Damage> frame{queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(taken)};
    queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(taken));
    stats_.pending = queue_.size();
    if (!queue_.empty()) {
        ++stats_.yields;
    }
    return frame;
}

auto DamageQueue::pending() const -> bool {
    const std::lock_guard lock{mutex_};
    return !queue_.empty();
}

auto DamageQueue::stats() const -> QueueStats {
    const std::lock_guard lock{mutex_};
    return stats_;
}

auto DamageQueue::merge_into_back(const Damage &damage) -> void {
    auto &back = queue_.back();
    back.first_row = std::min(back.first_row, damage.first_row);
    back.last_row = std::max(back.last_row, damage.last_row);
    ++stats_.merges;
}

}  // namespace borealis::session
