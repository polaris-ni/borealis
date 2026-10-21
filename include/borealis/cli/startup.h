#pragma once

// ============================================================
// CLI 启动参数（include/borealis/cli/startup.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.04` 的纯逻辑面：把一次启动的参数折成结构化请求，并按
// 当前档案表解析 `--profile`。本层不读盘、不建窗、不解析路径是否存在
// （AGENTS.md §4.5 第 23 条：平台知识留在 platform 腿）；同一个解析结果既用于
// 冷启动，也用于转发给已有实例。
//
// 语法（一次调用只表达一个动作）：
//   Borealis [new-tab | split-pane] [--profile <name-or-id>] [--cwd <dir>]
//            [--new-window] [--help]
//
// 缺省动作是 `new-tab`；`--help` 在本地消费，不占用单实例通道。
// `--new-window` 是第三个动作的写法（不是修饰符）：它与两个子命令互斥，因为「在新窗里切当前窗的
// 那一格」没有可读的语义。冷启动那一侧它与缺省 `new-tab` 同一条腿——正要建的这一扇就是那扇新窗。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/conn/profile.h"

namespace borealis::cli {

/// @brief 一次启动要执行的动作。
enum class StartupAction : std::uint8_t {
    NewTab,     ///< 在当前窗口新开一个标签（冷启动即首标签）。
    SplitPane,  ///< 切分当前标签的焦点 pane。
    NewWindow,  ///< 开一扇新窗，窗里落一个标签（`SPEC.FEAT.WS.03` 稿 D10；线格式 action 的第 3 值）。
};

/// @brief 单个字符串参数的上限（profile 名 / cwd）；超出即用法错误。
inline constexpr std::size_t kMaxStartupValueBytes = 4096;

/// @brief 解析后的启动请求。
struct StartupRequest {
    StartupAction action{StartupAction::NewTab};
    std::string profile;     ///< `--profile` 原值（id 或展示名）；空＝未指定。
    std::string cwd;         ///< `--cwd` 原值；空＝未指定。
    bool new_window{false};  ///< `--new-window` 是否出现；解析产出里它与 `action == NewWindow` 同真同假。
                             ///< 解码侧按线格式独立还原旗标，故「动作 0 + 旗标 1」这一格仍可能出现——
                             ///< 那是旧版 CLI 转交过来的帧，含义同一条（用户要的是新窗）。
    /// @brief 逐字段全等比较（用例断言用）。
    [[nodiscard]] auto operator==(const StartupRequest &) const noexcept -> bool = default;
};

/// @brief 语法解析结果：三态互斥（request / help / error），后两者不产生请求。
struct StartupParse {
    std::optional<StartupRequest> request;
    bool help{false};
    std::string error;  ///< ASCII 诊断；空串表示无错。
};

/// @brief 解析去掉程序名之后的参数表。
/// @param args `argv[1..]`；不含可执行文件名。
/// @return 请求、`--help` 或 ASCII 诊断；三者互斥。
[[nodiscard]] auto parse_startup_args(std::span<const std::string> args) -> StartupParse;

/// @brief 用法文本（ASCII，供 `AURORA_LOG_RAW` 原样输出）。
[[nodiscard]] auto startup_usage() -> std::string_view;

/// @brief 按档案解析 `--profile` 的结果。
struct ProfileResolution {
    const conn::Profile *profile{nullptr};
    std::string error;  ///< ASCII 诊断；空串表示命中。
};

/// @brief 把 `--profile <name-or-id>` 解析到档案表里的一条。
///
/// 命中顺序：精确 id → 精确展示名 → 唯一的大小写不敏感展示名。任一档命中多条即
/// 「ambiguous」失败，不取第一条；没有命中即「not found」。不做模糊搜索——那是
/// 搜索框的语义（`ProfileStore::search`），不是命令行指定的语义。
/// @param profiles 当前内存中的档案表。
/// @param query 命令行给的 id 或展示名。
[[nodiscard]] auto resolve_profile(const std::vector<conn::Profile> &profiles, std::string_view query)
    -> ProfileResolution;

/// @brief 动作与参数组合的策略检查（纯逻辑，不看运行期状态）。
///
/// 每 pane 一会话落地后（`SPEC.FEAT.WS.02`），`split-pane` 只留一条拒绝：`--profile` 指向**非本地**
/// 档案。本地腿没有凭据链，目录与命令都能当场给全；SSH / 串口 / Telnet 的新格要先问密码或选串口，
/// 而切分工厂是同步返回的——异步弹框与同步工厂互斥，那条补链属独立一件（稿 `WS02_PANE_SESSION` §9
/// 第 4 条）。`split-pane --cwd` 已放开，当前标签是不是本地腿属运行期事实，由装配层判、不在这里。
/// `--cwd` 只对本地语义生效（缺省本地或 Local 档案）那一条不变。
/// `--new-window`（`SPEC.FEAT.WS.03` 稿 D10）与 `--profile` / `--cwd` 相容：新窗那一格吃的就是这一份料，
/// 与 `new-tab` 同一条判定，本件对它不设拒绝档。
/// @param request 已解析的请求。
/// @param profile_type `--profile` 命中档案的类型；未指定档案时为空（未解析即按非本地处置）。
/// @return 空串表示通过；否则为 ASCII 诊断。
[[nodiscard]] auto startup_policy_error(const StartupRequest &request,
                                        std::optional<conn::ConnectionType> profile_type)
    -> std::string;

}  // namespace borealis::cli
