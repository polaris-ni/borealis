#pragma once

// ============================================================
// SSH 建连 + 认证共用腿（src/conn/ssh_dial.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.02（终端 SSH 传输）与 SPEC.FEAT.CONN.04（SFTP 浏览器）是
// libssh 会话的两个独立消费方；在 CONN.02 的连接复用落地之前，SFTP 自开一条
// 会话。本头把「建会话选项 → 连接 → 主机密钥核对 → 认证」一条龙收拢为
// ssh_dial_and_authenticate()，两个消费方共用同一套策略裁决与日志口径。
//
// 本头是**私有头**（裁决 D1① 同款纪律）：libssh 类型只以前向声明的指针形态
// 出现，平台/传输知识不进 include/。纯逻辑裁决函数不携带 libssh 类型，
// 可无头单测（utest_ssh）。
// ============================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "borealis/conn/profile.h"

// libssh 前向声明：头里只出现指针，实现文件才包含 <libssh/libssh.h>。
struct ssh_session_struct;

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
/// - AcceptNew：Known/New 放行（New 顺手写入 known_hosts，实现侧），Changed/Error 拒绝；
/// - Yes：仅 Known 放行；
/// - No：一律放行（不核对，仅旧设备兼容，模型注释已警示）；
/// - Ask：本期按 Yes 保守处理——未知即拒绝，绝不静默放行（询问件属 CONN.09）。
[[nodiscard]] auto policy_accepts(KnownHostsPolicy policy, HostKeyState state) -> bool;

/// @brief 生效端口：档案未填（≤0）时回落 22（与 OpenSSH 缺省一致，纯逻辑）。
[[nodiscard]] auto effective_port(const SshProfile &profile) -> int;

/// @brief 生效认证方式：空串或未识别值回落 "agent"（模型缺省同源，纯逻辑）。
[[nodiscard]] auto normalized_auth_method(const SshProfile &profile) -> std::string_view;

/// @brief 该认证方式是否必须有秘密材料（password 必需；privatekey 的 passphrase
///        可选——无口令私钥为空即可；agent / keyboard-interactive 不强求，纯逻辑）。
[[nodiscard]] auto auth_requires_secret(std::string_view method) -> bool;

/// @brief 拨号一条龙的结果分档（`SPEC.FEAT.WS.05` 的关闭归因数据源，裁决 7.99 D2①/D3①）。
///
/// 只回答「卡在哪一步」，不回答「为什么卡」——后者要 libssh 错误文本，留在日志里。
/// 分档的意义在自动重连的取舍：`Network` 值得按退避重拨；`HostKey` 与 `Auth` 重拨只会
/// 重复失败（还可能触发远端账号锁定）；`Unallocated` 是本机资源问题。后三者一律不重拨。
enum class DialOutcome : std::uint8_t {
    Ok,          ///< 连接、核对、认证三步全过。
    Unallocated, ///< 会话句柄未分配（调用方传了空指针）。
    Network,     ///< `ssh_connect` 失败：不可达、超时、协议协商不上。
    HostKey,     ///< 主机密钥与记录不符或核对出错，被档案策略拒绝。
    Auth,        ///< 认证被服务器拒绝，或缺该方式必需的秘密材料。
};

/// @brief 在已分配的 libssh 会话上完成「连接 → 主机密钥核对 → 认证」一条龙。
///
/// 阻塞语义：ssh_connect 可阻塞数秒，调用方必须在非 UI 线程上调用。不抛出：
/// 任一步失败自行记 AURORA_LOG_ERROR（只含 libssh 错误文本，绝不回显秘密材料）
/// 并按 `DialOutcome` 归到失败所在的那一档。@p secret 无论成败都在本函数返回时随形参一并消失
/// （CONN.09：明文不比认证活得久；调用方要留到下一次重拨就自己保一份，见裁决 7.99 D7②）。
///
/// @param profile 档案（主机/端口/用户/认证方式/known_hosts 策略）。
/// @param secret  已解析的秘密材料：password＝口令，privatekey＝passphrase，
///                keyboard-interactive＝首提示答案；agent 认证传 nullopt。
/// @param session 调用方先 ssh_new() 分配好的会话；本函数不负责 ssh_free()。
/// @return 认证成功回 `Ok`；其余按失败所在的那一步回对应档位。
[[nodiscard]] auto ssh_dial_and_authenticate(const SshProfile &profile,
                                             std::optional<std::string> secret,
                                             ssh_session_struct *session) -> DialOutcome;

}  // namespace borealis::conn
