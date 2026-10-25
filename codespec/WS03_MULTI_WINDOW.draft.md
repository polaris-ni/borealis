# WS.03 多窗口 —— 设计稿（批 1–批 7 已全落，2026-10-21 收口）

需求：`SPEC.FEAT.WS.03` 独立多窗口（依赖 Aurora `Application::open_window`）；窗口布局分别记忆。与单实例参数转交的交互见裁决 **7.7**（`--new-window` 显式开新窗口）。
分期：`PLAN.md` 记 P2 / M4，现状只有一句「M4」（`SPECIFICATIONS.md` §7 无该条裁决，`ARCHITECTURE.md` §8.4 只写了「消费 `Application::open_window`」）——**该句是开工前的现状盘点，三处均已由批 7 回写**（裁决 7.126、`PLAN.md` 的 WS.03 行、`ARCHITECTURE.md` §8.4，见 §23⑴）。
本稿状态：**§4 的 D1–D14 每条都给了推荐档，未经人逐条裁决**（常存授权「遇到问题按推荐去做」下的自决收口，与 `INTERACT.05`（裁决 7.119）、`WS.08`（7.120）同形态）。**批 1–批 7 已于 2026-10-21 全部落地，裁决 **7.126** 已入 `SPECIFICATIONS.md` §7、`CHANGELOG.md` v0.126 已记，各批台账与结项清单见 §11–§23。** 落地后与推荐档的口径差逐条就地回写不删原文：D9／D10／D11／D13 随批 5／批 6 落地（差处分别见 §21、§22 与 D10 那一行的四处），D3 的「另一条关窗入口」与那句 `Ctrl+Shift+W` 前提在批 7 读到不符（见 D3 的落地状态与 §23⑶），D1／D2／D4／D5／D6／D7／D8／D12／D14 按推荐档形态落。§0 的 F1–F16 是本会话读源与读装配层的实测事实，标「未实测」者除外；F17–F24 是批 4 开工前补读 Aurora 活动分支的实测事实，F25 是批 4-B 用无头帧循环驱动出来的读数（含两处只取框架自述、不列作实测的半区，见该条）；F26 是批 5 开工前读几何持久化那条链的实测事实；F27 是批 6 开工前读「运行中开的那一扇窗与几何自动链」的实测事实。

---

## 0 地基实测（F1–F27）

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
- ~~**未实测项**（本稿不冒充读数）：本仓无头 itest 里同时开两扇 `au::Window{HeadlessSurface}` 的可行为~~ **已闭合（批 1-A，2026-10-21）**：`tests/integration/itest_workspace_window.cpp` 五例全绿，台账与本批变异读数见 §11。余下判据仍以该 harness 为前提，但「两扇无头窗各渲染各自的 `TabBar`」不再是没有读数的假设。

**批 4 开工实测（2026-10-21 补，读的是 Aurora 活动分支源码，路径按其仓内记法）**

- **F17 关闭链路的形状与时序**：OS 事件 → `Surface::should_close()`（虚函数，`window/surface.h`）→ `Window::should_close()` → `WindowHost::should_close()`（`app/window_host.h`，`return close_requested_ || window_->should_close()`）→ `Application::reap_closed()`。`Application::step_frame` 的顺序是 `on_frame_ → render_all → reap_closed` ⇒ **`set_on_frame` 里轮询 `should_close()` 是唯一「销毁前」可观测点**（本批撤账入口**不**据此选它，理由与读数见 F25 与 §6 批 4-B 的改口）。
- **F18 没有否决机制**：全仓无 `close_requested` 回调、无返回 bool 的拦截点、无可撤销关闭的 API（框架文档 `codespec/specification/06-app-platform.md` 亦无该提案）。⇒ **D4 的「本棒不做关窗确认」由推测升级为实测**；真要做 veto 只能自绘标题栏（CSD）或在 `set_on_frame` 里抢在帧末之前弹框——而抢了之后**关不下来**，只会变成「弹框还开着、窗已经没了」。跨仓接缝属 Aurora 的账，登记为候选缺口。
- **F19 `set_on_window_closed` 的两处限制（批 4-B 实测后重新分档）**：① 触发点在宿主 `teardown()` + `hosts_.erase()` **之后**，此刻**宿主自己那份**`Scene`／`FocusManager`／控件树已散——这一条对**本层**账目不构成障碍：F22 已实测「应用侧另存的控件 `shared_ptr` 在宿主销毁后仍存活」，且此刻那一扇再不会被渲染，撤账落在这里反而比落在 `on_frame` 干净（见 F25）；真正留下的代价是②：`reap_closed` **保留最后一个宿主**，于是「关掉唯一那扇窗」时该回调**根本不触发**，那一扇的账目须由退出序收尾。⇒ 判活也不许只判 `window_host(id) != nullptr`，要读 `should_close()`。
- **F20 `WindowId` 单调递增、永不复用**（`register_host` 里 `next_id_++`），宿主注销后 `window_host(id)` 回空。⇒ `tracker.forget(id)` 之后该号不可能被下一扇窗撞上（与 `next_tab_id`、`PaneId` 同一条理由）。
- **F21 `ExitPolicy` 缺省 `LastWindowClosed`，但全为无头宿主时永不驱动退出**（`should_exit()` 里 `any_window` 为假 ⇒ 不退出）。⇒ 无头 itest 可以「关掉一扇而进程不退」，这正是本批机制证人的地基；反过来，退出序那条判据在无头面**不可证**，只能走查。
- **F22 宿主按值拥有 `Scene` 与 `FocusManager`，`teardown()` 先 `focus_.clear()`**；应用侧另存的控件 `shared_ptr` 在宿主销毁后仍存活（`Node` 是共享所有权包装），框架**不提供销毁前通知**。另实测：`Dialog`/`Popup` 无析构触发点 ⇒ **关窗路径上 `set_on_close` 不会被调用**。本仓四件用了它：`DebugPanel`、`StartupNotice`（`scope_pushed_ = false`）、`SearchOverlay`（`on_popup_closed()`）、`SettingsPanel` 只是注释——四处都随本控件一起析构，故「没回调」不产生悬垂，只产生「内部状态在销毁链上一度不一致」，登记为观察项（若日后有件靠它做外部副作用，那条会静默丢失）。
- **F23 遍历入口只有 `windows()`**（回登记序的 `std::vector<WindowHost *>`）＋ `window_host(id)` ＋ `window_count()` ＋ `main_window()`；**无** `window_ids()` / `for_each_window_host`。
- **F24 无头后端可模拟「用户点了关闭」**：`HeadlessSurface::set_should_close(bool)` 是框架自带的测试 seam（`window/surface.h` 注释即「多窗口测试需驱动『某一窗口被关闭』」），框架侧 `utest_multi_window.closing_one_window_keeps_others_running` / `close_window_requests_reap_at_frame_end` 与 `utest_window_lifecycle` 全用它。⇒ 本批的窗侧判据可在 `itest_workspace_window` 里真驱动关闭。
- **F25 批 4-B 的驱动读数**（`itest_workspace_window` 四案，2026-10-21，都是本通道实测而不是读源）：
  - 被请求关闭的那一扇在**第一帧的 `on_frame` 时刻仍登记在宿主表里**，其后各帧取不到；`on_window_closed` 的账目在「取不到」的那些帧里**已经写上** ⇒ 回调排在下一帧 `on_frame` 之前。同帧内 `on_frame → render_all → reap_closed` 的先后取框架自述（`application.h` 里 `run()` 的八步注释），本棒不改 Aurora，不做框架侧注入，故这一半不列作实测。
  - `run()` 的**启动**判据是「至少一扇有 `Window` 且未请求关闭」：把所有窗的关闭请求都排在进循环之前 ⇒ `run()` 一帧不排、直接返回并 WARN。这是「没启动」而不是「启动了不退出」，两档不能混用（本案第一版栽在这里）。
  - 保留档的精确形状：`closing.size() == hosts_.size()` 时框架只从待收集合里剔掉**主宿主那一枚**，其余照旧回收并回调 ⇒ 两扇全关时非主窗那一扇**仍会**走一次回收，只有被保留的那一扇不报。
  - 主窗被收走时 `main_host_` 在帧末交给 `hosts_.front()`（登记序里还活着的第一扇），应用级 `scene()`／`focus()`／`window()` 整组跟着易主 ⇒ 本层兜底落点必须由装配层换（`set_fallback`），否则 `forget` 之后恒取不到落点。
  - **帧预算的读法只有一处**：`WindowOptions::max_frames` 被 `run()` 读的是**应用级那一份**（构造 `Application` 时传入的 options），逐窗各自的那份不参与预算。`power_saving=false` 是必须项——无头后端没有 OS 等待通道，省电档下 `wait_once` 会取到「无限等待」把 `run()` 挂死（Aurora 侧 `utest_multi_window` 同档处置）。
  - **空闲帧不出帧**：`HeadlessSurface::frame_count()` 只在真 `present()` 时自增，脏区决策会把没有脏的宿主整段跳过 ⇒ 「另一扇仍在渲染集合里」这句不主动标脏就是恒真空转（M4 变异证到的正是这一档）。

- **F26 批 5 开工实测（几何分别记忆的框架面，2026-10-21，读 Aurora 活动分支源码）**：
  - **两处入口、一处保存、一枚只写一次的闸**：恢复走 `Application::register_host`（每扇新窗）与 `set_window_geometry_store`（对**已登记**宿主补做一次，主窗那条构造路径靠的就是这一跳）；保存走 `reap_closed` 的**收集步骤**，且对「保留最后一个宿主」那一档同样执行（`closing` 集合里的每一枚都存，只是不销毁），`WindowHost::geometry_saved()` 保证同一扇只写一次。⇒ 本仓不需要自己抓「关窗前」的几何，两处 `flush()` 取的是**已写进内存**的那份。
  - **自动链的键作用域是 `Preferences` 顶层，不是 `windows` 分组**：`restore_geometry_for` 调的是 `load_window_geometry(*geometry_store_, persist_id)` 那一枚 **`Preferences&` 重载**（键即 `persist_id` 本身）。框架文档里另有一句「窗口组用 `prefs.group("windows")` 分组建键」，那说的是调用方**自己**直接调 save/load 的形态；`set_window_geometry_store` 这条自动链不查分组。⇒ 本棒若把几何写进 `group("windows")`，落盘看得见、恢复取不到——这条错位没有别的证人，必须钉在用例里（§7 批 5 的第一案）。
  - **恢复只落 `size` 与 `position`**：`apply_host_geometry` 实测把物理像素按 `scale_factor()` 折回逻辑 dp 后 `set_size` / `set_position`，**不**调任何几何态入口。`WindowGeometry::mode` 因此是「存而不取」——`capture_host_geometry` 写它、`window_geometry_from_json` 读它，恢复侧无消费点。⇒ 需求那句「窗口布局分别记忆」里**最大化态重启回来**这一档，框架目前给不了；本仓不补偿（AGENTS.md §5.1「不长期持有框架分叉」＋§4.1 第 3 条「不加需求外行为」），落进 §9 第 4 条的真机读数与 §10 的 Aurora 侧待议。
  - **可用性判据在 POSIX 腿上是张合成屏**：`load_window_geometry` 用 `is_window_geometry_usable(g)`，它取 `app::list_displays()`；非 Windows 分支（`display_win32.cpp` 的 `#else` 腿）恒返回一张 `1920×1080 @ (0,0)` 的单显。⇒ 「显示器拔掉／完全屏外 ⇒ 回退默认并 WARN」这一档在无头与本机 POSIX 面上**只能靠手写越界几何造**，真实拔屏只在 Windows 主通道可取（§9 第 4 条）。
  - **Linux 三后端的缩放不自动跟**：Aurora 侧 `06-app-platform.md` §2.4 明写 X11／Wayland／GLFW 未实现缩放变化上报，`scale_factor()` 在改系统缩放或跨屏后仍是旧值 ⇒ 恢复按旧 scale 折算。本仓的 DPI 老账（`G17`／`G21`）在 POSIX 腿上的现状读数。
  - **落盘是显式提交且无脏标记**：`Preferences::flush()` 回 `Result<void>`，内部已做「`<file>.lock` advisory 锁＋临时文件＋原子 rename」，本仓不重造（裁决 7.26②）；`Preferences` **不**提供「自上次 flush 后是否变过」的查询 ⇒ 两处 flush 是无条件的，`windows.json` 在第一次退出时即被创建（正常路径下那时至少已存过一枚几何）。
  - **接缝的归属**：`src/config/store.cpp` 现在仍是「全仓唯一触达 Aurora `Preferences` 的翻译单元」（裁决 7.26③、架构 §2.2），而 `set_window_geometry_store` 要的是 `Preferences *`。批 5 因此另起一枚 `src/config/window_geometry_store.{h,cpp}`（**私有头**，理由同裁决 D1①：声明里含框架类型），把第二份 `Preferences` 关在这一个 TU 内，公共头 `include/borealis/config/` 一行不动 ⇒ 「本仓与框架的读写接缝仍在 config 侧」这条边界不扩散到 `main.cpp`。
- **F27 批 6 开工实测（运行中开的那一扇窗与几何自动链，2026-10-21，读 Aurora 活动分支源码）**：恢复那一跳写在 `Application::register_host` 的**尾部**（`hosts_.push_back` 之后紧跟 `restore_geometry_for(*raw, opts.persist_id)`），与 `set_window_geometry_store` 的补做循环是**两个入口、同一条腿**。⇒ 「进程已经在跑、由 CLI 转交触发开的那一扇新窗」只要走本仓既有的开窗工厂（`open_workspace_window` 在建窗前把 `persist_id` 写进 `WindowOptions`），几何就自动按**它自己那一格**恢复——批 6 因此**零新增几何接缝**，也不需要为 `--new-window` 另写一次恢复。真机上「新窗放得回去吗」与 §9 第 4 条同源，读数为 §9 第 5 条新增那一档。

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

## 3 框架侧缺口（按 AGENTS §5.1 分流，但本棒不等不绕）

**2026-10-22 出件补记**：本节原写「候选，待登记」的两条已按 §5.1 各自出件，加上批 4 撞到的第三条（关闭否决，本节原未列，读数在 F17／F18、口径在稿 D4），**三张任务书在册**＝`codespec/FRAMEWORK_TASK_WS03_G42.md`／`codespec/FRAMEWORK_TASK_WS03_G43.md`／`codespec/FRAMEWORK_TASK_WS03_G44.md`（裁决 **7.127**，附录 A.2 三行有指针）。本节的「不等不绕」照旧成立：三张任务书不改本棒任何判据，也不引入应用侧的私改路径。

**G42（原「候选，待登记」，已登记为附录 A.2 的 G42，并于 2026-10-22 出任务书 `codespec/FRAMEWORK_TASK_WS03_G42.md`）**：应用级共享 `ShortcutRegistry` 的 `ShortcutBinding::action` 是无参闭包，`Application` 也没有「当前活动窗口」查询（F4／F5），而 `window_state()` 只聚合主窗、`request_wake_all()` 是 private（F7）。多窗应用因此**无法从公共面得知一条全局快捷键该落在哪一扇窗**。两条可接受形态：A) `ShortcutBinding::action` 改带 `WindowId`（或 `ShortcutContext{WindowId}`）；B) `Application::active_window()`（按最近一次宿主侧键盘/指针派发判定，无活动窗时回 `main_window()`）。
**本棒不阻塞**：§4 的 D7 用本仓自持的「最近派发窗」记录器兑现同一事实，属可组合的应用侧能力（我们的视口控件本来就吃到那一笔按键）；待任一条回货，替换点只在那一枚解析器。按 §5.1 的「先在 Aurora 补公共 API」在这里让位给「本仓可组合」的判定，理由与代价见 D7③。

**G43（原「候选缺口二，未编号待登记」，已登记为附录 A.2 的 G43，并于 2026-10-22 出任务书 `codespec/FRAMEWORK_TASK_WS03_G43.md`）＝`WindowGeometry::mode` 有存无取**：框架把 `mode` 序列化进几何（`window_geometry_to_json` 六键之一，反序列化还校验它落在 `[0,3]`），恢复侧 `apply_host_geometry` 却只 `set_size`＋`set_position`，`mode` 全程无人消费 ⇒ 「最大化／全屏那一种重启」回不到那个态，而文件里那一格写得清清楚楚，读者会以为功能存在。补法有两条（A) 恢复腿按 `mode` 追一次 `set_maximized`/`set_fullscreen`，并处理「目标显示器已不在」的降级；B) 明确 `mode` 不参与恢复、从结构里去掉，免得存一份永远不读的字段。**本棒不阻塞也不补偿**：在装配层私调最大化要猜框架的时序（宿主登记完、首帧之前那一截没有公共入口可挂），属 AGENTS §5.2「不在应用侧私改框架路径」与 §4.1 第 3 条「不做半截实现」两头都不占的那一档。现状读数与真机判据见 §0 的 F26③、§9 第 4 条①、§10 批 5 补记第 4 条。

## 4 待人裁决（每条已给推荐①）

- **D1 一扇新窗的初始内容**：① 推荐＝**一个新缺省本地标签**，目录取**配置值**、**不**继承发起窗的 OSC 7（新窗没有「当前标签」，`INTEG.02` 的判据「当前标签是活着的本地终端」在结构上不成立）。备选＝继承发起窗焦点格（代价：两扇同名标签同一目录，与「新窗是独立工作现场」的直觉相反）。代价：用户想要「把这格挪到新窗」时只能手工再开。
- **D2 新窗入口**：① 推荐＝命令 `workspace.new_window` + `Ctrl+Shift+N`（分屏保留键表的**第十五条**，`utest_workspace_keys` 的认领集随之 14→15 条）+ 命令面板一条 + 标签条不加「＋新窗」按钮（「＋」已归新标签）。备选＝只做命令不做键（与 WT 习惯不符）。
- **D3 关窗的判据与出口**：① 推荐＝**不做**「末位标签关闭 ⇒ 关窗」（`WS.01` 的末位标签本来就没有关闭按钮，`ARCHITECTURE.md` §8.1 把那条路径挂在本条名下，但按现状它不需要存在）；关窗只有 OS 关闭钮与 `workspace.close_window`（**不给缺省键**，`Ctrl+Shift+W` 留给「关标签」的既有直觉）。代价：架构 §8.1 那句话要就地改口（见 §10 文档回写）。 **落地状态（批 7 收口，2026-10-21）**：前半句**已按推荐档落**（末位标签不关窗，§10 那句改口已写进 `ARCHITECTURE.md` §8.1、`UI_WORKSPACE_INTERACT.draft.md` 判据 ⑳ 与裁决 **7.126**④）。后半句有**两处与实际不符**，按代码运行时为准登记：⑴ **`workspace.close_window` 未落**——批 3-B 只落了 `workspace.new_window`，故程序化关窗入口不存在，只有 OS 关闭钮；需求没要求它，记为欠项而不是漏项。⑵ 推荐档那句「`Ctrl+Shift+W` 留给关标签」的前提本来就不成立——`src/ui/workspace_keys.cpp` 的保留键表里 `Ctrl+Shift+W` 一直是 `WorkspaceCommand::ClosePane`（关当前 pane），标签的关闭只有标签条那颗按钮，而那颗按钮按 `has_close_button = (tab_strip.count() > 1)` 给，末位不给。
- **D4 关窗要不要确认「还有会话在跑」**：① 推荐＝**本棒不做**——框架没有「关闭请求可否决」的公共入口（**F18 已实测**：全仓无 `close_requested` 回调、无返回 bool 的拦截点；原先只按 F1 推断，现升级为读数）。即便在 `set_on_frame` 里抢在帧末弹框也**关不下来**（F17 的 `reap_closed` 紧跟在 `render_all` 之后），结果只会是「框还开着、窗已经没了」。做 veto 需先有 Aurora 侧接缝，登记为候选缺口。关窗即随窗析构会话，与关标签的确认判据不同档，登记为边界。备选＝向 Aurora 提关闭否决任务（跨仓，另棒）。代价：用户点 X 时正在跑的进程被静默带走 ⇒ 这条必须写进 §10 的「不称可用」账。
  - **2026-10-23 落地状态（改判）**：Aurora 已回货 G44（`Application::set_on_close_requested`），推荐档①「本棒不做」由框架能力变化解除——本仓按 `codespec/WS03_WINDOW_CLOSE_CONFIRM.draft.md` 落了**按窗**关窗确认（OS 来源、按窗活会话聚合、确认框可否决；程序化与策略连带放行），判据与读数见该稿与裁决 **7.138**。真机走查见 §9 第 8 条；本稿 D4 的推荐档原文照录不改写。
- **D5 广播组跨不跨窗**：① 推荐＝**跨窗成立**（组本就 app 级、成员键 `(TabId, PaneId)` 全局唯一，F10/F14），横幅改**每窗一枚**且只列本窗成员，计数按全组去重会话数（与 `WS.09` 现口径同一条算式）。备选＝组按窗隔离（代价：切窗丢组，违背「批量操作」的初衷）。
- **D6 面板与浮层的单例性**：① 推荐＝设置页／密钥／SFTP／隧道／侧栏／命令面板保持 **app 级单实例**，浮层挂到**发起命令的那一扇窗**的 `OverlayHost`（关闭即摘回），同一面板换窗打开时先从原窗摘掉；凭据询问链同一条规则。备选＝每窗各一套面板（代价：`pending_*` 挂起态、worker 队列、密钥扫盘缓存全部要复制，本棒做不完）。代价：两扇窗不能同时各开一块 SFTP 面板；设置页在两窗间是「搬过去」而不是「各有一份」。
- **D7 命令与快捷键落哪一扇窗**：① 推荐＝本仓自持 `ui::ActiveWindowTracker`（纯逻辑件：每窗在「本窗的视口/标签条/横幅收到一次按键或一次指针 Press」时上报自己的 `WindowId`，tracker 记最近一次；查不到回主窗）。备选＝向 Aurora 提 G42（§3），等回货再开工。代价：OS 层「哪扇在活动」与本仓「最近收到派发的窗」在无头与失焦场景下可能不同（真机走查项），且回货后要替换一处；换来的是**本棒不跨仓、不阻塞**。
- **D8 窗口标题**：① 推荐＝**每窗各写各的**（本窗选中格的 `resolve_tab_name`，沿用 `WS.06` 的逐帧差分写法），标题内容不含窗序号。备选＝加「窗口 N」后缀（代价：OS 任务栏噪音，且单窗常态下多一枚后缀）。
- **D9 几何分别记忆的键与载体**：① 推荐＝用框架 `persist_id` + `preferences::Preferences`（F6），文件落配置目录另起一枚（`windows.json`），键＝`main`（主窗）与 `window-2`、`window-3`…（创建序）；`flush()` 取在「任一窗关闭后」与「退出前」两处。明确**不**进 `config::Settings` schema（代价：设置核对表 69 键不动、`PREF.07` 的导出件不含几何——几何是本机屏幕相关值，跨机导出即坏值，这条是特性不是漏项）。备选＝自写 `Settings.windows[]`（违背 AGENTS §5.2「原语须以公共 API 存在，本仓只消费」）。
- **D10 `--new-window` 放开**：① 推荐＝`StartupAction` 加 `NewWindow`（线格式 magic/version/action 字段都不动，action 多一个值），转交侧收到即开一扇新窗并按 D1 落一个缺省本地标签，**`--profile`/`--cwd` 与 `NewWindow` 相容**（新窗的首标签吃该档案/目录）；`--help` 文案补一条。旧实例若收到它不认识的 action 值 ⇒ 整帧 fail-closed 拒（F15），诊断照旧退 2。代价：混版（旧实例 + 新 CLI）时该旗标不生效。**已落（批 6，2026-10-21，形态、读数与五则变异见 §22）**，与推荐档有四处口径差：⑴ 旗标是**第三个动作的写法**而不是修饰符——`--new-window` 与两个子命令互斥，解析产出里它同时置 `new_window` 并把 `action` 折成 `NewWindow`（「在新窗里切当前窗的那一格」没有可读的语义，故不给组合档）；⑵ 线格式**两个读法都认**：`action` 字段写 2，同时保留 flags bit0，接收侧任一读到即按新窗处理——旧版 CLI 只会写旗标（动作仍是 `new-tab`），那条帧是合法混版帧而不是坏帧；⑶ **冷启动那一侧它与缺省 `new-tab` 共用同一条腿**（正要建的这一扇就是那扇新窗），不额外开窗、不报错；⑷ 「动作 `new-tab` ＋旗标」按同一含义接受，而「`split-pane` ＋旗标」这一格解析器产不出、只可能来自手工帧 ⇒ 接收侧显式拒并退 2。另补一处推荐档没写的接线：开成新窗后向 `ActiveWindowTracker` **上报该窗号**，因为四条档案腿是经 `tab_host_or_none` 现取落点的（含凭据询问那条异步续跑），不上报则首标签可能开在旧窗里。
- **D11 新窗的退出与「最后一扇」**：① 推荐＝保持框架缺省 `LastWindowClosed`（F8，且 **F21 实测**：缺省即此值）——关掉最后一扇窗即退出，退出序沿用现主窗那一条（先 `startup_wake.store(nullptr)` → `instance_listener.reset()` join 线程 → 遍历**全部窗全部格**的会话 `close()`），`ExplicitOnly` 与 `MainWindowClosed` 都否。代价：主窗先关时「主窗」身份随帧末回收落在保留的最后宿主上（**F19 实测**：`reap_closed` 保留最后一个宿主，于是关唯一那扇时 `set_on_window_closed` **根本不触发**），`app.focus()` 语义仍指主宿主 ⇒ 本仓不得再依赖 `app.focus()`（F3 那一处改按窗取）；也正因 F19②，**关窗撤账的入口不能只挂 `set_on_window_closed`**，要用 `set_on_frame` 里轮询 `should_close()`（F17 的唯一销毁前可观测点）。退出序本身在无头面**不可证**（F21：全为无头宿主时 `should_exit()` 永假），只能进 §9 走查清单。
- **D12 撤销重开（`WS.10`）跨不跨窗**：① 推荐＝`ClosedTabStack` 改**每窗一份**（撤销是把标签放回「我关它的那扇窗」），栈上限与顺序语义不变（`utest_closed_tab_stack` 零改动）。代价：跨窗撤销不再可能（本条需求没要求），`tab.undo_close` 的动作按 D7 解析目标窗。
- **D13 逐窗尺寸下发与去抖**：① 推荐＝不改一件现有代码——去抖表与 `Session::resize` 的归属在 `WorkspaceView`（每窗各一份），`dispatch_grid` 按格取会话（7.125）；新窗的 surface 由 `WindowOptions::size` 给初值 960×640，之后走既有布局派生。代价：D9 的恢复几何落在**开窗那一刻**，首帧行列数随恢复后的尺寸算（须验一条「恢复后首帧只下发一次」）。
- **D14 本条的验收面**：① 推荐＝批 1 起每批带无头用例（两扇 `HeadlessSurface` 窗），真机走查列 §9 清单、未走完不称「可用」（§4.4 第 33 条）；`main.cpp` 不进 runner 这条物理事实不变（裁决 7.83），故「两扇窗真能各开各的」那一档只有源码审查 + 走查。

## 5 数据流与不变量

1. **建窗**＝`create_native_window(opts)` → 装 `WorkspaceWindow`（`TabStrip`/`root_column`/横幅/`OverlayHost`/`chrome`/`Scene`）→ `app.open_window(...)` 取回 `WindowId` → 回填 `owner` → 建首标签（`next_tab_id` 全局取号）→ `present_root` 只在**该 Scene** 上做一次。
2. **泵**＝`set_on_frame` 一轮遍历**全部窗**：逐窗排空启动请求队列（一次，全局）→ 逐窗 `rebuild_root_children`（脏时）→ 逐窗取本窗焦点格的 OSC 标题写本窗标签名与标题（7.125 的「焦点格独占」在窗内成立，跨窗互不干扰）→ 逐窗 `set_exited` 与横幅刷新。
3. **唤醒**＝每条会话的 `frame_wake` 捕**所属窗**的 surface（F11 的修法）；转交线程仍只叫醒 `startup_wake` 那一枚（F15，叫醒任意一扇即跑到帧循环）。
4. **拆**＝关窗只拆呈现（`WorkspaceWindow`），标签与其会话仍由 `teardown_pane`/`perform_close` 那两笔账撤；窗析构序必须**先摘浮层再散 host**（与 `G34` 那棒同源的教训），且 `tab_workspaces` 里该窗名下的标签要逐个走与关标签同一条撤账链（广播成员、四张连接表、`Session::close()`），不允许「窗散了而表里还挂着标签」。

## 6 分批（每批一个分层提交）

1. **批 1（拆分，行为不变）**：
   - **1-A（已落，2026-10-21）**：itest harness 先跑通「两扇无头窗各渲染各自 `TabBar`」——只加测试 TU，生产一行未动。
   - **1-B（已落，2026-10-21）**：`WorkspaceWindow` 结构 + `open_workspace_window()` + `std::vector` 持有；主窗走同一条构造路径。生产仍只开一扇 ⇒ 现有全部用例零改动即须绿；`main.cpp` 不进 runner，本批无自动化证人，只有源码审查（裁决 7.83）。**落到的形状与差的那一截见 §12**——装配闭包此刻仍按别名吃第一扇窗，`open_workspace_window()` 只造地基半区（窗 + 浮层宿主 + 根 Column），标签栏与横幅两支仍由装配层在原处填进成员。
2. **批 2（逐窗接线）**：
   - **2-A（已落，2026-10-21）**：`TabWorkspace` 补 `owner`（指针而非 `WindowId`）、会话 `frame_wake` 与视口浮层宿主/焦点管理器按属主窗取（`wire_view` 加形参、`window_focus` 按 `app.window_host(id)` 取）。
   - **2-B（已落，2026-10-21）**：`set_on_frame` 的角标/标签名/OSC/退出态四条写给 `owner.tab_strip`，窗口标题改为遍历 `workspace_windows` 逐窗差分写回（每窗一份 `last_title`）。四处 `app.focus()` 只收敛了视口那一条，`DebugPanel`／`StartupNotice`／`SettingsPanel` 三条与布局重建的按窗过滤留在批 4（面板「搬窗」＝D6，落点窗＝§7 撤销重开那条），口径与台账见 §13。
3. **批 3（新窗入口与命令落点）**：
   - **3-A（已落，2026-10-21）**：`ui::ActiveWindowTracker` 纯逻辑件 + `utest_active_window_tracker`（五例、四则变异见 §14）。生产侧一行未动，件是「已测而未用」，同批 1-A 那一档次序。
   - **3-B（已落，2026-10-21）**：把主窗的「标签栏 hooks + 关闭确认 + 布局重建」收成按窗可复用的一段，`open_workspace_window()` 走到全形；命令 `workspace.new_window` + `Ctrl+Shift+N`（第十五条保留键，`utest_workspace_keys` 的认领集 14→15）+ 面板一条。形态、承重次序与四则变异见 §15。
   - **3-C（已落，2026-10-21）**：三处上报点接线（视口＝按键与指针按下两条腿／标签条 Press／横幅 Press）＋ `search.open`／`session.restart`／`serial.set_baud`／`workspace.toggle_fullscreen`／`tab.undo_close` 五类闭包按 tracker 解析目标窗。形态、闭包接线次序、统一兜底与四则变异见 §16。
4. **批 4（拆窗账目）**，拆三棒，**次序是硬约束**：
   - **4-A（别名收口，先做，已落 2026-10-21）**：把装配层仍然按别名吃第一扇窗的读法（`first_window` 及其 `surface`／`tab_strip`／`current_tab_id`／`banner`／`overlay_host` 五枚别名）一律改成「按窗现取」。**必须在拆账链之前**：`workspace_windows` 一旦 `erase`，`first_window` 是它的引用（批 1-B 的台账记过这一点），五枚别名连同 `au::Surface &` 全部悬空——先收别名，erase 才是可做的动作。本棒行为不变、单窗形状不变。落进来的两件事与两件事**没**落进来的，见 §17：横幅的逐窗分流（D5）与别名是同一处代码，一并收；`Ctrl+F` 的窗内 `view_of(1)` 属 `SPEC.FEAT.INTERACT.04`，波特率选择框的浮层落点属 D6＝4-C。
   - **4-B（关窗撤账链，已落 2026-10-21）**：撤账链＝按 `perform_close` 同一条链关掉该窗名下全部标签（不弹确认，理由见 D4／F18）→ 归属兜底清扫（`owner` 指着这扇窗而栏里已无其格 ⇒ 同一条撤账＋一条 WARN）→ 摘广播成员 → `active_window_tracker.forget(id)` → 从 `workspace_windows` 移除 → 兜底落点易主（取不到落点即把本层账目里还活着的第一扇经 `set_fallback` 交回 tracker）→ 重指或清空 `startup_wake`。**触发点改口**：原写「`set_on_frame` 里轮询 `should_close()`（F17），**不用** `set_on_window_closed`（F19）」，落进来的是反的那一个——理由不是时机而是销毁序：`on_frame` 排在同一帧的 `render_all` **之前**，本层若在那一跳里 `erase` 容器元素，这一帧仍会为那扇已请求关闭的窗布局与绘制，而 `TabBarWidget`／横幅的 hooks 捕的是 `WorkspaceWindow &`，读的就是刚被析构的那一格；宿主回收之后（`on_window_closed` 的时点，`window_host(id)` 已取空）那一扇再不会被渲染，控件树随宿主一起散，本层对象才是可以安全落地的时点。「销毁前要能看见关闭请求」那一半留给稿 D4（关闭否决），本棒不做（F18：框架不给否决入口）。**代价照 F19 登记**：关最后一扇时框架保留主宿主、这一跳**不触发**，那一扇的账目留到 `run()` 返回之后由退出序统一收。证人用 `HeadlessSurface::set_should_close(true)` 真驱动（F24），无头可跑（F21）；形态、四案读数与变异自证见 §18。
   - **4-C（面板按窗与退出序）**：面板「搬窗」而非「各一份」（D6，含波特率那只框的浮层落点与 `hooks.preview_wake` 那条 `TODO`）、剩余三处 `app.focus()`（`DebugPanel`／`StartupNotice`／`SettingsPanel`）改按窗取（F19／F22 之后这一条不能再拖）、`ExitPolicy` 缺省确认与退出序遍历全部窗（F21：无头不可证，进 §9 走查）。
     - **4-C-1（面板搬窗，已落 2026-10-21）**：七只瞬态面板加 `set_overlay_host`／`overlay_host` 接缝，两只常驻框加 `set_overlay_destination`（宿主＋焦点权威成对换）与 `abandon_overlay_destination`（散窗兜底腿），装配层直挂宿主的三只常驻浮层另立「宿主＋序号」那份搬窗账目，装配层两条腿接上（打开腿先搬再开、撤账腿 ③′ 把挂在散掉那一扇上的十二件搬到落点）。**改口两处**：稿里写的「剩余三处 `app.focus()`」实测只剩两处（`DebugPanel`／`StartupNotice`），`SettingsPanel` 构造吃的是 `app.shortcuts()`——快捷键表按 F2 本就是 app 级一份，那一条对它不适用；`hooks.preview_wake` 那条 `TODO(SPEC.FEAT.WS.03)` 已收（真值源改成面板自己记的那枚宿主指针）。形态、十二件清单、四条代价档与六则变异见 §19。
     - **4-C-2（`ExitPolicy` 缺省与退出序遍历全部窗，已落 2026-10-21）**：**`ExitPolicy` 一行代码未改**——D11 的推荐档就是保持框架缺省，而实测读数确认「缺省即 `LastWindowClosed`」这件事（本仓从未调 `set_exit_policy`，Aurora 主仓 `include/aurora/app/application.h` 里 `exit_policy_` 的成员初值即 `ExitPolicy::LastWindowClosed`），另把「为什么不是另外两档」写进 `main()` 末尾那段注释，不留给下一个读者猜。落进来的那一半是**退出序**：`run()` 返回之后那条「直接扫全局 `tab_workspaces`」改成「逐窗走三步——关该窗名下每一格的全部会话 → 摘该窗的根 → 撤那些格的账」，表里若还剩下格（只可能是「不属于任何活着窗的栏位」那种分叉态）一律同一条腿收掉并落一条 WARN。摘根那一跳是 §12 留的那句「要把这一段显式化，而不是继续依赖声明序」的落点：`workspace_windows` 声明在最前、最后析构，于是根上那份「当前那一格的视口」原本要活得比它背后的会话久，而 `~TerminalView` 的 OSC 52 跨帧承诺回写（`SPEC.FEAT.CONN.12` 判据 ⑤）走的就是这条腿。零新增用例（F21：无头面不可证，进 §9 第 3 条走查）⇒ 门读数不变即「不破坏既有形态」那一侧的证据；形态、等价性论证与两档不可证清单见 §20。
5. **批 5（几何分别记忆，已落 2026-10-21）**：`persist_id` 分配 + `preferences::Preferences` 落配置目录 + 两处 `flush()` + 「恢复后首帧只下发一次」用例。落进来的形态、六案单元读数与四案集成读数、四条变异（含一条**等价**注入的指名）见 §21。两处**刻意不做**：`WindowGeometry::mode` 存而不取（F26③，本仓不补偿）、键不进 `group("windows")`（F26②，写进去就是「存得回而恢复取不到」）。
6. **批 6（CLI，已落 2026-10-21）**：`StartupAction::NewWindow` + 编码/解码 + `parse_startup_args` 旗标语义 + `--help` 文案 + 撤 `src/main.cpp` 里那条「`SPEC.FEAT.WS.03` 未落地」的拒绝支 + `utest_cli_startup`／`utest_cli_handoff`／`itest_posix_instance_channel` 扩案。落进来的形态（旗标即动作、两读法、冷启动共腿、开窗腿上报 tracker）、门读数、产品侧退出码走查与五则变异见 §22；与推荐档的四处口径差已回写进 D10。
7. **批 7（收口，已落 2026-10-21）**：`SPECIFICATIONS.md` 的 WS.03 现状句与裁决、`CHANGELOG.md` 新版本、`PLAN.md` 的 WS.03／INTEG.04／WS.10 行、`ARCHITECTURE.md` §8.1 那句「末位标签关闭 → 关窗口属 WS.03」改口与 §8.4 多窗口段、本稿追加结项读数（落成 **§23**，稿面原写的「§8 结项读数」是编号笔误——§8 是「与既有裁决的相接处」）。落点清单、三处同口径计数迁移与两处**开工后才读到的不符**见 §23。

## 7 判据与变异计划（结项三件齐：ctest 全绿 + runner 案数增量 + 变异证人逐条转红）

- `utest_active_window_tracker`（新）：多窗上报次序、失配回主窗、窗关闭后该号不再被返回、并发上报只留最后一次。**已落（批 3-A，五例，台账与四则变异见 §14）**；其中「窗关闭后该号不再被返回」实现为撤掉当前落点即**回主窗**而不是回上一个上报过的窗（本件不做栈），该档形态选择与理由见 §14②。
- `itest_workspace_window`（新）：两扇窗的标签互不串（`current_tab_id` 各一份）、横幅只列本窗成员、撤销重开回到关它的那扇窗、逐窗标题差分写回、关一扇不退出而关最后一扇走同一条退出序、窗拆后 `tab_workspaces`/四张连接表/广播成员三处账目为空。**批 1-A 已落该套件的前五案**（harness 地基＋§5 四条不变量的窗侧形态，台账见 §11），**批 3-C 加落落点记档一案**（§16③）。上面列的其余判据里，「关一扇不退出」在批 4-B 用 `HeadlessSurface::set_should_close(true)` 真驱动后**已落**（`itest_workspace_window` 四案：关一扇不退出而另一扇仍在渲染集合里、`on_frame` 与 `on_window_closed` 各自看得见什么、主窗易主、最后一扇被保留——读数见 §18）；「窗拆后三处账目为空」的**执行体**在 `main.cpp` 的撤账链里，无头面只能证到它的**前提**（宿主已摘、按窗现取的腿自此取空＝命令落空），本身仍无证人（裁决 7.83）；**「关最后一扇走同一条退出序」在无头面物理上不可证**（F21：全为无头宿主时框架永不驱动退出），只进 §9 第 3 条走查，本稿不把它算进案数账——批 4-C-2 落的是这条退出序腿的**本体**（逐窗三步＋兜底，§20），判据本身仍在那条走查上。「横幅只列本窗成员」这条的**折算点**在 `main.cpp` 的 `refresh_broadcast_ui`（批 4-A 落的逐窗分流，§17⑵）——同属无自动化证人那一段（裁决 7.83）；控件侧「喂什么状态画什么」在 `itest_workspace_broadcast` 已有案，本稿也不把它算进案数账。
- `utest_window_geometry_store`（新，**已落（批 5）**，六案）：键的形态（`main` 与 `window-2` 起、且键里不含点号）、文件绑到 `<目录>/windows.json`、`flush()` 后换一个实例仍读得到同一格、**写进 `group("windows")` 的那一格框架自动链取不到**（F26② 的陷阱证人）、不可用几何回空值（完全屏外／尺寸非正回空，部分越界仍算可用）、落盘失败回一条 ASCII 原因而不是抛出。判据与读数见 §21⑵。
- `itest_workspace_window` 的批 5 四案（**已落**）：各窗取自己那一格的尺寸与位置、没有那一格的那扇保持默认、恢复落在**首次 present 之前**（快照里尺寸已是恢复值而 present 计数仍 0）、关掉某一扇把最终几何写回**它自己那一格**而活着那扇不写。harness 为这四案加的接缝（`WindowFace` 的 `persist_id` 入参、驱动台的存储入参与首帧快照、四个读数口）与取值边界见 §21⑶，四条注入与一则等价读数见 §21⑸。
- `utest_workspace_keys`：认领集 14→15 条。**已落（批 3-B）**，同时新增 `itest_workspace_layout.ctrl_shift_n_is_claimed_and_only_handed_to_the_assembly_layer`——认领与「只转交、不自己开窗」这一接缝是唯一有自动化证人的部分，`main.cpp` 里真正开窗那一段仍无证人（§15）。
- 三处上报点（稿 D7，**已落（批 3-C）**）各有证人，`main.cpp` 那一段接线本身无证人（裁决 7.83）：
  - `itest_workspace_layout.the_viewport_reports_each_key_and_mouse_press_exactly_once`：视口的两条腿（键＝按下、鼠＝按下）各报一次，被工作区层认领的键同样报（上报先于前置过滤），抬起与移动不报，转发腿不因上报而改变。
  - `itest_workspace_window.a_tab_bar_press_moves_the_active_window_record`：经**该窗宿主**派发一次栏带按下即把本窗号交进 `ui::ActiveWindowTracker`，且落点取的是**空白栏带**——上报不挂在命中分支里（§16③ 记这条的第一版落点是栏位、因此对 M3 等价）。
  - `itest_workspace_broadcast.the_banner_reports_the_press_that_cleared_the_group`：横幅那一次按下先于清组判定上报（上报时组仍满），抬起与失活后的第二次点击不重复报。
- `itest_panel_retarget`（新，**已落（批 4-C-1）**，十一案）：面板搬窗这条接缝的自动化证人，判的全是**控件侧**的可断言之形——瞬态七件（`ConnectionSidebar`／`ConnectionWizard`／`CredentialPrompt`／`KeysPanel`／`SettingsPanel`／`SftpPanel`／`TunnelPanel`）走同一张判据模板：基线 → 打开 → 同宿主重复设＝空操作 → 搬到另一扇 ⇒ 旧宿主清零、新宿主也**不**挂着（换完是关闭态，这一档就是「不是各一份」那条口径的读数）→ 再开 ⇒ 只在新宿主上有那一层。两只常驻框另判焦点作用域（`DebugPanel`：旧栈弹回、新栈由下一次 `show()` 自己压；`StartupNotice`：搬过去**仍开着**，一启动只弹一次那条不能因搬窗而消失）。散窗兜底腿另两案：`DebugPanel` 的旧宿主先经 `unique_ptr` 真散一次，判的是「此后任何一次 `remove_overlay` 都是往已释放对象落手，本件不许走 `set_overlay_destination` 那条腿」，`StartupNotice` 那案则在旧宿主**还活着**时判「旧栈没被弹、旧宿主那层没被摘」（代价档见 §19⑵⑶）。装配层那六处入口与撤账腿 ③′ 本身仍无证人（`main.cpp` 不进 runner，裁决 7.83）。
- `utest_cli_startup`／`utest_cli_handoff`：`--new-window` 的旗标解析与 `NewWindow` 的编解码往返、未知 action 仍整帧拒。**已落（批 6）**——`utest_cli_startup` 12→14 案（旗标折成第三动作、与两个子命令互斥各一案，另把「政策不拒 `NewWindow`」并进既有政策案），`utest_cli_handoff` 6→7 案（新值往返 + 旧旗标仍读得通 + 越界值不编码），`itest_posix_instance_channel` 8→9 案（**真协议帧**过通道：含 NUL 的字节流原样到达、解码回来与发出那份逐字段全等）。判据、四条产品侧读数与五则变异见 §22⑶⑷⑸。
- 变异候选（每条一个判据，注入后按 §4.4 第 22 条点名转红）：新窗继承发起窗目录（应为配置目录）、`frame_wake` 全部捕主窗 surface、`current_tab_id` 回全局一份、横幅恒列全组成员、`ClosedTabStack` 回全局、关窗不撤标签账、`persist_id` 恒空、`--new-window` 仍走拒绝支（**批 6 之后这一条候选随那条拒绝一起撤了**，等价的新候选＝「接收侧不认新动作／旗标」，落点在 `main.cpp` ⇒ 同属无自动化证人那一段，只有 §22⑷ 的产品退出码读数）、tracker 无记录时不回主窗。**其中「`current_tab_id` 回全局一份」「横幅恒列全组成员」「关窗不撤标签账」三条的注入点在 `main.cpp`**（批 4-A 之后那两枚别名与逐窗分流都在装配层）——第 22 条要求的变异自证在这一件上只能落进 §9 走查与源码审查，本稿不冒充读数（与 §16 末段「未做的两则」同一档，理由同一条：`main.cpp` 不进 runner，裁决 7.83）。批 4-B 新落的四则注入（tracker 两件转红、harness 两件转红）与各自的等价档读数见 §18——那一条注入点的界同样受这条边界约束：帧循环那半区的真值在 Aurora 侧，本棒不改框架、不做框架侧注入。批 4-C-1 新落的六则注入（M1–M6，落点全在本仓九件控件源文件内）与各自的转红读数见 §19。批 5 新落的四则（`persist_id` 恒空＝本清单里那条候选，落点 `src/config/window_geometry_store.cpp`；两扇共用一格；完全不接线；把首帧快照挪到出帧之后）与**一则指名等价的**（接线排在第二扇登记之前／之后——setter 对已登记宿主补做一次恢复，两档读数相同）见 §21⑸；四条注入的落点全在本仓源文件内，不改 Aurora。批 6 新落的五则（旗标折动作的条件改恒假、删旗标侧的互斥判、编码侧的 action 上界写死旧值、解码侧写死越界值、通道按 C 串取帧长）与各自**只红一案**的点名读数见 §22⑸；五则落点全在本仓纯逻辑件与 POSIX 通道源文件内，不改 Aurora。

## 8 与既有裁决的相接处

`7.7`（`--new-window` 显式开新窗）＝D10；`7.43`（末位标签不归 `TabStrip` 关）＝D3 的依据；`7.47⑧`（关闭确认判据）＝D4 的对照；`7.82`（装配层「标签 → pane 树 → 每 pane 会话与视图」）＝§2 拆分边界的起点；`7.121`（广播 app 级单组）＝D5；`7.124`/`7.125`（CLI 与多会话）＝F15 与 D10；`7.86`/`7.100`（重启按 `(TabId, PaneId)`）＝命令落点按窗解析后的同一条接缝。

## 9 真机走查清单（未走完不得称「可用」）

1. 两扇窗各自最大化／还原后网格是否铺满（DPI 那条老账按窗各验一次，`G17`/`G21` 的历史读数只覆盖单窗）。
2. 键入落活动窗：A 窗聚焦后 `Ctrl+Shift+N`、`Ctrl+Shift+D`、`Ctrl+F`、`Ctrl+Shift+R` 是否各打各的（tracker 的 OS 活动态偏差档）。两档已知会答非所问、须取真机读数：① 新窗落成后**第一件事**就是全局快捷键／面板命令（开窗腿不上报，§16 末段）；② Alt+Tab 到 B 窗后**第一下**拖把手或点缩放角标（工作区层不上报，同段）。
3. 关一扇窗后另一扇不受扰动、广播组与横幅跟上；关最后一扇退出时无残留进程（两腿）。批 4-C-2 之后加取三条读数：① 日志里**不该**出现 `exit: tab N is not listed by any live window's strip`（出现即「栏位表与全局表分叉」的证人，那条兜底腿正常路径恒不走）；② 退出过程无 ASan／无崩溃、且 `main()` 返回后不留 PTY 子进程——多窗各带若干标签、从任意一扇开始关到最后一扇，两种次序（先关主窗／先关新窗）都要取一次；③ 关掉最后一扇之前**先触发一次 OSC 52 读方向询问并保持其不答话**（`SPEC.FEAT.CONN.12` 判据 ⑤ 的那条在途腿），这一条是摘根那一跳的实际靶子：原形态下 `~TerminalView` 会在会话已散之后才运行。`ExitPolicy` 缺省＝`LastWindowClosed`（F21 在无头面不可证），② 这一条是唯一能取到真值的地方。
4. 几何记忆：挪位、改大小、最大化三态各重启一次，显示器拔掉时的回退档。批 5 之后加取三条**只有真机能答**的读数（三条都在无头面读不到，理由逐条写在括号里）：① **最大化那一种重启后回不来**——框架 `apply_host_geometry` 只 `set_size`＋`set_position`，`WindowGeometry::mode` 存而不取（F26③），本仓按 AGENTS §5.1／§4.1 第 3 条不补偿、也不在应用侧私改渲染路径 ⇒ 真机要确认「最大化窗重启后回到普通态且尺寸是最大化前那一份」，并把它作为 Aurora 侧待议项登记（§3 候选缺口二、§21⑹）；② **拔屏回退档只在 Windows 腿取得到**——POSIX 腿 `app::list_displays()` 恒回一张合成的 1920×1080 @(0,0) 单显（`display_win32.cpp` 的 `#else`），屏外几何只能靠手写越界值造（`utest_window_geometry_store` 走的正是这一档），真机上「拔掉一台显示器再启动」取的才是那条真值；③ **Linux 三后端的缩放不跟**——X11／Wayland／GLFW 的 `scale_factor` 不随改缩放或跨屏更新（Aurora 主仓 `codespec/specification/06-app-platform.md` §2.4 明写），故恢复按**旧值**折算，真机要读「改系统缩放后重启，窗尺寸是旧 DPI 的那一份」这一偏差是否可接受。另加一条：两扇窗各自挪位、各自改大小后重启，验的是 §21⑶ 第一案在真后端那一档的同一判据（无头面证的是机制，X11 的 WM 会改位置与装饰，只有真机能证「放得回去」这件事本身）。
5. `--new-window` 的转交与 `--profile`/`--cwd` 组合在真机上的落点。批 6 之后拆成七档读数（前三档就是 §22⑷ 那四条退出码读不到的一半）：① 首实例在跑时 `Borealis --new-window` ⇒ 真开出**第二扇**、里面恰好一个缺省本地标签（不是两格）、转发端退 0、首实例那一扇不受扰动；② `--new-window --profile <远程档案>` ⇒ 新窗的首标签就是那条档案的连接，若需要口令则**询问框弹在新窗上**（D6 的落点口径，且凭据续跑那条腿是经 tracker 现取落点的——只有真机能证它没开回旧窗）；③ `--new-window --cwd <目录>` ⇒ 新窗那一格的起始目录是那个目录；④ 混版两档：旧 CLI（只写旗标）打新实例 ⇒ 同样开新窗；新 CLI 打旧实例 ⇒ 整帧拒、退 2、首实例不崩且不落半格；⑤ 冷启动 `Borealis --new-window` ⇒ **只有一扇窗**（那一扇就是新窗，不许因为旗标多建一格）；⑥ 新窗按自己那一格恢复几何（F27），挪位重启看放不放得回去；⑦ 请求被拒那一档（如 `--new-window --cwd <非目录>`）退 2 且**桌面上不留一扇空的新窗**——本棒把开窗排在验证之后，这条只有真机看得见。
6. 面板在两窗间「搬过去」的观感（D6 代价档）与凭据询问弹框的所属窗。
7. Windows 主通道（MSVC + Win32 后端）的多窗与几何恢复；本沙箱只跑 POSIX 无头腿。
8. 关窗确认（2026-10-23 随 G44 接货，`WS03_WINDOW_CLOSE_CONFIRM.draft.md`）：① 某窗还有活会话时点 × ⇒ 确认框弹在**该窗**、取消后窗与全部会话仍在且能继续输入；② 确认后会话终止、窗被回收、`on_window_closed` 撤账无残留；③ 无活会话时不弹框、直接关；④ 程序化关窗（用户确认那一次）不再二次弹框；⑤ 两扇窗同时各弹一只时两框互不覆盖（按窗一份的判据）。

## 10 边界（登记在册，不在本棒解决）

`main.cpp` 不进 runner ⇒ 批 2/批 4/批 5 的装配腿无自动化证人（裁决 7.83）；D4 的关闭否决已随 G44 回货落地（2026-10-23，`WS03_WINDOW_CLOSE_CONFIRM.draft.md`），装配闭包本身仍无自动化证人；G42（§3）**部分回货**——`active_window()` 不记宿主级指针/触摸，本仓 tracker 代偿保留（缺陷见 `FRAMEWORK_TASK_WS03_G42.md` §7）；D6 的面板单例意味着「两窗不能同时各开一块 SFTP」；几何不进设置导出件（D9 是特性）；混版 CLI 的 `--new-window` 分两档（批 6 落定，见 §22⑵）：**旧 CLI + 新实例**＝那面旧 CLI 只写旗标，新实例按同一个含义接受，功能正常；**新 CLI + 旧实例**＝旧版解码器不认识 action 值 2 ⇒ 整帧 fail-closed 拒、转发端退 2（这是 D10 原记代价的确切形态，比「旗标不生效」更硬：它不是静默忽略而是明确报错，且拒的是**整条请求**而不是那枚旗标）；WS.11 会话恢复**不**在本条做（多窗之后那一件的形状是「每窗的标签与分屏快照」，另棒裁决）。

**批 4-C-2 之后新增两档**（同属「无头面物理上不可证」那一类，不是本棒偷懒）：① 退出序整条腿要 `run()` 返回，而全为无头宿主时框架的 `should_exit()` 永假（F21），itest 里没有「跑完整条退出序」这一步可走的位置；② 兜底腿在正常路径上不可达——它扫的是「表里有格而不属于任何活着窗」，那种形态只在账目分叉（真 bug）时出现，用例面造不出一个「已经散掉而 `owner` 仍指着它」的窗还能让本层活着走到退出。⇒ 两条都落进 §9 第 3 条走查，本稿不算案数、也不算转红。

**观察项（F22 补记）**：宿主销毁路径上 `Dialog`／`Popup` 的 `set_on_close` **不会被调用**，框架也不提供销毁前通知。本仓现有四处用它（`DebugPanel`、`StartupNotice`、`SearchOverlay`、`SettingsPanel` 那条只是注释）都只清自己的内部状态、随控件一起析构，因此「没回调」不产生悬垂，只产生销毁链上一度不一致。**若日后有件要靠它做外部副作用（摘全局表、归还焦点、落盘），那条会在关窗路径上静默丢失** ⇒ 批 4-B 的撤账只能挂在装配层自己的 `retire_window` 里显式做，不许指望控件的 `on_close`。（4-B 落的是 `set_on_window_closed` 而不是 `retire_window` 这个名字，链与这一句的口径一致，见 §6 批 4-B 改口与 §18⑴。）

**观察项（批 4-B 补记）**：主窗散后，装配层那枚按值留存的 `shared_ptr<au::OverlayHost>`（§17⑷）会把它的浮层栈——连同挂在上面的面板与关闭确认框——续命到进程结束。形态是「看不见但不是野指针」：宿主已散、那一扇再不会被渲染，控件随 `main()` 一起析构。可见的那一半（面板「搬窗」到落点窗）属 D6＝4-C，本棒不动。

**观察项（批 4-C-1 补记）**，四条登记在册、本棒不解决：
1. **撤账腿 ③′ 与三枚「宿主＋序号」的重挂本身无证人**：判据全在控件侧 setter，`main.cpp` 那一段（落点窗怎么挑、十二件的调用次序）不进 runner（裁决 7.83），只能源码审查＋§9 第 6 条走查。
2. **「取不到落点窗即原地不动」那一档在生产路径上不可达**：`landing_window == nullptr` 只在「本层账目里除这一扇外一扇都不剩」时成立，而关最后一扇时框架保留主宿主、`on_window_closed` 根本不触发（F19，§18⑹）。代码仍按 fail-closed 写，是因为它不该依赖框架这一条没写在公共契约上的行为。
3. **序号是易主的**：`remove_overlay` 按当前子节点表 `erase`，摘掉一层会让它后面所有序号整体前移（框架 `popup.h` 自述）。故 `MountedOverlay` 在每次重挂后**必须重取**序号，而瞬态面板的 setter 走的是「先按旧序号摘、再记新序号」那一条腿——次序反过来即摘掉别人的那一层。同一宿主上两枚都能摘挂的浮层（如命令面板与波特率框）因此是**彼此序号的读者**，本棒靠「各件只记自己那枚、重挂即重取」把它约束住；若日后加入别处也摘挂的件，这条约束得由框架侧的稳定句柄解决（按 §5.1 分流，本棒不提任务书）。
4. **向导的落点按 tracker 而不是「点它的那一扇」**：`ConnectionWizard` 由侧栏点击唤起，而侧栏那一条腿没有上报点（稿 D7 的三处上报点不含侧栏），故「在 B 窗点侧栏档案、向导开在最近被碰过的那一扇（可能是 A）」这一档仍在。它是 D6「落点＝发起命令的那一扇」口径的一个已知缺口，不是本棒新引入的形态；补齐要么给侧栏加第四处上报点，要么让向导吃打开它的那枚宿主指针——两者都要另立裁决，本棒不顺手做。

**观察项（批 5 补记）**，四条登记在册、本棒不解决：
1. **两处 `flush()` 是无条件提交**：`Preferences` 没有「自上次提交后是否变过」的查询（F26⑥ 实测），所以哪怕一次几何都没写，撤账钩子那一跳与退出那一跳照样各提交一遍。**「首次启动会不会因此产出一份只含 `{}` 的文件」本棒未实测**——真机读数取到的是反的那一档：`XDG_CONFIG_HOME` 指向隔离目录跑 8 s 后由 `timeout` 收走（§21 读数段），那两处 flush 都排在正常退出链上，被信号杀掉时一轮都不走，目录里一个文件都没有。⇒ 「配置目录里出现了 `windows.json`」不能读成「几何已经记住了」，它只说明有一次 flush 真的走完；真机上第一次正常退出（点关闭钮）之后取一次目录清单，才是这条的读数位置（§9 第 3、4 条）。
2. **旧键不清理**：键按创建序**只增不复用**（§21⑷），于是「开三扇、关掉后两扇、再开一扇」会把键推到 `window-4`，而 `window-3` 那一格的几何留在文件里再没人读。这是「两扇共写同一格」那个更坏的形态的对价——留着不动是最便宜的处置，清理则要引入「哪些键算活着」这件事实，本棒不做。
3. **接线的先后不构成判据**：原以为「`set_window_geometry_store` 必须排在第二扇登记之前」是一条要证的时序，注入证明它**等价**（§21⑸ 末则「等价注入」）——那一枚 setter 对**已登记**的全部宿主补做一次恢复，故排在开第二扇之前与之后读数相同。生产里那两跳真正要的次序只有一条：**排在 `run()` 之前**（D13 的首帧之前），而 `main.cpp` 不进 runner，那一条只能走查。
4. **恢复不回最大化态**（框架侧消费点缺失，非本仓缺陷）：`WindowGeometry::mode` 被序列化、被存回，但 `apply_host_geometry` 只读尺寸与位置 ⇒ 「最大化那一种重启」回不来。本仓按 AGENTS §5.1／§4.1 第 3 条不在应用侧补偿（那要在装配层私调 `set_maximized` 并猜框架的时序），已在 §3 登记为候选缺口、在 §9 第 4 条列为真机读数。

**观察项（批 6 补记）**，四条登记在册、本棒不解决：
1. **开窗那一跳本身无自动化证人**：`apply_startup_request` 里「先验证再 `open_new_window()`、成功后向 tracker 上报该窗号」全在 `main.cpp`（裁决 7.83）。本棒拿到的最强证据是产品侧**退出码**——转交退 0 只有在窗真开成并登记进 `workspace_windows` 之后才会发生（取不到落点即回一条 ASCII 诊断并退 2），故它证到「那条腿没空转」，证不到「窗里那一格长什么样、开在哪一扇」。⇒ 真机读数在 §9 第 5 条①②③⑦。
2. **「`split-pane` ＋旗标」这一格只有手工帧能造**：解析器两侧都拦（`--new-window` 与两个子命令互斥），故解码侧那条拒绝分支在生产上没有入口，用例是从手工字节帧打的（§22⑵）。它在真机上只可能来自版本错配或第三方实现，本棒按 fail-closed 写而不猜语义。
3. **flags bit0 从此永久占位**：旧版 CLI 会往那一bit 写「要新窗」，接收侧必须一直认它 ⇒ 该位不能再挪作它用，线格式里真正「保留」的只剩 offset 7 那一字节。日后要加第二枚旗标须同时升 version 值，否则新旧两侧会各自误读同一帧。
4. **冷启动那一侧它没有独立语义**：`NewWindow` 在冷启动折进与缺省 `new-tab` 同一条腿（正要建的这一扇就是那扇新窗），故「冷启动＋旗标」与「冷启动＋缺省」在真机上应当**完全同形**。若哪天要「启动即开两扇」，那是新判据而不是这条的漏项（§9 第 5 条⑤钉的是「不许多建一格」）。

## 11 批 1-A 实测台账（`itest_workspace_window`，2026-10-21）

**读数**：`cmake --build --preset linux` 通过且无警告；`ctest --preset linux -E etest_` **96/96**（注册 99 含 3 条 etest，9.22 s）、`borealis_test_runner` 全跑 **1212 案**全绿（21.1 s；v0.125 基线 95 项／1207 案 ⇒ 本件 **+1 套件／+5 案**，五案全长在新增 TU 内，既有 1207 案零改动照绿——生产 `src/main.cpp` 本批未动，这正是「拆分前行为不变」的那一侧证据）。

**harness 形态**：一支 `au::Application` + 两扇 `AURORA_BACKEND_HEADLESS` 窗，每扇一份 `WindowFace`（`HeadlessOptions` → `create_window` → `au::Window`，根＝`OverlayHost`→`Column`→`TabBarWidget`，与 `main.cpp` 的 `chrome`→`host`→`root_column` 同形）。三处承重：⑴ 成员声明序让 `app_` **先析构**，宿主连同它接管的 `Scene` 一起散，`WindowFace` 后散——反序时宿主场景根指向已消散的控件树；⑵ 逐窗 present 走 `WindowHost::render_frame()`，**不碰**应用级 `scene()`；⑶ 每条「另一扇整帧不动」的判据都先要求两幅内容相同的画布**逐位相同**，这条前置排掉「第二扇根本没在画自己的根」与「两扇共用一支真值源」两种缺陷——否则那句「B 不动」会退化成恒真空转。

**五条用例 ↔ §5 不变量**：注册与两扇都渲染（建窗）／加一格只动本窗画布且两份标签计数分离（泵＋真值源）／栏内实点只改收事件那扇的选中且另一扇逐位不动（唤醒）／浮层只遮挂载那扇（拆的前置：overlay 按窗挂）／应用级 `scene()`/`focus()`/`window()` 恒指主宿主且两宿主焦点管理器互不相同（**批 2 的开工依据**：装配层现有四处 `app.focus()` 与那条捕唯一 surface 的唤醒腿，在第二扇窗里指的仍是主窗，F3／F11 必须在自动化面现形，不许留给真机走查）。

**变异自证**（§4.4 第 22 条，每条只注一处、跑完即还原，生产码零改动）：

| 注入 | 指名红案 |
|---|---|
| `render(face)` 恒泵主宿主 | **5/5 红**——构造期第二次 `render(1U)` 没给 B 的缓冲 present ⇒ `preflight()` 里那幅空画布把五条全带倒，「第二扇压根没在画」这条判据覆盖全条 |
| `face_ref(face)` 恒指 A（两扇共用一支 `TabStrip`） | 3 红（加标签／点击／浮层），注册案与访问器案照绿——宿主确实还是两扇，串的是真值源，红得对得上口径 |
| `click(face,…)` 派发到另一扇宿主 | 1 红（`a_click_selects_only_in_the_window_that_received_it`） |
| 两扇共用一支 `OverlayHost` | 3 红（加标签／点击／浮层）——共用根之后两幅画布本就同源，前置那句 `identical` 也一并红，符合预期 |

无「注入后全绿」的空转档。**注**：首版第四则写成类内 `static std::shared_ptr<…> host_ = make_shared<…>()`，编译失败（非字面类型不可类内初始化静态成员），那次跑的是上一则的旧二进制，**不算读数**；改写成 lambda 内函数级静态后重编重跑，得上表那一行。

**已知噪声（如实记，不判为本件缺陷）**：本套件退出时 `OverlayHost` / `TabBarWidget` 那条 `layout parent detached while the child widget is still alive` WARN 会刷约 20 行。按套件计数比对既有通道：`itest_tab_bar`／`itest_startup_notice`／`itest_command_palette`／`itest_debug_panel` **0 条**，`itest_search_overlay` **34 条**、`itest_settings_panel` **18 条**——形态与本件相同（浮层宿主活得比被渲染的树久），故它是本仓无头 harness 的既有特征，不是批 1-A 引入的缺陷；但它意味着**无头用例不能判「析构次序本身」那一档**，拆窗时的真实析构序仍归 §9 第 3 条真机走查。

## 12 批 1-B 台账（`WorkspaceWindow` 归属与 vector 持有，2026-10-21）

**落到的形状**：`src/main.cpp` 匿名命名空间里新增 `WorkspaceWindow`（§2 那份「每窗一份」清单：`options`/`window`/`surface`/`id`/`overlay_host`/`root_column`/`tab_bar`/`banner`/`tab_strip`/`closed_tab_stack`/`current_tab_id`）与 `open_workspace_window(std::string title)`——工厂只造**地基半区**（后端窗 + 浮层宿主 + 撑满窗宽的根 Column），建窗失败即回空指针并把后端错误原文落日志，调用方不接宿主（§5「建窗」不变量）。`main()` 里那十处散装局部（原 598-607／622-623／655／681／1172／1176／1276）改为**同一批对象的别名**，持有容器是 `std::vector<std::unique_ptr<WorkspaceWindow>>`：按值 vector 一次扩容就把对象搬走，而闭包按引用吃成员，悬垂是当场的事。`au::Application` 那一跳改用 `first_window.take_window()` 交所有权，随后 `first_window.id = app.main_window()` 回填（框架 `next_id_` 实测从 1 起、0 留作无效值，故未登记的窗取宿主必空）。

**本批没做的那一截（批 2 的入口）**：标签栏的 hooks 要摸二十来条装配闭包、横幅的 `exit_all` 要摸广播组本体，两处都还在应用级作用域里，故 `tab_bar`/`banner` 由装配层在原处填进成员，而不是工厂造出来。别名过渡是刻意留下的形状：它让「归属换了、闭包还没逐窗化」这件事在源码面上看得见，批 2 每接一条就少一支别名。

**读数**：`cmake --build --preset linux` 通过且无警告；`ctest --preset linux -E etest_` **96/96**、runner 全跑 **1212 案**全绿——与批 1-A 之后的门（96 项／1212 案）**逐字相同**，本件零新增用例、零用例改动，正是「拆分不改变行为」那条判据的读数面。产品侧只跑了不触达真打开腿的一档：`Borealis --help` 退出码 0 且输出纯 ASCII（`--help` 在建窗、读盘、认领通道之前就地消化，`SPEC.FEAT.INTEG.04`）。

**析构次序这一档怎么过的**：把 `root_column` 挪进「比 `tab_workspaces` 更晚析构」的位置，看着像在违反 `SPEC.FEAT.WS.02` 批 2 那条「会话容器须活得比视口久」。实测口径是：容器多持一份只让当前那格的视口**散得更晚**，而每一格的视口与其会话的相对次序由 `TabWorkspace` 自己的成员次序保证（会话容器声明在 `workspace` 之前），不会被外层容器的次序翻转；会翻车的是 `perform_close` 那种**中途**撤账的路径，那边早已按「先摘根再 erase」写过。批 4「退出序遍历全部窗」要把这一段显式化，而不是继续依赖声明序。

**边界**：本批「同一条构造路径」只有主窗走过一次——生产仍只开一扇，`open_workspace_window()` 的第二调用点要到批 3 的 `workspace.new_window` 才出现，故**工厂本身的正确性只由主窗那一次运行与源码审查担保**（裁决 7.83）；真机走查未做，§9 全部条目照欠，本批不称「可用」，也不称批 1 收口。

## 13 批 2 台账（装配层逐窗接线，2026-10-21）

**落到的形状**（`src/main.cpp` 一处文件，102 增／46 删）：⑴ `TabWorkspace` 首字段加 `WorkspaceWindow *owner`，两处建表点（`create_new_tab_with`、撤销重开）各自填属主窗；存**指针**而不是 `WindowId`——首标签必须在 `au::Application` 登记之前建好（根 Column 先持着它才进得了场景根），那一刻号还不存在，而 `unique_ptr` 的 vector 保证堆上对象地址在窗的生命周期内不动，要号时读 `owner->id`。⑵ `build_pane_view` 加 `WorkspaceWindow &window` 形参：会话唤醒腿从「捕唯一 `surface` 引用」改成「捕属主窗、现读 `window.surface`」，`wire_view` 加同形参，把浮层宿主挂到 `*window.overlay_host`、把搜索依赖的焦点管理器换成 `window_focus(window)`（按 `app.window_host(id)` 取，取不到回主窗那一份＝兜底而非猜测）。三条建视口路径（新开格／切分／重启换会话）与首标签补挂循环同源走这一处，构造点不再各写一遍。⑶ `set_on_frame` 的帧泵循环首行取 `WorkspaceWindow &owner = *workspace.owner`，BEL／活动／OSC 标题／退出角标四条写给属主标签条；窗口标题段改为遍历 `workspace_windows`，每窗读自己的 `current_tab_id` 与 `tab_strip`、按 `window_host(id)->window()` 下发、差分账是每窗一份 `WorkspaceWindow::last_title`（原全局 `last_window_title` 已删）；回调捕获表随之从 `&current_tab_id`/`&tab_strip`/`&last_window_title` 换成 `&workspace_windows`。⑷ 切分新格继承本标签属主窗（`hooks.make_pane` 里读 `found->second.owner`）是刻意不跨窗：切出去的那一格与它背后的会话仍在同一扇窗，「切到别的窗」是另一件尚未裁决的事。

**本批没做的那一截（不是漏，是边界）**：§6 批 2 原文写「四处 `app.focus()` 改按窗取」，实测口径是**视口那一处**按窗取了，`DebugPanel`／`StartupNotice`／`SettingsPanel` 三处仍捕 `app.focus()` 与主窗 `*host`——它们构造一次、活在应用级作用域，按窗取的前提是「面板随窗搬」（D6）或「每窗一份面板」，落点在批 4；`bind_shortcuts` 那条链吃的是应用级 `ShortcutRegistry`（F2 判给共享），本就不需要按窗取，故四处收敛为「一处已改 + 两处留批 4 + 一处不适用」。同理，布局重建（`rebuild_root_children`）与撤销重开的落点窗仍吃主窗别名，各带 `TODO(SPEC.FEAT.WS.03)` 在册。§7 那条「逐窗标题差分写回」判据此刻**还无可测落点**：这一段整段在 `main.cpp` 的帧回调里、不进 runner，要变成自动化判据得先把「本窗选中格的显示名 → 标题串」抽成纯件——本批不抽（§4.1 第 3 条：不加将来才用的抽象），归批 3 动命令落点时一并处理，此前由 §9 第 1/2 条真机走查担保。

**读数**：`cmake --build --preset linux` 通过且无 error/warning；`ctest --preset linux -E etest_` **96/96**（9.24 s）、`borealis_test_runner` 全跑 **1212 案**全绿（21.0 s）——与批 1-B 的门**逐字相同**，本件零新增用例、零用例改动；产品侧仍只跑不触达真打开腿的那一档（`Borealis --help` 退出码 0、输出纯 ASCII）。

**担保档位（如实记）**：`main.cpp` 不进 runner ⇒ 本批**没有可注入的自动化证人**（裁决 7.83、§10 第一条）。这同时意味着 §7 变异表里与装配层同名的那几则（`frame_wake` 全部捕主窗 surface、`current_tab_id` 回全局一份、`ClosedTabStack` 回全局）现在**注入也不会转红**——它们要等批 3 的 `utest_active_window_tracker` 与批 4 的 `itest_workspace_window` 扩案成为判据之后才是可注入证人，本批不冒充读数。本批的担保只有两条：门不变（行为不变的读数面）+ 源码审查（按 owner 取的那几处逐条对照 §2 那份「每窗一份」清单）。

**边界**：生产仍只开一扇窗，第二调用点在批 3；两扇窗并存时的真实行为（键入落活动窗、逐窗标题、关窗撤账）全部未验，§9 七条照欠，本批不称「可用」。

## 14 批 3-A 台账（`ui::ActiveWindowTracker` 纯逻辑件与 `utest`，2026-10-21）

**落到的形状**：新增 `src/ui/active_window_tracker.{h,cpp}`（私有头，理由同裁决 D1①：声明里吃 `au::WindowId` 与 `AURORA_INVALID_WINDOW_ID` 两枚框架标识）与 `tests/unit/utest_active_window_tracker.cpp`。三格状态：构造期传入的兜底落点 `main_window_`、`optional<WindowId> last_reported_`、以及 `forget()` 的撤账。四条判据的形态口径：①无记录回主窗＝**兜底而非猜测**（命令永远有一个明确落点，不是「取不到就谁也不做」）；②最近者胜，A→B→A 之后落点是 A——本件**不做计数也不做栈**，故「撤掉当前落点」之后回的是主窗而不是「上一个上报过的窗」，这一档在 §7 原文里只写了「窗关闭后该号不再被返回」，实现选前者，理由就是 D7 那句「落点要的是最近谁被碰过」；③无效号（0＝那扇窗还没登记进宿主）不改动任何记录，宁可留着上一个真实落点；④兜底落点自己被撤即置**无效号**，由调用方经 `window_host()` 取空后按「无落点」处理——「主窗易主后谁接任兜底」是关窗那一棒的账目（§6 批 4），本件不自己挑，也**不**为此预留一个现在没人调的 setter（§4.1 第 3 条）。

**装配面**：`src/CMakeLists.txt` 的 `borealis_core` 源表加一行——库侧是**显式清单**，AGENTS §7 那条「新增 `.cpp` 经 `CONFIGURE_DEPENDS` GLOB 自动进构建」只覆盖 `tests/`，新私有件不登记就只有一处链接错误（本棒首跑即撞上）。生产侧 `src/main.cpp` 本批**一行未动**：上报点（视口/标签条/横幅各一次 Press 或按键）与「命令按 tracker 解析目标窗」在批 3 的后续笔，故这枚件此刻是**已测而未用**，形态与批 1-A 那笔「先跑通 harness 再动生产」同一档选择。

**读数**：`cmake --build --preset linux` 通过且无警告；`ctest --preset linux -E etest_` **97/97**、runner 全跑 **1217 案**全绿。门迁移形态：**96 项／1212 案 → 97 项／1217 案（+新套件 `utest_active_window_tracker` 5 案，−历史排除 0）**，既有 1212 案零改动照绿。三处文档（`PLAN.md`／`SPECIFICATIONS.md`／`ARCHITECTURE.md`）的同口径计数按 §6 批 7 收口时一次性改，登记为在册欠项。

**变异自证**（§4.4 第 22 条，每条只注 `active_window_tracker.cpp` 一处、跑完即还原，生产码零改动）：

| 注入 | 指名红案 |
|---|---|
| `report` 只记首次（不覆盖） | 2 红：`the_last_window_that_received_a_dispatch_wins`／`a_closed_window_is_never_returned_again` |
| `active()` 兜底回无效号而非主窗 | 3 红：`without_a_single_report_…`／`an_unregistered_window_id_changes_nothing`／`a_closed_window_is_never_returned_again` |
| `report` 撤掉无效号那一条拦截 | 1 红：`an_unregistered_window_id_changes_nothing` |
| `forget` 整条空实现 | 2 红：`without_a_single_report_…`（撤主窗后仍回主窗）／`a_closed_window_is_never_returned_again` |

无「注入后全绿」的空转档，也无编译失败的不算读数项（四则均一次编过）。首轮用例本身有一处把「回主窗」写成了「回上一个上报过的窗」，跑测现形后按实现的形态口径改正用例并在上面②登记——不是把实现改去迁就用例。

**边界**：本件与生产派发事实之间还没有接线，故「键入落活动窗」那一档在本批既没有证人也没有读数（属批 3 后续笔与 §9 第 2 条走查）；真机走查全部照欠，不称「可用」。

## 15 批 3-B 台账（按窗装配复用与第十五保留键，2026-10-21）

**落到的形状**（`src/main.cpp` 263 增／155 删，另九件小改）：

⑴ **主窗那三段收成按窗可复用的五支闭包**：`create_new_tab(window, cwd)`（原第一参是隐含主窗，现按窗取当前号）、`ask_close_confirm(window, raw_id)`、`make_tab_bar(window)`（六条 hooks 逐条吃 `window.tab_strip` / `window.current_tab_id` / `rebuild_root_children(window, …)` / `perform_close(window, …)`，变更钩子装在本窗 `TabStrip` 上）、`assemble_window_content(window)`（标签栏 → 横幅 → 根 Column 入浮层宿主 → 布局重建，次序与主窗原先那段逐字相同）、`make_window_scene(window)`。顺带删掉 `std::shared_ptr<au::Column> &root_column` 那枚主窗别名（留着它就是第二处真值源），`app.focus()` 别名 `host` 仍保留给面板与 `StartupNotice`（§13 登记的批 4 欠项）。键盘焦点恢复抽成 `restore_window_keyboard_focus(window)`，横幅工厂的显式捕获表随之加这一支。

⑵ **关闭确认按窗一份**：`WorkspaceWindow` 加 `close_confirm` 与 `close_confirm_mounted` 两枚成员，工厂惰性建 Dialog、**只挂一次** `window.overlay_host->add_overlay(...)`、每次打开只换 `set_content`。浮层宿主是每窗一份，挂两遍就是把同一枚 Dialog 压进同一棵浮层栈两次——答一次话只剩一层还在（本仓「按窗取宿主」的既有形态，§13⑵）。

⑶ **第十五条保留键 `Ctrl+Shift+N`**：命令 `WorkspaceCommand::NewWindow` 走的仍是焦点 pane 的前置过滤通道，因此同步处一次列全——枚举与 `workspace_keys.h` 文件头说明、`kBindings` 一行、`WorkspaceView::Hooks::open_new_window`、`run_command` 分支、`reshapes_layout` 把它归**不还原缩放**那一组（它不动本工作区拓扑，缩放态下按它也应立刻开窗）、`WorkspaceKeyBinding` 文档「十四→十五」、`utest_workspace_keys` 的 `kClaimed`/`forwarded_keys`（含 `Ctrl+N` 与 `Shift+Alt+N` 两条反例）/命中断言/非步进列表与用例名 `…_fifteen_keys`、`itest_workspace_layout` 新案、`shortcuts_table.h` 与 `itest_settings_panel.cpp` 的「十四条保留位」措辞、`settings_i18n.cpp` 词条 `workspace.action.new_window`、命令登记。**面板与快捷键表不需要额外接线**：`workspace_key_bindings()` 是那张保留位表的唯一真值源，第十五条进表即进表。

⑷ **命令 `workspace.new_window` 只登记命令、不登记 `default_binding`**（承重决策，别写成疏漏）：`Ctrl+Shift+N` 已被分屏前置过滤消费，快捷键层再绑一次就是一次按键开两扇窗；而框架 `ShortcutBinding::action` 是无参闭包、不带派发来源，连「哪一扇窗按的」都判不出来（稿 §0 的 F2 同源事实）。面板那一条给的是入口，键位只在保留位表里有一份。

⑸ **开窗腿三段次序全部承重**（`open_new_window_request`）：`open_workspace_window()` → `assemble_window_content()` → `app.open_window(window.take_window(), make_window_scene(window), window.options)` → `push_back` → `create_new_tab(window, std::nullopt)` → `rebuild_root_children(window, …)`。① 先入根再折场景（`Scene` 收所有权，根 Column 必须已在树里）；② 号要等 `open_window()` 返回才有效，而首标签那只视口经 `window_focus(window)` 按号取焦点管理器，建标签排在登记之前就会把键入打进主窗那一份（F3）；③ `push_back` 搬走的是 `unique_ptr` 而不是对象，堆上地址不动，故上面那串按引用捕 `window` 是合法的。建窗失败即 `return`，不入容器不登记宿主，§5 那条「没建成窗就不留半个账目」在第二扇窗这条路上同样成立。新窗首标签**不继承发起窗的 OSC 7**：此刻本窗没有当前号，`create_new_tab` 的继承腿自动落配置的启动目录那一档（D1），「新窗跟旧窗的目录走」尚未裁决、不在此猜。

⑹ **视口两条构造点收敛为 `wire_workspace_hooks(id, hooks)`**（= `make_pane_hooks` + `wire_broadcast_hooks` + 新开窗钩子）：新开标签与撤销重开原先各写一遍，第十五键这条「每格都要有、又不属于任何一格」的接缝若继续两处各装，漏一处就是同形两格异味；沿用 `SPEC.FEAT.WS.02` 稿 §3 的「靠一处装钩而不是靠人记得」纪律。

**读数**：`cmake --build --preset linux` 通过且无 error/warning；`ctest --preset linux -E etest_` **97/97**（9.51 s）、runner 全跑 **1218/1218**（21.09 s）。门迁移形态：**97 项／1217 案 → 97 项／1218 案（+`itest_workspace_layout` 新案 1，−历史排除 0）**，套件数不变（本批未新增 TU），既有 1217 案零改动照绿。三处文档（`PLAN.md`／`SPECIFICATIONS.md`／`ARCHITECTURE.md`）的同口径计数照例归 §6 批 7 一次性改，登记为在册欠项。产品侧仍只跑不触达真打开腿的那一档：`Borealis --help` 退出码 0、470 字节纯 ASCII。

**变异自证**（§4.4 第 22 条；每条只注一处、跑完即还原，还原后复跑两道门取上面的读数）：

| 注入 | 指名红案 |
|---|---|
| M1 `kBindings` 删掉 `N` 那一行 | 5 红：utest `pane_commands_match_their_documented_bindings`／`the_claimed_set_is_exactly_those_fifteen_keys`／`num_lock_state_changes_no_verdict`／`listed_bindings_are_exactly_the_claimed_keys` + itest `ctrl_shift_n_is_claimed_and_only_handed_to_the_assembly_layer` |
| M2 该行的 Shift 位改假（即绑成 `Ctrl+N`） | 与 M1 **逐字相同的 5 红**——差别在失败断言所在的一半：M1 栽在认领集循环缺一条，M2 栽在 `forwarded_keys` 里的 `Ctrl+N` 被误吞。两注名单相同故**不可靠案名区分注入形态**，如实记在此 |
| M3 `run_command` 里钩子不调（`if` 体清空） | 1 红：`ctrl_shift_n_is_claimed_and_only_handed_to_the_assembly_layer`；同套件其余 27 案全绿 |
| M4 钩子调两次 | 同 M3：**1 红、27 绿**（新案的「恰转交一次」断言是这一档的唯一证人） |

无空转档（四则各有 ≥1 红）、无编译失败的不算读数项（M2 的注入形态把「少一位修饰」变成误吞会话键，正是 `one_extra_modifier_is_not_a_hit` 那条口径的反面证人）。

**担保档位（如实记）**：`main.cpp` 不进 runner ⇒ ⑴⑵⑸⑹ 四段**没有自动化证人**（裁决 7.83、§10 第一条），只有源码审查；本批唯一的证人是「键被认领 + 恰转交一次 + 不动本工作区拓扑」这一接缝（M3/M4 只打得中它，正说明装配层那半区在自动化之外）。§7 变异表里与装配层同名的那几则（`frame_wake` 全捕主窗 surface、`current_tab_id` 回全局、`ClosedTabStack` 回全局）此刻注入**仍不转红**，要等批 4 的 `itest_workspace_window` 扩案成为判据；本批不冒充读数。

**边界**：生产侧第一次能开出第二扇窗，但两扇窗并存的四类行为分别落在后续笔且全部未验——键入落活动窗与五类命令闭包按窗解析（批 3-C）、关窗撤账与逐窗横幅／撤销栈（批 4，`main.cpp` 三处 `TODO(SPEC.FEAT.WS.03)` 在册：唤醒腿随窗撤账、面板搬窗 D6、撤销回到关它的窗）、几何各自记忆（批 5）、CLI `--new-window`（批 6）。§9 七条真机走查照欠，本批不称「可用」。

## 16 批 3-C 台账（三处上报点接线与命令落点按窗解析，2026-10-21）

**落到的形状**（`src/main.cpp` 134 增／36 删；控件侧三枚新接缝各一对头／源，共六件文件小改；三套测试扩案）：

⑴ **上报点的时机口径：一律先于本层的判定**。三处各一枚无参闭包，装配层把它们折成同一个「本窗号」：
- `TerminalView::set_dispatch_observer`（新接缝）在 `on_key_event` 的 Down 守卫之后、`to_key_press` 与
  键盘前置过滤**之前**调一次，在 `on_pointer_event` 的第一行按 `Press` 调一次。装在视口而不是
  `WorkspaceView` 是因为后者收不到 pane 内的点击（命中链里根本没有本层，稿 §6 批 3 的那条实测事实），
  而放在前置过滤之前才让「被十五条认领」与「转发进会话」两条腿都算碰过——第十五键那一击既不转发也
  不分格，上报若挂在转发分支之后就永远不响。
- `TabBarHooks::note_press` 在 `on_pointer_event` 的 `Press` 分支里先于 `hit_test_at`，故命中栏位、"＋"
  还是空白栏带都算一次触碰。
- `BroadcastBannerHooks::note_press` 先于「是否在广播态」的判定：事实与动作分成两件事，清组那条仍受状态
  限制，二者不互相担保。

⑵ **装配层的接线次序承重**：`std::function<void(WorkspaceWindow &)> report_dispatch;` 先声明（tracker
要等宿主登记才有主窗号），`make_tab_bar`／`make_broadcast_banner`／`wire_view` 三处工厂**按引用**捕它，
`active_window_tracker{first_window.id}` 与 `report_dispatch = …` 的赋值排在 `window_focus` 之后、
`wire_view` 装钩之前——点击那一刻读到的永远是最新那一份（与 §15⑸ 的开窗腿同一形态）。`wire_view` 同时
补 `set_dispatch_observer` 与 `set_restart_hook`（后者由「隐含主窗」改成捕本窗，dead-session 浮层那枚
「重启」按钮从此知道自己长在哪个视口里）。

⑶ **五类闭包按窗解析，兜底统一**：`search.open`／`session.restart`／`serial.set_baud`／
`workspace.toggle_fullscreen`／`tab.undo_close` 都写成 `target_window()` 取窗，取不到（只剩「主窗自己
已散」这一种形态）即回主窗那枚别名／主窗对象，而不是「什么都不做」。刻意**不**改成「回退到最后一扇活着
的窗」：那等于替用户挑一扇他没碰过的窗。`window_of` 既查 `workspace_windows` 也查
`app.window_host(id)`——号在容器里而宿主已散是关窗那一棒（批 4）的账目，此刻按空落点处理。

⑷ **撤销重开回到关它的那扇窗**（§9 走查清单第 3 条的前半、原 `TODO(SPEC.FEAT.WS.03)` 之一闭合）：
`tab.undo_close` 整体改读 `window.closed_tab_stack` / `window.tab_strip` / `window.current_tab_id`，
新格属主写 `tab.owner = &window`，入根走 `rebuild_root_children(window, …)`。随之 `closed_tab_stack`
那枚主窗别名没有读者，删（留着就是第二处真值源，同 `root_column` 的处理）。

⑸ **三处证人**（`main.cpp` 不进 runner，证人只能落在控件与 harness 侧，裁决 7.83）：
`itest_workspace_layout.the_viewport_reports_each_key_and_mouse_press_exactly_once`、
`itest_workspace_window.a_tab_bar_press_moves_the_active_window_record`、
`itest_workspace_broadcast.the_banner_reports_the_press_that_cleared_the_group`。其中第二条**第一版
落点取的是栏位正中，对 M3 是等价变异**（点中栏位时两种形态都报）；改成空白栏带后才有证人——这条
教训记在这里：上报点的判据要把落点选在「没有动作」的那一处，否则「挂在命中分支里」这种退化实现看不出来。
横幅那条另有一枚顺序证人：上报闭包里顺手抄下当时的组成员数，挂在 `exit_all` 之后就成 0。

**读数**：`cmake --build --preset linux` 通过且无 error/warning；`ctest --preset linux -E etest_` **97/97**
（9.71 s）、runner 全跑 **1221/1221**（21.58 s）。门迁移形态：**97 项／1218 案 → 97 项／1221 案
（+新案 3，−历史排除 0）**，套件数不变（本批未新增 TU），既有 1218 案零改动照绿——五类闭包的兜底链写
成全窗同形，正是「单窗行为逐字不变」这条读数的来路。三处文档的同口径计数照例归 §6 批 7 一次性改。

**变异自证**（§4.4 第 22 条；每条只注一处、跑完即还原，还原后复跑两道门取上面的读数）：

| 注入 | 指名红案 |
|---|---|
| M1 视口把上报挪到键盘前置过滤**之后**（只在转发腿报） | 1 红：`itest_workspace_layout.the_viewport_reports_each_key_and_mouse_press_exactly_once`（栽在认领腿那一段：`Ctrl+Alt+→` 期望 1 报、实为 0） |
| M2 视口 `on_pointer_event` 不再上报（只留按键那一路） | 与 M1 **同一案名**——差别只在失败断言落在指针段。两注红案名单相同故不可靠案名区分注入形态，如实记在此（同 §15 M1/M2 的那一档） |
| M3 `TabBarWidget` 把上报挪进命中栏位那一支 | 1 红：`itest_workspace_window.a_tab_bar_press_moves_the_active_window_record`；同套件其余 5 案全绿 |
| M4 横幅把上报挪到 `exit_all` 之后 | 1 红：`itest_workspace_broadcast.the_banner_reports_the_press_that_cleared_the_group`（顺序证人取到 0）；同套件其余 6 案全绿 |

无空转档（四则各有 1 红）。**未做的两则**（`target_window()` 恒回主窗、`undo_close` 仍读主窗别名）与
§7 变异表里那几条装配层同名项一样**注入仍不转红**——`main.cpp` 在自动化之外，本批不冒充读数。

**在册欠项（如实记，批 4 起接）**：① 仍读主窗别名的六处：`refresh_broadcast_ui` 里的 `tab_strip`
（逐窗横幅账目）、`hooks.broadcast`、串口建标签写 `serial_by_tab`、SFTP 三条 hooks、
`current_tab_session`＋`logging_toggle_action`、CLI `SplitPane` 那条腿（属批 6）；② `Ctrl+F` 在窗内仍取
1 号视图（逐格搜索属 `SPEC.FEAT.INTERACT.04`）；③ 串口波特率那只框的**浮层落点**仍挂主窗宿主（稿 D6
「面板搬窗」）；④ `ActiveWindowTracker::forget` 暂无调用方（关窗撤账属批 4）；⑤ OS 激活态与派发事实
可能不同（§9 第 2 条），无头面不可证。

**落点仍会答非所问的两档（本批刻意不补，理由记在此）**：
- **缩放角标与把手那一击不报**：`WorkspaceView::on_hit_test_chain` 把这两处认领在本层（pane 内的一击
  才交给视口），而本层没有上报点——于是「Alt+Tab 到 B 窗、第一下拖它的把手、再按 `Ctrl+F`」仍落 A 窗。
  稿 D7 只点名视口／标签条／横幅三处，补第四处（工作区层）要改 `WorkspaceView::Hooks` 与
  `wire_workspace_hooks` 的形参表（此刻它不认窗），属形态变更而不是接线，留给批 4 一并改；
- **开窗腿落成那一刻不报**：`Ctrl+Shift+N` 之后 tracker 仍停在发起窗（那一击是经发起窗的视口前置过滤
  进来的，它本身就是最近一次派发事实）。普通键入会立刻自愈——第一下打进新窗的视口就报了——只剩「新窗
  里第一件事就是走全局快捷键／面板命令」这一窄档。不在本批补的理由是**契约**：稿 D7 与本件头注都写着
  「只有真的收到过一次按键或指针 Press 才上报」，让装配层自己写一次落点就是把这一枚记录器从「事实」
  改成「事实＋推断」，要改得先改口径（须人裁决或与 OS 激活态那条一并处理）。两条都进了 §9 第 2 条的
  走查场景，真机读数回来再定。

**边界**：批 3 到此收口（新窗入口＋命令落点）；关窗撤账与逐窗横幅（批 4）、几何各自记忆（批 5）、CLI
`--new-window`（批 6）、文档回写与三处同口径计数（批 7）均未落。§9 七条真机走查照欠，本批不称「可用」。

## 17 批 4-A 台账（装配层主窗别名收口＋逐窗横幅，2026-10-21）

**落到的形状**（`src/main.cpp` 255 增／137 删；控件、纯逻辑件与三套测试一行未动）：

⑴ **两条现取腿取代五枚别名**：`fallback_window()` 每次重读容器首格（空即「没有兜底窗」），只给应用级面板的唤醒与浮层落点用；`command_window`（`std::function<WorkspaceWindow *()>`）＝`target_window()` 的转发，是命令、快捷键、档案连接与 CLI 转交的落点；`command_tab_id()` 在它之上再取落点窗自己那一格。`command_window` 与 `report_dispatch`／`restore_window_keyboard_focus` 同形态——**先声明、按引用捕、赋值排在 tracker 建钩之后**，于是更早建好的闭包在调用那一刻读到的永远是最新那一份。`surface`／`tab_strip`／`current_tab_id`／`banner`／`overlay_host` 五枚别名连同 `first_window` 引用全部删除；`workspace_windows.front()` 的现取只剩装配早期四处（建首标签、入根、场景、`Application` 构造与 `id` 登记），它们都在 `app` 建成之前，那一刻既不可能有第二格也不可能被 `erase`，故安全——这条理由写在代码注释里，不留给读者猜。

⑵ **逐窗横幅（稿 D5，与别名同一处代码故一并收）**：`refresh_broadcast_ui` 改成遍历 `workspace_windows`，成员归属只问 `TabWorkspace::owner`（批 2-A 那枚指针，本仓唯一真源）——不另立「这张标签属于谁」的账，也不去别窗的标签栏里找一个本不存在的号。两处**档位选择**在此登记：① `state.active` 取「本窗有成员」而不是「全组非空」，否则 B 窗会出现一条高着却空名单的横幅（眼见事实与画面自相矛盾）；② `session_count` 仍取全组去重数（D5 原文：批量操作是一件事，数字要能对上总数，别窗的成员在本法眼里是看得见的名字但列不出的一行）。取不到属主的成员在各窗都被跳过，这一支只在标签撤账时出现（4-B 的窗口期），按「本窗不列」处理而不是猜一扇。

⑶ **兜底档统一改口**：§16 在册欠项① 那六处（逐窗横幅、`hooks.broadcast`、串口建标签写 `serial_by_tab`、SFTP 三条钩子、`current_tab_session`＋`logging_toggle_action`、CLI `SplitPane`）在此闭合，另外把批 3-C 留的五类命令闭包里「取不到窗即回主窗别名／主窗对象／应用级主宿主」那三档兜底一并收掉——**4-B 之后别名指着的可能正是那一扇已经关掉的窗**，退回它不再是兜底而是读已销毁对象。现在的形态只剩一种：取不到落点窗 ⇒ 什么都不做（新开格与 CLI 转交那两条各留一条 ASCII 诊断日志／诊断串）。档案连接的四条腿（SSH／本地／串口／Telnet）从此经 `tab_host_or_none` 按 tracker 解析，「在 B 窗点档案、标签开进 A 窗」这一档消除。`hooks.broadcast` 是唯一**不按 tracker** 的那条：设置页是应用级单实例（D6），这一腿的语义是「刚落盘的配置即时生效」，生效面按窗遍历全部可见工作现场——把它解析成「最近被碰过的那一扇」就变成「在 B 窗改设置，A 窗还停在旧配色」，那是另一件没裁决的事。

⑷ **`host` 从引用别名改成按值留一份 `shared_ptr`**：四块面板与降级提示持的是 `au::OverlayHost &`（非拥有，`SettingsPanel`／`SftpPanel`／`DebugPanel`／`StartupNotice` 的头文件都是这个形态）。若装配层只留一枚指着容器元素的引用，4-B 把首格 `erase` 时面板就悬空；按值留副本则对象随 `main()` 一起活，最坏形态是「浮层挂在一扇已经关掉的窗上」（看不见，但不是野指针）。**可见的那一半没做**——面板「搬窗」而非「各一份」是 D6、落点在 4-C，届时这一支连 `hooks.preview_wake` 那条 `TODO(SPEC.FEAT.WS.03)` 一起改按发起窗取。

⑸ **串口连接表的号源**：`open_serial_tab` 此前在 `create_new_tab_with` 之后回读「当前标签号」再写 `serial_by_tab`，那是「落点窗那一刻的选中项」而不是这一格的身份——两者只在单窗下巧合相等。改成与 SSH／Telnet 两条腿同形的**本支预取号**（`next_tab_id` 在 `create_new_tab_with` 里 `++` 是本仓唯一取号点，两处之间不夹别的发号），于是「把串口绑定挂到别的标签上」只可能是真 bug 而不是一次串窗。

⑹ **`startup_wake`**：冷启动交出唤醒指针那一处改成现取首格（取不到即不写，原子量保持空），4-B 的撤账链负责在 `erase` 之后重指或清空。本棒不提前做。

**读数**：`cmake --build --preset linux` 通过且无 error／warning；`ctest --preset linux -E etest_` **97/97**（9.90 s）、runner 全跑 **1221/1221**（22.10 s）。门迁移形态：**97 项／1221 案 → 97 项／1221 案（+0 案／−0 案）**——本棒不新增 TU，既有 1221 案零改动照绿是「单窗行为逐字不变」那一侧的证据（同批 1-B 那一次的用法）；它**不**是本棒改动的证人：`main.cpp` 不进 runner（裁决 7.83），逐窗分流与兜底改口两处都没有自动化判据可读。三处文档的同口径计数照例归 §6 批 7 一次性改。

**变异自证**：本棒**无可转红的注入**，如实记不冒充读数——§7 变异表里落得下来的三条（`current_tab_id` 回全局一份、横幅恒列全组成员、`ClosedTabStack` 回全局的读取侧）注入点全在 `main.cpp` 之外无判据可指。§16 那一条教训在这里反过来用：装配层的账目要由**控件侧接缝**留出可断言之形，而本棒的改动正是把「读者只有一处」这个事实做实——4-B 的 `HeadlessSurface::set_should_close(true)` 那一枚证人（F24）才是本件第一次能真驱动「窗散了之后账目为空」。

**在册欠项（如实记，交批 4-B／4-C）**：① `ActiveWindowTracker::forget` 仍无调用方（4-B）；② `startup_wake` 的重指与清空（4-B）；③ 面板与浮层落点按发起窗（D6，含波特率那只框）、`hooks.preview_wake` 的 `TODO`、剩余三处 `app.focus()`（`DebugPanel`／`StartupNotice`／`SettingsPanel`）＝4-C；④ `Ctrl+F` 在窗内仍取 1 号视图（逐格搜索属 `SPEC.FEAT.INTERACT.04`）；⑤ OS 激活态与派发事实可答非所问的两档（§9 第 2 条），无头面不可证。

**边界**：批 4-A 到此收口（别名归零，`main.cpp` 内已无「第一扇窗」的长期引用，`erase` 从此是可做的动作）；关窗撤账链（4-B）、面板搬窗与退出序（4-C）、几何各自记忆（批 5）、CLI `--new-window`（批 6）、文档回写与三处同口径计数（批 7）均未落。§9 七条真机走查照欠，本批不称「可用」。

## 18 批 4-B 台账（关窗撤账链＋帧循环的框架半区证人，2026-10-21）

**落到的形状**（六件：`src/main.cpp`、`src/ui/active_window_tracker.{h,cpp}`、`utest_active_window_tracker`、`itest_workspace_window`、本稿；合计 416 增／43 删）。

⑴ **触发点落在 `set_on_window_closed`，与稿 §6 原写相反**。改口的证据分两半，各自来源不同，此处按档登记：
- **实测到的那一半**（新案 `the_frame_callback_still_sees_the_host_the_closed_hook_does_not`）：一帧循环里，被请求关闭的那一扇在**第一帧的 `on_frame` 时刻仍在宿主登记表里**（`window_host(id)` 取得到、`seen_alive`＝1），而在其后两帧的同一时刻已经取不到（`seen_gone`＝2）；且「取不到」的那些帧里 `on_window_closed` 的账目**已经写上**。⇒ `on_frame` 那一跳做得任何撤账，都发生在「这一帧还会轮到它」的窗口内；回收排在帧末，而回调排在下一帧之前。
- **未实测、取框架自述的那一半**：同帧内 `on_frame → render_all → reap_closed` 的先后写死在 Aurora 主仓 `include/aurora/app/application.h` 里 `run()` 的八步帧序注释中。本棒不改 Aurora（AGENTS.md §5），也不做框架侧注入，故这一条不冒充读数。它正是「在 `on_frame` 里 `erase` ⇒ 同一帧仍为刚被析构的那一格布局绘制」这一步推理的支点：本层的 `TabBarWidget`／横幅 hooks 捕的是 `WorkspaceWindow &`，控件不持窗、窗随宿主散，而 `render_all` 就排在同一帧的后面。
- 原写看中的那句（「销毁前唯一可观测点」）没有被否定，只是它给的是**读**的窗口而不是**写**的窗口；「关闭请求到达后、销毁前还要能否决」那一半留给稿 D4，框架不给否决入口（F18），本棒不做。

⑵ **撤账链的六步与一支共用件**：① 快照栏位号 → 逐张走 `perform_close`（不弹确认）② 归属兜底清扫 ③ `active_window_tracker.forget(id)` ④ 从 `workspace_windows` `erase` ⑤ 取不到落点即 `set_fallback(本层账目里活着的第一扇)` ⑥ `startup_wake` 若指着这一扇的 surface 即重指或清空。`perform_close` 里那段「摘根 → 撤会话表 → 撤四张连接表与类型表」的账目半区抽成 `erase_tab_ledger(window, id)`，两支共用同一支而不是各写一遍——这是 §5 那句「窗拆后三处账目为空」与「关掉这一格账目为空」成为同一条代码的落点。次序承重照原注释留在代码里（先摘根再撤会话，反过来 `~TerminalView` 会往已散的会话写撤账空响应）。

⑶ **归属兜底清扫不是死码，也不是第二份账**：真值源是 `TabWorkspace::owner`（批 2-A 那枚指针），凡 `owner` 指着这扇窗而其号已不在栏里，一律走同一支撤账并落一条 WARN。正常路径恒空——扫到即两处账目已经分叉（某格建起来没进栏一类真 bug），按「静默丢掉」处理就是让 §5 那句变成巧合而不是结构。先收号再撤：`erase_tab_ledger` 会 `erase` 正被遍历的那一节，边扫边撤即迭代器失效。

⑷ **`set_fallback` 那一枚接缝的来由是读数而不是设想**：新案 `the_main_window_slot_moves_to_a_surviving_window` 证到主窗散掉后 `app.main_window()` 交给活着的那一扇、`app.scene()`／`app.window()` 整组跟着易主。本层的兜底落点若**不**跟着换出去，`forget` 之后 `active()` 恒回无效号，命令在唯一活着的窗上永远没有落点。形态口径不变：`set_fallback` **只改兜底、不动派发事实**（`last_reported_` 不碰），无效号不装——与 `report` 同一条档，M2 把这条档摘掉即转红。

⑸ **4-A 交下来的两枚欠项在此闭合**：`ActiveWindowTracker::forget` 有了调用方；`startup_wake` 的重指与清空落了。⑥ 那一步用的是**先抄下来的 `closing_surface`**——比较必须在 `erase` 之后做而对象在 `erase` 那一刻就散，故抄指针而不是事后取。

⑹ **代价档（F19）如实记**：全关时框架保留主宿主，被保留的那一扇既不从 `hosts_` 摘掉、也**不**触发 `on_window_closed`（新案 `the_last_host_is_retained_so_the_closed_hook_skips_it`：`closed_windows()` 只有被摘掉那一扇、`window_count()` 收尾是 1 不是 0）。⇒ 最后一扇的账目靠本棒这条链收不掉，仍由 `run()` 返回之后那条 `session->close()` 循环收尾，形态与本棒之前一致；「关最后一扇走同一条退出序」在无头面物理上不可证（F21），照 §7 留在 §9 第 3 条走查。附带读数一条：`run()` 的**启动**判据是「至少一扇有 Window 且未请求关闭」，两扇都在进循环之前就请求关闭时 `run()` 直接返回、一帧都不排——那是「没启动」而不是「启动了又不退出」，本案第一版就栽在这里，故最后一扇的关闭请求改由帧内探针发出。

⑺ **控件在宿主散后仍活着的同一事实**：无头跑这四案时框架会刷一串 `layout parent detached while the child widget is still alive`（WARN，非失败）。它是本件的安全前提而不是缺陷：宿主散掉场景树之后，本层（`WorkspaceWindow`／`WindowFace`）仍持 `shared_ptr`，控件于是随本层的账目一起散——`perform_close` 里那两句 `mark_needs_*` 因此在撤账时点打不到野指针。这条观察同时是 §10 F22 那条「`on_close` 不会被调用」的另一面。

**读数**：`cmake --build --preset linux` 无 error／warning；`ctest --preset linux -E etest_` **97/97**（9.61 s）、runner 全跑 **1227/1227**（21.11 s）。门迁移：**97 项／1221 案 → 97 项／1227 案（+0 项／+6 案）**——`utest_active_window_tracker` +2（兜底可换、无效兜底不装）、`itest_workspace_window` +4（关一扇不退出／两枚观测点的先后／主窗易主／最后一扇被保留），既有六案零改动照绿。三处文档的同口径计数照例归 §6 批 7 一次性改。

**变异自证**（§4.4 第 22 条，四则注入逐条点名，等价档一并记）：
- **M1 `set_fallback` 的赋值不执行**（注入点＝本仓生产件 `src/ui/active_window_tracker.cpp`）⇒ `utest_active_window_tracker.the_assembly_can_hand_the_fallback_to_a_surviving_window`（两笔读数 0 vs 2、0 vs 4）与 `itest_workspace_window.the_main_window_slot_moves_to_a_surviving_window`（0 vs 2）双双面红。
- **M2 `set_fallback` 的无效号档摘掉** ⇒ `utest...an_invalid_fallback_is_never_installed` 面红（0 vs 1）；对四案 itest **等价**——它们从不装无效兜底，如实记不冒充转红。
- **M3 `request_close` 不落地关闭请求**（注入点＝测试件 `TwoWindowRig`；帧循环那半区的真值在 Aurora 侧，本棒不改框架、不做框架侧注入）⇒ 四案全部面红（`window_count()` 2 vs 1、`seen_alive` 3 vs 1、`closed_windows().size()` 0 vs 1 等），既有六案**等价**照绿（它们不关窗）。
- **M4 `touch()` 不标脏**（注入点＝测试件 `WindowFace`）⇒ `closing_one_window_...` 那一笔「另一扇仍在渲染集合里」面红（1 vs 1）。这一则是本件最要紧的一则：无头的脏区决策会整段跳过空闲帧，`frame_count()` 在没有任何脏时**本来就不涨**，不先给它一口脏，那句断言就是一句恒真的空转。
- 装配链本身（`main.cpp` 里那六步）仍无可注入的判据（裁决 7.83），照 §17 末段那一档处理：源码审查＋§9 走查。

**在册欠项（如实记，交批 4-C／批 5–7）**：① 「窗拆后三处账目为空」在生产路径上仍无自动化证人；② 面板与浮层落点按发起窗（D6，含波特率那只框）、`hooks.preview_wake` 的 `TODO(SPEC.FEAT.WS.03)`、剩余三处 `app.focus()`＝4-C；③ 主窗散后其 `OverlayHost` 由装配层那枚按值副本续命到进程结束（看不见但不是野指针），可见性那一半属 D6＝4-C（§17⑷）；④ `Ctrl+F` 在窗内仍取 1 号视图（属 `SPEC.FEAT.INTERACT.04`）；⑤ OS 激活态与派发事实答非所问的两档、以及「关最后一扇走同一条退出序」，无头面不可证，均在 §9。

**边界**：批 4-B 到此收口（一扇窗被框架收走之后，本层不再有指着它的活引用、落点与唤醒指针都有去处）；4-C-1 的面板搬窗（§19）、4-C-2 的退出序、批 5 几何记忆、批 6 CLI `--new-window`、批 7 文档回写与三处同口径计数均未落。§9 七条真机走查照欠，本批不称「可用」。

## 19 批 4-C-1 台账（面板搬窗与散窗兜底腿，2026-10-21）

**落到的形状**（二十件：`src/main.cpp` 221 增／36 删、九对 `src/ui/*.{h,cpp}` 合计 370 增／74 删、新增 `tests/integration/itest_panel_retarget.cpp`（358 行／十一案）、本稿；`src/ui/terminal_view.*` 与纯逻辑件一行未动）。

⑴ **三类件、两种接缝、一份装配账**：
- **七只瞬态面板**（`ConnectionSidebar`／`ConnectionWizard`／`CredentialPrompt`／`KeysPanel`／`SettingsPanel`／`SftpPanel`／`TunnelPanel`）加 `set_overlay_host(OverlayHost &)` 与 `overlay_host()`。setter 只做三件事：按自己记下的序号从**旧**宿主摘回、清掉本件的浮层句柄与内部状态、把宿主指针换成新的。换完是**关闭态**——往新宿主的第几层由调用方紧接着走的既有开合腿建，本棒**不**给 setter 加第二条建浮层的路径（加了就是同一件事两种形状，判据只能证到其中一种）。
- **两只常驻框**（`DebugPanel`／`StartupNotice`：浮层建一次、开合只切显隐）另加一对：`set_overlay_destination(OverlayHost &, FocusManager &)` 与 `abandon_overlay_destination(OverlayHost &, FocusManager &)`。前者宿主与焦点权威**成对换**（稿 §0 的 F3：压进哪一扇窗的栈就必须由同一扇窗弹出，分两次设就是让作用域漏在散掉的那一栈里）；后者是散窗那一侧的对应腿，三本账（`scope_pushed_`／`overlay_index_`／`dialog_`）一律**清零**而不是摘回——旧宿主与旧焦点栈都随那一扇一起没了，既无处可摘也无处可弹，能做的只有承认「上一处的东西已经不在了」。`dialog_` 必须一起放：否则下一次 `toggle()`／`show_if_needed()` 走「句柄已在、只切显隐」那条腿，而切开的这份对话框并没有挂在新宿主上，面板此后再也不出现。放句柄**不**回调 `on_close`（框架只在 `close()` 末尾调它，`dialog.h` 自陈无析构回调），因此也不会拿着新焦点权威去少弹一层——这一条是 M6 的落点。
- **三枚装配层直挂宿主的常驻浮层**（命令面板、串口波特率框、会话日志框）不经控件接缝（它们是框架的 `CommandPalette`／`Dialog`，不认识窗），账目由装配层自己那份 `MountedOverlay{on, index}` 与 `remount_overlay` 兑现：同宿主即空操作，否则先按序号从旧宿主摘、再往新宿主 `add_overlay` 并**重取**序号。留序号而不是留一枚 `bool mounted` 的理由就在搬窗这一步——没有序号就摘不到自己那一层，只能往别的窗再挂一份，那变成「各窗一份」，不是本条口径。

⑵ **两条腿、三条口径**：打开腿（九处 `retarget_panel`／`retarget_destination` 与三枚 `remount_overlay` 调用，排在既有开合动作**之前**）与撤账腿 ③′。三条口径写在装配层那段注释里，判据按同一口径读：① 取不到落点窗即**原地不动**（面板继续挂在它现在那一扇，而不是猜一扇活着的窗——「把用户没碰过的那一扇改得看得见」比「这一条没生效」更难解释，与 §17⑶ 那一条同源）；② 真值源在各件自己记的那枚宿主指针，装配层**不**另立一张「面板在哪扇窗」的表（那是第二份账，必然与摘挂时机分叉）；③ 摘与挂的次序归各件 setter，本支只决定「搬到哪」。`window_of_host` 是「宿主指针 → 本仓那份窗」的唯一换算处（容器才同时有宿主与窗）。

⑶ **撤账腿 ③′ 排在 ④ `erase` 之前**，位置是承重的：setter 与重挂都要从**旧宿主**按序号摘回，而旧宿主随本层对象一起散（主窗那一份另有 §17⑷ 那枚按值副本续命，其余窗没有这层保险）。落点取「除这一扇之外的第一扇」而不是 `command_window()`——关闭不是一条来自某扇窗的命令、没有派发事实可依，与 ⑤⑥ 那两跳同一条兜底口径；这一支与 ⑤ 事后取到的 `fallback_window()` 在同一份容器读法下等价，但必须**在 erase 之前**算。两只常驻框在这一跳走 `abandon_` 那条而不是 `set_overlay_destination`：旧窗的 `FocusManager` 在 ④ 之前就已被框架回收（Aurora 主仓 `codespec/specification/06-app-platform.md` §3「宿主被回收**之后**触发一次」），那条腿里的 `pop_scope()` 会落手在已析构的对象上——**这是本棒新增第二枚接缝的全部理由**，否则同一件事只需要一个方法。

⑷ **改口两处**：① 稿 §6 批 4-C 写的「剩余三处 `app.focus()`（`DebugPanel`／`StartupNotice`／`SettingsPanel`）」实测只剩两处——`SettingsPanel` 构造吃的是 `app.shortcuts()`，快捷键表按稿 §0 的 F2 本就是 app 级一份，那一条对它不适用；② `hooks.preview_wake` 那条 `TODO(SPEC.FEAT.WS.03)` 已收，真值源改成设置页自己记的那枚宿主指针（经 `window_of_host` 换算回窗），对不上号即「面板挂在一扇已散掉的窗上」＝撤账腿还没跑到的窗口，那一档退回 `fallback_window()` 而不是不醒——夹具重投的那批脏总要有一扇窗来排。

⑸ **四条代价档**（D6 的「搬窗而不是各一份」在本棒之后长这样，§9 第 6 条走查取观感读数）：
- **C1 搬完即关闭态**：瞬态七件由「先搬再开」掩盖掉，两只常驻框与三枚直挂浮层是真代价——在 B 窗按 F12，A 窗那块调试面板就此消失且**不**在 B 窗自动打开，用户要重按一次。
- **C2 「两窗不能同时各开一块 SFTP」**（D6 原文）：设置页、密钥页、SFTP、隧道同理，界面上是搬过去而不是各有一份。`the_settings_page_moves_instead_of_existing_twice` 那一案的案名就是这条代价的读数形态。
- **C3 降级提示随窗散去即消失、不重弹**：`StartupNotice` 的 `shown_` 幂等闸记的是「本次启动弹过」，与挂在哪儿无关；它也没有打开腿（一启动只弹一次），只有撤账腿。
- **C4 旧焦点栈那一层作用域随窗带走**：`abandon_` 那条腿不弹旧栈。真实散窗时旧管理器根本已经不存在，测试里那案（`the_startup_notice_abandons_without_touching_the_dead_stack`）特意让旧宿主与旧管理器**还活着**，正是为了把这一档读成可见断言（`focus_a.scope_depth()` 仍是 1）而不是让它藏在销毁序里不可观测。

**读数**：`cmake --build --preset linux` 通过且无 error／warning；`ctest --preset linux -E etest_` **98/98**（9.59 s）、runner 全跑 **1238/1238**（21.41 s）。门迁移：**97 项／1227 案 → 98 项／1238 案（+1 项／+11 案）**——新 TU `itest_panel_retarget` 进 CTest 一项、十一案；既有 1227 案零改动照绿是「单窗形态与批 4-B 那条链未破」那一侧的证据。三处文档的同口径计数照例归 §6 批 7 一次性改。

**变异自证**（§4.4 第 22 条，六则注入逐条点名、逐条取实际读数，落点全在本仓九件控件源文件内；M2 单独跑一轮，M1／M3／M4 同批注入以证「各件 setter 互不相干」——那一轮只有各自点名的那一案转红、其余八案照绿，M5 与 M6 各单独一轮。注入转红后全部还原，`.bak` 复原会保留旧 mtime 而骗过 ninja，故 `touch` 一遍再重编，11/11 复绿）：
- **M1 `SftpPanel::set_overlay_host` 不关旧浮层** ⇒ `the_sftp_panel_moves_without_leaving_a_copy_behind` 面红四笔：`a.overlay_count()` 1 vs 0（判据里两处）、`panel.is_open()` 仍真、搬完再开时 `b.overlay_count()` 0 vs 1（旧宿主那一层还开着，重开腿即不再往新宿主挂）。其余六案**等价**照绿——各件 setter 独立。
- **M2 `KeysPanel::set_overlay_host` 的同宿主闸门摘掉** ⇒ `the_keys_panel_moves_its_rows_off_the_retiring_host` 面红两笔：`panel.is_open()` 变假、`a.overlay_count()` 0 vs 1。同宿主重复设本应空操作，闸门一摘就真去 `close()` 了一次——这一案判的是「重复设不伤害现场」那条口径。
- **M3 `DebugPanel::set_overlay_destination` 不往新宿主重挂** ⇒ `the_debug_panel_is_detached_from_the_old_host_and_remounted_on_the_new_one` 面红两笔（搬开后与再开一次后各一笔，`b.overlay_count()` 0 vs 1）。这一则是常驻框与瞬态面板的**形态分界**：瞬态靠调用方重开，常驻框必须自己重挂。
- **M4 `StartupNotice::abandon_overlay_destination` 改成去弹旧栈** ⇒ `the_startup_notice_abandons_without_touching_the_dead_stack` 面红（`focus_a.scope_depth()` 0 vs 1）。这一条证的正是 ⑶ 那句销毁序：真实散窗时那一跳是往已析构对象落手，本件不许走那条腿。
- **M5 `SettingsPanel::set_overlay_host` 的摘挂次序反过来**（先换指针再摘）⇒ `the_settings_page_moves_instead_of_existing_twice` 面红两笔（`a.overlay_count()` 1 vs 0，判据里两处）。摘的那一跳落到了 B 宿主上，而 B 只有基础内容那格，框架的越界档让它**空摘**（`popup.h`：序号越界或为 0 时不做任何事）——旧宿主那一层就此留在原地。口径 ③ 的承重次序由此成为有证人的一条而不是一句注释。
- **M6 `DebugPanel::abandon_overlay_destination` 不放对话框句柄** ⇒ `the_debug_panel_abandons_a_host_that_is_already_gone` 面红三笔：`panel.dialog() == nullptr`、下一次 `toggle()` 后 `b.overlay_count()` 0 vs 1 与 `focus_b.scope_depth()` 0 vs 1（走的是「句柄已在、只切显隐」那条腿，而那份对话框并没有挂在新宿主上）。这一则是本件最阴的一则：注入后**当前这一次**看不出问题，坏在「此后再也不能出现」。
- 装配层那两条腿（九处 retarget、三枚 `MountedOverlay`、③′ 的落点选择与调用次序）无可注入的判据（`main.cpp` 不进 runner，裁决 7.83），照 §17 末段与 §18 末段同一档处理：源码审查＋§9 第 6 条走查。

**在册欠项（如实记，交批 4-C-2／批 5–7）**：① `ExitPolicy` 从未在本仓设置，实测读数是框架缺省 `ExitPolicy::LastWindowClosed`（Aurora 主仓 `include/aurora/app/application.h` 的成员初值），「缺省确认与退出序遍历全部窗」＝4-C-2；② 侧栏→向导那条落点缺口（向导吃 tracker 而不是点它的那一扇，§10 批 4-C-1 补记第 4 条）；③ 三处文档同口径计数 96／1212 → 98／1238 归批 7；④ `Ctrl+F` 在窗内仍取 1 号视图（属 `SPEC.FEAT.INTERACT.04`）；⑤ OS 激活态与派发事实答非所问的两档、以及「关最后一扇走同一条退出序」，无头面不可证，均在 §9。

**边界**：批 4-C-1 到此收口（面板与浮层的落点窗成为可变事实且散窗时有人接手，§17⑷ 那条「看不见但不是野指针」的形态自此有了可见的那一半）；批 5 几何记忆、批 6 CLI `--new-window`、批 7 文档回写与三处同口径计数均未落。§9 七条真机走查照欠，本批不称「可用」。

## 20 批 4-C-2 台账（`ExitPolicy` 缺省确认与退出序按窗走，2026-10-21）

**落到的形状**（一件：`src/main.cpp` 65 增／3 删，两跳都在 `run()` 返回之后的退出段与其注释上；控件、纯逻辑件、测试目录一行未动）。

⑴ **`ExitPolicy` 的「缺省确认」是一件实测，不是一行代码**。读数两笔：本仓（`src/`＋`include/`）`set_exit_policy` **零调用点**，框架侧 `include/aurora/app/application.h` 里 `exit_policy_` 的成员初值写死 `ExitPolicy exit_policy_ = ExitPolicy::LastWindowClosed`。⇒ 「关掉最后一扇才退出」是框架给的语义，本条直接消费它（D11 推荐档）。**不显式再设一遍**是刻意的：设了就把「本仓依赖框架缺省」这件事从代码面上抹掉，而框架哪天改缺省正是本条要第一时间看见的变化——本棒把它留在注释里（`main()` 末尾那段），并把真值取法留给 §9 第 3 条。另外两档为什么否：`MainWindowClosed` 让「先关主窗」那一跳直接终结进程（多窗下等于把 F19 的保留宿主语义当没看见），`ExplicitOnly` 让最后一扇关掉后进程不退出（终端类应用因此留着 PTY 子进程）。

⑵ **退出序的读法从「扫全局表」改成「逐窗三步」**，与 4-B 撤账链第 ① 步同一条口径：真值源是「每窗名下全部标签」，`tab_workspaces` 只是那套账目的另一种落处。原读法把「这一格属于谁」当成不必知道的事实，多窗之后那正是本条要守住的一格。三步＝**关该窗名下每一格的全部会话**（`Session::close()` 自带「先 join 读线程再停笔」的次序，`SPEC.FEAT.CONN.11` 的末段完整性）→ **摘该窗的根**（`root_column->child_nodes_mut().clear()`，见 ⑷）→ **撤那些格的账**（`tab_workspaces.erase`）。栏位号先取快照再逐格走：`tabs()` 给的是栏位表本体的视图，而 erase 要动那张表。

⑶ **兜底腿存在的全部理由是「无残留进程」那条判据**（§9 第 3 条）：逐窗三步之后，表里还留下的每一格必然不属于任何活着窗的栏位——那是「栏位表与全局表分叉」的证人，形态同 4-B 撤账链第 ② 步，正常路径恒空。走到即落一条 `exit: tab N is not listed by any live window's strip` 并照收不误（关＋撤）。那条日志因此是本件在真机上唯一能读出「账目分叉发生过」的地方，不是装饰。判据**不查 `owner`**：这一跳恰恰要为「属主那一扇已被 4-B 的 ④ 从容器里取走」那种情形收账，此刻 `owner` 是悬垂值，解引用即 UB，而「先跑完全部活着窗的三步、剩下的全归兜底」这条结构本来就不需要读它。

⑷ **摘根那一跳是本棒的实质修正，落的是 §12 留的那一句**（「批 4 要把这一段显式化，而不是继续依赖声明序」）。实测形状：`main()` 里 `workspace_windows` 的声明排在 `tab_workspaces` 之前，于是**最后**析构的是窗容器；根 Column 的子节点 [2] 正是「当前那一格的 `WorkspaceView`」（`rebuild_root_children` 写的），而 `TerminalView::session_` 是**非拥有裸指针**（`src/ui/terminal_view.h` 那条成员声明）。⇒ 原形态下那份视口要等 `main()` 返回才散，那时它背后的会话早已随 `tab_workspaces` 析构；`~TerminalView` 里的 OSC 52 跨帧承诺回写（`SPEC.FEAT.CONN.12` 判据 ⑤：「问出去的那一条与排队没问的，本控件散场时都要各有一条回写」）走的正是这条腿，而它成立的前提是「会话对象还在、只是已关」——写腿的 closing 闸丢的是**已关**会话的写，不是已散对象。先清根再 erase，视口就在自己那一格被销毁，`TabWorkspace` 的成员次序（会话容器声明在 `workspace` 之前）保证会话在其后散——与 `perform_close` 那条「先摘根再 erase」是同一条不变量，本棒只是把它在退出路上也补成了形。触发条件（退出时仍有在途 OSC 52 读）窄，但形态错位是结构性的，不该留给运气。**同一处还顺带合上第二档**：`au::Application app` 的声明排在这两个容器之后，于是**它最先析构**——原形态下视口是在框架运行时已经散完之后才走到 `~TerminalView` 的（那里要动计时器句柄与对话框）。本棒不宣称那一定崩，只登记「视口的销毁点挪回了 `app` 还活着的时候」这条次序事实。

⑸ **不复用 `perform_close`／`erase_tab_ledger`**：那两条是「关标签」这个动作的链，要搬栏位表、要在当前号被关掉时 `rebuild_root_children` 重排现场并给宿主排布局脏、要刷横幅。`run()` 已返回，那些副作用一项都不需要，而重排那一跳恰恰可能落在已请求关闭的宿主上（§18⑵ 那条销毁序在此同样成立）。四张连接表在这里也不撤：本函数此后没有任何读者，多撤一遍要付的就是那条重排。

⑹ **改动面按结构读，不按「跑过一遍」读**：收掉的会话集合与原来相同（`TabId` 全 app 唯一，`next_tab_id` 在 `create_new_tab_with` 里 `++` 是唯一取号点 ⇒ 逐窗三步 ∪ 兜底腿＝原扫表那一遍，不多不漏、同一格不会被关第二次）；**多出来的那一跳是把视口与会话的析构从 `main()` 返回时提前到退出序内、按「先散视口后散会话」的顺序做一遍**——那正是本件要的，不是副作用。

**读数**：`cmake --build --preset linux` 无 error／warning；`ctest --preset linux -E etest_` **98/98**（9.70 s）、runner 全跑 **1238/1238**（21.36 s）——与批 4-C-1 之后的门**逐字相同**，本件零新增用例、零用例改动；这一档只证「既有形态没被打断」，**不**是退出序本身的证人（`main.cpp` 不进 runner，裁决 7.83）。产品侧两档：`build/src/Borealis --help` 退出码 0、输出 470 字节纯 ASCII；真实二进制在本机图形会话里跑满 6 s 与 8 s 两档，stderr／stdout 零字节、被 `timeout` 收走之后 `pgrep` 无残留。后者**只**覆盖「启动与帧循环」，退出序那半区仍然走不到——本沙箱没有把关闭请求投给一扇窗的手段（无 `xdotool`／`wmctrl`，`xwininfo -root -tree` 在根less Xwayland 里看不见本仓窗口，脚本层也无从点掉标题栏）。三处文档的同口径计数照例归 §6 批 7 一次性改。

**变异自证**：本棒**无可转红的注入**，如实记不冒充读数——结项三件齐里的第三件在这一棒只能落进 §9 第 3 条走查与源码审查。理由是两条物理边界（已进 §10）：退出序整条腿要 `run()` 返回，而全为无头宿主时 `should_exit()` 永假（F21）；兜底腿扫的是账目分叉态，无头用例造不出「窗已散而本层仍活着并走到退出」的那个现场。加上 `main.cpp` 不进 runner（裁决 7.83）这一条老边界，本件连「注入一个错法看有没有用例变红」的余地都没有。

**在册欠项（如实记，交批 5–7）**：① 批 4 至此收口，稿 §6 批 4 的三棒（4-A／4-B／4-C-1／4-C-2）全落，剩下的都是批 5 以后；② 三处文档同口径计数 96／1212 → 98／1238 与 WS.03 现状句、D11 裁决的正式回写归批 7；③ §9 第 3 条新增的三条读数（兜底 WARN 不该出现、两种关窗次序的退出与残留进程、退出前留一条在途 OSC 52 读）照欠真机。

**边界**：批 4-C-2 到此收口；批 5 几何记忆**已落**（§21）、批 6 CLI `--new-window` 与批 7 文档回写／三处同口径计数未落。§9 七条真机走查照欠，本批不称「可用」。

## 21 批 5 台账（逐窗几何分别记忆，2026-10-21）

**落到的形状**（六件：新增私有头两枚、`src/CMakeLists.txt` 一行、`src/main.cpp` 56 增／5 删、新增单元 TU、既有集成 TU 加接缝与四案）。

⑴ **接缝落在 config 侧的一枚私有头**（`src/config/window_geometry_store.{h,cpp}`，83／49 行，D1① 口径）。为什么不是公共头：本件要交出去的是 `au::preferences::Preferences *`，而公共头不吃框架类型（AGENTS §2 那句）；为什么不是 `config::Store` 的一个成员：`Store` 吃的是 `borealis.json` 与 69 键 schema，几何既不进 schema 也不进 `PREF.07` 导出件（D9），把它塞进同一份 `Preferences` 会得到「一个文件两种生命周期」——schema 自校验与降级备份那套动作每次都该只管设置，不该为一枚窗口位置重写一遍。于是本仓第二枚 `Preferences` 落点另起一文件（`windows.json`，与 `borealis.json` 同一层目录），而 `src/config/store.cpp` 仍是「按裁决 7.26③／架构 §2.2 触达 `Preferences`」的那一小片地方的成员之一——**同一域，两个文件**。件本身只做三件事：`preferences()`（给框架裸指针）、`path()`（诊断与用例读）、`flush()`（把 `Result<void>` 折成「有则 ASCII 原因、无则成功」，不碰 stdio，§4.3 第 15 条）。禁复制禁搬移写死在类上：`Preferences` 内含互斥量，搬走它等于搬走一把正被框架读的锁。

⑵ **六案单元证人**（`utest_window_geometry_store`，140 行）判的是**存储本体**，一扇窗都不开：① 键的形态（`main`／`window-2`／`window-3`，`window_persist_id(11)`＝`window-12`，且键里不含点号——框架 `reconcile` 会按点拆嵌套，拆了就找不到键，裁决 7.27⑤）；② 文件绑到 `<给定目录>/windows.json`，且 `is_persistent()` 为真（内存模式的 `flush()` 是恒失败的空转，这一档必须显式钉住）；③ `flush()` 之后换一个实例读同一份文件仍拿得到同一格（框架无隐式 I/O，落盘时机是本件与装配层的全部职责）；④ **写进 `group("windows")` 的那一格，自动链取不到**——分组里读得到、顶层读不到，这一对断言就是 F26② 那处错位的形状：写错层的后果是「存得回而恢复永不发生」，生产面上只表现为「窗口每次都回默认尺寸」，一行日志都不会说键找错了地方，所以它必须有证人；⑤ 不可用几何回空值（完全屏外、尺寸非正两类走同一条 WARN 并回空，部分越界仍算可用——判据按框架的宽松口径写，本仓不收紧）；⑥ 落盘失败回一条**非空**原因而不抛（夹具：路径上先占一枚普通文件当 blocker，`auto_create_dir` 造不出「文件下面的目录」）。

⑶ **四案集成证人**（`itest_workspace_window` 的批 5 段）判的是**窗侧行为**，全部经本仓那枚存储落文件与键，恢复与保存都走框架自动链：① 各窗取自己那一格（`main`＝800×300 @(120,90)、`window-2`＝900×400 @(300,210)，尺寸与位置逐值比，另加一条「两扇宽度不等」的反面证人——两扇若共读一格，尺寸相同而位置不同，只有逐值比对才现形）；② 没有那一格的那扇保持默认 640×120、位置不动（这一档就是首次启动的形态：恢复腿回空值时**不回写**，所以「默认尺寸」是「没得恢复」的结果而不是「恢复成默认」的结果，两者得分开证）；③ 恢复落在**首次 present 之前**（驱动台在 `render()` 之前取快照：尺寸已是恢复值而后端 `frame_count()` 仍是 0——这一条就是稿 D13 要的「恢复后首帧只下发一次」的可见面，终端网格若在恢复前后各下发一次，那一档在这里现形）；④ 关掉某一扇把最终几何写回**它自己那一格**，且活着那一扇那一格仍空（保存腿在 `reap_closed` 的收集步里，F26①；不 `flush()`，因为本件判的是「有没有写对格子」，落盘那条腿在 ⑵ 的第三案）。harness 为此改了四处：`WindowFace` 加第四入参 `persist_id`（写进 `WindowOptions::persist_id`）、`TwoWindowRig` 构造加第三入参 `au::preferences::Preferences *`（空即完全不接线，与批 5 之前逐字相同）、接线排在**第二扇登记之前**、新增四个读数口（`surface_size`／`surface_position`／`size_before_first_frame`／`frames_before_first_frame`，宿主散了或没建成一律回空值而不裸解引用）。几何案**不调 `preflight()`**：那条要读画布，而 `pixels()` 按驱动台默认的 640×120 定长 `memcpy`——恢复尺寸若更小就是过读，故新案一律取**大于**默认值的几何，另以 `surface_scale(face) == 1.0F` 一条前置顶替它（缩放为 1 才是「物理像素＝逻辑值」，逐值比对才成立）。

⑷ **键的分配用创建序、只增不复用**（`std::size_t next_window_index` 一支计数器，工厂吃掉它）。两个否掉的读法：按 `WindowId` 当键——号由框架在宿主登记时发、拆窗不回收、也不保证重启之间稳定，同一扇窗每次启动换一个名字；按「活着几扇」当键——拆一扇再开一扇会让**两扇先后共写同一格**（前者的几何被后者覆盖，且覆盖发生在关窗那一刻，看起来像「记住的是错的那扇」）。代价是旧键不清理（`window-3` 那一格会留在文件里再没人读），登记为 §10 批 5 补记第 2 条；那条「两扇共写一格」的形态更坏，且清理要引入「哪些键算活着」这件新事实，本棒不付这个价。键在**建窗之前**写好（`out->options.persist_id = ...` 排在 `create_window` 那一跳之前），失败的那一扇不留账。

⑸ **变异自证**（四条转红＋一条**指名等价**，落点全在本仓源文件内，不改 Aurora）：
- **M1 `window_persist_id` 恒回空**（生产件）⇒ 集成四案**全红**、单元两案红（键形态与往返）。这一则就是 §7 变异候选清单里那条「`persist_id` 恒空」，现在有了读数。
- **M2 两扇共用一格**（harness：第二扇的键取 `window_persist_id(0U)`）⇒ 集成四案**全红**。第二扇读到主窗那一格（800×300 @(120,90)），故①的尺寸与「宽度不等」断言红、②的「未键合那扇保持默认」红、③的快照尺寸红、④写回落到 `main` 那格而 `window-2` 仍空红。
- **M3 完全不接线**（harness：`set_window_geometry_store(nullptr)`）⇒ 集成四案**全红**（既没有恢复也没有保存）。
- **M4 把首帧快照挪到两次 `render()` 之后**（harness 观测点，不是生产时序）⇒ **只有第③案红**。这一则证的是「③ 的读数不是空转」：同一份存储、同一条接线，仅观测时刻不同就能分箱——它**不**冒充「生产里恢复排在首帧之后」的证人，那一跳在 `main.cpp`，无自动化证人（裁决 7.83）。
- **等价注入（如实记）**：把 `set_window_geometry_store` 从「第二扇登记之前」挪到**之后** ⇒ **一案都不红**。等价原因读自框架自身：那枚 setter 对 `hosts_` 里**已登记**的每一扇补做一次 `restore_geometry_for`（`application.cpp` 的 `set_window_geometry_store` 循环），故两档都覆盖到主窗与第二扇，只是走的路径不同（前者＝第二扇经 `register_host`、主窗经补做；后者＝两扇都经补做）。⇒ 「接线排在 `run()`／首帧之前」仍是硬要求（D13），但它**不是**「排在第二扇登记之前」的硬要求；稿 §0 的 F26① 那句两跳入口照旧成立，只是不构成判据。原写在这里的预期已由读数改写，登记为 §10 批 5 补记第 3 条。

⑹ **两处刻意不做**：`WindowGeometry::mode` 存而不取（框架 `apply_host_geometry` 只 `set_size`＋`set_position`）⇒ 最大化态回不来，本仓不在装配层补偿（要猜框架时序、又在应用侧私改框架路径，AGENTS §5.1／§5.3 与 §4.1 第 3 条都不占），已进 §3 候选缺口二与 §9 第 4 条①；`group("windows")` 不用（F26②），本件根本不提供分组入口，也不许在外面另建一份键表。

⑺ **装配层三跳仍无证人**（`main.cpp` 不进 runner，裁决 7.83）：建窗时分配键、`set_window_geometry_store` 接线、两处 `flush()`（撤账钩子尾部一次、`run()` 返回后再一次——后者覆盖「最后一扇被保留、不走 `on_window_closed`」那一档，F19）。`flush()` 的返回值取的是「有原因即落一条 WARN」，`Preferences` 无「改过没有」查询（F26⑥）故两处**无条件**提交。

**读数**：`cmake --build --preset linux` 无 error／warning；`ctest --preset linux -E etest_` **99/99**（9.64 s）、runner 全跑 **1248/1248**（21.17 s）——与批 4-C-2 之后的门相比 **+1 套件／+10 案**（98→99、1238→1248），增量全部落在新增 TU 与本 TU 的新增段内，既有 1238 案零改动照绿。产品侧一条：`XDG_CONFIG_HOME` 指到隔离目录跑真实二进制 8 s（`DISPLAY=:0`／`WAYLAND_DISPLAY=wayland-0` 的图形会话），退出码 124（被 `timeout` 收走）、stdout 与 stderr 各 **0 字节**、`pgrep` 无残留进程，隔离目录**空**——两处 `flush()` 都排在正常退出链上，被信号收走时一轮都不走。⇒ 这条既不是缺陷也不是「几何已记住」的证据，它把「点关闭钮、重启看位置」这件事留在唯一能取到真值的地方（§9 第 3、4 条）。

**在册欠项（如实记，交批 6–7）**：① 装配层三跳与两处 `flush()` 无自动化证人（⑺），§9 第 4 条走查照欠；② `windows.json` 首启即创建、旧键不清理、接线先后不构成判据、`mode` 不回四档已进 §10 批 5 补记；③ §3 候选缺口二（`mode` 有存无取）待按 §5.1 决定是否提 Aurora 任务书，本棒未提；④ 三处文档同口径计数（96／1212 → 98／1238 → **99／1248**）与 WS.03 现状句、D9／D13 裁决的正式回写归批 7。

**边界**：批 5 到此收口（逐窗几何有键、有存储、有落盘时机，四案＋六案有证人）；批 6 CLI `--new-window` **已落**（§22）、批 7 文档回写与三处同口径计数**已落**（§23）。§9 七条真机走查照欠，本批不称「可用」。

## 22 批 6 台账（CLI `--new-window` 放开，2026-10-21）

**落到的形状**（八件：两枚公共头、两枚纯逻辑件、`src/main.cpp`、三个既有测试 TU；零新增文件，`git diff --stat`＝248 增／44 删）。

⑴ **`--new-window` 是第三个动作的写法，不是修饰符**（`include/borealis/cli/startup.h`＋`src/cli/startup.cpp`）。`StartupAction` 加 `NewWindow`（值 2），解析侧出现该旗标即置 `new_window` 并把 `action` 折成 `NewWindow`；它与两个子命令**双向互斥**（先写的后到的都拒，诊断串里带上两侧的名字），重复出现幂等。为什么折而不是并列：「在新窗里切当前窗的那一格」没有可读的语义，而一旦允许并列，请求就要表达两件事、回复只能表达一件事。`--help` 文案随此改写（一句「open a new window instead of touching the running one」，删掉原来那句「when an instance is already running」——冷启动侧它同样成立，留着就是把错误口径写进产品输出）。`startup_policy_error` **一行未动**：`--profile`／`--cwd` 的判定按「首标签吃什么料」而不是「落到哪扇窗」，新窗那一格吃的就是同一份料，故本件对它不设拒绝档（稿 D10 相容档）。

⑵ **线格式：action 多一个值，旗标位同时保留**（`include/borealis/cli/handoff.h`＋`src/cli/handoff.cpp`）。magic／version／字段布局全不动，`action` 域多写一个 2；编码与解码**共用同一枚上界** `kMaxActionValue`（取自本版枚举的 `NewWindow` 而不是写死数字——界与表同源，加第四档时不会出现「编码放行、解码拒收」的错配）。解码侧把上界检查与折枚举分开做：越界即**整帧拒**，表内值直接 `static_cast<StartupAction>`，不再为「动作 0/1 ＋旗标 1」另造一档语义。为什么保留 flags bit0：旧版 CLI 只会写旗标（动作仍是 `new-tab`），那条帧是**合法混版帧**而不是坏帧，接收侧读到 bit0 即按同一个含义办。反过来，手工造的「`split-pane` ＋旗标」那一格只可能来自版本错配或第三方实现，接收侧显式拒并回 ASCII 诊断（§10 批 6 补记第 2 条）。

⑶ **装配层：先验证、后开窗、开了就上报**（`src/main.cpp`）。撤掉那条「`--new-window` 需要 WS.03，尚未实现」的拒绝支，换成 `wants_new_window` 一支判定。次序是刻意的：档案解析、政策判定、`--cwd` 是不是目录全部排在**开窗之前**，失败即回诊断、桌面上不留一扇空窗（真机读数＝§9 第 5 条⑦）。`open_new_window` 从既有工厂腿里**拆出来**（原 `open_new_window_request` 是「开窗＋落 D1 缺省本地标签」那一整块，`Ctrl+Shift+N` 要的就是那一块），拆成「只开窗、回该窗指针」的内层与「开窗＋缺省标签」的外层——CLI 这一腿不能先落一格缺省标签再补一条请求，那会变成两格。开成新窗后立刻 `active_window_tracker.report(landing->id)`：四条档案腿（SSH／本地／串口／Telnet）与 `connect_profile` 的凭据询问续跑都是经 `tab_host_or_none` **现取**落点的，不报则远程档案那一格可能开回旧窗（稿 D10 补记那一处接线，§10 批 6 补记第 1 条）。冷启动侧把 `NewWindow` 与 `NewTab` 并成同一条「首标签即动作产物」的判定（那一扇就是新窗），不额外开窗。

⑷ **判据与读数**（三处扩案，共 +4 案）：`utest_cli_startup` 12→14——旗标折成第三动作（裸旗标、重复旗标、带 `--profile`/`--cwd` 的料仍在）与「和两个子命令两种先后都互斥、且不吞掉未知选项那条诊断」各一案，另把「政策不拒 `NewWindow`」并进既有政策案的四档动作循环里；`utest_cli_handoff` 6→7——新值编码到 `bytes[5]`＝2、往返全等，**旧版形态**（动作 0＋旗标 1）编码仍是 0 且往返读回旗标，越界值（3 与 0xFF）不交出一帧；`itest_posix_instance_channel` 8→9——**真协议帧过通道**：把 `encode_startup_request` 的产物原样转发，处理端拿到的字节序列与发出那份全等、回执原样送回，再解码回来与请求逐字段全等。这一案补的是既有八案共同缺的那根轴：它们转的都是可打印字符串，而协议帧**必然含 NUL**（offset 7 的 reserved、`cwd_len` 的高字节），帧长只能取自长度前缀而不是 C 串。

产品侧走查（隔离 `HOME`＋`XDG_RUNTIME_DIR`，`DISPLAY=:0`／`WAYLAND_DISPLAY=wayland-0` 的真图形会话）八档读数：`--help` 退 0 且文案已换；`--new-window new-tab` 与 `split-pane --new-window` 各退 2（诊断带上冲突的两侧名字）；`--new-window` 转交**退 0**（连打两次都 0，第二次排在一条失败请求之后）；`--new-window --cwd /tmp/nope-dir` 退 2 且诊断是「the --cwd directory is not a directory: ...」——**这条来自收侧**，即 `apply_startup_request` 真跑到了验证那一步（转发端只回传收侧的诊断）；`--new-window --profile nosuch` 退 2「profile not found」；冷启动 `Borealis --new-window` 成为首实例并存活；主实例日志 0 字节；隔离目录里只剩 `borealis-1000/{instance.sock,instance.lock}`，无多余文件。⇒ 退 0 是本棒能拿到的最强证据（见 ⑶ 与 §10 批 6 补记第 1 条），「窗真开出来、标签开在哪一扇」不在这里。

⑸ **变异自证**（五则，每则跑完即复原；末次全仓 `grep` 无 `MUTATION` 残留）：
- **M1 折动作的条件改恒假**（`src/cli/startup.cpp`：`if (request.new_window)` 改成永不成立）⇒ **只有** `utest_cli_startup.new_window_flag_folds_into_the_third_action` 红。这一则就是「旗标只剩修饰符、动作仍是 new-tab」的形态。
- **M2 删掉旗标侧的互斥判**（只留子命令侧那一条）⇒ **只有** `utest_cli_startup.new_window_conflicts_with_both_subcommands` 红。两案本是一条 case 里的两组断言，M1/M2 会互相打不到，故拆成两案——这是本棒按第 22 条做的用例整形，不是事后补的口径。
- **M3 编码侧上界写死旧值**（`action > 1U`）⇒ **只有** `utest_cli_handoff.new_window_action_crosses_the_wire_and_the_old_flag_still_reads` 红（新值那一档编不出帧）。
- **M4 解码侧上界写死越界值**（`bytes[5] > 3U`）⇒ **只有** `utest_cli_handoff.request_rejects_bad_magic_version_reserved_and_action` 红（值 3 那格本该整帧拒却放进来）。M3/M4 一上一下，正是 ⑵ 那句「界与表同源」的两半。
- **M5 通道按 C 串取帧长**（`src/platform/posix/instance_channel.cpp` 的 `write_frame` 改用 `strlen`）⇒ **只有** `itest_posix_instance_channel.a_real_startup_frame_crosses_the_channel_unchanged` 红，且红得很快（200 ms 级超时），其余八案照绿——它精确指名「真协议帧含 NUL」这一根轴，既有八案在这根轴上本来没有牙齿。

⑹ **本轮抓到并修掉的三处自身失误**（都不在生产判据上，登记以免下棒误读为「实现踩坑」）：① 一次编辑把已删的旧判定块**插了两遍**（手抄快照而非现读文件所致），事后现读 `src/cli/handoff.cpp` 的编码段确认只剩那一枚上界检查；② 一处公共头编辑顺手吞掉了成员文档与下一条之间的空行；③ itest 处理端最初把 `complete()` 放在持锁区间里，与本文件既有写法不一致，改成锁内快照、锁外回执。另有一条纪律性返工：M1/M2 起初打同一案，不满足「逐条转红」，故先拆用例再重跑注入。

**读数**：`cmake --build --preset linux` 无 error／warning；`ctest --preset linux -E etest_` **99/99**（9.41 s，套件数与批 5 相同）、runner 全跑 **1252/1252**（21.88 s）——与批 5 之后的门相比 **＋0 套件／＋4 案**（1248→1252），全部落在三个既有 TU 的新增段内，既有 1248 案零改动照绿。分套件读数：`--filter utest_cli` 21 案、`--filter instance_channel` 9 案。

**在册欠项（如实记，交批 7）**：① 装配腿的开窗与 tracker 那一跳无自动化证人（⑶），§9 第 5 条七档读数照欠真机；② 混版两档（§10 批 6 补记第 3 条、§22⑵）里「旧 CLI 打新实例」只在用例面证到帧形状，真机未取；③ 三处文档同口径计数（96／1212 → 98／1238 → 99／1248 → **99／1252**）与 WS.03 现状句、D10 裁决的正式回写归批 7（**批 7 已落，落点清单见 §23⑴⑵**）；④ 稿 §7 那条「`--new-window` 仍走拒绝支」的旧候选已随实现失效，就地改口而未删（保持候选清单的历史可读）。

**边界**：批 6 到此收口（第三种动作有写法、有线格式、有落点，纯逻辑与通道两侧共 4 案有证人，产品退出码有读数）；批 7 文档回写与三处同口径计数**已落**（§23）。§9 七条真机走查照欠，本批不称「可用」。


## 23 批 7 台账（文档回写与结项读数，2026-10-21）

⑴ **回写落点（逐处点名，禁「文档已更新」这类结项）**：`SPECIFICATIONS.md` 的 WS.03 现状句（新写整段落地形态＋判据＋边界）、**裁决 7.126**（本条需求的正式裁决，含 D1／D3／D4／D5／D6／D7／D8／D9／D10／D11／D12／D13 的口径与读数）、INTEG.04 现状句（`--new-window` 由拒绝转动作那一档就地改口，案数 12／6／8 → 14／7／9，读数指向 7.126）、WS.10 现状句（`ClosedTabStack` 每窗一份）；`CHANGELOG.md` 新增 **v0.126**；`PLAN.md` 的 WS.03 行（原本只有裸的「M4」）、WS.10 行（原本裸的「M2」）、INTEG.04 行（撤「拒绝仍是 WS.03 欠账」）；`ARCHITECTURE.md` 的 §8.1（末位标签关窗改口）、§8.3（几何记忆落地，原句「布局记忆…属计划能力」）、§8.4（多窗口段落成落地形态）、§11.3 config 边界（「`store.{h,cpp}` 是与框架的**唯一**读写接缝」改口为两枚）、§2.2 模块表（`src/config/` 第二枚接缝与 `src/ui/` 记录器）；`UI_WORKSPACE_INTERACT.draft.md` 判据 ⑳（那条「⇒ 装配层转而关窗口 `Window::close()`」在本仓从未成立，就地改口）；本稿 D3 追加落地状态、§6 第 7 条标已落、§22 的边界句改口。

⑵ **三处同口径计数迁移**：`PLAN.md` 的 WS.03 行、`SPECIFICATIONS.md` 的 WS.03 现状句与裁决 7.126、`CHANGELOG.md` v0.126 三处一律写 **99 项非 etest／1252 案**（注册 102 含 3 条 etest），基线链条 95／1207（v0.125）→ 96／1212（批 1-A）→ 97／1217 → 97／1218 → 97／1221 → 97／1227（批 4-B）→ 98／1238（批 4-C-1）→ 99／1248（批 5）→ **99／1252**（批 6）⇒ 本条净 **+4 套件／+45 案**。`ARCHITECTURE.md` 全文不载门读数（§6 现状快照只写长期两条），故「三处」＝上述三份文档，本条不复述。

⑶ **开工后才读到、与稿面不符的两处（已按代码运行时回填）**：① 稿 D3 推荐档把 `workspace.close_window` 与 OS 关闭钮并列为两条入口——**命令未落**（批 3-B 只落 `workspace.new_window`），且「`Ctrl+Shift+W` 留给关标签」的前提本就不成立，保留键表里那一枚一直是 `WorkspaceCommand::ClosePane`；② 稿 §5 第 4 条与批 4-B 原写「撤账链触发点在 `set_on_frame` 轮询 `should_close()`、不用 `set_on_window_closed`」，落进来的是反的那一条（销毁序理由，见 §18 与 §6 批 4-B 改口）。两处都不是本棒新引入的形态，但都必须在裁决里以运行时为准写出来（AGENTS §4.2 第 12 条）。

⑷ **结项读数**：本棒只改文档（六份 `codespec/*.md`），代码与用例零改动 ⇒ 门读数须与批 6 之后完全相同；实测 `ctest --preset linux -E etest_` **99/99**、runner 全跑 **1252/1252**，与 §22 的读数逐位相同即「文档回写没有夹带行为改动」那一侧的证据。变异自证在本棒不适用（无生产代码改动，AGENTS §4.4 第 22 条那条自证针对门禁脚本与新增判据）。

⑸ **本条需求的结项状态**：批 1–批 7 **全部落地**，`SPEC.FEAT.WS.03` 的现状句与裁决就位。仍欠的两类都不是「没做完」而是「做不了／不做」：**真机走查七条**（§9，未走完不得称「可用」，第 33 条）与 **Windows 主通道**（多窗与几何恢复未编译未实测，推断／未验证）；在册欠项见 §10 与 §22 的观察项清单，其中 `workspace.close_window`、G42 与关闭否决两条框架缺口、`WindowGeometry::mode` 消费点缺失四条**需要另立棒或任务书**，不归本条。**2026-10-22 的进度**：后三条已各自出件（`codespec/FRAMEWORK_TASK_WS03_G42.md`／`_G43.md`／`_G44.md`，见 §3 的出件补记与裁决 **7.127**）——出件不等于回货，本条的「不称可用」账与 §9 的走查清单照旧不改；`workspace.close_window` 仍待另立棒。

## 24 交付后记：三处批 4 在册 `TODO(SPEC.FEAT.WS.03)` 的最后一条「唤醒腿随窗撤账」结项（2026-10-25，裁决 7.152）

**形态改判**：收口不落在装配层加护栏（`main.cpp` 不进 runner，裁决 7.83 那条物理事实不因加件而变），而落在**会话层做成可测不变式**——`Session::close()` 在连接侧 join 返回后清空注入的 `frame_wake_`：join 前沿读线程投出的末路 `on_closed` 唤醒仍可达（`SPEC.FEAT.WS.05` dead-session 浮层要的那一眼不被吞），join 后 `resize`、迟到边沿一类路径永不再点亮帧。批 4-C 那枚捕属主窗引用的长命腿由此不再需要「此刻只可能指着活窗」这句无证人论证：**先 close 再离手的残留档（重启的 `retired`、换视口失败回滚归还那一条）结构性不持任何闭包**，`src/main.cpp` 的 `TODO(SPEC.FEAT.WS.03)` 撤账。

**判据与代价**：`utest_session` 扩 2 案（53→55）——「close 后同一条腿永久沉默（只停叫醒、不吞已排的脏）」与「close 内末路唤醒赶得上清腿前的班车」；两刃变异各有指名红（撤清腿⇒两案红；清腿挪到 join 之前⇒只有末路唤醒案红），逐字节还原复绿。代价档：死会话的 `resize` 脏不再主动排帧，改由窗事件自带的帧消费（形态即语义：会话死了就不该由它调度画面）。结项读数 `ctest -E etest_` **126/126**、runner **1529/1529**；入册见裁决 7.152 与 `CHANGELOG.md` v0.152。
