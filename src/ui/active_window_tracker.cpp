// ============================================================
// 「命令该落哪一扇窗」的记录器实现（src/ui/active_window_tracker.cpp）
// ------------------------------------------------------------
// 三格状态：兜底落点、最近一次上报、以及「上报过但已被撤账」这一档由空 optional 表示。
// 判据与形态口径见头文件横幅。
// ============================================================

#include "ui/active_window_tracker.h"

namespace borealis::ui {

ActiveWindowTracker::ActiveWindowTracker(au::WindowId main_window) : main_window_(main_window) {}

auto ActiveWindowTracker::report(au::WindowId id) -> void {
    // 无效号即「这一扇还没登记进宿主」，此刻的按键确实发生了，但把记录换成 0 会让下一条命令
    // 落进空处——宁可留着上一个真实落点。
    if (id == au::AURORA_INVALID_WINDOW_ID) {
        return;
    }
    last_reported_ = id;
}

auto ActiveWindowTracker::active() const -> au::WindowId {
    return last_reported_.value_or(main_window_);
}

auto ActiveWindowTracker::forget(au::WindowId id) -> void {
    if (last_reported_ == id) {
        last_reported_.reset();
    }
    // 兜底落点自己关了：置无效，让调用方经 `window_host(0)` 取空后按「无落点」处理。主窗易主
    // 后由谁接任兜底属关窗那一棒的账目（稿 §6 批 4），本件不自己挑——它只记派发事实，接任者由
    // 装配层经 `set_fallback` 交进来。
    if (main_window_ == id) {
        main_window_ = au::AURORA_INVALID_WINDOW_ID;
    }
}

auto ActiveWindowTracker::set_fallback(au::WindowId id) -> void {
    // 与 `report` 同一条无效号档：无效号即「这一扇还没登记进宿主」，把兜底换成它等于亲手抹掉落点。
    if (id == au::AURORA_INVALID_WINDOW_ID) {
        return;
    }
    main_window_ = id;
}

}  // namespace borealis::ui
