#pragma once

// ============================================================
// 断线重连的纯逻辑裁决（src/conn/reconnect.h）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.05` 的 SSH 腿回答两问：这次关闭值不值得自动重拨（裁决 7.99 D2① 的分类表）、
// 下一次什么时候拨（用尽判定 + 退避档位）；`SPEC.FEAT.CONN.04` 的 SFTP 面板借同一件回答
// 第三问：这次操作失败算不算掉线（裁决 7.99 D7④ 改判新增）。
//
// 退避算式**不在这里重写**：直接吃 `conn::RetryPolicy` + `retry_delay_ms` / `should_retry`
// （`conn/tunnel_model.h`，裁决 7.96 已由 `utest_tunnel_model` 锁过表），会话腿与隧道腿共用
// 一份翻倍钳顶的算式（裁决 7.99 D4①：不为它单拆公共头）。
//
// 本头不携带 libssh 类型、不碰线程与 UI，全部函数可无头单测（`utest_reconnect`）。
// ============================================================

#include <cstdint>
#include <optional>

#include "borealis/conn/tunnel.h"          // conn::RetryPolicy
#include "borealis/session/connection.h"   // session::CloseReason
#include "conn/sftp_model.h"               // conn::SftpError

namespace borealis::conn {

/// @brief 自动重连停在哪儿：类型定义在公共头（随进度快照跨连接/会话边界），本件只消费。
using session::ReconnectStop;

/// @brief 该关闭原因是否值得自动重拨（分类表见裁决 7.99 D2①）。
///
/// 可重连＝Active 期间掉线（`LinkLost`）与建连阶段的网络类失败（`DialNetwork`）；
/// 不可重连＝认证失败、主机密钥与策略拒绝、远端正常退出、本端主动关闭、归因不明。
/// 把 `HostKeyRejected` 与 `AuthFailed` 排除在外是安全裁决而非效率取舍：对二者重拨
/// 只会重复失败，且可能触发远端账号锁定，与裁决 7.94④「未知即拒绝，绝不静默放行」同向。
[[nodiscard]] auto should_reconnect(session::CloseReason reason) -> bool;

/// @brief 会话腿的缺省退避档：重拨 3 回，退避 1 s→2 s→4 s（首次 1 s、逐次翻倍钳到 30 s，
///        上限 30 s）（裁决 7.99 D4①）。
///
/// 装配层把 `settings.connection.ssh` 域的重连三键（裁决 7.99 D4①，`UI_RECONNECT.draft.md`
/// §3）搬进 `RetryPolicy` 后交进来；本函数只在配置面读不到时作回落。缺省次数取 3 而不是
/// 不限——不限次会把「连不上」变成静默无限重试，用户只能靠肉眼看屏幕不动。
[[nodiscard]] auto default_reconnect_policy() -> RetryPolicy;

/// @brief 第 @p redial_index 回重拨（自 1 起）该不该拨、要等多久。
///
/// 回 `nullopt`＝次数已用尽，调用方据此落 `ReconnectStop::AttemptsExhausted`；回值＝拨之前
/// 该睡多少毫秒。与 `should_reconnect` 是两问：那条看**原因**值不值得，这条看**次数**够不够，
/// 二者都过才真去拨。
///
/// **为什么这里差一格**：`conn::RetryPolicy::max_attempts` 的既定语义是「第 N 次失败即止」
/// （隧道腿 `should_retry(attempt + 1, ...)` 的调用点即此口径，实际重拨 N−1 回），而本键面向
/// 用户的话是「自动重连次数＝还能再拨几回」，故闸门写在本件、以 `should_retry(redial_index - 1)`
/// 表达，缺省档 3 即「重拨 3 回，退避 1 s→2 s→4 s，第 4 次失败落终态」（草图屏 A/B 的读数）。
/// 模型与隧道腿的语义一字不动，只在此处换算。
/// @param redial_index 这是本次掉线以来的第几回重拨；<1 按 1 处理。
[[nodiscard]] auto reconnect_step(int redial_index, const RetryPolicy &policy) -> std::optional<int>;

/// @brief 由关闭原因与「是否因用尽而停」合成终态档位。
///
/// `exhausted` 只在 `should_reconnect(reason)` 为真时才有意义（原因本身不值得重拨时，
/// 用没用尽都不改变落哪一档）。
[[nodiscard]] auto reconnect_stop(session::CloseReason reason, bool exhausted) -> ReconnectStop;

/// @brief 该 SFTP 失败是否属「会话链路已掉」（裁决 7.99 D7④ 的判据）。
///
/// 只有 `Network`（连接/会话级失败）与 `NotConnected`（会话已不在）算掉线——服务器拒绝、
/// 路径不存在、本地读写失败、协议意外都是**重拨也解决不了**的原因，把它们当掉线会让面板
/// 对着一份没坏的会话反复重连。`Cancelled` 更不是：那是调用方自己要停的。
[[nodiscard]] auto sftp_error_drops_link(SftpError error) -> bool;

}  // namespace borealis::conn
