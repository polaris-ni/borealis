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

#include "connection_glyph.h"  // 类型图标的唯一字形表（与侧栏同一处，`SPEC.FEAT.WS.04`）
#include "settings_panel.h"    // settings_chrome（同 `connection_sidebar.cpp` 的先例：chrome 表的公共读点）

namespace borealis::ui {

namespace {

// 类型图标槽（`SPEC.FEAT.WS.04` 的 D4）：字形盒 12 dp + 与名称的间距 6 dp。占用它的三处（栏位宽、
// 名称原点、可截断宽）都从这两个常量取，故「左槽挪一格就要同改四处」这件事在本 TU 里不存在。
constexpr float kTypeSlotWidth = 12.0F;
constexpr float kTypeSlotGap = 6.0F;
constexpr float kTypeSlotTotal = kTypeSlotWidth + kTypeSlotGap;

// 状态角标簇（`SPEC.FEAT.WS.04` 三枚角标的落位，屏 1 稿面逐值；口径见裁决 7.183）：三枚都是
// r=3 实心圆点，最右一枚的圆心在栏位右端内缩 27 dp 处（稿 `cx = 右端 − 27`）、纵向落在带中线。
// 多枚并存时按「断线 → BEL → 活动」的优先级向左排，圆心距 10 dp（点径 6 + 间隙 4）——优先级决定
// 谁占稿那个锚位，而不是谁先点亮。
constexpr float kBadgeRadiusDp = 3.0F;
constexpr float kBadgeAnchorInsetDp = 27.0F;
constexpr float kBadgePitchDp = 10.0F;
/// @brief 最左一枚的左沿到名称右端的留白（用于把角标簇从名称的可截断宽里扣出去）。
constexpr float kBadgeTextGapDp = 5.0F;

constexpr float kPi = 3.14159265358979323846F;
constexpr float kFullCircle = 2.0F * kPi;

// 右端入口簇（屏 1 稿面：`＋ 新建` 与 `⌄ 最近关闭`，窗口 1500 dp 下的原始横坐标 1352..1484，
// 右缘内缩 16）。稿那两枚是文本 run 与折线，基线不可反推行盒顶 ⇒ 纵向通栏（与自绘标题栏那三枚
// 钮同一条口径，裁决 7.181①）。
constexpr float kClusterRightPadDp = 16.0F;   ///< 稿：文字右缘距栏右端 1500 − 1484。
constexpr float kClusterGapDp = 31.0F;        ///< 稿：`新建` 右缘 1394 → 箭头左沿 1425。
constexpr float kPlusSizeDp = 10.0F;          ///< 稿：两线交成的 `＋` 包络 1352..1362 × 50..60。
constexpr float kPlusToLabelGapDp = 8.0F;      ///< 稿：`＋` 右缘 1362 → 文字左沿 1370。
constexpr float kChevronWidthDp = 8.0F;       ///< 稿：`⌄` 折线 1425..1433。
constexpr float kChevronHeightDp = 4.0F;      ///< 稿：`⌄` 折线纵向 4 dp。
constexpr float kChevronToLabelGapDp = 5.0F;   ///< 稿：箭头右缘 1433 → 文字左沿 1438。
constexpr float kGlyphStrokeDp = 1.6F;         ///< 稿：`＋` 与 ✕ 同为 1.6 dp。
constexpr double kNewLabelFontPt = 12.0;       ///< 稿 `新建` font-size=12。
constexpr double kRecentLabelFontPt = 11.0;    ///< 稿 `最近关闭` font-size=11。

/// @brief 本格点亮了哪几枚状态角标（决定簇宽，见上面那组常量）。
///
/// 活动那枚沿用裁决 7.82① 的「只在未选中格显形」，故它是否占位与它是否落墨是同一句话——两处若分叉，
/// 名称的截断宽就会按一枚根本没画的角标让位。
[[nodiscard]] auto lit_badge_count(const TabVisual &tab) -> int {
    int count = 0;
    if (tab.exited) {
        ++count;
    }
    if (tab.bell_triggered) {
        ++count;
    }
    if (tab.has_activity && !tab.is_selected) {
        ++count;
    }
    return count;
}

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

auto TabBarWidget::set_action_labels(std::string new_tab_label, std::string recent_label)
    -> TabBarWidget & {
    new_tab_label_ = std::move(new_tab_label);
    recent_label_ = std::move(recent_label);
    mark_needs_paint();
    return *this;
}

auto TabBarWidget::right_cluster(const float width) const -> RightCluster {
    RightCluster out;
    if (width <= 0.0F) {
        return out;  // 空盒：绘制不落墨，命中测试也不吃点击（与「栏还没布局」同一档）
    }
    const float center_y = bar_height_ * 0.5F;
    const auto font_at = [](const double size_pt) -> au::Font {
        au::Font font;
        font.size_pt = static_cast<float>(size_pt);
        return font;
    };
    // 落笔盒：左沿给定、高度按实测、纵向居中在带中线上（稿那两枚是文本 run，基线不可反推行盒顶——
    // 与自绘标题栏那三枚钮同一条口径，裁决 7.181①）。
    const auto label_box = [center_y](const float left, const au::Font &font,
                                      const std::string &text) -> au::Rect {
        const float h = au::render::FontEngine::measure_height(font);
        const float w = text.empty() ? 0.0F : au::render::FontEngine::measure_width(text, font);
        return au::Rect{.origin = au::Point{.x = left, .y = center_y - h * 0.5F},
                        .size = au::Size{.width = w, .height = h}};
    };
    const au::Font recent_font = font_at(kRecentLabelFontPt);
    const au::Font new_font = font_at(kNewLabelFontPt);
    const auto width_of = [](const std::string &text, const au::Font &font) -> float {
        return text.empty() ? 0.0F : au::render::FontEngine::measure_width(text, font);
    };

    // 「⌄ 最近关闭」先右锚（稿：文字右缘 1484 ⇒ 距栏右端 16），箭头在文字之左、两者夹 5 dp。
    const float undo_right = width - kClusterRightPadDp;
    const float recent_w = width_of(recent_label_, recent_font);
    float undo_left = undo_right;
    if (hooks_.undo_close) {
        out.recent_label_box = label_box(undo_right - recent_w, recent_font, recent_label_);
        const float chevron_right = undo_right - recent_w - kChevronToLabelGapDp;
        out.chevron_center = au::Point{.x = chevron_right - kChevronWidthDp * 0.5F, .y = center_y};
        undo_left = chevron_right - kChevronWidthDp;
        out.undo_close_box = au::Rect{.origin = au::Point{.x = undo_left, .y = 0.0F},
                                      .size = au::Size{.width = undo_right - undo_left,
                                                       .height = bar_height_}};
    }
    // 「＋ 新建」再向左：字形 10 dp、与文字夹 8 dp，两簇之间留稿那个 31 dp。文字空时整枚只占字形宽
    //（末位标签那种窄栏的退化档就是这一支）。
    const float new_text_w = width_of(new_tab_label_, new_font);
    const float total = kPlusSizeDp + (new_text_w > 0.0F ? kPlusToLabelGapDp + new_text_w : 0.0F);
    const float new_left = undo_left - kClusterGapDp - total;
    out.plus_center = au::Point{.x = new_left + kPlusSizeDp * 0.5F, .y = center_y};
    if (new_text_w > 0.0F) {
        out.new_label_box = label_box(new_left + kPlusSizeDp + kPlusToLabelGapDp, new_font,
                                      new_tab_label_);
    }
    out.new_tab_box = au::Rect{.origin = au::Point{.x = new_left, .y = 0.0F},
                               .size = au::Size{.width = total, .height = bar_height_}};
    return out;
}

auto TabBarWidget::tab_width(const TabVisual &tab) const -> float {
    au::Font font;
    font.size_pt = 13.0F;
    const auto utf8_name = to_utf8(tab.display_name);
    const float text_width = au::render::FontEngine::measure_width(utf8_name, font);
    float w = text_width + 24.0F;  // 左右内边距各 12 dp
    if (tab.kind.has_value()) {
        w += kTypeSlotTotal;  // 名称左侧的类型槽：无类型即不占位（判据 ②）
    }
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
    // 外壳色随 chrome 明暗档现取（`SPEC.FEAT.PREF.01` 稿 D6）：每次重绘读一次，建树时不定色；仍**不**随
    // 终端主题联动（稿 D8 与裁决 7.52 的 S5① 同一条边界）。
    const SettingsChrome chrome = settings_chrome();

    p.fill_rect(bounds, chrome.card_bg);

    au::Font font;
    font.size_pt = 13.0F;

    float x = bounds.origin.x - scroll_offset_;
    for (std::size_t i = 0; i < cached_tabs_.size(); ++i) {
        const auto &tab = cached_tabs_[i];
        const float w = tab_width(tab);
        const au::Rect tab_box{
            .origin = au::Point{.x = x, .y = bounds.origin.y},
            .size = au::Size{.width = w, .height = bar_height_}};

        // 选中态背景 + 下划线
        if (tab.is_selected) {
            p.fill_rect(tab_box, chrome.control_bg);
            // 2 dp 下划线
            const au::Rect underline{
                .origin = au::Point{.x = tab_box.origin.x, .y = tab_box.origin.y + bar_height_ - 2.0F},
                .size = au::Size{.width = w, .height = 2.0F}};
            p.fill_rect(underline, chrome.accent);
        }

        // hover 态背景：选中底再压暗一档（`Color::shaded` 是框架对 hover/pressed 的统一派生法），两套色板
        // 同一条式子——亮档因此是比选中底略深的灰，暗档是栏底与选中底之间的一档。
        if (hovered_tab_id_ == tab.id && !tab.is_selected) {
            p.fill_rect(tab_box, chrome.control_bg.shaded(0.9F));
        }

        // 拖拽占位：正文色的低透明度盖层。跟着 `text` 走而不是写死白色——亮档正文近黑，白色盖层会看不见。
        if (drag_.has_value() && drag_->dragged_id == tab.id && drag_->is_dragging) {
            p.fill_rect(tab_box, au::Color{chrome.text.r, chrome.text.g, chrome.text.b, 64});
        }

        // 文本（截断到可用宽度）
        const float slot_width = tab.kind.has_value() ? kTypeSlotTotal : 0.0F;
        // 右留白（名称右端到栏位右端的距离）：关闭钮那一档是既有口径（12 + 命中区 20）；角标簇点亮时
        // 按簇宽让位，两者取大者——故「无角标」的每一格与本棒之前逐位相同（裁决 7.183 记的就是这条
        // 最小改动约束），而三枚同亮时最左一枚的左沿仍在名称右端之外 5 dp。
        const int badges = lit_badge_count(tab);
        const float badge_inset =
            badges > 0 ? kBadgeAnchorInsetDp + kBadgeRadiusDp + kBadgeTextGapDp +
                             kBadgePitchDp * static_cast<float>(badges - 1)
                       : 0.0F;
        const float text_inset =
            std::max(12.0F + (tab.has_close_button ? close_zone_ : 0.0F), badge_inset);
        const float text_max_width = std::max(0.0F, w - text_inset - 12.0F - slot_width);
        const auto display = truncate_name(tab.display_name, text_max_width, font);
        const au::Color text_color = tab.is_selected ? chrome.text : chrome.text_dim;
        const au::Rect text_box{
            .origin = au::Point{.x = tab_box.origin.x + 12.0F + slot_width, .y = tab_box.origin.y + 14.0F},
            .size = au::Size{.width = text_max_width, .height = bar_height_ - 28.0F}};
        const auto display_utf8 = to_utf8(display);

        // 类型图标（`SPEC.FEAT.WS.04`）：名称左侧一枚字形，色随名称（D7 的口径——状态三枚角标已占
        // 绿／红／琥珀三个色通道，类型只靠字形区分）。槽宽与名称原点同取 `slot_width`，故图标缺席时
        // 名称回到今日那个 `x+12`（判据 ② 的「逐位相同」就是这一句的屏幕形态）。
        if (tab.kind.has_value()) {
            const au::Rect glyph_box{
                .origin = au::Point{.x = tab_box.origin.x + 12.0F, .y = tab_box.origin.y + 14.0F},
                .size = au::Size{.width = kTypeSlotWidth, .height = bar_height_ - 28.0F}};
            p.draw_text(glyph_box, std::string{connection_type_glyph(*tab.kind)}, font, text_color);
        }
        p.draw_text(text_box, display_utf8, font, text_color);

        // 关闭钮（只在 hover 或选中时显形）
        if (tab.has_close_button && (tab.is_selected || hovered_tab_id_ == tab.id)) {
            const au::Rect close_box{
                .origin = au::Point{.x = tab_box.origin.x + w - close_zone_ + 4.0F,
                                    .y = tab_box.origin.y + 13.0F},
                .size = au::Size{.width = 12.0F, .height = 16.0F}};
            const au::Color close_color = hovered_tab_id_ == tab.id ? chrome.accent : chrome.text_dim;
            p.draw_text(close_box, "×", font, close_color);
        }

        // 状态角标簇（`SPEC.FEAT.WS.04`）：三枚 r=3 实心圆点，右锚、带中线、按优先级向左排。
        // 分工仍是形态之外的两档（裁决 7.91③）：BEL 与活动是「刚发生过什么」的事件位（活动只在未选中
        // 格显形，7.82①），退出是「现在是什么状态」，故选中格**也要**画——用户在一个已退出的格子里
        // 打字时正该看见它已退出。
        //
        // 三枚同形、只靠颜色与次序分档，是屏 1 稿面那一幅的兑现（稿画的就是三枚 r=3 圆点，Bell 那枚
        // 的铃形在稿内出现在另一格上、本棒按人裁的「圆点档」统一；登记在裁决 7.183）。颜色各占 13 枚
        // token 里的一格：断线＝`danger`、BEL＝`warning`、活动＝`success`。簇内次序即优先级，故两枚
        // 同格点亮时最占眼位的那一枚落在稿那个锚位上，而每一枚的**圆心横坐标**由它在簇里的位次唯一决定
        // ——这也是本套件几何证人取的读数（比的是位置与色道，不是外接面积：`fill_sector` 四条边各 1 px
        // 羽化，实心点数随羽化档浮动，把「约 7×7」写成等号就是在钉框架的抗锯齿实现）。
        const auto draw_badge = [&](int rank, const au::Color &ink) -> void {
            p.fill_sector(au::Point{.x = tab_box.origin.x + w - kBadgeAnchorInsetDp -
                                          kBadgePitchDp * static_cast<float>(rank),
                                    .y = tab_box.origin.y + bar_height_ * 0.5F},
                          kBadgeRadiusDp, 0.0F, 0.0F, kFullCircle, ink);
        };
        int badge_rank = 0;
        if (tab.exited) {
            draw_badge(badge_rank++, chrome.danger);
        }
        if (tab.bell_triggered) {
            draw_badge(badge_rank++, chrome.warning);
        }
        if (tab.has_activity && !tab.is_selected) {
            draw_badge(badge_rank++, chrome.success);
        }

        // 拖拽落点导引线
        if (drag_.has_value() && drag_->is_dragging && drag_->drop_index.has_value()) {
            // 简化：在被拖标签与目标位置之间画一条 2 dp 竖线
            // 实际实现需要更精确的落点计算，这里先占位
        }

        x += w;
    }

    // 右端两枚入口（屏 1 稿：`＋ 新建` 与 `⌄ 最近关闭`，固定右端、不参与溢出滚动）。
    // 字形按稿的折线画而不是按文本 run：稿那两枚是 `＋` 与 `⌄` 的线条包络，用字符画会随字体度量
    // 漂出稿位（自绘标题栏的三枚钮同一条口径，裁决 7.181①）。
    const RightCluster cluster = right_cluster(bounds.size.width);
    const auto draw_label = [&](const au::Rect &box, const double size_pt, const std::string &text,
                                const au::Color &ink) -> void {
        if (box.size.width <= 0.0F || text.empty()) {
            return;
        }
        au::Font font;
        font.size_pt = static_cast<float>(size_pt);
        p.draw_text(au::Rect{.origin = au::Point{.x = bounds.origin.x + box.origin.x,
                                                 .y = bounds.origin.y + box.origin.y},
                             .size = box.size},
                    text, font, ink);
    };
    const float plus_x = bounds.origin.x + cluster.plus_center.x;
    const float plus_y = bounds.origin.y + cluster.plus_center.y;
    p.draw_line(au::Point{.x = plus_x - kPlusSizeDp * 0.5F, .y = plus_y},
                au::Point{.x = plus_x + kPlusSizeDp * 0.5F, .y = plus_y}, kGlyphStrokeDp,
                chrome.text_dim);
    p.draw_line(au::Point{.x = plus_x, .y = plus_y - kPlusSizeDp * 0.5F},
                au::Point{.x = plus_x, .y = plus_y + kPlusSizeDp * 0.5F}, kGlyphStrokeDp,
                chrome.text_dim);
    draw_label(cluster.new_label_box, kNewLabelFontPt, new_tab_label_, chrome.text_dim);
    if (hooks_.undo_close) {
        const float chevron_x = bounds.origin.x + cluster.chevron_center.x;
        const float chevron_y = bounds.origin.y + cluster.chevron_center.y;
        p.stroke_polyline({au::Point{.x = chevron_x - kChevronWidthDp * 0.5F,
                                     .y = chevron_y - kChevronHeightDp * 0.5F},
                           au::Point{.x = chevron_x, .y = chevron_y + kChevronHeightDp * 0.5F},
                           au::Point{.x = chevron_x + kChevronWidthDp * 0.5F,
                                     .y = chevron_y - kChevronHeightDp * 0.5F}},
                          1.4F, chrome.text_dim);
        draw_label(cluster.recent_label_box, kRecentLabelFontPt, recent_label_, chrome.text_faint);
    }
}

auto TabBarWidget::on_pointer_event(au::MouseEvent &e) -> void {
    if (e.action == au::MouseAction::Press) {
        // 事实上报先于命中测试（稿 D7）：命中栏位、"＋"还是空白处都算「本窗被碰过一下」。
        if (hooks_.note_press) {
            hooks_.note_press();
        }
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
        // 右端两枚入口：命中盒由 `right_cluster` 与绘制同一枚算式给出，故「画在哪儿」与「点哪儿算」
        // 不可能分叉。空盒（栏没宽、或 `hooks_.undo_close` 不在位）一律不吃点击。
        const auto widget_size = size();
        const auto cluster = right_cluster(widget_size.width);
        const auto hits = [&](const au::Rect &box) -> bool {
            return box.size.width > 0.0F && box.contains(e.local_position);
        };
        if (hits(cluster.undo_close_box)) {
            if (hooks_.undo_close) {
                hooks_.undo_close();
            }
            e.is_handled = true;
            return;
        }
        if (hits(cluster.new_tab_box)) {
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
