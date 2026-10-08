# ============================================================
# Borealis 源码
# ------------------------------------------------------------
# 纯逻辑模块独立成静态库：应用与测试 runner 都要链接它，且它不依赖 UI
# （AGENTS.md §4.4 第 20 条：纯逻辑层必须可独立单测）。
# ============================================================

add_library(borealis_core STATIC
        config/form_transfer.cpp
        config/settings.cpp
        config/store.cpp
        config/themes.cpp
        vt/parser.cpp
        term/utf8.cpp
        term/charset.cpp
        term/keymap.cpp
        term/mouse.cpp
        term/osc.cpp
        term/paste.cpp
        term/width.cpp
        term/terminal.cpp
        grid/storage.cpp
        session/clipboard_outbox.cpp
        session/damage_queue.cpp
        session/screen_mirror.cpp
        session/session.cpp
        ui/cell_layout.cpp
        ui/closed_tab_stack.cpp
        ui/color_text.cpp
        ui/debug_panel.cpp
        ui/font_choice.cpp
        ui/grid_size_debounce.cpp
        ui/palette.cpp
        ui/pane_tree.cpp
        ui/right_click.cpp
        ui/selection.cpp
        ui/search.cpp
        ui/search_overlay.cpp
        ui/settings_catalog.cpp
        ui/settings_form.cpp
        ui/settings_i18n.cpp
        ui/settings_panel.cpp
        ui/settings_preview.cpp
        ui/shortcuts_table.cpp
        ui/startup_notice.cpp
        ui/tab_bar.cpp
        ui/tab_strip.cpp
        ui/terminal_view.cpp
        ui/workspace_keys.cpp
        ui/workspace_view.cpp)

# 平台层实现按目标平台编入：共享路径只见接口，ConPTY 句柄与 Win32 API 留在 src/platform/ 内
# （裁决 7.11、架构 §2.2）。POSIX 腿于 2026-10-08 落地（SPEC.FEAT.CONN.01）。
if (WIN32)
    target_sources(borealis_core PRIVATE
            platform/win/conpty_connection.cpp
            platform/win/local_terminal.cpp
            platform/win/win_text.cpp)
elseif (UNIX)
    target_sources(borealis_core PRIVATE
            platform/posix/forkpty_connection.cpp
            platform/posix/local_terminal.cpp)
    # forkpty 需要链接 libutil（某些发行版如 Debian/Ubuntu 需要显式指定）。
    find_library(UTIL_LIBRARY util)
    if (UTIL_LIBRARY)
        target_link_libraries(borealis_core PRIVATE ${UTIL_LIBRARY})
    endif ()
endif ()

# 消费者统一配置（链接 aurora + C++20 + 共享 PCH + 统一告警），额外私有头目录为本仓公共头根。
aurora_setup_consumer_target(borealis_core "${CMAKE_CURRENT_SOURCE_DIR}/../include")

add_executable(borealis main.cpp)

aurora_setup_consumer_target(borealis "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(borealis PRIVATE borealis_core)

# 产物名 Borealis（裁决 7.14：仓内可自主命名的标识统一 borealis，产物首字母大写）。
set_target_properties(borealis PROPERTIES OUTPUT_NAME "Borealis")
