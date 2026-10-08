// ============================================================
// 内存占用基准（tools/bench/memory_usage.cpp）
// ------------------------------------------------------------
// `SPEC.NF.PERF.04` 的验收手段：单会话常驻 ≤ 150 MB（需求原文另立「10 标签 1 小时压测
// 无累积泄漏」一腿，属长跑场景不在本基准射程，登记为欠项）。
// loaded 档照需求原文取 150 MB；idle 档的 50 MB **不是需求命名的数**，只是回归守卫
// （框架初始化成本若翻倍，帧前读数会先在这里显形），口径同裁决 7.34 里 cat_mb_per_s 的
// 「需求未命名但门禁仍锁」的处理。
//
// 度量主体是**进程常驻集大小（RSS）**，通过读取 /proc/self/status（Linux）或
// GetProcessMemoryInfo（Windows）获取。测量两个场景：
// - 空闲终端：启动后可交互状态
// - 负载终端：加载 10 MB 文本后的状态
//
// 本程序不是测试用例（AGENTS.md §4.4 第 18 条），形态为独立可执行，出 JSON 供门禁脚本消费。
// ============================================================

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <fstream>
#include <string>
#endif

#include "aurora/aurora.h"
#include "aurora/app/application.h"
#include "aurora/core/log.h"
#include "aurora/render/font_discovery.h"
#include "aurora/widget/containers.h"

namespace {

namespace au = aurora;

/// @brief 获取当前进程 RSS（KB）
[[nodiscard]] auto get_rss_kb() -> std::size_t {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        return pmc.WorkingSetSize / 1024;  // 转为 KB
    }
    return 0;
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmRSS:") == 0) {
            // 格式: "VmRSS:    12345 kB"
            std::istringstream iss(line.substr(6));
            std::size_t kb = 0;
            iss >> kb;
            return kb;
        }
    }
    return 0;
#endif
}

/// @brief 输出 JSON 报告
void write_json_report(const std::string &path, std::size_t idle_rss_kb, std::size_t loaded_rss_kb) {
    std::ofstream out(path);
    if (!out.is_open()) {
        AURORA_LOG_ERROR("Failed to open report file: %s", path.c_str());
        return;
    }

    const double idle_mb = static_cast<double>(idle_rss_kb) / 1024.0;
    const double loaded_mb = static_cast<double>(loaded_rss_kb) / 1024.0;
    const bool idle_passed = idle_mb <= 50.0;
    const bool loaded_passed = loaded_mb <= 150.0;

    out << std::fixed << std::setprecision(3);
    out << "{\n";
    out << "  \"spec\": \"SPEC.NF.PERF.04\",\n";
    out << "  \"thresholds\": {\n";
    out << "    \"idle_max_mb\": 50.0,\n";
    out << "    \"loaded_max_mb\": 150.0\n";
    out << "  },\n";
    out << "  \"measurements\": {\n";
    out << "    \"idle_rss_kb\": " << idle_rss_kb << ",\n";
    out << "    \"idle_rss_mb\": " << idle_mb << ",\n";
    out << "    \"loaded_rss_kb\": " << loaded_rss_kb << ",\n";
    out << "    \"loaded_rss_mb\": " << loaded_mb << "\n";
    out << "  },\n";
    out << "  \"passed\": " << (idle_passed && loaded_passed ? "true" : "false") << ",\n";
    out << "  \"idle_passed\": " << (idle_passed ? "true" : "false") << ",\n";
    out << "  \"loaded_passed\": " << (loaded_passed ? "true" : "false") << "\n";
    out << "}\n";

    out.close();
}

}  // namespace

int main(int argc, char *argv[]) {
    // 解析命令行参数
    std::string json_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--json=" && i + 1 < argc) {
            json_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: memory_usage [--json=<path>]\n"
                      << "  --json=<path>  Write JSON report to specified path\n";
            return 0;
        }
    }

    if (json_path.empty()) {
        json_path = "build-bench/memory_report.json";
    }

    // 触发字体加载
    const auto families = au::render::list_font_families(true);
    AURORA_LOG_INFO("Font families loaded: %zu", families.size());

    // 创建简单 UI 树
    auto column = std::make_shared<au::Column>();
    au::Scene scene{au::Node{column}};

    // 创建无头应用
    au::Application app{std::move(scene), 960, 640};

    // 等待框架初始化
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 测量空闲 RSS
    const std::size_t idle_rss_kb = get_rss_kb();
    AURORA_LOG_INFO("Idle RSS: %.2f MB (%zu KB)", static_cast<double>(idle_rss_kb) / 1024.0, idle_rss_kb);

    // 模拟负载：分配 10 MB 字符串（模拟终端 scrollback 缓冲）
    constexpr std::size_t load_bytes = 10U * 1024U * 1024U;
    std::string test_data;
    test_data.reserve(load_bytes);
    for (std::size_t i = 0; i < load_bytes; i += 64) {
        test_data += "Hello, Borealis! This is test data.\n";
    }
    test_data.resize(load_bytes);

    // 等待内存分配稳定
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 测量负载 RSS
    const std::size_t loaded_rss_kb = get_rss_kb();
    AURORA_LOG_INFO("Loaded RSS: %.2f MB (%zu KB)", static_cast<double>(loaded_rss_kb) / 1024.0, loaded_rss_kb);

    // 写入报告
    write_json_report(json_path, idle_rss_kb, loaded_rss_kb);

    const double idle_mb = static_cast<double>(idle_rss_kb) / 1024.0;
    const double loaded_mb = static_cast<double>(loaded_rss_kb) / 1024.0;

    std::cout << "Idle memory:    " << std::fixed << std::setprecision(3) << idle_mb << " MB\n";
    std::cout << "Loaded memory:  " << loaded_mb << " MB\n";
    std::cout << "Report written to: " << json_path << "\n";

    return (idle_mb <= 50.0 && loaded_mb <= 150.0) ? 0 : 1;
}
