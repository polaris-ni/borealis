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
    // ---- 连接管理器侧栏（UI_CONNECTIONS.draft.md；词条与 settings.* 同格角色）----
    {"connections.title", "连接"},
    {"connections.action.close", "关闭"},
    {"connections.action.connect", "连接"},
    {"connections.action.confirm", "确定"},
    {"connections.action.cancel", "取消"},
    {"connections.action.save", "保存"},
    {"connections.action.edit", "编辑"},
    {"connections.action.new", "新建"},
    {"connections.action.import", "导入 ~/.ssh/config"},
    {"connections.quick.placeholder", "host[:port] 即连"},
    {"connections.search.placeholder", "搜索档案"},
    {"connections.view.all", "全部"},
    {"connections.view.favorites", "收藏"},
    {"connections.view.groups", "分组"},
    {"connections.recent.title", "最近连接"},
    {"connections.notice.imported", "已导入 {0} 条"},
    {"connections.type.local", "本地终端"},
    {"connections.type.ssh", "SSH"},
    {"connections.field.name", "名称"},
    {"connections.field.host", "主机"},
    {"connections.field.port", "端口"},
    {"connections.field.user", "用户"},
    {"connections.field.auth", "认证方式"},
    {"connections.field.identity", "私钥路径"},
    {"connections.field.command", "命令"},
    {"connections.field.workdir", "工作目录"},
    {"connections.auth.agent", "Agent 密钥"},
    {"connections.auth.password", "口令"},
    {"connections.auth.privatekey", "私钥"},
    {"connections.auth.keyboard-interactive", "交互式"},
    {"connections.policy.accept_new", "首次接受"},
    {"connections.policy.yes", "严格核对"},
    {"connections.policy.no", "不核对"},
    {"connections.policy.ask", "询问"},
    {"connections.credential.password_title", "请输入 {0} 的口令"},
    {"connections.credential.passphrase_title", "请输入 {0} 的私钥 passphrase"},
    {"connections.credential.interactive_title", "请回答 {0} 的认证提示"},
    {"connections.wizard.title_new", "新建连接"},
    {"connections.wizard.title_edit", "编辑连接"},
    {"connections.wizard.issue_empty_name", "名称不能为空"},
    {"connections.wizard.issue_empty_host", "SSH 主机不能为空"},
    {"connections.wizard.issue_port", "端口须在 1..65535"},
    {"connections.wizard.issue_auth", "认证方式无效"},

    // ---- SSH 隧道面板（UI_TUNNEL.draft.md，裁决 7.97；state/error/listen 键由
    //      tunnel_format 唯一产出，面板不 switch 出第二套措辞）----
    {"tunnel.title", "隧道"},
    {"tunnel.empty", "暂无隧道：点「新建隧道」添加"},
    {"tunnel.header.running", "运行中 {0}"},
    {"tunnel.action.close", "关闭"},
    {"tunnel.action.new", "新建隧道"},
    {"tunnel.action.start", "启动"},
    {"tunnel.action.stop", "停止"},
    {"tunnel.action.edit", "编辑"},
    {"tunnel.action.delete", "删除"},
    {"tunnel.state.stopped", "已停止"},
    {"tunnel.state.dialing", "拨号中"},
    {"tunnel.state.active", "已就绪"},
    {"tunnel.state.backoff", "退避重试"},
    {"tunnel.state.failed", "已失败"},
    {"tunnel.error.dial_failed", "建连/认证失败"},
    {"tunnel.error.bind_failed", "监听端口起不来"},
    {"tunnel.error.remote_refused", "远端监听被服务器拒绝"},
    {"tunnel.error.channel_lost", "会话瞬断"},
    {"tunnel.listen.server_picked", "端口由服务器择定"},
    {"tunnel.listen.server_chosen", "服务器已择定端口"},
    {"tunnel.row.profile", "承载：{0}"},
    {"tunnel.row.retry_wait", "第 {0} 次重试 · {1}s 后"},
    {"tunnel.row.autostart_skipped", "自动启动已跳过：需先存凭据"},
    {"tunnel.notice.persist_failed", "隧道表落盘失败：本次改动只留在内存"},
    {"tunnel.kind.local", "本地 -L"},
    {"tunnel.kind.remote", "远端 -R"},
    {"tunnel.kind.dynamic", "SOCKS5 -D"},
    {"tunnel.confirm.title", "停止并删除？"},
    {"tunnel.confirm.body", "「{0}」正在运行，将先停止再删除。"},
    {"tunnel.confirm.action", "停止并删除"},
    {"tunnel.editor.title_new", "新建隧道"},
    {"tunnel.editor.title_edit", "编辑隧道"},
    {"tunnel.editor.field.name", "名称"},
    {"tunnel.editor.field.listen_addr", "监听地址"},
    {"tunnel.editor.field.listen_port", "监听端口"},
    {"tunnel.editor.field.target_host", "目标主机"},
    {"tunnel.editor.field.target_port", "目标端口"},
    {"tunnel.editor.field.profile", "承载档案"},
    {"tunnel.editor.field.autostart", "自动启动"},
    {"tunnel.editor.no_profile", "（无 SSH 档案）"},
    {"tunnel.editor.retry.base", "退避 ms"},
    {"tunnel.editor.retry.cap", "上限 ms"},
    {"tunnel.editor.retry.attempts", "次数"},
    {"tunnel.editor.retry.hint", "留空＝旧全局档（1s / 30s / 不限次）"},
    {"tunnel.editor.action.save", "保存"},
    {"tunnel.editor.action.cancel", "取消"},
    {"tunnel.editor.issue_name", "名称不能为空"},
    {"tunnel.editor.issue_profile", "需要选择一个 SSH 档案"},
    {"tunnel.editor.issue_listen_port", "监听端口须在合法范围（-R 允许 0）"},
    {"tunnel.editor.issue_target_host", "目标主机不能为空"},
    {"tunnel.editor.issue_target_port", "目标端口须在 1..65535"},
    {"tunnel.editor.issue_conflict", "与其他隧道抢同一监听点"},

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

    // ---- 配置韧性动作区（`SPEC.FEAT.PREF.07` 的 M2 腿：快照名单 + 一键回滚 + 本地导出导入）----
    // 说明里**不写份数**：那个数字的真值源是 `config::kSnapshotRetention`，而面板不 include `config`
    // （模块环，与 `SnapshotEntry` 同一条理由），写进模板就是在本件第二处钉一个 5。名单本身逐行摆在界面上，
    // 用户数得到。留痕里的文件名是**取值**（同 `settings.font.fallback` 的两个参数，也同降级对话框把备份
    // 路径逐字上屏那一档，裁决 7.76⑤），故走位置参数而不是拼进模板。
    {"settings.config.export", "导出配置"},
    {"settings.config.import", "导入配置"},
    {"settings.config.snapshots", "配置快照"},
    {"settings.config.back", "返回"},
    {"settings.snapshot.note", "快照取在每次保存之前，回滚会让这份配置回到那一个现场；面板上尚未落盘的改动会一起丢掉"},
    {"settings.snapshot.empty", "暂无快照：本仓还没有落过盘"},
    {"settings.snapshot.rollback", "回滚"},
    {"settings.config.exported", "已导出到 {0}"},
    {"settings.config.imported", "已导入 {0}"},
    {"settings.config.rolled_back", "已回滚到 {0}"},
    // 三条失败留痕**不含存储侧交回的那句原因**：那是 ASCII 诊断串，上中文界面即违 AGENTS.md §4.3 第 14 条，
    // 故它只进日志（与降级对话框把 `LoadReport::message` 只进 `AURORA_LOG_WARN` 同口径，裁决 7.76⑤）。
    {"settings.config.export_failed", "导出失败：目标文件没能写出"},
    {"settings.config.import_failed", "导入失败：那份文件不是一份可用的配置，当前设置未改动"},
    {"settings.config.rollback_failed", "回滚失败：当前设置未改动"},

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

    // ---- 工作区操作（`SPEC.FEAT.WS.06` 全屏切换的命令标题）----
    {"workspace.action.toggle_fullscreen", "切换全屏"},

    // ---- 撤销关闭标签（`SPEC.FEAT.WS.10` 的命令标题，待 WS.01 多标签落地后启用）----
    {"tab.action.undo_close", "重开最近关闭的标签"},

    // ---- dead-session 一键重启（`SPEC.FEAT.WS.05`：`session.restart` 命令标题 + 视口浮层两处文案）----
    {"session.restart.title", "重启会话"},
    {"session.restart.hint", "会话已退出"},
    {"session.restart.button", "重启"},

    // ---- 命令面板（`SPEC.FEAT.WS.07`，判据文 `codespec/UI_WORKSPACE.draft.md` §4）----
    // 占位符与空态两条的 **key 由框架命名**（`CommandPalette::AURORA_DEFAULT_*_KEY`，G38 / G39 回货形
    // 态），本件只登记取值，故这两串 key 不得自创——key 一漂移，面板就静默回落英文（裁决 **7.92**②）。
    {"command_palette.title", "命令面板"},
    {"command_palette.placeholder", "输入命令..."},
    {"command_palette.no_results", "没有匹配的命令"},
    {"command_palette.action.open", "打开命令面板"},

    // ---- 调试面板（`SPEC.NF.RELI.01`：解析降级 / 非法字节 / 背压水位三族计数器的上屏形态）----
    // 模板里的 {n} 一律是**取值**（计数值与行标题的已解析串），措辞不含 ASCII 诊断通道。
    {"diagnostics.title", "诊断"},
    {"diagnostics.action.open", "打开诊断面板"},
    {"diagnostics.empty", "没有活动的会话"},
    {"diagnostics.session", "标签 {0} · 分屏 {1}"},
    {"diagnostics.parse", "未知序列 {0}，被打断 {1}"},
    {"diagnostics.decode", "非法字节 {0}，码点 {1}"},
    {"diagnostics.queue", "水位 {0}（峰值 {1}），过载 {2}，合并 {3}，让出 {4}"},

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
    {"connection.ssh.reconnect_base_delay_ms", "重连首轮退避"},
    {"connection.ssh.reconnect_max_delay_ms", "重连退避上限"},
    {"connection.ssh.reconnect_attempts", "自动重连次数"},
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
    {"settings.shortcut.column.title", "动作名"},
    {"settings.shortcut.column.category", "分组"},
    {"settings.shortcut.column.binding", "当前组合键"},
    {"settings.shortcut.column.note", "标注"},
    {"settings.shortcut.unbound", "未绑定"},
    {"settings.shortcut.edit_title", "编辑键位绑定"},
    {"settings.shortcut.conflict_warning", "与以下命令冲突"},
    {"settings.action.edit", "编辑"},
    {"settings.action.restore_defaults", "恢复默认"},
    {"settings.action.confirm", "确定"},
    {"settings.action.cancel", "取消"},

    // ---- 提交未通过的原因（`ui::CommitIssue` 的十二个非 `None` 值）----
    {"settings.issue.unknown_key", "这个键不在当前版本的设置表里"},
    {"settings.issue.not_loaded", "这个键没有装载到面板上"},
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

    // ---- SFTP 浏览器（SPEC.FEAT.CONN.04；codespec/UI_SFTP.draft.md §4，裁决 7.95）----
    {"sftp.title", "SFTP 文件"},
    {"sftp.action.close", "关闭"},
    {"sftp.action.retry", "重试"},
    {"sftp.action.up", "向上"},
    {"sftp.action.refresh", "刷新"},
    {"sftp.action.upload", "上传"},
    {"sftp.action.download", "下载"},
    {"sftp.action.new_folder", "新建文件夹"},
    {"sftp.action.new_file", "新建文件"},
    {"sftp.action.rename", "重命名"},
    {"sftp.action.delete", "删除"},
    {"sftp.action.cancel", "取消"},
    {"sftp.action.confirm", "确定"},
    {"sftp.column.local", "本地"},
    {"sftp.column.remote", "远程"},
    {"sftp.column.name", "名称"},
    {"sftp.column.size", "大小"},
    {"sftp.column.mtime", "修改时间"},
    {"sftp.column.permissions", "权限"},
    {"sftp.status.connecting", "连接中..."},
    {"sftp.status.connected", "已连接"},
    {"sftp.status.failed", "连接失败"},
    {"sftp.status.not_ssh", "当前标签不是 SSH 会话"},
    {"sftp.error.not_connected", "未连接"},
    {"sftp.error.network", "网络错误"},
    {"sftp.error.permission_denied", "权限不足"},
    {"sftp.error.not_found", "路径不存在"},
    {"sftp.error.cancelled", "已取消"},
    {"sftp.error.local_io", "本地读写失败"},
    {"sftp.error.protocol", "协议错误"},
    {"sftp.delete.title", "删除确认"},
    {"sftp.delete.body", "确定删除 {0} 吗？此操作不可恢复。"},
    {"sftp.delete.dir_note", "仅可删除空目录。"},
    {"sftp.delete.confirm", "删除"},
    {"sftp.input.name", "名称"},
    {"sftp.input.new_folder", "新建文件夹"},
    {"sftp.input.new_file", "新建文件"},
    {"sftp.input.rename", "重命名"},
    {"sftp.transfer.uploading", "上传 {0}"},
    {"sftp.transfer.downloading", "下载 {0}"},
    {"sftp.transfer.queued", "排队 {0}"},
};

/// @brief `CommitIssue` → 词条 key：本件是唯一把该枚举翻成文案的地方（表单件只回枚举，不产文案）。
[[nodiscard]] auto issue_key(CommitIssue issue) -> std::string_view {
    switch (issue) {
    case CommitIssue::UnknownKey:
        return "settings.issue.unknown_key";
    case CommitIssue::NotLoaded:
        return "settings.issue.not_loaded";
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
