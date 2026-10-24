// ============================================================
// SSH 跳链建立与逐跳中继实现（src/conn/ssh_hop_chain.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.03` 跳板机（稿 D4）的自建 `direct-tcpip` 路线：跳板会话只是
// 一台普通 SSH 服务器，「到达下一跳」这件事在它身上表现为一条转发通道 + 一根只搬
// 字节的线程。逐跳的建连/核对/认证全部交给 `ssh_dial_and_authenticate()`，本文件
// 因此不含任何自己的策略裁决（稿 D1 的结构性收益：每一跳吃本仓自己的口径）。
//
// 中继线程的形态取自 `conn/tunnel_client.cpp` 的 `pump_channel()`（同一条仓内先例：
// 按拍轮询、双向搬运、任一方向收摊即退），而不是库的 `ssh_connector` 族——后者在
// 隧道腿里已按弃用处理，且一对 connector 只能服务一条通道，与「停止位 + join」的
// 关闭序（F12）配合不起来。偏差已回写稿 D4。
// ============================================================

#include "conn/ssh_hop_chain.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <libssh/libssh.h>

#include "aurora/core/log.h"
#include "conn/proxy_jump.h"
#include "conn/ssh_dial.h"
#include "platform/hop_pipe.h"

namespace borealis::conn {

namespace {

/// @brief 中继线程的一拍：既是 `seed` 侧读的等待上界，也是 `close()` join 的等待上界。
constexpr int kRelayTickMs = 10;

/// @brief 中继单块缓冲（与隧道泵同档：16 KiB 一跳，链路 N 跳就是 N 份这么大的工作缓冲）。
constexpr std::size_t kRelayChunkBytes = 16384;

/// @brief 转发通道的「发起方」标识：本仓的 seed 永远来自本机内存里的一对管道，
///        没有真实源端口可报（稿 §5 建立序里的写法，与探针 P1 一致）。
constexpr std::string_view kRelayOriginator = "127.0.0.1";
constexpr int kRelayOriginatorPort = 65535;

/// @brief 一跳折成一份临时档案：跳板本身不再套跳板（稿 D2 的射程边界），也不转发 agent。
[[nodiscard]] auto hop_profile_of(const SshHop &hop) -> SshProfile {
    auto profile = SshProfile{};
    profile.host = hop.host;
    profile.port = hop.port;
    profile.user = hop.user;
    profile.auth_method = hop.auth_method;
    profile.identity_file = hop.identity_file;
    profile.agent_forwarding = false;
    profile.known_hosts_policy = hop.known_hosts_policy;
    profile.credential = hop.credential;
    return profile;
}

/// @brief 全量写入 libssh 通道（阻塞模式仍可能部分写；形态同隧道腿的 `write_channel`）。
[[nodiscard]] auto write_channel(ssh_channel_struct *channel, std::span<const std::uint8_t> bytes)
    -> bool {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto written = ssh_channel_write(channel, bytes.data() + offset,
                                               static_cast<std::uint32_t>(bytes.size() - offset));
        if (written <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

/// @brief 通道 ↔ 本地句柄之间的双向泵：任一方向收摊即退，退出后由 `close()` 释放两侧。
///
/// 只在本跳的中继线程里跑：该跳的 libssh 会话因此**只被这一根线程**触碰（libssh 会话
/// 不跨线程），`open()` 建完之后主线程不再碰它，直到 join 之后再拆。
auto relay_loop(ssh_channel_struct *channel,
                platform::HopPipeEnd &seed_side,
                const std::atomic<bool> &stopping) -> void {
    auto buffer = std::array<std::uint8_t, kRelayChunkBytes>{};
    auto seed_open = true;     ///< 本地句柄侧还能说话。
    auto channel_open = true;  ///< 转发通道侧还能说话。
    while (!stopping.load(std::memory_order_relaxed) && seed_open && channel_open) {
        // 方向一：seed（下一跳或目标的 SSH 报文）→ 转发通道。这一读就是一拍。
        const auto read = seed_side.read(std::span<std::uint8_t>{buffer}, kRelayTickMs);
        switch (read.outcome) {
            case platform::HopPipeReadOutcome::Data:
                if (!write_channel(
                        channel,
                        std::span<const std::uint8_t>{buffer.data(), read.bytes})) {
                    channel_open = false;
                }
                break;
            case platform::HopPipeReadOutcome::Closed:
                seed_open = false;
                static_cast<void>(ssh_channel_send_eof(channel));
                break;
            case platform::HopPipeReadOutcome::Error:
                seed_open = false;
                break;
            case platform::HopPipeReadOutcome::Timeout:
                break;
        }
        if (!channel_open) {
            break;
        }
        // 方向二：转发通道 → seed。方向一已等过一拍，这里零等；seed 收摊后才轮到它等。
        const auto available =
            ssh_channel_poll_timeout(channel, seed_open ? 0 : kRelayTickMs, 0);
        if (available == SSH_EOF || available < 0) {
            channel_open = available == SSH_EOF;
            break;
        }
        while (available > 0) {
            const auto got = ssh_channel_read_nonblocking(
                channel, buffer.data(), static_cast<uint32_t>(buffer.size()), 0);
            if (got <= 0) {
                break;  // 空了（或出错）：下一拍再说。
            }
            if (!seed_side.write(std::span<const std::uint8_t>{buffer.data(),
                                                               static_cast<std::size_t>(got)})) {
                seed_open = false;
                break;
            }
        }
    }
    // 两侧任一到尽头就收：关掉本地这一端，对面的会话读到 socket 关闭即知链路没了。
    // （幂等：`close()` 里还会再关一次。）
    seed_side.close();
}

}  // namespace

/// @brief 一跳的现场：会话、通往下一跳的转发通道、那一对本地句柄、搬字节的那根线程。
///
/// 堆上稳定（`unique_ptr` 持有）是硬要求——中继线程按指针/引用持有其中的通道与句柄端。
struct SshHopChain::Impl {
    struct Hop {
        ssh_session_struct *session{nullptr};
        ssh_channel_struct *channel{nullptr};
        std::unique_ptr<platform::HopPipeEnd> session_end{};  ///< 交给下一跳（或目标）的一端。
        std::unique_ptr<platform::HopPipeEnd> relay_end{};    ///< 本跳中继持有的一端。
        std::shared_ptr<std::atomic<bool>> stopping{};
        std::thread relay{};
    };

    std::vector<std::unique_ptr<Hop>> hops{};
    DialFailure failure{};
    std::uintptr_t seed{0};

    /// @brief 逆序收口全部已建好的跳（幂等；析构、`close()`、失败路径三处共用）。
    auto close_all() -> void {
        for (auto it = hops.rbegin(); it != hops.rend(); ++it) {
            teardown(**it);
        }
        hops.clear();
        seed = 0;
    }

    /// @brief 单跳的关闭序（稿 §5 的 F12：先 join 后释放，一步都不许越）。
    static auto teardown(Hop &hop) -> void {
        if (hop.stopping != nullptr) {
            hop.stopping->store(true, std::memory_order_relaxed);
        }
        if (hop.relay.joinable()) {
            hop.relay.join();
        }
        if (hop.channel != nullptr) {
            static_cast<void>(ssh_channel_close(hop.channel));
            ssh_channel_free(hop.channel);
            hop.channel = nullptr;
        }
        if (hop.session != nullptr) {
            ssh_disconnect(hop.session);
            ssh_free(hop.session);
            hop.session = nullptr;
        }
        if (hop.relay_end != nullptr) {
            hop.relay_end->close();
        }
        if (hop.session_end != nullptr) {
            // 吃这一端的会话（下一跳或目标）已在更早的迭代里断开——逆序保证的正是这件事。
            hop.session_end->close();
        }
    }
};

SshHopChain::SshHopChain() = default;

SshHopChain::~SshHopChain() { close(); }

auto SshHopChain::open(const SshProfile &target,
                       const std::vector<std::optional<std::string>> &hop_secrets,
                       const SshAlgorithmOverrides &algorithms) -> bool {
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    auto &state = *impl_;
    state.close_all();  // 重拨＝整链重开（稿 D5：跳板不单独留环），也兜住失败路径的残留。
    state.failure = DialFailure{};

    const auto &hops = target.proxy_jumps;
    if (hops.empty()) {
        return true;  // 0 跳：没有链路要建，seed 保持 0（调用方按直连拨）。
    }
    // 合法性闸在这里（稿 D2/D3：读侧只留痕不吞链，坏值不给拨号的机会）。
    if (!validate_proxy_hops(hops)) {
        AURORA_LOG_ERROR("conn", "ssh: hop chain refused: invalid proxy jump list (",
                         hops.size(), " hop(s)) for target '", target.host, "'");
        state.failure = DialFailure{DialOutcome::Hop, 0};
        return false;
    }

    auto next_seed = std::uintptr_t{0};  // 第 0 跳走真 TCP（稿 §5 建立序）。
    for (std::size_t index = 0; index < hops.size(); ++index) {
        auto hop = std::make_unique<Impl::Hop>();
        const auto profile = hop_profile_of(hops[index]);
        hop->session = ssh_new();
        if (hop->session == nullptr) {
            AURORA_LOG_ERROR("conn", "ssh: hop ", index, ": cannot allocate ssh session");
            state.failure = DialFailure{DialOutcome::Unallocated, index};
            state.close_all();
            return false;
        }
        const auto secret = index < hop_secrets.size() ? hop_secrets[index]
                                                      : std::optional<std::string>{};
        const auto outcome =
            ssh_dial_and_authenticate(profile, secret, hop->session, algorithms, next_seed);
        if (outcome != DialOutcome::Ok) {
            ssh_free(hop->session);
            hop->session = nullptr;
            AURORA_LOG_ERROR("conn", "ssh: hop ", index, " (", profile.host, ":",
                             std::to_string(effective_port(profile)), ") failed with outcome ",
                             static_cast<int>(outcome));
            state.failure = DialFailure{outcome, index};
            state.close_all();
            return false;
        }

        // 本跳已认证：为「下一跳或目标」开一条 direct-tcpip 通道（稿 D1 路线的唯一库侧动作）。
        // 目的地两式：链上还有跳就转发到下一跳，否则转发到目标——目标的 host/port 只在
        // `SshProfile` 那一份上，故 `open()` 的形参是目标档案而不是裸跳链（稿 D4 的补注）。
        const auto is_last_hop = index + 1 >= hops.size();
        const auto &destination = is_last_hop ? target.host : hops[index + 1].host;
        const auto destination_port =
            is_last_hop ? effective_port(target) : hops[index + 1].port;
        hop->channel = ssh_channel_new(hop->session);
        if (hop->channel == nullptr) {
            AURORA_LOG_ERROR("conn", "ssh: hop ", index, ": cannot allocate forward channel");
            ssh_free(hop->session);
            hop->session = nullptr;
            state.failure = DialFailure{DialOutcome::Network, index};
            state.close_all();
            return false;
        }
        const auto originator = std::string{kRelayOriginator};
        if (ssh_channel_open_forward(hop->channel, destination.c_str(), destination_port,
                                     originator.c_str(), kRelayOriginatorPort) != SSH_OK) {
            AURORA_LOG_ERROR("conn", "ssh: hop ", index, " refused to forward to ", destination,
                             ":", std::to_string(destination_port),
                             ": ", ssh_get_error(hop->session));
            ssh_channel_free(hop->channel);
            hop->channel = nullptr;
            ssh_free(hop->session);
            hop->session = nullptr;
            state.failure = DialFailure{DialOutcome::Network, index};
            state.close_all();
            return false;
        }
        auto pair = platform::make_hop_pair();
        if (!pair) {
            ssh_channel_free(hop->channel);
            hop->channel = nullptr;
            ssh_free(hop->session);
            hop->session = nullptr;
            state.failure = DialFailure{DialOutcome::Network, index};
            state.close_all();
            return false;
        }
        hop->session_end = std::move(pair->session_end);
        hop->relay_end = std::move(pair->relay_end);
        hop->stopping = std::make_shared<std::atomic<bool>>(false);
        auto *channel_for_thread = hop->channel;
        auto *relay_end_for_thread = hop->relay_end.get();
        auto stopping_for_thread = hop->stopping;
        hop->relay = std::thread([channel_for_thread, relay_end_for_thread, stopping_for_thread] {
            relay_loop(channel_for_thread, *relay_end_for_thread, *stopping_for_thread);
        });
        next_seed = hop->session_end->handle();
        state.seed = next_seed;
        state.hops.push_back(std::move(hop));
    }
    return true;
}

auto SshHopChain::failure() const noexcept -> DialFailure {
    return impl_ == nullptr ? DialFailure{} : impl_->failure;
}

auto SshHopChain::seed_fd() const noexcept -> std::uintptr_t {
    return impl_ == nullptr ? 0U : impl_->seed;
}

auto SshHopChain::close() -> void {
    if (impl_ != nullptr) {
        impl_->close_all();
    }
}

}  // namespace borealis::conn
