#include "borealis/config/form_transfer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "schema_names.h"

namespace borealis::config {
namespace {

using borealis::ui::FormEntry;
using borealis::ui::FormValue;
using borealis::ui::RgbaColor;
using borealis::ui::SettingsForm;
using borealis::ui::ShortcutOverride;

/// @brief 一行 = 一个落盘叶子键在 `Settings` 里的两个方向。
///
/// 两个方向并成一行是本件唯一的防分叉手段：键与成员的对应只写一次，取侧与写侧不可能各指一个成员。
struct Binding {
    std::string_view key;
    FormValue (*get)(const Settings &);
    void (*set)(Settings &, const FormValue &);
};

/// @brief 写回一个标量：形态不符（该行声明的形态族之外）不改基线，见公共头 ③。
auto assign(bool &field, const FormValue &value) -> void {
    if (const auto held = value.as_boolean()) {
        field = *held;
    }
}

auto assign(int &field, const FormValue &value) -> void {
    if (const auto held = value.as_integral()) {
        field = static_cast<int>(*held);
    }
}

auto assign(std::size_t &field, const FormValue &value) -> void {
    if (const auto held = value.as_integral()) {
        field = static_cast<std::size_t>(*held);
    }
}

auto assign(double &field, const FormValue &value) -> void {
    if (const auto held = value.as_real()) {
        field = *held;
    }
}

auto assign(float &field, const FormValue &value) -> void {
    if (const auto held = value.as_real()) {
        field = static_cast<float>(*held);
    }
}

auto assign(std::string &field, const FormValue &value) -> void {
    if (const auto held = value.as_text()) {
        field = *held;
    }
}

auto assign(RgbaColor &field, const FormValue &value) -> void {
    if (const auto held = value.as_color()) {
        field = *held;
    }
}

/// @brief 写回一个可缺省的色槽：「未配」折回 `nullopt`，与「配成黑色」保持可区分（裁决 7.27③）。
auto assign(std::optional<RgbaColor> &field, const FormValue &value) -> void { field = value.as_color(); }

/// @brief 写回 16 格色板。
///
/// 格数不合的**整表不改**：装载侧对这种形态也是整键回落（`ui::ValueDomain::ColorTable` 那行的
/// `TableSizeWrong`），半张表落盘会画出一套半新半旧的配色。
auto assign(std::array<RgbaColor, 16> &field, const FormValue &value) -> void {
    const auto table = value.as_color_table();
    if (!table.has_value() || table->size() != field.size()) {
        return;
    }
    std::ranges::copy(*table, field.begin());
}

/// @brief 枚举 → 文本档（表里没有的值给出空串，与 `put_enum` 的「该键不写盘」同向）。
template <typename Enum>
[[nodiscard]] auto enumerated(Enum value, std::span<const EnumName> names) -> FormValue {
    return FormValue::text(std::string{name_of(names, static_cast<std::int64_t>(value))});
}

/// @brief 文本档 → 枚举；名字不在表里时保留基线。
template <typename Enum>
auto assign_enum(Enum &field, const FormValue &value, std::span<const EnumName> names) -> void {
    const auto text = value.as_text();
    if (!text.has_value()) {
        return;
    }
    if (const auto raw = value_of(names, *text)) {
        field = static_cast<Enum>(*raw);
    }
}

[[nodiscard]] auto optional_color(const std::optional<RgbaColor> &color) -> FormValue {
    return color.has_value() ? FormValue::color(*color) : FormValue::unset_color();
}

[[nodiscard]] auto palette_basic(const Settings &settings) -> FormValue {
    const auto &basic = settings.appearance.palette.basic;
    return FormValue::color_table(std::vector<RgbaColor>(basic.begin(), basic.end()));
}

/// @brief 有序族名链：链的**次序**就是语义（优先级自左向右），故两个方向都不排序。
[[nodiscard]] auto fallback_chain(const Settings &settings) -> FormValue {
    return FormValue::text_list(settings.appearance.font_fallback_chain);
}

auto assign_chain(Settings &settings, const FormValue &value) -> void {
    if (const auto chain = value.as_text_list()) {
        settings.appearance.font_fallback_chain = *chain;
    }
}

/// @brief 快捷键覆盖表：配置侧是映射、表单侧是数组（落盘形态即数组，裁决 7.27②）。
///
/// 数组次序照映射的键序，写回时映射自行排序，故往返等值；同一命令在表单里出现两次时后一条胜出，
/// 与落盘侧 `command_overrides` 的「同键后写覆盖」同口径。
[[nodiscard]] auto shortcut_rows(const Settings &settings) -> FormValue {
    std::vector<ShortcutOverride> rows;
    rows.reserve(settings.shortcuts.overrides.size());
    for (const auto &[command, combo] : settings.shortcuts.overrides) {
        rows.push_back(ShortcutOverride{command, combo});
    }
    return FormValue::overrides(std::move(rows));
}

auto assign_shortcuts(Settings &settings, const FormValue &value) -> void {
    const auto rows = value.as_overrides();
    if (!rows.has_value()) {
        return;
    }
    auto &field = settings.shortcuts.overrides;
    field.clear();
    for (const auto &row : *rows) {
        field[row.command] = row.combo;
    }
}

[[nodiscard]] auto build_bindings() -> std::vector<Binding> {
    std::vector<Binding> bindings{
        // ---- 外观 ----
        Binding{"appearance.theme",
                [](const Settings &s) -> FormValue { return FormValue::text(s.appearance.theme); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.theme, v); }},
        Binding{"appearance.palette.basic",
                &palette_basic,
                [](Settings &s, const FormValue &v) { assign(s.appearance.palette.basic, v); }},
        Binding{"appearance.palette.foreground",
                [](const Settings &s) -> FormValue { return FormValue::color(s.appearance.palette.default_foreground); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.palette.default_foreground, v); }},
        Binding{"appearance.palette.background",
                [](const Settings &s) -> FormValue { return FormValue::color(s.appearance.palette.default_background); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.palette.default_background, v); }},
        Binding{"appearance.palette.cursor",
                [](const Settings &s) -> FormValue { return optional_color(s.appearance.palette.cursor_color); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.palette.cursor_color, v); }},
        Binding{"appearance.palette.selection",
                [](const Settings &s) -> FormValue { return optional_color(s.appearance.palette.selection_color); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.palette.selection_color, v); }},
        Binding{"appearance.palette.bold_is_bright",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.palette.bold_is_bright); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.palette.bold_is_bright, v); }},
        Binding{
            "appearance.palette.min_contrast_enabled",
            [](const Settings &s) -> FormValue {
                return FormValue::boolean(s.appearance.palette.min_contrast_enabled);
            },
            [](Settings &s, const FormValue &v) { assign(s.appearance.palette.min_contrast_enabled, v); }},
        Binding{"appearance.palette.min_contrast",
                [](const Settings &s) -> FormValue { return FormValue::real(s.appearance.palette.min_contrast); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.palette.min_contrast, v); }},
        Binding{"appearance.font_family",
                [](const Settings &s) -> FormValue { return FormValue::text(s.appearance.font_family); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.font_family, v); }},
        Binding{"appearance.font_size_pt",
                [](const Settings &s) -> FormValue { return FormValue::real(s.appearance.font_size_pt); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.font_size_pt, v); }},
        Binding{"appearance.font_line_height",
                [](const Settings &s) -> FormValue { return FormValue::real(s.appearance.font_line_height); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.font_line_height, v); }},
        Binding{"appearance.font_letter_spacing_dp",
                [](const Settings &s) -> FormValue { return FormValue::real(s.appearance.font_letter_spacing_dp); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.font_letter_spacing_dp, v); }},
        Binding{"appearance.font_fallback_chain", &fallback_chain, &assign_chain},
        Binding{"appearance.viewport_padding_dp",
                [](const Settings &s) -> FormValue { return FormValue::real(static_cast<double>(s.appearance.viewport_padding_dp)); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.viewport_padding_dp, v); }},
        Binding{"appearance.cursor_shape",
                [](const Settings &s) -> FormValue { return enumerated(s.appearance.cursor_shape, kCursorShapeNames); },
                [](Settings &s, const FormValue &v) { assign_enum(s.appearance.cursor_shape, v, kCursorShapeNames); }},
        Binding{"appearance.cursor_blinking",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.cursor_blinking); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.cursor_blinking, v); }},
        Binding{"appearance.cursor_blink_period_ms",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.appearance.cursor_blink_period_ms));
                },
                [](Settings &s, const FormValue &v) { assign(s.appearance.cursor_blink_period_ms, v); }},
        Binding{"appearance.sidebar_collapsed",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.sidebar_collapsed); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.sidebar_collapsed, v); }},
        Binding{"appearance.tab_name_priority",
                [](const Settings &s) -> FormValue {
                    return enumerated(s.appearance.tab_name_priority, kTabNamePriorityNames);
                },
                [](Settings &s, const FormValue &v) { assign_enum(s.appearance.tab_name_priority, v, kTabNamePriorityNames); }},
        Binding{"appearance.status_bar.show_connection",
                [](const Settings &s) -> FormValue {
                    return FormValue::boolean(s.appearance.status_bar.show_connection);
                },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_connection, v); }},
        Binding{"appearance.status_bar.show_reconnect",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.status_bar.show_reconnect); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_reconnect, v); }},
        Binding{"appearance.status_bar.show_cursor_position",
                [](const Settings &s) -> FormValue {
                    return FormValue::boolean(s.appearance.status_bar.show_cursor_position);
                },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_cursor_position, v); }},
        Binding{"appearance.status_bar.show_encoding",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.status_bar.show_encoding); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_encoding, v); }},
        Binding{"appearance.status_bar.show_grid_size",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.status_bar.show_grid_size); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_grid_size, v); }},
        Binding{"appearance.status_bar.show_font_size",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.status_bar.show_font_size); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_font_size, v); }},
        Binding{"appearance.status_bar.show_theme",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.status_bar.show_theme); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_theme, v); }},
        Binding{"appearance.status_bar.show_scrollback",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.appearance.status_bar.show_scrollback); },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_scrollback, v); }},
        Binding{"appearance.status_bar.show_clipboard_policy",
                [](const Settings &s) -> FormValue {
                    return FormValue::boolean(s.appearance.status_bar.show_clipboard_policy);
                },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_clipboard_policy, v); }},
        Binding{"appearance.status_bar.show_input_latency",
                [](const Settings &s) -> FormValue {
                    return FormValue::boolean(s.appearance.status_bar.show_input_latency);
                },
                [](Settings &s, const FormValue &v) { assign(s.appearance.status_bar.show_input_latency, v); }},

        // ---- 终端 ----
        Binding{"terminal.scrollback_limit",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.terminal.scrollback_limit));
                },
                [](Settings &s, const FormValue &v) { assign(s.terminal.scrollback_limit, v); }},
        Binding{"terminal.ambiguous_width",
                [](const Settings &s) -> FormValue {
                    return enumerated(s.terminal.ambiguous_width, kAmbiguousWidthNames);
                },
                [](Settings &s, const FormValue &v) { assign_enum(s.terminal.ambiguous_width, v, kAmbiguousWidthNames); }},
        Binding{"terminal.long_line",
                [](const Settings &s) -> FormValue { return enumerated(s.terminal.long_line, kLongLineNames); },
                [](Settings &s, const FormValue &v) { assign_enum(s.terminal.long_line, v, kLongLineNames); }},
        Binding{"terminal.bell",
                [](const Settings &s) -> FormValue { return enumerated(s.terminal.bell, kBellNames); },
                [](Settings &s, const FormValue &v) { assign_enum(s.terminal.bell, v, kBellNames); }},
        Binding{"terminal.encoding",
                [](const Settings &s) -> FormValue { return FormValue::text(s.terminal.encoding); },
                [](Settings &s, const FormValue &v) { assign(s.terminal.encoding, v); }},
        Binding{"terminal.paste_newlines",
                [](const Settings &s) -> FormValue { return enumerated(s.terminal.paste_newlines, kPasteNewlineNames); },
                [](Settings &s, const FormValue &v) { assign_enum(s.terminal.paste_newlines, v, kPasteNewlineNames); }},
        Binding{"terminal.right_click",
                [](const Settings &s) -> FormValue { return enumerated(s.terminal.right_click, kRightClickNames); },
                [](Settings &s, const FormValue &v) { assign_enum(s.terminal.right_click, v, kRightClickNames); }},
        Binding{"terminal.copy_on_select",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.terminal.copy_on_select); },
                [](Settings &s, const FormValue &v) { assign(s.terminal.copy_on_select, v); }},
        Binding{"terminal.trim_pasted_trailing_space",
                [](const Settings &s) -> FormValue {
                    return FormValue::boolean(s.terminal.trim_pasted_trailing_space);
                },
                [](Settings &s, const FormValue &v) { assign(s.terminal.trim_pasted_trailing_space, v); }},
        Binding{"terminal.smart_line_join",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.terminal.smart_line_join); },
                [](Settings &s, const FormValue &v) { assign(s.terminal.smart_line_join, v); }},
        Binding{"terminal.strip_tmux_border_chars",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.terminal.strip_tmux_border_chars); },
                [](Settings &s, const FormValue &v) { assign(s.terminal.strip_tmux_border_chars, v); }},
        Binding{"terminal.word_delimiters",
                [](const Settings &s) -> FormValue { return FormValue::text(s.terminal.word_delimiters); },
                [](Settings &s, const FormValue &v) { assign(s.terminal.word_delimiters, v); }},

        // ---- 连接 ----
        Binding{"connection.local_shell",
                [](const Settings &s) -> FormValue { return FormValue::text(s.connection.local_shell); },
                [](Settings &s, const FormValue &v) { assign(s.connection.local_shell, v); }},
        Binding{"connection.startup_directory",
                [](const Settings &s) -> FormValue { return FormValue::text(s.connection.startup_directory); },
                [](Settings &s, const FormValue &v) { assign(s.connection.startup_directory, v); }},
        Binding{"connection.ssh.port",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.ssh.port));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.ssh.port, v); }},
        // SSH 与串口的这几个下拉行在 schema 里本就是字符串（`one_of` 白名单），故不走枚举表。
        Binding{"connection.ssh.auth_method",
                [](const Settings &s) -> FormValue { return FormValue::text(s.connection.ssh.auth_method); },
                [](Settings &s, const FormValue &v) { assign(s.connection.ssh.auth_method, v); }},
        Binding{"connection.ssh.agent_forwarding",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.connection.ssh.agent_forwarding); },
                [](Settings &s, const FormValue &v) { assign(s.connection.ssh.agent_forwarding, v); }},
        Binding{"connection.ssh.keepalive_interval_sec",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.ssh.keepalive_interval_sec));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.ssh.keepalive_interval_sec, v); }},
        Binding{"connection.ssh.connect_timeout_sec",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.ssh.connect_timeout_sec));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.ssh.connect_timeout_sec, v); }},
        Binding{"connection.ssh.reconnect_base_delay_ms",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.ssh.reconnect_base_delay_ms));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.ssh.reconnect_base_delay_ms, v); }},
        Binding{"connection.ssh.reconnect_max_delay_ms",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.ssh.reconnect_max_delay_ms));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.ssh.reconnect_max_delay_ms, v); }},
        Binding{"connection.ssh.reconnect_attempts",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.ssh.reconnect_attempts));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.ssh.reconnect_attempts, v); }},
        Binding{"connection.serial.baud",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.serial.baud));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.serial.baud, v); }},
        Binding{"connection.serial.data_bits",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.serial.data_bits));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.serial.data_bits, v); }},
        Binding{"connection.serial.stop_bits",
                [](const Settings &s) -> FormValue {
                    return FormValue::integral(static_cast<std::int64_t>(s.connection.serial.stop_bits));
                },
                [](Settings &s, const FormValue &v) { assign(s.connection.serial.stop_bits, v); }},
        Binding{"connection.serial.parity",
                [](const Settings &s) -> FormValue { return FormValue::text(s.connection.serial.parity); },
                [](Settings &s, const FormValue &v) { assign(s.connection.serial.parity, v); }},
        Binding{"connection.serial.line_ending",
                [](const Settings &s) -> FormValue { return FormValue::text(s.connection.serial.line_ending); },
                [](Settings &s, const FormValue &v) { assign(s.connection.serial.line_ending, v); }},
        Binding{"connection.serial.encoding",
                [](const Settings &s) -> FormValue { return FormValue::text(s.connection.serial.encoding); },
                [](Settings &s, const FormValue &v) { assign(s.connection.serial.encoding, v); }},
        Binding{"connection.session_logging",
                [](const Settings &s) -> FormValue { return FormValue::boolean(s.connection.session_logging); },
                [](Settings &s, const FormValue &v) { assign(s.connection.session_logging, v); }},
        Binding{"connection.session_log_dir",
                [](const Settings &s) -> FormValue { return FormValue::text(s.connection.session_log_dir); },
                [](Settings &s, const FormValue &v) { assign(s.connection.session_log_dir, v); }},

        // ---- 快捷键 ----
        Binding{"shortcuts.overrides", &shortcut_rows, &assign_shortcuts},
    };
    return bindings;
}

[[nodiscard]] auto bindings() -> const std::vector<Binding> & {
    static const std::vector<Binding> table = build_bindings();
    return table;
}

}  // namespace

auto form_entries(const Settings &settings) -> std::vector<FormEntry> {
    std::vector<FormEntry> entries;
    entries.reserve(bindings().size());
    for (const auto &binding : bindings()) {
        entries.push_back(FormEntry{std::string{binding.key}, binding.get(settings)});
    }
    return entries;
}

auto apply_form(const SettingsForm &form, const Settings &base) -> Settings {
    Settings settings{base};
    for (const auto &binding : bindings()) {
        // 表单里没有该键的值 = 装载时判为缺键或错型，面板少画一个控件；基线照旧保留。
        const auto *current = form.value(binding.key);
        if (current != nullptr) {
            binding.set(settings, *current);
        }
    }
    return settings;
}

}  // namespace borealis::config
