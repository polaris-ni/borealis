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
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/core/log.h"
#include "aurora/event/keycode.h"
#include "aurora/i18n/string_table.h"
#include "aurora/modifier/modifier.h"
#include "aurora/render/font_engine.h"
#include "aurora/render/painter.h"
#include "aurora/widget/button.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/layout_builder.h"
#include "aurora/widget/radio_spin.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/stack.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"

#include "borealis/ui/color_text.h"
#include "settings_i18n.h"
#include "settings_preview.h"

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

/// @brief 预览横条的高度（判据文 F-e，人已拍板的落位：卡片底部一条横条，而不是稿面原先的「右侧」）。
///
/// 做成常量而非按内容算高：横条与卡片总高解耦——卡片尺寸由外层 `LayoutBuilder` 显式钳定，多这一条
/// 子节点只从行区那一段里扣，行区因而已是 `expand()` 的滚动容器，扣完仍可滚。
constexpr float kPreviewHeightDp = 140.0F;

// ---- 外观页三个区段的排版量（判据文 §4 A1-a / A2-a / A5-a，尺寸按 `UI_SETTINGS.draft.svg` 量得）----
constexpr float kThemeCardHeightDp = 64.0F;
constexpr std::size_t kThemeCardPerRow = 4;  ///< 每行几张卡：`Scroll` 只竖向滚动，八套必须换行而非挤成一横排。
constexpr float kSwatchTileWidthDp = 48.0F;
constexpr float kSwatchTileHeightDp = 26.0F;
constexpr std::size_t kSwatchPerRow = 8;  ///< 16 格分两行。
constexpr float kSectionGapDp = 6.0F;     ///< 区段内行与行的间距。

constexpr float kChainItemHeightDp = 32.0F;   ///< 链条目行高（列表给子项的是紧约束宽 + 无限高，故条目自锁）。
constexpr float kChainOrdinalWidthDp = 24.0F; ///< 序号列宽：两位足够（链上限是个位数）。
constexpr float kChainButtonWidthDp = 36.0F;  ///< 上移 / 下移 / 移除三枚按钮的宽度。
constexpr float kChainFilterWidthDp = 180.0F;  ///< 过滤框宽。
constexpr float kChainHandleBandDp = 48.0F;    ///< 列表自留的手柄带（框架 `AURORA_HANDLE_BAND` 的同值口径）。
constexpr std::size_t kChainCandidateRows = 6; ///< 候选池一次列几档（常驻按钮数，多余的那几档隐藏）。

/// @brief 回退链的容量上限：框架侧截断的那个数字，本件取同一个真值源而不是另写一个 8。
///
/// 截断发生在框架的 `TextLayoutOpts::with_fallback_chain()`（`render/font_engine.h`）且对用户不可见，
/// 故面板必须在界面上自己判、自己说（判据文 A5-a；裁决 7.50 记的那条代价）。
constexpr std::size_t kChainCapacity = aurora::render::AURORA_TEXT_FALLBACK_CHAIN_MAX;

/// @brief 色板格数：从 `PaletteSpec::basic` 的长度取，而不是在本件再写一遍 16。
///
/// 判据 A2-a 的「16 格」与表单的 `TableSizeWrong` 都判该长度，此处硬写一个数字就是第三份真值源。
/// 取一份缺省 `PaletteSpec` 来读长度（MSVC 的 `std::array::size` 不是静态成员，不能按类型直调）。
const std::size_t kPaletteSlotCount = PaletteSpec{}.basic.size();

/// @brief 三个区段各自归属的行键：卡片、色板与链都是**一行**的区段（角标、状态列与提交都落在那一行上）。
constexpr std::string_view kThemeKey{"appearance.theme"};
constexpr std::string_view kPaletteKey{"appearance.palette.basic"};
constexpr std::string_view kChainKey{"appearance.font_fallback_chain"};

/// @brief `ui::RgbaColor` → 框架颜色（逐字段搬，alpha 参与）。
///
/// 与 `terminal_view.cpp` 的同名件各自一份而非共享：那是绘制侧的文件内私有实现，本件按裁决 D1①
/// 不去动它，两处都没有公共头可放（公共头上放一个 POD 互转函数就是给 widget 层开公共接缝）。
[[nodiscard]] auto to_color(const RgbaColor &color) noexcept -> aurora::Color {
    return aurora::Color{color.red, color.green, color.blue, color.alpha};
}

/// @brief 一张主题卡的四个样例色：次序＝前 / 背 / 光标 / `basic[1]`（文件头②）。
///
/// 绘制闭包与 `theme_cards()` 共用本函数，故「界面上看到的四格」与「用例读到的四格」不会各算一遍。
/// 光标未配时按裁决 7.25③ 的算式回落前景色，与绘制侧同源。
[[nodiscard]] auto theme_samples(const PaletteSpec &palette) noexcept -> std::array<RgbaColor, 4> {
    return {palette.default_foreground,
            palette.default_background,
            palette.cursor_color.value_or(palette.default_foreground),
            palette.basic[1]};
}

/// @brief 色板的可选槽 → 表单值：没配就是一张「未配」，不是黑色（A2-b 的两态在值上也要不同）。
[[nodiscard]] auto slot_value(const std::optional<RgbaColor> &color) -> FormValue {
    return color.has_value() ? FormValue::color(*color) : FormValue::unset_color();
}

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

/// @brief ASCII 小写折叠（只对 A–Z 生效，其余字节原样）。
///
/// 族名比较走折叠而不是逐字节：框架的族名匹配本身是逐字节精确、区分大小写的（裁决 7.46③ 那条实测，
/// 拼错的族名静默回落），故**候选侧**必须给出与框架不同的更宽口径——用户在过滤框里敲 `cas` 时，把
/// `Cascadia Code` 排除掉是不合直觉的。本函数只服务候选池的匹配与「是否已在链内」的判定，不改写也不
/// 重新拼写族名本身（交进表单的那一串永远是目录里的原样）。
[[nodiscard]] auto ascii_lower(std::string_view text) -> std::string {
    std::string out{text};
    for (char &ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

/// @brief 回退链区段的提示行文案（四档互斥，次序即优先级）。
///
/// 提示是**本区段自己**的状态而非某次提交的失败原因，故它落在区段内的独立一行而不占用行的状态列：
/// 状态列由 `after_commit` 写角标，两处共用就会在每次改链后把提示抹掉。上限数经词条的位置参数交出
/// （`{0}`），因为那个数字的真值源在框架头里而不是本件（`kChainCapacity`）。
///
/// 第一条（超出上限）是本件**不改数据**的直接后果：装载值长于上限时列表照单全画，只说明「超出部分
/// 不参与绘制」。面板若擅自裁到前 N 项，用户打开面板改别的一行就会把存储里那两条静默抹掉。
[[nodiscard]] auto chain_hint_text(const std::vector<std::string> &items, std::string_view filter,
                                   std::size_t candidate_count) -> aurora::LocalizedString {
    const aurora::LocalizedString capacity{std::to_string(kChainCapacity)};
    if (items.size() > kChainCapacity) {
        return settings_text("settings.chain.truncated", {capacity});
    }
    if (items.size() == kChainCapacity) {
        return settings_text("settings.chain.full", {capacity});
    }
    if (items.empty()) {
        return settings_text("settings.chain.empty");
    }
    if (!filter.empty() && candidate_count == 0U) {
        return settings_text("settings.chain.no_match");
    }
    return aurora::LocalizedString{std::string{}};
}

/// @brief 把提示文案就地解析成显示串（观察面交的是文本而不是词条 key，用例才不必再引一次表）。
///
/// 绘制侧不走这里：`chain_hint_` 的 `content` 收的是 `LocalizedString`，框架在绘制时自己查表。
[[nodiscard]] auto resolve_hint(const aurora::LocalizedString &hint) -> std::string {
    return hint.resolve(&aurora::default_string_table(), settings_locale());
}

/// @brief 回退链列表区段的高度：按条目数全部展开（上限是链容量，故最多 `kChainCapacity` 行）。
///
/// 必须显式给出：`ReorderableList::on_layout` 在**无界约束**下回落 `320×480`（文件头①），而行区的
/// `Scroll` + `Column` 给子项的纵向约束是无界的。空链也要占一行，否则「链为空」那一档的提示连同
/// 列表一起塌成零高，用户看不到区段。溢出由行区的 `Scroll` 承担，故这里不再自设可见行数上限。
[[nodiscard]] auto chain_section_height_dp(std::size_t item_count) -> float {
    return static_cast<float>(std::max<std::size_t>(item_count, 1U)) * kChainItemHeightDp;
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
    // 预览盒要**先**建好：`build_card()` 按本件是否已持有预览来决定卡片列的第三条子节点（F-e），
    // 而浮层重建在那之后才跑。反过来就会画出一张没有横条的卡片，且没有任何地方报错。
    refresh_preview();
    rebuild_overlay();
    if (escape_binding_ == 0) {
        // 关闭键挂 Global：面板内的控件都可获焦，挂 Focus 反而在焦点落在遮罩层时失灵（S2①）。
        // 闭包先问回退链「有没有正被抓在键盘上的那一项」：全局快捷键在任何控件之前消费（裁决 7.51③
        // 理由 (a)），不先问的话，用户在链里正按着 Space 搬一项时敲 Escape 会连面板一起关掉，而那一项
        // 还悬在半空。放下抓取就原地不动——这是本件与框架之间的一次交接，不是缺口（同裁决 7.58 的判法）。
        escape_binding_ = shortcuts_.add(aurora::KeyCombo(aurora::ModifierKey::None, aurora::KeyCode::Escape),
                                         [this]() -> void {
                                             if (chain_list_ != nullptr && chain_list_->cancel_keyboard_grab()) {
                                                 return;
                                             }
                                             close();
                                         },
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
    // 时序同 `rebuild_overlay()`：本仓自持的派生控件句柄先放，旧卡片子树析构才不逐子告警。
    status_texts_.clear();
    rows_.clear();
    theme_canvases_.clear();
    theme_labels_.clear();
    swatch_canvases_.clear();
    swatch_editor_.reset();
    swatch_reset_button_.reset();
    clear_chain_state();

    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    preview_.reset();  // 横条随浮层一起消失：控件树已脱离宿主，留一份「看着还挂在树上」的视口是最难查的陈旧态
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
            .badge = badge_text(*control),
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

auto SettingsPanel::commit_slot(std::string_view key, std::size_t slot, std::string_view text) -> CommitIssue {
    const SettingsControl *control = find_settings_control(key);
    if (control == nullptr) {
        return CommitIssue::UnknownKey;
    }
    const CommitOutcome outcome = form_.commit_color_slot(key, slot, text);
    after_commit(key, *control, outcome.issue);
    return outcome.issue;
}

auto SettingsPanel::theme_cards() const -> std::vector<ThemeCardView> {
    const std::string current = current_theme_name();
    std::vector<ThemeCardView> out;
    out.reserve(theme_choices_.size());
    for (const ThemeChoice &choice : theme_choices_) {
        ThemeCardView card{.name = choice.name, .selected = choice.name == current};
        const std::array<RgbaColor, 4> samples = theme_samples(choice.palette);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            card.samples[i] = color_to_hex(samples[i]);
        }
        out.push_back(std::move(card));
    }
    return out;
}

auto SettingsPanel::is_palette_customized() const -> bool {
    const PaletteSpec *baseline = theme_baseline();
    if (baseline == nullptr) {
        // 没有可比基线就不能声称当前色板是任何一套的默认（判据 A1-b 的「改过」在这里的最强形态）。
        return true;
    }
    const std::vector<RgbaColor> table = palette_table_from_form();
    if (table.size() != baseline->basic.size()) {
        return true;
    }
    for (std::size_t slot = 0; slot < table.size(); ++slot) {
        if (table[slot] != baseline->basic[slot]) {
            return true;
        }
    }
    // 四个单格槽逐档比：没装载的那一档按「与基线不同」判，面板画不出的一格不该被说成是主题默认。
    const std::pair<std::string_view, FormValue> slots[] = {
        {"appearance.palette.foreground", FormValue::color(baseline->default_foreground)},
        {"appearance.palette.background", FormValue::color(baseline->default_background)},
        {"appearance.palette.cursor", slot_value(baseline->cursor_color)},
        {"appearance.palette.selection", slot_value(baseline->selection_color)},
    };
    for (const auto &[key, expected] : slots) {
        const FormValue *actual = form_.value(key);
        if (actual == nullptr || *actual != expected) {
            return true;
        }
    }
    return false;
}

auto SettingsPanel::theme_card(std::size_t index) const -> aurora::Widget * {
    return index < theme_canvases_.size() ? theme_canvases_[index].get() : nullptr;
}

auto SettingsPanel::swatch_slot(std::size_t index) const -> aurora::Widget * {
    return index < swatch_canvases_.size() ? swatch_canvases_[index].get() : nullptr;
}

auto SettingsPanel::swatch_input() const -> aurora::Widget * {
    return swatch_editor_.get();
}

auto SettingsPanel::chain_view() const -> ChainView {
    ChainView out;
    // 次序与内容一律回读表单而不是读派生态：面板显示的链与落盘的链必须同源（文件头「三个区段的当前值都
    // 不进控件自己的存储」同一条纪律），用例判的也是「表单里那一份」而不是控件树。
    const std::vector<std::string> items = chain_items_from_form();
    out.items.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        out.items.push_back(ChainItem{
            .family = items[i],
            .can_move_up = i > 0U,
            .can_move_down = i + 1U < items.size(),
        });
    }
    for (const std::string &candidate : candidate_names_) {
        if (!candidate.empty()) {
            out.candidates.push_back(candidate);
        }
    }
    out.at_capacity = items.size() >= kChainCapacity;
    out.hint = resolve_hint(chain_hint_text(items, chain_filter_text_, out.candidates.size()));
    return out;
}

auto SettingsPanel::chain_up_button(std::size_t index) const -> aurora::Widget * {
    return index < chain_up_buttons_.size() ? chain_up_buttons_[index].get() : nullptr;
}

auto SettingsPanel::chain_down_button(std::size_t index) const -> aurora::Widget * {
    return index < chain_down_buttons_.size() ? chain_down_buttons_[index].get() : nullptr;
}

auto SettingsPanel::chain_remove_button(std::size_t index) const -> aurora::Widget * {
    return index < chain_remove_buttons_.size() ? chain_remove_buttons_[index].get() : nullptr;
}

auto SettingsPanel::chain_candidate(std::size_t index) const -> aurora::Widget * {
    return index < chain_candidates_.size() ? chain_candidates_[index].get() : nullptr;
}

auto SettingsPanel::chain_filter_input() const -> aurora::Widget * {
    return chain_filter_.get();
}

auto SettingsPanel::chain_list() const -> aurora::Widget * {
    return chain_list_.get();
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
    // 先放掉本仓自持的派生控件句柄，再摘旧浮层：否则旧卡片子树析构时这些控件仍被面板持有，
    // G34 回货后的那条「活在容器之外被摘走」告警就会逐子刷屏（残量实测归因于此时序）。
    rows_ = collect_rows();
    status_texts_.assign(rows_.size(), nullptr);
    // 候选表每次建浮层现取（与字体族目录同一条分工，裁决 7.46③），派生态的控件指针一律先清：
    // 新页可能根本没有这些区段，留着旧指针会让 `theme_cards()` 与刷新腿读到上一张卡的孤儿。
    theme_choices_ = hooks_.themes ? hooks_.themes() : std::vector<ThemeChoice>{};
    family_catalog_ = hooks_.families ? hooks_.families() : std::vector<FontFamilyEntry>{};
    theme_canvases_.clear();
    theme_labels_.clear();
    swatch_canvases_.clear();
    swatch_editor_.reset();
    swatch_reset_button_.reset();
    clear_chain_state();

    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }

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
    auto builder = std::make_shared<aurora::LayoutBuilder>(
        [this](const aurora::BuildContext &ctx, const aurora::Constraints &c) -> aurora::Node {
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

            // 行区是 `Scroll` + `Column` 而不是 `LazyList`：后者的 `item_extent` 是**全局**固定行高，
            // 而外观页的四类专用区段（主题卡 / 16 格色板 / 字体族 / 回退链）各自要占多行高度。
            // 内容一次全量建好（每页 ≤30 行），换来的是可变行高与「滚动偏移折回内容坐标」的命中链
            // ——后者正是 G27 回货给 `Scroll` 补上的那条腿，故本件对它的真实点击另配一例证人。
            std::vector<aurora::Node> row_nodes;
            row_nodes.reserve(rows_.size());
            for (std::size_t ordinal = 0; ordinal < rows_.size(); ++ordinal) {
                row_nodes.push_back(build_row(ordinal));
            }
            auto rows_column = std::make_shared<aurora::Column>(aurora::ColumnProps{
                .children = std::move(row_nodes),
                .gap = 0.0F,
            });
            rows_column->modifier.set(aurora::Modifier{}.fill_max_width());
            auto rows = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
                .child = aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(rows_column))},
            });
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

            std::vector<aurora::Node> card_children;
            card_children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(header))});
            card_children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(body))});
            if (preview_ != nullptr) {
                // 横条是卡片的**第三条**子节点，故它从 `body`（`expand()`）那一段里扣 140 dp，卡片总高不变
                // （判据文 F-e：高度做成常量、不改卡片尺寸）。
                auto bar = build_preview_bar();
                // 浮层子树不经 `Window::present_root` 那一次遍历挂载（`OverlayHost::add_overlay` 只 push +
                // 标脏布局），故本视口的 `on_mount` 要在这里补——不补则闪烁档与主题订阅永不注册
                // （裁决 7.49④ 同一条物理事实；`Widget::mount` 幂等，重建闭包再跑一次也无害）。
                preview_->ensure_mounted(ctx);
                card_children.push_back(std::move(bar));
            }
            auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
                .children = std::move(card_children),
                .gap = 0.0F,
            });
            card->modifier.set(aurora::Modifier{}.size(width, height).background(kCardBg, 8.0F).border(
                1.0F, kCardLine));
            // 状态列指针在这里登记（`build_row` 已把每行的状态列建好），故本处**不得**再清空——
            // 行区改全量实例化之后，构建与这段代码在同一次布局里先后发生，倒空就把刚登记的指针抹掉了。
            return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card))};
        });
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(builder))};
}

auto SettingsPanel::build_row(std::size_t ordinal) -> aurora::Node {
    if (ordinal >= rows_.size()) {
        return aurora::Node{};
    }
    const SettingsControl &control = *rows_[ordinal];
    // 三个专用区段各占多行高度，故在行的分派处就拐出去：它们仍是一行 catalog 键（角标、状态列与提交
    // 都落在那一行上），只是内容不是一条 56 dp 的行盒。
    if (control.kind == ControlKind::ThemePicker) {
        return build_theme_section(ordinal);
    }
    if (control.kind == ControlKind::SwatchGrid) {
        return build_swatch_section(ordinal);
    }
    if (control.kind == ControlKind::FamilyList) {
        return build_chain_section(ordinal);
    }
    const bool editable = is_editable(control);

    auto [label, status] = build_header(ordinal, control, editable);
    auto row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {std::move(label), std::move(status), build_control(control, editable)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 12.0F,
    });
    row->modifier.set(aurora::Modifier{}.fill_max_width().height(kRowExtentDp).padding(aurora::EdgeInsets{
        .left = 16.0F, .top = 8.0F, .right = 16.0F, .bottom = 8.0F}));
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row))};
}

auto SettingsPanel::build_header(std::size_t ordinal, const SettingsControl &control, bool editable)
    -> std::pair<aurora::Node, aurora::Node> {
    auto label = std::make_shared<aurora::Text>(
        aurora::TextProps{.content = settings_text(control.key), .text_color = editable ? kText : kTextDim});
    label->modifier.set(aurora::Modifier{}.expand());

    auto status = std::make_shared<aurora::Text>(aurora::TextProps{
        .content = badge_text(control), .text_color = kTextDim, .text_align = aurora::TextAlign::Right});
    status->modifier.set(aurora::Modifier{}.width(kStatusWidthDp));
    if (ordinal < status_texts_.size()) {
        status_texts_[ordinal] = status;  // 条目构建器只在序号有效时才登记指针
    }
    return {aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(label))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(status))}};
}

auto SettingsPanel::build_theme_section(std::size_t ordinal) -> aurora::Node {
    const SettingsControl &control = *rows_[ordinal];
    auto [label, status] = build_header(ordinal, control, true);
    auto header = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {std::move(label), std::move(status)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 12.0F,
    });
    header->modifier.set(aurora::Modifier{}.fill_max_width());

    // `LayoutBuilder` 在约束变化时会重跑整棵子树，故本区段的派生态指针在这里先清——
    // 不清就会一轮比一轮长，而刷新腿只认最后那批。
    theme_canvases_.clear();
    theme_labels_.clear();
    std::vector<aurora::Node> cards;
    cards.reserve(theme_choices_.size());
    for (std::size_t index = 0; index < theme_choices_.size(); ++index) {
        const std::string name = theme_choices_[index].name;
        auto canvas = std::make_shared<aurora::Canvas>([this, index](aurora::Painter &painter,
                                                                     const aurora::Rect &box) -> void {
            paint_theme_card(painter, box, index);
        });
        // 点击挂在画布上而不是外层 `Stack` 上：链上「只有可点或含可点后代者入链」，纯展示的画布根本
        // 不会被派发交回（`Widget::hit_test_chain`），那样 `theme_card()` 就点不到。画布 `fill_max_size`
        // 铺满整张卡，故落点即使在卡名那一档也一样命中它——`Text` 无可点语义，链会继续往下找。
        canvas->modifier.set(aurora::Modifier{}.fill_max_size().clickable(
            [this, name]() -> void { apply_theme(name); }));
        theme_canvases_.push_back(canvas);

        auto card_label = std::make_shared<aurora::Text>(
            aurora::TextProps{.content = name, .text_color = theme_choices_[index].name == current_theme_name()
                                                       ? kText
                                                       : kTextDim});
        // 卡名落在下沿那一档：画布的样例四格在上半部、分隔线在中线，名字不能压到它们。
        card_label->modifier.set(aurora::Modifier{}.padding(
            aurora::EdgeInsets{.left = 10.0F, .top = 36.0F, .right = 0.0F, .bottom = 0.0F}));
        theme_labels_.push_back(card_label);

        // 卡 = 一张画尽底色/样例/描边/勾的画布 + 叠在其上的卡名。`Stack` 确实写子节点 bounds（裁决 7.56①），
        // 故整张卡可命中；外层 `Stack` 与画布各挂一份同一个闭包，落在卡名、样例还是留白上都同样切主题。
        auto card = std::make_shared<aurora::Stack>(std::vector<aurora::Node>{
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(canvas))},
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card_label))}});
        card->modifier.set(aurora::Modifier{}
                               .height(kThemeCardHeightDp)
                               .expand()
                               .clickable([this, name]() -> void { apply_theme(name); }));
        cards.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card))});
    }

    std::vector<aurora::Node> bands;
    for (std::size_t begin = 0; begin < cards.size(); begin += kThemeCardPerRow) {
        std::vector<aurora::Node> band;
        for (std::size_t i = begin; i < std::min(begin + kThemeCardPerRow, cards.size()); ++i) {
            band.push_back(std::move(cards[i]));
        }
        auto row = std::make_shared<aurora::Row>(
            aurora::RowProps{.children = std::move(band), .gap = kSectionGapDp});
        row->modifier.set(aurora::Modifier{}.fill_max_width());
        bands.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row))});
    }

    auto body = std::make_shared<aurora::Column>(
        aurora::ColumnProps{.children = std::move(bands), .gap = kSectionGapDp});
    body->modifier.set(aurora::Modifier{}.fill_max_width());
    auto section = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(header))},
                     aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(body))}},
        .gap = kSectionGapDp,
    });
    section->modifier.set(aurora::Modifier{}.fill_max_width().padding(aurora::EdgeInsets{
        .left = 16.0F, .top = 8.0F, .right = 16.0F, .bottom = 8.0F}));
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(section))};
}

auto SettingsPanel::build_swatch_section(std::size_t ordinal) -> aurora::Node {
    const SettingsControl &control = *rows_[ordinal];
    auto [label, status] = build_header(ordinal, control, true);
    auto header = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {std::move(label), std::move(status)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 12.0F,
    });
    header->modifier.set(aurora::Modifier{}.fill_max_width());

    swatch_canvases_.clear();
    std::vector<aurora::Node> swatch_nodes;
    swatch_nodes.reserve(kPaletteSlotCount);
    for (std::size_t slot = 0; slot < kPaletteSlotCount; ++slot) {
        auto tile = std::make_shared<aurora::Canvas>(
            kSwatchTileWidthDp, kSwatchTileHeightDp, [this, slot](aurora::Painter &painter,
                                                                 const aurora::Rect &box) -> void {
                paint_swatch(painter, box, slot);
            });
        tile->modifier.set(
            aurora::Modifier{}.clickable([this, slot]() -> void { select_swatch(slot); }));
        swatch_canvases_.push_back(tile);
        swatch_nodes.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(tile))});
    }

    std::vector<aurora::Node> bands;
    for (std::size_t begin = 0; begin < swatch_nodes.size(); begin += kSwatchPerRow) {
        std::vector<aurora::Node> band;
        for (std::size_t i = begin; i < std::min(begin + kSwatchPerRow, swatch_nodes.size()); ++i) {
            band.push_back(std::move(swatch_nodes[i]));
        }
        auto row = std::make_shared<aurora::Row>(aurora::RowProps{.children = std::move(band), .gap = 4.0F});
        row->modifier.set(aurora::Modifier{}.fill_max_width());
        bands.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row))});
    }

    // 编辑器行：A2-a 要「点一格 → 下方给出该格 HEX 输入、当前值与恢复主题默认」，这里给的是**同一个**
    // 常驻输入框换指向（文件头③）。它与通用 HEX 行共用一套着色与失焦才提交的口径（S14）。
    auto editor = std::make_shared<BlurCommitText>();
    editor->set_background(kControlBg);
    editor->set_focused_background(kControlBg);
    editor->set_border_color(kCardLine);
    editor->set_focused_border_color(kAccent);
    editor->set_text_color(kText);
    editor->set_cursor_color(kText);
    editor->set_placeholder_color(kTextDim);
    editor->set_on_submit([this](const std::string &text) -> void {
        commit_slot(kPaletteKey, selected_swatch_, text);
    });
    editor->commit = [this](const std::string &text) -> void {
        commit_slot(kPaletteKey, selected_swatch_, text);
    };
    editor->modifier.set(aurora::Modifier{}.width(120.0F));
    swatch_editor_ = editor;
    sync_swatch_editor();

    auto reset = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_text("settings.action.theme_default"),
        .color = kControlBg,
        .on_color = kText,
        .border_color = kCardLine,
        .border_width = 1.0F,
    });
    reset->set_on_click([this]() -> void { reset_swatch_to_theme_default(); });
    // 主题名不在候选表里时没有「主题默认」可恢复：按钮禁用（框架的禁用态降级绘制并忽略点击），
    // 而不是留一个点了没反应的按钮（S9 / D3-a 同口径）。
    reset->set_disabled_colors(kControlBg, kTextDim);
    reset->set_enabled(theme_baseline() != nullptr);
    swatch_reset_button_ = reset;

    auto editor_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(editor))},
                     aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(reset))}},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 6.0F,
    });
    editor_row->modifier.set(aurora::Modifier{}.fill_max_width());

    std::vector<aurora::Node> children;
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(header))});
    for (aurora::Node &band : bands) {
        children.push_back(std::move(band));
    }
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(editor_row))});
    auto section = std::make_shared<aurora::Column>(
        aurora::ColumnProps{.children = std::move(children), .gap = kSectionGapDp});
    section->modifier.set(aurora::Modifier{}.fill_max_width().padding(aurora::EdgeInsets{
        .left = 16.0F, .top = 8.0F, .right = 16.0F, .bottom = 8.0F}));
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(section))};
}

auto SettingsPanel::paint_theme_card(aurora::Painter &painter, const aurora::Rect &box, std::size_t index)
    const -> void {
    painter.fill_rounded_rect(box, 6.0F, kControlBg);
    if (index >= theme_choices_.size()) {
        return;
    }
    const std::array<RgbaColor, 4> samples = theme_samples(theme_choices_[index].palette);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        painter.fill_rect(aurora::Rect{aurora::Point{box.origin.x + 10.0F + 16.0F * static_cast<float>(i),
                                                     box.origin.y + 10.0F},
                                       aurora::Size{12.0F, 12.0F}},
                          to_color(samples[i]));
    }
    painter.draw_line(aurora::Point{box.origin.x + 10.0F, box.origin.y + 30.0F},
                      aurora::Point{box.right() - 10.0F, box.origin.y + 30.0F}, 1.0F, kCardLine);

    const bool selected = theme_choices_[index].name == current_theme_name();
    painter.draw_rounded_border(box, 6.0F, selected ? 2.0F : 1.0F, selected ? kAccent : kCardLine);
    if (selected) {
        // A1-a 的「选中卡在右下角给勾」：三条折线一个拐，画在卡名那一档的右端。
        painter.stroke_polyline(std::vector<aurora::Point>{aurora::Point{box.right() - 18.0F, box.bottom() - 16.0F},
                                                           aurora::Point{box.right() - 14.0F, box.bottom() - 12.0F},
                                                           aurora::Point{box.right() - 6.0F, box.bottom() - 22.0F}},
                                2.0F, kAccent);
    }
}

auto SettingsPanel::paint_swatch(aurora::Painter &painter, const aurora::Rect &box, std::size_t slot)
    const -> void {
    const std::vector<RgbaColor> table = palette_table_from_form();
    const RgbaColor color = slot < table.size() ? table[slot] : RgbaColor{};
    painter.fill_rounded_rect(box, 3.0F, to_color(color));
    if (slot == selected_swatch_) {
        painter.draw_rounded_border(box, 3.0F, 2.0F, kAccent);
    }
}

auto SettingsPanel::build_chain_section(std::size_t ordinal) -> aurora::Node {
    const SettingsControl &control = *rows_[ordinal];
    auto [label, status] = build_header(ordinal, control, true);
    auto header = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {std::move(label), std::move(status)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 12.0F,
    });
    header->modifier.set(aurora::Modifier{}.fill_max_width());

    // 条目指针表在本函数开头清一次：卡片是 `LayoutBuilder`，本闭包在每次布局都会重跑（裁决 7.61 在
    // 主题卡那一区撞过的同一条），不清就会留下上一版的孤儿指针并被刷新腿写值。
    chain_up_buttons_.clear();
    chain_down_buttons_.clear();
    chain_remove_buttons_.clear();
    chain_candidates_.clear();
    candidate_names_.clear();

    chain_items_ = std::make_shared<aurora::State<std::vector<std::string>>>(chain_items_from_form());
    chain_list_ = std::make_shared<aurora::ReorderableList<std::string>>(
        chain_items_,
        [this](const std::string &family, int index) -> aurora::Node {
            const std::size_t i = static_cast<std::size_t>(index);
            const std::size_t count = chain_items_ == nullptr ? 0U : chain_items_->get().size();

            auto ordinal_text = make_text(std::to_string(i + 1U), kTextDim);
            ordinal_text.widget().modifier.set(aurora::Modifier{}.width(kChainOrdinalWidthDp));
            auto name_text = make_text(family, kText);
            name_text.widget().modifier.set(aurora::Modifier{}.expand());

            // 三枚按钮全部落在手柄带之左：`set_drag_handle(true)` 让列表把右侧 48 dp 收作自己的
            // 起拖区（带内的落点连条目都拿不到，文件头②），带内由本件自绘一幅 grip 点阵。
            const auto make_arrow = [this](std::string_view key, bool enabled,
                                           std::function<void()> action) -> std::shared_ptr<aurora::Button> {
                auto button = std::make_shared<aurora::Button>(aurora::ButtonProps{
                    .color = kControlBg,
                    .on_color = kText,
                    .corner_radius = 3.0F,
                    .border_color = kCardLine,
                    .border_width = 1.0F,
                    .min_width = kChainButtonWidthDp,
                });
                button->label = settings_text(key);
                button->set_disabled_colors(kControlBg, kTextDim);
                button->set_enabled(enabled);
                button->set_on_click(std::move(action));
                button->modifier.set(aurora::Modifier{}.width(kChainButtonWidthDp));
                return button;
            };
            auto up = make_arrow("settings.chain.up", i > 0U, [this, i]() -> void { move_chain_item(i, -1); });
            auto down = make_arrow("settings.chain.down", i + 1U < count, [this, i]() -> void { move_chain_item(i, 1); });
            auto remove = make_arrow("settings.chain.remove", true, [this, i]() -> void { remove_chain_item(i); });
            chain_up_buttons_.push_back(up);
            chain_down_buttons_.push_back(down);
            chain_remove_buttons_.push_back(remove);

            auto grip = std::make_shared<aurora::Canvas>(
                kChainHandleBandDp, kChainItemHeightDp, [this](aurora::Painter &painter, const aurora::Rect &box) -> void {
                    paint_chain_handle(painter, box);
                });

            auto item = std::make_shared<aurora::Row>(aurora::RowProps{
                .children = {std::move(ordinal_text),
                             std::move(name_text),
                             aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(up))},
                             aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(down))},
                             aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(remove))},
                             aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(grip))}},
                .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
                .gap = 6.0F,
            });
            item->modifier.set(aurora::Modifier{}.fill_max_width().height(kChainItemHeightDp));
            return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(item))};
        },
        0.0F);
    chain_list_->set_drag_handle(true);
    chain_list_->set_on_reorder([this](int, int) -> void {
        // 框架在 `reorder()` 内已改写完数据源并 `invalidate()`，但那一次重建发生在下一次布局里；本件把
        // 它拉进这个回调内同步做完，于是按钮指针表在回调返回时就是新的（异步重建会让刷新腿读到旧指针）。
        rebuild_chain_rows(chain_list_->data());
        commit_chain();
    });

    chain_list_holder_ = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {aurora::Node{std::static_pointer_cast<aurora::Widget>(chain_list_)}},
        .gap = 0.0F,
    });
    chain_list_holder_->modifier.set(aurora::Modifier{}.fill_max_width().height(
        chain_section_height_dp(chain_items_->get().size())));

    // 过滤框：普通 `TextInput` 而不是 `BlurCommitText`——它的内容**永不进表单**（文件头「添加族」段），
    // 失焦提交反而会把一串族名当成配置值写进去。
    chain_filter_ = std::make_shared<aurora::TextInput>();
    chain_filter_->set_background(kControlBg);
    chain_filter_->set_focused_background(kControlBg);
    chain_filter_->set_border_color(kCardLine);
    chain_filter_->set_focused_border_color(kAccent);
    chain_filter_->set_text_color(kText);
    chain_filter_->set_cursor_color(kText);
    chain_filter_->set_placeholder_color(kTextDim);
    chain_filter_->set_placeholder(settings_label("settings.chain.filter"));
    chain_filter_->set_on_changed([this](const std::string &text) -> void {
        chain_filter_text_ = text;
        refresh_chain_candidates();
    });
    chain_filter_->modifier.set(aurora::Modifier{}.width(kChainFilterWidthDp));

    // 候选池是**常驻**的 N 枚按钮，换候选只改标签与 `show`（文件头「添加族」段：200+ 族不能撑开一个
    // 无上限浮层，而 `Reactive<bool> show` 为假时该控件量成零盒且不参与命中与绘制，故不需要销毁重建，
    // 命中序号因此稳定）。槽位号在闭包里捕获，点击时按 `candidate_names_` 的当前内容取族名。
    std::vector<aurora::Node> candidate_nodes;
    candidate_nodes.reserve(kChainCandidateRows);
    for (std::size_t slot = 0; slot < kChainCandidateRows; ++slot) {
        auto button = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .color = kControlBg,
            .on_color = kText,
            .corner_radius = 3.0F,
            .border_color = kCardLine,
            .border_width = 1.0F,
        });
        button->set_disabled_colors(kControlBg, kTextDim);
        button->set_on_click([this, slot]() -> void {
            if (slot < candidate_names_.size() && !candidate_names_[slot].empty()) {
                append_chain_family(candidate_names_[slot]);
            }
        });
        button->modifier.set(aurora::Modifier{}.fill_max_width().height(kChainItemHeightDp));
        button->show = false;  // 建好即隐藏：只有过滤出候选时才现形
        chain_candidates_.push_back(button);
        candidate_names_.emplace_back();
        candidate_nodes.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(button))});
    }
    auto pool = std::make_shared<aurora::Column>(aurora::ColumnProps{.children = std::move(candidate_nodes), .gap = 4.0F});
    pool->modifier.set(aurora::Modifier{}.fill_max_width());

    chain_hint_ = std::make_shared<aurora::Text>(aurora::TextProps{.content = std::string{}, .text_color = kTextDim});
    chain_hint_->modifier.set(aurora::Modifier{}.fill_max_width());

    std::vector<aurora::Node> children;
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(header))});
    // 三件成员交**副本**而不是 `std::move`：`static_pointer_cast` 的右值重载会把成员清空，而区段建完之后
    // 还要经这三只指针改高度、改过滤文本与换提示文案（`rebuild_chain_rows` / `refresh_chain_candidates`）。
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(chain_list_holder_)});
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(chain_filter_)});
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(pool))});
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(chain_hint_)});
    auto section = std::make_shared<aurora::Column>(
        aurora::ColumnProps{.children = std::move(children), .gap = kSectionGapDp});
    section->modifier.set(aurora::Modifier{}.fill_max_width().padding(aurora::EdgeInsets{
        .left = 16.0F, .top = 8.0F, .right = 16.0F, .bottom = 8.0F}));

    refresh_chain_candidates();
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(section))};
}

auto SettingsPanel::paint_chain_handle(aurora::Painter &painter, const aurora::Rect &box) const -> void {
    // 三行等长短横：框架的手柄带只留命中区、**不画 grip**（文件头②），故起拖落点必须由本件画出可见
    // 形状，否则用户无从知道右边那 48 dp 是可以抓的。
    constexpr float kDotWidthDp = 12.0F;
    constexpr float kDotHeightDp = 2.0F;
    constexpr float kDotGapDp = 3.0F;
    const float total_h = kDotHeightDp * 3.0F + kDotGapDp * 2.0F;
    const float top = box.origin.y + (box.size.height - total_h) * 0.5F;
    const float left = box.origin.x + (box.size.width - kDotWidthDp) * 0.5F;
    for (std::size_t row = 0; row < 3U; ++row) {
        const float y = top + (kDotHeightDp + kDotGapDp) * static_cast<float>(row);
        painter.fill_rect(aurora::Rect{aurora::Point{left, y}, aurora::Size{kDotWidthDp, kDotHeightDp}}, kTextDim);
    }
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
            case ControlKind::FamilyList:
                break;  // 三个区段不是「一行的控件腿」，在 `build_row` 的分派处就已建完，走不到这里。
            case ControlKind::FontDropdown:
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
    set_status(ordinal, badge_text(control));
    // 两个区段的当前值都在 `form_` 里，故每次成功提交都要重跑那一腿派生态：画布重绘、卡名亮度、
    // 恢复按钮可用性与主题行的「自定义」角标。
    refresh_palette_views();
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
    refresh_preview();  // 预览取的是存储侧刚写出的那一份，故排在落盘之后（落盘失败已在上面 return）
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

auto SettingsPanel::badge_text(const SettingsControl &control) const -> std::string {
    std::string out = badge_for(control);
    // A1-b：「自定义」不是第十张卡，而是任一 palette 槽与主题默认不逐档相等时挂在**主题行**上的状态。
    if (control.key == kThemeKey && is_palette_customized()) {
        if (!out.empty()) {
            out += " · ";
        }
        out += settings_label("settings.badge.customized");
    }
    return out;
}

auto SettingsPanel::current_theme_name() const -> std::string {
    const FormValue *value = form_.value(kThemeKey);
    if (value == nullptr) {
        return {};
    }
    return value->as_text().value_or(std::string{});
}

auto SettingsPanel::theme_baseline() const -> const PaletteSpec * {
    const std::string name = current_theme_name();
    for (const ThemeChoice &choice : theme_choices_) {
        if (choice.name == name) {
            return &choice.palette;
        }
    }
    return nullptr;  // 名字不在候选表里：没有可比基线，也没有可恢复的「主题默认」
}

auto SettingsPanel::palette_table_from_form() const -> std::vector<RgbaColor> {
    const FormValue *value = form_.value(kPaletteKey);
    if (value == nullptr) {
        return {};
    }
    return value->as_color_table().value_or(std::vector<RgbaColor>{});
}

auto SettingsPanel::apply_theme(const std::string &name) -> void {
    const auto choice = std::find_if(theme_choices_.begin(), theme_choices_.end(),
                                     [&name](const ThemeChoice &candidate) -> bool {
                                         return candidate.name == name;
                                     });
    const SettingsControl *control = find_settings_control(kThemeKey);
    if (choice == theme_choices_.end() || control == nullptr) {
        return;  // 画不出来的卡不会有回调，而 catalog 行随表单装载必然存在
    }
    const PaletteSpec &palette = choice->palette;
    // S6①「切主题＝整份色值取新主题的默认值」：一次写完主题名 + 五档色值键。三枚用户开关
    //（`bold_is_bright` / `min_contrast_enabled` / `min_contrast`）不由主题派生（`src/config/themes.cpp`
    // 一套都不带），故本函数一字不碰它们。
    struct Write {
        std::string_view key;
        FormValue value;
    };
    const std::vector<Write> writes = {
        {kThemeKey, FormValue::text(name)},
        {kPaletteKey, FormValue::color_table({palette.basic.begin(), palette.basic.end()})},
        {"appearance.palette.foreground", FormValue::color(palette.default_foreground)},
        {"appearance.palette.background", FormValue::color(palette.default_background)},
        {"appearance.palette.cursor", slot_value(palette.cursor_color)},
        {"appearance.palette.selection", slot_value(palette.selection_color)},
    };
    for (const Write &write : writes) {
        const CommitOutcome outcome = form_.commit_value(write.key, write.value);
        if (outcome.issue != CommitIssue::None) {
            // 主题表就是装载侧的默认值来源，故这一步理论上走不到；走到了既不落盘也不广播，
            // 并把原因写进主题行的状态列（界面上不存在「点了没反应」的通路）。
            after_commit(kThemeKey, *control, outcome.issue);
            AURORA_LOG_WARN("settings", "theme rejected by the form, nothing persisted: ", write.key);
            return;
        }
    }
    bool persisted = true;
    if (form_.has_unsaved_changes() && hooks_.persist) {
        // 一次切主题＝一次写文件：整份表单落盘，而不是六键各 flush 一次。
        if (const auto reason = hooks_.persist(form_); reason.has_value()) {
            AURORA_LOG_ERROR("settings", "persist failed, keep the form dirty: ", *reason);
            persisted = false;
        } else {
            form_.note_persisted();
        }
    }
    if (persisted && apply_scope(*control) == ApplyScope::PersistAndApplyNow && hooks_.broadcast) {
        hooks_.broadcast(form_);
    }
    sync_swatch_editor();
    refresh_palette_views();
    if (persisted) {
        refresh_preview();  // 切主题是六键一次落盘，预览同批跟上新色板（判据 A1 的预览腿）
    }
}

auto SettingsPanel::select_swatch(std::size_t slot) -> void {
    if (slot >= kPaletteSlotCount) {
        return;
    }
    selected_swatch_ = slot;
    sync_swatch_editor();
    refresh_palette_views();
}

auto SettingsPanel::reset_swatch_to_theme_default() -> void {
    const PaletteSpec *baseline = theme_baseline();
    if (baseline == nullptr) {
        return;  // 无基线时按钮是禁用态（框架的禁用态忽略点击），这里只是第二道
    }
    commit_slot(kPaletteKey, selected_swatch_, color_to_hex(baseline->basic[selected_swatch_]));
    // 恢复是一次程序性写值，编辑器必须跟着换：用户看到的文本框内容就是刚写进去的那一档。
    sync_swatch_editor();
}

auto SettingsPanel::sync_swatch_editor() -> void {
    if (swatch_editor_ == nullptr) {
        return;
    }
    const std::vector<RgbaColor> table = palette_table_from_form();
    swatch_editor_->set_value(selected_swatch_ < table.size() ? color_to_hex(table[selected_swatch_])
                                                              : std::string{});
}

auto SettingsPanel::refresh_palette_views() -> void {
    const std::string current = current_theme_name();
    for (const std::shared_ptr<aurora::Canvas> &canvas : theme_canvases_) {
        canvas->mark_needs_paint();
    }
    for (const std::shared_ptr<aurora::Canvas> &canvas : swatch_canvases_) {
        canvas->mark_needs_paint();
    }
    for (std::size_t i = 0; i < theme_labels_.size(); ++i) {
        const bool selected = i < theme_choices_.size() && theme_choices_[i].name == current;
        theme_labels_[i]->color(selected ? kText : kTextDim);
        theme_labels_[i]->mark_needs_paint();
    }
    if (swatch_reset_button_ != nullptr) {
        swatch_reset_button_->set_enabled(theme_baseline() != nullptr);
    }
    // 「自定义」挂在主题行的状态列上，故本行状态列也要跟着重算（该行不在当前页时序号循环找不到它，静默跳过）。
    if (const SettingsControl *control = find_settings_control(kThemeKey); control != nullptr) {
        for (std::size_t ordinal = 0; ordinal < rows_.size(); ++ordinal) {
            if (rows_[ordinal]->key == kThemeKey) {
                set_status(ordinal, badge_text(*control));
                break;
            }
        }
    }
}

auto SettingsPanel::refresh_preview() -> void {
    if (!hooks_.preview_appearance) {
        return;  // 未装接缝即不画横条：预览是可选腿，面板既有行为一字不变
    }
    if (preview_ == nullptr) {
        preview_ = std::make_unique<SettingsPreview>(hooks_.preview_appearance());
    } else {
        preview_->apply(hooks_.preview_appearance());
    }
    // 外观改动会改行列数，而视口在 `on_layout` 里才把新尺寸发给自己的会话、夹具随那次 `resize` 重投；
    // 那一批脏**要下一帧才排**，不唤醒就停在「横条画了但内容还是上一版」。
    if (hooks_.preview_wake) {
        hooks_.preview_wake();
    }
}

auto SettingsPanel::build_preview_bar() -> aurora::Node {
    if (preview_ == nullptr) {
        return aurora::Node{};
    }
    // 扁条高度只能经**控件级尺寸意图**下达（`au::px`），挂在修饰链上的 `.height(140)` 会被静默吃掉：
    // `ui::TerminalView` 构造时自宣 `width/height = fill()`（撑满父级是它自身的意图，见其构造末两句），
    // 而框架 `Widget::layout` 在跑完整条修饰链之后还要按该意图覆写本控件尺寸（「显式盒严格等于设定值」
    // 那一段），于是高轴的 Expand 意图把链上钉好的 140 改回「父级剩余高」。实测读数：意图缺省时横条
    // 占满 header 以下整段（506 dp）并把行区压成 0 高；补上本句即回到 140 dp。
    preview_->view().height(aurora::px(kPreviewHeightDp));
    return preview_->node();
}

auto SettingsPanel::preview_view() const noexcept -> TerminalView * {
    return preview_ == nullptr ? nullptr : &preview_->view();
}

auto SettingsPanel::pump_preview() -> void {
    if (preview_ != nullptr) {
        preview_->pump();
    }
}

auto SettingsPanel::chain_items_from_form() const -> std::vector<std::string> {
    if (const FormValue *value = form_.value(kChainKey); value != nullptr) {
        if (const auto chain = value->as_text_list(); chain.has_value()) {
            return *chain;
        }
    }
    return {};
}

auto SettingsPanel::rebuild_chain_rows(std::vector<std::string> next) -> void {
    if (chain_items_ == nullptr || chain_list_ == nullptr) {
        return;  // 区段没画（当前页不是外观页，或面板关着）：结构性改动无从发生
    }
    const std::size_t count = next.size();
    // 指针表先清：下面那一次同步重建会按新次序逐条重新登记，留着旧条目就会多出尾巴。
    chain_up_buttons_.clear();
    chain_down_buttons_.clear();
    chain_remove_buttons_.clear();
    chain_items_->set(std::move(next));
    chain_list_->invalidate();
    // 长度不变而次序变时框架不会自动重建（`rebuild_if_needed()` 只认长度差），故 `invalidate()` 之后
    // 立刻自己调一次：重建本发生在下一次布局里，拉到此处是为了让本函数返回时按钮指针与数据同序。
    chain_list_->rebuild_if_needed();
    if (chain_list_holder_ != nullptr) {
        chain_list_holder_->modifier.set(aurora::Modifier{}.fill_max_width().height(chain_section_height_dp(count)));
    }
    refresh_chain_views();
}

auto SettingsPanel::refresh_chain_views() -> void {
    // 上移 / 下移 / 移除三枚按钮的可用性在条目构造时就按「序号 + 条目数」算好了（次序一变就重建），
    // 故此处剩下的派生态只有候选池与提示行。
    refresh_chain_candidates();
}

auto SettingsPanel::refresh_chain_candidates() -> void {
    const std::vector<std::string> items = chain_items_ != nullptr ? chain_items_->get() : chain_items_from_form();
    const std::string needle = ascii_lower(chain_filter_text_);
    // 空过滤时**不列任何候选**：目录里 200+ 族，摆哪 N 档都是一次没有依据的挑选（而且用户看不到剩下的），
    // 故池子只在过滤文本非空时现形，此时列出的就是「按目录次序的前 N 个匹配项」。
    std::vector<std::string> matched;
    if (!needle.empty()) {
        for (const FontFamilyEntry &entry : family_catalog_) {
            const std::string lowered = ascii_lower(entry.family);
            if (lowered.find(needle) == std::string::npos) {
                continue;
            }
            const bool already_in = std::any_of(items.begin(), items.end(),
                                                [&lowered](const std::string &family) {
                                                    return ascii_lower(family) == lowered;
                                                });
            if (already_in) {
                continue;  // 已在链内的族不再给第二次追加口（同一族出现两次没有意义）
            }
            matched.push_back(entry.family);
            if (matched.size() >= kChainCandidateRows) {
                break;
            }
        }
    }

    const bool at_capacity = items.size() >= kChainCapacity;
    candidate_names_.assign(chain_candidates_.size(), std::string{});
    std::vector<aurora::Button *> shifted;
    for (std::size_t slot = 0; slot < chain_candidates_.size(); ++slot) {
        const bool visible = slot < matched.size();
        if (chain_candidates_[slot]->show.get() != visible) {
            shifted.push_back(chain_candidates_[slot].get());
        }
        if (visible) {
            candidate_names_[slot] = matched[slot];
        }
        chain_candidates_[slot]->set_label(visible ? matched[slot] : std::string{});
        chain_candidates_[slot]->set_enabled(visible && !at_capacity);  // 达上限时追加口关死（A5-a）
        chain_candidates_[slot]->show = visible;
    }
    if (!shifted.empty()) {
        // 翻转 `show` 必须**手工补布局脏**：框架把 `show` 当测量输入（`Widget::layout` 在 show 为假时直接
        // 回零盒，且该早返回早于布局缓存那一支），但写它不标脏布局，于是祖先按缓存复用「零盒」那次的尺寸，
        // 候选按钮从此量不出高度、进不了命中链（实测：过滤出两档后 `size()` 恒 0x0）。框架文档自陈
        // 「`mark_needs_layout()` 只能沿显式标脏路径失效缓存」，补脏归调用方。
        // 只标叶子即可：脏沿 `layout_parent_` 上溯到渲染根，而那条父链自 **G32** 回货（Aurora `1b3fe58c`：
        // `Node` 析构不再清父指针，改由父侧在真正摘除时清）之后不会再被临时句柄抹断。登记时这里还多标一层
        // 卡片外层的 `LayoutBuilder` 以绕过断链，框架侧的变异自证把那种「应用侧顺手多标祖先脏」判为**掩盖而
        // 非修复**，故随回货一并撤除。
        for (aurora::Button *button : shifted) {
            button->mark_needs_layout();
        }
    }

    if (chain_hint_ != nullptr) {
        chain_hint_->content = chain_hint_text(items, chain_filter_text_, matched.size());
        chain_hint_->mark_needs_paint();
    }
}

auto SettingsPanel::commit_chain() -> void {
    if (chain_items_ == nullptr) {
        return;
    }
    // 表单是唯一权威：一次结构性改动交一次提交，于是「已接线 ∧ 即时」的该行在一次改链里恰好落盘一次、
    // 广播一次（`apply_scope()` 的既有腿，判据文 A5-a）。提交不可能失败——`SettingsForm` 对
    // `FamilyChain` 只判形态（本件交出的永远是 `text_list`），故这里没有回滚分支可写。
    commit(kChainKey, FormValue::text_list(chain_items_->get()));
}

auto SettingsPanel::move_chain_item(std::size_t index, int delta) -> void {
    if (chain_items_ == nullptr || chain_list_ == nullptr) {
        return;
    }
    const std::size_t count = chain_items_->get().size();
    if (index >= count) {
        return;
    }
    const int target = static_cast<int>(index) + delta;
    if (target < 0 || static_cast<std::size_t>(target) >= count) {
        return;  // 越界即不动：与首 / 末项那枚按钮的禁用态是同一判据的两道
    }
    // 换位算式不在本件重述（`std::rotate` 语义在框架的 `reorder()` 里），提交发生在它的回调内。
    chain_list_->reorder(static_cast<int>(index), target);
}

auto SettingsPanel::remove_chain_item(std::size_t index) -> void {
    if (chain_items_ == nullptr) {
        return;
    }
    std::vector<std::string> next = chain_items_->get();
    if (index >= next.size()) {
        return;
    }
    next.erase(next.begin() + static_cast<std::ptrdiff_t>(index));
    rebuild_chain_rows(std::move(next));
    commit_chain();
}

auto SettingsPanel::append_chain_family(const std::string &family) -> void {
    if (chain_items_ == nullptr) {
        return;
    }
    std::vector<std::string> next = chain_items_->get();
    const std::string lowered = ascii_lower(family);
    if (next.size() >= kChainCapacity || std::any_of(next.begin(), next.end(),
                                                     [&lowered](const std::string &item) {
                                                         return ascii_lower(item) == lowered;
                                                     })) {
        return;  // 达上限或已在链内：候选池在那两档本就不显示（或已禁用）它，这里是第二道
    }
    next.push_back(family);
    rebuild_chain_rows(std::move(next));
    commit_chain();
}

auto SettingsPanel::clear_chain_state() -> void {
    chain_items_.reset();
    chain_list_.reset();
    chain_list_holder_.reset();
    chain_filter_.reset();
    chain_hint_.reset();
    chain_up_buttons_.clear();
    chain_down_buttons_.clear();
    chain_remove_buttons_.clear();
    chain_candidates_.clear();
    candidate_names_.clear();
    chain_filter_text_.clear();
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
        case ControlKind::FamilyList:
            return true;  // 三个区段在 `build_row` 就分派出去，本函数只为行表与观察面给出可交互判据
        case ControlKind::FontDropdown:
        case ControlKind::ReadOnlyTable:
            return false;  // 专用控件随后续棒落地（本棒先如实显示当前值而不是给个死控件）
    }
    return false;
}

}  // namespace borealis::ui
