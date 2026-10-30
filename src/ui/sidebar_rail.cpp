// ============================================================
// 侧栏折叠轨实现（src/ui/sidebar_rail.cpp）
// ------------------------------------------------------------
// 三枚图标的顶点逐条取自 `codespec/UI_WORKSPACE.draft.svg` 屏 1 那一条上的原始 path，
// 只有「每枚落在哪一格」改成了常数档：稿面那三枚的起笔 y（120／148／180）之间是 28 与 32
// 两种间距，与裁决 7.179④ 处理状态栏格距时同一判读——手绘起笔不构成可反推的格距口径，
// 故这里取单一 30 dp 槽距（中值吸附），逐值偏差 ≤2 dp 登记在 7.180。
// ============================================================

#include "sidebar_rail.h"

#include <vector>

#include "aurora/core/dimension.h"
#include "aurora/core/types.h"

#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

/// @brief 轨宽（屏 1 那条 `w=34`）。
constexpr double kRailWidthDp = 34.0;
constexpr double kHairlineWidthDp = 1.0;
constexpr double kSlotTopInsetDp = 6.0;   ///< 首格顶距轨顶（稿面首枚图标起笔于 16，见文件头那段吸附说明）。
constexpr double kSlotHeightDp = 30.0;    ///< 单格命中盒高＝槽距（稿两档间距 28／32 取中）。
constexpr double kGlyphBoxDp = 12.0;      ///< 每枚图标的外接方框（稿三枚的包络 10.5／10／12 取上界）。
constexpr double kStrokeDp = 1.4;         ///< 描边宽度（稿面逐枚都是 1.4）。

constexpr double kPi = 3.14159265358979323846;

}  // namespace

auto sidebar_rail_slot_order() -> const std::array<conn::SidebarViewMode, 3> & {
    static const std::array<conn::SidebarViewMode, 3> kOrder{
        conn::SidebarViewMode::All,
        conn::SidebarViewMode::Favorites,
        conn::SidebarViewMode::Groups,
    };
    return kOrder;
}

SidebarRail::SidebarRail(SidebarRailHooks hooks) : hooks_{std::move(hooks)} {
    // 不可获焦：与横幅、状态栏同一条纪律——常驻的标识列不该吃掉键盘，否则点一下轨之后
    // 用户敲的第一句就落进了 chrome 而不是任何 pane。
    set_focusable(false);
}

auto SidebarRail::set_active_view(conn::SidebarViewMode mode) -> void {
    if (active_ == mode) {
        return;
    }
    active_ = mode;
    // 宽与高都不变（34×窗高），变的只有三枚的亮暗 → 只标脏绘制，不重排。
    mark_needs_paint();
}

auto SidebarRail::slot_box(std::size_t index) const noexcept -> const au::Rect & {
    static const au::Rect kEmpty{};
    return index < slots_.size() ? slots_[index] : kEmpty;
}

auto SidebarRail::slot_at(double local_y) const noexcept -> std::size_t {
    const double offset = local_y - kSlotTopInsetDp;
    if (offset < 0.0) {
        return slots_.size();
    }
    const auto index = static_cast<std::size_t>(offset / kSlotHeightDp);
    return index < slots_.size() ? index : slots_.size();
}

auto SidebarRail::on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size {
    (void)ctx;
    // 宽是自报的常量，高跟随交叉轴（根 Row 的 Stretch 给整列高）——与横幅「常量只在一轴」同形。
    au::Size self{.width = static_cast<float>(kRailWidthDp),
                  .height = c.max.is_finite() ? c.max.height : 480.0F};
    return c.constrain(self);
}

auto SidebarRail::on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void {
    (void)ctx;
    slots_ = {};
    if (bounds.size.width <= 0.0F || bounds.size.height <= 0.0F) {
        return;
    }
    const SettingsChrome chrome = settings_chrome();
    p.fill_rect(bounds, chrome.card_bg);
    // 右沿那条 1 dp 线取 `card_line` 而不是稿上的 `#3A3C4C`——与状态栏条顶线同一条口径（7.179④c）：
    // 那个值不在两档 13 枚 token 里，为本件单开一枚就是给色板加第 14 格。
    p.fill_rect(au::Rect{.origin = au::Point{
                             .x = static_cast<float>(static_cast<double>(bounds.origin.x) +
                                                    kRailWidthDp - kHairlineWidthDp),
                             .y = bounds.origin.y},
                         .size = au::Size{.width = static_cast<float>(kHairlineWidthDp),
                                          .height = bounds.size.height}},
                chrome.card_line);

    const double left = static_cast<double>(bounds.origin.x);
    const double top = static_cast<double>(bounds.origin.y);
    const double center_x = left + kRailWidthDp / 2.0;
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        const double slot_top = top + kSlotTopInsetDp + static_cast<double>(index) * kSlotHeightDp;
        slots_[index] = au::Rect{.origin = au::Point{.x = bounds.origin.x,
                                                     .y = static_cast<float>(slot_top)},
                                 .size = au::Size{.width = static_cast<float>(kRailWidthDp),
                                                  .height = static_cast<float>(kSlotHeightDp)}};
        // 当前档那枚用 `text_dim`、其余用 `text_faint`：稿面就是「一枚亮两枚暗」（#8B93A7／#62728A），
        // 两枚都已在两档色板在册，故这一处与稿**逐值相等**（`text_faint` 正是 7.179⑤ 为 dim 补的第 13 枚）。
        const au::Color ink = sidebar_rail_slot_order()[index] == active_ ? chrome.text_dim
                                                                          : chrome.text_faint;
        const double glyph_y = slot_top + (kSlotHeightDp - kGlyphBoxDp) / 2.0 + kGlyphBoxDp / 2.0;
        switch (index) {
            case 0: {
                // 档案树（folder）：稿 `M 11 127 v -7 h 4 l 1.5 2 h 5 v 5 z`，10.5×7。
                const double x = center_x - 5.25;
                const double y = glyph_y - 3.5;
                const std::vector<au::Point> shape = {
                    au::Point{.x = static_cast<float>(x), .y = static_cast<float>(y + 7.0)},
                    au::Point{.x = static_cast<float>(x), .y = static_cast<float>(y)},
                    au::Point{.x = static_cast<float>(x + 4.0), .y = static_cast<float>(y)},
                    au::Point{.x = static_cast<float>(x + 5.5), .y = static_cast<float>(y + 2.0)},
                    au::Point{.x = static_cast<float>(x + 10.5), .y = static_cast<float>(y + 2.0)},
                    au::Point{.x = static_cast<float>(x + 10.5), .y = static_cast<float>(y + 7.0)},
                    au::Point{.x = static_cast<float>(x), .y = static_cast<float>(y + 7.0)},
                };
                p.stroke_polyline(shape, static_cast<float>(kStrokeDp), ink);
                break;
            }
            case 1: {
                // 收藏：稿 `M 21 153 a 5 5 0 1 1 -5 -5` 那段 270° 弧（心在 16,153、半径 5、缺右上象限）
                // 加 `M 16 151 v 2.5 h 2.5` 那一记折角。y 轴向下故角度递增即顺时针，[0, 3π/2) 正好是画的那段。
                p.stroke_arc(au::Point{.x = static_cast<float>(center_x), .y = static_cast<float>(glyph_y)},
                             5.0F, static_cast<float>(kStrokeDp), 0.0F,
                             static_cast<float>(3.0 * kPi / 2.0), ink);
                const std::vector<au::Point> tick = {
                    au::Point{.x = static_cast<float>(center_x), .y = static_cast<float>(glyph_y - 2.0)},
                    au::Point{.x = static_cast<float>(center_x), .y = static_cast<float>(glyph_y + 0.5)},
                    au::Point{.x = static_cast<float>(center_x + 2.5), .y = static_cast<float>(glyph_y + 0.5)},
                };
                p.stroke_polyline(tick, static_cast<float>(kStrokeDp), ink);
                break;
            }
            default: {
                // 按分组：稿 `M 16 180 a 3 3 0 1 0 0 6 a 3 3 0 1 0 0 -6`＝上下两枚 r=3 的整圆。
                for (const double dy : {-3.0, 3.0}) {
                    p.stroke_arc(
                        au::Point{.x = static_cast<float>(center_x), .y = static_cast<float>(glyph_y + dy)},
                        3.0F, static_cast<float>(kStrokeDp), 0.0F, static_cast<float>(2.0 * kPi), ink);
                }
                break;
            }
        }
    }
}

auto SidebarRail::on_pointer_event(au::MouseEvent &e) -> void {
    if (e.action != au::MouseAction::Press || e.button != au::MouseButton::Left) {
        return;
    }
    // 上报先于「命中哪一格」：点在两格之间也算碰过这一扇窗（与横幅 D7 同一条分账）。
    if (hooks_.note_press) {
        hooks_.note_press();
    }
    const std::size_t index = slot_at(static_cast<double>(e.local_position.y));
    if (index >= slots_.size() || !hooks_.select_view) {
        e.is_handled = true;  // 点的是轨本身：不该漏到轨后面的任何 pane。
        return;
    }
    hooks_.select_view(sidebar_rail_slot_order()[index]);
    e.is_handled = true;
}

auto SidebarRail::cursor_shape() const -> std::optional<au::CursorShape> {
    return au::CursorShape::PointingHand;
}

}  // namespace borealis::ui
