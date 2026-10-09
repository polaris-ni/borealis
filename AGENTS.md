# AGENTS.md — Borealis（路由 + 硬约束）

> 单一入口：只路由到 `codespec/` 权威文档（§3）与硬约束（§4 / §5）。细则不在这里——超 8 KiB 会静默截断，由 `check_agents_size` 门禁守护。

## 1 项目定位

**Borealis** 是基于 **Aurora**（C++20 AI-first GUI 库）的终端管理软件：本地/SSH/串口/Telnet + 多标签与任意分屏。本仓是**消费者应用**，不是库。

- 接入 `add_subdirectory(../aurora)` 源码树，三方依赖走 vcpkg，不复制 Aurora 源码。
- 标识统一 `borealis`；MSVC + Ninja + Win32 主通道，Linux GCC + POSIX（forkpty）腿可用。

## 2 目录布局

`codespec/` 项目文档；`CMakeLists.txt` `CMakePresets.json` `cmake/` 构建（测试与门禁在 BorealisTests.cmake）；`include/borealis/` 公共头（不含 Aurora 类型）；`src/` 实现（平台腿 `src/platform/{win,posix}/`）；`tests/`（framework/ support/、unit/ integration/ e2e/ fixtures/）；`tools/`（msvc_env.bat、bench/、check/）；`assets/` **计划 / 待建**。

分层细则见 `ARCHITECTURE.md` §2；绘制侧控件类声明含框架类型，留私有头 `src/ui/`（裁决 D1①）；标「计划/待建」者非既存事实，落地后回填。

## 3 文档导航

- **需求**（做什么/验收判据）→ `codespec/SPECIFICATIONS.md`（标识 §1.4，裁决 §7）
- **优先级/分期/观察池** → `codespec/PLAN.md`（唯一分期来源）
- **版本演进/旧编号映射** → `codespec/CHANGELOG.md`
- **架构/分层/数据流/平台边界** → `codespec/ARCHITECTURE.md`（§16 待定）
- **框架能力/缺口/编码规则** → Aurora 主仓 `AGENTS.md` 与其 `codespec/`

引用写「相对路径+章节号」，不写 `file:line` 或本机文件。

## 4 硬规则（编号对外有引用，勿改序、勿复用）

### 4.1 任务边界

1. 需求先行：先读需求全文（含「延后子项」）与 `PLAN.md` 分期，不推断需求外行为。
2. 等开工指令：澄清阶段不动代码；口径冲突或需决策时给结构化选项交人裁决。
3. 最小意图 diff：一个改动一个意图；不重构未要求处、不加将来才用的抽象，不做半截实现。
4. 改动前先读相邻实现，沿用既有命名、错误处理与所有权。
5. 不凭训练记忆假设 API 存在：读头文件或文档；框架以 Aurora 活动分支实测为准。
6. 可见性不顺手改：`public`/`protected`/`private` 划分不变，确需改时说明原因。
7. 占位须可追溯：未完成分支用 `TODO(SPEC.<...>): <说明>`，禁裸 TODO。

### 4.2 标识与同步

8. 标识 `SPEC.<类别>.<域>.<NN>`：规范见 `SPECIFICATIONS.md` §1.4；引用写全。
9. 只追加、不复用、不重排：新增取该域最大序号 + 1；改域换新号并记 `CHANGELOG.md`。
10. `P0`–`P3` / `M0`–`M5` 只许出现在 `PLAN.md`，其余处一律语义表述（本条即例外）。
11. 口径冲突回写成裁决：`SPECIFICATIONS.md` §7 加裁决 + `CHANGELOG.md` 记版本；未实测推断标「推断 / 未验证」。
12. 代码与文档同步：改公共行为 / 边界 / 跨平台约定后即回写；冲突以**代码运行时**为准并回填。
13. 引用可达：不引用不存在的文档、章节、符号或路径；未落地规划标「计划 / 待建」。

### 4.3 风格与语言

14. 字面量一律 ASCII 英文；注释可中文。例外＝换英文即让被测事实消失者，须标 `CJK-LITERAL: <类别> - <原因>`（词表见 Aurora 主仓 `CODING_STANDARDS.md` §14.2）；**诊断不属例外**。
15. 禁裸标准输出：日志走 `AURORA_LOG_*`、产品走 `AURORA_LOG_RAW`、测试用框架宏；不新增 `std::cout`/`printf` 等。
16. 注释默认不写：只写 WHY（隐藏约束、不变量、workaround、意外行为），不写任务叙述。
17. 文档注释：公共类型与函数按 Aurora 主仓 `CODING_STANDARDS.md` §13 形态写；实现叙述用 //。

### 4.4 测试与验证

18. 新增公共 API 与核心逻辑须配套测试并接入 CTest：前缀 `utest_`/`itest_`/`etest_`（真实后端 E2E），套件名＝文件 stem，禁自定义 `main()`。
19. 能无头就不碰 GUI：真实后端或像素断言用 `HeadlessSurface`。
20. 纯逻辑层必须可独立单测；转义序列以 `tests/fixtures/` 回放夹具驱动。
21. 性能与资源改动以基准为准：口径见 `SPECIFICATIONS.md` §5，基线在 `tools/bench`；不允许以「应该更快了」结项。
22. 门禁脚本须以变异注入自证非空转，默认不单配单元测试。

UI 件节奏：UI 编写前先出设计图评审（稿入 `codespec/`），实现后按第 33 条真机走查。

### 4.5 平台与安全

23. 平台假设不得渗入共享路径：PTY、串口、传输、shell 探测、DPI 上报只在 `src/platform/` 内，共享代码经接口抽象。
24. 凭据不落明文：密码与私钥 passphrase 经 OS 凭据库，配置目录只存句柄（`SPEC.FEAT.CONN.09`）；库不可用降级「每次询问」、绝不落明文；禁 `.env`、私钥、真实凭据入库或提交；「配置目录 grep 无明文凭据」做成自动化用例。
25. UI 性能纪律：事件回调与绘制路径禁阻塞 IO 与长计算；后台线程与 UI 经有界队列交换合成后的最终值（`SPEC.NF.PERF.06`）。

### 4.6 提交与协作

26. 分层本地提交：每项独立工作一个提交，按意图切分。
27. 推送与远端操作逐次授权：push、建 PR、评论他人仓库逐次确认；push 前给事实化风险评估。
28. 显式路径暂存：只 `git add <具体路径>`，禁 `git add -A`/`git add .`；暂存后 `git status` 复核，可疑文件先读内容。
29. 提交信息 `<type>: <一句话摘要>`（≤72 字符中文，type ∈ `docs`/`feat`/`fix`/`refactor`/`test`/`chore`/`perf`）；正文写「为什么 + 口径与代价」与需求标识；禁绕过钩子。
30. 分支命名 `<type>/<scope>-<短描述>`；跨仓改 Aurora 在该仓单独提交，互相引用不互为包含。
31. 危险操作先确认：`reset --hard`、`checkout --`、`clean -f`、删分支/文件、覆盖未提交内容，先 `git status` 再征同意或改可逆手段；共享工作区不用裸 `git stash`。
32. 长任务每 2 分钟播报进度；后台长命令日志无缓冲重定向。
33. DoD：构建通过 + 测试全绿 + 文档已回写 + 已本地提交；UI 未真机走查不得称「可用」。

## 5 与 Aurora 主仓的边界

1. 框架缺口按类别分流：渲染与事件链路的缺口先在 Aurora 侧补「公共 API+单测+文档回写」，本仓不等不绕；可组合的体验类能力留本仓。
2. 不长期持有框架分叉：进框架的原语须以公共 API 存在，本仓只消费公共头。
3. 不在应用侧私改渲染路径（硬禁）；按公共 API 组合的应用侧控件不算私改。
4. 改 Aurora 前读其根 `AGENTS.md` 并遵守之；引用写「Aurora 主仓 `codespec/<文档>` §N」。
5. `add_subdirectory` 两前提：① Aurora 默认 ON 的开关须在其前以缓存变量关掉；② Aurora 须「子项目安全」。任一不成立构建即断。

## 6 现状快照

逐项现状以 `SPECIFICATIONS.md` §7/附录 A.2 与 `CHANGELOG.md`、`PLAN.md` 为准。长期两条：

- **无 CI 工作流**：门禁已落、无流水线（`SPEC.NF.PERF.02` 的「进 CI」未闭环）。
- 本机 vcpkg 已接（2026-10-09，`VCPKG_ROOT=/home/polaris/Projects/demo/vcpkg`，manifest 锁 `libssh` 0.12.0；未设时按无 vcpkg 构建，仅 SSH 腿受影响）。

## 7 构建 · 测试

```sh
# Windows 主通道（MSVC+Ninja，须 VS 开发者环境）；Linux 把 msvc 换 linux
cmake --preset msvc && cmake --build --preset msvc && ctest --preset msvc -E etest_
# 基准走优化档 → build-bench/（裁决 7.35）
cmake --preset msvc-bench && cmake --build --preset msvc-bench
```

- `utest_*`/`itest_*` 沙箱内直接跑；`etest_*` 依赖真实 PTY：Windows 腿投放交互桌面会话、POSIX 腿需 shell 会话（两腿同判据）。
- 时间/资源门禁不挂 CTest，按脚本自述单独执行。
- 新增 `.cpp` 经 `CONFIGURE_DEPENDS` GLOB 自动进构建与测试注册。
