// ============================================================
// SSH 隧道传输腿实现（src/conn/tunnel_client.cpp）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.08：工作线程内的「拨号→监听→串行受理→退避重连」主循环。
// 线程纪律同 SshConnection（架构 §3.1）：一切阻塞 IO 关在工作线程；
// 状态事件经 TunnelEvents 在工作线程上触发，消费方负责投递回 UI。
// ============================================================

#include "conn/tunnel_client.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <libssh/libssh.h>

#include "aurora/core/log.h"
#include "conn/ssh_dial.h"
#include "conn/tunnel_socks.h"
#include "platform/tcp.h"

namespace borealis::conn {

namespace {

/// @brief 工作线程轮询拍：accept/read/会话探活的统一超时（stop() 的 join
///        等待上界即此值 + 一次建连超时）。
constexpr int kTickMs = 200;

/// @brief 转发泵单块缓冲。
constexpr std::size_t kChunkBytes = 16384;

/// @brief SOCKS5 握手的整体耐心（对端发呆不能拖死受理循环——超时即弃连接）。
constexpr int kSocksBudgetMs = 10000;

/// @brief -R 回程连本地目标的 TCP 建连超时。
constexpr int kLocalConnectTimeoutMs = 5000;

/// @brief 全量写入 libssh 通道（阻塞模式仍可能部分写）。
[[nodiscard]] auto write_channel(ssh_channel_struct *channel, std::span<const std::uint8_t> bytes)
    -> bool {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const int n = ssh_channel_write(channel, bytes.data() + offset,
                                        static_cast<std::uint32_t>(bytes.size() - offset));
        if (n <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(n);
    }
    return true;
}

/// @brief 一条已开通道与一条本地 TCP 流之间的双向泵；任一方向断即收尾。
///        回 None＝干净结束（本端收摊或通道到 EOF）；ChannelLost＝会话级错误。
auto pump_channel(ssh_channel_struct *channel, platform::TcpStream &stream,
                  const std::atomic<bool> &closing) -> TunnelError {
    std::vector<std::uint8_t> buffer(kChunkBytes);
    bool tcp_to_channel = true;
    bool channel_to_tcp = true;
    while (!closing.load(std::memory_order_relaxed) && channel_to_tcp) {
        if (tcp_to_channel) {
            const std::span<std::uint8_t> out{buffer.data(), buffer.size()};
            const auto read = stream.read(out, kTickMs);
            switch (read.outcome) {
            case platform::TcpReadOutcome::Data:
                if (!write_channel(channel, std::span<const std::uint8_t>{out.data(), read.bytes})) {
                    return TunnelError::ChannelLost;
                }
                break;
            case platform::TcpReadOutcome::Closed:
                tcp_to_channel = false;
                ssh_channel_send_eof(channel);
                break;
            case platform::TcpReadOutcome::Error:
                return TunnelError::ChannelLost;
            case platform::TcpReadOutcome::Timeout:
                break;
            }
        }
        // 通道侧：本端还在说话时零等（不让读通道饿着 TCP 侧），收摊后转等待尾包。
        const int poll_ms = tcp_to_channel ? 0 : kTickMs;
        const int available = ssh_channel_poll_timeout(channel, poll_ms, 0);
        if (available == SSH_EOF) {
            break;  // 目标侧到 EOF：本方向收尾，整个连接收摊。
        }
        if (available < 0) {
            return closing.load(std::memory_order_relaxed) ? TunnelError::None
                                                           : TunnelError::ChannelLost;
        }
        if (available > 0) {
            while (true) {
                const int n = ssh_channel_read_nonblocking(channel, buffer.data(),
                                                           static_cast<std::uint32_t>(buffer.size()),
                                                           0);
                if (n <= 0) {
                    break;
                }
                const std::span<const std::uint8_t> bytes{buffer.data(),
                                                          static_cast<std::size_t>(n)};
                if (!stream.write(bytes)) {
                    return TunnelError::ChannelLost;
                }
            }
        }
        if (ssh_channel_is_closed(channel) || ssh_channel_is_eof(channel)) {
            channel_to_tcp = false;
        }
    }
    stream.close();
    return closing.load(std::memory_order_relaxed) ? TunnelError::None : TunnelError::None;
}

/// @brief 关闭并释放通道（幂等腿收尾，两式共用）。
auto close_channel(ssh_channel_struct *channel) -> void {
    if (channel == nullptr) {
        return;
    }
    ssh_channel_close(channel);
    ssh_channel_free(channel);
}

/// @brief SOCKS5 握手推进：读—喂—回应答，直到拿到可拨目标或失败。
///        成功回 true 并带出 target；失败已把协议应答发回客户端。
auto socks_negotiate(platform::TcpStream &stream, Socks5Negotiator &negotiator,
                     const std::atomic<bool> &closing, SocksTarget &target) -> bool {
    std::vector<std::uint8_t> buffer(64);
    int budget_ms = kSocksBudgetMs;
    while (budget_ms > 0 && !closing.load(std::memory_order_relaxed)) {
        const std::span<std::uint8_t> out{buffer.data(), buffer.size()};
        const auto read = stream.read(out, kTickMs);
        budget_ms -= kTickMs;
        if (read.outcome == platform::TcpReadOutcome::Closed ||
            read.outcome == platform::TcpReadOutcome::Error) {
            return false;
        }
        if (read.outcome == platform::TcpReadOutcome::Timeout) {
            continue;
        }
        const auto verdict =
            negotiator.feed(std::span<const std::uint8_t>{out.data(), read.bytes});
        auto reply = negotiator.take_pending_reply();
        if (!reply.empty() && !stream.write(std::span<const std::uint8_t>{reply})) {
            return false;
        }
        if (verdict == SocksVerdict::ReadyToConnect) {
            target = negotiator.target();
            return true;
        }
        if (verdict != SocksVerdict::NeedMore) {
            return false;  // 协议应答已发出（版本/认证/命令/地址类型拒绝）。
        }
    }
    return false;
}

/// @brief 向 SOCKS 客户端回「常规失败」（拨号目标失败时用，RFC 1928 REP=1）。
auto socks_send_general_failure(platform::TcpStream &stream) -> void {
    const std::uint8_t reply[] = {0x05, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    stream.write(std::span<const std::uint8_t>{reply});
}

/// @brief Local/Dynamic 受理循环：accept 一条、串行泵一条（裁决 7.96：
///        本期串行受理，并发多连接为延后子项）。
auto serve_local_side(ssh_session_struct *session, const TunnelSpec &spec,
                      platform::TcpListener &listener, const std::atomic<bool> &closing)
    -> TunnelError {
    while (!closing.load(std::memory_order_relaxed)) {
        if (ssh_is_connected(session) == 0) {
            return TunnelError::ChannelLost;
        }
        auto stream = listener.accept(kTickMs);
        if (stream == nullptr) {
            continue;
        }
        std::string dest_host;
        int dest_port = 0;
        Socks5Negotiator negotiator;
        if (spec.kind == TunnelKind::Dynamic) {
            SocksTarget target;
            if (!socks_negotiate(*stream, negotiator, closing, target)) {
                continue;  // 握手失败属该连接，隧道本体无恙。
            }
            dest_host = std::move(target.host);
            dest_port = target.port;
        } else {
            dest_host = spec.target_host;
            dest_port = spec.target_port;
        }
        ssh_channel_struct *channel = ssh_channel_new(session);
        if (channel == nullptr) {
            return TunnelError::ChannelLost;
        }
        const std::string originator =
            stream->peer_address().empty() ? std::string{"127.0.0.1"} : stream->peer_address();
        const int origin_port = stream->peer_port();
        if (ssh_channel_open_forward(channel, dest_host.c_str(), dest_port, originator.c_str(),
                                     origin_port) != SSH_OK) {
            AURORA_LOG_WARN("conn", "tunnel: open forward to ", dest_host, ":",
                            std::to_string(dest_port),
                            " failed: ", ssh_get_error(session));
            close_channel(channel);
            if (spec.kind == TunnelKind::Dynamic) {
                socks_send_general_failure(*stream);
            }
            continue;
        }
        if (spec.kind == TunnelKind::Dynamic) {
            negotiator.append_connect_ok();
            const auto reply = negotiator.take_pending_reply();
            if (!reply.empty()) {
                stream->write(std::span<const std::uint8_t>{reply});
            }
        }
        const auto error = pump_channel(channel, *stream, closing);
        close_channel(channel);
        if (error != TunnelError::None || closing.load(std::memory_order_relaxed)) {
            return error;
        }
        // 会话若被这一条连接拖垮（服务器断链），下一拍顶部探活会转退避。
    }
    return TunnelError::None;
}

/// @brief Remote 受理循环：服务器推来的连接逐个回连本地目标。
///        ssh_channel_open_forward_port 是 libssh 现存的排队接口；更现代的
///        ssh_connector 族与本期形态不匹配（逐连接取队列），故按弃用告警
///        抑制消费（Aurora 主仓缺口分流之外，应用侧只等 vcpkg 版本推进）。
auto serve_remote_side(ssh_session_struct *session, const TunnelSpec &spec,
                       const std::atomic<bool> &closing) -> TunnelError {
    while (!closing.load(std::memory_order_relaxed)) {
        if (ssh_is_connected(session) == 0) {
            return TunnelError::ChannelLost;
        }
        int destination_port = 0;
        char *originator = nullptr;
        int originator_port = 0;
        ssh_channel_struct *channel = nullptr;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        channel = ssh_channel_open_forward_port(session, kTickMs, &destination_port, &originator,
                                                &originator_port);
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
        if (originator != nullptr) {
            ssh_string_free_char(originator);
        }
        if (channel == nullptr) {
            continue;  // 一拍无客。
        }
        auto stream = platform::tcp_connect(spec.target_host, spec.target_port,
                                            kLocalConnectTimeoutMs);
        if (stream == nullptr) {
            AURORA_LOG_WARN("conn", "tunnel: remote request cannot reach local target ",
                            spec.target_host, ":", std::to_string(spec.target_port));
            close_channel(channel);
            continue;
        }
        const auto error = pump_channel(channel, *stream, closing);
        close_channel(channel);
        if (error != TunnelError::None || closing.load(std::memory_order_relaxed)) {
            return error;
        }
    }
    return TunnelError::None;
}

/// @brief 按 100 ms 切片睡眠并检查停止标志（退避等待期 stop() 不等满）。
auto sleep_checked(const std::atomic<bool> &closing, int total_ms) -> void {
    while (total_ms > 0 && !closing.load(std::memory_order_relaxed)) {
        const int slice = total_ms < 100 ? total_ms : 100;
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        total_ms -= slice;
    }
}

}  // namespace

Tunnel::Tunnel(const SshProfile &profile, std::optional<std::string> secret, TunnelSpec spec,
               RetryPolicy policy)
    : profile_(profile), secret_(std::move(secret)), spec_(std::move(spec)), policy_(policy) {}

Tunnel::~Tunnel() { stop(); }

auto Tunnel::start(TunnelEvents &events) -> void {
    if (worker_.joinable()) {
        return;
    }
    events_ = &events;
    if (validate_tunnel_spec(spec_) != TunnelSpecIssue::None) {
        AURORA_LOG_ERROR("conn", "tunnel: invalid spec, refused to start");
        emit(TunnelState::Failed, TunnelError::BindFailed);
        return;
    }
    closing_.store(false);
    worker_ = std::thread([this] { run_loop(); });
}

auto Tunnel::stop() -> void {
    closing_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    secret_.reset();  // CONN.09：停止即清内存副本。
}

auto Tunnel::state() const -> TunnelState {
    return static_cast<TunnelState>(state_.load(std::memory_order_relaxed));
}

auto Tunnel::bound_listen_port() const -> int {
    const int bound = bound_port_.load(std::memory_order_relaxed);
    return bound > 0 ? bound : spec_.listen_port;
}

auto Tunnel::emit(TunnelState state, TunnelError error) -> void {
    state_.store(static_cast<int>(state), std::memory_order_relaxed);
    if (state == last_emitted_ && error == last_emitted_error_) {
        return;
    }
    last_emitted_ = state;
    last_emitted_error_ = error;
    if (events_ != nullptr) {
        events_->on_tunnel_state(state, error, bound_listen_port());
    }
}

auto Tunnel::run_loop() -> void {
    int attempt = 0;
    TunnelState terminal = TunnelState::Stopped;
    TunnelError terminal_error = TunnelError::None;

    while (!closing_.load(std::memory_order_relaxed)) {
        emit(TunnelState::Dialing, TunnelError::None);
        ssh_session_struct *session = ssh_new();
        if (session == nullptr) {
            AURORA_LOG_ERROR("conn", "tunnel: cannot allocate ssh session");
            terminal = TunnelState::Failed;
            terminal_error = TunnelError::DialFailed;
            break;
        }
        const auto secret_copy = secret_;
        const bool dialed =
            ssh_dial_and_authenticate(profile_, secret_copy, session) == DialOutcome::Ok;

        TunnelError error{TunnelError::None};
        std::unique_ptr<platform::TcpListener> listener;
        int bound = spec_.listen_port;
        if (!dialed) {
            error = TunnelError::DialFailed;
        } else if (spec_.kind != TunnelKind::Remote) {
            listener = platform::tcp_listen(effective_listen_address(spec_), spec_.listen_port);
            if (listener == nullptr) {
                error = TunnelError::BindFailed;
            } else {
                bound = listener->bound_port();
            }
        } else {
            int server_bound = spec_.listen_port;
            const std::string bind = spec_.listen_address;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            // libssh 的 -R 请求/取消仍是 listen_forward/cancel_forward 族；
            // 与 open_forward_port 同理按抑制消费（裁决 7.96 登记）。
            if (ssh_channel_listen_forward(session, bind.c_str(), spec_.listen_port,
                                           &server_bound) != SSH_OK) {
                AURORA_LOG_WARN("conn", "tunnel: remote forward refused: ",
                                ssh_get_error(session));
                error = TunnelError::RemoteRefused;
            } else {
                bound = server_bound;
            }
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
        }

        if (error == TunnelError::None) {
            bound_port_.store(bound, std::memory_order_relaxed);
            attempt = 0;
            emit(TunnelState::Active, TunnelError::None);
            if (spec_.kind == TunnelKind::Remote) {
                error = serve_remote_side(session, spec_, closing_);
            } else {
                error = serve_local_side(session, spec_, *listener, closing_);
            }
            if (spec_.kind == TunnelKind::Remote && !closing_.load(std::memory_order_relaxed)) {
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
                ssh_channel_cancel_forward(session, spec_.listen_address.c_str(), bound);
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
            }
        }

        if (listener != nullptr) {
            listener->close();
        }
        if (dialed) {
            ssh_disconnect(session);
        }
        ssh_free(session);

        if (closing_.load(std::memory_order_relaxed) || error == TunnelError::None) {
            break;
        }
        const auto signal = signal_for_error(error);
        if (signal == TunnelSignal::FatalFailure || !should_retry(attempt + 1, policy_)) {
            terminal = TunnelState::Failed;
            terminal_error = error;
            break;
        }
        ++attempt;
        emit(TunnelState::Backoff, error);
        sleep_checked(closing_, retry_delay_ms(attempt, policy_));
    }

    secret_.reset();  // 认证材料不比对活得久（终态即清）。
    if (terminal == TunnelState::Failed) {
        emit(TunnelState::Failed, terminal_error);
    } else {
        emit(TunnelState::Stopped, TunnelError::None);
    }
}

}  // namespace borealis::conn
