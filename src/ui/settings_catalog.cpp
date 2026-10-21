#include "borealis/ui/settings_catalog.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace borealis::ui {
namespace {

/// @brief 组装一行（聚合初始化的字段次序即 `SettingsControl` 的声明次序）。
[[nodiscard]] auto row(std::string_view key,
                       SettingsPage page,
                       ControlKind kind,
                       ValueDomain domain,
                       NumericDomain numeric,
                       std::vector<std::string> choices,
                       ConsumerStatus consumer,
                       EffectLevel effect) -> SettingsControl {
    return SettingsControl{std::string{key}, page, kind, domain, numeric, std::move(choices), consumer, effect};
}

[[nodiscard]] auto toggle(std::string_view key, SettingsPage page, ConsumerStatus consumer, EffectLevel effect)
    -> SettingsControl {
    return row(key, page, ControlKind::Toggle, ValueDomain::Boolean, {}, {}, consumer, effect);
}

/// @brief 整数步进器：区间照抄 `ScopeReader::integer(...)` 把守的两个数。
[[nodiscard]] auto integer_step(std::string_view key,
                                SettingsPage page,
                                double lo,
                                double hi,
                                ConsumerStatus consumer,
                                EffectLevel effect) -> SettingsControl {
    return row(key, page, ControlKind::NumberStep, ValueDomain::Integral, NumericDomain{lo, hi}, {}, consumer, effect);
}

/// @brief 实数步进器：区间照抄 `ScopeReader::real(...)` 把守的两个数。
[[nodiscard]] auto real_step(std::string_view key,
                             SettingsPage page,
                             double lo,
                             double hi,
                             ConsumerStatus consumer,
                             EffectLevel effect) -> SettingsControl {
    return row(key, page, ControlKind::NumberStep, ValueDomain::Real, NumericDomain{lo, hi}, {}, consumer, effect);
}

/// @brief 下拉：`names` 是装载侧白名单里的**落盘文本名**，逐字照抄 `store.cpp` 的枚举名表与 `one_of` 表。
[[nodiscard]] auto dropdown(std::string_view key,
                            SettingsPage page,
                            std::vector<std::string> names,
                            ConsumerStatus consumer,
                            EffectLevel effect) -> SettingsControl {
    return row(key, page, ControlKind::Dropdown, ValueDomain::Choice, {}, std::move(names), consumer, effect);
}

[[nodiscard]] auto hex(std::string_view key, SettingsPage page, bool optional_slot) -> SettingsControl {
    return row(key,
               page,
               optional_slot ? ControlKind::OptionalHexInput : ControlKind::HexInput,
               ValueDomain::ColorText,
               {},
               {},
               ConsumerStatus::Wired,
               EffectLevel::Immediate);
}

/// @brief 补量纲后缀：判据文 A3-a 要三个步进器各自显示单位，而后缀只能落在这张表里——
///        面板 .cpp 另列一张按键的表就是第二个真值源。
[[nodiscard]] auto with_unit(SettingsControl control, std::string_view unit) -> SettingsControl {
    control.unit = std::string{unit};
    return control;
}

[[nodiscard]] auto build_catalog() -> std::vector<SettingsControl> {
    std::vector<SettingsControl> catalog;
    catalog.reserve(58U);

    // ---- 外观页（§2 表的次序：主题 → 语言 → 调色板 → 字体与排版 → 视口 → 光标 → 侧栏与标签 → 状态栏）----
    // 主题名虽然只有八套预置，但候选的唯一来源是 `config/themes.h`；本行不留名字，否则色值表与
    // 面板各列一遍主题清单，换预置时必然漂移一处。
    catalog.push_back(row("appearance.theme",
                          SettingsPage::Appearance,
                          ControlKind::ThemePicker,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::Immediate));
    // 语言档的两个候选就是落盘名，也是 i18n 的区域标签（`SPEC.FEAT.PREF.05` 稿 D3／D4：名字的唯一
    // 读点在 `src/config/schema_names.h`，本行照抄那两串）。生效档位取 `NextLaunch` 而不是
    // `NextSession`：词条在**建任何控件之前**装填，运行期改它既不会重放既有会话，也不会重画已经
    // 急切解析的那批命令与角标，只有重启才换（该稿 F6／D2）。
    catalog.push_back(dropdown("appearance.language",
                               SettingsPage::Appearance,
                               {"zh", "en"},
                               ConsumerStatus::Wired,
                               EffectLevel::NextLaunch));
    catalog.push_back(row("appearance.palette.basic",
                          SettingsPage::Appearance,
                          ControlKind::SwatchGrid,
                          ValueDomain::ColorTable,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::Immediate));
    catalog.push_back(hex("appearance.palette.foreground", SettingsPage::Appearance, false));
    catalog.push_back(hex("appearance.palette.background", SettingsPage::Appearance, false));
    catalog.push_back(hex("appearance.palette.cursor", SettingsPage::Appearance, true));
    catalog.push_back(hex("appearance.palette.selection", SettingsPage::Appearance, true));
    catalog.push_back(
        toggle("appearance.palette.bold_is_bright", SettingsPage::Appearance, ConsumerStatus::Wired, EffectLevel::Immediate));
    catalog.push_back(toggle("appearance.palette.min_contrast_enabled",
                             SettingsPage::Appearance,
                             ConsumerStatus::Wired,
                             EffectLevel::Immediate));
    catalog.push_back(real_step("appearance.palette.min_contrast",
                                SettingsPage::Appearance,
                                1.0,
                                21.0,
                                ConsumerStatus::Wired,
                                EffectLevel::Immediate));
    catalog.push_back(row("appearance.font_family",
                          SettingsPage::Appearance,
                          ControlKind::FontDropdown,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::Immediate));
    catalog.push_back(with_unit(real_step("appearance.font_size_pt", SettingsPage::Appearance, 6.0, 72.0,
                                          ConsumerStatus::Wired, EffectLevel::Immediate),
                                "pt"));
    // CJK-LITERAL: on-screen-demo - 行高是倍数，判据文 A3-a 逐字要求后缀显 `×` 而非字母 x
    catalog.push_back(with_unit(real_step("appearance.font_line_height", SettingsPage::Appearance, 1.0, 3.0,
                                          ConsumerStatus::Wired, EffectLevel::Immediate),
                                "×"));
    catalog.push_back(with_unit(real_step("appearance.font_letter_spacing_dp", SettingsPage::Appearance, 0.0, 8.0,
                                          ConsumerStatus::Wired, EffectLevel::Immediate),
                                "dp"));
    catalog.push_back(row("appearance.font_fallback_chain",
                          SettingsPage::Appearance,
                          ControlKind::FamilyList,
                          ValueDomain::FamilyChain,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::Immediate));
    catalog.push_back(real_step("appearance.viewport_padding_dp", SettingsPage::Appearance, 0.0, 64.0,
                                ConsumerStatus::Wired, EffectLevel::Immediate));
    // 光标形态与闪烁档的消费方在状态机的构造期注入（`term::TerminalDefaults`，裁决 7.76②）：值已由
    // `src/main.cpp` 的 `make_terminal_defaults()` 交进每一条会话，故按「已接线」呈现；生效档位仍是
    // 「下次会话」——构造期取用，运行期改动不重放既有会话。
    catalog.push_back(dropdown("appearance.cursor_shape",
                               SettingsPage::Appearance,
                               {"block", "underline", "bar"},
                               ConsumerStatus::Wired,
                               EffectLevel::NextSession));
    catalog.push_back(toggle(
        "appearance.cursor_blinking", SettingsPage::Appearance, ConsumerStatus::Wired, EffectLevel::NextSession));
    catalog.push_back(with_unit(integer_step("appearance.cursor_blink_period_ms", SettingsPage::Appearance, 50.0,
                                             5000.0, ConsumerStatus::Wired, EffectLevel::Immediate),
                                "ms"));
    catalog.push_back(toggle(
        "appearance.sidebar_collapsed", SettingsPage::Appearance, ConsumerStatus::Absent, EffectLevel::Immediate));
    // 标签名优先级的消费方在标签条那一棒（在途）：判定件 `ui::resolve_tab_name` 已取该值，
    // 装配层接线随其落地，故本行按「已接线」呈现，而不是挂「延后」角标。
    catalog.push_back(dropdown("appearance.tab_name_priority",
                               SettingsPage::Appearance,
                               {"manual_wins", "osc_wins"},
                               ConsumerStatus::Wired,
                               EffectLevel::Immediate));
    const std::vector<std::string_view> status_bar_keys{
        "show_connection",      "show_reconnect",   "show_cursor_position", "show_encoding",
        "show_grid_size",       "show_font_size",   "show_theme",           "show_scrollback",
        "show_clipboard_policy", "show_input_latency",
    };
    for (std::string_view leaf : status_bar_keys) {
        auto key = std::string{"appearance.status_bar."};
        key.append(leaf);
        catalog.push_back(
            toggle(key, SettingsPage::Appearance, ConsumerStatus::Absent, EffectLevel::Immediate));
    }

    // ---- 终端页 ----
    catalog.push_back(integer_step("terminal.scrollback_limit",
                                   SettingsPage::Terminal,
                                   0.0,
                                   100000.0,
                                   ConsumerStatus::Wired,
                                   EffectLevel::NextSession));
    catalog.push_back(dropdown("terminal.ambiguous_width",
                               SettingsPage::Terminal,
                               {"narrow", "wide"},
                               ConsumerStatus::Wired,
                               EffectLevel::NextSession));
    catalog.push_back(dropdown("terminal.long_line",
                               SettingsPage::Terminal,
                               {"truncate", "wrap"},
                               ConsumerStatus::Absent,
                               EffectLevel::NextSession));
    catalog.push_back(dropdown(
        "terminal.bell", SettingsPage::Terminal, {"off", "visual", "audible"}, ConsumerStatus::Absent, EffectLevel::Immediate));
    // 装载侧对编码只判类型（会话编码腿按名字建解码器，见 `term/codec.h`），下拉里的六个名字是
    // **建议项**：面板不得据此硬校验，否则「先接 GB18030 串口」这类取值在面板里存不进去。
    // 映射不到的名字回落 UTF-8 并留 WARN 痕，拼错的值不能让会话直接失明。
    catalog.push_back(row("terminal.encoding",
                          SettingsPage::Terminal,
                          ControlKind::Dropdown,
                          ValueDomain::FreeText,
                          {},
                          {"UTF-8", "GB18030", "GBK", "Big5", "Latin-1", "CP437"},
                          ConsumerStatus::Wired,
                          EffectLevel::NextSession));
    catalog.push_back(dropdown("terminal.unrepresentable",
                               SettingsPage::Terminal,
                               {"replace", "drop", "pass_through_utf8"},
                               ConsumerStatus::Wired,
                               EffectLevel::NextSession));
    catalog.push_back(dropdown("terminal.paste_newlines",
                               SettingsPage::Terminal,
                               {"as_is", "filter", "convert"},
                               ConsumerStatus::Wired,
                               EffectLevel::Immediate));
    catalog.push_back(dropdown("terminal.right_click",
                               SettingsPage::Terminal,
                               {"context_menu", "paste", "copy_on_select"},
                               ConsumerStatus::Wired,
                               EffectLevel::Immediate));
    // 读方向的授权档（`SPEC.FEAT.CONN.12`，裁决 D2①）：行序紧随同族的 `right_click`，终端页其余
    // 行序不动。效果档是 **Immediate** 而不是 `NextSession`——这一档关掉的是「远端能不能读你的
    // 剪贴板」，延后生效等于给安全闸留一个跨标签的洞；落法是 app 级一份闸门被现读，不扩广播。
    catalog.push_back(dropdown("terminal.clipboard_read",
                               SettingsPage::Terminal,
                               {"deny", "allow", "ask"},
                               ConsumerStatus::Wired,
                               EffectLevel::Immediate));
    // 打开链接前的两枚确认档（`SPEC.FEAT.INTERACT.05`，裁决 D8①）：行序紧随同族的 `clipboard_read`，
    // 终端页其余行序不动。两枚键而不是「一枚全局 + 两枚 bool」是为了让「信任级别可分别配置」这句
    // 需求真的有独立可改的两档；效果档取 **Immediate**（改完立刻生效，不重开任何会话）。
    catalog.push_back(dropdown("terminal.link_osc8",
                               SettingsPage::Terminal,
                               {"never", "ask_once", "ask_always"},
                               ConsumerStatus::Wired,
                               EffectLevel::Immediate));
    catalog.push_back(dropdown("terminal.link_heuristic",
                               SettingsPage::Terminal,
                               {"never", "ask_once", "ask_always"},
                               ConsumerStatus::Wired,
                               EffectLevel::Immediate));
    catalog.push_back(toggle(
        "terminal.copy_on_select", SettingsPage::Terminal, ConsumerStatus::Wired, EffectLevel::Immediate));
    catalog.push_back(toggle(
        "terminal.trim_pasted_trailing_space", SettingsPage::Terminal, ConsumerStatus::Wired, EffectLevel::Immediate));
    catalog.push_back(toggle(
        "terminal.smart_line_join", SettingsPage::Terminal, ConsumerStatus::Wired, EffectLevel::Immediate));
    catalog.push_back(toggle(
        "terminal.strip_tmux_border_chars", SettingsPage::Terminal, ConsumerStatus::Wired, EffectLevel::Immediate));
    catalog.push_back(row("terminal.word_delimiters",
                          SettingsPage::Terminal,
                          ControlKind::TextInput,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::Immediate));
    // 协议白名单（`SPEC.FEAT.INTERACT.05`，D8①）：与 `word_delimiters` 同一族「原样收字符」的域
    // （`FreeText` 只判类型不判域），非法或空白 scheme 由识别件在判定时拒绝、面板不拒收。
    catalog.push_back(row("terminal.link_schemes",
                          SettingsPage::Terminal,
                          ControlKind::TextInput,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::Immediate));
    // 命令块分隔线（`SPEC.FEAT.INTEG.01`，裁决 7.123 D9①）：纯装饰开关，改完立刻生效。
    catalog.push_back(toggle("terminal.command_block_separator", SettingsPage::Terminal, ConsumerStatus::Wired,
                             EffectLevel::Immediate));

    // ---- 连接页（建档表单属屏 2，本页只有默认值）----
    catalog.push_back(row("connection.local_shell",
                          SettingsPage::Connection,
                          ControlKind::TextInput,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::NextSession));
    catalog.push_back(row("connection.startup_directory",
                          SettingsPage::Connection,
                          ControlKind::TextInput,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::NextSession));
    // SSH 与串口两族的消费方随各自需求号落期，故按 S15 挂「延后」角标而**不灰置**（裁决 7.68③：一律可改可落盘）；
    // 区间仍照装载侧把守的数登记：面板要显示当前值并让「改动只落盘」这句可验证，控件的步进范围不能到了落期再猜。
    catalog.push_back(integer_step("connection.ssh.port",
                                   SettingsPage::Connection,
                                   1.0,
                                   65535.0,
                                   ConsumerStatus::Absent,
                                   EffectLevel::NextSession));
    catalog.push_back(dropdown("connection.ssh.auth_method",
                               SettingsPage::Connection,
                               {"password", "privatekey", "agent", "keyboard-interactive"},
                               ConsumerStatus::Absent,
                               EffectLevel::NextSession));
    catalog.push_back(toggle(
        "connection.ssh.agent_forwarding", SettingsPage::Connection, ConsumerStatus::Absent, EffectLevel::NextSession));
    catalog.push_back(integer_step("connection.ssh.keepalive_interval_sec",
                                   SettingsPage::Connection,
                                   0.0,
                                   86400.0,
                                   ConsumerStatus::Absent,
                                   EffectLevel::NextSession));
    catalog.push_back(integer_step("connection.ssh.connect_timeout_sec",
                                   SettingsPage::Connection,
                                   0.0,
                                   600.0,
                                   ConsumerStatus::Absent,
                                   EffectLevel::NextSession));
    // 自动重连三键（WS.05，裁决 7.99 D4①）：区间照抄装载侧把守的那三条。消费方是装配层在建会话
    // 那一刻把三键折成 `conn::RetryPolicy` 交进 `SshConnection` 的构造参数（批 3 接线），故生效档取
    // NextSession 而不是 Immediate：在途连接的重拨环读的是那份快照，改档位要等下一格 SSH 会话。
    catalog.push_back(integer_step("connection.ssh.reconnect_base_delay_ms",
                                   SettingsPage::Connection,
                                   100.0,
                                   60000.0,
                                   ConsumerStatus::Wired,
                                   EffectLevel::NextSession));
    catalog.push_back(integer_step("connection.ssh.reconnect_max_delay_ms",
                                   SettingsPage::Connection,
                                   1000.0,
                                   600000.0,
                                   ConsumerStatus::Wired,
                                   EffectLevel::NextSession));
    catalog.push_back(integer_step("connection.ssh.reconnect_attempts",
                                   SettingsPage::Connection,
                                   0.0,
                                   100.0,
                                   ConsumerStatus::Wired,
                                   EffectLevel::NextSession));
    // 串口七键（`SPEC.FEAT.CONN.05`，裁决 7.110 D3①/D4①）：消费方是向导建串口档案那一刻的默认值
    // 灌入（`ConnectionWizard::Hooks::serial_defaults`），档案自此独立——故生效档取 NextSession 而非
    // Immediate：改这里的默认值不回溯已在档的串口档案，也不影响在途会话。
    catalog.push_back(integer_step("connection.serial.baud",
                                   SettingsPage::Connection,
                                   1.0,
                                   4000000.0,
                                   ConsumerStatus::Wired,
                                   EffectLevel::NextSession));
    catalog.push_back(integer_step("connection.serial.data_bits",
                                   SettingsPage::Connection,
                                   5.0,
                                   8.0,
                                   ConsumerStatus::Wired,
                                   EffectLevel::NextSession));
    catalog.push_back(integer_step("connection.serial.stop_bits",
                                   SettingsPage::Connection,
                                   1.0,
                                   2.0,
                                   ConsumerStatus::Wired,
                                   EffectLevel::NextSession));
    catalog.push_back(dropdown("connection.serial.parity",
                               SettingsPage::Connection,
                               {"none", "even", "odd"},
                               ConsumerStatus::Wired,
                               EffectLevel::NextSession));
    catalog.push_back(dropdown("connection.serial.flow",
                               SettingsPage::Connection,
                               {"none", "hardware", "software"},
                               ConsumerStatus::Wired,
                               EffectLevel::NextSession));
    catalog.push_back(dropdown("connection.serial.line_ending",
                               SettingsPage::Connection,
                               {"LF", "CR", "CRLF"},
                               ConsumerStatus::Wired,
                               EffectLevel::NextSession));
    catalog.push_back(row("connection.serial.encoding",
                          SettingsPage::Connection,
                          ControlKind::TextInput,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::NextSession));
    // 会话日志三行（`SPEC.FEAT.CONN.11`，裁决 7.116）。第一行是**能力闸**：它开着不代表在记录，
    // 用户还得逐会话点一次「开启会话日志」——生效档因此是即时（开启动作那一刻现读，不回溯在途会话），
    // 而旁注那句话就是这道「两道开关」的理解成本（D1① 的代价面，见 `settings.note.session_log`）。
    catalog.push_back(
        toggle("connection.session_logging", SettingsPage::Connection, ConsumerStatus::Wired, EffectLevel::Immediate));
    // 目录与模式都在**开启那一刻**取用（改它不动在途会话已在写的文件与已在打的码），故同挂延后角标。
    catalog.push_back(row("connection.session_log_dir",
                          SettingsPage::Connection,
                          ControlKind::TextInput,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::NextSession));
    catalog.push_back(row("connection.session_log_pattern",
                          SettingsPage::Connection,
                          ControlKind::TextInput,
                          ValueDomain::FreeText,
                          {},
                          {},
                          ConsumerStatus::Wired,
                          EffectLevel::NextSession));

    // ---- 快捷键页（首版只读，S9）----
    // 生效档位记的是「展示」：这张表本身即消费方，而重绑与恢复默认无接缝故挂延后角标。
    catalog.push_back(row("shortcuts.overrides",
                          SettingsPage::Shortcuts,
                          ControlKind::ReadOnlyTable,  // PREF.04: 虽标只读但每行有 edit 按钮
                          ValueDomain::OverrideMap,
                          {},
                          {},
                          ConsumerStatus::Absent,
                          EffectLevel::Immediate));

    return catalog;
}

}  // namespace

auto settings_catalog() -> const std::vector<SettingsControl> & {
    static const std::vector<SettingsControl> catalog = build_catalog();
    return catalog;
}

auto find_settings_control(std::string_view key) -> const SettingsControl * {
    const auto &catalog = settings_catalog();
    const auto found = std::ranges::find(catalog, std::string{key}, &SettingsControl::key);
    return found == catalog.end() ? nullptr : &*found;
}

}  // namespace borealis::ui
