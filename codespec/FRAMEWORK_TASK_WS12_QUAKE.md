# FRAMEWORK_TASK_WS12_QUAKE.md — 下拉式终端要的两条运行期窗口开关：可见性切换与置顶切换（附一条文档缺陷）

> 本文件是 Borealis `SPEC.FEAT.WS.12` Quick Terminal（quake 模式）的唯一框架依赖项的派发任务书，登记为缺口 **G47**（`codespec/SPECIFICATIONS.md` 附录 A.2；裁决 **7.160**）。消费动机见 `codespec/UI_QUAKE_TERMINAL.draft.md` §3。
> 依据：对 Aurora 活动分支的**读源实测**（2026-10-26），涉及 `include/aurora/window/window.h`、`include/aurora/window/surface.h`、`include/aurora/app/display.h`、`include/aurora/app/window_geometry.h`、`src/aurora/window/win32_host.cpp`。只读源码、未编译未真机，涉及后端行为的句子一律标「读源」。
> **结论先行**： quake 模式的四条腿里，**两条今天就能用**（全局热键 `OsHotkeyRegistry`、显示器工作区枚举 `app::list_displays()`），**两条没有公共入口**（运行期显隐、运行期置顶）——`ShowWindow` 与 `SetWindowPos(HWND_TOPMOST)` 在框架里都只存在于**构造路径**，之后应用侧再无任何句柄可触达它们。另附带报一条**文档缺陷**：`Surface::set_size` 头注释的单位与实现相反。

---

## 1 框架侧既有能力清册（读源实测）

| 能力项 | 位置 | 现状 |
|:---|:---|:---|
| OS 级全局热键 | `app::os_hotkeys()` → `OsHotkeyRegistry::add(KeyCombo, fn)` | **可用**（裁决 7.29③ 回货）。Win32 走 `RegisterHotKey`/`WM_HOTKEY`，无焦点也触发；无后端平台 `enabled()` 为 `false` 且 `add()` 机器可见地失败。**命中由 `Application::step_frame()` 内部 `drain_pending()` 排空**——消费方**无需**自己每帧调用（头注释「由帧循环每帧调用一次」的「帧循环」指框架自身，示例 `examples/demos/demo_os_hotkey.cpp` 里应用侧再调一次是冗余的，不冲突） |
| 建窗期可见性 | `WindowOptions::visibility`（`WindowVisibility::Normal` / `NoActivate` / `Hidden`） | 只在构造时生效一次：Win32 在 `Win32Host` 构造尾部按档位 `SW_SHOW` / `SW_SHOWNA` / `SW_HIDE`（读源），**此后无任何公共入口再改** |
| 建窗期置顶 | `WindowStyle::always_on_top` → `WS_EX_TOPMOST` + `SetWindowPos(HWND_TOPMOST)` | 同上：构造时一次定档（读源），**运行期没有改置顶的入口** |
| 运行期 z 序 | `Surface::raise()`（Win32 `BringWindowToTop`）、`Surface::focus_window()`（`SetForegroundWindow` + `SetFocus`） | 有，但**都不改变可见性**：对一个已 `SW_HIDE` 的窗口两者都无事发生（读源）。`BringWindowToTop` / `SetForegroundWindow` 对**已最小化**窗口是否顺带还原，属 Win32 行为、本仓未实测，标「推断 / 未验证」 |
| 运行期几何 | `Surface::position()` / `set_position()`（物理 px）、`set_size()`（见 §4 文档缺陷）、`Window::minimize()` / `toggle_maximize()` / `set_fullscreen()` | 有——**这是本条唯一能用作代偿的窗口开关**，但它是「最小化」不是「隐藏」：任务栏留痕、带还原动画、且还原腿不可靠 |
| 显示器与工作区 | `app::list_displays()` / `primary_display()` / `display_containing()`，`Display::bounds` / `work_area` / `scale_factor`（物理 px） | **可用**：贴边矩形应用侧自算的输入齐备（含任务栏扣减），故「贴边/夹取」**不构成阻塞**，本仓按裁决 7.160 归应用侧策略（§5.1 分流：可组合的体验类能力留本仓） |
| 几何可用性校验 | `app::is_window_geometry_usable(g, displays)` | **只判不夹**：判「尺寸为正且与任一工作区有交集」，不返回修正值。热键跨屏/分辨率变化后要落位，调用方得自己算夹取 |
| 窗口状态上报 | `Surface::set_window_state_handler`（`WindowState::Visible` / `Occluded` / `Minimized`…）、`Application::set_on_window_state` | 有，但 `Application` 那层**只转发主窗口宿主**；副窗口要自己经 `WindowHost::set_state_sink` 取——「失焦自动隐藏」这条腿的输入因此是可得的 |

**Borealis 消费现状（代偿）**：热键与几何两腿本批已在应用侧落地并单测（`ui::quake_target_rect()` 自 `app::list_displays()` 折算贴边矩形；`ui::QuakeToggle` 管显隐意图与去抖）。显隐与置顶两腿**没有代偿**：`Window::minimize()` 会把窗口留在任务栏且还原腿未验证，用它冒充「隐藏」是错的产品形态，故本仓把装配腿整体挂起等回货，未硬凑一个次优形态。

---

## 2 缺的两条腿（每条给两档可接受形态，任一档都能闭合）

### 2.1 运行期可见性切换（硬阻塞）

**甲＝直接开关（推荐，最小面）**

- `Surface::set_visible(bool)` + `Window::set_visible(bool)` 透传；Win32 落到 `ShowWindow(hwnd, visible ? SW_SHOWNA/SW_SHOW : SW_HIDE)`。
- 口径：与建窗期 `WindowVisibility` 同一份状态（`set_visible(false)` 之后 `window_state()` 报 `Hidden`，或明确另立枚举值，二者取一但必须自洽）；**不改变激活态**——显示档默认不抢焦点（ quake 呼出要焦点是应用侧另调 `focus_window()`），隐藏档不产生「先可见后隐藏」的一帧闪烁；无头后端为记录值语义的 seam（与 `HeadlessSurface` 既有 `set_position`/`set_size` 同形态），供用例断言。
- 线程：主线程调用；从热键回调（已在帧内主线程）调用即安全。

**乙＝意图式开关**

- `Surface::toggle_visibility()` 或 `Window::show()` / `Window::hide()`。语义与甲同，只是把「当前档」的读权也交给框架（需同时提供 `[[nodiscard]] auto is_visible() const -> bool`）。
- 若选乙，`is_visible()` 必须答「应用意图」还是「OS 实际态」需写清：最小化 / 被别的程序覆盖时二者可以不同，框架不得把二者混为一谈。

### 2.2 运行期置顶切换（硬阻塞）

**甲＝直接开关（推荐）**

- `Surface::set_always_on_top(bool)`；Win32 落到 `SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)`。
- 口径：与建窗期 `WindowStyle::always_on_top` 同源（构造时给的那一档就是它的初值）；改动不得触发窗口移动、尺寸变化或激活（`SWP_NOACTIVATE`）；`GWL_EXSTYLE` 读回值须与新意图一致（供用例与诊断）；无头后端记录值。

**乙＝一次性意图**

- 若框架不愿开运行期开关，则至少把「`always_on_top` 可在运行期改」列成已知限制并在头注释写明（当前读源看不到任何关于「构造后不可改」的说明，应用侧只能靠读实现发现）。本仓接受甲/乙任一，但**乙不解除阻塞**。

### 2.3 两条都必须一并定的口径

1. **不重排既有语义**：`WindowVisibility` 建窗期行为、`position()`/`set_position()` 的物理像素口径、`is_window_geometry_usable()` 只判不夹的宽松判据全部逐位不变。
2. **失败不静默**：无该能力的后端（GLFW / X11 / Wayland / WASM）要么提供等价实现，要么让调用方**可查询**（如 `Surface::supports_runtime_visibility()` 或让 `set_visible` 在无头/不支持档返回 bool），不得做成 no-op 后让用户以为「热键坏了」。
3. **用例面**：无头后端即可断言（记录值 + 状态上报），不要求像素、不要求真实桌面会话。
4. **文档同步**：回货后 `WindowOptions::visibility` / `WindowStyle::always_on_top` 的头注释须写明「建窗为初值，运行期可经 X 改」。

---

## 3 回货判据（框架侧用例，Headless 即可）

1. **可见性**：`set_visible(false)` 后 `window_state()` 与新建 `Hidden` 档窗口报同一值；再 `set_visible(true)` 回到原档；隐藏期间 `raise()` / `focus_window()` 不产生「窗口出现」的副作用；无头后端两值可断言。
2. **置顶**：`set_always_on_top(true)` 后读回为真、`false` 后为假；切换前后窗口 position/size 逐位不变；无头后端两值可断言。
3. **既有行为不动**：`WindowVisibility` 三档建窗行为、`always_on_top` 建窗档、`position()`/`set_position()` 读数、`is_window_geometry_usable()` 判据、`WindowState` 上报序列逐位不变，既有用例全绿。

---

## 4 附带：一条文档缺陷（不占缺口号，随手修）

`include/aurora/window/surface.h` 的 `virtual auto set_size(Size)` 头注释写「程序化设置窗口**外框**尺寸（**物理像素**）」，而 `src/aurora/window/win32_host.cpp` 的 `Win32Host::set_size()` 实参先过 `to_physical()` 再做非客户区补偿（其注释自陈「逻辑 dp → 物理客户区 → 物理外框」，并明写不能把逻辑值当外框尺寸直传）——**单位是逻辑 dp、语义是「请求的客户区尺寸」，与头注释两处相反**。同文件 `set_position` 的「物理像素」注释与实现一致（直传 `SetWindowPos`），可见这不是口径统一而是 `set_size` 一处的注释过期。
影响：应用侧照头注释写就把 dp 当物理像素传，在 150% 缩放下窗口小三分之一。请**改注释对齐实现**（实现是对的：客户区口径才与「内容要占多大」的产品意图一致），并顺带在注释里点名「外框尺寸由框架按装饰/菜单补偿得出，调用方无需自己加」。本仓已按实现口径写死用例，改注释不需要本仓改动。

---

## 5 本仓接线面（回货后）

- `src/main.cpp` 的装配腿：把 `ui::QuakeToggle` 的显隐意图接到 `window().set_visible(...)`，呼出后调 `focus_window()`；置顶接 `set_always_on_top(true)`（建窗档 `always_on_top` 已可同时给初值）。
- 代偿删除项：无（本仓未落 `minimize` 冒充隐藏的代偿，回货前装配腿整体不接线，`TODO(SPEC.FEAT.WS.12): 等 G47 回货接运行期显隐与置顶` 在册）。
- 验收：`utest_ui_quake` 既有判据不改全绿；新增装配腿走查 7 项（`codespec/UI_QUAKE_TERMINAL.draft.md` §9）由人真机执行。
- 闭环节点：附录 A.2 的 G47 行标「已闭合」、裁决 7.160 补记回货形态、`PLAN.md` §5 观察池与 `SPECIFICATIONS.md` WS.12 现状句同步回填。

## 6 不做什么

- 不在框架内做「贴边锚定 / 工作区夹取」的策略——边缘选择、宽度比例、跨屏落点是产品形态，归应用侧（本仓已落 `ui::quake_target_rect()`）；框架只需继续提供 `app::list_displays()` 与 `set_position/set_size` 这两样输入。
- 不做动画（滑入滑出属应用侧或框架 `anim_`，本条不要求）。
- 不做系统级「独占热键抢占」的补救（`RegisterHotKey` 失败已机器可见，够用）。
- 不为 macOS / X11 / Wayland 造新的可见性模型：Wayland 本就没有「运行期置顶」，按 §2.3 第 2 条以「可查询的不支持」收口即可。
