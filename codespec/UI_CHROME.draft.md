# 界面 chrome 的亮/暗适配设计稿（`SPEC.FEAT.PREF.01` 的最后一腿）

> 本稿回答「界面 chrome 从恒暗变成可亮、可随系统，需要立哪些档、色值取哪一套、运行期怎么换」。
> 需求原文的终端配色腿（≥8 套预置 + 16 色重映射 + 前景/背景/选区/光标）**早已交付**（裁决 7.26②、7.52 ~ 7.61），本稿只补需求里「UI 主题暗色优先、跟随 Aurora `Theme::dark()` / `ThemeScope` 体系」这一句的亮/暗半边。
> **本稿出稿时人不在场**，按长期目标指令「决策点按最优方案、不考虑工作量」**自裁 D1–D10**，逐条列理由、备选与代价；后续人若改判，按 `SPECIFICATIONS.md` §7 新增裁决覆盖，不回收本稿编号。
> 状态：**已落地（2026-10-26，裁决 7.157）**——D1–D10 按本稿推荐档实现；§4 一处「标签栏底＝`window_bg`」的笔误按代码与 §1 色值表更正为 `card_bg`；D4 的框架缺口登记为 **G45**（任务书 `temp_tasks/FRAMEWORK_TASK_CHROME_OS_NOTIFY.md`），1 s 轮询代偿在册；真机走查未做故不称「可用」。
> **2026-10-30 两处改口（裁决 7.170／7.178）**：D4 的运行期跟随已随 **G45 回货**改成应用级 OS 明暗通知、轮询删除；§1 暗档三枚基准值按人的裁决改钉在 `UI_OVERVIEW.draft.md` §2.1 那套 token 上（亮档不动，两档都保留）。本稿其余决策（D1–D3、D5–D10）未变。

---

## 0 现状与缺口（2026-10-26 读源 + 探针）

- **色值面**：界面 chrome 的唯一来源是 `ui::settings_chrome()`（7 色）与 `ui::settings_chrome_theme()`（由同一批常量合成的 `aurora::Theme`），挂在每扇窗场景根的 `ThemeScope` 上。它是**显式恒暗**的：`settings_panel.cpp` 的 154 处常量、`tab_bar.cpp` 的 10 处固定色、`workspace_view.cpp` 的 3 处固定色都直接写死暗色值（后两处甚至没走 `settings_chrome()`）。
- **数据源面**：Aurora 已有公共 `aurora::os_color_scheme()`（Win32 读 `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`，缺失键回 Light、打不开回 `Unspecified`；GTK 启发式；不缓存）。故 PLAN 原记的「本仓无 light/dark 数据源」**已过期**。
- **通知面（新缺口，登记为 G45）**：`Surface::set_os_color_scheme_handler` 是**单槽**且已被 `WindowHost` 独占消费（`WindowHost::on_os_color_scheme` 只做 `Window::set_os_color_scheme` + 整窗重绘），**没有**应用级多播、也**没有**取得「当前生效明暗」的公共读点（`Window::resolved_root_theme()` 与 `os_scheme_` 均私有）。本仓在场景根注入自己的 `ThemeScope` 后，框架解析出的明暗**到不了**本仓控件，运行期也不会通知本仓。→ 按跨仓纪律出任务书 `temp_tasks/FRAMEWORK_TASK_CHROME_OS_NOTIFY.md`，本仓先以**低频轮询**代偿（D4）。**闭合（2026-10-27）**：框架回齐 `Application::os_color_scheme_state()`／`set_on_os_color_scheme()`／`refresh_os_color_scheme()`（甲＋乙并存、跨宿主去重、主线程、`Unspecified` 原样透传），本仓轮询随裁决 **7.170** 删除。
- **入口面**：配置无对应键、面板无对应行、装配层不读 `os_color_scheme()`。

---

## 1 两套色板（本稿的唯一色值表）

> **2026-10-30 改钉（裁决 7.178）**：暗档三枚基准值改取 `UI_OVERVIEW.draft.md` §2.1 那套 token（`window_bg`←`win`、`text_dim`←`mut`、`warning`←`yellow`），原因是「稿一版、码一版」的两套基准长期漂移——按稿改代码时，色值表必须只有一个来源。分工自此定死：**`UI_OVERVIEW.draft.md` §2.1 是暗档的基准来源**，本 §1 是**两档的完整承载表**并与代码逐格同源（`ui::chrome_palette()`）；两表冲突以 §2.1 为准并回改本表与代码。亮档三枚不动：层级关系（窗口底比卡片低一档、次要文本与语义三色 ≥4.5）在两档同构成立。下表已按改钉后的值刷新，旧值在括号里留档。

| token | 暗（现值） | 亮（新增） | 用途 |
|:---|:---|:---|:---|
| `window_bg` | `#1E1F29`（**原 `#181921`**） | `#F4F4F7` | 场景根底、面板外框 |
| `card_bg` | `#21222C` | `#FFFFFF` | 卡片底、浮层条体、标签栏底 |
| `nav_bg` | `#1B1C24` | `#EAEAF0` | 设置页左导航列（新增字段） |
| `card_line` | `#4D4F63` | `#D4D5DE` | 卡片描边、分隔线 |
| `control_bg` | `#282A36` | `#ECEDF3` | 输入底、次级按钮底、选中标签底 |
| `text` | `#F8F8F2` | `#1C1D26` | 正文 |
| `text_dim` | `#8B93A7`（**原 `#8A8FA3`**，更早 `#627280`） | `#5A5D6E` | 角标、副标题、未选标签 |
| `accent` | `#BD93F9` | `#7C3AED` | 选中页、主按钮、下划线、焦点描边 |
| `scrim` | `#00000099` | `#00000066` | 全屏遮罩（新增字段） |
| `danger` | `#FF5555` | `#C62828` | 铃铛角标（新增字段） |
| `success` | `#50FA7B` | `#15803D` | 活动角标（新增字段） |
| `warning` | `#F1FA8C`（**原 `#FFB86C`**） | `#B45309` | 退出/断线角标（新增字段） |

对比度（WCAG 2.1，`utest_chrome_palette` 按同一算式断言）：

| 前景 / 背景 | 暗 | 亮 |
|:---|:---|:---|
| `text` / `card_bg` | 14.81 | 16.76 |
| `text_dim` / `card_bg` | **5.14**（改钉前 4.92；最早 `#627280` 只 3.19 不达 AA） | 6.51 |
| `accent` / `card_bg` | 6.55 | 5.70 |
| `danger` / `card_bg` | 5.03 | 5.62 |
| `success` / `card_bg` | 11.51 | 5.02 |
| `warning` / `card_bg` | 14.13（改钉前 9.27） | 5.02 |

判据：正文与次要文本 `≥ 4.5`，语义三色 `≥ 4.5`，`accent` 作为文字/边框 `≥ 4.5`。`card_line` 是装饰描边不在此断言（两套都低于 3.0，与 VS Code Light / Windows 11 的描边同档）。

---

## 2 决策（D1–D10，自裁）

**D1 配置键 = `appearance.chrome_theme`，三档 `system` / `dark` / `light`，缺省 `system`。**
业界（Windows Terminal 的「主题：系统/深色/浅色」、iTerm2 的 Use System Appearance、GNOME Terminal 跟 GTK）都把「跟随系统」作为终端应用的缺省；本仓需求句里的「暗色优先」折成**系统不表态时回暗**（`ColorScheme::Unspecified` → Dark），而不是「永远暗」。备选「缺省 `dark`」只在旧观感零变化这一点上占优，但它让新装用户在浅色桌面上继续得到一块黑面板，正是本条要消掉的现象；「暗色优先」在语义上也不等于「拒绝亮色」。
代价：浅色系统上的既有用户升级后界面变亮——**这是本条的交付内容本身**，不是回归；终端配色不跟着变（D8）。

**D2 色值 = 上表两套显式常量，扩展 `SettingsChrome` 到 12 个 token。**
`nav_bg` / `scrim` / `danger` / `success` / `warning` 原先散落在 `settings_panel.cpp` 与 `tab_bar.cpp` 的文件内常量里，亮色档必须让它们也有一位，故一次性收进唯一那张表（AGENTS §4.4 第 16 条的反面：多一处色值表就是多一处漂移面）。亮色 accent 取 `#7C3AED`（violet-600）而不是把 `#BD93F9` 原样搬过去：后者在白底上只有 2.4:1，选中态与下划线会读不出来。

**D3 运行期换肤 = 进程级活动档 + 每窗共享 `State<Theme>`。**
`settings_chrome()` 是 60 处调用点的公共读点，改成逐控件注入会把 20 个 UI 文件与所有绘制闭包重写一遍，且 `on_paint(Painter&)` 没有环境可读。故：
- `ui::set_chrome_scheme(ChromeScheme)` / `ui::chrome_scheme()` 是**主线程单线程状态**（与 palette 的既有全局读点同性质，头注释写明）；
- 每扇窗的 `ThemeScope` 改用框架的共享 `State<Theme>` 构造（`Provider` 的「运行时换肤」公共入口），换档时先设活动档、再 `state->set(chrome_theme(active))`，框架按既有 `on_signal_dirty` 清整子树缓存并重排重绘；
- 装配层对全部窗 `request_wake()`，空闲深睡也能排到这一帧。
备选「重建场景根」会丢焦点、选区与滚动位置，且每窗一棵树不可接受。

**D4 OS 跟随 = `os_color_scheme()` 轮询 1 s，缺口 G45 出任务书。**（**已改判，2026-10-27**：G45 回货后运行期跟随走应用级通知，轮询删除——裁决 **7.170**。原文留档，因为「为什么当时只能轮询」是那两条单槽限制的直接结论，不是可回收的措辞。）
框架没有「明暗变化」的应用级通知（§0），单槽 handler 被宿主用掉，故运行期只能轮询。轮询挂在 `Application::scheduler()` 的 `set_interval(1 s)` 上：该调度器的 `next_deadline_ms()` 参与帧循环的等待决策（`application.cpp` 的 `decide_wait` 读它），**空闲深睡也会准时醒**，不依赖「恰好有帧」。代价：Windows 上每秒一次注册表读（数十微秒级、在主线程帧边界而非绘制路径），以及最多 1 s 的切换延迟；框架补出通知后删除轮询即可，接缝只有 `apply_chrome_scheme()` 一个函数。
`Unspecified`（非 Windows、GTK 读不到）一律回暗，与 D1 的「暗色优先」同一条兜底。

**D5 面板入口 = 外观页第二行 `appearance.chrome_theme`，下拉三档，`Wired ∧ Immediate`。**
紧跟 `appearance.theme`（终端配色）之后、`appearance.language` 之前：两者都是「主题」但作用面不同，相邻才能让人看出区别；标签走词条（「界面主题」/「Application theme」）。候选串按既有下拉惯例显示落盘名（`system` / `dark` / `light`），与 `appearance.language` 的 `zh` / `en`、`terminal.bell` 的 `off` / `visual` / `audible` 同档——**下拉候选项的本地化是既有面板的整条缺口**，不在本条单独改（见 §5）。

**D6 绘制面迁移 = 设置面板 + 标签栏 + 工作区分屏三处；`broadcast_banner` 保留自带色。**
前两处是 chrome 的主体，必须跟着换；`workspace_view` 的缝隙/把手/焦点描边是同一套 chrome 色，不迁移会留下「亮面板 + 暗缝隙」。`broadcast_banner` 的琥珀底是**语义警示条**且自带不透明底与墨色，两套 chrome 下都可读，保持原样并在裁决里登记为「非 chrome」。
`settings_chrome_theme()` 的基调维持 `ColorRoles::light()`：暗档只覆写 `primary/on_primary/surface/on_surface` 四个角色，其余角色沿用亮色基准是既有实测下的刻意选择（未被读到的角色不上屏）；亮档同样以亮色基准 + 四个覆写。两档都写 `Theme::scheme`（Dark / Light），供框架侧将来按档解析。`on_primary` 分档：暗档 = `window_bg`（深字压浅紫），亮档 = `card_bg`（白字压深紫）。

**D7 暗档 `text_dim` 由 `#627280` 提亮到 `#8A8FA3`。**
旧值对 `card_bg` 只有 3.19:1，低于 AA；既然本条为「两套色板都要立得住」而重定色表，顺手把不达标的那一档修到 4.92:1，避免「亮色合规、暗色不合规」的双标。代价：暗色面板的副标题与未选标签会明显亮一档（可见变化）。

**D8 作用面边界：终端配色完全不动。**
`appearance.theme` 与 16 色重映射仍是终端**内容**的配色，chrome 只是窗口外壳；两者继续解耦（裁决 7.52 S5①/N6 的语义不变，只把「chrome 恒暗」改判为「chrome 按本档取亮/暗」）。因此「浅 chrome + 深色终端配色」是合法且常见的组合（Windows Terminal 同款形态），不做联动。

**D9 落盘与降级：缺键 = `system`，域外值按既有 `enumerated` 口径留痕并保留基线。**
`Store` 的装载侧已把枚举取值域把守做成一条腿（裁决 7.46②），本键不另写回落。写侧在 `appearance` 分组内落 `chrome_theme`，与 `language` / `cursor_shape` 同形态。

**D10 不做的事：chrome 色自定义、按窗 chrome、按显示器明暗、终端默认亮色板。**
需求原文的自定义面点名的是 16 色/前景/背景/选区/光标（终端配色，已交付）；再开一套「界面 chrome 调色」是需求外行为（AGENTS §4.1 第 1 条）。按窗与按显示器都超出「跟随系统」的语义，且当前每窗共享同一份 `Theme` 状态，不预留第二套结构。

---

## 3 运行时序（装配层）

1. **启动**（`Store` 就绪、任何控件构造之前）：`resolve_chrome_scheme(settings.appearance.chrome_theme, os_color_scheme())` → `set_chrome_scheme(...)`；随后建 `shared_ptr<State<Theme>>(settings_chrome_theme())`，每扇窗的 `ThemeScope` 用它构造。
2. **面板改档**（`hooks.broadcast`）：更新捕获的 `chrome_choice` → 重解析 → 若活动档变了则 `apply_chrome_scheme()`（设活动档 + `state.set` + 全窗 `request_wake`）。
3. **跟随系统**：仅当 `chrome_choice == System` 时动屏面；解析结果与活动档不同才走第 2 步的同一函数。~~实现＝`Scheduler::set_interval(1 s)` 现读 `os_color_scheme()`~~ → 实现＝`Application::set_on_os_color_scheme()` 的回调吃送来的 OS 原值，启动时先 `refresh_os_color_scheme()` 显式 seed（2026-10-27，裁决 **7.170**）。
4. 三条路径共用 `apply_chrome_scheme()` 一处，包含「同值不动作」的短路——避免每秒一次无谓的整树重排。

---

## 4 验收与变异

**单元（`utest_chrome_palette`）**：解析表五档（system×{Light,Dark,Unspecified}、dark、light）；`set_chrome_scheme` 后 `settings_chrome()` 与 `chrome_theme().scheme` 同步；两套色板的对比度下界；两套色板逐 token 不同（防止「亮档忘了改某一项」）；`chrome_theme(Light).roles.on_primary` 分档正确。

**集成（`itest_tab_bar` 增一例）**：同一棵标签栏在暗档与亮档各出一次无头帧，栏底像素分别等于两套 `card_bg`（§1 表「标签栏底」那一格；本行初稿误写 `window_bg`，以表与 D6 为准）；切回暗档帧与首帧逐位相同（状态可逆）。

**变异自证（AGENTS §4.4 第 22 条）**：
- m1：`resolve_chrome_scheme(System, Unspecified)` 返回 Light → 解析表单测红；
- m2：`settings_chrome()` 忽略活动档、恒返暗板 → 单测与标签栏像素案双红；
- m3：亮档 `text_dim` 换成 `#A0A0A0`（对比度 2.6）→ 对比度单测红；
- m4：标签栏栏底改回写死 `#21222C` → 亮档像素案红而暗档仍绿（证明证人指名的正是这一处）。

**不称「可用」**：`src/main.cpp` 不进 CTest runner（裁决 7.83），故启动解析、广播重解析、OS 变更通知三条腿只有源码审查；真机需目视「Windows 设置里切换应用明暗 → **≤1 帧**整窗换肤，且静止期不再有每秒一次注册表读」「面板改档即时生效」「浅色下标签/缝隙/设置页无色带残留」三档，走查前不宣称可用（第三条腿的措辞随裁决 **7.170** 由「1 s 轮询／1 s 内」改成通知档）。

---

## 5 在册缺口（不在本条修）

1. **下拉候选项未本地化**（`system` / `zh` / `off` 原样上屏）：既有面板的整条形态，改它要给 `aurora::Dropdown` 一份「显示串 / 落盘值」双列，影响全部下拉行，属独立一棒。
2. ~~**G45**：应用级明暗切换通知（§0 / 任务书）。回货后删轮询、接公共通知即可。~~ **已闭合并接线**（2026-10-27，裁决 **7.170**）：轮询已删、公共通知已接。
3. **`broadcast_banner` 的自带色**：语义警示条不做亮暗双版（D6）。
