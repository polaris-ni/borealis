#pragma once

// ============================================================
// 快捷键只读表的行源与冲突比对（include/borealis/ui/shortcuts_table.h）
// ------------------------------------------------------------
// `SPEC.FEAT.PREF.02` 的 D 页（判据文 `codespec/UI_SETTINGS.draft.md` §4 D1-a / D2-a / D3-a）里与
// widget 无关的那半：表里有哪些行、每行的「当前组合键」是什么、哪几行撞了同一个键位。首版**只读**
// （裁决 7.52 的 S9）——重绑需要键位录制件与 `shortcuts.overrides` 的消费方，两者都不在本棒射程内。
//
// 三条决定形态的口径（人已拍板，裁决 7.72）：
// - **行源是注册表而不是覆盖表**：注册行逐字来自框架 `CommandRegistry::all()`（装配层折成
//   `ShortcutCommandEntry` 交进来），覆盖表只贡献**孤儿行**——即配置里写了命令 id 而注册表没有的那些。
//   理由与 7.27② 同源：那张表当下无消费方，故「当前组合键」的实际值只可能来自注册表；把一条未生效的
//   覆盖画成一行就是谎报生效，而把已注册命令的覆盖条目**并进**同一行又需要「谁覆盖谁」的第二份状态。
// - **冲突比对吃 `term::KeyPress` 而不是文本**：文本形态（`Ctrl+Shift+D`）由框架 `KeyCombo::to_string()`
//   生成、只用于显示，公共面**没有**它的反向解析（`key_name` 是单向表）。若在界面上比字符串，就必须
//   在本仓自造一张键名反查表——那是第二真值源，而比键位语义不需要它。
//   孤儿行因此**不参与**比对（它们只有文本），这一点在表里如实呈现为「延后」而不是「无冲突」。
// - **保留位含本仓十二条分屏键位**：那些键位不经 `ShortcutRegistry`（`ui/workspace_keys.h` 文件头），
//   而同一个组合键在派发上是快捷键层先消费（裁决 7.51③ 理由 (a)）。于是一条命令若与分屏键位同形，
//   实际生效的是快捷键而分屏命令按不到——这一格必须标冲突，不能留给用户自己去试。
//   比对的修饰位与 `workspace_command` / 框架 `KeyCombo::matches` 逐位一致：只取 Shift / Ctrl / Alt /
//   Meta 四位，键盘锁定态位不参与（裁决 7.51①）。
// ============================================================

#include <optional>
#include <string>
#include <vector>

#include "borealis/term/keymap.h"
#include "borealis/ui/settings_form.h"

namespace borealis::ui {

/// @brief 注册表交进来的一条命令（装配层从框架 `Command` 折算，本件因此不含 Aurora 类型）。
///
/// `binding_text` 与 `binding` 是**同一份绑定的两个形态**：前者照框架 `KeyCombo::to_string()` 供显示，
/// 后者供比对。它们必须由装配层从同一个 `KeyCombo` 同行取出——分成两次取就是让显示串与判据值各走
/// 一条路，而 D2-a 判的正是「标注来自实际比对」。
struct ShortcutCommandEntry {
    std::string command{};        ///< 命令 id（一律 ASCII 英文，裁决 7.25⑬ N8）。
    std::string title{};          ///< 动作名；注册时已走本仓词条表，面板不再查第二遍（D1-a）。
    std::string category{};       ///< 分组，逐字取注册时的 `category`。
    std::string binding_text{};   ///< 「当前组合键」列的文本；未绑定为空串。
    std::optional<term::KeyPress> binding{};  ///< 与 @p binding_text 同源的比对值；未绑定为空。
};

/// @brief 只读表里的一行（比对之后，面板按本结构逐行落笔）。
struct ShortcutTableRow {
    std::string command{};        ///< 命令 id。
    std::string title{};          ///< 动作名。
    std::string category{};       ///< 分组。
    std::string binding_text{};   ///< 「当前组合键」列文本；未绑定为空串。
    bool registered{};            ///< false＝覆盖表里的孤儿命令 id（注册表查无此命令），界面挂「延后」角标。
    bool comparable{};            ///< 该行有可比对的**键位语义**值；孤儿行只有文本，故恒 false。
    std::vector<std::string> conflicts_with{};  ///< 表内同组合键的其他行命令 id，次序＝行序。
    bool conflicts_with_workspace{};            ///< 与本仓分屏键位（`ui::workspace_key_bindings()`）同形。

    /// @brief 这一行是否需要冲突标注。
    [[nodiscard]] auto has_conflict() const noexcept -> bool {
        return !conflicts_with.empty() || conflicts_with_workspace;
    }
};

/// @brief 把注册表与覆盖表折成只读表的行，并完成两两冲突标注。
///
/// 纯函数：不持时钟、不触达框架，故三列内容与冲突判据都能脱离界面断言。
/// @param commands 注册表的全部命令，次序即框架的注册序（面板不再自排一次，否则界面上看到的次序
///                 与注册顺序无关，而 `SPEC.FEAT.PREF.04` 的默认键位表是按功能分组的）。
/// @param overrides 表单里那份覆盖表（落盘形态 `[{command, combo}, ...]`，裁决 7.27②）。只有
///                  **注册表查无此 id** 的条目产生行，且一律排在注册行之后、按覆盖表自身次序。
/// @param workspace_bindings 本仓保留的分屏键位；比对只取四个可按修饰位。
/// @return 表行：注册行在前（原序），孤儿行在后（覆盖表序）。
[[nodiscard]] auto build_shortcut_rows(std::vector<ShortcutCommandEntry> commands,
                                       const std::vector<ShortcutOverride> &overrides,
                                       const std::vector<term::KeyPress> &workspace_bindings)
    -> std::vector<ShortcutTableRow>;

}  // namespace borealis::ui
