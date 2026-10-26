# FRAMEWORK_TASK_LAZYLIST_SCROLL.md — 虚拟列表的三条公共面缺口：条目内容刷新、末尾锚定、偏移变更通知

> 本文件是 Borealis `SPEC.FEAT.CONN.05` 串口监视卡二期（滚动回溯与历史窗口）在 Aurora 框架侧的唯一依赖项的派发任务书，登记为缺口 **G46**（`codespec/SPECIFICATIONS.md` 附录 A.2；裁决 **7.158**）。设计稿 `codespec/UI_SERIAL_MONITOR.draft.md` §9.7 是本条的消费动机。
> 依据：对 Aurora 活动分支的**读源实测**（2026-10-26），涉及 `include/aurora/widget/lazy_list.h`、`include/aurora/state/signal_view.h`。只读源码、未编译未真机，涉及后端行为的句子一律标「读源」。
> **结论先行**：三条都不是「没实现」，而是「实现了但没有公共入口」——live 条目的刷新被 `on_layout` 的「只建缺项」挡住；「落到末尾」藏在 `deserialize_props` 的 `pending_offset_` 里；偏移信号只有 `Effect` 订阅一条路。Borealis 三条各有代偿（整卡重建浮层、极大偏移 JSON、`tick()` 轮询），回货后逐条删除。

---

## 1 框架侧既有能力清册（读源实测）

| 能力项 | 位置 | 现状 |
|:---|:---|:---|
| 改总项数 | `LazyList::set_count` | 下标仍在新范围内的**存活条目不重建**（其内部状态原样保留），只有滚出窗口或超出新范围的被回收；条目减少时夹取偏移并标脏。语义正确，但**不能用来刷新内容** |
| 换条目构建器 | `LazyList::set_item_builder` | 只赋值并 `mark_needs_layout()`；`on_layout` 对已在 `live_` 里的下标**直接复用**，不会重跑 builder ⇒ 已实例化条目继续显示旧内容 |
| 布局建项 | `LazyList::on_layout`（读源） | 先按 `visible_range()` 回收滚出项，再对窗口内**缺失**的下标调用 builder；`live_` 中已有的一律保留 |
| 程序化滚动 | `LazyList::scroll_to` / `scroll_to_item` | 目标值一律按 `max_scroll_offset()` 夹取；而 `max_scroll_offset()` 依赖 `viewport_height_`，**首次布局前为 0** ⇒ 布局前调用二者都只会落在 0，无法表达「落到末尾」 |
| 偏移的「待布局后应用」通道 | `LazyList::deserialize_props` 的 `scroll_offset` | 写进 `pending_offset_`，`maybe_restore_scroll()` 在**首次可滚动布局**时夹取应用，且优先于 `restore_key`。这是目前唯一能在建树期表达「首帧落底」的公共入口，但它借用的是序列化面 |
| 偏移信号 | `LazyList::offset_signal()` | 懒创建 `State<float>`，滚轮／滑动帧／程序化写入时更新；返回 `SignalView<float>&` |
| 信号订阅面 | `SignalViewBase::subscribe(Effect&)` / `read()` | **只有 Effect 一条订阅路**；没有 `std::function` 回调，也没有可注销的连接句柄。`LazyList::collect_signals` 返回空（不向控件树暴露该信号）⇒ 应用侧只能自轮询 `scroll_offset()` |
| 可见范围 | `LazyList::visible_range()` | 含 `cache_extent` 的半开区间；空表/零视口为 `(0,0)`，轮询判据可用 |

**Borealis 消费现状（代偿）**：`ui::SerialMonitorPanel` 每次跟尾重建都整卡摘掉浮层再建（`refresh_overlay()`），只为让 live 条目吃到新行文本；首帧落底靠 `deserialize_props({"scroll_offset": <极大值>})`；冻结/恢复靠 `tick()` 里比较 `scroll_offset()` 与 `max_scroll_offset()`。三条代偿都在工作，代价分别是：每次数据变化重建整棵可见子树、借用序列化面写运行期意图、恢复依赖下一帧的 `tick()`。

---

## 2 缺的三条腿（每条两条可接受形态）

### 2.1 条目内容刷新

**甲＝显式失效（推荐）**

- `LazyList::invalidate_items()`（命名由 Aurora 定）：回收**全部** `live_` 条目、标布局脏；下一帧 `on_layout` 按当前窗口与当前 builder 重建。效果等价于应用侧「整卡重建」的内聚版。
- 语义口径：只在**下一帧布局**生效（不在调用栈内建树）；`count` / `offset` / `scroll_offset` 不动；焦点落在被回收条目上的处置与 `set_count` 回收**同口径**（焦点失效，`FocusManager` 持弱引用）；主线程。

**乙＝builder 带刷新语义**

- `set_item_builder(builder)` 或其新重载在赋值时**同时重建存活条目**（而不是只标布局脏）；或给 builder 一个 revision 参数，revision 变即重建 live。
- 若选乙，「`set_count` 保值语义」与「换 builder 即重建语义」的边界必须在头注里写清：前者动总数、后者动内容。

### 2.2 末尾锚定（布局前可表达）

**甲＝一次性待定偏移（最小形态）**

- `set_pending_scroll_offset(float)`，或 `scroll_to_end(bool animate = false)`。语义：布局前调用先记下，**首次可布局时按当时的 `max_scroll_offset()` 夹取并应用**；与 `deserialize_props` 的 `pending_offset_` 走同一条路径与同一优先级（显式意图 > `restore_key`）。布局后调用等价于 `scroll_to(max)`。
- 这是 Borealis 的最低需求：面板每次重建浮层都能在建树期把「贴底」表达成意图。

**乙＝持续锚定（可选增强）**

- `set_scroll_anchor(ScrollAnchor::End)`：内容或视口变化时自动保持贴底，直到用户手势或显式 `set_scroll_offset` 取消。这是聊天/终端类「跟尾」的天然语义。
- 若给乙，取消条件（用户滚轮、程序化 set、reduce-motion 下的行为）与 snap/glide 的关系必须一并定义；本仓**不替框架选路**，甲乙任一都能闭合本条。

### 2.3 偏移变更通知

**甲＝回调**

- `set_on_scroll_changed(std::function<void(float)>)`：偏移**实际变化**时调用一次；同一值的重复 `set_scroll_offset` 不触发；来源不限（滚轮／滑动帧／程序化／无障碍滚动）；主线程；控件销毁即失效。

**乙＝通用订阅**

- `SignalView` 增一条 `std::function` 订阅（返回可注销的 `Connection`），或在 `LazyList` 上暴露带注销句柄的 `offset_changed()`。现有的 `subscribe(Effect&)` 面不动。

### 2.4 无论选哪条都必须一并定的口径

1. **只在下一帧生效**：刷新与锚定都不得在调用栈内重建/跳转（回调栈里可能正在派发被回收的控件，G34 一族）。
2. **主线程**：三条都只在主线程读写；不引入后台线程。
3. **不改既有语义**：`set_count` 的存活保序、`restore_key`、snap/paging、无障碍滚动、`visible_range` 全部逐位不变。
4. **空表的边界**：`count == 0` 时末尾锚定落到 0、刷新无条目、通知不因夹取产生伪变化。
5. **不隐藏优先级**：显式锚定与 `restore_key` 的先后必须文档化（建议沿用 `pending_offset_` 既有的「显式 > 恢复」）。

---

## 3 回货判据（框架侧用例，Headless 即可）

1. **刷新**：100 行、视口 10 行；首布局后改数据闭包并触发刷新 ⇒ 下一帧可见项文本为新值、`live_item_count` 仍为窗口量级、`scroll_offset()` 不变。
2. **末尾锚定**：建树期设锚定再挂树 ⇒ 首次布局后 `scroll_offset() == max_scroll_offset()`；空表为 0；布局后追加 `count` 再次锚定生效且只跳一次；与 `restore_key` 同时存在时按文档化的优先级取值。
3. **通知**：滚轮一次、滑动若干帧（每帧一次）、程序化 set、无障碍滚动各触发恰一次；同值 set 不触发；注销后不再触发；回调在主线程。
4. **既有行为不动**：`set_count` 存活保序、`restore_key` 恢复、snap、`visible_range` 与无障碍三量读数逐位不变，既有用例全绿。

---

## 4 本仓接线面（回货后）

- `ui::SerialMonitorPanel::refresh_overlay()` 不再为「行内容变」而整卡重建：跟尾帧只改快照与 `set_count`，再经 2.1 的刷新入口让 live 条目吃到新内容；浮层与暂停/冻结状态不随数据重建。
- `build_overlay()` 里 `deserialize_props({"scroll_offset": <极大值>})` 的技巧改为 2.2 的锚定入口；那个 JSON 常量删除。
- `tick()` 里比较 `scroll_offset()` / `max_scroll_offset()` 的轮询改为 2.3 的回调置 `rebuild_pending_`；「滚到底当帧解冻」以回调为准，帧驱动的兜底最多保留一帧。
- 验收：面板既有 11 案判据不改、全绿；以用例计数证明跟尾帧不再走 `refresh_overlay()`。
- 闭环节点：附录 A.2 的 G46 行标「已闭合」、裁决 7.158 补记回货形态、`PLAN.md` 对应注记同步。

## 5 不做什么

- 不做条目复用池、可变行高、横向虚拟化（本节只补三条公共入口）；
- 不在框架内做「跟尾／冻结」策略——那是监视卡的显示状态机；
- 不把 `offset_signal()` 改成每帧广播；通知只在偏移真正变化时发一次；
- 不引入后台线程、平台后端改动或像素级依赖。
