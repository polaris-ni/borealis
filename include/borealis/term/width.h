#pragma once

// ============================================================
// 宽度判定接缝（include/borealis/term/width.h）
// ------------------------------------------------------------
// 架构 §6.3：一个码点占几格由**注入的**判定接口给出，终端状态机与网格层都不查表
// （AGENTS.md §4.4 第 20 条要求纯逻辑层可独立单测，判定表属外部数据）。
// 表本身在框架侧：Aurora `aurora/core/unicode_width.h` 的 `unicode_cell_width`（裁决 7.20），
// 生产路径挂 `UnicodeWidthPolicy`；`SingleWidthPolicy` 是测试用的常数注入值，
// 让不关心宽度的用例不受 Unicode 版本影响。
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
    /// @return 占位格数：0（不独立占格，须并入前一个基础格）、1 或 2。
    [[nodiscard]] virtual auto width_of(char32_t code_point, AmbiguousWidth ambiguous) const noexcept
        -> std::uint8_t = 0;
};

/// @brief 一律单宽的注入值：用于不关心宽度语义的用例。
///
/// 它不是「应用侧的宽度表」——没有表可查，只返回常数。
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

/// @brief 按 Unicode East Asian Width 与 General_Category 判定的生产实现。
///
/// 判定次序（先零宽、再宽度）与数据版本都归框架侧，本类只做口径转发，
/// 声明在此、实现在 `src/term/width.cpp`——Aurora 头不进本仓公共头。
class UnicodeWidthPolicy final : public WidthPolicy {
  public:
    /// @brief 转调框架判定原语。
    /// @param code_point 待判定码点。
    /// @param ambiguous Ambiguous 类在本会话采用的口径。
    /// @return 占位格数 0 / 1 / 2。
    [[nodiscard]] auto width_of(char32_t code_point, AmbiguousWidth ambiguous) const noexcept
        -> std::uint8_t override;
};

}  // namespace borealis::term
