#pragma once

// ============================================================
// SSH 隧道面板纯格式化函数（src/ui/tunnel_format.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.08 面板层的「最后一步」格式化：五态→词条 key、
// 错误归因→词条 key、行模型与监听点/目标串折叠（-R 0 端口的
// 「由服务器择定」形态）。全部只依赖标准类型与 conn/tunnel_model
// 的枚举，不含 aurora/au:: 类型、不碰网络与 UI（sftp_format 同族先例，
// 可无头单测）。
//
// 词条分工照 `sftp_error_key`：本件是唯一把枚举翻成词条 key 的地方，
// 面板不 switch 出第二套措辞；数字与地址等数据段由本件直出，
// 上屏文案一律经 settings_label(key) 现取（AGENTS §4.3 第 14 条）。
// ============================================================

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "conn/tunnel_model.h"  // TunnelSpec/TunnelState/TunnelError（非框架类型）

namespace borealis::ui {

/// @brief 一条隧道的运行态快照（装配层 Sink 写、面板逐帧读，裁决 7.97 D6①）。
struct TunnelRuntime {
    conn::TunnelState state{conn::TunnelState::Stopped};
    conn::TunnelError error{conn::TunnelError::None};
    int attempt{0};             ///< 已发生的失败次数（Backoff 行显示「第 N 次重试」）。
    int bound_port{0};          ///< -R 服务器择定的实际端口（Active 回报，0＝还没有）。
    bool autostart_skipped{false};  ///< D8 细则：启动序因询问型凭据跳过自启。

    [[nodiscard]] auto operator==(const TunnelRuntime &) const noexcept -> bool = default;
};

/// @brief 隧道行表的一行：定义 + 运行态（面板只消费算好的行）。
struct TunnelRow {
    conn::TunnelSpec spec;
    TunnelRuntime runtime{};
};

/// @brief 行模型：按落盘次序逐条配快照；没有快照的定义按缺省 Stopped 行呈现。
[[nodiscard]] auto tunnel_rows(const std::vector<conn::TunnelSpec> &specs,
                               const std::map<std::string, TunnelRuntime> &runtimes)
    -> std::vector<TunnelRow>;

/// @brief 形态徽标（三式的上屏短标）。
[[nodiscard]] auto tunnel_kind_badge(conn::TunnelKind kind) -> std::string_view;

/// @brief 五态 → 词条 key（tunnel.state.*）。
[[nodiscard]] auto tunnel_state_key(conn::TunnelState state) -> std::string_view;

/// @brief 错误归因 → 词条 key（tunnel.error.*）；None 回空串——无错时不该取文案。
[[nodiscard]] auto tunnel_error_key(conn::TunnelError error) -> std::string_view;

/// @brief 次行数据段「监听点 → 目标」：
///        Local：`127.0.0.1:5432 → db01.internal:5432`（地址取 effective_listen_address）；
///        Dynamic：`SOCKS5 127.0.0.1:1080`（目标逐请求，无右端）；
///        Remote：`:8730 → 127.0.0.1:873`，0 端口未回报时左端为 `:0`（注记见
///        tunnel_endpoint_note_key），Active 后换成服务器择定的实际端口。
[[nodiscard]] auto tunnel_endpoint_line(const TunnelRow &row) -> std::string;

/// @brief -R 0 端口注记 → 词条 key：未 Active 是「由服务器择定」、Active 且已回报是
///        「服务器已择定」；其余形态（含非 0 端口）回空串。
[[nodiscard]] auto tunnel_endpoint_note_key(const TunnelRow &row) -> std::string_view;

/// @brief Backoff 行的下次重试等待秒数（向上取整；非 Backoff 回 0）。
[[nodiscard]] auto tunnel_retry_wait_s(const TunnelRow &row) -> int;

}  // namespace borealis::ui
