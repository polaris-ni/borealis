#pragma once

// ============================================================
// SSH 真传输连接（src/conn/ssh_connection.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.02 的集成式 SSH 传输腿：libssh（vcpkg，裁决 D8）在进程内完成
// 连接→主机密钥核对→认证→PTY 通道→shell，字节流经 session::Connection 投递。
//
// 本头是**私有头**（裁决 D1① 同款纪律）：libssh 类型只以前向声明的指针形态出现，
// 平台/传输知识不进 include/；会话层只看到 session::Connection 接口（架构 §7.2）。
//
// 线程模型沿用 forkpty 腿（架构 §3.1）：连接/认证/读循环都在**读线程**上，
// start() 立即返回；凭据明文只在本对象内存里存活到认证结束，不落盘不进日志
// （CONN.09，裁决 7.93）。
//
// 已知边界（本期不做，见设计稿 §7）：Ask 主机密钥策略按 Yes 保守处理（弹 UI 询问
// 留给 CONN.09 询问件到货后的独立任务）；keyboard-interactive 只支持单提示一次作答。
//
// agent 转发（`SPEC.FEAT.CONN.02`，按 profile 开关、默认关）落在本腿：会话通道打开后
// 向服务器发 `auth-agent-req@openssh.com`，服务器反向打开的每个 auth-agent 通道都由
// 读循环接到本机 agent 的字节管道（`platform/agent_socket.h`）上。开关只认档案字段
// `ssh.agent_forwarding`（`~/.ssh/config` 的 `ForwardAgent` 导入可达）；向导里那颗
// 复选框按 `UI_OVERVIEW.draft.md` 仍是延后项。
//
// 自动重连（`SPEC.FEAT.WS.05`，裁决 7.99 D1②）落在**本腿内部**：读线程掉线后不立即报
// 关闭，自己按退避重跑「dial→通道→PTY→shell→读循环」那一段，只有不可重连的原因、
// 次数用尽、用户停止或本端 close() 才把 on_closed(reason) 投出去。会话层与网格内容
// 因此全程不换血（需求那句「保留终端内容供回看」的根据）。
// ============================================================

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "conn/reconnect.h"
#include "conn/ssh_dial.h"

// libssh 前向声明：头里只出现指针，实现文件才包含 <libssh/libssh.h>。
// ssh_session_struct 的前向声明在 conn/ssh_dial.h。
struct ssh_channel_struct;

namespace borealis::conn {

/// @brief SSH 传输实现：libssh 阻塞模型 + 每会话一个读线程。
///
/// 秘密材料经构造传入——调用方在 start() 之前经 CredentialStore / 询问对话框
/// 解析完成。裁决 7.99 D7② 把驻留期从「认证结束」延到 `close()`：重拨要重新认证，
/// 每次尝试都从成员复制一份，形参副本在 dial 腿返回时消失。与 7.94③ 的「认证后即清」
/// 相冲已如实登记，口径与 7.96⑥（隧道为自动重试保留到 stop）、装配层为 SFTP 保留
/// `ssh_secret_by_tab` 同向——明文仍不落盘、不进日志、不出本对象。
class SshConnection final : public session::Connection, public session::ReconnectControl {
  public:
    /// @param profile 档案（主机/端口/用户/认证方式/known_hosts 策略）。
    /// @param secret  已解析的秘密材料：password 认证＝口令，privatekey＝passphrase，
    ///                keyboard-interactive＝首提示答案；agent 认证传 nullopt。
    /// @param initial_size 首次 PTY 尺寸（SPEC.FEAT.XFER.01 会话启动腿）；重拨取的是
    ///        resize() 更新后的**最新值**，不是本参数（裁决 7.99 D7①）。
    /// @param reconnect_policy 退避档（装配层把 `settings.connection.ssh` 三键搬进来；
    ///        省略即 `default_reconnect_policy()`，裁决 7.99 D4① 的那份缺省）。
    /// @param algorithms 自定义 KEX／主机密钥算法表（空＝库缺省；`SPEC.FEAT.CONN.02` 延后子项）。
    ///        重拨沿用同一份快照——协商面在会话建立那一刻定，不随设置面板改动换血。
    SshConnection(const SshProfile &profile, std::optional<std::string> secret,
                  session::Size initial_size,
                  RetryPolicy reconnect_policy = default_reconnect_policy(),
                  SshAlgorithmOverrides algorithms = {});

    SshConnection(const SshConnection &) = delete;
    SshConnection(SshConnection &&) = delete;
    auto operator=(const SshConnection &) -> SshConnection & = delete;
    auto operator=(SshConnection &&) -> SshConnection & = delete;

    ~SshConnection() override;

    auto start(session::ConnectionEvents &events) -> void override;

    auto write(std::span<const std::byte> bytes) -> void override;

    auto resize(session::Size size) -> void override;

    auto close() -> void override;

    [[nodiscard]] auto alive() const noexcept -> bool override;

    /// @brief 「立即重试」：截断当前那段退避休眠，下一次分段片到点即拨（最坏晚 200 ms）。
    auto retry_now() -> void override { retry_now_.store(true); }

    /// @brief 「停止重连」：置终止位让环自己落终态，不硬杀读线程（裁决 7.99 屏 B 的唯一出口）。
    auto stop_reconnect() -> void override { stop_reconnect_.store(true); }

  private:
    /// @brief 一次完整尝试的产出：是否真连上过、以及这次结束归到哪条原因。
    struct Attempt {
        bool linked;                       ///< 是否进到过读循环（false＝拨号段就失败了）。
        session::CloseReason reason;       ///< 结束归因。
    };

    /// @brief 读线程主体：退避环——反复跑 dial_and_serve()，可重连的原因按档重拨，
    ///        直到不可重连 / 次数用尽 / 用户停止 / 本端关闭，才投最后一份进度与 on_closed。
    auto run_loop() -> void;

    /// @brief 单次尝试：建会话→dial→通道→PTY→shell→读循环（首次与重拨共用同一段，
    ///        裁决 7.99 D1 代价 (c) 的「不复制第二份」）。PTY 取当下的 size_。
    [[nodiscard]] auto dial_and_serve() -> Attempt;

    /// @brief 非阻塞读循环，直到本端关闭 / 链路丢失 / 对端 EOF。
    /// @return 结束归因（LocalClose / LinkLost / RemoteExit）。
    [[nodiscard]] auto read_loop() -> session::CloseReason;

    /// @brief 分段退避休眠：每片 200 ms 查 closing_ / stop_reconnect_ / retry_now_。
    /// @return false＝被 close() 打断（调用方据此落本端关闭档）；true＝可以去拨下一回。
    [[nodiscard]] auto sleep_backoff(int delay_ms) -> bool;

    /// @brief 投一份进度快照（latest-value，UI 每帧读最近一份；见 session::ReconnectProgress）。
    auto post_progress(const session::ReconnectProgress &progress) -> void;

    /// @brief 释放会话与通道：close() 收口时用，退避环换血时也用它丢掉上一回的句柄。
    ///        libssh 句柄只在持 `state_mutex_` 时触及，故读线程自身调用与 UI 线程的
    ///        resize() 互斥；write() 靠 `channel_open_` 早退（与既有单腿形态同口径）。
    auto teardown() -> void;

    /// @brief agent 转发的一次尝试状态：回调表必须比会话活得久（libssh 存的是指针，
    ///        不拷贝），反向通道与其本机 agent 句柄也绑在同一次尝试上。
    struct AgentForwardState;

    /// @brief 装回调表（仅当档案开关为真）：须在 ssh_connect 之前调，否则连接期内
    ///        服务器发出的反向通道请求不会被派发到我们。
    auto setup_agent_forwarding(ssh_session_struct *session) -> void;

    /// @brief 会话通道打开后向服务器请求 agent 转发；本机没有 agent 或服务器拒绝
    ///        都只记 WARN、不阻断 shell（转发是增强项，不是连接前提）。
    auto request_agent_forwarding() -> void;

    /// @brief 读循环每拍搬一次反向通道的字节（远端 ↔ 本机 agent）。
    auto service_agent_forwarding() -> void;

    /// @brief 收掉所有反向通道与本机 agent 句柄（重拨换血与会话收口共用）。
    auto close_agent_forwarding() -> void;

    /// @brief libssh 反向通道请求回调：连上本机 agent 才接受，通道数有上限。
    static auto handle_agent_channel_open(ssh_session_struct *session, void *userdata)
        -> ssh_channel_struct *;

    SshProfile profile_;
    std::optional<std::string> secret_;   ///< 驻留到 close()（裁决 7.99 D7②），每次尝试复制一份。
    session::Size size_;
    RetryPolicy reconnect_policy_;        ///< 退避档，来自 settings.connection.ssh 三键。
    SshAlgorithmOverrides algorithms_;    ///< 算法覆盖项，来自 settings.connection.ssh 两键。

    /// 守护 libssh 句柄的建立/销毁，避免 resize 与 close 竞态（forkpty 同款）。
    std::mutex state_mutex_;
    /// 串行化通道写：用户键入与读线程回显响应两条路径不得撕裂转义序列。
    std::mutex write_mutex_;
    /// 保护 agent 反向通道表：回调由 libssh 在「处理待收包」的那一刻派发，而 UI 线程的
    /// write()/resize() 也会触发收包处理，故建表与搬运不能只靠「都在读线程」这条假设。
    /// 递归是必须的——读线程搬字节时里面的 libssh 调用可能当场派发回调，同一线程要再进一次。
    std::recursive_mutex agent_mutex_;

    ssh_session_struct *session_ = nullptr;
    ssh_channel_struct *channel_ = nullptr;
    /// agent 转发的回调表与反向通道：只在读线程上建立/搬运，收口发生在 join 之后。
    std::unique_ptr<AgentForwardState> agent_forward_;

    session::ConnectionEvents *events_ = nullptr;
    std::thread reader_;

    std::atomic<bool> closing_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> retry_now_{false};       ///< 「立即重试」：截断当前退避。
    std::atomic<bool> stop_reconnect_{false};  ///< 「停止重连」：让环自己落终态。
    /// 通道是否已可用（认证+shell 成功置位；出错/关闭清位）。alive() 免锁读取。
    std::atomic<bool> channel_open_{false};
};

}  // namespace borealis::conn
