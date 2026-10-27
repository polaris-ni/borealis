# UI_QUAKE_TERMINAL.draft.md — `SPEC.FEAT.WS.12` Quick Terminal：贴边矩形折算与显隐意图状态机（批 1＝纯逻辑件；装配腿等 G47）

> 状态：**批 1 已落地（2026-10-26，裁决 7.160）**；装配腿**挂起等框架回货**（缺口 **G47**，任务书 `temp_tasks/FRAMEWORK_TASK_WS12_QUAKE.md`）。
> 本稿由人不在场时的自主推进产出，D 项全部**自裁**并写明理由与被拒形态，人回来看 §5 即可逐条推翻。

## 1 需求复述（原文口径）

`SPEC.FEAT.WS.12`：全局热键呼出/隐藏的下拉式终端（Windows Terminal / Guake 语义）；原列两项依赖＝OS 级全局热键注册（G9，**已闭合**）与「无边框贴边窗口形态」。
需求文没写的、本稿不自加的：动画、多显示器各自一份、透明度、逐 profile 绑定。

## 2 依赖现状（2026-10-26 对 Aurora 活动分支读源实测）

 quake 形态拆成四条腿，**两条今天可用、两条没有公共入口**：

| 腿 | 现状 | 落点 |
|:---|:---|:---|
| 全局热键（无焦点也触发） | 可用：`Application::os_hotkeys()`，且命中由 `step_frame()` 内部排空，**应用侧不必自调 `drain_pending()`** | 装配腿（待接线） |
| 显示器工作区枚举 | 可用：`app::list_displays()` / `primary_display()` 给物理像素 `work_area` 与 `scale_factor` | **本批：贴边矩形折算的输入** |
| 无边框 | 可用：`WindowStyle::frameless`（建窗期一次定档，quake 不需要运行期改） | 装配腿（待接线） |
| **运行期显隐** | **缺口**：`WindowVisibility::Hidden` 只在构造期生效一次，`SW_HIDE`/`SW_SHOW` 无运行期入口；`raise()`/`focus_window()` 都不改可见性 | **G47 §2.1** |
| **运行期置顶** | **缺口**：`always_on_top` 只在构造期落 `WS_EX_TOPMOST` | **G47 §2.2** |

原「无边框贴边窗口形态未落」这个口径**部分过期**：无边框、置顶初值、工作区枚举、贴边几何自算全部可用，真正缺的是「运行期把一扇已存在的窗藏起来再唤回」与「运行期改置顶」。**夹取不构成缺口**（工作区可读，策略归应用侧）。

## 3 本批交付什么、为什么不硬凑代偿

框架的两条硬缺口都在装配层，本批 therefore 只落**与缺口无关、且回货后不重写**的那半：

1. `ui::quake_target_rect()` —— 贴边目标矩形折算（物理 origin + 逻辑 dp size 双份输出）。
2. `ui::QuakeToggle` —— 显隐意图状态机（含热键与失焦的竞争裁决）。

**被否的代偿**：用 `Window::minimize()` 冒充隐藏。三条理由——① 任务栏留痕与还原动画是用户看得见的错误形态；② 还原腿依赖 `SetForegroundWindow` 对最小化窗口的顺带还原，本仓未实测（标「推断 / 未验证」），拿它当唯一显隐通道等于把需求压在一个未验证行为上；③ G47 回货后这段整块要删，属裁决意义上的半截实现（AGENTS §4.1 第 3 条）。
**被否的另一半**：现在就加 `ui.quick_terminal.*` 配置键。键的取值入口在装配腿，先落盘就是「有控件无消费方」——那正是上一棒（裁决 7.159）刚为 `terminal.bell` 修掉的缺陷形态。配置六处同步与装配腿同批到货时一起做。

## 4 形态

```
include/borealis/ui/quake.h        // 公共头：不含任何框架类型（AGENTS §2）
  enum class QuakeAnchor : uint8_t { Top, Bottom, Left, Right };
  struct QuakeGeometryRequest { Rect work_area; float scale_factor; QuakeAnchor anchor;
                                float cross_ratio; float along_ratio; };  // work_area 物理 px
  struct QuakeTarget { Point origin; Size size_dp; };                     // origin 物理 px，size 逻辑 dp
  auto quake_target_rect(QuakeGeometryRequest) -> QuakeTarget;
  class QuakeToggle { ... }   // 两态 + 意图输出
src/ui/quake.cpp
tests/unit/utest_ui_quake.cpp
```

装配腿（回货后）只做三件事：读 `settings` → 拼 `QuakeGeometryRequest` → 把 `QuakeToggle` 的意图接到 `set_visible` / `set_position` / `set_size` / `focus_window`。**单位换算不在装配层**（见 D2）。

## 5 自裁决策（D 项）

**D1 边缘档取四边，缺省 `Top`**。业界下拉式（Guake / Yakuake / Tilda）缺省都从上边落下，但 Tilda 与 OmniWM 明写可配边——四边成本为零（同一公式换轴），故一次给全。被拒：只做 Top（下一条棒要改接口）。

**D2 输出双份：`origin` 物理 px + `size` 逻辑 dp**。判据是框架**实现**而非头注释：`set_position` 直传 `SetWindowPos`（物理 px，注释与实现一致），`set_size` 先 `to_physical()` 再补非客户区（**逻辑 dp 客户区**，注释写「物理像素外框」是错的，已随 G47 §4 报缺陷）。把换算收在纯逻辑件里，装配层就没有混单位的机会——本仓踩过这个坑（绘制侧 dp/px 边界）。被拒：只输出物理 px（逼装配层自己 ÷scale，正是最容易错的那一步）。

**D3 尺寸语义＝比例的乘积，`cross_ratio` 作用于长边方向、`along_ratio` 作用于落下方向**，两者各夹 `[0.05, 1.0]`（下界＝别让一扇 0 宽的窗存在）。缺省 `Top/Bottom` 时宽＝`work_area.width × cross_ratio`、高＝`work_area.height × along_ratio`；`Left/Right` 时换轴。缺省值 **1.0 / 0.5**：跨边占满（下拉式的天然形态，宽度不满就失去「整幅帘子」的意义），纵深一半（业界普遍落在 1/3～2/3 之间，一半是无人可调时最不刺眼的中间值——**此数为设计判断，非从各家默认抄得，标推断**）。

**D4 居中只在跨边方向，落下方向贴边**。`Top` 档 `origin.y == work_area.top`（不是 `bounds.top`——任务栏不遮挡终端，这是 `work_area` 存在的意义）。被拒：贴 `bounds`（Windows 下会盖任务栏，业界一致避开）。

**D5 落位显示器＝主屏**（`primary_display()`）。被拒：「指针所在屏」——框架没有「当前指针落在哪块屏」的查询面（`display_containing(Point)` 要调用方自己有点，而 `Point` 要从平台消息里取），为它加装配层平台腿属 §4.5 第 23 条的平台假设渗入。该需求若真要做，另立框架候选项，不混进 G47。

**D6 显隐状态机两态（`Hidden` / `Shown`），`on_hotkey(now)` 与 `on_focus_lost(now)` 各返回一个可选意图（`Show` / `Hide` / `None`）**。这是本件的全部价值所在：
- **热键与失焦的竞争必须显式处理**。Tilix 的 quake 模式有个被上游 issue 记录的真实故障——热键隐藏窗口后它自己立刻弹回，因为「隐藏」让窗口失焦、失焦又触发「自动隐藏」路径的重新显示判定。本件的解法是**过渡忽略窗**：任何由本件自己发起的显隐都置 `ignore_focus_until = now + kFocusSettle{250ms}`，其间 `on_focus_lost` 返回 `None`。250 ms 是「OS 焦点稳定」的经验量级（**未实测，标推断**；做成构造参数，装配层可从配置给）。
- **不做计数、不做堆栈**：意图是幂等的（`Shown` 再收到 Show 返回 `None`），避免「连按两次热键卡在中间态」这类 Guake 老 bug。
- 被拒：把状态机写成布尔翻转（`shown = !shown`）——那等于把「失焦自动隐藏后热键应该显示」这条依赖当前态的分支留给装配层，正是要下沉的逻辑。

**D7 自动隐藏（失焦即藏）缺省**开、可关**。业界分歧：Guake 系把它当卖点、Tilix 因它出过上述故障而有人长期关掉。取「开」是因为 quake 语义的核心承诺就是「不占用视线」，取「可关」是因为关掉它是那些产品里的一条正经设置。状态机里体现为 `auto_hide_on_focus_lost` 构造参数。

**D8 去抖不放在本件**。热键是 OS 层注册的，重复触发由 OS 与用户手势决定；本件的幂等性（D6）已经吃掉「同态重复意图」，再加时间窗去抖只会把「快速连按＝开-关-开」的正常意图吞掉。被拒：抄铃脚的 200 ms 节流形态（那是同一物理源重复的问题，与这里不同因）。

## 6 用例与自证

`utest_ui_quake`（无头、纯逻辑，AGENTS §4.4 第 20 条）：四边各一条几何断言（含 150% 缩放下的 `size_dp` 反算）、`work_area` 非零原点（副屏在左/在上时的负原点已在框架 `Display` 口径内，本件不特殊化）、比例越界夹取、幂等意图、过渡忽略窗内失焦被吞、忽略窗过期后失焦生效、`auto_hide` 关时失焦永不 `Hide`、热键从 `Hidden` 出 `Show`。
**变异自证**（§4.4 第 22 条）：① 把 `Top` 档的 `origin.y` 从 `work_area.top` 改成 `bounds.top` 语义（用 `along` 的另一端）→ 上边贴边案红；② 去掉过渡忽略窗的置位 → 竞争案红；③ 把 `size_dp` 的 `÷ scale_factor` 删掉 → 150% 案红。每条指名红案，回改后逐字节复原并复跑。

## 7 交框架的接线面（G47 回货后）

见任务书 §5。本仓这一侧只欠一段装配代码与配置六处同步，`ui::quake` 两件不动。

## 8 边界与不称可用

- 本批**没有任何用户可见行为变化**：无新键、无新面板行、无装配接线。说「WS.12 已落」是错的——落的是可独立测的两件与一份依赖账。
- 真机走查未做（锁屏会话下 `SendInput` 静默失效，裁决 7.31①）；G47 未回货前也**无法**走查 quake 形态。§4.4 第 33 条：本条不替 WS.12 摘「可用」。
- 走查清单（回货后由人执行，7 项）：① 无焦点时按热键能从任意应用呼出；② 呼出的窗口压在所有窗之上；③ 失焦自动隐藏即时生效且不弹回（Tilix 那类）；④ 再按热键能收回；⑤ 任务栏不出现重复条目；⑥ 缩放 150% 下矩形与屏幕工作区贴合（不越界、不缩一号）；⑦ 副屏为主屏/非主屏两种拼接下各跑一次 ①③。
- 在册延后：动画、透明度、逐 profile 绑定、指针所在屏（需框架补查询面）、与 `SPEC.FEAT.WS.03` 多窗口的退出策略交互（`ExitPolicy` 与隐藏窗口的关系——隐藏窗口不该被当「还有窗口」而永不退出，这是装配腿要显式定的第一条口径）。
