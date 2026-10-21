/// 测试类型: unit
/// 目标单元: src/ui/settings_i18n.{h,cpp} 的语言档接缝 + src/config/schema_names.h 的 `language_tag`
/// 测试说明: 守住 `SPEC.FEAT.PREF.05` 判据 ① 在**英文档还没有本体**这一天就得绿的那半边，共四条：
///           ① 落盘名与 i18n 区域标签同源（`language_tag` 是那条同源关系的唯一读点，装填侧不得自列
///           第二份名字）；② 装填只装选定那一档——选英文时表里查不到任何中文词条，于是「把缺省档写死
///           回 `zh`、忽略参数」这类退化（该稿 §8 的 m1）在这里转红；③ 缺省档随参数走，于是框架在没有
///           `Provider<Locale>` 的那几处按 `Locale{}`（＝`en`）查表时也落回用户选的那一档（F3／F5）；
///           ④ 进程级 `settings_locale()` 随装填参数移动。查表失败的可见形态（F13：回退 `text` 恒空 ⇒
///           空白而不是漏出 key）另立一案，因为它是面板上「覆盖率证人」成立的前提。
///
///           **本案刻意不立的两句**（`codespec/PREF05_I18N.draft.md` §4.1 第 3 条）：判据 ②「两档同 key
///           集」与判据 ③ 的复数形态在英文档为空时要么即红要么空转，故随批 3 英文档本体落地时再补
///           `the_two_tiers_share_the_same_key_space`；同理，「选英文档时每一 key 都解析不出」今天成立、
///           批 3 之后必然翻转，因此不作为断言写入本案（只在注释里登记那是 F13 的已知代价）。

#include "ui/settings_i18n.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/i18n/locale.h"
#include "aurora/i18n/string_table.h"

#include "borealis/config/settings.h"
#include "borealis/ui/settings_catalog.h"

#include "config/schema_names.h"

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_i18n_tiers {

namespace au = aurora;

namespace {

using borealis::config::InterfaceLanguage;

constexpr std::array<InterfaceLanguage, 2> kBothTiers{InterfaceLanguage::Zh, InterfaceLanguage::En};

/// @brief 按落盘名造 `Locale`（与装填侧同一只读点，不自列字符串）。
[[nodiscard]] auto locale_of(InterfaceLanguage language) -> au::Locale {
    return au::Locale{std::string{borealis::config::language_tag(language)}, {}};
}

/// @brief 在给定表里按给定 `Locale` 数得出多少条给定 key。
[[nodiscard]] auto resolvable(const au::StringTable &table, const au::Locale &locale,
                              const std::vector<std::string> &keys) -> std::size_t {
    std::size_t hits = 0U;
    for (const std::string &key : keys) {
        if (table.lookup(key, locale).has_value()) {
            ++hits;
        }
    }
    return hits;
}

/// @brief 把进程级那张表与 `settings_locale()` 恢复回中文档的守卫。
///
/// 本件是那张表的唯一装填点，而 runner 在**同一进程**里跑完所有用例：本案要把进程档切到英文再切回，
/// 若 `AURORA_TEST_REQUIRE_*` 提前返回就把英文档（今天是空档）留给了后面的用例，别的套件会读成一片
/// 空白而不是读到它们自己的红。守卫放在函数作用域，撤销与析构同拍。
struct ProcessTierGuard {
    ProcessTierGuard() = default;
    ProcessTierGuard(const ProcessTierGuard &) = delete;
    auto operator=(const ProcessTierGuard &) -> ProcessTierGuard & = delete;
    ProcessTierGuard(ProcessTierGuard &&) = delete;
    auto operator=(ProcessTierGuard &&) -> ProcessTierGuard & = delete;
    ~ProcessTierGuard() { borealis::ui::install_settings_strings(); }
};

}  // namespace

AURORA_TEST_CASE(the_on_disk_name_is_the_locale_tag) {
    // ① 的字符串半边：这两枚 tag 逐字等于配置里写的名字，也逐字等于设置页下拉的那两个候选。
    //    「下拉显示什么、落盘写什么、词条表住哪一档」三件事在此钉成一件事——装填侧另列一份名字表时，
    //    中文档下查表与回落都照常命中、看不见分叉，只有这条同源断言打得中（`schema_names.h` 的注）。
    AURORA_TEST_CHECK_EQ(std::string{borealis::config::language_tag(InterfaceLanguage::Zh)}, std::string{"zh"});
    AURORA_TEST_CHECK_EQ(std::string{borealis::config::language_tag(InterfaceLanguage::En)}, std::string{"en"});

    const borealis::ui::SettingsControl *control = borealis::ui::find_settings_control("appearance.language");
    AURORA_TEST_REQUIRE(control != nullptr);
    AURORA_TEST_REQUIRE_EQ(control->choices.size(), kBothTiers.size());
    for (std::size_t index = 0; index < kBothTiers.size(); ++index) {
        const std::string want{borealis::config::language_tag(kBothTiers[index])};
        AURORA_TEST_CHECK_MSG(control->choices[index] == want,
                              "choice[" + std::to_string(index) + "]=" + control->choices[index] + " != " + want);
    }

    // ① 的分组半边 + ③ 的「装出来的确实住在该档那一格」：把某一档装进一张干净的表，再按**落盘名**拼出的
    //    `Locale` 去查，该档源表的每一 key 都要命中。装填侧若把 tag 写成 `"zh-CN"`、把两档对调，或者只改了
    //    表内分组而 `set_default_locale` 用了另一串，这里逐条红。
    //    英文档今天是空表（该稿 §5），故这一轮在 `En` 上走 0 次——本案的第二句因此今天只由中文档供给读数，
    //    批 3 补上英文档本体后同一句自动取得非空半边，无需改动本案。
    for (const InterfaceLanguage language : kBothTiers) {
        au::StringTable table;
        borealis::ui::install_settings_strings(table, language);
        const auto &keys = borealis::ui::settings_string_keys(language);
        const std::string tag{borealis::config::language_tag(language)};
        AURORA_TEST_CHECK_MSG(resolvable(table, locale_of(language), keys) == keys.size(),
                              "installed tier not addressable by its on-disk name: " + tag);
    }
}

AURORA_TEST_CASE(installing_registers_only_the_selected_tier) {
    // ②「只装选定那一档」（该稿 §4.1 第 2 条，与 D4 的「两档都装」相反）：另一档根本没进表，于是查它必空。
    //    这一句就是 m1（「装填时把缺省档写死回 `zh`、忽略参数」）的落点——那种实现下中文档会被装进英文档
    //    那张表，下面每一 key 都查得到，本案整体转红。
    //
    //    登记一条已知代价（该稿 F13／§5.3）：今天的英文档是空表，所以「选英文 ⇒ 整窗上屏文案全空」是
    //    本批的真实形态，不是测试假象；批 3 补上英文本体后翻转为「另一档查不到」。同理，今天**不**对
    //    `lookup(key, Locale{})` 在英文表上断空——那一断言批 3 之后必然红，属为翻转而写的断言。
    au::StringTable english_table;
    borealis::ui::install_settings_strings(english_table, InterfaceLanguage::En);
    for (const std::string &key : borealis::ui::settings_string_keys(InterfaceLanguage::Zh)) {
        AURORA_TEST_CHECK_MSG(!english_table.lookup(key, locale_of(InterfaceLanguage::Zh)).has_value(),
                              "zh entry leaked into the en table: " + key);
    }

    // 反方向同判据：选中文时表里也不该有英文档的东西（今天英文档为空，这一句在批 3 之后才有牙）。
    au::StringTable chinese_table;
    borealis::ui::install_settings_strings(chinese_table, InterfaceLanguage::Zh);
    const std::vector<std::string> &english_keys = borealis::ui::settings_string_keys(InterfaceLanguage::En);
    for (const std::string &key : english_keys) {
        AURORA_TEST_CHECK_MSG(!chinese_table.lookup(key, locale_of(InterfaceLanguage::En)).has_value(),
                              "en entry leaked into the zh table: " + key);
    }

    // ③ 的进程无关半边：装了中文档的表，按框架的**缺省读数**（`Locale{}`，其 language 字段就是 `"en"`）
    //    仍然逐条命中——因为 `set_default_locale` 跟着参数走，两级回落的第二级落回中文档。
    //    这正是「面板上那几个仍硬编码 `Locale{}` 的解析口」（`button.h` 的无障碍名、`rich_text`、
    //    `command_palette` 的占位符）在只装一档时不会变空白的理由（该稿 F5／F19）。
    const std::vector<std::string> &chinese_keys = borealis::ui::settings_string_keys(InterfaceLanguage::Zh);
    AURORA_TEST_CHECK_EQ(resolvable(chinese_table, au::Locale{}, chinese_keys), chinese_keys.size());
}

AURORA_TEST_CASE(the_process_locale_moves_with_the_installed_language) {
    ProcessTierGuard guard;  // 见 `ProcessTierGuard`：进程级那张表是全仓共享的。

    // ④：`settings_locale()` 是面板侧唯一的 `Locale` 副本（`Locale::tag()` 每次现拼串），装配层与用例
    //    驱动台把它交进 `au::LocaleProvider`。写它的只有下面这两跳，故「切档 ⇒ 绘制侧跟着切」在这里有牙。
    borealis::ui::install_settings_strings(InterfaceLanguage::En);
    AURORA_TEST_CHECK_EQ(borealis::ui::settings_locale().tag(), std::string{"en"});

    borealis::ui::install_settings_strings(InterfaceLanguage::Zh);
    AURORA_TEST_CHECK_EQ(borealis::ui::settings_locale().tag(), std::string{"zh"});

    // 中文档下覆盖率判据的接缝自身可用（`itest_settings_panel` 的覆盖率案吃的是 `has_settings_string`）。
    AURORA_TEST_CHECK_TRUE(borealis::ui::has_settings_string("appearance.language"));
}

AURORA_TEST_CASE(an_unregistered_key_resolves_to_empty_not_the_key) {
    // F13：`LocalizedString::tr()` 造出的实例 `text` 恒空，查表失败时 `resolve` 回退的就是那份空串。
    // 于是「面板漏配词条」在界面上的形态是**空白**，而不是把 `appearance.font_size_pt` 这种 key 直接
    // 上屏——这条可见形态是本仓用「每键都有词条」的证人（`itest_settings_panel` 的覆盖率案）代替
    // 「界面不会露出 key」这种断言的理由（该稿 §5 第 1 条）。
    borealis::ui::install_settings_strings();
    AURORA_TEST_CHECK_TRUE(borealis::ui::settings_label("settings.key.definitely.not.registered").empty());
    AURORA_TEST_CHECK_FALSE(borealis::ui::has_settings_string("settings.key.definitely.not.registered"));
    // key 本身绝不出现在结果里：回退路径不注入 key 文本。
    AURORA_TEST_CHECK_MSG(
        borealis::ui::settings_label("settings.key.definitely.not.registered").find("definitely") == std::string::npos,
        "lookup failure leaked the key into the display string");
}

AURORA_TEST_CASE(both_tiers_are_addressed_by_sorted_unique_keys) {
    // `settings_string_keys` 是判据 ② 的读点（读**源表**而不是读装填后的表，见该稿 §5 第 2 条），
    // 故它的两条结构性质要在这一侧自证：字典序（比对与调试都按它）与 key 唯一（重复注册会被
    // `std::map` 静默吞掉，正是 F8 那处死条目曾经的样子）。中文档今天非空，这一案因此不是空转。
    for (const InterfaceLanguage language : kBothTiers) {
        const auto &keys = borealis::ui::settings_string_keys(language);
        AURORA_TEST_CHECK_MSG(std::ranges::adjacent_find(keys, std::ranges::greater{}) == keys.end(),
                              "tier keys are not sorted");
        AURORA_TEST_CHECK_MSG(std::ranges::adjacent_find(keys) == keys.end(), "tier keys hold a duplicate");
    }
    AURORA_TEST_CHECK_GT(borealis::ui::settings_string_keys(InterfaceLanguage::Zh).size(), 300U);
}

}  // namespace borealis::test_cases::utest_i18n_tiers
