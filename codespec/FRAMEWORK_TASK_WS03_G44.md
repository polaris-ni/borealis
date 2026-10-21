# FRAMEWORK_TASK_WS03_G44.md — 「关闭请求可否决」的公共入口在 Aurora 框架侧的可派发任务书

> 本文件是 Borealis `SPEC.FEAT.WS.03`（多窗口，关窗那一腿）在 Aurora 框架侧的依赖分析与回货判据。
> 依据：Borealis 对 Aurora 活动分支 `include/aurora/window/surface.h`、`include/aurora/window/window.h`、`include/aurora/app/window_host.h`、`include/aurora/app/application.h` 与 `src/aurora/app/{application,window_host}.cpp` 及各后端（`src/aurora/window/win32_host.cpp`／`x11_surface.cpp`／`wayland_surface.cpp`／`glfw_surface.cpp`／`macos_surface.cpp`）的**读源实测**（2026-10-21，WS.03 批 4 开工盘点，读数登记在 `codespec/WS03_MULTI_WINDOW.draft.md` §0 的 F17／F18——其中 F18 把原先只按 F1 的**推断**升级为**读数**）。MSVC／Win32 腿未编译未实测，本文所有后端行为句一律标「读源」，不得读成 Windows 真机结论。
> **结论先行**：关闭是一条**单向旗标链**——后端置位、宿主聚合、帧末无条件回收。全仓没有「关闭请求」回调、没有返回 `bool` 的拦截点、没有撤销关闭的公共 API（Aurora 自家 `codespec/specification/06-app-platform.md` 亦无该提案）。因此应用侧**连「问用户一句」的窗口都拿不到**：唯一在销毁之前的可观测点是 `set_on_frame` 里轮询 `WindowHost::should_close()`，而那一跳排在同一帧的 `render_all` 之前、`reap_closed` 之后紧跟 `render_all`——抢在那里弹框，结果是「**弹框还开着、窗已经没了**」，框还失去了宿主。
> 登记形态见 `codespec/SPECIFICATIONS.md` 附录 A.2 的 **G44** 行与 §7 裁决 **7.126**⑨；本仓因此**未做**关窗确认（`codespec/WS03_MULTI_WINDOW.draft.md` 稿 D4 的「本棒不做」），并把它写进 `SPEC.FEAT.WS.03` 的「不称可用」账。

---

## 1 框架侧既有能力清册（无需补全）

| 能力项 | 位置 | 现状（读源实测） |
|:---|:---|:---|
| 后端关闭旗标 | `include/aurora/window/surface.h` 的 `Surface::should_close()` | 虚函数，缺省回 `false`；`Surface::close()` 虚函数（应用请求关闭的后端通道） |
| 窗口层聚合 | `include/aurora/window/window.h` 的 `Window::should_close()` | `return surface_->should_close()`；同文件有 `close()`／`minimize()`／`toggle_maximize()` |
| 宿主层聚合 | `include/aurora/app/window_host.h` 的 `should_close()` | `return close_requested_ \|\| (window_ != nullptr && window_->should_close())`；注释口径「程序化请求优先——后端未必实现 `close()`（无头无 OS 关闭语义）」；**`close_requested_` 只有 `request_close()` 一处置位，无清除入口** |
| 程序化关闭 | `application.h` 的 `close_window(WindowId)` | 转 `WindowHost::request_close()`，注释自述「下一次帧末由 `reap_closed` 回收」 |
| 帧循环顺序 | `application.h` 的 `run()` 文档 + `step_frame` | `drain_posted → pump_all_once → on_frame_ → tick_all → anim_/sched_.tick → render_all → reap_closed → pump_deferred_work → wait_once` ⇒ `on_frame_` 与 `reap_closed` **同帧**，前者是销毁前唯一可观测点 |
| 回收实现 | `src/aurora/app/application.cpp` 的 `reap_closed()` | ① `MainWindowClosed` 策略下主窗已请求关闭 ⇒ 给**全部**宿主 `request_close()`（本帧只置位）；② 收集待回收 id，并在此刻对**所有**已请求关闭的宿主做几何持久化；③ 全关时**保留主宿主**（使 `scene()`/`window()` 在 `run()` 后仍可用）；④ 逐个回收：`teardown()` → 销毁宿主 → `on_window_closed_(id)` → 恢复模态 owner 输入 |
| 关闭后清理 | `application.h` 的 `set_on_window_closed(std::function<void(WindowId)>)` | 在宿主被帧末回收**之后**触发一次；文档明写「不要在此回调内再关闭其他窗口」 |
| 退出策略 | `application.h` 的 `ExitPolicy`／`quit()` | `LastWindowClosed`（缺省）／`MainWindowClosed`／`ExplicitOnly`；`quit()` 任何策略下都生效 |

**各后端置位路径的读数**（决定本条工作量的关键，全部只读源）：Win32 的 `Win32Host::Impl::handle_close()` 与 `handle_destroy()` 都置 `should_close = true`，`poll_platform_events()` 收到 `WM_QUIT` 也置位（同段注释记着「`WM_QUIT` 是线程级的，多窗下不能再 `PostQuitMessage`」那条已修的坑）；X11 由 `ClientMessage` 的 `WM_DELETE_WINDOW`、`DestroyNotify`、以及 CSD 自绘标题栏的关闭钮三处置 `close_requested`；Wayland 在 toplevel 关闭事件处置位，注释自述「等价于用户点 ×」；GLFW 直读 `glfwWindowShouldClose`；macOS 走 `windowShouldClose:`；`HeadlessSurface` 有 public 的 `set_should_close(bool)`。**⇒ 今天只有 GLFW 与无头两档「有清位的可能路径」**（GLFW 侧有现成的 `glfwSetWindowShouldClose`，无头侧 `HeadlessSurface::set_should_close(bool)` 是 public），**其余后端的旗标都在私有 pimpl 里、连框架外部都拿不到清位入口**——这就是「否决必须由框架实现、不能下放给应用」的实测理由。

**Borealis 消费现状**：关窗后的清理链已落（`set_on_window_closed` 里做本窗名下全部标签的撤账、广播成员摘除、活动窗记录 `forget`、容器移除、兜底落点易主）；`run()` 返回后的退出序再走一遍「关会话 → 摘根 → 撤账」。**而「关闭之前先问用户」这一腿完全没有**，与已落的关标签确认（判据＝该标签任一 pane 的 `Session::alive()`，确认框按窗一份）**不同档**：关标签会弹确认、点窗口 X 直接把正在跑的进程静默带走。这条差额就是 `SPEC.FEAT.WS.03` 不得称「可用」的主因。

---

## 2 缺的那一条腿（三条可接受形态，首选甲）

**甲＝关闭请求闸门（一次一问、可否决）**
`Application::set_on_close_requested(std::function<bool(WindowId, CloseRequestSource)>)`：后端报告关闭后、宿主进入回收判定**之前**问一次；返回 `false` 即撤销该次关闭，窗口继续正常渲染与存活。必须同时定死四条，否则甲比不做更坏：

1. **调用点**：装在 `pump_all_once` 之后、`reap_closed` 的收集步之前（同帧即可），且**只问一次**——被否决的这一次不得每帧重复询问，除非后端再次上报新的关闭请求。否则应用弹一次框，框还被下一帧的询问重复触发。
2. **来源可分**：`CloseRequestSource` 至少分「OS 关闭钮／窗口管理器请求」与「程序化 `close_window()`／`Window::close()`」。本仓的确认只该对前者弹框；两者混档会让「应用自己关一扇窗」也要过用户闸门。
3. **连带路径同样受闸**：`reap_closed` 第 ① 步在 `MainWindowClosed` 下给全部宿主 `request_close()`，这一批也必须逐扇问（来源可标「策略连带」），否则「关主窗 → 其余窗静默全关」正好绕过闸门。
4. **撤销的后端责任**：框架须在置位的那一侧清旗标（Win32 不得顺势 `DestroyWindow`，X11 的 `WM_DELETE_WINDOW` 本就可不销毁，Wayland 的 toplevel close 需明确「客户端可继续存活」的形态，GLFW 走 `glfwSetWindowShouldClose(false)`，无头走 `set_should_close(false)`）——**这条清单是甲的全部成本所在，也是它不能下放给应用的原因**。

**乙＝给一枚撤销入口，闸门由应用自装**（非破坏、纯增，但把轮询留给应用）
`WindowHost::cancel_close()`（连同各后端清位）。应用可在 `set_on_frame` 里轮询 `should_close()`、命中即弹框并 `cancel_close()`。缺点实测在册：`on_frame_` 排在 `render_all` 之前，而本仓的确认框要挂在该窗的浮层宿主上——同一帧里既弹框又渲染那扇「正在关闭」的窗，形态能做但时序脆，且每帧轮询与「只问一次」的口径要应用自己兜。**若甲被否，本仓接受乙并要求 §2.1 的来源区分改由 `should_close()` 之外的一枚只读来源查询承担。**

**丙＝明确本条不属于框架**（下位形态）
若 Aurora 判定「关闭否决」属应用层（例如要求应用一律自绘标题栏 CSD），请给出该判定的书面口径，本仓据此把 `SPEC.FEAT.WS.03` 的这条边界**永久登记为「不做」**而不是「等框架」——现稿写的是「本棒不做」，语义上是欠项而非结论，需要框架侧一句话把它钉死。

**不接受的形态＝只在文档里写「用 `set_on_window_closed` 清理即可」**：那一跳排在回收**之后**，窗口已经没了，问什么都来不及。

### 2.1 与 `set_on_window_closed` 的时点分工（必须一并定）

| 时点 | 现有／新增 | 用途 | 约束 |
|:---|:---|:---|:---|
| 后端置位 → 闸门（甲新增） | `set_on_close_requested`（新） | 询问用户、可否决 | 销毁前、可撤销、只问一次 |
| `reap_closed` 第 ② 步 | 现有几何持久化 | 写快照 | 已在册，闸门否决时不应触发 |
| `reap_closed` 第 ④ 步之后 | `set_on_window_closed`（现有） | 摘本层账目 | 不可否决、不得在此再关其他窗 |

---

## 3 回货判据（五条）

1. **否决真的能否决**：闸门回 `false` 后，该窗继续存活且可再次被关闭；`WindowHost::should_close()` 归 `false`，`reap_closed` 不再收集它，`set_on_window_closed` 不触发。
2. **只问一次**：一次 OS 关闭请求对应一次询问（用例断询问计数为 1，即便被否决后仍持续渲染多帧）。
3. **来源可分**：OS 关闭钮与程序化 `close_window()` 两类请求在参数上可区分；程序化那条可由应用直接放行而**不弹框**。
4. **策略连带不漏问**：`MainWindowClosed` 下主窗关闭引起的连带 `request_close()` 同样进入闸门（本仓的会话确认对每一扇有活会话的窗都要问得到）。
5. **无头可证**：`HeadlessSurface::set_should_close(true)` 即可驱动整条闸门（含否决与放行两档），不需要真实后端——本仓现有 `itest_workspace_window` 的撤账证人正是用这一枚旗标驱动的，闸门必须落在同一可驱动面上。

---

## 4 配套用例（框架侧）

- `itest_close_veto_keeps_window_alive`：注册回 `false` 的闸门 → 无头置关闭旗标 → 驱动若干帧 → 断宿主仍在 `windows()` 里、`should_close()` 为 `false`、`set_on_window_closed` 未被调用。
- `itest_close_veto_asks_once`：同一枚旗标只询问一次；再置一次则再询问一次（询问计数 2）。
- `itest_close_veto_source_is_programmatic`：`Application::close_window(id)` 触发时来源标为程序化；断两种来源可区分。
- `itest_close_veto_covers_main_window_policy`：`ExitPolicy::MainWindowClosed` 下关主窗 → 连带那一批逐扇询问，全部否决时**没有任何一扇**被回收。
- `utest_close_veto_geometry_not_saved_on_veto`：被否决的那一扇不得在 `reap_closed` 第 ② 步写下几何快照（否则「否决了却存了关闭态」）。
- 各后端一份（可 `itest` 或平台腿）：`Win32` 的 `WM_CLOSE` 被否决后窗口未销毁、`X11` 的 `WM_DELETE_WINDOW` 被否决后仍存活、`Wayland` 同、`GLFW` 用 `glfwSetWindowShouldClose` 清位、无头用 `set_should_close(false)`。

---

## 5 本仓接货复验形态

- **本仓要新增一棒**（与 G42／G43 的「接货后零改动」不同，这条接货只是解除阻塞，确认框本身要本仓写）：闸门里按窗判定「本窗名下任一 pane 的 `Session::alive()`」→ 命中即在该窗自己的浮层宿主上弹确认（复用关标签那条已落的「按窗一份确认框」形态与判据，裁决 7.47⑧／7.83），用户否决即放行 `false`，确认则回 `true` 并让既有撤账链照跑；程序化来源（本层主动关窗、退出序）一律放行不弹框。
- **判据与证人**：`itest_workspace_window` 加一段——无头置旗标 + 活会话 ⇒ 询问一次、否决后窗仍活、确认后走完整撤账链；`utest` 面给「有无活会话」的折算件补案。装配层那一跳无自动化证人（`main.cpp` 不进 runner，裁决 7.83），如实登记。
- **文档回写**：`SPECIFICATIONS.md` 附录 A.2 的 **G44** 行改「已闭合（日期＋形态）」、`SPEC.FEAT.WS.03` 现状句与 §7 裁决 **7.126**⑨的「不称可用」账划掉这一条、`CHANGELOG.md` 补版本、`codespec/WS03_MULTI_WINDOW.draft.md` 的稿 D4 与 §9 走查清单、`src/main.cpp` 里那句「留给稿 D4／本棒不做」的注释（退出序附近）与 `set_on_window_closed` 处的「不弹确认，理由见 D4／F18」一并改口。
- **真机腿**：这条**必须真机走查**——OS 关闭钮的置位时机、否决后 WM 是否重发、Wayland compositor 的期望行为，POSIX 无头面读不到；Windows 腿未编译未实测，接货后先跑 POSIX 再单独记 Win32。
- **变异自证**：m1 闸门恒回 `true` → 有活会话时点 X 不弹框、会话随窗散（红，正是现状）；m2 恒回 `false` → 关不掉任何窗、退出序不推进（红）；m3 程序化来源也弹 → 退出序那一路卡在一个没人能答的框上（红）。

---

## 6 工作量评估与优先级

框架侧新增：甲＝一枚回调槽 + `reap_closed` 前置询问 + `CloseRequestSource` 枚举 + **五套后端的清位路径**（Win32 要保证否决时不 `DestroyWindow`、X11 不销毁、Wayland 明确可存活、GLFW／无头用现成 setter）⇒ **中偏大**，风险集中在「各后端置位语义不一」而非代码量；乙＝一枚 `cancel_close()` + 同样的清位清单 ⇒ **中**（公共签名变更最小，但把「只问一次」的负担留给应用，易被消费者用错）。

**本仓优先级**：**高**——这是 `SPEC.FEAT.WS.03` 「不称可用」账里最重的一条，且是**数据损失面**而非观感问题：点 X 会静默带走正在跑的会话进程，而同一应用里关标签却会弹确认，两档并存对用户是「同一动作两种后果」。本仓按 AGENTS §5.1 的类别分流（事件链路，裁决 7.13①）出件，不在应用侧私改框架销毁路径、不自造旗标轮询的半截实现；**G41／G42／G43 三条可与本条并行，本条排最前。**
