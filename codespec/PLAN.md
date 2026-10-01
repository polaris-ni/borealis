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
| **M0 框架补强** | Aurora 侧前置：框架缺口 G1（**已闭合，2026-10-01**）、G2（见 SPECIFICATIONS.md 附录 A.2） | — | 网格/批量文本绘制原语、等宽整像素 cell 度量、East Asian Width 宽度判定、多击语义均以公共 API 进框架并带单测；网格吞吐基准入框架 `tools/bench`（G1 已按此交付两行观测项）。原「并挂性能回归门禁（劣化 >10% 即 FAIL）」经实测改判：批量绘制的时间收益落在环境抖动内、无可锁阈值，守门改由像素级「整批 vs 逐片段全像素差分为 0」的用例承担（裁决 7.22③）；余下未落地项为 **G2 与 G13**（批量入口的排版选项透传），连同观察池里的 G9 / G11，四条的补全分工与实测复核结论见裁决 7.24 |
| **M1 单终端 MVP（Windows）** | 多标签 + 任意分屏的本地终端 | `SPEC.FEAT.TERM.01–05`、`SPEC.FEAT.TERM.08`、`SPEC.FEAT.RENDER.01–04`、`SPEC.FEAT.INTERACT.01`、`SPEC.FEAT.INTERACT.02`（流式拖拽/矩形块基本面）、`SPEC.FEAT.INTERACT.03`、`SPEC.FEAT.INTERACT.06`、`SPEC.FEAT.WS.01–02`、`SPEC.FEAT.XFER.01`、`SPEC.FEAT.CONN.01`、`SPEC.FEAT.PREF.03`、`SPEC.FEAT.PREF.06`、`SPEC.FEAT.PREF.01` 的预置配色腿、`SPEC.FEAT.PREF.07` 的损坏降级腿（后两条的提前由裁决 7.26②④ 定）、`SPEC.NF.PERF.01`、`SPEC.NF.PERF.02`、`SPEC.NF.PERF.06`、`SPEC.NF.PLAT.01`（仅 Windows 侧） | 本人日常替换 Windows Terminal 本地会话；多标签与任意方向/深度/比例/pane 数的分屏可用；**中文可输入可显示**（输入法与 CJK 字形回退）；窗口连续 resize 后 TUI 不错位；高频输出不卡死、内存不无界增长；VT 序列回放夹具进 CI 且真机手工签收通过 |
| **M2 工作区打磨 + Linux 等价** | 搜索/选择/设置/面板/分发，并补齐 Linux | `SPEC.FEAT.TERM.06–07`、`SPEC.FEAT.INTERACT.02`（双击/三击等其余）、`SPEC.FEAT.INTERACT.04`、`SPEC.FEAT.RENDER.05`、`SPEC.FEAT.WS.04–07`（`SPEC.FEAT.WS.05` 的 SSH 重连腿随 M3）、`SPEC.FEAT.WS.10`、`SPEC.FEAT.PREF.01`（色值表已随配置层提前，本阶段做主题切换入口与亮/暗 chrome 适配）、`SPEC.FEAT.PREF.04`、`SPEC.FEAT.PREF.07` 的快照回滚与本地导出导入（损坏降级腿已随配置层提前）、`SPEC.NF.PERF.03–05`、`SPEC.NF.RELI.01`、`SPEC.NF.PKG.01`、`SPEC.NF.PLAT.01`（Linux 等价补齐） | 搜索 + 文本选择 + 主题 + 命令面板全流程；性能/内存/空闲 CPU 指标达标；配置损坏可降级启动；产出可分发安装包；Linux 侧同批功能与指标达标（含缩放上报腿与 PTY/默认 shell 探测） |
| **M3 远程连接** | SSH + 档案 + SFTP + 凭据安全 | `SPEC.FEAT.CONN.02–04`、`SPEC.FEAT.CONN.07–10`、`SPEC.FEAT.TERM.09`、`SPEC.FEAT.WS.05`（SSH 断线重连腿） | SSH 日常运维全流程（密钥/agent/known_hosts/重连/隧道）；非 UTF-8 输出正确显示；配置目录明文凭据审计（自动化用例）通过 |
| **M4 扩展连接与集成打磨** | 串口/Telnet/日志/系统集成 | `SPEC.FEAT.INTERACT.05`、`SPEC.FEAT.WS.03`、`SPEC.FEAT.WS.08–09`、`SPEC.FEAT.WS.11`、`SPEC.FEAT.CONN.05–06`、`SPEC.FEAT.CONN.11–12`、`SPEC.FEAT.PREF.05`、`SPEC.FEAT.INTEG.01–02`、`SPEC.FEAT.INTEG.04` | 嵌入式串口调试场景可用（含发送行尾序列配置、**中文按 GB18030 编码发送**）；重启恢复工作区；OSC 133 / OSC 7 集成可用；剪贴板访问授权三态生效 |
| **M5 观察池** | 依赖框架缺口或低频 | 见 §5（未排期项）与 §6（未关闭缺口） | 按缺口关闭情况与真实使用频率择期排期，不设固定时间 |

**并行与预研**：M0 与 M1 可部分并行（渲染原语先行，PTY 与解析器同步开发）；SSH 传输栈与 M1/M2 的 UI 无耦合，可提前预研（裁决 7.2）。

**M0 → M1 的阻塞粒度**（2026-10-01 口径，供开工排序用）：M0 不是 M1 的整体前置。**G1 三腿已全部闭合（2026-10-01 实测）**：`SPEC.FEAT.RENDER.01` `SPEC.FEAT.RENDER.03` `SPEC.FEAT.RENDER.05` 与 `SPEC.FEAT.TERM.08` 的框架前置就此解除（宽度判定腿的判据见裁决 7.20，绘制两腿见裁决 7.22；原缺口形态与闭合过程的完整表述保留在附录 A.2 的 G1 行）。余下 **G2 阻塞 `SPEC.FEAT.INTERACT.02` 的双击/三击腿**；G13（批量入口的排版选项透传）**不构成第一棒的阻塞**——斜体与字距走逐片段 `Painter::draw_text` 的带 opts 重载即可交付，框架侧透传落地后撤销该分流（裁决 7.24④）。M1 的其余项（终端逻辑层、键盘映射、UI 容器、PTY 与尺寸同步、配置）不依赖框架缺口，可在 M0 未关闭期间推进。

**M1 渲染腿的棒内序**（2026-10-01 裁决 7.23④）：**第一棒＝上屏主路径**——`SPEC.FEAT.RENDER.01`（等宽网格 + 脏行重绘 + 视口自管）、`SPEC.FEAT.RENDER.03`（SGR 属性合成，含 bold-is-bright 与最小对比度）、`SPEC.FEAT.RENDER.04`（光标三形态与闪烁），交付到「本地会话输出看得见、光标跟手」即止；`SPEC.NF.PERF.02` 的吞吐基准随第一棒建，**并挂时间门禁**（劣化 >10% 即 FAIL，与裁决 7.22③ 的改判不冲突：那里锁的是框架批量入口的相对收益，这里锁的是本仓上屏层的每帧成本）。**第二棒＝`SPEC.FEAT.RENDER.02` 字体族**（系统枚举 + 内置 Cascadia + CJK 缺字回退链）与缩放变更后的度量/字形缓存重建，因第一棒取系统等宽字体即可上屏、回退链的验收只在含汉字会话里才成立。文本选择（`SPEC.FEAT.INTERACT.02`）、键盘映射（`SPEC.FEAT.INTERACT.01`）与尺寸去抖（`SPEC.FEAT.XFER.01` 的 UI 侧来源）属交互层，不在渲染两棒内。

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
| Quick Terminal（quake 下拉终端） | `SPEC.FEAT.WS.12`、框架缺口 G9 | 无 OS 级全局热键注册，且缺无边框贴边窗口形态；两者均为硬依赖 |
| 系统通知（OSC 9 / 777 转 toast） | `SPEC.FEAT.INTEG.03`、框架缺口 G11 | 无系统通知 API；框架内 Toast 是应用内控件，不满足系统级提醒 |
| 无障碍基线 | `SPEC.NF.A11Y.01` | 依赖 Aurora 无障碍平台桥（Aurora 主仓 `codespec/ARCHITECTURE.md` §8.5）落地；语义树与 UIA/AT-SPI2 桥已部分具备 |
| scrollback reflow | `SPEC.FEAT.TERM.04`、裁决 7.5 | 默认不 reflow，与超长行截断（裁决 7.4）配套；重开需先解决历史行内存模型 |
| 字体连字 | `SPEC.FEAT.RENDER.02` | 终端等宽语义下收益低，且与整像素 cell 度量冲突 |
| kitty keyboard protocol / `modifyOtherKeys` | `SPEC.FEAT.INTERACT.01` | 现代协议增量；先由基础组合键转发覆盖 |
| Emoji 精确对齐 | `SPEC.FEAT.TERM.08` | 位图混排与变体选择处理成本高，基础呈现已可用 |
| 终端内图像协议（Sixel / kitty graphics / imgcat） | SPECIFICATIONS.md §2.2 裁剪表 | 依赖位图混排渲染路径，工作量与收益不成比 |
| Zmodem 文件传输 | SPECIFICATIONS.md §2.2 裁剪表 | SFTP 覆盖主流场景 |
| 触摸选择与滚轮手势 | 框架缺口 G6 | 框架手势层齐备，win32/glfw/wayland 无触摸采集实现 |
| macOS 平台 | `SPEC.NF.PLAT.01`、框架缺口 G8 | `MacOSSurface` 仅骨架 |

---

## 6 框架缺口补入排期

| 缺口 | 内容 | 补入时点 |
|:---|:---|:---|
| G1 | ~~批量文本绘制原语 + 等宽整像素 cell 度量 + East Asian Width 宽度判定~~ **已闭合（2026-10-01）**：三腿均以 Aurora 公共 API + 单测 + 契约文档交付——`unicode_cell_width`（裁决 7.20）、`Painter::draw_text_runs` 与 `render::FontEngine::monospace_cell` → `render::CellMetrics`（裁决 7.22） | ~~M0（阻塞 `SPEC.FEAT.RENDER.01` `SPEC.FEAT.RENDER.03` `SPEC.FEAT.RENDER.05`）~~ 阻塞解除；渲染层可开工，形态边界见架构 §9.2 |
| G2 | `MouseEvent` 多击语义（框架统一自算）。**2026-10-01 复核仍开放**，补全任务已在 Aurora 侧派发（裁决 7.24） | M0（阻塞 `SPEC.FEAT.INTERACT.02` 的双击/三击腿，属 M2；该条的 M1 腿是流式拖拽/矩形块基本面，不受阻） |
| G13 | 批量文本入口 `Painter::draw_text_runs` 不透出 `TextLayoutOpts` / `TextAAMode`（2026-10-01 实测新增；斜体能力本身在框架已具备，见裁决 7.24②）。补全任务已在 Aurora 侧派发 | **不阻塞** M1 第一棒：斜体与字距走逐片段 `Painter::draw_text` 的带 opts 重载（裁决 7.24④），框架侧透传落地后以一次改动撤销分流并复跑像素回归 |
| G12 残留单腿 | X11 / Wayland / GLFW 的 DPI 缩放变化上报（公共 API 与 Win32 已具备） | M2 的 Linux 等价补齐 |
| G9 / G11 | OS 级全局热键、系统通知 API（2026-10-01 复核仍开放，补全任务已在 Aurora 侧派发；G9 原列的「应用侧直调平台 API」候选作废，见裁决 7.24①） | 观察池（§5），触发条件为 Quick Terminal 或系统通知开工 |
| G10 | ~~无音频播放 API~~ 已关闭（WASAPI/ALSA 后端具备），无须补框架 | — |

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
| **M0 框架补强** | 仅剩 G2 / G13（均已派发 Aurora 侧补全） | **G1 已整体闭合（2026-10-01）**：三腿均在 Aurora 主仓以公共 API 落地并带单测与契约文档（`unicode_cell_width`，裁决 7.20；`Painter::draw_text_runs` 与 `FontEngine::monospace_cell`，裁决 7.22）。余下 **G2**（`MouseEvent` 多击语义，按 §2 的阻塞粒度仅阻 `SPEC.FEAT.INTERACT.02` 的双击/三击腿）与 **G13**（批量入口的排版选项透传，不阻塞第一棒）未落地，二者连同 G9 / G11 的补全分工见裁决 7.24；G1 / G9 / G11 均不构成 M1 的整体前置 |
| **M1 单终端 MVP（Windows）** | 部分落地 | 已落地（纯逻辑层均可脱离 UI 独立单测；平台层为 Windows 侧真机 e2e）：`SPEC.FEAT.TERM.01` 的 VT 解析器与字符集切换映射、`SPEC.FEAT.TERM.09` 的 UTF-8 双向编解码（M1 基线腿）、`SPEC.FEAT.TERM.04` 的 scrollback 环形存储、**终端状态机**（`SPEC.FEAT.TERM.01` 的 Print/Execute/ESC/CSI 显示内核与私有模式登记、`SPEC.FEAT.TERM.02` 的 16/256/真彩色、`SPEC.FEAT.TERM.03` 的主备屏、`SPEC.FEAT.TERM.05` 的光标与滚动区域/擦除/插删/制表位；`SPEC.FEAT.TERM.08` 的宽度与双宽占位、combining 并入腿（判定表随 G1 第三腿闭合））、**会话层**（架构 §7.2 粒度拍板后的 `session::Connection` 基础接口与 `ConnectionEvents` 反向通道、`session::Session` 组合解码/状态机/网格与写通道、`session::DamageQueue` 的有界背压队列：`SPEC.NF.PERF.06` 的合并而非丢弃与单帧预算让出、`SPEC.NF.RELI.01` 的水位/合并/让出计数、`SPEC.FEAT.TERM.01` 的 DA1/DSR 应答回写、`SPEC.FEAT.XFER.01` 的尺寸下发腿）、**`SPEC.FEAT.CONN.01` 的本地终端 Windows 腿**（`conn::LocalTerminalSpec` + 连接工厂、`platform::ConptyConnection` 的伪终端创建/挂载/读写/关停、默认 shell 探测链、PTY 环境注入与 profile 覆盖，实现口径见裁决 7.19）、**OSC 消费腿**（`SPEC.FEAT.TERM.07` 的状态机侧消费提前落地，裁决 7.21：`0/2` 标题、`7` 目录原文、`8` 超链接区间与有界链接表、`52` 写方向经主线程落系统剪贴板、`133` 边界与退出码，未识别命令号计入留痕；`OSC 7` / `133` 只是 `SPEC.FEAT.INTEG.01/02` 的来源预埋，其消费行为仍留 M4）、**配置层**（`SPEC.FEAT.PREF.03` + `SPEC.FEAT.PREF.06` 的 schema 与落盘、`SPEC.FEAT.PREF.01` 的 ≥8 套预置色值表、`SPEC.FEAT.PREF.07` 的损坏降级线（备份 + 回落默认 + `LoadReport` 交 UI），按裁决 7.26 建全量四分类、按裁决 7.27 适配框架的点号路径模型；**消费方尚未落地**：设置面板 `SPEC.FEAT.PREF.02` 与降级对话框 `TODO(SPEC.FEAT.PREF.07)` 见下）。未开工：`SPEC.FEAT.CONN.01` 的 POSIX 腿（`$SHELL` 探测 + `forkpty` 同口径）、`SPEC.FEAT.RENDER.01–04` 的**绘制侧**（框架前置已随 G1 闭合解除，可直接消费 `Painter::draw_text_runs` 与 `FontEngine::monospace_cell`，见架构 §9.2 与裁决 7.22；其中颜色合成与几何/run 切分两块纯逻辑已按裁决 7.23② 落地为 `ui::palette` 与 `ui::cell_layout`，各带单元用例，未落的是 `src/ui/terminal_view.cpp` 与帧唤醒（`SPEC.FEAT.RENDER.04` 光标三形态所需的 DECSCUSR 状态机侧已于本批落地，形态绘制随该棒接））、`SPEC.FEAT.WS.01` `SPEC.FEAT.WS.02`、`SPEC.FEAT.INTERACT.01`、`SPEC.FEAT.XFER.01` 的去抖与 UI 侧取值（行列数的算式已在 `ui::make_geometry`） |
| **M2–M5** | 未开工 | 排期不变 |

**M1 待接接缝**（已落地模块之间尚未打通处；表中顺序即建议的开工顺序）：

| 接缝 | 现状 | 说明 |
|:---|:---|:---|
| 解码 → 解析 → 状态机 → 网格 | 已打通 | 集成用例 `tests/integration/itest_terminal_scene.cpp` 以真实形态的首帧输出驱动全链路（字节流 → 解码 → 解析 → 网格内容），断言落在框线码点、SGR 色值与来源、OSC 吞掉、主备屏互不污染。**`ESC(0` 的框线效果就此有端到端证据**（`SPEC.FEAT.TERM.01` 验收判据腿） |
| 状态机 → 会话回写 | 已打通 | 终端属性与光标位置查询（DA1、DSR）经会话写通道回写：状态机经注入的 `term::ResponseSink` 在锁内登记应答、会话出锁后编码写入连接（架构 §3.4、§5.2，裁决 7.18③ 定下只报档位 62）。单元用例 `tests/unit/utest_session.cpp` 以 `Connection` 替身驱动全链路并断言回写字节。OSC 各命令的消费见下一行 |
| OSC 消费 → 会话产物 | 已打通（`SPEC.FEAT.TERM.07` 提前落地） | 状态机把 `OSC 0/2/7/8/52/133` 消费成结构化产物（`term::OscState` 快照 + 有界链接表 + `take_clipboard_write()` 取走语义，形态与理由见裁决 7.21），会话侧以加锁访问器透出；`OSC 52` 写方向由 `session::ClipboardOutbox::drain()` 在主线程转调 Aurora `Clipboard::set_text` 真落系统剪贴板，读方向 `52;c;?` 回写空响应。单测 `tests/unit/utest_terminal_osc.cpp` 定点驱动每条命令，e2e `tests/e2e/etest_osc_clipboard.cpp` 用真机 ConPTY + powershell 发出 OSC 52 并回读剪贴板。余下的是消费方：标题上标签名/窗口标题栏、超链接的 hover 与点击、133 的命令块区间 |
| 会话 → 平台连接 | 已打通（Windows 腿） | `conn::make_local_terminal_connection` 造出 `platform::ConptyConnection` 并交予 `session::Session`，读线程把伪终端管道的原始字节喂进「解码 → 解析 → 状态机 → 网格」整条链；端到端证据是 `tests/e2e/etest_local_terminal.cpp`（真实 shell 的输出上屏、键入往返、突发输出的队列水位、关闭后进程终结且内容保留）。四处 Win32 实现口径见裁决 7.19。余下的是帧调度通知（提交入队后由提交侧 `Window::surface().request_wake()` 唤醒主线程、在 `Application::set_on_frame` 里排空自有队列，裁决 7.23ⓒ，架构 §3.2），随上屏层接，当前消费方按帧轮询 `has_damage()` |
| 网格行数变化 | 已定并落地（PTY 下发已接线，UI 侧来源未接） | 策略按裁决 7.17 收敛：移动窗口边界、底部锚定，历史自动收回或溢出；存储层 `Storage::set_rows`、状态机 `Terminal::resize` 与会话层 `Session::resize`（含连接侧下发与整屏脏通知）三层已串通并带单测，连接侧下发由 `platform::ConptyConnection::resize` 走 `ResizePseudoConsole`（真机 e2e 覆盖尺寸变更后流不中断）。余下的是**尺寸由 UI 侧何处来**与去抖合并（`SPEC.FEAT.XFER.01`），随渲染层那一棒接 |
| 宽度判定 | 已接线（框架真判定） | 状态机经注入的 `WidthPolicy` 取格数、自身不查表（架构 §6.3）；生产实现 `term::UnicodeWidthPolicy` 转调 Aurora `unicode_cell_width`（0 / 1 / 2 三档 + Ambiguous 入参，裁决 7.20），零宽码点并入网格行的组合标记侧表。验收证据：`tests/integration/itest_unicode_width.cpp` 以真实字节流跑全链路，断言 CJK 双宽占位与延续格、同一份含 Ambiguous 输出在两种口径下的列位一致、行末整体换行不留半格、combining 不占格不推进光标。`SingleWidthPolicy` 退为测试用常数注入值（不受 Unicode 版本影响的用例），桩判定仍用于状态机自身的机制类单测 |

**验证现状**（2026-10-02）：MSVC + Ninja 全量构建通过；非 e2e 的 CTest 全绿共 17 项——解析器 / UTF-8 / 字符集 / 网格存储（含组合标记侧表）/ 终端状态机 / OSC 消费 / 宽度判定口径 / 背压队列 / 会话 / 调色板合成 / 整格几何与 run 切分 / 预置配色表 / 配置装载与降级十三个单元用例，加「解码→解析」「全链路回放首帧」「宽度判定端到端（含 Ambiguous 双口径与 combining）」三个集成用例与框架自检。e2e 两项须在有窗口站/桌面的交互会话里跑（裁决 7.19⑤）：`etest_local_terminal` 绿（Windows 侧 ConPTY 全链路），`etest_osc_clipboard` 本机停在 `OpenClipboard` 返 5（系统级剪贴板占用，同机 PowerShell 亦如此，判定见 `CHANGELOG.md` v0.20 / v0.21），宿主须能访问窗口站与桌面，无控制台的受限上下文里子进程的伪终端初始化会失败，该环境下的失败不代表实现有误。**仍未真机走查、无像素级验收**：M1 出口判据中的「替换 Windows Terminal 日常使用」「窗口连续 resize 后 `vim`/`tmux`/`htop` 不错位」均未验证——二者的前置（渲染层与 UI 侧尺寸来源）未落地，字节链路虽已由真实 PTY 驱动，但没有上屏路径就没有用户可见的行为。

**待裁决**：`codespec/ARCHITECTURE.md` §16 的 A（会话抽象粒度）已于 2026-09-30 拍板为「基础接口 + 能力接口组合」并写入该文档 §7.2（同时作为裁决 7.18 记入需求侧）；D（网格所有权与主线程读取方式）已于 2026-09-30 收敛为「后台权威 + 主线程增量快照脏 cell」并写入该文档 §3.4，终端状态机与会话层据此落地，整屏位移以「整屏脏」标记通知副本重建；B（视口是否复用框架 `Scroll`）已于 2026-10-01 拍板为**自管**（裁决 7.23①，该文档 §9.5）。余下 E（单实例转交通道）待 `SPEC.FEAT.INTEG.04` 开工时拍板；F（配置 schema 迁移链）仍未决，但首版行为已由裁决 7.26③ 定死——`schema_version` 高于本仓支持时不做迁移，直接备份 + 回落默认。视觉稿的 `U1~U5` 与四屏草图的 `N1~N8` 已于 2026-10-02 全部收口（裁决 7.25），`src/ui/` 的实现约束解除。
