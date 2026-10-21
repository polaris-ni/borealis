// ============================================================
// 广播横幅实现（src/ui/broadcast_banner.cpp）
// ------------------------------------------------------------
// 只做「按状态落笔 + 点击转交」，成员名与计数都由装配层折好交来（F12：名字只有一个真值源）。
// 绘制不接终端主题：横幅标识的是操作态，底色/字色取稿面固定 token（与分屏 chrome 同档）。
// ============================================================

#include "broadcast_banner.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "aurora/core/dimension.h"
#include "aurora/core/font.h"
#include "aurora/core/types.h"
#include "aurora/render/font_engine.h"
#include "borealis/term/utf8.h"
#include "settings_i18n.h"

namespace borealis::ui {

namespace {

/// @brief 几何全部来自稿 §2.2（逻辑 dp）。
constexpr double kHeightDp = 26.0;
constexpr double kTriangleWidthDp = 14.0;
constexpr double kTriangleHeightDp = 12.0;
constexpr double kTriangleInsetDp = 16.0;
constexpr float kTriangleStrokeDp = 1.2F;
constexpr double kTriangleDotDp = 1.4;
constexpr double kTriangleDotBottomDp = 2.0;  ///< 点距三角底边的留白。
constexpr double kTitleLeftDp = 32.0;
constexpr double kGapAfterTitleDp = 16.0;
constexpr float kTitlePt = 12.0F;
constexpr float kSubtitlePt = 11.5F;
constexpr double kButtonWidthDp = 100.0;
constexpr double kButtonHeightDp = 22.0;
constexpr double kButtonRightDp = 18.0;
constexpr float kButtonRadiusDp = 5.0F;
constexpr float kButtonPt = 13.0F;
constexpr double kTextRightGapDp = 16.0;  ///< 文本区与按钮之间至少留这么宽。
constexpr double kTextLineHeightDp = 16.0;

/// @brief 稿面固定 token（§2.2）：横幅底、标题/按钮字、副文案、按钮底、按钮边框。
constexpr au::Color kBannerBackground{0x3A, 0x2A, 0x14};
constexpr au::Color kBannerInk{0xF1, 0xFA, 0x8C};
constexpr au::Color kBannerDimInk{0xC8, 0xB2, 0x7A};
constexpr au::Color kButtonBackground{0x5A, 0x44, 0x20};
constexpr au::Color kButtonBorder{0x4D, 0x4F, 0x63};

[[nodiscard]] auto to_utf8(const std::u32string &text) -> std::string {
    std::string out;
    static_cast<void>(term::encode_utf8(text, out));
    return out;
}

/// @brief 成员名按加入序用 ASCII 逗号连起来：CJK 顿号会被 AGENTS §4.3 第 14 条拦在词条表之外，
///        而这里只是分隔符，不承载任何用户可见的语义（名字本身与标题都走词条表/标签名）。
[[nodiscard]] auto joined_names(const std::vector<std::u32string> &names) -> std::string {
    std::string out;
    for (const std::u32string &name : names) {
        if (!out.empty()) {
            out += ", ";
        }
        out += to_utf8(name);
    }
    return out;
}

/// @brief 按码点边界截断并补省略号（`…`，U+2026）：渐次测宽 O(n²)，但横幅只有几十个码点。
[[nodiscard]] auto truncate_utf8(const std::string &text, double max_width, const au::Font &font,
                                 const au::render::TextLayoutOpts &opts, float scale) -> std::string {
    if (au::render::FontEngine::display_width(text, font, opts, scale) <= max_width) {
        return text;
    }
    static const std::string kEllipsis = "\xE2\x80\xA6";
    if (max_width <= au::render::FontEngine::display_width(kEllipsis, font, opts, scale)) {
        return {};
    }
    std::size_t cut = text.size();
    while (cut > 0U) {
        --cut;
        while (cut > 0U && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
            --cut;  // 退到本码点的首字节，避免把 UTF-8 序列截成半个
        }
        std::string candidate = text.substr(0U, cut);
        candidate += kEllipsis;
        if (au::render::FontEngine::display_width(candidate, font, opts, scale) <= max_width) {
            return candidate;
        }
    }
    return kEllipsis;
}

}  // namespace

BroadcastBanner::BroadcastBanner(BroadcastBannerHooks hooks) : hooks_(std::move(hooks)) {
    // 不可获焦（F10）：否则点一下横幅，键盘焦点就进了这条 chrome，退出后第一下键入无处可去。
    set_focusable(false);
    width(au::fill());  // 横向填满根 Column
}

auto BroadcastBanner::set_state(BroadcastBannerState state) -> void {
    if (state_ == state) {
        return;
    }
    const bool height_changed = state_.active != state.active;
    state_ = std::move(state);
    if (!state_.active) {
        exit_button_box_ = au::Rect{};
    }
    if (height_changed) {
        mark_needs_layout();  // 0/26 是布局事实：工作区要随之让出/收回 26 dp
        // 横幅的宽度意图是 fill（`is_relayout_boundary()` 为真），`mark_needs_layout` 的布局缓存
        // 失效**不会上溯**；高度 0/26 却是父 Column 必须知道的变化，漏了这一句工作区永远停在旧盒上。
        // 走公开的 `layout_parent()` 显式让父容器重排（D10 的「常驻节点 + 高度切换」形态）。
        if (au::Widget *parent = layout_parent(); parent != nullptr) {
            parent->mark_needs_layout();
        }
    }
    mark_needs_paint();
}

auto BroadcastBanner::on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size {
    (void)ctx;
    const float height = state_.active ? static_cast<float>(kHeightDp) : 0.0F;
    au::Size self = c.max.is_finite() ? c.max : au::Size{.width = 480.0F, .height = height};
    self.height = height;
    return c.constrain(self);
}

auto BroadcastBanner::on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void {
    exit_button_box_ = au::Rect{};
    if (!state_.active || bounds.size.height <= 0.0F || bounds.size.width <= 0.0F) {
        return;
    }
    p.fill_rect(bounds, kBannerBackground);

    const float scale = ctx.scale_factor > 0.0F ? ctx.scale_factor : 1.0F;
    const au::render::TextLayoutOpts opts{};
    const double left = static_cast<double>(bounds.origin.x);
    const double top = static_cast<double>(bounds.origin.y);
    const double width_dp = static_cast<double>(bounds.size.width);
    const double height_dp = static_cast<double>(bounds.size.height);

    // ① 警告三角：14×12、左内缩 16、垂直居中；描边 + 底部一点（稿 §2.2）。
    const double tri_x = left + kTriangleInsetDp;
    const double tri_y = top + (height_dp - kTriangleHeightDp) / 2.0;
    const std::vector<au::Point> triangle = {
        au::Point{.x = static_cast<float>(tri_x + kTriangleWidthDp / 2.0), .y = static_cast<float>(tri_y)},
        au::Point{.x = static_cast<float>(tri_x), .y = static_cast<float>(tri_y + kTriangleHeightDp)},
        au::Point{.x = static_cast<float>(tri_x + kTriangleWidthDp),
                  .y = static_cast<float>(tri_y + kTriangleHeightDp)},
        au::Point{.x = static_cast<float>(tri_x + kTriangleWidthDp / 2.0), .y = static_cast<float>(tri_y)},
    };
    p.stroke_polyline(triangle, kTriangleStrokeDp, kBannerInk);
    p.fill_rect(au::Rect{.origin = au::Point{.x = static_cast<float>(tri_x + (kTriangleWidthDp - kTriangleDotDp) / 2.0),
                                             .y = static_cast<float>(tri_y + kTriangleHeightDp -
                                                                     kTriangleDotBottomDp - kTriangleDotDp)},
                         .size = au::Size{.width = static_cast<float>(kTriangleDotDp),
                                          .height = static_cast<float>(kTriangleDotDp)}},
                kBannerInk);

    // ② 退出按钮：100×22、圆角 5、距右 18、垂直居中；整条横幅都可点，故按钮盒只作观测点。
    const double button_x = left + width_dp - kButtonRightDp - kButtonWidthDp;
    const double button_y = top + (height_dp - kButtonHeightDp) / 2.0;
    const au::Rect button{static_cast<float>(button_x), static_cast<float>(button_y),
                          static_cast<float>(kButtonWidthDp), static_cast<float>(kButtonHeightDp)};
    p.fill_rounded_rect(button, kButtonRadiusDp, kButtonBackground);
    p.draw_rect(button, kButtonBorder);
    const au::Font button_font{.size_pt = kButtonPt};
    const std::string exit_label = settings_label("workspace.broadcast.exit");
    const double exit_text_w =
        static_cast<double>(au::render::FontEngine::display_width(exit_label, button_font, opts, scale));
    p.draw_text(au::Rect{.origin = au::Point{.x = static_cast<float>(button_x + (kButtonWidthDp - exit_text_w) / 2.0),
                                             .y = static_cast<float>(button_y + (kButtonHeightDp -
                                                                                 kTextLineHeightDp) / 2.0)},
                         .size = au::Size{.width = static_cast<float>(exit_text_w),
                                          .height = static_cast<float>(kTextLineHeightDp)}},
                exit_label, button_font, kBannerInk);
    exit_button_box_ = button;

    // ③ 标题（12 pt/600）：文本区到按钮左侧 16 dp 为止；超宽按省略号截断，副文案先让位。
    const au::Font title_font{.size_pt = kTitlePt, .weight = 600};
    const au::Font subtitle_font{.size_pt = kSubtitlePt};
    const std::string title =
        settings_label("workspace.broadcast.title",
                       {au::LocalizedString{joined_names(state_.names)},
                        au::LocalizedString{std::to_string(state_.session_count)}});
    const double text_left = left + kTitleLeftDp;
    const double text_room = button_x - kTextRightGapDp - text_left;
    if (text_room <= 0.0) {
        return;  // 窄到连标题都放不下：只留三角与退出按钮
    }
    const std::string shown_title = truncate_utf8(title, text_room, title_font, opts, scale);
    if (shown_title.empty()) {
        return;
    }
    const double title_w =
        static_cast<double>(au::render::FontEngine::display_width(shown_title, title_font, opts, scale));
    const double text_y = top + (height_dp - kTextLineHeightDp) / 2.0;
    p.draw_text(au::Rect{.origin = au::Point{.x = static_cast<float>(text_left), .y = static_cast<float>(text_y)},
                         .size = au::Size{.width = static_cast<float>(title_w),
                                          .height = static_cast<float>(kTextLineHeightDp)}},
                shown_title, title_font, kBannerInk);
    if (shown_title != title) {
        return;  // 标题已被截断：副文案整段不画（§2.2「放不下时整段省略（先让标题）」）
    }

    // ④ 副文案（11.5 pt）：标题之后 16 dp；放不下整段省略，不截短。
    const double subtitle_left = text_left + title_w + kGapAfterTitleDp;
    const double subtitle_room = button_x - kTextRightGapDp - subtitle_left;
    if (subtitle_room <= 0.0) {
        return;
    }
    const std::string subtitle = settings_label("workspace.broadcast.hint");
    const double subtitle_w =
        static_cast<double>(au::render::FontEngine::display_width(subtitle, subtitle_font, opts, scale));
    if (subtitle_w > subtitle_room) {
        return;
    }
    p.draw_text(au::Rect{.origin = au::Point{.x = static_cast<float>(subtitle_left), .y = static_cast<float>(text_y)},
                         .size = au::Size{.width = static_cast<float>(subtitle_w),
                                          .height = static_cast<float>(kTextLineHeightDp)}},
                subtitle, subtitle_font, kBannerDimInk);
}

auto BroadcastBanner::on_pointer_event(au::MouseEvent &e) -> void {
    if (e.action == au::MouseAction::Press && e.button == au::MouseButton::Left && hooks_.note_press) {
        // 上报先于「是否在广播态」：非广播态的横幅高 0、命中不到，这一句只是把「事实」与「动作」
        // 分成两件事（稿 D7）。清组那条仍受状态限制，二者不互相担保。
        hooks_.note_press();
    }
    if (!state_.active || e.action != au::MouseAction::Press || e.button != au::MouseButton::Left) {
        return;
    }
    // 整条 26 dp 都可点（稿 §2.2）；清组与焦点归还是装配层的事，本件不认识工作区。
    if (hooks_.exit_all) {
        hooks_.exit_all();
    }
    e.is_handled = true;
}

auto BroadcastBanner::cursor_shape() const -> std::optional<au::CursorShape> {
    if (!state_.active) {
        return std::nullopt;
    }
    return au::CursorShape::PointingHand;
}

}  // namespace borealis::ui
