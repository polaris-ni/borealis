#include "borealis/ui/input_latency.h"

#include <algorithm>

namespace borealis::ui {

namespace {

/// @brief 最近秩法的分子/分母（95%）：写成整数对而不写 `0.95`，是为了让「第几小」这条判据在
///        用例里可以按整数逐值断言——浮点会让 20 笔那一档在 19 与 20 之间飘。
constexpr std::size_t kPercentileNumerator = 95U;
constexpr std::size_t kPercentileDenominator = 100U;

}  // namespace

InputLatency::InputLatency(std::size_t sample_cap,
                            std::chrono::milliseconds attribution_cap,
                            std::chrono::milliseconds stale_after) noexcept
    : sample_cap_(sample_cap == 0U ? 1U : sample_cap)  // 空窗口没有读数，也不该由调用方造出来
    , attribution_cap_(attribution_cap)
    , stale_after_(stale_after) {}

auto InputLatency::note_input_issued(std::chrono::steady_clock::time_point now) -> void {
    // 后到的覆盖先到的：连打与自动重复时起点不断后移，测的是「末笔键入 → 首次上屏」（裁决 7.186③）。
    pending_ = now;
}

auto InputLatency::note_output(std::chrono::steady_clock::time_point now) -> bool {
    if (!pending_.has_value()) {
        return false;  // 没有欠着的键入：这一帧的上屏不是任何一笔键入的回应（远端自己刷屏）
    }
    const auto start = *pending_;
    pending_.reset();  // 一笔键入只配**首个**上屏时刻，此后的同批 damage 不再产出样本
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    if (elapsed < std::chrono::milliseconds{0}) {
        elapsed = std::chrono::milliseconds{0};  // 倒序时刻只可能来自调用方：夹到 0 而不是让负值进窗口
    }
    if (elapsed > attribution_cap_) {
        // 那一段不是「你刚按下那个键」的回应（无回显的等待、远端卡住），计进来会把 P95 长期钉死；
        // 丢弃单独计数交可观测面，不静默吞（裁决 7.186②）。
        ++dropped_;
        return false;
    }
    push_sample(elapsed);
    last_sample_at_ = now;
    return true;
}

auto InputLatency::p95(std::chrono::steady_clock::time_point now) const
    -> std::optional<std::chrono::milliseconds> {
    if (sorted_.empty()) {
        return std::nullopt;
    }
    if (now - last_sample_at_ > stale_after_) {
        return std::nullopt;  // 过期：挂着一小时前的老读数比什么都不画更坏（裁决 7.186④）
    }
    const std::size_t count = sorted_.size();
    // ceil(0.95 × N)：整数写法，恒落在 [1, N]，故下一行的 `rank − 1` 不需要再判界。
    const std::size_t rank = (kPercentileNumerator * count + kPercentileDenominator - 1U) / kPercentileDenominator;
    return sorted_[rank - 1U];
}

auto InputLatency::push_sample(std::chrono::milliseconds sample) -> void {
    // 两个容器各管一头：按笔数窗口淘汰的是**最旧**那一笔（时间序的头），而取分位数要按值排。
    // 只留时间序就得每次读数排序（逐帧路径，§4.5 第 25 条不让），只留有序则「谁是最旧」无从得知。
    if (time_order_.size() >= sample_cap_) {
        const auto oldest = time_order_.front();
        time_order_.pop_front();
        const auto at = std::lower_bound(sorted_.begin(), sorted_.end(), oldest);
        if (at != sorted_.end()) {
            sorted_.erase(at);  // 同值样本可能有多枚：任消其一即与时间序等量，不必知道具体是哪一笔
        }
    }
    time_order_.emplace_back(sample);
    sorted_.insert(std::upper_bound(sorted_.begin(), sorted_.end(), sample), sample);
}

}  // namespace borealis::ui
