# FRAMEWORK_TASK_CHROME_OS_NOTIFY.md — 应用级「系统明暗偏好变更」通知与当前档读点在 Aurora 框架侧的补全

> 本文件是 Borealis `SPEC.FEAT.PREF.01`（亮/暗 chrome 适配）在 Aurora 框架侧的唯一依赖项的派发任务书，登记为缺口 **G45**（`codespec/SPECIFICATIONS.md` 附录 A.2；裁决 **7.157⑤**）。设计稿 `codespec/UI_CHROME.draft.md` 的 D4 是本条的消费动机。
> 依据：Borealis 对 Aurora 活动分支 `dev-1.0.0-alpha.10` 的**读源实测**（2026-10-26），涉及 `include/aurora/window/surface.h`、`include/aurora/window/platform.h`、`include/aurora/window/window.h`、`include/aurora/app/window_host.h`、`include/aurora/app/application.h` 与 `src/aurora/app/window_host.cpp`、`src/aurora/window/win32_host.cpp`。只读源码、未编译未真机，涉及后端行为的句子一律标「读源」。
> **结论先行**：框架已经**检测到**系统明暗切换（Win32 `WM_SETTINGCHANGE` → 重读注册表 → 逐宿主去重上报），也已经把它用于**框架自己的根主题解析**（`Window::os_scheme_` 的三级优先链）。缺的是**把这件事交给应用**：`Surface::set_os_color_scheme_handler` 是独占单槽、已被 `WindowHost` 装上自己的处理器（赋值语义，应用再装即顶掉框架那一层），而 `Window` 解析后的档（`os_scheme_` / `resolved_root_theme()`）与 `Application` 全无公共读点 ⇒ 应用要么自轮询注册表、要么放弃运行期跟随。

---

## 1 框架侧既有能力清册（无需补全）

| 能力项 | 位置 | 现状（读源实测） |
|:---|:---|:---|
| OS 偏好显式查询 | `include/aurora/window/platform.h` 的 `aurora::os_color_scheme()` | Windows 直读 `HKCU\...\Themes\Personalize\AppsUseLightTheme`（无缓存，每次现读；缺失键回 Light、打不开回 `Unspecified`），GTK 启发式，其余平台 `Unspecified`；注释自陈「显式查询，跳过探测」 |
| 变化检测（Win32） | `src/aurora/window/win32_host.cpp` 的 `handle_setting_change` | `WM_SETTINGCHANGE` → 重读注册表 → 与宿主自己的 `os_scheme_cache` 比对 → 变化才上报（逐宿主去重） |
| 上报槽 | `include/aurora/window/surface.h` 的 `Surface::set_os_color_scheme_handler` / `notify_os_color_scheme_change` | `virtual` setter，**赋值即替换**（`os_color_scheme_handler_ = std::move(h)`）；`HeadlessSurface::emit_os_color_scheme_change` 是现成测试缝 |
| 框架内部消费 | `src/aurora/app/window_host.cpp` 的 `WindowHost::on_os_color_scheme` | 推 `Window::set_os_color_scheme` + `force_full_redraw`；`Window` 的三级优先链＝`set_theme`（explicit）> `os_scheme_` > 明色 |
| 宿主枚举 | `include/aurora/app/application.h` 的 `windows()` / `window_host(WindowId)` | 均 public，应用拿得到每扇窗的宿主 |

**实测到的三处限制**（本任务书的射程）：

1. **handler 单槽且赋值即替换**：`WindowHost` 在装窗路径上把 `set_os_color_scheme_handler([this](ColorScheme s){ on_os_color_scheme(s); })` 装上（`window_host.cpp`），应用再装就是**顶掉**框架那一枚 ⇒ 框架根主题不再跟随系统。没有链式、没有多播、没有「只读旁听」入口。
2. **没有应用级读点**：`Window::set_os_color_scheme` 是 public setter，但 `os_scheme_` 与 `resolved_root_theme()` 均 **private**；`Application` 全类无 `os_color_scheme()` / `theme()` 一类查询。应用即使只想知道「现在是亮还是暗」，也只能自读 `aurora::os_color_scheme()`——每次一记注册表读，且与框架此刻正在用的值不是同一份真值源。
3. **通知是逐窗的、不是应用的**：每扇窗的宿主各收一次 `WM_SETTINGCHANGE` 并各上报一次，框架没有「一次系统切换 = 一次应用级通知」的聚合。应用若想逐窗挂钩，既会收到 N 次，又挂不上（限制 1）。

**Borealis 消费现状（代偿）**：装配层在启动时调一次 `aurora::os_color_scheme()` 解算 `appearance.chrome_theme == system` 的活动档；运行期以 `Application::scheduler()` 的 **1 s interval** 轮询同一函数（调度器 deadline 参与帧循环等待决策，空闲深睡也准时醒），值变即 `apply_chrome_scheme()`——切进程级活动档、`State<Theme>::set()` 换每窗共享主题、逐窗 `surface->request_wake()`、下一调度 tick 重建设置面板浮层（`src/main.cpp`）。这条轮询是**代偿**：代价是 Windows 上每秒一次注册表读与最多 1 s 的切换延迟；回货后删除点只有那一处 `set_interval`。

---

## 2 缺的那一条腿（两条可接受形态）

**甲＝应用级 `State<ColorScheme>`（推荐，与 `Application::window_state()` 同构）**

- `Application` 持一枚 `State<ColorScheme>`，初值 `Unspecified`；
- 公共读：`os_color_scheme_state() -> State<ColorScheme>&`（命名由 Aurora 定）——观察者走既有 `on_signal_dirty` / Provider 体系，应用既可直接读也可挂主题；
- 公共 seed：`refresh_os_color_scheme()` 显式查询 `aurora::os_color_scheme()` 并写入 state（幂等、仅在值变化时通知），启动期消费者不必等第一扇窗；
- 各 `WindowHost::on_os_color_scheme` 在推给 `Window` 之后**再**向应用级 state 报一次，`if (value != state.get()) state.set(value);` ⇒ 跨宿主天然去重（N 扇窗同时收到同一次切换只通知一次）。

**乙＝应用级回调 + 读点（最小形态，非 State）**

- `Application::set_os_color_scheme_handler(std::function<void(ColorScheme)>)` + `os_color_scheme() const -> ColorScheme`（最近一次上报值，未上报前 `Unspecified`）+ `refresh_os_color_scheme()`；
- 去重、线程、初值口径与甲相同。

**本仓不替框架选路**：两条任一都能闭合本条，选路权在 Aurora（其分层与兼容约束优先）。不接受「应用侧自行替换 `Surface` handler」——第 1 节限制 1 已否证（会顶掉框架的根主题跟随）。

### 2.1 无论选哪条都必须一并定的五条口径

1. **去重**：一次系统切换在应用级只报一次；同一值的重复上报不得产生第二次通知（`State::set` 若对相等值也发信号，须在转发处先比）。
2. **线程**：只在主线程回调（`WM_SETTINGCHANGE` 在消息泵线程）；不在后端线程上回调。
3. **初值**：不得为了给初值在 `Application` 构造里隐式读注册表（`platform()` 的既有口径是「显式查询、跳过探测」）；用 `refresh_os_color_scheme()` 显式 seed，未 seed 前读 `Unspecified`。
4. **不改既有解析**：`Window` 的三级优先链、`WindowHost` 的 `force_full_redraw`、Win32 的逐宿主 `os_scheme_cache` 去重一律不变——本条只加「应用级会看到」这一层。
5. **原样透传**：框架只报 OS 原值（含 `Unspecified`），**不得**替应用回落 Light/Dark——「系统不表态回暗」是应用策略（稿 D1/D4）。

---

## 3 回货判据（五条）

1. **跨宿主聚合**：两扇窗，`emit_os_color_scheme_change(Dark)` 打在任一窗上 ⇒ 应用级观察者恰一次、值 `Dark`；两窗各打一次同值 ⇒ 仍恰一次。
2. **同值去重**：同一窗重复 emit 同值 ⇒ 不产生第二次通知；emit `Light` 再 emit `Dark` ⇒ 两次、顺序正确。
3. **seed 与无头**：Headless 上 `refresh_os_color_scheme()`（`os_color_scheme()` 返回 `Unspecified`）⇒ 读点 `Unspecified`，观察者仅在值变化时触发。
4. **主线程**：观察者从主线程被调（用例主线程 emit、主线程断言）。
5. **框架行为不动**：`Window` 根主题解析与重绘行为逐位不变，既有用例全绿。

---

## 4 配套用例（框架侧）

- 单窗去重、两窗聚合、重复同值不通知、`refresh` 的幂等与「变化才通知」、未 seed 时 `Unspecified`、两窗先后不同值的顺序。
- 全部走 `HeadlessSurface`（`emit_os_color_scheme_change` 现成），不需要真实后端。

---

## 5 本仓接线面（回货后）

- 删 `src/main.cpp` 的 1 s `set_interval` 轮询（唯一代偿点），改挂应用级 state / 回调；启动解算与 `apply_chrome_scheme()` 保留（前者可继续读一次性 `os_color_scheme()`）。
- 闭环节点：附录 A.2 的 G45 行标「已闭合」、裁决 7.157 补记回货形态、`PLAN.md` §6 的 G45 注记同步。
- 验收：Windows 设置里切换「应用模式」后 ≤ 1 帧整窗 chrome 换档，且无轮询期注册表读。

---

## 6 不做什么

- 不在框架内做 chrome 色值（那是应用主题层）；
- 不引入平台线程或后台探测线程；
- 不把「系统明暗」与「应用明暗」合成一个概念（应用可显式覆盖，三档是应用策略）。
