// ============================================================
// 字体族选择的判定件（src/ui/font_choice.cpp）
// ------------------------------------------------------------
// 只做一次目录查表：命中且等宽才采用，其余一律回落内置族。不含框架类型，故判定能全量单测
// （AGENTS.md §4.4 第 20 条）；目录的取用（框架枚举有同步扫盘的首次成本）留在装配层做一次。
// ============================================================

#include "borealis/ui/font_choice.h"

#include <algorithm>
#include <string_view>

namespace borealis::ui {

auto choose_font_family(std::string_view configured, std::span<const FontFamilyEntry> catalog) -> FontFamilyChoice {
    const auto fallback = [&](FontFamilyVerdict verdict) -> FontFamilyChoice {
        return FontFamilyChoice{.family = std::string{kDefaultMonospaceFamily}, .verdict = verdict};
    };
    const auto found = std::ranges::find(catalog, configured, &FontFamilyEntry::family);
    if (found == catalog.end()) {
        return fallback(FontFamilyVerdict::Unlisted);  // 空串与拼写/大小写不符都归这一档
    }
    // 等宽性是框架的**度量判据**，不是族名里有没有 "Mono"：非等宽族进网格会让列位与光标脱钩
    // （`SPEC.FEAT.RENDER.02` 的验收线），故宁可回落也不采用。
    return found->monospace ? FontFamilyChoice{.family = found->family, .verdict = FontFamilyVerdict::Configured}
                            : fallback(FontFamilyVerdict::NotMonospace);
}

}  // namespace borealis::ui
