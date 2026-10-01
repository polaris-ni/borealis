# CHANGELOG.md — Borealis 需求规格书变更历史

> 本文件记录 [`SPECIFICATIONS.md`](SPECIFICATIONS.md) 的版本演进，并保存**旧纯数字需求编号 → 新标识**的完整映射。
> 规格书正文只述需求，不含优先级与交付分期（那部分属 [`PLAN.md`](PLAN.md)）。本文件是历史记录：早期版本条目沿用其**当时**的优先级与里程碑口径原文，不做回填改写，以便对照每一次调整的取舍依据。
> 现行需求标识规范见 [`SPECIFICATIONS.md`](SPECIFICATIONS.md) §1.4。

---

## v0.21（2026-10-01）DECSCUSR 光标形态进状态机：`SPEC.FEAT.RENDER.04` 的最后一块纯逻辑前置

**动机**：`SPEC.FEAT.RENDER.04` 的三形态写明「随 DECSCUSR 切换」，而状态机此前不解释 `CSI Ps SP q`——编辑器与 shell 换提示符时的光标形态请求会静默落到「未识别终结符」分支被忽略，绘制侧再怎么画也只有块状。v0.20 的边界⑤把这一笔单独列为绘制棒之前的前置项，本批就是它；落完之后渲染第一棒在状态机侧不再有欠账。

- **状态机侧（`include/borealis/term/terminal.h` + `src/term/terminal.cpp`）**：新增 `term::CursorShape`（`Block` / `Underline` / `Bar`）与 `TermModes` 的 `cursor_shape`、`cursor_blinking` 两字段。`do_csi` 的 `q` 分支以**空格中间字节为识别标志**——缺了它 `CSI 5q` 是另一条序列，照单全收的话任何以 `q` 结尾的未实现序列都会改掉光标形态；带私有前缀的 `CSI ? Ps SP q` 一并接受（xterm 同口径）。档位表照 xterm：1/2 块、3/4 下划线、5/6 竖线，奇数闪烁、偶数静止；**缺省 Ps、显式 0 与越界档位整档回落默认闪烁块，而不是保留旧值**——设备发出未定义档位通常意在复位，留着上一档会让形态取决于历史输入。`reset_to_default()`（`ESC c`）随 `modes_ = {}` 一并复位，无需额外分支。会话侧零改动：`Session` 把整份 `TermModes` 交给读取回调，新字段自动抵达未来的绘制方。
- **测试**：`tests/unit/utest_terminal.cpp` 新增 2 例（六档形态与闪烁逐档断言、缺省与越界档位的回落、空格中间字节的识别标志与私有前缀写法）并给既有的 RIS 复位用例补上光标形态两条断言。非 e2e 的 15 项全绿（MSVC + Ninja）；e2e 两项里 `etest_local_terminal` 绿，`etest_osc_clipboard` 仍停在 `OpenClipboard` 返 5，成因与判定见 v0.20，与本批无关。
- **代价与边界**：① 本批只表达**意图**，不画光标：形态与闪烁的像素呈现、以及 `SPEC.FEAT.RENDER.04` 的「可配置闪烁频率」与「失焦降级为空心/静止」全在绘制棒，闪烁节奏按裁决 7.23④ 由框架既有的 `Widget::tick(now)` 驱动，不新起线程；② 闪烁档只随 DECSCUSR 变，`CSI ? 12 h`（DEC 光标闪烁开关）一类未实现也未建模，遇到即维持当前档；③ 未知 Ps 的回落选择是本仓自定的实现细节——`SPEC.FEAT.RENDER.04` 未规定越界行为，改成「保留旧值」须同时动测试。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是裁决 7.23④ 第一棒的状态机收尾，`SPEC.FEAT.RENDER.04` 的语义未改，只把「随 DECSCUSR 切换」变成状态机里可观察的取值。

---

## v0.20（2026-10-01）上屏层两块纯逻辑前置落地：颜色合成与整格几何 / run 切分（SPEC.FEAT.RENDER.01、SPEC.FEAT.RENDER.03）

**动机**：裁决 7.23 把上屏主路径分成两半——无框架依赖的算式，与唯一触达 `au::Painter` / `au::Widget` 的绘制翻译单元。先把前者落定，是因为最容易算错的两处（SGR 颜色的合成次序、`monospace_cell` 物理像素与 `TextRun` 逻辑 dp 的换算）都必须钉在测试里，而不是等绘制侧接上后从像素反推；后者只剩层叠顺序与裁剪过滤。

- **`ui::palette`**（`include/borealis/ui/palette.h` + `src/ui/palette.cpp`，`SPEC.FEAT.RENDER.03`）：`PaletteSpec`（16 基本色 + 默认前景背景 + bold-is-bright 与最小对比度两个开关及其阈值）经 `resolve(grid::Cell, PaletteSpec) -> CellPaint` 出最终绘制意图。合成次序定死为**亮色档 → 暗淡 → 反色 → 最小对比度**：反色若排在亮色档之前，粗体亮色档的格子反色后会把基色当亮色用；最小对比度必须最后，因为前三步都可能把颜色拉近背景。256 色的 16..231 走 6×6×6 立方、232..255 走 24 阶灰的 xterm 固定式子，不受主题影响故不入库配置。两处口径值得单独记录：① **暗淡按「向底色靠拢一半」而非减亮度**——底色不受本仓控制（主题与真彩色背景都可能比前景更暗），减亮度在浅色主题上会把暗淡字推成高对比的深色字，语义正好相反；② **最小对比度只动前景**，朝与背景对比更强的那一极做整数档位二分（256 档）取「达标的最浅插值」，不一路推到纯白纯黑；两侧极端的对比都达不到阈值时取较强一侧，不为凑数去改背景——背景是整屏色带的基准，动它会让相邻格的底色互不一致。对比度按 sRGB 线性化（IEC 61966-2-1 分段式）的 WCAG 相对亮度。
- **`ui::cell_layout`**（`include/borealis/ui/cell_layout.h` + `src/ui/cell_layout.cpp`，`SPEC.FEAT.RENDER.01` 与裁决 7.23②）：`make_geometry(CellPixels, scale, LogicalSize) -> GridGeometry` 把整像素度量折成 dp 步长并给出行列数（向下取整、装不满一格仍给一格、度量或可视尺寸不可用则行列 0），这就是 `SPEC.FEAT.XFER.01` 一直缺的「尺寸来源算式」，widget 侧取值与去抖仍待接线；除法容差 `1e-9` 经实测必需——7 px 在 1.5× 下是无限二进制小数，可视宽由同一次除法反乘回来时比值差出 1e-15 量级，无容差就少算一整列一整行。`layout_row(grid::Row, PaletteSpec) -> vector<StyleRun>` 按**样式全等**（前景 / 背景 / 字重三者）合并到行，于是色带矩形与文本片段共用同一批切分边界、区间表只维护一张；双宽延续格不进文本但留在列区间内（字形才占得满两格宽）；combining 取 `grid::Row` 侧表**拼在基础码点之后随同一段文本**送出（裁决 7.23ⓑ，叠字与零推进由框架 shaping 承担）；文本按 `TextRun.text` 的口径在本层就编成 UTF-8，绘制侧不再二次编码。文本、装饰、色带三者皆无的区间整段丢弃（全空白文本、无下划线与删除线、底色等于主题默认底色），一屏空格的常见形态就此收敛成零条 run。
- **测试**：`tests/unit/utest_palette.cpp` 11 例（三段索引式子、色值按来源解析、bold-is-bright 的四类不适用形态、暗淡与反色次序、不可见保留底色、WCAG 三组对照值、最小对比度的「达标即止」与不可达时取较强一侧及其 alpha 不变、开关门控）；`tests/unit/utest_cell_layout.cpp` 14 例（scale 换算、整格计数与至少一格、非整除缩放的取整容差、不可用度量与最小化窗口、矩形映射、空白行不产段、同样式合并、切分边界共用、字重分裂、双宽延续格不进文本、combining 并字、不可见段丢弃而底色保留、裸下划线的空格段仍需交绘制侧、纯色带段）。两个新套件与既有用例一并入 CTest：非 e2e 的 15 项全绿（MSVC + Ninja）；e2e 两项里 `etest_local_terminal` 绿，`etest_osc_clipboard` 在本机两次（含投放到交互桌面会话）都停在 `OpenClipboard` 返 5——同机 PowerShell 读剪贴板同样报错，属系统级占用而非代码回归（e2e 的会话要求见裁决 7.19⑤）。
- **代价与边界**：① 本批**不含任何绘制代码**，`src/ui/terminal_view.cpp` 与帧唤醒在下一棒，故渲染层在屏幕上仍不可见；② `include/borealis/ui/` 两个头刻意不含 Aurora 类型，`CellPixels` 与框架 `render::CellMetrics`、`RgbaColor` 与 `au::Color` 的互转点全留绘制 TU，本层用 `int32_t` 与 `double` 是刻意的口径隔离；③ 颜色合成的三处数字（暗淡一半、最小对比度 256 档插值、256 色的式子）属本仓自定的实现细节——`SPEC.FEAT.RENDER.03` 只要求「可配开关」，调整它们必须同时动测试；④ `StyleRun` 是每帧每行的临时结构，切分成本随行宽线性，颜色频繁交替的行会把 run 数推到格数量级，这是裁决 7.23② 已认下的代价，优化面在切分而非框架原语；⑤ `SPEC.FEAT.RENDER.04` 光标三形态所需的 DECSCUSR（`CSI SP q`）在状态机侧仍未实现，是绘制棒之前的单独一笔。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是裁决 7.23②④ 的第一块落地，`SPEC.FEAT.RENDER.01` 与 `SPEC.FEAT.RENDER.03` 的语义未改，只把「色带与片段共用切分边界」从裁决文字变成可执行实现。

---

## v0.19（2026-10-01）上屏层的取用形态拍板：视口自管、run 按样式合并、吞吐挂门禁（裁决 7.23）

**动机**：G1 三腿闭合后 `SPEC.FEAT.RENDER.01` 不再有框架阻塞，渲染层开工前须先把形态定死；而实测 Aurora 头文件时撞到四处「需求与架构都没写、但会决定正确性」的现实——① 框架局部帧只把裁剪盒压进 `Painter` 裁剪栈，`on_paint` 收到的仍是**全量 bounds**，于是「单帧只重绘变更单元格」只能由绘制侧自行过滤，不是框架替我们裁好；② `monospace_cell` 给物理像素而 `TextRun.box` 原点收逻辑 dp（裁决 7.22②的刻意例外），两者混用即整屏偏移一个 scale 倍数；③ 跨线程唤醒主线程的公开入口是 `Window::surface().request_wake()`，而架构 §3.2 原写的 `post_to_main` 实测只存在于 `aurora::detail` 命名空间；④ 框架 `Scroll` 容器不能表达终端视口（无公开视口高度 getter、无惯性、整屏 blit 与「行级脏 + 后台权威网格」正交），§9.5 的待定项就此必须拍板。四处一律以裁决与实测口径回写，不留到代码里各凭理解。

- **新增裁决 7.23**（四条 + 三处实测口径）：① 视口与 scrollback **自管**，用 `Widget` 内置的 `ScrollViewport` 内核，通用件按「去掉终端字样后仍成立」的判据经附录 A.3 反哺 Aurora；② run 切分粒度＝**按样式全等合并到行**，且色带矩形与文本片段共用同一批切分边界（双宽延续格是空串，不产片段也不画豆腐块）；③ **渲染吞吐基准挂时间门禁**（`SPEC.NF.PERF.02` 原文维持，劣化 >10% 即 FAIL），并写明它与裁决 7.22③ 的改判不冲突——两处锁的成本主体不同；④ **第一棒＝`SPEC.FEAT.RENDER.01` + `SPEC.FEAT.RENDER.03` + `SPEC.FEAT.RENDER.04` 主路径**，`RENDER.02` 字体族与缩放重建随第二棒。实测口径三条记在 ② 之后：局部帧裁剪盒读取方式、combining 随同 run 文本并字（框架 shaping 负责叠字，`Painter` 无逐字形入口）、唤醒只走公共 `request_wake` 不依赖 `detail` 符号。
- **文档连带回写**（本批无代码改动）：`ARCHITECTURE.md` §9.2 补「取用形态细则」（px↔dp 换算式、一个参考 `Font` 取一次度量的纪律、一帧的层叠顺序、`src/ui/terminal_view.cpp` 作为全仓唯一触达 `au::Painter` / `au::Widget` 的翻译单元、`cell_layout` 与 `palette` 保持无框架依赖以便全量单测）；§9.5 由**待定**改为拍板形态并写明反哺通道；§16 移除 B 行（该表要求「每项拍板后写入正文并从本表移除」）；§3.2 的唤醒口径按实测改写。`PLAN.md` §2 新增「M1 渲染腿的棒内序」段，§3 的 `SPEC.FEAT.RENDER.01–05` 五行标注棒次。
- **验收映射**（随第一棒落地，本批只定判据形态）：脏行重绘用 `HeadlessSurface` 两帧全像素差分，断言差异行集 ⊆ 队列提交行集；属性合成走 `palette` 纯函数单测加一条像素用例；光标的三形态与闪烁按像素位置断言，闪烁由框架既有的 `Widget::tick(now)` 驱动而非新起线程；双宽与 combining 复用 `tests/fixtures/` 回放夹具并把「列位与光标一致」从逻辑层延伸到像素层。
- **代价与边界**：① 自管视口意味着惯性滚动与滑动手感要应用侧自写，换来的是行级脏与后台权威网格两条前提不被容器截断；② 「色带与片段共用切分边界」在颜色频繁交替的行里会把 run 数推到格数量级，届时优化面在切分而非框架原语；③ 吞吐门禁的基线由首批实测固定，跨机性能差异须以同机回归为准（门禁锁相对劣化，不锁绝对值）；④ `RENDER.02` 后移使第一棒的汉字显示依赖系统等宽字体的缺字回退，若目标机上取不到 CJK 面则表现为豆腐块——这正是它单列第二棒的原因。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次为「待定项拍板 + 实测口径回写」，`SPEC.FEAT.RENDER.01–04` 的语义未改，只补执行形态。

---

## v0.18（2026-10-01）G1 绘制两腿在框架侧闭合：批量文本片段与等宽整格度量（裁决 7.22）

**动机**：附录 A.2 G1 列的三条腿里，宽度判定腿已随裁决 7.20 闭合，剩下两条都卡在同一处——`Painter::draw_text` 的三个重载全是**字符串粒度**，终端一帧要按样式切成几十到几百个片段，只能逐段调用；而等宽网格需要的「第 k 列/行的整像素起点」框架只给浮点自然度量，应用侧自行折算就落在非整数 scale 上逐格各自取整，列位随列号漂移。按裁决 7.13 类别 ①（影响渲染链路与公共 API 形态的缺口）本仓不等不绕，两腿须在 Aurora 侧以公共 API 落地。落地过程定死四条形态与判据选择，其中「验收判据」一条被实测结果推翻，故回写成裁决 7.22 而不是照原承诺交差。

- **新增裁决 7.22**（四条）：① 批量原语落**在 run 层**（片段 = 同属性文本 + 区域 + `Font` + `Color`），不暴露 `ShapedGlyph`、不含 cell / 终端语义，网格模型与脏行 diff 仍在本仓；② 整格度量**以函数形态进框架、排布留应用侧**，刻意**不给 `TextLayoutOpts` 增设「整格模式」**——那会把终端排版语义塞进通用文本层；③ **验收判据由时间门禁改判为逐位一致**（实测收益落在环境抖动内，无稳定阈值可锁红灯，见下）；④ 本仓**不因本批改动任何代码**，两原语随 `SPEC.FEAT.RENDER.01` 上屏层落地时直接消费，架构 §9.2 的「应用侧自绘 cell 网格」过渡备路径就此不再启用。
- **框架侧（跨仓，Aurora 分支 `dev-1.0.0-alpha.9.uat.2` 提交 `ec71a3aa` 功能、`182fb11e` 测试、`ad8142d9` 基准、`64afbfa4` 文档，均在本地未推送）**：新增 `Painter::draw_text_runs(std::span<const render::TextRun>)` 与 `render::FontEngine::monospace_cell(Font, scale) -> render::CellMetrics{cell_width_px, cell_height_px, ascent_px}`，契约文档写进 Aurora 主仓 `codespec/specification/03-layout-render.md` §8.1 与 §8.2，`CHANGELOG.md` 记入 alpha.9 的 Added。取整口径与实绘同源：格宽取参考字形集 `{'0', U+2500}` 的整像素 hinted advance 最大值（含制表符是因为它在部分字体里比数字宽，只量数字会让边框压进相邻格），行高与基线取绘制侧同一次 `line_height_px` / `ascender_px` 的 snap 值。批量化省的是每次调用的字体面解析（`family#weight` 堆键）、像素尺寸与行高度量（各含一次 `FT_Set_Pixel_Sizes`），落笔算式一字未动，故输出与逐片段调用**逐位相同**；录制态逐片段各落一条 `DrawText` 命令，三个 RHI 回放后端与 `CmdKind` 零改动。
- **测试**（均在 Aurora 侧）：`tests/unit/utest_painter.cpp` 新增 3 例，判据是「整批一次」与「逐片段各一次」两张画布的**全像素差分为 0**（其中一例第 3 片段换字重，覆盖相邻片段 `Font` 变化时重建派生上下文的分支），另覆盖空数组无操作；`tests/unit/utest_font_engine.cpp` 新增 4 例（三值恒正且随字号单调、ascent/height 与绘制侧落笔基线逐位一致、格宽不小于参考字形集内每个字形的整像素 advance、按 `x = col * cell_width_px` 摆放 `U+2500` 串时最左墨迹列零漂移）。`tools/bench/bench_render.cpp` 新增一屏规模场景（24 行 × 12 同属性短片段 = 288 片段，9 组「逻辑尺寸 × scale」两轮）作观测。Aurora 全量 `ctest` 333/333 绿（含 17 道 `check_*` 门禁：核心层边界、API 预算与 schema 同步、代码 / 文档同步、字面量语言、命名与版本一致性等），`format-check` 892 文件全合规，`lint` 537 TU 仅剩 4 条本批未触碰文件的存量 findings（`include/aurora/widget/provider.h`、`src/aurora/inspector/inspector_server.cpp`、`tests/unit/utest_inspector_server.cpp`，非本批引入）。
- **代价与边界**：① **原承诺的吞吐门禁未兑现**——附录 A.3 反哺清单原文要求「网格吞吐基准挂性能回归门禁（劣化 >10% 即 FAIL）」，实测批量入口相对逐片段为中位约 −1%、区间 −8.5%…+8%，因该场景成本主体是字形 blit 与图集查找而非被省下的固定开销，无稳定阈值可锁，故基准两行降级为观测项、改由像素级用例守门（此即裁决 7.22③）；② `FontEngine::draw_text_runs` 的片段原点收**逻辑 dp**，与该引擎其余绘制入口收预缩放物理像素的约定刻意不同，理由与代价写在 Aurora 头文件与 §8.2，本仓消费时**不要再自行乘 scale**；③ 本批只闭合绘制两腿，G1 的宽度判定腿虽已闭合，零宽标记**仍只存不绘**，combining 上屏须随 `SPEC.FEAT.RENDER.01` 取 `grid::Row` 侧表合成字形；④ 字重切换带来的 1px advance 差要求同一网格固定用一个参考 `Font` 取一次度量，这属应用侧排布纪律，框架不代持。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是「框架缺口闭合 + 原验收判据实测改判」，`SPEC.FEAT.RENDER.01` 与 `SPEC.FEAT.RENDER.05` 的语义未改，只把附录 A.1 两行、A.2 G1 行改写为已关闭，并把架构 §9.2 从过渡备路径改述为「就绪后的取用形态」。

---

## v0.17（2026-09-30）OSC 由整体吞掉改为结构化消费，`OSC 52` 写方向真落系统剪贴板（裁决 7.21）

**动机**：`SPEC.FEAT.TERM.07` 的消费腿此前是状态机里的一句 `TODO(SPEC.FEAT.TERM.07)`——OSC 串按协议合法地不进气，但既不产生产物也不留痕迹，「标题没生效」这类排障无从下手。本批把 `0/2` 标题、`7` 工作目录、`8` 超链接、`52` 剪贴板、`133` 命令块一并改成消费成结构化产物；其中 `7` / `133` 本属 `SPEC.FEAT.INTEG.01/02`（P2/M4），按用户裁决只作**来源预埋**，`52` 按裁决真写系统剪贴板，并把整条腿在 PLAN.md 标为提前落地。形态上的关键约束来自架构 §3.4：状态机跑在会话读线程的网格锁内，锁内只能留存不能投递，而剪贴板是主线程的 IO。

- **新增裁决 7.21**（五条）：① 产物形态是**状态快照 + 一个取走型动作**而非事件队列；② 超链接不进 `Cell` 主结构，走 `grid::Row` 侧表 + 有界链接表（`kMaxHyperlinks = 1024`，标识递增且永不复用，满表淘汰最旧，后果限定为「不可点」而非指向另一条 URL）；③ `OSC 52` 写方向按「锁内留存 → `Session::take_clipboard_write()` 合并取走 → 主线程 `ClipboardOutbox::drain()` 转调 Aurora `Clipboard::set_text`」落地，读方向 `52;c;?` 默认禁止但**回写空响应**；④ 提前落地范围与其「来源预埋」边界；⑤ 未消费命令号计入 `unhandled_count`，不得整体吞掉不留痕。
- **本仓代码落地**：`include/borealis/term/osc.h`（`OscState`、`PromptMarker`、`kMaxHyperlinks`）、`src/term/osc.{h,cpp}`（OSC 字段切分与**严格** base64 解码——字母表外字符、截断、填充后载荷一律判非法，解出的字节再走一遍 `Utf8Decoder`）、`grid::Row` 的「列号 → `HyperlinkId`」侧表（`set` 即清链、`reset`/截断/整行搬移随行走）、`Terminal::do_osc` 的命令号派发与 `osc_state()` / `hyperlink_target()` / `take_clipboard_write()`、`Session` 的三处加锁访问器、`include/borealis/session/clipboard_outbox.h` + `src/session/clipboard_outbox.cpp`（全仓唯一触达 `aurora::Clipboard` 的翻译单元，公共头不含框架头）。`TODO(SPEC.FEAT.TERM.07)` 就此消除；`TODO(SPEC.FEAT.INTEG.01/02)`、`TODO(SPEC.FEAT.CONN.12)` 留在各自消费点。
- **测试**：新增 `tests/unit/utest_terminal_osc.cpp`（19 例：`0/1/2` 标题、`7` 原文留存、`8` 区间括出与 params 段切分、覆盖/擦除清链、SGR 不截断区间、双宽只挂前半格、有界表淘汰后旧标识解析不出而历史格仍挂该标识、`52` 解码/合并/空载荷合法/三类非法载荷、`52;c;?` 的空应答、`133` 各边界与退出码、未识别命令号计数后链路照常、RIS 清零全部产物）；`tests/unit/utest_grid_storage.cpp` 补侧表 5 例；`tests/unit/utest_session.cpp` 补 4 例（含「字节 → 解码 → 解析 → 状态机 → 网格侧表 → 会话取值」的整链断言与多次写的合并语义）；`tests/integration/itest_terminal_scene.cpp` 的 OSC 断言从「标题串被吞」改为「不进网格且消费成状态」；新增 `tests/e2e/etest_osc_clipboard.cpp` 用真机 ConPTY 让 powershell 发出 `OSC 52`，回读系统剪贴板校验明文（断言素材是 base64，明文不经屏幕），并校验「取走即清空」与用例结束归还用户既有剪贴板内容——须在有窗口站/桌面的交互会话投放（裁决 7.19⑤）。CTest 共 15 项全绿（MSVC + Ninja）。
- **代价与边界**：① 快照只留**最近一次**，`133` 的完整命令块区间（起止附着到网格行）须由 `SPEC.FEAT.INTEG.01` 落地时补结构，`7` 的目录继承与远端降级语义同归 `SPEC.FEAT.INTEG.02`；② 剪贴板只取最终值，「多次写各自生效」的追加型用法不在本档覆盖内；③ `OSC 52` 的三态授权（`SPEC.FEAT.CONN.12`）与 `OSC 4/10/110` 调色板族仍只计数不消费；④ 主线程 `drain()` 的调用点随帧调度接线（`TODO(SPEC.FEAT.RENDER.01)`），当前由消费方按帧轮询；⑤ 剪贴板是用户共享状态，e2e 用例先读回、结束原样归还。
- 需求条目数量（66 条）、标识体系与优先级/阶段的**结构**均未变动；本次按裁决 7.21④ 只在 PLAN.md §3 的「阶段」列加子句级提前落地标注（`TERM.07`、`CONN.12`、`INTEG.01/02` 四行），并在 §8 现状表新增「OSC 消费 → 会话产物」一行。

---

## v0.16（2026-09-30）码点宽度判定由框架交付，双宽占位与 combining 一并接线（裁决 7.20）

**动机**：`SPEC.FEAT.TERM.08` 的宽度腿此前只到「机制可用」——状态机经注入接缝取格数，但生产侧挂的是「一律单宽」的缺省实现，CJK 双宽占位不生效，combining 更是无处可去。按裁决 7.13 类别 ①（影响渲染链路的框架缺口）**本仓不等不绕**，判定表须先在 Aurora 侧以公共 API 落地；而落地时实测出一个规格书没写到的分叉：Ambiguous 区间**包含**组合区段（U+0300–U+036F 一类），于是「先判宽度还是先判零宽」成了会决定正确性的选择，连同零宽码点在网格里怎么存一起回写成裁决 7.20。

- **框架侧（跨仓，Aurora 分支 `dev-1.0.0-alpha.9.uat.2` 提交 `9efe3968`，未推送）**：新增 `aurora/core/unicode_width.h` 的 `unicode_cell_width(char32_t, AmbiguousWidthMode) -> 0 / 1 / 2`，一个原语同时给出零宽（General_Category Mn/Me/Cf）与 East Asian Width（W/F 双宽）两判据，内部固定「先零宽、再宽度」的次序，Ambiguous 口径以入参给出（裁决 7.15 的要求）。数据由 UCD 18.0.0 实测导出为零宽 379 段 / Wide+Fullwidth 126 段 / Ambiguous 179 段的 constexpr 区间表，**不把上游数据文件复制进仓**，许可声明记入 Aurora `THIRD_PARTY_LICENSES.md` 第 9 节，能力面写入 `codespec/specification/01-core.md` §7.3，单测 `tests/unit/utest_unicode_width.cpp`（含全码点扫描：结果恒 ≤2 且 Wide 口径不小于 Narrow 口径）。
- **本仓代码落地**：`term::UnicodeWidthPolicy`（声明在 `include/borealis/term/width.h`、实现在 `src/term/width.cpp`，因本仓公共头不含 Aurora 头）替换生产侧的单宽缺省；`grid::Row` 新增「基础格列号 → 零宽标记序列」侧表（`CombiningMark`、`attach_combining` / `combining` / `clear_combining`、单格上限 `kMaxCombiningMarksPerCell = 8`），落实架构 §4.1 早就写明却无载体的低频属性外置；状态机 `do_print` 在宽度为 0 时不写格、不推进光标、不触发换行，把码点并入光标左侧最近的基础格（跳过双宽延续格挂到前半格）。`SingleWidthPolicy` 退为测试用常数注入值。
- **测试**：新增 `tests/unit/utest_width_policy.cpp`（口径类断言：W/F 恒双宽、Ambiguous 随口径 1/2、零宽先于宽度、中性单宽）与 `tests/integration/itest_unicode_width.cpp`（全链路字节流跑 `SPEC.FEAT.TERM.08` 的验收线：CJK 双宽占位与延续格、**同一份**含 Ambiguous 的输出编一次字节喂两台终端、行末放不下的双宽字符整体换行不留半格、combining 并入/不推进光标/落双宽前半格/行首丢弃/行末不触发换行/覆盖即清除）；`tests/unit/utest_grid_storage.cpp` 补侧表六例（并入序、脏标记、只清本列、上限、截断与复位、随整行搬移）；`tests/integration/itest_terminal_scene.cpp` 改挂 `UnicodeWidthPolicy` 并把「单宽缺省」的旧注释改写为映射前判定口径的说明；字节→状态机的接线从场景用例下沉到 `tests/support/terminal_feed.h` 以免两处漂移。CTest 共 13 项全绿（MSVC + Ninja）。
- **代价与边界**：① 判定表版本随框架升级，本仓不持表也不复制数据，故本仓宽度用例只断言「口径」不断言具体码点归属，UCD 变更由 Aurora 侧承担；② 零宽标记**只存不绘**——上屏层落地（`SPEC.FEAT.RENDER.01`）时须按 §4.1 取侧表合成字形，否则 combining 在屏幕上仍不可见；③ 上限 8 之外的标记静默丢弃，与「不可信输入须有界」相比损失的是极端字素簇的保真度；④ 字素簇切分与 Emoji 呈现仍不属本条（`SPEC.FEAT.TERM.08` 的延后观察项）。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次为「框架缺口闭合 + 需求空洞补裁决」，`SPEC.FEAT.TERM.08` 的语义未改，附录 A.2 G1 的「影响需求」列补登该条（此前是推断，现为实测）。

---

## v0.15（2026-09-30）本地终端连接在 Win32 侧落地，会话首次由真实 PTY 驱动（裁决 7.19）

**动机**：会话层之上六层纯逻辑都由测试替身驱动，`SPEC.FEAT.CONN.01` 的 Windows 腿（ConPTY）是 M1 出口判据「替换 Windows Terminal」的唯一硬前置。落地过程撞到四处 API 语义空白——宿主标准句柄的传递、伪终端管道端的归属、`ClosePseudoConsole` 与子进程的关系、环境块的合成与排序——任一处漏掉都表现为「进程在跑但屏幕空白」或「关标签卡死」，且都不在需求与架构文档里，故先实现后实测再回写成裁决。

- **新增裁决 7.19**：①②③④ 即上述四处口径（①`STARTF_USESTDHANDLES` + 三句柄置空；②交给伪终端的两端在子进程建好后关掉本进程那份，否则输出管道永不 EOF、读线程回收不了；③关标签即补 `TerminateProcess`；④环境按「继承 → PTY 默认 → profile 覆盖」合成并排序），另附两条实测事实（宽字符环境块必须带 `CREATE_UNICODE_ENVIRONMENT`，否则任何非空 `lpEnvironment` 都返回 87；`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 的 `lpValue` 传 HPCON 本身而非其地址，传地址子进程以 `0xC0000142` 失败），并给出 ⑤「e2e 用例只能在能访问窗口站/桌面的交互会话中运行」这条运行前提。
- **代码落地**（全部在 `src/platform/win/`，Win32 类型不外泄，裁决 7.11）：`conn::LocalTerminalSpec` 与 `make_local_terminal_connection` 工厂（`include/borealis/conn/local_terminal.h`，本仓第一个 `conn` 域公共头）、`platform::ConptyConnection`（`CreatePseudoConsole` + 属性表挂子进程 + 单读线程 + 有界关停）、`default_shell_command_line()` 的 Windows 探测链（PowerShell → cmd → WSL，对应 `SPEC.FEAT.CONN.01`）、UTF-8 ↔ UTF-16 转换工具 `win_text`；`src/CMakeLists.txt` 以 `if (WIN32)` 增补平台源。
- **测试**：新增 `tests/e2e/etest_local_terminal.cpp`（本仓首个 e2e，8 个用例）以真实 ConPTY 驱动全链路，断言落在权威网格而非原始字节：自定义命令输出上屏、`TERM`/`COLORTERM` 注入与 profile 覆盖、启动目录、键入往返 + 尺寸变更后流不中断、3000 行突发下队列水位不越界且末行可达、关闭后进程不在且内容保留（`SPEC.FEAT.CONN.01`、`SPEC.FEAT.XFER.01` 会话启动腿、`SPEC.NF.PERF.06` 真机腿）。CTest 共 11 项全绿。
- **代价与边界**：③ 的「关标签即终结」意味着后台跑长任务再关标签会丢进程，与 Windows Terminal 同口径但须随 `SPEC.FEAT.WS.01` 的关闭前确认一起在 UI 侧提示；⑤ 使本仓 e2e 无法在无控制台的受限环境里跑（该环境下的失败不是实现有误）；默认 shell 探测链、POSIX 侧的 `$SHELL` 腿与 SSH 族连接仍未开工。真机走查「替换 Windows Terminal 日常使用」与 `vim`/`tmux` 的 TUI 验收仍待渲染层。

---

## v0.14（2026-09-30）会话抽象粒度拍板，输出背压与查询回写落口径（裁决 7.18）

**动机**：状态机之后要接「会话回写」这一棒，撞上文档里三处空白——架构 §16 A 的会话抽象粒度仍列三案待拍板、`SPEC.NF.PERF.06` 只说「合并而非丢弃」却没说队列条目是什么粒度、`SPEC.FEAT.TERM.01` 要求 DA1/DSR「正确响应」却没定应答该报哪些能力号。三者都会决定公共接口形态与可观测行为，不该由实现自行择一，故先由用户裁决粒度、再落代码。

- **新增裁决 7.18**：① 会话接口取**基础接口 + 能力接口组合**（`Connection` 五方法 + `ConnectionEvents` 反向通道，SSH 独有面到落期以独立能力接口增补）；② 背压队列的条目是**视口行区间提交**而非字节/cell 值，队列满按**并宽区间**做到合并而非丢弃，且**行脏标记由消费侧清除**（写侧只登记，否则列级增量取不到、`SPEC.FEAT.RENDER.01` 的单帧只重绘变更单元格落空）；③ 查询应答**只报能力档位 62**，不报 132 列 / sixel / ReGIS / 打印机附加能力号，DA2 与 `CSI ? 6 n` 按「宁可不答也不答一份错格式」忽略。
- **架构文档连带回写**：§7.2 由「待定」改为拍板形态并把 §16 的 A 行移除（该表要求「每项拍板后写入正文并从本表移除」）；§3.4 补会话层持锁口径（解码 + `feed` 同处临界区、出锁后才 IO 与入队、应答锁内登记锁外回写）；§3.6 补提交粒度、合并规则、锁序「先队列后网格」与默认预算；§5.2 补 DSR/DA1 的应答取值与不报附加能力号的理由。
- **代码落地**：`include/borealis/session/{connection,damage_queue,session}.h` 与 `src/session/{damage_queue,session}.cpp`（编入 `borealis_core`）、`term::ResponseSink` 接缝与 `Terminal::set_response_sink`、`Session::resize` 串通存储层与连接侧下发；`TODO(SPEC.FEAT.TERM.01)` 的两处查询响应占位据此消除，OSC 消费仍留 `TODO(SPEC.FEAT.TERM.07)`，帧调度通知留 `TODO(SPEC.FEAT.RENDER.01)`（平台层 ConPTY 与上屏层是下一棒）。
- 需求条目数量（66 条）与标识体系未变动；本次为「空洞补裁决 + 待定项拍板」，不改任何既有需求语义。真机验收（`vim`/`tmux` 的 DA1/DSR 往返、`yes` 类无限输出的背压表现）须待 ConPTY 落地，当前由 `Connection` 测试替身在单测中驱动。

---

## v0.13（2026-09-30）窗口行数变化口径落定（裁决 7.17）

**动机**：终端状态机落地后要给 PTY 尺寸同步（`SPEC.FEAT.XFER.01`）接线，才发现规格书只裁决了**列宽**变化不 reflow（7.5），而「视口行数增减时已有行与 scrollback 历史怎么处理」在文档里是空的，代码侧只留 `TODO(SPEC.FEAT.XFER.01)`。三种取向（从历史收回 / 直接丢弃 / 固定总容量）对「能回滚多少行」的承诺差别很大，不该由实现自行择一，故先落裁决再落代码。

- **新增裁决 7.17**：行数变化按**移动窗口边界、底部锚定**处理——变高从 scrollback 顶部收回历史填满，变矮时顶行溢出进历史，超出容量从最旧端丢弃，历史不足时顶部补空白；并明确 **scrollback 的配置容量不随视口变高而缩水**，代价（变高必然扩容重分配、须靠 XFER.01 去抖压频）写入该条。
- **`SPEC.FEAT.TERM.04` 就地补指针**：原句只指向裁决 7.5（列宽），现同时指向 7.17，免得「默认 10,000 行」被读成随窗口高度浮动的值。需求语义未变，仅补交叉引用。
- **`ARCHITECTURE.md` §4.5 连带**：补行数变化的窗口边界语义、环拉直的原因（容量模数一变，跨旧边界的行号会错位），并点明尺寸变化属整屏位移级事件、一律以整屏脏通知副本重建（§3.4），以及终端模式随尺寸收敛的三条（DECSTBM 带复位、制表位按新列宽重建、光标与 DECSC 保存位置一并钳制）。
- **§7 标题的追加区间**改为 7.16–7.17。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次为需求空洞补裁决，不改任何既有需求语义。

---

## v0.12（2026-09-30）审查整改：引用形态、口径矛盾与架构落点补全

**动机**：审查报告剩余项全量整改。本批**不改动任何需求语义**，优先级与阶段主体不变（PLAN.md 仅做子句级拆分标注），内容为引用形态修正、文档间口径矛盾消除与已排期需求的架构落点补全。

- **引用形态与事实口径修正**：§3.3 第 4 条「Aurora 规则 §5.8」改为「Aurora 主仓 `AGENTS.md` §5 规则 8」（该规则实测存在——「禁止直接触达标准输出」，原引用未指明文档名且 `§5.8` 形态易误读为不存在的子章节号）；`SPEC.NF.A11Y.01` 与 `PLAN.md` 观察池对 Aurora `ACCESSIBILITY_DESIGN` 草案的引用改为已实测存在的 Aurora `codespec/ARCHITECTURE.md` §8.5（Aurora 自身规则禁引 draft 文档）；附录 A.2 G2 的「五后端」补明确口径（win32 / x11 / wayland / glfw / macos，headless 经同一 dispatcher 路径用于单测）；§4.1 `SPEC.FEAT.TERM.06`「less/less」笔误改「less/more」；§7 标题的裁决基线日期按 CHANGELOG 各版本实际落定日期拆分（7.1–7.4 于 2026-09-22、7.5–7.10 于 2026-09-23）。
- **`ARCHITECTURE.draft.md` 口径矛盾消除**：§1.3 命名空间清单补 `borealis::platform`（与 §2.1 一致，标注纯实现层无公共头）；§4.4 删除「alt screen 独立 scrollback」的自相矛盾表述，改为「备屏不与主屏 scrollback 混排、备屏本身无回滚（xterm 惯例，滚轮由 alternate scroll 承接）」。
- **架构落点补全**（此前一批已排期需求在架构文档无归属）：§3.1 补单实例转交通道指针；§3.4 补「主线程读取 cell 内容方式未定」指针；§9.2 过渡自绘标注适用条件（M0 完成前应用侧不开工，本节为 G1 延期时的预研备路径）；新增 §7.5「SSH 之上的子系统归属」（SFTP / 隧道 / 公钥推送 / 档案 / 凭据 / 连接管理器 UI / 会话日志的模块归属；系统通知依赖 G11 属观察池）与 §8.4「其余工作区能力归属」（多窗口 / 全屏 / pane 缩放 / 广播输入）；§10.3 搜索补持锁口径指针；新增 §10.6「无障碍（延后项的落点约束）」（自绘网格的语义树钩子须应用侧实现）；§7.2 会话抽象增列第三案「基础接口 + 能力接口组合」；§16 待定清单增列 D（网格所有权与主线程读取方式，三案：持锁遍历 / 每帧快照 / 双副本）、E（单实例转交通道：命名管道 / 本地 socket / `WM_COPYDATA`）、F（schema 版本升级策略），A 案候选补第三案。
- **`PLAN.md` 分期标注修正**（子句拆分，优先级与阶段主体不变）：`SPEC.FEAT.WS.05` 原整条落 M2，但其「SSH 断线自动重连」子句依赖 M3 的 SSH 本体（M3 出口判据本就含「重连」）→ 标注为「M2（本地终端腿）+ M3（SSH 断线重连腿）」，M3 涉及需求同步补该腿；`SPEC.FEAT.INTERACT.03`（P0 / M1，含「选区复制」）依赖文本选择基本面，而 `SPEC.FEAT.INTERACT.02` 原整条落 M2 → 流式拖拽 / 矩形块基本面（不依赖框架缺口 G2，见 `ARCHITECTURE.draft.md` §9.6）前移 M1，双击 / 三击等其余仍留 M2（拆分模式沿用 `SPEC.NF.PLAT.01`「M1（Windows）+ M2（Linux）」先例）。
- **仓库卫生**：`.gitignore` 补 `/.workbuddy/`（AI 协作本地记录目录，此前 untracked 且未被忽略）。
- 需求条目数量（66 条）、标识体系、优先级与阶段主体均未变动；本批为引用 / 口径 / 落点修正，不涉及需求语义。

---

## v0.11（2026-09-30）中文输入与显示链路补全（裁决 7.16）

**动机**：规格书审查发现中文场景有三处需求空洞——此前只覆盖「显示宽度」（`SPEC.FEAT.TERM.08`），漏掉「输入」与「字形可得性」两个维度；而首要用户为中文开发者、串口默认编码为 GB18030（裁决 7.6），三者均属日常刚需而非增量特性。框架侧能力经实测均已具备，故本次为**需求侧补全，不新增 G 编号**。

- **新增 `SPEC.FEAT.INTERACT.06` 输入法（IME）**：preedit 就地渲染于光标单元格、候选窗按光标屏幕坐标定位、组合中间态不发往会话、commit 文本经会话编码写入。按 §1.4 第 1 条取 INTERACT 域最大序号 + 1（原为 05）。连带回填 §2.1 能力域表（终端交互改 `01–06` 并补「输入法」）与 §4.3 小节标题；附录 A.1 的 IME 行此前是唯一「箭头后无需求标识」的行（因无处可指），现指向本条。
- **`SPEC.FEAT.RENDER.02` 补 CJK 缺字回退链**：内置 Cascadia Code **不含汉字字形**，无回退则 `SPEC.FEAT.TERM.09` 的「GB18030 串口输出正确显示」验收必然呈现豆腐块。补默认回退至系统等宽 CJK 字体、回退链顺序可配、回退后仍按双宽占位而不破坏网格对齐，并补验收判据；§2.1 网格渲染行同步补「CJK 缺字回退」。
- **`SPEC.FEAT.TERM.09` 编码口径由单向改双向**：原文与 §1.3 术语表均只定义解码方向（字节流 → 文本），但 `SPEC.FEAT.CONN.05` 串口（默认 GB18030）需输入中文并发送行尾序列，用户输入必须**反向编码**为会话编码字节，否则设备侧收到乱码。现补发送方向（覆盖键入、IME commit、粘贴、片段发送）与「目标编码不可表示字符」的可配策略（默认替换 + 一次性提示，不静默发乱码），§1.3 术语表同步改为双向定义，验收判据补「设备侧收到合法 GB18030 序列」。
- **新增裁决 7.16** 记录上述三项的理由与代价；§7 标题的追加区间改为 7.11–7.16。
- **`PLAN.md` 连带**：`SPEC.FEAT.INTERACT.06` 赋 **P0 / M1**（首个交付阶段在本地终端输入中文即触发，不可延后），M1 出口判据补「中文可输入可显示」；M4 出口判据补「中文按 GB18030 编码发送」；§3 总表增行，计数由 65 条改为 **66 条**、P0 由 22 条改为 **23 条**。`SPEC.FEAT.RENDER.02` 与 `SPEC.FEAT.TERM.09` 的既有优先级与阶段不变（本次仅为其增补子句）。
- **`ARCHITECTURE.draft.md` 连带**：§3.5 输入路径补「文本 → 会话编码」环节与输入法路径；§6.1 补双向口径；§6.2 由「解码失败处理」改为「转换失败处理」并补编码方向策略；新增 §10.5 输入法（点明自绘网格下框架不会自动把 cell 内容暴露给输入法，且坐标换算须与 DPI 口径一致）；§11.1 补 CJK 缺字回退链。

---

## v0.10（2026-09-29）修正对框架 `TabBar` 能力的高估，映射表名称笔误

- **附录 A.1 新增「标签栏」行**：`TabBar`（`widget/tab_bar.h`）经实测只有 `on_change` / `on_close` 两个事件，属性为选中序号、栏高、四个**全局**配色、指示器粗细、字号与内边距；**无拖拽重排、无逐标签图标、无逐标签状态角标**。此前 A.1 未收录该控件，导致 `ARCHITECTURE.draft.md` §8.1 写下「框架 `tab_bar` 可直接承载标签栏」这一未经实测的乐观结论——与 `SPEC.FEAT.WS.02` 对 `Splitter` 的告警（不得因框架已有控件而低估工作量）属同一类错误，故按同一口径补录能力边界。
- **连带修正 `ARCHITECTURE.draft.md` §8.1**：改为「`TabBar` 仅覆盖选中切换与关闭」，并点明 `SPEC.FEAT.WS.01` 的拖拽重排与图标、`SPEC.FEAT.WS.04` 的逐标签状态角标须应用侧自研。
- **映射表 `#44` 名称修正**：原写「近乎零成本」，应为「命令面板」（`SPEC.FEAT.WS.07`）。已用 v0.6 提交原文核对——其 §2.1 工作区行为 `#18–#23 #44–#49`，对应 `SPEC.FEAT.WS.01–12`，故 `#44` 确为 WS.07；「近乎零成本」是附录 A.1 中描述 WS.07 复用成本的措辞串入了名称列。「历史条目原文照录」原则约束的是叙述性版本条目，映射表为 v0.7 生成的工具表，属笔误可修。
- 需求条目数量（65 条）、标识体系、裁决结论与分期结构均未变动；本次为事实修正与笔误修正，不涉及需求语义。

---

## v0.9（2026-09-29）工作区目录改名落地，命名例外条款删除

- 由人在 IDE 会话外把工作区目录改名为 `borealis`，裁决 7.14 设定的「目录名为唯一例外，改名后须同步回填」条件就此达成，按该条自身要求删除例外表述。
- **裁决 7.14 就地修订**：编号保留（撤销会让其他处的 `7.14` 引用变成伪死链），正文改为「目录与文档自称均不再出现旧名」并说明例外条款已失效及原因。
- **裁决 7.1 括注收回**：7.1 曾以括注形式指向 7.14 的例外，现只述产品名本身。
- **旧名指涉清零**：本文开头括注、7.1、7.14 与 `AGENTS.md` §1 命名行的四处已清理；全仓（含 `PLAN.md`）grep 确认无残留。
- 需求条目数量（65 条）、标识体系与分期结构均未变动；本次为纯文档口径回填，不涉及需求语义。

---

## v0.8（2026-09-29）Ambiguous 宽度默认值落定

- **新增裁决 7.15**：East Asian Width 的 Ambiguous 类字符**默认按单宽（窄）**，并提供 **profile 级覆盖为双宽**。理由与场景分工见该条——UTF-8 本机/SSH 场景取单宽与主流终端一致，GB18030/GBK 串口等少数场景以覆盖项解决，故不把默认值翻到对多数场景错误的一侧。
- **SPEC.FEAT.TERM.08 就地修订**：写入上述默认值与覆盖粒度（覆盖项与 SPEC.FEAT.TERM.09 的会话编码项协同），并补出验收判据（两种配置下光标列位与占位一致、不出现半格错位）。
- **附录 A.2 G1 连带约束**：框架宽度判定原语的**入参须含 Ambiguous 宽度模式**，不得在框架内硬编码单/双宽——否则应用侧只能绕开公共 API 自行查表，违反「渲染路径不私改」的约束。
- 标识体系、需求条目数量（65 条）与分期结构均未变动；本次为需求语义增量。

---

## v0.7（2026-09-29）需求标识改造与「纯需求」化

**动机**：两项结构性指令——① 规格书应为纯粹的需求描述，不得承载优先级（`P0`…）与任务分期（`M1`…），计划类内容另立文档；② 不得用纯数字作需求标识，因为插入新需求会导致整体编号偏移，交叉引用一改就是百余处。

**改动**：

- **标识体系落地**：新增 §1.4 需求标识规范，格式 `SPEC.<类别>.<域>.<NN>`（类别 `FEAT`/`NF`；FEAT 域 `TERM` `RENDER` `INTERACT` `XFER` `WS` `CONN` `PREF` `INTEG`，NF 域 `PERF` `PLAT` `RELI` `PKG` `A11Y`），并附 5 条稳定性规则：只追加不复用不重排、改域即换号且映射入本文件、引用一律写全、同域连续可写区间、更名不改号。
- **全量重编号**：65 条需求（`SPEC.FEAT.*` 55 条 + `SPEC.NF.*` 10 条）由 `#1`–`#65` 改为其域内语义标识，映射见下方「旧需求编号 → 新标识」表。重编号以脚本机械执行并对每条替换断言，正文与附录的交叉引用零残留（含 §2.1 覆盖表、§7 裁决、附录 A.1/A.2/A.3）。
- **优先级与分期移出正文**：新建 [`PLAN.md`](PLAN.md)，承接优先级定义、阶段划分与出口判据、需求优先级总表、延后子项清册、观察池、框架缺口补入排期、跨平台分期决策。
- **正文去标记**：约 30 处内联 `P0`–`P3`/`M0`–`M5` 措辞改写为语义表述（「延后子项」「延后观察项」「分期见 PLAN.md」），需求条目只保留「本项边界」不含「落期」；§6 由里程碑表改为指向 PLAN.md 的指针，章节号序列保持不变以免 §6 之后的锚点漂移。
- **裁决连带调整**：7.10 重定为「工作区下限」的需求侧表述；7.11 只保留「平台层自始接口隔离」这一需求约束，交付分期移入 PLAN.md §7；7.12 措辞去里程碑化。
- **表格压缩**：§2.1 覆盖表与 §4 各小节标题改用区间写法（如 `SPEC.FEAT.TERM.01–08`），并标注非功能需求共 10 条不入能力域表。
- **尾部收敛**：原「## 版本」小节整体迁入本文件，规格书尾部只留当前版本号与指向本文件的指针。

**影响**：纯结构性迁移，**无任何需求语义变更**——每条需求的验收判据、边界与裁决结论与 v0.6 一致。

---

## 旧需求编号 → 新标识

| 旧编号 | 新标识 | 名称 |
|:---|:---|:---|
| `#1` | `SPEC.FEAT.TERM.01` | VT 解析器 |
| `#2` | `SPEC.FEAT.TERM.02` | 颜色支持 |
| `#3` | `SPEC.FEAT.TERM.03` | 主/备屏幕缓冲 |
| `#4` | `SPEC.FEAT.TERM.04` | Scrollback 回滚 |
| `#5` | `SPEC.FEAT.TERM.05` | 滚动区域与光标控制 |
| `#6` | `SPEC.FEAT.TERM.06` | 鼠标上报 |
| `#7` | `SPEC.FEAT.TERM.07` | OSC 集成 |
| `#8` | `SPEC.FEAT.TERM.08` | 宽字符 |
| `#9` | `SPEC.FEAT.RENDER.01` | 等宽网格渲染 |
| `#10` | `SPEC.FEAT.RENDER.02` | 字体 |
| `#11` | `SPEC.FEAT.RENDER.03` | 属性渲染 |
| `#12` | `SPEC.FEAT.RENDER.04` | 光标 |
| `#13` | `SPEC.FEAT.INTERACT.01` | 键盘映射 |
| `#14` | `SPEC.FEAT.INTERACT.02` | 文本选择 |
| `#15` | `SPEC.FEAT.INTERACT.03` | 复制粘贴 |
| `#16` | `SPEC.FEAT.INTERACT.04` | 终端内搜索 |
| `#17` | `SPEC.FEAT.INTERACT.05` | URL 检测 |
| `#18` | `SPEC.FEAT.WS.01` | 多标签页 |
| `#19` | `SPEC.FEAT.WS.02` | 任意分屏 |
| `#20` | `SPEC.FEAT.WS.03` | 多窗口 |
| `#21` | `SPEC.FEAT.WS.04` | 标签状态提示 |
| `#22` | `SPEC.FEAT.WS.05` | 会话生命周期 |
| `#23` | `SPEC.FEAT.WS.06` | 全屏 |
| `#24` | `SPEC.FEAT.CONN.01` | 本地终端 |
| `#25` | `SPEC.FEAT.CONN.02` | SSH 连接 |
| `#26` | `SPEC.FEAT.CONN.03` | SSH 档案管理 |
| `#27` | `SPEC.FEAT.CONN.04` | SFTP 浏览器 |
| `#28` | `SPEC.FEAT.CONN.05` | 串口终端 |
| `#29` | `SPEC.FEAT.CONN.06` | Telnet |
| `#30` | `SPEC.FEAT.CONN.07` | 连接管理器 UI |
| `#31` | `SPEC.FEAT.PREF.01` | 主题 |
| `#32` | `SPEC.FEAT.PREF.02` | 设置面板 |
| `#33` | `SPEC.FEAT.PREF.03` | 持久化 |
| `#34` | `SPEC.FEAT.PREF.04` | 快捷键系统 |
| `#35` | `SPEC.FEAT.PREF.05` | i18n |
| `#36` | `SPEC.FEAT.PREF.06` | 零配置可用 |
| `#37` | `SPEC.NF.PERF.01` | 输入延迟 |
| `#38` | `SPEC.NF.PERF.02` | 渲染吞吐 |
| `#39` | `SPEC.NF.PERF.03` | 启动时间 |
| `#40` | `SPEC.NF.PERF.04` | 内存 |
| `#41` | `SPEC.NF.PLAT.01` | 跨平台 |
| `#42` | `SPEC.NF.RELI.01` | 可观测性 |
| `#43` | `SPEC.NF.A11Y.01` | 无障碍基线 |
| `#44` | `SPEC.FEAT.WS.07` | 命令面板 |
| `#45` | `SPEC.FEAT.WS.08` | Pane 缩放 |
| `#46` | `SPEC.FEAT.WS.09` | 广播输入 |
| `#47` | `SPEC.FEAT.WS.10` | 撤销关闭标签 |
| `#48` | `SPEC.FEAT.WS.11` | 会话恢复 |
| `#49` | `SPEC.FEAT.WS.12` | Quick Terminal |
| `#50` | `SPEC.FEAT.CONN.08` | SSH 隧道 |
| `#51` | `SPEC.FEAT.CONN.09` | 凭据安全存储 |
| `#52` | `SPEC.FEAT.CONN.10` | 密钥管理器 |
| `#53` | `SPEC.FEAT.CONN.11` | 会话日志 |
| `#54` | `SPEC.FEAT.INTEG.01` | OSC 133 命令块 |
| `#55` | `SPEC.FEAT.INTEG.02` | OSC 7 工作目录 |
| `#56` | `SPEC.FEAT.INTEG.03` | 系统通知 |
| `#57` | `SPEC.NF.PKG.01` | 打包分发 |
| `#58` | `SPEC.FEAT.INTEG.04` | CLI 启动参数 |
| `#59` | `SPEC.FEAT.XFER.01` | PTY 尺寸同步 |
| `#60` | `SPEC.FEAT.RENDER.05` | 缩放适配 |
| `#61` | `SPEC.FEAT.TERM.09` | 字符编码 |
| `#62` | `SPEC.NF.PERF.06` | 输出背压 |
| `#63` | `SPEC.FEAT.PREF.07` | 配置韧性 |
| `#64` | `SPEC.FEAT.CONN.12` | 剪贴板访问授权 |
| `#65` | `SPEC.NF.PERF.05` | 空闲资源占用 |

---

## 历史版本（v0.1–v0.6，原文照录）

> 以下条目为各版本当时的记录原文，其中出现的 `P0`–`P3`/`M0`–`M5` 是历史口径（现行定义见 [`PLAN.md`](PLAN.md)）；需求编号已随 v0.7 统一替换为语义标识，历史事实本身未作改写。

- v0.1（2026-09-20）：初稿。
- v0.2（2026-09-22）：业界查漏补缺（对照 Windows Terminal / Termius / XShell / WezTerm / iTerm2 / SecureCRT）——新增 SPEC.FEAT.WS.07、SPEC.FEAT.WS.08、SPEC.FEAT.WS.09、SPEC.FEAT.WS.10、SPEC.FEAT.WS.11、SPEC.FEAT.WS.12、SPEC.FEAT.CONN.08、SPEC.FEAT.CONN.09、SPEC.FEAT.CONN.10、SPEC.FEAT.CONN.11、SPEC.FEAT.INTEG.01、SPEC.FEAT.INTEG.02、SPEC.FEAT.INTEG.03、SPEC.NF.PKG.01、SPEC.FEAT.INTEG.04（命令面板、pane 缩放、广播输入、会话恢复、SSH 隧道、凭据安全存储、密钥管理、会话日志、snippets、OSC 133/7 集成、CLI 参数、打包分发等）；修订 SPEC.FEAT.TERM.01（bracketed paste/focus reporting）、SPEC.FEAT.TERM.06（alternate scroll）、SPEC.FEAT.RENDER.03（bold-is-bright/最小对比度）、SPEC.FEAT.INTERACT.02/SPEC.FEAT.INTERACT.03（智能选择、quick select、copy-on-select、右键行为）、SPEC.FEAT.CONN.01（TERM 注入/自定义命令）、SPEC.FEAT.CONN.02（keyboard-interactive/agent 转发）；裁剪表补 6 项（自动更新、Warp AI、Sixel、复用域、tmux control、Triggers）；缺口表补 G9–G11（全局热键/音频/通知）；里程碑扩 M5 观察池。
- v0.3（2026-09-22）：四项裁决落定（§7 改为已裁决）——产品名 Borealis、SSH 库 libssh2（接口抽象隔离）、默认字体内置 Cascadia Code、scrollback 默认截断；同步回填标题、SPEC.FEAT.TERM.04 SPEC.FEAT.RENDER.02 SPEC.FEAT.CONN.02 SPEC.NF.PKG.01。
- v0.4（2026-09-22）：规格书审查查漏补缺（对照 Aurora 主仓源码逐项实测 + 业界能力面；该行原引的审查报告 `REVIEW_SPECIFICATIONS_v0.3.md` 经 2026-09-29 全仓核查并不存在，其结论已全部并入本文，故不再作为引用目标）——
  - **新增 7 条**：SPEC.FEAT.XFER.01 PTY 尺寸同步（P0）、SPEC.NF.PERF.06 输出背压（P0）、SPEC.FEAT.RENDER.05 缩放适配（P1）、SPEC.FEAT.PREF.07 配置韧性（P1）、SPEC.FEAT.TERM.09 字符编码（P1）、SPEC.FEAT.CONN.12 剪贴板访问授权（P2）、SPEC.NF.PERF.05 空闲资源占用（P1）；
  - **就地修订 13 条**：SPEC.FEAT.TERM.01（DEC Special Graphics 字符集、`DECAWM`/`DECOM`/`IRM`、`DA1`/`DSR`）、SPEC.FEAT.TERM.07（OSC 标题消费链路与优先级）、SPEC.FEAT.INTERACT.01（`DECCKM`/keypad 应用模式）、SPEC.FEAT.INTERACT.03（复制语义三开关）、SPEC.FEAT.INTERACT.04（100k 行搜索指标）、SPEC.FEAT.INTERACT.05（打开前确认与协议白名单）、SPEC.FEAT.WS.02（任意分屏四义 + Splitter 二元约束 + 焦点路由自研）、SPEC.FEAT.WS.06 SPEC.FEAT.WS.10（P2 改标 P1）、SPEC.FEAT.WS.01（一度改标 P1，见 v0.5 回改 P0）、SPEC.FEAT.CONN.02（连接复用/旧算法兼容）、SPEC.FEAT.CONN.05（发送行尾序列/十六进制视图）、SPEC.FEAT.WS.07（改复用框架 `CommandPalette`）、SPEC.FEAT.CONN.09（审计范围界定）、SPEC.FEAT.CONN.11（默认关闭 + 风险提示 + 敏感过滤）；
  - **legend 修订**：P2 的交付窗口由「M4」放宽为「M3-M4，允许因主题内聚随 M3 一并交付」（SSH 族的 SPEC.FEAT.CONN.04 SPEC.FEAT.CONN.08 SPEC.FEAT.CONN.10 原本即落 M3）；M5 行的 `SPEC.FEAT.WS.04(beep)` 改为非编号写法，避免被解析为 SPEC.FEAT.WS.04 整体进 M5；
- v0.5（2026-09-23）：M1 工作区下限上调（裁决 7.10）——**SPEC.FEAT.WS.01 多标签页由 P1 回改为 P0、SPEC.FEAT.WS.02 分屏由 P1 改 P0，二者均由 M2 前移至 M1**；SPEC.FEAT.WS.02 重写为「任意分屏」并按**任意方向 / 任意深度 / 任意比例 / 任意 pane 数**四义界定，明确每一层为多子 pane 容器而非两两嵌套，并点明框架 `Splitter` 仅二元、pane 树须自研；与 SPEC.FEAT.XFER.01 PTY 尺寸同步标注强耦合。里程碑连带调整：M1 主题改为「多标签 + 任意分屏的本地终端」并收 SPEC.FEAT.WS.01 SPEC.FEAT.WS.02，M2 主题改为「工作区打磨」并移出 SPEC.FEAT.WS.01 SPEC.FEAT.WS.02；新增裁决 7.10 记录该决策及其工作量代价。
  - **新增裁决 5 项**：7.5 不 reflow、7.6 串口默认 GB18030、7.7 单实例转交落点、7.8 单进程模型、7.9 日志默认关闭；
  - **里程碑重排**：M1 收 SPEC.FEAT.XFER.01 SPEC.NF.PERF.06，M2 收 SPEC.FEAT.RENDER.05 SPEC.FEAT.PREF.07 SPEC.NF.PERF.05，M3 收 SPEC.FEAT.TERM.09，M4 收 SPEC.FEAT.CONN.12，M5 观察池收 SPEC.NF.A11Y.01（无障碍基线不再悬空）；
  - **附录 A**：路径改相对主仓根目录、引用改符号名（去除全部 `file:line` 锚点）、修正 4 处位置（`commands.h` / `splitter.h` / `theme_scope.h` / `storage.h`）、补 `CommandPalette` 与可拖拽 `Splitter` 两项高价值可复用能力、补 X11/Wayland IME 与 `PerfOverlay`、新增 G12 待评估项。
- v0.6（2026-09-29）：开工前口径对齐（框架现状按 Aurora 当日活动分支复核实测）——
  - **新增裁决 4 项**：7.11 跨平台交付分期（SPEC.NF.PLAT.01 保持 P0，M1 只交付 Windows、Linux 等价随 M2，并要求平台层从 M1 起即接口隔离）、7.12 应用侧三方依赖经 `find_package` + vcpkg（含 `x64-windows-static-md` 的 CRT 约束，标注为未经真机验证的推断）、7.13 框架缺口按类别分流（渲染与事件链路先补框架，交互体验类留应用侧）、7.14 命名统一为 `borealis`（修订 7.1 的「目录名不变」括注，工作区目录名列为唯一例外）；
  - **附录 A 事实修正 3 处**：G10 音频关闭（`AudioContext`/`AudioBuffer` 图 API 恒编译，WASAPI 与 ALSA 后端已实现，须经 `AURORA_ENABLE_AUDIO` 开启）、G12 DPI 通知关闭（`Surface::set_scale_change_handler` + Win32 `WM_DPICHANGED` 已接线，Linux 三后端未接线故留单腿）、A.1 相应新增「音频播放」「DPI 缩放变更上报」两行复用能力；
  - **G1/G2 形态落定**：G1 = `Painter` 批量文本 run 原语 + 等宽整像素 cell 度量 + East Asian Width 判定进框架（网格模型与颜色合成留应用侧，原语只收合成后的最终值），并记录 `FontEngine` 的 dp 测量与物理光栅宽度偏差为不可复用现有链路的依据；G2 = `click_count` 框架统一自算、五后端一致；A.3 反哺承诺据此改写（基准由「候选用例」升为随 M0 落地并挂 10% 回归门禁）；
  - **里程碑连带**：M0 出口判据补度量/EAW/基准门禁并将「G1–G3」更正为「G1–G2」（G3 属应用域，本就不进框架）；M1 主题标注 Windows、收 SPEC.NF.PLAT.01 的 Windows 侧；M2 主题改为「工作区打磨 + Linux 等价」并收 SPEC.NF.PLAT.01 与 SPEC.FEAT.RENDER.05 的 Linux 缩放腿；M5 行的缺口清单由 G9/G10/G11 收窄为 G9/G11；
  - **就地修订 2 条**：SPEC.FEAT.WS.04（可听铃声的能力缺口已消除，仅余排期取舍，优先级不动）、SPEC.FEAT.RENDER.05（框架现状与残留单腿）；SPEC.NF.PLAT.01 补交付窗口说明；
  - **文档缺陷修正 2 处**：A.2 表头声明 5 列而全部数据行为 4 列（「缺口」列本就内含事实依据），表头改为 4 列以消除渲染错位；v0.4 版本行所引的 `REVIEW_SPECIFICATIONS_v0.3.md` 全仓不存在，已就地标注为失效引用（与本仓即将沿用的「引用必须可达」口径一致）。
