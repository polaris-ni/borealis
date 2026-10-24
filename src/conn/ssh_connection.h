#pragma once

// ============================================================
// SSH 真传输连接（薄适配器，src/conn/ssh_connection.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.02` 的集成式 SSH 传输腿：libssh（vcpkg，裁决 D8）在进程内完成
// 连接→主机密钥核对→认证→PTY 通道→shell，字节流经 session::Connection 投递。
//
// **连接复用（设计稿 `codespec/CONN02_REUSE.draft.md`，裁决 7.147）之后本类只
// 剩适配器职责**：真正的会话、通道与 I/O 线程在 `conn/ssh_shared.h` 的
// `SshSharedSession` 里，一个共享会话可挂多份本类（每个终端标签/pane 一份）。
// 构造末位参数 `registry` 缺省为空时，本类自建一条不登记的共享会话，于是单标签
// 路径与既有测试的编译面、行为都不变。
//
// 线程模型：所有 libssh 调用只在共享会话的 I/O 线程上；本类的公开面
// （start/write/resize/close/alive/retry_now/stop_reconnect）由 UI 线程调用，
// 只压命令、等 ack 或读原子位。`close()` 返回后不再回调本对象的事件接收端。
//
// 自动重连（`SPEC.FEAT.WS.05`，裁决 7.99 D1②）仍由传输腿内部完成，只是环变成
// **连接级**：一条退避环、进度广播给全部挂接标签、`stop_reconnect` 停整条环。
//
// 已知边界（本期不做，见设计稿 §11）：Ask 主机密钥策略按 Yes 保守处理；
// keyboard-interactive 只支持单提示一次作答；SFTP/隧道/密钥推送不参与共享。
// ============================================================

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "conn/reconnect.h"
#include "conn/ssh_dial.h"
#include "conn/ssh_shared.h"

namespace borealis::conn {

/// @brief SSH 传输的适配器：一份标签/pane 一条 shell 通道，底层可共享一条 TCP。
///
/// 秘密材料经构造传入——调用方在 start() 之前经 CredentialStore / 询问对话框
/// 解析完成。材料在 `start()` 成功挂接后移交给共享会话并清掉本对象的副本；
/// 共享会话驻留到它自己收口（重拨要重新认证，裁决 7.99 D7② 的共享版）。
class SshConnection final : public session::Connection, public session::ReconnectControl {
  public:
    /// @param profile 档案（主机/端口/用户/认证方式/known_hosts 策略/跳链）。
    /// @param secret  已解析的秘密材料：password 认证＝口令，privatekey＝passphrase，
    ///                keyboard-interactive＝首提示答案；agent 认证传 nullopt。
    ///                带跳链的档案在此一并交**逐跳**材料（`SPEC.FEAT.CONN.03` 稿 D8）；
    ///                只有目标材料的旧形态经隐式转换进来，六条装配腿的形参不用改。
    /// @param initial_size 本标签首次 PTY 尺寸；重拨取 resize() 更新后的最新值。
    /// @param reconnect_policy 退避档（共享会话取首份租约交来的这一份）。
    /// @param algorithms 自定义 KEX／主机密钥算法表（空＝库缺省）。
    /// @param registry 共享表；空＝本连接自建一条不共享的会话。
    SshConnection(const SshProfile &profile, SshSecretBundle secret,
                  session::Size initial_size,
                  RetryPolicy reconnect_policy = default_reconnect_policy(),
                  SshAlgorithmOverrides algorithms = {},
                  std::shared_ptr<SshSessionRegistry> registry = nullptr);

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

    /// @brief 「立即重试」：截断共享退避（连接级）。
    auto retry_now() -> void override;

    /// @brief 「停止重连」：整条共享链路落终态（连接级，设计稿 D5）。
    auto stop_reconnect() -> void override;

  private:
    SshProfile profile_;
    SshSecretBundle secret_;        ///< 目标 + 逐跳材料；挂接成功即移交共享会话并清空。
    session::Size size_;
    RetryPolicy reconnect_policy_;
    SshAlgorithmOverrides algorithms_;
    std::shared_ptr<SshSessionRegistry> registry_;
    std::shared_ptr<SshSharedSession> shared_;

    /// close() 幂等：第二次调用等第一次完成（`Connection::close` 的「返回后不再
    /// 回调」对两个调用点同时成立）。
    std::once_flag close_once_;

    std::atomic<bool> started_{false};
    std::atomic<bool> closing_{false};
    /// 本租约的通道是否可用（共享会话的 I/O 线程置位/清位）。alive() 免锁读取。
    std::atomic<bool> channel_open_{false};
};

}  // namespace borealis::conn
