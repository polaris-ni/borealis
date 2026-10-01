# ============================================================
# Borealis 源码
# ------------------------------------------------------------
# 纯逻辑模块独立成静态库：应用与测试 runner 都要链接它，且它不依赖 UI
# （AGENTS.md §4.4 第 20 条：纯逻辑层必须可独立单测）。
# ============================================================

add_library(borealis_core STATIC
        config/settings.cpp
        config/store.cpp
        config/themes.cpp
        vt/parser.cpp
        term/utf8.cpp
        term/charset.cpp
        term/osc.cpp
        term/width.cpp
        term/terminal.cpp
        grid/storage.cpp
        session/clipboard_outbox.cpp
        session/damage_queue.cpp
        session/session.cpp
        ui/cell_layout.cpp
        ui/palette.cpp)

# 平台层实现按目标平台编入：共享路径只见接口，ConPTY 句柄与 Win32 API 留在 src/platform/ 内
# （裁决 7.11、架构 §2.2）。posix 侧等价实现待建，见 codespec/PLAN.md §8。
if (WIN32)
    target_sources(borealis_core PRIVATE
            platform/win/conpty_connection.cpp
            platform/win/local_terminal.cpp
            platform/win/win_text.cpp)
endif ()

# 消费者统一配置（链接 aurora + C++20 + 共享 PCH + 统一告警），额外私有头目录为本仓公共头根。
aurora_setup_consumer_target(borealis_core "${CMAKE_CURRENT_SOURCE_DIR}/../include")

add_executable(borealis main.cpp)

aurora_setup_consumer_target(borealis "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(borealis PRIVATE borealis_core)

# 产物名 Borealis（裁决 7.14：仓内可自主命名的标识统一 borealis，产物首字母大写）。
set_target_properties(borealis PROPERTIES OUTPUT_NAME "Borealis")
