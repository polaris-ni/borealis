# ============================================================
# BorealisTests.cmake — 注册式测试 runner（CTest）
# ------------------------------------------------------------
# 复刻 Aurora 测试体系（AGENTS.md §4.4 第 18 条）：
#   - tests/framework 下的框架源 + tests/{unit,integration,e2e} 下的用例源链入单一可执行
#     borealis_test_runner，全量构建只链接一次；
#   - 用例由框架静态注册，main 由框架唯一提供（tests/framework/test_main.cpp），
#     测试文件禁止自定义 main()；
#   - CTest 粒度：每条 add_test = runner --run=<stem>（文件级，进程隔离）。
# ============================================================

enable_testing()

option(BOREALIS_BUILD_E2E "Build end-to-end tests that need a real window backend" ON)

file(GLOB BOREALIS_TEST_FRAMEWORK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/framework/*.cpp")
file(GLOB BOREALIS_TEST_CASE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/*.cpp")
# 平台专属的探测腿套件只编进自己那一侧：`utest_posix_local_terminal` 取的是 `access()`/`setenv()`，
# Windows 上没有这两个入口；反过来同理。按文件名过滤而不是在源里 `#ifdef` 掏空用例——后者会让
# runner 拿到一条「no test case matched」的红灯（注册式框架按文件 stem 匹配，空套件等于失败）。
if (WIN32)
    list(FILTER BOREALIS_TEST_CASE_SOURCES EXCLUDE REGEX "/utest_posix_")
else ()
    list(FILTER BOREALIS_TEST_CASE_SOURCES EXCLUDE REGEX "/utest_win_")
endif ()
if (BOREALIS_BUILD_E2E)
    file(GLOB BOREALIS_TEST_E2E_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/*.cpp")
    list(APPEND BOREALIS_TEST_CASE_SOURCES ${BOREALIS_TEST_E2E_SOURCES})
endif ()

add_executable(borealis_test_runner
        ${BOREALIS_TEST_FRAMEWORK_SOURCES}
        ${BOREALIS_TEST_CASE_SOURCES})

# tests/ 供框架头按 "framework/xxx.h" 解析；include/ 供用例包含本仓公共头；
# Aurora 的 tools/include 供框架解析 known_enums.h（该头是 Aurora 的枚举 SSOT，属其内部目录，
# 复刻框架时直接使用而非复制副本，避免测到副本而非 SSOT）。
aurora_setup_consumer_target(borealis_test_runner
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${BOREALIS_AURORA_DIR}/tools/include")

# 纯逻辑模块：用例直接链接被测实现，不经应用可执行文件（§4.4 第 20 条）。
target_link_libraries(borealis_test_runner PRIVATE borealis_core)

foreach (tst ${BOREALIS_TEST_CASE_SOURCES})
    get_filename_component(tname ${tst} NAME_WE)
    if (tname MATCHES "^etest_")
        # 真实窗口事件循环挂起时 runner 默认不设限，须显式设限；标签供 ctest -L e2e 独立编排。
        add_test(NAME ${tname} COMMAND borealis_test_runner --run=${tname}
                --timeout=120000)
        set_tests_properties(${tname} PROPERTIES LABELS "e2e")
    else ()
        add_test(NAME ${tname} COMMAND borealis_test_runner --run=${tname})
    endif ()
endforeach ()

# 框架自检：断言宏、注册器与 runner 自身的用例（无需任何业务用例即可验证测试链路）。
add_test(NAME framework_selftest COMMAND borealis_test_runner --selftest)
