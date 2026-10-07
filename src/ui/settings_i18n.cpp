#include "settings_i18n.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "aurora/i18n/string_table.h"

namespace borealis::ui {
namespace {

/// @brief 面板词条所在的区域标签（同时是字符串表的缺省档，见文件头③）。
constexpr std::string_view kTag = "zh";

/// @brief key → 中文模板。key 一律是落盘点号路径或 `settings.` 前缀的界面文案键。
///
/// CJK-LITERAL: locale-output - 本表整张就是面板的上屏文案，换成英文被测事实即消失（裁决 7.52 的 S11）。
/// 设置项标签与 `codespec/UI_SETTINGS.draft.md` 的 SVG 行标签逐字对齐；SVG 未画出的行按该稿 §2 表的
/// 控件列取名。SVG 上被该稿撤销的四格（第五类「高级」、跟随系统浅色、多行粘贴警告、OSC 52 三态）**不在此表**，
/// 否则面板会画出一个 schema 里没有的标签。
const std::map<std::string, std::string, std::less<>> kStrings{
    // ---- 面板骨架 ----
    {"settings.title", "设置"},
    {"settings.subtitle", "修改即时生效"},
    {"settings.close", "关闭"},
    {"settings.action.open", "打开设置"},
    {"settings.page.appearance", "外观"},
    {"settings.page.terminal", "终端"},
    {"settings.page.connection", "连接"},
    {"settings.page.shortcuts", "快捷键"},
    {"settings.action.unset", "未配"},
    {"settings.action.theme_default", "恢复主题默认"},
    {"settings.badge.customized", "自定义"},
    {"settings.badge.deferred", "延后"},
    {"settings.badge.next_session", "下次会话生效"},

    // ---- 组顶说明（S15 的延后档在组级的那一半；措辞须点名「哪个消费方还没开工」）----
    // 三条同一句式：行尾角标说的是这一行，组顶说明说的是这一组，两者不重复也不冲突（B3-a 的同一口径）。
    {"settings.note.status_bar", "入口先于消费方落地：关闭状态栏件尚未开工，本组改动只落盘"},
    {"settings.note.ssh", "入口先于消费方落地：SSH 连接族尚未开工，本组改动只落盘"},
    {"settings.note.serial", "入口先于消费方落地：串口连接族尚未开工，本组改动只落盘"},
    {"settings.note.shortcuts", "入口先于消费方落地：键位重绑尚未开工，覆盖表里的组合键不会生效"},

    // ---- 回退链区段（A5-a：顺序可改、逐行可删、末尾追加）----
    // 上限的数字**不写进模板**：它的真值源是框架头里的 `AURORA_TEXT_FALLBACK_CHAIN_MAX`，故经 `{0}`
    // 位置参数交出（裁决 7.62）。三枚按钮的字形是符号而非词，仍留本表（同一张上屏文案表）。
    {"settings.chain.up", "↑"},
    {"settings.chain.down", "↓"},
    {"settings.chain.remove", "✕"},
    {"settings.chain.filter", "输入族名以添加"},
    {"settings.chain.empty", "回退链为空：只走框架的全局默认链"},
    {"settings.chain.full", "已达上限 {0} 项：追加口已关闭"},
    {"settings.chain.truncated", "链长超过上限 {0}：超出部分不参与绘制"},
    {"settings.chain.no_match", "没有匹配的族名"},

    // ---- 字体族区段（A4-b：配置那族不可用时在字体行下方留痕，而不是把按钮显示成那族）----
    // 一句话覆盖「目录里没有」与「目录里有但度量非等宽」两档（两者对用户是同一件事），两个位置参数是
    // **取值**而非词条 key，故交字面档。
    {"settings.font.fallback", "配置的 {0} 不可用：实际使用 {1}"},

    // ---- 快捷键只读表（D1-a 的列名 / D2-a 的标注两档 / 未绑定那一格）----
    // 表体四列的列名是本件自己的排版量而非 catalog 键，故 key 前缀走 `settings.shortcut.`；
    // 「重复」那一条的位置参数是**另一行的动作名**（取值），与 `settings.font.fallback` 同档。
    {"settings.shortcut.column.title", "动作"},
    {"settings.shortcut.column.category", "分组"},
    {"settings.shortcut.column.binding", "当前组合键"},
    {"settings.shortcut.column.note", "标注"},
    {"settings.shortcut.unbound", "未绑定"},
    {"settings.shortcut.conflict_with", "与「{0}」重复"},
    {"settings.shortcut.conflict_workspace", "与分屏键位冲突"},

    // ---- 启动降级提示（`SPEC.FEAT.PREF.07`，判据文 §7 的 S13①）----
    // 标题按 `LoadOutcome` 两态各一条（同一句话覆盖不了「读不出」与「读得懂但不归本程序管」两件事），
    // 版本号与备份路径都是**取值**故走位置参数，与 `settings.font.fallback` 同档。
    // `LoadReport::message` 不在此表：它是 ASCII 英文诊断，上中文界面即违 AGENTS.md §4.3 第 14 条。
    {"settings.startup.corrupt.title", "配置文件已损坏"},
    {"settings.startup.version.title", "配置文件版本高于本程序"},
    {"settings.startup.corrupt.body", "无法读取这个配置文件，本程序已回落到默认设置启动。"},
    {"settings.startup.version.body", "这个配置文件由更新的版本写出（版本 {0}，本程序支持 {1}），已回落到默认设置启动。"},
    {"settings.startup.backup", "损坏的文件已备份为：{0}"},
    {"settings.startup.writes_refused", "未能备份损坏的文件：本会话不会写入配置文件，原文件保持原样"},
    {"settings.startup.rejected", "以下设置项的值超出可用范围，已回落到默认值："},
    {"settings.startup.ack", "知道了"},

    // ---- 终端内搜索浮层（`SPEC.FEAT.INTERACT.04`，判据文 `codespec/UI_SEARCH.draft.md` §4 第 6 条）----
    // 那一条列出的就是下面那十一句界面词条：chip 上的 `Aa` / `.*` 与按钮的 `↑` / `↓` 是**符号而非文案**，
    // 不进表（A1-d 的宽度算式要求它们在 44 dp 单行内恰占 40 dp，而中文标签会把 E 段的三档全部顶穿）；
    // 但符号档的**无障碍标签**进表，于是「屏上是符号、树里是中文」由 `set_accessibility_label` 一处兑现。
    // `search.count_cap` 的位置参数是 `ui::kMaxSearchMatches` 的十进制形态（取值，B3 的 `+` 即裁决
    // 7.78④「不得谎报总数」在界面上的兑现形态），故不在界面里写死那个串。
    // `search.action.open` 不在 §4 第 6 条那十一句之内，是 F1-a 那条打开入口的命令标题——它与
    // `settings.action.open` 同一格角色（快捷键只读表的「动作名」列），故同表登记（裁决 7.81）。
    {"search.action.open", "打开搜索"},
    {"search.placeholder", "搜索终端内容"},
    {"search.count_none", "无匹配"},
    {"search.count_cap", "{0}+"},
    {"search.count_invalid", "表达式非法"},
    {"search.pending_enter", "按 Enter 应用"},
    {"search.reopen_hint", "按 Enter 搜索"},
    {"search.toggle_case", "区分大小写"},
    {"search.toggle_regex", "正则表达式"},
    {"search.prev", "上一个"},
    {"search.next", "下一个"},
    {"search.close", "关闭"},

    // ---- 外观域 ----
    {"appearance.theme", "主题"},
    {"appearance.palette.basic", "16 色重映射"},
    {"appearance.palette.foreground", "默认前景色"},
    {"appearance.palette.background", "默认背景色"},
    {"appearance.palette.cursor", "光标色"},
    {"appearance.palette.selection", "选区色"},
    {"appearance.palette.bold_is_bright", "粗体提亮"},
    {"appearance.palette.min_contrast_enabled", "启用最小对比度"},
    {"appearance.palette.min_contrast", "最小对比度阈值"},
    {"appearance.font_family", "等宽字体"},
    {"appearance.font_size_pt", "字号"},
    {"appearance.font_line_height", "行高"},
    {"appearance.font_letter_spacing_dp", "字距"},
    {"appearance.font_fallback_chain", "CJK 缺字回退链"},
    {"appearance.viewport_padding_dp", "视口内边距"},
    {"appearance.cursor_shape", "光标形态"},
    {"appearance.cursor_blinking", "光标闪烁"},
    {"appearance.cursor_blink_period_ms", "闪烁频率"},
    {"appearance.sidebar_collapsed", "侧栏收起"},
    {"appearance.tab_name_priority", "标签名优先级"},
    {"appearance.status_bar.show_connection", "连接状态"},
    {"appearance.status_bar.show_reconnect", "重连状态"},
    {"appearance.status_bar.show_cursor_position", "光标位置"},
    {"appearance.status_bar.show_encoding", "会话编码"},
    {"appearance.status_bar.show_grid_size", "行列数"},
    {"appearance.status_bar.show_font_size", "字号"},
    {"appearance.status_bar.show_theme", "当前主题"},
    {"appearance.status_bar.show_scrollback", "回看行数"},
    {"appearance.status_bar.show_clipboard_policy", "剪贴板策略"},
    {"appearance.status_bar.show_input_latency", "输入延迟"},

    // ---- 终端域 ----
    {"terminal.scrollback_limit", "scrollback 行数"},
    {"terminal.ambiguous_width", "宽字符判定"},
    {"terminal.long_line", "超长行处理"},
    {"terminal.bell", "铃声"},
    {"terminal.encoding", "会话编码"},
    {"terminal.paste_newlines", "粘贴换行处理"},
    {"terminal.right_click", "右键行为"},
    {"terminal.copy_on_select", "选中即复制"},
    {"terminal.trim_pasted_trailing_space", "复制时剥行尾空白"},
    {"terminal.smart_line_join", "合并续行"},
    {"terminal.strip_tmux_border_chars", "剥 tmux 细线制表符"},
    {"terminal.word_delimiters", "双击选词断点集"},

    // ---- 连接域 ----
    {"connection.local_shell", "默认 shell"},
    {"connection.startup_directory", "启动目录"},
    {"connection.ssh.port", "端口"},
    {"connection.ssh.auth_method", "认证方式"},
    {"connection.ssh.agent_forwarding", "agent 转发"},
    {"connection.ssh.keepalive_interval_sec", "保活间隔"},
    {"connection.ssh.connect_timeout_sec", "连接超时"},
    {"connection.serial.baud", "波特率"},
    {"connection.serial.data_bits", "数据位"},
    {"connection.serial.stop_bits", "停止位"},
    {"connection.serial.parity", "校验位"},
    {"connection.serial.line_ending", "行尾符"},
    {"connection.serial.encoding", "串口编码"},
    {"connection.session_logging", "会话日志"},
    {"connection.session_log_dir", "日志目录"},

    // ---- 快捷键域 ----
    {"shortcuts.overrides", "键位覆盖表"},

    // ---- 提交未通过的原因（`ui::CommitIssue` 的十三个非 `None` 值）----
    {"settings.issue.unknown_key", "这个键不在当前版本的设置表里"},
    {"settings.issue.not_loaded", "这个键没有装载到面板上"},
    {"settings.issue.read_only", "这一项暂不支持在面板里修改"},
    {"settings.issue.domain_mismatch", "这个值的类型与该项不符"},
    {"settings.issue.text_not_accepted", "这一项不能直接输入文本"},
    {"settings.issue.malformed_number", "请输入一个数字"},
    {"settings.issue.not_integral", "这一项只接受整数"},
    {"settings.issue.out_of_range", "超出允许范围"},
    {"settings.issue.not_a_choice", "请从列表里选择一个取值"},
    {"settings.issue.malformed_color", "色值须是 #RRGGBB 七位形态"},
    {"settings.issue.unset_not_allowed", "这一项必须配置一个颜色"},
    {"settings.issue.slot_out_of_range", "色板格序号超出 0 到 15"},
    {"settings.issue.table_size_wrong", "调色板必须是 16 格"},
};

/// @brief `CommitIssue` → 词条 key：本件是唯一把该枚举翻成文案的地方（表单件只回枚举，不产文案）。
[[nodiscard]] auto issue_key(CommitIssue issue) -> std::string_view {
    switch (issue) {
    case CommitIssue::UnknownKey:
        return "settings.issue.unknown_key";
    case CommitIssue::NotLoaded:
        return "settings.issue.not_loaded";
    case CommitIssue::ReadOnly:
        return "settings.issue.read_only";
    case CommitIssue::DomainMismatch:
        return "settings.issue.domain_mismatch";
    case CommitIssue::TextNotAccepted:
        return "settings.issue.text_not_accepted";
    case CommitIssue::MalformedNumber:
        return "settings.issue.malformed_number";
    case CommitIssue::NotIntegral:
        return "settings.issue.not_integral";
    case CommitIssue::OutOfRange:
        return "settings.issue.out_of_range";
    case CommitIssue::NotAChoice:
        return "settings.issue.not_a_choice";
    case CommitIssue::MalformedColor:
        return "settings.issue.malformed_color";
    case CommitIssue::UnsetNotAllowed:
        return "settings.issue.unset_not_allowed";
    case CommitIssue::SlotOutOfRange:
        return "settings.issue.slot_out_of_range";
    case CommitIssue::TableSizeWrong:
        return "settings.issue.table_size_wrong";
    case CommitIssue::None:
        return {};
    }
    return {};
}

}  // namespace

auto settings_locale() -> const aurora::Locale & {
    // 一处构造、逐次同值：`tag()` 每次调用现拼串，故面板侧只留这一份 `Locale`。
    static const aurora::Locale locale{std::string{kTag}, {}};
    return locale;
}

auto install_settings_strings() -> void {
    auto &table = aurora::default_string_table();
    table.set_default_locale(settings_locale());
    for (const auto &[key, text] : kStrings) {
        table.add(settings_locale(), key, text);
    }
}

auto settings_text(std::string_view key, std::vector<aurora::LocalizedString> args) -> aurora::LocalizedString {
    return aurora::LocalizedString::tr(std::string{key}, std::move(args));
}

auto settings_label(std::string_view key, std::vector<aurora::LocalizedString> args) -> std::string {
    return settings_text(key, std::move(args)).resolve(&aurora::default_string_table(), settings_locale());
}

auto has_settings_string(std::string_view key) -> bool {
    return aurora::default_string_table().lookup(std::string{key}, settings_locale()).has_value();
}

auto settings_issue_text(CommitIssue issue) -> aurora::LocalizedString {
    const auto key = issue_key(issue);
    if (key.empty()) {
        return aurora::LocalizedString{std::string_view{}};
    }
    return settings_text(key);
}

}  // namespace borealis::ui
