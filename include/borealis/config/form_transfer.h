#pragma once

// ============================================================
// 配置与表单值之间的搬运件（include/borealis/config/form_transfer.h）
// ------------------------------------------------------------
// `codespec/UI_SETTINGS.draft.md` §8 的 ④ 说的是一件很小的事：面板界面腿要把
// `config::Settings` 的成员搬进搬出 `ui::SettingsForm`。它必须落在 **config 侧**——
// `ui/settings_form.h` 与 `ui/settings_catalog.h` 刻意不含 `config` 类型（`config/settings.h`
// 已 include 本域的 `ui/palette.h`，反向 include 即 `config ⇄ ui` 模块环，裁决 7.32① 与 7.41①
// 同一条理由），而 `config → ui` 是允许的方向。
//
// 本件补上的正是 `utest_settings_form` 文件头如实登记的那条单向边界：表单件按键放的是**值**，
// 不知道某个键对应哪个成员，故「两个同域成员互换」（`appearance.font_line_height` 与
// `appearance.font_letter_spacing_dp` 都是实数）在它结构上抓不到。这里每个叶子键一行、行内并列
// 「取」与「写」两个方向，两侧共用同一行，于是那条边界在本件只剩「整行写错」一种形态，
// 而它由 `tests/unit/utest_form_transfer.cpp` 的取侧与写侧两条逐键证人各守一半。
//
// 三条刻意形态：
// ① **只有一张表**。键 ↔ 成员的对应不在面板、不在表单件、也不在装载侧另列一遍；键一律用
//    `ui::settings_catalog()` 的落盘点号路径，与 `LoadReport::rejected_keys` 同一套地址。
// ② **不复检取值域**。区间、白名单与色板格数由 `ui::SettingsForm` 与装载侧把守（裁决 7.46② 的
//    分工：域由装载侧判，面板与绘制侧都不夹取）。本件因此不夹取、不校验，只折算类型。
//    写回时取的是该行声明的形态，形态不合的值**不改基线**——表单之外的调用方塞错形态，
//    后果是该键保持原值，而不是被写成一个错的档位。
// ③ **未装载的键保留基线**。`ui::SettingsForm` 对缺键与错型键不给值（见其 `FormLoadReport`），
//    面板于是少画一个控件；本件写回时对这些键不动 `base`，因为「面板少画一个控件」不该等于
//    「把该项设置抹掉」。
//
// 枚举的落盘名取自 `src/config/schema_names.h`（与 `store.cpp` 同一张表），故面板里选中的档
// 经 `Store::replace()` 写回再读仍是同一档。
// ============================================================

#include <vector>

#include "borealis/config/settings.h"
#include "borealis/ui/settings_form.h"

namespace borealis::config {

/// @brief 把一份配置摊成表单的装载名单。
///
/// 覆盖 `ui::settings_catalog()` 的**全部**行且次序照表（表内次序即面板的排版次序），故
/// `ui::SettingsForm` 装载本件产物时三张分叉名单恒为空；这条覆盖关系由证人守住，新增配置键
/// 而忘记在此加行会在那里转红，而不是让面板静默少画一个控件。
/// @param settings 面板打开时装载的那份配置（通常是 `Store::settings()`）。
/// @return 逐键的装载项，可直接交 `ui::SettingsForm` 的构造。
[[nodiscard]] auto form_entries(const Settings &settings) -> std::vector<ui::FormEntry>;

/// @brief 把表单的当前值写回一份基线配置。
///
/// 取的是表单的**当前值**而非脏键名单：未改的键写回同值，故本件幂等。「要不要落盘」仍归面板
/// （`ui::SettingsForm::has_unsaved_changes()` 与 `Store::replace()`，裁决 7.52 的 S3①），
/// 本件不碰存储也不改 `base`。
/// @param form 面板自持的那份表单。
/// @param base 基线配置，通常是 `Store::settings()`。
/// @return 一份新配置，可直接交 `Store::replace()`。
[[nodiscard]] auto apply_form(const ui::SettingsForm &form, const Settings &base) -> Settings;

}  // namespace borealis::config
