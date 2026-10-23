#pragma once

// ============================================================
// 崩溃处理腿接口（src/platform/crash_handler.h）
// ------------------------------------------------------------
// `SPEC.NF.RELI.01` 的崩溃留存腿（裁决 7.142）：把 `diag::emit_crash_report()` 接到平台
// 的致命信号 / 未处理异常上。选型是**自写诊断快照**而不是全内存 dump——dump 会把整段
// 会话内存（可能含用户口令与私钥口令）原样落盘，绕过会话日志默认关闭的裁决 7.9。
//
// 平台知识不外溢（AGENTS.md §4.5 第 23 条）：POSIX 的 sigaction/sigaltstack 与 Win32 的
// SetUnhandledExceptionFilter 只在 src/platform/{posix,win}/crash_handler.cpp。
//
// 线程口径：安装只在启动时主线程调用一次；处理函数在崩溃线程上跑，只用 async-signal-safe
// 调用（open/write/fsync/close/getpid/time/raise/_exit 与无锁原子读），不分配、不取锁。
// ============================================================

#include <filesystem>

#include "borealis/diag/crash_report.h"

namespace borealis::platform {

/// @brief 崩溃钩子的安装参数。
struct CrashHandlerConfig {
    std::filesystem::path directory;  ///< 崩溃文件落点；安装时建好（父目录须已存在）。
    /// 必须活到进程结束：装配层把它放在 `main` 的栈帧上，声明得比窗与会话都早。
    const diag::CrashFactsStore *facts = nullptr;
};

/// @brief 安装平台崩溃钩子；成功之后每次致命崩溃写一份 `crash-<pid>.txt`。
///
/// 文件只在崩溃时创建（正常退出不留空文件），内容见 `diag::emit_crash_report()`。
/// @param config 落点目录与事实来源。
/// @return 目录建不出来、路径过长或钩子注册失败时 false；调用方只落一条 WARN，不拦启动。
[[nodiscard]] auto install_crash_handler(const CrashHandlerConfig &config) -> bool;

}  // namespace borealis::platform
