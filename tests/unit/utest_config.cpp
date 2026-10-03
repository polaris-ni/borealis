/// 测试类型: unit
/// 目标单元: include/borealis/config/settings.h + store.h，src/config/{settings,store,themes}.cpp
/// 测试说明: 全量默认值的自洽（缺省主题即色值来源）、首启不写盘（裁决 7.26⑤）、
///           四分类全量往返等值（读写键名漂移的自动守卫，含「未配光标色/选区色」与「配了黑色」
///           可区分，裁决 7.25③ 与 7.38②；含选词界定符缺省集的不变量，裁决 7.38③）、
///           缺键/类型不符/域外的回落与留痕、色值段损坏时随**文件内主题名**回落，
///           以及 `SPEC.FEAT.PREF.07` 的损坏降级线（先备份再回落，裁决 7.26④）
///           与落盘形态（单文件 + 按域嵌套 + 顶层 `schema_version`，覆盖表用数组形态避开框架的
///           点号路径模型，且无凭据字段，裁决 7.26⑥）。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/core/json.h"

#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/config/themes.h"
#include "borealis/grid/storage.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_config {

namespace au = aurora;

using borealis::config::BellMode;
using borealis::config::builtin_themes;
using borealis::config::kDefaultThemeName;
using borealis::config::LoadOutcome;
using borealis::config::LongLinePolicy;
using borealis::config::PasteNewlinePolicy;
using borealis::config::RightClickAction;
using borealis::config::Settings;
using borealis::config::Store;
using borealis::config::TabNamePriority;
using borealis::config::theme_palette;
using borealis::term::AmbiguousWidth;
using borealis::term::CursorShape;
using borealis::ui::RgbaColor;

namespace {

/// @brief 本轮用例的独占目录（框架按用例建目录并在结束时清理）。
[[nodiscard]] auto make_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name};
}

/// @brief 手写配置文件：损坏与畸形输入的现场只能由文本给出，不经 `Store` 的写侧。
auto write_file(const std::filesystem::path &path, std::string_view text) -> void {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
}

[[nodiscard]] auto read_file(const std::filesystem::path &path) -> std::string {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// @brief 键（点号路径）是否被登记。
[[nodiscard]] auto holds(const std::vector<std::string> &keys, std::string_view key) -> bool {
    return std::ranges::find(keys, std::string{key}) != keys.end();
}

/// @brief 回落默认值的现场：每个枚举都取缺省的相反档，标量取域内的另一个合法值。
[[nodiscard]] auto non_default() -> Settings {
    Settings next{};

    next.appearance.theme = "nord";
    // 色值刻意不等于任何预置主题：往返等值才排得出「写侧漏了某个字段、读侧又从主题表捡回来」。
    for (std::size_t index = 0; index < next.appearance.palette.basic.size(); ++index) {
        next.appearance.palette.basic[index] =
            RgbaColor{static_cast<std::uint8_t>(0x10U * index), static_cast<std::uint8_t>(0xFFU - 0x10U * index),
                      static_cast<std::uint8_t>(0x30U + index)};
    }
    next.appearance.palette.default_foreground = RgbaColor{12U, 232U, 160U};
    next.appearance.palette.default_background = RgbaColor{31U, 29U, 45U};
    next.appearance.palette.cursor_color = RgbaColor{255U, 0U, 128U};
    next.appearance.palette.selection_color = RgbaColor{60U, 120U, 40U};
    next.appearance.palette.bold_is_bright = true;
    next.appearance.palette.min_contrast_enabled = true;
    next.appearance.palette.min_contrast = 7.5;

    next.appearance.font_family = "Consolas";
    next.appearance.font_size_pt = 11.5;
    next.appearance.viewport_padding_dp = 0.0F;  // 贴边形态（裁决 7.25②）
    next.appearance.cursor_shape = CursorShape::Bar;
    next.appearance.cursor_blinking = false;
    next.appearance.cursor_blink_period_ms = 120;
    next.appearance.sidebar_collapsed = false;
    next.appearance.tab_name_priority = TabNamePriority::OscWins;  // 非缺省档，往返等值才判得出接线
    auto &bar = next.appearance.status_bar;
    bar.show_connection = false;
    bar.show_reconnect = true;
    bar.show_cursor_position = false;
    bar.show_encoding = true;
    bar.show_grid_size = false;
    bar.show_font_size = true;
    bar.show_theme = false;
    bar.show_scrollback = true;
    bar.show_clipboard_policy = false;
    bar.show_input_latency = true;

    next.terminal.scrollback_limit = 4096U;
    next.terminal.ambiguous_width = AmbiguousWidth::Wide;
    next.terminal.long_line = LongLinePolicy::Wrap;
    next.terminal.bell = BellMode::Audible;
    next.terminal.encoding = "GBK";
    next.terminal.paste_newlines = PasteNewlinePolicy::Convert;
    next.terminal.right_click = RightClickAction::Paste;
    next.terminal.copy_on_select = true;
    next.terminal.trim_pasted_trailing_space = true;
    next.terminal.smart_line_join = true;
    next.terminal.strip_tmux_border_chars = true;
    // 刻意含反斜杠与空格：选词界定符是「原始字符集」形态，转义漏一侧就会在读回时静默变样。
    next.terminal.word_delimiters = "-/. \\";

    next.connection.local_shell = "pwsh.exe";
    next.connection.startup_directory = "work-dir";
    next.connection.ssh.port = 2222;
    next.connection.ssh.auth_method = "keyboard-interactive";
    next.connection.ssh.agent_forwarding = true;
    next.connection.ssh.keepalive_interval_sec = 30;
    next.connection.ssh.connect_timeout_sec = 5;
    next.connection.serial.baud = 9600;
    next.connection.serial.data_bits = 7;
    next.connection.serial.stop_bits = 2;
    next.connection.serial.parity = "even";
    next.connection.serial.line_ending = "CRLF";
    next.connection.serial.encoding = "ISO-8859-1";
    next.connection.session_logging = true;
    next.connection.session_log_dir = "log-dir";

    next.shortcuts.overrides["terminal.new_tab"] = "Ctrl+T";
    next.shortcuts.overrides["app.settings"] = "Ctrl+Shift+Comma";
    return next;
}

/// @brief 收集 JSON 里的全部对象键名（凭据扫描用）。
auto collect_keys(const au::json::Value &node, std::vector<std::string> &out) -> void {
    if (node.is_object()) {
        for (const auto &entry : node.entries()) {
            out.emplace_back(entry.key);
            collect_keys(entry.value, out);
        }
        return;
    }
    if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            const au::json::Value *element = node.at(index);
            if (element != nullptr) {
                collect_keys(*element, out);
            }
        }
    }
}

}  // namespace

AURORA_TEST_CASE(defaults_are_the_first_launch_shape) {
    const Settings defaults{};

    AURORA_TEST_CHECK_EQ(defaults.appearance.theme, kDefaultThemeName);
    // 主题表是色值唯一来源：默认调色板必须逐色等于缺省主题，否则换主题名与首屏色会各自漂移。
    AURORA_TEST_CHECK_TRUE(defaults.appearance.palette.basic == theme_palette(kDefaultThemeName).basic);
    AURORA_TEST_CHECK_TRUE(defaults.appearance.palette.default_foreground ==
                           theme_palette(kDefaultThemeName).default_foreground);
    AURORA_TEST_CHECK_TRUE(defaults.appearance.palette.cursor_color == theme_palette(kDefaultThemeName).cursor_color);
    AURORA_TEST_REQUIRE(defaults.appearance.palette.cursor_color.has_value());
    // 选区色同样由主题表派生，本文件不另存一份（裁决 7.38②）。
    AURORA_TEST_CHECK_TRUE(defaults.appearance.palette.selection_color ==
                           theme_palette(kDefaultThemeName).selection_color);
    AURORA_TEST_REQUIRE(defaults.appearance.palette.selection_color.has_value());
    // 开关与阈值不随主题：缺省关闭，但阈值预置成 WCAG AA 的正文档，开了就立刻起作用。
    AURORA_TEST_CHECK_FALSE(defaults.appearance.palette.bold_is_bright);
    AURORA_TEST_CHECK_FALSE(defaults.appearance.palette.min_contrast_enabled);
    AURORA_TEST_CHECK_NEAR(defaults.appearance.palette.min_contrast, 4.5, 0.001);

    AURORA_TEST_CHECK_NEAR(defaults.appearance.font_size_pt, 14.0, 0.001);
    AURORA_TEST_CHECK_NEAR(defaults.appearance.viewport_padding_dp, 4.0F, 0.001F);
    AURORA_TEST_CHECK_TRUE(defaults.appearance.cursor_shape == CursorShape::Block);
    AURORA_TEST_CHECK_TRUE(defaults.appearance.cursor_blinking);
    AURORA_TEST_CHECK_TRUE(defaults.appearance.sidebar_collapsed);
    // 手动重命名压过 OSC 标题是缺省档（`SPEC.FEAT.TERM.07` 的「用户重命名优先级更高（可配）」）。
    AURORA_TEST_CHECK_TRUE(defaults.appearance.tab_name_priority == TabNamePriority::ManualWins);

    // 状态栏十项缺省全开（裁决 7.25⑧）。
    const auto &bar = defaults.appearance.status_bar;
    AURORA_TEST_CHECK_TRUE(bar.show_connection && bar.show_reconnect && bar.show_cursor_position &&
                           bar.show_encoding && bar.show_grid_size && bar.show_font_size && bar.show_theme &&
                           bar.show_scrollback && bar.show_clipboard_policy && bar.show_input_latency);

    AURORA_TEST_CHECK_EQ(defaults.terminal.scrollback_limit, borealis::grid::kDefaultScrollbackLimit);
    AURORA_TEST_CHECK_TRUE(defaults.terminal.bell == BellMode::Visual);
    AURORA_TEST_CHECK_TRUE(defaults.terminal.ambiguous_width == AmbiguousWidth::Narrow);
    AURORA_TEST_CHECK_TRUE(defaults.terminal.long_line == LongLinePolicy::Truncate);
    AURORA_TEST_CHECK_EQ(defaults.terminal.encoding, "UTF-8");
    AURORA_TEST_CHECK_TRUE(defaults.terminal.right_click == RightClickAction::ContextMenu);
    AURORA_TEST_CHECK_FALSE(defaults.terminal.copy_on_select);

    // 缺省界定符是 ASCII 可见标点全集（裁决 7.38③）。判据取其不变量而不是逐字符抄一遍默认值：
    // 这张表是数据而非诊断文案，抄表就等于把默认值本身当断言。
    const auto &delimiters = defaults.terminal.word_delimiters;
    AURORA_TEST_CHECK_EQ(delimiters.size(), 32U);
    for (const char character : delimiters) {
        const bool never_a_delimiter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                       (character >= '0' && character <= '9') || character == ' ' || character == '\t';
        AURORA_TEST_CHECK_MSG(!never_a_delimiter, std::string(1, character));
    }
    // 路径与标识符里的分隔位必须在集内：双击 `C:\dir\file.txt` 选中的是一段而非整条。
    for (const char character : {':', '\\', '/', '.', '-', '_'}) {
        AURORA_TEST_CHECK_MSG(delimiters.find(character) != std::string::npos, std::string(1, character));
    }

    AURORA_TEST_CHECK_TRUE(defaults.connection.local_shell.empty());
    AURORA_TEST_CHECK_EQ(defaults.connection.ssh.port, 22);
    AURORA_TEST_CHECK_EQ(defaults.connection.ssh.auth_method, "agent");
    AURORA_TEST_CHECK_FALSE(defaults.connection.ssh.agent_forwarding);
    AURORA_TEST_CHECK_EQ(defaults.connection.serial.line_ending, "LF");
    AURORA_TEST_CHECK_EQ(defaults.connection.serial.encoding, "GB18030");
    AURORA_TEST_CHECK_FALSE(defaults.connection.session_logging);  // 裁决 7.9

    AURORA_TEST_CHECK_TRUE(defaults.shortcuts.overrides.empty());
    // 「`Settings{}` 就是首次启动的那份配置」：与预置表首项同源。
    AURORA_TEST_CHECK_TRUE(defaults.appearance.palette.basic == builtin_themes().front().palette.basic);
}

AURORA_TEST_CASE(first_run_writes_nothing) {
    const auto file = make_path("first_run.json");
    AURORA_TEST_REQUIRE_FALSE(std::filesystem::exists(file));

    const Store store{file};
    AURORA_TEST_CHECK_TRUE(store.report().outcome == LoadOutcome::FirstRun);
    AURORA_TEST_CHECK_EQ(store.report().file.string(), file.string());
    AURORA_TEST_CHECK_TRUE(store.settings() == Settings{});
    AURORA_TEST_CHECK_FALSE(store.report().corrupt_backup.has_value());
    AURORA_TEST_CHECK_TRUE(store.report().message.empty());
    AURORA_TEST_CHECK_TRUE(store.report().rejected_keys.empty());
    AURORA_TEST_CHECK_TRUE(store.report().unknown_keys.empty());

    // 装载不产生任何写入（裁决 7.26⑤）：连配置文件本身都要留到用户第一次改设置。
    AURORA_TEST_CHECK_FALSE(std::filesystem::exists(file));
}

AURORA_TEST_CASE(every_value_survives_a_write_read_round_trip) {
    const auto file = make_path("round_trip.json");
    const Settings next = non_default();
    {
        Store writer{file};
        AURORA_TEST_CHECK_FALSE(writer.replace(next).has_value());
    }
    AURORA_TEST_REQUIRE(std::filesystem::exists(file));

    const Store reader{file};
    AURORA_TEST_CHECK_TRUE(reader.report().outcome == LoadOutcome::Loaded);
    // 读写任一侧漏键都会在这里露出来：整份配置逐字段等值，且两份留痕皆空。
    AURORA_TEST_CHECK_TRUE(reader.settings().appearance == next.appearance);
    AURORA_TEST_CHECK_TRUE(reader.settings().terminal == next.terminal);
    AURORA_TEST_CHECK_TRUE(reader.settings().connection == next.connection);
    AURORA_TEST_CHECK_TRUE(reader.settings().shortcuts == next.shortcuts);
    AURORA_TEST_CHECK_TRUE(reader.report().rejected_keys.empty());
    AURORA_TEST_CHECK_TRUE(reader.report().unknown_keys.empty());
}

AURORA_TEST_CASE(unset_cursor_color_is_not_black_cursor_color) {
    const auto file = make_path("cursor_color.json");
    Settings with_black = non_default();
    with_black.appearance.palette.cursor_color = RgbaColor{0U, 0U, 0U};
    Settings unset = with_black;
    unset.appearance.palette.cursor_color = std::nullopt;

    {
        Store writer{file};
        AURORA_TEST_CHECK_FALSE(writer.replace(with_black).has_value());
        AURORA_TEST_CHECK_FALSE(writer.replace(unset).has_value());
    }

    const Store reader{file};
    AURORA_TEST_CHECK_FALSE(reader.settings().appearance.palette.cursor_color.has_value());
    AURORA_TEST_CHECK_TRUE(reader.report().rejected_keys.empty());

    {
        Store writer{file};
        AURORA_TEST_CHECK_FALSE(writer.replace(with_black).has_value());
    }
    const Store back{file};
    AURORA_TEST_REQUIRE(back.settings().appearance.palette.cursor_color.has_value());
    AURORA_TEST_CHECK_TRUE(*back.settings().appearance.palette.cursor_color == RgbaColor{0U, 0U, 0U});
}

AURORA_TEST_CASE(bad_values_fall_back_to_defaults_and_are_reported) {
    const auto file = make_path("bad_values.json");
    // 类型不符 / 域外 / 缺键三类各覆盖一处，另带 schema 之外的多余键。快捷键覆盖表取数组形态：
    // 命令 id 里的点会被框架的点号路径模型拆成嵌套对象（见 `ScopeReader::command_overrides`）。
    write_file(file, R"({
  "schema_version": 1,
  "appearance": {
    "font_size_pt": "large",
    "cursor_blink_period_ms": 0,
    "cursor_shape": "triangle",
    "tab_name_priority": "sometimes",
    "sidebar_collapsed": "yes",
    "legacy_widget": true
  },
  "terminal": {
    "bell": "silent",
    "scrollback_limit": -5,
    "encoding": 8
  },
  "connection": 7,
  "shortcuts": {"overrides": [{"command": "app.settings", "combo": 12}]},
  "migrated_from": "0.1"
})");

    const Store store{file};
    const Settings defaults{};
    AURORA_TEST_CHECK_TRUE(store.report().outcome == LoadOutcome::Loaded);
    // 满是坏值的文件仍然起得来，起到的就是全量默认配置——降级不能变成拒启动。
    AURORA_TEST_CHECK_TRUE(store.settings() == defaults);

    const auto &rejected = store.report().rejected_keys;
    for (std::string_view key : {"appearance.font_size_pt", "appearance.cursor_blink_period_ms",
                                 "appearance.cursor_shape", "appearance.tab_name_priority",
                                 "appearance.sidebar_collapsed", "appearance.theme",
                                 "appearance.palette", "terminal.bell", "terminal.scrollback_limit",
                                 "terminal.encoding", "connection", "shortcuts.overrides[0]"}) {
        AURORA_TEST_CHECK_MSG(holds(rejected, key), std::string{key});
    }
    AURORA_TEST_CHECK_TRUE(holds(store.report().unknown_keys, "migrated_from"));
    AURORA_TEST_CHECK_TRUE(holds(store.report().unknown_keys, "appearance.legacy_widget"));
    AURORA_TEST_CHECK_FALSE(holds(store.report().unknown_keys, "terminal.bell"));
}

AURORA_TEST_CASE(missing_domains_are_reported_once_each) {
    const auto file = make_path("empty_domains.json");
    write_file(file, R"({"schema_version": 1})");

    const Store store{file};
    AURORA_TEST_CHECK_TRUE(store.report().outcome == LoadOutcome::Loaded);
    AURORA_TEST_CHECK_TRUE(store.settings() == Settings{});
    // 父作用域整体缺失只留痕父键一次：逐子键刷屏会让截断文件报出几十条噪声。
    AURORA_TEST_CHECK_EQ(store.report().rejected_keys.size(), 4U);
    for (std::string_view domain : {"appearance", "terminal", "connection", "shortcuts"}) {
        AURORA_TEST_CHECK_MSG(holds(store.report().rejected_keys, domain), std::string{domain});
    }
    AURORA_TEST_CHECK_TRUE(store.report().unknown_keys.empty());
}

AURORA_TEST_CASE(damaged_palette_falls_back_to_the_theme_named_in_the_file) {
    const auto file = make_path("theme_fallback.json");
    write_file(file, R"({
  "schema_version": 1,
  "appearance": {"theme": "nord", "palette": {"basic": ["#GGGGGG"]}}
})");

    const Store store{file};
    const auto &appearance = store.settings().appearance;
    AURORA_TEST_CHECK_EQ(appearance.theme, "nord");
    // 用户点名 Nord 而色值段损坏时，屏上应是 Nord 的色，否则主题设置形同被忽略。
    const auto nord = theme_palette("nord");
    AURORA_TEST_CHECK_TRUE(appearance.palette.basic == nord.basic);
    AURORA_TEST_CHECK_TRUE(appearance.palette.default_foreground == nord.default_foreground);
    AURORA_TEST_CHECK_TRUE(appearance.palette.default_background == nord.default_background);
    // cursor 段缺失＝「没写」，随本文件点名的主题色并留痕；只有显式 null 才是「未配」（下一用例）。
    AURORA_TEST_CHECK_TRUE(appearance.palette.cursor_color == nord.cursor_color);
    AURORA_TEST_CHECK_TRUE(appearance.palette.selection_color == nord.selection_color);
    AURORA_TEST_CHECK_NEAR(appearance.palette.min_contrast, 4.5, 0.001);  // 开关类回落随默认值

    const auto &rejected = store.report().rejected_keys;
    AURORA_TEST_CHECK_TRUE(holds(rejected, "appearance.palette.basic"));
    for (std::string_view key : {"appearance.palette.foreground", "appearance.palette.background",
                                 "appearance.palette.cursor", "appearance.palette.selection",
                                 "appearance.palette.bold_is_bright", "appearance.palette.min_contrast_enabled",
                                 "appearance.palette.min_contrast"}) {
        AURORA_TEST_CHECK_MSG(holds(rejected, key), std::string{key});
    }
}

AURORA_TEST_CASE(explicit_null_selection_falls_back_to_bright_black_at_use) {
    const auto file = make_path("selection_null.json");
    // 用户显式清空选区色（`null` 而非缺键）＝点名「未配」，装载侧不留痕（裁决 7.25③ 的同一形态）。
    write_file(file, R"({"schema_version": 1, "appearance": {"theme": "monokai", "palette": {"selection": null}}})");

    const Store store{file};
    const auto &palette = store.settings().appearance.palette;
    AURORA_TEST_CHECK_FALSE(palette.selection_color.has_value());
    AURORA_TEST_CHECK_TRUE(!holds(store.report().rejected_keys, "appearance.palette.selection"));

    // 「未配」的取用形态由 `ui::selection_color` 定死为该色板的 basic[8]（裁决 7.38②）。取 monokai
    // 而不是缺省主题，证明回落线跟着用户点名的主题走：整份文件其余键都按默认，唯有主题被点名。
    const auto monokai = theme_palette("monokai");
    AURORA_TEST_CHECK_TRUE(borealis::ui::selection_color(palette) == monokai.basic[8]);
    AURORA_TEST_CHECK_NE(palette.selection_color, monokai.selection_color);
}

AURORA_TEST_CASE(unreadable_json_is_backed_up_before_defaulting) {
    const auto file = make_path("corrupt.json");
    constexpr std::string_view damage = R"({"schema_version": 1, "appearance": )";
    write_file(file, damage);

    {
        const Store store{file};
        AURORA_TEST_CHECK_TRUE(store.report().outcome == LoadOutcome::RecoveredCorrupt);
        AURORA_TEST_CHECK_TRUE(store.settings() == Settings{});
        AURORA_TEST_CHECK_FALSE(store.report().message.empty());
        AURORA_TEST_REQUIRE(store.report().corrupt_backup.has_value());
        const auto &backup = *store.report().corrupt_backup;
        AURORA_TEST_REQUIRE(std::filesystem::exists(backup));
        // 备份必须保住唯一现场：内容逐字节原样，且文件名带 `.corrupt-` 段。
        AURORA_TEST_CHECK_EQ(read_file(backup), std::string{damage});
        AURORA_TEST_CHECK_TRUE(backup.filename().string().rfind("corrupt.json.corrupt-", 0U) == 0U);
    }

    // 空文件同样是「不是可读 JSON」：手工编辑中断的常见形态。
    const auto empty_file = make_path("empty.json");
    write_file(empty_file, {});
    const Store empty_store{empty_file};
    AURORA_TEST_CHECK_TRUE(empty_store.report().outcome == LoadOutcome::RecoveredCorrupt);
    AURORA_TEST_CHECK_TRUE(empty_store.settings() == Settings{});

    // 备份成功 ⇒ 写盘解禁：降级后的第一次变更要能干净落回默认（裁决 7.26④ 的后半段）。
    {
        Store store{file};
        AURORA_TEST_CHECK_FALSE(store.replace(Settings{}).has_value());
    }
    const Store after{file};
    AURORA_TEST_CHECK_TRUE(after.report().outcome == LoadOutcome::Loaded);
    AURORA_TEST_CHECK_TRUE(after.settings() == Settings{});
    AURORA_TEST_CHECK_TRUE(after.report().rejected_keys.empty());
}

AURORA_TEST_CASE(newer_schema_version_is_backed_up_before_defaulting) {
    const auto file = make_path("newer.json");
    constexpr std::string_view text = R"({"schema_version": 999, "appearance": {"theme": "tokyo-night"}})";
    write_file(file, text);

    const Store store{file};
    AURORA_TEST_CHECK_TRUE(store.report().outcome == LoadOutcome::RecoveredVersion);
    AURORA_TEST_CHECK_TRUE(store.settings() == Settings{});
    AURORA_TEST_CHECK_TRUE(store.report().message.find("999") != std::string::npos);
    AURORA_TEST_REQUIRE(store.report().corrupt_backup.has_value());
    AURORA_TEST_CHECK_EQ(read_file(*store.report().corrupt_backup), std::string{text});
    // 版本判定在 schema 校验之前：高于本仓支持的文件不去逐键试读。
    AURORA_TEST_CHECK_TRUE(store.report().rejected_keys.empty());
}

AURORA_TEST_CASE(stored_file_is_a_single_json_with_four_domains) {
    const auto file = make_path("shape.json");
    {
        Store writer{file};
        AURORA_TEST_CHECK_FALSE(writer.replace(non_default()).has_value());
    }

    const auto parsed = au::json::parse(read_file(file));
    AURORA_TEST_REQUIRE(parsed.ok());
    const auto &root = parsed.value();
    AURORA_TEST_CHECK_TRUE(root.is_object());
    AURORA_TEST_CHECK_EQ(root.as_or<std::int64_t>("schema_version", 0), 1);

    std::vector<std::string> domains;
    for (const auto &entry : root.entries()) {
        domains.emplace_back(entry.key);
    }
    // 框架的元数据键由 `Preferences` 自己读写，不属本仓 schema（`keys()` 亦已剥离它）。
    std::erase(domains, "__aurora_preference_meta__");
    std::ranges::sort(domains);
    AURORA_TEST_CHECK_TRUE((domains == std::vector<std::string>{"appearance", "connection", "schema_version",
                                                                "shortcuts", "terminal"}));
    for (std::string_view domain : {"appearance", "terminal", "connection", "shortcuts"}) {
        const au::json::Value *node = root.at(domain);
        AURORA_TEST_REQUIRE(node != nullptr);
        AURORA_TEST_CHECK_MSG(node->is_object(), std::string{domain});
    }

    // 凭据不入 schema（裁决 7.26⑥）：任何层级的键名都不得是凭据字段。
    std::vector<std::string> keys;
    collect_keys(root, keys);
    for (const auto &key : keys) {
        for (std::string_view banned : {"password", "passphrase", "secret", "token", "private_key"}) {
            AURORA_TEST_CHECK_MSG(key.find(banned) == std::string::npos, key + " ~ " + std::string{banned});
        }
    }
}

}  // namespace borealis::test_cases::utest_config
