# FRAMEWORK_TASK_WS03_G42.md — 「一条应用级快捷键该落在哪一扇窗」在 Aurora 框架侧的可派发任务书

> 本文件是 Borealis `SPEC.FEAT.WS.03`（多窗口）在 Aurora 框架侧的依赖分析与回货判据。
> 依据：Borealis 对 Aurora 活动分支 `include/aurora/app/shortcuts.h`、`include/aurora/app/application.h`、`include/aurora/app/window_host.h`、`include/aurora/window/window_state.h` 与 `src/aurora/app/{application,window_host}.cpp` 的**读源实测**（2026-10-21，WS.03 批 3 开工盘点，读数登记在 `codespec/WS03_MULTI_WINDOW.draft.md` §0 的 F4／F5／F7）。MSVC／Win32 腿未编译未实测，涉及后端行为的句子一律标「读源」。
> **结论先行**：框架的快捷键派发是「**应用级一份注册表** + **逐宿主一枚前置拦截器**」，而命中那一刻框架**是知道来源宿主的**——`Application::register_host` 里装进 `WindowHost::set_key_pre_handler` 的那枚 lambda 捕获了本宿主指针并把它的焦点态交给 `ShortcutRegistry::handle`。这个事实在 `ShortcutBinding::action` 那枚**无参**闭包处掉了，所以缺的不是能力，而是**把已有事实传到调用方**的一次签名扩展（或一枚纯增的活动窗查询）。
> 登记形态见 `codespec/SPECIFICATIONS.md` 附录 A.2 的 **G42** 行与 §7 裁决 **7.126**②；本仓的代偿件是 `src/ui/active_window_tracker.{h,cpp}`（纯逻辑件，七条 `utest_active_window_tracker`）。

---

## 1 框架侧既有能力清册（无需补全）

| 能力项 | 位置 | 现状（读源实测） |
|:---|:---|:---|
| 应用级注册表 | `include/aurora/app/shortcuts.h` 的 `ShortcutRegistry` | `add(KeyCombo, std::function<void()> action, ShortcutScope, description)`、`remove(int)`、`set_enabled(int,bool)`、`handle(const KeyEvent&, bool has_focus_widget)`；注释自述「由 Application 持有并在键盘事件派发前查询」，`@note Thread: main-thread only` |
| 单条绑定 | 同上 `ShortcutBinding` | `combo` / `action` / `scope` / `enabled` / `description` 五字段；**`action` 是无参闭包** |
| 取用入口 | `include/aurora/app/application.h` 的 `shortcuts()` | 返回应用级唯一一份注册表引用 |
| 逐宿主前置拦截 | `include/aurora/app/window_host.h` 的 `set_key_pre_handler` | 由 `Application::register_host` 在登记时装钩；`WindowHost::dispatch` 的 `KeyEvent` 分支先问它，命中即置 `is_handled` 并**不再向本窗焦点控件派发** |
| 宿主清单与寻址 | `application.h` 的 `windows()` / `window_host(WindowId)` / `main_window()` / `set_main_window(WindowId)` | 皆 public；`WindowHost::id()` 给窗号；`main_window()` 无宿主时回 `AURORA_INVALID_WINDOW_ID` |
| 逐宿主状态上报 | `window_host.h` 的 `set_state_sink` / `set_mode_sink` | 写入端齐备；但 `register_host` 里两枚 sink 都带 `if (raw == main_host_)` 闸门 ⇒ **非主窗的可见性／几何态只落自己宿主，公共面取不到**（`WindowHost` 无 state／mode 的 getter） |
| 应用级聚合值 | `application.h` 的 `window_state()` | 单枚 `State<WindowState>`，语义是「主窗 ≡ 应用」；`WindowState` 只有 `Visible`／`Occluded`／`Hidden` 三值，无窗号 |

**实测到的三处限制**（本任务书的射程）：

1. **全仓无「当前活动窗口」概念**：`include/` + `src/` 内 `active_window` / `focused_window` / `current_window` 三名皆无（2026-10-21 复测仍无）。`FocusManager` 是**逐窗隔离**的（`register_host` 的模态段注释自述「焦点本就不跨窗」），所以「哪个控件有焦点」是窗内事实，而「哪个窗该收这条快捷键」在公共面上无人回答。
2. **`request_wake_all()` 是 private**：跨线程唤醒只有「唤醒全部」这一档，且不给应用侧一枚「唤醒指定窗」的公共入口（本仓的唤醒腿因此绕道 `Surface::request_wake()`，与本缺口无关，仅在册以免被读成已具备）。
3. **应用侧不能自装 pre-handler 顶替**：`set_key_pre_handler` 是**赋值即替换**（`key_pre_ = std::move(h)`），应用装上自己的那一枚就等于吃掉框架那一层的快捷键匹配。本仓因此不动它——这也是「代偿只能自建记录器、不能借用那一枚钩子」的实测理由。

**Borealis 消费现状**：`ui::ActiveWindowTracker` 由装配层在三处上报点记账——视口的 `set_dispatch_observer`、标签条的 `note_press`、横幅的 `note_press`（三类事实＝本窗真的收到过一次按键或指针 Press），命令与快捷键闭包经 `src/main.cpp` 的 `target_window` 解析器取落点，无记录时回构造期主窗号，窗关闭即 `forget(id)`、主窗易主即 `set_fallback(next)`。代价已登记：这条记录器与 OS 眼里的「哪扇在活动」在**无头**与**失焦**两档场景下可以不同，故它进的是 WS.03 真机走查清单而不是判据面。

---

## 2 缺的那一条腿（两条可接受形态）

**甲＝把来源窗交给动作闭包**（语义最贴，代价是签名变更）
`ShortcutBinding::action` 改带来源：`std::function<void(WindowId)>`，或引入 `struct ShortcutContext { WindowId window; }` 以便日后加字段而不再改签名。改动点是三处：`ShortcutRegistry::handle` 多一枚来源入参、`register_host` 那枚 lambda 传 `raw->id()`、`WindowHost::dispatch` 无需改（它本来就是按宿主进来的）。**破坏面**：所有既有 `add(...)` 调用点的闭包都要跟着改，包括本仓的六条命令闭包与框架自家示例；若 Aurora 保留无参重载以兼容「不关心来源」的注册，则破坏面收敛为「新增一档重载」。

**乙＝给一枚活动窗查询**（非破坏、纯增，本仓期望的最小形态）
`Application::active_window() const -> WindowId`：在 `WindowHost::dispatch` 的键盘与指针分支里（或复用 `register_host` 已捕获 `raw` 的那两枚钩子）记一枚「最近派发宿主」，宿主被 `reap_closed` 回收即清除，无记录时回 `main_window()`。语义与本仓 tracker 逐字同构，故回货后本仓的替换点只有那一枚解析器。

**本仓不替框架选路**：两条任一都能闭合 `SPEC.FEAT.WS.03` 的落点腿，选路权在 Aurora（其 AGENTS 的分层与兼容约束优先）。本仓只要求 §3 的判据成立；**若两条都不接受，请给出第三种能表达「派发来源」的公共面形态**，不接受「应用侧自装 pre-handler」这一档（第 1 节限制 3 已否证）。

### 2.1 无论选哪条都必须一并定的四条口径

1. **无 OS 焦点概念时的返回值**：无头宿主／未接线后端下 `active_window()` **不得回无效号**，须回 `main_window()`；否则应用侧要自己写兜底，等于把缺口原样退回调用方。
2. **主窗易主**：帧末回收「保留最后一个宿主」那条路径下主窗身份会换（本仓读数为 F19），活动窗查询不得因此返回已消散的号。
3. **记账范围**：键盘派发与指针 Press 都要算「碰过这一扇」——只记键盘会让「点了 B 窗的标签条、再按全局快捷键」落回主窗。
4. **号不残留**：宿主回收后该号不得再被返回。`WindowId` 由 `Application` 单调发放、不回收复用，但记录器若不清，命令会落到一扇已经不存在的窗上，`window_host(id)` 取空即整条静默不做事。

---

## 3 回货判据（四条）

1. **来源可达**：同一份应用级注册表里的一条 `Global` 快捷键，在两扇窗分别按下时，动作侧拿到的窗号是那一次**派发来源**那一扇（判据形态：以「闭包内记录到的窗号」为观测点，两窗各按一次即得两个不同号）。
2. **无头可证**：`HeadlessSurface` 两扇窗 + `Application` 帧循环即可断，不需要真实后端；本仓与框架两侧的用例都走这一档。
3. **单窗用法逐位不变**：只有一扇窗（含无头单测里那扇）时，行为与本缺口闭合前一致——落点就是那唯一一扇，`main_window()` 为兜底这条不得反过来变成新语义。
4. **不改既有匹配语义**：`ShortcutScope::Global`／`Focus` 的判定、`KeyCombo::matches` 的修饰位口径（G25 已闭合的那两条掩码划分）、命中即消费的后果，都不得因本条改动而变——本条只交付「落在哪一扇」，不重做「算不算命中」。

---

## 4 配套用例（框架侧）

- `utest_shortcut_binding_carries_window`（甲）：两宿主各派发一次同一枚 `KeyCombo`，断动作闭包收到两个不同 `WindowId`；无焦点控件时 `Focus` 作用域仍不命中（守 §3 第 4 条）。
- `utest_application_active_window_tracks_dispatch`（乙）：无派发 → 回 `main_window()`；向 B 派发按键 → 回 B；向 A 派发指针 Press → 回 A；`reap_closed` 收掉 B 后 → 不再回 B。
- `itest_multi_window_shortcut_landing`：两扇无头窗各持一份场景，注册一条全局快捷键，逐窗派发并断言落点随派发来源移动；关掉一扇后另一扇仍拿得到自己那一格。
- `utest_application_active_window_headless`：全为无头宿主时不得返回 `AURORA_INVALID_WINDOW_ID`（§2.1 第 1 条的反面证人）。

---

## 5 本仓接货复验形态

- **落点**：`src/main.cpp` 的 `target_window` 解析器（五类命令闭包与 `workspace.new_window` 之外的落点都读它）。回货后它改读框架公共面，**判据零改动**——本仓现有证人断的是「命令落哪一扇」，不断「谁告诉我们的」。
- **撤除项**：`src/ui/active_window_tracker.{h,cpp}` 与其七条 `utest_active_window_tracker`、三处上报点（视口 `set_dispatch_observer`、标签条与横幅的 `note_press`）。撤 tracker 前须确认框架的记账范围覆盖这三处同一类事实（§2.1 第 3 条），否则上报点留着、只把读取端换成公共面。
- **文档回写**：`SPECIFICATIONS.md` 附录 A.2 的 **G42** 行改「已闭合（日期＋形态）」、`SPEC.FEAT.WS.03` 现状句与 §7 裁决 **7.126**②就地更正、`CHANGELOG.md` 补一条接货复验版本、`codespec/WS03_MULTI_WINDOW.draft.md` §3 与 §9 第 2 条走查项撤除、`codespec/ARCHITECTURE.md` §2.2 的 `src/ui/` 行去掉 tracker。
- **真机腿**：本仓那条「tracker 与 OS 活动态可能不同」的走查项随本条闭合改写为「框架的判定与 OS 活动态是否一致」，仍需真机读数一次；Windows 腿未编译未实测，接货后不得据 POSIX 绿称 Win32 可用。
- **变异自证**：m1 解析器恒回主窗 → B 窗的快捷键落进主窗那一张标签（红）；m2 恒回「最近派发」但不 `forget` → 关一扇窗后命令落空、另一扇不动（红）；m3 落点取自焦点控件而非派发来源 → 焦点在搜索框时的分屏命令落错窗（红）。

---

## 6 工作量评估与优先级

框架侧新增：甲＝三处改签名（含全仓调用点迁移与兼容策略），乙＝一枚成员 + 派发处一行记账 + 一个 public getter。

- 走 **乙** 路：约 20–40 行 + 三份用例 ⇒ **小到中**，非破坏，本仓可原样撤代偿。
- 走 **甲** 路：闭包签名变更要过全仓与示例，且要决定是否保留无参重载 ⇒ **中**，语义上最贴「快捷键作用于哪一扇窗」，日后还能在同一 context 里带更多派发信息。

**本仓优先级**：**中**——不阻塞 `SPEC.FEAT.WS.03` 的落地（本仓代偿已落，七批已于 2026-10-21 收口），阻塞的是「代偿能否撤除」与真机走查清单里那一条。按 AGENTS §5.1 的类别分流，本条属事件链路（裁决 7.13①），故走「Aurora 补公共 API + 单测 + 该仓文档回写，本仓只消费公共头」这一路，本仓不私挂分叉、不在应用侧自算派发来源的权威态。
