#pragma once

// ============================================================
// SSH 跳板链纯逻辑件（src/conn/proxy_jump.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.03` 的延后子项「跳板机（ProxyJump 语义）」的解析／规范化／校验层
// （设计稿 `codespec/CONN03_PROXYJUMP.draft.md` D3）：向导那一行的字符串、
// `~/.ssh/config` 导入的 `ProxyJump` 值、档案里的 `proxy_jumps` 数组、连接复用的
// 共享键串形，四处都经本件折算，于是「跳链怎么表达」只有一处答案。
//
// 本层不碰 libssh、不碰 socket、不碰 UI（AGENTS.md §4.4 第 20 条）：拨号侧的
// `conn/ssh_hop_chain.h` 才是碰到库的那一层。
//
// 语法取 OpenSSH 的 `[user@]host[:port]` 逗号串（裁决稿 F10：数组第 0 项＝最靠近
// 本机的第一跳，与 `-J`／`ProxyJump` 的语义同型）。整串拒绝是刻意的口径：部分接受
// 会让用户以为跳板生效了，而实际连的是另一条链路。
// ============================================================

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/conn/profile.h"

namespace borealis::conn {

/// @brief 跳数上限（设计稿 D2）：解析侧与保存侧两处都拒，防的是「向导里误粘一大串」。
inline constexpr std::size_t kMaxProxyHops = 8;

/// @brief 把 OpenSSH 的 `[user@]host[:port]` 逗号串折成跳链。
///
/// - 空串与 `none` 都回**空链路**（合法值：`none` 是库与 OpenSSH 共用的「显式关闭」写法）；
/// - 端口留空折 22，用户留空即空串（＝跟随 SSH 缺省，与 `SshProfile::user` 同口径）；
/// - `@` 之前是用户（IPv6 写作 `user@[::1]:22`），主机的 `%h`／`%p` 一类宏不展开，
///   含 `%` 直接算非法 token；
/// - 任一枚 token 非法（空、缺主机、端口非数字或越界、含空白或控制字符、条数超
///   `kMaxProxyHops`）⇒ 整串回 `nullopt`，不部分接受。
/// @param default_policy 逐跳的主机密钥策略缺省值：向导按档案缺省（`AcceptNew`），
///        `~/.ssh/config` 导入传该 Host 块 `StrictHostKeyChecking` 的折算结果（稿 D7）。
[[nodiscard]] auto parse_proxy_jump_list(std::string_view text,
                                         KnownHostsPolicy default_policy =
                                             KnownHostsPolicy::AcceptNew)
    -> std::optional<std::vector<SshHop>>;

/// @brief 跳链的规范化串形（逐项 `user@host:port`，逗号连接；空链路回空串）。
///
/// 端口**总是写出来**：留空与 `:22` 在解析时折成同一个值，串形也必须收敛成同一个串，
/// 否则连接复用的共享键会因为写法不同而分键（稿 D5）。
[[nodiscard]] auto render_proxy_jump_list(const std::vector<SshHop> &hops) -> std::string;

/// @brief 整条链路的合法性闸（档案保存与快照重放前跑一次；D2 的同一条判据）。
///
/// 与 `parse_proxy_jump_list` 用的是同一套规则，但输入已经是结构化跳链——向导那一行
/// 走解析、JSON 与 `~/.ssh/config` 那两路走本函数。
[[nodiscard]] auto validate_proxy_hops(const std::vector<SshHop> &hops) -> bool;

}  // namespace borealis::conn
