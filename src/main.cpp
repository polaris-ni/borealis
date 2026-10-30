#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
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
#include "aurora/core/json.h"
#include "aurora/core/log.h"
#include "aurora/render/font_discovery.h"
#include "aurora/window/native_surfaces.h"
// 构建期生成的词表头（`cmake` 见 `src/CMakeLists.txt` 的 `aurora_i18n_generate`）：装配层只有这里
// 需要**编译期可见的词条 Id**——命令面板那两处上屏文案按 Id 交框架查目录，其余一律按 key 走
// `ui::settings_label()`。文件名随基准词表 `assets/i18n/messages_zh.json` 的 stem。
#include "messages_zh.hpp"
#include "borealis/conn/local_terminal.h"
#include "borealis/conn/profile.h"
#include "borealis/cli/handoff.h"
#include "borealis/cli/startup.h"
#include "borealis/config/form_transfer.h"
#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/config/themes.h"
#include "borealis/diag/crash_report.h"
#include "borealis/session/bell.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/session/notification_outbox.h"
#include "borealis/session/session.h"
#include "borealis/session/session_log.h"
#include "borealis/term/clipboard_read.h"
#include "borealis/term/keymap.h"
#include "borealis/term/utf8.h"
#include "borealis/term/width.h"
#include "borealis/ui/broadcast.h"
#include "borealis/ui/font_choice.h"
#include "borealis/ui/monitor_binding.h"
#include "borealis/ui/osc7.h"
#include "borealis/ui/quake.h"
#include "borealis/ui/shortcuts_table.h"
#include "borealis/ui/settings_form.h"
#include "borealis/ui/tab_strip.h"
#include "borealis/ui/closed_tab_stack.h"
#include "borealis/ui/url.h"
#include "borealis/ui/window_close.h"
#include "borealis/ui/workspace_snapshot.h"
#include "conn/key_model.h"
#include "conn/key_push.h"
#include "conn/key_store.h"
#include "conn/profile_views.h"
#include "conn/proxy_jump.h"  // render_proxy_jump_list：逐跳回存条目的 label 取唯一串形
#include "conn/serial_connection.h"
#include "conn/serial_model.h"
#include "conn/ssh_connection.h"
#include "conn/ssh_shared.h"
#include "conn/telnet_connection.h"
#include "conn/tunnel_client.h"
#include "config/schema_names.h"  // chrome_theme_tag：落盘名 → 解算函数的唯一读点（PREF.01）
#include "config/shortcut_overrides.h"
#include "config/window_geometry_store.h"
#include "session/bell_audio.h"
#include "config/workspace_store.h"
#include "platform/credential_store.h"
#include "platform/crash_handler.h"
#include "platform/file_secure.h"
#include "platform/instance_channel.h"
#include "platform/open_url.h"
#include "platform/process_args.h"
#include "platform/serial.h"
#include "platform/user_dir.h"
#include "ui/active_window_tracker.h"
#include "ui/app_title_bar.h"
#include "ui/broadcast_banner.h"
#include "ui/connection_sidebar.h"
#include "ui/connection_wizard.h"
#include "ui/credential_prompt.h"
#include "ui/debug_panel.h"
#include "ui/keys_format.h"
#include "ui/keys_panel.h"
#include "ui/serial_format.h"
#include "ui/session_monitor_panel.h"
#include "ui/settings_i18n.h"
#include "ui/settings_panel.h"
#include "ui/sftp_panel.h"
#include "ui/snippet_panel.h"
#include "ui/startup_notice.h"
#include "ui/status_bar.h"
#include "ui/status_bar_format.h"
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
/// `suggested_name` 是**预选而不是答案**（框架缺口 G48 的口径，裁决 7.169）：对话框把它落在名字栏，
/// 用户照改、清空、换目录都仍算同一次确认；空串＝不表达意图，与不传该格同形。
[[nodiscard]] auto picked_save_path(std::string_view title,
                                    std::vector<aurora::file_dialog::Filter> filters,
                                    std::string_view suggested_name) -> std::string {
    aurora::file_dialog::Options opts;
    opts.title = std::string{title};  // 平台对话框的标题由 OS 呈现，不经本仓词条表（S13② 的同一条口径）
    opts.filters = std::move(filters);
    opts.suggested_name = std::string{suggested_name};
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

/// @brief 把一段文本落到用户点名的路径；成功为空，失败为 ASCII 原因（稿 §10.2 D23①）。
///
/// 三道闸（0600 临时件 → 截断写 → `rename` 就位、失败即删）在 `platform::write_private_file`
/// 里收着，本件只补两处面板不认识的事：⑴ D19③——监视折算件交回的文本行间只有 LF 且**末尾没有**
/// 换行，落盘这侧补上那一个（剪贴板那侧不补，粘出来不该多一行空行）；⑵ 枚举分档折成一句 ASCII
/// 诊断串，只进日志，上屏的是词条那句（同 `Store::export_settings` 的口径）。
[[nodiscard]] auto write_private_dump(const std::string &path, const std::string &text)
    -> std::optional<std::string> {
    using borealis::platform::PrivateWriteOutcome;
    switch (borealis::platform::write_private_file(path, text + "\n")) {
        case PrivateWriteOutcome::Written:
            return std::nullopt;
        case PrivateWriteOutcome::InvalidPath:
            return "serial monitor export refused: the picked path is not usable: " + path;
        case PrivateWriteOutcome::NoScratchName:
            return "serial monitor export found no free scratch name next to: " + path;
        case PrivateWriteOutcome::Failed:
            return "serial monitor export could not be put in place: " + path;
    }
    return "serial monitor export failed: " + path;
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
/// Column、标签栏控件、广播横幅、中段行与常驻列、状态栏、本窗的标签真值源与闭包栈、本窗当前标签号。应用级那一份（Settings /
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
    /// @brief 屏 1 那一幅的中段行（常驻列 + 分栏工作区），根 Column 里 `expand(1)` 的那一格。
    ///
    /// 常驻列与工作区共用这一行而不是把列塞进工作区：稿那条 34 dp 图标列的纵向范围是
    /// `y=104..872`＝标签条与横幅之下、状态栏之上，正是这一行的矩形（裁决 7.180）。
    std::shared_ptr<au::Row> content_row{};
    /// @brief 屏 1 顶部那条 36 dp 自绘标题栏（`SPEC.FEAT.WS.06` 的「标题栏显示当前会话名」那一腿的载体）。
    ///
    /// **按窗一份**：一枚控件节点只容一个父节点，而根 Column 的首格就是它。帘子（`style.frameless`
    /// 的 quake 那一扇）不挂这条——它没有可拖的标题带，也没有需要显示的窗口标识（`SPEC.FEAT.WS.12`）。
    std::shared_ptr<borealis::ui::AppTitleBar> title_bar{};
    std::shared_ptr<borealis::ui::TabBarWidget> tab_bar{};
    std::shared_ptr<borealis::ui::BroadcastBanner> banner{};
    /// @brief 屏 1 判据 ③ 那条常驻列（折叠 34 dp 轨／展开 320 dp，`SPEC.FEAT.CONN.07`＋裁决 7.180）。
    ///
    /// **每窗一份**而不是 app 级单实例：一枚控件节点只容得下一个父节点，常驻列不像浮层那样可以「搬过去」
    /// ——一搬就把发起窗自己的骨架抽掉。开合档按窗各自记，落盘那份取 `appearance.sidebar_collapsed`。
    std::shared_ptr<borealis::ui::ConnectionSidebar> sidebar{};
    /// @brief 屏 1 判据 ⑨／⑩ 那条 28 dp 读数条（`SPEC.FEAT.PREF.02` 的即时反馈面），常驻根 Column 末位。
    ///
    /// 与横幅同一条形态：常驻节点而不是按需增删——摘除会让它随「有没有读数」显隐，而判据要的是
    /// 「一格没有也留着那条」。内容按窗各折各的（焦点格属于哪扇窗，这一条就读哪扇窗的那一格）。
    std::shared_ptr<borealis::ui::StatusBar> status_bar{};
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
    /// 本窗的**关窗**确认框（`SPEC.FEAT.WS.03` 稿 D4／G44）：与关标签那只分开——两件事可能同时
    /// 发生，共用一只换 `content` 会把另一句问题覆盖掉。生存期同样跨 Show→答话。
    std::shared_ptr<au::Dialog> window_close_confirm{};
    bool window_close_confirm_mounted = false;

    /// @brief 交出后端窗口的所有权：`Application` 的宿主接管它。
    [[nodiscard]] auto take_window() -> std::unique_ptr<au::Window> { return std::move(window); }
};

/// @brief 撤销重开把标签放回哪扇窗、哪个位置、哪个名字（`SPEC.FEAT.WS.10`）。
///
/// 锚点只在闭包栈那条腿上出现，且要跨过凭据的异步问答——故存 `WindowId` 而不是
/// `WorkspaceWindow *`：答话回来时那一扇可能已经关了，按号现取才判得出「窗没了」。
struct RestoreAnchor {
    au::WindowId window_id = au::AURORA_INVALID_WINDOW_ID;
    std::size_t index_in_strip = 0;
    std::u32string name;
};

/// @brief 询问型档案的挂起态：材料回来后开哪一份档案、带不带撤销重开的锚点。
struct PendingProfileConnect {
    borealis::conn::Profile profile;
    std::optional<RestoreAnchor> anchor;
};

/// @brief 造一扇工作区窗的地基：后端窗口 + 浮层宿主 + 撑满窗宽的根 Column。
/// @param title 窗口标题初值（真值随后由逐窗标题差分腿改写）。
/// @param creation_index 本窗的创建序（稿 D9 的几何键来源，主窗是 0）。
/// @param quake 按下拉式终端（`SPEC.FEAT.WS.12`，裁决 7.172）的形态造这一扇：无边框、置顶、建窗即隐藏。
/// @return 建窗失败即空指针（后端错误原文已在工厂里落日志），调用方**不得**接着登记宿主——
///         稿 §5 的「建窗」不变量：没建成窗就不许留下半个账目。
[[nodiscard]] auto open_workspace_window(std::string title, std::size_t creation_index, bool quake = false)
    -> std::unique_ptr<WorkspaceWindow> {
    auto out = std::make_unique<WorkspaceWindow>();
    out->options.size = au::Size{.width = 960.0F, .height = 640.0F};
    out->options.title = std::move(title);
    if (quake) {
        // 三档都是**建窗期一次定档**：无边框没有运行期入口，置顶一并写成初值（运行期那一口留给呼出时
        // 重申），可见性取 `Hidden` 而不是「先显示再隐藏」——后者会先闪一帧 960×640 的普通窗口，而
        // 呼出那一步才把落位与尺寸算出来（`apply_quake_intent` 的 Show 分支）。
        out->options.style.frameless = true;
        out->options.style.always_on_top = true;
        out->options.visibility = au::WindowVisibility::Hidden;
        // 空白 `persist_id`：框架的几何恢复腿按这个键回放上次的位置与尺寸，而帘子的矩形由占比与工作区
        // 每次呼出现算——两者同写一份几何，恢复腿就会把算出的落位盖掉。同一枚空键也让它不进几何保存：
        // 帘子不是用户拖出来的工作区，没有可延续的几何。
    } else {
        // **自绘标题栏档**（屏 1 顶部那条 36 dp 带，裁决 7.181）：`Borderless` 撤掉 OS 标题带但留着
        // 缩放热区与修饰键拖拽兜底（Win32 侧 `WS_OVERLAPPEDWINDOW & ~WS_CAPTION`，热区由 NC 承担、
        // 阴影由 DWM 合成），且客户区零内缩——那条带因此是**内容**而不是非客户区，色值走 chrome 表、
        // 三枚钮走 `WindowChrome`。`Frameless` 会把 `WS_SYSMENU` 一族一起去掉（quake 帘子要的就是那
        // 一档），任务栏与 Alt+F4 那条腿也跟着没了，故普通工作区窗不用它。
        out->options.style.decoration = au::DecorationPolicy::Borderless;
        // 几何键在建窗**之前**写好：框架的恢复腿读的是 `opts.persist_id`，而 `create_native_window` 吃
        // 的就是同一份 options（`SPEC.FEAT.WS.03` 稿 D9／§0 的 F26）。失败的那一扇不留账——本跳只写了
        // 一个字符串，没建成窗就没有宿主、也就没有下一次保存。
        out->options.persist_id = borealis::config::window_persist_id(creation_index);
    }
    auto created = au::create_native_window(out->options);
    if (!created) {
        AURORA_LOG_ERROR("main", "native window creation failed: ", created.error().message);
        return nullptr;
    }
    out->window = std::move(created.value());
    out->surface = &out->window->surface();
    // 右键菜单与多行粘贴确认都是浮层，故场景根是浮层宿主而非视口本身（裁决 7.41③）：宿主 [0] 是根
    // Column，浮层从 [1] 起追加；根 Column 自己的次序是 [0] 标题栏、[1] 标签栏、[2] 广播横幅、
    // [3] 中段行、[4] 状态栏（承重，见 `rebuild_root_children`）。
    out->overlay_host = std::make_shared<au::OverlayHost>();
    // 纵向容器承载：OverlayHost 只管理浮层，常规内容须走普通容器；交叉轴 Stretch 使标签栏与工作区
    // 均撑满窗宽。
    out->root_column = std::make_shared<au::Column>();
    out->root_column->set_modifier(au::Modifier{}.fill_max_size());
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
    // 崩溃留存腿（`SPEC.NF.RELI.01`，裁决 7.142）：自写诊断快照，只留结构计数与每格身份，
    // 不含终端文本、标签名、主机名与凭据。声明紧跟 `store` 故几乎最后析构——处理函数只持它的
    // 指针，而进程终止前它必须一直有效。安装点放在这里而不是建窗之后：启动途中的崩溃同样要留档。
    // 目录建不出来只落一条 WARN，不拦启动（诊断腿不得成为新的启动失败点）。
    borealis::diag::CrashFactsStore crash_facts;
    {
        const std::filesystem::path config_directory = store.report().file.parent_path();
        if (config_directory.empty()) {
            AURORA_LOG_WARN("main", "crash handler not armed: config directory unknown");
        } else {
            const std::filesystem::path crash_directory = config_directory / "crash";
            if (!borealis::platform::install_crash_handler(
                    borealis::platform::CrashHandlerConfig{crash_directory, &crash_facts})) {
                AURORA_LOG_WARN("main", "crash handler not armed: ", crash_directory.string());
            }
        }
    }
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
    // BEL 呈现（`SPEC.FEAT.WS.04`，裁决 7.159）：策略是纯逻辑件，设备是单独的可听出口。设备按需
    // 才建：缺省视觉档不启动音频设备线程，「只看角标」的用户不为一个不存在的铃声付启动开销。
    // `decide` 与 `ring` 都在帧边界主线程调用，与下方角标同一条排帧纪律。
    borealis::session::BellPolicy bell_policy;
    std::unique_ptr<borealis::session::BellAudio> bell_audio;
    // 起手即按落盘档预建：可听档用户的第一条 BEL 不该等设备起转（设备线程初始化要几十毫秒，
    // 铃声会晚于角标）。建不出来（无设备 / 框架未编音频）不拦启动——`ring` 自己静默降级并只
    // 留一条 WARN，铃声不是会话事件的失败点。
    if (settings.terminal.bell == borealis::config::BellMode::Audible) {
        bell_audio = std::make_unique<borealis::session::BellAudio>();
    }
    // 运行期从视觉档改到可听档时，首声铃在这里懒建设备；此后复用同一台。失败也不回填——下一
    // 条 BEL 会再试一次构造，而构造只做一次设备探测，代价有界（与「投不出去不再重试」的通知腿
    // 不同：这里的失败多半是「设备晚到」，再试是合理的）。
    auto ring_bell = [&]() -> void {
        if (!bell_audio) {
            bell_audio = std::make_unique<borealis::session::BellAudio>();
        }
        static_cast<void>(bell_audio->ring());
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
    // 档位在这一行读定、整进程不再改（`SPEC.FEAT.PREF.05` 稿 D2）：面板上那一行的角标「重启后生效」
    // 就是这条不变量的用户可见半边。装填把整份 bin（两档都在）读进目录，选中的那一档定成缺省档与
    // `settings_locale()`（i18n v2 改形：该稿 D4 的「只装选定那一档」被「全语言合一」取代）。
    borealis::ui::install_settings_strings(settings.appearance.language);
    // 外壳明暗档的启动解算（`SPEC.FEAT.PREF.01` 稿 §3 第 1 步）：必须排在任何控件构造之前——标签栏与
    // 分屏缝隙的 `on_paint` 每次重绘现读活动档，首帧即取到；而设置面板的控件在**构建期**取定色值，
    // 晚一步解算就是「先按暗档建好面板、用户打开才发现是亮的」。`system` 档在这里现读一次 OS 偏好
    // ——此刻还没有 `Application`，只能走自由函数；此后系统侧的变化由下面那条应用级变更通知补齐
    // （缺口 G45 已闭合，裁决 **7.170**；曾经的 1 s 轮询代偿随该条删除）。
    borealis::ui::set_chrome_scheme(borealis::ui::resolve_chrome_scheme(
        borealis::config::chrome_theme_tag(settings.appearance.chrome_theme), aurora::os_color_scheme()));

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

    // 逐窗几何的那份存储（`SPEC.FEAT.WS.03` 稿 D9／§6 批 5）：本仓只决定它落在哪、什么时候提交，
    // 读写两跳都在框架的几何链里（恢复＝宿主登记与接线那两处，保存＝帧末回收的收集步骤）。
    // **声明必须早于 `app`**：`set_window_geometry_store` 交出去的是本件内部那枚 `Preferences` 的裸
    // 指针，而框架在 `run()` 返回后的收尾里仍会读它（保留的最后宿主就是在这一刻落几何）——局部对象
    // 按声明逆序析构，晚声明就是「框架收尾时读一块已散的存储」。
    borealis::config::WindowGeometryStore geometry_store;
    // 两处提交共用一条腿（稿 D9：任一窗关闭后、退出前）。框架没有「自上次提交后改过没有」的查询，
    // 故提交是无条件的；失败只落日志——几何丢了下一次自己再记，不该为一枚窗口位置打断关窗或退出。
    auto flush_window_geometry = [&geometry_store]() -> void {
        if (const auto reason = geometry_store.flush(); reason.has_value()) {
            AURORA_LOG_WARN("main", "window geometry store flush failed: ", *reason);
        }
    };
    // 上次那套标签布局的落盘件（`SPEC.FEAT.WS.11` 稿 D1①／批 3）：配置目录里第三枚 `Preferences`，
    // 与上面那枚几何、`borealis.json` 各管各的键空间——**位置与尺寸归框架的几何链，内容归这一枚**
    // （稿 D6① 的刻意分工，两枚文件互不含对方字段）。本件只有「取存储 + 提交」两跳、没有任何删除
    // 入口，于是判据 ③ 那句「开关关掉之前已有的快照不被删除」是结构上的事实（F26 的实测同一条：框架
    // 的 `remove` 连整棵对象都删不掉）。
    // **声明必须早于 `app`**：与上面那枚同一条继承（`Preferences` 内含互斥量、交出去的是裸指针）。
    borealis::config::WorkspaceStore workspace_store;
    // 上次那套标签布局的**读侧**（`SPEC.FEAT.WS.11` 批 5-C，稿 D7①／D8①）：启动时只读一次，之后整轮
    // 重放只用这一份副本。三道闸各管一件事：开关关掉不读（判据 ③）、显式 CLI 请求不读（D7①）、文档
    // 坏了不读（D8①）——三档都只落日志，界面上不弹框（`StartupNotice` 只吃 `LoadReport`，稿 F17）。
    // 读在**建首窗之前**：首标签那三行布尔要用 `restore_at_startup` 决定是否让位（稿 F16）。
    std::optional<borealis::ui::WorkspaceSnapshot> restored_layout;
    bool restore_at_startup = false;
    if (settings.connection.restore_sessions) {
        // D7① 的「显式」只认三件：`--profile`、`--new-window`（旗标与动作两档都认，旧版转交帧只有
        // 旗标）与 `--split-pane`。`--cwd` 单飞不在此列——它只是给首标签换个目录（稿 §3 D7 的代价档）。
        const bool explicit_startup_request =
            startup_request.has_value() &&
            (!startup_request->profile.empty() || startup_request->new_window ||
             startup_request->action == borealis::cli::StartupAction::NewWindow ||
             startup_request->action == borealis::cli::StartupAction::SplitPane);
        if (explicit_startup_request) {
            AURORA_LOG_INFO("main", "session restore skipped: an explicit startup request takes precedence");
        } else {
            const au::json::Value stored = workspace_store.preferences()->get<au::json::Value>(
                std::string{borealis::config::kWorkspaceLayoutKey}, au::json::Value{});
            if (stored.is_null()) {
                AURORA_LOG_INFO("main", "session restore: no saved layout");
            } else if (const au::Result<std::string> text = au::json::dump(stored); !text.ok()) {
                // 内存里那棵树本身写不回去（框架只会在含非有限数时拒）——与「文件坏了」同一档处理：
                // 不恢复、不弹框，下一次真变更再整份覆盖。
                AURORA_LOG_WARN("main", "session restore: saved layout cannot be re-encoded: ",
                                text.error().message);
            } else {
                borealis::ui::SnapshotDecodeResult decoded =
                    borealis::ui::WorkspaceSnapshot::from_json(text.value());
                if (!decoded.snapshot.has_value()) {
                    AURORA_LOG_WARN("main", "session restore: saved layout rejected: ", decoded.reason);
                } else if (decoded.snapshot->windows.empty()) {
                    AURORA_LOG_INFO("main", "session restore: saved layout lists no window");
                } else {
                    AURORA_LOG_INFO("main", "session restore: replaying ", decoded.snapshot->windows.size(),
                                    " window(s)");
                    restored_layout = std::move(decoded.snapshot);
                    restore_at_startup = true;
                }
            }
        }
    }
    // 几何键的**创建序计数器**（稿 D9 那句「键＝创建序」）：只增不复用，于是在用的那些窗之间不可能
    // 撞键。按「当前活着几扇」取号看着更省，但拆一扇再开一扇时会给新窗发一枚**还挂在活着那扇上**的
    // 键，两扇从此共写同一格；不复用的代价只是文件里多几格旧几何，本棒不清理（清哪一格是用户的账，
    // 不是本层的）。
    std::size_t next_window_index = 0U;

    // ---- 下拉式终端（`SPEC.FEAT.WS.12`，裁决 7.172）-----------------------------------------------
    // 四道闸全部排在**建窗之前**：无边框是 `WindowStyleOptions` 的建窗初值、框架没有运行期入口，于是
    // 「这台机器上能不能开帘子」只在造窗那一刻定得下来。任一闸不过就退回普通窗口并落一条 WARN——不
    // 静默降级成「热键按了没反应」：
    //   ① 配置开关（设置页标「下次启动」：窗形与全局热键都在建窗期定，改它要重启）；
    //   ② 运行期显隐与置顶能力（`capabilities()`；帘子的呼出与收起全靠这两口，Win32／D3D11 之外全 false）；
    //   ③ 全局热键后端（无焦点也要能呼出，这是 quake 的定义；没有它就没有任何入口）；
    //   ④ 组合键文本可解析（`KeyCombo::from` 与显示串逐字节往返，畸形即整条降级）。
    const borealis::config::QuickTerminalSettings &quick_terminal = settings.ui.quick_terminal;
    borealis::ui::QuakeToggle quake_toggle{quick_terminal.auto_hide,
                                           std::chrono::milliseconds{quick_terminal.focus_settle_ms}};
    // 热键闭包与失焦回调共用的唯一出口。声明留空、等宿主登记之后才赋值：Show 分支要那扇窗的 surface
    // 才算得出落位。热键只在帧循环里被排空（见 `set_on_frame` 顶部），而那时这里已经就位。
    std::function<void(borealis::ui::QuakeIntent)> apply_quake_intent;
    std::unique_ptr<au::OsHotkeyRegistry> quake_hotkeys;
    bool quake_enabled = quick_terminal.enabled;
    std::optional<au::KeyCombo> quake_combo;
    if (quake_enabled) {
        quake_combo = au::KeyCombo::from(quick_terminal.hotkey);
        const au::PlatformCapabilities capabilities = au::platform().capabilities();
        if (!capabilities.runtime_visibility || !capabilities.runtime_always_on_top) {
            AURORA_LOG_WARN("main", "quick terminal disabled: this backend lacks runtime visibility or always-on-top");
            quake_enabled = false;
        } else if (!au::OsHotkeyRegistry::enabled()) {
            AURORA_LOG_WARN("main", "quick terminal disabled: no OS-wide hotkey backend");
            quake_enabled = false;
        } else if (!quake_combo.has_value()) {
            AURORA_LOG_WARN("main", "quick terminal disabled: unparsable hotkey \"", quick_terminal.hotkey, "\"");
            quake_enabled = false;
        }
    }
    if (quake_enabled) {
        quake_hotkeys = std::make_unique<au::OsHotkeyRegistry>();
        // 动作只翻转状态机并交意图，不在这里碰窗口：命中由框架的共享消息窗收进队列、帧内排空
        // （`Application::step_frame` 只排它自己那份注册表，本仓自持的这份在 `on_frame` 里自己排），
        // 于是改几何与显隐时栈上没有窗口过程帧。
        const au::Result<au::OsHotkeyHandle> registered =
            quake_hotkeys->add(*quake_combo, [&quake_toggle, &apply_quake_intent]() -> void {
                if (apply_quake_intent) {
                    apply_quake_intent(quake_toggle.on_hotkey(borealis::ui::QuakeToggle::Clock::now()));
                }
            });
        if (!registered.ok()) {
            // 注册失败排在造窗之前 ⇒ 这一扇还没按帘子形态造，回退到普通窗口是完整的一档。
            AURORA_LOG_WARN("main", "quick terminal disabled: hotkey registration failed: ",
                            registered.error().message);
            quake_hotkeys.reset();
            quake_enabled = false;
        }
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
    auto first_workspace = open_workspace_window("Borealis", next_window_index++, quake_enabled);
    if (first_workspace == nullptr) {
        return 1;  // 建窗失败原文已由工厂落日志；这里不建宿主、不读盘、不起会话。
    }
    std::vector<std::unique_ptr<WorkspaceWindow>> workspace_windows;
    workspace_windows.push_back(std::move(first_workspace));
    // **不留「第一扇窗」的长期引用（稿 §6 批 4-A）**：批 1 到批 3 的过渡期靠五枚别名吃
    // `workspace_windows.front()` 那个对象（`surface`／`tab_strip`／`current_tab_id`／`banner`／
    // `overlay_host`），而关窗那一棒要把这一格从容器里取走——引用当场悬空，面板与闭包读到的就是已经
    // 散掉的那一份。于是这里只留两条**现取**的腿：`fallback_window()` 每次重新读容器首格（空即没有
    // 兜底窗，调用方按「无事发生」处理，不猜任何一扇），给应用级面板的唤醒与浮层落点用；
    // `command_window()` 是命令与快捷键的落点，取的是「最近被碰过的那一扇」（tracker），它要等宿主
    // 登记之后才取得到，故与 `report_dispatch` 同形态——先声明、按引用捕、赋值在建钩之后。
    auto fallback_window = [&workspace_windows]() -> WorkspaceWindow * {
        return workspace_windows.empty() ? nullptr : workspace_windows.front().get();
    };
    std::function<WorkspaceWindow *()> command_window;
    // 别名过渡：批 1 只把**归属**换到 `WorkspaceWindow` 上。批 2 把「逐窗才正确」的那几条腿改按 owner
    // 取了（会话唤醒取属主 surface、视口浮层宿主与焦点管理器按属主窗、帧泵的角标与标签名写给属主标签
    // 条、窗口标题逐窗差分写回），批 3 把命令落点按活动窗解析，剩下的按窗账目是布局重建的按窗过滤与
    // 面板「搬窗」的落点（稿 D6＝批 4-C）。`opts` 与 `window` 两支不另设腿：前者只在交给宿主那一句里
    // 读，后者当场交出所有权。

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
    // 留第二处真值源（同 `root_column` 那枚别名的处理），故删除。`tab_strip` 那一枚同样删掉：横幅刷新
    // 改成逐窗取各自那一栏（稿 D5，本棒 4-A），留着它就是把「主窗那一栏」当成全局真值源。
    // `next_tab_id` 刻意**不在**这里——它是进程级一支（稿 §0 的 F10），两扇窗共用才不撞号。

    // 标识只增不复用（与 PaneTree 的容器标识同一条理由，裁决 7.42⑩）：新建与撤销重开**共用**这一支
    // 计数器，否则「关一个再重开」造出的新身份会撞进仍存活的标签里，`add`/`insert_at` 的重复即静默失败。
    borealis::ui::TabId next_tab_id = 1;

    // 每个标签对应一个 WorkspaceView（支持该标签内的分屏）
    //
    /// @brief 一格会话的料：会话本体，与该格视口粘贴档要用的行尾（裁决 7.110 D11① 的同源口径）。
    ///
    /// `local_spec` 是本地腿带回来的那份**有效料**（`SPEC.FEAT.WS.11` 稿 D4①、F8 末句的那份欠账）：
    /// 命令／目录／环境三件在工厂里算完就丢，而恢复要按格存它——切分新格的料只有这一条腿知道（OSC 7
    /// 继承与一次性 `--cwd` 都折算在它里面）。非本地腿留空值，登记侧据此分流，本层不认识标签类型。
    struct PaneBundle {
        std::shared_ptr<borealis::session::Session> session{};
        borealis::term::LineEnding line_ending{borealis::term::LineEnding::Lf};
        std::optional<borealis::conn::LocalTerminalSpec> local_spec{};
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
        // 本地腿每格的有效料（`SPEC.FEAT.WS.11` 稿 D4①：PLAN 那句「每标签连接规格」缺的正是本地这条）。
        // 键与 `sessions_by_pane` 同号、同生死（`make_pane` 登记、`teardown_pane` 撤账），值存**有效值**
        // 而不是引用——口径照 WS.10 那枚 `ClosedTabSpec`（F14①）。非本地标签这张表恒空：那三腿的料挂在
        // 标签级那四张表上（F8）。采集腿按 `panes_in_order()` 的序位读它，故这里不需要记序位。
        std::map<borealis::ui::PaneId, borealis::conn::LocalTerminalSpec> local_specs_by_pane;
        PaneSessionFactory make_session;  ///< 本标签的切分工厂；缺席即这一格切不开（fail-closed）
        std::shared_ptr<borealis::ui::WorkspaceView> workspace;
    };
    std::map<borealis::ui::TabId, TabWorkspace> tab_workspaces;

    // 当前显示的标签号**按窗一份**（稿 §2）：两扇窗各有各的「当前」，故这里不再有全局别名——闭包读的是
    // `command_window()->current_tab_id`（落点窗自己那一格），装配早期给主窗建首标签那几处直接现取容器
    // 首格。留着别名等于留第二处真值源，而关窗那一棒会把首格从容器里取走（批 4-A 的理由）。

    // 标签的会话类型（`SPEC.FEAT.INTEG.02`）：新标签是否继承当前目录只由「当前标签是不是本地
    // 终端」决定，而本地标签在 SSH / 串口 / Telnet 三张接线表里都没有身份，故单立一张类型表。
    // 撤账纪律照那三张：`perform_close` 随标签同撤，撤销重开登记为 Local，重启换腿不改类型。
    //
    // 标签栏的类型图标也从这同一张表折（`SPEC.FEAT.WS.04` 稿 §5 第 1 条：类型只有一个来源，UI 侧不再
    // 记第二份）。枚举本体取档案域的 `conn::ConnectionType` 而不是再造一枚局部 `TabKind`（稿 D2①）：
    // 第四枚同义枚举会把「只有本地格才继承 OSC 7」与「图标取哪一枚字形」拆成两个类型域各判一次。
    std::map<borealis::ui::TabId, borealis::conn::ConnectionType> tab_kind;

    // ---- 广播组（`SPEC.FEAT.WS.09`，裁决 7.121）----
    // 组与端点表是 app 级一份（D2）：成员 `(TabId, PaneId)` 跨标签保留、不落盘。横幅常驻根 Column
    // 的 chrome 前缀之后（D10：原本 index 1，自绘标题栏挂上首格后是 index 2，裁决 7.181），本处先造
    // 控件，稍后与标题栏 / 标签条 / 工作区一起入根（顺序见 `root_column` 的组装）。
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
    // 主窗那一枚横幅要在首标签创建**之前**就存在（`router.attach` 那一刻就要有可写的横幅），故在此先造；
    // 第二扇窗之后由 `assemble_window_content(window)` 按窗补建。不留 `broadcast_banner` 别名：读者只有
    // `refresh_broadcast_ui` 那一处，而它已经改成逐窗写各自的横幅（稿 D5）。
    workspace_windows.front()->banner = make_broadcast_banner(*workspace_windows.front());

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
    // 逐窗各刷各的横幅（稿 D5，批 4-A）：成员按**属主窗**分流，而属主的唯一真源就是
    // `TabWorkspace::owner`（批 2-A 那枚指针）——不另立「这张标签属于谁」的账，也不去别窗的标签栏里
    // 找一个本不存在的号。高 0/26 因此是「我这扇窗里有没有格子在跟着收」这一眼见事实；那句计数仍按
    // 全组去重数（D5 原文：批量操作是一件事，数字要能对上总数，别窗的成员在本法眼里是看不见的名字）。
    refresh_broadcast_ui = [&]() -> void {
        const auto &members = broadcast_router.members();
        for (const std::unique_ptr<WorkspaceWindow> &window_ptr : workspace_windows) {
            WorkspaceWindow &window = *window_ptr;
            if (!window.banner) {
                continue;  // 还没装配内容的窗（建窗失败那一档不在此列，它压根不入容器）
            }
            borealis::ui::BroadcastBannerState state;
            state.session_count = broadcast_router.unique_size();

            std::map<borealis::ui::TabId, std::size_t> per_tab;
            for (const borealis::ui::BroadcastMember &member : members) {
                const auto owner_found = tab_workspaces.find(member.tab);
                if (owner_found != tab_workspaces.end() && owner_found->second.owner == &window) {
                    ++per_tab[member.tab];
                }
            }
            const auto tabs = window.tab_strip.tabs();
            std::vector<const void *> seen;
            std::map<borealis::ui::TabId, std::size_t> ordinals;
            for (const borealis::ui::BroadcastMember &member : members) {
                // 只列本窗成员（D5）：属主取不到或不是这一扇即跳过——别的窗的横幅会列它。
                const auto owner_found = tab_workspaces.find(member.tab);
                if (owner_found == tab_workspaces.end() || owner_found->second.owner != &window) {
                    continue;
                }
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
                    name += decode_utf8(borealis::ui::settings_label("workspace.broadcast.pane",
                                                                     {{"ordinal", static_cast<std::int64_t>(
                                                                       ++ordinals[member.tab])}}));
                }
                state.names.push_back(std::move(name));
            }
            state.active = !state.names.empty();
            window.banner->set_state(std::move(state));
        }
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
    // CONN.03 批 4 起这份留档是**一整套**（目标 + 逐跳）：切分与重启都按同一份 bundle 重拨，
    // 于是新格与老格共享同一条跳链（稿 D5「跳链属于链路，不随标签」）。批 6 起 SFTP 面板也吃
    // 整套——它的独立会话经同一份逐跳材料建链（批 6 D3，`HopUnwired` 拒拨口径随接线退场）。
    std::map<borealis::ui::TabId, borealis::conn::SshProfile> ssh_profile_by_tab;
    std::map<borealis::ui::TabId, borealis::conn::SshSecretBundle> ssh_secret_by_tab;

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

    // 标签→**档案 id**（`SPEC.FEAT.WS.11` 稿 D4① 的 id 半边）：上面三张表存的是整份档案字段，而需求
    // 那句「profile」要的是「这一格开的是哪一份档案」。两条都写、恢复时 id 命中 `settings.profiles`
    // 即以表里那一份为准（档案改过名再重启看到新名字，单一真值源留在配置），未命中才用快照里的字段
    // 兜底——quick connect 现造的那份不落表（F10），只存 id 就结构上取不回任何料。
    // 本地缺省腿与撤销重开回来的标签没有档案身份，表里没有这一格即空 id（写侧照存空串）。
    // 撤账纪律照那三张表：`erase_tab_ledger` 随标签同撤。
    std::map<borealis::ui::TabId, std::string> tab_profile_id;

    // ---- 会话恢复的采集与提交（`SPEC.FEAT.WS.11` 批 5-B，稿 D2①／D4／判据 ③）----
    //
    // 采集腿**只读现成的账**（稿 F15）：窗序＝容器序、标签序＝`TabStrip::tabs()` 的条内序、类型＝
    // `tab_kind`（WS.04 认定的唯一来源）、档案＝上面四张表、分屏形状与选中格＝`WorkspaceView` 批 5-A
    // 那两枚读数、本地格的有效料＝`TabWorkspace::local_specs_by_pane`。本层不新造遍历原语，也不另立
    // 一张「布局在哪」的表——与 `TabStrip` 或树并存的第二份账必然分叉。
    //
    // fail-closed 的落点是**单张标签**而不是整份文档：某一格的类型或档案在表里取不到（正常路径走不到，
    // 登记与建标签同批发生）就跳过它并记一条 WARN。跳过比「整份不写」好——后者会让布局从这一次起永远
    // 冻结在旧值；也比利他好——把一格格料不明的标签写进快照，恢复出来的是本仓没有的「半个连接」。
    auto capture_workspace_snapshot = [&]() -> borealis::ui::WorkspaceSnapshot {
        borealis::ui::WorkspaceSnapshot snapshot;
        for (const std::unique_ptr<WorkspaceWindow> &window_ptr : workspace_windows) {
            if (window_ptr == nullptr) {
                continue;
            }
            WorkspaceWindow &window = *window_ptr;
            borealis::ui::SnapshotWindow entry;
            for (const borealis::ui::Tab &tab : window.tab_strip.tabs()) {
                const auto workspace = tab_workspaces.find(tab.id);
                if (workspace == tab_workspaces.end() || workspace->second.workspace == nullptr) {
                    AURORA_LOG_WARN("main", "snapshot: tab ", tab.id, " has no workspace on file, skipped");
                    continue;
                }
                const auto kind = tab_kind.find(tab.id);
                if (kind == tab_kind.end()) {
                    AURORA_LOG_WARN("main", "snapshot: tab ", tab.id, " has no connection kind, skipped");
                    continue;
                }
                borealis::ui::WorkspaceView &view = *workspace->second.workspace;
                const std::vector<borealis::ui::PaneId> order = view.panes_in_order();
                if (order.empty()) {
                    // 空树在写侧不可达（`PaneTree` 恒有至少一枚叶子），读侧的 `focused_order < leaves`
                    // 因此在这一格无处可取——留着它就是把一份必然被整份拒绝的文档写进盘。
                    AURORA_LOG_WARN("main", "snapshot: tab ", tab.id, " lists no pane, skipped");
                    continue;
                }
                borealis::ui::SnapshotTab item;
                item.kind = kind->second;
                if (const auto held = tab_profile_id.find(tab.id); held != tab_profile_id.end()) {
                    item.profile_id = held->second;
                }
                // 只搬**手动重命名**那一档（稿 D10①）：OSC 标题属会话内容，需求那句「会话内容不恢复」
                // 把它排除在外，重启后由远端重发；把它存成手动名就等于让一次旧会话压过用户自己的改名。
                static_cast<void>(borealis::term::encode_utf8(tab.names.manual_name, item.title));
                item.shape = view.pane_shape();
                // 选中格记**序位**不记 `PaneId`（稿 F23：号是本进程发的，跨进程存它必然漂）。
                const borealis::ui::PaneId focused = view.focused_pane();
                for (std::size_t index = 0U; index < order.size(); ++index) {
                    if (order[index] == focused) {
                        item.focused_order = index;
                        break;
                    }
                }
                bool incomplete = false;
                switch (item.kind) {
                    case borealis::conn::ConnectionType::Local:
                        // 本地腿的料是 per-pane 的，且必须与形状的叶子**按序位同长**（模型读侧的那道闸）。
                        for (const borealis::ui::PaneId pane : order) {
                            const auto spec = workspace->second.local_specs_by_pane.find(pane);
                            if (spec == workspace->second.local_specs_by_pane.end()) {
                                AURORA_LOG_WARN("main", "snapshot: tab ", tab.id, " pane ", pane,
                                                " carries no local spec, tab skipped");
                                incomplete = true;
                                break;
                            }
                            item.panes.push_back(spec->second);
                        }
                        break;
                    case borealis::conn::ConnectionType::Ssh: {
                        const auto held = ssh_profile_by_tab.find(tab.id);
                        if (held == ssh_profile_by_tab.end()) {
                            AURORA_LOG_WARN("main", "snapshot: tab ", tab.id, " carries no SSH profile, tab skipped");
                            incomplete = true;
                            break;
                        }
                        item.ssh = held->second;  // 内含 `SecretHandle` 而不是明文（判据 ⑥ 的正面形态）。
                        break;
                    }
                    case borealis::conn::ConnectionType::Serial: {
                        const auto held = serial_by_tab.find(tab.id);
                        if (held == serial_by_tab.end()) {
                            AURORA_LOG_WARN("main", "snapshot: tab ", tab.id,
                                            " carries no serial profile, tab skipped");
                            incomplete = true;
                            break;
                        }
                        item.serial = held->second.profile;  // 裸指针不进快照：那条腿归 Session 所有。
                        break;
                    }
                    case borealis::conn::ConnectionType::Telnet: {
                        const auto held = telnet_by_tab.find(tab.id);
                        if (held == telnet_by_tab.end()) {
                            AURORA_LOG_WARN("main", "snapshot: tab ", tab.id,
                                            " carries no telnet profile, tab skipped");
                            incomplete = true;
                            break;
                        }
                        item.telnet = held->second;
                        break;
                    }
                }
                if (incomplete) {
                    continue;
                }
                entry.tabs.push_back(std::move(item));
            }
            if (entry.tabs.empty()) {
                continue;  // 空窗进不了快照：模型读侧的 `selected_tab` 在零标签上无处可取（文件头在册）。
            }
            // 选中档取**本窗那一份当前号**在条内的下标；对不上（中间态）就落回首格，仍是合法文档。
            std::size_t selected = 0U;
            if (window.current_tab_id.has_value()) {
                if (const auto held = window.tab_strip.index_of(*window.current_tab_id); held.has_value()) {
                    selected = *held;
                }
            }
            entry.selected_tab = selected;
            snapshot.windows.push_back(std::move(entry));
        }
        return snapshot;
    };
    // 提交腿（稿 D2①「拓扑变更事件当场写」）：与 `flush_window_geometry()` 同一形态——内存改写 + 主动
    // 提交，失败只落日志不弹框（稿 D8① 的在册代价：布局静默消失，界面上没有清理入口也没有提示）。
    // 为什么是「当场」而不是只留退出那一处：`windows.json` 那条腿的注释已经把崩溃那件事说清了（F4）——
    // 崩掉的那一次不会有退出腿。为什么不算阻塞 IO 违第 25 条：本条的变更点是**用户动作**（按一下键、
    // 点一下格）而不是每帧绘制，写的是几 KB 的本地原子文件，量级与既有的几何提交逐字相同。
    auto flush_workspace_snapshot = [&workspace_store, &settings, &capture_workspace_snapshot]() -> void {
        // 判据 ③ 的写侧半边：开关关掉即不写，而**已有那份文件不动**（本件没有删除入口，F26）。
        if (!settings.connection.restore_sessions) {
            return;
        }
        const std::optional<std::string> text = capture_workspace_snapshot().to_json();
        if (text == std::nullopt) {
            AURORA_LOG_WARN("main", "workspace snapshot encode refused (non-finite pane length)");
            return;
        }
        const auto parsed = au::json::parse(*text);
        if (!parsed.ok()) {
            AURORA_LOG_WARN("main", "workspace snapshot did not parse: ", parsed.error().message);
            return;
        }
        workspace_store.preferences()->set(std::string{borealis::config::kWorkspaceLayoutKey}, parsed.value());
        if (const auto reason = workspace_store.flush(); reason.has_value()) {
            AURORA_LOG_WARN("main", "workspace snapshot flush failed: ", *reason);
        }
    };

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

    // 关标签的请求（`SPEC.FEAT.WS.01` 的末位 pane 腿）：标签条那颗关闭钮与 `Ctrl+Shift+W` 在单格
    // 标签上走同一条请求，判据、确认与撤账只在 `perform_close` 一处。与 `open_new_window_request`
    // 同形态——工作区的 `Hooks` 建得比关标签闭包早，故这里只声明、由建钩处按引用捕获。
    std::function<void(borealis::ui::TabId)> close_tab_request;

    // 只开一扇新窗、**不**落首标签，回该窗指针（没建成回 nullptr）。CLI `--new-window` 那一腿要用它：
    // 首标签吃哪份料（`--profile` 的档案或 `--cwd` 的目录）由请求决定，先落一格缺省本地标签再补一条
    // 就成了两格。`open_new_window_request` 是它加 D1 缺省首标签的那一层壳。
    std::function<WorkspaceWindow *()> open_new_window;

    // 「关闭这一扇窗」的请求（`SPEC.FEAT.WS.03` 稿 D3 的另一条入口）：命令面板那一格在登记段就要按
    // 引用捕获它，而它的两条腿（窗级活会话折法、按窗确认框）与关窗闸门同源于后面那一段，故这里只声明。
    // 与 `open_new_window_request` 同形态；缺钩即那条命令只占一格面板行、什么都不做。
    std::function<void(WorkspaceWindow &)> request_close_window;

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
        // 仍有效（窗口本体归宿主，surface 归窗口）。拆窗时这一腿的停发有**两层**担保：装配侧关窗
        // 撤账链先逐格 close 会话（连接侧 join 读线程）才 erase 窗；会话侧 `Session::close()` 在
        // join 返回后清空本腿——先 close 再离手的残留档（重启的 `retired`、回滚归还那一条）自此
        // 不持任何闭包，悬垂一类被结构性关死（判据证人 `utest_session`，`SPEC.FEAT.WS.03`）。
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
            // 本地腿的有效料随会话同登（稿 F8 末句那份欠账在这一处清偿）：这一格将来在快照里占的
            // 那个序位要有料，而工厂之外的任何一处都算不出它（OSC 7 继承与一次性 `--cwd` 都在工厂里折）。
            if (bundle->local_spec.has_value()) {
                found->second.local_specs_by_pane[pane] = *bundle->local_spec;
            }
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
                // 有效料与会话同号同撤（上面那一笔的对应腿）：漏这一笔就是「快照里写着一格已经不存在的
                // 料」，而采集按树里的序位取料，多出来那格只是永远读不到的废纸——按撤账纪律一并摘。
                found->second.local_specs_by_pane.erase(pane);
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
        // 末位 pane 的关格键转交「关标签」：本层不造空树、也不判退出（`SPEC.FEAT.WS.01`，裁决
        // 7.42⑤）。缺钩即该键只被认领、什么都不做；生产路径在关标签闭包就绪后赋值。
        hooks.close_last_pane = [&close_tab_request, id]() -> void {
            if (close_tab_request) {
                close_tab_request(id);
            }
        };
        // 布局变更的提交点（`SPEC.FEAT.WS.11` 稿 D2①的「切分」「关格」两类）。装在这里而不是各调用点
        // 补一句：三条建工作区的路径（新开格 / 撤销重开 / 批 5-C 的重放）都经这一支，漏装的结构上不存在
        // ——与上面那两条「靠一处装钩而不是靠人记得」的纪律同一条。
        hooks.structure_changed = [&flush_workspace_snapshot]() -> void { flush_workspace_snapshot(); };
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
    // @p window 是这张标签落在哪扇窗（稿 §2）：标签条、当前标签号与往后那一切的唤醒都按它走。
    //     调用点各按自己的落点窗传：冷启动首标签与新建窗是容器首格现取，档案连接那四条腿取的是
    //     tracker 解析出的落点窗（批 3 给出入口、批 4-A 收掉主窗别名）。
    // @p local_spec 只有本地腿给值（`SPEC.FEAT.WS.11` 稿 D4①）：这一格起什么命令、在哪个目录，除了
    //     调用方没有任何人知道，而恢复要按序位把这格的有效料要回来。三腿档案不从这里走——它们的料挂在
    //     标签级那四张表上，由各条腿在登记处自己写。
    auto create_new_tab_with = [&](WorkspaceWindow &window,
                                   std::shared_ptr<borealis::session::Session> session,
                                   const std::u32string &tab_name, borealis::conn::ConnectionType kind,
                                   PaneSessionFactory factory,
                                   borealis::term::LineEnding line_ending =
                                       borealis::term::LineEnding::Lf,
                                   std::optional<borealis::conn::LocalTerminalSpec> local_spec =
                                       std::nullopt) -> void {
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
        if (local_spec.has_value()) {
            tab.local_specs_by_pane[1U] = *local_spec;  // 同一条常数约定：首格的标识就是 1。
        }
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
        if (kind != tab_kind.end() && kind->second == borealis::conn::ConnectionType::Local && source != nullptr &&
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
            // 有效料与会话一起交回去（`local_spec` 那一笔）：这一格的目录与命令行只在这一处算得出来，
            // 而快照要按序位读它。留副本不留引用——`spec` 是本闭包的局部量。
            return PaneBundle{.session = make_local_session(spec), .local_spec = spec};
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
            return PaneBundle{.session = make_local_session(spec), .local_spec = spec};
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
                              std::optional<std::string> cwd = std::nullopt,
                              bool commit = true) -> void {
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
        create_new_tab_with(window, make_local_session(spec),
                            decode_utf8(borealis::ui::settings_label("connections.type.local")),
                            borealis::conn::ConnectionType::Local, make_local_pane_factory(id),
                            borealis::term::LineEnding::Lf, spec);
        // 布局变更的提交点（稿 D2①「开标签」那一类）。本地缺省腿在这里已经齐料，四条档案腿各自排在
        // 登记之后——`create_new_tab_with` 内部不提交，否则 SSH 腿会写出一张「类型是 Ssh 而档案表里
        // 还没有它」的标签，而那一格在采集侧是要被跳过的（等于白关一次现场）。
        // @p commit 为 false 只服务启动重放里「这一扇一格都没恢复出来」的兜底标签（批 5-C）：那一格
        // 不是用户现场，重放中途写出去就把一份半成品当成「上次的布局」。
        if (commit) {
            flush_workspace_snapshot();
        }
    };

    // 首标签与冷启动请求的关系（稿 §5.1 / §5.4）：只有「开标签那一档动作且带 `--profile`」要把这一格
    // **换掉**，否则会多出一条没人要的缺省标签；无档案那一档在这一格就把 `--cwd` 折进去，
    // `split-pane` 则照旧建这一格、等接线齐备后再对它切一刀。
    // 冷启动的 `--new-window` 与缺省 `new-tab` 同一条腿（稿 §5.3「接受且不产生额外动作」）：此刻还没有
    // 别的窗，正要建的这一扇就是那扇新窗，`apply_startup_request` 里也只按转交侧才另开。
    const bool cold_first_tab_is_tab_action =
        startup_request.has_value() &&
        (startup_request->action == borealis::cli::StartupAction::NewTab ||
         startup_request->action == borealis::cli::StartupAction::NewWindow);
    const bool cold_startup_profile_tab =
        cold_first_tab_is_tab_action && !startup_request->profile.empty();
    // 缺省 `new-tab` 已由上面那一格交付（含 `--cwd`），`app.run()` 前不再重复开标签。
    const bool cold_startup_plain_tab_done =
        cold_first_tab_is_tab_action && startup_request->profile.empty();
    const std::optional<std::string> startup_cwd_override =
        (startup_request.has_value() && !startup_request->cwd.empty())
            ? std::optional<std::string>{startup_request->cwd}
            : std::nullopt;
    // 恢复腿在场时这一格让位（`SPEC.FEAT.WS.11` 稿 D7①）：首标签由重放按快照造，这里再补一张缺省
    // 本地标签就是「恢复出来的那扇窗里凭空多一格」。`--cwd` 单飞不算显式请求，它既没有跳过恢复，
    // 也没有在这一格被消费——重放的泵收尾时会把同一份请求喂给 `apply_startup_request`（补在恢复
    // 出来的主窗里；挂起等凭据时只在最后一格回来后补，免得用户点名要的那一格被之后的收尾选中压掉）。
    if (!cold_startup_profile_tab && !restore_at_startup) {
        create_new_tab(*workspace_windows.front(), startup_cwd_override);
    }

    // 辅助函数：重建**某一扇窗**根 Column 的子节点列表（标题栏 + 标签栏 + 广播横幅 + 中段行 + 状态栏）。
    // 形参从「列 + 全局当前号」换成「窗 + 全表」（稿 §6 批 3）：多窗下那张 `tab_workspaces` 是应用级
    // 一张表（稿 §0 的 F10：号全局唯一），按号查出来的那一格可能属于别的窗——把它塞进本窗的根就是
    // 「两扇窗显示同一格工作区」，而工作区只认一个父节点，两扇的布局会互相掀掉。归属由批 2 那一枚
    // `TabWorkspace::owner` 判，号本身不携带这个信息。
    auto rebuild_root_children = [](WorkspaceWindow &window,
                                    const std::map<borealis::ui::TabId, TabWorkspace> &workspaces)
        -> void {
        // 保留头部那几格 chrome（标题栏 + 标签栏 + 广播横幅），替换其后两格（中段行、状态栏）。横幅是常驻节点（D10），
        // 组空时高 0 也不摘除——摘除会让显隐走「增删子节点」这条分叉。标题栏同一条口径：全屏那一档它自报 0 高而不是
        // 从根上被摘走（裁决 7.181），故这里的格数只按「本窗挂没挂这一条」定，不随窗口形态起伏——帘子（quake）不挂。
        const std::size_t chrome_prefix = window.title_bar ? 3U : 2U;
        auto &nodes = window.root_column->child_nodes_mut();
        if (nodes.size() < chrome_prefix) {
            return;  // 还没初始化完
        }
        nodes.resize(chrome_prefix);
        // 中段行自己也是常驻节点：换标签换的是**行里那一格工作区**，而不是行本身——行一摘，挂在行里的
        // 常驻列（屏 1 判据 ③）就跟着散一次，而它按 D10 的口径与横幅、状态栏同档：骨架不随内容起伏。
        if (window.content_row) {
            std::vector<aurora::Node> row_children{};
            // 常驻列排第一（34 dp 或 320 dp，槽内在轨与面板之间换）；侧栏还没建成的那一帧不占位。
            if (window.sidebar) {
                row_children.push_back(aurora::Node{window.sidebar->slot_widget()});
            }
            if (window.current_tab_id.has_value()) {
                const auto it = workspaces.find(window.current_tab_id.value());
                if (it != workspaces.end() && it->second.owner == &window) {
                    // 工作区吃掉这一行里除常驻列以外的全部宽度（expand 权重交 FlexLayouter 分配）。
                    aurora::Node ws{it->second.workspace};
                    ws.widget().set_modifier(au::Modifier{}.expand(1.0F));
                    row_children.push_back(std::move(ws));
                }
            }
            window.content_row->child_nodes_mut() = std::vector<aurora::Node>{std::move(row_children)};
            window.content_row->mark_needs_layout();
            nodes.push_back(aurora::Node{window.content_row});
        }
        // 状态栏排在末位、自报 28 dp 高（屏 1 判据 ⑨），因此**不参与** expand 的分母：上面那一格
        // expand(1.0) 吃掉除 28 dp 以外的全部剩余高度。与横幅同一条常驻纪律——没有读数的那一帧
        // 也留着这一条，否则窗底会随会话状态起伏。
        if (window.status_bar) {
            nodes.push_back(aurora::Node{window.status_bar});
        }
        window.root_column->mark_needs_layout();
        window.root_column->mark_needs_paint();
    };

    /// @brief 撤销重开：把已建好的会话按原位置插回标签条（本地腿与 SSH 腿共用）。
    ///
    /// 与 `create_new_tab_with` 的差别只有「位置」与「名字」：那一支 `add` 到条尾并按档案名，
    /// 这一支 `insert_at` 回原位并按闭包栈记下的名字。两处共用 `build_pane_view` /
    /// `wire_workspace_hooks` / `attach_broadcast_sink`，闸门与浮层宿主因此不会因这条腿少接一段
    /// （裁决 7.82②(b) 的同一条纪律）。插入失败即整张 `tab` 析构，成员次序保证视口先散、会话后散。
    /// @param id 调用方从 `next_tab_id` 取走的号；本函数不发行身份。
    /// @return true＝已入栏；false＝位置不合，本次不作数（号已烧掉，身份只增不复用）。
    auto restore_tab_at = [&](WorkspaceWindow &window, borealis::ui::TabId id,
                              std::shared_ptr<borealis::session::Session> session,
                              const std::u32string &name, std::size_t index,
                              borealis::conn::ConnectionType kind, PaneSessionFactory factory,
                              std::optional<borealis::conn::LocalTerminalSpec> local_spec =
                                  std::nullopt) -> bool {
        const std::shared_ptr<borealis::ui::TerminalView> view =
            build_pane_view(window, session, borealis::term::LineEnding::Lf);
        borealis::ui::WorkspaceView::Hooks ws_hooks;
        wire_workspace_hooks(id, ws_hooks);

        TabWorkspace tab;
        tab.owner = &window;
        tab.sessions_by_pane[1U] = std::move(session);
        if (local_spec.has_value()) {
            tab.local_specs_by_pane[1U] = *local_spec;
        }
        tab.make_session = std::move(factory);
        tab.workspace = std::make_shared<borealis::ui::WorkspaceView>(view, std::move(ws_hooks));
        if (!window.tab_strip.insert_at(index, id, name)) {
            AURORA_LOG_WARN("main", "undo close tab: insert_at failed at index ", index);
            return false;
        }
        tab_workspaces[id] = std::move(tab);
        tab_kind[id] = kind;
        // 新格的成员表从空开始；端点仍按「三处构造点同源」登记。
        attach_broadcast_sink(id);
        window.current_tab_id = id;
        window.tab_strip.select(id);
        rebuild_root_children(window, tab_workspaces);
        return true;
    };

    // 浮层宿主已随 `open_workspace_window()` 建好（批 1 把它的归属挪进 `WorkspaceWindow`，场景根形态与
    // 裁决 7.41③ 那段理由都在工厂里）。这里给四面板与降级提示取的是**首格那一份的副本**（`shared_ptr`
    // 按值，而不是引用别名）——面板持的是 `OverlayHost &`（非拥有），若本装配层只留一枚指着容器元素的
    // 引用，关窗那一棒把首格从容器取走时面板就悬空了；按值留一份则对象随 `main()` 一起活，最坏只是
    // 浮层挂在一扇已经关掉的窗上（看不见，但不是野指针）。面板「搬窗」而非「各一份」是 D6 的口径、
    // 批 4-C 那一棒，届时这一支按发起窗现取。根 Column 一支不设别名：入根已经收进
    // `assemble_window_content()`，其余读它的地方全按窗取。
    std::shared_ptr<au::OverlayHost> host = workspace_windows.front()->overlay_host;

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

    // 一张标签的**账目半区**撤账：摘根、撤会话表、四张连接表与类型表。`perform_close`（用户关掉一格）
    // 与批 4-B 的关窗撤账链（窗散了，它名下那几格跟着散）共用这一支，于是「窗拆后三处账目为空」与
    // 「关掉这一格账目为空」是同一条代码而不是两处各写一遍。差别只在栏位摘除与闭包栈入栈留在
    // `perform_close` 里：一扇散掉的窗，它的闭包栈随窗一起没，而栏外孤儿格本来就不在栏里。
    // 次序承重（原 `perform_close` 那一段）：先把根上那份工作区摘掉再撤会话表（稿 §5「会话活过视口」），
    // 此刻根上已不再持有任何一格的视口，视口在这一跳里散尽；反过来就是「会话先散、视口后散」，
    // 而 `~TerminalView` 要往它背后的会话写撤账空响应。
    auto erase_tab_ledger = [&](WorkspaceWindow &window, borealis::ui::TabId id) -> void {
        if (window.current_tab_id.has_value() && window.current_tab_id.value() == id) {
            // 选下一个或上一个（关窗那一路走到这里时栏已空，`selected()` 回空 ⇒ 当前号清掉）。
            const auto next = window.tab_strip.selected();
            if (next.has_value()) {
                window.current_tab_id = next.value();
            } else {
                window.current_tab_id.reset();
            }
            // 更新显示的工作区
            rebuild_root_children(window, tab_workspaces);
        }
        const auto it = tab_workspaces.find(id);
        if (it == tab_workspaces.end()) {
            return;
        }
        // 销毁对应的 WorkspaceView 和会话：`TabWorkspace` 的成员次序保证 `sessions_by_pane` 后析构。
        tab_workspaces.erase(it);
        // SFTP 面板接线数据随标签同撤（面板的 ssh_tab_alive 即以此判活，D8①）。
        ssh_profile_by_tab.erase(id);
        ssh_secret_by_tab.erase(id);
        // 串口接线同撤：那条连接腿随本格的 Session 一起析构，裸指针不留过夜（批 3）。
        serial_by_tab.erase(id);
        telnet_by_tab.erase(id);  // Telnet 同撤（CONN.06 批 3）。
        tab_kind.erase(id);       // 类型随标签同撤：重开那一支会重新登记（INTEG.02）。
        tab_profile_id.erase(id);  // 档案 id 同撤（WS.11 稿 D4①）：撤账纪律照那四张表，一格的账不跨它的生死。
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
        // 关闭前记录到闭包栈（`SPEC.FEAT.WS.10`）：名称与位置都取 `close()` 记下的那份——此刻该标签
        // 已不在表里，回查只会拿到空名，于是重开回来的标签变成一个无名格，那正是本件要避免的
        // 「内容不可恢复」被扩大成「名字也丢了」。
        if (tab_workspaces.find(id) != tab_workspaces.end()) {
            borealis::ui::ClosedTabSpec spec;
            if (const auto closed = window.tab_strip.last_closed(); closed.has_value()) {
                static_cast<void>(borealis::term::encode_utf8(closed->name, spec.name));
                spec.index_in_strip = closed->index_in_strip;
            }
            // 本地终端的连接规格从配置取；SSH 腿记档案 id 与整份档案快照（`SPEC.FEAT.WS.10`）——
            // id 用来在重开时回查**现在**那一份档案，快照兜底 quick connect 与已删档案。串口／Telnet
            // 仍按本地腿重开（本条需求只点名本地与 SSH 两条腿，这一档照旧）。
            spec.local_shell = settings.connection.local_shell;
            spec.startup_directory = settings.connection.startup_directory;
            const auto kind = tab_kind.find(id);
            if (kind != tab_kind.end() && kind->second == borealis::conn::ConnectionType::Ssh) {
                spec.leg = borealis::ui::ClosedTabLeg::Ssh;
                if (const auto profile = tab_profile_id.find(id); profile != tab_profile_id.end()) {
                    spec.profile_id = profile->second;
                }
                if (const auto ssh = ssh_profile_by_tab.find(id); ssh != ssh_profile_by_tab.end()) {
                    spec.ssh = ssh->second;
                }
            }
            window.closed_tab_stack.push(std::move(spec));
        }
        erase_tab_ledger(window, id);
        // 布局变更的提交点（稿 D2①「关标签」那一类）。排在撤账**之后**：采集读的是撤完那一份账，
        // 排在前面就写出一份「快照里还数着这张标签」的文档，而它已经在栏里没了。
        flush_workspace_snapshot();
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
                borealis::ui::settings_label("tab.close_confirm.title"),
                borealis::ui::settings_label("tab.close_confirm.body"),
                [&window, raw_id, &perform_close,
                 dialog = window.close_confirm](bool accepted) -> void {
                    dialog->close();
                    if (accepted) {
                        perform_close(window, raw_id);
                    }
                }));
        window.close_confirm->show();
    };

    // 关标签的请求收敛到一处（`SPEC.FEAT.WS.01`）：标签条那颗关闭钮与单格标签上的 `Ctrl+Shift+W`
    // 都走它，判据 `has_live_session`、确认框与撤账因此只有一份。末位标签仍由 `TabStrip::close()`
    // 拒绝（裁决 7.43⑤）——本闭包不接管「关窗口」，那条路径归 `SPEC.FEAT.WS.03`。
    close_tab_request = [&tab_workspaces, &has_live_session, &ask_close_confirm,
                         &perform_close](borealis::ui::TabId id) -> void {
        const auto found = tab_workspaces.find(id);
        if (found == tab_workspaces.end() || found->second.owner == nullptr) {
            return;  // fail-closed：账已撤或没有属主窗时，不猜一扇窗去弹框
        }
        WorkspaceWindow &window = *found->second.owner;
        if (has_live_session(id)) {
            ask_close_confirm(window, id);
            return;
        }
        perform_close(window, id);
    };

    // 标签栏的构造按窗一份（稿 §6 批 3）：六条 hooks 读的全是**本窗**那一份真值源——标签表、当前号、
    // 闭包栈（`perform_close` 里）。留在全局的只有「这张标签的工作区与会话」那几张应用级表（稿 §0
    // 的 F10：`TabId` 全局唯一），它们按号查、与窗无关。变更钩子同样装在本窗的 `TabStrip` 上。
    auto make_tab_bar = [&](WorkspaceWindow &window) -> void {
        window.tab_bar = std::make_shared<borealis::ui::TabBarWidget>(borealis::ui::TabBarHooks{
            .tabs = [&window, &settings, &tab_kind]() -> std::vector<borealis::ui::TabVisual> {
                std::vector<borealis::ui::TabVisual> out;
                const auto tabs = window.tab_strip.tabs();
                const auto selected = window.tab_strip.selected();
                for (const auto &tab : tabs) {
                    // 类型从这张表折来（`SPEC.FEAT.WS.04`）：表里没有 ⇒ 空值，绘制侧既不画也不占位
                    //（稿 D5①）。这一行是本条链唯一无自动化证人的一行（`main.cpp` 不进 runner，
                    // 裁决 7.83），故判据 ⑦ 只由编译、链接与真机走查兜底。
                    std::optional<borealis::conn::ConnectionType> kind;
                    const auto found = tab_kind.find(tab.id);
                    if (found != tab_kind.end()) {
                        kind = found->second;
                    }
                    out.push_back(borealis::ui::TabVisual{
                        .id = tab.id,
                        .display_name = borealis::ui::resolve_tab_name(tab.names, settings.appearance.tab_name_priority),
                        .is_selected = (selected.has_value() && selected.value() == tab.id),
                        .has_close_button = (window.tab_strip.count() > 1),  // 末位不给关
                        .bell_triggered = tab.bell_triggered,
                        .has_activity = tab.has_activity,
                        .exited = tab.exited,
                        .kind = kind,
                    });
                }
                return out;
            },
            .select = [&window, &tab_workspaces, &rebuild_root_children, &flush_workspace_snapshot](
                          std::uint64_t id) -> void {
                window.tab_strip.select(static_cast<borealis::ui::TabId>(id));
                window.current_tab_id = static_cast<borealis::ui::TabId>(id);
                // 切换当前显示的工作区视图
                rebuild_root_children(window, tab_workspaces);
                // 切到该标签即把两枚**事件**角标取走（`SPEC.FEAT.WS.04`）：BEL 与活动同一口径，只清活动
                // 会让铃铛在用户已经站在那一格之后还亮着且再也没有取走的入口。
                window.tab_strip.take_activity(static_cast<borealis::ui::TabId>(id));
                window.tab_strip.take_bell_triggered(static_cast<borealis::ui::TabId>(id));
                // 布局变更的提交点（稿 D2①「换选中」）：快照里的 `selected_tab` 就是这一格改的。
                flush_workspace_snapshot();
            },
            .close = [&close_tab_request](std::uint64_t id) -> void {
                // 与单格标签上的关格键同一条请求：判据、确认与撤账只在 `close_tab_request` 一处。
                if (close_tab_request) {
                    close_tab_request(static_cast<borealis::ui::TabId>(id));
                }
            },
            .rename = [&window, &flush_workspace_snapshot](std::uint64_t id, std::u32string name) -> void {
                window.tab_strip.rename(static_cast<borealis::ui::TabId>(id), std::move(name));
                // 布局变更的提交点（稿 D2①「改名」）：快照存的就是这一档**手动名**（D10①）。
                flush_workspace_snapshot();
            },
            .move = [&window, &flush_workspace_snapshot](std::uint64_t id, std::size_t to_index) -> void {
                window.tab_strip.move(static_cast<borealis::ui::TabId>(id), to_index);
                // 布局变更的提交点：稿 D2① 那七类没列「拖拽重排」，但快照里标签数组的**次序**就是条内序
                // （F15 采集腿的唯一来源），不在此提交就要等下一次变更或退出才记上——那是把已经发生的
                // 事实推迟，不是少写一类料。
                flush_workspace_snapshot();
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

    // 一扇窗的**内容入根**（稿 §6 批 3）：标题栏 → 标签栏 → 广播横幅 → 中段行 → 状态栏，次序是承重的
    // （横幅常驻 chrome 前缀之后，D10），故这一段整体复用而不是让第二扇窗各写一遍。根 Column 交进浮层宿主排在三件之后，
    // 与主窗原先那段次序逐字相同（裁决 7.41③：场景根是浮层宿主而不是视口）。
    /// @brief 按窗建那条常驻列（屏 1 判据 ③），先声明后赋值（`wizard_ref` / `report_dispatch` 同一形态）。
    ///
    /// 它的实体在档案表与向导都建好之后才写（下面 `sidebar_hooks` 那一块），而首扇窗的内容装配排在
    /// 那之前——故这里只能判「赋值了没有」：主窗的那一列由赋值处回头补建，第二扇窗之后走这一支。
    std::function<void(WorkspaceWindow &)> make_sidebar;

    auto assemble_window_content = [&](WorkspaceWindow &window) -> void {
        // 屏 1 顶部那条 36 dp 带（`SPEC.FEAT.WS.06`＋裁决 7.181）：只在**普通工作区窗**挂——帘子
        // （quake 那一档 `style.frameless`）没有标题带，也没有要显示的窗口标识。✕ 交回的是装配层
        // 那一条同一个 `request_close_window`（同一个确认闸门，不因发起方式是鼠标而少问一句），
        // 故本处按引用捕那枚 `std::function`：它的赋值排在首扇窗装配之后（`report_dispatch` 同形态）。
        if (!window.options.style.frameless && !window.title_bar) {
            window.title_bar = std::make_shared<borealis::ui::AppTitleBar>(
                borealis::ui::AppTitleBar::Hooks{
                    [&window, &request_close_window]() -> void {
                        if (request_close_window) {
                            request_close_window(window);
                        }
                    },
                    [](const borealis::ui::AppTitleBarAction action) -> void {
                        // 三枚钮的唯一效果观测点：无头后端把 minimize／toggle_maximize 记成空操作，
                        // 用例与日志只能从这里读到「哪一枚被按过」。真机侧的动作由 `WindowChrome` 落地。
                        const std::string_view label =
                            action == borealis::ui::AppTitleBarAction::Minimize
                                ? "minimize"
                                : action == borealis::ui::AppTitleBarAction::ToggleMaximize ? "maximize"
                                                                                            : "close";
                        AURORA_LOG_INFO("ui", "title bar action: ", label);
                    },
                });
        }
        make_tab_bar(window);
        // 主窗那一枚横幅在首标签创建**之前**就要存在（`refresh_broadcast_ui` 在 `router.attach` 那一刻
        // 就要有可写的横幅），故上面已建；按窗补建只服务第二扇窗之后的入口。
        if (!window.banner) {
            window.banner = make_broadcast_banner(window);
        }
        // 状态栏没有「必须在首标签之前存在」那一层理由（它只被 `refresh_status_bar_ui` 逐帧写），故一律
        // 在这里按窗造；入根由 `rebuild_root_children()` 那一句负责，本处不自己 push，否则两处的次序会
        // 在下一次重建时互相掀掉（那边按 chrome 前缀起步重排）。
        if (!window.status_bar) {
            window.status_bar = std::make_shared<borealis::ui::StatusBar>();
        }
        // 中段行（常驻列 + 工作区）与状态栏同一条理由：入根交给 `rebuild_root_children()`，本处只造本体。
        // 交叉轴拉满高度＝屏 1 那条列从横幅下沿一直走到状态栏上沿（稿面 `y=104..872`）。
        if (!window.content_row) {
            window.content_row = std::make_shared<au::Row>();
            window.content_row->set_cross_axis_alignment(au::CrossAxisAlignment::Stretch);
            window.content_row->set_modifier(au::Modifier{}.expand(1.0F).fill_max_size());
        }
        // 标题栏排最前（屏 1 那条带就是 `y=0..36`，其下才是 `y=36..78` 的标签条）。它与标签栏、横幅同
        // 一档：常驻节点，`rebuild_root_children()` 保留 chrome 前缀那几格而不动它们。
        if (window.title_bar) {
            window.root_column->child_nodes_mut().push_back(aurora::Node{window.title_bar});
        }
        window.root_column->child_nodes_mut().push_back(aurora::Node{window.tab_bar});
        window.root_column->child_nodes_mut().push_back(aurora::Node{window.banner});
        window.overlay_host->child_nodes_mut().push_back(aurora::Node{window.root_column});
        if (make_sidebar) {
            make_sidebar(window);
        }
        rebuild_root_children(window, tab_workspaces);
    };

    // 活动档主题的共享状态（稿 §3 第 1／2 步）：每扇窗的 `ThemeScope` 共用这一枚，换档只 `set()` 一次
    // 就让**已有**窗口在下一帧整体重渲，不必逐窗找 `ThemeScope` 改值。声明排在首扇窗的场景之前，
    // 生命周期随装配层到进程尾；句柄按值复制进各窗的 ThemeScope，副本与原句柄共享同一枚来源，
    // 故换档只需对下面那一枚 `set()`。
    auto chrome_theme_state = aurora::State<aurora::Theme>{borealis::ui::settings_chrome_theme()};
    // 场景根的造法按窗一份（稿 §2）：chrome 主题挂浮层宿主外面那一层，面板与后续界面件读同一份
    // `Theme`，而它**刻意不随终端主题联动**（裁决 7.52 的 S5① / N6：切配色主题时整窗跟着变会让用户
    // 以为丢了设置）。第二扇窗必须拿到**自己**的一枚 Scene：`Application::open_window` 收的是场景本体
    // 而不是引用，两扇窗共用一棵树就是把同一批控件挂到两个宿主上。主题值本身是**共享**的：换档正是
    // 靠各窗 ThemeScope 观察同一枚 `State` 才会一处处同时生效。
    auto make_window_scene = [chrome_theme_state](WorkspaceWindow &window) -> au::Scene {
        auto chrome = std::make_shared<au::ThemeScope>(
            chrome_theme_state,
            aurora::Node{std::static_pointer_cast<au::Widget>(window.overlay_host)});
        // 语言档的注入腿（`SPEC.FEAT.PREF.05` 稿 F19）：绘制侧的 `Text`／`Button` 读的是**环境里的
        // `Locale`**，没有注入就按 `Locale{}`（＝`en`）查表。i18n v2 之后这是**硬要求**而不是保险：
        // 构建期那枚 bin 两档都有本体，缺了注入就是整窗绘制侧文案换成英文，缺省档兜不住这一回
        // （`settings_i18n.h` 文件头④）。
        auto locale_scope = std::make_shared<au::LocaleProvider>(
            borealis::ui::settings_locale(),
            aurora::Node{std::static_pointer_cast<au::Widget>(std::move(chrome))});
        return au::Scene{au::Node{std::static_pointer_cast<au::Widget>(std::move(locale_scope))}};
    };

    // 主窗这几行只在 `app` 建成**之前**读容器首格：那一刻还谈不上任何拆窗，现取即安全。不把它写成
    // 一枚引用变量留给运行期读——批 4-A 的账目就是要让「容器首格」这个事实每次都用时现查。
    assemble_window_content(*workspace_windows.front());
    au::Scene scene = make_window_scene(*workspace_windows.front());
    borealis::session::ClipboardOutbox outbox;

    au::Application app{std::move(scene), workspace_windows.front()->take_window(),
                        workspace_windows.front()->options};
    // 宿主登记之后才有号（`Application` 从 1 起自增，稿 §0 的 F2）：这一枚是本扇窗后续一切「按窗取」
    // 的键，未登记之前它是 `AURORA_INVALID_WINDOW_ID`，于是「拿没建成的窗去取宿主」必然取空。
    workspace_windows.front()->id = app.main_window();

    // 逐窗几何的接线（`SPEC.FEAT.WS.03` 稿 D9／§6 批 5）：主窗是在 `Application` 构造期登记的，那一跳
    // 还没有存储可读，故靠 `set_window_geometry_store` 的第二条入口——它对**已登记**的宿主补做一次恢复
    // （稿 §0 的 F26），此后每扇新窗在 `open_window` 里走同一条腿。这一跳必须排在任何一次布局与出帧
    // 之前：恢复只改 surface 的尺寸与位置，排在首帧之后就变成「先按 960×640 铺一帧、再按恢复值铺一帧」，
    // 而两帧各会推一次网格尺寸（稿 D13 那条「恢复后首帧只下发一次」判据的来由）。
    //
    // 本仓不做的事有两件，都登记在册（稿 §0 的 F26、§21）：① 框架的恢复腿只落 `size` 与 `position`，
    // `WindowGeometry::mode` 存而不取 ⇒ 最大化态回不来，本棒不在应用侧补一份几何态下发（AGENTS.md §5.1
    // 「不长期持有框架分叉」）；② 键写在这份 `Preferences` 的**顶层**而不是 `group("windows")` 里——
    // 自动链读的就是顶层那枚 `persist_id`，写进分组即「存得回而恢复取不到」。
    app.set_window_geometry_store(geometry_store.preferences());

    // 按窗取焦点管理器（稿 §0 的 F3）：`app.focus()` 恒指主宿主，拿它给第二扇窗接线就是「键入打不
    // 进第二扇窗」那条已知形态。取不到宿主时回主窗那一份是**兜底而不是猜测**——号没登记说明这扇窗
    // 还没建成，此刻唯一能用的就是主窗，兜底让其余接线不因一扇没建成的窗整体断掉。
    auto window_focus = [&app](WorkspaceWindow &window) -> au::FocusManager & {
        au::WindowHost *const target = app.window_host(window.id);
        return target != nullptr ? target->focus() : app.focus();
    };

    // ---- 状态栏的折算腿（屏 1 判据 ⑨／⑩，`SPEC.FEAT.PREF.02` 的即时反馈面）----
    //
    // 逐窗各折一份：状态栏读的是「这一扇窗的焦点格」的事实，两扇窗共用一份就是让 B 窗的条上出现
    // A 窗的行列数。每帧重算而**不会每帧标脏**——`StatusBar::set_state` 值相等即空操作，于是这一腿
    // 可以无脑跟着帧走，不必为「哪一处改动该刷新状态栏」再立一张账（与逐窗标题差分同一条思路：
    // 每帧现算，只在变了才下发）。
    //
    // 三个刻意取舍：
    // ① 全部读点**无锁**。编码取 `Session::encoding_name()`（构造期写定、指向静态存储期），格数取
    //    视口的 `grid_geometry()`（布局侧本帧算好的那一份），其余取 `settings`——一处都不碰
    //    `Session::read()` 那条临界区。状态栏是每帧都折的面，挂在会话锁上就是拿 §4.5 第 25 条换读数。
    // ② 十枚开关里今日只有七枚有取处；`reconnect`／`cursor_position`／`input_latency` 三枚交空文本
    //    即不上屏，理由逐条写在 `status_bar_registration_order()` 的注释里（判据 ⑧ 把断线与重连画在
    //    pane 上，不是画在这条上）。三枚的**位置仍留在注册次序里**，窄窗时的省略序因此不随数据源漂。
    // ③ 锚点的键位取注册表那一份而不是在此重述 `Ctrl+Shift+P`：用户改过键位后这条要跟着说真话，而
    //    框架命令面板显示的也是同一枚 `default_binding`——两个投影不各持一份真值。
    auto refresh_status_bar_ui = [&]() -> void {
        const borealis::config::StatusBarSettings &bar = settings.appearance.status_bar;
        const auto &order = borealis::ui::status_bar_registration_order();
        std::string palette_combo;
        if (const au::Command *const command = app.commands().find("command_palette.open");
            command != nullptr && command->default_binding.has_value()) {
            palette_combo = command->default_binding->to_string();
        }

        for (const std::unique_ptr<WorkspaceWindow> &window_ptr : workspace_windows) {
            WorkspaceWindow &window = *window_ptr;
            if (!window.status_bar) {
                continue;  // 还没装配内容的窗（与横幅刷新同一道闸）
            }
            // 焦点格：本窗当前标签的 `focused_pane()`。当前号取不到、或那一格属于别的窗（多窗下的
            // 常态）都只折全局那几格——会话级读数宁缺勿错，读别窗的行列比读不到更容易误导人。
            borealis::session::Session *session = nullptr;
            const borealis::ui::TerminalView *view = nullptr;
            std::optional<borealis::ui::TabId> tab;
            if (window.current_tab_id.has_value()) {
                const auto found = tab_workspaces.find(window.current_tab_id.value());
                if (found != tab_workspaces.end() && found->second.owner == &window) {
                    tab = window.current_tab_id;
                    const borealis::ui::PaneId pane = found->second.workspace->focused_pane();
                    const auto held = found->second.sessions_by_pane.find(pane);
                    if (held != found->second.sessions_by_pane.end()) {
                        session = held->second.get();
                    }
                    view = found->second.workspace->view_of(pane);
                }
            }

            borealis::ui::StatusBarState state;
            // 一格一调用，形参次序＝`status_bar_registration_order()` 的下标：省略序由那一份注册表
            // 决定，而这里写下的下标只决定「哪一枚开关管这一格」。开关关掉、或文本为空，都不上屏。
            auto push = [&state, &order](const std::size_t index, const bool on, std::u32string text,
                                         const borealis::ui::StatusBarTone tone,
                                         const bool leading_dot = false,
                                         const bool leading_separator = false) -> void {
                if (!on || text.empty() || index >= order.size()) {
                    return;
                }
                state.slots.push_back(borealis::ui::StatusBarSlot{
                    .id = order[index],
                    .text = std::move(text),
                    .tone = tone,
                    .leading_dot = leading_dot,
                    .leading_separator = leading_separator,
                });
            };
            // label(key, args) → 码点串：词条表是唯一文案来源（AGENTS §4.3 第 14 条），状态栏上屏的
            // 每一个字面短语都从这儿过。
            auto phrase = [&decode_utf8](const char *key,
                                         std::vector<aurora::MessageArg> args = {}) -> std::u32string {
                return decode_utf8(borealis::ui::settings_label(key, std::move(args)));
            };

            // ① 连接态（稿面「已连接 root@web-01:22」＋一枚 `success` 圆点）。目标串的口径照
            //    `SSH user@host:port`；串口只有设备名（波特率那些档在监视卡上，判据 ⑨ 没点到这条）；
            //    本地腿没有目标，交档案类型名「本地终端」而不是自造一句「本机 shell」。
            //    只在会话**活着**时报「已连接」：断线与重连按判据 ⑧ 归 pane 那一腿，稿面在这条上
            //    没画过断开态，本件因此不备 danger 档的点。
            std::string target;
            if (session != nullptr && session->alive() && tab.has_value()) {
                const auto kind = tab_kind.find(tab.value());
                if (kind != tab_kind.end()) {
                    switch (kind->second) {
                        case borealis::conn::ConnectionType::Local:
                            target = borealis::ui::settings_label("connections.type.local");
                            break;
                        case borealis::conn::ConnectionType::Ssh: {
                            const auto held = ssh_profile_by_tab.find(tab.value());
                            if (held != ssh_profile_by_tab.end()) {
                                const borealis::conn::SshProfile &profile = held->second;
                                target = profile.user.empty() ? profile.host : profile.user + "@" + profile.host;
                                target += ":" + std::to_string(profile.port);
                            }
                            break;
                        }
                        case borealis::conn::ConnectionType::Serial: {
                            const auto held = serial_by_tab.find(tab.value());
                            if (held != serial_by_tab.end()) {
                                target = held->second.profile.device_path;
                            }
                            break;
                        }
                        case borealis::conn::ConnectionType::Telnet: {
                            const auto held = telnet_by_tab.find(tab.value());
                            if (held != telnet_by_tab.end()) {
                                target = held->second.host + ":" + std::to_string(held->second.port);
                            }
                            break;
                        }
                    }
                }
            }
            push(0U, bar.show_connection,
                 target.empty() ? std::u32string{} : phrase("status_bar.connection", {{"target", target}}),
                 borealis::ui::StatusBarTone::Dim, /*leading_dot=*/true);

            // ③ 编码：取会话的**生效腿**而不是配置里那一名（`SPEC.FEAT.TERM.09` 的 BOM／平台缺省
            //    折算只发生在会话里），稿面这一格就是个裸名字，不带前缀、不进词条表。
            push(3U, bar.show_encoding,
                 session == nullptr ? std::u32string{}
                                    : decode_utf8(std::string(session->encoding_name())),
                 borealis::ui::StatusBarTone::Faint);

            // ④ 行列数（稿面「82×24」）：视口本帧算好的那份几何，0 行 0 列（窗最小化）不上屏。
            if (bar.show_grid_size && view != nullptr) {
                const std::size_t columns = view->grid_geometry().columns;
                const std::size_t rows = view->grid_geometry().rows;
                if (columns > 0U && rows > 0U) {
                    push(4U, true,
                         phrase("status_bar.grid_size", {{"cols", static_cast<std::int64_t>(columns)},
                                                         {"rows", static_cast<std::int64_t>(rows)}}),
                         borealis::ui::StatusBarTone::Faint);
                }
            }

            // ⑤⑥⑦ 三格取自配置：字号／终端主题名／scrollback 容量（稿面「14 pt」「主题 Dracula」
            //     「scrollback 10,000」——千分位是稿面形态，由 `group_thousands` 折，不是词条）。
            push(5U, bar.show_font_size,
                 phrase("status_bar.font_size",
                        {{"size", borealis::ui::format_font_size(settings.appearance.font_size_pt)}}),
                 borealis::ui::StatusBarTone::Faint);
            push(6U, bar.show_theme, phrase("status_bar.theme", {{"name", settings.appearance.theme}}),
                 borealis::ui::StatusBarTone::Faint);
            push(7U, bar.show_scrollback,
                 phrase("status_bar.scrollback",
                        {{"rows", borealis::ui::group_thousands(settings.terminal.scrollback_limit)}}),
                 borealis::ui::StatusBarTone::Faint);

            // ⑧ 剪贴板授权三态（`SPEC.FEAT.CONN.12`）：稿面在它前面画了一条 `card_line` 分隔线，把
            //    「一条授权策略」与前面那串读数分开；黄色只给**禁止**那一档（稿面画的就是它），
            //    允许与询问是常态，跟着变黄会让这条永远在叫。
            std::string_view policy_key = "status_bar.clipboard.deny";
            if (settings.terminal.clipboard_read == borealis::term::ClipboardReadPolicy::Allow) {
                policy_key = "status_bar.clipboard.allow";
            } else if (settings.terminal.clipboard_read == borealis::term::ClipboardReadPolicy::Ask) {
                policy_key = "status_bar.clipboard.ask";
            }
            push(8U, bar.show_clipboard_policy,
                 phrase("status_bar.clipboard_policy", {{"policy", borealis::ui::settings_label(policy_key)}}),
                 settings.terminal.clipboard_read == borealis::term::ClipboardReadPolicy::Deny
                     ? borealis::ui::StatusBarTone::Warning
                     : borealis::ui::StatusBarTone::Faint,
                 /*leading_dot=*/false, /*leading_separator=*/true);

            // ⑩ 命令面板锚点（右端固定，稿面 `accent` 色）：没有键位读数即整格不占位——`plan` 那边
            //      reserved 的宽度也就跟着归零，不会为一句画不出来的提示扣掉一格。
            if (!palette_combo.empty()) {
                state.anchor =
                    phrase("status_bar.palette_hint", {{"combo", palette_combo}});
            }
            window.status_bar->set_state(std::move(state));
        }
    };

    // 帘子的装配腿（`SPEC.FEAT.WS.12`，裁决 7.172）：状态机 → 框架那三口的唯一出口。
    // 状态与意图的账全在 `QuakeToggle`（只有它认得「这次失焦是不是自己刚造的」），本跳只把意图翻成
    // OS 调用。几何**每次 Show 现算**：贴边与两向占比是即时生效档，改完下一次呼出就该按新值落位。
    apply_quake_intent = [&settings, &workspace_windows](const borealis::ui::QuakeIntent intent) -> void {
        WorkspaceWindow *const window = workspace_windows.empty() ? nullptr : workspace_windows.front().get();
        au::Surface *const surface = window == nullptr ? nullptr : window->surface;
        if (surface == nullptr) {
            return;  // 帘子窗已被关掉：没有可显隐的对象；状态机那侧的账由下一次热键自然纠正。
        }
        if (intent == borealis::ui::QuakeIntent::Hide) {
            surface->set_visible(false);
            return;
        }
        if (intent != borealis::ui::QuakeIntent::Show) {
            return;
        }
        const borealis::config::QuickTerminalSettings &quick = settings.ui.quick_terminal;
        // 主屏工作区（已扣任务栏，物理像素）：帘子只贴这一块，副屏不参与。
        const au::Display display = au::app::primary_display();
        const borealis::ui::QuakeTarget target = borealis::ui::quake_target_rect(borealis::ui::QuakeGeometryRequest{
            .work_area = borealis::ui::QuakeWorkArea{.origin_x = display.work_area.origin.x,
                                                     .origin_y = display.work_area.origin.y,
                                                     .width = display.work_area.size.width,
                                                     .height = display.work_area.size.height},
            .scale_factor = display.scale_factor,
            .anchor = quick.anchor,
            .cross_ratio = quick.cross_ratio,
            .along_ratio = quick.along_ratio,
        });
        // 先定几何再显示：反序会先以建窗尺寸露一帧、再被 SetWindowPos 挪走。两个单位故意不同——原点
        // 物理 px、尺寸逻辑 dp（框架那两口的实现口径，见 `QuakeTarget` 的注释）。
        surface->set_position(au::Point{.x = static_cast<float>(target.origin_x),
                                        .y = static_cast<float>(target.origin_y)});
        surface->set_size(au::Size{.width = static_cast<float>(target.width_dp),
                                   .height = static_cast<float>(target.height_dp)});
        surface->set_always_on_top(true);
        surface->set_visible(true);
        // 显示档刻意不抢焦点（`Surface::set_visible` 头注释）：呼出要能接管键入，必须显式补这一口。
        surface->focus_window();
        if (!surface->is_visible()) {
            // 会话策略挡下（如非交互桌面）：如实留痕，不把「没显示出来」记成已呼出。
            AURORA_LOG_WARN("main", "quick terminal show did not take effect; window stayed hidden");
        }
    };
    if (quake_enabled) {
        // 失焦即隐（`auto_hide`）：框架把「可见但失焦」归成 `Occluded` 并按**转移**上报，`Hidden` 是
        // 隐藏/最小化而不是失焦，故不在此列。自己发起的那次显隐所引出的 `Occluded`（`SW_SHOWNA` 不激活，
        // 状态从 `Hidden` 落到 `Occluded`）由状态机的过渡窗吞掉——那条竞态正是 `QuakeToggle` 的存在理由。
        app.set_on_window_state([&quake_toggle, &apply_quake_intent](const au::WindowState state) -> void {
            if (state == au::WindowState::Occluded) {
                if (apply_quake_intent) {
                    apply_quake_intent(quake_toggle.on_focus_lost(borealis::ui::QuakeToggle::Clock::now()));
                }
            }
        });
    }

    // 「命令该落哪一扇窗」的记录器（`SPEC.FEAT.WS.03` 稿 D7）：框架既不给活动窗查询，也不把派发来源
    // 带进快捷键闭包（稿 §0 的 F4／F5、稿 §3 的 G42），故本仓自持这一枚最近记录器，落点由**事实**决定。
    borealis::ui::ActiveWindowTracker active_window_tracker{app.main_window()};
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
    // 六类闭包（`search.open`／`quick_select.open`／`session.restart`／`serial.set_baud`／
    // `workspace.toggle_fullscreen`／`tab.undo_close`）的落点：tracker 无记录时回的是**主窗号**
    //（构造期那枚兜底），故正常路径恒有一扇窗；
    // 只有主窗自己也没了才回空值，调用方按「无事发生」处理。
    auto target_window = [&active_window_tracker, &window_of]() -> WorkspaceWindow * {
        return window_of(active_window_tracker.active());
    };
    // 命令与快捷键的落点腿就位（稿 D7）：上面那些更早建好的闭包（设置页广播、四张连接表的写入、会话
    // 日志、CLI 转交）按引用捕这一枚 `std::function`，调用那一刻读到的永远是最新那一份。取不到窗的
    // 形态**只剩一种**（tracker 的兜底号对应的主窗自己已散），此时按「无事发生」处理而不是挑一扇活着的
    // 窗——「用户没碰过的那一扇被改了」比「这一条没生效」更难解释（上面 `window_of` 那条理由同一条）。
    command_window = [&target_window]() -> WorkspaceWindow * { return target_window(); };
    // 落点窗的**当前标签号**（批 4-A 剩下的那些读取）：SFTP 三条钩子、会话日志、CLI 的 `split-pane`
    // 都读这一支。取不到窗即「没有落点」而回空号，调用方本来就有「无当前标签」那一档分支，形态不变；
    // 它替代的是原来钉在主窗上的那枚 `current_tab_id` 别名——留着就是把「A 窗的当前标签」当成全局事实。
    auto command_tab_id = [&command_window]() -> std::optional<borealis::ui::TabId> {
        WorkspaceWindow *const window = command_window ? command_window() : nullptr;
        return window == nullptr ? std::nullopt : window->current_tab_id;
    };

    // 面板与常驻浮层的落窗腿（稿 D6 的推荐档：app 级一份、浮层挂**发起命令的那一扇窗**）。两条腿共用这一
    // 处的六个入口——打开腿（命令闭包在 open/toggle 之前先搬）与批 4-B 的撤账腿（散掉那一扇名下的浮层搬到
    // 活着的落点）。三条口径：
    //   ① 取不到落点窗即**原地不动**：面板继续挂在它现在那一扇，而不是猜一扇活着的窗——「把用户没碰过的
    //      那一扇改得看得见」比「这一条没生效」更难解释（上面 `command_window` 那条理由同一条）；
    //   ② 真值源在各件自己记的那枚宿主指针（`overlay_host()`），装配层不另立一张「面板在哪扇窗」的表，
    //      那是第二份账、必然与摘挂时机分叉；
    //   ③ 摘与挂的次序归各件自己的 setter（它按记下的序号从**旧宿主**摘回），本支只决定「搬到哪」。
    // 宿主指针 → 本仓那份窗：把「某件现在挂在哪」换算回「哪一扇」的唯一地方（容器才同时有宿主与窗）。
    // 取不到即那枚宿主属于一扇已经散掉的窗（撤账腿还没跑到），调用方按空落点处理。
    auto window_of_host = [&workspace_windows](const au::OverlayHost *mounted) -> WorkspaceWindow * {
        if (mounted == nullptr) {
            return nullptr;
        }
        for (const std::unique_ptr<WorkspaceWindow> &candidate : workspace_windows) {
            if (candidate->overlay_host.get() == mounted) {
                return candidate.get();
            }
        }
        return nullptr;
    };
    auto retarget_panel_to = [](auto &panel, WorkspaceWindow *window) -> void {
        if (window == nullptr) {
            return;
        }
        panel.set_overlay_host(*window->overlay_host);
    };
    auto retarget_panel = [&command_window, &retarget_panel_to](auto &panel) -> void {
        retarget_panel_to(panel, command_window ? command_window() : nullptr);
    };
    // 两只「浮层常驻、开合只切显隐」的框另走一条腿：焦点权威必须与宿主一起换（稿 §0 的 F3——压进哪一扇窗
    // 的焦点栈就由哪一扇窗弹出），单换宿主会把作用域留在已经散掉的那一栈里。
    auto retarget_destination_to = [&window_focus](auto &panel, WorkspaceWindow *window) -> void {
        if (window == nullptr) {
            return;
        }
        panel.set_overlay_destination(*window->overlay_host, window_focus(*window));
    };
    auto retarget_destination = [&command_window, &retarget_destination_to](auto &panel) -> void {
        retarget_destination_to(panel, command_window ? command_window() : nullptr);
    };
    // 散窗那一侧的对应腿（撤账链的 ③′）：与各件那条 `set_overlay_destination` 的差别只在方法本身——旧窗的
    // 焦点管理器已被框架回收（`06-app-platform.md` §3「宿主被回收**之后**触发」），摘与弹都没有落点，
    // 只能清零认下新落点。焦点权威同样按窗取，故这一支也要经 `window_focus`。
    auto abandon_destination_to = [&window_focus](auto &target, WorkspaceWindow *window) -> void {
        if (window == nullptr) {
            return;
        }
        target.abandon_overlay_destination(*window->overlay_host, window_focus(*window));
    };
    // 装配层直接挂进宿主的三只常驻浮层（命令面板、串口波特率框、会话日志框）的搬窗账目：记「挂在谁那儿 +
    // 那一层的序号」。序号在重挂后**必须重取**——`remove_overlay` 是按当前子节点表 erase 的，摘掉一层会让
    // 它后面的序号整体前移。宿主没有基础内容时 `add_overlay` 回空（框架 G29 的口径），而本仓每扇窗入根前
    // 已经放上那层根 Column，故这一档在这三条腿上不可达。
    struct MountedOverlay {
        au::OverlayHost *on = nullptr;       ///< nullptr＝还没挂过（首次 `remount_overlay` 即建挂载）。
        std::optional<std::size_t> index{};  ///< 挂上之后的序号（重挂要重取）。
    };
    auto remount_overlay = [](const auto &widget, MountedOverlay &state, au::OverlayHost &target) -> void {
        if (state.on == &target) {
            // 已经挂在发起窗上：常驻浮层的形态就是「挂一次、只切显隐」，不重复摘挂。
            return;
        }
        if (state.index.has_value() && state.on != nullptr) {
            state.on->remove_overlay(*state.index);
        }
        state.on = &target;
        state.index = target.add_overlay(au::Node{std::static_pointer_cast<au::Widget>(widget)});
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

    // 新开一扇工作区窗（`SPEC.FEAT.WS.03` 批 3，稿 D1／D2）：地基与内容两段**全部复用主窗那
    // 一套定义**（`open_workspace_window()` → `assemble_window_content()`），于是第二扇窗不是主窗的
    // 特例复制，主窗那条路径也第一次有了第二个调用点。次序是承重的三处：
    // ① 先入根再折场景——`Scene` 收的是浮层宿主外面那一层的**所有权**，根 Column 必须已经在树里；
    // ② 号要等 `open_window()` 返回才有效，而首标签那一只视口要按号取焦点管理器（`window_focus`），
    //    建标签排在登记之前就会把键入打进主窗那一份焦点（稿 §0 的 F3）；
    // ③ 入容器排在 `take_window()` 之后、用 `window` 引用之前——容器是 `unique_ptr` 的 vector，搬走的
    //    是指针而不是对象，堆上那一格的地址在窗的生命周期内不动（故返回的裸指针在窗关掉之前有效）。
    // 首标签不在这一段：`Ctrl+Shift+N` 要的是 D1 那一格缺省本地标签，而 CLI `--new-window` 要的那一格
    // 吃 `--profile` / `--cwd`（稿 D10），两档共用同一条开窗腿、各自决定料，故拆成下面两支。
    open_new_window = [&app, &workspace_windows, &assemble_window_content, &make_window_scene,
                       &next_window_index]() -> WorkspaceWindow * {
        auto created = open_workspace_window("Borealis", next_window_index++);
        if (created == nullptr) {
            // 建窗失败原文已在工厂里落日志。这里不入容器、不登记宿主，于是稿 §5 那条「没建成窗就不
            // 留下半个账目」在第二扇窗这条路上同样成立。
            return nullptr;
        }
        WorkspaceWindow &window = *created;
        assemble_window_content(window);
        window.id = app.open_window(window.take_window(), make_window_scene(window), window.options);
        workspace_windows.push_back(std::move(created));
        return &window;
    };

    open_new_window_request = [&open_new_window, &create_new_tab, &tab_workspaces,
                               &rebuild_root_children]() -> void {
        WorkspaceWindow *const window = open_new_window ? open_new_window() : nullptr;
        if (window == nullptr) {
            return;
        }
        // 首标签是一个**缺省本地**标签（D1）：新窗此刻没有当前标签，`create_new_tab` 的继承腿因此自动
        // 落在配置的启动目录那一档，不继承发起窗的 OSC 7——「新窗跟着旧窗的目录走」是另一件尚未
        // 裁决的事，不在本条里猜。
        create_new_tab(*window, std::nullopt);
        rebuild_root_children(*window, tab_workspaces);
    };

    // 设置页自己那份句柄留给下面两条腿现读：`hooks.preview_wake` 要叫醒的是**面板此刻挂着的那一扇**，
    // 而面板是 app 级单实例、宿主会随搬窗变（稿 D6）。与 `wizard_ref` 同一形态——先声明闭包、构造完再
    // 赋值，钩子跑起来时读到的永远是那一枚已经建好的面板。
    borealis::ui::SettingsPanel *settings_panel = nullptr;
    // 换肤的唯一执行腿（`SPEC.FEAT.PREF.01` 稿 §3 第 4 条）：面板保存后的广播与下面那条 OS 变更通知都
    // 收口到这里。「同值不动作」的短路是判据的一部分而不只是省一次重绘：框架那条通知只在值实际变化时
    // 被调，但用户在设置面板里把档位绕回同一档（`system` → `light` → `system`）也会算出同一个解算结果，
    // 不短路就会在静止期反复 `set()` 主题、反复叫醒全部窗口。
    auto apply_chrome_scheme = [&app, &chrome_theme_state, &workspace_windows, &settings_panel](
                                   borealis::ui::ChromeScheme scheme) -> void {
        if (scheme == borealis::ui::chrome_scheme()) {
            return;
        }
        borealis::ui::set_chrome_scheme(scheme);
        chrome_theme_state.set(borealis::ui::chrome_theme(scheme));
        // 逐窗叫醒：`Application::request_wake_all()` 是私有腿（公共面只有各窗自己的 surface）。
        // 主题变化已把子树标脏，这里补的是「帧循环正睡在某个 surface 上、等不到下一次事件」那一档；
        // OS 那条通知跑在平台消息泵里、那一帧本就醒了，需要这段的是来自点击的广播腿——两者共用一条腿，
        // 叫醒对前者只是空操作。
        for (const std::unique_ptr<WorkspaceWindow> &window : workspace_windows) {
            if (window->surface != nullptr) {
                window->surface->request_wake();
            }
        }
        // 面板的控件色值构建期取定，换档必须重建浮层才换色；但这条腿可能正跑在面板自己的按钮回调里
        // （保存即换档），当场 `remove_overlay()` 会撤掉**正在派发**的控件（G34 那一族时序），故排到
        // 下一帧调度器。面板届时已被用户关掉时 `refresh_chrome()` 自己空操作。
        static_cast<void>(app.scheduler().set_timeout(
            au::Scheduler::Duration::zero(),
            [&settings_panel, &workspace_windows]() -> void {
                if (settings_panel != nullptr) {
                    settings_panel->refresh_chrome();
                }
                // 常驻列同一档：展开态那些控件也是构建期取定的色值，要重建那一列才换色；折叠态那枚轨
                // 每次重绘现读色板，`refresh_chrome()` 在那里自己空操作。
                for (const std::unique_ptr<WorkspaceWindow> &window : workspace_windows) {
                    if (window->sidebar != nullptr) {
                        window->sidebar->refresh_chrome();
                    }
                }
            }));
    };
    // 跟随系统的运行期腿（`SPEC.FEAT.PREF.01`；缺口 G45 回货后的形态，裁决 **7.170**）：框架在 Win32
    // `WM_SETTINGCHANGE` 里重读偏好、逐宿主上报，并在应用级**去重成一次**通知（同值不重复、只在主线程被调）。
    // `refresh_os_color_scheme()` 是先显式 seed：构造期不隐式读注册表是框架口径，没收过系统上报的进程里
    // 那枚 State 恒读 `Unspecified`，而上面那次启动解算走的是自由函数、不经这枚 State——不 seed 就有两份
    // 读数的起点。seed 排在注册回调**之前**，因为启动期那一次读数不该被表达成「系统刚刚切换」。
    app.refresh_os_color_scheme();
    app.set_on_os_color_scheme([&store, &apply_chrome_scheme](const aurora::ColorScheme os) -> void {
        // 只有意图是「跟随系统」时才动屏面：用户显式选了亮/暗，OS 偏好就只是一份与屏面无关的读数（稿 §3
        // 第 3 条的三级优先链）。这里吃**回调给的 OS 原值**而不是回头再读一次——回落哪一档由
        // `resolve_chrome_scheme()` 判，装配层不再自己读注册表造第二个真值源。
        const borealis::config::ChromeTheme choice = store.settings().appearance.chrome_theme;
        if (choice != borealis::config::ChromeTheme::System) {
            return;
        }
        apply_chrome_scheme(borealis::ui::resolve_chrome_scheme(
            borealis::config::chrome_theme_tag(choice), os));
    });
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
    hooks.broadcast = [&tab_workspaces, &workspace_windows, &store, &catalog, &quake_toggle,
                       &apply_chrome_scheme](const borealis::ui::SettingsForm &form) -> void {
        const borealis::config::Settings next = borealis::config::apply_form(form, store.settings());
        // 帘子的失焦开关（即时生效）：状态机是这一档的唯一持有者。贴边与两向占比不必在这里转发——
        // 它们由 Show 分支每次现读 `store`，转发一份反而成了第二处真值源。
        quake_toggle.set_auto_hide_on_focus_lost(next.ui.quick_terminal.auto_hide);
        // 外壳明暗档先落地（稿 §3 第 2 步）：本函数跑在面板按钮回调里，换肤只改活动档与共享主题状态，
        // 面板自身的重建已由 `apply_chrome_scheme` 排到下一帧，这里不必也不该重入面板。
        apply_chrome_scheme(borealis::ui::resolve_chrome_scheme(
            borealis::config::chrome_theme_tag(next.appearance.chrome_theme), aurora::os_color_scheme()));
        // 常驻列的开合档（`appearance.sidebar_collapsed`，接线见裁决 **7.180**）：设置页那条腿的语义是
        // 「改的是全局配置」，故按即时档作用到**每一扇窗**的那一条列——与配色同一条口径，只改落点窗会把
        // 「全局开关」在交互面上降级成「本窗开关」。轨上或命令上的那一次点击走的是另一条腿（只改本窗、
        // 记住全局），两条腿都收在 `set_collapsed()`，它按当前档空操作，因此不会互相掀。
        for (const std::unique_ptr<WorkspaceWindow> &window : workspace_windows) {
            if (window->sidebar != nullptr) {
                window->sidebar->set_collapsed(next.appearance.sidebar_collapsed);
            }
        }
        // 广播到**每一扇窗的当前标签**的所有视口（一格一会话之后仍是「工作区里可能有多个 pane」）。
        // 为什么按窗而不是按落点窗：设置页是应用级单实例（稿 D6），这一条腿的语义是「刚落盘的配置即时
        // 生效」，而生效面天然是全部可见的工作现场——把它按 tracker 解析成「最近被碰过的那一扇」就变成
        // 「在 B 窗改设置，A 窗还停在旧配色」，那是另一件没裁决的事。主窗别名在这里换成现取容器：
        // 关窗那一棒会把首格取走（批 4-A 的理由）。
        for (const std::unique_ptr<WorkspaceWindow> &window_ptr : workspace_windows) {
            const std::optional<borealis::ui::TabId> &target = window_ptr->current_tab_id;
            if (!target.has_value()) {
                continue;
            }
            auto it = tab_workspaces.find(target.value());
            if (it == tab_workspaces.end()) {
                continue;
            }
            // 遍历该工作区的所有 pane 视图并应用新外观
            for (borealis::ui::PaneId pane = 1; pane <= it->second.workspace->pane_count(); ++pane) {
                auto *v = it->second.workspace->view_of(pane);
                if (v) {
                    v->apply_appearance(make_appearance(next, catalog));
                    v->apply_interaction_options(make_interaction(next));
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
    // 唤醒腿按**面板现在挂着的窗**取 surface（稿 D6 的搬窗腿之后，面板挂哪一扇是可变事实，真值源在面板
    // 自己记的那枚宿主指针）：预览浮层就在那一扇的宿主里，叫醒别的窗只会让别的窗多画一帧，而这一帧该排的
    // 横条脏仍留在设置页那一扇的队列里。对不上号即「面板挂在一扇已经散掉的窗上」——那是撤账腿还没跑到的
    // 窗口，退回 `fallback_window()` 而不是不醒：夹具重投的脏总要有一扇窗来排。
    hooks.preview_wake = [&settings_panel, &window_of_host, &fallback_window]() -> void {
        WorkspaceWindow *window = window_of_host(settings_panel != nullptr ? settings_panel->overlay_host()
                                                                           : nullptr);
        if (window == nullptr) {
            window = fallback_window();
        }
        if (window != nullptr && window->surface != nullptr) {
            window->surface->request_wake();
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
    hooks.pick_export_path = []() -> std::string {
        // 设置导出交一份固定名（G48）：这份内容本身就是「当前配置的全部」，没有比它自己更准的名字。
        return picked_save_path("Export settings", {{"JSON", {"*.json"}}},
                                "borealis-settings.json");
    };
    hooks.pick_import_path = []() -> std::string { return picked_open_path(); };
    borealis::ui::SettingsPanel panel{*host, app.shortcuts(), std::move(hooks)};
    settings_panel = &panel;

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

    // 连接复用的共享表（CONN.02，裁决 7.147 / 稿 D6、D11）：装配层建**一枚**，六条 SSH 腿都经
    // `make_ssh_session` 取它。表只持弱引用，活下来的共享会话由适配器们强持有；键不含档案 id，
    // 同一主机+用户+认证+秘密的两格因此自然共线。
    auto ssh_registry = std::make_shared<borealis::conn::SshSessionRegistry>();

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
                                        borealis::term::LineEnding::Lf,
                                    std::optional<borealis::conn::LocalTerminalSpec> local_spec =
                                        std::nullopt) -> void {
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
        // 有效料排在换视口**成功之后**：回滚那一条把会话还给了旧的那份，此时若已经把新料写进账，
        // 下一次采集就会给一格已死的会话写一份它从没起过的命令（只有本地腿交这一枚，其余三腿留空值）。
        if (local_spec.has_value()) {
            found->second.local_specs_by_pane[pane] = *local_spec;
        }
    };

    /// @brief SSH 腿的会话构造（新开标签 / 重启换会话 / 切分新格三处共用的那一段）。
    ///
    /// 每次一份宽度表：这张表被写侧与折算侧共享，跨会话共用就是让两条连接互相污染列宽缓存（同本地腿
    /// 那一条理由）。`bundle`（目标 + 逐跳材料）照旧移交给 `SshConnection`，调用方要留档就自己先复制一份。
    /// 末位交共享表：生效参数（含跳链形状）与秘密都相同的两格落在同一条 TCP 上，各自一条 shell 通道。
    auto make_ssh_session = [&](const borealis::conn::SshProfile &ssh,
                                borealis::conn::SshSecretBundle bundle)
        -> std::shared_ptr<borealis::session::Session> {
        auto connection = std::make_unique<borealis::conn::SshConnection>(
            ssh, std::move(bundle), kNominalViewport, reconnect_policy(),
            borealis::conn::SshAlgorithmOverrides{settings.connection.ssh.kex_algorithms,
                                                  settings.connection.ssh.host_key_algorithms},
            ssh_registry);
        auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();
        return std::make_shared<borealis::session::Session>(
            std::move(connection), kNominalViewport, settings.terminal.scrollback_limit,
            width_policy, make_terminal_defaults(settings), make_session_encoding(settings));
    };

    /// @brief 逐跳材料的补齐接缝（`SPEC.FEAT.CONN.03` 稿 D8 + 批 7 D1/D2）：目标材料已到手 ⇒
    ///        补齐 hops ⇒ 交回 bundle；`done` 收 `nullopt`＝某一询问跳被用户取消，整次取料作罢。
    ///
    /// 声明在这里（而不是在 `credential_store` 之后就地定义）是因为六条腿的装配点都**先于**凭据库本体；
    /// 实现体在凭据库到位那一处写入，形态同下面的 `restore_secret_sink`（同一处先例）。
    /// 跳链为空时实现体同步回调；有跳且至少一跳是 `Stored` 时走后台任务（钥匙环可能弹解锁框并阻塞，
    /// UI 线程不许等，同目标那条腿的 D8 理由），回调仍落在主线程。`Ask` 的那几跳由实现体内的
    /// 串问泵经 `ask_secret` 逐跳询问——四个消费者因此**免费**获得逐跳问句，各自的取消支见调用点。
    /// `profile_id` 只用于答齐后的乐观回存（批 8）：空串＝临时档案（quick connect），键位整表为空、
    /// 泵只搬答案不落盘。
    auto with_hop_secrets = std::function<void(const borealis::conn::SshProfile &,
                                               const std::string &profile_id,
                                               std::optional<std::string>,
                                               std::function<void(std::optional<
                                                   borealis::conn::SshSecretBundle>)>)>{};

    /// @brief SSH 腿的切分工厂：同一份档案 + 留档材料再起一格（稿 §4 的 F13）。
    ///
    /// 档案与材料都从两张表里**现读**，不在闭包里留副本：`on_secret` 那一腿会把询问到的材料改写进
    /// `ssh_secret_by_tab`，工厂拿副本就会重拨成过期的那份材料。表里没有档案身份即回空值——正常路径
    /// 不会走到（登记与建标签同批发生），留这一条是 fail-closed 而非第二重判据。
    /// 连接复用后这里不再必然新拨一条 TCP：档案与材料相同的那条链路还在（含同一条跳链），切分格挂上去即可。
    auto make_ssh_pane_factory = [&](borealis::ui::TabId id) -> PaneSessionFactory {
        return [&ssh_profile_by_tab, &ssh_secret_by_tab, &make_ssh_session, id](
                   borealis::ui::PaneId /*origin*/) -> std::optional<PaneBundle> {
            const auto profile = ssh_profile_by_tab.find(id);
            if (profile == ssh_profile_by_tab.end()) {
                AURORA_LOG_WARN("main", "split refused: tab ", id, " carries no SSH profile on file");
                return std::nullopt;
            }
            borealis::conn::SshSecretBundle bundle;
            if (const auto held = ssh_secret_by_tab.find(id); held != ssh_secret_by_tab.end()) {
                bundle = held->second;
            }
            return PaneBundle{.session = make_ssh_session(profile->second, std::move(bundle))};
        };
    };

    // 按档案重开一格 SSH 会话（裁决 7.99 D6）：新开一格是另一个意图，故这里只覆盖既有那一格。
    // 标签身份**不清账**——这一格仍是那张档案的会话，SFTP 面板与「重启」的分流判据都按它取料。
    // 逐跳材料沿用**这一格已有的那份**（稿 D8：重启不该另起一条取料链，也不该把已经答完的串问再问
    // 一遍；留档里没有跳板材料时那一跳在拨号侧落 `Auth`，看得见也解释得通）。
    auto restart_ssh_tab = [&](borealis::ui::TabId id, const borealis::conn::SshProfile &ssh,
                               std::optional<std::string> secret) -> void {
        auto bundle = borealis::conn::SshSecretBundle{std::move(secret)};
        if (const auto held = ssh_secret_by_tab.find(id); held != ssh_secret_by_tab.end()) {
            bundle.hops = held->second.hops;
        }
        auto retained = bundle;  // 留档副本先取，bundle 那份随后移交。
        auto session = make_ssh_session(ssh, std::move(bundle));
        ssh_secret_by_tab[id] = std::move(retained);
        replace_pane_session(id, std::move(session));
        AURORA_LOG_INFO("main", "restart: tab ", id, " re-dialled by profile");
    };

    // 新开一格的落点窗（稿 D7，批 4-A 收到这一条腿）：档案连接（侧栏、向导、CLI 转交）此前恒钉主窗
    // 别名，多窗下就是「在 B 窗点档案，标签开进了 A 窗」。取的是「最近被碰过的那一扇」，取不到只剩
    // 一种形态（主窗自己已散），当场记一条 WARN 并什么都不做——把新标签挂进用户没碰过的那一扇更难解释。
    auto tab_host_or_none = [&command_window](std::string_view leg) -> WorkspaceWindow * {
        WorkspaceWindow *const window = command_window ? command_window() : nullptr;
        if (window == nullptr) {
            AURORA_LOG_WARN("main", leg, ": refused, there is no window to host the new tab");
        }
        return window;
    };

    // 同一档案的取料在途去重：连点两下只发一次后台 retrieve，结果回来再合并（D8）。
    // 声明在 `open_ssh_tab` **之前**：跳链那份逐跳取料（CONN.03 批 4）也在开标签那一步里用它。
    auto pending_credential_fetches = std::make_shared<std::set<std::string>>();
    // 撤销重开取料的序号：同一档案可能在两格里各关一次，那两次撤销是两条**独立**意图，
    // 不能共用去重键——共用就会让后到的那条被静默吞掉，而栈里的条目已经弹出、没有第二次机会。
    std::uint64_t undo_fetch_serial = 0;

    // @param anchor 撤销重开（`SPEC.FEAT.WS.10`）的落点：有值时按**记下的那扇窗、原位置与原名字**
    //        插回，而不是在最近被碰过的窗尾部按档案名新开一格。跨过凭据问答也要保住它，故随锚点带上窗号。
    // 材料齐了才建格的那一段（跳链与非跳链共用；`open_ssh_tab` 保证走到这里时 bundle 已补齐）。
    auto open_ssh_tab_ready = [&](const borealis::conn::Profile &profile,
                                  borealis::conn::SshSecretBundle bundle,
                                  std::optional<RestoreAnchor> anchor) -> void {
        WorkspaceWindow *const window = anchor.has_value() ? window_of(anchor->window_id)
                                                           : tab_host_or_none("ssh profile connect");
        if (window == nullptr) {
            if (anchor.has_value()) {
                AURORA_LOG_WARN("main", "undo close tab: the host window is gone");
            }
            return;
        }
        // SFTP 面板接线：留档副本先取，bundle 照旧移交给 SshConnection。
        borealis::conn::SshSecretBundle sftp_bundle = bundle;
        const borealis::conn::SshProfile sftp_profile = profile.ssh;
        auto session = make_ssh_session(profile.ssh, std::move(bundle));
        // 工厂要的标签号就是本支即将取到的那个号（同 `create_new_tab` 处那一条）：取号点只有
        // `create_new_tab_with` 里那一处，两处之间没有别的发号。撤销重开那一支自己发号——它不走
        // `create_new_tab_with`（那一支只会追加到条尾）。
        const borealis::ui::TabId id = next_tab_id;
        if (anchor.has_value()) {
            ++next_tab_id;
            if (!restore_tab_at(*window, id, std::move(session), anchor->name,
                                anchor->index_in_strip, borealis::conn::ConnectionType::Ssh,
                                make_ssh_pane_factory(id))) {
                return;  // 会话与视口已随 `tab` 散掉，四张账一个都不登记。
            }
        } else {
            create_new_tab_with(*window, std::move(session), decode_tab_name(profile.name),
                                borealis::conn::ConnectionType::Ssh, make_ssh_pane_factory(id));
        }
        ssh_profile_by_tab[id] = sftp_profile;
        ssh_secret_by_tab[id] = std::move(sftp_bundle);
        tab_profile_id[id] = profile.id;
        flush_workspace_snapshot();  // 登记齐了才提交（同 `create_new_tab` 处那条次序）
        record_recent(profile.id);
        if (anchor.has_value()) {
            AURORA_LOG_INFO("main", "restored closed SSH tab: profile '", profile.id, "' at index ",
                            anchor->index_in_strip);
        }
    };

    // 开一格 SSH 标签（目标材料已在手上）。档案带跳链时先补齐**逐跳**材料再建格（稿 D8）：那一趟
    // 走凭据库、可能阻塞，故与目标那次 retrieve 同一套「后台取 + 在途去重」形态；取料期间锚点随
    // profile 一起留在闭包里，回插原位的语义不因这一跳而变。跳链为空 ⇒ `with_hop_secrets` 同步回调，
    // 非跳档案的时序与本批之前**完全一致**。`nullopt`＝某一询问跳被取消（批 7 D1）：静默返回，
    // 同目标询问取消的现语义。
    auto open_ssh_tab = [&](const borealis::conn::Profile &profile,
                            std::optional<std::string> secret,
                            std::optional<RestoreAnchor> anchor = std::nullopt) -> void {
        if (profile.ssh.proxy_jumps.empty()) {
            open_ssh_tab_ready(profile, borealis::conn::SshSecretBundle{std::move(secret)},
                               std::move(anchor));
            return;
        }
        const std::string fetch_key = anchor.has_value()
                                          ? "@jump-undo/" + profile.id + "/" +
                                                std::to_string(++undo_fetch_serial)
                                          : "@jump/" + profile.id;
        if (!pending_credential_fetches->insert(fetch_key).second) {
            return;  // 同一档案的跳板取料已在途：连点合并成一次。
        }
        with_hop_secrets(profile.ssh, profile.id, std::move(secret),
                         [profile, anchor, fetch_key, pending = pending_credential_fetches,
                          &open_ssh_tab_ready](
                             std::optional<borealis::conn::SshSecretBundle> bundle) -> void {
                             static_cast<void>(pending->erase(fetch_key));
                             if (!bundle.has_value()) {
                                 return;  // 逐跳取消：这一格不开，去重键已摘、可再点。
                             }
                             open_ssh_tab_ready(profile, std::move(*bundle), anchor);
                         });
    };

    // 本地档案会话：LocalProfile 与 LocalTerminalSpec 同构（命令/目录/环境三件），直搬。
    auto open_local_profile_tab = [&](const borealis::conn::Profile &profile) -> void {
        WorkspaceWindow *const window = tab_host_or_none("local profile connect");
        if (window == nullptr) {
            return;
        }
        borealis::conn::LocalTerminalSpec spec;
        spec.command_line = profile.local.command_line;
        spec.working_directory = profile.local.working_directory;
        spec.environment = profile.local.environment;
        // 号在本支取（同 SSH 腿那一条理由）：`create_new_tab_with` 里 `next_tab_id++` 是唯一的发号点。
        const borealis::ui::TabId id = next_tab_id;
        create_new_tab_with(*window, make_local_session(spec), decode_tab_name(profile.name), borealis::conn::ConnectionType::Local,
                            make_local_profile_pane_factory(profile.local), borealis::term::LineEnding::Lf, spec);
        tab_profile_id[id] = profile.id;
        flush_workspace_snapshot();
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
        WorkspaceWindow *const window = tab_host_or_none("serial profile connect");
        if (window == nullptr) {
            return;
        }
        auto bundle = make_serial_session(profile.serial);
        // 登记连接表用的是**本支取到的号**（同 SSH 腿那一条），不再回读「当前标签号」：前者是这一格
        // 的身份，后者是「落点窗那一刻的选中项」——把串口绑定挂到别的标签上，只可能来自一次串窗。
        const borealis::ui::TabId id = next_tab_id;
        create_new_tab_with(*window, bundle.session, decode_tab_name(profile.name), borealis::conn::ConnectionType::Serial,
                            make_refusing_pane_factory("serial device is exclusive"),
                            bundle.line_ending);
        serial_by_tab[id] =
            SerialTabBinding{.profile = profile.serial, .connection = bundle.connection};
        tab_profile_id[id] = profile.id;
        flush_workspace_snapshot();
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
        WorkspaceWindow *const window = tab_host_or_none("telnet profile connect");
        if (window == nullptr) {
            return;
        }
        auto bundle = make_telnet_session(profile.telnet);
        // 工厂要的号同 SSH 腿那一条理由：`create_new_tab_with` 里 `next_tab_id++` 是本支唯一的取号点。
        const borealis::ui::TabId id = next_tab_id;
        create_new_tab_with(*window, bundle.session, decode_tab_name(profile.name), borealis::conn::ConnectionType::Telnet,
                            make_telnet_pane_factory(id), bundle.line_ending);
        telnet_by_tab[id] = profile.telnet;
        tab_profile_id[id] = profile.id;
        flush_workspace_snapshot();
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

    // 凭据询问件（CONN.09 的降级腿）：档案没有可用的 Reference（或库不可用）时才走到这里；
    // 明文只经 on_secret 交给**本次**装配，不落盘不进日志（裁决 7.93）；提交后按 D6 先乐观
    // 回写确定性句柄、再把明文异步入库。
    auto credential_store = std::shared_ptr<borealis::conn::CredentialStore>{
        borealis::platform::make_credential_store()};

    // 凭据问句的唯一漏斗在这里**先声明后赋值**（赋值在下方 `credential_prompt` 到位那一处，同
    // `with_hop_secrets` 自身的先例）：逐跳串问泵（`SPEC.FEAT.CONN.03` 批 7 D2）写进缝的实现体，
    // 比缝更早可见就拿不到问句出口。
    auto ask_secret = std::function<void(borealis::conn::SecretAsk, const std::string &)>{};

    /// @brief 逐跳串问泵的一次在途会话（`SPEC.FEAT.CONN.03` 批 7 D1）。
    ///
    /// 单槽：与 `pending_profile` 同一条「后一次意图覆盖前一次」纪律——问句漏斗本就单实例，
    /// 新串问开始时旧串的 `done` 不再被调用（那次连接静默作罢），与目标询问被覆盖的现语义同形。
    /// 答案先落槽位；答齐一次**批量**回存（批 8 D3）——取消整串则一个都不写。
    struct HopAskSession {
        std::vector<std::size_t> queue{};   ///< 还没答的 Ask 跳下标（档案序，一题一题）。
        std::vector<borealis::conn::SecretAsk> kinds{};  ///< 与 queue 同序的问型。
        std::vector<std::string> subjects{};             ///< 与 queue 同序的 {subject}（稿 D3 A 档）。
        /// 与 queue 同序的回存键位（`hop_vault_key` 折算；空＝只进内存）。队列推进时一起弹。
        std::vector<std::string> keys{};
        std::vector<std::pair<std::size_t, std::string>> answered{};  ///< 已答 (下标, 键位)。
        std::string profile_id{};  ///< 本串所属档案；空＝临时档案，答齐也不回存（稿 D5①）。
        borealis::conn::SshSecretBundle bundle{};
        std::function<void(std::optional<borealis::conn::SshSecretBundle>)> done;
        bool active = false;
    };
    auto hop_ask = std::make_shared<HopAskSession>();

    // 逐跳材料的补齐实现（`SPEC.FEAT.CONN.03` 稿 D8 + 批 7 D1/D2）：计划先在调用方线程算好（纯逻辑，不碰钥匙环），
    // 只把「该去取的句柄」交给一趟后台任务——钥匙环可能弹解锁框并阻塞，UI 线程不许等（同目标那次
    // retrieve 的理由）。回程后 `Ask` 的那几跳按档案序经 `ask_secret` 逐跳串问：上一答落进槽位才发
    // 下一问，答齐才交回；取消任一跳 ⇒ `done(nullopt)` 整次作罢。Stored 跳取回空**不再二次询问**
    // （兜底询问是目标腿的语义；句柄在档就是用户选过「不回存问」的那一档），该跳在拨号侧落 `Auth`
    // 并带 hop_index，用户看得见是第几跳谈不成。
    // 跳链为空 ⇒ 同步回调，非跳档案不因此多一个异步跳；`hops` 无论取到与否都**按跳数对齐**，
    // 否则同一条链的两次取料会因「一份空向量 / 一份 [nullopt,nullopt]」被判成两条不同链路（稿 D5）。
    with_hop_secrets = [&credential_store, &ask_secret, hop_ask](
                           const borealis::conn::SshProfile &ssh,
                           const std::string &profile_id,
                           std::optional<std::string> secret,
                           std::function<void(std::optional<borealis::conn::SshSecretBundle>)> done)
        -> void {
        auto bundle = borealis::conn::SshSecretBundle{std::move(secret)};
        bundle.hops.resize(ssh.proxy_jumps.size());
        if (ssh.proxy_jumps.empty()) {
            done(std::move(bundle));
            return;
        }
        auto handles = std::vector<std::optional<borealis::conn::SecretHandle>>{};
        handles.reserve(ssh.proxy_jumps.size());
        auto any_to_fetch = false;
        auto queue = std::vector<std::size_t>{};
        auto kinds = std::vector<borealis::conn::SecretAsk>{};
        auto subjects = std::vector<std::string>{};
        // 键位表与串问队列同源（批 8 D2）：同一份 `plan_hop_credential` 判据筛 Ask 跳，答齐时
        // 按这份表回存。临时档案的 profile_id 为空 ⇒ 整表空，泵照常问、只是不落盘。
        const auto vault_keys = borealis::conn::plan_hop_vault_keys(ssh, *credential_store, profile_id);
        auto keys = std::vector<std::string>{};
        for (std::size_t k = 0; k < ssh.proxy_jumps.size(); ++k) {
            const auto &hop = ssh.proxy_jumps[k];
            const auto plan = borealis::conn::plan_hop_credential(hop, *credential_store);
            if (plan.route == borealis::conn::CredentialRoute::Stored) {
                handles.emplace_back(std::optional<borealis::conn::SecretHandle>{plan.handle});
                any_to_fetch = true;
                continue;
            }
            handles.emplace_back(std::nullopt);
            if (plan.route == borealis::conn::CredentialRoute::Ask) {
                queue.push_back(k);
                kinds.push_back(plan.ask);
                keys.push_back(vault_keys[k]);
                // {subject}＝`user@host:port`（稿 D3 A 档，零新询问词条）：主机/端口是动态数据
                // 不是字面量，不触门禁 CJK 闸；user 空时省 `user@`，与档案展示同口径。
                std::string subject;
                if (!hop.user.empty()) {
                    subject += hop.user + "@";
                }
                subject += hop.host + ":" + std::to_string(hop.port);
                subjects.push_back(std::move(subject));
            }
        }
        // 串问腿（D2）：队列空 ⇒ 当场交回；非空 ⇒ 装单槽会话、发第一问。之后的每一问都落在
        // 上一拍的回调栈外（`on_secret` 里答一题发下一题），无递归风险（先例＝恢复泵）。
        auto start_asks = [hop_ask, &ask_secret](
                              borealis::conn::SshSecretBundle filled,
                              std::vector<std::size_t> pending_queue,
                              std::vector<borealis::conn::SecretAsk> pending_kinds,
                              std::vector<std::string> pending_subjects,
                              std::vector<std::string> pending_keys,
                              std::string session_profile_id,
                              std::function<void(std::optional<borealis::conn::SshSecretBundle>)> finish)
            -> void {
            if (pending_queue.empty()) {
                finish(std::move(filled));
                return;
            }
            hop_ask->queue = std::move(pending_queue);
            hop_ask->kinds = std::move(pending_kinds);
            hop_ask->subjects = std::move(pending_subjects);
            hop_ask->keys = std::move(pending_keys);
            hop_ask->answered.clear();
            hop_ask->profile_id = std::move(session_profile_id);
            hop_ask->bundle = std::move(filled);
            hop_ask->done = std::move(finish);
            hop_ask->active = true;
            ask_secret(hop_ask->kinds.front(), hop_ask->subjects.front());
        };
        if (!any_to_fetch) {
            start_asks(std::move(bundle), std::move(queue), std::move(kinds),
                       std::move(subjects), std::move(keys), std::string{profile_id},
                       std::move(done));
            return;
        }
        auto task = au::async([credential_store,
                               handles]() -> std::vector<std::optional<std::string>> {
            auto out = std::vector<std::optional<std::string>>{};
            out.reserve(handles.size());
            for (const auto &handle : handles) {
                out.push_back(handle.has_value() ? credential_store->retrieve(*handle)
                                                 : std::optional<std::string>{});
            }
            return out;
        });
        static_cast<void>(task.then(
            [bundle = std::move(bundle), queue = std::move(queue), kinds = std::move(kinds),
             subjects = std::move(subjects), keys = std::move(keys),
             profile_id = std::string{profile_id}, done = std::move(done), start_asks](
                const au::Result<std::vector<std::optional<std::string>>> &result) mutable -> void {
                // 整趟取料失败（任务级错误）⇒ 逐跳留空：宁可在拨号侧现形，也不拿半截材料硬连。
                if (result.ok()) {
                    bundle.hops = result.value();
                }
                start_asks(std::move(bundle), std::move(queue), std::move(kinds),
                           std::move(subjects), std::move(keys), std::move(profile_id),
                           std::move(done));
            }));
    };
    // 在途去重键与撤销取料序号声明在上方 `open_ssh_tab` 之前（跳链取料也要用）。
    auto pending_profile = std::optional<PendingProfileConnect>{};
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
        std::tuple<std::string, std::string, borealis::conn::SshProfile, std::string>>{};
    auto submit_keys_export =
        std::function<void(const std::string &, std::optional<std::string>)>{};
    auto submit_keys_push = std::function<void(const std::string &, const std::string &,
                                               const borealis::conn::SshProfile &,
                                               std::optional<std::string>, std::string_view)>{};
    // 启动重放的凭据挂起态（`SPEC.FEAT.WS.11` 批 5-C）：SSH 腿按 D4① 要重新问材料，而询问件的答案是
    // 异步的，重放游标因此在这里挂起、由 `on_secret`／`on_cancel` 放行。三项都声明在 `prompt_hooks`
    // 之前是承重的——回调按引用捕获它们，晚声明就是「钩子先引用了一块还不存在的状态」。
    bool restore_waiting_for_secret = false;
    // 挂起中的那一步的放行口：有答案即继续，空值即中止余下重放（取消语义）。运行期由重放块赋值。
    std::function<void(std::optional<std::string>)> restore_secret_sink;
    // 放行过来的明文只在泵里过一手、取用即清（材料不落盘、不进日志，同 `ssh_secret_by_tab` 的存活期）。
    std::optional<std::string> restore_secret;
    // 询问提交后的回存（CONN.09 D6）：句柄是确定性的，先乐观写进档案并落盘，再把明文异步入库。
    // 入库失败不回滚句柄——下一次 retrieve 落空即自然回到询问，绝不会误用旧秘密。档案不在
    // `settings.profiles`（quick connect、临时档案、已删除档案）或库不可用时整段跳过。
    auto remember_profile_secret = [&store, credential_store](const std::string &profile_id,
                                                              const std::string &secret) -> void {
        if (profile_id.empty() || !credential_store->available()) {
            return;
        }
        auto next = store.settings();
        borealis::conn::Profile *target = nullptr;
        for (auto &candidate : next.profiles) {
            if (candidate.id == profile_id) {
                target = &candidate;
                break;
            }
        }
        if (target == nullptr || target->type != borealis::conn::ConnectionType::Ssh) {
            return;
        }
        const std::string vault_id = borealis::platform::credential_vault_id(
            borealis::platform::credential_vault_prefix(), profile_id);
        if (vault_id.empty()) {
            return;
        }
        target->ssh.credential = borealis::conn::SecretHandle::reference(vault_id);
        if (const auto error = store.replace(next); error.has_value()) {
            AURORA_LOG_WARN("main", "credential handle not persisted for ", profile_id, ": ", *error);
        }
        const std::string label = target->name;
        // 返回 bool 而非 void：Aurora 的 async 值萃取对 void 没有特化；真值只表示「任务跑完了」。
        static_cast<void>(au::async([credential_store, profile_id, label, secret]() -> bool {
            static_cast<void>(credential_store->store(profile_id, label, secret));
            return true;
        }));
    };
    // 逐跳答案的回存（`SPEC.FEAT.CONN.03` 批 8 D1/D3）：整串答齐才写，一次 replace 带多跳句柄，
    // 每键一枚任务入库；取消整串则一个都不写。句柄是确定性的 `<前缀><profile_id>#<跳形>`，
    // 入库失败不回滚——与目标腿同档代价（下次取空自然重问，不会误用旧秘密）。
    auto remember_hop_secrets =
        [&store, credential_store](const std::string &profile_id,
                                   const std::vector<std::pair<std::size_t, std::string>> &answered,
                                   const borealis::conn::SshSecretBundle &bundle) -> void {
        if (profile_id.empty() || answered.empty() || !credential_store->available()) {
            return;  // 临时档案／库不可用：与目标腿的现闸同口径（稿 D5①②）。
        }
        auto next = store.settings();
        borealis::conn::Profile *target = nullptr;
        for (auto &candidate : next.profiles) {
            if (candidate.id == profile_id) {
                target = &candidate;
                break;
            }
        }
        if (target == nullptr || target->type != borealis::conn::ConnectionType::Ssh) {
            return;
        }
        // 「该写哪几跳」收在纯逻辑件里（键位复核、Stored 不覆写、缺答案跳过都在那儿有证人）；
        // 这里只做三步搬运：置句柄 → 一次 replace → 每键一枚任务入库。
        const auto entries =
            borealis::conn::plan_hop_writeback(*target, *credential_store, answered, bundle.hops);
        if (entries.empty()) {
            return;
        }
        auto pending = std::vector<std::tuple<std::string, std::string, std::string>>{};
        pending.reserve(entries.size());
        for (const auto &entry : entries) {
            const std::string vault_id = borealis::platform::credential_vault_id(
                borealis::platform::credential_vault_prefix(), entry.key);
            if (vault_id.empty()) {
                continue;
            }
            target->ssh.proxy_jumps[entry.index].credential =
                borealis::conn::SecretHandle::reference(vault_id);
            pending.emplace_back(entry.key, entry.label, entry.secret);
        }
        if (pending.empty()) {
            return;
        }
        if (const auto error = store.replace(next); error.has_value()) {
            AURORA_LOG_WARN("main", "hop credential handles not persisted for ", profile_id, ": ",
                            *error);
        }
        static_cast<void>(au::async([credential_store, pending = std::move(pending)]() -> bool {
            for (const auto &[key, label, secret] : pending) {
                static_cast<void>(credential_store->store(key, label, secret));
            }
            return true;
        }));
    };
    borealis::ui::CredentialPrompt::Hooks prompt_hooks;
    prompt_hooks.on_secret =
        [&open_ssh_tab, &restart_ssh_tab, &pending_profile, &pending_restart,
         &pending_tunnel, &pending_export, &pending_push, &remember_profile_secret,
         &remember_hop_secrets, &tab_profile_id, &launch_tunnel, &submit_keys_export,
         &submit_keys_push, &restore_waiting_for_secret, &restore_secret_sink, &ask_secret,
         hop_ask](const std::string &secret) -> void {
        // 逐跳串问先认领（`SPEC.FEAT.CONN.03` 批 7 D1）：泵在途时的问句只可能是它发的。答一题
        // 落进槽位——队列还有下一题就当场续问（本回调栈里只发起模态，不等答案），答齐才交回。
        // 批 8 D3：答齐后按键位列**一次**回存本串答过的跳；取消走 on_cancel，不经过这里。
        if (hop_ask->active) {
            const auto index = hop_ask->queue.front();
            static_cast<void>(hop_ask->queue.erase(hop_ask->queue.begin()));
            static_cast<void>(hop_ask->kinds.erase(hop_ask->kinds.begin()));
            static_cast<void>(hop_ask->subjects.erase(hop_ask->subjects.begin()));
            const auto vault_key = std::move(hop_ask->keys.front());
            static_cast<void>(hop_ask->keys.erase(hop_ask->keys.begin()));
            if (!vault_key.empty()) {
                hop_ask->answered.emplace_back(index, vault_key);
            }
            hop_ask->bundle.hops[index] = secret;
            if (hop_ask->queue.empty()) {
                hop_ask->active = false;
                auto finish = std::move(hop_ask->done);
                auto filled = std::move(hop_ask->bundle);
                remember_hop_secrets(hop_ask->profile_id, hop_ask->answered, filled);
                hop_ask->answered.clear();
                finish(std::move(filled));
                return;
            }
            ask_secret(hop_ask->kinds.front(), hop_ask->subjects.front());
            return;
        }
        // 恢复腿先认领（批 5-C）：挂起期间的问句只可能是它发的，答话也只回到它那一侧。
        if (restore_waiting_for_secret) {
            restore_waiting_for_secret = false;
            if (restore_secret_sink) {
                restore_secret_sink(secret);
            }
            return;
        }
        if (pending_restart.has_value()) {
            const auto [id, ssh] = std::move(*pending_restart);
            pending_restart.reset();
            if (const auto profile = tab_profile_id.find(id); profile != tab_profile_id.end()) {
                remember_profile_secret(profile->second, secret);
            }
            restart_ssh_tab(id, ssh, secret);
            return;
        }
        if (pending_profile.has_value()) {
            auto pending = std::move(*pending_profile);
            pending_profile.reset();
            remember_profile_secret(pending.profile.id, secret);
            open_ssh_tab(pending.profile, secret, std::move(pending.anchor));
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
            auto [path, profile_id, ssh_profile, line] = std::move(*pending_push);
            pending_push.reset();
            submit_keys_push(path, profile_id, ssh_profile, secret, line);
        }
    };
    prompt_hooks.on_cancel = [&pending_profile, &pending_restart, &pending_tunnel,
                              &pending_export, &pending_push, &restore_waiting_for_secret,
                              &restore_secret_sink, hop_ask]() -> void {
        // 串问泵取消＝整次作罢（批 7 D1）：`done(nullopt)` 让四个消费者各走自己的取消支
        // （开标签静默返回、隧道丢弃、密钥不投、恢复泵中止余下重放）。先放行再清 pending——
        // 与恢复腿同款：两套状态互不相干，多清一遍无害，但漏清会留下半挂起的意图。
        // 批 8 D3：取消整串 ⇒ 一个键位都不写（`answered` 连同半截链一起作废）。
        if (hop_ask->active) {
            hop_ask->active = false;
            hop_ask->answered.clear();
            hop_ask->keys.clear();
            auto finish = std::move(hop_ask->done);
            finish(std::nullopt);
        }
        // 恢复腿的取消＝中止余下重放（批 5-C）：已经恢复出来的格原样留下，当前窗没格则补一格缺省
        // 本地标签（收尾在 `pump_restore` 那一侧）。这里先放行再清 pending——两套状态互不相干，
        // 多清一遍无害，但**不能**先 return，否则一次恢复期的取消会漏掉其他 pending 的清理。
        if (restore_waiting_for_secret) {
            restore_waiting_for_secret = false;
            if (restore_secret_sink) {
                restore_secret_sink(std::nullopt);
            }
        }
        pending_profile.reset();
        pending_restart.reset();
        pending_tunnel.reset();
        pending_export.reset();
        pending_push.reset();
    };
    auto credential_prompt =
        std::make_shared<borealis::ui::CredentialPrompt>(*host, std::move(prompt_hooks));
    // 凭据问句的唯一漏斗（稿 D6 的搬窗腿）：六处入口（档案连接、隧道、密钥口令、重启重拨、SFTP 重拨、
    // 逐跳串问泵）都要「先把问句搬到发起连接的那一扇窗，再问」。写在调用点就会漏一处，漏掉的那一处等于把材料问句
    // 弹在用户没碰过的窗脸上——那正是 `CredentialPrompt` 这一支按窗取要防的形态。
    ask_secret = [&credential_prompt, &retarget_panel](const borealis::conn::SecretAsk ask,
                                                      const std::string &name) -> void {
        retarget_panel(*credential_prompt);
        credential_prompt->ask(ask, name);
    };

    // 档案连接的四条腿共用这一支。@p anchor 只有撤销重开（`SPEC.FEAT.WS.10`）会给：它是 SSH
    // 标签的「原窗原位原名」落点，穿过凭据计划的三条路线一直带到 `open_ssh_tab`。其余三条腿
    // 不认这一枚（撤销只对 SSH 与本地两档分腿，本地那一档不走 `connect_profile`）。
    auto connect_profile_to = [&](const borealis::conn::Profile &profile,
                                  std::optional<RestoreAnchor> anchor) -> void {
        if (profile.type == borealis::conn::ConnectionType::Local) {
            open_local_profile_tab(profile);
            return;
        }
        if (profile.type == borealis::conn::ConnectionType::Serial) {
            open_serial_tab(profile);  // 串口无凭据可问：档案即全部料（CONN.09 的审计天然过）。
            return;
        }
        // Telnet 同串口那条：没有认证协议、没有材料可要（登录提示是远端程序给的文本，不是
        // 本仓的凭据链），故它也必须**挡在**凭据计划之前。少了这一支不会崩，但会静默
        // 走错腿：空白 `profile.ssh` 的 `auth_method` 缺省是 "agent" ⇒ `CredentialRoute::None` ⇒
        // 拿一个空主机去拨 SSH，标签名却还是 Telnet 档案名——拨号失败的事实被挂到一条不存在的连接上。
        if (profile.type == borealis::conn::ConnectionType::Telnet) {
            open_telnet_tab(profile);
            return;
        }
        const auto plan = borealis::conn::plan_credential(profile.ssh, *credential_store);
        if (plan.route == borealis::conn::CredentialRoute::None) {
            open_ssh_tab(profile, std::nullopt, std::move(anchor));  // agent 认证：无材料可要。
            return;
        }
        if (plan.route == borealis::conn::CredentialRoute::Stored) {
            // 钥匙环可能弹解锁框并阻塞（D8）：事件回调只发起任务，命中后回主线程再开标签。
            // 在途去重的键对撤销重开另起一档并带序号：撤销与侧栏连接是两条意图，两次撤销
            // 之间也是两条意图，共用一格就会让后到的那条被静默吞掉，而撤销栈的条目已经弹出、
            // 没有第二次机会。
            const std::string fetch_key =
                anchor.has_value() ? std::string{"@undo/"} + profile.id + "/" +
                                         std::to_string(++undo_fetch_serial)
                                   : profile.id;
            if (!pending_credential_fetches->insert(fetch_key).second) {
                return;  // 同一档案的取料已在途：连点合并成一次。
            }
            auto task = au::async([credential_store, handle = plan.handle]()
                                      -> std::optional<std::string> {
                return credential_store->retrieve(handle);
            });
            static_cast<void>(task.then(
                [profile = borealis::conn::Profile{profile}, ask = plan.ask, anchor,
                 fetch_key, pending = pending_credential_fetches,
                 prompt = std::weak_ptr<borealis::ui::CredentialPrompt>{credential_prompt},
                 &pending_profile, &ask_secret,
                 &open_ssh_tab](const au::Result<std::optional<std::string>> &result) -> void {
                    pending->erase(fetch_key);
                    if (prompt.expired()) {
                        return;  // 宿主已散：迟到结果不碰任何装配态。
                    }
                    if (result.ok() && result.value().has_value()) {
                        open_ssh_tab(profile, *result.value(), std::move(anchor));
                        return;
                    }
                    if (!result.ok()) {
                        AURORA_LOG_WARN("main", "credential retrieve failed for ", profile.id, ": ",
                                        result.error().message);
                    }
                    // 取不到（未命中或后端出错）⇒ 回到询问，绝不拿空材料硬连（D3/D5）。
                    pending_profile = PendingProfileConnect{profile, anchor};
                    ask_secret(ask, profile.name);
                }));
            return;
        }
        pending_profile = PendingProfileConnect{profile, std::move(anchor)};
        ask_secret(plan.ask, profile.name);
    };
    // 入口形态（侧栏 / 向导 / CLI 转交）只要「按档案连一标签」：锚点为空即原先的行为。
    auto connect_profile = [&connect_profile_to](const borealis::conn::Profile &profile) -> void {
        connect_profile_to(profile, std::nullopt);
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
    // `create_new` / `edit` / `note_window_press` / `persist_collapsed` 四枚按窗不同（常驻列每窗一份，
    // 而向导仍是 app 级单实例），故在下面的 `make_sidebar` 里逐窗补，不在这张应用级表上。
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
    // 常驻列按窗各建一份（裁决 7.180 改判的落点）：槽位钉在这一扇的中段行里，搬不走——一枚控件节点
    // 只有一个父节点，把列从 A 窗搬到 B 窗就是让 A 窗的骨架缺一条腿，那与浮层「搬过去」是两件事。
    // 应用级那几条接缝整张复制过来，四枚按窗不同的在这里逐窗补。
    make_sidebar = [&store, &sidebar_hooks, &workspace_windows, &tab_workspaces, &wizard,
                    &retarget_panel_to, &report_dispatch](WorkspaceWindow &window) -> void {
        if (window.sidebar != nullptr) {
            return;  // 一扇窗一条列：回头装配（切标签、开窗撤账）不许再建第二条
        }
        auto hooks = sidebar_hooks;
        hooks.create_new = [&wizard, &retarget_panel_to, &window]() -> void {
            retarget_panel_to(*wizard, &window);  // 向导仍是 app 级单实例：开在这一扇窗的浮层上
            wizard->open_new();
        };
        hooks.edit = [&wizard, &retarget_panel_to,
                      &window](const borealis::conn::Profile &profile) -> void {
            retarget_panel_to(*wizard, &window);
            wizard->open_edit(profile);
        };
        hooks.persist_collapsed = [&store](bool collapsed) -> void {
            // 只写盘，不广播：本条腿的来向是用户点了某一扇窗的轨或命令，而 `Store::replace()` 本身
            // 不回流到 UI（回流那一条是设置页的 `hooks.broadcast`）。同值不写，省一次落盘与快照。
            auto next = store.settings();
            if (next.appearance.sidebar_collapsed == collapsed) {
                return;
            }
            next.appearance.sidebar_collapsed = collapsed;
            static_cast<void>(store.replace(std::move(next)));
        };
        hooks.note_window_press = [&report_dispatch, &window]() -> void {
            // 稿 D7 的又一处上报点：点过这一扇的轨，下一条全局快捷键就该落这一扇。
            if (report_dispatch) {
                report_dispatch(window);
            }
        };
        window.sidebar = std::make_shared<borealis::ui::ConnectionSidebar>(std::move(hooks));
        // 起步档取落盘那份（N1：折叠缺省，展开态被记住）。走 `set_collapsed` 而不是 `open`——配置是
        // 来向，回写它就是把刚读到的值再写回去一次。
        window.sidebar->set_collapsed(store.settings().appearance.sidebar_collapsed);
    };
    // 主窗的内容装配排在本段之前（`assemble_window_content` 那时 `make_sidebar` 还没赋值），
    // 故已经活着的那些窗在这里回头补建，并各自重排一次中段行把槽位挂进去。
    for (const std::unique_ptr<WorkspaceWindow> &window : workspace_windows) {
        make_sidebar(*window);
        rebuild_root_children(*window, tab_workspaces);
    }

    // 打开入口（Q4/Q5 改判，裁决 7.180）：仍走命令层（快捷键 / 命令面板 / 菜单的共同真源，同
    // `settings.open`），但开合的对象换成**发起命令那一扇窗**自己的列——旧腿是「搬浮层到落点窗再 toggle」，
    // 常驻列不搬，落点窗那一条本来就在。默认不绑死键位（进 PREF.04 体系后用户自行绑定）。
    au::Command toggle_connections;
    toggle_connections.id = "connections.toggle";
    toggle_connections.title = borealis::ui::settings_label("connections.title");
    toggle_connections.category = "terminal";
    toggle_connections.action = [&command_window]() -> void {
        WorkspaceWindow *const target = command_window ? command_window() : nullptr;
        if (target != nullptr && target->sidebar != nullptr) {
            target->sidebar->toggle();
        }
    };
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

    // 拉起前先过 `with_hop_secrets`（`SPEC.FEAT.CONN.03` 批 6 D3/D4 + 批 7 D1）：0 跳同步回调、时序与
    // 批 6 之前完全一致；带跳时逐跳材料经凭据库后台补齐，`Ask` 的那几跳由缝内串问泵当场问用户——
    // 手动支有人在等框，取消任一跳即丢弃这次拉起（同 `pending_tunnel` 取消语义）。回-call 落主线程
    // 再建 `Tunnel`；启动序零模态由自启扫描的 `hop_secrets_silent` 闸保证（批 7 D8），不靠这里。
    // 回调按引用捕获
    // main 的局部是既有先例形态（`open_ssh_tab` 那一支同款）：任务在途期间 main 不散。
    launch_tunnel = [&store, &tunnel_entries, &with_hop_secrets, tunnel_mutex, tunnel_runtimes,
                     tunnel_skipped](const borealis::conn::SshProfile &ssh_profile,
                                     std::optional<std::string> secret,
                                     const borealis::conn::TunnelSpec &spec) -> void {
        with_hop_secrets(ssh_profile, spec.profile_id, std::move(secret),
                         [&store, &tunnel_entries, tunnel_mutex, tunnel_runtimes, tunnel_skipped,
                          profile = ssh_profile,
                          spec](std::optional<borealis::conn::SshSecretBundle> bundle) -> void {
                             if (!bundle.has_value()) {
                                 return;  // 逐跳询问取消：整次拉起作罢（批 7 D1）。
                             }
                             // profile/spec 按值入闭包：调用方的档案可能是 `store.settings()` 局部副本
                             // 里的引用（tunnel_hooks.start 那一支），异步回-call 时原件早已不在。
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
                             auto tunnel = std::make_unique<borealis::conn::Tunnel>(
                                 profile, std::move(*bundle), spec, spec.retry,
                                 borealis::conn::SshAlgorithmOverrides{
                                     store.settings().connection.ssh.kex_algorithms,
                                     store.settings().connection.ssh.host_key_algorithms});
                             tunnel->start(*sink);
                             tunnel_entries[spec.id] = TunnelEntry{std::move(sink), std::move(tunnel)};
                         });
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
        [&store, &ask_secret, &pending_tunnel, &launch_tunnel, tunnel_mutex,
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
        ask_secret(ask, target->name);
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

    // 启动序自启扫描（D8②细则 + 批 7 D8）：只拉起「autostart ∧ 目标 ∧ **每一跳**都可静默解析」的
    // 隧道；任一格是询问型就记入 skipped 集供行内提示——本路径零模态框、零阻塞等待。批 6 那次只审了
    // 目标（F10 的模态泄漏：目标 agent、跳 Ask 时缝里的串问泵会在开机弹第一框），本条把闸补全。
    // Stored 跳的取料仍在后台（钥匙环解锁是系统级行为），取不到的跳留空、在拨号侧落 `Auth` 带
    // hop_index——提示文案不说谎，「需先存凭据」两格都算（目标与询问型跳板）。
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
                    borealis::conn::SecretAsk::None &&
                borealis::conn::hop_secrets_silent(target->ssh, *credential_store);
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
    open_tunnels.action = [&tunnel_panel, &retarget_panel]() -> void {
        retarget_panel(tunnel_panel);
        tunnel_panel.toggle();
    };
    open_tunnels.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_tunnels));

    // SFTP 浏览器面板（SPEC.FEAT.CONN.04）：右停靠浮层，与侧栏同族。三条 Hooks 全部
    // 指回上面的标签接线数据，不新造状态：活动 SSH 档案、CONN.09 凭据链的句柄解析、
    // 标签判活。stack 对象即可——命令与帧回调都只捕获引用，析构顺序上它晚于浮层宿主
    // 的使用期（main 末尾），析构里 join worker 与断链。
    borealis::ui::SftpPanel::Hooks sftp_hooks;
    sftp_hooks.current_ssh_profile = [&ssh_profile_by_tab, &command_tab_id]()
        -> std::optional<borealis::conn::SshProfile> {
        const std::optional<borealis::ui::TabId> target = command_tab_id();
        if (!target.has_value()) {
            return std::nullopt;
        }
        const auto it = ssh_profile_by_tab.find(target.value());
        return it != ssh_profile_by_tab.end() ? std::optional{it->second} : std::nullopt;
    };
    // SFTP 面板吃**整套留档**（目标 + 逐跳，`SPEC.FEAT.CONN.03` 批 6 D3）：材料在开标签那一刻
    // 就齐了（`with_hop_secrets` 那一趟），这里只随格交出——不新增取料趟，标签不在档即空 bundle
    // （agent 认证本就无需材料；带跳缺料的跳在拨号侧落 `Auth` 带 hop_index，口径同终端腿）。
    sftp_hooks.resolve_secret = [&ssh_secret_by_tab, &command_tab_id]() -> borealis::conn::SshSecretBundle {
        const std::optional<borealis::ui::TabId> target = command_tab_id();
        if (!target.has_value()) {
            return borealis::conn::SshSecretBundle{};
        }
        const auto it = ssh_secret_by_tab.find(target.value());
        return it != ssh_secret_by_tab.end() ? it->second : borealis::conn::SshSecretBundle{};
    };
    // 算法覆盖项：设置是全局档，每次 Connect 现取——改完设置后面板的重拨（`connect_now`）
    // 即用新表，无需重建面板（NextSession 口径：在途会话不换协商面）。
    sftp_hooks.algorithm_overrides = [&settings]() -> borealis::conn::SshAlgorithmOverrides {
        return borealis::conn::SshAlgorithmOverrides{settings.connection.ssh.kex_algorithms,
                                                     settings.connection.ssh.host_key_algorithms};
    };
    sftp_hooks.ssh_tab_alive = [&ssh_profile_by_tab, &command_tab_id]() -> bool {
        const std::optional<borealis::ui::TabId> target = command_tab_id();
        return target.has_value() && ssh_profile_by_tab.count(target.value()) > 0;
    };
    borealis::ui::SftpPanel sftp_panel{*host, std::move(sftp_hooks)};

    au::Command toggle_sftp;
    toggle_sftp.id = "sftp.toggle";
    toggle_sftp.title = borealis::ui::settings_label("sftp.title");
    toggle_sftp.category = "terminal";
    toggle_sftp.action = [&sftp_panel, &retarget_panel]() -> void {
        retarget_panel(sftp_panel);
        sftp_panel.toggle();
    };
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

    // 推送公钥的提交点（hook 与凭据回-call 共用）：认证材料随 dial 返回即消失（CONN.09 的
    // 明文纪律）。批 6 D3：提交前先过 `with_hop_secrets` 补齐逐跳材料——0 跳同步回-call，
    // 时序与本批之前完全一致；带跳时回-call 落主线程，算法快照仍按「提交这一刻」在 UI 线程
    // 现取。批 7 D1：某一询问跳被取消 ⇒ 不投（这一趟推送就此作罢）。档案与公钥行**按值**入闭包：
    // 询问型那一支交来的是 `store.settings()` 局部副本里的引用（`keys_hooks.push`），异步回来时原件早已不在。
    submit_keys_push = [&](const std::string &path, const std::string &profile_id,
                           const borealis::conn::SshProfile &profile,
                           std::optional<std::string> secret, std::string_view line) -> void {
        with_hop_secrets(profile, profile_id, std::move(secret),
                         [&, path, profile,
                          line = std::string{line}](
                             std::optional<borealis::conn::SshSecretBundle> bundle) -> void {
                             if (!bundle.has_value()) {
                                 return;  // 逐跳询问取消：不投这次推送（批 7 D1）。
                             }
                             // 算法覆盖项在提交这一刻拍快照（UI 线程读配置），随任务进 worker——
                             // worker 不直接读 store。
                             const auto algorithms = borealis::conn::SshAlgorithmOverrides{
                                 store.settings().connection.ssh.kex_algorithms,
                                 store.settings().connection.ssh.host_key_algorithms};
                             keys_worker.submit(
                                 borealis::ui::KeysOpKind::Push, path,
                                 [path_target = path, profile, bundle = std::move(*bundle), line,
                                  algorithms, mutex = keys_worker.mutex(),
                                  latest = keys_worker.latest()]() -> void {
                                     const auto report = borealis::conn::push_public_key(
                                         profile, std::move(bundle), line, algorithms);
                                     const std::lock_guard lock{*mutex};
                                     latest->push_step =
                                         borealis::conn::push_stage_index(report.stage);
                                     latest->notice_key =
                                         std::string{borealis::ui::key_push_notice_key(report)};
                                     latest->notice_arg.clear();
                                 });
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
    // 不留痕（稿 §8 判据）。落盘逐字进 `key_dirs` 域（去重归 `normalize_key_dirs`，往返逐字
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
                ask_secret(borealis::conn::SecretAsk::Passphrase, row.basename);
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
            submit_keys_push(path, profile_id, target->ssh, std::nullopt, line);
            return;
        }
        pending_push = std::tuple{path, profile_id, target->ssh, line};
        ask_secret(ask, target->name);
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
    open_keys.action = [&keys_panel, &retarget_panel]() -> void {
        retarget_panel(keys_panel);
        keys_panel.toggle();
    };
    open_keys.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_keys));

    // 快捷片段（`SPEC.FEAT.CONN.07` 的延后子项，稿 `UI_SNIPPETS.draft.md` D1/D3/D6）：app 级一份面板，
    // 打开前先搬到发起命令的那一扇窗。三条接缝与面板的分工照私有头那段：本层只交「取表／整表落盘／
    // 发一段已渲染 UTF-8」，校验、占位符渲染与留痕都在面板；面板不碰文件，也就不必知道 `Store` 的形态。
    borealis::ui::SnippetPanel::Hooks snippet_hooks;
    snippet_hooks.load = [&store]() -> std::vector<borealis::conn::Snippet> {
        return store.settings().snippets;
    };
    snippet_hooks.persist = [&store](const std::vector<borealis::conn::Snippet> &next) -> bool {
        borealis::config::Settings settings_next = store.settings();
        settings_next.snippets = next;
        return !store.replace(settings_next).has_value();
    };
    // 落点（稿 D3）：**活动窗当前标签的焦点格**，只发这一格、不广播、不回退到 1 号格——把命令发进用户
    // 没看着的那一格比「这一条没发出去」更难解释（`quick_select.open` 取焦点格同一条理由）。取不到窗／
    // 标签／视图一律交回 false，面板据此留 `snippets.notice.no_session`。
    snippet_hooks.send = [&tab_workspaces, &command_tab_id](const std::string &payload) -> bool {
        const std::optional<borealis::ui::TabId> target = command_tab_id();
        if (!target.has_value()) {
            return false;
        }
        auto it = tab_workspaces.find(target.value());
        if (it == tab_workspaces.end() || it->second.workspace == nullptr) {
            return false;
        }
        auto *const view = it->second.workspace->view_of(it->second.workspace->focused_pane());
        if (view == nullptr) {
            return false;
        }
        view->send_text_utf8(payload);
        return true;
    };
    borealis::ui::SnippetPanel snippet_panel{*host, std::move(snippet_hooks)};

    // `snippets.open`（稿 D6）：与搜索／quick select 同一条「命令是共同真源」形态，登记一次即同时得到
    // `Ctrl+Shift+K` 与命令面板里的同一行。组合键全仓未占用（稿 F7），由快捷键层在控件消费之前拦截，
    // 不进会话。打开腿先搬窗再 toggle：关着时开在发起窗上，开着时再按一次即收掉当前窗那一份。
    au::Command open_snippets;
    open_snippets.id = "snippets.open";
    open_snippets.title = borealis::ui::settings_label("snippets.title");
    open_snippets.category = "terminal";
    open_snippets.action = [&snippet_panel, &retarget_panel]() -> void {
        retarget_panel(snippet_panel);
        snippet_panel.toggle();
    };
    open_snippets.default_binding =
        au::KeyCombo{au::ModifierKey::Control | au::ModifierKey::Shift, au::KeyCode::K};
    open_snippets.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_snippets));

    // 打开入口按 `SPEC.FEAT.PREF.02` 走命令层：命令是快捷键、菜单与命令面板的共同真源（架构 §11.2），
    // 在此登记一次即同时得到 `Ctrl+,` 与将来面板/命令面板里的同一条目。
    au::Command open_settings;
    open_settings.id = "settings.open";
    open_settings.title = borealis::ui::settings_label("settings.action.open");
    open_settings.category = "settings";
    open_settings.action = [&panel, &retarget_panel]() -> void {
        // 搬窗（稿 D6）：设置页挂在按值留存的那枚宿主上（=首格）直到第一次被打开为止，这一跳之后它跟着
        // 发起命令的窗走。`hooks.preview_wake` 读的就是这一支改完之后的宿主。
        retarget_panel(panel);
        panel.open();
    };
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
    open_search.action = [&tab_workspaces, &command_tab_id]() -> void {
        // 落点按窗解析（`SPEC.FEAT.WS.03` 稿 D7）：这一条经框架快捷键层进来，闭包拿不到派发来源，
        // 于是「哪一扇窗的当前标签」只能问 tracker——最近被碰过的那一扇。取不到窗只剩一种形态
        //（主窗自己已散），此时**什么都不做**：批 3-C 那版在这里退回主窗别名，而别名指着的可能正是
        // 那一扇已经关掉的窗（批 4-A 收掉它）。窗内的格仍取 1 号视图：那是既有形态，逐格搜索属
        // `SPEC.FEAT.INTERACT.04` 的账，本条不动它。
        const std::optional<borealis::ui::TabId> target = command_tab_id();
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

    // quick select 的打开入口（`SPEC.FEAT.INTERACT.02` 的延后子项，稿 §2）：与搜索同一条「命令是共同真源」
    // 形态，故 `Ctrl+Shift+Space` 与命令面板一处登记。落点是**焦点格**而非 1 号格（`search.open` 是后者，
    // 它那一笔逐格搜索仍欠着）：quick select 复制的是用户正看着的那一格，聚焦在别的 pane 上却扫 1 号格
    // 会把标签贴到看不见的文本上。组合键全仓未占用（稿 F8），由快捷键层在控件消费之前拦截，不进会话。
    au::Command open_quick_select;
    open_quick_select.id = "quick_select.open";
    open_quick_select.title = borealis::ui::settings_label("quick_select.action.open");
    open_quick_select.category = "terminal";
    open_quick_select.action = [&tab_workspaces, &command_tab_id]() -> void {
        const std::optional<borealis::ui::TabId> target = command_tab_id();
        if (!target.has_value()) {
            return;
        }
        auto it = tab_workspaces.find(target.value());
        if (it == tab_workspaces.end() || it->second.workspace == nullptr) {
            return;
        }
        if (auto *view = it->second.workspace->view_of(it->second.workspace->focused_pane())) {
            view->open_quick_select();
        }
    };
    open_quick_select.default_binding =
        au::KeyCombo{au::ModifierKey::Control | au::ModifierKey::Shift, au::KeyCode::Space};
    open_quick_select.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(open_quick_select));

    // 命令面板实例（`SPEC.FEAT.WS.07`）：挂在 `OverlayHost` 上作为全局模态浮层，与设置面板同级。
    // 先建实例再登记打开命令，这样 action 闭包可以捕获面板指针。
    auto command_palette = std::make_shared<au::CommandPalette>(&app.commands());
    command_palette->set_on_execute([&app](const std::string &id) -> void {
        // 框架的 `execute_selected()` 已经调过 `invoke`，这里只做收尾：面板已在 execute_selected()
        // 内部关闭，故只需记录日志（若需要）。
        AURORA_LOG_INFO("main", "command palette executed: ", id);
    });
    // 占位符与空态文案按**词条 Id** 交框架查 `default_catalog()`：框架自带的两条 builtin Id 属于库的保留
    // 区，本仓的词表不能占用那两个名字（gen_i18n 会直接拒收），故这里显式换成本仓那两条 app 档 key。
    // 解析发生在布局与绘制期、locale 取场景根注入的 `Locale`，因此这两句跟着 `appearance.language` 走。
    // 刻意不调 `set_placeholder()` / `set_empty_message()`：那两枚是**文本覆盖**，置位即不再走 i18n，
    // 运行期切 locale 就再也翻不动这两句（裁决 **7.92**①）。
    command_palette->set_placeholder_key(borealis::msg::id::AURORA_COMMAND_PALETTE_FIELD_PLACEHOLDER);
    command_palette->set_empty_message_key(borealis::msg::id::AURORA_COMMAND_PALETTE_EMPTY_MESSAGE);

    // 把命令面板挂进场景根（`OverlayHost` 的子节点 [1..] 是按需追加的浮层）：初始不打开，故只建不弹。
    // 面板的 `open()` / `close()` 会自动管理焦点作用域（push_scope / pop_scope），与设置面板同口径。
    // 初始挂首格那一份宿主，此后每次打开先搬到发起窗（稿 D6）。序号在这里就记下：不留序号的常驻浮层
    // 搬不了窗——摘不到自己那一层，只能往别的窗再挂一份，那变成「各窗一份」，不是本条的口径。
    MountedOverlay palette_mount;
    remount_overlay(command_palette, palette_mount, *host);

    // 命令面板的打开入口（判据文 §3 D1）：`Ctrl+Shift+P` 呼出，语义同 Tabby / Windows Terminal。
    // 框架侧 CommandPalette 已具备模态浮层 + 即时过滤 + Enter/Esc/↑/↓ 全接管能力（widget/command_palette.h）。
    au::Command open_command_palette;
    open_command_palette.id = "command_palette.open";
    open_command_palette.title = borealis::ui::settings_label("command_palette.action.open");
    open_command_palette.category = "workspace";
    open_command_palette.action = [command_palette, &command_window, &palette_mount,
                                   &remount_overlay]() -> void {
        // 搬窗（稿 D6）：app 级一只面板，浮层要开在发起 `Ctrl+Shift+P` 的那一扇窗上。取不到落点窗即按
        // 它现在挂着的那一扇打开（口径①，与面板那几条同一条）。
        WorkspaceWindow *const window = command_window ? command_window() : nullptr;
        if (window != nullptr) {
            remount_overlay(command_palette, palette_mount, *window->overlay_host);
        }
        command_palette->open();
    };
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

    // 关闭这一扇窗（`SPEC.FEAT.WS.03` 稿 D3 的另一条入口，裁决 7.161）：与上一条同形态——**只登记命令、
    // 不登记 `default_binding`**。`Ctrl+Shift+W` 在分屏保留键表里一直是 `ClosePane`（稿 §23⑶ 的那处不符
    // 就是这句），把关窗再绑一枚键要动的是既有键位表，需求没要求它，故键位留给用户自己绑
    // （`SPEC.FEAT.PREF.04` 的可重绑面）。落点窗按稿 D7 取活动窗；判据与确认框与点 × 那条**同一条**，
    // 所以这里的动作只是把窗交给 `request_close_window`——同一个动作不因发起方式是鼠标还是回车而少问一句。
    au::Command close_window;
    close_window.id = "workspace.close_window";
    close_window.title = borealis::ui::settings_label("workspace.action.close_window");
    close_window.category = "workspace";
    close_window.action = [&command_window, &request_close_window]() -> void {
        WorkspaceWindow *const resolved = command_window ? command_window() : nullptr;
        if (resolved == nullptr) {
            AURORA_LOG_INFO("main", "close window: there is no window to close");
            return;
        }
        if (request_close_window) {
            request_close_window(*resolved);
        }
    };
    close_window.scope = au::ShortcutScope::Global;
    app.commands().add(std::move(close_window));

    // 全屏切换（`SPEC.FEAT.WS.06`，判据文 §3 D1）：F11 在普通与全屏之间切换，ESC 不退出（避免与会话内 ESC 冲突）。
    au::Command toggle_fullscreen;
    toggle_fullscreen.id = "workspace.toggle_fullscreen";
    toggle_fullscreen.title = borealis::ui::settings_label("workspace.action.toggle_fullscreen");
    toggle_fullscreen.category = "workspace";
    // 全屏落点按窗解析（稿 D7）：`app.window()` 恒是主宿主那一只（稿 §0 的 F4，`itest_workspace_window`
    // 第四条已钉），在 B 窗按 `F11` 却把 A 窗拉全屏就是答非所问。取不到窗（主窗自己已散）时**不动任何
    // 一扇**——批 3-C 那版在这里回的是应用级主宿主，而那一只会与已散的主窗别名一起失效（批 4-A 收口）。
    toggle_fullscreen.action = [&app, &command_window]() -> void {
        WorkspaceWindow *const resolved = command_window ? command_window() : nullptr;
        if (resolved == nullptr) {
            return;
        }
        au::WindowHost *const host_of_target = app.window_host(resolved->id);
        au::Window *const target = host_of_target == nullptr ? nullptr : host_of_target->window();
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
    // `Ctrl+Shift+T` 捞走，而捞回去的那一张挂在 A 窗的标签栏里。取不到窗（主窗自己已散）就当场不做事
    // 并留一条日志：栈属于哪一扇已经无从谈起，把别人的栈 pop 出来挂进某一扇活着的路窗是造事实。
    au::Command undo_close_tab;
    undo_close_tab.id = "tab.undo_close";
    undo_close_tab.title = borealis::ui::settings_label("tab.action.undo_close");
    undo_close_tab.category = "workspace";
    undo_close_tab.action = [&command_window, &settings, &make_local_session, &next_tab_id,
                             &make_local_pane_factory, &restore_tab_at, &decode_tab_name,
                             &flush_workspace_snapshot, &connect_profile_to]() -> void {
        WorkspaceWindow *const resolved = command_window ? command_window() : nullptr;
        if (resolved == nullptr) {
            AURORA_LOG_INFO("main", "undo close tab: there is no window to restore into");
            return;
        }
        WorkspaceWindow &window = *resolved;
        auto spec_opt = window.closed_tab_stack.pop();
        if (!spec_opt.has_value()) {
            AURORA_LOG_INFO("main", "undo close tab: stack is empty");
            return;
        }
        const borealis::ui::ClosedTabSpec &spec = spec_opt.value();

        // SSH 腿（`SPEC.FEAT.WS.10`）：按档案重连，**不是**按关闭时的字节复制一条连接——档案这
        // 期间改过主机就以现在那一份为准；quick connect 与已删档案才用快照兜底。凭据仍走
        // `connect_profile_to` 的三路线（None 直连 / Stored 后台取 / Ask 询问），故撤销回来的一格
        // 与侧栏点开的一格共用同一条取料链，不会出现「重开时静默拿空口令硬连」。
        if (spec.leg == borealis::ui::ClosedTabLeg::Ssh) {
            const auto profile = borealis::ui::resolve_closed_ssh_profile(settings.profiles, spec);
            if (!profile.has_value()) {
                AURORA_LOG_WARN("main", "undo close tab: SSH profile '", spec.profile_id,
                                "' is gone and the closed tab carries no snapshot");
                return;
            }
            RestoreAnchor anchor{.window_id = window.id,
                                 .index_in_strip = spec.index_in_strip,
                                 .name = decode_tab_name(spec.name)};
            connect_profile_to(*profile, std::move(anchor));
            return;
        }

        // 本地腿：重建会话并回插原位（需求原文「本地终端重连进程」）。
        borealis::conn::LocalTerminalSpec conn_spec;
        conn_spec.command_line =
            spec.local_shell.empty() ? settings.connection.local_shell : spec.local_shell;
        conn_spec.working_directory =
            spec.startup_directory.empty() ? settings.connection.startup_directory
                                           : spec.startup_directory;
        const borealis::ui::TabId id = next_tab_id++;
        // 切分工厂走**缺省本地腿**那一支：新格按被切那一格现读的 OSC 7 继承目录，而不是照闭包栈里
        // 那份旧规格复制——闭包栈存的是「这张标签当初怎么开的」，不是「这一格现在在哪」。
        if (!restore_tab_at(window, id, make_local_session(conn_spec), decode_tab_name(spec.name),
                            spec.index_in_strip, borealis::conn::ConnectionType::Local,
                            make_local_pane_factory(id), conn_spec)) {
            return;
        }
        // 布局变更的提交点（稿 D2①「开标签」）：撤销重开不经过 `create_new_tab_with` 那支壳，
        // 故在这里补一次；`insert_at` 失败早退的那一支不到这里——栏里没这一格，快照也不该有。
        flush_workspace_snapshot();
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
            replace_pane_session(id, make_local_session(spec), borealis::term::LineEnding::Lf, spec);
            AURORA_LOG_INFO("main", "restart: tab ", id, " local session replaced");
            return;
        }

        const borealis::conn::SshProfile ssh = owned->second;
        const auto ask = borealis::conn::secret_ask_for(borealis::conn::normalized_auth_method(ssh));
        std::optional<std::string> material{};
        if (const auto held = ssh_secret_by_tab.find(id); held != ssh_secret_by_tab.end()) {
            material = held->second.target;
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
        ask_secret(ask, ssh.host);
    };

    // `session.restart` 命令（`SPEC.FEAT.WS.05`）：与浮层按钮同源，判据是「同一份闭包、两处入口」。
    // 快捷键 `Ctrl+Shift+R`（先前拍板建议档）——不与本仓分屏保留位十五条中的任何一条同形（`workspace_key_bindings`）。
    // 命令这一路没有派发来源，落点窗问 tracker（稿 D7）；取不到窗只剩主窗自己已散那一档，此时不动作
    //（批 4-A 收掉了这里的主窗别名兜底——别名指着的那一扇正是已经没了的那一扇）。
    au::Command restart_session;
    restart_session.id = "session.restart";
    restart_session.title = borealis::ui::settings_label("session.restart.title");
    restart_session.category = "workspace";
    restart_session.action = [&restart_current_tab, &command_window]() -> void {
        if (!restart_current_tab) {
            return;
        }
        WorkspaceWindow *const resolved = command_window ? command_window() : nullptr;
        if (resolved == nullptr) {
            AURORA_LOG_INFO("main", "restart refused: there is no window to restart a tab in");
            return;
        }
        restart_current_tab(*resolved);
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
    // 落点账目与命令面板同一形态（稿 D6）：初始挂首格那一份宿主，此后每次开框先搬到发起窗。留序号而不是
    // 留一枚 `bool mounted` 的理由就在搬窗这一步——没有序号就摘不到自己那一层。
    MountedOverlay serial_baud_mount;
    remount_overlay(serial_baud_dialog, serial_baud_mount, *host);
    auto show_serial_baud_frame = [&](au::Node content) -> void {
        // 浮层落点按发起窗：这一只框由 `serial.set_baud` 命令唤起，命令闭包拿不到派发来源（稿 §0 的 F5），
        // 落点只能问 tracker。取不到窗即按它现在挂着的那一扇（口径①），不是退回主窗别名。
        WorkspaceWindow *const window = command_window ? command_window() : nullptr;
        if (window != nullptr) {
            remount_overlay(serial_baud_dialog, serial_baud_mount, *window->overlay_host);
        }
        serial_baud_dialog->set_content(std::move(content));
        serial_baud_dialog->show();
    };
    auto show_serial_baud_notice = [&](std::string_view key, std::string_view arg_name,
                                       const std::string &arg) -> void {
        const std::string title = borealis::ui::settings_label("serial.set_baud.title");
        const std::string message = arg.empty()
                                        ? borealis::ui::settings_label(key)
                                        : borealis::ui::settings_label(
                                              key, {{std::string{arg_name}, arg}});
        auto dialog = serial_baud_dialog;
        // alert 的 OK 钮不自带关闭：关掉它必须由调用方给回调（`au::alert` 的 on_ok 那一支）。
        show_serial_baud_frame(au::alert(title, message, [dialog]() -> void { dialog->close(); }));
    };
    auto show_serial_baud_picker = [&](borealis::conn::SerialConnection &connection) -> void {
        const std::string title = borealis::ui::settings_label(
            "serial.set_baud.current", {{"baud", std::to_string(connection.current_baud())}});
        auto dialog = serial_baud_dialog;
        auto rows = std::vector<au::Node>{};
        // 标题行即「当前 {0}」：当前档只有连接腿那一个真值源，框里别处不再重复显示（D6② 的
        // 「档案值 vs 当前值分开展示」靠这一行与设置页/档案编辑器的分工）。
        auto head = std::make_shared<au::Text>(au::TextProps{
            .content = title,
            .text_color = borealis::ui::settings_chrome().text,
        });
        head->set_modifier(au::Modifier{}.fill_max_width());
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
                        // 具名形参的**名**随分支走：applied 槽位叫 `baud`、rejected 叫 `reason`。
                        const bool applied = connection.set_baud(rate);
                        show_serial_baud_notice(applied ? "serial.set_baud.applied"
                                                        : "serial.set_baud.rejected",
                                                applied ? "baud" : "reason", std::to_string(rate));
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
    // 窗决定；取不到窗即回空号，走 `not_serial` 那一支。注意**浮层落点**此刻仍挂按值留下的那一枚宿主
    // （`show_serial_baud_frame` 捕的是 app 级 `host`），改它属稿 D6「面板搬窗」＝批 4-C，见 §17 的在册欠项。
    set_serial_baud.action =
        [&serial_by_tab, &command_tab_id, &show_serial_baud_picker,
         &show_serial_baud_notice]() -> void {
        const std::optional<borealis::ui::TabId> target = command_tab_id();
        if (!target.has_value()) {
            show_serial_baud_notice("serial.set_baud.not_serial", "", "");
            return;
        }
        const auto bound = serial_by_tab.find(target.value());
        if (bound == serial_by_tab.end() || bound->second.connection == nullptr) {
            show_serial_baud_notice("serial.set_baud.not_serial", "", "");
            return;
        }
        show_serial_baud_picker(*bound->second.connection);
    };
    app.commands().add(std::move(set_serial_baud));

    // ---- 字节监视卡（`SPEC.FEAT.CONN.05` 的两条延后子项，稿 UI_SERIAL_MONITOR §4 批 3；五期稿 D36
    // 把入口从串口一型扩到四型）----
    //
    // 绑定在「从关闭态打开」那一刻按当时的落点标签**与焦点格**写下（稿 D8①：面板不跟标签跑——看着 A
    // 的时候切到 B，屏面不该自己换内容；稿 D37：格同理，切焦点不换环），之后每帧只经这几枚句柄读那
    // 一格的环与身份料。
    // `weak_ptr` 而不是 `shared_ptr`：面板不许为了让已关掉的标签还能「回看历史」而把整条会话（含平台腿与
    // 读线程）留在内存里——会话散了即取不到环，卡上是空态，标题那格因失去身份而整格不画。
    std::optional<borealis::ui::TabId> monitor_tab{};
    std::optional<borealis::ui::PaneId> monitor_pane{};
    std::weak_ptr<borealis::session::Session> monitor_session{};
    auto monitor_tap = [&monitor_session]() -> std::shared_ptr<borealis::session::RawTap> {
        const auto session = monitor_session.lock();
        return session == nullptr ? nullptr : session->raw_tap();
    };

    borealis::ui::SessionMonitorPanel::Hooks monitor_hooks;
    monitor_hooks.snapshot = [&monitor_tap]() -> borealis::ui::SessionMonitorPanel::Snapshot {
        const auto tap = monitor_tap();
        if (tap == nullptr) {
            return {};
        }
        // revision 先读、runs 后取（D17①）：反序时两次读之间的写入会让新数据被当成旧帧跳过。
        const auto revision = tap->revision();
        const auto cap_bytes = tap->max_bytes();
        return borealis::ui::SessionMonitorPanel::Snapshot{
            .runs = tap->snapshot(),
            .dropped_bytes = tap->dropped_bytes(),
            .revision = revision,
            .cap_bytes = cap_bytes,
        };
    };
    monitor_hooks.clear = [&monitor_tap]() -> void {
        if (const auto tap = monitor_tap(); tap != nullptr) {
            tap->clear();  // 本期唯一的止血手段（D7①）：关面板与关标签都不清屏
        }
    };
    // 标题那格的绑定＝档案名 · 连接目标，两条都**现读**（档案改名、重启换设备路径、换端口都在里面），
    // 本层不另存第三份。取不到档案名（quick connect 那类没档案身份的标签）就只留目标半串。
    // 五期（稿 D37）：绑定单位是**开卡那一刻的焦点格**——会话与本地规格都按 `PaneId` 存，标签级绑定在
    // 分屏标签上会给出「字节来自这一格、身份来自那个标签」的错配。判「卡还连着谁」因此是先看那一格
    // 的会话还在不在（散了即整格不画），再按 `tab_kind` 从四张表里取那一型的料（D36）。
    monitor_hooks.binding_label =
        [&monitor_tab, &monitor_pane, &tab_workspaces, &tab_kind, &serial_by_tab, &ssh_profile_by_tab,
         &telnet_by_tab, &tab_profile_id, &store]() -> std::string {
        if (!monitor_tab.has_value() || !monitor_pane.has_value()) {
            return {};
        }
        const auto held = tab_workspaces.find(monitor_tab.value());
        if (held == tab_workspaces.end() ||
            held->second.sessions_by_pane.find(monitor_pane.value()) == held->second.sessions_by_pane.end()) {
            return {};  // 标签或那一格散了：绑定失去身份（卡与格同生死）
        }
        const auto kind = tab_kind.find(monitor_tab.value());
        if (kind == tab_kind.end()) {
            return {};
        }
        // 取料纪律照采集腿（稿 F15）：四型的料分别挂在 `TabId` 那三张表和按 `PaneId` 存的本地规格上，
        // 查不到就留空 optional，由折算件回空串——「这一格散了」与「这一型本来不挂标签级档案」都表达为
        // 空，不造第二份「未知设备」的假身份。
        borealis::ui::MonitorBindingSource source;
        source.kind = kind->second;
        switch (kind->second) {
            case borealis::conn::ConnectionType::Serial:
                if (const auto found = serial_by_tab.find(monitor_tab.value());
                    found != serial_by_tab.end()) {
                    source.serial = found->second.profile;
                }
                break;
            case borealis::conn::ConnectionType::Ssh:
                if (const auto found = ssh_profile_by_tab.find(monitor_tab.value());
                    found != ssh_profile_by_tab.end()) {
                    source.ssh = found->second;
                }
                break;
            case borealis::conn::ConnectionType::Telnet:
                if (const auto found = telnet_by_tab.find(monitor_tab.value());
                    found != telnet_by_tab.end()) {
                    source.telnet = found->second;
                }
                break;
            case borealis::conn::ConnectionType::Local:
                if (const auto found = held->second.local_specs_by_pane.find(monitor_pane.value());
                    found != held->second.local_specs_by_pane.end()) {
                    source.local = found->second;
                }
                break;
        }
        const std::string target = borealis::ui::monitor_binding_target(source);
        if (target.empty()) {
            return {};
        }
        std::string name;
        if (const auto id = tab_profile_id.find(monitor_tab.value()); id != tab_profile_id.end()) {
            for (const auto &candidate : store.settings().profiles) {
                if (candidate.id == id->second) {
                    name = candidate.name;
                    break;
                }
            }
        }
        if (name.empty()) {
            return target;
        }
        return borealis::ui::settings_label("session.monitor.binding",
                                           {{"name", name}, {"path", target}});
    };
    // 文本视图的解码腿（稿 §11 D28①）：交的是那条会话**构造期折好的实际生效腿名**，不是设置现值——
    // 编码是建会话那一刻取一次的构造期值（`SPEC.FEAT.TERM.09`），改过设置再按现值去解历史字节就是拿错的
    // 钥匙开旧的锁；折好的腿名也与会话真正在跑的那台解码器同源（裁决 7.104 D6②），故别名（`GB2312` 那类）
    // 不会在卡片上报成另一条腿。会话散了交回缺省档，面板侧不另立第二套回落口径。
    monitor_hooks.encoding_name = [&monitor_session]() -> std::string {
        const auto session = monitor_session.lock();
        if (session == nullptr) {
            return std::string{"UTF-8"};
        }
        return std::string{session->encoding_name()};
    };

    // 三期「取出去」（稿 §10.4 批 B）：面板只做显示，三条腿的 OS 接触面都收在装配层。
    // 剪贴板走全仓唯一写点（与密钥面板「一键复制」同一入口）；导出先问路径再写盘，问不到（取消或
    // 对话框起不来）就交回空串——面板据此整条不跑、连留痕都不写（D21②）。
    // 这两条都在**点击派发**里做 IO，口径与 `SettingsPanel::run_export()` 同源，判据见稿 §10.5：
    // 一次性、用户显式发起、毫秒级折算，后面本来就跟着原生对话框与写盘这两段阻塞 IO。
    monitor_hooks.copy_text = [](std::string text) -> void {
        borealis::session::ClipboardOutbox::write(text);
    };
    monitor_hooks.pick_export_path = [&monitor_tab]() -> std::string {
        // 缺省名与内容同源（G48 回货后本仓唯一的接线动作）：`session-<标签>-<epoch 秒>.txt`，与
        // `session::make_log_file_name()` 那族 `session-<tab>-<epoch>.log` 同一形状——两个数都取自
        // 「这一次点导出」的现场，纯 ASCII 因此不含 OS 会拒的非法字符，也不必替用户猜设备名或档案名
        // （档案名可以是中文，而对话框那格的名字栏要逐字节走得通码页这一关，本仓不替用户赌）。
        if (!monitor_tab.has_value()) {
            return {};
        }
        const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        return picked_save_path("Export session byte dump", {{"Text", {"*.txt"}}},
                                "session-" + std::to_string(monitor_tab.value()) + "-" +
                                    std::to_string(epoch) + ".txt");
    };
    monitor_hooks.export_text = [](const std::string &path, const std::string &text)
        -> std::optional<std::string> { return write_private_dump(path, text); };

    borealis::ui::SessionMonitorPanel monitor_panel{*host, std::move(monitor_hooks)};
    // 一句提示沿用波特率那一只框（`serial_baud_dialog` + `show_serial_baud_frame` 那对既有件）：
    // 「两只框共用一只 Dialog 换 content」在 `close_confirm` 与多行粘贴确认那里已是先例，再开第二只
    // `Dialog` 与第二枚 `MountedOverlay` 就要多维护一套落点账，而这两句话本来不会同时在场。
    auto show_monitor_notice = [&serial_baud_dialog, &show_serial_baud_frame](std::string_view key) -> void {
        const std::string title = borealis::ui::settings_label("session.monitor.title");
        const std::string message = borealis::ui::settings_label(key);
        auto dialog = serial_baud_dialog;
        show_serial_baud_frame(au::alert(title, message, [dialog]() -> void { dialog->close(); }));
    };

    // `session.monitor.toggle` 命令（稿 D8① + 五期 D36）：分流判据从「当前标签在 `serial_by_tab` 里有没有
    // 身份」改成「**这一格有没有会话**」——四型的会话都是 `Session`，采集腿（`RawTap`）本来就不分型，把入口
    // 卡在串口一张表上等于把一张通用的机制切掉四分之三（稿 F16／D36①）。开卡只留一句话给真的没有会话的
    // 落点（空标签、连接尚未建立的中间态），**不武装任何采集**（未武装的会话在采集点判一次指针就走，这是
    // §5 那条「未开卡的会话零成本」的落地，稿 D41：因果是「未武装」而不是「非串口」）。同一条命令再按一次
    // 是关闭：关闭既不停采集也不清环，重开看到的就是刚才那一段（D7①）。
    au::Command toggle_session_monitor;
    toggle_session_monitor.id = "session.monitor.toggle";
    toggle_session_monitor.title = borealis::ui::settings_label("session.monitor.command");
    toggle_session_monitor.category = "terminal";
    toggle_session_monitor.action =
        [&monitor_panel, &monitor_tab, &monitor_pane, &monitor_session, &retarget_panel, &command_tab_id,
         &focused_pane_session, &focused_pane_of, &show_monitor_notice]() -> void {
        retarget_panel(monitor_panel);  // 搬窗（稿 D6，本件是面板族第七只——侧栏改常驻列后出了族，裁决 7.180）
        if (monitor_panel.is_open()) {
            monitor_panel.toggle();
            return;
        }
        const std::optional<borealis::ui::TabId> target = command_tab_id();
        const auto session =
            target.has_value() ? focused_pane_session(target.value()) : nullptr;
        // 格号现取一次并**存下来**（稿 D37）：`focused_pane_of` 在标签或工作区不存在时回 0，那正是
        // 「这一格无从属」的形态，与 `session == nullptr` 同一条 fail-closed 分支。
        const borealis::ui::PaneId pane = target.has_value() ? focused_pane_of(target.value()) : 0U;
        if (session == nullptr || !target.has_value() || pane == 0U) {
            show_monitor_notice("session.monitor.not_monitorable");
            return;
        }
        monitor_tab = target;
        monitor_pane = pane;  // 绑定单位是这一刻的焦点格（稿 D37），不是标签
        monitor_session = session;
        static_cast<void>(session->enable_raw_tap());  // 幂等：重开看到的是历史而不是空白
        monitor_panel.open();
    };
    app.commands().add(std::move(toggle_session_monitor));

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
    // 落点账目同命令面板与波特率框（稿 D6）：初始挂首格，开框时按发起窗搬。
    MountedOverlay log_dialog_mount;
    remount_overlay(log_dialog, log_dialog_mount, *host);
    auto log_arg = [](std::string_view name, const std::string &text) -> std::vector<au::MessageArg> {
        return {{std::string{name}, text}};
    };
    auto show_log_frame = [&](au::Node content) -> void {
        WorkspaceWindow *const window = command_window ? command_window() : nullptr;
        if (window != nullptr) {
            remount_overlay(log_dialog, log_dialog_mount, *window->overlay_host);
        }
        log_dialog->set_content(std::move(content));
        log_dialog->show();
    };
    auto show_log_notice = [&](std::string_view key, std::string_view arg_name,
                                const std::string &arg) -> void {
        auto dialog = log_dialog;
        const std::string message = arg.empty()
                                        ? borealis::ui::settings_label(key)
                                        : borealis::ui::settings_label(key, log_arg(arg_name, arg));
        show_log_frame(au::alert(borealis::ui::settings_label("session.log.confirm.title"), message,
                                 [dialog]() -> void { dialog->close(); }));
    };
    // 当前标签**活动格**的那一份会话（`SPEC.FEAT.WS.02` 稿 §6 第一行）：会话日志的两条钩子与
    // `session.restart` 吃的是同一个量。一格一会话之前这里是 `sessions.front()`，两格指向同一份会话时
    // 「front」恰好还是用户看着那一格；现在必须按焦点格取，否则在副格开日志会写到主格那条连接上。
    auto current_tab_session = [&command_tab_id, &focused_pane_session]()
        -> std::shared_ptr<borealis::session::Session> {
        const std::optional<borealis::ui::TabId> target = command_tab_id();
        if (!target.has_value()) {
            return {};
        }
        return focused_pane_session(target.value());
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
    logging_toggle_action = [&store, &command_tab_id, &current_tab_session, &log_dialog, &show_log_frame,
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
            show_log_notice("session.log.alert.bad_pattern", "pattern",
                              now.connection.session_log_pattern);
            return;
        }
        const std::string directory = borealis::session::resolve_log_directory(
            now.connection.session_log_dir, store.report().file.parent_path().string());
        if (directory.empty()) {
            show_log_notice("session.log.alert.cannot_open", "path", std::string{});
            return;
        }
        // 落盘文件名里的号取自**当前落点窗**：会话已经从这一号取出来了，此处再取不到即意味着
        // 落点窗在两次现取之间散了（同一帧内不会发生，但 fail-closed 不靠「不会发生」）。
        const std::optional<borealis::ui::TabId> tab_id_for_log = command_tab_id();
        if (!tab_id_for_log.has_value()) {
            return;
        }
        const borealis::ui::TabId tab_id = tab_id_for_log.value();
        const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        const std::string path = (std::filesystem::path{directory} /
                                  borealis::session::make_log_file_name(tab_id, epoch))
                                     .string();
        // `au::confirm` 只吃一条 message，故判据 3 那两句（敏感提醒 + 完整路径）合成一行。
        const std::string body = std::string{borealis::ui::settings_label("session.log.confirm.sensitive")} +
                                 " " + borealis::ui::settings_label("session.log.confirm.path",
                                                                        {{"path", path}});
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
                // 快照即契约（裁决 7.143）：目录、文件名与打码器都在弹框那一刻算定并写进正文，
                // 答话时只做 IO、不重读配置。否则用户在框开着时改了目录，就会出现「答的是屏上这条
                // 路径、开的却是另一条」——确认框与副作用脱钩，判据 3 的风险提示会指错文件。
                // 配置在两步之间失效（目录不可建、文件打不开）仍走 `cannot_open` 的 fail-closed。
                if (const auto failure = ensure_log_directory(std::filesystem::path{directory});
                    failure.has_value()) {
                    show_log_notice("session.log.alert.cannot_open", "path", *failure);
                    return;
                }
                borealis::session::LogSpec spec;
                spec.path = path;
                spec.timestamp = true;  ///< D8①：会话级参数，形态固定「每行 + 本地秒」，不进 schema。
                spec.redactor = redactor;
                if (target->enable_logging(std::move(spec)) == borealis::session::LogOutcome::CannotOpen) {
                    show_log_notice("session.log.alert.cannot_open", "path", path);
                }
                // 标题的翻面不在这里补一次：本棒只有一个刷新点（下面 `set_on_frame` 里那一次比较）。
            }));
    };

    // 调试面板（`SPEC.NF.RELI.01`）：F12 开合，打开那一刻对**全部标签的每个会话**取一次计数器快照
    // （解析降级 / 非法字节 / 发送侧不可表示 / 背压水位四族）。快照而非活读：面板只在打开时被填一次，之后不再触碰会话，
    // 于是 `Session` 的四个 stats 取数点只在主线程这一次调用上发生锁竞争。崩溃留存腿走另一条路
    // （自写诊断快照，裁决 7.142）：四族计数器不进去——取数要碰会话锁，信号上下文不安全。
    // 宿主与焦点权威**成对**交进来（稿 §0 的 F3：`app.focus()` 恒指宿主表里的主窗那一份，跟着
    // `main_host_` 易主）：这一对在启动时是首格那扇窗，之后每次搬窗由 `set_overlay_destination` 一起换，
    // 于是「压进哪一栈就由哪一栈弹出」那条不变量不会因中间有人关了主窗而断掉。
    borealis::ui::DebugPanel debug_panel{*host, window_focus(*workspace_windows.front())};
    au::Command open_diagnostics;
    open_diagnostics.id = "diagnostics.open";
    open_diagnostics.title = borealis::ui::settings_label("diagnostics.action.open");
    open_diagnostics.category = "workspace";
    open_diagnostics.action = [&debug_panel, &tab_workspaces, &retarget_destination]() -> void {
        // 搬窗（稿 D6）：诊断框的浮层与焦点权威一起换到发起这一条的窗上（F12 经命令层进来，落点问
        // tracker）。快照内容与挂在哪儿无关，这一跳只决定「看得见的那一扇」。
        retarget_destination(debug_panel);
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
                        {{"tab", static_cast<std::int64_t>(tab_id)},
                         {"pane", static_cast<std::int64_t>(pane)}}),
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

    // 启动重放 `shortcuts.overrides`（`SPEC.FEAT.PREF.04`，任务书 §5）：这一段必须排在投影**之前**——
    // `set_binding` 在注册表尚未投影时只改 `default_binding`，随后 `bind_shortcuts` 一次性建立投影；
    // 反过来写就会先按缺省绑定建一遍、再逐条撤建。解析与跳过口径落在 `replay_shortcut_overrides`
    // （畸形串与未知 id 各留一条 warn，一条坏覆盖不拦启动、也不静默失效），装配层只负责排序。
    static_cast<void>(
        borealis::config::replay_shortcut_overrides(app.commands(), settings.shortcuts.overrides));
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
        // `--new-window`（稿 D10，批 6 放开）：转交侧把它落成一扇新窗，窗里那一格吃随行的 `--profile` /
        // `--cwd`。冷启动那一侧不另开——正要建的这一扇就是它，上面 `cold_first_tab_is_tab_action` 已按
        // `new-tab` 同一条腿消化掉。旗标与动作两个都认：本版 CLI 两者同真，而旧版转来的帧只有旗标
        // （动作还是 `new-tab`），用户的这一根手指不该随版本错开而失效。
        const bool wants_new_window =
            forwarded && (pending.new_window ||
                          pending.action == borealis::cli::StartupAction::NewWindow);
        if (wants_new_window && pending.action == borealis::cli::StartupAction::SplitPane) {
            // 纯逻辑件产不出这一格（`--new-window` 与两个子命令互斥），能走到这里的只有手工拼的帧：
            // 「开一扇新窗去切另一扇窗的焦点格」没有可读的语义，整条拒绝而不是猜一半。
            return "--new-window cannot be combined with split-pane";
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
            // 切腿的落点同样按窗现取：CLI 帧从管道线程转交到主线程，那一刻活动窗可能已经散了；
            // 取不到就回诊断，不猜「大概是主窗」（稿 D7 的同一条 fail-closed 档）。
            const std::optional<borealis::ui::TabId> split_target = command_tab_id();
            borealis::ui::WorkspaceView *workspace = nullptr;
            if (split_target.has_value()) {
                const auto found = tab_workspaces.find(split_target.value());
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
                const auto kind = tab_kind.find(split_target.value());
                if (kind == tab_kind.end() || kind->second != borealis::conn::ConnectionType::Local) {
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
        // 落点窗：`--new-window` 是「现开一扇再往里放」，其余仍取 tracker 记下的最近被碰过的那一扇
        //（稿 §6 批 4-A：取不到即窗已散 ⇒ 整条请求作废并回诊断，不再像旧腿那样吃主窗别名）。
        WorkspaceWindow *landing = nullptr;
        if (wants_new_window) {
            landing = open_new_window ? open_new_window() : nullptr;
            if (landing != nullptr) {
                // 四条档案腿（ssh / local / serial / telnet）都经 `tab_host_or_none` 现取落点，故这里把
                // 刚落地的新窗交进 tracker，随行的 `--profile` 才连进**这一扇**而不是用户上一回碰过的
                // 那一扇。新窗此刻也是 OS 焦点窗、下一笔派发照会报它，这一跳只是把同一件事说在前面——
                // 说要紧的那一处是凭据询问那条异步腿：框答完再连时读的仍是 tracker。
                active_window_tracker.report(landing->id);
            }
        } else {
            landing = tab_host_or_none("startup request");
        }
        if (landing == nullptr) {
            return wants_new_window ? "failed to open a new window for --new-window"
                                    : "there is no window to open the requested tab in";
        }
        if (profile != nullptr) {
            borealis::conn::Profile target = *profile;
            if (target.type == borealis::conn::ConnectionType::Local && cwd.has_value()) {
                target.local.working_directory = *cwd;  // 覆盖只进本次副本，档案本体不动（稿 §5.1）。
            }
            connect_profile(target);  // 四类分腿照旧，SSH 要材料时仍走既有凭据询问链。
        } else {
            create_new_tab(*landing, cwd);
        }
        rebuild_root_children(*landing, tab_workspaces);
        return {};
    };

    // 启动降级提示（`SPEC.FEAT.PREF.07`，裁决 7.76⑤）：`LoadOutcome` 四态里只有两条降级态会弹，弹一次即止。
    // `message` 是 ASCII 英文诊断，只进日志不上中文界面（判据文 S13①），故以「本次真的弹了」为条件——
    // 「哪两态弹」的判定只在 `StartupNotice` 里存一份，装配层不另做一遍 `outcome` 分支。
    borealis::ui::StartupNotice notice{*host, window_focus(*workspace_windows.front())};
    if (notice.show_if_needed(store.report())) {
        AURORA_LOG_WARN("main", "config load degraded: ", store.report().message);
    }

    app.set_on_frame([&tab_workspaces, &workspace_windows, &outbox, &panel, &app, &settings,
                      &has_live_session, &pane_session, &sftp_panel, &tunnel_panel,
                      &keys_panel, &logging_state_probe, &add_logging_command, &logging_command_shows,
                      &show_log_notice, &startup_mutex, &startup_queue, &apply_startup_request,
                      &frame_bytes, &reply_wire, &credential_prompt, &restore_waiting_for_secret,
                      &restore_secret_sink, &tab_kind, &crash_facts, &monitor_panel, &bell_policy,
                      &ring_bell, &quake_hotkeys, &refresh_status_bar_ui]() -> void {
        // 帘子热键的排空（`SPEC.FEAT.WS.12`，裁决 7.172）：框架的 `step_frame` 只排它**自己**那份
        // 注册表，本仓自持的这一份要在帧边界自己排——同一条理由：命中不在消息泵里同步执行，回调要
        // 改几何与显隐，不能在窗口过程帧里重入。
        if (quake_hotkeys != nullptr) {
            static_cast<void>(quake_hotkeys->drain_pending());
        }
        // 恢复的凭据挂起兜底（`SPEC.FEAT.WS.11` 批 5-C）：问句被**静默关掉**（Esc、搬窗时
        // `set_overlay_host` 的 close、宿主散了）不会回调 `on_cancel`，而那个缺口会让泵永远停在
        // 「等一个不会来的答案」上。帧边界是唯一能看见「框已经不在了」的地方：认领并中止余下重放，
        // 已恢复的格照旧收尾（与按钮取消同一条语义）。
        if (restore_waiting_for_secret &&
            (!credential_prompt || !credential_prompt->is_open())) {
            restore_waiting_for_secret = false;
            if (restore_secret_sink) {
                restore_secret_sink(std::nullopt);
            }
        }
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
                static_cast<void>(outbox.drain(*tag_session));

                // 系统通知（`SPEC.FEAT.INTEG.03`）：与上一行同一条排帧范围、同一条「取走即空」纪律。
                // 这里的 `(tab_id, pane)` 是 tag 的唯一来源——`Session` 自身没有标识，而 `PaneId`
                // 只在**本标签的树内**只增不复用，故会话级恒定的那一对号只有这一格同时可得。
                // 返回值刻意不看：一条远端发起的提醒不该改变排帧，投不出去的详情已在落地件留了诊断。
                static_cast<void>(borealis::session::NotificationOutbox::drain(*tag_session, tab_id,
                                                                              pane));

                // 会话日志的写失败提示（`SPEC.FEAT.CONN.11`）：会话侧只上弦一次，取走与弹窗都在帧边界，
                // 形态照上面 `outbox.drain` 与下面 `take_bell_triggered` 那两条（AGENTS §4.5 第 25 条）。
                if (const auto note = tag_session->take_logging_notice(); note.has_value()) {
                    // 提示语里的路径是 ASCII 诊断，上屏那句走词条，故日志留原文、界面只报「已停止」。
                    AURORA_LOG_WARN("main", "session log disabled after a write failure: ", *note);
                    show_log_notice("session.log.alert.write_failed", {}, {});
                }

                // BEL 事件（`SPEC.FEAT.WS.04`，裁决 7.159）：档位逐次现读，改档即时生效。取走即清零，
                // 不会被下一帧重复处置。节流只吞铃声、不吞角标：后台标签响过哪几格不能丢，而多格同时
                // 响时全局只出一声（`BellPolicy` 的 200 ms 窗口）。
                if (tag_session->take_bell_triggered()) {
                    switch (bell_policy.decide(settings.terminal.bell,
                                               std::chrono::steady_clock::now())) {
                        case borealis::session::BellAction::None:
                            break;
                        case borealis::session::BellAction::Badge:
                            owner.tab_strip.mark_bell_triggered(tab_id);
                            break;
                        case borealis::session::BellAction::BadgeAndTone:
                            owner.tab_strip.mark_bell_triggered(tab_id);
                            ring_bell();
                            break;
                    }
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

        // 崩溃快照的发布点（`SPEC.NF.RELI.01` 的留存腿，裁决 7.142）：每帧把结构计数与每格身份写进
        // 无锁双缓冲，崩溃处理函数只读这一份。会话文本、标签名、主机名与凭据一概不进——终端输出可能
        // 含口令，而会话日志默认关闭（裁决 7.9）；把屏幕内容搬进一份默认开启的崩溃文件等于绕过那条默认。
        // 逐帧只做一次原子与计数（不碰会话锁、不取标题），发布侧写的是非当前缓冲（见 `CrashFactsStore`）。
        {
            borealis::diag::CrashFactsSnapshot crash_snapshot;
            crash_snapshot.windows = static_cast<std::uint64_t>(workspace_windows.size());
            crash_snapshot.last_publish_epoch = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
            for (const auto &[tab_id, workspace] : tab_workspaces) {
                ++crash_snapshot.tabs;
                const auto kind = tab_kind.find(tab_id);
                for (const auto &[pane, session] : workspace.sessions_by_pane) {
                    ++crash_snapshot.panes;
                    if (crash_snapshot.session_count < borealis::diag::kMaxCrashSessions) {
                        crash_snapshot.sessions[crash_snapshot.session_count++] =
                            borealis::diag::CrashSessionFact{
                                .tab = tab_id,
                                .pane = pane,
                                .kind = kind == tab_kind.end() ? borealis::conn::ConnectionType::Local
                                                               : kind->second,
                                .alive = session->alive()};
                    }
                }
            }
            crash_facts.publish(crash_snapshot);
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
            // 屏 1 那条带的上下文串（裁决 7.181）：会话名取上面同一份 `title_utf8`——同一个判定处，
            // 标签条、OS 标题与这条带不会在手动重命名那一刻分叉。目录那一半只认**本地腿的焦点格**：
            // SSH／串口／Telnet 的 OSC 7 是远端语义或根本没有，与继承起始目录那一条判据同源。
            // 逐帧现算，`set_context` 在串没变时即空操作（与状态栏、OS 标题同一条差分纪律）。
            if (window.title_bar) {
                std::string directory;
                const auto owned = tab_workspaces.find(window.current_tab_id.value());
                const auto kind = tab_kind.find(window.current_tab_id.value());
                if (owned != tab_workspaces.end() && owned->second.owner == &window &&
                    kind != tab_kind.end() &&
                    kind->second == borealis::conn::ConnectionType::Local) {
                    const std::shared_ptr<borealis::session::Session> focused =
                        pane_session(window.current_tab_id.value(),
                                     owned->second.workspace->focused_pane());
                    if (focused != nullptr && focused->alive()) {
                        if (const auto parsed = borealis::ui::local_directory_from_osc7(
                                focused->osc_state().working_directory,
                                borealis::ui::osc7_path_style())) {
                            directory = *parsed;
                        }
                    }
                }
                window.title_bar->set_context(
                    borealis::ui::format_title_context(directory, title_utf8));
            }
        }

        // 状态栏（屏 1 判据 ⑨／⑩）：与上面那条标题差分同一条思路——逐帧现算、值没变即空操作。换标签、
        // 切焦点格、改字号／主题／scrollback／剪贴板档、会话断线都从这一条腿跟上，而不是在每一处改动
        // 旁边再补一句「记得刷状态栏」。
        refresh_status_bar_ui();

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
        monitor_panel.tick();  // 字节监视行表随环刷新（稿 D6①；关着即空操作，暂停中不读环）
    });

    // 关窗的两条入口共用一段（`SPEC.FEAT.WS.03` 稿 D3／D4，裁决 7.161）：点 × 与命令面板那条
    // 「关闭此窗」是同一次动作，于是窗级判据、确认框、答话之后的那一跳都只许有一份。
    //
    // 窗级活会话只做**汇总**：任一标签活着即算活，而「这张标签活不活」仍取关标签那枚
    // `has_live_session`——判据不复制第二份（确认稿 D2）。
    auto window_has_live_session = [&has_live_session](WorkspaceWindow &window) -> bool {
        for (const auto &tab : window.tab_strip.tabs()) {
            if (has_live_session(tab.id)) {
                return true;
            }
        }
        return false;
    };

    // 按窗一份的确认框（确认稿 D3）：与关标签那只分开——两件事可能同时发生，共用一只换 `content`
    // 会把另一句问题覆盖掉。生存期跨 Show→答话，故由窗账目持有。答「关」走 `app.close_window()`：
    // 程序化来源被闸门直接放行，帧末由既有撤账链收，本层不另写一条销毁路径（确认稿 D4）。
    auto ask_window_close = [&app](WorkspaceWindow &window) -> void {
        if (!window.window_close_confirm) {
            window.window_close_confirm = std::make_shared<au::Dialog>();
        }
        if (!window.window_close_confirm_mounted) {
            static_cast<void>(window.overlay_host->add_overlay(
                aurora::Node{std::static_pointer_cast<au::Widget>(window.window_close_confirm)}));
            window.window_close_confirm_mounted = true;
        }
        window.window_close_confirm->set_content(au::confirm(
            borealis::ui::settings_label("window.close_confirm.title"),
            borealis::ui::settings_label("window.close_confirm.body"),
            [&app, id = window.id, dialog = window.window_close_confirm](bool accepted) -> void {
                dialog->close();
                if (accepted) {
                    app.close_window(id);  // Programmatic：闸门直接放行，帧末走既有撤账链
                }
            }));
        window.window_close_confirm->show();
    };

    // 命令面板那一格的落点：先折成处置，再决定弹框还是直接关。`UserCommand` 与闸门的
    // `OperatingSystem` 在 `ui::window_close_disposition` 里同判据——这一条就是本棒存在的理由：
    // 「同一个动作换个发起方式就少问一句」不能成立。
    request_close_window = [&app, &window_has_live_session,
                            &ask_window_close](WorkspaceWindow &window) -> void {
        const auto disposition = borealis::ui::window_close_disposition(
            borealis::ui::WindowCloseInitiator::UserCommand, window_has_live_session(window));
        if (disposition == borealis::ui::WindowCloseDisposition::AskUser) {
            ask_window_close(window);
            return;
        }
        app.close_window(window.id);
    };

    // 关窗闸门（G44 回货的接货腿）：用户点 × 时先问一句，可否决。
    // 回 false 是**同步**答复——框架在 `on_frame` 之后、回收之前逐窗问一次（边沿闩），被否的窗
    // 当帧继续渲染；确认框在回调里 `show()` 即当帧上屏。
    //
    // 框架的三类来源折成本层的两档发起方：OS 关闭钮归 `OsCloseButton`，程序化（含确认后那一次）
    // 与策略连带都归 `AppInitiated`——恒不问。拿闸门挡自己发起的关闭会把退出序卡在一个没人能答、
    // 甚至没有窗口可挂的框上（确认稿 D1／任务书 m3）。命令面板那条不经这里：它没有向框架提关闭
    // 请求，问与关都由上面 `request_close_window` 自己办。取不到窗（已散／不归本层管）同样放行。
    app.set_on_close_requested(
        [&window_of, &window_has_live_session,
         &ask_window_close](au::WindowId id, au::CloseRequestSource source) -> bool {
            const auto initiator = source == au::CloseRequestSource::OperatingSystem
                                       ? borealis::ui::WindowCloseInitiator::OsCloseButton
                                       : borealis::ui::WindowCloseInitiator::AppInitiated;
            WorkspaceWindow *const window = window_of(id);
            if (window == nullptr) {
                return true;
            }
            if (borealis::ui::window_close_disposition(initiator, window_has_live_session(*window)) ==
                borealis::ui::WindowCloseDisposition::AskUser) {
                ask_window_close(*window);
                return false;  // 撤销本次关闭；用户确认后那条程序化路径再进来
            }
            return true;  // 没有活会话，或本来就不该问：不必问，直接关
        });

    // 关窗撤账链（`SPEC.FEAT.WS.03` 稿 §6 批 4-B）：一扇窗被框架收走时，挂在它名下的账目必须一起撤
    // 干净——否则横幅继续列一张已经不存在的标签、命令落点指向已经析构的对象、四张连接表里留着一条
    // 没人认领的绑定。稿 §5 第 4 条那句「窗拆后 `tab_workspaces`／四张连接表／广播成员三处账目为空」
    // 就是这一跳的判据。
    //
    // **触发点与稿 §6 原写的不同，理由在销毁序而不是时机**：稿里选的是「`set_on_frame` 里轮询
    // `should_close()`」（F17 那句「唯一销毁前可观测点」），但 `on_frame` 排在 `render_all` **之前**，
    // 而本层的对象一旦在那一跳里从容器 `erase`，同一帧仍会为这扇已请求关闭的窗布局与绘制——
    // `TabBarWidget` 与横幅的 hooks 捕的是 `WorkspaceWindow &`，读的就是刚被析构的那一格。宿主回收
    // 之后（此刻）这扇窗再不会被渲染，控件树随宿主一起散，本层的对象才是可以安全落地的时点。
    // 「销毁前要能看见关闭请求」那一半由上面的闸门承担（稿 D4，G44 回货后）：命中活会话先弹确认，
    // 本钩子只在窗**真被收走**之后撤账，两者时序不重叠。
    //
    // 代价照实记（F19）：关最后一扇时框架保留主宿主，这一跳**不触发**，于是那一扇的账目留到 `run()`
    // 返回之后由退出序统一收（本函数末尾那条逐窗关会话的腿，形态与批 4-C-2 之前的扫表读法等价），
    // 于是那一扇的账目不靠这条链。
    app.set_on_window_closed([&workspace_windows, &perform_close, &erase_tab_ledger, &tab_workspaces,
                              &broadcast_router, &active_window_tracker, &fallback_window,
                              &startup_wake, &retarget_panel_to, &abandon_destination_to,
                              &remount_overlay, &wizard, &credential_prompt, &panel,
                              &tunnel_panel, &sftp_panel, &keys_panel, &snippet_panel, &debug_panel,
                              &notice, &monitor_panel,
                              &command_palette, &serial_baud_dialog, &log_dialog, &palette_mount,
                              &serial_baud_mount, &log_dialog_mount,
                              &flush_window_geometry,
                              &flush_workspace_snapshot](au::WindowId closed_id) -> void {
        std::size_t index = workspace_windows.size();
        for (std::size_t probe = 0U; probe < workspace_windows.size(); ++probe) {
            if (workspace_windows[probe]->id == closed_id) {
                index = probe;
                break;
            }
        }
        if (index == workspace_windows.size()) {
            return;  // 不是本层的工作区窗：账目不归这里，也不许猜一扇
        }
        WorkspaceWindow &window = *workspace_windows[index];
        // `surface` 先抄下来：对象在下面 `erase` 那一刻就散，而唤醒指针的比较与重指要在那之后。
        au::Surface *const closing_surface = window.surface;
        // ① 本窗名下全部标签走与关标签**同一条**链。不弹确认：用户的关闭意图已在闸门处问过（稿 D4），
        //    此刻窗已回收，再问一句只会把「窗没了、账还在」挂在一整轮答话上。
        //    先取快照再逐张关：`perform_close` 会搬动栏位表，边遍历 `tabs()` 边关就是迭代器失效。
        std::vector<borealis::ui::TabId> strip_ids;
        for (const auto &tab : window.tab_strip.tabs()) {
            strip_ids.push_back(tab.id);
        }
        for (const borealis::ui::TabId id : strip_ids) {
            perform_close(window, static_cast<std::uint64_t>(id));
        }
        // ② 兜底清扫：归属是唯一真源，凡 `owner` 指着这扇窗而栏里已经没有它的格子（两处账目分叉的形态，
        //    例如某格建起来却没进栏），一律走同一支撤账并记一条 WARN——正常路径恒空，扫到即为真 bug，
        //    不能静默。这一跳才让 §5 那句「为空」成为结构性事实而不是巧合。
        //    先收号再撤：`erase_tab_ledger` 会 erase 掉正在被遍历的那一节，边扫边撤就是迭代器失效。
        std::vector<borealis::ui::TabId> strays;
        for (const auto &[id, workspace] : tab_workspaces) {
            if (workspace.owner == &window) {
                strays.push_back(id);
            }
        }
        for (const borealis::ui::TabId stray : strays) {
            AURORA_LOG_WARN("main",
                            "window retire: tab ", stray,
                            " is owned by a closed window but is not in its strip");
            broadcast_router.remove_tab(stray);
            erase_tab_ledger(window, stray);
        }
        // ③ 落点记录器撤账（F20：号只增不复用，撤掉的号永远不会再被返回）。
        active_window_tracker.forget(closed_id);
        // ③′ 浮层落点搬窗（稿 D6＝批 4-C）：面板与三只装配层直接挂进宿主的常驻浮层都是 **app 级一份**，
        //     挂在哪一扇由发起命令的那一扇决定；那一扇散了，「一份」不能跟着散，得搬到还落得下去的窗上。
        //     这一跳必须排在 ④ **之前**：setter 与重挂都要从**旧宿主**按记下的序号摘回，而旧宿主随本层对象
        //     一起散（主窗那一份另有 `host` 那枚按值副本续命，其余窗没有这层保险）。
        //     落点是「除这一扇之外的第一扇」而不是 `command_window()`：关闭不是一条来自某扇窗的命令，
        //     没有派发事实可依，与 ⑤⑥ 那两跳同一条兜底口径；一扇都不剩即空落点，各处按口径 ① 原地不动
        //     （稿 F19：关最后一扇时框架保留主宿主，本钩子根本不触发，那一档走的是 `run()` 之后的退出序）。
        //     两只常驻框另走 `abandon_overlay_destination`——旧窗的焦点管理器在 ④ 之前就已被框架回收
        //     （aurora 主仓 `06-app-platform.md` §3「宿主被回收之后触发」），那条 `set_overlay_destination`
        //     里的 `pop_scope()` 会落手在已析构的对象上。
        WorkspaceWindow *landing_window = nullptr;
        for (const std::unique_ptr<WorkspaceWindow> &candidate : workspace_windows) {
            if (candidate.get() != &window) {
                landing_window = candidate.get();
                break;
            }
        }
        au::OverlayHost *const closing_host = window.overlay_host.get();
        auto retire_panel = [closing_host, landing_window, &retarget_panel_to](auto *panel) -> void {
            if (panel != nullptr && panel->overlay_host() == closing_host) {
                retarget_panel_to(*panel, landing_window);
            }
        };
        auto retire_destination = [closing_host, landing_window, &abandon_destination_to](auto *panel) -> void {
            if (panel != nullptr && panel->overlay_host() == closing_host) {
                // 无落点时助手原地不动（口径 ①）：此刻旧宿主已被 ④ 收走，本件的账由析构收尾。
                abandon_destination_to(*panel, landing_window);
            }
        };
        auto retire_mount =
            [closing_host, landing_window, &remount_overlay](const auto &widget, MountedOverlay &state) -> void {
                if (landing_window != nullptr && state.on == closing_host) {
                    remount_overlay(widget, state, *landing_window->overlay_host);
                }
            };
        // 常驻列不在这张表里：它随本窗一起散（每窗一份，成员析构就是撤账，没有要搬到别处的浮层）。
        retire_panel(wizard.get());
        retire_panel(credential_prompt.get());
        retire_panel(&panel);
        retire_panel(&tunnel_panel);
        retire_panel(&sftp_panel);
        retire_panel(&keys_panel);
        retire_panel(&snippet_panel);
        retire_panel(&monitor_panel);
        retire_destination(&debug_panel);
        retire_destination(&notice);
        retire_mount(command_palette, palette_mount);
        retire_mount(serial_baud_dialog, serial_baud_mount);
        retire_mount(log_dialog, log_dialog_mount);
        // ④ 本层对象落地：此刻宿主已散，控件树只剩这一格持着的 `shared_ptr`，随它一起散。
        workspace_windows.erase(workspace_windows.begin() + static_cast<std::ptrdiff_t>(index));
        // ⑤ 兜底落点易主：主窗自己散了才换（`forget` 之后取不到任何落点即那一档）。框架把主窗位交给
        //    登记序里活着的第一扇，本层交出去的是自己账目里的第一扇——两者都是「还活着的第一扇」，
        //    而本层的这一支才是与命令落点同源的账。
        if (active_window_tracker.active() == au::AURORA_INVALID_WINDOW_ID) {
            if (WorkspaceWindow *const next = fallback_window(); next != nullptr) {
                active_window_tracker.set_fallback(next->id);
            }
        }
        // ⑥ 单实例唤醒指针：正指着这扇已散窗的那一枚要重指到活着的第一扇，一格都不剩才清空
        //    （转交线程此后只等下一帧或超时收尾，不会去唤醒一只已析构的 surface）。
        if (startup_wake.load() == closing_surface) {
            WorkspaceWindow *const next = fallback_window();
            startup_wake.store(next == nullptr ? nullptr : next->surface);
        }
        // ⑦ 几何提交（稿 D9 的第一处 flush）：框架在**帧末回收的收集步骤**里就把这一扇的几何写进了
        //    内存（`reap_closed` 排在 `on_window_closed` 之前，故这里读得到），本跳只负责把它落到文件。
        //    为什么要这一处而不是只留退出那一处：关一扇之后进程还可能跑很久，而崩掉的那一次不会有退出腿。
        flush_window_geometry();
        // 布局提交（`SPEC.FEAT.WS.11` 稿 D2①「关窗」那一类，与上面同一条「崩掉的那一次不会有退出腿」
        // 的取向）。排在 ④ **之后**：本层对象已经落地、容器里已经没有这一扇，采集出来的才是拆完那一份
        // 账。第 ① 步每关一张标签也写一次，中间那几份带着一扇正在拆的窗——落点窗散了以后没有更晚的
        // 读者，最后一次提交就是唯一有意义的那一份。
        flush_workspace_snapshot();
    });

    // 帘子的首发（`SPEC.FEAT.WS.12`，裁决 7.172）：帘子窗建出来就是 `Hidden`，进循环之前走**与热键
    // 完全同一条**路（同一个状态机、同一个 Show 分支）把它放到屏上——本应用没有托盘也没有第二个入口，
    // 起手就藏等于没有窗口。放在这里而不是建窗那一刻：全部接线已就绪，首帧即有内容。排在冷启动请求与
    // 启动重放**之前**是硬要求：那两条都可能弹凭据问答，而浮层画在帘子窗里——窗不显示，问句就没人
    // 看得见，重放泵会永远停在「等一个不会来的答案」上。
    if (quake_enabled) {
        apply_quake_intent(quake_toggle.on_hotkey(borealis::ui::QuakeToggle::Clock::now()));
    }

    // 冷启动请求在接线齐备之后、进循环之前执行一次（稿 §5.4）：首帧直接画出目标标签或那一刀切分，
    // 不出现「先空一帧再补一格」。唤醒指针同样在这一刻交出去——此前入队的转交请求随首帧排空。
    // 唤醒指针交出去的是**当前还在的第一扇窗**的 surface（转交线程按这一枚唤醒 UI）；窗被关掉时
    // 上面那条撤账链会把它重指到活着的第一扇，一格都不剩则清空。
    if (WorkspaceWindow *const wake_window = fallback_window(); wake_window != nullptr) {
        startup_wake.store(wake_window->surface);
    }

    // ---- 启动重放（`SPEC.FEAT.WS.11` 批 5-C，稿 D2／D4／D5／D6／D7／D8）--------------------------
    //
    // 读侧已在建首窗之前完成（上面 `restored_layout` 那一块），这里只做「按快照把会话造出来」。形态是
    // **游标 + 一次性泵**而不是双层 for：SSH 腿按 D4①／F9 要重新走凭据询问，而答案在 `on_secret` 那
    // 一侧异步回来，故泵在缺口处挂起、由 `restore_secret_sink` 放行。泵推进的每一步只做一件可失败、
    // 可跳过的事：单张标签失败不回滚已恢复的格（稿 F29④ 的 fail-closed 落点），取消则收尾当前窗、
    // 余下的窗与标签不再恢复（已恢复的现场原样留下）。
    //
    // 为什么在 `app.run()` 之前：首帧直接画出目标布局（与冷启动 CLI 同一条「不先空一帧」的取向，
    // 稿 F16）。不需要材料的那一路在这一处一口气建完；需要材料的那一路建到第一格 SSH 为止，剩下的
    // 由事件循环里的 `on_secret` 接着建。所有状态与闭包都声明在**本函数作用域**而不是 `if` 块里：
    // 挂起期间它们要活过 `app.run()`，缩进一层就是回调握着一批已析构的引用。
    std::size_t restore_window_cursor = 0U;
    std::size_t restore_tab_cursor = 0U;
    std::vector<std::optional<borealis::ui::TabId>> restore_tab_ids;
    std::optional<au::WindowId> restore_window_id;
    bool restore_aborted = false;
    bool restore_done = false;
    std::function<void()> pump_restore;
    // 跳链补齐的挂起态（`SPEC.FEAT.CONN.03` 稿 D8）：与询问那一个同族，缺口在凭据库的取料趟上。
    // `restore_awaiting_hops` 只在发起调用到调用点返回之间为真；`restore_hop_release` 是它的放行口，
    // 只在确认走异步之后才装上——同步补齐的那一路由调用点就地续跑，不让回调递归进泵。
    bool restore_awaiting_hops = false;
    std::function<void()> restore_hop_release;
    // 补齐回来的 bundle（目标 + 逐跳）：明文只在泵里过一手、取用即清，同 `restore_secret` 的存活期。
    std::optional<borealis::conn::SshSecretBundle> restore_hop_bundle;

    // 档案 id 命中配置表且**同型**才以表里那一份为准（稿 D4①）：只比 id 不比类型，会让「档案被改成
    // 另一种连接」之后的首次重启拿一份形状不对的档案去拨。未命中（含 quick connect 那些不落表的临时
    // id）回空指针，调用方退回快照里那一份字段。
    auto restored_profile = [&settings](const borealis::ui::SnapshotTab &item)
        -> const borealis::conn::Profile * {
        if (item.profile_id.empty()) {
            return nullptr;
        }
        for (const borealis::conn::Profile &candidate : settings.profiles) {
            if (candidate.id == item.profile_id && candidate.type == item.kind) {
                return &candidate;
            }
        }
        return nullptr;
    };

    // 标签缺省名只用**一处真值源**：档案命中取档案名（改过名就显示新名），否则落回连接类型词条
    // （稿 D10①）。手动名存在 `item.title` 里；OSC 标题不存也不恢复——远端重发后自会回来。
    auto restored_type_label = [&decode_utf8](borealis::conn::ConnectionType kind) -> std::u32string {
        switch (kind) {
            case borealis::conn::ConnectionType::Local:
                return decode_utf8(borealis::ui::settings_label("connections.type.local"));
            case borealis::conn::ConnectionType::Ssh:
                return decode_utf8(borealis::ui::settings_label("connections.type.ssh"));
            case borealis::conn::ConnectionType::Serial:
                return decode_utf8(borealis::ui::settings_label("connections.type.serial"));
            case borealis::conn::ConnectionType::Telnet:
                return decode_utf8(borealis::ui::settings_label("connections.type.telnet"));
        }
        return {};
    };

    /// @brief 把一条快照标签造回 @p window：会话、视口、工作区、账目、标签条一次成。
    ///
    /// 任一环失败即回空值且**不留账**：会话先 close 再散，`tab_kind`／四张连接表／标签条一处不写。
    /// @param bundle 仅 SSH 腿使用（目标 + 逐跳材料，稿 D8）；询问型档案由调用方先问到再交进来
    ///               （目标那格空值即缺材料）。带跳链的快照要先经 `with_hop_secrets` 补齐才走到这里。
    auto build_restored_tab = [&](WorkspaceWindow &window, const borealis::ui::SnapshotTab &item,
                                  borealis::conn::SshSecretBundle bundle)
        -> std::optional<borealis::ui::TabId> {
        // 先跑树的整套自检并取叶子数（`restore` 里还会再跑一遍，那一次是界面侧的前置）。
        borealis::ui::PaneTree probe{1U};
        if (!probe.restore(item.shape, 1U)) {
            AURORA_LOG_WARN("main", "restore: tab skipped, its pane shape is not a valid tree");
            return std::nullopt;
        }
        const std::size_t leaves = probe.pane_count();
        const borealis::conn::Profile *const named = restored_profile(item);

        // 逐腿的档案与料在造会话**之前**取齐：缺料在这一步就拒，不先起一半会话再散。本地腿命中档案时
        // 整张标签按档案那份算（档案腿的切分工厂本就不继承 OSC 7，故各格同料是它的既有语义）；未命中
        // 则用采集下来的逐格有效料——缺省本地标签的各格目录可以不同，那份料只有快照里有。
        std::optional<borealis::conn::SshProfile> ssh;
        std::optional<borealis::conn::SerialProfile> serial;
        std::optional<borealis::conn::TelnetProfile> telnet;
        std::vector<borealis::conn::LocalTerminalSpec> local_specs;
        switch (item.kind) {
            case borealis::conn::ConnectionType::Local:
                if (named != nullptr) {
                    borealis::conn::LocalTerminalSpec spec;
                    spec.command_line = named->local.command_line;
                    spec.working_directory = named->local.working_directory;
                    spec.environment = named->local.environment;
                    local_specs.assign(leaves, spec);
                } else {
                    local_specs = item.panes;
                }
                if (local_specs.size() != leaves) {
                    AURORA_LOG_WARN("main", "restore: local tab skipped, ", local_specs.size(),
                                    " pane specs for ", leaves, " panes");
                    return std::nullopt;
                }
                break;
            case borealis::conn::ConnectionType::Ssh:
                if (named != nullptr) {
                    ssh = named->ssh;
                } else if (item.ssh.has_value()) {
                    ssh = item.ssh;
                }
                if (!ssh.has_value()) {
                    AURORA_LOG_WARN("main", "restore: SSH tab skipped, the snapshot carries no profile");
                    return std::nullopt;
                }
                // 询问型档案必须由调用方先把材料问到；走到这里还缺就是调用方的账没对上，跳过而不是拿
                // 空串去拨（空串会被当成用户提交了一个空口令，重拨必然失败且现场看不出原因）。
                if (borealis::conn::secret_ask_for(borealis::conn::normalized_auth_method(*ssh)) !=
                        borealis::conn::SecretAsk::None &&
                    !bundle.target.has_value()) {
                    AURORA_LOG_WARN("main", "restore: SSH tab skipped, no credential material");
                    return std::nullopt;
                }
                // 带跳链的快照必须先过 `with_hop_secrets`（稿 D8）：`hops` 空而链非空就是调用方的账
                // 没对上——宁可跳过这一格，也不带着「一条都没补齐的跳板材料」去拨一条注定谈不开的链。
                if (!ssh->proxy_jumps.empty() && bundle.hops.empty()) {
                    AURORA_LOG_WARN("main", "restore: SSH tab skipped, its hop chain was never filled");
                    return std::nullopt;
                }
                break;
            case borealis::conn::ConnectionType::Serial:
                // 串口腿在建标签那条路径上就切不开（恒拒工厂，稿 §4 的 F12）；一份声称多格的串口形状
                // 只可能来自手改文档，按单张标签 fail-closed。
                if (leaves != 1U) {
                    AURORA_LOG_WARN("main", "restore: serial tab skipped, it cannot hold ", leaves, " panes");
                    return std::nullopt;
                }
                if (named != nullptr) {
                    serial = named->serial;
                } else if (item.serial.has_value()) {
                    serial = item.serial;
                }
                if (!serial.has_value()) {
                    AURORA_LOG_WARN("main", "restore: serial tab skipped, the snapshot carries no profile");
                    return std::nullopt;
                }
                break;
            case borealis::conn::ConnectionType::Telnet:
                if (named != nullptr) {
                    telnet = named->telnet;
                } else if (item.telnet.has_value()) {
                    telnet = item.telnet;
                }
                if (!telnet.has_value()) {
                    AURORA_LOG_WARN("main", "restore: telnet tab skipped, the snapshot carries no profile");
                    return std::nullopt;
                }
                break;
        }

        // 号在这一刻发（`create_new_tab_with` 里 `next_tab_id++` 是唯一的另一处发号点）：失败也照旧
        // 消耗一个号——标识只增不复用，跳号无害，回收一个号才会撞进可能还活着的标签。
        const borealis::ui::TabId id = next_tab_id++;
        std::vector<std::shared_ptr<borealis::session::Session>> sessions;
        std::vector<std::shared_ptr<borealis::ui::TerminalView>> views;
        sessions.reserve(leaves);
        views.reserve(leaves);
        std::optional<borealis::conn::SerialConnection *> serial_connection;
        // 询问到的材料留一份给 SFTP 面板与往后切分的新格（同 `open_ssh_tab` 的 sftp_secret）：
        // 内存副本、不落盘、不进日志（CONN.09 的降级语义）。逐跳材料同批留档——切分新格走的是
        // 同一个 `make_ssh_pane_factory`，它读的就是这份 bundle。
        const borealis::conn::SshSecretBundle retained_bundle = bundle;
        auto abandon = [&]() -> std::optional<borealis::ui::TabId> {
            views.clear();  // 先散视口：`~TerminalView` 要往背后的会话回写撤账空响应。
            for (const std::shared_ptr<borealis::session::Session> &session : sessions) {
                if (session != nullptr) {
                    session->close();
                }
            }
            sessions.clear();
            return std::nullopt;
        };
        for (std::size_t order = 0U; order < leaves; ++order) {
            std::shared_ptr<borealis::session::Session> session;
            borealis::term::LineEnding ending = borealis::term::LineEnding::Lf;
            switch (item.kind) {
                case borealis::conn::ConnectionType::Local:
                    session = make_local_session(local_specs[order]);
                    break;
                case borealis::conn::ConnectionType::Ssh: {
                    // 同一档案的各格共用一条 TCP、各持一条通道；最后一份材料 move 走（同
                    // `open_ssh_tab` 的「留档副本先取」）。
                    borealis::conn::SshSecretBundle material =
                        order + 1U < leaves ? retained_bundle : std::move(bundle);
                    session = make_ssh_session(*ssh, std::move(material));
                    break;
                }
                case borealis::conn::ConnectionType::Serial: {
                    auto leg = make_serial_session(*serial);
                    session = std::move(leg.session);
                    serial_connection = leg.connection;
                    ending = leg.line_ending;
                    break;
                }
                case borealis::conn::ConnectionType::Telnet: {
                    auto leg = make_telnet_session(*telnet);
                    session = std::move(leg.session);
                    ending = leg.line_ending;
                    break;
                }
            }
            if (session == nullptr) {
                return abandon();
            }
            sessions.push_back(session);
            views.push_back(build_pane_view(window, session, ending));
        }

        borealis::ui::WorkspaceView::Hooks ws_hooks;
        wire_workspace_hooks(id, ws_hooks);
        const std::shared_ptr<borealis::ui::WorkspaceView> workspace =
            borealis::ui::WorkspaceView::restore(std::move(views), item.shape, item.focused_order,
                                                 std::move(ws_hooks));
        if (workspace == nullptr) {
            // `restore` 的前置全跑在造对象之前，回空即「没有半个工作区」：散掉这一批会话即可。
            return abandon();
        }

        // 账目一次登记齐（与 `create_new_tab_with` 同一份形态，只是工作区是恢复出来的那一棵）：
        // 会话表、本地有效料、切分工厂、属主与工作区，然后才是类型表与四张连接表。
        TabWorkspace tab;
        tab.owner = &window;
        for (std::size_t order = 0U; order < leaves; ++order) {
            const borealis::ui::PaneId pane = static_cast<borealis::ui::PaneId>(order) + 1U;
            tab.sessions_by_pane[pane] = sessions[order];
            if (item.kind == borealis::conn::ConnectionType::Local) {
                tab.local_specs_by_pane[pane] = local_specs[order];
            }
        }
        switch (item.kind) {
            case borealis::conn::ConnectionType::Local:
                tab.make_session = named != nullptr ? make_local_profile_pane_factory(named->local)
                                                    : make_local_pane_factory(id);
                break;
            case borealis::conn::ConnectionType::Ssh:
                tab.make_session = make_ssh_pane_factory(id);
                break;
            case borealis::conn::ConnectionType::Serial:
                tab.make_session = make_refusing_pane_factory("serial device is exclusive");
                break;
            case borealis::conn::ConnectionType::Telnet:
                tab.make_session = make_telnet_pane_factory(id);
                break;
        }
        tab.workspace = workspace;
        tab_kind[id] = item.kind;
        tab_workspaces[id] = std::move(tab);
        switch (item.kind) {
            case borealis::conn::ConnectionType::Local:
                break;
            case borealis::conn::ConnectionType::Ssh:
                ssh_profile_by_tab[id] = *ssh;
                ssh_secret_by_tab[id] = retained_bundle;
                break;
            case borealis::conn::ConnectionType::Serial:
                serial_by_tab[id] = SerialTabBinding{.profile = *serial,
                                                     .connection = serial_connection.value_or(nullptr)};
                break;
            case borealis::conn::ConnectionType::Telnet:
                telnet_by_tab[id] = *telnet;
                break;
        }
        if (!item.profile_id.empty()) {
            tab_profile_id[id] = item.profile_id;
        }
        attach_broadcast_sink(id);

        std::u32string tab_name =
            named != nullptr ? decode_tab_name(named->name) : restored_type_label(item.kind);
        window.tab_strip.add(id, std::move(tab_name));
        window.current_tab_id = id;
        if (!item.title.empty()) {
            static_cast<void>(window.tab_strip.rename(id, decode_utf8(item.title)));
        }
        return id;
    };

    /// @brief 收尾一扇恢复窗：选中快照里那一格（缺了就取第一个恢复成功的），一格都没有就补一张缺省
    ///        本地标签。
    ///
    /// 兜底标签走 `commit=false`：重放中途的半成品不进快照，否则一次失败的恢复会把「上次的布局」
    /// 改写成「这次半成品」（稿 D2① 的写侧纪律）。
    auto finish_restore_window = [&](WorkspaceWindow &window,
                                     const borealis::ui::SnapshotWindow &saved) -> void {
        std::optional<borealis::ui::TabId> selected;
        if (saved.selected_tab < restore_tab_ids.size()) {
            selected = restore_tab_ids[saved.selected_tab];
        }
        if (!selected.has_value()) {
            for (const std::optional<borealis::ui::TabId> &candidate : restore_tab_ids) {
                if (candidate.has_value()) {
                    selected = candidate;
                    break;
                }
            }
        }
        if (selected.has_value()) {
            static_cast<void>(window.tab_strip.select(*selected));
            window.current_tab_id = *selected;
        } else {
            create_new_tab(window, std::nullopt, false);
        }
        rebuild_root_children(window, tab_workspaces);
    };

    /// @brief 重放收尾后补 `--cwd` 单飞那一格（稿 D7① 的「不跳过恢复」半边）。
    ///
    /// 为什么不放在 `app.run()` 之前那一跳：恢复可能在等凭据，此刻补进去的标签会被之后的收尾选中
    /// 压掉，用户点名要的那一格就沉到后面。挂在泵的完成点，它一定是最后被选中/补上的那一格。
    auto apply_deferred_startup_cwd = [&]() -> void {
        if (!startup_request.has_value() || startup_request->cwd.empty()) {
            return;
        }
        const std::string problem = apply_startup_request(*startup_request, false);
        startup_request.reset();  // 一次性：完成点只可能到这（挂起期间的每一次放行都汇入同一条泵）。
        if (!problem.empty()) {
            AURORA_LOG_WARN("main", "startup request not applied: ", problem);
        }
    };

    pump_restore = [&]() -> void {
        for (;;) {
            if (restore_done) {
                return;
            }
            const std::vector<borealis::ui::SnapshotWindow> &saved_windows =
                restored_layout->windows;
            if (restore_aborted) {
                if (restore_window_id.has_value()) {
                    if (WorkspaceWindow *const window = window_of(*restore_window_id);
                        window != nullptr) {
                        finish_restore_window(*window, saved_windows[restore_window_cursor]);
                    }
                }
                restore_window_id.reset();
                restore_tab_ids.clear();
                restore_done = true;
                AURORA_LOG_INFO("main", "session restore stopped: the credential prompt was cancelled");
                apply_deferred_startup_cwd();
                return;
            }
            if (restore_window_cursor >= saved_windows.size()) {
                restore_done = true;
                AURORA_LOG_INFO("main", "session restore complete");
                apply_deferred_startup_cwd();
                return;
            }
            if (!restore_window_id.has_value()) {
                if (restore_window_cursor == 0U) {
                    if (workspace_windows.empty()) {
                        restore_done = true;
                        return;
                    }
                    // 第 0 扇就是建好等在那里的主窗：窗序与 `windows.json` 的创建序键按同一条算式
                    // 对齐（稿 D6①），故快照窗序不另存身份。
                    restore_window_id = workspace_windows.front()->id;
                } else {
                    WorkspaceWindow *const created = open_new_window ? open_new_window() : nullptr;
                    if (created == nullptr) {
                        AURORA_LOG_WARN("main", "restore: saved window skipped, a new window could not be created");
                        ++restore_window_cursor;
                        continue;
                    }
                    restore_window_id = created->id;
                }
                restore_tab_cursor = 0U;
                restore_tab_ids.assign(saved_windows[restore_window_cursor].tabs.size(), std::nullopt);
            }
            WorkspaceWindow *const window = window_of(*restore_window_id);
            if (window == nullptr) {
                // 只在凭据挂起期间可能发生（用户把那一扇关掉了）：余下的格无处可放，跳过整扇。
                AURORA_LOG_WARN("main",
                                "restore: the window being replayed is gone, its remaining tabs are skipped");
                restore_window_id.reset();
                restore_tab_ids.clear();
                ++restore_window_cursor;
                continue;
            }
            const borealis::ui::SnapshotWindow &saved = saved_windows[restore_window_cursor];
            if (restore_tab_cursor >= saved.tabs.size()) {
                finish_restore_window(*window, saved);
                restore_window_id.reset();
                restore_tab_ids.clear();
                ++restore_window_cursor;
                continue;
            }
            const borealis::ui::SnapshotTab &item = saved.tabs[restore_tab_cursor];
            // 材料折成 bundle（稿 D8）：上一格跳链补齐的结果若还留在手上就地取用（它已含目标那格），
            // 否则从 `restore_secret` 抄一份——目标材料要等这一格**真的建出来**才作废，挂起期间不能搬走，
            // 否则泵重进时「已问到答案」这一判据就断了，会对同一格再问一次。
            borealis::conn::SshSecretBundle bundle;
            if (restore_hop_bundle.has_value()) {
                bundle = std::move(*restore_hop_bundle);
                restore_hop_bundle.reset();
            } else if (item.kind == borealis::conn::ConnectionType::Ssh) {
                bundle.target = restore_secret;
            }
            if (item.kind == borealis::conn::ConnectionType::Ssh) {
                const borealis::conn::Profile *const named = restored_profile(item);
                const borealis::conn::SshProfile *const profile =
                    named != nullptr ? &named->ssh : (item.ssh.has_value() ? &*item.ssh : nullptr);
                if (profile == nullptr) {
                    AURORA_LOG_WARN("main", "restore: SSH tab skipped, the snapshot carries no profile");
                    ++restore_tab_cursor;
                    continue;
                }
                const borealis::conn::SecretAsk ask = borealis::conn::secret_ask_for(
                    borealis::conn::normalized_auth_method(*profile));
                if (ask != borealis::conn::SecretAsk::None && !restore_secret.has_value()) {
                    if (credential_prompt == nullptr) {
                        AURORA_LOG_WARN("main", "restore: SSH tab skipped, no credential prompt is available");
                        ++restore_tab_cursor;
                        continue;
                    }
                    // 挂起前先把问句搬到这一扇窗上（稿 D6 的唯一漏斗）：「一串旧窗恢复起来之后，
                    // 材料框弹在用户没碰过的那一扇」比多问一次更难解释。
                    restore_waiting_for_secret = true;
                    retarget_panel_to(*credential_prompt, window);
                    const std::string ask_name =
                        named != nullptr && !named->name.empty()
                            ? named->name
                            : (item.profile_id.empty()
                                   ? borealis::ui::settings_label("connections.type.ssh")
                                   : item.profile_id);
                    credential_prompt->ask(ask, ask_name);
                    return;
                }
                // 带跳链的快照：逐跳材料先补齐才建格（稿 D8）。取料走凭据库、可能阻塞，询问跳会弹问句，
                // 故与询问同一套「挂起 + 放行」形态；`hops` 空而链非空就是「还没补过」的判据，泵重进时
                // 不会二次发起。任一询问跳被取消 ⇒ `nullopt` ⇒ 中止余下重放（批 7 D1，同
                // `restore_secret_sink(nullopt)` 的形态：已恢复的格原样留下）。
                if (!profile->proxy_jumps.empty() && bundle.hops.empty()) {
                    restore_awaiting_hops = true;
                    with_hop_secrets(
                        *profile, item.profile_id, bundle.target,
                        [&restore_awaiting_hops, &restore_hop_bundle, &restore_hop_release,
                         &restore_done, &restore_aborted](
                            std::optional<borealis::conn::SshSecretBundle> filled) -> void {
                            restore_awaiting_hops = false;
                            // 重放已经收尾（取消／跑完）：这份材料没有下一格会来取它，当场作废。
                            if (restore_done || restore_aborted) {
                                return;
                            }
                            if (!filled.has_value()) {
                                restore_aborted = true;
                                if (restore_hop_release) {
                                    auto resume = std::move(restore_hop_release);
                                    restore_hop_release = nullptr;
                                    resume();
                                }
                                return;
                            }
                            restore_hop_bundle = std::move(*filled);
                            // 放行口只在异步分支装上（发起调用之后）：同步回调走到这里时它还是空函数，
                            // 于是当场补齐的那一路由调用点就地 `continue` 续跑，不递归进泵。
                            if (restore_hop_release) {
                                auto resume = std::move(restore_hop_release);
                                restore_hop_release = nullptr;  // 一次性：取用即清，别留第二次放行。
                                resume();
                            }
                        });
                    if (restore_awaiting_hops) {
                        // 这里**不能**写成 `restore_hop_release = pump_restore`：赋值发生在
                        // `pump_restore` 自己的调用栈里，会当场销毁正在执行的那份闭包。
                        restore_hop_release = [&pump_restore] { pump_restore(); };
                        return;
                    }
                    continue;
                }
            } else {
                restore_secret.reset();  // 非 SSH 腿不吃材料：上一条答案若还留在手上，到此作废。
            }
            const std::optional<borealis::ui::TabId> id =
                build_restored_tab(*window, item, std::move(bundle));
            restore_secret.reset();
            if (id.has_value()) {
                restore_tab_ids[restore_tab_cursor] = *id;
            }
            ++restore_tab_cursor;
        }
    };

    if (restore_at_startup && restored_layout.has_value()) {
        restore_secret_sink = [&](std::optional<std::string> secret) -> void {
            restore_secret = std::move(secret);
            if (!restore_secret.has_value()) {
                restore_aborted = true;  // 取消＝中止余下重放；已恢复的格原样留下（on_cancel 的注释同一条）。
            }
            pump_restore();
        };
        pump_restore();
    }

    if (startup_request.has_value() && !cold_startup_plain_tab_done) {
        const std::string problem = apply_startup_request(*startup_request, false);
        if (!problem.empty()) {
            // 冷启动的运行期失败不关掉应用：首标签已经在屏上，为一次切分失败退进程比留着它更糟
            //（稿 §5.4 末条）。转交那一侧同一条诊断是回给发起进程的退出码 2。
            AURORA_LOG_WARN("main", "startup request not applied: ", problem);
        }
    }
    app.run();

    // 几何提交的第二处（稿 D9「任一窗关闭后」之外的那半句「退出前」）：最后一扇被保留、不走
    // `on_window_closed`（F19 的代价档），它那一格的几何是框架在帧末收集步骤里写进内存的，本跳是它
    // 唯一的落盘机会。排在关会话之前是有意的：下面那条退出序会把 `tab_workspaces` 与视口整片散掉，
    // 而这一跳只碰存储与文件，不需要任何窗还活着——先把它提交掉，退出序再怎么改都不影响这份账。
    flush_window_geometry();

    // 布局提交的第二处（稿 D2①「退出」那一类，与几何同一条腿的取向：排在退出序**之前**）。
    // 关最后一扇窗时框架保留主宿主、`on_window_closed` 不触发（F19 的代价档），于是那一扇的布局只有
    // 这一次机会；而下面的退出序会把 `tab_workspaces` 整片散掉，排在它之后采集到的恒是空表。
    flush_workspace_snapshot();

    // 单实例通道在 surface 消散之前停：先撤唤醒指针（此后入队的请求不再碰这枚引用，只等下一次帧
    // 边界或超时收尾），再关监听并 join transport 线程——反过来就成了「线程正在唤醒一只已析构的
    // surface」，而这条竞态只在关掉最后一格的那几毫秒里存在，真机极难复现。
    startup_wake.store(nullptr);
    instance_listener.reset();

    // 关停在读线程仍可能唤醒 surface 之前：需要关闭所有标签的所有会话（稿 §5 的「退出」一行：
    // 一格一会话之后这里遍历的是每格，而不是每标签一份）。
    //
    // **退出序按窗走**（稿 D11／§6 批 4-C-2）：真值源是「每窗名下全部标签」，与 4-B 撤账链第 ① 步同一条
    // 读法。这一跳存在的理由是 F19②：`reap_closed` 保留最后一个宿主，于是「关掉唯一那扇窗」时
    // `set_on_window_closed` 根本不触发，那一扇名下的会话只能在这里收——`perform_close` 那条链不会为它走
    // 第二遍（窗还在，撤账是「关标签」这个动作才做的）。
    //
    // **摘根那一跳是这一棒补上的**（批 1-B §12 留的那一句「要把这一段显式化，而不是继续依赖声明序」）：
    // `root_column` 持着「当前那一格的视口」，而 `workspace_windows` 声明在最前、`tab_workspaces` 之后析构。
    // 于是原形态下那份视口要等本函数返回才散，那时它背后的会话早就没了——`~TerminalView` 的 OSC 52 跨帧
    // 承诺回写（`SPEC.FEAT.CONN.12` 判据 ⑤）走的正是这条腿，它的前提是「会话对象还在、只是已关」（写腿的
    // closing 闸丢的是**已关**会话的写，不是已散对象）。先清根再撤账，视口就在自己那一格被销毁，而
    // `TabWorkspace` 的成员次序保证会话在其后散——与 `perform_close` 那条「先摘根再 erase」同一条不变量。
    //
    // 逐窗腿不复用 `erase_tab_ledger`：它按「关标签」的语义走，会搬栏位表、会在当前号被关掉时
    // `rebuild_root_children` 重排现场并给宿主排布局脏——`run()` 已返回，那些副作用一项都不需要，而重排那一跳
    // 恰恰可能落在已请求关闭的宿主上（§18⑵ 同一条理由）。四张连接表在这里不撤：本函数此后没有任何读者，
    // 而多撤一遍要付的就是那条重排。
    //
    // `ExitPolicy` 不在这里设置：稿 D11 的推荐档＝保持框架缺省，实测读数即 `ExitPolicy::LastWindowClosed`
    //（Aurora 主仓 `include/aurora/app/application.h` 里 `exit_policy_` 的成员初值，本仓 `set_exit_policy` 零调用点），
    // 语义正是「关掉最后一扇才退出」。显式再设一遍会把「本仓依赖框架缺省」这件事从代码面上抹掉，而框架哪天
    // 改缺省正是本条要第一时间看见的变化。另外两档在 D11 里被否：`MainWindowClosed` 让「先关主窗」那一跳直接
    // 终结进程，`ExplicitOnly` 让最后一扇关掉后进程不退出——终端类应用因此留着 PTY 子进程。
    auto close_workspace_sessions = [](TabWorkspace &workspace) -> void {
        for (auto &[pane, session] : workspace.sessions_by_pane) {
            static_cast<void>(pane);
            session->close();
        }
    };
    for (const std::unique_ptr<WorkspaceWindow> &window : workspace_windows) {
        if (window == nullptr) {
            continue;
        }
        // 先取快照：下面 erase 要动 `tab_workspaces`，而 `tabs()` 给的是栏位表本体的视图。
        std::vector<borealis::ui::TabId> strip_ids;
        for (const auto &tab : window->tab_strip.tabs()) {
            strip_ids.push_back(tab.id);
        }
        for (const borealis::ui::TabId id : strip_ids) {
            const auto found = tab_workspaces.find(id);
            if (found == tab_workspaces.end()) {
                continue;  // 栏里留着一格而表里没有＝那格的账早已撤走，这里没有还能关的会话
            }
            close_workspace_sessions(found->second);
        }
        window->root_column->child_nodes_mut().clear();  // 摘根：见上面那段
        for (const borealis::ui::TabId id : strip_ids) {
            static_cast<void>(tab_workspaces.erase(id));
        }
    }
    // 兜底腿：上面逐窗那一跳收走的是「活着的那几扇各自栏位里的每一格」，此刻表里还剩的就不属于任何一扇
    // 窗的栏位——那是「栏位表与全局表分叉」的证人（形态同 4-B 撤账链第 ② 步），正常路径恒空。判据「关最后一
    // 扇退出时无残留进程」（§9 第 3 条）要求这里**照收**并把分叉写进日志，而不是按静默丢掉处理：漏下一格就是
    // 留一个 PTY 子进程在退出之后。此刻所有活着窗的根都已清空，故 erase 这一格同样落在会话之前散视口。
    std::vector<borealis::ui::TabId> strays;
    for (const auto &[tab_id, workspace] : tab_workspaces) {
        static_cast<void>(workspace);
        strays.push_back(tab_id);
    }
    for (const borealis::ui::TabId id : strays) {
        AURORA_LOG_WARN("main", "exit: tab ", id, " is not listed by any live window's strip");
        const auto found = tab_workspaces.find(id);
        if (found == tab_workspaces.end()) {
            continue;
        }
        close_workspace_sessions(found->second);
        static_cast<void>(tab_workspaces.erase(found));
    }
    return 0;
}
