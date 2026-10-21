// ============================================================
// 连接类型 → 图标的唯一字形表实现（src/ui/connection_glyph.cpp）
// ------------------------------------------------------------
// 四档各一枚 ASCII 单字，表就在这一处（头注写了这张表是判据）。末档用 `break` + switch 之后
// 再 return 的形态（`serial_access_key` 同族先例）：`conn::ConnectionType` 将来加一档时这里得到
// 编译器提醒，而不是悄悄落进前一档的图标。
// ============================================================

#include "connection_glyph.h"

namespace borealis::ui {

auto connection_type_glyph(conn::ConnectionType type) -> std::string_view {
    switch (type) {
    case conn::ConnectionType::Local:
        return ">";
    case conn::ConnectionType::Ssh:
        return "#";
    case conn::ConnectionType::Serial:
        return "S";
    case conn::ConnectionType::Telnet:
        break;
    }
    return "T";
}

}  // namespace borealis::ui
