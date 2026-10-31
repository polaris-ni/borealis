// ============================================================
// 状态栏排布件实现（src/ui/status_bar_format.cpp）
// ------------------------------------------------------------
// 前缀单调性是这条算式的承重墙：一旦允许「跳过放不下的那格继续放后面的」，同一格就会随窗口宽度
// 与左邻文案的长短忽现忽隐，而状态栏的读数恰恰要靠「它一直在老位置」才读得动。故这里只有一个
// 游标、一次 break，不回头。
// ============================================================

#include "status_bar_format.h"

#include <cmath>

namespace borealis::ui {

namespace {

/// @brief 负宽度按 0 用：那是调用方的测量 bug，不该让它把整条算式变成非单调。
[[nodiscard]] auto at_least_zero(double value) -> double {
    return value > 0.0 ? value : 0.0;
}

}  // namespace

auto status_bar_registration_order() -> const std::vector<std::string_view> & {
    // 与 `include/borealis/config/settings.h` 的 `StatusBarSettings` 字段次序逐条同源（十枚开关），
    // 并由 `utest_status_bar_format` 拿设置页注册表的行序双向核对（次序漂了就转红，不靠注释守着）。
    // 稿屏 1 的状态栏画的就是这十枚里的八格（`UI_WORKSPACE.draft.svg` 编号块 ⑨／⑩），八格今日都有取处
    // （第八格 = `ui::InputLatency`，`SPEC.NF.PERF.01` 的量测腿，裁决 7.186）。另两格的今日去向是：
    // `reconnect`——判据 ⑧ 把断线与重连（第 N/M 次）画在 pane 上而不是这条；`cursor_position`——判据 ⑨
    // 那八项里没有「光标行列」这一象，稿上「焦点 · 行列数」角标（判据 ④）说的是格数而不是光标位置，故
    // 这一枚在稿面上**没有形态**。两枚都不进本帧的测量表，但**留在次序里**：补上消费方那一棒不必再排
    // 一次序，而省略次序按的就是这一条。
    static const std::vector<std::string_view> kOrder{
        "connection",     "reconnect",       "cursor_position", "encoding",       "grid_size",
        "font_size",      "theme",           "scrollback",      "clipboard_policy", "input_latency",
    };
    return kOrder;
}

auto plan_status_bar(const std::vector<StatusBarSlotMeasure> &slots,
                     double available_dp,
                     const StatusBarGeometry &geometry) -> StatusBarPlan {
    const double inset = at_least_zero(geometry.left_inset_dp);
    const double gap = at_least_zero(geometry.gap_dp);
    const double right_inset = at_least_zero(geometry.right_inset_dp);
    const double reserved_right = at_least_zero(geometry.reserved_right_dp);

    // 右端固定件（命令面板锚点）先占着，整条的剩余宽度就是数据格能用到的量。
    auto room_for = [&](double right_fixed) {
        return available_dp - at_least_zero(right_inset) - at_least_zero(right_fixed);
    };
    const double room_with_anchor = room_for(reserved_right);
    const double room_without_anchor = room_for(0.0);

    /// @brief 按给定房间取最长前缀，返回上屏数量。
    auto count_fit = [&](double room) -> std::size_t {
        double cursor = inset;
        std::size_t shown = 0;
        for (const auto &slot : slots) {
            const double need = (shown == 0 ? 0.0 : gap) + at_least_zero(slot.prefix_dp) +
                                at_least_zero(slot.text_width_dp);
            if (cursor + need > room) {
                break;
            }
            cursor += need;
            ++shown;
        }
        return shown;
    };

    // 锚点与数据格相争时让锚点先倒：状态栏的本分是读数，右端那一格是入口提示。判据只在这一帧
    // 成立——`reserved_right` 与宽度都来自同一帧，不会与下一帧的结论互相担保。
    StatusBarPlan plan;
    std::size_t shown_count = count_fit(room_with_anchor);
    if (shown_count == 0 && !slots.empty() && count_fit(room_without_anchor) > 0) {
        plan.right_anchor_dropped = true;
        shown_count = count_fit(room_without_anchor);
    }

    plan.shown.reserve(shown_count);
    plan.omitted.reserve(slots.size() - shown_count);
    for (std::size_t index = 0; index < slots.size(); ++index) {
        (index < shown_count ? plan.shown : plan.omitted).push_back(slots[index].id);
    }

    // 实占宽度按同一台算式再走一遍：两遍之间的唯一差别是 `shown_count` 已经定下来了。
    double cursor = inset;
    for (std::size_t index = 0; index < shown_count; ++index) {
        cursor += (index == 0 ? 0.0 : gap) + at_least_zero(slots[index].prefix_dp) +
                  at_least_zero(slots[index].text_width_dp);
    }
    plan.content_width_dp = shown_count == 0 ? 0.0 : cursor;
    return plan;
}

auto group_thousands(std::uint64_t value) -> std::string {
    const std::string digits = std::to_string(value);
    std::string out;
    out.reserve(digits.size() + (digits.size() - 1U) / 3U);
    for (std::size_t index = 0; index < digits.size(); ++index) {
        // 逗号插在「右侧还剩整组三位」的那一刻，故最左一组可以是 1～3 位（10,000 / 1,234,567）。
        if (index > 0U && (digits.size() - index) % 3U == 0U) {
            out += ',';
        }
        out += digits[index];
    }
    return out;
}

auto format_font_size(double pt) -> std::string {
    // 十分位是本键取值域内唯一需要的精度（`[6, 72]` 的步进档），先吸附再拆整数与小数，
    // 于是 `14.0` 与 `14.0000000001` 都落成同一句 `14`——后者是配置往返的常态。
    const long long tenths = std::llround(pt * 10.0);
    const long long whole = tenths / 10LL;
    const long long fraction = tenths % 10LL;
    if (fraction == 0LL) {
        return std::to_string(whole);
    }
    return std::to_string(whole) + "." + std::to_string(fraction < 0LL ? -fraction : fraction);
}

}  // namespace borealis::ui
