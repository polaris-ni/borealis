#pragma once

// ============================================================
// Win32 ConPTY 连接（src/platform/win/conpty_connection.h）
// ------------------------------------------------------------
// 平台层私有头：Win32 类型只出现在本目录内，共享路径经 `session::Connection` 抽象调用
// （裁决 7.11、架构 §2.3 第 1 条）。
//
// 线程形态按架构 §3.1：每会话一个读线程，读到的原始字节经 `ConnectionEvents::on_bytes`
// 交给会话；解码、解析与网格写入都在会话侧完成，本类只管进程与管道。
// ============================================================

#include <windows.h>

#include <atomic>
#include <mutex>
#include <span>
#include <thread>

#include "borealis/conn/local_terminal.h"
#include "borealis/session/connection.h"

namespace borealis::platform {

/// @brief 本地终端的 Win32 实现：`CreatePseudoConsole` + 子进程 + 读线程。
class ConptyConnection final : public session::Connection {
  public:
    /// @param spec 启动规格（命令行、启动目录、追加环境变量）。
    /// @param initial_size 伪终端初始尺寸，创建时即生效（`SPEC.FEAT.XFER.01` 的会话启动腿）。
    ConptyConnection(conn::LocalTerminalSpec spec, session::Size initial_size);

    ConptyConnection(const ConptyConnection &) = delete;
    ConptyConnection(ConptyConnection &&) = delete;
    auto operator=(const ConptyConnection &) -> ConptyConnection & = delete;
    auto operator=(ConptyConnection &&) -> ConptyConnection & = delete;

    ~ConptyConnection() override;

    auto start(session::ConnectionEvents &events) -> void override;

    auto write(std::span<const std::byte> bytes) -> void override;

    auto resize(session::Size size) -> void override;

    auto close() -> void override;

    [[nodiscard]] auto alive() const noexcept -> bool override;

  private:
    /// @brief 读线程主体：阻塞读输出管道直到 EOF，随后通知会话流结束。
    auto read_loop() -> void;

    /// @brief 创建伪终端并挂上子进程；任一步失败即回收已创建的句柄并返回 false。
    /// @param events 事件接收端，spawn 成功后由读线程使用。
    /// @return 子进程是否已启动。
    auto spawn(session::ConnectionEvents &events) -> bool;

    conn::LocalTerminalSpec spec_;
    session::Size size_;

    /// 护住 @c console_ 句柄的使用与关停，使 `resize` 不会撞上 `ClosePseudoConsole`。
    std::mutex state_mutex_;
    /// 串行化写管道：用户按键来自主线程、查询应答来自读线程，两条路交织会把一个转义序列拆散。
    std::mutex write_mutex_;

    HPCON console_ = nullptr;
    HANDLE input_write_ = nullptr;
    HANDLE output_read_ = nullptr;

    /// 子进程句柄：`alive()` 无锁读它，故原子化且只在析构回收。主线程句柄与伪终端的输入/输出端
    /// 在移交 conhost 后即关闭（见 `spawn`），本类只留自己读写的那两端。
    std::atomic<HANDLE> process_{nullptr};

    session::ConnectionEvents *events_ = nullptr;
    std::thread reader_;

    /// 关停标记：`close()` 的抢占、`write()`/`resize()` 的提前退出都以它为准。
    std::atomic<bool> closing_{false};
    std::atomic<bool> started_{false};
};

}  // namespace borealis::platform
