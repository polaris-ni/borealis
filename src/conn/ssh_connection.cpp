// ============================================================
// SSH 真传输连接实现（src/conn/ssh_connection.cpp）
// ------------------------------------------------------------
// 本文件是 SPEC.FEAT.CONN.02 集成式 SSH 传输在 Linux 腿的全部平台知识：libssh
// 会话建立、主机密钥核对（D3②/D8 裁决）、认证、PTY 通道与读循环。对照 forkpty
// 腿的三条纪律同样适用：
//   1. 连接/认证/读都在读线程上做——ssh_connect 会阻塞数秒，绝不占 UI 线程。
//   2. close() 先置 closing 再 join 读线程，最后才释放 libssh 句柄，回调不悬空。
//   3. alive() 只读原子量，免锁（UI 线程随时会问）。
// 凭据（CONN.09）：明文只在构造传入的 secret_ 里存活，认证完成后即清空；
// 错误日志只打 libssh 的错误文本（不含口令），绝不回显秘密材料。
// ============================================================

#include "ssh_connection.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include <libssh/libssh.h>

#include "aurora/core/log.h"

namespace borealis::conn {

namespace {

/// @brief 读循环单块上限：与 forkpty 腿同款，原样整块投递给会话层。
constexpr std::size_t kReadChunkBytes = 32768;

/// @brief 非阻塞读空转的休眠步长：close() 靠 closing 标志叫停循环，步长即关闭延迟上界。
constexpr useconds_t kReadIdleUs = 30000;

/// @brief TCP 连接超时（秒）：libssh 的 SSH_OPTIONS_TIMEOUT。
constexpr long kConnectTimeoutSeconds = 10;

/// @brief libssh 主机密钥状态 → 本仓枚举的翻译（唯一知道 libssh 枚举取值的地方）。
[[nodiscard]] auto translate_host_key_state(enum ssh_known_hosts_e state) -> HostKeyState {
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

/// @brief PTY 请求的 TERM 值：与本地腿 `kPtyDefaultEnvironment` 的 TERM 同源口径。
constexpr std::string_view kPtyTerm = "xterm-256color";

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
            // 本期保守处理：未知即拒绝，绝不静默放行（头部「已知边界」）。
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

SshConnection::SshConnection(const SshProfile &profile, std::optional<std::string> secret,
                             session::Size initial_size)
    : profile_{profile}, secret_{std::move(secret)}, size_{initial_size} {}

SshConnection::~SshConnection() {
    close();
    teardown();
}

auto SshConnection::start(session::ConnectionEvents &events) -> void {
    if (started_.exchange(true) || closing_.load()) {
        return;
    }
    events_ = &events;
    // 连接/认证会阻塞数秒，全部推到读线程：start() 立即返回（forkpty 同款语义）。
    reader_ = std::thread([this] { run_loop(); });
}

auto SshConnection::run_loop() -> void {
    // ---- 建会话与选项 ----
    {
        const std::lock_guard lock{state_mutex_};
        session_ = ssh_new();
    }
    if (session_ == nullptr) {
        AURORA_LOG_ERROR("conn", "ssh: cannot allocate session");
        events_->on_closed();
        return;
    }

    const auto port = effective_port(profile_);
    ssh_options_set(session_, SSH_OPTIONS_HOST, profile_.host.c_str());
    ssh_options_set(session_, SSH_OPTIONS_PORT, &port);
    if (!profile_.user.empty()) {
        ssh_options_set(session_, SSH_OPTIONS_USER, profile_.user.c_str());
    }
    const auto timeout = static_cast<unsigned int>(kConnectTimeoutSeconds);
    ssh_options_set(session_, SSH_OPTIONS_TIMEOUT, &timeout);

    // ---- 连接 ----
    if (ssh_connect(session_) != SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: connect to '", profile_.host, ":", port, "' failed: ",
                         ssh_get_error(session_));
        events_->on_closed();
        return;
    }

    // ---- 主机密钥核对（D3②：进程内核对，拒绝权在 policy_accepts）----
    const auto known = translate_host_key_state(ssh_session_is_known_server(session_));
    if (!policy_accepts(profile_.known_hosts_policy, known)) {
        AURORA_LOG_ERROR("conn", "ssh: host key rejected by policy for '", profile_.host,
                         "' (state=", static_cast<int>(known), ")");
        events_->on_closed();
        return;
    }
    if (known == HostKeyState::New && profile_.known_hosts_policy == KnownHostsPolicy::AcceptNew) {
        // accept-new 语义的「首次接受」腿：写回 known_hosts，之后即 Known。
        if (ssh_session_update_known_hosts(session_) != SSH_OK) {
            AURORA_LOG_WARN("conn", "ssh: cannot update known_hosts: ", ssh_get_error(session_));
        }
    }

    // ---- 认证 ----
    const auto method = normalized_auth_method(profile_);
    // libssh 0.12 的 ssh_userauth_* 返回 int（取值域是 enum ssh_auth_e）；
    // 按 int 接住、与枚举值比较，避免处处 static_cast 噪声。
    auto auth_rc = int{SSH_AUTH_ERROR};
    if (method == "password") {
        // password 认证必须有秘密材料；缺材料直接失败，绝不发明空口令去撞。
        if (!secret_.has_value()) {
            AURORA_LOG_ERROR("conn", "ssh: password auth requested but no secret available");
            events_->on_closed();
            return;
        }
        auth_rc = ssh_userauth_password(session_, profile_.user.empty() ? nullptr
                                                                        : profile_.user.c_str(),
                                        secret_->c_str());
    } else if (method == "privatekey") {
        // passphrase 可选：无口令私钥传空串；identity_file 空＝SSH 默认（~/.ssh/id_*）。
        const char *passphrase = secret_.has_value() ? secret_->c_str() : "";
        auth_rc = ssh_userauth_privatekey_file(session_,
                                               profile_.user.empty() ? nullptr
                                                                     : profile_.user.c_str(),
                                               profile_.identity_file.empty()
                                                   ? nullptr
                                                   : profile_.identity_file.c_str(),
                                               passphrase);
    } else if (method == "agent") {
        auth_rc = ssh_userauth_agent(session_, profile_.user.empty() ? nullptr
                                                                     : profile_.user.c_str());
    } else {  // keyboard-interactive：单提示一次作答（本期边界，见头部）。
        auth_rc = ssh_userauth_kbdint(session_, profile_.user.empty() ? nullptr
                                                                      : profile_.user.c_str(),
                                      nullptr);
        while (auth_rc == SSH_AUTH_INFO) {
            const auto nprompts = ssh_userauth_kbdint_getnprompts(session_);
            for (int i = 0; i < nprompts; ++i) {
                // 非回显提示（口令类）且有答案才作答；其余提示留空让服务器裁决。
                auto echo = char{0};
                static_cast<void>(ssh_userauth_kbdint_getprompt(session_,
                                                                static_cast<unsigned int>(i),
                                                                &echo));
                if (secret_.has_value() && echo == 0) {
                    ssh_userauth_kbdint_setanswer(session_, static_cast<unsigned int>(i),
                                                  secret_->c_str());
                }
            }
            auth_rc = ssh_userauth_kbdint(session_, nullptr, nullptr);
        }
    }
    // 秘密材料使命完成，立即清副本（CONN.09：明文不比认证活得久）。
    secret_.reset();

    if (auth_rc != SSH_AUTH_SUCCESS) {
        AURORA_LOG_ERROR("conn", "ssh: auth (", method, ") failed for '", profile_.host,
                         "': ", ssh_get_error(session_));
        events_->on_closed();
        return;
    }

    // ---- 通道 + PTY + shell ----
    {
        const std::lock_guard lock{state_mutex_};
        channel_ = ssh_channel_new(session_);
    }
    if (channel_ == nullptr || ssh_channel_open_session(channel_) != SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: cannot open channel: ", ssh_get_error(session_));
        events_->on_closed();
        return;
    }
    const auto cols = static_cast<int>(std::clamp(std::max(size_.columns, std::size_t{1}),
                                                  std::size_t{1}, std::size_t{32767}));
    const auto rows = static_cast<int>(std::clamp(std::max(size_.rows, std::size_t{1}),
                                                  std::size_t{1}, std::size_t{32767}));
    // PTY 与首尺寸一次到位（0.12 的 request_pty 不再收 TERM 参数，走带尺寸的变体）。
    if (ssh_channel_request_pty_size(channel_, std::string{kPtyTerm}.c_str(), cols, rows) !=
        SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: pty request failed: ", ssh_get_error(session_));
        events_->on_closed();
        return;
    }
    if (ssh_channel_request_shell(channel_) != SSH_OK) {
        AURORA_LOG_ERROR("conn", "ssh: shell request failed: ", ssh_get_error(session_));
        events_->on_closed();
        return;
    }
    channel_open_.store(true);

    // ---- 读循环（非阻塞读 + 空转休眠；closing 标志是唯一停机令）----
    auto buffer = std::vector<std::byte>(kReadChunkBytes);
    for (;;) {
        if (closing_.load()) {
            break;
        }
        const auto bytes_read = ssh_channel_read_nonblocking(channel_, buffer.data(),
                                                             static_cast<std::uint32_t>(
                                                                 buffer.size()),
                                                             0);
        if (bytes_read > 0) {
            events_->on_bytes({buffer.data(), static_cast<std::size_t>(bytes_read)});
            continue;
        }
        if (bytes_read == SSH_ERROR || ssh_channel_is_closed(channel_)) {
            break;
        }
        if (bytes_read == 0 && ssh_channel_is_eof(channel_)) {
            break;
        }
        ::usleep(kReadIdleUs);
    }
    channel_open_.store(false);
    events_->on_closed();
}

auto SshConnection::write(std::span<const std::byte> bytes) -> void {
    const std::lock_guard lock{write_mutex_};
    if (closing_.load() || !channel_open_.load()) {
        return;
    }
    auto remaining = bytes;
    while (!remaining.empty()) {
        const auto written = ssh_channel_write(channel_, remaining.data(),
                                               static_cast<std::uint32_t>(remaining.size()));
        if (written <= 0) {
            return;  // 对端已走或缓冲异常：与 forkpty 腿同语义，键入丢弃。
        }
        remaining = remaining.subspan(static_cast<std::size_t>(written));
    }
}

auto SshConnection::resize(session::Size size) -> void {
    const std::lock_guard lock{state_mutex_};
    if (closing_.load() || !channel_open_.load()) {
        return;
    }
    size_ = size;
    const auto cols = static_cast<int>(std::clamp(std::max(size.columns, std::size_t{1}),
                                                  std::size_t{1}, std::size_t{32767}));
    const auto rows = static_cast<int>(std::clamp(std::max(size.rows, std::size_t{1}),
                                                  std::size_t{1}, std::size_t{32767}));
    if (ssh_channel_change_pty_size(channel_, cols, rows) != SSH_OK) {
        AURORA_LOG_WARN("conn", "ssh: resize rejected: ", ssh_get_error(session_));
    }
}

auto SshConnection::close() -> void {
    if (closing_.exchange(true)) {
        if (reader_.joinable()) {
            reader_.join();
        }
        return;
    }
    if (reader_.joinable()) {
        reader_.join();  // 读线程每 30ms 查 closing，这里至多等一个休眠步长。
    }
    teardown();
}

auto SshConnection::teardown() -> void {
    const std::lock_guard lock{state_mutex_};
    if (channel_ != nullptr) {
        ssh_channel_close(channel_);
        ssh_channel_free(channel_);
        channel_ = nullptr;
    }
    if (session_ != nullptr) {
        ssh_disconnect(session_);
        ssh_free(session_);
        session_ = nullptr;
    }
    channel_open_.store(false);
}

auto SshConnection::alive() const noexcept -> bool {
    return started_.load() && !closing_.load() && channel_open_.load();
}

}  // namespace borealis::conn
