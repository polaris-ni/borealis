#pragma once

// ============================================================
// 进程参数来源腿（src/platform/process_args.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.04`：CLI 参数在 POSIX 上就是 UTF-8 的 `argv` 字节，在 Win32
// 上窄 `argv` 会按 ANSI 代码页丢字，权威来源是 `GetCommandLineW` +
// `CommandLineToArgvW`。取法本身是平台知识，留在本腿（AGENTS.md §4.5 第 23 条）。
// ============================================================

#include <string>
#include <vector>

namespace borealis::platform {

/// @brief 取本进程的完整参数表（含 `argv[0]`）。
///
/// POSIX：原样交回 `argv` 的 UTF-8 字节。
/// Win32：走宽字符命令行并折 UTF-8；取宽字符失败时退回窄 `argv` 并记日志。
[[nodiscard]] auto process_arguments(int argc, char **argv) -> std::vector<std::string>;

}  // namespace borealis::platform
