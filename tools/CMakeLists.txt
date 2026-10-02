# ============================================================
# Borealis 工具目标（tools/CMakeLists.txt）
# ------------------------------------------------------------
# 基准与门禁脚本的挂靠处。基准是**非测试**可执行文件：它要读命令行、要出 JSON，而测试框架禁
# 自定义 `main()`（AGENTS.md §4.4 第 18 条），故不并入 borealis_test_runner，也不注册成 CTest
# 项——时间门禁由 tools/check/check_perf_gates.ps1 在 CI 里判红绿（口径见裁决 7.23③）。
# ============================================================

add_executable(borealis_bench bench/render_throughput.cpp)

aurora_setup_consumer_target(borealis_bench "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(borealis_bench PRIVATE borealis_core)
