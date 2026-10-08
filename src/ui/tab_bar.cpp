// ============================================================
// 标签栏绘制侧控件实现（src/ui/tab_bar.cpp）
// ------------------------------------------------------------
// 第六个触达 au::Painter / au::Widget 的 TU（私有头形态同裁决 D1①）。
// 职责：把 TabStrip 的真值源折成可点击、可拖拽、可重命名的界面。
// 不持 TabStrip 指针，而是经 Hooks 交装配层兑现。
// ============================================================

#include "tab_bar.h"

#include <algorithm>
#include <cmath>

#include "aurora/core/color.h"
#include "aurora/render/font_engine.h"
#include "aurora/render/painter.h"
#include "borealis/term/utf8.h"

namespace borealis::ui {

namespace {

/// @brief u32string → UTF-8 string.
[[nodiscard]] auto to_utf8(const std::u32string &s) -> std::string {
    std::string out;
    term::encode_utf8(s, out);
    return out;
}

/// @brief 截断显示名并加省略号（单行，不超过 max_width_dp）。
[[nodiscard]] auto truncate_name(const std::u32string &name, float max_width_dp, const au::Font &font)
    -> std::u32string {
    if (name.empty()) {
        return name;
    }
    const auto utf8 = to_utf8(name);
    const float full_width = au::render::FontEngine::measure_width(utf8, font);
    if (full_width <= max_width_dp) {
        return name;
    }
    // 逐步截断直到能放下 + "…"
    const char32_t ellipsis[] = U"…";
    const auto ellipsis_utf8 = to_utf8(ellipsis);
    const float ellipsis_width = au::render::FontEngine::measure_width(ellipsis_utf8, font);
    const float available = max_width_dp - ellipsis_width;
    if (available <= 0.0F) {
        return ellipsis;
    }
    std::u32string truncated;
    for (char32_t cp : name) {
        truncated.push_back(cp);
        const auto w_str = to_utf8(truncated);
        const float w = au::render::FontEngine::measure_width(w_str, font);
        if (w > available) {
            truncated.pop_back();
            break;
        }
    }
    truncated.append(ellipsis);
    return truncated;
}

}  // namespace

TabBarWidget::TabBarWidget(TabBarHooks hooks) : hooks_{std::move(hooks)} {}

auto TabBarWidget::tab_width(const TabVisual &tab) const -> float {
    au::Font font;
    font.size_pt = 13.0F;
    const auto utf8_name = to_utf8(tab.display_name);
    const float text_width = au::render::FontEngine::measure_width(utf8_name, font);
    float w = text_width + 24.0F;  // 左右内边距各 12 dp
    if (tab.has_close_button) {
        w += close_zone_;
    }
    return std::clamp(w, tab_min_width_, tab_max_width_);
}

auto TabBarWidget::hit_test_at(float x_dp) const -> HitResult {
    HitResult result;
    float x = -scroll_offset_;
    for (const auto &tab : cached_tabs_) {
        const float w = tab_width(tab);
        if (x_dp >= x && x_dp < x + w) {
            result.tab_id = tab.id;
            // 关闭钮命中区：栏位右端 20 dp
            if (tab.has_close_button && x_dp > x + w - close_zone_) {
                result.on_close_button = true;
            }
            return result;
        }
        x += w;
    }
    return result;
}

auto TabBarWidget::compute_scroll_offset(float available_width) const -> float {
    if (cached_tabs_.empty()) {
        return 0.0F;
    }
    // 计算总宽
    float total_width = 0.0F;
    for (const auto &tab : cached_tabs_) {
        total_width += tab_width(tab);
    }
    if (total_width <= available_width) {
        return 0.0F;
    }
    // 确保选中标签可见
    float x = 0.0F;
    for (const auto &tab : cached_tabs_) {
        if (tab.is_selected) {
            const float w = tab_width(tab);
            // 如果选中标签在可视区左边之外
            if (x - scroll_offset_ < 0.0F) {
                return x;
            }
            // 如果选中标签在可视区右边之外
            if (x + w - scroll_offset_ > available_width) {
                return x + w - available_width;
            }
            // 已在可视区内，保持当前偏移
            return scroll_offset_;
        }
        x += tab_width(tab);
    }
    return scroll_offset_;
}

auto TabBarWidget::on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size {
    // 取最新标签列表
    if (hooks_.tabs) {
        cached_tabs_ = hooks_.tabs();
    }
    // 栏高固定 42 dp，宽度填满父约束
    au::Size self = c.max;
    if (!c.max.is_finite()) {
        self = au::Size{.width = 480.0F, .height = bar_height_};
    } else {
        self.height = bar_height_;
    }
    // 计算溢出偏移
    scroll_offset_ = compute_scroll_offset(self.width);
    return c.constrain(self);
}

auto TabBarWidget::on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void {
    // 标签栏背景（chrome 色，不随主题）
    const au::Color bar_bg{33, 34, 44, 255};  // #21222C
    p.fill_rect(bounds, bar_bg);

    au::Font font;
    font.size_pt = 13.0F;

    const au::Color accent{98, 114, 164, 255};     // dracula cyan-ish
    const au::Color text_unselected{139, 141, 152, 255};  // gray
    const au::Color text_selected{248, 248, 242, 255};    // white

    float x = bounds.origin.x - scroll_offset_;
    for (std::size_t i = 0; i < cached_tabs_.size(); ++i) {
        const auto &tab = cached_tabs_[i];
        const float w = tab_width(tab);
        const au::Rect tab_box{
            .origin = au::Point{.x = x, .y = bounds.origin.y},
            .size = au::Size{.width = w, .height = bar_height_}};

        // 选中态背景 + 下划线
        if (tab.is_selected) {
            const au::Color selected_bg{40, 42, 54, 255};  // #282A36
            p.fill_rect(tab_box, selected_bg);
            // 2 dp 下划线
            const au::Rect underline{
                .origin = au::Point{.x = tab_box.origin.x, .y = tab_box.origin.y + bar_height_ - 2.0F},
                .size = au::Size{.width = w, .height = 2.0F}};
            p.fill_rect(underline, accent);
        }

        // hover 态背景
        if (hovered_tab_id_ == tab.id && !tab.is_selected) {
            const au::Color hover_bg{68, 71, 90, 255};  // #44475A
            p.fill_rect(tab_box, hover_bg);
        }

        // 拖拽占位：半透明
        if (drag_.has_value() && drag_->dragged_id == tab.id && drag_->is_dragging) {
            const au::Color drag_overlay{255, 255, 255, 64};
            p.fill_rect(tab_box, drag_overlay);
        }

        // 文本（截断到可用宽度）
        const float text_max_width = w - 24.0F - (tab.has_close_button ? close_zone_ : 0.0F);
        const auto display = truncate_name(tab.display_name, text_max_width, font);
        const au::Color text_color = tab.is_selected ? text_selected : text_unselected;
        const au::Rect text_box{
            .origin = au::Point{.x = tab_box.origin.x + 12.0F, .y = tab_box.origin.y + 14.0F},
            .size = au::Size{.width = text_max_width, .height = bar_height_ - 28.0F}};
        const auto display_utf8 = to_utf8(display);
        p.draw_text(text_box, display_utf8, font, text_color);

        // 关闭钮（只在 hover 或选中时显形）
        if (tab.has_close_button && (tab.is_selected || hovered_tab_id_ == tab.id)) {
            const au::Rect close_box{
                .origin = au::Point{.x = tab_box.origin.x + w - close_zone_ + 4.0F,
                                    .y = tab_box.origin.y + 13.0F},
                .size = au::Size{.width = 12.0F, .height = 16.0F}};
            const au::Color close_color = hovered_tab_id_ == tab.id ? accent : text_unselected;
            p.draw_text(close_box, "×", font, close_color);
        }

        // BEL 角标（铃铛图标，`SPEC.FEAT.WS.04`）
        if (tab.bell_triggered) {
            const au::Color bell_color{255, 85, 85, 255};  // 红色警示色
            const auto bell_pos = au::Point{
                .x = tab_box.origin.x + w - 6.0F,
                .y = tab_box.origin.y + 6.0F};
            const au::Rect bell_dot{
                .origin = bell_pos,
                .size = au::Size{.width = 6.0F, .height = 6.0F}};
            p.fill_rect(bell_dot, bell_color);
        }

        // 活动高亮指示器（小圆点在标签左上角，`SPEC.FEAT.WS.04`）
        if (tab.has_activity && !tab.is_selected) {
            const au::Color activity_color{80, 250, 123, 255};  // dracula green
            const auto activity_pos = au::Point{
                .x = tab_box.origin.x + 4.0F,
                .y = tab_box.origin.y + 4.0F};
            const au::Rect activity_dot{
                .origin = activity_pos,
                .size = au::Size{.width = 4.0F, .height = 4.0F}};
            p.fill_rect(activity_dot, activity_color);
        }

        // 断线/退出角标（`SPEC.FEAT.WS.04`）：右下角一枚**空心**方框。
        // 与上面两枚的分工是形态上的而不是颜色上的：BEL 与活动是「刚发生过什么」的事件位（且活动只在
        // 未选中格显形，裁决 7.82①），退出是「现在是什么状态」，故选中格**也要**画——用户在一个已退出
        // 的格子里打字时正该看见它已退出。空心描边 + 落在右下，与右上角的实心铃铛在位置和形状两档都
        // 不撞，两枚同格出现时各自可判。
        if (tab.exited) {
            const au::Color exit_color{255, 184, 108, 255};  // amber
            const au::Rect exit_ring{
                .origin = au::Point{.x = tab_box.origin.x + w - 8.0F,
                                    .y = tab_box.origin.y + bar_height_ - 12.0F},
                .size = au::Size{.width = 6.0F, .height = 6.0F}};
            p.draw_rect(exit_ring, exit_color);
        }

        // 拖拽落点导引线
        if (drag_.has_value() && drag_->is_dragging && drag_->drop_index.has_value()) {
            // 简化：在被拖标签与目标位置之间画一条 2 dp 竖线
            // 实际实现需要更精确的落点计算，这里先占位
        }

        x += w;
    }

    // "＋"按钮（固定右端）
    const au::Rect add_button{
        .origin = au::Point{.x = bounds.origin.x + bounds.size.width - 32.0F,
                            .y = bounds.origin.y + 13.0F},
        .size = au::Size{.width = 16.0F, .height = 16.0F}};
    p.draw_text(add_button, "+", font, text_unselected);
}

auto TabBarWidget::on_pointer_event(au::MouseEvent &e) -> void {
    if (e.action == au::MouseAction::Press) {
        const auto hit = hit_test_at(e.local_position.x);
        if (hit.tab_id.has_value()) {
            if (hit.on_close_button) {
                // 关闭标签
                if (hooks_.close) {
                    hooks_.close(*hit.tab_id);
                }
            } else {
                // 开始拖拽或选中
                drag_ = DragState{
                    .dragged_id = *hit.tab_id,
                    .press_x = e.local_position.x,
                    .current_x = e.local_position.x,
                    .is_dragging = false,
                };
                // 选中该标签
                if (hooks_.select) {
                    hooks_.select(*hit.tab_id);
                }
            }
            e.is_handled = true;
            return;
        }
        // 点击"＋"按钮
        const auto widget_size = size();
        if (e.local_position.x > widget_size.width - 32.0F) {
            if (hooks_.add_new) {
                hooks_.add_new();
            }
            e.is_handled = true;
            return;
        }
    } else if (e.action == au::MouseAction::Move && drag_.has_value()) {
        drag_->current_x = e.local_position.x;
        // 超过阈值即进入拖拽态
        if (!drag_->is_dragging && std::abs(drag_->current_x - drag_->press_x) > drag_threshold_) {
            drag_->is_dragging = true;
            mark_needs_paint();
        }
        if (drag_->is_dragging) {
            // TODO: 计算落点下标
            mark_needs_paint();
            e.is_handled = true;
            return;
        }
    } else if (e.action == au::MouseAction::Release && drag_.has_value()) {
        if (drag_->is_dragging && drag_->drop_index.has_value() && hooks_.move) {
            hooks_.move(drag_->dragged_id, *drag_->drop_index);
        }
        drag_.reset();
        mark_needs_paint();
        e.is_handled = true;
        return;
    }

    // hover 态
    if (e.action == au::MouseAction::Move) {
        const auto hit = hit_test_at(e.local_position.x);
        if (hit.tab_id != hovered_tab_id_) {
            hovered_tab_id_ = hit.tab_id;
            mark_needs_paint();
        }
    }

    au::Widget::on_pointer_event(e);
}

}  // namespace borealis::ui
