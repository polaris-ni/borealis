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

// 卡片几何（屏 1 稿 `codespec/UI_WORKSPACE.draft.svg` 标签条那一行，与 `UI_WORKSPACE_INTERACT.draft.md`
// §1 的「栏高 / 格高 / 圆角 = 42 / 38 / 6 dp」同值；稿的 `<rect x=8 y=40 w=176 h=38 rx=6>` 折成 dp 就是
// 下面四枚）：格高 38 落在 42 的栏里 ⇒ **顶内缩 4、底贴齐**；整条栏的首格左内缩 8、相邻零间距；活动格的
// 2 dp `accent` 画在**格顶**且左右各内缩 8（稿 `line x1=16 y1=41 x2=176`，即卡左右各让开一个圆角）。
//
// 两处稿内自相矛盾按不变量取，登记在裁决 7.187：① 交互稿判据 ⑭ 写的是「2 dp accent **下划线**」，而卡
// 底贴齐栏底且四角都圆 ⇒ 通宽下划线会在两个圆角外溢出墨，稿那个「内缩 8」正是避开圆角的那一档，故取
// 卡顶；② 稿的栏底 `#191A21` 是 §2.1 表里的 `input`（用途只列输入框／键位胶囊／终端格底），同一张表把
// 「标签条底」点名给了 `chrome` ⇒ 颜色仍走 `card_bg`，本棒不动。
constexpr float kCardHeightDp = 38.0F;
constexpr float kCardRadiusDp = 6.0F;
constexpr float kStripLeftInsetDp = 8.0F;
constexpr float kAccentInsetDp = 8.0F;
constexpr float kAccentThicknessDp = 2.0F;
/// @brief 卡片左内缩（稿：卡左沿 8 → 类型图标左沿 20；无类型时名称仍从这一档起笔）。
///        名称区算式的两项都取它（左内缩与右留白的对称位），故它不是散在 `on_paint` 里的那个 `12.0F`。
constexpr float kCardTextLeftInsetDp = 12.0F;

// 拖拽落点导引线（交互稿判据 ⑯「两格之间画 2 dp `accent` 落点导引线」）：粗细与色道都与选中格那枚
// 卡顶 accent 同源，故本件里「accent 色」这一条证人只能是这一笔或卡顶那一笔，不会有第三种。纵向取
// 卡片同带（38 dp 顶内缩、底贴齐）而**不通栏**：导引线说的是「落进哪一格的位置」，画到栏外就等于给
// 栏加一条稿没有的分界（裁决 7.190）。
constexpr float kGuideThicknessDp = 2.0F;

// 关闭钮那枚 ✕（屏 1 余项 (b)，裁决 7.191）：稿是两枚 1.6 dp 折线交成的 8×8 包络（首格
// `line x1=167 y1=53 x2=175 y2=61` 与其镜像），中心距栏位右缘 184 − 171 = 13 dp、纵向 cy=57 正落在
// 36..78 那条 42 dp 带中线上（卡片只在顶部内缩，故带中线比卡中线高 2 dp）。此前这里画的是 `×`
// **字形**，实测中心距右缘 10 dp——字形墨随字体度量漂，落不到稿那一档；右端 `＋` 与 `⌄` 早已改成折线
// （裁决 7.181①），本笔把同一口径补到这枚钮上。
constexpr float kCloseGlyphSizeDp = 8.0F;     ///< 稿：两线交成的 ✕ 包络 167..175 × 53..61。
constexpr float kCloseCenterInsetDp = 13.0F;  ///< 稿：栏位右缘 184 → 字形中心 171。

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
    // 返回值是「被替换的码点数」，显示名这一路不上报它（与 `src/main.cpp` 那三处同一条口径，裁决 7.192）。
    static_cast<void>(term::encode_utf8(s, out));
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

auto TabBarWidget::strip_right_edge(const RightCluster &cluster, const float width) -> float {
    // 界外那一截不属于任何一格。取簇左沿而不折新区间：稿没给「末格 → 入口簇」那一档间隙，自造一枚
    // 就等于给本件加一条稿上没有的口径（裁决 7.188）。
    // 不用 `std::clamp`：`width ≤ 0`（栏还没布局）时它的上下界会反序，那是未定义行为。
    return std::min(std::max(0.0F, cluster.new_tab_box.origin.x), width);
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

auto TabBarWidget::hit_test_at(const float x_dp, const float right_edge) const -> HitResult {
    HitResult result;
    // 右端入口簇那一截先让掉：溢出时末格一路铺到入口之下，若这里不判界，`＋ 新建` 的点击就被一格
    // 抢走（绘制腿在同一条界上裁剪，故「看不见」与「点不着」不可能分叉，裁决 7.188）。
    if (x_dp >= right_edge) {
        return result;
    }
    // 左端同理，但**必须显式判**：下面的走位起点是「左内缩 − 溢出偏移」，偏移非 0 时首格的盒一路挪进
    // 0..8 dp 那一档飞地——不判界就成了「点得着却看不见」（与 `on_paint` 的裁剪同源，裁决 7.193；
    // 未溢出时走位起点本就是 8，这一句只是把那条口径推到溢出态）。
    if (x_dp < kStripLeftInsetDp) {
        return result;
    }
    // 起点与绘制同源（首格左内缩那 8 dp 不是任何一格的落区）。
    float x = kStripLeftInsetDp - scroll_offset_;
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

auto TabBarWidget::strip_slots(const std::optional<std::uint64_t> &skip_id) const -> std::vector<Slot> {
    std::vector<Slot> out;
    out.reserve(cached_tabs_.size());
    // 起点与 `hit_test_at`、`on_paint` 同取「左内缩 − 溢出偏移」：三处各持一份走位算式，就会有某一处
    // 忘了扣偏移，于是溢出态下的导引线、命中、落笔当场分叉（裁决 7.190）。
    float x = kStripLeftInsetDp - scroll_offset_;
    for (const auto &tab : cached_tabs_) {
        const float w = tab_width(tab);
        // 剔格只剔那**一格**的落位，不走位照旧扣它的宽：被拖那格在原处仍占着（半透明占位），其余格
        // 不因它被拖走而挪位——这正是「基准是其余标签」在屏幕上的形态。
        if (!skip_id.has_value() || skip_id.value() != tab.id) {
            out.push_back(Slot{.id = tab.id, .x0 = x, .x1 = x + w});
        }
        x += w;
    }
    return out;
}

auto TabBarWidget::name_region(const TabVisual &tab, const float w) const -> NameRegion {
    NameRegion out;
    out.slot_width = tab.kind.has_value() ? kTypeSlotTotal : 0.0F;
    out.left_inset = kCardTextLeftInsetDp + out.slot_width;
    // 右留白（名称右端到栏位右端的距离）：关闭钮那一档是既有口径（12 + 命中区 20）；角标簇点亮时
    // 按簇宽让位，两者取大者——故「无角标」的每一格与本棒之前逐位相同（裁决 7.183 记的就是这条
    // 最小改动约束），而三枚同亮时最左一枚的左沿仍在名称右端之外 5 dp。
    const int badges = lit_badge_count(tab);
    const float badge_inset =
        badges > 0 ? kBadgeAnchorInsetDp + kBadgeRadiusDp + kBadgeTextGapDp +
                         kBadgePitchDp * static_cast<float>(badges - 1)
                   : 0.0F;
    const float text_inset =
        std::max(kCardTextLeftInsetDp + (tab.has_close_button ? close_zone_ : 0.0F), badge_inset);
    out.width = std::max(0.0F, w - text_inset - kCardTextLeftInsetDp - out.slot_width);
    return out;
}

auto TabBarWidget::name_span(const std::uint64_t id) const -> std::optional<std::pair<float, float>> {
    // 落位取 `strip_slots`（与绘制、命中、导引线同一条走位算式），故溢出态里双击的是**屏幕上**那一名
    // 字的这一截，而不是表序里第 n 格原本的位置（裁决 7.190 那条同源性在这里继续成立）。
    const std::vector<Slot> slots = strip_slots(std::nullopt);
    for (std::size_t i = 0; i < cached_tabs_.size() && i < slots.size(); ++i) {
        if (cached_tabs_[i].id != id) {
            continue;
        }
        const NameRegion region = name_region(cached_tabs_[i], slots[i].x1 - slots[i].x0);
        // 零宽即「这一格没有可供编辑的那一截」：不弹胶囊，也不认领这次双击（与 `rename_target()` 同一判）。
        if (region.width <= 0.0F) {
            return std::nullopt;
        }
        const float x0 = slots[i].x0 + region.left_inset;
        return std::pair{x0, x0 + region.width};
    }
    return std::nullopt;
}

auto TabBarWidget::rename_target(const std::uint64_t id) const -> std::optional<RenameTarget> {
    const std::optional<std::pair<float, float>> span = name_span(id);
    const std::optional<au::Rect> bar_box = window_bounds();
    if (!span.has_value() || !bar_box.has_value()) {
        return std::nullopt;  // 栏未布局（首帧之前）或那一格已经不在表里
    }
    for (const auto &tab : cached_tabs_) {
        if (tab.id == id) {
            return RenameTarget{
                // 横向＝名称区（`span` 是本件本地 dp，加栏的窗口原点即窗口 dp）；纵向＝卡顶 + 稿那 8 dp
                // 顶内缩，而卡顶的折法与 `on_paint` 同一条（栏高减格高）。
                .box = au::Rect{.origin =
                                     au::Point{.x = bar_box->origin.x + span->first,
                                               .y = bar_box->origin.y + bar_height_ - kCardHeightDp +
                                                    kRenamePillTopInsetDp},
                                 .size = au::Size{.width = span->second - span->first,
                                                  .height = kRenamePillHeightDp}},
                .current_name = tab.display_name,
            };
        }
    }
    return std::nullopt;
}

auto TabBarWidget::resolve_drop(const float center_x, const float right_edge,
                                const std::uint64_t dragged_id) const -> std::optional<DropTarget> {
    const std::vector<Slot> rest = strip_slots(dragged_id);
    // 只剩被拖那一格（单标签窗）⇒ 没有「其余标签」，也就没有落点可言：导引线不画，`move` 不呼。
    if (rest.empty()) {
        return std::nullopt;
    }
    // 中心先钳进右界：界外那一截不属于标签条（判据 21「固定右端、不参与溢出滚动」），拖过界时的落点
    // 就是表尾，而那一笔导引线不能画到入口簇的墨迹上。与 `strip_right_edge` 同用 min/max 而不用
    // `std::clamp`（那里登记的理由：上下界反序是未定义行为）。
    //
    // 下界**不**跟着抬到左内缩（裁决 7.193）：这里的读法是「指针落在栏的留白＝落在表头」。抬到 8 之后，
    // 一枚中心还留在 0..8 之间的「其余标签」会被算进「中心在左」，同一档指针读出下标 1、缝落到它右沿——
    // 导引线指哪儿、松手却落哪儿，判据 ⑯ 那条同源性当场断。这一档分叉要的是「某格的中心恰落在留白里」，
    // 本套件的六格等宽（偏移 14）与七格（偏移 110）两档都折不出它，故没有证人（等价变异 M5 登记在裁决里）。
    const float clamped = std::min(std::max(0.0F, center_x), right_edge);
    // 目标下标＝中心在左侧的那些「其余标签」的个数，读作「插在第 index 格之前」，取值 `[0, count-1]`
    // 恰与 `TabStrip::move` 的合法域同档（7.43④ 那句「指针落在两格之间时算出的插入位天然不含被拖那格」）。
    std::size_t index = 0U;
    for (const auto &slot : rest) {
        if ((slot.x0 + slot.x1) * 0.5F < clamped) {
            ++index;
        }
    }
    // 那条缝：插在第 index 格之前就是它的左沿，落到表尾则是最后一格的右沿。下标与缝同出一枚算式，
    // 故「导引线指向哪条缝」与「松手落到哪条缝」不是两条口径（判据 ⑯）。
    const float seam = index < rest.size() ? rest[index].x0 : rest.back().x1;
    // 那一笔导引线的中心取钳回内侧的缝：粗 `kGuideThicknessDp`、以中心对称，故左界比内缩再多让半粗——
    // 层裁剪的左沿是**含端**的（右沿那一档由框架的取整口径让出整整一列，7.190 已登记），中心若只钳到
    // 内缩本身，两列里靠左那一列会被裁掉，判据 ⑯ 那条「2 dp」在溢出态里就只剩 1 dp（裁决 7.193）。
    return DropTarget{
        .index = index,
        .guide_x = std::min(std::max(kStripLeftInsetDp + kGuideThicknessDp * 0.5F, seam), right_edge)};
}

auto TabBarWidget::compute_scroll_offset(float available_width) const -> float {
    if (cached_tabs_.empty()) {
        return 0.0F;
    }
    // 可用宽两头都要扣：首格那 8 dp 左内缩是栏的一部分但不是任何一格的地盘，右端入口簇那一截则根本
    // 不属于标签条（判据 21 那句「固定右端、不参与溢出滚动」在这里兑现成偏移，裁决 7.188）。
    const float strip_edge = strip_right_edge(right_cluster(available_width), available_width);
    const float usable = std::max(0.0F, strip_edge - kStripLeftInsetDp);
    // 计算总宽
    float total_width = 0.0F;
    for (const auto &tab : cached_tabs_) {
        total_width += tab_width(tab);
    }
    if (total_width <= usable) {
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
            if (x + w - scroll_offset_ > usable) {
                return x + w - usable;
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

    // 右端两枚入口的落笔盒先折出来：它的左沿就是标签层的右界（与 `hit_test_at`、`compute_scroll_offset`
    // 同一枚算式）。溢出时末格会一路铺到入口之下，故这里把标签层裁在界内——判据 21 那句「固定右端、
    // 不参与溢出滚动」的屏幕形态就是这一句（裁决 7.188）。
    const RightCluster cluster = right_cluster(bounds.size.width);
    const float strip_edge = strip_right_edge(cluster, bounds.size.width);
    // 只在界确实落在栏内时才压裁剪：界归零是栏窄到装不下入口簇的退化档，那一档入口簇本就越过标签条右界，
    // 而它落笔在标签之后（画在上头），空裁剪盒只会把标签整层抹掉。
    //
    // 左沿一并裁在**左内缩**那一档（裁决 7.193）：溢出偏移非 0 时首格的盒会一路挪进 0..8 dp，画从 0 起
    // 就成了「一格里铺进飞地」——那 8 dp 是栏的留白、不属于任何一格，`hit_test_at` 与偏移算式都这么认，
    // 绘制此前只裁右界（7.189 登记的余项 (h) 正是这一处镜像缺口）。
    //
    // `strip_edge` 是**栏相对**的（`right_cluster` 与 `strip_right_edge` 都按 `bounds.size.width` 折），
    // 故它与 `kStripLeftInsetDp` 都得先加上 `bounds.origin.x` 才能同一条尺比——嵌入式布局下（栏左边有
    // 分屏把手那类占位）直接把相对量当绝对坐标读，裁剪盒会短掉一整截 origin.x。
    const float clip_left = bounds.origin.x + kStripLeftInsetDp;
    const float clip_right = bounds.origin.x + std::min(strip_edge, bounds.size.width);
    const bool clip_strip = clip_right > clip_left;
    if (clip_strip) {
        p.push_clip(au::Rect{.origin = au::Point{.x = clip_left, .y = bounds.origin.y},
                             .size = au::Size{.width = clip_right - clip_left,
                                              .height = bar_height_}});
    }

    // 卡片纵向**底贴齐、顶内缩**（内缩由栏高减格高折出，两处不可能分叉成「卡浮在栏中间」）。
    // 内容仍按**栏**中线落笔、不随卡片下移：稿的 ✕（y 53..61）与三枚角标（cy=57）都在栏中线 21 dp 上，
    // 而卡片中线是 23 dp——本棒只动卡的那层底与描边，一格文字都不挪（挪动的 2 dp 会牵动本套件全部
    // 像素证人，而稿自己这两档本就差 2 dp，裁决 7.187 登记这一条为「内容纵向不变」）。
    const float card_top = bounds.origin.y + bar_height_ - kCardHeightDp;
    const float content_top = bounds.origin.y + 14.0F;
    const float content_mid = bounds.origin.y + bar_height_ * 0.5F;
    // 每格的落位从 `strip_slots` 取——与 `hit_test_at`、`resolve_drop` 同一条走位算式，故「画在哪儿」「点
    // 哪儿算」「导引线落在哪条缝」三件事在溢出态里也不可能各自为政（裁决 7.190）。
    const std::vector<Slot> slots = strip_slots(std::nullopt);
    // 被拖那格的下标；取「表尾之外」这一档哨兵表示没有拖拽（下标域是 `[0, size-1]`，故不会撞车）。
    std::size_t dragged_at = cached_tabs_.size();
    if (drag_.has_value() && drag_->is_dragging) {
        for (std::size_t i = 0; i < cached_tabs_.size(); ++i) {
            if (cached_tabs_[i].id == drag_->dragged_id) {
                dragged_at = i;
                break;
            }
        }
    }
    const bool is_dragging = dragged_at < cached_tabs_.size();
    // 落笔序＝表序、被拖那格殿后：`Painter` 没有 z 序概念，先落的那格会被后落那格的文字盖住一笔，而跟手
    // 那格恰恰会压在它的右邻身上（判据 ⑯「被拖格跟手」读作「它在这一刻浮在最上」）。
    std::vector<std::size_t> order;
    order.reserve(cached_tabs_.size());
    for (std::size_t i = 0; i < cached_tabs_.size(); ++i) {
        if (i != dragged_at) {
            order.push_back(i);
        }
    }
    if (is_dragging) {
        order.push_back(dragged_at);
    }
    // 拖拽占位（判据 ⑯「原格留半透明占位」）：正文色的低透明度盖层，跟着 `text` 走而不是写死白色——亮档
    // 正文近黑，白色盖层会看不见。落位是**原格**，画在所有卡片**之前**：刚过阈值那档跟手格与原格还重叠
    // 着，占位若后落就会把那半张跟手卡片染上一档正文色。
    if (is_dragging) {
        const float w = tab_width(cached_tabs_[dragged_at]);
        p.fill_rounded_rect(au::Rect{.origin = au::Point{.x = bounds.origin.x + slots[dragged_at].x0,
                                                         .y = card_top},
                                     .size = au::Size{.width = w, .height = kCardHeightDp}},
                            kCardRadiusDp,
                            au::Color{chrome.text.r, chrome.text.g, chrome.text.b, 64});
    }
    for (const std::size_t i : order) {
        const auto &tab = cached_tabs_[i];
        const float w = tab_width(tab);
        const bool dragged = (i == dragged_at);
        // 跟手位＝当前指针减「按下点相对左沿的那截偏移」：不按格中心对齐，否则一过 4 dp 阈值那格就当场
        // 跳 `grab_dx − 半格宽`，而稿与判据说的都是「跟手」。
        const float card_x =
            bounds.origin.x + (dragged ? drag_->current_x - drag_->grab_dx : slots[i].x0);
        const au::Rect card_box{
            .origin = au::Point{.x = card_x, .y = card_top},
            .size = au::Size{.width = w, .height = kCardHeightDp}};

        // 选中态：圆角卡片底 + 卡顶 2 dp accent（左右各内缩 8，让开两个圆角）。
        if (tab.is_selected) {
            p.fill_rounded_rect(card_box, kCardRadiusDp, chrome.control_bg);
            const au::Rect accent{
                .origin = au::Point{.x = card_box.origin.x + kAccentInsetDp, .y = card_top},
                .size = au::Size{.width = std::max(0.0F, w - 2.0F * kAccentInsetDp),
                                 .height = kAccentThicknessDp}};
            p.fill_rect(accent, chrome.accent);
        }

        // hover 态背景：选中底再压暗一档（`Color::shaded` 是框架对 hover/pressed 的统一派生法），两套色板
        // 同一条式子——亮档因此是比选中底略深的灰，暗档是栏底与选中底之间的一档。
        // 稿的 hover 档写的是 `sel`（§2.1 表里那枚「代码侧尚无消费者」的格），本件按既有派生法而不为
        // 一枚 hover 新增第 14 枚 token；差额登记在裁决 7.187 的欠项里。
        if (hovered_tab_id_ == tab.id && !tab.is_selected) {
            p.fill_rounded_rect(card_box, kCardRadiusDp, chrome.control_bg.shaded(0.9F));
        }

        // 文本（截断到可用宽度）：左内缩与可用宽同出 `name_region`——「名称落在哪一截」和「双击命中哪一截」
        // 「胶囊盒交出去的是哪一截」必须是同一个数（裁决 7.195），各写一份迟早会在溢出态里错开半格。
        // 右留白的两档口径（关闭钮 12 + 命中区 / 角标簇取大者）与「无角标逐位相同」这条最小改动约束
        // 都在那枚函数里（裁决 7.183 登记的原句随之搬过去）。
        const NameRegion region = name_region(tab, w);
        const auto display = truncate_name(tab.display_name, region.width, font);
        const au::Color text_color = tab.is_selected ? chrome.text : chrome.text_dim;
        const au::Rect text_box{
            .origin = au::Point{.x = card_box.origin.x + region.left_inset, .y = content_top},
            .size = au::Size{.width = region.width, .height = bar_height_ - 28.0F}};
        const auto display_utf8 = to_utf8(display);

        // 类型图标（`SPEC.FEAT.WS.04`）：名称左侧一枚字形，色随名称（D7 的口径——状态三枚角标已占
        // 绿／红／琥珀三个色通道，类型只靠字形区分）。槽宽含在 `region.left_inset` 里，故图标缺席时
        // 名称回到 `x+12`（判据 ② 的「逐位相同」就是这一句的屏幕形态）。
        if (tab.kind.has_value()) {
            const au::Rect glyph_box{
                .origin = au::Point{.x = card_box.origin.x + kCardTextLeftInsetDp, .y = content_top},
                .size = au::Size{.width = kTypeSlotWidth, .height = bar_height_ - 28.0F}};
            p.draw_text(glyph_box, std::string{connection_type_glyph(*tab.kind)}, font, text_color);
        }
        p.draw_text(text_box, display_utf8, font, text_color);

        // 关闭钮（只在 hover 或选中时显形）：按稿那两枚折线落笔，中心距栏位右缘 13 dp、纵向取带中线
        // （裁决 7.191）。命中区仍是 `close_zone_` 那 20 dp，与本笔的 8 dp 包络不必相等——包络整个落在
        // 命中区之内，改的只是墨落哪儿。
        if (tab.has_close_button && (tab.is_selected || hovered_tab_id_ == tab.id)) {
            const au::Color close_color = hovered_tab_id_ == tab.id ? chrome.accent : chrome.text_dim;
            const float center_x = card_box.origin.x + w - kCloseCenterInsetDp;
            const float center_y = bounds.origin.y + bar_height_ * 0.5F;
            const float half = kCloseGlyphSizeDp * 0.5F;
            p.draw_line(au::Point{.x = center_x - half, .y = center_y - half},
                        au::Point{.x = center_x + half, .y = center_y + half}, kGlyphStrokeDp,
                        close_color);
            p.draw_line(au::Point{.x = center_x + half, .y = center_y - half},
                        au::Point{.x = center_x - half, .y = center_y + half}, kGlyphStrokeDp,
                        close_color);
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
            p.fill_sector(au::Point{.x = card_box.origin.x + w - kBadgeAnchorInsetDp -
                                          kBadgePitchDp * static_cast<float>(rank),
                                    .y = content_mid},
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
    }

    // 落点导引线（判据 ⑯）：画在**整层卡片之后**——被拖那格跟手时它的中心就在缝附近，而缝到中心的距离
    // 不会超过半格，导引线若先落就被那张卡片盖得看不见，「两格之间画一条线」这条判据在屏幕上不复存在。
    // 纵向取卡片同带（顶内缩、底贴齐），横坐标以那条缝为中心；`resolve_drop` 已把它钳回右界内，故这一笔
    // 永不压在入口簇的墨上（裁决 7.190）。
    if (drag_.has_value() && drag_->is_dragging && drag_->drop.has_value()) {
        const au::Rect guide{
            .origin = au::Point{.x = bounds.origin.x + drag_->drop->guide_x - kGuideThicknessDp * 0.5F,
                                .y = card_top},
            .size = au::Size{.width = kGuideThicknessDp, .height = kCardHeightDp}};
        p.fill_rect(guide, chrome.accent);
    }
    if (clip_strip) {
        p.pop_clip();
    }

    // 栏底那条 1 dp 细线（稿 `line x1=0 y1=78 x2=1500 y2=78 stroke=#3A3C4C`）：压在卡片之上画，因为稿的
    // 次序就是先五格再这一线——它是标签条与下方内容区的分界，不属于任何一格。`#3A3C4C` 是 §2.1 表的
    // `line`，13 枚 token 里没有这一格，按既有细线口径取 `card_line`（`app_title_bar` / `sidebar_rail`
    // 同一条，裁决 7.179④c）。栏顶那条同类细线由自绘标题栏的底缘承担，不在本件。
    const au::Rect hairline{
        .origin = au::Point{.x = bounds.origin.x, .y = bounds.origin.y + bar_height_ - 1.0F},
        .size = au::Size{.width = bounds.size.width, .height = 1.0F}};
    p.fill_rect(hairline, chrome.card_line);

    // 右端两枚入口的字形按稿的折线画而不是按文本 run：稿那两枚是 `＋` 与 `⌄` 的线条包络，用字符画会随
    // 字体度量漂出稿位（自绘标题栏的三枚钮同一条口径，裁决 7.181①）。落笔盒 `cluster` 在上面就折好了，
    // 标签层的裁剪与它同源。
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
        // 右端两枚入口与标签层的右界同出一枚算式（`right_cluster` + `strip_right_edge`），故「画在哪儿」
        // 与「点哪儿算」不可能分叉：命中测试先在入口簇左沿止步，剩下的那一截才交给两枚入口。
        // 空盒（栏没宽、或 `hooks_.undo_close` 不在位）一律不吃点击。
        const auto widget_size = size();
        const auto cluster = right_cluster(widget_size.width);
        const auto hit = hit_test_at(e.local_position.x, strip_right_edge(cluster, widget_size.width));
        if (hit.tab_id.has_value()) {
            // 连击对账的账本：先把**前一下**落在哪一格留在手里，再记下这一下。关闭那一条腿也要记——
            // 它正是这套对账存在的理由（见 `last_tab_press_id_` 那条注释）。
            const std::optional<std::uint64_t> previous = last_tab_press_id_;
            last_tab_press_id_ = *hit.tab_id;
            if (hit.on_close_button) {
                // 关闭标签
                if (hooks_.close) {
                    hooks_.close(*hit.tab_id);
                }
            } else {
                // 就地重命名（屏 1 余项 (c)，裁决 7.195）：**双击**且落点在那一截名称里才弹胶囊。格的其余
                // 落点（类型图标、左右留白、角标簇）仍走选中与拖拽——否则「拖一格」会在第二下落下那一刻
                // 变成编辑。命中区间与绘制、胶囊盒同出 `name_span`，故「点中哪一截」和「弹在哪一截」同源。
                // 这一支不建拖拽态、也不再呼 `select`：第二下的前一下已经把这一格选中了。
                //
                // 只认**第二下**、且与前一下落在同一格：框架的连击计数说的是「这两下在时间窗与 4 dp 位移
                // 之内、中间没有别的 Press」，不说「这两下落在同一格的同一截名称里」。第一下若把这一格关掉，
                // 走位立刻重排，第二下的同一屏幕坐标就归了邻格——于是「双击 ✕」会在关掉一格之后把胶囊弹到
                // 它邻格的名字上；按 `>= 2` 还会让第三下再认领一次，把已弹开的胶囊当成第二次入口。位移那一档
                // 不在这里重复判：`local_position` 是栏相对的，栏自己在两下之间不动，框架那 4 dp 半径已经
                // 把它管住了。
                const bool second_click_on_same_tab =
                    e.click_count == 2U && hooks_.begin_rename && previous.has_value() &&
                    previous.value() == *hit.tab_id;
                const std::optional<std::pair<float, float>> span =
                    second_click_on_same_tab ? name_span(*hit.tab_id) : std::nullopt;
                if (span.has_value() && e.local_position.x >= span->first &&
                    e.local_position.x < span->second) {
                    hooks_.begin_rename(*hit.tab_id);
                    e.is_handled = true;
                    return;
                }
                // 开始拖拽或选中。跟手那两枚量（按下点相对左沿的偏移、被拖格宽）必须在 `hooks_.select`
                // **之前**从当前布局折出来：选中会推一次重排偏移，之后再量左沿读到的是已经挪过的那一档，
                // 被拖格会在越过阈值那一刻当场跳一截（裁决 7.190）。
                float grab_dx = 0.0F;
                float dragged_width = 0.0F;
                for (const auto &slot : strip_slots(std::nullopt)) {
                    if (slot.id == *hit.tab_id) {
                        grab_dx = e.local_position.x - slot.x0;
                        dragged_width = slot.x1 - slot.x0;
                        break;
                    }
                }
                drag_ = DragState{
                    .dragged_id = *hit.tab_id,
                    .press_x = e.local_position.x,
                    .current_x = e.local_position.x,
                    .is_dragging = false,
                    .grab_dx = grab_dx,
                    .dragged_width = dragged_width,
                };
                // 选中该标签
                if (hooks_.select) {
                    hooks_.select(*hit.tab_id);
                }
            }
            e.is_handled = true;
            return;
        }
        // 右端两枚入口：命中盒与上面那条界同源（同一次 `right_cluster` 调用），故「画在哪儿」与「点哪儿算」
        // 不可能分叉。
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
            // 落点每帧现折：下标与导引线同出 `resolve_drop` 那一枚算式，故导引线指到哪儿、松手就落到
            // 哪儿（判据 ⑯）。落点中心＝跟手左沿 + 半格宽，右界取与绘制、命中同源的 `strip_right_edge`。
            const auto widget_size = size();
            const float strip_edge =
                strip_right_edge(right_cluster(widget_size.width), widget_size.width);
            drag_->drop = resolve_drop(drag_->current_x - drag_->grab_dx + drag_->dragged_width * 0.5F,
                                       strip_edge, drag_->dragged_id);
            mark_needs_paint();
            e.is_handled = true;
            return;
        }
    } else if (e.action == au::MouseAction::Release && drag_.has_value()) {
        // 落点仍在原格 ⇒ `TabStrip::move` 走到 `from == to_index` 那一档、原地返回（7.43④），故这里
        // 不必另判「有没有真的换序」；没有其余标签（单标签窗）时 `drop` 为空，连呼都不呼。
        if (drag_->is_dragging && drag_->drop.has_value() && hooks_.move) {
            hooks_.move(drag_->dragged_id, drag_->drop->index);
        }
        drag_.reset();
        mark_needs_paint();
        e.is_handled = true;
        return;
    }

    // hover 态
    if (e.action == au::MouseAction::Move) {
        const auto widget_size = size();
        const float strip_edge =
            strip_right_edge(right_cluster(widget_size.width), widget_size.width);
        const auto hit = hit_test_at(e.local_position.x, strip_edge);
        if (hit.tab_id != hovered_tab_id_) {
            hovered_tab_id_ = hit.tab_id;
            mark_needs_paint();
        }
    }

    au::Widget::on_pointer_event(e);
}

}  // namespace borealis::ui
