// ============================================================
// Windows 本机 ssh-agent 字节通道（src/platform/win/agent_socket.cpp）
// ------------------------------------------------------------
// platform::agent_connect 的 Win32 腿（SPEC.FEAT.CONN.02 的 agent 转发）。
// OpenSSH for Windows 把 agent 暴露为命名管道 `\\.\pipe\openssh-ssh-agent`
// （或 `SSH_AUTH_SOCK` 指向的管道），故本腿只认命名管道形态。
//
// **本沙箱无 MSVC，本文件未经编译验证**（推断/未验证）：接口与 POSIX 腿同构，
// 读写走 PeekNamedPipe/ReadFile/WriteFile；带超时读在命名管道上退化为「看一眼
// 有没有货」，与 POSIX 腿的 poll(0) 语义一致。
// ============================================================

#include "platform/agent_socket.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <iterator>

#include "aurora/core/log.h"

namespace borealis::platform {

namespace {

/// @brief OpenSSH for Windows 的 agent 命名管道默认名。
constexpr const wchar_t *kDefaultAgentPipe = L"\\\\.\\pipe\\openssh-ssh-agent";

/// @brief RAII 句柄：INVALID_HANDLE_VALUE 表示未持有。
class PipeHandle {
  public:
    explicit PipeHandle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
    ~PipeHandle() { reset(); }

    PipeHandle(const PipeHandle &) = delete;
    auto operator=(const PipeHandle &) -> PipeHandle & = delete;
    PipeHandle(PipeHandle &&other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    auto operator=(PipeHandle &&) -> PipeHandle & = delete;

    [[nodiscard]] auto get() const -> HANDLE { return handle_; }
    [[nodiscard]] auto valid() const -> bool { return handle_ != INVALID_HANDLE_VALUE; }
    auto reset(HANDLE handle = INVALID_HANDLE_VALUE) -> void {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = handle;
    }

  private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class Win32AgentSocket final : public AgentSocket {
  public:
    explicit Win32AgentSocket(PipeHandle pipe) : pipe_(std::move(pipe)) {}

    auto read(std::span<std::uint8_t> out, int /*timeout_ms*/) -> AgentRead override {
        if (!pipe_.valid() || out.empty()) {
            return {AgentReadOutcome::Timeout, 0};
        }
        DWORD available = 0;
        if (::PeekNamedPipe(pipe_.get(), nullptr, 0, nullptr, &available, nullptr) == FALSE) {
            return {AgentReadOutcome::Closed, 0};
        }
        if (available == 0) {
            return {AgentReadOutcome::Timeout, 0};
        }
        DWORD got = 0;
        const auto want = static_cast<DWORD>(std::min<std::size_t>(available, out.size()));
        if (::ReadFile(pipe_.get(), out.data(), want, &got, nullptr) == FALSE) {
            AURORA_LOG_WARN("platform", "agent pipe read failed: ", ::GetLastError());
            return {AgentReadOutcome::Error, 0};
        }
        if (got == 0) {
            return {AgentReadOutcome::Closed, 0};
        }
        return {AgentReadOutcome::Data, static_cast<std::size_t>(got)};
    }

    auto write(std::span<const std::uint8_t> bytes) -> bool override {
        if (!pipe_.valid()) {
            return false;
        }
        auto remaining = bytes;
        while (!remaining.empty()) {
            DWORD sent = 0;
            const auto want = static_cast<DWORD>(std::min<std::size_t>(remaining.size(), 0xFFFFFFFFU));
            if (::WriteFile(pipe_.get(), remaining.data(), want, &sent, nullptr) == FALSE) {
                AURORA_LOG_WARN("platform", "agent pipe write failed: ", ::GetLastError());
                return false;
            }
            remaining = remaining.subspan(static_cast<std::size_t>(sent));
        }
        return true;
    }

    auto close() -> void override { pipe_.reset(); }

  private:
    PipeHandle pipe_;
};

}  // namespace

auto agent_connect() -> std::unique_ptr<AgentSocket> {
    const char *env = std::getenv("SSH_AUTH_SOCK");
    wchar_t path[512]{};
    if (env != nullptr && *env != '\0') {
        const auto converted = ::MultiByteToWideChar(CP_UTF8, 0, env, -1, path,
                                                     static_cast<int>(std::size(path)));
        if (converted == 0) {
            AURORA_LOG_WARN("platform", "SSH_AUTH_SOCK is not valid UTF-8");
            return nullptr;
        }
    } else {
        std::wmemcpy(path, kDefaultAgentPipe, std::wcslen(kDefaultAgentPipe) + 1);
    }
    PipeHandle pipe{::CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  0, nullptr)};
    if (!pipe.valid()) {
        AURORA_LOG_WARN("platform", "cannot open agent pipe: ", ::GetLastError());
        return nullptr;
    }
    return std::make_unique<Win32AgentSocket>(std::move(pipe));
}

}  // namespace borealis::platform
