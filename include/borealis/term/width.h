#pragma once

// ============================================================
// 宽度判定接缝（include/borealis/term/width.h）
// ------------------------------------------------------------
// 架构 §6.3：一个码点占几格由**注入的**判定接口给出，终端状态机与网格层都不查表
// （PLAN.md §2「应用侧不得自行查表替代」）。真正的 East Asian Width 表属框架原语 G1，
// M0 未开工前生产侧只能挂 `SingleWidthPolicy`，故 CJK 双宽占位当前不生效；
// G1 落地后替换实现即可，两侧代码不动。
// ============================================================

#include <cstdint>

namespace borealis::term {

/// @brief East Asian Width 中 Ambiguous 类的口径（裁决 7.15）。
enum class AmbiguousWidth : std::uint8_t {
    Narrow,  ///< 默认：Ambiguous 类按单宽呈现（UTF-8 本机/SSH 场景）。
    Wide,    ///< profile 覆盖：按双宽呈现（GB18030/GBK 串口一类）。
};

/// @brief 码点 → 占位格数的判定接口。
class WidthPolicy {
  public:
    virtual ~WidthPolicy() = default;

    /// @brief 判定码点占几格。
    /// @param code_point 待判定码点。
    /// @param ambiguous Ambiguous 类在本会话采用的口径。
    /// @return 占位格数；调用方只消费 1 与 2。
    [[nodiscard]] virtual auto width_of(char32_t code_point, AmbiguousWidth ambiguous) const noexcept
        -> std::uint8_t = 0;
};

/// @brief 一律单宽的缺省实现：G1 未落地期间生产路径的唯一可用取值。
///
/// 它不是「应用侧的宽度表」——没有表可查，只返回常数；双宽判定随框架原语到货才存在。
class SingleWidthPolicy final : public WidthPolicy {
  public:
    /// @brief 恒返回单宽。
    /// @param code_point 忽略。
    /// @param ambiguous 忽略。
    /// @return 恒为 1。
    [[nodiscard]] auto width_of(char32_t code_point, AmbiguousWidth ambiguous) const noexcept
        -> std::uint8_t override {
        static_cast<void>(code_point);
        static_cast<void>(ambiguous);
        return 1U;
    }
};

}  // namespace borealis::term
