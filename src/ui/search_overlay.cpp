// ============================================================
// 终端内搜索浮层实现（src/ui/search_overlay.cpp）
// ------------------------------------------------------------
// 第四个触达 `au::Painter` / `au::Widget` 的翻译单元（私有头形态同裁决 D1①）。本文件只有
// 「控件装配 + 状态搬运」：匹配表、扫描与跳转的算式一律在 `ui::search` 与视口侧，这里连一次
// 字符串比对都不做（文件头那段分工）。
// ============================================================

#include "search_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/core/log.h"
#include "aurora/event/event.h"
#include "aurora/event/focus.h"
#include "aurora/event/keycode.h"
#include "aurora/app/shortcuts.h"
#include "aurora/layout/flex.h"
#include "aurora/modifier/modifier.h"
#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "borealis/term/utf8.h"
#include "borealis/ui/search.h"
#include "settings_i18n.h"
#include "settings_panel.h"
#include "terminal_view.h"

namespace borealis::ui {
namespace {

/// @brief 条高（dp）：判据文 A1-c 的 44 与 E3-a 的 66，都是「上下内边距 + 每行 22」的纯算式
///        （44 ＝ 11 + 22 + 11，66 ＝ 11 + 22 + 22 + 11），故行高那一项在两档之间是同一个量。
constexpr float kBarHeightDp = 44.0F;
constexpr float kTwoRowBarHeightDp = 66.0F;
constexpr float kBarItemHeightDp = 22.0F;
constexpr aurora::EdgeInsets kBarPadding{.left = 8.0F, .top = 11.0F, .right = 8.0F, .bottom = 11.0F};

/// @brief 条体与 pane 边缘的间距（A1-a 的「距右缘与上缘各 8 dp」）与件间距（A1-b 的 8 dp）。
///        同一个 8 也进了 E 段的钳位算式（`box 宽 − 16` 就是左右各 8），故三处共用一个常量。
constexpr float kEdgeInsetDp = 8.0F;
constexpr float kItemGapDp = 8.0F;

/// @brief 七件的宽度（dp），逐档取自判据文 E1-a / E2-a / E3-a 的加和分解：固定件 ＋ 六段间距 ＋
///        输入框下限 ＝ 内容宽，内容宽 ＋ 左右内边距 ＝ 条宽。这些数不是目测的排版量，而是那三
///        条加和式的分项，改任何一项都要重算档位阈值，故全部点名在此。
constexpr float kInputMinWidthDp = 120.0F;
constexpr float kCountWidthDp = 64.0F;           ///< E1：`序号/总数` 的宽度。
constexpr float kCountCompactWidthDp = 56.0F;    ///< E2：`10000+` 也放得下的收窄档。
constexpr float kChipWidthDp = 40.0F;            ///< `Aa` / `.*` 恰占 40 dp（A1-d）。
constexpr float kJumpTextWidthDp = 64.0F;        ///< E1 的中文跳转钮。
constexpr float kJumpIconWidthDp = 40.0F;        ///< E2 的符号跳转钮。
constexpr float kCloseWidthDp = 40.0F;
constexpr aurora::EdgeInsets kChipPadding{.left = 6.0F, .top = 4.0F, .right = 6.0F, .bottom = 4.0F};
constexpr aurora::EdgeInsets kInputPadding{.left = 8.0F, .top = 2.0F, .right = 8.0F, .bottom = 2.0F};

/// @brief 三档的条宽下限（＝该档内容宽 ＋ 16）与钳位上限（560 dp，E1-a 末句：再宽也不把输入框
///        无限拉长，一屏的字读不过来）。
constexpr float kWideBarWidthDp = 496.0F;
constexpr float kCompactBarWidthDp = 440.0F;
constexpr float kTwoRowBarWidthDp = 184.0F;
constexpr float kBarMaxWidthDp = 560.0F;

/// @brief 换档判据的宽度容差（dp）：浮点盒宽在两次布局之间差一个末位就重建一棵树，代价是夺回
///        输入焦点，而收益是零。钳位与折算都是浮点，故这一档必须留。
constexpr float kBarWidthEpsDp = 0.5F;

/// @brief 码点接收端：把解出的码点依次收进串里（与视口侧同名件同形——那是匿名命名空间里的
///        文件内件，跨 TU 复用就要把它提进公共头，而本件只需要一次 UTF-8 → 码点的转换）。
class StringCollector final : public term::CodePointSink {
  public:
    explicit StringCollector(std::u32string &into) : into_(&into) {}

    auto on_code_point(char32_t code_point) -> void override { into_->push_back(code_point); }

  private:
    std::u32string *into_ = nullptr;
};

/// @brief 框架文本事件给的 UTF-8 → 码点流（`SearchQuery::text` 的坐标空间）。
[[nodiscard]] auto to_code_points(std::string_view utf8) -> std::u32string {
    std::u32string out;
    out.reserve(utf8.size());  // 纯 ASCII 时即为上界
    StringCollector collector{out};
    term::Utf8Decoder decoder;
    decoder.feed(std::as_bytes(std::span<const char>{utf8.data(), utf8.size()}), collector);
    return out;
}

/// @brief 查询文本 → 输入框吃的 UTF-8（重开与换档时把已存的文本种回去）。
[[nodiscard]] auto to_utf8(const std::u32string &code_points) -> std::string {
    std::string out;
    // 码点是本件自己从 UTF-8 解出来的，故「不可表示码点数」那个返回值结构上没有消费者。
    (void)term::encode_utf8(code_points, out);
    return out;
}

/// @brief 焦点 pane 的**内容盒**（窗口逻辑 dp）：可视区四周内边距扣掉之后的那一块。
///
/// 判据 A1-a 的锚点与 E 段的钳位都写的是「内容盒」而不是控件盒：内边距那一带是底色 chrome，
/// 把条体压上去就等于把浮层画在留白里；而格宽与内边距只有视口知道，故量从这里取（裁决 7.25②
/// 的同一分工）。取不到 `window_bounds()`（未测量 / `show` 为假）时回空——猜一个坐标就是把浮层
/// 画在错误的位置上。
[[nodiscard]] auto pane_content_box(const TerminalView &view) -> std::optional<aurora::Rect> {
    const std::optional<aurora::Rect> box = view.window_bounds();
    if (!box.has_value()) {
        return std::nullopt;
    }
    const double padding = view.grid_geometry().padding;
    return aurora::Rect{
        .origin = aurora::Point{.x = box->origin.x + static_cast<float>(padding),
                                .y = box->origin.y + static_cast<float>(padding)},
        .size = aurora::Size{.width = box->size.width - static_cast<float>(2.0 * padding),
                             .height = box->size.height - static_cast<float>(2.0 * padding)},
    };
}

/// @brief 输入框的私有子类：只为把 `Shift+Enter` 分给「后退」那一档（D1-a）。
///
/// 框架 `TextInput::on_key_event` 的 Enter 分支只看键码、不看 Shift，而 `set_on_submit` 的回调
/// 不带修饰位，故后退档在公共 API 上无处可取——只能在基类之前认领这一次 `Down`。与面板的
/// `BlurCommitText` 同档（裁决 7.52 的 S14：失焦才提交也是这么接的）。
class SearchInput final : public aurora::TextInput {
  public:
    /// @brief 以属性聚合与 Enter 回调构造。
    /// @param props 输入框属性（初值、占位与字号）。
    /// @param on_enter Enter 回调，形参是「是否按住 Shift」。
    SearchInput(const aurora::TextInputProps &props, std::function<void(bool shift)> on_enter)
        : aurora::TextInput(props), on_enter_(std::move(on_enter)) {}

    /// @brief 键盘派发：Enter 由本件吃掉并转成「前进 / 后退」，其余一律交回基类编辑逻辑。
    /// @param e 键盘事件（消费时置 `is_handled`）。
    auto on_key_event(aurora::KeyEvent &e) -> void override {
        if (e.action == aurora::KeyAction::Down && e.key == static_cast<int>(aurora::KeyCode::Enter) &&
            is_focused()) {
            const bool shift = (e.modifiers & aurora::ModifierKey::Shift) != 0U;
            e.is_handled = true;  // 认领：基类那条不辨 Shift 的提交路径因此不会被走到
            on_enter_(shift);
            return;
        }
        aurora::TextInput::on_key_event(e);
    }

  private:
    std::function<void(bool shift)> on_enter_;
};

/// @brief 把共享指针包成节点（本文件的每一棵树都这么交，与面板侧同一形态）。
template <typename W>
[[nodiscard]] auto node(std::shared_ptr<W> widget) -> aurora::Node {
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(widget))};
}

}  // namespace

auto SearchOverlay::tier_for(float box_width) -> std::pair<SearchTier, float> {
    // E 段的钳位是同一个算式（`W = clamp(box 宽 − 16, 本档下限, 560)`），差别只在下限取哪一档。
    // 阈值就是「该档下限 ＋ 16」，故 E1-a 的 pane ≥ 512 与 E2-a 的 ≥ 456 不是另外两个数而是同
    // 一条算式的两个解；低于两档下限即走 E3，此时钳位取下限，条体可能超出 pane——那是最小合法
    // pane（228 dp，E3-b）之外才发生的形态，本仓不为它写夹取。
    const float usable = box_width - 2.0F * kEdgeInsetDp;
    const auto clamp_width = [usable](float floor_dp) -> float {
        return std::min(std::max(usable, floor_dp), kBarMaxWidthDp);
    };
    if (usable >= kWideBarWidthDp) {
        return {SearchTier::Wide, clamp_width(kWideBarWidthDp)};
    }
    if (usable >= kCompactBarWidthDp) {
        return {SearchTier::Compact, clamp_width(kCompactBarWidthDp)};
    }
    return {SearchTier::TwoRow, clamp_width(kTwoRowBarWidthDp)};
}

auto SearchOverlay::build_bar(SearchTier tier, float width) -> aurora::Node {
    const SettingsChrome chrome = settings_chrome();
    const SearchQuery &query = view_.search_query();
    const bool nav = view_.search_matches() != nullptr && view_.search_matches()->count() > 0;

    // —— 输入框：本件的唯一文本入口，初值取自视口那份查询（F1-c 的「重开保留文本」）——
    auto input = std::make_shared<SearchInput>(
        aurora::TextInputProps{.value = to_utf8(query.text), .placeholder = settings_label("search.placeholder")},
        [this](bool shift) -> void { on_enter(shift); });
    // chrome 交接（裁决 7.58 的同一判法：框架输入类控件的缺省色是浅色档，深色条体上要自己交）
    input->set_text_color(chrome.text);
    input->set_placeholder_color(chrome.text_dim);
    input->set_background(chrome.control_bg);
    input->set_focused_background(chrome.control_bg);  // 聚焦那一瞬不能白底白字（7.58① 的实测病灶）
    input->set_border_color(chrome.card_line);
    input->set_focused_border_color(chrome.accent);
    input->set_border_width(1.0F);
    input->set_cursor_color(chrome.text);
    input->set_padding(kInputPadding);
    input->set_on_changed([this](const std::string &text) -> void { on_text_changed(text); });
    // 刻意**不装** `set_on_submit`：那条回调拿不到 Shift，装了也分不出前进与后退（见 `SearchInput`）。
    input->modifier.set(aurora::Modifier{}.height(kBarItemHeightDp).expand());

    // —— 计数槽（B0 ~ B5）：定宽、右对齐，于是「按 Enter 应用」这种长串是被钳住的而不是把条挤走 ——
    const float count_width = tier == SearchTier::Compact ? kCountCompactWidthDp : kCountWidthDp;
    auto count = std::make_shared<aurora::Text>(aurora::TextProps{
        .content = count_text(),
        .text_color = chrome.text,
        .text_align = aurora::TextAlign::Right,
    });
    count->modifier.set(aurora::Modifier{}.width(count_width).height(kBarItemHeightDp));

    // —— 两枚 chip：可视标签是 ASCII 符号、朗读标签是中文词条（A1-d + 文件头⑤）——
    const auto make_chip = [this, &chrome](std::shared_ptr<aurora::Button> &slot, std::string_view symbol,
                                           std::string_view a11y_key, bool on) -> aurora::Node {
        auto chip = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = std::string(symbol),
            .color = on ? chrome.accent : chrome.control_bg,
            .on_color = on ? chrome.window_bg : chrome.text_dim,
            .corner_radius = 4.0F,
            .padding = kChipPadding,
            .border_color = chrome.card_line,
            .border_width = 1.0F,
            .min_width = kChipWidthDp,
        });
        // 朗读名交**已解析**的显示串：`set_accessibility_label` 收 `std::string`，而 `tr()` 造的实例
        // 那份 `text` 恒空（裁决 7.59⑥ 的失败模式），交过去就是一枚没有名字的按钮。
        chip->set_accessibility_label(settings_label(a11y_key));
        chip->modifier.set(aurora::Modifier{}.height(kBarItemHeightDp));
        slot = std::move(chip);
        return node(slot);
    };

    // —— 跳转钮：E1 用中文词条、E2 换成 `↑` / `↓`，E3 干脆不摆（判据 E3-a 末句）——
    // 可见标签按档位取两种形态之一：E1 交 `LocalizedString` 由框架就地查表（G28 回货后的口径），
    // E2 是符号字面值，查表路径对它没有意义故直接交 `std::string`。
    const auto make_jump = [this,
                            &chrome](std::shared_ptr<aurora::Button> &slot, aurora::LocalizedString label,
                                     std::string a11y, float min_width, bool enabled,
                                     SearchDirection direction) -> aurora::Node {
        auto button = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = std::move(label),
            .color = chrome.control_bg,
            .on_color = chrome.text,
            .corner_radius = 4.0F,
            .padding = kChipPadding,
            // 无可跳的匹配即灰置（A2-b 的通则，裁决 7.81）：禁用态在构造期一次交进属性，故不产
            // 生第二次 `set_enabled()` 的无条件标脏。
            .enabled = enabled,
            .border_color = chrome.card_line,
            .border_width = 1.0F,
            .disabled_color = chrome.control_bg,
            .disabled_text_color = chrome.text_dim,
            .min_width = min_width,
        });
        button->set_accessibility_label(std::move(a11y));
        button->set_on_click([this, direction]() -> void { view_.advance_search(direction); });
        button->modifier.set(aurora::Modifier{}.height(kBarItemHeightDp));
        slot = std::move(button);
        return node(slot);
    };

    auto close_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_text("search.close"),
        .color = chrome.control_bg,
        .on_color = chrome.text,
        .corner_radius = 4.0F,
        .padding = kChipPadding,
        .border_color = chrome.card_line,
        .border_width = 1.0F,
        .min_width = kCloseWidthDp,
    });
    close_button->set_on_click([this]() -> void { close(); });
    close_button->modifier.set(aurora::Modifier{}.height(kBarItemHeightDp));
    close_ = std::move(close_button);

    std::vector<aurora::Node> children;
    children.push_back(node(input));
    children.push_back(node(count));
    children.push_back(make_chip(case_toggle_, "Aa", "search.toggle_case", query.case_sensitive));
    children.push_back(make_chip(regex_toggle_, ".*", "search.toggle_regex", query.regex));
    if (tier != SearchTier::TwoRow) {
        // CJK-LITERAL: 上屏文案 - E2 档的跳转钮按判据文 A1-d / E2-a 就是这两个箭头符号，40 dp 的宽度
        // 装不下中文词条，而朗读名另经 `search.prev` / `search.next` 词条给出。
        if (tier == SearchTier::Compact) {
            children.push_back(make_jump(prev_, aurora::LocalizedString{"↑"}, settings_label("search.prev"),
                                         kJumpIconWidthDp, nav, SearchDirection::Backward));
            children.push_back(make_jump(next_, aurora::LocalizedString{"↓"}, settings_label("search.next"),
                                         kJumpIconWidthDp, nav, SearchDirection::Forward));
        } else {
            children.push_back(make_jump(prev_, settings_text("search.prev"), settings_label("search.prev"),
                                         kJumpTextWidthDp, nav, SearchDirection::Backward));
            children.push_back(make_jump(next_, settings_text("search.next"), settings_label("search.next"),
                                         kJumpTextWidthDp, nav, SearchDirection::Forward));
        }
    } else {
        prev_.reset();
        next_.reset();
    }
    children.push_back(node(close_));

    case_toggle_->set_on_click([this]() -> void {
        apply_query(!view_.search_query().case_sensitive, view_.search_query().regex);
    });
    regex_toggle_->set_on_click([this]() -> void {
        apply_query(view_.search_query().case_sensitive, !view_.search_query().regex);
    });
    input_ = std::move(input);
    count_ = std::move(count);

    // 条体的宽与高必须由修饰链显式下达：`Popup::on_layout` 按宽松约束测内容（文件头①），而 E 段
    // 断的是控件盒宽度。链序是「先压入者靠外」，故 `width` / `height` 在 `padding` 之前——总尺寸等
    // 于这两个数，内容区再按内边距收缩，正好对上「内容宽 ＋ 16 ＝ 条宽」那条加和式。
    const auto chain = [width, tier, &chrome]() -> aurora::Modifier {
        const float height = tier == SearchTier::TwoRow ? kTwoRowBarHeightDp : kBarHeightDp;
        return aurora::Modifier{}.width(width).height(height).padding(kBarPadding)
            .background(chrome.card_bg, 8.0F)
            .border(1.0F, chrome.card_line);
    };

    if (tier == SearchTier::TwoRow) {
        // 两行档：第一行是输入框 + 关闭（它决定 168 dp 的内容宽），第二行是计数 + 两枚 chip。
        // 跳转钮不出现（E3-a 末句），因为窄到这一档时「上一个 / 下一个」四个字的宽度就是溢出本身。
        std::vector<aurora::Node> row_one;
        row_one.push_back(node(input_));
        row_one.push_back(node(close_));
        auto top = std::make_shared<aurora::Row>(aurora::RowProps{
            .children = std::move(row_one),
            .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
            .gap = kItemGapDp,
        });
        top->modifier.set(aurora::Modifier{}.height(kBarItemHeightDp));

        std::vector<aurora::Node> row_two;
        row_two.push_back(node(count_));
        row_two.push_back(node(case_toggle_));
        row_two.push_back(node(regex_toggle_));
        auto bottom = std::make_shared<aurora::Row>(aurora::RowProps{
            .children = std::move(row_two),
            .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
            .gap = kItemGapDp,
        });
        bottom->modifier.set(aurora::Modifier{}.height(kBarItemHeightDp));

        auto bar = std::make_shared<aurora::Column>(aurora::ColumnProps{
            .children = {node(std::move(top)), node(std::move(bottom))},
            .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Stretch},
            .gap = 0.0F,  // 66 dp ＝ 11 + 22 + 22 + 11：两行之间不再留缝，否则条高对不上判据
        });
        bar->modifier.set(chain());
        return node(std::move(bar));
    }

    auto bar = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = kItemGapDp,
    });
    bar->modifier.set(chain());
    return node(std::move(bar));
}

SearchOverlay::SearchOverlay(TerminalView &view, aurora::OverlayHost &host, aurora::ShortcutRegistry &shortcuts,
                             aurora::FocusManager &focus)
    : view_(view), host_(host), shortcuts_(shortcuts), focus_(focus) {
    popup_ = std::make_shared<aurora::Popup>();
    popup_->set_on_close([this]() -> void { on_popup_closed(); });
    // 建成即登记、条体留到第一次 `open()`：`Popup` 的四个入口都在 `!open_ || !child_` 上早返回，
    // 故一只未打开且无内容的浮层是哑节点，而视口的懒建（F1-c）正是靠这一点。
    overlay_index_ = host_.add_overlay(node(popup_));
}

SearchOverlay::~SearchOverlay() {
    // 裁决 7.67 的次序：先收还在世的作用域与快捷键，再放本件自持的派生控件句柄，最后摘浮层。
    // 反过来（先摘树后放句柄）会让那些控件落在框架「活在容器之外被摘走」那一档而逐子告警。
    // 「还在开着」是这两件事唯一的前提：没开过就没压过作用域、也没登记 `Escape`，而深度读数在
    // 从未 `open()` 时还是 0，此时祖先（设置面板的浮层）压着的作用域会被这一句错弹掉。
    if (is_open()) {
        if (focus_.scope_depth() > scope_depth_before_) {
            focus_.pop_scope();
        }
        shortcuts_.remove(escape_binding_);
    }
    input_.reset();
    count_.reset();
    case_toggle_.reset();
    regex_toggle_.reset();
    prev_.reset();
    next_.reset();
    close_.reset();
    popup_.reset();
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
    }
}

auto SearchOverlay::open() -> void {
    if (is_open()) {
        sync_geometry();  // 重复打开只重定位：不重复压作用域，也不重建条体（输入框里的文本要留着）
        return;
    }
    const std::optional<aurora::Rect> box = pane_content_box(view_);
    if (!box.has_value()) {
        AURORA_LOG_WARN("search overlay: pane has no measured window box yet, not opening");
        return;  // 判据 A1-a 的锚点取不到就不弹，猜坐标等于把浮层画在错误的位置上
    }
    const std::pair<SearchTier, float> tier_and_width = tier_for(box->size.width);
    retier(tier_and_width.first, tier_and_width.second);
    anchor_ = anchor_for(*box);

    // 打开前的深度是对账的基准（文件头③）：`Popup::open_at` 只在派发栈内才压作用域，故栈外打开时
    // 由本件补压那一次——#117 启动对话框同一处置（裁决 7.68⑤）。
    scope_depth_before_ = focus_.scope_depth();
    popup_->open_at(anchor_);
    if (focus_.scope_depth() == scope_depth_before_) {
        focus_.push_scope(popup_.get());
    }

    // 重开那一帧的提示档（F1-c：保留文本但**不**自动重扫，于是此刻画面上的高亮不属于框里那串）。
    reopen_hint_ = view_.search_pending_submit();

    // 挂 Global 而不是 Focus：与设置面板同一条理由（S2①）——浮层里的控件都可获焦，焦点落在
    // 遮罩那一层时 Focus 档反而失灵。关闭即解绑（F2-a，解绑在 `on_popup_closed()`），留着就会
    // 静默吞掉发往会话的 `Esc`。
    escape_binding_ = shortcuts_.add(aurora::KeyCombo(aurora::ModifierKey::None, aurora::KeyCode::Escape),
                                     [this]() -> void { handle_escape(); }, aurora::ShortcutScope::Global,
                                     "search.close");

    // `push_scope` 会自动把焦点移进子树首个可聚焦控件，而那未必是输入框（chip 与按钮也在树里），
    // 故「打开即得焦点」（§5 第 1 条）这一句要自己写出来。
    focus_.set_focus(input_.get());
    sync_state();
}

auto SearchOverlay::close() -> void {
    popup_->close();  // 三条真路径都汇到这里，收口在 `on_popup_closed()`；未开着时框架自己守卫 `open_`
}

auto SearchOverlay::sync_state() -> void {
    if (!is_open()) {
        return;
    }
    refresh_toggles();

    const std::string shown = count_text();
    // 以控件自己的现值当「上一次写过什么」：计数有六档措辞，而复制一份字符串就是第二份状态。
    if (count_ != nullptr && count_->content.get().text != shown) {
        count_->set_content(shown);
        count_->mark_needs_paint();  // `Text::set_content` 不标脏（文件头的三处写入口之一）
    }

    const SearchMatches *matches = view_.search_matches();
    const bool nav = matches != nullptr && matches->count() > 0;
    if (painted_nav_.has_value() && *painted_nav_ == nav) {
        return;
    }
    painted_nav_ = nav;
    if (prev_ != nullptr && next_ != nullptr) {  // 两行档没有这两枚（E3-a）
        prev_->set_enabled(nav);
        next_->set_enabled(nav);
    }
}

auto SearchOverlay::sync_geometry() -> void {
    if (!is_open()) {
        return;  // 未开时档位与锚点留到下一次 `open()` 现算，于是 resize 期间不会有半档状态
    }
    const std::optional<aurora::Rect> box = pane_content_box(view_);
    if (!box.has_value()) {
        return;
    }
    const std::pair<SearchTier, float> tier_and_width = tier_for(box->size.width);
    if (tier_and_width.first != tier_ || std::abs(tier_and_width.second - bar_width_) > kBarWidthEpsDp) {
        retier(tier_and_width.first, tier_and_width.second);
    }
    const aurora::Point next_anchor = anchor_for(*box);
    if (next_anchor.x != anchor_.x || next_anchor.y != anchor_.y) {
        anchor_ = next_anchor;
        popup_->open_at(anchor_);  // 已开着时这一句只改锚点：那一次 `push_scope` 有 `!open_` 守卫
    }
}

auto SearchOverlay::count_text() const -> std::string {
    const SearchQuery &query = view_.search_query();
    if (query.text.empty()) {
        // CJK-LITERAL: 上屏文案 - B0 的占位档是一枚全角破折号，词条表里没有它的位置（判据文 §4
        // 第 6 条只列那十一条），而英文 `-` 在等宽条体上读起来像负号而不是「还没输入」。
        return "—";
    }
    if (reopen_hint_) {
        return settings_label("search.reopen_hint");
    }
    // 只在正则档说「按 Enter 应用」：字面量档每键都扫，那一串会在每次击键之后闪一下（B5 的两档
    // 措辞因此不是同义的，裁决 7.81）。
    if (query.regex && view_.search_pending_submit()) {
        return settings_label("search.pending_enter");
    }
    if (view_.search_pattern_invalid()) {
        return settings_label("search.count_invalid");
    }
    const SearchMatches *matches = view_.search_matches();
    if (matches == nullptr || matches->count() == 0) {
        return settings_label("search.count_none");
    }
    const std::string denominator = matches->truncated()
                                        ? settings_label("search.count_cap",
                                                         {aurora::LocalizedString{std::to_string(kMaxSearchMatches)}})
                                        : std::to_string(matches->count());
    // 游标为空时只给分母：新扫出的表本来就没有游标，挂一个 `1/` 就是谎报「当前命中在第一段上」。
    const std::optional<std::size_t> cursor = matches->cursor();
    if (!cursor.has_value()) {
        return denominator;
    }
    return std::to_string(*cursor + 1) + "/" + denominator;
}

auto SearchOverlay::retier(SearchTier next, float width) -> void {
    if (next == tier_ && std::abs(width - bar_width_) <= kBarWidthEpsDp && input_ != nullptr) {
        return;  // 同档同宽不动树：打字、滚动与逐帧同步都不该重建一棵控件树
    }
    tier_ = next;
    bar_width_ = width;
    popup_->set_content(build_bar(next, width));  // `set_content` 不标脏，`open_at` 才标（7.59 的实测）
    // 换档会把用户正在编辑的那只输入框换成新的一只，焦点因此丢掉；不夺回来的话，换档之后的按键
    // 就发往会话（代价登记于裁决 7.81，与「resize 晚一帧换档」同一条）。
    if (is_open()) {
        focus_.set_focus(input_.get());
    }
}

auto SearchOverlay::anchor_for(const aurora::Rect &box) const -> aurora::Point {
    // `Popup` 的锚点是内容盒的**左上角**（其 `content_bounds()` ＝ `{origin = anchor_, size = 实测}`），
    // 故「右上角往内 8 dp」要先把条宽减掉，否则条体有一半伸出 pane 右缘（实测口径见裁决 7.81）。
    return aurora::Point{
        .x = box.right() - kEdgeInsetDp - bar_width_,
        .y = box.origin.y + kEdgeInsetDp,
    };
}

auto SearchOverlay::handle_escape() -> void {
    close();
}

auto SearchOverlay::on_popup_closed() -> void {
    // 按深度对账而不是按「关闭现场是否在派发栈内」（文件头③）：`Popup::close()` 只在它自己也取到
    // 了焦点管理器时才弹，故四条开合组合里恰好有两条会漏——本件按打开前的读数补齐，四种组合都不漏
    // 也不双弹。
    if (focus_.scope_depth() > scope_depth_before_) {
        focus_.pop_scope();
    }
    shortcuts_.remove(escape_binding_);
    escape_binding_ = 0;  // 头里那句「0 ＝ 未登记」是本件对该字段唯一的读法，解绑后必须回到哨兵档
    view_.close_search();  // 判据 D7：清高亮而**不**清查询文本与回看位置
}

auto SearchOverlay::refresh_toggles() -> void {
    const SettingsChrome chrome = settings_chrome();
    const SearchQuery &query = view_.search_query();
    const auto paint_chip = [](const std::shared_ptr<aurora::Button> &chip, bool on,
                               const SettingsChrome &colors) -> void {
        chip->background(on ? colors.accent : colors.control_bg);
        chip->text_color(on ? colors.window_bg : colors.text_dim);
        chip->mark_needs_paint();  // 这两处写入口只改值不标脏（文件头的三处之一）
    };
    if (case_toggle_ != nullptr && query.case_sensitive != painted_case_) {
        painted_case_ = query.case_sensitive;
        paint_chip(case_toggle_, painted_case_, chrome);
    }
    if (regex_toggle_ != nullptr && query.regex != painted_regex_) {
        painted_regex_ = query.regex;
        paint_chip(regex_toggle_, painted_regex_, chrome);
    }
}

auto SearchOverlay::apply_query(bool case_sensitive, bool regex) -> void {
    SearchQuery query = view_.search_query();
    query.text = to_code_points(input_->value());
    query.case_sensitive = case_sensitive;
    query.regex = regex;
    reopen_hint_ = false;  // 用户已经动了查询，B5 的重开提示到此为止
    view_.set_search_query(std::move(query));
    view_.submit_search_query();  // 判据 D4：开关的翻转是提交点之一，正则档因此不必非按 Enter
}

auto SearchOverlay::on_text_changed(const std::string &text) -> void {
    SearchQuery query = view_.search_query();
    query.text = to_code_points(text);
    reopen_hint_ = false;
    view_.set_search_query(std::move(query));  // 字面量档由视口置脏；正则档不动（裁决 7.78①）
}

auto SearchOverlay::on_enter(bool shift) -> void {
    if (view_.search_pending_submit()) {
        reopen_hint_ = false;
        view_.submit_search_query();  // 这一次 Enter 是 B5 的「应用」，不是跳转（判据 D4）
        return;
    }
    view_.advance_search(shift ? SearchDirection::Backward : SearchDirection::Forward);
}

}  // namespace borealis::ui
