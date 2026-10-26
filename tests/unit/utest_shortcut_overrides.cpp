/// 测试类型: unit
/// 目标单元: src/config/shortcut_overrides.{h,cpp}
/// 测试说明: `SPEC.FEAT.PREF.04` 的装配腿（框架侧任务书 `codespec/FRAMEWORK_TASK_PREF04.md` §5）：
///           启动期把落盘的覆盖表灌进 `CommandRegistry::default_binding`，再由 `bind_shortcuts`
///           统一投影。本套件钉四件事：
///           ① 有效覆盖只换键位——写完后投影，新组合键命中、旧缺省键不再命中，标题与注册次序不动；
///           ② 重放本身**不投影**（投影归装配层一次做），空表即零改动；
///           ③ 畸形串只坏这一条：其余覆盖照旧生效，被拒那条保留缺省绑定（m2 的证人）；
///           ④ 未注册的命令 id 只坏这一条、不 crash，其余覆盖照旧生效（m3 的证人）；
///           ⑤ 旧命令 id 在读侧折成现 id（`SPEC.FEAT.CONN.05` 五期稿 D39③）：折叠本身、表外透传、
///              新旧同现时现 id 胜出，以及「折完之后重放命中新命令、不折就是 unknown_command」这一段
///              端到端——静默失效是本件要防的唯一失败形态。

#include <cstddef>
#include <map>
#include <optional>
#include <string>

#include "aurora/app/shortcuts.h"
#include "aurora/commands.h"
#include "aurora/event/event.h"
#include "config/shortcut_overrides.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_shortcut_overrides {

namespace {

using borealis::config::migrate_shortcut_overrides;
using borealis::config::replay_shortcut_overrides;
using borealis::config::ShortcutReplayReport;

/// @brief 一条按下事件：判据只看「哪个组合键命中」，动作与键码取按下态。
[[nodiscard]] auto key_down(au::KeyCode code, au::ModifierKey modifiers = au::ModifierKey::None)
    -> au::KeyEvent {
    au::KeyEvent event;
    event.key = static_cast<int>(code);
    event.action = au::KeyAction::Down;
    event.modifiers = modifiers;
    return event;
}

/// @brief 注册一条带缺省绑定的命令；动作只计命中次数。
auto add_command(au::CommandRegistry &commands, std::string id, std::string title,
                 std::optional<au::KeyCombo> binding, int &hits) -> void {
    commands.add(au::Command{.id = std::move(id),
                             .title = std::move(title),
                             .category = "test",
                             .action = [&hits]() -> void { ++hits; },
                             .default_binding = std::move(binding)});
}

[[nodiscard]] auto override_of(std::string command, std::string combo)
    -> std::pair<const std::string, std::string> {
    return {std::move(command), std::move(combo)};
}

}  // namespace

AURORA_TEST_CASE(an_override_replaces_only_the_key_combo_and_wins_after_projection) {
    au::CommandRegistry commands;
    int hits = 0;
    add_command(commands, "view.overview", "overview",
                au::KeyCombo{au::ModifierKey::Control, au::KeyCode::O}, hits);
    add_command(commands, "view.other", "other",
                au::KeyCombo{au::ModifierKey::Control, au::KeyCode::K}, hits);

    au::ShortcutRegistry shortcuts;
    const std::map<std::string, std::string> overrides{override_of("view.overview", "Ctrl+Shift+P")};
    const ShortcutReplayReport report = replay_shortcut_overrides(commands, overrides);
    AURORA_TEST_CHECK_EQ(report.applied, std::size_t{1});
    AURORA_TEST_CHECK_EQ(report.unparsable, std::size_t{0});
    AURORA_TEST_CHECK_EQ(report.unknown_command, std::size_t{0});
    // 重放只写 `default_binding`：此刻还没投影，快捷键表仍应是空的（投影归装配层统一做一次）。
    AURORA_TEST_CHECK_EQ(shortcuts.count(), std::size_t{0});

    const au::Command *overridden = commands.find("view.overview");
    AURORA_TEST_REQUIRE_NOT_NULL(overridden);
    AURORA_TEST_CHECK_EQ(overridden->title, std::string{"overview"});  // 元数据不动
    AURORA_TEST_REQUIRE(overridden->default_binding.has_value());
    AURORA_TEST_CHECK_EQ(overridden->default_binding->to_string(), std::string{"Ctrl+Shift+P"});
    // 未被覆盖的那条逐字保留缺省绑定；注册次序也不动。
    const au::Command *untouched = commands.find("view.other");
    AURORA_TEST_REQUIRE_NOT_NULL(untouched);
    AURORA_TEST_REQUIRE(untouched->default_binding.has_value());
    AURORA_TEST_CHECK_EQ(untouched->default_binding->to_string(), std::string{"Ctrl+K"});
    const std::vector<au::Command> all = commands.all();
    AURORA_TEST_REQUIRE_EQ(all.size(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(all[0].id, std::string{"view.overview"});
    AURORA_TEST_CHECK_EQ(all[1].id, std::string{"view.other"});

    commands.bind_shortcuts(shortcuts);
    AURORA_TEST_CHECK(
        shortcuts.handle(key_down(au::KeyCode::P, au::ModifierKey::Control | au::ModifierKey::Shift)));
    AURORA_TEST_CHECK_EQ(hits, 1);
    // 旧缺省组合键在重放里已被换掉，投影后不该再命中。
    AURORA_TEST_CHECK(!shortcuts.handle(key_down(au::KeyCode::O, au::ModifierKey::Control)));
    AURORA_TEST_CHECK_EQ(hits, 1);
    // 另一条命令的缺省键照旧工作，说明重放没有连带清掉别人的投影。
    AURORA_TEST_CHECK(shortcuts.handle(key_down(au::KeyCode::K, au::ModifierKey::Control)));
    AURORA_TEST_CHECK_EQ(hits, 2);
}

AURORA_TEST_CASE(an_empty_table_changes_nothing) {
    au::CommandRegistry commands;
    int hits = 0;
    add_command(commands, "view.overview", "overview",
                au::KeyCombo{au::ModifierKey::Control, au::KeyCode::O}, hits);

    const ShortcutReplayReport report = replay_shortcut_overrides(commands, {});
    AURORA_TEST_CHECK_EQ(report, ShortcutReplayReport{});
    const au::Command *command = commands.find("view.overview");
    AURORA_TEST_REQUIRE_NOT_NULL(command);
    AURORA_TEST_REQUIRE(command->default_binding.has_value());
    AURORA_TEST_CHECK_EQ(command->default_binding->to_string(), std::string{"Ctrl+O"});
}

AURORA_TEST_CASE(a_malformed_combo_only_skips_that_entry) {
    au::CommandRegistry commands;
    int hits = 0;
    add_command(commands, "view.overview", "overview",
                au::KeyCombo{au::ModifierKey::Control, au::KeyCode::O}, hits);
    add_command(commands, "view.other", "other",
                au::KeyCombo{au::ModifierKey::Control, au::KeyCode::K}, hits);

    au::ShortcutRegistry shortcuts;
    const std::map<std::string, std::string> overrides{override_of("view.overview", "Ctrl+"),
                                                       override_of("view.other", "Alt+X")};
    const ShortcutReplayReport report = replay_shortcut_overrides(commands, overrides);
    AURORA_TEST_CHECK_EQ(report.applied, std::size_t{1});
    AURORA_TEST_CHECK_EQ(report.unparsable, std::size_t{1});
    AURORA_TEST_CHECK_EQ(report.unknown_command, std::size_t{0});
    // 被拒那条保留缺省绑定，另一条照旧生效。
    const au::Command *rejected = commands.find("view.overview");
    AURORA_TEST_REQUIRE_NOT_NULL(rejected);
    AURORA_TEST_REQUIRE(rejected->default_binding.has_value());
    AURORA_TEST_CHECK_EQ(rejected->default_binding->to_string(), std::string{"Ctrl+O"});
    const au::Command *accepted = commands.find("view.other");
    AURORA_TEST_REQUIRE_NOT_NULL(accepted);
    AURORA_TEST_REQUIRE(accepted->default_binding.has_value());
    AURORA_TEST_CHECK_EQ(accepted->default_binding->to_string(), std::string{"Alt+X"});

    commands.bind_shortcuts(shortcuts);
    AURORA_TEST_CHECK(shortcuts.handle(key_down(au::KeyCode::X, au::ModifierKey::Alt)));
    AURORA_TEST_CHECK_EQ(hits, 1);
    AURORA_TEST_CHECK(shortcuts.handle(key_down(au::KeyCode::O, au::ModifierKey::Control)));
    AURORA_TEST_CHECK_EQ(hits, 2);
}

AURORA_TEST_CASE(an_unknown_command_id_only_skips_that_entry) {
    au::CommandRegistry commands;
    int hits = 0;
    add_command(commands, "view.other", "other",
                au::KeyCombo{au::ModifierKey::Control, au::KeyCode::K}, hits);

    const std::map<std::string, std::string> overrides{override_of("view.missing", "Alt+X"),
                                                       override_of("view.other", "Alt+K")};
    const ShortcutReplayReport report = replay_shortcut_overrides(commands, overrides);
    AURORA_TEST_CHECK_EQ(report.applied, std::size_t{1});
    AURORA_TEST_CHECK_EQ(report.unparsable, std::size_t{0});
    AURORA_TEST_CHECK_EQ(report.unknown_command, std::size_t{1});
    AURORA_TEST_CHECK_EQ(commands.count(), std::size_t{1});  // 未知 id 不入表
    AURORA_TEST_CHECK_NULL(commands.find("view.missing"));
    const au::Command *accepted = commands.find("view.other");
    AURORA_TEST_REQUIRE_NOT_NULL(accepted);
    AURORA_TEST_REQUIRE(accepted->default_binding.has_value());
    AURORA_TEST_CHECK_EQ(accepted->default_binding->to_string(), std::string{"Alt+K"});
}

AURORA_TEST_CASE(a_legacy_command_id_folds_onto_the_current_one) {
    const std::map<std::string, std::string> stored{override_of("serial.monitor.toggle", "Ctrl+Alt+M")};
    const std::map<std::string, std::string> migrated = migrate_shortcut_overrides(stored);

    // 只换键、不换值：组合键文本逐字节是用户写下的那一份。
    AURORA_TEST_REQUIRE_EQ(migrated.size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(migrated.count("serial.monitor.toggle"), std::size_t{0});
    AURORA_TEST_CHECK_EQ(migrated.count("session.monitor.toggle"), std::size_t{1});
    const auto found = migrated.find("session.monitor.toggle");
    AURORA_TEST_REQUIRE(found != migrated.end());
    AURORA_TEST_CHECK_EQ(found->second, std::string{"Ctrl+Alt+M"});
    // 入参不动：折叠产出一张新表，读侧之外没人被顺手改到。
    AURORA_TEST_CHECK_EQ(stored.size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(stored.count("serial.monitor.toggle"), std::size_t{1});
}

AURORA_TEST_CASE(ids_outside_the_table_pass_straight_through) {
    const std::map<std::string, std::string> stored{override_of("app.settings", "Ctrl+Shift+Comma"),
                                                   override_of("terminal.new_tab", "Ctrl+T"),
                                                   override_of("view.missing", "Alt+X")};
    // 表外 id 原样透传，连「未注册」那条判断都不做——那是重放侧 `unknown_command` 的账。
    AURORA_TEST_CHECK(migrate_shortcut_overrides(stored) == stored);
    AURORA_TEST_CHECK(migrate_shortcut_overrides({}).empty());
}

AURORA_TEST_CASE(the_current_id_wins_when_both_are_present) {
    const std::map<std::string, std::string> stored{override_of("serial.monitor.toggle", "Ctrl+Alt+M"),
                                                   override_of("session.monitor.toggle", "Ctrl+Alt+N")};
    const std::map<std::string, std::string> migrated = migrate_shortcut_overrides(stored);

    // 用户既然显式写过现名，那一行就是当下意图；旧行整条丢弃而不是覆盖新行。
    AURORA_TEST_REQUIRE_EQ(migrated.size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(migrated.count("serial.monitor.toggle"), std::size_t{0});
    const auto found = migrated.find("session.monitor.toggle");
    AURORA_TEST_REQUIRE(found != migrated.end());
    AURORA_TEST_CHECK_EQ(found->second, std::string{"Ctrl+Alt+N"});
}

AURORA_TEST_CASE(a_folded_override_reaches_the_registry_the_raw_one_does_not) {
    int hits = 0;
    const std::map<std::string, std::string> stored{override_of("serial.monitor.toggle", "Ctrl+Alt+M")};

    {
        // 不折叠的那条路：命令注册后 id 已是现名，旧行折进重放就是 `unknown_command` —— 键位留在配置
        // 里、什么也不再触发，且只读表把它画成孤儿行。这条断言是折叠的存在理由，不是冗余。
        au::CommandRegistry unmigrated;
        add_command(unmigrated, "session.monitor.toggle", "Byte monitor",
                    au::KeyCombo{au::ModifierKey::Control | au::ModifierKey::Shift, au::KeyCode::M}, hits);
        AURORA_TEST_CHECK_EQ(replay_shortcut_overrides(unmigrated, stored).unknown_command, std::size_t{1});
    }

    au::CommandRegistry commands;
    add_command(commands, "session.monitor.toggle", "Byte monitor",
                au::KeyCombo{au::ModifierKey::Control | au::ModifierKey::Shift, au::KeyCode::M}, hits);
    const ShortcutReplayReport report =
        replay_shortcut_overrides(commands, migrate_shortcut_overrides(stored));
    AURORA_TEST_CHECK_EQ(report.applied, std::size_t{1});
    AURORA_TEST_CHECK_EQ(report.unknown_command, std::size_t{0});

    au::ShortcutRegistry shortcuts;
    commands.bind_shortcuts(shortcuts);
    AURORA_TEST_CHECK(shortcuts.handle(key_down(au::KeyCode::M,
                                                au::ModifierKey::Control | au::ModifierKey::Alt)));
    AURORA_TEST_CHECK_EQ(hits, 1);
    AURORA_TEST_CHECK(!shortcuts.handle(key_down(au::KeyCode::M,
                                                 au::ModifierKey::Control | au::ModifierKey::Shift)));
    AURORA_TEST_CHECK_EQ(hits, 1);
}

}  // namespace borealis::test_cases::utest_shortcut_overrides
