/// 测试类型: unit
/// 目标单元: src/ui/settings_i18n.{h,cpp} 的语言档接缝（`settings_locale`／两枚装填函数／
///           `settings_string_keys`／`settings_string_text`）+ src/config/schema_names.h 的 `language_tag`
/// 测试说明: 守住 `SPEC.FEAT.PREF.05` 判据 ①②③ 的纯逻辑半边，共九条：
///           ① 落盘名与 i18n 区域标签同源（`language_tag` 是那条同源关系的唯一读点，装填侧不得自列
///           第二份名字）；② 装填只装选定那一档——按另一档的 tag 去查也只能拿回选定那一档的模板（或两级
///           全 miss），于是「把缺省档写死回 `zh`、忽略参数」这类退化（该稿 §8 的 m1）在这里转红，而这一
///           形态在英文档补上本体之后依然成立；②' 同一件事的**独立读点**：五对期望串抄在测试里，
///           与被测的 `source_tier()` 无关，因此 m1 的两种形态分别在 ②'/④ 与 ④ 上有牙（② 那条等式案
///           两侧同读源表，对「连源表一起写死」自反成立——批 3 段 2 实测，故补本案）；③ 缺省档随参数走，
///           于是框架在没有 `Provider<Locale>` 的那几处按 `Locale{}`（＝`en`）查表时也落回用户选的那一档
///           （F3／F5）；④ 进程级 `settings_locale()` 随装填参数移动；⑤ 查表失败的可见形态（F13：回退
///           `text` 恒空 ⇒ 空白而不是漏出 key）另立一案，因为它是面板上「覆盖率证人」成立的前提；
///           ⑥ 判据 ② 的单向半边（英文档的每一 key 都在中文档有非空孪生条目），随批 3 四段一路有效；
///           ⑦ 判据 ② 的等式半边（两档 key 集逐字相等，且每一 key 在两档都有非空模板），随批 3 段 4
///           英文本体落齐立项，是 7.25⑬「新增界面串必须先加两档」那条纪律唯一的结构式证人；
///           ⑧ 判据 ③ 的复数形态（成块的计数词条在英文档 1／2 两值必须两串、在中文档剥掉计数后必须同串；
///           批 4 立 5 枚，批 6 从表外搬进 `terminal.paste_multiline.body` 后为 6 枚），随批 4 落地，
///           外加两组抄在测试里的英文金样串。
///
///           **本案刻意不立的一句**（`codespec/PREF05_I18N.draft.md` §4.1 第 3 条）：「选英文档时每一 key 都
///           解析不出」那种形态在段 1 成立、批 3 补上本体后必然翻转，因此不写成断言——②⑥⑦ 三案吃的是
///           「等式与子集」，它们不随英文档的厚度翻转。判据 ③ 那一句过去也登记在这里，批 4 计数短语落齐后
///           它已撤销并长成 ⑧。
///           段 4 之前英文档没译到的 key 上屏为空白，是 F13 登记过的已知代价；段 4 落齐后它由 ⑦ 的
///           「非空模板」那半边接手，不再是本案的读数。
///           ②' 抄在测试里的那五对是金样：改那一档的措辞就得同步改这里，这是把 m1 变成可抓事实的价格。

#include "ui/settings_i18n.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/i18n/locale.h"
#include "aurora/i18n/localized_string.h"
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

/// @brief 判据 ③ 的样本：一枚带计数的词条 key、计数在哪个参数位、总共有几个参数位。
///
/// 其余参数位一律填 `kCountFiller`（不含数字）——本案只看「数进没进串、单复数变没变」，
/// 让填充串带数字就会把 `find("1")` 那两句变成空转。参数位数取自实测调用点（批 4 读数），
/// 位数错了 `resolve` 就把那一块原样留下，下面的非空/含数断言当场红。
struct CountPhrase {
    std::string_view key;
    std::size_t count_slot;
    std::size_t slots;
};

constexpr std::string_view kCountFiller{"A"};

/// @brief 按判据 ③ 的口径渲染一枚计数词条：走真解析链路（`StringTable::resolve`），不是读模板串。
///
/// 复数块只有在 `resolve` 里才展开（`lookup` 拿回的是模板本身），所以判据 ③ 必须经这条链路——
/// 也正是这一点让本案能抓到「块写坏」（未闭合的花括号会让 `find_closing_brace` 找不到配平，
/// 那一块原样上屏）与「`other=` 分支漏了占位符」两种形态。
[[nodiscard]] auto render_count(const au::StringTable &table, const InterfaceLanguage language,
                                const CountPhrase &phrase, const int count) -> std::string {
    std::vector<au::LocalizedString> args;
    args.reserve(phrase.slots);
    for (std::size_t slot = 0; slot < phrase.slots; ++slot) {
        args.push_back(slot == phrase.count_slot
                           ? au::LocalizedString{std::to_string(count)}
                           : au::LocalizedString{std::string{kCountFiller}});
    }
    return table.resolve(au::LocalizedString::tr(std::string{phrase.key}, std::move(args)), locale_of(language));
}

/// @brief 把渲染串里的第一枚 `1` 改成 `2`。
///
/// 「随数变格」这件事不能直接断两串相等或不等：计数本身就在串里，n=1 与 n=2 的两串天然不同，
/// 于是「不等」骗得过退化、「相等」又永远骗不过去。剥掉数之后比才是两侧各自的读数——中文侧要
/// 剥完**相等**（`plural.h` 给 `zh` 恒 `other`，只有数在动），英文侧要剥完**不等**（名词跟着变格）。
/// 填充位不含数字，故第一枚 `1` 必是那个计数。
[[nodiscard]] auto renumbered_to_two(const std::string &rendered) -> std::string {
    std::string out{rendered};
    if (const auto at = out.find('1'); at != std::string::npos) {
        out.replace(at, 1U, "2");
    }
    return out;
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

/// @brief 两档源表 key 的并集（今天＝中文档那 470 条；两档齐后仍是对称的一份）。
[[nodiscard]] auto all_source_keys() -> std::vector<std::string> {
    std::vector<std::string> out = borealis::ui::settings_string_keys(InterfaceLanguage::Zh);
    const auto &english = borealis::ui::settings_string_keys(InterfaceLanguage::En);
    out.insert(out.end(), english.begin(), english.end());
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

/// @brief 两档有序 key 表的首个差异，只用于等式案转红时的定位。
///
/// 四百余枚 key 报一句「不相等」没法定位：拼错的 key 和漏掉的 key 都缩在同一枚布尔里，两档各 470 条时
/// 差一枚的表要把整表打印出来才找得着。这里按字典序走到第一处分叉就停，把两侧那枚原样吐出来。
[[nodiscard]] auto first_difference(const std::vector<std::string> &left, const std::vector<std::string> &right)
    -> std::string {
    for (std::size_t i = 0U; i < left.size() || i < right.size(); ++i) {
        if (i >= left.size()) {
            return "index " + std::to_string(i) + " only in the right tier: " + right[i];
        }
        if (i >= right.size()) {
            return "index " + std::to_string(i) + " only in the left tier: " + left[i];
        }
        if (left[i] != right[i]) {
            return "index " + std::to_string(i) + " is " + left[i] + " on the left, " + right[i]
                   + " on the right";
        }
    }
    return std::string{"<none>"};
}

/// @brief 把进程级那张表与 `settings_locale()` 恢复回中文档的守卫。
///
/// 本件是那张表的唯一装填点，而 runner 在**同一进程**里跑完所有用例：本案要把进程档切到英文再切回，
/// 若 `AURORA_TEST_REQUIRE_*` 提前返回就把英文档留给了后面的用例，别的套件会读到另一种语言的模板而不是
/// 读到它们自己的红。守卫放在函数作用域，撤销与析构同拍。
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
    //    **本案不是 m1 的落点**（批 3 段 2 实测）：m1 的两种形态（连源表一起写死／只把 tag 与缺省档写死）
    //    在这里都是 0 条读数红——因为等式右边经由 `settings_string_text()` 读的就是同一个 `source_tier()`，
    //    而被回落规则喂平的另一半同理成立。m1 由下面那一案的金样读点（②'）与进程 locale 案（④）分抓。
    //    本案真正咬住的是「装填腿把空模板当成一条装进去」这类形态（p2 实测 1 条读数红）与 key 集本身
    //    错位（m3），两条腿的回落方向也在 ③ 那一案上与 `Locale{}` 的形态对齐。
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

AURORA_TEST_CASE(installed_tiers_answer_templates_pinned_in_the_test) {
    // 判据 ① 装填腿的**第二份**证人，与上面那一案互补而非重复：等式案两侧都经由 `settings_string_text()`
    // 读源表，而 m1 那条变异恰好写死的是同一个 `source_tier()`，于是「表答回的＝源表读数」自反成立、
    // 等式案对 m1 全形态失明（批 3 段 2 实测：m1 全形态下等式案 0 条读数红）。本案把期望串**抄在测试里**，
    // 读点与被测函数无关，因此 m1 的两种形态（只写死缺省档 / 连源表一起写死）都在这里转红。
    //
    // 代价照实登记：这五对是**金样**，改了那一档的措辞就得同步改这里——这是将 m1 变成可抓事实的价格。
    // 选 key 的口径：两档都有本体、且两档文字不同（同字的那些条目，如 `settings.chain.up` 那三枚字形，
    // 在这里没有分辨力）；`keys.dir.unreachable` 那一枚顺带证明位置参数原样进表。批 4 起它替掉原来的
    // `keys.header.count`——后者现在住进复数块，抄在测试里的那份就成了语法而不是文案，金样案要守的是文案。
    struct Pin {
        std::string_view key;
        std::string_view english;
    };
    constexpr std::array<Pin, 5> kPins{
        Pin{"settings.title", "Settings"},
        Pin{"settings.badge.next_launch", "Takes effect after restart"},
        Pin{"settings.chain.filter", "Type a family name to add it"},
        Pin{"connections.action.import", "Import ~/.ssh/config"},
        Pin{"keys.dir.unreachable", "Directory unreachable: {0}"},
    };

    au::StringTable english_table;
    borealis::ui::install_settings_strings(english_table, InterfaceLanguage::En);
    au::StringTable chinese_table;
    borealis::ui::install_settings_strings(chinese_table, InterfaceLanguage::Zh);

    for (const Pin &pin : kPins) {
        const auto en_hit = english_table.lookup(std::string{pin.key}, locale_of(InterfaceLanguage::En));
        AURORA_TEST_CHECK_MSG(en_hit.has_value() && en_hit.value() == pin.english,
                              std::string{"installing the en tier did not serve the pinned template for "}
                                  + std::string{pin.key} + ": got "
                                  + (en_hit.has_value() ? *en_hit : std::string{"<none>"}));

        // 反方向只需「不是那份英文金样」：装了中文档的表若答出英文模板，说明源表那半被写死反了。
        const auto zh_hit = chinese_table.lookup(std::string{pin.key}, locale_of(InterfaceLanguage::Zh));
        AURORA_TEST_CHECK_MSG(zh_hit.has_value() && zh_hit.value() != pin.english,
                              std::string{"installing the zh tier served the pinned english template for "}
                                  + std::string{pin.key} + ": got "
                                  + (zh_hit.has_value() ? *zh_hit : std::string{"<none>"}));
    }
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
    // 中途立它必红）。它管的正是英文档在生长过程中最容易犯的两种错：
    //   ① 写了一枚中文档没有的 key（拼错的 key 在界面上是**空白**而不是错字，F13，所以只能这样抓）；
    //   ② 用空串占位「回头再译」——空串让判据 ② 的等式在四段之后仍然成立，却把 F13 的空白留在了界面上。
    // 段 4 落齐后本案的读数已被下面那一案**完全覆盖**（等式 ⇒ 子集，且非空断言两侧都查），故它自此不是
    // 必要证人；按该稿 §4.1 第 5 条登记的形态它仍是段 1–3 期间的在场证人，去留留批 7 结项时一并定。
    const std::vector<std::string> &english_keys = borealis::ui::settings_string_keys(InterfaceLanguage::En);
    AURORA_TEST_CHECK_GT(english_keys.size(), 0U);
    for (const std::string &key : english_keys) {
        AURORA_TEST_CHECK_MSG(!borealis::ui::settings_string_text(InterfaceLanguage::En, key).empty(),
                              "the en tier registers an empty template: " + key);
        AURORA_TEST_CHECK_MSG(!borealis::ui::settings_string_text(InterfaceLanguage::Zh, key).empty(),
                              "the en tier has a key the zh tier never heard of: " + key);
    }
}

AURORA_TEST_CASE(the_two_tiers_share_the_same_key_space) {
    // 判据 ② 的**等式**半边，批 3 四段落齐后才立项（`§4.1` 第 3 条：中途立它必红，红在「还没译到的那些
    // key」上而不是红在真缺陷上）。这一案是 7.25⑬「新增界面串必须先加两档」那条纪律唯一的结构式证人，
    // 也是 m2（`kStringsEn` 删一枚 key）自此有牙的地方——上面那一案吃的是英文档自己的形态，删一条它看不见。
    //
    // 断的是两件不同的事，故分开写：① 两档 key 集排序后逐字相等（漏 key 与拼错 key 都在这里红）；
    // ② 每一 key 在两档都有**非空**模板（用空串占位「回头再译」在这一条红，而它骗得过 ①）。
    // 两档同字的那几枚（`{0}` 与三枚字形一类，`§4.1` 第 6 条点名过）在 ①② 上都有读数，本案不看文本是否
    // 相同，故不受它们影响。
    const std::vector<std::string> &chinese_keys = borealis::ui::settings_string_keys(InterfaceLanguage::Zh);
    const std::vector<std::string> &english_keys = borealis::ui::settings_string_keys(InterfaceLanguage::En);
    AURORA_TEST_CHECK_MSG(chinese_keys == english_keys,
                          "the two tiers do not share the same key space: zh holds "
                              + std::to_string(chinese_keys.size()) + " keys, en holds "
                              + std::to_string(english_keys.size()) + ", first difference at "
                              + first_difference(chinese_keys, english_keys));

    for (const std::string &key : chinese_keys) {
        AURORA_TEST_CHECK_MSG(!borealis::ui::settings_string_text(InterfaceLanguage::Zh, key).empty(),
                              "the zh tier registers an empty template: " + key);
        AURORA_TEST_CHECK_MSG(!borealis::ui::settings_string_text(InterfaceLanguage::En, key).empty(),
                              "the en tier registers an empty template: " + key);
    }
}

AURORA_TEST_CASE(the_count_phrases_decline_in_english_but_not_in_chinese) {
    // 判据 ③ 的案，随批 4 立（`§4.1` 第 3 条：英文档本体没落齐时它要么即红要么空转）。断的是三件事：
    //   ① 英文档同一 key 在 n=1 与 n=2 两值渲染出**两条不同的串**（字面判据），且**剥掉计数之后仍不等**
    //      （有牙的那半，见循环里那句注释）——后者吃的才是 `one=` 分支本身：摘掉它（该稿 §8 的 m4），
    //      `select_plural_branch` 回落到 `other=`，名词就不再随数变格；
    //   ② 中文档那两串**剥掉计数之后逐字相等**，且数仍进串——框架 `plural.h` 给 `zh` 的规则恒 `other`，
    //      故中文侧写成块是「形态统一而行为不变」，这一句就是那个「不变」的读数（不能直接断两串相等：
    //      计数本身在串里，见 `renumbered_to_two`）；
    //   ③ 两组金样串抄在测试里（`keys.header.count` 与 `connections.notice.imported`），抓的是分支
    //      **文本**而不是形状：把 `one={0} key` 写成 `one={0} keys` 在 ① 上看不出来。
    // 覆盖范围：全表带计数的词条实测 13 枚（F17），其中英文侧数词直接修饰可数名词、因此两档都写进块的
    // 是那 5 枚（另 8 枚为序数、单位、纯索引或数词后无名词，逐枚理由见该稿 §8 的批 4 读数）。本案吃
    // 这 5 枚——它们在两档都必须是块形态，而 `other`-only 的中文块在 ② 上与旧串同形。批 6 从表外搬进来的
    // `terminal.paste_multiline.body`（「…有 {0} 行…」）是第 14 枚计数词条、第 6 枚成块条目，随批 6 入列。
    const std::array<CountPhrase, 6> kCountPhrases{
        CountPhrase{"connections.notice.imported", 0U, 1U},
        CountPhrase{"keys.header.count", 0U, 1U},
        CountPhrase{"workspace.broadcast.title", 1U, 2U},
        CountPhrase{"session.reconnect.exhausted", 0U, 1U},
        CountPhrase{"terminal.notice.unrepresentable.line", 1U, 2U},
        CountPhrase{"terminal.paste_multiline.body", 0U, 1U},
    };

    au::StringTable english_table;
    borealis::ui::install_settings_strings(english_table, InterfaceLanguage::En);
    au::StringTable chinese_table;
    borealis::ui::install_settings_strings(chinese_table, InterfaceLanguage::Zh);

    for (const CountPhrase &phrase : kCountPhrases) {
        const std::string en_one = render_count(english_table, InterfaceLanguage::En, phrase, 1);
        const std::string en_two = render_count(english_table, InterfaceLanguage::En, phrase, 2);
        AURORA_TEST_CHECK_MSG(!en_one.empty() && !en_two.empty(),
                              std::string{"the en tier resolved the count phrase to an empty string: "}
                                  + std::string{phrase.key});
        AURORA_TEST_CHECK_MSG(en_one.find('1') != std::string::npos && en_two.find('2') != std::string::npos,
                              std::string{"the count never reached the en string for "} + std::string{phrase.key}
                                  + ": 1 -> " + en_one + ", 2 -> " + en_two);
        AURORA_TEST_CHECK_MSG(en_one != en_two,
                              std::string{"the en tier reads identically for one and two on "}
                                  + std::string{phrase.key} + ": 1 -> " + en_one + ", 2 -> " + en_two);
        // 上面那句**不是 m4 的落点**（批 4 实测）：计数本身在串里，块退化到只剩 `other=` 时 n=1 拿回
        // "1 keys"、n=2 拿回 "2 keys"，两串照样不等，① 的字面判据当场空转。真正有牙的是这一句——
        // 把数剥掉后仍要不等，说的才是「名词跟着数变了格」。摘掉 5 枚里任何一枚的 `one=`，这里逐枚红。
        AURORA_TEST_CHECK_MSG(renumbered_to_two(en_one) != en_two,
                              std::string{"the en tier differs from one to two only by the digit on "}
                                  + std::string{phrase.key} + " (the plural block has no one= branch): 1 -> " + en_one
                                  + ", 2 -> " + en_two + ", renumbered 1 -> " + renumbered_to_two(en_one));

        const std::string zh_one = render_count(chinese_table, InterfaceLanguage::Zh, phrase, 1);
        const std::string zh_two = render_count(chinese_table, InterfaceLanguage::Zh, phrase, 2);
        AURORA_TEST_CHECK_MSG(!zh_one.empty() && zh_one.find('1') != std::string::npos,
                              std::string{"the zh tier lost the count for "} + std::string{phrase.key}
                                  + ": 1 -> " + zh_one);
        AURORA_TEST_CHECK_MSG(renumbered_to_two(zh_one) == zh_two,
                              std::string{"the zh tier declined a count phrase (it must not: plural.h gives zh "
                                          "the other category always): 1 -> "}
                                  + zh_one + ", 2 -> " + zh_two + ", renumbered 1 -> " + renumbered_to_two(zh_one));
    }

    // ③ 的金样：抄在测试里的两条英文串，读点与被测表无关（与 ②' 那一案同一口径，代价也同——改措辞要同步改这里）。
    AURORA_TEST_CHECK_EQ(render_count(english_table, InterfaceLanguage::En, kCountPhrases[1], 1),
                         std::string{"1 key"});
    AURORA_TEST_CHECK_EQ(render_count(english_table, InterfaceLanguage::En, kCountPhrases[1], 2),
                         std::string{"2 keys"});
    AURORA_TEST_CHECK_EQ(render_count(english_table, InterfaceLanguage::En, kCountPhrases[0], 1),
                         std::string{"Imported 1 profile"});
    AURORA_TEST_CHECK_EQ(render_count(english_table, InterfaceLanguage::En, kCountPhrases[0], 2),
                         std::string{"Imported 2 profiles"});
}

}  // namespace borealis::test_cases::utest_i18n_tiers
