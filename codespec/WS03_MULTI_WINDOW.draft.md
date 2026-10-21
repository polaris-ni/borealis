# WS.03 多窗口 —— 设计稿（开工前，待评审）

需求：`SPEC.FEAT.WS.03` 独立多窗口（依赖 Aurora `Application::open_window`）；窗口布局分别记忆。与单实例参数转交的交互见裁决 **7.7**（`--new-window` 显式开新窗口）。
分期：`PLAN.md` 记 P2 / M4，现状只有一句「M4」（`SPECIFICATIONS.md` §7 无该条裁决，`ARCHITECTURE.md` §8.4 只写了「消费 `Application::open_window`」）。
本稿状态：**§4 的 D1–D14 每条都给了推荐档，未经人逐条裁决**（常存授权「遇到问题按推荐去做」下的自决收口，与 `INTERACT.05`（裁决 7.119）、`WS.08`（7.120）同形态）。§0 的 F1–F16 是本会话读源与读装配层的实测事实，标「未实测」者除外。

---

## 0 地基实测（F1–F16）

**框架面（Aurora 活动分支，读源）**

- **F1 多窗口是框架既有能力，不是缺口**：`Application::open_window(unique_ptr<Window>, Scene, WindowOptions) -> WindowId`（`include/aurora/app/application.h` §「多窗口」）；`window_host(WindowId)`、`windows()`、`window_count()`、`close_window(WindowId)`、`main_window()`、`set_exit_policy(ExitPolicy)` 全为公共 API。`Aurora` 自带 `utest_multi_window` / `utest_window_lifecycle` / `utest_window_shell` 与 `examples/demos/demo_multi_window.cpp`。
- **F2 逐窗的只有三件**：`Scene`、`FocusManager`、后端 `Window`/`Surface` 各一份；`Animator` / `Scheduler` / `ShortcutRegistry` / `CommandRegistry` 是**应用级共享**（`application.h` §58 注释与成员表 707-708 写死）。`app.scene()` / `app.focus()` / `app.window()` 的作用端恒是**主宿主**。
- **F3 焦点不跨窗**：`FocusManager` 逐宿主（`window_host.h` 注释「焦点序仅作用于本宿主」），模态只需 OS 层阻断 owner 输入，无需焦点陷阱。⇒ 本仓现在四处捕 `app.focus()`（`set_search_dependencies`、`DebugPanel`、`StartupNotice`、`bind_shortcuts` 链）在第二扇窗里指的是**主窗**的焦点序。
- **F4 框架没有「当前活动窗口」查询**：全仓 `include/` + `src/` 无 `active_window` / `focused_window` / `current_window` 任何一名；`Application::window_state()` 只聚合**主窗**的可见性（`application.cpp` 的 `set_state_sink` lambda 里 `if (raw == main_host_)`），非主窗的状态只落自己宿主且**公共面取不到**（`WindowHost` 只有 `set_state_sink` 写入端，无 getter）。⇒ 见 §3。
- **F5 快捷键命中与作用窗**：`Application` 给每个宿主装同一个前置拦截器 `shortcuts_.handle(key, raw->focus().focused() != nullptr)`（`application.cpp` 77-80）——**派发是按宿主进来的**（OS 只把键交给活动窗），但命中用的是应用级共享表，且 `ShortcutBinding::action` 是无参 `std::function<void()>`，**回调拿不到「哪一扇窗派发的」**。`set_key_pre_handler` 也是 Application 自己占的，覆盖即破坏快捷键链。
- **F6 几何持久化框架已备**：`WindowOptions::persist_id` 非空且 `Application::set_window_geometry_store(Preferences *)` 已指定时，**关窗自动保存、开窗自动恢复**，`set_window_geometry_store` 还会对调用前已登记的宿主补做一次恢复（主窗那条路径因此可用）。保存的是 origin + 物理像素 size + `WindowMode` + display_id，恢复前有「几何不可用（显示器拔掉／完全在屏外／尺寸非正）⇒ 回退默认并 WARN」的兜底。落盘时机归调用方（`Preferences::flush()`），框架不做隐式 I/O。
- **F7 `request_wake_all()` 是 private**（`application.h` 524，被内部 589 调用）⇒ 跨线程唤醒「全部窗」在公共面取不到，本仓只能自己持窗口 surface 清单或只叫醒任意一扇（见 F15）。
- **F8 `WindowRole{Main,Auxiliary,Transient}` + `ExitPolicy{LastWindowClosed,MainWindowClosed,ExplicitOnly}`**：`Auxiliary` 是与主窗平级的独立顶层窗，关它不影响别窗；缺省策略 `LastWindowClosed` 在多窗下即「关掉最后一扇才退出」。`Transient` 随 owner 连带关闭（帧末回收阶段执行）。

**装配面（`src/main.cpp`，3132 行，行号为本稿当日读数）**

- **F9 一扇窗的状态散在 `main()` 的栈上，没有「窗口」这个抽象**：`opts`/`create_native_window`/`window`/`surface` 598-607；`tab_strip` 622、`closed_tab_stack` 623、`next_tab_id` 627、`tab_workspaces` 652、`current_tab_id` 655、`tab_kind` 661、`broadcast_router` 666、`broadcast_banner` 681；`host`(OverlayHost) 1172、`root_column` 1176、`tab_bar` 1325、横幅 1327、当前工作区 1349、`chrome` 1357、`scene` 1359、`app{scene, window, opts}` 1362。⇒ 多窗需要把「622–681 的呈现半区 + 1172–1362」收成一个每窗一份的结构体，其余按 F10 分工留全局。
- **F10 标签号是进程唯一且只增不复用**（`next_tab_id` 627，取号点 976／1123／1436／1644／1751／2436），`TabStrip::add/insert_at` 只认领外部号（`ui/tab_strip.h`），`ClosedTabStack` 不分配号。⇒ 第二扇窗**不会撞号**，所以按 `TabId` 索引的那批表（`tab_workspaces`、`tab_kind`、`ssh_profile_by_tab`/`ssh_secret_by_tab`/`serial_by_tab`/`telnet_by_tab` 818-835）留在应用级是安全的；但 `current_tab_id` **只有一个**（655），`rebuild_root_children`（1147）只切一列，撤销重开（2416-2478）会把标签插进「当前」那扇窗。
- **F11 会话的唤醒腿捕的是唯一那只 surface**：`Session::set_frame_wake([&surface]{ surface.request_wake(); })` 在 900，`hooks.preview_wake` 同形（1460）。⇒ 第二扇窗的后台输出会只叫醒主窗；`TabWorkspace`（`map<PaneId, shared_ptr<Session>>`，裁决 7.125）里没有窗口归属可用。
- **F12 应用级命令的动作闭包硬绑主窗**：`search.open` 2351 捕 `&current_tab_id` 与 `&tab_workspaces` 且**硬取 `view_of(1)`**；`session.restart` 2548 → `restart_current_tab` 2490 读 `*current_tab_id`；`serial.set_baud` 2649 捕 `&serial_by_tab, &current_tab_id`；`workspace.toggle_fullscreen` 2401 用 `app.window()`（主窗）；`tab.undo_close` 2416 用进程级栈。全部 `scope = ShortcutScope::Global`（框架只有 `Global`/`Focus` 两档）。
- **F13 浮层与面板全挂唯一 `host`**：显式 `add_overlay` 点 1259（关闭确认）、2383（命令面板）、2569／2680（粘贴与日志对话框）；构造时吃 `*host` 的有设置页 1508、凭据询问 1858、侧栏 1981、隧道 2086、SFTP 2149、密钥 2324、调试 2833、启动提示 2983。凭据询问的挂起状态（`pending_profile`／`pending_restart`／`pending_tunnel`… 1791-1809）也是进程一份。⇒ 「一窗一浮层栈」是当前的隐含前提。
- **F14 广播组是应用级一份**（666），成员类型 `(TabId, PaneId)`、按会话身份去重（`include/borealis/ui/broadcast.h`），刷新链 693-744 遍历**全部** `tab_workspaces`；但横幅只挂在唯一 `root_column` 的 index 1（1327）。⇒ 多窗下扇出会跨窗成立，而「哪些成员在我这扇窗里」这一眼见事实无处显示。
- **F15 单实例转交的唤醒与落点**：`startup_wake` 是 `std::atomic<au::Surface *>`（517，置址 3105、清址 3119、transport 线程读 545/554），`app.run()` 的帧循环由 `Application` 统一驱动**所有**宿主（`application.h` 帧循环注释）⇒ 叫醒任意一扇 surface 就会跑到 `set_on_frame`，唤醒腿不需要「叫醒每一扇」；需要的是 `apply_startup_request`（2896）有一个明确的落点窗。`--new-window` 现在被拒：2898-2901，诊断 `--new-window needs SPEC.FEAT.WS.03, which is not implemented yet`，且只拒转交那一支（冷启动同标志无额外动作）。`cli` 的线格式是 `BORI | version u8 | action u8 | flags u8 | reserved u8 | …`（`include/borealis/cli/handoff.h` §头部注释），`StartupAction` 现只两值（`startup.h` 31-33），解码侧对未知 action **fail-closed 整帧拒**（`utest_cli_handoff` 在册）。
- **F16 窗口几何与 DPI 在本仓是零**：`src/main.cpp` 无 `set_size`/`set_position` 读写、无 `opts.persist_id`、无 `set_window_geometry_store` 调用；`include/borealis/config/settings.h` 的键表里**没有任何窗口几何键**；窗口尺寸只有 598-600 那一句硬编码 960×640。⇒ 「窗口布局分别记忆」是从零起，且**不该**进 `config::Settings`（见 D9）。
- **未实测项**（本稿不冒充读数）：本仓无头 itest 里同时开两扇 `au::Window{HeadlessSurface}` 的可行为——Aurora 侧 `utest_multi_window` 证明三宿主登记与逐窗 `scene/focus` 隔离成立，但本仓 harness（`itest_workspace_layout.cpp` 542 那一对 `make_window`）目前只造一扇，批 1 须先把它跑绿再谈其余判据。

---

## 1 本条要兑现的四件事与需求没写的部分

需求原文两句半：①独立多窗口；②窗口布局分别记忆；③与单实例转交的交互按裁决 7.7（`--new-window` 显式开新窗口）。需求**没写**的、必须此刻定死的是：新窗的初始内容、命令与快捷键落在哪一扇、广播组跨不跨窗、面板与浮层单例还是每窗一份、末位标签关闭要不要关窗、几何按什么键分别记忆、关窗要不要确认「还有会话在跑」，以及 WS.10 的撤销栈跨不跨窗。这七处即 D1–D14 的来由。

## 2 拆分边界（推荐档）

| 归属 | 内容 | 依据 |
|:--|:--|:--|
| **每窗一份** | `Window`/`Surface`、`Scene`、`OverlayHost`、`root_column`、`TabBarWidget`、广播横幅、`TabStrip`、`ClosedTabStack`、`current_tab_id`、`WindowId` | F2／F3／F9／F10（`current_tab_id` 与撤销栈是「这扇窗的呈现与选择」，不是全局事实） |
| **应用级一份** | `Settings`/`config::Store`、`ClipboardGate`、`TrustedLinkHosts`、`BroadcastRouter`、`CommandRegistry`/`ShortcutRegistry`、四张按 `TabId` 索引的连接表、`tab_workspaces`、`tab_kind`、`next_tab_id`、凭据询问链、密钥/SFTP/隧道/设置四面板、单实例 listener | F10（号全局唯一 ⇒ 按号索引的表留全局最省）、F12／F13 |
| **新增** | `struct WorkspaceWindow`（每窗一份的那一列）+ `std::vector<std::unique_ptr<WorkspaceWindow>>` + 一份「命令作用窗」解析器 | F9 那段栈上代码需要一个身份 |

`TabWorkspace` 需补一枚 `WindowId owner`（F11 的唤醒腿按它取本窗 surface；号由建标签那一跳传入）。

## 3 一条框架侧缺口（按 AGENTS §5.1 分流，但本棒不等不绕）

**G42（候选，待登记）**：应用级共享 `ShortcutRegistry` 的 `ShortcutBinding::action` 是无参闭包，`Application` 也没有「当前活动窗口」查询（F4／F5），而 `window_state()` 只聚合主窗、`request_wake_all()` 是 private（F7）。多窗应用因此**无法从公共面得知一条全局快捷键该落在哪一扇窗**。两条可接受形态：A) `ShortcutBinding::action` 改带 `WindowId`（或 `ShortcutContext{WindowId}`）；B) `Application::active_window()`（按最近一次宿主侧键盘/指针派发判定，无活动窗时回 `main_window()`）。
**本棒不阻塞**：§4 的 D7 用本仓自持的「最近派发窗」记录器兑现同一事实，属可组合的应用侧能力（我们的视口控件本来就吃到那一笔按键）；待任一条回货，替换点只在那一枚解析器。按 §5.1 的「先在 Aurora 补公共 API」在这里让位给「本仓可组合」的判定，理由与代价见 D7③。

## 4 待人裁决（每条已给推荐①）

- **D1 一扇新窗的初始内容**：① 推荐＝**一个新缺省本地标签**，目录取**配置值**、**不**继承发起窗的 OSC 7（新窗没有「当前标签」，`INTEG.02` 的判据「当前标签是活着的本地终端」在结构上不成立）。备选＝继承发起窗焦点格（代价：两扇同名标签同一目录，与「新窗是独立工作现场」的直觉相反）。代价：用户想要「把这格挪到新窗」时只能手工再开。
- **D2 新窗入口**：① 推荐＝命令 `workspace.new_window` + `Ctrl+Shift+N`（分屏保留键表的**第十五条**，`utest_workspace_keys` 的认领集随之 14→15 条）+ 命令面板一条 + 标签条不加「＋新窗」按钮（「＋」已归新标签）。备选＝只做命令不做键（与 WT 习惯不符）。
- **D3 关窗的判据与出口**：① 推荐＝**不做**「末位标签关闭 ⇒ 关窗」（`WS.01` 的末位标签本来就没有关闭按钮，`ARCHITECTURE.md` §8.1 把那条路径挂在本条名下，但按现状它不需要存在）；关窗只有 OS 关闭钮与 `workspace.close_window`（**不给缺省键**，`Ctrl+Shift+W` 留给「关标签」的既有直觉）。代价：架构 §8.1 那句话要就地改口（见 §10 文档回写）。
- **D4 关窗要不要确认「还有会话在跑」**：① 推荐＝**本棒不做**——框架没有「关闭请求可否决」的公共入口（只有 `close_window` 立即 `Window::close()`，`F1`），做 veto 需先有 Aurora 侧接缝。关窗即随窗析构会话，与关标签的确认判据不同档，登记为边界。备选＝向 Aurora 提关闭否决任务（跨仓，另棒）。代价：用户点 X 时正在跑的进程被静默带走 ⇒ 这条必须写进 §10 的「不称可用」账。
- **D5 广播组跨不跨窗**：① 推荐＝**跨窗成立**（组本就 app 级、成员键 `(TabId, PaneId)` 全局唯一，F10/F14），横幅改**每窗一枚**且只列本窗成员，计数按全组去重会话数（与 `WS.09` 现口径同一条算式）。备选＝组按窗隔离（代价：切窗丢组，违背「批量操作」的初衷）。
- **D6 面板与浮层的单例性**：① 推荐＝设置页／密钥／SFTP／隧道／侧栏／命令面板保持 **app 级单实例**，浮层挂到**发起命令的那一扇窗**的 `OverlayHost`（关闭即摘回），同一面板换窗打开时先从原窗摘掉；凭据询问链同一条规则。备选＝每窗各一套面板（代价：`pending_*` 挂起态、worker 队列、密钥扫盘缓存全部要复制，本棒做不完）。代价：两扇窗不能同时各开一块 SFTP 面板；设置页在两窗间是「搬过去」而不是「各有一份」。
- **D7 命令与快捷键落哪一扇窗**：① 推荐＝本仓自持 `ui::ActiveWindowTracker`（纯逻辑件：每窗在「本窗的视口/标签条/横幅收到一次按键或一次指针 Press」时上报自己的 `WindowId`，tracker 记最近一次；查不到回主窗）。备选＝向 Aurora 提 G42（§3），等回货再开工。代价：OS 层「哪扇在活动」与本仓「最近收到派发的窗」在无头与失焦场景下可能不同（真机走查项），且回货后要替换一处；换来的是**本棒不跨仓、不阻塞**。
- **D8 窗口标题**：① 推荐＝**每窗各写各的**（本窗选中格的 `resolve_tab_name`，沿用 `WS.06` 的逐帧差分写法），标题内容不含窗序号。备选＝加「窗口 N」后缀（代价：OS 任务栏噪音，且单窗常态下多一枚后缀）。
- **D9 几何分别记忆的键与载体**：① 推荐＝用框架 `persist_id` + `preferences::Preferences`（F6），文件落配置目录另起一枚（`windows.json`），键＝`main`（主窗）与 `window-2`、`window-3`…（创建序）；`flush()` 取在「任一窗关闭后」与「退出前」两处。明确**不**进 `config::Settings` schema（代价：设置核对表 69 键不动、`PREF.07` 的导出件不含几何——几何是本机屏幕相关值，跨机导出即坏值，这条是特性不是漏项）。备选＝自写 `Settings.windows[]`（违背 AGENTS §5.2「原语须以公共 API 存在，本仓只消费」）。
- **D10 `--new-window` 放开**：① 推荐＝`StartupAction` 加 `NewWindow`（线格式 magic/version/action 字段都不动，action 多一个值），转交侧收到即开一扇新窗并按 D1 落一个缺省本地标签，**`--profile`/`--cwd` 与 `NewWindow` 相容**（新窗的首标签吃该档案/目录）；`--help` 文案补一条。旧实例若收到它不认识的 action 值 ⇒ 整帧 fail-closed 拒（F15），诊断照旧退 2。代价：混版（旧实例 + 新 CLI）时该旗标不生效。
- **D11 新窗的退出与「最后一扇」**：① 推荐＝保持框架缺省 `LastWindowClosed`（F8）——关掉最后一扇窗即退出，退出序沿用现主窗那一条（先 `startup_wake.store(nullptr)` → `instance_listener.reset()` join 线程 → 遍历**全部窗全部格**的会话 `close()`），`ExplicitOnly` 与 `MainWindowClosed` 都否。代价：主窗先关时「主窗」身份随 F8 的帧末回收保留最后宿主，`app.focus()` 语义仍指主宿主 ⇒ 本仓不得再依赖 `app.focus()`（F3 那一处改按窗取）。
- **D12 撤销重开（`WS.10`）跨不跨窗**：① 推荐＝`ClosedTabStack` 改**每窗一份**（撤销是把标签放回「我关它的那扇窗」），栈上限与顺序语义不变（`utest_closed_tab_stack` 零改动）。代价：跨窗撤销不再可能（本条需求没要求），`tab.undo_close` 的动作按 D7 解析目标窗。
- **D13 逐窗尺寸下发与去抖**：① 推荐＝不改一件现有代码——去抖表与 `Session::resize` 的归属在 `WorkspaceView`（每窗各一份），`dispatch_grid` 按格取会话（7.125）；新窗的 surface 由 `WindowOptions::size` 给初值 960×640，之后走既有布局派生。代价：D9 的恢复几何落在**开窗那一刻**，首帧行列数随恢复后的尺寸算（须验一条「恢复后首帧只下发一次」）。
- **D14 本条的验收面**：① 推荐＝批 1 起每批带无头用例（两扇 `HeadlessSurface` 窗），真机走查列 §9 清单、未走完不称「可用」（§4.4 第 33 条）；`main.cpp` 不进 runner 这条物理事实不变（裁决 7.83），故「两扇窗真能各开各的」那一档只有源码审查 + 走查。

## 5 数据流与不变量

1. **建窗**＝`create_native_window(opts)` → 装 `WorkspaceWindow`（`TabStrip`/`root_column`/横幅/`OverlayHost`/`chrome`/`Scene`）→ `app.open_window(...)` 取回 `WindowId` → 回填 `owner` → 建首标签（`next_tab_id` 全局取号）→ `present_root` 只在**该 Scene** 上做一次。
2. **泵**＝`set_on_frame` 一轮遍历**全部窗**：逐窗排空启动请求队列（一次，全局）→ 逐窗 `rebuild_root_children`（脏时）→ 逐窗取本窗焦点格的 OSC 标题写本窗标签名与标题（7.125 的「焦点格独占」在窗内成立，跨窗互不干扰）→ 逐窗 `set_exited` 与横幅刷新。
3. **唤醒**＝每条会话的 `frame_wake` 捕**所属窗**的 surface（F11 的修法）；转交线程仍只叫醒 `startup_wake` 那一枚（F15，叫醒任意一扇即跑到帧循环）。
4. **拆**＝关窗只拆呈现（`WorkspaceWindow`），标签与其会话仍由 `teardown_pane`/`perform_close` 那两笔账撤；窗析构序必须**先摘浮层再散 host**（与 `G34` 那棒同源的教训），且 `tab_workspaces` 里该窗名下的标签要逐个走与关标签同一条撤账链（广播成员、四张连接表、`Session::close()`），不允许「窗散了而表里还挂着标签」。

## 6 分批（每批一个分层提交）

1. **批 1（拆分，行为不变）**：`WorkspaceWindow` 结构 + `open_workspace_window()` + `std::vector` 持有；主窗走同一条构造路径；itest harness 先跑通「两扇无头窗各渲染各自 `TabBar`」。生产仍只开一扇 ⇒ 现有全部用例零改动即须绿。
2. **批 2（逐窗接线）**：会话 `frame_wake` 按 owner 取 surface、`set_on_frame` 逐窗泵、标题与标签名逐窗、四处 `app.focus()` 改按窗取、`tab_workspaces` 补 `owner`。
3. **批 3（新窗入口与命令落点）**：`ui::ActiveWindowTracker` 纯逻辑件 + `utest`；命令 `workspace.new_window` + `Ctrl+Shift+N`（第十五条保留键）+ 面板一条；`search.open`／`session.restart`／`serial.set_baud`／`workspace.toggle_fullscreen`／`tab.undo_close` 五类闭包按 tracker 解析目标窗。
4. **批 4（拆窗账目）**：关窗撤该窗名下全部标签（同 `perform_close` 一条链）、每窗横幅与每窗 `ClosedTabStack`、`ExitPolicy` 缺省确认与退出序遍历全部窗、面板「搬窗」而非「各一份」（D6）。
5. **批 5（几何分别记忆）**：`persist_id` 分配 + `preferences::Preferences` 落配置目录 + 两处 `flush()` + 「恢复后首帧只下发一次」用例。
6. **批 6（CLI）**：`StartupAction::NewWindow` + 编码/解码 + `parse_startup_args` 旗标语义 + `--help` 文案 + 撤 2898-2901 那条拒绝 + `utest_cli_startup`／`utest_cli_handoff`／`itest_posix_instance_channel` 扩案。
7. **批 7（收口）**：`SPECIFICATIONS.md` 的 WS.03 现状句与裁决、`CHANGELOG.md` 新版本、`PLAN.md` 的 WS.03／INTEG.04／WS.10 行、`ARCHITECTURE.md` §8.1 那句「末位标签关闭 → 关窗口属 WS.03」改口与 §8.4 多窗口段、本稿追加「§8 结项读数」。

## 7 判据与变异计划（结项三件齐：ctest 全绿 + runner 案数增量 + 变异证人逐条转红）

- `utest_active_window_tracker`（新）：多窗上报次序、失配回主窗、窗关闭后该号不再被返回、并发上报只留最后一次。
- `itest_workspace_window`（新）：两扇窗的标签互不串（`current_tab_id` 各一份）、横幅只列本窗成员、撤销重开回到关它的那扇窗、逐窗标题差分写回、关一扇不退出而关最后一扇走同一条退出序、窗拆后 `tab_workspaces`/四张连接表/广播成员三处账目为空。
- `utest_workspace_keys`：认领集 14→15 条。
- `utest_cli_startup`／`utest_cli_handoff`：`--new-window` 的旗标解析与 `NewWindow` 的编解码往返、未知 action 仍整帧拒。
- 变异候选（每条一个判据，注入后按 §4.4 第 22 条点名转红）：新窗继承发起窗目录（应为配置目录）、`frame_wake` 全部捕主窗 surface、`current_tab_id` 回全局一份、横幅恒列全组成员、`ClosedTabStack` 回全局、关窗不撤标签账、`persist_id` 恒空、`--new-window` 仍走拒绝支、tracker 无记录时不回主窗。

## 8 与既有裁决的相接处

`7.7`（`--new-window` 显式开新窗）＝D10；`7.43`（末位标签不归 `TabStrip` 关）＝D3 的依据；`7.47⑧`（关闭确认判据）＝D4 的对照；`7.82`（装配层「标签 → pane 树 → 每 pane 会话与视图」）＝§2 拆分边界的起点；`7.121`（广播 app 级单组）＝D5；`7.124`/`7.125`（CLI 与多会话）＝F15 与 D10；`7.86`/`7.100`（重启按 `(TabId, PaneId)`）＝命令落点按窗解析后的同一条接缝。

## 9 真机走查清单（未走完不得称「可用」）

1. 两扇窗各自最大化／还原后网格是否铺满（DPI 那条老账按窗各验一次，`G17`/`G21` 的历史读数只覆盖单窗）。
2. 键入落活动窗：A 窗聚焦后 `Ctrl+Shift+N`、`Ctrl+Shift+D`、`Ctrl+F`、`Ctrl+Shift+R` 是否各打各的（tracker 的 OS 活动态偏差档）。
3. 关一扇窗后另一扇不受扰动、广播组与横幅跟上；关最后一扇退出时无残留进程（两腿）。
4. 几何记忆：挪位、改大小、最大化三态各重启一次，显示器拔掉时的回退档。
5. `--new-window` 的转交与 `--profile`/`--cwd` 组合在真机上的落点。
6. 面板在两窗间「搬过去」的观感（D6 代价档）与凭据询问弹框的所属窗。
7. Windows 主通道（MSVC + Win32 后端）的多窗与几何恢复；本沙箱只跑 POSIX 无头腿。

## 10 边界（登记在册，不在本棒解决）

`main.cpp` 不进 runner ⇒ 批 2/批 4 的装配腿无自动化证人（裁决 7.83）；D4 的关闭否决缺框架公共入口（若提任务书按 §5.1 走 Aurora 侧）；G42（§3）以本仓 tracker 代偿，回货后替换一处；D6 的面板单例意味着「两窗不能同时各开一块 SFTP」；几何不进设置导出件（D9 是特性）；混版 CLI 的 `--new-window` 不生效（D10 代价）；WS.11 会话恢复**不**在本条做（多窗之后那一件的形状是「每窗的标签与分屏快照」，另棒裁决）。
