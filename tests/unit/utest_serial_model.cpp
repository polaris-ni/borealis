/// 测试类型: unit
/// 目标单元: src/conn/serial_model.{h,cpp}
/// 测试说明: `SPEC.FEAT.CONN.05`（串口终端）纯逻辑层的无头单测（裁决 7.110 D13①）。
///           覆盖四件事：波特率档位表与区间校验（D4① 的「档位表只有一份」）、
///           名表 ↔ 枚举 ↔ 落地字节的双向映射（D11① 的写出侧取用口径）、
///           档案→生效参数的折算（平台腿的唯一折法来源）、设备枚举行的自然序与
///           可达性投影（D9①：置灰项照列、排序不按可达性换行位）。
///
///           帧格式在替身设备上不可判（F5），故本件只证「折成哪一档」，不证「设备侧
///           跑成什么」；后者属 `itest_serial_connection`（批 2）与真机走查。

#include <string>
#include <vector>

#include "borealis/term/paste.h"
#include "conn/serial_model.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_serial_model {

namespace {

using borealis::conn::SerialFlow;
using borealis::conn::SerialParity;
using borealis::conn::SerialPortAccess;
using borealis::conn::SerialPortInfo;
using borealis::conn::SerialProfile;
using borealis::conn::SerialProfileIssue;
using borealis::term::LineEnding;

/// @brief 一条合法的串口档案初值（其余用例在此之上逐项破坏）。
[[nodiscard]] auto valid_profile() -> SerialProfile {
    SerialProfile profile;
    profile.device_path = "/dev/ttyUSB0";
    profile.baud = 115200;
    profile.data_bits = 8;
    profile.stop_bits = 1;
    profile.parity = "none";
    profile.flow = "none";
    profile.line_ending = "LF";
    profile.encoding = "GB18030";
    return profile;
}

/// @brief 取issue 的整数值，便于跨枚举档位比较而不引入运算符重载。
[[nodiscard]] auto code(SerialProfileIssue issue) -> int {
    return static_cast<int>(issue);
}

}  // namespace

AURORA_TEST_CASE(baud_table_is_sorted_and_locks_the_fourteen_rates) {
    const auto &table = conn::kSerialBaudRates;
    AURORA_TEST_REQUIRE_EQ(table.size(), 14U);
    for (std::size_t index = 1; index < table.size(); ++index) {
        // 下拉按表序画，故表本身必须严格升序。
        AURORA_TEST_CHECK_MSG(table[index] > table[index - 1], "baud table is not ascending");
    }
    AURORA_TEST_CHECK(conn::is_supported_baud(9600));
    AURORA_TEST_CHECK(conn::is_supported_baud(115200));
    AURORA_TEST_CHECK(conn::is_supported_baud(2000000));
    // 表外的速率不是「非法」，只是「不在可选项里」（4800 见 validate 用例）。
    AURORA_TEST_CHECK_FALSE(conn::is_supported_baud(4800));
    AURORA_TEST_CHECK_FALSE(conn::is_supported_baud(0));
}

AURORA_TEST_CASE(name_tables_round_trip_both_directions) {
    for (const auto name : conn::kSerialParityNames) {
        const auto parity = conn::serial_parity(name);
        AURORA_TEST_REQUIRE(parity.has_value());
        AURORA_TEST_CHECK_MSG(conn::serial_parity_name(*parity) == name, "parity name diverged");
    }
    for (const auto name : conn::kSerialFlowNames) {
        const auto flow = conn::serial_flow(name);
        AURORA_TEST_REQUIRE(flow.has_value());
        AURORA_TEST_CHECK_MSG(conn::serial_flow_name(*flow) == name, "flow name diverged");
    }
    for (const auto name : conn::kSerialLineEndingNames) {
        const auto ending = conn::serial_line_ending(name);
        AURORA_TEST_REQUIRE(ending.has_value());
        AURORA_TEST_CHECK_MSG(conn::line_ending_name(*ending) == name, "line ending name diverged");
    }
    // 表外名一律拒收（与设置域 `one_of` 同口径：大小写敏感，不做宽容）。
    AURORA_TEST_CHECK_FALSE(conn::serial_parity("mark").has_value());
    AURORA_TEST_CHECK_FALSE(conn::serial_flow("None").has_value());
    AURORA_TEST_CHECK_FALSE(conn::serial_line_ending("lf").has_value());
    AURORA_TEST_CHECK_FALSE(conn::serial_line_ending("").has_value());
}

AURORA_TEST_CASE(line_ending_bytes_are_the_three_documented_sequences) {
    AURORA_TEST_CHECK_EQ(conn::line_ending_bytes(LineEnding::Lf).size(), 1U);
    AURORA_TEST_CHECK(conn::line_ending_bytes(LineEnding::Lf) == "\n");
    AURORA_TEST_CHECK(conn::line_ending_bytes(LineEnding::Cr) == "\r");
    AURORA_TEST_CHECK(conn::line_ending_bytes(LineEnding::Crlf) == "\r\n");
    // 行尾名与 `term::LineEnding` 同域（裁决 7.33⑥）：三档全命中才没有第二真值源。
    AURORA_TEST_CHECK(conn::serial_line_ending("CRLF") == LineEnding::Crlf);
    AURORA_TEST_CHECK(conn::serial_line_ending("CR") == LineEnding::Cr);
    AURORA_TEST_CHECK(conn::serial_line_ending("LF") == LineEnding::Lf);
}

AURORA_TEST_CASE(validate_profile_walks_the_field_matrix) {
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(valid_profile())),
                         code(SerialProfileIssue::None));

    auto empty_device = valid_profile();
    empty_device.device_path.clear();
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(empty_device)),
                         code(SerialProfileIssue::DevicePathEmpty));

    // 速率只判区间：4800 表外但合法（判死只会让手改的档案连不上，换不来安全）。
    auto legacy_baud = valid_profile();
    legacy_baud.baud = 4800;
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(legacy_baud)), code(SerialProfileIssue::None));
    auto zero_baud = valid_profile();
    zero_baud.baud = 0;
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(zero_baud)),
                         code(SerialProfileIssue::BaudOutOfRange));
    auto huge_baud = valid_profile();
    huge_baud.baud = 4000001;
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(huge_baud)),
                         code(SerialProfileIssue::BaudOutOfRange));

    auto few_bits = valid_profile();
    few_bits.data_bits = 4;
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(few_bits)),
                         code(SerialProfileIssue::DataBitsOutOfRange));
    few_bits.data_bits = 5;
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(few_bits)), code(SerialProfileIssue::None));

    auto three_stops = valid_profile();
    three_stops.stop_bits = 3;
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(three_stops)),
                         code(SerialProfileIssue::StopBitsOutOfRange));

    auto bad_parity = valid_profile();
    bad_parity.parity = "space";
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(bad_parity)), code(SerialProfileIssue::UnknownParity));

    auto bad_flow = valid_profile();
    bad_flow.flow = "xonxoff";
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(bad_flow)), code(SerialProfileIssue::UnknownFlow));

    auto bad_ending = valid_profile();
    bad_ending.line_ending = "CRLF\r";
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(bad_ending)),
                         code(SerialProfileIssue::UnknownLineEnding));

    auto no_encoding = valid_profile();
    no_encoding.encoding.clear();
    AURORA_TEST_CHECK_EQ(code(conn::validate_serial_profile(no_encoding)),
                         code(SerialProfileIssue::EncodingEmpty));
}

AURORA_TEST_CASE(fold_profile_yields_platform_bits_or_refuses) {
    auto profile = valid_profile();
    profile.baud = 9600;
    profile.data_bits = 7;
    profile.stop_bits = 2;
    profile.parity = "odd";
    profile.flow = "hardware";
    const auto bits = conn::fold_serial_frame(profile);
    AURORA_TEST_REQUIRE(bits.has_value());
    AURORA_TEST_CHECK_EQ(bits->baud, 9600);
    AURORA_TEST_CHECK_EQ(bits->data_bits, 7);
    AURORA_TEST_CHECK_EQ(bits->stop_bits, 2);
    AURORA_TEST_CHECK(bits->parity == SerialParity::Odd);
    AURORA_TEST_CHECK(bits->flow == SerialFlow::Hardware);

    // 折算是校验的同一处：非法档案折不出值。
    profile.parity = "mark";
    AURORA_TEST_CHECK_FALSE(conn::fold_serial_frame(profile).has_value());
    auto no_device = valid_profile();
    no_device.device_path.clear();
    AURORA_TEST_CHECK_FALSE(conn::fold_serial_frame(no_device).has_value());

    // 软流控档同样折得出来（F6：两档流控在替身设备上写得下去）。
    auto software = valid_profile();
    software.flow = "software";
    const auto soft_bits = conn::fold_serial_frame(software);
    AURORA_TEST_REQUIRE(soft_bits.has_value());
    AURORA_TEST_CHECK(soft_bits->flow == SerialFlow::Software);
}

AURORA_TEST_CASE(only_openable_rows_are_selectable) {
    auto row = SerialPortInfo{"/dev/ttyS0", "", SerialPortAccess::Openable};
    AURORA_TEST_CHECK(conn::serial_port_is_selectable(row));
    row.access = SerialPortAccess::Denied;
    AURORA_TEST_CHECK_FALSE(conn::serial_port_is_selectable(row));
    row.access = SerialPortAccess::Busy;
    AURORA_TEST_CHECK_FALSE(conn::serial_port_is_selectable(row));
    row.access = SerialPortAccess::Unavailable;
    AURORA_TEST_CHECK_FALSE(conn::serial_port_is_selectable(row));
}

AURORA_TEST_CASE(sort_ports_is_natural_order_and_drops_duplicates) {
    std::vector<SerialPortInfo> rows{
        SerialPortInfo{"/dev/ttyS10", "", SerialPortAccess::Denied},
        SerialPortInfo{"/dev/ttyS2", "", SerialPortAccess::Openable},
        SerialPortInfo{"/dev/ttyUSB0", "", SerialPortAccess::Openable},
        SerialPortInfo{"/dev/ttyS1", "", SerialPortAccess::Unavailable},
        SerialPortInfo{"/dev/ttyS2", "", SerialPortAccess::Openable},  // 重复行：两处入口报同一节点
    };
    conn::sort_serial_ports(rows);

    AURORA_TEST_REQUIRE_EQ(rows.size(), 4U);
    AURORA_TEST_CHECK_EQ(rows[0].path, "/dev/ttyS1");
    AURORA_TEST_CHECK_EQ(rows[1].path, "/dev/ttyS2");
    // 自然序而不是字典序：ttyS10 必须排在 ttyS2 之后。
    AURORA_TEST_CHECK_EQ(rows[2].path, "/dev/ttyS10");
    AURORA_TEST_CHECK_EQ(rows[3].path, "/dev/ttyUSB0");

    // 排序不看可达性（D9①）：置灰项留在原位，刷新一次不会让下拉跳行。
    AURORA_TEST_CHECK(rows[0].access == SerialPortAccess::Unavailable);
    AURORA_TEST_CHECK(rows[1].access == SerialPortAccess::Openable);

    // 去重保留首条：两条 ttyS2 只留得下一条。
    for (std::size_t index = 1; index < rows.size(); ++index) {
        AURORA_TEST_CHECK_MSG(rows[index].path != rows[index - 1].path, "duplicate row survived");
    }
}

AURORA_TEST_CASE(sort_ports_handles_missing_digits_and_empty_input) {
    std::vector<SerialPortInfo> empty{};
    conn::sort_serial_ports(empty);
    AURORA_TEST_CHECK_EQ(empty.size(), 0U);

    std::vector<SerialPortInfo> rows{
        SerialPortInfo{"/dev/ttyS", "", SerialPortAccess::Openable},
        SerialPortInfo{"/dev/ttyS3", "", SerialPortAccess::Openable},
        SerialPortInfo{"/dev/ttyS0", "", SerialPortAccess::Openable},
    };
    conn::sort_serial_ports(rows);
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);
    AURORA_TEST_CHECK_EQ(rows[0].path, "/dev/ttyS");
    AURORA_TEST_CHECK_EQ(rows[1].path, "/dev/ttyS0");
    AURORA_TEST_CHECK_EQ(rows[2].path, "/dev/ttyS3");

    // 尾数字超长的行不参与溢出：按饱和值排到该干末尾即可。
    std::vector<SerialPortInfo> huge{
        SerialPortInfo{"/dev/ttyS99999999999999999999", "", SerialPortAccess::Openable},
        SerialPortInfo{"/dev/ttyS1", "", SerialPortAccess::Openable},
    };
    conn::sort_serial_ports(huge);
    AURORA_TEST_REQUIRE_EQ(huge.size(), 2U);
    AURORA_TEST_CHECK_EQ(huge[0].path, "/dev/ttyS1");
}

}  // namespace borealis::test_cases::utest_serial_model
