/// 测试类型: unit
/// 目标单元: include/borealis/ui/shortcuts_table.h + src/ui/shortcuts_table.cpp
/// 测试说明: 快捷键只读表的行源与冲突比对（`SPEC.FEAT.PREF.02` 的 D 页，裁决 7.72 的②③）。
///           断三族事实：行的**来源与次序**（注册行原序在前、孤儿覆盖行在后，已注册命令的覆盖条目
///           既不产生行也不改写那一行）；**比对在键位语义域而不在文本域**（孤儿行只有文本故恒不判，
///           修饰逐位相等、锁定态位不参与）；**冲突标注是表内两两与工作区保留位两半的并**。

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "borealis/term/keymap.h"
#include "borealis/ui/shortcuts_table.h"
#include "borealis/ui/workspace_keys.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_shortcuts_table {

namespace {

using borealis::term::KeyPress;
using borealis::term::KeySym;
using borealis::ui::build_shortcut_rows;
using borealis::ui::ShortcutCommandEntry;
using borealis::ui::ShortcutOverride;
using borealis::ui::ShortcutTableRow;

/// @brief 一条键位的简写，形态照 `utest_workspace_keys`：修饰缺省即「没按」。
[[nodiscard]] constexpr auto key(KeySym sym, bool shift = false, bool control = false, bool alt = false,
                                 bool meta = false, bool num_lock = false) -> KeyPress {
    return KeyPress{.sym = sym,
                    .shift = shift,
                    .control = control,
                    .alt = alt,
                    .meta = meta,
                    .num_lock = num_lock};
}

/// @brief 注册表里的一条命令：文本与比对值一律由装配层从同一个 `KeyCombo` 同行取出，本件只照收。
///
/// 动作名由命令 id 派生（与孤儿行「动作名列取命令 id」同形态），故本件按 id 取行时两处一致。
[[nodiscard]] auto entry(std::string id, KeySym sym, bool shift = false, bool control = false,
                         bool alt = false, bool num_lock = false) -> ShortcutCommandEntry {
    ShortcutCommandEntry made{};
    made.command = std::move(id);
    made.title = "title-of-" + made.command;
    made.category = "category";
    made.binding_text = "Ctrl+K";
    made.binding = key(sym, shift, control, alt, false, num_lock);
    return made;
}

/// @brief 未绑定的注册命令：`binding_text` 与 `binding` 同时为空，正是框架 `Command` 的那一态。
[[nodiscard]] auto unbound(std::string id) -> ShortcutCommandEntry {
    return ShortcutCommandEntry{.command = std::move(id), .title = "t", .category = "c"};
}

/// @brief 覆盖表的一条（落盘形态 `[{command, combo}, ...]`）。
[[nodiscard]] auto override_row(std::string id, std::string combo) -> ShortcutOverride {
    return ShortcutOverride{.command = std::move(id), .combo = std::move(combo)};
}

/// @brief 按命令 id 取行；未命中即失败，免得用下标写判据时把「行没进来」读成「行的内容不对」。
[[nodiscard]] auto row_of(const std::vector<ShortcutTableRow> &rows, const std::string &id)
    -> const ShortcutTableRow & {
    for (const ShortcutTableRow &row : rows) {
        if (row.command == id) {
            return row;
        }
    }
    AURORA_TEST_FAIL_FATAL("no row named " + id);
    static const ShortcutTableRow kMissing{};
    return kMissing;
}

/// @brief 工作区保留位折成比对输入（跨件判据：那张表新增一键位，本套件的分屏冲突例就跟着现形）。
[[nodiscard]] auto workspace_presses() -> std::vector<KeyPress> {
    std::vector<KeyPress> presses;
    for (const ui::WorkspaceKeyBinding &binding : ui::workspace_key_bindings()) {
        presses.push_back(binding.press);
    }
    return presses;
}

}  // namespace

AURORA_TEST_CASE(nothing_registered_gives_an_empty_table) {
    AURORA_TEST_CHECK_EQ(build_shortcut_rows({}, {}, {}).size(), 0U);
}

AURORA_TEST_CASE(registered_rows_keep_the_registry_order_and_its_three_columns) {
    const auto rows = build_shortcut_rows(
        std::vector<ShortcutCommandEntry>{
            ShortcutCommandEntry{.command = "file.open", .title = "打开文件", .category = "文件"},
            entry("settings.open", KeySym::Comma, true, true),
        },
        {}, {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    // 面板不自排第二次：次序逐字照注册序（裁决 7.72 的②）。
    AURORA_TEST_CHECK_EQ(rows[0].command, "file.open");
    AURORA_TEST_CHECK_EQ(rows[1].command, "settings.open");
    AURORA_TEST_CHECK_EQ(rows[1].title, "title-of-settings.open");
    AURORA_TEST_CHECK_EQ(rows[1].category, "category");
    AURORA_TEST_CHECK_EQ(rows[1].binding_text, "Ctrl+K");
    AURORA_TEST_CHECK_TRUE(rows[1].registered);
    AURORA_TEST_CHECK_TRUE(rows[1].comparable);
}

AURORA_TEST_CASE(unbound_commands_are_listed_but_take_no_part_in_comparisons) {
    const auto rows = build_shortcut_rows(
        std::vector<ShortcutCommandEntry>{unbound("a.no.binding"), unbound("b.no.binding")}, {}, {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    // 两条都没有绑定 ≠ 两条撞在一起：未绑定那一格显示空，且不进比对集合。
    for (const ShortcutTableRow &row : rows) {
        AURORA_TEST_CHECK_TRUE(row.registered);
        AURORA_TEST_CHECK_FALSE(row.comparable);
        AURORA_TEST_CHECK_EQ(row.binding_text, "");
        AURORA_TEST_CHECK_FALSE(row.has_conflict());
    }
}

AURORA_TEST_CASE(orphan_overrides_append_after_the_registered_rows) {
    const auto rows = build_shortcut_rows(std::vector<ShortcutCommandEntry>{unbound("file.open")},
                                          std::vector<ShortcutOverride>{
                                              override_row("edit.find", "Ctrl+F"),
                                              override_row("pane.zoom", "Ctrl+Shift+Enter"),
                                          },
                                          {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);
    AURORA_TEST_CHECK_EQ(rows[0].command, "file.open");
    AURORA_TEST_CHECK_TRUE(rows[0].registered);
    // 孤儿行按覆盖表自身次序排在注册行之后，动作名只能取命令 id（没有注册条目可取）。
    AURORA_TEST_CHECK_EQ(rows[1].command, "edit.find");
    AURORA_TEST_CHECK_EQ(rows[1].title, "edit.find");
    AURORA_TEST_CHECK_EQ(rows[1].binding_text, "Ctrl+F");
    AURORA_TEST_CHECK_FALSE(rows[1].registered);
    AURORA_TEST_CHECK_EQ(rows[2].command, "pane.zoom");
    AURORA_TEST_CHECK_EQ(rows[2].binding_text, "Ctrl+Shift+Enter");
}

AURORA_TEST_CASE(an_override_for_a_registered_command_neither_adds_a_row_nor_shifts_it) {
    // 那张覆盖表当下无消费方，实际生效的组合键仍是注册表里的那一份（文件头第二条口径）。画成一行、
    // 或让它盖掉注册行的读数，都是把「用户希望生效」当成「当前生效」。
    auto registered = entry("settings.open", KeySym::Comma, true, true);
    registered.binding_text = "Ctrl+,";
    const auto rows = build_shortcut_rows(std::vector<ShortcutCommandEntry>{registered},
                                          std::vector<ShortcutOverride>{
                                              override_row("settings.open", "Ctrl+."),
                                              override_row("settings.open", "Ctrl+."),  // 同一条写两次
                                          },
                                          {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 1U);
    AURORA_TEST_CHECK_EQ(rows[0].binding_text, "Ctrl+,");
    AURORA_TEST_CHECK_TRUE(rows[0].registered);
}

AURORA_TEST_CASE(two_commands_on_the_same_binding_mark_each_other) {
    const auto rows = build_shortcut_rows(
        std::vector<ShortcutCommandEntry>{entry("a", KeySym::K, true, true), entry("b", KeySym::K, true, true)},
        {}, {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_REQUIRE_EQ(rows[0].conflicts_with.size(), 1U);
    AURORA_TEST_CHECK_EQ(rows[0].conflicts_with[0], "b");
    // 标注是对称的：只标其中一行会让用户看到「a 撞了、b 没撞」。
    AURORA_TEST_REQUIRE_EQ(rows[1].conflicts_with.size(), 1U);
    AURORA_TEST_CHECK_EQ(rows[1].conflicts_with[0], "a");
    AURORA_TEST_CHECK_TRUE(rows[0].has_conflict());
}

AURORA_TEST_CASE(a_three_way_collision_lists_the_others_in_table_order) {
    const auto rows = build_shortcut_rows(
        std::vector<ShortcutCommandEntry>{
            entry("mid", KeySym::G, true, true),
            entry("first", KeySym::G, true, true),
            unbound("unrelated"),
            entry("last", KeySym::G, true, true),
        },
        {}, {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 4U);
    const auto &middle = row_of(rows, "mid");
    AURORA_TEST_REQUIRE_EQ(middle.conflicts_with.size(), 2U);
    AURORA_TEST_CHECK_EQ(middle.conflicts_with[0], "first");
    AURORA_TEST_CHECK_EQ(middle.conflicts_with[1], "last");
    AURORA_TEST_CHECK_EQ(row_of(rows, "last").conflicts_with.size(), 2U);
    AURORA_TEST_CHECK_FALSE(row_of(rows, "unrelated").has_conflict());
}

AURORA_TEST_CASE(an_extra_modifier_is_a_different_binding) {
    // 逐位相等而非「包含」，与 `workspace_command` 及框架 `KeyCombo::matches` 同一口径（裁决 7.48② /
    // 7.51①）：`Ctrl+K` 与 `Ctrl+Shift+K` 是两个动作，标成冲突就是报出派发上并不存在的碰撞。
    const auto rows = build_shortcut_rows(
        std::vector<ShortcutCommandEntry>{entry("plain", KeySym::K, false, true),
                                         entry("shifted", KeySym::K, true, true)},
        {}, {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK_FALSE(rows[0].has_conflict());
    AURORA_TEST_CHECK_FALSE(rows[1].has_conflict());
}

AURORA_TEST_CASE(num_lock_state_is_not_a_distinction) {
    // 反向同理：两条只在 NumLock 上不同的绑定必须判成同一个键位，否则表上会列出派发上抓不到的冲突。
    const auto rows = build_shortcut_rows(
        std::vector<ShortcutCommandEntry>{entry("off", KeySym::K, false, true),
                                         entry("on", KeySym::K, false, true, false, true)},
        {}, {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    // 先 REQUIRE 大小再索引：`conflicts_with[0]` 越界在 Debug 档是一次 fastfail 崩溃，那会把「变异被抓到」
    // 的读数从一条指名用例变成一个退出码（与裁决 7.54 那条「行数判据须先 REQUIRE 再逐格比」同口径）。
    AURORA_TEST_REQUIRE_EQ(rows[0].conflicts_with.size(), 1U);
    AURORA_TEST_REQUIRE_EQ(rows[1].conflicts_with.size(), 1U);
    AURORA_TEST_CHECK_EQ(rows[0].conflicts_with[0], "on");
    AURORA_TEST_CHECK_EQ(rows[1].conflicts_with[0], "off");
}

AURORA_TEST_CASE(orphan_rows_are_presented_as_deferred_instead_of_conflict_free) {
    // 孤儿行**不参与**比对是刻意的：组合键文本没有公共的反向解析（`key_name` 单向），比字符串就等于
    // 在本仓自造第二张键名表。故它在表里以「延后」呈现，而不是被算成「与注册行不冲突」。
    const auto rows = build_shortcut_rows(std::vector<ShortcutCommandEntry>{entry("file.open", KeySym::O, false, true)},
                                          std::vector<ShortcutOverride>{override_row("edit.find", "Ctrl+O")}, {});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK_FALSE(row_of(rows, "file.open").has_conflict());
    AURORA_TEST_CHECK_FALSE(row_of(rows, "edit.find").has_conflict());
    AURORA_TEST_CHECK_FALSE(row_of(rows, "edit.find").comparable);
}

AURORA_TEST_CASE(workspace_bindings_are_reserved_slots) {
    // 分屏那十二条不经 `ShortcutRegistry`，而同一个组合键在派发上是快捷键层先消费（裁决 7.51③ 理由 (a)）。
    // 于是「命令撞上分屏键位」的实际情况是快捷键生效、分屏命令按不到——这一格必须标出来。
    const auto rows = build_shortcut_rows(
        std::vector<ShortcutCommandEntry>{
            entry("workspace.split.right", KeySym::D, true, true),   // Ctrl+Shift+D ＝ SplitRight
            entry("settings.open", KeySym::Comma, true, true),       // Ctrl+, ＝ 保留位之外
        },
        {}, workspace_presses());
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK_TRUE(rows[0].conflicts_with_workspace);
    AURORA_TEST_CHECK_TRUE(rows[0].has_conflict());
    AURORA_TEST_CHECK(rows[0].conflicts_with.empty());  // 表内没有同行者，冲突只来自保留位
    AURORA_TEST_CHECK_FALSE(rows[1].conflicts_with_workspace);
    AURORA_TEST_CHECK_FALSE(rows[1].has_conflict());
}

AURORA_TEST_CASE(has_conflict_is_the_union_of_table_and_workspace_halves) {
    // 一条命令同时撞表内与撞保留位时，两半各自独立成字段：界面据此才能分清「与另一条命令同键」和
    // 「与分屏键位同键」两种提示文案。
    const auto rows = build_shortcut_rows(
        std::vector<ShortcutCommandEntry>{
            entry("first", KeySym::E, true, true),   // Ctrl+Shift+E ＝ SplitDown（保留位）
            entry("second", KeySym::E, true, true),  // 与 first 同键
        },
        {}, workspace_presses());
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    for (const ShortcutTableRow &row : rows) {
        AURORA_TEST_CHECK_TRUE(row.conflicts_with_workspace);
        AURORA_TEST_REQUIRE_EQ(row.conflicts_with.size(), 1U);
        AURORA_TEST_CHECK_TRUE(row.has_conflict());
    }
}

}  // namespace borealis::test_cases::utest_shortcuts_table
