#pragma once

// ============================================================
// 配置 schema 与全量默认值（include/borealis/config/settings.h）
// ------------------------------------------------------------
// `SPEC.FEAT.PREF.03` 的「全部设置与档案」按裁决 7.26① 取 SPEC.FEAT.PREF.02 的四分类
// （外观 / 终端 / 连接 / 快捷键）一次性建全量字段：UI 侧尚未接线的键先有默认值、无消费方。
// 于是「默认值」只有一处定义，`SPEC.FEAT.PREF.06` 的零配置可用与设置面板的「恢复默认」都
// 直接落到这里的类内初始值——`Settings{}` 就是首次启动的那份配置。
//
// 凭据不在本文件（裁决 7.26⑥）：连接域只放非敏感的默认参数，密码与私钥 passphrase 到
// `SPEC.FEAT.CONN.09` 落期以 OS 凭据库句柄存在，配置文件里永远不出现明文值。
//
// 本头刻意不含 Aurora 类型（AGENTS.md §4.4 第 20 条同口径：纯逻辑、可独立单测）；
// 与框架 `Preferences` 的读写接缝在 `config/store.h`。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/config/themes.h"
#include "borealis/conn/profile.h"
#include "borealis/grid/storage.h"
#include "borealis/term/paste.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/font_choice.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/right_click.h"
#include "borealis/ui/tab_strip.h"

namespace borealis::config {

/// @brief BEL 的呈现方式（`SPEC.FEAT.WS.04`）。
enum class BellMode : std::uint8_t {
    Off,     ///< 完全忽略。
    Visual,  ///< 只在 UI 上给视觉提示（标签高亮），不发声音。
    Audible, ///< 视觉提示之外再发可听铃声；须显式开启框架音频通道，故不作缺省。
};

/// @brief scrollback 里超长行的处理策略（裁决 7.4：截断为默认）。
enum class LongLinePolicy : std::uint8_t {
    Truncate, ///< 超出容量部分丢弃（省内存，对齐 Tabby）。
    Wrap,     ///< 折成多行计入容量。
};

/// @brief 粘贴文本里换行符的处理策略（`SPEC.FEAT.INTERACT.03`）。
///
/// 取值与语义的唯一定义在 `term::PasteNewlinePolicy`（发送侧要用它折算计划），配置侧只作别名，
/// 以免两处枚举表在取值上分叉——与 `term::CursorShape`、`term::AmbiguousWidth` 同口径。
using term::PasteNewlinePolicy;

/// @brief 右键的缺省行为（`SPEC.FEAT.INTERACT.03` 的 Windows Terminal 三态）。
///
/// 取值与语义的唯一定义在 `ui::RightClickAction`（界面腿要用它折算右键处置计划），配置侧只作
/// 别名，与上面的 `PasteNewlinePolicy` 同一条单一真值源口径（裁决 7.41）。
using ui::RightClickAction;

/// @brief 标签名的手动重命名与 `OSC` 标题谁优先（`SPEC.FEAT.TERM.07` 那句「（可配）」）。
///
/// 取值与语义的唯一定义在 `ui::TabNamePriority`（名称折算发生在标签列表件里），配置侧只作别名，
/// 与上面两条同口径（裁决 7.43①）。
using ui::TabNamePriority;

/// @brief 状态栏各项的开关（裁决 7.25⑧：每项一个开关，缺省全开）。
///
/// 名称与 `codespec/UI_OVERVIEW.draft.md` §2 屏 1 的状态栏条目一一对应；宽度不足时按本结构
/// 的**声明次序**从右向左省略（该条要求「按注册次序」，声明次序就是注册次序）。
struct StatusBarSettings {
    bool show_connection{true};       ///< 已连接的目标摘要。
    bool show_reconnect{true};        ///< 断线与自动重连倒计时。
    bool show_cursor_position{true};  ///< 光标行列。
    bool show_encoding{true};         ///< 当前会话编码。
    bool show_grid_size{true};        ///< 行列数。
    bool show_font_size{true};        ///< 字号。
    bool show_theme{true};            ///< 主题名。
    bool show_scrollback{true};       ///< scrollback 容量。
    bool show_clipboard_policy{true}; ///< OSC 52 剪贴板授权态。
    bool show_input_latency{true};    ///< 输入延迟 P95。

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] constexpr auto operator==(const StatusBarSettings &other) const noexcept -> bool = default;
};

/// @brief 外观域：主题、字体、光标、视口内边距与界面件缺省形态（`SPEC.FEAT.PREF.02` 的「外观」）。
struct AppearanceSettings {
    /// @brief 构造为全量默认值：调色板按缺省主题填充。
    ///
    /// 不在类内初始值里展开 16 个色值，是为了让主题表成为色值的唯一来源——否则换主题名
    /// 与首屏默认色会各存一份、随时间漂移。
    AppearanceSettings();

    std::string theme{kDefaultThemeName};  ///< 预置主题名；取值见 `config/themes.h`。
    ui::PaletteSpec palette{};                          ///< 生效调色板＝主题派生值 + 用户重映射；落盘的是这份最终值。

    std::string font_family{ui::kDefaultMonospaceFamily};  ///< 默认字体（裁决 7.3）；与回落族同名，故只有一处定义。
    double font_size_pt{14.0};                             ///< 14pt 是视觉稿的实测基准（100% DPI → 11×22 dp 格）。
    /// @brief 行高倍数（`SPEC.FEAT.RENDER.02`）：网格行步长 = 字体行高 × 它，取值域 [1.0, 3.0]。
    ///
    /// 域从 1.0 起：小于字体自身行高会把下伸部切进下一行，而终端的网格对齐不容许行重叠。
    double font_line_height{1.0};
    /// @brief 字距（dp，同上）：加在每对相邻字形之间，取值域 [0.0, 8.0]。
    ///
    /// 域从 0.0 起：负字距让字形压进下一列，等宽网格里那不是「紧凑」而是错位。
    double font_letter_spacing_dp{0.0};
    /// @brief 缺字回退链（`SPEC.FEAT.RENDER.02` 的「回退链顺序可配」）：主族缺字时依次尝试的族名序列。
    ///
    /// 顺序即语义，越靠前优先级越高；族名的口径与框架的字体族枚举**同源**（逐字节精确、区分大小写），
    /// 解析不到的族由框架跳过而不报错。长度上限由绘制侧按框架常量截断并留痕，本头因此不写那个数
    /// （公共头不得含 Aurora 类型）。
    ///
    /// 缺省为空 = 不注入按族链，只走框架的全局默认回退链——那是本键落地前唯一的形态，故空值与
    /// 「配置里没有这个键」逐位相同，而不是「用户要求不回退」。
    std::vector<std::string> font_fallback_chain{};

    float viewport_padding_dp{4.0F};  ///< 终端视口内边距（裁决 7.25②）；改 0 即回到贴边形态。

    term::CursorShape cursor_shape{term::CursorShape::Block};  ///< 光标缺省形态；远端 DECSCUSR 之后以远端为准。
    bool cursor_blinking{true};                                ///< 缺省闪烁档，同上只在建会话时喂给状态机。
    int cursor_blink_period_ms{500};                           ///< 闪烁周期（`SPEC.FEAT.RENDER.04` 的可配项）。

    bool sidebar_collapsed{true};  ///< 侧栏缺省折叠（裁决 7.25⑥），展开态由本字段记住。
    /// @brief 标签名冲突时的优先级（`SPEC.FEAT.TERM.07`）：手动重命名与 `OSC` 标题谁赢。
    ///
    /// 归外观域而非终端域：它改的是标签栏上那行字，不改会话里的任何字节。
    TabNamePriority tab_name_priority{TabNamePriority::ManualWins};
    StatusBarSettings status_bar{};

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const AppearanceSettings &other) const noexcept -> bool = default;
};

/// @brief 终端域：回滚容量、宽度口径、铃声、粘贴行为与编码（`SPEC.FEAT.PREF.02` 的「终端」）。
struct TerminalSettings {
    std::size_t scrollback_limit{grid::kDefaultScrollbackLimit};  ///< 不含视口行数（`SPEC.FEAT.TERM.04`）。
    term::AmbiguousWidth ambiguous_width{term::AmbiguousWidth::Narrow};  ///< Ambiguous 口径（裁决 7.15）。
    LongLinePolicy long_line{LongLinePolicy::Truncate};
    BellMode bell{BellMode::Visual};
    std::string encoding{"UTF-8"};  ///< 会话级编码（`SPEC.FEAT.TERM.09`），本地终端与 SSH / Telnet 的缺省。

    PasteNewlinePolicy paste_newlines{PasteNewlinePolicy::AsIs};
    RightClickAction right_click{RightClickAction::ContextMenu};
    bool copy_on_select{false};             ///< 选中即复制（`SPEC.FEAT.INTERACT.03` 明确默认关闭）。
    bool trim_pasted_trailing_space{false}; ///< 复制三项开关之一，默认关闭以保留原样。
    bool smart_line_join{false};            ///< 跨行反斜杠续行智能合并。
    bool strip_tmux_border_chars{false};  ///< 去除 tmux 分屏边框字符。
    /// @brief 双击选词的断点集（裁决 7.38③，空集口径见 7.39④）：ASCII 可见标点全集，空格与制表恒为断点。
    ///
    /// 存原始字符而非码点表：该键的取值域就是「哪些字符打断一个词」。空串不是「每个界定符各自成词」
    /// 而是「只有空格与制表断词」——把某字符从缺省表里删掉就是让它成为字组成分的唯一途径，
    /// 故该键的取值域属用户显式选择而非损坏，装载侧只判类型不判域。
    std::string word_delimiters{"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"};

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const TerminalSettings &other) const noexcept -> bool = default;
};

/// @brief SSH 的建档默认值（`SPEC.FEAT.CONN.02`；档案本体属 M3 的 `SPEC.FEAT.CONN.03`）。
struct SshDefaults {
    int port{22};
    std::string auth_method{"agent"};  ///< 取值：password / privatekey / agent / keyboard-interactive。
    bool agent_forwarding{false};      ///< `SPEC.FEAT.CONN.02` 明写「按 profile 开关，默认关」。
    int keepalive_interval_sec{60};    ///< 需求未规定数值，本仓取 60 秒（与 OpenSSH 客户端缺省同量级）。
    int connect_timeout_sec{10};       ///< 同上：错误分类提示需要它，具体值属本仓自定。

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const SshDefaults &other) const noexcept -> bool = default;
};

/// @brief 串口的建档默认值（`SPEC.FEAT.CONN.05`）。
struct SerialDefaults {
    int baud{115200};           ///< 需求未规定数值，本仓取嵌入式主流档。
    int data_bits{8};           ///< 取值 5..8。
    int stop_bits{1};           ///< 取值 1..2。
    std::string parity{"none"}; ///< 取值：none / even / odd。
    std::string line_ending{"LF"};   ///< `SPEC.FEAT.CONN.05` 明写默认 LF（发错设备无响应）。
    std::string encoding{"GB18030"}; ///< 串口默认编码（裁决 7.6）。

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const SerialDefaults &other) const noexcept -> bool = default;
};

/// @brief 最近连接列表的定长上限（`SPEC.FEAT.CONN.07` 明写五条）。
constexpr std::size_t kRecentConnectionLimit = 5;

/// @brief 最近连接里的一条（`SPEC.FEAT.CONN.07`）。
///
/// 只存档案 id 与时间戳，不存任何凭据；与撤销关闭标签（`SPEC.FEAT.WS.10`）的栈分开维护。
struct RecentConnection {
    std::string profile_id{}; ///< 档案 id；quick connect 用其临时 id。
    std::int64_t used_at{0};  ///< Unix 秒时间戳，只用于排序与展示。

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const RecentConnection &other) const noexcept -> bool = default;
};

/// @brief 连接域：本地终端、SSH 与串口的默认值（`SPEC.FEAT.PREF.02` 的「连接」）。
struct ConnectionSettings {
    std::string local_shell{};       ///< 空＝走探测链（Windows PowerShell → cmd → WSL；POSIX `$SHELL` → /bin/bash → /bin/sh；裁决 7.19④ 与 7.89）。
    std::string startup_directory{}; ///< 空＝继承进程当前目录。
    SshDefaults ssh{};
    SerialDefaults serial{};

    bool session_logging{false};   ///< 会话日志默认关闭（裁决 7.9），开启须逐会话显式动作。
    std::string session_log_dir{}; ///< 空＝未设置；日志路径配置归本域（架构 §7.5）。

    /// @brief 最近连接（最近在前，定长 `kRecentConnectionLimit`）。
    ///
    /// 与 `SPEC.FEAT.WS.10` 撤销关闭标签的栈是两回事：那条栈记「关掉的会话」，这张表记
    /// 「用过哪些档案」，两者各自维护、互不复用。
    std::vector<RecentConnection> recent{};

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const ConnectionSettings &other) const noexcept -> bool = default;
};

/// @brief 登记一次「用过这条连接」：按 id 去重并置顶，超出 `kRecentConnectionLimit` 丢弃尾部。
///
/// 纯逻辑、不读时钟也不碰文件系统——`used_at` 由调用方传入，故可无头单测。
auto push_recent_connection(ConnectionSettings &connection, std::string_view profile_id, std::int64_t used_at)
    -> void;

/// @brief 快捷键域：以 Command id 为锚的覆盖表（`SPEC.FEAT.PREF.04`、`SPEC.FEAT.WS.07`）。
///
/// 键＝命令 id（一律 ASCII 英文，中/英词条另经 `StringTable`，裁决 7.25⑬）；值＝组合键文本
/// （如 `Ctrl+Shift+P`，形态照框架 `KeyCombo::to_string()`）。表里没有的命令沿用注册表缺省绑定。
///
/// 落盘形态是 `[{command, combo}, ...]` 数组而不是「命令 id 作键」的映射：框架的点号路径模型
/// 会把键里的点当路径分隔符拆开（裁决 7.27②），且空映射作为对象落盘后会在装载时消失。
struct ShortcutsSettings {
    std::map<std::string, std::string> overrides{};

    /// @brief 逐字段全等比较（配置往返断言用）。
    [[nodiscard]] auto operator==(const ShortcutsSettings &other) const noexcept -> bool = default;
};

/// @brief 一份完整配置：四分类各一域 + 连接档案域（M3 的 `SPEC.FEAT.CONN.03`），
///        `Settings{}` 即 `SPEC.FEAT.PREF.06` 的首次启动形态。
struct Settings {
    AppearanceSettings appearance{};
    TerminalSettings terminal{};
    ConnectionSettings connection{};
    ShortcutsSettings shortcuts{};
    /// @brief 连接档案（SSH/本地终端的分组、收藏、搜索、quick connect、ssh config 导入）。
    ///        凭据按 `SPEC.FEAT.CONN.09` 只以 `SecretHandle` 引用/哨兵形态落盘，绝不存明文。
    std::vector<conn::Profile> profiles{};

    /// @brief 逐域全等比较：配置往返（写盘再读回）的判据就是它。
    [[nodiscard]] auto operator==(const Settings &other) const noexcept -> bool = default;
};

}  // namespace borealis::config
