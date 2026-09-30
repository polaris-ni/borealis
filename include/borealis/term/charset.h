#pragma once

// ============================================================
// 字符集指派与映射（include/borealis/term/charset.h）
// ------------------------------------------------------------
// 架构 §5.3：字符集切换须生效——DEC Special Graphics 线条字符集（`ESC ( 0`）与 ASCII
// （`ESC ( B`）。这条直接顶着 SPEC.FEAT.TERM.01 的验收线：vim/htop/tmux/less 的边框
// 线条若没有这层映射，就会显示成 l/q/k/x 一类的字母。
//
// 本模块只做「码点 → 呈现码点」的查表，不持有字符集槽位、不解释 GL/GR 调用——
// 槽位与切换归终端状态机。
// ============================================================

#include <cstdint>

namespace borealis::term {

/// @brief 已被支持的字符集（DEC 指派序列的终结符决定）。
enum class Charset : std::uint8_t {
    Ascii,               ///< `ESC ( B`：恒等映射
    DecSpecialGraphics,  ///< `ESC ( 0`：VT100 线条字符集
};

/// @brief 把当前字符集下的码点映射为呈现用码点。
/// @param charset 当前生效的字符集。
/// @param cp 待映射码点。
/// @return 呈现用码点；不在该字符集映射范围内的原样返回。
[[nodiscard]] auto map_charset(Charset charset, char32_t cp) noexcept -> char32_t;

/// @brief 该终结符是否指派了本模块已支持的字符集。
/// @param final_byte ESC 序列的终结符（如 `(` 之后的 `0` / `B`）。
/// @param charset 输出：对应的字符集。
/// @return 是否已支持；false 表示该字符集尚未实现（按 ASCII 处理即可，不中断）。
[[nodiscard]] auto charset_from_designator(char32_t final_byte, Charset &charset) noexcept -> bool;

}  // namespace borealis::term
