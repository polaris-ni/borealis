#pragma once

// ============================================================
// SSH 跳链建立与逐跳中继（src/conn/ssh_hop_chain.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.03` 跳板机（设计稿 `codespec/CONN03_PROXYJUMP.draft.md` D4）的
// 拨号腿：按 `SshProfile::proxy_jumps` 的跳序逐跳跑「连接 → 主机密钥核对 → 认证」
// （全部复用 `ssh_dial_and_authenticate()`，于是每一跳都吃本仓自己的策略、算法覆盖项
// 与日志口径），跳与跳之间用 `direct-tcpip` 通道加一对本地句柄
// （`platform::make_hop_pair()`，稿 D10）接起来，最后一跳交出的句柄即目标会话的 seed。
//
// 所有权与关闭序（稿 §5：F12 是硬约束，探针踩过段错误）：
//   - 每跳一根中继线程，线程持有该跳的会话与通道指针 ⇒ `close()` 必须先置停止位、
//     join 该线程，再关通道、断会话、关句柄对；越过「先 join 后释放」就是未定义行为。
//   - `seed_fd()` 交出去的那一端**仍归本对象所有**：libssh 0.10 起 `ssh_disconnect()`
//     不关 `SSH_OPTIONS_FD` 递进去的句柄。因此调用方必须在 `close()` 之前把吃这个 seed
//     的会话断开并释放，`close()` 才按逆序关掉两端。
//   - 跳板的明文只活到该跳拨号返回（`ssh_dial_and_authenticate()` 按值收 secret，
//     函数内就地清副本）；本对象不保留任何跳的秘密材料。
//
// 线程边界：`open()` 与 `close()` 都阻塞（逐跳各一次建连，可数秒），必须在非 UI 线程上
// 调用，且与消费它的 SSH 会话同线程（libssh 会话不跨线程）。0 跳链路是合法的空操作：
// `open()` 回 true、`seed_fd()` 回 0，调用方按直连拨即可。
//
// 本头是私有头（裁决 D1① 同款纪律）：libssh 与平台句柄类型只出现在实现文件里。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "borealis/conn/profile.h"
#include "conn/ssh_dial.h"

namespace borealis::conn {

/// @brief 一条自建 `direct-tcpip` 跳链：N 枚跳板会话 + N 根中继线程 + 一枚交出的 seed。
class SshHopChain final {
  public:
    // 构造/析构都定义在实现文件里：pimpl 的 `unique_ptr<Impl>` 要等 `Impl` 完全类型才
    // 能生成（头里 `= default` 会让用例侧一包含本头就撞上不完整类型）。
    SshHopChain();
    ~SshHopChain();

    SshHopChain(const SshHopChain &) = delete;
    auto operator=(const SshHopChain &) -> SshHopChain & = delete;
    SshHopChain(SshHopChain &&) = delete;
    auto operator=(SshHopChain &&) -> SshHopChain & = delete;

    /// @brief 按跳序建立链路：第 0 跳走真 TCP，其后每一跳经上一跳的转发通道拨号。
    ///
    /// 任一跳失败就就地收口已建好的部分（不留悬着的线程与句柄），回 false。
    /// @param target        目标档案：跳链取它的 `proxy_jumps`（数组序＝跳序，第 0 项最靠近
    ///                      本机），**最后一跳的转发目的地**取它的 host 与生效端口——
    ///                      这一枚形参是稿 D4 签名落地时补的：只给跳链就表达不出
    ///                      「跳板要转发到哪个目标」。
    /// @param hop_secrets   逐跳的秘密材料，与 `target.proxy_jumps` 同序；短于跳链时缺的那几枚
    ///                      按「没有秘密」处理（agent 认证本就无需材料，password 缺材料
    ///                      由 `ssh_dial_and_authenticate()` 落 `Auth` 档）。
    /// @param algorithms    算法覆盖项：原样带给每一跳（稿 D1 的结构性收益——逐跳同策略）。
    /// @return false＝链路没建起来；失败档位与卡住的那一跳见 `failure()`。
    [[nodiscard]] auto open(const SshProfile &target,
                            const std::vector<std::optional<std::string>> &hop_secrets,
                            const SshAlgorithmOverrides &algorithms) -> bool;

    /// @brief 上一次 `open()` 的失败归因；成功或未建链时为 `{Ok, 0}`。
    ///
    /// `outcome` 是那一跳自己的拨号档位（`Network`/`HostKey`/`Auth`……），
    /// `hop_index` 是它在 @p hops 里的下标——跳板侧的失败与目标侧的失败因此分得开。
    [[nodiscard]] auto failure() const noexcept -> DialFailure;

    /// @brief 目标会话的 seed 句柄（0＝没有链路，按直连拨）。`close()` 之后回 0。
    [[nodiscard]] auto seed_fd() const noexcept -> std::uintptr_t;

    /// @brief 逆序收口：置停止位 → join 中继 → 关通道 → 断会话 → 关句柄对（幂等）。
    ///
    /// 前置条件：吃本链路 seed 的那枚会话已经 `ssh_disconnect()` + `ssh_free()`——
    /// 顺序颠倒会关掉仍在被使用的句柄（稿 §5 的关闭序）。
    auto close() -> void;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace borealis::conn
