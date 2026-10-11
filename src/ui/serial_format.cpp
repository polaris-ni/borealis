// ============================================================
// 串口向导与热改入口的纯格式化函数实现（src/ui/serial_format.cpp）
// ------------------------------------------------------------
// 三张名表与一张可达性表在这里落成词条 key；向导与装配层只调用本件，不 switch 措辞
// （tunnel_format 同族分工）。名表逐字取自 `conn::kSerial*Names`（档位与取值的唯一一份表，
// 裁决 7.110 D4①），本件只给它配措辞 key，不再列第二张名表。
// ============================================================

#include "serial_format.h"

#include <cstddef>

namespace borealis::ui {
namespace {

// 三张 key 表与 `conn` 侧三张名表**同序同长**，索引即对应关系；`utest_serial_format` 逐档锁这条
// 平行（名表加一档而这里漏一档，用例即在表外回落档上转红）。
constexpr std::string_view kParityKeys[]{
    "connections.serial.parity.none", "connections.serial.parity.even",
    "connections.serial.parity.odd"};
constexpr std::string_view kFlowKeys[]{
    "connections.serial.flow.none", "connections.serial.flow.hardware",
    "connections.serial.flow.software"};
constexpr std::string_view kLineEndingKeys[]{
    "connections.serial.line_ending.lf", "connections.serial.line_ending.cr",
    "connections.serial.line_ending.crlf"};

/// @brief 按 `conn` 侧名表的次序取 key；表外名回该表第 0 档（＝装载侧缺省，见头注）。
[[nodiscard]] auto keyed_by_names(const std::string_view *names, std::size_t count,
                                 const std::string_view *keys, std::string_view name)
    -> std::string_view {
    for (std::size_t i = 0; i < count; ++i) {
        if (names[i] == name) {
            return keys[i];
        }
    }
    return keys[0];
}

}  // namespace

auto serial_parity_key(std::string_view parity) -> std::string_view {
    return keyed_by_names(conn::kSerialParityNames.data(), conn::kSerialParityNames.size(),
                          kParityKeys, parity);
}

auto serial_flow_key(std::string_view flow) -> std::string_view {
    return keyed_by_names(conn::kSerialFlowNames.data(), conn::kSerialFlowNames.size(), kFlowKeys,
                          flow);
}

auto serial_line_ending_key(std::string_view ending) -> std::string_view {
    return keyed_by_names(conn::kSerialLineEndingNames.data(),
                          conn::kSerialLineEndingNames.size(), kLineEndingKeys, ending);
}

auto serial_access_key(conn::SerialPortAccess access) -> std::string_view {
    switch (access) {
    case conn::SerialPortAccess::Openable:
        return {};  // 可达项不带归因尾巴（头注）。
    case conn::SerialPortAccess::Denied:
        return "connections.serial.access.denied";
    case conn::SerialPortAccess::Busy:
        return "connections.serial.access.busy";
    case conn::SerialPortAccess::Unavailable:
        break;
    }
    return "connections.serial.access.unavailable";
}

auto serial_device_line(const conn::SerialPortInfo &port,
                        std::string_view attribution) -> std::string {
    std::string line{port.path};
    // 分隔符只在对应段非空时才出现：友好名在本沙箱恒空（F3），归因对可达项恒空
    //（`serial_access_key` 的 Openable 档），留下分隔符就是行首行尾各挂一个孤点。
    // 那枚「·」是行折叠的既定视觉形态（`tunnel_endpoint_line` 的方向箭头同口径）。
    if (!port.friendly.empty()) {
        line += " · ";  // CJK-LITERAL: 上屏文案 - 分隔符是行折叠的既定视觉形态
        line += port.friendly;
    }
    if (!attribution.empty()) {
        line += " · ";  // CJK-LITERAL: 上屏文案 - 同上
        line.append(attribution);
    }
    return line;
}

}  // namespace borealis::ui
