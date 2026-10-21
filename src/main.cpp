#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/app/file_dialog.h"
#include "aurora/core/log.h"
#include "aurora/render/font_discovery.h"
#include "aurora/window/native_surfaces.h"
#include "borealis/conn/local_terminal.h"
#include "borealis/cli/handoff.h"
#include "borealis/cli/startup.h"
#include "borealis/config/form_transfer.h"
#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/config/themes.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/session/session.h"
#include "borealis/session/session_log.h"
#include "borealis/term/clipboard_read.h"
#include "borealis/term/keymap.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"
#include "borealis/ui/broadcast.h"
#include "borealis/ui/font_choice.h"
#include "borealis/ui/osc7.h"
#include "borealis/ui/shortcuts_table.h"
#include "borealis/ui/settings_form.h"
#include "borealis/ui/tab_strip.h"
#include "borealis/ui/closed_tab_stack.h"
#include "borealis/ui/url.h"
#include "conn/key_model.h"
#include "conn/key_push.h"
#include "conn/key_store.h"
#include "conn/profile_views.h"
#include "conn/serial_connection.h"
#include "conn/serial_model.h"
#include "conn/ssh_connection.h"
#include "conn/telnet_connection.h"
#include "conn/tunnel_client.h"
#include "platform/instance_channel.h"
#include "platform/open_url.h"
#include "platform/process_args.h"
#include "platform/serial.h"
#include "platform/user_dir.h"
#include "ui/active_window_tracker.h"
#include "ui/broadcast_banner.h"
#include "ui/connection_sidebar.h"
#include "ui/connection_wizard.h"
#include "ui/credential_prompt.h"
#include "ui/debug_panel.h"
#include "ui/keys_format.h"
#include "ui/keys_panel.h"
#include "ui/serial_format.h"
#include "ui/settings_i18n.h"
#include "ui/settings_panel.h"
#include "ui/sftp_panel.h"
#include "ui/startup_notice.h"
#include "ui/tab_bar.h"
#include "ui/terminal_view.h"
#include "ui/tunnel_panel.h"
#include "ui/workspace_view.h"
#include <aurora/widget/command_palette.h>

namespace {

/// @brief 会话的名义初始尺寸：真实行列由控件首次布局按自身尺寸派生并下发
///        （`SPEC.FEAT.XFER.01` 的 UI 取值腿），这里只要保证状态机与连接一起来有个可用的格。
constexpr borealis::session::Size kNominalViewport{80U, 24U};

/// @brief Telnet 的发送行尾（裁决 7.112 D8① 的固定档，不可配）：Telnet NVT 的行结束是 CR LF
///        （RFC 854），故本地/SSH 那一条缺省 `Lf` 在这里不适用。装配层只此一枚常量，会话的写出
///        折算与视口的粘贴档都读它——两处各写一遍字面量就会分叉。
constexpr borealis::term::LineEnding kTelnetLineEnding = borealis::term::LineEnding::Crlf;

/// @brief 隧道状态事件汇（裁决 7.97 D6①）：工作线程上只写「最新值」快照，
///        UI 线程经 snapshot Hook 逐帧合并——不排队、不积压（AGENTS §25 的
///        合成后最终值形态）。attempt 由本汇在 Backoff 事件上自增（D6①）。
struct TunnelSink final : borealis::conn::TunnelEvents {
    std::shared_ptr<std::mutex> mutex;
    std::shared_ptr<std::map<std::string, borealis::ui::TunnelRuntime>> runtimes;
    std::string id;

    auto on_tunnel_state(borealis::conn::TunnelState state, borealis::conn::TunnelError error,
                         int bound_port) -> void override {
        const std::lock_guard lock{*mutex};
        auto &cell = (*runtimes)[id];
        cell.state = state;
        cell.error = error;
        if (bound_port > 0) {
            cell.bound_port = bound_port;  // -R 0 端口择定回报（判据 4）。
        }
        if (state == borealis::conn::TunnelState::Backoff) {
            ++cell.attempt;
        }
    }
};

/// @brief 装配层持有的一条隧道：传输腿对象 + 它的事件汇（sink 先声明、后销毁——
///        Tunnel 持其引用，析构次序上必须先走 tunnel）。
struct TunnelEntry {
    std::unique_ptr<TunnelSink> sink;
    std::unique_ptr<borealis::conn::Tunnel> tunnel;
};

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
/// @param line_ending 本条连接的发送行尾（裁决 7.110 D11①）：粘贴的 `Convert` 档与键入 Enter
///        落地的是同一份折算，故它与 `Session` 的构造入参必须同源——两处各取一份就会出现
///        「敲回车一个样、粘多行另一个样」。本地/SSH 传缺省 `Lf`，即既有行为逐字节不变；
///        串口传档案里那一档，Telnet 传固定 `Crlf`（`SPEC.FEAT.CONN.06` 的裁决 7.112 D8①）。
[[nodiscard]] auto make_interaction(const borealis::config::Settings &settings,
                                    borealis::term::LineEnding line_ending =
                                        borealis::term::LineEnding::Lf)
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
    interaction.paste.line_ending = line_ending;  // CONN.05 的行尾设置到此接线（块间隔需求未开键）。
    // 链接的三枚键在同一次搬运里走完（`SPEC.FEAT.INTERACT.05` 判据 5／稿 D8①）：白名单在**装配层**
    // 拆一次，视口因此不认识拆串规则，`"http,https"` 那枚缺省串也就只有配置侧一处定义。该键被清空
    // 的表现是「一切不可点」而不是「未配故放行」——缺省空表就是那个意思，设置页旁注负责把话说清。
    interaction.link.scan = borealis::ui::LinkScanOptions{
        .schemes = borealis::ui::parse_link_schemes(settings.terminal.link_schemes)};
    interaction.link.osc8 = settings.terminal.link_osc8;
    interaction.link.heuristic = settings.terminal.link_heuristic;
    interaction.command_block_separator = settings.terminal.command_block_separator;
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

/// @brief 从配置装出会话的编解码档（`SPEC.FEAT.TERM.09`）。
///
/// 与 `make_terminal_defaults` 同一条生效口径：**建会话那一刻**取一次，运行期改配置不重放既有会话
/// （判据文 §0 边界②）。取的是 `terminal.encoding` 这一全局缺省档——串口自身的
/// `connection.serial.encoding`（缺省 GB18030，裁决 7.6）要等串口连接腿落地才有人读它，本棒不预判。
[[nodiscard]] auto make_session_encoding(const borealis::config::Settings &settings)
    -> borealis::term::SessionEncoding {
    return borealis::term::SessionEncoding{
        .name = settings.terminal.encoding,
        .unrepresentable = settings.terminal.unrepresentable,
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

/// @brief 让用户点名一个要写出的文件；空串＝取消。
///
/// 失败（平台对话框起不来）与取消走同一个空串，因为面板那一条腿的判据就是「没给路径就整条不跑、
/// 连留痕都不写」，把「取消」读成一次失败反而会在界面上谎报「导出失败」。
[[nodiscard]] auto picked_save_path() -> std::string {
    aurora::file_dialog::Options opts;
    opts.title = "Export settings";  // 平台对话框的标题由 OS 呈现，不经本仓词条表（S13② 的同一条口径）
    opts.filters = {{"JSON", {"*.json"}}};
    const au::Result<std::string> result = aurora::file_dialog::save_file(opts);
    return result.ok() ? result.value() : std::string{};
}

/// @brief 让用户点名一个要读入的文件；空串＝取消（同上）。
[[nodiscard]] auto picked_open_path() -> std::string {
    aurora::file_dialog::Options opts;
    opts.title = "Import settings";
    opts.filters = {{"JSON", {"*.json"}}};
    const au::Result<std::vector<std::string>> result = aurora::file_dialog::open_file(opts);
    if (!result.ok() || result.value().empty()) {
        return std::string{};
    }
    return result.value().front();
}

/// @brief 定长 UTF-8 串的码点收集器（与 `session.cpp` 的 `BufferSink` 同一形态，装配侧另有一份）。
class DecodeCollector final : public borealis::term::CodePointSink {
  public:
    auto on_code_point(char32_t cp) -> void override { out.push_back(cp); }

    std::u32string out;
};

/// @brief 一次扫面的完整任务体（worker 线程上跑）：规范化（含可达性 stat）→ 扫盘 →
///        排序 → 写快照。动作任务的尾巴也交它——「每个动作之后都有一次重扫」（稿 §5）。
auto run_keys_scan(const std::shared_ptr<std::mutex> &mutex,
                   const std::shared_ptr<borealis::ui::KeysSnapshot> &latest,
                   const std::vector<std::string> &raw_dirs, std::string_view user_dir) -> void {
    auto probe = [](std::string_view path) -> bool {
        std::error_code ec;
        return std::filesystem::is_directory(std::filesystem::path{path}, ec);
    };
    auto dirs = borealis::conn::normalize_key_dirs(raw_dirs, user_dir, probe);
    auto rows = borealis::conn::scan_keys(dirs);
    auto paths = std::vector<std::string>{};
    paths.reserve(dirs.size());
    for (const auto &dir : dirs) {
        paths.push_back(dir.path);
    }
    static_cast<void>(borealis::conn::sort_rows(rows, paths));
    const std::lock_guard lock{*mutex};
    latest->dirs = std::move(dirs);
    latest->rows = std::move(rows);
}

/// @brief 行的公钥单行（判据 4/6 交出的字节；快照里没有该行或拼不出即空串）。
[[nodiscard]] auto keys_line_of(const borealis::ui::KeysSnapshot &snapshot,
                                const std::string &path) -> std::string {
    for (const auto &row : snapshot.rows) {
        if (row.path == path) {
            return borealis::ui::KeyRow{row, borealis::ui::KeysOpKind::Idle}.public_text();
        }
    }
    return {};
}

/// @brief 密钥装配的串行 worker（稿 §5 的 KeysWorker；裁决 7.106 D4①/D6①/D7①）：
///        单线程任务队列，同一时刻只跑一把钥匙的事；每个任务的终值写 mutex 保护的
///        latest-value 快照，UI 线程经 snapshot Hook 逐帧取副本——与隧道快照同族
///        （状态是可覆盖的量，不排事件队列；AGENTS §25 的「合成后的最终值」形态）。
///        在途标记（op/op_path）在**提交时**发布：UI 立即置灰与点名，不等队列轮到；
///        排队中那一档没有独立措辞（`keys_format.h` 头注⑵同口径），尾巴上把标记
///        交给队列里的下一件、没有就归空闲。
class KeysWorker final {
  public:
    KeysWorker() : thread_{[this]() -> void { drain(); }} {}
    KeysWorker(const KeysWorker &) = delete;
    auto operator=(const KeysWorker &) -> KeysWorker & = delete;
    ~KeysWorker() {
        {
            const std::lock_guard lock{*mutex_};
            stopping_ = true;
        }
        cv_->notify_all();
        thread_.join();  // 排空队列再收工：已提交的动作都跑完，不留半截实现（第 3 条）。
    }

    /// @brief 提交一个动作：先发布在途标记（UI 立即置灰/点名），再入队。
    auto submit(borealis::ui::KeysOpKind op, std::string op_path, std::function<void()> run)
        -> void {
        {
            const std::lock_guard lock{*mutex_};
            latest_->op = op;
            latest_->op_path = op_path;
            queue_.push_back(Item{op, std::move(op_path), std::move(run)});
        }
        cv_->notify_all();
    }

    /// @brief 快照的一份副本（UI 线程逐帧调用；与隧道 snapshot Hook 同一形态）。
    [[nodiscard]] auto snapshot() const -> borealis::ui::KeysSnapshot {
        const std::lock_guard lock{*mutex_};
        return *latest_;
    }

    /// @brief 不经队列的即时留痕（一键复制这类瞬时动作：没有任务可排，也就没有终值）。
    auto publish_notice(std::string key, std::string arg) -> void {
        const std::lock_guard lock{*mutex_};
        latest_->notice_key = std::move(key);
        latest_->notice_arg = std::move(arg);
    }

    /// @brief 任务体要写的两件共享物（快照与其锁）：任务闭包按值捕获它们，
    ///        worker 线程因此不摸本对象——栈上的成员先于线程消失也不踩空。
    [[nodiscard]] auto mutex() const -> const std::shared_ptr<std::mutex> & { return mutex_; }
    [[nodiscard]] auto latest() const -> const std::shared_ptr<borealis::ui::KeysSnapshot> & {
        return latest_;
    }

  private:
    struct Item {
        borealis::ui::KeysOpKind op{};
        std::string op_path;
        std::function<void()> run;
    };

    auto drain() -> void {
        while (true) {
            Item item;
            {
                std::unique_lock lock{*mutex_};
                cv_->wait(lock, [this]() -> bool { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) {
                    return;
                }
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            item.run();
            const std::lock_guard lock{*mutex_};
            if (queue_.empty()) {
                latest_->op = borealis::ui::KeysOpKind::Idle;
                latest_->op_path.clear();
            } else {
                latest_->op = queue_.front().op;
                latest_->op_path = queue_.front().op_path;
            }
        }
    }

    std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
    std::shared_ptr<std::condition_variable> cv_ = std::make_shared<std::condition_variable>();
    std::shared_ptr<borealis::ui::KeysSnapshot> latest_ =
        std::make_shared<borealis::ui::KeysSnapshot>();
    std::deque<Item> queue_;
    bool stopping_ = false;
    std::thread thread_;
};

/// @brief 一扇工作区窗的「呈现半区 + 逐窗账目」（`SPEC.FEAT.WS.03` 批 1，稿 §2 那份「每窗一份」清单）。
///
/// 成员只放稿 §2 判给每窗的那几件：后端窗口与其 surface、宿主登记的 `WindowId`、浮层宿主、根
/// Column、标签栏控件、广播横幅、本窗的标签真值源与闭包栈、本窗当前标签号。应用级那一份（Settings /
/// Store、四张 `TabId` 连接表、`next_tab_id`、广播组本体、凭据链、四面板）不进这里。
///
/// **批 1 的归属、批 3 的可复用**：`tab_bar` 与 `banner` 两支仍由装配层填进成员，但填它们的
/// 那段已是**按窗**的函数（`make_tab_bar(window)` / `make_broadcast_banner(window)`），并被
/// `assemble_window_content(window)` 串成一条入根序列，故第二扇窗走的是同一条装配路径而不是主窗的
/// 特例。`surface` 是裸指针而不是 `unique_ptr` 的派生：窗口本体在交给宿主之后归 `WindowHost`，而
/// surface 归窗口，于是宿主活着这一枚就一直有效，反过来谁都不必为了取 surface 而替窗口续命。
struct WorkspaceWindow {
    au::WindowOptions options{};
    std::unique_ptr<au::Window> window{};  ///< 交出之后为空（`take_window()`）
    au::Surface *surface = nullptr;
    au::WindowId id = au::AURORA_INVALID_WINDOW_ID;  ///< 登记进 `Application` 之后才有效
    std::shared_ptr<au::OverlayHost> overlay_host{};
    std::shared_ptr<au::Column> root_column{};
    std::shared_ptr<borealis::ui::TabBarWidget> tab_bar{};
    std::shared_ptr<borealis::ui::BroadcastBanner> banner{};
    borealis::ui::TabStrip tab_strip;
    borealis::ui::ClosedTabStack closed_tab_stack;
    std::optional<borealis::ui::TabId> current_tab_id;
    /// 上一次写到**本窗**标题栏的串：逐帧都调 `set_title` 就是每帧一次 `WM_SETTEXT`，而这个量在用户
    /// 不换标签、对端不改 OSC 标题时根本不动。差分账按窗记——两扇窗的当前标签可以不同，共用一格
    /// 就会让后写的那一扇把前一扇的串当成「没变」而漏下发。
    std::string last_title;
    /// 本窗的关闭确认框：盖在本窗的浮层宿主上（稿 §2「每窗一份浮层」），生存期须跨过
    /// Show→答话那一段，故由本结构持有而不是装配层的一只局部。两扇窗共用一只对话框会让 B 窗的
    /// 那句确认弹在 A 窗脸上，而答话回来的 `raw_id` 又是按 A 窗那栏算的。
    std::shared_ptr<au::Dialog> close_confirm{};
    bool close_confirm_mounted = false;  ///< 首次挂入后复用同一实例换文案（`add_overlay` 只走一次）

    /// @brief 交出后端窗口的所有权：`Application` 的宿主接管它。
    [[nodiscard]] auto take_window() -> std::unique_ptr<au::Window> { return std::move(window); }
};

/// @brief 造一扇工作区窗的地基：后端窗口 + 浮层宿主 + 撑满窗宽的根 Column。
/// @return 建窗失败即空指针（后端错误原文已在工厂里落日志），调用方**不得**接着登记宿主——
///         稿 §5 的「建窗」不变量：没建成窗就不许留下半个账目。
[[nodiscard]] auto open_workspace_window(std::string title) -> std::unique_ptr<WorkspaceWindow> {
    auto out = std::make_unique<WorkspaceWindow>();
    out->options.size = au::Size{.width = 960.0F, .height = 640.0F};
    out->options.title = std::move(title);
    auto created = au::create_native_window(out->options);
    if (!created) {
        AURORA_LOG_ERROR("main", "native window creation failed: ", created.error().message);
        return nullptr;
    }
    out->window = std::move(created.value());
    out->surface = &out->window->surface();
    // 右键菜单与多行粘贴确认都是浮层，故场景根是浮层宿主而非视口本身（裁决 7.41③）：宿主的子节点
    // [0] 是标签栏，[1] 是广播横幅，[2] 是当前工作区，[3..] 是按需追加的 Popup / Dialog / 面板浮层。
    out->overlay_host = std::make_shared<au::OverlayHost>();
    // 纵向容器承载：OverlayHost 只管理浮层，常规内容须走普通容器；交叉轴 Stretch 使标签栏与工作区
    // 均撑满窗宽。
    out->root_column = std::make_shared<au::Column>();
    out->root_column->modifier.set(au::Modifier{}.fill_max_size());
    out->root_column->set_cross_axis_alignment(au::CrossAxisAlignment::Stretch);
    return out;
}

}  // namespace

auto main(int argc, char **argv) -> int {
    // ---- CLI 启动请求（`SPEC.FEAT.INTEG.04`，稿 §2 / §7 / §8）--------------------------------
    // 参数腿把 `argv` 折成 UTF-8 表（Windows 走 `CommandLineToArgvW`，窄 `argv` 在 CJK 路径上丢字）。
    // `--help` 与用法错误在建窗、读盘、认领通道**之前**就地消化：稿 §2 明写 `--help` 不占用单实例
    // 通道，而错误退出若先建窗就会闪一扇没内容的窗口。
    const std::vector<std::string> arguments = borealis::platform::process_arguments(argc, argv);
    const std::span<const std::string> all_arguments{arguments};
    const std::span<const std::string> operands =
        all_arguments.empty() ? all_arguments : all_arguments.subspan(1);
    const borealis::cli::StartupParse parsed = borealis::cli::parse_startup_args(operands);
    if (parsed.help) {
        AURORA_LOG_RAW("cli", borealis::cli::startup_usage(), "\n");
        return 0;
    }
    if (!parsed.error.empty()) {
        AURORA_LOG_RAW("cli", parsed.error, "\n", borealis::cli::startup_usage(), "\n");
        return 2;
    }
    std::optional<borealis::cli::StartupRequest> startup_request = parsed.request;
    // 相对 `--cwd` 在**发起进程**里折算成绝对路径（稿 §5.1）：转发之后首实例的进程目录与用户那个
    // 不是同一个，把折算留给接收侧就等于让目录随首实例怎么起的而变。存在性不在这里判——那是
    // 首实例执行动作时的事（本进程可能就是首实例，也可能是已被转交的转发器）。
    if (startup_request.has_value() && !startup_request->cwd.empty()) {
        std::error_code ec;
        const std::filesystem::path raw{std::u8string{startup_request->cwd.begin(),
                                                       startup_request->cwd.end()}};
        const std::u8string normalized = std::filesystem::absolute(raw, ec).lexically_normal().u8string();
        if (ec) {
            AURORA_LOG_RAW("cli", "cannot resolve --cwd to an absolute path\n");
            return 2;
        }
        // 按**指针+长度**落进 `std::string`，不交 `u8string` 的迭代器区间：迭代器版在本机 libstdc++
        // 上把长度算错（实测一条 21 字节的目录折出 1.8e19 的 capacity，`std::length_error` 直接
        // terminate，机制未查）。`platform` 那几处字节搬运同样走 `reinterpret_cast<const char *>`。
        startup_request->cwd.assign(reinterpret_cast<const char *>(normalized.data()),
                                    normalized.size());
    }

    // 局部对象的声明次序即析构次序的倒序：`store` 最先声明故最后析构，`app` 最先析构——窗口与
    // 控件树都随它消散，会话与宽度判定策略仍活着，控件不会摸到已销毁的会话。
    borealis::config::Store store;  // 面板要 `replace()`，故本对象不再 `const`
    const borealis::config::Settings &settings = store.settings();
    // OSC 52 读方向的授权闸门（`SPEC.FEAT.CONN.12` 判据 ①②）：全 app 一份，会话状态机与视口各持
    // 一枚观察指针——同一枚 atomic 同时决定「登记不登记」与「问不问」，分两处就会造出「状态机收了
    // 一条请求、界面那侧却按另一档答掉」的窗口。声明紧挨 `store` 故**几乎最后析构**：会话线程现读
    // 它时结构上不可能摸到已消散的对象。初值即已落盘那一档；未接线的会话读不到闸门，于是落在
    // 「远端读不到剪贴板」那侧——漏接线的后果必须是读不到，不是谁都读得到。
    borealis::term::ClipboardGate clipboard_gate{settings.terminal.clipboard_read};
    // 闸门跟着**内存值**走，不跟着落盘成败走：`Store::replace()` 的口径是「内存值总是更新、只有落盘
    // 可能失败」（`SPEC.FEAT.PREF.07` 的损坏降级线），而广播腿读的也是同一份内存值 ⇒ 若这里以「写盘
    // 成功」为条件，就会出现「屏上已按新档、闸门还停旧档」那种三向分叉。能改动这一档的入口只有三条
    // （面板保存、快照回滚、配置导入），其余 `replace()` 调用只搬档案 / 隧道 / 密钥目录那几张表。
    auto sync_clipboard_gate = [&]() -> void {
        clipboard_gate.set_policy(settings.terminal.clipboard_read);
    };
    // 「已记住的同域」名单（`SPEC.FEAT.INTERACT.05` 判据 4／稿 D9①）：全 app 一份，声明紧挨 `store`
    // 因而同样**几乎最后析构**——视口只持观察指针，名单本体不会先于控件消散。它与 `clipboard_gate`
    // 的差别在落盘归属：这一枚是**运行期数据而非偏好**，设置面板不画它，故面板那三条持久化入口
    // （保存 / 回滚 / 导入）都搬不到它，只能由它自己的变化回调写 `connection.trusted_url_hosts`
    // 那枚走 `kNonPreferenceLeaves` 例外通道的数据叶子（`utest_settings_catalog` 计数闸认它）。
    borealis::ui::TrustedLinkHosts link_hosts;
    // 灌入既有名单**不触发**回调（`load` 的口径：灌入不是变化），于是启动路径上不写盘——
    // 与「装载不产生任何写入」（裁决 7.26⑤）同一条纪律。
    link_hosts.load(settings.connection.trusted_url_hosts);
    // 走到这一步的唯一时刻是用户在确认框里点了「记住该域并打开」。交回的是**整表快照**而非增量，
    // 故本闭包不需要知道名单内部怎么排序；落盘是同步小文件写（临时文件 + rename），与面板保存那条腿
    // 同一档，不属 AGENTS.md §4.5 第 25 条要避让的阻塞 IO。失败的后果只有「内存已记、盘上没记」，
    // 即重启后同域再问一次——fail-safe 的方向是**多问**，不是少问。
    link_hosts.set_change_sink([&store](std::span<const std::string> hosts) -> void {
        borealis::config::Settings next = store.settings();
        next.connection.trusted_url_hosts.assign(hosts.begin(), hosts.end());
        static_cast<void>(store.replace(std::move(next)));
    });
    // 生产打开腿的装填（稿 §5 第 4 条）：`Presentation::open_url` **刻意没有**「留空即调本腿」的回落
    //（稿 F15 记的正是「测试里接上生产腿＝每案开一扇浏览器窗口」那次事故），所以这里是指向它的
    // **唯一**一处。三处建视口都经 `wire_view`，装填也就只有这一处。
    borealis::ui::TerminalView::Presentation production_presentation;
    production_presentation.open_url = [](std::string_view target) -> void {
        // 分档结果不在这里再报一遍：`platform/open_url.h` 那条 `@return` 写明除 `Opened` 外每一档都已在
        // 实现侧记过日志，调用方重复一条只会让同一件事有两处措辞。本闭包的全部职责就是「把这一指
        // 向那条腿」，而它返回值在这里没有可处置的对象——「没开成功」的最坏后果就是屏上没反应。
        static_cast<void>(borealis::platform::open_url(target));
    };
    // 词条必须在任何控件绘制之前装填：查表失败回退空文本，晚装就会让首帧闪一下空标签。
    borealis::ui::install_settings_strings();

    // 建窗之前的两道校验（稿 §5.4）：档案可解析、动作与档案类型相容。任一不符即退出 2，不建窗、
    // 不启动 shell——用户敲错一个字母时不该看见一扇空窗口。档案表读的是**内存值**，与首实例执行
    // 那一格同源，故这里只判「能不能解析」，不判运行期连不连得上。
    if (startup_request.has_value()) {
        std::optional<borealis::conn::ConnectionType> profile_type{};
        if (!startup_request->profile.empty()) {
            const borealis::cli::ProfileResolution hit =
                borealis::cli::resolve_profile(settings.profiles, startup_request->profile);
            if (hit.profile == nullptr) {
                AURORA_LOG_RAW("cli", hit.error, "\n");
                return 2;
            }
            profile_type = hit.profile->type;
        }
        if (const std::string problem =
                borealis::cli::startup_policy_error(*startup_request, profile_type);
            !problem.empty()) {
            AURORA_LOG_RAW("cli", problem, "\n");
            return 2;
        }
    }

    // ---- 单实例通道（稿 §3.3）--------------------------------------------------------------
    // transport 线程只做「入本仓有界队列 + `request_wake()`」，档案解析与开标签都在主线程帧边界
    // 执行。`surface` 此刻还没建，故唤醒目标是一枚可空原子指针：`app.run()` 之前补上、返回之后
    // 立刻撤下，于是线程要么醒得着主循环，要么谁也吵不到正在消散的 surface。这三样的声明必须
    // 早于 `instance_listener`——析构次序倒过来就成了「线程 join 时队列已经没了」。
    constexpr std::size_t kMaxPendingStartupRequests = 8;
    std::mutex startup_mutex;
    std::deque<std::shared_ptr<borealis::platform::InstanceRequest>> startup_queue;
    std::atomic<au::Surface *> startup_wake{nullptr};
    // 帧 ↔ 字节的换算只有这两处用：载荷语义归 `cli`，平台通道只见不透明字节（稿 §6）。
    auto frame_bytes = [](const std::string &text) -> std::span<const std::uint8_t> {
        return std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
    };
    auto wire_of = [](const std::optional<std::vector<std::uint8_t>> &bytes) -> std::string {
        // 同样按指针+长度搬，不走迭代器区间（上面 `--cwd` 那处注释记的就是迭代器版实测炸过的形状）。
        return bytes.has_value() ? std::string(reinterpret_cast<const char *>(bytes->data()),
                                               bytes->size())
                                 : std::string{};
    };
    auto reply_wire = [&wire_of](bool ok, std::string_view message) -> std::string {
        return wire_of(borealis::cli::encode_startup_reply(
            borealis::cli::StartupReply{.ok = ok, .message = std::string{message}}));
    };
    const std::string instance_key = borealis::platform::instance_default_key();
    borealis::platform::InstanceListenResult claimed = borealis::platform::instance_listen(
        instance_key,
        [&startup_mutex, &startup_queue, &startup_wake, &reply_wire](
            std::shared_ptr<borealis::platform::InstanceRequest> pending) -> void {
            au::Surface *wake{nullptr};
            bool queued{false};
            {
                const std::lock_guard lock{startup_mutex};
                queued = startup_queue.size() < kMaxPendingStartupRequests;
                if (queued) {
                    startup_queue.push_back(std::move(pending));
                }
                wake = startup_wake.load();
            }
            if (!queued) {
                // 满了当场回 busy：排队会把「点了没反应」推到用户已经放弃之后，而转发端那 2 s 预算
                // 本来就是给「首实例卡住」准备的（稿 §10 第 6 条）。
                pending->complete(reply_wire(false, "the running instance is busy"));
                return;
            }
            if (wake != nullptr) {
                wake->request_wake();
            }
        });

    std::unique_ptr<borealis::platform::InstanceListener> instance_listener;
    if (claimed.role == borealis::platform::InstanceRole::Existing) {
        // 已有实例：本进程只是转发器，退出码就是那一格的语义（稿 §7）。
        const std::string request_wire =
            wire_of(startup_request.has_value()
                        ? borealis::cli::encode_startup_request(*startup_request)
                        : std::nullopt);
        if (request_wire.empty()) {
            AURORA_LOG_RAW("cli", "startup request does not fit the handoff frame\n");
            return 2;
        }
        const borealis::platform::InstanceForwardResult forwarded = borealis::platform::instance_forward(
            instance_key, std::string_view{request_wire}, borealis::platform::kInstanceReplyTimeoutMs);
        if (!forwarded.delivered) {
            AURORA_LOG_RAW("cli",
                           forwarded.error.empty() ? std::string_view{"the running instance is unreachable"}
                                                   : std::string_view{forwarded.error},
                           "\n");
            return 3;
        }
        const std::optional<borealis::cli::StartupReply> reply =
            borealis::cli::decode_startup_reply(frame_bytes(forwarded.reply));
        if (!reply.has_value()) {
            AURORA_LOG_RAW("cli", "the running instance sent an unreadable reply\n");
            return 3;
        }
        if (!reply->ok) {
            AURORA_LOG_RAW("cli", reply->message, "\n");
            return 2;
        }
        return 0;
    }
    if (claimed.role == borealis::platform::InstanceRole::Failed) {
        // 通道不可用不等于不能启动：这一轮只是没有单实例语义。留一条 warn 而不是静默起第二实例，
        // 否则同一份配置目录会被两个进程同时写。
        AURORA_LOG_WARN("main", "single-instance channel unavailable: ", claimed.error);
    } else {
        instance_listener = std::move(claimed.listener);
    }

    // 工作区窗的持有（稿 §2／§6 批 1）：`unique_ptr` 的 **vector** 而不是按值 vector——装配层的成员
    // 与那二十来条闭包按引用吃第一扇窗（下面那批别名），按值容器一次扩容就把对象搬走、引用当场悬垂。
    // 本批生产仍只开一扇 ⇒ 行为与批 1-A 之前逐字相同；开第二扇的入口在批 3（稿 §6）。
    //
    // **析构次序**：本容器声明在最前，于是它**最后**析构，`root_column` 里那份「当前那一格的视口」
    // 也随之活得比 `tab_workspaces` 久。这一条之所以仍然安全，靠的是 `TabWorkspace` 自己的成员次序：
    // 会话容器声明在 `workspace` 之前，故每一格的视口都在它背后的会话之前散——容器多持一份只让**散
    // 得更晚**，不会让视口活过自己的会话。真正会出事的是 `perform_close` 那条中途撤账的路径（根上还
    // 持着同一份时必须先摘根再 erase，那边已按这条写）。批 4「退出序遍历全部窗」要显式化的就是这
    // 一整段：一扇窗的呈现半区要在它还认得自己那些会话的时候散掉。
    auto first_workspace = open_workspace_window("Borealis");
    if (first_workspace == nullptr) {
        return 1;  // 建窗失败原文已由工厂落日志；这里不建宿主、不读盘、不起会话。
    }
    std::vector<std::unique_ptr<WorkspaceWindow>> workspace_windows;
    workspace_windows.push_back(std::move(first_workspace));
    WorkspaceWindow &first_window = *workspace_windows.front();
    // 别名过渡：批 1 只把**归属**换到 `WorkspaceWindow` 上，装配闭包仍按老名字吃第一扇窗。批 2 把「逐窗
    // 才正确」的那几条腿改按 owner 取了（会话唤醒取属主 surface、视口浮层宿主与焦点管理器按属主窗、帧泵
    // 的角标与标签名写给属主标签条、窗口标题逐窗差分写回），剩下的别名是**布局重建、面板与命令闭包**三条
    // 未收敛的腿：前者的按窗过滤与后两者的「搬窗」落点在批 4，命令落点按活动窗解析在批 3（稿 §6）。
    // `opts` 与 `window` 两支不另设别名：前者只在交给宿主那一句里读，后者当场交出所有权。
    au::Surface &surface = *first_window.surface;

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

    // WS.01 多标签真值源 + 闭包栈（WS.10 撤销用）：两份都**按窗一份**（稿 §2）。闭包栈那枚别名在批 3-C
    // 之后没有读者了——撤销重开改按 tracker 解析落点窗、直接读 `window.closed_tab_stack`，留着别名就是
    // 留第二处真值源（同 `root_column` 那枚别名的处理），故删除。`tab_strip` 这枚仍有读者（横幅刷新那
    // 一处按窗读的是主窗那一条，属批 4 的逐窗横幅账目）。`next_tab_id` 刻意**不在**这里——它是进程级
    // 一支（稿 §0 的 F10），两扇窗共用才不撞号。
    borealis::ui::TabStrip &tab_strip = first_window.tab_strip;

    // 标识只增不复用（与 PaneTree 的容器标识同一条理由，裁决 7.42⑩）：新建与撤销重开**共用**这一支
    // 计数器，否则「关一个再重开」造出的新身份会撞进仍存活的标签里，`add`/`insert_at` 的重复即静默失败。
    borealis::ui::TabId next_tab_id = 1;

    // 每个标签对应一个 WorkspaceView（支持该标签内的分屏）
    //
    /// @brief 一格会话的料：会话本体，与该格视口粘贴档要用的行尾（裁决 7.110 D11① 的同源口径）。
    struct PaneBundle {
        std::shared_ptr<borealis::session::Session> session{};
        borealis::term::LineEnding line_ending{borealis::term::LineEnding::Lf};
    };
    /// @brief 切分新格的会话工厂：@p origin 是被切那一格；返回空即**这一类标签不能切分**（串口独占）。
    using PaneSessionFactory =
        std::function<std::optional<PaneBundle>(borealis::ui::PaneId origin)>;
    //
    // 成员次序是**承重的**（`SPEC.FEAT.CONN.12` 批 2 实测 + `SPEC.FEAT.WS.02` 稿 §5）：
    // `sessions_by_pane` 必须声明在 `workspace` 之前，于是它后析构——视口在 `~TerminalView` 里要给挂起
    // 的读请求回写撤账空响应，那一笔写要摸到会话。每格一会话之后，会话的唯一拥有者就是这张表，
    // 原先两枚闭包**按值**捕获 `shared_ptr<Session>` 那层「替视口撑住会话」的保险随之撤除（它补的
    // 正是「一个标签只有一份会话」那个洞）。表还是 `std::map`：插入只新增节点、不搬动其他节点，
    // 所以正在执行的接缝闭包不会因为同一标签又开了一格而悬垂。
    struct TabWorkspace {
        // 归属那一扇窗（`SPEC.FEAT.WS.03` 批 2，稿 §2）。存指针而不是 `WindowId`：首标签在
        // `Application` 登记之前就要建好（根 Column 得先持着它才进得了场景根），那一刻号还不存在；
        // 而容器是 `unique_ptr` 的 vector，堆上对象的地址在窗的生命周期内不动。要号的时候读
        // `owner->id`。会话的唤醒、视口的浮层宿主与焦点管理器都从这一支取，取的就是「属主那扇窗」
        // 而不是「主窗」——稿 §0 的 F3／F11 那两处按窗取的落点。
        WorkspaceWindow *owner = nullptr;
        std::map<borealis::ui::PaneId, std::shared_ptr<borealis::session::Session>>
            sessions_by_pane;  ///< 每格一份会话，键即 `PaneId`
        PaneSessionFactory make_session;  ///< 本标签的切分工厂；缺席即这一格切不开（fail-closed）
        std::shared_ptr<borealis::ui::WorkspaceView> workspace;
    };
    std::map<borealis::ui::TabId, TabWorkspace> tab_workspaces;

    // 当前显示的标签 ID（用于场景根切换）：按窗一份（稿 §2），两扇窗各有各的「当前」。
    std::optional<borealis::ui::TabId> &current_tab_id = first_window.current_tab_id;

    // 标签的会话类型（`SPEC.FEAT.INTEG.02`）：新标签是否继承当前目录只由「当前标签是不是本地
    // 终端」决定，而本地标签在 SSH / 串口 / Telnet 三张接线表里都没有身份，故单立一张类型表。
    // 撤账纪律照那三张：`perform_close` 随标签同撤，撤销重开登记为 Local，重启换腿不改类型。
    enum class TabKind : std::uint8_t { Local, Ssh, Serial, Telnet };
    std::map<borealis::ui::TabId, TabKind> tab_kind;

    // ---- 广播组（`SPEC.FEAT.WS.09`，裁决 7.121）----
    // 组与端点表是 app 级一份（D2）：成员 `(TabId, PaneId)` 跨标签保留、不落盘。横幅常驻根 Column
    // 的 index 1（D10），本处先造控件，稍后与标签条 / 工作区一起入根（顺序见 `root_column` 的组装）。
    borealis::ui::BroadcastRouter broadcast_router;
    std::function<void()> refresh_broadcast_ui;
    // 归还键盘焦点按窗取（稿 §6 批 3）：横幅是每窗一枚，点 B 窗的横幅「退出广播」却把焦点还给主窗
    // 那一格，就是替用户切了窗。此刻还没有第二扇窗的调用点，但它的形态已经只能是「哪个窗的横幅
    // 自己那一格的焦点」，故形参从缺省改成窗。
    std::function<void(WorkspaceWindow &)> restore_window_keyboard_focus;
    // 派发到哪一扇窗的**上报腿**（`SPEC.FEAT.WS.03` 稿 D7）：三处上报点（视口按键与指针 Press／
    // 标签条 Press／横幅 Press）都只调这一支，真正的记录器 `ActiveWindowTracker` 要等宿主登记之后
    // 才取得到主窗号，故与 `open_new_window_request` 同形态——先声明、按引用捕、赋值发生在建钩之前，
    // 点击那一刻读到的永远是最新那一份。
    std::function<void(WorkspaceWindow &)> report_dispatch;

    // 横幅的造法（按窗一份，稿 §2：它挂在每扇窗自己的根 Column 上），而组本体仍是应用级那一份
    //（裁决 7.121）⇒ `exit_all` 清的是全局组，这一点在批 4「每窗横幅内容只列本窗成员」里保持不变。
    // 形参是窗而不是缺省：接缝形态 `BroadcastBannerHooks::exit_all` 是无参闭包（ ui 层不认框架的窗），
    // 「哪扇窗的横幅」由造它的那一跳捕进去——`WorkspaceWindow` 在 `unique_ptr` 的 vector 里地址不动。
    auto make_broadcast_banner = [&](WorkspaceWindow &window)
        -> std::shared_ptr<borealis::ui::BroadcastBanner> {
        return std::make_shared<borealis::ui::BroadcastBanner>(
            borealis::ui::BroadcastBannerHooks{.exit_all = [&window, &broadcast_router,
                                                       &restore_window_keyboard_focus]() -> void {
                // 点击横幅 = 清空整组（D8）；只有真的清了组才归还焦点——空组的横幅高 0，本不该被命中，
                // 这一句是防御而非正常路径。
                if (broadcast_router.size() > 0U) {
                    broadcast_router.clear();
                    if (restore_window_keyboard_focus) {
                        restore_window_keyboard_focus(window);
                    }
                }
            },
            // 稿 D7 的第三处上报点：点过 B 窗的横幅，下一条全局快捷键就该落 B 窗。
            .note_press = [&window, &report_dispatch]() -> void {
                if (report_dispatch) {
                    report_dispatch(window);
                }
            }});
    };
    first_window.banner = make_broadcast_banner(first_window);
    auto &broadcast_banner = first_window.banner;

    // UTF-8 → 码点：词条表里「未命名会话」「窗格 N」是 UTF-8，而横幅状态存 `u32string`。逐字节折成
    // 码点会把 CJK 拆成 Latin-1 乱码（与撤销重开标签名同一处教训）。
    auto decode_utf8 = [](const std::string &utf8) -> std::u32string {
        borealis::term::Utf8Decoder decoder;
        DecodeCollector collector;
        const auto *bytes = reinterpret_cast<const std::byte *>(utf8.data());
        decoder.feed(std::span<const std::byte>(bytes, utf8.size()), collector);
        decoder.finish(collector);
        return collector.out;
    };

    // 名字与计数只有一个折算点（F12/D11）：标签名经 `resolve_tab_name` 现取，同标签多成员追加
    // 「窗格 N」，计数取**去重后的会话数**（同一会话被两格指向时只列一次、只算一个）。
    refresh_broadcast_ui = [&]() -> void {
        const auto &members = broadcast_router.members();
        borealis::ui::BroadcastBannerState state;
        state.active = !members.empty();
        state.session_count = broadcast_router.unique_size();

        std::map<borealis::ui::TabId, std::size_t> per_tab;
        for (const borealis::ui::BroadcastMember &member : members) {
            ++per_tab[member.tab];
        }
        const auto tabs = tab_strip.tabs();
        std::vector<const void *> seen;
        std::map<borealis::ui::TabId, std::size_t> ordinals;
        for (const borealis::ui::BroadcastMember &member : members) {
            const void *identity = broadcast_router.stream_of(member);
            if (identity != nullptr) {
                bool duplicate = false;
                for (const void *known : seen) {
                    if (known == identity) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) {
                    continue;  // 同一会话被两格指向：横幅只列一次（D7/D11）
                }
                seen.push_back(identity);
            }
            std::u32string name;
            for (const auto &tab : tabs) {
                if (tab.id == member.tab) {
                    name = borealis::ui::resolve_tab_name(tab.names, settings.appearance.tab_name_priority);
                    break;
                }
            }
            if (name.empty()) {
                name = decode_utf8(borealis::ui::settings_label("workspace.broadcast.unnamed"));
            } else if (per_tab[member.tab] > 1U) {
                name.push_back(U' ');
                name += decode_utf8(borealis::ui::settings_label(
                    "workspace.broadcast.pane",
                    {au::LocalizedString{std::to_string(++ordinals[member.tab])}}));
            }
            state.names.push_back(std::move(name));
        }
        broadcast_banner->set_state(std::move(state));
        // pill 按各工作区自己的 pane 数门（N4）；隐藏标签的工作区也刷，切过去时状态已对。
        for (auto &[tab_id, tab] : tab_workspaces) {
            (void)tab_id;
            tab.workspace->refresh_broadcast_badges();
        }
    };

    restore_window_keyboard_focus = [&](WorkspaceWindow &window) -> void {
        if (!window.current_tab_id.has_value()) {
            return;
        }
        const auto found = tab_workspaces.find(window.current_tab_id.value());
        if (found != tab_workspaces.end()) {
            found->second.workspace->restore_keyboard_focus();
        }
    };

    // 成员集合真变即刷新（横幅 + 全部工作区的 pill）：toggle / remove / remove_tab / clear 四条路
    // 共用这一处，于是「新增一条改组的调用点而忘了刷新」结构上进不来。
    broadcast_router.set_change_hook([&refresh_broadcast_ui]() -> void {
        if (refresh_broadcast_ui) {
            refresh_broadcast_ui();
        }
    });

    // 三处 `WorkspaceView` 构造点（新开标签 / 重启换会话 / 撤销重开）同源装四条接缝与端点（稿 §4 第 6 条）。
    auto wire_broadcast_hooks = [&](borealis::ui::TabId id, borealis::ui::WorkspaceView::Hooks &hooks) -> void {
        hooks.is_broadcast = [&broadcast_router, id](borealis::ui::PaneId pane) -> bool {
            return broadcast_router.contains(id, pane);
        };
        hooks.toggle_broadcast = [&broadcast_router, id](borealis::ui::PaneId pane) -> bool {
            return broadcast_router.toggle(id, pane);  // 变更钩子负责刷新横幅与 pill
        };
        hooks.mirror_key = [&broadcast_router, id](borealis::ui::PaneId pane,
                                                   const borealis::term::KeyPress &press) -> void {
            broadcast_router.dispatch_key(id, pane, press);
        };
        hooks.mirror_text = [&broadcast_router, id](borealis::ui::PaneId pane, std::u32string_view text) -> void {
            broadcast_router.dispatch_text(id, pane, text);
        };
    };
    // 端点按标签登记一次；回调每次现查 `tab_workspaces`，于是同标签重启换会话后也不需要重新登记
    //（`replace_pane_session` 因此只刷横幅计数，不再 `attach` 一遍）。
    auto attach_broadcast_sink = [&](borealis::ui::TabId id) -> void {
        broadcast_router.attach(
            id, borealis::ui::BroadcastRouter::Sink{
                    .stream = [&tab_workspaces, id](borealis::ui::PaneId pane) -> const void * {
                        const auto found = tab_workspaces.find(id);
                        if (found == tab_workspaces.end() || found->second.workspace == nullptr) {
                            return nullptr;
                        }
                        const borealis::ui::TerminalView *view = found->second.workspace->view_of(pane);
                        return view == nullptr ? nullptr : view->session_identity();
                    },
                    .key = [&tab_workspaces, id](borealis::ui::PaneId pane,
                                                 const borealis::term::KeyPress &press) -> void {
                        const auto found = tab_workspaces.find(id);
                        if (found == tab_workspaces.end() || found->second.workspace == nullptr) {
                            return;
                        }
                        if (borealis::ui::TerminalView *view = found->second.workspace->view_of(pane)) {
                            view->send_broadcast_key(press);
                        }
                    },
                    .text = [&tab_workspaces, id](borealis::ui::PaneId pane, std::u32string_view text) -> void {
                        const auto found = tab_workspaces.find(id);
                        if (found == tab_workspaces.end() || found->second.workspace == nullptr) {
                            return;
                        }
                        if (borealis::ui::TerminalView *view = found->second.workspace->view_of(pane)) {
                            view->send_broadcast_text(text);
                        }
                    }});
    };

    // SFTP 面板（SPEC.FEAT.CONN.04）的标签接线数据：活动标签的 SSH 档案，与该标签
    // 拨号那次凭据询问得到的秘密材料。材料是**内存副本**，存活期口径同
    // `SshConnection::secret_`（连接关闭即清）——不落盘、不进日志；CONN.09 本期
    // 降级为「每次询问」，SFTP 面板的独立会话复用同一次询问的材料，不再二次弹窗。
    std::map<borealis::ui::TabId, borealis::conn::SshProfile> ssh_profile_by_tab;
    std::map<borealis::ui::TabId, std::optional<std::string>> ssh_secret_by_tab;

    // 串口标签的接线数据（`SPEC.FEAT.CONN.05` 批 3）：档案是「重启按档案、热改对话框显示当前档」
    // 的料源，裸指针指向该格 Session 拥有的那条连接腿（热改呼 `set_baud` 与读 `current_baud`）。
    // 指针不比连接腿活得久是结构性成立的：`Session` 独占那条 unique_ptr，而本表与格同生死
    // （`perform_close` 撤账、`restart_current_tab` 换腿时改写），撤账纪律照上面的 SSH 两表。
    struct SerialTabBinding {
        borealis::conn::SerialProfile profile{};
        borealis::conn::SerialConnection *connection{nullptr};
    };
    std::map<borealis::ui::TabId, SerialTabBinding> serial_by_tab;

    // Telnet 标签的接线数据（`SPEC.FEAT.CONN.06` 批 3）：只存档案，不学串口那张表带一份连接
    // 裸指针——Telnet 没有在途可改的参数档（热改波特率那一条是串口独有的），「重启按档案」读的
    // 就是 host 与 port 两个字段。撤账纪律同上：`perform_close` 里随标签同撤；重启重拨用的还是
    // 同一份档案，故那张表在重启前后不动。
    std::map<borealis::ui::TabId, borealis::conn::TelnetProfile> telnet_by_tab;

    // 每只视口的浮层与搜索依赖接线（宿主与注册表须待 `host` / `app` 建成才取得到，故首标签建得早的
    // 那几只在建成之后统一补挂，见下面赋值处）。缺这两条挂接，右键菜单、多行粘贴确认与搜索浮层
    // **结构上弹不起来**（裁决 7.41③ / 7.81），而集成用例经替身宿主测的是接线而非这里。
    // 属主窗随形参进来（批 2）：浮层宿主按窗一份，`app.focus()` 那一条恒指主宿主、在第二扇窗里就是
    // 打不进去的键（稿 §0 的 F3）。
    std::function<void(borealis::ui::TerminalView &, WorkspaceWindow &)> wire_view;

    // dead-session 浮层「重启」按钮的动作（`SPEC.FEAT.WS.05`）：与命令层 `session.restart` 同源一份，
    // 于是按钮点击与 `Ctrl+Shift+R` 走的是同一次换会话。声明在前是必需的——`wire_view` 定义得比它早，
    // 若把 `restart_current_tab` 直接闭包进 `wire_view` 就取不到；捕获**引用**并在实际点击时读它，
    // 首标签那一只视口因此结构上不可能拿到一个空的钩子。
    // 形参是**落点窗**（`SPEC.FEAT.WS.03` 稿 D7）：按钮那一路知道自己长在哪个视口里（`wire_view` 的形参），
    // 命令那一路没有派发来源、只能问 tracker。两条入口差的只是这一枚来源，动作本体仍是同一份闭包。
    std::function<void(WorkspaceWindow &)> restart_current_tab;

    // 会话日志的两条视口钩子（`SPEC.FEAT.CONN.11`，裁决 7.116 的批 2）：与上面那支同一条理由——
    // 真正的动作在装配层后面那一份闭包里，声明在前只为了让 `wire_view` 能按**引用**捕获，
    // 首标签那只建得早的视口结构上拿不到空钩子。
    std::function<std::optional<bool>()> logging_state_probe;
    std::function<void()> logging_toggle_action;

    // 第十五条保留键的动作（`SPEC.FEAT.WS.03` 稿 D2）：与上面几支同一条「声明在前」理由——工作区的
    // `Hooks` 在首标签创建那一刻就要装上，而真正的开窗腿要等 `Application` 建成才写得出（它按引用捕获
    // 那支 `std::function`，点击时读最新那一份）。缺钩即这一键只被认领、什么都不做。
    std::function<void()> open_new_window_request;

    // ---- 每格一会话的三条公共件（`SPEC.FEAT.WS.02`，稿 §3／§5）------------------------------
    //
    // 地基实测 F3／F4 的成因是「三处构造点各写一遍接缝」：重启腿与撤销重开那两处的 `teardown_pane`
    // 是空闭包（关一格不撤广播成员），`dispatch_grid` 吃的是捕获进来的首会话（切分格的行列打给了首格）。
    // 下面这一组把「按格取会话」「起会话建视口」「装三条接缝」各自收敛到一处定义。

    /// @brief 按 `(标签, 格)` 取会话；两处任一不存在即回空指针。
    auto pane_session = [&tab_workspaces](borealis::ui::TabId tab, borealis::ui::PaneId pane)
        -> std::shared_ptr<borealis::session::Session> {
        const auto found = tab_workspaces.find(tab);
        if (found == tab_workspaces.end()) {
            return nullptr;
        }
        const auto session = found->second.sessions_by_pane.find(pane);
        return session == found->second.sessions_by_pane.end() ? nullptr : session->second;
    };

    /// @brief 当前标签的活动格会话（`SPEC.FEAT.INTEG.02` 的「多会话落地后改取焦点 pane」那条 TODO）。
    auto focused_pane_session = [&tab_workspaces, &pane_session](borealis::ui::TabId tab)
        -> std::shared_ptr<borealis::session::Session> {
        const auto found = tab_workspaces.find(tab);
        if (found == tab_workspaces.end() || found->second.workspace == nullptr) {
            return nullptr;
        }
        return pane_session(tab, found->second.workspace->focused_pane());
    };

    /// @brief 本地腿的会话构造（六条开标签腿里本地那一支唯一的重复段：命令／目录／环境三件搬进规格）。
    auto make_local_session =
        [&](const borealis::conn::LocalTerminalSpec &spec) -> std::shared_ptr<borealis::session::Session> {
        // 宽度表按会话各造一份：这张表被写侧与折算侧共享，跨会话共用就是让两条连接互相污染列宽缓存。
        auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();
        return std::make_shared<borealis::session::Session>(
            borealis::conn::make_local_terminal_connection(spec, kNominalViewport), kNominalViewport,
            settings.terminal.scrollback_limit, width_policy, make_terminal_defaults(settings),
            make_session_encoding(settings));
    };

    /// @brief 起用一格会话并建出它的视口（新开标签 / 切分新格 / 重启换会话三处共用的那五步）。
    ///
    /// @p window 是这一格所属的那扇窗：唤醒腿与浮层宿主都按它取（稿 §2／§6 批 2）。
    auto build_pane_view = [&](WorkspaceWindow &window,
                               const std::shared_ptr<borealis::session::Session> &session,
                               borealis::term::LineEnding line_ending)
        -> std::shared_ptr<borealis::ui::TerminalView> {
        // 按属主窗取 surface：读的是 `WorkspaceWindow::surface` 这一枚成员指针，窗登记进宿主之后它
        // 仍有效（窗口本体归宿主，surface 归窗口）。拆窗时这一腿要停发——那属批 4 的账目，
        // TODO(SPEC.FEAT.WS.03): 窗关闭后唤醒腿须随窗撤账，此刻生产还开不出第二扇，也关不掉这一扇。
        session->set_frame_wake([&window]() -> void {
            if (window.surface != nullptr) {
                window.surface->request_wake();
            }
        });
        // 两接同一枚闸门（裁决 D13②）：会话那一接决定查询**收不收**（状态机现读，Deny 档当场回空响应），
        // 视口那一接决定收了之后**问不问**。漏前者即「查询当场被吞、界面永远不问」，漏后者即
        // 「请求登记了却没人答」——两边都接在同一枚 app 级对象上，改档才能对所有在途标签即刻一致。
        session->set_clipboard_gate(&clipboard_gate);
        session->start();
        auto view = std::make_shared<borealis::ui::TerminalView>(
            *session, make_appearance(settings, catalog), make_interaction(settings, line_ending));
        view->set_clipboard_gate(&clipboard_gate);
        // 浮层宿主与搜索依赖在创建那一刻一并挂上（`host` / `app` 已就绪时；首标签由后面统一补挂）。
        if (wire_view) {
            wire_view(*view, window);
        }
        return view;
    };

    /// @brief 装三条与「格」有关的接缝：造格、下发行列、回收。三处构造点只调这一支，不再各写一遍。
    auto make_pane_hooks = [&](borealis::ui::TabId id, borealis::ui::WorkspaceView::Hooks &hooks) -> void {
        hooks.make_pane = [&tab_workspaces, &build_pane_view, id](
                              borealis::ui::PaneId pane, borealis::ui::PaneId origin)
            -> std::shared_ptr<borealis::ui::TerminalView> {
            const auto found = tab_workspaces.find(id);
            if (found == tab_workspaces.end() || !found->second.make_session) {
                AURORA_LOG_INFO("main", "split refused: tab ", id, " carries no pane session factory");
                return nullptr;
            }
            const std::optional<PaneBundle> bundle = found->second.make_session(origin);
            if (bundle == std::nullopt || bundle->session == nullptr) {
                return nullptr;  // 这一类标签切不开（稿 §4 的串口一条）：本层回空即 `split_pane` 不动树
            }
            // 先登记会话再造视口：`tree_.split` 失败那一支的回收走 `teardown_pane`，要摘的就是这一笔。
            const std::shared_ptr<borealis::session::Session> session = bundle->session;
            found->second.sessions_by_pane[pane] = session;
            // 新格属主继承本标签那扇窗（稿 §2）：切分不跨窗，切出来的那一格与它背后那份会话都还在
            // 同一扇窗里——「切到别的窗去」是另一件尚未裁决的事，不在本条做。
            return build_pane_view(*found->second.owner, session, bundle->line_ending);
        };
        hooks.dispatch_grid = [&tab_workspaces, id](borealis::ui::PaneId pane,
                                                    borealis::ui::GridSize size) -> void {
            // 按格取会话（F4）：切分格的行列打给首格会让新格永远停在标称 80×24，而屏上看着是好的。
            const auto found = tab_workspaces.find(id);
            if (found == tab_workspaces.end()) {
                return;
            }
            const auto session = found->second.sessions_by_pane.find(pane);
            if (session == found->second.sessions_by_pane.end() || session->second == nullptr) {
                AURORA_LOG_WARN("main", "dispatch_grid: tab ", id, " pane ", pane, " has no session");
                return;
            }
            session->second->resize({size.columns, size.rows});
        };
        hooks.teardown_pane = [&tab_workspaces, &broadcast_router, id](borealis::ui::PaneId pane) -> void {
            // 两笔账一处撤（F3）：成员资格与那一份会话。非成员 remove 是空操作，切分失败回收的那个
            // 号也走这里，不需要第二判断。摘会话必须发生在视口已销毁之后——`~TerminalView` 要往它
            // 背后的会话写撤账空响应（`WorkspaceView::close_pane` 先摘子后回收，切分失败那一支由本层
            // 先 `reset()` 视口，稿 §5 同一条次序）。
            broadcast_router.remove(id, pane);
            const auto found = tab_workspaces.find(id);
            if (found != tab_workspaces.end()) {
                found->second.sessions_by_pane.erase(pane);
            }
        };
    };

    // 一个工作区的转交接缝**在这一处装齐**（新开标签与撤销重开两条构造点都调这一支）：接缝按「靠一处
    // 装钩而不是靠人记得」的纪律成对出现，某条只在一条路径上装了就会让两格同形而异味（`SPEC.FEAT.WS.02`
    // 稿 §3 的 F3／F4 正是这么来的）。第十五键 `Ctrl+Shift+N`（`SPEC.FEAT.WS.03` 稿 D2）加的正是这一类
    // 「每格都要有、又不属于任何一格」的接缝：动作闭包按引用捕获，赋值发生在开窗腿建好之后，点击时
    // 读到的永远是最新那一份。
    auto wire_workspace_hooks = [&](borealis::ui::TabId id,
                                    borealis::ui::WorkspaceView::Hooks &hooks) -> void {
        make_pane_hooks(id, hooks);
        wire_broadcast_hooks(id, hooks);
        hooks.open_new_window = [&open_new_window_request]() -> void {
            if (open_new_window_request) {
                open_new_window_request();
            }
        };
    };

    // 开标签的两段式：会话构造在外（本地腿 / SSH 腿 / 串口腿 / Telnet 腿各一份，SSH 腿即
    // CONN.02 的 SshConnection），壳（视口 / 工作区 / 标签条）共用，各处开标签点不再各自复制一遍
    // 连接装配。标签名由调用方给——档案连接取档案名，原先那句「首版先给固定措辞」的
    // TODO(SPEC.FEAT.CONN.02) 就此闭合。
    // `line_ending` 只有串口腿与 Telnet 腿会给非缺省值（裁决 7.110 D11①、7.112 D8①）：它同时进
    // `Session` 的写出折算与视口的粘贴档，两处同源才不会出现「敲回车与粘多行两个样」，故由开标签
    // 那一侧一并传进来。
    // `factory` 是这一张标签**往后切分新格**的料源（六条腿各一份，稿 §3）：串口腿交一支恒回空值的
    // 工厂，那一刀的拒绝因此只发生在「工厂返回空」这一处，本层不认识标签类型。
    // @p window 是这张标签落在哪扇窗（稿 §6 批 2）：标签条、当前标签号与往后那一切的唤醒都按它走，
    //     本棒每条调用点传的都是主窗——第二扇窗的入口在批 3。
    auto create_new_tab_with = [&](WorkspaceWindow &window,
                                   std::shared_ptr<borealis::session::Session> session,
                                   const std::u32string &tab_name, TabKind kind,
                                   PaneSessionFactory factory,
                                   borealis::term::LineEnding line_ending =
                                       borealis::term::LineEnding::Lf) -> void {
        const borealis::ui::TabId id = next_tab_id++;
        tab_kind[id] = kind;

        const std::shared_ptr<borealis::ui::TerminalView> view =
            build_pane_view(window, session, line_ending);

        borealis::ui::WorkspaceView::Hooks ws_hooks;
        wire_workspace_hooks(id, ws_hooks);

        TabWorkspace tab;
        tab.owner = &window;
        // 首格挂 1 是界面腿的公开约定（`WorkspaceView` 构造：单叶工作区把 `first_view` 挂在标识 1 上，
        // 新号从 2 起且只增不复用），故这里按常数登记，不去反查一棵还没折算过布局的树。
        tab.sessions_by_pane[1U] = std::move(session);
        tab.make_session = std::move(factory);
        tab.workspace = std::make_shared<borealis::ui::WorkspaceView>(view, std::move(ws_hooks));
        tab_workspaces[id] = std::move(tab);
        attach_broadcast_sink(id);

        // 加入标签条：这一支与下面那句「当前标签」都是**属主窗**那一份，不是全局那一份（稿 §2）。
        window.tab_strip.add(id, tab_name);
        window.current_tab_id = id;
    };
    /// @brief 本地腿的起始目录折算：读 @p tab 的 @p pane 那一格 OSC 7，走「继承 > 配置」那一档。
    ///
    /// 「CLI > 档案 > 继承 > 配置」这条次序只在 `ui::choose_start_directory` 一处算（`SPEC.FEAT.INTEG.02`），
    /// 本函数只负责把**该问的那一格**交出去：新开标签问的是当前标签的活动格，切分新格问的是被切那
    /// 一格（`origin`）。两条腿因此不可能一条继承、一条复制首格目录。
    auto local_start_directory = [&settings, &tab_kind,
                                  &pane_session](borealis::ui::TabId tab,
                                                 borealis::ui::PaneId pane) -> borealis::ui::StartDirectory {
        // 继承只发生在「这一格是本地终端且会话仍活着」那一档：SSH / 串口 / Telnet 的目录是远端或
        // 无意义的；存活判据与关闭确认同源（`Session::alive()`），死会话留下的最后一条目录不再追认。
        std::u32string osc7;
        bool local_session = false;
        const auto kind = tab_kind.find(tab);
        const std::shared_ptr<borealis::session::Session> source = pane_session(tab, pane);
        if (kind != tab_kind.end() && kind->second == TabKind::Local && source != nullptr &&
            source->alive()) {
            osc7 = source->osc_state().working_directory;
            local_session = true;
        }
        return borealis::ui::choose_start_directory(local_session, osc7,
                                                   settings.connection.startup_directory,
                                                   borealis::ui::osc7_path_style());
    };

    /// @brief 本地腿切分工厂共用的**一次性本地料**（`SPEC.FEAT.WS.02` 稿 §7 放开 `split-pane --cwd`
    ///        与 Local `--profile` 那两条）。
    ///
    /// 形态与 `create_new_tab(cwd)` 里那枚不写回任何一处的 `--cwd` 同一条：命令行只吃一次，下一格照旧
    /// 走本腿语义。设它的那一跳在切分回来之后无条件作废（`apply_startup_request` 末尾），于是「一次性」
    /// 有两处担保：成功即工厂已经把它并进新格，失败（几何不容、树没动）也不会把它留给下一次
    /// `Ctrl+Shift+D`。非本地标签更走不到这里——设置点先拒再设。
    std::optional<borealis::conn::LocalTerminalSpec> split_spec_override;

    /// @brief 把一次性本地料并进本腿算好的那份规格：**CLI 钉了哪一格才换哪一格**，没钉的照本腿。
    ///
    /// 这里不重排目录优先级（`SPEC.FEAT.INTEG.02` 的「CLI > 档案 > 继承 > 配置」只在
    /// `ui::choose_start_directory` 一处算）：空字段一律当作「命令行没说这件事」，于是
    /// `--profile` 指了一份目录为空的档案时，缺省腿仍按被切那一格的 OSC 7 继承，而不是悄悄换成
    /// 进程当前目录。
    auto consume_split_spec = [](const borealis::conn::LocalTerminalSpec &pending,
                                 borealis::conn::LocalTerminalSpec owned)
        -> borealis::conn::LocalTerminalSpec {
        if (!pending.command_line.empty()) {
            owned.command_line = pending.command_line;
        }
        if (!pending.working_directory.empty()) {
            owned.working_directory = pending.working_directory;
        }
        if (!pending.environment.empty()) {
            owned.environment = pending.environment;
        }
        return owned;
    };

    /// @brief 本地缺省腿的切分工厂：新格起同一枚 shell，目录按被切那一格现读的 OSC 7 继承。
    auto make_local_pane_factory = [&](borealis::ui::TabId id) -> PaneSessionFactory {
        return [&settings, &local_start_directory, &make_local_session, &split_spec_override,
                &consume_split_spec, id](borealis::ui::PaneId origin) -> std::optional<PaneBundle> {
            borealis::conn::LocalTerminalSpec spec;
            spec.command_line = settings.connection.local_shell;
            spec.working_directory = local_start_directory(id, origin).path;
            if (split_spec_override.has_value()) {
                spec = consume_split_spec(std::move(*split_spec_override), std::move(spec));
            }
            return PaneBundle{.session = make_local_session(spec)};
        };
    };

    /// @brief 显式本地档案腿的切分工厂：新格照同一份档案起，**不**走 OSC 7 继承（档案目录优先级更高）。
    auto make_local_profile_pane_factory =
        [&](const borealis::conn::LocalProfile &profile) -> PaneSessionFactory {
        // 档案按**值**进闭包：这一支工厂要活到整张标签散掉，而交进来的那份 `LocalProfile` 是调用方
        // 档案表里的临时副本（`open_local_profile_tab` 的形参），留引用就是留一把悬垂指针。
        return [profile, &make_local_session, &split_spec_override, &consume_split_spec](
                 borealis::ui::PaneId /*origin*/) -> std::optional<PaneBundle> {
            borealis::conn::LocalTerminalSpec spec;
            spec.command_line = profile.command_line;
            spec.working_directory = profile.working_directory;
            spec.environment = profile.environment;
            if (split_spec_override.has_value()) {
                spec = consume_split_spec(std::move(*split_spec_override), std::move(spec));
            }
            return PaneBundle{.session = make_local_session(spec)};
        };
    };

    /// @brief 恒拒的切分工厂（串口腿，稿 §4 的 F12 一条）。
    ///
    /// 拒绝只发生在「工厂返回空」这一处：`make_pane` 回空即 `split_pane` 不动树，本层因此不需要认识
    /// 标签类型。口径照稿 §4 末段——只记一条日志，**不弹框**：多一个提示件就多一个视觉件，与本件的
    /// 最小意图冲突，而真机走查时「切不开」本身就是判据。
    auto make_refusing_pane_factory = [](const char *reason) -> PaneSessionFactory {
        return [reason](borealis::ui::PaneId /*origin*/) -> std::optional<PaneBundle> {
            AURORA_LOG_INFO("main", "split refused: ", reason);
            return std::nullopt;
        };
    };

    /// @brief 某张标签当前活动格的身份；标签或工作区不存在时回 0（本地腿据此判「无源格」）。
    auto focused_pane_of = [&tab_workspaces](borealis::ui::TabId tab) -> borealis::ui::PaneId {
        const auto found = tab_workspaces.find(tab);
        if (found == tab_workspaces.end() || found->second.workspace == nullptr) {
            return 0U;
        }
        return found->second.workspace->focused_pane();
    };

    auto create_new_tab = [&](WorkspaceWindow &window,
                              std::optional<std::string> cwd = std::nullopt) -> void {
        // 缺省腿＝本地终端：命令从配置读，目录优先继承当前标签活动格的 OSC 7（`SPEC.FEAT.INTEG.02`
        // 的「多会话落地后改取焦点 pane」那条 TODO 在此闭合：一格一份会话之后，「当前那一格」是
        // 唯一说得通的继承源，`sessions.front()` 已经是错读）。
        borealis::ui::StartDirectory start{.path = settings.connection.startup_directory};
        if (const borealis::ui::TabId previous = window.current_tab_id.value_or(0U);
            previous != 0U && focused_pane_of(previous) != 0U) {
            start = local_start_directory(previous, focused_pane_of(previous));
        }

        borealis::conn::LocalTerminalSpec spec;
        spec.command_line = settings.connection.local_shell;
        // `--cwd` 是命令行给的一次性意图，优先级高于 OSC 7 继承与配置值（稿 §5.1）；它不写回任何
        // 一处，因此下一格照旧走继承链。显式本地档案那份覆盖在 `apply_startup_request` 里折进档案
        // 副本，两条腿共用「CLI > 档案 > 继承 > 配置」这一条次序而不是各写一遍。
        spec.working_directory = cwd.has_value() ? *cwd : start.path;
        // 工厂要的标签号就是本支即将取到的那个号：`create_new_tab_with` 里 `next_tab_id++` 取号，
        // 这里先读现值，两处之间没有别的取号点（工厂只在切分那一刻被调，那时号已经发出去了）。
        const borealis::ui::TabId id = next_tab_id;
        create_new_tab_with(window, make_local_session(spec), U"本地终端", TabKind::Local,
                            make_local_pane_factory(id));
    };

    // 首标签与冷启动请求的关系（稿 §5.1 / §5.4）：只有「`new-tab` 且带 `--profile`」要把这一格**换掉**，
    // 否则会多出一条没人要的缺省标签；缺省 `new-tab`（无档案）在这一格就把 `--cwd` 折进去，
    // `split-pane` 则照旧建这一格、等接线齐备后再对它切一刀。
    const bool cold_startup_profile_tab = startup_request.has_value() &&
                                          startup_request->action == borealis::cli::StartupAction::NewTab &&
                                          !startup_request->profile.empty();
    // 缺省 `new-tab` 已由上面那一格交付（含 `--cwd`），`app.run()` 前不再重复开标签。
    const bool cold_startup_plain_tab_done = startup_request.has_value() &&
                                             startup_request->action == borealis::cli::StartupAction::NewTab &&
                                             startup_request->profile.empty();
    const std::optional<std::string> startup_cwd_override =
        (startup_request.has_value() && !startup_request->cwd.empty())
            ? std::optional<std::string>{startup_request->cwd}
            : std::nullopt;
    if (!cold_startup_profile_tab) {
        create_new_tab(first_window, startup_cwd_override);
    }

    // 辅助函数：重建**某一扇窗**根 Column 的子节点列表（标签栏 + 广播横幅 + 当前工作区）。
    // 形参从「列 + 全局当前号」换成「窗 + 全表」（稿 §6 批 3）：多窗下那张 `tab_workspaces` 是应用级
    // 一张表（稿 §0 的 F10：号全局唯一），按号查出来的那一格可能属于别的窗——把它塞进本窗的根就是
    // 「两扇窗显示同一格工作区」，而工作区只认一个父节点，两扇的布局会互相掀掉。归属由批 2 那一枚
    // `TabWorkspace::owner` 判，号本身不携带这个信息。
    auto rebuild_root_children = [](WorkspaceWindow &window,
                                    const std::map<borealis::ui::TabId, TabWorkspace> &workspaces)
        -> void {
        // 保留前两个子节点（标签栏 + 广播横幅），替换第三个（工作区）。横幅是常驻节点（D10），
        // 组空时高 0 也不摘除——摘除会让显隐走「增删子节点」这条分叉。
        auto &nodes = window.root_column->child_nodes_mut();
        if (nodes.size() < 2) {
            return;  // 还没初始化完
        }
        nodes.resize(2);
        if (window.current_tab_id.has_value()) {
            const auto it = workspaces.find(window.current_tab_id.value());
            if (it != workspaces.end() && it->second.owner == &window) {
                // 工作区占满标签栏以下的全部剩余高度（expand 权重交 FlexLayouter 分配）。
                aurora::Node ws{it->second.workspace};
                ws.widget().modifier.set(au::Modifier{}.expand(1.0F));
                nodes.push_back(std::move(ws));
            }
        }
        window.root_column->mark_needs_layout();
        window.root_column->mark_needs_paint();
    };

    // 浮层宿主已随 `open_workspace_window()` 建好（批 1 把它的归属挪进 `WorkspaceWindow`，场景根形态与
    // 裁决 7.41③ 那段理由都在工厂里）；这里只取第一扇窗的那一支别名，给四面板与降级提示用——面板
    //「搬窗」而非「各一份」是 D6 的口径，落点在批 4。根 Column 一支不再设别名：入根已经收进
    // `assemble_window_content()`，其余读它的地方全按窗取。
    std::shared_ptr<au::OverlayHost> &host = first_window.overlay_host;

    // 「该标签还有活着的会话吗」的**唯一**判定点：关闭确认（`SPEC.FEAT.WS.01`，裁决 7.47⑧）与退出角标
    //（`SPEC.FEAT.WS.04`）吃的是同一个量，两处各写一遍就会出现「画着角标的格子关掉时却弹『还有进程在
    // 运行』」这种自相矛盾。判据权威恒在 `Session::alive()`，本件不复制一份状态。
    auto has_live_session = [&tab_workspaces](borealis::ui::TabId id) -> bool {
        const auto found = tab_workspaces.find(id);
        if (found == tab_workspaces.end()) {
            return false;
        }
        for (const auto &[pane, session] : found->second.sessions_by_pane) {
            static_cast<void>(pane);
            if (session->alive()) {
                return true;
            }
        }
        return false;
    };

    // 真正关闭一张标签：从标签条摘除、记进闭包栈、销毁工作区与会话、必要时切到相邻标签。
    // 「确认」与「直接关」两条路径共用这一支，差别只在要不要先问一句（裁决 7.47⑧）。
    // @p window 是这张标签的**属主窗**（稿 §2 的「呈现半区按窗一份」）：标签条、闭包栈与当前号都取
    // 本窗那一份，别的窗那一栏里没有这张标签，`close()` 因此在属主之外的窗上恒回 false——这条
    // fail-closed 恰好是「串窗撤账」的守门，不需要另立一份「这张标签属于谁」的账。
    auto perform_close = [&](WorkspaceWindow &window, std::uint64_t raw_id) -> void {
        const borealis::ui::TabId id = static_cast<borealis::ui::TabId>(raw_id);
        const bool removed = window.tab_strip.close(id);
        if (!removed) {
            return;
        }
        // 关标签即撤该标签的全部成员与端点（`SPEC.FEAT.WS.09`）：成员身份按跨标签复合键存，
        // 不随工作区析构自动消失，漏这一笔会让横幅继续列着一张已经不存在的标签、并向其扇出。
        // 变更钩子负责刷新横幅与其余工作区的 pill，此处无须另调。
        broadcast_router.remove_tab(id);
        // 关闭前记录到闭包栈（`SPEC.FEAT.WS.10`）
        // 先把根上那份工作区摘掉，再撤会话表（稿 §5 的「会话活过视口」）：关的是当前标签时根 Column
        // 还持着同一个 `shared_ptr`，先 erase 就等于让会话先散、视口后散，而 `~TerminalView` 要往它
        // 背后的会话写撤账空响应。关后台标签时根本来不持它，这一句在那条路上是 no-op。
        if (window.current_tab_id.has_value() && window.current_tab_id.value() == id) {
            // 选下一个或上一个
            const auto next = window.tab_strip.selected();
            if (next.has_value()) {
                window.current_tab_id = next.value();
            } else {
                window.current_tab_id.reset();
            }
            // 更新显示的工作区
            rebuild_root_children(window, tab_workspaces);
        }
        auto it = tab_workspaces.find(id);
        if (it != tab_workspaces.end()) {
            borealis::ui::ClosedTabSpec spec;
            // 名称与位置都取 `close()` 记下的那份：此刻该标签已不在表里，回查只会拿到空名，
            // 于是重开回来的标签变成一个无名格——那正是本件要避免的「内容不可恢复」被扩大成
            // 「名字也丢了」。
            if (const auto closed = window.tab_strip.last_closed(); closed.has_value()) {
                static_cast<void>(borealis::term::encode_utf8(closed->name, spec.name));
                spec.index_in_strip = closed->index_in_strip;
            }
            // 本地终端的连接规格从配置取（目前只支持本地终端，SSH 腿到货后追加）
            spec.local_shell = settings.connection.local_shell;
            spec.startup_directory = settings.connection.startup_directory;
            window.closed_tab_stack.push(std::move(spec));
            // 销毁对应的 WorkspaceView 和会话：`TabWorkspace` 的成员次序保证 `sessions_by_pane` 后析构，
            // 此刻根上已不再持有任何一格的视口，视口在这一跳里散尽。
            tab_workspaces.erase(it);
            // SFTP 面板接线数据随标签同撤（面板的 ssh_tab_alive 即以此判活，D8①）。
            ssh_profile_by_tab.erase(id);
            ssh_secret_by_tab.erase(id);
            // 串口接线同撤：那条连接腿随本格的 Session 一起析构，裸指针不留过夜（批 3）。
            serial_by_tab.erase(id);
            telnet_by_tab.erase(id);  // Telnet 同撤（CONN.06 批 3）。
            tab_kind.erase(id);       // 类型随标签同撤：重开那一支会重新登记（INTEG.02）。
        }
    };

    // 关闭确认框**按窗一份**（稿 §2 的「每窗一份浮层」）：判据是该标签**任一** pane 的会话进程仍在
    //（`Session::alive()`），一次确认关整张标签（逐 pane 追问在多 pane 标签上就是 N 个对话框）。全部
    // 已退出则直接关、不再问。它盖在**本窗**的浮层宿主上、实例由本窗持有，于是两扇窗共用一只对话框
    // 那种「B 窗的确认弹在 A 窗脸上、答话回来的 `raw_id` 又按 A 窗那栏算」就进不来。生存期须跨过
    // Show→答话这一段，与多行粘贴确认（`TerminalView::ask_multiline_warning`）同形态：首次挂入后
    // 复用同一实例换文案。
    auto ask_close_confirm = [&](WorkspaceWindow &window, std::uint64_t raw_id) -> void {
        if (!window.close_confirm) {
            window.close_confirm = std::make_shared<au::Dialog>();
        }
        if (!window.close_confirm_mounted) {
            static_cast<void>(window.overlay_host->add_overlay(
                aurora::Node{std::static_pointer_cast<au::Widget>(window.close_confirm)}));
            window.close_confirm_mounted = true;
        }
        window.close_confirm->set_content(
            au::confirm(
                "关闭标签",  // CJK-LITERAL: 上屏文案 - 面向用户的对话框标题
                "该标签里还有正在运行的进程，关闭会终止它们。确定关闭吗？",  // CJK-LITERAL: 上屏文案 - 面向用户的对话框正文
                [&window, raw_id, &perform_close,
                 dialog = window.close_confirm](bool accepted) -> void {
                    dialog->close();
                    if (accepted) {
                        perform_close(window, raw_id);
                    }
                }));
        window.close_confirm->show();
    };

    // 标签栏的构造按窗一份（稿 §6 批 3）：六条 hooks 读的全是**本窗**那一份真值源——标签表、当前号、
    // 闭包栈（`perform_close` 里）。留在全局的只有「这张标签的工作区与会话」那几张应用级表（稿 §0
    // 的 F10：`TabId` 全局唯一），它们按号查、与窗无关。变更钩子同样装在本窗的 `TabStrip` 上。
    auto make_tab_bar = [&](WorkspaceWindow &window) -> void {
        window.tab_bar = std::make_shared<borealis::ui::TabBarWidget>(borealis::ui::TabBarHooks{
            .tabs = [&window, &settings]() -> std::vector<borealis::ui::TabVisual> {
                std::vector<borealis::ui::TabVisual> out;
                const auto tabs = window.tab_strip.tabs();
                const auto selected = window.tab_strip.selected();
                for (const auto &tab : tabs) {
                    out.push_back(borealis::ui::TabVisual{
                        .id = tab.id,
                        .display_name = borealis::ui::resolve_tab_name(tab.names, settings.appearance.tab_name_priority),
                        .is_selected = (selected.has_value() && selected.value() == tab.id),
                        .has_close_button = (window.tab_strip.count() > 1),  // 末位不给关
                        .bell_triggered = tab.bell_triggered,
                        .has_activity = tab.has_activity,
                        .exited = tab.exited,
                    });
                }
                return out;
            },
            .select = [&window, &tab_workspaces, &rebuild_root_children](std::uint64_t id) -> void {
                window.tab_strip.select(static_cast<borealis::ui::TabId>(id));
                window.current_tab_id = static_cast<borealis::ui::TabId>(id);
                // 切换当前显示的工作区视图
                rebuild_root_children(window, tab_workspaces);
                // 切到该标签即把两枚**事件**角标取走（`SPEC.FEAT.WS.04`）：BEL 与活动同一口径，只清活动
                // 会让铃铛在用户已经站在那一格之后还亮着且再也没有取走的入口。
                window.tab_strip.take_activity(static_cast<borealis::ui::TabId>(id));
                window.tab_strip.take_bell_triggered(static_cast<borealis::ui::TabId>(id));
            },
            .close = [&window, &has_live_session, &ask_close_confirm, &perform_close](
                         std::uint64_t id) -> void {
                // 判据：该标签任一 pane 的会话进程仍在 ⇒ 一次确认关整张标签；全部已退出 ⇒ 直接关、不再问
                //（裁决 7.47⑧）。与退出角标共用 `has_live_session`，两处不可能各判一次。
                if (has_live_session(static_cast<borealis::ui::TabId>(id))) {
                    ask_close_confirm(window, id);
                    return;
                }
                perform_close(window, id);
            },
            .rename = [&window](std::uint64_t id, std::u32string name) -> void {
                window.tab_strip.rename(static_cast<borealis::ui::TabId>(id), std::move(name));
            },
            .move = [&window](std::uint64_t id, std::size_t to_index) -> void {
                window.tab_strip.move(static_cast<borealis::ui::TabId>(id), to_index);
            },
            .add_new = [&window, &create_new_tab, &tab_workspaces, &rebuild_root_children]() -> void {
                create_new_tab(window);
                // 新标签创建后重建根节点子节点以包含新工作区
                rebuild_root_children(window, tab_workspaces);
            },
            // 稿 D7 的第二处上报点：栏带上的任何一次 Press 都算碰过这一扇窗（含点在空白处与「＋」）。
            .note_press = [&window, &report_dispatch]() -> void {
                if (report_dispatch) {
                    report_dispatch(window);
                }
            },
        });
        // 标签真值源有任何一次**实际转移**就把标签栏标脏（裁决 **7.91**④）。不装这一钩子，三枚角标与
        // 显示名都落不了屏：`TabBarWidget` 的输入全在 `on_layout` 里经 hooks 现取，而框架的布局缓存在
        // 「约束没变且本控件未被标脏」时直接复用上次尺寸并**跳过 `on_layout`**——窗口大小没变正是常态，
        // 于是缓存恒有效。钩子装在 `TabStrip` 一侧而不是每帧都标脏，就是为了让后台标签的输出洪流只
        // 点亮一次而不是每帧重排。
        window.tab_strip.set_changed_hook(
            [bar = window.tab_bar, &refresh_broadcast_ui]() -> void {
                bar->mark_needs_layout();
                bar->mark_needs_paint();
                // 标签改名（含 OSC 标题）要立刻反映到横幅的成员名（F12）；新建 / 关闭标签也经这里，
                // 于是「横幅只在按键改组时刷新」那条漏配结构上不存在。
                if (refresh_broadcast_ui) {
                    refresh_broadcast_ui();
                }
            });
    };

    // 一扇窗的**内容入根**（稿 §6 批 3）：标签栏 → 广播横幅 → 当前工作区，次序是承重的（横幅常驻
    // index 1，D10），故这一段整体复用而不是让第二扇窗各写一遍。根 Column 交进浮层宿主排在三件之后，
    // 与主窗原先那段次序逐字相同（裁决 7.41③：场景根是浮层宿主而不是视口）。
    auto assemble_window_content = [&](WorkspaceWindow &window) -> void {
        make_tab_bar(window);
        // 主窗那一枚横幅在首标签创建**之前**就要存在（`refresh_broadcast_ui` 在 `router.attach` 那一刻
        // 就要有可写的横幅），故上面已建；按窗补建只服务第二扇窗之后的入口。
        if (!window.banner) {
            window.banner = make_broadcast_banner(window);
        }
        window.root_column->child_nodes_mut().push_back(aurora::Node{window.tab_bar});
        window.root_column->child_nodes_mut().push_back(aurora::Node{window.banner});
        window.overlay_host->child_nodes_mut().push_back(aurora::Node{window.root_column});
        rebuild_root_children(window, tab_workspaces);
    };

    // 场景根的造法按窗一份（稿 §2）：chrome 主题挂浮层宿主外面那一层，面板与后续界面件读同一份
    // `Theme`，而它**刻意不随终端主题联动**（裁决 7.52 的 S5① / N6：切配色主题时整窗跟着变会让用户
    // 以为丢了设置）。第二扇窗必须拿到**自己**的一枚 Scene：`Application::open_window` 收的是场景本体
    // 而不是引用，两扇窗共用一棵树就是把同一批控件挂到两个宿主上。
    auto make_window_scene = [](WorkspaceWindow &window) -> au::Scene {
        auto chrome = std::make_shared<au::ThemeScope>(
            borealis::ui::settings_chrome_theme(),
            aurora::Node{std::static_pointer_cast<au::Widget>(window.overlay_host)});
        return au::Scene{au::Node{std::static_pointer_cast<au::Widget>(std::move(chrome))}};
    };

    assemble_window_content(first_window);
    au::Scene scene = make_window_scene(first_window);
    borealis::session::ClipboardOutbox outbox;

    au::Application app{std::move(scene), first_window.take_window(), first_window.options};
    // 宿主登记之后才有号（`Application` 从 1 起自增，稿 §0 的 F2）：这一枚是本扇窗后续一切「按窗取」
    // 的键，未登记之前它是 `AURORA_INVALID_WINDOW_ID`，于是「拿没建成的窗去取宿主」必然取空。
    first_window.id = app.main_window();

    // 按窗取焦点管理器（稿 §0 的 F3）：`app.focus()` 恒指主宿主，拿它给第二扇窗接线就是「键入打不
    // 进第二扇窗」那条已知形态。取不到宿主时回主窗那一份是**兜底而不是猜测**——号没登记说明这扇窗
    // 还没建成，此刻唯一能用的就是主窗，兜底让其余接线不因一扇没建成的窗整体断掉。
    auto window_focus = [&app](WorkspaceWindow &window) -> au::FocusManager & {
        au::WindowHost *const target = app.window_host(window.id);
        return target != nullptr ? target->focus() : app.focus();
    };

    // 「命令该落哪一扇窗」的记录器（`SPEC.FEAT.WS.03` 稿 D7）：框架既不给活动窗查询，也不把派发来源
    // 带进快捷键闭包（稿 §0 的 F4／F5、稿 §3 的 G42），故本仓自持这一枚最近记录器，落点由**事实**决定。
    borealis::ui::ActiveWindowTracker active_window_tracker{first_window.id};
    report_dispatch = [&active_window_tracker](WorkspaceWindow &window) -> void {
        active_window_tracker.report(window.id);
    };
    // 号 → 本仓那份窗：既查容器也查宿主。宿主取不到即这扇窗已经不在（号还在容器里而宿主已散是关窗
    // 那一棒的账目，批 4），此时按**空落点**处理而不是猜「最后一扇活着的窗」——命令宁可不动，也不要把
    // 用户没碰过的那一扇改得看得见。
    auto window_of = [&app, &workspace_windows](au::WindowId id) -> WorkspaceWindow * {
        if (id == au::AURORA_INVALID_WINDOW_ID || app.window_host(id) == nullptr) {
            return nullptr;
        }
        for (const std::unique_ptr<WorkspaceWindow> &candidate : workspace_windows) {
            if (candidate->id == id) {
                return candidate.get();
            }
        }
        return nullptr;
    };
    // 五类闭包（`search.open`／`session.restart`／`serial.set_baud`／`workspace.toggle_fullscreen`／
    // `tab.undo_close`）的落点：tracker 无记录时回的是**主窗号**（构造期那枚兜底），故正常路径恒有一扇窗；
    // 只有主窗自己也没了才回空值，调用方按「无事发生」处理。
    auto target_window = [&active_window_tracker, &window_of]() -> WorkspaceWindow * {
        return window_of(active_window_tracker.active());
    };

    // 视口的两条浮层依赖此刻才取得到，先给已存在的全部视图（首标签那一只）补挂，再交给
    // `create_new_tab` 与撤销重开在创建那一刻调用——两条建视图路径共用同一处接线，结构上不可能分叉。
    wire_view = [&window_focus, &app, &restart_current_tab, &logging_state_probe, &logging_toggle_action,
                 &link_hosts, &production_presentation,
                 &report_dispatch](borealis::ui::TerminalView &view,
                                                        WorkspaceWindow &window) -> void {
        view.set_overlay_host(*window.overlay_host);
        view.set_search_dependencies(app.shortcuts(), window_focus(window));
        // 名单与生产打开腿在这一处一起挂上：三处建视口（新开格 / 重启换会话 / 撤销重开）都经这里，
        // 「有的格子记不住同域、有的格子点了不开」就结构上进不来（同上面 `set_clipboard_gate` 两接、
        // 以及稿 §0 的 F4 那条「靠一处装钩而不是靠人记得」的纪律）。
        view.set_trusted_link_hosts(link_hosts);
        view.set_presentation(production_presentation);
        // 稿 D7 的第一处上报点（视口）：键入与点击都报，报的是「这一扇被碰过」这一事实。捕的是窗
        // 而不是号——`window.id` 在开窗腿里是**先建标签后登记**吗？不是（先登记后建标签），但按引用
        // 捕窗、点击那一刻现读 `window.id`，两扇窗的形态就完全一致，不依赖任何时序假设。
        view.set_dispatch_observer([&report_dispatch, &window]() -> void {
            if (report_dispatch) {
                report_dispatch(window);
            }
        });
        // 视口只持一份可调用体；真正的重启在装配层实现（见下方 `restart_current_tab` 的赋值处）。
        // 按引用捕获那支 `std::function`：赋值发生在 `wire_view` 之后、点击之前，钩子里读到的永远是最新那一份。
        // 形参是**本视口所属的窗**（稿 D7）：dead-session 浮层那枚「重启」按钮在 B 窗被点，就该重开
        // B 窗当前标签的会话，而不是主窗那一张——命令面板入口没有派发来源，那条走 tracker。
        view.set_restart_hook([&restart_current_tab, &window]() -> void {
            if (restart_current_tab) {
                restart_current_tab(window);
            }
        });
        // 会话日志的第三条目走同一条纪律（`SPEC.FEAT.CONN.11`）：视口不问配置也不碰文件，
        // 只把「在不在记录」与「点一下就翻面」两件事交回装配层。这一处是**唯一**装钩的地方，
        // 而设置面板的预览视口不经 `wire_view`（它由 `SettingsPreview` 自建），于是稿 §0 的 F4
        // 那条主线程反例结构上进不了日志——不是靠人记得不给它装。
        view.set_logging_hooks([&logging_state_probe]() -> std::optional<bool> {
                                   return logging_state_probe ? logging_state_probe() : std::nullopt;
                               },
                               [&logging_toggle_action]() -> void {
                                   if (logging_toggle_action) {
                                       logging_toggle_action();
                                   }
                               });
    };
    for (auto &[tab_id, workspace] : tab_workspaces) {
        static_cast<void>(tab_id);
        for (borealis::ui::PaneId pane = 1; pane <= workspace.workspace->pane_count(); ++pane) {
            if (auto *v = workspace.workspace->view_of(pane)) {
                wire_view(*v, *workspace.owner);
            }
        }
    }

    // 新开一扇工作区窗（`SPEC.FEAT.WS.03` 批 3，稿 D1／D2）：地基、内容、首标签三段**全部复用主窗那
    // 一套定义**（`open_workspace_window()` → `assemble_window_content()` → `create_new_tab(window)`），
    // 于是第二扇窗不是主窗的特例复制，主窗那条路径也第一次有了第二个调用点。次序是承重的三处：
    // ① 先入根再折场景——`Scene` 收的是浮层宿主外面那一层的**所有权**，根 Column 必须已经在树里；
    // ② 号要等 `open_window()` 返回才有效，而首标签那一只视口要按号取焦点管理器（`window_focus`），
    //    建标签排在登记之前就会把键入打进主窗那一份焦点（稿 §0 的 F3）；
    // ③ 入容器排在 `take_window()` 之后、用 `window` 引用之前——容器是 `unique_ptr` 的 vector，搬走的
    //    是指针而不是对象，堆上那一格的地址在窗的生命周期内不动。
    open_new_window_request = [&app, &workspace_windows, &assemble_window_content, &make_window_scene,
                               &create_new_tab, &tab_workspaces, &rebuild_root_children]() -> void {
        auto created = open_workspace_window("Borealis");
        if (created == nullptr) {
            // 建窗失败原文已在工厂里落日志。这里不入容器、不登记宿主，于是稿 §5 那条「没建成窗就不
            // 留下半个账目」在第二扇窗这条路上同样成立。
            return;
        }
        WorkspaceWindow &window = *created;
        assemble_window_content(window);
        window.id = app.open_window(window.take_window(), make_window_scene(window), window.options);
        workspace_windows.push_back(std::move(created));
        // 首标签是一个**缺省本地**标签（D1）：新窗此刻没有当前标签，`create_new_tab` 的继承腿因此自动
        // 落在配置的启动目录那一档，不继承发起窗的 OSC 7——「新窗跟着旧窗的目录走」是另一件尚未
        // 裁决的事，不在本条里猜。
        create_new_tab(window, std::nullopt);
        rebuild_root_children(window, tab_workspaces);
    };

    // 面板的三条接缝都在装配层兑现：装载取 `Store` 的当前配置，落盘走 `apply_form` + `replace()`，
    // 广播把刚落盘的配置重新折算成外观包与交互项交回视口（`SPEC.FEAT.PREF.02` 的「即时生效」腿）。
    borealis::ui::SettingsPanel::Hooks hooks;
    hooks.load = [&store]() -> std::vector<borealis::ui::FormEntry> {
        return borealis::config::form_entries(store.settings());
    };
    hooks.persist = [&store, &sync_clipboard_gate](const borealis::ui::SettingsForm &form) -> std::optional<std::string> {
        const auto reason = store.replace(borealis::config::apply_form(form, store.settings()));
        sync_clipboard_gate();
        return reason;
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
    // 唤醒腿按属主窗取 surface（稿 §0 的 F11）。设置面板此刻仍只挂在主窗的浮层宿主上——面板「搬窗」
    // 而非「各一份」是 D6 的口径，落点在批 4，TODO(SPEC.FEAT.WS.03): 面板所属窗改动时这一腿要跟着改。
    hooks.preview_wake = [&first_window]() -> void {
        if (first_window.surface != nullptr) {
            first_window.surface->request_wake();
        }
    };
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
    // `SPEC.FEAT.PREF.07` 的四条动作腿与两条取路径腿。面板只认 `std::filesystem::path` 与「空串＝取消」，
    // 于是 `config::Store` 与平台对话框都留在装配层这一侧（面板不得认识 `config` 类型，7.32① 同因）。
    hooks.snapshots = [&store]() -> std::vector<borealis::ui::SettingsPanel::SnapshotEntry> {
        std::vector<borealis::ui::SettingsPanel::SnapshotEntry> out;
        for (const borealis::config::SnapshotInfo &info : store.snapshots()) {
            out.push_back(borealis::ui::SettingsPanel::SnapshotEntry{
                .path = info.path, .timestamp_epoch = info.timestamp_epoch, .size_bytes = info.size_bytes});
        }
        return out;
    };
    hooks.rollback = [&store, &sync_clipboard_gate](const std::filesystem::path &snapshot) -> std::optional<std::string> {
        const auto reason = store.rollback_to(snapshot);
        sync_clipboard_gate();
        return reason;
    };
    hooks.export_config = [&store](const std::filesystem::path &file) -> std::optional<std::string> {
        return store.export_settings(file);
    };
    // 导入与回滚都只是**存储侧动作**：把新落盘的配置广播回视口是面板那条 `reload_from_store()` 的腿
    // （重装载 → 整份广播 → 预览 → 重建浮层），故两条动作共用同一次生效而不各写一遍。
    hooks.import_config = [&store, &sync_clipboard_gate](const std::filesystem::path &file) -> std::optional<std::string> {
        const auto reason = store.import_settings(file);
        sync_clipboard_gate();
        return reason;
    };
    hooks.pick_export_path = []() -> std::string { return picked_save_path(); };
    hooks.pick_import_path = []() -> std::string { return picked_open_path(); };
    borealis::ui::SettingsPanel panel{*host, app.shortcuts(), std::move(hooks)};

    // ---- 连接管理器（CONN.07 / CONN.09，设计稿 D1–D8 已裁决收口）----
    // UTF-8 档案名 → 标签名：与「撤销重开」那条腿同一解法（CJK 名逐字节折码点会成乱码）。
    auto decode_tab_name = [](const std::string &utf8) -> std::u32string {
        borealis::term::Utf8Decoder decoder;
        DecodeCollector collector;
        const auto *bytes = reinterpret_cast<const std::byte *>(utf8.data());
        decoder.feed(std::span<const std::byte>(bytes, utf8.size()), collector);
        decoder.finish(collector);
        return collector.out;
    };

    // 连上即记最近（D4）：登记是纯逻辑件（去重置顶 + 截断），这里只管取时间戳与落盘。
    auto record_recent = [&store](const std::string &profile_id) -> void {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        auto next = store.settings();
        borealis::config::push_recent_connection(next.connection, profile_id, now);
        static_cast<void>(store.replace(std::move(next)));
    };

    // SSH 会话装配（CONN.02）：SshConnection + 与本地腿同一套视口参数。
    // 自动重连档（WS.05，裁决 7.99 D4①）在这里折算：三键在**建会话那一刻**折成 `conn::RetryPolicy`
    // 交进构造参数，故设置面板把这三行登记为「下次会话生效」。不让传输腿回头读 store——配置的宿主在
    // 装配层，且跨线程现读会撞上架构 §3.4（锁内不做 IO）那条纪律。
    auto reconnect_policy = [&settings]() -> borealis::conn::RetryPolicy {
        return borealis::conn::RetryPolicy{
            .base_ms = settings.connection.ssh.reconnect_base_delay_ms,
            .cap_ms = settings.connection.ssh.reconnect_max_delay_ms,
            .max_attempts = settings.connection.ssh.reconnect_attempts,
        };
    };

    // 就地换掉**焦点那一格**的会话（`SPEC.FEAT.WS.05` 的重启腿，稿 §6 的重启行）：关旧会话 → 起新会话
    // 建视口 → `WorkspaceView::replace_view`。本地腿与按档案腿（裁决 7.99 D6）都走它，差别只在交进来
    // 的是哪一条传输腿。
    // 由标签级降到 pane 级之后，`tab_strip` 那一格的身份、`tab_workspaces` 的键、广播成员资格、缩放态
    // 与分屏结构全部原样留下——「同一格标签、同一格重新起用」是结构性成立的，不再靠拆树重建。
    auto replace_pane_session = [&](borealis::ui::TabId id,
                                    std::shared_ptr<borealis::session::Session> session,
                                    borealis::term::LineEnding line_ending =
                                        borealis::term::LineEnding::Lf) -> void {
        const auto found = tab_workspaces.find(id);
        if (found == tab_workspaces.end() || found->second.workspace == nullptr || session == nullptr) {
            AURORA_LOG_WARN("main", "restart: tab id not present in workspace map");
            return;
        }
        borealis::ui::WorkspaceView &workspace = *found->second.workspace;
        const borealis::ui::PaneId pane = workspace.focused_pane();
        const auto old = found->second.sessions_by_pane.find(pane);
        if (old == found->second.sessions_by_pane.end()) {
            // 焦点格在表里没有会话即装配账目不一致（稿 §2 第 1 条不许这副形态出现）。这里不猜别的格：
            // 换错一格比不换更糟——重启是用户点下去的动作，静默作用到另一格等于改了别人的现场。
            AURORA_LOG_WARN("main", "restart: tab ", id, " pane ", pane, " has no session on file");
            return;
        }
        // 旧会话先 `close()` 再离手，但**留在 `retired` 里到本函数末尾**：`replace_view` 内部就地销毁
        // 旧视口，而 `~TerminalView` 要往它背后的会话写撤账空响应——先散会话就是取用已消散的对象
        // （稿 §5 那条次序；两份集成用例驱动台的 `retired_` 是同一形态）。
        old->second->close();
        std::shared_ptr<borealis::session::Session> retired = std::move(old->second);
        found->second.sessions_by_pane.erase(old);
        // 新会话**先登记再换视口**：`replace_view` 里 `wire_view` 与 `debounce_.forget(pane)` 之后补发的
        // 行列走 `dispatch_grid`，而那一条接缝是按表取会话的。换视口失败即整体回滚，账目不留在中间态。
        const std::shared_ptr<borealis::session::Session> fresh = std::move(session);
        found->second.sessions_by_pane[pane] = fresh;
        const std::shared_ptr<borealis::ui::TerminalView> view =
            build_pane_view(*found->second.owner, fresh, line_ending);
        if (!workspace.replace_view(pane, view)) {
            // 回滚只回到「账目一致」：旧会话已被 close，那一格因此停在已退出态。这一支只在装配账目
            // 自相矛盾时才走得到（格存在却没有视图、或两格共用一份视图），宁可留一格死的也不换错格。
            AURORA_LOG_WARN("main", "restart: replace_view refused on tab ", id, " pane ", pane);
            found->second.sessions_by_pane.erase(pane);
            found->second.sessions_by_pane[pane] = std::move(retired);
            return;
        }
        // 端点回调每次现查 `tab_workspaces`，故这一跳不需要重登记；旧会话已散、去重身份随之改变，
        // 因此只刷一次横幅计数与列名（`replace_view` 自己已重算过各格的 pill）。
        refresh_broadcast_ui();
    };

    /// @brief SSH 腿的会话构造（新开标签 / 重启换会话 / 切分新格三处共用的那一段）。
    ///
    /// 每次一份宽度表：这张表被写侧与折算侧共享，跨会话共用就是让两条连接互相污染列宽缓存（同本地腿
    /// 那一条理由）。`secret` 照旧移交给 `SshConnection`，调用方要留档就自己先复制一份。
    auto make_ssh_session = [&](const borealis::conn::SshProfile &ssh, std::optional<std::string> secret)
        -> std::shared_ptr<borealis::session::Session> {
        auto connection = std::make_unique<borealis::conn::SshConnection>(
            ssh, std::move(secret), kNominalViewport, reconnect_policy());
        auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();
        return std::make_shared<borealis::session::Session>(
            std::move(connection), kNominalViewport, settings.terminal.scrollback_limit,
            width_policy, make_terminal_defaults(settings), make_session_encoding(settings));
    };

    /// @brief SSH 腿的切分工厂：同一份档案 + 留档秘密再拨一条连接（新 TCP 会话，稿 §4 的 F13）。
    ///
    /// 档案与秘密都从两张表里**现读**，不在闭包里留副本：`on_secret` 那一腿会把询问到的材料改写进
    /// `ssh_secret_by_tab`，工厂拿副本就会重拨成过期的那份材料。表里没有档案身份即回空值——正常路径
    /// 不会走到（登记与建标签同批发生），留这一条是 fail-closed 而非第二重判据。
    auto make_ssh_pane_factory = [&](borealis::ui::TabId id) -> PaneSessionFactory {
        return [&ssh_profile_by_tab, &ssh_secret_by_tab, &make_ssh_session, id](
                   borealis::ui::PaneId /*origin*/) -> std::optional<PaneBundle> {
            const auto profile = ssh_profile_by_tab.find(id);
            if (profile == ssh_profile_by_tab.end()) {
                AURORA_LOG_WARN("main", "split refused: tab ", id, " carries no SSH profile on file");
                return std::nullopt;
            }
            std::optional<std::string> secret;
            if (const auto held = ssh_secret_by_tab.find(id); held != ssh_secret_by_tab.end()) {
                secret = held->second;
            }
            return PaneBundle{.session = make_ssh_session(profile->second, std::move(secret))};
        };
    };

    // 按档案重开一格 SSH 会话（裁决 7.99 D6）：新开一格是另一个意图，故这里只覆盖既有那一格。
    // 标签身份**不清账**——这一格仍是那张档案的会话，SFTP 面板与「重启」的分流判据都按它取料。
    auto restart_ssh_tab = [&](borealis::ui::TabId id, const borealis::conn::SshProfile &ssh,
                               std::optional<std::string> secret) -> void {
        std::optional<std::string> sftp_secret = secret;  // 留档副本先取，形参那份随后移交。
        auto session = make_ssh_session(ssh, std::move(secret));
        ssh_secret_by_tab[id] = std::move(sftp_secret);
        replace_pane_session(id, std::move(session));
        AURORA_LOG_INFO("main", "restart: tab ", id, " re-dialled by profile");
    };

    auto open_ssh_tab = [&](const borealis::conn::Profile &profile,
                            std::optional<std::string> secret) -> void {
        // SFTP 面板接线：留档副本先取，secret 照旧移交给 SshConnection。
        std::optional<std::string> sftp_secret = secret;
        const borealis::conn::SshProfile sftp_profile = profile.ssh;
        auto session = make_ssh_session(profile.ssh, std::move(secret));
        // 工厂要的标签号就是本支即将取到的那个号（同 `create_new_tab` 处那一条）：取号点只有
        // `create_new_tab_with` 里那一处，两处之间没有别的发号。
        const borealis::ui::TabId id = next_tab_id;
        create_new_tab_with(first_window, std::move(session), decode_tab_name(profile.name), TabKind::Ssh,
                            make_ssh_pane_factory(id));
        ssh_profile_by_tab[id] = sftp_profile;
        ssh_secret_by_tab[id] = std::move(sftp_secret);
        record_recent(profile.id);
    };

    // 本地档案会话：LocalProfile 与 LocalTerminalSpec 同构（命令/目录/环境三件），直搬。
    auto open_local_profile_tab = [&](const borealis::conn::Profile &profile) -> void {
        borealis::conn::LocalTerminalSpec spec;
        spec.command_line = profile.local.command_line;
        spec.working_directory = profile.local.working_directory;
        spec.environment = profile.local.environment;
        create_new_tab_with(first_window, make_local_session(spec), decode_tab_name(profile.name), TabKind::Local,
                            make_local_profile_pane_factory(profile.local));
        record_recent(profile.id);
    };

    // ---- 串口腿装配（`SPEC.FEAT.CONN.05` 批 3）--------------------------------------------
    //
    // 连接腿只吃档案：编码与发送行尾**不在** `SerialConnection`，是 `Session` 的构造入参
    // （裁决 7.110 D10①/D11① 的「折算点在写出侧一处」），故本段是这两条口径唯一的接线处。
    struct SerialSessionBundle {
        std::shared_ptr<borealis::session::Session> session{};
        borealis::conn::SerialConnection *connection{nullptr};
        borealis::term::LineEnding line_ending{borealis::term::LineEnding::Lf};
    };
    auto make_serial_session =
        [&](const borealis::conn::SerialProfile &serial) -> SerialSessionBundle {
        // 表外行尾名（手改 JSON）回落到缺省 LF：装载侧按 Choice 域把守，走到这里的表外名
        // 只可能来自绕过装载侧的构造，回落比让整条连接起不来温和。
        const auto ending =
            borealis::conn::serial_line_ending(serial.line_ending)
                .value_or(borealis::term::LineEnding::Lf);
        auto connection =
            std::make_unique<borealis::conn::SerialConnection>(serial, reconnect_policy());
        auto *raw = connection.get();  // 交进 Session 前留一份：热改波特率与当前档显示都读它。
        auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();
        auto session = std::make_shared<borealis::session::Session>(
            std::move(connection), kNominalViewport, settings.terminal.scrollback_limit,
            width_policy, make_terminal_defaults(settings),
            borealis::term::SessionEncoding{
                .name = serial.encoding,  // 串口缺省 GB18030（裁决 7.6），不吃全局 `terminal.encoding`。
                .unrepresentable = settings.terminal.unrepresentable,
            },
            ending);
        return SerialSessionBundle{.session = std::move(session), .connection = raw,
                                   .line_ending = ending};
    };

    auto open_serial_tab = [&](const borealis::conn::Profile &profile) -> void {
        auto bundle = make_serial_session(profile.serial);
        create_new_tab_with(first_window, bundle.session, decode_tab_name(profile.name), TabKind::Serial,
                            make_refusing_pane_factory("serial device is exclusive"),
                            bundle.line_ending);
        if (current_tab_id.has_value()) {
            serial_by_tab[current_tab_id.value()] =
                SerialTabBinding{.profile = profile.serial, .connection = bundle.connection};
        }
        record_recent(profile.id);
    };

    // ---- Telnet 腿装配（`SPEC.FEAT.CONN.06` 批 3）--------------------------------------------
    //
    // 与串口同一条分界（裁决 7.112 D5①/D8①）：编码与发送行尾**不在** `TelnetConnection`，这里
    // 是这两条口径唯一的接线处。两处取值都刻意不新造：编码吃既有 `make_session_encoding(settings)`
    // （本地/SSH 同一枚现成件，缺省 UTF-8 由裁决 7.6 定），行尾是 NVT 规程要求的固定 `Crlf`
    // （RFC 854），**不可配**——档案里没有这一项，故也没有「改设置不回溯在档」那个坑。
    struct TelnetSessionBundle {
        std::shared_ptr<borealis::session::Session> session{};
        borealis::term::LineEnding line_ending{kTelnetLineEnding};
    };
    auto make_telnet_session =
        [&](const borealis::conn::TelnetProfile &telnet) -> TelnetSessionBundle {
        auto connection =
            std::make_unique<borealis::conn::TelnetConnection>(telnet, reconnect_policy());
        auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();
        auto session = std::make_shared<borealis::session::Session>(
            std::move(connection), kNominalViewport, settings.terminal.scrollback_limit,
            width_policy, make_terminal_defaults(settings), make_session_encoding(settings),
            kTelnetLineEnding);
        // 折送入参与下面视口粘贴档读的是**同一枚**常量（bundle 的缺省值就是它），不是两处各写一遍
        // 字面量——各改其一就会出现「敲回车 CRLF、粘多行 CR」。
        return TelnetSessionBundle{.session = std::move(session)};
    };

    /// @brief Telnet 腿的切分工厂：按 `telnet_by_tab` 的那份档案重拨，行尾仍固定 `Crlf`（稿 §4）。
    ///
    /// 与 SSH 那条同样**现读**表里的档案：本表就是这一张标签的 Telnet 身份，重启前后都是同一份，
    /// 但闭包留副本会让「表被撤了还在拨」这条 fail-closed 支路消失。
    auto make_telnet_pane_factory = [&](borealis::ui::TabId id) -> PaneSessionFactory {
        return [&telnet_by_tab, &make_telnet_session, id](
                   borealis::ui::PaneId /*origin*/) -> std::optional<PaneBundle> {
            const auto found = telnet_by_tab.find(id);
            if (found == telnet_by_tab.end()) {
                AURORA_LOG_WARN("main", "split refused: tab ", id, " carries no Telnet profile on file");
                return std::nullopt;
            }
            auto bundle = make_telnet_session(found->second);
            return PaneBundle{.session = std::move(bundle.session), .line_ending = bundle.line_ending};
        };
    };

    auto open_telnet_tab = [&](const borealis::conn::Profile &profile) -> void {
        auto bundle = make_telnet_session(profile.telnet);
        // 工厂要的号同 SSH 腿那一条理由：`create_new_tab_with` 里 `next_tab_id++` 是本支唯一的取号点。
        const borealis::ui::TabId id = next_tab_id;
        create_new_tab_with(first_window, bundle.session, decode_tab_name(profile.name), TabKind::Telnet,
                            make_telnet_pane_factory(id), bundle.line_ending);
        telnet_by_tab[id] = profile.telnet;
        record_recent(profile.id);
    };

    // 设备枚举的 latest-value 快照（稿 §5，AGENTS §4.5 第 25 条）：`serial_list_ports()` 含目录
    // IO 与逐个试开，只能在后台跑；UI 读到的恒是「合成后的最终值」那一份副本，与隧道/密钥快照
    // 同族（可覆盖的量，不排事件队列）。
    auto ports_mutex = std::make_shared<std::mutex>();
    auto ports_latest = std::make_shared<std::vector<borealis::conn::SerialPortInfo>>();
    auto read_ports_snapshot = [ports_mutex,
                                ports_latest]() -> std::vector<borealis::conn::SerialPortInfo> {
        const std::lock_guard lock{*ports_mutex};
        return *ports_latest;
    };
    auto fetch_ports = [ports_mutex, ports_latest]() -> bool {
        auto ports = borealis::platform::serial_list_ports();
        static_cast<void>(borealis::conn::sort_serial_ports(ports));
        const std::lock_guard lock{*ports_mutex};
        *ports_latest = std::move(ports);
        return true;  // 任务值即「跑完了」：快照才是产物，布尔只把 `Task` 的值类型坐实。
    };
    // 启动取一次（裁决 7.110 D8①）：**不带** UI 通知——那一刻 `Application::loop_begin()` 还没跑，
    // 主线程投递器未装，`.then()` 的回调会落在 worker 线程上；而快照写入已在任务体内锁好，
    // 热身的产物只等向导来读。
    static_cast<void>(au::async(fetch_ports));
    // 向导那两路（开设备行 / 点刷新）都在 run 循环里发起，回投有保证，故可以通知重建下拉。
    auto request_ports = [&](const std::weak_ptr<borealis::ui::ConnectionWizard> &notify) -> void {
        auto task = au::async(fetch_ports);
        static_cast<void>(task.then([notify](const au::Result<bool> &) -> void {
            if (auto wizard = notify.lock()) {
                wizard->reload_ports();
            }
        }));
    };

    // 凭据询问件（CONN.09 降级腿）：本期无 OS 凭据库后端，要材料的认证一律走这里；
    // 明文只经 on_secret 交给**本次**装配，不落盘不进日志（裁决 7.93）。
    auto pending_profile = std::optional<borealis::conn::Profile>{};
    // 隧道启动的异步凭据腿（D7）：询问型档案的隧道把「档案副本+定义」留在这里，
    // 明文回到 on_secret 后才真正拉起 Tunnel（同 pending_profile 的先例形态）。
    auto pending_tunnel =
        std::optional<std::pair<borealis::conn::SshProfile, borealis::conn::TunnelSpec>>{};
    // 重启时的凭据询问（裁决 7.99 D6）：形态同 `pending_profile`，差别在回来的材料喂给**已存在的
    // 那一格**（`restart_ssh_tab`）而不是新开一格。询问型档案断线后手上没有材料，这一支就是它的出口。
    auto pending_restart =
        std::optional<std::pair<borealis::ui::TabId, borealis::conn::SshProfile>>{};
    // 拉起隧道主体的装配点（下方隧道段赋值；询问回-call 与自启扫描共用）。
    auto launch_tunnel =
        std::function<void(const borealis::conn::SshProfile &, std::optional<std::string>,
                           const borealis::conn::TunnelSpec &)>{};
    // 密钥两腿的异步凭据回-call（批 3 段赋值）：导出「带口令的钥匙必须先问口令」（D12⑵
    // 的反面）；推送「询问型档案先问认证材料」（D7①，同 pending_tunnel 的先例形态）。
    auto pending_export = std::optional<std::pair<std::string, std::string>>{};
    auto pending_push = std::optional<
        std::tuple<std::string, borealis::conn::SshProfile, std::string>>{};
    auto submit_keys_export =
        std::function<void(const std::string &, std::optional<std::string>)>{};
    auto submit_keys_push = std::function<void(const std::string &,
                                               const borealis::conn::SshProfile &,
                                               std::optional<std::string>, std::string_view)>{};
    borealis::ui::CredentialPrompt::Hooks prompt_hooks;
    prompt_hooks.on_secret =
        [&open_ssh_tab, &restart_ssh_tab, &pending_profile, &pending_restart,
         &pending_tunnel, &pending_export, &pending_push,
         &launch_tunnel, &submit_keys_export, &submit_keys_push](const std::string &secret) -> void {
        if (pending_restart.has_value()) {
            const auto [id, ssh] = std::move(*pending_restart);
            pending_restart.reset();
            restart_ssh_tab(id, ssh, secret);
            return;
        }
        if (pending_profile.has_value()) {
            auto profile = std::move(*pending_profile);
            pending_profile.reset();
            open_ssh_tab(profile, secret);
            return;
        }
        if (pending_tunnel.has_value()) {
            auto [ssh_profile, spec] = std::move(*pending_tunnel);
            pending_tunnel.reset();
            launch_tunnel(ssh_profile, secret, spec);
            return;
        }
        if (pending_export.has_value()) {
            auto [path, name] = std::move(*pending_export);
            pending_export.reset();
            submit_keys_export(path, secret);
            return;
        }
        if (pending_push.has_value()) {
            auto [path, ssh_profile, line] = std::move(*pending_push);
            pending_push.reset();
            submit_keys_push(path, ssh_profile, secret, line);
        }
    };
    prompt_hooks.on_cancel = [&pending_profile, &pending_restart, &pending_tunnel,
                              &pending_export, &pending_push]() -> void {
        pending_profile.reset();
        pending_restart.reset();
        pending_tunnel.reset();
        pending_export.reset();
        pending_push.reset();
    };
    auto credential_prompt =
        std::make_shared<borealis::ui::CredentialPrompt>(*host, std::move(prompt_hooks));

    auto connect_profile = [&](const borealis::conn::Profile &profile) -> void {
        if (profile.type == borealis::conn::ConnectionType::Local) {
            open_local_profile_tab(profile);
            return;
        }
        if (profile.type == borealis::conn::ConnectionType::Serial) {
            open_serial_tab(profile);  // 串口无凭据可问：档案即全部料（CONN.09 的审计天然过）。
            return;
        }
        // Telnet 同串口那条：没有认证协议、没有材料可要（登录提示是远端程序给的文本，不是
        // 本仓的凭据链），故它也必须**挡在** `secret_ask_for` 之前。少了这一支不会崩，但会静默
        // 走错腿：空白 `profile.ssh` 的 `auth_method` 缺省是 "agent" ⇒ `SecretAsk::None` ⇒ 拿一个
        // 空主机去拨 SSH，标签名却还是 Telnet 档案名——拨号失败的事实被挂到一条不存在的连接上。
        if (profile.type == borealis::conn::ConnectionType::Telnet) {
            open_telnet_tab(profile);
            return;
        }
        const auto ask = borealis::conn::secret_ask_for(
            borealis::conn::normalized_auth_method(profile.ssh));
        if (ask == borealis::conn::SecretAsk::None) {
            open_ssh_tab(profile, std::nullopt);  // agent 认证：无材料可要。
            return;
        }
        // Reference 句柄本期同样落询问：真实 OS 后端（libsecret/Keychain/CredMan）留独立
        // 任务，这是 CONN.09 的降级语义而非捷径（设计稿 Q3）。
        pending_profile = profile;
        credential_prompt->ask(ask, profile.name);
    };

    borealis::ui::ConnectionWizard::Hooks wizard_hooks;
    wizard_hooks.on_save = [&store](const borealis::conn::Profile &profile) -> void {
        auto next = store.settings();
        bool replaced = false;
        for (auto &existing : next.profiles) {
            if (existing.id == profile.id) {
                existing = profile;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            next.profiles.push_back(profile);
        }
        static_cast<void>(store.replace(std::move(next)));
    };
    // 设备枚举回投用的弱句柄：向导自己就是回调的宿主，握强引用会让它永不被释放。
    std::weak_ptr<borealis::ui::ConnectionWizard> wizard_ref;
    wizard_hooks.current_ports = read_ports_snapshot;
    wizard_hooks.refresh_ports = [&request_ports, &wizard_ref]() -> void {
        request_ports(wizard_ref);
    };
    wizard_hooks.serial_defaults = [&settings]() -> borealis::conn::SerialProfile {
        // D4①：设置域的默认值灌进新建档案那一刻，之后各走各的（档案才是连接的料源）。
        const auto &d = settings.connection.serial;
        auto profile = borealis::conn::SerialProfile{};
        profile.baud = d.baud;
        profile.data_bits = d.data_bits;
        profile.stop_bits = d.stop_bits;
        profile.parity = d.parity;
        profile.flow = d.flow;
        profile.line_ending = d.line_ending;
        profile.encoding = d.encoding;
        return profile;
    };
    auto wizard = std::make_shared<borealis::ui::ConnectionWizard>(*host, std::move(wizard_hooks));
    wizard_ref = wizard;

    borealis::ui::ConnectionSidebar::Hooks sidebar_hooks;
    sidebar_hooks.load = [&store]() -> std::vector<borealis::conn::Profile> {
        return store.settings().profiles;
    };
    sidebar_hooks.persist = [&store](const std::vector<borealis::conn::Profile> &table) -> bool {
        auto next = store.settings();
        next.profiles = table;
        return !store.replace(std::move(next)).has_value();
    };
    sidebar_hooks.connect = connect_profile;
    sidebar_hooks.create_new = [&wizard]() -> void { wizard->open_new(); };
    sidebar_hooks.edit = [&wizard](const borealis::conn::Profile &profile) -> void {
        wizard->open_edit(profile);
    };
    // `~/.ssh/config` 只读导入（D5）：读盘 + parse_ssh_config + 确定性 id 去重；单向不回写。
    sidebar_hooks.import_ssh_config = [&store]() -> std::size_t {
        const char *home = std::getenv("HOME");
        if (home == nullptr) {
            return 0;
        }
        std::ifstream file{std::filesystem::path{home} / ".ssh" / "config"};
        if (!file.is_open()) {
            return 0;
        }
        const std::string content{std::istreambuf_iterator<char>{file},
                                  std::istreambuf_iterator<char>{}};
        const auto imported = borealis::conn::parse_ssh_config(content);
        auto next = store.settings();
        std::size_t added = 0;
        for (const auto &profile : imported) {
            bool exists = false;
            for (const auto &existing : next.profiles) {
                if (existing.id == profile.id) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                next.profiles.push_back(profile);
                ++added;
            }
        }
        if (added > 0) {
            static_cast<void>(store.replace(std::move(next)));
        }
        return added;
    };
    sidebar_hooks.recent_ids = [&store]() -> std::vector<std::string> {
        std::vector<std::string> ids;
        for (const auto &entry : store.settings().connection.recent) {
            ids.push_back(entry.profile_id);
        }
        return ids;
    };
    auto sidebar = std::make_shared<borealis::ui::ConnectionSidebar>(*host, std::move(sidebar_hooks));

    // 打开入口（Q4/Q5）：走命令层（快捷键 / 命令面板 / 菜单的共同真源，同 `settings.open`）；
    // 默认不绑死键位（进 PREF.04 体系后用户自行绑定）。
    au::Command toggle_connections;
    toggle_connections.id = "connections.toggle";
    toggle_connections.title = borealis::ui::settings_label("connections.title");
    toggle_connections.category = "terminal";
    toggle_connections.action = [&sidebar]() -> void { sidebar->toggle(); };
    toggle_connections.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(toggle_connections));

    // ---- SSH 隧道装配（SPEC.FEAT.CONN.08，裁决 7.97 D5–D8）----
    // 隧道对象与运行态快照都握在本装配层（D5①）：面板关着隧道照跑，进程退出
    // 时 tunnel_entries 析构逐个 stop()（传输腿既定 join 纪律）。快照经
    // mutex+map 的最新值形态交换（D6①；与 AGENTS §25「合成后的最终值」同义，
    // 状态是可覆盖的量，不需要事件队列）。
    auto tunnel_mutex = std::make_shared<std::mutex>();
    auto tunnel_runtimes =
        std::make_shared<std::map<std::string, borealis::ui::TunnelRuntime>>();
    auto tunnel_skipped = std::make_shared<std::set<std::string>>();
    std::map<std::string, TunnelEntry> tunnel_entries{};

    launch_tunnel = [&store, &tunnel_entries, tunnel_mutex, tunnel_runtimes, tunnel_skipped](
                        const borealis::conn::SshProfile &ssh_profile,
                        std::optional<std::string> secret,
                        const borealis::conn::TunnelSpec &spec) -> void {
        auto sink = std::make_unique<TunnelSink>();
        sink->mutex = tunnel_mutex;
        sink->runtimes = tunnel_runtimes;
        sink->id = spec.id;
        // 快照先归零再起线程：start() 之后的第一个事件（Dialing）不能被这次清零盖掉。
        {
            const std::lock_guard lock{*tunnel_mutex};
            (*tunnel_runtimes)[spec.id] = borealis::ui::TunnelRuntime{};  // 新一轮从空白起。
            tunnel_skipped->erase(spec.id);
        }
        // 逐条重试档直接取自定义（D9②），不再吃全局缺省。
        auto tunnel = std::make_unique<borealis::conn::Tunnel>(ssh_profile, std::move(secret),
                                                               spec, spec.retry);
        tunnel->start(*sink);
        tunnel_entries[spec.id] = TunnelEntry{std::move(sink), std::move(tunnel)};
    };

    borealis::ui::TunnelPanel::Hooks tunnel_hooks;
    tunnel_hooks.load = [&store]() -> std::vector<borealis::conn::TunnelSpec> {
        return store.settings().tunnels;
    };
    tunnel_hooks.persist = [&store](const std::vector<borealis::conn::TunnelSpec> &table) -> bool {
        auto next = store.settings();
        next.tunnels = table;
        return !store.replace(std::move(next)).has_value();
    };
    tunnel_hooks.profiles = [&store]() -> std::vector<borealis::conn::Profile> {
        return store.settings().profiles;
    };
    // D7 凭据链：agent 型（SecretAsk::None）静默拉起；询问型经 credential_prompt
    // 异步回-call（启动序绝不弹模态框——自启路径根本不走这里）。
    tunnel_hooks.start =
        [&store, &credential_prompt, &pending_tunnel, &launch_tunnel, tunnel_mutex,
         tunnel_runtimes](const borealis::conn::TunnelSpec &spec) -> void {
        const auto settings = store.settings();
        const borealis::conn::Profile *target = nullptr;
        for (const auto &profile : settings.profiles) {
            if (profile.id == spec.profile_id &&
                profile.type == borealis::conn::ConnectionType::Ssh) {
                target = &profile;
                break;
            }
        }
        if (target == nullptr) {
            const std::lock_guard lock{*tunnel_mutex};
            auto &cell = (*tunnel_runtimes)[spec.id];
            cell.state = borealis::conn::TunnelState::Failed;
            cell.error = borealis::conn::TunnelError::DialFailed;  // 承载档案已不存在。
            return;
        }
        const auto ask =
            borealis::conn::secret_ask_for(borealis::conn::normalized_auth_method(target->ssh));
        if (ask == borealis::conn::SecretAsk::None) {
            launch_tunnel(target->ssh, std::nullopt, spec);
            return;
        }
        pending_tunnel = std::pair{target->ssh, spec};
        credential_prompt->ask(ask, target->name);
    };
    tunnel_hooks.stop = [&tunnel_entries, tunnel_mutex,
                         tunnel_runtimes](const std::string &id) -> void {
        const auto it = tunnel_entries.find(id);
        if (it != tunnel_entries.end()) {
            it->second.tunnel->stop();  // join 上界＝一个轮询拍 + 一次建连超时（既定纪律）。
            tunnel_entries.erase(it);   // 析构即清 secret_（CONN.09）。
        }
        const std::lock_guard lock{*tunnel_mutex};
        (*tunnel_runtimes)[id] = borealis::ui::TunnelRuntime{};
    };
    tunnel_hooks.snapshot = [tunnel_mutex, tunnel_runtimes,
                             tunnel_skipped]() -> std::map<std::string, borealis::ui::TunnelRuntime> {
        const std::lock_guard lock{*tunnel_mutex};
        auto out = *tunnel_runtimes;
        for (const auto &id : *tunnel_skipped) {
            out[id].autostart_skipped = true;  // D8 细则：行内提示「需先存凭据」。
        }
        return out;
    };
    borealis::ui::TunnelPanel tunnel_panel{*host, std::move(tunnel_hooks)};

    // 启动序自启扫描（D8②细则）：只拉起「autostart ∧ 凭据可静默解析」的隧道；
    // 询问型记入 skipped 集供行内提示——本路径零模态框、零阻塞等待。
    {
        const auto settings = store.settings();
        for (const auto &spec : settings.tunnels) {
            if (!spec.autostart) {
                continue;
            }
            const borealis::conn::Profile *target = nullptr;
            for (const auto &profile : settings.profiles) {
                if (profile.id == spec.profile_id &&
                    profile.type == borealis::conn::ConnectionType::Ssh) {
                    target = &profile;
                    break;
                }
            }
            const bool silent =
                target != nullptr &&
                borealis::conn::secret_ask_for(
                    borealis::conn::normalized_auth_method(target->ssh)) ==
                    borealis::conn::SecretAsk::None;
            if (silent) {
                launch_tunnel(target->ssh, std::nullopt, spec);
            } else {
                const std::lock_guard lock{*tunnel_mutex};
                tunnel_skipped->insert(spec.id);
            }
        }
    }

    au::Command open_tunnels;
    open_tunnels.id = "tunnels.open";
    open_tunnels.title = borealis::ui::settings_label("tunnel.title");
    open_tunnels.category = "terminal";
    open_tunnels.action = [&tunnel_panel]() -> void { tunnel_panel.toggle(); };
    open_tunnels.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_tunnels));

    // SFTP 浏览器面板（SPEC.FEAT.CONN.04）：右停靠浮层，与侧栏同族。三条 Hooks 全部
    // 指回上面的标签接线数据，不新造状态：活动 SSH 档案、CONN.09 凭据链的句柄解析、
    // 标签判活。stack 对象即可——命令与帧回调都只捕获引用，析构顺序上它晚于浮层宿主
    // 的使用期（main 末尾），析构里 join worker 与断链。
    borealis::ui::SftpPanel::Hooks sftp_hooks;
    sftp_hooks.current_ssh_profile = [&ssh_profile_by_tab, &current_tab_id]()
        -> std::optional<borealis::conn::SshProfile> {
        if (!current_tab_id.has_value()) {
            return std::nullopt;
        }
        const auto it = ssh_profile_by_tab.find(current_tab_id.value());
        return it != ssh_profile_by_tab.end() ? std::optional{it->second} : std::nullopt;
    };
    sftp_hooks.resolve_secret = [&ssh_secret_by_tab, &current_tab_id]() -> std::optional<std::string> {
        if (!current_tab_id.has_value()) {
            return std::nullopt;
        }
        const auto it = ssh_secret_by_tab.find(current_tab_id.value());
        return (it != ssh_secret_by_tab.end() && it->second.has_value()) ? it->second : std::nullopt;
    };
    sftp_hooks.ssh_tab_alive = [&ssh_profile_by_tab, &current_tab_id]() -> bool {
        return current_tab_id.has_value() && ssh_profile_by_tab.count(current_tab_id.value()) > 0;
    };
    borealis::ui::SftpPanel sftp_panel{*host, std::move(sftp_hooks)};

    au::Command toggle_sftp;
    toggle_sftp.id = "sftp.toggle";
    toggle_sftp.title = borealis::ui::settings_label("sftp.title");
    toggle_sftp.category = "terminal";
    toggle_sftp.action = [&sftp_panel]() -> void { sftp_panel.toggle(); };
    toggle_sftp.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(toggle_sftp));

    // ---- SSH 密钥装配（SPEC.FEAT.CONN.10 批 3，裁决 7.106 D4–D9 + 7.107/7.108；稿 §5）----
    // 串行 worker（D7① 同族：同一时刻只跑一把钥匙的事）+ mutex 保护的 latest-value 快照
    // （D6① 同族），面板每帧取副本。五件事全是阻塞 IO 或长计算（RSA-4096 秒级、建连 10 s
    // 上界），一律在 worker 线程上跑。启动序不扫盘（PREF.06 首屏零同步 IO）：第一次扫面
    // 发生在 keys.open 的 request_scan。
    KeysWorker keys_worker;
    const std::string keys_user_dir = borealis::platform::ssh_user_dir();

    auto submit_keys_scan = [&keys_worker, keys_user_dir](std::vector<std::string> raw_dirs)
        -> void {
        keys_worker.submit(
            borealis::ui::KeysOpKind::Scan, {},
            [raw_dirs, keys_user_dir, mutex = keys_worker.mutex(),
             latest = keys_worker.latest()]() -> void {
                run_keys_scan(mutex, latest, raw_dirs, keys_user_dir);
            });
    };

    // 导出公钥的提交点（hook 与凭据回-call 共用）：口令已在入口处问过或本就不需要。
    submit_keys_export = [&](const std::string &path, std::optional<std::string> secret) -> void {
        keys_worker.submit(
            borealis::ui::KeysOpKind::Export, path,
            [path, secret = std::move(secret), mutex = keys_worker.mutex(),
             latest = keys_worker.latest(), raw_dirs = store.settings().key_dirs,
             keys_user_dir]() -> void {
                const auto outcome =
                    borealis::conn::export_public_key(path, std::move(secret));
                run_keys_scan(mutex, latest, raw_dirs, keys_user_dir);
                const std::lock_guard lock{*mutex};
                latest->notice_key = std::string{borealis::ui::key_export_outcome_key(outcome)};
                latest->notice_arg.clear();
            });
    };

    // 推送公钥的提交点（hook 与凭据回-call 共用）：档案副本与公钥行按值进任务，
    // 认证材料随 dial 返回即消失（CONN.09 的明文纪律）。
    submit_keys_push = [&](const std::string &path, const borealis::conn::SshProfile &profile,
                           std::optional<std::string> secret, std::string_view line) -> void {
        keys_worker.submit(
            borealis::ui::KeysOpKind::Push, path,
            [path_target = path, profile, secret = std::move(secret),
             line = std::string{line}, mutex = keys_worker.mutex(),
             latest = keys_worker.latest()]() -> void {
                const auto report =
                    borealis::conn::push_public_key(profile, std::move(secret), line);
                const std::lock_guard lock{*mutex};
                latest->push_step = borealis::conn::push_stage_index(report.stage);
                latest->notice_key = std::string{borealis::ui::key_push_notice_key(report)};
                latest->notice_arg.clear();
            });
    };

    borealis::ui::KeysPanel::Hooks keys_hooks;
    keys_hooks.snapshot = [&keys_worker]() -> borealis::ui::KeysSnapshot {
        return keys_worker.snapshot();
    };
    keys_hooks.request_scan = [&store, &submit_keys_scan]() -> void {
        submit_keys_scan(store.settings().key_dirs);
    };
    // 目录腿（D2③/D11②）：面板只交回选中的路径；空串＝取消或平台起不来，整条不跑且
    // 不留痕（稿 §8 判据）。落盘逐字进第七域（去重归 `normalize_key_dirs`，往返逐字
    // 有 utest_config 证人），随后重扫让新目录面与可达性留痕同帧可见。
    keys_hooks.pick_key_dir = []() -> std::string {
        aurora::file_dialog::Options opts;
        opts.title = "Choose SSH key directory";  // 平台对话框标题不经本仓词条表（picked_save_path 同口径）
        const au::Result<std::string> result = aurora::file_dialog::open_folder(opts);
        return result.ok() ? result.value() : std::string{};
    };
    // 面板侧保证 add_dir 只收非空选路（D11②：空＝取消，整条腿在面板就止住）；这里逐字
    // 落盘不去重——规范化（可达性、~/.ssh 恒在表首）是扫盘时 normalize_key_dirs 的事。
    keys_hooks.add_dir = [&store, &submit_keys_scan](const std::string &dir) -> void {
        auto next = store.settings();
        next.key_dirs.push_back(dir);
        static_cast<void>(store.replace(std::move(next)));
        submit_keys_scan(store.settings().key_dirs);
    };
    keys_hooks.profiles = [&store]() -> std::vector<borealis::conn::Profile> {
        return store.settings().profiles;
    };
    // 一键复制（判据 4）：帧边界落系统剪贴板（ClipboardOutbox 是主线程静态件）＋即时留痕。
    keys_hooks.copy_line = [&keys_worker](const std::string &line) -> void {
        borealis::session::ClipboardOutbox::write(line);
        keys_worker.publish_notice("keys.notice.copied", {});
    };
    // 生成（判据 5）：面板已把名称/注释/口令一致三闸判过，这里只折成传输腿的请求形态
    // （口令空串与没填是同一件事，`GenerateRequest` 头注同口径）。
    keys_hooks.generate = [&keys_worker, &store, keys_user_dir](
                              const borealis::ui::KeysPanel::GenerateDraft &draft) -> void {
        keys_worker.submit(
            borealis::ui::KeysOpKind::Generate, {},
            [draft, mutex = keys_worker.mutex(), latest = keys_worker.latest(),
             raw_dirs = store.settings().key_dirs, keys_user_dir]() -> void {
                auto request = borealis::conn::GenerateRequest{};
                request.type =
                    draft.rsa ? borealis::conn::KeyType::Rsa : borealis::conn::KeyType::Ed25519;
                request.rsa_bits = static_cast<std::uint16_t>(draft.rsa_bits);
                request.directory = draft.directory;
                request.basename = draft.basename;
                request.comment = draft.comment;
                request.passphrase = draft.passphrase;
                const auto outcome = borealis::conn::generate_key(request);
                run_keys_scan(mutex, latest, raw_dirs, keys_user_dir);
                const std::lock_guard lock{*mutex};
                latest->notice_key = std::string{borealis::ui::key_generate_outcome_key(outcome)};
                latest->notice_arg.clear();
            });
    };
    // 导出入口（判据 3 的「公钥缺失」出口；D12⑵ 反面）：带口令的钥匙先问口令再提交，
    // 无口令的直接进队列。行表在快照里，这里不碰文件系统。
    keys_hooks.export_public = [&](const std::string &path) -> void {
        const auto snapshot = keys_worker.snapshot();
        for (const auto &row : snapshot.rows) {
            if (row.path != path) {
                continue;
            }
            if (row.encrypted) {
                pending_export = std::pair{path, row.basename};
                credential_prompt->ask(borealis::conn::SecretAsk::Passphrase, row.basename);
            } else {
                submit_keys_export(path, std::nullopt);
            }
            return;
        }
    };
    // 推送（判据 6）：目标档案取 SSH 子集里点中的那一个；询问型档案经凭据链异步回来
    // 再提交（启动序与点击序都不做模态等待之外的事——这里就是模态询问本身）。
    keys_hooks.push = [&](const std::string &path, const std::string &profile_id) -> void {
        const auto snapshot = keys_worker.snapshot();
        const auto line = keys_line_of(snapshot, path);
        if (line.empty()) {
            return;  // 行已不在或拼不出公钥单行（快照错帧的那道防，面板侧同款）。
        }
        const borealis::conn::Profile *target = nullptr;
        for (const auto &profile : store.settings().profiles) {
            if (profile.id == profile_id &&
                profile.type == borealis::conn::ConnectionType::Ssh) {
                target = &profile;
                break;
            }
        }
        if (target == nullptr) {
            return;
        }
        const auto ask = borealis::conn::secret_ask_for(
            borealis::conn::normalized_auth_method(target->ssh));
        if (ask == borealis::conn::SecretAsk::None) {
            submit_keys_push(path, target->ssh, std::nullopt, line);
            return;
        }
        pending_push = std::tuple{path, target->ssh, line};
        credential_prompt->ask(ask, target->name);
    };
    keys_hooks.delete_key = [&keys_worker, &store, keys_user_dir](const std::string &path)
        -> void {
        keys_worker.submit(
            borealis::ui::KeysOpKind::Delete, path,
            [path, mutex = keys_worker.mutex(), latest = keys_worker.latest(),
             raw_dirs = store.settings().key_dirs, keys_user_dir]() -> void {
                const auto outcome = borealis::conn::delete_key_pair(path);
                run_keys_scan(mutex, latest, raw_dirs, keys_user_dir);
                const std::lock_guard lock{*mutex};
                latest->notice_key = std::string{borealis::ui::key_delete_outcome_key(outcome)};
                latest->notice_arg.clear();
            });
    };
    borealis::ui::KeysPanel keys_panel{*host, std::move(keys_hooks)};

    au::Command open_keys;
    open_keys.id = "keys.open";
    open_keys.title = borealis::ui::settings_label("keys.title");
    open_keys.category = "terminal";
    open_keys.action = [&keys_panel]() -> void { keys_panel.toggle(); };
    open_keys.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_keys));

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
    open_search.action = [&tab_workspaces, &target_window, &current_tab_id]() -> void {
        // 落点按窗解析（`SPEC.FEAT.WS.03` 稿 D7）：这一条经框架快捷键层进来，闭包拿不到派发来源，
        // 于是「哪一扇窗的当前标签」只能问 tracker——最近被碰过的那一扇。取不到窗只剩一种形态
        //（主窗自己已散），此时退回装配层那枚主窗别名而不是什么都不做。窗内的格仍取 1 号视图：那是
        // 既有形态，逐格搜索属 `SPEC.FEAT.INTERACT.04` 的账，本条不动它。
        WorkspaceWindow *const resolved = target_window();
        const std::optional<borealis::ui::TabId> target =
            resolved == nullptr ? current_tab_id : resolved->current_tab_id;
        if (target.has_value()) {
            auto it = tab_workspaces.find(target.value());
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
    // 占位符与空态文案**不在这里交**：框架按 `AURORA_DEFAULT_PLACEHOLDER_KEY` /
    // `AURORA_DEFAULT_EMPTY_MESSAGE_KEY` 两条 key 现查 `default_string_table()`（G38 / G39 回货形态），
    // 而本仓的中文词条已由 `install_settings_strings()` 装进那张表并把缺省档设成 `zh`。
    // 刻意不调 `set_placeholder()` / `set_empty_message()`：那两枚是**文本覆盖**，置位即不再走 i18n，
    // 运行期切 locale 就再也翻不动这两句（裁决 **7.92**①）。

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

    // 新开一扇窗在命令面板里那一条（`SPEC.FEAT.WS.03` 稿 D2）：**只登记命令、不登记 `default_binding`**。
    // `Ctrl+Shift+N` 是分屏保留键表的第十五条，由焦点 pane 的按键前置过滤消费（`ui::workspace_command`），
    // 两层都绑就会一次按键开两扇窗；而快捷键层的动作闭包不带发起信息（框架 `ShortcutBinding::action`
    // 是无参闭包），连「哪一扇窗按的」都判不出来。键位因此只在保留位表里有一份，面板这一条只给入口。
    au::Command new_window;
    new_window.id = "workspace.new_window";
    new_window.title = borealis::ui::settings_label("workspace.action.new_window");
    new_window.category = "workspace";
    new_window.action = [&open_new_window_request]() -> void {
        if (open_new_window_request) {
            open_new_window_request();
        }
    };
    new_window.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(new_window));

    // 全屏切换（`SPEC.FEAT.WS.06`，判据文 §3 D1）：F11 在普通与全屏之间切换，ESC 不退出（避免与会话内 ESC 冲突）。
    au::Command toggle_fullscreen;
    toggle_fullscreen.id = "workspace.toggle_fullscreen";
    toggle_fullscreen.title = borealis::ui::settings_label("workspace.action.toggle_fullscreen");
    toggle_fullscreen.category = "workspace";
    // 全屏落点按窗解析（稿 D7）：`app.window()` 恒是主宿主那一只（稿 §0 的 F4，`itest_workspace_window`
    // 第四条已钉），在 B 窗按 `F11` 却把 A 窗拉全屏就是答非所问。取不到窗（主窗已散）仍回应用级那一只，
    // 形态与上面 `search.open` 的兜底同一条。
    toggle_fullscreen.action = [&app, &target_window, &first_window]() -> void {
        WorkspaceWindow *const resolved = target_window();
        au::WindowHost *const host_of_target =
            resolved == nullptr ? app.window_host(first_window.id) : app.window_host(resolved->id);
        au::Window *const target = host_of_target == nullptr ? app.window() : host_of_target->window();
        if (target == nullptr) {
            return;
        }
        const auto mode = target->window_mode();
        target->set_fullscreen(mode != au::WindowMode::FullScreen);
    };
    toggle_fullscreen.default_binding = au::KeyCombo{au::KeyCode::F11};
    toggle_fullscreen.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(toggle_fullscreen));

    // 撤销关闭标签（`SPEC.FEAT.WS.10`）：从闭包栈弹出最近关闭的标签规格，重建会话并插入到原位置。
    // 落点窗按稿 D7 解析（批 3-C）：闭包栈本就是**每窗一份**（`perform_close` 写的是关它那一窗的栈，
    // 批 1-B 就定了归属），此前只有这一处读取还钉在主窗别名上——那会让 B 窗关掉的标签被 A 窗的
    // `Ctrl+Shift+T` 捞走，而捞回去的那一张挂在 A 窗的标签栏里。取不到窗（主窗已散）仍按主窗处理，
    // 与上面 `search.open` 同一条兜底。
    au::Command undo_close_tab;
    undo_close_tab.id = "tab.undo_close";
    undo_close_tab.title = borealis::ui::settings_label("tab.action.undo_close");
    undo_close_tab.category = "workspace";
    undo_close_tab.action = [&target_window, &first_window, &settings, &make_local_session,
                             &build_pane_view, &next_tab_id, &wire_workspace_hooks,
                             &make_local_pane_factory, &tab_workspaces, &tab_kind,
                             &attach_broadcast_sink, &rebuild_root_children]() -> void {
        WorkspaceWindow *const resolved = target_window();
        WorkspaceWindow &window = resolved == nullptr ? first_window : *resolved;
        auto spec_opt = window.closed_tab_stack.pop();
        if (!spec_opt.has_value()) {
            AURORA_LOG_INFO("main", "undo close tab: stack is empty");
            return;
        }
        const borealis::ui::ClosedTabSpec &spec = spec_opt.value();

        // 重建会话（目前只支持本地终端，SSH 腿到货后追加分支）
        borealis::conn::LocalTerminalSpec conn_spec;
        conn_spec.command_line = spec.local_shell.empty() ? settings.connection.local_shell : spec.local_shell;
        conn_spec.working_directory = spec.startup_directory.empty() ? settings.connection.startup_directory : spec.startup_directory;
        const std::shared_ptr<borealis::session::Session> session = make_local_session(conn_spec);
        // 会话起用、视口与两条闸门都在 `build_pane_view` 一处：撤销重开曾是「八处构造点」里最后一条
        // 自己写一遍的旁路（漏接闸门就是一格看着正常、实际永远 deny 的标签），同源之后结构上不可能漏。
        // 视口的浮层宿主与唤醒腿按**落点窗**取（稿 §6 批 2 的 `wire_view` 形参），故 B 窗重开的那一格
        // 弹浮层不会盖到 A 窗脸上。
        const auto view = build_pane_view(window, session, borealis::term::LineEnding::Lf);

        // 建工作区分屏容器（单 pane 起步）
        // 生成新标签 ID：走共享的那支只增计数器，不复用已发过的身份。声明提前到 hooks 之前，
        // 广播接缝与切分工厂都要拿它当身份的一半；原先它紧挨 `insert_at`，只是碰巧没有更早的读者。
        const borealis::ui::TabId id = next_tab_id++;

        borealis::ui::WorkspaceView::Hooks ws_hooks;
        // 接缝装的是与新开标签同一份定义（F3 的空 `teardown_pane` 与 F4 的「恒打首会话」就此消失）。
        // 切分工厂走**缺省本地腿**那一支：新格按被切那一格现读的 OSC 7 继承目录，而不是照闭包栈里
        // 那份旧规格复制——闭包栈存的是「这张标签当初怎么开的」，不是「这一格现在在哪」。
        wire_workspace_hooks(id, ws_hooks);

        TabWorkspace tab;
        tab.owner = &window;
        tab.sessions_by_pane[1U] = session;
        tab.make_session = make_local_pane_factory(id);
        tab.workspace = std::make_shared<borealis::ui::WorkspaceView>(view, std::move(ws_hooks));

        // 标签名按 UTF-8 解回码点：闭包栈存的是 `encode_utf8` 的产物，逐字节折成码点会把
        // CJK 名拆成 Latin-1 乱码，重开回来的标签就成了一串问号。
        borealis::term::Utf8Decoder decoder;
        DecodeCollector collector;
        const auto *bytes = reinterpret_cast<const std::byte *>(spec.name.data());
        decoder.feed(std::span<const std::byte>(bytes, spec.name.size()), collector);
        decoder.finish(collector);
        const bool inserted = window.tab_strip.insert_at(spec.index_in_strip, id, collector.out);
        if (!inserted) {
            AURORA_LOG_WARN("main", "undo close tab: insert_at failed at index ", spec.index_in_strip);
            return;  // `tab` 随之消散：成员次序保证视口先散、会话后散（稿 §5）
        }

        tab_workspaces[id] = std::move(tab);
        // 闭包栈只存本地终端的规格，重开回来仍是本地标签（`SPEC.FEAT.INTEG.02` 的类型表同源登记）。
        tab_kind[id] = TabKind::Local;
        // 新格的成员表从空开始（闭包栈只存标签规格，不存广播资格）；端点仍按「三处构造点同源」登记。
        attach_broadcast_sink(id);
        window.current_tab_id = id;

        // 选中恢复的标签并重建根节点子节点
        window.tab_strip.select(id);
        rebuild_root_children(window, tab_workspaces);
        
        AURORA_LOG_INFO("main", "restored closed tab: '", spec.name, "' at index ", spec.index_in_strip);
    };
    undo_close_tab.default_binding = au::KeyCombo{au::ModifierKey::Control | au::ModifierKey::Shift, au::KeyCode::T};
    undo_close_tab.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(undo_close_tab));

    // dead-session 一键重启（`SPEC.FEAT.WS.05`）：**同 TabId 就地换会话**而不是新加一格。
    // 与撤销重开共享建会话/建视口/建工作区那三步（`Session` 不可拷贝不可移动，`TerminalView`
    // 的会话指针在构造那一刻定死），差别只在两处——闭包栈不入不取，`tab_strip` 不动顺序也不动名。
    // 于是「同一格标签、同一份回看内容被同一格重新起用」这条语义是结构性成立的，而不是靠用户
    // 自己去辨别两格同名标签。
    // 落点窗由调用方给（`SPEC.FEAT.WS.03` 稿 D7）：浮层按钮那一路知道自己所在视口的窗（`wire_view`
    // 捕的形参），命令面板与 `Ctrl+Shift+R` 那一路问 tracker。两路只差这一枚来源，动作本体同源。
    restart_current_tab = [&](WorkspaceWindow &target) -> void {
        // 「哪一扇窗的当前标签」按落点窗现取（批 3-C）：主窗那枚别名在此不再被读，B 窗里重启
        // 只会换掉 B 窗当前标签的会话。
        if (!target.current_tab_id.has_value()) {
            AURORA_LOG_INFO("main", "restart: no current tab");
            return;
        }
        const borealis::ui::TabId id = *target.current_tab_id;
        // 串口分流（批 3）：这一格在 `serial_by_tab` 里有身份就是串口标签，「重启」＝按档案重开
        // 设备并重新置参（裁决 7.110 D7①：不改口称「续传」）。少了这一支，串口标签按重启会被
        // 静默换成本地 shell——判据与分流形态都照 SSH 那条（裁决 7.99 D6）。
        if (const auto serial = serial_by_tab.find(id); serial != serial_by_tab.end()) {
            auto bundle = make_serial_session(serial->second.profile);
            replace_pane_session(id, bundle.session, bundle.line_ending);
            serial->second.connection = bundle.connection;  // 旧腿随旧 Session 析构，这里只改写指针
            AURORA_LOG_INFO("main", "restart: tab ", id, " serial session re-opened by profile");
            return;
        }
        // Telnet 分流（`SPEC.FEAT.CONN.06` 批 3）：判据与形态都照上面那支——在 `telnet_by_tab`
        // 里有身份就是 Telnet 标签，重启＝按档案重拨。这一支不必改写表里任何东西：那份绑定只有
        // 档案、没有连接指针，而档案在重启前后是同一份（串口那支改的是裸指针，此处没有可改的）。
        if (const auto telnet = telnet_by_tab.find(id); telnet != telnet_by_tab.end()) {
            auto bundle = make_telnet_session(telnet->second);
            replace_pane_session(id, bundle.session, bundle.line_ending);
            AURORA_LOG_INFO("main", "restart: tab ", id, " telnet session re-opened by profile");
            return;
        }
        // 分流判据（裁决 7.99 D6）：这一格在 `ssh_profile_by_tab` 里有没有档案身份。有就是 SSH 标签，
        // 「重启」必须重开 SSH——此前这里固定起本地 shell，标签名却还是档案名，那是已落的生产语义错误。
        const auto owned = ssh_profile_by_tab.find(id);
        if (owned == ssh_profile_by_tab.end()) {
            // 本地腿：命令与目录一律从**当前**配置读，与「本会话最初那一格用的那份」脱钩，
            // 因为用户在面板里可能已经改过 `connection.local_shell` 或 `connection.startup_directory`。
            borealis::conn::LocalTerminalSpec spec;
            spec.command_line = settings.connection.local_shell;
            spec.working_directory = settings.connection.startup_directory;
            replace_pane_session(id, make_local_session(spec));
            AURORA_LOG_INFO("main", "restart: tab ", id, " local session replaced");
            return;
        }

        const borealis::conn::SshProfile ssh = owned->second;
        const auto ask = borealis::conn::secret_ask_for(borealis::conn::normalized_auth_method(ssh));
        std::optional<std::string> material{};
        if (const auto held = ssh_secret_by_tab.find(id); held != ssh_secret_by_tab.end()) {
            material = held->second;
        }
        if (ask == borealis::conn::SecretAsk::None || material.has_value()) {
            // agent 认证没有材料可要；句柄型/询问型只要这一格此前问出来过材料就静默重拨
            //（CONN.09 的降级语义：材料在内存里活到连接关闭，不二次弹窗）。
            restart_ssh_tab(id, ssh, std::move(material));
            return;
        }
        // 询问型且手上没材料：弹询问，明文回到 `on_secret` 的 `pending_restart` 那一支才真正重开。
        // 标题给的是主机而不是档案名——本表只留 `SshProfile`，档案展示名不在这一层的料里。
        pending_restart = std::make_pair(id, ssh);
        credential_prompt->ask(ask, ssh.host);
    };

    // `session.restart` 命令（`SPEC.FEAT.WS.05`）：与浮层按钮同源，判据是「同一份闭包、两处入口」。
    // 快捷键 `Ctrl+Shift+R`（先前拍板建议档）——不与本仓分屏保留位十五条中的任何一条同形（`workspace_key_bindings`）。
    // 命令这一路没有派发来源，落点窗问 tracker（稿 D7）；取不到窗（主窗已散）按主窗处理，与上面
    // `search.open` 同一条兜底。
    au::Command restart_session;
    restart_session.id = "session.restart";
    restart_session.title = borealis::ui::settings_label("session.restart.title");
    restart_session.category = "workspace";
    restart_session.action = [&restart_current_tab, &target_window, &first_window]() -> void {
        if (!restart_current_tab) {
            return;
        }
        WorkspaceWindow *const resolved = target_window();
        restart_current_tab(resolved == nullptr ? first_window : *resolved);
    };
    restart_session.default_binding =
        au::KeyCombo{au::ModifierKey::Control | au::ModifierKey::Shift, au::KeyCode::R};
    restart_session.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(restart_session));

    // 连接中热改波特率（`SPEC.FEAT.CONN.05` 判据 5/6，裁决 7.110 D5①/D6②）：入口只有命令面板
    // 这一条，不给默认快捷键——它是低频且不可逆感很强的动作，误按的代价比多按两次大。
    //
    // 两只框共用一只 Dialog 换 content（`close_confirm` 与多行粘贴确认的同款形态：实例的生存期
    // 要跨过 Show→答话那一段）。生效判据是平台腿的 setattr 后 getattr 回读比对：不符就报「设备侧
    // 拒绝」而**当前档不动**——`current_baud()` 只在回读相符时才前进，所以「界面回原档」这条是
    // 结构性的，不需要装配层再回滚一遍。
    auto serial_baud_dialog = std::make_shared<au::Dialog>();
    bool serial_baud_mounted = false;
    auto show_serial_baud_frame = [&](au::Node content) -> void {
        if (!serial_baud_mounted) {
            static_cast<void>(host->add_overlay(
                aurora::Node{std::static_pointer_cast<au::Widget>(serial_baud_dialog)}));
            serial_baud_mounted = true;
        }
        serial_baud_dialog->set_content(std::move(content));
        serial_baud_dialog->show();
    };
    auto serial_baud_arg = [](const std::string &text) -> std::vector<au::LocalizedString> {
        return {au::LocalizedString{text}};
    };
    auto show_serial_baud_notice = [&](std::string_view key, const std::string &arg) -> void {
        const std::string title = borealis::ui::settings_label("serial.set_baud.title");
        const std::string message =
            arg.empty() ? borealis::ui::settings_label(key)
                        : borealis::ui::settings_label(key, serial_baud_arg(arg));
        auto dialog = serial_baud_dialog;
        // alert 的 OK 钮不自带关闭：关掉它必须由调用方给回调（`au::alert` 的 on_ok 那一支）。
        show_serial_baud_frame(au::alert(title, message, [dialog]() -> void { dialog->close(); }));
    };
    auto show_serial_baud_picker = [&](borealis::conn::SerialConnection &connection) -> void {
        const std::string title = borealis::ui::settings_label(
            "serial.set_baud.current",
            serial_baud_arg(std::to_string(connection.current_baud())));
        auto dialog = serial_baud_dialog;
        auto rows = std::vector<au::Node>{};
        // 标题行即「当前 {0}」：当前档只有连接腿那一个真值源，框里别处不再重复显示（D6② 的
        // 「档案值 vs 当前值分开展示」靠这一行与设置页/档案编辑器的分工）。
        auto head = std::make_shared<au::Text>(au::TextProps{
            .content = title,
            .text_color = borealis::ui::settings_chrome().text,
        });
        head->modifier.set(au::Modifier{}.fill_max_width());
        rows.push_back(au::Node{std::static_pointer_cast<au::Widget>(std::move(head))});
        // 十四档按四列三行半排（稿 §2 C 屏的档位单选框）；当前档不做特殊标记，标题行给的就是它。
        for (std::size_t start = 0; start < borealis::conn::kSerialBaudRates.size(); start += 4U) {
            auto cells = std::vector<au::Node>{};
            for (std::size_t i = start; i < start + 4U && i < borealis::conn::kSerialBaudRates.size();
                 ++i) {
                const int rate = borealis::conn::kSerialBaudRates[i];
                auto cell = std::make_shared<au::Button>(au::ButtonProps{
                    .label = std::to_string(rate),
                    .min_width = 84.0F,
                });
                cell->set_on_click(
                    [&connection, dialog, rate, &show_serial_baud_notice]() -> void {
                        dialog->close();
                        // 腿未连（含退避期间）时 `set_baud` 恒 false，走的就是「设备侧拒绝」这一支——
                        // 与「回读不符」同一句措辞，因为对用户而言两者都是「这会儿没改成」。
                        show_serial_baud_notice(
                            connection.set_baud(rate) ? "serial.set_baud.applied"
                                                      : "serial.set_baud.rejected",
                            std::to_string(rate));
                    });
                cells.push_back(au::Node{std::static_pointer_cast<au::Widget>(std::move(cell))});
            }
            auto row = std::make_shared<au::Row>(au::RowProps{.children = std::move(cells), .gap = 6.0F});
            rows.push_back(au::Node{std::static_pointer_cast<au::Widget>(std::move(row))});
        }
        auto cancel = std::make_shared<au::Button>(au::ButtonProps{
            .label = borealis::ui::settings_label("connections.action.cancel"),
            .min_width = 84.0F,
        });
        cancel->set_on_click([dialog]() -> void { dialog->close(); });
        rows.push_back(au::Node{std::static_pointer_cast<au::Widget>(std::move(cancel))});
        auto content = std::make_shared<au::Column>(au::ColumnProps{
            .children = std::move(rows),
            .gap = 6.0F,
        });
        show_serial_baud_frame(
            au::Node{std::static_pointer_cast<au::Widget>(std::move(content))});
    };

    // `serial.set_baud` 命令（判据 5）：分流判据是「当前标签在 `serial_by_tab` 里有没有身份」，
    // 与重启那条同源。非串口会话走 `not_serial` 那一支且**不动任何状态**（稿 §2 标号 5 的
    // 后半句），故这里不给默认快捷键（D5①）。
    au::Command set_serial_baud;
    set_serial_baud.id = "serial.set_baud";
    set_serial_baud.title = borealis::ui::settings_label("serial.set_baud.title");
    set_serial_baud.category = "terminal";
    // 落点窗按 tracker 解析（稿 D7）：分流判据读的仍是「落点窗的当前标签」，只是那一扇由最近被碰过的
    // 窗决定。注意**浮层落点**此刻仍挂主窗宿主（`show_serial_baud_frame` 捕的是 app 级 `host`），改它
    // 属稿 D6「面板搬窗」＝批 4 那一棒，见 §16 的在册欠项。
    set_serial_baud.action =
        [&serial_by_tab, &target_window, &current_tab_id, &show_serial_baud_picker,
         &show_serial_baud_notice]() -> void {
        WorkspaceWindow *const resolved = target_window();
        const std::optional<borealis::ui::TabId> target =
            resolved == nullptr ? current_tab_id : resolved->current_tab_id;
        if (!target.has_value()) {
            show_serial_baud_notice("serial.set_baud.not_serial", {});
            return;
        }
        const auto bound = serial_by_tab.find(target.value());
        if (bound == serial_by_tab.end() || bound->second.connection == nullptr) {
            show_serial_baud_notice("serial.set_baud.not_serial", {});
            return;
        }
        show_serial_baud_picker(*bound->second.connection);
    };
    app.commands().add(std::move(set_serial_baud));

    // ---- 会话日志（`SPEC.FEAT.CONN.11`，裁决 7.116 的批 2：界面路径自此可达）----
    //
    // 一份闭包两处入口：右键菜单第三条（经 `wire_view` 装进视口那两条钩子）与命令面板那一条命令，
    // 两者都调 `logging_toggle_action`。开启动作的五步顺序是裁决本身定的，不是随手排的：
    //   读能力闸 → 现读模式并**先**编译（非法即回报，不建文件、不改态：D10 的 fail-closed）
    //   → `confirm` 风险提示（正文带完整路径：D7①）→ 建目录 + 算文件名（取消提示时这两步都不发生，
    //   稿 §6 那条「不建零字节文件」的判据）→ `Session::enable_logging`。
    // 一次 `open` 与一次 `mkdir` 落在 UI 线程，是裁决 7.116⑤ 明确接受的代价（量级是一次系统调用，
    // 而每分片的重复工都在该会话自己的读线程上，见 `session_log.h` 的头注）。
    auto log_dialog = std::make_shared<au::Dialog>();
    bool log_dialog_mounted = false;
    auto log_arg = [](const std::string &text) -> std::vector<au::LocalizedString> {
        return {au::LocalizedString{text}};
    };
    auto show_log_frame = [&](au::Node content) -> void {
        if (!log_dialog_mounted) {
            static_cast<void>(host->add_overlay(
                aurora::Node{std::static_pointer_cast<au::Widget>(log_dialog)}));
            log_dialog_mounted = true;
        }
        log_dialog->set_content(std::move(content));
        log_dialog->show();
    };
    auto show_log_notice = [&](std::string_view key, const std::string &arg) -> void {
        auto dialog = log_dialog;
        const std::string message =
            arg.empty() ? borealis::ui::settings_label(key) : borealis::ui::settings_label(key, log_arg(arg));
        show_log_frame(au::alert(borealis::ui::settings_label("session.log.confirm.title"), message,
                                 [dialog]() -> void { dialog->close(); }));
    };
    // 当前标签**活动格**的那一份会话（`SPEC.FEAT.WS.02` 稿 §6 第一行）：会话日志的两条钩子与
    // `session.restart` 吃的是同一个量。一格一会话之前这里是 `sessions.front()`，两格指向同一份会话时
    // 「front」恰好还是用户看着那一格；现在必须按焦点格取，否则在副格开日志会写到主格那条连接上。
    auto current_tab_session = [&current_tab_id, &focused_pane_session]()
        -> std::shared_ptr<borealis::session::Session> {
        if (!current_tab_id.has_value()) {
            return {};
        }
        return focused_pane_session(current_tab_id.value());
    };
    // 目录不存在时补建并把权限钉 0700：口径照 `key_store.cpp` 那一条——**只有本次新建且为空**的那一个
    // 目录才钉，用户已有的目录（哪怕 0777）不在本动作的职权里。
    auto ensure_log_directory = [](const std::filesystem::path &directory) -> std::optional<std::string> {
        auto ec = std::error_code{};
        const bool freshly_made = std::filesystem::create_directories(directory, ec);
        if (ec || !std::filesystem::is_directory(directory)) {
            return ec ? ec.message() : std::string{"not a directory"};
        }
        if (freshly_made) {
            auto empty_ec = std::error_code{};
            if (std::filesystem::is_empty(directory, empty_ec) && !empty_ec) {
                auto perm_ec = std::error_code{};
                std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                             std::filesystem::perm_options::replace, perm_ec);
                if (perm_ec) {
                    AURORA_LOG_WARN("main", "session log: cannot tighten '", directory.string(), "': ",
                                    perm_ec.message());
                }
            }
        }
        return std::nullopt;
    };
    // 命令标题的两态：注册表没有「只改标题」的入口（Aurora 侧事实），故整条按同 id 覆盖重注册——
    // `add` 同 id 保持注册序，命令面板里的位置不会因翻面而跳。唯一的调用点是下面 `set_on_frame` 里的
    // 一次比较（每帧读一个原子旗标、只在真的翻面时才重注册），于是「写失败自我禁用」那种没有点击
    // 者的翻面也会跟上，装配层不必在每个动作后面补一次刷新。
    auto add_logging_command = [&store, &logging_toggle_action, &app](bool logging) -> void {
        au::Command command;
        command.id = "session.toggle_logging";
        command.title =
            borealis::ui::settings_label(logging ? "session.log.command.stop" : "session.log.command.start");
        command.category = "terminal";
        command.action = [&logging_toggle_action]() -> void {
            if (logging_toggle_action) {
                logging_toggle_action();
            }
        };
        // 能力闸现读（判据 7 的「即时生效」档）：关着就灰显，开了不必重开程序。
        command.enabled = [&store]() -> bool { return store.settings().connection.session_logging; };
        app.commands().add(std::move(command));
    };
    add_logging_command(false);
    // 上一次注册命令标题时用的那一态：逐帧只比一次布尔，翻面才整条覆盖重注册（`set_on_frame` 里那一处）。
    bool logging_command_shows = false;

    logging_state_probe = [&current_tab_session]() -> std::optional<bool> {
        const std::shared_ptr session = current_tab_session();
        if (session == nullptr) {
            return std::nullopt;  // 这一格没有会话 ⇒ 条目不出现（与闸关同档，F17）
        }
        return session->logging_enabled();
    };
    logging_toggle_action = [&store, &current_tab_id, &current_tab_session, &log_dialog, &show_log_frame,
                             &show_log_notice, &ensure_log_directory, &log_arg]() -> void {
        const std::shared_ptr session = current_tab_session();
        if (session == nullptr) {
            return;
        }
        if (session->logging_enabled()) {
            session->disable_logging();
            return;  // 标题两态由下面 `set_on_frame` 里的那一次比较跟上，装配层只有一个刷新点
        }
        if (!store.settings().connection.session_logging) {
            // 第二道闸：菜单条目在闸关时根本不会出现，而命令、快捷键或后面的入口都可能绕过界面判定，
            // 落盘这条腿只认配置——默认关闭（裁决 7.9）不能有第三条路。
            AURORA_LOG_INFO("main", "session log refused: capability gate is off");
            return;
        }
        const borealis::config::Settings &now = store.settings();
        const auto redactor = borealis::session::compile_redactor(now.connection.session_log_pattern);
        if (!redactor.has_value()) {
            // fail-closed：不回退成内置词表静默续记——「用户显式配了而失效」必须可见（D10 连带的档位）。
            show_log_notice("session.log.alert.bad_pattern", now.connection.session_log_pattern);
            return;
        }
        const std::string directory = borealis::session::resolve_log_directory(
            now.connection.session_log_dir, store.report().file.parent_path().string());
        if (directory.empty()) {
            show_log_notice("session.log.alert.cannot_open", std::string{});
            return;
        }
        const borealis::ui::TabId tab_id = current_tab_id.value();
        const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        const std::string path = (std::filesystem::path{directory} /
                                  borealis::session::make_log_file_name(tab_id, epoch))
                                     .string();
        // `au::confirm` 只吃一条 message，故判据 3 那两句（敏感提醒 + 完整路径）合成一行。
        const std::string body = std::string{borealis::ui::settings_label("session.log.confirm.sensitive")} +
                                 " " + borealis::ui::settings_label("session.log.confirm.path", log_arg(path));
        // 答话可能在标签被关掉之后才回来，故这里存 `weak_ptr`：锁不到就什么都不做。
        const std::weak_ptr<borealis::session::Session> weak_session{session};
        auto dialog = log_dialog;
        show_log_frame(au::confirm(
            borealis::ui::settings_label("session.log.confirm.title"), body,
            [weak_session, directory, path, redactor, dialog, &show_log_notice,
             &ensure_log_directory](bool accepted) -> void {
                dialog->close();
                if (!accepted) {
                    return;  // 取消 ⇒ 目录与文件都不出现（稿 §6 那条判据）
                }
                const std::shared_ptr target = weak_session.lock();
                if (target == nullptr) {
                    return;
                }
                // TODO(SPEC.FEAT.CONN.11): 目录与文件名在确认前算定，答话那一刻不再重读配置——
                //   「对话框开着时改了目录」这一窗口的口径未裁，本棒不自行择一。
                if (const auto failure = ensure_log_directory(std::filesystem::path{directory});
                    failure.has_value()) {
                    show_log_notice("session.log.alert.cannot_open", *failure);
                    return;
                }
                borealis::session::LogSpec spec;
                spec.path = path;
                spec.timestamp = true;  ///< D8①：会话级参数，形态固定「每行 + 本地秒」，不进 schema。
                spec.redactor = redactor;
                if (target->enable_logging(std::move(spec)) == borealis::session::LogOutcome::CannotOpen) {
                    show_log_notice("session.log.alert.cannot_open", path);
                }
                // 标题的翻面不在这里补一次：本棒只有一个刷新点（下面 `set_on_frame` 里那一次比较）。
            }));
    };

    // 调试面板（`SPEC.NF.RELI.01`）：F12 开合，打开那一刻对**全部标签的每个会话**取一次计数器快照
    // （解析降级 / 非法字节 / 发送侧不可表示 / 背压水位四族）。快照而非活读：面板只在打开时被填一次，之后不再触碰会话，
    // 于是 `Session` 的四个 stats 取数点只在主线程这一次调用上发生锁竞争。
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
            // 标题里的格号就是真实 `PaneId`（稿 §6 的诊断行）：旧形态取的是 vector 下标 +1，
            // 一格一会话之后两者只在「没关过格」的标签上恰好相等，而面板是排查现场的地方，
            // 报错号与广播横幅上的「窗格 N」对不上就失去意义。
            for (const auto &[pane, session] : workspace.sessions_by_pane) {
                const auto parse = session->parse_stats();
                const auto decode = session->decode_stats();
                const auto queue = session->queue_stats();
                snapshots.push_back(borealis::ui::DebugSessionSnapshot{
                    .title = borealis::ui::settings_label(
                        "diagnostics.session",
                        {au::LocalizedString{std::to_string(tab_id)},
                         au::LocalizedString{std::to_string(pane)}}),
                    .parse_ignored = parse.ignored,
                    .parse_cancelled = parse.cancelled,
                    .decode_replaced = decode.replaced,
                    .decode_code_points = decode.code_points,
                    .encode_unrepresentable = session->unrepresentable_count(),
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

    // TODO(SPEC.FEAT.PREF.04): 装配层启动重放 `shortcuts.overrides` 的一腿待 G40 回货
    //   （`CommandRegistry` 无「改绑定而不动元数据」入口、`KeyCombo` 无 `from_string`；
    //   本仓若自造文本 → 键位的反向解析就是第二真值源，违 AGENTS.md §5 第 2 条与裁决 7.72。
    //   回货判据与接货形态见 `codespec/FRAMEWORK_TASK_PREF04.md`；代价登记于裁决 7.84。）
    app.commands().bind_shortcuts(app.shortcuts());

    // ---- 启动请求落地（`SPEC.FEAT.INTEG.04`，稿 §5）------------------------------------------
    //
    // 冷启动与转交共用这一条腿，差别只在失败的回告方式：冷启动记一条 warn 继续跑（稿 §5.4 末条，
    // 首标签已经在了，不该为一次切分失败关掉整个应用），转交把诊断原样回给发起进程。档案在这里
    // **再解析一次**——转发那一侧的校验发生在另一个进程，中间档案表可能已改，而本函数读的才是
    // 马上要被连接的那一份。返回值空串＝成功。
    auto startup_cwd_problem = [](const borealis::cli::StartupRequest &pending) -> std::string {
        if (pending.cwd.empty()) {
            return {};
        }
        std::error_code ec;
        const std::filesystem::path directory{
            std::u8string(reinterpret_cast<const char8_t *>(pending.cwd.data()), pending.cwd.size())};
        if (!std::filesystem::is_directory(directory, ec) || ec) {
            return "the --cwd directory is not a directory: " + pending.cwd;
        }
        return {};
    };
    auto apply_startup_request = [&](const borealis::cli::StartupRequest &pending, bool forwarded)
        -> std::string {
        if (forwarded && pending.new_window) {
            // 稿 §5.3：单实例进程模型下不能靠再起一个进程冒充新窗口，多窗口本体（WS.03）还没落地；
            // 冷启动那一侧这个标志本就无额外语义，故只拒绝「转交过来的新窗口」。
            return "--new-window needs SPEC.FEAT.WS.03, which is not implemented yet";
        }
        const borealis::conn::Profile *profile = nullptr;
        std::optional<borealis::conn::ConnectionType> profile_type{};
        if (!pending.profile.empty()) {
            const borealis::cli::ProfileResolution hit =
                borealis::cli::resolve_profile(settings.profiles, pending.profile);
            if (hit.profile == nullptr) {
                return hit.error;
            }
            profile = hit.profile;
            profile_type = profile->type;
        }
        if (const std::string problem = borealis::cli::startup_policy_error(pending, profile_type);
            !problem.empty()) {
            return problem;
        }
        if (const std::string problem = startup_cwd_problem(pending); !problem.empty()) {
            return problem;
        }
        const std::optional<std::string> cwd =
            pending.cwd.empty() ? std::nullopt : std::optional<std::string>{pending.cwd};
        if (pending.action == borealis::cli::StartupAction::SplitPane) {
            borealis::ui::WorkspaceView *workspace = nullptr;
            if (current_tab_id.has_value()) {
                const auto found = tab_workspaces.find(current_tab_id.value());
                if (found != tab_workspaces.end()) {
                    workspace = found->second.workspace.get();
                }
            }
            if (workspace == nullptr) {
                return "there is no tab to split";
            }
            // `--cwd` 与 Local `--profile` 在切分腿上折成一份一次性本地料（稿 §7）。当前标签必须是本地腿：
            // SSH / 串口 / Telnet 的工厂各自重拨或恒拒，把本地料递进去要么被悄悄吞掉、要么让新格变成
            // 与这张标签无关的第三种会话，两种都比当场拒绝糟（AGENTS §4.1 第 3 条：不做半截实现）。
            if (cwd.has_value() || profile != nullptr) {
                const auto kind = tab_kind.find(current_tab_id.value());
                if (kind == tab_kind.end() || kind->second != TabKind::Local) {
                    return "split-pane --cwd and --profile need the current tab to be local";
                }
                borealis::conn::LocalTerminalSpec pinned;
                if (profile != nullptr) {
                    // 档案字段当场**取值**进这份料：`profile` 指向 `settings.profiles`，而那张表会在
                    // 设置页保存时被整表换掉。空字段留给工厂的本腿语义（见 `consume_split_spec`）。
                    pinned.command_line = profile->local.command_line;
                    pinned.working_directory = profile->local.working_directory;
                    pinned.environment = profile->local.environment;
                }
                if (cwd.has_value()) {
                    pinned.working_directory = *cwd;  // CLI 压过档案（`SPEC.FEAT.INTEG.02` 那条次序）
                }
                split_spec_override = pinned;
            }
            // 与 `Ctrl+Shift+D` 同一条路径、同一根轴（稿 §5.2）：目标取模型焦点那一格。
            const std::size_t before = workspace->pane_count();
            workspace->split_focused(borealis::ui::PaneAxis::Horizontal);
            // 工厂没取走（几何不容、树没动）就当场作废：一次命令行意图不该留给下一次 `Ctrl+Shift+D`
            // 的切分，否则用户手动分出来的那一格会莫名其妙用上一条命令的目录起步。
            split_spec_override.reset();
            if (workspace->pane_count() == before) {
                // 工厂缺席或几何不容：树没动，回诊断而不是假装成功（判据「切分失败回 ASCII 诊断」）。
                return "the focused pane cannot be split";
            }
            return {};
        }
        if (profile != nullptr) {
            borealis::conn::Profile target = *profile;
            if (target.type == borealis::conn::ConnectionType::Local && cwd.has_value()) {
                target.local.working_directory = *cwd;  // 覆盖只进本次副本，档案本体不动（稿 §5.1）。
            }
            connect_profile(target);  // 四类分腿照旧，SSH 要材料时仍走既有凭据询问链。
        } else {
            create_new_tab(first_window, cwd);
        }
        rebuild_root_children(first_window, tab_workspaces);
        return {};
    };

    // 启动降级提示（`SPEC.FEAT.PREF.07`，裁决 7.76⑤）：`LoadOutcome` 四态里只有两条降级态会弹，弹一次即止。
    // `message` 是 ASCII 英文诊断，只进日志不上中文界面（判据文 S13①），故以「本次真的弹了」为条件——
    // 「哪两态弹」的判定只在 `StartupNotice` 里存一份，装配层不另做一遍 `outcome` 分支。
    borealis::ui::StartupNotice notice{*host, app.focus()};
    if (notice.show_if_needed(store.report())) {
        AURORA_LOG_WARN("main", "config load degraded: ", store.report().message);
    }

    app.set_on_frame([&tab_workspaces, &workspace_windows, &outbox, &panel, &app, &settings,
                      &has_live_session, &pane_session, &sftp_panel, &tunnel_panel,
                      &keys_panel, &logging_state_probe, &add_logging_command, &logging_command_shows,
                      &show_log_notice, &startup_mutex, &startup_queue, &apply_startup_request,
                      &frame_bytes, &reply_wire]() -> void {
        // 转交请求在帧边界排空（稿 §3.3）：transport 线程只负责入队与唤醒，档案解析、开标签、切分
        // 全部发生在主线程这一侧。逐条回执要在执行**之后**才写，于是转发端拿到的「成功」意味着
        // 那一格已经建好（或已确定建不出来）。
        for (;;) {
            std::shared_ptr<borealis::platform::InstanceRequest> pending;
            {
                const std::lock_guard lock{startup_mutex};
                if (startup_queue.empty()) {
                    break;
                }
                pending = std::move(startup_queue.front());
                startup_queue.pop_front();
            }
            const std::optional<borealis::cli::StartupRequest> parsed =
                borealis::cli::decode_startup_request(frame_bytes(pending->frame()));
            if (!parsed.has_value()) {
                // 帧结构不符即整帧拒绝，不猜半个请求（稿 §6 的 fail-closed）。
                pending->complete(reply_wire(false, "malformed startup request"));
                continue;
            }
            const std::string problem = apply_startup_request(*parsed, true);
            pending->complete(reply_wire(problem.empty(), problem));
        }

        // 排帧范围是**全部标签**每帧都排（含隐藏标签，裁决 7.47⑩）：可见性只影响绘制不影响数据。
        // 只排当前标签的后果是三处静默失灵——别的标签的 `OSC 52` 永远留在队列里（取走语义按会话
        // 记账，裁决 7.21③）、BEL 与活动角标要等用户切过去才补亮、OSC 标题不落进标签名。
        for (auto &[tab_id, workspace] : tab_workspaces) {
            // 角标与标签名写给**属主那扇窗**的标签条（稿 §2）：号是进程级一支、全局不撞，但一张标签
            // 只属于一扇窗，点亮到别的窗那一栏就是凭空多出一格没开过的标签。
            WorkspaceWindow &owner = *workspace.owner;
            for (borealis::ui::PaneId pane = 1; pane <= workspace.workspace->pane_count(); ++pane) {
                auto *v = workspace.workspace->view_of(pane);
                if (v) {
                    v->on_frame();
                }
            }
            for (const auto &[pane, tag_session] : workspace.sessions_by_pane) {
                static_cast<void>(pane);  // 下面四件都是「任一格」语义，无须分辨是哪一格
                static_cast<void>(outbox.drain(*tag_session));

                // 会话日志的写失败提示（`SPEC.FEAT.CONN.11`）：会话侧只上弦一次，取走与弹窗都在帧边界，
                // 形态照上面 `outbox.drain` 与下面 `take_bell_triggered` 那两条（AGENTS §4.5 第 25 条）。
                if (const auto note = tag_session->take_logging_notice(); note.has_value()) {
                    // 提示语里的路径是 ASCII 诊断，上屏那句走词条，故日志留原文、界面只报「已停止」。
                    AURORA_LOG_WARN("main", "session log disabled after a write failure: ", *note);
                    show_log_notice("session.log.alert.write_failed", {});
                }

                // BEL 事件传给 TabStrip（`SPEC.FEAT.WS.04`）：取走即清零，不会被下一帧重复点亮
                if (tag_session->take_bell_triggered()) {
                    owner.tab_strip.mark_bell_triggered(tab_id);
                }

                // 有新的网格更新（damage）即标记活动（`SPEC.FEAT.WS.04`）
                if (tag_session->has_damage()) {
                    owner.tab_strip.mark_activity(tab_id);
                }
            }
            // 消费 OSC 标题并更新到标签名（`SPEC.FEAT.TERM.07` UI 消费链路）：**焦点格独占这一槽位**
            // （稿 §6 与 §9 第 2 条的推荐档）。一格一会话之后若仍逐格循环，写进标签名的是 `map` 里号
            // 最大的那一格，而不是用户看着的那一格；且切焦点时旧格的名字会残留在标签上。这里刻意**可写
            // 空**：焦点格没设标题就让位给下一优先级来源（手动名 / 档案名），比留着别格的名字诚实。
            // 逐帧同值由 `TabStrip::set_osc_title` 自己去重，不必在这里比字符串。焦点格当下没有会话（重启
            // 那一跳的进行中）也写空：留着旧会话的名字会让用户以为还连着那一格。
            const std::shared_ptr<borealis::session::Session> focused =
                pane_session(tab_id, workspace.workspace->focused_pane());
            static_cast<void>(owner.tab_strip.set_osc_title(
                tab_id, focused == nullptr ? std::u32string{} : focused->osc_state().title));
            // 退出角标（`SPEC.FEAT.WS.04`）：每帧现算现推，`set_exited` 自己在值没变时不通知，
            // 会话重启（`SPEC.FEAT.WS.05`）之后不必另找清除入口——下一帧推进来的就是 false。
            owner.tab_strip.set_exited(tab_id, !has_live_session(tab_id));
        }
        
        // 窗口标题＝**该窗选中标签的显示名**（`SPEC.FEAT.WS.06`）：名只经 `resolve_tab_name` 这一个判定处
        //（裁决 7.43③），窗口侧再读一次 OSC 就是第二套优先级，标签条与标题栏会在手动重命名那一刻分叉。
        // 逐窗各写各的（稿 §6 批 2）：一扇窗的标题取的是它自己那一栏的选中格，`last_title` 也是逐窗账，
        // 回全局一份会让第二扇窗的标题栏永远停在主窗的名字上。
        for (const auto &window_ptr : workspace_windows) {
            WorkspaceWindow &window = *window_ptr;
            if (!window.current_tab_id.has_value()) {
                continue;
            }
            std::u32string display_name;
            for (const auto &tab : window.tab_strip.tabs()) {
                if (tab.id == window.current_tab_id.value()) {
                    display_name = borealis::ui::resolve_tab_name(
                        tab.names, settings.appearance.tab_name_priority);
                    break;
                }
            }
            std::string title_utf8;
            static_cast<void>(borealis::term::encode_utf8(display_name, title_utf8));
            if (title_utf8 != window.last_title) {
                window.last_title = title_utf8;
                if (au::WindowHost *const target = app.window_host(window.id); target != nullptr) {
                    if (auto *win = target->window()) {
                        win->set_title(title_utf8);
                    }
                }
            }
        }
        
        // 日志命令的两态标题（`SPEC.FEAT.CONN.11`，裁决 7.116 D6①）：注册表没有改标题的入口，故这里
        // 只在**翻面**时按同 id 整条覆盖重注册（`add` 保序，面板里的位置不动）。逐帧读一次原子旗标
        // 与一次串比较，不去碰文件也不碰会话锁——这是本棒唯一一处标题刷新点，切换标签、重启会话、
        // 写失败自我禁用三种翻面都从这一条腿跟上。
        const std::optional<bool> logging_now =
            logging_state_probe ? logging_state_probe() : std::optional<bool>{};
        const bool shows = logging_now.value_or(false);
        if (shows != logging_command_shows) {
            logging_command_shows = shows;
            add_logging_command(shows);
        }

        panel.pump_preview();  // 预览盒是第二个会话，脏行同样按帧排（面板关着即空操作）
        sftp_panel.tick();     // SFTP 面板取件与判活（D8①/D9①；面板关着即空操作）
        tunnel_panel.tick();   // 隧道行表随快照刷新（D6①；面板关着即空操作）
        keys_panel.tick();     // 密钥行表随快照刷新（D6① 同族；面板关着即空操作）
    });

    // 冷启动请求在接线齐备之后、进循环之前执行一次（稿 §5.4）：首帧直接画出目标标签或那一刀切分，
    // 不出现「先空一帧再补一格」。唤醒指针同样在这一刻交出去——此前入队的转交请求随首帧排空。
    startup_wake.store(&surface);
    if (startup_request.has_value() && !cold_startup_plain_tab_done) {
        const std::string problem = apply_startup_request(*startup_request, false);
        if (!problem.empty()) {
            // 冷启动的运行期失败不关掉应用：首标签已经在屏上，为一次切分失败退进程比留着它更糟
            //（稿 §5.4 末条）。转交那一侧同一条诊断是回给发起进程的退出码 2。
            AURORA_LOG_WARN("main", "startup request not applied: ", problem);
        }
    }
    app.run();

    // 单实例通道在 surface 消散之前停：先撤唤醒指针（此后入队的请求不再碰这枚引用，只等下一次帧
    // 边界或超时收尾），再关监听并 join transport 线程——反过来就成了「线程正在唤醒一只已析构的
    // surface」，而这条竞态只在关掉最后一格的那几毫秒里存在，真机极难复现。
    startup_wake.store(nullptr);
    instance_listener.reset();

    // 关停在读线程仍可能唤醒 surface 之前：需要关闭所有标签的所有会话（稿 §5 的「退出」一行：
    // 一格一会话之后这里遍历的是每格，而不是每标签一份）。
    for (auto &[tab_id, workspace] : tab_workspaces) {
        static_cast<void>(tab_id);
        for (auto &[pane, session] : workspace.sessions_by_pane) {
            static_cast<void>(pane);
            session->close();
        }
    }
    return 0;
}
