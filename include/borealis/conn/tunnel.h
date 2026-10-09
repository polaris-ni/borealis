#pragma once

// ============================================================
// SSH 隧道定义的纯值部分（include/borealis/conn/tunnel.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.08` 的三式形态、重试策略与隧道定义。放公共头是因为
// config 第六域 `tunnels`（裁决 7.97 D3①）以 `TunnelSpec` 作 `Settings`
// 成员——settings.h 不得依赖 `src/` 私有包含路径（bench 目标只链
// borealis_core，不继承其 PRIVATE include）。校验/状态机/退避等逻辑函数
// 仍在 `conn/tunnel_model.h`（src 私有头）。
//
// 凭据纪律（CONN.09）：本结构全字段纯值，凭据只经 `profile_id` 引用，
// 落盘天然过「配置目录 grep 无明文凭据」审计。
// ============================================================

#include <cstdint>
#include <string>

namespace borealis::conn {

/// @brief 隧道形态（需求口径的「三式」）。
enum class TunnelKind : std::uint8_t {
    Local,    ///< -L：本地监听，转发到远端 target。
    Remote,   ///< -R：远端监听，转发回本地 target。
    Dynamic,  ///< -D：本地监听 SOCKS5，目标逐请求决定。
};

/// @brief 重试策略（指数退避）。max_attempts==0 表示不限次数——需求只说
///        「失败自动重试」，未给穷尽上限；给 0 让人可随时 stop()。
///        缺省值即裁决 7.97 D9② 的「旧全局档」。
struct RetryPolicy {
    int base_ms{1000};     ///< 首次退避。
    int cap_ms{30000};     ///< 退避上限（翻倍越过即钳住）。
    int max_attempts{0};   ///< 0＝不限；>0＝第 N 次失败后进 Failed。

    [[nodiscard]] auto operator==(const RetryPolicy &) const noexcept -> bool = default;
};

/// @brief 一条隧道的定义。listen/target 的「哪端在监听」由 kind 决定：
///        Local/Dynamic 是本机，Remote 是 SSH 服务器。
struct TunnelSpec {
    std::string id;                    ///< 稳定唯一 id（与 Profile.id 同款纪律）。
    std::string name;                  ///< 展示名。
    TunnelKind kind{TunnelKind::Local};
    std::string listen_address;        ///< 空＝effective_listen_address() 的缺省。
    int listen_port{0};                ///< Remote 允许 0＝服务器择定端口。
    std::string target_host;           ///< Dynamic 不用（目标在 SOCKS 请求里）。
    int target_port{0};                ///< Dynamic 不用。
    std::string profile_id;            ///< 承载隧道拨号的 SSH 档案 id（装配层解析）。
    // 两成员随裁决 7.97 改判入模型（D8②/D9②）。缺省值即旧行为：不自启、
    // 用全局退避档——旧配置文件（无这两键）读回后与新建逐字段相等。
    bool autostart{false};             ///< 启动序自启（仅凭据可静默解析时拉起，D8 细则）。
    RetryPolicy retry{};               ///< 逐条重试策略（空＝不限次，D9②）。

    [[nodiscard]] auto operator==(const TunnelSpec &) const noexcept -> bool = default;
};

}  // namespace borealis::conn
