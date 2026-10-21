# CLI 启动参数与单实例转交 · 设计稿

> 需求：`SPECIFICATIONS.md` **`SPEC.FEAT.INTEG.04`**；落点裁决 **7.7**。
> 本稿只定形态与边界，不写实现叙述。状态：**设计收口、开工**（2026-10-21）。
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

- **一个调用只表达一个动作**：`new-tab` 与 `split-pane` 互斥；重复出现即用法错误。缺省动作＝`new-tab`。
- `--profile` / `--cwd` 各最多一次，值不得为空；未知选项、位置参数、缺失值一律**退出码 2**。
- 选项顺序无关；子命令可出现在选项前或后。
- `--help` 在**本地**打印用法并退出 0，不占用 / 不查询单实例通道。
- 所有字面量 ASCII；用法文本与错误文本也是 ASCII（诊断不属 CJK 例外，AGENTS §4.3 第 14 条）。
- 语法层不碰文件系统、不解析档案；它只产出一张结构化请求。

**为何不做多动作**：需求只列子命令，未要求一次开多标签；多动作会把「一次请求对应一个回复」的协议与失败原子性一起复杂化。留作观察项，不预埋。

## 3 单实例通道（D2；给 §16 E 的推荐）

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

- **已有实例**：拒绝并回「`SPEC.FEAT.WS.03` 未落地」的诊断（退出码 2）。单实例进程模型（裁决 7.8 / 术语表）下不能靠再起一个进程冒充新窗口。
- **冷启动**：本就是新窗口，接受且不产生额外动作；不把它当错误。
- 这是需求 7.7 的**已知缺口**；WS.03 落地后在此接入 `Application::open_window` 与第二份 scene。

### 5.4 冷启动与转交的先后

- 冷启动请求在**窗口与接线全部就绪之后、`app.run()` 之前**执行一次；首帧直接画出目标标签，不出现「先空一帧再补标签」。
- 转交请求在 `set_on_frame` 开头排空；执行后要求重排 / 重绘，并在回复里回告成功或 ASCII 诊断。
- 冷启动的 `--profile` 在**建窗前**先做一次可解析性校验（档案存在、类型与 `--cwd` / `split-pane` 相容）；失败即退出 2，不建窗、不启动 shell。
- 冷启动若因运行期原因动作失败（如切分被几何约束拒绝），记录诊断并保留已建的首标签，继续运行；转交请求则回错误。

## 6 协议载荷（D8）

请求载荷（纯逻辑件 `cli` 负责编解码，平台通道只见不透明帧）：

```
offset 0   "BORI"            4 bytes
offset 4   version = 1       u8
offset 5   action            u8   0=new-tab  1=split-pane
offset 6   flags             u8   bit0=new-window
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
- 没有指针 / 句柄 / 明文凭据进入协议；只有动作、档案名、绝对目录、窗口标志。

## 7 退出码（D9）

| 码 | 含义 |
|:--|:--|
| 0 | 成功（冷启动继续运行；转交已被首实例接受并执行） |
| 1 | 建窗 / 内部失败 |
| 2 | 用法错误、档案不可解析、动作被拒（含 WS.02 / WS.03 边界） |
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
| 纯逻辑 | `utest_cli_startup`（新） | 缺省动作、两个子命令互斥、选项顺序、重复 / 缺值 / 未知 / 位置参数、`--help`、ASCII 用法；档案解析的 id / 名 / 大小写 / 重名 / 缺失；`--cwd` + 非本地档案的拒绝策略 |
| 纯逻辑 | `utest_cli_handoff`（新） | 请求与回复往返（含 UTF-8 目录 / 档案名）、magic / version / 长度 / 尾随字节 / 超限拒绝 |
| 平台 | `itest_instance_channel`（新，POSIX） | 首实例认领、同键第二个监听得到 Existing、请求 / 回复往返、超长帧拒绝、监听析构后同键可再认领（陈旧 socket 清理）、并发两条请求串行完成、队列满拒绝 |
| 界面腿 | 既有 `itest_workspace_*` 补案 | 公开的「按焦点切分」入口与既有按键切分同轴、单 pane 失败不改树 |
| 装配 | 无自动化 | `main.cpp` 不进 runner（F1 / 裁决 7.83）：冷启动动作、转发落点、`--new-window` 拒绝只有源码审查 + 真机走查 |

**变异自证**（每则必须让指定套件转红）：① 缺省动作改成 `split-pane`；② 重名档案取第一条；③ 请求长度上限放行多一个字节；④ POSIX 认领前不 `unlink` 陈旧 socket；⑤ `--cwd` + SSH 档案从拒绝改成静默忽略。

## 10 已知边界（不称「可用」）

1. **WS.02 欠账**：分屏共享一个会话，故 `split-pane --profile` / `--cwd` 拒绝；`split-pane` 本身只能切出同会话的第二格。多会话工厂落地后重开这两档。
2. **WS.03 欠账**：`--new-window` 在已有实例上必然拒绝；冷启动无动作。
3. **Windows 腿未编译 / 未真机验证**：命名管道、`GetCommandLineW` 折 UTF-8、`FILE_FLAG_FIRST_PIPE_INSTANCE` 认领均按文档实现，标推断 / 未验证；本机只跑 POSIX 腿与无头用例。
4. **`main.cpp` 装配最后一跳无自动化证人**（裁决 7.83）；CLI 与通道本体有证人，但「参数真的变成那个标签」要靠真机走查。
5. **无 shell 补全 / 无多动作 / 无命令行窗口标题等未列参数**。
6. **首实例忙 / 卡住时**：转交 2 s 超时即报错退出 3，不回落第二实例——单实例是硬语义，宁可让用户重试。

## 11 落地切分（按 AGENTS §4.6 第 26 条）

1. **纯逻辑**：`cli::parse_startup_args` / `resolve_profile` / 请求回复编解码 + 两个 utest。
2. **平台**：`platform/instance_channel` 两腿 + `platform/process_args` 两腿 + POSIX itest；ARCHITECTURE §16 E 按本稿 §3 收口。
3. **装配**：`main(argc, argv)`、认领 / 转发 / 冷启动动作、`WorkspaceView` 公开切分入口、`create_new_tab` 的显式目录覆盖 + 既有套件补案。
4. **文档回写**：SPEC 现状句 + 裁决 7.124、CHANGELOG v0.124、PLAN 的 INTEG.04 行、ARCHITECTURE §3.1 / §16 E、本稿状态头与回填。

## 12 待回填

- 结项读数（CTest / runner 案数）。
- 五项变异结果。
- 真机走查：冷启动三档、转发落点、`--new-window` 拒绝、Windows 腿。
