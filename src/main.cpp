#include <chrono>
#include <memory>
#include <utility>

#include "aurora/aurora.h"
#include "aurora/core/log.h"
#include "aurora/window/native_surfaces.h"
#include "borealis/conn/local_terminal.h"
#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/session/session.h"
#include "borealis/term/width.h"
#include "ui/terminal_view.h"

namespace {

/// @brief 会话的名义初始尺寸：真实行列由控件首次布局按自身尺寸派生并下发
///        （`SPEC.FEAT.XFER.01` 的 UI 取值腿），这里只要保证状态机与连接一起来有个可用的格。
constexpr borealis::session::Size kNominalViewport{80U, 24U};

}  // namespace

auto main() -> int {
    // 局部对象的声明次序即析构次序的倒序：`store` 最先声明故最后析构，`app` 最先析构——窗口与
    // 控件树都随它消散，会话与宽度判定策略仍活着，控件不会摸到已销毁的会话。
    const borealis::config::Store store;
    const borealis::config::Settings &settings = store.settings();

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

    // 选区与复制的可配项由配置逐字段搬进来：视口刻意不认识 `config`（`config` 含 `ui::PaletteSpec`，
    // 反向依赖会成模块环，架构 §2.3），故搬运点只能在装配层。
    borealis::ui::TerminalView::InteractionOptions interaction;
    interaction.word_delimiters = settings.terminal.word_delimiters;
    interaction.copy_on_select = settings.terminal.copy_on_select;
    interaction.copy = borealis::ui::CopyOptions{
        .trim_trailing_space = settings.terminal.trim_pasted_trailing_space,
        .smart_line_join = settings.terminal.smart_line_join,
        .strip_tmux_border_chars = settings.terminal.strip_tmux_border_chars,
    };

    auto view = std::make_shared<borealis::ui::TerminalView>(
        session, settings.appearance.palette,
        au::Font{.family = settings.appearance.font_family,
                 .size_pt = static_cast<float>(settings.appearance.font_size_pt),
                 .weight = 400},
        settings.appearance.viewport_padding_dp,
        std::chrono::milliseconds{settings.appearance.cursor_blink_period_ms}, std::move(interaction));
    // TODO(SPEC.FEAT.PREF.02): 配置里的光标缺省形态与闪烁档、Ambiguous 口径尚无会话侧接缝可注入，
    // 三者当前分别取状态机的 `Block` / `blinking=true` 缺省值与判定入参的 `Narrow`。
    au::Scene scene{au::Node{std::static_pointer_cast<au::Widget>(view)}};
    borealis::session::ClipboardOutbox outbox;

    au::Application app{std::move(scene), std::move(window), opts};
    // 框架不在启动时给焦点序里的首个控件派焦点（只有模态 `push_scope` 会这么做），不设这一步则
    // 按键与滚轮都路由不到视口，光标也永远停在失焦的空心描边形态。
    app.focus().set_focus(view.get());
    app.set_on_frame([&view, &outbox, &session]() -> void {
        view->on_frame();  // 先取脏行提交、再在临界区并入本地副本（顺序不可颠倒，见 session.h）
        static_cast<void>(outbox.drain(session));
    });
    app.run();

    // 关停在读线程仍可能唤醒 surface 之前：`close()` 会 join 读线程，之后不再有帧唤醒。
    session.close();
    return 0;
}
