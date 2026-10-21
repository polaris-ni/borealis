#include "settings_i18n.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "aurora/i18n/string_table.h"

#include "borealis/config/settings.h"

#include "config/schema_names.h"

namespace borealis::ui {
namespace {

/// @brief 一档词条的容器形态：key → 模板。`std::less<>` 让 `find(string_view)` 不必构造临时串。
///
/// 两档共用这一种形态，于是「同一 key 在两档各注册一次」这件事在结构上只有一处写法（`SPEC.FEAT.PREF.05`
/// 稿 D4：两张表同文件相邻，可并排校对）。
///
/// 每一档内**同一条目只许出现一次**：`std::map` 的 initializer-list 走 insert 语义，重复 key 的后一份被
/// 静默丢弃（该稿 F8 的实测形态），于是「条数」与「key 集合」两类断言对这种错法天然无感，只有逐字验串
/// 才打得中（该稿 §8 的 m6 即按这条定形）。
using Tier = std::map<std::string, std::string, std::less<>>;

/// @brief 中文档（`zh`）的 key → 模板。key 一律是落盘点号路径或 `settings.` 前缀的界面文案键。
///
/// CJK-LITERAL: locale-output - 本表整张就是面板的上屏文案，换成英文被测事实即消失（裁决 7.52 的 S11）。
/// 设置项标签与 `codespec/UI_SETTINGS.draft.md` 的 SVG 行标签逐字对齐；SVG 未画出的行按该稿 §2 表的
/// 控件列取名。SVG 上被该稿撤销的四格（第五类「高级」、跟随系统浅色、多行粘贴警告、OSC 52 三态）**不在此表**，
/// 否则面板会画出一个 schema 里没有的标签。
const Tier kStringsZh{
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
    // 向导第四型的类型卡（`SPEC.FEAT.CONN.06`，裁决 7.112 D2①）：本棒的新词条**只有这一枚**
    // ——字段名复用 host/port 两行、掉线留痕复用既有措辞，故 Telnet 没有自己的词条族。
    // 「串口终端」那枚在下面的串口块里，随该块的参数档一族同处登记。
    {"connections.type.telnet", "Telnet"},
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
    // 措辞不带型名：SSH 与 Telnet 共用这一档问题码（裁决 7.112 D2① 的「零新增问题码」），
    // 写成「SSH 主机不能为空」会在 Telnet 字段集上把用户指向另一个型。
    {"connections.wizard.issue_empty_host", "主机不能为空"},
    {"connections.wizard.issue_port", "端口须在 1..65535"},
    {"connections.wizard.issue_auth", "认证方式无效"},
    // ---- 串口向导与热改波特率（UI_SERIAL.draft.md，裁决 7.110；`connections.serial.*` 的
    //      参数档与可达性归因由 `serial_format` 唯一产出，装配与向导不 switch 第二套措辞）----
    {"connections.type.serial", "串口终端"},
    {"connections.field.device", "设备"},
    {"connections.field.baud", "波特率"},
    {"connections.field.data_bits", "数据位"},
    {"connections.field.stop_bits", "停止位"},
    {"connections.field.parity", "校验位"},
    {"connections.field.flow", "流控"},
    {"connections.field.line_ending", "发送行尾"},
    {"connections.field.encoding", "编码"},
    {"connections.action.refresh_devices", "刷新"},
    {"connections.action.custom_path", "自定义…"},
    {"connections.action.pick_from_list", "列表选择"},
    {"connections.device.placeholder", "请选择设备"},
    {"connections.serial.parity.none", "无"},
    {"connections.serial.parity.even", "偶"},
    {"connections.serial.parity.odd", "奇"},
    {"connections.serial.flow.none", "无"},
    {"connections.serial.flow.hardware", "硬件 RTS/CTS"},
    {"connections.serial.flow.software", "软件 XON/XOFF"},
    {"connections.serial.line_ending.lf", "LF"},
    {"connections.serial.line_ending.cr", "CR"},
    {"connections.serial.line_ending.crlf", "CRLF"},
    {"connections.serial.access.denied", "无权限"},
    {"connections.serial.access.busy", "已被占用"},
    {"connections.serial.access.unavailable", "暂不可开"},
    {"connections.wizard.issue_device", "设备不能为空"},
    {"connections.wizard.issue_baud", "波特率须在档位表内"},
    {"connections.wizard.issue_data_bits", "数据位须在 5..8"},
    {"connections.wizard.issue_stop_bits", "停止位须在 1..2"},
    {"serial.set_baud.title", "切换串口波特率"},
    {"serial.set_baud.current", "当前 {0}"},
    {"serial.set_baud.applied", "已切到 {0}"},
    {"serial.set_baud.rejected", "设备侧拒绝：{0}"},
    {"serial.set_baud.not_serial", "当前会话不是串口"},

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

    // ---- SSH 密钥管理面板（UI_KEYS.draft.md，裁决 7.106/7.107/7.108；全部 key 由
    //      keys_format 唯一产出，面板不 switch 出第二套措辞）----
    {"keys.title", "SSH 密钥"},
    {"keys.empty", "暂无密钥：点「生成密钥」创建一把，或添加扫描目录"},
    {"keys.header.count", "{0} 把"},
    {"keys.action.add_dir", "添加目录"},
    {"keys.action.generate", "生成密钥"},
    {"keys.action.close", "关闭"},
    {"keys.action.copy", "复制"},
    {"keys.action.push", "推送"},
    {"keys.action.export", "导出公钥"},
    {"keys.action.delete", "删除"},
    {"keys.op.scan", "扫描中"},
    {"keys.op.generate", "生成中"},
    {"keys.op.export", "导出中"},
    {"keys.op.delete", "删除中"},
    {"keys.op.push", "推送中"},
    {"keys.row.has_passphrase", "有口令"},
    {"keys.row.no_passphrase", "无口令"},
    {"keys.row.public_missing", "公钥缺失"},
    {"keys.issue.name.empty", "文件名不能为空"},
    {"keys.issue.name.too_long", "文件名过长"},
    {"keys.issue.name.hidden", "点开头的名称会成为隐藏文件"},
    {"keys.issue.name.has_separator", "文件名不能包含路径分隔符"},
    {"keys.issue.name.control_char", "文件名含不可见字符"},
    {"keys.issue.name.reserved", "这个名称是系统保留名"},
    {"keys.issue.comment.too_long", "注释过长"},
    {"keys.issue.comment.control_char", "注释含不可见字符"},
    {"keys.dir.unreachable", "目录不可达：{0}"},
    {"keys.generate.title", "生成密钥"},
    {"keys.generate.field.bits", "位数"},
    {"keys.generate.field.name", "文件名"},
    {"keys.generate.field.comment", "注释"},
    {"keys.generate.field.directory", "目标目录"},
    {"keys.generate.field.passphrase", "口令（可空）"},
    {"keys.generate.field.passphrase_again", "再输一遍口令"},
    {"keys.generate.browse", "浏览"},
    {"keys.generate.action.save", "生成"},
    {"keys.generate.action.cancel", "取消"},
    {"keys.generate.invalid_request", "请求不成立：检查目录与文件名"},
    {"keys.generate.name_taken", "同名文件已存在"},
    {"keys.generate.directory_unusable", "目标目录不可用"},
    {"keys.generate.failed", "生成失败"},
    {"keys.generate.issue_mismatch", "两次输入的口令不一致"},
    {"keys.kind.ed25519", "Ed25519"},
    {"keys.kind.rsa", "RSA"},
    {"keys.export.already_exists", "公钥文件已存在，未覆盖"},
    {"keys.export.needs_passphrase", "这把私钥带口令，需要口令才能导出"},
    {"keys.export.failed", "导出公钥失败"},
    {"keys.delete.title", "删除密钥"},
    {"keys.delete.warn", "私钥与配对的公钥将被删除，此操作不可恢复。"},
    {"keys.delete.action", "删除"},
    {"keys.delete.action.cancel", "取消"},
    {"keys.delete.refused_symlink", "符号链接不删：链接与目标都保持原样"},
    {"keys.delete.not_found", "文件已不在原处"},
    {"keys.delete.failed", "删除失败"},
    {"keys.push.title", "推送公钥"},
    {"keys.push.field.profile", "目标档案"},
    {"keys.push.no_profile", "（无 SSH 档案）"},
    {"keys.push.action", "推送"},
    {"keys.push.action.close", "关闭"},
    {"keys.push.already_authorized", "这把公钥已在授权表里"},
    {"keys.push.appended", "公钥已追加到服务器的授权表"},
    {"keys.push.stage.dial", "连接"},
    {"keys.push.stage.auth", "认证"},
    {"keys.push.stage.exec", "执行"},
    {"keys.push.stage.done", "完成"},
    {"keys.push.fail.session_unallocated", "未能建立会话"},
    {"keys.push.fail.network", "网络错误：连不上服务器"},
    {"keys.push.fail.host_key", "主机密钥核对未通过"},
    {"keys.push.fail.auth", "认证被拒"},
    {"keys.push.fail.exec_refused", "服务器拒绝执行授权表命令"},
    {"keys.push.fail.write_rejected", "授权表写入未成功"},
    {"keys.push.fail.invalid_line", "这把公钥拼不出有效内容，未发起推送"},
    {"keys.notice.copied", "已复制到剪贴板"},


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
    {"settings.badge.next_launch", "重启后生效"},

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
    // 这五串**只在此处注册一次**：本表是 `std::map`，initializer-list 构造走 insert 语义，同 key 的
    // 后一份会被**静默丢弃**（`SPEC.FEAT.PREF.05` 稿 F8 读到的正是这一处踩过的事实）。
    {"settings.shortcut.column.title", "动作名"},
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

    // ---- 工作区操作（`SPEC.FEAT.WS.06` 全屏切换、`SPEC.FEAT.WS.03` 新开一扇窗的命令标题）----
    {"workspace.action.toggle_fullscreen", "切换全屏"},
    {"workspace.action.new_window", "新建窗口"},

    // ---- 广播输入（`SPEC.FEAT.WS.09`，稿 `codespec/UI_WORKSPACE_BROADCAST.draft.md` §2.2）----
    // 六句是横幅、pill 与成员命名的**全部**上屏文案。标题的 {0} 是成员名（同标签多 pane 已由装配层
    // 追加「窗格 N」）、{1} 是去重后的会话数；两者都是取值，故走位置参数而不是拼进模板。
    // 句末的「点击此处退出」是 D8 改口的落点：`Esc` 留给终端，横幅是退出整组的唯一入口。
    {"workspace.broadcast.title", "广播输入已开启：{0} 共 {1} 个会话"},
    {"workspace.broadcast.hint", "仅组内窗格键入时同步发送；点击此处退出"},
    {"workspace.broadcast.exit", "退出广播"},
    {"workspace.broadcast.badge", "广播 B"},
    {"workspace.broadcast.pane", "窗格 {0}"},
    {"workspace.broadcast.unnamed", "未命名会话"},

    // ---- 撤销关闭标签（`SPEC.FEAT.WS.10` 的命令标题，待 WS.01 多标签落地后启用）----
    {"tab.action.undo_close", "重开最近关闭的标签"},

    // ---- dead-session 一键重启（`SPEC.FEAT.WS.05`：`session.restart` 命令标题 + 视口浮层两处文案）----
    {"session.restart.title", "重启会话"},
    {"session.restart.hint", "会话已退出"},
    {"session.restart.button", "重启"},

    // ---- SSH 自动重连浮层两态（`SPEC.FEAT.WS.05` 的 SSH 腿，裁决 7.99 D5；草图屏 A/B）----
    // 终态三档的措辞只以快照里的**档位**分档：关闭原因本身不外传（D3 只交档位），故「不自动重试」
    // 那一档的文案必须同时罩得住认证失败与主机密钥拒绝两种原因。
    {"session.reconnect.line", "连接已断开 · 第 {0} / {1} 次自动重连"},
    {"session.reconnect.countdown", "{0} 秒后自动重试"},
    {"session.reconnect.retry_now", "立即重试"},
    {"session.reconnect.stop", "停止重连"},
    {"session.reconnect.exhausted", "重试 {0} 次仍失败 · 已停止"},
    {"session.reconnect.not_reconnectable", "认证被拒 · 不自动重试"},
    {"session.reconnect.user_stopped", "自动重连已停止"},
    {"session.restart.by_profile", "重启（按档案）"},
    {"session.restart.retry_credential", "重试并换凭据"},

    // ---- 发送侧「不可表示字符」一次性提示卡（`SPEC.FEAT.TERM.09` 的提示腿，裁决 7.104 的 D4①）----
    // 第二行只指路径、不报当前处置档名：那枚下拉显示的是 ASCII 档名本身（replace / drop /
    // pass_through_utf8），卡片报中文档名会对不上、报 ASCII 又违背界面语言；档名中文化属设置面板本体。
    // 路径三段各与真标签同源（`settings.page.terminal` ＋ `terminal.unrepresentable`）。
    {"terminal.notice.unrepresentable.line", "{0} 发不出这一段里的 {1} 个字符"},
    {"terminal.notice.unrepresentable.action", "已按当前处置档发送 · 改档见 设置 › 终端 › 不可表示字符处置"},

    // ---- 右键菜单与「会话日志」一族（`SPEC.FEAT.INTERACT.03` 的三条目 + `SPEC.FEAT.CONN.11`）----
    // 前两条是既有菜单文案**换来源**而不是换措辞：`menu_label` 自此一族三个条目同走词条表，
    // 与视口里 dead-session 那几句同一条路径（裁决 7.116 批 2 的口径：新增文案进词条表）。
    // 风险提示两句合成一行显示（框架的 `au::confirm` 只吃一条 message），路径完整报出是判据 3
    // 那句「将写入：完整路径」的落点；开启失败的回报两条各自独立，因为「正则非法」与「建不了文件」
    // 的下一步动作完全不同（改模式 vs 看目录）。
    {"menu.item.copy", "复制"},
    // 第二条只在最近一条完成块有非空输出时出现（`SPEC.FEAT.INTEG.01`，裁决 7.123 D3①）：
    // 与链接那条同一口径——出现与否由纯件 `plan_right_click` 决定，本条只管上屏措辞。
    {"menu.item.copy_command_output", "复制上一命令的全部输出"},
    {"menu.item.paste", "粘贴"},
    // 第四条只在右键落点扫到链接时出现（`codespec/UI_URL.draft.md` 的 D12①）：它的出现与否由纯件
    // `plan_right_click` 决定，本条只管上屏措辞。
    {"menu.item.open_link", "打开链接"},
    {"session.log.menu.start", "开启会话日志"},
    {"session.log.menu.stop", "停止会话日志"},
    {"session.log.command.start", "会话日志：开启（当前标签）"},
    {"session.log.command.stop", "会话日志：停止（当前标签）"},
    {"session.log.confirm.title", "开启会话日志"},
    {"session.log.confirm.sensitive", "日志可能包含密码、令牌或其他敏感信息。"},
    {"session.log.confirm.path", "将写入：{0}"},
    {"session.log.alert.bad_pattern", "追加的正则无法编译，会话日志未开启：{0}"},
    {"session.log.alert.cannot_open", "无法创建日志文件，会话日志未开启：{0}"},
    // 写失败那句不带路径：路径是 ASCII 诊断，只进日志（AGENTS §4.3 第 14 条的诊断不属中文例外）。
    {"session.log.alert.write_failed", "日志写入失败，已停止记录"},
    {"settings.note.session_log",
     "能力闸：开启后仍需逐会话显式点「开启会话日志」。日志目录里的旧文件本期不清理、不轮转。"},
    // 读方向那道闸的旁注（`SPEC.FEAT.CONN.12`，裁决 D9① 的**赔付处**）：拒绝档是刻意静默的——
    // 远端程序安装脚本与 `viminfo` 探测都会常规地读一次剪贴板，逐条弹提示就成了弹窗骚扰；而那一族
    // 计数住在 `OscState` 里、调试面板本期不渲染 OSC 那一行（G17），于是屏幕上**查不到**任何痕迹。
    // 「界面上把话说清」因此不是措辞偏好而是唯一可用的赔付：把三档各自的后果一次讲完。
    // 三档名**照上屏的候选原文写**（`deny` / `allow` / `ask`）而不是译成「禁止 / 允许 / 每次询问」：
    // 面板的下拉直接把落盘名交给 `aurora::Dropdown`（批 3 实测，`right_click` 与 `paste_newlines` 同例），
    // 旁注若改用另一套中文名，用户就在框里找不到旁注所说的那一档——赔付就成了新的困惑源。
    {"settings.note.clipboard_read",
     "三态各档的后果：deny 与超限都只回送一条空响应，不弹提示、屏上也查不到；改档即刻对所有在途"
     "标签生效；ask 档每条读请求各问一次（同一会话最多同时挂 8 条，超出当场按 deny 答）。"},

    // 打开链接那三行的两条旁注（判据 5／8）：第一行要说清「两枚键各管一个来源」，否则用户会以为
    // 改上面那枚就管了全部链接；第二行要说清白名单**不参与校验**——填了不等于生效，空串或全非法的
    // 表现是「什么都不可以点」，这一档在面板里没有任何别的回执。
    {"settings.note.link_confirm",
     "两枚键各管一个来源：osc8 是程序显式声明的目标，heuristic 是按形状猜出来的串。"
     "ask_once 记住的是协议 + 主机 + 端口，同域第二次不再问，换个端口仍会问。"},
    {"settings.note.link_schemes", "逗号分隔；白名单外的链接不显示为可点（留空或全是非法词＝什么都不可以点）"},

    // ---- 链接打开确认（`SPEC.FEAT.INTERACT.05`，`codespec/UI_URL.draft.md` 判据 4／稿 D11①）----
    // 标题、正文首行与来源两行**逐字沿用**已评审稿面（`codespec/UI_COMMANDS.draft.svg` 屏 4）：判据 4
    // 要的就是「那张稿面落到控件」，措辞自创即破那条对齐。
    // 后两行是**回显当前生效值**而不是稿面那句固定措辞——稿画在缺省配置下，把「（默认）」二字印在
    // 一档非默认的档位旁边就是假话；而「信任级别两档分开配置」那半句留在白名单行，因为它与配置值无关。
    // 三档名后面括注落盘名（`never` / `ask_once` / `ask_always`）：面板的下拉直接吃落盘原文
    //（`terminal.clipboard_read` 同例，见上面那条旁注的理由），只给中文名就没法把框里那句话和面板里
    // 那一档对上——这与旁注「照上屏候选原文写」是同一条纪律的两处应用。
    {"links.confirm.title", "打开链接？"},
    {"links.confirm.uri", "{0}"},
    {"links.confirm.source.osc8", "来源：OSC 8 显式超链接"},
    {"links.confirm.source.heuristic", "来源：纯文本启发式识别（非 OSC 8 显式超链接）"},
    {"links.confirm.whitelist", "协议白名单：{0}；信任级别两档分开配置"},
    {"links.confirm.policy", "确认策略：{0}"},
    {"links.confirm.cancel", "取消"},
    {"links.confirm.open_once", "仅本次打开"},
    {"links.confirm.remember_and_open", "记住该域并打开"},
    {"links.policy.never", "不询问、直接打开（never）"},
    {"links.policy.ask_once", "首次确认并记住同域（ask_once）"},
    {"links.policy.ask_always", "每次都问（ask_always）"},

    // ---- 命令面板（`SPEC.FEAT.WS.07`，判据文 `codespec/UI_WORKSPACE.draft.md` §4）----
    // 占位符与空态两条的 **key 由框架命名**（`CommandPalette::AURORA_DEFAULT_*_KEY`，G38 / G39 回货形
    // 态），本件只登记取值，故这两串 key 不得自创——key 一漂移，面板就静默回落英文（裁决 **7.92**②）。
    {"command_palette.title", "命令面板"},
    {"command_palette.placeholder", "输入命令..."},
    {"command_palette.no_results", "没有匹配的命令"},
    {"command_palette.action.open", "打开命令面板"},

    // ---- 调试面板（`SPEC.NF.RELI.01`：解析降级 / 非法字节 / 发送侧不可表示 / 背压水位四族计数器的上屏形态）----
    // 模板里的 {n} 一律是**取值**（计数值与行标题的已解析串），措辞不含 ASCII 诊断通道。
    {"diagnostics.title", "诊断"},
    {"diagnostics.action.open", "打开诊断面板"},
    {"diagnostics.empty", "没有活动的会话"},
    {"diagnostics.session", "标签 {0} · 分屏 {1}"},
    {"diagnostics.parse", "未知序列 {0}，被打断 {1}"},
    {"diagnostics.decode", "非法字节 {0}，码点 {1}"},
    {"diagnostics.encode", "发送侧不可表示 {0}"},
    {"diagnostics.queue", "水位 {0}（峰值 {1}），过载 {2}，合并 {3}，让出 {4}"},

    // ---- 外观域 ----
    {"appearance.theme", "主题"},
    // 语言档那一行的标签。英文档的同名条目排在批 3 段 4（`appearance.*` 那一族）——本行所属的域还没轮到，
    // 于是它在英文档暂时是 F13 的空白形态；「两档同 key 集」那条结构式证人（判据 ②）随四段落齐立项
    // （`§4.1` 第 3 条）。
    {"appearance.language", "界面语言"},
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
    {"terminal.unrepresentable", "不可表示字符处置"},
    {"terminal.paste_newlines", "粘贴换行处理"},
    {"terminal.right_click", "右键行为"},
    // 该行标签（`SPEC.FEAT.CONN.12`，裁决 D1① 把键放在 `terminal` 域而标识属连接域）。措辞按 D10①
    // 收窄到「读取」：写方向是 `TERM.07` 已落的「默认允许」，不归这道闸管，写成「剪贴板授权」就是
    // 让一行设置涵盖它没做的事。三档候选不在这里——面板的下拉直接吃落盘名（`aurora::Dropdown`
    // 收 `choices` 原样显示，同族的 `right_click` / `paste_newlines` 一样没有候选词条）。
    {"terminal.clipboard_read", "远端读取剪贴板"},
    // 读方向的授权一问（`SPEC.FEAT.CONN.12`）：这两句不是设置项标签，而是视口那枚模态框的标题与
    // 正文，走同一张表是为了让「上屏文案只在词条表一处」这条纪律不被破例。正文那一句「拒绝会回送
    // 一条空响应」不是修辞：裁决 D5 要框里说清「不给会怎样」，而 `Dialog` 没有关闭叉、遮罩不答，
    // 用户唯一能读到的说明就只剩这一句。
    {"terminal.clipboard_read.title", "远端请求读取剪贴板"},
    {"terminal.clipboard_read.body",
     "远端程序想读取本地剪贴板的内容并发回会话（选择位 {0}）。允许吗？拒绝会回送一条空响应，远端不会挂起。"},
    {"terminal.copy_on_select", "选中即复制"},
    {"terminal.trim_pasted_trailing_space", "复制时剥行尾空白"},
    {"terminal.smart_line_join", "合并续行"},
    {"terminal.strip_tmux_border_chars", "剥 tmux 细线制表符"},
    {"terminal.word_delimiters", "双击选词断点集"},
    // 打开链接前的三行（`SPEC.FEAT.INTERACT.05`，裁决 D8①）：两枚确认档的行名要**点明是哪一来源**，
    // 因为「信任级别可分别配置」这句需求的全部意义就在两来源各走各的闸；写成一枚「链接安全」反而
    // 把那件事藏起来。白名单那行的行名不叫「协议」而叫「允许打开的协议」：面板里没有第二处解释
    // 「这一串管什么」，而它管的是识别的入口，不是发送的编码。
    {"terminal.link_osc8", "显式超链接（OSC 8）打开前"},
    {"terminal.link_heuristic", "纯文本识别的链接打开前"},
    {"terminal.link_schemes", "允许打开的协议"},
    // 分隔线那行点明「悬停时」与「命令块」两件事：面板里没有第二处解释这条线什么时候出现，
    // 写成「显示分隔线」会让用户以为它常驻（裁决 7.123 D9①）。
    {"terminal.command_block_separator", "悬停命令块时显示分隔线"},

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
    {"connection.serial.flow", "流控"},
    {"connection.serial.line_ending", "行尾符"},
    {"connection.serial.encoding", "串口编码"},
    {"connection.session_logging", "会话日志"},
    {"connection.session_log_dir", "日志目录"},
    {"connection.session_log_pattern", "追加敏感正则"},

    // ---- 快捷键域 ----
    // 表头四列与「未绑定」那五串不在此处（它们在上方『快捷键只读表』段，`SPEC.FEAT.PREF.05` 稿 F8／D10）。
    {"shortcuts.overrides", "键位覆盖表"},
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

/// @brief 英文档（`en`）的 key → 模板：批 3 按域四段推完，**段 1 落 `connections.*` 与 `keys.*` 146 条、
/// 段 2 落 `settings.*` 77 条、段 3 落终端与会话侧 93 条、段 4 落连接域与其余 150 条（`connection.*`／
/// `serial.*`／`sftp.*`／`tunnel.*`／`shortcuts.*`／`appearance.*`），合计 466 条，与中文表逐 key 相等**。
///
/// CJK-LITERAL: locale-output - 本表与中文表同为上屏文案的真值源，换成转写即失去并排对照的价值。表内
/// 非 ASCII 的字面量只有两类共九枚：三枚字形（`settings.chain.*`，与中文表同字）与六枚 `" · "` 分隔符
/// （沿 `serial_format.cpp` 与 `settings_panel.cpp` 那两处同类别已标的既定视觉形态）。
///
/// 位置刻意紧跟中文表（该稿 D4），并排校对是这一档存在的唯一理由——466 条 key（批 1 收敛后的 464 条加
/// 批 2 两枚）分在两个文件里，「两档同 key 集」那条判据就要跨文件聚合。段内条目序与中文表逐条一致，
/// 中文表里解释「为什么是这个措辞」的那几条注也随条目一起搬过来：改一条时两档同屏可见。
///
/// 两档文本逐字相同的 13 枚不是漏译：`CR`／`CRLF`／`LF`／`SSH`／`Telnet`／`Ed25519`／`RSA`／`SOCKS5 -D`
/// 本来就是英文缩写，`{0}` 与 `{0}+` 是纯读数，另三枚是上面那三枚字形。「两档同 key 集」那条判据（该稿
/// §5 第 2 条）断的是 key 集与「每档都有非空模板」，不比文本，故这 13 枚在判据上都有读数。
///
/// 装填腿只装选定那一档（该稿 D4 的落地期改形，理由见 `§4.1` 第 2 条），于是本档此前「没译到的 key 上屏
/// 为空白」——该稿 F13 在批 3 中途的已知代价——随段 4 闭合：本档没有空串占位条目。自此「新增界面串必须
/// 先加两档才能上屏」（7.25⑬）由 `utest_i18n_tiers.the_two_tiers_share_the_same_key_space` 常驻把守；
/// 判据 ② 的等式形态在四段中途必红（红在还没译到的那些 key 上），故按该稿 `§4.1` 第 3 条排到今天。
const Tier kStringsEn{
    // ---- 连接管理器侧栏（UI_CONNECTIONS.draft.md；词条与 settings.* 同格角色）----
    {"connections.title", "Connections"},
    {"connections.action.close", "Close"},
    {"connections.action.connect", "Connect"},
    {"connections.action.confirm", "OK"},
    {"connections.action.cancel", "Cancel"},
    {"connections.action.save", "Save"},
    {"connections.action.edit", "Edit"},
    {"connections.action.new", "New"},
    {"connections.action.import", "Import ~/.ssh/config"},
    {"connections.quick.placeholder", "Type host[:port] to connect"},
    {"connections.search.placeholder", "Search profiles"},
    {"connections.view.all", "All"},
    {"connections.view.favorites", "Favorites"},
    {"connections.view.groups", "Groups"},
    {"connections.recent.title", "Recent connections"},
    {"connections.notice.imported", "Imported {0} profiles"},
    {"connections.type.local", "Local terminal"},
    {"connections.type.ssh", "SSH"},
    // 向导第四型的类型卡（`SPEC.FEAT.CONN.06`，裁决 7.112 D2①）：本棒的新词条**只有这一枚**
    // ——字段名复用 host/port 两行、掉线留痕复用既有措辞，故 Telnet 没有自己的词条族。
    // 「串口终端」那枚在下面的串口块里，随该块的参数档一族同处登记。
    {"connections.type.telnet", "Telnet"},
    {"connections.field.name", "Name"},
    {"connections.field.host", "Host"},
    {"connections.field.port", "Port"},
    {"connections.field.user", "User"},
    {"connections.field.auth", "Auth method"},
    {"connections.field.identity", "Private key path"},
    {"connections.field.command", "Command"},
    {"connections.field.workdir", "Working directory"},
    {"connections.auth.agent", "Agent key"},
    {"connections.auth.password", "Password"},
    {"connections.auth.privatekey", "Private key"},
    {"connections.auth.keyboard-interactive", "Interactive"},
    {"connections.policy.accept_new", "Accept on first use"},
    {"connections.policy.yes", "Strict check"},
    {"connections.policy.no", "No check"},
    {"connections.policy.ask", "Ask"},
    {"connections.credential.password_title", "Enter the password for {0}"},
    {"connections.credential.passphrase_title", "Enter the private key passphrase for {0}"},
    {"connections.credential.interactive_title", "Answer the authentication prompts for {0}"},
    {"connections.wizard.title_new", "New connection"},
    {"connections.wizard.title_edit", "Edit connection"},
    {"connections.wizard.issue_empty_name", "Name cannot be empty"},
    // 措辞不带型名：SSH 与 Telnet 共用这一档问题码（裁决 7.112 D2① 的「零新增问题码」），
    // 写成「SSH host cannot be empty」会在 Telnet 字段集上把用户指向另一个型。
    {"connections.wizard.issue_empty_host", "Host cannot be empty"},
    {"connections.wizard.issue_port", "Port must be 1..65535"},
    {"connections.wizard.issue_auth", "The auth method is invalid"},
    // ---- 串口向导与热改波特率（UI_SERIAL.draft.md，裁决 7.110；`connections.serial.*` 的
    //      参数档与可达性归因由 `serial_format` 唯一产出，装配与向导不 switch 第二套措辞）----
    {"connections.type.serial", "Serial terminal"},
    {"connections.field.device", "Device"},
    {"connections.field.baud", "Baud rate"},
    {"connections.field.data_bits", "Data bits"},
    {"connections.field.stop_bits", "Stop bits"},
    {"connections.field.parity", "Parity"},
    {"connections.field.flow", "Flow control"},
    {"connections.field.line_ending", "Sent line ending"},
    {"connections.field.encoding", "Encoding"},
    {"connections.action.refresh_devices", "Refresh"},
    {"connections.action.custom_path", "Custom..."},
    {"connections.action.pick_from_list", "Pick from list"},
    {"connections.device.placeholder", "Select a device"},
    {"connections.serial.parity.none", "None"},
    {"connections.serial.parity.even", "Even"},
    {"connections.serial.parity.odd", "Odd"},
    {"connections.serial.flow.none", "None"},
    {"connections.serial.flow.hardware", "Hardware RTS/CTS"},
    {"connections.serial.flow.software", "Software XON/XOFF"},
    {"connections.serial.line_ending.lf", "LF"},
    {"connections.serial.line_ending.cr", "CR"},
    {"connections.serial.line_ending.crlf", "CRLF"},
    {"connections.serial.access.denied", "No permission"},
    {"connections.serial.access.busy", "In use"},
    {"connections.serial.access.unavailable", "Cannot open right now"},
    {"connections.wizard.issue_device", "Device cannot be empty"},
    {"connections.wizard.issue_baud", "Baud rate must be one of the listed values"},
    {"connections.wizard.issue_data_bits", "Data bits must be 5..8"},
    {"connections.wizard.issue_stop_bits", "Stop bits must be 1..2"},
    // `serial.set_baud.*` 那五枚与下面的 SSH 隧道面板一族按该稿 §4 归批 3 段 4（`serial.*`／`tunnel.*`），
    // 本段不占位：占位空串会让判据 ② 的「逐 key 两档都非空」提前以另一种形态失败，读不出还差多少。
    // ---- SSH 密钥管理面板（UI_KEYS.draft.md，裁决 7.106/7.107/7.108；全部 key 由
    //      keys_format 唯一产出，面板不 switch 出第二套措辞）----
    {"keys.title", "SSH keys"},
    {"keys.empty", "No keys yet: choose Generate key to create one, or add a directory to scan"},
    {"keys.header.count", "{0} keys"},
    {"keys.action.add_dir", "Add directory"},
    {"keys.action.generate", "Generate key"},
    {"keys.action.close", "Close"},
    {"keys.action.copy", "Copy"},
    {"keys.action.push", "Push"},
    {"keys.action.export", "Export public key"},
    {"keys.action.delete", "Delete"},
    {"keys.op.scan", "Scanning"},
    {"keys.op.generate", "Generating"},
    {"keys.op.export", "Exporting"},
    {"keys.op.delete", "Deleting"},
    {"keys.op.push", "Pushing"},
    {"keys.row.has_passphrase", "Passphrase set"},
    {"keys.row.no_passphrase", "No passphrase"},
    {"keys.row.public_missing", "Public key missing"},
    {"keys.issue.name.empty", "File name cannot be empty"},
    {"keys.issue.name.too_long", "File name is too long"},
    {"keys.issue.name.hidden", "A name starting with a dot becomes a hidden file"},
    {"keys.issue.name.has_separator", "File name cannot contain a path separator"},
    {"keys.issue.name.control_char", "File name contains invisible characters"},
    {"keys.issue.name.reserved", "This file name is reserved by the system"},
    {"keys.issue.comment.too_long", "Comment is too long"},
    {"keys.issue.comment.control_char", "Comment contains invisible characters"},
    {"keys.dir.unreachable", "Directory unreachable: {0}"},
    {"keys.generate.title", "Generate key"},
    {"keys.generate.field.bits", "Bits"},
    {"keys.generate.field.name", "File name"},
    {"keys.generate.field.comment", "Comment"},
    {"keys.generate.field.directory", "Target directory"},
    {"keys.generate.field.passphrase", "Passphrase (optional)"},
    {"keys.generate.field.passphrase_again", "Repeat the passphrase"},
    {"keys.generate.browse", "Browse"},
    {"keys.generate.action.save", "Generate"},
    {"keys.generate.action.cancel", "Cancel"},
    {"keys.generate.invalid_request", "The request does not hold: check the directory and file name"},
    {"keys.generate.name_taken", "A file with that name already exists"},
    {"keys.generate.directory_unusable", "The target directory is unusable"},
    {"keys.generate.failed", "Key generation failed"},
    {"keys.generate.issue_mismatch", "The two passphrases do not match"},
    {"keys.kind.ed25519", "Ed25519"},
    {"keys.kind.rsa", "RSA"},
    {"keys.export.already_exists", "The public key file already exists; nothing was overwritten"},
    {"keys.export.needs_passphrase", "This private key is passphrase protected; unlocking is required to export"},
    {"keys.export.failed", "Public key export failed"},
    {"keys.delete.title", "Delete key"},
    {"keys.delete.warn", "The private key and its paired public key will be deleted. This cannot be undone."},
    {"keys.delete.action", "Delete"},
    {"keys.delete.action.cancel", "Cancel"},
    {"keys.delete.refused_symlink", "Symbolic links are never deleted: the link and its target stay untouched"},
    {"keys.delete.not_found", "The file is no longer in its original place"},
    {"keys.delete.failed", "Delete failed"},
    {"keys.push.title", "Push public key"},
    {"keys.push.field.profile", "Target profile"},
    {"keys.push.no_profile", "(no SSH profile)"},
    {"keys.push.action", "Push"},
    {"keys.push.action.close", "Close"},
    {"keys.push.already_authorized", "This public key is already in the authorized keys file"},
    {"keys.push.appended", "The public key was appended to the server's authorized keys file"},
    {"keys.push.stage.dial", "Connect"},
    {"keys.push.stage.auth", "Authenticate"},
    {"keys.push.stage.exec", "Execute"},
    {"keys.push.stage.done", "Done"},
    {"keys.push.fail.session_unallocated", "No session could be established"},
    {"keys.push.fail.network", "Network error: the server is unreachable"},
    {"keys.push.fail.host_key", "Host key verification failed"},
    {"keys.push.fail.auth", "Authentication was rejected"},
    {"keys.push.fail.exec_refused", "The server refused to run the authorized keys command"},
    {"keys.push.fail.write_rejected", "Writing the authorized keys file did not succeed"},
    {"keys.push.fail.invalid_line", "This public key does not form a valid line; the push was not started"},
    {"keys.notice.copied", "Copied to the clipboard"},

    // ---- 面板骨架（批 3 段 2：`settings.*` 全族 77 条，段内序与中文表逐条一致）----
    // 副标题说的是本件默认的提交语义，行尾与组顶角标说的是被标的那一行/那一组（B3-a 同一口径），
    // 故「立即生效」与下面 `settings.badge.*` 的三档延后并不互相推翻。
    {"settings.title", "Settings"},
    {"settings.subtitle", "Changes take effect immediately"},
    {"settings.close", "Close"},
    {"settings.action.open", "Open Settings"},
    {"settings.page.appearance", "Appearance"},
    {"settings.page.terminal", "Terminal"},
    {"settings.page.connection", "Connection"},
    {"settings.page.shortcuts", "Shortcuts"},
    {"settings.action.unset", "Not set"},
    {"settings.action.theme_default", "Restore theme default"},
    {"settings.badge.customized", "Customized"},
    {"settings.badge.deferred", "Deferred"},
    {"settings.badge.next_session", "Takes effect next session"},
    {"settings.badge.next_launch", "Takes effect after restart"},

    // ---- 组顶说明：每条都要点名「哪个消费方还没开工」，否则「只落盘」就成了没来由的话 ----
    {"settings.note.status_bar",
     "The entry landed before its consumer: hiding the status bar is not built yet, so this group only reaches disk"},
    {"settings.note.ssh",
     "The entry landed before its consumer: the SSH connection family is not built yet, "
     "so this group only reaches disk"},
    {"settings.note.serial",
     "The entry landed before its consumer: the serial connection family is not built yet, "
     "so this group only reaches disk"},
    {"settings.note.shortcuts",
     "The entry landed before its consumer: key rebinding is not built yet, "
     "so bindings in the override table do nothing"},

    // ---- 回退链区段（A5-a）----
    // 三枚按钮是**字形**而不是词，两档同字（`SPEC.FEAT.PREF.05` 稿 m1 那条等价变异点名的正是这类条目）；
    // 上限的数字仍经 `{0}` 交出，不写进模板（裁决 7.62，理由同中文表那一条注）。
    {"settings.chain.up", "↑"},
    {"settings.chain.down", "↓"},
    {"settings.chain.remove", "✕"},
    {"settings.chain.filter", "Type a family name to add it"},
    {"settings.chain.empty", "The fallback chain is empty: only the framework's global default chain runs"},
    {"settings.chain.full", "The limit of {0} is reached: the add field is closed"},
    {"settings.chain.truncated", "The chain is longer than the limit of {0}: the extra entries are not drawn"},
    {"settings.chain.no_match", "No family name matches"},

    // ---- 字体族区段（A4-b：一句话覆盖「目录里没有」与「度量非等宽」两档，两个参数是取值）----
    {"settings.font.fallback", "The configured {0} is unavailable: {1} is used instead"},

    // ---- 快捷键只读表（D1-a 的列名 / D2-a 的标注两档 / 未绑定那一格）----
    // 这五串同样只在此处注册一次；「重复」那枚的位置参数是**另一行的动作名**（取值），
    // 与 `settings.font.fallback` 同档，故两侧都带引号形态。
    {"settings.shortcut.column.title", "Action"},
    {"settings.shortcut.column.category", "Group"},
    {"settings.shortcut.column.binding", "Current binding"},
    {"settings.shortcut.column.note", "Note"},
    {"settings.shortcut.unbound", "Unbound"},
    {"settings.shortcut.conflict_with", "Duplicates \"{0}\""},
    {"settings.shortcut.conflict_workspace", "Conflicts with a split-screen binding"},

    // ---- 配置韧性动作区（`SPEC.FEAT.PREF.07` 的 M2 腿）----
    // 说明里不写份数（真值源是 `config::kSnapshotRetention`，本件不 include `config`）；
    // 文件名走位置参数，三条失败留痕不含存储侧交回的那句 ASCII 诊断（裁决 7.76⑤，理由同中文表）。
    {"settings.config.export", "Export settings"},
    {"settings.config.import", "Import settings"},
    {"settings.config.snapshots", "Settings snapshots"},
    {"settings.config.back", "Back"},
    {"settings.snapshot.note",
     "Each snapshot is taken before a save, so rolling back returns this file to that moment; panel edits that were "
     "never saved are lost with them"},
    {"settings.snapshot.empty", "No snapshots yet: nothing has been written to disk"},
    {"settings.snapshot.rollback", "Roll back"},
    {"settings.config.exported", "Exported to {0}"},
    {"settings.config.imported", "Imported {0}"},
    {"settings.config.rolled_back", "Rolled back to {0}"},
    {"settings.config.export_failed", "Export failed: the target file could not be written"},
    {"settings.config.import_failed",
     "Import failed: that file is not a usable configuration, current settings unchanged"},
    {"settings.config.rollback_failed", "Rollback failed: current settings unchanged"},

    // ---- 启动降级提示（`SPEC.FEAT.PREF.07`，判据文 §7 的 S13①）----
    // 标题按 `LoadOutcome` 两态各一条；版本号与备份路径都是**取值**故走位置参数。
    {"settings.startup.corrupt.title", "The configuration file is corrupt"},
    {"settings.startup.version.title", "The configuration file is newer than this program"},
    {"settings.startup.corrupt.body",
     "This configuration file could not be read, so the program started on defaults."},
    {"settings.startup.version.body",
     "This configuration file was written by a newer version (file {0}, this program supports {1}), so the program "
     "started on defaults."},
    {"settings.startup.backup", "The corrupt file was backed up to: {0}"},
    {"settings.startup.writes_refused",
     "The corrupt file could not be backed up: this session will not write the configuration file, "
     "the original stays as it is"},
    {"settings.startup.rejected", "These settings were outside their usable range and fell back to their defaults:"},
    {"settings.startup.ack", "Got it"},

    // ---- 三条延后落地的旁注（会话日志 / OSC 52 读方向 / 打开链接）----
    // 读方向那一条要把三档各自的后果一次讲完，并把候选原文 `deny` / `ask` 照上屏形态写进句子：
    // 面板的下拉直接把落盘名交给 `aurora::Dropdown`，旁注换另一套说法就让用户在框里找不到那一档。
    {"settings.note.session_log",
     "Capability gate: even with this on, each session must still start its log explicitly. Old files in the log "
     "directory are neither cleaned nor rotated in this phase."},
    {"settings.note.clipboard_read",
     "What each tier does: deny and over-limit both reply with a single empty response, no prompt and nothing to "
     "find on screen; changing the tier takes effect immediately for every live tab; ask asks once per read request "
     "(a session can hold at most 8 pending reads, any further request is answered as deny)."},
    {"settings.note.link_confirm",
     "The two keys each cover one source: osc8 is a target the program declared explicitly, heuristic is a string "
     "guessed from its shape. ask_once remembers scheme + host + port, so a second link in the same domain is not "
     "asked about again, but a different port is."},
    {"settings.note.link_schemes",
     "Comma separated; links outside the allowlist are not drawn as clickable (an empty or all-invalid list means "
     "nothing is clickable)"},

    // ---- 快捷键域（表头四列与「未绑定」在上方只读表段，此处只有编辑浮层那六串）----
    {"settings.shortcut.edit_title", "Edit key binding"},
    {"settings.shortcut.conflict_warning", "Conflicts with these commands"},
    {"settings.action.edit", "Edit"},
    {"settings.action.restore_defaults", "Restore defaults"},
    {"settings.action.confirm", "OK"},
    {"settings.action.cancel", "Cancel"},

    // ---- 提交未通过的原因（`ui::CommitIssue` 的十二个非 `None` 值；措辞只说这一格能做什么）----
    {"settings.issue.unknown_key", "That key is not in this version's settings table"},
    {"settings.issue.not_loaded", "That key is not loaded into this panel"},
    {"settings.issue.domain_mismatch", "That value's type does not match this setting"},
    {"settings.issue.text_not_accepted", "This setting does not take typed text"},
    {"settings.issue.malformed_number", "Enter a number"},
    {"settings.issue.not_integral", "This setting only takes whole numbers"},
    {"settings.issue.out_of_range", "Outside the allowed range"},
    {"settings.issue.not_a_choice", "Choose one of the listed values"},
    {"settings.issue.malformed_color", "A color must be the 7-character #RRGGBB form"},
    {"settings.issue.unset_not_allowed", "This setting must have a color"},
    {"settings.issue.slot_out_of_range", "The palette slot is outside 0 to 15"},
    {"settings.issue.table_size_wrong", "The palette must have exactly 16 slots"},

    // ---- 终端内搜索浮层（批 3 段 3 起：终端与会话侧一族 93 条，段内序与中文表逐条一致）----
    // chip 上的 `Aa` / `.*` 与按钮的 `↑` / `↓` 是符号不是文案，不进表；符号档的**无障碍标签**进表
    //（同中文表那条注，A1-d 的宽度算式与 `set_accessibility_label` 一处兑现）。
    {"search.action.open", "Open Search"},
    {"search.placeholder", "Search terminal content"},
    {"search.count_none", "No matches"},
    {"search.count_cap", "{0}+"},
    {"search.count_invalid", "Invalid expression"},
    {"search.pending_enter", "Press Enter to apply"},
    {"search.reopen_hint", "Press Enter to search"},
    {"search.toggle_case", "Match case"},
    {"search.toggle_regex", "Regular expression"},
    {"search.prev", "Previous"},
    {"search.next", "Next"},
    {"search.close", "Close"},

    // ---- 工作区操作与广播输入（WS.06 / WS.03 / WS.09）----
    // 标题的 {0} 是成员名、{1} 是去重后的会话数，两者都是取值故走位置参数；「点击此处退出」是 D8
    // 的落点（`Esc` 留给终端，横幅是退出整组的唯一入口）。
    {"workspace.action.toggle_fullscreen", "Toggle fullscreen"},
    {"workspace.action.new_window", "New window"},
    {"workspace.broadcast.title", "Input broadcast is on: {0}, {1} sessions"},
    {"workspace.broadcast.hint", "Sent to every pane in the group as you type; click here to exit"},
    {"workspace.broadcast.exit", "Exit broadcast"},
    {"workspace.broadcast.badge", "Broadcast B"},
    {"workspace.broadcast.pane", "Pane {0}"},
    {"workspace.broadcast.unnamed", "Unnamed session"},

    // ---- 撤销关闭标签（WS.10 的命令标题）----
    {"tab.action.undo_close", "Reopen the last closed tab"},

    // ---- dead-session 一键重启与自动重连浮层（WS.05）----
    // 终态三档只以快照里的**档位**分档，关闭原因本身不外传（D3），故「不自动重试」那一档必须同时
    // 罩得住认证失败与主机密钥拒绝两种原因——英文也保持这一句不带原因。
    {"session.restart.title", "Restart session"},
    {"session.restart.hint", "The session has exited"},
    {"session.restart.button", "Restart"},
    {"session.reconnect.line", "Connection dropped · automatic reconnect {0} of {1}"},
    {"session.reconnect.countdown", "Retrying automatically in {0} s"},
    {"session.reconnect.retry_now", "Retry now"},
    {"session.reconnect.stop", "Stop reconnecting"},
    {"session.reconnect.exhausted", "Still failing after {0} tries · stopped"},
    {"session.reconnect.not_reconnectable", "Authentication refused · no automatic retry"},
    {"session.reconnect.user_stopped", "Automatic reconnect stopped"},
    {"session.restart.by_profile", "Restart (from profile)"},
    {"session.restart.retry_credential", "Retry with different credentials"},

    // ---- 发送侧「不可表示字符」提示卡（TERM.09 的提示腿，裁决 7.104 D4①）----
    // 第二行只指路径、不报当前处置档名：那一枚下拉显示的就是 ASCII 档名本身（replace / drop /
    // pass_through_utf8），报第二种说法会对不上。路径三段与真标签同源（见 `settings.page.terminal`
    // 与下面的 `terminal.unrepresentable`）。
    {"terminal.notice.unrepresentable.line", "{0} cannot send {1} characters in this block"},
    {"terminal.notice.unrepresentable.action",
     "Sent under the current policy · change it in Settings > Terminal > Unrepresentable character policy"},

    // ---- 右键菜单与「会话日志」一族（INTERACT.03 三条目 + CONN.11）----
    // 这四条是既有菜单文案**换来源**而不是换措辞；日志一族六句同理，命令标题与菜单条目各一条。
    {"menu.item.copy", "Copy"},
    {"menu.item.copy_command_output", "Copy the output of the last command"},
    {"menu.item.paste", "Paste"},
    {"menu.item.open_link", "Open link"},
    {"session.log.menu.start", "Start session log"},
    {"session.log.menu.stop", "Stop session log"},
    {"session.log.command.start", "Session log: start (current tab)"},
    {"session.log.command.stop", "Session log: stop (current tab)"},
    {"session.log.confirm.title", "Start session log"},
    {"session.log.confirm.sensitive", "The log may contain passwords, tokens or other sensitive information."},
    {"session.log.confirm.path", "Will be written to: {0}"},
    {"session.log.alert.bad_pattern", "The added regex does not compile; the session log did not start: {0}"},
    {"session.log.alert.cannot_open", "The log file could not be created; the session log did not start: {0}"},
    {"session.log.alert.write_failed", "The log write failed; recording stopped"},

    // ---- 打开链接的确认框与三档候选（INTERACT.05，判据 4／稿 D11①）----
    // 标题、正文首行与来源两行逐字沿用已评审稿面；后两行回显的是**当前生效值**。三档候选括注落盘名
    //（`never` / `ask_once` / `ask_always`）：面板的下拉直接吃落盘原文，只给另一种说法就对不上。
    {"links.confirm.title", "Open this link?"},
    {"links.confirm.uri", "{0}"},
    {"links.confirm.source.osc8", "Source: explicit OSC 8 hyperlink"},
    {"links.confirm.source.heuristic", "Source: heuristic text detection (not an explicit OSC 8 hyperlink)"},
    {"links.confirm.whitelist", "Scheme allowlist: {0}; the two trust levels are configured separately"},
    {"links.confirm.policy", "Confirmation policy: {0}"},
    {"links.confirm.cancel", "Cancel"},
    {"links.confirm.open_once", "Open this one only"},
    {"links.confirm.remember_and_open", "Remember this domain and open"},
    {"links.policy.never", "Open without asking (never)"},
    {"links.policy.ask_once", "Ask the first time and remember the domain (ask_once)"},
    {"links.policy.ask_always", "Ask every time (ask_always)"},

    // ---- 命令面板（WS.07）----
    // 占位符与空态两条的 key 由框架命名（`CommandPalette::AURORA_DEFAULT_*_KEY`），本件只登记取值，
    // key 一漂移面板就静默回落英文（裁决 7.92②），故这两枚 key 不得自创。
    {"command_palette.title", "Command palette"},
    {"command_palette.placeholder", "Type a command..."},
    {"command_palette.no_results", "No matching commands"},
    {"command_palette.action.open", "Open Command Palette"},

    // ---- 调试面板（`SPEC.NF.RELI.01`）----
    // 模板里的 {n} 一律是取值（计数值与行标题的已解析串），措辞不含 ASCII 诊断通道。
    {"diagnostics.title", "Diagnostics"},
    {"diagnostics.action.open", "Open diagnostics"},
    {"diagnostics.empty", "No active session"},
    {"diagnostics.session", "Tab {0} · pane {1}"},
    {"diagnostics.parse", "Unknown sequences {0}, interrupted {1}"},
    {"diagnostics.decode", "Illegal bytes {0}, code points {1}"},
    {"diagnostics.encode", "Unrepresentable on send {0}"},
    {"diagnostics.queue", "Watermark {0} (peak {1}), overloads {2}, coalesced {3}, yields {4}"},

    // ---- 终端域的设置行与那一族浮层 ----
    // `terminal.clipboard_read` 的措辞按 D10① 收窄到「读取」：写方向不归这道闸管；三档候选不在本表
    //（面板的下拉直接显示落盘名，同族的 `right_click` / `paste_newlines` 一样没有候选词条）。
    // 打开链接前的两行要点明**是哪一来源**（D8①），白名单那行叫「允许打开的协议」而不是「协议」。
    {"terminal.scrollback_limit", "Scrollback lines"},
    {"terminal.ambiguous_width", "Ambiguous width handling"},
    {"terminal.long_line", "Overlong line handling"},
    {"terminal.bell", "Bell"},
    {"terminal.encoding", "Session encoding"},
    {"terminal.unrepresentable", "Unrepresentable character policy"},
    {"terminal.paste_newlines", "Newline handling on paste"},
    {"terminal.right_click", "Right-click behavior"},
    {"terminal.clipboard_read", "Remote clipboard read"},
    {"terminal.clipboard_read.title", "Remote requests a clipboard read"},
    {"terminal.clipboard_read.body",
     "The remote program wants to read the local clipboard and send the contents back to the session "
     "(selection {0}). Allow this? A denial replies with a single empty response, so the remote does not hang."},
    {"terminal.copy_on_select", "Copy on select"},
    {"terminal.trim_pasted_trailing_space", "Trim trailing spaces on copy"},
    {"terminal.smart_line_join", "Join wrapped lines"},
    {"terminal.strip_tmux_border_chars", "Strip tmux border glyphs"},
    {"terminal.word_delimiters", "Double-click word boundaries"},
    {"terminal.link_osc8", "Before opening OSC 8 hyperlinks"},
    {"terminal.link_heuristic", "Before opening detected links"},
    {"terminal.link_schemes", "Schemes allowed to open"},
    {"terminal.command_block_separator", "Show a divider while hovering a command block"},

    // ---- 热改波特率（批 3 段 4 收尾这一笔 150 条：`serial.set_baud.*`／`tunnel.*`／`appearance.*`／
    //      `connection.*`／`shortcuts.*`／`sftp.*`，段内序与中文表逐条一致）----
    {"serial.set_baud.title", "Change serial baud rate"},
    {"serial.set_baud.current", "Current: {0}"},
    {"serial.set_baud.applied", "Switched to {0}"},
    {"serial.set_baud.rejected", "Refused by the device: {0}"},
    {"serial.set_baud.not_serial", "The current session is not a serial port"},

    // ---- SSH 隧道面板（UI_TUNNEL.draft.md，裁决 7.97）----
    // state / error / listen 三族由 `tunnel_format` 唯一产出，面板不 switch 出第二套措辞。
    // `tunnel.listen.server_picked` 与 `.server_chosen` 不是一句话的两种写法：前者是「-R 配 0 端口、
    // 运行时还没回报择定值」那一档的注记，后者是 `bound_port>0`（或 Active 等帧）之后已择定的回显，
    // 两枚各由 `tunnel_endpoint_note_key` 的一个分支产出（`src/ui/tunnel_format.cpp`）。
    {"tunnel.title", "Tunnels"},
    {"tunnel.empty", "No tunnels yet: use New tunnel to add one"},
    {"tunnel.header.running", "{0} running"},
    {"tunnel.action.close", "Close"},
    {"tunnel.action.new", "New tunnel"},
    {"tunnel.action.start", "Start"},
    {"tunnel.action.stop", "Stop"},
    {"tunnel.action.edit", "Edit"},
    {"tunnel.action.delete", "Delete"},
    {"tunnel.state.stopped", "Stopped"},
    {"tunnel.state.dialing", "Dialing"},
    {"tunnel.state.active", "Ready"},
    {"tunnel.state.backoff", "Backing off"},
    {"tunnel.state.failed", "Failed"},
    {"tunnel.error.dial_failed", "Connect or authenticate failed"},
    {"tunnel.error.bind_failed", "Listen port could not be opened"},
    {"tunnel.error.remote_refused", "Server refused the remote listen"},
    {"tunnel.error.channel_lost", "Session dropped"},
    {"tunnel.listen.server_picked", "The server picks the port"},
    {"tunnel.listen.server_chosen", "The server chose the port"},
    {"tunnel.row.profile", "Carried by: {0}"},
    {"tunnel.row.retry_wait", "Retry {0} · in {1} s"},
    {"tunnel.row.autostart_skipped", "Autostart skipped: store the credential first"},
    {"tunnel.notice.persist_failed", "The tunnel table could not be saved: this change stays in memory only"},
    {"tunnel.kind.local", "Local -L"},
    {"tunnel.kind.remote", "Remote -R"},
    {"tunnel.kind.dynamic", "SOCKS5 -D"},
    {"tunnel.confirm.title", "Stop and delete?"},
    {"tunnel.confirm.body", "{0} is running; it will be stopped before deleting."},
    {"tunnel.confirm.action", "Stop and delete"},
    {"tunnel.editor.title_new", "New tunnel"},
    {"tunnel.editor.title_edit", "Edit tunnel"},
    {"tunnel.editor.field.name", "Name"},
    {"tunnel.editor.field.listen_addr", "Listen address"},
    {"tunnel.editor.field.listen_port", "Listen port"},
    {"tunnel.editor.field.target_host", "Target host"},
    {"tunnel.editor.field.target_port", "Target port"},
    {"tunnel.editor.field.profile", "Carrying profile"},
    {"tunnel.editor.field.autostart", "Start automatically"},
    {"tunnel.editor.no_profile", "(no SSH profile)"},
    {"tunnel.editor.retry.base", "Base backoff ms"},
    {"tunnel.editor.retry.cap", "Cap ms"},
    {"tunnel.editor.retry.attempts", "Attempts"},
    {"tunnel.editor.retry.hint", "Blank = the old global tier (1s / 30s / unlimited)"},
    {"tunnel.editor.action.save", "Save"},
    {"tunnel.editor.action.cancel", "Cancel"},
    {"tunnel.editor.issue_name", "Name cannot be empty"},
    {"tunnel.editor.issue_profile", "An SSH profile must be selected"},
    {"tunnel.editor.issue_listen_port", "Listen port must be in range (-R allows 0)"},
    {"tunnel.editor.issue_target_host", "Target host cannot be empty"},
    {"tunnel.editor.issue_target_port", "Target port must be 1..65535"},
    {"tunnel.editor.issue_conflict", "Another tunnel claims the same listen point"},

    // ---- 外观域 ----
    // `appearance.language` 是批 2 那枚新键：英文本体到此才闭合「新键须两档齐加」那条纪律，
    // 判据 ② 的常驻案（两档 key 集逐字相等）也随这一段落齐才能立（`§4.1` 第 3 条）。
    {"appearance.theme", "Theme"},
    {"appearance.language", "Interface language"},
    {"appearance.palette.basic", "16-color remap"},
    {"appearance.palette.foreground", "Default foreground color"},
    {"appearance.palette.background", "Default background color"},
    {"appearance.palette.cursor", "Cursor color"},
    {"appearance.palette.selection", "Selection color"},
    {"appearance.palette.bold_is_bright", "Bold uses bright colors"},
    {"appearance.palette.min_contrast_enabled", "Enforce minimum contrast"},
    {"appearance.palette.min_contrast", "Minimum contrast"},
    {"appearance.font_family", "Monospace font"},
    {"appearance.font_size_pt", "Font size"},
    {"appearance.font_line_height", "Line height"},
    {"appearance.font_letter_spacing_dp", "Letter spacing"},
    {"appearance.font_fallback_chain", "CJK fallback chain"},
    {"appearance.viewport_padding_dp", "Viewport padding"},
    {"appearance.cursor_shape", "Cursor shape"},
    {"appearance.cursor_blinking", "Cursor blinking"},
    {"appearance.cursor_blink_period_ms", "Blink period"},
    {"appearance.sidebar_collapsed", "Collapse the sidebar"},
    {"appearance.tab_name_priority", "Tab name priority"},
    {"appearance.status_bar.show_connection", "Connection status"},
    {"appearance.status_bar.show_reconnect", "Reconnect status"},
    {"appearance.status_bar.show_cursor_position", "Cursor position"},
    {"appearance.status_bar.show_encoding", "Session encoding"},
    {"appearance.status_bar.show_grid_size", "Rows and columns"},
    {"appearance.status_bar.show_font_size", "Font size"},
    {"appearance.status_bar.show_theme", "Current theme"},
    {"appearance.status_bar.show_scrollback", "Scrollback lines"},
    {"appearance.status_bar.show_clipboard_policy", "Clipboard policy"},
    {"appearance.status_bar.show_input_latency", "Input latency"},

    // ---- 连接域（设置页那 20 行的行名；键名沿用落盘点号，故 `agent 转发` 这种中英混排在此档
    //      就是纯英文 `Agent forwarding`）----
    {"connection.local_shell", "Default shell"},
    {"connection.startup_directory", "Startup directory"},
    {"connection.ssh.port", "Port"},
    {"connection.ssh.auth_method", "Authentication method"},
    {"connection.ssh.agent_forwarding", "Agent forwarding"},
    {"connection.ssh.keepalive_interval_sec", "Keepalive interval"},
    {"connection.ssh.connect_timeout_sec", "Connect timeout"},
    {"connection.ssh.reconnect_base_delay_ms", "Reconnect base backoff"},
    {"connection.ssh.reconnect_max_delay_ms", "Reconnect backoff cap"},
    {"connection.ssh.reconnect_attempts", "Automatic reconnect attempts"},
    {"connection.serial.baud", "Baud rate"},
    {"connection.serial.data_bits", "Data bits"},
    {"connection.serial.stop_bits", "Stop bits"},
    {"connection.serial.parity", "Parity"},
    {"connection.serial.flow", "Flow control"},
    {"connection.serial.line_ending", "Line ending"},
    {"connection.serial.encoding", "Serial encoding"},
    {"connection.session_logging", "Session log"},
    {"connection.session_log_dir", "Log directory"},
    {"connection.session_log_pattern", "Extra sensitive regex"},

    // ---- 快捷键域（表头四列与「未绑定」在上方只读表段，这里只有组名那一枚）----
    {"shortcuts.overrides", "Key override table"},

    // ---- SFTP 浏览器（SPEC.FEAT.CONN.04；codespec/UI_SFTP.draft.md §4，裁决 7.95）----
    {"sftp.title", "SFTP files"},
    {"sftp.action.close", "Close"},
    {"sftp.action.retry", "Retry"},
    {"sftp.action.up", "Up"},
    {"sftp.action.refresh", "Refresh"},
    {"sftp.action.upload", "Upload"},
    {"sftp.action.download", "Download"},
    {"sftp.action.new_folder", "New folder"},
    {"sftp.action.new_file", "New file"},
    {"sftp.action.rename", "Rename"},
    {"sftp.action.delete", "Delete"},
    {"sftp.action.cancel", "Cancel"},
    {"sftp.action.confirm", "OK"},
    {"sftp.column.local", "Local"},
    {"sftp.column.remote", "Remote"},
    {"sftp.column.name", "Name"},
    {"sftp.column.size", "Size"},
    {"sftp.column.mtime", "Modified"},
    {"sftp.column.permissions", "Permissions"},
    {"sftp.status.connecting", "Connecting..."},
    {"sftp.status.connected", "Connected"},
    {"sftp.status.failed", "Connection failed"},
    {"sftp.status.not_ssh", "The current tab is not an SSH session"},
    {"sftp.error.not_connected", "Not connected"},
    {"sftp.error.network", "Network error"},
    {"sftp.error.permission_denied", "Permission denied"},
    {"sftp.error.not_found", "Path not found"},
    {"sftp.error.cancelled", "Cancelled"},
    {"sftp.error.local_io", "Local read or write failed"},
    {"sftp.error.protocol", "Protocol error"},
    {"sftp.delete.title", "Confirm delete"},
    {"sftp.delete.body", "Delete {0}? This cannot be undone."},
    {"sftp.delete.dir_note", "Only empty directories can be deleted."},
    {"sftp.delete.confirm", "Delete"},
    {"sftp.input.name", "Name"},
    {"sftp.input.new_folder", "New folder"},
    {"sftp.input.new_file", "New file"},
    {"sftp.input.rename", "Rename"},
    {"sftp.transfer.uploading", "Uploading {0}"},
    {"sftp.transfer.downloading", "Downloading {0}"},
    {"sftp.transfer.queued", "Queued {0}"},
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

/// @brief 某一档的**源表**（不经装填）。
[[nodiscard]] auto source_tier(config::InterfaceLanguage language) -> const Tier & {
    return language == config::InterfaceLanguage::En ? kStringsEn : kStringsZh;
}

/// @brief 某一档的 `Locale`：区域标签逐字取落盘名，映射唯一在 `config::language_tag()`。
[[nodiscard]] auto locale_for(config::InterfaceLanguage language) -> aurora::Locale {
    return aurora::Locale{std::string{config::language_tag(language)}, {}};
}

[[nodiscard]] auto keys_of(const Tier &tier) -> std::vector<std::string> {
    std::vector<std::string> out;
    out.reserve(tier.size());
    for (const auto &[key, text] : tier) {
        out.push_back(key);
    }
    return out;
}

/// @brief 某一档的全部 key。`std::map` 的遍历序就是 key 的字典序，故交出去时已排序。
[[nodiscard]] auto keys_for(config::InterfaceLanguage language) -> const std::vector<std::string> & {
    static const std::vector<std::string> zh_keys = keys_of(kStringsZh);
    static const std::vector<std::string> en_keys = keys_of(kStringsEn);
    return language == config::InterfaceLanguage::En ? en_keys : zh_keys;
}

/// @brief 进程当前语言档的唯一副本：只有收 `InterfaceLanguage` 的那一枚装填函数写它。
///
/// 写点按文件头①的纪律排在**任何控件绘制之前**（`src/main.cpp` 里那一句），所以「一处构造、逐次
/// 同值」在运行期依然成立；而 `Locale::tag()` 每次调用现拼串，面板侧因此只留这一份 `Locale`。
auto current_locale() -> aurora::Locale & {
    static aurora::Locale locale = locale_for(config::InterfaceLanguage::Zh);
    return locale;
}

}  // namespace

auto settings_locale() -> const aurora::Locale & { return current_locale(); }

auto install_settings_strings(aurora::StringTable &table, config::InterfaceLanguage language) -> void {
    const aurora::Locale locale = locale_for(language);
    table.set_default_locale(locale);
    for (const auto &[key, text] : source_tier(language)) {
        table.add(locale, key, text);
    }
}

auto install_settings_strings(config::InterfaceLanguage language) -> void {
    current_locale() = locale_for(language);
    install_settings_strings(aurora::default_string_table(), language);
}

auto settings_string_keys(config::InterfaceLanguage language) -> const std::vector<std::string> & {
    return keys_for(language);
}

auto settings_string_text(config::InterfaceLanguage language, std::string_view key) -> std::string {
    const Tier &tier = source_tier(language);
    const auto found = tier.find(key);
    return found == tier.end() ? std::string{} : std::string{found->second};
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
