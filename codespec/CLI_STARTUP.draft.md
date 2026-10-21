# CLI 启动参数与单实例转交 · 设计稿

> 需求：`SPECIFICATIONS.md` **`SPEC.FEAT.INTEG.04`**；落点裁决 **7.7**。
> 本稿只定形态与边界，不写实现叙述。状态：**四批全部落地、结项**（2026-10-21，裁决 **7.124**；§12 为结项读数与未验面）。**2026-10-21 批 6 就地改口一处**：`--new-window` 由「拒绝（WS.03 未落地）」转为真动作——多窗口本体已落地，§2 语法、§5.3／§5.4、§6 线格式的 action 值、§7 退出码 2 的口径、§9 三套件案数与变异清单、§10 边界第 2 条均已更新，形态与台账见 `WS03_MULTI_WINDOW.draft.md` D10 与 §22；本稿其余读数原样有效。
> 地基读数均以当前工作树为准；未实测的标「推断 / 未验证」。

## 0 范围与不在范围

**在范围**：进程启动时的参数解析（`--profile <name>`、`new-tab` / `split-pane`、`--cwd <dir>`、`--new-window`）、已有实例的参数转交（裁决 7.7：默认落在**当前窗口新标签**）、两个平台上的单实例通道、装配层把请求落到既有开标签 / 分屏路径。

**不在范围**：`--title` / `--size` / `--tab-color` / 一次多动作等需求未列的参数；shell 补全；多窗口本体（`SPEC.FEAT.WS.03`）；分屏多会话（`SPEC.FEAT.WS.02`）；把 CLI 请求持久化成「恢复会话」。

## 1 地基实测（F）

| # | 事实 | 依据（符号 / 章节） |
|:--|:--|:--|
| F1 | 进程入口是 `auto main() -> int`，**不收 `argc/argv`**；`src/main.cpp` 不编入测试 runner | `src/main.cpp` 的 `main`；裁决 7.83 |
| F2 | 应用单窗口装配：一个 `au::Window` + 一个 `au::Application`，标签 / 工作区 / 会话表都是 `main` 内局部状态 | `src/main.cpp` 的 `tab_workspaces`、`root_column`、`app.run()` |
| F3 | Aurora 有 `Application::open_window`，但本仓没有任何第二窗口的 scene / 状态副本；多窗口属 WS.03 | Aurora `app/application.h` 的 `open_window`；`ARCHITECTURE.md` §16 与 `SPEC.FEAT.WS.03` |
| F4 | 开标签已分腿：`create_new_tab`（缺省本地）、`connect_profile`（Local / SSH / 串口 / Telnet 分流）、`open_*_tab`；新标签即选中 | `src/main.cpp` 的 `create_new_tab` / `connect_profile` / `open_*_tab`；`ui::TabStrip::add` |
| F5 | 档案只有按 **id** 的 `ProfileStore::find`，没有按展示名解析；展示名 `Profile::name` 可重名 | `include/borealis/conn/profile.h` 的 `ProfileStore` |
| F6 | 起始目录优先级今天只有「显式本地档案 > OSC 7 继承 > 配置值」；`create_new_tab` 直接调 `choose_start_directory` | `include/borealis/ui/osc7.h`；`src/main.cpp` 的 `create_new_tab` |
| F7 | 分屏生产工厂 `make_pane` 返回**同一个视口 / 会话**（`TODO(SPEC.FEAT.WS.02)`）；UI 的切分键已可用，但两个 pane 今天只能看同一会话 | `src/main.cpp` 三处 `ws_hooks.make_pane`；`itest_workspace_broadcast` 的 shared_view 档 |
| F8 | `WorkspaceView::split_pane` / `run_command` 是私有；装配层没有「非按键触发一次切分」的公共入口 | `src/ui/workspace_view.h` |
| F9 | 单实例通道在 `ARCHITECTURE.md` §16 E **待拍板**；§3.1 要求「经进程间通道由主线程消费」 | `ARCHITECTURE.md` §3.1 / §16 E |
| F10 | 已有主线程回投形态：后台线程只入本仓队列 + `Surface::request_wake()`，主线程在 `set_on_frame` 排空（不依赖 Aurora `detail`） | `ARCHITECTURE.md` §3.2；`src/main.cpp` 的 `set_on_frame` |
| F11 | 平台层已有 POSIX `forkpty` / TCP 与 Win32 对应腿；无 Unix socket / 命名管道 / 进程参数腿 | `src/platform/{posix,win}/`；`src/platform/tcp.h` |
| F12 | Win32 已链接 `shell32`（`open_url`）；参数取 Unicode 需 `GetCommandLineW` + `CommandLineToArgvW`，不能靠窄 `argv` | `src/CMakeLists.txt` 的 Win32 链接；本仓与 Aurora 均无现成 helper（实测） |
| F13 | `--cwd` 的存在性 / 相对路径语义只有平台腿能判；§2.2 的分层不许把平台路径知识放进纯逻辑件（AGENTS §4.5 第 23 条） | `ARCHITECTURE.md` §2.2；`conn/local_terminal.h`；`platform/posix/local_terminal.cpp` |

## 2 命令行语法（D1）

```
Borealis [new-tab | split-pane] [--profile <name-or-id>] [--cwd <dir>]
         [--new-window] [--help]
```

- **一个调用只表达一个动作**：`new-tab` 与 `split-pane` 互斥；重复出现即用法错误。缺省动作＝`new-tab`。`--new-window` 是**第三个动作的写法**而不是修饰符——它与两个子命令双向互斥（先写哪个都拒），重复出现幂等（WS.03 稿 D10／§22⑴，2026-10-21 批 6 落地后改口）。
- `--profile` / `--cwd` 各最多一次，值不得为空；未知选项、位置参数、缺失值一律**退出码 2**。
- 选项顺序无关；子命令可出现在选项前或后。
- `--help` 在**本地**打印用法并退出 0，不占用 / 不查询单实例通道。
- 所有字面量 ASCII；用法文本与错误文本也是 ASCII（诊断不属 CJK 例外，AGENTS §4.3 第 14 条）。
- 语法层不碰文件系统、不解析档案；它只产出一张结构化请求。

**为何不做多动作**：需求只列子命令，未要求一次开多标签；多动作会把「一次请求对应一个回复」的协议与失败原子性一起复杂化。留作观察项，不预埋。

## 3 单实例通道（D2；原 §16 E 的推荐，2026-10-21 按此拍板，现见 `ARCHITECTURE.md` §3.8）

### 3.1 选型

| 候选 | 结论 | 理由 |
|:--|:--|:--|
| POSIX 本地 socket + Windows 命名管道 | **采用** | 面向字节流、天然限本用户、无需窗口句柄；两平台语义可对齐 |
| 本地 TCP 回环 | 否 | 端口占用 / 防火墙提示 / 同机其他用户可连；还要做令牌认证 |
| `WM_COPYDATA` | 否 | 必须先有窗口句柄，首个实例建窗前的竞态无解；只能 Win32；承载结构化 UTF-8 还要自定义消息生命周期 |
| 文件锁 + 文件内容 | 否 | 无请求 / 回复通道；文件轮询延迟与清理都不可控 |

### 3.2 通道形态

- **POSIX**：`AF_UNIX` / `SOCK_STREAM`。路径取 `$XDG_RUNTIME_DIR/borealis-<uid>/instance.sock`；变量缺失或不可用时回落 `/tmp/borealis-<uid>/instance.sock`。目录 `0700`、socket `0600`；目录已存在时校验属主与权限，不符即失败（不冒险复用别人的目录）。路径超 `sun_path` 上限时回落到 `/tmp` 形态，仍超则失败。
- **Windows**：命名管道 `\\.\pipe\borealis-<user>`；首个实例用 `FILE_FLAG_FIRST_PIPE_INSTANCE` 认领，第二个进程得到 `ERROR_ACCESS_DENIED` 即「已有实例」。默认 DACL 即「创建者 + 管理员」，不再自造安全描述符。
- **认领权威是锁而不是「能否 connect」**：POSIX 首实例在生命周期内持 `flock(LOCK_EX)` 的 `instance.lock`；拿不到锁即已有实例。抢到锁后先 `unlink` 陈旧 socket 再 `bind`，故崩溃残留不会把用户永久挡在门外。Windows 以「首个命名管道实例」等价认领，进程退出即由内核回收。
- **帧格式**：`u32 little-endian 长度` + 载荷；请求载荷上限 **16 KiB**、回复上限 **4 KiB**，超限即拒（不截断）。外层长度让 Windows 字节模式不必依赖半关闭。
- **并发**：一次只处理一条连接（本地 CLI 转发没有吞吐需求）；连接排队由 listen backlog 承担。
- **超时**：转发端 2 s 内拿不到回复即失败退出（不静默启动第二实例）；监听端等待主线程回执同样有上限，超时关闭连接。

### 3.3 线程与主线程消费

- 监听端只起**一条** transport 线程：`accept`（POSIX，`poll` 200 ms 轮询停止位）/ `ConnectNamedPipe`（Windows）→ 读请求 → 交给注入的 handler → 等 handler 回执 → 写回复。
- handler 在 transport 线程上**只做**「把请求对象塞进本仓有界队列 + `request_wake()`」；真正的解析、档案解析、开标签 / 分屏都在主线程 `set_on_frame` 的帧边界执行。这与 `ARCHITECTURE.md` §3.1 / §3.2 的既有回投形态同构，不新增第二条 UI 线程。
- 请求对象自带 `mutex` / `condition_variable` / `complete(reply)`；主线程执行完调用 `complete`，transport 线程被唤醒后写回。监听端在应用退出时置停止位、关闭监听句柄并 join；未完成的请求按超时收尾。
- 队列有界（8 条）；满时拒绝新连接（回复「busy」而不是无限排队）。

## 4 请求解析与档案解析（D3 / D4）

- 语法解析是纯函数：`cli::parse_startup_args(args) -> {request, help, error}`，不读盘、不解析档案。
- 档案解析**只在首实例的主线程**做，针对**当前内存里的** `settings.profiles`：精确 id → 精确展示名 → 唯一的大小写不敏感展示名。命中多条即报「ambiguous」并给出条数；不做模糊搜索（`ProfileStore::search` 是给搜索框的，语义不同）。
- 解析失败不猜、不退回第一条；错误文本是 ASCII 诊断。
- 展示名可重名的现实由「唯一命中才接受」兜底；用户仍可用 id 精确指定。

## 5 动作语义（D5 / D6 / D7）

### 5.1 `new-tab`（缺省）

- 有 `--profile`：`connect_profile` 走既有四类分腿；SSH 需要材料时照既有凭据询问链弹框。**不再额外开缺省本地标签**。
- 无 `--profile`：本地终端，沿用 `local_shell` 与既有起始目录链。
- `--cwd`：只对**本地**语义生效（缺省本地或 `Local` 档案）。优先级为 **CLI 显式目录 > 档案目录 > OSC 7 继承 > 配置值**；对 `Local` 档案是覆盖其 `local.working_directory` 的本次副本，不改档案本体。
- `--cwd` + SSH / 串口 / Telnet：**拒绝**（档案里没有远端 cwd 字段；静默忽略会让用户以为切了目录）。
- 相对路径在**发起进程**里先折算成绝对路径再进协议（否则转发后会按首实例的进程目录解析）；存在性由首实例在动作执行时校验，非目录即拒绝。

### 5.2 `split-pane`

- 动作＝对当前标签的焦点 pane 执行与切分键同一条路径（`WorkspaceView` 公开一个装配层入口；默认右侧切分，与 `Ctrl+Shift+D` 同轴）。
- **`--profile` / `--cwd` + `split-pane` 一律拒绝**：当前生产工厂让同标签所有 pane 共享一个会话（F7），换档案 / 换目录需要 WS.02 的多会话工厂。宁可不做，也不静默把当前会话复制一份。
- 冷启动带 `split-pane`：先按 5.1 建首标签，再切分；转发到已有实例：切当前标签。
- 单 pane 数量上限 / 最小尺寸沿用 `WorkspaceView` 既有约束；切分失败回 ASCII 诊断。

### 5.3 `--new-window`

> **2026-10-21 改口（批 6；口径与台账见 `WS03_MULTI_WINDOW.draft.md` D10 与 §22）**：本节原先那条「已有实例 ⇒ 拒绝并回 `SPEC.FEAT.WS.03` 未落地」的诊断已**撤销**——多窗口本体落地了，这面旗标自此是真动作。以下四档是现口径。

- **它是第三个动作的写法，不是修饰符**：与 `new-tab`／`split-pane` 双向互斥（先写哪个都退 2，诊断带上冲突的两侧名字），重复出现幂等；解析产出里 `new_window` 与 `action == NewWindow` 同真同假。理由＝「在新窗里切当前窗的那一格」没有可读语义，而一次请求只该表达一件事。
- **已有实例**：转交侧收到即**开一扇新窗**，窗里的首标签吃这一份料（`--profile` 的档案或 `--cwd` 的目录；都没给则按 D1 落一枚缺省本地标签）。档案解析、政策判定与目录存在性排在**开窗之前**——失败即回 ASCII 诊断并退 2，不留一扇空窗。开成新窗后向本仓的 `ActiveWindowTracker` 上报该窗号：四条档案腿是经 `tab_host_or_none` 现取落点的（含凭据询问那条异步续跑），不报则远程档案那一格可能开回旧窗。
- **冷启动**：本就是新窗口，与缺省 `new-tab` 共用同一条腿（正要建的这一扇就是那扇新窗），不额外开窗、不当错误；真机上「冷启动＋旗标」与「冷启动＋缺省」应当**完全同形**。
- **混版两档**：旧 CLI（只写 flags bit0）打新实例 ⇒ 同一含义照办；新 CLI 打旧实例 ⇒ 旧解码器不认识 action 值 2，**整帧 fail-closed 拒**、转发端退 2（拒的是整条请求，不是那枚旗标）。
- 需求 7.7 的这条缺口自此闭合；单实例进程模型（裁决 7.8）不变——新窗由**首实例**开，不是再起一个进程冒充窗口。

### 5.4 冷启动与转交的先后

- 冷启动请求在**窗口与接线全部就绪之后、`app.run()` 之前**执行一次；首帧直接画出目标标签，不出现「先空一帧再补标签」。
- 转交请求在 `set_on_frame` 开头排空；执行后要求重排 / 重绘，并在回复里回告成功或 ASCII 诊断。
- 冷启动的 `--profile` 在**建窗前**先做一次可解析性校验（档案存在、类型与 `--cwd` / `split-pane` 相容）；失败即退出 2，不建窗、不启动 shell。
- 批 6 之后同一条次序也管住**转交侧**：`--new-window` 的档案解析、政策判定与 `--cwd` 目录存在性排在**开新窗之前**，被拒的请求在桌面上不留一扇空窗（真机读数位＝`WS03_MULTI_WINDOW.draft.md` §9 第 5 条⑦）。
- 冷启动若因运行期原因动作失败（如切分被几何约束拒绝），记录诊断并保留已建的首标签，继续运行；转交请求则回错误。

## 6 协议载荷（D8）

请求载荷（纯逻辑件 `cli` 负责编解码，平台通道只见不透明帧）：

```
offset 0   "BORI"            4 bytes
offset 4   version = 1       u8
offset 5   action            u8   0=new-tab  1=split-pane  2=new-window
offset 6   flags             u8   bit0=new-window（旧版 CLI 只写这一位；新 CLI 两处都写）
offset 7   reserved = 0      u8
offset 8   profile_len       u16 LE
offset 10  cwd_len           u16 LE
offset 12  profile bytes     UTF-8
...        cwd bytes         UTF-8
```

回复载荷：

```
offset 0   "BORR"            4 bytes
offset 4   version = 1       u8
offset 5   status            u8   0=ok  1=error
offset 6   message_len       u16 LE
offset 8   message bytes     UTF-8（ASCII 诊断；成功时为空）
```

- 两侧长度先于越界检查；magic / version / reserved / 尾随字节 / 超限任一不符即整帧拒绝（fail-closed）。
- **action 的合法值界取自本版枚举而不是写死数字**（编码与解码共用同一枚上界）：本版收到表外的值即整帧拒、不猜半个请求，故「新 CLI 打旧实例」的形态是明确报错而非静默错办（§5.3 混版两档）。界与表同源，加第四档时不会出现「编码放行、解码拒收」的错配。
- 没有指针 / 句柄 / 明文凭据进入协议；只有动作、档案名、绝对目录、窗口标志。

## 7 退出码（D9）

| 码 | 含义 |
|:--|:--|
| 0 | 成功（冷启动继续运行；转交已被首实例接受并执行） |
| 1 | 建窗 / 内部失败 |
| 2 | 用法错误、档案不可解析、动作被拒（批 6 前还含「`--new-window` 未落地」那一档，现已撤；WS.02 的 `split-pane` 边界照旧，另加「`split-pane` ＋旗标」这类只可能来自手工帧的错配） |
| 3 | 已有实例但通道不可达 / 超时（不回落成第二实例） |

- CLI 的用法与错误是**产品输出**，走 `AURORA_LOG_RAW`（AGENTS §4.3 第 15 条），不新增 `std::cout` / `printf`。

## 8 平台参数腿（D10）

- `platform::process_arguments(argc, argv)`：POSIX 原样交 `argv` 字节；Windows 走 `GetCommandLineW` + `CommandLineToArgvW` 再折 UTF-8。窄 `argv` 在 CJK / 非 ANSI 路径上会丢字，不能作为 Windows 的权威来源。
- 拆成独立平台腿而不是在 `main` 里 `#ifdef`：AGENTS §4.5 第 23 条。
- 不引 `wmain`：两平台共用同一 `main` 形态，避免第二套入口与 CMake 子系统细节。
- 路径 → `std::filesystem::path` 用 `std::u8string` 构造，避免 Windows 窄路径按 ACP 解释；这是标准库转换，不新增平台 API 面。

## 9 测试与自证（D11）

| 层 | 套件 | 判据 |
|:--|:--|:--|
| 纯逻辑 | `utest_cli_startup`（新，**14 案**，批 6 由 12 扩） | 缺省动作、**三个动作互斥**（两个子命令之间、旗标与任一子命令双向）、选项顺序、重复 / 缺值 / 未知 / 位置参数、`--help`、ASCII 用法；档案解析的 id / 名 / 大小写 / 重名 / 缺失；`--cwd` + 非本地档案的拒绝策略（批 6 起该案的判定循环含 `NewWindow` 一档：新窗那一格吃的就是同一份料，故不设拒绝档） |
| 纯逻辑 | `utest_cli_handoff`（新，**7 案**，批 6 由 6 扩） | 请求与回复往返（含 UTF-8 目录 / 档案名）、magic / version / 长度 / 尾随字节 / 超限拒绝；**action 值 2 往返 + 旧版「动作 0＋旗标 1」仍读得通 + 表外值不交出一帧** |
| 平台 | `itest_posix_instance_channel`（新，POSIX 腿，**9 案**，批 6 由 8 扩） | 同键第二个认领得到 Existing 且首实例释放后名字可再认领、转发往返与两条请求串行完成、超长帧在上线前拒绝、陈旧 socket 文件被替换、非私有目录被拒、转发目标缺失报错、空键即拒、请求只保留首份回执且超时收尾；**真协议帧过通道字节全等**（协议帧必然含 NUL——reserved 字节与 `cwd_len` 高字节，帧长只能取自长度前缀而不能按 C 串，这条轴此前八案共同没有牙齿） |
| 界面腿 | 既有 `itest_workspace_layout` 补案（新增 1 案至 24 案） | 公开的「按焦点切分」入口按**模型焦点**落刀（新格插在当前焦点格的领地，与 `Ctrl+Shift+D` 同轴）、工厂缺失时只认领动作不改树 |
| 装配 | 无自动化 | `main.cpp` 不进 runner（F1 / 裁决 7.83）：冷启动动作、转发落点、批 6 的「先验证→开新窗→向 tracker 上报窗号」那一跳只有源码审查 + 真机走查。本棒拿到的最强证据是产品侧**退出码**——转交退 0 只在窗真开成并登记进容器之后才会发生（取不到落点即回一条 ASCII 诊断并退 2），它证到「那条腿没空转」，证不到「标签开在哪一扇」 |

**变异自证**（每则必须让指定套件转红；结果见 §12）：① 缺省动作改成 `split-pane`；② 重名档案取第一条；③ 请求长度上限放行多一个字节；④ POSIX 认领前不 `unlink` 陈旧 socket；⑤ `--cwd` + SSH 档案从拒绝改成静默忽略。

**批 6 另加五则**（`--new-window` 放开那一棒，读数与用例整形见 `WS03_MULTI_WINDOW.draft.md` §22⑸）：⑥ 旗标折成动作的条件改恒假 → 只红 `utest_cli_startup.new_window_flag_folds_into_the_third_action`；⑦ 删旗标侧的互斥判 → 只红同套件的 `new_window_conflicts_with_both_subcommands`（⑥⑦ 起初打同一案，为满足第 22 条「逐条转红」先把用例拆成两案再重跑）；⑧ 编码侧 action 上界写死旧值 → 只红 `utest_cli_handoff.new_window_action_crosses_the_wire_and_the_old_flag_still_reads`；⑨ 解码侧上界写死越界值 → 只红 `request_rejects_bad_magic_version_reserved_and_action`（⑧⑨ 一上一下，正是「界与表同源」的两半）；⑩ POSIX `write_frame` 改按 C 串取帧长 → 只红 `itest_posix_instance_channel.a_real_startup_frame_crosses_the_channel_unchanged`，其余八案照绿。

## 10 已知边界（不称「可用」）

1. **WS.02 欠账**：分屏共享一个会话，故 `split-pane --profile` / `--cwd` 拒绝；`split-pane` 本身只能切出同会话的第二格。多会话工厂落地后重开这两档。
2. **WS.03 已接上（批 6，2026-10-21）**：`--new-window` 在已有实例上**不再拒绝**，转交侧真开一扇新窗；冷启动那一侧它与缺省 `new-tab` 共用同一条腿（那一扇就是新窗），故「冷启动＋旗标」与「冷启动＋缺省」应当完全同形。仍欠的是真机读数（新窗开在哪、远程档案的询问框弹在哪一扇），见 §9 装配行与 `WS03_MULTI_WINDOW.draft.md` §9 第 5 条。
3. **Windows 腿未编译 / 未真机验证**：命名管道、`GetCommandLineW` 折 UTF-8、`FILE_FLAG_FIRST_PIPE_INSTANCE` 认领均按文档实现，标推断 / 未验证；本机只跑 POSIX 腿与无头用例。
4. **`main.cpp` 装配最后一跳无自动化证人**（裁决 7.83）；CLI 与通道本体有证人，但「参数真的变成那个标签」要靠真机走查。
5. **无 shell 补全 / 无多动作 / 无命令行窗口标题等未列参数**。
6. **首实例忙 / 卡住时**：转交 2 s 超时即报错退出 3，不回落第二实例——单实例是硬语义，宁可让用户重试。

## 11 落地切分（按 AGENTS §4.6 第 26 条）

1. **纯逻辑**：`cli::parse_startup_args` / `resolve_profile` / 请求回复编解码 + 两个 utest。→ `0ec5e99`
2. **平台**：`platform/instance_channel` 两腿 + `platform/process_args` 两腿 + POSIX itest；原 `ARCHITECTURE.md` §16 E 按本稿 §3 收口（现写入该文档 §3.8）。→ `eebb931`
3. **装配**：`main(argc, argv)`、认领 / 转发 / 冷启动动作、`WorkspaceView` 公开切分入口、`create_new_tab` 的显式目录覆盖 + 既有套件补案。→ `1855df1`
4. **文档回写**：SPEC 现状句 + 裁决 7.124、CHANGELOG v0.124、PLAN 的 INTEG.04 行、ARCHITECTURE §2.1／§2.2／§3.1／§3.8 与 §16 E 撤行、本稿状态头与回填。设计稿本体 → `63270a2`

## 12 结项读数与未验面（2026-10-21）

**读数**：构建通过、无警告；`ctest --preset linux` **98/98**（注册 98 含 3 条 `etest_`，非 etest **95** 项，9.42 s）；`borealis_test_runner` 全跑 **1202 案**全绿（21.0 s；v0.123 基线 92／1175，本棒 +3 套件／+27 案）。分套件：`utest_cli_startup` 12、`utest_cli_handoff` 6、`itest_posix_instance_channel` 8、`itest_workspace_layout` 24（含新增 1 案）。

**实跑退出码**（只走「在建窗之前就能返回」的那几档，无头环境不开窗口）：`--help`→0；未知选项→2 并打用法；`--profile nope`→2；子命令重复→2；`split-pane --profile nope`→2；相对 `--cwd ../rel` + 坏档案→2。

**五项变异结果**（脚本注入 → 只跑指定套件 → 备份 `cp` 复原并重跑全绿；五则只改纯逻辑与 POSIX 认领路径，不触达任何「真打开」腿，故在册事故纪律 F15／D11 的断桥要求本棒无需）：

| # | 变异 | 结果 |
|:--|:--|:--|
| ① | 缺省动作改成 `split-pane` | 红：`utest_cli_startup.default_invocation_is_new_tab_with_empty_fields` |
| ② | 重名档案取第一条 | **草稿形态不红**（把 `name_matches > 1` 的歧义分支改成返回那一条精确名命中——用例的歧义来自大小写折叠那一档，精确名分支打不到）→ 改注 `folded_matches > 1` 的同一分支后红：`duplicate_names_are_ambiguous_not_first_wins` |
| ③ | 请求长度上限放行多一个字节 | **草稿形态不红**（`total > kMaxHandoffRequestBytes` 加一枚字节：`--profile`／`--cwd` 各被 4096 钳住，总长到不了 16 KiB 外层判定）→ 改注单值上限 `value_fits` 后红：`utest_cli_handoff.request_encoding_enforces_value_and_value_cap_inclusive` |
| ④ | POSIX 认领前不 `unlink` 陈旧 socket | 红：`itest_posix_instance_channel.stale_socket_file_is_replaced` |
| ⑤ | `--cwd` + 非本地档案从拒绝改静默忽略 | 红：`utest_cli_startup.policy_rejects_split_with_profile_or_cwd_and_nonlocal_cwd` |

**实现期抓到的两条真实缺陷**（均由 `Borealis --cwd /definitely/not/here` 实跑抓到，非推演）：① `std::string::assign(u8string 迭代器对)` 在 `--cwd` 折绝对路径那一步算出离谱容量（约 1.8e19）并抛 `std::length_error` → `terminate`（`gdb -batch` 回溯定位），改判为跨容器的字节搬运一律用指针 + 长度，深层机理未查故注释只写实测形态；② 排空队列时把回复状态写死成成功，首实例会一边执行失败一边回 `ok`、转发端据此退 0，改为先算诊断串再落状态位。

**未验面（真机走查清单，做完才能称「可用」）**：冷启动三档（缺省 `new-tab`、`--profile` 命中档案、`--cwd` 覆盖目录）；转发两档（首实例在跑时 `new-tab --profile` 落在当前窗口新标签、`split-pane` 切当前焦点格）；`--new-window` 在已有实例上的拒绝文案；Windows 腿（命名管道认领、`GetCommandLineW` 折 UTF-8、CJK 目录名）；`main.cpp` 装配最后一跳无自动化证人（F1／裁决 7.83），以上四条只有源码审查与退出码可证。§10 的六条边界原样有效。

**批 6 补记（2026-10-21，多窗口本体落地后）**：上面「未验面」里那条「`--new-window` 在已有实例上的**拒绝文案**」自此作废——它不再被拒，而是由首实例开一扇新窗。该档的真机读数（新窗里的首标签吃什么料、远程档案的询问框弹在哪一扇、被拒请求是否留空窗、混版两档、冷启动是否与缺省同形）拆成七档，写在其棒台账 `WS03_MULTI_WINDOW.draft.md` §9 第 5 条；本稿 §12 的其余读数与退出码实测是批 4 结项时的真实观测，按只追加口径原样留档。同批另补五则变异（本稿 §9 的⑥–⑩）与三套件案数增量（`utest_cli_startup` 12→14、`utest_cli_handoff` 6→7、`itest_posix_instance_channel` 8→9），门读数为 ctest 99/99、runner 1252/1252。
