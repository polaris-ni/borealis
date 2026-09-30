# CHANGELOG.md — Borealis 需求规格书变更历史

> 本文件记录 [`SPECIFICATIONS.md`](SPECIFICATIONS.md) 的版本演进，并保存**旧纯数字需求编号 → 新标识**的完整映射。
> 规格书正文只述需求，不含优先级与交付分期（那部分属 [`PLAN.md`](PLAN.md)）。本文件是历史记录：早期版本条目沿用其**当时**的优先级与里程碑口径原文，不做回填改写，以便对照每一次调整的取舍依据。
> 现行需求标识规范见 [`SPECIFICATIONS.md`](SPECIFICATIONS.md) §1.4。

---

## v0.13（2026-09-30）窗口行数变化口径落定（裁决 7.17）

**动机**：终端状态机落地后要给 PTY 尺寸同步（`SPEC.FEAT.XFER.01`）接线，才发现规格书只裁决了**列宽**变化不 reflow（7.5），而「视口行数增减时已有行与 scrollback 历史怎么处理」在文档里是空的，代码侧只留 `TODO(SPEC.FEAT.XFER.01)`。三种取向（从历史收回 / 直接丢弃 / 固定总容量）对「能回滚多少行」的承诺差别很大，不该由实现自行择一，故先落裁决再落代码。

- **新增裁决 7.17**：行数变化按**移动窗口边界、底部锚定**处理——变高从 scrollback 顶部收回历史填满，变矮时顶行溢出进历史，超出容量从最旧端丢弃，历史不足时顶部补空白；并明确 **scrollback 的配置容量不随视口变高而缩水**，代价（变高必然扩容重分配、须靠 XFER.01 去抖压频）写入该条。
- **`SPEC.FEAT.TERM.04` 就地补指针**：原句只指向裁决 7.5（列宽），现同时指向 7.17，免得「默认 10,000 行」被读成随窗口高度浮动的值。需求语义未变，仅补交叉引用。
- **`ARCHITECTURE.md` §4.5 连带**：补行数变化的窗口边界语义、环拉直的原因（容量模数一变，跨旧边界的行号会错位），并点明尺寸变化属整屏位移级事件、一律以整屏脏通知副本重建（§3.4），以及终端模式随尺寸收敛的三条（DECSTBM 带复位、制表位按新列宽重建、光标与 DECSC 保存位置一并钳制）。
- **§7 标题的追加区间**改为 7.16–7.17。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次为需求空洞补裁决，不改任何既有需求语义。

---

## v0.12（2026-09-30）审查整改：引用形态、口径矛盾与架构落点补全

**动机**：审查报告剩余项全量整改。本批**不改动任何需求语义**，优先级与阶段主体不变（PLAN.md 仅做子句级拆分标注），内容为引用形态修正、文档间口径矛盾消除与已排期需求的架构落点补全。

- **引用形态与事实口径修正**：§3.3 第 4 条「Aurora 规则 §5.8」改为「Aurora 主仓 `AGENTS.md` §5 规则 8」（该规则实测存在——「禁止直接触达标准输出」，原引用未指明文档名且 `§5.8` 形态易误读为不存在的子章节号）；`SPEC.NF.A11Y.01` 与 `PLAN.md` 观察池对 Aurora `ACCESSIBILITY_DESIGN` 草案的引用改为已实测存在的 Aurora `codespec/ARCHITECTURE.md` §8.5（Aurora 自身规则禁引 draft 文档）；附录 A.2 G2 的「五后端」补明确口径（win32 / x11 / wayland / glfw / macos，headless 经同一 dispatcher 路径用于单测）；§4.1 `SPEC.FEAT.TERM.06`「less/less」笔误改「less/more」；§7 标题的裁决基线日期按 CHANGELOG 各版本实际落定日期拆分（7.1–7.4 于 2026-09-22、7.5–7.10 于 2026-09-23）。
- **`ARCHITECTURE.draft.md` 口径矛盾消除**：§1.3 命名空间清单补 `borealis::platform`（与 §2.1 一致，标注纯实现层无公共头）；§4.4 删除「alt screen 独立 scrollback」的自相矛盾表述，改为「备屏不与主屏 scrollback 混排、备屏本身无回滚（xterm 惯例，滚轮由 alternate scroll 承接）」。
- **架构落点补全**（此前一批已排期需求在架构文档无归属）：§3.1 补单实例转交通道指针；§3.4 补「主线程读取 cell 内容方式未定」指针；§9.2 过渡自绘标注适用条件（M0 完成前应用侧不开工，本节为 G1 延期时的预研备路径）；新增 §7.5「SSH 之上的子系统归属」（SFTP / 隧道 / 公钥推送 / 档案 / 凭据 / 连接管理器 UI / 会话日志的模块归属；系统通知依赖 G11 属观察池）与 §8.4「其余工作区能力归属」（多窗口 / 全屏 / pane 缩放 / 广播输入）；§10.3 搜索补持锁口径指针；新增 §10.6「无障碍（延后项的落点约束）」（自绘网格的语义树钩子须应用侧实现）；§7.2 会话抽象增列第三案「基础接口 + 能力接口组合」；§16 待定清单增列 D（网格所有权与主线程读取方式，三案：持锁遍历 / 每帧快照 / 双副本）、E（单实例转交通道：命名管道 / 本地 socket / `WM_COPYDATA`）、F（schema 版本升级策略），A 案候选补第三案。
- **`PLAN.md` 分期标注修正**（子句拆分，优先级与阶段主体不变）：`SPEC.FEAT.WS.05` 原整条落 M2，但其「SSH 断线自动重连」子句依赖 M3 的 SSH 本体（M3 出口判据本就含「重连」）→ 标注为「M2（本地终端腿）+ M3（SSH 断线重连腿）」，M3 涉及需求同步补该腿；`SPEC.FEAT.INTERACT.03`（P0 / M1，含「选区复制」）依赖文本选择基本面，而 `SPEC.FEAT.INTERACT.02` 原整条落 M2 → 流式拖拽 / 矩形块基本面（不依赖框架缺口 G2，见 `ARCHITECTURE.draft.md` §9.6）前移 M1，双击 / 三击等其余仍留 M2（拆分模式沿用 `SPEC.NF.PLAT.01`「M1（Windows）+ M2（Linux）」先例）。
- **仓库卫生**：`.gitignore` 补 `/.workbuddy/`（AI 协作本地记录目录，此前 untracked 且未被忽略）。
- 需求条目数量（66 条）、标识体系、优先级与阶段主体均未变动；本批为引用 / 口径 / 落点修正，不涉及需求语义。

---

## v0.11（2026-09-30）中文输入与显示链路补全（裁决 7.16）

**动机**：规格书审查发现中文场景有三处需求空洞——此前只覆盖「显示宽度」（`SPEC.FEAT.TERM.08`），漏掉「输入」与「字形可得性」两个维度；而首要用户为中文开发者、串口默认编码为 GB18030（裁决 7.6），三者均属日常刚需而非增量特性。框架侧能力经实测均已具备，故本次为**需求侧补全，不新增 G 编号**。

- **新增 `SPEC.FEAT.INTERACT.06` 输入法（IME）**：preedit 就地渲染于光标单元格、候选窗按光标屏幕坐标定位、组合中间态不发往会话、commit 文本经会话编码写入。按 §1.4 第 1 条取 INTERACT 域最大序号 + 1（原为 05）。连带回填 §2.1 能力域表（终端交互改 `01–06` 并补「输入法」）与 §4.3 小节标题；附录 A.1 的 IME 行此前是唯一「箭头后无需求标识」的行（因无处可指），现指向本条。
- **`SPEC.FEAT.RENDER.02` 补 CJK 缺字回退链**：内置 Cascadia Code **不含汉字字形**，无回退则 `SPEC.FEAT.TERM.09` 的「GB18030 串口输出正确显示」验收必然呈现豆腐块。补默认回退至系统等宽 CJK 字体、回退链顺序可配、回退后仍按双宽占位而不破坏网格对齐，并补验收判据；§2.1 网格渲染行同步补「CJK 缺字回退」。
- **`SPEC.FEAT.TERM.09` 编码口径由单向改双向**：原文与 §1.3 术语表均只定义解码方向（字节流 → 文本），但 `SPEC.FEAT.CONN.05` 串口（默认 GB18030）需输入中文并发送行尾序列，用户输入必须**反向编码**为会话编码字节，否则设备侧收到乱码。现补发送方向（覆盖键入、IME commit、粘贴、片段发送）与「目标编码不可表示字符」的可配策略（默认替换 + 一次性提示，不静默发乱码），§1.3 术语表同步改为双向定义，验收判据补「设备侧收到合法 GB18030 序列」。
- **新增裁决 7.16** 记录上述三项的理由与代价；§7 标题的追加区间改为 7.11–7.16。
- **`PLAN.md` 连带**：`SPEC.FEAT.INTERACT.06` 赋 **P0 / M1**（首个交付阶段在本地终端输入中文即触发，不可延后），M1 出口判据补「中文可输入可显示」；M4 出口判据补「中文按 GB18030 编码发送」；§3 总表增行，计数由 65 条改为 **66 条**、P0 由 22 条改为 **23 条**。`SPEC.FEAT.RENDER.02` 与 `SPEC.FEAT.TERM.09` 的既有优先级与阶段不变（本次仅为其增补子句）。
- **`ARCHITECTURE.draft.md` 连带**：§3.5 输入路径补「文本 → 会话编码」环节与输入法路径；§6.1 补双向口径；§6.2 由「解码失败处理」改为「转换失败处理」并补编码方向策略；新增 §10.5 输入法（点明自绘网格下框架不会自动把 cell 内容暴露给输入法，且坐标换算须与 DPI 口径一致）；§11.1 补 CJK 缺字回退链。

---

## v0.10（2026-09-29）修正对框架 `TabBar` 能力的高估，映射表名称笔误

- **附录 A.1 新增「标签栏」行**：`TabBar`（`widget/tab_bar.h`）经实测只有 `on_change` / `on_close` 两个事件，属性为选中序号、栏高、四个**全局**配色、指示器粗细、字号与内边距；**无拖拽重排、无逐标签图标、无逐标签状态角标**。此前 A.1 未收录该控件，导致 `ARCHITECTURE.draft.md` §8.1 写下「框架 `tab_bar` 可直接承载标签栏」这一未经实测的乐观结论——与 `SPEC.FEAT.WS.02` 对 `Splitter` 的告警（不得因框架已有控件而低估工作量）属同一类错误，故按同一口径补录能力边界。
- **连带修正 `ARCHITECTURE.draft.md` §8.1**：改为「`TabBar` 仅覆盖选中切换与关闭」，并点明 `SPEC.FEAT.WS.01` 的拖拽重排与图标、`SPEC.FEAT.WS.04` 的逐标签状态角标须应用侧自研。
- **映射表 `#44` 名称修正**：原写「近乎零成本」，应为「命令面板」（`SPEC.FEAT.WS.07`）。已用 v0.6 提交原文核对——其 §2.1 工作区行为 `#18–#23 #44–#49`，对应 `SPEC.FEAT.WS.01–12`，故 `#44` 确为 WS.07；「近乎零成本」是附录 A.1 中描述 WS.07 复用成本的措辞串入了名称列。「历史条目原文照录」原则约束的是叙述性版本条目，映射表为 v0.7 生成的工具表，属笔误可修。
- 需求条目数量（65 条）、标识体系、裁决结论与分期结构均未变动；本次为事实修正与笔误修正，不涉及需求语义。

---

## v0.9（2026-09-29）工作区目录改名落地，命名例外条款删除

- 由人在 IDE 会话外把工作区目录改名为 `borealis`，裁决 7.14 设定的「目录名为唯一例外，改名后须同步回填」条件就此达成，按该条自身要求删除例外表述。
- **裁决 7.14 就地修订**：编号保留（撤销会让其他处的 `7.14` 引用变成伪死链），正文改为「目录与文档自称均不再出现旧名」并说明例外条款已失效及原因。
- **裁决 7.1 括注收回**：7.1 曾以括注形式指向 7.14 的例外，现只述产品名本身。
- **旧名指涉清零**：本文开头括注、7.1、7.14 与 `AGENTS.md` §1 命名行的四处已清理；全仓（含 `PLAN.md`）grep 确认无残留。
- 需求条目数量（65 条）、标识体系与分期结构均未变动；本次为纯文档口径回填，不涉及需求语义。

---

## v0.8（2026-09-29）Ambiguous 宽度默认值落定

- **新增裁决 7.15**：East Asian Width 的 Ambiguous 类字符**默认按单宽（窄）**，并提供 **profile 级覆盖为双宽**。理由与场景分工见该条——UTF-8 本机/SSH 场景取单宽与主流终端一致，GB18030/GBK 串口等少数场景以覆盖项解决，故不把默认值翻到对多数场景错误的一侧。
- **SPEC.FEAT.TERM.08 就地修订**：写入上述默认值与覆盖粒度（覆盖项与 SPEC.FEAT.TERM.09 的会话编码项协同），并补出验收判据（两种配置下光标列位与占位一致、不出现半格错位）。
- **附录 A.2 G1 连带约束**：框架宽度判定原语的**入参须含 Ambiguous 宽度模式**，不得在框架内硬编码单/双宽——否则应用侧只能绕开公共 API 自行查表，违反「渲染路径不私改」的约束。
- 标识体系、需求条目数量（65 条）与分期结构均未变动；本次为需求语义增量。

---

## v0.7（2026-09-29）需求标识改造与「纯需求」化

**动机**：两项结构性指令——① 规格书应为纯粹的需求描述，不得承载优先级（`P0`…）与任务分期（`M1`…），计划类内容另立文档；② 不得用纯数字作需求标识，因为插入新需求会导致整体编号偏移，交叉引用一改就是百余处。

**改动**：

- **标识体系落地**：新增 §1.4 需求标识规范，格式 `SPEC.<类别>.<域>.<NN>`（类别 `FEAT`/`NF`；FEAT 域 `TERM` `RENDER` `INTERACT` `XFER` `WS` `CONN` `PREF` `INTEG`，NF 域 `PERF` `PLAT` `RELI` `PKG` `A11Y`），并附 5 条稳定性规则：只追加不复用不重排、改域即换号且映射入本文件、引用一律写全、同域连续可写区间、更名不改号。
- **全量重编号**：65 条需求（`SPEC.FEAT.*` 55 条 + `SPEC.NF.*` 10 条）由 `#1`–`#65` 改为其域内语义标识，映射见下方「旧需求编号 → 新标识」表。重编号以脚本机械执行并对每条替换断言，正文与附录的交叉引用零残留（含 §2.1 覆盖表、§7 裁决、附录 A.1/A.2/A.3）。
- **优先级与分期移出正文**：新建 [`PLAN.md`](PLAN.md)，承接优先级定义、阶段划分与出口判据、需求优先级总表、延后子项清册、观察池、框架缺口补入排期、跨平台分期决策。
- **正文去标记**：约 30 处内联 `P0`–`P3`/`M0`–`M5` 措辞改写为语义表述（「延后子项」「延后观察项」「分期见 PLAN.md」），需求条目只保留「本项边界」不含「落期」；§6 由里程碑表改为指向 PLAN.md 的指针，章节号序列保持不变以免 §6 之后的锚点漂移。
- **裁决连带调整**：7.10 重定为「工作区下限」的需求侧表述；7.11 只保留「平台层自始接口隔离」这一需求约束，交付分期移入 PLAN.md §7；7.12 措辞去里程碑化。
- **表格压缩**：§2.1 覆盖表与 §4 各小节标题改用区间写法（如 `SPEC.FEAT.TERM.01–08`），并标注非功能需求共 10 条不入能力域表。
- **尾部收敛**：原「## 版本」小节整体迁入本文件，规格书尾部只留当前版本号与指向本文件的指针。

**影响**：纯结构性迁移，**无任何需求语义变更**——每条需求的验收判据、边界与裁决结论与 v0.6 一致。

---

## 旧需求编号 → 新标识

| 旧编号 | 新标识 | 名称 |
|:---|:---|:---|
| `#1` | `SPEC.FEAT.TERM.01` | VT 解析器 |
| `#2` | `SPEC.FEAT.TERM.02` | 颜色支持 |
| `#3` | `SPEC.FEAT.TERM.03` | 主/备屏幕缓冲 |
| `#4` | `SPEC.FEAT.TERM.04` | Scrollback 回滚 |
| `#5` | `SPEC.FEAT.TERM.05` | 滚动区域与光标控制 |
| `#6` | `SPEC.FEAT.TERM.06` | 鼠标上报 |
| `#7` | `SPEC.FEAT.TERM.07` | OSC 集成 |
| `#8` | `SPEC.FEAT.TERM.08` | 宽字符 |
| `#9` | `SPEC.FEAT.RENDER.01` | 等宽网格渲染 |
| `#10` | `SPEC.FEAT.RENDER.02` | 字体 |
| `#11` | `SPEC.FEAT.RENDER.03` | 属性渲染 |
| `#12` | `SPEC.FEAT.RENDER.04` | 光标 |
| `#13` | `SPEC.FEAT.INTERACT.01` | 键盘映射 |
| `#14` | `SPEC.FEAT.INTERACT.02` | 文本选择 |
| `#15` | `SPEC.FEAT.INTERACT.03` | 复制粘贴 |
| `#16` | `SPEC.FEAT.INTERACT.04` | 终端内搜索 |
| `#17` | `SPEC.FEAT.INTERACT.05` | URL 检测 |
| `#18` | `SPEC.FEAT.WS.01` | 多标签页 |
| `#19` | `SPEC.FEAT.WS.02` | 任意分屏 |
| `#20` | `SPEC.FEAT.WS.03` | 多窗口 |
| `#21` | `SPEC.FEAT.WS.04` | 标签状态提示 |
| `#22` | `SPEC.FEAT.WS.05` | 会话生命周期 |
| `#23` | `SPEC.FEAT.WS.06` | 全屏 |
| `#24` | `SPEC.FEAT.CONN.01` | 本地终端 |
| `#25` | `SPEC.FEAT.CONN.02` | SSH 连接 |
| `#26` | `SPEC.FEAT.CONN.03` | SSH 档案管理 |
| `#27` | `SPEC.FEAT.CONN.04` | SFTP 浏览器 |
| `#28` | `SPEC.FEAT.CONN.05` | 串口终端 |
| `#29` | `SPEC.FEAT.CONN.06` | Telnet |
| `#30` | `SPEC.FEAT.CONN.07` | 连接管理器 UI |
| `#31` | `SPEC.FEAT.PREF.01` | 主题 |
| `#32` | `SPEC.FEAT.PREF.02` | 设置面板 |
| `#33` | `SPEC.FEAT.PREF.03` | 持久化 |
| `#34` | `SPEC.FEAT.PREF.04` | 快捷键系统 |
| `#35` | `SPEC.FEAT.PREF.05` | i18n |
| `#36` | `SPEC.FEAT.PREF.06` | 零配置可用 |
| `#37` | `SPEC.NF.PERF.01` | 输入延迟 |
| `#38` | `SPEC.NF.PERF.02` | 渲染吞吐 |
| `#39` | `SPEC.NF.PERF.03` | 启动时间 |
| `#40` | `SPEC.NF.PERF.04` | 内存 |
| `#41` | `SPEC.NF.PLAT.01` | 跨平台 |
| `#42` | `SPEC.NF.RELI.01` | 可观测性 |
| `#43` | `SPEC.NF.A11Y.01` | 无障碍基线 |
| `#44` | `SPEC.FEAT.WS.07` | 命令面板 |
| `#45` | `SPEC.FEAT.WS.08` | Pane 缩放 |
| `#46` | `SPEC.FEAT.WS.09` | 广播输入 |
| `#47` | `SPEC.FEAT.WS.10` | 撤销关闭标签 |
| `#48` | `SPEC.FEAT.WS.11` | 会话恢复 |
| `#49` | `SPEC.FEAT.WS.12` | Quick Terminal |
| `#50` | `SPEC.FEAT.CONN.08` | SSH 隧道 |
| `#51` | `SPEC.FEAT.CONN.09` | 凭据安全存储 |
| `#52` | `SPEC.FEAT.CONN.10` | 密钥管理器 |
| `#53` | `SPEC.FEAT.CONN.11` | 会话日志 |
| `#54` | `SPEC.FEAT.INTEG.01` | OSC 133 命令块 |
| `#55` | `SPEC.FEAT.INTEG.02` | OSC 7 工作目录 |
| `#56` | `SPEC.FEAT.INTEG.03` | 系统通知 |
| `#57` | `SPEC.NF.PKG.01` | 打包分发 |
| `#58` | `SPEC.FEAT.INTEG.04` | CLI 启动参数 |
| `#59` | `SPEC.FEAT.XFER.01` | PTY 尺寸同步 |
| `#60` | `SPEC.FEAT.RENDER.05` | 缩放适配 |
| `#61` | `SPEC.FEAT.TERM.09` | 字符编码 |
| `#62` | `SPEC.NF.PERF.06` | 输出背压 |
| `#63` | `SPEC.FEAT.PREF.07` | 配置韧性 |
| `#64` | `SPEC.FEAT.CONN.12` | 剪贴板访问授权 |
| `#65` | `SPEC.NF.PERF.05` | 空闲资源占用 |

---

## 历史版本（v0.1–v0.6，原文照录）

> 以下条目为各版本当时的记录原文，其中出现的 `P0`–`P3`/`M0`–`M5` 是历史口径（现行定义见 [`PLAN.md`](PLAN.md)）；需求编号已随 v0.7 统一替换为语义标识，历史事实本身未作改写。

- v0.1（2026-09-20）：初稿。
- v0.2（2026-09-22）：业界查漏补缺（对照 Windows Terminal / Termius / XShell / WezTerm / iTerm2 / SecureCRT）——新增 SPEC.FEAT.WS.07、SPEC.FEAT.WS.08、SPEC.FEAT.WS.09、SPEC.FEAT.WS.10、SPEC.FEAT.WS.11、SPEC.FEAT.WS.12、SPEC.FEAT.CONN.08、SPEC.FEAT.CONN.09、SPEC.FEAT.CONN.10、SPEC.FEAT.CONN.11、SPEC.FEAT.INTEG.01、SPEC.FEAT.INTEG.02、SPEC.FEAT.INTEG.03、SPEC.NF.PKG.01、SPEC.FEAT.INTEG.04（命令面板、pane 缩放、广播输入、会话恢复、SSH 隧道、凭据安全存储、密钥管理、会话日志、snippets、OSC 133/7 集成、CLI 参数、打包分发等）；修订 SPEC.FEAT.TERM.01（bracketed paste/focus reporting）、SPEC.FEAT.TERM.06（alternate scroll）、SPEC.FEAT.RENDER.03（bold-is-bright/最小对比度）、SPEC.FEAT.INTERACT.02/SPEC.FEAT.INTERACT.03（智能选择、quick select、copy-on-select、右键行为）、SPEC.FEAT.CONN.01（TERM 注入/自定义命令）、SPEC.FEAT.CONN.02（keyboard-interactive/agent 转发）；裁剪表补 6 项（自动更新、Warp AI、Sixel、复用域、tmux control、Triggers）；缺口表补 G9–G11（全局热键/音频/通知）；里程碑扩 M5 观察池。
- v0.3（2026-09-22）：四项裁决落定（§7 改为已裁决）——产品名 Borealis、SSH 库 libssh2（接口抽象隔离）、默认字体内置 Cascadia Code、scrollback 默认截断；同步回填标题、SPEC.FEAT.TERM.04 SPEC.FEAT.RENDER.02 SPEC.FEAT.CONN.02 SPEC.NF.PKG.01。
- v0.4（2026-09-22）：规格书审查查漏补缺（对照 Aurora 主仓源码逐项实测 + 业界能力面；该行原引的审查报告 `REVIEW_SPECIFICATIONS_v0.3.md` 经 2026-09-29 全仓核查并不存在，其结论已全部并入本文，故不再作为引用目标）——
  - **新增 7 条**：SPEC.FEAT.XFER.01 PTY 尺寸同步（P0）、SPEC.NF.PERF.06 输出背压（P0）、SPEC.FEAT.RENDER.05 缩放适配（P1）、SPEC.FEAT.PREF.07 配置韧性（P1）、SPEC.FEAT.TERM.09 字符编码（P1）、SPEC.FEAT.CONN.12 剪贴板访问授权（P2）、SPEC.NF.PERF.05 空闲资源占用（P1）；
  - **就地修订 13 条**：SPEC.FEAT.TERM.01（DEC Special Graphics 字符集、`DECAWM`/`DECOM`/`IRM`、`DA1`/`DSR`）、SPEC.FEAT.TERM.07（OSC 标题消费链路与优先级）、SPEC.FEAT.INTERACT.01（`DECCKM`/keypad 应用模式）、SPEC.FEAT.INTERACT.03（复制语义三开关）、SPEC.FEAT.INTERACT.04（100k 行搜索指标）、SPEC.FEAT.INTERACT.05（打开前确认与协议白名单）、SPEC.FEAT.WS.02（任意分屏四义 + Splitter 二元约束 + 焦点路由自研）、SPEC.FEAT.WS.06 SPEC.FEAT.WS.10（P2 改标 P1）、SPEC.FEAT.WS.01（一度改标 P1，见 v0.5 回改 P0）、SPEC.FEAT.CONN.02（连接复用/旧算法兼容）、SPEC.FEAT.CONN.05（发送行尾序列/十六进制视图）、SPEC.FEAT.WS.07（改复用框架 `CommandPalette`）、SPEC.FEAT.CONN.09（审计范围界定）、SPEC.FEAT.CONN.11（默认关闭 + 风险提示 + 敏感过滤）；
  - **legend 修订**：P2 的交付窗口由「M4」放宽为「M3-M4，允许因主题内聚随 M3 一并交付」（SSH 族的 SPEC.FEAT.CONN.04 SPEC.FEAT.CONN.08 SPEC.FEAT.CONN.10 原本即落 M3）；M5 行的 `SPEC.FEAT.WS.04(beep)` 改为非编号写法，避免被解析为 SPEC.FEAT.WS.04 整体进 M5；
- v0.5（2026-09-23）：M1 工作区下限上调（裁决 7.10）——**SPEC.FEAT.WS.01 多标签页由 P1 回改为 P0、SPEC.FEAT.WS.02 分屏由 P1 改 P0，二者均由 M2 前移至 M1**；SPEC.FEAT.WS.02 重写为「任意分屏」并按**任意方向 / 任意深度 / 任意比例 / 任意 pane 数**四义界定，明确每一层为多子 pane 容器而非两两嵌套，并点明框架 `Splitter` 仅二元、pane 树须自研；与 SPEC.FEAT.XFER.01 PTY 尺寸同步标注强耦合。里程碑连带调整：M1 主题改为「多标签 + 任意分屏的本地终端」并收 SPEC.FEAT.WS.01 SPEC.FEAT.WS.02，M2 主题改为「工作区打磨」并移出 SPEC.FEAT.WS.01 SPEC.FEAT.WS.02；新增裁决 7.10 记录该决策及其工作量代价。
  - **新增裁决 5 项**：7.5 不 reflow、7.6 串口默认 GB18030、7.7 单实例转交落点、7.8 单进程模型、7.9 日志默认关闭；
  - **里程碑重排**：M1 收 SPEC.FEAT.XFER.01 SPEC.NF.PERF.06，M2 收 SPEC.FEAT.RENDER.05 SPEC.FEAT.PREF.07 SPEC.NF.PERF.05，M3 收 SPEC.FEAT.TERM.09，M4 收 SPEC.FEAT.CONN.12，M5 观察池收 SPEC.NF.A11Y.01（无障碍基线不再悬空）；
  - **附录 A**：路径改相对主仓根目录、引用改符号名（去除全部 `file:line` 锚点）、修正 4 处位置（`commands.h` / `splitter.h` / `theme_scope.h` / `storage.h`）、补 `CommandPalette` 与可拖拽 `Splitter` 两项高价值可复用能力、补 X11/Wayland IME 与 `PerfOverlay`、新增 G12 待评估项。
- v0.6（2026-09-29）：开工前口径对齐（框架现状按 Aurora 当日活动分支复核实测）——
  - **新增裁决 4 项**：7.11 跨平台交付分期（SPEC.NF.PLAT.01 保持 P0，M1 只交付 Windows、Linux 等价随 M2，并要求平台层从 M1 起即接口隔离）、7.12 应用侧三方依赖经 `find_package` + vcpkg（含 `x64-windows-static-md` 的 CRT 约束，标注为未经真机验证的推断）、7.13 框架缺口按类别分流（渲染与事件链路先补框架，交互体验类留应用侧）、7.14 命名统一为 `borealis`（修订 7.1 的「目录名不变」括注，工作区目录名列为唯一例外）；
  - **附录 A 事实修正 3 处**：G10 音频关闭（`AudioContext`/`AudioBuffer` 图 API 恒编译，WASAPI 与 ALSA 后端已实现，须经 `AURORA_ENABLE_AUDIO` 开启）、G12 DPI 通知关闭（`Surface::set_scale_change_handler` + Win32 `WM_DPICHANGED` 已接线，Linux 三后端未接线故留单腿）、A.1 相应新增「音频播放」「DPI 缩放变更上报」两行复用能力；
  - **G1/G2 形态落定**：G1 = `Painter` 批量文本 run 原语 + 等宽整像素 cell 度量 + East Asian Width 判定进框架（网格模型与颜色合成留应用侧，原语只收合成后的最终值），并记录 `FontEngine` 的 dp 测量与物理光栅宽度偏差为不可复用现有链路的依据；G2 = `click_count` 框架统一自算、五后端一致；A.3 反哺承诺据此改写（基准由「候选用例」升为随 M0 落地并挂 10% 回归门禁）；
  - **里程碑连带**：M0 出口判据补度量/EAW/基准门禁并将「G1–G3」更正为「G1–G2」（G3 属应用域，本就不进框架）；M1 主题标注 Windows、收 SPEC.NF.PLAT.01 的 Windows 侧；M2 主题改为「工作区打磨 + Linux 等价」并收 SPEC.NF.PLAT.01 与 SPEC.FEAT.RENDER.05 的 Linux 缩放腿；M5 行的缺口清单由 G9/G10/G11 收窄为 G9/G11；
  - **就地修订 2 条**：SPEC.FEAT.WS.04（可听铃声的能力缺口已消除，仅余排期取舍，优先级不动）、SPEC.FEAT.RENDER.05（框架现状与残留单腿）；SPEC.NF.PLAT.01 补交付窗口说明；
  - **文档缺陷修正 2 处**：A.2 表头声明 5 列而全部数据行为 4 列（「缺口」列本就内含事实依据），表头改为 4 列以消除渲染错位；v0.4 版本行所引的 `REVIEW_SPECIFICATIONS_v0.3.md` 全仓不存在，已就地标注为失效引用（与本仓即将沿用的「引用必须可达」口径一致）。
