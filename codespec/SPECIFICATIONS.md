# SPECIFICATIONS.md — Borealis 终端管理软件需求规格书

> 本文件是 **Borealis**（命名口径见裁决 7.14）的需求规格总纲，仿 Aurora `codespec/SPECIFICATIONS.md` 形态。
> 章节号统一纯数字点分层级（`1` / `1.1` / `1.1.1`）；需求标识见 §1.4，与章节号并存。
> **本文只述需求**：不表达优先级、不表达交付分期与任务划分——那部分属 [`PLAN.md`](PLAN.md)（里程碑、优先级映射、观察池排期）。需求条目里出现的「延后子项」「延后观察项」只是需求本身的边界标注，其落期由 PLAN.md 决定。
> 依据框架现状（Aurora alpha，2026-09-20 实测、2026-09-22 与 2026-09-29 复核，见附录 A）写成；技术选型与架构留给 `codespec/ARCHITECTURE.md`。

---

## 1 项目定位

### 1.1 一句话定位

基于 **Aurora**（C++20 跨平台 AI-first GUI 库）开发的跨平台终端管理软件：以本地终端、SSH、串口、Telnet 四类连接为核心，提供多标签/分屏工作区，产品形态与体验对齐 Tabby。

### 1.2 目标与非目标

目标：

1. 做出**日常可自用**的全功能终端管理工具，替代既有终端（Windows Terminal / Tabby / iTerm）之一部或全部。
2. 以真实重度场景（高频 IO、大量文本、长会话、多连接）**间接催熟 Aurora**：渲染性能、文本网格、输入链路、持久化、多窗口均被推向极限验证。
3. 成为 Aurora 的**旗舰级示例工程**：演示框架完整能力面，同时暴露框架缺口并驱动其补齐（缺口分析独立成附录 A，不与需求逐条挂钩）。

非目标：

1. 不做插件体系与第三方扩展市场。
2. 不做配置同步（Vault）与云端服务。
3. 不做 GPU 终端渲染专项优化（跟随 Aurora Painter 演进，不自带渲染后端）。
4. 不做分页器 / 编辑器等 shell 内工具的替代品。

### 1.3 术语

| 术语 | 含义 |
|:---|:---|
| 终端仿真 | 模拟 xterm 类终端对转义序列（CSI/OSC/SGR 等）的解释与呈现 |
| PTY | 伪终端；Windows 侧指 ConPTY，Unix 侧指 `forkpty` 系 |
| ConPTY | Windows 控制台伪终端 API；尺寸变更须经 `ResizePseudoConsole` 下发 |
| 会话 Session | 一次连接的完整生命周期（本地 shell / SSH / 串口 / Telnet） |
| Pane | 分屏中的单个终端格；一个标签页内可含多个 pane 的嵌套分割 |
| 工作区 Workspace | 窗口内标签页 × 分屏的布局组合 |
| 单实例 | 同一用户会话仅运行一个应用进程；后续启动的参数经进程间协议转交首个实例 |
| Scrollback | 终端主缓冲区之上的历史回滚行存储 |
| 备屏幕 Alternate screen | `vim`/`htop` 类全屏程序使用的独立缓冲 |
| 字符编码 | 会话字节流与文本之间的**双向**转换规则（UTF-8 / GB18030 / Latin-1 等）：解码为「字节流 → 文本」，编码为「文本 → 字节流」（发送方向，见 `SPEC.FEAT.TERM.09`）；与「宽字符」为两个独立维度 |
| Profile | 一组可复用的连接参数（主机、认证方式、串口参数等） |

### 1.4 需求标识规范

需求标识格式为 **`SPEC.<类别>.<域>.<NN>`**，四段含义：

| 段 | 取值 | 含义 |
|:---|:---|:---|
| 类别 | `FEAT` / `NF` | 功能需求（§4）/ 非功能需求（§5） |
| 域（FEAT） | `TERM` `RENDER` `INTERACT` `XFER` `WS` `CONN` `PREF` `INTEG` | 功能域，与 §2.1 能力域表逐项对应 |
| 域（NF） | `PERF` `PLAT` `RELI` `PKG` `A11Y` | 性能 / 跨平台 / 可观测与韧性 / 打包分发 / 无障碍 |
| 序号 | `01`–`99` | 域内递增的两位十进制 |

**稳定性规则**（编号体系存在的唯一理由就是「永不偏移」，故规则从严）：

1. **只追加，不复用、不重排**：新增需求取所属域内当前最大序号 + 1；某条需求被裁剪或撤销后，其编号即作废，但**不得被后续需求占用**，以免同一标识在不同时间点指代不同需求。
2. **改域即换号**：需求迁移到另一个域时分配新号，旧号 → 新号映射记入 [`CHANGELOG.md`](CHANGELOG.md)，正文不保留旧号。
3. **引用一律写全**：正文、表格与提交信息引用需求时写完整标识（如 `SPEC.FEAT.TERM.04`），不缩写成域内尾号。
4. **同域连续可写区间**：同一域内连续编号可写作 `SPEC.FEAT.TERM.01–08`（区间末端省略前缀）；跨域引用一律逐条列举，不设复合区间。
5. **更名不改号**：需求标题措辞调整不影响其标识——编号是身份，标题是描述。

> 历史沿革：本文早期版本使用 `1`–`65` 的纯数字需求编号。纯数字在插入新需求时必然整体偏移（改一条要改百余处交叉引用），故自 v0.7 起改为上述带域语义的不可变标识。旧号映射见 CHANGELOG.md。

---

## 2 产品范围（对齐 Tabby 裁剪表）

### 2.1 范围内

| 能力域 | 内容 | 对应需求 |
|:---|:---|:---|
| 终端仿真 | xterm-256color 级转义序列、真彩色、主/备屏、scrollback、鼠标上报、宽字符 | `SPEC.FEAT.TERM.01–08` |
| 网格渲染 | 等宽网格、字形缓存与 CJK 缺字回退、属性样式、光标形态、DPI 缩放适配 | `SPEC.FEAT.RENDER.01–05` |
| 终端交互 | 键盘映射、多击选择、复制粘贴、搜索、URL 检测、输入法 | `SPEC.FEAT.INTERACT.01–06` |
| 会话传输 | PTY 尺寸同步、字符编码与解码韧性 | `SPEC.FEAT.XFER.01`、`SPEC.FEAT.TERM.09` |
| 工作区 | 多标签、分屏、多窗口、状态提示、命令面板 | `SPEC.FEAT.WS.01–12` |
| 连接类型 | 本地终端、SSH（含档案管理）、SFTP 浏览、串口、Telnet | `SPEC.FEAT.CONN.01–07` |
| 连接安全与增强 | 凭据安全存储、密钥管理、SSH 隧道、会话日志、剪贴板访问授权 | `SPEC.FEAT.CONN.08–12` |
| Shell 集成 | OSC 133 命令块、OSC 7 目录继承、系统通知、CLI 参数 | `SPEC.FEAT.INTEG.01–04` |
| 外观与配置 | 主题、字体、设置面板、持久化、快捷键、i18n、配置韧性 | `SPEC.FEAT.PREF.01–07` |

> 非功能需求共 10 条（全部 `SPEC.NF.*`），见 §5，不列入本能力域表；本表的区间缩写规则见 §1.4 第 4 条。

### 2.2 明确裁剪（不做）

| 业界能力（来源） | 裁剪理由 |
|:---|:---|
| 插件体系 | 非目标；功能全量内置，扩展走版本迭代 |
| 配置同步（Vault/Gist） | 非目标；本地 JSON 持久化 + 本地导出导入（SPEC.FEAT.PREF.07）即可 |
| Zmodem 文件传输 | 依赖 SPEC.FEAT.CONN.04 覆盖主流场景；列为延后观察项（落期见 PLAN.md） |
| Docker/Kubernetes 连接类型 | Tabby 中本由插件提供 |
| Web 版 / 浏览器接入 | 非目标（Aurora 有 WasmSurface，留作远期可能） |
| 热键冲突时提示第三方工具 | 仅做应用内快捷键 |
| 自动更新 | 需 HTTP 客户端 + 签名校验链（框架缺口 G5）；发布以打包产物 + 手动更新为主 |
| Warp 式 AI 助手 / 命令块重构 | 非目标；OSC 133 集成（SPEC.FEAT.INTEG.01）覆盖其无 AI 子集 |
| 终端内图像协议（Sixel / kitty graphics / iTerm2 imgcat） | 渲染内核依赖位图混排路径，工作量与收益不成比；列为延后观察项（落期见 PLAN.md） |
| WezTerm 式多路复用域（Unix/SSH domain） | 与 tmux 生态重叠；远程多路复用交由 tmux 自身 |
| tmux control mode | 同上 |
| iTerm2 触发器（Triggers，正则动作） | 重配置低频；VT 序列统计（SPEC.NF.RELI.01）+ 会话日志（SPEC.FEAT.CONN.11）覆盖调试诉求 |
| 会话崩溃隔离（多进程模型） | 采纳单进程 + 会话级后台线程（裁决 7.8）；崩溃诊断靠 SPEC.NF.RELI.01 留存兜底 |

---

## 3 总体描述

### 3.1 用户画像

1. **开发者本人（首要）**：跨 Windows/Linux 双平台工作，重度终端用户，需要 SSH 多主机管理 + 串口调试。
2. **运维 / 嵌入式工程师（次要）**：多连接档案、串口参数配置、断线重连。

### 3.2 运行环境

- Windows 10+（Win32 后端，ConPTY）
- Linux X11 / Wayland（对应后端）
- macOS（依赖 `MacOSSurface` 骨架就绪；目标平台分期见 PLAN.md）

### 3.3 设计约束

1. UI 层**只使用 Aurora 公共 API**；框架缺口先补框架、再做应用，不在应用侧私改渲染路径。
2. 传输层（SSH/串口/Telnet/PTY）为平台相关实现，与 UI 之间以会话抽象解耦；线程模型遵守 Aurora「单线程 UI、后台线程池 + 信号回主线程」。
3. 配置持久化使用 Aurora `Preferences`（JSON 文件）。
4. 日志走 Aurora `Logger`，禁止裸标准输出（Aurora 主仓 `AGENTS.md` §5 规则 8）。

---

## 4 功能需求

### 4.1 终端仿真（`SPEC.FEAT.TERM.01–09`）

**SPEC.FEAT.TERM.01 VT 解析器** 实现 xterm/VT100/VT220 兼容转义序列解析：CSI/OSC/SGR/DEC 私有序列，含 bracketed paste（DECSET 2004，粘贴安全：交给 shell 判定而非应用改写）、focus reporting（1004，上报焦点到远端程序）、私有模式集（`?1` `?4` `?6` `?7` `?25` `?47` `?1049` `?2004` 等 DECSET/DECRST）全量登记；**字符集切换**（`ESC(0` / `ESC(B` DEC Special Graphics 线条字符集、`ESC%G` UTF-8 切换）；`DECAWM`（`?7` 自动换行开关）、`DECOM`（`?6` 原点模式）、`IRM`（`?4` 插入模式）须生效；`DA1` / `DSR`（设备属性与光标位置查询）须正确响应（`vim`/`tmux` 会查询）。解析器为纯逻辑模块，可脱离 UI 单元测试。覆盖度以「跑通 `vim`/`htop`/`tmux`/`top`/`less` + shell 提示符生态（starship/oh-my-posh）」为验收线，**且这些程序的边框线条字符不得显示为乱码字母**（即 DEC Special Graphics 必测）。

**SPEC.FEAT.TERM.02 颜色支持** 16 基本色（可由主题重映射）、256 色、24-bit 真彩色（`38;2;r;g;b`）。

**SPEC.FEAT.TERM.03 主/备屏幕缓冲** 支持 alternate screen 切换（`1049`/`47`/`1047`）；备屏内容不进 scrollback，退出备屏恢复主屏原状。

**SPEC.FEAT.TERM.04 Scrollback 回滚** 可配置容量（默认 10,000 行，上限 100,000）；超长行**默认截断**（裁决 7.4，省内存），策略可配（截断/换行）；窗口尺寸变化时的已有行处理见裁决 7.5（默认不 reflow）与裁决 7.17（行数变化按窗口边界移动、容量配置值不随视口缩水）。

**SPEC.FEAT.TERM.05 滚动区域与光标控制** DECSTBM 滚动区域、光标定位/保存恢复/可见性、字符擦除/插入/删除、制表位。

**SPEC.FEAT.TERM.06 鼠标上报** X10 / 普通 / 按钮事件 / SGR 扩展模式；`vim`/`htop` 内滚轮与点击可用；备屏模式下滚轮转方向键（alternate scroll，DECSET 1007）供 less/more 类程序翻页；上报模式与本地选择交互自动切换。

**SPEC.FEAT.TERM.07 OSC 集成** OSC 52 剪贴板写（读方向见 SPEC.FEAT.CONN.12）、OSC 8 超链接、OSC 0/2 标题设置。**标题消费链路**：OSC 设置的标题覆盖标签名，用户手动重命名的优先级更高（可配）；活动标签标题同步至窗口标题栏（与 SPEC.FEAT.WS.06 配合）。状态机侧的消费形态与提前落地范围见裁决 7.21。

**SPEC.FEAT.TERM.08 宽字符** 按 Unicode East Asian Width 处理 CJK 双宽占位；**Ambiguous 类字符（箱线字符、`±`/`°`/`→` 一类符号、全角标点等）默认按单宽（窄）处理，并可按 profile 覆盖为双宽**（裁决 7.15）——覆盖项与 SPEC.FEAT.TERM.09 的会话编码项协同（GB18030/GBK 与 CP437 类场景常需切换该宽度口径）；Emoji 呈现不要求完美对齐（延后观察项）；combining character 基础处理。验收：同一份含 Ambiguous 字符的输出，在默认配置与覆盖配置下分别按单宽/双宽排布，且光标列位与占位一致（不出现半格错位）。

**SPEC.FEAT.TERM.09 字符编码** 会话级编码可配：UTF-8 为本地终端与 SSH 默认；**串口默认 GB18030**（裁决 7.6），备选 GBK / Big5 / Latin-1 / CP437。编码为**双向**口径（裁决 7.16）：**解码方向**（会话字节流 → 文本）失败按替换字符处理，且**不得中断解析、不得污染后续行**，非法字节序列计数进 SPEC.NF.RELI.01 可观测面板；**发送方向**（文本 → 会话编码字节）适用于一切写入会话的文本——键入字符、SPEC.FEAT.INTERACT.06 的输入法 commit 文本、SPEC.FEAT.INTERACT.03 的粘贴、快捷片段发送，遇目标编码不可表示的字符（如 GB18030 会话里输入 Emoji）按可配策略处理（替换 / 丢弃并提示 / 原样透传 UTF-8），**默认替换并给出一次性提示，不得静默发送乱码字节**。验收：GB18030 串口输出正确显示，且混入非法字节后终端持续可用；GB18030 串口会话中输入中文，发往设备的字节为合法 GB18030 序列（设备侧正确回显）。

### 4.2 网格渲染与光标（`SPEC.FEAT.RENDER.01–05`）

**SPEC.FEAT.RENDER.01 等宽网格渲染** 终端视区为固定单元格网格；单帧只重绘变更行/单元格（脏行 diff）；字形经缓存复用，避免整屏重排。性能验收见 SPEC.NF.PERF.02。

**SPEC.FEAT.RENDER.02 字体** 等宽字体选择（系统等宽字体枚举 + **内置 Cascadia Code 为默认**，裁决 7.3）、字号调整（含 Ctrl+滚轮缩放）、行高/字距可调。字体连字（ligature）为延后子项。**CJK 缺字回退链**（裁决 7.16）：内置 Cascadia Code **不含汉字字形**，故须配缺字回退——默认回退至系统等宽 CJK 字体（Windows 微软雅黑/等线一类、Linux 文泉驿或 Noto Sans Mono CJK 一类），回退链顺序可配；回退得到的双宽字符仍须按 SPEC.FEAT.TERM.08 占两格，**不得因回退破坏网格对齐**。本项是 SPEC.FEAT.TERM.09「GB18030 串口输出正确显示」验收的前置条件。验收：默认配置下含汉字的会话输出呈现为字形而非豆腐块，且列位与光标位置一致。

**SPEC.FEAT.RENDER.03 属性渲染** 前景/背景色、粗体/暗淡/斜体/下划线（含双线/波浪线）/删除线/反色/不可见，按 SGR 状态渲染；「粗体渲染为亮色」（bold-is-bright）与「最小对比度强制」（避免深色主题下不可读）均为可配开关。

**SPEC.FEAT.RENDER.04 光标** 块/下划线/竖线三形态（随 DECSCUSR 切换）、可配置闪烁频率、失焦时降级为空心/静止。

**SPEC.FEAT.RENDER.05 缩放适配** 支持系统 DPI 变更与跨屏 DPI 差异（Windows per-monitor DPI、Linux fractional scaling）；逻辑 dp → 物理像素的 cell 尺寸换算须保证网格对齐（cell 边界吸附、字形缓存按 DPI 分档失效重建）；字体缩放（SPEC.FEAT.RENDER.02）叠加在系统 DPI 之上，二者正交。验收：150% / 175% / 200% 及跨屏拖动后无错位、无字形模糊。**框架现状（2026-09-29 复核）**：DPI 变更通知的公共 API 已具备——`Surface::set_scale_change_handler`（`include/aurora/window/surface.h`），Win32 后端已在 `WM_DPICHANGED` 处理中接线上报，Headless 侧另有 `emit_scale_change` 测试钩子，故 **Windows 无框架阻塞**（G12 关闭）。唯 Linux 三后端（X11 / Wayland / GLFW）尚无缩放变化上报调用，该腿属 Linux 等价范畴（分期见 PLAN.md），核实确为框架缺失则按 A.3 以公共 API 反哺框架。

### 4.3 终端交互（`SPEC.FEAT.INTERACT.01–06`）

**SPEC.FEAT.INTERACT.01 键盘映射** 完整转发 Ctrl/Alt/Shift/Meta 组合键、功能键、方向键至 PTY；`Ctrl+C`/`Ctrl+Z` 等控制字符直通；Ctrl+Alt 系与 UI 快捷键冲突时以配置裁决。`DECCKM`（`?1` 光标键应用模式）与 keypad 应用模式（`DECKPAM`/`DECKPNM`）须生效——应用模式下方向键发送 `SS3 A` 而非 `ESC[A`。kitty keyboard protocol / `modifyOtherKeys` 为延后观察项。

**SPEC.FEAT.INTERACT.02 文本选择** 单击拖拽流式选择、双击选词、三击选行、列模式（矩形块选择）；选区随 scrollback 滚动跟随；选词界定符（word delimiters）可配；智能选择（双击落在引号/括号内时扩展选至配对符，iTerm2 语义）为延后子项；quick select 模式（快捷键后视区内 URL/路径/哈希自动标注字母标签，按标签即复制，WezTerm 语义）为延后子项。

**SPEC.FEAT.INTERACT.03 复制粘贴** 选区复制 / 粘贴（含多行粘贴警告与逐行发送节流）；粘贴换行处理策略可配（过滤/转换/原样）；bracketed paste 激活时（SPEC.FEAT.TERM.01）原样透传不节流；copy-on-select（选中即复制）与右键行为（复制/粘贴/菜单，Windows Terminal 三态）可配；从 scrollback 复制时剥离输出中的鼠标上报残留由 SPEC.FEAT.TERM.06 的模式切换保证。复制语义三项可配开关（**均默认关闭，保留原样为默认**）：剥离行尾空白、跨行反斜杠续行智能合并、去除 tmux 分屏边框字符。

**SPEC.FEAT.INTERACT.04 终端内搜索** Ctrl+F 浮层：大小写开关、正则开关、全部匹配高亮、Enter/N+Enter 前后跳转、匹配计数。性能：100,000 行 scrollback（SPEC.FEAT.TERM.04 上限）下首次搜索响应 ≤ 200 ms（P95）。

**SPEC.FEAT.INTERACT.05 URL 检测** 视区内 URL 识别（含 OSC 8），Ctrl/Cmd+点击或右键「打开链接」。打开前确认可配（默认「首次确认并记住同域」）；协议白名单（默认仅 http/https）；OSC 8 显式超链接与纯文本启发式识别的信任级别可分别配置。

**SPEC.FEAT.INTERACT.06 输入法（IME）** CJK 输入法在终端视区内可用（裁决 7.16）：组合中（preedit）文本**就地渲染在光标所在单元格处**，候选词窗口按光标的屏幕坐标定位（不得固定在窗口角落或跟随鼠标）；**组合中间态不发往会话**，仅在 commit 时把最终文本经会话编码写入（SPEC.FEAT.TERM.09 的发送方向；非 UTF-8 会话的编码发送随该条落地）；组合期间的光标形态与位置须可辨，避免与终端自身光标混淆。框架侧能力已具备——`TextCompositionEvent` 完整含 preedit，Win32 / X11 / Wayland 三后端均已实现（附录 A.1）；但终端为自绘网格，框架不会自动把 cell 内容暴露给输入法，故 preedit 绘制与「光标 → 屏幕坐标」换算由应用侧视口实现。验收：中文输入法在本地终端会话中可正常输入并正确显示；候选窗出现在光标处而非窗口角落；GB18030 串口会话中输入中文，设备侧收到合法 GB18030 字节序列。

### 4.4 工作区（`SPEC.FEAT.WS.01–12`）

**SPEC.FEAT.WS.01 多标签页** 新建/关闭/切换/重排（拖拽）/重命名；标签显示连接类型图标与活动状态；关闭前确认（有运行中进程时）。标签名来源与优先级见 SPEC.FEAT.TERM.07。多标签是「日常替换既有终端」的前提，单个标签页不足以构成替换，故属不可裁剪项（裁决 7.10）。

**SPEC.FEAT.WS.02 任意分屏** 视区内的 pane 布局须满足**任意**四义（裁决 7.10）：

1. **任意方向**：任一 pane 可横向或纵向切分，切分方向不绑定全局模板（不是「只能上下」或「只能左右」的固定布局）；
2. **任意深度**：分割可嵌套任意层（仅受 UI 最小 pane 尺寸约束，不预设层数上限）；
3. **任意比例**：分隔条拖拽调整，并提供键盘步进调整（等分/微调）——比例不被锁定为固定的 50:50；
4. **任意 pane 数**：每一层为**多子 pane 容器**而非两两嵌套，关闭任一 pane 时兄弟格就地合并、布局正确重排。

另需：焦点在二维 pane 树上的**方向键路由**（上下左右按几何位置跨层跳转）、当前 pane 视觉标识。
**实现约束**：框架 `Splitter` 为二元可拖拽分割器（附录 A.1），只能表达「两个子节点 + 一个比例」；本需求要求的应用侧 **pane 树（多子节点 + 递归布局 + 方向键路由）需自研**——这是本需求的主要工作量所在，不得因框架 `Splitter` 的存在而低估。与 SPEC.FEAT.XFER.01（PTY 尺寸同步）强耦合：每次切分/缩放/拖拽后须向受影响会话重发尺寸。

**SPEC.FEAT.WS.03 多窗口** 独立多窗口（依赖 Aurora `Application::open_window`）；窗口布局分别记忆。与单实例参数转交的交互见裁决 7.7。

**SPEC.FEAT.WS.04 标签状态提示** 活动输出时标签高亮、铃声（BEL）视觉提示、断线/退出状态角标；可听铃声（beep）的**框架依赖已具备**（附录 A.1「音频」、A.2 G10 关闭），故不再是能力缺口，仅余排期取舍（PLAN.md）；应用侧须显式开启 `AURORA_ENABLE_AUDIO`。

**SPEC.FEAT.WS.05 会话生命周期** 进程退出后保留终端内容供回看（可一键重启）；SSH 断线自动重连（次数与间隔可配）。

**SPEC.FEAT.WS.06 全屏** F11 全屏切换；窗口标题栏显示当前会话名（来源见 SPEC.FEAT.TERM.07）。

**SPEC.FEAT.WS.07 命令面板** `Ctrl+Shift+P` 呼出（Tabby/Windows Terminal 语义）：模糊搜索执行全部注册动作（开标签/分屏/切主题/改设置/连档案）。**复用框架既有 `CommandPalette`**（`widget/command_palette.h`：模态浮层 + 即时过滤 + 焦点作用域 + Enter/Esc/↑/↓ 全接管）与 `CommandRegistry`（`commands.h` 的 `add` / `invoke` / `search` / `command_fuzzy_score`）；本需求工作量收敛为「命令注册 + 主题适配 + 中文词条」。所有 UI 动作一律注册为 Command，快捷键绑定以 Command 为锚（与 SPEC.FEAT.PREF.04 统一）。

**SPEC.FEAT.WS.08 Pane 缩放** 快捷键将当前 pane 临时最大化/还原（zoom/unzoom，Windows Terminal/Tabby 语义）；缩放态视觉标识。

**SPEC.FEAT.WS.09 广播输入** 广播组：选中的多个 pane/标签进入组后，键盘输入同步发送至组内全部会话（运维批量操作场景）；组状态显著标识，避免误操作。

**SPEC.FEAT.WS.10 撤销关闭标签** `Ctrl+Shift+T` 重开最近关闭的标签（栈深 ≥10）；本地终端重连进程、SSH 按档案重连（内容不可恢复）。

**SPEC.FEAT.WS.11 会话恢复** 应用重启（含崩溃后）恢复上次的标签布局（连接类型/profile/分屏结构/各标签标题）；会话内容不恢复（本地终端无源可恢复）；恢复开关可配。区别于已裁剪的配置同步：仅本机记忆，不跨设备。配置本身损坏时的降级行为见 SPEC.FEAT.PREF.07。

**SPEC.FEAT.WS.12 Quick Terminal** 全局热键呼出/隐藏的下拉式终端（quake 模式，Windows Terminal/Guake 语义）；依赖 OS 级全局热键注册（框架缺口 G9）与无边框贴边窗口形态，两项均未具备，故为延后观察项，并同时列为框架候选项。

### 4.5 连接类型与会话（`SPEC.FEAT.XFER.01`、`SPEC.FEAT.CONN.01–12`）

**SPEC.FEAT.XFER.01 PTY 尺寸同步** 视区网格尺寸变化（窗口 resize / 分屏拖拽 / pane 缩放 / 全屏切换 / 字体缩放）时须同步下发新尺寸至 PTY 并重排：Unix `ioctl(TIOCSWINSZ)` + `SIGWINCH`，Windows ConPTY `ResizePseudoConsole`；resize 须去抖（连续拖拽期间合并下发，避免 shell 高频重排）；会话启动、分屏初始挂载、会话恢复时各下发一次初始尺寸。验收：连续拖拽窗口后 `vim` 行列不错位、`tmux` 状态栏宽度跟随、`htop` 重新布局。

**SPEC.FEAT.CONN.01 本地终端** 平台默认 shell 探测：Windows（PowerShell → cmd → WSL 探测）、Linux/macOS（`$SHELL` → `/bin/bash` → `/bin/sh`）；profile 可指定启动目录与环境变量；PTY 环境注入 `TERM=xterm-256color`、`COLORTERM=truecolor`；profile 亦可指定「自定义命令」模式（以任意可执行文件为会话主体，如直接挂 `ssh`/`docker exec`/串口工具）。

**SPEC.FEAT.CONN.02 SSH 连接** 基于 **libssh2**（裁决 7.2；传输层经接口抽象隔离，必要时可替换实现）：密码 / 私钥（含 passphrase）/ ssh-agent 认证，另含 keyboard-interactive（覆盖 TOTP/2FA 逐步问答）与主机级认证链（`none`→`publickey`→`password`，按服务器允许集自动降级）；known_hosts 首次确认与变更警告；keepalive 心跳；连接超时与错误分类提示（网络不通/认证失败/主机密钥变更）；断线重连（见 SPEC.FEAT.WS.05）；agent 转发（`ForwardAgent`，按 profile 开关，默认关）为延后子项；同一 profile 的**连接复用**（多标签共享一条 TCP 连接）为延后子项；密钥交换与主机密钥算法可配并允许显式启用旧算法（兼容老交换机/嵌入式设备，启用时给出安全提示）为延后子项。

**SPEC.FEAT.CONN.03 SSH 档案管理** 连接档案：分组/收藏/搜索、quick connect（临时 host:port 输入即连）、`~/.ssh/config` 只读导入（变更检测，单向）；跳板机（ProxyJump 语义）为延后子项。

**SPEC.FEAT.CONN.04 SFTP 浏览器** SSH 会话内双栏/侧栏文件浏览：目录树、上传/下载（进度与取消）、删除/重命名/新建、权限显示；不依赖服务器额外组件。

**SPEC.FEAT.CONN.05 串口终端** 串口枚举、波特率/数据位/停止位/校验/流控完整配置、断开重连、连接中热改波特率；默认编码 GB18030（见 SPEC.FEAT.TERM.09 / 裁决 7.6）；**发送行尾序列可配**（CR / LF / CRLF，默认 LF——发错设备无响应，属嵌入式调试刚需）；可选十六进制收发视图与行首时间戳前缀（延后子项）。

**SPEC.FEAT.CONN.06 Telnet** 基础 Telnet 客户端（不含 TN3270）；选项协商覆盖 ECHO / SGA / TTYPE；编码按 SPEC.FEAT.TERM.09 可配。

**SPEC.FEAT.CONN.07 连接管理器 UI** 侧边栏：档案树 + 新建向导（按连接类型分步表单）；最近连接列表；快捷片段（snippets，Termius/XShell 语义）：常用命令片段库（分组/搜索/参数占位），单击或快捷键发送至当前会话（延后子项）。

**SPEC.FEAT.CONN.08 SSH 隧道** 端口转发三式（Termius/XShell/Tabby 标配）：本地（`-L`）/ 远程（`-R`）/ 动态 SOCKS（`-D`）；隧道独立于终端会话管理（可开隧道不开终端）、列表启停、失败自动重试与状态提示。

**SPEC.FEAT.CONN.09 凭据安全存储** 密码与私钥 passphrase **不落 Preferences 明文 JSON**：经 OS 凭据库存储（Windows Credential Manager / Linux libsecret / macOS Keychain），Preferences 只存引用句柄；OS 凭据库不可用时降级为「每次询问」，绝不降级为明文。安全审计项：**审计范围为配置目录**——全配置目录 grep 无任何明文凭据，并做成自动化用例而非人工检查项。日志文件目录（SPEC.FEAT.CONN.11）不在本审计范围内，单独标注为「用户显式开启、风险自负」。

**SPEC.FEAT.CONN.10 密钥管理器** SSH 密钥管理（Tabby 语义）：生成密钥对（ed25519 优先/RSA 备选，可设 passphrase）、`~/.ssh` 密钥列表、公钥导出一键复制、公钥推送至主机（`authorized_keys` 追加，SFTP/执行通道实现）。

**SPEC.FEAT.CONN.11 会话日志** 按会话开关的输出落盘记录（XShell/SecureCRT 语义）：纯文本（剥离 SGR 属性但保留换行结构）或带属性的原生存式（自定格式）、可配时间戳前缀、环形覆盖策略；日志文件路径可在设置中配置。**默认关闭**——开启须用户逐会话显式动作，且首次落盘给出「日志可能包含密码 / 令牌」风险提示（裁决 7.9）；提供可选的**敏感输出过滤**（按可配正则匹配 password / token / secret 等行打码）。

**SPEC.FEAT.CONN.12 剪贴板访问授权** OSC 52 **读**方向默认**禁止**，可在设置中开启（三态：禁止 / 允许 / 每次询问）——远端程序可静默读取本机剪贴板，属真实隐私风险；写方向默认允许（见 SPEC.FEAT.TERM.07）。触发读请求被拒时须向会话回写空响应而非静默无响应（避免远端程序挂起）。

### 4.6 外观与配置（`SPEC.FEAT.PREF.01–07`）

**SPEC.FEAT.PREF.01 主题** 内置 ≥8 套常用配色（Dracula、Nord、Solarized、One Dark 等）+ 自定义（16 色重映射 + 前景/背景/选区/光标色）；UI 主题暗色优先、跟随 Aurora `Theme::dark()` / `ThemeScope` 体系。

**SPEC.FEAT.PREF.02 设置面板** 分类：外观（主题/字体/光标）、终端（scrollback/铃声/粘贴行为/编码）、连接（SSH/串口默认值）、快捷键；修改即时生效 + 预览。

**SPEC.FEAT.PREF.03 持久化** 全部设置与档案经 Aurora `Preferences` 落 JSON；首版 schema 带版本号字段，为后续迁移预留。写入原子化与损坏降级见 SPEC.FEAT.PREF.07。

**SPEC.FEAT.PREF.04 快捷键系统** 基于框架 `ShortcutRegistry`（应用内，非 OS 全局）；全部动作可重绑、冲突检测与提示、恢复默认；按连接类型区分终端透传键与 UI 键；绑定以 Command 为锚（与 SPEC.FEAT.WS.07 统一）。

**SPEC.FEAT.PREF.05 i18n** 中文/英文首版双语，经 Aurora `StringTable`。

**SPEC.FEAT.PREF.06 零配置可用** 首次启动即得可用本地终端，全部默认值合理，无需任何配置动作。

**SPEC.FEAT.PREF.07 配置韧性** 配置写入原子化（临时文件 + 重命名，避免写一半崩溃）；启动时 schema 校验失败（版本不兼容 / JSON 损坏）→ 备份损坏文件为 `*.corrupt-<时间戳>` 并**回落默认配置**启动，同时显著提示用户，**绝不静默清空**；保留最近 N 份配置快照可回滚；支持**本地导出/导入**（档案 + 设置，**凭据句柄不导出**）用于换机迁移。

### 4.7 Shell 集成与系统集成（`SPEC.FEAT.INTEG.01–04`）

> 以 OSC 序列为契约、shell 侧零侵入（仅需用户在 rc 文件加官方集成片段，或检测到集成脚本未装时给出指引）。

**SPEC.FEAT.INTEG.01 OSC 133 命令块** 解析 OSC 133（prompt/command start/command executed/output 边界标记，iTerm2/WezTerm FinalTerm 语义）；基于标记提供：「复制上一命令的全部输出」（不带提示符与命令行回显）、命令块悬停分隔视觉（可选开关）、失败命令（exit code ≠ 0）输出行高亮提示（配合 OSC 133;D exitcode）。

**SPEC.FEAT.INTEG.02 OSC 7 工作目录** 解析 OSC 7（`file://host/path`）上报的当前目录；「新标签/分屏继承当前会话目录」（本地终端语义，SSH 会话在远端目录语义下降级为不继承）。

**SPEC.FEAT.INTEG.03 系统通知** OSC 9 / OSC 777 通知转系统 toast（长命令结束提醒、远端程序消息）；依赖系统通知 API（框架缺口 G11）。

**SPEC.FEAT.INTEG.04 CLI 启动参数** 命令行参数控制启动形态（`wt` 风格）：`--profile <name>` 指定连接、`new-tab`/`split-pane` 子命令、`--cwd <dir>`；已有实例运行时按单实例协议转交参数（Tabby 语义），转交后默认在**当前窗口新标签**打开，`--new-window` 显式开新窗口（裁决 7.7）。

---

## 5 非功能需求

**SPEC.NF.PERF.01 输入延迟** 按键 → 字符上屏端到端（PTY 往返 + 渲染）≤ 50 ms（本机 shell、非负载场景，P95）。

**SPEC.NF.PERF.02 渲染吞吐** `cat` 10 MB 文本 ≥ 45 fps 滚动不掉帧（软件 Painter，默认窗口尺寸）；全屏重绘（如 `vim` 首帧）≤ 100 ms。基准场景纳入可重复测试（HeadlessSurface + 定时灌注回放），并进 CI 设回归门禁（劣化 >10% 即 FAIL）。

**SPEC.NF.PERF.03 启动时间** 冷启动 → 可交互终端 ≤ 1.5 s（SSD、无网络等待），含内置字体加载与首帧渲染。

**SPEC.NF.PERF.04 内存** 单会话（10k 行 scrollback）常驻 ≤ 150 MB；10 标签场景线性可控、无累积泄漏（1 小时压测）。

**SPEC.NF.PLAT.01 跨平台** Windows + Linux 全量需求等价实现；平台差异仅限 PTY/串口/传输层实现与默认 shell 探测。macOS 为远期目标平台（依赖 `MacOSSurface` 骨架）。**本条只述等价性要求**，各平台的交付先后属分期决策，见 PLAN.md。

**SPEC.NF.RELI.01 可观测性** 全链路日志分级（走 Aurora Logger）；VT 解析器未知序列计数统计（调试面板可查）；非法字节序列计数（SPEC.FEAT.TERM.09）与背压水位（SPEC.NF.PERF.06）一并进调试面板；崩溃时留存会话与诊断信息。

**SPEC.NF.A11Y.01 无障碍基线** 键盘全操作可达；随 Aurora 无障碍平台桥（Aurora 主仓 `codespec/ARCHITECTURE.md` §8.5）落地逐步接入。本项为延后观察项，分期见 PLAN.md。

**SPEC.NF.PKG.01 打包分发** Windows：便携 zip + 安装包（NSIS 或 MSIX 二选一，尚未裁决）双形态；Linux：tar 通用 + AppImage；产物含 LICENSE/第三方声明（Cascadia Code OFL、libssh2/BSD 等合规文件）；自动更新不做（裁剪表），但安装包须支持覆盖升级且保留用户配置。产品对外名称统一 **Borealis**（裁决 7.1）。

**SPEC.NF.PERF.06 输出背压** 后台读线程与 UI 之间设**有界队列**；队列满时按「合并而非丢弃」策略（累积的纯文本变更合并为一次网格更新提交），保证最终内容与 PTY 输出一致；单帧最多消费 N 次合并提交后主动让出，保证输入响应与 UI 不被饿死；丢弃仅发生在「同一单元格被多次覆盖」的情形（语义无损）。队列水位、合并次数、让出阈值进 SPEC.NF.RELI.01 可观测面板。验收：`yes` / `journalctl -f` 类无限高频输出下不卡死、内存不无界增长、SPEC.NF.PERF.01 输入延迟仍达标。

**SPEC.NF.PERF.05 空闲资源占用** 无输出且无输入时，5 分钟平均 CPU ≤ 1%（单会话）；失焦窗口降低渲染频率；Scrollback 无变化时不做脏行重绘。终端为常驻应用，本项为日常可用的体感底线。

---

## 6 需求分期与优先级

**本文不表达优先级、交付分期与任务划分。** 全部需求的优先级定义、阶段划分、需求到阶段的映射、出口判据、延后子项清单与观察池排期，统一见 [`PLAN.md`](PLAN.md)。阶段编号是 PLAN.md 的私有维度，本文任何条目均不得引用；需求条目里的「延后子项」「延后观察项」只是需求自身的边界标注，落期一律以 PLAN.md 为准。

---

## 7 已裁决项（7.1–7.4 于 2026-09-22、7.5–7.10 于 2026-09-23 落定；7.11–7.15 于 2026-09-29 追加，7.16–7.21 于 2026-09-30 追加，7.22–7.24 于 2026-10-01 追加，7.25–7.28 于 2026-10-02 追加，7.29 于 2026-10-02 追加）

| # | 议题 | 裁决 |
|:---|:---|:---|
| 7.1 | 产品名 | **Borealis**（北极光，呼应 Aurora 极光血统） |
| 7.2 | SSH 传输库 | **libssh2**（C、成熟、SFTP 内置）；接入后如遇阻塞可换 libssh，接口层预留抽象 |
| 7.3 | 默认等宽字体 | **内置 Cascadia Code**（SIL OFL 1.1，允许再分发） |
| 7.4 | scrollback 超长行策略 | **截断为默认**（省内存，对齐 Tabby）；`SPEC.FEAT.TERM.04` 中做成配置项（截断/换行） |
| 7.5 | 窗口尺寸变化时的已有行处理 | **不 reflow**（与 7.4 截断默认一致：历史输出保持原样、实现简单、内存可控）；仅新输出按新宽度排布。reflow 为延后观察项（PLAN.md） |
| 7.6 | 串口默认编码 | **GB18030**（国内嵌入式设备主流）；本地终端 / SSH / Telnet 默认 UTF-8，四类均可按 SPEC.FEAT.TERM.09 覆盖 |
| 7.7 | 单实例转交参数的落点 | **当前窗口新标签**；`--new-window` 显式开新窗口 |
| 7.8 | 进程模型 | **单进程 + 会话级后台线程**（与 Aurora 单线程 UI 模型一致）；不做会话崩溃隔离，靠 SPEC.NF.RELI.01 诊断留存兜底 |
| 7.9 | 会话日志默认态 | **默认关闭** + 开启时风险提示（对齐 SPEC.FEAT.CONN.09 安全口径）；可选敏感输出过滤 |
| 7.10 | 工作区下限 | **多标签（SPEC.FEAT.WS.01）+ 任意分屏（SPEC.FEAT.WS.02）为不可裁剪项**，二者均属需求本体，落期见 PLAN.md 的首个交付阶段。「任意」按 SPEC.FEAT.WS.02 的四义界定（任意方向 / 任意深度 / 任意比例 / 任意 pane 数）。理由：出口判据是「日常替换 Windows Terminal」，单标签页与锁定布局的分屏均不足以构成替换。代价：pane 树需自研（框架 `Splitter` 仅二元），工作量显著上升，且必须与 SPEC.FEAT.XFER.01 PTY 尺寸同步联调 |
| 7.11 | 跨平台等价与分期解耦 | **等价性要求（SPEC.NF.PLAT.01）不打折，交付分期不在本文表达**——「先 Windows 后 Linux」的落期与理由归 PLAN.md。本条在需求侧保留的硬约束是：**平台相关层（PTY、串口、传输、默认 shell 探测、DPI 缩放上报）自首个交付阶段起即以接口抽象隔离**，禁止 Windows 假设（ConPTY 句柄、Win32 类型、码页 API）渗入共享路径；否则后补的 Linux 等价会退化成重写。本条同时修订 7.6 与 SPEC.FEAT.RENDER.05 中任何以里程碑表述分期的旧措辞 |
| 7.12 | 应用侧三方依赖获取方式 | **经 `find_package` + vcpkg 获取**，不采用 Aurora 的「源码全量进 `third_party/`、断网可构建」口径（该口径是库交付约束，不约束消费者）。**链接形态约束**：Aurora 以静态库交付且不强制 CRT（顶层 `CMakeLists.txt` 仅在 ASan 前置块设 `CMAKE_MSVC_RUNTIME_LIBRARY`，其余走 CMake 默认 `/MD`/`/MDd`），故 vcpkg triplet 取 **`x64-windows-static-md`**（静态库 + 动态 CRT）以同时满足「不把 DLL 拖进 SPEC.NF.PKG.01 打包」与「CRT 与 Aurora 一致」；`x64-windows`（DLL 形态）与 `x64-windows-static`（`/MT`）各有冲突。**本条 triplet 组合尚未经真机链接验证**（属推断），留待接入 libssh2 时实测确认并回填。前置条件：本机尚无 vcpkg（`VCPKG_ROOT` 未设、PATH 无该命令），须在 SSH 族需求开工前安装 |
| 7.13 | 框架缺口的处理节奏 | 除 G1/G2 外，开发中再撞到的框架缺口按**类别分流**：① 渲染与事件链路上的（影响公共 API 形态，如 G1 网格原语、G2 多击语义）——撞到即先在 Aurora 侧补公共 API + 单测 + 文档回写，应用侧不等不绕；② 交互体验类的（选择 overlay、光标闪烁驱动、tooltip 等用现有公共 API 即可组合实现的）——先在应用侧实现，不进框架。本条是设计约束 3.3.1 的执行细则：「不在应用侧私改渲染路径」仍为硬禁，但**用公共 API 组合出的应用侧控件不属于私改**，无需强行 push 进框架 |
| 7.14 | 命名统一（修订 7.1 的括注） | 仓内**一切可自主命名的标识统一 `borealis`**：命名空间 `borealis`（按模块域分 `borealis::vt` / `borealis::term` / `borealis::session` 等）、CMake project 与 target `borealis`、可执行产物 `Borealis`、目录与文档自称均不再出现旧名。本条曾把「工作区目录名仍为旧名」列为唯一例外（理由：改目录名须由人在 IDE 会话外执行，会牵动工程路径、既有构建目录与 IDE 配置），并规定改名后须同步回填；该例外已于 2026-09-29 随人完成目录改名而失效，本文与 `AGENTS.md` 中的相应括注已按本条要求删除，例外条款不再适用 |
| 7.15 | East Asian Width 中 Ambiguous 类的默认宽度 | **默认按单宽（窄）处理**，并提供 **profile 级覆盖为双宽**。理由：Ambiguous 区间（箱线字符、`±`/`°`/`→`、全角标点等）在 UTF-8 环境下主流终端（Windows Terminal、xterm 默认、WezTerm 默认）按单宽呈现，本机 shell 与 SSH 是首要场景，取单宽可与既有终端的复制/换行/列对齐直觉一致；而 GB18030/GBK 串口与部分日文环境按双宽更正确，故覆盖项挂在 SPEC.FEAT.TERM.09 的会话配置旁边（profile 粒度，非全局设置），避免为少数场景把默认值改成对多数场景错误的一侧。**该默认值随框架宽度判定 API 一并表达**：判定接口须接受 Ambiguous 宽度模式作为入参（见附录 A.2 G1），而非在框架内硬编码单/双宽，否则应用侧只能绕开公共 API 自行查表 |
| 7.16 | 中文输入与显示链路 | **三处一并补全**（此前规格书只覆盖「显示宽度」，漏掉「输入」与「字形可得性」两个维度）：① 新增 `SPEC.FEAT.INTERACT.06` 输入法——preedit 就地渲染、候选窗按光标坐标定位、组合中间态不发往会话、commit 文本经会话编码写入；② `SPEC.FEAT.RENDER.02` 补 **CJK 缺字回退链**——内置 Cascadia Code 不含汉字字形，无回退则 `SPEC.FEAT.TERM.09` 的「GB18030 串口输出正确显示」验收必然呈现豆腐块；③ `SPEC.FEAT.TERM.09` 的编码口径由**单向解码**改为**双向**，补发送方向（文本 → 会话编码字节），§1.3 术语表同步。理由：首要用户为中文开发者、串口默认编码为 GB18030（裁决 7.6），中文输入与显示属日常刚需而非增量特性；框架侧能力均已具备（`TextCompositionEvent` 含 preedit 且三后端已实现、字体缺字链回退，见附录 A.1），故本条属**需求侧补全而非框架缺口**，不占用 G 编号。代价：①②须随首个交付阶段落地（在本地终端输入或显示中文即触发），不可延后；③的完整非 UTF-8 发送方向随 `SPEC.FEAT.TERM.09` 的既有落期实现 |
| 7.17 | 窗口**行数**变化时的历史处理 | **移动窗口边界、底部锚定**：最后一行仍是同一行——视口变高时从 scrollback 顶部收回历史填满，变矮时视口顶行自然溢出进历史，超出容量则从最旧端丢弃，历史不足以填满新视口时顶部补空白行。已有行不重排（沿用 7.5 的列宽口径，二者共同构成尺寸变化的完整行为）。**scrollback 容量是需求的配置值，不随视口变高而缩水**：`SPEC.FEAT.TERM.04` 的「可配置容量（默认 10,000 行）」若被 resize 蚕食，该配置就失去意义，故存储容量按「视口行数 + scrollback 容量」计，**视口变高必然扩容**。理由：候选「固定总容量、视口变大时历史上限相应缩小」可让尺寸变化完全不搬数据也不重分配，但把需求承诺的回滚行数变成随窗口高度浮动的值；候选「变矮时直接丢弃多余行」则与 `SPEC.FEAT.TERM.04` 的回滚定位相悖。代价：行数变化是重分配路径（代价按现存行数计），须与 `SPEC.FEAT.XFER.01` 的去抖合并下发配合，才能把开销压到每帧边界一次；行号与内容的对应关系整体改变，读取方须整屏重建（架构 §3.4） |
| 7.18 | 会话层接口形态与输出背压的提交粒度 | **①接口粒度取「基础接口 + 能力接口组合」**（架构 §7.2 的待定项就此拍板）：所有连接类型共同实现 `Connection`（`start` / `write` / `resize` / `close` / `alive`）与反向的 `ConnectionEvents`（`on_bytes` / `on_closed`），SSH 独有面（SFTP / 隧道 / 执行通道）到其落期以独立能力接口增补——本地终端不为空实现买单，公共接口也不因新增连接类型翻改。② **背压队列的条目是「视口行区间提交」，不是字节也不是 cell 值**：权威网格由读线程持有（架构 §3.4），主线程按提交去网格取最新值，故队列满时把区间**并宽**即可做到「合并而非丢弃」，条目数上限即内存上限；行脏标记**由消费侧清除**，写侧只登记，否则主线程取不到列级增量、退化成整行重绘（`SPEC.FEAT.RENDER.01` 的单帧只重绘变更单元格落空）。③ **设备查询应答只报能力档位 62**（VT220 + 高级视频选项），不报 132 列 / sixel / ReGIS / 打印机附加能力号；`CSI > c`（DA2）与 `CSI ? 6 n` 按「宁可不答也不答一份错格式」忽略。理由：`SPEC.FEAT.TERM.01` 只要求 `DA1`/`DSR`「正确响应」，而附加能力号一旦误报，`vim`/`tmux` 会走本仓未实现的分支并留下难排查的显示异常。代价：①②的锁序固定为「先取队列、再读网格」，任何持网格锁入队的写法都会构成反向锁序；③以 `vim`/`tmux` 真机走查为最终验收（须待 ConPTY 落地） |
| 7.19 | 本地终端连接在 Win32 侧的实现口径 | `SPEC.FEAT.CONN.01` 的 Windows 腿落地时撞到的四处 API 语义，均不在需求与架构文档里，漏掉表现为空白屏或挂死且难以归因，就此定死：① **宿主的标准输入/输出/错误句柄一律不交给子进程**（`STARTF_USESTDHANDLES` 置位并把三句柄设空）：不设这条时 `CreateProcessW` 会把**宿主**的标准句柄拷给子进程，宿主经脚本重定向 / 日志文件启动（CI、`>` 重定向、后台启动）时子进程输出直接落进宿主那个文件，伪终端管道一个字节都收不到，会话呈现为「进程活着但屏幕永远空白」；置空后子进程按自己的控制台（即伪终端）打开 `CONIN$`/`CONOUT$`，与无标准句柄的图形宿主同形态。② **交给伪终端的那两个管道端（输入读端、输出写端）须在子进程建好后关闭本进程这一份**：本进程继续持有输出管道的写端，conhost 退场时写端不归零、`ReadFile` 永不返回 `ERROR_BROKEN_PIPE`，读线程退不出来，关闭标签就卡在读线程回收上。③ **关闭标签即终结子进程**：`ClosePseudoConsole` 只断控制台连接、不保证带走子进程，故关停路径补 `TerminateProcess`（与 Windows Terminal 的关闭同口径）。④ **PTY 环境注入的次序为「继承值 → PTY 默认注入 → profile 覆盖」**，合成后的环境块按键排序（`CreateProcessW` 的硬要求），`TERM=xterm-256color` / `COLORTERM=truecolor` 两个默认值以共享常量给出，posix 侧须同口径。另记两条实测事实供排障：给了宽字符环境块必须带 `CREATE_UNICODE_ENVIRONMENT`，否则 `CreateProcessW` 对**任何**非空 `lpEnvironment` 返回 `ERROR_INVALID_PARAMETER`(87)（原样拷贝的 `GetEnvironmentStringsW()` 亦不例外）；`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 的 `lpValue` 传 **HPCON 本身**并配 `sizeof(HPCON)`，传 `&hPC` 会被判为非法伪终端句柄、子进程以 `0xC0000142` 初始化失败——官方文档未列该属性，形态以 Windows Terminal 生产代码与 `winconpty.h` 的「该结构是与 OS 共享的 ABI」注释为准。**⑤ 真机 e2e 的运行前提**：宿主进程须能访问窗口站与桌面；无控制台的受限上下文（沙箱、服务会话）里子进程的伪终端初始化会失败，`SPEC.FEAT.CONN.01` 的 e2e 用例只能在交互会话中运行，不得以该环境下的失败结论判定实现有误 |
| 7.20 | 码点占位格数的判定归属、零宽的处理与判定次序 | **① 判定表归框架，应用侧只转调**：Aurora 侧新增 `aurora/core/unicode_width.h` 的 `unicode_cell_width(char32_t, AmbiguousWidthMode)`，**一个原语同时给出三档结果 0 / 1 / 2**（0 = 不独立占格的零宽码点，判据为 General_Category Mn/Me/Cf；2 = East Asian Width 的 W/F），Ambiguous 口径以入参传入（裁决 7.15 的要求）。本仓 `term::UnicodeWidthPolicy` 只做口径转发、不持表；`include/borealis/**` 公共头不含 Aurora 头，故声明在接缝头、实现落在 `src/term/width.cpp`。**② 判定次序不可交换**：Ambiguous 区间**包含**组合区段（U+0300–U+036F、U+FE00–U+FE0F、U+E0100–U+E01EF）与 U+00AD，若先按 Ambiguous 口径判宽度，零宽字符就会占 1–2 格；故框架原语内部固定「先零宽、再宽度」，数据以 UCD 18.0.0 实测导出（零宽 379 段、Wide+Fullwidth 126 段、Ambiguous 179 段）。**③ 零宽码点不进 `Cell` 主结构**：网格行内挂「基础格列号 → 标记序列」侧表（架构 §4.1 的低频属性外置口径），写入基础格即清其标记、整行搬移随行走、截断与复位一并丢弃；单格上限 `Row::kMaxCombiningMarksPerCell = 8`，超限**丢弃不报错**——会话字节流是不可信输入，连续投喂零宽码点不得让一行无限膨胀。**④ 宽度按字符集映射前的码点判定**：`ESC ( 0` 把 0x5F–0x7E 重映射为 U+2500 一类框线，那些码点属 Ambiguous，按映射后判定会让 Ambiguous=Wide 的 profile 把边框画成半格错位。代价：UCD 数据升级只由 Aurora 侧承担（本仓不复制表），本仓的宽度用例因此以「口径」而非「具体码点归属」为断言主体 |
| 7.21 | OSC 消费的产物形态、超链接存储与提前落地范围 | `SPEC.FEAT.TERM.07` 的消费腿落地时定死五条：① **产物形态是「状态快照 + 一个取走型动作」，不是事件队列**：`term::OscState` 存最近一次的标题（`0/2`）、工作目录原文（`7`）、命令块边界与退出码（`133`）、剪贴板写请求计数与未消费命令号计数；超链接走「标识 → URI」的有界表，网格里只存数字标识。理由：状态机跑在会话读线程的网格锁内（架构 §3.4），锁内只能留存不能投递；OSC 的频率是「一次标题设置、一条目录上报」量级，主线程按帧取值即够；事件队列要为尚不存在的消费方造抽象。② **超链接不进 `Cell` 主结构**，走 `grid::Row` 的「列号 → 标识」侧表（与裁决 7.20③ 同口径），写入该列即清其标识、擦除与覆盖同样清（否则删掉的文字仍可点）；链接表容量 `term::kMaxHyperlinks = 1024`，满则淘汰最旧一条，标识**递增且永不复用**——会话字节流不可信，`OSC 8` 可无限供给 URI，不封顶就是让一条输出吃掉内存；淘汰的后果限定为「旧格解析不出目标、按不可点处理」，绝不指向另一条 URL。③ **`OSC 52` 写方向经主线程落地**：状态机锁内留存待写文本 → `Session::take_clipboard_write()` 取走（同段多次写**合并为最终值**）→ `session::ClipboardOutbox::drain()` 在主线程转调 Aurora `Clipboard::set_text`（`include/borealis/**` 不含框架头，故依赖只留在实现里）；落地失败只记诊断不打断帧循环。读方向 `52;c;?` 按 `SPEC.FEAT.CONN.12` 默认禁止，但**回写空响应而非静默无响应**，三态授权的本档随 CONN.12 落地。④ **提前落地范围**：`SPEC.FEAT.TERM.07` 在 PLAN.md 记 P1/M2，本次把其**状态机消费腿**提前到 M1 底座一并闭合；`OSC 7` / `OSC 133` 本属 `SPEC.FEAT.INTEG.01/02`（P2/M4），此处只作**来源预埋**（记录最近一次边界标记与目录原文），其命令块区间附着到网格行、「复制上一命令输出」、新标签/分屏继承目录等消费行为仍在原分期。标题→标签名→窗口标题栏的 UI 消费链路亦留原分期。⑤ **未消费的命令号须留痕**：`4`/`10`/`110` 一类调色板族与拆不出命令号的串计入 `unhandled_count`（架构 §5.5），不得整体吞掉不留痕——「标题没生效」这类排障无从下手。代价：快照只留最近一次，`133` 的完整命令块区间须由 INTEG.01 落地时补结构；剪贴板只取最终值，「多次写各自生效」的追加型用法不在本档覆盖内 |
| 7.22 | G1 绘制两腿的框架形态与验收判据 | 三腿中的绘制两腿进 Aurora 时定死四条：① **批量文本原语落在 run 层**——`Painter::draw_text_runs(std::span<const render::TextRun>)`，片段 = 同属性文本 + 区域 + `Font` + `Color`，**不暴露 `ShapedGlyph`、不含 cell / 终端语义**（网格模型、脏行 diff、颜色合成仍在本仓，框架只收合成后的最终值，与架构 §9.3 边界一致）；输出与逐片段 `draw_text` **逐位相同**（落笔算式一字未动，省的是每次调用的字体面解析、像素尺寸与行高度量），录制态逐片段各落一条 `DrawText` 命令，故 RHI 回放后端与 `CmdKind` 零改动。② **整格度量以函数形态进框架、排布留应用侧**——`render::FontEngine::monospace_cell(Font, scale) -> render::CellMetrics{cell_width_px, cell_height_px, ascent_px}`，格宽取参考字形集 `{'0', U+2500}` 在绘制同源像素尺寸下整像素 hinted advance 的最大值（含制表符是因为它在部分字体里比数字宽，只量数字会让边框压进相邻格），行高与基线与绘制侧首行 snap 同源；**不给 `TextLayoutOpts` 增设「整格模式」**：那会把终端排版语义塞进通用文本层，而列起点、跨格与换行策略本就属应用侧。③ **验收判据由时间门禁改为逐位一致**——实测一屏规模（24 行 × 12 同属性短片段 = 288 片段，9 组「逻辑尺寸 × scale」两轮）批量入口相对逐片段调用为中位约 −1%、区间 −8.5%…+8%，落在环境抖动内：该场景的成本主体是字形 blit 与图集查找，不是被省下的派生量，故无稳定阈值可锁红灯；基准两行只作观测项，守门由「整批与逐片段两画布全像素差分为 0」的像素级用例承担（Aurora 主仓 `tests/unit/utest_painter.cpp`）。此条是对附录 A.3 原承诺「网格吞吐基准挂性能回归门禁（劣化 >10% 即 FAIL）」的实测修正，A.2 G1 行与 `PLAN.md` M0 行同步改判。④ **本仓不因本批改动任何代码**——两腿是渲染层（`SPEC.FEAT.RENDER.01`）的开工前提，落地时直接消费；架构 §9.2 的「应用侧自绘 cell 网格」过渡备路径就此不再启用 |
| 7.23 | 上屏层的取用形态：视口自管、run 切分粒度与吞吐门禁 | 渲染层（`SPEC.FEAT.RENDER.01`）开工前定死四条并附三处实测口径：① **视口与 scrollback 自管**——以 `Widget` 内置的 `ScrollViewport` 内核承载「行偏移」，**不复用框架 `Scroll` 容器**（实测三处不合：其视口高度无公共 getter、无惯性滚动仅有 150ms `ScrollGlide` 吸附、overscan 整屏 blit 与「行级脏 + 后台权威网格」两条前提正交，且 §9.5 记载 `RelayoutBoundary` 坑）。自管过程沉淀出的通用件按裁决 7.13① 与附录 A.3 **反哺 Aurora 成为公共 API**，判据是「**去掉终端字样后仍成立**」，不满足则留在本仓；本仓只消费公共头。§9.5 与 §16 的 B 行就此拍板。② **run 切分粒度＝按样式全等合并到行**——一行内相邻格的前景/背景/字体三者全等才并入同一 `TextRun`；**色带矩形与文本片段共用同一批切分边界**（背景不同的格必须切开，否则色带与字形错位），区间表只维护一份；双宽延续格是空串，不产片段也不画豆腐块；跨格截断在应用侧完成（框架只收合成后的最终值，裁决 7.22②）。③ **渲染吞吐基准挂时间门禁**——`SPEC.NF.PERF.02` 原文要求维持：本仓 `tools/bench` 建 HeadlessSurface + 定时灌注回放场景，fps 与全屏重绘耗时进 CI，劣化 >10% 即 FAIL。本条与裁决 7.22③ 不冲突：那次改判的对象是「框架批量入口相对逐片段调用」的差值（收益淹没在字形 blit 抖动里），本条锁的是**上屏层自身的每帧成本**（切分、色合成、脏行过滤），主体不同。④ **第一棒交付范围＝`SPEC.FEAT.RENDER.01` + `SPEC.FEAT.RENDER.03` + `SPEC.FEAT.RENDER.04` 的主路径**；`RENDER.02` 的字体可配与 `RENDER.05` 的 DPI 变更重建随第二棒；文本选择、IME 与尺寸去抖属交互层，不在本棒。<br>**随附实测口径（2026-10-01，写入以免后续再撞）**：ⓐ `Widget::on_paint` 收到的是**全量 bounds**，框架局部帧只把裁剪盒压进 `Painter` 裁剪栈，故「单帧只重绘变更单元格」只能由绘制侧按 `Painter::clip_bounds()` 自行跳过窗外行；ⓑ combining 标记**拼在基础码点之后随同一 run 的文本送出**，由框架 shaping 负责叠字与零推进（`Painter` 无逐字形入口，实测原语清单确认），列位仍取自网格；ⓒ 跨线程唤醒走本仓持有的 `Window::surface().request_wake()`（头文件明写线程安全）+ `Application::set_on_frame` 内排空自有队列，**不依赖 `aurora::detail::post_to_main`**——`detail` 命名空间下的符号不属消费方的长期依赖面 |
| 7.24 | 框架缺口的 2026-10-01 复核结论与补全分工 | 对 Aurora 当日活动分支逐项实测，四条缺口就此定死处置：① **G2（`MouseEvent` 无 `click_count`）、G9（无 OS 级全局热键）、G11（无跨平台系统通知 API）经复核仍开放**。G9 的既有候选「应用侧直调 `RegisterHotKey` / X11 grab」**作废**，改为框架侧补 OS 级注册表并与 `ShortcutRegistry` 的应用内作用域并存——`ShortcutScope::Global` 实测语义是「应用内不限焦点」，不是系统级。② **新增 G13**：批量文本入口 `Painter::draw_text_runs` 不透出 `TextLayoutOpts` / `TextAAMode`。此前「框架无斜体能力」的推断经实测**修正**为「能力在引擎层已具备（`TextLayoutOpts.italic` 由 FreeType shear 实现，`FontEngine::draw_text_runs` 静态入口与逐片段 `Painter::draw_text` 的带 opts 重载都收 opts），缺的只是批量公共入口的一层透传」，该项的框架工作量随之从「新增斜体支持」缩小为「入口透传」。③ **四条缺口的补全由 Aurora 侧承担**（按裁决 7.13① 的「公共 API + 单测 + 文档回写」路径），本仓不私挂分叉、不自算多击、不直调平台的热键与通知 API。④ **本仓撞缺口时的取用规则**：现有公共 API 能表达的行为就当日用——`SPEC.FEAT.RENDER.03` 的斜体与字距走逐片段 `Painter::draw_text` 的带 opts 重载，绘制侧按「是否带排版选项」分流，G13 关闭后即撤销分流并全量并入批量路径；公共 API 表达不出的才列观察池并不开工（G2 之于 `SPEC.FEAT.INTERACT.02` 的双击/三击腿、G9 之于 `SPEC.FEAT.WS.12`、G11 之于 `SPEC.FEAT.INTEG.03`，三者的落期随框架侧进度移动，见 `PLAN.md`）。代价：分流是过渡件，G13 关闭时须以一次改动撤销并复跑像素回归；「桩」只打在入口的合并上，不改像素判据与需求验收线。**2026-10-02 更新**：本条派发给 Aurora 侧的 G2 / G9 / G11 / G13 四条均已以公共 API 落地，本仓的取用形态、分流撤销与像素判据口径见裁决 7.29（本条的候选与派发记录保留，不作事实陈述用） |
| 7.25 | 上屏层视觉稿与四屏界面草图的拍板结论（`U1~U5` / `N1~N8`） | 视觉评审于 2026-10-02 收口，`RENDER_UI.draft.md` §4 与 `UI_OVERVIEW.draft.md` §4 的待决项就此全部闭合，逐条落到判据：① **U1＝画**右侧 2 dp 回看位置指示条（`fill_rect` 一块，按 scrollback 占比定位），回看态不得无线索。② **U2＝四周 4 dp 内边距且入配置**（缺省 4，可改 0 回到贴边）：几何侧先从可视 dp 扣除内边距再除格宽，偏移取整 dp、格宽取整 px，网格对齐判据不破；原「首行贴顶」表述按本条改写。③ **U3＝`ui::PaletteSpec` 增 `cursor_color`**，缺省回落 `default_foreground`——`SPEC.FEAT.PREF.01` 把「光标色」明列为自定义项，故这不是增量特性而是需求本体缺字段。④ **U4＝双线/波浪本棒一并落地**：`CellPaint.underline` 由 `bool` 扩为档位枚举（none/single/double/wavy），状态机解析 `SGR 21 / 4:3 / 4:4`（**编码映射经裁决 7.28 实测修正为 `21`/`4:2` 双线、`4:3` 波浪**，档位枚举与笔形判据不变）；笔形以 `fill_rect` 逐格合成（属裁决 7.13① 允许的应用侧公共 API 组合，不是私改渲染路径），像素判据锁到具体锯齿与双线形状。⑤ **U5** 已按裁决 7.24④ 闭合。界面层：⑥ **N1 侧栏折叠缺省**（展开态入配置，与 `SPEC.FEAT.PREF.06` 零配置可用同口径：首屏全宽给终端）。⑦ **N2 窄标签态状态角标优先于连接类型图标**（断线/退出不可替代，图标在本地终端为主时冗余）。⑧ **N3 状态栏条目可配 + 提供 UI 入口**（条目开关入配置，入口随设置面板，见 7.26⑦）。⑨ **N4 pane 底部角标仅在分屏态显示**（单 pane 时与标签条/状态栏三处重复，分屏时是「哪个 pane 处于何种连接/回看态」的唯一线索）。⑩ **N5 设置页实时预览直接用真实绘制路径**（复用终端视口的同一套格参数，依赖 `SPEC.FEAT.RENDER.01`；静态假预览违背 `SPEC.FEAT.PREF.02` 的即时性口径）。⑪ **N6 UI chrome 不随终端主题切换**，但主题结构**预留可选的 chrome 覆盖字段**（缺省不覆盖，入口后置）。⑫ **N7 右键菜单以单层 + 分隔线分区为主，低频簇收进二级子菜单**（复制语义三项开关、连接类动作），使可视高度不超视区。⑬ **N8 命令 id 一律英文入框架 `CommandRegistry`，中/英词条由本仓 `StringTable` 的映射表维护**（`SPEC.FEAT.PREF.05`），新增命令须同步词条。<br>代价：`RENDER_UI.draft.md` 的 V 系列判据须随 U2/U4 改写；U4 使第一棒范围比裁决 7.23④ 更宽（多一份状态机解析 + 三档笔形），该扩张由本条承担并同步进 `PLAN.md` |
| 7.26 | 配置层（`borealis::config`）首版范围、存储形态与分期前置 | `SPEC.FEAT.PREF.03` 落地时定死七条：① **schema 按 `SPEC.FEAT.PREF.02` 的四分类建全量**（外观 / 终端 / 连接 / 快捷键），UI 侧尚未接线的键先有默认值、无消费方——这是 PREF.03「全部设置与档案」的原文要求，与本仓「不引入将来可能用到的结构」的通则冲突，故以人的裁决为准。② **内置 ≥8 套预置配色随首版落地，缺省 Dracula**（另含 Nord / Solarized Dark / One Dark / Gruvbox Dark / Monokai / Campbell / Tokyo Night），即 `SPEC.FEAT.PREF.01` 的「内置 ≥8 套」腿提前；理由：上屏层第一棒必须有具体色值，`PaletteSpec` 的 16 色全零初始化无法验收。选区色与 chrome 覆盖不在首版（U3 只加 `cursor_color`，N6 只预留字段）。③ **存储形态＝单文件 + 按域分组嵌套 + 顶层版本号**：`borealis.json` 落在 Aurora `Preferences::default_config_dir()`（实测优先级 XDG_CONFIG_HOME → LOCALAPPDATA → `HOME/.config` → 当前工作目录），各域经 `group()` 落为嵌套对象，顶层 `schema_version` 为整数。框架自身元数据在 `__aurora_preference_meta__` 下、对 `keys()`/`get()` 不可见，故应用侧校验只看用户数据树（附录 A.2 G7 的「应用侧自校验」按此执行）。④ **`SPEC.FEAT.PREF.07` 的损坏降级线提前到本档**：缺键 / 类型不符 / 域外 → 回落默认并记诊断；未知键只记录不报错；JSON 解析失败（经 `last_load_error()` 判定）或 `schema_version` 高于本仓支持 → **先备份 `*.corrupt-<epoch 秒>` 再回落默认**。备份必须先于任何 `flush`，否则「不静默清空」就只剩口号。「显著提示」以 `LoadOutcome` 公共出口交 UI 侧，对话框落点标注 `TODO(SPEC.FEAT.PREF.07)`。快照回滚与导出导入仍留原分期。架构 §11.3 原把备份与 PREF.03 同段叙述，本条明确该降级线归属 PREF.07、只是落期提前。⑤ **首次启动不写盘**：只在内存得到全量默认值，用户第一次变更才 `flush`（零配置可用不应产生文件，且配置目录的凭据审计面保持最小）。⑥ **凭据不入 schema**：连接域首版只放非敏感默认值（端口、认证方式、串口参数与发送行尾、会话编码）；密码与私钥 passphrase 到 `SPEC.FEAT.CONN.09` 落期只存 OS 凭据库句柄。⑦ **`SPEC.FEAT.PREF.02` 设置面板前置**到上屏层之后紧接着做，含 N3 的状态栏条目入口、N5 的真实预览与主题切换。<br>**版本迁移仍未决**：本条只预留 `schema_version` 字段并定义「高于本仓支持即降级 + 备份」，迁移链的形态仍挂架构 §16 F，不在本档闭合。代价：schema 全量意味着字段先于消费方存在，设置面板落地时须以 schema 为准反向核对，不得另起一套键名 |
| 7.27 | 配置层落地的四条实测约束：存储键名、映射落盘形态、可缺省色值、比较运算 | `borealis::config` 落地并配单测时撞到四处「裁决 7.26 未覆盖、但决定配置能否正确往返」的事实，就此定死：① **框架的点号路径模型**——`Preferences::reconcile()` 先把整棵树 `flatten()` 成「点号复合键 → 叶子值」的平面表，再按点号 `resolve_set()` 重建嵌套，**只有数组是叶子**。后果两条：**存储键名（对象键）一律不得含点号**，含点即被拆成一层层嵌套对象；**空对象拍平时不产生任何条目、重建后即消失**，故「可空的映射」不得落 `{}`（写了也读不回来，还会造成假的缺键留痕）。② **`shortcuts.overrides` 的落盘形态因此是数组** `[{command, combo}, ...]`，内存模型仍是「命令 id → 组合键」的映射；元素缺 `command`/`combo` 或类型不符只丢该元素并留痕其下标（`shortcuts.overrides[3]` 形态），不让整张表失效。③ **主题表是色值唯一来源，且「缺键」与「显式 null」是两种状态**：`palette.cursor` 缺键 → 回落**本文件点名主题**的光标色并留痕（否则 one-dark 这类光标色≠前景色的主题会被画成前景色，主题设置形同被忽略）；显式 null → 用户点名的「未配」，绘制侧按裁决 7.25③ 回落 `default_foreground`，不留痕。开关与阈值不属色值，回落一律随 `Settings{}` 默认值，主题表不留开关的第二真值源。④ **`ui::PaletteSpec` 必须自带默认比较**：C++20 不为类隐式声明 `==`，缺了它 `config::Settings` 的 defaulted `==` 会被**静默删除**，而「写盘再读回逐域等值」正是 `SPEC.FEAT.PREF.03` 的验收判据。附带两条工程口径：**往返等值 + `rejected_keys`/`unknown_keys` 双空**是读写键名漂移的有效守卫（本批当场抓出 ② 的形态问题）；**配置文件的用例一律落 `aurora::testing::isolation::temp_dir()`**，不得构造默认 `Store{}`，否则用例会去备份用户真实配置目录里的损坏文件。代价：① 是框架既有的持久化模型，本仓只能适配不能改（改其形态属 Aurora 公共 API，须按裁决 7.13① 走派发路径）；② 的数组比映射冗长，换来的是「命令 id 可含任意字符」与「空表留得下来」 |
| 7.28 | 下划线档位的 SGR 编码映射（对裁决 7.25④ 与视觉稿 V7 的实测修正） | 落地 `SPEC.FEAT.RENDER.03` 的下划线三档时实测 xterm / ECMA-48 与 vtdn 的 SGR 参数表，发现 7.25④ 与 `RENDER_UI.draft.md` V7、§1 写的编码对应**错位**，就此定死：① **映射**：`SGR 4`（平参数、无子参数）与 `4:1` ＝单线；`4:0` ＝**关**（不是单线）；`4:2` ＝**双线**（不是 curly）；`4:3` ＝**波浪/curly**（不是双线）；`SGR 24` 与 `SGR 0`／空参数 `CSI m` 清档位。② **`SGR 21` 取 ECMA-48 的「双线」**：xterm 把 21 实现成「关粗体」是它自己的历史分歧，关粗体的标准写法是 `22`（本仓 `22` 已按 ECMA-48 关 bold+dim，不变）。后果：对按 xterm 口径发 `1 … 21` 的旧程序，本仓会多画一条双线且粗体不消——本条有意取 ECMA-48 一侧，与主流现代终端一致。③ **枚举只四档** `{None, Single, Double, Curly}`：`4:4`（dotted）与 `4:5`（dashed）按**单线**呈现且**不进枚举**——`SPEC.FEAT.RENDER.03` 只覆盖单/双/波浪，加档位就是实现需求未覆盖的行为（AGENTS.md §4.1 第 1 条）。降级**不挂计数器**：留痕件在消费方（调试面板）落地之前就是死代码，与裁决 7.21⑤ 的 `unhandled_count` 不同，那一条有架构 §5.5 的排障定位在先。④ **笔形口径**：装饰线宽 1 物理像素 = `1 / scale` dp；单线落在基线下 1px；双线为基线 +1px、+3px 两条 1px 线（间距与线宽同为 1px）；波浪以 **4 物理像素**为一周期、逐像素列在「基线 +1px」上下各偏 1px 的三角折线，**周期相位锚在该行的绝对像素 x** 而非 run 左沿——锚在 run 左沿会让相邻 run 的波形在边界处错位，而 run 切分本就随样式而变（裁决 7.23②），错位即判失败。三档共用同一格内笔形，越出格边界即判失败。代价：草稿 V7 与 §1 的两处编码须照本条改写；`4:4`/`4:5` 是已知不完全实现，将来要精确呈现须先扩 `SPEC.FEAT.RENDER.03` 再抬枚举 |
| 7.29 | 框架四条缺口（G2 / G9 / G11 / G13）的回货形态与 G13 分流的撤销 | 2026-10-02 在 Aurora 当日活动分支（`dev-1.0.0-alpha.9.uat.2`，本地树 `4967a28c`）实测，裁决 7.24③ 派发给框架侧的四条缺口**全部以公共 API 落地**，本仓的取用与撤销就此定死：① **G13 关闭**：`Painter::draw_text_runs` 新增 `(runs, opts)` 与 `(runs, aa_mode, opts)` 两个重载，与 `draw_text` 的 opts / aa 梯度对齐；单参重载改为委托实现，历史调用点逐位不变。框架**刻意不提供 per-run opts**（头注释明写：以免 `TextRun` 承载排版字段后与 `Font` 的样式语义重叠成两条矛盾来源），故本仓按「是否带排版选项」把一行的片段分两批下调用（每行最多两次批量调用），`src/ui/terminal_view.cpp` 的逐片段 `Painter::draw_text` 分流自本日起撤销。② **G2 关闭**：`MouseEvent::click_count`（`std::uint8_t`，取值 1..3，Release / Move 恒为 1）由 `EventDispatcher::dispatch_mouse` 在派发前**集中**判定、后端不参与；阈值取库内常量 `kDefaultClickWindowMs = 500` / `kDefaultClickRadiusDp = 4.0`，**未接线系统双击速度**（跨平台手感一致优先），并以派发器公开成员 `click_window_ms` / `click_radius_dp` 供测试注入极端值；判据取窗口逻辑坐标 `position` 而非随命中链改写的 `local_position`，只有 Press 参与计数（按住不放不产生连击），鼠标与触控合成流共用同一 `ClickTracker`。`SPEC.FEAT.INTERACT.02` 的双击选词 / 三击选段腿就此解除框架阻塞。③ **G9 关闭**：`OsHotkeyRegistry` + 值语义 `OsHotkeyHandle`（`app/os_hotkey.h`），Win32 经 `RegisterHotKey` / `WM_HOTKEY`；无后端平台（Wayland / GLFW / macOS / WASM）`enabled()` 为 `false` 且 `add()` 恒返回 `ErrorCode::OsHotkeyRegisterFailed`——**失败一律机器可见，不做静默 no-op**。命中**不在消息泵内同步执行动作**：`WM_HOTKEY` 只把 ID 推进内部队列，由帧循环调 `drain_pending()` 排空（回调可能重建页面 / 触发重排，在消息泵内重入会把布局与绘制切到半途的状态）。与 `ShortcutRegistry` 的应用内作用域并存，分工已在头注释落定。④ **G11 关闭**：`NotificationCenter::notify(Notification)`（`app/notification.h`，win32 气球 / XDG 桌面通知）+ `set_on_notification_activated(tag)` 激活回传 + `pump_events()` 排空平台异步事件；单测经 `install_recording_backend()` 走「仅记录」后端，不触达系统通知服务。⑤ **随撤销的验证义务**（裁决 7.24 代价条款的兑现）：斜体须经像素用例守住且判据**以变异自证非空转**。实测排除了一种看似自然的写法——「同一格先画正体、再加 `SGR 3` 重写、比两帧」**不成立**，因为加斜体本身改变了 `layout_row` 的 run 切分（原本与空白并成一段的一格独立成段），把 opts 整个丢掉时两帧差分仍非零，判据空转；跨行对照同理失效（行间的亚像素相位本身就是差异源）。成立形态是「**同一行、同格底色、同一字形的两格对照**」：底色让两格各自成段，两格像素带按整格宽取带以保持相位一致，于是唯一变量就是有没有斜切。代价：含斜体的行付两次批量调用的固定开销（换回同 `Font` 共用字体面解析与行高度量）；G9 / G11 的 `SPEC.FEAT.WS.12` Quick Terminal 与 `SPEC.FEAT.INTEG.03` 通知转 toast 仍留在 `PLAN.md` 观察池——**回货只解除框架依赖，不改变落期与优先级** |

---

## 附录 A Aurora 框架现状与缺口分析（2026-09-20 实测，2026-09-22、2026-09-29、2026-09-30 与 2026-10-01 复核）

> 「隐式驱动」：本文不逐条挂钩需求与缺口，仅在此独立盘点。
> **路径一律相对 Aurora 主仓根目录，引用一律用符号名**——不使用 `file:line` 锚点（随上游提交必然漂移）。

### A.1 已具备（可直接/低成本复用）

| 能力 | 事实 |
|:---|:---|
| 剪贴板 | `Clipboard::set_text` / `get_text`（`app/clipboard.h`）→ SPEC.FEAT.INTERACT.03 SPEC.FEAT.CONN.12 |
| IME | `TextCompositionEvent` 完整含 preedit，**Win32 / X11 / Wayland 三后端均已实现** → SPEC.FEAT.INTERACT.06（CJK 输入在三大平台均已接线；preedit 绘制与光标坐标换算属应用侧） |
| 焦点作用域 | `FocusManager::push_scope` / `pop_scope`（`event/focus.h`）→ 弹窗/搜索框焦点管理 |
| 光标形状 | `CursorShape`（`core/enums.h`）+ `Surface::set_cursor`（`window/surface.h`）→ 文本/指针切换 |
| 多窗口 | `Application::open_window` + `WindowEventBus`（`app/application.h`、`app/window_bus.h`）→ SPEC.FEAT.WS.03 |
| 字形缓存 | `GlyphAtlas`（`render/glyph_atlas.h`）+ shape LRU（`render/font_engine.h`）→ SPEC.FEAT.RENDER.01 的底层基础 |
| **批量文本绘制**（G1 第一腿关闭，2026-10-01 实测；G13 于 2026-10-02 关闭） | `Painter::draw_text_runs(std::span<const render::TextRun>)`（`render/painter.h`）：片段 = 同属性文本 + 区域 + `Font` + `Color`，整批一次调用，相邻同 `Font` 共用字体面解析与行高度量；输出与逐片段 `draw_text` **逐位相同**，录制态逐片段落 `DrawText` 命令（回放后端零改动）→ SPEC.FEAT.RENDER.01 SPEC.FEAT.RENDER.03；形态与收益口径见裁决 7.22。**2026-10-02 追加**：同一入口另有 `(runs, opts)` 与 `(runs, aa_mode, opts)` 两个重载，与 `draw_text` 的 opts / aa 梯度对齐，录制态把 `aa_mode` 与 `opts` 逐片段写入 `DrawCmd`（回放端据此重建，故录制—回放与直绘三条路径同样逐位一致）；**opts 是整批共用、框架刻意不做 per-run opts**（免与 `Font` 样式语义重叠），本仓按「是否带排版选项」分两批下调用，见裁决 7.29①。此前登记为 G13 的「批量入口不收排版选项」限定就此作废 |
| **连击序号**（G2 关闭，2026-10-02 实测） | `MouseEvent::click_count`（`std::uint8_t`，1..3；Release / Move 恒为 1）由 `EventDispatcher::dispatch_mouse` 在派发前集中判定，`ClickTracker` 被鼠标与触控合成流共用，五后端不参与 → SPEC.FEAT.INTERACT.02 的双击选词 / 三击选段；阈值是库内常量 `kDefaultClickWindowMs = 500` / `kDefaultClickRadiusDp = 4.0`（未接系统双击速度），以派发器公开成员形态供测试注入极端值，形态与判据见裁决 7.29② |
| **OS 级全局热键**（G9 关闭，2026-10-02 实测） | `OsHotkeyRegistry` + 值语义 `OsHotkeyHandle`（`app/os_hotkey.h`）：Win32 经 `RegisterHotKey` / `WM_HOTKEY`，应用无焦点也触发；无后端平台 `enabled()` 为 `false` 且 `add()` 恒返回 `OsHotkeyRegisterFailed`（不静默 no-op）；命中只入内部队列，由帧循环 `drain_pending()` 排空后才执行动作 → SPEC.FEAT.WS.12；与 `ShortcutRegistry` 的应用内作用域并存，见裁决 7.29③ |
| **跨平台系统通知**（G11 关闭，2026-10-02 实测） | `NotificationCenter::notify(Notification)`（`app/notification.h`，win32 气球 / XDG 桌面通知）+ `set_on_notification_activated(tag)` 激活回传 + `pump_events()` 排空；`install_recording_backend()` 为「仅记录」测试后端，不触达系统通知服务 → SPEC.FEAT.INTEG.03，见裁决 7.29④ |
| **等宽整格度量**（G1 第二腿关闭，2026-10-01 实测） | `render::FontEngine::monospace_cell(const Font&, float scale) -> render::CellMetrics{cell_width_px, cell_height_px, ascent_px}`（`render/font_engine.h`）：物理像素整数值，格宽取参考字形集 `{'0', U+2500}` 的整像素 hinted advance 最大值，行高与基线与绘制侧首行 snap 同源 → SPEC.FEAT.RENDER.01 SPEC.FEAT.RENDER.05 的列位对齐；排布（列起点 / 跨格 / 换行）仍在本仓网格层，框架不持终端语义，见裁决 7.22 |
| 字体注册/回退 | `register_font_memory` / `register_font_file`（`render/font_discovery.h`）、CJK 缺字链回退 → SPEC.FEAT.RENDER.02 内置字体 |
| **音频播放**（G10 关闭） | `AudioBuffer` + `AudioContext` 音频图（`media/audio.h`）恒编译；真实设备后端 WASAPI（Windows 共享模式，`src/aurora/media/audio_wasapi.cpp`）与 ALSA、WebAudio 均已实现，须经 CMake 开关 `AURORA_ENABLE_AUDIO` 显式编入（默认 OFF = 静默模式，设备初始化失败亦降级为静默） → SPEC.FEAT.WS.04 可听铃声 |
| **DPI 缩放变更上报**（G12 关闭） | `Surface::set_scale_change_handler` 注册回调 + 后端经 `notify_scale_change` 上报（`window/surface.h`）；Win32 后端已在 `WM_DPICHANGED` 处理中接线上报，Headless 侧另有 `emit_scale_change` 测试钩子可供回放断言 → SPEC.FEAT.RENDER.05（Linux 三后端尚无上报调用，该腿随 Linux 等价补齐，分期见 PLAN.md） |
| **命令注册表** | `CommandRegistry`（顶层 `commands.h`）：`add` / `remove` / `find` / `search` / `invoke` / `bind_shortcuts` / `to_menu_items`，另有 `command_fuzzy_score` → SPEC.FEAT.PREF.04 SPEC.FEAT.WS.07 |
| **命令面板** | `CommandPalette`（`widget/command_palette.h`）：模态浮层 + 即时过滤 + 焦点作用域 + Enter/Esc/↑/↓ 全接管，数据源即 `CommandRegistry*` → SPEC.FEAT.WS.07 近乎零成本 |
| **可拖拽分割器** | `Splitter`（`widget/splitter.h`）：响应式 `ratio()`、`handle_size`、`on_ratio_change`、`min_first`/`min_second` 钳制 → SPEC.FEAT.WS.02；**二元**（first/second），嵌套与方向键焦点路由需应用侧实现 |
| **标签栏**（能力有限，2026-09-29 实测） | `TabBar`（`widget/tab_bar.h`）：事件仅 `on_change`（选中）/ `on_close`（关闭），属性为 `selected_index`、`tab_height`、四个**全局**配色（`active_color` / `bar_background` / `tab_background` / `text_color`）、`indicator_thickness`、字号与内边距 → SPEC.FEAT.WS.01 的切换与关闭；**无拖拽重排、无逐标签图标、无逐标签状态角标**（配色为全局，不能区分单个标签），故 SPEC.FEAT.WS.01 的重排与图标、SPEC.FEAT.WS.04 的状态角标须应用侧自研 |
| 滚动容器 | `Scroll` 惯性/位置恢复（`widget/scroll.h`）→ scrollback 视口参考 |
| 主题 | token 体系 + `Theme::light()` / `dark()`（`theming/theme.h`）+ `ThemeScope`（`theming/theme_scope.h`）→ SPEC.FEAT.PREF.01 |
| 持久化 | `Preferences` JSON（`preferences/preferences.h`）+ `Storage` 抽象（`storage/storage.h`）→ SPEC.FEAT.PREF.03 SPEC.FEAT.PREF.07。**2026-10-02 实测的模型限定**（写入以免应用侧按「JSON 就是树」的直觉踩坑）：`reconcile()` 走「拍平为点号复合键 → 按点号重建嵌套」的双向过程，**只有数组是叶子**，故对象键不得含点号、空对象落不了盘；原子写（临时文件 + `rename`）与跨进程 advisory 锁在 `flush()` 内，另有无条件写入的 `__aurora_preference_meta__`（对 `keys()`/`get()` 不可见）。详见裁决 7.27① |
| 定时/后台任务 | `Scheduler`（`app/scheduler.h`）+ `ThreadPool`（`core/thread_pool.h`）+ 协程 → PTY 读线程模型 |
| 菜单/快捷键 | `MenuItem` / `MenuBar` / `ContextMenu`（`app/menu.h`）+ `ShortcutRegistry` 作用域（`app/shortcuts.h`）→ SPEC.FEAT.PREF.04 |
| 布局 | Row/Column/Grid/Stack（`widget/containers.h` 等）+ `Splitter` → SPEC.FEAT.WS.02 |
| i18n | `StringTable` + 复数规则（`i18n/string_table.h`）→ SPEC.FEAT.PREF.05 |
| **性能观测** | `PerfOverlay` + 帧统计（`app/perf_overlay.h`，`Application::record(dt)` 每帧调用）→ SPEC.NF.PERF.02 基准与 SPEC.NF.RELI.01 调试面板可复用 |
| 多行文本编辑 | `RichTextEdit` StyledChar 模型（`widget/rich_text_edit.h`）→ 无直接用途但可参考 |
| 日志/诊断 | `AURORA_LOG_*` 通道体系 → SPEC.NF.RELI.01 |

### A.2 缺口（G = 需先补框架，标注阻塞的需求；已关闭项保留编号与行位，以免他处引用成为死链）

| # | 缺口（含事实依据） | 阻塞 | 优先级与去向 |
|:---|:---|:---|:---|
| G1 | ~~无等宽网格/逐字符绘制原语~~ **已关闭**（2026-10-01，三腿齐备，形态与判据见裁决 7.22）。原始事实：`Painter::draw_text` 三个重载均为字符串粒度（Rect + 字符串 + Font + Color），无批量 run 入口、无整像素单格度量；另经复核，`FontEngine` 自身记录了 FreeType hinting 下 dp 测量与物理光栅宽度的偏差（advance 取整到整像素、行尾累计），故终端整屏网格不可直接复用现有 dp 链路。**关闭后仍在本仓的一侧**：cell 网格模型、脏行 diff 与颜色合成从来就不属框架（裁决 7.22①②把它们定在应用侧），故本行不因关闭而失去对应用层工作量的提示 | SPEC.FEAT.RENDER.01 SPEC.FEAT.RENDER.03 SPEC.FEAT.RENDER.05 SPEC.FEAT.TERM.08 | **形态已裁决**：以 `Painter` 批量文本 run 原语 + 等宽整像素 cell 度量 + East Asian Width 宽度判定进框架（宽度判定入参须含 Ambiguous 宽度模式，见裁决 7.15，不得在框架内硬编码单/双宽）；网格模型、脏行 diff 策略与颜色合成（含 SGR 属性、bold-is-bright、最小对比度）留应用侧，原语只收合成后的最终值。**第三腿（宽度判定）2026-09-30 闭合**：Aurora 侧公共 API `aurora/core/unicode_width.h` 的 `unicode_cell_width(char32_t, AmbiguousWidthMode)` 一次给出 0 / 1 / 2 三档、Ambiguous 口径为入参（数据 UCD 18.0.0，形态与次序见裁决 7.20），带 `tests/unit/utest_unicode_width.cpp`。**第一、二腿 2026-10-01 闭合**：另两腿以 Aurora 公共 API `Painter::draw_text_runs`（批量文本 run，`render/painter.h`）与 `render::FontEngine::monospace_cell` → `render::CellMetrics`（等宽整像素度量，`render/font_engine.h`）交付，各带单测与契约文档。原「框架原语落地时同步建网格吞吐基准并**挂性能回归门禁**」一项经实测改判：基准两行已建（Aurora `tools/bench/bench_render.cpp` 的 `grid_text_per_span_calls` / `grid_text_batched_spans`），但时间阈值落在环境抖动内、无可锁判据，正确性回归改由「整批与逐片段全像素差分为 0」的像素级用例承担（裁决 7.22③） |
| G2 | ~~鼠标无多击语义~~ **已关闭**（2026-10-02 实测，`MouseEvent::click_count`，见 A.1 与裁决 7.29②）。原始事实：`MouseEvent` 无 `click_count`，双击/三击需应用自算（2026-10-01 复核时仍开放，Aurora 全公共头只有 `media/video_player.h` 私有的 `on_double_tap`） | SPEC.FEAT.INTERACT.02 | **形态即裁决的落地**：`click_count` 由框架在派发层统一自算（时间窗 + 位移容差），后端不参与、触控合成流同口径，headless 经同一 dispatcher 路径用于单测，未接各平台双时设置。本仓未自算多击、未私挂分叉，兑现裁决 7.24③；双击选词 / 三击选段的实装随 `SPEC.FEAT.INTERACT.02` 那一棒 |
| G3 | **无 VT/OSC 解析工具**：全库无 vt/ansi 相关代码 | SPEC.FEAT.TERM.01–08 | 属应用域，不进框架；要求纯逻辑模块 + 全量单测（HeadlessSurface 回放断言） |
| G4 | `LazyList` 仅固定行高模式（`item_extent`，可变行高列为后续增强） | — | scrollback 用 `Scroll` + 自管视口更合适，不阻塞 |
| G5 | 无 HTTP 客户端（`image_widget.h` 注释明言不内置） | — | 本产品不需要；OSC 8 图片类远期特性才受影响 |
| G6 | 触摸无后端采集（框架层齐备、win32/glfw/wayland 零实现） | — | 触摸选择/滚轮手势为延后观察项，不阻塞 |
| G7 | preferences 无 schema 校验 | SPEC.FEAT.PREF.03 SPEC.FEAT.PREF.07 | 应用侧自校验配置结构即可，不阻塞。**已按此执行**（2026-10-02）：`config::Store` 建全量 schema 的读侧校验，缺键 / 类型不符 / 域外一律回落默认并留痕，未知键只记录，结论以 `LoadReport` 交出（裁决 7.26④、7.27③） |
| G8 | macOS 后端仅骨架 | SPEC.NF.PLAT.01 | macOS 平台整体延后（PLAN.md 观察池） |
| G9 | ~~无 OS 级全局热键注册~~ **已关闭**（2026-10-02 实测，`OsHotkeyRegistry`，见 A.1 与裁决 7.29③）。原始事实：应用失焦后收不到按键；2026-10-01 复核时全仓无 `RegisterHotKey` / 全局热键形态，`app/shortcuts.h` 的 `ShortcutScope::Global` 实测语义是「应用内不限焦点」而非系统级 | SPEC.FEAT.WS.12 | quick terminal 的硬依赖已解除。本行原列的「应用侧直调 Win32 `RegisterHotKey` / X11 grab」候选作废（裁决 7.24①），框架侧以公共 API 补的注册表与 `ShortcutRegistry` 的应用内作用域并存；`add()` 的失败一律机器可见（`OsHotkeyRegisterFailed`），无后端平台 `enabled()` 为 `false`。落期仍随 `SPEC.FEAT.WS.12`（观察池，另需无边框贴边窗口形态） |
| G10 | ~~无音频播放 API~~ **已关闭**（2026-09-29 复核：`AudioContext`/`AudioBuffer` 图 API 恒编译，WASAPI 与 ALSA 设备后端已实现，见 A.1） | SPEC.FEAT.WS.04(beep) | 无需补框架。可听铃声从「能力缺口」退化为「排期取舍」（PLAN.md 观察池）；应用侧须显式开 `AURORA_ENABLE_AUDIO` |
| G11 | ~~无系统通知（toast）API~~ **已关闭**（2026-10-02 实测，`NotificationCenter`，见 A.1 与裁决 7.29④）。原始事实（2026-10-01 复核时仍开放）：`widget/toast.h` 的 `ToastHost` 是应用内控件，`app/system_tray.h` 的气泡只在 Win32 存在且其头注释明写「非 Win32 / Headless 下所有方法为 no-op」，无跨平台入口与激活回传 | SPEC.FEAT.INTEG.03 | 延后观察项的框架依赖已解除：跨平台入口（win32 气球 / XDG 桌面通知）、激活回传回调与「仅记录」测试后端均在公共 API。OSC 9 / 777 转系统通知的实装仍随 `SPEC.FEAT.INTEG.03` 的落期（`PLAN.md` M5） |
| G12 | ~~DPI 变更通知待评估~~ **已核实并关闭**（2026-09-29：`Surface::set_scale_change_handler` 为公共 API，Win32 后端已在 `WM_DPICHANGED` 接线上报，Headless 有 `emit_scale_change` 钩子，见 A.1） | SPEC.FEAT.RENDER.05 | Windows 侧无框架阻塞。**残留单腿**：X11 / Wayland / GLFW 未调用缩放上报，属 Linux 等价范畴（分期见 PLAN.md）；届时核实确为框架缺失再走 A.3 路径 |
| G13 | ~~批量文本入口不透出排版选项~~ **已关闭**（2026-10-02 实测，`Painter::draw_text_runs` 的 `(runs, opts)` / `(runs, aa_mode, opts)` 两个重载，见 A.1 与裁决 7.29①）。原始事实（2026-10-01 实测新增）：单参公共入口不收 `TextLayoutOpts` / `TextAAMode`，`render::TextRun` 无 opts 位，注释自陈「排版选项取默认值」。**缺的不是斜体能力**：`TextLayoutOpts` 已含 `letter_spacing` / `word_spacing` / `italic` / `direction`，`FontEngine::draw_text_runs` 静态入口与逐片段 `Painter::draw_text` 的带 opts 重载都收 opts——差的只是批量公共入口的一层透传 | SPEC.FEAT.RENDER.03 | 本仓的过渡形态（带排版选项的段走逐片段 `Painter::draw_text`，裁决 7.24④）已随本条关闭撤销：改按「是否带排版选项」分两批走批量入口，opts 整批共用。**撤销的验收**是一条以变异自证的像素用例（丢掉 opts 即失败），反面写法的失效原因见裁决 7.29⑤ |

### A.3 对框架的反哺承诺

G1（网格渲染）、G2（多击事件）落地后**必须以框架公共 API + 单测 + 文档回写**形式进入 Aurora 主仓（遵守其 AGENTS.md §5 硬规则），本仓库不长期私挂分叉；网格吞吐基准（SPEC.NF.PERF.02 场景）随框架原语一并贡献为 Aurora `tools/bench` 用例（G1 两腿已按此落地：`bench_render` 的 `grid_text_per_span_calls` / `grid_text_batched_spans`）。原承诺的「并挂性能回归门禁（劣化 >10% 即 FAIL）」经实测改判为**时间门禁不锁、正确性门禁锁**：批量入口的收益上限是「每次调用的派生量 × 片段数」，一屏规模实测落在环境抖动内，无稳定阈值可判红灯，故以「整批与逐片段两画布全像素差分为 0」的像素级回归承担守门职责（裁决 7.22③，G1 行同口径）。后续开发中撞到的框架缺口按裁决 7.13 的类别分流处置——凡属渲染与事件链路（类别 ①）者走本条同一路径。G12 已核实为「API 具备、Win32 已接线、Linux 三后端未接线」，故本承诺在 SPEC.FEAT.RENDER.05 上仅余 Linux 缩放上报一腿，随 Linux 等价补齐一并核实处理。**G2 / G9 / G11 / G13 四条（2026-10-01 同日复核仍开放者）的补全按裁决 7.24 由 Aurora 侧承担**，其落地判据与本条同口径：公共 API + 单测 + 该仓文档回写，本仓只消费公共头、不私挂分叉，也不在应用侧自算多击或直调平台的热键与通知 API。**四条已于 2026-10-02 全部按本条形态进入 Aurora 公共 API 并为本仓消费**（裁决 7.29），G13 的过渡分流亦于同日撤销；G9 / G11 虽已可用，但本仓的消费者分别还在 `SPEC.FEAT.WS.12` 与 `SPEC.FEAT.INTEG.03` 的落期上，回货只解除框架依赖、不改分期。

---

## 版本与变更历史

当前 **v0.25**。完整变更历史（含各版本当时的优先级与里程碑口径表述，作为历史记录不回填改写）与旧需求编号 → 新标识的映射表，见 [`CHANGELOG.md`](CHANGELOG.md)。
