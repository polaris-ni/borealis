// ============================================================
// 隧道面板纯格式化实现（src/ui/tunnel_format.cpp）——判据与设计口径见
// tunnel_format.h 头注与 codespec/UI_TUNNEL.draft.md §2/§3。
// ============================================================

#include "tunnel_format.h"

#include <algorithm>
#include <string>

namespace borealis::ui {

namespace {

/// @brief 端点串：地址为空（Remote 的服务器侧缺省）时只留 `:port`。
[[nodiscard]] auto endpoint(std::string_view address, int port) -> std::string {
    return std::string{address} + ":" + std::to_string(port);
}

}  // namespace

auto tunnel_rows(const std::vector<conn::TunnelSpec> &specs,
                 const std::map<std::string, TunnelRuntime> &runtimes) -> std::vector<TunnelRow> {
    std::vector<TunnelRow> rows;
    rows.reserve(specs.size());
    for (const auto &spec : specs) {
        auto row = TunnelRow{spec, TunnelRuntime{}};
        // 运行态只认「装配层起过这条」的快照；面板开关/重启后无快照即 Stopped 行。
        if (const auto it = runtimes.find(spec.id); it != runtimes.end()) {
            row.runtime = it->second;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

auto tunnel_kind_badge(conn::TunnelKind kind) -> std::string_view {
    switch (kind) {
    case conn::TunnelKind::Local:
        return "-L";
    case conn::TunnelKind::Remote:
        return "-R";
    case conn::TunnelKind::Dynamic:
        return "-D";
    }
    return "-L";
}

auto tunnel_state_key(conn::TunnelState state) -> std::string_view {
    switch (state) {
    case conn::TunnelState::Stopped:
        return "tunnel.state.stopped";
    case conn::TunnelState::Dialing:
        return "tunnel.state.dialing";
    case conn::TunnelState::Active:
        return "tunnel.state.active";
    case conn::TunnelState::Backoff:
        return "tunnel.state.backoff";
    case conn::TunnelState::Failed:
        return "tunnel.state.failed";
    }
    return "tunnel.state.stopped";
}

auto tunnel_error_key(conn::TunnelError error) -> std::string_view {
    switch (error) {
    case conn::TunnelError::None:
        return {};
    case conn::TunnelError::DialFailed:
        return "tunnel.error.dial_failed";
    case conn::TunnelError::BindFailed:
        return "tunnel.error.bind_failed";
    case conn::TunnelError::RemoteRefused:
        return "tunnel.error.remote_refused";
    case conn::TunnelError::ChannelLost:
        return "tunnel.error.channel_lost";
    }
    return {};
}

auto tunnel_endpoint_line(const TunnelRow &row) -> std::string {
    const auto &spec = row.spec;
    switch (spec.kind) {
    case conn::TunnelKind::Dynamic:
        return "SOCKS5 " + endpoint(conn::effective_listen_address(spec), spec.listen_port);
    case conn::TunnelKind::Remote: {
        // 0 端口：未回报前显示 `:0`（注记另走词条），Active 回报后换成实际端口（判据 4）。
        const int shown = (spec.listen_port == 0 && row.runtime.bound_port > 0) ? row.runtime.bound_port
                                                                                : spec.listen_port;
        return endpoint("", shown) + " → " + endpoint(spec.target_host, spec.target_port);  // CJK-LITERAL: 上屏文案 - 方向箭头是行表折叠的既定视觉形态
    }
    case conn::TunnelKind::Local:
        break;
    }
    return endpoint(conn::effective_listen_address(spec), spec.listen_port) + " → "
         + endpoint(spec.target_host, spec.target_port);  // CJK-LITERAL: 上屏文案 - 同上
}

auto tunnel_endpoint_note_key(const TunnelRow &row) -> std::string_view {
    if (row.spec.kind != conn::TunnelKind::Remote || row.spec.listen_port != 0) {
        return {};
    }
    if (row.runtime.bound_port > 0) {
        return "tunnel.listen.server_chosen";
    }
    if (row.runtime.state == conn::TunnelState::Active) {
        // Active 但回报值还没进快照：按「已择定」口径留注记，等下一帧换数。
        return "tunnel.listen.server_chosen";
    }
    return "tunnel.listen.server_picked";
}

auto tunnel_retry_wait_s(const TunnelRow &row) -> int {
    if (row.runtime.state != conn::TunnelState::Backoff) {
        return 0;
    }
    const int ms = conn::retry_delay_ms(std::max(row.runtime.attempt, 1), row.spec.retry);
    return (ms + 999) / 1000;
}

}  // namespace borealis::ui
