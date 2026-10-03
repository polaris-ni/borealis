// ============================================================
// 配置 schema 的读写与损坏降级（src/config/store.cpp）
// ------------------------------------------------------------
// 全仓唯一触达 Aurora `Preferences` 的翻译单元：公共头因此不含框架类型（裁决 7.26③），
// 「临时文件 + 原子 rename + 跨进程 advisory 锁」直接取用框架的 `flush()`，本文件不重造
// （架构 §11.3）；框架不管的两件事在这里做——schema 自校验与 `SPEC.FEAT.PREF.07` 的降级备份
// （裁决 7.26④）。
//
// 键名在「读」与「写」两处各列一遍是刻意的：两侧按同一顺序逐字段对应，往返等值与「写出的键
// 恰是 schema 全集」由 `tests/unit/utest_config.cpp` 守住。表驱动的单一 schema 描述要为 40 个
// 标量做类型擦除，代价高于这层可由测试兜住的重复。
// ============================================================

#include "borealis/config/store.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "aurora/core/json.h"
#include "aurora/preferences/preferences.h"

#include "borealis/config/themes.h"
#include "borealis/grid/storage.h"
#include "borealis/term/width.h"
#include "borealis/ui/palette.h"

namespace borealis::config {
namespace {

namespace au = aurora;
namespace fs = std::filesystem;

using au::json::Value;

/// @brief 本仓支持的最高 schema 版本（裁决 7.26③）；高于它的文件按损坏降级处理。
constexpr std::int64_t kSchemaVersion = 1;
/// @brief 顶层版本键。
constexpr std::string_view kVersionKey = "schema_version";
/// @brief 四个域键名（裁决 7.26① 的四分类）。
constexpr std::array<std::string_view, 4> kDomainKeys{
    {"appearance", "terminal", "connection", "shortcuts"}};

/// @brief 一个枚举值与它在 JSON 里的文本名（写与读共用同一张表，避免两处各列一遍名字）。
struct EnumName {
    std::string_view name;
    std::int64_t value;
};

constexpr std::array<EnumName, 3> kBellNames{{
    {"off", static_cast<std::int64_t>(BellMode::Off)},
    {"visual", static_cast<std::int64_t>(BellMode::Visual)},
    {"audible", static_cast<std::int64_t>(BellMode::Audible)},
}};

constexpr std::array<EnumName, 2> kLongLineNames{{
    {"truncate", static_cast<std::int64_t>(LongLinePolicy::Truncate)},
    {"wrap", static_cast<std::int64_t>(LongLinePolicy::Wrap)},
}};

constexpr std::array<EnumName, 3> kPasteNewlineNames{{
    {"as_is", static_cast<std::int64_t>(PasteNewlinePolicy::AsIs)},
    {"filter", static_cast<std::int64_t>(PasteNewlinePolicy::Filter)},
    {"convert", static_cast<std::int64_t>(PasteNewlinePolicy::Convert)},
}};

constexpr std::array<EnumName, 3> kRightClickNames{{
    {"context_menu", static_cast<std::int64_t>(RightClickAction::ContextMenu)},
    {"paste", static_cast<std::int64_t>(RightClickAction::Paste)},
    {"copy_on_select", static_cast<std::int64_t>(RightClickAction::CopyOnSelect)},
}};

constexpr std::array<EnumName, 2> kTabNamePriorityNames{{
    {"manual_wins", static_cast<std::int64_t>(TabNamePriority::ManualWins)},
    {"osc_wins", static_cast<std::int64_t>(TabNamePriority::OscWins)},
}};

constexpr std::array<EnumName, 2> kAmbiguousWidthNames{{
    {"narrow", static_cast<std::int64_t>(term::AmbiguousWidth::Narrow)},
    {"wide", static_cast<std::int64_t>(term::AmbiguousWidth::Wide)},
}};

constexpr std::array<EnumName, 3> kCursorShapeNames{{
    {"block", static_cast<std::int64_t>(term::CursorShape::Block)},
    {"underline", static_cast<std::int64_t>(term::CursorShape::Underline)},
    {"bar", static_cast<std::int64_t>(term::CursorShape::Bar)},
}};

constexpr std::array<std::string_view, 4> kSshAuthMethods{"password", "privatekey", "agent", "keyboard-interactive"};

constexpr std::array<std::string_view, 3> kSerialParities{"none", "even", "odd"};

constexpr std::array<std::string_view, 3> kSerialLineEndings{"LF", "CR", "CRLF"};

/// @brief 按名取枚举值。
/// @param names 映射表。
/// @param name JSON 文本名。
/// @return 命中的底层值；表里没有时为空（由调用方回落默认并留痕）。
[[nodiscard]] auto value_of(std::span<const EnumName> names, std::string_view name)
    -> std::optional<std::int64_t> {
    const auto found = std::ranges::find(names, name, &EnumName::name);
    return found == names.end() ? std::optional<std::int64_t>{} : std::optional<std::int64_t>{found->value};
}

/// @brief 按枚举值取名。
/// @param names 映射表。
/// @param value 枚举的底层值。
/// @return 文本名；表里没有时为空串，该键于是**不写盘**（读回时按缺键回落默认）。
[[nodiscard]] auto name_of(std::span<const EnumName> names, std::int64_t value) -> std::string_view {
    const auto found = std::ranges::find(names, value, &EnumName::value);
    return found == names.end() ? std::string_view{} : found->name;
}

/// @brief 色值 → `#RRGGBB`。alpha 不参与存储：终端色带不做透明混合（`ui::contrast_ratio` 同口径）。
[[nodiscard]] auto color_to_text(ui::RgbaColor color) -> std::string {
    static constexpr std::string_view kDigits = "0123456789ABCDEF";
    std::string text{'#'};
    for (const std::uint8_t channel : {color.red, color.green, color.blue}) {
        text.push_back(kDigits[(channel >> 4U) & 0x0FU]);
        text.push_back(kDigits[channel & 0x0FU]);
    }
    return text;
}

/// @brief 取一个十六进制位的值。
[[nodiscard]] auto hex_digit(char digit) -> std::optional<std::uint8_t> {
    if (digit >= '0' && digit <= '9') {
        return static_cast<std::uint8_t>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f') {
        return static_cast<std::uint8_t>(digit - 'a' + 10);
    }
    if (digit >= 'A' && digit <= 'F') {
        return static_cast<std::uint8_t>(digit - 'A' + 10);
    }
    return std::nullopt;
}

/// @brief `#RRGGBB` → 色值（大小写不敏感）。
[[nodiscard]] auto color_from_text(std::string_view text) -> std::optional<ui::RgbaColor> {
    if (text.size() != 7U || text.front() != '#') {
        return std::nullopt;
    }
    std::uint8_t channels[3]{};
    for (std::size_t index = 0; index < 3U; ++index) {
        const auto high = hex_digit(text[1 + index * 2]);
        const auto low = hex_digit(text[2 + index * 2]);
        if (!high || !low) {
            return std::nullopt;
        }
        channels[index] = static_cast<std::uint8_t>((*high << 4U) | *low);
    }
    return ui::RgbaColor{channels[0], channels[1], channels[2]};
}

/// @brief JSON 值 → 色值：非字符串或形态不合都算解析失败。
[[nodiscard]] auto color_from(const Value &value) -> std::optional<ui::RgbaColor> {
    const auto text = value.as_string();
    if (!text) {
        return std::nullopt;
    }
    return color_from_text(*text);
}

/// @brief 指针取文本：节点缺失（`nullptr`）与类型不符都归为「没读到」。
[[nodiscard]] auto text_of(const Value *node) -> std::optional<std::string_view> {
    return node == nullptr ? std::nullopt : node->as_string();
}

/// @brief 按 schema 读取一个作用域（裁决 7.26④）。
///
/// 三类不合一律回落默认并把这个键（点号路径）记进 `LoadReport::rejected_keys`：键缺失、
/// 类型不符、值在定义域外。**父作用域整体缺失时只留痕父键一次**，不逐子键刷屏——截断的文件
/// 报「appearance 没了」比报 20 条子键更可诊断。
///
/// schema 之外的键由 `collect_unknown()` 单独登记：只记录不报错，否则旧版本或手写的多余键
/// 会让新版本拒绝装载。
class ScopeReader {
  public:
    /// @brief 绑定作用域。
    /// @param path 该作用域的点号路径（顶层作用域为空串）。
    /// @param node 作用域节点；非对象或不存在时传 `nullptr`。
    /// @param report 留痕去向。
    ScopeReader(std::string path, const Value *node, LoadReport &report)
        : path_(std::move(path)), node_(node), report_(report) {}

    /// @brief 进入子作用域。
    /// @param key 子作用域键名。
    /// @return 子作用域读取器；缺失或类型不符时其 `node_` 为空。
    [[nodiscard]] auto enter(std::string_view key) -> ScopeReader {
        known_.emplace_back(key);
        const auto child_path = qualified(key);
        if (node_ == nullptr) {
            return ScopeReader{child_path, nullptr, report_};
        }
        const Value *child = node_->at(key);
        if ((child == nullptr) || !child->is_object()) {
            report_.rejected_keys.push_back(child_path);
            return ScopeReader{child_path, nullptr, report_};
        }
        return ScopeReader{child_path, child, report_};
    }

    /// @brief 登记本作用域内 schema 之外的键。
    auto collect_unknown() -> void {
        if (node_ == nullptr) {
            return;
        }
        for (const auto &entry : node_->entries()) {
            const std::string key(entry.key);
            if (std::ranges::find(known_, key) == known_.end()) {
                report_.unknown_keys.push_back(qualified(key));
            }
        }
    }

    /// @brief 读布尔。
    [[nodiscard]] auto boolean(std::string_view key, bool fallback) -> bool {
        return value_or(parse(key, [](const Value &value) { return value.as_bool(); }), fallback);
    }

    /// @brief 读整数并限定在 `[lo, hi]`。
    [[nodiscard]] auto integer(std::string_view key, std::int64_t fallback, std::int64_t lo, std::int64_t hi)
        -> std::int64_t {
        const auto value = parse(key, [](const Value &item) { return item.as_int(); });
        if (!value) {
            return fallback;
        }
        if (*value < lo || *value > hi) {
            reject(key);
            return fallback;
        }
        return *value;
    }

    /// @brief 读浮点并限定在 `[lo, hi]`。
    [[nodiscard]] auto real(std::string_view key, double fallback, double lo, double hi) -> double {
        const auto value = parse(key, [](const Value &item) { return item.as_double(); });
        if (!value) {
            return fallback;
        }
        if (*value < lo || *value > hi) {
            reject(key);
            return fallback;
        }
        return *value;
    }

    /// @brief 读自由文本（路径、字体名一类：只判类型，不判域）。
    [[nodiscard]] auto text(std::string_view key, std::string_view fallback) -> std::string {
        const auto value = parse(key, [](const Value &item) { return item.as_string(); });
        return value ? std::string(*value) : std::string(fallback);
    }

    /// @brief 读取值受限的文本（域外回落默认）。
    [[nodiscard]] auto one_of(std::string_view key,
                              std::string_view fallback,
                              std::span<const std::string_view> allowed) -> std::string {
        const auto value = parse(key, [](const Value &item) { return item.as_string(); });
        if (!value) {
            return std::string(fallback);
        }
        if (std::ranges::find(allowed, *value) == allowed.end()) {
            reject(key);
            return std::string(fallback);
        }
        return std::string(*value);
    }

    /// @brief 读枚举（文本名 ↔ 枚举值见 `EnumName` 表）。
    [[nodiscard]] auto enumerated(std::string_view key, std::span<const EnumName> names, std::int64_t fallback)
        -> std::int64_t {
        const auto value = parse(key, [names](const Value &item) {
            const auto text = item.as_string();
            return text ? value_of(names, *text) : std::optional<std::int64_t>{};
        });
        return value ? *value : fallback;
    }

    /// @brief 读单个色值。
    [[nodiscard]] auto color(std::string_view key, const ui::RgbaColor &fallback) -> ui::RgbaColor {
        return value_or(parse(key, [](const Value &value) { return color_from(value); }), fallback);
    }

    /// @brief 读 16 色基本表：元素数不符或任一格形态不合，整键回落（半套色比没有色更难解释）。
    [[nodiscard]] auto colors(std::string_view key, const std::array<ui::RgbaColor, 16> &fallback)
        -> std::array<ui::RgbaColor, 16> {
        const auto value = parse(key, [](const Value &item) -> std::optional<std::array<ui::RgbaColor, 16>> {
            if (!item.is_array() || item.size() != 16U) {
                return std::nullopt;
            }
            std::array<ui::RgbaColor, 16> out{};
            for (std::size_t index = 0; index < out.size(); ++index) {
                const Value *element = item.at(index);
                if (element == nullptr) {
                    return std::nullopt;
                }
                const auto color = color_from(*element);
                if (!color) {
                    return std::nullopt;
                }
                out[index] = *color;
            }
            return out;
        });
        return value ? *value : fallback;
    }

    /// @brief 读可缺省色值。
    ///
    /// 「缺键」与「显式 null」是两种状态：缺键与其余键同口径（回落并留痕），回落值取本文件点名
    /// 主题的色（与 `basic`/`foreground` 同一回落线，否则 one-dark 这类光标色不同于前景的主题会
    /// 被画成前景色，主题设置形同被忽略）；显式 null 才是用户点名的「未配」，
    /// 绘制侧回落 `default_foreground`（裁决 7.25③），不留痕。
    [[nodiscard]] auto optional_color(std::string_view key, const std::optional<ui::RgbaColor> &fallback)
        -> std::optional<ui::RgbaColor> {
        known_.emplace_back(key);
        if (node_ == nullptr) {
            return fallback;
        }
        const Value *value = node_->at(key);
        if (value == nullptr) {
            reject(key);
            return fallback;
        }
        if (value->is_null()) {
            return std::nullopt;
        }
        const auto color = color_from(*value);
        if (!color) {
            reject(key);
            return fallback;
        }
        return color;
    }

    /// @brief 读快捷键覆盖表：数组元素形态是 `{"command": ..., "combo": ...}`。
    ///
    /// 不用「命令 id 作对象键」的映射形态：框架的持久化模型把点号当路径分隔符
    /// （`Preferences::reconcile` 先拍平再按点号重建嵌套），命令 id 里的点会被拆成一层层
    /// 嵌套对象。元素不合形态只丢该元素并留痕其下标，不让整张表失效。
    [[nodiscard]] auto command_overrides(std::string_view key, std::map<std::string, std::string> fallback)
        -> std::map<std::string, std::string> {
        known_.emplace_back(key);
        if (node_ == nullptr) {
            return fallback;
        }
        const Value *value = node_->at(key);
        if ((value == nullptr) || !value->is_array()) {
            reject(key);
            return fallback;
        }
        std::map<std::string, std::string> out;
        for (std::size_t index = 0; index < value->size(); ++index) {
            const Value *item = value->at(index);
            const auto command = text_of(item == nullptr ? nullptr : item->at("command"));
            const auto combo = text_of(item == nullptr ? nullptr : item->at("combo"));
            if (!command || !combo) {
                report_.rejected_keys.push_back(qualified(key) + "[" + std::to_string(index) + "]");
                continue;
            }
            out[std::string{*command}] = std::string{*combo};
        }
        return out;
    }

  private:
    /// @brief 取键并解析：缺失 / null / 解析失败都留痕一次，返回空 optional。
    /// @tparam Parser 返回 `std::optional<T>` 的可调用件。
    template <typename Parser>
    [[nodiscard]] auto parse(std::string_view key, Parser parser)
        -> decltype(parser(std::declval<const Value &>())) {
        using Parsed = decltype(parser(std::declval<const Value &>()));
        known_.emplace_back(key);
        if (node_ == nullptr) {
            return Parsed{};
        }
        const Value *value = node_->at(key);
        if ((value == nullptr) || value->is_null()) {
            reject(key);
            return Parsed{};
        }
        Parsed parsed = parser(*value);
        if (!parsed) {
            reject(key);
        }
        return parsed;
    }

    /// @brief 回落值：解析没成功时用它，并把键记进诊断。
    template <typename T>
    [[nodiscard]] static auto value_or(const std::optional<T> &value, const T &fallback) -> T {
        return value ? *value : fallback;
    }

    /// @brief 把作用域内键名拼成点号路径。
    [[nodiscard]] auto qualified(std::string_view key) const -> std::string {
        return path_.empty() ? std::string(key) : path_ + "." + std::string(key);
    }

    /// @brief 记一个被回落的键。
    auto reject(std::string_view key) -> void { report_.rejected_keys.push_back(qualified(key)); }

    std::string path_;
    const Value *node_;
    LoadReport &report_;
    std::vector<std::string> known_;
};

/// @brief 写一个标量进对象节点。
template <typename T>
auto put(Value &node, std::string_view key, T value) -> void {
    node.set(key, Value(std::move(value)));
}

/// @brief 写枚举：值不在表里（本仓新增档位而旧配置读入）时整键不写，读回按缺键回落默认。
auto put_enum(Value &node, std::string_view key, std::span<const EnumName> names, std::int64_t value) -> void {
    const auto name = name_of(names, value);
    if (!name.empty()) {
        node.set(key, Value(name));
    }
}

/// @brief 写调色板：`cursor` / `selection` 为 null 表示「未配」，与「配了黑色」可区分（裁决 7.25③、7.38②）。
[[nodiscard]] auto palette_to_json(const ui::PaletteSpec &spec) -> Value {
    auto node = Value::object();
    auto basic = Value::array();
    for (const auto &color : spec.basic) {
        basic.push_back(Value(color_to_text(color)));
    }
    node.set("basic", std::move(basic));
    put(node, "foreground", color_to_text(spec.default_foreground));
    put(node, "background", color_to_text(spec.default_background));
    node.set("cursor", spec.cursor_color ? Value(color_to_text(*spec.cursor_color)) : Value(nullptr));
    node.set("selection", spec.selection_color ? Value(color_to_text(*spec.selection_color)) : Value(nullptr));
    put(node, "bold_is_bright", spec.bold_is_bright);
    put(node, "min_contrast_enabled", spec.min_contrast_enabled);
    put(node, "min_contrast", spec.min_contrast);
    return node;
}

/// @brief 写状态栏的 10 个条目开关（声明次序即宽度不足时的省略次序，裁决 7.25⑧）。
[[nodiscard]] auto status_bar_to_json(const StatusBarSettings &bar) -> Value {
    auto node = Value::object();
    put(node, "show_connection", bar.show_connection);
    put(node, "show_reconnect", bar.show_reconnect);
    put(node, "show_cursor_position", bar.show_cursor_position);
    put(node, "show_encoding", bar.show_encoding);
    put(node, "show_grid_size", bar.show_grid_size);
    put(node, "show_font_size", bar.show_font_size);
    put(node, "show_theme", bar.show_theme);
    put(node, "show_scrollback", bar.show_scrollback);
    put(node, "show_clipboard_policy", bar.show_clipboard_policy);
    put(node, "show_input_latency", bar.show_input_latency);
    return node;
}

[[nodiscard]] auto appearance_to_json(const AppearanceSettings &appearance) -> Value {
    auto node = Value::object();
    put(node, "theme", appearance.theme);
    put(node, "font_family", appearance.font_family);
    put(node, "font_size_pt", appearance.font_size_pt);
    put(node, "font_line_height", appearance.font_line_height);
    put(node, "font_letter_spacing_dp", appearance.font_letter_spacing_dp);
    put(node, "viewport_padding_dp", appearance.viewport_padding_dp);
    put_enum(node, "cursor_shape", kCursorShapeNames, static_cast<std::int64_t>(appearance.cursor_shape));
    put(node, "cursor_blinking", appearance.cursor_blinking);
    put(node, "cursor_blink_period_ms", appearance.cursor_blink_period_ms);
    put(node, "sidebar_collapsed", appearance.sidebar_collapsed);
    put_enum(node, "tab_name_priority", kTabNamePriorityNames,
             static_cast<std::int64_t>(appearance.tab_name_priority));
    node.set("status_bar", status_bar_to_json(appearance.status_bar));
    node.set("palette", palette_to_json(appearance.palette));
    return node;
}

[[nodiscard]] auto terminal_to_json(const TerminalSettings &terminal) -> Value {
    auto node = Value::object();
    put(node, "scrollback_limit", static_cast<std::int64_t>(terminal.scrollback_limit));
    put_enum(node, "ambiguous_width", kAmbiguousWidthNames, static_cast<std::int64_t>(terminal.ambiguous_width));
    put_enum(node, "long_line", kLongLineNames, static_cast<std::int64_t>(terminal.long_line));
    put_enum(node, "bell", kBellNames, static_cast<std::int64_t>(terminal.bell));
    put(node, "encoding", terminal.encoding);
    put_enum(node, "paste_newlines", kPasteNewlineNames, static_cast<std::int64_t>(terminal.paste_newlines));
    put_enum(node, "right_click", kRightClickNames, static_cast<std::int64_t>(terminal.right_click));
    put(node, "copy_on_select", terminal.copy_on_select);
    put(node, "trim_pasted_trailing_space", terminal.trim_pasted_trailing_space);
    put(node, "smart_line_join", terminal.smart_line_join);
    put(node, "strip_tmux_border_chars", terminal.strip_tmux_border_chars);
    put(node, "word_delimiters", terminal.word_delimiters);
    return node;
}

[[nodiscard]] auto connection_to_json(const ConnectionSettings &connection) -> Value {
    auto node = Value::object();
    put(node, "local_shell", connection.local_shell);
    put(node, "startup_directory", connection.startup_directory);

    auto ssh = Value::object();
    put(ssh, "port", connection.ssh.port);
    put(ssh, "auth_method", connection.ssh.auth_method);
    put(ssh, "agent_forwarding", connection.ssh.agent_forwarding);
    put(ssh, "keepalive_interval_sec", connection.ssh.keepalive_interval_sec);
    put(ssh, "connect_timeout_sec", connection.ssh.connect_timeout_sec);
    node.set("ssh", std::move(ssh));

    auto serial = Value::object();
    put(serial, "baud", connection.serial.baud);
    put(serial, "data_bits", connection.serial.data_bits);
    put(serial, "stop_bits", connection.serial.stop_bits);
    put(serial, "parity", connection.serial.parity);
    put(serial, "line_ending", connection.serial.line_ending);
    put(serial, "encoding", connection.serial.encoding);
    node.set("serial", std::move(serial));

    put(node, "session_logging", connection.session_logging);
    put(node, "session_log_dir", connection.session_log_dir);
    return node;
}

[[nodiscard]] auto shortcuts_to_json(const ShortcutsSettings &shortcuts) -> Value {
    auto node = Value::object();
    auto overrides = Value::array();
    for (const auto &[command, combo] : shortcuts.overrides) {
        auto entry = Value::object();
        put(entry, "command", command);
        put(entry, "combo", combo);
        overrides.push_back(std::move(entry));
    }
    // 数组而不是映射：命令 id 里的点会被框架的点号路径模型拆成嵌套对象（见 `command_overrides`）；
    // 空表也必须是留得下来的叶子，空对象在装载时会被拍平掉。
    node.set("overrides", std::move(overrides));
    return node;
}

/// @brief 读外观域。
[[nodiscard]] auto read_appearance(ScopeReader &scope, const Settings &defaults) -> AppearanceSettings {
    AppearanceSettings appearance{};
    appearance.theme = scope.text("theme", defaults.appearance.theme);
    appearance.font_family = scope.text("font_family", defaults.appearance.font_family);
    appearance.font_size_pt = scope.real("font_size_pt", defaults.appearance.font_size_pt, 6.0, 72.0);
    // 两档排版可调量的取值域在此把守，`ui::apply_typography` 因此不含夹取（裁决 7.46②）。
    appearance.font_line_height = scope.real("font_line_height", defaults.appearance.font_line_height, 1.0, 3.0);
    appearance.font_letter_spacing_dp =
        scope.real("font_letter_spacing_dp", defaults.appearance.font_letter_spacing_dp, 0.0, 8.0);
    appearance.viewport_padding_dp =
        static_cast<float>(scope.real("viewport_padding_dp", defaults.appearance.viewport_padding_dp, 0.0, 64.0));
    appearance.cursor_shape = static_cast<term::CursorShape>(
        scope.enumerated("cursor_shape", kCursorShapeNames, static_cast<std::int64_t>(defaults.appearance.cursor_shape)));
    appearance.cursor_blinking = scope.boolean("cursor_blinking", defaults.appearance.cursor_blinking);
    appearance.cursor_blink_period_ms =
        static_cast<int>(scope.integer("cursor_blink_period_ms", defaults.appearance.cursor_blink_period_ms, 50, 5000));
    appearance.sidebar_collapsed = scope.boolean("sidebar_collapsed", defaults.appearance.sidebar_collapsed);
    appearance.tab_name_priority = static_cast<TabNamePriority>(scope.enumerated(
        "tab_name_priority", kTabNamePriorityNames, static_cast<std::int64_t>(defaults.appearance.tab_name_priority)));

    auto bar = scope.enter("status_bar");
    appearance.status_bar.show_connection = bar.boolean("show_connection", defaults.appearance.status_bar.show_connection);
    appearance.status_bar.show_reconnect = bar.boolean("show_reconnect", defaults.appearance.status_bar.show_reconnect);
    appearance.status_bar.show_cursor_position =
        bar.boolean("show_cursor_position", defaults.appearance.status_bar.show_cursor_position);
    appearance.status_bar.show_encoding = bar.boolean("show_encoding", defaults.appearance.status_bar.show_encoding);
    appearance.status_bar.show_grid_size = bar.boolean("show_grid_size", defaults.appearance.status_bar.show_grid_size);
    appearance.status_bar.show_font_size = bar.boolean("show_font_size", defaults.appearance.status_bar.show_font_size);
    appearance.status_bar.show_theme = bar.boolean("show_theme", defaults.appearance.status_bar.show_theme);
    appearance.status_bar.show_scrollback =
        bar.boolean("show_scrollback", defaults.appearance.status_bar.show_scrollback);
    appearance.status_bar.show_clipboard_policy =
        bar.boolean("show_clipboard_policy", defaults.appearance.status_bar.show_clipboard_policy);
    appearance.status_bar.show_input_latency =
        bar.boolean("show_input_latency", defaults.appearance.status_bar.show_input_latency);
    bar.collect_unknown();

    // 调色板的回落值随**本文件里的主题名**，而不是随 Dracula：用户点名 Nord 而色值段损坏时，
    // 屏上应是 Nord 的色，否则主题设置形同被忽略。
    auto palette = scope.enter("palette");
    const ui::PaletteSpec themed = theme_palette(appearance.theme);
    appearance.palette.basic = palette.colors("basic", themed.basic);
    appearance.palette.default_foreground =
        palette.color("foreground", themed.default_foreground);
    appearance.palette.default_background =
        palette.color("background", themed.default_background);
    appearance.palette.cursor_color = palette.optional_color("cursor", themed.cursor_color);
    appearance.palette.selection_color = palette.optional_color("selection", themed.selection_color);
    appearance.palette.bold_is_bright = palette.boolean("bold_is_bright", defaults.appearance.palette.bold_is_bright);
    appearance.palette.min_contrast_enabled =
        palette.boolean("min_contrast_enabled", defaults.appearance.palette.min_contrast_enabled);
    appearance.palette.min_contrast =
        palette.real("min_contrast", defaults.appearance.palette.min_contrast, 1.0, 21.0);
    palette.collect_unknown();

    scope.collect_unknown();
    return appearance;
}

/// @brief 读终端域。
[[nodiscard]] auto read_terminal(ScopeReader &scope, const Settings &defaults) -> TerminalSettings {
    TerminalSettings terminal{};
    terminal.scrollback_limit = static_cast<std::size_t>(scope.integer(
        "scrollback_limit",
        static_cast<std::int64_t>(defaults.terminal.scrollback_limit),
        0,
        static_cast<std::int64_t>(grid::kMaxScrollbackLimit)));
    terminal.ambiguous_width = static_cast<term::AmbiguousWidth>(scope.enumerated(
        "ambiguous_width", kAmbiguousWidthNames, static_cast<std::int64_t>(defaults.terminal.ambiguous_width)));
    terminal.long_line = static_cast<LongLinePolicy>(
        scope.enumerated("long_line", kLongLineNames, static_cast<std::int64_t>(defaults.terminal.long_line)));
    terminal.bell = static_cast<BellMode>(
        scope.enumerated("bell", kBellNames, static_cast<std::int64_t>(defaults.terminal.bell)));
    terminal.encoding = scope.text("encoding", defaults.terminal.encoding);
    terminal.paste_newlines = static_cast<PasteNewlinePolicy>(scope.enumerated(
        "paste_newlines", kPasteNewlineNames, static_cast<std::int64_t>(defaults.terminal.paste_newlines)));
    terminal.right_click = static_cast<RightClickAction>(
        scope.enumerated("right_click", kRightClickNames, static_cast<std::int64_t>(defaults.terminal.right_click)));
    terminal.copy_on_select = scope.boolean("copy_on_select", defaults.terminal.copy_on_select);
    terminal.trim_pasted_trailing_space =
        scope.boolean("trim_pasted_trailing_space", defaults.terminal.trim_pasted_trailing_space);
    terminal.smart_line_join = scope.boolean("smart_line_join", defaults.terminal.smart_line_join);
    terminal.strip_tmux_border_chars =
        scope.boolean("strip_tmux_border_chars", defaults.terminal.strip_tmux_border_chars);
    terminal.word_delimiters = scope.text("word_delimiters", defaults.terminal.word_delimiters);
    scope.collect_unknown();
    return terminal;
}

/// @brief 读连接域（凭据不在 schema 内，裁决 7.26⑥）。
[[nodiscard]] auto read_connection(ScopeReader &scope, const Settings &defaults) -> ConnectionSettings {
    ConnectionSettings connection{};
    connection.local_shell = scope.text("local_shell", defaults.connection.local_shell);
    connection.startup_directory = scope.text("startup_directory", defaults.connection.startup_directory);

    auto ssh = scope.enter("ssh");
    connection.ssh.port =
        static_cast<int>(ssh.integer("port", defaults.connection.ssh.port, 1, 65535));
    connection.ssh.auth_method =
        ssh.one_of("auth_method", defaults.connection.ssh.auth_method, kSshAuthMethods);
    connection.ssh.agent_forwarding = ssh.boolean("agent_forwarding", defaults.connection.ssh.agent_forwarding);
    connection.ssh.keepalive_interval_sec =
        static_cast<int>(ssh.integer("keepalive_interval_sec", defaults.connection.ssh.keepalive_interval_sec, 0, 86400));
    connection.ssh.connect_timeout_sec =
        static_cast<int>(ssh.integer("connect_timeout_sec", defaults.connection.ssh.connect_timeout_sec, 0, 600));
    ssh.collect_unknown();

    auto serial = scope.enter("serial");
    connection.serial.baud =
        static_cast<int>(serial.integer("baud", defaults.connection.serial.baud, 1, 4000000));
    connection.serial.data_bits =
        static_cast<int>(serial.integer("data_bits", defaults.connection.serial.data_bits, 5, 8));
    connection.serial.stop_bits =
        static_cast<int>(serial.integer("stop_bits", defaults.connection.serial.stop_bits, 1, 2));
    connection.serial.parity = serial.one_of("parity", defaults.connection.serial.parity, kSerialParities);
    connection.serial.line_ending =
        serial.one_of("line_ending", defaults.connection.serial.line_ending, kSerialLineEndings);
    connection.serial.encoding = serial.text("encoding", defaults.connection.serial.encoding);
    serial.collect_unknown();

    connection.session_logging = scope.boolean("session_logging", defaults.connection.session_logging);
    connection.session_log_dir = scope.text("session_log_dir", defaults.connection.session_log_dir);
    scope.collect_unknown();
    return connection;
}

/// @brief 读快捷键域。
[[nodiscard]] auto read_shortcuts(ScopeReader &scope, const Settings &defaults) -> ShortcutsSettings {
    ShortcutsSettings shortcuts{};
    shortcuts.overrides = scope.command_overrides("overrides", defaults.shortcuts.overrides);
    scope.collect_unknown();
    return shortcuts;
}

/// @brief 从存储读出一份配置，并把校验留痕写进报告。
[[nodiscard]] auto read_settings(const au::preferences::Preferences &prefs, LoadReport &report) -> Settings {
    const Settings defaults{};
    Settings settings{};

    for (std::string_view domain : kDomainKeys) {
        const Value node = prefs.get<Value>(std::string(domain), Value{});
        ScopeReader scope(std::string(domain), node.is_object() ? &node : nullptr, report);
        if (!node.is_object()) {
            report.rejected_keys.push_back(std::string(domain));
        }
        if (domain == "appearance") {
            settings.appearance = read_appearance(scope, defaults);
        } else if (domain == "terminal") {
            settings.terminal = read_terminal(scope, defaults);
        } else if (domain == "connection") {
            settings.connection = read_connection(scope, defaults);
        } else {
            settings.shortcuts = read_shortcuts(scope, defaults);
        }
    }

    for (const auto &key : prefs.keys()) {
        if ((key == kVersionKey) || (std::ranges::find(kDomainKeys, key) != kDomainKeys.end())) {
            continue;
        }
        report.unknown_keys.push_back(key);
    }
    return settings;
}

/// @brief 把损坏文件备份为 `<file>.corrupt-<epoch 秒>`（裁决 7.26④）。
///
/// 备份必须先于任何落盘：降级之后用户第一次改设置就会 `flush`，此时原文件是唯一现场。
/// @param file 损坏的配置文件。
/// @return 备份路径；复制失败时为空，调用方据此**拒绝再写这个文件**。
[[nodiscard]] auto make_corrupt_backup(const fs::path &file) -> std::optional<fs::path> {
    const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const auto target = fs::path(file.string() + ".corrupt-" + std::to_string(epoch));
    std::error_code ec;
    fs::copy_file(file, target, fs::copy_options::skip_existing, ec);
    if (ec) {
        return std::nullopt;
    }
    return target;
}

}  // namespace

/// @brief 实现体：`Preferences` 含互斥量（不可搬移），故以 `optional` 就地构造。
///
/// 装载与降级写成成员而不是匿名namespace 的自由函数：`Store::Impl` 是私有嵌套类型，
/// 类外的函数签名里点它的名字就不合法（MSVC C2248）。
struct Store::Impl {
    /// @brief 装载配置文件：文件缺失即首次启动，不产生任何写入（裁决 7.26⑤）。
    auto load(const fs::path &file) -> void {
        report.file = file;
        prefs.emplace(file);
        std::error_code ec;
        if (!fs::exists(file, ec)) {
            report.outcome = LoadOutcome::FirstRun;
            return;
        }
        if (const auto error = prefs->last_load_error(); error.has_value()) {
            recover(file, LoadOutcome::RecoveredCorrupt, "config file is not readable JSON: " + error->message);
            return;
        }
        const auto version = prefs->get<std::int64_t>(std::string(kVersionKey), 0);
        if (version > kSchemaVersion) {
            recover(file,
                    LoadOutcome::RecoveredVersion,
                    "config schema version " + std::to_string(version) + " is newer than supported " +
                        std::to_string(kSchemaVersion));
            return;
        }
        report.outcome = LoadOutcome::Loaded;
        settings = read_settings(*prefs, report);
    }

    /// @brief 提交并落盘。
    auto store(const Settings &next) -> std::optional<std::string> {
        settings = next;
        if (!writes_allowed) {
            return std::string("refused to write the config file: the damaged original was not backed up");
        }
        auto &prefs = *this->prefs;
        prefs.set(std::string(kVersionKey), kSchemaVersion);
        prefs.set("appearance", appearance_to_json(next.appearance));
        prefs.set("terminal", terminal_to_json(next.terminal));
        prefs.set("connection", connection_to_json(next.connection));
        prefs.set("shortcuts", shortcuts_to_json(next.shortcuts));
        if (const auto flushed = prefs.flush(); !flushed.ok()) {
            return flushed.error().message;
        }
        return std::nullopt;
    }

    std::optional<au::preferences::Preferences> prefs;
    Settings settings{};
    LoadReport report{};
    bool writes_allowed{true};

  private:
    /// @brief 降级：备份损坏文件并置报告；备份没成功就禁止再写这个文件（否则唯一现场被覆盖）。
    auto recover(const fs::path &file, LoadOutcome outcome, std::string reason) -> void {
        report.outcome = outcome;
        report.message = std::move(reason);
        report.corrupt_backup = make_corrupt_backup(file);
        if (!report.corrupt_backup) {
            writes_allowed = false;
            report.message += " (backup failed, the file is kept as is and writes are refused)";
        }
    }
};

Store::Store() : Store(au::preferences::Preferences::default_config_dir() / kConfigFileName) {}

Store::Store(std::filesystem::path file) : impl_(std::make_unique<Impl>()) {
    impl_->load(file);
}

Store::Store(Store &&other) noexcept = default;

auto Store::operator=(Store &&other) noexcept -> Store & = default;

Store::~Store() noexcept = default;

auto Store::settings() const noexcept -> const Settings & {
    return impl_->settings;
}

auto Store::report() const noexcept -> const LoadReport & {
    return impl_->report;
}

auto Store::replace(const Settings &next) -> std::optional<std::string> {
    return impl_->store(next);
}

}  // namespace borealis::config
