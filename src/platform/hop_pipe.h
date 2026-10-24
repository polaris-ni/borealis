#pragma once

// ============================================================
// 跳链中继的本地字节对（src/platform/hop_pipe.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.03` 跳板机（设计稿 `codespec/CONN03_PROXYJUMP.draft.md` D10）：
// 自建 `direct-tcpip` 跳链里，跳板会话上的转发通道与下一跳的 SSH 会话之间需要一对
// **可对等读写的本地句柄**——一端交给 `ssh_dial_and_authenticate()` 写进
// `SSH_OPTIONS_FD`，另一端由该跳的中继线程搬字节。造这对句柄的手段（POSIX
// `socketpair` 与 Winsock 回环 TCP）是平台假设，只进 `src/platform/{posix,win}/`
// 实现（AGENTS.md §4.5 第 23 条）；conn 层只看见下面这一枚字节管道。
//
// 语义收窄：只做「造一对并原样搬运字节」，不解释 SSH 报文——线状的字节序由两端
// 各自维持，本件是透明中继（同 `platform/agent_socket.h` 的口径）。
//
// 句柄值的边界：`SSH_OPTIONS_FD` 那一档以 **0 表示「不走 seed」**（见
// `conn/ssh_dial.h`），故本件交出的句柄必须非 0。POSIX 侧 0/1/2 三档在正常进程里
// 已被标准流占用，`socketpair` 拿到的必然 ≥3；若宿主以关闭stdin的形态启动，这条
// 前提不成立——本仓的启动路径没有那种形态（GUI 会话与测试 runner 都带着标准流）。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace borealis::platform {

/// @brief 一次读的结果分档：Data 之外都算「这一拍没货」，Closed 与 Error 的区别供
///        中继区分「对端正常收摊」与「真错」（同 `TcpReadOutcome` 的分档口径）。
enum class HopPipeReadOutcome : std::uint8_t {
    Data,     ///< 读到 bytes 个字节。
    Timeout,  ///< 超时窗口内无可读。
    Closed,   ///< 对端有序关闭（读到 EOF）。
    Error,    ///< 句柄错误（已记 platform 日志）。
};

/// @brief read() 返回值：分档 + 字节数（仅 Data 档有意义）。
struct HopPipeRead {
    HopPipeReadOutcome outcome{HopPipeReadOutcome::Error};
    std::size_t bytes{0};
};

/// @brief 字节对的一头（单向所有权：`unique_ptr` 交到谁谁负责关闭）。
class HopPipeEnd {
  public:
    HopPipeEnd() = default;
    virtual ~HopPipeEnd() = default;

    HopPipeEnd(const HopPipeEnd &) = delete;
    auto operator=(const HopPipeEnd &) -> HopPipeEnd & = delete;
    HopPipeEnd(HopPipeEnd &&) = delete;
    auto operator=(HopPipeEnd &&) -> HopPipeEnd & = delete;

    /// @brief 带超时读；out 为空恒回 Timeout 档（不为零长度阻塞）。
    [[nodiscard]] virtual auto read(std::span<std::uint8_t> out, int timeout_ms) -> HopPipeRead = 0;

    /// @brief 全量写（内部吞掉部分写与 EINTR）；对端断开或出错回 false。
    virtual auto write(std::span<const std::uint8_t> bytes) -> bool = 0;

    /// @brief 关闭并释放底层句柄（幂等；析构自动调用）。
    virtual auto close() -> void = 0;

    /// @brief 平台句柄的原值（POSIX＝fd，Win32＝SOCKET），供 `SSH_OPTIONS_FD` 用。
    ///        关闭之后回 `kInvalidHopPipeHandle`。
    [[nodiscard]] virtual auto handle() const noexcept -> std::uintptr_t = 0;
};

/// @brief 已关闭的句柄哨兵（与 libssh 的 `SSH_INVALID_SOCKET` 同型：按位取反）。
inline constexpr std::uintptr_t kInvalidHopPipeHandle = ~std::uintptr_t{0};

/// @brief 一对可对等读写的本地句柄。
///
/// 两端的分工是命名性的、不是类型性的：`session_end` 交给下一跳（或目标）的 SSH
/// 会话，`relay_end` 留在本跳的中继线程。**关闭责任全在造对的一方**——libssh 的
/// `ssh_disconnect()` 不会关掉 `SSH_OPTIONS_FD` 递进去的句柄（库 0.10 起的明确语义），
/// 所以持有 `session_end` 的会话必须先断开，再关这一端（稿 §5 的 F12 关闭序）。
struct HopPair {
    std::unique_ptr<HopPipeEnd> session_end{};
    std::unique_ptr<HopPipeEnd> relay_end{};
};

/// @brief 造一对本地字节句柄；失败回 `nullopt`（原因已记 platform 日志）。
[[nodiscard]] auto make_hop_pair() -> std::optional<HopPair>;

}  // namespace borealis::platform
