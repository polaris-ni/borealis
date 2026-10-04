/// 测试类型: unit
/// 目标单元: include/borealis/config/form_transfer.h + src/config/form_transfer.cpp
/// 测试说明: 配置成员与表单值之间的搬运件（`codespec/UI_SETTINGS.draft.md` §8 的 ④，面板本体的前置）。
///           它补上的是 `utest_settings_form` 文件头如实登记的那条单向边界：表单件按键放的是**值**、
///           不认识成员，故「两个同域成员互换」在它结构上抓不到。这里的判据按方向配齐三件：
///           **取侧逐键字面量**（一份逐键互异的配置摊出 58 个显式预期）、**写侧逐键唯一移动**
///           （只提交一键，其余 57 键的取值必须一字不动）、**布尔两族点成员名**（布尔只有两档值，
///           前两件对「整行互换的两枚同值布尔」结构上抓不到，故这两族各配一条直接读写 `Settings`
///           成员的证人——这不是凑数，是那类错误唯一的观测通道）。
///           另有两条跨件判据：下拉候选经**配置文件**往返（本件与 `store.cpp` 共用一张落盘名表，
///           拼写漂移会在读回时现形）与「未配」色槽往返成未配而不是黑色（裁决 7.27③ 的 A2-b）。

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/config/form_transfer.h"
#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/settings_catalog.h"
#include "borealis/ui/settings_form.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_form_transfer {

using borealis::config::form_entries;
using borealis::config::apply_form;
using borealis::config::Settings;
using borealis::config::Store;
using borealis::term::AmbiguousWidth;
using borealis::term::CursorShape;
using borealis::ui::ControlKind;
using borealis::ui::FormEntry;
using borealis::ui::FormValue;
using borealis::ui::RgbaColor;
using borealis::ui::SettingsControl;
using borealis::ui::SettingsForm;
using borealis::ui::ShortcutOverride;
using borealis::ui::ValueDomain;

namespace {

constexpr std::size_t kPaletteSlots = 16U;

/// @brief 一份**逐键可辨**的配置：每个叶子都取与缺省不同的值，同族取值两两互异（布尔族除外，
///        那里由 `bool_probes()` 点成员名来判）。取侧字面量预期与写侧的「只有一键移动」都建立在它上面。
[[nodiscard]] auto made_up() -> Settings {
    Settings settings{};
    settings.appearance.theme = "nord";
    for (std::size_t index = 0; index < kPaletteSlots; ++index) {
        settings.appearance.palette.basic[index] = RgbaColor{
            static_cast<std::uint8_t>(index + 1U),
            static_cast<std::uint8_t>(index * 7U),
            static_cast<std::uint8_t>(200U - index),
            static_cast<std::uint8_t>(index == 5U ? 64U : 255U)};
    }
    settings.appearance.palette.default_foreground = RgbaColor{0x10U, 0x11U, 0x12U};
    settings.appearance.palette.default_background = RgbaColor{0x13U, 0x14U, 0x15U};
    settings.appearance.palette.cursor_color = RgbaColor{0x16U, 0x17U, 0x18U};
    settings.appearance.palette.selection_color = std::nullopt;
    settings.appearance.palette.bold_is_bright = true;
    settings.appearance.palette.min_contrast_enabled = true;
    settings.appearance.palette.min_contrast = 7.5;
    settings.appearance.font_family = "Consolas";
    settings.appearance.font_size_pt = 20.0;
    settings.appearance.font_line_height = 2.0;
    settings.appearance.font_letter_spacing_dp = 3.0;
    settings.appearance.font_fallback_chain = {"JetBrains Mono", "NSimSun"};
    settings.appearance.viewport_padding_dp = 12.0F;
    settings.appearance.cursor_shape = CursorShape::Bar;
    settings.appearance.cursor_blinking = false;
    settings.appearance.cursor_blink_period_ms = 750;
    settings.appearance.sidebar_collapsed = false;
    settings.appearance.tab_name_priority = borealis::config::TabNamePriority::OscWins;
    settings.appearance.status_bar.show_connection = false;
    settings.appearance.status_bar.show_cursor_position = false;
    settings.appearance.status_bar.show_grid_size = false;
    settings.appearance.status_bar.show_theme = false;
    settings.appearance.status_bar.show_clipboard_policy = false;
    settings.terminal.scrollback_limit = 20000U;
    settings.terminal.ambiguous_width = AmbiguousWidth::Wide;
    settings.terminal.long_line = borealis::config::LongLinePolicy::Wrap;
    settings.terminal.bell = borealis::config::BellMode::Audible;
    settings.terminal.encoding = "GB18030";
    settings.terminal.paste_newlines = borealis::config::PasteNewlinePolicy::Convert;
    settings.terminal.right_click = borealis::config::RightClickAction::Paste;
    settings.terminal.copy_on_select = true;
    settings.terminal.smart_line_join = true;
    settings.terminal.word_delimiters = "!?";
    settings.connection.local_shell = "pwsh.exe";
    settings.connection.startup_directory = "D:/work";
    settings.connection.ssh.port = 2222;
    settings.connection.ssh.auth_method = "password";
    settings.connection.ssh.agent_forwarding = true;
    settings.connection.ssh.keepalive_interval_sec = 120;
    settings.connection.ssh.connect_timeout_sec = 30;
    settings.connection.serial.baud = 9600;
    settings.connection.serial.data_bits = 7;
    settings.connection.serial.stop_bits = 2;
    settings.connection.serial.parity = "even";
    settings.connection.serial.line_ending = "CRLF";
    settings.connection.serial.encoding = "GBK";
    settings.connection.session_logging = true;
    settings.connection.session_log_dir = "D:/logs";
    settings.shortcuts.overrides = {
        {"session.copy", "Ctrl+Shift+C"},
        {"workspace.split.right", "Ctrl+Alt+Right"},
    };
    return settings;
}

[[nodiscard]] auto table_of(const std::array<RgbaColor, kPaletteSlots> &basic) -> FormValue {
    return FormValue::color_table(std::vector<RgbaColor>(basic.begin(), basic.end()));
}

[[nodiscard]] auto integral_at(std::int64_t value) -> FormValue { return FormValue::integral(value); }

/// @brief 给一行造一个**与当前值不同且仍在域内**的值（写侧「只有一键移动」判据的提交素材）。
///
/// 素材从表里取（区间端点、白名单的另一档），不在本文件写死第二个域表；两档同值时回原值，
/// 调用方据此跳过该行而不是判一个假红。
[[nodiscard]] auto alternate_of(const SettingsControl &control, const FormValue &current) -> FormValue {
    switch (control.domain) {
        case ValueDomain::Boolean:
            return FormValue::boolean(!current.as_boolean().value_or(false));
        case ValueDomain::Integral: {
            const auto lo = static_cast<std::int64_t>(control.numeric.min);
            const auto hi = static_cast<std::int64_t>(control.numeric.max);
            return FormValue::integral(current.as_integral().value_or(lo) == hi ? lo : hi);
        }
        case ValueDomain::Real: {
            const auto lo = control.numeric.min;
            const auto hi = control.numeric.max;
            return FormValue::real(current.as_real().value_or(lo) == hi ? lo : hi);
        }
        case ValueDomain::Choice: {
            const auto now = current.as_text().value_or(std::string{});
            for (const auto &name : control.choices) {
                if (name != now) {
                    return FormValue::text(name);
                }
            }
            return FormValue::text(now);
        }
        case ValueDomain::ColorText: {
            constexpr RgbaColor first{0x11U, 0x22U, 0x33U};
            constexpr RgbaColor second{0x44U, 0x55U, 0x66U};
            const auto now = current.as_color();
            return FormValue::color((now.has_value() && *now == first) ? second : first);
        }
        case ValueDomain::ColorTable: {
            auto table = current.as_color_table().value_or(std::vector<RgbaColor>{});
            if (table.size() == kPaletteSlots) {
                table[0] = (table[0] == RgbaColor{0x77U, 0x88U, 0x99U}) ? RgbaColor{0xAAU, 0xBBU, 0xCCU}
                                                                       : RgbaColor{0x77U, 0x88U, 0x99U};
            }
            return FormValue::color_table(std::move(table));
        }
        case ValueDomain::FreeText: {
            const auto now = current.as_text().value_or(std::string{});
            return FormValue::text(now == "probe value" ? "probe value two" : "probe value");
        }
        case ValueDomain::FamilyChain: {
            auto chain = current.as_text_list().value_or(std::vector<std::string>{});
            if (chain.empty()) {
                chain.emplace_back("Fallback Probe");
            } else {
                chain.clear();
            }
            return FormValue::text_list(std::move(chain));
        }
        case ValueDomain::OverrideMap:
            return FormValue::overrides(std::vector<ShortcutOverride>{
                ShortcutOverride{"probe.command", "Ctrl+Alt+P"}});
    }
    return current;
}

/// @brief 一列布尔行的「键 → 它负责的成员」。
///
/// 本表的存在理由：布尔只有两档值，故「取侧逐键字面量」与「写侧只有一键移动」两条判据对
/// **整行互换的两枚同值布尔**结构上抓不到（互换后每个键看到的值都和预期一致）。直接读写
/// `Settings` 成员是那一类错误的唯一观测通道，故逐键点名。
struct BoolProbe {
    std::string_view key;
    bool (*read)(const Settings &);
    void (*flip)(Settings &);
};

[[nodiscard]] auto bool_probes() -> const std::vector<BoolProbe> & {
    static const std::vector<BoolProbe> probes = {
        BoolProbe{"appearance.palette.bold_is_bright",
                  [](const Settings &s) -> bool { return s.appearance.palette.bold_is_bright; },
                  [](Settings &s) -> void { s.appearance.palette.bold_is_bright = !s.appearance.palette.bold_is_bright; }},
        BoolProbe{"appearance.palette.min_contrast_enabled",
                  [](const Settings &s) -> bool { return s.appearance.palette.min_contrast_enabled; },
                  [](Settings &s) -> void { s.appearance.palette.min_contrast_enabled = !s.appearance.palette.min_contrast_enabled; }},
        BoolProbe{"appearance.cursor_blinking",
                  [](const Settings &s) -> bool { return s.appearance.cursor_blinking; },
                  [](Settings &s) -> void { s.appearance.cursor_blinking = !s.appearance.cursor_blinking; }},
        BoolProbe{"appearance.sidebar_collapsed",
                  [](const Settings &s) -> bool { return s.appearance.sidebar_collapsed; },
                  [](Settings &s) -> void { s.appearance.sidebar_collapsed = !s.appearance.sidebar_collapsed; }},
        BoolProbe{"appearance.status_bar.show_connection",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_connection; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_connection = !s.appearance.status_bar.show_connection; }},
        BoolProbe{"appearance.status_bar.show_reconnect",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_reconnect; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_reconnect = !s.appearance.status_bar.show_reconnect; }},
        BoolProbe{"appearance.status_bar.show_cursor_position",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_cursor_position; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_cursor_position = !s.appearance.status_bar.show_cursor_position; }},
        BoolProbe{"appearance.status_bar.show_encoding",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_encoding; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_encoding = !s.appearance.status_bar.show_encoding; }},
        BoolProbe{"appearance.status_bar.show_grid_size",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_grid_size; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_grid_size = !s.appearance.status_bar.show_grid_size; }},
        BoolProbe{"appearance.status_bar.show_font_size",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_font_size; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_font_size = !s.appearance.status_bar.show_font_size; }},
        BoolProbe{"appearance.status_bar.show_theme",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_theme; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_theme = !s.appearance.status_bar.show_theme; }},
        BoolProbe{"appearance.status_bar.show_scrollback",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_scrollback; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_scrollback = !s.appearance.status_bar.show_scrollback; }},
        BoolProbe{"appearance.status_bar.show_clipboard_policy",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_clipboard_policy; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_clipboard_policy = !s.appearance.status_bar.show_clipboard_policy; }},
        BoolProbe{"appearance.status_bar.show_input_latency",
                  [](const Settings &s) -> bool { return s.appearance.status_bar.show_input_latency; },
                  [](Settings &s) -> void { s.appearance.status_bar.show_input_latency = !s.appearance.status_bar.show_input_latency; }},
        BoolProbe{"terminal.copy_on_select",
                  [](const Settings &s) -> bool { return s.terminal.copy_on_select; },
                  [](Settings &s) -> void { s.terminal.copy_on_select = !s.terminal.copy_on_select; }},
        BoolProbe{"terminal.trim_pasted_trailing_space",
                  [](const Settings &s) -> bool { return s.terminal.trim_pasted_trailing_space; },
                  [](Settings &s) -> void { s.terminal.trim_pasted_trailing_space = !s.terminal.trim_pasted_trailing_space; }},
        BoolProbe{"terminal.smart_line_join",
                  [](const Settings &s) -> bool { return s.terminal.smart_line_join; },
                  [](Settings &s) -> void { s.terminal.smart_line_join = !s.terminal.smart_line_join; }},
        BoolProbe{"terminal.strip_tmux_border_chars",
                  [](const Settings &s) -> bool { return s.terminal.strip_tmux_border_chars; },
                  [](Settings &s) -> void { s.terminal.strip_tmux_border_chars = !s.terminal.strip_tmux_border_chars; }},
        BoolProbe{"connection.ssh.agent_forwarding",
                  [](const Settings &s) -> bool { return s.connection.ssh.agent_forwarding; },
                  [](Settings &s) -> void { s.connection.ssh.agent_forwarding = !s.connection.ssh.agent_forwarding; }},
        BoolProbe{"connection.session_logging",
                  [](const Settings &s) -> bool { return s.connection.session_logging; },
                  [](Settings &s) -> void { s.connection.session_logging = !s.connection.session_logging; }},
    };
    return probes;
}

[[nodiscard]] auto boolean_key_count() -> std::size_t {
    std::size_t total = 0;
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.domain == ValueDomain::Boolean) {
            ++total;
        }
    }
    return total;
}

[[nodiscard]] auto find_entry(const std::vector<FormEntry> &entries, std::string_view key) -> const FormValue * {
    for (const auto &entry : entries) {
        if (entry.key == key) {
            return &entry.value;
        }
    }
    return nullptr;
}

[[nodiscard]] auto make_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name};
}

}  // namespace

AURORA_TEST_CASE(the_bridge_loads_every_catalog_key_and_nothing_else) {
    const auto entries = form_entries(Settings{});
    const auto &catalog = borealis::ui::settings_catalog();
    // 覆盖关系：少一行面板就少画一个控件，多一行就是 schema 外的键。行数须先 REQUIRE 再逐格比，
    // 否则少一行时下面的按序索引会读出越界，变异读数就成了一次崩溃而不是一个红用例。
    AURORA_TEST_REQUIRE_EQ(entries.size(), catalog.size());
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        AURORA_TEST_CHECK_MSG(entries[index].key == catalog[index].key, "row order diverged from the catalog");
        AURORA_TEST_CHECK_MSG(entries[index].value.holds(catalog[index].domain),
                              catalog[index].key + " value does not hold the declared domain");
    }
    // 次序照表意味着表单的装载报告恒干净：三张分叉名单任何一张非空都是本件与表分叉的现场。
    const SettingsForm form{entries};
    AURORA_TEST_REQUIRE_MSG(form.load_report().clean(), "loading the bridge produced structure drift");
    AURORA_TEST_CHECK_EQ(form.dirty_keys().size(), 0U);
    AURORA_TEST_CHECK(!form.has_unsaved_changes());
}

AURORA_TEST_CASE(each_key_takes_the_member_it_names) {
    const Settings settings = made_up();
    const SettingsForm form{form_entries(settings)};
    AURORA_TEST_REQUIRE(form.load_report().clean());

    const auto expect = [&](std::string_view key, const FormValue &want) -> void {
        const auto *held = form.value(key);
        AURORA_TEST_REQUIRE_MSG(held != nullptr, std::string{key} + " has no loaded value");
        AURORA_TEST_CHECK_MSG(held != nullptr && *held == want, std::string{key} + " took another member's value");
    };

    expect("appearance.theme", FormValue::text("nord"));
    expect("appearance.palette.basic", table_of(settings.appearance.palette.basic));
    expect("appearance.palette.foreground", FormValue::color(RgbaColor{0x10U, 0x11U, 0x12U}));
    expect("appearance.palette.background", FormValue::color(RgbaColor{0x13U, 0x14U, 0x15U}));
    expect("appearance.palette.cursor", FormValue::color(RgbaColor{0x16U, 0x17U, 0x18U}));
    expect("appearance.palette.selection", FormValue::unset_color());
    expect("appearance.palette.bold_is_bright", FormValue::boolean(true));
    expect("appearance.palette.min_contrast_enabled", FormValue::boolean(true));
    expect("appearance.palette.min_contrast", FormValue::real(7.5));
    expect("appearance.font_family", FormValue::text("Consolas"));
    expect("appearance.font_size_pt", FormValue::real(20.0));
    // 这一对就是 `utest_settings_form` 抓不到的那两条：同为实数，但取值互异，故互换必在此现形。
    expect("appearance.font_line_height", FormValue::real(2.0));
    expect("appearance.font_letter_spacing_dp", FormValue::real(3.0));
    expect("appearance.font_fallback_chain",
           FormValue::text_list({"JetBrains Mono", "NSimSun"}));
    expect("appearance.viewport_padding_dp", FormValue::real(12.0));
    expect("appearance.cursor_shape", FormValue::text("bar"));
    expect("appearance.cursor_blinking", FormValue::boolean(false));
    expect("appearance.cursor_blink_period_ms", integral_at(750));
    expect("appearance.sidebar_collapsed", FormValue::boolean(false));
    expect("appearance.tab_name_priority", FormValue::text("osc_wins"));
    expect("appearance.status_bar.show_connection", FormValue::boolean(false));
    expect("appearance.status_bar.show_reconnect", FormValue::boolean(true));
    expect("appearance.status_bar.show_cursor_position", FormValue::boolean(false));
    expect("appearance.status_bar.show_encoding", FormValue::boolean(true));
    expect("appearance.status_bar.show_grid_size", FormValue::boolean(false));
    expect("appearance.status_bar.show_font_size", FormValue::boolean(true));
    expect("appearance.status_bar.show_theme", FormValue::boolean(false));
    expect("appearance.status_bar.show_scrollback", FormValue::boolean(true));
    expect("appearance.status_bar.show_clipboard_policy", FormValue::boolean(false));
    expect("appearance.status_bar.show_input_latency", FormValue::boolean(true));
    expect("terminal.scrollback_limit", integral_at(20000));
    expect("terminal.ambiguous_width", FormValue::text("wide"));
    expect("terminal.long_line", FormValue::text("wrap"));
    expect("terminal.bell", FormValue::text("audible"));
    expect("terminal.encoding", FormValue::text("GB18030"));
    expect("terminal.paste_newlines", FormValue::text("convert"));
    expect("terminal.right_click", FormValue::text("paste"));
    expect("terminal.copy_on_select", FormValue::boolean(true));
    expect("terminal.trim_pasted_trailing_space", FormValue::boolean(false));
    expect("terminal.smart_line_join", FormValue::boolean(true));
    expect("terminal.strip_tmux_border_chars", FormValue::boolean(false));
    expect("terminal.word_delimiters", FormValue::text("!?"));
    expect("connection.local_shell", FormValue::text("pwsh.exe"));
    expect("connection.startup_directory", FormValue::text("D:/work"));
    expect("connection.ssh.port", integral_at(2222));
    expect("connection.ssh.auth_method", FormValue::text("password"));
    expect("connection.ssh.agent_forwarding", FormValue::boolean(true));
    expect("connection.ssh.keepalive_interval_sec", integral_at(120));
    expect("connection.ssh.connect_timeout_sec", integral_at(30));
    expect("connection.serial.baud", integral_at(9600));
    expect("connection.serial.data_bits", integral_at(7));
    expect("connection.serial.stop_bits", integral_at(2));
    expect("connection.serial.parity", FormValue::text("even"));
    expect("connection.serial.line_ending", FormValue::text("CRLF"));
    expect("connection.serial.encoding", FormValue::text("GBK"));
    expect("connection.session_logging", FormValue::boolean(true));
    expect("connection.session_log_dir", FormValue::text("D:/logs"));
    expect("shortcuts.overrides",
           FormValue::overrides({ShortcutOverride{"session.copy", "Ctrl+Shift+C"},
                                 ShortcutOverride{"workspace.split.right", "Ctrl+Alt+Right"}}));
}

AURORA_TEST_CASE(writing_the_form_back_rebuilds_that_settings) {
    const Settings settings = made_up();
    // 写侧的主判据：以缺省为基线，把这份逐键互异配置的表单整个写回，结果必须逐位等于它。
    // 取侧与写侧同时写反才会在此保持等值，而那种形态由上一条与下面两条布尔判据分抓。
    const Settings rebuilt = apply_form(SettingsForm{form_entries(settings)}, Settings{});
    AURORA_TEST_CHECK(rebuilt == settings);
    // 幂等：基线已是同一份配置时，写回不产生任何改动。
    AURORA_TEST_CHECK(apply_form(SettingsForm{form_entries(settings)}, settings) == settings);
    // 缺省形态也往返（面板首次启动即 `Settings{}`，裁决 7.26⑤）。
    AURORA_TEST_CHECK(apply_form(SettingsForm{form_entries(Settings{})}, Settings{}) == Settings{});
}

AURORA_TEST_CASE(only_the_committed_key_moves) {
    const Settings base{};
    const auto baseline = form_entries(base);
    for (const auto &control : borealis::ui::settings_catalog()) {
        // 快捷键页首版只读（S9 / D3-a），本件收不到它的提交，故不在此列。
        if (control.kind == ControlKind::ReadOnlyTable) {
            continue;
        }
        SettingsForm form{form_entries(base)};
        const auto *current = form.value(control.key);
        AURORA_TEST_REQUIRE_MSG(current != nullptr, control.key + " has no loaded value");
        const auto want = alternate_of(control, *current);
        // 素材本身与当前值同档（例如单候选白名单）时该行无法构成判据，跳过而不是判一个假红。
        if (*current == want) {
            continue;
        }
        AURORA_TEST_CHECK_MSG(form.commit_value(control.key, want).accepted(), control.key + " commit refused");
        const Settings moved = apply_form(form, base);
        AURORA_TEST_CHECK_MSG(moved != base, control.key + " did not reach any member");
        const auto after = form_entries(moved);
        for (const auto &other : borealis::ui::settings_catalog()) {
            const auto *before_value = find_entry(baseline, other.key);
            const auto *now_value = find_entry(after, other.key);
            AURORA_TEST_REQUIRE_MSG(before_value != nullptr && now_value != nullptr,
                                    other.key + " vanished from the bridge");
            if (other.key == control.key) {
                AURORA_TEST_CHECK_MSG(*now_value == want, other.key + " did not keep the committed value");
            } else {
                AURORA_TEST_CHECK_MSG(*now_value == *before_value,
                                      other.key + " moved while only " + control.key + " was committed");
            }
        }
    }
}

AURORA_TEST_CASE(a_boolean_key_reads_the_boolean_it_names) {
    // 本表必须覆盖全部布尔行，否则新增一个布尔键就悄悄退出这条判据。
    AURORA_TEST_CHECK_EQ(bool_probes().size(), boolean_key_count());
    for (const auto &probe : bool_probes()) {
        Settings settings{};
        probe.flip(settings);
        AURORA_TEST_CHECK_MSG(probe.read(settings) != probe.read(Settings{}),
                              std::string{probe.key} + " probe flipped nothing");
        const SettingsForm form{form_entries(settings)};
        const auto *held = form.value(probe.key);
        AURORA_TEST_REQUIRE_MSG(held != nullptr, std::string{probe.key} + " has no loaded value");
        // 直接点成员：整行互换的两枚同值布尔只有在这里分得开（本文件头登记的残余盲区）。
        AURORA_TEST_CHECK_MSG(held != nullptr && *held == FormValue::boolean(probe.read(settings)),
                              std::string{probe.key} + " reads another boolean");
    }
}

AURORA_TEST_CASE(a_boolean_commit_writes_the_boolean_it_names) {
    const Settings base{};
    for (const auto &probe : bool_probes()) {
        SettingsForm form{form_entries(base)};
        const bool want = !probe.read(base);
        AURORA_TEST_CHECK_MSG(form.commit_value(probe.key, FormValue::boolean(want)).accepted(),
                              std::string{probe.key} + " commit refused");
        const Settings written = apply_form(form, base);
        AURORA_TEST_CHECK_MSG(probe.read(written) == want, std::string{probe.key} + " wrote another boolean");
    }
}

AURORA_TEST_CASE(an_unset_color_slot_stays_unset) {
    const Settings base{};
    // 「未配」是一次显式动作，不是空串也不是黑色（裁决 7.27③ 的 A2-b）：写回后该槽必须是空值。
    SettingsForm unset_form{form_entries(base)};
    AURORA_TEST_REQUIRE(unset_form.commit_unset_color("appearance.palette.cursor").accepted());
    const Settings unset = apply_form(unset_form, base);
    AURORA_TEST_CHECK(!unset.appearance.palette.cursor_color.has_value());

    // 同一槽配成黑色与「未配」在配置里必须是两个值，否则面板上两态不可区分。
    SettingsForm black_form{form_entries(base)};
    AURORA_TEST_REQUIRE(black_form.commit_text("appearance.palette.cursor", "#000000").accepted());
    const Settings black = apply_form(black_form, base);
    AURORA_TEST_REQUIRE(black.appearance.palette.cursor_color.has_value());
    AURORA_TEST_CHECK(*black.appearance.palette.cursor_color == (RgbaColor{0U, 0U, 0U}));
    AURORA_TEST_CHECK(!(black == unset));

    // 取侧同理：未配的槽摊给表单的是「未配」形态，而不是回落出来的某个色。
    const SettingsForm back{form_entries(unset)};
    AURORA_TEST_REQUIRE(back.value("appearance.palette.cursor") != nullptr);
    AURORA_TEST_CHECK(back.value("appearance.palette.cursor")->is_unset_color());
}

AURORA_TEST_CASE(an_unloaded_key_keeps_the_baseline) {
    const Settings settings = made_up();
    const Settings base{};
    auto entries = form_entries(settings);
    // 缺键：表里有而装载没给值（面板少画一个控件）。
    std::erase_if(entries, [](const FormEntry &entry) { return entry.key == "connection.startup_directory"; });
    // 错型：实数行收到布尔（装载报告记一笔，该键同样没有值）。
    for (auto &entry : entries) {
        if (entry.key == "terminal.encoding") {
            entry.value = FormValue::boolean(true);
        }
    }
    const SettingsForm form{std::move(entries)};
    AURORA_TEST_CHECK(!form.load_report().clean());
    const Settings written = apply_form(form, base);
    // 两键保留基线而不是被抹成空值或别的档位。
    AURORA_TEST_CHECK(written.connection.startup_directory == base.connection.startup_directory);
    AURORA_TEST_CHECK(written.terminal.encoding == base.terminal.encoding);
    // 余下的键照常写回。
    AURORA_TEST_CHECK(written.connection.serial.encoding == settings.connection.serial.encoding);
    AURORA_TEST_CHECK(written.appearance.theme == settings.appearance.theme);
}

AURORA_TEST_CASE(every_declared_choice_survives_the_config_file) {
    // 本件与 `store.cpp` 共用一张落盘名表（`src/config/schema_names.h`）：名字只在一个表里改过
    // 就会在这里现形——面板选中的档写进文件再读回来成了另一档。
    const std::filesystem::path file = make_path("form_transfer_choices.json");
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.domain != ValueDomain::Choice) {
            continue;
        }
        for (const auto &name : control.choices) {
            SettingsForm form{form_entries(Settings{})};
            AURORA_TEST_REQUIRE_MSG(form.commit_value(control.key, FormValue::text(name)).accepted(),
                                    control.key + " refuses its own declared choice " + name);
            Store writer{file};
            AURORA_TEST_REQUIRE_MSG(!writer.replace(apply_form(form, Settings{})).has_value(),
                                    "the config file refused the write");
            const Store reader{file};
            const SettingsForm back{form_entries(reader.settings())};
            const auto *held = back.value(control.key);
            AURORA_TEST_REQUIRE_MSG(held != nullptr, control.key + " has no loaded value");
            AURORA_TEST_CHECK_MSG(held != nullptr && *held == FormValue::text(name),
                                  control.key + " came back as another choice");
        }
    }
}

AURORA_TEST_CASE(the_shortcut_rows_move_both_ways) {
    Settings settings{};
    settings.shortcuts.overrides = {
        {"session.copy", "Ctrl+Shift+C"},
        {"workspace.split.right", "Ctrl+Alt+Right"},
    };
    // 映射 ↔ 数组两个方向都不排序也不丢行（落盘形态是数组，裁决 7.27②）。
    const SettingsForm form{form_entries(settings)};
    const auto *rows = form.value("shortcuts.overrides");
    AURORA_TEST_REQUIRE(rows != nullptr);
    const auto held = rows->as_overrides();
    AURORA_TEST_REQUIRE(held.has_value());
    AURORA_TEST_CHECK_EQ(held->size(), 2U);
    AURORA_TEST_CHECK(apply_form(form, Settings{}) == settings);
    // 空表也是合法值：快捷键页首版只读，但「一条覆盖都没有」必须落得回去。
    AURORA_TEST_CHECK(apply_form(SettingsForm{form_entries(Settings{})}, settings) == Settings{});
}

}  // namespace borealis::test_cases::utest_form_transfer
