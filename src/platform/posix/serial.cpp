// ============================================================
// POSIX 串口腿实现（src/platform/posix/serial.cpp）
// ------------------------------------------------------------
// platform::serial_open / serial_list_ports 的 POSIX 腿（SPEC.FEAT.CONN.05）。
// termios 知识不外溢：接口见 platform/serial.h（AGENTS.md §4.5 第 23 条）。
//
// 本文件的可证面以 2026-10-11 沙箱读数为准（UI_SERIAL.draft §0）：
// - F4：十四档波特率在 pty slave 替身路径上 setattr+getattr 逐档一致 ⇒ 速率是可判据；
// - F5：CS6/CS7 与「只置 PARENB」直接 EINVAL，CS5/CS8 接受但回读 c_cflag 与写入值不等
//       ⇒ 帧格式只折位、不以回读判死（否则替身与部分内核路径会假报失败）；
// - F6：CRTSCTS 与 IXON|IXOFF 写得下去且回读一致 ⇒ 流控两档可证；
// - F8：设备规程不改写 CR/LF ⇒ 行尾折算在本仓写出侧，这里一律 raw（OPOST 关）；
// - F9：替身掉线形态是 EOF 而不是 EIO ⇒ 两档都出接口，归因留给 conn 层。
// ============================================================

#include "platform/serial.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "aurora/core/log.h"

namespace borealis::platform {
namespace {

/// @brief 写方向的总时限：写不出去＝对端不消费且无流控，超时就报失败而不是吊住调用线程。
constexpr int kWriteDeadlineMs = 5000;

/// @brief 枚举目录与别名目录（F2/F3 的实测口径：判据是 `type` 文件存在，不是名字前缀）。
constexpr const char *kSysTtyClass = "/sys/class/tty";
constexpr const char *kDevDir = "/dev";
constexpr const char *kSerialById = "/dev/serial/by-id";

/// @brief 速率 → termios 码。F4 实测十四档必收，其余是 Linux 传统档位（档案校验允许
///        [1,4000000]，落不到码表的速率由 `apply_frame` 报 ConfigRejected）。
[[nodiscard]] auto baud_to_speed(int baud) -> std::optional<speed_t> {
    switch (baud) {
        case 50:
            return B50;
        case 75:
            return B75;
        case 110:
            return B110;
        case 134:
            return B134;
        case 150:
            return B150;
        case 200:
            return B200;
        case 300:
            return B300;
        case 600:
            return B600;
        case 1200:
            return B1200;
        case 1800:
            return B1800;
        case 2400:
            return B2400;
        case 4800:
            return B4800;
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
#ifdef B460800
        case 460800:
            return B460800;
#endif
#ifdef B921600
        case 921600:
            return B921600;
#endif
#ifdef B1000000
        case 1000000:
            return B1000000;
#endif
#ifdef B1500000
        case 1500000:
            return B1500000;
#endif
#ifdef B2000000
        case 2000000:
            return B2000000;
#endif
        default:
            return std::nullopt;
    }
}

/// @brief errno → 打开归因档（裁决 7.110 D7① 的分类事实，CloseReason 映射不在这里）。
[[nodiscard]] auto failure_from_errno(int code) -> SerialFailure {
    switch (code) {
        case EACCES:
        case EPERM:
            return SerialFailure::Denied;
        case ENOENT:
        case ENODEV:
            return SerialFailure::NoNode;
        case ENXIO:
            return SerialFailure::Hangup;
        case EBUSY:
            return SerialFailure::Busy;
        default:
            return SerialFailure::Io;
    }
}

/// @brief 把折好的帧格式写进 termios 并回读；失败回归因档（None＝成功）。
///
/// 速率以回读相符为硬判据（D6②），帧格式的回读差异只记 WARN：F5 实测同一台替身设备
/// 对 CS5/CS8 的回读值与写入值不等，拿它判死会在无真设备时假报失败。
[[nodiscard]] auto apply_frame(int fd, const conn::SerialFrameBits &frame) -> SerialFailure {
    const auto code = baud_to_speed(frame.baud);
    if (!code) {
        AURORA_LOG_ERROR("platform", "serial baud has no termios code: ", std::to_string(frame.baud));
        return SerialFailure::ConfigRejected;
    }
    termios state{};
    if (::tcgetattr(fd, &state) != 0) {
        AURORA_LOG_ERROR("platform", "serial tcgetattr failed: ", std::strerror(errno));
        return failure_from_errno(errno);
    }
    // raw 全套：设备规程一律不参与改写（F8），行尾由本仓写出侧拼字节。
    state.c_iflag &= ~(static_cast<unsigned>(IGNBRK) | static_cast<unsigned>(BRKINT) |
                       static_cast<unsigned>(PARMRK) | static_cast<unsigned>(ISTRIP) |
                       static_cast<unsigned>(INLCR) | static_cast<unsigned>(IGNCR) |
                       static_cast<unsigned>(ICRNL) | static_cast<unsigned>(IXON) |
                       static_cast<unsigned>(IXOFF) | static_cast<unsigned>(IXANY));
    state.c_oflag &= ~static_cast<unsigned>(OPOST);
    state.c_lflag &= ~(static_cast<unsigned>(ECHO) | static_cast<unsigned>(ECHOE) |
                       static_cast<unsigned>(ECHOK) | static_cast<unsigned>(ECHONL) |
                       static_cast<unsigned>(ICANON) | static_cast<unsigned>(ISIG) |
                       static_cast<unsigned>(IEXTEN));
    state.c_cflag &= ~(static_cast<unsigned>(CSIZE) | static_cast<unsigned>(PARENB) |
                       static_cast<unsigned>(PARODD) | static_cast<unsigned>(CSTOPB));
#ifdef CRTSCTS
    state.c_cflag &= ~static_cast<unsigned>(CRTSCTS);
#endif

    switch (frame.data_bits) {
        case 5:
            state.c_cflag |= CS5;
            break;
        case 6:
            state.c_cflag |= CS6;
            break;
        case 7:
            state.c_cflag |= CS7;
            break;
        default:
            state.c_cflag |= CS8;
            break;
    }
    if (frame.stop_bits == 2) {
        state.c_cflag |= CSTOPB;
    }
    if (frame.parity != conn::SerialParity::None) {
        state.c_cflag |= PARENB;
        // 只有设了校验位才启内核校验与丢错帧：none 档下 INPCK 会把偶发噪声读成校验错。
        state.c_iflag |= INPCK;
        if (frame.parity == conn::SerialParity::Odd) {
            state.c_cflag |= PARODD;
        }
    } else {
        state.c_cflag &= ~static_cast<unsigned>(PARENB);
        state.c_iflag |= IGNPAR;
    }
    if (frame.flow == conn::SerialFlow::Hardware) {
#ifdef CRTSCTS
        state.c_cflag |= CRTSCTS;
#else
        AURORA_LOG_WARN("platform", "serial hardware flow control unsupported on this libc");
#endif
    } else if (frame.flow == conn::SerialFlow::Software) {
        state.c_iflag |= (IXON | IXOFF | IXANY);
    }
    state.c_cflag |= static_cast<unsigned>(CLOCAL | CREAD);  // 不认调制解调器线、不挂断（串口调试常态）
    state.c_cc[VMIN] = 0;                                    // 读方向靠 poll 定时，不靠 VMIN/VTIME
    state.c_cc[VTIME] = 0;
    if (::cfsetispeed(&state, *code) != 0 || ::cfsetospeed(&state, *code) != 0) {
        AURORA_LOG_ERROR("platform", "serial cfset failed: ", std::strerror(errno));
        return SerialFailure::ConfigRejected;
    }
    if (::tcsetattr(fd, TCSANOW, &state) != 0) {
        AURORA_LOG_ERROR("platform", "serial tcsetattr failed: ", std::strerror(errno));
        return SerialFailure::ConfigRejected;
    }
    termios readback{};
    if (::tcgetattr(fd, &readback) != 0) {
        AURORA_LOG_ERROR("platform", "serial readback tcgetattr failed: ", std::strerror(errno));
        return failure_from_errno(errno);
    }
    if (::cfgetispeed(&readback) != *code || ::cfgetospeed(&readback) != *code) {
        AURORA_LOG_WARN("platform", "serial baud readback differs: ", std::to_string(frame.baud));
        return SerialFailure::ConfigRejected;
    }
    unsigned mask = static_cast<unsigned>(CSIZE | PARENB | PARODD | CSTOPB);
#ifdef CRTSCTS
    mask |= static_cast<unsigned>(CRTSCTS);
#endif
    if ((readback.c_cflag & mask) != (state.c_cflag & mask)) {
        // F5：帧格式在替身设备上不可判，故不以此判死连接，只留读数供真机排查。
        AURORA_LOG_WARN("platform", "serial frame readback differs, not enforced");
    }
    return SerialFailure::None;
}

/// @brief 一个已打开的 POSIX 串口。
class PosixSerialPort final : public SerialPort {
  public:
    explicit PosixSerialPort(int fd, int baud) : fd_(fd), baud_(baud) {}

    ~PosixSerialPort() override { close(); }

    auto write(std::span<const std::uint8_t> bytes) -> bool override {
        const auto deadline = steady_clock::now() + std::chrono::milliseconds(kWriteDeadlineMs);
        while (!bytes.empty()) {
            const auto written = ::write(fd_, bytes.data(), bytes.size());
            if (written > 0) {
                bytes = bytes.subspan(static_cast<std::size_t>(written));
                continue;
            }
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (steady_clock::now() >= deadline || !wait_for(POLLOUT, kWriteDeadlineMs)) {
                    AURORA_LOG_ERROR("platform", "serial write drained too slow");
                    return false;
                }
                continue;
            }
            AURORA_LOG_ERROR("platform", "serial write failed: ", std::strerror(errno));
            return false;
        }
        return true;
    }

    [[nodiscard]] auto read(std::span<std::uint8_t> out, int timeout_ms) -> SerialRead override {
        if (out.empty()) {
            return SerialRead{SerialReadOutcome::Timeout, 0};
        }
        if (!wait_for(POLLIN, timeout_ms)) {
            return SerialRead{SerialReadOutcome::Timeout, 0};
        }
        const auto got = ::read(fd_, out.data(), out.size());
        if (got > 0) {
            return SerialRead{SerialReadOutcome::Data, static_cast<std::size_t>(got)};
        }
        if (got == 0) {
            // F9：替身设备与拔线后的主形态都是 EOF。
            return SerialRead{SerialReadOutcome::Closed, 0};
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return SerialRead{SerialReadOutcome::Timeout, 0};
        }
        // EIO（真设备拔出）与其余硬错同档：掉线归因由 conn 层统一映射（D7①）。
        AURORA_LOG_ERROR("platform", "serial read failed: ", std::strerror(errno));
        return SerialRead{SerialReadOutcome::Error, 0};
    }

    auto set_baud(int baud) -> bool override {
        const auto code = baud_to_speed(baud);
        if (!code) {
            AURORA_LOG_ERROR("platform", "serial baud has no termios code: ", std::to_string(baud));
            return false;
        }
        termios state{};
        if (::tcgetattr(fd_, &state) != 0) {
            AURORA_LOG_ERROR("platform", "serial hot set tcgetattr failed: ", std::strerror(errno));
            return false;
        }
        if (::cfsetispeed(&state, *code) != 0 || ::cfsetospeed(&state, *code) != 0) {
            return false;
        }
        if (::tcsetattr(fd_, TCSANOW, &state) != 0) {
            AURORA_LOG_ERROR("platform", "serial hot setattr failed: ", std::strerror(errno));
            return false;
        }
        // D6②：setattr 之后立刻 getattr 回读比对，相符才算生效并更新「会话当前速率」。
        termios readback{};
        if (::tcgetattr(fd_, &readback) != 0) {
            AURORA_LOG_ERROR("platform", "serial hot readback failed: ", std::strerror(errno));
            return false;
        }
        if (::cfgetispeed(&readback) != *code || ::cfgetospeed(&readback) != *code) {
            AURORA_LOG_WARN("platform", "serial hot baud readback differs: ", std::to_string(baud));
            return false;
        }
        baud_ = baud;
        return true;
    }

    [[nodiscard]] auto current_baud() const -> int override { return baud_; }

    auto close() -> void override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

  private:
    using steady_clock = std::chrono::steady_clock;

    /// @brief poll 等待一个事件档；超时或被中断回 false。
    [[nodiscard]] auto wait_for(short events, int timeout_ms) -> bool {
        pollfd watched{fd_, events, 0};
        for (;;) {
            const auto ready = ::poll(&watched, 1, timeout_ms);
            if (ready > 0) {
                return (watched.revents & events) != 0;
            }
            if (ready < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
    }

    int fd_{-1};
    int baud_{0};
};

/// @brief 试开一次定可达性（D9①：不可达项照列，判据来自实开而不来自权限位猜测）。
[[nodiscard]] auto probe_access(const std::string &path) -> conn::SerialPortAccess {
    const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd >= 0) {
        ::close(fd);
        return conn::SerialPortAccess::Openable;
    }
    switch (failure_from_errno(errno)) {
        case SerialFailure::Denied:
            return conn::SerialPortAccess::Denied;
        case SerialFailure::Busy:
            return conn::SerialPortAccess::Busy;
        default:
            return conn::SerialPortAccess::Unavailable;
    }
}

/// @brief 取 `/dev/serial/by-id` 里指向同一节点的名字作友好名；取不到回空串。
///
/// 本沙箱无该目录（F3），故此函数在 Linux 上按「目录不存在＝一律无友好名」工作；
/// USB 串口的 `product` 文件取法待真机复测回写。
[[nodiscard]] auto friendly_for(const std::string &path) -> std::string {
    char target[PATH_MAX] = {0};
    const auto resolved = ::realpath(path.c_str(), target);
    if (resolved == nullptr) {
        return {};
    }
    DIR *dir = ::opendir(kSerialById);
    if (dir == nullptr) {
        return {};
    }
    std::string friendly{};
    while (const auto *entry = ::readdir(dir)) {
        const std::string name{entry->d_name};
        if (name == "." || name == "..") {
            continue;
        }
        char link_target[PATH_MAX] = {0};
        const auto link = std::string{kSerialById} + "/" + name;
        if (::realpath(link.c_str(), link_target) != nullptr && std::strcmp(link_target, resolved) == 0) {
            friendly = name;
            break;
        }
    }
    ::closedir(dir);
    return friendly;
}

}  // namespace

auto serial_open(const std::string &device_path, const conn::SerialFrameBits &frame) -> SerialOpenResult {
    if (device_path.empty()) {
        AURORA_LOG_ERROR("platform", "serial open with empty device path");
        return SerialOpenResult{nullptr, SerialFailure::NoNode};
    }
    const int fd = ::open(device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        const auto failure = failure_from_errno(errno);
        AURORA_LOG_ERROR("platform", "serial open failed: ", device_path, " ", std::strerror(errno));
        return SerialOpenResult{nullptr, failure};
    }
    const auto failure = apply_frame(fd, frame);
    if (failure != SerialFailure::None) {
        ::close(fd);
        return SerialOpenResult{nullptr, failure};
    }
    ::tcflush(fd, TCIOFLUSH);  // 置参后丢弃残留：内核缓冲里的旧字节是上一个速率读的，不该串进新会话
    return SerialOpenResult{std::make_unique<PosixSerialPort>(fd, frame.baud), SerialFailure::None};
}

auto serial_list_ports() -> std::vector<conn::SerialPortInfo> {
    std::vector<conn::SerialPortInfo> rows{};
    DIR *dir = ::opendir(kSysTtyClass);
    if (dir == nullptr) {
        AURORA_LOG_ERROR("platform", "serial enum cannot open: ", kSysTtyClass);
        return rows;
    }
    while (const auto *entry = ::readdir(dir)) {
        const std::string name{entry->d_name};
        if (name == "." || name == "..") {
            continue;
        }
        // F2：`type` 文件只有串口类节点有（虚拟 tty 没有），比名字前缀白名单可靠。
        const auto type_path = std::string{kSysTtyClass} + "/" + name + "/type";
        if (::access(type_path.c_str(), R_OK) != 0) {
            continue;
        }
        const auto device_path = std::string{kDevDir} + "/" + name;
        if (::access(device_path.c_str(), F_OK) != 0) {
            continue;
        }
        auto info = conn::SerialPortInfo{};
        info.path = device_path;
        info.friendly = friendly_for(device_path);
        info.access = probe_access(device_path);
        rows.push_back(std::move(info));
    }
    ::closedir(dir);
    return rows;
}

}  // namespace borealis::platform
