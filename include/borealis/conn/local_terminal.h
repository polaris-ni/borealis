#pragma once

// ============================================================
// 本地终端连接（include/borealis/conn/local_terminal.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.01` 的公共声明面：连接类型归 `borealis::conn`（架构 §2.1），实现归
// `src/platform/`。本头只出现标准类型——ConPTY 句柄、Win32 类型、码页 API 一律留在实现内
// （裁决 7.11），否则后补 posix 等价时会变成重写而不是补一个实现文件。
//
// profile 的持久化与配置层（`SPEC.FEAT.PREF.03`）尚未落地，故启动目录、环境变量与自定义
// 命令行以 `LocalTerminalSpec` 的形态由调用方显式给出（架构 §2.2 把 profile 归
// `include/borealis/config/`，该域待建）。
// ============================================================

#include <map>
#include <memory>
#include <string>

#include "borealis/session/connection.h"

namespace borealis::conn {

/// @brief 一条本地终端会话的启动规格（`SPEC.FEAT.CONN.01`）。
struct LocalTerminalSpec {
    /// 自定义命令行（「自定义命令」模式：直接挂任意可执行文件，如 `ssh host`、`docker exec -it c sh`）。
    /// 空 = 走 @ref default_shell_command_line 的平台默认 shell 探测。
    std::string command_line;
    /// 启动目录；空 = 继承本进程当前目录。
    std::string working_directory;
    /// 追加环境变量，同名时覆盖继承来的那份。键值均为 UTF-8。
    std::map<std::string, std::string> environment;
};

/// PTY 环境注入的默认变量（`SPEC.FEAT.CONN.01`）：终端类程序据此选择色深与能力集。
/// 实现方在合并环境块时先套用这两个，再用 @ref LocalTerminalSpec::environment 覆盖，
/// 于是使用者仍可显式改写 `TERM`。
inline const std::map<std::string, std::string> kPtyDefaultEnvironment = {
    {"TERM", "xterm-256color"},
    {"COLORTERM", "truecolor"},
};

/// @brief 探测平台默认 shell 的命令行（`SPEC.FEAT.CONN.01`：Windows 为 PowerShell → cmd → WSL）。
///
/// 返回值可直接作为 @ref LocalTerminalSpec::command_line 使用；含空格的程序路径已带引号。
/// 探测只做存在性判断，不启动任何进程。
/// @return 命令行（UTF-8）；平台上一个候选都找不到时为空串。
[[nodiscard]] auto default_shell_command_line() -> std::string;

/// @brief 创建本地终端连接。
///
/// 返回的是 @ref session::Connection 抽象，会话层不知道对面是 ConPTY 还是 posix PTY。
/// 初始尺寸在此下发，即 `SPEC.FEAT.XFER.01` 的「会话启动时下发一次初始尺寸」腿；
/// 后续尺寸变更经 `Connection::resize`。
/// @param spec 启动规格。
/// @param initial_size 视口初始尺寸（列 × 行）。
/// @return 未启动的连接；当前仅 Win32 有实现（posix 侧待建，见 codespec/PLAN.md §8）。
[[nodiscard]] auto make_local_terminal_connection(const LocalTerminalSpec &spec,
                                                  session::Size initial_size)
    -> std::unique_ptr<session::Connection>;

}  // namespace borealis::conn
