# AGENTS.md — Borealis 项目结构与协作规范

> 本文件是 **AI 编码助手（及人类协作者）访问 Borealis 项目的单一入口**。
> 改代码、写文档、理解设计之前先读本文件，再按 §3 的导航表定位 `codespec/` 下的具体文档。
> 规则来源：Aurora 主仓 `AGENTS.md` 的硬规则（本仓是其消费者，凡与框架交互处沿用同口径）+ 业界通用协作与提交实践。

---

## 1 项目定位（一句话）

**Borealis** 是基于 **Aurora**（C++20 跨平台 AI-first GUI 库）开发的跨平台终端管理软件：本地终端 / SSH / 串口 / Telnet 四类连接 + 多标签与任意分屏工作区。本仓是 **消费者应用**，不是库。

- 与 Aurora 的接入方式：`add_subdirectory(../aurora)` 走源码树；本仓自有三方依赖经 `find_package` + vcpkg 获取（裁决 7.12），**不把 Aurora 源码复制进本仓**。
- 命名：仓内一切可自主命名的标识统一 `borealis`（命名空间 `borealis::vt` / `borealis::term` / `borealis::session` 等、CMake project 与 target `borealis`、产物 `Borealis`、工作区目录名亦已统一）。裁决 7.14 曾把目录名列为例外，该例外随 2026-09-29 的改名已失效并删除。
- 工具链：MSVC + Ninja + Win32 后端；构建命令一律最大化并行线程（Ninja 默认满核，CMake 用 `--parallel`，MSBuild 用 `/m`，Make 用 `-j%NUMBER_OF_PROCESSORS%`）。

---

## 2 目录布局（约定）

下表混合了**已落地路径**与**规划路径**，以「现状」列为准；引用标着「计划 / 待建」的路径处不得写成既存事实。

| 路径 | 作用 | 现状 |
|:---|:---|:---|
| `codespec/` | 全部项目文档（需求 / 计划 / 变更历史 / 架构） | 已有（四份：`SPECIFICATIONS.md` `PLAN.md` `CHANGELOG.md` `ARCHITECTURE.md`） |
| `CMakeLists.txt` `CMakePresets.json` `cmake/` | 构建编排与模块 | 已有：`CMakeLists.txt`（消费 Aurora 源码树 + `add_subdirectory(src)` + `include(cmake/BorealisTests.cmake)`）、`CMakePresets.json`（单一 `msvc` 预设：Ninja + MSVC，构建目录 `build/`）、`cmake/BorealisTests.cmake`（注册式 runner，每条 CTest = `--run=<stem>`，另有一条 `framework_selftest`） |
| `include/borealis/` | 本仓公共头（按模块域分目录：`vt` `term` `grid` `session` `conn` `ui` `config`） | 部分落地：`vt/`（`sequence.h` 语义单元结构、`parser.h` 表驱动解析器）、`term/`（`utf8.h` 编解码、`charset.h` 字符集映射、`terminal.h` 终端状态机与 `ResponseSink` 应答接缝、`width.h` 宽度判定接缝与生产实现声明、`osc.h` OSC 消费产物形态）、`grid/`（`cell.h` `row.h` 含零宽标记与超链接两张侧表 `storage.h` 环形存储）、`session/`（`connection.h` 连接基础接口与事件通道、`damage_queue.h` 有界背压队列、`session.h` 会话组合与跨线程协调点、`clipboard_outbox.h` OSC 52 剪贴板落地件声明）、`conn/`（`local_terminal.h` 本地终端启动规格、PTY 环境注入默认值、默认 shell 探测与连接工厂——只有标准类型，Win32 知识在其实现内）、`ui/`（`palette.h` 调色板与 SGR 颜色合成、`cell_layout.h` 整格几何与行内 run 切分，两者刻意不含 Aurora 类型）；`ui` 域的 `terminal_view.h` 与 `config` 域计划 / 待建 |
| `src/` | 实现；平台相关实现落在 `src/platform/{win,posix}/` | 已有：`src/CMakeLists.txt` + `src/main.cpp`（空壳应用）+ `src/vt/parser.cpp` `src/term/utf8.cpp` `src/term/charset.cpp` `src/term/width.cpp`（转调框架判定原语）`src/term/terminal.cpp` `src/grid/storage.cpp` `src/session/damage_queue.cpp` `src/session/session.cpp` `src/session/clipboard_outbox.cpp`（全仓唯一触达 Aurora `Clipboard` 的翻译单元）`src/term/osc.{h,cpp}`（OSC 字段切分与 base64 载荷解码，内部件、不进公共头）、`src/ui/palette.cpp`（颜色合成，含 bold-is-bright 与最小对比度）`src/ui/cell_layout.cpp`（像素格度量 ÷ scale 的 dp 换算与按样式全等的 run 切分）（编入 `borealis_core` 静态库）、`src/platform/win/`（`conpty_connection.{h,cpp}` 伪终端连接、`local_terminal.cpp` shell 探测与工厂、`win_text.{h,cpp}` UTF-8↔UTF-16；经 `if (WIN32)` 增编入同一库）；`src/platform/posix/`（`SPEC.FEAT.CONN.01` 的 POSIX 腿）计划 / 待建 |
| `tests/` | `framework/`（测试框架）+ `support/`（公共设施）+ `unit/utest_*` + `integration/itest_*` + `e2e/etest_*` + `fixtures/`（转义序列回放夹具） | 已有：`tests/framework/`（复刻 Aurora 注册式测试框架源码）、`tests/support/`（`paths.h` 仓库根拼接、`fixture_text.h` 夹具转义与读取、`terminal_feed.h` 字节→解码→状态机的链路接线）、`tests/unit/`（解析器 / UTF-8 / 字符集 / 网格存储 / 终端状态机 / OSC 消费 / 宽度判定口径 / 背压队列 / 会话 / 调色板合成 / 整格几何与 run 切分十一个用例）、`tests/integration/`（`itest_decode_parse.cpp`、`itest_terminal_scene.cpp` 全链路首帧回放、`itest_unicode_width.cpp` 宽度与 combining 的全链路验收）、`tests/e2e/`（`etest_local_terminal.cpp` 真实 ConPTY 驱动的本地终端用例、`etest_osc_clipboard.cpp` 真机 `OSC 52` → 系统剪贴板的回读腿，均须在有窗口站/桌面的交互会话里跑，见裁决 7.19⑤）、`tests/fixtures/`（`vt/parser_cases.tsv`、`term/utf8_cases.tsv`、`vt/scene_tmux_frame.txt`） |
| `assets/` | 内置字体（Cascadia Code，OFL）与图标；随包分发的许可声明 | 计划 / 待建（目录尚未创建） |
| `tools/` | 基准、门禁与校验脚本 | 仅有 `tools/msvc_env.bat`（VS 开发者环境包装，供 MSVC 通道的配置与构建使用）；基准与门禁脚本计划 / 待建 |

> 纯逻辑用例（`utest_*` / `itest_*`）可在沙箱内直接跑；`etest_local_terminal` 依赖真实 ConPTY，必须由 `cmd //c "start /min <bat>"` 投放到交互桌面会话再读回日志（裁决 7.19⑤）。

---

## 3 文档导航

| 你想了解 | 读这个文件 | 权威性 |
|:---|:---|:---|
| **需求本身**（做什么、做到什么程度、验收判据） | `codespec/SPECIFICATIONS.md` | 🥇 需求以它为准；本文 §4.2 解释其标识规范 |
| **优先级、交付分期、观察池、延后子项** | `codespec/PLAN.md` | 🥇 唯一的分期来源；需求文档里找不到「何时做」就来这里找 |
| **版本演进、旧需求编号 → 新标识映射** | `codespec/CHANGELOG.md` | 历史记录，条目按当时口径原文照录 |
| **架构 / 模块划分 / 线程与数据流 / 平台层边界** | `codespec/ARCHITECTURE.md` | 🥇 架构以它为准（2026-09-30 评审通过，由 `ARCHITECTURE.draft.md` 提升）；待定决策见其 §16 |
| **框架侧能力、缺口、编码规则** | Aurora 主仓 `AGENTS.md` 与其 `codespec/` | 跨仓改动 Aurora 前必读 |

引用文档一律「仓库相对路径 + 章节号」（如 `codespec/SPECIFICATIONS.md` §1.4），**不写 `file:line` 行号锚点**（会随改动漂移），**不引用只存在于本机的文件**。

---

## 4 硬规则

### 4.1 任务边界与开工纪律

1. **需求先行**：动手改某模块前，先读该条需求全文（含其「延后子项」标注）与其在 `PLAN.md` 的分期；不得从需求文档之外的来源推断产品行为，也不得实现需求未覆盖的行为。
2. **等开工指令**：需求澄清阶段不动代码。发现口径冲突、文档与现状不符、或需要额外决策时，先把问题做成结构化选项交给人裁决，不自行择一实现。
3. **最小意图 diff**：一个改动一个意图。禁止顺手扩大范围：不重构未被要求的部分、不格式化整文件、不引入「将来可能用到」的抽象或参数、不添加不可能发生路径上的防御代码、不做半截实现。三层相似代码好过一次提前抽象。
4. **改动前先读相邻实现**：沿用该文件/该模块既有的命名、错误处理、所有权与注释风格，而不是新造一套。
5. **不凭训练记忆假设 API 存在**：无论 Aurora 还是三方库的接口，用到就去读头文件或其文档；框架现状以 Aurora **当日活动分支实测**为准，`SPECIFICATIONS.md` 附录 A 只是某一时点的复核结论，可能过期。
6. **可见性不顺手改**：保持被改符号改动前后的 `public` / `protected` / `private` 划分不变；仅当本次改动本身在语义上要求调整（新增公共 API、新增需被子类覆盖的钩子）时才改，且须说明原因。
7. **占位须可追溯**：临时实现或未完成分支用 `TODO(SPEC.<...>): <说明>` 标注，禁止无标识的裸 TODO。

### 4.2 需求标识与文档同步

8. **标识格式**：`SPEC.<类别>.<域>.<NN>`，规范与五条稳定性规则见 `codespec/SPECIFICATIONS.md` §1.4。引用一律写全（不缩写为域内尾号），同域连续才可写区间。
9. **只追加、不复用、不重排**：新增需求取该域当前最大序号 + 1；需求撤销后其标识作废但不得被占用；改域则换新号并把映射记入 `CHANGELOG.md`。
10. **优先级与阶段标记不入代码与需求文档**：`P0`–`P3`、`M0`–`M5` 这类标记**只允许出现在 `codespec/PLAN.md`**；代码、注释、提交信息、需求文档一律以语义表述（「延后子项」「首个交付阶段」等）替代，阶段与优先级的映射改 `PLAN.md` 即可，不必动需求标识。（本条为说明禁止形态而列举的字符串是本仓唯一例外。）
11. **口径冲突必须回写成裁决**：任何决策与既有文档或实测现状冲突时，在 `SPECIFICATIONS.md` §7 新增（或修订）裁决条目 + 在 `CHANGELOG.md` 记版本条目，保留旧编号以免形成伪死链；未经实测的推断必须在文中显式标注为「推断 / 未验证」。
12. **代码与文档同步**：改动公共行为、模块边界或跨平台约定后，落完代码即回写对应文档；文档与代码运行时冲突时，以**代码运行时**为准并回填文档，禁止为迁就旧文档保留错误实现。
13. **引用可达**：不得引用不存在的文档、章节、符号或路径；提及未落地的规划必须标注「计划 / 待建」。

### 4.3 代码风格与语言

14. **字符串字面量一律 ASCII 英文**（日志、诊断、错误消息、CLI 输出、测试输出），因为字面量会经控制台与日志抵达用户，代码页不受本仓控制。注释可用中文。唯一例外是「换成英文就让被测事实消失」的功能必需中文（CJK 断言素材、编码/宽度测试、上屏文案、locale 输出），须就地标注 `CJK-LITERAL: <类别> - <原因>`；类别词表沿用 Aurora 主仓 `codespec/CODING_STANDARDS.md` §14.2；**诊断文案不属例外**。
15. **禁止裸标准输出**：日志走 Aurora `Logger`（`AURORA_LOG_*` 诊断通道），程序产品性输出走 `AURORA_LOG_RAW`；测试内的打印用本仓测试框架既有宏。不新增 `std::cout` / `printf` / `fprintf` / `puts`。
16. **注释默认不写**：只写 WHY——隐藏约束、微妙不变量、针对某个具体 bug 的 workaround、会让读者意外的行为。不写「这段做什么」「谁调用它」「本次任务改了什么」这类叙述。
17. **文档注释**：`include/borealis/` 下的公共类型与函数按 Aurora 主仓 `codespec/CODING_STANDARDS.md` §13 的形态写（`///` 标记、`@brief` 居块首、命令一律 `@` 前缀、按命令必选矩阵补齐 `@param` / `@return` / `@tparam`）；纯实现叙述用 `//` 且不得紧贴可文档化声明。

### 4.4 测试与验证

18. **新增公共 API 与核心逻辑必须配套测试**，并接入CTest。测试设施复刻 Aurora 的注册式框架：文件前缀 `utest_`（单元）/ `itest_`（集成）/ `etest_`（真实后端 E2E），套件名恒等于文件 stem，断言用既有宏家族，**禁止自定义 `main()`**。
19. **能无头就不碰 GUI**：依赖真实后端或需要断言像素的场景用 `HeadlessSurface`（内存 PNG），避免引入交互式 GUI 测试。
20. **纯逻辑层必须可独立单测**：VT 解析器、网格模型、宽度判定、编码解码、scrollback 环形缓冲、背压合并策略等都不得依赖 UI 才能测试；转义序列行为以 `tests/fixtures/` 的回放夹具驱动断言。
21. **性能与资源类改动须以基准为准**：吞吐、输入延迟、内存、空闲 CPU 的验收口径见 `SPECIFICATIONS.md` §5，回归基线由基准工具维护；不允许以「应该更快了」结项。
22. **门禁脚本的自我要求**：新增校验脚本须以变异注入自证非空转，且默认不为其单配单元测试。

### 4.5 平台与安全

23. **平台假设不得渗入共享路径**：PTY、串口、传输层、默认 shell 探测、DPI 缩放上报只能存在于平台层实现内，共享代码经接口抽象调用（裁决 7.11）。否则后补的 Linux 等价会退化成重写。
24. **凭据不落明文**：密码与私钥 passphrase 经 OS 凭据库存储，配置目录只存引用句柄（`SPEC.FEAT.CONN.09`）；OS 凭据库不可用时降级为「每次询问」，绝不降级为明文。禁止把 `.env`、私钥、真实凭据写入仓库或提交；「配置目录全量 grep 无明文凭据」做成自动化用例而非人工检查。
25. **UI 侧性能纪律**：单线程 UI 模型下，禁止在事件回调、绘制路径里做阻塞 IO 或长计算；后台线程与 UI 之间经有界队列交换合成后的最终值（`SPEC.NF.PERF.06`）。

### 4.6 提交与协作节奏

26. **分层本地提交**：每完成一项被要求的独立工作就提交一个提交，按意图切分（文档 / 构建 / 功能 / 测试分开）。
27. **推送与远端操作须逐次授权**：`push`、建 remote、建 PR、评论他人仓库，每一次都要单独确认，且不因为上一次批准过就推定本次也批准。push 前给出事实化风险评估（红底/落后分支、钩子、密钥暴露）。
28. **显式路径暂存**：`git add <具体路径>`，禁止 `git add -A` / `git add .`，以免混入人同时进行的并行手改；暂存后 `git status` 复核清单，发现可疑文件（可能含密钥）先读内容再决定。仓库归属判断看 `git log`，不看文件的 author 元数据。
29. **提交信息格式**：`<type>: <一句话摘要>`，摘要不超过 72 字符、用中文、描述「改了什么」；正文写「**为什么改 + 关键口径与代价**」，不写操作流水账，可用 `-` 分条列要点。`type` ∈ `docs` / `feat` / `fix` / `refactor` / `test` / `chore` / `perf`。需求标识（`SPEC.FEAT.CONN.09` 形态）**应当**出现在正文，便于从代码回溯需求。禁止 `--no-verify` 之类绕过钩子的用法；钩子失败要查根因。
30. **分支命名**：功能分支 `<type>/<scope>-<短描述>`（如 `feat/vt-parser`）。跨仓改动 Aurora 时，在 Aurora 仓用自己的分支按其规则单独提交，两仓提交互相引用说明但不互相包含提交。
31. **危险操作先确认**：`reset --hard`、`checkout --`、`clean -f`、删除分支/文件、覆盖未提交内容等，先 `git status` 查明现场，再征得同意或改用可逆手段（移动/重命名/stash 标签）。共享工作区内不使用裸 `git stash`。
32. **长任务定期播报进度**（默认每 2 分钟），后台长命令的日志要无缓冲重定向（`| tail` 会缓冲到进程退出），以免判错卡死。
33. **完成定义（DoD）**：构建通过 + 相关测试全绿 + 应回写的文档已回写 + 已本地提交，四项缺一就不宣称完成；UI/交互类改动未经真机走查不得声称「可用」，无法验证时明说。

---

## 5 与 Aurora 主仓的边界

1. **框架缺口按类别分流**（裁决 7.13）：渲染与事件链路上的缺口（影响公共 API 形态）——撞到即先在 Aurora 侧补「公共 API + 单测 + 文档回写」，本仓不等不绕；用现有公共 API 即可组合出来的交互体验类能力——留在本仓，不强行推入框架。
2. **本仓不长期持有框架分叉**：进了框架的原语必须以 Aurora 的公共 API 形式存在，本仓只消费公共头。
3. **不在应用侧私改渲染路径**：这是硬禁；但按第 1 条用公共 API 组合出的应用侧控件不算私改。
4. **改 Aurora 前读 Aurora 的根 `AGENTS.md`**，并遵守其规则（含其字面量语言、文档注释、测试注册与门禁口径）。本文所称「Aurora 主仓」即 §1 里经 `add_subdirectory` 接入的那个仓库；对它的引用一律写「Aurora 主仓 `codespec/<文档>` §N」形态，不写本机绝对路径。
5. **`add_subdirectory` 消费的两个前提**（2026-09-30 实测）：① Aurora 有 7 个默认 ON 的开关（`AURORA_BUILD_TESTS` / `AURORA_BUILD_E2E` / `AURORA_BUILD_DEMOS` / `AURORA_ENABLE_CLANG_FORMAT` / `AURORA_ENABLE_CLANG_TIDY` / `AURORA_BUILD_DOCS` / `AURORA_ENABLE_CCACHE`），须在 `add_subdirectory` 之前以缓存变量关掉，否则其目标与 CTest 门禁会灌进本仓构建面——细节见 Aurora 主仓 `codespec/BUILD_OPTIONS.md` §9.5；② Aurora 侧须是「子项目安全」的：其 `cmake/` 模块曾一律用 `CMAKE_SOURCE_DIR` 拼自身源码树路径，子项目场景下该变量指向本仓根，生成链落不到规则上（只能 configure、不能 build），已由 Aurora 主仓改为 `AURORA_SOURCE_DIR` 修掉。二者任一不成立，本仓构建即断，排查时先看这两处。

---

## 6 现状快照（2026-10-01）

- 已有：`codespec/` 四份文档（需求 / 计划 / 变更历史 / 架构 `ARCHITECTURE.md`，2026-09-30 评审通过）、构建骨架（顶层 `CMakeLists.txt`、`CMakePresets.json` 的单一 `msvc` 预设 = Ninja + MSVC 且构建目录为 `build/`、`cmake/BorealisTests.cmake`、`src/CMakeLists.txt` + `src/main.cpp` 空壳应用、`tests/framework/` 测试框架源码）、`tools/msvc_env.bat`、`.gitignore`、`.gitattributes`、按意图分层的本地提交；remote 为 `origin = https://github.com/polaris-ni/borealis`（PUBLIC，2026-09-30 首次推送 `master`），推送与远端操作仍按 §4.6 第 27 条逐次授权。
- 业务代码已落地七层底座（均编入 `borealis_core` 静态库，应用与测试 runner 都链接它）：终端逻辑层的 VT 解析器（`include/borealis/vt/` + `src/vt/parser.cpp`，`SPEC.FEAT.TERM.01`、架构 §5）、UTF-8 双向编解码（`include/borealis/term/utf8.h` + `src/term/utf8.cpp`，`SPEC.FEAT.TERM.09`、架构 §6）、字符集映射（`include/borealis/term/charset.h` + `src/term/charset.cpp`，`SPEC.FEAT.TERM.01`、架构 §5.3）、网格与 scrollback 的环形存储（`include/borealis/grid/` + `src/grid/storage.cpp`，`SPEC.FEAT.TERM.04`、架构 §4）、终端状态机（`include/borealis/term/terminal.h` + `src/term/terminal.cpp`，把解析出的语义单元执行成网格内容与终端模式：`SPEC.FEAT.TERM.01` 显示内核与私有模式登记、`SPEC.FEAT.TERM.02` 的 16/256/真彩色、`SPEC.FEAT.TERM.03` 主备屏、`SPEC.FEAT.TERM.05` 光标与滚动区域/擦除/插删/制表位、`SPEC.FEAT.TERM.08` 双宽占位与零宽并入（判定表由框架提供，见裁决 7.20）、`SPEC.FEAT.TERM.07` 的 OSC 消费（`include/borealis/term/osc.h` + `src/term/osc.{h,cpp}`：`0/2` 标题、`7` 目录原文、`8` 超链接区间、`52` 剪贴板、`133` 命令块边界，产物形态见裁决 7.21），架构 §5、§3.4、§6.3）、**会话层**（`include/borealis/session/` + `src/session/`：架构 §7.2 拍板后的 `Connection` 基础接口与 `ConnectionEvents` 反向通道、`Session` 把「解码 → 解析 → 状态机 → 网格」串成一条链并持有唯一跨线程协调点、`DamageQueue` 有界背压队列按行区间合并提交（`SPEC.NF.PERF.06`、裁决 7.18），DA1/DSR 应答经 `term::ResponseSink` 锁内登记、出锁后写回连接（`SPEC.FEAT.TERM.01`），OSC 产物经加锁访问器透出、`OSC 52` 写方向由 `session::ClipboardOutbox::drain()` 在主线程落系统剪贴板（裁决 7.21③））、**平台连接层（Windows 腿）**（`include/borealis/conn/local_terminal.h` + `src/platform/win/`：`ConptyConnection` 以 `CreatePseudoConsole` + 属性表挂子进程、单读线程喂会话、`ResizePseudoConsole` 接尺寸、关停按裁决 7.19 的 ①②③，默认 shell 探测链 PowerShell → cmd → WSL 与环境注入按 7.19④，Win32 类型不出 `src/platform/win/`（裁决 7.11））。前六层均为纯逻辑、可脱离 UI 单测；集成用例有「解码 → 解析」`tests/integration/itest_decode_parse.cpp`、「解码 → 解析 → 状态机 → 网格」的 `tests/integration/itest_terminal_scene.cpp`（回放真实形态首帧，断言 `ESC(0` 框线码点、SGR 色值与来源、OSC 吞掉、主备屏互不污染），以及 `tests/integration/itest_unicode_width.cpp`（以真实字节流断言 `SPEC.FEAT.TERM.08` 验收线：CJK 双宽占位与延续格、同一份含 Ambiguous 的输出在窄/宽口径下的列位一致、行末整体换行不留半格、combining 并入基础格且不推进光标），会话层单测以 `Connection` 替身驱动；e2e 用例 `tests/e2e/etest_local_terminal.cpp` 以真实 ConPTY 驱动整条链（输出上屏、环境注入与覆盖、启动目录、键入往返与尺寸变更、突发输出的队列水位、关闭后进程终结内容保留），`tests/e2e/etest_osc_clipboard.cpp` 让 powershell 发出 `OSC 52` 后回读系统剪贴板（断言素材是 base64，明文不经屏幕；用例结束归还用户既有剪贴板内容）。
- **框架侧渲染前置已闭合（2026-10-01 实测）**：Aurora 主仓（分支 `dev-1.0.0-alpha.9.uat.2`，本地提交，未推送）新增 `Painter::draw_text_runs(std::span<const render::TextRun>)`（批量同属性文本片段，输出与逐片段 `draw_text` **逐位相同**；录制态逐片段落 `DrawText` 命令，回放后端零改动）与 `render::FontEngine::monospace_cell(const Font&, float scale) -> render::CellMetrics{cell_width_px, cell_height_px, ascent_px}`（物理像素整格度量，行高与基线与绘制侧首行 snap 同源），连同 2026-09-30 的 `aurora::unicode_cell_width` 使 G1 三腿齐备（裁决 7.22）。据此本仓上屏层 `SPEC.FEAT.RENDER.01` 不再有框架阻塞，架构 §9.2 的自绘过渡备路径不再启用；**本仓尚未消费这两个原语**（渲染层落地时直接取用）。**同日（2026-10-01）复核新增缺口 G13**：`Painter::draw_text_runs` 不收 `TextLayoutOpts` / `TextAAMode`（`render::TextRun` 亦无 opts 位），而斜体能力本身在框架已具备（`TextLayoutOpts.italic` 由 FreeType shear 实现，`FontEngine::draw_text_runs` 与逐片段 `Painter::draw_text` 都收 opts）——缺的只是批量入口的一层透传，故本棒斜体与字距走逐片段带 opts 重载的过渡形态，框架侧透传落地后撤销分流；G2（`MouseEvent` 无 `click_count`）、G9（无 OS 级全局热键）、G11（无跨平台系统通知 API）经同日复核仍开放。四条缺口的**补全由 Aurora 侧承担**，分工与取用规则见裁决 7.24，本仓不私挂分叉、不自算多击、不直调平台的热键与通知 API。上屏层的取用形态已按**裁决 7.23** 拍板：视口自管（`ScrollViewport` 内核而非框架 `Scroll`）、run 按样式全等合并到行且与色带共用切分边界、吞吐基准挂时间门禁、第一棒范围＝`SPEC.FEAT.RENDER.01` + `SPEC.FEAT.RENDER.03` + `SPEC.FEAT.RENDER.04`；细则见架构 §9.2 的「取用形态细则」，代码尚未开工。
- 上屏层的**纯逻辑前置**已落（2026-10-01，裁决 7.23② 的形态，均编入 `borealis_core` 且各有单元用例）：`ui::palette`（`include/borealis/ui/palette.h` + `src/ui/palette.cpp`：色值与来源的合成、256 色的立方与灰阶式子、bold-is-bright、暗淡向底色靠拢、反色次序、WCAG 对比度与最小对比度的整数插值）与 `ui::cell_layout`（`include/borealis/ui/cell_layout.h` + `src/ui/cell_layout.cpp`：`monospace_cell` 的物理像素按 scale 折成 dp 步长与行列数、按样式全等把一行切成 run 且色带与文本共用同一批切分边界、双宽延续格不进文本、combining 随基础码点并进同段 UTF-8 文本）。两个头刻意不含 Aurora 类型，框架的 `Color` / `Font` / `Rect` 互转只发生在绘制侧。`SPEC.FEAT.RENDER.04` 的状态机前置亦已落（同一日）：`term::CursorShape` 与 `TermModes.cursor_shape` / `cursor_blinking` 由 `CSI Ps SP q`（DECSCUSR，空格中间字节是识别标志，私有前缀写法同收）驱动，六档映射照 xterm、缺省与越界档位回落闪烁块，随 `ESC c` 复位；形态与闪烁的像素呈现、失焦降级仍随绘制棒。
- 尚未接线的接缝：网格的**行数**变化语义已按裁决 7.17 落地，且三层已串通（`Storage::set_rows` → `Terminal::resize` → `Session::resize` 下发连接侧），但**尺寸的 UI 侧来源与去抖合并**仍未接线（`SPEC.FEAT.XFER.01`，连接侧已由 ConPTY 实现；行列数的算式已落在 `ui::make_geometry`（可视 dp ÷ 格宽向下取整），缺的是 widget 侧取值与去抖合并）；宽度判定（架构 §6.3）**已接真表**：`term::UnicodeWidthPolicy`（`src/term/width.cpp`）转调 Aurora 公共 API `aurora/core/unicode_width.h` 的 `unicode_cell_width`（0 / 1 / 2 三档 + Ambiguous 入参，UCD 18.0.0，裁决 7.20），零宽码点落在 `grid::Row` 的组合标记侧表；`SingleWidthPolicy` 只作测试用常数注入值。但**侧表内容尚未上屏**——`ui::layout_row` 已按架构 §4.1 取侧表把零宽码点并进 run 文本（裁决 7.23ⓑ），叠字由框架 shaping 承担，绘制侧仍待落地；OSC 消费**已接线**（`SPEC.FEAT.TERM.07` 的状态机腿按裁决 7.21 消费成 `term::OscState` 快照 + 有界链接表 + 取走型剪贴板动作；余下的是消费方：标题上标签名/窗口标题栏、超链接的 hover 与点击、`133` 的命令块区间附着，分别属 `SPEC.FEAT.WS.01`/`SPEC.FEAT.INTERACT.05`/`SPEC.FEAT.INTEG.01`）；DA1/DSR 响应回写**已接**（`term::ResponseSink` + 会话写通道，应答取值口径见裁决 7.18③）；会话产出脏区提交后**尚未唤醒主线程排帧**（`TODO(SPEC.FEAT.RENDER.01)`：接线形态已按裁决 7.23 实测拍板为「提交侧 `Window::surface().request_wake()` + 主线程 `Application::set_on_frame` 排空自有队列」，随上屏层第一棒落地，当前消费方按帧轮询）；`SPEC.FEAT.CONN.01` 剩下的平台腿是 POSIX 侧（`$SHELL` 探测 + `forkpty`），尚未开工——Aurora 无 PTY/进程原语（附录 A 实测），Windows 腿已按此自研，POSIX 腿同口径。
- 构建命令用法事实：MSVC 通道的配置与构建须经 `tools/msvc_env.bat` 包装（或等价的 VS 开发者环境），因为默认 PATH 不含 `cl.exe`；配置走 `cmake --preset msvc`，构建走 `cmake --build --preset msvc`（Ninja 默认满核并行）。该包装另行探测 MSVC 工具集与 Windows SDK 目录补齐 `INCLUDE` / `LIB`：`vcvars64.bat` 要用 `reg.exe` 定位 SDK，而本机沙箱把 `reg.exe` 列入黑名单，拦截后 `cl.exe` 会报找不到 `crtdbg.h`。
- 尚无：`include/borealis/ui/terminal_view.h`（绘制侧控件的公共头）与 `include/borealis/config/` 的公共头、`src/ui/terminal_view.cpp`（全仓唯一触达 `au::Painter` / `au::Widget` 的翻译单元，架构 §9.2）、`src/platform/posix/` 的等价实现、渲染上屏层、`assets/`、基准与门禁脚本；本机 vcpkg（`VCPKG_ROOT` 未设）。
- 本文凡引用「计划 / 待建」路径处均非既存事实；相应目录或文件落地后必须回填本表与 §2，避免出现「文档有、代码无」的死链。
