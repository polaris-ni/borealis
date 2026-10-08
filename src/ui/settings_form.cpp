#include "borealis/ui/settings_form.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "borealis/ui/color_text.h"

namespace borealis::ui {
namespace {

/// @brief 色板格数：取 `PaletteSpec::basic` 的长度而非写死 16。
///
/// 本仓的 16 色板定义在 `ui/palette.h`，装载侧判的也是那一张表的长度；这里跟着它的类型走，
/// 面板与表单就不会在「第 17 格存不存得回去」上各说一套。
constexpr std::size_t kPaletteSlots =
    std::tuple_size_v<std::remove_reference_t<decltype(std::declval<const PaletteSpec &>().basic)>>;

/// @brief 该行的控件是否收文本提交（S14 的「失焦或 Enter 才交进来」只对这些形态成立）。
///
/// 下拉、开关、色板整表、族名链、主题卡与只读表的候选由控件自己给出，走 `commit_value`；
/// 给它们开文本入口就是允许面板绕过表里声明的白名单。
[[nodiscard]] auto accepts_text(const SettingsControl &control) noexcept -> bool {
    switch (control.kind) {
        case ControlKind::HexInput:
        case ControlKind::OptionalHexInput:
        case ControlKind::TextInput:
        case ControlKind::NumberStep:
            return true;
        case ControlKind::ThemePicker:
        case ControlKind::SwatchGrid:
        case ControlKind::Toggle:
        case ControlKind::Dropdown:
        case ControlKind::FontDropdown:
        case ControlKind::FamilyList:
        case ControlKind::ReadOnlyTable:
            return false;
    }
    return false;
}

/// @brief 严格整数文本：可带一个前导负号，其后全是数字，不留尾串。
[[nodiscard]] auto parse_integral(std::string_view text) -> std::optional<std::int64_t> {
    if (text.empty()) {
        return {};
    }
    const auto *first = text.data();
    const auto *last = text.data() + text.size();
    std::int64_t parsed{};
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last) {
        return {};
    }
    return parsed;
}

/// @brief 严格实数文本：同样要求整串被消费，故 `12abc` 与空串都判非法。
[[nodiscard]] auto parse_real(std::string_view text) -> std::optional<double> {
    if (text.empty()) {
        return {};
    }
    const auto *first = text.data();
    const auto *last = text.data() + text.size();
    double parsed{};
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last) {
        return {};
    }
    return parsed;
}

/// @brief 取候选值的数值形态（两档数值键共用装载侧那对闭区间）。
[[nodiscard]] auto numeric_of(const FormValue &value) -> std::optional<double> {
    if (const auto integral = value.as_integral(); integral.has_value()) {
        return static_cast<double>(*integral);
    }
    return value.as_real();
}

}  // namespace

auto FormValue::boolean(bool value) -> FormValue {
    return FormValue{detail::FormValueStorage{value}};
}

auto FormValue::integral(std::int64_t value) -> FormValue {
    return FormValue{detail::FormValueStorage{value}};
}

auto FormValue::real(double value) -> FormValue {
    return FormValue{detail::FormValueStorage{value}};
}

auto FormValue::text(std::string value) -> FormValue {
    return FormValue{detail::FormValueStorage{std::move(value)}};
}

auto FormValue::color(RgbaColor value) -> FormValue {
    return FormValue{detail::FormValueStorage{std::optional<RgbaColor>{value}}};
}

auto FormValue::unset_color() -> FormValue {
    return FormValue{detail::FormValueStorage{std::optional<RgbaColor>{}}};
}

auto FormValue::color_table(std::vector<RgbaColor> value) -> FormValue {
    return FormValue{detail::FormValueStorage{std::move(value)}};
}

auto FormValue::text_list(std::vector<std::string> value) -> FormValue {
    return FormValue{detail::FormValueStorage{std::move(value)}};
}

auto FormValue::overrides(std::vector<ShortcutOverride> value) -> FormValue {
    return FormValue{detail::FormValueStorage{std::move(value)}};
}

FormValue::FormValue(detail::FormValueStorage value) : value_{std::move(value)} {}

auto FormValue::holds(ValueDomain domain) const noexcept -> bool {
    switch (domain) {
        case ValueDomain::Boolean:
            return std::holds_alternative<bool>(value_);
        case ValueDomain::Integral:
            return std::holds_alternative<std::int64_t>(value_);
        case ValueDomain::Real:
            return std::holds_alternative<double>(value_);
        case ValueDomain::Choice:
        case ValueDomain::FreeText:
            return std::holds_alternative<std::string>(value_);
        case ValueDomain::ColorText:
            return std::holds_alternative<std::optional<RgbaColor>>(value_);
        case ValueDomain::ColorTable:
            return std::holds_alternative<std::vector<RgbaColor>>(value_);
        case ValueDomain::FamilyChain:
            return std::holds_alternative<std::vector<std::string>>(value_);
        case ValueDomain::OverrideMap:
            return std::holds_alternative<std::vector<ShortcutOverride>>(value_);
    }
    return false;
}

auto FormValue::as_boolean() const noexcept -> std::optional<bool> {
    if (const auto *held = std::get_if<bool>(&value_); held != nullptr) {
        return *held;
    }
    return {};
}

auto FormValue::as_integral() const noexcept -> std::optional<std::int64_t> {
    if (const auto *held = std::get_if<std::int64_t>(&value_); held != nullptr) {
        return *held;
    }
    return {};
}

auto FormValue::as_real() const noexcept -> std::optional<double> {
    if (const auto *held = std::get_if<double>(&value_); held != nullptr) {
        return *held;
    }
    return {};
}

auto FormValue::as_text() const -> std::optional<std::string> {
    if (const auto *held = std::get_if<std::string>(&value_); held != nullptr) {
        return *held;
    }
    return {};
}

auto FormValue::as_color() const noexcept -> std::optional<RgbaColor> {
    if (const auto *held = std::get_if<std::optional<RgbaColor>>(&value_); held != nullptr && held->has_value()) {
        return **held;
    }
    return {};
}

auto FormValue::is_unset_color() const noexcept -> bool {
    const auto *held = std::get_if<std::optional<RgbaColor>>(&value_);
    return held != nullptr && !held->has_value();
}

auto FormValue::as_color_table() const -> std::optional<std::vector<RgbaColor>> {
    if (const auto *held = std::get_if<std::vector<RgbaColor>>(&value_); held != nullptr) {
        return *held;
    }
    return {};
}

auto FormValue::as_text_list() const -> std::optional<std::vector<std::string>> {
    if (const auto *held = std::get_if<std::vector<std::string>>(&value_); held != nullptr) {
        return *held;
    }
    return {};
}

auto FormValue::as_overrides() const -> std::optional<std::vector<ShortcutOverride>> {
    if (const auto *held = std::get_if<std::vector<ShortcutOverride>>(&value_); held != nullptr) {
        return *held;
    }
    return {};
}

auto apply_scope(const SettingsControl &control) noexcept -> ApplyScope {
    if (control.consumer == ConsumerStatus::Wired && control.effect == EffectLevel::Immediate) {
        return ApplyScope::PersistAndApplyNow;
    }
    return ApplyScope::PersistOnly;
}

SettingsForm::SettingsForm(std::vector<FormEntry> entries) {
    // 先照表的次序挑装载值，故 `rows_` 与 `dirty_keys()` 与面板排版同序；表外的键另列一份名单。
    for (const auto &control : settings_catalog()) {
        const auto found = std::ranges::find(entries, control.key, &FormEntry::key);
        if (found == entries.end()) {
            report_.missing_keys.push_back(control.key);
            continue;
        }
        if (!found->value.holds(control.domain)) {
            report_.mistyped_keys.push_back(control.key);
            continue;
        }
        rows_.push_back(Row{control.key, found->value, found->value});
    }
    for (const auto &entry : entries) {
        if (find_settings_control(entry.key) == nullptr) {
            report_.unknown_keys.push_back(entry.key);
        }
    }
}

auto SettingsForm::load_report() const noexcept -> const FormLoadReport & {
    return report_;
}

auto SettingsForm::find_row(std::string_view key) -> Row * {
    const auto found = std::ranges::find(rows_, std::string{key}, &Row::key);
    return found == rows_.end() ? nullptr : &*found;
}

auto SettingsForm::find_row(std::string_view key) const -> const Row * {
    const auto found = std::ranges::find(rows_, std::string{key}, &Row::key);
    return found == rows_.end() ? nullptr : &*found;
}

auto SettingsForm::value(std::string_view key) const -> const FormValue * {
    const auto *row = find_row(key);
    return row == nullptr ? nullptr : &row->current;
}

auto SettingsForm::is_dirty(std::string_view key) const -> bool {
    const auto *row = find_row(key);
    return row != nullptr && row->current != row->persisted;
}

auto SettingsForm::dirty_keys() const -> std::vector<std::string> {
    std::vector<std::string> keys;
    for (const auto &row : rows_) {
        if (row.current != row.persisted) {
            keys.push_back(row.key);
        }
    }
    return keys;
}

auto SettingsForm::has_unsaved_changes() const -> bool {
    return std::ranges::any_of(rows_, [](const Row &row) { return row.current != row.persisted; });
}

auto SettingsForm::note_persisted() -> void {
    for (auto &row : rows_) {
        row.persisted = row.current;
    }
}

auto SettingsForm::apply_scope(std::string_view key) const -> std::optional<ApplyScope> {
    const auto *control = find_settings_control(key);
    if (control == nullptr) {
        return {};
    }
    return ui::apply_scope(*control);
}

auto SettingsForm::validate(const SettingsControl &control, const FormValue &next) -> CommitIssue {
    if (!next.holds(control.domain)) {
        return CommitIssue::DomainMismatch;
    }
    if (control.is_numeric()) {
        const auto number = numeric_of(next);
        if (!number.has_value() || *number < control.numeric.min || *number > control.numeric.max) {
            return CommitIssue::OutOfRange;
        }
    }
    if (control.domain == ValueDomain::Choice) {
        const auto text = next.as_text();
        if (!text.has_value() || std::ranges::find(control.choices, *text) == control.choices.end()) {
            return CommitIssue::NotAChoice;
        }
    }
    if (control.domain == ValueDomain::ColorText && next.is_unset_color()
        && control.kind != ControlKind::OptionalHexInput) {
        return CommitIssue::UnsetNotAllowed;
    }
    if (control.domain == ValueDomain::ColorTable) {
        const auto table = next.as_color_table();
        if (!table.has_value() || table->size() != kPaletteSlots) {
            return CommitIssue::TableSizeWrong;
        }
    }
    return CommitIssue::None;
}

auto SettingsForm::store(std::string_view key, FormValue next) -> CommitOutcome {
    const auto *control = find_settings_control(key);
    if (control == nullptr) {
        return CommitOutcome{CommitIssue::UnknownKey, ApplyScope::PersistOnly};
    }
    Row *row = find_row(key);
    if (row == nullptr) {
        return CommitOutcome{CommitIssue::NotLoaded, ApplyScope::PersistOnly};
    }
    // `ReadOnlyTable` 的「只读」是**入口形态**而不是「这个键不可写」：`accepts_text()` 不收它、色槽与
    // 未配两条按域挡下，于是唯一能改动它的值是控件自己给出的 `OverrideMap`（即 `commit_value`）。
    // 这里曾整行拒改，于是 `SPEC.FEAT.PREF.04` 的编辑对话框里那枚「确定」成了死按钮。
    const auto issue = validate(*control, next);
    if (issue != CommitIssue::None) {
        return CommitOutcome{issue, ApplyScope::PersistOnly};
    }
    row->current = std::move(next);
    return CommitOutcome{CommitIssue::None, ui::apply_scope(*control)};
}

auto SettingsForm::commit_value(std::string_view key, FormValue next) -> CommitOutcome {
    return store(key, std::move(next));
}

auto SettingsForm::commit_text(std::string_view key, std::string_view text) -> CommitOutcome {
    const auto *control = find_settings_control(key);
    if (control == nullptr) {
        return CommitOutcome{CommitIssue::UnknownKey, ApplyScope::PersistOnly};
    }
    if (!accepts_text(*control)) {
        return CommitOutcome{CommitIssue::TextNotAccepted, ApplyScope::PersistOnly};
    }
    switch (control->domain) {
        case ValueDomain::ColorText: {
            const auto parsed = color_from_hex(text);
            if (!parsed.has_value()) {
                return CommitOutcome{CommitIssue::MalformedColor, ApplyScope::PersistOnly};
            }
            return store(key, FormValue::color(*parsed));
        }
        case ValueDomain::Integral: {
            if (const auto parsed = parse_integral(text); parsed.has_value()) {
                return store(key, FormValue::integral(*parsed));
            }
            // 带小数点单独判：`5.0` 对整数档是「填错了控件」，而 `abc` 才是「不成个数字」。
            const auto issue = text.find('.') == std::string_view::npos ? CommitIssue::MalformedNumber
                                                                        : CommitIssue::NotIntegral;
            return CommitOutcome{issue, ApplyScope::PersistOnly};
        }
        case ValueDomain::Real: {
            const auto parsed = parse_real(text);
            if (!parsed.has_value()) {
                return CommitOutcome{CommitIssue::MalformedNumber, ApplyScope::PersistOnly};
            }
            return store(key, FormValue::real(*parsed));
        }
        case ValueDomain::FreeText:
            return store(key, FormValue::text(std::string{text}));
        case ValueDomain::Boolean:
        case ValueDomain::Choice:
        case ValueDomain::ColorTable:
        case ValueDomain::FamilyChain:
        case ValueDomain::OverrideMap:
            return CommitOutcome{CommitIssue::TextNotAccepted, ApplyScope::PersistOnly};
    }
    return CommitOutcome{CommitIssue::TextNotAccepted, ApplyScope::PersistOnly};
}

auto SettingsForm::commit_color_slot(std::string_view key, std::size_t slot, std::string_view text) -> CommitOutcome {
    const auto *control = find_settings_control(key);
    if (control == nullptr) {
        return CommitOutcome{CommitIssue::UnknownKey, ApplyScope::PersistOnly};
    }
    if (control->domain != ValueDomain::ColorTable) {
        return CommitOutcome{CommitIssue::DomainMismatch, ApplyScope::PersistOnly};
    }
    if (slot >= kPaletteSlots) {
        return CommitOutcome{CommitIssue::SlotOutOfRange, ApplyScope::PersistOnly};
    }
    const auto *row = find_row(key);
    if (row == nullptr) {
        return CommitOutcome{CommitIssue::NotLoaded, ApplyScope::PersistOnly};
    }
    const auto parsed = color_from_hex(text);
    if (!parsed.has_value()) {
        return CommitOutcome{CommitIssue::MalformedColor, ApplyScope::PersistOnly};
    }
    auto table = *row->current.as_color_table();
    table[slot] = *parsed;
    return store(key, FormValue::color_table(std::move(table)));
}

auto SettingsForm::commit_unset_color(std::string_view key) -> CommitOutcome {
    const auto *control = find_settings_control(key);
    if (control == nullptr) {
        return CommitOutcome{CommitIssue::UnknownKey, ApplyScope::PersistOnly};
    }
    if (control->domain != ValueDomain::ColorText) {
        return CommitOutcome{CommitIssue::DomainMismatch, ApplyScope::PersistOnly};
    }
    return store(key, FormValue::unset_color());
}

}  // namespace borealis::ui
