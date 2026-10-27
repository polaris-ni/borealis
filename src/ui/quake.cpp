// ============================================================
// Quick Terminal 的纯逻辑腿（src/ui/quake.cpp）
// ------------------------------------------------------------
// 只算矩形与状态，不碰框架：贴边折算的输入是调用方递进来的工作区（物理像素），输出把「原点按
// 物理像素、尺寸按逻辑 dp」这个框架实现口径固化在一处（裁决 7.160 D2）。显隐两腿等缺口 G47
// 回货才有消费者，故本文件今天不接任何窗口令——任务书 `temp_tasks/FRAMEWORK_TASK_WS12_QUAKE.md`。
// ============================================================

#include "borealis/ui/quake.h"

#include <algorithm>
#include <cmath>

namespace borealis::ui {

namespace {

/// @brief 把占比夹进合法闭区间；非有限值（NaN / inf）按上界处理——装载侧的怪值不能把帘子折成
///        不可见，也不能让它无限大。
[[nodiscard]] auto clamp_ratio(double ratio) -> double {
    if (!std::isfinite(ratio)) {
        return kQuakeRatioMax;
    }
    return std::clamp(ratio, kQuakeRatioMin, kQuakeRatioMax);
}

/// @brief 非正尺寸归零：本件不猜屏，也不把负工作区翻成「从右往左长」。
[[nodiscard]] auto positive_or_zero(double extent) -> double {
    if (!std::isfinite(extent) || extent < 0.0) {
        return 0.0;
    }
    return extent;
}

}  // namespace

auto quake_target_rect(const QuakeGeometryRequest &request) -> QuakeTarget {
    const auto area_width = positive_or_zero(request.work_area.width);
    const auto area_height = positive_or_zero(request.work_area.height);
    const auto scale = std::isfinite(request.scale_factor) && request.scale_factor > 0.0 ? request.scale_factor : 1.0;
    const auto cross = clamp_ratio(request.cross_ratio);
    const auto along = clamp_ratio(request.along_ratio);

    // 落下方向贴边、跨边方向居中（稿 D4）：`Top` 档原点在工作区上沿，横向留一半余量做居中。
    auto target = QuakeTarget{};
    switch (request.anchor) {
        case QuakeAnchor::Top:
            target.width_dp = (area_width * cross) / scale;
            target.height_dp = (area_height * along) / scale;
            target.origin_x = request.work_area.origin_x + (area_width - area_width * cross) / 2.0;
            target.origin_y = request.work_area.origin_y;
            break;
        case QuakeAnchor::Bottom:
            target.width_dp = (area_width * cross) / scale;
            target.height_dp = (area_height * along) / scale;
            target.origin_x = request.work_area.origin_x + (area_width - area_width * cross) / 2.0;
            target.origin_y = request.work_area.origin_y + area_height - area_height * along;
            break;
        case QuakeAnchor::Left:
            target.width_dp = (area_width * along) / scale;
            target.height_dp = (area_height * cross) / scale;
            target.origin_x = request.work_area.origin_x;
            target.origin_y = request.work_area.origin_y + (area_height - area_height * cross) / 2.0;
            break;
        case QuakeAnchor::Right:
            target.width_dp = (area_width * along) / scale;
            target.height_dp = (area_height * cross) / scale;
            target.origin_x = request.work_area.origin_x + area_width - area_width * along;
            target.origin_y = request.work_area.origin_y + (area_height - area_height * cross) / 2.0;
            break;
    }
    return target;
}

auto QuakeToggle::on_hotkey(TimePoint now) -> QuakeIntent {
    // 过渡窗在**两个方向**都开：呼出时框架随后要 `focus_window()`，那次激活同样会闪一下失焦；
    // 收回时窗口失焦更是必然（稿 D6 竞争的两半）。
    ignore_until_ = now + focus_settle_;
    if (state_ == QuakeState::Hidden) {
        state_ = QuakeState::Shown;
        return QuakeIntent::Show;
    }
    state_ = QuakeState::Hidden;
    return QuakeIntent::Hide;
}

auto QuakeToggle::on_focus_lost(TimePoint now) -> QuakeIntent {
    if (!auto_hide_on_focus_lost_ || state_ != QuakeState::Shown) {
        return QuakeIntent::None;
    }
    if (ignoring_focus_at(now)) {
        return QuakeIntent::None;
    }
    state_ = QuakeState::Hidden;
    ignore_until_ = now + focus_settle_;
    return QuakeIntent::Hide;
}

}  // namespace borealis::ui
