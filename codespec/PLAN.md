# PLAN.md — Borealis 优先级与交付计划

> 本文件是 Borealis **唯一的**优先级与交付分期来源。[`SPECIFICATIONS.md`](SPECIFICATIONS.md) 只述需求本身（做什么、做到什么程度），不含任何优先级、阶段或任务划分；本文件只述何时做与先做谁，不重述需求内容。
> 需求标识（`SPEC.<类别>.<域>.<NN>`）的规范见 SPECIFICATIONS.md §1.4。**阶段编号（`M0`–`M5`）与优先级标记（`P0`–`P3`）是本文件的私有维度**：调整分期、重排阶段、改写优先级都不得触动需求标识。
> 依据：SPECIFICATIONS.md 裁决 7.10（工作区下限）、7.11（跨平台等价与分期解耦）、7.13（框架缺口按类别分流）。

---

## 1 优先级定义

| 标记 | 含义 | 判据 |
|:---|:---|:---|
| **P0** | 首个可自用交付必须完成；缺则产品无法替换既有终端 | 是否阻断「日常自己用它当唯一终端」 |
| **P1** | 前两个交付阶段内完成 | 属日常可用性与体感底线，但短时可绕过 |
| **P2** | 第三、四阶段完成；同主题内聚者允许提前随前置阶段一并交付（如 SSH 族的 SFTP/隧道/密钥管理随 SSH 阶段） | 功能完整度与场景覆盖 |
| **P3** | 观察池，按 §5 择期 | 依赖框架缺口关闭，或使用频率低 |
| 延后子项 | 某条需求内部可裁剪的子能力，父需求本身仍按期交付 | 集中在 §4 清册，不散落到需求正文 |

**规则**：新增需求必须同时在本文件赋优先级与阶段，否则视为分期缺失；需求撤销时其标识作废但不得复用（SPECIFICATIONS.md §1.4 第 1 条），本文件的对应行删除并注明作废日期。

---

## 2 阶段划分与出口判据

| 阶段 | 主题 | 涉及需求 | 出口判据 |
|:---|:---|:---|:---|
| **M0 框架补强** | Aurora 侧前置：框架缺口 G1（**已闭合，2026-10-01**）、G2 / G9 / G11 / G13（**已闭合，2026-10-02**，见 SPECIFICATIONS.md 附录 A.2） | — | 网格/批量文本绘制原语、等宽整像素 cell 度量、East Asian Width 宽度判定、多击语义均以公共 API 进框架并带单测；网格吞吐基准入框架 `tools/bench`（G1 已按此交付两行观测项）。原「并挂性能回归门禁（劣化 >10% 即 FAIL）」经实测改判：批量绘制的时间收益落在环境抖动内、无可锁阈值，守门改由像素级「整批 vs 逐片段全像素差分为 0」的用例承担（裁决 7.22③）；余下未落地项为 **G2 与 G13**（批量入口的排版选项透传），连同观察池里的 G9 / G11，四条的补全分工与实测复核结论见裁决 7.24。**2026-10-02 更新**：该四条（G2 / G9 / G11 / G13）已全部以 Aurora 公共 API 闭合并被本仓消费，G13 的逐片段分流已于同日撤销——**M0 就此整体收口**，取用与撤销口径见裁决 7.29。 |
| **M1 单终端 MVP（Windows）** | 多标签 + 任意分屏的本地终端 | `SPEC.FEAT.TERM.01–05`、`SPEC.FEAT.TERM.08`、`SPEC.FEAT.RENDER.01–04`、`SPEC.FEAT.INTERACT.01`、`SPEC.FEAT.INTERACT.02`（流式拖拽/矩形块基本面）、`SPEC.FEAT.INTERACT.03`、`SPEC.FEAT.INTERACT.06`、`SPEC.FEAT.WS.01–02`、`SPEC.FEAT.XFER.01`、`SPEC.FEAT.CONN.01`、`SPEC.FEAT.PREF.03`、`SPEC.FEAT.PREF.06`、`SPEC.FEAT.PREF.01` 的预置配色腿、`SPEC.FEAT.PREF.07` 的损坏降级腿（后两条的提前由裁决 7.26②④ 定）、`SPEC.NF.PERF.01`、`SPEC.NF.PERF.02`、`SPEC.NF.PERF.06`、`SPEC.NF.PLAT.01`（仅 Windows 侧） | 本人日常替换 Windows Terminal 本地会话；多标签与任意方向/深度/比例/pane 数的分屏可用；**中文可输入可显示**（输入法与 CJK 字形回退）；窗口连续 resize 后 TUI 不错位；高频输出不卡死、内存不无界增长；VT 序列回放夹具进 CI 且真机手工签收通过 |
| **M2 工作区打磨 + Linux 等价** | 搜索/选择/设置/面板/分发，并补齐 Linux | `SPEC.FEAT.TERM.06–07`、`SPEC.FEAT.INTERACT.02`（双击/三击等其余）、`SPEC.FEAT.INTERACT.04`、`SPEC.FEAT.RENDER.05`、`SPEC.FEAT.WS.04–07`（`SPEC.FEAT.WS.05` 的 SSH 重连腿随 M3）、`SPEC.FEAT.WS.10`、`SPEC.FEAT.PREF.01`（色值表已随配置层提前，本阶段做主题切换入口与亮/暗 chrome 适配）、`SPEC.FEAT.PREF.04`、`SPEC.FEAT.PREF.07` 的快照回滚与本地导出导入（损坏降级腿已随配置层提前）、`SPEC.NF.PERF.03–05`、`SPEC.NF.RELI.01`、`SPEC.NF.PKG.01`、`SPEC.NF.PLAT.01`（Linux 等价补齐） | 搜索 + 文本选择 + 主题 + 命令面板全流程；性能/内存/空闲 CPU 指标达标；配置损坏可降级启动；产出可分发安装包；Linux 侧同批功能与指标达标（含缩放上报腿与 PTY/默认 shell 探测） |
| **M3 远程连接** | SSH + 档案 + SFTP + 凭据安全 | `SPEC.FEAT.CONN.02–04`、`SPEC.FEAT.CONN.07–10`、`SPEC.FEAT.TERM.09`、`SPEC.FEAT.WS.05`（SSH 断线重连腿） | SSH 日常运维全流程（密钥/agent/known_hosts/重连/隧道）；非 UTF-8 输出正确显示；配置目录明文凭据审计（自动化用例）通过 |
| **M4 扩展连接与集成打磨** | 串口/Telnet/日志/系统集成 | `SPEC.FEAT.INTERACT.05`、`SPEC.FEAT.WS.03`、`SPEC.FEAT.WS.08–09`、`SPEC.FEAT.WS.11`、`SPEC.FEAT.CONN.05–06`、`SPEC.FEAT.CONN.11–12`、`SPEC.FEAT.PREF.05`、`SPEC.FEAT.INTEG.01–02`、`SPEC.FEAT.INTEG.04` | 嵌入式串口调试场景可用（含发送行尾序列配置、**中文按 GB18030 编码发送**）；重启恢复工作区；OSC 133 / OSC 7 集成可用；剪贴板访问授权三态生效 |
| **M5 观察池** | 依赖框架缺口或低频 | 见 §5（未排期项）与 §6（未关闭缺口） | 按缺口关闭情况与真实使用频率择期排期，不设固定时间 |

**并行与预研**：M0 与 M1 可部分并行（渲染原语先行，PTY 与解析器同步开发）；SSH 传输栈与 M1/M2 的 UI 无耦合，可提前预研（裁决 7.2）。

**M0 → M1 的阻塞粒度**（2026-10-01 口径，2026-10-02 收口，供开工排序用）：M0 不是 M1 的整体前置。**G1 三腿已全部闭合（2026-10-01 实测）**：`SPEC.FEAT.RENDER.01` `SPEC.FEAT.RENDER.03` `SPEC.FEAT.RENDER.05` 与 `SPEC.FEAT.TERM.08` 的框架前置就此解除（宽度判定腿的判据见裁决 7.20，绘制两腿见裁决 7.22；原缺口形态与闭合过程的完整表述保留在附录 A.2 的 G1 行）。**余下的 G2 与 G13 亦已于 2026-10-02 闭合**（裁决 7.29）：`SPEC.FEAT.INTERACT.02` 的双击/三击腿不再受框架阻塞（实装仍随该条 own 的棒次），`SPEC.FEAT.RENDER.03` 的斜体改走批量入口的整批 opts、逐片段分流已撤销并由像素用例守住。**M0 至此对本仓当前排期内的条目不再有未闭项**（`SPEC.FEAT.WS.12` 与 `SPEC.FEAT.INTEG.03` 仍在观察池，落期不变）。M1 的其余项（终端逻辑层、键盘映射、UI 容器、PTY 与尺寸同步、配置）本就不依赖框架缺口，可在 M0 未关闭期间推进。**同日键盘映射棒新撞的三条（G14 `Tab` 派发 / G15 Win32 Alt 系 / G16 小键盘与 NumLock）不并入 M0**：它们阻塞的是 `SPEC.FEAT.INTERACT.01` 的**完整度**而非该条的**开工**（本仓首版已落并带三层验收），已按裁决 7.30⑤ 派发框架侧，**三条已于同日回货并由本仓接线闭合**（接线形态与 keypad 两档口径见裁决 7.36①②，§6 的对应行已标闭合），去向与影响面见 §6 与架构 §9.6。

**M1 渲染腿的棒内序**（2026-10-01 裁决 7.23④）：**第一棒＝上屏主路径**——`SPEC.FEAT.RENDER.01`（等宽网格 + 脏行重绘 + 视口自管）、`SPEC.FEAT.RENDER.03`（SGR 属性合成，含 bold-is-bright 与最小对比度）、`SPEC.FEAT.RENDER.04`（光标三形态与闪烁），交付到「本地会话输出看得见、光标跟手」即止；`SPEC.NF.PERF.02` 的吞吐基准随第一棒建，**并挂时间门禁**（劣化 >10% 即 FAIL，与裁决 7.22③ 的改判不冲突：那里锁的是框架批量入口的相对收益，这里锁的是本仓上屏层的每帧成本）。**该基准与门禁已于 2026-10-02 落地**，其执行形态、开与不开的指标、以及 CI 腿待基建的边界见裁决 7.34；同日按裁决 7.35 补上**构建档契约**（基准必须跑 `msvc-bench` 优化档，`build_config` 不一致即硬 FAIL）、把 `cat_mb_per_s` 从「写侧缺陷豁免」改为入门禁 B-7（原豁免的前提经实测不成立），并重捕获全部基线。**第二棒＝`SPEC.FEAT.RENDER.02` 字体族**（系统枚举 + 内置 Cascadia + CJK 缺字回退链）与缩放变更后的度量/字形缓存重建，因第一棒取系统等宽字体即可上屏、回退链的验收只在含汉字会话里才成立。文本选择（`SPEC.FEAT.INTERACT.02`）、键盘映射（`SPEC.FEAT.INTERACT.01`）与尺寸去抖（`SPEC.FEAT.XFER.01` 的 UI 侧来源）属交互层，不在渲染两棒内。

**配置层与面板的棒位**（2026-10-02 裁决 7.26 追加）：`borealis::config`（`SPEC.FEAT.PREF.03` + `SPEC.FEAT.PREF.06` + 提前落地的 ≥8 套预置色值与损坏降级线）**排在渲染第一棒之前**（本条已执行，配置层于 2026-10-02 落地）——第一棒的像素判据需要具体色值，而当时 `ui::PaletteSpec` 的 16 色与前/背景还是全零初始化，没有主题表就没有可判定的目标像素。`SPEC.FEAT.PREF.02` 设置面板按裁决 7.26⑦ **前置**到上屏两棒之后紧接着做。

---

## 3 需求优先级总表（权威映射）

| 需求标识 | 名称 | 优先级 | 阶段 |
|:---|:---|:---|:---|
| `SPEC.FEAT.TERM.01` | VT 解析器 | P0 | M1 |
| `SPEC.FEAT.TERM.02` | 颜色支持 | P0 | M1 |
| `SPEC.FEAT.TERM.03` | 主/备屏幕缓冲 | P0 | M1 |
| `SPEC.FEAT.TERM.04` | Scrollback 回滚 | P0 | M1 |
| `SPEC.FEAT.TERM.05` | 滚动区域与光标控制 | P0 | M1 |
| `SPEC.FEAT.TERM.06` | 鼠标上报 | P1 | M2 |
| `SPEC.FEAT.TERM.07` | OSC 集成 | P1 | M2；**状态机消费腿提前落地**（裁决 7.21④，含 OSC 52 写方向真机落系统剪贴板），标题→标签名→窗口标题栏的 UI 消费链路仍留 M2 |
| `SPEC.FEAT.TERM.08` | 宽字符 | P0 | M1 |
| `SPEC.FEAT.TERM.09` | 字符编码 | P1 | M3；UTF-8 基线腿随 M1（本地终端默认编码，是 M1 出口判据「中文可输入可显示」的必需前置），编码可配与 GB18030 等其余腿留 M3 |
| `SPEC.FEAT.RENDER.01` | 等宽网格渲染 | P0 | M1 **第一棒**（裁决 7.23④） |
| `SPEC.FEAT.RENDER.02` | 字体 | P0 | M1 **第二棒**（字体枚举 + 内置 Cascadia + CJK 缺字回退链；第一棒取系统等宽即可上屏，裁决 7.23④） |
| `SPEC.FEAT.RENDER.03` | 属性渲染 | P0 | M1 **第一棒**（裁决 7.23④） |
| `SPEC.FEAT.RENDER.04` | 光标 | P0 | M1 **第一棒**（裁决 7.23④） |
| `SPEC.FEAT.RENDER.05` | 缩放适配 | P1 | M2；与 `SPEC.FEAT.RENDER.02` 同棒接缩放变更后的度量与字形缓存重建（裁决 7.23④） |
| `SPEC.FEAT.INTERACT.01` | 键盘映射 | P0 | M1 |
| `SPEC.FEAT.INTERACT.02` | 文本选择 | P1 | M1（流式拖拽/矩形块基本面，`SPEC.FEAT.INTERACT.03` 选区复制的前置）+ M2（双击/三击等其余） |
| `SPEC.FEAT.INTERACT.03` | 复制粘贴 | P0 | M1 |
| `SPEC.FEAT.INTERACT.04` | 终端内搜索 | P1 | M2 |
| `SPEC.FEAT.INTERACT.05` | URL 检测 | P2 | M4 |
| `SPEC.FEAT.INTERACT.06` | 输入法（IME） | P0 | M1 |
| `SPEC.FEAT.XFER.01` | PTY 尺寸同步 | P0 | M1 |
| `SPEC.FEAT.WS.01` | 多标签页 | P0 | M1 |
| `SPEC.FEAT.WS.02` | 任意分屏 | P0 | M1 |
| `SPEC.FEAT.WS.03` | 多窗口 | P2 | M4 |
| `SPEC.FEAT.WS.04` | 标签状态提示 | P1 | M2 |
| `SPEC.FEAT.WS.05` | 会话生命周期 | P1 | M2（本地终端腿）+ M3（SSH 断线重连腿） |
| `SPEC.FEAT.WS.06` | 全屏 | P1 | M2 |
| `SPEC.FEAT.WS.07` | 命令面板 | P1 | M2 |
| `SPEC.FEAT.WS.08` | Pane 缩放 | P2 | M4 |
| `SPEC.FEAT.WS.09` | 广播输入 | P2 | M4 |
| `SPEC.FEAT.WS.10` | 撤销关闭标签 | P1 | M2 |
| `SPEC.FEAT.WS.11` | 会话恢复 | P2 | M4 |
| `SPEC.FEAT.WS.12` | Quick Terminal | P3 | M5 |
| `SPEC.FEAT.CONN.01` | 本地终端 | P0 | M1 |
| `SPEC.FEAT.CONN.02` | SSH 连接 | P1 | M3 |
| `SPEC.FEAT.CONN.03` | SSH 档案管理 | P1 | M3 |
| `SPEC.FEAT.CONN.04` | SFTP 浏览器 | P2 | M3 |
| `SPEC.FEAT.CONN.05` | 串口终端 | P2 | M4 |
| `SPEC.FEAT.CONN.06` | Telnet | P2 | M4 |
| `SPEC.FEAT.CONN.07` | 连接管理器 UI | P1 | M3 |
| `SPEC.FEAT.CONN.08` | SSH 隧道 | P2 | M3 |
| `SPEC.FEAT.CONN.09` | 凭据安全存储 | P1 | M3 |
| `SPEC.FEAT.CONN.10` | 密钥管理器 | P2 | M3 |
| `SPEC.FEAT.CONN.11` | 会话日志 | P2 | M4 |
| `SPEC.FEAT.CONN.12` | 剪贴板访问授权 | P2 | M4；`OSC 52` 写方向的「默认允许」档已随 `SPEC.FEAT.TERM.07` 的提前腿落地（裁决 7.21③），读方向三态授权仍留 M4 |
| `SPEC.FEAT.PREF.01` | 主题 | P1 | M2；**≥8 套预置配色的色值表随配置层提前并已落**（2026-10-02 八套，裁决 7.26②，上屏第一棒须有可判定的色值，缺省 Dracula）；主题切换入口与亮/暗 chrome 适配仍留 M2 |
| `SPEC.FEAT.PREF.02` | 设置面板 | P1 | **前置**：紧跟上屏层之后做（裁决 7.26⑦），含裁决 7.25⑧ 的状态栏条目入口、裁决 7.25⑩ 的真实绘制路径预览与主题切换 |
| `SPEC.FEAT.PREF.03` | 持久化 | P0 | M1；**代码已落**（2026-10-02）：四分类全量 schema + 单文件落盘 + 首启不写盘，落盘形态三条框架约束见裁决 7.27 |
| `SPEC.FEAT.PREF.04` | 快捷键系统 | P1 | M2 |
| `SPEC.FEAT.PREF.05` | i18n | P2 | M4；「命令 id → 中/英词条」的映射表口径已由裁决 7.25⑬ 定死，词条本体随本条 |
| `SPEC.FEAT.PREF.06` | 零配置可用 | P0 | M1 |
| `SPEC.FEAT.PREF.07` | 配置韧性 | P1 | M2；**损坏降级线（备份 `*.corrupt-<时间戳>` + 回落默认 + 显著提示）随配置层提前并已落**（2026-10-02，裁决 7.26④：备份早于任何写入、备份未成功即拒绝落盘，提示以 `LoadReport` 交 UI；**对话框本体 `TODO(SPEC.FEAT.PREF.07)` 随设置面板**）；快照回滚与本地导出导入留 M2 |
| `SPEC.FEAT.PREF.06` | 零配置可用 | P0 | M1；**代码已落**（2026-10-02）：`Settings{}` 即首次启动那份，装载不产生文件 |
| `SPEC.FEAT.INTEG.01` | OSC 133 命令块 | P2 | M4；`OSC 133` 的边界标记与退出码来源已预埋（裁决 7.21④），命令块区间附着到网格行与其消费行为仍留 M4 |
| `SPEC.FEAT.INTEG.02` | OSC 7 工作目录 | P2 | M4；`OSC 7` 的目录原文来源已预埋（裁决 7.21④），新标签/分屏继承与远端语义下的降级仍留 M4 |
| `SPEC.FEAT.INTEG.03` | 系统通知 | P3 | M5 |
| `SPEC.FEAT.INTEG.04` | CLI 启动参数 | P2 | M4 |
| `SPEC.NF.PERF.01` | 输入延迟 | P0 | M1 |
| `SPEC.NF.PERF.02` | 渲染吞吐 | P0 | M1 |
| `SPEC.NF.PERF.03` | 启动时间 | P1 | M2 |
| `SPEC.NF.PERF.04` | 内存 | P1 | M2 |
| `SPEC.NF.PERF.05` | 空闲资源占用 | P1 | M2 |
| `SPEC.NF.PERF.06` | 输出背压 | P0 | M1 |
| `SPEC.NF.PLAT.01` | 跨平台 | P0 | M1（Windows）+ M2（Linux） |
| `SPEC.NF.RELI.01` | 可观测性 | P1 | M2 |
| `SPEC.NF.PKG.01` | 打包分发 | P1 | M2 |
| `SPEC.NF.A11Y.01` | 无障碍基线 | P3 | M5 |

计 66 条：P0 23 条、P1 24 条、P2 16 条、P3 3 条。

---

## 4 延后子项清册（父需求按期交付，子能力后补）

| 父需求 | 延后子项 | 目标阶段 |
|:---|:---|:---|
| `SPEC.FEAT.INTERACT.02` | 智能选择（引号/括号配对扩展，iTerm2 语义） | M4 |
| `SPEC.FEAT.INTERACT.02` | quick select 模式（视区内 URL/路径/哈希字母标签速取，WezTerm 语义） | M4 |
| `SPEC.FEAT.CONN.02` | agent 转发（`ForwardAgent`，按 profile 开关） | M4 |
| `SPEC.FEAT.CONN.02` | 同一 profile 的连接复用（多标签共享一条 TCP 连接） | M4 |
| `SPEC.FEAT.CONN.02` | 密钥交换与主机密钥算法可配、显式启用旧算法 | M4 |
| `SPEC.FEAT.CONN.03` | 跳板机（ProxyJump 语义） | M4 |
| `SPEC.FEAT.CONN.05` | 十六进制收发视图与行首时间戳前缀 | M4 之后 |
| `SPEC.FEAT.CONN.07` | 快捷片段（snippets：分组/搜索/参数占位） | M4 |
| `SPEC.FEAT.WS.04` | 可听铃声（beep；框架音频后端已具备，须开 `AURORA_ENABLE_AUDIO`） | M5 观察池 |

---

## 5 观察池

| 项 | 关联需求 / 缺口 | 阻塞条件与现状 |
|:---|:---|:---|
| Quick Terminal（quake 下拉终端） | `SPEC.FEAT.WS.12`、框架缺口 G9（**已于 2026-10-02 闭合**） | OS 级全局热键的框架依赖已解除（`OsHotkeyRegistry`，裁决 7.29③）；原列的另一项硬依赖「无边框贴边窗口形态」仍未落，落期不变 |
| 系统通知（OSC 9 / 777 转 toast） | `SPEC.FEAT.INTEG.03`、框架缺口 G11（**已于 2026-10-02 闭合**） | 跨平台通知 API 与激活回传已在框架公共 API（`NotificationCenter`，裁决 7.29④）；本仓的 OSC 9 / 777 消费与转发仍未开工，落期不变 |
| 无障碍基线 | `SPEC.NF.A11Y.01` | 依赖 Aurora 无障碍平台桥（Aurora 主仓 `codespec/ARCHITECTURE.md` §8.5）落地；语义树与 UIA/AT-SPI2 桥已部分具备 |
| scrollback reflow | `SPEC.FEAT.TERM.04`、裁决 7.5 | 默认不 reflow，与超长行截断（裁决 7.4）配套；重开需先解决历史行内存模型 |
| 字体连字 | `SPEC.FEAT.RENDER.02` | 终端等宽语义下收益低，且与整像素 cell 度量冲突 |
| kitty keyboard protocol / `modifyOtherKeys` | `SPEC.FEAT.INTERACT.01` | 现代协议增量；先由基础组合键转发覆盖 |
| Emoji 精确对齐 | `SPEC.FEAT.TERM.08` | 位图混排与变体选择处理成本高，基础呈现已可用 |
| 终端内图像协议（Sixel / kitty graphics / imgcat） | SPECIFICATIONS.md §2.2 裁剪表 | 依赖位图混排渲染路径，工作量与收益不成比 |
| Zmodem 文件传输 | SPECIFICATIONS.md §2.2 裁剪表 | SFTP 覆盖主流场景 |
| 触摸选择与滚轮手势 | 框架缺口 G6 | 框架手势层齐备，win32/glfw/wayland 无触摸采集实现 |
| 写侧四项微优化观测 | `SPEC.NF.PERF.02`、裁决 7.35⑥ | 逐行滚动每帧走整屏重建（`Terminal::scroll_text` 整屏分支置 `full_screen_dirty_`）、`grid::Row::reset()` 按整行清零而不用 `occupancy_` 上界、零宽与超链接侧表缺空表守卫、每条 CSI 解析分配一个 `std::vector` 子参数。四者都缺「改了就转红」的基准判据，故只登记不动手；真要做须各以其基准数字结项（AGENTS.md §4.4 第 21 条），不凭「看起来更省」 |
| macOS 平台 | `SPEC.NF.PLAT.01`、框架缺口 G8 | `MacOSSurface` 仅骨架 |

---

## 6 框架缺口补入排期

| 缺口 | 内容 | 补入时点 |
|:---|:---|:---|
| G1 | ~~批量文本绘制原语 + 等宽整像素 cell 度量 + East Asian Width 宽度判定~~ **已闭合（2026-10-01）**：三腿均以 Aurora 公共 API + 单测 + 契约文档交付——`unicode_cell_width`（裁决 7.20）、`Painter::draw_text_runs` 与 `render::FontEngine::monospace_cell` → `render::CellMetrics`（裁决 7.22） | ~~M0（阻塞 `SPEC.FEAT.RENDER.01` `SPEC.FEAT.RENDER.03` `SPEC.FEAT.RENDER.05`）~~ 阻塞解除；渲染层可开工，形态边界见架构 §9.2 |

| G2 | ~~`MouseEvent` 多击语义（框架统一自算）~~ **已闭合（2026-10-02 实测）**：`MouseEvent::click_count` 由派发层集中自算（1..3，Release/Move 恒为 1，阈值 500ms / 4dp 为库内常量、未接系统双击速度），形态与判据见裁决 7.29② | ~~M0（阻塞 `SPEC.FEAT.INTERACT.02` 的双击/三击腿，属 M2）~~ 阻塞解除；实装随该条 own 的棒次，本仓不自算多击 |
| G13 | ~~批量文本入口 `Painter::draw_text_runs` 不透出 `TextLayoutOpts` / `TextAAMode`~~ **已闭合（2026-10-02 实测）**：新增 `(runs, opts)` / `(runs, aa_mode, opts)` 重载，opts 整批共用、框架刻意不做 per-run opts（裁决 7.29①） | 已撤销：本仓的逐片段 `Painter::draw_text` 分流于同日改为「按是否斜体分两批走批量入口」，并以一条变异自证的像素用例守住（裁决 7.29⑤） |
| G12 残留单腿 | X11 / Wayland / GLFW 的 DPI 缩放变化上报（公共 API 与 Win32 已具备） | M2 的 Linux 等价补齐 |
| G9 / G11 | ~~OS 级全局热键、系统通知 API（2026-10-01 复核仍开放）~~ **均已闭合（2026-10-02 实测）**：`OsHotkeyRegistry`（裁决 7.29③，无后端平台 `add()` 机器可见地失败、不静默 no-op）、`NotificationCenter`（裁决 7.29④，含激活回传与「仅记录」测试后端）；G9 原列的「应用侧直调平台 API」候选继续作废（裁决 7.24①） | 观察池（§5）：**回货只解除框架依赖，不改落期**，触发条件仍是 Quick Terminal 或系统通知开工 |
| G10 | ~~无音频播放 API~~ 已关闭（WASAPI/ALSA 后端具备），无须补框架 | — |
| G14 / G15 / G16 | ~~`Tab` 被派发器无条件当焦点遍历消费而无控件优先钩子、Win32 把 `WM_SYSKEY*` 挡在事件链之外（Alt 系到不了控件）、`KeyCode` 未建模数字小键盘与 NumLock~~ **均已闭合（2026-10-02 同日回货并接线）**：`Widget::wants_tab_keys()`（计入 `has_input_semantics()`）、syskey 与常规键合流走同一按键入口、`KeyCode::KP_*` 段（`KP_Insert = 100` 连号）与 `ModifierKey::NumLock`（原始事实保留于附录 A.2 的行位）。派发侧的 `TODO(SPEC.FEAT.INTERACT.01)` 桩已消除，keypad 两档编码口径见裁决 7.36② | ~~阻塞 `SPEC.FEAT.INTERACT.01` 的完整度（`Tab`、Alt 系、keypad 应用模式三腿）与 `SPEC.FEAT.PREF.05` 的 Alt 系快捷键~~ 阻塞解除；**键入腿的真机目视仍待人工**（锁屏下 `SendInput` 静默失效，裁决 7.31① / 7.36④） |
| G17 | ~~DPI 缩放的真值源不统一：`ctx.scale_factor` / `Painter::scale()` 在 144 DPI 机器上恒 `1.0`，而同一窗口逻辑尺寸随路径给出 `960x640` / `2560x1369.33` / `640x426.667` 三个互不相容值~~ **已闭合（2026-10-02 同日回货并经本仓真机复验）**：Aurora 侧把 cached `scale` 与 `handle_size` / `handle_getminmaxinfo` 的现调合一，`refresh_scale()` 为唯一 DPI 读点、`to_logical` / `to_physical` 为唯二换算点；原定验收（解除其两个 e2e 用例的 known-scale-gap SKIP）达成，本仓三态（启动 / 最大化 / 还原）截屏均铺满客户区（原始事实与证人保留于附录 A.2 与裁决 7.36④）。**残留一腿另立 G21** | ~~阻塞 `SPEC.FEAT.RENDER.01` / `RENDER.05` / `XFER.01` 的**真机验收腿**~~ 尺寸与覆盖两腿的阻塞解除；本仓换算一直单源（只经 `ctx.scale_factor`），回货零改动 |
| G18 | ~~指针与滚轮事件不携带修饰键位：`KeyEvent` 有 `modifiers` 而 `MouseEvent` / `ScrollEvent` 没有，全库亦无「此刻按下哪些修饰键」的查询公共 API~~ **已闭合（2026-10-02 同日回货）**：两个事件类型各带 `ModifierKey modifiers`（缺省 `None`、复用同一位掩码枚举），由各后端在事件构造处 stamp 平台真实修饰态，触点与程序合成恒 `None`，派发器不推断（原始事实保留于附录 A.2，形态见裁决 7.36①）。**Alt+拖拽列模式的消费点已随 2026-10-03 的选区界面腿落地（裁决 7.40⑤），Ctrl+滚轮缩放一腿仍待** | 框架阻塞解除而**落期不变**：`SPEC.FEAT.INTERACT.02` 的列模式（Alt+拖拽）**已随 2026-10-03 的选区界面腿落地**（视觉稿 D1~D7 已于裁决 7.38① 拍板）、`SPEC.FEAT.RENDER.02` 的 Ctrl+滚轮随字体棒、`SPEC.FEAT.INTERACT.05` 的 Ctrl+点击随链接腿；本仓回货前不自造替代判定的纪律继续有效 |
| G19 | ~~等宽宽度判定逐码点做三次区间查找：`aurora::unicode_cell_width` 对三张 constexpr 区间表各一次 `std::ranges::upper_bound`，且实现落在 `.cpp` 故跨翻译单元不可内联~~ **已闭合（2026-10-02 同日回货并经本仓基准复验）**：框架侧取「由三表首项导出的单宽下界」把三次二分换成一次比较（判定结果逐码点不变，全码点穷举等值为其验收）。本仓消费侧零改动，按裁决 7.35③ 整体重捕获基线：写侧纯链观测 25.9 → **约 41 MB/s**，B-7 参考值 20.627 → **28.888**（原始事实与探针口径保留于附录 A.2 与裁决 7.35⑤） | ~~观察池：只关余量大小与写侧上限~~ 已闭环；`SPEC.NF.PERF.02` 两条绝对线在优化档仍全部通过，`cat_mb_per_s` 作为唯一能看见写侧退化的档位入门禁 B-7（相对线的判据细化见裁决 7.36③） |
| G20 | **主键盘 `Insert` 未建模**（2026-10-02 键盘回货复验实测）：`KeyCode` 只有小键盘导航区的 `KP_Insert`，主键盘 `Insert` 无对应项，Win32 后端亦无该键映射痕迹，故遗留档的 `CSI 2~` 在 Win32 永无从产出；事实依据见附录 A.2 | 按裁决 7.13① / 7.36⑤ **派发 Aurora 侧**：新增键码须**追加到枚举末尾并写死显式数值**（本仓 `KeySym` 与框架逐值对齐的单测锁住该规则，隐式连号段插入即红），并在四后端映射。本仓波浪号族 `2` 号位已实现且被两条来源走过，回货接一行键码折算；阻塞 `SPEC.FEAT.INTERACT.01` 的 `Insert` / `Shift+Insert`（= `Paste`）两键，**不阻塞该条其余腿** |
| G21 | **建窗时刻的 DPI 读数仍为 `1.0`，故 `WindowOptions::size` 的逻辑 dp 被按物理像素用掉**（2026-10-02 真机走查实测）：`CreateWindowExA` 之前调 `refresh_scale()` 而此刻 `hwnd` 尚空，`GetDpiForWindow(nullptr)` 返回 0 且「回落 `GetDpiForSystem`」挂在函数指针为空的分支上故结构不可达；实测 960 dp 请求得 655 dp 窗口。G17 闭合后的残留腿、成因不同，故另立新号（裁决 7.36⑤）；事实依据见附录 A.2 | 按裁决 7.13① **派发 Aurora 侧**：建窗期按「窗口即将落位的显示器」取 DPI（或让 `dpi == 0` 真正落到下一级回落），建窗后仍按窗口值刷新；验收须把「≠100% DPI 下请求 `800 x 600 dp` 的首帧客户区逻辑尺寸逐位相等」补进那枚真机探针的判据（现缺此腿）。**阻塞真机的首屏行列数**（高分屏首屏比期望少约 `1/1.5`），**不阻塞开工与无头用例**：`HeadlessSurface` 的 scale 由测试注入，本仓换算路径已单源、回货零改动 |
| G22 | **无「列出系统等宽字体族」的公共入口**（2026-10-03 排期前复核实测）：`font_discovery.h` 的每条 API 都要先知道 family 名，系统字体目录扫描与等宽判定藏在实现内不回报族名，故 `SPEC.FEAT.RENDER.02` 的「系统等宽字体枚举」腿无数据源；事实依据见附录 A.2 | 按裁决 7.13① / 7.37① **派发 Aurora 侧**补「列出可用字体族（可按是否等宽过滤）」并令其与 `resolve_faces` 族名口径同源。**不阻塞 `RENDER.02` 开工**：首版只暴露内置 Cascadia Code 一族 + 可配回退链（需求以内置族为默认），枚举腿待回货接并以 `TODO(SPEC.FEAT.RENDER.02)` 留痕；`SPEC.FEAT.PREF.02` 的字体族下拉同批受影响 |

后续按裁决 7.13 的类别分流：凡属渲染与事件链路的缺口，撞到即先补框架；交互体验类缺口留在应用侧。

---

## 7 跨平台分期决策

- **先 Windows 后 Linux**：开发机为 win32，首个交付阶段的出口判据本身就是「替换 Windows Terminal」，Linux 侧当时缺少等价验证手段。
- 该分期**不减损** `SPEC.NF.PLAT.01` 的等价性要求；平台相关层（PTY、串口、传输、默认 shell 探测、DPI 缩放上报）自首个交付阶段起即按接口抽象隔离，禁止 Windows 假设（ConPTY 句柄、Win32 类型、码页 API）渗入共享路径——否则后补的 Linux 等价会退化成重写。
- Linux 侧验证通道与 CI 形态属实施细节，见 `codespec/ARCHITECTURE.md` §14.4；本机 WSL 与 GitHub Actions 两条路径的取舍随 M2 开工前裁决。

---

## 8 交付进展（滚动更新）

> 本节只记**阶段级**进展与待接接缝，供开工排序用。**文件级现状**（哪些路径已落地、哪些仍待建）以 `AGENTS.md` §6 为唯一来源，此处不重复。随进展就地更新，不另开文档。

| 阶段 | 状态 | 依据与剩余项 |
|:---|:---|:---|
| **M0 框架补强** | **已收口（2026-10-02）** | **G1 已整体闭合（2026-10-01）**：三腿均在 Aurora 主仓以公共 API 落地并带单测与契约文档（`unicode_cell_width`，裁决 7.20；`Painter::draw_text_runs` 与 `FontEngine::monospace_cell`，裁决 7.22）。**G2 / G9 / G11 / G13 四条亦于 2026-10-02 以公共 API 落地并被本仓消费**（取用与撤销口径见裁决 7.29）：M0 对本仓排期内的条目不再有未闭项，G9 / G11 只解除框架依赖、其消费者仍在观察池的落期上 |
| **M1 单终端 MVP（Windows）** | 部分落地 | 已落地（纯逻辑层均可脱离 UI 独立单测；平台层为 Windows 侧真机 e2e）：`SPEC.FEAT.TERM.01` 的 VT 解析器与字符集切换映射、`SPEC.FEAT.TERM.09` 的 UTF-8 双向编解码（M1 基线腿）、`SPEC.FEAT.TERM.04` 的 scrollback 环形存储、**终端状态机**（`SPEC.FEAT.TERM.01` 的 Print/Execute/ESC/CSI 显示内核与私有模式登记、`SPEC.FEAT.TERM.02` 的 16/256/真彩色、`SPEC.FEAT.TERM.03` 的主备屏、`SPEC.FEAT.TERM.05` 的光标与滚动区域/擦除/插删/制表位；`SPEC.FEAT.TERM.08` 的宽度与双宽占位、combining 并入腿（判定表随 G1 第三腿闭合））、**会话层**（架构 §7.2 粒度拍板后的 `session::Connection` 基础接口与 `ConnectionEvents` 反向通道、`session::Session` 组合解码/状态机/网格与写通道、`session::DamageQueue` 的有界背压队列：`SPEC.NF.PERF.06` 的合并而非丢弃与单帧预算让出、`SPEC.NF.RELI.01` 的水位/合并/让出计数、`SPEC.FEAT.TERM.01` 的 DA1/DSR 应答回写、`SPEC.FEAT.XFER.01` 的尺寸下发腿）、**`SPEC.FEAT.CONN.01` 的本地终端 Windows 腿**（`conn::LocalTerminalSpec` + 连接工厂、`platform::ConptyConnection` 的伪终端创建/挂载/读写/关停、默认 shell 探测链、PTY 环境注入与 profile 覆盖，实现口径见裁决 7.19）、**OSC 消费腿**（`SPEC.FEAT.TERM.07` 的状态机侧消费提前落地，裁决 7.21：`0/2` 标题、`7` 目录原文、`8` 超链接区间与有界链接表、`52` 写方向经主线程落系统剪贴板、`133` 边界与退出码，未识别命令号计入留痕；`OSC 7` / `133` 只是 `SPEC.FEAT.INTEG.01/02` 的来源预埋，其消费行为仍留 M4）、**配置层**（`SPEC.FEAT.PREF.03` + `SPEC.FEAT.PREF.06` 的 schema 与落盘、`SPEC.FEAT.PREF.01` 的 ≥8 套预置色值表、`SPEC.FEAT.PREF.07` 的损坏降级线（备份 + 回落默认 + `LoadReport` 交 UI），按裁决 7.26 建全量四分类、按裁决 7.27 适配框架的点号路径模型）、**渲染上屏第一棒**（`SPEC.FEAT.RENDER.01` 的等宽网格 + 脏行重绘 + 视口自管、`SPEC.FEAT.RENDER.03` 的 SGR 属性合成含 bold-is-bright / 暗淡 / 反色 / 最小对比度与下划线四档笔形、`SPEC.FEAT.RENDER.04` 的光标三形态 + 闪烁 + 失焦降级，绘制主路径与装配于 2026-10-02 落地：`src/ui/terminal_view.{h,cpp}` 全仓唯一触达 `au::Painter` / `au::Widget`、`session/screen_mirror.h` 主线程可见区副本、`Session::set_frame_wake` + `Application::set_on_frame` 的帧唤醒排帧（`TODO(SPEC.FEAT.RENDER.01)` 就此消除）、`SPEC.FEAT.XFER.01` 的 UI 侧取值腿由控件首帧布局派生行列并下发；验收形态是 `tests/integration/itest_render_viewport.cpp` 的十例无头像素差分，其中斜体两例以变异自证；细则见架构 §9.2 / §9.5 与 `codespec/RENDER_VIEWPORT.draft.md`）、**键盘映射首版**（`SPEC.FEAT.INTERACT.01` 于 2026-10-02 落地：纯逻辑编码件 `term::encode_key`（遗留档 xterm 口径逐字节定死，`KeySym` 与框架 `KeyCode` 逐值对齐）、视口控件的 `on_key_event` / `on_text_input` 两入口与「可打印键归文本通道」的分工、`DECCKM` 经真实状态机生效、`Ctrl+Alt` 系让快捷键层独占，口径与打桩边界见裁决 7.30，回货后的两档 keypad 编码口径见裁决 7.36②，形态见架构 §3.5 / §10.1））、**选区与复制文本的纯逻辑件**（`SPEC.FEAT.INTERACT.02` 首版交付面与 `SPEC.FEAT.INTERACT.03` 复制腿里与像素无关的那半，2026-10-02 落地：`ui::row_spans` 端点归一（流式 / 列模式两态）+ `ui::copy_text` 取文本（LF 分隔、双宽整字符、三项默认关闭的复制变换及其固定次序），口径见裁决 7.32，形态见架构 §10.2）、**粘贴处置计划的纯逻辑件**（`SPEC.FEAT.INTERACT.03` 粘贴腿里与对话框无关的那半，2026-10-02 落地：`term::plan_paste` 把「发什么 / 分几块 / 块间等多久 / 要不要多行警告」一次算成 `PastePlan`，bracketed paste 原样透传优先于一切换行策略，换行三策略的枚举迁入领域件后 config 侧只作别名（单一真值源），口径见裁决 7.33，形态见架构 §10.2）、**指针落点 → 格子序号的换算件**（`SPEC.FEAT.INTERACT.02` 界面腿的第一件，2026-10-02 落地：`ui::cell_at_point` 把框架事件的逻辑 dp 折成 `ui::GridCellPos`，与 `rect_for` 同坐标空间、同为绘制行号，越界**钳位**而非丢事件（Press 时 `SetCapture` 使拖出窗口的 Move 持续到达），口径与代价见 CHANGELOG v0.30、形态见架构 §10.2）、**选区界面腿本体**（`SPEC.FEAT.INTERACT.02` 首版交付面 + `SPEC.FEAT.INTERACT.03` 复制落地，2026-10-03：`TerminalView::on_pointer_event` 的按下/拖动/抬起状态机按框架 `click_count` 定粒度（逐格 / 按词 / 按行，其 M2 腿一并落）、高亮在色带层内替换底色并按 `min_contrast` 重合成、失焦各半混合、Alt+拖拽列模式取 `MouseEvent::modifiers`（G18 回货字段在本仓的第一个消费点）、复制在视口侧的既有 `Session::read` 临界区内取文本出锁落剪贴板（接缝改判见裁决 7.40①），视觉稿 `codespec/UI_SELECTION.draft.md` 的 D1~D7 由裁决 7.38① 拍板）、**右键三态与粘贴呈现腿**（`SPEC.FEAT.INTERACT.03` 的剩余交付面，2026-10-03：纯逻辑决策件 `ui::plan_right_click`（三态 × 有无选区 → 意图四值 `None / Copy / Paste / Menu`，`RightClickAction` 定义迁入 `ui/right_click.h`、config 侧只作别名）、视口内 `au::Popup` 菜单（无选区时「复制」置灰而非静默失败）与 `au::Dialog` + `aurora::confirm` 的多行粘贴警告（先确认后发，问不了就不粘）、`session::ClipboardOutbox::read()` 补上读方向、按 `PastePlan` 经 `Scheduler::set_timeout` 的逐块排期；装配层场景根换 `au::OverlayHost`，剪贴板读写与菜单动作一律落帧边界，口径见裁决 7.41、形态见架构 §9.2 / §10.2）、**分屏 pane 树的纯逻辑件**（`SPEC.FEAT.WS.02` 四义里与绘制无关的那半，2026-10-03：`ui::PaneTree` 多子容器 + 叶子树，`split` / `close` / `set_focus` / `container_of` / `layout` / `move_divider` / `equalize` 与自由函数 `ui::route_focus`；同轴切分并入父层作兄弟、切分只动 target 那一份、树不持几何记忆（绝对尺寸只在 `layout()` 产物里，与 `move_divider` 共用一条折算算式故缩放不回弹）、装不下最小值时整层退均分、边界吸附整数物理像素、把手由 `(ContainerId, slot)` 确定、方向键按「先垂直间隙再主方向间隙」两级排序且候选须严格居侧，十一条口径见裁决 7.42、形态见架构 §8.1）、**标签列表的纯逻辑件**（`SPEC.FEAT.WS.01` 里与绘制、与会话无关的那半，2026-10-03：`ui::TabStrip` 给出顺序、选中与「该显示哪个名字」，自由函数 `resolve_tab_name` 是优先级与「空串即让位」两条口径的唯一判定点；`TabNamePriority` 的唯一定义处在该头、`config` 侧只作别名并新增 `appearance.tab_name_priority`（落盘名 `manual_wins` / `osc_wins`，缺省前者），重排目标下标以「其余标签」为基准、末位标签不归本件关（那是 `SPEC.FEAT.WS.03`）、选中交接在**移除之前**的次序里算，九条口径见裁决 7.43、形态见架构 §8.1）、**渲染吞吐基准与时间门禁**（`SPEC.NF.PERF.02`，2026-10-02 落地：`tools/bench/render_throughput.cpp` 的独立可执行 `borealis_bench` 三场景（逐行滚动 / 强制整屏重绘 / `cat` 10 MB 灌注，软件 Painter + `HeadlessSurface`，960×640 dp 实测 24×73 格，固定种子混合行内容），`tools/check/check_perf_gates.ps1` 按 `perf_baseline.json` 判红线（绝对线照需求原文、相对线 10% 只对抖动窄于该窗口的指标开、三次独立进程取中位），三条变异注入自证非空转；基准不入 CTest 的理由见裁决 7.34，形态见架构 §13。**同日的写侧归因把 `cat_mb_per_s` 从豁免改为入门禁 B-7，并立下构建档契约（基准须跑 `msvc-bench` 优化档，`build_config` 不一致即硬 FAIL），基线整体重捕获，口径见裁决 7.35**；余下真实瓶颈在框架宽度判定（附录 A.2 的 G19），本仓探针已撤销——**G19 于同日回货闭合，七档参考值随之整体上移（B-7 由 20.627 到 28.888），复验读数与「相对线按逆向侧偏离量判」的细化见裁决 7.36③**）。**CI 腿未落**：本仓无 CI 工作流基建，脚本以退出码就绪，需求原文「进 CI」随该基建兑现。未开工：`SPEC.FEAT.CONN.01` 的 POSIX 腿（`$SHELL` 探测 + `forkpty` 同口径）、`SPEC.FEAT.RENDER.02` 字体族与缩放变更后的缓存重建（第二棒）、`SPEC.FEAT.WS.01` 与 `SPEC.FEAT.WS.02` 的**界面腿**（标签栏与分屏容器 widget、栏位宽度与溢出滚动、逐标签连接类型图标与活动角标、拖拽落点折下标、关闭按钮命中、「关闭前确认（有运行中进程时）」的对话框、把手命中与拖拽接线、焦点 pane 描边、方向键接线、逐 pane `Session::resize` 下发；`WS.01` 的标签列表件与 `WS.02` 的 pane 树件两块纯逻辑前置均已落（裁决 7.43 / 7.42），故本条余下的就是这段界面腿，开工前须先补标签栏与分屏交互的视觉稿评审）、`SPEC.FEAT.INTERACT.02` 界面腿与 `SPEC.FEAT.INTERACT.03` 右键三态的**真机走查未做**（拖拽跟手、跨回看滚动的选区跟随、copy-on-select 是否真落系统剪贴板、右键菜单的点选与多行警告的呈现、逐行节流是否丢字；本体均已落地，见上两条）、`SPEC.FEAT.INTERACT.06` 的 preedit 就地绘制与候选窗定位（committed 文本腿已随键盘映射接上）、`SPEC.FEAT.PREF.02` 设置面板与 `TODO(SPEC.FEAT.PREF.07)` 降级对话框（右键三态与复制语义三项的 **UI 入口**随本条，键已在 schema 内）、`SPEC.FEAT.XFER.01` 的**尺寸去抖合并**（取值腿已由控件首帧布局接上，连续 resize 的合并策略未做）。`SPEC.FEAT.INTERACT.01` 已落首版并**完成回货接线**（G14 的 `wants_tab_keys()` 钩子、G15 的 Alt 系 syskey 合流、G16 的 keypad 两档与 `NumLock` 修饰位，裁决 7.36②，见 §6 与架构 §9.6）；本条余下的是**真机手感与对端接受度的目视腿**（锁屏会话下 `SendInput` 静默失效，裁决 7.31①），以及随 **G20**（主键盘 `Insert` 未建模）回货的 `CSI 2~` 一条腿。`SPEC.FEAT.RENDER.04` 的闪烁频率仍内置 500ms，代码处挂 `TODO(SPEC.FEAT.PREF.02)` |
| **M2–M5** | 未开工 | 排期不变 |

**M1 待接接缝**（已落地模块之间尚未打通处；表中顺序即建议的开工顺序）：

| 接缝 | 现状 | 说明 |
|:---|:---|:---|
| 解码 → 解析 → 状态机 → 网格 | 已打通 | 集成用例 `tests/integration/itest_terminal_scene.cpp` 以真实形态的首帧输出驱动全链路（字节流 → 解码 → 解析 → 网格内容），断言落在框线码点、SGR 色值与来源、OSC 吞掉、主备屏互不污染。**`ESC(0` 的框线效果就此有端到端证据**（`SPEC.FEAT.TERM.01` 验收判据腿） |
| 状态机 → 会话回写 | 已打通 | 终端属性与光标位置查询（DA1、DSR）经会话写通道回写：状态机经注入的 `term::ResponseSink` 在锁内登记应答、会话出锁后编码写入连接（架构 §3.4、§5.2，裁决 7.18③ 定下只报档位 62）。单元用例 `tests/unit/utest_session.cpp` 以 `Connection` 替身驱动全链路并断言回写字节。OSC 各命令的消费见下一行 |
| OSC 消费 → 会话产物 | 已打通（`SPEC.FEAT.TERM.07` 提前落地） | 状态机把 `OSC 0/2/7/8/52/133` 消费成结构化产物（`term::OscState` 快照 + 有界链接表 + `take_clipboard_write()` 取走语义，形态与理由见裁决 7.21），会话侧以加锁访问器透出；`OSC 52` 写方向由 `session::ClipboardOutbox::drain()` 在主线程转调 Aurora `Clipboard::set_text` 真落系统剪贴板，读方向 `52;c;?` 回写空响应。单测 `tests/unit/utest_terminal_osc.cpp` 定点驱动每条命令，e2e `tests/e2e/etest_osc_clipboard.cpp` 用真机 ConPTY + powershell 发出 OSC 52 并回读剪贴板。余下的是消费方：标题上标签名/窗口标题栏、超链接的 hover 与点击、133 的命令块区间 |
| 网格 → 主线程副本 → 视口绘制 | 已打通（2026-10-02，`SPEC.FEAT.RENDER.01/03/04` 第一棒） | 后台权威网格不动，主线程经 `session::ScreenMirror` 持有可见区副本：`Session::read` 临界区里按脏行区间取回内容并重投影回看偏移（D5②/D6①），偏移变化即整窗重建。绘制侧 `src/ui/terminal_view.cpp` 五层序列（铺底 → 色带 → 分两批的批量文本 → 装饰 → 光标与指示条）只消费 `ui::palette` / `ui::cell_layout` 合成后的最终值，架构 §9.2。验收是 `tests/integration/itest_render_viewport.cpp` 十例无头像素差分：未变行逐位相同、差集恰等于「本帧提交行 ∪ 两帧光标行」、色带与装饰落笔矩形、斜体两例（批量入口与块形光标重画，各以变异自证）、光标三形态与失焦描边、整窗换源与回到底逐位复现、备屏不可滚、双宽与零宽列位。**余下**：真机走查的**目视**判据（M1 出口判据的「窗口连续 resize 后 `vim`/`tmux` 不错位」；2026-10-02 首轮撞出 G17——144 DPI 机器上最大化/还原后网格只覆盖客户区约 `1/1.5`，该判据当时不可能通过；G17 已于同日日终闭合并真机复验通过，三态均铺满客户区，裁决 7.36④——但「不错位」这句话要人眼看，且**首屏行列数仍被 G21 挡住**：建窗期 DPI 回 `1.0`，960 dp 请求实得 655 dp，故连续 resize 走查须等 G21 回货后再做）、`SPEC.FEAT.TERM.06` 上报模式下的滚轮转发（代码处 `TODO`） |
| 会话 → 平台连接 | 已打通（Windows 腿） | `conn::make_local_terminal_connection` 造出 `platform::ConptyConnection` 并交予 `session::Session`，读线程把伪终端管道的原始字节喂进「解码 → 解析 → 状态机 → 网格」整条链；端到端证据是 `tests/e2e/etest_local_terminal.cpp`（真实 shell 的输出上屏、键入往返、突发输出的队列水位、关闭后进程终结且内容保留）。四处 Win32 实现口径见裁决 7.19余下的是键入与选择等输入侧（`SPEC.FEAT.INTERACT.01`）。**帧调度通知已接**（2026-10-02）：提交侧经 `Session::set_frame_wake` 注入 `Window::surface().request_wake()`（出锁后调、一轮批量输入只唤醒一次），主线程在 `Application::set_on_frame` 回调里 `TerminalView::on_frame()` 排空自有队列并标脏，裁决 7.23ⓒ、架构 §3.2/§3.3；`utest_session.cpp` 锁住「有提交才唤醒、且一次」，真机 e2e 那条仍缺 |
| 按键 / 文本 → 编码 → 会话 → 连接 | 已打通（2026-10-02，`SPEC.FEAT.INTERACT.01` 首版；同日的 G14 / G15 / G16 回货已接线） | 框架事件派发 → `TerminalView::on_key_event` / `on_text_input` → `term::encode_key`（转义字节走 `Session::send_bytes`）或会话编码（字符走 `Session::send_text`）→ 连接。三层验收不重叠：`utest_keymap` 逐字节（11 例，含 keypad 两档）、`itest_key_input` 链路形状与 `DECCKM` 经真实状态机（8 例，含「可打印键不双发」「无焦点不着陆」「抬起不重发」）、`etest_key_forwarding` 真机 cmd.exe 的行编辑与命令提交（2 例）。**回货接线后**：`Tab` / `Shift+Tab` 经 `Widget::wants_tab_keys()` 落到控件、Alt 系随 syskey 合流进事件链、keypad 两档（`DECKPAM`/`DECKPNM` × NumLock）按 PuTTY xterm-funky 表有可发之键，派发侧 `TODO` 桩消除（裁决 7.36①②）。**余下**：主键盘 `Insert` 随 G20 回货（Win32 上该键在事件链里根本到不了）；IME 组合态就地绘制随 `SPEC.FEAT.INTERACT.06` 另立一棒。**键入的真机目视走查半结**（2026-10-02，裁决 7.31 / 7.36④）：走查抓出并修掉一个装配缺陷——视口从未获焦故真机上按键全哑（框架不在启动时派焦点，装配层须 `app.focus().set_focus(view)`；集成用例不经真实焦点序因而全绿），像素证人为「空心描边 → 闪烁实心块」；**键入手感、`Tab` / Alt 系 / 小键盘的对端接受度仍未结**（会话锁屏下 `SendInput` 静默失效，不可验，本轮不宣称「可用」，须解锁后由人目视） |
| 鼠标 → 选区 → 复制文本 | **已打通（2026-10-03，`SPEC.FEAT.INTERACT.02` 界面腿 + `SPEC.FEAT.INTERACT.03` 复制落地）** | 纯逻辑前置：`ui::row_spans` 把两个端点归一成逐行的闭开列区间（流式与列模式两态、单击不成选区、列端点按宽度截断），`ui::copy_text` 由区间取文本（LF 分隔、双宽延续格整字符纳入、越界行不产文本也不补空行、三项默认关闭的复制变换按「边框 → 行尾空白 → 续行」的固定次序，裁决 7.32）；`ui::cell_at_point` 把指针的逻辑 dp 折成 `ui::GridCellPos`（越界钳位而非丢事件，裁决 7.31 那棒的 CHANGELOG v0.30）；`ui::word_span_at` 出双击选词的区间、`ui::translate_selection_rows` 按 `grid::Storage::dropped_lines()` 折算漂移（裁决 7.39）。界面腿本体：`TerminalView::on_pointer_event` 覆写，Press 按框架 `click_count` 定粒度（逐格 / 按词 / 按行）、Move 推进端点、Release 收口并按 `copy_on_select` 置待复制标志，文本在同一次 `Session::read` 短临界区内取、出锁经 `session::ClipboardOutbox::write()` 落剪贴板（接缝改判见裁决 7.40①），高亮在色带层内替换底色并按 `min_contrast` 重合成前景，失焦态各半混合，Alt+拖拽取 `MouseEvent::modifiers`（G18 字段的第一个消费点）。验收：`tests/unit/utest_selection.cpp` 二十四例 + `tests/integration/itest_render_viewport.cpp` 的选区八例（无头像素差分，共 18 例全绿），两次变异自证见 7.40 验收段。**余下**：DECSTBM 带内滚动的位移补偿（7.39③ 的显式欠项）、Ctrl+滚轮缩放（随 `SPEC.FEAT.RENDER.02`）、三态与复制语义三项的 UI 入口（随 `SPEC.FEAT.PREF.02`）；**真机走查未做**（拖拽跟手、跨回看滚动、copy-on-select 是否真落系统剪贴板，以及右键菜单点选、多行警告呈现、逐行节流是否丢字），故本行只到「无头可证」 |
| 右键三态 → 菜单 / 粘贴 → 会话 | **已打通（2026-10-03，`SPEC.FEAT.INTERACT.03` 界面腿，裁决 7.41）** | 纯逻辑决策件 `ui::plan_right_click` 把「三态 × 有无选区」折成意图四值（`None / Copy / Paste / Menu`）与菜单项，`ContextMenu` 态无选区时「复制」置灰而非静默失败，`CopyOnSelect` 态无选区回 `None`（空写会覆盖用户剪贴板原有内容）。呈现侧全部由框架公共 API 组合：装配层场景根换 `au::OverlayHost`，菜单是 `au::Popup` 承载的一列 `au::Button`，多行警告是 `au::Dialog` + `aurora::confirm`；实测框架的 `Modifier::context_menu` / `MenuItem` 只有状态模型、`OverlayHost::handle_outside_click()` 全仓无调用点，故外部点击由本仓在指针入口自驱，按裁决 7.13② 留本仓、**不登记缺口**。剪贴板读方向补进 `session::ClipboardOutbox::read()`（全仓唯一触达系统剪贴板的 TU），读写与菜单动作一律落下一帧 `on_frame`（AGENTS.md §4.5 第 25 条）；多行先确认后发，计划扣在视口里且**无宿主即不粘贴**。逐块节流经 `Scheduler::set_timeout` 取累积延迟，无运行中调度器时按次序一次发完。验收：`tests/unit/utest_right_click.cpp` 八例 + `tests/integration/itest_right_click_paste.cpp` 十六例（真指针派发 + `Popup` 真实命中测试），四条变异自证含一处判据空洞的修补，见 7.41 验收段。**真机走查未做**：菜单点选、警告呈现与逐行节流是否丢字，故本行只到「无头可证」；三态的 UI 入口随 `SPEC.FEAT.PREF.02` |
| 标签列表 → 标签栏 widget → 会话 | **纯逻辑件已落、界面腿未开工（2026-10-03，`SPEC.FEAT.WS.01`，裁决 7.43）** | `ui::TabStrip` 给出顺序、当前选中格与每格该显示的名字（档案名 / `OSC 0/2` 标题 / 手动重命名三来源按 `appearance.tab_name_priority` 折算，空串即「该来源未设置」而逐级让位），`add` / `close` / `select_relative` / `move` 四个动作都不问绘制与会话。未打通的是它到真实窗口的那段：标签栏 widget 按 `tabs()` 铺栏位并显示折算结果、`OSC` 产物经会话取回后喂 `set_osc_title`、拖拽落点折成 `move` 的下标（基准是「其余标签」）、关闭按钮命中折成 `close`、**末格关闭 → 关窗口**（`SPEC.FEAT.WS.03`）那条路径、栏位溢出后的滚动，以及「关闭前确认（有运行中进程时）」——该判据属会话侧故不入本件；`SPEC.FEAT.WS.04` 的连接类型图标与活动角标同理留绘制侧。按「UI 编写前先出设计图评审」，开工前须先补标签栏与分屏的交互视觉稿 |
| 分屏树 → 几何 → 逐 pane 会话 | **纯逻辑件已落、界面腿未开工（2026-10-03，`SPEC.FEAT.WS.02`，裁决 7.42）** | `ui::PaneTree` 给出拓扑（谁与谁同层、每层沿哪个轴、各层各占多大一份）、`layout()` 给出 dp 矩形、`route_focus` 给出方向键的目标 pane；**树不含几何记忆**，故 resize / 切分 / 拖拽走同一条折算，且 `move_divider` 与 `layout` 共用算式（拖到边界是「顶住」而非越界，松手后比例不回弹）。未打通的是它到真实窗口的那段：分屏容器 widget 按矩形铺子控件、把手的命中与拖拽位移喂回 `move_divider`、焦点 pane 的描边、方向键折成 `PaneDirection`、每个矩形经 `ui::make_geometry` 现算行列并向对应 `Session::resize` 下发（`SPEC.FEAT.XFER.01` 的强耦合处）。`LayoutSpec::min_pane_dp` 的界面腿入参也在此处定（须按「最小行列数 × 格步长」而非库内 48 dp 下界）。按「UI 编写前先出设计图评审」，开工前须先补一版分屏交互视觉稿 |
| 网格行数变化 | 已定并落地（PTY 下发已接线，UI 侧来源未接） | 策略按裁决 7.17 收敛：移动窗口边界、底部锚定，历史自动收回或溢出；存储层 `Storage::set_rows`、状态机 `Terminal::resize` 与会话层 `Session::resize`（含连接侧下发与整屏脏通知）三层已串通并带单测，连接侧下发由 `platform::ConptyConnection::resize` 走 `ResizePseudoConsole`（真机 e2e 覆盖尺寸变更后流不中断）。**尺寸由 UI 侧何处来已接**（2026-10-02，`SPEC.FEAT.XFER.01` 的取值腿）：`TerminalView::on_layout` 按自身可视 dp 与 `ui::make_geometry` 派生行列，变化时在 `on_frame` 里下发 `Session::resize`，像素用例断言首帧只下发一次且行列数与几何一致；余下的是连续 resize 的**去抖合并**策略 |
| 宽度判定 | 已接线（框架真判定） | 状态机经注入的 `WidthPolicy` 取格数、自身不查表（架构 §6.3）；生产实现 `term::UnicodeWidthPolicy` 转调 Aurora `unicode_cell_width`（0 / 1 / 2 三档 + Ambiguous 入参，裁决 7.20），零宽码点并入网格行的组合标记侧表。验收证据：`tests/integration/itest_unicode_width.cpp` 以真实字节流跑全链路，断言 CJK 双宽占位与延续格、同一份含 Ambiguous 输出在两种口径下的列位一致、行末整体换行不留半格、combining 不占格不推进光标。`SingleWidthPolicy` 退为测试用常数注入值（不受 Unicode 版本影响的用例），桩判定仍用于状态机自身的机制类单测。**框架侧热路径已随 G19 闭合**（2026-10-02 回货）：`unicode_cell_width` 内部把「对三张区间表各一次二分」换成「与由表首项导出的单宽下界比一次」，判定结果逐码点不变（全码点穷举等值是它的验收），本仓消费形态不变（仍逐码点转调、不持宽度表的复制品），写侧纯链观测由 25.9 上移到约 41 MB/s |

**验证现状**（2026-10-03 复核）：MSVC + Ninja 全量构建通过；非 e2e 的 CTest 全绿共 27 项——解析器 / UTF-8 / 字符集 / 网格存储（含组合标记侧表）/ 终端状态机 / OSC 消费 / 宽度判定口径 / 背压队列 / 会话（含帧唤醒五例）/ 调色板合成 / 整格几何、run 切分与落点换算 / 预置配色表 / 配置装载与降级 / 主线程可见区副本 / 键盘映射编码表 / 选区归一与选中文本 / 粘贴处置计划 / 右键三态处置 / 分屏 pane 树的拓扑折算与方向键路由 / 标签列表的顺序、选中与三名折算共二十个单元用例，加「解码→解析」「全链路回放首帧」「宽度判定端到端（含 Ambiguous 双口径与 combining）」「视口绘制与选区高亮的像素差分（十八例）」「键入链路形状与 `DECCKM` / `DECKPAM` 经真实状态机（八例）」「右键三态与菜单 / 粘贴排期的帧边界（十六例，真指针派发 + `Popup` 真实命中测试）」六个集成用例与框架自检。像素判据只在 `AURORA_BACKEND_HEADLESS` 的 `HeadlessSurface` 帧缓冲上成立，故它验的是绘制序列与几何，不是真机合成器路径。e2e 须在有窗口站/桌面的交互会话里跑（裁决 7.19⑤）：`etest_local_terminal` 绿（Windows 侧 ConPTY 全链路），`etest_key_forwarding` 绿（真机 cmd.exe 的键入提交与 DEL 行编辑，两例各 77ms），`etest_osc_clipboard` 本机停在 `OpenClipboard` 返 5（系统级剪贴板占用，同机 PowerShell 亦如此，判定见 `CHANGELOG.md` v0.20 / v0.21），宿主须能访问窗口站与桌面，无控制台的受限上下文里子进程的伪终端初始化会失败，该环境下的失败不代表实现有误。**真机走查已开一轮、半结**（2026-10-02，裁决 7.31）：可验面＝尺寸变更与渲染覆盖（WGC 截屏 + 标题栏按钮的 UIA `Invoke` 走启动 / 最大化 / 还原 / 关闭），不可验面＝键入与光标跟手（会话锁屏下 `SetForegroundWindow` / `SendInput` 全部静默失效，而**不用注入消息冒充真实点击**）。该轮抓出并修掉装配层的初始焦点缺失（框架不在启动时派焦点，`src/main.cpp` 须显式 `set_focus(view)`；集成用例不经真实焦点序故抓不到），并登记 **G17**：144 DPI 机器上 `ctx.scale_factor` 恒 `1.0` 而窗口逻辑尺寸随路径给出三个互不相容值，最大化后网格只覆盖客户区约 `1/1.5`。**G17 已于同日日终闭合并真机复验**：框架侧收敛为单一缩放真值源（其两处 known-scale-gap SKIP 改为真断言），本仓三态截屏均铺满客户区、实测格宽 15.3 物理 px = 10.2 dp @150%（裁决 7.36④），「尺寸 / 覆盖」腿由此可判。**复验再撞 G21**——建窗时刻的 DPI 读数仍为 `1.0`，960 dp 的请求实得 655 dp 窗口，故首屏行列数比按 dp 换算的期望少约 `1/1.5`，「连续 resize 不错位」那条目视判据在 G21 回货前判不了。**仍未由人验证**：M1 出口判据中的「替换 Windows Terminal 日常使用」「窗口连续 resize 后 `vim`/`tmux`/`htop` 不错位」与**键入的手感目视**（光标跟手、`Tab` / Alt 系 / 小键盘的对端接受度、CJK 上屏观感、滚动回看手感）；键盘链路已有真机自动化证据（字节被对端正确解释），但真机窗口内的手感与 TUI 应用的实况错位还要人判。装配层已把会话与视口控件接到窗口上并派好焦点，键盘与文本入口已通，`SPEC.FEAT.INTERACT.02` 的选择与复制已通（2026-10-03，裁决 7.40），`SPEC.FEAT.INTERACT.03` 的右键三态与粘贴呈现亦已通（同日，裁决 7.41），但其**真机走查未做**——拖拽跟手、跨回看滚动的选区跟随、copy-on-select 是否真落系统剪贴板三条只有无头像素与文本断言可证，右键菜单的点选、多行粘贴警告的呈现与逐行节流是否丢字同样只到「无头可证」。

**待裁决**：`codespec/ARCHITECTURE.md` §16 的 A（会话抽象粒度）已于 2026-09-30 拍板为「基础接口 + 能力接口组合」并写入该文档 §7.2（同时作为裁决 7.18 记入需求侧）；D（网格所有权与主线程读取方式）已于 2026-09-30 收敛为「后台权威 + 主线程增量快照脏 cell」并写入该文档 §3.4，终端状态机与会话层据此落地，整屏位移以「整屏脏」标记通知副本重建；B（视口是否复用框架 `Scroll`）已于 2026-10-01 拍板为**自管**（裁决 7.23①，该文档 §9.5）。余下 E（单实例转交通道）待 `SPEC.FEAT.INTEG.04` 开工时拍板；F（配置 schema 迁移链）仍未决，但首版行为已由裁决 7.26③ 定死——`schema_version` 高于本仓支持时不做迁移，直接备份 + 回落默认。视觉稿的 `U1~U5` 与四屏草图的 `N1~N8` 已于 2026-10-02 全部收口（裁决 7.25），`src/ui/` 的实现约束解除。
