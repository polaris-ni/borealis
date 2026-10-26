# WS03_WINDOW_CLOSE_CONFIRM.draft.md — 关窗确认（G44 接货）设计稿

> 需求：`SPEC.FEAT.WS.03` 多窗口，关窗那一腿。框架侧回货：`codespec/FRAMEWORK_TASK_WS03_G44.md` 的**形态甲**（`Application::set_on_close_requested` 闸门）已在 Aurora 活动分支落地。
> 本稿只定「本仓怎么接」，不改框架、不加配置键。状态：**已按推荐档落地（2026-10-23）**——`src/main.cpp` 的 `WorkspaceWindow::window_close_confirm` 与 `set_on_close_requested` 闭包、两档词条、`itest_workspace_window` 两案；读数见 §5。待人复核。
> **2026-10-26 增补（§6，裁决 7.161）**：稿 D3 推荐档并列的另一条入口 `workspace.close_window` 已落，判据随之从闸门闭包里搬进公共纯逻辑件 `ui::window_close_disposition`；本棒读数见 §7。

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
- **未验证**：Windows／MSVC 未编译；真机走查（本稿 §4 末条与 `WS03_MULTI_WINDOW.draft.md` §9 第 8 条）未做 ⇒ 不称「可用」。（本条的「Windows 未编译」由 §7 的 2026-10-26 读数解除——MSVC 主通道已编译并跑过全量非 etest，真机走查照欠。）

## 6 增补：第二条用户入口 `workspace.close_window`（2026-10-26，裁决 7.161）

§2 那版落地的只有「点 ×」一条入口。稿 D3 推荐档并列的另一条命令（`WS03_MULTI_WINDOW.draft.md` §23⑶ 登记为在册欠项）本棒补上，而它一落就带来一条此前不可能遇到的口径，必须当场定死：

### D7 两条用户入口同判据、同一只确认框

关窗自此有两条入口：OS 标题栏关闭钮与命令面板的 `workspace.close_window`。它们是**同一次动作**，所以「问不问」不能取决于用户按下去的是鼠标还是回车——本仓此前已经有过同一族反例（关标签会弹确认而点 × 静默带走会话，本稿 §1 那句），不能再造一条。判据落成公共纯逻辑件 `borealis::ui::window_close_disposition(发起方, 本窗有无活会话)`：两条用户入口在 `AskUser` 列同值，本层自持那一档恒 `CloseNow`。业界同档：Windows Terminal 文档把 "closing the last open window by clicking the `X` button **or using the `closeWindow` command**" 并列成同一件事（`customize-settings/startup`，`firstWindowPreference` 的 `persistedWindowLayout` 段），本仓取同一档。

三档发起方而不是两档：命令入口在框架眼里同样是 `Programmatic`，若照框架的三分类直接映射，「用户发起的那一次」和「用户已经答过关的那一次」就共用一档，判据无从区分。装配层因此做一次显式折档（`OperatingSystem` → `OsCloseButton`，`Programmatic` 与 `PolicyCascade` → `AppInitiated`），命令入口自己带 `UserCommand`。

> 未验证：WT 的 `confirmBeforeClosing` 三档取值与缺省本次没能从官方文档取到（抓取两次 404／无该条目），此处只按上面那句已取到的原文立「入口等价」这一条，不引用其缺省值。

### D8 不给缺省键

`Ctrl+Shift+W` 在分屏保留键表里一直是 `ClosePane`（§23⑶ 那处「稿面前提不成立」就是这个），把关窗从它手里拿走要动既有键位表与快捷键只读表的冲突判定，而需求没要求程序化关窗入口带键。故命令只进面板，要键的用户经 `SPEC.FEAT.PREF.04` 的可重绑面自己绑。备选＝换绑 `Ctrl+Shift+W` ⇒ 否：那枚在本仓与 WT 习惯里都是关 pane/关标签，换绑是把已有用户的肌肉记忆拆掉。

### D9 落点窗＝活动窗，取不到即不做事

命令经 `command_window()`（`ui::ActiveWindowTracker`，稿 D7 的同一条解析）取落点窗；取不到只留一条 INFO，不猜某一扇窗。关最后一扇不设「别关」的闸：框架缺省 `ExitPolicy::LastWindowClosed` 让它等于退出，这与点 × 关最后一扇完全同形（D11 的口径不因本棒改变）。

### D10 不新增配置键、不新增持久化域

§3 那句「不加『以后不再询问』偏好键」继续成立；关窗确认与否仍由「本窗有没有活会话」这一条事实决定。

### D11 不复用关标签那只框，也不为命令新造第二只

命令入口吃的是闸门那一只按窗 `window_close_confirm`，因此两条入口在**同一扇窗**上同时发起时仍是同一句问题、同一只框（后到的一次换文案，内容与关窗那一句相同）。这是刻意的：多一只框要多一格生存期账，而两句话本来就是一句。

## 7 落地读数（2026-10-26，MSVC 主通道）

- **代码**：`include/borealis/ui/window_close.h` + `src/ui/window_close.cpp`（公共纯逻辑件，不含框架类型）；`src/main.cpp`（闸门闭包拆成三件——窗级活会话折法 `window_has_live_session`、按窗确认框 `ask_window_close`、命令落点 `request_close_window`，闸门与命令各占其一到同一处判据；命令登记 `workspace.close_window` 排在 `workspace.new_window` 之后，无 `default_binding`）；`assets/i18n/messages_{en,zh}.json` 各一枚 `workspace.action.close_window`；`tests/integration/itest_settings_panel.cpp` 的表外点名圈加这一枚；`tests/unit/utest_window_close.cpp` 四案。
- **构建与测试**：`cmake --build --preset msvc --parallel` 通过（含 `src/main.cpp` 与 `Borealis.exe` 链接）；`ctest --preset msvc -E etest_ -j 16` **123/123**（上一棒的 122 项 ⇒ 本棒 +1 套件 `utest_window_close`）；`borealis_test_runner` 全量 **1517 案 / 1514 绿**（3 红＝既有的 `etest_libsecret_credential_store` 三案，Windows 上无 libsecret，与基线同形）。
- **证人**：`utest_window_close` 四案——① 六行全表逐格钉住；② 两条用户入口逐位等价且 `UserCommand ∧ live == AskUser`（本棒的承重判据）；③ `AppInitiated` 在两档活会话下都 `CloseNow`；④ 无活会话时三条入口一律 `CloseNow`。确认框本体、命令动作的装配段仍**无自动化证人**（`main.cpp` 不进 runner，裁决 7.83）。
- **变异自证**：M1（把 `UserCommand` 并入 `AppInitiated` 那一支）⇒ 恰 `the_whole_table_is_pinned` 与 `the_two_user_entries_get_the_same_answer` 两案红（2/4），另两案绿——正是「键盘入口不再问」的形状；M2（删掉 `AppInitiated` 的豁免支）⇒ 恰 `the_whole_table_is_pinned` 与 `app_initiated_closes_without_asking_even_with_live_sessions` 红（2/4），即「答过一次的关窗被再问一遍／退出序卡死」；M3（把 `workspace.action.close_window` 的中文档置空）⇒ 恰 `itest_settings_panel.every_catalog_key_resolves_to_a_display_label` 红并点名该键（`has_settings_string(key): workspace.action.close_window missing`），其余 64 案绿。三则变异均逐字节回退后重建复绿。另有一条**构建期**证人顺带取到：删掉中文档那一整枚键时 `gen_i18n` 直接报错停建（「translation ... of locale 'en' is absent from the base vocabulary」），故中英两档的键集相等本来就不是靠用例维持。
- **在册边界**：真机走查（§9 第 8 条与下面两条新增）未做 ⇒ 本条不称「可用」；`workspace.action.new_window` 至今仍不在 `itest_settings_panel` 的表外点名圈里（批 3-B 的漏项，本棒不顺手补，登记为观察项）。
