/// 测试类型: unit
/// 目标单元: src/ui/serial_format.{h,cpp}（无 au:: 类型）纯格式化件
/// 测试说明: 守住 CONN.05 向导与热改入口的「最后一步」纯函数——三张参数名表→词条 key 的
///           逐档对应（与 `conn::kSerial*Names` 同序，名表加档而 key 表漏档即在此转红）、
///           表外名的回落档、可达性→归因 key（可达项回空串）、设备行的友好名与归因拼接。
///           真派发与置灰绘制属 itest_connection_wizard。

#include "ui/serial_format.h"

#include <string>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_serial_format {

namespace {

[[nodiscard]] auto port(std::string path, std::string friendly, conn::SerialPortAccess access)
    -> conn::SerialPortInfo {
    auto info = conn::SerialPortInfo{};
    info.path = std::move(path);
    info.friendly = std::move(friendly);
    info.access = access;
    return info;
}

}  // namespace

AURORA_TEST_CASE(parity_flow_and_line_ending_names_land_their_label_keys) {
    // 逐档：key 表与 `conn` 侧名表同序同长，这里是那张表与本件之间唯一的平行锁。
    AURORA_TEST_CHECK_TRUE(ui::serial_parity_key(conn::kSerialParityNames[0]) ==
                           "connections.serial.parity.none");
    AURORA_TEST_CHECK_TRUE(ui::serial_parity_key(conn::kSerialParityNames[1]) ==
                           "connections.serial.parity.even");
    AURORA_TEST_CHECK_TRUE(ui::serial_parity_key(conn::kSerialParityNames[2]) ==
                           "connections.serial.parity.odd");
    AURORA_TEST_CHECK_TRUE(ui::serial_flow_key(conn::kSerialFlowNames[0]) ==
                           "connections.serial.flow.none");
    AURORA_TEST_CHECK_TRUE(ui::serial_flow_key(conn::kSerialFlowNames[1]) ==
                           "connections.serial.flow.hardware");
    AURORA_TEST_CHECK_TRUE(ui::serial_flow_key(conn::kSerialFlowNames[2]) ==
                           "connections.serial.flow.software");
    AURORA_TEST_CHECK_TRUE(ui::serial_line_ending_key(conn::kSerialLineEndingNames[0]) ==
                           "connections.serial.line_ending.lf");
    AURORA_TEST_CHECK_TRUE(ui::serial_line_ending_key(conn::kSerialLineEndingNames[1]) ==
                           "connections.serial.line_ending.cr");
    AURORA_TEST_CHECK_TRUE(ui::serial_line_ending_key(conn::kSerialLineEndingNames[2]) ==
                           "connections.serial.line_ending.crlf");

    // 表外名回落该域缺省档：装载侧按 Choice 域把守，走到这里的表外名只可能来自绕过装载侧的构造。
    AURORA_TEST_CHECK_TRUE(ui::serial_parity_key("space") == "connections.serial.parity.none");
    AURORA_TEST_CHECK_TRUE(ui::serial_flow_key("xonxoff") == "connections.serial.flow.none");
    // 大小写敏感的既有口径：名表里是 "CRLF"，小写不算命中，故落回缺省档 "LF"。
    AURORA_TEST_CHECK_TRUE(ui::serial_line_ending_key("crlf") == "connections.serial.line_ending.lf");
    AURORA_TEST_CHECK_TRUE(ui::serial_line_ending_key("CRLF") ==
                           "connections.serial.line_ending.crlf");
    AURORA_TEST_CHECK_TRUE(ui::serial_line_ending_key({}) == "connections.serial.line_ending.lf");
}

AURORA_TEST_CASE(unreachable_devices_are_the_only_rows_with_an_attribution) {
    using conn::SerialPortAccess;

    // 可达项回空串：调用方据此不拼尾巴（稿 §2 标号 3 判的是「不可达项带归因」，不是「每行带状态」）。
    AURORA_TEST_CHECK_TRUE(ui::serial_access_key(SerialPortAccess::Openable).empty());
    AURORA_TEST_CHECK_TRUE(ui::serial_access_key(SerialPortAccess::Denied) ==
                           "connections.serial.access.denied");
    AURORA_TEST_CHECK_TRUE(ui::serial_access_key(SerialPortAccess::Busy) ==
                           "connections.serial.access.busy");
    AURORA_TEST_CHECK_TRUE(ui::serial_access_key(SerialPortAccess::Unavailable) ==
                           "connections.serial.access.unavailable");
}

AURORA_TEST_CASE(device_line_keeps_the_path_alone_without_friendly_name_or_attribution) {
    // F3：本沙箱无 USB 口，友好名恒空——空段不得留下一截悬空分隔符。
    AURORA_TEST_CHECK_EQ(
        ui::serial_device_line(port("/dev/ttyS0", {}, conn::SerialPortAccess::Denied), {}),
        "/dev/ttyS0");
    // 归因由调用方现取词条后传入（本件不碰词条表，才保得住「无 au:: 类型」的无头形态）。
    AURORA_TEST_CHECK_EQ(ui::serial_device_line(
                             port("/dev/ttyUSB0", "USB Serial", conn::SerialPortAccess::Busy),
                             "busy"),
                         "/dev/ttyUSB0 · USB Serial · busy");
    AURORA_TEST_CHECK_EQ(
        ui::serial_device_line(port("/dev/ttyS4", {}, conn::SerialPortAccess::Denied), "denied"),
        "/dev/ttyS4 · denied");
}

}  // namespace borealis::test_cases::utest_serial_format
