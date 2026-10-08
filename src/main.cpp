#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/core/log.h"
#include "aurora/render/font_discovery.h"
#include "aurora/window/native_surfaces.h"
#include "borealis/conn/local_terminal.h"
#include "borealis/config/form_transfer.h"
#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/config/themes.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/session/session.h"
#include "borealis/term/keymap.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"
#include "borealis/ui/font_choice.h"
#include "borealis/ui/shortcuts_table.h"
#include "borealis/ui/settings_form.h"
#include "borealis/ui/tab_strip.h"
#include "borealis/ui/closed_tab_stack.h"
#include "ui/debug_panel.h"
#include "ui/settings_i18n.h"
#include "ui/settings_panel.h"
#include "ui/startup_notice.h"
#include "ui/tab_bar.h"
#include "ui/terminal_view.h"
#include "ui/workspace_view.h"
#include <aurora/widget/command_palette.h>

namespace {

/// @brief 会话的名义初始尺寸：真实行列由控件首次布局按自身尺寸派生并下发
///        （`SPEC.FEAT.XFER.01` 的 UI 取值腿），这里只要保证状态机与连接一起来有个可用的格。
constexpr borealis::session::Size kNominalViewport{80U, 24U};

/// @brief 降级留痕的 ASCII 取值名（日志字面量须是英文，AGENTS.md §4.3 第 14 条）。
[[nodiscard]] auto verdict_name(borealis::ui::FontFamilyVerdict verdict) -> std::string_view {
    switch (verdict) {
        case borealis::ui::FontFamilyVerdict::Configured:
            return "configured";
        case borealis::ui::FontFamilyVerdict::NotMonospace:
            return "not_monospace";
        case borealis::ui::FontFamilyVerdict::Unlisted:
            return "unlisted";
    }
    return "unknown";  // 枚举已穷尽：新增档位而忘跟上文案时，这串比静默复用上一条更醒目
}

/// @brief 从配置装出视口的外观包。
///
/// 面板的即时广播与启动时的初始构造共用本函数，于是「改字号立刻生效」与「启动时就是这个字号」
/// 不可能分叉。字体族目录由调用方在装配阶段取一次并传进来（首次枚举是同步 IO，不得进绘制路径）。
[[nodiscard]] auto make_appearance(const borealis::config::Settings &settings,
                                   const std::vector<borealis::ui::FontFamilyEntry> &catalog)
    -> borealis::ui::TerminalView::Appearance {
    const borealis::ui::FontFamilyChoice font_choice =
        borealis::ui::choose_font_family(settings.appearance.font_family, catalog);
    if (font_choice.verdict != borealis::ui::FontFamilyVerdict::Configured) {
        // 诊断走日志而非对话框：配置的字体族不可用不阻断启动，视口照常起来只是换了族。
        AURORA_LOG_WARN("main", "configured font family is unusable, using fallback: '",
                        settings.appearance.font_family, "' -> '", font_choice.family, "' (",
                        verdict_name(font_choice.verdict), ", catalog size ", catalog.size(), ")");
    }
    borealis::ui::TerminalView::Appearance appearance;
    appearance.palette = settings.appearance.palette;
    appearance.ref_font = au::Font{.family = font_choice.family,
                                   .size_pt = static_cast<float>(settings.appearance.font_size_pt),
                                   .weight = 400};
    appearance.typography = borealis::ui::Typography{
        .line_height = settings.appearance.font_line_height,
        .letter_spacing_dp = settings.appearance.font_letter_spacing_dp,
    };
    appearance.padding_dp = settings.appearance.viewport_padding_dp;
    appearance.blink_period = std::chrono::milliseconds{settings.appearance.cursor_blink_period_ms};
    appearance.font_fallback_chain = settings.appearance.font_fallback_chain;
    return appearance;
}

/// @brief 从配置装出视口的选区与复制行为。
///
/// 视口刻意不认识 `config`（`config` 含 `ui::PaletteSpec`，反向依赖会成模块环，架构 §2.3），
/// 故搬运点只能在装配层；与外观包同理，广播腿复用同一函数。
[[nodiscard]] auto make_interaction(const borealis::config::Settings &settings)
    -> borealis::ui::TerminalView::InteractionOptions {
    borealis::ui::TerminalView::InteractionOptions interaction;
    interaction.word_delimiters = settings.terminal.word_delimiters;
    interaction.copy_on_select = settings.terminal.copy_on_select;
    interaction.copy = borealis::ui::CopyOptions{
        .trim_trailing_space = settings.terminal.trim_pasted_trailing_space,
        .smart_line_join = settings.terminal.smart_line_join,
        .strip_tmux_border_chars = settings.terminal.strip_tmux_border_chars,
    };
    interaction.right_click = settings.terminal.right_click;
    interaction.paste.newline = settings.terminal.paste_newlines;
    // TODO(SPEC.FEAT.CONN.05): 粘贴的 `line_ending` 取连接的行尾设置，本地终端就是缺省 LF；
    // 串口那一腿到货后由连接的行尾配置搬进来（块间隔同为缺省值，需求未开配置键）。
    return interaction;
}

/// @brief 从配置装出会话的初始档（光标形态 / 闪烁档 / Ambiguous 口径）。
///
/// 三条都是**建会话那一刻**取用（判据文 §0 边界②），运行期入口碰不到它们，故本函数只在构造点出现；
/// 但主会话与预览会话必须同源，否则同一屏里画出两种光标（`Bar` / `Underline` 两档在绘制侧先于失焦
/// 降级落笔），所以它与 `make_appearance()` 一样是「一处折算、两处复用」的搬运件（裁决 7.76②）。
[[nodiscard]] auto make_terminal_defaults(const borealis::config::Settings &settings)
    -> borealis::term::TerminalDefaults {
    return borealis::term::TerminalDefaults{
        .cursor_shape = settings.appearance.cursor_shape,
        .cursor_blinking = settings.appearance.cursor_blinking,
        .ambiguous_width = settings.terminal.ambiguous_width,
    };
}

/// @brief 互转点：框架 `KeyCombo` → 本仓键位语义值（`term::KeyPress`）。
///
/// 与 `ui::TerminalView` 那条「`KeyEvent` → `KeyPress`」的互转点是两个**来源**而不是两条算式：命令表
/// 上的绑定是注册值、不带运行期锁定态，而冲突比对只吃四个可按位（裁决 7.51①），与框架
/// `KeyCombo::matches` 在 G25 回货后「两侧各取可按住位子集再逐位相等」的口径逐位一致。锁定位照搬不屏蔽。
[[nodiscard]] auto key_press_of(const au::KeyCombo &combo) -> borealis::term::KeyPress {
    return borealis::term::KeyPress{
        .sym = static_cast<borealis::term::KeySym>(combo.key),
        .shift = (combo.modifiers & au::ModifierKey::Shift) != 0U,
        .control = (combo.modifiers & au::ModifierKey::Control) != 0U,
        .alt = (combo.modifiers & au::ModifierKey::Alt) != 0U,
        .meta = (combo.modifiers & au::ModifierKey::Meta) != 0U,
        .num_lock = (combo.modifiers & au::ModifierKey::NumLock) != 0U,
    };
}

/// @brief 定长 UTF-8 串的码点收集器（与 `session.cpp` 的 `BufferSink` 同一形态，装配侧另有一份）。
class DecodeCollector final : public borealis::term::CodePointSink {
  public:
    auto on_code_point(char32_t cp) -> void override { out.push_back(cp); }

    std::u32string out;
};

}  // namespace

auto main() -> int {
    // 局部对象的声明次序即析构次序的倒序：`store` 最先声明故最后析构，`app` 最先析构——窗口与
    // 控件树都随它消散，会话与宽度判定策略仍活着，控件不会摸到已销毁的会话。
    borealis::config::Store store;  // 面板要 `replace()`，故本对象不再 `const`
    const borealis::config::Settings &settings = store.settings();
    // 词条必须在任何控件绘制之前装填：查表失败回退空文本，晚装就会让首帧闪一下空标签。
    borealis::ui::install_settings_strings();

    au::WindowOptions opts;
    opts.size = au::Size{.width = 960.0F, .height = 640.0F};
    opts.title = "Borealis";
    auto created = au::create_native_window(opts);
    if (!created) {
        AURORA_LOG_ERROR("main", "native window creation failed: ", created.error().message);
        return 1;
    }
    std::unique_ptr<au::Window> window = std::move(created.value());
    au::Surface &surface = window->surface();

    // 字体族目录只在装配阶段取一次：框架的首次调用要递归扫系统字体目录并逐个开 face 才能判定
    // 等宽性，属同步 IO，不得进事件回调或绘制路径（AGENTS.md §4.5 第 25 条）。取全量而非
    // `monospace_only` 的那个子集，是为了让「装了但非等宽」与「压根没这个族」两档降级可分别留痕。
    // 面板的字体族下拉与广播腿共用这一份目录（裁决 7.52 的 S16），不再第二次枚举。
    const std::vector<borealis::ui::FontFamilyEntry> catalog = []() -> std::vector<borealis::ui::FontFamilyEntry> {
        std::vector<borealis::ui::FontFamilyEntry> out;
        for (const au::render::FontFamilyInfo &info : au::render::list_font_families()) {
            out.push_back(borealis::ui::FontFamilyEntry{.family = info.family, .monospace = info.monospace});
        }
        return out;
    }();

    // WS.01 多标签真值源 + 闭包栈（WS.10 撤销用）
    borealis::ui::TabStrip tab_strip;
    borealis::ui::ClosedTabStack closed_tab_stack;

    // 标识只增不复用（与 PaneTree 的容器标识同一条理由，裁决 7.42⑩）：新建与撤销重开**共用**这一支
    // 计数器，否则「关一个再重开」造出的新身份会撞进仍存活的标签里，`add`/`insert_at` 的重复即静默失败。
    borealis::ui::TabId next_tab_id = 1;

    // 每个标签对应一个 WorkspaceView（支持该标签内的分屏）
    struct TabWorkspace {
        std::shared_ptr<borealis::ui::WorkspaceView> workspace;
        std::vector<std::shared_ptr<borealis::session::Session>> sessions;  // 该标签的所有会话（目前单 pane 只有一个）
    };
    std::map<borealis::ui::TabId, TabWorkspace> tab_workspaces;

    // 当前显示的标签 ID（用于场景根切换）
    std::optional<borealis::ui::TabId> current_tab_id;

    // 每只视口的浮层与搜索依赖接线（宿主与注册表须待 `host` / `app` 建成才取得到，故首标签建得早的
    // 那几只在建成之后统一补挂，见下面赋值处）。缺这两条挂接，右键菜单、多行粘贴确认与搜索浮层
    // **结构上弹不起来**（裁决 7.41③ / 7.81），而集成用例经替身宿主测的是接线而非这里。
    std::function<void(borealis::ui::TerminalView &)> wire_view;

    /// 上一次写到窗口标题栏的串：逐帧都调 `set_title` 就是每帧一次 `WM_SETTEXT`，而这个量在
    /// 用户不换标签、对端不改 OSC 标题时根本不动。
    std::string last_window_title;

    // 创建首个本地终端标签
    auto create_new_tab = [&]() -> void {
        const borealis::ui::TabId id = next_tab_id++;

        // 建会话
        borealis::conn::LocalTerminalSpec spec;
        spec.command_line = settings.connection.local_shell;
        spec.working_directory = settings.connection.startup_directory;
        borealis::term::UnicodeWidthPolicy width_policy;
        auto session = std::make_shared<borealis::session::Session>(
            borealis::conn::make_local_terminal_connection(spec, kNominalViewport),
            kNominalViewport, settings.terminal.scrollback_limit, width_policy,
            make_terminal_defaults(settings));
        session->set_frame_wake([&surface]() -> void { surface.request_wake(); });
        session->start();

        // 建视口
        auto view = std::make_shared<borealis::ui::TerminalView>(
            *session, make_appearance(settings, catalog), make_interaction(settings));
        // 浮层宿主与搜索依赖在创建那一刻一并挂上（`host` / `app` 已就绪时；首标签由后面统一补挂）。
        if (wire_view) {
            wire_view(*view);
        }

        // 建工作区分屏容器（单 pane 起步）
        borealis::ui::WorkspaceView::Hooks ws_hooks;
        ws_hooks.make_pane = [session, view](borealis::ui::PaneId /*pane*/) -> std::shared_ptr<borealis::ui::TerminalView> {
            // 首 pane 复用传入的 view，后续 pane 需要新建会话（暂不支持，返回空）
            return view;
        };
        ws_hooks.dispatch_grid = [session](borealis::ui::PaneId /*pane*/, borealis::ui::GridSize size) -> void {
            session->resize({size.columns, size.rows});
        };
        ws_hooks.teardown_pane = [](borealis::ui::PaneId /*pane*/) -> void {
            // 单会话架构下不回收
        };

        auto workspace = std::make_shared<borealis::ui::WorkspaceView>(view, std::move(ws_hooks));
        tab_workspaces[id] = TabWorkspace{.workspace = std::move(workspace), .sessions = {session}};

        // 加入标签条
        // TODO(SPEC.FEAT.CONN.02): 档案名待连接档案落地后从档案取，首版先给本地终端的固定措辞。
        const std::u32string default_name = U"本地终端";
        tab_strip.add(id, default_name);
        current_tab_id = id;
    };

    create_new_tab();

    // 辅助函数：重建根 Stack 的子节点列表（标签栏 + 当前工作区）
    auto rebuild_root_children = [](std::shared_ptr<au::Stack> &stack,
                                     const std::map<borealis::ui::TabId, TabWorkspace> &workspaces,
                                     std::optional<borealis::ui::TabId> current_id) -> void {
        // 保留第一个子节点（标签栏），替换第二个（工作区）
        auto &nodes = stack->child_nodes_mut();
        if (nodes.size() < 2) {
            return;  // 还没初始化完
        }
        nodes.resize(1);  // 只留标签栏
        if (current_id.has_value()) {
            auto it = workspaces.find(current_id.value());
            if (it != workspaces.end()) {
                nodes.push_back(aurora::Node{it->second.workspace});
            }
        }
        stack->mark_needs_layout();
        stack->mark_needs_paint();
    };

    // 右键菜单与多行粘贴确认都是浮层，故场景根是浮层宿主而非视口本身（裁决 7.41③）：宿主的子节点
    // [0] 是撑满窗口的标签栏，[1] 是当前工作区，[2..] 是按需追加的 Popup / Dialog / 面板浮层。
    auto host = std::make_shared<au::OverlayHost>();

    // 添加标签栏（用 Stack 承载，因为 OverlayHost 只管理浮层，常规内容须走普通容器）
    auto root_stack = std::make_shared<au::Stack>();
    root_stack->modifier.set(au::Modifier{}.fill_max_size());

    // 真正关闭一张标签：从标签条摘除、记进闭包栈、销毁工作区与会话、必要时切到相邻标签。
    // 「确认」与「直接关」两条路径共用这一支，差别只在要不要先问一句（裁决 7.47⑧）。
    auto perform_close = [&](std::uint64_t raw_id) -> void {
        const borealis::ui::TabId id = static_cast<borealis::ui::TabId>(raw_id);
        const bool removed = tab_strip.close(id);
        if (!removed) {
            return;
        }
        // 关闭前记录到闭包栈（`SPEC.FEAT.WS.10`）
        auto it = tab_workspaces.find(id);
        if (it != tab_workspaces.end()) {
            borealis::ui::ClosedTabSpec spec;
            // 名称与位置都取 `close()` 记下的那份：此刻该标签已不在表里，回查只会拿到空名，
            // 于是重开回来的标签变成一个无名格——那正是本件要避免的「内容不可恢复」被扩大成
            // 「名字也丢了」。
            if (const auto closed = tab_strip.last_closed(); closed.has_value()) {
                static_cast<void>(borealis::term::encode_utf8(closed->name, spec.name));
                spec.index_in_strip = closed->index_in_strip;
            }
            // 本地终端的连接规格从配置取（目前只支持本地终端，SSH 腿到货后追加）
            spec.local_shell = settings.connection.local_shell;
            spec.startup_directory = settings.connection.startup_directory;
            closed_tab_stack.push(std::move(spec));
            // 销毁对应的 WorkspaceView 和会话
            tab_workspaces.erase(it);
        }
        if (current_tab_id.has_value() && current_tab_id.value() == id) {
            // 选下一个或上一个
            const auto next = tab_strip.selected();
            if (next.has_value()) {
                current_tab_id = next.value();
            } else {
                current_tab_id.reset();
            }
            // 更新显示的工作区
            rebuild_root_children(root_stack, tab_workspaces, current_tab_id);
        }
    };

    // 关闭确认对话框常驻一只：判据是该标签**任一** pane 的会话进程仍在（`Session::alive()`），
    // 一次确认关整张标签（逐 pane 追问在多 pane 标签上就是 N 个对话框）。全部已退出则直接关、不再问。
    // 对话框盖在浮层宿主上，故其生存期须跨过 Show→答话这一段，用一只成员 shared_ptr 持有；与多行粘贴
    // 确认（`TerminalView::ask_multiline_warning`）同形态，首次挂入后复用同一实例换文案。
    auto close_confirm = std::make_shared<au::Dialog>();
    bool close_confirm_mounted = false;
    auto ask_close_confirm = [&](std::uint64_t raw_id) -> void {
        if (!close_confirm_mounted) {
            static_cast<void>(host->add_overlay(aurora::Node{std::static_pointer_cast<au::Widget>(close_confirm)}));
            close_confirm_mounted = true;
        }
        close_confirm->set_content(
            au::confirm(
                "关闭标签",  // CJK-LITERAL: 上屏文案 - 面向用户的对话框标题
                "该标签里还有正在运行的进程，关闭会终止它们。确定关闭吗？",  // CJK-LITERAL: 上屏文案 - 面向用户的对话框正文
                [raw_id, &perform_close, close_confirm](bool accepted) -> void {
                    close_confirm->close();
                    if (accepted) {
                        perform_close(raw_id);
                    }
                }));
        close_confirm->show();
    };

    // 标签栏界面腿已在上面 root_stack 创建时内联定义，此处不再重复
    auto tab_bar_ptr = std::make_shared<borealis::ui::TabBarWidget>(borealis::ui::TabBarHooks{
        .tabs = [&]() -> std::vector<borealis::ui::TabVisual> {
            std::vector<borealis::ui::TabVisual> out;
            const auto tabs = tab_strip.tabs();
            const auto selected = tab_strip.selected();
            for (const auto &tab : tabs) {
                out.push_back(borealis::ui::TabVisual{
                    .id = tab.id,
                    .display_name = borealis::ui::resolve_tab_name(tab.names, settings.appearance.tab_name_priority),
                    .is_selected = (selected.has_value() && selected.value() == tab.id),
                    .has_close_button = (tab_strip.count() > 1),  // 末位不给关
                    .bell_triggered = tab.bell_triggered,
                    .has_activity = tab.has_activity,
                });
            }
            return out;
        },
        .select = [&](std::uint64_t id) -> void {
            tab_strip.select(static_cast<borealis::ui::TabId>(id));
            current_tab_id = static_cast<borealis::ui::TabId>(id);
            // 切换当前显示的工作区视图
            rebuild_root_children(root_stack, tab_workspaces, current_tab_id);
            // 切换到该标签时清除活动标记（`SPEC.FEAT.WS.04`）
            tab_strip.take_activity(static_cast<borealis::ui::TabId>(id));
        },
        .close = [&](std::uint64_t id) -> void {
            // 判据：该标签任一 pane 的会话进程仍在 ⇒ 一次确认关整张标签；全部已退出 ⇒ 直接关、不再问
            //（裁决 7.47⑧）。
            bool any_alive = false;
            if (const auto found = tab_workspaces.find(static_cast<borealis::ui::TabId>(id));
                found != tab_workspaces.end()) {
                for (const auto &session : found->second.sessions) {
                    if (session->alive()) {
                        any_alive = true;
                        break;
                    }
                }
            }
            if (any_alive) {
                ask_close_confirm(id);
                return;
            }
            perform_close(id);
        },
        .rename = [&](std::uint64_t id, std::u32string name) -> void {
            tab_strip.rename(static_cast<borealis::ui::TabId>(id), std::move(name));
        },
        .move = [&](std::uint64_t id, std::size_t to_index) -> void {
            tab_strip.move(static_cast<borealis::ui::TabId>(id), to_index);
        },
        .add_new = [&]() -> void {
            create_new_tab();
            // 新标签创建后重建根节点子节点以包含新工作区
            rebuild_root_children(root_stack, tab_workspaces, current_tab_id);
        },
    });
    root_stack->child_nodes_mut().push_back(aurora::Node{tab_bar_ptr});
    
    // 添加当前工作区
    if (current_tab_id.has_value()) {
        auto it = tab_workspaces.find(current_tab_id.value());
        if (it != tab_workspaces.end()) {
            root_stack->child_nodes_mut().push_back(aurora::Node{it->second.workspace});
        }
    }
    
    host->child_nodes_mut().push_back(aurora::Node{root_stack});

    // chrome 主题挂场景根：面板与后续界面件读同一份 `Theme`，而它**刻意不随终端主题联动**
    //（裁决 7.52 的 S5① / N6：切配色主题时整窗跟着变会让用户以为丢了设置）。
    auto chrome = std::make_shared<au::ThemeScope>(borealis::ui::settings_chrome_theme(),
                                                   au::Node{std::static_pointer_cast<au::Widget>(host)});
    au::Scene scene{au::Node{std::static_pointer_cast<au::Widget>(std::move(chrome))}};
    borealis::session::ClipboardOutbox outbox;

    au::Application app{std::move(scene), std::move(window), opts};
    
    // 视口的两条浮层依赖此刻才取得到，先给已存在的全部视图（首标签那一只）补挂，再交给
    // `create_new_tab` 与撤销重开在创建那一刻调用——两条建视图路径共用同一处接线，结构上不可能分叉。
    wire_view = [&host, &app](borealis::ui::TerminalView &view) -> void {
        view.set_overlay_host(*host);
        view.set_search_dependencies(app.shortcuts(), app.focus());
    };
    for (auto &[tab_id, workspace] : tab_workspaces) {
        static_cast<void>(tab_id);
        for (borealis::ui::PaneId pane = 1; pane <= workspace.workspace->pane_count(); ++pane) {
            if (auto *v = workspace.workspace->view_of(pane)) {
                wire_view(*v);
            }
        }
    }

    // 面板的三条接缝都在装配层兑现：装载取 `Store` 的当前配置，落盘走 `apply_form` + `replace()`，
    // 广播把刚落盘的配置重新折算成外观包与交互项交回视口（`SPEC.FEAT.PREF.02` 的「即时生效」腿）。
    borealis::ui::SettingsPanel::Hooks hooks;
    hooks.load = [&store]() -> std::vector<borealis::ui::FormEntry> {
        return borealis::config::form_entries(store.settings());
    };
    hooks.persist = [&store](const borealis::ui::SettingsForm &form) -> std::optional<std::string> {
        return store.replace(borealis::config::apply_form(form, store.settings()));
    };
    hooks.broadcast = [&tab_workspaces, &current_tab_id, &store, &catalog](const borealis::ui::SettingsForm &form) -> void {
        const borealis::config::Settings next = borealis::config::apply_form(form, store.settings());
        // 广播到当前标签的所有视口（目前每个标签只有一个工作区，但工作区内可能有多个 pane）
        if (current_tab_id.has_value()) {
            auto it = tab_workspaces.find(current_tab_id.value());
            if (it != tab_workspaces.end()) {
                // 遍历该工作区的所有 pane 视图并应用新外观
                for (borealis::ui::PaneId pane = 1; pane <= it->second.workspace->pane_count(); ++pane) {
                    auto *v = it->second.workspace->view_of(pane);
                    if (v) {
                        v->apply_appearance(make_appearance(next, catalog));
                        v->apply_interaction_options(make_interaction(next));
                    }
                }
            }
        }
    };
    // 主题候选表交进面板（裁决 7.61①）：`BuiltinTheme` 只带存储键名，卡面就逐字显示那串字，
    // 于是「卡片次序与名字」的唯一真源仍是 `config::builtin_themes()`，面板侧不另立一份显示名表。
    hooks.themes = []() -> std::vector<borealis::ui::SettingsPanel::ThemeChoice> {
        std::vector<borealis::ui::SettingsPanel::ThemeChoice> out;
        for (const borealis::config::BuiltinTheme &theme : borealis::config::builtin_themes()) {
            out.push_back(borealis::ui::SettingsPanel::ThemeChoice{
                .name = std::string{theme.name}, .palette = theme.palette});
        }
        return out;
    };
    // 回退链候选族目录交的是装配阶段那一份目录本身（裁决 7.52 的 S16），既不再第二次枚举，也不按
    // 等宽性过滤：回退链的存在理由是「主族缺字时找另一个面」，另一个面不必等宽。
    hooks.families = [&catalog]() -> std::vector<borealis::ui::FontFamilyEntry> {
        return catalog;
    };
    // 预览盒的外观包与主视口**同源**（判据文 F-c）：同一个 `make_appearance()`，读同一份已落盘的配置。
    // 面板自己再拼一份外观就是第二条搬运路径，「面板改了某项而预览拿不到该项」那种分叉正由此消除。
    hooks.preview_appearance = [&store, &catalog]() -> borealis::ui::TerminalView::Appearance {
        return make_appearance(store.settings(), catalog);
    };
    // 三条构造期注入与主会话**同一对折算**（判据文 §7 的三条，裁决 7.76②）：预览那条会话建好就不再
    // 重建，拿不到外观包那条运行期入口，故这条接缝独立于 `preview_appearance`；面板任缺其一即不画横条，
    // 「装了外观而漏了初始档」在结构上不成立。
    hooks.preview_defaults = [&store]() -> borealis::term::TerminalDefaults {
        return make_terminal_defaults(store.settings());
    };
    // 夹具在预览视口的 `on_layout` 里随 `resize` 重投，那一批脏要下一帧才排；不唤醒就会停在
    // 「横条画了但内容还是上一版」。与下面 `set_on_frame` 里的 `pump_preview()` 配对存在。
    hooks.preview_wake = [&surface]() -> void { surface.request_wake(); };
    // 快捷键只读表的行源（裁决 7.72②）：注册表逐条折成本域形态，覆盖表只贡献孤儿行那一半归纯逻辑件判。
    // 显示串与比对值**必须从同一个 `KeyCombo` 同行取出**——分两次取就是让「标注来自实际比对」那条判据
    // （D2-a）失去根据，因为界面上看到的串与比掉的键位不再是一个来源。
    // 取的是**打开面板时**的注册表快照，故 `settings.open` 之类在本行之后登记的命令也在表内。
    hooks.commands = [&app]() -> std::vector<borealis::ui::ShortcutCommandEntry> {
        std::vector<borealis::ui::ShortcutCommandEntry> out;
        for (const au::Command &command : app.commands().all()) {
            borealis::ui::ShortcutCommandEntry entry{
                .command = command.id,
                .title = command.title,
                .category = command.category,
            };
            if (command.default_binding.has_value()) {
                entry.binding_text = command.default_binding->to_string();
                entry.binding = key_press_of(*command.default_binding);
            }
            out.push_back(std::move(entry));
        }
        return out;
    };
    borealis::ui::SettingsPanel panel{*host, app.shortcuts(), std::move(hooks)};

    // 打开入口按 `SPEC.FEAT.PREF.02` 走命令层：命令是快捷键、菜单与命令面板的共同真源（架构 §11.2），
    // 在此登记一次即同时得到 `Ctrl+,` 与将来面板/命令面板里的同一条目。
    au::Command open_settings;
    open_settings.id = "settings.open";
    open_settings.title = borealis::ui::settings_label("settings.action.open");
    open_settings.category = "settings";
    open_settings.action = [&panel]() -> void { panel.open(); };
    open_settings.default_binding = au::KeyCombo{au::ModifierKey::Control, au::KeyCode::Comma};
    open_settings.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_settings));
    // 搜索浮层的打开入口与设置面板同形态（判据文 F1-a）：命令是快捷键、菜单与命令面板的共同真源，
    // 故这里登记一次就同时得到 `Ctrl+F` 与快捷键只读表上那一行。`Ctrl+F` 由快捷键层在控件消费之前
    // 拦截，**不进会话**——需求原文写死了这个组合，vim 的整屏翻页因此被拿走，代价登记在 §3 D8。
    au::Command open_search;
    open_search.id = "search.open";
    open_search.title = borealis::ui::settings_label("search.action.open");
    open_search.category = "terminal";
    open_search.action = [&tab_workspaces, &current_tab_id]() -> void {
        if (current_tab_id.has_value()) {
            auto it = tab_workspaces.find(current_tab_id.value());
            if (it != tab_workspaces.end()) {
                // 取第一个 pane 的视图来打开搜索
                auto *first_view = it->second.workspace->view_of(1);
                if (first_view) {
                    first_view->open_search();
                }
            }
        }
    };
    open_search.default_binding = au::KeyCombo{au::ModifierKey::Control, au::KeyCode::F};
    open_search.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_search));

    // 命令面板实例（`SPEC.FEAT.WS.07`）：挂在 `OverlayHost` 上作为全局模态浮层，与设置面板同级。
    // 先建实例再登记打开命令，这样 action 闭包可以捕获面板指针。
    auto command_palette = std::make_shared<au::CommandPalette>(&app.commands());
    command_palette->set_on_execute([&app](const std::string &id) -> void {
        // 框架的 `execute_selected()` 已经调过 `invoke`，这里只做收尾：面板已在 execute_selected()
        // 内部关闭，故只需记录日志（若需要）。
        AURORA_LOG_INFO("main", "command palette executed: ", id);
    });
    // 面板的中文占位符与空态文案等框架 G38/G39 回货后再走 i18n（当前仍硬编码英文）。
    // TODO(SPEC.FEAT.WS.07): 框架回货后调用 set_placeholder() / set_empty_state() 交中文词条。

    // 把命令面板挂进场景根（`OverlayHost` 的子节点 [1..] 是按需追加的浮层）：初始不打开，故只建不弹。
    // 面板的 `open()` / `close()` 会自动管理焦点作用域（push_scope / pop_scope），与设置面板同口径。
    (void)host->add_overlay(std::static_pointer_cast<au::Widget>(command_palette));

    // 命令面板的打开入口（判据文 §3 D1）：`Ctrl+Shift+P` 呼出，语义同 Tabby / Windows Terminal。
    // 框架侧 CommandPalette 已具备模态浮层 + 即时过滤 + Enter/Esc/↑/↓ 全接管能力（widget/command_palette.h）。
    au::Command open_command_palette;
    open_command_palette.id = "command_palette.open";
    open_command_palette.title = borealis::ui::settings_label("command_palette.action.open");
    open_command_palette.category = "workspace";
    open_command_palette.action = [command_palette]() -> void { command_palette->open(); };
    open_command_palette.default_binding = au::KeyCombo{au::ModifierKey::Control | au::ModifierKey::Shift, au::KeyCode::P};
    open_command_palette.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_command_palette));

    // 全屏切换（`SPEC.FEAT.WS.06`，判据文 §3 D1）：F11 在普通与全屏之间切换，ESC 不退出（避免与会话内 ESC 冲突）。
    au::Command toggle_fullscreen;
    toggle_fullscreen.id = "workspace.toggle_fullscreen";
    toggle_fullscreen.title = borealis::ui::settings_label("workspace.action.toggle_fullscreen");
    toggle_fullscreen.category = "workspace";
    toggle_fullscreen.action = [&app]() -> void {
        auto *win = app.window();
        if (!win) return;
        auto mode = win->window_mode();
        win->set_fullscreen(mode != au::WindowMode::FullScreen);
    };
    toggle_fullscreen.default_binding = au::KeyCombo{au::KeyCode::F11};
    toggle_fullscreen.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(toggle_fullscreen));

    // 撤销关闭标签（`SPEC.FEAT.WS.10`）：从闭包栈弹出最近关闭的标签规格，重建会话并插入到原位置。
    au::Command undo_close_tab;
    undo_close_tab.id = "tab.undo_close";
    undo_close_tab.title = borealis::ui::settings_label("tab.action.undo_close");
    undo_close_tab.category = "workspace";
    undo_close_tab.action = [&]() -> void {
        auto spec_opt = closed_tab_stack.pop();
        if (!spec_opt.has_value()) {
            AURORA_LOG_INFO("main", "undo close tab: stack is empty");
            return;
        }
        const borealis::ui::ClosedTabSpec &spec = spec_opt.value();
        
        // 重建会话（目前只支持本地终端，SSH 腿到货后追加分支）
        borealis::conn::LocalTerminalSpec conn_spec;
        conn_spec.command_line = spec.local_shell.empty() ? settings.connection.local_shell : spec.local_shell;
        conn_spec.working_directory = spec.startup_directory.empty() ? settings.connection.startup_directory : spec.startup_directory;
        borealis::term::UnicodeWidthPolicy width_policy;
        auto session = std::make_shared<borealis::session::Session>(
            borealis::conn::make_local_terminal_connection(conn_spec, kNominalViewport),
            kNominalViewport, settings.terminal.scrollback_limit, width_policy,
            make_terminal_defaults(settings));
        session->set_frame_wake([&surface]() -> void { surface.request_wake(); });
        session->start();
        
        // 建视口
        auto view = std::make_shared<borealis::ui::TerminalView>(
            *session, make_appearance(settings, catalog), make_interaction(settings));
        if (wire_view) {
            wire_view(*view);
        }
        
        // 建工作区分屏容器（单 pane 起步）
        borealis::ui::WorkspaceView::Hooks ws_hooks;
        ws_hooks.make_pane = [session, view](borealis::ui::PaneId /*pane*/) -> std::shared_ptr<borealis::ui::TerminalView> {
            return view;
        };
        ws_hooks.dispatch_grid = [session](borealis::ui::PaneId /*pane*/, borealis::ui::GridSize size) -> void {
            session->resize({size.columns, size.rows});
        };
        ws_hooks.teardown_pane = [](borealis::ui::PaneId /*pane*/) -> void {};
        
        auto workspace = std::make_shared<borealis::ui::WorkspaceView>(view, std::move(ws_hooks));
        
        // 生成新标签 ID：走共享的那支只增计数器，不复用已发过的身份。
        const borealis::ui::TabId id = next_tab_id++;

        // 标签名按 UTF-8 解回码点：闭包栈存的是 `encode_utf8` 的产物，逐字节折成码点会把
        // CJK 名拆成 Latin-1 乱码，重开回来的标签就成了一串问号。
        borealis::term::Utf8Decoder decoder;
        DecodeCollector collector;
        const auto *bytes = reinterpret_cast<const std::byte *>(spec.name.data());
        decoder.feed(std::span<const std::byte>(bytes, spec.name.size()), collector);
        decoder.finish(collector);
        const bool inserted = tab_strip.insert_at(spec.index_in_strip, id, collector.out);
        if (!inserted) {
            AURORA_LOG_WARN("main", "undo close tab: insert_at failed at index ", spec.index_in_strip);
            return;
        }
        
        tab_workspaces[id] = TabWorkspace{.workspace = std::move(workspace), .sessions = {session}};
        current_tab_id = id;
        
        // 选中恢复的标签并重建根节点子节点
        tab_strip.select(id);
        rebuild_root_children(root_stack, tab_workspaces, current_tab_id);
        
        AURORA_LOG_INFO("main", "restored closed tab: '", spec.name, "' at index ", spec.index_in_strip);
    };
    undo_close_tab.default_binding = au::KeyCombo{au::ModifierKey::Control | au::ModifierKey::Shift, au::KeyCode::T};
    undo_close_tab.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(undo_close_tab));

    // 调试面板（`SPEC.NF.RELI.01`）：F12 开合，打开那一刻对**全部标签的每个会话**取一次计数器快照
    // （解析降级 / 非法字节 / 背压水位三族）。快照而非活读：面板只在打开时被填一次，之后不再触碰会话，
    // 于是 `Session` 的三个 stats 访问器只在主线程这一次调用点上发生锁竞争。
    // TODO(SPEC.NF.RELI.01): 崩溃留存腿（信号钩子、dump 还是自写诊断文件、Windows/POSIX 分平台形态）
    // 与既有文档无出处可依，待人裁决后再开工，本棒不自行择一实现。
    borealis::ui::DebugPanel debug_panel{*host, app.focus()};
    au::Command open_diagnostics;
    open_diagnostics.id = "diagnostics.open";
    open_diagnostics.title = borealis::ui::settings_label("diagnostics.action.open");
    open_diagnostics.category = "workspace";
    open_diagnostics.action = [&debug_panel, &tab_workspaces]() -> void {
        std::vector<borealis::ui::DebugSessionSnapshot> snapshots;
        for (const auto &[tab_id, workspace] : tab_workspaces) {
            for (std::size_t pane = 0; pane < workspace.sessions.size(); ++pane) {
                borealis::session::Session &session = *workspace.sessions[pane];
                const auto parse = session.parse_stats();
                const auto decode = session.decode_stats();
                const auto queue = session.queue_stats();
                snapshots.push_back(borealis::ui::DebugSessionSnapshot{
                    .title = borealis::ui::settings_label(
                        "diagnostics.session",
                        {au::LocalizedString{std::to_string(tab_id)}, au::LocalizedString{std::to_string(pane + 1)}}),
                    .parse_ignored = parse.ignored,
                    .parse_cancelled = parse.cancelled,
                    .decode_replaced = decode.replaced,
                    .decode_code_points = decode.code_points,
                    .queue_pending = queue.pending,
                    .queue_peak_pending = queue.peak_pending,
                    .queue_overloads = queue.overloads,
                    .queue_merges = queue.merges,
                    .queue_yields = queue.yields,
                });
            }
        }
        debug_panel.toggle(snapshots);
    };
    open_diagnostics.default_binding = au::KeyCombo{au::KeyCode::F12};
    open_diagnostics.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_diagnostics));

    app.commands().bind_shortcuts(app.shortcuts());

    // 启动降级提示（`SPEC.FEAT.PREF.07`，裁决 7.76⑤）：`LoadOutcome` 四态里只有两条降级态会弹，弹一次即止。
    // `message` 是 ASCII 英文诊断，只进日志不上中文界面（判据文 S13①），故以「本次真的弹了」为条件——
    // 「哪两态弹」的判定只在 `StartupNotice` 里存一份，装配层不另做一遍 `outcome` 分支。
    borealis::ui::StartupNotice notice{*host, app.focus()};
    if (notice.show_if_needed(store.report())) {
        AURORA_LOG_WARN("main", "config load degraded: ", store.report().message);
    }

    app.set_on_frame([&tab_workspaces, &current_tab_id, &outbox, &panel, &tab_strip, &app, &settings,
                      &last_window_title]() -> void {
        // 排帧范围是**全部标签**每帧都排（含隐藏标签，裁决 7.47⑩）：可见性只影响绘制不影响数据。
        // 只排当前标签的后果是三处静默失灵——别的标签的 `OSC 52` 永远留在队列里（取走语义按会话
        // 记账，裁决 7.21③）、BEL 与活动角标要等用户切过去才补亮、OSC 标题不落进标签名。
        for (auto &[tab_id, workspace] : tab_workspaces) {
            for (borealis::ui::PaneId pane = 1; pane <= workspace.workspace->pane_count(); ++pane) {
                auto *v = workspace.workspace->view_of(pane);
                if (v) {
                    v->on_frame();
                }
            }
            for (auto &tag_session : workspace.sessions) {
                static_cast<void>(outbox.drain(*tag_session));

                // BEL 事件传给 TabStrip（`SPEC.FEAT.WS.04`）：取走即清零，不会被下一帧重复点亮
                if (tag_session->take_bell_triggered()) {
                    tab_strip.mark_bell_triggered(tab_id);
                }

                // 有新的网格更新（damage）即标记活动（`SPEC.FEAT.WS.04`）
                if (tag_session->has_damage()) {
                    tab_strip.mark_activity(tab_id);
                }

                // 消费 OSC 标题并更新到标签名（`SPEC.FEAT.TERM.07` UI 消费链路）
                const borealis::term::OscState osc = tag_session->osc_state();
                if (!osc.title.empty()) {
                    tab_strip.set_osc_title(tab_id, osc.title);
                }
            }
        }
        
        // 窗口标题＝**选中标签的显示名**（`SPEC.FEAT.WS.06`）：名只经 `resolve_tab_name` 这一个判定处
        //（裁决 7.43③），窗口侧再读一次 OSC 就是第二套优先级，标签条与标题栏会在手动重命名那一刻分叉。
        if (current_tab_id.has_value()) {
            std::u32string display_name;
            for (const auto &tab : tab_strip.tabs()) {
                if (tab.id == current_tab_id.value()) {
                    display_name = borealis::ui::resolve_tab_name(
                        tab.names, settings.appearance.tab_name_priority);
                    break;
                }
            }
            std::string title_utf8;
            static_cast<void>(borealis::term::encode_utf8(display_name, title_utf8));
            if (title_utf8 != last_window_title) {
                last_window_title = title_utf8;
                if (auto *win = app.window()) {
                    win->set_title(title_utf8);
                }
            }
        }
        
        panel.pump_preview();  // 预览盒是第二个会话，脏行同样按帧排（面板关着即空操作）
    });
    app.run();

    // 关停在读线程仍可能唤醒 surface 之前：需要关闭所有标签的所有会话
    for (auto &[tab_id, workspace] : tab_workspaces) {
        for (auto &session : workspace.sessions) {
            session->close();
        }
    }
    return 0;
}
