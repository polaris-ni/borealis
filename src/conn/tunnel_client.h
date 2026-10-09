#pragma once

// ============================================================
// SSH 隧道传输腿（src/conn/tunnel_client.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.08 的三式转发实现（裁决 7.96 口径）：
// - 每隧道一条独立 SSH 会话：建连/认证复用 conn/ssh_dial 共用腿
//   （口径同 SFTP 面板「打开即连」），不占用终端会话的读线程；
// - 每隧道一个工作线程：拨号→监听→串行受理→退避重连，全部阻塞 IO
//   都关在该线程内（AGENTS.md §4.5 第 25 条）；
// - 本端监听/字节流经 platform/tcp.h 抽象（裸 socket 不外溢，第 23 条）。
//
// 状态事件在工作线程上触发——消费方不得直接触达 UI 状态，须经有界队列
// 投递（口径同 session::ConnectionEvents，架构 §3.2）。
//
// 凭据纪律（CONN.09，裁决 7.93）：secret 明文随对象存活至隧道停止——这是
// 「自动重连需要重复认证」的必然代价，口径同 SshConnection::secret_：
// 只在本对象内存、不落盘、不进日志；析构/stop 后清空。
// ============================================================

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

#include "borealis/conn/profile.h"
#include "conn/tunnel_model.h"

namespace borealis::conn {

/// @brief 隧道状态出口（工作线程回调，消费方自行投递回 UI）。
///
/// @p bound_port 为生效监听端口：-R 的 0 端口择定结果只有服务器知道，
/// 故 Active 事件顺带回报；本地式即配置端口。
class TunnelEvents {
  public:
    TunnelEvents() = default;
    virtual ~TunnelEvents() = default;

    TunnelEvents(const TunnelEvents &) = delete;
    auto operator=(const TunnelEvents &) -> TunnelEvents & = delete;

    virtual auto on_tunnel_state(TunnelState state, TunnelError error, int bound_port) -> void = 0;
};

/// @brief 一条隧道：定义、生命周期与它专属的工作线程。
///
/// 生命周期：构造 → start() → （运行/退避/…） → stop()/析构。start() 立即
/// 返回；stop() 置停止标志并 join，等待上界＝一个轮询拍（200 ms）+ 一次
/// 建连超时，与 SshConnection::close() 同款纪律。
class Tunnel final {
  public:
    /// @param profile 承载隧道拨号的 SSH 档案（装配层按 spec.profile_id 解析）。
    /// @param secret  已解析的秘密材料，口径同 ssh_dial_and_authenticate；
    ///                每次重拨在副本上消费，原值留到隧道停止（见头注释）。
    /// @param spec    隧道定义（形态/监听点/目标）。
    /// @param policy  失败重试策略。
    Tunnel(const SshProfile &profile, std::optional<std::string> secret, TunnelSpec spec,
           RetryPolicy policy = {});

    Tunnel(const Tunnel &) = delete;
    auto operator=(const Tunnel &) -> Tunnel & = delete;
    Tunnel(Tunnel &&) = delete;
    auto operator=(Tunnel &&) -> Tunnel & = delete;

    ~Tunnel();

    /// @brief 启动工作线程。重复调用（运行中）幂等忽略。
    auto start(TunnelEvents &events) -> void;

    /// @brief 停止并 join（未启动时幂等）。
    auto stop() -> void;

    /// @brief 当前状态（原子读，UI 线程可问）。
    [[nodiscard]] auto state() const -> TunnelState;

    /// @brief 最近一次 Active 的生效监听端口（非 Active 期回配置值）。
    [[nodiscard]] auto bound_listen_port() const -> int;

  private:
    /// @brief 工作线程主体：拨号→监听→受理→退避重连（见 .cpp）。
    auto run_loop() -> void;

    /// @brief 投一次状态事件（同态去抖：状态与错误都没变则不投）。
    auto emit(TunnelState state, TunnelError error) -> void;

    SshProfile profile_;
    std::optional<std::string> secret_;
    TunnelSpec spec_;
    RetryPolicy policy_;
    TunnelEvents *events_ = nullptr;

    std::thread worker_;
    std::atomic<bool> closing_{false};
    std::atomic<int> state_{static_cast<int>(TunnelState::Stopped)};
    std::atomic<int> bound_port_{0};
    /// 事件去抖基准（仅工作线程读写）。
    TunnelState last_emitted_{TunnelState::Stopped};
    TunnelError last_emitted_error_{TunnelError::None};
};

}  // namespace borealis::conn
