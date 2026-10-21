# FRAMEWORK_TASK_WS03_G43.md — `WindowGeometry::mode` 有存无取在 Aurora 框架侧的可派发任务书

> 本文件是 Borealis `SPEC.FEAT.WS.03`（多窗口，逐窗几何记忆那一腿）在 Aurora 框架侧的依赖分析与回货判据。
> 依据：Borealis 对 Aurora 活动分支 `include/aurora/app/window_geometry.h`、`include/aurora/window/window_state.h`、`include/aurora/window/window.h`、`include/aurora/app/window_host.h`、`include/aurora/app/application.h` 与 `src/aurora/app/{application,window_geometry}.cpp` 的**读源实测**（2026-10-21，WS.03 批 5 开工盘点，读数登记在 `codespec/WS03_MULTI_WINDOW.draft.md` §0 的 F26，其中第 ③ 条即本缺口）。MSVC／Win32 腿未编译未实测，涉及后端行为的句子一律标「读源」。
> **结论先行**：几何的**保存腿写了 `mode`、序列化写了 `mode`、反序列化还校验 `mode`，唯独恢复腿不读它**。`capture_host_geometry` 取 `Window::window_mode()` 入快照，`window_geometry_to_json` 把它写成六键之一，`window_geometry_from_json` 对越界的 `mode` 判格式错误，而 `apply_host_geometry` 只下发 `set_size` ＋ `set_position` ⇒ 「最大化／全屏那一档重启」回不到那一档，而落盘文件里那一格白纸黑字，读者会以为功能存在。**这不是缺 API，是缺一条消费点**。
> 登记形态见 `codespec/SPECIFICATIONS.md` 附录 A.2 的 **G43** 行与 §7 裁决 **7.126**⑦；本仓侧的存储与键分配在 `src/config/window_geometry_store.{h,cpp}`（`SPEC.FEAT.WS.03` 批 5，`WindowOptions::persist_id` + `Application::set_window_geometry_store()` 全用框架现成接缝）。

---

## 1 框架侧既有能力清册（无需补全）

| 能力项 | 位置 | 现状（读源实测） |
|:---|:---|:---|
| 几何结构 | `include/aurora/app/window_geometry.h` 的 `WindowGeometry` | `origin`（物理像素）／`size`（物理像素）／`mode`（`WindowMode`）／`display_id`；注释自述「最大化/最小化/全屏需一并记忆」 |
| 几何态枚举 | `include/aurora/window/window_state.h` 的 `WindowMode` | `Normal`／`Maximized`／`Minimized`／`FullScreen` 四值，`to_string` 齐备；与 `WindowState`（可见性）正交 |
| 序列化 | `window_geometry.h` 的 `window_geometry_to_json` | 六键稳定：`origin_x`／`origin_y`／`width`／`height`／`mode`／`display_id` |
| 反序列化 | 同上 `window_geometry_from_json` | 键缺失或类型不符回 `nullopt`；**`mode` 越 `[0,3]` 也判格式错误** ⇒ 读取端认得这一格 |
| 可用性判定 | 同上 `is_window_geometry_usable` | 宽松口径：尺寸为正且与任一显示器工作区**有交集**即算可用（部分越界不拒）；完全屏外／尺寸非正回不可用 |
| 存取到 `Preferences` | 同上 `save_window_geometry` / `load_window_geometry` | 两枚重载（顶层键与 `group(...)`）；「格式错误」与「几何不可用」两类失败各留一条 WARN 以区分 |
| 保存腿 | `src/aurora/app/application.cpp` 的 `capture_host_geometry` | 读 `Window::surface().position()`、`Window::size()` × `scale_factor()`、**`Window::window_mode()`**、`surface().display_id()`；调用点在 `reap_closed` 的收集步（Surface 仍有效、宿主未销毁），`WindowHost::geometry_saved()` 做幂等守卫 |
| 恢复腿 | 同上 `apply_host_geometry` | `sf.set_size(Size{g.size.width / scale, ...})` ＋ `sf.set_position(g.origin)`——**`g.mode` 全程无人使用** |
| 恢复入口 | 同上 `restore_geometry_for` | 两跳：`register_host` 尾部一次、`set_window_geometry_store` 对已登记宿主补做一次；无存储／无 `persist_id`／无窗口／几何不可用即 no-op |
| 几何态的操作原语 | `include/aurora/window/window.h` | `minimize()`／`toggle_maximize()`／`set_fullscreen(bool)` 三枚 public，且有 `window_mode()` 读、`set_window_mode(WindowMode)` 写；`WindowHost::display_id()`／`move_to_display(int)` 齐备 |

**Borealis 消费现状**：几何的键由本仓按**创建序**分配（`main` 与 `window-2` 起，`WindowId` 只用于活体寻址、不作键），存储落在配置目录的 `windows.json`（刻意**不进** `config::Settings` schema、**不进** `SPEC.FEAT.PREF.07` 导出件——本机屏幕相关值跨机导出即坏值，裁决 7.126 的稿 D9）。落盘时机归调用方：任一窗关闭后一次、`run()` 返回后再一次（后者覆盖「最后一扇被框架保留、不走 `set_on_window_closed`」那一档）。证人＝`utest_window_geometry_store` 六案（含「写进 `group("windows")` 的那一格框架自动链取不到」这一条陷阱证人）＋ `itest_workspace_window` 批 5 段四案（各窗取自己那一格、无格者保持默认、恢复落在首帧之前、保存写回它自己那一格）。**`mode` 那一格现在每次都写进文件、每次都被恢复腿忽略**，本仓不补偿（第 3 节）。

---

## 2 缺的那一条腿（两条可接受形态）

**甲＝恢复腿按 `mode` 追一次几何态**
在 `apply_host_geometry` 里消费 `g.mode`：`Maximized` 走最大化、`FullScreen` 走全屏、`Normal` 保持现行为。三条必须同时定死，否则半截实现比不做更坏：

1. **与 `set_size` 的先后**：最大化／全屏之后窗口尺寸由 OS 决定，`set_size` 必须让位（否则两跳互相拉扯，实测表现为「恢复成最大化前的那份尺寸」或闪烁）。建议形态＝`mode != Normal` 时只下发位置与态、不下发尺寸，或先态后尺寸并由后端自决。
2. **`Minimized` 的处理要显式表态**：`WindowMode::Minimized` 与 `WindowState::Hidden` 是耦合的（`window_state.h` 注释自述），把「最小化」恢复成「最小化」意味着启动即看不见，恢复成 `Normal` 又与文件里那一格不符。请框架选定一档并在文档里写死，**别留给调用方猜**。
3. **降级路径**：`display_id` 指向的显示器已不在时按现有 `is_window_geometry_usable` 那条宽松判定回退——本条要求「回退后 `mode` 一并归 `Normal`」并留一条 WARN，而不是带着旧显示器坐标去最大化。

**乙＝明确 `mode` 不参与恢复，把它从结构里去掉**
`WindowGeometry` 删 `mode` 字段、`window_geometry_to_json` 少写那一键、`window_geometry_from_json` 不再校验它（旧文件的多余键忽略并留 WARN 以兼容），`capture_host_geometry` 不再读 `window_mode()`。这样落盘文件与代码行为一致，读者不会被那一格误导。

**本仓不替框架选路**：本仓期望的是甲（需求 `SPEC.FEAT.WS.03` 的几何记忆本意含「最大化那一档也要回得来」，稿 D9 已把它列为真机读数项），但乙同样可接受——它至少消除「文件里写了而功能不存在」这个误导面。**不接受的形态＝只在文档里加一句「`mode` 目前不恢复」**：字段照存、行为照旧，等于把误导从代码搬进注释。

**为什么本仓不自己补这一腿**（在册理由，供框架侧判断「应用侧真的做不到」是否成立）：在装配层私调 `toggle_maximize()`／`set_fullscreen()` 要猜框架时序——恢复发生在 `register_host` 尾部（首帧之前），那一段没有公共入口可挂，晚一帧补调就是「先按普通尺寸上一帧再最大化」的可见跳动；且这属于应用侧改框架的销毁／装配路径，撞 Borealis AGENTS §5 的「不在应用侧私改框架路径」与 §4.1 第 3 条「不做半截实现」两头。

---

## 3 回货判据（四条）

1. **写读对称**：`mode` 这一格被写进存储的每一份文件都必须被恢复腿消费（甲），或者根本不再被写（乙）。判据形态＝落盘内容与重启后的 `Window::window_mode()` 逐项一致，而非「文件里有那一格」。
2. **无头可证**：`HeadlessSurface` 那一档没有 OS 几何语义，`apply_host_geometry` 在 `mode != Normal` 时必须**安全 no-op 或只记 `set_window_mode`**，不得报错、不得让宿主被判为不可用——本仓与框架的用例都要能在无头面跑这条。
3. **不被尺寸拉扯**：恢复为 `Maximized`／`FullScreen` 后，同一趟恢复里的 `set_size` 不得把窗口拉回快照里的那份尺寸（判据：重启后 `window_mode()` 与 `size()` 的自洽性由后端决定，框架需给出明确口径并有用例钉住）。
4. **降级不留脏态**：目标显示器不在、或恢复的坐标完全屏外时，回退默认布局那一跳必须把 `mode` 一并归 `Normal` 并留 WARN，不得「默认尺寸 ＋ 全屏」这种组合。

---

## 4 配套用例（框架侧）

- `utest_window_geometry_mode_roundtrip`：`save_window_geometry` 写 `Maximized` → `load_window_geometry` 读到 `Maximized`（这条现在就应该绿，证的是序列化，不是消费点）。
- `itest_apply_host_geometry_consumes_mode`：两扇无头宿主，先把 `mode` 置 `Maximized` 落盘，重建后经 `set_window_geometry_store` 恢复，断 `Window::window_mode()` 回到 `Maximized`；同案跑 `FullScreen` 与 `Normal` 三档。
- `itest_geometry_mode_minimized_policy`：按 §2 甲 第 2 条选定那一档写死断言（`Minimized` 落盘后重启回什么态），使「选择」成为可回归的事实而不是注释。
- `itest_geometry_display_gone_degrades_mode`：存储里的 `display_id` 指向不存在显示器 ⇒ 回默认布局、`mode` 归 `Normal`、一条 WARN 可归因。
- 若走乙：`utest_window_geometry_schema_has_no_mode`——旧文件带 `mode` 仍解析成功（多余键忽略）、新文件不再写那一键。

---

## 5 本仓接货复验形态

- **接货后本仓改动＝0 行**（甲／乙任一）：本仓从不读 `WindowGeometry::mode`，也不用框架的 `save/load_window_geometry`，几何全走 `WindowOptions::persist_id` + `Application::set_window_geometry_store()` 那条自动链。这一点是这条缺口登记的价值——**消费侧不需要再等应用侧接线**。
- **文档回写**：`SPECIFICATIONS.md` 附录 A.2 的 **G43** 行改「已闭合（日期＋形态）」、§7 裁决 **7.126**⑦就地更正、`CHANGELOG.md` 补一条接货复验版本、`codespec/WS03_MULTI_WINDOW.draft.md` 撤两处「刻意不做」（§0 的 F26③、§6 批 5 的第 5 条与 §10 批 5 补记第 4 条、§21 ⑹、§23⑸）并把 §9 第 4 条①那条真机读数从「确认回不来」改写为「确认回得来」。
- **真机腿**：这条**必须真机读数一次**（POSIX 三后端与 Win32 各一次）——无头面证的是「消费点存在」，最大化/全屏在 WM 与 compositor 下的实际尺寸与动画只有真机能答；且本仓 §9 第 4 条已记明 Linux 三后端的 `scale_factor` 不随改缩放或跨屏更新（Aurora 主仓 `codespec/specification/06-app-platform.md` §2.4 明写），故恢复按旧值折算那一档与 `mode` 是否恢复是**两条独立偏差**，不得混成一个读数。
- **变异自证**（接货当批，本仓侧）：m1 存储文件里的 `mode` 手动改写成越界值 → 框架判格式错误、回默认布局且不崩（证 §2 的校验仍生效）；m2 若走甲，把某窗那一格 `mode` 置 `Maximized` 重启 → 该窗最大化而另一窗不受影响（逐窗隔离的证人）；m3 若走乙，旧 `windows.json` 带 `mode` 键启动 → 忽略多余键、位置尺寸照复（兼容证人）。

---

## 6 工作量评估与优先级

框架侧新增：甲＝`apply_host_geometry` 里按 `mode` 追一跳 ＋ 三条口径定死（尺寸让位、`Minimized` 政策、屏外降级）＋ 一份集成用例 ⇒ **小**（改动集中在同一枚函数内，无公共签名变更）；乙＝删一枚字段并处理旧文件的多余键 ⇒ **小到中**（结构体是公共类型，删除是破坏性变更，要走 Aurora 的编号与文档回写流程）。

**本仓优先级**：**低-中**——不阻塞 `SPEC.FEAT.WS.03` 结项（批 1–批 7 已于 2026-10-21 全落，几何的「尺寸与位置逐窗记得回」这一档已可用），阻塞的是「最大化那一档回不回得来」这条用户体验，以及 §9 走查清单里一条只能真机答的读数。按 AGENTS §5.1 的类别分流，本条属渲染／窗口装配链路（裁决 7.13①），故走「Aurora 补公共行为 + 单测 + 该仓文档回写」这一路，本仓不自建第二份几何恢复腿。
