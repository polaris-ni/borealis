// ============================================================
// 断线重连的纯逻辑裁决（src/conn/reconnect.cpp）
// ------------------------------------------------------------
// 见同头注释。本文件只做分类与合成，退避算式转调 conn/tunnel_model 的现成件。
// ============================================================

#include "conn/reconnect.h"

#include <algorithm>

#include "conn/tunnel_model.h"

namespace borealis::conn {

auto should_reconnect(session::CloseReason reason) -> bool {
    switch (reason) {
        case session::CloseReason::LinkLost:
        case session::CloseReason::DialNetwork:
            return true;
        case session::CloseReason::AuthFailed:
        case session::CloseReason::HostKeyRejected:
        case session::CloseReason::RemoteExit:
        case session::CloseReason::LocalClose:
        case session::CloseReason::Unknown:
            return false;
    }
    // 枚举穷尽才走到这里：新增档位若忘了归类，保守按不重拨而不是默认放行。
    return false;
}

auto default_reconnect_policy() -> RetryPolicy {
    return RetryPolicy{.base_ms = 1000, .cap_ms = 30000, .max_attempts = 3};
}

auto reconnect_step(int redial_index, const RetryPolicy &policy) -> std::optional<int> {
    const auto nth = std::max(redial_index, 1);
    // 见头注：本键数的是「还能再拨几回」，模型数的是「第几次失败即止」，差一格在此换算。
    if (!should_retry(nth - 1, policy)) {
        return std::nullopt;
    }
    return retry_delay_ms(nth, policy);
}

auto reconnect_stop(session::CloseReason reason, bool exhausted) -> ReconnectStop {
    switch (reason) {
        case session::CloseReason::RemoteExit:
            return ReconnectStop::RemoteExit;
        case session::CloseReason::LocalClose:
            return ReconnectStop::UserStopped;
        case session::CloseReason::LinkLost:
        case session::CloseReason::DialNetwork:
            // 可重拨的原因停在终态只有两条路：次数用尽，或用户按「停止重连」让环自己落终态。
            return exhausted ? ReconnectStop::AttemptsExhausted : ReconnectStop::UserStopped;
        case session::CloseReason::AuthFailed:
        case session::CloseReason::HostKeyRejected:
        case session::CloseReason::Unknown:
            return ReconnectStop::NotReconnectable;
    }
    return ReconnectStop::NotReconnectable;
}

auto sftp_error_drops_link(SftpError error) -> bool {
    switch (error) {
        case SftpError::Network:
        case SftpError::NotConnected:
            return true;
        case SftpError::None:
        case SftpError::PermissionDenied:
        case SftpError::NotFound:
        case SftpError::Cancelled:
        case SftpError::LocalIo:
        case SftpError::Protocol:
            return false;
    }
    return false;
}

}  // namespace borealis::conn
