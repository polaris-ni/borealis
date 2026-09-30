#pragma once

// ============================================================
// 连接基础接口（include/borealis/session/connection.h）
// ------------------------------------------------------------
// 架构 §7.2 的粒度已拍板为「基础接口 + 能力接口组合」：所有连接类型共同实现本文件的
// 基础接口（生命周期、字节读写、尺寸下发），SSH 才有的面（SFTP / 隧道 / 执行通道）
// 到 M3 以**独立能力接口**增补，不改本接口，也不塞进来让本地终端背上空实现。
//
// 平台假设（PTY、串口、传输层）一律留在 src/platform/ 下的实现里，本头只有标准类型
// （裁决 7.11、架构 §2.3 第 1 条）——否则后补 Linux 等价时会变成重写而非补实现。
// ============================================================

#include <cstddef>
#include <span>

namespace borealis::session {

/// @brief 视口尺寸（列 × 行），PTY 尺寸同步的传递单位（`SPEC.FEAT.XFER.01`）。
struct Size {
    std::size_t columns = 0;
    std::size_t rows = 0;
};

/// @brief 连接 → 会话的事件接收端。
///
/// 回调发生在**连接的读线程**上（架构 §3.1：每会话一个读线程），实现方不得在此触达
/// UI 状态（架构 §3.2）。
class ConnectionEvents {
  public:
    virtual ~ConnectionEvents() = default;

    /// @brief 收到一段原始字节（任意块大小，可切断多字节序列）。
    /// @param bytes 原始字节。
    virtual auto on_bytes(std::span<const std::byte> bytes) -> void = 0;

    /// @brief 对端进程已退出：会话停止读取但**保留网格内容**供回看（架构 §7.3）。
    virtual auto on_closed() -> void = 0;
};

/// @brief 所有连接类型共同实现的基础接口。
///
/// 会话层只经本接口调用传输能力，不知道对面是 ConPTY、SSH 还是串口设备。
class Connection {
  public:
    virtual ~Connection() = default;

    /// @brief 启动连接并在其读线程上开始投递事件。
    /// @param events 事件接收端，生命周期由调用方保证，且不短于 `close()` 返回。
    virtual auto start(ConnectionEvents &events) -> void = 0;

    /// @brief 向对端写入字节（会话编码已在会话层完成，`SPEC.FEAT.TERM.09` 发送方向）。
    /// @param bytes 待发送字节。
    virtual auto write(std::span<const std::byte> bytes) -> void = 0;

    /// @brief 下发新的视口尺寸，令对端按新尺寸重排（`SPEC.FEAT.XFER.01`）。
    ///
    /// 去抖合并由调用方负责（连续拖拽期间只在帧边界下发一次），本接口只表达「最终尺寸」。
    /// @param size 新尺寸。
    virtual auto resize(Size size) -> void = 0;

    /// @brief 主动结束连接（关闭标签、退出会话）。
    ///
    /// 返回后实现不得再回调 @c ConnectionEvents。
    virtual auto close() -> void = 0;

    /// @brief 对端进程是否仍在运行（`SPEC.FEAT.WS.01` 的关闭前确认据此判定）。
    [[nodiscard]] virtual auto alive() const noexcept -> bool = 0;
};

}  // namespace borealis::session
