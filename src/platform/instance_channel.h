#pragma once

// ============================================================
// 单实例通道接口（src/platform/instance_channel.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.04` 的进程间通道（裁决 7.7）。共享层只见本接口：
// POSIX 腿是 Unix domain socket + `flock` 认领，Win32 腿是命名管道
// （选型与理由见 `codespec/CLI_STARTUP.draft.md` §3 与 `ARCHITECTURE.md` §3.1）。
//
// 分工：
//   * `instance_listen` 负责认领首实例并起一条 transport 线程；线程只做
//     accept / 读帧 / 等 handler 回执 / 写帧，不触达 UI（§3.2）。
//   * handler 由装配层提供：把请求塞进本仓队列并唤醒主线程；主线程执行完
//     调 `InstanceRequest::complete`，transport 线程随即写回复。
//   * `instance_forward` 是后来进程的那一侧：发一帧、等一帧回复。
//
// 帧格式（本层）：`u32 little-endian 长度` + 载荷。载荷语义归
// `borealis::cli`（请求 / 回复编解码），平台层不解析。
// ============================================================

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace borealis::platform {

/// @brief 一帧载荷的上限（不含 4 字节外层长度）。
inline constexpr std::size_t kMaxInstanceFrameBytes = 16 * 1024;

/// @brief transport 线程等待主线程回执的上限。
inline constexpr int kInstanceReplyTimeoutMs = 2000;

/// @brief 一个在途转交请求：transport 线程等它，主线程完成它。
class InstanceRequest {
  public:
    explicit InstanceRequest(std::string frame);

    InstanceRequest(const InstanceRequest &) = delete;
    auto operator=(const InstanceRequest &) -> InstanceRequest & = delete;
    InstanceRequest(InstanceRequest &&) = delete;
    auto operator=(InstanceRequest &&) -> InstanceRequest & = delete;

    /// @brief 请求载荷（构造后不变；transport 线程与主线程都只读）。
    [[nodiscard]] auto frame() const -> const std::string & { return frame_; }

    /// @brief 主线程回执；重复调用只保留第一次。
    auto complete(std::string reply) -> void;

    /// @brief transport 线程等待回执；超时回空。
    [[nodiscard]] auto wait(int timeout_ms) -> std::optional<std::string>;

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string frame_;
    std::string reply_;
    bool done_{false};
};

/// @brief 新请求到达时的回调；在 transport 线程上执行，禁止触达 UI。
using InstanceHandler = std::function<void(std::shared_ptr<InstanceRequest>)>;

/// @brief 监听端句柄：析构即停止线程、关闭监听并释放首实例认领。
class InstanceListener {
  public:
    InstanceListener() = default;
    virtual ~InstanceListener() = default;

    InstanceListener(const InstanceListener &) = delete;
    auto operator=(const InstanceListener &) -> InstanceListener & = delete;
    InstanceListener(InstanceListener &&) = delete;
    auto operator=(InstanceListener &&) -> InstanceListener & = delete;
};

/// @brief 认领结果分档。
enum class InstanceRole : std::uint8_t {
    Primary,   ///< 本进程成为首实例，listener 有效。
    Existing,  ///< 已有实例在跑；listener 为空，调用方应转交。
    Failed,    ///< 通道不可用；error 说明原因。
};

/// @brief 认领结果。
struct InstanceListenResult {
    InstanceRole role{InstanceRole::Failed};
    std::unique_ptr<InstanceListener> listener;
    std::string error;  ///< ASCII 诊断；仅 Failed / Existing 可能非空。
};

/// @brief 当前用户的默认通道键。
///
/// POSIX 是 socket 文件路径（`$XDG_RUNTIME_DIR` 或 `/tmp` 下的私有目录）；
/// Win32 是命名管道名后缀。测试可传自己的键，生产只用本函数。
[[nodiscard]] auto instance_default_key() -> std::string;

/// @brief 尝试认领首实例并开始监听。
/// @param key 通道键；空串即 Failed。
/// @param handler 新请求回调；在 transport 线程上执行。空回调即每条请求都回空。
[[nodiscard]] auto instance_listen(const std::string &key, InstanceHandler handler)
    -> InstanceListenResult;

/// @brief 一次转交的结果。
struct InstanceForwardResult {
    bool delivered{false};  ///< 是否收到完整回复帧。
    std::string reply;      ///< 回复载荷（delivered 时有效，语义归 `cli`）。
    std::string error;      ///< ASCII 诊断（未送达时有效）。
};

/// @brief 向已有实例转交一帧并等回复。
/// @param timeout_ms 连接 / 读写的总预算（实现按平台能力近似）。
[[nodiscard]] auto instance_forward(const std::string &key, std::string_view frame, int timeout_ms)
    -> InstanceForwardResult;

}  // namespace borealis::platform
