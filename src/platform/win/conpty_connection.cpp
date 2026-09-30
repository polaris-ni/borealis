// ============================================================
// Win32 ConPTY 连接实现（src/platform/win/conpty_connection.cpp）
// ------------------------------------------------------------
// 本文件是 `SPEC.FEAT.CONN.01` 在 Windows 侧的全部平台知识：伪终端创建、子进程挂载、
// 管道读写与关停。三处口径值得记录：
//   1. 交给伪终端的那两个管道端要在子进程建好后关为本进程的一份：本进程再持有输出管道的写端，
//      conhost 退场时管道不 EOF，读线程就退不出来，关停会卡在 join 上；
//   2. `ClosePseudoConsole` 只断控制台连接，不带走子进程，故关停由本类补一刀；
//   3. 子进程句柄到析构才关闭，好让 `alive()` 保持无锁（架构 §3.1 的每会话一读线程里，
//      UI 线程随时要读它）。
// ============================================================

#include "conpty_connection.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <vector>

#include "aurora/core/log.h"
#include "win_text.h"

namespace borealis::platform {

namespace {

/// @brief 单次读管道的块上限：读线程把块原样交给会话，不做二次切分。
constexpr DWORD kReadChunkBytes = 32768;

/// @brief 终止子进程后等它确实退场的上限。
constexpr DWORD kProcessExitWaitMs = 2000;

/// @brief `COORD` 的分量是 `SHORT`，超界会回绕成无意义尺寸。
constexpr std::size_t kMaxConsoleDimension = 32767;

/// @brief 会话尺寸转伪终端尺寸。
[[nodiscard]] auto make_size_coord(session::Size size) noexcept -> COORD {
    const auto to_short = [](std::size_t value) noexcept {
        return static_cast<SHORT>(std::clamp(value, std::size_t{1}, kMaxConsoleDimension));
    };
    return {to_short(size.columns), to_short(size.rows)};
}

/// @brief 取当前进程环境（每个条目形如 `KEY=VALUE`，驱动器工作目录的隐藏条目以 `=` 开头）。
[[nodiscard]] auto inherited_environment() -> std::vector<std::wstring> {
    auto entries = std::vector<std::wstring>{};
    auto *block = GetEnvironmentStringsW();
    if (block == nullptr) {
        return entries;
    }
    for (auto *cursor = block; *cursor != L'\0';) {
        const auto entry = std::wstring(cursor);
        if (!entry.empty()) {
            entries.push_back(entry);
        }
        cursor += entry.size() + 1U;
    }
    static_cast<void>(FreeEnvironmentStringsW(block));
    return entries;
}

/// @brief 覆盖同名变量：先删掉继承来的那份，再按新值追加。
auto apply_override(std::vector<std::wstring> &entries, const std::wstring &key,
                    const std::wstring &value) -> void {
    std::erase_if(entries, [&key](const std::wstring &entry) {
        const auto separator = entry.find(L'=');
        // 以 `=` 开头的条目是「驱动器 X 的当前目录」，不参与键覆盖。
        return separator != std::wstring::npos && separator != 0U &&
               _wcsicmp(entry.c_str(), (key + L'=').c_str()) == 0;
    });
    entries.push_back(key + L'=' + value);
}

/// @brief 拼出 `CreateProcessW` 要的环境块：继承值 → PTY 默认注入 → 规格覆盖，按键排序、双 NUL 收尾。
///
/// 排序是 API 的硬要求（文档：环境块须按键字母序），乱序会让子进程内的 `GetEnvironmentVariable`
/// 二分查找失配。
[[nodiscard]] auto build_environment_block(const conn::LocalTerminalSpec &spec)
    -> std::vector<wchar_t> {
    auto entries = inherited_environment();
    for (const auto &[key, value] : conn::kPtyDefaultEnvironment) {
        apply_override(entries, to_wide(key), to_wide(value));
    }
    for (const auto &[key, value] : spec.environment) {
        apply_override(entries, to_wide(key), to_wide(value));
    }
    std::sort(entries.begin(), entries.end(), [](const std::wstring &left, const std::wstring &right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });

    auto block = std::vector<wchar_t>{};
    for (const auto &entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

}  // namespace

ConptyConnection::ConptyConnection(conn::LocalTerminalSpec spec, session::Size initial_size)
    : spec_{std::move(spec)}, size_{initial_size} {}

ConptyConnection::~ConptyConnection() {
    close();
    // 句柄集中在析构回收：`alive()` 无锁读子进程句柄，提前关闭会让它等在一个已失效的内核对象上。
    for (const auto handle :
         std::array<HANDLE, 3>{input_write_, output_read_, process_.load()}) {
        if (handle != nullptr) {
            static_cast<void>(CloseHandle(handle));
        }
    }
}

auto ConptyConnection::start(session::ConnectionEvents &events) -> void {
    if (started_.load() || closing_.load()) {
        return;
    }
    started_ = spawn(events);
    if (!started_.load()) {
        // 起不来时也必须让会话收到流结束，否则标签页会停在空白屏且无法关闭。
        events.on_closed();
    }
}

auto ConptyConnection::spawn(session::ConnectionEvents &events) -> bool {
    const auto command_line =
        spec_.command_line.empty() ? conn::default_shell_command_line() : spec_.command_line;
    if (command_line.empty()) {
        AURORA_LOG_ERROR("platform", "no local terminal command line available");
        return false;
    }
    auto mutable_command = to_wide(command_line);
    auto mutable_working_directory = to_wide(spec_.working_directory);
    auto environment = build_environment_block(spec_);

    auto input_read = HANDLE{nullptr};
    auto input_write = HANDLE{nullptr};
    auto output_read = HANDLE{nullptr};
    auto output_write = HANDLE{nullptr};
    auto console = HPCON{nullptr};
    auto process_info = PROCESS_INFORMATION{};
    auto process_created = false;

    {
        // 四端都不需要被子进程继承：伪终端自己复制走输入/输出那两端，子进程的控制台由属性表交出去，
        // 故按官方样例以 NULL 安全属性建管道即可。
        auto pipes_created = CreatePipe(&input_read, &input_write, nullptr, 0) != FALSE;
        pipes_created = CreatePipe(&output_read, &output_write, nullptr, 0) != FALSE && pipes_created;

        const auto console_result =
            pipes_created
                ? CreatePseudoConsole(make_size_coord(size_), input_read, output_write, 0U, &console)
                : HRESULT{E_FAIL};

        if (SUCCEEDED(console_result)) {
            auto attribute_size = SIZE_T{0};
            // 首次调用只为问出属性表大小，返回 FALSE 是预期路径。
            static_cast<void>(
                InitializeProcThreadAttributeList(nullptr, 1, 0U, &attribute_size));
            auto attribute_storage = std::vector<char>(static_cast<std::size_t>(attribute_size));
            auto *attributes =
                reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
            auto attributes_ready =
                InitializeProcThreadAttributeList(attributes, 1, 0U, &attribute_size) != FALSE;
            if (attributes_ready) {
                // lpValue 传 HPCON **本身**而非 &console：官方文档没列这个属性，但 HPCON 指向的结构
                // 首个字段就是系统要读的句柄（winconpty.h 注明该结构是与 OS 共享的 ABI），
                // Windows Terminal 生产代码同样传 _hPC.get() 配 sizeof(HPCON)。
                attributes_ready =
                    UpdateProcThreadAttribute(attributes, 0U, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                              console, sizeof(console), nullptr, nullptr) != FALSE;
            }
            if (attributes_ready) {
                auto startup = STARTUPINFOEXW{};
                startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
                startup.lpAttributeList = attributes;
                // 三个标准句柄显式置空并声明「用我给的这套」：不给的话 CreateProcess 会把**本进程**的
                // 标准句柄拷给子进程，而本进程的标准句柄可能是重定向文件（经脚本/日志启动时就是这样），
                // 子进程就往那个文件里写，伪终端管道一个字节都收不到。置空后子进程按自己的控制台
                // （即伪终端）打开 CONIN$/CONOUT$，与 Windows Terminal 这种无标准句柄的宿主同形态。
                startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                startup.StartupInfo.hStdInput = nullptr;
                startup.StartupInfo.hStdOutput = nullptr;
                startup.StartupInfo.hStdError = nullptr;
                // 句柄经属性表交出去，故 bInheritHandles 传 FALSE。
                // CREATE_UNICODE_ENVIRONMENT 不可省：给了宽字符环境块却不带这个标志，
                // CreateProcessW 对**任何**非空 lpEnvironment 都返回 ERROR_INVALID_PARAMETER(87)，
                // 连原样拷贝的 GetEnvironmentStringsW() 也一样（本机探针实测）。
                process_created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr,
                                                 FALSE,
                                                 EXTENDED_STARTUPINFO_PRESENT |
                                                     CREATE_UNICODE_ENVIRONMENT,
                                                 environment.data(),
                                                 mutable_working_directory.empty()
                                                     ? nullptr
                                                     : mutable_working_directory.data(),
                                                 &startup.StartupInfo, &process_info) != FALSE;
                static_cast<void>(DeleteProcThreadAttributeList(attributes));
            }
        }

        // 交给伪终端的那两端在子进程建好后就必须关掉（官方文档明确列出这一点）：本进程再持有它们，
        // 输出管道的写端就永不归零，conhost 退场后 ReadFile 仍不返回 BROKEN_PIPE，关停会卡在 join 上。
        static_cast<void>(CloseHandle(input_read));
        static_cast<void>(CloseHandle(output_write));
    }

    if (!process_created) {
        AURORA_LOG_ERROR("platform", "ConPTY session launch failed: ", GetLastError());
        for (const auto handle : std::array<HANDLE, 2>{input_write, output_read}) {
            if (handle != nullptr) {
                static_cast<void>(CloseHandle(handle));
            }
        }
        if (console != nullptr) {
            ClosePseudoConsole(console);
        }
        return false;
    }

    static_cast<void>(CloseHandle(process_info.hThread));  // 主线程句柄无用，只留进程句柄
    console_ = console;
    input_write_ = input_write;
    output_read_ = output_read;
    events_ = &events;
    process_ = process_info.hProcess;
    reader_ = std::thread([this] { read_loop(); });
    return true;
}

auto ConptyConnection::read_loop() -> void {
    auto buffer = std::vector<std::byte>(static_cast<std::size_t>(kReadChunkBytes));
    for (;;) {
        auto read = DWORD{0};
        if (ReadFile(output_read_, buffer.data(), kReadChunkBytes, &read, nullptr) == FALSE) {
            // 子进程退出后 conhost 关掉写端，管道以 BROKEN_PIPE 收尾；其余错误同样按流结束处理。
            break;
        }
        if (read == 0U) {
            continue;
        }
        events_->on_bytes({buffer.data(), static_cast<std::size_t>(read)});
    }
    events_->on_closed();
}

auto ConptyConnection::write(std::span<const std::byte> bytes) -> void {
    const std::lock_guard lock{write_mutex_};
    if (closing_.load() || input_write_ == nullptr) {
        return;
    }
    auto remaining = bytes;
    while (!remaining.empty()) {
        auto written = DWORD{0};
        const auto ok = WriteFile(input_write_, remaining.data(),
                                  static_cast<DWORD>(remaining.size()), &written, nullptr);
        if (ok == FALSE || written == 0U) {
            return;  // 子进程已退场，按键丢弃
        }
        remaining = remaining.subspan(static_cast<std::size_t>(written));
    }
}

auto ConptyConnection::resize(session::Size size) -> void {
    const std::lock_guard lock{state_mutex_};
    if (closing_.load() || console_ == nullptr) {
        return;
    }
    size_ = size;
    if (const auto result = ResizePseudoConsole(console_, make_size_coord(size));
        result != S_OK) {
        AURORA_LOG_WARN("platform", "ResizePseudoConsole failed: ", static_cast<long>(result));
    }
}

auto ConptyConnection::close() -> void {
    if (closing_.exchange(true)) {
        // 已有关停者在前：约定是返回后不再回调，故等读线程退场即可。
        if (reader_.joinable()) {
            reader_.join();
        }
        return;
    }
    {
        const std::lock_guard lock{state_mutex_};
        if (console_ != nullptr) {
            ClosePseudoConsole(console_);
            console_ = nullptr;  // 读管道随之 EOF，读线程把剩余缓冲交给会话后退出
        }
    }
    if (reader_.joinable()) {
        reader_.join();
    }
    // ClosePseudoConsole 只断控制台连接，不带走子进程（与 Windows Terminal 的关闭同口径：关标签即终结）。
    if (auto process = process_.load();
        process != nullptr && WaitForSingleObject(process, 0U) == WAIT_TIMEOUT) {
        static_cast<void>(TerminateProcess(process, 0U));
        static_cast<void>(WaitForSingleObject(process, kProcessExitWaitMs));
    }
}

auto ConptyConnection::alive() const noexcept -> bool {
    if (closing_.load()) {
        return false;
    }
    const auto process = process_.load();
    return process != nullptr && WaitForSingleObject(process, 0U) == WAIT_TIMEOUT;
}

}  // namespace borealis::platform
