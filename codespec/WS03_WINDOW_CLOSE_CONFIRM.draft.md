# WS03_WINDOW_CLOSE_CONFIRM.draft.md — 关窗确认（G44 接货）设计稿

> 需求：`SPEC.FEAT.WS.03` 多窗口，关窗那一腿。框架侧回货：`codespec/FRAMEWORK_TASK_WS03_G44.md` 的**形态甲**（`Application::set_on_close_requested` 闸门）已在 Aurora 活动分支落地。
> 本稿只定「本仓怎么接」，不改框架、不加配置键。状态：**已按推荐档落地（2026-10-23）**——`src/main.cpp` 的 `WorkspaceWindow::window_close_confirm` 与 `set_on_close_requested` 闭包、两档词条、`itest_workspace_window` 两案；读数见 §5。待人复核。

## 1 背景与缺口闭合形态

G44 之前：点窗口 X 是「后端置位 → 帧末无条件回收」，正在跑的会话被静默带走；同一应用里关标签却会弹确认（裁决 7.47⑧／7.83），两档并存。

回货后：框架在 `on_frame_` 之后、`reap_closed` 之前逐窗问一次闸门（边沿闩，一次一问），回 `false` 即清记账位与后端旗标、窗口当帧继续渲染；来源分 `OperatingSystem` / `Programmatic` / `PolicyCascade` 三类；被否决的窗不写几何快照、不触发 `set_on_window_closed`。

## 2 拍板口径

### D1 只对 `OperatingSystem` 来源询问

`Programmatic`（本层 `close_window()`、退出序）与 `PolicyCascade`（框架策略连带；本仓不设 `ExitPolicy::MainWindowClosed`，今日不可达）一律**直接放行**。理由：闸门是给「用户点了 ×」准备的；拿它挡本层自己发起的关闭，会把退出序卡在一个没人能答、甚至没有窗口可挂的框上（任务书 m3）。

### D2 判据与关标签同源

本窗名下**任一标签的任一 pane** 的 `Session::alive()` 为真 ⇒ 需要询问；全部已退出 ⇒ 直接放行（不弹框）。窗口级折法遍历 `window.tab_strip.tabs()` 再复用既有 `has_live_session(tab_id)`，不复制第二份「活会话」定义（与关标签确认、退出角标同一条判据）。

### D3 确认框按窗一份，且与关标签的那只分开

新增 `WorkspaceWindow::window_close_confirm`（`shared_ptr<au::Dialog>` + mounted 标志），挂在**本窗**的浮层宿主上。**不复用** `close_confirm`：关标签与关窗两件事可能同时发生，共用一只换 `content` 会把另一句问题覆盖掉。生存期跨 Show→答话，由窗账目持有（与 `ask_close_confirm` 同形态）。

### D4 答话路径

- 闸门回 `false` 是**同步**答复（框架要求回调只许快、不阻塞），框在回调里 `show()` 即可当帧上屏；
- 用户**取消**：关框，窗继续活着；框架已撤销本次关闭，下一次点 × 是新的抬沿、会再问一次；
- 用户**确认**：先关框，再调 `app.close_window(id)`（`Programmatic` ⇒ 闸门直接放行）→ 同一帧帧末 `reap_closed` → 既有 `set_on_window_closed` 撤账链照跑；不另写一条销毁路径。

### D5 取不到窗的档

`window_of(id)` 取空（宿主已散／不是本层建的窗）⇒ 放行。没有可弹框的对象时不假装询问，也不拦一扇不归本层管的窗。

### D6 文案

新增两条词条：`window.close_confirm.title` / `window.close_confirm.body`，中英两档各一条（表长 510 → 512）。正文与关标签那句同义但**点名整扇窗**：「这一扇窗里还有正在运行的会话，关闭会终止它们」。

## 3 不做

- 不加「以后不再询问」偏好键：需求未提，且属新的持久化域。
- 不逐会话追问（一格一框会把多标签窗变成 N 个对话框）：一次确认关整扇窗，与关标签「一次确认关整张标签」同粒度。
- 不改 `ExitPolicy`，不接 `PolicyCascade` 的级联询问（今日不可达；将来若启用主窗策略再按任务书 §2.1 第 3 条补）。
- 不在应用侧轮询 `should_close()` 或私清后端旗标（G44 的甲形态已给出公共闸门，乙形态不需要了）。

## 4 证人

- `itest_workspace_window` 增两案（无头、双窗、`set_should_close(true)` 驱动）：
  ① 有活会话（以门内布尔模拟）⇒ 询问一次、回 `false` 后窗仍活、`should_close()` 双清、`on_window_closed` 不触发；随后 `close_window()`（`Programmatic`）⇒ 第二次询问来源可辨、窗被回收、关闭回调恰一次；
  ② 没有活会话 ⇒ 询问一次即放行、窗被回收。
- 装配层那段闭包（弹框与词条）**无自动化证人**（`src/main.cpp` 不进 runner，裁决 7.83），由编译、链接与真机走查兜底。
- 变异自证（本仓侧）：m1 闸门恒回 `true` ⇒ 有活会话也不弹框、窗被带走；m2 恒回 `false` ⇒ 窗永远关不掉；m3 程序化来源也弹框 ⇒ 退出序卡死。前两条由 itest 的询问计数与存活断言各自指名。
- **真机欠账**：OS 关闭钮的置位时机、否决后 WM 是否重发关闭请求、Wayland compositor 的期望行为，POSIX 无头面读不到；Windows 腿未编译未实测。按 `AGENTS.md` §4.4 第 33 条，未走查不得称「可用」。

## 5 落地读数（2026-10-23）

- **代码**：`src/main.cpp`（`WorkspaceWindow` 两个成员 + 闸门闭包；`set_on_window_closed` 上方的时点说明改口为「闸门承担询问、撤账钩子在回收之后」）；`src/ui/settings_i18n.cpp`（`window.close_confirm.title/body` 中英各一条，表长 510 → 512）；`tests/integration/itest_workspace_window.cpp`（两案）。
- **构建与测试**：`cmake --build --preset linux` 通过；`ctest --preset linux -E etest_` **113/113**；`borealis_test_runner` 全量 **1401/1401**（v0.137 基线 1395 ⇒ +4 `utest_shortcut_overrides` +2 本稿 itest 两案）。`itest_workspace_window` 本套件 **16/16**。
- **两案读数**：① `a_close_request_can_be_vetoed_and_a_later_programmatic_close_reaps`：OS 抬沿只问一次、来源 `OperatingSystem`、否决后 `should_close()` 已清、B 窗存活且 `on_window_closed` 空；随后 `close_window()` 带来第二次询问（来源 `Programmatic`）并在帧末回收，关闭回调恰一次。② `an_os_close_request_without_a_live_session_passes_straight_through`：无活会话档第一帧问一次即放行，账与落地前同形。
- **变异自证**：m1（第一案回调恒回 `true`）⇒ 恰该案红（窗被带走，存活断言先落空）；m2（第一案恒回 `false`）⇒ 恰该案红（程序化那条也被否，窗不回收）；另在本仓 itest 的第二案回调恒回 `false` 再注入一次 ⇒ 恰该案红（无活会话也关不掉）。m3（程序化来源也弹框/否决）与 m2 的读数同构：第一案对第二次询问的 `Programmatic` 来源断言与回收断言同时失败即指名；装配闭包本身无自动化证人（裁决 7.83），m3 的执行体仍归真机。
- **未验证**：Windows／MSVC 未编译；真机走查（本稿 §4 末条与 `WS03_MULTI_WINDOW.draft.md` §9 第 8 条）未做 ⇒ 不称「可用」。
