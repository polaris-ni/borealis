#include "borealis/ui/shortcuts_table.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace borealis::ui {

namespace {

/// @brief 两条键位是否同形：只比主键与**四个可按住的**修饰位。
///
/// `num_lock` 不参与，与 `ui::workspace_command` 及框架 `KeyCombo::matches` 同一口径（裁决 7.51①）——
/// 三处任一改成「整字节相等」都会让快捷键表报出派发上并不存在的冲突。
[[nodiscard]] auto same_binding(const term::KeyPress &a, const term::KeyPress &b) noexcept -> bool {
    return a.sym == b.sym && a.shift == b.shift && a.control == b.control && a.alt == b.alt && a.meta == b.meta;
}

}  // namespace

auto build_shortcut_rows(std::vector<ShortcutCommandEntry> commands,
                         const std::vector<ShortcutOverride> &overrides,
                         const std::vector<term::KeyPress> &workspace_bindings)
    -> std::vector<ShortcutTableRow> {
    std::vector<ShortcutTableRow> rows;
    // 比对值与行表平行存放：行的公共形态只给显示用的文本，把 `term::KeyPress` 也放进去就等于让面板
    // 有能力自己再比一次，而「标注来自实际比对」（D2-a）的唯一证人就是面板只读得到算好的那一列。
    std::vector<std::optional<term::KeyPress>> presses;
    rows.reserve(commands.size() + overrides.size());
    presses.reserve(commands.size() + overrides.size());

    for (ShortcutCommandEntry &entry : commands) {
        const bool comparable = entry.binding.has_value();
        rows.push_back(ShortcutTableRow{
            .command = std::move(entry.command),
            .title = std::move(entry.title),
            .category = std::move(entry.category),
            .binding_text = std::move(entry.binding_text),
            .registered = true,
            .comparable = comparable,
        });
        presses.push_back(std::move(entry.binding));
    }

    for (const ShortcutOverride &override : overrides) {
        // 已注册命令的覆盖条目**不产生行也不改写那一行**：那张表当下无消费方，实际生效的组合键仍是
        // 注册表里的 `default_binding`（文件头第二条口径）。把它画成一行、或让它盖掉注册行的读数，
        // 都是把「用户希望生效的值」当成「当前生效的值」。
        const auto same_command = [&override](const ShortcutTableRow &row) -> bool {
            return row.command == override.command;
        };
        if (std::any_of(rows.begin(), rows.end(), same_command)) {
            continue;  // 含「同一条覆盖写了两次」：两个同名同键位的行会被读成两条命令。
        }
        rows.push_back(ShortcutTableRow{
            .command = override.command,
            // 孤儿行没有注册条目可取动作名，就把命令 id 原样摆在动作名列——那是该行唯一读得到的标识。
            .title = override.command,
            .category = {},
            .binding_text = override.combo,
            .registered = false,
            .comparable = false,
        });
        presses.push_back(std::nullopt);
    }

    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!presses[i].has_value()) {
            continue;
        }
        for (std::size_t j = i + 1U; j < rows.size(); ++j) {
            if (presses[j].has_value() && same_binding(*presses[i], *presses[j])) {
                rows[i].conflicts_with.push_back(rows[j].command);
                rows[j].conflicts_with.push_back(rows[i].command);
            }
        }
        rows[i].conflicts_with_workspace =
            std::any_of(workspace_bindings.begin(), workspace_bindings.end(),
                        [&press = *presses[i]](const term::KeyPress &reserved) -> bool {
                            return same_binding(press, reserved);
                        });
    }
    return rows;
}

}  // namespace borealis::ui
