#pragma once

// ============================================================
// 启动重放快捷键覆盖表（src/config/shortcut_overrides.h，私有头）
// ------------------------------------------------------------
// `SPEC.FEAT.PREF.04` 的装配腿（框架侧任务书 `codespec/FRAMEWORK_TASK_PREF04.md` §5）：把
// `config::ShortcutsSettings::overrides`（命令 id → `KeyCombo::to_string()` 形态的组合键文本）
// 灌进 `CommandRegistry`，随后由 `bind_shortcuts` 一次性投影进 `ShortcutRegistry`。
//
// 口径三条：
// - **必须排在投影之前**：`set_binding` 在注册表尚未投影时只改 `default_binding`；反过来写会先按
//   缺省绑定投影一遍再逐条撤建，多一轮且多一处可解释状态（框架两条路径语义等价）。
// - **一条坏条目只坏这一条**：畸形串与未注册 id 各留一条 warn 后跳过，其余覆盖照旧生效——配置是
//   用户可手改的文件，一条写错不该拦住启动，也不该静默失效。
// - **解析只走框架公共面**：文本 → 键位的反向解析用 `KeyCombo::from`（与 `to_string()` 同源），
//   本仓不自造第二真值源（裁决 7.72、AGENTS.md §5 第 2 条）。
//
// 线程：只在启动期主线程用一次（装配层在 `bind_shortcuts` 之前调用）。
// ============================================================

#include <cstddef>
#include <map>
#include <string>

#include "aurora/commands.h"

namespace au = aurora;

namespace borealis::config {

/// @brief 一次重放的结果：三类条目各自的条数（测试读它，日志逐条留痕）。
struct ShortcutReplayReport {
    std::size_t applied = 0;          ///< 解析成功且命令 id 已注册，已写入 `default_binding`。
    std::size_t unparsable = 0;       ///< 组合键文本畸形，跳过。
    std::size_t unknown_command = 0;  ///< 命令 id 未注册，跳过。

    [[nodiscard]] auto operator==(const ShortcutReplayReport &) const noexcept -> bool = default;
};

/// @brief 把覆盖表逐条写进命令注册表的 `default_binding`。
///
/// 覆盖条目**不改**命令的其余字段与注册次序（框架 `set_binding` 的契约），因此重放只影响键位。
/// 本件不调用 `bind_shortcuts`——投影由装配层在全部覆盖写完之后统一做一次。
/// @param commands 待重放的命令注册表（调用时尚未投影，或投影后由框架先行撤建）。
/// @param overrides 落盘形态的命令 id → 组合键文本；空表即什么都不做。
/// @return 三类条目的条数；跳过项各留一条 `AURORA_LOG_WARN`。
auto replay_shortcut_overrides(au::CommandRegistry &commands,
                               const std::map<std::string, std::string> &overrides)
    -> ShortcutReplayReport;

}  // namespace borealis::config
