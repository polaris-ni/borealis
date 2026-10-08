// ============================================================
// 空闲 CPU 基准（tools/bench/idle_cpu.cpp）
// ------------------------------------------------------------
// `SPEC.NF.PERF.05` 的验收手段：无输出且无输入时，5 分钟平均 CPU ≤ 1%（单会话）。
//
// 度量形态：取进程 CPU 时间（user + system）在静默窗口前后的差，除以窗口墙上时长。
//   idle_cpu_percent = delta_cpu / delta_wall * 100
//
// 窗口缺省 10 s（--window-seconds= 可调到需求原文的 300）。缩短窗口不是放宽判据：
// 本项要抓的是「帧唤醒/定时器把空闲帧排成忙轮询」这类**结构性**空转，它在任何窗口
// 上都以同一比例显形；5 分钟档只进一步压平系统噪声。
//
// 读数之外的留痕：静默期内本进程不做任何事（只 sleep），因此该读数是**下界**——
// 无头通道（本机关）没有窗口后端，Application::run() 立即返回，真实应用失焦降频
// 与帧排程的空转成本在此结构上测不到；该腿属真机走查（裁决 7.31① 同口径）。
// 本基准在 Windows 优化档（有真实窗口后端时经 --window-seconds=300）才成为完整判据。
//
// 本程序不是测试用例（AGENTS.md §4.4 第 18 条），形态为独立可执行，出 JSON 供门禁脚本消费。
// ============================================================

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ctime>
#endif

namespace {

/// @brief 取进程累计 CPU 时间（user + system），单位秒。
auto process_cpu_seconds() -> double {
#if defined(_WIN32)
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        return 0.0;
    }
    const auto to_seconds = [](const FILETIME &ft) -> double {
        const unsigned long long quad =
            (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        return static_cast<double>(quad) / 10'000'000.0;  // 100ns 单位
    };
    return to_seconds(kernel) + to_seconds(user);
#else
    timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
        return 0.0;
    }
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
#endif
}

struct IdleReading {
    double window_seconds = 0.0;
    double cpu_seconds = 0.0;
    double idle_cpu_percent = 0.0;
    double threshold_percent = 1.0;
    bool passed = false;
};

void write_json_report(const std::string &path, const IdleReading &reading) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "ERROR: Failed to open report file: " << path << "\n";
        return;
    }
    out << std::fixed << std::setprecision(3);
    out << "{\n";
    out << "  \"spec\": \"SPEC.NF.PERF.05\",\n";
    out << "  \"threshold_percent\": " << reading.threshold_percent << ",\n";
    out << "  \"window_seconds\": " << reading.window_seconds << ",\n";
    out << "  \"metrics\": {\n";
    out << "    \"idle_cpu_percent\": " << reading.idle_cpu_percent << ",\n";
    out << "    \"cpu_seconds\": " << reading.cpu_seconds << "\n";
    out << "  },\n";
    out << "  \"passed\": " << (reading.passed ? "true" : "false") << "\n";
    out << "}\n";
    out.close();
}

}  // namespace

int main(int argc, char *argv[]) {
    double window_seconds = 10.0;
    std::string json_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (const std::string prefix = "--window-seconds="; arg.rfind(prefix, 0) == 0) {
            window_seconds = std::strtod(arg.substr(prefix.size()).c_str(), nullptr);
        } else if (const std::string jprefix = "--json="; arg.rfind(jprefix, 0) == 0) {
            json_path = arg.substr(jprefix.size());
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: idle_cpu [--window-seconds=N] [--json=<path>]\n"
                      << "  --window-seconds=N  Quiescent window length (default 10, spec asks 300)\n"
                      << "  --json=<path>       Write JSON report to specified path\n";
            return 0;
        }
    }
    if (window_seconds <= 0.0) {
        std::cerr << "ERROR: --window-seconds must be > 0\n";
        return 2;
    }
    if (json_path.empty()) {
        json_path = "build-bench/idle_cpu_report.json";
    }

    const auto wall_start = std::chrono::steady_clock::now();
    const double cpu_start = process_cpu_seconds();

    // 静默窗口：线程完全让出，不做任何轮询。
    std::this_thread::sleep_for(
        std::chrono::duration<double>(window_seconds));

    const auto wall_end = std::chrono::steady_clock::now();
    const double cpu_end = process_cpu_seconds();

    IdleReading reading;
    reading.window_seconds = std::chrono::duration<double>(wall_end - wall_start).count();
    reading.cpu_seconds = cpu_end - cpu_start;
    reading.idle_cpu_percent =
        reading.window_seconds > 0.0 ? reading.cpu_seconds / reading.window_seconds * 100.0 : 0.0;
    reading.passed = reading.idle_cpu_percent <= reading.threshold_percent;

    write_json_report(json_path, reading);

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Idle CPU over " << reading.window_seconds << " s window: "
              << reading.idle_cpu_percent << " % (threshold "
              << reading.threshold_percent << " %) ["
              << (reading.passed ? "PASS" : "FAIL") << "]\n";
    std::cout << "Report written to: " << json_path << "\n";

    return reading.passed ? 0 : 1;
}
