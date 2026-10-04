/// 测试类型: unit
/// 目标单元: include/borealis/ui/settings_form.h + src/ui/settings_form.cpp
/// 测试说明: 设置面板的表单状态机（`codespec/UI_SETTINGS.draft.md` §8 的纯逻辑前置③，裁决 7.52 的
///           S3① 与 S14）。判据分四组：**装载的结构核对**（表内每一键都要有值，表外的键、缺的键、
///           形态族错的键各列一张名单且该键在表单里没有值）；**提交校验**（区间与白名单逐键取自反向
///           核对表交给本件，色值走装载侧同一条式子故八位 alpha 形态必判非法，未配只有两行可缺省）；
///           **脏标记按值而非按提交次数**（改了又改回来既不落盘也不广播）；**生效档位的折叠**
///           （只有「已接线 ∧ 即时」给运行期广播，其余三档只落盘）。
///
///           一条边界如实登记：本件按**落盘点号路径**取放值，把 `config::Settings` 的成员搬进搬出在
///           面板界面腿，故「两个同域成员互换」（`appearance.font_line_height` 与
///           `appearance.font_letter_spacing_dp` 都是实数）在本件结构上抓不到，其判据是该稿 §8 集成
///           那条「改 palette 一格 → 预览盒与主视口的同一格色逐位变化」。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "borealis/ui/palette.h"
#include "borealis/ui/settings_catalog.h"
#include "borealis/ui/settings_form.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_settings_form {

using borealis::ui::ApplyScope;
using borealis::ui::CommitIssue;
using borealis::ui::ControlKind;
using borealis::ui::ConsumerStatus;
using borealis::ui::EffectLevel;
using borealis::ui::FormEntry;
using borealis::ui::FormValue;
using borealis::ui::RgbaColor;
using borealis::ui::SettingsControl;
using borealis::ui::SettingsForm;
using borealis::ui::ShortcutOverride;
using borealis::ui::ValueDomain;

namespace {

constexpr std::string_view kOffList = "__not_a_declared_value__";
constexpr std::string_view kGoodColor = "#12ab34";
constexpr std::string_view kAlphaColor = "#12ab34ff";
constexpr std::size_t kPaletteSlots = 16U;

/// @brief 按该行的域造一个**在域内**的值；`alternate` 给另一个同样合法的值。
///
/// 数值档取闭区间的两端（装载侧是闭区间，反向核对件的探针已逐键验过），文本档取白名单的首尾，
/// 于是「端点被本件判越界」这类错误必然落在某一行上。
[[nodiscard]] auto good_value(const SettingsControl &control, bool alternate) -> FormValue {
    switch (control.domain) {
        case ValueDomain::Boolean:
            return FormValue::boolean(alternate);
        case ValueDomain::Integral:
            return FormValue::integral(alternate ? static_cast<std::int64_t>(control.numeric.max)
                                                 : static_cast<std::int64_t>(control.numeric.min));
        case ValueDomain::Real:
            return FormValue::real(alternate ? control.numeric.max : control.numeric.min);
        case ValueDomain::Choice:
            return FormValue::text(control.choices[alternate ? control.choices.size() - 1U : 0U]);
        case ValueDomain::ColorText:
            return FormValue::color(alternate ? RgbaColor{0x44U, 0x55U, 0x66U} : RgbaColor{0x11U, 0x22U, 0x33U});
        case ValueDomain::ColorTable: {
            std::vector<RgbaColor> table(kPaletteSlots, alternate ? RgbaColor{0x44U, 0x55U, 0x66U}
                                                                   : RgbaColor{0x11U, 0x22U, 0x33U});
            return FormValue::color_table(std::move(table));
        }
        case ValueDomain::FreeText:
            return FormValue::text(alternate ? "second value" : "first value");
        case ValueDomain::FamilyChain: {
            std::vector<std::string> chain;
            if (alternate) {
                chain.emplace_back("Cascadia Mono");
            }
            return FormValue::text_list(std::move(chain));
        }
        case ValueDomain::OverrideMap: {
            std::vector<ShortcutOverride> rows;
            if (alternate) {
                rows.push_back(ShortcutOverride{"workspace.split.right", "Ctrl+Alt+Right"});
            }
            return FormValue::overrides(std::move(rows));
        }
    }
    return FormValue::boolean(false);
}

/// @brief 给表里每一键都造一个域内值（装载的基准形态）。
[[nodiscard]] auto entries_for_whole_catalog() -> std::vector<FormEntry> {
    std::vector<FormEntry> entries;
    for (const auto &control : borealis::ui::settings_catalog()) {
        entries.push_back(FormEntry{control.key, good_value(control, false)});
    }
    return entries;
}

[[nodiscard]] auto make_form() -> SettingsForm {
    return SettingsForm{entries_for_whole_catalog()};
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

/// @brief 表里「控件形态不收文本」的行（证 `TextNotAccepted` 是按控件形态而非按域判的）。
[[nodiscard]] auto non_text_rows() -> std::vector<std::string> {
    std::vector<std::string> keys;
    for (const auto &control : borealis::ui::settings_catalog()) {
        const bool text_kind = control.kind == ControlKind::HexInput || control.kind == ControlKind::OptionalHexInput
                               || control.kind == ControlKind::TextInput || control.kind == ControlKind::NumberStep;
        if (!text_kind) {
            keys.push_back(control.key);
        }
    }
    return keys;
}

}  // namespace

AURORA_TEST_CASE(whole_catalog_loads_without_structure_drift) {
    const auto form = make_form();
    AURORA_TEST_REQUIRE_MSG(form.load_report().clean(), "a clean load reported drift");
    // 表里每一键都必须有值：少一键面板就少画一个控件，而这不是「可以接受的降级」。
    for (const auto &control : borealis::ui::settings_catalog()) {
        AURORA_TEST_CHECK_MSG(form.value(control.key) != nullptr, control.key + " has no loaded value");
    }
}

AURORA_TEST_CASE(structure_drift_is_named_in_three_lists) {
    auto entries = entries_for_whole_catalog();
    // ① 表外的键：只记录，不报错，且该键在表单里没有值。
    entries.push_back(FormEntry{"appearance.not_a_schema_key", FormValue::boolean(true)});
    // ② 缺键：表里有而装载没给。
    const auto omitted = std::string{"terminal.trim_pasted_trailing_space"};
    std::erase_if(entries, [&](const FormEntry &entry) { return entry.key == omitted; });
    // ③ 错型：布尔行收到实数。
    const auto mistyped = std::string{"appearance.palette.bold_is_bright"};
    for (auto &entry : entries) {
        if (entry.key == mistyped) {
            entry.value = FormValue::real(1.0);
        }
    }

    SettingsForm form{std::move(entries)};
    const auto &report = form.load_report();
    AURORA_TEST_CHECK_MSG(holds(report.unknown_keys, "appearance.not_a_schema_key"), "unknown key not named");
    AURORA_TEST_CHECK_MSG(holds(report.missing_keys, omitted), "missing key not named");
    AURORA_TEST_CHECK_MSG(holds(report.mistyped_keys, mistyped), "mistyped key not named");
    AURORA_TEST_CHECK_MSG(!report.clean(), "drift reported as clean");
    AURORA_TEST_CHECK_MSG(!holds(report.missing_keys, mistyped), "mistyped key also named as missing");

    // 缺型与缺键的键都没有值，于是面板少画控件；往它们里提交是 `NotLoaded` 而不是静默成功。
    AURORA_TEST_CHECK(form.value(omitted) == nullptr);
    AURORA_TEST_CHECK(form.value(mistyped) == nullptr);
    AURORA_TEST_CHECK_EQ(form.commit_value(mistyped, FormValue::boolean(true)).issue, CommitIssue::NotLoaded);
    AURORA_TEST_CHECK_EQ(form.commit_value("appearance.not_a_schema_key", FormValue::boolean(true)).issue,
                         CommitIssue::UnknownKey);
    // 其余未点名的键照常有值。
    AURORA_TEST_CHECK(form.value("appearance.theme") != nullptr);
}

AURORA_TEST_CASE(numeric_ranges_are_enforced_per_row) {
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (!control.is_numeric()) {
            continue;
        }
        SettingsForm form{entries_for_whole_catalog()};
        // 域外：拒、值不动、不脏。区间不在本件写死，逐键取自反向核对表。
        const auto below = control.domain == ValueDomain::Integral
                               ? FormValue::integral(static_cast<std::int64_t>(control.numeric.min) - 1)
                               : FormValue::real(control.numeric.min - 0.5);
        const auto above = control.domain == ValueDomain::Integral
                               ? FormValue::integral(static_cast<std::int64_t>(control.numeric.max) + 1)
                               : FormValue::real(control.numeric.max + 0.5);
        AURORA_TEST_CHECK_MSG(!form.commit_value(control.key, below).accepted(), control.key + " below accepted");
        AURORA_TEST_CHECK_EQ(form.commit_value(control.key, above).issue, CommitIssue::OutOfRange);
        AURORA_TEST_CHECK(!form.is_dirty(control.key));
        // 两端点都收（闭区间）。
        AURORA_TEST_CHECK_MSG(form.commit_value(control.key, good_value(control, true)).accepted(),
                              control.key + " upper bound rejected");
        AURORA_TEST_CHECK(form.is_dirty(control.key));
    }
}

AURORA_TEST_CASE(choice_whitelists_are_enforced_per_row) {
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.domain != ValueDomain::Choice) {
            continue;
        }
        SettingsForm form{entries_for_whole_catalog()};
        for (const auto &name : control.choices) {
            AURORA_TEST_CHECK_MSG(form.commit_value(control.key, FormValue::text(name)).accepted(),
                                  control.key + " declared choice rejected");
        }
        // 另起一份表单判「拒了就不脏」：上面逐个候选的提交本来就改了值，混在一起这句失去判据。
        SettingsForm reject_form{entries_for_whole_catalog()};
        const auto rejected = reject_form.commit_value(control.key, FormValue::text(std::string{kOffList}));
        AURORA_TEST_CHECK_EQ(rejected.issue, CommitIssue::NotAChoice);
        AURORA_TEST_CHECK(!reject_form.is_dirty(control.key));
    }
}

AURORA_TEST_CASE(hex_input_shares_the_loaders_form_and_has_no_alpha_bit) {
    for (const auto &control : borealis::ui::settings_catalog()) {
        if (control.domain != ValueDomain::ColorText) {
            continue;
        }
        SettingsForm form{entries_for_whole_catalog()};
        AURORA_TEST_CHECK(form.commit_text(control.key, kGoodColor).accepted());
        const auto stored = form.value(control.key)->as_color();
        AURORA_TEST_REQUIRE(stored.has_value());
        AURORA_TEST_CHECK_EQ(*stored, (RgbaColor{0x12U, 0xabU, 0x34U}));
        // 大小写不敏感（同 `color_from_hex`）；八位 alpha、缺前缀、`rgb()` 式一律判非法（A2-d）。
        AURORA_TEST_CHECK(form.commit_text(control.key, "#AABBCC").accepted());
        AURORA_TEST_CHECK_EQ(form.commit_text(control.key, kAlphaColor).issue, CommitIssue::MalformedColor);
        AURORA_TEST_CHECK_EQ(form.commit_text(control.key, "12ab34").issue, CommitIssue::MalformedColor);
        AURORA_TEST_CHECK_EQ(form.commit_text(control.key, "#12ab3").issue, CommitIssue::MalformedColor);
        AURORA_TEST_CHECK_EQ(form.commit_text(control.key, "rgb(18,171,52)").issue, CommitIssue::MalformedColor);
        // 空串不是「未配」：装载侧把空串当畸形值回落并留痕，未配是显式 `null`（裁决 7.27③）。
        AURORA_TEST_CHECK_EQ(form.commit_text(control.key, "").issue, CommitIssue::MalformedColor);

        // 「未配」另起一份表单判：上面几次已通过的提交本来就脏，混在一起会让「拒了就不脏」这句失去判据。
        SettingsForm unset_form{entries_for_whole_catalog()};
        const auto unset = unset_form.commit_unset_color(control.key);
        if (control.kind == ControlKind::OptionalHexInput) {
            AURORA_TEST_CHECK_MSG(unset.accepted(), control.key + " refuses the unset slot");
            AURORA_TEST_CHECK(unset_form.value(control.key)->is_unset_color());
            AURORA_TEST_CHECK(unset_form.is_dirty(control.key));
        } else {
            AURORA_TEST_CHECK_MSG(unset.issue == CommitIssue::UnsetNotAllowed, control.key + " allows unset");
            AURORA_TEST_CHECK_FALSE(unset_form.is_dirty(control.key));
        }
    }
    // 「未配」与「配成黑色」在值上必须不同（A2-b 的两态）。
    SettingsForm form{entries_for_whole_catalog()};
    const auto cursor = std::string{"appearance.palette.cursor"};
    AURORA_TEST_CHECK(form.commit_value(cursor, FormValue::color(RgbaColor{})).accepted());
    AURORA_TEST_CHECK(form.value(cursor)->as_color().has_value());
    AURORA_TEST_CHECK(form.commit_unset_color(cursor).accepted());
    AURORA_TEST_CHECK(form.value(cursor)->is_unset_color());
    AURORA_TEST_CHECK_FALSE(form.value(cursor)->as_color().has_value());
}

AURORA_TEST_CASE(free_text_is_taken_verbatim_and_suggestions_are_not_a_whitelist) {
    SettingsForm form{entries_for_whole_catalog()};
    // B2-b：断点集原样收字符，引号、反斜杠、竖线都不做转义。
    const auto delimiters = std::string{"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"};
    AURORA_TEST_CHECK(form.commit_text("terminal.word_delimiters", delimiters).accepted());
    form.note_persisted();
    const auto stored = form.value("terminal.word_delimiters")->as_text();
    AURORA_TEST_REQUIRE(stored.has_value());
    AURORA_TEST_CHECK_MSG(*stored == delimiters, "delimiters were escaped");
    // 空串是合法值（只有空格与制表断词），不是「恢复默认」。
    AURORA_TEST_CHECK(form.commit_text("terminal.word_delimiters", "").accepted());
    AURORA_TEST_CHECK(form.is_dirty("terminal.word_delimiters"));
    // 路径类允许含空格。
    AURORA_TEST_CHECK(form.commit_text("connection.startup_directory", "C:\\Program Files\\Borealis").accepted());

    // `terminal.encoding` 的六个名是**建议项**：装载侧只判类型，故本件也不据它们拒。
    AURORA_TEST_CHECK(form.commit_value("terminal.encoding", FormValue::text("GB18030")).accepted());
    AURORA_TEST_CHECK_EQ(form.commit_value("terminal.encoding", FormValue::text(std::string{kOffList})).issue,
                         CommitIssue::None);
    // 但该行的控件是下拉，文本入口不开（否则面板可以绕过候选直接写）。
    AURORA_TEST_CHECK_EQ(form.commit_text("terminal.encoding", "GB18030").issue, CommitIssue::TextNotAccepted);

    for (const auto &key : non_text_rows()) {
        AURORA_TEST_CHECK_MSG(form.commit_text(key, "12").issue == CommitIssue::TextNotAccepted, key + " accepts text");
    }
}

AURORA_TEST_CASE(numeric_text_separates_malformed_from_non_integral) {
    SettingsForm form{entries_for_whole_catalog()};
    const auto integral = std::string{"appearance.cursor_blink_period_ms"};
    AURORA_TEST_CHECK(form.commit_text(integral, "500").accepted());
    AURORA_TEST_CHECK_EQ(form.commit_text(integral, "5.0").issue, CommitIssue::NotIntegral);
    AURORA_TEST_CHECK_EQ(form.commit_text(integral, "abc").issue, CommitIssue::MalformedNumber);
    AURORA_TEST_CHECK_EQ(form.commit_text(integral, "").issue, CommitIssue::MalformedNumber);
    AURORA_TEST_CHECK_EQ(form.commit_text(integral, "12abc").issue, CommitIssue::MalformedNumber);

    const auto real = std::string{"appearance.font_line_height"};
    AURORA_TEST_CHECK(form.commit_text(real, "1.5").accepted());
    AURORA_TEST_CHECK(form.commit_text(real, "2").accepted());  // 整数文本对实数档合法
    AURORA_TEST_CHECK_EQ(form.commit_text(real, "1.5x").issue, CommitIssue::MalformedNumber);
    AURORA_TEST_CHECK_EQ(form.commit_text(real, "4.0").issue, CommitIssue::OutOfRange);
    // 错型不走文本腿：整数档收到实数形态是面板接错行。
    AURORA_TEST_CHECK_EQ(form.commit_value(integral, FormValue::real(500.0)).issue, CommitIssue::DomainMismatch);
}

AURORA_TEST_CASE(color_table_commits_per_slot_and_keeps_every_cell) {
    const auto table = std::string{"appearance.palette.basic"};
    SettingsForm form{entries_for_whole_catalog()};
    const auto before = *form.value(table)->as_color_table();

    const auto outcome = form.commit_color_slot(table, 3U, "#010203");
    AURORA_TEST_CHECK(outcome.accepted());
    AURORA_TEST_CHECK(form.is_dirty(table));
    const auto after = *form.value(table)->as_color_table();
    AURORA_TEST_REQUIRE_EQ(after.size(), before.size());
    AURORA_TEST_CHECK_EQ(after[3], (RgbaColor{0x01U, 0x02U, 0x03U}));
    for (std::size_t slot = 0U; slot < kPaletteSlots; ++slot) {
        if (slot == 3U) {
            continue;
        }
        AURORA_TEST_CHECK(after[slot] == before[slot]);
    }

    AURORA_TEST_CHECK_EQ(form.commit_color_slot(table, kPaletteSlots, "#010203").issue, CommitIssue::SlotOutOfRange);
    AURORA_TEST_CHECK_EQ(form.commit_color_slot(table, 0U, kAlphaColor).issue, CommitIssue::MalformedColor);
    // 校验不过时整张表不动（半套色比没有色更难解释，装载侧同口径）。
    AURORA_TEST_CHECK(*form.value(table)->as_color_table() == after);
    AURORA_TEST_CHECK_EQ(form.commit_color_slot("appearance.palette.cursor", 0U, "#010203").issue,
                         CommitIssue::DomainMismatch);

    std::vector<RgbaColor> short_table(kPaletteSlots - 1U, RgbaColor{0x77U, 0x88U, 0x99U});
    AURORA_TEST_CHECK_EQ(form.commit_value(table, FormValue::color_table(short_table)).issue,
                         CommitIssue::TableSizeWrong);
    AURORA_TEST_CHECK_FALSE(form.commit_value(table, FormValue::color_table(short_table)).accepted());
    std::vector<RgbaColor> full_table(kPaletteSlots, RgbaColor{0x77U, 0x88U, 0x99U});
    AURORA_TEST_CHECK(form.commit_value(table, FormValue::color_table(full_table)).accepted());
    AURORA_TEST_CHECK_EQ(form.value(table)->as_color_table()->size(), kPaletteSlots);
}

AURORA_TEST_CASE(family_chain_keeps_order_and_empty_is_a_value_not_unset) {
    SettingsForm form{entries_for_whole_catalog()};
    const auto chain = std::string{"appearance.font_fallback_chain"};
    const std::vector<std::string> first{"Cascadia Mono", "Noto Sans Mono CJK SC"};
    const std::vector<std::string> second{"Noto Sans Mono CJK SC", "Cascadia Mono"};
    AURORA_TEST_CHECK(form.commit_value(chain, FormValue::text_list(first)).accepted());
    AURORA_TEST_CHECK(*form.value(chain)->as_text_list() == first);
    // 顺序即语义（裁决 7.50），故换序是改动。
    AURORA_TEST_CHECK(form.commit_value(chain, FormValue::text_list(second)).accepted());
    AURORA_TEST_CHECK(*form.value(chain)->as_text_list() == second);
    // 空表是「不注入按族链」，不是「未配」：装载与提交都收。
    AURORA_TEST_CHECK(form.commit_value(chain, FormValue::text_list({})).accepted());
    // 装载的基准值本就是空表（缺键即空，裁决 7.50），故清空等于回到那一份：按值判脏的直接推论。
    AURORA_TEST_CHECK_FALSE(form.is_dirty(chain));
    AURORA_TEST_CHECK_EQ(form.commit_value(chain, FormValue::text("Cascadia Mono")).issue,
                         CommitIssue::DomainMismatch);
}

AURORA_TEST_CASE(read_only_rows_refuse_edits_entirely) {
    SettingsForm form{entries_for_whole_catalog()};
    const auto overrides = std::string{"shortcuts.overrides"};
    const std::vector<ShortcutOverride> rows{ShortcutOverride{"workspace.split.right", "Ctrl+Alt+Right"}};
    // S9 / D3-a：快捷键页首版只读，故本件不给它任何可改动的路径（面板因此画不出会失灵的按钮）。
    AURORA_TEST_CHECK_EQ(form.commit_value(overrides, FormValue::overrides(rows)).issue, CommitIssue::ReadOnly);
    AURORA_TEST_CHECK_EQ(form.commit_text(overrides, "workspace.split.right").issue, CommitIssue::TextNotAccepted);
    AURORA_TEST_CHECK_EQ(form.commit_unset_color(overrides).issue, CommitIssue::DomainMismatch);
    AURORA_TEST_CHECK_FALSE(form.is_dirty(overrides));
    AURORA_TEST_CHECK_FALSE(form.has_unsaved_changes());
}

AURORA_TEST_CASE(dirty_marks_follow_values_not_clicks) {
    SettingsForm form{entries_for_whole_catalog()};
    const auto size = std::string{"appearance.font_size_pt"};
    AURORA_TEST_CHECK_FALSE(form.has_unsaved_changes());
    // 改回来要对着装载的那一份改，而不是对着 schema 默认值改（表单基准由 entries 给定）。
    const auto baseline = *form.value(size)->as_real();

    AURORA_TEST_CHECK(form.commit_value(size, FormValue::real(16.0)).accepted());
    AURORA_TEST_CHECK(form.has_unsaved_changes());
    AURORA_TEST_CHECK(form.commit_value(size, FormValue::real(20.0)).accepted());
    AURORA_TEST_CHECK(form.commit_value(size, FormValue::real(baseline)).accepted());
    AURORA_TEST_CHECK_MSG(!form.has_unsaved_changes(), "a reverted value still counts as unsaved");
    AURORA_TEST_CHECK_TRUE(form.dirty_keys().empty());

    // 落盘标记只在成功写回后推进：`Store::replace()` 拒绝落盘时脏标记必须留着。
    AURORA_TEST_CHECK(form.commit_value(size, FormValue::real(18.0)).accepted());
    AURORA_TEST_CHECK(form.has_unsaved_changes());
    form.note_persisted();
    AURORA_TEST_CHECK_FALSE(form.has_unsaved_changes());
    AURORA_TEST_CHECK_FALSE(form.is_dirty(size));
}

AURORA_TEST_CASE(dirty_keys_follow_the_catalog_layout_order) {
    SettingsForm form{entries_for_whole_catalog()};
    // 先改排版靠后的，再改靠前的，返回次序仍应照表（面板逐行排版时的角标即依此）。
    AURORA_TEST_CHECK(form.commit_value("terminal.copy_on_select", FormValue::boolean(true)).accepted());
    AURORA_TEST_CHECK(form.commit_value("appearance.theme", FormValue::text("nord")).accepted());
    const auto keys = form.dirty_keys();
    const std::vector<std::string> expected{"appearance.theme", "terminal.copy_on_select"};
    AURORA_TEST_CHECK_MSG(keys == expected, "dirty keys out of order: " + join(keys));
}

AURORA_TEST_CASE(only_wired_immediate_rows_broadcast) {
    SettingsForm form{entries_for_whole_catalog()};
    // 四个代表值各挡一条折叠规则；两列具名集合的漂移由 `utest_settings_catalog` 守。
    AURORA_TEST_CHECK_EQ(form.apply_scope("appearance.font_size_pt").value_or(ApplyScope::PersistOnly),
                         ApplyScope::PersistAndApplyNow);
    AURORA_TEST_CHECK_EQ(form.apply_scope("terminal.scrollback_limit").value_or(ApplyScope::PersistAndApplyNow),
                         ApplyScope::PersistOnly);
    AURORA_TEST_CHECK_EQ(form.apply_scope("appearance.cursor_shape").value_or(ApplyScope::PersistAndApplyNow),
                         ApplyScope::PersistOnly);
    AURORA_TEST_CHECK_EQ(form.apply_scope("appearance.status_bar.show_theme").value_or(ApplyScope::PersistAndApplyNow),
                         ApplyScope::PersistOnly);
    AURORA_TEST_CHECK_FALSE(form.apply_scope("appearance.not_a_schema_key").has_value());

    // 提交结果里的档位与表一致：延后的行改了也只落盘。
    const auto deferred = form.commit_value("appearance.sidebar_collapsed", FormValue::boolean(false));
    AURORA_TEST_CHECK(deferred.accepted());
    AURORA_TEST_CHECK_EQ(deferred.scope, ApplyScope::PersistOnly);
    const auto immediate = form.commit_value("appearance.palette.selection", FormValue::color(RgbaColor{1U, 2U, 3U}));
    AURORA_TEST_CHECK(immediate.accepted());
    AURORA_TEST_CHECK_EQ(immediate.scope, ApplyScope::PersistAndApplyNow);

    // 整表核算：广播行的计数与表的两列一致，且每一行都取到档位。
    std::size_t broadcast_rows = 0U;
    for (const auto &control : borealis::ui::settings_catalog()) {
        AURORA_TEST_CHECK_MSG(form.apply_scope(control.key).has_value(), control.key + " has no scope");
        if (control.consumer == ConsumerStatus::Wired && control.effect == EffectLevel::Immediate) {
            ++broadcast_rows;
            AURORA_TEST_CHECK_MSG(borealis::ui::apply_scope(control) == ApplyScope::PersistAndApplyNow,
                                  control.key + " should broadcast");
        } else {
            AURORA_TEST_CHECK_MSG(borealis::ui::apply_scope(control) == ApplyScope::PersistOnly,
                                  control.key + " should not broadcast");
        }
    }
    AURORA_TEST_CHECK(broadcast_rows > 0U);
    AURORA_TEST_CHECK(broadcast_rows < borealis::ui::settings_catalog().size());
}

}  // namespace borealis::test_cases::utest_settings_form
