# Pane 缩放设计稿（`SPEC.FEAT.WS.08`）

> **状态**：**评审收口（2026-10-21）**——用户离场，常存授权「按照顺序持续推进，有任何疑问现在澄清，后续再遇到问题则你自行按照推荐去做」把 AGENTS §4.1 第 2 条的「等开工指令」替换为「按推荐自决 + 入册」。§3 的 D1–D11 全部取推荐档，逐条理由与代价见下。裁决入 `codespec/SPECIFICATIONS.md` §7 为 **7.120**，版本记 `codespec/CHANGELOG.md` **v0.120**。
> **实现落地（2026-10-21，裁决 7.120）**：纯逻辑件 `a76f508`、界面腿 `fa1e84c`、集成六案 `004e04a`、本笔文档回写。结项读数 `ctest --preset linux -E etest_` **86 项**、`borealis_test_runner` 全跑 **1107 案**全绿。**§5 末条的真机手感未走查 ⇒ 本稿与裁决一律不称「可用」**（AGENTS §4.4 第 33 条）；Win32/MSVC 腿未编译验证，WT 缺省键仍为推断／未验证。
> **与屏 1 的关系**：`codespec/UI_WORKSPACE.draft.svg` 屏 1 的注记「pane 最大化入口（`SPEC.FEAT.WS.08`）／悬停右上角 ⤢；缩放态下其余 pane 隐藏并保留角标」是本稿唯一的视觉上游。本稿把它落成**可量、可断言的几何与状态**，不另画一版终态图：这一棒只新增一个 20 dp 的角标与一种布局态，文字判据足以复核。屏 1 上被画的其余部分（标签条、把手、焦点描边）沿用 `codespec/UI_WORKSPACE_INTERACT.draft.md`，本稿不改它们。
> **上游依据**：`codespec/SPECIFICATIONS.md` 的 `SPEC.FEAT.WS.08` 原文（「快捷键将当前 pane 临时最大化/还原（zoom/unzoom，Windows Terminal/Tabby 语义）；缩放态视觉标识」）；`codespec/PLAN.md` 的 M4 行；屏 1 注记；`ui::PaneTree` 的布局与焦点面（裁决 7.42）、`ui::workspace_command` 的派发通道（裁决 7.47② / 7.51③）、`ui::WorkspaceView` 的三条实测口径（`src/ui/workspace_view.h` 文件头）。

---

## 0 地基实测（2026-10-21，POSIX 腿；Windows/MSVC 一律标推断／未验证）

本棒的形态由下面十条读数定形。引用按「相对路径 + 符号/章节」，不写行号。

| # | 读数 | 对本稿的约束 |
|---|---|---|
| **F1** | **Aurora 没有「最大化 pane」这类原语**。框架的窗口层只有 `open_window` / `close_window` / `windows()` / `WindowEventBus` 与几何记忆（Aurora 主仓 `include/aurora/app/application.h`、`app/window_geometry.h`）；控件层没有「把某一个子控件临时铺满父盒」的公共件。`WorkspaceView` 本来就是自研的绝对定位容器（`src/ui/workspace_view.cpp` 的 `on_layout` 手动给每个 `TerminalView` 紧约束并写 `Node::bounds`），铺满是它已有的能力。 | 缩放**不新增框架缺口**，也不碰 `PaneTree`：树只存拓扑与比例，临时显示态留在 `WorkspaceView`。这与「树不含几何记忆」的既有边界（`include/borealis/ui/pane_tree.h` 文件头）一致——`zoomed_` 不是几何，下一次 `layout()` 仍旧从树算出真相。 |
| **F2** | **`Widget::show=false` 会把尺寸夹成零盒**：`Widget::layout` 在 `show` 为假时直接写 `size_ = {0,0}` 并返回，`Container::on_paint` / `on_hit_test(_chain)` 也不跳过 `show` 为假的子节点（跳过发生在 `Widget::paint` / `Widget::hit_test` 那一层）。 | **不能用 `show` 隐藏 pane**：终端视图的下一帧布局会变成 0×0，`grid_geometry` 塌成 0 列 0 行，正好制造本稿要避免的 resize 抖动。隐藏必须在 `WorkspaceView` 自己的绘制与命中原语里做（D4）。 |
| **F3** | **零 bounds 也不等于不画**：`Container::on_paint` 对每个子节点调 `child.widget().paint(p, bounds.origin + cb.origin, …)`，而 `TerminalView::on_paint` 用传入 `bounds.origin` 定位行、用 `bounds` 铺底；它没有「尺寸为零就提前返回」的分支。`Widget::paint` 也不检查零尺寸（只检查 `show`）。 | 隐藏 pane 不能靠「把节点 bounds 写成零」来省事——那样终端内容仍可能从容器原点画出来。绘制侧必须**显式跳过**隐藏 pane（D4）。 |
| **F4** | **命中链的祖先可认领子控件上的点**：`Widget::hit_test_chain` 把 `on_hit_test_chain` 的返回当作「后代链」；本仓的把手正是靠「缝隙里返回 `{this}`」让 `WorkspaceView` 接管缝隙（`src/ui/workspace_view.cpp` 的 `on_hit_test_chain`，文件头第三条口径）。`Container::on_hit_test_chain` 会反向遍历**全部**子节点。 | 角标落在 pane 内部、叠在 `TerminalView` 之上，必须由 `WorkspaceView::on_hit_test_chain` 在点中角标时**先于**子节点返回 `{this}`，否则 Press 会被终端视图吃掉。缩放态下的命中也要绕过隐藏子节点，不能直接委托 `Container`。 |
| **F5** | **Move 会冒泡到容器**：`TerminalView` 的非拖拽 Move 不置 `is_handled`（`src/ui/terminal_view.cpp` 的 `on_pointer_event`），所以 `WorkspaceView` 能收到 pane 上的 Move；把手 hover 与 `note_framework_focus` 都建立在这条事实上（文件头与 `on_pointer_event` 的注释）。 | 角标的悬停显示**不需要新增事件订阅**，就在现有的 Move 分支里记 `hovered_zoom_`；按下则靠 F4 的命中链改道。 |
| **F6** | **缩放态的真实焦点仍只有一个 pane**：`focused_pane()` 遍历视图问 `is_focused()`（框架焦点是唯一权威，`src/ui/workspace_view.h` 文件头第一条），`last_focused_` 只是兜底。键盘输入只会进持焦视图，故「谁说收到了键，谁就是当前 pane」这条既有证词（`on_key_press`）在缩放态照样成立。 | 「缩放跟随焦点」不需要额外状态：键从哪一格来、或方向命令把焦点交给谁，就把 `zoomed_` 改到谁。隐藏 pane 不可能在缩放态被点中（它不在命中链里），只可能因框架侧改焦点而出现——`on_key_press` 再做一次兜底同步即可（D2）。 |
| **F7** | **会话尺寸的唯一下发口是工作区去抖腿**：`TerminalView` 把期望行列交给 `grid_size_sink`，`WorkspaceView` 入 `GridSizeDebounce` 并在尾沿调 `hooks_.dispatch_grid`（裁决 7.47⑩ / 7.48；`src/ui/workspace_view.cpp` 的 `wire_view` / `arm_debounce`）。`request_grid_size` 只在真变了才请求。 | 被隐藏的 pane 若仍按**原尺寸**布局，它的行列数一格不变 ⇒ 缩放与还原都不会产生隐藏会话的 resize 下发。这正是「隐藏但保持会话网格」能成立的原因（D4），也是集成用例可以断言的端到端证人。 |
| **F8** | **文本需要字体，字形不需要**：`TerminalView` 的通知卡走 `ref_font_` + `settings_label` + `draw_text`（`src/ui/terminal_view.cpp` 的 `paint_unrepresentable_notice` 一族）；`WorkspaceView` 的 chrome 只有 `Painter::fill_rect` 与三个固定色，没有任何字体依赖（`src/ui/workspace_view.cpp` 文件头与常量）。框架 `Painter` 另有 `draw_line` / `stroke_polyline` / `fill_rounded_rect`（Aurora 主仓 `include/aurora/render/painter.h`）。 | 角标的 ⤢ / 收起字形用**线段与圆角矩形**拼，不引入字体与词条：pane 容器不应为了一个 20 dp 图标去依赖字体回退链，也无头用例的像素判据因此稳定。 |
| **F9** | **键位表恰有十二个保留位**：`src/ui/workspace_keys.cpp` 的 `kBindings` 与 `utest_workspace_keys` 的 `kClaimed` 逐条同形；`workspace_key_bindings()` 供快捷键冲突比对（裁决 7.72③）。`Ctrl+Shift+Z` 当前**不在**表内，本仓也没有任何注册命令占用它（`src/main.cpp` 的 `bind_shortcuts` 与框架默认键位均无）；`itest_settings_panel` 里那条 `Ctrl+Shift+Z` 是**孤儿行**，按判据「孤儿不参与比对」呈「延后」而非冲突。 | 新增第十三键的改动面是确定的：`kBindings` + 两处 `utest_workspace_keys` 计数/认领集 + 文档里的「十二条」改口。设置页的冲突标注自动把新键算进保留位，无需第二份表。 |
| **F10** | **Windows Terminal 的 TogglePaneZoom 缺省键未在本沙箱确证**：官方文档页在沙箱内抓取失败，只能确认动作语义（当前 pane 铺满、其余隐藏、再按还原）与它属于 `togglePaneZoom`。Contour / Terminator 一族把缩放放在 `Ctrl+Shift+Z`。 | 语义照 WT，**键位是本仓选择**，在稿面标「推断／未验证」；选 `Ctrl+Shift+Z` 的理由是贴本仓 `Ctrl+Shift` 家族（D/E/W/0）且不与终端内常见的 `Ctrl+Z`（SIGTSTP / undo）撞车。日后若确证 WT 用别的组合，改 `kBindings` 一行即可，判据不动（D1）。 |

---

## 1 范围与边界

**本期交付**：

1. **一条分屏命令与第十三键**：`WorkspaceCommand::ToggleZoom`，绑定 `Ctrl+Shift+Z`；走既有「焦点 pane 的按键前置过滤」通道（裁决 7.47②），不进框架 `ShortcutRegistry`、不进命令面板（与 WS.02 的十二条同通道，D10）。
2. **缩放态布局**：`WorkspaceView` 同时维护两份矩形表——`base_layout_`（树折算的真相，供路由、步进与隐藏 pane 的视图布局）与 `layout_`（显示表：缩放 pane 占满可用区、其余零盒、把手为空）。缩放不改 `PaneTree` 一行。
3. **视觉标识**：缩放 pane 右上角恒显一枚 20 dp 角标（chrome 圆角底 + accent 双箭头，展开/收起两形）；未缩放时角标只在指针进入该 pane 右上角热区时显形；缩放态不画焦点描边（与单 pane 同理，D6）。
4. **鼠标入口**：悬停角标出字形、光标改手型；Press 即「先选该 pane 再切换缩放」。角标在命中链里先于终端视图被 `WorkspaceView` 认领。
5. **与焦点和结构命令的交接**：缩放跟随焦点（方向键换格 ⇒ 缩放换到新格）；切分 / 关闭 / 等分 / 步进一律**先还原再执行**；关闭缩放 pane 自动清缩放；单 pane 时切换为 no-op 但仍认领该键（D2 / D3 / D9）。
6. **观测面**：`WorkspaceView::zoomed_pane()` 返回当前缩放 pane（未缩放为空值），供集成用例断言；`current_layout()` 在缩放态给出显示表（缩放 pane 满盒、其余零盒、无把手）。
7. **无装配改动**：键位在 `WorkspaceView` 内部消化，`src/main.cpp` 一字不动；不新增设置键、不改 schema、不动核对表。

**本期不交付**（§6 在册）：

- 缩放态的**转场动画**（铺开 / 收起都是瞬时换布局；本仓没有布局动画的公共件，也不为 20 dp 的角标新造一条）。
- 缩放状态的**跨重启记忆**（属 `SPEC.FEAT.WS.11`；`zoomed_` 只活在当前标签的 `WorkspaceView` 里）。
- 命令面板里的 `workspace.*` 动作（WS.02 的一批同样未进命令表；这是既有边界，不是本棒新欠的账，D10）。
- 真机走查（§4.4 第 33 条：本条不称「可用」）。

---

## 2 交互与状态

### 2.1 状态机

| 当前态 | 输入 | 结果 |
|---|---|---|
| 未缩放（pane 数 ≥ 2） | `Ctrl+Shift+Z` | 缩放当前焦点 pane；把手与其余 pane 隐藏；角标常显 |
| 未缩放（pane 数 = 1） | `Ctrl+Shift+Z` | no-op；键仍被认领（不发给会话） |
| 缩放 | `Ctrl+Shift+Z` | 还原；`layout_` 回到 `base_layout_`；焦点不变 |
| 缩放 | `Alt+方向键` | 用**未缩放的几何**路由；焦点换到哪一格，缩放跟到哪一格（仍处缩放态） |
| 缩放 | 切分 / 关闭 / 等分 / 步进 | **先还原**，再按未缩放的几何执行；执行完不自动回到缩放 |
| 缩放 | 指针点另一 pane 的角标 | 不可达（其余 pane 已隐藏）；缩放态只有当前 pane 的角标 |
| 未缩放 | 指针进入某 pane 右上角热区 | 该 pane 的角标显形、光标改手型 |
| 未缩放 | 在某 pane 的角标上 Press | 该 pane 取得焦点并缩放；角标随即移到大窗右上角 |
| 缩放 | 在角标上 Press | 还原；指针落点可能已不在该 pane 的新盒内，悬停态清零等下一次 Move |
| 缩放 / 未缩放 | 关闭 pane | 若关的是缩放 pane，先清缩放；其余情况缩放不受影响（键盘路径下焦点恒在缩放 pane） |
| 任意 | 切标签 | 各标签的 `WorkspaceView` 各自保留自己的 `zoomed_`（D7） |

### 2.2 几何（全部为逻辑 dp，缩放因子在绘制时乘）

| 量 | 值 | 说明 |
|:---|:---|:---|
| 角标热区 / 底板 | **20 × 20**，圆角 **3** | 底板即命中区；不另放更大的不可见热区 |
| 角标内缩 | 距 pane 右边与上边各 **4** | 即 `pane.right - 24 .. pane.right - 4`、`pane.top + 4 .. pane.top + 24` |
| 字形盒 | **10 × 10**，在底板内居中 | 1.5 dp 线段，圆帽（`Painter::draw_line` 的既有形态） |
| 底板色 | `#21222C`（chrome 固定 token，裁决 7.25 N6） | 不透明：终端内容在角标下不可见，避免浅色主题下字形读不出 |
| 字形色 | `#BD93F9`（accent） | 与把手 hover / 焦点描边同色 |
| 展开字形 | 一条从中心指向右上、一条从中心指向左下的双箭头（⤢），箭头翼为水平/垂直短线 | 未缩放时的入口 |
| 收起字形 | 一条从左上角指向中心、一条从右下角指向中心的双箭头（内向 ⤡），箭头翼在中心一端 | 缩放态的「还原」入口，与展开形方向相反 |
| 焦点描边 | 缩放态**不画** | 缩放 pane 的边界就是窗口边，画了等于给窗口描边，不携带「哪一格是焦点」的信息 |

### 2.3 角标的命中与绘制顺序

- **命中**：`WorkspaceView::zoom_badge_at(local)` 只遍历**显示表里宽高为正**的 pane 盒；命中即由 `on_hit_test` / `on_hit_test_chain` 返回自身（先于 `Container` 的子节点遍历）。单 pane 工作区不提供角标（没有可放大的对象）。
- **绘制**：底色 chrome → 各可见 pane（隐藏 pane 跳过）→ 把手（缩放态为空）→ 焦点描边（缩放态跳过）→ 角标（最后画，压在终端内容之上）。
- **悬停刷新**：Move 时比较 `hovered_zoom_` 是否变化，变了只标脏绘制、不重排；Press 角标时先 `apply_focus(pane, Pointer)` 再切换缩放。
- **光标**：`hovered_zoom_` 有值 ⇒ `CursorShape::PointingHand`；否则沿用把手的双向箭头逻辑。

---

## 3 决策点（全取推荐档，逐条理由与代价）

| # | 议题 | 选项 | 影响面 |
|:---|:---|:---|:---|
| **D1** | 触发键 | ① **`Ctrl+Shift+Z`**〔建议〕；② `Ctrl+Shift+Enter`；③ 跟随 WT 缺省键 | ③ 无法在本沙箱确证（F10：官方页抓取失败），照抄一个记不住的组合风险更大；② 与终端内常见的「插入换行」类绑定相撞；① 贴本仓 `Ctrl+Shift` 家族、与 `Ctrl+Z` 的 SIGTSTP/undo 不冲突。**代价**：`Ctrl+Shift+Z` 自此不再转发给会话，少数 TUI 若用它做重做/撤销会失效；这是所有「保留位」键位的共同代价，与十二条同档在册。语义照 WT/Tabby，键位为本仓选择，标**推断／未验证**（WT 缺省键） |
| **D2** | 缩放是否跟随焦点 | ① **跟随**〔建议〕；② 缩放固定在原 pane、焦点命令先还原 | ② 会出现「焦点在隐藏格、键盘输入发进看不见的会话」这种最坏状态，或者每次换焦点都被迫退出缩放；① 的代价是方向键连按时缩放格跟着跳，但屏上永远只有「当前格」，语义自洽（F6 的证词足以支撑） |
| **D3** | 结构命令在缩放态的行为 | ① **先还原再执行**〔建议〕；② 在隐藏几何上执行；③ 缩放态禁用结构命令 | ② 的「在看不见的邻居上切一刀」没有反馈，且切完仍缩放会让用户找不到新格；③ 多一条禁用态与提示，而用户的意图本就是「先退出缩放再动布局」；① 只多一次布局折算 |
| **D4** | 隐藏 pane 的会话尺寸 | ① **仍按树的原矩形布局与保持网格**〔建议〕；② 铺满后重新布局；③ 卸载视图 | ② 每次缩放/还原都让隐藏会话 resize 两次，`tmux` 一类应用会重排、回看内容跳动（F7）；③ 会丢会话内容并让挂载/卸载与帧泵的既有生命周期多一条分叉。① 的代价是隐藏会话仍占 CPU 与内存、其输出继续进镜像，但界面不画（F2/F3 的跳过路径），且还原是零等待 |
| **D5** | 布局表的形态 | ① **`base_layout_` 与 `layout_` 两份**〔建议〕；② 只留一份、用标志位在消费点分支 | ② 会让路由、步进、把手拖拽、命中、绘制各自判一次「是否缩放」，五处各写一遍必分叉；① 把「树真相」与「显示真相」分开，路由/步进读 `base_layout_`，绘制/命中读 `layout_`，每处只有一条路 |
| **D6** | 缩放态的视觉标识 | ① **右上角常显 20 dp 角标 + 不画焦点描边**〔建议〕；② 只画焦点描边加粗；③ 在标签条上加一个状态字 | ② 缩放 pane 的描边与窗口边重合，用户读不出「这是缩放态」；③ 缩放是 pane 级状态，搬到标签条上会与标签级状态（活动/BEL/退出）混在一起；① 的角标同时是**还原入口**，鼠标用户不必记快捷键。代价是它盖住右上角 20×20 的终端内容（不透明底板，D6 的取舍） |
| **D7** | 缩放状态的归属与寿命 | ① **`WorkspaceView` 私有成员，按标签独立、不落盘**〔建议〕；② app 级全局一份；③ 进 `Settings` 持久化 | ② 会让两个标签共享缩放，切换标签时出现「这个标签的 pane 被另一个标签放大了」；③ 属 `SPEC.FEAT.WS.11` 的会话恢复，且「临时最大化」的语义与「重启后还放大」相反；① 与 `WorkspaceView` 的生命周期天然一致（切标签保留、关标签丢弃） |
| **D8** | 角标只在缩放态常显还是两态都常显 | ① **缩放态常显、未缩放态悬停显**〔建议〕；② 两态都常显；③ 两态都只悬停显 | ② 每个 pane 右上角常驻一个按钮，日常分屏的终端内容被四块 20×20 的底板占掉；③ 缩放态没有「从哪退出」的可见入口，屏 1 的「保留角标」也就落空了；① 与稿面注记逐字一致 |
| **D9** | 单 pane 工作区的行为 | ① **切换为 no-op，但键仍被认领；不画角标**〔建议〕；② 允许「缩放」单 pane（视觉无变化）；③ 把键转发给会话 | ③ 会让同一组合键在分屏与单 pane 下语义不同，用户无法预期；② 是零信息的状态变化，却要维护一个「缩放但无可视差」的态；① 的代价是按下去没有反馈，但这对「没有可放大的对象」是诚实的 |
| **D10** | 是否进命令面板 / 命令表 | ① **不进，走 WS.02 同一条前置过滤**〔建议〕；② 同时注册 `workspace.toggle_zoom` 命令 | ② 需要把「当前 pane」从命令的 `std::function<void()>` 里补出来——框架的 `Command` 不带来源窗口/pane 身份，这正是 WS.03 的框架前置（Aurora 无焦点窗口身份）；① 的代价是命令面板搜不到它，与分屏十二条同档，等 WS.03 的专棒一起搬 |
| **D11** | 角标底板的透明度 | ① **不透明 chrome**〔建议〕；② 半透明；③ 只画字形不画底 | ① 在任何主题与任何终端输出上都能读出字形，且像素判据稳定；② 浅色主题下灰字可能糊在浅色内容里，半透明的合成值也让像素断言依赖底层内容；③ 同 ②，且字形与终端文字互相干扰。代价是底板盖住 20×20 的内容，与 D6 的「鼠标用户入口」是同一笔账 |

---

## 4 与现状的差距 → 本棒须达到的条目

1. **纯逻辑件**（`include/borealis/ui/workspace_keys.h` / `src/ui/workspace_keys.cpp`）：`WorkspaceCommand::ToggleZoom` + `kBindings` 一行；`utest_workspace_keys` 的 `kClaimed` 由十二改十三、补 `ToggleZoom` 命中案与「`Ctrl+Shift+Z` 不转发」案。**不做**：不改 `divider_step` 的算式（`ToggleZoom` 不是步进，落进 `default` 的拒绝分支即可）。
2. **界面腿**（`src/ui/workspace_view.{h,cpp}`）：`base_layout_` / `zoomed_` / `hovered_zoom_` 三个成员；`on_layout` 产出显示表并让隐藏视图仍按 `base_layout_` 紧约束；`on_paint` 跳过隐藏子节点；`on_hit_test(_chain)` 先认角标、缩放态只命中所见 pane；`on_pointer_event` 的角标 Press / Move；`cursor_shape` 的手型；`run_command` 的焦点跟随与结构命令先还原；`close_pane` 清缩放；`zoomed_pane()` 观测面。
3. **测试**（`tests/integration/itest_workspace_layout.cpp`）：布局满盒/零盒、隐藏会话不 resize、缩放还原的尾沿下发、焦点跟随、结构命令先还原、关闭缩放 pane、角标悬停/点击/像素、单 pane no-op。既有 17 案一字不放宽。
4. **文档**：本条现状句、裁决 7.120、`CHANGELOG.md` v0.120、`PLAN.md` 的 WS.08 行；`UI_WORKSPACE_INTERACT.draft.md` 的排除语把 WS.08 从「不在本稿范围」移出并指向本稿；各处「十二条分屏键位」的计数改口。

**为何没有装配改动**：`WorkspaceView` 已经是每个标签的容器（`src/main.cpp` 的 `TabWorkspace`），键位在它内部消化；装配层不需要知道缩放存在。这也是本条与 WS.03 的分界——多窗口才需要装配层拆上下文。

---

## 5 验收判据

- **纯逻辑**（`utest_workspace_keys`）：`Ctrl+Shift+Z` 命中 `ToggleZoom`；认领集恰为十三条；`Ctrl+Z` / 裸 `Z` / 多一位修饰不命中；NumLock 两态同判；`workspace_key_bindings()` 与派发函数逐条同源。
- **集成**（`itest_workspace_layout`，`HeadlessSurface`）：
  1. 分屏后缩放焦点 pane：`zoomed_pane() == 焦点`；`current_layout().boxes` 里缩放 pane 满窗、其余零盒、`dividers.empty()`；缩放 pane 的行列数等于「满窗 ÷ 格步长」，隐藏 pane 的行列数逐字不变。
  2. 缩放与还原各只产生**缩放 pane 自己**一次尺寸下发；隐藏 pane 的连接 `resizes` 计数不变（F7 的端到端证人）。
  3. `Alt+Left/Right` 在缩放态把焦点与缩放一起换到目标 pane；目标 pane 立刻满窗。
  4. 缩放态按切分 / 等分 / 步进：先回到未缩放布局，再执行；`zoomed_pane()` 为空。
  5. 缩放态关闭焦点 pane：缩放清空、pane 与视图回收、焦点交接照旧。
  6. 角标：未缩放时悬停前不显形、悬停后右上角出现 chrome 底板与 accent 字形；点击后该 pane 取得焦点并缩放；缩放态角标常显；再点还原。单 pane 时无角标、`Ctrl+Shift+Z` 不产生下发也不写会话字节。
  7. 缩放态左沿不画焦点描边（像素取样为 pane 自己的底色）。
- **不做**：真机手感（角标悬停的出现/消失、连按方向键时缩放跳格的视觉稳定性）不在自动化面，须人工走查；本条因此**不称「可用」**。

---

## 6 在册代价与证据缺口

1. **无真机走查**（§4.4 第 33 条）：本沙箱无输入注入与可用截屏工具（CONN.12 那棒已实测 `xdotool`/`wtype`/`ydotool`/`xte` 全 MISSING、`import -window root` 挂住），缩放态的手感、角标在真实主题与真实终端输出上的可读性均未验证 ⇒ 本条只称「件本体 + 集成判据落地」，不称「可用」。
2. **Windows/MSVC 腿**：本棒的共享逻辑（布局、命中、绘制）不含平台分支，但 MSVC 编译与 Win32 指针/`draw_line` 的实际像素**未经编译验证**（§4.2 第 11 条，推断／未验证）。
3. **`Ctrl+Shift+Z` 与 WT 缺省键的关系未确证**（F10）：语义照 WT/Tabby，键位是本仓选择；若日后确证 WT 用别的组合，改 `kBindings` 一行即可。
4. **TUI 的键位让渡**：`Ctrl+Shift+Z` 自此是保留位，不再转发给会话；这与十二条同档，登记在册而不是「本棒新增的风险」。
5. **命令面板不可达**：缩放不进命令表（D10），与分屏十二条同档；等 WS.03 补齐「焦点窗口/pane 身份」的框架公共面后一起搬（Aurora 主仓 `include/aurora/app/application.h` 与 `app/shortcuts.h` 的现状已由 WS.03 地基实测登记）。
6. **隐藏 pane 仍在跑**：其帧泵、会话读线程与镜像更新继续工作（D4 的代价）；若将来出现「隐藏 pane 的输出积压」类问题，应在 `SPEC.FEAT.WS.09` / `WS.11` 的帧泵范围里统一裁，而不是在本层私自停表。
7. **没有转场动画**：缩放是瞬时换布局，与屏 1 的静止态稿一致；若将来要动画，需先有框架的布局动画公共件。

---

## 7 与旧稿的边界改口

- `codespec/UI_WORKSPACE_INTERACT.draft.md` 的「三条不在本稿范围」（当时措辞）把 pane 最大化与广播、多窗口、会话恢复并列；本条落地后，WS.08 从那一串移出，改指本稿。该稿面板 4 的键位总表仍是 WS.01/02 的十二键，**不追画**第十三键——表的本义是「本棒全部键位」，WS.08 的键位归本稿（避免在一张一次性生成的 SVG 上手工插行、连带挪动后续面板）。这条边界在 `UI_WORKSPACE_INTERACT.draft.md` §0 与 `UI_SETTINGS.draft.md` 的 D2-a 脚注里各留一句。
- 屏 1 的注记「悬停右上角 ⤢；缩放态下其余 pane 隐藏并保留角标」是本稿的视觉上游，本稿未改其像素；两者的对应关系是：展开/收起两形 = §2.2 的两条字形，常显/悬停显 = D8，「其余 pane 隐藏」= D4 的显示表。
