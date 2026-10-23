#pragma once

// ============================================================
// SSH 连接复用（src/conn/ssh_shared.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.02` 的延后子项「同一 profile 的连接复用（多标签共享一条
// TCP 连接）」的私有基座（设计稿 `codespec/CONN02_REUSE.draft.md`，D1–D12）：
//
//   * `SshSessionKey`：共享判据——生效连接参数全等（端口/认证方式已归一、
//     算法表逐项比较），再叠一层「秘密材料全等」。档案 id **不入键**：键回答
//     「连到哪里、以谁的身份、用什么协商面」，不回答「哪条档案记录」；档案
//     被编辑后旧标签的快照天然分键。
//   * `SshSessionRegistry`：一张线性表，持弱引用；表项不存秘密（秘密比较问
//     仍活着的会话），末位租约收口后自动剪枝。
//   * `SshSharedSession`：一条 libssh 会话、一条 I/O 线程、N 条 shell 通道。
//     `SshConnection` 是它的薄适配器；所有 libssh 调用只发生在这条 I/O 线程
//     上（libssh 非线程安全，旧单标签腿里 UI 线程直写通道与读线程并发的潜在
//     竞态就此收敛）。
//
// 线程边界：`attach` / `detach` / `write` / `resize` 由调用方线程（UI）进入，
// 只做「压命令 / 等 ack」；`detach` 返回后该租约不会再收到 `ConnectionEvents`
// 回调。I/O 线程的伪码与关闭序见设计稿 §5–§6。
//
// 本头是私有头（裁决 D1① 同款纪律）：libssh 类型只以前向声明的指针形态出现。
// ============================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "conn/reconnect.h"
#include "conn/ssh_dial.h"

// libssh 前向声明：实现文件才包含 <libssh/libssh.h>。
struct ssh_channel_struct;
struct ssh_session_struct;

namespace borealis::conn {

class SshConnection;
class SshSharedSession;

/// @brief 共享判据：一条 SSH 传输的全部「生效连接参数」。
///
/// 取值一律走归一口径（`effective_port` / `normalized_auth_method`），于是
/// 「端口留空」与「端口 22」、认证方式留空与 `agent` 不会各拨一条。
struct SshSessionKey {
    std::string host;
    int port{22};
    std::string user;
    std::string auth_method;
    std::string identity_file;
    bool agent_forwarding{false};
    KnownHostsPolicy known_hosts_policy{KnownHostsPolicy::AcceptNew};
    std::string kex_algorithms;      ///< `SshAlgorithmOverrides::kex` 的原串（空＝库缺省）。
    std::string host_key_algorithms; ///< `SshAlgorithmOverrides::host_key` 的原串。

    /// @brief 从档案与算法覆盖项折出键（纯逻辑，无 libssh 类型）。
    [[nodiscard]] static auto of(const SshProfile &profile,
                                 const SshAlgorithmOverrides &algorithms) -> SshSessionKey;

    [[nodiscard]] auto operator==(const SshSessionKey &) const noexcept -> bool = default;
};

/// @brief 两份秘密材料是否可共用同一条已认证连接（逐字节比较，含「都没有」一档）。
///
/// 材料不同就绝不共享：把两个口令不同的标签接到同一条已认证链路上，等于让
/// 第二个标签绕过它自己的认证材料。
[[nodiscard]] auto same_secret(const std::optional<std::string> &lhs,
                               const std::optional<std::string> &rhs) -> bool;

/// @brief 共享会话表：同键同秘密的标签借同一条传输（设计稿 D2/D6）。
///
/// 只在装配层的一枚实例上使用；内部有锁，但正常路径全在 UI 线程。
class SshSessionRegistry final {
  public:
    SshSessionRegistry() = default;
    SshSessionRegistry(const SshSessionRegistry &) = delete;
    SshSessionRegistry(SshSessionRegistry &&) = delete;
    auto operator=(const SshSessionRegistry &) -> SshSessionRegistry & = delete;
    auto operator=(SshSessionRegistry &&) -> SshSessionRegistry & = delete;

    /// @brief 找一条键与秘密都相符、且仍接纳新租约的会话；找不到就新建一条。
    /// @param secret 秘密材料；只在比较期间使用，表与新建的会话各留自己那份。
    [[nodiscard]] auto acquire(const SshProfile &profile, std::optional<std::string> secret,
                               const SshAlgorithmOverrides &algorithms, RetryPolicy policy)
        -> std::shared_ptr<SshSharedSession>;

  private:
    struct Entry {
        SshSessionKey key;
        std::weak_ptr<SshSharedSession> session;
    };

    std::mutex mutex_;
    std::vector<Entry> entries_;
};

/// @brief 一条可被多个终端标签共用的 SSH 传输。
///
/// 生命周期：首个 `attach` 启动 I/O 线程；最后一个 `detach` 之后 I/O 线程自行
/// 收口（关通道 → 断会话 → 释放回调表 → 清明文）并退出。对象由适配器与
/// registry 的临时强引用持有，析构会 join I/O 线程。
class SshSharedSession final {
  public:
    SshSharedSession(const SshProfile &profile, std::optional<std::string> secret,
                     RetryPolicy policy, SshAlgorithmOverrides algorithms);
    ~SshSharedSession();

    SshSharedSession(const SshSharedSession &) = delete;
    SshSharedSession(SshSharedSession &&) = delete;
    auto operator=(const SshSharedSession &) -> SshSharedSession & = delete;
    auto operator=(SshSharedSession &&) -> SshSharedSession & = delete;

    /// @brief 键与秘密是否与本会话一致（registry 用；秘密比较不落表）。
    [[nodiscard]] auto matches(const SshSessionKey &key,
                               const std::optional<std::string> &secret) const -> bool;

    /// @brief 是否仍接纳新租约（进入重连终态后为假，registry 不再借出）。
    [[nodiscard]] auto accepting() const noexcept -> bool;

    /// @brief 挂接一份租约；首个挂接启动 I/O 线程。
    ///
    /// @param on_open / on_finished 适配器的原子位钩子：只由 I/O 线程调用。
    ///        `on_finished` 保证在 `detach` 的 ack 之前、以及任何 `on_closed`
    ///        之前发生（`alive()` 与「关闭前确认」据此判）。
    /// @return false＝会话已进入终态，调用方应换一条（registry 重取或自建）。
    auto attach(SshConnection *lease, session::ConnectionEvents &events, session::Size size,
                std::function<void()> on_open, std::function<void()> on_finished) -> bool;

    /// @brief 摘掉一份租约并阻塞到 I/O 线程处理完；返回后不再回调该租约。
    auto detach(SshConnection *lease) -> void;

    /// @brief 向该租约的通道写一笔（字节被复制；队列有界，满则丢）。
    auto write(SshConnection *lease, std::span<const std::byte> bytes) -> void;

    /// @brief 更新该租约的最新尺寸；退避期间也记账（重拨按新尺寸开 PTY）。
    auto resize(SshConnection *lease, session::Size size) -> void;

    /// @brief 「立即重试」：截断共享退避（连接级，设计稿 D5）。
    auto retry_now() -> void;

    /// @brief 「停止重连」：整条环落 `UserStopped`，投给全部挂接租约（连接级）。
    auto stop_reconnect() -> void;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace borealis::conn
