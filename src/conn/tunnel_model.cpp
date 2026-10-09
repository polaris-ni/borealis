// ============================================================
// SSH 隧道纯逻辑模型实现（src/conn/tunnel_model.cpp）
// ------------------------------------------------------------
// 全部纯函数：无 IO、无 libssh、无 socket（见 conn/tunnel_model.h 头注释）。
// ============================================================

#include "conn/tunnel_model.h"

#include <algorithm>

namespace borealis::conn {

namespace {

/// @brief 端口是否落在合法服务端口区间 [1,65535]。
[[nodiscard]] auto is_service_port(int port) -> bool {
    return port >= 1 && port <= 65535;
}

/// @brief 该 kind 的监听点是否设在本机（Local/Dynamic 一侧）。
[[nodiscard]] auto listens_locally(TunnelKind kind) -> bool {
    return kind == TunnelKind::Local || kind == TunnelKind::Dynamic;
}

}  // namespace

auto effective_listen_address(const TunnelSpec &spec) -> std::string {
    if (!spec.listen_address.empty()) {
        return spec.listen_address;
    }
    // Remote 的空地址留给服务器裁决；本端缺省只绑回环（不暴露局域网）。
    return listens_locally(spec.kind) ? std::string{"127.0.0.1"} : std::string{};
}

auto validate_tunnel_spec(const TunnelSpec &spec) -> TunnelSpecIssue {
    // Remote 允许 0 端口：由服务器择定后再经 bound_port 回报；其余必须 [1,65535]。
    const auto listen_ok = is_service_port(spec.listen_port) ||
                           (spec.kind == TunnelKind::Remote && spec.listen_port == 0);
    if (!listen_ok) {
        return TunnelSpecIssue::ListenPortInvalid;
    }
    if (spec.kind == TunnelKind::Dynamic) {
        // SOCKS 的目标逐请求来自对端，定义里没有 target 一说。
        return TunnelSpecIssue::None;
    }
    if (spec.target_host.empty()) {
        return TunnelSpecIssue::TargetHostEmpty;
    }
    if (!is_service_port(spec.target_port)) {
        return TunnelSpecIssue::TargetPortInvalid;
    }
    return TunnelSpecIssue::None;
}

auto specs_conflict(const TunnelSpec &a, const TunnelSpec &b) -> bool {
    if (&a == &b) {
        return false;
    }
    // 分属两端监听（Remote vs Local/Dynamic）不构成争抢。
    if (listens_locally(a.kind) != listens_locally(b.kind)) {
        return false;
    }
    // 两侧同为 Remote：0 端口由服务器择定，无法预知冲突。
    if (a.kind == TunnelKind::Remote && b.kind == TunnelKind::Remote) {
        return a.listen_port != 0 && b.listen_port != 0 && a.listen_port == b.listen_port &&
               a.listen_address == b.listen_address;
    }
    // 本端一对：生效地址 + 端口全等即撞车（Local 与 Dynamic 同侧同端口也算）。
    return a.listen_port == b.listen_port && a.listen_port != 0 &&
           effective_listen_address(a) == effective_listen_address(b);
}

auto has_conflict(const std::vector<TunnelSpec> &specs) -> bool {
    for (std::size_t i = 0; i < specs.size(); ++i) {
        for (std::size_t j = i + 1; j < specs.size(); ++j) {
            if (specs_conflict(specs[i], specs[j])) {
                return true;
            }
        }
    }
    return false;
}

auto next_state(TunnelState state, TunnelSignal signal) -> TunnelState {
    if (signal == TunnelSignal::Stop) {
        return TunnelState::Stopped;
    }
    switch (state) {
    case TunnelState::Stopped:
        return signal == TunnelSignal::Start ? TunnelState::Dialing : state;
    case TunnelState::Dialing:
        if (signal == TunnelSignal::DialSucceeded) {
            return TunnelState::Active;
        }
        if (signal == TunnelSignal::RecoverableFailure) {
            return TunnelState::Backoff;
        }
        return signal == TunnelSignal::FatalFailure ? TunnelState::Failed : state;
    case TunnelState::Active:
        if (signal == TunnelSignal::RecoverableFailure) {
            return TunnelState::Backoff;
        }
        return signal == TunnelSignal::FatalFailure ? TunnelState::Failed : state;
    case TunnelState::Backoff:
        // 退避到点重拨，或重拨前就撞上终态错误。
        if (signal == TunnelSignal::Start) {
            return TunnelState::Dialing;
        }
        return signal == TunnelSignal::FatalFailure ? TunnelState::Failed : state;
    case TunnelState::Failed:
        // 终态只被用户再启（Start）唤醒；其余信号保持。
        return signal == TunnelSignal::Start ? TunnelState::Dialing : state;
    }
    return state;
}

auto state_is_running(TunnelState state) -> bool {
    return state == TunnelState::Dialing || state == TunnelState::Active ||
           state == TunnelState::Backoff;
}

auto retry_delay_ms(int attempt, const RetryPolicy &policy) -> int {
    const auto safe_attempt = std::max(attempt, 1);
    const auto cap = std::max(policy.cap_ms, 0);
    auto delay = std::max(policy.base_ms, 1);
    // 逐步翻倍、到顶即停：乘法只在 delay<cap 时发生，天然不溢出。
    for (int i = 1; i < safe_attempt && delay < cap; ++i) {
        delay *= 2;
    }
    return std::min(delay, cap);
}

auto should_retry(int attempt, const RetryPolicy &policy) -> bool {
    if (policy.max_attempts <= 0) {
        return true;
    }
    return attempt < policy.max_attempts;
}

auto signal_for_error(TunnelError error) -> TunnelSignal {
    switch (error) {
    case TunnelError::None:
        // 不该作为失败信号出现；保守回 Stop 之外的保持态——用 FatalFailure 会误杀，
        // 用 RecoverableFailure 会空转，故传输腿约定：None 不进本函数。回 Start
        // 是恒等保持（任何状态收 Start 至多回 Dialing），实际调用方须自行避免。
        return TunnelSignal::Start;
    case TunnelError::DialFailed:
        return TunnelSignal::RecoverableFailure;
    case TunnelError::BindFailed:
        return TunnelSignal::FatalFailure;
    case TunnelError::RemoteRefused:
        return TunnelSignal::FatalFailure;
    case TunnelError::ChannelLost:
        return TunnelSignal::RecoverableFailure;
    }
    return TunnelSignal::RecoverableFailure;
}

}  // namespace borealis::conn
