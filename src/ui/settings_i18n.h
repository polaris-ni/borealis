#pragma once

// ============================================================
// 设置面板的词条表（src/ui/settings_i18n.h）
// ------------------------------------------------------------
// 裁决 7.52 的 S11 拍板「中文显示文案直接用框架 i18n」，本件是那三条接缝的唯一装填点：
// ① 词条 key **就是落盘点号路径**（`appearance.font_size_pt` 这一串），于是面板不需要另维护
//    「键 → 标签」的映射，反向核对件（`ui::settings_catalog()`）新增一行而忘记配词条，就会让
//    `itest_settings_panel` 的「每一 catalog 键都有中文词条」转红——而不是界面上静默出现一个空标签
//    （框架查表失败回退 `LocalizedString::text`，而 `tr()` 造出的实例 text 恒空）。
// ② 装填走进程级 `default_string_table()`，读取走 `LocalizedString::tr(key)`，因此**控件侧不需要
//    持有表**：框架 `Text` 在绘制时就地查那张表（`widget/text.h` 的 `resolve(&default_string_table(), ...)`）。
// ③ 区域设置由**装填时给定的语言档**决定（`SPEC.FEAT.PREF.05`）：`install_settings_strings(language)`
//    把该档的 tag 设为表内缺省档，并只注册**那一档**的词条——另一档根本不装，理由见下面第④条。
//    缺省档为什么必须跟着走：框架的 `Text` 在没有 `Provider<Locale>` 时按 `Locale{}`（即 `en`）查表，
//    而 `StringTable::lookup` 的第一级失败会**回落到缺省档的 tag**，于是「只装选定档 + 缺省档随参数」
//    这一对才让所有回落路径都落回用户真正选的那一档（该稿 F3／F5／F19 三条读数的合取）。缺省语言
//    是 `zh`（该稿 D3），表本身没有 `default_locale()` 读点，所以 `Locale` 的唯一副本在本件里。
// ④ 绘制侧（框架 `Text`／`Button` 经 `ctx` 解析的那一路）读的是**环境里注入的 `Locale`**，不是表的
//    缺省档。装配层与用例驱动台因此都要在场景根装一枚 `au::LocaleProvider(settings_locale(), …)`：
//    不装，补上英文档的那一天整窗绘制侧文案会换成英文（该稿 F19 的探针实测就是这件事）。
//
// 一处框架实测（曾登记为缺口 **G28**，已回货闭合，裁决 7.59）：`Button::label` 与 `Text::content` 同为
// `LocalizedString`，但登记时只有 `Text` 在绘制路径查表——`Button` 的 `paint_label` 直读
// `label.get().text`，而同件 `accessibility_label()` 却走 `resolve()`，故 `tr()` 造出的实例（`text` 恒空）
// 在按钮上是**空白标签而朗读得到译文**。回货形态是 `Button::resolved_label(ctx)` 成为布局与绘制的唯一
// 显示串来源（解析结果与 `cached_text_width_/height_` 配套缓存在 `cached_display_text_`，无障碍复用同串），
// 于是按钮文案与 `Text` 一样直接交 `LocalizedString`。`settings_label()` 因此**只留给收 `std::string` 的
// 入口**（`Text::placeholder`、`Command::title`、角标拼接）——那些入口本就没有查表路径，调用方给显示串
// 才是唯一形态，不属本条缺口。
//
// 私有头（裁决 D1① 同口径）：本件含框架类型，不进 `include/borealis/`。
// ============================================================

#include <string>
#include <string_view>
#include <vector>

#include "aurora/i18n/locale.h"
#include "aurora/i18n/localized_string.h"
#include "aurora/i18n/string_table.h"

#include "borealis/config/settings.h"
#include "borealis/ui/settings_form.h"

namespace borealis::ui {

/// @brief 面板词条当前所在的区域设置（＝最近一次进程级装填选定那一档，未装填时为中文档）。
///
/// 运行期恒等：写它的只有下面那枚收 `InterfaceLanguage` 的装填函数，而那一跳按文件头①的纪律排在
/// 任何控件绘制之前（`SPEC.FEAT.PREF.05` 稿 §5 第 1 条）。装配层与用例驱动台把它交进
/// `au::LocaleProvider`，绘制侧才跟着档位走（文件头④）。
[[nodiscard]] auto settings_locale() -> const aurora::Locale &;

/// @brief 把某一档的全部词条装进 `table`，并把该档的 tag 设为它的缺省档。
///
/// 指哪张表由形参决定，**不碰进程级那张**：判据 ①② 的证人因此可以在同一进程里并排比较两档而
/// 不影响别的用例（`codespec/PREF05_I18N.draft.md` §4.1 第 3 条）。只装选定那一档的理由见文件头③。
/// @param table 目标字符串表。
/// @param language 语言档。
auto install_settings_strings(aurora::StringTable &table, config::InterfaceLanguage language) -> void;

/// @brief 同上，宿主是进程级 `default_string_table()`，并把 `settings_locale()` 定到该档。
///
/// 幂等：重复调用逐条覆盖同 key，故装配层与用例都可以放心各调一次。必须在任何面板控件**绘制之前**
/// 调用；否则首帧的空标签会闪一下（查表失败回退空 text）。缺省参数就是中文档——那是本键落地前
/// 唯一的形态，也是 `SPEC.FEAT.PREF.05` 稿 D3 选的缺省。
/// @param language 语言档；装配层传配置里读到的那一档。
auto install_settings_strings(config::InterfaceLanguage language = config::InterfaceLanguage::Zh) -> void;

/// @brief 某一档的全部 key（**读源表**，与该档有没有装填无关；已按字典序）。
///
/// 给用例侧的「两档同 key 集」判据用（`SPEC.FEAT.PREF.05` 判据 ②）。这条接缝刻意存在而不是让用例
/// 去数装填后的表：装填只装选定那一档（文件头③），表里看不到另一档的 key。
/// @param language 语言档。
[[nodiscard]] auto settings_string_keys(config::InterfaceLanguage language) -> const std::vector<std::string> &;

/// @brief 按 key 取待本地化串，可直接交 `Text::content` 与 `FormField` 的标签。
/// @param key 词条 key（设置项一律传落盘点号路径）。
/// @param args 模板参数，默认为空。
[[nodiscard]] auto settings_text(std::string_view key, std::vector<aurora::LocalizedString> args = {})
    -> aurora::LocalizedString;

/// @brief 就地解析成显示串。
///
/// 只给框架**不收 `LocalizedString`** 的入口用（`Text::placeholder`、`Command::title`、角标拼接，
/// 见文件头那条实测的后半段）；收 `LocalizedString` 的属性（`Text::content`、`Button::label`）一律
/// 优先 `settings_text()`，由框架就地查表。
/// @param key 词条 key。
/// @param args 模板参数，默认为空。
[[nodiscard]] auto settings_label(std::string_view key, std::vector<aurora::LocalizedString> args = {}) -> std::string;

/// @brief 判某 key 是否已有词条（用例的「词条覆盖率」判据用）。
/// @param key 词条 key。
[[nodiscard]] auto has_settings_string(std::string_view key) -> bool;

/// @brief 一次提交未通过的原因 → 中文词条。
///
/// 本件是 `ui::CommitIssue` 翻成文案的**唯一**地点：表单件只回枚举而不产文案（其文件头②），
/// 于是十二个非 `None` 值各有词条、且面板不 switch 出第二套措辞（枚数照 `settings_form.h` 的枚举本体，
/// `SPEC.FEAT.PREF.05` 稿 F9 就地更正此处曾写的「十三」）。
/// @param issue 提交结果里的原因；`None` 回空串实例（调用方在通过时不该取它）。
[[nodiscard]] auto settings_issue_text(CommitIssue issue) -> aurora::LocalizedString;

}  // namespace borealis::ui
