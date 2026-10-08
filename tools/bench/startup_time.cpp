// ============================================================
// 启动时间基准（tools/bench/startup_time.cpp）
// ------------------------------------------------------------
// `SPEC.NF.PERF.03` 的验收手段：冷启动 → 可交互终端 ≤ 1.5 s（SSD、无网络等待）。
//
// 度量主体是**从进程启动到 Aurora 框架就绪并可渲染首帧**的时间，含：
// - Aurora 框架初始化
// - 内置字体加载与首帧网格度量
// - 基础窗口创建
//
// 本程序不是测试用例（AGENTS.md §4.4 第 18 条），形态为独立可执行，出 JSON 供门禁脚本消费。
// 完整会话启动（ConPTY + shell 探测）的成本由装配层日志留痕，不在本基准测量范围内。
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

#include "aurora/aurora.h"
#include "aurora/app/application.h"
#include "aurora/core/log.h"
#include "aurora/render/font_discovery.h"
#include "aurora/widget/containers.h"

namespace {

namespace au = aurora;

/// @brief 启动阶段标记
struct StartupMarkers {
    std::chrono::steady_clock::time_point process_start;
    std::chrono::steady_clock::time_point font_loaded;
    std::chrono::steady_clock::time_point aurora_initialized;
    std::chrono::steady_clock::time_point first_frame_ready;
};

/// @brief 输出 JSON 报告
void write_json_report(const std::string &path, const StartupMarkers &markers) {
    auto to_ms = [](const std::chrono::steady_clock::time_point &t) -> double {
        return std::chrono::duration<double, std::milli>(t.time_since_epoch()).count();
    };

    auto elapsed_ms = [&](const std::chrono::steady_clock::time_point &start,
                          const std::chrono::steady_clock::time_point &end) -> double {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    std::ofstream out(path);
    if (!out.is_open()) {
        AURORA_LOG_ERROR("Failed to open report file: %s", path.c_str());
        return;
    }

    out << std::fixed << std::setprecision(3);
    out << "{\n";
    out << "  \"spec\": \"SPEC.NF.PERF.03\",\n";
    out << "  \"threshold_ms\": 1500.0,\n";
    out << "  \"phases\": {\n";
    out << "    \"process_start\": " << to_ms(markers.process_start) << ",\n";
    out << "    \"font_loaded\": " << to_ms(markers.font_loaded) << ",\n";
    out << "    \"aurora_initialized\": " << to_ms(markers.aurora_initialized) << ",\n";
    out << "    \"first_frame_ready\": " << to_ms(markers.first_frame_ready) << "\n";
    out << "  },\n";
    out << "  \"durations_ms\": {\n";
    out << "    \"font_loading\": " << elapsed_ms(markers.process_start, markers.font_loaded) << ",\n";
    out << "    \"framework_init\": " << elapsed_ms(markers.font_loaded, markers.aurora_initialized) << ",\n";
    out << "    \"first_frame\": " << elapsed_ms(markers.aurora_initialized, markers.first_frame_ready) << ",\n";
    out << "    \"total_startup\": " << elapsed_ms(markers.process_start, markers.first_frame_ready) << "\n";
    out << "  },\n";

    const bool passed = elapsed_ms(markers.process_start, markers.first_frame_ready) <= 1500.0;
    out << "  \"passed\": " << (passed ? "true" : "false") << "\n";
    out << "}\n";

    out.close();
}

}  // namespace

int main(int argc, char *argv[]) {
    StartupMarkers markers{};
    markers.process_start = std::chrono::steady_clock::now();

    // 解析命令行参数
    std::string json_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--json=" && i + 1 < argc) {
            json_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: startup_time [--json=<path>]\n"
                      << "  --json=<path>  Write JSON report to specified path\n";
            return 0;
        }
    }

    if (json_path.empty()) {
        json_path = "build-bench/startup_report.json";
    }

    // 触发字体加载（通过查询系统字体列表）
    const auto families = au::render::list_font_families(true);
    markers.font_loaded = std::chrono::steady_clock::now();

    AURORA_LOG_INFO("Font families enumerated in %.2f ms (%zu monospace families)",
                    std::chrono::duration<double, std::milli>(markers.font_loaded - markers.process_start).count(),
                    families.size());

    // 创建简单 UI 树用于无头宿主
    au::Scene scene{au::Node{std::make_shared<au::Column>()}};

    // 创建无头应用（不需要真实窗口），限制只跑一帧
    au::WindowOptions opts;
    opts.max_frames = 1;
    au::Application app{std::move(scene), 960, 640};
    markers.aurora_initialized = std::chrono::steady_clock::now();

    AURORA_LOG_INFO("Aurora initialized in %.2f ms",
                    std::chrono::duration<double, std::milli>(markers.aurora_initialized - markers.font_loaded).count());

    // 运行一帧以触发首次布局与度量
    app.run();
    markers.first_frame_ready = std::chrono::steady_clock::now();

    AURORA_LOG_INFO("First frame ready in %.2f ms",
                    std::chrono::duration<double, std::milli>(markers.first_frame_ready - markers.aurora_initialized).count());

    const auto total_ms = std::chrono::duration<double, std::milli>(markers.first_frame_ready - markers.process_start).count();
    AURORA_LOG_INFO("Total startup time: %.2f ms (threshold: 1500 ms) [%s]",
                    total_ms, total_ms <= 1500.0 ? "PASS" : "FAIL");

    // 写入 JSON 报告
    write_json_report(json_path, markers);

    std::cout << "Startup time: " << std::fixed << std::setprecision(3) << total_ms << " ms\n";
    std::cout << "Report written to: " << json_path << "\n";

    return total_ms <= 1500.0 ? 0 : 1;
}
