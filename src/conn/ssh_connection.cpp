// ============================================================
// SSH 真传输连接实现（src/conn/ssh_connection.cpp）
// ------------------------------------------------------------
// 本文件是 SPEC.FEAT.CONN.02 集成式 SSH 传输在 Linux 腿的通道与读循环知识：
// PTY 通道、shell 请求与字节流泵；建会话选项、连接、主机密钥核对（D3②/D8 裁决）
// 与认证在共用腿 conn/ssh_dial.cpp（SFTP 浏览器 SPEC.FEAT.CONN.04 同款消费）。
// 对照 forkpty 腿的三条纪律同样适用：
//   1. 连接/认证/读都在读线程上做——ssh_connect 会阻塞数秒，绝不占 UI 线程。
//   2. close() 先置 closing 再 join 读线程，最后才释放 libssh 句柄，回调不悬空。
//   3. alive() 只读原子量，免锁（UI 线程随时会问）。
// 凭据（CONN.09）：明文只在构造传入的 secret_ 里存活，认证完成后即清空；
// 错误日志只打 libssh 的错误文本（不含口令），绝不回显秘密材料。
// ============================================================

#include "ssh_connection.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include <libssh/libssh.h>

#include "aurora/core/log.h"
#include "conn/ssh_dial.h"

namespace borealis::conn {

namespace {

/// @brief 读循环单块上限：与 forkpty 腿同款，原样整块投递给会话层。
constexpr std::size_t kReadChunkBytes = 32768;

/// @brief 非阻塞读空转的休眠步长：close() 靠 closing 标志叫停循环，步长即关闭延迟上界。
constexpr useconds_t kReadIdleUs = 30000;

/// @brief PTY 请求的 TERM 值：与本地腿 `kPtyDefaultEnvironment` 的 TERM 同源口径。
constexpr std::string_view kPtyTerm = "xterm-256color";

}  // namespace

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

    // ---- 连接 → 主机密钥核对 → 认证（共用腿 ssh_dial，SFTP 浏览器同款）----
    // 秘密材料移交给 dial 腿，成员处立即清引用（CONN.09：明文不比认证活得久）。
    auto secret = std::move(secret_);
    secret_.reset();
    if (!ssh_dial_and_authenticate(profile_, std::move(secret), session_)) {
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
