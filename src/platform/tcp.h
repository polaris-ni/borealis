#pragma once

// ============================================================
// TCP 转发腿接口（src/platform/tcp.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.08（SSH 隧道）在「本端」一侧需要的监听与字节流能力：
// 裸 socket API（POSIX socket/poll 与 Winsock2）是平台假设，只进
// src/platform/{win,posix}/ 实现（AGENTS.md §4.5 第 23 条，裁决 7.11 同款
// 纪律）；conn 层的隧道传输腿只消费下面两个抽象类加两个工厂函数。
//
// 本期射程收窄（裁决 7.96）：仅 IPv4；阻塞语义 + 每调用超时（隧道传输腿
// 每条隧道一个工作线程，串行受理连接，故接口不设并发 accept 面）。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace borealis::platform {

/// @brief 一次带超时读的结果分档：Data 之外都算「这一拍没货」，语义不同供
///        转发泵区分「对端正常收摊」与「真错」。
enum class TcpReadOutcome : std::uint8_t {
    Data,     ///< 读到 bytes 个字节。
    Timeout,  ///< 超时窗口内无可读。
    Closed,   ///< 对端有序关闭（recv 返回 0）。
    Error,    ///< socket 错误（已记日志）。
};

/// @brief read() 返回值：分档 + 字节数（仅 Data 档有意义）。
struct TcpRead {
    TcpReadOutcome outcome{TcpReadOutcome::Error};
    std::size_t bytes{0};
};

/// @brief 一条已建立的 TCP 字节流（单向所有权：unique_ptr 交到谁谁负责）。
class TcpStream {
  public:
    TcpStream() = default;
    virtual ~TcpStream() = default;

    TcpStream(const TcpStream &) = delete;
    auto operator=(const TcpStream &) -> TcpStream & = delete;
    TcpStream(TcpStream &&) = delete;
    auto operator=(TcpStream &&) -> TcpStream & = delete;

    /// @brief 带超时读；out 为空恒回 Timeout 档（不为零长度阻塞）。
    [[nodiscard]] virtual auto read(std::span<std::uint8_t> out, int timeout_ms) -> TcpRead = 0;

    /// @brief 全量写（内部吞掉部分写与 EINTR）；对端断开或出错回 false。
    virtual auto write(std::span<const std::uint8_t> bytes) -> bool = 0;

    /// @brief 关闭并释放底层句柄（幂等；析构自动调用）。
    virtual auto close() -> void = 0;

    /// @brief 对端点分 IPv4（accept 侧用于 direct-tcpip 的 originator）；取不到回空串。
    [[nodiscard]] virtual auto peer_address() const -> std::string = 0;

    /// @brief 对端端口；取不到回 0。
    [[nodiscard]] virtual auto peer_port() const -> int = 0;
};

/// @brief 本端 TCP 监听点。
class TcpListener {
  public:
    TcpListener() = default;
    virtual ~TcpListener() = default;

    TcpListener(const TcpListener &) = delete;
    auto operator=(const TcpListener &) -> TcpListener & = delete;
    TcpListener(TcpListener &&) = delete;
    auto operator=(TcpListener &&) -> TcpListener & = delete;

    /// @brief 带超时受理；超时回 nullptr（隧道工作线程按拍检查停止标志）。
    [[nodiscard]] virtual auto accept(int timeout_ms) -> std::unique_ptr<TcpStream> = 0;

    /// @brief 实际绑定的端口（0 端口择定后问这个）。
    [[nodiscard]] virtual auto bound_port() const -> int = 0;

    /// @brief 关闭监听句柄（幂等；accept 随即回 nullptr）。
    virtual auto close() -> void = 0;
};

/// @brief 建立监听点：bind_address 空＝INADDR_ANY，port 0＝内核择定；失败回
///        nullptr（原因已记 platform 日志）。
[[nodiscard]] auto tcp_listen(const std::string &bind_address, int port)
    -> std::unique_ptr<TcpListener>;

/// @brief 带超时建连（域名走系统解析，仅取 IPv4 结果）；失败回 nullptr。
[[nodiscard]] auto tcp_connect(const std::string &address, int port, int timeout_ms)
    -> std::unique_ptr<TcpStream>;

}  // namespace borealis::platform
