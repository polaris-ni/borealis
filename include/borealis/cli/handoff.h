#pragma once

// ============================================================
// 单实例转交帧（include/borealis/cli/handoff.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.04` 的请求 / 回复载荷编解码。平台通道只见不透明字节
// （`platform/instance_channel.h` 的外层长度帧），协议语义留在本纯逻辑件：
//
// 请求： "BORI" | version u8 | action u8 | flags u8 | reserved u8
//        | profile_len u16le | cwd_len u16le | profile | cwd
// 回复： "BORR" | version u8 | status u8 | message_len u16le | message
//
// 长度先于越界检查；magic / version / reserved / 尾随字节 / 超限任一不符即整帧
// 拒绝（fail-closed）。载荷只有动作、档案名、绝对目录与窗口标志，不含凭据。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "borealis/cli/startup.h"

namespace borealis::cli {

/// @brief 请求载荷上限（不含平台通道的 4 字节外层长度）。
inline constexpr std::size_t kMaxHandoffRequestBytes = 16 * 1024;
/// @brief 回复载荷上限（不含平台通道的 4 字节外层长度）。
inline constexpr std::size_t kMaxHandoffReplyBytes = 4 * 1024;

/// @brief 编码请求；字段超限 / 含 NUL / 总长越界时回空。
[[nodiscard]] auto encode_startup_request(const StartupRequest &request)
    -> std::optional<std::vector<std::uint8_t>>;

/// @brief 解码请求；任何结构不符回空（不返回部分结果）。
[[nodiscard]] auto decode_startup_request(std::span<const std::uint8_t> bytes)
    -> std::optional<StartupRequest>;

/// @brief 首实例对一个请求的回复。
struct StartupReply {
    bool ok{false};
    std::string message;  ///< ASCII 诊断；成功时为空。

    [[nodiscard]] auto operator==(const StartupReply &) const noexcept -> bool = default;
};

/// @brief 编码回复；message 超限 / 含 NUL 时回空。
[[nodiscard]] auto encode_startup_reply(const StartupReply &reply)
    -> std::optional<std::vector<std::uint8_t>>;

/// @brief 解码回复；任何结构不符回空。
[[nodiscard]] auto decode_startup_reply(std::span<const std::uint8_t> bytes)
    -> std::optional<StartupReply>;

}  // namespace borealis::cli
