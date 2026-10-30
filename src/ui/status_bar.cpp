// ============================================================
// 状态栏实现（src/ui/status_bar.cpp）
// ------------------------------------------------------------
// 本件只做「测宽 → 交给降级算式 → 按结果落笔」。为什么在绘制里测宽而不是在 `on_layout`：状态栏的高
// 是常量 28 dp，文本变了不会改布局，只有宽度会变；而宽度既是排布输入又是绘制输入，放在一处算就没有
// 「布局用旧宽、绘制用新宽」的分叉（横幅的 `exit_button_box_` 同一先例）。
//
// 一次绘制的成本＝每格一次 `display_width`（框架内有度量缓存）＋一条纯算式。不做字号压缩、不截短
// 文本——那是 N3 明确否掉的两种形态。
// ============================================================

#include "status_bar.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "aurora/core/dimension.h"
#include "aurora/core/font.h"
#include "aurora/core/types.h"
#include "aurora/render/font_engine.h"
#include "borealis/term/utf8.h"

#include "settings_panel.h"

namespace borealis::ui {

namespace {

/// @brief 几何全部来自稿 §2.2 与屏 1 那条编号块的逐值（逻辑 dp）。
constexpr double kHeightDp = 28.0;
constexpr float kTextPt = 11.5F;          ///< 稿 §2.2 字号阶梯的「辅助 11~11.5」，屏 1 状态栏取 11.5。
constexpr double kTextLineHeightDp = 16.0;
constexpr double kLeftInsetDp = 16.0;     ///< 稿 §2.2 在册的左右内边距（菜单行同一条），见稿面 53 那处偏差。
constexpr double kGapDp = 20.0;           ///< 稿面逐格间距实测在 16~28 之间，取中值作单一常数。
constexpr double kRightInsetDp = 16.0;
constexpr double kDotSizeDp = 4.0;        ///< 连接态方点：稿面 4×4 描边框。
constexpr float kDotStrokeDp = 1.4F;
constexpr double kDotToTextDp = 5.0;      ///< 点右沿到文本左沿（稿面 53+4 → 62）。
constexpr double kSeparatorWidthDp = 1.4;   ///< 分隔线：稿面 1.4×16。
constexpr double kSeparatorHeightDp = 16.0;
constexpr double kSeparatorToTextDp = 12.6;  ///< 分隔线左沿到其右侧文本（稿面 1076 → 1090）。

[[nodiscard]] auto to_utf8(const std::u32string &text) -> std::string {
    std::string out;
    static_cast<void>(term::encode_utf8(text, out));
    return out;
}

/// @brief 一格的左附加物宽（圆点、分隔线可以叠加，今日没有同时出现的格，但算式只有一条）。
[[nodiscard]] auto prefix_of(const StatusBarSlot &slot) -> double {
    double prefix = 0.0;
    if (slot.leading_dot) {
        prefix += kDotSizeDp + kDotToTextDp;
    }
    if (slot.leading_separator) {
        prefix += kSeparatorWidthDp + kSeparatorToTextDp;
    }
    return prefix;
}

}  // namespace

StatusBar::StatusBar() {
    // 不可获焦、不可点（稿判据 ⑨／⑩ 只要求「读」）：一条常驻窗底的读数条若能吃掉点击，
    // 用户在那一格按下的一下就落进了 chrome 而不是任何 pane。
    set_focusable(false);
    width(au::fill());  // 横向填满根 Column（与横幅同一条：整条全宽）
}

auto StatusBar::set_state(StatusBarState state) -> void {
    if (state_ == state) {
        return;
    }
    state_ = std::move(state);
    // 高是常量，文本变化不改布局，只改宽度与颜色——故只标脏绘制。排布结果在下一帧绘制里重算，
    // `plan()` 读到的因此永远是「上一帧真的画出来的那一份」。
    mark_needs_paint();
}

auto StatusBar::on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size {
    (void)ctx;
    au::Size self = c.max.is_finite() ? c.max : au::Size{.width = 480.0F, .height = static_cast<float>(kHeightDp)};
    self.height = static_cast<float>(kHeightDp);
    return c.constrain(self);
}

auto StatusBar::on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void {
    anchor_box_ = au::Rect{};
    plan_ = StatusBarPlan{};
    if (bounds.size.height <= 0.0F || bounds.size.width <= 0.0F) {
        return;
    }
    const SettingsChrome chrome = settings_chrome();
    p.fill_rect(bounds, chrome.card_bg);
    // 条顶那 1 dp 分隔线是稿面画的（`codespec/UI_WORKSPACE.draft.svg` 里工作区与这条之间那道线），
    // 色取 `card_line` 而不是稿上的十六进制值：后者不在两档色板的 13 枚 token 里，而 `UI_CHROME.draft.md`
    // §1 是唯一色值源——为本件单开一枚「分隔线专用色」就是给色板加第 14 格（代价：暗档这条比稿面亮一档）。
    p.fill_rect(au::Rect{.origin = au::Point{.x = bounds.origin.x, .y = bounds.origin.y},
                         .size = au::Size{.width = bounds.size.width, .height = 1.0F}},
                chrome.card_line);

    const float scale = ctx.scale_factor > 0.0F ? ctx.scale_factor : 1.0F;
    const au::render::TextLayoutOpts opts{};
    const au::Font font{.size_pt = kTextPt};
    const double left = static_cast<double>(bounds.origin.x);
    const double top = static_cast<double>(bounds.origin.y);
    const double width_dp = static_cast<double>(bounds.size.width);
    const double text_y = top + (kHeightDp - kTextLineHeightDp) / 2.0;

    // ① 测宽：空文本的格不进测量表（N3 的省略序只数「真的有读数的格」，否则关掉一项就会把
    //    后一项挤出屏而用户在设置页看不出为什么）。`origins` 记住每一格来自 `state_.slots` 的第几格，
    //    绘制侧要读的形态旗标（点、分隔线、颜色档）在那一份里。
    std::vector<StatusBarSlotMeasure> measures;
    std::vector<std::string> texts;
    std::vector<std::size_t> origins;
    measures.reserve(state_.slots.size());
    texts.reserve(state_.slots.size());
    origins.reserve(state_.slots.size());
    for (std::size_t index = 0; index < state_.slots.size(); ++index) {
        const StatusBarSlot &slot = state_.slots[index];
        if (slot.text.empty()) {
            continue;
        }
        const std::string utf8 = to_utf8(slot.text);
        measures.push_back(StatusBarSlotMeasure{
            .id = slot.id,
            .text_width_dp = static_cast<double>(au::render::FontEngine::display_width(utf8, font, opts, scale)),
            .prefix_dp = prefix_of(slot),
        });
        texts.push_back(std::move(utf8));
        origins.push_back(index);
    }

    // ② 右端锚点先量好：它占的宽度连同自己左侧那一次格间距一起从房间里扣掉。
    double anchor_width = 0.0;
    std::string anchor_utf8;
    if (!state_.anchor.empty()) {
        anchor_utf8 = to_utf8(state_.anchor);
        anchor_width = static_cast<double>(au::render::FontEngine::display_width(anchor_utf8, font, opts, scale));
    }
    const StatusBarGeometry geometry{
        .left_inset_dp = kLeftInsetDp,
        .gap_dp = kGapDp,
        .right_inset_dp = kRightInsetDp,
        .reserved_right_dp = anchor_width > 0.0 ? anchor_width + kGapDp : 0.0,
    };

    // ③ 降级算式在纯件里（`status_bar_format`），本件只按它的结果落笔。
    plan_ = plan_status_bar(measures, width_dp, geometry);
    const bool show_anchor = !anchor_utf8.empty() && !plan_.right_anchor_dropped;

    auto x = left + kLeftInsetDp;
    for (std::size_t index = 0; index < plan_.shown.size(); ++index) {
        if (index > 0U) {
            x += kGapDp;
        }
        // `plan_status_bar()` 交出的 `shown` 是入参测量表的**前缀**（`utest_status_bar_format` 里
        // 「放不下的那格不许跳到前面」那一条把守），故下标可直接复用，不必按身份回找。
        const StatusBarSlot &slot = state_.slots[origins[index]];
        const au::Color ink = slot.tone == StatusBarTone::Dim         ? chrome.text_dim
                              : slot.tone == StatusBarTone::Warning ? chrome.warning
                                                                    : chrome.text_faint;

        if (slot.leading_separator) {
            p.fill_rect(au::Rect{.origin = au::Point{.x = static_cast<float>(x),
                                                     .y = static_cast<float>(top + (kHeightDp - kSeparatorHeightDp) /
                                                                                   2.0)},
                                 .size = au::Size{.width = static_cast<float>(kSeparatorWidthDp),
                                                  .height = static_cast<float>(kSeparatorHeightDp)}},
                        chrome.card_line);
            x += kSeparatorWidthDp + kSeparatorToTextDp;
        }
        if (slot.leading_dot) {
            const double dot_y = top + 8.0;  // 稿面逐值：点顶距条顶 8 dp（不是整条居中）
            const std::vector<au::Point> box = {
                au::Point{.x = static_cast<float>(x), .y = static_cast<float>(dot_y)},
                au::Point{.x = static_cast<float>(x + kDotSizeDp), .y = static_cast<float>(dot_y)},
                au::Point{.x = static_cast<float>(x + kDotSizeDp), .y = static_cast<float>(dot_y + kDotSizeDp)},
                au::Point{.x = static_cast<float>(x), .y = static_cast<float>(dot_y + kDotSizeDp)},
                au::Point{.x = static_cast<float>(x), .y = static_cast<float>(dot_y)},
            };
            p.stroke_polyline(box, kDotStrokeDp, chrome.success);
            x += kDotSizeDp + kDotToTextDp;
        }
        p.draw_text(au::Rect{.origin = au::Point{.x = static_cast<float>(x), .y = static_cast<float>(text_y)},
                             .size = au::Size{.width = static_cast<float>(measures[index].text_width_dp),
                                              .height = static_cast<float>(kTextLineHeightDp)}},
                    texts[index], font, ink);
        x += measures[index].text_width_dp;
    }

    if (!show_anchor) {
        return;
    }
    const double anchor_x = left + width_dp - kRightInsetDp - anchor_width;
    const au::Rect box{static_cast<float>(anchor_x), static_cast<float>(text_y),
                       static_cast<float>(anchor_width), static_cast<float>(kTextLineHeightDp)};
    p.draw_text(box, anchor_utf8, font, chrome.accent);
    anchor_box_ = box;
}

}  // namespace borealis::ui
