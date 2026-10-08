// ============================================================
// schema 的派生默认值与同域纯逻辑件（src/config/settings.cpp）
// ------------------------------------------------------------
// schema 的标量默认值一律写在类内初始值里（`include/borealis/config/settings.h`），
// 唯独调色板要由主题表派生——色值有第二处定义就会与主题表漂移，故这里只转调。
// 同域的纯逻辑件（如最近连接的定长登记）也放这里：它们只依赖 schema 类型、可无头单测。
// ============================================================

#include "borealis/config/settings.h"

#include <algorithm>

#include "borealis/config/themes.h"

namespace borealis::config {

AppearanceSettings::AppearanceSettings() {
    palette = theme_palette(theme);
    // 阈值取 WCAG AA 的正文档：最小对比度是开关（缺省关闭），开它的人要的正是「达标」，
    // 留 1.0 等于开了个不起作用的开关。
    palette.min_contrast = 4.5;
}

auto push_recent_connection(ConnectionSettings &connection, std::string_view profile_id, std::int64_t used_at)
    -> void {
    if (profile_id.empty()) {
        return;
    }
    auto &items = connection.recent;
    // 同一档案再次连接时先摘掉旧条目再插到表头：列表的语义是「最近用过的顺序」而非「首次用过的顺序」，
    // 留着旧条目会出现同一档案占两行。
    const auto existing = std::ranges::find(items, profile_id, &RecentConnection::profile_id);
    if (existing != items.end()) {
        items.erase(existing);
    }
    items.insert(items.begin(), RecentConnection{std::string{profile_id}, used_at});
    if (items.size() > kRecentConnectionLimit) {
        items.resize(kRecentConnectionLimit);
    }
}

}  // namespace borealis::config
