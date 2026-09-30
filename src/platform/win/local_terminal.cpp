// ============================================================
// Win32 本地终端工厂（src/platform/win/local_terminal.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.01` 的两条 Windows 腿：默认 shell 探测与连接工厂。
//
// 探测顺序 PowerShell → cmd → WSL 沿用 Windows Terminal 的默认 profile 次序：新版 PowerShell
// 优先（`pwsh` 在 PATH 或 ProgramFiles 下），退回内置 Windows PowerShell，再退回 cmd，
// 三者都不存在时才用 WSL 作为最后的可用 shell。探测只做存在性判断，不启动进程，
// 因此可以在 UI 线程上随时调用而不引入延迟。
// ============================================================

#include "borealis/conn/local_terminal.h"

#include <windows.h>

#include <string>
#include <vector>

#include "conpty_connection.h"
#include "win_text.h"

namespace borealis::conn {

namespace {

/// @brief 普通文件存在性判断（目录与不存在一律为假）。
[[nodiscard]] auto file_exists(const std::wstring &path) -> bool {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

/// @brief 读环境变量的值。
[[nodiscard]] auto environment_value(const wchar_t *name) -> std::wstring {
    auto buffer = std::vector<wchar_t>(MAX_PATH);
    for (;;) {
        const auto length = GetEnvironmentVariableW(name, buffer.data(),
                                                    static_cast<DWORD>(buffer.size()));
        if (length == 0U) {
            return {};  // 变量不存在或为空
        }
        if (length < buffer.size()) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(static_cast<std::size_t>(length) + 1U);
    }
}

/// @brief 在 PATH 中查找可执行文件的完整路径。
[[nodiscard]] auto search_path(const wchar_t *file_name) -> std::wstring {
    auto buffer = std::vector<wchar_t>(MAX_PATH);
    for (;;) {
        const auto length =
            SearchPathW(nullptr, file_name, nullptr, static_cast<DWORD>(buffer.size()),
                        buffer.data(), nullptr);
        if (length == 0U) {
            return {};
        }
        if (length < buffer.size()) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(static_cast<std::size_t>(length) + 1U);
    }
}

/// @brief 拼 `<目录>\<文件>`；目录为空时返回空串。
[[nodiscard]] auto join(const std::wstring &directory, const wchar_t *file_name) -> std::wstring {
    if (directory.empty()) {
        return {};
    }
    auto path = directory;
    if (path.back() != L'\\') {
        path.push_back(L'\\');
    }
    path.append(file_name);
    return path;
}

/// @brief 按 `CreateProcessW` 的命令行语义加引号（路径含空格时整段加引号）。
[[nodiscard]] auto quote_if_needed(const std::wstring &path) -> std::wstring {
    if (path.find(L' ') == std::wstring::npos) {
        return path;
    }
    return L'"' + path + L'"';
}

/// @brief 取第一个存在的路径。
[[nodiscard]] auto first_existing(const std::vector<std::wstring> &candidates) -> std::wstring {
    for (const auto &candidate : candidates) {
        if (!candidate.empty() && file_exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace

auto default_shell_command_line() -> std::string {
    const auto system_root = environment_value(L"SystemRoot");
    const auto program_files = environment_value(L"ProgramFiles");
    const auto program_files_x86 = environment_value(L"ProgramFiles(x86)");

    // PowerShell：pwsh（7+，含 preview 通道）优先，其次内置 Windows PowerShell。
    auto powershell = std::vector<std::wstring>{search_path(L"pwsh.exe")};
    for (const auto &directory : {program_files, program_files_x86}) {
        powershell.push_back(join(directory, L"PowerShell\\7\\pwsh.exe"));
        powershell.push_back(join(directory, L"PowerShell\\7-preview\\pwsh.exe"));
    }
    powershell.push_back(join(system_root, L"System32\\WindowsPowerShell\\v1.0\\powershell.exe"));
    if (const auto shell = first_existing(powershell); !shell.empty()) {
        return platform::to_utf8(quote_if_needed(shell));
    }

    // cmd：优先 %ComSpec%，退回 System32 下的那份。
    auto command_prompt = std::vector<std::wstring>{environment_value(L"ComSpec")};
    command_prompt.push_back(join(system_root, L"System32\\cmd.exe"));
    if (const auto shell = first_existing(command_prompt); !shell.empty()) {
        return platform::to_utf8(quote_if_needed(shell));
    }

    // WSL：末位兜底，前面三者都不在的机器上仍有可用的 shell。
    if (const auto wsl = join(system_root, L"System32\\wsl.exe"); file_exists(wsl)) {
        return platform::to_utf8(quote_if_needed(wsl));
    }
    return {};
}

auto make_local_terminal_connection(const LocalTerminalSpec &spec, session::Size initial_size)
    -> std::unique_ptr<session::Connection> {
    return std::make_unique<platform::ConptyConnection>(spec, initial_size);
}

}  // namespace borealis::conn
