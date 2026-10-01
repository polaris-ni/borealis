# 上屏层第一棒设计稿（`SPEC.FEAT.RENDER.01` / `03` / `04` 主路径）— **评审稿**

> 状态：**待评审**。评审通过后本稿内容并入 `codespec/ARCHITECTURE.md` §9（渲染接入与视口）并从 `codespec/` 删除本文件；未通过则按批注整改后再评审。
> 配图：`codespec/RENDER_VIEWPORT.draft.svg`（A 控件盒解剖 / B 层叠顺序 / C 脏行过滤 / D 光标三形态 / E 帧唤醒时序）。
> 前置已闭合：G1 三腿（裁决 7.20 / 7.22）、取用形态（裁决 7.23）、纯逻辑两块（`ui::palette`、`ui::cell_layout`，CHANGELOG v0.20）、DECSCUSR 状态机侧（CHANGELOG v0.21）。
> 框架侧 API 一律按 2026-10-01 实测签名书写，未实测的推断在文末 §11 单独标注。

---

## 0 需要拍板的五项（本稿的唯一未定处）

| # | 议题 | 候选 | 推荐 | 影响面 |
|:---|:---|:---|:---|:---|
| D1 | `TerminalView` 类声明放哪 | ① `src/ui/terminal_view.h`（私有头）＋装配点在 `src/main.cpp`；② 破例进 `include/borealis/ui/terminal_view.h` | **①** | 架构 §2.3「公共头不含框架头」与 §6.3、§5.4 既有先例（`src/term/osc.h`、`src/platform/win/conpty_connection.h`）一致；选② 就得为它写例外说明，并让所有含此头者被迫依赖框架编译 |
| D2 | 帧唤醒句柄的持有方 | ① `session::Session` 增 `set_frame_wake(std::function<void()>)`，在**出锁后**、本次确有提交时调用；② `DamageQueue` 构造时持句柄、push 时调用；③ 不唤醒，靠主循环超时轮询 | **①** | ② 会把回调放进网格锁临界区（`push_rows` 在锁内），与「锁内不做 IO」的纪律相抵；③ 在 `Surface::wait_events` 阻塞期间输入延迟不可控，`SPEC.NF.PERF.02` 的输入延迟项直接受损 |
| D3 | 主线程本地副本的归属域 | ① `session` 域新增纯逻辑件（`session::ScreenMirror`）；② `ui` 域新增同名件；③ 副本直接由 `TerminalView` 持有、不抽件 | **①** | 副本的输入是「权威网格 + 脏行提交」，与 `DamageQueue`/`Session` 同一话题，归 session 域可全量单测（`grid::Storage` 侧无框架依赖）；选③ 则副本合并逻辑只能在像素层验，回归面变大 |
| D4 | 装饰线与光标的粗细口径 | ① 一律「1 物理像素」= `1.0F / scale` dp，下划线贴基线下方、删除线取 ascent 中点；② 用 `Painter::draw_line`（AA 圆帽） | **①** | `draw_line` 是抗锯齿线，半透边缘会让 1px 规则在暗底上发灰；`fill_rect` 落在整 dp 坐标上，与网格边界天然对齐。粗细不随字号放大是本条的**刻意选择**，需在 §9 验收里写清 |
| D5 | 本棒是否画 scrollback 偏移 | ① 恒画视口（偏移 0），滚动随后续棒接；② 现在就接滚轮与 `ScrollViewport` | **①** | 裁决 7.23④ 把文本选择/IME/尺寸去抖划出本棒，滚动浏览与选区跟随同属交互层；现在做会把 §9.5 的自管偏移与「偏移变化即整屏脏」两条一起拖进本棒的验收面 |

---

## 1 范围

- **做**：终端视口控件的绘制主路径（网格 → 色带 → 文本 run → 装饰 → 光标）、整格几何与 dp 换算的消费、尺寸来源（`SPEC.FEAT.XFER.01` 的 UI 侧取值腿，不含去抖）、跨线程帧唤醒与剪贴板排帧、`HeadlessSurface` 两帧像素差分的验收用例。
- **不做**（各归其棒，本稿不留半成品接缝）：文本选择与选区着色（`SPEC.FEAT.INTERACT.02`）、IME preedit 绘制与候选窗定位（`SPEC.FEAT.INTERACT.06`）、键映射与字节发送（`SPEC.FEAT.INTERACT.01`）、scrollback 滚轮浏览（D5）、字号/字体可配（`SPEC.FEAT.RENDER.02`）、DPI 变更后的度量与字形缓存重建（`SPEC.FEAT.RENDER.05`）、尺寸去抖合并（裁决 7.23④）、吞吐基准与时间门禁（`SPEC.NF.PERF.02`，本仓 task #36）。

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
| `on_layout` | `auto on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size`（protected virtual，纯虚） | 撑满父级：`max` 有限时取 `c.constrain({c.max.width, c.max.height})`；被给成无限（Flex 主轴 `loose_*` 的按需上限）时退回自然尺寸 `columns × cell_width`。尺寸变化即触发 §5 的几何重取 |
| `on_paint` | `auto on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void`（protected virtual，纯虚） | §6 的四层序列。`bounds` 是**全局逻辑 dp** 的内容盒，且框架给的是全量 bounds（裁决 7.23ⓐ） |
| `type_name` | `[[nodiscard]] auto type_name() const -> const char *`（纯虚） | 返回 `"TerminalView"`（诊断与 `WidgetRegistry` 的键；本棒不注册 JSON 工厂） |
| `wants_focus` | `[[nodiscard]] auto wants_focus() const -> bool` | 覆写为 `true`（基类已 true，此处显式写出以免后续改动悄悄关掉焦点） |
| `on_focus_change` | `auto on_focus_change(bool focused) -> void` | 记录焦点态，供 §7 的「失焦降级为空心/静止」（`SPEC.FEAT.RENDER.04`） |
| `tick` | `auto tick(std::chrono::steady_clock::time_point now) -> void` | 闪烁相位；相位翻转即 `mark_needs_paint()`。不新起线程（裁决 7.23 验收映射的既有约定） |
| `can_cache_display_list` / `can_cache_layout` | `[[nodiscard]] ... const -> bool` | 覆写为 `false`：本控件每帧内容都可能变，缓存只会掩盖脏行过滤的缺陷 |

失效路径：`mark_needs_paint()`（本控件自绘失效的唯一入口）；`request_frame()` 只用于尺寸变化后请求重排。装配时以 `.width(au::fill()).height(au::fill())` 声明意图，父级是 `au::Column{}`（`src/main.cpp` 现状即 Column 根）。

成员（全部主线程所有，无锁）：`session::Session *session_`（非拥有，生命周期由装配层保证）、`session::ScreenMirror mirror_`、`ui::PaletteSpec spec_`、`ui::GridGeometry geom_`、度量缓存 `{au::Font ref_font_, float scale_, ui::CellPixels cell_px_}`、`term::Cursor cursor_` / `term::TermModes modes_`（副本，帧内取用）、`bool focused_`、`bool blink_on_`、`std::size_t scrollback_offset_rows_ = 0`（D5：本棒恒 0，仅作为 §9.5 自管偏移的落点，不参与绘制分支）。

---

## 4 本地副本（`session::ScreenMirror`，D3）

**为什么必须有副本**：架构 §3.4 拍板「后台权威 + 主线程增量快照」，绘制在锁外；`Session::read()` 明确把可变 `grid::Storage &` 交给读取方并约定「按本帧提交把脏列区间并入本地副本，随后 `clear_dirty()`」。这份合并语义即本件。

对外形态（只含标准类型与 `borealis::grid` 类型）：

- `apply(const grid::Storage &authoritative, std::span<const session::Damage> frame) -> void`：把提交里的行区间（或 `full_screen` 标志）从权威网格复制进自身；复制粒度是**整行的 cell + 零宽侧表 + 超链接侧表**（列级增量已由 `DamageQueue` 合并到行区间，行内再切片不省成本）。
- `line(std::size_t row) const -> const grid::Row &`、`columns()`、`rows()`。
- 尺寸变化（权威网格 `columns`/`visible_rows` 与自身不一致）即整屏重建，不做搬移——与裁决 7.17「行号与内容的对应关系整体变了须整屏重建」同口径。

单测判据（无需框架）：只并脏行、`full_screen` 并全部、尺寸突变整屏重建、未并入的行保持旧值（证明没有整屏拷贝的退化路径）、侧表随行搬运。

---

## 5 几何与尺寸来源（`SPEC.FEAT.XFER.01` 的 UI 腿）

```
scale      = painter.scale()                         // 物理像素 / 逻辑 dp
cell_px    = au::render::FontEngine::monospace_cell(ref_font, scale)   // 物理像素整格度量
geom       = ui::make_geometry(ui::CellPixels{cell_px.cell_width_px, cell_px.cell_height_px, cell_px.ascent_px},
                               scale, ui::LogicalSize{bounds.size.width, bounds.size.height})
session.resize(session::Size{geom.columns, geom.rows})   // 仅在行列真的变了时（Terminal::resize 内部已同尺寸去抖）
```

- **一个参考 `Font` 取一次度量**（裁决 7.22④ / 架构 §9.2）：`ref_font_` 变化（字号、族）或 `scale` 变化才重取 `monospace_cell`；粗体只换 `weight` 进 `TextRun.font`，不改格宽。
- `monospace_cell` 的 `scale` 入参取与 `Painter::scale()` 同值（头文件明写）。
- 尺寸来源即 `on_paint` 拿到的 `bounds.size`（首帧在 `on_layout` 之后），故**行列数由控件实际尺寸派生**，不新造一条「窗口尺寸」通道。
- 本棒**不做去抖**（裁决 7.23④）：拖窗口时每帧最多下发一次；代价是整屏脏重建随行数变化发生，验收里不锁这项。

---

## 6 绘制序列（`on_paint`，四层）

```
clip = p.clip_bounds()                                    // 全局逻辑 dp，与各层裁剪的交集（实测口径 ⓐ）
可见行 = [floor((clip.top - bounds.origin.y) / cell_height) .. ceil((clip.bottom - ...) / cell_height))
        ∩ [0, geom.rows)                                   // 窗外行整体跳过
行集 = mirror 脏行 ∪ {光标行}（整屏脏时为全部可见行）

① p.fill_rect(clip ∩ 内容盒 ∩ 全部可见行的带, 主题默认背景)      // 一屏一次
② 对每条 StyleRun：paint.background != 默认底色 → p.fill_rect(色带矩形, 该色)
③ 对每行一次 p.draw_text_runs(runs)                            // TextRun{.text=run.text, .box.origin=格左上, .font, .color}
④ 装饰：run.paint.underline / strike → p.fill_rect(1/scale dp 高的规则)
   光标：modes.cursor_visible && 相位为 on → 按 §7 形态 fill_rect
```

- `TextRun.text` 是 UTF-8 `std::string_view`，指向我方 `StyleRun::text` 的缓冲（本层不重编码，裁决 7.22② 的「只收合成后的最终值」）；run 的生命周期到 `draw_text_runs` 返回为止，故按行建临时 vector。
- `TextRun.box` **只读 origin**，宽高不参与布局，故 `Rect` 的 size 填该 run 的盒宽（跨 `last_column - first_column` 个格）只为诊断可读，不影响落笔。
- 颜色：`ui::resolve(cell, spec)` 在 `layout_row()` 内已完成（含 bold-is-bright、暗淡、反色、最小对比度），绘制侧只做 `RgbaColor → au::Color` 逐字段搬。
- 不可见（`CellPaint::hidden`）：`layout_row()` 已保留底色段、丢弃文本段，绘制侧无需再判——这是 v0.20 定下的分工，本棒不重复实现。

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
- `CSI ? 25 l` 的不可见由 `TermModes::cursor_visible` 表达，已在状态机侧。

---

## 8 帧唤醒与排帧（D2；消除 `src/session/session.cpp` 的 `TODO(SPEC.FEAT.RENDER.01)`）

- 装配层在造出 `Window` 后向 `Session` 注入唤醒句柄：`session.set_frame_wake([surface = &window.surface()] { surface->request_wake(); })`。`Surface::request_wake()` 头文件明写线程安全（跨线程唤醒阻塞在 `wait_events` 的帧循环）。
- 调用点：`Session::ingest()` 里**出锁之后**、且本次确有提交时调一次（一轮批量输入只唤醒一次，不按 push 次数唤醒）。这既是 D2 选①的理由，也是「锁内不做 IO」纪律的兑现。
- 主线程侧：`Application::set_on_frame(cb)`（回调在 `present_root` 前调用，故本帧就能生效）。回调里做三件事：① `view->on_frame()`——`drain_damage()` → `Session::read(...)` 临界区并入 `ScreenMirror`、取光标与模式 → `mark_needs_paint()`；② `ClipboardOutbox::drain()`（OSC 52 落地，裁决 7.21③ 约定主线程做）；③ `take_clipboard_write()` 的取走与编码写回（应答与剪贴板都不在锁内触达）。
- 顺序固定「先取队列、再读网格」——`session.h` 头注释已写明反过来会与读线程构成 ABBA 死锁，本棒沿用并在新用例里守住。

---

## 9 验收与用例形态

| 判据 | 用例 | 手段 |
|:---|:---|:---|
| 单帧只重绘变更行（`SPEC.FEAT.RENDER.01`） | `itest_render_viewport.cpp`（新增，纯逻辑 + headless） | `au::HeadlessSurface`（`AURORA_BACKEND_HEADLESS` 默认 ON，实测）连渲两帧：第一帧全量、第二帧只喂一行输出，用 `HeadlessSurface::data()` 的 RGBA8 做全像素差分，断言差异像素**只落在那一行的高度带内**；另一例断言 `clip_bounds()` 之外的行不被画（局部帧形态） |
| 属性渲染（`SPEC.FEAT.RENDER.03`） | 同上 | 断言色带矩形覆盖该 run 的整格盒、颜色等于 `ui::resolve` 的输出（与 `utest_palette` 的纯逻辑值逐字段一致） |
| 光标三形态（`SPEC.FEAT.RENDER.04`） | 同上 | 喂 `CSI 1..6 SP q` 后断言光标像素的**位置**（块=整格、下划线=底部规则、竖线=左沿），失焦态断言描边四边 |
| 列位与字形对齐（`SPEC.FEAT.TERM.08` 的像素延伸） | 同上 | 沿用 `tests/integration/itest_unicode_width.cpp` 的同一段含 CJK / Ambiguous / combining 的素材（若需共享则提为 `tests/fixtures/term/` 夹具，与既有 `utf8_cases.tsv` 同目录），断言双宽格占两格宽、combining 不额外推进列（叠字是否显形依赖框架 shaping，像素判据只到「不破坏网格」） |
| 尺寸来源（`SPEC.FEAT.XFER.01` UI 腿） | `utest_screen_mirror.cpp` + 控件层一条 | 断言 85 dp ÷ 8 px 的取整与 `make_geometry` 一致；控件侧断言尺寸变化时 `Session::resize` 收到同一行列数（替身连接记录） |
| 帧唤醒（D2） | 一条 e2e（投放交互桌面，裁决 7.19⑤） | 真机 ConPTY 会话里让子进程延迟输出，断言无键盘输入时窗口也被唤醒并排帧（`frame_count()` 递增），关掉唤醒即不复现 |
| 吞吐（`SPEC.NF.PERF.02`，**不在本棒**） | task #36 | `tools/bench` + 时间门禁，劣化 >10% FAIL（裁决 7.23③） |

差分工具复用框架既有件：`au::render::compare_snapshots` / `SnapshotDiff{pixel_diff_count, diff_ratio, passed(max_ratio)}`（`include/aurora/render/snapshot_diff.h`）与 `render_to_image(Node &, int, int, optional<Color>)`（不需要 Surface 的场合）。注意其头注释的坑：headless 缓冲起始为全透明黑，而真实 Surface 清成 `{245,245,247,255}`——两帧对比必须显式传同一背景色，否则首帧与次帧的差集会包含背景自身。

像素判据的写法遵循既有教训：**差分要比到 alpha**，且断言「差异行集 ⊆ 队列提交行集」而不是「等于」，避免框架自身的整屏清屏动作被算成回归。

---

## 10 评审通过后随代码一起的回写项

1. `AGENTS.md` §2 的 `ui/` 行补 `src/ui/terminal_view.{h,cpp}` 与 `session/screen_mirror.h`，§6「尚无」条目里 `include/borealis/ui/terminal_view.h（绘制侧控件的公共头）` 改为私有头形态（D1）。
2. `ARCHITECTURE.md` §9.2 追加「控件形态与绘制序列」小节（本稿 §3/§6/§7 的正文），§9.5 补记 D5 的「本棒偏移恒 0」现状，§3.2/§7 的唤醒描述按 §8 的注入形态补一句；随后删除本 `.draft.md` 与配图，或把配图移入正式文档引用路径。
3. `SPECIFICATIONS.md` §7 新增裁决（若 D1/D2/D3 中任一项选了非推荐项，须写成裁决而非只在代码里体现）；`CHANGELOG.md` 记 v0.22；`PLAN.md` §8 的 M1 现状与待接接缝表按落地结果更新（`SPEC.FEAT.RENDER.01/03/04` 从「未开工」改为「绘制主路径已落，余 …」）。
4. `src/session/session.cpp` 的 `TODO(SPEC.FEAT.RENDER.01)` 在唤醒接线后消除，其注释里残留的 `au::post_to_main` 表述按裁决 7.23ⓒ 一并改掉（该符号只在 `aurora::detail` 下存在，不属消费方的长期依赖面）；若闪烁频率仍内置，则新增 `TODO(SPEC.FEAT.PREF.02)` 一处。

---

## 11 推断与未验证项（评审时请重点看这几条）

- **`on_layout` 在 `loose_height` 下会拿到无限 `max`**：这是从 `Constraints` 头注释（「max = 无限 = 不限制」「loose_* 是按需上限」）推出的分支，**尚未实测**根为 `Column` 时终端控件实际收到的约束值。若实测为有限，则该分支是死代码，按 AGENTS.md §4.1 第 5 条应删。
- **块形光标的「反色重画一次」能否与 ③ 的整行 `draw_text_runs` 输出逐位兼容**：`draw_text_runs` 与 `draw_text` 逐位相同（裁决 7.22 实测），但**同一段文本画两遍**（先原色后反色）会产生字形叠印；本稿的解法是第二次只画光标格内的子串并按底色重画该格色带。该做法的像素效果未实测，属需要在第一条用例里先跑通再定型的部分。
- **`mark_needs_paint()` 在 `set_on_frame` 回调内调用能否赶上同一帧**：依据是 `Application` 的 `on_frame_` 注释「每帧回调（在 present_root 前调用）」。present 之前的失效是否真的进入本次 paint 未实测；若否，退路是句柄改走 `request_frame()`（同样未实测）。
- **`HeadlessSurface` 是否能在本仓构建里与 Win32 后端共存**：`AURORA_BACKEND_HEADLESS` 默认 ON，本仓未在 `add_subdirectory` 前显式关闭它，故推断可用；未实测。
- **`ScrollViewport` 的 `step` / `clamp_offset` 语义**（本棒不用，仅在 §9.5 未来接滚动时要用）：其 `delta_y` 正方向为「向上滚动」的符号约定是头注释原文，未做行为实测。
