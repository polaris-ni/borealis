#pragma once

// ============================================================
// 网格 cell（include/borealis/grid/cell.h）
// ------------------------------------------------------------
// 架构 §4.1：主结构只放高频字段，低频属性（零宽字符、超链接一类）外置到侧表，
// 避免主结构膨胀拖累整屏内存与遍历——10k 行 scrollback 的常驻预算（§4.7）全靠
// 这个结构的大小撑住。
// ============================================================

#include <cstdint>

namespace borealis::grid {

/// @brief 颜色值的「未指定」标记：由主题提供默认前景/背景，网格不存实际色值。
inline constexpr std::uint32_t kColorDefault = 0xFFFFFFFFU;

/// @brief cell 标志位（高频属性，位掩码）。
enum : std::uint16_t {
    kFlagNone = 0U,
    kFlagBold = 1U << 0,
    kFlagDim = 1U << 1,
    kFlagItalic = 1U << 2,
    kFlagUnderline = 1U << 3,
    kFlagBlink = 1U << 4,
    kFlagReverse = 1U << 5,
    kFlagHidden = 1U << 6,
    kFlagStrike = 1U << 7,
    /// 双宽字符的延续格：本身不是独立字符，光标与复制都须跳过它。
    kFlagWideContinuation = 1U << 8,
};

/// @brief 标志位掩码类型。
using CellFlags = std::uint16_t;

/// @brief 一个网格单元。
///
/// `width` 由写入方填入：双宽字符占两格时，首格写 2、延续格写 0 并置
/// `kFlagWideContinuation`。存储层不解释宽度语义——宽度判定归 `borealis::term`（§6.3）。
struct Cell {
    char32_t code_point = U' ';
    std::uint32_t foreground = kColorDefault;
    std::uint32_t background = kColorDefault;
    CellFlags flags = kFlagNone;
    std::uint8_t width = 1;

    /// @brief 该格是否为双宽字符的延续格（不承载字符、不被光标停留）。
    [[nodiscard]] auto is_wide_continuation() const noexcept -> bool {
        return (flags & kFlagWideContinuation) != 0U;
    }

    /// @brief 恢复为默认空格（行 reset 与滚动复用行时调用）。
    auto reset() noexcept -> void {
        code_point = U' ';
        foreground = kColorDefault;
        background = kColorDefault;
        flags = kFlagNone;
        width = 1;
    }
};

}  // namespace borealis::grid
