/// 测试类型: unit
/// 目标单元: src/ui/settings_i18n.{h,cpp} 的语言档接缝（`settings_locale`／两枚装填函数／
///           `settings_string_keys`／`settings_string_text`）+ src/config/schema_names.h 的 `language_tag`
/// 测试说明: 守住 `SPEC.FEAT.PREF.05` 判据 ①②③ 的纯逻辑半边。Aurora i18n 换成构建期管线（v2）之后，
///           判据的**职责不变、承载换了**：真值源从本件里的 C++ 表搬进 `assets/i18n/messages_{zh,en}.json`，
///           两档合一枚 `AUM1` bin 由 `load_binary` 整读，于是「只装选定那一档」这一形态**在 v2 里不存在**
///           （bin 里两档都在，档位差别只体现在缺省档与查询用的 `Locale` 上）。八条：
///           ① 落盘名与 i18n 区域标签同源（`language_tag` 是那条同源关系的唯一读点，装填侧不得自列第二份
///           名字）；② 两档都把全表译满（`missing_translations` 在本仓 key 集上为空，且每一 key 在两档都
///           解析得出非空串）——这是 7.25⑬「新增界面串必须先加两档」那条纪律唯一的结构式证人，旧形态里
///           由「单向子集案 + 等式案」两条分管，v2 里 key 集是两档共用的单一真值，故合成一案；
///           ②' 同一件事的**独立读点**：五对期望串抄在测试里，与被测的装填接缝无关，因此「把缺省档与源
///           表一起写死回 `zh`」这类退化（该稿 §8 的 m1）在这里转红；③ 缺省档随参数走——按一枚**两档都没
///           有**的区域（`fr`）去查，回退链末端就是刚装的那一档，于是框架在没有 `Provider<Locale>` 的那几处
///           也落回用户真正选的档（F3／F5）；④ 进程级 `settings_locale()` 随装填参数移动；⑤ 查表失败的
///           可见形态（F13：回退 `text` 恒空 ⇒ 空白而不是漏出 key）另立一案，因为它是面板上「覆盖率证人」
///           成立的前提；⑥ key 表有序且唯一（覆盖率判据的读点自身要站得住）；⑦ 判据 ③ 的复数形态（成块
///           的计数词条在英文档 1／2 两值必须两串、在中文档剥掉计数后必须同串），外加四组抄在测试里的
///           英文金样串。
///
///           **本案刻意不立的两句**：
///           ① 「选英文档时中文档不在表里」——v2 的 bin 全语言合一，这一形态从成立变成根本不成立，写它
///              就是在断框架的装载细节而不是断本仓的口径；
///           ② 「`Locale{}`（＝`en`）查表落回用户档」——旧形态只装一档时这条成立，v2 里 `en` 是有本体的
///              一档，缺 `Provider<Locale>` 的绘制侧**必然**读成英文。这件事的新读点是「场景根必须装
///              `au::LocaleProvider(settings_locale())`」，由 `itest_command_palette` 的绘制案与
///              `src/main.cpp` 装配层承担，不在这份纯逻辑案里复述（文件头④）。
///           ②' 抄在测试里的那五对是金样：改那一档的措辞就得同步改这里，这是把 m1 变成可抓事实的价格。

#include "ui/settings_i18n.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/i18n/locale.h"
#include "aurora/i18n/localized_string.h"
#include "aurora/i18n/message_catalog.h"
#include "aurora/i18n/message_id.h"

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
    return au::Locale{std::string{borealis::config::language_tag(language)}, {}, {}};
}

/// @brief 按 key 在最本质的链路上渲染一枚串（走 `MessageCatalog::resolve`，不是读模板）。
///
/// 复数块只有在 `resolve` 里才展开，判据 ⑦ 必须经这条链路；未填的占位由调用方补 `kOtherFiller`，
/// 免得「块写坏」的读数被占位符本身盖掉。
[[nodiscard]] auto render(const au::MessageCatalog &catalog, std::string_view key,
                          std::vector<au::MessageArg> args, const au::Locale &locale) -> std::string {
    return catalog.resolve(au::LocalizedString::tr(catalog.find_by_name(key), std::move(args)), locale);
}

/// @brief 判据 ⑦ 的样本：一枚带计数的词条 key，以及除计数外还要填的占位名。
///
/// 计数一律住 `count`（两档 ARB 的块都写 `{count, plural, …}`），其余占位填 `kOtherFiller`
/// （不含数字）——本案只看「数进没进串、单复数变没变」，让填充串带数字就会把 `find('1')` 那两句
/// 变成空转；而占位**不填**的话框架会把 `{names}` 原样留下，金样案就得分叉处理，不如统一给值。
struct CountPhrase {
    std::string_view key;
    std::array<std::string_view, 2> other_names;  ///< 除 `count` 之外的占位名（不足两枚即留空）。
    std::size_t other_count;
};

constexpr std::string_view kOtherFiller{"A"};

/// @brief 按判据 ⑦ 的口径渲染一枚计数词条。
[[nodiscard]] auto render_count(const au::MessageCatalog &catalog, const CountPhrase &phrase,
                                const au::Locale &locale, const std::int64_t count) -> std::string {
    std::vector<au::MessageArg> args;
    args.emplace_back("count", count);
    for (std::size_t k = 0; k < phrase.other_count; ++k) {
        args.emplace_back(std::string{phrase.other_names[k]}, std::string{kOtherFiller});
    }
    return render(catalog, phrase.key, std::move(args), locale);
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

/// @brief 整张模板就是一枚占位的 key（填不出参数，只能按「该档有没有这一条」判，不能按「解析出的串
///        非空」判）。v2 的 `resolve` 对缺参数的占位回空串，于是这一枚在全表非空读数里是唯一的例外：
///        界面上它拿得到串（调用点把 `{uri}` 填成链接目标），缺参数的解析链路只能拿回空。漏译/删 key
///        仍由 `missing_translations` 那半边管，本案只放掉「非空模板」这一句。
constexpr std::string_view kPlaceholderOnlyKey{"links.confirm.uri"};

/// @brief 本仓 key 集里是否住着一个 Id（`missing_translations` 的过滤腿）。
///
/// 库内置词表（`command_palette.placeholder` 那一族）本来就只给了英文，让它混进「本仓两档译满」的
/// 读数会把案写成恒红；按规范名过滤才只说本仓 ARB 的事。
[[nodiscard]] auto holds_name(const std::vector<std::string> &keys, const au::MessageCatalog &catalog,
                              const au::MessageId id) -> bool {
    const std::string_view name = catalog.canonical_name(id);
    return std::find(keys.begin(), keys.end(), name) != keys.end();
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

    // ① 的分组半边 + 「装出来的东西确实住在该档那一格」：按**落盘名**拼出的 `Locale` 去查，该档源表的
    //    每一 key 都要解析得出非空串。装填侧若把 tag 写成 `"zh-CN"`（`gen_i18n` 按 ARB 的 `locale` 字段
    //    归档，那边是 `"zh"`），回退链右删次序会先丢 region 再命中 `zh`，所以这条今天仍能命中——它真正
    //    咬住的是「key 集整个对不上」与「档内空模板」（后者由 ② 的非空半边接手）。
    AURORA_TEST_REQUIRE(borealis::ui::install_settings_strings());  // 进程表未装填时 key 表是空集。
    const std::vector<std::string> &keys = borealis::ui::settings_string_keys();
    AURORA_TEST_REQUIRE(!keys.empty());
    for (const InterfaceLanguage language : kBothTiers) {
        au::MessageCatalog catalog;
        borealis::ui::install_settings_strings(catalog, language);
        for (const std::string &key : keys) {
            if (key == kPlaceholderOnlyKey) {
                continue;  // 见 `kPlaceholderOnlyKey`：缺参数的占位模板恒解析为空。
            }
            AURORA_TEST_CHECK_MSG(!render(catalog, key, {}, locale_of(language)).empty(),
                                  std::string{borealis::config::language_tag(language)} + " tier did not answer "
                                      + key + " when addressed by its on-disk name");
        }
    }
}

AURORA_TEST_CASE(both_tiers_translate_every_key) {
    // 判据 ② 的等式半边，v2 形态：key 集是两档共用的单一真值（译文不得加 key、也不得删 key，`gen_i18n`
    // 在构建期就拦下），所以「两档 key 集逐字相等」不再有独立读点，剩下要断的是**每一 key 在两档都有非空
    // 模板**——旧形态里这由「单向子集案 + 等式案」两条分管，现在合成一案，仍是 7.25⑬ 那条纪律唯一的
    // 结构式证人，也是 m2（英文档删一枚 key）自此有牙的地方。
    //
    // 断的是两件不同的事，故分开写：① `missing_translations` 在本仓 key 集上为空（删 key／漏译在这里红，
    // 且它绕过了回退链——`resolve` 查不到该档会落到缺省档的模板，非空读数骗得过 `resolve` 那半，骗不过
    // 「这一档到底有没有自己的模板」）；② 每一 key 在两档都解析得出**非空**串（用空串占位「回头再译」
    // 在 ① 上看不见：空串是一条登记，② 才管它）。库内置词条只给英文，故 ① 按本仓 key 名过滤（见
    // `holds_name`）。
    // `settings_string_keys()` 数的是**进程级**那张目录（v2 里 key 集住在装填后的目录里），故先确保
    // 进程档已装填——上面那一案只往新造的目录里装，不会把进程表填上。
    AURORA_TEST_REQUIRE(borealis::ui::install_settings_strings());
    const std::vector<std::string> &keys = borealis::ui::settings_string_keys();
    AURORA_TEST_REQUIRE(!keys.empty());

    for (const InterfaceLanguage language : kBothTiers) {
        au::MessageCatalog catalog;
        borealis::ui::install_settings_strings(catalog, language);
        for (const au::MessageId id : catalog.missing_translations(locale_of(language))) {
            AURORA_TEST_CHECK_MSG(!holds_name(keys, catalog, id),
                                  std::string{borealis::config::language_tag(language)}
                                      + " tier is missing a translation of "
                                      + std::string{catalog.canonical_name(id)});
        }
        for (const std::string &key : keys) {
            if (key == kPlaceholderOnlyKey) {
                continue;  // 见 `kPlaceholderOnlyKey`。
            }
            AURORA_TEST_CHECK_MSG(!borealis::ui::settings_string_text(language, key).empty(),
                                  std::string{"the "} + std::string{borealis::config::language_tag(language)}
                                      + " tier holds an empty template: " + key);
        }
    }
}

AURORA_TEST_CASE(installed_tiers_answer_templates_pinned_in_the_test) {
    // 判据 ①② 装填腿的**第二份**证人，与上面两案互补而非重复：那两案的等式右边都经由装填后的同一张表
    // 读数，而 m1 那条变异恰好写死的是同一处，于是「表答回的＝源表读数」自反成立、等式案对 m1 全形态
    // 失明（批 3 段 2 实测）。本案把期望串**抄在测试里**，读点与被测函数无关，因此 m1 的两种形态（只写
    // 死缺省档 / 连源表一起写死）都在这里转红。
    //
    // 代价照实登记：这五对是**金样**，改了那一档的措辞就得同步改这里——这是将 m1 变成可抓事实的价格。
    // 选 key 的口径：两档都有本体、且两档文字不同（同字的那些条目，如 `settings.chain.up` 那三枚字形，
    // 在这里没有分辨力）；`keys.dir.unreachable` 那一枚顺带证明**具名占位**的参数值原样进串（v2 里位置
    // 占位已不存在）。
    struct Pin {
        std::string_view key;
        std::string_view english;
        std::string_view arg_name;  ///< 非空＝渲染时要填一枚具名参数（值取 `kPinnedArgValue`）。
    };
    constexpr std::string_view kPinnedArgValue{"/srv/gone"};
    constexpr std::array<Pin, 5> kPins{
        Pin{"settings.title", "Settings", {}},
        Pin{"settings.badge.next_launch", "Takes effect after restart", {}},
        Pin{"settings.chain.filter", "Type a family name to add it", {}},
        Pin{"connections.action.import", "Import ~/.ssh/config", {}},
        Pin{"keys.dir.unreachable", "Directory unreachable: /srv/gone", "path"},
    };

    au::MessageCatalog english_catalog;
    borealis::ui::install_settings_strings(english_catalog, InterfaceLanguage::En);
    au::MessageCatalog chinese_catalog;
    borealis::ui::install_settings_strings(chinese_catalog, InterfaceLanguage::Zh);

    for (const Pin &pin : kPins) {
        std::vector<au::MessageArg> args;
        if (!pin.arg_name.empty()) {
            args.emplace_back(std::string{pin.arg_name}, std::string{kPinnedArgValue});
        }
        const std::string en = render(english_catalog, pin.key, std::vector<au::MessageArg>{args},
                                      locale_of(InterfaceLanguage::En));
        AURORA_TEST_CHECK_MSG(en == pin.english,
                              std::string{"the en tier did not serve the pinned template for "}
                                  + std::string{pin.key} + ": got " + en);

        // 反方向只需「不是那份英文金样」：中文档答出英文模板，说明源表那半被写死反了。
        const std::string zh = render(chinese_catalog, pin.key, std::vector<au::MessageArg>{args},
                                      locale_of(InterfaceLanguage::Zh));
        AURORA_TEST_CHECK_MSG(!zh.empty() && zh != pin.english,
                              std::string{"the zh tier served the pinned english template for "}
                                  + std::string{pin.key} + ": got " + zh);
    }
}

AURORA_TEST_CASE(an_unregistered_locale_falls_back_to_the_installed_tier) {
    // 判据 ③：缺省档随装填参数走。读点是一枚**两档都没有**的区域 `fr`——回退链在自有档之后落到末端
    // （＝刚装的缺省档），于是「装了哪一档」在这条查表上直接可读。装填侧若把缺省档写死回 `zh`、忽略
    // 参数（该稿 §8 的 m1），下面第二跳当场红。
    //
    // 旧形态里这一条由 `Locale{}`（＝`en`）供给读数，因为英文档当时是空表；v2 的 bin 两档都有本体，
    // `Locale{}` 因此落进「有自己模板」的那一格、永远读英文，不再是一支可用的探针（文件头那句「刻意
    // 不立的第二句」）。`fr` 不受词表厚度影响，是这件事在 v2 里的等价证人。
    ProcessTierGuard guard;  // 进程级那张表是全仓共享的，见 `ProcessTierGuard`。
    const au::Locale foreign{"fr", {}, {}};
    const std::string_view key{"settings.title"};

    AURORA_TEST_REQUIRE(borealis::ui::install_settings_strings(InterfaceLanguage::Zh));
    const std::string chinese_answer = borealis::ui::settings_resolve(borealis::ui::settings_text(key));
    au::MessageCatalog catalog;
    borealis::ui::install_settings_strings(catalog, InterfaceLanguage::Zh);
    AURORA_TEST_CHECK_EQ(render(catalog, key, {}, foreign), chinese_answer);

    borealis::ui::install_settings_strings(InterfaceLanguage::En);
    borealis::ui::install_settings_strings(catalog, InterfaceLanguage::En);
    const std::string english_answer = render(catalog, key, {}, locale_of(InterfaceLanguage::En));
    AURORA_TEST_CHECK_EQ(english_answer, std::string{"Settings"});
    AURORA_TEST_CHECK_EQ(render(catalog, key, {}, foreign), english_answer);
    // 同一枚 key 在两档答回不同串，`foreign` 跟着缺省档移动才有读数（两档同字时本案是空转）。
    AURORA_TEST_CHECK_MSG(chinese_answer != english_answer, "the two tiers answer identical text for settings.title");
}

AURORA_TEST_CASE(the_process_locale_moves_with_the_installed_language) {
    ProcessTierGuard guard;  // 见 `ProcessTierGuard`：进程级那张表是全仓共享的。

    // ④：`settings_locale()` 是面板侧唯一的 `Locale` 副本（`Locale::tag()` 每次现拼串），装配层与用例
    //    驱动台把它交进 `au::LocaleProvider`（文件头④）。写它的只有下面这两跳，故「切档 ⇒ 绘制侧跟着
    //    切」在这里有牙。
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

AURORA_TEST_CASE(the_vocabulary_keys_are_sorted_and_unique) {
    // `settings_string_keys()` 是判据 ② 的读点（v2 里它数的是**装填后的目录**，从 Id 基址沿直排读到
    // 空洞即停），故它的三条结构性质要在这一侧自证：字典序（比对与调试都按它）、key 唯一（名字表里
    // 重复注册会被 `register_vocabulary` 结构化拒掉，正是 F8 那处死条目曾经的样子）、以及基址上没有
    // 空洞（空洞会把后面的整段词条静默截断，覆盖率证人于是变成一条恒真的短表）。
    AURORA_TEST_REQUIRE(borealis::ui::install_settings_strings());
    const std::vector<std::string> &keys = borealis::ui::settings_string_keys();
    AURORA_TEST_CHECK_MSG(std::ranges::adjacent_find(keys, std::ranges::greater{}) == keys.end(),
                          "vocabulary keys are not sorted");
    AURORA_TEST_CHECK_MSG(std::ranges::adjacent_find(keys) == keys.end(), "vocabulary keys hold a duplicate");
    AURORA_TEST_CHECK_GT(keys.size(), 300U);
    // 基址那枚必须落得住：`install_into` 的幂等判据（「基址上有名字＝bin 进过这张目录」）吃的就是它，
    // 空洞若出现在基址上，上面三条读数会一起退化成空集断言。
    AURORA_TEST_REQUIRE(!keys.empty());
    AURORA_TEST_CHECK_MSG(!borealis::ui::settings_string_text(InterfaceLanguage::Zh, keys.front()).empty(),
                          "the id base hole truncated the vocabulary: " + keys.front());
}

AURORA_TEST_CASE(the_count_phrases_decline_in_english_but_not_in_chinese) {
    // 判据 ⑦ 的案。断的是三件事：
    //   ① 英文档同一 key 在 n=1 与 n=2 两值渲染出**两条不同的串**（字面判据），且**剥掉计数之后仍不等**
    //      （有牙的那半，见循环里那句注释）——后者吃的才是 `one{…}` 分支本身：摘掉它（该稿 §8 的 m4），
    //      `select_plural_branch` 回落到 `other{…}`，名词就不再随数变格；
    //   ② 中文档那两串**剥掉计数之后逐字相等**，且数仍进串——框架 `plural.h` 给 `zh` 的规则恒 `other`，
    //      故中文侧写成块是「形态统一而行为不变」，这一句就是那个「不变」的读数（不能直接断两串相等：
    //      计数本身在串里，见 `renumbered_to_two`）；
    //   ③ 四枚金样串抄在测试里（`keys.header.count` 与 `connections.notice.imported` 各两值），抓的是
    //      分支**文本**而不是形状：把 `one {{count} key}` 写成 `one {{count} keys}` 在 ① 上看不出来。
    //      这一条在迁移期真的红过一次：从旧 `one={0} … other=` 无括号语法机械转写时，`one` 分支尾部吃了
    //      一个 ` o`，形态是 `one {{count} profile o}`——① 照旧通过，金样案把它抓回。
    // 覆盖范围：全表带计数的词条实测 14 枚（F17），其中英文侧数词直接修饰可数名词、因此两档都写进块的
    // 是那 6 枚（另 8 枚为序数、单位、纯索引或数词后无名词，逐枚理由见该稿 §8 的批 4 读数）。
    constexpr std::array<CountPhrase, 6> kCountPhrases{
        CountPhrase{"connections.notice.imported", {}, 0U},
        CountPhrase{"keys.header.count", {}, 0U},
        CountPhrase{"workspace.broadcast.title", {"names"}, 1U},
        CountPhrase{"session.reconnect.exhausted", {}, 0U},
        CountPhrase{"terminal.notice.unrepresentable.line", {"leg"}, 1U},
        CountPhrase{"terminal.paste_multiline.body", {}, 0U},
    };

    au::MessageCatalog catalog;
    borealis::ui::install_settings_strings(catalog, InterfaceLanguage::En);

    for (const CountPhrase &phrase : kCountPhrases) {
        const std::string en_one = render_count(catalog, phrase, locale_of(InterfaceLanguage::En), 1);
        const std::string en_two = render_count(catalog, phrase, locale_of(InterfaceLanguage::En), 2);
        AURORA_TEST_CHECK_MSG(!en_one.empty() && !en_two.empty(),
                              std::string{"the en tier resolved the count phrase to an empty string: "}
                                  + std::string{phrase.key});
        AURORA_TEST_CHECK_MSG(en_one.find('1') != std::string::npos && en_two.find('2') != std::string::npos,
                              std::string{"the count never reached the en string for "} + std::string{phrase.key}
                                  + ": 1 -> " + en_one + ", 2 -> " + en_two);
        // 块写坏（未闭合的花括号）的形态是那一块原样上屏，串里留着的 `{count` 占位永远等不来数：
        AURORA_TEST_CHECK_MSG(en_one.find("plural") == std::string::npos && en_two.find('{') == std::string::npos,
                              std::string{"the en plural block for "} + std::string{phrase.key}
                                  + " was not expanded: 1 -> " + en_one + ", 2 -> " + en_two);
        AURORA_TEST_CHECK_MSG(en_one != en_two,
                              std::string{"the en tier reads identically for one and two on "}
                                  + std::string{phrase.key} + ": 1 -> " + en_one + ", 2 -> " + en_two);
        // 上面那句**不是 m4 的落点**（批 4 实测）：计数本身在串里，块退化到只剩 `other{…}` 时 n=1 拿回
        // "1 keys"、n=2 拿回 "2 keys"，两串照样不等，① 的字面判据当场空转。真正有牙的是这一句——
        // 把数剥掉后仍要不等，说的才是「名词跟着数变了格」。摘掉 6 枚里任何一枚的 `one{…}`，这里逐枚红。
        AURORA_TEST_CHECK_MSG(renumbered_to_two(en_one) != en_two,
                              std::string{"the en tier differs from one to two only by the digit on "}
                                  + std::string{phrase.key} + " (the plural block has no one branch): 1 -> " + en_one
                                  + ", 2 -> " + en_two + ", renumbered 1 -> " + renumbered_to_two(en_one));
    }

    au::MessageCatalog chinese_catalog;
    borealis::ui::install_settings_strings(chinese_catalog, InterfaceLanguage::Zh);
    for (const CountPhrase &phrase : kCountPhrases) {
        const std::string zh_one = render_count(chinese_catalog, phrase, locale_of(InterfaceLanguage::Zh), 1);
        const std::string zh_two = render_count(chinese_catalog, phrase, locale_of(InterfaceLanguage::Zh), 2);
        AURORA_TEST_CHECK_MSG(!zh_one.empty() && zh_one.find('1') != std::string::npos,
                              std::string{"the zh tier lost the count for "} + std::string{phrase.key}
                                  + ": 1 -> " + zh_one);
        AURORA_TEST_CHECK_MSG(renumbered_to_two(zh_one) == zh_two,
                              std::string{"the zh tier declined a count phrase (it must not: plural.h gives zh "
                                          "the other category always): 1 -> "}
                                  + zh_one + ", 2 -> " + zh_two + ", renumbered 1 -> " + renumbered_to_two(zh_one));
    }

    // ③ 的金样：抄在测试里的四条英文串，读点与被测表无关（与 ②' 那一案同一口径，代价也同——改措辞要同步改这里）。
    AURORA_TEST_CHECK_EQ(render_count(catalog, kCountPhrases[1], locale_of(InterfaceLanguage::En), 1),
                         std::string{"1 key"});
    AURORA_TEST_CHECK_EQ(render_count(catalog, kCountPhrases[1], locale_of(InterfaceLanguage::En), 2),
                         std::string{"2 keys"});
    AURORA_TEST_CHECK_EQ(render_count(catalog, kCountPhrases[0], locale_of(InterfaceLanguage::En), 1),
                         std::string{"Imported 1 profile"});
    AURORA_TEST_CHECK_EQ(render_count(catalog, kCountPhrases[0], locale_of(InterfaceLanguage::En), 2),
                         std::string{"Imported 2 profiles"});
}

}  // namespace borealis::test_cases::utest_i18n_tiers
