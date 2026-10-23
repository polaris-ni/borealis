#pragma once

// ============================================================
// 本机 ssh-agent 字节通道（src/platform/agent_socket.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.02 的 agent 转发（ForwardAgent）在客户端一侧要把服务器反向
// 打开的 auth-agent 通道接到**本机** agent 上。agent 的寻址与句柄是平台假设
// （POSIX＝`SSH_AUTH_SOCK` 指向的 unix socket，Win32＝OpenSSH 的命名管道），
// 故只进 src/platform/{win,posix}/ 实现（AGENTS.md §4.5 第 23 条）；conn 层
// 只看见下面这枚字节管道。
//
// 语义收窄：只做「连上并原样搬运字节」，不解析 agent 协议——SSH2 agent 协议
// 的消息边界由远端与 agent 两端自己维持，本仓是透明中继。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace borealis::platform {

/// @brief 一次读的结果分档：Data 之外都算「这一拍没货」，Closed 与 Error 的
///        区别供中继区分「agent 正常收摊」与「真错」。
enum class AgentReadOutcome : std::uint8_t {
    Data,     ///< 读到 bytes 个字节。
    Timeout,  ///< 超时窗口内无可读。
    Closed,   ///< agent 侧有序关闭（EOF）。
    Error,    ///< 句柄错误（已记 platform 日志）。
};

/// @brief read() 返回值：分档 + 字节数（仅 Data 档有意义）。
struct AgentRead {
    AgentReadOutcome outcome{AgentReadOutcome::Error};
    std::size_t bytes{0};
};

/// @brief 一条已建立的本机 agent 字节管道（单向所有权）。
class AgentSocket {
  public:
    AgentSocket() = default;
    virtual ~AgentSocket() = default;

    AgentSocket(const AgentSocket &) = delete;
    auto operator=(const AgentSocket &) -> AgentSocket & = delete;
    AgentSocket(AgentSocket &&) = delete;
    auto operator=(AgentSocket &&) -> AgentSocket & = delete;

    /// @brief 带超时读；out 为空恒回 Timeout（不为零长度阻塞）。
    [[nodiscard]] virtual auto read(std::span<std::uint8_t> out, int timeout_ms) -> AgentRead = 0;

    /// @brief 全量写（内部吞掉部分写与 EINTR）；对端断开或超时回 false。
    virtual auto write(std::span<const std::uint8_t> bytes) -> bool = 0;

    /// @brief 关闭并释放底层句柄（幂等；析构自动调用）。
    virtual auto close() -> void = 0;
};

/// @brief 连本机 agent：环境变量缺失、路径非法或连不上回 nullptr（原因进
///        platform 日志）。每次反向通道各连一次，不共享句柄——agent 协议是
///        逐连接会话，复用会让两条远端请求的应答串线。
[[nodiscard]] auto agent_connect() -> std::unique_ptr<AgentSocket>;

}  // namespace borealis::platform
