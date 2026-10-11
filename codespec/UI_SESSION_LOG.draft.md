# 会话日志设计稿（`SPEC.FEAT.CONN.11`）

> **状态**：**评审已收口（2026-10-12）**——§3 的 D1–D13 经人逐条裁决：**D1–D9、D11–D13 全按稿面推荐档**（无一条改判），**D10 改判「内置固定词表 + 用户可配追加正则」**（稿面原推荐是只落内置词表、可配延后）。据此并入 `codespec/SPECIFICATIONS.md` §7 裁决 **7.116**，转实现按 §4 布局分三批走。
> **实现落地（2026-10-12，裁决 7.117）**：三批全部落完——批 1＝折算件 + 落盘件 + `Session` 挂钩（`5716ac7`），批 2＝菜单第三条 + 命令面板两态 + 提示对话框 + 新键六处同步 + 装配（`f9bc491`），批 3＝本回写一笔。**界面路径自此可达**：设置页勾上能力闸 ⇒ 右键出第三条 ⇒ 点它弹风险提示 ⇒ 确认后文件真出现（0600）。结项读数 `ctest --preset linux -E etest_` **81 项**、`borealis_test_runner` 全跑 **1019 案**全绿。**但 §6 六条真机走查一条未做 ⇒ 本稿与裁决一律不称「可用」**（AGENTS §4.4 第 33 条）。落地期五处口径**按实现实际改口而不是照抄稿面**，逐条见 §8 的「批 2/3 收账」。
> **D10 改判的连带回写**（本稿七处已同步改口）：§1 交付第 6/7 条、§2 判据 7（设置页自此**三行**）、§3 D10 行（含新增键的六处同步与 F8 的三处计数改口）、§4 文件布局多出 schema/store/form_transfer/settings_i18n 四行、§5 多一条「编译点」、§6 多两条验收（可配正则命中 + 非法模式拒绝开启）、§7 把「可配正则」移出「不做」清单。草图屏 C 与屏 A 的对话框注记同步改口。
> **D10 改判带出的第二条裁决（人已裁）**：用户自带正则**非法**时（F13：`std::regex` 在构造期就抛 `regex_error`）⇒ **拒绝开启并回报错误**，不退回内置词表静默续记。理由：过滤是缓解腿，「用户显式配了而失效」必须可见——「不开日志」比「以为打了码其实没打」安全。
> **配套高保真草图**：`codespec/UI_SESSION_LOG.draft.svg`（矢量源）/ `codespec/UI_SESSION_LOG.draft.png`（1× 出图）。图中圈号 1–7 即 §2 的判据标号，各贴在所属屏旁；SVG 为事实来源，改图直接改 SVG。
> **上游依据**：`SPEC.FEAT.CONN.11` 原文（`codespec/SPECIFICATIONS.md` §连接域）、裁决 **7.9**（会话日志默认态：默认关闭 + 开启时风险提示 + 可选敏感输出过滤）、`SPEC.FEAT.CONN.09` 的审计边界（该条原文与 `codespec/ARCHITECTURE.md` §12.1 第 2 条都把**日志文件目录排除在凭据审计之外**，单独标注「用户显式开启、风险自负」）、架构 §7.5 的归属句（`ARCHITECTURE.md`：「会话日志由会话层落盘、路径配置归 `borealis::config`」）、7.104 D1①（一次性提示的每对象上弦形态）、7.41（右键三态与菜单条目族）、7.38⑥（两态不发菜单）、7.99 D4①（构造期快照与在途不重放）、7.26③（配置目录来源）。
> **既成地基（本棒只消费，不新建）**：`connection.session_logging` / `connection.session_log_dir` 两枚键**已全链路在册而零消费方**（schema `include/borealis/config/settings.h:222-223`、落盘读写 `src/config/store.cpp:532-533` 与 `:846-847`、表单搬运 `src/config/form_transfer.cpp:384-389`、面板词条 `src/ui/settings_i18n.cpp:462-463`、反向核对表两行标 `Absent × NextSession` `src/ui/settings_catalog.cpp:348-357`、默认关这条已有证人 `tests/unit/utest_config.cpp:599`）⇒ 这是一条**有闸可证**的缺口：接线之后核对表必然要求改判，漏判即红。其余现成品见 §0 的 F1–F9。

---

## 0 地基实测（2026-10-12 本沙箱，POSIX 腿 + C++/Python 探针；Windows 腿一律标未验证）

形态不是推演出来的，F10–F15 每条都有本机读数（探针是一次性脚本，不入仓；复现口径见 §8）。

| # | 读数 | 对本稿的约束 |
|---|---|---|
| **F1** | 会话的入向字节**只有一个入口**：`Session::ingest(bytes, end_of_stream)`（`include/borealis/session/session.h:229` + `src/session/session.cpp:174-203`），`on_bytes` 与 `on_closed`（`:162`/`:164`）都汇到它；临界区里做「解码 → `terminal_.feed` → 收脏行 → 换出应答」，出锁后才发应答与唤醒帧 | 落盘的**取数点**只可能在这一处（或其内部），别处都拿不到「全量远端输出」。判据 1 的「一处挂钩」成立 |
| **F2** | 会话层的既有纪律是「**锁内不做 IO**」：查询应答锁内登记、出锁才写连接（`src/session/session.cpp:188` → `:190-192`），`OSC 52` 的剪贴板写锁内只留存、由 `ClipboardOutbox` 在帧边界取走落地（`:151-155`，`include/borealis/session/clipboard_outbox.h:31`） | 日志件若想在持锁期间写文件即违这条铁律。可行的形态只有两种：出锁后同轮写（D5①），或留存后由别处取走写（D5②） |
| **F3** | `Session` 的**全部**成员见 `include/borealis/session/session.h:246-277`：连接、终端状态机、解码器、编码档、行尾档、脏队列、应答缓冲——**没有 id、没有标题、没有档案、没有起始时刻** | 会话不知道自己是谁。日志文件的**命名素材只能由装配层给**（`TabId` 与档案名都住在 `src/main.cpp:411-451` 那几张按 TabId 的表里），故落盘件的入参是一份「已算好的路径 + 档」，不是一句「你自己起名」 |
| **F4** | 会话**不自有线程**（`session.h:50-51`）：读线程由连接实现持有，`ConnectionEvents::on_bytes` 的文档明写「回调发生在连接的读线程上」（`include/borealis/session/connection.h:69-77`）。**但有一条例外**：设置面板的预览腿 `PreviewConnection` 在**主线程**上呼 `events_->on_bytes(raw)`（`src/ui/settings_preview.cpp:90`，其注释 `:67-68` 说明那是 `Session::resize` 锁外路径） | 「在读线程写文件不违 AGENTS 第 25 条」这句话有一个真实的反例。故 D5 的推荐档必须自带一条闸：**预览会话不挂日志**（装配层不给它 `LogSpec`），而不是「容忍主线程写盘」 |
| **F5** | 网格侧**没有软换行标志**：`include/borealis/grid/row.h` 与 `storage.h` 全文无 `wrap`/`soft` 命中；既有的「取纯文本」件是**按选区**的 `ui::copy_text(const grid::Storage&, const Selection&, const CopyOptions&)`（`include/borealis/ui/selection.h:127`），其逐行读数体 `row_text` 是 `src/ui/selection.cpp:44-76` 的文件私有函数，未导出 | 「剥离 SGR 但保留换行结构」**从网格还原不出来**：一旦折成屏行，硬回车与列宽换行就成了同一条 LF。要拿到真实换行结构，取数点必须在字节/码点这一侧（D2① 的根据，也是 D4 否掉「原生式=网格快照」的根据） |
| **F6** | 解析器吃的是**码点**不是字节：`vt::Parser::feed(std::u32string_view, SequenceSink&)`（`include/borealis/vt/parser.h:62-67`，头注 `:6-8` 明写），产出 `Sequence{kind, code_point, …}`，`SequenceKind` 里 `Print` 与 `Execute` 两类带着可打印码点与控制码（`include/borealis/vt/sequence.h:18-28`） | 折算件可以只依赖解析器的**公开出口**（`Print`/`Execute` 留、其余丢），不必自造一条转义识别机；UTF-8 落地有现成件 `term::encode_utf8`（`include/borealis/term/utf8.h:95`） |
| **F7** | 一次性提示的既有形态是「**每对象只上弦一次**」：`Session::take_unrepresentable_notice()`（`include/borealis/session/session.h:158-164`）首次处置上锁内 latch、此后只累计数不再上弦，「会话重启＝新对象，latch 从零开始」；跨进程的「已提示过」标志**全仓不存在**（`startup_notice.cpp:90-93` 那个 `shown_` 是进程内的，schema 里也没有 `first_run`/`ack` 类键） | 风险提示要不要**持久化**是一个真选择，不是既有能力的复用（D7）。要持久化就得新增一枚 schema 键，代价见 F8 |
| **F8** | 反向核对表是**双向闸**：`tests/unit/utest_settings_catalog.cpp:291` 钉死「写出的 schema 键 = 表里的行数」＝**63**，`:556` 钉死 `Absent` 行数＝**21**，`:526-544` 钉死「`Wired ∧ NextSession`」的**逐条名单**。多一枚键要同批改五处（schema / store 读写 / form_transfer / 核对表行 / 词条），并改这三处计数 | 「加一枚设置键」在本仓不是免费的。§3 的 D8/D9/D10 都以「本期究竟新加几枚键」为轴，能不加的不加（AGENTS §4.1 第 3 条） |
| **F9** | 文件创建的现成品两条：`platform::create_private_file(path)`（`src/platform/file_secure.h:48`）＝ `O_CREAT\|O_EXCL` + **0600 不受 umask 影响** + 符号链接不跟随 + **只建不写、不代建父目录**，其头注 `:23-24` 明写「阻塞文件系统 IO，调用方须在 worker 线程」；父目录那条走 `key_store.cpp:380-391` 的形态（`create_directories` + `permissions(owner_only)` 钉 0700） | 0600 这条安全口径**不需要新平台件**（本棒不新增 `src/platform/` 任何东西，AGENTS 第 23 条零风险）。Win 腿的权限语义未经 MSVC 验证（`SPECIFICATIONS.md` 附录记它只 WARN），标推断 |
| **F10** | **PTY 把 `\n` 折成 `\r\n`**：同一个 `printf 'value=%d\r\n'` 循环，直写管道 691 字节（CR 41 / LF 41），经 PTY 捕获 893 字节（CR **82** / LF 44）；真会话里 `ls --color=auto /etc` 的 7149 字节是 CR 83 / LF 86 / **CRLF 83**，ESC 309 枚**全部**落在 `ESC[` 族（SGR），BS 0 | 折算的第一条判据由读数定形：**成对的 CRLF 是一条换行**，孤立 CR 另档（D3）。真会话输出里 SGR 是绝对主角，剥离后可打印率实测 **0.994**，产物逐行可读（F11） |
| **F11** | 朴素剥离（丢 `ESC]…BEL/ST`、丢 `ESC[ … 终结符`、丢两字节单字符转义，再把 CRLF/CR 折成 LF）在 4 KiB 的彩色 `ls` 产物上得到 99.4% 可打印、首屏即目录列表本体 | 纯文本档**值得做且能做好**；且剥离开销是一次线性扫描，不涉状态机 |
| **F12** | **整屏重绘在剥离档里会「多出来」**：40 次 `\033[H\033[2J` + 一行 `value=N` 的样本，屏上始终只有 1 行，剥离产物却有 **43** 条非空行；`top -b -n 2`（批处理档，76305 字节、ESC 0、CR 949/LF 952）不受影响 | 这条是**语义而非缺陷**：剥离档记的是「远端发出的行」，不是「屏上那一屏」。界面文案与走查判据都得照此说清，否则用户第一次 `top` 完打开日志会以为坏了（判据 6） |
| **F13** | 非法正则的形态与成本（libstdc++，`-std=c++20`）：`ECMAScript \| icase` 的 `password\|token\|secret` 在**含中文 UTF-8 字节的 `std::string`** 上直接命中（`match=1`），不需要 `search.cpp` 那套 UTF-16 缓冲；`(password\|token` 抛 `std::regex_error`（code=5，"Mismatched '(' and ')'"）；`.` **不跨 `\r`**（`a\rb` 不匹配 `a.b`）；10000 行 / 430 KB 全量扫描 **13 ms** | 打码可以在**行**上做（`regex_search` + 整行替换），不必转 UTF-16，也拿不到列位置——正好是本需求要的粒度（D10）。非法模式在构造期就抛，故「装载侧不校验域、开启时才编译」这条路必须配一条 fail-closed（D10②） |
| **F14** | 同步 append 的成本量级（本沙箱 NVMe、普通 `std::ofstream(app)`）：4 KiB 分片 × 2048（共 8 MiB）累计 **2.5 ms**，均值 **0.0012 ms/片**、最差 0.016 ms/片；每片都 `flush()` 时均值 0.002 ms、最差 0.018 ms | 「在会话的读线程上同步写」比一次 socket 读分片便宜**几个数量级**，D5① 因此成立。慢盘/NFS 的形态本沙箱无从复现 ⇒ 该外推标**推断／未验证**，走查第 ⑸ 条实测 |
| **F15** | 会话级有界队列的既有两形态都是**丢弃型**：`DamageQueue` 越界做合并（`src/session/damage_queue.cpp:28-48`，容量 64 见 `damage_queue.h:23`）、`SftpPanel` 的收件箱越界**丢最旧**（`src/ui/sftp_panel.cpp:46` `kInboxCapacity=64`、`:196-201` `pop_front`） | 现成的「异步落盘」先例都带一个静默丢数据的口子。日志丢行＝日志不可信，比慢一点严重 ⇒ D5 不选这条形态，代价如实写在 §5 |
| **F16** | 菜单命令族是**留了位而没有货**：`ui::MenuCommand` 现只有 `Copy`/`Paste` 两值，其头注明写「视觉稿的其余各项（……会话日志……）随各自需求落地再入列，不在这里留『点了什么也不发生』的空位」（`include/borealis/ui/right_click.h:36-44`）；`plan_right_click(RightClickAction, bool has_selection)` 是纯函数（`src/ui/right_click.cpp:5-26`）；绘制侧的 `menu_label` 与 `on_menu_command` 都是穷尽 switch（`src/ui/terminal_view.cpp:193-201`、`:1458-1473`） | 会话日志正是那条「到货的消费方」。加一条命令要**同时**动四处，且 `plan_right_click` 的签名必须多收一枚「此刻的开关态」——否则菜单项无法区分「开启/停止/不可用」（D6，代价见 §4 批 3） |
| **F17** | 「无宿主就不问」与「留空即不画」两条既有纪律同形：`ask_multiline_warning`（`src/ui/terminal_view.cpp:1511-1534`）在 `host_ == nullptr` 时直接不动作，而 `set_restart_hook` 的头注把同一条判据写成明文的（`src/ui/terminal_view.h:313-319`：「留空即不画按钮——那是装配层尚未接好这条腿的形态，而不是画了但不响」） | 日志开关的钩子沿用这一条：装配层未注入 ⇒ 菜单项**不出现**（不是灰显），预览与夹具因此天然没有入口，F4 那条例外不会被踩到 |
| **F18** | 无头文件系统的现成地基：`aurora::testing::isolation::temp_dir()`（`tests/framework/isolation.h:36`，逐案 `fs::remove_all` 收尾），`utest_settings_catalog.cpp:54-56` 已在用它造路径 | 落盘类断言不需要任何新夹具，也不需要真用户目录（D12 的证人面） |
| **F19** | 配置目录来源：`Preferences::default_config_dir()`（Aurora `src/aurora/preferences/preferences.cpp:249-268`）＝`$XDG_CONFIG_HOME`，否则 `$HOME/.config`，否则当前目录——**不含应用子目录**；本仓的 `borealis.json` 就直接落在那一层（裁决 7.26③） | 「`session_log_dir` 留空时的回落点」需要自己带一个有归属的名字（D9）。另一条边界：凭据审计的范围是配置目录（`SPEC.FEAT.CONN.09` 原文），日志目录被**明文排除**在该审计之外 ⇒ 回落点选在配置目录里既不违规也最好找（判据 7） |

---

## 1 范围与边界

**本期交付**：

1. **一份会话级落盘件**（`session::SessionLog`）：吃入向码点流，出 UTF-8 文本文件。折算＝丢 `Escape`/`Csi`/`Osc`/`Dcs*`/`Ignored`，留 `Print`，`Execute` 里的 CR/LF 折成 LF、TAB 留、BS 与其余 C0 丢（D3）。文件首建走 `platform::create_private_file`（0600，F9），后续同会话 append。
2. **可配时间戳前缀**：开启时给每条**折算出的行**加一次本地时刻前缀，形态固定 `[%Y-%m-%d %H:%M:%S] `；档位来源是一枚新的**会话级参数**，本期不进 schema（D8，代价见该条）。
3. **越限覆盖**：单文件字节上限取**文件级常量**，越限时把已写内容的前半丢弃、后半前移到文件头继续写（语义即「覆盖最旧的」）。上限不可配、不轮转多文件（D9，在册）。
4. **逐会话显式开关 + 默认关闭的兑现**：右键菜单追加一条 `MenuCommand::ToggleSessionLog`，同时注册一条命令面板命令 `session.toggle_logging`——两条入口都**必须**有，因为 `terminal.right_click` 取 `Paste` / `CopyOnSelect` 两态时**根本不发菜单**（裁决 7.38⑥），只挂右键会让一半用户没有入口（D6）。既有两枚键把 `connection.session_logging` 读成**能力闸**：关 ⇒ 菜单项不出现、命令不响；开 ⇒ 仍需逐会话点一次（D1）。
5. **风险提示**：开启动作那一刻用现成的 `au::confirm` 弹一次「日志可能包含密码 / 令牌」，文案里报出**目标文件的完整路径**；确认后才创建文件（D7）。
6. **敏感输出过滤**：本期落**内置固定词表**（`password|passwd|token|secret|api[_-]?key|authorization`，`ECMAScript | icase`，整行命中即整行替换为 `[redacted]`）**并叠加用户可配的正则**——新键 `connection.session_log_pattern`（string，缺省空＝只用内置词表），二者是并集关系（D10 改判，人裁）。非法模式 ⇒ **拒绝开启并回报错误**（fail-closed，见状态块第二条）。
7. **核对表三处改口**：`connection.session_logging` 与 `connection.session_log_dir` 从 `Absent` 改判 `Wired`（消费方＝会话层 + 装配层），`session_logging` 的生效档随之从 `NextSession` 改 `Immediate`（开启动作现读，不重放在途会话）；新键 `session_log_pattern` 自入表起就是 `Wired ∧ NextSession`（编译点在开启那一刻）。F8 那三处计数因此同批改口（63→64、`Absent` 21→19、`Wired ∧ NextSession` 名单 +2）。

**本期不交付**（§7）：带属性的原生存式（自定格式）、按会话记忆的开关（重启程序后不自动续开）、跨进程的「已提示过」持久化、日志文件的回放/查看界面、目录级配额与多文件轮转、上限可配、真机走查。（原列在此的「正则可配」随 D10 改判**升为本期交付**。）

**关键前提（口径不是本稿新造的）**：

- **架构已给归属**：`ARCHITECTURE.md` §7.5 那句「会话日志由会话层落盘、路径配置归 `borealis::config`」是本稿 D2/D11 的**上界**——落盘的代码进 `src/session/`，键的读写留在 `src/config/`，装配层只做「算路径 + 注入档」。
- **本棒不新增任何 `src/platform/` 东西**（AGENTS 第 23 条零风险面）：0600 用 F9 的现成件，目录创建用 key_store 的现成形态。
- **`Session` 的构造点有八处**（`src/main.cpp` 七处 + `tools/bench/render_throughput.cpp:254`），F3 决定了日志不可能是构造入参——它是**运行期动作**，故走「钩子 + setter」而不是「又一个构造参数」，八处构造点一字不动。
- **无自动化证人的面照旧存在**：`src/main.cpp` 不编入 CTest runner（裁决 7.83），故装配那两处（路径派生、能力闸的读点）没有证人，按 Telnet 批 3 的同档口径登记为在册代价（§7）。

---

## 2 草图与判据对应

草图四屏（A 右键菜单两态 + 风险提示对话框 / B 日志文件的真实形态对照 / C 设置页连接域两行改口 / D 侧栏与本期边界），图中圈号 1–7 即下表判据：

| 标号 | 判据 | 对应需求 |
|---|---|---|
| 1 | 右键菜单自此**三条**：复制 / 粘贴 / **会话日志**（`Copy`、`Paste` 两条逐字节不变，新增项排在末尾，次序即 `plan_right_click` 的产出次序）。文案随当前态在两值之间取：**未记录＝「开启会话日志」，记录中＝「停止会话日志」**；`terminal.right_click` 取 `Paste`/`CopyOnSelect` 两态时菜单根本不发，此时入口只剩命令面板那一条（判据 2） | `CONN.11`「开启须用户逐会话显式动作」+ `INTERACT.03` 的三态语义（D1/D6） |
| 2 | 命令面板与快捷键同一条：`session.toggle_logging`，标题「会话日志：开启 / 停止（当前标签）」随态改；能力闸关闭时该命令 `enabled=false`（灰显不响应，同 F16 那族）| 同上（D6，判据 1 的另一半） |
| 3 | 点开启 ⇒ **先弹提示后建文件**：`au::confirm` 两行话术——「日志可能包含密码、令牌或其他敏感信息。」+「将写入：`<完整路径>`」；确认才落文件并返回「记录中」，取消则**什么都不创建**（不留零字节的空文件）。全仓没有跨进程的「已提示过」标志，本棒也不新增（F7/F8）⇒ 每次开启都问一次，这是**刻意的**（D7 的代价面） | 裁决 7.9「开启时风险提示」+ 原文「首次落盘给出风险提示」（D7） |
| 4 | 落盘形态（`<目录>/session-<TabId>-<起始 epoch 秒>.log`）：一次开启＝一个文件；标签名/档案名**不进文件名**（中文、空格、`/\:*?"<>|` 都要洗，洗名字是新风险，D13），人类可读性由判据 3 那句「将写入：完整路径」承担 | 原文「按会话开关的输出落盘记录」（D13） |
| 5 | 换行结构按 F10 的读数折算：CRLF（PTY 与 Telnet 的主流形态）与 LF 各折成**一条**换行；孤立 CR（覆盖式进度条）也折成一条换行；**BS 丢弃**——不在日志里模拟回退覆盖，代价是 `progress` 那种一屏刷新的输出会留下一串中间态行（F12 同族）。TAB 保留，其余 C0 丢弃 | 原文「纯文本（剥离 SGR 属性但保留换行结构）」（D2/D3） |
| 6 | 时间戳前缀按**行**给（不是按字节分片），且只在本件的折算边界上插——分片边界与行边界不等价（F10 一片可含 3.5 行）。开启态若越限：日志文件里出现一次「前段被覆盖」的分界，界两侧时刻仍单调 | 原文「可配时间戳前缀」+「环形覆盖策略」（D8/D9） |
| 7 | 设置页「连接」页自此**三行**有消费方。`connection.session_logging` 是**能力闸**——行旁注一句「开启后仍需逐会话显式点『开启会话日志』」；`connection.session_log_dir` 留空＝落到 `<配置目录>/borealis-logs`（F19：该目录不含应用子目录，故本仓自带一个有归属的名字），非空＝原样用；**新行** `connection.session_log_pattern` 是「追加正则（空＝只用内置词表）」，与内置词表取**并集**。三行的徽标：`session_logging` 从「延后」变「即时」（开启动作现读），`session_log_dir` 与 `session_log_pattern` 保持「延后」（路径与模式都在**开启那一刻**取用，改它不回溯在途会话） | 原文「日志文件路径可在设置中配置」+「按可配正则」+ 裁决 7.9 的默认关闭（D1/D9/D10②） |

---

## 3 裁决（D1–D13，2026-10-12 人已逐条裁完；下表「推荐」列即稿面立场，**除 D10 外全部按推荐档收口**，裁决随理由与代价一并入册为 7.116）

| # | 待定项 | 选项 | 推荐 | 理由与代价 |
|---|---|---|---|---|
| **D1** | 既有两枚键与「逐会话开关」的关系 | ① `connection.session_logging` 读成**能力闸**（默认关；关＝菜单项与命令都不出现，开＝仍需逐会话点一次）；② 读成「新会话缺省档」（开＝每个新标签自动开始记录）；③ 键只做参数、开关纯会话侧，键改判 `Absent` 不动 | **①** | 原文两句必须同时成立：「默认关闭」与「开启须用户逐会话显式动作」。② 直接把第二句变成摆设（一次全局勾选＝所有会话都在落盘，与裁决 7.9 的安全取向正面冲突）；③ 让那枚键继续零消费方，面板画着控件而没人读它——F8 的闸正是为这种分叉设的。① 是两者唯一自洽的读法，代价是「两道开关」的理解成本，用判据 7 那句旁注承担 |
| **D2** | 折算的**取数点** | ① **入向码点流**（`Session::ingest` 内、`decoder_->feed` 之后那份 `decode_buffer_`，喂给状态机的同一份）；② 入向**原始字节**（解码前）；③ 网格/副本的屏行文本 | **①** | ③ 被 F5 直接否掉：网格没有软换行标志，屏行折叠会把「远端一行长输出」记成多条 LF，正与需求那句「保留换行结构」相反，且 `row_text` 未导出、要用它就得开新公共 API。② 要在日志件里再解一次码（同一份字节解两遍），或让日志文件跟着会话编码走（GB18030 会话的日志就不是 UTF-8，判据 5 的「纯文本」名不副实）。① 落在既有那一次解码之后，落盘统一 UTF-8，且 `Print`/`Execute` 正是 F6 那两类**带着内容与换行**的产出。代价：日志与状态机共用 `decode_buffer_` 这一份缓冲，挂钩必须在 `terminal_.feed` 之前或同处、且**不得**持锁写（F2） |
| **D3** | 控制码的处置表 | ① CR/LF/CRLF→一条 LF；**孤立 CR 折成换行**；BS 丢弃；TAB 保留；其余 C0 丢弃；转义族（`Escape`/`Csi`/`Osc`/`Dcs*`/`Ignored`）全丢；② 孤立 CR 丢弃（视同覆盖不产生换行）；③ BS 也保留（日志里留下 0x08） | **①** | F10 的读数是决定性的：真会话里 CR 与 LF **成对出现**（83 对 / CR 83 / LF 86），所以「CRLF 折一条」无论如何都要做；剩下孤立 CR 只有两档，① 把覆盖式输出记成多行（每帧一行，可 grep），② 把它们粘成一条超长行（不可读且可能一条几 MB）。BS 走①（丢弃）的理由同 F12：日志不是屏幕仿真器，模拟回退覆盖等于再造一台状态机。代价：`progress`/下载条一类在日志里是一串行而非一行；这一条与 F12 的整屏重绘同源，都归判据 6 的那句「记的是远端发出的行」一并说清 |
| **D4** | 「带属性的原生存式（自定格式）」本期是否做 | ① 本期只做纯文本档，原生式**在册延后**；② 顺带做「原始字节原样落盘」当原生式；③ 真做一份带属性的自定格式（逐帧 + 属性表） | **①** | 原文是「或」，两档取其一即满足验收；③ 要新造一套格式**和它的查看侧**，写得出而读不了的格式在本仓没有判据可证（§6 断不了的东西就是 F16 头注那句「点了什么也不发生」的另一副面孔）；② 看着便宜，但它是「把字节倒进文件」而不是「自定格式」，`top`/`vim` 一开就是一份几十 MB 且没有换行结构的二进制文本（F10/F12 的同一批读数反过来用），交付一个用户读不懂的文件不是功能。代价：需求这一腿本期只到一半，§7 登记为延后子项并在 `SPECIFICATIONS.md` 的现状句里写明「原生式未落」 |
| **D5** | 谁写文件、在哪条线程写 | ① **读线程同轮 append**（`ingest` 出锁后写；日志件由 `Session` 持有，仅装配层显式注入过 `LogSpec` 的会话挂得上）；② 会话级 writer 线程 + 有界队列（照 `SftpPanel` 的收件箱形态，F15）；③ 帧边界取走落地（照 `ClipboardOutbox`，F2） | **①** | F14：4 KiB 分片同步 append 均值 0.0012 ms，比一次读分片低几个数量级，读线程本来就在做 IO——这是它的主业。② 的两个现成先例（F15）都带**静默丢数据**的口子，日志丢一行比慢一点严重得多，而「不丢」的正确实现是「越界就阻塞读线程」，那与①相比只多了一条线程与一次握手。③ 更糟：`ClipboardOutbox` 那条例子的前提是「剪贴板是主线程的 IO」，而**主线程写盘正是要避开的那件事**（F4 的预览例外就是这条路的活样本）。代价（①实际吃下的两条）：磁盘慢会拖慢该会话的输入到达（慢盘/NFS 本沙箱无从复现 ⇒ **推断／未验证**，走查第 ⑸ 条实测）；预览会话在主线程喂字节（F4）⇒ 结构上**不给它注入 `LogSpec`**，并由 F17 那条「未注入就没有入口」的纪律保证这不是靠人记住 |
| **D6** | 开关的 UI 落点 | ① 右键菜单一条 + 命令面板一条（同一闭包），两态文案；② 只做右键菜单项；③ 只做命令面板（不碰菜单）；④ 视口里常驻一枚小图标 | **①** | ② 单独不成立：`terminal.right_click` 的 `Paste`/`CopyOnSelect` 两态**不发菜单**（裁决 7.38⑥，`include/borealis/ui/right_click.h:19-21` 写明的），这两档用户拿不到入口；③ 单独也不成立——菜单里那条「会话日志」是本需求在界面语汇里的天然位置，且 `right_click.h:36-44` 的头注早就把它列为待入列项（F16）。④ 要新的绘制件与新的常驻位置，本期不值。代价：`plan_right_click` 是纯函数，要区分「开启/停止/不可用」就得**多收一枚入参** ⇒ 它的签名与既有的 `utest_right_click`/`itest_right_click_paste` 用例同批改口（§4 批 3），这是本棒唯一一处「改既有公共件签名」，原因须写进裁决（否则后人会当成顺手扩面） |
| **D7** | 风险提示的时机与次数 | ① **开启动作即弹** `au::confirm`，每次开启都问，确认后才建文件；② 严格照原文「首次落盘」时弹 `alert`（需要一个「已落盘」边沿与一次跨线程投递）；③ 弹过一次就持久化不再问（新增 schema 键） | **①** | ③ 要新键，代价是 F8 那一整套（63→64、三处计数、装载、搬运、词条），且「永久不再提示」正削弱裁决 7.9 想买的提醒效果；② 的「首次落盘」在①之下**必然已被满足**（每次开启都问 ⇒ 第一次落盘前一定问过），而它要多造一条边沿投递腿，没有额外收益。① 复用现成品 `ask_multiline_warning` 的形态（F17 同一条「无宿主就不问」），并且提示语里能报出**目标文件的完整路径**——这是原文那句话真正想要的东西。代价：与原文「首次落盘给出」措辞有别 ⇒ 按 AGENTS §4.2 第 11 条回写成裁决，不改需求原文 |
| **D8** | 时间戳前缀的**档位来源** | ① 会话级参数（开启时由装配层给，形态固定「每行 + 本地秒」），不进 schema；② 新增 schema 键 `connection.session_log_timestamp`（bool）；③ 新增枚举键给多档（关/秒/毫秒/无） | **①** | 需求只说「可配」，而「配」的入口在**开启动作**里就已经成立（SecureCRT/iTerm2 也都是开启时定形）。②/③ 的代价 F8 已经量出来：每枚键都是「schema + store 读写 + 搬运 + 核对表行 + 词条 + 三处计数」六处同步，而本期**没有任何别的消费方**要读它。③ 的三档取值本期没有判据能区分手感。代价：重启程序后时间戳档不保留（回到缺省开）——与「本期不记忆逐会话开关」这条 D11 的口径同档，一并登记；若人裁改②，§4 的文件布局里预留的那一行就是它的位置 |
| **D9** | 越限「环形覆盖」的实现档与上限 | ① 单文件 + 上限常量，越限**丢弃已写内容的前半、把后半前移到文件头**继续写；② 真环形（两个半区 + 头尾指针，读时拼接）；③ 越限即**停止写入**并投一次性提示；④ 多文件轮转 `.1/.2` | **①** | 原文说「环形覆盖策略」，② 名义最贴但产物**人读不了**（`less` 打开是尾段拼头段，除非再做一个读取器——即 D4③ 那类「没有消费方的格式」）。③ 不覆盖，违背原文；④ 是「轮转」不是「环形」，且目录里凭空多份历史文件。① 语义等价于「覆盖最旧的」而文件始终是正常文本，代价是越限那一刻要做一次「读后半 + 重写」的全量 IO——按上限 8 MiB 计最坏读回 4 MiB，发生在一轮 `ingest` 出锁后的写点上（F14 的量级外推），且**只在越限的那一轮**发生。上限取 `kMaxSessionLogBytes = 8 MiB` 文件级常量、不入 schema（D8 同一条理由），§7 登记为可配延后 |
| **D10** | 敏感输出过滤的本期形态 | ① **内置固定词表**（`password\|passwd\|token\|secret\|api[_-]?key\|authorization`，icase），整行命中即整行替为 `[redacted]`，正则可配延后；② 本期就加可配正则键（空＝不过滤）；③ 本期不做过滤 | **①＋② 的并集（人改判，非稿面推荐）** | 稿面原推荐是 ①（F8 量过每枚键的六处同步，而本期看似没有别的消费方要读它），**人裁决改为「内置词表 + 用户可配追加正则」**：内置词表解决「缺省就该有缓解腿」，可配正则解决原文那句「按可配正则匹配 password / token / secret 等行」——只落前者等于把原文那一截推给延后，而这一截正是需求点名的形态，不是「将来才用」的抽象。二者取**并集**：新键 `connection.session_log_pattern` 缺省空＝只用内置词表（避开②原档「缺省空＝多数人根本不会去填，等于没有过滤」那个陷阱，因为内置词表已经在挡），非空＝内置之上叠加。<br>**代价（本条实际吃下的四条）**：① 新增一枚 schema 键 ⇒ F8 那一整套六处同步（schema / `store.cpp` 读写两处 / `form_transfer` / 核对表行 / 词条）+ `utest_settings_catalog` 三处计数改口（63→64、`Absent` 21→19、`Wired ∧ NextSession` 名单 +2）；② 非法模式在 `std::regex` **构造期**就抛 `regex_error`（F13 实测 code=5）⇒ 编译点必须落在「开启那一刻」而不是装载那一刻，并配一条 **fail-closed**（人已裁：**拒绝开启并回报错误**，不退回内置词表静默续记——过滤是缓解腿，「用户显式配了而失效」必须可见）；③ 生效档取 `NextSession`：改它不回溯在途会话的已写与正在打的码（与 `session_log_dir` 同档），故它**进** `Wired ∧ NextSession` 那份逐条名单；④ 词表与用户模式的**并集次序**要写进用例（同一行两侧都命中 ⇒ 只替一次 `[redacted]`，不叠字）。另两条落地依据不变：ASCII 供替选在含 CJK 的 UTF-8 `std::string` 上可直接匹配（**不需要** `search.cpp` 那套 UTF-16 转换）、`.` 不跨 `\r`（行内匹配天然安全）、万行级 13 ms（成本可接受）。③ 档（本期不做过滤）直接削掉裁决 7.9 点名的这条缓解腿，两档都没取 |
| **D11** | 逐会话开关要不要**跨重启记忆** | ① 不记忆：重启程序后一律回到「未记录」，与默认关闭同一条线；② 记住「这个档案上次开着日志」，下次自动续开 | **①** | ② 与裁决 7.9「默认关闭 + 开启须用户显式动作」的精神相抵（自动续开就是没有显式动作），且要么给档案加字段（第四型档案的 JSON 形态要动）要么给会话身份加持久化映射——两头都是新契约。① 的代价一句话：每次程序启动都要重新点一次，而这正是安全提示想让用户看见的那一次 |
| **D12** | 验证形态与批次 | 三层：① `utest_session_log`——脱开界面逐条断折算表（D3 的逐档）、时间戳插进行边界（判据 6）、词表打码、越限截断的前后段、路径回落与命名；② `itest_session_logging`——替身连接（`utest_session.cpp:55` 的 `FakeConnection` 那族）喂真字节，断**真实文件内容逐字节**（`isolation::temp_dir()`，F18）、断「取消提示 ⇒ 不建文件」、「能力闸关 ⇒ 无落点」；③ 走查（真 telnetd/真 shell 下的观感、慢盘手感） | 如述 | ①/② 的分工照串口棒与 Telnet 棒的既有切法：纯折算在 utest（不需要文件系统），文件语义在 itest（需要且能给）。**变异注入自证**（AGENTS 第 22 条）在 `src/main.cpp` 那两处不可用（裁决 7.83：装配不编入 runner），故本棒的证人面天生缺一块 ⇒ 按 Telnet 批 3 的同档口径登记为「无自动化证人」，不称覆盖完整。批次：批 1＝折算 + 落盘件 + `Session` 挂钩与 utest/itest（**不含 UI**，界面路径未通 ⇒ 不称可用）；批 2＝菜单项 + 命令 + 提示对话框 + 装配 + 核对表两行改口（界面路径自此可达）；批 3＝文档回写与在册收口 |
| **D13** | 日志文件命名 | ① `session-<TabId>-<起始 epoch 秒>.log`（名字里**不带**标签名/档案名）；② 带净化后的标签名；③ 带档案 id | **①** | ② 要洗的名字里混着中文、空格、`/ \ : * ? " < > |` 与控制字符，Windows 保留名（`CON`/`NUL`）还要另挡一截——那是一条**平台知识**，AGENTS 第 23 条要求它待在 `src/platform/`，本棒不想为命名新开平台腿（F9 那条腿只教「怎么建」，不教「洗名字」）。③ 的档案 id 对用户同样是不可读串。① 的可读性缺口由判据 3 那句「将写入：`<完整路径>`」补——用户在开启那一刻看到自己点了什么。代价：文件多了以后目录里只有一列时刻与序号，翻找要靠时刻排序（TabId 单调不复用 ⇒ 同目录不撞名，`src/main.cpp:414-416` 的那条纪律正好用得上） |

---

## 4 文件布局 / TU 拆分（裁决后按此开工）

```
include/borealis/session/session_log.h    新增 —— 会话级落盘件的公共契约（不含 Aurora 类型，架构 §2.3）：
                                                  `LogSpec{path, timestamp, redactor}` 一份入参形态
                                                  （**按实现改口**：稿面原写 `user_pattern` 字符串，实落的是
                                                  `compile_redactor` 的**产物**——编译点在装配层「开启那一刻」，
                                                  移交的是已编译件，模式串本身不进会话层，见 §5 那条编译点）、
                                                  `resolve_log_directory(configured_dir, config_dir)` 与
                                                  `make_log_file_name(tab_id, start_epoch)`（**批 2 补的两枚纯派生**：
                                                  稿 §4 只写「装配层算路径」，实落把算路径拆成可独立单测的两枚
                                                  纯函数，装配只调用——否则 D9①／D13① 那两条形态没有证人）、
                                                  `kSessionLogDirectoryName`（回落子目录名，D9①）、
                                                  `kMaxSessionLogBytes` 上限常量（D9）、
                                                  `fold_to_plain_text(std::u32string_view) -> std::string`
                                                  （**纯函数**：D3 的折算表，无 IO、可全量单测，AGENTS 第 20 条）、
                                                  `compile_redactor(user_pattern) -> std::optional<Redactor>`
                                                  （**纯函数**：内置词表 ∪ 用户模式；非法模式返回 nullopt，
                                                  即 fail-closed 的判定位，D10 连带口径）
src/session/session_log.cpp               新增 —— 同一件的落地腿：行缓冲 + 时间戳插行前（判据 6）+ 打码
                                                  （F13 的 icase 供替选，整行替换）+ `create_private_file` 首建 +
                                                  append + 越限截断重写（D9①）+ 写失败自我禁用并上弦一次性提示
include/borealis/session/session.h        修改 —— 三枚运行期入口（D5①/D12 批 1）：
                                                  `enable_logging(LogSpec) -> LogOutcome`、`disable_logging()`、
                                                  `take_logging_notice()`（失败/禁用那一条一次性提示，形态照
                                                  `take_unrepresentable_notice`，session.h:158-164）；
                                                  构造点与既有成员**一字不动**（八处构造点见 §1 前提）。
                                                  **批 2 补第四枚**（按实现回写）：`logging_enabled()`——菜单
                                                  条目与命令标题的两态都要现读它，而这份态除会话自己没人知道；
                                                  稿 §4 出稿时只列了三枚
src/session/session.cpp                   修改 —— `ingest` 内**只留一份引用**（锁内 `log_pending_.assign(...)`，
                                                  仅当已挂日志才做，F2：锁内不做 IO），出锁后同轮写（D5①）
include/borealis/config/settings.h        修改 —— `connection` 域追加 `session_log_pattern`（std::string，
                                                  缺省空＝只用内置词表）—— **D10 改判带来的唯一新键**（批 2）
src/config/store.cpp                      修改 —— 该键的读与写两处（缺省值不写盘的既有口径照旧）
src/config/form_transfer.cpp              修改 —— 设置表单搬运一行（与 `session_log_dir` 同族同形态）
src/ui/right_click.h                      修改 —— `MenuCommand` 追加 `ToggleSessionLog`（F16 那条头注点名的到货
                                                  消费方）；`plan_right_click` 多收一枚「日志能力 + 当前态」
                                                  入参 —— **本棒唯一一处改既有签名**，理由见 D6 的代价列
src/ui/right_click.cpp                    修改 —— 计划里第三条条目；能力闸关 ⇒ 该条**不出现在 items**
                                                  （F17：未注入即无入口，而不是灰显）
src/ui/settings_i18n.cpp                  修改 —— 菜单两态文案、提示对话框两句、命令标题、能力闸旁注、
                                                  非法正则的回报错误句、**既有两条菜单文案换来源入表**
                                                  （`menu.item.copy` / `menu.item.paste`，见下条按实现改口）。
                                                  **按实现改口**：稿面那句「新键的标签与占位词条」只落了**标签**
                                                  一枚——本仓的 `SettingsControl` 没有 per-key placeholder
                                                  机制（`TextInput` 只在 `OptionalHexInput` 那一档挂「未配」），
                                                  加占位串要先加一档框架语义，不属本意图
src/ui/settings_panel.cpp                 修改 —— 组顶旁注表 `kGroupNotes` 追加一行把
                                                  `settings.note.session_log` 罩在 `connection.session_logging`
                                                  那一行上方（D1① 那道「两道开关」的理解成本的赔付处）。
                                                  **此行不在稿 §4 的原清单里**：判据 7 要求的那句旁注是
                                                  面板侧渲染的，而 §4 出稿时只列了 catalog 与词条两件
src/ui/terminal_view.{h,cpp}              修改 —— `menu_label` 补两态文案（且三条条目都改从词条表取串）、
                                                  `on_menu_command` 补一支；新增 `set_logging_hooks(...)`
                                                  （照 `set_restart_hook` 的「留空即不画」形态，F17）；
                                                  `on_right_click` **一次按下只呼一次状态钩子**——计划与
                                                  文案同源，分两次呼就是让次序与措辞来自两个时刻
src/ui/settings_catalog.cpp               修改 —— **三行**：`connection.session_logging` 改判 `Wired ∧ Immediate`、
                                                  `connection.session_log_dir` 改判 `Wired ∧ NextSession`、
                                                  新行 `connection.session_log_pattern` 以 `Wired ∧ NextSession`
                                                  入表（F8 的三处计数同批改：schema 键 63→64、`Absent` 21→19、
                                                  `Wired ∧ NextSession` 名单 +2；`session_logging` 那一行**不进**
                                                  名单，见判据 7）
src/main.cpp                              修改 —— 装配：`session.toggle_logging` 命令、`LogSpec` 的算路径
                                                  （目录回落 `default_config_dir()/borealis-logs`，F19；
                                                  D13 的名字形态；D8 的时间戳档；D10 的用户模式现读）、
                                                  **开启前** `compile_redactor` 一次 ⇒ nullopt 即 `alert` 回报
                                                  并**不建文件**（fail-closed）、`confirm` 风险提示（D7）、
                                                  能力闸的现读点。
                                                  **按实现补口**（五处，逐条理由见 §8 的「批 2/3 收账」）：
                                                  ① 一份 `logging_toggle_action` 闭包两处入口（菜单第三条经
                                                  `wire_view` 的两条钩子、命令面板经命令），不是两份实现；
                                                  ② 稿面那句「`perform_close` 撤账」**没落成一行**——日志态挂在
                                                  Session 自己身上，`Session::~Session` 在 join 读线程之后停笔并关文件
                                                  （那份顺序注释写在 `src/session/session.cpp` 的析构处：反过来会让
                                                  末段 flush 与在途 append 抢同一把锁），而标签关闭走的是 `tab_workspaces.erase`，
                                                  装配层没有第二份表可撤；
                                                  ③ 命令标题两态靠**同 id 整条重注册**（Aurora 侧无 `set_title`），
                                                  唯一刷新点在 `set_on_frame` 里比较一枚翻面旗标，故写失败自我
                                                  禁用／切标签／重启三种翻面都跟上，不必在每个建/换会话点补一次；
                                                  ④ `au::confirm` 只吃**一条** message（Aurora 侧事实），故稿 §5
                                                  那两句风险提示在装配里合成一行、完整路径拼在正文尾部（D7① 兑现）；
                                                  ⑤ 每帧对当前标签会话呼一次 `take_logging_notice()`，取到即
                                                  `AURORA_LOG_WARN` 留痕 + 弹一句无参提示（路径不进界面，AGENTS
                                                  第 14 条诊断不属中文例外）
src/CMakeLists.txt                        修改 —— `src/session/session_log.cpp` 进 `borealis_core` 的**显式**源
                                                  清单（只有 tests 走 GLOB；漏这一行即链接期 undefined reference，
                                                  Telnet 批 2 的同一处坑）
tests/unit/utest_session_log.cpp          新增 —— D3 折算表逐档（CRLF/LF/孤立 CR/BS/TAB/其余 C0/五类转义）、
                                                  F12 的整屏重绘样本、时间戳只在行边界插、内置词表命中与不命中、
                                                  **用户模式与内置词表的并集**（同一行两侧都命中 ⇒ 只替一次）、
                                                  **非法模式返回 nullopt**（F13 的 `(` 未闭合那一档）、
                                                  越限截断后前后段与时刻单调、目录回落与文件名形态
tests/integration/itest_session_logging.cpp 新增 —— 替身连接喂字节 ⇒ 真文件逐字节；取消提示 ⇒ 文件不存在；
                                                  能力闸关 ⇒ 无落点；关闭标签 ⇒ 末段已 flush 完整
tests/unit/utest_right_click.cpp           修改 —— `plan_right_click` 新入参的全部组合（能力关/开×态关/开×
                                                  三态右键 × 有无选区），条目次序
tests/integration/itest_right_click_paste.cpp 修改 —— 同上一条改口 + 「Paste/CopyOnSelect 两态仍不发菜单」
                                                  这条既有判据不受新条目影响
tests/unit/utest_settings_catalog.cpp      修改 —— F8 的三处计数与名单改口（63→64、Absent 21→19、名单 +2）
tests/unit/utest_config.cpp                修改 —— 新键的往返等值与「缺省空」两档；既有那条「默认关闭」证人
                                                  （`:599`）不动
```

> 不改既有公共头的可见性与既有成员顺序（AGENTS §4.1 第 6 条）；`plan_right_click` 的入参追加是**唯一**一处签名变化，其理由与用例改口在 D6 里已经写明。新键 `connection.session_log_pattern` 是 **D10 改判**吃下的六处同步（F8 那一整套），不是顺手扩面——它兑现的是原文那句「按可配正则」。框架侧不预登记新缺口：对话框、菜单、命令面板、凭据式文件创建、无头文件系统夹具全部有已落地的消费先例（F9/F17/F18）。

---

## 5 数据流与线程模型

```
远端字节 ──连接读线程──► Session::on_bytes ──► ingest(bytes, eos)            （F1 唯一入口）
                                                  │ 锁内：decoder_->feed → decode_buffer_
                                                  │        terminal_.feed(decode_buffer_)
                                                  │        **仅当本会话已挂日志**：把这一份码点拷进 log_pending_
                                                  │ 出锁：应答 flush、脏行入队、wake_frame
                                                  │
                                                  └─► SessionLog::append(log_pending_)   ← 读线程同步（D5①）
                                                        行缓冲 → 时间戳（判据 6）→ 打码（内置词表 ∪ 用户模式，D10）
                                                        → UTF-8 → ofstream(app)
                                                        越 8 MiB ⇒ 丢前半、后半前移（D9①）
                                                        写失败 ⇒ 自我禁用 + 上弦一次性提示（F7 同形）

UI 线程（右键 / 命令面板 / 设置面板）
   ├─ plan_right_click(action, has_selection, log_capability_and_state)      （F16/D6）
   ├─ MenuCommand::ToggleSessionLog ──钩子──► 装配层：读能力闸 → 读 session_log_pattern 并
   │        compile_redactor(...) 先编译一次（**开启前**，非装载期）⇒ nullopt 即 alert 回报且**不建文件**
   │        → au::confirm 风险提示（D7）→ 算路径（F19/D13）→ Session::enable_logging(LogSpec)
   └─ 设置页三行：Store.replace 落盘；核对表改判（§4）
```

- **顺序即文件内容**：所有写点都在该会话自己的读线程上，故分片顺序天然是到达顺序，不需要队列也不需要重排——这是 D5① 相对 F15 那两条丢弃型队列的全部优势。
- **锁的持有面**：`mutex_` 内只做一次码点拷贝（`std::u32string::assign`），文件 IO 全在出锁之后。这与 `pending_responses_` 的「锁内登记、出锁才写」是同一形态（F2）；日志关闭时这份拷贝不发生（一个 `bool` 早退），故既有会话的字节一寸不动。
- **唯一的主线程反例已被结构挡在外面**：F4 那条预览腿在主线程呼 `on_bytes`。本稿不打算「容忍」它，也不给它加线程判断——预览会话由 `SettingsPreview` 自己构造，装配层从不给它 `LogSpec`，而 F17 那条「钩子未注入即无入口」保证预览视口里连菜单项都没有。这一条要写成注释钉在挂钩处，否则后人加「预览也开日志」时会静默在主线程写盘。
- **慢盘的形态**：D5① 明确接受的代价——磁盘越慢，该会话的输入到达越慢（读线程被自己的写点拖住），而 UI 线程不受影响。本沙箱 F14 的读数只覆盖 NVMe 本地盘，NFS/慢盘属**推断／未验证**，走查第 ⑸ 条实测后回写。
- **正则的编译点只有一次，且在开启那一刻**：`compile_redactor` 把内置词表与用户模式并成**一个** `std::regex`（供替选并联，不是逐行跑两遍），在装配层呼 `enable_logging` **之前**编译一次、随 `LogSpec` 移交给该会话的 `SessionLog`。故读线程每行只做一次 `regex_search`（F13：万行 13 ms），装载期与设置页输入框里都不编译——用户在表单里逐字符敲的模式不是要执行的东西。非法模式 ⇒ 返回 `nullopt` ⇒ 装配层 `alert` 回报并**不建文件、不改开关态**（D10 连带的 fail-closed 档）。
- **关闭与撤账**：`Session` 析构即关文件（`ofstream` 随成员析构 flush）；标签关闭走既有 `perform_close` 的撤账纪律；重启会话＝新 `Session` 对象＝新文件且回到未记录态（D11① 的自然结果，与一次性提示的 latch 同档口径）。

---

## 6 验收判据映射

- **按会话开关**：`itest_session_logging` 锁「A 会话开、B 会话不开 ⇒ 只有 A 有文件」（`only_the_session_the_user_turned_on_writes_a_file`）；`utest_right_click` 锁能力闸与开关态的四组组合下条目在不在、次序为何；`itest_right_click_paste` 锁「`Paste`/`CopyOnSelect` 两态仍不发菜单」（既有判据不因新条目而变，`direct_right_click_states_stay_menuless_with_logging_wired`）。**按实现改口**：这一条后半句「命令面板那条是这两态的唯一入口」**没有自动化证人**——命令注册在 `src/main.cpp`（裁决 7.83：该 TU 不编入 runner），有证人的只是「这两态拿不到菜单」那半段，见 §8.1。
- **纯文本剥离 SGR 但保留换行结构**：`utest_session_log` 逐档锁 D3 的表；现场证人用 F10 那份彩色 `ls` 的真实捕获字节（夹具化进 `tests/fixtures/`，AGENTS 第 20 条「转义序列以回放夹具驱动」）——断「可打印率 ≈ 1、目录列表逐行成行、`ESC[0m` 一类一个字节都不留」。
- **整屏重绘的语义**（F12）：同件锁「40 次清屏归位样本 ⇒ 43 行」，把这条**不是缺陷的形态**钉在用例名里，防止后人当 bug 修。
- **时间戳前缀**：`utest_session_log` 锁「一条分片含 3.5 行 ⇒ 每行一条前缀、分片边界不产生前缀」，以及「CRLF 折一条 ⇒ 只有一条时间戳」。
- **环形覆盖**：`utest_session_log` 用**小上限**（测试入参覆盖，非常量）锁「越限后前半消失、后半保留、后续字节接在上文之后」与「不越限时逐字节不变」。
- **敏感过滤（内置词表）**：`utest_session_log` 锁命中词表的行整行替为 `[redacted]`、大小写与 CJK 混排行都命中（F13 的第一条读数）、不含关键词的行**逐字节不变**（打码不是改写）。
- **敏感过滤（用户可配正则，D10 改判）**：同件另锁三条——① 用户模式**独立**命中而内置词表不命中的行也被打码（证明并集不是装饰）；② 同一行两侧都命中 ⇒ 只替**一次** `[redacted]`，不叠字；③ 非法模式（F13 的 `(password|token` 那一档）⇒ `compile_redactor` 返回 `nullopt`（`illegal_user_pattern_refuses_to_compile`）。**按实现改口**：稿面那句「`itest_session_logging` 锁『nullopt 这条路走下来**文件不存在**、开关态不变』」只落了**相邻的一档**——有证人的是 `a_refused_open_leaves_the_session_unlogged_and_creates_no_file`（打开被拒 ⇒ 不落记录态、不建文件），而「装配层拿到 nullopt 之后不弹 confirm、不建文件、不改态」那半段在 `src/main.cpp`，**无自动化证人**（§8.1）。缺省空模式的「只用内置词表」另锁一条（`empty_user_pattern_keeps_the_builtin_vocabulary_only`），防并集实现把空模式折成 `()` 之类匹配一切的东西。
- **默认关闭 + 风险提示**：默认关这条的既有证人 `tests/unit/utest_config.cpp` 那条「默认关闭」不动；`itest_session_logging` 锁「从未开启过的会话一个文件都不建」（`a_session_that_never_enabled_logging_creates_nothing`）与「打开被拒 ⇒ 不落记录态、不建文件」（`a_refused_open_leaves_the_session_unlogged_and_creates_no_file`）。**按实现改口**：稿面那两句「取消提示 ⇒ 路径上不存在文件（含不创建零字节文件）」与「确认 ⇒ 文件即建」都落在 `au::confirm` 的答话回调那一侧，即 `src/main.cpp` ⇒ **无自动化证人**，兜底只有编译链接与走查第 ⑵/⑶ 条；本棒能保证的是**顺序**——目录与文件名都在提示之后、`enable_logging` 之前才动手（取消 ⇒ 目录与文件都不出现，见 §8.1 那条 `TODO`）。提示文案里含完整路径（`au::confirm` 只吃一条 message，两句合成一行）。
- **0600 与不覆盖**：`itest_session_logging` 锁首建走 `create_private_file` 的返回档（`Created` vs `AlreadyExists` ⇒ append 而非 truncate）。**POSIX-only**；Win 腿的权限语义按 F9 标推断，走查第 ⑷ 条实测。
- **测试读数口径**：交付时以 `ctest --preset linux -E etest_` 的**项数**与 `borealis_test_runner` 的**全案数**结项（AGENTS 第 21 条不认「应该更快了」，本棒同样不认「测了很多」）；结项读数 **81 项／1019 案全绿**，变异注入按第 22 条逐则给「注入点 ⇒ 哪条用例转红」，全表见 §8.1；装配那**三处**无证人（裁决 7.83）与一处未裁窗口按 Telnet 批 3 同档如实登记。
- **真机走查**（AGENTS 第 33 条，未走查不得称「可用」）：⑴ 真 SSH/本地 shell 下开日志的观感，`ls --color` 的产物在常见编辑器里是不是干净文本；⑵ 开 `top`/`vim` 之后日志里那一堆重绘行**读起来是否可解释**（F12/判据 6 那句话够不够）；⑶ 口令提示与打码的实际效果（`ssh` 口令、`passwd` 交互各试一次），并**在设置里填一条自己的正则**（含一条非法的）看回报与生效面（D10 改判的新增走查面）；⑷ Win32 腿：文件权限、路径分隔、`borealis-logs` 的落点与编辑器关联；⑸ 大输出（`cat` 一个几百 MB 文件）时的手感与输入迟滞（D5① 的代价面）；⑹ 两道开关（能力闸 + 逐会话）点起来的直觉，以及旁注那句话读不读得懂（D1 的代价面）。

---

## 7 本期不做（延后 / 在册）

- **带属性的原生存式（自定格式）**：需求原文那一腿本期只落纯文本档（D4①）。延后的真实理由不是「难」，而是**没有消费方**：写出来的东西需要一个读法，否则违反 `right_click.h:36-40` 那条「不留点了什么也不发生的空位」的纪律。
- **多条用户模式（字符串数组）与设置页的「试打一把文本」预览**：本期只落**一枚** `connection.session_log_pattern`（单条模式，与内置词表取并集）。「一组模式」要新键形态（数组装载与往返守卫）、「即时预览」要新交互面，两者本期都没有判据可验收 ⇒ 延后。
  **同时登记 D10 改判吃下的代价**：本棒**多了一枚 schema 键**——F8 量过的那一整套六处同步（schema / `store.cpp` 读写两处 / `form_transfer` / 核对表行 / 词条）加 `utest_settings_catalog` 三处计数改口（63→64、`Absent` 21→19、`Wired ∧ NextSession` 名单 +2）。稿面原推荐档（只落内置词表）本可避开这笔账，人裁改判即接过它，并换来原文那句「按可配正则」本期落地。
- **逐会话开关的记忆、上限的可配、目录级配额与清理**：D9/D11/D8 三条的同一族延后。目录里的旧日志**不清理不轮转**（本期只管单文件不越限），长期堆积由用户自清——这条要在风险提示的旁注里可见，不能静默。
- **真机走查全部六条**（§6 末）：未做 ⇒ **本棒结项不称「可用」**（AGENTS 第 33 条）。
- **装配三处无自动化证人**（`src/main.cpp` 的算路径、能力闸现读、**开启前的正则编译与拒绝档**，裁决 7.83 同档）：登记为在册缺口，与 Telnet 批 3 那三处分流同档处理。其中「非法模式 ⇒ 不建文件」这半段在 `compile_redactor` 侧有证人（utest 锁 nullopt），无证人的是装配层拿到 nullopt 之后**不弹 confirm、不建文件、不改态**那条分支。
- **Win32/MSVC 腿的一切说法**（0600 的实际生效、`borealis-logs` 的落点、编辑器打开 .log 的形态）：本沙箱无 MSVC，标**推断／未验证**（AGENTS 第 11 条）。
- **日志的搜索/回放界面**：属新需求面，本稿不预建，也不写进 SPECIFICATIONS。

---

## 8 探针复现与裁决记录

探针都是一次性脚本，落在 `$TMPDIR`（本沙箱 `/home/polaris/tmpbuild/probe/`），**不入仓**。

- **F10/F11/F12（真实字节流）**：`script -q -e -c 'ls --color=auto /etc; …' raw.txt` 取 PTY 侧捕获；同一份循环体分别写直连管道与 PTY，用 Python 逐字节数 `ESC`/`CR`/`LF`/`BS` 并按「丢 OSC → 丢 CSI → 丢单字符转义 → CRLF/CR 折 LF」的顺序做朴素剥离，打印可打印率与前 400 字节。整屏重绘样本是 40 次 `\033[H\033[2J` + 一行文本。
- **F13（正则形态与成本）**：`g++ -std=c++20 -O1` 的小程序，四段分别是「含 CJK 的 UTF-8 `std::string` 上 icase 供替选是否命中」「非法模式抛什么（`regex_error::code()` 与 `what()`）」「10000 行 430 KB 全量 `regex_search` 的耗时」「`.` 是否跨 `\r`」。
- **F14（同步写成本）**：`std::ofstream(app)` 写 4 KiB 分片 × 2048，分别测「不逐片 flush」与「逐片 flush」的每片耗时均值与最差值；`std::chrono::steady_clock` 计时。
- **F5/F3/F7/F16/F17（代码事实）**：直接读源码确认——网格头文件无 `wrap`/`soft` 字样、`Session` 成员表无身份字段、`take_unrepresentable_notice` 的一次性 latch、`MenuCommand` 头注里点名的待入列项、`ask_multiline_warning` 与 `set_restart_hook` 两处「无宿主/留空即不动作」。
- **待补**：慢盘/NFS 的写成本（D5① 的外推边界）、Win32 侧 `create_private_file` 的权限语义（F9 只 WARN）——两者都要真机，走查第 ⑸/⑷ 条。

**裁决记录（2026-10-12 收口）**：D1–D13 经人逐条裁决——**D1–D9、D11–D13 全按稿面推荐档**（无一条改判），**D10 改判「内置固定词表 + 用户可配追加正则」**（原推荐是只落内置词表、可配延后），并连带裁下**非法模式 ⇒ 拒绝开启并回报错误**的 fail-closed 档。据此入册 `codespec/SPECIFICATIONS.md` §7 裁决 **7.116**、`CHANGELOG.md` **v0.116**，`PLAN.md` §3 的 `CONN.11` 行与 §8 的 M4 行同步改口。本稿的七处回写见状态块第二条。实现按 §4 布局分三批走：批 1＝折算件 + 落盘件 + `Session` 挂钩与 utest/itest（不含 UI）；批 2＝菜单项 + 命令 + 提示对话框 + **新键六处同步** + 装配 + 核对表三行改口；批 3＝文档回写与在册收口。


### 8.1 批 2／批 3 收账（2026-10-12，落地期按实现回写；入册为裁决 **7.117** 与 **v0.117**）

**三批落点与读数**。批 1 = `5716ac7`：新增两件测试 TU（`utest_session_log` 20 案、`itest_session_logging` 10 案），`ctest --preset linux -E etest_` 79→**81 项**、`borealis_test_runner` 980→**1010 案**。批 2 = `f9bc491`：**不新增 TU** ⇒ 项数不变，全案数 1010→**1019**（`utest_right_click` 8→11、`itest_right_click_paste` 21→25、`utest_session_log` 20→22——目录回落与文件名形态那两案随 §4 第⑤条的拆件补上）。批 3 = 本节 + `SPECIFICATIONS.md` 的 7.117 与 CONN.11 现状句 + `CHANGELOG.md` v0.117 + `PLAN.md` 两行。结项复核：**81 项全绿、1019 案全绿**。

**变异注入逐条（注入点 ⇒ 转红的用例名；脚本 `$TMPDIR/mutate2.py` 与 `mutate3.py`，一次性、不入仓，AGENTS §4.4 第 22 条）**。

| 把实现改坏的那一句 | 套件 | 转红 |
| --- | --- | --- |
| `on_menu_command` 的 `ToggleSessionLog` 那支不回调装配钩子（`if (false)`） | `itest_right_click_paste` | 2：`the_logging_row_lands_below_copy_and_paste`、`the_logging_row_reads_the_start_wording_then_the_stop_wording` |
| 钩子缺失也放条目（`logging_menu_state` 返回 `available = true`） | `itest_right_click_paste` | 2：`menu_paste_item_reads_the_clipboard_at_the_next_frame`、`the_unwired_view_reads_nothing_about_logging` |
| `on_right_click` 呼状态钩子两遍 | `itest_right_click_paste` | 1：`the_logging_row_lands_below_copy_and_paste`（`log_state_reads` 从 1 变 2） |
| 计划里日志条目插在粘贴之前 | `itest_right_click_paste` | 1：`the_logging_row_reads_the_start_wording_then_the_stop_wording` |
| `plan_right_click` 恒 `if (true)`（能力闸那条判据摘掉） | `utest_right_click` | 3：`context_menu_with_selection_offers_both_commands_enabled`、`context_menu_without_selection_grays_out_only_the_copy_row`、`the_log_row_is_absent_when_the_gate_is_closed_not_grayed_out` |
| `menu_label` 两态折成一态（`logging ? stop : start` 只留 start） | `itest_right_click_paste` | 1：`the_logging_row_reads_the_start_wording_then_the_stop_wording` |
| `resolve_log_directory` 不拼子目录名（直接回 config_dir） | `utest_session_log` | 1：`an_empty_configured_directory_falls_back_to_the_named_child_of_config_dir` |
| `make_log_file_name` 丢了起始秒 | `utest_session_log` | 1：`the_log_file_name_carries_tab_id_and_start_second_only` |
| `store.cpp` 的 `put(node, "session_log_pattern", …)` 注释掉 | `utest_config` | 6：`every_value_survives_a_write_read_round_trip`、`an_exported_document_imports_back_the_same_settings`、`rollback_restores_a_saved_scene_and_keeps_the_current_one` 等 |
| `form_transfer.cpp` 的 `Binding` 键名改一个字符 | `utest_form_transfer` | 4：`the_bridge_loads_every_catalog_key_and_nothing_else`、`each_key_takes_the_member_it_names`、`writing_the_form_back_rebuilds_that_settings`、`only_the_committed_key_moves` |
| 核对表那一行的键名改一个字符 | `utest_settings_catalog` | 2：`table_covers_exactly_the_written_schema_keys`、`grade_columns_match_the_two_physical_boundaries` |
| `kGroupNotes` 里 `settings.note.session_log` 那一行删除 | `itest_settings_panel` | 1：`the_deferred_status_bar_toggles_are_real_controls_and_only_persist` |

批 1 那九处变异的点位与用例名随该批记在提交 `5716ac7` 的正文里（D3 折算表逐档、越限丢前半、0600 首建、写失败自我禁用、时间戳只在行边界、内置词表命中与不命中、并集只替一次、非法模式 nullopt、末段 flush）。

**一处实测教训，值得留在稿里**。变异脚本还原时若用 `shutil.move(backup, path)`，它**保留备份的旧 mtime** ⇒ ninja 判定「无需重建」⇒ 下一枚变异跑的仍是上一枚变异编译出的对象文件。第一版电池因此有两处读数虚高（「计划里日志条目排在粘贴之前」与「组顶旁注漏挂」各报 2 条，修复后都是 1 条），全量回归也一度停在 17 案转红的假象上。修复＝还原后 `os.utime(path, None)` 把时间戳钉到「现在」，末尾再统一重建一次；上表是**修复后那一版**的读数，跑完 `git diff -- src include` 为空、回归恢复 81 项／1019 案全绿。

**五处口径按实现改口**（稿面 ≠ 落地，逐条已同步进 §4）：① `LogSpec` 移交的是 `compile_redactor` 的产物（`redactor`）而不是模式串——编译点必须落在 UI 线程的「开启那一刻」（7.116⑤），读线程拿到的已是内置与用户并好的一个供替选；② `session.h` 的运行期入口从三枚变**四枚**，多出的 `logging_enabled()` 是菜单条目与命令标题两态的唯一现读点，这份态除会话自己没人知道；③ 稿面那句「新键的标签与**占位词条**」只落了标签——本仓的 `SettingsControl` 没有 per-key placeholder 机制（`TextInput` 只在 `OptionalHexInput` 那一档挂「未配」），加占位串要先加一档框架语义，不属本意图；④ 组顶旁注落在 `src/ui/settings_panel.cpp` 的 `kGroupNotes`，**这一件不在 §4 的原清单里**（判据 7 那句旁注是面板侧渲染的，出稿时只列了 catalog 与词条两件）；⑤ §4 那句「装配层算路径」拆成 `resolve_log_directory` 与 `make_log_file_name` 两枚**纯派生件**放进 `session_log.h`——否则 D9① 的目录回落与 D13① 的名字形态没有 utest 证人（§4.1 第 20 条），装配层只调用。另两处结构性改口：那句「`perform_close` 撤账」**没落成一行**——日志态挂在 `Session` 自己身上，`~Session` 在 join 读线程之后停笔并关文件（`session.cpp` 的头注写明顺序反过来会让末段 flush 与在途 append 抢同一把锁），装配层没有第二份表可撤；命令标题两态靠**同 id 整条覆盖重注册 + 帧边界唯一刷新点**（Aurora 侧 `CommandRegistry::add` 同 id 保持注册序、**没有 `set_title`**），于是写失败自我禁用／切标签／重启会话（D11①）三种翻面都从这一条腿跟上，不必在每个建/换会话点补一次。风险提示那两句因 `au::confirm` 只吃**一条** message 而合成一行、完整路径拼在正文尾部（D7① 的兑现形态）。

**仍然没有自动化证人的三处 + 一处未裁窗口**。`src/main.cpp` 不编入 runner（裁决 7.83 的物理事实）⇒ ①算路径、②能力闸现读、③**正则非法 ⇒ 不弹 confirm／不建文件／不改态**那一条分支（其前半段在 `compile_redactor` 侧有 utest 锁 nullopt，后半段没有）只有编译链接与真机走查兜底。另登记一处**未裁窗口**：`TODO(SPEC.FEAT.CONN.11)` 写在 confirm 的答话回调里——目录与文件名在提示**之前**算定，答话那一刻不重读配置，「对话框开着时改了目录」这一窗口的口径未裁，本棒不自行择一。

**六条真机走查（§6 末）一条未做** ⇒ 本稿与 §3 的十三条裁决、以及上面所有读数一律**不称「可用」**（AGENTS §4.4 第 33 条）。§7 的在册清单在本批之后不变。
