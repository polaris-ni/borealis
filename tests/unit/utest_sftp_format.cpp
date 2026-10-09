/// 测试类型: unit
/// 目标单元: src/ui/sftp_format.h（私有头）纯格式化函数
/// 测试说明: 守住 SFTP 面板格式化矩阵——human_size 的 1024 进各档与整值去
///           ".0"、UTC 时间的定值断言（含闰日）、本地时间的形态断言（时区
///           依运行环境不可定值）、八进制权限的掩码口径、错误码→词条 key 的
///           全枚举穷举。面板装配（sftp_panel）不在无头单测范围。

#include "ui/sftp_format.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_sftp_format {

AURORA_TEST_CASE(human_size_covers_byte_and_binary_tiers) {
    using ui::human_size;

    // B 档：不带小数。
    AURORA_TEST_CHECK_MSG(human_size(0) == "0 B", "zero bytes");
    AURORA_TEST_CHECK_MSG(human_size(1) == "1 B", "single byte");
    AURORA_TEST_CHECK_MSG(human_size(512) == "512 B", "half kilobyte stays in bytes");
    AURORA_TEST_CHECK_MSG(human_size(1023) == "1023 B", "one below the binary tier boundary");

    // 二进档：整值去 ".0"，非整值一位小数（截断口径）。
    AURORA_TEST_CHECK_MSG(human_size(1024) == "1 KB", "whole kilobyte drops the decimal");
    AURORA_TEST_CHECK_MSG(human_size(1536) == "1.5 KB", "one and a half kilobyte");
    AURORA_TEST_CHECK_MSG(human_size(1048576) == "1 MB", "whole megabyte");
    AURORA_TEST_CHECK_MSG(human_size(1073741824ULL) == "1 GB", "whole gigabyte");
    AURORA_TEST_CHECK_MSG(human_size(1099511627776ULL) == "1 TB", "whole terabyte caps the ladder");
}

AURORA_TEST_CASE(format_mtime_utc_renders_fixed_instants) {
    using ui::format_mtime_utc;

    AURORA_TEST_CHECK_MSG(format_mtime_utc(0) == "1970-01-01 00:00", "epoch itself");
    AURORA_TEST_CHECK_MSG(format_mtime_utc(1728489600) == "2024-10-09 16:00", "fixed modern instant");
    AURORA_TEST_CHECK_MSG(format_mtime_utc(951782400) == "2000-02-29 00:00", "leap day renders as Feb 29");
}

AURORA_TEST_CASE(format_mtime_local_keeps_display_shape) {
    // 本地时区依运行环境，不可定值断言；只锁「同一条渲染管线」的形态：
    // 16 字符、分隔符落位、其余全为数字。UTC 腿的定值断言已覆盖拼串本身。
    const std::string text = ui::format_mtime_local(1728489600);
    AURORA_TEST_CHECK_EQ(text.size(), std::size_t{16});
    AURORA_TEST_CHECK_MSG(text[4] == '-' && text[7] == '-', "date separators in place");
    AURORA_TEST_CHECK_MSG(text[10] == ' ' && text[13] == ':', "time separators in place");
    AURORA_TEST_CHECK_MSG(std::all_of(text.begin(), text.end(), [](char c) -> bool {
                              return c == '-' || c == ' ' || c == ':' || (c >= '0' && c <= '9');
                          }),
                          "digits and separators only");
}

AURORA_TEST_CASE(format_octal_masks_to_permission_bits) {
    using ui::format_octal;

    AURORA_TEST_CHECK_MSG(format_octal(0644) == "0644", "plain file mode");
    AURORA_TEST_CHECK_MSG(format_octal(0755) == "0755", "plain executable mode");
    AURORA_TEST_CHECK_MSG(format_octal(04755) == "4755", "setuid lands in the octal string");
    AURORA_TEST_CHECK_MSG(format_octal(0) == "0000", "zero pads to four digits");
    AURORA_TEST_CHECK_MSG(format_octal(0100644U) == "0644", "type bits are masked away");
}

AURORA_TEST_CASE(sftp_error_key_maps_every_classifier_value) {
    using conn::SftpError;
    using ui::sftp_error_key;

    AURORA_TEST_CHECK_TRUE(sftp_error_key(SftpError::None).empty());
    AURORA_TEST_CHECK_MSG(sftp_error_key(SftpError::NotConnected) == "sftp.error.not_connected",
                          "not connected key");
    AURORA_TEST_CHECK_MSG(sftp_error_key(SftpError::Network) == "sftp.error.network", "network key");
    AURORA_TEST_CHECK_MSG(sftp_error_key(SftpError::PermissionDenied) == "sftp.error.permission_denied",
                          "permission denied key");
    AURORA_TEST_CHECK_MSG(sftp_error_key(SftpError::NotFound) == "sftp.error.not_found", "not found key");
    AURORA_TEST_CHECK_MSG(sftp_error_key(SftpError::Cancelled) == "sftp.error.cancelled", "cancelled key");
    AURORA_TEST_CHECK_MSG(sftp_error_key(SftpError::LocalIo) == "sftp.error.local_io", "local io key");
    AURORA_TEST_CHECK_MSG(sftp_error_key(SftpError::Protocol) == "sftp.error.protocol", "protocol key");
}

}  // namespace borealis::test_cases::utest_sftp_format
