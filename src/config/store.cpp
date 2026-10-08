// ============================================================
// 配置 schema 的读写、损坏降级与快照回滚（src/config/store.cpp）
// ------------------------------------------------------------
// 全仓唯一触达 Aurora `Preferences` 的翻译单元：公共头因此不含框架类型（裁决 7.26③），
// 「临时文件 + 原子 rename + 跨进程 advisory 锁」直接取用框架的 `flush()`，本文件不重造
// （架构 §11.3）；框架不管的三件事在这里做——schema 自校验、`SPEC.FEAT.PREF.07` 的降级备份
// （裁决 7.26④），以及同一那份需求的快照滚动与导出导入（裁决 7.87；导出件的落盘按框架
// `flush()` 的同一条纪律自己做一次「临时文件 + rename」，因为目标路径由用户点名而框架只认自家配置文件）。
//
// 键名在「读」与「写」两处各列一遍是刻意的：两侧按同一顺序逐字段对应，往返等值与「写出的键
// 恰是 schema 全集」由 `tests/unit/utest_config.cpp` 守住。表驱动的单一 schema 描述要为 40 个
// 标量做类型擦除，代价高于这层可由测试兜住的重复。
// ============================================================

#include "borealis/config/store.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "aurora/core/json.h"
#include "aurora/core/log.h"
#include "aurora/preferences/preferences.h"

#include "borealis/config/themes.h"
#include "borealis/grid/storage.h"
#include "borealis/term/width.h"
#include "borealis/ui/color_text.h"
#include "borealis/ui/palette.h"

#include "schema_names.h"

namespace borealis::config {
namespace {

namespace au = aurora;
namespace fs = std::filesystem;

using au::json::Value;

/// @brief 顶层版本键。
constexpr std::string_view kVersionKey = "schema_version";
/// @brief 四个域键名（裁决 7.26① 的四分类）。
constexpr std::array<std::string_view, 4> kDomainKeys{
    {"appearance", "terminal", "connection", "shortcuts"}};

constexpr std::array<std::string_view, 4> kSshAuthMethods{"password", "privatekey", "agent", "keyboard-interactive"};

constexpr std::array<std::string_view, 3> kSerialParities{"none", "even", "odd"};

constexpr std::array<std::string_view, 3> kSerialLineEndings{"LF", "CR", "CRLF"};

/// @brief JSON 值 → 色值：非字符串或形态不合都算解析失败。
///
/// 校验式取自 `ui::color_from_hex` 而不是在本文件另写一份：设置面板的色值输入框收同一个判定
/// （裁决 7.52 的 S14），面板接受而这里存不回去的形态就是第二真值源。
[[nodiscard]] auto color_from(const Value &value) -> std::optional<ui::RgbaColor> {
    const auto text = value.as_string();
    if (!text) {
        return std::nullopt;
    }
    return ui::color_from_hex(*text);
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

    /// @brief 读一张有序的文本表（缺字回退链一类）：非文本或空串的元素只丢该元素并留痕其下标。
    ///
    /// 与下面的 `command_overrides` 同一分工：表里坏一项不该让整张表失效——框架对链上解析不到的族
    /// 也只是跳过而不报错，故这里的「丢一项」不是自造宽容而是照抄该领域件的容错方向。空串不算一项
    /// （它在框架侧只会白占一个链位），而**空表是合法值**（= 不注入按族链，走全局默认链）。
    [[nodiscard]] auto string_list(std::string_view key, const std::vector<std::string> &fallback)
        -> std::vector<std::string> {
        known_.emplace_back(key);
        if (node_ == nullptr) {
            return fallback;
        }
        const Value *value = node_->at(key);
        if ((value == nullptr) || !value->is_array()) {
            reject(key);
            return fallback;
        }
        std::vector<std::string> out;
        for (std::size_t index = 0; index < value->size(); ++index) {
            const auto family = text_of(value->at(index));
            if (!family || family->empty()) {
                report_.rejected_keys.push_back(qualified(key) + "[" + std::to_string(index) + "]");
                continue;
            }
            out.emplace_back(*family);
        }
        return out;
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
        basic.push_back(Value(ui::color_to_hex(color)));
    }
    node.set("basic", std::move(basic));
    put(node, "foreground", ui::color_to_hex(spec.default_foreground));
    put(node, "background", ui::color_to_hex(spec.default_background));
    node.set("cursor", spec.cursor_color ? Value(ui::color_to_hex(*spec.cursor_color)) : Value(nullptr));
    node.set("selection", spec.selection_color ? Value(ui::color_to_hex(*spec.selection_color)) : Value(nullptr));
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
    auto chain = Value::array();
    for (const auto &family : appearance.font_fallback_chain) {
        chain.push_back(Value(family));
    }
    // 空表也写成数组而非省掉本键：与 `shortcuts.overrides` 同一条理由（空对象在装载时会被拍平掉），
    // 且「用户清空了链」与「从未配过链」在回退语义上本就同值，不必为区分它们留两个形态。
    node.set("font_fallback_chain", std::move(chain));
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
    // 链上只判「是不是一段非空文本」，不查它是否在系统字体目录里：目录要扫盘才有，而装载接缝
    // 不得做同步 IO（§4.5 第 25 条）。写了没装的族由框架跳过，代价是一格白占链位。
    appearance.font_fallback_chain =
        scope.string_list("font_fallback_chain", defaults.appearance.font_fallback_chain);
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

/// @brief 把一份顶层 JSON 对象读成 `Settings`，并把校验留痕写进报告。
///
/// 装载、回滚与导入三条路都走这里（裁决 7.87③）：快照与导出件本来就是同一个 schema 的文档，
/// 另起一份读侧就是第二真值源，一份能装载的文件在另一条路上装载不出同样的值即分叉。
[[nodiscard]] auto read_settings(const Value &root, LoadReport &report) -> Settings {
    const Settings defaults{};
    Settings settings{};

    for (std::string_view domain : kDomainKeys) {
        const Value *node = root.at(domain);
        const bool is_domain = (node != nullptr) && node->is_object();
        ScopeReader scope(std::string(domain), is_domain ? node : nullptr, report);
        if (!is_domain) {
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

    for (const auto &entry : root.entries()) {
        const std::string_view key = entry.key;
        if ((key == kVersionKey) || (std::ranges::find(kDomainKeys, key) != kDomainKeys.end())) {
            continue;
        }
        report.unknown_keys.emplace_back(key);
    }
    return settings;
}

/// @brief 把存储里的顶层键摊成一份 JSON 对象，交同一条读侧。
///
/// 逐个键搬而不是只搬四域：`report.unknown_keys` 记的就是 schema 之外的顶层键，只搬四域的话
/// 那条留痕结构上抓不到东西。框架的元数据键已由 `keys()` 剥离（`utest_config` 的落盘形态例同断）。
[[nodiscard]] auto root_from_preferences(const au::preferences::Preferences &prefs) -> Value {
    auto root = Value::object();
    for (const auto &key : prefs.keys()) {
        root.set(key, prefs.get<Value>(key, Value{}));
    }
    return root;
}

/// @brief 一份配置 → 落盘/导出共用的顶层文档（版本号 + 四域，裁决 7.87④）。
///
/// 「导出的文件能原样被 `import_settings()` 装回」这条判据的根据就是这一份文档同时是给
/// `Preferences` 的写入内容与导出件的正文；两处各列一遍四域，导出件就会落后于 schema。
[[nodiscard]] auto settings_to_json(const Settings &settings) -> Value {
    auto root = Value::object();
    root.set(kVersionKey, Value(kSupportedSchemaVersion));
    root.set("appearance", appearance_to_json(settings.appearance));
    root.set("terminal", terminal_to_json(settings.terminal));
    root.set("connection", connection_to_json(settings.connection));
    root.set("shortcuts", shortcuts_to_json(settings.shortcuts));
    return root;
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

// ------------------------------------------------------------
// 快照的命名形态与滚动保留（`SPEC.FEAT.PREF.07`，裁决 7.87）
// ------------------------------------------------------------

/// @brief 快照文件名里配置文件名与序号之间的那段（与 `.corrupt-` 同族形态）。
constexpr std::string_view kSnapshotInfix = ".snapshot-";

/// @brief 一份快照的排序键：同一秒内的多次落盘靠序号分先后（epoch 秒是文件名的一部分）。
struct SnapshotStamp {
    std::int64_t epoch{};
    std::int64_t seq{};
};

/// @brief 快照文件名（不含目录）的前缀，用于筛选与校验。
[[nodiscard]] auto snapshot_name_prefix(const fs::path &file) -> std::string {
    return file.filename().string() + std::string{kSnapshotInfix};
}

/// @brief 读一段纯十进制数字；有空闲字符、越界或空串都算不合形态。
[[nodiscard]] auto parse_digits(std::string_view text) -> std::optional<std::int64_t> {
    std::int64_t value{};
    const auto *begin = text.data();
    const auto *end = begin + text.size();
    const auto [next, error] = std::from_chars(begin, end, value);
    if ((error != std::errc{}) || (next != end)) {
        return std::nullopt;
    }
    return value;
}

/// @brief 解析 `.snapshot-` 之后的尾段：`<epoch>` 或 `<epoch>-<seq>`。
[[nodiscard]] auto parse_snapshot_tail(std::string_view tail) -> std::optional<SnapshotStamp> {
    const auto dash = tail.find('-');
    const auto epoch = parse_digits(tail.substr(0, dash));
    if (!epoch) {
        return std::nullopt;
    }
    if (dash == std::string_view::npos) {
        return SnapshotStamp{*epoch, 0};
    }
    const auto seq = parse_digits(tail.substr(dash + 1));
    if (!seq) {
        return std::nullopt;
    }
    return SnapshotStamp{*epoch, *seq};
}

/// @brief 列出本目录内的快照，按**新→旧**排列；不属于本仓命名形态的文件一律不出现。
///
/// 回滚的「只接受自家快照」这条安全判据就落在这里（裁决 7.87⑤）：UI 交回的路径是外部输入，
/// 而目录里既有的文件才是候选集，membership 检查同时钉住了目录、命名与存在性三件事。
[[nodiscard]] auto collect_snapshots(const fs::path &file) -> std::vector<SnapshotInfo> {
    const auto prefix = snapshot_name_prefix(file);
    std::vector<std::pair<SnapshotStamp, SnapshotInfo>> listed;
    std::error_code ec;
    for (const fs::directory_entry &item : fs::directory_iterator{file.parent_path(), ec}) {
        if (!item.is_regular_file(ec)) {
            continue;
        }
        const std::string name = item.path().filename().string();
        if ((name.size() <= prefix.size()) || (name.compare(0, prefix.size(), prefix) != 0)) {
            continue;
        }
        const auto stamp = parse_snapshot_tail(std::string_view{name}.substr(prefix.size()));
        if (!stamp) {
            continue;
        }
        std::error_code size_ec;
        const auto size = item.file_size(size_ec);
        listed.emplace_back(*stamp,
                            SnapshotInfo{item.path(),
                                         stamp->epoch,
                                         size_ec ? 0U : static_cast<std::uint64_t>(size)});
    }
    std::ranges::sort(listed, [](const auto &left, const auto &right) {
        return std::tie(left.first.epoch, left.first.seq) > std::tie(right.first.epoch, right.first.seq);
    });
    std::vector<SnapshotInfo> out;
    out.reserve(listed.size());
    for (const auto &entry : listed) {
        out.emplace_back(entry.second);
    }
    return out;
}

/// @brief 删掉超出保留档数的那几份（尽力而为：删不掉只留痕，不影响本次落盘）。
auto prune_snapshots(const fs::path &file) -> void {
    const auto list = collect_snapshots(file);
    for (std::size_t index = kSnapshotRetention; index < list.size(); ++index) {
        std::error_code ec;
        fs::remove(list[index].path, ec);
        if (ec) {
            AURORA_LOG_WARN("config", "failed to prune a retired config snapshot: ", list[index].path.string());
        }
    }
}

/// @brief 同一秒内下一份快照的序号：取该秒已有尾段序号的最大值 + 1。
///
/// 不能从 0 起找第一个空位——淘汰删掉的恰是最旧那一份，也就是这一秒里序号最低的那格，
/// 复用它等于把刚拍的「最新现场」命名为最旧档，下一次落盘的淘汰会立刻把它删掉。
[[nodiscard]] auto next_snapshot_seq(const fs::path &file, std::int64_t epoch) -> std::int64_t {
    const auto prefix = snapshot_name_prefix(file);
    std::int64_t max_seq{-1};
    std::error_code ec;
    for (const fs::directory_entry &item : fs::directory_iterator{file.parent_path(), ec}) {
        if (!item.is_regular_file(ec)) {
            continue;
        }
        const std::string name = item.path().filename().string();
        if ((name.size() <= prefix.size()) || (name.compare(0, prefix.size(), prefix) != 0)) {
            continue;
        }
        const auto stamp = parse_snapshot_tail(std::string_view{name}.substr(prefix.size()));
        if (stamp && (stamp->epoch == epoch)) {
            max_seq = std::max(max_seq, stamp->seq);
        }
    }
    return max_seq + 1;
}

/// @brief 落盘之前把当前配置文件复制成一份快照（裁决 7.87②）。
///
/// 取在「写之前」而不是「写之后」，于是列表第一项恒是「这次即将被覆盖掉的那一份现场」：
/// 面板的「回滚到上一次」因此落在一个真实存在过的状态上，而回滚本身也不必另设应急备份——
/// 它就是一条普通的写，走同一条复制。
///
/// 同一秒内的第二次落盘会撞名（epoch 只到秒），故撞名即加序号。首次落盘时没有现场可拍，
/// 那不是失败也不该记成一份快照，回空值。
[[nodiscard]] auto snapshot_current(const fs::path &file) -> std::optional<fs::path> {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return std::nullopt;
    }
    const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const auto seq = next_snapshot_seq(file, epoch);
    const auto target =
        fs::path{file.string() + std::string{kSnapshotInfix} + std::to_string(epoch) +
                 (seq == 0 ? std::string{} : "-" + std::to_string(seq))};
    fs::copy_file(file, target, fs::copy_options::none, ec);
    if (ec) {
        AURORA_LOG_WARN("config", "failed to snapshot the current config file: ", file.string());
        return std::nullopt;
    }
    prune_snapshots(file);
    return target;
}

// ------------------------------------------------------------
// 导出件的凭据闸（`SPEC.FEAT.PREF.07` 的「凭据句柄不导出」，裁决 7.26⑥）
// ------------------------------------------------------------

/// @brief 禁列名单：任何层级的键名撞到其中之一就拒绝导出/导入。
///
/// schema 里本来就没有凭据字段（裁决 7.26⑥ 把它挡在 `Settings` 之外），所以这道闸对本仓自己
/// 写出的文件是永不开启的分支——它守的是**外部交回**的那一份：手改过的配置文件、别人机器上
/// 旧版本的导出件。`utest_config` 的落盘形态例已在键名维度证过同一份名单，本件是它的执行点。
constexpr std::array<std::string_view, 5> kBannedKeyNames{
    {"password", "passphrase", "secret", "token", "private_key"}};

/// @brief 递归找一个撞名单的键名，命中即回它的点号路径。
[[nodiscard]] auto find_credential_key(const Value &node, std::string_view path) -> std::optional<std::string> {
    if (node.is_object()) {
        for (const auto &entry : node.entries()) {
            std::string child{entry.key};
            if (!path.empty()) {
                child = std::string{path} + "." + child;
            }
            for (std::string_view banned : kBannedKeyNames) {
                if (entry.key.find(banned) != std::string_view::npos) {
                    return child;
                }
            }
            if (const auto hit = find_credential_key(entry.value, child); hit) {
                return hit;
            }
        }
    } else if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            const Value *item = node.at(index);
            if (item == nullptr) {
                continue;
            }
            const std::string child{std::string{path} + "[" + std::to_string(index) + "]"};
            if (const auto hit = find_credential_key(*item, child); hit) {
                return hit;
            }
        }
    }
    return std::nullopt;
}

/// @brief 把一份 JSON 文本验成「本仓的文档形态」；不合即回原因、不回值。
///
/// 与装载侧的宽容**刻意相反**（裁决 7.87⑥）：自家损坏文件要「逐键回落 + 仍能起来」，
/// 外部交回的错文件只能整体拒绝——把一份手滑的文件读成「一堆默认值」再落盘，就是需求
/// 那句「绝不静默清空」要挡的事。取值域之外的单个值仍走 `read_settings` 的回落，那是
/// 版本之间正常的演进，而顶层结构不合是「这根本不是一份配置」。
/// @param text 文件全文。
/// @param source 只用于诊断文本的来源描述。
/// @param reason 失败时写入 ASCII 英文原因。
/// @return 成功是顶层对象；失败为空。
[[nodiscard]] auto parse_document(std::string_view text, std::string_view source, std::string &reason)
    -> std::optional<Value> {
    const auto fail = [&reason, source](std::string tail) -> std::optional<Value> {
        reason = std::string{source} + " " + tail;
        return std::nullopt;
    };
    const auto parsed = au::json::parse(text);
    if (!parsed.ok()) {
        return fail("is not readable JSON: " + parsed.error().message);
    }
    Value root = parsed.value();
    if (!root.is_object()) {
        return fail("is not a JSON object");
    }
    const Value *version = root.at(kVersionKey);
    if (version == nullptr) {
        return fail("has no schema_version");
    }
    const auto stored = version->as_int();
    if (!stored) {
        return fail("has a non-integer schema_version");
    }
    if (*stored > kSupportedSchemaVersion) {
        return fail("has schema_version " + std::to_string(*stored) + " which is newer than supported " +
                    std::to_string(kSupportedSchemaVersion));
    }
    bool any_domain = false;
    for (std::string_view domain : kDomainKeys) {
        const Value *node = root.at(domain);
        any_domain = any_domain || ((node != nullptr) && node->is_object());
    }
    if (!any_domain) {
        return fail("carries none of the four config domains");
    }
    if (const auto hit = find_credential_key(root, {}); hit) {
        return fail("carries a credential-like key: " + *hit);
    }
    return root;
}

/// @brief 把一份文档全文读进内存（文件不存在与读失败都回原因，不回空文本当成「合法的空配置」）。
[[nodiscard]] auto read_text(const fs::path &file) -> std::optional<std::string> {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// @brief 按框架 `flush()` 的同一条纪律写一份文件：临时文件 + rename（裁决 7.87⑦）。
///
/// 目标由用户点名，框架的锁与临时文件只管自家配置文件，故这一小段不经 `Preferences`；
/// 导出不该留下写一半的文件，那是换机迁移的唯一一份素材。
/// @return 成功为空；失败为 ASCII 英文原因。
[[nodiscard]] auto write_document(const fs::path &file, const Value &root) -> std::optional<std::string> {
    const auto dumped = au::json::dump(root, au::json::DumpOptions{.indent = 2});
    if (!dumped.ok()) {
        return dumped.error().message;
    }
    const auto temp = fs::path{file.string() + ".tmp"};
    std::error_code ec;
    {
        std::ofstream out{temp, std::ios::binary | std::ios::trunc};
        if (!out) {
            return "cannot open the export temporary file: " + temp.string();
        }
        const auto text = dumped.value();
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            return "failed writing the export temporary file: " + temp.string();
        }
    }
    fs::rename(temp, file, ec);
    if (ec) {
        fs::remove(temp, ec);
        return "failed to move the export into place: " + file.string();
    }
    return std::nullopt;
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
        if (version > kSupportedSchemaVersion) {
            report.stored_schema_version = version;
            recover(file,
                    LoadOutcome::RecoveredVersion,
                    "config schema version " + std::to_string(version) + " is newer than supported " +
                        std::to_string(kSupportedSchemaVersion));
            return;
        }
        report.outcome = LoadOutcome::Loaded;
        settings = read_settings(root_from_preferences(*prefs), report);
    }

    /// @brief 提交并落盘。
    auto store(const Settings &next) -> std::optional<std::string> {
        settings = next;
        if (!writes_allowed) {
            return std::string("refused to write the config file: the damaged original was not backed up");
        }
        // 快照取在**写之前**：列表第一项因此恒是「这次即将被覆盖掉的那一份现场」，回滚不必另设应急备份
        // （裁决 7.87②）。拍不成只留痕、不拦下用户的保存——保存是他的意图，快照是保险。
        static_cast<void>(snapshot_current(report.file));
        auto &prefs = *this->prefs;
        prefs.set(std::string(kVersionKey), kSupportedSchemaVersion);
        prefs.set("appearance", appearance_to_json(next.appearance));
        prefs.set("terminal", terminal_to_json(next.terminal));
        prefs.set("connection", connection_to_json(next.connection));
        prefs.set("shortcuts", shortcuts_to_json(next.shortcuts));
        if (const auto flushed = prefs.flush(); !flushed.ok()) {
            return flushed.error().message;
        }
        return std::nullopt;
    }

    /// @brief 把一份文件装成生效配置并落盘：回滚与导入共用同一条腿（裁决 7.87③）。
    ///
    /// 三条路（装载/回滚/导入）共用 `read_settings`，于是一份能装载的文件在另一条路上必然装载出
    /// 同样的值；另起一份读侧就是第二真值源。
    /// @param label 只用于诊断文本的来源描述（ASCII 英文）。
    auto apply_document(const fs::path &file, std::string_view label) -> std::optional<std::string> {
        const auto text = read_text(file);
        if (!text) {
            return std::string{"cannot read "} + std::string{label} + ": " + file.string();
        }
        std::string reason;
        const auto root = parse_document(*text, label, reason);
        if (!root) {
            return reason;
        }
        LoadReport scratch{};
        const auto next = read_settings(*root, scratch);
        if (!scratch.rejected_keys.empty()) {
            AURORA_LOG_WARN("config",
                            "a config document was accepted with keys outside their domain; they fell back to "
                            "defaults: ",
                            file.string());
        }
        return store(next);
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
            report.writes_refused = true;
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

auto Store::snapshots() const -> std::vector<SnapshotInfo> {
    return collect_snapshots(impl_->report.file);
}

auto Store::rollback_to(const std::filesystem::path &snapshot) -> std::optional<std::string> {
    // membership 检查同时钉住目录、命名形态与存在性：这条入口拿到的是 UI 交回的字符串，
    // 让它指向目录外任一文件就是把「回滚」变成「覆盖任意文件」（裁决 7.87⑤）。
    const auto list = collect_snapshots(impl_->report.file);
    if (std::ranges::find(list, snapshot, &SnapshotInfo::path) == list.end()) {
        return "refused to roll back: the path is not one of this directory's config snapshots: " + snapshot.string();
    }
    return impl_->apply_document(snapshot, "the config snapshot");
}

auto Store::export_settings(const std::filesystem::path &file) const -> std::optional<std::string> {
    return write_document(file, settings_to_json(impl_->settings));
}

auto Store::import_settings(const std::filesystem::path &file) -> std::optional<std::string> {
    return impl_->apply_document(file, "the imported config file");
}

}  // namespace borealis::config
