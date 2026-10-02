# ARCHITECTURE.md — Borealis 架构设计

> **状态**：已通过评审（2026-09-30），由 `ARCHITECTURE.draft.md` 提升而来。评审期间的修订沿革见 [`CHANGELOG.md`](CHANGELOG.md) v0.10–v0.12；§16 待定决策清单随开发收敛，每项拍板后写入正文并从该表移除。
> **职责边界**：本文件只述架构与设计（分层、运行时、模块映射、数据流、平台边界、关键取舍）。需求本身见 [`SPECIFICATIONS.md`](SPECIFICATIONS.md)，优先级与交付分期见 [`PLAN.md`](PLAN.md)——本文件不表达优先级与阶段。
> 章节号为纯数字点分层级；需求标识引用一律写全（见 `SPECIFICATIONS.md` §1.4）。

---

## 1 定位与边界

### 1.1 定位

Borealis 是基于 **Aurora**（C++20 跨平台 AI-first GUI 库）开发的跨平台终端管理软件：**消费者应用，不是库**。以本地终端 / SSH / 串口 / Telnet 四类连接为核心，提供多标签与任意分屏工作区。

### 1.2 接入方式

- Aurora 经 `add_subdirectory(../aurora)` 走源码树接入，**不复制其源码进本仓**。
- 本仓自有三方依赖（如 SSH 传输栈）经 `find_package` + vcpkg 获取（裁决 7.12）；该 triplet 组合尚未经真机链接验证，留待接入时实测回填。
- UI 层**只使用 Aurora 公共 API**；不私改渲染路径（`SPECIFICATIONS.md` §3.3 第 1 条）。

### 1.3 命名

仓内一切可自主命名的标识统一 `borealis`：命名空间 `borealis`（按模块域分 `borealis::vt` / `borealis::term` / `borealis::grid` / `borealis::session` / `borealis::conn` / `borealis::ui` / `borealis::config` / `borealis::platform`——后者为纯实现层、无公共头，实现落于 `src/platform/{win,posix}/`）、CMake project 与 target `borealis`、产物 `Borealis`（裁决 7.14）。

### 1.4 非目标

插件体系、配置同步（Vault）、GPU 终端渲染专项、shell 内工具替代品——见 `SPECIFICATIONS.md` §1.2 与 §2.2。

---

## 2 分层与模块映射

### 2.1 分层

自底向上六层，依赖方向单向（下层不得反向依赖上层）：

| 层 | 职责 | 主要命名空间 |
|:---|:---|:---|
| 平台层 | PTY、串口、传输、默认 shell 探测、DPI 缩放上报的**平台相关实现** | `borealis::platform`（实现落在 `src/platform/{win,posix}/`） |
| 传输与会话层 | 连接生命周期、字节流读写、尺寸下发、凭据取用 | `borealis::conn`、`borealis::session` |
| 终端逻辑层 | 解码、VT/OSC 解析、终端状态机 | `borealis::vt`、`borealis::term` |
| 网格与模型层 | cell/行/环形缓冲、主备屏、脏区、scrollback | `borealis::grid` |
| UI 与工作区层 | 终端视口、标签、pane 树、交互、设置面板 | `borealis::ui` |
| 应用装配层 | 进程入口、配置装载、命令注册、日志与诊断装配 | `borealis`、`borealis::config` |

### 2.2 模块目录映射

| 路径 | 内容 |
|:---|:---|
| `include/borealis/vt/` | 解析器、序列定义 |
| `include/borealis/term/` | 终端状态机、编码解码、字符集指派与映射、宽度判定 |
| `include/borealis/grid/` | cell / 行 / 环形缓冲 / 主备屏 / 脏区 |
| `include/borealis/session/` | 会话抽象、背压队列、生命周期 |
| `include/borealis/conn/` | 连接类型与档案、凭据句柄 |
| `include/borealis/ui/` | 终端视口、标签栏、pane 树、搜索浮层、设置面板 |
| `include/borealis/config/` | 配置 schema、原子写与降级、profile |
| `src/platform/win/`、`src/platform/posix/` | 平台相关实现（**平台假设不得渗入共享路径**，裁决 7.11） |

### 2.3 依赖方向不变量

1. 平台层实现不得被共享代码直接包含具体类型（ConPTY 句柄、Win32 类型、码页 API 等）；共享代码只经接口抽象调用。
2. 终端逻辑层与网格层**不依赖 UI**，可脱离界面单测（`AGENTS.md` §4.4 第 20 条）。
3. UI 层不访问网格的私有存储布局，只消费其公开查询与脏区摘要。

---

## 3 线程与数据流

### 3.1 进程与线程模型

单进程 + **会话级后台线程**（裁决 7.8）：每个会话一个读线程负责 PTY/连接读取；不做会话崩溃隔离，靠 `SPEC.NF.RELI.01` 诊断留存兜底。已有实例运行时的参数转交（`SPEC.FEAT.INTEG.04`、裁决 7.7）经进程间通道由主线程消费，通道选型见 §16 E。

### 3.2 Aurora 单线程 UI 不变量的承接

Aurora 的硬不变量要求 widget 树、状态订阅与重绘调度只在主线程，`State::set` 仅限主线程调用（Aurora 主仓 `codespec/ARCHITECTURE.md` §3.1 与 §11）。因此：

- **后台线程一律不触达 UI 状态与 widget**；跨线程结果须回投主线程后再写状态。
- 回投后需唤醒帧循环，否则空闲帧被脏区决策跳过会饿死投递队列。**本仓的取用形态**（2026-10-01 实测、2026-10-02 已接线，裁决 7.23ⓒ）：会话提交侧经本仓自己持有的 `Window` 调 `surface().request_wake()`（Aurora 头文件明写该入口线程安全），回调体只入本仓自有队列；主线程侧在 `Application::set_on_frame` 的帧回调里排空该队列并标脏。不依赖 `aurora::detail::post_to_main`——它在 `detail` 命名空间下，虽可由 `Task<T>::set_main_poster` 安装成进程级投递器，但消费方不长期依赖 `detail` 符号。

### 3.3 输出路径（会话 → 屏幕）

```
PTY/连接 ──► 会话读线程 ──► 解码(会话编码) ──► VT/OSC 解析 ──► grid 更新(含脏区标记)
                                                                        │
                                            合成后的最终值 + 脏区摘要 ──┘
                                                        │
                                            提交侧 request_wake 唤醒帧循环 ──► 主线程 on_frame 排空队列并按脏区上屏
```

**决策**：解码、VT 解析与 grid 更新**均在后台读线程内完成**，主线程只接收「合成后的最终值 + 脏区摘要」并绘制。理由：与 Contour / Ghostty / Alacritty 封装一类主流做法一致，把高频解析与网格写入移出主线程，契合 Aurora 单线程 UI 不变量（后台不碰 `State`）。

**已落地形态**（2026-10-02）：主线程侧的收口是 `Session::set_frame_wake` 注入的唤醒句柄（出锁后调、一轮批量输入只唤醒一次）+ `Application::set_on_frame` 帧回调里的 `TerminalView::on_frame()`；后者 `drain_damage()` → 在 `Session::read` 临界区把网格与脏区并入 `session::ScreenMirror` 的本地可见区副本 → 按需 `mark_needs_paint()`，同一回调还排 OSC 52 的剪贴板落地件与写回（裁决 7.21③）。取队列与读网格的先后固定为「先取队列、再读网格」，反序与读线程构成 ABBA 死锁。

### 3.4 网格同步策略

grid 由后台线程写、主线程读，故需同步保护：

- 采用**短临界区**保护：解析与网格写入在临界区内完成并产出脏区摘要，主线程取用脏区摘要时持锁时间尽量短。
- 解析器状态**置于锁外**（仅被会话读线程访问），避免无谓加锁。
- 禁止在临界区内调用任何 UI 或阻塞 IO。
- **主线程读取方式（2026-09-30 拍板，原 §16 D）**：grid 由会话读线程持有并写入（**后台权威**），主线程每帧只在短临界区内取走**脏 cell 增量**并入本地副本，绘制与光标读取本地副本；整屏位移（区域内滚动、清屏、主备屏切换）以「整屏脏」标记通知副本失效重建。理由：锁窗口正比于本帧变更格数而非屏大小，主线程不会长持锁阻塞会话读线程（`SPEC.NF.PERF.01`）。代价：多一份可见区副本的常驻内存，且滚动与尺寸变化处必须显式失效副本——遗漏即表现为「画面滞后一行」类错帧。
- 终端内搜索（§10.3）读**权威 grid** 而非 UI 副本（副本只覆盖可见区，不含 scrollback），持锁口径同上：按行分片短持锁，不得整程持锁遍历。
- **会话层落地口径（2026-09-30）**：解码 + `Terminal::feed`（解析与网格写入）在同一临界区内完成，锁窗口正比于本次输入字节量；**出锁后才做 IO 与入队**。查询应答（DSR/DA1，`SPEC.FEAT.TERM.01`）在锁内只经 `term::ResponseSink` 登记进缓冲，出锁后编码写连接——直接在回调里写连接就是把 PTY 写算进锁窗口。解析器与解码器的**内部状态**只被会话读线程触达，不需要单独加锁；需要锁的是它们写入的网格与需要跨线程取阅的光标/模式。

### 3.5 输入路径

UI 事件 → 键映射（`SPEC.FEAT.INTERACT.01`）→ **文本按会话编码编码**（`SPEC.FEAT.TERM.09` 的发送方向）→ 会话写接口 → PTY/连接。输入法路径（`SPEC.FEAT.INTERACT.06`）：框架 `TextCompositionEvent` 的 preedit 由视口就地绘制在光标单元格处、候选窗按光标屏幕坐标定位，**组合中间态不发往会话**，仅 commit 文本经同一编码环节写入。鼠标上报模式与本地选择交互的切换由终端状态驱动（`SPEC.FEAT.TERM.06`）。

**已落地形态（2026-10-02，裁决 7.30）**：键映射是纯逻辑件 `term::encode_key(const KeyPress &, const TermModes &) -> std::optional<std::string>`（`include/borealis/term/keymap.h` + `src/term/keymap.cpp`，公共头不含 Aurora 类型），`term::KeySym` 与 `aurora::KeyCode` 逐值对齐、互转即 `static_cast`。视口控件的两个入口按通道分工下笔：`on_key_event` 只发**已过编码环节的转义字节**（`Session::send_bytes`），`on_text_input` 只发**码点**（`Session::send_text`，故非 UTF-8 会话的编码环节仍归 `SPEC.FEAT.TERM.09` 那一棒）；`encode_key` 对「无 Alt/Meta 的可打印键」返回空正是这条分工的落点——真实后端先给 `WM_KEYDOWN` 再给 `WM_CHAR`，两份都发即每字符上屏两次。控制字符不冲突：Win32 后端在 `ch < 0x20` 时不发文本事件（其注释自陈「控制字符交给 `KeyEvent` 处理」）。模式快照（`DECCKM` 等）经既有的 `Session::read` 短临界区取用，不新增跨线程协调点。`Ctrl+Alt` 系一律返回空：框架 `Application` 在按键到达控件之前先跑 `ShortcutRegistry`，该系归快捷键层独占（配置裁决口径见 `SPEC.FEAT.PREF.05`）。

三条派发出去的框架缺口使本路径**部分达成**：`Tab`（G14）、Alt 系（G15）、小键盘与 NumLock（G16）到不了控件，详见 §9.6 与 `SPECIFICATIONS.md` 附录 A.2。

**装配层的前置条件（2026-10-02 真机走查实测，裁决 7.31②）**：框架**不在启动时给焦点序里的首个控件派焦点**（实测只有模态 `FocusManager::push_scope` 会 `set_focus(cands.front())`），故 `src/main.cpp` 在 `Application` 构造后必须显式 `app.focus().set_focus(view.get())`——缺这一步时按键与滚轮都路由不到视口，光标也永远停在失焦的空心描边形态，而**集成用例抓不到**（它以替身连接观测字节、不经真实焦点序）。派发点刻意在装配层而非控件 `on_mount`：后者依赖 thread-local 的 `current_focus_manager()`，挂载前不保证可取，且「谁该有焦点」本是装配意图。

### 3.6 背压与有界队列

后台读线程与 UI 之间设**有界队列**（`SPEC.NF.PERF.06`，落地为 `session::DamageQueue`）：

- 队列里流转的是**「哪些视口行要重读」的提交**（行区间，或一个「整屏脏」标记），不是字节也不是 cell 值——权威 grid 由读线程持有（§3.4），主线程按提交去网格取最新值，于是「同一格被多次覆盖」只会合并成一次重读，最终内容恒等于 PTY 输出（需求里唯一许可的丢弃形态）。
- 队列满时按「合并而非丢弃」：新提交与队尾提交并成更宽的行区间。区间只变宽不变窄，多读几行是冗余不是错误；条目数上限即内存上限。
- 行脏标记**由消费侧清除**：写侧只登记不消费，主线程在临界区内按行的 `[dirty_left, dirty_right)` 取列级增量后 `clear_dirty()`。反过来（读线程取完即清）会让主线程拿不到列级增量，退化成整行重绘（§4.6 的行内左右界落空）。
- 单帧最多消费 N 次合并提交后主动让出（默认 N=8，容量默认 64），保证输入响应与 UI 不被饿死。
- 队列水位、合并次数、溢出次数与让出次数进可观测面板（`SPEC.NF.RELI.01`）。
- 锁序固定为「**先取队列、再读网格**」：会话推入提交前必须先离开网格锁，否则与主线程的取用顺序构成反向锁序。

### 3.7 尺寸同步

视区网格尺寸变化（窗口 resize / 分屏拖拽 / pane 缩放 / 全屏 / 字体缩放）须下发新尺寸并重排（`SPEC.FEAT.XFER.01`）：Unix `ioctl(TIOCSWINSZ)` + `SIGWINCH`，Windows ConPTY `ResizePseudoConsole`；**去抖合并**，会话启动、分屏初始挂载、会话恢复时各下发一次初始尺寸。

---

## 4 网格与 scrollback 模型

**决策**：采用**环形缓冲 + 行块**存储（对齐 Alacritty 一类做法：`Storage` 以 `zero` 字段模加实现 O(1) 滚动，对比整片搬移显著省去高频滚动开销；`Row` 带脏上界以跳过干净 cell；cell 主结构精简、少用属性外置）。

### 4.1 cell 表示

- 主结构只放高频字段（码点、前景、背景、常用标志位）；
- 低频字段（零宽字符、超链接等）**外置**到侧表，避免主结构膨胀拖累整屏内存与遍历。

**零宽侧表的落地形态**（`include/borealis/grid/row.h`，裁决 7.20③）：`Row` 持一张「基础格列号 → 标记序列」的表，按列号升序，故同列标记必成连续游程。三条不变量决定它随网格操作走：写入某列的 cell 即丢弃该列原有标记（旧字符的零宽不得跟着新字形上屏）、整行 `std::move`（区域滚动与插删的搬移路径）连带标记、`reset` 与列宽截断一并丢弃。单格上限 8 个标记，超限**丢弃不报错**——会话字节流是不可信输入，连续投喂零宽码点不得让一行无限膨胀。并入标记会标脏该格，否则渲染侧把它当干净列跳过。

**超链接侧表的落地形态**（同一 `Row`，裁决 7.21②）：持另一张「列号 → `HyperlinkId`」的升序表，格子里只存一个数字标识，URI 文本由状态机的有界链接表持有（`kMaxHyperlinks = 1024`，标识递增且永不复用，满表淘汰最旧一条）。清链时机与零宽侧表同律：`set` 该列即清其标识（擦除、覆盖、`reset`、列宽截断都走这条路），否则「删掉的文字仍然可点」；挂链只由 `OSC 8` 的区间括出，`CSI m` 复位 SGR 不截断区间。双宽字符只挂前半格（延续格本就不停留）。悬在已被淘汰标识上的格子解析不出目标，读取方按「不可点」处理。

### 4.2 行表示

行维护「自上次重置以来被修改元素的上界」——重置时可跳过干净区间，避免整行扫描。

### 4.3 环形缓冲与容量

- 滚动以环形缓冲的 `zero` 偏移模加完成，**不搬移行数据**；
- 容量默认 10,000 行、上限 100,000（`SPEC.FEAT.TERM.04`）；超长行默认**截断**（裁决 7.4），策略可配；
- 超出容量时覆盖最旧行。

### 4.4 主/备屏幕

备屏独立缓冲、不与主屏 scrollback 混排；按 xterm 惯例备屏本身**无回滚**（备屏期滚轮行为由 `SPEC.FEAT.TERM.06` 的 alternate scroll 承接），退出备屏恢复主屏原状（`SPEC.FEAT.TERM.03`）。

### 4.5 尺寸变化处理

窗口尺寸变化时已有行**默认不 reflow**（裁决 7.5），仅新输出按新宽度排布。

**行数变化按窗口边界移动**（裁决 7.17）：底部锚定，变高从 scrollback 顶部收回历史填满、变矮时顶行溢出进历史、超出容量从最旧端丢弃、历史不足则顶部补空白。scrollback 容量是 `SPEC.FEAT.TERM.04` 的配置值，不随视口变高缩水，故缓冲区按「视口行数 + 该容量」取容，**变高必然扩容并把环按逻辑序拉直**（模数一变，不拉直会让跨旧边界的行号错位）；代价按现存行数计，靠 `SPEC.FEAT.XFER.01` 的去抖合并下发压到每帧边界一次，不落进滚动路径。

行数与列数变化都属整屏位移级事件：行号与内容的对应关系变了，脏 cell 增量表达不出「平移了一屏」，故一律以**整屏脏**通知副本重建（§3.4）。终端模式随尺寸收敛：DECSTBM 带复位为全屏、制表位按新列宽重建、光标与 DECSC 保存的位置一并钳进新视口。

### 4.6 脏区跟踪

- 行级记录左右列界，避免整行重绘；
- 按视口偏移过滤，只上报可见区域的变更；
- 区分「整屏刷新」与「局部行变更」两类。

### 4.7 内存预算

以 cell 主结构紧凑化为前提估算单会话常驻，目标对齐 `SPEC.NF.PERF.04`（10k 行 scrollback 常驻 ≤ 150 MB）。外置属性表按实际使用增长，不进基线预算。

---

## 5 VT 解析与终端状态

**决策**：**自研表驱动状态机**（Paul Williams ANSI/VT 状态机的表驱动形态），C++ 纯逻辑模块，配 `tests/fixtures/` 回放夹具做全量单测。理由：可完全掌控协议解释权（DEC Special Graphics、`DA1`/`DSR`、OSC 52/8/133/7、bracketed paste 等），契合裁决 7.13「VT/OSC 解析属应用域，不进框架；要求纯逻辑模块 + 全量单测」。代价：边界覆盖靠自身测试资产积累，工作量最大。

### 5.1 解析器形态

- 表驱动、分支最小化；解析器**不赋予语义**，语义交由终端状态机执行；
- 解析器实例**仅由会话读线程持有**（置于网格锁外，见 §3.4）；
- 可脱离 UI 单测，输入为字节流、输出为语义事件。

### 5.2 序列覆盖与响应回写

`CSI` / `OSC` / `DCS` / `ESC` 序列；`DA1`、`DSR` 等设备查询须**正确回写响应**（`vim`/`tmux` 依赖）；私有模式集（`?1` `?4` `?6` `?7` `?25` `?47` `?1049` `?2004` 等）全量登记。

回写形态（2026-09-30 落地）：状态机不持有写通道，查询应答经注入的 `term::ResponseSink` 交给会话（§3.4 的锁内登记、锁外回写）。应答取值：`DSR 5` → `\e[0n`、`DSR 6` → `\e[<行>;<列>R`（**整屏**视口坐标 1 基，与 DECOM 无关）、`DA1`（无私有前缀的 `CSI c`）→ `\e[?62c`。能力号只报档位 62（VT220 + 高级视频选项），132 列 / sixel / ReGIS / 打印机一律不报——误报会让 `vim`/`tmux` 走未实现的分支。`CSI > c`（DA2）与 `CSI ? 6 n`（DEC 定位器）不在 `SPEC.FEAT.TERM.01` 覆盖内，按「宁可不答也不答一份错格式」忽略。

### 5.3 字符集与模式

- 字符集切换：DEC Special Graphics 线条字符集（`ESC(0` / `ESC(B`）、`ESC%G`；
- 模式须生效：`DECAWM`（`?7`）、`DECOM`（`?6`）、`IRM`（`?4`）、`DECCKM`（`?1`）与 keypad 应用模式。

### 5.4 OSC 处理

`OSC 0/2` 标题、`OSC 7` 工作目录、`OSC 8` 超链接、`OSC 52` 剪贴板、`OSC 133` 命令块（`SPEC.FEAT.TERM.07`、`SPEC.FEAT.INTEG.01/02`）。标题消费链路：OSC 标题覆盖标签名，用户手动重命名优先级更高（可配）。

**落地形态**（裁决 7.21）：派发在 `Terminal::do_osc`，产物是**状态快照 + 一个取走型动作**——`term::OscState` 给最近一次的标题 / 目录原文 / 133 边界与退出码 / 计数，超链接以「标识 → URI」有界表配合 §4.1 的行侧表，`OSC 52` 写方向的文本走 `Terminal::take_clipboard_write()` 的取走语义。不选事件队列的理由是 §3.4 的锁纪律：状态机跑在读线程的网格锁内，锁内只能留存不能投递。`OSC 52` 的真正落地是主线程的 IO，故由 `session::ClipboardOutbox::drain()` 转调 Aurora `Clipboard::set_text`（框架依赖只出现在 `src/`，公共头不含框架头，§2.3）；`52;c;?` 的读方向默认禁止但**回写空响应**，不让远端程序干等。未识别的命令号计入 `unhandled_count` 走 §5.5 的降级口径，不整体吞掉。

### 5.5 未识别序列与降级

未识别序列**不得中断解析**；计数进可观测面板（`SPEC.NF.RELI.01`），并按 Aurora「降级而非中止」的既有口径产出诊断而非异常（Aurora 主仓 `codespec/ARCHITECTURE.md` §11）。

---

## 6 字符编码与宽度判定

### 6.1 会话级编码

UTF-8 为本地终端与 SSH 默认；**串口默认 GB18030**（裁决 7.6）；备选 GBK / Big5 / Latin-1 / CP437（`SPEC.FEAT.TERM.09`）。编码为**双向**（裁决 7.16）：解码（字节流 → 文本）与发送方向编码（文本 → 字节流）共用同一份会话编码配置。

### 6.2 转换失败处理

**解码方向**：按替换字符处理，**不得中断解析、不得污染后续行**；非法字节序列计数进可观测面板。

**编码方向**（发送）：目标编码不可表示的字符按可配策略处理（替换 / 丢弃并提示 / 原样透传 UTF-8），默认替换并给出一次性提示，**不得静默产出乱码字节**（`SPEC.FEAT.TERM.09`）。

### 6.3 宽度判定

按 Unicode East Asian Width 处理 CJK 双宽占位；**Ambiguous 类默认按单宽（窄）处理，可按 profile 覆盖为双宽**（裁决 7.15）。宽度判定接口须接受 Ambiguous 宽度模式作为入参——框架侧原语同样不得硬编码单/双宽（`SPECIFICATIONS.md` 附录 A.2 G1）。

**接缝形态**：终端状态机经**注入的**宽度判定接口（`include/borealis/term/width.h`，入参为码点 + Ambiguous 宽度模式）取占位格数，写入路径自身不查表（`PLAN.md` §2 的「应用侧不得自行查表替代」）。判定表由框架提供（G1 的宽度判定腿已于 2026-09-30 闭合）：Aurora 公共 API `aurora/core/unicode_width.h` 的 `unicode_cell_width(code_point, AmbiguousWidthMode)` 一次给出 **0 / 1 / 2** 三档（0 = 不独立占格的零宽码点，判据 General_Category Mn/Me/Cf；2 = East Asian Width 的 W/F），数据 UCD 18.0.0。本仓的生产实现 `term::UnicodeWidthPolicy` 只做口径转发、不持表；因 `include/borealis/**` 公共头不含 Aurora 头，其实现落在 `src/term/width.cpp`。`SingleWidthPolicy` 退为测试用的常数注入值（让不关心宽度的用例不受 Unicode 版本影响），桩实现仍用于状态机机制类单测。

**零宽码点的去向**（裁决 7.20②③④）：判定次序是「先零宽、后宽度」且不可交换——Ambiguous 区间包含组合区段，先按 Ambiguous 判宽会让零宽字符占格。状态机在宽度为 0 时既不写格也不推进光标、更不触发换行，而是把码点并入光标左侧最近的基础格（跳过双宽字符的延续格，挂到前半格上），网格侧表见 §4.1。宽度按**字符集映射前**的码点判定，`ESC ( 0` 映射出的框线字符因此不受 Ambiguous 口径影响。

---

## 7 会话与连接抽象

### 7.1 抽象原则

平台相关能力（PTY、串口、传输层、默认 shell 探测、DPI 缩放上报）**只能存在于平台层实现内**，共享代码经接口抽象调用（裁决 7.11）。否则后补的 Linux 等价会退化成重写。

### 7.2 接口粒度（2026-09-30 拍板，原 §16 A）

**基础接口 + 能力接口组合**。落地形态：

- `session::Connection` 是**所有**连接类型共同实现的基础接口，只表达生命周期与数据面：`start(ConnectionEvents&)` / `write(bytes)` / `resize(Size)` / `close()` / `alive()`；
- 连接 → 会话的反向通道是 `session::ConnectionEvents`（`on_bytes` / `on_closed`），回调发生在连接的读线程上，实现方不得在此触达 UI 状态（§3.2）；
- SSH 独有的面（SFTP / 隧道 / 执行通道，`SPEC.FEAT.CONN.04` / `SPEC.FEAT.CONN.08` / `SPEC.FEAT.CONN.10`）到其落期时以**独立能力接口**增补，既不改基础接口，也不让本地终端为空实现买单；
- 接口只收标准类型：PTY 句柄、Win32 类型、码页 API 一律留在 `src/platform/` 的实现内（裁决 7.11）。

选它的理由与代价：按能力正交（`Reader`/`Writer`/`Sizer` 分立）利于测试替身但把一个连接的语义拆散，按连接类型分立则复用面窄；组合案兼顾两者，代价是接口面比单一大接口略多。`alive()` 存在的原因是 `SPEC.FEAT.WS.01` 的关闭前确认要区分「进程还在」与「标签要关」；`resize` 的去抖合并归调用方（§3.7），接口只表达最终尺寸。

会话侧的组合点在 `session::Session`：持 `unique_ptr<Connection>` + `term::Terminal` + `term::Utf8Decoder` + `session::DamageQueue`，一把 mutex 护住状态机与解码器，是本项目里**唯一**的跨线程协调点（§3.4 的持锁口径见该节）。

### 7.3 连接类型族与生命周期

本地终端（`SPEC.FEAT.CONN.01`）、SSH（`SPEC.FEAT.CONN.02`）、串口（`SPEC.FEAT.CONN.05`）、Telnet（`SPEC.FEAT.CONN.06`）。进程退出后保留终端内容供回看；SSH 断线自动重连（`SPEC.FEAT.WS.05`）。

本地终端的落地形态（2026-09-30，Windows 腿）：声明面在 `conn` 域公共头 `include/borealis/conn/local_terminal.h`——`LocalTerminalSpec`（命令行 / 启动目录 / 追加环境变量，全标准类型）、`kPtyDefaultEnvironment`（`TERM` / `COLORTERM` 的注入默认值，避免两条平台腿各写一份）、`default_shell_command_line()` 与 `make_local_terminal_connection()` 工厂（返回 `session::Connection`）；实现面在 `src/platform/win/`（`platform::ConptyConnection`，私有头不出 `src/`）。工厂吃初始尺寸，使**会话启动只下发一次尺寸**（`SPEC.FEAT.XFER.01`）——`Session` 不再在 `start()` 里补发一次 `resize`。Win32 侧有四处只能靠实测确定的 API 语义（宿主标准句柄、伪终端管道端归属、关停与子进程、环境块合成），已回写为裁决 7.19；POSIX 腿按同一接口与同一口径实现，尚未开工。

### 7.4 凭据

凭据经 OS 凭据库存储，配置只存引用句柄；OS 凭据库不可用时降级为「每次询问」，**绝不降级为明文**（`SPEC.FEAT.CONN.09`）。

### 7.5 SSH 之上的子系统归属

SFTP（`SPEC.FEAT.CONN.04`）、隧道（`SPEC.FEAT.CONN.08`）、公钥推送的执行通道（`SPEC.FEAT.CONN.10`）是 SSH 会话之上的附加通道，归 `borealis::conn`；档案与凭据句柄（`SPEC.FEAT.CONN.03` / `SPEC.FEAT.CONN.09`）归 `borealis::conn`，其持久化经 `borealis::config`；连接管理器 UI（`SPEC.FEAT.CONN.07`）归 `borealis::ui`；会话日志（`SPEC.FEAT.CONN.11`）由会话层落盘、路径配置归 `borealis::config`。系统通知（`SPEC.FEAT.INTEG.03`）的框架依赖已解除（G11 于 2026-10-02 以 `NotificationCenter` 公共 API 闭合，裁决 7.29④），落期仍在观察池，届时落点归 `borealis::session` 的 OSC 消费出口 + `borealis::ui` 的转发。

---

## 8 工作区模型

### 8.1 标签与 pane 树

- 多标签（`SPEC.FEAT.WS.01`）；框架 `TabBar`（`widget/tab_bar.h`）**仅覆盖选中切换与关闭**——事件只有 `on_change` / `on_close`，属性为选中序号、栏高、四个**全局**配色、指示器粗细、字号与内边距。`SPEC.FEAT.WS.01` 要求的**拖拽重排**与**逐标签图标**、`SPEC.FEAT.WS.04` 要求的**逐标签状态角标与活动高亮**均无对应能力（配色是全局的，无法区分单个标签），须应用侧自研标签栏或在其外自绘这些元素。不得因 `TabBar` 的存在而低估本项工作量（与下条 `Splitter` 同理）。
- **pane 树须自研**：框架 `Splitter` 为二元分割器，只能表达「两个子节点 + 一个比例」；本需求要求每一层为**多子 pane 容器**并满足任意方向 / 任意深度 / 任意比例 / 任意 pane 数四义（裁决 7.10）。
- 焦点在二维 pane 树上的**方向键路由**（按几何位置跨层跳转）自研。

### 8.2 布局与尺寸联动

每次切分 / 缩放 / 拖拽后须向受影响会话重发尺寸（与 `SPEC.FEAT.XFER.01` 强耦合）。

### 8.3 布局持久化与恢复

布局记忆、会话恢复、撤销关闭标签——属计划能力，详见 `SPEC.FEAT.WS.10` / `SPEC.FEAT.WS.11`；本文件不在需求之外扩展其语义。

### 8.4 其余工作区能力归属

多窗口（`SPEC.FEAT.WS.03`）消费 `Application::open_window`（附录 A.1）；全屏（`SPEC.FEAT.WS.06`）依赖的框架窗口接口以开工实测为准；pane 缩放（`SPEC.FEAT.WS.08`）是 pane 树的视图状态；广播输入（`SPEC.FEAT.WS.09`）在 `borealis::ui` 输入分发层实现。

---

## 9 渲染接入与视口

### 9.1 原则

只消费 Aurora 公共 API；不私改渲染路径。用公共 API 组合出的应用侧控件**不属于私改**（裁决 7.13）。

### 9.2 视口控件的取用形态与绘制序列（**已落地**，2026-10-02 更新）

G1 的三腿均已在 Aurora 侧以公共 API 落地（宽度判定见裁决 7.20，绘制两腿见裁决 7.22）：`render::FontEngine::monospace_cell` 给整像素单格度量、`Painter::draw_text_runs` 给批量同属性片段绘制、`aurora::unicode_cell_width` 给占格数。因此：

- 渲染层**直接消费这两个原语**：按 `monospace_cell` 定列位与行位，把一行按样式切成 `TextRun` 数组一次提交；
- 本节此前的「应用侧自绘 cell 网格 + 逐格 `draw_text`」过渡形态（G1 交付延期时的预研备路径）**不再启用**；
- 网格模型、脏行 diff、颜色合成（含 SGR 属性、bold-is-bright、最小对比度）**仍留在应用侧**，原语只收合成后的最终值——这一条边界不因原语落地而改变。

**取用形态细则**（2026-10-01 裁决 7.23，随渲染层落地）：

```
monospace_cell(font, scale) → {cell_width_px, cell_height_px, ascent_px}   ← 物理像素
TextRun.box.origin / Painter 几何                                           ← 逻辑 dp

cell(col,row) 的落笔原点 = { pad_dp + col * cell_width_px / scale,  pad_dp + row * cell_height_px / scale }
视口列/行数 = (可视 dp − 2 * pad_dp) ÷ (cell_px / scale)   ← SPEC.FEAT.XFER.01 的尺寸来源；pad 见裁决 7.25②
```

- **视口内边距 `pad` 缺省四周 4 dp、可配为 0**（裁决 7.25②）：偏移取整 dp、格宽取整 px，故 125%/150% 缩放下的 5/6 px 偏移不破坏网格边界；`ui::make_geometry` 须先扣内边距再除格宽（差距清单 G-8）。
- 同一视口只用**一个参考 `Font`** 取一次度量：字重切换可带来 1px advance 差，故粗体只换 weight、不换格宽。
- run 按「前景/背景/字体三者全等」在行内合并，色带矩形与文本片段**共用同一批切分边界**（裁决 7.23②）。
- 一帧的层叠顺序（`src/ui/terminal_view.cpp` 的 `on_paint`，已落地）：① 整盒铺主题默认底色（每帧必铺，与「本帧有没有脏」无关）→ ② 色带 `fill_rect` → ③ 每行按「是否斜体」分两批 `draw_text_runs`（combining 随同 run 文本并字）→ ④ 下划线/删除线装饰 → ⑤ 光标与回看指示条。③ 的分批是因为框架批量入口的 `TextLayoutOpts` **整批共用**（裁决 7.29①），每行最多两次调用；⑤ 的块形光标走「先画字、后画块、再按该格合成底色重画该格子串一次」的三段式，**第三段必须带上与 ③ 同源的 `opts.italic`**，否则光标停在斜体格上会把那一格画成正体（该纪律由一条像素用例守住并以变异自证，见 `RENDER_VIEWPORT.draft.md` §9）。
- **下划线为档位枚举**（none/single/double/curly，裁决 7.25④；SGR 编码映射按 7.28 实测修正：`4`/`4:1` 单、`4:2` 与 `21` 双、`4:3` 波浪、`4:0`/`24` 关，dotted/dashed 按单线）：双线＝基线 +1px 与 +3px 两条 1px 线，波浪＝4 物理像素一周期逐像素列 ±1px 折线（相位锚在该行绝对像素 x），两者都以 `fill_rect` 在应用侧合成（裁决 7.13① 的公共 API 组合，非私改渲染路径）。
- **光标色取自 `PaletteSpec.cursor_color`，缺省回落 `default_foreground`**（裁决 7.25③）；回看态在视口右侧画 2 dp 位置指示条一块（裁决 7.25①）。
- 局部帧只压裁剪栈而 `on_paint` 收全量 bounds，故绘制行集取 `[0, rows)` ∩ 按 `Painter::clip_bounds()` 折算的行区间（裁决 7.23ⓐ）。**这条纪律没有像素可观测面**：脏区上报走非虚的 `Widget::dirty_bounds()`（恒等于控件自身盒），控件无法把子矩形报成脏，故帧间差分观察不到窗外是否被画——像素用例刻意不断言它。
- 视口滚动状态只有一个「起始行偏移」，走 `Widget` 内置的 `ScrollViewport` 内核而非框架 `Scroll` 容器（§9.5）。
- 唯一触达 `au::Painter` / `au::Widget` 的实现收口在 `src/ui/terminal_view.cpp` 一个翻译单元；`cell_layout` 与 `palette` 为无框架依赖的纯逻辑，可全量单测。

### 9.3 G1 就绪后的替换接缝

网格模型、脏行策略与颜色合成（含 SGR 属性、bold-is-bright、最小对比度）**留在应用侧**；框架原语只接收合成后的最终值。故替换面收敛在绘制后端一层。

### 9.4 dp 与物理像素的 cell 度量

Aurora 中字体测量在逻辑 dp 空间、光栅化按真实屏幕 DPI 生成物理分辨率字形，二者解耦（Aurora 主仓 `codespec/ARCHITECTURE.md` §8.2）。终端须保证 **cell 尺寸整像素对齐与网格边界吸附**，字形缓存按 DPI 分档失效重建（`SPEC.FEAT.RENDER.05`）。该整像素单格尺寸自 2026-10-01 起由框架给出：`render::FontEngine::monospace_cell(f, scale)` 返回物理像素的 `{cell_width_px, cell_height_px, ascent_px}`，其中行高与基线与绘制侧首行 pen 的 snap 口径同源，故按它排布的多行文本与实绘像素对齐；应用侧不再自行把 dp 度量乘 scale 后取整（那会得到小数列宽并逐列累积成半格错位），但**列起点、跨格与换行的排布策略仍在本仓**（裁决 7.22②）。

### 9.5 与框架 Scroll 滑窗的关系（**已拍板**，2026-10-01，原 §16 B）

终端视口与 scrollback 一律由本仓**自管**，不复用框架 `Scroll` 容器：`Scroll` 是「容器持有单一子控件 + overscan 滑窗一次 blit」的形态，其视口高度无公共 getter、只有 150ms `ScrollGlide` 吸附而无惯性滚动，且整屏 blit 与终端的「行级脏 + 权威网格在后台读线程」两条前提正交；本项先前的评审还记有一条框架不变量——`Scroll` 不得无条件置 `RelayoutBoundary`，否则脏冒泡被截断、离屏缓冲恒为骨架——自管形态一并绕开它。取而用之的是 `Widget` 内置的 `ScrollViewport` 内核（`offset_y` / `content_h` / `clamp_offset` / `max_offset`），终端的滚动状态收敛为一个**起始行偏移**；偏移变化即整屏脏重建，选区随滚动跟随由此表达。

**反哺通道**：自管过程沉淀出的、去掉终端字样后仍成立的通用件（例如「按行偏移的视口裁剪与位置恢复」），按裁决 7.13① 与附录 A.3 以 Aurora 公共 API 形态反哺，本仓只消费公共头；不满足该判据的留在本仓应用侧。

**已落地形态**（2026-10-02，绘制侧见 §9.2、副本侧见 `session/screen_mirror.h`）：

- 内核量一律以**行**为单位而非 dp（`step = 1.0F`，一个滚轮单位 = 一行），免得浮点残差让取整少算一行；`offset_y` 是框架语义的「距顶行数」，本层绘制与副本换算只认派生量 `back_rows = max_offset() - round(offset_y)`（距底行数，0 = 贴底）。两个方向不同的量不得混用。
- **不声明 `overflow_strategy(Scroll)`**：实测 `Widget::paint_content` 只在该策略下把 `offset_y` 当 **dp** 平移量加进 `on_paint` 的 `bounds.origin.y`，而本层的偏移是「取哪几行」的语义，平移会让首行画到盒外、底部留白且单位对不上。故保持默认 `Visible`，覆写 `wants_scroll()` 拿滚轮、`on_scroll()` 自己驱动内核，并把 clamp 吃不尽的余量写进 `e.remaining_y` 上冒给更浅的可滚动祖先（工作区分屏场景）。副作用记一句：基类 getter `scroll_offset_y()` 因此返回「距顶行数」而非 dp，本控件不对外暴露该语义。
- **回看态遇新输出＝距底恒定**（裁决 D6①）：`back_rows` 是用户意图、`offset_y` 只是它在当前 `total` 下的投影，故每帧拿到新 `total_lines()` 后重投影一次再交副本；效果是可见窗整体向更早方向推一行（`row_band(grown, i) == row_band(again, i + 1)`），既非画面静止也非底部多一行。代价是长输出下固定片段会被顶走；若日后改取绝对行锚定，改动面是去掉这段重投影 + `grid::Storage` 补一个「已覆盖最旧行数」的单调计数。
- **备屏天然不可滚**：`Terminal` 的备屏以 scrollback 容量 0 构造，故 `total_lines() == rows` → `max_offset() == 0` → clamp 恒回 0，无需特判（像素用例已断言备屏连滚 5 格两帧逐位相同）。
- 无惯性 / 动量：一步一格是刻意选择，`ScrollGlide` 的 150ms 吸附属框架 `Scroll` 路径，本层不引。`SPEC.FEAT.TERM.06` 上报模式下的滚轮转发与 alternate scroll 尚未接入，代码处留 `TODO(SPEC.FEAT.TERM.06)`，当前恒走本地回看。

### 9.6 交互阻塞项

**渲染与文本选择链路的绘制面调用无框架阻塞项**（2026-10-02）：G1（三腿）、G13（批量入口的排版选项）、G2（`click_count`）均已闭合，视口绘制主路径与多击选择的实现面全在公共 API 上。**但真机验收面有一条：G17（DPI 缩放真值源不统一）**，见本节末与 §15.3。

**键盘链路有且只有三条阻塞，且都阻塞「完整度」而不阻塞「开工」**（2026-10-02 实测，登记为附录 A.2 的 G14 / G15 / G16，裁决 7.30⑤）：

- **G14 `Tab` 到不了控件**：派发器的 `match_shortcut` 把 `KeyCategory::Tab` 无条件当焦点遍历消费；方向键与 Enter/Space 各有「控件优先」钩子（`wants_navigation_keys()` / `wants_activation_keys()`，本仓两者已覆写）而 `Tab` 没有对应钩子。后端侧无阻拦——`VK_TAB` 已映射为 `KeyCode::Tab` 并进常规按键链，故缺的是派发层一个钩子分支。
- **G15 Alt 系在 Win32 被挡在事件链之外**：`handle_syskey` 只推进修饰态便把 `WM_SYSKEYDOWN/UP` 交回 `DefWindowProcA`，不产生 `KeyEvent`；Alt 系也不生成字符，故文本通道亦无。
- **G16 数字小键盘与 NumLock 未建模**：`KeyCode` 无 `KP_*` 项（其注释自陈 KP_0-9 映射到 `Unknown`、`KP_Enter` 并入 `Enter`），也没有 NumLock 键码或修饰态。后果是 `DECKPAM`/`DECKPNM` **有模式位而无可发之键**——状态机侧的模式登记已落（`SPEC.FEAT.TERM.01`），该腿验收随 G16 关闭移动。

三者的处置按裁决 7.13① / 7.24③ 派发 Aurora 侧以「公共 API + 单测 + 文档回写」补全；本仓**不等不绕**：编码层已把 `Tab` / `Shift+Tab` / Alt 前缀三者的字节形态写对并各自有逐字节单测（小键盘序列要的是框架侧的键码本身，本仓无从代写），回货后接入的是派发侧（一行覆写或一个钩子），不自造窗口消息钩子、不改派发顺序。

- 多击语义直接用框架的 `MouseEvent::click_count`（派发层集中自算，裁决 7.29②），本仓不自算、不私挂分叉；阈值未接系统双击速度，跨平台手感一致而各平台各自的设置不生效，属框架侧刻意的取舍。
- 视口滚动的取用口径见 §9.5；真机走查时要确认「鼠标须悬停在控件盒内才响应滚轮」（`wants_scroll()` 按命中链派发）符合终端软件的预期——部分终端是「焦点在窗口即响应」，那条差异属本仓交互层的选择而非框架缺陷。

**G17：缩放真值源不统一使尺寸相关的真机验收全断**（2026-10-02 真机走查实测，机器 144 DPI / 150%）。事实：`BuildContext::scale_factor` 与 `Painter::scale()` 恒 `1.0`，而同一窗口的客户区逻辑尺寸随操作路径给出 `960x640`（启动，与请求值逐位相同）/ `2560x1369.33`（最大化，高度带 `×1.5=2054` 的痕迹）/ `640x426.667`（还原，比启动时还窄）三个互不相容值；截屏证人为最大化后网格只覆盖客户区约 `1/1.5`，其余露出框架浅色基底。本仓无涉：`on_paint` 第 ① 层按框架给的 bounds 铺底色，格度量与 dp 步长一律经 `ctx.scale_factor` 单源换算（§9.2），故未上色带必然落在 bounds **之外**。**成因已在 Aurora 侧在册**（其 `codespec/specification/08-tooling.md` §8.2 记为遗留库层缺口，`tests/e2e/etest_smoke_render.cpp` 与 `etest_multi_window.cpp` 各有一处以「known Win32Host scale gap」为由 SKIP）：`Win32Host::Impl` 的 `scale` 成员在成员初始化列表里取 `dpi_scale()`，**早于构造体内的 `enable_dpi_awareness()`**，且此刻 `hwnd` 尚空故落到 `GetDC(nullptr)` → 进程首窗 `scale` 恒 `1.0`；`handle_size` 与 `handle_getminmaxinfo` 却各自现调 `dpi_scale()`（感知生效后是真实的 1.5），`set_size` 与鼠标/IME/a11y 坐标又读那个 cached 成员——三方记账发散。本仓实测数值与该机制逐位吻合：最大化时 `WM_GETMINMAXINFO` 按 1.5 放大报给 OS（物理 `3840x2054`）、`WM_SIZE` 再除回 1.5 得逻辑 `2560x1369.33`；还原侧按 `1.0` 建物理 `960x640`、除回 1.5 得逻辑 `640x426.667`。处置按裁决 7.13① / 7.31③ **不等不绕**：由 Aurora 侧收敛为单一真值源（`GetDpiForWindow` + `WM_DPICHANGED` / 跨屏迁移重建缓存；建窗、`WM_SIZE`、帧缓冲、上报四面同源），并配「150% / 175% / 200% 下逻辑尺寸 × scale 与物理客户区逐位相等」的用例——**解除上述两处 SKIP 即为本条的验收**；本仓在此腿上的代码零改动。**受阻的验收**：`SPEC.FEAT.RENDER.01` 与 `RENDER.05` 的真机无错位判据、`SPEC.FEAT.XFER.01` 的「UI 取值 → 下发行列」在高分屏上的正确性——`HeadlessSurface` 侧的像素用例不受影响（其 scale 由测试自己注入），故该缺陷**只在真机暴露**，与 §3.5 的焦点盲区同构。

---

## 10 交互层

### 10.1 键盘映射

完整转发 Ctrl/Alt/Shift/Meta 组合键、功能键、方向键；控制字符直通；`DECCKM` 与 keypad 应用模式生效（`SPEC.FEAT.INTERACT.01`）。

**已落地（2026-10-02，裁决 7.30）**：编码表 `term::encode_key` + 视口的 `on_key_event` / `on_text_input` 两入口，遗留档（xterm）口径逐字节定死并有 7 例单测；链路形状由 `itest_key_input`（替身连接观测、`DECCKM` 经真实状态机喂入）守，真机对端接受度由 `etest_key_forwarding`（cmd.exe 的行编辑与命令提交）守。功能键、方向键、控制字符、可打印文本与 Alt/Meta 前缀的**编码**齐备；`Tab` 与 Alt 系的**到达**、以及 keypad 应用模式的**可发之键**仍受 §9.6 的 G14 / G15 / G16 阻塞。kitty keyboard protocol / `modifyOtherKeys` 是需求原文的延后观察项，本件对「无遗留编码可发」的键返回空而不发新式序列。

### 10.2 选择与复制粘贴

- 流式拖拽选择、矩形块选择：可先行实现；
- 双击选词 / 三击选行：框架侧 `click_count` 已闭合（G2，裁决 7.29②），实装随 `SPEC.FEAT.INTERACT.02` 那一棒，本仓不自算多击；
- 复制语义三开关默认关闭，保留原样为默认；bracketed paste 激活时原样透传不节流（`SPEC.FEAT.INTERACT.03`）。

**已落地的纯逻辑件（2026-10-02，裁决 7.32）**：`ui::row_spans`（`include/borealis/ui/selection.h` + `src/ui/selection.cpp`）把两个端点归一成按行升序的**闭开列区间**表，`ui::copy_text` 由区间逐行取文本。三处与线程/网格模型相关的架构决定：

- **选区存存储行序，不存副本行序**。权威网格由后台读线程持有（§3.4），跨可见区的复制只能在会话锁内按 `grid::Storage::line` 的索引取行；主线程 `session::ScreenMirror` 的行号随回看偏移变，选区若存它就在滚动时漂移。于是「选区随 scrollback 滚动跟随」由坐标空间表达，本件无状态、不含框架类型也不含 `config` 类型（后者会成 `config ⇄ ui` 模块环），三项复制变换以 `ui::CopyOptions` 表达、由调用方从 `config::TerminalSettings` 搬值。
- **文本口径**：行以 LF 分隔且剪贴板侧不翻译（行尾的唯一决策点留在粘贴策略与串口 `line_ending`）；选区左界落在双宽延续格上时整字符纳入，延续格永不产第二个字符；越界行（历史已溢出）不产文本也不补空行。
- **变换次序固定**为「剥 tmux 细线制表符 → 剥行尾空白 → 合并反斜杠续行」，前一步的产物是后一步的判据。

**余下未落**：鼠标按下/拖动/抬起 → 格子坐标的换算与选区状态机、选区高亮的绘制层（`ui::rect_for` 已能按行列区间出矩形）、剪贴板写入与 copy-on-select / 右键三态、`Session::copy_text`（计划 / 待建：在既有短临界区内取文本后交回主线程）、粘贴侧的多行警告与逐行节流。另有一条显式欠项：scrollback 饱和后新输出把最旧行挤出，其余行的存储索引整体减一，仍活着的选区因此相对内容下移一行——消除须给存储加单调行号，随界面腿一并处理（裁决 7.32②）。

### 10.3 搜索

100,000 行 scrollback 下首次搜索响应 ≤ 200 ms（P95）；搜索在网格层做，不经 UI 遍历。读取路径与持锁口径见 §3.4（后台权威 grid、按行分片短持锁）——主线程长持锁搜索会阻塞会话读线程，威胁 `SPEC.NF.PERF.01`。

### 10.4 URL 检测

视区内 URL 识别（含 OSC 8）；打开前确认可配；协议白名单默认仅 http/https（`SPEC.FEAT.INTERACT.05`）。

### 10.5 输入法

框架 `TextCompositionEvent`（含 preedit，Win32 / X11 / Wayland 三后端已实现，见 `SPECIFICATIONS.md` 附录 A.1）→ 视口就地绘制组合中文本于光标单元格、候选窗按「光标 → 屏幕坐标」换算定位、commit 文本经会话编码写入（`SPEC.FEAT.INTERACT.06`、`SPEC.FEAT.TERM.09` 发送方向）。**终端为自绘网格，框架不会自动把 cell 内容暴露给输入法**，preedit 的绘制与坐标换算须由视口自行实现；坐标换算须与 `SPEC.FEAT.RENDER.05` 的 DPI 缩放口径一致，否则高分屏下候选窗错位。

### 10.6 无障碍（延后项的落点约束）

`SPEC.NF.A11Y.01` 为延后观察项，但架构约束先记录：终端视区是自绘网格 widget，框架语义树不会自动派生 cell 内容，届时须经 Aurora 无障碍平台桥（Aurora 主仓 `codespec/ARCHITECTURE.md` §8.5）挂接应用侧语义钩子（行文本聚合、光标位置、选区状态）。该工作面在无障碍开工前不可回避，提前记录以免届时被迫重构视口。

---

## 11 外观与配置

### 11.1 主题与字体

内置配色 + 自定义 16 色重映射，跟随 Aurora `Theme` / `ThemeScope`（`SPEC.FEAT.PREF.01`）。**首版即落 ≥8 套预置，缺省 Dracula**（另含 Nord / Solarized Dark / One Dark / Gruvbox Dark / Monokai / Campbell / Tokyo Night，裁决 7.26②）；每套预置给 16 色 + 默认前景/背景 + 光标色，色值表归 `borealis::config`，`ui::PaletteSpec` 只消费合成后的最终值。**UI chrome 不随终端主题切换**（裁决 7.25⑪）：标签栏/侧栏/状态栏固定一套 token，主题结构预留可选的 chrome 覆盖字段而缺省不覆盖。默认字体内置 Cascadia Code（裁决 7.3），经 `register_font_memory` 装载。**Cascadia Code 不含汉字字形**，故须配 CJK 缺字回退链（框架缺字链回退能力，见 `SPECIFICATIONS.md` 附录 A.1）：默认回退至系统等宽 CJK 字体，顺序可配；回退字形仍按 `SPEC.FEAT.TERM.08` 占两格，不得因回退破坏网格对齐（`SPEC.FEAT.RENDER.02`、裁决 7.16）。

### 11.2 命令、快捷键与面板

复用框架 `CommandRegistry` 与 `CommandPalette`；**全部 UI 动作一律注册为 Command，快捷键绑定以 Command 为锚**（与 `SPEC.FEAT.WS.07`、`SPEC.FEAT.PREF.04` 统一）。命令 id 一律 ASCII 英文入框架，**中/英词条由本仓 `StringTable` 的「命令 id → 词条」映射表维护**（裁决 7.25⑬、`SPEC.FEAT.PREF.05`），面板渲染时按当前语言查表；新增命令须同步词条。

### 11.3 持久化与韧性

经 Aurora `Preferences` 落 JSON，schema 带版本号；写入原子化（临时文件 + 重命名）；启动校验失败则备份为 `*.corrupt-<时间戳>` 并回落默认配置启动，**绝不静默清空**（`SPEC.FEAT.PREF.03`、`SPEC.FEAT.PREF.07`）。版本升级迁移策略待定（首版仅预留版本号字段），见 §16 F。

本节的执行形态由裁决 7.26 定死四处：**①存储**＝单文件 `borealis.json`（`Preferences::default_config_dir()`）+ 按域 `group()` 嵌套 + 顶层 `schema_version`，框架元数据在 `__aurora_preference_meta__` 下、对应用侧的 `keys()`/`get()` 不可见，故自校验只看用户数据树（附录 A.2 G7）。**②原子写不在本仓重造**——`Preferences::flush()` 已实现「`<file>.lock` 跨进程 advisory 锁 + 临时文件 + 原子 `rename`」，本仓只负责提交时机。**③降级线提前到首版**（`SPEC.FEAT.PREF.07` 的该腿随 M1 落）：缺键 / 类型不符 / 域外 → 回落默认并记诊断，未知键只记录不报错；JSON 解析失败（经 `last_load_error()`）或 `schema_version` 高于本仓支持 → **先备份 `*.corrupt-<epoch 秒>` 再回落默认**，备份必须早于任何 `flush`，否则「不静默清空」只剩口号；「显著提示」以 `LoadOutcome` 公共出口交 UI 侧，对话框落点 `TODO(SPEC.FEAT.PREF.07)`；快照回滚与导出导入留原分期。**④首启不写盘**：首次启动只在内存得到全量默认值，用户第一次变更才 `flush`。凭据一律不入 schema（§12.1）。

落盘形态另有三条由**裁决 7.27** 定死的硬约束，它们来自框架 `Preferences::reconcile()` 的实现（拍平成点号复合键再按点号重建嵌套，只有数组是叶子）而不是本仓的选择：**⑤键名不得含点号**——对象键里的点会被拆成一层层嵌套对象，`shortcuts.overrides` 里的命令 id（`terminal.new_tab` 形态）因此不能作键；**⑥可空的映射落为数组**——空对象在拍平时不产生条目、重建后即消失，故覆盖表落 `[{command, combo}, ...]`，内存模型仍是映射，元素形态不合只丢该元素并留痕其下标；**⑦色值的回落线以「本文件点名的主题」为基准**——`palette.cursor` 缺键回落该主题的光标色并留痕，显式 null 才是「未配」（绘制侧回落 `default_foreground`，裁决 7.25③），开关与阈值不属色值、回落一律随 `Settings{}`。

`borealis::config` 的模块边界：`settings.h` 是唯一默认值来源（`Settings{}` 即首次启动那份），`themes.{h,cpp}` 是色值唯一来源（`AppearanceSettings` 的默认调色板由它派生，开关不入库表以免成为第二真值源），`store.{h,cpp}` 是与框架的唯一读写接缝（`Preferences` 藏在 PIMPL 内，本头与 `settings.h` 均不含 Aurora 类型，架构 §2.2）。三者的验收判据是「写盘再读回逐域等值」，故 `ui::PaletteSpec` 与四域结构各带默认比较（C++20 不隐式声明 `==`，缺了就整份配置的等值被静默删除）。

### 11.4 i18n

经 Aurora `StringTable`（`SPEC.FEAT.PREF.05`）。

### 11.5 界面层的绑定量（四屏草图收口，裁决 7.25）

`codespec/UI_OVERVIEW.draft.md` 的 N1~N8 落到实现侧的六条绑定行为，作为 `src/ui/` 与像素用例的对照表（完整尺寸与颜色表仍在该稿 §2）：

- **侧栏缺省折叠**，展开态入配置并记住（N1，与 `SPEC.FEAT.PREF.06` 零配置可用同口径）。
- **窄标签态状态角标优先于连接类型图标**，且只在非活动标签上让位（N2）。
- **状态栏每项一个开关**入 `borealis::config`，缺省全开；宽度不足时按注册次序从右向左省略，被省略项进右键菜单，不压缩字号（N3）。
- **pane 底部角标仅分屏态显示**（N4）。
- **设置页实时预览复用终端视口的真实绘制路径**（N5，依赖 `SPEC.FEAT.RENDER.01`）。
- **右键菜单单层 + 分隔线分区，低频簇（复制语义三开关、连接类动作）收二级子菜单**，一级可视项 ≤10（N7）。

---

## 12 安全边界

1. **凭据**：OS 凭据库存储 + 引用句柄；不可用则降级「每次询问」，非明文（`SPEC.FEAT.CONN.09`）。
2. **审计**：审计范围为**配置目录**——全目录 grep 无明文凭据，做成**自动化用例**而非人工检查。日志目录不在审计范围内，单独标注为「用户显式开启、风险自负」。
3. **剪贴板授权**：OSC 52 读方向默认禁止，三态可配；被拒时须回写空响应，避免远端程序挂起（`SPEC.FEAT.CONN.12`）。
4. **会话日志**：默认关闭，开启须逐会话显式动作并给出风险提示；可选敏感输出过滤（裁决 7.9、`SPEC.FEAT.CONN.11`）。

---

## 13 可观测、诊断与性能门禁

1. **日志**：全链路分级，走 Aurora `Logger`；禁止裸标准输出（`AGENTS.md` §4.3）。
2. **诊断计数**：VT 未知序列计数、非法字节序列计数、背压水位与合并次数——进调试面板（`SPEC.NF.RELI.01`）。
3. **性能观测**：接入 Aurora `PerfOverlay` / FrameStats / PerfLog；区分空闲帧（`SPEC.NF.PERF.05` 空闲 CPU 指标依赖此区分）。
4. **基准与门禁**：吞吐、输入延迟、内存、空闲 CPU 以基准工具维护回归基线，劣化超过阈值即 FAIL（`SPECIFICATIONS.md` §5）；性能类结论必须有 benchmark 支撑。

---

## 14 测试、CI 与分发

### 14.1 测试分层

复刻 Aurora 注册式框架：`utest_`（单元）/ `itest_`（集成）/ `etest_`（真实后端端到端），套件名恒等于文件 stem，**禁止自定义 `main()`**（`AGENTS.md` §4.4）。

### 14.2 纯逻辑可测性与回放夹具

VT 解析器、网格模型、宽度判定、编码解码、scrollback、背压合并策略**均不得依赖 UI 才能测试**；转义序列行为以 `tests/fixtures/` 回放夹具驱动断言。可利用 Aurora 确定性渲染不变量（同树同尺寸 → 同像素）做 `HeadlessSurface` 快照比对。

### 14.3 无头优先

依赖真实后端或需断言像素的场景用 `HeadlessSurface`，避免引入交互式 GUI 测试。

### 14.4 跨平台验证通道

Linux 侧验证通道与 CI 形态属实施细节，待定；本机 WSL 与 CI 两条路径的取舍随相应阶段开工前裁决（见 `PLAN.md` §7）。

### 14.5 分发

Windows 便携 zip + 安装包；Linux tar 通用 + AppImage；产物含第三方许可声明（Cascadia Code OFL、SSH 栈许可等）。安装包须支持覆盖升级且保留用户配置（`SPEC.NF.PKG.01`）。

---

## 15 与 Aurora 主仓的边界

### 15.1 缺口类别分流（裁决 7.13）

1. 渲染与事件链路上的缺口（影响公共 API 形态）：撞到即先在 Aurora 侧补「公共 API + 单测 + 文档回写」，本仓不等不绕；
2. 交互体验类缺口（用现有公共 API 即可组合实现）：留在应用侧，不强行推入框架。

### 15.2 反哺承诺

进框架的原语必须以 Aurora 公共 API 形式存在，本仓只消费公共头，**不长期持有框架分叉**。网格吞吐基准随框架原语一并贡献为 Aurora `tools/bench` 用例并挂性能回归门禁（见 `SPECIFICATIONS.md` 附录 A.3）。

### 15.3 当前阻塞项（2026-09-29 实测起账，2026-09-30 / 2026-10-01 / 2026-10-02 三次复验）

| 缺口 | 现状 | 影响 |
|:---|:---|:---|
| G1 网格 / 批量文本绘制原语 + 整像素 cell 度量 + East Asian Width 判定 | **三腿全部闭合（2026-10-01 实测）**：宽度判定 = Aurora `unicode_cell_width`（裁决 7.20）；批量文本 run = `Painter::draw_text_runs(std::span<const render::TextRun>)`；整像素单格度量 = `render::FontEngine::monospace_cell` → `render::CellMetrics`（形态、逐位一致判据与「时间门禁改判为像素级回归」见裁决 7.22） | 绘制层直接按 §9.2 消费两原语，应用侧自绘过渡备路径不再启用；网格模型、脏行 diff 与颜色合成仍在本仓；宽度腿已接真表，状态机与网格层不再挂单宽缺省（§6.3） |
| G2 `MouseEvent` 多击语义 | **已闭合（2026-10-02 实测）**：`MouseEvent::click_count`（1..3，Release / Move 恒为 1）由 `EventDispatcher::dispatch_mouse` 在派发前集中判定，阈值是库内常量 500ms / 4dp（未接系统双击速度），形态与判据见裁决 7.29② | 双击 / 三击能力**不再受框架阻塞**，实装随 `SPEC.FEAT.INTERACT.02` 那一棒；本仓未自算多击 |
| G9 OS 级全局热键 / G11 跨平台系统通知 | **均已闭合（2026-10-02 实测）**：`OsHotkeyRegistry` + `OsHotkeyHandle`（裁决 7.29③）、`NotificationCenter::notify` + 激活回传 + 「仅记录」测试后端（裁决 7.29④） | 两条的框架依赖解除；本仓消费者分别在 `SPEC.FEAT.WS.12` 与 `SPEC.FEAT.INTEG.03` 的落期上，未提前实装 |
| G13 批量入口的排版选项 | **已闭合（2026-10-02 实测）**：`draw_text_runs` 另有 `(runs, opts)` / `(runs, aa_mode, opts)` 重载，opts 整批共用、框架刻意不做 per-run opts（裁决 7.29①） | §9.2 的逐片段分流已撤销，改按「是否斜体」分两批；像素用例以变异自证守住 |
| G14 `Tab` 派发 / G15 Win32 Alt 系 / G16 小键盘与 NumLock | **开放（2026-10-02 键盘映射棒实测新增）**：三处原始事实见 §9.6 与 `SPECIFICATIONS.md` 附录 A.2，按裁决 7.13① / 7.30⑤ 派发 Aurora 侧补全 | 阻塞 `SPEC.FEAT.INTERACT.01` 的「完整转发」完整度与 keypad 应用模式腿，不阻塞该条开工（本仓已落首版）。本仓编码层已把 `Tab` / `Shift+Tab` / Alt 前缀写对，回货后接入的是派发侧；`SPEC.FEAT.PREF.05` 的 Alt 系快捷键同样受 G15 制约 |
| G17 DPI 缩放真值源不统一 | **开放（2026-10-02 真机走查实测新增）**：`ctx.scale_factor` / `Painter::scale()` 恒 1.0，而窗口逻辑尺寸随路径给出三个互不相容值；事实、推断成因与判据见 §9.6 与附录 A.2，按裁决 7.13① / 7.31③ 派发 Aurora 侧收敛为单一真值源 | 阻塞 `SPEC.FEAT.RENDER.01` / `RENDER.05` / `XFER.01` 的**真机**验收腿（150% 屏上网格覆盖不足、还原后比启动还窄）；`HeadlessSurface` 像素用例与纯逻辑层不受影响（scale 由测试注入），故只在真机暴露。本仓换算已单源，回货零改动 |

> 框架现状以 Aurora **当日活动分支实测**为准；上表的绘制原语两腿为 2026-09-29 实测结论，宽度判定一腿为 2026-09-30 在 Aurora 当日活动分支落地的实测结论；可能随上游提交变化，复用前须复验。

---

## 16 待定决策清单

| # | 议题 | 候选方案 | 状态 |
|:---|:---|:---|:---|
| E | 单实例转交通道（§3.1） | Windows 命名管道 / 本地 socket / `WM_COPYDATA` 定位既有窗口 | 待拍板——随 `SPEC.FEAT.INTEG.04` 开工 |
| F | 配置 schema 版本升级策略（§11.3） | 启动时逐版本迁移链 / 拒绝旧版本并提示导出导入 | 待拍板——随首个 schema 变更裁决。**首版行为已定**（裁决 7.26③）：`schema_version` 高于本仓支持时不迁移，直接备份 + 回落默认；迁移链形态仍未决 |

> 本清单随评审收敛；每项拍板后写入正文对应章节并从本表移除。
