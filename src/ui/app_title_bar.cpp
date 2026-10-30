// ============================================================
// 屏 1 自绘标题栏实现（src/ui/app_title_bar.cpp）
// ------------------------------------------------------------
// 逐值来源：`codespec/UI_WORKSPACE.draft.svg` 屏 1 那条 36 dp 带（窗口 1500 dp 宽下的原始元素）——
//   rect 0,0 1500×36 `#21222C` ／ circle cx=20 cy=18 r=7 `#BD93F9` ＋ text「B」10 w700 `#1E1F29` ／
//   text「Borealis」x=38 13 w600 `#F8F8F2` ／ text 上下文 x=105.2 12 `#62728A` ／
//   line x=1350 y=10..26 `#4D4F63` ／ text「—」x=1370 14 `#8B93A7` ／ text「▢」x=1400 13 `#8B93A7` ／
//   text「F11」x=1430 11 `#62728A` ／ 两线交成的 ✕ 1469..1477 × 14..22 `#8B93A7` 1.6 ／
//   line y=36 通宽 `#3A3C4C`。
//
// 三处**按算式而非按字面值**落地，逐条登记在裁决 7.181：
// ① 稿那三枚钮与 F11 是**文本 run**（基线都在 y=23），落笔范围随字体度量而变、不可反推盒；故热区
//    取「以 run 中心为心、统一 28 dp 宽、通栏高」，字形按各自的外接尺寸画线。中心距右缘的内缩
//    直接取稿面值（123／94／27），pitch 因此是非等距的——这正是 F11 插在 ▢ 与 ✕ 之间的结果。
// ② 上下文串的左沿跟标题右端走（稿那个 105.2 只在「Borealis」这枚字宽下成立）。
// ③ 底缘那条线取 `card_line`（#4D4F63）而不是稿的 `#3A3C4C`：后者不在两档 13 枚 token 里，为本件
//    单开一枚就是给色板加第 14 格——与状态栏条顶线（7.179④c）、折叠轨右沿线同一条口径。
//
// 文本落点（`title_box_`／`context_box_`）算在**绘制**里而不是布局里：它们只随串宽动，放进布局就
// 要在改标题时额外重排一整条带（`StatusBar::anchor_box_` 同一先例）。三枚钮与 F11 的盒只随窗宽动，
// 留在 `on_layout`，与基类把 chips 盒算在布局期同形。
// ============================================================

#include "app_title_bar.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "aurora/core/color.h"
#include "aurora/core/dimension.h"
#include "aurora/core/font.h"
#include "aurora/core/types.h"
#include "aurora/render/font_engine.h"
#include "aurora/window/window_state.h"

#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

constexpr double kBarHeightDp = 36.0;  ///< 稿 §2.2「标题栏高 36 dp，四屏一致」。
constexpr double kHairlineWidthDp = 1.0;

// ---- 左半：徽标 + 应用名 + 上下文串（稿面逐值） ----
constexpr double kBadgeCenterXDp = 20.0;
constexpr double kBadgeCenterYDp = 18.0;  ///< 即栏中线（稿 cy=18，栏高 36）
constexpr double kBadgeRadiusDp = 7.0;
constexpr double kBadgeFontPt = 10.0;
constexpr int kBadgeFontWeight = 700;
constexpr double kTitleLeftDp = 38.0;  ///< 稿标题 run 的左沿（徽标右缘 27 之外 11 dp）
constexpr double kTitleFontPt = 13.0;
constexpr int kTitleFontWeight = 600;   ///< 稿 font-weight=600（基类那一支钉的是 500）
constexpr double kTitleToContextGapDp = 14.0;  ///< 稿面 105.2 −（38 + 「Borealis」实测字宽）取整
constexpr double kContextFontPt = 12.0;
constexpr double kContextRightPadDp = 8.0;  ///< 上下文串到竖分隔线之间留白（窄窗退化的截断点）

// ---- 右半：竖线 + 三枚钮 + F11 提示（全部右锚） ----
constexpr double kSeparatorInsetDp = 150.0;  ///< 稿 line x=1350 ⇒ 距右缘 150
constexpr double kSeparatorTopDp = 10.0;     ///< 稿 y1=10
constexpr double kSeparatorHeightDp = 16.0;  ///< 稿 y2=26 − y1=10
constexpr double kMinimizeCenterInsetDp = 123.0;  ///< 稿「—」run x=1370（14 号 em-dash 半宽≈7）⇒ 中心 1377
constexpr double kMaximizeCenterInsetDp = 94.0;   ///< 稿「▢」run x=1400（13 号方框半宽≈6.5）⇒ 中心 1406.5
constexpr double kCloseCenterInsetDp = 27.0;      ///< 稿 ✕ 两线包络 1469..1477 ⇒ 中心 1473
constexpr double kButtonHitWidthDp = 28.0;        ///< 统一热区宽（三枚中心非等距，见文件头①）
constexpr double kHintRightInsetDp = 70.0;        ///< 稿「F11」run x=1430 ⇒ 左沿距右缘 70
constexpr double kHintFontPt = 11.0;
constexpr const char *kHintText = "F11";

// ---- 字形尺寸（稿那三枚是文本 run，取其外接档；见文件头①） ----
constexpr double kMinimizeGlyphWidthDp = 12.0;
constexpr double kMaximizeGlyphSizeDp = 10.0;
constexpr double kRestoreGlyphSizeDp = 9.0;   ///< 还原形那两枚方框的边长（稿没画过最大化态）
constexpr double kRestoreGlyphOffsetDp = 1.5;  ///< 两框沿两轴的错位量（＝边距 3）
constexpr double kCloseGlyphHalfDp = 4.0;      ///< 稿 ✕ 包络的一半（8 dp）
constexpr double kGlyphStrokeDp = 1.5;
constexpr double kCloseStrokeDp = 1.6;  ///< 稿 stroke-width=1.6

constexpr double kPi = 3.14159265358979323846;
constexpr float kFallbackWidthDp = 640.0F;  ///< 与基类同一条「无界约束」退化档

/// @brief 一行的行盒顶（稿把三枚 run 的基线都写在 y=23，但基线不可反推行盒顶 ⇒ 统一按栏中线居中）。
[[nodiscard]] auto text_top(const au::Font &font) -> double {
    return (kBarHeightDp - static_cast<double>(au::render::FontEngine::measure_height(font))) / 2.0;
}

[[nodiscard]] auto make_font(double size_pt, int weight) -> au::Font {
    au::Font font;
    font.size_pt = static_cast<float>(size_pt);
    font.weight = weight;
    return font;
}

/// @brief 方框描边（四段折线闭合，与折叠轨那三枚图标同一支笔）。
auto stroke_rect(au::Painter &p, double left, double top, double side, double stroke,
                 const au::Color &ink) -> void {
    const std::vector<au::Point> corners = {
        au::Point{.x = static_cast<float>(left), .y = static_cast<float>(top)},
        au::Point{.x = static_cast<float>(left + side), .y = static_cast<float>(top)},
        au::Point{.x = static_cast<float>(left + side), .y = static_cast<float>(top + side)},
        au::Point{.x = static_cast<float>(left), .y = static_cast<float>(top + side)},
        au::Point{.x = static_cast<float>(left), .y = static_cast<float>(top)},
    };
    p.stroke_polyline(corners, static_cast<float>(stroke), ink);
}

[[nodiscard]] auto inside(const au::Rect &box, const au::Point &at) -> bool {
    return box.size.width > 0.0F && box.contains(at);
}

/// @brief 以 `center_x` 为中心摆一段文本，交回落笔后的墨迹盒（本地坐标 dp）。
auto draw_centered(au::Painter &p, const au::Rect &bounds, double center_x, const au::Font &font,
                   const std::string &text, const au::Color &ink, au::Rect &out_box) -> void {
    out_box = au::Rect{};
    if (text.empty()) {
        return;
    }
    const double width = static_cast<double>(au::render::FontEngine::measure_width(text, font));
    const double height = static_cast<double>(au::render::FontEngine::measure_height(font));
    if (width <= 0.0 || height <= 0.0) {
        return;
    }
    out_box = au::Rect{.origin = au::Point{.x = static_cast<float>(center_x - width / 2.0),
                                           .y = static_cast<float>(text_top(font))},
                       .size = au::Size{.width = static_cast<float>(width),
                                        .height = static_cast<float>(height)}};
    p.draw_text(au::Rect{.origin = au::Point{.x = static_cast<float>(bounds.origin.x + out_box.origin.x),
                                             .y = static_cast<float>(bounds.origin.y + out_box.origin.y)},
                         .size = out_box.size},
                text, font, ink);
}

}  // namespace

auto format_title_context(const std::string_view directory, const std::string_view session) -> std::string {
    std::string out{directory};
    if (!session.empty()) {
        if (!out.empty()) {
            // CJK-LITERAL: 上屏文案 - 那枚间隔号是屏 1 上下文串自身的排版（两侧各一空格），换成 ASCII
            //            竖线就不是稿面那一幅；它不是可译短语，故不进 ARB 词表（裁决 7.150 的那条口径）。
            out += " · ";
        }
        out.append(session);
    }
    return out;
}

AppTitleBar::AppTitleBar(Hooks hooks) : hooks_{std::move(hooks)} {
    // 不可获焦：与横幅、状态栏、折叠轨同一条纪律——常驻的标识条不该吃掉键盘，否则点完标题栏敲的
    // 第一句落进了 chrome 而不是任何 pane。
    set_focusable(false);
    // 栏高走基类那一份（`serialize_props` 写的就是这个值），三枚钮交给本件自绘 ⇒ 基类那三支关掉：
    // 留着会让两套热区同时吃点击，且基类的悬停 Snap 弹窗会在稿面没有的地方开出一块菜单。
    set_height(static_cast<float>(kBarHeightDp));
    set_window_controls(false);
    // 应用名交给基类持有（`serialize_props` 的 title 字段因此不是空转）；落笔由本件按稿面的 x=38 画。
    set_title("Borealis");
}

AppTitleBar::~AppTitleBar() = default;

auto AppTitleBar::set_context(std::string context) -> AppTitleBar & {
    if (context_ == context) {
        return *this;
    }
    context_ = std::move(context);
    // 只重绘：这一条的高是常量，串变了既不改自己的布局也不改身后任何一格的布局。
    mark_needs_paint();
    return *this;
}

auto AppTitleBar::serialize_props(au::Json &props) const -> void {
    au::TitleBar::serialize_props(props);
    props.set("context", context_);
}

auto AppTitleBar::deserialize_props(const au::Json &props) -> void {
    au::TitleBar::deserialize_props(props);
    if (props.contains("context")) {
        context_ = props.at("context")->as_or<std::string>("");
    }
    mark_needs_paint();
}

auto AppTitleBar::is_fullscreen() const -> bool {
    const au::WindowMode *mode = bar_env_ != nullptr ? bar_env_->get<au::WindowMode>() : nullptr;
    return mode != nullptr && *mode == au::WindowMode::FullScreen;
}

auto AppTitleBar::on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size {
    bar_env_ = ctx.env;  // 事件与绘制阶段读 `WindowChrome`／`WindowState` 用
    // 基类那一跳不是为了它的返回值，而是为了把它自己的 `env_` 接上——空白区拖拽与双击最大化那两条腿
    // 是从它那份 `env_` 取 chrome 的，不接就静默 no-op。
    const au::Size sized = au::TitleBar::on_layout(c, ctx);
    const float width = std::isfinite(sized.width) ? sized.width : kFallbackWidthDp;
    if (is_fullscreen()) {
        // 「全屏就不挂这条，退出挂回」（人已裁）：落地形态是常驻节点自报 0 高，理由见头注。
        height_ = 0.0;
        layout_controls(0.0F);
        return c.constrain(au::Size{.width = width, .height = 0.0F});
    }
    height_ = kBarHeightDp;
    layout_controls(width);
    return c.constrain(au::Size{.width = width, .height = static_cast<float>(kBarHeightDp)});
}

auto AppTitleBar::layout_controls(const float width) -> void {
    const double right = static_cast<double>(width);
    const auto box_for = [right](const double center_inset) -> au::Rect {
        const double left = right - center_inset - kButtonHitWidthDp / 2.0;
        return au::Rect{.origin = au::Point{.x = static_cast<float>(left), .y = 0.0F},
                        .size = au::Size{.width = static_cast<float>(kButtonHitWidthDp),
                                         .height = static_cast<float>(kBarHeightDp)}};
    };
    if (width <= 0.0F) {
        // 两枚文本盒Normally只在 `on_paint` 里落定，但全屏那一档本件自报 0 高、绘制根本不会被调到
        // （空 bounds 不进脏区），不在这里清掉就会把上一帧的盒留给观测者——形态读点会撒谎。
        minimize_box_ = au::Rect{};
        maximize_box_ = au::Rect{};
        close_box_ = au::Rect{};
        hint_box_ = au::Rect{};
        title_box_ = au::Rect{};
        context_box_ = au::Rect{};
        return;
    }
    minimize_box_ = box_for(kMinimizeCenterInsetDp);
    maximize_box_ = box_for(kMaximizeCenterInsetDp);
    close_box_ = box_for(kCloseCenterInsetDp);
    // 提示串：左锚在距右缘 70 处，盒宽按实测字宽（不可点，只作观测）。
    const au::Font hint_font = make_font(kHintFontPt, 400);
    const double hint_width = static_cast<double>(au::render::FontEngine::measure_width(kHintText, hint_font));
    const double hint_height = static_cast<double>(au::render::FontEngine::measure_height(hint_font));
    hint_box_ = au::Rect{.origin = au::Point{.x = static_cast<float>(right - kHintRightInsetDp),
                                             .y = static_cast<float>(text_top(hint_font))},
                         .size = au::Size{.width = static_cast<float>(hint_width),
                                          .height = static_cast<float>(hint_height)}};
}

auto AppTitleBar::on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void {
    title_box_ = au::Rect{};
    context_box_ = au::Rect{};
    // **形态与几何的自洽闸**（本件唯一的自愈腿）：`height_` 是上次布局按当时的 `WindowMode` 算的，而
    // 框架的布局缓存按「约束未变且未标脏」短路（`Widget::layout`）——进出全屏**必然**改窗口尺寸，
    // 因而正常路径上约束就变了；只有「全屏尺寸恰好等于普通尺寸」那一档会让它停在旧高度上。这里发现
    // 两者打架就把自己要一次重排，本帧不落墨（宁可少画一帧，也不让那条带占在全屏顶上）。
    if (is_fullscreen() != (height_ <= 0.0)) {
        mark_needs_layout();
        return;
    }
    if (height_ <= 0.0 || bounds.size.width <= 0.0F) {
        return;  // 全屏那一档：这条带既不占位也不落墨
    }
    const SettingsChrome chrome = settings_chrome();
    const au::WindowState *state = ctx.environment<au::WindowState>();
    const bool active = state == nullptr || *state == au::WindowState::Visible;
    const double left = static_cast<double>(bounds.origin.x);
    const double top = static_cast<double>(bounds.origin.y);
    const double width = static_cast<double>(bounds.size.width);

    // ① 条身与底缘。失焦让位一档：稿只画了激活态，`window_bg` 是暗档里比卡片低一格的既有值，
    //    亮档同构（与基类 `bg_inactive` 的角色一致，只是值改由 13 枚 token 给）。
    p.fill_rect(au::Rect{.origin = bounds.origin,
                         .size = au::Size{.width = bounds.size.width,
                                          .height = static_cast<float>(kBarHeightDp)}},
                active ? chrome.card_bg : chrome.window_bg);
    p.fill_rect(au::Rect{.origin = au::Point{.x = bounds.origin.x,
                                             .y = static_cast<float>(top + kBarHeightDp - kHairlineWidthDp)},
                         .size = au::Size{.width = bounds.size.width,
                                          .height = static_cast<float>(kHairlineWidthDp)}},
                chrome.card_line);

    // ② 徽标：一枚 `accent` 圆底 + 居中一枚「B」（稿 fill `#1E1F29`＝`window_bg`）。
    p.fill_sector(au::Point{.x = static_cast<float>(left + kBadgeCenterXDp),
                             .y = static_cast<float>(top + kBadgeCenterYDp)},
                  static_cast<float>(kBadgeRadiusDp), 0.0F, 0.0F, static_cast<float>(2.0 * kPi),
                  chrome.accent);
    au::Rect badge_box;
    draw_centered(p, bounds, left + kBadgeCenterXDp, make_font(kBadgeFontPt, kBadgeFontWeight), "B",
                  chrome.window_bg, badge_box);
    (void)badge_box;  // 徽标落点由圆心得数唯一决定，不另开观测点

    // ③ 应用名 + 上下文串（后者取装配层从「与 OS 标题同一条真值源」喂进来的那一格）。
    const au::Color title_ink = active ? chrome.text : chrome.text_dim;
    const au::Font title_font = make_font(kTitleFontPt, kTitleFontWeight);
    double cursor = left + kTitleLeftDp;
    {
        const std::string &title = this->title();
        if (!title.empty()) {
            const double title_width = static_cast<double>(au::render::FontEngine::measure_width(title, title_font));
            const double title_height = static_cast<double>(au::render::FontEngine::measure_height(title_font));
            title_box_ = au::Rect{.origin = au::Point{.x = static_cast<float>(kTitleLeftDp),
                                                      .y = static_cast<float>(text_top(title_font))},
                                  .size = au::Size{.width = static_cast<float>(title_width),
                                                   .height = static_cast<float>(title_height)}};
            p.draw_text(au::Rect{.origin = au::Point{.x = static_cast<float>(cursor),
                                                     .y = static_cast<float>(top + title_box_.origin.y)},
                                 .size = title_box_.size},
                        title, title_font, title_ink);
            cursor += title_width + kTitleToContextGapDp;
        }
    }
    if (!context_.empty()) {
        const au::Font context_font = make_font(kContextFontPt, 400);
        const double separator_x = left + width - kSeparatorInsetDp;
        const double room = std::max(0.0, separator_x - kContextRightPadDp - (cursor - left));
        const double context_width = static_cast<double>(
            au::render::FontEngine::measure_width(context_, context_font));
        const double context_height =
            static_cast<double>(au::render::FontEngine::measure_height(context_font));
        if (room > 4.0 && context_width > 0.0) {
            // 画到竖线前就截：`draw_text` 按排版区裁剪，故给的宽度是「剩余房间」而不是整串宽度。
            // CJK-LITERAL: 上屏文案 - 前缀那枚长破折号是稿面画的连接符（不是可译短语），随串一起落笔
            p.draw_text(au::Rect{.origin = au::Point{.x = static_cast<float>(cursor),
                                                     .y = static_cast<float>(top + text_top(context_font))},
                                 .size = au::Size{.width = static_cast<float>(room),
                                                  .height = static_cast<float>(context_height)}},
                        "— " + context_, context_font, chrome.text_faint);
            context_box_ = au::Rect{.origin = au::Point{.x = static_cast<float>(cursor - left),
                                                        .y = static_cast<float>(text_top(context_font))},
                                    .size = au::Size{.width = static_cast<float>(std::min(room, context_width)),
                                                     .height = static_cast<float>(context_height)}};
        }
    }

    // ④ 竖分隔线（稿 `#4D4F63`＝`card_line`，两档都在册）。
    p.fill_rect(au::Rect{.origin = au::Point{.x = static_cast<float>(left + width - kSeparatorInsetDp),
                                             .y = static_cast<float>(top + kSeparatorTopDp)},
                         .size = au::Size{.width = static_cast<float>(kHairlineWidthDp),
                                          .height = static_cast<float>(kSeparatorHeightDp)}},
                chrome.card_line);

    // ⑤ 三枚钮的字形 + F11 提示（稿：钮 `#8B93A7`＝`text_dim`，提示 `#62728A`＝`text_faint`）。
    const au::Color glyph_ink = active ? chrome.text_dim : chrome.text_faint;
    const double center_y = top + kBarHeightDp / 2.0;
    const double minimize_x = left + width - kMinimizeCenterInsetDp;
    p.draw_line(au::Point{.x = static_cast<float>(minimize_x - kMinimizeGlyphWidthDp / 2.0),
                          .y = static_cast<float>(center_y)},
                au::Point{.x = static_cast<float>(minimize_x + kMinimizeGlyphWidthDp / 2.0),
                          .y = static_cast<float>(center_y)},
                static_cast<float>(kGlyphStrokeDp), glyph_ink);
    const double maximize_x = left + width - kMaximizeCenterInsetDp;
    const au::WindowMode *mode = ctx.environment<au::WindowMode>();
    if (mode != nullptr && *mode == au::WindowMode::Maximized) {
        // 还原形：两枚错位方框。稿没画过最大化态这一档，取业界通用形态（登记在 7.181）。
        const double half = kRestoreGlyphSizeDp / 2.0;
        stroke_rect(p, maximize_x - half - kRestoreGlyphOffsetDp, center_y - half - kRestoreGlyphOffsetDp,
                    kRestoreGlyphSizeDp, kGlyphStrokeDp, glyph_ink);
        stroke_rect(p, maximize_x - half + kRestoreGlyphOffsetDp, center_y - half + kRestoreGlyphOffsetDp,
                    kRestoreGlyphSizeDp, kGlyphStrokeDp, glyph_ink);
    } else {
        stroke_rect(p, maximize_x - kMaximizeGlyphSizeDp / 2.0, center_y - kMaximizeGlyphSizeDp / 2.0,
                    kMaximizeGlyphSizeDp, kGlyphStrokeDp, glyph_ink);
    }
    const double close_x = left + width - kCloseCenterInsetDp;
    p.draw_line(au::Point{.x = static_cast<float>(close_x - kCloseGlyphHalfDp),
                           .y = static_cast<float>(center_y - kCloseGlyphHalfDp)},
                au::Point{.x = static_cast<float>(close_x + kCloseGlyphHalfDp),
                          .y = static_cast<float>(center_y + kCloseGlyphHalfDp)},
                static_cast<float>(kCloseStrokeDp), glyph_ink);
    p.draw_line(au::Point{.x = static_cast<float>(close_x + kCloseGlyphHalfDp),
                          .y = static_cast<float>(center_y - kCloseGlyphHalfDp)},
                au::Point{.x = static_cast<float>(close_x - kCloseGlyphHalfDp),
                          .y = static_cast<float>(center_y + kCloseGlyphHalfDp)},
                static_cast<float>(kCloseStrokeDp), glyph_ink);
    p.draw_text(au::Rect{.origin = au::Point{.x = static_cast<float>(left + hint_box_.origin.x),
                                             .y = static_cast<float>(top + hint_box_.origin.y)},
                         .size = hint_box_.size},
                kHintText, make_font(kHintFontPt, 400), chrome.text_faint);
}

auto AppTitleBar::dispatch(const AppTitleBarAction action) -> void {
    const au::WindowChrome *chrome = bar_env_ != nullptr ? bar_env_->get<au::WindowChrome>() : nullptr;
    switch (action) {
        case AppTitleBarAction::Minimize:
            if (chrome != nullptr) {
                chrome->minimize();
            }
            break;
        case AppTitleBarAction::ToggleMaximize:
            if (chrome != nullptr) {
                chrome->toggle_maximize();
            }
            break;
        case AppTitleBarAction::Close:
            // 本仓的关窗要过确认闸门（`SPEC.FEAT.WS.03` 稿 D4）：装配层那条腿折的是
            // `WindowCloseInitiator::UserCommand`，与命令面板「关闭此窗」同一个判据。留空才回落
            // `chrome->close()`——那是程序化来源，闸门恒不问，用它当 ✕ 就等于少问一句。
            if (hooks_.request_close) {
                hooks_.request_close();
            } else if (chrome != nullptr) {
                chrome->close();
            }
            break;
    }
    if (hooks_.note_window_action) {
        hooks_.note_window_action(action);
    }
}

auto AppTitleBar::on_pointer_event(au::MouseEvent &e) -> void {
    if (e.action == au::MouseAction::Press && e.button == au::MouseButton::Left && height_ > 0.0) {
        if (inside(close_box_, e.local_position)) {
            dispatch(AppTitleBarAction::Close);
            e.is_handled = true;
            return;
        }
        if (inside(maximize_box_, e.local_position)) {
            dispatch(AppTitleBarAction::ToggleMaximize);
            e.is_handled = true;
            return;
        }
        if (inside(minimize_box_, e.local_position)) {
            dispatch(AppTitleBarAction::Minimize);
            e.is_handled = true;
            return;
        }
    }
    // 其余交给基类：空白区的拖拽移动与双击最大化（都在 Press 派发栈内同步调 chrome），
    // 以及本件不认的动作（Move／Release／右键）。
    au::TitleBar::on_pointer_event(e);
}

}  // namespace borealis::ui
