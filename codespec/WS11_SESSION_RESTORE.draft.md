# WS11_SESSION_RESTORE.draft.md — `SPEC.FEAT.WS.11` 会话恢复设计稿（批 0：稿与地基实测）

> **状态**：**批 0 出稿并自决按推荐档推进（2026-10-22）→ 批 1 已落地**（`ui::PaneShape` + `describe()` / `restore()`，读数见 §8 末段）。§3 的 D1–D10 每条已给推荐①与其代价，人已给出的常存令是「按照顺序持续推进，有任何疑问现在澄清，后续再遇到问题则你自行按照推荐去做」，故本轮按既有棒次惯例**先出稿再自决按推荐档推进**；稿面每条的推荐理由与代价都写在行内，结项时按 `AGENTS.md` §4.2 第 11 条回写为裁决。
> **本棒的意图只有一件事**：让应用重启（含崩溃后）起来的还是**上次那套标签布局**——连接类型、档案、分屏结构、各标签标题。需求原文括号里的四件事之外，本棒一律不做（§6）。
> **需求原文**（`codespec/SPECIFICATIONS.md` 的 `SPEC.FEAT.WS.11`）：「应用重启（含崩溃后）恢复上次的标签布局（连接类型/profile/分屏结构/各标签标题）；会话内容不恢复（本地终端无源可恢复）；恢复开关可配。区别于已裁剪的配置同步：仅本机记忆，不跨设备。配置本身损坏时的降级行为见 `SPEC.FEAT.PREF.07`。」
> **两条在册硬约束**（`codespec/PLAN.md` §3 的 `SPEC.FEAT.WS.11` 行原文）：「开工时按『每标签连接规格 + `PaneTree` 快照』重排；SSH／串口／Telnet 那一腿的凭据重取口径须先裁（句柄在 OS 凭据库，不落盘），且**不得把明文凭据写进快照**（`SPEC.FEAT.CONN.09`）」。前者定形 D4／D5，后者定形 D4 的凭据半边并在判据 ⑥ 给证人。

---

## 0 地基实测（F1–F22，2026-10-22，Linux GCC + POSIX + Headless 通道；Win32/MSVC 一律标推断／未验证）

| 号 | 读数 | 它决定了什么 |
|:---|:---|:---|
| **F1** | **全仓无本棒的桩**：`TODO(SPEC.FEAT.WS.11)` 在 `src/`、`include/`、`tests/` 三处 grep 零命中；`SPECIFICATIONS.md` 的本条现状句空白，`PLAN.md` §3 那行明写「未开工（排序取舍已解除，2026-10-21）」 | 没有既有实现要兼容，形态全由本稿定；同时意味着「恢复开关」这一枚键**从不存在**，它是本棒唯一的新偏好键（D3） |
| **F2** | **载体有三条路，其中两条各撞一条既有特性**：`config::Store::Impl::store()` 每次落盘都先 `snapshot_current()` 拍一份快照进滚动环（`kSnapshotRetention = 5`），且 `settings_to_json()` 这一份文档同时是**写盘内容**与**导出件正文**（裁决 7.87④ 的同源设计） | ① 把「每帧都在变的运行期布局」塞进 `borealis.json` ⇒ 布局改动会把 PREF.07 那五格快照环烧穿，用户真需要回滚配置时现场早已被挤掉；② 同源那份文档又会让布局**跟着导出件跨机迁移**，而需求明写「仅本机记忆，不跨设备」。两条都是否决而非代价，故 D1 取「另起第三枚文件」 |
| **F3** | **第三枚 `Preferences` 已有完整先例**：`src/config/window_geometry_store.{h,cpp}`（私有头 83 行 + 实现 49 行）只做三件事——按默认位置绑 `<配置目录>/windows.json`、`preferences()` 交框架裸指针、`flush()` 提交。它的文件头三条口径与本棒同族：几何**不进 schema**（理由写的是「本机屏幕相关值，跨机导出即坏值，所以既不在 `borealis.json` 里也不在导出件里，**这条是特性不是漏项**」）、键在 `Preferences` **顶层**而不是分组、落盘时机归调用方 | 本棒的落盘件照这一枚的形态长，唯一区别是它要**自己写 JSON**（几何那份由框架的 `save_window_geometry` 链自己写，本仓只出指针）。`禁复制禁搬移`（内含互斥量）与「声明必须早于 `app`」两条次序约束同样继承 |
| **F4** | **提交时机的既有先例只有两处，而其中一处的注释已经把崩溃那件事说清了**：`flush_window_geometry()` 调在 `on_window_closed` 的 ⑦ 步与 `app.run()` 返回之后，前一处注释原话——「为什么要这一处而不是只留退出那一处：关一扇之后进程还可能跑很久，而**崩掉的那一次不会有退出腿**」 | 需求那句「含崩溃后」因此不是修辞：**只在退出腿写必然丢崩溃那次**。D2 必须给「存活期也写」的一档；同时框架 `Preferences` 的落盘是「内存改写 + 主动 `flush()`」模型（无自动写穿），所以「什么时候 flush」这件事在本仓永远是一处需要拍板的口径 |
| **F5** | **`ui::PaneTree` 没有任何序列化接缝**：公开面只有 `pane_count`/`panes`/`has_pane`/`split`/`close`/`focused`/`set_focus`/`container_of`/`layout`/`move_divider`/`equalize`；`struct Node` 是只在 `src/ui/pane_tree.cpp` 里定义的前置声明，**每层沿轴的相对长度**（即用户拖把手拖出来的那个比例）没有任何读取入口 | PLAN 那句「`PaneTree` 快照」在本仓**尚无对应物**：要么给这枚纯逻辑件加一对公开的描述／重建件（D5①，结构和比例都能还原，代价是扩公共面并配单测），要么绕开树、由装配层自记切分脚本（D5②，结构精确而比例必丢）。第三条「不记分屏结构」直接违需求括号里那四个字，本稿排除 |
| **F6** | **`ui::WorkspaceView` 的公开面被注释划成了「观测面：用例的判据入口，装配层不需要」**，只有 `pane_count`/`focused_pane`/`view_of`/`current_layout`/`zoomed_pane`/`flush_grid_sizes`/`split_focused`/`replace_view` 等；`current_layout()` 给的是 **dp 矩形**与把手（含 `container`/`slot`/`axis`），而 `ContainerId` 只在树内唯一、只增不复用 | ① 从布局产物**反推**拓扑能拿到父子序与轴，但比例是「本次窗宽 ÷ 该窗宽」的函数，换一扇宽的窗再写回就是失真（F7 同源）；② `split_pane(target, axis)` 是私有，装配层能驱动的只有 `split_focused(axis)`——即「重放切分」这一路径的形态必然是 `set_focus(pane)` → `split_focused(axis)` 交替，而 `set_focus` 要的 pane 标识此刻还不存在（要先把视图与会话建出来）。这条决定了批 5 的接线形状 |
| **F7** | **切分工厂拿得到 `origin` 而拿不到 `axis`**：`PaneViewFactory = std::function<shared_ptr<TerminalView>(PaneId pane, PaneId origin)>`，`origin` 是 WS.02 批 1 为装配层**专门扩进去**的一枚形参（注释写明「少这一个参数，工厂就只能反过来猜框架焦点」） | 「扩这一枚接缝的形参」在本仓是有先例的**接缝扩容**而不是新抽象。若 D5 落②（切分脚本），则 `axis` 是第二条要扩的形参；落①（树自描述）则一处都不动。这条把 D5 的代价差从「估计」变成「两处代码位置」 |
| **F8** | **每标签的连接规格分散在四张装配层表，而本地腿那张表不存在**：`tab_kind`（`TabId → conn::ConnectionType`，WS.04 认定的唯一类型来源）、`ssh_profile_by_tab`（`TabId → conn::SshProfile`，**整份字段而不是 id**）、`serial_by_tab`（`TabId → SerialTabBinding{profile, connection*}`）、`telnet_by_tab`（`TabId → conn::TelnetProfile`）。本地标签的料在**开标签那一刻现算**（`local_start_directory` 的「CLI > 档案 > 继承 > 配置」算式 + OSC 7 继承），切分期间的 `split_spec_override` 是一次性瞬态、用完当场 `reset()` | 「恢复一格的连接规格」在 SSH／串口／Telnet 三腿上**已有现成的账**（且存的已是整份档案），本地腿则一格都不留。PLAN 那句「每标签连接规格」要兑现，缺的正是本地这条：同标签内两格可以跑在不同目录（分屏继承 OSC 7），故这份账的粒度是 **per-pane** 而不是 per-tab（D4 的本地半边） |
| **F9** | **明文口令在内存里有一枚活口，且它的位置就在恢复要读的那张表旁边**：`ssh_secret_by_tab` 存 `TabId → std::optional<std::string>`（SFTP 面板复用同一次询问的材料），其注释自陈「材料是**内存副本**，不落盘、不进日志、连接关闭即清」；本期 OS 凭据库后端未落，`CONN.09` 的运行期形态是 `UnavailableCredentialStore` ⇒ 任何 `store()` 只回 `AskEveryTime` 哨兵、`retrieve()` 恒空 | 恢复 SSH 腿**必然**重新走一次凭据询问——这不是本棒的缺陷而是 `CONN.09` 的现状，D4 因此把「重取凭据」定义为「复用既有 `connect_profile` → `ask_secret` 那一条链」而不是新造一条静默重拨。任何让恢复「不弹框」的方案都得把这份内存副本持久化，即违 `SPEC.FEAT.CONN.09` 与 AGENTS.md §4.5 第 24 条，本稿一律排除 |
| **F10** | **档案 id 不保证指向档案表**：quick connect 走 `conn::quick_connect_profile(...)` 现造一份 `Profile` 交 `connect_profile`，**不落** `settings.profiles`（该表的唯一写入方是侧栏 `sidebar_hooks.persist` 的整表替换）；`RecentConnection::profile_id` 的注释自陈「quick connect 用其临时 id」 | D4 只存 id 那一档（②）对这一类标签**结构上取不回**任何料——恢复出来是丢标签还是丢连接都取决于怎么补这个洞，且补法要新造一份「临时档案表」。故推荐档取「id 与字段**双写**，id 命中档案表时以表里那一份为准」：既保住「档案被改名后再重启看到的是新名字」这条单一真值源，又让 quick connect 那种无表可依的连接恢复得出来 |
| **F11** | **`SshProfile` 整份落盘是既有形态且天然不含明文**：`ssh_profile_to_json` 写 host/port/user/auth_method/identity_file/agent_forwarding/known_hosts_policy + 一枚 `credential` 块（`credential_to_json` 只写 `kind`/`vault_id`/`profile_id`/`label`）；`SecretHandle::is_plaintext()` 恒 false 是对外承诺 | 快照复用这三块档案形态（SSH／串口／Telnet）**不会**新增任何明文字段，也不会新增键名撞禁列名单的机会——因为撞名的字段在本仓结构上不存在。D4① 的实现代价因此主要是「把 `store.cpp` 匿名区那三块搬成可共用的形态」，而不是「重新设计一套档案序列化」（F12） |
| **F12** | **`store.cpp` 的全部 `*_to_json` 与 `ScopeReader` 都在匿名命名空间**：`settings_to_json` 与 `Store::Impl::store()` 两处**各自**列一遍七个域（`kDomainKeys` 与读取侧配对），helper 一个都出不了这个 TU；`find_credential_key` + `kBannedKeyNames{password, passphrase, secret, token, private_key}` 那道闸守的是**外部交回**的文档（自家写出的文件里是永不开启的分支） | ① 「把档案序列化搬出匿名区给第三个 TU 用」会同时动 `Store::Impl::store()` 与导出件两条既有判据面（`utest_config` 的往返等值全打在那两处一致性上），属跨意图改动，本棒不做（AGENTS.md §4.1 第 3 条）；② 快照件因此要**自己写一份**档案块的编解码，代价是「同一份 `SshProfile` 有两处 JSON 形态」——这是本稿明写的在册第二真值源风险，缓解办法见判据 ⑦ 那条「两处的键名集与取值域逐字相等」的证人 |
| **F13** | **凭据审计的自动化形态只到「键名」这一维**：`utest_config` 的 `key_dirs` 案用 `collect_keys(parsed)` 扫**整份文档**的键名、逐个对五枚禁列子串；AGENTS.md §4.5 第 24 条要求的「配置目录 grep 无明文凭据」那枚**目录级**用例全仓未落 | 新增一枚落盘文件把这条在册欠账的射程扩大了一格。本棒的处理：给快照文件配**同形态**的键名案（判据 ⑥），并把「目录级扫描」那枚用例留在 §6 的在册边界里（它属 `CONN.09` 的余腿，不该由本棒顺手做掉） |
| **F14** | **WS.10 的 `ui::ClosedTabSpec` 是「重建连接所需最小信息」的现成先例，但三处不能照抄**：字段是 name / index_in_strip / local_shell / startup_directory（**有效值 + 空即回落 `settings.connection`**，见 `undo_close_tab` 里那两行三元式），另带 `TODO(SPEC.FEAT.CONN.02)`（SSH 档案字段至今没追加）、`closed_at` 是 `std::chrono::steady_clock`、且那枚栈**挂在 `WorkspaceWindow` 上、只在内存** | ① 「存有效值 + 空即回落」这条字段口径本棒沿用（它同时是恢复语义里最省的一档）；② `steady_clock` 跨进程无意义 ⇒ 快照里任何时间戳只能是 Unix 秒（`RecentConnection::used_at` 那一档）；③ WS.10 那条栈没落盘，本棒不复用它、也不把恢复做在它的宿主上（一内存一磁盘，两回事） |
| **F15** | **采集所需的遍历一处都不用新造**：`app.set_on_frame` 里已有「全部标签 × `sessions_by_pane`」那两层循环（排 outbox、排通知、取 BEL、取 damage 都在其中），`on_window_closed` 有整条撤账链，退出序按窗走（裁决 7.126）；标签序与标题由 `ui::TabStrip::tabs()` 给（深度优先的**条内序** + `resolve_tab_name`），焦点格由 `focused_pane()` 给 | 「读现成的账」是本棒采集腿的全部工作量。这条也是**不新增遍历原语**的依据——若批 5 里冒出「要在工作区里另存一份标签序」的需求，就是与 `TabStrip` 造第二真值源，按 F12 同一条理由拒绝 |
| **F16** | **启动路径上有三个「先有窗再有内容」的时刻，恢复要插进第一类**：首标签在建窗后立刻由 `create_new_tab(*workspace_windows.front(), cwd)` 建出；冷启动 CLI 请求在 `app.run()` **之前**经 `apply_startup_request` 落地（注释明写「首帧直接画出目标标签或那一刀切分，不出现『先空一帧再补一格』」）；转交请求在帧边界排空。`cold_startup_profile_tab` 那三行布尔专门在处理「CLI 的 `--profile` 要把首标签**换掉**而不是多出一条没人要的缺省标签」 | 恢复与 CLI **争的是同一格首标签**。不拍这一条，`borealis --profile gw` 的可见结果是「一堆旧窗 + 你要的那一格」，而它那句 `cold_startup_profile_tab` 的换形逻辑会当场失效（要换的那一格不再是唯一的一格）。D7 因此是本棒的形态必决项而非可选项 |
| **F17** | **降级提示的宿主只吃一份报告类型**：`ui::StartupNotice::show_if_needed(const config::LoadReport &)` 的入参类型是配置装载那份 `LoadReport`（四态 + `corrupt_backup` + `rejected_keys`），文件头四条 WHY 的第一条就是「**收 `const config::LoadReport &` 而不是收一份散字段**：『四态里哪两态弹』是本件的判据本体」，且 `message` 是 ASCII 英文诊断、按 S13① **不上界面** | 需求把降级只写在「**配置本身损坏**」那一句，而快照文件不属配置本体（F2 已把它分家）。若要让快照损坏也弹同一枚框，就得扩这枚件的入参形态并改它的用例面（`itest_startup_notice` 现按 `LoadReport` 手工构造驱动），收益抵不过代价 ⇒ D8 取「整体丢弃 + 只落日志」 |
| **F18** | **`EffectLevel::NextLaunch` 这一档现在只有一句消费方**：`settings_catalog.h` 里该档的注释原文「值只在**建窗之前**取用一次，本进程内没有任何重放入口」，且明写「唯一消费点是面板角标那一支」；追加它的理由是 PREF.05 的语言档（词条在建任何控件之前装填） | 恢复开关是这一档的**第二个消费者**（改了它，本进程既不重放也不影响已经开起来的那些窗），且 F18 那句「唯一消费点」要被就地更正为两处。这条同时是 D3 里「档位不新造第五档」的依据：`NextLaunch` 表达得准，不需要为它另立一档 |
| **F19** | **一枚新偏好键的固定代价是六处 + 两处计数闸**：`include/borealis/config/settings.h`（字段与缺省）→ `src/config/store.cpp`（读写两半）→ `src/config/form_transfer.cpp`（Binding）→ `src/ui/settings_catalog.cpp`（表里那一行）→ `src/ui/settings_i18n.cpp`（两档词条）→ `src/main.cpp`（消费点）；另加 `utest_settings_catalog` 现钉的 `keys.size() == 70U` 与 `ConsumerStatus::Absent` 行数 `== 19U`，以及 `codespec/UI_SETTINGS.draft.md` §2 那一行 | 新键的落点与改口清单是**已知量**，本棒不为此发明新形态。开关档取 `Wired`（D3）⇒ 70→71、19 不动，`EffectLevel` 那一列的角标案要新增一档 `NextLaunch` 的第二消费者 |
| **F20** | **两样相邻状态都已被明文排除在落盘之外**：`WorkspaceView::zoomed_` 注释写「按标签独立、不落盘」（WS.08），广播组成员「跨标签保存、**不落盘**」（WS.09，裁决 7.121）；`connection.trusted_url_hosts` 走 `kNonPreferenceLeaves` 例外通道，面板不画 | 恢复的射程就是需求括号里那四件事。缩放态与广播组若跟着恢复，就是「图上画了需求没要的东西」（AGENTS.md §4.1 第 1 条），且会把一条已明文登记的「不落盘」口径偷偷翻掉——本稿在 §6 把这两条列成显式的「不做且理由在册」 |
| **F21** | **无头证人形态已知而装配层永远无证人**：`itest_workspace_layout`（1491 行）以 `HeadlessSurface` + `FakeConnection` 驱动「键位 → 树 → 矩形 → 子视图 → 会话尺寸」这条链，`itest_workspace_window` 已跑通两扇无头窗各渲染，`itest_startup_notice` 有手工构造 `LoadReport` 驱动降级框的先例；而 `src/main.cpp` 不编入 runner（裁决 7.83） | 「读快照 → 开窗 → 按快照重放」那一腿**结构性无自动化证人**，与 PREF.05 那棒「main.cpp 两处消费点结构性无证人」同一档，按同一口径登记（判据 ⑤）：本棒的自动化判据只能落在纯逻辑件（树、快照模型、编解码）与 ui 件（工作区重放）上，装配那一段靠编译 + 走查兜底 |
| **F22** | **开工基线（本棒同日实测）**：`ctest --preset linux -E etest_` **104 项**全绿（含 `etest_` 为 107 项），`./build/borealis_test_runner` 全跑 **1306 案**全绿；`utest_settings_catalog` 钉 70 行／`Absent` 19 行；`check_display_literals` 门禁在位（PREF.05 批 7-A 升为常驻 CTest 项） | 结项三件齐（ctest 全绿 + runner 案数增量 + 变异证人逐条转红）以这三个数为起点。本棒的增量口径预估为「`utest_pane_tree` 扩案 + `utest_workspace_snapshot` 新套件 + `itest_workspace_restore` 新套件 + `itest_workspace_layout` 扩案」 |
| **F23** | **落地阶段（批 1）读数：形状**不**带容器标识**——`ui::PaneShape` 只有「叶子：序位」与「层：轴 + 逐子相对长度 + 子表」四种字段，`ContainerId` 由 `restore` 经既有的 `next_container_` 现场发放（同文件那条只增不复用的口径） | 形状与「本次进程里的层号」解耦，跨进程存它不会漂；副作用是重建后旧层标识**失效**而非指向别层（`rebuilding_twice_does_not_recycle_the_container_handles` 钉这一条）。批 5 的接线因此不能把形状与把手句柄混存：恢复之后要重新取一次 `container_of` |
| **F24** | **落地阶段读数：`describe()` 绝不归一**。相对长度只在 `layout` 的 `distribute` 里折算，形状原样搬出才能做到「折出来 → 重建 → 再折出来」两次读数按位相等；实测把拖到底的那一层（540／48／196 dp）搬进新树再折一次，逐位相同（`a_dragged_proportion_survives_the_round_trip`） | 这条是判据 ① 的落法：比的不是「比例看起来一样」，而是**双精度按位相等**（`PaneShape::operator==` 取 default 正是为此）。任何一处顺手加归一／吸附，两次读数就差在 1e-16 上，判据当场失去意义 |
| **F25** | **落地阶段读数：`restore` 的自检清单是六道判据**——单子层、长度与子数不符、长度非有限或 ≤0、叶子却带子（判别位有歧义）、叶子序位不是 `[0, N)` 的排列、身份递增越界；全部在写树**之前**跑完，任一条不过即整份拒绝且旧树逐位不动（`malformed_shapes_are_rejected_and_leave_the_tree_alone`，11 例坏形状 + 2 例反空转） | 批 2 的 JSON 读侧要在六道**之外**再加「顶层类型／数组元素类型／计数超上界」那一层：树件管形状合法性，快照件管文档合法性，两道闸叠加才是判据 ② 的完整形态。批 2 不要把六道判据重写一遍——`from_json` 折出形状后交给 `restore`，坏文档自然被这六道接住 |
| **F26** | **落地阶段（批 3）读数：框架的 `remove(key)` 删不掉「值是整棵对象」的那种键**。实测同一份 `Preferences`：①`set("layout", <一棵对象>)` + `flush()` 写盘；②换实例 `remove("layout")` + `flush()`；③再换新实例——`contains("layout")` 仍为 **true**，整棵布局原样在盘上。原因是墓碑按 `flatten` 之后的**叶子复合键**记账，中间对象层只作前缀、自己不落成独立条目，故顶层那个容器键找不到对应墓碑；`clear()` 则靠「清空纪元」对叶子生效，是**唯一**能清掉整棵布局的那一跳（探针 `/tmp/wsprobe/rmprobe.cpp`，不入库） | 判据 ③ 那句「开关关掉之前已有的快照不被删除」因此**连框架那跳都挡得住**——不是本件刻意留了后门，而是这条路本来走不通。反向的约束落在批 5／批 7：装配层**不许**指望用 `remove` 清快照，界面上也确实没有清理入口（D9① 的在册代价照旧成立）；真要清只能整份 `clear()`，而那是把别人的键也一起清掉的爆炸半径，本棒不用 |
| **F27** | **落地阶段（批 4）读数：一枚缺省为开的布尔键，同步面比 F19 那句「六处 + 两处计数闸」还多一处**。实测红的第一枚不是 catalog 的计数闸而是 `utest_form_transfer` 里的布尔覆盖闸——`a_boolean_key_reads_the_boolean_it_names` 断 `bool_probes().size() == boolean_key_count()`（按 `ValueDomain::Boolean` 现数 catalog），新增布尔键即 21→22 直接转红，故 `bool_probes()` 那张表是**第三处必须同步的清单**。它与「缺省开」这一档还有第二处咬合：该表与 `made_up()` 的通用写法都是「取离缺省最远的那个值」，缺省 `true` 的键在这里必须填 `false`，填反了那条探针就成了恒真空话（本案按此形态填，`utest_config` 的往返位同理取 `false`） | F19 那条清单在结项回写时补上这一处（批 7 的 `SPECIFICATIONS.md` 口径位）。对本棒的直接影响：批 4 的证人范围从「catalog 那一行」扩到「行案 + 缺省案 + 往返案 + 布尔探针案 + 两档词条案」五处，M5／M6／M7／M9 四支变异正是落在这四处而非行案上——**判据 ③ 的 catalog 半边只是入口，真把「键存在但存不回去／搬不进表单」抓住的是那三张既有清点表** |
| **F28** | **落地阶段（批 5-A）读数：恢复接缝的形态是静态工厂，不是「扩 `Hooks` 的形参」**。§8 表中那句「形态与 WS.02 批 1 的 `origin` 形参 + `replace_view` 同档」是开工前的推测，实测两头都不成立：① `make_pane(pane, origin)` 的 `origin` 语义是**被切那一格**，恢复腿没有源格可给，硬套就要装配层把「阅读序序位」当 `PaneId` 传进自己的会话工厂——本地腿的 OSC 7 继承据此读错格（正是 F4 那一族「按错的号取料」）；② 「改一扇现成的工作区」这条路必先散旧格再造新格，任何一步失败都留下一棵半棵树加一批已散的手柄，而 `PaneTree::restore` 的「先整体自检再一次性换根」在视图这一层没有对应物可借。落地为 `WorkspaceView::restore(views, shape, focused_order, hooks)`：**四道前置（形状自检、视图份数与叶子数相符、无空指针、选中序位不越界）全部跑在造对象之前**，回空即「没有半个工作区」，`Hooks` 一个字段没动。附带把「序位 i 的格挂标识 `i + 1`」这条既有约定写成公开承诺（构造路径本就这么发号），并补两枚采集读数 `pane_shape()` / `panes_in_order()`——装配层按序位对料必须要有一份「阅读序 → 标识」的表，而标识在切分与关闭之后**不连续**（`1、2、4` 是合法读数） | 批 5-B 的采集腿因此只调这两枚读数，不必自己反查树；批 5-C 的重放腿只管「按序位把会话造出来再交出去」，标识由本层按同一条算式发。反空转的两条硬账：删掉「按选中序位写模型投影」那一跳，**只有**「恢复之后再切一刀、看落在谁头上」那案能转红（`focused_pane()` 优先读框架焦点，只看它会漏掉模型投影没跟上这一种错）；跳过逐格 `wire_view` 由「三格各收到一份自己的行列」那条断言转红 |
| **F29** | **落地阶段（批 5-B）读数：`Hooks` 原先没有一处看得见「树已经换了」**。`make_pane` 排在 `tree_.split` **之前**（工厂失败时树因此不动，F28 同一族），`teardown_pane` 拿到的是被回收的那一格，两者都不是「变更后的拓扑」；而切分与关格都发自 `WorkspaceView` 自己的按键派发，装配层没有第二个调用点可挂。落地补第四条接缝 `structure_changed`（`Hooks` 末位，缺省空即不关心）：`split_pane` / `close_pane` 两条改树腿的末段各调一次，三条早退（无工厂、工厂回空、`tree_.split` 拒）与把手步进都不调。证人落在 `itest_workspace_layout`：切分后一响且**钩内已是两格**、已回收数为 0；关格后一响且已回收数为一；工厂回空／把手步进／关末位格三档零响（**32 案 → 34 案**） | ① 把手比例不在变更点内：只拖过把手就崩，比例丢一次（随下一次真变更或退出提交）——D2① 把「拓扑」与「相对长度」分档的代价，在册；② 提交次序承重：`create_new_tab_with` **内部不提交**，四个档案腿在 `tab_profile_id` 与四张连接表登记齐之后才提交——否则 SSH 腿会先写出一张「类型有、档案无」的标签，采集侧按 fail-closed 跳过它（等于白关一次崩溃窗口）；③ 实测的变更点比 D2① 那张表多两类：标签条 `.move`（条内序就是快照的标签序）当场提交，重启腿的本地料改写不改拓扑、不单独提交（随下一次真变更或退出落盘）；④ fail-closed 的落点定在**单张标签**（跳过 + WARN）：既不冻结整份文档，也不写出恢复侧读不了的「半个连接」；零标签的窗同样不进快照 |

---

## 1 需求那四件事在本仓落成什么

需求括号里的四项与代码里的对应物，逐项只有一处来源：

- **连接类型** = `tab_kind` 表里那枚 `conn::ConnectionType`（F8，WS.04 认定的唯一来源）。
- **profile** = 该腿已有的整份档案（`ssh_profile_by_tab` / `serial_by_tab` / `telnet_by_tab`）+ 档案 id（D4① 双写）；本地腿缺的那份账由本棒补成 **per-pane 有效料**（F8 末句）。
- **分屏结构** = 那一棵 `ui::PaneTree`（D5 决定它是自描述还是被外部记脚本）。
- **各标签标题** = `ui::TabStrip` 里**手动重命名那一项**（D10；OSC 标题属会话内容，需求那句「会话内容不恢复」把它排除在外，重启后由远端重发）。

需求没写而开工前必须拍的六件事，本稿的处理分别在 D1（文件落点）、D2（何时写，崩溃窗口）、D3（那枚开关的键名/域/档位/缺省/关的射程）、D6（恢复几扇窗）、D7（与 CLI 抢首标签）、D9（读侧上界）。

---

## 2 交付判据（①②③④⑥⑦ 有自动化证人；⑤ 装配腿无证人）

1. **重启后布局逐字回来**：`ui::PaneTree` 的自描述 → 重建（`describe()` 出的形状喂回 `restore()`）后，`panes()` 的阅读序、各层的 `PaneAxis` 与**相对长度**逐位相同（含「同轴切分并入父层而不另立一层」那条不变量在重建路径上同样成立，F5/裁决 7.42）。这条挡住的是「恢复出来的树看着一样、拖把手时全露馅」。
2. **快照的往返等值与结构 fail-closed**：`utest_workspace_snapshot` 对一份含四腿、含多层分屏的样本做「模型 → JSON → 模型」全等；对**手改过的坏文档**（顶层不是对象、任计数超上界、数组元素类型不符、`schema_version` 高于本仓支持）逐例整体拒绝而不回落半个（照 `cli::handoff` 与 `import_settings` 那条「外部文档宽容刻意相反」的既有纪律，D9）。
3. **开关关掉就不读也不写**：`connection.restore_sessions == false` 时，装配层取的恢复料恒空，且本进程任何拓扑变更都不再触发落盘；关掉之前已有的快照文件**不被删除**（本棒的落盘件与 `WindowGeometryStore` 同形态：只做「取存储 + 提交」两跳、没有任何删除入口，清它的账归用户），这一条由 catalog 那枚行案 + 快照件的档位案各打一半。
4. **恢复的每一格照旧下发一次初始尺寸**：重放路径走的是既有的「新 pane 挂载 → 标脏 → 去抖尾沿」那条腿，故 `SPEC.FEAT.XFER.01` 那句「会话恢复时各下发一次初始尺寸」由既有证人脸覆盖——本棒给它加一条**显式**案：恢复后每格都收到过一次 `Session::resize`，且次数恰为格数（多了就是重复挂载，少了就是漏发）。
5. **装配层的恢复腿与凭据重取那一段无自动化证人**：`src/main.cpp` 不编入 runner（裁决 7.83、F21），故「读快照 → 开窗 → 按快照重放」与「SSH 腿重新走 `connect_profile` → `ask_secret`」这两段只有编译 + 链接 + §5 走查兜底。本棒**不称变异自证**这两条，也不为它们造假宿主。
6. **明文凭据进不了快照**（`SPEC.FEAT.CONN.09` 的硬闸，F13）：照 `utest_config` 的 `key_dirs` 案同形态扫快照全文档的键名，逐个对 `{password, passphrase, secret, token, private_key}` 五枚子串断言不含；另加一条**值侧**断言：把一份带 `AskEveryTime` 哨兵与一份带 `vault_id` 的 SSH 档案各往返一次，写出的字节里不含喂进去的那枚口令串（替身凭据，非真凭据）。这条是本棒对第 24 条负责的唯一证人。
7. **两处档案 JSON 形态不分叉**（F12 的缓解证人）：同一份 `conn::SshProfile` / `SerialProfile` / `TelnetProfile` 分别喂给 `config::Store` 的落盘与快照件，断言两者写出的**键名集与枚举取值文本逐字相等**。这条红了就是「`SshProfile` 有了第二套落盘形态」，正是本棒最想让结构挡住的事。

判据 ①②③④⑥⑦ 有自动化证人（①②⑥⑦ 在 utest、③ 由 catalog 行案与快照件的档位案各打一半、④ 在无头集成），⑤ 无证人。

---

## 3 待人裁决（每条已给推荐①）

| 号 | 问题 | 候选 | 推荐 | 理由与代价 |
|:---|:---|:---|:---|:---|
| **D1** | 快照落在哪 | ① 另起第三枚 `Preferences` 文件 `<配置目录>/workspace.json`（照 `window_geometry_store` 形态）；② 进 `borealis.json` 的新域；③ 复用 `windows.json`；④ 自写文件不走框架 | **①** | ② 被 F2 那两条同时否决（快照环被烧穿 + 导出件跨机，后者直接违「不跨设备」）；③ 把「框架自己写的几何」与「本仓写的布局」混进一枚文件，而前者由 `save_window_geometry` 链独占键空间（`window_geometry_store.h` 明写「不许在外面另建一份键表」）；④ 等于重造 `Preferences` 已有的原子写与 advisory 锁（架构 §11.3 那条「本仓不重造」）。代价：配置目录多一枚文件、`CONN.09` 的审计射程扩一格（F13），且第三枚 `Preferences` 的**声明次序**（须早于 `app`）要照 F3 那条注释继承 |
| **D2** | 什么时候写 | ① 拓扑变更事件当场写（开/关标签、切分、关格、改名、换选中、关窗、退出）；② 后台线程 + 有界队列合并写；③ 只在关窗与退出两处写 | **①** | ③ 被需求那句「含崩溃后」否掉（F4 的注释已经把这条说透）；② 是 AGENTS.md §4.5 第 25 条的正解形态，但本棒的变更点是**用户动作**（按一下键、点一下格）而不是每帧绘制，写的是几 KB 的本地原子文件，量级与既有的 `flush_window_geometry()`（同样在 `on_window_closed` 回调里同步 flush）逐字相同——25 条禁的是事件回调里的**阻塞 IO 与长计算**，这条口径要在稿里说清而不是绕过去。代价：连续切分（极端手感）会产生数次连续落盘；**若走查第 ⑥ 条摸到卡顿，②就是既定的升级路径**，本棒在 §6 留此一句 |
| **D3** | 那枚开关的键名、归属页与生效档 | ① `connection.restore_sessions`（bool，缺省**开**）× `Wired` × `NextLaunch`，画在**连接页**；② 同键但画在外观页；③ 不画控件、登记进 `kNonPreferenceLeaves` 例外通道 | **①** | 面板页必须与路径首段的域一致（`utest_settings_catalog` 双向闸的一条），所以「域名」与「画在哪页」不是两个独立选择；四分类是 PREF.02 定死的、第五类已由 `codespec/UI_SETTINGS.draft.md` §2 撤销过一回，其撤销理由里就有「会话日志两键归连接页」这条**同类归处**先例——恢复决定的是「重启后要不要把上次的连接重连起来」，与 `connection.local_shell`／`startup_directory` 同域。③ 是拿来装运行期数据的（那一枚例外通道现五枚：`connection.recent`、`profiles.items`、`tunnels.items`、`key_dirs.items`、`connection.trusted_url_hosts`），把一条**用户可改的偏好**塞进去就是绕过面板，需求那句「恢复开关可配」落不到界面上。缺省取开：`PLAN.md` 的 M4 出口判据原文是「重启恢复工作区」，缺省关会让本条的验收只在改过设置的人身上成立。代价：关 ⇒ 重启后可能一次弹多枚凭据询问框（`CONN.09` 本期就是每次询问，F9），这条落 §5 走查第 ④ 条；另「关」之后再「开」会恢复出一份**很旧**的布局（关期间不写），这一档同样登记为代价而不是缺陷 |
| **D4** | 每标签／每格的连接规格存什么 | ① **档案 id 与整份字段双写**，启动时 id 命中 `settings.profiles` 即以表里那一份为准，未命中即用快照里的字段；本地腿补一张 per-pane 有效料（`LocalTerminalSpec` 三件）；② 只存 id；③ 只存字段 | **①** | ② 被 F10 否掉（quick connect 造的档案不落表，只存 id 结构上取不回料）；③ 丢的是「档案后来被改名/改端口，重启看到的还是旧的那份」，且与 `ssh_profile_by_tab` 现在「整份存」的形态虽同源，却不满足需求那句「profile」。① 的「id 命中优先」把单一真值源留在档案表，快照字段只当**无表可依时的兜底**，两条各守一半。凭据半边：只搬 `SecretHandle`（F11），恢复腿走既有 `connect_profile` → `ask_secret` 链重新问（F9），**绝不**把 `ssh_secret_by_tab` 那枚内存副本写进快照。代价：F12 那处「同一份档案两处 JSON 形态」的分叉风险，由判据 ⑦ 的等式案认住 |
| **D5** | 分屏结构怎么记 | ① 给 `ui::PaneTree` 加一对公开的**描述／重建**件（嵌套形状 + 每层相对长度），快照存这份形状；② 装配层自记切分脚本（每格 `origin` + `axis`），恢复时 `set_focus` → `split_focused` 重放；③ 只记格数，恢复成一行等分 | **①** | PLAN 的在册口径原文就写着「`PaneTree` 快照」；③ 违需求括号里的「分屏结构」；② 的代价是**比例必丢**（用户拖好的分屏恢复成中点），还要把 `PaneViewFactory` 的形参再扩一枚 `axis`（F7 有先例但仍是接缝扩容），并让装配层为每一格维护一份「切分史」——那是与树并存的第二份拓扑真值源，本仓反复登记过的坏形态。① 的代价：`include/borealis/ui/pane_tree.h` 公共面扩两枚函数（纯逻辑、不含 Aurora 类型，符合第 20 条），并要 `utest_pane_tree` 配「描述→重建→再描述逐字相同」的案；②留作①被否时的退路 |
| **D6** | 恢复几扇窗 | ① 全部窗（每窗一份标签表，窗序与 `windows.json` 的创建序键对齐，几何仍由框架那枚链恢复）；② 只恢复主窗；③ 只恢复主窗，其余标签并进主窗 | **①** | 多窗自 WS.03 起已是既成形态，「上次的标签布局」在用户眼里包含「我开的那三扇」；② 会让用户第二次重启就发现窗老丢；③ 是造一种本仓没有的「并窗」行为。几何与布局**分两家记**是本稿的刻意分工：位置尺寸归框架的 persist 链（F3），内容归本棒，两枚文件互不含对方字段——代价是二者按**创建序**对齐，一旦创建序与快照里的窗序不一致（手改过文件），就是「布局对了、尺寸来自另一扇」这种肉眼可辨的错位，§5 走查第 ⑤ 条专门走它 |
| **D7** | 与 CLI 冷启动谁让谁 | ① 冷启动带**显式**请求（`--profile` / `--new-window` / `--split-pane`）⇒ 跳过恢复并留痕一句日志；② 恢复照常，CLI 再开一格；③ 任何 CLI 参数（含 `--cwd` 单飞）都不恢复 | **①** | F16 那三行布尔证明本仓早就在处理「CLI 意图不该被一条缺省标签淹没」，同一取向推到「也不该被一堆旧窗淹没」；② 的可见结果是 `borealis --profile gw` 打开成五扇窗，而用户要的是那一格；③ 把 `--cwd` 这种「只是给首标签换个目录」的请求也当成覆盖意图，过头。代价：「双击图标 + 命令行」两种起法得到不同布局，这条要在 §5 说清；转交侧（已有实例收请求）不涉及恢复，本稿不动那条腿 |
| **D8** | 快照文件坏掉怎么办 | ① 整体丢弃、按首启动形态起来、只落 `AURORA_LOG_WARN`；② 先备份 `*.corrupt-<epoch>` 再丢弃（照 PREF.07 那条腿）；③ 复用 `ui::StartupNotice` 弹框 | **①** | 需求把降级只写在「**配置本身损坏**」那一句，快照不属配置本体（D1 已分家）；③ 要扩一枚「判据本体就是那份报告类型」的件（F17），连带改它的用例面，为一次「布局没了」打断用户不划算；② 的备份对一份**只描述布局**的文件意义有限（没有用户现场要守，现场在配置那枚文件里）。代价：布局静默消失，只留日志——本稿把它写进 §5 走查第 ⑦ 条，真人验收过「丢了之后能不能接受」；不能接受即按 ② 升级 |
| **D9** | 快照的规模上界 | ① 写侧不设限，**读侧**设计数上界并超限整份拒绝；② 写侧也截断（每窗 N 标签、每标签 M 格）；③ 都不设 | **①** | 读侧上界是必要的：一份手改过的文件若声称有一百万格，恢复腿就会在启动时建一百万个视口（本仓唯一的先例是 `cli::handoff` 的「长度先验 + 超限整帧拒」，F12 同一族）。写侧截断（②）会在用户真的开到第 N+1 格时**静默丢布局**，比拒绝更坏；③ 则把「手改文件 ⇒ 启动卡死」这条路留着。代价：超界那份文件等于废掉直到用户自己清（提示只进日志，D8① 同档），界面上没有「清理快照」入口——这条与 `trusted_url_hosts` 那条「要清只能改 JSON」的在册代价同类 |
| **D10** | 「各标签标题」存哪一项 | ① 存**手动重命名**那一项（没有就存空，重启后按既有算式落回类型缺省名）；② 存恢复那一刻 `resolve_tab_name` 解析出的当前显示名 | **①** | ② 存进去的是 **OSC 标题**（远端程序发的），而需求明写「会话内容不恢复」——OSC 标题是内容的一部分，重启后远端会重发；把它存成手动名，等于让一次旧会话的标题永久压过新会话（`TabNamePriority::ManualWins` 缺省下还压过用户自己的重命名）。① 保住「标题有两种来源、只有用户那一种可持久化」这条边界。代价：重启瞬间标签显示缺省名、几秒后随远端 OSC 重发才变成上次的样子，这条落 §5 走查第 ③ 条 |

---

## 4 落地形态（推荐的部件切分与归属）

三个部件，各自可独立单测，公共头一律不含 Aurora 类型（AGENTS.md §4.4 第 20 条）：

1. **`ui::PaneTree` 的描述／重建件**（`include/borealis/ui/pane_tree.h` + `src/ui/pane_tree.cpp`）：一枚嵌套形状类型（叶子 = `PaneId` 的**序位**而不是身份，容器 = `PaneAxis` + 子表 + 每子相对长度）与 `describe()` / `restore(shape, first_pane_id)` 两函数。身份由调用方给定这条既有口径（`PaneId` 由装配层分配、树内不自增）在重建路径上同样成立。
2. **快照数据模型 + JSON 编解码**（`include/borealis/ui/workspace_snapshot.h` + `src/ui/workspace_snapshot.cpp`，纯逻辑；档案块字段形态照 F11 那三块，代价与缓解见 F12／判据 ⑦）：`WorkspaceSnapshot{ windows: [ { tabs: [ { kind, profile_id, 档案字段块, 标题, 每格有效料(本地腿), pane 形状, 选中格 } ], 选中标签 } ] }` 与其 `to_json()` / `from_json()`（fail-closed，判据 ②⑥⑦）。
3. **落盘件**（`src/config/workspace_store.{h,cpp}`，私有头，照 `window_geometry_store` 形态）：绑定 `<配置目录>/workspace.json`、只提供取存储与 `flush()` 两跳，键在 `Preferences` **顶层**、不自造第二份键表。

装配层（`src/main.cpp`）只做两件事：变更点采集 + 启动重放，二者都读现成的账（F15、F8），**不**新增遍历原语，也**不**把任何 `Preferences` 之外的 IO 引进帧循环。

---

## 5 真机走查清单（未走完不得称「可用」，AGENTS.md §4.4 第 33 条）

1. 开三格分屏、拖两条把手改比例 → 重启 → 三格的比例肉眼是**同一份**（判据 ① 的界面侧，D5① 的全部理由就在这条上成立或崩掉）。
2. 本地 + SSH + 串口 + Telnet 各开一标签、每标签起个手动名 → 重启 → 四腿的类型图标（`>` / `#` / `S` / `T`）与四个名字对得上，且**先见缺省名、远端 OSC 重发后变回手动名**这一过渡可接受（D10① 的代价档）。
3. 只 kill 进程（`kill -9`）而非常规退出 → 重启 → 布局回来，回来的是**哪一次**变更（D2① 的崩溃窗口，本条是它与 ③ 的唯一分辨手段）。
4. SSH 档案需要口令那份：重启后**弹一枚询问框**、填进去能拨通；多枚 SSH 标签同时恢复时的询问次序不糊成一团（D3 缺省开的那条代价，F9 的现状）。
5. 两扇窗（各带标签）→ 重启 → 两扇都在、各自的位置尺寸与内容配对正确，没有「B 窗的内容配了 A 窗的尺寸」（D6① 与 F3 那枚创建序键的接缝）。
6. 连续切分 10 格（每切一次即一次落盘）：手感不卡、不掉帧（D2① 的代价档；卡即按 D2② 升级，本条是唯一的分辨手段）。
7. 手工把 `workspace.json` 写成坏 JSON / 把某个计数改成超界值 → 重启：应用照常起来（首启动形态）、日志有一句、**不弹任何框**（D8① 与 D9① 的代价档）。
8. 把开关关掉：重启不再恢复、期间也不再写文件；再打开时恢复出的是关掉之前那一份旧布局（D3①「关 ⇒ 读写皆停、文件不删」的代价档）。

---

## 6 边界（登记在册，不在本棒解决）

- **会话内容不恢复**：scrollback、屏幕内容、当前目录（OSC 7 实时值）都不进快照；恢复出来的每一格是一条**新连接**。需求那句「本地终端无源可恢复」即此。
- **缩放态与广播组不落盘**：F20 那两处明文不变。恢复后回到未缩放、无广播组的形态。
- **凭据本身永不落盘**：`CONN.09` 的 OS 凭据库后端仍是独立任务（本期 `UnavailableCredentialStore`，F9），本棒只是**不放宽**这条，不给它加东西。AGENTS.md §4.5 第 24 条要求的「配置目录 grep 无明文凭据」那枚**目录级**自动化用例仍未落（F13），归 `CONN.09` 余腿。
- **不做跨设备/同步**：需求原文已把配置同步裁剪掉，本棒快照与导出件互不相干（F2 的否决理由之一）。
- **不动 WS.10 的闭包栈**：那份是内存栈、字段与粒度都不同（F14）；本棒不复用它，也不顺手把它接上盘。
- **不碰 `SPEC.NF.RELI.01` 的崩溃留存腿**：`src/main.cpp` 里那条 `TODO(SPEC.NF.RELI.01)`（信号钩子/dump/自写诊断的形态待人裁）与本棒的「崩溃后恢复布局」是两件事，本棒不借道实现它。
- **不扩 `ui::StartupNotice`**：D8① 之后它仍只吃 `config::LoadReport`。
- **连带改口的在册句**（批 7 已落，2026-10-22）：`EffectLevel::NextLaunch` 注释里那句「唯一消费点是面板角标」（F18）、`SPEC.FEAT.XFER.01` 的「『会话恢复时』那一档随 `SPEC.FEAT.WS.11` 落期」（`SPECIFICATIONS.md` 的该条原文）、`SPEC.FEAT.WS.01` 与 `INTEG.04` 里与启动首标签有关的句子（D7）、`PLAN.md` §3 的 WS.11 行与 §8 的 M4 行。

---

## 7 与既有裁决的相接处

- **7.26③**（`borealis.json` 的存储形态＝单文件 + 按域分组嵌套 + 顶层版本号，框架承担原子写与 advisory 锁）：本棒只承前半句的**单文件 + 顶层版本号**，**不**沿用「按域分组」那一半——按 F3 与 7.126 逐窗几何那枚的「单文件 + 顶层键」长，理由是快照不是可导出的设置域（同那条「跨机导出即坏值，这条是特性不是漏项」）。
- **7.87**（快照环与导出/导入的文档同构）：本棒刻意**不进**那份文档，判据 ⑦ 因此是「两处形态等式」而不是「同一处」。
- **7.42**（PaneTree 三条不在需求原文却决定形态的口径）：D5① 的重建路径必须复用同轴并入与只增不复用两条，判据 ① 即此。
- **7.47⑩／7.48③**（去抖归工作区层）：判据 ④ 走既有那条尾沿，本棒不新设第二处 resize 下发点。
- **7.121**（广播成员不落盘）／**7.120⑥**（`zoomed_` 按标签独立、不落盘，原文写「重启不恢复（属 WS.11）」）：§6 第 2 条的出处，本棒即那条挂账的兑现方。
- **7.126**（`WorkspaceWindow` 与 `ActiveWindowTracker`，逐窗账）：D6① 的「窗序按创建序」用的是 F3 那条既有的 `window_persist_id(creation_index)` 算式，本棒**不**新造第二套窗身份。
- **7.83**（`main.cpp` 不编入 runner）：判据 ⑤ 与 F21 那句「装配腿无证人」的登记口径出处。
- **7.118**（有键有消费方才画正常行）与 **7.52** S15（无键不画控件）：D3① 的「画得出真控件」判据。

---

## 8 分批（每批一个分层提交）

| 批 | 内容 | 触及 | 证人 |
|:---|:---|:---|:---|
| **0** | 本稿与地基实测（本节即产出） | `codespec/` | 无（稿） |
| **1** | `PaneTree` 的描述／重建件 + 嵌套形状类型 | `include/borealis/ui/pane_tree.h`、`src/ui/pane_tree.cpp` | `utest_pane_tree` 扩案（判据 ①） |
| **2** | 快照数据模型 + JSON 编解码（fail-closed 读侧） | `include/borealis/ui/workspace_snapshot.h`、`src/ui/workspace_snapshot.cpp`、`src/CMakeLists.txt`（`borealis_core` 是显式清单，实测必须登记；测试侧 `cmake/BorealisTests.cmake` 的 GLOB 零改）、`include/borealis/conn/local_terminal.h`（补 `operator==`） | `utest_workspace_snapshot` 新套件（判据 ②⑥⑦） |
| **3** | 第三枚 `Preferences` 的落盘件 | `src/config/workspace_store.{h,cpp}`、`src/CMakeLists.txt`（同批登记一行，`borealis_core` 显式清单） | 判据 ② 的落盘往返腿 + 判据 ③ 的档位半边（同套件扩案） |
| **4** | 新键六处同步 + 面板那一行 + 两档词条 + 两处计数闸改口 | `settings.h`、`store.cpp`、`form_transfer.cpp`、`settings_catalog.cpp`、`settings_i18n.cpp`、`utest_settings_catalog.cpp`（另加两处实测必需的同步：`utest_config.cpp` 的缺省档与往返、`utest_form_transfer.cpp` 的布尔探针表，见 F27） | catalog 那一行案（判据 ③） |
| **5** | 装配腿：本地 per-pane 账、采集、启动重放、D7 的 CLI 次序 | **5-A** `src/ui/workspace_view.{h,cpp}` 的**恢复接缝**（实测前置：`WorkspaceView` 公开面只有 `split_focused` / `close_pane` / `replace_view`，没有任何「按一棵现成的形状重建」的入口，而 `PaneTree::restore` 在树件里、视图与挂载归本件——形状要落回界面必须有这一条接缝。**落地形态与表中那句猜测不同，见 F28**：静态工厂 `WorkspaceView::restore(views, shape, focused_order, hooks)` 加两枚采集读数 `pane_shape()` / `panes_in_order()`，`Hooks` 零改）；**5-B** `src/main.cpp` 的每格本地料账 + 采集腿 + 变更点落盘（D2①）+ 开关闸门（**采集点要的第四条接缝 `structure_changed` 见 F29**）；**5-C** `src/main.cpp` 的启动重放与 D7 的 CLI 次序（含 `WorkspaceStore` 声明须早于 `app`） | **5-A 有自动化证人**：`itest_workspace_layout` 新增三案（判据 ① 的形状半边，该套件 **29 案 → 32 案**）；**5-B 的 ui 接缝有自动化证人**（同套件再增两案、**32 案 → 34 案**，F29），5-B 的装配腿与 5-C **无自动化证人**（F21），§5 走查第 ①–⑤ 条兜底 |
| **6** | 无头集成用例：写快照 → 新宿主读回 → 断标签数/类型/格数/焦点/尺寸下发 | `tests/integration/itest_workspace_restore.cpp`（新）、`itest_workspace_layout.cpp`（扩） | 判据 ①④ 的接线半边 + 变异自证 |
| **7** | 文档回写与结项：`SPECIFICATIONS.md` 现状句 + 裁决 7.131、`CHANGELOG.md` v0.131、`PLAN.md` §3/§8、`ARCHITECTURE.md` §2.2 与 §11、`UI_SETTINGS.draft.md` 那一行与 §4 相关句 | `codespec/` | 结项三件齐读数 |

批 5 与批 6 的次序刻意为「先有可断言的件、再补接线」：批 6 的集成用例驱动的是 ui 与快照件，不等批 5。

**批 1 已落地（2026-10-22）**：`ui::PaneShape` + `PaneTree::describe()` / `restore(shape, first_pane_id)`
落在 `include/borealis/ui/pane_tree.h` 与 `src/ui/pane_tree.cpp`，`utest_pane_tree` 由 18 案涨到 **24 案**
（新增 6 案：嵌套形状往返、拖出来的比例往返、序位对位、单叶往返、11 例坏形状 + 2 例反空转、层标识不回收）。
读数：`ctest --preset linux -E etest_` **104/104 全绿**、`./build/borealis_test_runner` **1312 案全绿**
（起点 1306，增量 +6）。变异逐条转红：`describe` 不带长度 → 3 案红；不查序位排列 → 1 案红；不查身份越界
→ 1 案红；重建时忽略序位发身份 → 4 案红；放过单子层 → 1 案红；重建不动焦点 → 1 案红；序位写成身份
→ 4 案红。**七支无一空转**，判据 ① 与 F23／F24／F25 三条读数各有证人。

**批 2 已落地（2026-10-22）**：`ui::WorkspaceSnapshot`（`windows → tabs → 每格本地料 + 形状 + 焦点序位`）
与它的 `to_json()` / `from_json()` 落在 `include/borealis/ui/workspace_snapshot.h` 与
`src/ui/workspace_snapshot.cpp`，新套件 `utest_workspace_snapshot` **13 案**。
读数：`ctest --preset linux -E etest_` **105/105 全绿**（104 → +1 个新套件）、`./build/borealis_test_runner`
**1325 案全绿**（起点 1312，增量 +13）。这一批只碰模型与编解码，**不碰文件系统**，故判据 ③ 的档位半边
与判据 ② 的落盘往返腿仍归批 3。
变异逐条转红：写侧漏 `focused` 键 → 4 案红；SSH 块里 `agent_forwarding` 改名 → 5 案红；凭据块的
`vault_id` 写成含禁列子串 `secret_ref` → 6 案红；读侧把窗数上界放宽 → 1 案红；读侧对「键缺失」改判
宽容（回落缺省而非拒绝）→ 2 案红；取消「非本地标签的 `panes` 恒空」那道对位闸 → 1 案红。**六支无一空转**，
判据 ②（往返全等 + 坏文档整份拒）、⑥（键名扫描 + 值侧替身口令）、⑦（两处档案 JSON 逐字相等）各有证人。
两处与本批同批落地的**必要附带件**：① `src/CMakeLists.txt` 的 `borealis_core` 源清单是**显式列表**而非
GLOB（本表 §8 那一栏原先只写了 `cmake/BorealisTests.cmake` 的 GLOB，测到链接期才暴露），故新实现文件
必须登记一行；② `conn::LocalTerminalSpec` 补一枚 `operator== = default`——判据 ② 的「模型 → JSON → 模型
全等」要逐格比本地料，而该结构此前没有相等比较（`conn` 的 `LocalProfile` / `SshProfile` 等早已带同形态
一枚，这里只是补齐缺的那一块）。
一条实现缺陷由往返案**当场抓住**并已在同批修掉：读侧折容器层时把 `axis` / `lengths` / `children` 都折对了，
却没把 `leaf=false` 写回 `PaneShape`，于是恢复出来的树「看着一样」（叶子数对得上、对位闸全过）而形状本身
退化成一格——判据 ① 那句「拖把手时全露馅」在本件的另一半，正是这一条。
F12 那处在册代价第一次实际支付：`src/ui/` 依架构 §2.1 不得反向依赖 `config`（含其私有头
`schema_names.h`），故快照件自列一份枚举名表；判据 ⑦ 那条等式案就是这处第二真值源的证人，
它红了就等于「档案有了第二套落盘形态」。批 5 的装配腿与凭据重取那一段本批未碰，判据 ⑤ 仍无证人。

**批 3 已落地（2026-10-22）**：`config::WorkspaceStore`（第三枚 `Preferences`，绑 `<配置目录>/workspace.json`）
落在 `src/config/workspace_store.{h,cpp}`，形态逐字照 F3 那枚先例 `config::WindowGeometryStore`：两处构造 +
`preferences()` / `path()` / `flush()`，**没有任何删除入口**；`src/CMakeLists.txt` 的 `borealis_core` 显式清单
登记一行（同批 2 那条附带件）。证人扩在既有套件 `utest_workspace_snapshot`：**13 案 → 16 案**，新增三案全是
文件腿——落盘往返（判据 ② 的落盘半边）、只提交不写仍拿得到上一份（判据 ③ 的档位半边）、提交失败回 ASCII 原因。
读数：`ctest --preset linux -E etest_` **105/105 全绿**（套件数不变，本批零新测试目标，涨的是既有套件的案数）、
`./build/borealis_test_runner` **1328 案全绿**（起点 1325，增量 +3）。
变异逐条转红：默认文件名改写 → 案 1 红；构造时忽略传入路径只用默认 → 案 1+2 红；`preferences()` 回空指针
→ 案 1+2+3 红；`flush()` 之前先 `clear()` → 案 1+2 红；`flush()` 吞掉框架的错误原因回空 → 案 3 红；给落盘件
补上 `remove` / `clear` 两跳 → **编译期红**（`static_assert(!HasDeleteEntry<WorkspaceStore>)` 失败）。
**六支无一空转**，判据 ② 的落盘腿与判据 ③ 的档位半边各有证人；判据 ③ 的另一半（catalog 那一行案）与
`connection.restore_sessions` 的六处同步归批 4。
两条口径在册：① 判据 ③ 的**主证是编译期那枚 `concept`**——本件面上根本没有删除那两跳，故运行案只需要承担
「只 `flush()` 不写」这一半；反空转对照特意**没有**用框架的 `remove`（F26 实测它对整值对象键无效，用它当对照
会得到一条恒不成立的断言），改用一枚从未写过布局的存储来证明那三条「还在」不是恒真的空话。② 夹具文件名一律
取中立名（`store_round_trip.json` / `keep_previous.json`），生产那枚 `workspace.json` 只在默认绑定那一跳断——
否则夹具跟着文件名一起被变异带走，落盘案对「文件名写错」这支变异就没有鉴别力。

**批 4 已落地（2026-10-22）**：`connection.restore_sessions`（bool，缺省**开**，`Wired` × `NextLaunch`，画在连接页）
六处同步全部落定：`settings.h` 的字段与缺省 → `store.cpp` 的读写两半（键名 `restore_sessions`，紧随日志三键之后）
→ `form_transfer.cpp` 的 `Binding`（与 catalog 同序，那条「行数与逐格次序照表」的闸要求两者同批落地）
→ `settings_catalog.cpp` 连接页那一行 → `settings_i18n.cpp` 两档词条（`重启后恢复会话` / `Restore sessions after
restart`）→ `codespec/UI_SETTINGS.draft.md` §2 C 段那一行。`main.cpp` 的消费点按 §8 的棒次归批 5。
读数：`utest_settings_catalog` **11 案 → 12 案**（新增 `restore_sessions_is_a_wired_next_launch_connection_switch`），
计数闸 70 → **71**、`Absent` 行数**停在 19 未动**（本案是 `Wired`，不进那个集合）；`ctest --preset linux -E etest_`
**105/105 全绿**（零新套件）、`./build/borealis_test_runner` **1329 案全绿**（起点 1328，增量 +1）。
变异九支逐条转红、**无一空转**：页挂到外观 → 2 案红（本案 + `keys_are_unique_and_rows_are_addressable` 的页-域双向闸）；
消费方改 `Absent` → 2 案红（本案 + `grade_columns_match_the_two_physical_boundaries`）；生效档改 `NextSession`
→ 同上 2 案红；缺省改 `false` → 行案与 `utest_config.defaults_are_the_first_launch_shape` 各 1 案红；
写侧漏那一枚 `put` → `utest_config` 6 案红 + `table_covers_exactly_the_written_schema_keys` 1 案红；
读侧漏那一跳 → `utest_config` 4 案红；`Binding` 整块删 → `utest_form_transfer` 6 案红（catalog **0 案红**，
两处证人不重叠，正是 F27 那条读数的现场）；词条只留英文档 → `utest_i18n_tiers` 2 案红；`Binding` 接到同域
另一枚布尔成员 → `utest_form_transfer` 4 案红。两支源文件与各测试件均已按 `/tmp/mut4/*.orig` 逐文件 diff 确认还原。
两处批 4 独有、值得与批 5 对齐的口径：① **本棒的第一枚缺省为 `true` 的布尔偏好键**，于是所有「取离缺省最远值」
的既有写法都必须在这一枚上填 `false`（`bool_probes()`、`made_up()`、`utest_config` 的往返位三处同理），而
`bool_probes().size() == boolean_key_count()` 那枚覆盖闸是 F19 清单之外的第三处同步（F27）；② catalog 一行
现在就写 `Wired`，而消费点在批 5 才接上——这两批之间面板那句话与实际不符，是分批的固有代价，兑现手段是
§5 走查第 ⑧ 条（关 ⇒ 重启不恢复且期间不写；再开 ⇒ 恢复出关掉之前那一份旧布局），本棒不称其已被自动化证明。
判据 ③ 的两半自此齐了：行案（本批）+ 快照件的档位案与那枚编译期 `concept`（批 3）。

**批 5-A 已落地（2026-10-22）**：`WorkspaceView` 的恢复接缝（F28 的静态工厂形态）落在
`src/ui/workspace_view.{h,cpp}`——公开面共三枚：`restore(views, shape, focused_order, hooks)`、`pane_shape()`、
`panes_in_order()`；`Hooks` 与既有三条接缝**零改动**，`split_pane` / `close_pane` / `replace_view` 三条既有腿也零改动。
用例侧只动 `tests/integration/itest_workspace_layout.cpp`：驱动台的两条构造路径拆出共用的 `bootstrap()` /
`wire_hooks()` / `finish()`，恢复腿多一个形参「是否把构造期读数清成基线」——那份读数（每格各一条行列下发）
只在构造那一段存在，清了就没有第二处能看见它。
读数：`itest_workspace_layout` **29 案 → 32 案**（新增 `a_restored_shape_lays_out_every_pane_it_claims`／
`restore_is_refused_without_leaving_a_half_built_workspace`／`a_restored_workspace_still_splits_from_the_next_identity`）、
`ctest --preset linux -E etest_` **105/105 全绿**（零新套件）、`./build/borealis_test_runner` **1332 案全绿**
（起点 1329，增量 +3）、`check_display_literals` PASS 211 files。
变异七支逐条转红、**无一空转**：删「视图份数 == 叶子数」那道闸 → 进程在越界处**中止**（核心已转储，23 案后停）；
删「选中序位不越界」→ 拒绝案红；`next_pane_` 固定成 2（即恢复把已发过的号退回去）→ 续号案红；不装重建出来的树 →
2 案红；只写框架侧焦点、不写模型投影 → 续号案红；跳过恢复格的 `wire_view` → 逐格行列案红；删「视图含空指针即拒」
→ 进程**段错误**。`workspace_view.{h,cpp}` 与用例件三处均按 `/tmp/mut5a/*.orig` 逐文件 diff 确认还原。
三条口径在册：① 上列第一、第七支以崩溃收场而非红案——它们守的位置一旦失守就是越界取用与摸空指针，没有断言
来得及说话；本稿把它们记成「杀掉」但不计入红案数，避免结项读数虚高。② 「只写框架焦点、不写模型投影」那一支
**第一版用例抓不到**（32 案全绿）：`focused_pane()` 优先读框架持焦，而驱动台的 `finish()` 恰好把框架焦点派到了
同一格，两处读数互为掩护。改案的判据为「恢复之后再切一刀、看这一刀落在谁头上」（`last_origin`）——装配层的
启动入口正是这么走的（`split_focused` 读 `tree_.focused()`，见 `SPEC.FEAT.INTEG.04` 那条「栈外读数落后一拍」），
故这一支转红同时是判据 ① 里「选中格真的恢复了」的唯一证人。③ 本批只覆盖判据 ① 的**形状半边**（形状 → 界面）；
标签数、连接类型、档案与凭据重取那半边仍无证人，归批 6 的 `itest_workspace_restore`。

**批 5-B 已落地（2026-10-22）**：装配层的每格本地料账、采集腿与提交腿落在 `src/main.cpp`，另补
`src/ui/workspace_view.{h,cpp}` 的第四条接缝 `structure_changed`（F29——既有 `Hooks` 没有任何一处看得见
「树已经换了」，而切分与关格又都发自本层的按键派发）。账目三件：`PaneBundle.local_spec`（工厂算完就丢
的那份有效料）、`TabWorkspace::local_specs_by_pane`（按格存，切分／撤销重开／重启三条腿各登记）、
`tab_profile_id`（D4① 的 id 半边，四腿档案与撤账链同步）。采集腿逐窗逐标签只读现成账，缺料按单张标签
跳过并 WARN；提交腿的「当场」共八类：开标签（含四条档案腿与撤销重开，均排在登记齐之后）、切分、关格、
关标签、改名、换选中、移动（稿外补的一类，条内序即快照标签序）、关窗；第九类是退出。闸门在提交腿入口：
`connection.restore_sessions` 关掉即不写，已有文件不动（判据 ③ 的写侧）。
读数：`itest_workspace_layout` **32 案 → 34 案**（两支新案见 F29）、`ctest --preset linux -E etest_` **105/105
全绿**（零新套件）、`./build/borealis_test_runner` **1334 案全绿**（起点 1332，增量 +2）、
`check_display_literals` PASS 211 files。变异两支逐条转红、**无一空转**：删 `split_pane` 末跳 → 切分案红
（`0 vs 1`）；删 `close_pane` 末跳 → 两支各红一处（`0 vs 1`）。`workspace_view.{h,cpp}` 与用例件均按 Edit 原样复原，提交前逐文件 `git diff --check` 干净、复原后套件 34/34 全绿。
四条口径在册：① 接缝的新证人与装配腿的无证人分隔清楚——`src/main.cpp` 不编入 runner（F21），本批在
`main.cpp` 里的采集／提交／账目没有自动化断言，§5 走查第 ①–⑤ 条兜底；② 把手比例不在变更点内、重启腿
改写的本地料不单独提交：两者都随下一次真变更或退出落盘，只在这之间崩掉才丢一次（F29 的两条代价）；
③ 提交失败只落 WARN，布局静默消失，界面上没有清理入口与提示（D8①／D9① 的在册代价照旧）；④ 采集读到
的形状是**变更后**那一份（钩子排在 `apply_focus` 之后），关格腿读到的已回收数是一——这两条正是「装错位置」
在无头案里会露馅的地方。

**批 5-C 已落地（2026-10-22）**：启动重放落在 `src/main.cpp` 建首窗之后、`app.run()` 之前，形态是「游标 +
一次性泵」而非双层 for：SSH 询问型档案在 D4① 下要重新走凭据链，而答案从 `on_secret` 异步回来，泵因此在
缺口处挂起、由同一枚 sink 放行。读侧在**建首窗之前**完成三档闸（开关关／D7① 显式请求／D8① 坏文档各自
只落日志），首标签那一格按 `restore_at_startup` 让位（F16）。`--cwd` 单飞不跳过恢复，改在泵的完成点补开
——挂起等凭据时若在 `app.run()` 前补，用户点名要的那一格会被之后 `finish_restore_window` 的选中压到后面。
逐标签 fail-closed：形状、档案、本地料对位、串口单格任一不合即跳过且**不留账**（会话先 close 再散，四张
连接表与 `tab_kind` 一处不写）；整窗一格都没恢复出来才补一张 `commit=false` 的缺省标签，半成品不进快照。
取消（按钮或静默关闭）只收尾当前窗，已恢复的现场原样留下。静默关闭那条是实测补的：`CredentialPrompt::close()`
**不**回调 `on_cancel`，Esc、搬窗时 `set_overlay_host` 与宿主消散都会把泵留在一个不会来的答案上，故帧边界
兜底认领并中止（与按钮取消同一条语义）。读数：`cmake --build --preset linux` 通过、`ctest --preset linux -E
etest_` **105/105 全绿**、`./build/borealis_test_runner` **1334 案全绿**。装配腿仍无自动化证人（F21／判据 ⑤），
本批不称其已被自动证明。

**批 6 已落地（2026-10-22）**：新套件 `tests/integration/itest_workspace_restore.cpp` **2 案**走完「样本快照
→ `to_json` → 新宿主 `from_json` → 逐标签 `WorkspaceView::restore`」这条链：全文等值、两扇窗与四腿标签数、
连接类型、手动标题、SSH/串口/Telnet 档案块与逐格本地料、格数、阅读序身份、形状与选中格逐项复算；判据 ④
的接线半边在每条腿各断一次「恰一条初始尺寸」，并以第二遍呈现证明净变化为零不重发。`itest_workspace_layout`
**34 案 → 35 案**（新增 `a_restored_tab_receives_one_initial_grid_per_pane_and_no_more`：三格各自一次、
尺寸等于自己的矩形、再排一帧不重发）。读数：`ctest --preset linux -E etest_` **105/105 → 106/106**（新套件
即新测试目标）、`./build/borealis_test_runner` **1334 → 1337 案全绿**、`check_display_literals` PASS 211 files。
变异四支逐条转红、**无一空转**：读侧把 `title` 清空 → 新套件 1 案红（含全文等值与两处标题，共三处诊断）；
写侧把 `kind` 恒写 Local → 新套件在读侧对位闸处整份拒；恢复忽略 `focused_order` → 新套件 1 案红 + 布局套件
2 案红；跳过恢复格的 `wire_view` → 新套件 2 案红（逐腿行列与「恰一次」各一处）+ 布局套件 2 案红。四处均以
Edit 原样复原，复原后 `git diff --check` 干净、两套件复跑全绿。两条边界在册：① 本件不造假宿主替装配层
（`main.cpp` 不编入 runner，F21／判据 ⑤）；标签条显示名的三名折算、`selected_tab` 的实际切换与窗口创建仍
只在 §5 走查里验。② 快照里串口恒单格是装配层的判据（读侧不查，见 F25 末条的职责分家），本件按合法样本
造现场，不把「读侧该不该拒多格串口」混进恢复链。

**批 7 已落地（2026-10-22）**：六处回写全部落定——`SPECIFICATIONS.md` 的 WS.11 现状句与裁决 7.131、`CHANGELOG.md` v0.131、`PLAN.md` §3 的 WS.11 行与 §8 的 M4 行、`ARCHITECTURE.md` §2.2／§11.3、`UI_SETTINGS.draft.md` 那一行与新增 C5-a、`EffectLevel::NextLaunch` 注释改口；连带改口 XFER.01／WS.01／INTEG.04 三句同时落。结项复跑：`cmake --build --preset linux` 通过、`ctest --preset linux -E etest_` **106/106 全绿**、`./build/borealis_test_runner` **1337/1337 全绿**、`check_display_literals` PASS 211 files。本条不称「可用」：判据 ⑤ 无证人、§5 八条真机走查未做、Win32／MSVC 腿未验。
