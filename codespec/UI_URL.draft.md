# URL 检测与打开设计稿（`SPEC.FEAT.INTERACT.05`）

> **状态**：**评审收口（2026-10-12）**——§3 的 D1–D13 按稿面**推荐档逐条自决收口**（用户离场，常存授权「后续再遇到问题则你自行按照推荐去做」，AGENTS §4.1 第 2 条的那句「等开工指令」已由该授权替换为「按推荐自决 + 入册」）。每条推荐都带着 §0 的一手读数与代价，人复核时若要改判，按 §4.2 第 11 条回写裁决即可，本稿不改已收口的编号。裁决入 `codespec/SPECIFICATIONS.md` §7 为 **7.119**，版本记 `CHANGELOG.md` **v0.119**。
> **配套高保真草图**：`codespec/UI_URL.draft.svg`（矢量源）/ `codespec/UI_URL.draft.png`（1× 出图）。屏 A–D 的圈号 1–8 即 §2 的判据标号；SVG 为事实来源，改图直接改 SVG。
> **与既有视觉稿的关系**：`codespec/UI_COMMANDS.draft.svg` 屏 4 里那张「链接打开确认」**已评审在册**（标题「打开链接？」、正文报 URI 全文、副行报识别来源、注明「协议白名单：http / https；信任级别两档分开配置」「确认策略：首次确认并记住同域（默认）」，动作为「仅本次打开 / 记住该域并打开」）。本稿不另立一套外观：§2 判据 4/5 就是把那张稿面**落到控件与代码**，草图屏 B 与之逐字对齐。同理，那张稿的九项右键菜单（复制 / 粘贴 / 全选 / 打开链接 / 分屏 / 重命名 / 广播组 / 会话日志 / 关闭）是**终态图**，本棒只让「打开链接」这一条到货，余条仍不出现（D12 的次序因此是「复制 → 粘贴 → 打开链接 → 会话日志」，与本棒实际入列的四条一致）。
> **上游依据**：`SPEC.FEAT.INTERACT.05` 原文（`codespec/SPECIFICATIONS.md` §交互域，一句话三件事：视区内 URL 识别含 OSC 8、Ctrl/Cmd+点击或右键「打开链接」、打开前确认可配 + 协议白名单 + 两档信任级别分别可配）、`codespec/ARCHITECTURE.md` §10.4（同一句话的架构侧登记）与 §4.1 第 145 行那段超链接侧表形态、裁决 **7.21②**（标识永不复用、满表淘汰最旧、悬在已淘汰标识上的格子按「不可点」处理）、裁决 **7.77①②④**（上报档优先于本地选区、Shift 让位、同格不重报）、裁决 **7.38⑥**（右键取 `Paste`/`CopyOnSelect` 两态**不发菜单**）、裁决 **7.116 D6**（菜单两入口一闭包 + `plan_right_click` 第一处签名改口）、裁决 **7.118 D13**（app 级闸门 + 观察指针注入 + 「测试替身可以只答一次」的接缝形态）。
> **既成地基（本棒只消费，不新建）**：OSC 8 的**来源腿全链已落**且**UI 侧零消费**——状态机 `term::Terminal::apply_hyperlink`（`src/term/terminal.cpp:827`，有界表 `kMaxHyperlinks = 1024`、`8;;` 结束区间）→ 网格侧表 `grid::Row::set_hyperlink/hyperlink`（`include/borealis/grid/row.h:141-161`）→ 取目标的公开入口 `Session::hyperlink_target`（`include/borealis/session/session.h:205`）。第三条**至今没有消费方**：调用者只有 `tests/unit/utest_terminal_osc.cpp:72` 与 `tests/integration/utest_session` 那条 `:409`，`src/ui/` 全文 grep `hyperlink` **零命中**（F1）。本棒就是把这枚「有预埋、无消费方」补成消费方，不新增任何解析、网格或会话侧的结构。

---

## 0 地基实测（2026-10-12 本沙箱，POSIX 腿 + C++/Python 探针 + 真机 bash 读数；Windows 腿一律标未验证）

形态不是推演出来的。F1–F22 每条都有本沙箱读数；探针是一次性脚本，不入仓（复现口径见 §8）。

| # | 读数 | 对本稿的约束 |
|---|---|---|
| **F1** | **OSC 8 有来源无消费**：`src/ui/` 目录全文 `grep -n hyperlink` **No matches**；`hyperlink_target` 的命中只有 `src/term/terminal.cpp:907`、`src/session/session.cpp:202-204`、两个头文件与三条用例（`tests/unit/utest_terminal_osc.cpp:72,142,196,327`、`tests/integration/utest_session.cpp:409`） | 本棒的缺口精确到「**取目标这件事没人做**」，而不是「OSC 8 没解析」。判据 1 的下划线、判据 3 的目标条、判据 6 的菜单条目全部落在这一条接缝上，不需要动 `term`/`grid`/`session` 任何一处 |
| **F2** | 网格侧表的**列粒度**形态：一列至多一条标识（`row.h:141` 的 `set_hyperlink` 同列覆盖），**写入即清链**（`row.h:76-81`：`set()` 一并 `clear_combining` + `clear_hyperlink`），双宽字符**只挂前半格**（`ARCHITECTURE.md` §4.1），`kNoHyperlink = 0` 不占表项（`row.h:36`） | 「一条链接的列区间」在网格里**不是一份区间表，而是同标识的连续游程**；扫描件必须以「游程」为产物，且**双宽字符的延续格没有标识**——按列直取会把一条 URL 在任意一个全角字符处切断（判据 2 的第二条约束由此来） |
| **F3** | 副本带着侧表：`session::ScreenMirror::take_copy` 走**整行拷贝**（`src/session/screen_mirror.cpp:24`），`grid::Row` 的 `hyperlinks_` 是普通成员，随拷贝走。视口侧已有「存储行号 → 副本行」的换算件 `mirror_line_of(storage_row)`（`src/ui/terminal_view.cpp:1270-1276`），滚出可见区回 `nullptr` | 悬停与点击取行**不必进会话锁**（§4.5 第 25 条零风险），且「回看态里滚上去的旧行照样可点」这条不需要新代码——`mirror_line_of` 给不给 `nullptr` 就是唯一判据。链接因此**天然只覆盖可见区**，不做全 scrollback 的链接索引（D4 的一半根据） |
| **F4** | **缺省断词符集把 URL 骨架全断了**：`config` 侧缺省值 `terminal.word_delimiters = "!\"#$%&'()*+,-./:;<=>?@[\\]^_\`{|}~"`（`include/borealis/config/settings.h:170`）——`/ : ? & = # % + , ; @ - .` 逐个在内；这枚键**用户可改且原样收字符**（`SPEC.FEAT.TERM.06` 一族，判据 `codespec/UI_SETTINGS.draft.md` §3 B2-b：不做任何转义） | 「双击选中的词」与「这条 URL」在缺省配置下**必然不同**（`https://a/b?c=1` 双击只到 `https:`）。更硬的一条：这枚键是可配置的，故任何「复用断词符」的做法都会让**改断词符静默改变 URL 的边界**（D3 否掉②③的根据） |
| **F5** | 取词件公开、取行件不公开：`ui::word_span_at(const grid::Row&, GridCellPos, std::string_view delimiters)`（`include/borealis/ui/selection.h:101`）是公共件，其文档明写「空格与制表**恒**为断点而不必列入」「双宽延续格按基础格判定」；而按行取文本的 `row_text` 至今是 `src/ui/selection.cpp:44-76` 的**文件私有**函数（同 `codespec/UI_SESSION_LOG.draft.md` F5） | 链接扫描件的入参形态只能自己定：**吃 `const grid::Row&` 直读 cell**（`row.cell(col).code_point`，`include/borealis/grid/cell.h:63`），不借 `row_text`（它不导出，为它开公共 API 是为本棒之外的消费方做嫁衣），也不借 `word_span_at`（F4）。产物复用既有的 `ui::RowSpan{row, first_column, last_column}`（`selection.h:61-68`）而不是新造区间类型 |
| **F6** | 指针归属的**唯一分流判据**已在位且本棒可直接复用：`takes_pointer_by_report(e, level)`（`src/ui/terminal_view.cpp:819-836`）——非左键恒归本地（`:821` 那句「右键归本地三态」）、有在途按下整笔跟随（`:824`）、`Off` 或带 **Shift** 归本地（`:827`）、无按下的 Move **只在 `?1003`（`AnyEvents`）档归上报**（`:835`）。上报腿在 `report_pointer` 里无条件 `e.is_handled = true`（`:843`）；本地腿的 Move 在 `!dragging_` 时直接 `return` 且**不认领**（`:696-703`，注释就写着「悬停移动不改选区」） | 两条结论同时成立：①「无键悬停」事件在**除 `?1003` 之外**的每一档都落到本地且无人认领 ⇒ 本棒的悬停落点就在那条 `return` 之前，不新增事件订阅；② `?1003` 档下悬停线**拿不到**（事件被上报腿认领），这是 D7 的代价而不是本棒的漏判，且 Shift 让位档同样拿不到（那一次点击的归属已整笔让给本地，但 Move 在 `:835` 那一条上因 `level != AnyEvents` 而归本地——两档要分开断言，见 §6 判据 ⑦） |
| **F7** | Ctrl 位在鼠标事件上**可达**（框架缺口 G18 已关闭）：`aurora::MouseEvent.modifiers`（Aurora `include/aurora/event/event.h:150`）配 `ModifierKey{None, Shift, Control = 1U<<1U, Alt, Meta, NumLock}`（`:46-55`）；本仓上报腿已经在读它（`terminal_view.cpp:844` 的 `const bool control = (e.modifiers & aurora::ModifierKey::Control) != 0U;`） | 需求那句「Ctrl/Cmd+点击」**不需要任何框架改动**，也不需要在应用侧自造修饰态查询（G18 那族的「先怀疑证人自己的执行环境」教训不重演）。同一条读数的另一面：`?1000`/`?1002` 档下 Ctrl+左击**归上报**（F6），届时那个 Ctrl 位会被编进 SGR 字节送给远端，本棒不抢（D7①） |
| **F8** | 装饰落笔的现成件与它的上色口径：`ui::decoration_rects(geometry, row, run)`（`src/ui/cell_layout.cpp:200-241`）产出下划线三档 + 删除线的矩形，线宽**恒 1 物理像素**且四边吸附物理像素边界，单线落点 `baseline + 1px`、双线 `+1/+3`、波浪以 4 物理像素为周期且**相位锚在不含内边距的网格绝对像素 x**；绘制侧第④层逐 run 落笔且用色 `run.paint.foreground`（`terminal_view.cpp:979-983`） | 悬停线要么并进 `run.paint.underline`（`include/borealis/ui/palette.h:81`，由 `src/ui/palette.cpp:143` 从 cell 折算而来），要么另起一层。并进去的**两处代价**是确定的：run 按样式全等切分（裁决 7.23②，`layout_row` 会把一条 URL 的边界再切一刀）与**线色跟前景走**（拿不到 accent）。另有本仓先例可走第二条：标签条的活动下划线就是 `p.fill_rect(underline, accent)`（`src/ui/tab_bar.cpp:172-175`） |
| **F9** | 非模态自绘卡的**完整先例**：`TerminalView::paint_unrepresentable_notice`（`src/ui/terminal_view.cpp:1222-1268`）——卡尺寸取常量（`kNoticeCardHeightDp` 等）、取色走 `settings_chrome()`、文案走 `settings_label(key, args)`、**让位不画**（纵向装不下「卡高 + 内缩」时整张不画，见 `:1231`）、以及那三条「**非模态三不**」：不铺覆盖整可见区的半透明带、不吃焦点、**不写任何命中盒**（`:1239-1240` 注释原文），故指针入口看不见它、卡下旧内容照常可读可选 | 目标 URI 提示条照这一族形态落，位置改**左下角**（业界同形：悬停目标的预览条在左下，不与居中的重连卡抢位）。三不是本条的承重部分：一条跟手走的提示**绝不能吃掉点击**，否则第一次移动鼠标就把选区起点挪到了提示条上。`Rect` 盒因此不写进 `*_button_box_` 那组成员 |
| **F10** | 框架 tooltip 是**半成品**：`Modifier::tooltip(text, delay_ms = 500)`（Aurora `include/aurora/modifier/modifier.h:368-372`）、`tick_tooltip`/`tooltip_hover_start`/`tooltip_hover_end`/`active_tooltip`（`:517-528`）都在，`Widget::tick_gestures` 也确实每帧呼 `tick_tooltip`（`include/aurora/widget/widget.h:1401-1404`，开启位见 `src/aurora/widget/widget.cpp:1035-1038`）。但全仓（`src/aurora` + `include/aurora`）**没有 `tooltip_hover_start` / `tooltip_hover_end` 的调用方**，也**没有任何把 `active_tooltip()` 文本画出来的地方**——`tooltip` 字样在框架实现里只有 `modifier.cpp:187-224` 的节点存取、`widget.cpp` 那两行注释，外加 `atspi_protocol.cpp:159` 的属性名 `"has-tooltip"` | 「显示悬停目标」这条**不能**交给框架 tooltip：计时有人推，起手的 hover 事件没人喂，气泡没人画。按 AGENTS §5.1 那是**框架缺口**（事件链路类），本棒既不等待也不自造替代判定，而是用 F9 那条已在本仓落地的自绘形态（应用侧按公共 API 组合控件不算私改，§5.3）。缺口本身登记进 §8 在册，派不派发由人定 |
| **F11** | 状态栏件**未开工**：`appearance.status_bar.*` 十枚键全在册（`src/config/form_transfer.cpp:223-260`、`src/ui/settings_catalog.cpp:165-176`、`src/ui/settings_i18n.cpp:450-459`），而面板旁注那句写得很直白：「**入口先于消费方落地：关闭状态栏件尚未开工**，本组改动只落盘」（`src/ui/settings_i18n.cpp:259`，其 `ConsumerStatus` 即 `Absent`） | 目标 URI 无处可进状态栏。D6 因此必须在「视口自绘」与「不显示」之间选，而选「不显示」就把 F1 那条安全含义（屏上文字与真实目标可以不一致）藏起来了。状态栏落地那一棒要重新裁这个位置（§8 在册一条将来改判） |
| **F12** | 模态确认的现成件**只有两按钮**：`au::confirm(title, message, on_result)` 产出 Yes/No 两枚（Aurora `include/aurora/widget/dialog.h:235-259`）、`au::alert` 一枚 OK（`:211-228`），`Dialog::set_content` 是**替换**语义（`:115-119`）且**遮罩点击不触发 `on_close`**（`:107` 那句「模态对话框的遮罩只吸收点击，不等于取消」）。三按钮 + 自定义排布的形态本仓已有先例：`src/ui/credential_prompt.cpp`（自组 `Column` + 三枚 `std::shared_ptr<aurora::Button>` + `settings_label` 取文案 + `host_.add_overlay` 记 `overlay_index_`，`:70-131`） | 「仅本次打开 / 记住该域并打开 / 取消」**不是**框架能一键给的，得照 `credential_prompt.cpp` 自组。而「遮罩点击不关闭」这条实测在本需求里是**安全侧**的：用户点空白处不会误开浏览器。它同时意味着「一直不答」是一档常态（与裁决 7.118 D5②那一条同族），故本棒的账本必须允许「问了没答 ⇒ 什么都不打开」而不留悬挂态（§6 判据 ⑥） |
| **F13** | **双 fork 派生不留尸**（一手探针 `/home/polaris/tmpbuild/probe_spawn`，g++ -std=c++20 -O0）：中间父 `fork` 出孙后立刻 `_exit(0)`，父只 `waitpid(中间父)` ⇒ 200 次调用后 `waitpid(-1, WNOHANG)` 收 **0** 条；单次成本 **176.97 µs**（`/bin/true`）、exec 失败档（不存在的绝对路径）**390 µs**。对照组：单 fork 不 wait，200 ms 后 `ps -o pid,stat` 显示 **`Z`**（僵尸）。全仓的 `waitpid` **只呼具名 `child_pid_`**（`src/platform/posix/forkpty_connection.cpp:398,405,407,420`），无一处 `-1` 通配回收 | 「spawn 一条外部程序」在本仓**必须自带收尸方案**，且没有别的腿会顺手收掉它（那条隐性耦合不存在，省下一次跨腿沟通）。双 fork 是**实测可用且成本在 UI 回调可接受量级**的形态：177 µs 与一次帧预算 1.26 ms 比是 14%，且它不等子进程、不等网络、不等浏览器 ⇒ 不是 §4.5 第 25 条禁的那类阻塞 IO。代价与 §5 第 4 条绑定：**双 fork 拿不到退出码**，故本棒不承诺「打开成功」 |
| **F14** | **exec 族不经 shell ⇒ 注入面天然封住**：同一探针第 5 步 `execvp("/bin/true", {"/bin/true", "; touch /home/polaris/tmpbuild/INJECTED"})` 之后标记文件**未生成**（`access() == ENOENT`，打印 `injection marker exists: NO`） | 「URI 是不可信输入」这条在本仓有 F1 那一族的共同前提（会话字节流）。守住注入的形态是**结构化 argv**，不是转义：绝不 `system()`、绝不拼命令行、绝不 `sh -c`。D10 因此否掉③（`system("xdg-open '<uri>'")`——单引号里的 URI 自带单引号即破防，而 `%27` 一类编码本来就可能出现在真 URL 里） |
| **F15** | 本机 `xdg-open` 可用且**真的会开窗口**：版本 1.2.1；`timeout 8 xdg-open "https://example.com"` **rc = 0**，strace 里见到 `execve("/opt/microsoft/msedge/msedge", …)`（`/home/polaris/tmpbuild/xo.strace`）。**本条的代价是一次一手事故**：那次探针在用户的 Wayland 桌面上**真的打开了一个 Edge 窗口** | 两件事同时定形：① POSIX 腿的首选就是 `xdg-open`（本机有、可用、立刻返回）；② **测试与自动化一律不得走真打开腿**——D11 因此把 `open_url` 做成 `Presentation` 注入接缝（与 CONN.12 那对 `clipboard_read` / `confirm_clipboard_read` 同族），itest 断的是「该不该开、开的逐字节是哪条串」，不是真开浏览器 |
| **F16** | 扫描成本**不是**本次选择的判据（近似探针 `/home/polaris/tmpbuild/probe_scan`，-O2，`volatile` sink 防死码消除，产物 `sink=96100000` 证明扫描真的跑了）：单行 120 列 **112 ns**、54×120 全可见区一次 **3.075 µs**、100,000 行全量 **5.11 ms**。参考帧时基线 `full_redraw_ms` **1.262 ms**（裁决 7.35 的新基线） | 诚实的读数反而否掉了「每帧预扫太贵所以不做」这种说法：预扫一屏只占帧预算约 **0.25%**。故 D4 的推荐档**不能靠成本论证**，要靠状态面：预扫要新增一份跨帧区间表并替它做行号折算与越限处置（搜索件为此写整族 `translate_rows` + `kMaxSearchMatches` + `truncated`，`include/borealis/ui/search.h:60-121`），而「悬停才划线」这一档界面判据**买不到**。成本论据在本稿一律不作主力 | 
| **F17** | `plan_right_click` 的签名**上一棒刚改过一次**：`plan_right_click(RightClickAction, bool has_selection, LogMenuState)`（`include/borealis/ui/right_click.h`），头注明写那是「**本棒唯一一处改既有签名**」，理由是纯函数里不能读会话。菜单条目次序句为「复制 → 粘贴 → 会话日志（`log.available` 为假时第三条不出现，前两条逐字节不变）」 | 本棒要加「打开链接」= **第二处签名改动**。这件事必须点名写进裁决（D12），否则下一棒会把它读成「顺手扩面」。同时 `MenuCommand` 的头注原本就把「全选 / 打开链接 / 分屏 / 重命名 / 广播组 / 关闭」列为「不在这里留点了什么也不发生的空位」（`right_click.h:36-44`），本棒正是那条承诺的兑现方之一 |
| **F18** | 视口的构造点**恰三处**，且都经同一个搬运件：`std::make_shared<borealis::ui::TerminalView>` 在 `src/main.cpp:514`（新开格）、`:957`（重连重启腿）、`:1788`（撤销重开腿），三处入参都是 `make_interaction(settings, line_ending)`（定义 `src/main.cpp:160-175`，逐字段从 `config::TerminalSettings` 搬进 `InteractionOptions`）。运行期改动走 `hooks.broadcast` → `apply_interaction_options(next)`（`:806-822`，实现体 `terminal_view.cpp:293-295` 就是整份赋值），且那段广播**只覆盖当前标签的 pane**（`:809-811` 的 `current_tab_id` 判据） | 「新增一项交互配置」的**唯一**正确落点是 `InteractionOptions`：三处构造点自动同值、不新增接线、与「面板改完立刻生效」的既有腿同形。两处既有边界要如实写进 §5：① 广播只到当前标签 ⇒ 别的标签要等它成为当前标签并再改一次档才刷新（本棒不扩这条腿，与 `clipboard_read` 用 app 级闸门避开它**不同**，因为链接档位不是安全闸，见 §5 第 2 条）；② `line_ending` 那种「两处必须同源」的坑不适用于本棒新字段 |
| **F19** | 设置键的**两处计数闸 + 一条例外通道**（`tests/unit/utest_settings_catalog.cpp`）：`:293` 钉「写出的 schema 叶子 − `kNonPreferenceLeaves` ＝ 核对表行数」且 `keys.size() == 65U`；`:563` 钉 `ConsumerStatus::Absent` 行数 `== 19U`；`:276-277` 那条例外名单是 4 条**面板不画控件的数据叶子**（`connection.recent` / `profiles.items` / `tunnels.items` / `key_dirs.items`，各自注释指明归属：侧栏、隧道面板、密钥面板） | 「已记住的域」这份**运行期数据**走的是例外通道 ⇒ 进名单（4→5）而**不占 65、不动 19**，面板不画它（D9①）。三枚新偏好键才占 65（65→68），且它们自入表起就是 `Wired`（消费方＝本棒的扫描/决策件），`Absent` 保持 19 ——「计数不动」这条本身就是证人：某条消费方被摘掉会让它反弹 |
| **F20** | 平台层是**逐文件显式列出**而非 GLOB（`src/CMakeLists.txt:85-107`：`if (WIN32)` 列七个 `platform/win/*.cpp`，`elseif (UNIX)` 列六个 `platform/posix/*.cpp`），Win 分支另有显式链接先例 `target_link_libraries(borealis_core PRIVATE ws2_32)`（`:110-112`）。Win 侧的 UTF-8↔UTF-16 现成件在 `src/platform/win/win_text.h:18` `to_wide(std::string_view)`（头注即裁决 7.11 那句「公共头一律 UTF-8，宽字符与码页 API 挡在 `src/platform/` 内」） | 新增 `platform/{posix,win}/open_url.cpp` 要**同时改两处**源列表，漏一处即「接口在、实现不在」的链接期断裂；Win 腿需要 `shell32`（`ShellExecuteW` 所在库），照 `ws2_32` 那一行的形态加。**Windows 腿的链接与语义本沙箱无法验证 ⇒ 一律标推断／未验证**（§4.2 第 11 条），走查清单里给它独立一条 |
| **F21** | 平台知识不外溢的**口径与线程注**：`src/platform/file_secure.h` 头注第 23-24 行「阻塞文件系统 IO，调用方须在 worker 线程（第 25 条），不得从 UI 事件回调或绘制路径进来」；平台腿的另一条纪律是 AGENTS §4.5 第 23 条 | 本棒的 `open_url` **不是**那条口径的违例也不是它的例外：它不等子进程、不等 IO 完成（F13 的 177 µs 是一次 fork/exec 系统调用），与 `file_secure` 那句「阻塞」的前提不同。这一点必须在公共头注里写死，否则下一棒会照着 `file_secure` 那句把它挪进 worker 线程，白白丢掉「点击即打开」的手感与证人面（D10①） |
| **F22** | 交互 bash 的提示符**本身就像邮箱**：`bash -ic 'echo $PS1'` 本机读数为 `[\[\e]0;\u@\h: \w\a\]${debian_chroot:+($debian_chroot)}\[\033[01;32m\]\u@\h\[\033[00m\]:…\w…\$ ]`，即屏上出现 `user@host:~$` 一类的 `\u@\h` 串，且 OSC 7 标题串里同样有 `\u@\h: \w` | 需求没要求邮箱识别，而「`user@host` 也是 URL」这一档在终端里**必然误报**（每一条提示符都命中）。D2 因此把 `mailto` 排除在缺省启发式之外，理由不是「没做」而是「在这里它是负收益」 |

---

## 1 范围与边界

**本期交付**：

1. **一份纯逻辑的链接识别与决策件**（`include/borealis/ui/url.h` + `src/ui/url.cpp`，namespace `borealis::ui`，不含 Aurora 与 `config` 类型）：`find_link_at(row, column, scan_options)` 从一格折出「这一格在不在一条 URL 上、整条的列游程、目标串、来源」；`plan_link_open(source, policy_osc8, policy_heuristic, trusted_hosts, target)` 折出「直接开 / 先问（并给出该记的那枚域）/ 拒绝」；`link_host_of(target)` 是唯一一处「什么算同域」的定义。三条都纯，故两来源 × 三档策略 × 白名单 × 已记住名单的全部组合都能脱开界面逐条断言（AGENTS §4.4 第 20 条）。
2. **OSC 8 优先，启发式补位**（D1①）：那一格挂着可解析的标识就用标识的目标；标识已被淘汰（`kMaxHyperlinks` 满表，裁决 7.21②）按「不可点」处理并**落回启发式**（同一段文字仍可能本身就是条 URL）。
3. **悬停态**（D4①、D5①、D6①）：鼠标停在一条 URL 上 ⇒ 该列游程底下一条 accent 色 1 物理像素下划线（另起一层，不并入 `CellPaint`），视口左下角非模态提示条显示**真实目标全文**（不是屏上那段文字）。三处入口共用同一个一行扫描件，无跨帧区间表。
4. **两条打开入口一条腿**（D7①）：Ctrl+左击（且仅当这一击**归本地**）与右键菜单条目「打开链接」，二者都走同一处 `open_link(hit)`；普通左击的行为逐字节不变（仍是选区起点）。
5. **右键菜单自此四条**（D12①）：复制 → 粘贴 → **打开链接** → 会话日志。前两条逐字节不变，第三条**只在命中链接时出现**（与会话日志那条「闸关则不出现」同形态），第四条随之后移一位。`plan_right_click` 多收一枚 `LinkMenuState{bool on_link}`。
6. **打开前的确认可配**（D8①）：三档策略 `never` / `ask_once`（缺省，即「首次确认并记住同域」）/ `ask_always`，OSC 8 与启发式**各一枚键**；确认框是**三按钮**自组形态（取消 / 仅本次打开 / 记住该域并打开），副行报识别来源，正文报 URI 全文。
7. **协议白名单**（D3①＋D8①）：新键 `terminal.link_schemes`，逗号分隔，缺省 `http,https`。**白名单外的串根本不识别为链接**（不画线、不进菜单、Ctrl+点击无效），故「可点」在本产品里只意味着「能打开」。
8. **「记住同域」落盘**（D9①）：域＝`scheme + host[:port]`（host 小写归一，等于该 scheme 缺省端口时折叠），存进**非偏好数据叶子** `connection.trusted_url_hosts`（字符串数组，走 `kNonPreferenceLeaves` 例外通道），面板不画控件。
9. **一条新平台腿**（D10①）：`platform::open_url(std::string_view utf8) -> OpenUrlOutcome{Opened, NotFound, Error}`。POSIX 腿＝双 fork + `execvp("xdg-open", …)`（不留尸、不经 shell）；Win 腿＝`ShellExecuteW` + `to_wide`（**推断／未验证**）。从 UI 回调直接调。
10. **打开腿与确认框都可注入**（D11①）：`Presentation` 加两条接缝 `confirm_open_link(request, answer)` 与 `open_url(target)`；装配层不装即走生产腿。

**本期不交付**（§7）：`mailto:` 与其余 scheme 的缺省识别、面板上「查看/清空已记住的域」的入口、`gio open` / `kde-open` 一类回落、鼠标上报档下的悬停线与 Ctrl+点击、全选/分屏/重命名/广播组/关闭那五条菜单条目、链接的收藏与历史、打开成败的回执、IDN 的 punycode 归一、真机走查。

**关键前提（口径不是本稿新造的）**：

- **本棒是消费方，不是来源方**（F1）：不新增 OSC 8 解析、不改网格侧表、不动 `Session` 公开面（`hyperlink_target` 已够用）。§4 的批 3 里唯一动到 `include/borealis/` 的是本棒自己的新公共头。
- **注入是结构要求，不是可选项**（F15 的一次手事故）：任何自动化路径都不得触达真 `xdg-open`。这条不是「洁癖」，是已经付出过一次「用户桌面上凭空多出一个浏览器窗口」代价的结论。
- **帧路径纪律**（F16）：本棒不在绘制路径里做与内容相关的扫描；三处扫描点（Move / Ctrl 点击 / 右键）都是**一行**，且发生在事件回调里。
- **无自动化证人的面照旧存在**：`src/main.cpp` 不编进测试 runner（裁决 7.83），故装配层那两处（生产 `open_url` 的装填、`trusted_url_hosts` 的读写闭包）没有证人，登记为在册代价（§7）。

---

## 2 草图与判据对应

草图四屏（A 悬停三态对照：OSC 8 / 启发式 / 鼠标移开 · B 三按钮确认 + 白名单内外对照 · C 右键菜单三条与四条 + 上报档四行对照表 · D 设置页终端页三行），图中圈号 1–8 即下表判据：

| 标号 | 判据 | 对应需求 |
|---|---|---|
| 1 | 鼠标停在一条 URL 上，该行该列游程底下出现**一条 accent 色 1 物理像素下划线**，随移动换段、离开即无。屏上文字与目标**可以不同**：OSC 8 那条屏上写「CI 构建日志」而目标条显示 `https://ci.example.com/builds/4821/logs`——**提示条与确认框显示的永远是目标全文，不是屏上文字** | 原文「视区内 URL 识别（含 OSC 8）」（D1①＋D6①；F1 那条「有 id 无人取」的安全含义就在这里兑现） |
| 2 | 启发式那档：一条 `curl https://example.com/a?b=1\|jq` 里，可点段是 `https://example.com/a?b=1`——`/ : ? = . -` **不是断点**（它们虽在缺省断词符集里，本件不用那枚键），而行尾的 `|` 与句子结尾的 `.,;:!?` 不进气。行内含全角字符（如 `见 https://a.cn/中文路径`）时游程**不因双宽延续格而截断** | 原文「纯文本启发式识别」与「信任级别可分别配置」（D3①；F2、F4 两条读数直接对应） |
| 3 | 视口左下角一枚非模态小条：**不铺覆盖带、不吃焦点、不吃点击**——提示条正下方那格文字照样能起选区。装不下（窗口过小）则整条不画，而不是截断或压掉内容 | 同上（D6①；F9 的「三不」与「让位不画」原样沿用） |
| 4 | 确认框（默认档 `ask_once`）标题「打开链接？」，正文**逐字沿用已评审稿面**：首行是 **URI 全文**，次行是来源「来源：OSC 8 显式超链接」或「来源：纯文本启发式识别（非 OSC 8 显式超链接）」，其下两行回显「协议白名单：http / https；信任级别两档分开配置」与「确认策略：首次确认并记住同域（默认）」；动作三枚「取消」「仅本次打开」「记住该域并打开」。点遮罩空白处**什么都不发生**（不关闭、不打开），与 F12 那条框架实测同口径 | 原文「打开前确认可配（默认『首次确认并记住同域』）」（D8/D9/F12；与 `codespec/UI_COMMANDS.draft.svg` 屏 4 那张已评审稿逐字对齐） |
| 5 | 白名单外的串**不可点**：`ftp://host/file` 与 `ssh://host` 底下**没有下划线**、右键菜单**没有「打开链接」**、Ctrl+点击不响。把 `ftp` 加进 `terminal.link_schemes` 之后**不重开任何东西**，那一处立刻变成可点 | 原文「协议白名单（默认仅 http/https）」（D3①；判据形态照 `codespec/UI_SETTINGS.draft.md` §3 B2-a 那一族「即时」） |
| 6 | 右键菜单自此**四条**（命中链接时）：复制 → 粘贴 → **打开链接** → 会话日志；未命中时**三条逐字节不变**，次序与文案与今天完全一致。「打开链接」的可用位恒真（能出现就能开），无选区时「复制」仍照旧置灰 | 原文「右键『打开链接』」（D12①；`right_click.h` 那句「不留点了什么也不发生的空位」的兑现） |
| 7 | 会话开了鼠标上报（`?1000`/`?1002`/`?1003`）时：**左击与悬停归远端**——无下划线、无目标条、Ctrl+点击不响，而**右键菜单那条「打开链接」照样在**（右键恒归本地）。按 Shift 让位本地的那一次左击可用 Ctrl+点击。`?1003` 档下悬停线不可得是本稿**明说的边界**，不是漏判 | 原文「Ctrl/Cmd+点击」（D7①；F6 的 `takes_pointer_by_report` 是**事件归属**的唯一分流判据，本棒不新造第二条；悬停**装饰层**另按 `mouse_tracking != Off` 让位，理由与代价见 §4 批 2 收口③） |
| 8 | 设置页「终端」页自此多**三行**：`terminal.link_osc8` 与 `terminal.link_heuristic`（各为三档下拉，缺省都显示 `ask_once`，行名「显式超链接（OSC 8）打开前」「纯文本识别的链接打开前」）与 `terminal.link_schemes`（文本框，缺省 `http,https`，行名「允许打开的协议」，旁注说明「逗号分隔；白名单外的链接不显示为可点」）。三行都带**即时**徽标、都**不**带「延后」徽标；「已记住的域」**不在本页出现**（数据件，非偏好设置） | 原文「确认可配」「信任级别可分别配置」（D8①＋D9①；F19 的 65→68 与 `Absent` 保持 19 就是这条的自动化证人） |

---

## 3 裁决（D1–D13；下表「推荐」列即本棒据以开工的档位，理由与代价一并入册为裁决 7.119）

| # | 待定项 | 选项 | 推荐 | 理由与代价 |
|---|---|---|---|---|
| **D1** | 同一格里 OSC 8 与纯文本启发式同时成立时取谁 | ① **OSC 8 优先**：有标识且解析得出目标即用之，不再跑启发式；标识已被淘汰则落回启发式；② 启发式优先（屏上文字最可信）；③ 两条都算，取列游程更长者 | **①** | OSC 8 是程序显式声明「这段文字指向这里」，是**意图**；启发式只是形状猜测（`www.` 前缀补 `http://` 就是猜）。② 会把「屏上写 docs.example.com、实际指向 `http://evil`」这一族攻击面**放大**而不是收窄——因为启发式只看屏上字。③ 的「取长」规则在一行里两串相邻时给出任一来源都不是用户想的那条，且要额外维护两套扫描的交叠。代价：① 下**屏上文字与目标可以不一致**，故本棒必须把「显示真实目标」做成硬判据（判据 1/4），而这条判据此前**没有任何消费方**（F1） |
| **D2** | 启发式认哪些形状 | ① 认 `<scheme>://…` 与 `www.…` 两类（后者按 `http://` 前缀参与白名单判定）；② 只认带 scheme 的；③ 再加 `user@host` 邮箱档 | **①** | 终端输出里 `www.` 起头的串极常见（文档、报错里的站点名），② 让一半可点内容不可点而需求那句「纯文本启发式识别」不区分有无 scheme。③ **在终端场景必然误报**：F22 的本机读数是缺省提示符本身就含 `\u@\h`（`user@host:~$`），把它当 URL 会做到「每一条提示符都点得开」，而需求没要求邮箱。代价：`mailto:` 本期不可点（§7 在册）；`www.` 那一档是**猜测前缀**，确认框的来源副行必须照 UI_COMMANDS 稿面写明「纯文本启发式识别」，让用户看得见这是猜的 |
| **D3** | URL 的字符集与边界从哪来 | ① **自带一张 URL 允许字符表**（ASCII 字母数字 + `-._~:/?#[]@!$&'()*+,;=%`）+ 行尾标点修剪（尾部 `.,;:!?'"` 与不配对右括号剔出）；② 复用 `ui::word_span_at` 与用户断词符；③ 复用断词符但在运行时剔除 URL 常用者 | **①** | F4 是决定性的：缺省断词符集把 `/ : ? & = # % + , ; @` 全收了进去，② 之下 `https://a/b?c=1` 双击取到的是 `https:`，URL 永远残缺。更要紧的是**那枚键用户可改**（判据 B2-b），于是「改断词符」会静默改掉链接边界——两件事被一根线绑住，谁都不知道。③ 把配置读进纯件，还得在纯件里维护「哪些字符要豁免」的第二套规则。代价：① 与双击选词**故意不一致**（用户可能以为「双击选中的就是链接」），走查里有一条专门看这一处；修剪规则的「不配对右括号」要写成可断言的计数判据而不是正则 |
| **D4** | 扫描时机 | ① **惰性**：Move / Ctrl+左击 / 右键三处入口各自按当前那**一条行**扫；② 每帧预扫全可见区并缓存一张区间表；③ 后台线程全量预扫（照 `ui::search` 的形态） | **①** | F16 说明这不是成本问题（预扫一屏 3 µs，约帧预算 0.25%，量不出差别），而是**状态面**问题：② 要新增一份跨帧区间表，并为它做行号折算、越限处置与失效时机——搜索那一族为此写了 `translate_rows` + `kMaxSearchMatches` + `truncated` 整套（`search.h:60-121`），那是**第二个搜索件**的成本，换来的界面判据却不存在（悬停本来就只有一条线）。③ 更过度：链接不需要全 scrollback 索引，可见区之外点不到（F3 的 `mirror_line_of` 回 `nullptr` 就是天然边界）。代价：鼠标在同一行内移动要重复扫该行（112 ns 档，可忽略）；「一行多枚 URL」时哪一枚可点由列游程决定，故三处入口都**现扫现判**，判定不缓存（缓存反而会产生「线还画着、目标已换」的分叉） |
| **D5** | 悬停视觉的落笔形态 | ① **视口另起一层** `fill_rect`（accent 色、1 物理像素、基线 +1px、四边吸附物理像素，照 `decoration_rects` 的口径），只画悬停那一段；② 折进 `CellPaint::underline` 走既有装饰层；③ 不画线，只在提示条里报名字 | **①** | ② 的两处代价是确定的（F8）：run 按样式全等切分（裁决 7.23②）让 URL 边界多切一刀，而 `decoration_rects` 用色 `run.paint.foreground` 拿不到 accent，`palette.cpp:143` 那条折算路径还得为一个界面态开一个字段——把「鼠标在哪」写进「这个 cell 长什么样」是**层次错位**。③ 少了一半功能：需求的验收判据写的是「视区内 URL 识别」，没有线的识别只在点击后才知道。① 有本仓先例（`tab_bar.cpp:172` 的 accent 下划线）且零改动复用 `rect_for`。代价：URL 段本身带 SGR 4 下划线时，悬停那条线**覆盖**在同一基线位置（`baseline+1`），线色由前景变 accent——刻意选「同位覆盖」而非「再画一条」，否则两条线重叠会发粗；这条视觉差写进走查 | 
| **D6** | 目标 URI 显示在哪 | ① **视口左下角自绘小条**（照 F9 的「非模态三不」+「装不下就不画」）；② 框架 `Modifier::tooltip`；③ 状态栏；④ 不显示 | **①** | ② 按 F10 是半成品：`tooltip_hover_start/end` **全仓无调用方**、也没有气泡绘制，用它等于登记一个没人画的字符串，且按 AGENTS §5.1 那是事件链路类缺口，要等 Aurora 侧补公共 API——本棒不等不绕（§5.1 只要求**缺口先在框架侧补**，而本需求这条**不是缺口**：自绘小条是可组合的应用侧体验，§5.3 明文允许）。③ 按 F11 件未开工。④ 直接把 D1 的代价（屏上文字≠目标）藏起来。代价：新增一处绘制层与一条 `Rect` 盒（**不写进 `*_button_box_`**，否则抢点击）；状态栏那一棒落地后这个位置要重新裁（§8 在册一条将来改判） |
| **D7** | 触发腿与鼠标上报档的关系 | ① **Ctrl+左击打开，且仅当这一击归本地**（`takes_pointer_by_report` 回 `false`）；右键菜单条目始终可用（右键恒归本地）；普通左击行为不动；② 普通左击即打开；③ 本期只做菜单，不做 Ctrl+点击 | **①** | ② 与拖选正面冲突：终端里左击是选区的起点，误开浏览器的代价远大于「少点一次」。③ 单独不成立——`terminal.right_click` 取 `Paste` / `CopyOnSelect` 两态时**根本不发菜单**（裁决 7.38⑥），那两档用户会失去唯一入口；这与会话日志 D6 同一条理由，而链接更硬：它是本需求的**主入口**。① 复用 F6 那条既有分流判据，因此「`?1003` 下无悬停线、无 Ctrl+点击」不是本棒漏判而是让位的自然结果（远端 vim/tmux 要的是那一击）。代价：开了鼠标上报的会话里悬停视觉与 Ctrl+点击 都不可用（走查第 ⓻ 条实测这一档），Shift 让位那一击可用 Ctrl+点击 这条要单独有证人；判据 7 把这条边界写在稿面上而不是留给用户猜 |
| **D8** | 确认策略与白名单的键数与形态 | ① **三枚偏好键**：`terminal.link_osc8` 与 `terminal.link_heuristic`（同为 Choice，三档落盘名 `never` / `ask_once` / `ask_always`，缺省都 `ask_once`）+ `terminal.link_schemes`（`ControlKind::TextInput` × `ValueDomain::FreeText`，缺省 `http,https`）；② 一枚全局 `link_confirm` + 两枚 bool（osc8 可信 / 启发式可信）；③ 再往面板加「已记住的域」那一行（共四枚偏好键） | **①** | 需求两句话（「打开前确认可配（默认首次确认并记住同域）」「OSC 8 与纯文本启发式的信任级别可分别配置」）的最短覆盖就是**两枚同型枚举**：「信任级别」在本产品里可表达的就是「要不要问」的三档，再拆成两族键等于把一句话写成两句。② 的 bool 表达不出 `ask_always` 那一档（安全向用户要的正是它）。③ 违反 F19 的键成本，且「已记住的域」是数据不是偏好（D9①）。代价：65→68 三行核对表 + 词条 + 表单搬运 + 装载落盘五处同步；「三档 × 两来源 = 六组」全部要有 utest 证人。**白名单那一枚的装载侧不校验域**（`ValueDomain::FreeText` 的定义就是「只判类型不判域」，`settings_catalog.h:71`）⇒ 非法/空白 scheme 由纯件在判定时拒绝，面板**不拒收**，与 `word_delimiters` 的「原样收字符」同口径；代价是空串/全非法那档会表现为「什么都不可以点」，判据 5 旁注要说清 |
| **D9** | 「同域」的定义与那份名单的落盘形态 | ① 域＝**`scheme + host[:port]`**（host 小写归一；端口等于该 scheme 缺省端口时折叠掉），名单走**非偏好数据叶子** `connection.trusted_url_hosts`（字符串数组，进 `kNonPreferenceLeaves` 4→5，面板不画）；② 域＝只记 host（不含端口）；③ 名单进面板可编辑（占 65 的一行，需列表编辑控件）；④ 不落盘（每次都问） | **①** | ④ 与需求那句「记住同域」正面冲突，先排除。② 在开发机上是**扩大信任**：`localhost:8080`（自己起的服务）与 `localhost:9000`（另一个进程）按②算同域，一次「记住」把信任铺满整台机器的所有端口——本需求唯一的风险缓解腿就是这一问，不该由自己削弱。① 的折叠避免 `http://x` 与 `http://x:80` 各存一份。③ 要新造一份列表编辑控件（本仓无先例），成本与「用户真要管这份名单」的比例不成立。代价：面板**没有**查看/清空入口（要清只能改 `borealis.json`），§7 登记为一条观察项，若真人用起来别扭再补那一行（届时它才占 65）；host 的字面串按原样记，**不做 punycode/IDN 归一**（本沙箱无 IDN 测试面 ⇒ 标推断／未验证），百分号编码不在 host 里出现故不处理 |
| **D10** | 打开动作的平台腿形态与线程 | ① `platform::open_url(utf8) -> OpenUrlOutcome{Opened, NotFound, Error}`：POSIX＝**双 fork + `execvp("xdg-open", {…})`**（中间父 waitpid 收、孙不留尸，F13），Win＝`ShellExecuteW(nullptr, L"open", to_wide(url), …)`（F20，**推断／未验证**）；**从 UI 回调直接调**，不经 worker；② 投 worker 线程 + 有界队列；③ `system("xdg-open '<uri>'")` | **①** | ③ 把注入面从 argv 换成 shell 命令行，而 URI 是不可信输入（会话字节流）；F14 证明「不经 shell」才封得住，且真 URL 里 `%27`/单引号本就可能存在。② 的现成先例（本仓两条有界队列）都是**丢弃型**（`codespec/UI_SESSION_LOG.draft.md` F15 那条读数），而「打开一次」是一次性动作，双 fork 的成本 177 µs 已在 UI 回调可接受量级、不阻塞任何 IO（F13/F21），加线程只换来「点了一下、两帧后才开」与一份无处断言的队列。① 的**两条代价**要如实登记：⑴ 双 fork 拿不到孙进程退出码 ⇒ 本腿**不承诺打开成功**，故本期不做「已在系统中打开」那类 toast（没有证人的陈述不进界面）；⑵ `xdg-open` 缺位的机器回 `NotFound`，本期不做 `gio`/`kde-open` 回落（§7），于是「能力闸开着但没桥」这一档只有日志、没有 UI 说法——与 CONN.12 的缺桥档同一族，写进走查 |
| **D11** | 打开腿与确认框的测试形态 | ① `Presentation` 加**两条**接缝：`confirm_open_link(request, answer)`（问的呈现与答什么）与 `open_url(target)`（最终发出去的串逐字节断言）；② itest 直接调 `platform::open_url`（真开） | **①** | ② 在本沙箱有**一手事故**：探针 `xdg-open https://example.com` rc=0 并真的 exec 了 msedge（F15），在用户桌面开出一个窗口。把这条腿交给自动化等于每次跑测试开一个浏览器，且它在无头 CI 环境里必然假红。① 与本仓已落地的 CONN.12 那对完全同族（`terminal_view.h:82-100`：`clipboard_read` + `confirm_clipboard_read`，「字段留空即回落默认」），因此测试替身**不是新发明**，装配层也不需要为它改任何接口形状。代价：生产腿（`src/platform/*/open_url.cpp`）**没有自动化证人**（`src/main.cpp` 不编进 runner，裁决 7.83），只有走查；「问了没答」那一档在本需求里是**安全的**（什么都不打开），故不需要 CONN.12 那种析构撤账腿——这一句要写进头注，否则下一棒会照抄那条设计多写一份撤账 |
| **D12** | 「打开链接」进菜单的形态与 `plan_right_click` 的第二处签名改动 | ① 纯件多收一枚 `LinkMenuState{bool on_link}`，次序折成「复制 → 粘贴 → 打开链接 → 会话日志」，未命中时**第三条不出现**（前两条逐字节不变）；② 把三枚条件（选区/日志/链接）折成一枚 `MenuContext` 聚合体一次改签名；③ 不动纯件，绘制侧自己 append 这一条 | **①** | ③ 把「菜单里有几条、哪条灰」这件**本来就可全量断言**的事拆到两处——正是 `right_click.h` 头注那句「判定的全部取值都能脱开界面逐条断言」要防的分叉。② 的调用点改动量与①相同（每个调用点都要动），但把上一棒刚立的 `LogMenuState` 重新包一次，是为**还不存在的第四枚条件**做抽象（AGENTS §4.1 第 3 条）。① 与 D6/7.116 那条「`log.available` 为假时第三条不出现」严格同形，读者不需要学新规矩。代价：这是 `right_click.h` 头注声明「唯一一处」之后的**第二处签名改动**（F17），必须在裁决里点名；`utest_right_click` 与 `itest_right_click_paste` 的调用点同批改口，且「三条变四条」这条判据的证人在 `plan_right_click` 那组而非界面侧 |
| **D13** | 悬停与选区/搜索命中的层序，以及点击后选区是否被改 | ① 悬停线**最上层**（它只覆盖同一基线的 SGR 线，不改底色），且**任何打开动作都不碰选区**（Ctrl+点击 直接返回、不 `extend_selection`、不认领事件后再起选区）；② 选区盖悬停；③ Ctrl+点击 顺手把那一词选中 | **①** | `codespec/UI_SELECTION.draft.md` 的 D4 已经裁过「选区盖过命中高亮」，那是**底色**之间的层序；本棒的悬停线是**装饰**层，不与底色抢同一格属性，故「最上层」不与既有裁决冲突（写清这一句是为了不让下一棒读成改判）。③ 看着像「所见即所得」，实际是让一次打开顺带改一次选区——随后的 copy-on-select 会把 URL 塞进剪贴板，那是用户没要的动作。代价：Ctrl+点击 之后屏上残留的旧选区不消失（用户可能以为要点第二下），这一条留给走查判读；`e.is_handled` 在本棒这一击为 `true`（不让它再去起选区），这条要在 itest 里用「点完没有新选区」来断 |

---

## 4 文件布局与批次

**批 1＝纯逻辑件（不碰 UI，不碰平台）**

| 路径 | 动作 | 内容 |
|---|---|---|
| `include/borealis/ui/url.h` | 新建 | `LinkSource{Osc8, Heuristic}`、`LinkConfirmPolicy{Never, AskOnce, AskAlways}`（**唯一定义处**，`config::LinkConfirmPolicy` 是其别名，与 `term::PasteNewlinePolicy`、`ui::RightClickAction` 同一条单一真值源迁移先例）、`LinkScanOptions{std::vector<std::string> schemes}`、`LinkHit{RowSpan span; std::string target; LinkSource source;}`（**目标串按 UTF-8 落**，见下方批 1 收口回填①）、`TrustedLinkHosts`（`contains` / `add` / `load` + `set_change_sink`，纯内存、不含 IO）、`LinkOpenPlan{intent(Open/Confirm/Refuse), host, reason}`、`find_link_at` / `plan_link_open` / `link_host_of` / `parse_link_schemes`。全部类型逐字段 `operator==` default（照 `right_click.h` 的断言形态） |
| `src/ui/url.cpp` | 新建 | 三个纯件实现：OSC 8 游程（跳双宽延续格、F2）、启发式字符表 + 行尾修剪（F4/F22）、scheme 白名单判定、host[:port] 归一与缺省端口折叠 |
| `src/CMakeLists.txt` | 改 | 在 `borealis_core` 的显式源列表里加 `ui/url.cpp`（该列表非 GLOB，F20 同一条口径适用于 `src/`） |
| `tests/unit/utest_ui_url.cpp` | 新建 | 判据 1/2/5/7 的全部取值：两来源 × 三档策略 × 白名单内/外 × 已记住/未记住 × 端口折叠 × 大小写归一 × 淘汰标识落回启发式 × 修剪规则逐档。用手工构造的 `grid::Row`（`set_hyperlink` + `set()`）造现场，不需要界面 |

**批 1 收口回填（2026-10-12，AGENTS §4.2 第 12 条：冲突以代码运行时为准）**：

- **① 目标串的编码从 `std::u32string` 改为 UTF-8 `std::string`**。原稿那行写的是码点串，落地按裁决 7.11 那句「公共头一律 UTF-8」改：OSC 8 侧的 `std::u32string`（`Session::hyperlink_target` 的原型）在 `find_link_at` 内**编一次**，此后提示条文案、确认框正文与落盘的域名单同持一份编码，不在三处各转一次。入参 `osc8_target` 仍是 `std::optional<std::u32string>`，那是与 `Session` 的接缝形状，不改它。
- **② 新增一枚公共件 `parse_link_schemes(std::string_view csv)`**：配置里那枚逗号分隔串拆成小写、去空白、丢空项的表。放在纯件而不是 `src/main.cpp`，是为了让「非法/空白 scheme 由纯件在判定时拒绝、面板不拒收」（稿 D8① 的代价）这一条有证人而不是只有装配语句。
- **③ `LinkOpenPlan::reason` 落成枚举 `LinkOpenReason{None, HostUnparsable}`** 而不是字符串；白名单不进本调用的入参（识别处已把关，见 §1 第 1 条与判据 5 的形态）。
- **④ 证人与变异读数**：`utest_ui_url` 22 案全绿（结项读数 84 项 / 1074 案，基线 83 项 / 1052 案）。变异 M-A 摘启发式白名单闸 ⇒ **恰红 1 案**（`only_whitelisted_schemes_become_clickable`）；补一则 **M-A′** 摘 OSC 8 那一侧的同一道闸 ⇒ **恰红 1 案**（`osc8_target_outside_the_whitelist_...`）——两闸的证人在本件里本就是两处调用点，各打各的红，故拆成两则记；M-B 把 OSC 8 那一支短路（启发式先跑）⇒ 红 3 案，三案全部落在**同一条**判据（D1 优先）的不同侧面（目标取声明 / 游程取标识 / 过闸不落回），不是两条判据同源；M-C 让「记住」不落账 ⇒ 红 2 案（「同域第二次不再问」+ 名单件本身，同一条腿的两层）；M-E 把双宽延续格改成按列直取 ⇒ **恰红 1 案**。M-D（上报档让位闸）的证人在批 2 的归属闸上，本批无从注入。

**批 2＝视口（命中、悬停、两条入口、确认框）**

| 路径 | 动作 | 内容 |
|---|---|---|
| `src/ui/terminal_view.h` | 改 | `InteractionOptions` 加 `LinkOptions link{...}`（两枚 `LinkConfirmPolicy` + 白名单，F18）；`Presentation` 加 `confirm_open_link` 与 `open_url` 两条接缝（D11）；新成员 `hover_link_`、`link_notice_box_`（**不进 `*_button_box_`** 那组，F9）；新私有入口 `open_link(hit)` / `ask_open_link(hit)` / `paint_link_hover(p, bounds)`；头注写清「问了没答 ⇒ 什么都不打开，故**不需要** CONN.12 那条析构撤账腿」 |
| `src/ui/terminal_view.cpp` | 改 | 本地 Move 分支（`:696` 那条 `return` 之前）加一行扫描与 `hover_link_` 更新 + `mark_needs_paint`；左键 Press 分支加 Ctrl 档短路（在 `takes_pointer_by_report` 之后，D7）；`on_right_click` 现扫命中格并把它喂进 `plan_right_click`；`menu_label` 与 `on_menu_command` 加 `MenuCommand::OpenLink`；`paint_link_hover` 两层（游程下划线 + 左下提示条）；`ask_open_link` 自组三按钮（照 `credential_prompt.cpp` 形态与 `settings_label` 取词） |
| `include/borealis/ui/right_click.h` + `src/ui/right_click.cpp` | 改 | `MenuCommand` 增 `OpenLink`；新 `LinkMenuState{bool on_link}`；`plan_right_click` 第四枚入参与四条/三条的产出。**这是第二处签名改动**（D12/F17），头注那句「本棒唯一一处」就地改口 |
| `tests/unit/utest_right_click.cpp` + `tests/integration/itest_right_click_paste.cpp` | 改 | 调用点随签名改口；补「命中链接 ⇒ 四条、次序 复制/粘贴/打开链接/会话日志」「未命中 ⇒ 三条逐字节不变」两案 |
| `tests/integration/itest_url_link.cpp` | 新建 | 真派发鼠标事件（照 `itest_right_click_paste.cpp` 的 `pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_)` 形态）+ 替身接缝：判据 1/4/5/6/7 的界面侧与「无宿主」档 + 像素档（有/无下划线、有/无提示条、装不下让位不画）；`HeadlessSurface` 不可用的档照既有口径 SKIP 并写明 |

**批 2 收口回填（2026-10-12，AGENTS §4.2 第 12 条：冲突以代码运行时为准）**：

- **① 三枚类型落进控件作用域**：`LinkOptions`、`LinkOpenRequest`、`LinkOpenAnswer` 是 `TerminalView` 的**嵌套类型**而不是原稿的 `ui::` 自由类型——它们只服务这条 UI 腿，而 `ui::` 里那几枚（`LinkSource` / `LinkHit` / `LinkConfirmPolicy`）是跨层复用的纯件面。测试与装配层据此写 `TerminalView::LinkOpenRequest`。
- **② 名单注入走新增接缝 `set_trusted_link_hosts(TrustedLinkHosts &)`**，构造参数一字不动（F18 那句「三处视口构造点不动」适用于此）；未装时控件经 `link_hosts()` 自持一份退化名单。除原稿列的 `hover_link_` 外实际还多了 `menu_link_`：右键菜单那条「打开链接」用的是**按下那一刻现扫**的命中而不是悬停态——上报档下没有悬停态，而菜单条目照样要有目标（判据 7 的后半）。
- **③ 悬停层让位的判据落地为 `term::mouse_tracking(modes) != Off`，而非原稿的 `takes_pointer_by_report`**。这是本棒一处**自决改口**（按常存授权走推荐档）：F6 的实测说 `?1000`/`?1002` 档下**无键 Move 不落进上报**、事件归属仍回本地，故归属判据表达不出「这一层不该画」；而线所承诺的那一次点击在此刻确实不归本地（Press 全被吃掉），画出来就是让一个点不动的东西看起来能点。**压的只有装饰层**：本地腿照旧不认领这次 Move，选区行为逐字节不变——判据 7 那句「无下划线、无目标条」与「左击归远端」由此各自有证人。
- **④ `link_notice_box_` 与「本帧真的画了」同源**：`paint_link_hover` 每帧先 `reset()` 再按落笔回填，于是「装不下就不画」的证人是**盒为空**而不是「盒停在上一帧的位置」。量宽用 `FontEngine::display_width`（逻辑 dp）而非 `measure_width`（设备像素，与本段坐标系不同源）；刻意**不设宽度上限**——截断一条 URI 本身就破判据 1/4 那句「显示的恒是目标」，故装不下时整条不画。
- **⑤ 确认框正文下面那两行回显的是当前生效值**，不是稿面固定措辞：把「（默认）」印在用户已改过的档旁边就是假话；档位串跟着 `LinkOptions` 走，中文名后括注落盘名沿用既有词条纪律。
- **⑥ `open_url` 接缝刻意没有「留空即调生产腿」的回落**（与同族 `clipboard_read` 那两条相反）：F15 那次事故（一次性探针真的 exec 了 msedge、在用户桌面开出一扇窗口）就是这条口径的根据，头注已写明，故 `itest_url_link` 每一次都装替身。
- **⑦ 驱动台实测的框架事实（承重，写下免得下一棒重量）**：Headless 14pt 单宽格＝**11 dp × 22 dp、scale 1.0** ⇒ 窗口取 640×440（57 列 × 19 行）才同时满足「第 40 列还落在格里」与「一段 20 列游程整条在可见区」，`preflight` 的门槛按这两枚数写；`decoration_rects` 的线**高恒 1 物理像素**、长＝游程列数 × 单宽，像素期望据此算而不写死；派发器**集中盖连击章**（同一格连续两次点击 ⇒ 第二次 `click_count == 2` 进 `DragMode::Word`），故「Ctrl+点击不改选区」不能拿两次单击当证人；`ui::row_spans` 口径下**两端重合即空表**，选区断言一律用拖拽；`link_host_of` 交回的是 **origin 形态**（`scheme://host[:port]`），名单键、「记住」写入与「判同域」共用同一枚串，故测试字面串写 `"http://example.com"` 而不是 `"example.com"`。
- **⑧ 证人与变异读数**：`itest_url_link` **19 案全绿**（结项读数 **85 项 / 1096 案**；开工前基线 83 项 / 1052 案，批 1 收口时 84 项 / 1074 案）。**M-C**（让「记住」不落账，注入点在 `apply_link_answer`）⇒ 红 3 案：`remember_and_open_…`（同域第二次不再问）、`a_different_port_…`、`the_three_dialog_buttons_…`——三案是同一条「不落账」腿的三层证人（名单本身 / 端口边界 / 生产自组三按钮框），不是三条判据同源；**M-D**（去掉 Ctrl+点击 的「归本地」闸）⇒ **恰红 1 案** `pointer_reporting_suppresses_the_hover_layer_and_the_click_entry`，红的两半正是 `asks.empty()` 与「那一击整笔送给了远端」那条 `written()` 断言——这一则就是 §6 说的「唯一能证明本棒复用了 F6 那条既有判据」的证人。两则变异注入后即撤，撤后 85 项复全绿。

**批 3＝平台腿 + 设置键 + 装配 + 文档回写**

| 路径 | 动作 | 内容 |
|---|---|---|
| `src/platform/open_url.h` | 新建 | `OpenUrlOutcome` + `open_url(std::string_view utf8)`。头注写死三件事：⑴ **不等子进程、不等 IO 完成**，故不属 `file_secure.h` 那句「须 worker 线程」的范围（F13/F21）；⑵ POSIX 双 fork 拿不到退出码 ⇒ `Opened` 的确切含义是「已发出」，不承诺浏览器真的起了；⑶ URI 经 argv 交出去，不经 shell（F14） |
| `src/platform/posix/open_url.cpp` | 新建 | 双 fork + `execvp("xdg-open", {…})`；空/含 C0 的入参直接 `Error`；`NotFound`＝`execvp` 失败（子进程 `_exit(127)`，父侧收不到 ⇒ 故 `NotFound` 只由「先 `access` 判 `xdg-open` 在不在 PATH」这一条给出，写成读点而不是猜） |
| `src/platform/win/open_url.cpp` | 新建 | `ShellExecuteW` + `to_wide`（`win_text.h:18`）。**全部说法标推断／未验证**（本沙箱无 MSVC） |
| `src/CMakeLists.txt` | 改 | 两处源列表各加一条（F20）；Win 分支的 `shell32` 照 `ws2_32` 那一行形态加 |
| `include/borealis/config/settings.h` | 改 | `terminal.link_osc8` / `link_heuristic`（缺省 `AskOnce`）+ `link_schemes`（缺省 `"http,https"`）；`connection.trusted_url_hosts`（`std::vector<std::string>`，缺省空）；`using LinkConfirmPolicy = ui::LinkConfirmPolicy` |
| `src/config/store.cpp` | 改 | 三枚偏好键的读写 + `trusted_url_hosts` 的数组读写（照 `connection.recent` 那一份数组先例） |
| `src/config/form_transfer.cpp` | 改 | 三枚偏好键各一条 `Binding`；`trusted_url_hosts` **不进表单**（面板不画它，F19 的例外通道） |
| `src/ui/settings_catalog.cpp` | 改 | 终端页三行（两枚 `dropdown` 紧随 `terminal.clipboard_read` 那族，一枚 `row(TextInput, FreeText)`），全部 `Wired × Immediate` ⇒ 65→68、`Absent` 保持 19 |
| `src/ui/settings_i18n.cpp` | 改 | 三行行名 + 白名单旁注 + 确认框文案词条（`links.confirm.*`），词条 key 即落盘点号路径（`settings_i18n.h` 头注①） |
| `src/main.cpp` | 改 | `make_interaction` 里搬三枚新键；装配处装填生产 `open_url`；`TrustedLinkHosts` 一份 app 级实例 + `on_change` 闭包写盘 + 启动时从 config 灌入。**三处视口构造点一字不动**（F18） |
| 文档 | 改 | `SPECIFICATIONS.md` §7 裁决 **7.119** + `INTERACT.05` 条目现状句；`CHANGELOG.md` **v0.118 → v0.119**；`PLAN.md` §5 的 `INTERACT.05` 行与 §8 的 M4 行；`ARCHITECTURE.md` §10.4 那一句改口为落地形态；`codespec/UI_SETTINGS.draft.md` §3 的 B 段加三行；`codespec/UI_COMMANDS.draft.svg` 不动（稿面已在册，本棒是实现方） |

**三批切分的判据**：批 1 之后 `find_link_at` 的全部取值可断言、界面仍无任何变化（不产生「点了什么也不发生」的空位，因为还没有入口）；批 2 之后整条 UI 链在 itest 里以替身逐字节闭合；批 3 才让真浏览器可被打开——这三段边界各自独立可结项，与本仓 TERM.09 / CONN.11 / CONN.12 三棒的切法同构。

---

## 5 装配与接线（事实清单，逐条带出处）

1. **档位从配置到视口只有一条路**（F18）：`make_interaction(settings)`（`src/main.cpp:160-175`）里搬三枚新键 ⇒ 三处构造点（`:514` / `:957` / `:1788`）自动同值；运行期改动走 `hooks.broadcast` → `apply_interaction_options`（`:817`）。
2. **广播只覆盖当前标签**（F18 的既有边界）：`hooks.broadcast` 体以 `current_tab_id` 为范围（`:809-811`），故别的标签要等成为当前标签并再改一次档才刷新。本棒**不扩这条腿**——链接档位不是安全闸，与 `terminal.clipboard_read` 用 app 级闸门绕开它不同（那一条的理由是「关掉的是远端能不能读你的剪贴板」，裁决 7.118 的 `Immediate` 注释写明的）。这一句是刻意留下不对称，不是漏：安全类才值得为它多养一份全局态。
3. **「记住」的写盘闭包**：`TrustedLinkHosts::on_change` 由装配层给出，落 `connection.trusted_url_hosts`。视口不认识 `config`（架构 §2.3 的模块环约束，同 `right_click.h` 头注那句），故这条腿只能以闭包注入。**没有自动化证人**（裁决 7.83），登记在册（§7）。
4. **生产打开腿的装填**：`set_presentation` 的 `open_url` 字段在装配层指向 `platform::open_url`；留空即回落默认实现（同一函数）。这是全仓**唯一**一处从 UI 回调进 `fork/exec` 的地方，出处须与 F13/F21 那两条读数同处引用。
5. **命令面板不新增条目**：本期两条入口（Ctrl+点击、右键菜单）都是指针动作，没有对应的键盘命令可注册；「复制当前可点链接」一类不在需求内，不加。
6. **可观测**：打开次数与拒绝次数（白名单外 / host 解析失败）走 `AURORA_LOG_*` 记在平台腿与决策件的边界，不进 `Session` 的计数结构（那是状态机侧的 `OscState` 一族的账本，本棒不动它）。

---

## 6 验收映射（三层证人 + 结项读数 + 变异自证）

| 需求判据 | 证人 | 层 |
|---|---|---|
| 「视区内 URL 识别（含 OSC 8）」 | `utest_ui_url` 的游程/淘汰/双宽三组；`itest_url_link` 的「有/无下划线」像素档 | utest + itest |
| 「纯文本启发式识别」与「不依赖断词符」 | `utest_ui_url`：同一行在**改过 `word_delimiters`** 的两份配置下链接边界逐字节不变（这条断的是 D3 的绑线，比断识别本身更值钱） | utest |
| 「协议白名单默认仅 http/https」 | `utest_ui_url` 白名单内/外两组（外的**不产 `LinkHit`**）；`itest_url_link` 判据 5 那档「改完立刻可点」 | utest + itest |
| 「Ctrl/Cmd+点击」 | `itest_url_link`：Ctrl+Press 一案断 `open_url` 替身收到逐字节目标 **且** `selection_` 未被改动（D13①）；对照组 plain Press 断选区行为逐字节不变 | itest |
| 「上报档让位」 | `itest_url_link`：`?1000`/`?1003` 两档各断「无悬停线、Ctrl 击不响、菜单条目仍在」三件事（F6/F7） | itest |
| 「右键『打开链接』」 | `utest_right_click` 四组（on_link × 三态配置）；`itest_url_link` 的菜单命中链一案 | utest + itest |
| 判据 3 的「非模态三不」与「让位不画」 | `itest_url_link`：同一枚目标在宽窗画得出、140 窄窗整条不画（正例作非空转证人）；起点落在条内的一次拖拽照常起选区且不问不开（`link_notice_box_` 每帧与落笔同源） | itest |
| 「打开前确认可配（默认首次确认并记住同域）」 | `utest_ui_url` 六组（两来源 × 三档）+ 端口折叠/大小写归一/同域第二次不再问；`itest_url_link` 三按钮各点一次的替身读数 | utest + itest |
| 「信任级别可分别配置」 | `utest_ui_url` 中「osc8=never ∧ heuristic=ask_always」与反向那一组（两组必须各问各的） | utest |
| 「记住同域」的落盘 | 仅 `utest_config` 的往返等值（数组写读一遍）；装配闭包无证人 ⇒ §7 在册 | utest + 在册缺口 |
| 真打开浏览器 | **无自动化证人**（D11 的判断，F15 的事故）；走查第 ⑴⑵ 条 | 走查 |

**结项读数口径**（AGENTS §4.4 第 21 条）：`ctest --preset linux -E etest_` 的**项数** + `./build/borealis_test_runner` 全跑的**案数**两条都要报，不以「测试全绿」一句结项。开工前基线（2026-10-12 CONN.12 收口时）：**83 项 / 1052 案**。

**变异自证清单**（§4.4 第 22 条，每则只应打红一条判据，红两条即说明两条判据同源、需要再切）：

- **M-A**：把 `find_link_at` 的白名单判定摘掉（任何 scheme 都算链接）⇒ 应红「白名单外不可点」那一组，且 `itest_url_link` 的 ftp 那案随之红。
- **M-B**：把 D1 的优先级颠倒（启发式先跑）⇒ 应红「OSC 8 那条屏上文字与目标不同」那一案（红两条即 D1/D6 的证人没分开）。
- **M-C**：让「记住该域并打开」不写 ledger ⇒ 应红「同域第二次不再问」那一条，其余不红。
- **M-D**：把 Ctrl+点击 的「归本地」闸去掉（上报档也响）⇒ 应红 `?1000`/`?1003` 那两档的对照案，且这是**唯一**一条能证明本棒复用了 F6 那条既有判据的证人。
- **M-E**：把 OSC 8 游程的「跳双宽延续格」写成按列直取 ⇒ 应红含全角字符那一案（走查里也有一条，两处的形态要一致）。

---

## 7 真机走查清单（UI 件未走完不得称「可用」，AGENTS §4.4 第 33 条）

1. POSIX 腿：本机 `curl` 一个真站点的 URL 到屏上，悬停出下划线与左下提示条，Ctrl+点击**真的**打开默认浏览器（本沙箱＝Edge，F15）；`xdg-open` 不在位的机器上表现为「点了没反应 + `NotFound` 日志」，**不得**表现为「打开了别的东西」。
2. OSC 8 腿：`printf '\033]8;;https://example.com\033\\点我\033]8;;\033\\'` 上屏，屏上是「点我」而提示条/确认框必须显示 `https://example.com`（D1 的代价兑现处）。
3. 启发式边界：含中文路径、含行尾句号/右括号、含 `|jq` 尾巴三条各实测一次，核对判据 2 的进入与剔除。
4. 确认框：三枚按钮各点一次；点遮罩空白处确认**不关闭也不打开**（F12）；「记住该域并打开」后同域第二次不再问，**同 host 不同端口**第二次仍问（D9① 的那一条，是最容易被实现成②的地方）。
5. 白名单：把 `ftp` 加进去，不重开任何东西即 `ftp://…` 变可点；清空该键后表现为「什么都不可以点」而旁注那句话能解释它。
6. 右键三态：`context_menu` / `paste` / `copy_on_select` 三档各测一次，其中后两档**没有**「打开链接」条目但 Ctrl+点击仍可用（这正是 D7 否掉③的那条判据的真机形态）。
7. 鼠标上报：`vim`（`?1000` 一族）与 `?1003` 各开一次，核对判据 7 的三条；Shift 让位那一次 Ctrl+点击 应可用。
8. 层序：URL 段本身带 SGR 4 下划线时悬停，核对 D5 那句「同位覆盖、线色变 accent」；与选区、搜索命中相撞的一格核对 D13①。
9. **Windows/MSVC 腿全部说法未验证**（`ShellExecuteW` 的返回档、`shell32` 链接、URL 里带 `%` 与 `&` 的实际行为），F20/F13 两条读数都只在 POSIX 取到 ⇒ 走查第 1–8 条须在 Win 腿复跑一次再谈「可用」。

---

## 8 在册代价、教训与将来改判

**在册代价**（开工时已知、本棒不解决）：

- 生产打开腿与 `trusted_url_hosts` 的装配闭包**没有自动化证人**（`src/main.cpp` 不编进 runner，裁决 7.83 同一条）。
- 面板没有「查看/清空已记住的域」的入口（D9③ 排除）；若真人别扭，补那一行时它占 65 的一行、并需要一份列表编辑控件。
- `xdg-open` 缺位无回落（`gio open` / `kde-open` / `dbus-send` 一律不做，D10① 的代价 ⑵）。
- 双 fork ⇒ 无退出码 ⇒ 无「已打开」回执（D10① 的代价 ⑴）。
- `?1003` 档下悬停线不可得（D7① 的代价）；修法是把归属判据从「整档」细化到「Ctrl 让位」，那要改 F6 那条既有判据，跨 `TERM.06` 一棒，不在本期。
- **框架 tooltip 的半成品**（F10：有计时、无 hover 起始接线、无气泡绘制）是一条真实的框架缺口，本棒绕开它而不是补它。按 AGENTS §5.1 那属「可组合的体验类能力」与「事件链路」的交界处——派不派发给 Aurora 侧由人定，本稿只登记读数。
- 状态栏件未开工（F11）⇒ 目标条位置是**暂定**；那一棒落地后要重新裁 D6。
- ConPTY/SSH/串口会话的 URL 行为与本地腿一致（识别只吃副本行 + 目标只吃 `hyperlink_target`），但真机走查第 1–8 条需在 SSH 腿复跑一次才算数。
- `SPEC.FEAT.INTERACT.05` 无 CI 工作流（AGENTS §6 长期那条），结项读数只在本地。

**教训（本棒一手，写给下一棒）**：

- **探针有副作用**。本棒的一次 `xdg-open` 探针在用户桌面真的开了一个浏览器窗口，我没有擅自关闭它（怕丢用户已有标签页）。教训是**先设计可注入的接缝，再谈真触达**：凡「打开外部程序 / 写剪贴板 / 发通知」这类跨出进程的腿，自动化一律走替身（本棒 D11 的直接来源）。这条与 `codespec/UI_CLIPBOARD.draft.md` §8 那条 pty 探针教训同族（「先怀疑证人自己的执行环境」）。
- **成本读数要如实报，哪怕它削自己的论据**。F16 的量级（3 µs/屏）说明「每帧预扫太贵」站不住，于是 D4 的论证只能也必须落在状态面上。把一个不成立的效率论据写进裁决，代价是下一棒照着它做第二次决策。
- **界面像素断言的期望值要从落笔件反推，不能写死一枚数**。批 2 的 itest 首跑 19 案里 9 案红，每一红的根因都在**证人自己**而不是被测件：窗口档位与 `preflight` 门槛不同源（19 案全在门槛处假绿成「hover 到了空处」）、下划线按「20 枚像素」写死而 `decoration_rects` 实际给的是「1 物理像素 × 列数 × 单宽」、「单击一格即抬起」在 `ui::row_spans` 的口径下是**空表**却拿来当选区证人、同格第二次点击被派发器盖章成双击进了 `Word` 档。教训的形状与 `codespec/UI_SELECTION.draft.md` 那条同族：**先怀疑证人自己的执行环境**，再断被测件。

---

*版本：本稿随 `CHANGELOG.md` v0.119 入册；§0 读数全部取自 2026-10-12 本沙箱，Windows 腿除注明外未验证。*
