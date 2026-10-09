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
// 留给 CONN.09 询问件到货后的独立任务）；agent_forwarding 字段已入模型但传输层
// 尚未下发该请求；keyboard-interactive 只支持单提示一次作答。
// ============================================================

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"

// libssh 前向声明：头里只出现指针，实现文件才包含 <libssh/libssh.h>。
struct ssh_session_struct;
struct ssh_channel_struct;

namespace borealis::conn {

/// @brief 主机密钥核对结果（本仓自有枚举：实现文件里从 libssh 状态翻译而来，
///        纯逻辑裁决函数不必携带 libssh 类型即可单测）。
enum class HostKeyState : std::uint8_t {
    Known,   ///< 与 known_hosts 记录一致。
    New,     ///< known_hosts 没有该主机（首次连接）。
    Changed, ///< 与记录不一致（可能中间人，最高危）。
    Error,   ///< 核对本身失败（读不到 known_hosts 等）。
};

/// @brief 把主机密钥核对结果按档案策略裁决为「放行 / 拒绝」（纯逻辑，无头单测）。
///
/// - AcceptNew：Known/New 放行（New 顺手写入 known_hosts，实现侧），Changed/ Error 拒绝；
/// - Yes：仅 Known 放行；
/// - No：一律放行（不核对，仅旧设备兼容，模型注释已警示）；
/// - Ask：本期按 Yes 保守处理——未知即拒绝，绝不静默放行（见头部「已知边界」）。
[[nodiscard]] auto policy_accepts(KnownHostsPolicy policy, HostKeyState state) -> bool;

/// @brief 生效端口：档案未填（≤0）时回落 22（与 OpenSSH 缺省一致，纯逻辑）。
[[nodiscard]] auto effective_port(const SshProfile &profile) -> int;

/// @brief 生效认证方式：空串或未识别值回落 "agent"（模型缺省同源，纯逻辑）。
[[nodiscard]] auto normalized_auth_method(const SshProfile &profile) -> std::string_view;

/// @brief 该认证方式是否必须有秘密材料（password 必需；privatekey 的 passphrase
///        可选——无口令私钥为空即可；agent / keyboard-interactive 不强求，纯逻辑）。
[[nodiscard]] auto auth_requires_secret(std::string_view method) -> bool;

/// @brief SSH 传输实现：libssh 阻塞模型 + 每会话一个读线程。
///
/// 秘密材料经构造传入——调用方在 start() 之前经 CredentialStore / 询问对话框
/// 解析完成，取值只喂给本次认证，认证结束后本对象不再持有明文副本的需求
/// （成员在 close() 时清空）。
class SshConnection final : public session::Connection {
  public:
    /// @param profile 档案（主机/端口/用户/认证方式/known_hosts 策略）。
    /// @param secret  已解析的秘密材料：password 认证＝口令，privatekey＝passphrase，
    ///                keyboard-interactive＝首提示答案；agent 认证传 nullopt。
    /// @param initial_size 首次 PTY 尺寸（SPEC.FEAT.XFER.01 会话启动腿）。
    SshConnection(const SshProfile &profile, std::optional<std::string> secret,
                  session::Size initial_size);

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

  private:
    /// @brief 读线程主体：建会话→连接→主机密钥核对→认证→开通道→PTY→shell→读循环。
    ///        任一步失败即记日志并投递 on_closed（与 forkpty 腿的失败语义一致）。
    auto run_loop() -> void;

    /// @brief 释放会话与通道（须已 join 读线程；close() 与析构共用）。
    auto teardown() -> void;

    SshProfile profile_;
    std::optional<std::string> secret_;
    session::Size size_;

    /// 守护 libssh 句柄的建立/销毁，避免 resize 与 close 竞态（forkpty 同款）。
    std::mutex state_mutex_;
    /// 串行化通道写：用户键入与读线程回显响应两条路径不得撕裂转义序列。
    std::mutex write_mutex_;

    ssh_session_struct *session_ = nullptr;
    ssh_channel_struct *channel_ = nullptr;

    session::ConnectionEvents *events_ = nullptr;
    std::thread reader_;

    std::atomic<bool> closing_{false};
    std::atomic<bool> started_{false};
    /// 通道是否已可用（认证+shell 成功置位；出错/关闭清位）。alive() 免锁读取。
    std::atomic<bool> channel_open_{false};
};

}  // namespace borealis::conn
