// ============================================================
// SSH 建连 + 认证共用腿实现（src/conn/ssh_dial.cpp）
// ------------------------------------------------------------
// 从 SshConnection::run_loop 抽出的建连/认证段，逻辑原样迁移
// （SPEC.FEAT.CONN.02 落地时的口径）；SFTP 浏览器（SPEC.FEAT.CONN.04）
// 是第二消费方。libssh 的阻塞语义不变：本函数可能阻塞数秒，调用方必须
// 在非 UI 线程上调用。秘密材料只在形参里存活到返回，绝不落盘不进日志。
// ============================================================

#include "ssh_dial.h"

#include <string>
#include <string_view>
#include <utility>

#include <libssh/libssh.h>

#include "aurora/core/log.h"

namespace borealis::conn {

namespace {

/// @brief TCP 连接超时（秒）：libssh 的 SSH_OPTIONS_TIMEOUT。
constexpr long kConnectTimeoutSeconds = 10;

/// @brief libssh 主机密钥状态 → 本仓枚举的翻译（唯一知道 libssh 枚举取值的地方）。
///        状态以 int 接住，避免本文件之外需要 libssh 枚举类型。
[[nodiscard]] auto translate_host_key_state(int state) -> HostKeyState {
    switch (state) {
        case SSH_KNOWN_HOSTS_OK:
            return HostKeyState::Known;
        case SSH_KNOWN_HOSTS_NOT_FOUND:
            return HostKeyState::New;
        case SSH_KNOWN_HOSTS_CHANGED:
            return HostKeyState::Changed;
        case SSH_KNOWN_HOSTS_ERROR:
            return HostKeyState::Error;
        case SSH_KNOWN_HOSTS_OTHER:
            // 服务器给了我们不接受的密钥类型：按「核对失败」处理，策略自会拒绝。
            return HostKeyState::Error;
        case SSH_KNOWN_HOSTS_UNKNOWN:
            return HostKeyState::New;  // 核对未跑过视为「无记录」。
    }
    return HostKeyState::Error;
}

}  // namespace

auto policy_accepts(KnownHostsPolicy policy, HostKeyState state) -> bool {
    switch (policy) {
        case KnownHostsPolicy::AcceptNew:
            return state == HostKeyState::Known || state == HostKeyState::New;
        case KnownHostsPolicy::Yes:
            return state == HostKeyState::Known;
        case KnownHostsPolicy::No:
            return true;
        case KnownHostsPolicy::Ask:
            // 本期保守处理：未知即拒绝，绝不静默放行（询问件属 CONN.09）。
            return state == HostKeyState::Known;
    }
    return false;
}

auto effective_port(const SshProfile &profile) -> int {
    return profile.port > 0 ? profile.port : 22;
}

auto normalized_auth_method(const SshProfile &profile) -> std::string_view {
    const std::string_view method{profile.auth_method};
    if (method == "password" || method == "privatekey" || method == "agent" ||
        method == "keyboard-interactive") {
        return method;
    }
    return "agent";  // 未识别值回落模型缺省。
}

auto auth_requires_secret(std::string_view method) -> bool {
    return method == "password";
}

auto ssh_dial_and_authenticate(const SshProfile &profile,
                               std::optional<std::string> secret,
                               ssh_session_struct *session) -> bool {
    if (session == nullptr) {
        AURORA_LOG_ERROR("conn", "ssh: dial: session not allocated");
        return false;
    }

    // ---- 会话选项 ----
    const auto port = effective_port(profile);
    ssh_options_set(session, SSH_OPTIONS_HOST, profile.host.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    if (!profile.user.empty()) {
        ssh_options_set(session, SSH_OPTIONS_USER, profile.user.c_str());
    }
    const auto timeout = static_cast<unsigned int>(kConnectTimeoutSeconds);
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);

    // ---- 连接 ----
    if (ssh_connect(session) != SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: connect to '", profile.host, ":", port, "' failed: ",
                         ssh_get_error(session));
        return false;
    }

    // ---- 主机密钥核对（D3②：进程内核对，拒绝权在 policy_accepts）----
    const auto known =
        translate_host_key_state(static_cast<int>(ssh_session_is_known_server(session)));
    if (!policy_accepts(profile.known_hosts_policy, known)) {
        AURORA_LOG_ERROR("conn", "ssh: host key rejected by policy for '", profile.host,
                         "' (state=", static_cast<int>(known), ")");
        return false;
    }
    if (known == HostKeyState::New && profile.known_hosts_policy == KnownHostsPolicy::AcceptNew) {
        // accept-new 语义的「首次接受」腿：写回 known_hosts，之后即 Known。
        if (ssh_session_update_known_hosts(session) != SSH_OK) {
            AURORA_LOG_WARN("conn", "ssh: cannot update known_hosts: ", ssh_get_error(session));
        }
    }

    // ---- 认证 ----
    const auto method = normalized_auth_method(profile);
    const char *user = profile.user.empty() ? nullptr : profile.user.c_str();
    // libssh 0.12 的 ssh_userauth_* 返回 int（取值域是 enum ssh_auth_e）；
    // 按 int 接住、与枚举值比较，避免处处 static_cast 噪声。
    auto auth_rc = int{SSH_AUTH_ERROR};
    if (method == "password") {
        // password 认证必须有秘密材料；缺材料直接失败，绝不发明空口令去撞。
        if (!secret.has_value()) {
            AURORA_LOG_ERROR("conn", "ssh: password auth requested but no secret available");
            return false;
        }
        auth_rc = ssh_userauth_password(session, user, secret->c_str());
    } else if (method == "privatekey") {
        // passphrase 可选：无口令私钥传空串；identity_file 空＝SSH 默认（~/.ssh/id_*）。
        const char *passphrase = secret.has_value() ? secret->c_str() : "";
        auth_rc = ssh_userauth_privatekey_file(session, user,
                                               profile.identity_file.empty()
                                                   ? nullptr
                                                   : profile.identity_file.c_str(),
                                               passphrase);
    } else if (method == "agent") {
        auth_rc = ssh_userauth_agent(session, user);
    } else {  // keyboard-interactive：单提示一次作答（本期边界，见 SshConnection 头部）。
        auth_rc = ssh_userauth_kbdint(session, user, nullptr);
        while (auth_rc == SSH_AUTH_INFO) {
            const auto nprompts = ssh_userauth_kbdint_getnprompts(session);
            for (int i = 0; i < nprompts; ++i) {
                // 非回显提示（口令类）且有答案才作答；其余提示留空让服务器裁决。
                auto echo = char{0};
                static_cast<void>(ssh_userauth_kbdint_getprompt(session,
                                                                static_cast<unsigned int>(i),
                                                                &echo));
                if (secret.has_value() && echo == 0) {
                    ssh_userauth_kbdint_setanswer(session, static_cast<unsigned int>(i),
                                                  secret->c_str());
                }
            }
            auth_rc = ssh_userauth_kbdint(session, nullptr, nullptr);
        }
    }
    // 秘密材料使命完成，就地清副本（CONN.09：明文不比认证活得久）。
    secret.reset();

    if (auth_rc != SSH_AUTH_SUCCESS) {
        AURORA_LOG_ERROR("conn", "ssh: auth (", method, ") failed for '", profile.host,
                         "': ", ssh_get_error(session));
        return false;
    }
    return true;
}

}  // namespace borealis::conn
