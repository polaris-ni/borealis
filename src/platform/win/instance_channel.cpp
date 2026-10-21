// ============================================================
// 单实例通道：Win32 腿（src/platform/win/instance_channel.cpp）
// ------------------------------------------------------------
// 命名管道 + `FILE_FLAG_FIRST_PIPE_INSTANCE` 认领（`SPEC.FEAT.INTEG.04`，设计稿 §3）。
// 首实例带该标志创建唯一实例：同名管道已在则 `CreateNamedPipe` 直接失败（访问被拒），
// 判定为「已有实例」，转交端随后连上它。管道名随最后一个句柄关闭而消失，没有 POSIX
// 那种崩溃残留要清；`PIPE_REJECT_REMOTE_CLIENTS` 把远端连接挡在系统层。
//
// 选型理由见 `codespec/CLI_STARTUP.draft.md` §3.1：不用环回 TCP（对任何本机进程开放、
// 还要挑端口），也不用 `WM_COPYDATA`（要求首实例有窗口，跨会话不可达）。
//
// 所有 I/O 走 overlapped + stop 事件，析构能立刻叫醒阻塞在
// `ConnectNamedPipe` / 读 / 写上的 transport 线程——关闭句柄从另一线程打断阻塞调用是
// 未定义行为，不能用。
//
// **本沙箱无 MSVC，该腿未经编译验证**（与其余 win 腿同档，属推断/未验证）。
// ============================================================

#include "platform/instance_channel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "aurora/core/log.h"
#include "platform/win/win_text.h"

namespace borealis::platform {

namespace {

constexpr std::size_t kChannelLengthBytes = 4;
constexpr DWORD kPipeBufferBytes = static_cast<DWORD>(kMaxInstanceFrameBytes + kChannelLengthBytes);

[[nodiscard]] auto error_text(const char *what, DWORD code) -> std::string {
    return std::string{what} + " (win32 " + std::to_string(code) + ")";
}

/// @brief 通道键折成完整管道路径；键本身也可已是 `\\.\pipe\...` 形态。
[[nodiscard]] auto pipe_path(const std::string &key) -> std::wstring {
    constexpr std::string_view prefix = "\\\\.\\pipe\\";
    std::string full = key.rfind(prefix, 0) == 0 ? key : std::string{prefix} + key;
    return to_wide(full);
}

/// @brief 把 `u32 little-endian` 长度写进 @p target 前四字节。
auto put_length(std::string &target, std::uint32_t length) -> void {
    target[0] = static_cast<char>(length & 0xFFU);
    target[1] = static_cast<char>((length >> 8U) & 0xFFU);
    target[2] = static_cast<char>((length >> 16U) & 0xFFU);
    target[3] = static_cast<char>((length >> 24U) & 0xFFU);
}

[[nodiscard]] auto read_length(const char *data) -> std::uint32_t {
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(data);
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

struct IoOutcome {
    bool ok{false};
    bool stopped{false};
    DWORD bytes{0};
    DWORD error{0};
};

/// @brief 等一次在途 overlapped I/O；@p stop_event 非空时它也参与等待。
///
/// 等待侧被打断（stop / 超时）时先 `CancelIoEx` 再等内核收回操作，避免
/// OVERLAPPED 还在用而调用方已经离开栈帧。
[[nodiscard]] auto await_io(HANDLE pipe, OVERLAPPED &overlapped, HANDLE stop_event, DWORD timeout_ms)
    -> IoOutcome {
    IoOutcome outcome;
    HANDLE events[2] = {overlapped.hEvent, stop_event};
    const DWORD count = stop_event == nullptr ? 1 : 2;
    const DWORD wait = ::WaitForMultipleObjects(count, events, FALSE, timeout_ms);
    if (wait == WAIT_OBJECT_0) {
        if (::GetOverlappedResult(pipe, &overlapped, &outcome.bytes, FALSE) != 0) {
            outcome.ok = true;
        } else {
            outcome.error = ::GetLastError();
        }
        return outcome;
    }
    if (wait == WAIT_OBJECT_0 + 1 && stop_event != nullptr) {
        outcome.stopped = true;
    } else if (wait == WAIT_TIMEOUT) {
        outcome.error = ERROR_TIMEOUT;
    } else {
        outcome.error = ::GetLastError();
    }
    static_cast<void>(::CancelIoEx(pipe, &overlapped));
    DWORD ignored = 0;
    static_cast<void>(::GetOverlappedResult(pipe, &overlapped, &ignored, TRUE));
    return outcome;
}

[[nodiscard]] auto pipe_read(HANDLE pipe, void *buffer, DWORD size, HANDLE stop_event, DWORD timeout_ms)
    -> IoOutcome {
    OVERLAPPED overlapped{};
    overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
        return {.error = ::GetLastError()};
    }
    const BOOL started = ::ReadFile(pipe, buffer, size, nullptr, &overlapped);
    const DWORD started_error = started != FALSE ? ERROR_SUCCESS : ::GetLastError();
    IoOutcome outcome;
    if (started != FALSE) {
        DWORD transferred = 0;
        static_cast<void>(::GetOverlappedResult(pipe, &overlapped, &transferred, FALSE));
        outcome.ok = true;
        outcome.bytes = transferred;
    } else if (started_error == ERROR_IO_PENDING) {
        outcome = await_io(pipe, overlapped, stop_event, timeout_ms);
    } else {
        outcome.error = started_error;
    }
    ::CloseHandle(overlapped.hEvent);
    return outcome;
}

[[nodiscard]] auto pipe_write(HANDLE pipe, const void *data, DWORD size, HANDLE stop_event,
                              DWORD timeout_ms) -> IoOutcome {
    OVERLAPPED overlapped{};
    overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
        return {.error = ::GetLastError()};
    }
    const BOOL started = ::WriteFile(pipe, data, size, nullptr, &overlapped);
    const DWORD started_error = started != FALSE ? ERROR_SUCCESS : ::GetLastError();
    IoOutcome outcome;
    if (started != FALSE) {
        outcome.ok = true;
        outcome.bytes = size;
    } else if (started_error == ERROR_IO_PENDING) {
        outcome = await_io(pipe, overlapped, stop_event, timeout_ms);
    } else {
        outcome.error = started_error;
    }
    ::CloseHandle(overlapped.hEvent);
    return outcome;
}

enum class ConnectOutcome : std::uint8_t {
    Connected,
    Interrupted,  ///< 客户端连上又立刻走了（ERROR_NO_DATA）：重试，不是故障。
    Stopped,
    Failed,
};

/// @brief 监听端：transport 线程 + 生命周期收尾。
class WinInstanceListener final : public InstanceListener {
  public:
    WinInstanceListener(HANDLE pipe, HANDLE stop_event, InstanceHandler handler)
        : pipe_(pipe),
          stop_event_(stop_event),
          handler_(std::move(handler)),
          thread_([this]() -> void { run(); }) {}

    ~WinInstanceListener() override {
        if (stop_event_ != nullptr) {
            ::SetEvent(stop_event_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (pipe_ != INVALID_HANDLE_VALUE) {
            static_cast<void>(::DisconnectNamedPipe(pipe_));
            ::CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        if (stop_event_ != nullptr) {
            ::CloseHandle(stop_event_);
            stop_event_ = nullptr;
        }
    }

  private:
    auto run() -> void {
        std::vector<char> buffer(kPipeBufferBytes);
        for (;;) {
            const ConnectOutcome connected = connect_client();
            if (connected == ConnectOutcome::Stopped || connected == ConnectOutcome::Failed) {
                return;
            }
            if (connected == ConnectOutcome::Interrupted) {
                continue;
            }
            serve(buffer);
            // 回复先冲干净再断开：DisconnectNamedPipe 会丢掉客户端尚未读走的数据。
            static_cast<void>(::FlushFileBuffers(pipe_));
            static_cast<void>(::DisconnectNamedPipe(pipe_));
        }
    }

    [[nodiscard]] auto connect_client() -> ConnectOutcome {
        OVERLAPPED overlapped{};
        overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            return ConnectOutcome::Failed;
        }
        const BOOL started = ::ConnectNamedPipe(pipe_, &overlapped);
        const DWORD started_error = started != FALSE ? ERROR_SUCCESS : ::GetLastError();
        ConnectOutcome outcome = ConnectOutcome::Failed;
        if (started != FALSE || started_error == ERROR_PIPE_CONNECTED) {
            outcome = ConnectOutcome::Connected;
        } else if (started_error == ERROR_IO_PENDING) {
            const IoOutcome awaited = await_io(pipe_, overlapped, stop_event_, INFINITE);
            if (awaited.ok) {
                outcome = ConnectOutcome::Connected;
            } else if (awaited.stopped) {
                outcome = ConnectOutcome::Stopped;
            }
        } else if (started_error == ERROR_NO_DATA) {
            outcome = ConnectOutcome::Interrupted;
        } else {
            AURORA_LOG_WARN("platform", "instance pipe accept failed (win32 ",
                            static_cast<long>(started_error), ")");
        }
        ::CloseHandle(overlapped.hEvent);
        return outcome;
    }

    auto serve(std::vector<char> &buffer) -> void {
        const IoOutcome read = pipe_read(pipe_, buffer.data(), static_cast<DWORD>(buffer.size()),
                                         stop_event_, INFINITE);
        if (!read.ok || read.bytes < kChannelLengthBytes) {
            return;
        }
        const std::uint32_t length = read_length(buffer.data());
        if (length > kMaxInstanceFrameBytes ||
            read.bytes != length + static_cast<DWORD>(kChannelLengthBytes)) {
            return;  // 消息边界与长度前缀对不上：整条拒绝，不回帧。
        }
        auto request =
            std::make_shared<InstanceRequest>(std::string{buffer.data() + kChannelLengthBytes, length});
        if (handler_) {
            handler_(request);
        }
        const std::optional<std::string> reply = request->wait(kInstanceReplyTimeoutMs);
        if (!reply.has_value() || reply->size() > kMaxInstanceFrameBytes) {
            return;
        }
        std::string frame(kChannelLengthBytes + reply->size(), '\0');
        put_length(frame, static_cast<std::uint32_t>(reply->size()));
        frame.replace(kChannelLengthBytes, reply->size(), *reply);
        static_cast<void>(pipe_write(pipe_, frame.data(), static_cast<DWORD>(frame.size()),
                                     stop_event_, INFINITE));
    }

    HANDLE pipe_{INVALID_HANDLE_VALUE};
    HANDLE stop_event_{nullptr};
    InstanceHandler handler_;
    std::thread thread_;
};

/// @brief 句柄的 RAII 收尾（只在转交侧用）。
class HandleGuard {
  public:
    explicit HandleGuard(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
    ~HandleGuard() { reset(); }

    HandleGuard(const HandleGuard &) = delete;
    auto operator=(const HandleGuard &) -> HandleGuard & = delete;
    HandleGuard(HandleGuard &&) = delete;
    auto operator=(HandleGuard &&) -> HandleGuard & = delete;

    [[nodiscard]] auto get() const -> HANDLE { return handle_; }
    [[nodiscard]] auto valid() const -> bool { return handle_ != INVALID_HANDLE_VALUE; }
    auto reset(HANDLE handle = INVALID_HANDLE_VALUE) -> void {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = handle;
    }

  private:
    HANDLE handle_;
};

[[nodiscard]] auto current_user_token() -> std::string {
    std::string user;
    wchar_t buffer[256] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    if (::GetUserNameW(buffer, &size) != 0) {
        user = to_utf8(std::wstring_view{buffer});
    }
    if (user.empty()) {
        user = "user";
    }
    std::uint32_t hash = 2166136261U;
    for (const char ch : user) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619U;
    }
    std::string safe;
    for (const char ch : user) {
        const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        safe.push_back(keep ? ch : '_');
        if (safe.size() >= 24) {
            break;
        }
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token = "borealis-" + safe + "-";
    for (int shift = 28; shift >= 0; shift -= 4) {
        token.push_back(kHex[(hash >> shift) & 0xFU]);
    }
    return token;
}

}  // namespace

InstanceRequest::InstanceRequest(std::string frame) : frame_(std::move(frame)) {}

auto InstanceRequest::complete(std::string reply) -> void {
    {
        const std::lock_guard lock{mutex_};
        if (done_) {
            return;  // 幂等：第一次回执为准。
        }
        reply_ = std::move(reply);
        done_ = true;
    }
    cv_.notify_all();
}

auto InstanceRequest::wait(int timeout_ms) -> std::optional<std::string> {
    std::unique_lock lock{mutex_};
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                      [this]() -> bool { return done_; })) {
        return std::nullopt;
    }
    return reply_;
}

auto instance_default_key() -> std::string { return current_user_token(); }

auto instance_listen(const std::string &key, InstanceHandler handler) -> InstanceListenResult {
    if (key.empty()) {
        return {.role = InstanceRole::Failed, .listener = nullptr, .error = "instance key is empty"};
    }
    const std::wstring path = pipe_path(key);
    if (path.size() > 256) {
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = "instance pipe name is too long"};
    }
    HANDLE stop_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event == nullptr) {
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = error_text("cannot create the instance stop event", ::GetLastError())};
    }
    const HANDLE pipe = ::CreateNamedPipeW(
        path.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
        kPipeBufferBytes, kPipeBufferBytes, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        const DWORD code = ::GetLastError();
        ::CloseHandle(stop_event);
        if (code == ERROR_ACCESS_DENIED) {
            // FILE_FLAG_FIRST_PIPE_INSTANCE 的同名管道已在（也含 ACL 拒绝这一档；
            // 本键由当前用户拼出，误判的执行后果只是转交一次并报「无回复」）。
            return {.role = InstanceRole::Existing, .listener = nullptr, .error = {}};
        }
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = error_text("cannot create the instance pipe", code)};
    }
    InstanceListenResult result;
    result.role = InstanceRole::Primary;
    result.listener =
        std::make_unique<WinInstanceListener>(pipe, stop_event, std::move(handler));
    return result;
}

auto instance_forward(const std::string &key, std::string_view frame, int timeout_ms)
    -> InstanceForwardResult {
    InstanceForwardResult result;
    if (key.empty()) {
        result.error = "instance key is empty";
        return result;
    }
    if (frame.size() > kMaxInstanceFrameBytes) {
        result.error = "request frame exceeds the channel limit";
        return result;
    }
    if (timeout_ms <= 0) {
        timeout_ms = 1;
    }
    const DWORD budget = static_cast<DWORD>(timeout_ms);
    const std::wstring path = pipe_path(key);
    if (path.size() > 256) {
        result.error = "instance pipe name is too long";
        return result;
    }

    const ULONGLONG deadline = ::GetTickCount64() + budget;
    DWORD last_error = ERROR_FILE_NOT_FOUND;
    HandleGuard pipe;
    for (;;) {
        pipe.reset(::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (pipe.valid()) {
            break;
        }
        last_error = ::GetLastError();
        if (last_error != ERROR_PIPE_BUSY) {
            result.error = error_text("cannot connect to the running instance", last_error);
            return result;
        }
        const ULONGLONG now = ::GetTickCount64();
        if (now >= deadline) {
            break;
        }
        if (::WaitNamedPipeW(path.c_str(), static_cast<DWORD>(deadline - now)) == 0) {
            last_error = ::GetLastError();
            if (last_error == ERROR_PIPE_BUSY || last_error == ERROR_SEM_TIMEOUT) {
                break;
            }
            result.error = error_text("cannot wait for the running instance", last_error);
            return result;
        }
    }
    if (!pipe.valid()) {
        result.error = error_text("cannot connect to the running instance", last_error);
        return result;
    }

    std::string wire(kChannelLengthBytes + frame.size(), '\0');
    put_length(wire, static_cast<std::uint32_t>(frame.size()));
    wire.replace(kChannelLengthBytes, frame.size(), frame);
    const IoOutcome sent =
        pipe_write(pipe.get(), wire.data(), static_cast<DWORD>(wire.size()), nullptr, budget);
    if (!sent.ok) {
        result.error = sent.error == ERROR_TIMEOUT
                           ? std::string{"timed out sending the request to the running instance"}
                           : error_text("cannot send the request to the running instance", sent.error);
        return result;
    }

    std::vector<char> buffer(kPipeBufferBytes);
    const IoOutcome received =
        pipe_read(pipe.get(), buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, budget);
    if (!received.ok || received.bytes < kChannelLengthBytes) {
        if (received.error == ERROR_TIMEOUT) {
            result.error = "timed out waiting for the running instance";
        } else if (received.ok) {
            result.error = "no reply from the running instance";
        } else {
            result.error = error_text("no reply from the running instance", received.error);
        }
        return result;
    }
    const std::uint32_t length = read_length(buffer.data());
    if (length > kMaxInstanceFrameBytes ||
        received.bytes != length + static_cast<DWORD>(kChannelLengthBytes)) {
        result.error = "the reply frame is malformed";
        return result;
    }
    result.delivered = true;
    result.reply.assign(buffer.data() + kChannelLengthBytes, length);
    return result;
}

}  // namespace borealis::platform
