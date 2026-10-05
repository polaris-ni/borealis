// ============================================================
// 设置面板的界面腿本体实现（src/ui/settings_panel.cpp）
// ------------------------------------------------------------
// 本文件与 `terminal_view.cpp` / `workspace_view.cpp` 是触达框架 widget 面的三个翻译单元，
// 判断全在纯逻辑件里：行表与取值域来自 `ui::settings_catalog()`，校验与脏标记来自
// `ui::SettingsForm`，搬运来自 `config::form_entries()` / `config::apply_form()`（由装配层经
// `Hooks` 兑现）。本件只做三件事：**排版、把控件回调接到表单、把表单结果接到落盘与广播**。
//
// chrome 色值是本文件内常量的唯一一份，`settings_chrome_theme()` 由同一批常量合成，装配层给
// 场景根装的 `ThemeScope` 取的也是它——界面配色因此不会散成第二处（`workspace_view.cpp` 那三个
// 文件内常量是把手与描边用的，与本表不同用途，本棒不并表也不重构，见裁决 7.56 的代价段）。
// ============================================================

#include "settings_panel.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/core/log.h"
#include "aurora/event/keycode.h"
#include "aurora/modifier/modifier.h"
#include "aurora/render/painter.h"
#include "aurora/widget/button.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/layout_builder.h"
#include "aurora/widget/lazy_list.h"
#include "aurora/widget/radio_spin.h"
#include "aurora/widget/stack.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"

#include "borealis/ui/color_text.h"
#include "settings_i18n.h"

namespace borealis::ui {
namespace {

// ---- chrome 色值（S5①：界面配色不随终端主题联动）----
constexpr aurora::Color kScrim{0, 0, 0, 0x99};             ///< 全屏遮罩：不挡内容但吃掉窗外点击。
constexpr aurora::Color kWindowBg{0x18, 0x19, 0x21};       ///< 场景根底色（ThemeScope 的 background）。
constexpr aurora::Color kCardBg{0x21, 0x22, 0x2C};         ///< 卡片底。
constexpr aurora::Color kNavBg{0x1B, 0x1C, 0x24};          ///< 左导航列底（比卡片暗一档，分出两栏）。
constexpr aurora::Color kCardLine{0x4D, 0x4F, 0x63};       ///< 卡片描边与行区分隔线。
constexpr aurora::Color kAccent{0xBD, 0x93, 0xF9};         ///< 选中页与主按钮。
constexpr aurora::Color kText{0xF8, 0xF8, 0xF2};           ///< 正文。
constexpr aurora::Color kTextDim{0x62, 0x72, 0x80};        ///< 角标、副标题与灰置行。
constexpr aurora::Color kControlBg{0x28, 0x2A, 0x36};      ///< 输入类控件底与次级按钮底。

constexpr float kRowExtentDp = 56.0F;   ///< 行高（判据文 §1 不另立控件高度，故只在排版处出现一次）。
constexpr float kNavWidthDp = 200.0F;   ///< 左导航宽（判据文 §1）。
constexpr float kStatusWidthDp = 176.0F;  ///< 状态列宽：最长角标是「下次会话生效」七字。
constexpr float kCardMaxWidthDp = 1040.0F;
constexpr float kCardMaxHeightDp = 680.0F;
constexpr float kCardMarginDp = 16.0F;  ///< 卡片与窗口边的最小留白。

/// @brief 步进器的档位（自拍项，裁决 7.56）：跨度千级者按百推进，整数档按一推进，
///          实数档在窄跨度（行高、对比度、字距）按 0.1 推进。
[[nodiscard]] auto stepper_step(const SettingsControl &control) -> double {
    const double span = control.numeric.max - control.numeric.min;
    if (span >= 1000.0) {
        return 100.0;
    }
    if (control.domain == ValueDomain::Integral) {
        return 1.0;
    }
    return span >= 60.0 ? 1.0 : 0.1;
}

/// @brief 表单值 → 文本框初值（HEX 走 `ui::color_to_hex`，数字按整数或一位小数呈现）。
[[nodiscard]] auto form_value_as_text(const FormValue *value) -> std::string {
    if (value == nullptr) {
        return {};
    }
    if (const auto color = value->as_color(); color.has_value()) {
        return color_to_hex(*color);
    }
    if (value->is_unset_color()) {
        return {};  // 「未配」在输入框里就是空文本，占位提示另给（A2-b 的两态由按钮与脏标记区分）
    }
    if (const auto text = value->as_text(); text.has_value()) {
        return *text;
    }
    if (const auto integral = value->as_integral(); integral.has_value()) {
        return std::to_string(*integral);
    }
    if (const auto real = value->as_real(); real.has_value()) {
        return std::to_string(*real);
    }
    if (const auto flag = value->as_boolean(); flag.has_value()) {
        return *flag ? "true" : "false";
    }
    return {};
}

/// @brief 文本行的提交接缝：把「失焦或 Enter 才提交」做成类型而不是约定（S14）。
///
/// 框架 `TextInput::on_focus_change` 是 public virtual，故子类可在失焦那一刻交一次文本；Enter 走
/// `on_submit` 回调。逐字符的 `on_changed` **刻意不接**——半截输入（`#12`）因此到不了表单。
class BlurCommitText final : public aurora::TextInput {
public:
    using aurora::TextInput::TextInput;

    /// @brief 提交回调（失焦与 Enter 各调一次；同一文本重复提交由表单的按值脏标记吸收）。
    std::function<void(const std::string &)> commit;

    auto on_focus_change(bool focused) -> void override {
        aurora::TextInput::on_focus_change(focused);
        if (!focused && commit) {
            commit(value());
        }
    }
};

/// @brief 页 → 导航按钮的词条 key（`settings.page.<域>`，与 `SettingsPage` 四档一一对应）。
[[nodiscard]] auto page_title_key(SettingsPage page) -> std::string_view {
    switch (page) {
        case SettingsPage::Appearance:
            return "settings.page.appearance";
        case SettingsPage::Terminal:
            return "settings.page.terminal";
        case SettingsPage::Connection:
            return "settings.page.connection";
        case SettingsPage::Shortcuts:
            return "settings.page.shortcuts";
    }
    return "settings.page.appearance";
}

/// @brief 造一个只读文本片段（标签、值摘要与角标共用）；不定宽、左对齐。
///
/// 交的是**已解析**的显示串而非词条 key：调用点有两类文案（词条与 `color_to_hex` 这类算出来的值）
/// 走同一个入口，故这里不做第二次查表。
[[nodiscard]] auto make_text(const std::string &text, aurora::Color color) -> aurora::Node {
    auto label = std::make_shared<aurora::Text>(aurora::TextProps{.content = text, .text_color = color});
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(label))};
}

}  // namespace

auto settings_chrome_theme() -> aurora::Theme {
    return aurora::Theme{
        .background = kWindowBg,
        .primary = kAccent,
        .on_primary = kWindowBg,
        .text = kText,
        .font = aurora::Font{},
    };
}

SettingsPanel::SettingsPanel(aurora::OverlayHost &host, aurora::ShortcutRegistry &shortcuts, Hooks hooks)
    : host_(host), shortcuts_(shortcuts), hooks_(std::move(hooks)) {}

SettingsPanel::~SettingsPanel() {
    close();
}

auto SettingsPanel::open() -> void {
    if (hooks_.load) {
        form_ = SettingsForm{hooks_.load()};
    }
    open_ = true;
    rebuild_overlay();
    if (escape_binding_ == 0) {
        // 关闭键挂 Global：面板内的控件都可获焦，挂 Focus 反而在焦点落在遮罩层时失灵（S2①）。
        escape_binding_ = shortcuts_.add(aurora::KeyCombo(aurora::ModifierKey::None, aurora::KeyCode::Escape),
                                         [this]() -> void { close(); },
                                         aurora::ShortcutScope::Global,
                                         "settings.close");
    }
}

auto SettingsPanel::close() -> void {
    if (!open_) {
        return;
    }
    open_ = false;
    if (escape_binding_ != 0) {
        shortcuts_.remove(escape_binding_);
        escape_binding_ = 0;
    }
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    status_texts_.clear();
    rows_.clear();
}

auto SettingsPanel::select_page(SettingsPage page) -> void {
    if (page == page_) {
        return;
    }
    page_ = page;
    if (open_) {
        rebuild_overlay();
    }
}

auto SettingsPanel::visible_rows() const -> std::vector<VisibleRow> {
    std::vector<VisibleRow> out;
    out.reserve(rows_.size());
    for (const SettingsControl *control : rows_) {
        const bool editable = is_editable(*control);
        out.push_back(VisibleRow{
            .key = control->key,
            .kind = control->kind,
            .editable = editable,
            .badge = badge_for(*control),
            .summary = editable ? std::string{} : value_summary(*control),
        });
    }
    return out;
}

auto SettingsPanel::commit(std::string_view key, FormValue next) -> CommitIssue {
    const SettingsControl *control = find_settings_control(key);
    if (control == nullptr) {
        return CommitIssue::UnknownKey;
    }
    const CommitOutcome outcome = form_.commit_value(key, std::move(next));
    after_commit(key, *control, outcome.issue);
    return outcome.issue;
}

auto SettingsPanel::commit_text(std::string_view key, std::string_view text) -> CommitIssue {
    const SettingsControl *control = find_settings_control(key);
    if (control == nullptr) {
        return CommitIssue::UnknownKey;
    }
    const CommitOutcome outcome = form_.commit_text(key, text);
    after_commit(key, *control, outcome.issue);
    return outcome.issue;
}

auto SettingsPanel::commit_unset(std::string_view key) -> CommitIssue {
    const SettingsControl *control = find_settings_control(key);
    if (control == nullptr) {
        return CommitIssue::UnknownKey;
    }
    const CommitOutcome outcome = form_.commit_unset_color(key);
    after_commit(key, *control, outcome.issue);
    return outcome.issue;
}

auto SettingsPanel::collect_rows() const -> std::vector<const SettingsControl *> {
    std::vector<const SettingsControl *> out;
    for (const SettingsControl &control : settings_catalog()) {
        if (control.page == page_ && form_.value(control.key) != nullptr) {
            out.push_back(&control);
        }
    }
    return out;
}

auto SettingsPanel::rebuild_overlay() -> void {
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    rows_ = collect_rows();
    status_texts_.assign(rows_.size(), nullptr);

    // 遮罩层：`Canvas` 的自动尺寸会夹到 100×100，故必须 fill_max_size 才铺满整窗（裁决 7.56⑤）。
    auto scrim = std::make_shared<aurora::Canvas>([](aurora::Painter &painter, const aurora::Rect &bounds) -> void {
        painter.fill_rect(bounds, kScrim);
    });
    scrim->modifier.set(aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));

    auto layer = std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(scrim))},
                                  build_card()},
        aurora::Alignment::Center);
    overlay_index_ = host_.add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(layer))});
}

auto SettingsPanel::build_card() -> aurora::Node {
    return aurora::Node{std::make_shared<aurora::LayoutBuilder>(
        [this](const aurora::BuildContext &, const aurora::Constraints &c) -> aurora::Node {
            const float width = std::clamp(c.max.width - 2.0F * kCardMarginDp, 480.0F, kCardMaxWidthDp);
            const float height = std::clamp(c.max.height - 2.0F * kCardMarginDp, 320.0F, kCardMaxHeightDp);

            std::vector<aurora::Node> nav;
            nav.reserve(4U);
            for (const SettingsPage page : {SettingsPage::Appearance,
                                            SettingsPage::Terminal,
                                            SettingsPage::Connection,
                                            SettingsPage::Shortcuts}) {
                auto props = aurora::ButtonProps{};
                props.label = settings_text(page_title_key(page));
                props.color = page == page_ ? kAccent : kNavBg;
                props.on_color = page == page_ ? kWindowBg : kText;
                props.corner_radius = 0.0F;
                props.min_width = kNavWidthDp;
                props.border_width = 0.0F;
                auto button = std::make_shared<aurora::Button>(std::move(props));
                button->set_on_click([this, page]() -> void { select_page(page); });
                nav.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(button))});
            }

            auto nav_column = std::make_shared<aurora::Column>(aurora::ColumnProps{
                .children = std::move(nav),
                .gap = 2.0F,
            });
            nav_column->modifier.set(aurora::Modifier{}.width(kNavWidthDp).fill_max_height().background(kNavBg));

            auto rows = std::make_shared<aurora::LazyList>(
                static_cast<int>(rows_.size()),
                [this](int index) -> aurora::Node { return build_row(static_cast<std::size_t>(index)); },
                kRowExtentDp);
            rows->modifier.set(aurora::Modifier{}.fill_max_width().expand());

            auto body = std::make_shared<aurora::Row>(aurora::RowProps{
                .children = {aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(nav_column))},
                             aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(rows))}},
                .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
                .gap = 0.0F,
            });
            body->modifier.set(aurora::Modifier{}.fill_max_width().expand());

            auto close_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
                .label = settings_text("settings.close"),
                .color = kControlBg,
                .on_color = kText,
                .border_color = kCardLine,
                .border_width = 1.0F,
            });
            close_button->set_on_click([this]() -> void { close(); });

            auto title = make_text(settings_label("settings.title"), kText);
            title.widget().modifier.set(aurora::Modifier{}.expand());  // 标题吃掉剩余宽度，副标题与按钮靠右
            auto subtitle = make_text(settings_label("settings.subtitle"), kTextDim);
            subtitle.widget().modifier.set(aurora::Modifier{}.width(kStatusWidthDp));

            auto header = std::make_shared<aurora::Row>(aurora::RowProps{
                .children = {std::move(title),
                             std::move(subtitle),
                             aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(close_button))}},
                .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
                .gap = 12.0F,
            });
            header->modifier.set(aurora::Modifier{}.fill_max_width().padding(aurora::EdgeInsets{
                .left = 16.0F, .top = 12.0F, .right = 16.0F, .bottom = 12.0F}));

            auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
                .children = {aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(header))},
                             aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(body))}},
                .gap = 0.0F,
            });
            card->modifier.set(aurora::Modifier{}.size(width, height).background(kCardBg, 8.0F).border(
                1.0F, kCardLine));
            // 卡片重建时状态列一律作废：条目构建器会在本帧之后重新登记存活实例的指针。
            status_texts_.assign(rows_.size(), nullptr);
            return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card))};
        })};
}

auto SettingsPanel::build_row(std::size_t ordinal) -> aurora::Node {
    if (ordinal >= rows_.size()) {
        return aurora::Node{};
    }
    const SettingsControl &control = *rows_[ordinal];
    const bool editable = is_editable(control);

    auto label = std::make_shared<aurora::Text>(
        aurora::TextProps{.content = settings_text(control.key), .text_color = editable ? kText : kTextDim});
    label->modifier.set(aurora::Modifier{}.expand());

    auto status = std::make_shared<aurora::Text>(aurora::TextProps{
        .content = badge_for(control), .text_color = kTextDim, .text_align = aurora::TextAlign::Right});
    status->modifier.set(aurora::Modifier{}.width(kStatusWidthDp));
    if (ordinal < status_texts_.size()) {
        status_texts_[ordinal] = status;  // 条目构建器只在序号有效时才登记指针
    }

    auto row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(label))},
                     aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(status))},
                     build_control(control, editable)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 12.0F,
    });
    row->modifier.set(aurora::Modifier{}.fill_max_width().padding(aurora::EdgeInsets{
        .left = 16.0F, .top = 8.0F, .right = 16.0F, .bottom = 8.0F}));
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row))};
}

auto SettingsPanel::build_control(const SettingsControl &control, bool editable) -> aurora::Node {
    const std::string key = control.key;
    const FormValue *value = form_.value(key);

    if (editable) {
        switch (control.kind) {
            case ControlKind::Toggle: {
                const bool on = value != nullptr && value->as_boolean().value_or(false);
                auto sw = std::make_shared<aurora::Switch>(aurora::Reactive<bool>{on});
                // 行高 56 dp 扣掉上下各 8 dp 内边距后给子项的是**紧约束 40 dp**，而开关的滑块直径按
                // 自身盒高算（`bounds.height − 2×inset`），不锁高度就会被拉成 36 dp 的白饼、轨道只剩两侧细边。
                sw->modifier.set(aurora::Modifier{}.height(24.0F));
                sw->set_active_color(kAccent);
                sw->set_inactive_color(kControlBg);
                sw->set_border(kCardLine, 1.0F);
                sw->set_thumb_color(kText);
                sw->set_on_changed(
                    [this, key](bool next) -> void { commit(key, FormValue::boolean(next)); });
                return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(sw))};
            }
            case ControlKind::NumberStep: {
                const double step = stepper_step(control);
                double initial = control.numeric.min;
                if (const auto integral = value != nullptr ? value->as_integral() : std::nullopt;
                    integral.has_value()) {
                    initial = static_cast<double>(*integral);
                } else if (const auto real = value != nullptr ? value->as_real() : std::nullopt;
                           real.has_value()) {
                    initial = *real;
                }
                auto spin = std::make_shared<aurora::SpinBox>(
                    initial, control.numeric.min, control.numeric.max, step);
                spin->set_background(kControlBg);
                spin->set_border_color(kCardLine);
                spin->set_text_color(kText);
                spin->set_arrow_color(kTextDim);
                spin->set_suffix(control.unit);
                spin->set_decimals(step < 1.0 ? 1 : 0);
                spin->set_on_change([this, key, integral = control.domain == ValueDomain::Integral](double next) -> void {
                    commit(key, integral ? FormValue::integral(static_cast<std::int64_t>(next))
                                         : FormValue::real(next));
                });
                return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(spin))};
            }
            case ControlKind::Dropdown: {
                const auto current = value != nullptr ? value->as_text() : std::nullopt;
                int initial = 0;
                for (std::size_t i = 0; i < control.choices.size(); ++i) {
                    if (current.has_value() && control.choices[i] == *current) {
                        initial = static_cast<int>(i);
                        break;
                    }
                }
                auto box = std::make_shared<aurora::Dropdown>(control.choices, initial);
                box->set_box_color(kControlBg);
                box->set_border_color(kCardLine);
                box->set_text_color(kText);
                box->set_arrow_color(kTextDim);
                box->set_accent_color(kAccent);
                box->set_on_change([this, key, choices = control.choices](int index) -> void {
                    if (index >= 0 && static_cast<std::size_t>(index) < choices.size()) {
                        commit(key, FormValue::text(choices[static_cast<std::size_t>(index)]));
                    }
                });
                return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(box))};
            }
            case ControlKind::HexInput:
            case ControlKind::OptionalHexInput:
            case ControlKind::TextInput: {
                auto box = std::make_shared<BlurCommitText>();
                box->set_value(form_value_as_text(value));
                box->set_background(kControlBg);
                // 框架的聚焦态底色缺省是近白 {245,248,255}，而本件文本色近白：不显式给就聚焦即白底白字，
                // 故聚焦态只靠描边分档。
                box->set_focused_background(kControlBg);
                box->set_border_color(kCardLine);
                box->set_focused_border_color(kAccent);
                box->set_text_color(kText);
                box->set_cursor_color(kText);
                box->set_placeholder_color(kTextDim);
                if (control.kind == ControlKind::OptionalHexInput) {
                    box->set_placeholder(settings_label("settings.action.unset"));
                }
                box->set_on_submit([this, key](const std::string &text) -> void { commit_text(key, text); });
                box->commit = [this, key](const std::string &text) -> void { commit_text(key, text); };
                box->modifier.set(aurora::Modifier{}.width(control.kind == ControlKind::TextInput ? 280.0F
                                                                                                    : 120.0F));

                std::vector<aurora::Node> children;
                children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(box))});
                if (control.kind == ControlKind::OptionalHexInput) {
                    // 「未配」是一次显式动作而不是空串（裁决 7.27③）：空文本既可能是未配也可能是畸形值，
                    // 只有这个按钮把槽位置成未配。
                    auto unset = std::make_shared<aurora::Button>(aurora::ButtonProps{
                        .label = settings_text("settings.action.unset"),
                        .color = kControlBg,
                        .on_color = kText,
                        .border_color = kCardLine,
                        .border_width = 1.0F,
                        .min_width = 72.0F,
                    });
                    unset->set_on_click([this, key]() -> void { commit_unset(key); });
                    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(unset))});
                }
                auto group = std::make_shared<aurora::Row>(aurora::RowProps{
                    .children = std::move(children),
                    .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
                    .gap = 6.0F,
                });
                return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(group))};
            }
            case ControlKind::ThemePicker:
            case ControlKind::SwatchGrid:
            case ControlKind::FontDropdown:
            case ControlKind::FamilyList:
            case ControlKind::ReadOnlyTable:
                break;  // 专用控件形态不可交互（is_editable 已先判掉），落下面的占位分支。
        }
    }
    // 占位行：如实显示当前值，不给一个「点了没反应」的控件（S9 / D3-a 的同一口径）。
    return make_text(value_summary(control), kTextDim);
}

auto SettingsPanel::after_commit(std::string_view key, const SettingsControl &control, CommitIssue issue) -> void {
    const std::size_t ordinal = [this, &key]() -> std::size_t {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i]->key == key) {
                return i;
            }
        }
        return rows_.size();
    }();
    if (issue != CommitIssue::None) {
        // 校验不过时表单里的值一字未动，状态列改成失败原因（词条由 `settings_issue_text` 给）。
        set_status(ordinal, settings_issue_text(issue));
        return;
    }
    set_status(ordinal, badge_for(control));
    if (!form_.is_dirty(key)) {
        return;  // 改了又改回来：既不落盘也不广播
    }
    if (hooks_.persist) {
        if (const auto reason = hooks_.persist(form_); reason.has_value()) {
            // 落盘失败时不调 `note_persisted()`：脏标记留着，面板因此不会谎报已存（`store.h` 文件头那条）。
            AURORA_LOG_ERROR("settings", "persist failed, keep the form dirty: ", *reason);
            return;
        }
        form_.note_persisted();
    }
    if (apply_scope(control) == ApplyScope::PersistAndApplyNow && hooks_.broadcast) {
        hooks_.broadcast(form_);
    }
}

auto SettingsPanel::set_status(std::size_t ordinal, const aurora::LocalizedString &text) -> void {
    if (ordinal >= status_texts_.size() || status_texts_[ordinal] == nullptr) {
        return;  // 该行此刻不在可见窗口内、条目已被回收；下次进入窗口时按角标重建
    }
    status_texts_[ordinal]->content = text;
    status_texts_[ordinal]->mark_needs_paint();
}

auto SettingsPanel::badge_for(const SettingsControl &control) -> std::string {
    std::string out;
    if (control.consumer != ConsumerStatus::Wired) {
        out = settings_label("settings.badge.deferred");
    }
    if (control.effect == EffectLevel::NextSession) {
        if (!out.empty()) {
            out += " · ";  // B3-a：两条标签不冲突，故可以并列呈现
        }
        out += settings_label("settings.badge.next_session");
    }
    return out;
}

auto SettingsPanel::value_summary(const SettingsControl &control) const -> std::string {
    const FormValue *value = form_.value(control.key);
    if (value == nullptr) {
        return {};
    }
    if (const auto table = value->as_color_table(); table.has_value()) {
        std::string out;
        for (std::size_t i = 0; i < table->size(); ++i) {
            out += color_to_hex((*table)[i]);
            if (i + 1U < table->size()) {
                out += ' ';
            }
        }
        return out;
    }
    if (const auto chain = value->as_text_list(); chain.has_value()) {
        std::string out;
        for (const std::string &family : *chain) {
            if (!out.empty()) {
                out += ", ";
            }
            out += family;
        }
        return out;
    }
    if (const auto overrides = value->as_overrides(); overrides.has_value()) {
        return std::to_string(overrides->size());
    }
    return form_value_as_text(value);
}

auto SettingsPanel::is_editable(const SettingsControl &control) -> bool {
    if (control.consumer == ConsumerStatus::Absent) {
        return false;  // S15 第一档：有键、全仓无消费方 → 灰置 + 角标
    }
    switch (control.kind) {
        case ControlKind::Toggle:
        case ControlKind::NumberStep:
        case ControlKind::Dropdown:
        case ControlKind::HexInput:
        case ControlKind::OptionalHexInput:
        case ControlKind::TextInput:
            return true;
        case ControlKind::ThemePicker:
        case ControlKind::SwatchGrid:
        case ControlKind::FontDropdown:
        case ControlKind::FamilyList:
        case ControlKind::ReadOnlyTable:
            return false;  // 专用控件随后续棒落地（本棒先如实显示当前值而不是给个死控件）
    }
    return false;
}

}  // namespace borealis::ui
