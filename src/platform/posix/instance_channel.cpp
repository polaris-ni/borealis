// ============================================================
// 单实例通道：POSIX 腿（src/platform/posix/instance_channel.cpp）
// ------------------------------------------------------------
// Unix domain socket + `flock` 认领（`SPEC.FEAT.INTEG.04`，设计稿 §3）。
// 认领权威是锁文件而不是「能否 connect」：首实例在生命周期内持排他锁，
// 抢到锁后先 unlink 陈旧 socket 再 bind，崩溃残留因此不会把用户永久挡在门外。
// 目录 0700、socket 0600；transport 线程只搬字节，不解析载荷、不触达 UI。
// ============================================================

#include "platform/instance_channel.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

namespace borealis::platform {

namespace {

constexpr std::uint32_t kChannelLengthBytes = 4;
constexpr int kAcceptPollMs = 200;

[[nodiscard]] auto errno_text(const char *what, int code) -> std::string {
    return std::string{what} + " (errno " + std::to_string(code) + ")";
}

[[nodiscard]] auto ensure_private_directory(const std::filesystem::path &dir, std::string &error)
    -> bool {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        if (!std::filesystem::create_directory(dir, ec)) {
            error = errno_text("cannot create the instance directory", ec.value());
            return false;
        }
        std::filesystem::permissions(dir, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, ec);
        if (ec) {
            error = errno_text("cannot restrict the instance directory", ec.value());
            return false;
        }
    }
    if (!std::filesystem::is_directory(dir, ec)) {
        error = "the instance directory path is not a directory";
        return false;
    }
    struct stat info {};
    if (::stat(dir.c_str(), &info) != 0) {
        error = errno_text("cannot inspect the instance directory", errno);
        return false;
    }
    if (info.st_uid != ::getuid()) {
        error = "the instance directory is not owned by the current user";
        return false;
    }
    if ((info.st_mode & 0077) != 0) {
        error = "the instance directory is not private (expected mode 0700)";
        return false;
    }
    return true;
}

auto set_socket_timeout(int fd, int timeout_ms) -> void {
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)));
    static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)));
}

[[nodiscard]] auto read_full(int fd, void *data, std::size_t size) -> bool {
    auto *cursor = static_cast<std::uint8_t *>(data);
    std::size_t received = 0;
    while (received < size) {
        const ssize_t n = ::recv(fd, cursor + received, size - received, 0);
        if (n > 0) {
            received += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] auto write_full(int fd, const void *data, std::size_t size) -> bool {
    const auto *cursor = static_cast<const std::uint8_t *>(data);
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd, cursor + sent, size - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] auto read_frame(int fd, std::string &payload) -> bool {
    std::uint8_t header[kChannelLengthBytes] = {};
    if (!read_full(fd, header, sizeof(header))) {
        return false;
    }
    const std::uint32_t length = static_cast<std::uint32_t>(header[0]) |
                                 (static_cast<std::uint32_t>(header[1]) << 8U) |
                                 (static_cast<std::uint32_t>(header[2]) << 16U) |
                                 (static_cast<std::uint32_t>(header[3]) << 24U);
    if (length > kMaxInstanceFrameBytes) {
        return false;
    }
    payload.assign(length, '\0');
    return length == 0 || read_full(fd, payload.data(), length);
}

[[nodiscard]] auto write_frame(int fd, std::string_view payload) -> bool {
    if (payload.size() > kMaxInstanceFrameBytes) {
        return false;
    }
    const auto length = static_cast<std::uint32_t>(payload.size());
    const std::uint8_t header[kChannelLengthBytes] = {
        static_cast<std::uint8_t>(length & 0xFFU),
        static_cast<std::uint8_t>((length >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((length >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((length >> 24U) & 0xFFU),
    };
    return write_full(fd, header, sizeof(header)) &&
           (payload.empty() || write_full(fd, payload.data(), payload.size()));
}

/// @brief 监听端：transport 线程 + 生命周期收尾。
class PosixInstanceListener final : public InstanceListener {
  public:
    PosixInstanceListener(int listen_fd, int lock_fd, std::string path, InstanceHandler handler)
        : listen_fd_(listen_fd),
          lock_fd_(lock_fd),
          path_(std::move(path)),
          handler_(std::move(handler)),
          thread_([this]() -> void { run(); }) {}

    ~PosixInstanceListener() override {
        stop_.store(true, std::memory_order_relaxed);
        if (thread_.joinable()) {
            thread_.join();
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (!path_.empty()) {
            static_cast<void>(::unlink(path_.c_str()));
        }
        if (lock_fd_ >= 0) {
            ::close(lock_fd_);  // 释放 flock：下一进程可立刻认领。
            lock_fd_ = -1;
        }
    }

  private:
    auto run() -> void {
        while (!stop_.load(std::memory_order_relaxed)) {
            pollfd event{listen_fd_, POLLIN, 0};
            const int ready = ::poll(&event, 1, kAcceptPollMs);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return;
            }
            if (ready == 0) {
                continue;
            }
            if ((event.revents & POLLIN) == 0) {
                if ((event.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                    return;
                }
                continue;
            }
            const int connection = ::accept(listen_fd_, nullptr, nullptr);
            if (connection < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                return;
            }
            static_cast<void>(::fcntl(connection, F_SETFD, FD_CLOEXEC));
            serve(connection);
            ::close(connection);
        }
    }

    auto serve(int connection) -> void {
        set_socket_timeout(connection, kInstanceReplyTimeoutMs);
        std::string payload;
        if (!read_frame(connection, payload)) {
            return;
        }
        auto request = std::make_shared<InstanceRequest>(std::move(payload));
        if (handler_) {
            handler_(request);
        }
        const std::optional<std::string> reply = request->wait(kInstanceReplyTimeoutMs);
        if (!reply.has_value()) {
            return;  // 主线程未按时回执：关连接，转发端报「无回复」。
        }
        static_cast<void>(write_frame(connection, *reply));
    }

    int listen_fd_{-1};
    int lock_fd_{-1};
    std::string path_;
    InstanceHandler handler_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

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

auto instance_default_key() -> std::string {
    const std::string suffix = "borealis-" + std::to_string(::getuid()) + "/instance.sock";
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr && runtime[0] == '/') {
        const std::filesystem::path candidate =
            std::filesystem::path{runtime} / suffix;
        if (candidate.string().size() + 1 <= sizeof(sockaddr_un::sun_path)) {
            return candidate.string();
        }
    }
    return (std::filesystem::path{"/tmp"} / suffix).string();
}

auto instance_listen(const std::string &key, InstanceHandler handler) -> InstanceListenResult {
    if (key.empty()) {
        return {.role = InstanceRole::Failed, .listener = nullptr, .error = "instance key is empty"};
    }
    const std::filesystem::path socket_path{key};
    const std::filesystem::path directory = socket_path.parent_path();
    if (directory.empty()) {
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = "instance key has no parent directory"};
    }
    std::string error;
    if (!ensure_private_directory(directory, error)) {
        return {.role = InstanceRole::Failed, .listener = nullptr, .error = std::move(error)};
    }

    const std::filesystem::path lock_path = directory / "instance.lock";
    const int lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock_fd < 0) {
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = errno_text("cannot open the instance lock", errno)};
    }
    if (::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        const int code = errno;
        ::close(lock_fd);
        if (code == EWOULDBLOCK || code == EAGAIN) {
            return {.role = InstanceRole::Existing, .listener = nullptr, .error = {}};
        }
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = errno_text("cannot claim the instance lock", code)};
    }

    // 锁在手：上一进程即使崩在 bind 之后，残留的 socket 也由这里清掉。
    static_cast<void>(::unlink(key.c_str()));

    const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        const int code = errno;
        ::close(lock_fd);
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = errno_text("cannot create the instance socket", code)};
    }
    const int flags = ::fcntl(listen_fd, F_GETFL, 0);
    if (flags >= 0) {
        static_cast<void>(::fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK));
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (key.size() + 1 > sizeof(address.sun_path)) {
        ::close(listen_fd);
        ::close(lock_fd);
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = "instance socket path is too long"};
    }
    std::memcpy(address.sun_path, key.c_str(), key.size() + 1);
    if (::bind(listen_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        const int code = errno;
        ::close(listen_fd);
        ::close(lock_fd);
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = errno_text("cannot bind the instance socket", code)};
    }
    static_cast<void>(::chmod(key.c_str(), 0600));
    if (::listen(listen_fd, 16) != 0) {
        const int code = errno;
        static_cast<void>(::unlink(key.c_str()));
        ::close(listen_fd);
        ::close(lock_fd);
        return {.role = InstanceRole::Failed,
                .listener = nullptr,
                .error = errno_text("cannot listen on the instance socket", code)};
    }

    InstanceListenResult result;
    result.role = InstanceRole::Primary;
    result.listener =
        std::make_unique<PosixInstanceListener>(listen_fd, lock_fd, key, std::move(handler));
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

    const int connection = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (connection < 0) {
        result.error = errno_text("cannot create the client socket", errno);
        return result;
    }
    set_socket_timeout(connection, timeout_ms);

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (key.size() + 1 > sizeof(address.sun_path)) {
        result.error = "instance socket path is too long";
        ::close(connection);
        return result;
    }
    std::memcpy(address.sun_path, key.c_str(), key.size() + 1);
    if (::connect(connection, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        result.error = errno_text("cannot connect to the running instance", errno);
        ::close(connection);
        return result;
    }
    if (!write_frame(connection, frame)) {
        result.error = "cannot send the request to the running instance";
        ::close(connection);
        return result;
    }
    std::string payload;
    if (!read_frame(connection, payload)) {
        result.error = "no reply from the running instance";
        ::close(connection);
        return result;
    }
    ::close(connection);
    result.delivered = true;
    result.reply = std::move(payload);
    return result;
}

}  // namespace borealis::platform
