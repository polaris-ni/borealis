// ============================================================
// 启动重放快捷键覆盖表的实现（src/config/shortcut_overrides.cpp）
// ------------------------------------------------------------
// 判据与形态口径见头文件横幅。这里只做「逐条解析 → 写绑定」，投影归装配层统一一次。
// ============================================================

#include "config/shortcut_overrides.h"

#include <optional>

#include "aurora/core/log.h"

namespace borealis::config {

auto replay_shortcut_overrides(au::CommandRegistry &commands,
                               const std::map<std::string, std::string> &overrides)
    -> ShortcutReplayReport {
    ShortcutReplayReport report;
    for (const auto &[command_id, combo_text] : overrides) {
        const std::optional<au::KeyCombo> combo = au::KeyCombo::from(combo_text);
        if (!combo.has_value()) {
            AURORA_LOG_WARN("config", "shortcut override skipped, unparsable combo: '", command_id, "' = '",
                            combo_text, "'");
            ++report.unparsable;
            continue;
        }
        if (!commands.set_binding(command_id, *combo)) {
            AURORA_LOG_WARN("config", "shortcut override skipped, unknown command: '", command_id, "'");
            ++report.unknown_command;
            continue;
        }
        ++report.applied;
    }
    return report;
}

}  // namespace borealis::config
