cmake_minimum_required(VERSION 3.20)

# 三方依赖经 vcpkg（AGENTS.md §1「三方依赖走 vcpkg，不复制源码」）：工具链须在 project()
# 之前生效，故只能在这里按 VCPKG_ROOT 接。未设 VCPKG_ROOT 时按无 vcpkg 构建，届时
# find_package(libssh) 会失败——那是 SSH 腿的硬依赖，其余腿不受影响。
if (DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain")
endif ()

project(borealis VERSION 0.1.0 LANGUAGES CXX)

# ============================================================
# Borealis 顶层构建
# ------------------------------------------------------------
# 消费者应用，经 add_subdirectory 消费 Aurora 源码树（裁决 7.12：不复制框架源码进本仓）。
# ============================================================

set(BOREALIS_AURORA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../aurora" CACHE PATH "Aurora 源码树路径")

# Aurora 把自身的测试 / demo / 排版 / lint / 文档目标一并定义为默认 ON，作为子项目接入时
# 会把这几百个目标灌进本仓构建面，故在 add_subdirectory 之前以缓存变量覆盖为 OFF
# （option() 只认缓存变量的首次值，必须在目录被处理前写好）。
set(AURORA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(AURORA_BUILD_E2E OFF CACHE BOOL "" FORCE)
set(AURORA_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CLANG_FORMAT OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CLANG_TIDY OFF CACHE BOOL "" FORCE)
set(AURORA_BUILD_DOCS OFF CACHE BOOL "" FORCE)

add_subdirectory("${BOREALIS_AURORA_DIR}" "${CMAKE_BINARY_DIR}/aurora")

# 源码为 UTF-8（含中文注释），MSVC 在 CJK 代码页下须 /utf-8 解析，否则伪错误（与 Aurora 同口径）。
if (MSVC)
    add_compile_options(/utf-8)
endif ()

# Windows 头把 min/max 定义为宏，会撞 std::min/max。Aurora 顶层同样全局定义，但其
# add_compile_definitions 只作用于 Aurora 目录及其子目录，本仓须自行覆盖。
if (WIN32)
    add_compile_definitions(NOMINMAX)
endif ()

add_subdirectory(src)

option(BOREALIS_BUILD_BENCH "Build the render throughput benchmark (tools/bench)" ON)
if (BOREALIS_BUILD_BENCH)
    add_subdirectory(tools)
endif ()

option(BOREALIS_BUILD_TESTS "Build Borealis tests" ON)
if (BOREALIS_BUILD_TESTS)
    include(cmake/BorealisTests.cmake)
endif ()
