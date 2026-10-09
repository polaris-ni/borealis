#pragma once

// ============================================================
// SSH 隧道纯逻辑模型（src/conn/tunnel_model.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.08（SSH 隧道，端口转发三式）的纯逻辑层：隧道定义
// （TunnelSpec）、形态校验与冲突判定、生命周期状态机、失败重试退避——
// 全部只依赖标准类型，不碰 libssh、不碰 socket、不碰 UI
// （AGENTS.md §4.4 第 20 条，utest_tunnel_model 无头单测）。
//
// 传输腿见 conn/tunnel_client.h：每隧道经 conn/ssh_dial 自开一条 SSH 会话
// （口径同 SFTP 面板「打开即连」，裁决 7.96），本层只回答「这条定义合不合法、
// 现在处于哪个状态、下次重试什么时候」。
//
// 三式语义（对齐 OpenSSH -L/-R/-D）：
// - Local：本端 listen → 每条接入连接开 direct-tcpip 通道到 target；
// - Remote：远端 listen → 服务器推来的连接转回本端 target（0 端口＝服务器择定）；
// - Dynamic：本端 listen → 每条接入连接按 SOCKS5 请求里的目标开通道。
// ============================================================

#include <cstdint>
#include <string>
#include <vector>

namespace borealis::conn {

/// @brief 隧道形态（需求口径的「三式」）。
enum class TunnelKind : std::uint8_t {
    Local,    ///< -L：本地监听，转发到远端 target。
    Remote,   ///< -R：远端监听，转发回本地 target。
    Dynamic,  ///< -D：本地监听 SOCKS5，目标逐请求决定。
};

/// @brief 一条隧道的定义。listen/target 的「哪端在监听」由 kind 决定：
///        Local/Dynamic 是本机，Remote 是 SSH 服务器。
struct TunnelSpec {
    std::string id;                    ///< 稳定唯一 id（与 Profile.id 同款纪律）。
    std::string name;                  ///< 展示名。
    TunnelKind kind{TunnelKind::Local};
    std::string listen_address;        ///< 空＝effective_listen_address() 的缺省。
    int listen_port{0};                ///< Remote 允许 0＝服务器择定端口。
    std::string target_host;           ///< Dynamic 不用（目标在 SOCKS 请求里）。
    int target_port{0};                ///< Dynamic 不用。
    std::string profile_id;            ///< 承载隧道拨号的 SSH 档案 id（装配层解析）。

    [[nodiscard]] auto operator==(const TunnelSpec &) const noexcept -> bool = default;
};

/// @brief 校验结论（None＝合法）。
enum class TunnelSpecIssue : std::uint8_t {
    None,                    ///< 合法。
    ListenPortInvalid,       ///< Local/Dynamic 监听端口不在 [1,65535]（Remote 允许 0）。
    TargetHostEmpty,         ///< Local/Remote 缺目标主机。
    TargetPortInvalid,       ///< Local/Remote 目标端口不在 [1,65535]。
};

/// @brief 监听地址缺省：空地址时 Local/Dynamic 缺省只绑回环（转发端口默认
///        不暴露给局域网，与 OpenSSH -L 的 GatewayPorts no 同口径）；
///        Remote 回空串——服务器侧缺省由 sshd 决定，本层不越权猜。
[[nodiscard]] auto effective_listen_address(const TunnelSpec &spec) -> std::string;

/// @brief 形态校验（见 TunnelSpecIssue）。Dynamic 携带 target 不算错——装配层
///        表单里改形态留下的残值按「不用」忽略。
[[nodiscard]] auto validate_tunnel_spec(const TunnelSpec &spec) -> TunnelSpecIssue;

/// @brief 两条定义是否会在同一侧抢同一个监听点（先比 kind 归属侧，再比
///        生效地址与端口）。Remote 的 0 端口由服务器择定，不可能预知冲突，
///        恒回 false；Remote 与非 Remote 分属两端，也恒回 false。
[[nodiscard]] auto specs_conflict(const TunnelSpec &a, const TunnelSpec &b) -> bool;

/// @brief 向量内是否存在互斥对（列表落盘/编辑前的整体闸）。
[[nodiscard]] auto has_conflict(const std::vector<TunnelSpec> &specs) -> bool;

/// @brief 隧道生命周期状态。
enum class TunnelState : std::uint8_t {
    Stopped,  ///< 未启动（或 stop() 之后）。
    Dialing,  ///< 正在建会话/认证（含重试的那一次）。
    Active,   ///< 监听点就绪，可受理连接。
    Backoff,  ///< 可恢复失败后退避等待中。
    Failed,   ///< 终态：配置类失败或重试穷尽，只待用户再启。
};

/// @brief 状态机的输入信号（传输腿在事件点上报）。
enum class TunnelSignal : std::uint8_t {
    Start,             ///< 用户启动 / 退避到点重拨。
    DialSucceeded,     ///< 认证通过且监听点就绪。
    RecoverableFailure,///< 网络/服务器瞬断一类：值得按退避重试。
    FatalFailure,      ///< 配置类失败（端口占用、-R 被服务器拒）：重试无义。
    Stop,              ///< 用户停止（任何状态都回 Stopped）。
};

/// @brief 状态迁移表：非法组合保持原态（幂等，传输腿可放心重复投）。
[[nodiscard]] auto next_state(TunnelState state, TunnelSignal signal) -> TunnelState;

/// @brief 状态是否意味着「还活着、还在干活」（UI 状态提示用，纯查询）。
[[nodiscard]] auto state_is_running(TunnelState state) -> bool;

/// @brief 重试策略（指数退避）。max_attempts==0 表示不限次数——需求只说
///        「失败自动重试」，未给穷尽上限；给 0 让人可随时 stop()。
struct RetryPolicy {
    int base_ms{1000};     ///< 首次退避。
    int cap_ms{30000};     ///< 退避上限（翻倍越过即钳住）。
    int max_attempts{0};   ///< 0＝不限；>0＝第 N 次失败后进 Failed。
};

/// @brief 第 attempt 次失败（自 1 起）后的退避毫秒数：base * 2^(attempt-1)，
///        钳到 cap；指数位移钳制在 30 内防溢出。attempt<1 按 1 处理。
[[nodiscard]] auto retry_delay_ms(int attempt, const RetryPolicy &policy) -> int;

/// @brief 第 attempt 次失败（自 1 起）后是否还该重试（max_attempts==0 恒真）。
[[nodiscard]] auto should_retry(int attempt, const RetryPolicy &policy) -> bool;

/// @brief 传输腿失败分类（进 on_state 事件供 UI 提示；口径与 SftpError 同族，
///        日志细节在传输腿，本枚举只做粗粒度归因）。
enum class TunnelError : std::uint8_t {
    None,                    ///< 无错（事件用于状态广播时携带）。
    DialFailed,              ///< 建连/主机密钥/认证失败（细节在 ssh_dial 腿日志）。
    BindFailed,              ///< 本端监听点起不来（端口占用/地址非法）。
    RemoteRefused,           ///< -R 监听请求被服务器拒绝（AllowTcpForwarding 一类）。
    ChannelLost,             ///< Active 期间通道/会话瞬断（可恢复）。
};

/// @brief 失败信号归类：哪些错误值得退避重试（纯裁决，utest 守表）。
[[nodiscard]] auto signal_for_error(TunnelError error) -> TunnelSignal;

}  // namespace borealis::conn
