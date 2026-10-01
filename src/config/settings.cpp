// ============================================================
// 外观域的默认值合成（src/config/settings.cpp）
// ------------------------------------------------------------
// schema 的标量默认值一律写在类内初始值里（`include/borealis/config/settings.h`），
// 唯独调色板要由主题表派生——色值有第二处定义就会与主题表漂移，故这里只转调。
// ============================================================

#include "borealis/config/settings.h"

#include "borealis/config/themes.h"

namespace borealis::config {

AppearanceSettings::AppearanceSettings() {
    palette = theme_palette(theme);
    // 阈值取 WCAG AA 的正文档：最小对比度是开关（缺省关闭），开它的人要的正是「达标」，
    // 留 1.0 等于开了个不起作用的开关。
    palette.min_contrast = 4.5;
}

}  // namespace borealis::config
