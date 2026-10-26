// ============================================================
// 启动重放快捷键覆盖表与旧命令 id 折叠的实现（src/config/shortcut_overrides.cpp）
// ------------------------------------------------------------
// 判据与形态口径见头文件横幅。重放只做「逐条解析 → 写绑定」，投影归装配层统一一次；折叠只做
// 「逐条换键」，读写与展示都在它下游，故全仓只有读侧那一个调用点。
// ============================================================

#include "config/shortcut_overrides.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include "aurora/core/log.h"

namespace borealis::config {
namespace {

/// @brief 一次性改名表（旧 id → 现 id），本表就是命令 id 改名的唯一落点。
///
/// 只列**曾经上屏过**的 id；条目随框架侧确认「不再有任何用户的配置文件里还剩旧 id」后整表删除——
/// 留着不会出错（表外 id 原样透传），删早了才会。
constexpr std::array<std::pair<std::string_view, std::string_view>, 1> kLegacyCommandIds{{
    // `SPEC.FEAT.CONN.05` 五期（稿 D39③）：监视卡从串口一型扩到四型，命令随之改名。
    {"serial.monitor.toggle", "session.monitor.toggle"},
}};

/// @brief 旧 id 的现名；表外原样返回空。
[[nodiscard]] auto current_command_id(std::string_view command_id) -> std::string_view {
    for (const auto &[legacy, current] : kLegacyCommandIds) {
        if (command_id == legacy) {
            return current;
        }
    }
    return {};
}

}  // namespace

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

auto migrate_shortcut_overrides(const std::map<std::string, std::string> &stored)
    -> std::map<std::string, std::string> {
    std::map<std::string, std::string> migrated;
    // 两趟而不是就地改键：一趟就会让「旧行先落键、现行后覆盖」与「现行先落键、旧行被拒」两种次序
    // 都出现过，而冲突那一条判据要的是**与表内次序无关**的确定答案——现 id 恒胜。
    for (const auto &[command_id, combo_text] : stored) {
        if (current_command_id(command_id).empty()) {
            migrated.emplace(command_id, combo_text);
        }
    }
    for (const auto &[command_id, combo_text] : stored) {
        const std::string_view current = current_command_id(command_id);
        if (current.empty()) {
            continue;
        }
        if (migrated.contains(std::string{current})) {
            AURORA_LOG_WARN("config", "legacy shortcut override dropped, current id already present: '",
                            command_id, "' -> '", current, "'");
            continue;
        }
        migrated.emplace(std::string{current}, combo_text);
        AURORA_LOG_INFO("config", "legacy shortcut override renamed: '", command_id, "' -> '", current, "'");
    }
    return migrated;
}

}  // namespace borealis::config
