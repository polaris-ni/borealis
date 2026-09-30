// ============================================================
// 宽度判定接缝的生产实现（src/term/width.cpp）
// ------------------------------------------------------------
// 判定表与「先零宽、后宽度」的次序都在框架侧（Aurora `aurora/core/unicode_width.h`），
// 本文件只做口径转发：Aurora 头不进 `include/borealis/`，故实现落在这里。
// ============================================================

#include "aurora/core/unicode_width.h"

#include "borealis/term/width.h"

namespace borealis::term {

auto UnicodeWidthPolicy::width_of(char32_t code_point, AmbiguousWidth ambiguous) const noexcept
    -> std::uint8_t {
    const auto mode =
        ambiguous == AmbiguousWidth::Wide ? aurora::AmbiguousWidthMode::Wide : aurora::AmbiguousWidthMode::Narrow;
    return aurora::unicode_cell_width(code_point, mode);
}

}  // namespace borealis::term
