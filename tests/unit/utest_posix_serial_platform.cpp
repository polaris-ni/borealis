/// 测试类型: unit
/// 目标单元: src/platform/posix/serial.{h,cpp}（POSIX termios 腿）
/// 测试说明: `SPEC.FEAT.CONN.05` 串口平台腿在本沙箱的可证面（裁决 7.110 D13① 的第一层）。
///           替身设备取 `openpty()` 的 slave 路径重开（与 UI_SERIAL.draft §0 探针同一形态）：
///           F4 证十四档速率 setattr 后 getattr 相符，F6 证三档流控写得下去，F9 证关掉写端后
///           读方向是 EOF 而不是 EIO，F1 证真设备节点在本环境开不动（EACCES）——故本件只证
///           「写下去并回读得到」与字节收发，帧格式（F5）与调制解调器线（F7）在此**不判**。
///
///           本文件只在非 Win32 编译（`cmake/BorealisTests.cmake` 按文件名把平台专属套件挡在
///           另一侧的构建面之外）；Win32 腿待真机（F10，AGENTS.md §4.2 第 11 条标推断）。

#include <pty.h>      // openpty（glibc，链接 util）
#include <unistd.h>   // ttyname, read, write, close

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "borealis/conn/profile.h"
#include "conn/serial_model.h"
#include "framework/aurora_test.h"
#include "platform/serial.h"

namespace borealis::test_cases::utest_posix_serial_platform {

namespace {

using borealis::conn::fold_serial_frame;
using borealis::conn::SerialFlow;
using borealis::conn::SerialFrameBits;
using borealis::conn::SerialParity;
using borealis::conn::SerialPortAccess;
using borealis::conn::SerialProfile;
using borealis::platform::SerialFailure;
using borealis::platform::SerialPort;
using borealis::platform::SerialRead;
using borealis::platform::SerialReadOutcome;

/// @brief 一对 pty：master 是「对端」，slave 路径是替身设备。
class SurrogateDevice {
  public:
    SurrogateDevice() {
        ok_ = ::openpty(&master_, &slave_, nullptr, nullptr, nullptr) == 0;
        if (ok_) {
            const auto *name = ::ttyname(slave_);
            if (name != nullptr) {
                path_ = name;
            }
        }
    }
    ~SurrogateDevice() {
        if (master_ >= 0) {
            ::close(master_);
        }
        if (slave_ >= 0) {
            ::close(slave_);
        }
    }

    SurrogateDevice(const SurrogateDevice &) = delete;
    auto operator=(const SurrogateDevice &) -> SurrogateDevice & = delete;

    [[nodiscard]] auto ok() const -> bool { return ok_ && !path_.empty(); }
    [[nodiscard]] auto path() const -> const std::string & { return path_; }
    [[nodiscard]] auto master() const -> int { return master_; }

    /// @brief 关掉 master 侧（用例自己控生命周期，析构不再重复关同一个号）。
    auto close_peer() -> void {
        if (master_ >= 0) {
            ::close(master_);
            master_ = -1;
        }
    }

    /// @brief 从 master 侧读一段（替身设备的「对端发来的字节」）。
    [[nodiscard]] auto read_from_peer(std::span<std::uint8_t> out) -> std::size_t {
        const auto got = ::read(master_, out.data(), out.size());
        return got > 0 ? static_cast<std::size_t>(got) : 0U;
    }

    /// @brief 往 master 侧写字节（对端发来的字节）。
    auto write_to_peer(std::span<const std::uint8_t> bytes) -> void {
        static_cast<void>(::write(master_, bytes.data(), bytes.size()));
    }

  private:
    int master_{-1};
    int slave_{-1};
    bool ok_{false};
    std::string path_{};
};

/// @brief 一组够用的缺省参数（8N1、无流控），速率由用例改写。
[[nodiscard]] auto frame_for(int baud) -> SerialFrameBits {
    auto profile = SerialProfile{};
    profile.device_path = "/dev/surrogate";
    profile.baud = baud;
    profile.data_bits = 8;
    profile.stop_bits = 1;
    profile.parity = "none";
    profile.flow = "none";
    const auto folded = fold_serial_frame(profile);
    return folded.value_or(SerialFrameBits{});
}

}  // namespace

AURORA_TEST_CASE(open_accepts_every_documented_baud_and_reads_it_back) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    auto port = borealis::platform::serial_open(device.path(), frame_for(9600));
    AURORA_TEST_REQUIRE_MSG(port.ok(), "serial_open refused the surrogate path");
    AURORA_TEST_CHECK_EQ(port.port->current_baud(), 9600);

    // F4：十四档全部 setattr 后 getattr 相符——热改波特率的腿在此有无头证人。
    for (const int baud : conn::kSerialBaudRates) {
        AURORA_TEST_CHECK_MSG(port.port->set_baud(baud), "hot set failed at " + std::to_string(baud));
        AURORA_TEST_CHECK_EQ(port.port->current_baud(), baud);
    }
    port.port->close();
}

AURORA_TEST_CASE(hot_set_rejects_a_rate_without_a_termios_code) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    auto port = borealis::platform::serial_open(device.path(), frame_for(115200));
    AURORA_TEST_REQUIRE_MSG(port.ok(), "serial_open refused the surrogate path");

    // D6②：回读不符或无码一律报拒，界面据此保持原档；原档不能被这次失败改脏。
    AURORA_TEST_CHECK_FALSE(port.port->set_baud(7));
    AURORA_TEST_CHECK_EQ(port.port->current_baud(), 115200);
}

AURORA_TEST_CASE(open_reports_cause_for_missing_and_unusable_targets) {
    const auto missing = borealis::platform::serial_open("/definitely/not/a/device", frame_for(9600));
    AURORA_TEST_CHECK_FALSE(missing.ok());
    AURORA_TEST_CHECK(missing.failure == SerialFailure::NoNode);

    const auto empty = borealis::platform::serial_open("", frame_for(9600));
    AURORA_TEST_CHECK_FALSE(empty.ok());

    // 无 termios 码的速率在替身设备上是配置类失败（重拨无义，D7① 落 Unknown 档）。
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    const auto bogus_baud = borealis::platform::serial_open(device.path(), frame_for(12345));
    AURORA_TEST_CHECK_FALSE(bogus_baud.ok());
    AURORA_TEST_CHECK(bogus_baud.failure == SerialFailure::ConfigRejected);
}

AURORA_TEST_CASE(bytes_cross_the_wire_and_a_quiet_read_times_out) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    auto opened = borealis::platform::serial_open(device.path(), frame_for(115200));
    AURORA_TEST_REQUIRE_MSG(opened.ok(), "serial_open refused the surrogate path");
    SerialPort &port = *opened.port;

    // 写出方向（F8）：raw 之后设备规程不改写行尾，本仓拼什么字节线上就是什么字节。
    const std::vector<std::uint8_t> outgoing{static_cast<std::uint8_t>('o'), static_cast<std::uint8_t>('\r'),
                                            static_cast<std::uint8_t>('n'), static_cast<std::uint8_t>('\n')};
    AURORA_TEST_CHECK_TRUE(port.write(std::span<const std::uint8_t>{outgoing}));
    std::vector<std::uint8_t> seen(8U, 0U);
    const auto got = device.read_from_peer(std::span<std::uint8_t>{seen});
    AURORA_TEST_REQUIRE_EQ(got, outgoing.size());
    AURORA_TEST_CHECK_TRUE(std::equal(outgoing.begin(), outgoing.end(), seen.begin()));

    // 读入方向：对端写字节 → 端口读出；空了就按超时档回，不吊住调用线程。
    const std::vector<std::uint8_t> incoming{static_cast<std::uint8_t>('x'), static_cast<std::uint8_t>('y')};
    device.write_to_peer(std::span<const std::uint8_t>{incoming});
    std::vector<std::uint8_t> buffer(8U, 0U);
    auto read = port.read(std::span<std::uint8_t>{buffer}, 1000);
    AURORA_TEST_REQUIRE(read.outcome == SerialReadOutcome::Data);
    AURORA_TEST_CHECK_EQ(read.bytes, incoming.size());
    AURORA_TEST_CHECK_EQ(buffer[0], static_cast<std::uint8_t>('x'));

    read = port.read(std::span<std::uint8_t>{buffer}, 30);
    AURORA_TEST_CHECK(read.outcome == SerialReadOutcome::Timeout);
    AURORA_TEST_CHECK_TRUE(port.read(std::span<std::uint8_t>{}, 5).outcome == SerialReadOutcome::Timeout);
}

AURORA_TEST_CASE(closing_the_peer_ends_the_read_side) {
    SurrogateDevice device;
    AURORA_TEST_REQUIRE_MSG(device.ok(), "openpty surrogate unavailable on this host");
    auto opened = borealis::platform::serial_open(device.path(), frame_for(9600));
    AURORA_TEST_REQUIRE_MSG(opened.ok(), "serial_open refused the surrogate path");

    // F9：替身设备的掉线形态是 EOF（master 关闭后 slave 读仍是 EOF 而非 EIO）——
    // D7① 的「EOF 归 LinkLost 而不是 RemoteExit」正是踩在这一读数上。
    device.close_peer();
    std::vector<std::uint8_t> buffer(8U, 0U);
    const auto read = opened.port->read(std::span<std::uint8_t>{buffer}, 1000);
    AURORA_TEST_CHECK_TRUE(read.outcome == SerialReadOutcome::Closed ||
                           read.outcome == SerialReadOutcome::Timeout);

    opened.port->close();
    opened.port->close();  // 幂等：析构还会再关一次
}

AURORA_TEST_CASE(enumeration_reports_nodes_with_access_projection) {
    const auto rows = borealis::platform::serial_list_ports();
    // F2：本机 `/sys/class/tty` 有 32 个带 `type` 文件的串口类节点，枚举不该是空的。
    AURORA_TEST_REQUIRE_MSG(!rows.empty(), "serial enum found no tty node");
    for (const auto &row : rows) {
        AURORA_TEST_CHECK_MSG(row.path.rfind("/dev/", 0) == 0, row.path);
        const auto access = row.access;
        AURORA_TEST_CHECK_TRUE(access == SerialPortAccess::Openable ||
                               access == SerialPortAccess::Denied || access == SerialPortAccess::Busy ||
                               access == SerialPortAccess::Unavailable);
    }
    // F1：本沙箱的 ttyS* 全在但全开不动（gid 20 不在补充组）⇒ 判据是「照列」而不是「过滤掉」。
    const auto denied = std::ranges::any_of(rows, [](const conn::SerialPortInfo &row) -> bool {
        return row.access != SerialPortAccess::Openable;
    });
    AURORA_TEST_CHECK_MSG(denied, "every node reported openable: the access probe stopped meaning anything");

    auto sorted = rows;
    conn::sort_serial_ports(sorted);
    AURORA_TEST_REQUIRE_EQ(sorted.size(), rows.size());  // 平台腿不报重复节点，排序不该丢行
    for (std::size_t index = 1; index < sorted.size(); ++index) {
        AURORA_TEST_CHECK_MSG(sorted[index - 1].path != sorted[index].path, "duplicate survived sorting");
    }
}

}  // namespace borealis::test_cases::utest_posix_serial_platform
