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
#include "borealis/term/width.h"
#include "borealis/ui/font_choice.h"
#include "borealis/ui/settings_form.h"
#include "ui/settings_i18n.h"
#include "ui/settings_panel.h"
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
                                       kNominalViewport, settings.terminal.scrollback_limit, width_policy};
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
    // TODO(SPEC.FEAT.PREF.02): 配置里的光标缺省形态与闪烁档、Ambiguous 口径尚无会话侧接缝可注入，
    // 三者当前分别取状态机的 `Block` / `blinking=true` 缺省值与判定入参的 `Narrow`。
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

    app.set_on_frame([&view, &outbox, &session]() -> void {
        view->on_frame();  // 先取脏行提交、再在临界区并入本地副本（顺序不可颠倒，见 session.h）
        static_cast<void>(outbox.drain(session));
    });
    app.run();

    // 关停在读线程仍可能唤醒 surface 之前：`close()` 会 join 读线程，之后不再有帧唤醒。
    session.close();
    return 0;
}
