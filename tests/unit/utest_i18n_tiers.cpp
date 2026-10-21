/// 测试类型: unit
/// 目标单元: src/ui/settings_i18n.{h,cpp} 的语言档接缝（`settings_locale`／两枚装填函数／
///           `settings_string_keys`／`settings_string_text`）+ src/config/schema_names.h 的 `language_tag`
/// 测试说明: 守住 `SPEC.FEAT.PREF.05` 判据 ① 与 ② 的纯逻辑半边，共六条：
///           ① 落盘名与 i18n 区域标签同源（`language_tag` 是那条同源关系的唯一读点，装填侧不得自列
///           第二份名字）；② 装填只装选定那一档——按另一档的 tag 去查也只能拿回选定那一档的模板（或两级
///           全 miss），于是「把缺省档写死回 `zh`、忽略参数」这类退化（该稿 §8 的 m1）在这里转红，而这一
///           形态在英文档补上本体之后依然成立；③ 缺省档随参数走，于是框架在没有
///           `Provider<Locale>` 的那几处按 `Locale{}`（＝`en`）查表时也落回用户选的那一档（F3／F5）；
///           ④ 进程级 `settings_locale()` 随装填参数移动；⑤ 查表失败的可见形态（F13：回退 `text` 恒空 ⇒
///           空白而不是漏出 key）另立一案，因为它是面板上「覆盖率证人」成立的前提；⑥ 判据 ② 的单向半边
///           （英文档的每一 key 都在中文档有非空孪生条目），随批 3 四段一路有效，四段落齐后补反向等式。
///
///           **本案刻意不立的两句**（`codespec/PREF05_I18N.draft.md` §4.1 第 3 条）：判据 ② 的**等式**
///           「两档 key 集逐字相等」与判据 ③ 的复数形态在英文档还没落齐的四段中途要么即红要么空转，
///           故随段 4 一起补；同理，「选英文档时每一 key 都解析不出」那种形态今天成立、批 3 补上本体后
///           必然翻转，因此一律不写成断言——②⑥ 两案吃的是「等式与子集」，它们不依赖英文档的厚度。
///           英文档还没译到的 key 上屏为空白，是 F13 登记过的已知代价，不是本案的读数。

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

/// @brief 把「这张表只可能由 `installed` 那一档供数」按单键走一遍。
///
/// 等式右边读**源表**（`settings_string_text`），左边读**装填后的表**，于是这条断言与「键（key）跨
/// 命名空间冲突」无关，只说一件事：不管调用方递进来的是 `probe` 档的 tag，表里的东西要么就是
/// `installed` 档自己的模板，要么根本没有（两级全 miss）。这一形态在英文档从空表长成有内容的四段
/// 词条（`SPEC.FEAT.PREF.05` 批 3）的过程中**不翻转**，故可以作为常驻证人（该稿 §4.1 第 3 条要求的
/// 正是「不要为翻转而写断言」）。
/// @param table 装填后的表。
/// @param installed 装进表里的那一档。
/// @param probe 查询时用的那一档（与 `installed` 相反的那一档才测得出回落方向）。
/// @param key 词条 key。
auto check_serves_only_the_installed_tier(const au::StringTable &table, const InterfaceLanguage installed,
                                          const InterfaceLanguage probe, const std::string &key) -> void {
    const std::string want = borealis::ui::settings_string_text(installed, key);
    const auto hit = table.lookup(key, locale_of(probe));
    AURORA_TEST_CHECK_MSG(hit.has_value() == !want.empty() && hit.value_or(std::string{}) == want,
                          std::string{"the "} + std::string{borealis::config::language_tag(installed)}
                              + " table answered a " + std::string{borealis::config::language_tag(probe)}
                              + " lookup of " + key + " with "
                              + (hit.has_value() ? *hit : std::string{"<none>"}) + ", source tier holds "
                              + (want.empty() ? std::string{"<none>"} : want));
}

/// @brief 两档源表 key 的并集（今天＝中文档那 466 条；两档齐后仍是对称的一份）。
[[nodiscard]] auto all_source_keys() -> std::vector<std::string> {
    std::vector<std::string> out = borealis::ui::settings_string_keys(InterfaceLanguage::Zh);
    const auto &english = borealis::ui::settings_string_keys(InterfaceLanguage::En);
    out.insert(out.end(), english.begin(), english.end());
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
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
    // ②「只装选定那一档」（该稿 §4.1 第 2 条，与 D4 的「两档都装」相反）：另一档根本没进表。断言写成
    //    「表答出来的东西只能是选定那一档自己的模板」（见 `check_serves_only_the_installed_tier`），
    //    而不是「查另一档查不到」——后者今天成立、批 3 补上英文本体后必然翻转，属该稿 §4.1 第 3 条明令
    //    不写的那一类断言。
    //
    //    这一句就是 m1（「装填时把缺省档写死回 `zh`、忽略参数」）的落点：那种实现把中文模板装进了本该
    //    是英文档的那张表，第一条腿逐条拿到中文串而源表读数是英文串（今天是 `<none>`），本案整体转红。
    //    第二条腿反向同判据，且它今天就有 466 条读数——中文档装着、按英文 tag 去查也只能拿回中文模板，
    //    这正是「回落方向跟着参数」在纯逻辑侧的形态（③ 那条腿是它在 `Locale{}` 上的同一件事）。
    //
    //    登记一条已知代价（该稿 F13／§5 第 3 条）：英文档还是空表的今天，「选英文 ⇒ 上屏文案全空」是真实
    //    形态而不是测试假象；批 3 逐段收敛之后剩下的仍是**没译到的那些 key** 的空白，直到四段落齐。
    const std::vector<std::string> all_keys = all_source_keys();

    au::StringTable english_table;
    borealis::ui::install_settings_strings(english_table, InterfaceLanguage::En);
    for (const std::string &key : all_keys) {
        check_serves_only_the_installed_tier(english_table, InterfaceLanguage::En, InterfaceLanguage::Zh, key);
    }

    au::StringTable chinese_table;
    borealis::ui::install_settings_strings(chinese_table, InterfaceLanguage::Zh);
    for (const std::string &key : all_keys) {
        check_serves_only_the_installed_tier(chinese_table, InterfaceLanguage::Zh, InterfaceLanguage::En, key);
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

AURORA_TEST_CASE(every_english_entry_has_a_non_empty_chinese_twin) {
    // 判据 ② 的**单向半边**，形态是子集而不是等式（该稿 §4.1 第 3 条：等式那半边要等四段英文本体落齐，
    // 中途立它必红）。这一段今天有 146 条读数（`connections.*` 与 `keys.*`），故本案不是空转，且它管的
    // 正是英文档在生长过程中最容易犯的两种错：
    //   ① 写了一枚中文档没有的 key（拼错的 key 在界面上是**空白**而不是错字，F13，所以只能这样抓）；
    //   ② 用空串占位「回头再译」——空串让判据 ② 的等式在四段之后仍然成立，却把 F13 的空白留在了界面上。
    // 反向等式（中文档的每一 key 也在英文档里）随段 4 落齐时补，届时的形态见该稿 §8 的结项读数。
    const std::vector<std::string> &english_keys = borealis::ui::settings_string_keys(InterfaceLanguage::En);
    AURORA_TEST_CHECK_GT(english_keys.size(), 0U);
    for (const std::string &key : english_keys) {
        AURORA_TEST_CHECK_MSG(!borealis::ui::settings_string_text(InterfaceLanguage::En, key).empty(),
                              "the en tier registers an empty template: " + key);
        AURORA_TEST_CHECK_MSG(!borealis::ui::settings_string_text(InterfaceLanguage::Zh, key).empty(),
                              "the en tier has a key the zh tier never heard of: " + key);
    }
}

}  // namespace borealis::test_cases::utest_i18n_tiers
