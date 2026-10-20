#pragma once

// ============================================================
// OSC 7 本机目录解析与继承取舍（include/borealis/ui/osc7.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.02` 里与绘制、平台都无关的那半：把 `term::OscState::working_directory`
// 留存的原文（`file://host/path`）折成一条**本机**文件系统目录，再折算「新标签该从哪儿起」。
//
// 不含 Aurora、`config` 与 `session` 类型（同 `url.h` 的模块环口径，架构 §2.3）：原文由装配层
// 从 `Session::osc_state()` 取出后交进来，本件因此能脱开界面与会话全量单测（AGENTS §4.4 第 20 条）。
//
// 四条口径的由来：
// - **只认空 host 与 `localhost`**：`file://` 的 host 是「这份路径属于哪台机器」。空 host 与
//   `localhost` 明确指本机；其余 host（含真实机器名与嵌套 `ssh` 里远端 shell 报上来的名字）
//   一律不认——把远端路径交给下一只本地 shell，通常只会得到一个起不来的会话。宁可退回配置目录。
//   代价是常见的 shell 集成脚本直接报机器名时本需求不生效；这与「本地终端语义、远端不继承」
//   的边界同向，属 fail-closed，不猜。
// - **百分号解码从严**：OSC 7 的 path 是 URI 路径，`%` 后不是两位十六进制即整条不认；不把坏串
//   猜成路径。问号与井号在解码**之前**截断，于是 `%3F` / `%23` 仍属于文件名。
// - **拒绝 NUL**：路径最终要交给以 NUL 结尾的平台入口，串内 NUL 会让它静默截短到另一条路径。
// - **两平台路径形态分开判**：POSIX 直接用 URI 路径；Windows 的 `file:///C:/x` 在 URI 里是
//   `/C:/x`，交给 `CreateProcessW` 前要去掉盘符前那一枚 `/`。形态由调用方显式给，本件不读环境。
// ============================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace borealis::ui {

/// @brief OSC 7 路径按哪条平台的形态判（与终端内容无关，只随构建目标）。
enum class Osc7PathStyle : std::uint8_t {
    Posix,   ///< `/srv/repo`、`/`。
    Windows, ///< `C:/work`、`C:/`（URI 原文写作 `/C:/work`）。
};

/// @brief 当前构建目标的路径形态。
///
/// 这是一条**编译期约定**而不是运行时探测：同一份会话字节流在不同平台上本就该折成不同的本地
/// 路径形态，故装配层只问一次，解析件仍以显式参数接收（两平台都能在任一沙箱里单测）。
[[nodiscard]] auto osc7_path_style() -> Osc7PathStyle;

/// @brief 把 OSC 7 原文折成本机绝对目录。
///
/// 接受 `file:///path`、`file://localhost/path`（host 大小写不敏感）与 Windows 的
/// `file:///C:/path`；路径按 URI 规则做严格百分号解码。**不做存在性判断**——那是启动会话时
/// 平台腿的职责，本件只回答「这串报的是不是一条可用的本机路径」。
/// @param raw `OSC 7` 的原始载荷（`term::OscState::working_directory`，未解码）。
/// @param style 目标平台的路径形态。
/// @return UTF-8 目录；非 `file://`、host 非空且非 `localhost`、无路径、百分号编码非法、
///         解码后含 NUL，或不符合 @p style 的绝对路径形态时 `std::nullopt`。
[[nodiscard]] auto local_directory_from_osc7(std::u32string_view raw, Osc7PathStyle style)
    -> std::optional<std::string>;

/// @brief 新标签的起始目录及其来源。
struct StartDirectory {
    std::string path;        ///< 交给 `conn::LocalTerminalSpec::working_directory`；空＝继承进程目录。
    bool inherited = false;  ///< 是否取自当前会话的 OSC 7（false 即配置值，含配置为空）。

    /// @brief 逐字段全等比较（用例断言用）。
    [[nodiscard]] auto operator==(const StartDirectory &) const noexcept -> bool = default;
};

/// @brief 折算新标签的起始目录：本机会话优先继承当前目录，其余一律用配置值。
///
/// 「本机会话」由调用方判（当前标签是本地终端且会话仍活着）；SSH / 串口 / Telnet 标签的
/// `working_directory` 要么是远端语义、要么没有意义，故 @p local_session 为 false 时连解析都不做。
/// 显式本地档案不走本函数——档案目录是用户写死的意图，优先级高于继承。
/// @param local_session 当前标签是否为本地终端。
/// @param osc7 当前会话最近一次 OSC 7 原文。
/// @param configured `connection.startup_directory` 的当前值（可空）。
/// @param style 目标平台的路径形态。
/// @return 可继承时取解析结果并置 `inherited`，否则原样交回 @p configured。
[[nodiscard]] auto choose_start_directory(bool local_session, std::u32string_view osc7,
                                          std::string_view configured, Osc7PathStyle style)
    -> StartDirectory;

}  // namespace borealis::ui
