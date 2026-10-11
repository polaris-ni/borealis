#pragma once

// ============================================================
// 串口平台腿接口（src/platform/serial.h）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.05（串口终端）在「本端」一侧需要的设备枚举与字节流能力：
// termios（POSIX）与 COMM/DCB（Win32）是平台假设，只进 src/platform/{posix,win}/
// 实现（AGENTS.md §4.5 第 23 条，platform/tcp.h 同款纪律）；conn 层的串口连接腿
// 只消费下面的抽象类加两个工厂函数。
//
// 本头**含** conn/serial_model.h 的类型：那里是档位表、校验与折法的唯一一份
// （裁决 7.110 D4①/D6②），平台腿只把已折好的 `SerialFrameBits` 翻译成 termios
// 位或 DCB 字段——第 23 条画的是「平台知识不外溢」的线，不是「类型不下沉」的线。
//
// 本期射程收窄：打开即置参、单线程读写、每调用超时（串口传输腿一条会话一个
// 读线程，UI 线程零阻塞 IO，第 25 条）；调制解调器线状态不做（F7：替身设备
// TIOCMGET 回 ENOTTY，无证人可判）。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "conn/serial_model.h"

namespace borealis::platform {

/// @brief 一次带超时读的结果分档：与 platform/tcp.h 的 TcpReadOutcome 同口径。
///        串口没有「远端进程结束」的概念，EOF 只代表设备没了（F9、裁决 7.110 D7）。
enum class SerialReadOutcome : std::uint8_t {
    Data,     ///< 读到 bytes 个字节。
    Timeout,  ///< 超时窗口内无可读。
    Closed,   ///< 读到 EOF（替身设备与拔线后的常见形态）。
    Error,    ///< 读失败（含 EIO；已记 platform 日志）。
};

/// @brief read() 返回值：分档 + 字节数（仅 Data 档有意义）。
struct SerialRead {
    SerialReadOutcome outcome{SerialReadOutcome::Error};
    std::size_t bytes{0};
};

/// @brief 打开失败的归因档（errno/HRESULT 折成有限几类，供 conn 层映射
///        `session::CloseReason`——裁决 7.110 D7① 的映射表只在那一处）。
enum class SerialFailure : std::uint8_t {
    None,            ///< 成功。
    Denied,          ///< 权限不足（EACCES/EPERM）：节点在，当前用户开不动。
    NoNode,          ///< 节点不存在（ENOENT/ENODEV）。
    Hangup,          ///< 节点已挂断/暂缺（ENXIO：拔掉但未消失的 USB 口常见）。
    Busy,            ///< 已被占用（EBUSY 或独占请求被拒）。
    Io,              ///< 其他 IO 错误。
    ConfigRejected,  ///< 参数写不下去（tcsetattr/GetCommState 报错，或速率不在腿的支持表）。
};

/// @brief 一个已打开并置好参数的串口。
class SerialPort {
  public:
    SerialPort() = default;
    virtual ~SerialPort() = default;

    SerialPort(const SerialPort &) = delete;
    auto operator=(const SerialPort &) -> SerialPort & = delete;
    SerialPort(SerialPort &&) = delete;
    auto operator=(SerialPort &&) -> SerialPort & = delete;

    /// @brief 全量写（内部处理部分写与 EINTR）；设备断开、出错或写超时回 false。
    virtual auto write(std::span<const std::uint8_t> bytes) -> bool = 0;

    /// @brief 带超时读；out 为空恒回 Timeout 档（不为零长度阻塞）。
    [[nodiscard]] virtual auto read(std::span<std::uint8_t> out, int timeout_ms) -> SerialRead = 0;

    /// @brief 连接中热改波特率：写入后立即回读比对，相符才算成功（裁决 7.110 D6②）。
    ///        回读不符或系统调用报错一律回 false，调用侧界面保持原档。
    virtual auto set_baud(int baud) -> bool = 0;

    /// @brief 实际生效的波特率：最后一次成功的 `set_baud` 或打开时的档位。
    [[nodiscard]] virtual auto current_baud() const -> int = 0;

    /// @brief 关闭并释放底层句柄（幂等；析构自动调用）。
    virtual auto close() -> void = 0;
};

/// @brief 打开结果：成功带 port，失败带归因（两者互斥，用 ok() 分辨）。
struct SerialOpenResult {
    std::unique_ptr<SerialPort> port;
    SerialFailure failure{SerialFailure::None};

    [[nodiscard]] auto ok() const noexcept -> bool { return port != nullptr; }
};

/// @brief 打开设备并一次性置好帧格式与流控；失败回带归因的空结果（原因已记日志）。
/// @param device_path 原样交给平台的节点名（`/dev/ttyUSB0`、`COM3` 等）。
/// @param frame       `conn::fold_serial_frame` 折出来的生效参数。
[[nodiscard]] auto serial_open(const std::string &device_path, const conn::SerialFrameBits &frame)
    -> SerialOpenResult;

/// @brief 枚举本端串口节点并逐个试开定可达性（裁决 7.110 D8①：只在向导开行、
///        点刷新、启动取三处调用；本函数**含目录 IO**，调用方负责离开 UI 线程）。
/// @return 未排序的原始行；排序与去重交 `conn::sort_serial_ports`。
[[nodiscard]] auto serial_list_ports() -> std::vector<conn::SerialPortInfo>;

}  // namespace borealis::platform
