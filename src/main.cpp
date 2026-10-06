#include <chrono>
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
#include "borealis/term/width.h"
#include "borealis/ui/font_choice.h"
#include "borealis/ui/shortcuts_table.h"
#include "borealis/ui/settings_form.h"
#include "ui/settings_i18n.h"
#include "ui/settings_panel.h"
#include "ui/startup_notice.h"
#include "ui/terminal_view.h"

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

    borealis::conn::LocalTerminalSpec spec;
    spec.command_line = settings.connection.local_shell;
    spec.working_directory = settings.connection.startup_directory;

    borealis::term::UnicodeWidthPolicy width_policy;
    borealis::session::Session session{borealis::conn::make_local_terminal_connection(spec, kNominalViewport),
                                       kNominalViewport, settings.terminal.scrollback_limit, width_policy,
                                       make_terminal_defaults(settings)};
    // 后台读线程产出提交后叫醒主循环排帧（架构 §3.2）：`request_wake` 是线程安全的跨线程唤醒。
    session.set_frame_wake([&surface]() -> void { surface.request_wake(); });
    session.start();

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

    auto view = std::make_shared<borealis::ui::TerminalView>(
        session, make_appearance(settings, catalog), make_interaction(settings));
    // 右键菜单与多行粘贴确认都是浮层，故场景根是浮层宿主而非视口本身（裁决 7.41③）：宿主的子节点
    // [0] 是撑满窗口的视口，[1..] 是视口与设置面板按需追加的 Popup / Dialog / 面板浮层。
    auto host = std::make_shared<au::OverlayHost>(au::Node{std::static_pointer_cast<au::Widget>(view)});
    view->set_overlay_host(*host);
    // chrome 主题挂场景根：面板与后续界面件读同一份 `Theme`，而它**刻意不随终端主题联动**
    //（裁决 7.52 的 S5① / N6：切配色主题时整窗跟着变会让用户以为丢了设置）。
    auto chrome = std::make_shared<au::ThemeScope>(borealis::ui::settings_chrome_theme(),
                                                   au::Node{std::static_pointer_cast<au::Widget>(host)});
    au::Scene scene{au::Node{std::static_pointer_cast<au::Widget>(std::move(chrome))}};
    borealis::session::ClipboardOutbox outbox;

    au::Application app{std::move(scene), std::move(window), opts};
    // 框架不在启动时给焦点序里的首个控件派焦点（只有模态 `push_scope` 会这么做），不设这一步则
    // 按键与滚轮都路由不到视口，光标也永远停在失焦的空心描边形态。
    app.focus().set_focus(view.get());

    // 面板的三条接缝都在装配层兑现：装载取 `Store` 的当前配置，落盘走 `apply_form` + `replace()`，
    // 广播把刚落盘的配置重新折算成外观包与交互项交回视口（`SPEC.FEAT.PREF.02` 的「即时生效」腿）。
    borealis::ui::SettingsPanel::Hooks hooks;
    hooks.load = [&store]() -> std::vector<borealis::ui::FormEntry> {
        return borealis::config::form_entries(store.settings());
    };
    hooks.persist = [&store](const borealis::ui::SettingsForm &form) -> std::optional<std::string> {
        return store.replace(borealis::config::apply_form(form, store.settings()));
    };
    hooks.broadcast = [&view, &store, &catalog](const borealis::ui::SettingsForm &form) -> void {
        const borealis::config::Settings next = borealis::config::apply_form(form, store.settings());
        view->apply_appearance(make_appearance(next, catalog));
        view->apply_interaction_options(make_interaction(next));
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
    app.commands().bind_shortcuts(app.shortcuts());

    // 启动降级提示（`SPEC.FEAT.PREF.07`，裁决 7.76⑤）：`LoadOutcome` 四态里只有两条降级态会弹，弹一次即止。
    // `message` 是 ASCII 英文诊断，只进日志不上中文界面（判据文 S13①），故以「本次真的弹了」为条件——
    // 「哪两态弹」的判定只在 `StartupNotice` 里存一份，装配层不另做一遍 `outcome` 分支。
    borealis::ui::StartupNotice notice{*host, app.focus()};
    if (notice.show_if_needed(store.report())) {
        AURORA_LOG_WARN("main", "config load degraded: ", store.report().message);
    }

    app.set_on_frame([&view, &outbox, &session, &panel]() -> void {
        view->on_frame();  // 先取脏行提交、再在临界区并入本地副本（顺序不可颠倒，见 session.h）
        static_cast<void>(outbox.drain(session));
        panel.pump_preview();  // 预览盒是第二个会话，脏行同样按帧排（面板关着即空操作）
    });
    app.run();

    // 关停在读线程仍可能唤醒 surface 之前：`close()` 会 join 读线程，之后不再有帧唤醒。
    session.close();
    return 0;
}
