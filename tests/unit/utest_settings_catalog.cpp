/// 测试类型: unit
/// 目标单元: include/borealis/ui/settings_catalog.h + src/ui/settings_catalog.cpp
/// 测试说明: 设置面板的 schema 反向核对表（`codespec/UI_SETTINGS.draft.md` §8 的判据① 的可执行形态）。
///           核心判据是**双向**的：`Store::replace()` 真正写出的 JSON 里每个叶子键在表里恰有一行，
///           表里每个键都真在落盘形态里（面板不得画一个存不回去的控件，schema 也不得留一个面板
///           不知道的键）。表声明的取值域再逐键交给装载侧复核——域内值必须无回落留痕、域外值必须
///           留痕，于是「面板抄了一份比装载侧更宽或更窄的区间」这类分叉结构上抓得到。色值行的
///           alpha 位（A2-d）与自由文本行的建议项（`terminal.encoding`）各有一条独立判据。数值行的
///           量纲后缀同受双向判据守住：稿上画了后缀的四行有且只有那四个，余下行必须留空（面板自造一个
///           稿上没有的后缀、或把某个步进器画成裸数字，都在此转红）。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/core/json.h"

#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/settings_catalog.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_settings_catalog {

namespace au = aurora;

using borealis::config::Settings;
using borealis::config::Store;
using borealis::ui::ControlKind;
using borealis::ui::ConsumerStatus;
using borealis::ui::EffectLevel;
using borealis::ui::RgbaColor;
using borealis::ui::SettingsControl;
using borealis::ui::SettingsPage;
using borealis::ui::ValueDomain;

namespace {

/// @brief 探针用的越界取值：任何白名单与枚举名表里都不会有它。
constexpr std::string_view kOffList = "__not_a_declared_value__";
/// @brief 装载侧唯一接受的色值形态（7 字符）与它唯一的越界形态（8 字符，多一个 alpha 位）。
constexpr std::string_view kGoodColor = "#12ab34";
constexpr std::string_view kAlphaColor = "#12ab34ff";

[[nodiscard]] auto make_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name};
}

[[nodiscard]] auto read_file(const std::filesystem::path &path) -> std::string {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// @brief 把值放到点号路径上，沿途缺失的对象一并建出来。
///
/// 探针文件因此不需要为每个键手写一段 JSON 文本：表里新增一键，探针自动覆盖它。
auto put_at(au::json::Value &root, std::string_view dotted, au::json::Value value) -> void {
    au::json::Value *node = &root;
    std::size_t start = 0U;
    while (true) {
        const auto slash = dotted.find('.', start);
        if (slash == std::string_view::npos) {
            node->set(dotted.substr(start), std::move(value));
            return;
        }
        const auto segment = dotted.substr(start, slash - start);
        if (au::json::Value *child = node->at(segment); child != nullptr) {
            node = child;
        } else {
            node->set(segment, au::json::Value::object());
            node = node->at(segment);
        }
        start = slash + 1U;
    }
}

/// @brief 写一份只含探针键的配置文件并装载，返回被回落的键集合。
///
/// 其余键全部缺键故也会被留痕，所以判据只能是「本探针键在不在里面」，而不是留痕表为空。
[[nodiscard]] auto load_rejected(std::string_view name, const std::vector<std::pair<std::string, au::json::Value>> &probes)
    -> std::vector<std::string> {
    const auto file = make_path(name);
    auto root = au::json::Value::object();
    root.set("schema_version", au::json::Value{std::int64_t{1}});
    for (const auto &[key, value] : probes) {
        put_at(root, key, value);
    }
    const auto dumped = au::json::dump(root);
    AURORA_TEST_REQUIRE(dumped.ok());
    {
        std::ofstream out{file, std::ios::binary | std::ios::trunc};
        out.write(dumped.value().data(), static_cast<std::streamsize>(dumped.value().size()));
    }

    const Store store{file};
    return store.report().rejected_keys;
}

[[nodiscard]] auto holds(const std::vector<std::string> &keys, std::string_view key) -> bool {
    return std::ranges::find(keys, std::string{key}) != keys.end();
}

[[nodiscard]] auto join(const std::vector<std::string> &keys) -> std::string {
    std::string text;
    for (const auto &key : keys) {
        if (!text.empty()) {
            text += " | ";
        }
        text += key;
    }
    return text;
}

/// @brief 落盘形态里的全部叶子键（点号路径）。
///
/// 数组**算一个叶子**而不向下展开：`palette.basic` 与 `shortcuts.overrides` 在面板上是一件控件，
/// 其元素不是可独立配置的键。框架自己的元数据与顶层版本键不属于本仓 schema。
auto collect_leaves(const au::json::Value &node, std::string &prefix, std::vector<std::string> &out) -> void {
    for (const auto &entry : node.entries()) {
        const std::string path{prefix + std::string{entry.key}};
        if (entry.value.is_object()) {
            const auto saved = prefix;
            prefix = path + ".";
            collect_leaves(entry.value, prefix, out);
            prefix = saved;
            continue;
        }
        out.push_back(path);
    }
}

[[nodiscard]] auto written_leaves() -> std::vector<std::string> {
    const auto file = make_path("written_shape.json");
    Settings next{};
    next.appearance.theme = "nord";
    next.appearance.palette.cursor_color = RgbaColor{255U, 0U, 128U};
    next.appearance.palette.selection_color = RgbaColor{61U, 117U, 213U};
    next.appearance.font_fallback_chain = {"Noto Sans Mono CJK SC", "Microsoft YaHei Mono"};
    next.terminal.scrollback_limit = 4096U;
    next.shortcuts.overrides = {{"app.close_tab", "Ctrl+Shift+W"}};
    {
        Store writer{file};
        AURORA_TEST_REQUIRE_FALSE(writer.replace(next).has_value());
    }

    const auto parsed = au::json::parse(read_file(file));
    AURORA_TEST_REQUIRE(parsed.ok());
    std::vector<std::string> leaves;
    for (const auto &entry : parsed.value().entries()) {
        const std::string key{entry.key};
        if ((key == "schema_version") || (key == "__aurora_preference_meta__") || !entry.value.is_object()) {
            continue;
        }
        auto prefix = key + ".";
        collect_leaves(entry.value, prefix, leaves);
    }
    return leaves;
}

[[nodiscard]] auto catalog_keys() -> std::vector<std::string> {
    std::vector<std::string> keys;
    for (const auto &control : borealis::ui::settings_catalog()) {
        keys.push_back(control.key);
    }
    return keys;
}

/// @brief 按域首段取页名（表里的页必须与路径首段一致，否则面板把控件铺错页）。
[[nodiscard]] auto page_of(std::string_view key) -> std::optional<SettingsPage> {
    if (key.starts_with("appearance.")) {
        return SettingsPage::Appearance;
    }
    if (key.starts_with("terminal.")) {
        return SettingsPage::Terminal;
    }
    if (key.starts_with("connection.")) {
        return SettingsPage::Connection;
    }
    if (key.starts_with("shortcuts.")) {
        return SettingsPage::Shortcuts;
    }
    return std::nullopt;
}

/// @brief 数值探针的越界偏移量：整数挪一格，实数挪半格（各区间宽度都远大于此）。
[[nodiscard]] auto outside_delta(const SettingsControl &control) -> double {
    return (control.domain == ValueDomain::Integral) ? 1.0 : 0.5;
}

[[nodiscard]] auto as_value(const SettingsControl &control, double number) -> au::json::Value {
    if (control.domain == ValueDomain::Integral) {
        return au::json::Value{static_cast<std::int64_t>(number)};
    }
    return au::json::Value{number};
}

[[nodiscard]] auto color_array(std::string_view text) -> au::json::Value {
    auto array = au::json::Value::array();
    for (std::size_t index = 0; index < 16U; ++index) {
        array.push_back(au::json::Value{text});
    }
    return array;
}

/// @brief 按域给一个「该域内合法」的探针值（色值行与颜色表各给一格 / 十六格）。
[[nodiscard]] auto good_value(const SettingsControl &control, std::string_view text) -> au::json::Value {
    switch (control.domain) {
    case ValueDomain::Boolean:
        return au::json::Value{true};
    case ValueDomain::ColorTable:
        return color_array(kGoodColor);
    default:
        return au::json::Value{text};
    }
}

[[nodiscard]] auto numeric_rows() -> std::vector<const SettingsControl *> {
    std::vector<const SettingsControl *> rows;
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.is_numeric()) {
            rows.push_back(&control);
        }
    }
    return rows;
}

[[nodiscard]] auto rows_with_domain(ValueDomain domain) -> std::vector<const SettingsControl *> {
    std::vector<const SettingsControl *> rows;
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.domain == domain) {
            rows.push_back(&control);
        }
    }
    return rows;
}

[[nodiscard]] auto rows_with_consumer(ConsumerStatus consumer) -> std::vector<std::string> {
    std::vector<std::string> keys;
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.consumer == consumer) {
            keys.push_back(control.key);
        }
    }
    std::ranges::sort(keys);
    return keys;
}

[[nodiscard]] auto next_session_and_wired_keys() -> std::vector<std::string> {
    std::vector<std::string> keys;
    for (const auto &control : borealis::ui::settings_catalog()) {
        if ((control.consumer == ConsumerStatus::Wired) && (control.effect == EffectLevel::NextSession)) {
            keys.push_back(control.key);
        }
    }
    std::ranges::sort(keys);
    return keys;
}

}  // namespace

AURORA_TEST_CASE(table_covers_exactly_the_written_schema_keys) {
    auto leaves = written_leaves();
    auto keys = catalog_keys();
    std::ranges::sort(leaves);
    std::ranges::sort(keys);

    // 双向：落盘里有而表里没有 = 面板漏画控件；表里有而落盘里没有 = 面板画了存不回去的控件。
    AURORA_TEST_CHECK_MSG(leaves == keys, "leaves=[" + join(leaves) + "] table=[" + join(keys) + "]");
    AURORA_TEST_CHECK_EQ(leaves.size(), keys.size());
    AURORA_TEST_CHECK_EQ(keys.size(), 58U);
}

AURORA_TEST_CASE(keys_are_unique_and_rows_are_addressable) {
    const auto &catalog = borealis::ui::settings_catalog();

    auto keys = catalog_keys();
    std::ranges::sort(keys);
    keys.erase(std::ranges::unique(keys).begin(), keys.end());
    AURORA_TEST_CHECK_MSG(keys.size() == catalog.size(), "duplicate key in table");

    for (const auto &control : catalog) {
        const auto page = aurora::testing::require_value(page_of(control.key));
        AURORA_TEST_CHECK_MSG(page == control.page, control.key);
        AURORA_TEST_CHECK_MSG(borealis::ui::find_settings_control(control.key) == &control, control.key);
    }
    AURORA_TEST_CHECK_TRUE(borealis::ui::find_settings_control("appearance.legacy_widget") == nullptr);
    AURORA_TEST_CHECK_TRUE(borealis::ui::find_settings_control("terminal") == nullptr);
}

AURORA_TEST_CASE(numeric_rows_declare_the_ranges_the_loader_enforces) {
    std::vector<std::pair<std::string, au::json::Value>> at_min;
    std::vector<std::pair<std::string, au::json::Value>> at_max;
    std::vector<std::pair<std::string, au::json::Value>> below_min;
    std::vector<std::pair<std::string, au::json::Value>> above_max;
    std::vector<std::string> keys;
    for (const SettingsControl *control : numeric_rows()) {
        const auto delta = outside_delta(*control);
        at_min.emplace_back(control->key, as_value(*control, control->numeric.min));
        at_max.emplace_back(control->key, as_value(*control, control->numeric.max));
        below_min.emplace_back(control->key, as_value(*control, control->numeric.min - delta));
        above_max.emplace_back(control->key, as_value(*control, control->numeric.max + delta));
        keys.push_back(control->key);
    }
    AURORA_TEST_REQUIRE(keys.size() >= 13U);

    // 两端各自整表探针：面板可以照抄区间而不必再问装载侧「到底允许到几」。
    const auto clean_min = load_rejected("numeric_at_lower_edge.json", at_min);
    for (const auto &key : keys) {
        AURORA_TEST_CHECK_MSG(!holds(clean_min, key), key + " lower edge rejected");
    }
    const auto clean_max = load_rejected("numeric_at_upper_edge.json", at_max);
    for (const auto &key : keys) {
        AURORA_TEST_CHECK_MSG(!holds(clean_max, key), key + " upper edge rejected");
    }
    const auto low = load_rejected("numeric_below_lower.json", below_min);
    for (const auto &key : keys) {
        AURORA_TEST_CHECK_MSG(holds(low, key), key + " below lower accepted");
    }
    const auto high = load_rejected("numeric_above_upper.json", above_max);
    for (const auto &key : keys) {
        AURORA_TEST_CHECK_MSG(holds(high, key), key + " above upper accepted");
    }
}

AURORA_TEST_CASE(choice_rows_declare_the_whitelists_the_loader_enforces) {
    const auto rows = rows_with_domain(ValueDomain::Choice);
    AURORA_TEST_REQUIRE(rows.size() >= 8U);

    std::size_t widest = 0U;
    for (const SettingsControl *control : rows) {
        widest = std::max(widest, control->choices.size());
    }
    // 每个档位各下一份文件：白名单里少列或多列一个名字都会在这里露出来。
    for (std::size_t index = 0; index < widest; ++index) {
        std::vector<std::pair<std::string, au::json::Value>> probes;
        std::vector<std::string> keys;
        for (const SettingsControl *control : rows) {
            if (control->choices.size() <= index) {
                continue;
            }
            probes.emplace_back(control->key, au::json::Value{std::string_view{control->choices[index]}});
            keys.push_back(control->key);
        }
        const auto rejected = load_rejected("choice_edge_" + std::to_string(index) + ".json", probes);
        for (const auto &key : keys) {
            AURORA_TEST_CHECK_MSG(!holds(rejected, key), key + " declared choice rejected");
        }
    }

    std::vector<std::pair<std::string, au::json::Value>> off_list;
    for (const SettingsControl *control : rows) {
        off_list.emplace_back(control->key, au::json::Value{std::string_view{kOffList}});
    }
    const auto rejected = load_rejected("choice_off_list.json", off_list);
    for (const SettingsControl *control : rows) {
        AURORA_TEST_CHECK_MSG(holds(rejected, control->key), control->key + " off-list choice accepted");
    }
}

AURORA_TEST_CASE(color_rows_have_no_alpha_channel) {
    std::vector<std::pair<std::string, au::json::Value>> plain;
    std::vector<std::pair<std::string, au::json::Value>> with_alpha;
    std::vector<std::string> keys;
    for (const auto &control : borealis::ui::settings_catalog()) {
        if ((control.domain != ValueDomain::ColorText) && (control.domain != ValueDomain::ColorTable)) {
            continue;
        }
        plain.emplace_back(control.key, good_value(control, kGoodColor));
        with_alpha.emplace_back(control.key,
                                (control.domain == ValueDomain::ColorTable) ? color_array(kAlphaColor)
                                                                            : au::json::Value{kAlphaColor});
        keys.push_back(control.key);
    }
    // 色值行恰是「四个 HEX 输入 + 一个 16 色板」：schema 里没有第五个单色槽，多一行就是面板画了存不回去的键。
    AURORA_TEST_REQUIRE_EQ(keys.size(), 5U);

    const auto clean = load_rejected("color_plain.json", plain);
    for (const auto &key : keys) {
        AURORA_TEST_CHECK_MSG(!holds(clean, key), key + " seven-char color rejected");
    }
    // A2-d 的可执行形态：八位形态存不进去，故面板给一个 alpha 位就是让用户改了一个落不了盘的值。
    const auto alpha = load_rejected("color_with_alpha.json", with_alpha);
    for (const auto &key : keys) {
        AURORA_TEST_CHECK_MSG(holds(alpha, key), key + " eight-char color accepted");
    }
}

AURORA_TEST_CASE(free_text_rows_are_not_domain_checked_by_the_loader) {
    std::vector<std::pair<std::string, au::json::Value>> probes;
    std::vector<std::string> keys;
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.domain != ValueDomain::FreeText) {
            continue;
        }
        probes.emplace_back(control.key, au::json::Value{std::string_view{kOffList}});
        keys.push_back(control.key);
    }
    AURORA_TEST_REQUIRE(keys.size() >= 8U);

    // 建议项不是白名单：`terminal.encoding` 表里列了六个名字，装载侧却一个都不拦。
    // 面板若据建议项硬校验，将来接 GB18030 一类的取值就存不进去。
    const auto rejected = load_rejected("free_text_off_list.json", probes);
    for (const auto &key : keys) {
        AURORA_TEST_CHECK_MSG(!holds(rejected, key), key + " free text rejected");
    }
}

AURORA_TEST_CASE(row_fields_are_mutually_consistent) {
    std::size_t swatches = 0U;
    std::size_t chains = 0U;
    std::size_t tables = 0U;
    for (const auto &control : borealis::ui::settings_catalog()) {
        switch (control.domain) {
        case ValueDomain::Boolean:
            AURORA_TEST_CHECK_MSG(control.kind == ControlKind::Toggle, control.key);
            break;
        case ValueDomain::Integral:
        case ValueDomain::Real:
            AURORA_TEST_CHECK_MSG(control.kind == ControlKind::NumberStep, control.key);
            AURORA_TEST_CHECK_MSG(control.numeric.min < control.numeric.max, control.key);
            break;
        case ValueDomain::Choice:
            AURORA_TEST_CHECK_MSG(control.kind == ControlKind::Dropdown, control.key);
            AURORA_TEST_CHECK_MSG(control.choices.size() >= 2U, control.key);
            break;
        case ValueDomain::ColorText:
            AURORA_TEST_CHECK_MSG((control.kind == ControlKind::HexInput) ||
                                      (control.kind == ControlKind::OptionalHexInput),
                                  control.key);
            break;
        case ValueDomain::ColorTable:
            AURORA_TEST_CHECK_MSG(control.kind == ControlKind::SwatchGrid, control.key);
            ++swatches;
            break;
        case ValueDomain::FreeText:
            AURORA_TEST_CHECK_MSG((control.kind == ControlKind::TextInput) || (control.kind == ControlKind::Dropdown) ||
                                      (control.kind == ControlKind::ThemePicker) ||
                                      (control.kind == ControlKind::FontDropdown),
                                  control.key);
            break;
        case ValueDomain::FamilyChain:
            AURORA_TEST_CHECK_MSG(control.kind == ControlKind::FamilyList, control.key);
            ++chains;
            break;
        case ValueDomain::OverrideMap:
            AURORA_TEST_CHECK_MSG(control.kind == ControlKind::ReadOnlyTable, control.key);
            ++tables;
            break;
        }
        // 白名单只属 `Choice`，数值区间只属两档数值域；余下的行不得夹带（夹带就是第二个真值源）。
        if ((control.domain != ValueDomain::Choice) && (control.domain != ValueDomain::FreeText)) {
            AURORA_TEST_CHECK_MSG(control.choices.empty(), control.key);
        }
        if (!control.is_numeric()) {
            AURORA_TEST_CHECK_MSG((control.numeric.min == 0.0) && (control.numeric.max == 0.0), control.key);
        }
    }
    AURORA_TEST_CHECK_EQ(swatches, 1U);
    AURORA_TEST_CHECK_EQ(chains, 1U);
    AURORA_TEST_CHECK_EQ(tables, 1U);
    AURORA_TEST_CHECK_TRUE(borealis::ui::find_settings_control("appearance.palette.cursor")->kind ==
                           ControlKind::OptionalHexInput);
    AURORA_TEST_CHECK_TRUE(borealis::ui::find_settings_control("appearance.palette.selection")->kind ==
                           ControlKind::OptionalHexInput);
}

AURORA_TEST_CASE(units_are_declared_only_where_the_sketch_draws_them) {
    // CJK-LITERAL: cjk-fixture - 判据 A3-a 断的就是行高那个乘号字形，换成字母 x 被测事实即消失
    const std::vector<std::pair<std::string, std::string>> expected{{"appearance.font_size_pt", "pt"},
                                                                    {"appearance.font_line_height", "×"},
                                                                    {"appearance.font_letter_spacing_dp", "dp"},
                                                                    {"appearance.cursor_blink_period_ms", "ms"}};
    std::vector<std::string> declared;
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (!control.unit.empty()) {
            AURORA_TEST_CHECK_MSG(control.is_numeric(), control.key + " has a unit but is not numeric");
            const auto found =
                std::ranges::find(expected, control.key, [](const auto &item) { return item.first; });
            AURORA_TEST_REQUIRE(found != expected.end());
            declared.push_back(control.key);
            AURORA_TEST_CHECK_MSG(found->second == control.unit, control.key + " unit suffix drifted");
        }
    }
    std::ranges::sort(declared);
    std::vector<std::string> want;
    for (const auto &[key, suffix] : expected) {
        want.push_back(key);
    }
    std::ranges::sort(want);
    // 双向：少一行即面板把某个步进器画成裸数字，多一行即面板自造了稿上没有的后缀。
    AURORA_TEST_CHECK_MSG(declared == want, "declared units=[" + join(declared) + "] sketch=[" + join(want) + "]");
}

AURORA_TEST_CASE(grade_columns_match_the_two_physical_boundaries) {
    // 判据文 §7 点名的三条「接缝待开」已随裁决 7.76② 的构造期注入接缝接线，故该档的**当前行集为空**。
    // 断空而不是删掉这一列判据：档位保留着（`settings_catalog.h` 的 `SeamPending` 注给了理由），下一棒给
    // 一条无运行期接缝的键挂错档就会在这里转红，而不是静默变成「改了没反应」。
    AURORA_TEST_CHECK_MSG(rows_with_consumer(ConsumerStatus::SeamPending).empty(), "seam-pending set drifted");

    // 判据文 §0 的物理边界②：已接线却「下次会话生效」的键恰是这六条——三条会话侧的构造期注入
    // （裁决 7.76②）加三条本就取用于建会话那一刻的键。多一条即面板谎报即时，少一条即运行期入口被
    // 判成不存在而白开一条接缝。
    const std::vector<std::string> next_session{"appearance.cursor_blinking",
                                                "appearance.cursor_shape",
                                                "connection.local_shell",
                                                "connection.startup_directory",
                                                "terminal.ambiguous_width",
                                                "terminal.scrollback_limit"};
    AURORA_TEST_CHECK_MSG(next_session_and_wired_keys() == next_session, "wired next-session set drifted");

    // 不得出现「接缝待开但即时」这种矛盾档（该档现在行集为空，故这一条守的是将来挂错档的行）。
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.consumer == ConsumerStatus::SeamPending) {
            AURORA_TEST_CHECK_MSG(control.effect == EffectLevel::NextSession, control.key);
        }
    }
    AURORA_TEST_CHECK_EQ(rows_with_consumer(ConsumerStatus::Absent).size(), 28U);
}

}  // namespace borealis::test_cases::utest_settings_catalog
