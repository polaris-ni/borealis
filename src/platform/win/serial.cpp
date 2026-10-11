// ============================================================
// Win32 串口腿实现（src/platform/win/serial.cpp）
// ------------------------------------------------------------
// platform::serial_open / serial_list_ports 的 Win32 COMM 腿（SPEC.FEAT.CONN.05）。
// CreateFile / DCB / ClearCommError 知识不外溢：接口见 platform/serial.h
// （AGENTS.md §4.5 第 23 条，platform/win/tcp.cpp 同款纪律）。
//
// ⚠ 推断／未验证（F10，AGENTS.md §4.2 第 11 条）：本沙箱无 MSVC、无 Win32 头可编，
//   本文件的每一句 API 用法都来自 MSDN 形态而非实测读数。落地真机后须按实测回写：
//   ① COM 口枚举改用 SetupAPI/注册表（现按 `COM1..COM256` 逐个 CreateFile 试探）；
//   ② 掉线形态（Win 无 EOF 概念，只有错误码）与 `SerialReadOutcome::Closed` 的对应；
//   ③ 友好名（现留空，取法在 `QueryDosDevice` 与注册表 `Ports` 键之间未定）。
//   POSIX 腿的可证面见 platform/posix/serial.cpp 顶部的 F4–F9 读数，本腿无同级证人。
// ============================================================

#include "platform/serial.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "aurora/core/log.h"

namespace borealis::platform {
namespace {

using steady_clock = std::chrono::steady_clock;

/// @brief 写方向的总时限：与 POSIX 腿同值，语义是「设备不消费就报失败，不吊住调用线程」。
constexpr int kWriteDeadlineMs = 5000;

/// @brief 枚举上界：`COM1..COM256`（推断口径，见文件头①）。
constexpr int kMaxComPortIndex = 256;

/// @brief 读方向的轮询片长：Win 的 ReadFile 超时组合无法区分「没货」与「设备没了」，
///        故先看队列再看字节（推断口径，见文件头②）。
constexpr int kReadPollSliceMs = 5;

/// @brief 打开设备用的规范化名：`\\.\COMn` 让大于 COM9 的口不被当软盘口。
[[nodiscard]] auto build_device_name(const std::string &device_path) -> std::string {
    if (device_path.find("\\\\.\\") == 0) {
        return device_path;
    }
    return "\\\\.\\" + device_path;
}

[[nodiscard]] auto failure_from_last_error(DWORD code) -> SerialFailure {
    switch (code) {
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return SerialFailure::Denied;
        case ERROR_FILE_NOT_FOUND:
            return SerialFailure::NoNode;
        case ERROR_BUSY:
        case ERROR_TOO_MANY_OPEN_FILES:
            return SerialFailure::Busy;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_FUNCTION:
            return SerialFailure::ConfigRejected;
        default:
            return SerialFailure::Io;
    }
}

/// @brief 把折好的帧格式写进 DCB 并回读比对速率（D6② 的 Win 侧同构做法）。
[[nodiscard]] auto apply_frame(HANDLE handle, const conn::SerialFrameBits &frame) -> SerialFailure {
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (::GetCommState(handle, &dcb) == 0) {
        AURORA_LOG_ERROR("platform", "serial GetCommState failed: ", std::to_string(::GetLastError()));
        return failure_from_last_error(::GetLastError());
    }
    dcb.BaudRate = static_cast<DWORD>(frame.baud);
    dcb.ByteSize = static_cast<unsigned char>(frame.data_bits);
    dcb.StopBits = (frame.stop_bits == 2) ? static_cast<unsigned char>(TWOSTOPBITS)
                                          : static_cast<unsigned char>(ONESTOPBIT);
    switch (frame.parity) {
        case conn::SerialParity::Even:
            dcb.Parity = static_cast<unsigned char>(EVENPARITY);
            dcb.fParity = 1;
            break;
        case conn::SerialParity::Odd:
            dcb.Parity = static_cast<unsigned char>(ODDPARITY);
            dcb.fParity = 1;
            break;
        case conn::SerialParity::None:
            dcb.Parity = static_cast<unsigned char>(NOPARITY);
            dcb.fParity = 0;
            break;
    }
    // 打开即置的线控制：硬件流控走握手，否则把 DTR/RTS 拉起来（嵌入式板子的常见要求）。
    dcb.fRtsControl = (frame.flow == conn::SerialFlow::Hardware)
                          ? static_cast<DWORD>(RTS_CONTROL_HANDSHAKE)
                          : static_cast<DWORD>(RTS_CONTROL_ENABLE);
    dcb.fOutxCtsFlow = (frame.flow == conn::SerialFlow::Hardware) ? 1 : 0;
    dcb.fDtrControl = static_cast<DWORD>(DTR_CONTROL_ENABLE);
    dcb.fOutxDsrFlow = 0;
    dcb.fOutX = (frame.flow == conn::SerialFlow::Software) ? 1 : 0;
    dcb.fInX = (frame.flow == conn::SerialFlow::Software) ? 1 : 0;
    dcb.fBinary = 1;
    dcb.fAbortOnError = 0;
    if (::SetCommState(handle, &dcb) == 0) {
        AURORA_LOG_ERROR("platform", "serial SetCommState failed: ", std::to_string(::GetLastError()));
        return SerialFailure::ConfigRejected;
    }
    DCB readback{};
    readback.DCBlength = sizeof(readback);
    if (::GetCommState(handle, &readback) == 0) {
        return SerialFailure::Io;
    }
    if (readback.BaudRate != static_cast<DWORD>(frame.baud)) {
        AURORA_LOG_WARN("platform", "serial baud readback differs: ", std::to_string(frame.baud));
        return SerialFailure::ConfigRejected;
    }
    if (readback.ByteSize != dcb.ByteSize || readback.Parity != dcb.Parity ||
        readback.StopBits != dcb.StopBits) {
        // 与 POSIX 腿同一口径（F5）：帧格式的回读差异只记读数，不以此判死连接。
        AURORA_LOG_WARN("platform", "serial frame readback differs, not enforced");
    }
    return SerialFailure::None;
}

/// @brief 一个已打开的 Win32 串口。
class WinSerialPort final : public SerialPort {
  public:
    WinSerialPort(HANDLE handle, int baud) : handle_(handle), baud_(baud) {}

    ~WinSerialPort() override { close(); }

    auto write(std::span<const std::uint8_t> bytes) -> bool override {
        const auto deadline = steady_clock::now() + std::chrono::milliseconds(kWriteDeadlineMs);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            DWORD written = 0;
            if (::WriteFile(handle_, bytes.data() + offset, static_cast<DWORD>(remaining), &written,
                            nullptr) == 0) {
                purge();
                AURORA_LOG_ERROR("platform", "serial WriteFile failed: ",
                                 std::to_string(::GetLastError()));
                return false;
            }
            offset += static_cast<std::size_t>(written);
            if (written == 0) {
                if (steady_clock::now() >= deadline) {
                    AURORA_LOG_ERROR("platform", "serial write drained too slow");
                    return false;
                }
                ::Sleep(1);
            }
        }
        return true;
    }

    [[nodiscard]] auto read(std::span<std::uint8_t> out, int timeout_ms) -> SerialRead override {
        if (out.empty()) {
            return SerialRead{SerialReadOutcome::Timeout, 0};
        }
        const auto deadline = steady_clock::now() + std::milliseconds(timeout_ms);
        for (;;) {
            COMSTAT status{};
            DWORD errors = 0;
            if (::ClearCommError(handle_, &errors, &status) == 0) {
                // 队列查询本身失败＝口已经不在（推断口径，见文件头②）。
                return SerialRead{SerialReadOutcome::Closed, 0};
            }
            if (status.cbInQue > 0) {
                const auto want = static_cast<DWORD>(std::min<std::size_t>(out.size(), status.cbInQue));
                DWORD got = 0;
                if (::ReadFile(handle_, out.data(), want, &got, nullptr) == 0) {
                    AURORA_LOG_ERROR("platform", "serial ReadFile failed: ",
                                     std::to_string(::GetLastError()));
                    return SerialRead{SerialReadOutcome::Error, 0};
                }
                if (got > 0) {
                    return SerialRead{SerialReadOutcome::Data, static_cast<std::size_t>(got)};
                }
                // 读了 0 字节：状态与队列之间的竞态，当作这一拍没货继续等。
            }
            if (steady_clock::now() >= deadline) {
                return SerialRead{SerialReadOutcome::Timeout, 0};
            }
            ::Sleep(kReadPollSliceMs);
        }
    }

    auto set_baud(int baud) -> bool override {
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (::GetCommState(handle_, &dcb) == 0) {
            return false;
        }
        dcb.BaudRate = static_cast<DWORD>(baud);
        if (::SetCommState(handle_, &dcb) == 0) {
            AURORA_LOG_ERROR("platform", "serial hot SetCommState failed: ",
                             std::to_string(::GetLastError()));
            return false;
        }
        // D6②：写完立刻回读比对，相符才更新「会话当前速率」。
        DCB readback{};
        readback.DCBlength = sizeof(readback);
        if (::GetCommState(handle_, &readback) == 0 ||
            readback.BaudRate != static_cast<DWORD>(baud)) {
            AURORA_LOG_WARN("platform", "serial hot baud readback differs: ", std::to_string(baud));
            return false;
        }
        baud_ = baud;
        return true;
    }

    [[nodiscard]] auto current_baud() const -> int override { return baud_; }

    /// @brief 装读写字节的超时组合：读方向立即返回（等与不等由本类自己轮），
    ///        写方向带总时限。打开流程里由 `serial_open` 调一次。
    [[nodiscard]] auto apply_timeouts() -> bool {
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = 0;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = kWriteDeadlineMs;
        return ::SetCommTimeouts(handle_, &timeouts) != 0;
    }

    auto close() -> void override {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

  private:
    auto purge() -> void {
        ::PurgeComm(handle_, PURGE_TXABORT | PURGE_TXCLEAR);
    }

    HANDLE handle_{INVALID_HANDLE_VALUE};
    int baud_{0};
};

/// @brief 试开一次定可达性（D9①：不可达项照列）。
[[nodiscard]] auto probe_access(const std::string &name) -> conn::SerialPortAccess {
    const auto path = build_device_name(name);
    const HANDLE handle = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        ::CloseHandle(handle);
        return conn::SerialPortAccess::Openable;
    }
    switch (failure_from_last_error(::GetLastError())) {
        case SerialFailure::Denied:
            return conn::SerialPortAccess::Denied;
        case SerialFailure::Busy:
            return conn::SerialPortAccess::Busy;
        default:
            return conn::SerialPortAccess::Unavailable;
    }
}

}  // namespace

auto serial_open(const std::string &device_path, const conn::SerialFrameBits &frame) -> SerialOpenResult {
    if (device_path.empty()) {
        AURORA_LOG_ERROR("platform", "serial open with empty device path");
        return SerialOpenResult{nullptr, SerialFailure::NoNode};
    }
    const auto path = build_device_name(device_path);
    const HANDLE handle = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto failure = failure_from_last_error(::GetLastError());
        AURORA_LOG_ERROR("platform", "serial open failed: ", device_path, " ",
                         std::to_string(::GetLastError()));
        return SerialOpenResult{nullptr, failure};
    }
    auto port = std::make_unique<WinSerialPort>(handle, frame.baud);
    if (!port->apply_timeouts()) {
        AURORA_LOG_ERROR("platform", "serial SetCommTimeouts failed");
        return SerialOpenResult{nullptr, SerialFailure::Io};
    }
    const auto failure = apply_frame(handle, frame);
    if (failure != SerialFailure::None) {
        return SerialOpenResult{nullptr, failure};
    }
    ::PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);  // 置参后丢残留，口径同 POSIX 腿的 tcflush
    return SerialOpenResult{std::move(port), SerialFailure::None};
}

auto serial_list_ports() -> std::vector<conn::SerialPortInfo> {
    std::vector<conn::SerialPortInfo> rows{};
    for (int index = 1; index <= kMaxComPortIndex; ++index) {
        const auto name = "COM" + std::to_string(index);
        const auto access = probe_access(name);
        if (access == conn::SerialPortAccess::Unavailable) {
            continue;  // 不存在的口不进列表（权限不足与被占用者照列，D9①）
        }
        auto info = conn::SerialPortInfo{};
        info.path = name;
        info.friendly = {};  // 友好名取法未定（见文件头③）
        info.access = access;
        rows.push_back(std::move(info));
    }
    return rows;
}

}  // namespace borealis::platform
