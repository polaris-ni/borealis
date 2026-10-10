#pragma once

// ============================================================
// SSH 公钥推送腿（src/conn/key_push.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.10 批 2b（裁决 7.106 的 D7①/D8①，稿 §4 的「key_push」件）：
// 自开一条会话 → 认证 → **两段执行通道**把公钥行追加进远端 `~/.ssh/authorized_keys`。
//
// 本头是**私有头**（D1① 同款纪律）：libssh 类型只出现在实现文件里，接口只含标准
// 类型与档案模型（`SshProfile`）。
//
// 线程口径：**整条腿都是阻塞网络 IO**（建连可等 10 s，exec 往返再叠一段），调用方
// 必须在 worker 线程上跑，结果按 SPEC.NF.PERF.06 经有界队列交回 UI（AGENTS 第 25 条）。
// 一次调用**没有中间态回投**：`PushReport` 是终值。稿 §5 的 latest-value 快照本来就只
// 留最新值，途中的「认证中」写进去也会被终值盖掉，故本件不提供进度回调（第 3 条）。
//
// 会话归属（D8①）：每次推送自开自关，口径同 7.96①「每隧道自开会话」；不借用活动标签
// 的既有会话，`SshConnection` 的通道所有权模型因此一行未动。
//
// 明表面（CONN.09）：`secret` 随 `ssh_dial_and_authenticate` 的形参进、随返回消失，
// 本件不留副本、不落盘、不进日志。公钥行**不是**秘密，但按 D7① 同样不进命令行、
// 不进日志——它是用户的身份标识，留在日志里没有裁决价值。
//
// 与纯逻辑层的分界：判重与追加字节全在 `conn/key_model`（`is_authorized` /
// `plan_append`，裁决 7.107 已落并有证人）；本件只负责「把远端的字节读回来、
// 把模型交下的字节经 stdin 写进去」，一处也不重拼那套规则（第 3 条：不留第二真值源）。
// ============================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "borealis/conn/profile.h"

namespace borealis::conn {

/// @brief 步骤态四格（稿 §2 判据 6 的那条阶梯：拨号 → 认证 → 执行 → 完成/失败）。
///
/// 报告里这一格说的是**走到哪儿**：失败时是失败所在的那一格，成功时是 `Done`。
/// 「执行」只有一格——读回与追加是同一次「在远端跑一条命令」，两段 exec 之间没有
/// 用户能分辨的状态差，把面板的阶梯切成五格是发明不是呈现。
enum class PushStage : std::uint8_t {
    Dial,  ///< 建会话与 `ssh_connect`（含「会话压根没分配出来」）。
    Auth,  ///< 认证被服务器拒绝，或缺该方式必需的秘密材料。
    Exec,  ///< 执行通道：开不成、`request_exec` 被拒、或追加的回执不对。
    Done,  ///< 两（或多）段 exec 都按预期跑完。
};

/// @brief 推送的失败归因（稿 §2 判据 6 的措辞档位；词条 key 映射随批 3 进 `keys_format`）。
///
/// 前四档由 `conn::DialOutcome` 的四个失败值**一比一翻译**而来（该枚举实测五值含 `Ok`，
/// 其 `Ok` 不映射进本枚举——本枚举压根没有「无失败」值，成功与否由 `PushReport::failure`
/// 是否为空表达，省掉一个能被摆错的档位）。稿原写「四档失败＋推送腿两档＝六档措辞」，
/// 落地期按代码订正为下表七档：多出的 `InvalidLine` 是**拨号之前**的闸（见该档注释），
/// 它不属于 DialOutcome 也不属于 exec 腿，但它是失败归因的必读项，藏进 `ExecRefused`
/// 会说谎。
enum class PushFailure : std::uint8_t {
    DialUnallocated,   ///< 会话句柄分配不出来（本机资源问题，重拨无用）。
    DialNetwork,       ///< `ssh_connect` 失败：不可达、超时、协议协商不上。
    DialHostKey,       ///< 主机密钥与记录不符或被档案策略拦下。
    DialAuth,          ///< 认证被拒，或缺该认证方式必需的秘密材料。
    ExecRefused,       ///< 通道开不成或 `request_exec` 被拒：远端禁 exec（restricted shell）。
    WriteRejected,     ///< 追加腿回执异常：stdin 写不进、退出码非零或压根取不到退出码。
    InvalidLine,       ///< 交来的公钥行不成行（`plan_append` 出空 payload）：**未拨号、一物未动**。
};

/// @brief 一次推送的终值（稿 §5 的 `push(profile, secret, line)` 的回投面）。
struct PushReport {
    PushStage stage{PushStage::Dial};         ///< 走到哪一格。
    std::optional<PushFailure> failure{};     ///< 有值＝失败在这一格；空＝这一格是终局。
    bool already_authorized{false};           ///< 判据 §6 的「已在授权表里」：成功且**未发写**。
};

/// @brief 读回远端授权表的命令（D7① 的第一段 exec）。
///
/// 为什么暴露出来：稿 §6 把「exec 命令常量里没有 base64 段」钉成无头判据，断言的对象
/// 得是可读的常量。两条命令串都**不含任何密钥材料**——公钥行走第二段 exec 的 stdin，
/// 因此不进对端进程表，也不进本仓日志（D7① 的理由句）。
[[nodiscard]] auto authorized_keys_read_command() -> std::string_view;

/// @brief 追加命令（D7① 的第二段 exec）：`umask 077` 起头，目录与文件的权限位由
///        远端 shell 自己收，本仓不假设远端已有 `.ssh`。
[[nodiscard]] auto authorized_keys_append_command() -> std::string_view;

/// @brief 把一行公钥推到目标档案的主机上（判据 6 的整条腿）。
///
/// 顺序四步，任一步失败就地收尾并回终值：
/// ⑴ **纯逻辑闸**：`plan_append(empty, line)` 的 payload 为空即 `InvalidLine`，
///    连会话都不分配——拿着废行去拨号是白跑一次网络往返，还可能触发远端账号锁定；
/// ⑵ 自开会话 + `ssh_dial_and_authenticate`（口径同 sftp_client，策略与日志都在 dial 腿）；
/// ⑶ exec `authorized_keys_read_command()` 取回远端现内容，交 `plan_append()` 判重：
///    已含同一行则 `already_authorized` 真、**不发写**、回成功（判据 §6 第一句）；
/// ⑷ 需追加时 exec `authorized_keys_append_command()`，把 payload 走 stdin 交进去、
///    `send_eof`、排空两条流后取退出码；非零或取不到即 `WriteRejected`。
///
/// 读回腿的退出码**不参与判据**：`cat` 在文件不存在时本就回非零并往 stderr 抱怨，
/// 那正是「远端还没有授权表」的形态，判重侧按空内容走新建那一支（写腿才是权威）。
///
/// @param profile 目标档案（主机/端口/用户/认证方式/known_hosts 策略），来自档案下拉。
/// @param secret  已解析的秘密材料；agent 认证传 nullopt。随 dial 返回即消失。
/// @param line    `public_line()` 的产物，或 `.pub` 文件里的那一行。
[[nodiscard]] auto push_public_key(const SshProfile &profile, std::optional<std::string> secret,
                                   std::string_view line) -> PushReport;

}  // namespace borealis::conn
