# 上屏层第一棒设计稿（`SPEC.FEAT.RENDER.01` / `03` / `04` 主路径）— **评审稿**

> 状态：**待评审**。评审通过后本稿内容并入 `codespec/ARCHITECTURE.md` §9（渲染接入与视口）并从 `codespec/` 删除本文件；未通过则按批注整改后再评审。
> 配图：`codespec/RENDER_VIEWPORT.draft.svg`（A 控件盒解剖 / B 层叠顺序 / C 脏行过滤 / D 光标三形态 / E 帧唤醒时序 / F 回看偏移与行换算）。
> 像素目标稿另见 `codespec/RENDER_UI.draft.md`：本稿定「代码怎么写」，那份定「上屏后长什么样、按什么判据算做对了」。
> 前置已闭合：G1 三腿（裁决 7.20 / 7.22）、取用形态（裁决 7.23）、纯逻辑两块（`ui::palette`、`ui::cell_layout`，CHANGELOG v0.20）、DECSCUSR 状态机侧（CHANGELOG v0.21）。
> 框架侧 API 一律按 2026-10-01 实测签名书写，未实测的推断在文末 §11 单独标注。

---

## 0 六项议题的拍板结果（2026-10-01 问答全部定下，本稿正文按此改写）

| # | 议题 | 候选 | 结论 | 影响面 |
|:---|:---|:---|:---|:---|
| D1 | `TerminalView` 类声明放哪 | ① `src/ui/terminal_view.h`（私有头）＋装配点在 `src/main.cpp`；② 破例进 `include/borealis/ui/terminal_view.h` | **①** | 架构 §2.3「公共头不含框架头」与 §6.3、§5.4 既有先例（`src/term/osc.h`、`src/platform/win/conpty_connection.h`）一致；选② 就得为它写例外说明，并让所有含此头者被迫依赖框架编译 |
| D2 | 帧唤醒句柄的持有方 | ① `session::Session` 增 `set_frame_wake(std::function<void()>)`，在**出锁后**、本次确有提交时调用；② `DamageQueue` 构造时持句柄、push 时调用；③ 不唤醒，靠主循环超时轮询 | **①** | ② 会把回调放进网格锁临界区（`push_rows` 在锁内），与「锁内不做 IO」的纪律相抵；③ 在 `Surface::wait_events` 阻塞期间输入延迟不可控，`SPEC.NF.PERF.02` 的输入延迟项直接受损 |
| D3 | 主线程本地副本的归属域 | ① `session` 域新增纯逻辑件（`session::ScreenMirror`）；② `ui` 域新增同名件；③ 副本直接由 `TerminalView` 持有、不抽件 | **①** | 副本的输入是「权威网格 + 脏行提交」，与 `DamageQueue`/`Session` 同一话题，归 session 域可全量单测（`grid::Storage` 侧无框架依赖）；选③ 则副本合并逻辑只能在像素层验，回归面变大 |
| D4 | 装饰线与光标的粗细口径 | ① 一律「1 物理像素」= `1.0F / scale` dp，下划线贴基线下方、删除线取 ascent 中点；② 用 `Painter::draw_line`（AA 圆帽） | **①** | `draw_line` 是抗锯齿线，半透边缘会让 1px 规则在暗底上发灰；`fill_rect` 落在整 dp 坐标上，与网格边界天然对齐。粗细不随字号放大是本条的**刻意选择**，需在 §9 验收里写清 |
| D5 | 本棒是否画 scrollback 偏移 | ① 恒画视口（偏移 0），滚动随后续棒接；② 现在就接滚轮与 `ScrollViewport` | **②**（扩了本棒范围） | 正文按②改写，边界见 §1「做/不做」与 §6.1：**鼠标上报模式下的滚轮转发**（`SPEC.FEAT.TERM.06`）不属本棒，以一句口径 + `TODO` 留痕，不做半截实现。**该 TODO 已于 2026-10-07 随 `SPEC.FEAT.TERM.06` 棒消除**，`on_scroll` 现为四档短路（`Ctrl` 缩放 → 上报 → 备屏翻页 → 本地回看），见裁决 **7.77** |
| D6 | **回看态遇到新输出怎么锚定**（D5② 带出的新议题，已拍板） | ① **距底恒定**：新输出时把窗口推到 `offset_y = max_offset() - back_rows`，画面随输出上移、始终「距底 N 行」；不需要改 `Storage`；② **绝对行锚定**：画面内容不动（`offset_y` 不动），要真做到需在 `grid::Storage` 增一个「已溢出/已覆盖的最旧行数」单调计数（公共头 + 单测 + 文档回写），否则 scrollback 饱和后逻辑行号整体左移一格，画面会逐行漂移；③ **回看时收到输出就跳回底部**（放弃回看态） | **①**（2026-10-01 拍板） | ① 零改动、语义自洽（「距底 N 行」正是 §6.1 派生量 `back_rows` 的定义），代价是读历史时新行会把内容顶走，长输出下看不清固定片段——该代价经裁决接受，若日后要换成②，改动面收敛在 `on_scroll` 之外的一处每帧推窗，且须同时动 `grid::Storage` 公共头；② 是 xterm/Windows Terminal 的常见手感，但在选区到来前（`SPEC.FEAT.INTERACT.02`）没有别的消费者，属为未来需求先付代价；③ 实现最省但等于没有回看，与 D5② 的意图相反 |

---

## 1 范围

- **做**：终端视口控件的绘制主路径（网格 → 色带 → 文本 run → 装饰 → 光标）、整格几何与 dp 换算的消费、尺寸来源（`SPEC.FEAT.XFER.01` 的 UI 侧取值腿，不含去抖）、跨线程帧唤醒与剪贴板排帧、**scrollback 滚轮回看（D5 选②：自管行偏移 + 钳制 + 偏移变化即整屏脏）**、`HeadlessSurface` 两帧像素差分的验收用例。
- **不做**（各归其棒，本稿不留半成品接缝）：文本选择与选区着色（`SPEC.FEAT.INTERACT.02`）、IME preedit 绘制与候选窗定位（`SPEC.FEAT.INTERACT.06`）、键映射与字节发送（`SPEC.FEAT.INTERACT.01`）、字号/字体可配（`SPEC.FEAT.RENDER.02`）、DPI 变更后的度量与字形缓存重建（`SPEC.FEAT.RENDER.05`）、尺寸去抖合并（裁决 7.23④）、吞吐基准与时间门禁（`SPEC.NF.PERF.02`，2026-10-02 已落，形态与口径见裁决 7.34）。
- **D5 选② 带出的两条明确边界**（本棒只留口径与 `TODO`，不实装）：
  1. **回看态遇到新输出的锚定语义**：这是 D5 选② 才出现的问题（回看画面 + 后台仍在产出行），已按 **D6①「距底恒定」** 拍板（§0 表末行、实现式见 §6.1），不改 `grid::Storage` 公共头。
  2. **鼠标上报模式下的滚轮**：`SPEC.FEAT.TERM.06` 的上报模式与 alternate scroll（DECSET 1007）要求滚轮**转发给应用**而非本地回看，本棒不做该分派（键映射与字节发送不在本棒），故 `on_scroll` 里以 `TODO(SPEC.FEAT.TERM.06): 上报模式与备屏 alternate scroll 优先于本地回看` 标一处，代码路径恒走本地回看。**该 TODO 已随 2026-10-07 的 `SPEC.FEAT.TERM.06` 棒消除**，判据见裁决 **7.77**。

---

## 2 分层与文件形态

```
include/borealis/ui/palette.h        （已有，无框架类型）
include/borealis/ui/cell_layout.h    （已有，无框架类型）
include/borealis/session/screen_mirror.h   新增（D3：本地副本的合并语义，纯逻辑）
src/ui/palette.cpp / cell_layout.cpp （已有）
src/session/screen_mirror.cpp        新增
src/ui/terminal_view.h               新增 —— 私有头，含 au:: 类型，只被 src/ui/ 与 src/main.cpp 消费（D1）
src/ui/terminal_view.cpp             新增 —— 全仓唯一触达 au::Painter / au::Widget 的翻译单元（架构 §9.2 的收口纪律）
src/main.cpp                         改 —— 造出 Window/Application，装配 TerminalView + Session，接 set_on_frame 与 request_wake
```

边界口径（与既有裁决一致，逐条对应）：

- **判断全在纯逻辑层，`terminal_view.cpp` 只做翻译**：`resolve()` 出颜色、`layout_row()` 出 run、`make_geometry()` 出 dp 几何、`ScreenMirror` 出可见行内容；绘制 TU 里的代码只有「把 `CellPaint` 变 `au::Color`、把 `ui::Rect` 变 `au::Rect`、按层叠顺序下调用」。这条边界的存在理由与 v0.20 相同——最容易算错的东西必须能在无框架环境里全量单测。
- **互转点只有一处**：`ui::RgbaColor ↔ au::Color`、`ui::CellPixels ↔ render::CellMetrics`、`ui::Rect ↔ au::Rect`，都在 `terminal_view.cpp` 的匿名命名空间里各一个函数。
- **`include/borealis/**` 不出现 `au::` 类型**：本稿把 D1 定为私有头形态后，`AGENTS.md` §6「尚无」条目里「`include/borealis/ui/terminal_view.h`（绘制侧控件的公共头）」需改写为 `src/ui/terminal_view.{h,cpp}`，属评审通过后的回写项之一（§10）。

---

## 3 控件形态（`ui::TerminalView`）

派生 `au::LeafWidget`（`include/aurora/widget/widget.h`；命中即自身，无子控件）。覆写与使用的框架钩子，按实测签名：

| 钩子 | 签名 | 本控件的用途 |
|:---|:---|:---|
| `on_layout` | `auto on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size`（protected virtual，纯虚） | 撑满父级并在此重取几何：`max` 有限时取 `c.constrain({c.max.width, c.max.height})`；被给成无限（Flex 主轴给非加权子项的「按需上限」）时**沿用上一次的盒**——此刻没有可信的可视宽度，退回 `columns × cell_width` 的自然尺寸会把窗口撑大，故这条分支不是死代码（实测结论见 §11 首条）。`make_geometry()` 与 `Session::resize` 都发生在这里（§5） |
| `on_paint` | `auto on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void`（protected virtual，纯虚） | §6 的四层序列 + 回看指示条。`bounds` 是**全局逻辑 dp** 的内容盒，且框架给的是全量 bounds（裁决 7.23ⓐ），裁剪盒只压在 `Painter` 的裁剪栈上 |
| `type_name` | `[[nodiscard]] auto type_name() const -> const char *`（纯虚） | 返回 `"TerminalView"`（诊断与 `WidgetRegistry` 的键；本棒不注册 JSON 工厂） |
| `wants_focus` | `[[nodiscard]] auto wants_focus() const -> bool` | 覆写为 `true`（基类默认即 true，显式写出以防后续改动悄悄把焦点序关掉） |
| `wants_focus_ring` | `[[nodiscard]] auto wants_focus_ring() const -> bool`（默认 `true`） | 覆写为 `false`：焦点态由光标形态自己表达（§7 的空心描边），框架的统一焦点环是 chrome，还会往像素判据里多画一笔 |
| `on_scroll` | `auto on_scroll(au::ScrollEvent &e) -> void`（protected virtual，默认实现只在 `OverflowStrategy::Scroll` 下动内核） | D5②：滚轮回看。详见 §6.1——本控件**不**声明 `overflow_strategy(Scroll)`（那会让框架把 `scroll_viewport_.offset_y` 平移进 `bounds.origin.y`，与我们的行偏移自管重复），改为覆写 `wants_scroll()` 返回 `true` 以拿到滚轮，并在覆写的 `on_scroll` 里自己驱动内核 |
| `wants_scroll` | `[[nodiscard]] auto wants_scroll() const -> bool`（默认 `overflow_ == Scroll`） | 覆写为 `true`：滚轮沿命中链自最深向根找第一个 `wants_scroll` 者，本控件要成为该目标而又不借用框架的平移绘制 |
| `on_mount` | `auto on_mount(const au::BuildContext &ctx) -> void` | 向 `au::Scheduler::current()` 注册 `set_interval(blink_period, …)` 闪烁任务（§7）。**不用基类 `tick`**：实测 `tick` 只在含手势的控件路径上被驱动，空闲时相位会停；调度器任务在主线程 `present` 之前触发，故相位翻动能赶上当帧 |
| `can_cache_display_list` / `can_cache_layout` | `[[nodiscard]] ... const -> bool` | 覆写为 `false`：本控件每帧内容都可能变，缓存只会掩盖脏行并取的缺陷 |

失效路径：`mark_needs_paint()`（本控件自绘失效的唯一入口，实测在同帧 `set_on_frame` 回调里调用即赶上当次 present，见 §11）；`request_frame()` 本棒未用——尺寸变化由 `on_layout` 自身被驱动，不需要控件反向请求重排。撑满父级的意图写在构造函数里（`width(au::fill())` / `height(au::fill())`），免得每个装配点（含用例）重述一遍；父级是 `au::Column{}`（`src/main.cpp` 现状即 Column 根）。

焦点态**不记成员**：绘制时现取基类的 `is_focused()`（公开可读），因此不覆写 `on_focus_change`——回调与绘制的次序错拍会留下 stale，而这里要的恰恰是「本次落笔时到没到焦点」。

成员（全部主线程所有，无锁）：`session::Session *session_`（非拥有，生命周期由装配层的对象次序保证）、`ui::PaletteSpec spec_`、`au::Font ref_font_`、`float padding_dp_`（裁决 7.25②）、`blink_period_`、`session::ScreenMirror mirror_`、`ui::GridGeometry geometry_`、度量缓存 `{ui::CellPixels cell_px_, float metrics_scale_}`、`CursorState cursor_` / `CursorState painted_cursor_`（帧内取用的五项光标快照，`term::Cursor` 无 `==` 故本层自建可比的窄结构）、`std::size_t total_lines_`、`bool blink_on_`、`au::TimerHandle blink_timer_`、`session::Size requested_size_`（上次下发的行列，避免每次布局都重发）。滚动状态**不新造成员**：直接用基类的 `scroll_viewport_`（protected，`ScrollViewport{offset_y, content_h, viewport_h, step}`，实测可派生访问），行偏移由它派生（§6.1）。

---

## 4 本地副本（`session::ScreenMirror`，D3）

**为什么必须有副本**：架构 §3.4 拍板「后台权威 + 主线程增量快照」，绘制在锁外；`Session::read()` 明确把可变 `grid::Storage &` 交给读取方并约定「按本帧提交把脏列区间并入本地副本，随后 `clear_dirty()`」。这份合并语义即本件。

对外形态（只含标准类型与 `borealis::grid` 类型）：

- `apply(grid::Storage &authoritative, std::span<const session::Damage> frame, std::size_t back_rows) -> void`：把本帧要看的 `rows` 行从权威网格复制进自身；复制粒度是**整行的 cell + 零宽侧表 + 超链接侧表**（列级增量已由 `DamageQueue` 合并到行区间，行内再切片不省成本）。`back_rows` 即「**距底行数**」（§6.1 由内核偏移派生），本件只认这个数、不认内核。**入参是可变的 `Storage &` 而非 const**：权威网格的脏标记是 `Session::read` 的扫描依据，写侧只登记不消费，主线程取用若不把它清掉同一脏区就每帧重复上报，故「消费脏标记」这件事归本件（原设计稿写 const 不成立，回写见架构 §9.2）。
- **行号换算（D5② 的关键一处）**：`session::Damage` 的行号是**视口内 0 基**（`damage_queue.h` 明写），而回看态画的是「距底 `back_rows` 行之前的那一段」，两者不同源。换算固定为：屏幕行 `i` 取绝对行 `total - rows - back_rows + i`；故视口行 `v` 出现在屏幕行 `v + back_rows`，`v + back_rows < rows` 的脏行才需要并入，`i < back_rows` 的屏幕行取自 scrollback 段（该段不可变，只在偏移变化时整窗重取）。落在可见窗之外的提交**只消费脏标记不并入**，因为画面上没有它。
- `line(std::size_t row) const -> const grid::Row &`、`columns()`、`rows()`、`back_rows()`（上次并入生效的距底行数——绘制侧的光标行换算要用同一个数）。
- **整窗重建的三个触发条件**（架构 §9.5「偏移变化即整屏脏重建」的落点）：① 网格尺寸（`columns` / `visible_rows`）与自身不一致；② 可见窗的**绝对起点**变了（回看偏移变化，或 D6① 的距底恒定推窗）；③ 本帧含整屏脏提交。三者任一即按绝对行号整窗重取，不做搬移——与裁决 7.17「行号与内容的对应关系整体变了须整屏重建」同口径，理由同 `Storage::scroll_up`：行级脏模型表达不出整窗换源。

单测判据（无需框架，已落 `tests/unit/utest_screen_mirror.cpp`）：只并脏行、`full_screen` 并全部、尺寸突变整屏重建、未并入的行保持旧值（证明没有整屏拷贝的退化路径）、侧表随行搬运、**偏移 `k>0` 时脏行 `v` 落到屏幕行 `v+k`、且 `v+k ≥ rows` 的提交被消费但不并入（画面上不可见）**、**偏移变化后整窗内容等于按新窗口重取的结果**、脏标记在本次调用后已清。

---

## 5 几何与尺寸来源（`SPEC.FEAT.XFER.01` 的 UI 腿）

```
box        = c.max.is_finite() ? c.constrain({c.max.width, c.max.height}) : size_      // on_layout 的入参与上次的盒
scale      = ctx.scale_factor                                                          // 物理像素 / 逻辑 dp
cell_px    = au::render::FontEngine::monospace_cell(ref_font_, scale)                  // 物理像素整格度量
geom       = ui::make_geometry(cell_px, scale, ui::LogicalSize{box.width, box.height}, padding_dp)
if (geom.columns && geom.rows && 与上次下发的行列不等) session.resize(session::Size{geom.columns, geom.rows})
```

- **几何与尺寸下发都落在 `on_layout`，不在 `on_paint`**：绘制里改尺寸会在同一帧里引出自排重绘（布局—绘制互触发），而重排在派发路径上本就是异步的；`on_layout` 是唯一「已知盒尺寸、尚未落笔」的位置。
- **一个参考 `Font` 取一次度量**（裁决 7.22④ / 架构 §9.2）：`ref_font_` 变化（字号、族）或 `scale` 变化才重取 `monospace_cell`——缓存的键是缩放值本身（`metrics_scale_`），故 DPI 变更后的下一次布局自然重取。粗体只换 `weight` 进 `TextRun.font`，不改格宽。
- `monospace_cell` 的 `scale` 入参取与本次绘制同一缩放（头文件明写物理像素口径），本层再按 scale 折回 dp 步长（`ui::make_geometry`）。
- 行列数由**控件实际尺寸**派生，不新造一条「窗口尺寸」通道；`padding_dp`（裁决 7.25②）进 `make_geometry` 的可视区扣除，故内边距改变会如实改变行列数而不是只挪原点。
- 0 行或 0 列（窗口最小化）**连下发都不该发生**：`Terminal::resize` 收到 0 会静默忽略，而连接侧会把 0 报给伪终端，故本层在控件侧就拦掉。同尺寸去抖落在 `requested_size_`（每次布局比较），代价是拖动窗口时每帧最多下发一次。
- 本棒**不做去抖合并**（裁决 7.23④）：整屏脏重建随行数变化发生，验收里不锁这项。

---

## 6 绘制序列（`on_paint`，四层）

```
① p.fill_rect(bounds, 主题默认背景)                        // 每次进入绘制都要铺，与「本帧有没有脏」无关
rows = min(geom.rows, mirror.rows())
绘制行集 = [0, rows) ∩ clip 折算的行区间                    // 有裁剪时：
    top      = bounds.origin.y + geom.padding
    first    = floor((clip.origin.y  - top) / cell_height)      // 夹到 [0, rows-1]
    last     = ceil((clip.bottom()   - top) / cell_height) - 1  // 同上；first > last 直接返回
    （无裁剪时 first=0、last=rows-1，即全部可见行）
② 对每条 StyleRun：paint.background != 默认底色 → p.fill_rect(色带矩形, 该色)
③ 对每行按「是否斜体」分两批 p.draw_text_runs：正体批走单参重载，斜体批走 (runs, opts) 重载
    （缺口 G13 已闭合、整批 opts 共用，裁决 7.29①②；每行最多两次调用）
④ 装饰：decoration_rects(geom, screen_row, run) 逐个 p.fill_rect(1/scale 高的规则, run.paint.foreground)
⑤ 光标：按 §7 形态；回看指示条：back_rows > 0 才画（视觉稿 U1）
```

- **「只重绘变更行」在本层的落点是标脏决策，不是绘制循环里的行过滤**：`on_frame` 里「本帧有提交 ∥ 距底行数变了 ∥ 光标快照变了」三者任一才 `mark_needs_paint()`；一旦进入绘制就把 clip ∩ 可见行全量重画。这不是浪费——框架的失效粒度是整个 widget（`dirty_bounds()` 非虚，控件无法只标一行），而行级过滤会让「未变行」在整屏脏重建后画出与上一帧不同的像素，那正是 §9 差分判据要排除的回归。**未变行重画逐位相同**这一事实由 ①~④ 全走 `fill_rect` 的不透明快速路径与同一批 run 切分边界保证，像素用例已把它锁住。
- ① 层每帧铺整盒而非「脏行的高度带」：裁剪帧里框架会先把裁剪区清回零基底（透明黑），不铺这层就露出黑底（实测）。
- `TextRun.text` 是 UTF-8 `std::string_view`，指向我方 `StyleRun::text` 的缓冲（本层不重编码，裁决 7.22② 的「只收合成后的最终值」）；run 的生命周期到 `draw_text_runs` 返回为止，故按行建临时 vector。
- `TextRun.box` **只读 origin**，宽高不参与布局，故 `Rect` 的 size 填该 run 的盒宽（跨 `last_column - first_column + 1` 个格）只为诊断可读，不影响落笔。
- 颜色：`ui::resolve(cell, spec)` 在 `layout_row()` 内已完成（含 bold-is-bright、暗淡、反色、最小对比度），绘制侧只做 `RgbaColor → au::Color` 逐字段搬（alpha 参与混合）。
- 不可见（`CellPaint::hidden`）：`layout_row()` 已保留底色段、丢弃文本段，绘制侧无需再判——这是 v0.20 定下的分工，本棒不重复实现。
- 坐标换算只有一处：`ui::cell_layout` 产出的矩形一律以**绘制盒左上角为原点**（含内边距），落笔前统一加 `bounds.origin`（`to_rect`）。`ui::Rect → au::Rect` 的转换点与 `RgbaColor → au::Color` 一道构成 §2 说的「互转只发生一次」。
- **斜体单独成批**：框架的批量入口只收「整批共用」的 `TextLayoutOpts`（刻意不做 per-run opts，以免与 `Font` 的样式语义重叠成两条矛盾来源），故 `paint_row` 按 `run.paint.italic` 分两批、每行最多两次 `draw_text_runs`。裁决 7.24④ 的「斜体段走逐片段 `Painter::draw_text`」过渡形态已随 G13 闭合撤销。
- **块形光标的第三段重画必须带上同一份 opts**：`paint_cursor` 用 `Painter::draw_text(..., opts)` 且 `opts.italic = paint.italic`，判定与 ③ 同源。漏掉它只有光标那一格退化成正体，整帧其余部分逐位相同，故这条纪律的像素用例是「同一行相邻两格、同一字形、一正一斜，光标逐帧移到其上」的对照（§9）——同格两帧与跨行对照两种写法都实测空转，因为底色/相位一相同就差异为零。

### 6.1 回看偏移（D5②）

滚动状态只有一个数，落在基类的 `scroll_viewport_` 内核上（架构 §9.5、裁决 7.23① 的「用框架内置 `ScrollViewport` 而非框架 `Scroll` 容器」）。**两个量的方向不同，别混**：内核的 `offset_y` 距**顶**，本层绘制与副本换算用的 `back_rows` 距**底**。

```
内核量一律以「行」为单位（不用 dp，免得浮点残差让取整少算一行）：
    step       = 1.0F                        // 一个滚轮单位 = 一行
    content_h  = total_lines()               // scrollback + 视口
    viewport_h = rows
    offset_y   ∈ [0, total - rows]           // 框架语义：距顶行数，0 = 看到最旧那行
派生量（§4 的换算、§6 的行集、§7 的光标都只认它）：
    back_rows  = max_offset() - round(offset_y)     // 距底行数；0 = 贴底（正常跟随态）
符号约定（`ScrollViewport` 头注释原文，已实测）：delta_y 正 = 向上滚动 = offset_y 减小 = back_rows 增大
```

- `on_scroll` 只做一件事：调 `ScrollViewport::clamp_offset(offset_y, e.delta_y, 1.0F, content_h, viewport_h)` 回写 `offset_y`，并用 `ScrollViewport::remaining_offset(...)` 把 clamp 吃不尽的余量写进 `e.remaining_y`、置 `e.is_handled`——到顶/到底的余量要上冒给更浅的可滚动祖先（工作区分屏场景），框架的嵌套协调就认这个字段。**这里不标脏**：内容换源要等本帧随后的 `on_frame` 重取可见窗，它自会标脏（滚轮派发在帧序里先于排帧，故同帧生效）。
- **为什么不声明 `overflow_strategy(Scroll)`**：实测 `Widget::paint_content` 只在 `overflow_ == Scroll` 时把 `scroll_viewport_.offset_y` 当 **dp** 平移量加进传给 `on_paint` 的 `bounds.origin.y`（`src/aurora/widget/widget.cpp` 的内容盒平移分支）。本层的偏移是**取哪几行**的语义，不是把画好的内容整体平移——平移会让首行画到盒外、底部留白，且单位也对不上（我们存的是行数）。故保持默认 `Visible`，覆写 `wants_scroll()` → `true` 拿滚轮，覆写 `on_scroll()` 自己驱动内核。副作用要记一句：`scroll_offset_y()` 这个基类 getter 因此返回「距顶行数」而非 dp，本控件不对外暴露该语义。
- **偏移变了就整窗重取**：不在控件侧判——`ScreenMirror::apply` 自己比较可见窗绝对起点（§4 的三个触发条件之一），控件只比较 `apply` 前后的 `back_rows()` 来决定要不要 `mark_needs_paint()`（架构 §9.5 的「偏移变化即整屏脏重建」；行级脏模型表达不出整窗换源，与 `Storage::scroll_up` 同一理由）。不请求重排：盒尺寸没变。
- **备屏（`SPEC.FEAT.TERM.03`）天然不可滚**：`Terminal` 的备屏以 scrollback 容量 0 构造（`alt_{columns, rows, 0}`），故切到备屏后 `total_lines() == rows` → `max_offset() == 0` → `clamp_offset` 恒回 0，无需特判。像素用例已断言「备屏里连滚 5 格后两帧逐位相同」。
- 无惯性/动量：一步一格是刻意选择，`ScrollGlide` 的 150ms 吸附属框架 `Scroll` 路径，本层不引。
- **鼠标上报模式下的滚轮**（`SPEC.FEAT.TERM.06` 的转发与 alternate scroll）不在本棒，`on_scroll` 开头留一处 `TODO(SPEC.FEAT.TERM.06): 上报模式与备屏 alternate scroll 优先于本地回看`，代码路径恒走本地回看。**该 TODO 已随 2026-10-07 的 `SPEC.FEAT.TERM.06` 棒消除**，本地回看降为四档短路的最后一档，见裁决 **7.77**。
- **回看态遇到新输出：距底恒定（D6 已拍板①）**。`back_rows` 是用户意图，内核的 `offset_y` 只是它在当前 `total` 下的投影，故每帧拿到新的 `total_lines()` 后重投影一次（发生在 `Session::read` 的临界区内、`apply` 之前，因为 `apply` 要的就是这个返回值）：

```
每帧重投影（读到 total 之后）：
    kept_back    = max(0, max_offset_prev - offset_y)       // 用旧的 content/viewport 求上一帧的距底行数
    content_h    = total;  viewport_h = rows                // max_offset 随之变
    offset_y     = clamp_offset(max_offset_now - kept_back, 0.0F, 1.0F, content_h, viewport_h)
    back_rows    = round(max_offset_now - offset_y)         // 本次生效的距底行数，交 apply 与光标换算
```

  效果要读准：贴底态（`back_rows = 0`）随输出滚动；回看态下新输出**把可见窗整体向更早的方向推一行**——屏幕第 `i` 行显示的是推窗前一帧第 `i + 1` 行的内容，「距底 N 行」这个数不变。它**不是**「画面静止」，也不是「底部多出一行」；像素用例的判据形态就是 `row_band(grown, i) == row_band(again, i + 1)`（§9）。代价明写在 §0 表末行——长输出下固定片段会被顶走；日后若改取②（绝对行锚定），改动面就是这段去掉重投影，外加 `grid::Storage` 公共头补一个「已覆盖最旧行数」的单调计数（scrollback 饱和后逻辑行号整体左移，不补偿就会逐行漂移）。

---

## 7 光标（`SPEC.FEAT.RENDER.04`）

| 形态（`term::CursorShape`） | 画法 | 闪烁 off 相 | 失焦 |
|:---|:---|:---|:---|
| `Block` | 整格 `fill_rect(光标格盒, 光标色)`，字形在其上重画一次（前景取反色或原色，见下） | 不画块，字形照常 | 空心描边（四边 1/scale dp 的 `fill_rect`），静止 |
| `Underline` | 格底 `fill_rect({x, y + cell_height - t, w, t})`，`t = 1/scale`（D4） | 不画 | 同左，静止 |
| `Bar` | 格左沿 `fill_rect({x, y, t, cell_height})` | 不画 | 同左，静止 |

- 块形光标下的字形可读性：绘制序列 ③ 已在 ④ 之前把字形画上，故块形采用「先画字、后画块、再按反色重画该格一次文本」的三段式，代价只在光标格（一帧一次）。选择反色而非「块在下、字在上」的理由：主题光标色可能与前景同色，叠上去会让字符消失。
- 闪烁频率：本棒内置 500 ms，代码处挂 `TODO(SPEC.FEAT.PREF.02): 光标闪烁频率可配`（`SPEC.FEAT.RENDER.04` 的「可配置」项随设置面板那一棒）。
- 光标行不在本帧脏行集时的处理：把光标行并入本帧绘制行集（§6 的「∪ {光标行}」），因此闪烁不依赖整屏脏。
- 回看态的光标（D5②）：光标是**视口行** `v`，在屏幕上落在 `v + back_rows`；该值 ≥ `rows` 时光标不在可见窗内，**不画**（不是特例降级，就是它被滚出了画面）。`v + back_rows < rows` 时照常画在它所在的屏幕行上。
- `CSI ? 25 l` 的不可见由 `TermModes::cursor_visible` 表达，已在状态机侧。

---

## 8 帧唤醒与排帧（D2；消除 `src/session/session.cpp` 的 `TODO(SPEC.FEAT.RENDER.01)`）

- 装配层在造出 `Window` 后向 `Session` 注入唤醒句柄：`session.set_frame_wake([surface = &window.surface()] { surface->request_wake(); })`。`Surface::request_wake()` 头文件明写线程安全（跨线程唤醒阻塞在 `wait_events` 的帧循环）。
- 调用点：`Session::ingest()` 里**出锁之后**、且本次确有提交时调一次（一轮批量输入只唤醒一次，不按 push 次数唤醒）。这既是 D2 选①的理由，也是「锁内不做 IO」纪律的兑现。
- 主线程侧：`Application::set_on_frame(cb)`（回调在 `present_root` 前调用，故本帧就能生效）。回调里做三件事：① `view->on_frame()`——`drain_damage()` → `Session::read(...)` 临界区并入 `ScreenMirror`、取光标与模式 → `mark_needs_paint()`；② `ClipboardOutbox::drain()`（OSC 52 落地，裁决 7.21③ 约定主线程做）；③ `take_clipboard_write()` 的取走与编码写回（应答与剪贴板都不在锁内触达）。
- 顺序固定「先取队列、再读网格」——`session.h` 头注释已写明反过来会与读线程构成 ABBA 死锁，本棒沿用并在新用例里守住。

---

## 9 验收与用例形态

| 判据 | 用例（`itest_render_viewport.cpp`，除注明者外） | 实测手段 |
|:---|:---|:---|
| 尺寸来源（`SPEC.FEAT.XFER.01` 的 UI 取值腿） | `grid_size_downloads_from_the_viewport_box` | 控件首次布局即按自身可视 dp ÷ 整格 dp 派生行列，替身连接记录**只下发一次**；另断言 ① 层整盒（含内边距边带）是默认底色 |
| 单帧只重绘变更行（`SPEC.FEAT.RENDER.01`） | `frame_diff_stays_inside_dirty_rows_and_cursor` | 连渲两帧、第二帧只喂一行输出，用 `HeadlessSurface::data()` 的 RGBA8 做全像素差分，断言**变更行集恰等于**「本帧提交行 ∪ 本帧光标行 ∪ 上一帧光标行」（`changed_rows` 返回的正是行集合，判据写成集合相等而非子集——上一帧光标行必须在集内，光标残影就暴露在这里）；再逐格断言上一帧光标位整格回到默认底色 |
| 属性渲染：色带（`SPEC.FEAT.RENDER.03`） | `sgr_background_band_covers_exactly_its_cells` | 断言色带矩形覆盖该 run 的整格盒、边界外一格回到默认底色，颜色等于 `ui::resolve` 的输出（与 `utest_palette` 的纯逻辑值逐字段一致） |
| 属性渲染：装饰线（U4 / U2） | `underline_decoration_lands_on_the_shared_rects` | 断言下划线笔形落在 `ui::decoration_rects` 产出的同一批矩形上（像素与纯逻辑件同源，非另算一套坐标） |
| 斜体经批量入口（裁决 7.29①②） | `italic_runs_go_through_the_batch_entry` | 同行相邻两格同字形、一正一斜，各取整格像素带做差分，断言 `> 0`。**变异自证**：去掉批量重载的整批 opts 即该例失败、其余 9 例不动（反面写法「只断言有墨」实测空转，AA 灰度读不出斜切） |
| 块形光标重画保留斜体（`SPEC.FEAT.RENDER.04`） | `block_cursor_redraw_keeps_the_italic_slant` | 同一行相邻两格同一字形、一正一斜，块形光标逐帧移到其上；先以 `sample` REQUIRE 光标色确实落了笔（否则判据因另一条路径空转），再断言两格像素带差分 `> 0`。**变异自证**：`opts.italic = false` 即该例失败 |
| 光标三形态与失焦（`SPEC.FEAT.RENDER.04`） | `cursor_shapes_and_focus_state_land_on_their_cells` | 喂 `CSI 1..6 SP q` 后断言光标像素的**位置**（块=整格、下划线=底部规则、竖线=左沿），失焦态断言描边四边；光标停在空格上以免字形像素干扰落点判据 |
| 滚轮回看与距底恒定（D5②、D6①） | `utest_screen_mirror.cpp`（纯逻辑 9 例）+ `review_window_shifts_with_the_bottom_and_survives_new_output`、`alternate_screen_cannot_be_scrolled_back` | 像素层：连渲三帧「满屏 → 上滚 k 行 → 回到底」，断言中间帧差异覆盖全部行（整屏脏成立）且首屏与末屏**逐位相同**；D6① 的判据形态是 `row_band(grown, i) == row_band(again, i + 1)`；备屏连滚 5 格后两帧逐位相同 |
| 列位与字形对齐（`SPEC.FEAT.TERM.08` 的像素延伸） | `wide_and_zero_width_cells_keep_their_column_slots` | 以 SGR 底色段做「这一格被谁占了」的探针（底色走 `fill_rect` 不透明快速路径，可逐位比；字形是 AA 灰度，不可）：双宽格占两格宽且延续格不另推进列、combining 并入基础格且其后字符落在下一列。**素材内联在本例**（`CJK-LITERAL: cjk-fixture`），未提为 `tests/fixtures/` 夹具——只此一处消费，提取反而多一层间接 |

- **本棒未验收的两项**：帧唤醒（D2）的真机 e2e——逻辑层已由 `utest_session.cpp` 的 5 例锁住「出锁后唤醒一次、无提交不唤醒」，尚欠一条投放交互桌面的 e2e（裁决 7.19⑤：让子进程延迟输出，断言无键盘输入时窗口也被唤醒并排帧）；吞吐（`SPEC.NF.PERF.02`）已落，形态＝`tools/bench` 的 `borealis_bench` 三场景 + `tools/check` 的时间门禁（裁决 7.23③ 的要求，执行口径见裁决 7.34）。

- **「裁剪盒外不画」不作像素断言**（实测改口径）：脏区上报走非虚的 `Widget::dirty_bounds()`，恒等于控件自身盒，控件无法把子矩形报成脏，于是像素差分观察不到那条纪律；它仍是 §6 行集折算的行为，只是没有可观测面。用例文件头注释已按此说明。
- **像素判据的三类写法只有一类成立**（实测教训，写死以免后人重蹈）：①「同格两帧对比」与 ②「跨行对比」都空转——前者在内容未变时本就逐位相同，后者底色与相位都不同，差异与被判据无关；成立的形态是 **同底色、同行、相邻两格对照 + 整格取带（`cell_band`）+ 先 REQUIRE 被画的那一层确实落笔**。`preflight()` 要求格宽为整数，否则带子的格-local 坐标对不齐，判据会假绿。
- 差分工具**未用**框架的 `au::render::compare_snapshots` / `SnapshotDiff`（`include/aurora/render/snapshot_diff.h`）与 `render_to_image`：本文件的判据是「差集落在哪个行集/格内」而非「差异比例是否超阈值」，`HeadlessSurface::data()` 自取 RGBA8 + 本地 `count_diff` / `changed_rows` / `cell_band` 更直接。该头注释的坑仍记着：headless 缓冲起始为全透明黑（真实 Surface 清成 `{245,245,247,255}`），故凡比到「没画」的判据都必须**比到 alpha**，只比 RGB 会把「画了个纯黑」与「什么都没画」混为一谈。

---

## 10 随代码一起的回写项（2026-10-02 全部执行完毕）

1. ✅ `AGENTS.md` §2 的 `src/` 行补 `src/ui/terminal_view.{h,cpp}` 与 `src/session/screen_mirror.cpp`、`include/borealis/` 行补 `session/screen_mirror.h`；§6「尚无」条目里的 `include/borealis/ui/terminal_view.h` 已删除并注明**「绘制侧控件刻意无公共头」是裁决 D1① 的形态而非欠项**。
2. ✅ `ARCHITECTURE.md` §9.2 标题改为「视口控件的取用形态与绘制序列（已落地）」并吸收本稿 §3/§6/§7 的正文（五层序列、两批 opts、光标三段式的 opts 纪律、裁剪无像素可观测面），§9.5 补记 D5②/D6① 的实际形态（内核量以「行」为单位、不声明 `overflow_strategy(Scroll)` 的理由、`remaining_y` 上冒、备屏天然不可滚），§3.2/§3.3 的唤醒描述按本稿 §8 改写并加「已落地形态」段，§9.6 改写为「无框架阻塞项」。**两份 `.draft.md` 与配图保留**：正式文档折进的是正文判据，视觉对照图与逐条 V1~V27 / §9 的实测手段明细仍被 `PLAN.md` §8 与 `CHANGELOG.md` v0.25 引用，删除会造出死链（AGENTS.md §4.2 第 13 条）。
3. ✅ `SPECIFICATIONS.md` §7 新增裁决 7.28（下划线 SGR 编码与笔形）与 7.29（四条缺口回货与 G13 分流撤销）——本稿的 D1~D6 六项全部取了推荐项，故除这两条实测口径外无需另立裁决；`CHANGELOG.md` 记 **v0.25**（本稿写作时预想的 v0.22 已被配置层之前的批次占用，v0.22~v0.24 是宽度判定、视觉稿与配置层的条目）；`PLAN.md` §8 的 M1 现状与待接接缝表按落地结果更新，并新增「网格 → 主线程副本 → 视口绘制」一行。
4. ✅ `src/session/session.cpp` 的 `TODO(SPEC.FEAT.RENDER.01)` 已随唤醒接线消除，残留的 `au::post_to_main` 表述一并去掉（全仓 grep 无该符号）；闪烁频率仍内置，`TODO(SPEC.FEAT.PREF.02)` 挂在装配处 `src/main.cpp` 的帧回调注释点名「配置里的光标缺省形态与闪烁档、Ambiguous 口径尚无会消费的字段」，闪烁周期本身作为构造参数进 `TerminalView`），另有一处 `TODO(SPEC.FEAT.TERM.06)` 在 `src/ui/terminal_view.cpp` 的 `on_scroll` 上报模式分支（**该 TODO 已随 2026-10-07 的 `SPEC.FEAT.TERM.06` 棒消除，见裁决 7.77**）。

---

## 11 原推断项的实测结论（保留条目以示取证过程）

- **`on_layout` 在 `loose_height` 下会拿到无限 `max`** → **成立**：根为 `Column` 时子项确实收到 `max` 无限的松散约束，`c.max.is_finite()` 分支不是死代码（框架 `Constraints` 的「无限 = 不限制」语义按头注释如述）。落笔形态是「无限时沿用上一次的盒」，因此不请求重排。
- **块形光标的「按底色重画一次」与 ③ 的批量输出能否逐位兼容** → **成立但踩过坑**：第三次只画光标格内的子串并按该格合成底色作前景，像素效果正确（光标三形态用例通过）。缺陷出在**这一段没带 `TextLayoutOpts`**——撤销 G13 分流时只改了 ③ 的批量入口，光标停在斜体格上会把那一格静默画成正体（整帧其余部分逐位相同，肉眼与当时九例都发现不了）。补 `opts.italic = paint.italic`（与 ③ 同源）并加第十例，判据形态见 §9。**教训**：同一次「样式属性」在绘制序列里可能有多个下笔点，撤销过渡件时须按属性逐点核对，不能只改主路径。
- **`mark_needs_paint()` 在 `set_on_frame` 回调内能否赶上同一帧** → **成立**：`present_root` 之前标脏即进入本次 paint，证据是差分类用例（`on_frame` 里标脏 → 同帧 `present_root` 的帧缓冲就含新行）。不需要退路 `request_frame()`。
- **`HeadlessSurface` 与 Win32 后端能否共存于本仓构建** → **成立**：`AURORA_BACKEND_HEADLESS` 默认 ON 且本仓未在 `add_subdirectory` 前关它，`itest_render_viewport` 的十例在 `--run` 下全部实跑、无一条 SKIP（同一 runner 的 `etest_*` 仍走 Win32 后端，只是不入非 e2e 的 CTest 集）。
- **滚轮增量与物理 notch 的换算** → **口径未变，真机数值仍未实测**：`step = 1.0F`（1 单位 = 1 行）在逻辑与像素层都被用例锁住（滚一格 = 窗动一行），但 Win32 后端把 `WM_MOUSEWHEEL` 的 ±120 归一到几属真机手感项，随真机走查一并确认；若实测一格给 ±3 或 ±120，须在 `on_scroll` 前除一次，不改本稿的取整与 clamp 口径。
- **触控板式小增量的手感** → **仍未实测**（无触控板真机环境）：`offset_y` 累积非整数、画整数行（`lround`）而残差留在内核里，逻辑上不会丢行，「滚半行不动、下一格跳两行」的风险只有真机能判。
- **滚轮要求鼠标悬停在控件盒内** → **框架通用行为已实测**（`wants_scroll()` 沿命中链自最深向根找第一个 wants_scroll 者），故鼠标在窗口外滚动不回看；是否符合终端软件预期（部分终端「焦点在窗口即响应」）随真机走查判定，已写入架构 §9.6。
