# OSC 133 命令块设计稿（`SPEC.FEAT.INTEG.01`）

> **状态**：**评审收口（2026-10-21）**——§3 的 D1–D12 按稿面**推荐档逐条自决收口**（用户离场，常存授权「后续再遇到问题则你自行按照推荐去做」；AGENTS §4.1 第 2 条那句「等开工指令」由该授权替换为「按推荐自决 + 入册」）。每条推荐都带着 §0 的一手读数与代价，人复核时若要改判，按 §4.2 第 11 条回写裁决即可，本稿不改已收口的编号。裁决入 `codespec/SPECIFICATIONS.md` §7 为 **7.123**，版本记 `CHANGELOG.md` **v0.123**。
> **落地进度（2026-10-21 收口）**：批 1／批 2／批 3 已全部落地并本地提交——设计稿 `b1fd263`，纯逻辑与来源 `89fe12a`，界面与设置 `deeb749`，单测 `d910044`，集成九案 `8aa1756`，证人补强 `cf20d46`，文档回写为最后一笔。结项读数：CTest **92/92**（9.26 s，注册 95 含 3 条 etest）、`borealis_test_runner` 全跑 **1175 案**全绿（v0.122 基线 89／1133）。**真机走查九条一条未做**（§7），故本稿与 `SPEC.FEAT.INTEG.01` 的现状句只称「纯逻辑与无头链路已证」，不称「可用」。
> **配套高保真草图**：`codespec/UI_INTEG_OSC133.draft.svg`（矢量源）/ `codespec/UI_INTEG_OSC133.draft.png`（1× 出图）。屏 A–D 的圈号 1–8 即 §2 的判据标号；SVG 为事实来源，改图直接改 SVG。
> **与既有视觉稿的关系**：`codespec/UI_COMMANDS.draft.svg` 屏 4 那张右键菜单是**终态图**（复制 / 粘贴 / 全选 / 打开链接 / 分屏 / 重命名 / 广播组 / 会话日志 / 关闭）。本棒在「复制」之后插入一条**条件出现**的「复制上一命令的全部输出」，于是终态图的条目集合需要一次并稿；本稿只登记这个差异（§8 第 6 条），不在这一棒重绘那张终态图。设置页新增的一行按 `codespec/UI_SETTINGS.draft.md` 的终端域表回写（§8 第 7 条）。
> **上游依据**：`SPEC.FEAT.INTEG.01` 原文（`codespec/SPECIFICATIONS.md` §4.7：解析 OSC 133 的 prompt / command start / command executed / output 边界标记；基于标记提供「复制上一命令的全部输出」（不带提示符与命令行回显）、命令块悬停分隔视觉（可选开关）、失败命令（exit code ≠ 0）输出行高亮提示）、`codespec/ARCHITECTURE.md` §10.4 的同一句话、裁决 **7.21④**（OSC 133 与 OSC 7 当时只作来源预埋，消费行为留 M4）、裁决 **7.32②**（复制口径：行分隔 LF、双宽字符不产半个字、行号越界跳过）、裁决 **7.38⑤／7.39①②**（存储行序、顶边位移与选区折算）、裁决 **7.116 D6** 与 **7.119 D12①**（`plan_right_click` 的前两次签名改动、菜单「不留点了什么也不发生的空位」）。
> **既成地基（本棒只消费，不新建）**：OSC 133 的**解析腿已落**且**UI 侧零消费**——`term::PromptMarker` 与 `OscState::prompt_marker` / `command_exit_code` 在 `include/borealis/term/osc.h`，派发在 `Terminal::apply_prompt_marker`，`utest_terminal_osc` 已有 A/B/C/D 与缺退出码的用例；但那里只留**最近一次标记**，没有行号、没有历史、没有退出码的有无，`src/ui/` 与 `include/borealis/ui/` 全文 grep `PromptMarker` / `command_block` **零命中**（F1）。本棒把「有来源、无消费方」补成命令块记录与三层界面消费，不新增第二套 OSC 解析。

---

## 0 地基实测（2026-10-21 本沙箱，POSIX 腿 + 源码/用例读数；Windows 腿一律标未验证）

形态不是推演出来的。F1–F18 每条都有本沙箱读数；外部语义规范另标来源。

| # | 读数 | 对本稿的约束 |
|---|---|---|
| **F1** | **有来源无消费**：`PromptMarker` 五档（`None/PromptStart/PromptEnd/CommandStart/CommandExecuted/OutputStart` 之外还有 `None`，共六值）与 `OscState::command_exit_code` 已在公共头；`src/ui/`、`include/borealis/ui/` 对 `PromptMarker`、`command_block` 的 grep **零命中**；`OscState` 的消费方只有 `src/main.cpp` 两处取标题 / 目录 | 缺口精确到「**没有记录命令块的行区间，也没有界面消费**」，不是「OSC 133 没解析」。命令块记录件与三层界面（菜单、底色、分隔线）全部落在这一条接缝上，不需要重写解析器 |
| **F2** | **P 的既有语义是错的**：现码把 `133;P` 记成 `PromptMarker::OutputStart`，注释称其参数携 `Cwd=`（`src/term/terminal.cpp` 的 `apply_prompt_marker`）。而 OSC 133 的扩展语义把 `P` 定义为 **Prompt Part Start**（`k=r` 为右侧提示符），`C` 才是 Output Start：Contour 的扩展表、WezTerm issue #115 的脚本样例、Warp 的集成脚本（只发 `A/B/P`）与 Ghostty 在 ble.sh 下「用 P 代替 A」的提交说明都指向这一条 | 若沿用现映射，右侧提示符会把输出起点提前到提示符行，复制结果会带整段提示符——这正是本棒最不能出的错。**本棒必须把 P 的映射改正**，且命令块只在 `C` 上开输出起点；`P` 本身不进块（D2①） |
| **F3** | **只留最近一次**：`OscState` 是单份快照，`apply_prompt_marker` 逐标记覆盖 `prompt_marker` 与 `command_exit_code`；`reset_to_default()` 整体清空 `osc_state_`。`utest_terminal_osc` 的 `osc_133_records_prompt_boundaries_and_exit_code` 断的也是「最后一个是哪个」 | 命令块必须新增**一份有界的块历史**，不能把 `OscState` 扩成表（它是标题 / 目录 / 剪贴板的共用快照，改动会牵动三族消费方）。`OscState` 的字段保持原义，块历史是并列的一份记录 |
| **F4** | **网格行号的唯一稳定坐标是「绝对行」**：`grid::Storage` 的 `line(i)` 以 0 为最顶；四条路径推进 `dropped_lines()`——`scroll_up` 溢出、`set_rows` 溢出与顶部补空白、`scroll_down` 无历史时的顶部空白、`clear()`；带内滚动不记。于是同一份内容在存活期间满足 `storage_index + dropped_lines` 恒定 | 命令块不能存「存储行号」（一次溢出就整体漂移），也不能存「视口行」（回看就错）。记录件存**绝对行**，绘制与复制各自用当下的 `dropped_lines` 折算回存储行；这是一条算式，不是第二份状态 |
| **F5** | **游标给绝对行的换算**：`Terminal::cursor()` 是视口内坐标；`Storage::total_lines()` 含 scrollback 与视口，视口即最后 `visible_rows()` 行。故标记时刻的绝对行＝`total_lines − visible_rows + cursor.row + dropped_lines`。`Terminal::main_grid()` 与 `active_grid()` 都在公共头 | `apply_prompt_marker` 在状态机内部，能直接拿到主网格与游标；换算写在一处私有件里，块记录件本身不依赖 `Terminal`/`grid`（纯逻辑可单测） |
| **F6** | **备屏不能记录**：`Terminal::active_grid()` 在备屏时返回 `alt_`；`set_alternate_screen` 进入时清 `alt_`、退出时也清 `alt_`，主屏内容原样保留。全屏程序（vim/less）通常不发 133；即使发了，也不该把主屏的命令块弄脏 | 记录件只在**主屏**接收标记；备屏期间的标记一律忽略。切备屏不清主屏块表，切回来照旧可用 |
| **F7** | **会话侧已有「锁内小快照」先例**：`Session::osc_state()` 与 `hyperlink_target()` 都是短临界区返回副本；`Session::read` 交的是 `active_grid` / `cursor` / `modes`，签名被搜索、视口、日志等多个调用点共用 | 命令块用 `Session::command_blocks()` 返回副本（与 `osc_state()` 同形），**不改** `read` 的签名。代价是每帧多一次短锁；收益是既有调用点零改动 |
| **F8** | **复制件的现状**：`ui::copy_text(storage, Selection, CopyOptions)` 逐行走 `row_spans` 与文件私有的 `row_text`；`row_text` 把整段列区间的空格原样写出（只按选项剥行尾空白）；`Selection` 的端点是**存储行序**；行分隔 LF；行号越界跳过。没有「直接按行区间取文本」的公共入口 | 命令块复制不能借 `Selection` 硬凑：输出末行常常是「D 游标在列 0 的空行」或「列 > 0 的半行」，硬凑要么多一个空行、要么把整行网格填充带进剪贴板。本棒需要把 `copy_text` 的行拼接拆成可复用的 `copy_spans`（D4①），并对块输出逐行裁掉**网格填充空格** |
| **F9** | **右键菜单的签名已改过两次**：`plan_right_click(RightClickAction, bool has_selection, LogMenuState, LinkMenuState)`；次序为 复制 → 粘贴 → 打开链接（条件） → 会话日志（条件）。`MenuCommand` 的枚举注释明写「其余各项不在菜单里留点了什么也不发生的空位」 | 本棒加「复制上一命令的全部输出」是**第三次签名改动**，必须点名入册（D3）。它与会话日志 / 打开链接同形态：**条件不满足就不出现**，不是灰显。条目插在「复制」之后，前两条与后两条的相对次序不变 |
| **F10** | **复制请求的现有闸门是一枚 bool**：`copy_pending_` 由选区的 Release（copy-on-select）、右键菜单的 Copy、右键 Paste 三态里的 Copy 置位，统一在 `on_frame` 的 `flush_copy_request` 里落地；`selected_text()` 在会话锁内调 `copy_text` | 新增第二种复制来源后，两枚 bool 会允许「同一帧两个来源都置位」这种无定义状态。本棒把 `copy_pending_` 折成三值请求 `CopyRequest{None, Selection, CommandOutput}`（D10①），落地仍在帧边界，事件回调里不读剪贴板也不写剪贴板 |
| **F11** | **绘制的分层顺序**：`on_paint` 先整盒铺默认底色，再逐行 `paint_row`（`layout_row` 的 run 表只在 `background != 默认底色` 时填色带）、光标、回看指示条，随后是链接悬停层、两张状态卡、dead-session、广播角标。`bands_for_screen_row` 只交命中 / 当前命中 / 选区三种区间 | 失败命令的底色可以在 `paint_row` 里、`layout_row` **之前**预填整行矩形：默认底色的 run 不会覆盖它，显式 cell 底色与选区色带仍按既有次序盖在上面。不需要给纯布局件加字段（D5①） |
| **F12** | **悬停纪律已有两条先例**：链接悬停 `hover_link_` 在「本帧有任何 damage」时整条摘掉，且在上报档 `mouse_tracking != Off` 时不再更新；`apply_interaction_options` 在白名单 / 确认档变化时清悬停与菜单里那一条。命令块分隔线是**纯装饰、不承诺点击** | 分隔线可以比链接悬停更宽松：只要本地腿收得到 Move，就按落点更新；`?1003` 档下 Move 整笔归远端，分隔线拿不到（与链接同一边界）；输出持续刷新时**不摘**分隔线（块顶没变，摘了会闪）。设置关掉时立即摘（D6①） |
| **F13** | **右键菜单的上屏件是现成的**：`open_context_menu` 按 `plan.items` 建 `au::Button`，文案走 `menu_label(command, logging)` 的 switch，词条在 `settings_i18n.cpp` 的 `menu.item.*`；`menu_link_` 在右键那一刻把链接命中**存下来**，菜单开着期间不重扫 | 新条目只加一个 `MenuCommand`、一条 `menu_label` 分支、一条 i18n 词条；「上一命令」的块也在右键那一刻**整份存下来**（`menu_block_`），点击时不再按当前状态重选（D3②） |
| **F14** | **设置键的两处计数闸 + 六处同步点**：`utest_settings_catalog` 钉 schema 叶子 − 5 条例外叶 ＝ 核对表行数且 `keys.size() == 68U`，另钉 `ConsumerStatus::Absent` 行数 `== 19U`；终端域的新键要同改 `config/settings.h`、`config/store.cpp`、`config/form_transfer.cpp`、`ui/settings_catalog.cpp`、`ui/settings_i18n.cpp`、`main.cpp::make_interaction` | 新键 `terminal.command_block_separator` 占 68→**69**，`Absent` 保持 **19**（它是 Wired，消费方＝本棒的悬停层）。不新增数据叶，故例外名单仍 5 条。六处漏一处即「面板能改、视口不认」或「能存不能读」 |
| **F15** | **源列表是显式列出**：`src/CMakeLists.txt` 的 `borealis_core` 逐文件列出 `term/*.cpp` 与 `ui/*.cpp`，不是 GLOB；测试源则经 `cmake/BorealisTests.cmake` 自动注册 | 新增 `term/command_block.cpp` 与 `ui/command_block.cpp` 必须同时进源列表，漏一处即链接期断裂。本棒不新增平台腿、不改平台文件 |
| **F16** | **本仓没有 OSC 133 的 shell 集成安装器**，也没有能在本沙箱稳定产出 A/B/C/D 的交互 shell 脚本；既有 OSC 133 用例全部以 `Terminal::feed` 直接喂字节。无头集成的现成 harness 在 `tests/integration/itest_url_link.cpp`：`FakeConnection::deliver` + `HeadlessSurface` + 真实指针派发 + 像素差分 | 本棒的自动化证人只能走 `Terminal::feed` 与 `FakeConnection::deliver`；**不依赖真实 shell**、不启动外部程序。真机走查列出独立清单（§7），未走完不称「可用」 |
| **F17** | **像素证人的口径**：`itest_url_link` 用「带内等于 accent 的像素数」判链接下划线，默认色板各通道为 0、accent 为固定值；`itest_command_palette` / `itest_render_viewport` 用帧差分与颜色集合 | 失败底色可以同样用「某一行内等于公式色的像素数」判；分隔线用「块首行上沿一行内 card_line 色像素数」判。两者都不依赖字形栅格细节 |
| **F18** | **当前基线**：`cmake --build --preset linux` 无待编；`ctest --preset linux -N` 注册 **92** 项（截至 2026-10-21）。`src/main.cpp` 不编进测试 runner（裁决 7.83） | 新用例按 `utest_` / `itest_` 前缀进 CTest；装配层（`main.cpp` 的 `make_interaction` 搬字段）**没有自动化证人**，与既有交互键同一条在册代价 |

---

## 1 范围与边界

**本期交付**：

1. **一份纯逻辑的命令块记录件**（`include/borealis/term/command_block.h` + `src/term/command_block.cpp`，namespace `borealis::term`，不含 Aurora / `grid` / `config` 类型）：`CommandBlockTracker` 按 A/B/C/D 推进一份有界块历史；块存**绝对行**、输出起止的行列、退出码的**有无**；`last_completed()` 与 `block_at(row)` 是两条只读查询。容量 256，满表淘汰最旧，序号递增不复用。
2. **标记语义改正**（D2①）：`A` 开块、`B` 记输入区起点、`C` 开输出起点、`D` 收块并记退出码；`P` 改正为 `PromptMarker::PromptPart`（右提示符分段），**不参与命令块**；备屏标记忽略；孤立 `B/C/D` 忽略；未收 `D` 的块标 `superseded`，不参与复制。退出码接受可选负号（`D;-1` 记为 -1），`D` 无数字时 `has_exit_code = false`。
3. **来源接线**：`Terminal` 持有记录件，`apply_prompt_marker` 在锁内喂绝对行；`ED 3`（`main_.clear()`）与 RIS 清空记录件；`Terminal::command_blocks()` 与 `Session::command_blocks()` 提供锁内副本。
4. **复制上一命令输出**（D3①／D4①）：右键菜单在「复制」之后新增一条**条件出现**的「复制上一命令的全部输出」；只有「最近一条已完成且有可复制输出」的块出现该条，点击后把块输出（不含提示符与命令行回显、不含网格填充空格）经既有帧边界复制到剪贴板。
5. **失败命令输出行高亮**（D5①）：已完成、带退出码且 `exit_code != 0` 的块，其输出行铺一层低强度红底；提示符行与命令行回显行不铺；选区、搜索命中、显式 cell 底色仍在其上。
6. **命令块悬停分隔线**（D6①）：新键 `terminal.command_block_separator`（bool，缺省关，即时生效）；开启时，指针落在任一命令块内，就在该块**提示符首行上沿**画一条 1 物理像素的 `card_line` 分隔线；纯装饰、不写命中盒、不吃点击；块首行滚出可见区则不画。
7. **一份纯逻辑的界面折算件**（`include/borealis/ui/command_block.h` + `src/ui/command_block.cpp`）：绝对行 → 存储行区间；块输出 → `RowSpan` 表；失败块 → 可视行区间表；输出复制 → 文本。折算件不含 Aurora，全部可脱界面单测。
8. **复制件的可复用化**：把 `ui::copy_text` 的行拼接拆出 `ui::copy_spans(storage, spans, options)`；`copy_text` 的行为逐字节不变，命令块复制走同一条拼接与三项 `CopyOptions`。

**本期不交付**（§8）：shell 集成的安装 / 探测 / 引导、命令块导航与跳转、命令耗时的记录与显示、退出码文本的展示、把命令行回显单独复制的入口、全量 scrollback 的块索引、持久化块表、备屏里的命令块、多条链接菜单之外的其余终态菜单条目、Windows 真机验证、真机走查。

**关键前提（口径不是本稿新造的）**：

- **本棒是消费方**：OSC 133 的解析、`OscState` 的既有字段、`Storage` 的行号规则、`Session::read` 的锁形态都不重写。新的只有「块历史」与「界面消费」。
- **失败高亮不是模态提示**：它是一条底色，不占命中盒、不弹卡、不阻止任何操作；入口只有视觉。
- **复制在帧边界落地**（AGENTS §4.5 第 25 条）：右键点击只置请求与存下块快照，读网格、拼文本、写剪贴板都在下一帧的 `on_frame`。
- **无真实 shell 证人**：所有自动化都喂字节，真机走查另列（§7），未走完不称「可用」（AGENTS §4.4 第 33 条）。

---

## 2 草图与判据对应

草图四屏（A 命令块与失败高亮 / 悬停分隔线 · B 右键菜单两条形态 + 无块形态 · C 坐标与区间模型 · D 设置页终端页新增一行），图中圈号 1–8 即下表判据：

| 标号 | 判据 | 对应需求 |
|---|---|---|
| 1 | 一条完成的命令块在屏上分成三段：提示符（A→B）、命令行回显（B→C）、输出（C→D）。失败块（`exit code ≠ 0`）的**输出行**铺一层低强度红底，提示符与命令行回显仍是默认底色；有显式背景色的 cell 与选区色带盖在红底之上 | 原文「失败命令（exit code ≠ 0）输出行高亮提示」 |
| 2 | 右键菜单在块可用时为：复制 → **复制上一命令的全部输出** → 粘贴 →（命中链接时）打开链接 →（日志可用时）会话日志；没有可用块时新条目**不出现**，其余条目逐字节不变。块输出的复制结果不含提示符、不含命令行回显、不含行尾网格填充空格；命令 `cd` 这类无输出块不出现该条 | 原文「复制上一命令的全部输出（不带提示符与命令行回显）」 |
| 3 | 输出区间从 `C` 的游标位置起、到 `D` 的游标位置止：`D` 在列 0 时该行不算输出（命令以换行结尾的常态），`D` 在列 > 0 时该行的前半段算输出（命令没打换行的半行）；输出首行从 `C` 的列起算。`P` 只表示提示符的右段，**不改变**输出起点 | 原文的边界语义；F2 的改正 |
| 4 | 开启 `terminal.command_block_separator` 后，指针落在命令块内的任意一行，块首行上沿出现一条 1 物理像素的 `card_line` 分隔线；指针移出块、设置关掉、块被清空时线消失；块首行在可见区之上则不画。它不写命中盒，线下的文字照常可选、右键、Ctrl+点击 | 原文「命令块悬停分隔视觉（可选开关）」 |
| 5 | 窗口开了鼠标上报：`?1000`/`?1002` 档下无键 Move 仍归本地，分隔线照常可悬停；`?1003` 档下 Move 整笔归远端，分隔线不可得——这是与链接悬停同一条上报边界，不是漏判。右键菜单与复制不受影响（右键恒归本地） | F12、F6 的既有归属判据 |
| 6 | 回看与溢出：块的行号按绝对行保存，滚上滚下、scrollback 溢出、resize 都不漂移；块整体滚出存储顶端后，复制条目不出现、失败高亮不画、分隔线也找不到块；`ED 3` 与 `RIS` 清空全部块；备屏里收到的 133 标记不产块，切回主屏后旧块照旧 | F4、F5、F6 |
| 7 | 设置页「终端」页多一行开关（行名「悬停命令块时显示分隔线」，缺省关，即时徽标，无延后徽标）；`settings_catalog` 的键数 68→69，`Absent` 仍 19 | 原文「可选开关」；F14 |
| 8 | 上游不发 OSC 133 时，本棒三件全部静默：菜单不多条、无红底、无线；不做任何「猜命令块」的启发式。上游只发 `A/B/P` 而不发 `C/D`（Warp 一类）时同样不产块——复制条目按 fail-closed 不出现 | D2①、D3① 的边界 |

---

## 3 裁决（D1–D12；下表「推荐」列即本棒据以开工的档位，理由与代价一并入册为裁决 7.123）

| # | 待定项 | 选项 | 推荐 | 理由与代价 |
|---|---|---|---|---|
| **D1** | 命令块存什么坐标、留多少 | ① **绝对行 + 有界块表**：`absolute = storage_index + dropped_lines`；容量 256，满表淘汰最旧；字段含 `serial`、`prompt_row`、`prompt_end_row`、`output_start_row/column`、`output_end_row/column`、`exit_code`、`has_output_start`、`has_exit_code`、`completed`、`superseded`；② 存存储行号并在每帧折算；③ 把块区间挂到 `grid::Row` 的侧表上 | **①** | ② 每次顶边位移都要改全部块，且「同一份内容在两次折算之间又滚了」会分叉；③ 要为网格增加一份只在主屏有效的侧表，还要在 `Row` 拷贝、`clear()`、resize 三处同步，成本远大于收益（复制与高亮都只在主屏消费）。① 的代价是：记录件必须知道顶边位移读数，UI 每次消费都要做一次减法；块被淘汰或滚出后只剩「不可用」这一档，不指向别的内容。容量 256 按「一屏 50 行、一次命令 3 行」约合 4 屏历史，够覆盖可见回看；超出后旧块静默失去高亮与悬停，不补第二套索引 |
| **D2** | A/B/C/D/P 各喂什么、`P` 怎么办 | ① **改正 P**：`A` 开块并记提示符首行，`B` 记输入区起点，`C` 开输出起点（行 + 列），`D` 收块（行 + 列 + 有无退出码），`P` 改正为 `PromptPart` 且不进块；备屏忽略；孤立标记忽略；新 `A` 把未收 `D` 的旧块标 `superseded`；退出码接受可选负号；② 沿用现映射（`P` = 输出起点）；③ 不认 `P`，只把它的 `Cwd=` 忽略 | **①** | F2 是决定性的：`P` 是右提示符分段，Warp 与 ble.sh 都会在提示符阶段发它；沿用②会把提示符整段算进「输出」，复制出来的第一行是 `user@host:~$`，正是需求明确要排除的东西。③ 虽然不误判块，但 `OscState::prompt_marker` 仍会把 `P` 报成 `OutputStart`，下一棒照样会踩。代价：`PromptMarker` 的 `OutputStart` 改名 `PromptPart` 是一次公共枚举改名（全仓 4 处引用，含 2 条用例），必须在裁决与 `CHANGELOG` 里点名；不认 `P` 的集成（只发 A/B/P）不产块，这是 fail-closed 的边界 |
| **D3** | 「复制上一命令输出」从哪进、何时取块 | ① **右键菜单条件条目**，插在「复制」之后；右键那一刻把最近一条已完成块**整份存下**（`menu_block_`），点条目时不再按当前状态重选；块不可用（无 `D`、无 `C`、无输出、已滚出）时条目**不出现**；② 恒显示、不可用时灰显；③ 键盘快捷键 + 命令面板 | **①** | 与会话日志 / 打开链接同形态（F9、F13）：能力不存在时不留一条永远灰着的条目；而「复制」的灰显是**瞬时的选区状态**，两者不是一回事。存下整份块而不是在点击时重选，是为了让「用户看到菜单的那一刻指的是哪条命令」与「点下去复制哪条」同源——输出可以在菜单开着时继续增长，点击时重选会指到新块。代价：`plan_right_click` 第三次签名改口，调用点全要跟着改；菜单条目集合再增一条，终态图待并稿（§8 第 6 条） |
| **D4** | 块的输出区间怎么折成文本 | ① **从 C 游标到 D 游标的逐行区间**：首行从 `output_start_column` 起，末行在 `D` 列 0 时取「上一行整行」、列 > 0 时取 `[0, end_column)`；每行裁掉行尾的**网格填充空格**；中间行整行；行间 LF；不合成结尾换行；② 从 C 行到 D 行整行，事后再剥尾部空行；③ 从提示符首行到 D 行，再按 A/B/C 切掉首段 | **①** | ② 在多行输出且 D 在列 0 时会多一个空行，还会把整行网格填充带进剪贴板；③ 要重新识别提示符与回显的列边界，而 `C` 已经给了唯一权威的起点。① 的代价：命令显式打印的行尾空格与网格填充无法区分，一律裁掉；结尾换行的个数无法用「行区间 + LF 拼接」精确表达（`a\n\n` 会少一个结尾换行）。这两条写进 §8，不用正则或猜列去补。实现上需要把 `copy_text` 的拼接拆成 `copy_spans`（F8） |
| **D5** | 失败输出行怎么高亮 | ① **整行底色**：已完成 ∧ `has_exit_code` ∧ `exit_code != 0` 的块，输出行在 `layout_row` 之前预填 `mix_half(mix_half(basic[1], default_background), default_background)`（约 25% 红与底色）；提示符 / 回显行不填；选区、搜索命中、显式 cell 底色照旧覆盖；② 左侧竖条标记；③ 给整个块描边 | **①** | ② 要在行首挤占一列，终端内容不能被界面挤掉；③ 会把提示符与回显也框进去，与「输出行高亮」的原文不符。① 是唯一不动布局、不动命中、不占列的形态，且失败行的可读性由「低强度」保证。代价：某些主题的 `basic[1]` 与底色接近，红底会不明显；公式取 25% 是刻意的低强度，不另做主题专用色（补一套就要进主题配置，超出本条） |
| **D6** | 分隔线什么时候画、开关默认哪档 | ① **悬停画、开关缺省关**：`terminal.command_block_separator` 为真时，指针落在块内 → 块首行上沿 1 物理像素 `card_line`；不写命中盒；`?1003` 档拿不到 Move 时不画；块首行不可见不画；设置关掉立即摘；输出刷新不摘；② 常驻画每条块的边界；③ 悬停时画在输出首行上沿 | **①** | 需求原文把「可选开关」写在分隔视觉上，② 是另一条产品决定而不是本棒能替用户做的；③ 会漏掉提示符与命令行回显，且块首行与输出首行在常见集成里只差一两行，语义上「块的边界」就是提示符上沿。缺省关符合本仓「新增视觉默认不打扰」的既有取向（广播角标、搜索命中都是显式操作后才出现）。代价：用户要开一次才看得到；`?1003` 档与链接悬停同一边界；纯装饰线在浅色主题下要取 `card_line` 而不是前景色 |
| **D7** | 块表怎么进视口、每帧怎么取 | ① **`Session::command_blocks()` 锁内副本**，`on_frame` 每帧取一次存 `blocks_`；绘制用 `blocks_` + 副本的 `dropped_lines` / `window_top`；复制用右键存下的 `menu_block_` + 帧边界里 `Session::read` 的 `grid.dropped_lines`；块表变化（含 `D` 到达而画面无 damage）单独标脏；② 扩 `Session::read` 的签名把块一并交出；③ 把块塞进 `ScreenMirror` | **①** | ② 的 `read` 被搜索、视口、日志等多个调用点共用，为一条新消费改签名会把「锁内取什么」的边界摊到所有调用方；③ 让副本承担与可见行无关的历史状态，且 `ScreenMirror` 的职责会被撑破。① 的代价是每帧多一次短锁与一次 256 上限的向量拷贝（块小、可忽略）；「块快照」与「网格快照」可能差一帧，但块存绝对行、内容按 `dropped` 折算，最坏只是新块晚一帧到，绝不会把 A 块的区间指到 B 块的内容上。复制路径的块是右键时存下的整份快照，不依赖 `blocks_` 的时序 |
| **D8** | 失败底色与分隔线在绘制序里的位置 | ① 失败底色在 `paint_row` 的 run 表之前预填；分隔线在正文、光标、回看指示条之后，链接悬停层与两张状态卡之前；② 分隔线画在最上层；③ 失败底色并入 `RowBands` | **①** | ② 会让分隔线压住 dead-session 卡与重连卡——状态卡是更重的信息；③ 要给 `cell_layout` 的纯件加一个新字段并为它写覆盖，而 `paint_row` 的一次预填就能得到同一结果（F11）。① 的代价：失败底色在显式 cell 背景与选区之下，某些程序自带的底色会完全盖住它——这是正确的层次（内容自己的底色优先） |
| **D9** | 悬停分隔线的设置键形态 | ① **bool 开关**：`terminal.command_block_separator`，缺省 `false`，`Wired ∧ Immediate`；② 三档（关 / 悬停 / 常驻）；③ 不设键、常驻 | **①** | 需求只要求「可选开关」，① 是它的最小形态；② 的「常驻」没有需求背书，且会引入一条新的界面状态；③ 直接违背「可选」。代价：面板多一行、schema 多一键（68→69），六处同步与计数闸都要跟；广播只到当前标签（与链接档位同一条既有边界，§5 第 3 条） |
| **D10** | 两种复制来源怎么排队 | ① 把 `copy_pending_` 折成 `CopyRequest{None, Selection, CommandOutput}` 一枚三值请求，帧边界统一落地；② 两枚 bool；③ 右键回调里直接读网格并写剪贴板 | **①** | ② 允许「同一帧两个来源同时置位」这种无定义状态，落地次序会变成隐式约定；③ 把读锁、拼文本、剪贴板 IO 放进事件回调，违背 AGENTS §4.5 第 25 条，也会让「同一帧重绘」不再幂等。① 的代价：要改 `copy_pending_` 的 5 处引用（Release、右键 Copy、菜单 Copy、菜单新条目、flush），机械但必须一次改完；`menu_block_` 在落地后清空，避免下一次复制借到旧快照 |
| **D11** | 退出码的有无与符号 | ① `D` 后无数字 → `has_exit_code = false`，不参与失败高亮；`D;0` → 有码且成功；`D;-1` → 有码且失败；数字越界钳到 `int32` 上限/下限；② 沿用 `osc::decimal`（负数读成 0）；③ 只认 0..255，其余按无码 | **①** | ② 会把 `-1` 当成成功，正是「失败命令不高亮」的漏判；③ 把信号退出（128+）与负数一律藏掉，也没有需求依据。① 的代价：要在大写 `D` 分支里写一个小解析（可选 `-`/`+`、至少一位数字、钳制），并用「有无」而不是「值是否 0」区分「没报」与「报 0」——这正是失败高亮与复制可用性都需要的两态 |
| **D12** | 失败高亮是否可配 | ① **不可配、恒定开**；② 跟随主题 / 设置开关；③ 只在缺口处画左侧竖条 | **①** | 需求原文只把「悬停分隔视觉」写成可选开关，失败高亮是「提示」本身，没有要求可关；把它做成开关会多一枚键、多一条「关了之后失败命令完全看不出来」的路径。① 的代价：不想看红底的人只能改主题色；若将来真有需求，再按 §4.2 第 11 条加键，不在本棒预留空位 |

---

## 4 文件布局与批次

**批 1（来源与纯逻辑，无界面）**

- 新增 `include/borealis/term/command_block.h` / `src/term/command_block.cpp`：`CommandBlock`、`CommandBlockTracker`、`kMaxCommandBlocks = 256`。
- `include/borealis/term/osc.h`：`PromptMarker::OutputStart` 改名 `PromptPart`，注释改为右提示符分段（D2①）。
- `src/term/terminal.cpp` / `include/borealis/term/terminal.h`：`tracker_` 成员；`apply_prompt_marker` 计算绝对行并喂块；`D` 的带符号退出码解析；`ED 3` 与 `reset_to_default()` 清块；`command_blocks()` 只读入口。
- `include/borealis/session/session.h` / `src/session/session.cpp`：`command_blocks()` 锁内副本。
- `src/CMakeLists.txt`：`term/command_block.cpp`（F15）。
- 测试：新增 `tests/unit/utest_command_block.cpp`；改 `tests/unit/utest_terminal_osc.cpp`（P 改正、带符号码、备屏忽略、ED3/RIS 清块、绝对行不漂移）；`tests/unit/utest_session.cpp` 加一条副本。

**批 2（界面与设置）**

- 新增 `include/borealis/ui/command_block.h` / `src/ui/command_block.cpp`：`BlockRowRange`、`command_output_spans`、`failed_output_spans`、`copy_command_output`。
- `include/borealis/ui/selection.h` / `src/ui/selection.cpp`：拆出 `copy_spans`，`copy_text` 委托（行为不变）。
- `include/borealis/ui/right_click.h` / `src/ui/right_click.cpp`：`MenuCommand::CopyCommandOutput`、`CommandOutputMenuState`、`plan_right_click` 第五参；条目位置与出现条件（F9）。
- `src/ui/terminal_view.h` / `src/ui/terminal_view.cpp`：`blocks_` / `failed_spans_` / `hover_block_serial_` / `menu_block_`；`CopyRequest`；悬停更新、分隔线、失败底色、菜单条目与文案；帧边界复制。
- 设置六处：`config/settings.h`、`config/store.cpp`、`config/form_transfer.cpp`、`ui/settings_catalog.cpp`、`ui/settings_i18n.cpp`、`main.cpp::make_interaction`；`InteractionOptions::command_block_separator`。
- `src/CMakeLists.txt`：`ui/command_block.cpp`。
- 测试：新增 `tests/unit/utest_ui_command_block.cpp`；改 `tests/unit/utest_right_click.cpp`（第五参 + 新条目次序/出现条件）、`tests/unit/utest_settings_catalog.cpp`（69 / 19）、`tests/unit/utest_selection.cpp`（`copy_spans` 等价）。

**批 3（集成证人、文档与收口）**

- 新增 `tests/integration/itest_command_block.cpp`：无头 `FakeConnection` 喂 OSC 133 字节 + 真实指针派发，断复制文本、菜单两条形态、失败底色像素、分隔线像素、`?1000`/`?1003` 两档、ED3 清空、回看不漂移。
- 按需更新 `itest_url_link.cpp` 的菜单期望（无块时仍为原条目；有块时多一条）。
- 全量构建与测试（`ctest --preset linux -E etest_` + runner 全跑），记录读数。
- 文档：`SPECIFICATIONS.md` §7 新增 7.123 与 `INTEG.01` 现状句；`CHANGELOG.md` v0.123；`PLAN.md` 的 INTEG.01 行；`ARCHITECTURE.md` §10.4 的消费登记；`UI_SETTINGS.draft.md` 终端域表；`UI_COMMANDS.draft` 的差异登记；本稿状态头与 §4 回填。
- 分层提交：纯逻辑 / 界面与设置 / 测试 / 文档，按 AGENTS §4.6 第 26、29 条。

**落地回填（2026-10-21）**：批 1 `89fe12a`（11 文件，含三件单测的初始版本）、批 2 `deeb749`（16 文件，界面与设置六处）、单测 `d910044`（5 文件）、批 3 集成 `8aa1756`（9 案）、证人补强 `cf20d46`（2 文件）、本笔文档回写。`itest_url_link` 不喂 OSC 133 块，菜单期望无需改动（全量跑通过）。全量读数与变异结果见 §6。**两处与稿面的偏差如实登记**：① §6 写的「`P` 改回输出起点 ⇒ 用例 1 红」在本实现上不成立——记录件拒绝同一块的第二个 `C`，故 `P` 落在 `C` 之后时旧映射也不会移动起点；真正的缺口是「只有 P 没有 C」，补 `utest_terminal_osc.osc_133_prompt_part_without_c_never_opens_output` 后该变异才转红。② 上游依据写的 `ARCHITECTURE.md` §10.4 现为 URL 检测，不是 OSC 133 的同一句话；消费登记改落新增的 §10.7，不改旧节号。

---

## 5 装配与接线（事实清单）

1. **来源**：`Terminal` 是唯一喂块的地方；块记录件是 `Terminal` 的成员，生命周期与会话相同；`reset_to_default()` 是唯一的重置入口（RIS），`ED 3` 单独清主屏块。
2. **会话**：`Session::command_blocks()` 与 `osc_state()` 同形——锁内取副本、锁外消费。视口每帧取一次；不扩 `Session::read` 的签名。
3. **设置广播**：`make_interaction(settings, line_ending)` 是唯一搬运点；`InteractionOptions` 加 `command_block_separator`，三处 `TerminalView` 构造点自动同值；`hooks.broadcast` 只覆盖**当前标签**的 pane，别的标签要等它成为当前标签并再改一次档才刷新。这与 `terminal.link_*` 的既有边界同形，本棒不扩（不是安全闸）。
4. **复制**：沿用既有 `Presentation::clipboard_write` → `ClipboardOutbox::write` 的回落；不新增接缝、不新增 IO。右键只置 `CopyRequest::CommandOutput` 并存块，帧边界读网格并落地。
5. **菜单**：`plan_right_click` 出条目；`menu_label` 出新文案；`settings_i18n` 出词条；`open_context_menu` 不需要为新条目改结构。
6. **绘制**：失败底色在 `paint_row` 预填；分隔线是 `on_paint` 的新一层；两者都只读 `blocks_` / `failed_spans_` 与副本的坐标读数，不读会话锁。
7. **无自动化证人的面**：`src/main.cpp` 不编进 runner（裁决 7.83），故「设置面板改一下 → 广播搬进 `InteractionOptions` → 悬停层认新值」这条装配链的最后一跳只有源码审查；与既有交互键同一条在册代价。

---

## 6 验收映射（三层证人 + 结项读数 + 变异自证）

**纯逻辑单测**

- `utest_command_block`：A/B/C/D 的正常块；`D` 无码 / `D;0` / `D;-1` / 越界码；`C` 后 `P` 不改输出起点；新 `A` 把未收 `D` 的块标 `superseded`；孤立 `B/C/D` 忽略；容量 256 淘汰最旧且序号不复用；`block_at` 的完成块上界与开放块下界；`clear()`。
- `utest_terminal_osc`：`P` 报 `PromptPart`（改正后的证人）；`ED 3` 与 RIS 清块；备屏标记不产块、切回主屏旧块仍在；scrollback 溢出后块绝对行折算到正确的存储行。
- `utest_ui_command_block`：多行输出 + `D` 列 0；`D` 列 > 0 的半行；首行列偏移；全块滚出 / 部分滚出；失败块区间只含输出行；`copy_command_output` 的裁剪与 LF 拼接。
- `utest_right_click`：无块 / 有块 / 块与链接 / 块与日志的条目集合与次序；无块时逐字节等于旧三条或四条。
- `utest_selection`：`copy_spans` 与 `copy_text` 等价。
- `utest_settings_catalog`：68→69，`Absent` 19；新键在终端页且为 `Wired ∧ Immediate`。

**集成 / 像素证人**（`itest_command_block`，须 `AURORA_BACKEND_HEADLESS`）

1. 喂 `A` → 提示符字节 → `B` → 命令行字节 → `C` → 输出字节 → `D;1`；右键 → 菜单出现「复制上一命令的全部输出」→ 真实命中链点击 → 泵一帧 → 剪贴板替身收到**恰好**输出文本，不含提示符与回显。
2. 同上但 `D;0`：菜单条目仍在（成功命令也有输出可复制），复制结果一致；失败高亮不出现。
3. `cd` 型空输出（`C` 后立刻 `D;0`）：条目不出现。
4. 无 `D` 的开放块：条目不出现；分隔线仍可悬停（块在）。
5. 失败块：输出行像素中公式色计数 > 0，提示符 / 回显行计数 = 0；选区盖在红底之上。
6. 分隔线：开关关 → 块首行上沿无 `card_line` 像素；开 → 悬停块内出现、移出消失；`?1000` 档仍出现；`?1003` 档不出现。
7. `ED 3` 后块清空：菜单条目消失、红底消失；`RIS` 同理。
8. 回看：块首行滚出可见区后分隔线不画，但右键复制仍能复制（块在存储里）；块整体滚出存储后条目不出现。

**变异自证（每项须有转红证人）**

- 把 `P` 改回输出起点：`P` 在 `C` 后不红（记录件拒绝同一块的第二个 `C`），改用「只有 P 没有 C」的终端用例转红（见 §4 偏差①）。
- `D` 列 0 时仍包含末行：复制文本多一个空行（红）。
- 失败区间把 `prompt_end_row` 也纳入：像素用例在提示符行出现红底（红）。
- 菜单条目恒出现：无块用例多一条（红）。
- 设置缺省改真：设置页 / 缺省用例红。

**结项读数**

- `cmake --build --preset linux -j 8` 通过（无新增警告）。
- `ctest --preset linux -E etest_` **92/92** 全绿（9.26 s；注册 95 含 3 条 etest）；`borealis_test_runner` 全跑 **1175 案**全绿（20.6 s；v0.122 基线 89／1133）。
- 新增/补强：三个新套件 30 案（`utest_command_block` 14 + `utest_ui_command_block` 7 + `itest_command_block` 9）+ 既有套件补 12 案。
- **五项变异自证（均已还原并复跑全绿）**：① `P` → `CommandStart`：`utest_terminal_osc.osc_133_prompt_part_without_c_never_opens_output` 红（首轮「`P` 在 `C` 后」不红，原因见 §4 偏差①）；② `D` 列 0 仍计末行：`utest_ui_command_block` 3 案 + `itest_command_block` 3 案红；③ 失败区间从 `prompt_end_row` 起：折算 1 案 + 像素 1 案红；④ 菜单条目恒出现：`utest_right_click` 4 案 + `itest_command_block` 4 案红；⑤ 设置缺省改真：`utest_settings_catalog.command_block_separator_is_a_wired_immediate_terminal_switch` 红。
- 不跑吞吐门禁：新增代码不在每帧扫描路径（每帧至多一次 256 上限的块拷贝与几行区间判断），沿用「无性能相关改动不伪造基准」的既有口径；若实现中不慎把块扫描放进逐格路径，须补基准再结项。

---

## 7 真机走查清单（UI 件未走完不得称「可用」，AGENTS §4.4 第 33 条）

1. 在 bash/zsh 配好 OSC 133 集成（A/B/C/D）后运行：`ls`、`false`、`printf 'a\n\n'`、无换行的 `printf 'x'`、多行命令、`cd`，逐条看复制结果与高亮。
2. 用带右侧提示符的集成（发 `P;k=r`）确认提示符不被算进输出；用只发 A/B/P 的集成确认本棒**静默**、不产假块。
3. 失败高亮在深色 / 浅色主题下的可读性，以及显式 cell 底色、选区、搜索命中三层的覆盖次序。
4. 开关开 / 关各一次，悬停块首行 / 块中部 / 块外 / 空白行，确认线只在块内出现、不吃点击。
5. 回看：滚上滚下、resize、scrollback 溢出后旧块的高亮与复制；`clear`（ED 3）与 `reset`（RIS）。
6. 备屏：vim / less / top 进出，确认不产块、主屏旧块保留、切回后分隔线与复制正常。
7. 鼠标上报：`?1000` / `?1002` / `?1003` 三档下的分隔线与右键菜单；Shift 让位左击。
8. 多标签 / 分屏：A 标签改开关、B 标签成为当前标签后的生效边界；分屏共享同一会话时的天然一致。
9. Windows 腿：MSVC 构建、`PromptMarker` 改名后的两处用例、真实 Windows Terminal / ConPTY 下的 OSC 133 字节流。

**本棒未做上述任何一条**，故本条与 `SPEC.FEAT.INTEG.01` 的现状句一律不称「可用」；只能称「纯逻辑与无头链路已证」。

---

## 8 在册代价、教训与将来改判

1. **没有 shell 集成安装器 / 探测 / 引导**：本棒只消费标记；上游不发 133 就没有任何功能。安装器属 `SPEC.FEAT.INTEG.01` 原文的「shell 侧零侵入，仅需用户在 rc 加官方片段」那一层，但本仓尚无片段资产与安装入口，登记为将来一棒。
2. **只认 C/D 的完整集成**：Warp 一类只发 A/B/P 的集成不产块；本棒不做「按提示符猜块」的启发式（猜错会把提示符算进输出，正是需求要排除的）。
3. **结尾换行与行尾空格不可精确还原**：行区间 + LF 拼接无法表达「输出以两个换行结尾」的最后一枚；命令显式打印的行尾空格与网格填充不可区分，一律裁掉。若将来要精确，须改成「按字节记录输出流」的第二种来源，不在本棒。
4. **P 的改正是外部规范 + 代码读数，不是本沙箱真机证人**：Contour / WezTerm / Warp / Ghostty 的语义一致，但本机没有可复现的 `P;k=r` shell；Windows 腿同样未验证。真机走查第 2 条是它的证人位。
5. **块容量 256**：超出后最旧块静默失去高亮 / 悬停 / 复制；不补第二套索引。若真实使用中 256 不够，按 §4.2 第 11 条调常量并补用例，不改记录模型。
6. **右键菜单终态图待并稿**：`UI_COMMANDS.draft.svg` 的九项终态图尚未画「复制上一命令的全部输出」；本棒只加条件条目，不重绘终态图。下一轮终态图重绘时把这条并入，并与「全选 / 分屏 / 重命名 / 广播组 / 关闭」一起排序。（2026-10-21 批 3 已在 SVG 根节点加 `<desc>` 登记该差异，画面未改。）
7. **设置设计稿已回写**：`UI_SETTINGS.draft.md` 的终端域表已补 `command_block_separator` 一行（2026-10-21 批 3；SVG 终端页尚未画它，以表为准）。
8. **每帧一次块拷贝**：`Session::command_blocks()` 的短锁与向量拷贝在 256 上限下可忽略，但它是每帧一次的新锁；若将来块变大或帧预算吃紧，可改成「块表版本号 + 变更时才取」，不在本棒预留。
9. **失败高亮的主题边界**：`basic[1]` 与底色接近的主题会看不清；不另做主题专用失败色。
10. **装配层无证人**：`make_interaction` 的字段搬运与广播只到当前标签，`main.cpp` 不进 runner（裁决 7.83）；与既有交互键同一条在册。
11. **Windows 腿未验证**：本棒没有平台代码，但 `PromptMarker` 改名、块记录与像素用例都只在 POSIX/无头腿跑过；Windows 构建与真机走查独立一条（§7 第 9 条）。
