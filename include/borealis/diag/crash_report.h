#pragma once

// ============================================================
// 崩溃留存腿的诊断快照（include/borealis/diag/crash_report.h）
// ------------------------------------------------------------
// `SPEC.NF.RELI.01` 的「崩溃时留存会话与诊断信息」一腿（裁决 7.142）。形态是**自写诊断文件**：
// 崩溃处理函数在信号上下文里写一份固定文本，只含结构计数与每格身份（标签号 / 格号 / 连接类型 /
// 是否存活），**不含终端文本、标签名、主机名与任何凭据**——终端输出可能含口令，而会话日志默认
// 关闭（裁决 7.9），把屏幕内容搬进一份默认开启的崩溃文件等于绕过那条默认。
//
// 分工：
//  - `CrashFactsStore`：UI 线程发布、崩溃处理函数读取的双缓冲。写侧只写**非当前**那一份，
//    写完才翻指针 ⇒ 崩溃即使发生在发布中途，读侧拿到的仍是上一份完整快照。
//  - `emit_crash_report()`：把一份快照按固定文本写出。只用栈与固定缓冲、不分配、不取锁，
//    因此可在信号处理函数里调用；同一函数也是格式的唯一出处（用例直接断它的输出）。
//
// 平台知识不外溢（AGENTS.md §4.5 第 23 条）：信号 / 异常注册与文件句柄在
// src/platform/{posix,win}/crash_handler.cpp，本头只交数据形态与文本格式。
// ============================================================

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "borealis/conn/profile.h"

namespace borealis::diag {

/// @brief 一份崩溃快照最多记下的会话条数（超出只计数、不记身份）。
inline constexpr std::size_t kMaxCrashSessions = 128;

/// @brief 一格会话的身份与存活（不含任何会话文本）。
struct CrashSessionFact {
    std::uint64_t tab = 0;   ///< 标签号（进程内单调，不复用）。
    std::uint64_t pane = 0;  ///< 格号（标签树内单调，不复用）。
    conn::ConnectionType kind = conn::ConnectionType::Local;  ///< 连接类型。
    bool alive = false;  ///< 发布那一刻该会话的连接是否仍活着。
};

/// @brief 崩溃处理函数要写的全部事实。
struct CrashFactsSnapshot {
    std::uint64_t windows = 0;  ///< 窗数。
    std::uint64_t tabs = 0;     ///< 标签数（全部窗）。
    std::uint64_t panes = 0;    ///< 会话数（全部标签；含超出记录上限的那些）。
    std::uint64_t last_publish_epoch = 0;  ///< 上一次发布的 epoch 秒（与崩溃时刻对照看快照有多旧）。
    std::size_t session_count = 0;  ///< `sessions` 里有效的条数（发布侧已夹到上限）。
    std::array<CrashSessionFact, kMaxCrashSessions> sessions{};
};

/// @brief UI 线程发布、崩溃处理函数读取的双缓冲快照。
///
/// 线程口径：`publish()` 只许一个线程（装配层的帧回调）调用；`latest()` 可在任意线程调用，
/// 只用无锁原子，故崩溃处理函数可用。`latest()` 返回副本，故发布侧随后翻缓冲不会改动它。
class CrashFactsStore {
  public:
    /// @brief 发布一份新快照（写入非当前缓冲，写完才翻指针）。
    void publish(const CrashFactsSnapshot &snapshot) noexcept;

    /// @brief 取当前已完整发布的那一份（上一份或最新一份，永不是半份）。
    [[nodiscard]] auto latest() const noexcept -> CrashFactsSnapshot;

  private:
    std::array<CrashFactsSnapshot, 2> buffers_{};
    std::atomic<std::uint32_t> published_{0};
};

/// @brief 固定文本的写出回调（崩溃处理函数里是 `write()` / `WriteFile()` 的薄壳）。
using CrashEmitter = void (*)(void *context, const char *data, std::size_t size);

/// @brief 产品版本（编译期注入；未注入时为 `unknown`）。
[[nodiscard]] auto product_version() noexcept -> const char *;

/// @brief 把一份快照按固定的 ASCII 文本发出去。
///
/// 不分配内存、不取锁、不碰文件系统——只依次调用 @p emit。文本形态是本函数与用例的共同契约
/// （每行 `key=value`，会话行 `session tab=.. pane=.. kind=.. alive=..`）。
/// @param emit 接收器；为空时什么都不发。
/// @param context 原样传给 @p emit（POSIX 腿放 fd、Win32 腿放 HANDLE）。
/// @param facts 快照。
/// @param trigger_name 触发名（`SIGSEGV` / `EXCEPTION_ACCESS_VIOLATION` 等常量串）。
/// @param trigger_code 平台原始码（信号号 / 异常码）。
/// @param crash_epoch 崩溃时刻的 epoch 秒。
void emit_crash_report(CrashEmitter emit, void *context, const CrashFactsSnapshot &facts,
                       const char *trigger_name, std::uint64_t trigger_code,
                       std::uint64_t crash_epoch) noexcept;

}  // namespace borealis::diag
