// ============================================================
// 字符集映射实现（src/term/charset.cpp）
// ------------------------------------------------------------
// DEC Special Graphics 表按 VT100 定义：只有 0x5F–0x7E 这段被重定义，其余码点原样
// 通过——把框线映射做成整表替换会让字符集里的字母也跟着变形，那是错的。
//
// 映射目标一律写 `\u` 转义：源码保持 ASCII（AGENTS.md §4.3 第 14 条），
// 且避免编辑器/代码页差异把框线字符写坏。
// ============================================================

#include "borealis/term/charset.h"

namespace borealis::term {

auto map_charset(Charset charset, char32_t cp) noexcept -> char32_t {
    if (charset != Charset::DecSpecialGraphics) {
        return cp;
    }
    if (cp < U'\x5F' || cp > U'\x7E') {
        return cp;
    }
    switch (cp) {
        case U'\x5F':
            return U'\x00A0'; // NBSP：DEC 表里 `_` 是空白
        case U'\x60':
            return U'\x25C6'; // ◆
        case U'\x61':
            return U'\x2592'; // ▒
        case U'\x62':
            return U'\x2409'; // ␉
        case U'\x63':
            return U'\x240C'; // ␌
        case U'\x64':
            return U'\x240D'; // ␍
        case U'\x65':
            return U'\x240A'; // ␊
        case U'\x66':
            return U'\x00B0'; // °
        case U'\x67':
            return U'\x00B1'; // ±
        case U'\x68':
            return U'\x2424'; // ␤
        case U'\x69':
            return U'\x240B'; // ␋
        case U'\x6A':
            return U'\x2518'; // ┘
        case U'\x6B':
            return U'\x2510'; // ┐
        case U'\x6C':
            return U'\x250C'; // ┌
        case U'\x6D':
            return U'\x2514'; // └
        case U'\x6E':
            return U'\x253C'; // ┼
        case U'\x6F':
            return U'\x23BA'; // ⎺
        case U'\x70':
            return U'\x23BB'; // ⎻
        case U'\x71':
            return U'\x2500'; // ─
        case U'\x72':
            return U'\x23BC'; // ⎼
        case U'\x73':
            return U'\x23BD'; // ⎽
        case U'\x74':
            return U'\x251C'; // ├
        case U'\x75':
            return U'\x2524'; // ┤
        case U'\x76':
            return U'\x2534'; // ┴
        case U'\x77':
            return U'\x252C'; // ┬
        case U'\x78':
            return U'\x2502'; // │
        case U'\x79':
            return U'\x2264'; // ≤
        case U'\x7A':
            return U'\x2265'; // ≥
        case U'\x7B':
            return U'\x03C0'; // π
        case U'\x7C':
            return U'\x2260'; // ≠
        case U'\x7D':
            return U'\x00A3'; // £
        case U'\x7E':
            return U'\x00B7'; // ·
        default:
            return cp;
    }
}

auto charset_from_designator(char32_t final_byte, Charset &charset) noexcept -> bool {
    switch (final_byte) {
        case U'0':
            charset = Charset::DecSpecialGraphics;
            return true;
        case U'B':
            charset = Charset::Ascii;
            return true;
        default:
            return false;
    }
}

}  // namespace borealis::term
