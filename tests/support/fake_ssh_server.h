#pragma once

// ============================================================
// 测试公共设施：进程内 libssh 假 SSH 服务器（tests/support/fake_ssh_server.h）
// ------------------------------------------------------------
// 本沙箱没有 sshd（`/usr/sbin/sshd` 不存在），`SPEC.FEAT.CONN.02` 的终端腿、SFTP、
// 隧道与 agent 转发的「真协议对端」长期没有自动化证人。本文件用 libssh 的 server 腿
// （本机 vcpkg 构建开了 `WITH_SERVER`，见 SPECIFICATIONS §7.145②）在测试进程内起一台
// **最小 SSH 服务器**：loopback 临时端口、一次性主机密钥、password 认证、session 通道、
// PTY/shell 与字节回显，按需反向打开的 `auth-agent@openssh.com` 通道（agent 转发腿），
// 以及按需接受并真转发的 `direct-tcpip` 通道（`SPEC.FEAT.CONN.03` 的跳板腿——本仓的
// 一台假服务器就此当得了跳板机）。证人是真协议（含 KEX、加密、通道多路复用），不是桩。
//
// 线程模型：
//   - 一个受理线程：自带监听 socket（端口 0 由内核择定），`select` 分片等连接；
//   - 每条连接一个工作线程：`ssh_bind_accept_fd` + `ssh_handle_key_exchange` 之后由
//     `ssh_event_dopoll(10ms)` 驱动，回调在本线程内发生——libssh 会话不跨线程。
//   - 计数面（连接数/通道数/PTY/认证）加锁取快照，用例线程随时可读；
//   - 出站字节走共享队列 + 每连接游标：`send_more()` 投一份，各连接各自送出。
//
// 只说话不做断言：等待用 `wait_until()`，断言留在用例里（与 LoopbackPeer 同口径）。
// ============================================================

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "borealis/conn/profile.h"

namespace borealis::testing {

/// @brief 假服务器的行为旋钮（全部在构造时定，运行期不改——改旋钮等于换一台服务器）。
struct FakeSshServerOptions {
    std::string user{"tester"};      ///< 接受的用户名。
    std::string password{"secret"};  ///< 接受的口令。
    bool echo{true};                 ///< 收到的字节原样写回同一通道（终端腿的往返判据）。
    bool reject_auth{false};         ///< 一律拒绝认证（认证失败档的证人）。
    std::string banner{};            ///< shell 一起来就写出的字节（空＝不写）。
    bool open_agent_channel{false};  ///< 收到 `auth-agent-req` 且 shell 就绪后，反向开一条
                                     ///< auth-agent 通道（`SPEC.FEAT.CONN.02` 的转发腿）。
    std::string agent_payload{};     ///< 反向 agent 通道上发给客户端 agent 的字节（空＝不发）。
    bool allow_direct_tcpip{false};  ///< 接受 `direct-tcpip` 开通道请求并真转发到目的地
                                     ///< （`SPEC.FEAT.CONN.03` 的跳板腿：本仓的假服务器就此
                                     ///< 当得了一台跳板机）。缺省关——一台普通服务器不该代收
                                     ///< 转发，关掉也是「跳板拒绝转发」那条失败档的证人。
};

/// @brief 一台进程内 SSH 服务器（每条用例一台，端口随对象一起消失）。
class FakeSshServer final {
  public:
    explicit FakeSshServer(FakeSshServerOptions options = {});
    ~FakeSshServer();

    FakeSshServer(const FakeSshServer &) = delete;
    FakeSshServer(FakeSshServer &&) = delete;
    auto operator=(const FakeSshServer &) -> FakeSshServer & = delete;
    auto operator=(FakeSshServer &&) -> FakeSshServer & = delete;

    /// @brief 监听点是否起来了（失败时用例直接把夹具的红线报出来，不静默跳过）。
    [[nodiscard]] auto ok() const -> bool;

    /// @brief 内核择定的监听端口。
    [[nodiscard]] auto port() const -> int;

    // ---- 事实计数（累计值；连接复用落地后「一条 TCP 上几条通道」靠这几个数见证）----

    [[nodiscard]] auto accepted_connections() const -> std::size_t;
    [[nodiscard]] auto session_channels() const -> std::size_t;
    [[nodiscard]] auto auth_attempts() const -> std::size_t;
    [[nodiscard]] auto pty_requests() const -> std::size_t;
    [[nodiscard]] auto shell_requests() const -> std::size_t;
    [[nodiscard]] auto last_pty_columns() const -> int;
    [[nodiscard]] auto last_pty_rows() const -> int;

    /// @brief 第 @p index 条 session 通道（按打开次序，自 0 起）的最近 PTY 尺寸
    ///        `(columns, rows)`；越界回 `(0, 0)`。
    ///
    /// 连接复用落地后（`SPEC.FEAT.CONN.02`）一条 TCP 上有多条通道，逐通道尺寸是
    /// 「每个标签各有自己的行列」的线上证人；旧用例读的全局最近一档保持不动。
    [[nodiscard]] auto pty_size_at(std::size_t index) const -> std::pair<int, int>;

    // ---- agent 转发腿的事实（`SPEC.FEAT.CONN.02`，裁决 7.145 的真协议证人）----
    [[nodiscard]] auto agent_forward_requests() const -> std::size_t;
    [[nodiscard]] auto agent_channels() const -> std::size_t;

    /// @brief 从反向 agent 通道上收到的字节（客户端 agent 的应答）。
    [[nodiscard]] auto agent_received_text() const -> std::string;

    /// @brief 已从客户端收到的全部通道字节（ASCII 原样）。
    [[nodiscard]] auto received_text() const -> std::string;

    // ---- 跳板腿的事实（`SPEC.FEAT.CONN.03`：本仓假服务器当跳板时的线上证人）----

    /// @brief 收到过多少次 `direct-tcpip` 开通道请求（含被拒的：拒拨的证人也要数得到）。
    [[nodiscard]] auto direct_tcpip_requests() const -> std::size_t;

    /// @brief 逐次 `direct-tcpip` 请求的目的地，形如 `host:port`，按到达次序。
    ///
    /// 两跳链路的跳序证人（稿 §5 的 E2）读的就是这一串：第一跳的目的地必须是第二跳的端口，
    /// 第二跳的目的地必须是目标的端口——写反了就立刻现形。
    [[nodiscard]] auto forwarded_destinations() const -> std::vector<std::string>;

    /// @brief 向所有已开 shell 的连接投一段字节（每条连接各送一份）。
    auto send_more(std::string_view bytes) -> void;

    /// @brief 对所有 session 通道发 EOF：客户端读到 `RemoteExit`（不是断线）。
    auto end_shell() -> void;

    /// @brief 粗暴断开所有连接（`shutdown` 双向）：客户端读到链路丢失并进重拨环。
    auto drop_connections() -> void;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// @brief 轮询等待谓词成立（默认上限 5 s，够 KEX 与一次拨号）。
[[nodiscard]] auto wait_until(const std::function<bool()> &predicate, int timeout_ms = 5000)
    -> bool;

/// @brief 指向假服务器的 SSH 档案：password 认证、关闭 known_hosts 核对（不污染真机
///        的 `~/.ssh/known_hosts`——策略 No 的语义见 `conn::policy_accepts`）。
[[nodiscard]] auto fake_ssh_profile(int port, std::string user = "tester")
    -> borealis::conn::SshProfile;

}  // namespace borealis::testing
