#include "borealis/ui/grid_size_debounce.h"

namespace borealis::ui {

GridSizeDebounce::GridSizeDebounce(std::chrono::milliseconds quiet_period) : quiet_period_(quiet_period) {}

auto GridSizeDebounce::request(PaneId pane, GridSize size, Moment now) -> void {
    expected_[pane] = size;
    // 尾沿去抖：每次请求都把窗口推到「此刻 + 静默时长」，于是连续拖拽期间一直不下发，停下来
    // 才发一次。首个请求与后续请求走同一条赋值，不设「只在空表时武装」的分支。
    deadline_ = now + quiet_period_;
}

auto GridSizeDebounce::due(Moment now) -> std::vector<std::pair<PaneId, GridSize>> {
    if (!deadline_ || now < *deadline_) {
        return {};
    }
    std::vector<std::pair<PaneId, GridSize>> out;
    for (const auto &[pane, size] : expected_) {
        const auto dispatched = dispatched_.find(pane);
        if (dispatched != dispatched_.end() && dispatched->second == size) {
            continue;  // 净变化为零：对端已经处在这个行列，重发只会让它再重排一次整屏
        }
        dispatched_[pane] = size;
        out.emplace_back(pane, size);
    }
    expected_.clear();
    deadline_ = std::nullopt;
    return out;
}

auto GridSizeDebounce::next_deadline() const noexcept -> std::optional<Moment> { return deadline_; }

auto GridSizeDebounce::has_pending() const noexcept -> bool { return !expected_.empty(); }

auto GridSizeDebounce::forget(PaneId pane) -> void {
    expected_.erase(pane);
    dispatched_.erase(pane);
    if (expected_.empty()) {
        deadline_ = std::nullopt;
    }
}

}  // namespace borealis::ui
