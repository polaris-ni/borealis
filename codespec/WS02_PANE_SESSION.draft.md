# 分屏多会话（WS.02 生产侧）· 设计稿

> 需求：`SPECIFICATIONS.md` **`SPEC.FEAT.WS.02`**（四义 + 裁决 7.47⑤「切分新 pane 挂新会话」）；相关的在册欠账见 `SPEC.FEAT.WS.09`／`WS.11`／`INTEG.02`／`INTEG.04` 四行的「生产侧分屏仍单会话」措辞。
> 状态：**地基实测完成、口径待裁**（§9 是待人确认的清单，每条已给推荐）。本稿只定形态与边界，不写实现叙述。
> 本件**不新增视觉件**（焦点描边、广播 pill、缩放角标、横幅都由 WS.02 界面腿与 `SPEC.FEAT.WS.08／09` 已有），故按 AGENTS §4.4 的 UI 件节奏不出 `.png／.svg` 设计图；改的是接缝签名与装配取数。
> 地基读数均以当前工作树（`a1f8320`）为准；未实测的标「推断／未验证」。

## 0 范围与不在范围

**在范围**：装配层把「每标签一会话」换成「每 pane 一会话」；`WorkspaceView` 工厂接缝的签名与新增的「就地换视图」入口；三处构造点的接缝同源；逐 pane 语义（重启、会话日志、OSC 标签名、BEL／活动、诊断快照、退出关闭）；串口标签的切分拒绝；`SPEC.FEAT.INTEG.04` 里以本欠账为由拒绝的 `split-pane --cwd`／`--profile` 政策复核。

**不在范围**：分屏结构与各格档案的**持久化**（`SPEC.FEAT.WS.11` 会话恢复，本稿解除其架构阻塞但不开工）；多窗口（`SPEC.FEAT.WS.03`）；跨标签拖拽 pane；把「同会话两格」做成产品形态（今天它就是工厂偷懒的副产物，`SPEC.FEAT.WS.09` 的去重纪律保留为防御）；`--title`／`--size` 一类需求未列的 CLI 参数。

## 1 地基实测（F）

| # | 事实 | 依据（符号／章节） |
|:--|:--|:--|
| F1 | `TabWorkspace` 已是 `{sessions, workspace}` 两件、`sessions` 是 `vector`，但**恒长 1**；成员次序刻意让 `sessions` 后析构（视口 `~TerminalView` 要往会话写撤账空响应） | `src/main.cpp` 的 `TabWorkspace`（注释即该次序的实测记录，`SPEC.FEAT.CONN.12` 批 2） |
| F2 | 三处 `WorkspaceView` 构造点的 `make_pane` 都 `return view;`——新格拿到的正是本标签首格的同一份视图与会话，`TODO(SPEC.FEAT.WS.02)` 挂在第一处 | `src/main.cpp` 的 `create_new_tab_with`／`replace_tab_session`／`tab.undo_close` |
| F3 | **三处 `teardown_pane` 不同源**：新开标签那处撤广播成员，重启与撤销重开那两处是空 lambda ⇒ 在这两类标签里关一格，广播成员与横幅列名一直留着（`stream` 回 `nullptr` 只让扇出变哑，账没撤） | 同上三处的 `ws_hooks.teardown_pane`；`broadcast_router.remove`；`SPEC.FEAT.WS.09` §1 第 7 条 |
| F4 | 三处 `dispatch_grid` 都忽略 `PaneId`，恒 `session->resize(...)` ⇒ UI 腿已按格去抖，装配侧把**首会话**的尺寸当成每格的尺寸 | `ws_hooks.dispatch_grid`；`ui::GridSizeDebounce`；`SPEC.FEAT.XFER.01` |
| F5 | `split_pane` 把工厂返回的**同一枚** `shared_ptr` 再 `add` 一次；`aurora::Container::add` 只做 `children_.push_back`（无重复入子防护），`remove_child` 按控件地址只摘**首个**匹配 ⇒ 生产切分是「同一 widget 两个子节点」，关一格摘掉一个 Node、视图仍被另一个 Node 持有 | `src/ui/workspace_view.cpp` 的 `split_pane`／`close_pane`；Aurora `widget/widget.h` 的 `Container::add`／`remove_child` |
| F6 | `views_` 是 `PaneId → TerminalView *`，`pane_of(widget)` 按地址反查、命中即返回首个 ⇒ 同一视图两格时反查有歧义；`split_focused` 因此特意读 `tree_.focused()` 而不是按视图反查，其注释已写明这条成因 | `workspace_view.{h,cpp}` 的 `views_`／`pane_of`／`split_focused` |
| F7 | 逐格尺寸的 UI 腿今天就绪：`flush_grid_sizes` 按 `PaneId` 交出期望行列，`close_pane` 已 `debounce_.forget(pane)` | `workspace_view.cpp` 的 `flush_grid_sizes`／`close_pane` |
| F8 | `Session` 不可拷贝不可移动，`TerminalView` 构造吃 `Session &` ⇒ 「哪条会话属于这一格」在建视图那一刻定死，工厂必须**先造会话再造视图** | `session/session.h`；`terminal_view.h`；`SPEC.FEAT.WS.05` 装配注释 |
| F9 | 装配层按「整张标签」取会话的点共六处：`current_tab_session()` 读 `sessions.front()`（日志两条钩子与命令标题都经它）、`create_new_tab` 的 OSC 7 继承读 `sessions.front()`、`has_live_session` 取 any、`diagnostics.open` 按 `sessions` 的 **vector 下标 +1** 当 pane 号显示、`set_on_frame` 里 BEL／活动／OSC 标题逐会话循环（标题是**最后写者胜**，与 PaneId 无关）、退出时双层循环 `close()` | `src/main.cpp` 上述各处 |
| F10 | 每标签的四张接线表按 `TabId` 存（`ssh_profile_by_tab`／`ssh_secret_by_tab`／`serial_by_tab`／`telnet_by_tab`）+ `tab_kind`；SFTP 面板的 `current_ssh_profile`／`ssh_tab_alive` 也按 `TabId`；`perform_close` 撤这六笔加 `remove_tab` 共七笔账 | `src/main.cpp`；`sftp_hooks`；`SPEC.FEAT.CONN.09`／`INTEG.02` |
| F11 | `replace_tab_session` 重建**整张标签**的 `WorkspaceView`（单叶起步）⇒ 今天的「重启」在结构上必然拆掉已有分屏；它的注释把「同 TabId 覆盖、`tab_strip` 不动」记为 WS.05 的语义 | `src/main.cpp` 的 `replace_tab_session`；裁决 7.99 D6 |
| F12 | 串口第二腿开同一设备在两平台上都不是「可用」形态：Win32 `CreateFileA` 的 `dwShareMode` 传 0 ⇒ 已被持有的口第二次开必失败；POSIX `::open` 只带 `O_RDWR \| O_NOCTTY \| O_NONBLOCK`、无排他 ⇒ 第二次开**会成功**但两腿争同一份输入流（后者是据标志位的**推断／未验证**，无实测证人） | `src/platform/win/serial.cpp`；`src/platform/posix/serial.cpp` |
| F13 | SSH 的 `SshProfile` 与（可选的）解密后的 secret 已在 `ssh_profile_by_tab`／`ssh_secret_by_tab` 留档，重拨一格不必再问凭据；Telnet 档案同理且无凭据；Local 档案与 `LocalTerminalSpec` 同构可直搬 | `restart_ssh_tab`；`open_*_tab`；`make_serial_session` |
| F14 | 生产侧多会话**没有任何自动化证人**：`main.cpp` 不入 `borealis_test_runner`（裁决 7.83），`itest_workspace_layout` 的工厂造的是独立会话、`itest_workspace_broadcast` 另有 `shared_view` 档复刻今天的生产形态 ⇒ 判据只到 UI 腿 | 两份 itest；裁决 7.83 |
| F15 | `cli::startup_policy_error` 现在拒绝 `split-pane --profile`／`--cwd`，理由写在册文档里就是本欠账；`apply_startup_request` 的 split 分支已在用 `split_focused(Horizontal)` 并靠 `pane_count()` 变化判成败 | `include/borealis/cli/startup_request.h`；`src/main.cpp` 的 `apply_startup_request`；裁决 7.124④ |

## 2 目标口径（四条不变量）

1. **一格一会话**：任意 `PaneId` 对应一份自己的 `Session` 与 `TerminalView`；「两格同一会话」在生产装配路径上不再可能出现（`SPEC.FEAT.WS.09` 的按会话去重保留为防御，判据不变）。
2. **格的身份先于会话**：广播成员资格、焦点、缩放、去抖表都按 `PaneId` 记账；换掉这一格的会话不改这一格的身份（裁决 7.99 D6 的「换会话不换身份」从标签级降到 pane 级）。
3. **接缝同源**：一处工厂、一处尺寸下发、一处回收，被首格／切分／重启／撤销重开四条路共用；「三处构造点各写一遍 lambda」这种形态（F3）由结构消除，不靠人记得。
4. **会话活过视口**：任何销毁视图的路径，其时该格会话必须仍在表里（析构次序结论见 §5，继承 F1 那条实测）。

## 3 数据结构与接缝

**装配层**（全部留在 `src/main.cpp`，不外溢成新头——本件没有可独立单测的新算法，纯逻辑件为零）：

- `TabWorkspace` 换成 `{ std::map<PaneId, std::shared_ptr<Session>> sessions_by_pane; std::shared_ptr<WorkspaceView> workspace; PaneSessionFactory make_session; }`。`sessions_by_pane` 必须仍声明在 `workspace` **之前**（F1 的析构次序结论不因换容器类型而失效，成员次序而非容器类型才是承重的）。
- `PaneBundle { std::shared_ptr<Session> session; term::LineEnding line_ending; }`，`PaneSessionFactory = std::function<std::optional<PaneBundle>()>`——**返回空即这一类标签不能切分**，工厂缺席时 `make_pane` 回空、树不动（`split_pane` 已有这条 fail-closed 支路，F15 的 CLI 成败判定靠的就是它）。
- 六条开标签腿各交一份工厂：缺省本地腿的工厂按 `origin` 格的 OSC 7 走 `ui::choose_start_directory`（`SPEC.FEAT.INTEG.02` 的优先级链同源，不在分屏侧另写一遍），档案本地腿按档案目录，SSH 腿按 `ssh_profile_by_tab` + 留档 secret 重拨，Telnet 腿按 `telnet_by_tab` 重拨，串口腿交 `nullptr`（F12）。
- 工厂**自己**把新会话登记进 `sessions_by_pane` 后再返回视图（先会话后视图，F8）；登记发生在 `split_pane` 里 `tree_.split` 之前，所以 `tree_.split` 失败那条回收路必然走 `teardown_pane`，账一并撤掉。

**UI 腿**（`src/ui/workspace_view.{h,cpp}`，两处签名／入口变更）：

- `PaneViewFactory` 由 `(PaneId pane)` 改为 `(PaneId pane, PaneId origin)`，`origin` 即被切那一格（`split_pane` 的 `target`）。装配层因此能拿到 `origin` 的会话读 OSC 7，而不必在工厂调用那一刻反过来猜「框架焦点此刻是谁」（F5／F6 那两条歧义正是这么来的）。生产侧本批仍返回同一视图，行为不变。
- 新增 `auto replace_view(PaneId, std::shared_ptr<TerminalView>) -> bool`：换掉该格的视图并保住格的身份——`views_` 改指、旧 Node 就地替换、`debounce_.forget(pane)`、焦点与广播 pill 按原成员资格重算、`zoomed_` 若正是这一格保持缩放（它按 `PaneId` 记，天然随格不动）。这是 §6 重启腿不再拆树的前提。**回 false 即什么都没动**，四种情形各一条：新视图为空、该格不存在、交进来的就是当前那份视图、这一格与别格共用同一份视图——子节点身份只能靠视图地址反查，共用时无从分辨该换哪一个，而今天的生产工厂正是这副形态（F5／F6），故宁可拒绝也不猜。三条承重次序写在声明处：先清框架焦点再销毁旧视图、手写换子后补 `note_pending_mount()`、按格清去抖旧账。

## 4 每类标签的切分语义

| 标签类型 | 切分 | 新格会话从哪来 | 依据 |
|:--|:--|:--|:--|
| Local（缺省 shell 与本地档案） | 允许 | 新建一条 PTY，目录＝被切那格的 OSC 7 → 档案目录 → 配置值 | F13；`SPEC.FEAT.INTEG.02` |
| SSH | 允许 | 按同一份档案 + 留档 secret 重拨一条连接（新 TCP 会话） | F13 |
| Telnet | 允许 | 按 `telnet_by_tab` 的档案重拨，行尾仍固定 `Crlf` | F13；裁决 7.112 D8① |
| 串口 | **拒绝** | —（工厂缺席，`make_pane` 回空，树不动） | F12 |

串口这一条是本稿唯一「用户按下 `Ctrl+Shift+D` 而屏上什么都不发生」的支路。推荐口径：拒绝只发生在工厂返回空这一处，装配层补一条 `AURORA_LOG_INFO` 说明「串口设备独占，不切分」，**不弹框**——多一个提示件就多一个视觉件与本件的最小意图冲突，且真机走查时这一条以「切不开」为判据已足够显形。

## 5 生命周期与撤账纪律

- **销毁次序**：`~WorkspaceView` → 各 `~TerminalView`（写撤账空响应）→ `sessions_by_pane` 析构。由 §2 第 4 条与 F1 的次序结论保证；`make_pane` 闭包**不再需要**按值捕获 `shared_ptr<Session>` 来替视口撑住（那份捕获原本正是在补「表里只有一份会话」这个洞），会话的拥有权只在 `sessions_by_pane` 一处。
- **关一格**（`close_pane` 之后）：撤广播成员（`broadcast_router.remove`）+ 从 `sessions_by_pane` 摘掉该格会话。两条都必须发生，且**只此一处**定义（`teardown_pane` 由装配层提供一份，四条构造路径共用，F3 的成因消除）。摘会话必须在视图已销毁之后（F1：`remove_child` 在 `teardown_pane` 之前，次序天然正确，本稿不改动 `close_pane` 内部次序）。
- **关标签**：`perform_close` 的七笔账不变（F10），`tab_workspaces.erase` 一次带走该标签全部格的会话；`broadcast_router.remove_tab` 仍撤全部成员。
- **退出**：`app.run()` 之后的双层循环改成遍历 `sessions_by_pane` 全部值，逐格 `close()`。
- **登记时序**：工厂在 `split_pane` 栈内写 `tab_workspaces[id].sessions_by_pane`。这依赖 `std::map` 的节点稳定性（插入不搬动其他节点，故正在执行的 hook 不悬垂）。这一条写成注释留在代码里，因为把 `tab_workspaces` 换成 `unordered_map` 或换成按值搬运的容器就会当场破——与 F1 那条注释同级别的承重结论。
- **重启腿的调用上下文**（批 1 实测）：`replace_view` 里「先清焦点再销毁旧视口」与「焦点归还本格」两条，只有在**派发栈内**才写到派发用的那份 `FocusManager`。栈外 `current_focus_manager()` 为空，`resolve_focus_manager` 退到按根缓存的兜底实例，派发用的那份因此仍指着刚销毁的旧视口——框架的 `live_focused()` 有弱指针守卫，读成空而不致悬垂（实测不崩），但用户此后第一下键入无处可去，得由装配层补一次设焦。生产的 `session.restart` 由菜单／按钮的指针事件带动，天然在栈内；两份 itest 驱动台都以 `set_current_focus_manager` 包住这一跳，判据测的才是真链路而非兜底件。

## 6 逐 pane 语义收口（现状 → 目标）

| 点 | 今天 | 目标 |
|:--|:--|:--|
| 会话日志两条钩子与命令标题 | `sessions.front()` | 焦点格；无焦点格回空（条目不出现，与闸关同档，裁决 7.116 F17） |
| `session.restart`／dead-session「重启」 | 重建整张标签的工作区（拆树，F11） | 只换焦点格：`make_session` 造新 bundle → 新视图 → `replace_view`；广播成员与 `tab_strip` 一格不动 |
| 新标签的 OSC 7 继承 | `sessions.front()` | 焦点格（`SPEC.FEAT.INTEG.02` 的「多会话落地后改取焦点 pane」那条 TODO 就此闭合） |
| 标签名的 OSC 来源 | 逐会话循环，vector 最后写者胜 | **焦点格独占**该槽位：每帧写焦点格的当前标题（可空 ⇒ 让位给下一优先级来源） |
| BEL／活动角标／退出角标 | 逐会话 any | 不变（any 就是想要的语义） |
| 诊断面板快照 | `sessions[i]`，标题里的 pane 号是 vector 下标 +1 | 按 `PaneId` 遍历，标题就是真实 PaneId |
| 排帧 `on_frame` | 按 `pane_count()` 遍历视图 | 不变（视图本就按 pane 存） |
| SFTP／隧道／密钥面板的「当前 SSH 标签」 | 按 `TabId` | 不变（档案是标签级事实，同标签两格 SSH 用同一份档案） |
| 广播去重与「窗格 N」列名 | 依赖同会话 identity 去重 | 语义不变，防御性保留（F14 的 `shared_view` 档继续担保） |
| `--cwd` 与 `--profile` 的 split 政策 | 一律拒绝（F15） | 见 §7 |

## 7 CLI 政策复核（`SPEC.FEAT.INTEG.04`）

`split-pane --cwd`：随本件放开，目录优先级与 `new-tab` 同一条链（CLI > 档案 > OSC 7 继承 > 配置），实现在装配侧用「一次性覆盖」形态喂给本地腿工厂（与 `create_new_tab(cwd)` 里那个不写回任何一处的 `--cwd` 同形），`cli` 纯逻辑件只需从 `startup_policy_error` 删去该拒绝支并补证人。

`split-pane --profile`：`Local` 档案放开（本地腿无凭据链）；非 `Local` 档案维持拒绝，但拒绝语要换成**此刻真实成立**的那一条——新格的凭据询问链尚未裁（异步弹框与工厂同步返回互斥），而不是继续挂着「WS.02 未实现」这句已经过期的理由（AGENTS §4.2 第 12 条：冲突以代码运行时为准并回填）。同档案切分不受影响（留档 secret 已在表里，F13）。

`--new-window`：与本件无关，继续拒到 `SPEC.FEAT.WS.03`。

## 8 判据与自证

自动化能担保的：UI 腿的工厂收到正确的 `origin`（且 `origin` 跟着**模型焦点**走——栈外的 `split_focused` 只写得进模型投影，框架读数会落后一拍）、工厂返回空时树不动、`replace_view` 保住格身份／成员资格／焦点／缩放态并给新会话补一次 `resize`、共用视图时拒绝交换、`teardown_pane` 撤两笔账、逐 pane 尺寸下发只碰本格会话（`itest_workspace_layout`／`itest_workspace_broadcast` 扩案 + 生产形态的 `shared_view` 档改造）。

自动化担保不了的（F14）：`main.cpp` 里那份工厂是否真给每一格造了正确类型的会话、串口标签切不开、重启只换一格。结项文须写明「生产装配最后一跳无自动化证人」，并以下面这份真机走查清单收口——**未走完不得称「可用」**（AGENTS §4.4 第 33 条）：

1. 本地标签 `Ctrl+Shift+D` 出两格，`vim`／`top` 各开一格，行列数互不串（`XFER.01` 的逐格判据）；
2. 在 A 格 `cd /tmp` 后切分，新格起始目录为 `/tmp`；
3. 关掉缩放格、拖把手、`Ctrl+Alt+方向键` 步进后，剩余格的尺寸仍然对；
4. 重启（`Ctrl+Shift+R`）只换焦点格，另一格内容与分屏结构不动；
5. 串口标签按切分不开、日志有那句拒绝；SSH 标签切分能重拨、不断另一格；
6. 广播组跨两格开启后关其中一格，横幅列名与计数当场跟上（F3 那条路）；
7. 标签名跟随焦点格的 OSC 标题、切回去不残留上一格的名字。

## 9 待人裁决（每条已给推荐，按推荐即走）

1. **串口标签的切分**——推荐：拒绝 + 仅日志，不弹框（§4）。代价：用户看不到「为什么不开」，只有真机走查与文档能解释。
2. **标签名的 OSC 归属**——推荐：焦点格独占、可写空（§6）。备选：首格独占（切焦点时名字不动，代价是新格的名字永远不上标签）。
3. **重启的粒度**——推荐：由标签级换成 pane 级（§6），`WS.05` 的「同 TabId 就地换会话」措辞相应回填为「同 (TabId, PaneId) 就地换会话」。
4. **`split-pane --profile` 的非本地档案**——推荐：本棒维持拒绝、把拒绝语改成此刻真实的理由（§7）。备选：把凭据询问做成新格工厂的异步补链，属独立一件，不塞进本意图。
5. **是否改 `WorkspaceView` 的两处接缝**（`origin` 形参与 `replace_view`）——推荐：改（§3）。不改的替代方案是让装配层在工厂里现读框架焦点并整标签重建，两者都会把 F5／F6 那两条歧义固化成产品行为。

## 10 分批

1. **批 1（UI 腿）**：§3 的两处接缝 + 对应用例（含既有两份 itest 的工厂签名同步）。生产行为不变，可独立验证。
2. **批 2（装配层每 pane 会话）**：§3 数据结构与六条工厂、§5 的 teardown 同源与逐 pane 尺寸。本批结束即「切分 = 新会话」，重启仍是标签级。
3. **批 3（逐 pane 语义 + CLI）**：§6 全部行、§7 政策复核。
4. **批 4（收口）**：`SPECIFICATIONS.md` 的 WS.02 现状句与裁决、`CHANGELOG.md` 新版本、`PLAN.md` 的 WS.02／WS.11／WS.09／INTEG.02／INTEG.04 措辞回填、`ARCHITECTURE.md` 数据流那一行、本稿末尾追加的「§11 结项读数」一节（该编号在本批才存在）。

每批独立分层提交；结项读数以「ctest 全绿 + runner 案数增量 + 变异证人逐条转红」三件齐为凭（AGENTS §4.4 第 22 条）。
