# SPECIFICATIONS.md — Borealis 终端管理软件需求规格书

> 本文件是 **Borealis**（工作区目录 `aurora-view`，命名口径见裁决 7.14）的需求规格总纲，仿 Aurora `codespec/SPECIFICATIONS.md` 形态。
> 章节号统一纯数字点分层级（`1` / `1.1` / `1.1.1`）；需求标识见 §1.4，与章节号并存。
> **本文只述需求**：不表达优先级、不表达交付分期与任务划分——那部分属 [`PLAN.md`](PLAN.md)（里程碑、优先级映射、观察池排期）。需求条目里出现的「延后子项」「延后观察项」只是需求本身的边界标注，其落期由 PLAN.md 决定。
> 依据框架现状（Aurora alpha，2026-09-20 实测、2026-09-22 与 2026-09-29 复核，见附录 A）写成；技术选型与架构留给 `ARCHITECTURE.draft.md`。

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
| 字符编码 | 会话字节流到文本的解码规则（UTF-8 / GB18030 / Latin-1 等），与「宽字符」为两个独立维度 |
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
| 网格渲染 | 等宽网格、字形缓存、属性样式、光标形态、DPI 缩放适配 | `SPEC.FEAT.RENDER.01–05` |
| 终端交互 | 键盘映射、多击选择、复制粘贴、搜索、URL 检测 | `SPEC.FEAT.INTERACT.01–05` |
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
4. 日志走 Aurora `Logger`，禁止裸标准输出（Aurora 规则 §5.8）。

---

## 4 功能需求

### 4.1 终端仿真（`SPEC.FEAT.TERM.01–09`）

**SPEC.FEAT.TERM.01 VT 解析器** 实现 xterm/VT100/VT220 兼容转义序列解析：CSI/OSC/SGR/DEC 私有序列，含 bracketed paste（DECSET 2004，粘贴安全：交给 shell 判定而非应用改写）、focus reporting（1004，上报焦点到远端程序）、私有模式集（`?1` `?4` `?6` `?7` `?25` `?47` `?1049` `?2004` 等 DECSET/DECRST）全量登记；**字符集切换**（`ESC(0` / `ESC(B` DEC Special Graphics 线条字符集、`ESC%G` UTF-8 切换）；`DECAWM`（`?7` 自动换行开关）、`DECOM`（`?6` 原点模式）、`IRM`（`?4` 插入模式）须生效；`DA1` / `DSR`（设备属性与光标位置查询）须正确响应（`vim`/`tmux` 会查询）。解析器为纯逻辑模块，可脱离 UI 单元测试。覆盖度以「跑通 `vim`/`htop`/`tmux`/`top`/`less` + shell 提示符生态（starship/oh-my-posh）」为验收线，**且这些程序的边框线条字符不得显示为乱码字母**（即 DEC Special Graphics 必测）。

**SPEC.FEAT.TERM.02 颜色支持** 16 基本色（可由主题重映射）、256 色、24-bit 真彩色（`38;2;r;g;b`）。

**SPEC.FEAT.TERM.03 主/备屏幕缓冲** 支持 alternate screen 切换（`1049`/`47`/`1047`）；备屏内容不进 scrollback，退出备屏恢复主屏原状。

**SPEC.FEAT.TERM.04 Scrollback 回滚** 可配置容量（默认 10,000 行，上限 100,000）；超长行**默认截断**（裁决 7.4，省内存），策略可配（截断/换行）；窗口尺寸变化时的已有行处理见裁决 7.5（默认不 reflow）。

**SPEC.FEAT.TERM.05 滚动区域与光标控制** DECSTBM 滚动区域、光标定位/保存恢复/可见性、字符擦除/插入/删除、制表位。

**SPEC.FEAT.TERM.06 鼠标上报** X10 / 普通 / 按钮事件 / SGR 扩展模式；`vim`/`htop` 内滚轮与点击可用；备屏模式下滚轮转方向键（alternate scroll，DECSET 1007）供 less/less 类程序翻页；上报模式与本地选择交互自动切换。

**SPEC.FEAT.TERM.07 OSC 集成** OSC 52 剪贴板写（读方向见 SPEC.FEAT.CONN.12）、OSC 8 超链接、OSC 0/2 标题设置。**标题消费链路**：OSC 设置的标题覆盖标签名，用户手动重命名的优先级更高（可配）；活动标签标题同步至窗口标题栏（与 SPEC.FEAT.WS.06 配合）。

**SPEC.FEAT.TERM.08 宽字符** 按 Unicode East Asian Width 处理 CJK 双宽占位；Emoji 呈现不要求完美对齐（延后观察项）；combining character 基础处理。

**SPEC.FEAT.TERM.09 字符编码** 会话级编码可配：UTF-8 为本地终端与 SSH 默认；**串口默认 GB18030**（裁决 7.6），备选 GBK / Big5 / Latin-1 / CP437。解码失败按替换字符处理，且**不得中断解析、不得污染后续行**；非法字节序列计数进 SPEC.NF.RELI.01 可观测面板。验收：GB18030 串口输出正确显示，且混入非法字节后终端持续可用。

### 4.2 网格渲染与光标（`SPEC.FEAT.RENDER.01–05`）

**SPEC.FEAT.RENDER.01 等宽网格渲染** 终端视区为固定单元格网格；单帧只重绘变更行/单元格（脏行 diff）；字形经缓存复用，避免整屏重排。性能验收见 SPEC.NF.PERF.02。

**SPEC.FEAT.RENDER.02 字体** 等宽字体选择（系统等宽字体枚举 + **内置 Cascadia Code 为默认**，裁决 7.3）、字号调整（含 Ctrl+滚轮缩放）、行高/字距可调。字体连字（ligature）为延后子项。

**SPEC.FEAT.RENDER.03 属性渲染** 前景/背景色、粗体/暗淡/斜体/下划线（含双线/波浪线）/删除线/反色/不可见，按 SGR 状态渲染；「粗体渲染为亮色」（bold-is-bright）与「最小对比度强制」（避免深色主题下不可读）均为可配开关。

**SPEC.FEAT.RENDER.04 光标** 块/下划线/竖线三形态（随 DECSCUSR 切换）、可配置闪烁频率、失焦时降级为空心/静止。

**SPEC.FEAT.RENDER.05 缩放适配** 支持系统 DPI 变更与跨屏 DPI 差异（Windows per-monitor DPI、Linux fractional scaling）；逻辑 dp → 物理像素的 cell 尺寸换算须保证网格对齐（cell 边界吸附、字形缓存按 DPI 分档失效重建）；字体缩放（SPEC.FEAT.RENDER.02）叠加在系统 DPI 之上，二者正交。验收：150% / 175% / 200% 及跨屏拖动后无错位、无字形模糊。**框架现状（2026-09-29 复核）**：DPI 变更通知的公共 API 已具备——`Surface::set_scale_change_handler`（`include/aurora/window/surface.h`），Win32 后端已在 `WM_DPICHANGED` 处理中接线上报，Headless 侧另有 `emit_scale_change` 测试钩子，故 **Windows 无框架阻塞**（G12 关闭）。唯 Linux 三后端（X11 / Wayland / GLFW）尚无缩放变化上报调用，该腿属 Linux 等价范畴（分期见 PLAN.md），核实确为框架缺失则按 A.3 以公共 API 反哺框架。

### 4.3 终端交互（`SPEC.FEAT.INTERACT.01–05`）

**SPEC.FEAT.INTERACT.01 键盘映射** 完整转发 Ctrl/Alt/Shift/Meta 组合键、功能键、方向键至 PTY；`Ctrl+C`/`Ctrl+Z` 等控制字符直通；Ctrl+Alt 系与 UI 快捷键冲突时以配置裁决。`DECCKM`（`?1` 光标键应用模式）与 keypad 应用模式（`DECKPAM`/`DECKPNM`）须生效——应用模式下方向键发送 `SS3 A` 而非 `ESC[A`。kitty keyboard protocol / `modifyOtherKeys` 为延后观察项。

**SPEC.FEAT.INTERACT.02 文本选择** 单击拖拽流式选择、双击选词、三击选行、列模式（矩形块选择）；选区随 scrollback 滚动跟随；选词界定符（word delimiters）可配；智能选择（双击落在引号/括号内时扩展选至配对符，iTerm2 语义）为延后子项；quick select 模式（快捷键后视区内 URL/路径/哈希自动标注字母标签，按标签即复制，WezTerm 语义）为延后子项。

**SPEC.FEAT.INTERACT.03 复制粘贴** 选区复制 / 粘贴（含多行粘贴警告与逐行发送节流）；粘贴换行处理策略可配（过滤/转换/原样）；bracketed paste 激活时（SPEC.FEAT.TERM.01）原样透传不节流；copy-on-select（选中即复制）与右键行为（复制/粘贴/菜单，Windows Terminal 三态）可配；从 scrollback 复制时剥离输出中的鼠标上报残留由 SPEC.FEAT.TERM.06 的模式切换保证。复制语义三项可配开关（**均默认关闭，保留原样为默认**）：剥离行尾空白、跨行反斜杠续行智能合并、去除 tmux 分屏边框字符。

**SPEC.FEAT.INTERACT.04 终端内搜索** Ctrl+F 浮层：大小写开关、正则开关、全部匹配高亮、Enter/N+Enter 前后跳转、匹配计数。性能：100,000 行 scrollback（SPEC.FEAT.TERM.04 上限）下首次搜索响应 ≤ 200 ms（P95）。

**SPEC.FEAT.INTERACT.05 URL 检测** 视区内 URL 识别（含 OSC 8），Ctrl/Cmd+点击或右键「打开链接」。打开前确认可配（默认「首次确认并记住同域」）；协议白名单（默认仅 http/https）；OSC 8 显式超链接与纯文本启发式识别的信任级别可分别配置。

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

**SPEC.NF.A11Y.01 无障碍基线** 键盘全操作可达；随 Aurora 无障碍平台桥（`ACCESSIBILITY_DESIGN`）落地逐步接入。本项为延后观察项，分期见 PLAN.md。

**SPEC.NF.PKG.01 打包分发** Windows：便携 zip + 安装包（NSIS 或 MSIX 二选一，尚未裁决）双形态；Linux：tar 通用 + AppImage；产物含 LICENSE/第三方声明（Cascadia Code OFL、libssh2/BSD 等合规文件）；自动更新不做（裁剪表），但安装包须支持覆盖升级且保留用户配置。产品对外名称统一 **Borealis**（裁决 7.1）。

**SPEC.NF.PERF.06 输出背压** 后台读线程与 UI 之间设**有界队列**；队列满时按「合并而非丢弃」策略（累积的纯文本变更合并为一次网格更新提交），保证最终内容与 PTY 输出一致；单帧最多消费 N 次合并提交后主动让出，保证输入响应与 UI 不被饿死；丢弃仅发生在「同一单元格被多次覆盖」的情形（语义无损）。队列水位、合并次数、让出阈值进 SPEC.NF.RELI.01 可观测面板。验收：`yes` / `journalctl -f` 类无限高频输出下不卡死、内存不无界增长、SPEC.NF.PERF.01 输入延迟仍达标。

**SPEC.NF.PERF.05 空闲资源占用** 无输出且无输入时，5 分钟平均 CPU ≤ 1%（单会话）；失焦窗口降低渲染频率；Scrollback 无变化时不做脏行重绘。终端为常驻应用，本项为日常可用的体感底线。

---

## 6 需求分期与优先级

**本文不表达优先级、交付分期与任务划分。** 全部需求的优先级定义、阶段划分、需求到阶段的映射、出口判据、延后子项清单与观察池排期，统一见 [`PLAN.md`](PLAN.md)。阶段编号是 PLAN.md 的私有维度，本文任何条目均不得引用；需求条目里的「延后子项」「延后观察项」只是需求自身的边界标注，落期一律以 PLAN.md 为准。

---

## 7 已裁决项（2026-09-22；7.11–7.14 于 2026-09-29 追加）

| # | 议题 | 裁决 |
|:---|:---|:---|
| 7.1 | 产品名 | **Borealis**（北极光，呼应 Aurora 极光血统；仓库目录名 `aurora-view` 不变——该括注由裁决 7.14 修订为「目录名为例外」） |
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
| 7.14 | 命名统一（修订 7.1 的括注） | 仓内**一切可自主命名的标识统一 `borealis`**：命名空间 `borealis`（按模块域分 `borealis::vt` / `borealis::term` / `borealis::session` 等）、CMake project 与 target `borealis`、可执行产物 `Borealis`、子目录与文档自称均不再出现 `aurora-view`。**唯一例外是当前工作区目录名仍为 `aurora-view`**——改目录名须由人在 IDE 会话外执行（牵动工程路径、既有构建目录与 IDE 配置），故本文与代码中凡指涉该目录处（含本文开头「仓库 `aurora-view`」）在改名前保留原样，改名后须同步回填 |

---

## 附录 A Aurora 框架现状与缺口分析（2026-09-20 实测，2026-09-22 与 2026-09-29 复核）

> 「隐式驱动」：本文不逐条挂钩需求与缺口，仅在此独立盘点。
> **路径一律相对 Aurora 主仓根目录，引用一律用符号名**——不使用 `file:line` 锚点（随上游提交必然漂移）。

### A.1 已具备（可直接/低成本复用）

| 能力 | 事实 |
|:---|:---|
| 剪贴板 | `Clipboard::set_text` / `get_text`（`app/clipboard.h`）→ SPEC.FEAT.INTERACT.03 SPEC.FEAT.CONN.12 |
| IME | `TextCompositionEvent` 完整含 preedit，**Win32 / X11 / Wayland 三后端均已实现** → CJK 输入在三大平台均已接线 |
| 焦点作用域 | `FocusManager::push_scope` / `pop_scope`（`event/focus.h`）→ 弹窗/搜索框焦点管理 |
| 光标形状 | `CursorShape`（`core/enums.h`）+ `Surface::set_cursor`（`window/surface.h`）→ 文本/指针切换 |
| 多窗口 | `Application::open_window` + `WindowEventBus`（`app/application.h`、`app/window_bus.h`）→ SPEC.FEAT.WS.03 |
| 字形缓存 | `GlyphAtlas`（`render/glyph_atlas.h`）+ shape LRU（`render/font_engine.h`）→ SPEC.FEAT.RENDER.01 的底层基础 |
| 字体注册/回退 | `register_font_memory` / `register_font_file`（`render/font_discovery.h`）、CJK 缺字链回退 → SPEC.FEAT.RENDER.02 内置字体 |
| **音频播放**（G10 关闭） | `AudioBuffer` + `AudioContext` 音频图（`media/audio.h`）恒编译；真实设备后端 WASAPI（Windows 共享模式，`src/aurora/media/audio_wasapi.cpp`）与 ALSA、WebAudio 均已实现，须经 CMake 开关 `AURORA_ENABLE_AUDIO` 显式编入（默认 OFF = 静默模式，设备初始化失败亦降级为静默） → SPEC.FEAT.WS.04 可听铃声 |
| **DPI 缩放变更上报**（G12 关闭） | `Surface::set_scale_change_handler` 注册回调 + 后端经 `notify_scale_change` 上报（`window/surface.h`）；Win32 后端已在 `WM_DPICHANGED` 处理中接线上报，Headless 侧另有 `emit_scale_change` 测试钩子可供回放断言 → SPEC.FEAT.RENDER.05（Linux 三后端尚无上报调用，该腿随 Linux 等价补齐，分期见 PLAN.md） |
| **命令注册表** | `CommandRegistry`（顶层 `commands.h`）：`add` / `remove` / `find` / `search` / `invoke` / `bind_shortcuts` / `to_menu_items`，另有 `command_fuzzy_score` → SPEC.FEAT.PREF.04 SPEC.FEAT.WS.07 |
| **命令面板** | `CommandPalette`（`widget/command_palette.h`）：模态浮层 + 即时过滤 + 焦点作用域 + Enter/Esc/↑/↓ 全接管，数据源即 `CommandRegistry*` → SPEC.FEAT.WS.07 近乎零成本 |
| **可拖拽分割器** | `Splitter`（`widget/splitter.h`）：响应式 `ratio()`、`handle_size`、`on_ratio_change`、`min_first`/`min_second` 钳制 → SPEC.FEAT.WS.02；**二元**（first/second），嵌套与方向键焦点路由需应用侧实现 |
| 滚动容器 | `Scroll` 惯性/位置恢复（`widget/scroll.h`）→ scrollback 视口参考 |
| 主题 | token 体系 + `Theme::light()` / `dark()`（`theming/theme.h`）+ `ThemeScope`（`theming/theme_scope.h`）→ SPEC.FEAT.PREF.01 |
| 持久化 | `Preferences` JSON（`preferences/preferences.h`）+ `Storage` 抽象（`storage/storage.h`）→ SPEC.FEAT.PREF.03 SPEC.FEAT.PREF.07 |
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
| G1 | **无等宽网格/逐字符绘制原语**：`Painter::draw_text` 三个重载均为字符串粒度（Rect + 字符串 + Font + Color），无 cell 网格、无行 diff 绘制路径；另经复核，`FontEngine` 自身记录了 FreeType hinting 下 dp 测量与物理光栅宽度的偏差（advance 取整到整像素、行尾累计），故终端整屏网格不可直接复用现有 dp 链路 | SPEC.FEAT.RENDER.01 SPEC.FEAT.RENDER.03 SPEC.FEAT.RENDER.05 | **形态已裁决**：以 `Painter` 批量文本 run 原语 + 等宽整像素 cell 度量 + East Asian Width 宽度判定进框架；网格模型、脏行 diff 策略与颜色合成（含 SGR 属性、bold-is-bright、最小对比度）留应用侧，原语只收合成后的最终值。框架原语落地时同步建网格吞吐基准并挂性能回归门禁（落期见 PLAN.md） |
| G2 | **鼠标无多击语义**：`MouseEvent` 无 `click_count`，双击/三击需应用自算 | SPEC.FEAT.INTERACT.02 | **形态已裁决**：`click_count` 由框架统一自算（按下时间窗 + 位置容差），五后端行为一致且可用 HeadlessSurface 纯逻辑单测覆盖，不依赖各平台双时设置 |
| G3 | **无 VT/OSC 解析工具**：全库无 vt/ansi 相关代码 | SPEC.FEAT.TERM.01–08 | 属应用域，不进框架；要求纯逻辑模块 + 全量单测（HeadlessSurface 回放断言） |
| G4 | `LazyList` 仅固定行高模式（`item_extent`，可变行高列为后续增强） | — | scrollback 用 `Scroll` + 自管视口更合适，不阻塞 |
| G5 | 无 HTTP 客户端（`image_widget.h` 注释明言不内置） | — | 本产品不需要；OSC 8 图片类远期特性才受影响 |
| G6 | 触摸无后端采集（框架层齐备、win32/glfw/wayland 零实现） | — | 触摸选择/滚轮手势为延后观察项，不阻塞 |
| G7 | preferences 无 schema 校验 | SPEC.FEAT.PREF.03 SPEC.FEAT.PREF.07 | 应用侧自校验配置结构即可，不阻塞 |
| G8 | macOS 后端仅骨架 | SPEC.NF.PLAT.01 | macOS 平台整体延后（PLAN.md 观察池） |
| G9 | **无 OS 级全局热键注册**（应用失焦后收不到按键） | SPEC.FEAT.WS.12 | quick terminal 硬依赖；候选进框架（`ShortcutRegistry` 扩 OS 域）或应用侧直调 Win32 `RegisterHotKey` / X11 grab |
| G10 | ~~无音频播放 API~~ **已关闭**（2026-09-29 复核：`AudioContext`/`AudioBuffer` 图 API 恒编译，WASAPI 与 ALSA 设备后端已实现，见 A.1） | SPEC.FEAT.WS.04(beep) | 无需补框架。可听铃声从「能力缺口」退化为「排期取舍」（PLAN.md 观察池）；应用侧须显式开 `AURORA_ENABLE_AUDIO` |
| G11 | **无系统通知（toast）API** | SPEC.FEAT.INTEG.03 | 延后观察项；同上候选 |
| G12 | ~~DPI 变更通知待评估~~ **已核实并关闭**（2026-09-29：`Surface::set_scale_change_handler` 为公共 API，Win32 后端已在 `WM_DPICHANGED` 接线上报，Headless 有 `emit_scale_change` 钩子，见 A.1） | SPEC.FEAT.RENDER.05 | Windows 侧无框架阻塞。**残留单腿**：X11 / Wayland / GLFW 未调用缩放上报，属 Linux 等价范畴（分期见 PLAN.md）；届时核实确为框架缺失再走 A.3 路径 |

### A.3 对框架的反哺承诺

G1（网格渲染）、G2（多击事件）落地后**必须以框架公共 API + 单测 + 文档回写**形式进入 Aurora 主仓（遵守其 AGENTS.md §5 硬规则），本仓库不长期私挂分叉；网格吞吐基准（SPEC.NF.PERF.02 场景）随框架原语一并贡献为 Aurora `tools/bench` 用例并挂性能回归门禁（劣化 >10% 即 FAIL），不再是「候选用例」。后续开发中撞到的框架缺口按裁决 7.13 的类别分流处置——凡属渲染与事件链路（类别 ①）者走本条同一路径。G12 已核实为「API 具备、Win32 已接线、Linux 三后端未接线」，故本承诺在 SPEC.FEAT.RENDER.05 上仅余 Linux 缩放上报一腿，随 Linux 等价补齐一并核实处理。

---

## 版本与变更历史

当前 **v0.7**。完整变更历史（含各版本当时的优先级与里程碑口径表述，作为历史记录不回填改写）与旧需求编号 → 新标识的映射表，见 [`CHANGELOG.md`](CHANGELOG.md)。
