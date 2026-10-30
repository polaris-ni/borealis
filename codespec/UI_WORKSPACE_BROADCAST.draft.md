# 广播输入设计稿（`SPEC.FEAT.WS.09`）

> **状态**：**评审收口并同日落地（2026-10-21）**——用户离场，常存授权「按照顺序持续推进，有任何疑问现在澄清，后续再遇到问题则你自行按照推荐去做」把 AGENTS §4.1 第 2 条的「等开工指令」替换为「按推荐自决 + 入册」。§3 的 D1–D13 全部取推荐档，逐条理由与代价见下。裁决入 `codespec/SPECIFICATIONS.md` §7 为 **7.121**，版本记 `codespec/CHANGELOG.md` **v0.121**。实落与稿面的差异、实现期修掉的两条缺陷、实际判据与读数见 §8。
> **与屏 1 的关系**：`codespec/UI_WORKSPACE.draft.svg` 屏 1 的注记「广播横幅（`SPEC.FEAT.WS.09`）／组内会话名列出、退出入口常驻；组内每个 pane 角标『广播 B』」是本稿唯一的视觉上游；`codespec/UI_OVERVIEW.draft.md` 的 N4 追加了「pane 底部角标仅分屏态显示」。本稿把这两条落成**可量、可断言的几何与状态**，不另画终态图：本棒只新增一条 26 dp 的全局横幅与一个 18 dp 的 pane 内角标，文字判据足以复核。屏 1 上被画的其余部分（标签条、把手、焦点描边）沿用 `codespec/UI_WORKSPACE_INTERACT.draft.md`，本稿不改它们。
> **上游依据**：`codespec/SPECIFICATIONS.md` 的 `SPEC.FEAT.WS.09` 原文（「广播组：选中的多个 pane/标签进入组后，键盘输入同步发送至组内全部会话（运维批量操作场景）；组状态显著标识，避免误操作」）；`codespec/ARCHITECTURE.md` §8.4（「广播输入在 `borealis::ui` 输入分发层实现」）；`codespec/PLAN.md` 的 M4 行；屏 1 的注记 2 与 N4；`ui::workspace_command` 的派发通道（裁决 7.47② / 7.51③）、`ui::WorkspaceView` 的接缝形态（`src/ui/workspace_view.h` 文件头）、`ui::TerminalView` 的输入路径（`src/ui/terminal_view.h` 的 `on_key_event` / `on_text_input`）。

---

## 0 地基实测（2026-10-21，POSIX 腿；Windows/MSVC 一律标推断／未验证）

本棒的形态由下面十三条读数定形。引用按「相对路径 + 符号/章节」，不写行号。

| # | 读数 | 对本稿的约束 |
|---|---|---|
| **F1** | **广播属于输入分发层，不属框架**：`codespec/ARCHITECTURE.md` §8.4 明写「广播输入（`SPEC.FEAT.WS.09`）在 `borealis::ui` 输入分发层实现」。Aurora 没有「把一次输入复制给多个控件」的公共件，也没有跨窗口/跨控件的输入路由。 | 本棒在应用侧组合：纯逻辑件管组成员与扇出策略，视口管「按自己的模式重编码」，工作区管接线，装配层管跨标签登记。**不新增框架缺口**。 |
| **F2** | **屏 1 的视觉上游是「横幅 + 底部 pill」，不是边框**：屏 1 在标签条下方画了 26 dp 全宽横幅（`#3A2A14` 底、`#F1FA8C` 标题、`#C8B27A` 副文案、右侧 100×22 的「退出广播」按钮），并在每个 pane 底部左缘画了 46.1×18、圆角 9 的「广播 B」pill（`#5A4420` 底、`#F1FA8C` 字）。`UI_OVERVIEW.draft.md` 的 N4 进一步定死：**pane 底部角标仅分屏态显示**，单 pane 隐藏。 | 视觉按上游落：**不做 pane 边框、不做左上角角标、不做标签条竖条**（这三个形态没有稿面依据，且边框会与 2 dp 焦点描边争同一条边）。横幅在标签条与工作区之间；pill 只在 `pane_count() > 1` 的工作区里画。 |
| **F3** | **`TerminalView` 的输入路径是三条互不相同的通道**：`on_key_event` 只处理 `KeyAction::Down`，先问按键前置过滤（工作区命令在此被消费），再 `term::encode_key` → `Session::send_bytes`；`on_text_input` 把后端提交的 UTF-8 交 `Session::send_text`（IME 的 preedit 走另一条 `TextCompositionEvent`，不在此）；粘贴走独立的 `send_paste_plan`，鼠标上报走 `on_pointer_event`。 | 广播只接**两条**：已编码成功的按键与已提交的文本。工作区命令在过滤层就被吃掉，不会广播；粘贴、鼠标上报、IME preedit、滚轮缩放不广播（D5）。接点放在「本视口已发完自己那一份之后」，保证源 pane 不重复收。 |
| **F4** | **每个会话有自己的编码与行尾**：`Session::send_text` 在 `flush` 里按 `encoding_.name` 重新编码并按 `line_ending_` 折行；`send_bytes` 只做行尾折算。源 pane 的字节**不能**直接复制给同伴——同伴的编码可能是 GB18030、行尾可能是 CRLF。 | 同伴必须按**自己的** `Session` 重编码：按键经各自的 `modes_snapshot()` 走 `term::encode_key`，文本经各自的 `send_text`。跨编码/跨行尾因此天然正确（D6）。 |
| **F5** | **`TerminalView` 没有任何「代别的视口发输入」的入口**：`session_` 是私有非拥有指针，公开面只有「发给自己」。工作区的 `KeyPreFilter` 只在**持焦**视口上被调用，隐藏标签的视口收不到键。 | 需要给视口补三条窄接缝：`set_input_mirrors`（源侧回调）、`send_broadcast_key` / `send_broadcast_text`（同伴侧按自己模式发送），以及一个只用于判同一流的会话身份读数。它们都不改变既有输入路径的默认行为（未装即没有广播）。 |
| **F6** | **工作区的接缝是 `Hooks` 结构体**：`make_pane` / `dispatch_grid` / `teardown_pane` 三条回调把会话生命周期留在装配层（`src/ui/workspace_view.h`）；`wire_view` 已在那里装 `GridSizeSink` 与 `KeyPreFilter`。 | 广播按同一形态加四条接缝：`is_broadcast`（画不画 pill）、`toggle_broadcast`（第十四键）、`mirror_key` / `mirror_text`（源侧转交）。工作区仍不认识路由器、标签或会话。 |
| **F7** | **装配层有三处 `WorkspaceView` 构造点**：`create_new_tab_with`、`replace_tab_session`（同 `TabId` 覆盖）、撤销关闭的重开腿。三处各自复制一份 `Hooks`；`replace_tab_session` 会把旧工作区整份换掉，撤销重开是另一条独立构造。 | 三条接缝必须三处同源接线；`replace_tab_session` 换工作区后要**重新登记**广播端点（旧闭包持有旧工作区的弱引用），`perform_close` 要撤账。这类「漏一处就有一格静默不广播」的旁路，正是 `wire_view` 那条纪律的同一类问题。 |
| **F8** | **装配层目前仍是单会话**：`create_new_tab_with` 的 `make_pane` 返回**同一个**首视图（注释写着「后续 pane 需要新建会话（暂不支持）」），`TabWorkspace.sessions` 也只装一枚。裁决 7.47⑤ 拍过「切分新 pane 挂新会话」，但 `ARCHITECTURE.md` 的 WS.02 小节仍记「装配层仍持单会话」为未落项。 | 广播的分发**按会话身份去重**：两个 pane 若指向同一会话，只发一份。生产侧今天能真实验证的是**跨标签**广播；跨 pane 的语义由集成用例用「多会话工厂」验证。**修装配层多会话属 WS.02 的欠账，不在本棒**（D13）：它要连带解决 SSH/串口档案的复制与凭据来源，规模与广播无关。 |
| **F9** | **横幅必须是根 `Column` 的固定子节点**：`rebuild_root_children` 每次切标签都把子节点截到标签条再追加当前工作区；若横幅也按需增删，节点身份、挂载与焦点在每次显隐时都会抖动。 | 横幅**常驻**根 `Column` 的 index 1：未开启时 `on_layout` 返回 0 高、`on_paint` 直接返回；开启时 26 dp。`rebuild_root_children` 改成保留「标签条 + 横幅」两枚、只替换工作区。代价是开启/退出时工作区高度变 26 dp，可见会话各收一次 resize（§6 在册）。 |
| **F10** | **点不可获焦的 chrome 会清掉焦点**：Aurora 的指针 Press 在命中链上找最近的可获焦控件，整条链都不可获焦就 `set_focus(nullptr)`（`dispatcher.cpp` 的 Press 分支）；`WorkspaceView` 自己就因为 `set_focusable(false)` 而在把手 Press 时读到空焦点，故它用 `last_focused_` 兜底并在 Press 分支自己 `apply_focus`。 | 横幅也不可获焦（否则点击后键盘进了横幅）。点横幅退出广播后，必须**把焦点还给当前工作区最后一次持焦的 pane**，否则用户退出后第一下键入无处可去。工作区为此补一个公开的 `restore_keyboard_focus()`，与缩放角标的 Press 分支同一条恢复路径。 |
| **F11** | **绘制能力足够且无需新字体依赖**：`Painter` 有 `fill_rect` / `fill_rounded_rect` / `draw_rect` / `draw_line` / `stroke_polyline` / `draw_text`；标签条已经在 chrome 里用**默认 sans 字体**画 13 pt 文本，没有从装配层注入字体。 | 横幅与 pill 都用默认 sans 字体（12 / 11.5 / 13 pt 与 11 pt），不引入 `TerminalView::ref_font_`。这样 CJK 文案与标签条同一条回退链，像素判据也稳定。pill 的宽度按 `FontEngine::measure_width` 实测，不写死 46.1。 |
| **F12** | **成员名只有一个真值源**：`TabStrip::tabs()` 给出每格的 `TabNames`，`resolve_tab_name(names, priority)` 是唯一的折算点；`TabStrip::set_changed_hook` 在任何可见状态转移时触发（含手动重命名与 `OSC` 标题），装配层已用它把标签栏标脏。 | 横幅的成员名按 `(TabId, PaneId)` 现取标签显示名；同一标签有多个成员时追加「窗格 N」。标签改名时经既有的 changed hook 刷新横幅，不新造名字缓存。 |
| **F13** | **键位表恰有十三个保留位（本条落地后为十四），`Ctrl+Shift+B` 未占用**：`src/ui/workspace_keys.cpp` 的 `kBindings` 与 `utest_workspace_keys` 的 `kClaimed` 逐条同形；`workspace_key_bindings()` 供设置页冲突比对（裁决 7.72③）。全仓与 Aurora 的实现体都没有注册 `Ctrl+Shift+B`（Aurora 的 `RichTextEdit` 用它做加粗，但本仓不使用该控件）；`itest_settings_panel` 里那条 `Ctrl+Shift+B` 只是**合成显示行**，该用例不断言冲突标注。 | 新增第十四键的改动面是确定的：`kBindings` + `kClaimed` + 两处计数/措辞 + 文档里的「十三条」改口。设置页的保留位比对自动把新键算进去，不需要第二份表。 |

---

## 1 范围与边界

**本期交付**：

1. **一个 app 级广播组**：成员是 `(TabId, PaneId)` 复合身份；组跨标签保留、切标签不散、不落盘。成员只能由用户显式加入/退出，**允许只有一名成员**（跨标签组装的必经中间态）。
2. **一条分屏命令与第十四键**：`WorkspaceCommand::ToggleBroadcast`，绑定 `Ctrl+Shift+B`；走既有「焦点 pane 的按键前置过滤」通道（裁决 7.47②），不进框架 `ShortcutRegistry`、不进命令面板。
3. **输入扇出**：组内任一 pane 发出的**按键（Down，且本视口编码成功）**与**已提交文本**，同步发往组内其余成员；每个同伴按自己的终端模式、字符编码与行尾重编码。源 pane 只发自己那一份，同一会话被两个成员指向时只发一份。
4. **全局标识**：标签条与工作区之间 26 dp 横幅，列出成员会话名与数量，退出入口常驻；横幅在组非空时出现在**所有标签**上（包括当前标签没有成员时）。
5. **pane 标识**：分屏态（`pane_count() > 1`）下，每个成员 pane 底部左缘一枚 18 dp 的「广播 B」pill；单 pane 不画（N4）。
6. **退出入口**：点击横幅任意处或右侧「退出广播」按钮 = 清空整组；`Ctrl+Shift+B` = 只切换当前焦点 pane。`Esc` **不**退出广播（D8）。
7. **生命周期**：关 pane / 关标签自动撤账；同 `TabId` 的会话重启保留成员资格；撤销关闭重开的标签不是成员。
8. **纯逻辑可测**：`BroadcastGroup`（成员集合）与 `BroadcastRouter`（跨标签扇出、去重、变更通知）都是无 Aurora 类型的纯 C++，可独立单测。

**本期不交付**（§6 在册）：

- **装配层多会话化**：生产侧分屏的 `make_pane` 仍返回首视图（F8），故「多个 pane 各连一个会话」的真机形态还不存在。广播的去重保证它不会重复下发；跨 pane 的完整语义由集成用例验证。修那一处属 WS.02 的欠账。
- **鼠标加入成员**：本棒只有键盘入口；右键菜单不加「加入广播组」空位（`include/borealis/ui/right_click.h` 的既有边界），命令面板同样不可达。
- **组状态持久化**：重启应用即空组（属 `SPEC.FEAT.WS.11` 的会话恢复语义，且「临时批量操作」与「重启后还在广播」相反）。
- **按 pane 的独立广播组**：本棒只有一组（D2）。
- **真机走查**：§4.4 第 33 条，本条只称「件本体 + 集成判据落地」，不称「可用」。

---

## 2 交互与状态

### 2.1 状态机

| 当前态 | 输入 | 结果 |
|---|---|---|
| 组空 | 在任一分屏格按 `Ctrl+Shift+B` | 该 pane 入组；横幅出现（26 dp）；分屏态下该 pane 出现 pill |
| 组非空 | 在**成员** pane 按 `Ctrl+Shift+B` | 该 pane 退组；若组因此变空，横幅与全部 pill 消失 |
| 组非空 | 在**非成员** pane 按 `Ctrl+Shift+B` | 该 pane 入组；横幅成员名与计数更新 |
| 组非空 | 在成员 pane 键入（字符 / 功能键） | 本会话收一份；组内其余**不同会话**各收一份按自身模式重编码的同样输入 |
| 组非空 | 在非成员 pane 键入 | 只有本会话收到；不广播 |
| 组非空 | 在成员 pane 粘贴 / 鼠标上报 / IME preedit | 只有本会话收到；不广播 |
| 组非空 | 按分屏命令（`Ctrl+Shift+D` 等） | 命令被按键前置过滤消费，**不广播**给任何会话 |
| 组非空 | 按 `Esc` | 照常发给会话（成员则照常广播）；**不退出广播** |
| 组非空 | 点击横幅或「退出广播」按钮 | 清空整组；横幅与全部 pill 消失；焦点还给当前工作区最后持焦的 pane |
| 组非空 | 关闭某成员 pane | 该 `(TabId, PaneId)` 自动退组；其余成员不受影响 |
| 组非空 | 关闭含成员的标签 | 该标签的全部成员自动退组；焦点/标签交接照旧 |
| 组非空 | 同标签会话重启 | 成员资格按 `(TabId, PaneId)` 保留；新视口接上同一条广播腿 |
| 组非空 | 切标签 | 组不变；横幅在目标标签上照样显示；pill 只出现在**该标签内**的成员 pane 上 |
| 组非空 | 缩放某成员 pane | 组不变；缩放态下可见的成员 pane 仍画 pill（`pane_count()` 仍 >1） |
| 任意 | 关闭确认被取消 | 组与成员完全不动 |

### 2.2 几何（全部为逻辑 dp，缩放因子在绘制时乘）

| 量 | 值 | 说明 |
|:---|:---|:---|
| 横幅高 | **26**（组空时 **0**） | 常驻根 `Column` 的 index 1；0 高时不可命中、不绘制。**2026-10-31 改判（裁决 7.181）**：屏 1 顶部那条 36 dp 自绘带占了首格，横幅的常驻 index 自此是 **2**——「chrome 前缀之后」这件事没变，变的只是前缀里有几条带 |
| 横幅底 | `#3A2A14` | 固定 chrome 色，不随终端主题 |
| 警告三角 | **14 × 12**，左内缩 16，垂直居中 | `stroke_polyline` 描边 + 底部一个 1.4×1.4 的点；`#F1FA8C` |
| 标题 | 左起 **32**，12 pt / 600，`#F1FA8C` | 「广播输入已开启：{成员名} 共 {N} 个会话」；超宽按省略号截断 |
| 副文案 | 标题之后 16 dp，11.5 pt，`#C8B27A` | 「仅组内窗格键入时同步发送；点击此处退出」；放不下时整段省略（先让标题） |
| 退出按钮 | **100 × 22**，圆角 **5**，距右 **18**，垂直居中 | 填充 `#5A4420`、1 dp 边框 `#4D4F63`、文字「退出广播」13 pt `#F1FA8C` |
| 横幅命中 | 整条 26 dp 都可点 | 指针改手型；Press 即清空整组 |
| pill 高 / 圆角 | **18 / 9** | 距 pane 左、下各 **8** |
| pill 宽 | `max(46, 文字实测宽 + 16)` | 11 pt / 600；不写死屏 1 的 46.1 |
| pill 底 / 字 | `#5A4420` / `#F1FA8C` | 文案「广播 B」；纯展示，不进命中链 |
| 焦点描边 | 不改 | pill 在 pane 内缩 8 dp 处，不压 2 dp 内描边 |

### 2.3 绘制与命中顺序

- **根 `Column`**：标签条（42）→ 横幅（26/0）→ 当前工作区（expand）。**2026-10-31 追记（裁决 7.181，同上一条同一处改判）**：那条带的落笔形态定下来之后，`src/main.cpp` 的根 `Column` 实为「标题栏（36/0，全屏档）→ 标签条 → 横幅 → 中段行 → 状态栏（28）」——横幅相对位置未动，只是前面多了一条带、后面多了末位那条读数条。
- **工作区**：chrome 底 → 各可见 pane（含 pill，pill 由 `TerminalView` 在自身 `on_paint` 末尾画，压在终端内容与提示卡之上）→ 把手 → 焦点描边。
- **横幅**：独立叶子控件，`set_focusable(false)`、`wants_click()` 为真；`on_pointer_event` 只认 Press。点击后由装配层清组，再调 `WorkspaceView::restore_keyboard_focus()` 把焦点还给最后持焦的 pane。
- **pill**：只有 `set_broadcast_badge(true)` 的视口才画；该标志由工作区在「成员集合变化 / 切分 / 关闭」时重算，并且只在 `pane_count() > 1` 时为真。两个 pane 指向同一视图时按「任一成员」置真（避免共享视图的角标被后写者抹掉）。

---

## 3 决策点（全取推荐档，逐条理由与代价）

| # | 议题 | 选项 | 影响面 |
|:---|:---|:---|:---|
| **D1** | 触发键 | ① **`Ctrl+Shift+B`**〔建议〕；② `Ctrl+Shift+G`；③ 不设快捷键、只做鼠标选择 | ① 的 B 是 Broadcast 助记，贴本仓 `Ctrl+Shift` 家族（D/E/W/0/Z），全仓与 Aurora 实现体均未占用（F13）；`RichTextEdit` 的加粗虽用 B，但本仓不使用该件。② 的 G 与「组」相关但和 `Ctrl+Shift+G`（部分终端的「查找上一个」）更易撞。③ 需要新造多选 UI，屏 1 没有依据。**代价**：`Ctrl+Shift+B` 自此不转发给会话，TUI 若用它做绑定的会失效；与既有十三键同档在册 |
| **D2** | 组的数量与身份 | ① **单一 app 级组，成员 `(TabId, PaneId)`**〔建议〕；② 每标签一组；③ 每窗口一组 | ② 无法表达「跨标签批量操作」，而需求原文点名「pane/标签」；③ 多窗口本身未落（WS.03）。① 的代价是两个标签不能各带一组；但「运维批量」的意图恰是把异标签的会话并到一处，单组更贴语义。成员用复合身份而非 `PaneId` 单键：`PaneId` 在每个工作区内从 1 起，跨标签必撞 |
| **D3** | 成员从哪里加入 | ① **焦点 pane 上按 `Ctrl+Shift+B` 逐个加入**〔建议〕；② 在标签条上多选；③ 命令面板批量选择 | ② 要新增多选态与第三套标签交互，且屏 1 没有画；③ 命令面板拿不到「当前 pane」身份（WS.03 的框架前置），与既有分屏键同一条边界。① 的代价是加入 N 个会话要按 N 次、且只有键盘入口；换来的是零新 chrome、与现有键位通道同源。多 pane 标签按 pane 逐个加入，语义比「整标签一把加」更可控 |
| **D4** | 只有一名成员时是否自动解散 | ① **不解散，允许单成员**〔建议〕；② 退到一名即自动清空；③ 禁止加入第一名（至少两员） | ② 会让**跨标签组装不可能**：在标签 A 加入第一名后切到标签 B，组已经散了。③ 同理，且「先选一个再选第二个」是自然的操作节奏。① 的代价是单成员期间横幅已在报「广播」，但此时扇出为空、行为与普通输入完全相同；标识仍诚实（组确实开着，下一位成员加入即生效） |
| **D5** | 哪些输入进广播 | ① **按键 Down（编码成功）＋ 已提交文本**〔建议〕；② 再加上粘贴；③ 再加上鼠标上报 | ② 的粘贴要逐 pane 回答多行确认，且每格的 bracketed-paste 模式不同，一次粘贴会变成 N 次不可撤销的写入；③ 鼠标坐标是**相对各格网格**的，复制给同伴会产生错位点击，语义上也不成立。① 的代价是「粘贴不同步」，但需求只说键盘输入；这条边界写进 §2 与验收 |
| **D6** | 同伴的字节怎么来 | ① **按同伴自己的模式与编码重编码**〔建议〕；② 直接复制源 pane 的字节；③ 只广播文本、不广播功能键 | ② 会把 UTF-8 的字节灌进 GB18030 会话、把 `ESC [ A` 灌进 application-cursor 模式的会话（F4）；这是数据损坏而非观感差异。③ 会让 `Ctrl+C`、方向键、`vim` 的 `Esc` 全部失效，批量运维最需要的恰是这些键。① 的代价是每个同伴都走一次编码与一次模式快照（纯 CPU，可忽略） |
| **D7** | 同一会话被多个成员指向 | ① **按会话身份去重，只发一份**〔建议〕；② 按成员各发一份 | ② 在今天的生产形态里就会发生：分屏工厂返回同一个视图/会话（F8），一次键入会被同一会话收到两遍，`rm -rf` 会跑两遍。① 的代价是「成员数」与「实际会话数」可能不同；横幅按去重后的会话计数与列名（§2），故显示不撒谎 |
| **D8** | 退出广播的入口 | ① **横幅点击/按钮清空整组，`Ctrl+Shift+B` 切换单 pane；`Esc` 留给终端**〔建议〕；② 按屏 1 文案用 `Esc` 退出；③ 每次退出弹确认 | ② 会抢走终端最高频的 `Esc`（`vim` / `less` / `tmux` 的退出键），广播态下用户按 `Esc` 想退出编辑器却退出了广播，正是「误操作」。③ 让「退出」比「进入」更费事，且横幅已把状态说清。① 的代价是退出整组必须用鼠标（或逐个 pane 按快捷键）；屏 1 的按钮本就是常驻入口，文案随之改口（§7） |
| **D9** | 组状态的视觉标识 | ① **26 dp 全局横幅 + 分屏态底部 pill**〔建议〕；② pane 3 dp amber 边框 + 左上角角标；③ 只在标签条加一枚状态字 | ② 没有稿面依据，且会与 2 dp 焦点描边争边、在窄 pane 上压内容。③ 把「哪几格在组里」这一 pane 级事实塞进标签级状态，多 pane 标签上读不出来，也不能在切标签后告诉用户「这格也收广播」。① 与屏 1 注记和 N4 逐条对应；代价是横幅占 26 dp、pill 盖住 pane 左下角一小块内容 |
| **D10** | 横幅的布局形态 | ① **常驻根 `Column`，高 0/26 切换**〔建议〕；② 按需插入/移除节点；③ 绝对定位浮在工作区顶部 | ② 每次显隐都改子节点表，挂载、焦点与布局缓存多一条分叉（F9）。③ 会盖住终端首行，且浮层命中要与 pane 竞争，屏 1 的布局也不是这样。① 的代价是显隐时工作区高度变 26 dp，可见会话各收一次 resize（§6）；换来的是节点身份稳定、动画/焦点都无额外状态 |
| **D11** | 横幅的成员名与计数 | ① **标签显示名；同标签多 pane 追加「窗格 N」；按去重后的会话计数**〔建议〕；② 只报数量；③ 显示原始 `PaneId` | ② 丢了「哪几个会话会被一起写」这条最关键的防误操作信息（屏 1 明写列出会话名）。③ 对用户不可读。① 复用 `resolve_tab_name` 的唯一折算点（F12），标签改名时横幅跟着变；代价是长名要截断、窄窗时副文案先让位 |
| **D12** | 分层与可测性 | ① **`BroadcastGroup` + `BroadcastRouter` 纯 C++；视口只管重编码；工作区只管接缝；装配层管端点**〔建议〕；② 把扇出塞进 `WorkspaceView`，用标签指针互相调用；③ 把广播做成 `Session` 层 | ② 让工作区反向认识别的标签，生命周期与调用环都难收；③ 会话层不该知道「哪些会话属于同一操作组」，且隐藏标签的工作区才是视图的归属。① 的 Router 用 `Sink{stream, key, text}` 抽象端点，单测不需要 Aurora 或真实会话；代价是多两个小文件与一层回调 |
| **D13** | 生产侧分屏仍是单会话（F8） | ① **本棒不修，按会话去重并如实登记**〔建议〕；② 顺手把装配层多会话化 | ② 要复制 SSH/串口档案、决定凭据来源与每 pane 的会话生命周期，是 WS.02 的一条独立欠账；夹在广播里会让「一个改动一个意图」破功，也会把广播的验收拖进连接层。① 的代价是跨 pane 广播在本机无法真机演示；集成用例用多会话工厂把语义钉死，跨标签广播今天就能真机验证。这一条同时解释为什么 D7 不是过度设计 |

---

## 4 与现状的差距 → 本棒须达到的条目

1. **纯逻辑件**（`include/borealis/ui/broadcast.h` / `src/ui/broadcast.cpp`）：
   - `BroadcastMember{TabId tab; PaneId pane;}` + `BroadcastGroup`（插入序、`add` 幂等、`remove`、`toggle`、`contains`、`clear`、`members`）。
   - `BroadcastRouter`：`Sink{stream, key, text}`；`attach(TabId, Sink)` 覆盖式登记、`remove_tab(TabId)`（撤端点并清该标签成员）、`toggle` / `remove` / `clear` / `members` / `contains`；`dispatch_key` / `dispatch_text` 只接受**成员**来源、跳过源成员与同会话成员、对每个不同会话调一次端点；`set_change_hook` 在成员集合真变时触发一次。
   - `utest_broadcast`：组语义、跨标签扇出、非成员来源不扇出、同会话去重、端点缺失跳过、`remove_tab` 撤账、变更钩子只在真变时触发。
2. **第十四键**（`include/borealis/ui/workspace_keys.h` / `src/ui/workspace_keys.cpp`）：`WorkspaceCommand::ToggleBroadcast` + `kBindings` 一行；`utest_workspace_keys` 的 `kClaimed` 十三改十四、补命中案与「不转发」案；头注与用例名改口。`divider_step` 落进 `default` 拒绝分支即可。
3. **视口**（`src/ui/terminal_view.{h,cpp}`）：`set_input_mirrors(KeyMirror, TextMirror)`；`on_key_event` 在 `send_bytes` 之后、`on_text_input` 在 `send_text` 之后各调一次镜像回调；`send_broadcast_key` / `send_broadcast_text` 按本视口模式与编码发送；`session_identity()` 只回会话指针作判等；`set_broadcast_badge(bool)` 与底部 pill 绘制（默认 sans、11 pt、按实测宽）。未装镜像即零行为变化。
4. **工作区**（`src/ui/workspace_view.{h,cpp}`）：`Hooks` 增 `is_broadcast` / `toggle_broadcast` / `mirror_key` / `mirror_text`；`wire_view` 装镜像；`run_command` 的 `ToggleBroadcast` 分支；`refresh_broadcast_badges()`（按会话视图 OR 聚合，`pane_count()>1` 的门在此）；`split_pane` / `close_pane` / 构造末尾各刷一次；公开 `restore_keyboard_focus()`。
5. **横幅**（`src/ui/broadcast_banner.{h,cpp}`）：`BroadcastBanner` 叶子控件，`State{active, members, count}`，`set_state` 值变才标脏；`on_layout` 0/26；`on_paint` 按 §2.2；`on_pointer_event` Press → `Hooks.exit_all`；不可获焦、手型光标。`utest_` 或 `itest_` 覆盖 0/26、像素、点击回调。
6. **装配层**（`src/main.cpp`）：`BroadcastRouter` 实例与 `refresh_broadcast_ui()`（名字按 F12 现取、去重会话、更新横幅与全部工作区的 badge）；三处 `WorkspaceView` 构造点同源装四条 `Hooks` 与 `attach`；`make_pane` 工厂的会话身份经 `view_of` + `session_identity()` 暴露；`perform_close` 撤端点与成员、`replace_tab_session` 重新登记；`root_column` 的 index 1 放横幅、`rebuild_root_children` 保留它；点击横幅清组并恢复焦点；`install_settings_strings()` 增六条词条（标题/副文案/退出/角标/窗格/未命名）。
7. **构建**：`src/CMakeLists.txt` 的显式源列表加入 `src/ui/broadcast.cpp` 与 `src/ui/broadcast_banner.cpp`（漏加即链接失败）；测试由 GLOB 自动注册。
8. **集成用例**（`tests/integration/itest_workspace_broadcast.cpp`）：多会话工厂 + 真路由器 + 真事件派发，断「源一份、同伴一份、第三种编码/模式各按自己重编码」「非成员不扇出」「命令键被吃、不广播」「`Esc` 照发」「关 pane 撤账」「分屏 pill 与单 pane 无 pill 的像素」。横幅另立 `itest_broadcast_banner.cpp` 或并入同文件。

---

## 5 验收判据

- **纯逻辑**（`utest_broadcast`）：`BroadcastGroup` 的插入序与幂等；`toggle` 的返回值与集合；`BroadcastRouter` 的跨标签扇出、源成员跳过、同会话去重、非成员来源不扇出、`remove_tab` 清成员并触发一次钩子、无端点时静默跳过。
- **键位**（`utest_workspace_keys`）：`Ctrl+Shift+B` 命中 `ToggleBroadcast`；认领集恰为十四条；`Ctrl+B` / 裸 `B` / 多一位修饰不命中；NumLock 两态同判；列表与派发函数逐条同源。
- **集成**（`itest_workspace_broadcast`，`HeadlessSurface` + 真实派发）：
  1. 两个标签各一 pane，先后按 `Ctrl+Shift+B` 入组：成员数为 2，横幅为 active，两格的 pill 在**各自**工作区内可见；单 pane 工作区不画 pill（N4）。
  2. 在 A 输入 `a`（文本）与 `Up`（按键）：A 的连接各收一份；B 收到同样语义的输入；A 的连接没有第二份（源不重复）。
  3. B 的终端喂入 application-cursor 模式、编码切到 GB18030 后，再在 A 按 `Up` 与输入一个非 ASCII 字符：B 的字节按**自己的**模式与编码变化，A 的字节不变（D6 的端到端证人）。
  4. 在非成员 C 输入：A、B 的连接都不增字节（非成员来源不扇出）。
  5. 按 `Ctrl+Shift+B` 退出 A：成员数 1，A 的 pill 消失、B 的 pill 保留；再按一次退出 B：横幅 inactive、高度 0、全部 pill 消失。
  6. 按 `Esc`：成员会话照常收到 `0x1B`，组成员不变（D8 的负面证人）。
  7. 分屏后加入第二格再关闭它：路由器不再含该 `(tab, pane)`，剩余成员与横幅计数同步；关闭整张标签清掉它的全部成员。
  8. 点击横幅（或按钮）：组清空、焦点回到该工作区最后持焦的 pane（后续键入仍进终端），横幅高度归 0。
  9. 两个成员 pane 共享同一会话身份时，一次输入只产生一份字节（D7；用测试工厂造共享会话）。
  10. 像素：横幅底 `#3A2A14`、按钮底 `#5A4420`；pill 底 `#5A4420` 且不压焦点描边；未开启时横幅区域为工作区内容（高度 0）。
- **不做**：真机手感（横幅点击后焦点恢复的直觉、按键扇出的延迟与回声顺序、pill 在真实主题/真实终端输出上的可读性）不在自动化面，须人工走查；本条因此**不称「可用」**。

---

## 6 在册代价与证据缺口

1. **无真机走查**（§4.4 第 33 条）：本沙箱无输入注入与可用截屏工具（CONN.12 那棒已实测），横幅/ pill 的真实观感、点击退出的手感、`Ctrl+Shift+B` 与真实 TUI 的冲突均未验证。
2. **Windows/MSVC 腿**：共享逻辑不含平台分支，但 MSVC 编译与 Win32 的字体/圆角绘制**未经编译验证**（§4.2 第 11 条，推断／未验证）。
3. **生产侧跨 pane 不可真机验证**（F8 / D13）：装配层分屏仍返回同一会话，故今天能真机验证的是**跨标签**广播；跨 pane 的语义由集成用例的多会话工厂担保。修装配层多会话属 WS.02 欠账。
4. **广播会放大破坏性操作**：`rm -rf`、`Ctrl+C`、`sudo` 输入会同时作用于全部成员——这正是功能本意。防误操作的手段是横幅 + pill + 会话名列表，**不是**每次按键确认（那会让批量操作不可用）。退出整组在鼠标上只有横幅一处；键盘用户须逐个 pane 退出。
5. **`Ctrl+Shift+B` 成为保留位**：自此不再转发给会话；与既有十三键同档。
6. **横幅显隐改变工作区高度 26 dp**：可见会话各收一次 resize，`vim`/`tmux` 一类会重排。这是屏 1 的布局形态带来的，不另做浮层规避。
7. **无持久化**：重启应用即空组；同标签会话重启保留成员（按 `TabId, PaneId`），撤销关闭重开的标签不是成员。
8. **死者仍在组**：会话退出后成员资格不自动清除，写入被 `Session` 丢弃；用户可以看见标签的退出角标并手动退出。自动清除会让「断线重连」期间静默丢成员。
9. **窄窗与截断**：标题按省略号截断，副文案在放不下时整段省略；pane 很窄时 pill 可能与底部居中的「不可表示字符」提示卡重叠（提示卡先画、pill 后画，pill 在上）。屏 1 的 46.1 dp 是 11 pt 文案的实测值，本稿按实测宽算，不抄死数。
10. **只有键盘入口**：鼠标可退出但不能加入；右键菜单与命令面板不提供入口（既有边界）。
11. **扇出成本随成员数线性增长**：每个同伴一次 `Session::read`（模式快照）+ 一次编码 + 一次写队列入队；UI 线程不做 IO，但成员很多时按键回调会变长。需求未给上限，本稿不设硬上限；若将来出现卡顿，应在 `BroadcastRouter` 的扇出层统一限流，而不是在视口里私加节流。
12. **`Esc` 与屏 1 文案不一致**：屏 1 写「Esc 或点击此处退出」，本稿明确不采用（D8）；横幅副文案改为「仅组内窗格键入时同步发送；点击此处退出」。

---

## 7 与旧稿的边界改口

- `codespec/UI_WORKSPACE_INTERACT.draft.md` §0 的「两条不在本稿范围」已把 WS.09 从原「广播、多窗口、会话恢复」那一串移出，改指本稿；那一串现只剩多窗口与会话恢复。该稿面板 4 的键位总表仍是 WS.01/02 的十二键，**不追画**第十三、十四键——表的本义是「本棒全部键位」，WS.08 与 WS.09 的键位各归其专稿。
- `codespec/UI_WORKSPACE_ZOOM.draft.md` 的「第十三键 / 十三条」是**本条落地时**的读数；WS.09 落地后认领集为十四条，广播键归本稿。该稿的判据（缩放布局、角标、跟随焦点）不变，只把「当前认领集」的计数在 `UI_SETTINGS.draft.md` 的 D2-a 脚注里同步为十四。
- 屏 1 的注记「Esc 或点击此处退出」与 D8 冲突，按 D8 改口；屏 1 的像素**不改**（它是一次性生成的静态稿，交互口径以本稿为准）。屏 1 的「每个 pane 角标『广播 B』」与 N4 的「仅分屏态」合并为：**分屏态每成员 pill，单 pane 只靠横幅**。
- 本稿明确**不新增** pane 边框、左上角角标与标签条竖条——这三者是本棒开工前的备选形态，因无稿面依据且与焦点描边争边而放弃。
- `codespec/UI_SETTINGS.draft.md` 的 D2-a 把「本仓十三条分屏键位」改为十四条，并注明第十四枚是 WS.09 的 `Ctrl+Shift+B`。

---

## 8 落地回写（2026-10-21，裁决 7.121）

**实落范围**：D1–D13 全部按推荐档落地。提交按 §4.6 第 26 条切分：纯逻辑件 `2df1aef` → 界面腿 `2802e95` → 装配接线 `1075f9b` → 集成四案 `121cba0` → 文档本笔。`src/CMakeLists.txt` 加入 `ui/broadcast_banner.cpp`；测试由 GLOB 自动注册。

**实现期抓到的两条真实缺陷（均已修，带 WHY 注释）**：

1. **共享视图的角标被后写者抹掉**：`WorkspaceView::refresh_broadcast_badges` 原按 pane 逐次调 `set_broadcast_badge`，而装配层分屏工厂今天仍可能让两个 pane 指向同一视图（F8）——先遍历到 `true`、后遍历到 `false` 时，同一视图的角标会消失，结果取决于遍历次序。改为先按视图 `map<TerminalView *, bool>` 做 OR 聚合、每个视图只写一次；集成用例三用共享视图的两成员钉住。
2. **横幅高度变化不触发父侧重排**：横幅设了 `width(fill())`，故 `is_relayout_boundary()` 为真，而 `mark_needs_layout()` 的布局缓存失效**不向上传播**（Aurora 布局缓存语义）——`set_state` 里高度 0↔26 变化后，根 `Column` 仍按旧高布局，工作区停在旧原点，横幅要么没位置、要么与终端重叠。集成用例二首跑红在「视口 `origin.y` 0→26」这一条；修法是高度真变时除标脏自身外，显式 `layout_parent()->mark_needs_layout()`（与 `Widget::mark_size_intent_changed` 同一类短路）。

**与稿面的三处差异（按 AGENTS §4.2 第 12 条以运行时为准回写）**：

1. **量宽口径**：pill 宽按 `FontEngine::display_width(..., scale)` 取**逻辑 dp**，不是 F11 写的 `measure_width`（后者是设备像素）；无头用例 `scale == 1` 时两者逐位相等，真实缩放下按 dp 断言才不随 DPI 漂移。
2. **状态结构命名**：实落为 `BroadcastBannerState{active, names, session_count}`（§4 第 5 条草写的 `State{active, members, count}` 是提要，不是最终名）；`names` 是已折算好的成员显示名，横幅自己不再碰标签名。
3. **横幅用例归属**：按 §4 第 8 条的二选一，并入 `tests/integration/itest_workspace_broadcast.cpp`，未另立 `itest_broadcast_banner.cpp`；四案共用同一套多会话工厂与真实派发装置。

**实际判据与读数**：

- `tests/unit/utest_broadcast.cpp` 12 案：组语义（插入序/幂等/单成员/跨标签复合键）、跨标签扇出、非成员来源不扇出、同会话去重、端点缺失跳过、`remove_tab` 撤账、变更钩子只在真变时触发。
- `tests/unit/utest_workspace_keys.cpp`：认领集恰十四条、`Ctrl+Shift+B` 命中 `ToggleBroadcast`、`Ctrl+B`/裸 `B`/多一位修饰不命中、NumLock 两态同判、`workspace_key_bindings()` 与派发函数同源。
- `tests/integration/itest_workspace_broadcast.cpp` 4 案（真 `EventDispatcher` + `HeadlessSurface`）：① 两标签各一 pane 入组后，A 的 `Up` 与中文字符在 alpha（UTF-8/CSI）与 beta（GB18030/application-cursor）各收自己形态的字节，非成员 gamma 不扇出，`Ctrl+Shift+B` 被吃不广播，`Esc` 双份且组不变；② 组空时视口 `origin.y = 0`、入组后 26、横幅底与按钮底为固定 token、pill 仅分屏态、退组消失、关闭成员 pane 与 `remove_tab` 撤账；③ 共享视图两成员只写一份文本与一份按键；④ 点横幅清组、横幅失活、焦距回到 alpha pane1、随后 `type("q")` 仍进终端。
- 结项读数：`ctest --preset linux -E etest_` **88 项**、`borealis_test_runner` 全跑 **1123 案**全绿（v0.120 基线 86／1107；新增 2 项／16 案；无新增 `etest_`）。

**边界与证据缺口**：真机走查未做（横幅点击手感、按键扇出延迟与回声顺序、pill 在真实主题/输出上的可读性），本条不称「可用」（§4.4 第 33 条）；Win32/MSVC 未编译验证（推断／未验证）；生产侧分屏仍单会话（F8），跨 pane 语义仅由集成用例的多会话工厂担保，修那一处属 WS.02 欠账；无持久化；会话退出后成员不自动清除；横幅显隐改变工作区高度 26 dp，可见会话各收一次 resize。
