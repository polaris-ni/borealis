# ============================================================
# Borealis 源码
# ------------------------------------------------------------
# 纯逻辑模块独立成静态库：应用与测试 runner 都要链接它，且它不依赖 UI
# （AGENTS.md §4.4 第 20 条：纯逻辑层必须可独立单测）。
# ============================================================

add_library(borealis_core STATIC
        cli/handoff.cpp
        cli/startup.cpp
        config/form_transfer.cpp
        config/settings.cpp
        config/shortcut_overrides.cpp
        config/store.cpp
        config/themes.cpp
        config/window_geometry_store.cpp
        config/workspace_store.cpp
        conn/key_model.cpp
        conn/key_push.cpp
        conn/key_store.cpp
        conn/profile.cpp
        conn/profile_views.cpp
        conn/proxy_jump.cpp
        conn/reconnect.cpp
        conn/serial_connection.cpp
        conn/serial_model.cpp
        conn/sftp_client.cpp
        conn/sftp_model.cpp
        conn/snippet.cpp
        conn/ssh_connection.cpp
        conn/ssh_dial.cpp
        conn/ssh_hop_chain.cpp
        conn/ssh_shared.cpp
        conn/telnet_connection.cpp
        conn/telnet_negotiation.cpp
        conn/tunnel_client.cpp
        conn/tunnel_model.cpp
        conn/tunnel_socks.cpp
        diag/crash_report.cpp
        platform/credential_store.cpp
        ui/connection_glyph.cpp
        ui/connection_sidebar.cpp
        ui/connection_wizard.cpp
        ui/credential_prompt.cpp
        vt/parser.cpp
        term/codec.cpp
        term/utf8.cpp
        term/codec.cpp
        term/charset.cpp
        term/clipboard_read.cpp
        term/command_block.cpp
        term/keymap.cpp
        term/mouse.cpp
        term/notification.cpp
        term/osc.cpp
        term/paste.cpp
        term/width.cpp
        term/terminal.cpp
        grid/storage.cpp
        session/clipboard_outbox.cpp
        session/damage_queue.cpp
        session/notification_outbox.cpp
        session/raw_tap.cpp
        session/screen_mirror.cpp
        session/session.cpp
        session/session_log.cpp
        ui/active_window_tracker.cpp
        ui/broadcast.cpp
        ui/broadcast_banner.cpp
        ui/cell_layout.cpp
        ui/closed_tab_stack.cpp
        ui/color_text.cpp
        ui/command_block.cpp
        ui/debug_panel.cpp
        ui/font_choice.cpp
        ui/grid_size_debounce.cpp
        ui/keys_format.cpp
        ui/keys_panel.cpp
        ui/osc7.cpp
        ui/palette.cpp
        ui/pane_tree.cpp
        ui/quick_select.cpp
        ui/right_click.cpp
        ui/selection.cpp
        ui/search.cpp
        ui/search_overlay.cpp
        ui/serial_format.cpp
        ui/serial_monitor_format.cpp
        ui/serial_monitor_panel.cpp
        ui/settings_catalog.cpp
        ui/settings_form.cpp
        ui/settings_i18n.cpp
        ui/settings_panel.cpp
        ui/settings_preview.cpp
        ui/shortcuts_table.cpp
        ui/sftp_format.cpp
        ui/sftp_panel.cpp
        ui/snippet_panel.cpp
        ui/startup_notice.cpp
        ui/tab_bar.cpp
        ui/tab_strip.cpp
        ui/terminal_view.cpp
        ui/tunnel_format.cpp
        ui/tunnel_panel.cpp
        ui/url.cpp
        ui/workspace_keys.cpp
        ui/workspace_snapshot.cpp
        ui/workspace_view.cpp)

# 平台层实现按目标平台编入：共享路径只见接口，ConPTY 句柄与 Win32 API 留在 src/platform/ 内
# （裁决 7.11、架构 §2.2）。POSIX 腿于 2026-10-08 落地（SPEC.FEAT.CONN.01）。
if (WIN32)
    target_sources(borealis_core PRIVATE
            platform/win/agent_socket.cpp
            platform/win/conpty_connection.cpp
            platform/win/crash_handler.cpp
            platform/win/credential_store.cpp
            platform/win/file_secure.cpp
            platform/win/hop_pipe.cpp
            platform/win/instance_channel.cpp
            platform/win/local_terminal.cpp
            platform/win/open_url.cpp
            platform/win/process_args.cpp
            platform/win/serial.cpp
            platform/win/tcp.cpp
            platform/win/user_dir.cpp
            platform/win/win_text.cpp)
elseif (UNIX)
    target_sources(borealis_core PRIVATE
            platform/posix/agent_socket.cpp
            platform/posix/credential_store.cpp
            platform/posix/crash_handler.cpp
            platform/posix/file_secure.cpp
            platform/posix/hop_pipe.cpp
            platform/posix/forkpty_connection.cpp
            platform/posix/instance_channel.cpp
            platform/posix/local_terminal.cpp
            platform/posix/open_url.cpp
            platform/posix/process_args.cpp
            platform/posix/serial.cpp
            platform/posix/tcp.cpp
            platform/posix/user_dir.cpp)
    # forkpty 需要链接 libutil（某些发行版如 Debian/Ubuntu 需要显式指定）。
    find_library(UTIL_LIBRARY util)
    if (UTIL_LIBRARY)
        target_link_libraries(borealis_core PRIVATE ${UTIL_LIBRARY})
    endif ()
    # libsecret 腿经 dlopen 运行期接库（裁决 7.132 D1），只需动态装载器本身。
    target_link_libraries(borealis_core PRIVATE ${CMAKE_DL_LIBS})
endif ()

# Win32 腿的 TCP 转发（platform/win/tcp.cpp，SPEC.FEAT.CONN.08）需要 Winsock2 库。
# `shell32` 是打开外部 URI 那条腿（platform/win/open_url.cpp，SPEC.FEAT.INTERACT.05）要的
# `ShellExecuteW` 所在库；`advapi32` 是 CredMan 腿（platform/win/credential_store.cpp，
# SPEC.FEAT.CONN.09）的 `CredWriteW`/`CredReadW`/`CredDeleteW` 所在库。**本沙箱无 MSVC，
# 链接面未验证**（AGENTS.md §4.2 第 11 条）。
if (WIN32)
    target_link_libraries(borealis_core PRIVATE ws2_32 shell32 advapi32)
endif ()

# SSH 传输腿（`SPEC.FEAT.CONN.02`）：libssh 经 vcpkg 获取，vcpkg 给出的 CMake 目标名是 `ssh`
# （不是 `libssh::ssh`——其 port 的 usage 说明里写明 `target_link_libraries(main PRIVATE ssh)`）。
find_package(libssh CONFIG REQUIRED)
target_link_libraries(borealis_core PRIVATE ssh)

# 会话编码族（`SPEC.FEAT.TERM.09`）：iconv 单腿。POSIX 腿取系统 libc、Windows 腿取 vcpkg
# libiconv（其 port 的 usage 写明目标是 `Iconv::Iconv`，由 CMake 内置 FindIconv 模块给出，
# 故两腿同一行 find_package、同一份 `<iconv.h>` API）。manifest 里那条带了 `"platform": "windows"`
# ——Linux 上 libiconv 会盖掉系统实现而徒增一次构建。
find_package(Iconv REQUIRED)
target_link_libraries(borealis_core PRIVATE Iconv::Iconv)

# 消费者统一配置（链接 aurora + C++20 + 共享 PCH + 统一告警），额外私有头目录为本仓公共头根。
aurora_setup_consumer_target(borealis_core "${CMAKE_CURRENT_SOURCE_DIR}/../include")

# 界面短语的构建期词表（`SPEC.FEAT.PREF.05`，Aurora i18n v2 管线）：两档 ARB JSON 一份基准
# （zh）+ 一份译文（en）在构建期交 `gen_i18n`，产出强类型 Id 与具名形参构造函数的生成头，
# 以及 `AUM1` 二进制词表（运行期整读喂 `MessageCatalog::load_binary`）。译文字面量因此
# **不在源码里**——`tools/check/check_display_literals.py` 的白名单对象随之改成 JSON。
# 生成物落 core：core 是应用与测试 runner 的共同上游，头目录取 PUBLIC 让两侧同一份 Id。
aurora_i18n_generate(TARGET borealis_core
        NAMESPACE borealis::msg
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/i18n/messages_zh.json"
        LOCALES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/i18n/messages_en.json"
        OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/i18n"
        ID_BASE 1024)
target_include_directories(borealis_core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/i18n")
# 私有头互引路径：conn/ssh_connection.h、conn/profile_views.h 等私有头按
# "conn/xxx.h" 相对 src/ 解析（头仍在 src/ 树内，公共面不因此扩一寸）。
target_include_directories(borealis_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# 崩溃报告头行的产品版本（`diag::product_version()`，裁决 7.142）：由工程版本注入，
# 不在源码里硬编码第二份。
target_compile_definitions(borealis_core PRIVATE BOREALIS_VERSION="${PROJECT_VERSION}")

add_executable(borealis main.cpp)

aurora_setup_consumer_target(borealis "${CMAKE_CURRENT_SOURCE_DIR}/../include")
# 同 borealis_core：main 直引 conn/ssh_connection.h 等私有头装配 SSH 腿。
target_include_directories(borealis PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(borealis PRIVATE borealis_core)

# 产物名 Borealis（裁决 7.14：仓内可自主命名的标识统一 borealis，产物首字母大写）。
set_target_properties(borealis PROPERTIES OUTPUT_NAME "Borealis")
