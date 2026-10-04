# 设置与主题面板判据（屏 3 的判据文，`SPEC.FEAT.PREF.01–05` / `07` 的 UI 入口）

> 本稿是屏 3 `codespec/UI_SETTINGS.draft.svg` 的**判据文**——该稿自 2026-10-01 入库起只有图、没有文，本棒补它。
> 它与其余各稿的分工差别在于一件事：**本稿的控件清单不是从图上数出来的，而是从 `include/borealis/config/settings.h` 反向核对出来的**。裁决 7.26① 明写「面板落地时须以 schema 反向核对键名，不得另起一套键名」，故图与 schema 不一致时**以 schema 与需求原文为准**，图上多出的控件逐条处置见 §3。
> **人已拍板四条**（2026-10-04）：① 四分类骨架全建，未接线的域挂延后角标；② 「修改即时生效」的适用范围是外观 + 终端**全量即时**，`scrollback` 与编码标「下次会话生效」；③ 取色形态是**本仓 HEX 文本输入，不派框架补取色器**；④ UI 开工前**先补本判据文并自拍**，再动代码。
> **S1~S16 由本稿自拍、全取建议项**（沿用裁决 7.38① / 7.47 的自拍先例），逐条理由与代价见 `codespec/SPECIFICATIONS.md` §7 裁决 7.52。
> 配图：`codespec/UI_SETTINGS.draft.svg`（矢量源即事实来源）/ `codespec/UI_SETTINGS.draft.png`（1× 出图，1560×1394）。图上圆标 1~10 是 SVG 自带的那份编号判据块，本稿引用作「图标 N」；「文有图无」的一处见 §3 末。

---

## 0 本稿的性质与三条边界

- 本稿管「面板上有哪些控件、每个控件收哪个键、改完什么时候可见」，不管像素层序（那是 `codespec/RENDER_UI.draft.md` 与 `codespec/UI_SELECTION.draft.md` 的职责），也不管窗口 chrome 的尺寸阶梯（沿用 `codespec/UI_OVERVIEW.draft.md` §2）。
- 三条不在本稿范围：连接域的**建档表单**（档案树、五步向导、SSH / 串口表单在屏 2 `codespec/UI_CONNECTIONS.draft.svg`，本稿只画四域的**默认值**）；SFTP 双栏 / SSH 隧道 / 密钥管理器（`codespec/UI_OVERVIEW.draft.md` §5 的未画清单）；`SPEC.FEAT.PREF.07` 的快照回滚与导出导入**本体**（M2 腿，本稿只处置「导入配置 / 导出配置」两个按钮的呈现，见 §4E）。
- 「即时生效」在本棒有两条物理边界，先写明白免得评审时各说各话：
  - ① **本仓当下没有任何给面板用的运行期更新接缝**。`ui::TerminalView` 的外观与交互入参**全部**挂在构造函数上（调色板、参考字体、`ui::Typography`、内边距、闪烁周期、`InteractionOptions`、回退链七个形参），已有的四条接缝（`set_overlay_host` / `set_presentation` / `set_grid_size_sink` / `set_key_pre_filter`）没有一条能改颜色或字号。故 §5 S4 是本棒的开工项而不是既有能力。
  - ② **四个入参在「建会话」那一刻取用**（`scrollback_limit`、`connection.local_shell`、`connection.startup_directory`，以及 §7 那三个尚无接缝的会话侧缺省），运行期改它们不会重放既有会话。判据因此是「下次会话生效」，而不是把它谎报成即时。

---

## 1 全局参数（本稿新增处，其余沿用 `codespec/UI_OVERVIEW.draft.md` §2）

| 量 | 目标值 | 来源与理由 |
|:---|:---|:---|
| 面板画布 | 1500×900 dp @100% DPI | 图标顶栏与屏 1 同源；单位 dp，实现按框架缩放换算 |
| 分类导航宽 | 200 dp | `UI_OVERVIEW` §2.2 原样 |
| 实时预览盒 | 右端 `x ≥ 1120 dp` 一整条，逐格真实绘制 | 图标 7；裁决 7.25⑩（N5）**明令用真实绘制路径**，静态假预览不予采用 |
| 字号阶梯 / 按钮高 / 输入框高 | 与 `UI_OVERVIEW` §2.2 同 | 本稿不另立 |
| 色值输入形态 | HEX 文本（`#RRGGBB`，大小写不敏感）+ 色块预览 | 人已拍板③；框架 `widget/pickers.h` 只有 `DatePicker` / `TimePicker`，**没有取色器**。形态受装载侧约束：色值的解析与格式化只有一个判定点 `ui::color_from_hex` / `ui::color_to_hex`（`include/borealis/ui/color_text.h`，2026-10-04 落地），它只收 7 字符 `#RRGGBB` 且**写回不输出 alpha**，故 alpha 不经配置往返——面板收 8 位就是另立一套落盘形态（§5 S14） |
| chrome 色源 | `UI_OVERVIEW` §2.1 那张深色 token 表 | 见 §5 S5——**本仓当前一个 `ThemeScope` 都没装**，框架控件在无注入时回落 `Theme::light()`，与终端的 Dracula 是两条不相干的色源 |
| 延后控件的呈现 | 灰置 + 「延后 / 延后子项」角标，**不画成可用态** | `UI_OVERVIEW` §2.3 的文案口径；本稿对「有键无消费方」与「无键」两种延后给出不同处置，见 §5 S15 |

---

## 2 反向核对表：`config::Settings` 每一键的落点

> 「取值域」列写的是**装载侧 `src/config/store.cpp` 实际把守的区间**（裁决 7.46② 的分工：域由装载侧判、绘制侧不夹取），面板上的控件因此可以照抄同一区间而**不产生第二个真值源**。
> 下表按 `PaletteSpec` / `TerminalSettings` 的**成员名**速记（如 `palette.cursor_color`），而落盘与 `LoadReport::rejected_keys` 用的是**点号路径**且这两槽不带 `_color` 后缀（`appearance.palette.cursor` / `.selection`，裁决 7.27③ 的形态）。其可执行形态 `ui::settings_catalog()` 一律取后者，因为面板要写的就是这个路径。
> 「生效档位」四值：**即时**（本棒开 S4 的运行期入口后可见）／**下次会话**（构造期取用）／**接缝待开**（本棒开会话侧注入，见 §7）／**延后**（有键但全仓无消费方，画控件、灰置、挂角标）。

**外观域 `appearance.*`**

| 键 | 控件 | 取值域 | 消费方现状 | 生效档位 |
|:---|:---|:---|:---|:---|
| `theme` | 主题卡（8 张 + 自定义） | 取值见 `config/themes.h` | 派生 palette 的回落基色 | 即时（语义见 S6） |
| `palette.basic[0..15]` | 16 格色板 + HEX 输入 | `#RRGGBB`（大小写不敏感；**alpha 不经配置往返**） | `ui::resolve` | 即时 |
| `palette.default_foreground` / `default_background` | HEX 输入 ×2 | 同上 | `ui::resolve` | 即时 |
| `palette.cursor_color` | HEX 输入 + 「未配」档 | `optional`（缺键与显式 null 两态，裁决 7.27③）；色值形态同上 | 视口层取用（不参与 `resolve`） | 即时 |
| `palette.selection_color` | HEX 输入 + 「未配」档 | `optional`；未配经 `ui::selection_color` 回落 `basic[8]`；色值形态同上 | 色带层内替换 | 即时 |
| `palette.bold_is_bright` | 开关 | bool | `ui::resolve` | 即时 |
| `palette.min_contrast_enabled` | 开关 | bool | `ui::resolve` | 即时 |
| `palette.min_contrast` | 数值步进 `[1.0, 21.0]` | 装载侧把守 | `ui::enforce_contrast` | 即时 |
| `font_family` | 下拉（框架族目录） | 逐字节精确、区分大小写 | `ui::choose_font_family`；解析不到**静默回落**并留痕 | 即时 |
| `font_size_pt` | 数值步进 `[6, 72]` | 与 Ctrl+滚轮同区间 | 视口整格度量 | 即时 |
| `font_line_height` | 数值步进 `[1.0, 3.0]` | 裁决 7.46② | `ui::Typography` | 即时 |
| `font_letter_spacing_dp` | 数值步进 `[0.0, 8.0]` | 同上；量化后回填 | `ui::Typography` | 即时 |
| `font_fallback_chain` | 重排列表 + 添加下拉（S8） | 有序数组；超上限交框架截断 | 排版选项一条入口（裁决 7.50） | 即时 |
| `viewport_padding_dp` | 数值步进 `[0.0, 64.0]` | 裁决 7.25② | `ui::make_geometry` | 即时（行列数随之变，触发出列下发） |
| `cursor_shape` | 三档（块 / 下划线 / 竖线） | `term::CursorShape` | **无接缝** | 接缝待开 → 下次会话 |
| `cursor_blinking` | 开关 | bool | **无接缝** | 接缝待开 → 下次会话 |
| `cursor_blink_period_ms` | 数值步进 `[50, 5000]` | 装载侧把守 | 视口构造入参 | 即时（须重注册闪烁任务，S4 的代价条） |
| `sidebar_collapsed` | 开关 | bool | 无消费方（连接侧栏 `SPEC.FEAT.CONN.07` 未落地） | 延后 |
| `tab_name_priority` | 下拉两档 | `ui::TabNamePriority` | 消费方在标签条一棒（在途，本稿不为其背书） | 即时 |
| `status_bar.show_*`（10 项） | 开关 ×10（S10） | bool ×10 | 无消费方（状态栏件未落地） | 延后 |

**终端域 `terminal.*`**

| 键 | 控件 | 取值域 | 消费方现状 | 生效档位 |
|:---|:---|:---|:---|:---|
| `scrollback_limit` | 数值步进 `[0, 100000]` | 上限即 `grid::kMaxScrollbackLimit`，与 SVG 终端页那格「上限 100,000」的面板批注一致（圆标 8 只写「scrollback 上限」） | `Session` / `Storage` 构造期 | **下次会话** |
| `ambiguous_width` | 下拉两档 | `term::AmbiguousWidth` | **无接缝**（`Terminal::set_ambiguous_width` 存在但未经会话层露出） | 接缝待开 → 下次会话 |
| `long_line` | 下拉两档（截断 / 折行） | `config::LongLinePolicy` | 无消费方 | 延后 |
| `bell` | 下拉三档（关 / 视觉 / 可听） | `config::BellMode` | 无消费方（视觉提示未落地） | 延后 |
| `encoding` | 下拉（UTF-8 / GB18030 / GBK / Big5 / Latin-1 / CP437） | 自由文本键，装载侧只判类型 | 无消费方（会话编码腿未接） | 延后 + 下次会话 |
| `paste_newlines` | 下拉三档（原样 / 过滤 / 转换） | `term::PasteNewlinePolicy` | `InteractionOptions::paste` | 即时 |
| `right_click` | 下拉三档（菜单 / 粘贴 / 选中即复制） | `ui::RightClickAction` | `ui::plan_right_click` | 即时 |
| `copy_on_select` | 开关 | bool（缺省关闭） | 视口选区状态机 | 即时 |
| `trim_pasted_trailing_space` / `smart_line_join` / `strip_tmux_border_chars` | 开关 ×3 | 均缺省关闭（需求原文） | `ui::CopyOptions` | 即时 |
| `word_delimiters` | 文本输入（原始字符） | 装载侧**只判类型不判域**（裁决 7.39④：空串是「只有空格与制表断词」这一合法选择） | `ui::word_span_at` | 即时 |

**连接域 `connection.*`**（本稿只画默认值；建档表单属屏 2）

| 键 | 控件 | 取值域 | 消费方现状 | 生效档位 |
|:---|:---|:---|:---|:---|
| `local_shell` | 文本输入（空＝走探测链） | 自由文本 | 装配期取用（裁决 7.19④） | 下次会话 |
| `startup_directory` | 文本输入（空＝继承进程目录） | 自由文本 | 装配期取用 | 下次会话 |
| `ssh.*`（port / auth_method / agent_forwarding / keepalive_interval_sec / connect_timeout_sec） | 表单五项 | port 与两时长为整数、`auth_method` 四取值 | 无消费方（SSH 族未开工） | 延后 |
| `serial.*`（baud / data_bits / stop_bits / parity / line_ending / encoding） | 表单六项 | baud `[1, 4000000]`、data_bits `[5,8]`、stop_bits `[1,2]`、parity 与 line_ending 各为 `one_of` 白名单 | 无消费方（串口族未开工） | 延后 |
| `session_logging` / `session_log_dir` | 开关 + 文本输入 | bool / 自由文本 | 无消费方（`SPEC.FEAT.CONN.11` 属 M4） | 延后 |

> 凭据不在本表：`SPEC.FEAT.CONN.09` 的密码与私钥 passphrase 经 OS 凭据库，配置文件里永远不出现明文值（裁决 7.26⑥），面板因此**没有**任何密码输入框，只有「每次询问」这一降级态的说明文案。

**快捷键域 `shortcuts.overrides`**

| 键 | 控件 | 现状 | 生效档位 |
|:---|:---|:---|:---|
| `shortcuts.overrides` | **只读表**（命令 id + 当前组合键 + 冲突标注） | 全仓无 `CommandRegistry` 用量，即**无消费方**；重绑另需键位录制件 | 展示即时、重绑延后（S9） |

---

## 3 与 SVG 的九处出入（以本稿为准）

1. **主题卡的第 7、8 张名字不同**：SVG 画 Tomorrow Night / Daylight，`include/borealis/config/themes.h` 实际八套是 dracula / nord / solarized-dark / one-dark / gruvbox-dark / monokai / **campbell / tokyo-night**。以代码为准（AGENTS.md §4.2 第 12 条：文档与代码冲突时以代码运行时为准并回填文档）。图标 2 那句「内置 ≥8 套」两边都成立，问题只在名字。
2. **第五类「高级（韧性/日志）」撤销**：需求原文与 schema 都是**四分类**，多出一类就是「另起一套键名」。其两项内容各有归处——会话日志两键归**连接页**；配置韧性不是可配项而是**启动事件**，归 §7 的降级对话框。
3. **「语言 Language」下拉不建可用态**：`SPEC.FEAT.PREF.05` 未开工——本仓从未向 `i18n::default_string_table()` 注册过译文，也没调过 `set_default_locale`（`src/`、`include/`、`tests/` 三处 grep 均零命中）；框架的语言走 `Provider<Locale>`，没有进程级 locale 量。按人已拍板① 画**灰置区段 + 延后角标**。
4. **「跟随系统浅色」开关撤销**：框架 `environment/media_query.h` 的 `MediaQuery` 无亮度或 appearance 字段（全 include grep `brightness` 零命中），即**没有数据源**；且 `SPEC.FEAT.PREF.01` 原文只写「UI 主题暗色优先、跟随 Aurora `Theme::dark()` / `ThemeScope` 体系」，没要求跟随系统。这条属「图上画了需求没要的东西」，按 AGENTS.md §4.1 第 1 条不实现。
5. **「多行粘贴警告」开关撤销**：`SPEC.FEAT.INTERACT.03` 把多行警告写成**固有行为**而非可配项，schema 里没有该键；逐行节流的块间隔按裁决 7.33④ 是库内常量、明确「暂不开配置键」。画一个没有键可写的开关就是空位。
6. **「OSC 52 剪贴板读取（三态）」不画控件**：属 `SPEC.FEAT.CONN.12`（M4），schema 无键。处置与第 5 条同理，改在终端页脚注一行说明它随该条落期入 schema（S15 的「无键不画控件」档）。
7. **「CJK 缺字回退链」从静态串改为可重排列表**：SVG 画的是字面串「微软雅黑 → 等线 → Noto Sans CJK」，而真值是 `std::vector<std::string>` 且顺序即语义；裁决 7.50 已接消费。形态见 S8。
8. **快捷键页首版只读**：SVG 画了红字「冲突：与「复制路径」重复」与「恢复默认键位」按钮。当前 overrides 无消费方、重绑需要录制件，且本仓键位通道不经 `ShortcutRegistry`（裁决 7.51③），故首版只呈现注册表实际值与冲突标注，「重绑」「恢复默认」两处挂延后角标（S9）。
9. **右侧预览盒从手绘静态图改为真实绘制路径**：SVG 那一盒是逐格手画的示意。裁决 7.25⑩（N5）明令「用真实绘制路径：预览区复用终端视口的同一套格参数与合成链路」，静态假预览**不予采用**。注入方案见 S7。

**一处「文有图无」**：状态栏 10 项开关。裁决 7.25⑧（N3）明写「每项一个开关入 `borealis::config`，入口随设置面板」，而 SVG 没有这一组。本稿把它定在**外观页末段**（S10），并按 `codespec/UI_OVERVIEW.draft.md` §6.5 的既有口径登记为**配图落后项**——生图脚本未入库，故本轮不手改 SVG、不重出 PNG，以本文件为准。

---

## 4 逐页判据

**A. 外观页**

- A1-a：主题卡带四色样例（前 / 背 / 强调 / 一个基本色），选中卡在右下角给勾；**卡片名字必须逐字等于 `themes.h` 的显示名**（判据即 §3 第 1 条的反向核对）。
- A1-b：「自定义」不是一张第十卡，而是「任一 palette 槽被改过」后的**状态**：palette 编辑区常驻在主题卡下方，改一格即在主题行挂「自定义」角标（与 SVG 的「自定义」小签同形态）。
- A2-a：16 色重映射是**一格一格点**而不是 16 个常驻输入框：点一格 → 下方给出该格 HEX 输入、当前值与「恢复主题默认」（S6）。
- A2-b：`cursor_color` 与 `selection_color` 各带「未配」档，且「未配」与「配成黑色」在 UI 上必须可区分——这是裁决 7.27③ 那条两态在界面上的兑现。
- A2-c：最小对比度阈值输入旁**实时显示**当前前景/背景的 `ui::contrast_ratio` 读数与是否达标（WCAG），否则用户改完不知道有没有用。
- A2-d：所有色值输入框**都没有 alpha 位**——`palette.*` 的槽经配置往返只有 `#RRGGBB`（装载侧只收 7 字符，写回不输出 alpha），给一个存不回去的输入框比不给更糟（用户以为改了透明度，重启就没了）。透明度若将来要可配，须先给 `PaletteSpec` 的落盘形态补 alpha，那是一条 schema 变更而非面板改动（S14）。
- A3-a：字号、行高、字距三个步进器各自显示单位（`pt` / `×` / `dp`），与 SVG 的「14 pt / 行高 1.0 × / 字距 0.0 ×」逐字一致；三者一改即触发视口重取整格度量与行列数（S4）。
- A3-b：「字体连字 ligature」保留 SVG 的**延后子项**角标且灰置——它是需求原文的延后观察项，不得画成可用态（`UI_OVERVIEW` §2.3）。
- A4-a：等宽字体下拉的候选来自 `render::list_font_families(monospace_only = true)`，且**只在装配阶段取一次**（该入口首次调用是同步 IO，裁决 7.46③）；面板不得为它再取一遍目录（S16）。
- A4-b：配置里写错的族名会**静默回落**（框架族名逐字节精确、区分大小写），故面板打开时若当前生效族 ≠ 配置族，须在字体行下方显示回落留痕，而不是把下拉显示成那个不存在的族。
- A5-a：回退链区段显示顺序、支持上下移动与删除、末尾「添加族」下拉；链容量上限取框架 `AURORA_TEXT_FALLBACK_CHAIN_MAX`，超限时须显式提示「已截断到前 N 项」——因为截断发生在框架侧而**对用户不可见**（裁决 7.50）。
- A6-a：光标三形态 + 闪烁开关 + 闪烁周期；三档形态与 `term::CursorShape` 的 Block / Underline / Bar 逐一对应。SVG 那句「光标形态随 DECSCUSR 切换」保留：面板给的是**缺省档**，远端之后以远端为准（`settings.h` 该键的注释）。
- A7-a：视口内边距步进 `[0, 64]`，改完预览盒与主窗口的**行列数同时变**——它是唯一一个会改变网格尺寸的键，判据要断「不出现半格」。
- A8-a：标签名优先级下拉两档（手动重命名优先 / OSC 标题优先）。它归外观域的理由已在 `settings.h` 写明（改的是标签栏那行字，不改会话字节），面板因此**不放**进快捷键页。
- A9-a：状态栏 10 个开关成组出现（S10），文案与 `codespec/UI_OVERVIEW.draft.md` §2 屏 1 的状态栏条目逐一对应，组顶加一行「入口先于消费方落地：关闭状态栏件尚未开工，本组改动只落盘」（S15 的延后档措辞）。

**B. 终端页**

- B1-a：scrollback 步进器上限 100,000，并在同一行显示当前值与「下次会话生效」角标（S11）——**不给**「立即重开会话」动作，那属工作区语义。
- B1-b：Ambiguous 两档带一句口径说明（窄＝本机 UTF-8 / SSH；宽＝GB18030 / GBK 串口一类），因为需求把这条写成裁决 7.15 的取值而非新行为。
- B2-a：右键三态、粘贴换行三策略、选中即复制、三项复制变换、断点集六个控件全部**即时**生效，判据是「改完不重开任何东西，右键与复制立刻按新值走」。
- B2-b：`word_delimiters` 输入框必须原样收字符（含引号、反斜杠、竖线），且**不做任何转义**；空串要在 UI 上说明「只有空格与制表断词」这一含义，否则用户会以为清空＝恢复默认（默认是 32 个 ASCII 可见标点）。
- B3-a：`encoding` / `bell` / `long_line` 三个控件按 S15 灰置 + 延后角标，且 `encoding` 同时标「下次会话生效」——两条标签不冲突：前者说没有消费方，后者说有消费方时的档位。
- B4-a：页脚注一行 OSC 52 读授权（§3 第 6 条）。

**C. 连接页**

- C1-a：本地 shell 与启动目录两项**可编辑且即时落盘**（消费方是下次建会话的装配路径），角标「下次会话生效」。
- C2-a：SSH 五项与串口六项整组灰置 + 组级「延后」角标（SSH / 串口族未开工），控件仍显示 schema 当前值——这样「骨架全建」与「不画成可用态」两条同时成立（人已拍板①）。
- C3-a：会话日志两键按 `SPEC.FEAT.CONN.11` 的语义呈现：开关缺省关闭，且开启时在旁注一句「首次落盘会给出日志可能含密码 / 令牌的风险提示」——该提示属落期行为，本稿只登记文案位。
- C4-a：本页**没有**任何凭据输入（§2 表末那条）。

**D. 快捷键页**

- D1-a：只读表按「动作名 / 分组 / 当前组合键」三列，动作名走本仓 `StringTable` 词条（裁决 7.25⑬ N8：命令 id 一律英文入注册表，中/英词条由本仓维护）。
- D2-a：冲突标注来自**实际比对**而非硬编码文案；SVG 那行「与「复制路径」重复」只是示例形态。
- D3-a：「重绑」与「恢复默认键位」两处挂延后角标（S9），且**不给一个点了没反应的按钮**。

**E. 顶部动作**

- E1-a：「导入配置 / 导出配置」按 `SPEC.FEAT.PREF.07` 的 M2 腿挂延后角标；本稿不实现其对话框。**延后的是入口而不是口径**：凭据不落明文这条已由裁决 7.26⑥ 定死，`utest_config` 里已落的证人是「落盘文件递归扫键名不含 `password` / `passphrase` / `secret` / `token` / `private_key`」这一条禁列名单；导出件本体的凭据检查（导出的是内容而非仅键名，故名单比对不够）随 PREF.07 的 M2 腿一起落，其判据形态可复用同一思路。
- E2-a：标题区在面板打开时是「设置 / 修改即时生效 · 右侧为实时预览」；面板是浮层而非换根（S1），故**不改**主窗口标题栏文案，SVG 里那条「Borealis — 设置」属绘图约定不是判据。

**F. 实时预览盒**

- F-a：预览用**独立**的一份 `Session` + `TerminalView`，不接管用户会话（否则会污染真实终端）。
- F-b：预览内容来自固定夹具文本（含 CJK 双宽、框线码点、16/256/真彩三色段、粗体与下划线、光标形态示范），与 SVG 画的 `$ borealis --preview` / 主题名 + 字号行 / 中文双宽行 / `┌──┐` 框线行 / red-green-blue 行同构。
- F-c：预览与主窗口共用同一套 `PaletteSpec`、同一份字体族目录、同一份排版选项，故「设置里好看、上屏不对」这类分叉在结构上不可能发生——这句是 SVG 自己写在盒下的判据，也是 N5 的动机。
- F-d：预览盒的行列数由其自身矩形算出，**不跟随**主窗口的网格。

---

## 5 决策点（已拍板：全取建议项，逐条理由与代价见裁决 7.52）

| # | 议题 | 选项 | 影响面 |
|:---|:---|:---|:---|
| **S1** | 面板的宿主形态 | ① **同窗口、在既有场景根 `au::OverlayHost` 上加一层全屏浮层**〔建议〕；② 运行期换主窗口的根（`app.window()->present_root(...)`）；③ 独立窗口 | ② 的公共入口**是**存在的（`Application::window()` 回主窗口 `Window *`，`present_root(Node&)` 是公开成员），但代价实测三条：框架只在**根变化**时重挂（`root_changed` 才调 `mount`），故换根会让视口走一遍卸载-重挂，其 `on_mount` 里注册的闪烁任务与（S5 那条）主题订阅都要重新接线；焦点不会自动回到重挂后的树，须再显式 `set_focus` 一次（裁决 7.31 那条「装配层必须显式派初始焦点」的教训是同一条坑）；面板关掉就是第二次换根，等于每次开关面板都折腾一次主视图的生命周期。③ 的代价两条：框架的 `scene()` / `focus()` 只作用主窗口（该文件自陈），多窗口下焦点与捕获不跨窗口，面板要自己接一份派发与帧回调。① 的代价是面板遮住主窗口终端，「即时生效」的可见兑现由预览盒承担——这正是 N5 要求真实绘制路径的动机；关掉浮层即见真实终端，主窗口视图**不被重挂** |
| **S2** | 面板的关闭通道 | ① **打开时向 `ShortcutRegistry` 注册一条 `Global` 作用域的 `Escape`、关闭时 `remove`**〔建议〕+ 右上可见「关闭」按钮；② 面板自己覆写 `on_key_event` 拦 Esc；③ 只给按钮 | ① 是框架 `CommandPalette` 自己的做法（其 `open()` 推作用域并注册 Esc、`close()` 注销），有先例可循；② 需要面板持有键盘焦点且与终端视图抢键；框架全 include 没有 `on_escape` / `CancelAction` 钩子。代价：`Global` 作用域期间本仓其他全局快捷键仍会响应——当下本仓没有任何其他全局绑定，故不构成冲突，但这条要随 `SPEC.FEAT.PREF.04` 复评 |
| **S3** | 面板的状态与写回通道 | ① **本仓自持一份 `Settings` 副本 + 显式 `Store::replace()`**〔建议〕；② 直接用框架 `Preferences::binding` 双向绑定 | ② 不成立：框架 `binding` 是**存储 → State 的单向投递视图**，`set()` 只更新下游 State 而**不写回存储**（该文件自己明写，写回须另调 `set`），且只有 `Switch` / `Checkbox` / `Slider` / `ProgressIndicator` 四个控件有 Binding 构造重载（最后一个只读，与写回无关）。① 属裁决 7.13② 的「可组合、留本仓」，代价是表单状态机（副本、脏标记、逐键落盘时机）归本仓自研并有单测 |
| **S4** | 「即时生效」的传导形态 | ① **给 `TerminalView` 开两条运行期更新入口**：一条收「外观包」（palette / 参考字体 / `Typography` / 内边距 / 闪烁周期 / 回退链），一条收既有的 `InteractionOptions`〔建议〕；② 改完重建视口；③ 下次启动生效 | ② 会把选区、回看偏移、焦点态、`ScreenMirror` 副本一起作废（用户在面板里改个字号，主终端就丢了选区），不可接受；③ 直接违背需求原文的「修改即时生效」。① 的代价三条：外观包须一并触发 `cell_metrics()` 重取与 `layout_opts_` 重建（链、量化字距、固定格推进三者同源，裁决 7.50）；闪烁周期改动须取消旧 `TimerHandle` 再注册；改内边距/字号会改行列数，故须走既有的去抖下发而不是直发一个中间值 |
| **S5** | chrome 色源 | ① **本仓给场景根装 `ThemeScope(std::shared_ptr<State<Theme>>)`，其 `Theme` 由 `UI_OVERVIEW` §2.1 的 token 表构造**〔建议〕；② 不装，让控件走框架缺省浅色 | ② 的现实后果是面板按钮/输入框是**浅色**（`Theme::light()` 回落）而终端是 Dracula 深色，与视觉稿整幅深色不符；且裁决 7.25 N6 说的是 chrome「固定一套 token」而非「固定为框架浅色」。① 的代价：`Theme` 只有 5 个令牌（background / primary / on_primary / text / font）加一个 `tokens` 映射，**没有** surface / border / disabled 档，次要色只能进 `tokens` 或由本仓自绘控件承担；主题切换**不**联动 chrome（N6） |
| **S6** | 切主题时 palette 的语义 | ① **整份 palette 取新主题值 + 每槽一个「恢复主题默认」按钮**〔建议〕；② 只换 `theme` 字段、色值不动；③ 另存「哪些槽被手动改过」的账本 | 落盘的是**最终值**（`settings.h` 的字段注释），内存里已无法区分「这格色是主题给的」还是「用户改的」，故 ② 会让主题选择形同无效（palette 是生效值），③ 要新增 schema 键并成为第二真值源（违裁决 7.26① 与 `codespec/ARCHITECTURE.md` §11.1「色值表归 `borealis::config`」那条）。① 的代价：切主题会丢掉自定义重映射——所以每槽的「恢复主题默认」是**逐槽出口**，且默认色永远可由 `theme_palette(theme)` 现算而不必存 |
| **S7** | 实时预览的实现路径 | ① **内存连接 + 夹具文本喂一个独立 `Session`，其视图复用 `ui::TerminalView`**〔建议〕；② 把主会话的视图搬进面板；③ 画一张静态假预览 | ② 会让预览与真实终端争夺同一个 `Session`（且 resize 互相打架）；③ 违 N5 的明文结论。① 的代价：面板要持有一份不经 `main` 装配的连接替身（`Connection` 接口已有，会话层单测就是用它驱动的），夹具文本一次性内存读、不在事件回调里做阻塞 IO |
| **S8** | 回退链的编辑形态 | ① **框架 `ReorderableList` + 「添加族」下拉**〔建议〕；② 保持静态文本；③ 每族一个输入框平铺 | 真值是有序数组，② 无法表达「改顺序」；③ 让族名成为自由文本，而框架族名匹配逐字节精确、区分大小写——写错的族会**静默回落**，等于把 A4-b 那条留痕变成常态。① 的代价：`ReorderableList` 松手后自己 `std::rotate` 改写底层 vector 再 `set()`，故面板要接受「列表件持有顺序」这一分工；不虚拟化，链长上限是框架常量故项数天然很少 |
| **S9** | 快捷键页首版范围 | ① **只读表 + 实际冲突检测；重绑与恢复默认挂延后角标**〔建议〕；② 连同重绑一起做（含键位录制）；③ 整页挂延后 | ② 需要键位录制控件（框架无）与 overrides 的消费方（当下无），且本仓键位通道不经 `ShortcutRegistry`（裁决 7.51③），重绑要同时管两条通道——那是 `SPEC.FEAT.PREF.04` 的本体，属 M2；③ 又浪费已有的 schema。① 的代价：需求 PREF.04 的「全部动作可重绑」这一半在本棒不交付 |
| **S10** | 状态栏 10 开关的落点 | ① **外观页末段成组**〔建议〕；② 单列「高级」类；③ 不落面板 | ② 与四分类冲突（§3 第 2 条）；③ 违裁决 7.25⑧ 的明文「入口随设置面板」。① 的代价：该组当下无消费方，须按 S15 灰置并写清「改动只落盘」 |
| **S11** | 「下次会话生效」的呈现 | ① **控件照常可改可落盘，行尾挂「下次会话生效」角标；不提供「立即重开会话」动作**〔建议〕；② 灰置不让改；③ 改成即时并偷偷重启会话 | ② 会让人以为键没实现（其实骨架全建是人已拍板①）；③ 会杀掉用户正在跑的命令，是不可逆动作。① 的代价：用户改完看不到变化要自己重开会话，故须给一处说明文案 |
| **S12** | 降级对话框的触发点 | ① **启动时按 `LoadOutcome` 分支弹一次 `au::Dialog` + `aurora::alert`**〔建议〕；② 只在面板里显示报告；③ 只写日志 | ② 违反 `SPEC.FEAT.PREF.07` 的「显著提示用户」——面板不是首屏；③ 是「静默」的反面教材。① 的代价：弹窗发生在窗口创建之后，须与 S2 的 Esc 通道共存；且它消除的是 `config/store.h` 里那条 `TODO(SPEC.FEAT.PREF.07)` |
| **S13** | 对话框文案与 `LoadReport` 的分工 | ① **UI 按 `outcome` 枚举取本仓词条，`rejected_keys` 与 `corrupt_backup` 作为结构化列表呈现；`message` 只进日志不上 UI**〔建议〕；② 直接显示 `message` | `LoadReport::message` 是 **ASCII 英文**诊断（AGENTS.md §4.3 第 14 条：诊断文案不属中文例外），把它显示在中文界面上等于违那条规则。① 的代价：每个 `LoadOutcome` 都要配一条中文词条，且「哪些键回落了」的文案要能带键名参数 |
| **S14** | HEX 输入的校验与提交时机 | ① **框架 `TextInput` + `FormField` 校验（正则只收 `#RRGGBB`），失焦或 Enter 且校验通过才落盘并广播生效**〔建议〕；② 逐字符即时生效；③ 只靠色块取色 | ② 会让半截输入（`#12`）落盘或产生非法色；③ 无来源（框架无取色器，人已拍板③）。**校验式必须与装载侧同一条**：该条已于 2026-10-04 落地为唯一判定点 `ui::color_from_hex` / `ui::color_to_hex`（`include/borealis/ui/color_text.h`，`src/config/store.cpp` 已改由它消费，私有实现删除），故面板直接调它即可——只收 7 字符 `#RRGGBB` 且写回不输出 alpha，面板若自己接受 8 位 `#RRGGBBAA` 就是**在配置里另立一套落盘形态**（改了存不回去）——alpha 不经配置往返这条同时在 §1、§2 与 A2 的判据里在册。① 的代价：`FormField` 的错误呈现是 `error_text()` 一行红字，且校验规则要在 HEX 大小写上定死一套（本稿取：大小写均可、alpha 不接受、不认 `rgb()` 函数式写法，均以该件的用例为准） |
| **S15** | 延后的两档口径 | ① **有键无消费方 → 画控件、显示当前值、灰置 + 「延后」角标；无键（需求已登记但 schema 未收）→ 不画控件，只在页脚注一行**〔建议〕；② 一律不画；③ 一律画成可用 | ① 同时满足人已拍板①（骨架全建）与 `UI_OVERVIEW` §2.3（延后不得画成可用态）；② 会让「四分类骨架」名不副实；③ 就是 §3 那五处撤销控件的成因。代价：两档的措辞要能区分（前者写「改动只落盘，消费方随 <需求号> 落期」，后者写「<需求号> 落期时入 schema」） |
| **S16** | 字体族目录的取用 | ① **与主窗口共用装配阶段取的那一份 catalog**〔建议〕；② 面板自己再 `list_font_families` 一次 | 该入口首次调用是同步 IO（裁决 7.46③ 因此把目录限定为「装配阶段取一次」）；② 等于在面板里引入第二次同步枚举且可能与主窗口视图取到的目录不同步。代价：`main` 要把 catalog 交给面板，装配层的依赖多一根线 |

---

## 6 框架侧实测结论：本棒真缺口 0

> 逐条读 Aurora 当日活动分支的公共头得出，不是凭既有结论。判据是裁决 7.13①：**只有渲染链路与事件链路上的缺口**才派框架；能用现有公共 API 组合出来的交互体验留本仓。

八条形态限制都**不**落在裁决 7.13① 的类别里，故**本棒向框架派发的真缺口数 = 0**：

1. 「运行期换主窗口根」**有**公共入口（`Application::window()` 回主窗口 `Window *`，`present_root(Node&)` 是公开成员），但 `Application::open_window` 移交 `Window` 所有权且回的是 `WindowId`，故独立窗口要自己接派发与帧回调；换根那条的成本是视口的卸载-重挂与焦点重派（三条实测代价见 S1）。限制在**生命周期成本**而非可达性，故 S1① 取浮层形态。
2. `Preferences` 的 `binding` 是单向投递视图、不写回存储（该文件自陈写回须另调 `set`），且有 Binding 构造重载的控件恰为**四个**（`Switch` / `Checkbox` / `Slider` / `ProgressIndicator(Binding<double>)`，进度件只读无写回需求）→ 表单状态机归本仓（S3①）。
3. 无取色器（`pickers.h` 只有 `DatePicker` / `TimePicker`）→ HEX 文本输入（人已拍板③）。
4. `SpinBox` 不能键入数字（只能步进与前后缀），故带小数的量（行高、字距、对比度）用步进器 + 一个只读数值标签；要键入就换 `TextInput` + `FormField` 校验（S14 同一条校验腿）。
5. `Dropdown` 的 `options_` 私有且没有运行期 `set_options` → 面板打开时按当前配置构造，族名目录变了要重建面板（本棒可接受，因目录只在装配阶段取一次，S16）。
6. `MediaQuery` 无亮度字段 → 「跟随系统浅色」没有数据源（§3 第 4 条撤销）。
7. `ShortcutRegistry` 没有「暂停全部绑定」语义。它**有**逐条 `set_enabled(int id, bool)`（shortcuts.h 的 `add()` 回绑定 ID 并注明供 `remove`/`set_enabled` 使用），但 `bindings()` 返回的是 `ShortcutBinding` 副本而该结构**不含 ID 字段**，故「先枚举再逐条暂停」在只拿到注册表现状时走不通——`clear()` + `CommandRegistry::bind_shortcuts()`（该函数自陈幂等：先移除上次产生的绑定再重建）才是可组合的形态。**属可组合**，不登记缺口；代价是同期框架侧其他绑定一并暂停。本棒到 PREF.04 的缺口账目仍为 0。
8. `Dialog` **没有** `with_scrollable_body()`（全 include grep 零命中，且该类的公共构造只收内容节点）。长列表（`LoadReport::rejected_keys` 的点号路径）须由本仓把内容包进框架 `Scroll`（`widget/scroll.h`）再交给 `au::Dialog(Node content)` → **属可组合**，不登记缺口；§7 那条降级对话框的滚动形态即此。

**一处对既有在册实测结论的就地更正**（裁决 7.52）：本仓 2026-10-02 在册的「框架 `Modifier::context_menu` 与 `MenuItem` 只有状态模型、**没有渲染与点击派发**」这半句不成立——`MenuItem` 是**有**渲染与点击派发的，载体是 `widget/menu_bar.h` 的下拉：`on_paint` 逐项画分隔线 / 标签 / 勾选标记 / 置灰色，`on_pointer_event` 的 Press 分支按等分行高折出序号、命中非分隔且 `enabled` 的项即调 `item.on_click()`，其 `wants_click()` 恒 `true`。须更正的是**两处限定**：① 派发只发生在 `MenuBar` 自己那块下拉矩形内（`dropdown_bounds()` 相对本控件原点），不是任意位置的上下文菜单，框架没有后者这件控件；② 那条下拉的色值是**硬编码浅色**（白底 `Color(255,255,255,255)`、正文 `30,30,30`、disabled `170,170,170`），**不随 `Theme` / `ThemeScope`**，故 7.41③ 的置灰档在深色界面上本就不可用。据此，`Modifier::context_menu` / `ContextMenuNode` 那一腿**确实**只有模型（存 items + `open_at` / `is_open` / `position`，全仓无 widget 侧渲染者；`system_tray_win32.cpp` 的 `show_context_menu` 是托盘自己的 Win32 原生菜单，不经控件树），而本仓右键菜单取 `au::Popup` + 一列 `au::Button` 的形态**不变**，只是理由从「框架不渲染不派发」改为「框架渲染但不随主题、且没有任意位置的弹出件」。回写落点：`codespec/SPECIFICATIONS.md` 附录 A.1 的菜单那一行与裁决 7.41③、`codespec/ARCHITECTURE.md` §9.2、`AGENTS.md` §6 各加就地更正，`codespec/ARCHITECTURE.md` §11.1 另补两条本稿实测的边界（chrome 色源须由本仓在装配阶段注入 `ThemeScope`、色值不经配置往返故面板不得有 alpha 位）；`codespec/CHANGELOG.md` 的历史条目按「旧编号旧条款原文保留」不改，只在 v0.49 条目里记这条更正。
另一条复核**不变**：`OverlayHost::handle_outside_click()` 在 Aurora 全仓**没有生产调用点**（只有旧备份命中），故裁决 7.41③ 的「外部点击由本仓在 Press 分支自驱」继续成立。

---

## 7 两处 TODO 的消除

- **`config/store.h` 的降级对话框（`TODO(SPEC.FEAT.PREF.07)`）**：按 S12 / S13 落地。装载发生在 `main` 早期、面板尚未建，所以对话框不走面板而走启动路径；`LoadOutcome` 四态里 `RecoveredCorrupt` 与 `RecoveredVersion` 需要弹（前者文案要带备份路径、后者要带版本号差），`FirstRun` 与 `Loaded` 不弹。`rejected_keys` 的点号路径列表在对话框里可滚动展开——框架 `Dialog` **无** `with_scrollable_body()`（§6 第 8 条），故由本仓把列表包进 `au::Scroll` 再作为内容节点交入。
- **`src/main.cpp` 的三个会话侧注入接缝（`TODO(SPEC.FEAT.PREF.02)`）**：`appearance.cursor_shape`、`appearance.cursor_blinking`、`terminal.ambiguous_width` 三条当下分别取状态机的 `Block` / `blinking = true` 缺省值与判定入参的 `Narrow`。S11 定的是**构造期**注入而非运行期改档，理由有两条：`settings.h` 把前两条写成「缺省档，只在建会话时喂给状态机」；`ambiguous_width` 的 `Terminal::set_ambiguous_width` 虽然技术上可在运行期调，但**已上屏的格宽不会重排**，按即时呈现会给出「改完一半生效」的错觉。代价是面板上这三个控件的角标与 `scrollback` / `encoding` 同档，评审时容易被当成偷懒——故把理由写在 §2 表与 S11 里。

---

## 8 验收判据（拍板并实现后）

- **纯逻辑**：面板本体的可脱界面断言面是三件纯逻辑前置——① schema ↔ 控件的**反向核对件**（每一键给出页 / 控件类型 / 取值域 / 生效档位，且判据是「`config::Settings` 里没有的键不出现在表里，表里没有的键必须显式列为撤销项」，即 §2 与 §3 的可执行形态）**〔已落 2026-10-04：`ui::settings_catalog()` / `ui::find_settings_control()`（`include/borealis/ui/settings_catalog.h` + `src/ui/settings_catalog.cpp`）+ `utest_settings_catalog` 八例。落地时的三点处置：基准取 `Store::replace(全量非默认 Settings)` 真正写出的 JSON 剥掉 `schema_version` 与框架 `__aurora_preference_meta__` 后的叶子键集，故判据① 的双向都由表自身驱动（表里新增一键，探针自动覆盖它）；「消费方现状」与「生效档位」做成**两个正交字段**（`ConsumerStatus` × `EffectLevel`）而非本稿 §2 表头那一列四值档位，依据是 §4 B3-a 的原句「两条标签不冲突」，如此才表达得出 `terminal.encoding` 的「延后 + 下次会话」；**撤销项不做成表里的行**——§3 那四处「图上画了、schema 没有键」的控件本就不在叶子键集内，双向比对天然把它们排除，再列一份撤销清单就是第二个需要人维护的名单，其判据改由 §3 的文本与本表的键集共同守；**一条判据边界如实登记**：取值域那几列是**单向**可证的（表比装载侧宽 → 面板给出的值存不回去，探针写进去必留 `rejected_keys` 痕迹，抓得到；表比装载侧窄 → 只是面板少画一个合法选项，装载侧不会为此报错，而装载侧的白名单是 `store.cpp` 的文件内私有表、公共面没有「列出某键全部合法名」的入口，故本件无从反向枚举）。少画选项的后果由人在评审时看到并补表，不由本件伪造一个抓不到的判据〕**；② HEX 文本 ↔ `ui::RgbaColor` 的解析与格式化（大小写不敏感、位数不足 / 前缀缺失、非法字符三类，且**与装载侧同一条式子**——`#RRGGBBAA` 与 `rgb()` 式一律判非法）**〔已落 2026-10-04：`ui::color_to_hex` / `ui::color_from_hex` + `utest_color_text` 九例，`src/config/store.cpp` 的三份私有实现已删除并由该件消费〕**；③ 「哪些键改了、要不要落盘、生效档位」的表单状态机（含 S14 的提交时机）。各补 `utest_*` 并以变异注入自证。
- **集成**（`HeadlessSurface` 通道）：改 palette 一格 → 预览盒与主视口的**同一格色**逐位变化；改字号 → 两个视图的行列数各自按自身矩形重算且**不出现半格**；改 `right_click` 三态 → 下一次右键的意图与菜单项随新值走（复用 `itest_right_click_paste` 的派发形态）；切主题 → 整份 palette 按 S6 重置且「未配」光标/选区槽回落新主题（守 A2-b 的两态）；启动装载遇损坏 → 弹且只弹一次、备份路径出现在文案里、且**不静默清空**。
- **不做**：真机走查以外的判据不宣称。设置面板是 UI 件，按 AGENTS.md §4.6 第 33 条与「UI 编写前先出设计图评审」的既有节奏——本稿即评审前置，实现后仍须真机走查（会话锁屏下 `SendInput` 静默失效，同裁决 7.31① 的可用面判据），故本棒不宣称「可用」；面板的取色、拖拽重排链、逐槽恢复默认这三处的**手感**尤其只能人工判。

---

## 9 出图与复现

配图 `codespec/UI_SETTINGS.draft.svg` 是 2026-10-01 那一批（与其余四屏同一套逐格矢量法：每格一个 `<text>`、色带与控件底为 `<rect>`、判据文字排在窗口框外以免压图），PNG 由无头浏览器按 SVG 声明尺寸 1× 截图得出，生成脚本是一次性工具不入库。本稿的九处更正**不回写 SVG、不重出 PNG**，沿用 `codespec/UI_OVERVIEW.draft.md` §6.5 已确立的口径（配图批注已知落后，以判据文与裁决为准）；下一轮若要重出屏 3，须按 §2 反向核对表逐控件重画，而不是照 2026-10-01 的旧布局微调。
