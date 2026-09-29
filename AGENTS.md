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
| `include/borealis/` | 本仓公共头（按模块域分目录：`vt` `term` `grid` `session` `conn` `ui` `config`） | 部分落地：`vt/` 有 `sequence.h`（语义单元结构）与 `parser.h`（表驱动解析器）；`term` `grid` `session` `conn` `ui` `config` 各域计划 / 待建 |
| `src/` | 实现；平台相关实现落在 `src/platform/{win,posix}/` | 已有：`src/CMakeLists.txt` + `src/main.cpp`（空壳应用）+ `src/vt/parser.cpp`（编入 `borealis_core` 静态库）；`src/platform/{win,posix}/` 下的平台实现计划 / 待建 |
| `tests/` | `framework/`（测试框架）+ `support/`（公共设施）+ `unit/utest_*` + `integration/itest_*` + `e2e/etest_*` + `fixtures/`（转义序列回放夹具） | 已有：`tests/framework/`（复刻 Aurora 注册式测试框架源码）、`tests/support/paths.h`（仓库相对路径，夹具回放用）、`tests/unit/utest_vt_parser.cpp`、`tests/fixtures/vt/parser_cases.tsv`；`integration/` `e2e/` 目录仍为空，其用例计划 / 待建 |
| `assets/` | 内置字体（Cascadia Code，OFL）与图标；随包分发的许可声明 | 计划 / 待建（目录尚未创建） |
| `tools/` | 基准、门禁与校验脚本 | 仅有 `tools/msvc_env.bat`（VS 开发者环境包装，供 MSVC 通道的配置与构建使用）；基准与门禁脚本计划 / 待建 |

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

## 6 现状快照（2026-09-30）

- 已有：`codespec/` 四份文档（需求 / 计划 / 变更历史 / 架构 `ARCHITECTURE.md`，2026-09-30 评审通过）、构建骨架（顶层 `CMakeLists.txt`、`CMakePresets.json` 的单一 `msvc` 预设 = Ninja + MSVC 且构建目录为 `build/`、`cmake/BorealisTests.cmake`、`src/CMakeLists.txt` + `src/main.cpp` 空壳应用、`tests/framework/` 测试框架源码）、`tools/msvc_env.bat`、`.gitignore`、`.gitattributes`、若干本地提交；**无 remote，未推送**。
- 业务代码首块已落地：终端逻辑层的 VT 解析器——`include/borealis/vt/`（`sequence.h` `parser.h`）+ `src/vt/parser.cpp`，编入 `borealis_core` 静态库（应用与测试 runner 均链接它）；配套 `tests/unit/utest_vt_parser.cpp` 与回放夹具 `tests/fixtures/vt/parser_cases.tsv`（`SPEC.FEAT.TERM.01`；架构 §5 落点）。
- 构建命令用法事实：MSVC 通道的配置与构建须经 `tools/msvc_env.bat` 包装（或等价的 VS 开发者环境），因为默认 PATH 不含 `cl.exe`；配置走 `cmake --preset msvc`，构建走 `cmake --build --preset msvc`（Ninja 默认满核并行）。该包装另行探测 MSVC 工具集与 Windows SDK 目录补齐 `INCLUDE` / `LIB`：`vcvars64.bat` 要用 `reg.exe` 定位 SDK，而本机沙箱把 `reg.exe` 列入黑名单，拦截后 `cl.exe` 会报找不到 `crtdbg.h`。
- 尚无：`include/borealis/` 下除 `vt/` 之外的公共头（`term` `grid` `session` `conn` `ui` `config`）、终端状态机与网格模型、`tests/integration/` `tests/e2e/` 的用例、`assets/`、基准与门禁脚本；本机 vcpkg（`VCPKG_ROOT` 未设）。
- 本文凡引用「计划 / 待建」路径处均非既存事实；相应目录或文件落地后必须回填本表与 §2，避免出现「文档有、代码无」的死链。
