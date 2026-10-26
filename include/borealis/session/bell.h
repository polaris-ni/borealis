#pragma once

// ============================================================
// session/bell.h — BEL 呈现策略（纯逻辑）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.04`：可听铃声须应用侧显式开框架音频通道（`AURORA_ENABLE_AUDIO`），缺省档
// 仍是视觉。本件只决定「这次 BEL 该点角标还是再响一声」，不碰音频设备；节流也在这里——
// 策略与设备分开，节流才能脱设备单测（AGENTS.md §4.4 第 20 条）。
// ============================================================

#include <chrono>
#include <cstdint>
#include <optional>

namespace borealis::config {

enum class BellMode : std::uint8_t;

}  // namespace borealis::config

namespace borealis::session {

/// @brief 全进程两次可听铃声的最小间隔（`SPEC.FEAT.WS.04`，裁决 7.159）。
///
/// 一条 `printf '\a'` 循环可以把 BEL 刷成每秒上千次；铃声本体长 120 ms，间隔取 200 ms
/// 既保证两响不重叠，又把最坏情况压到 5 声/秒。节流是**全局**的：多个后台标签同时响铃
/// 只出一声，角标仍逐个点亮（哪一格响过不能丢）。
inline constexpr std::chrono::milliseconds kBellToneInterval{200};

/// @brief 一次 BEL 的处置（`SPEC.FEAT.WS.04`）。
enum class BellAction : std::uint8_t {
    None,          ///< 忽略（`BellMode::Off`）。
    Badge,         ///< 只点标签角标（视觉档，或可听档被节流）。
    BadgeAndTone,  ///< 角标 + 可听铃声（可听档且已过最小间隔）。
};

/// @brief BEL 呈现策略：把配置档位与时刻折成一次处置（`SPEC.FEAT.WS.04`，裁决 7.159）。
///
/// `BellMode` 的三档语义由 `config/settings.h` 定死：`Audible` 是「视觉提示之外再发铃声」，
/// 不是「只发声」；故只有 `Off` 会返回 `None`。节流只作用于铃声，不影响角标。
///
/// @note Thread: 主线程（帧边界逐会话调用）
/// @note Side-effects: none
/// @note Rebuildable: no
class BellPolicy {
  public:
    /// @brief 构造。
    /// @param min_tone_interval 两次铃声的最小间隔；非正值表示每次都响。
    explicit BellPolicy(std::chrono::milliseconds min_tone_interval = kBellToneInterval) noexcept
        : min_tone_interval_(min_tone_interval) {}

    /// @brief 判定一次 BEL 的处置；可听档在判定时推进节流锚点。
    /// @param mode 当前配置档位（逐次现读，故改档即时生效）。
    /// @param now 当前单调时刻。
    /// @return 本次该点角标、再响铃，或忽略。
    [[nodiscard]] auto decide(config::BellMode mode, std::chrono::steady_clock::time_point now) -> BellAction;

  private:
    std::chrono::milliseconds min_tone_interval_;
    std::optional<std::chrono::steady_clock::time_point> last_tone_;
};

}  // namespace borealis::session
