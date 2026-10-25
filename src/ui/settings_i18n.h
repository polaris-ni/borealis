#pragma once

// ============================================================
// 界面短语的装填与读取接缝（src/ui/settings_i18n.h）
// ------------------------------------------------------------
// 裁决 7.52 的 S11 拍板「中文显示文案直接用框架 i18n」，本件是那三条接缝的唯一装填点。
// `SPEC.FEAT.PREF.05` 结项后 Aurora 把 i18n 换成构建期管线（v2），三条接缝的**职责不变、承载换了**：
// ① 词条 key **就是落盘点号路径**（`appearance.font_size_pt` 这一串），于是面板不需要另维护
//    「键 → 标签」的映射，反向核对件（`ui::settings_catalog()`）新增一行而忘记配词条，就会让
//    `itest_settings_panel` 的「每一 catalog 键都有词条」转红——而不是界面上静默出现一个空标签
//    （`LocalizedString::tr()` 造出的实例 text 恒空，查表失败回退的就是那枚空串）。
// ② 短语的真值源不再是本文件里的 C++ 表，而是 `assets/i18n/messages_{zh,en}.json`：两档 ARB 词表
//    在构建期由 `gen_i18n` 合一成 `AUM1` 二进制（`cmake/AuroraI18n.cmake` 的 `aurora_i18n_generate`，
//    见 `src/CMakeLists.txt`），本件整读那份 bin 喂 `default_catalog().load_binary()`。按 key 读的那
//    一路走 `MessageCatalog::find_by_name`（数据驱动的 key 只能走这条），写死的 key 则可以直接用生成头
//    给出的具名形参构造函数 `borealis::msg::<域>::<名>(args…)`——那条路的参数名与类型在编译期就有牙。
//    于是「表外多一枚引号内中文」这件事的证人从「表」变成「JSON 之外不许有 CJK 字面量」，
//    `tools/check/check_display_literals.py` 按此改口径。
// ③ 区域设置由**装填时给定的语言档**决定（`SPEC.FEAT.PREF.05`）：`install_settings_strings(language)`
//    把该档的 tag 设为目录的缺省档（回退链末端），并把 `settings_locale()` 定到该档。缺省档为什么必须
//    跟着走：框架的 `Text` 在没有 `Provider<Locale>` 时按 `Locale{}`（即 `en`）查表，而回退链的末端
//    就是缺省档，于是「缺省档随参数」让所有回落路径都落回用户真正选的那一档（该稿 F3／F5／F19 三条
//    读数的合取）。缺省语言是 `zh`（该稿 D3）。
//    与旧形态的唯一差别：bin 里**两档都在**（全语言合一），选定英文档时中文档并没有被「不装」，而是
//    永远排在回退链之外——英文档缺译的那一枚解析不出模板，回退 `text`（恒空）而不是漏出中文。这条
//    可见形态仍由 F13 登记，且正是面板上「覆盖率证人」成立的前提。
// ④ 绘制侧（框架 `Text`／`Button` 经 `ctx` 解析的那一路）读的是**环境里注入的 `Locale`**，不是目录的
//    缺省档。装配层与用例驱动台因此都要在场景根装一枚 `au::LocaleProvider(settings_locale(), …)`：
//    不装，切到英文档的那一帧整窗绘制侧文案会按 `Locale{}`（`en`）解析（该稿 F19 的探针实测）。
//
// 一处框架实测（曾登记为缺口 **G28**，已回货闭合，裁决 7.59）：`Button::label` 与 `Text::content` 同为
// `LocalizedString`，但登记时只有 `Text` 在绘制路径查表——`Button` 的 `paint_label` 直读
// `label.get().text`，而同件 `accessibility_label()` 却走 `resolve()`，故 `tr()` 造出的实例（`text` 恒空）
// 在按钮上是**空白标签而朗读得到译文**。回货形态是 `Button::resolved_label(ctx)` 成为布局与绘制的唯一
// 显示串来源。`settings_label()` 因此**只留给收 `std::string` 的入口**（`Text::placeholder`、
// `Command::title`、角标拼接）——那些入口本就没有查表路径，调用方给显示串才是唯一形态。
//
// 私有头（裁决 D1① 同口径）：本件含框架类型，不进 `include/borealis/`。
// ============================================================

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/i18n/locale.h"
#include "aurora/i18n/localized_string.h"
#include "aurora/i18n/message_catalog.h"
#include "aurora/i18n/message_id.h"

#include "borealis/config/settings.h"
#include "borealis/ui/settings_form.h"

namespace borealis::ui {

/// @brief 面板短语当前所在的区域设置（＝最近一次进程级装填选定那一档，未装填时为中文档）。
///
/// 运行期恒等：写它的只有下面那枚收 `InterfaceLanguage` 的装填函数，而那一跳按文件头①的纪律排在
/// 任何控件绘制之前（`SPEC.FEAT.PREF.05` 稿 §5 第 1 条）。装配层与用例驱动台把它交进
/// `au::LocaleProvider`，绘制侧才跟着档位走（文件头④）。
[[nodiscard]] auto settings_locale() -> const aurora::Locale &;

/// @brief 把构建期词表装进进程级 `default_catalog()`，并把 `settings_locale()` 与目录缺省档定到该档。
///
/// 幂等且只读盘一次：词表字节与 `load_binary` 各做一次，之后的重复调用只移动缺省档，故装配层与用例
/// 都可以放心各调一次（本仓用例约两百处调用点吃的就是这一条）。必须在任何面板控件**绘制之前**调用，
/// 否则首帧的空标签会闪一下（查表失败回退空 text）。缺省参数就是中文档——那是本键落地前唯一的形态，
/// 也是 `SPEC.FEAT.PREF.05` 稿 D3 选的缺省。
/// @param language 语言档；装配层传配置里读到的那一档。
/// @return 词表字节读到且 `load_binary` 未报结构错误时为 true；读不到时界面按「空白而不是漏 key」
///         的 F13 形态退化，调用方据此记日志。
auto install_settings_strings(config::InterfaceLanguage language = config::InterfaceLanguage::Zh) -> bool;

/// @brief 同一件事，宿主是调用方给的目录，**不碰进程级那一张**、也不移动 `settings_locale()`。
///
/// 判据 ①② 的证人因此可以在同一进程里并排比较两档而不影响别的用例（`codespec/PREF05_I18N.draft.md`
/// §4.1 第 3 条）。`catalog` 拿到的是全语言合一的同一份 bin，档位差别只体现在缺省档与查询用的
/// `Locale` 上（文件头③）。
/// @param catalog 目标消息目录。
/// @param language 语言档。
/// @return 同上面那枚。
auto install_settings_strings(aurora::MessageCatalog &catalog, config::InterfaceLanguage language) -> bool;

/// @brief 应用词表的全部 key（规范名，已按字典序；与装填了哪一档无关）。
///
/// 给用例侧的覆盖率判据用（`SPEC.FEAT.PREF.05` 判据 ②）。旧形态下这是**两枚**按档读源表的接缝，
/// 现在 key 集是两档共用的单一真值（译文不得加 key、也不得删 key，由 `gen_i18n` 在构建期拦下），
/// 故只剩一枚；「某一档到底译没译到」改由 `settings_string_text(language, key)` 逐 key 读。
[[nodiscard]] auto settings_string_keys() -> const std::vector<std::string> &;

/// @brief 某一档下某 key 的**解析读数**（该档没有这一 key 时回空串）。
///
/// 走真解析链路（`MessageCatalog::resolve`），不是读模板串——模板串在 v2 里不出库（`MessageCatalog`
/// 只给 `resolve` / `canonical_name` / `find_by_name` 三张读点）。空串在这里读作「该档没有这一 key」，
/// 而不是「模板本身是空」：后者会让界面上出现 F13 那种空白，判据 ② 的「逐 key 两档都非空」案负责把
/// 两者分开。带占位的模板在缺参数时仍渲染出占位周围的文字，故非空读数不依赖参数。
/// @param language 语言档。
/// @param key 词条 key。
[[nodiscard]] auto settings_string_text(config::InterfaceLanguage language, std::string_view key) -> std::string;

/// @brief 按 key 取待本地化串，可直接交 `Text::content` 与 `FormField` 的标签。
///
/// key 不在词表里时回空 text 的实例（`localize` 关不上，因为 Id 无效——`resolve` 因此原样回退空串），
/// 与文件头①说的「空白而不是漏 key」同一形态。写死 key 且带参数的调用点优先用生成头的具名构造函数，
/// 那条路把参数名与类型挪到编译期。
/// @param key 词条 key（设置项一律传落盘点号路径）。
/// @param args 具名模板参数（名字须与该 key 的 `@key.placeholders` 元数据一致），默认为空。
[[nodiscard]] auto settings_text(std::string_view key, std::vector<aurora::MessageArg> args = {})
    -> aurora::LocalizedString;

/// @brief 就地解析成显示串。
///
/// 只给框架**不收 `LocalizedString`** 的入口用（`Text::placeholder`、`Command::title`、角标拼接，
/// 见文件头那条实测的后半段）；收 `LocalizedString` 的属性（`Text::content`、`Button::label`）一律
/// 优先 `settings_text()`，由框架就地查表。
/// @param key 词条 key。
/// @param args 具名模板参数，默认为空。
[[nodiscard]] auto settings_label(std::string_view key, std::vector<aurora::MessageArg> args = {}) -> std::string;

/// @brief 同上，宿主是已经构造好的待本地化串（生成头的具名构造函数产出的就是它）。
///
/// 名字与上面那枚分开而**不重载**：`LocalizedString` 与 `std::string_view` 都能从 `const char*` 构造，
/// 重载会让 `settings_label("字面 key")` 变成一摊歧义（本仓实测）。
/// @param text 待解析串；非本地化值原样回其 `text`。
[[nodiscard]] auto settings_resolve(const aurora::LocalizedString &text) -> std::string;

/// @brief 判某 key 在**当前档**是否解析得出（用例的「词条覆盖率」判据用）。
/// @param key 词条 key。
[[nodiscard]] auto has_settings_string(std::string_view key) -> bool;

/// @brief 一次提交未通过的原因 → 词条。
///
/// 本件是 `ui::CommitIssue` 翻成文案的**唯一**地点：表单件只回枚举而不产文案（其文件头②），
/// 于是十二个非 `None` 值各有词条、且面板不 switch 出第二套措辞（枚数照 `settings_form.h` 的枚举本体，
/// `SPEC.FEAT.PREF.05` 稿 F9 就地更正此处曾写的「十三」）。
/// @param issue 提交结果里的原因；`None` 回空串实例（调用方在通过时不该取它）。
[[nodiscard]] auto settings_issue_text(CommitIssue issue) -> aurora::LocalizedString;

}  // namespace borealis::ui
