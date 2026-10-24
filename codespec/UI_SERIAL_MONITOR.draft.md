# 串口十六进制收发监视设计稿（`SPEC.FEAT.CONN.05` 的延后子项）

> **状态**：地基实测已收口（2026-10-25，本机 POSIX 腿 + 一次性探针件，探针不入仓）；D1–D9 按 `AGENTS.md`
> §4.1 第 2 条列选项并给出推荐档，**已按长期授权自决取推荐档落稿**，人若改判按 §4.2 第 11 条回写裁决。
> 实现按 §4 分四批，每批独立本地提交（§4.6 第 26 条）。
> **需求原文**（`codespec/SPECIFICATIONS.md` §2 `SPEC.FEAT.CONN.05`）：「串口枚举、波特率/数据位/停止位/
> 校验/流控完整配置、断开重连、连接中热改波特率；默认编码 GB18030；发送行尾序列可配（CR / LF / CRLF，
> 默认 LF）；**可选十六进制收发视图与行首时间戳前缀（延后子项）**」。
> **在册去向**（`codespec/PLAN.md` §4）：`SPEC.FEAT.CONN.05` | 十六进制收发视图与行首时间戳前缀 | M4 之后。
> **已落地的前置**：串口全链三批（裁决 7.107–7.111，稿 `codespec/UI_SERIAL.draft.md`）、`Session` 的写出侧
> 一处行尾折算（7.110 D11①）、会话日志的「会话级参数不进 schema + 锁内取副本锁外写」形态（7.116）、
> 面板族与状态泵（`keys_panel` / `tunnel_panel` / `snippet_panel` / `sftp_panel`，裁决 7.97、7.137）、
> 面板搬窗链（`retarget_panel` / `retire_panel` 与 `itest_panel_retarget`）、词条双档与覆盖闸（PREF.05）。
> **配套草图**：本期不出高保真 SVG/PNG（`UI_SNIPPETS.draft.md`、`codespec/CONN03_PROXYJUMP.draft.md` 同口径），
> 形态复用面板族的停靠卡片 + `Scroll` 行区；§2 给行形态的算术与屏面草图。

---

## 0 地基实测（2026-10-25 本沙箱，Linux 腿；Windows/MSVC 一律标未验证）

| # | 实测事实 | 读数 | 对本稿的影响 |
|---|---|---|---|
| F1 | 入向字节的唯一截点：读线程把一批字节交给 `Session::on_bytes` → `ingest`，`ingest` 在锁内先喂 `decoder_->feed(bytes, sink)`，出锁后才做日志落盘 | `src/session/session.cpp` 的 `ingest`（锁内解码、锁外 `log_target->append`） | RX 采样点与日志同一条腿：**锁内不留痕、出锁才拷贝**；`bytes` 在整次 `ingest` 调用期内有效，故出锁后仍可用（D2） |
| F2 | 出向字节的唯一截点：两条发送通道（`flush` 的编码产物、`send_bytes` 的原样转义字节）都汇到 `Session::write_out`，那里做**唯一一处**行尾折算后交 `connection_->write` | `src/session/session.cpp` 的 `flush` / `write_out`（`write_out` 有两个 `connection_->write` 出口：不折算的早退路与零分配的尾部路） | TX 采样点取**折算之后**的字节，才是线上真实字节；两个出口要么各挂一次、要么收一处（D2 的代价项） |
| F3 | 串口读块与片长常量 | `kReadChunkBytes = 32768`、`kReadSliceMs = 20`、`kProbeTimeoutMs = 0`（`src/conn/serial_connection.cpp`） | 一次入向 run 的**上界是 32 KiB**；run 数量级由设备节奏决定，不由我们的缓冲决定（D4 的容量算绪） |
| F4 | 32 KiB 纯拷贝成本（探针 `probe_hexview`，10 万次环） | **0.604 µs / 32 KiB**（约 52 MB/s 的 memcpy 折算） | 武装态每批多花半微秒，`SPEC.NF.PERF.06` 的「回调不做长计算」成立；监视本身不是性能事件 |
| F5 | 「时间戳 + 十六进制」一行（16 字节档）的折算成本 | **0.239 µs / 行**，其中取本地历法 **0.204 µs / 行**（`localtime_r` + `strftime` 占 85%） | 折算**不进读线程**、只进面板侧重建那一次：60 行 ≈ 14 µs，仍在帧预算内（D6 的分工依据） |
| F6 | 行串形（16 字节档 + 完整日期前缀） | `[2026-10-25 00:06:57.496] RX 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F 50` = **77 字符** | 77 字符在默认窗宽里放不下（F8），故行形态要重排（D5） |
| F7 | 1 MiB 设备流量展开成行文本的体积 | 65536 行（16 B/行）= **5,046,272 字节**行文本 ≈ 流量的 4.8 倍 | **环里存的必须是原始字节而不是行文本**；行文本只在重建那一刻存在（D3） |
| F8 | 面板行宽与字号的实测换算（探针 `probe_cell`，`FontEngine::monospace_cell` / `measure_width`，scale=1） | Cascadia Code 单格推进：11/12 pt = 9 dp、13 pt = 10 dp、**14 pt = 11 dp**；故 520 dp 卡 ≈ **47 字符**、560 dp ≈ 50 字符、77 字符需 847 dp、86 字符需 946 dp。新窗缺省尺寸 960×640 dp（`src/main.cpp` 的 `open_workspace_window()` 建窗地基） | 「16 字节 + ASCII 尾栏」这类行放不下任何现有卡片宽度；要么减字节数、要么加宽卡片（加宽到 946 dp 即等于盖住整窗）——D5 的算术来源 |
| F9 | 等宽族在面板侧可用 | `au::Text::font_family(std::string)` 与 `Font::family` 是公开接缝；本仓缺省等宽族名常量 `ui::kDefaultMonospaceFamily` = `"Cascadia Code"`（`include/borealis/ui/font_choice.h`） | 十六进制行**不需要**新的绘制腿：给行 `Text` 设等宽族即可对齐（与 `terminal_view.cpp` 的 `monospace_cell` 同源字体） |
| F10 | pty 替身的投递形态（探针 `probe_serial_chunk`） | canonical 档下 400 字节**无换行即不投递**；`cfmakeraw` 后 400 字节突发**一次读全**；每 1 ms 各 10 字节的慢速流则**一批一 run**；**一条行会被切在两个读块里**（实测 15 + 3 字节） | 视图侧的「行」必须自己按 CR/LF 折算并**跨 run 留挂起尾段**，否则同一设备行显示成两行（§3 D5 的文本折算口径）；也说明 run 边界与设备行边界无关 |
| F11 | 时钟可用精度（探针 `probe_hexview` 相邻 `now()` 差值） | min **11 ns** / max 3.9 µs（`system_clock::duration::period` 在本 libstdc++ 上报 0 ns，不可用作结论） | 毫秒前缀够用；**run 级单点时间戳**（一批一个）而非逐字节时间戳，成本与 F10 的「一批就是设备的一次突发」语义都吻合 |
| F12 | 出向字节从不进网格；入向字节进网格前已被 VT 状态机吃掉 | `write_out` → `connection_->write` 之外无消费者；`ingest` 的产物是 `decode_buffer_` → `terminal_.feed`（`src/session/session.cpp`） | 十六进制视图**不可能**做成「终端格换一档」：TX 根本没有格；网格内容是解码+解析后的产物，塞假字节会同时污染复制、搜索与 scrollback（D1 的否决理由） |
| F13 | 既有会话级参数的先例 | `LogSpec::timestamp` 是开启时给的**会话级量**、不进 schema；`kMaxSessionLogBytes` 是文件级常量而非配置键（`include/borealis/session/session_log.h`，裁决 7.116 D8①/⑥） | 本稿的时间戳开关与环容量走同一条口径：**零新设置键**，省掉「schema + store 读写 + 搬运 + 核对表行 + 词条 + 计数」六处同步 |
| F14 | 面板状态泵与观测面先例 | `TunnelPanel::tick()` 由装配层 `set_on_frame` 调用、读 `snapshot` Hook，行表逐字段相等就不重建；`SnippetPanel` 同族；观测面 `visible_rows()` / `notice()` / `is_open()`；`itest_panel_retarget` 现覆盖**七只**瞬态面板 | 监视面板照这套骨架（D8/D9），并**必须**进 `itest_panel_retarget` 成为第八只——搬窗漏一条就是「浮层留在已散的那扇窗上」 |

**未在实测射程内**：Windows/MSVC 构建与真机走查（沙箱无 MSVC、无交互桌面、无 `tty`/`dialout` 权限——
`codespec/UI_SERIAL.draft.md` §0 的 F1/F10 那两条环境欠账在本稿同样成立）。本稿全部 UI 判据走无头真布局 +
真派发，「可用」结论留待人工走查（§4.4 第 33 条）。

---

## 1 范围与边界

**本期交付**＝需求原文那两句，逐句对口：

1. **十六进制收发视图**：一只停靠卡片面板，逐行显示该串口会话的**原始字节 run**，每行带方向（RX／TX）与
   十六进制字节；可打印 ASCII 就近显示在尾栏（D5）。
2. **行首时间戳前缀**：每行行首一个 `[HH:MM:SS.mmm]`，取自该 run 到达／写出那一刻（D6）；面板内一枚开关可关掉。
3. **可选**：默认不采集。只有用户在串口标签上唤起过一次面板，那条会话才开始留痕（D7）。
4. 面板动作四个：刷新（自动跟帧）／暂停、清空、时间戳开关、丢弃计数上屏。

**不在本期**（§7 在册）：滚动回溯、导出到文件、复制行到剪贴板、过滤与搜索、逐字节时间戳、报文边界识别
（不做任何协议解析）、把监视扩到本地/SSH/Telnet 三型（机制在 `Session` 上通用，**入口只挂串口标签**）、
真机走查与 Windows 腿验证。

**与既有裁决的交界**：

- **不动 VT / 网格 / 编码腿**：视图吃的是 `Session` 两端的字节，不碰 `decoder_`、不碰 `terminal_`、不碰
  `term::encode_key` 的字节表，也不新开第二套解码器（D5 的「文本档」为什么不做的理由）。
- **不动行尾折算**：TX 采样在 `write_out` 折算之后，因此视图显示的字节与线上逐字节一致（F2），也因此
  「档案选 LF 还是 CRLF」在这张卡上是**看得见的证据**——这正是这条判据的副产品。
- **不落盘**（§4.5 第 24 条同档）：环只在内存，不进会话日志文件、不进崩溃转储、不进配置目录。设备流量
  可能含口令回显，「不持久化」是本件的第一条安全口径，代价是关掉就没了（§7）。
- **不新设置键**（F13）：时间戳开关与显示行上限都是面板局部量；重启后回到缺省档。

---

## 2 屏面与判据对应

主屏＝左侧停靠卡片（非模态、默认隐藏）。行形态算术（14 pt 等宽、单格 11 dp，F8／F9）：

```
[12:00:00.345] RX  41 42 43 44 45 46 47 48  |ABCDEFGH|
└── 14 ──┘└ 5 ┘ └─────── 23 ───────────┘ └── 12 ──┘  = 54 字符 ≈ 594 dp（末段含分隔两空格与竖线）
```

| 区域 | 判据 | 对应需求子项 |
|---|---|---|
| 标题行 | 「串口监视」+ 绑定的档案名与设备路径 + 字节计数 +「清空」+「关闭」；非串口标签或该格无会话时面板不出现（命令侧先分流） | 十六进制收发视图 |
| 开关行 | 两枚：时间戳前缀（缺省开）、暂停刷新（缺省关）。**缺字段设置键**：都是面板局部量 | 行首时间戳前缀 + 「可选」 |
| 行区 | 每行一条 8 字节的转储行（D5）：`[HH:MM:SS.mmm]` + `RX`/`TX` + 8 组两位十六进制 + 竖线包起的 ASCII 尾栏；等宽族 `kDefaultMonospaceFamily`；RX／TX 只以这两个字母与一列强调色区分，行正文一个中文都不出（面板上的可见文案全走 `settings_label(key)`，否则覆盖闸的表外文件判据当场转红） | 两项 |
| 行边界 | 尾栏只承 ASCII 可打印区（0x20–0x7E），其余字节（0x00–0x1F 与 0x7F–0xFF）一律一个 `.`；十六进制正文一律大写字母 | 视图可读性 |
| 挂起尾段 | 一个 run 里的最后一段没有行尾跟随的字节，不单独成行、也不丢——它与后续 run 拼成同一显示行（F10） | 视图正确性 |
| 留痕行 | 「已丢弃 N 字节（环上限 256 KiB）」只在 N>0 时出现一行；「本面板不落盘，数据随会话结束消失」在武装那一刻出现一行 | 边界诚实 |
| 空态 | 尚未有任何 run 时行区画一句「等待数据」 | 视图 |

---

## 3 裁决（D1–D9，推荐档已自决采纳）

| # | 待决 | 选项 | 结论（推荐） | 理由 |
|---|---|---|---|---|
| **D1** | 视图落点 | ① 独立停靠卡片（面板族第 8 只）；② `TerminalView` 里加一档「十六进制模式」把行写进网格；③ 视口角落的常驻浮层 | **①** | ② 在结构上就不成立：TX 字节从不进网格（F12），要显示「发出去的东西」必须先造一套第二网格；而往网格塞假内容会同时污染复制、搜索、`OSC 133` 块表与 scrollback 语义。③ 常驻浮层违背「可选」，且遮挡正在看的设备输出。① 与隧道／片段／SFTP 同族，搬窗、状态泵、观测面三条腿全是现成的（F14） |
| **D2** | 采样落点 | ① 都在 `Session`：RX 取 `ingest` 出锁后那份原始 `bytes`，TX 取 `write_out` 交连接前的字节；② 各连接腿自己记；③ 平台层（`platform::serial`）记 | **①** | ② 要为四型连接各写一遍，其中三型本期没有消费方（§4.1 第 3 条不加将来才用的抽象），且 SSH 腿要同时覆盖 shell／SFTP／隧道多条通道，形态完全不同；③ 违反 §4.5 第 23 条（平台假设不得渗入共享路径），而且在平台层就看不到**折算后**的出向字节（F2），行尾那半条判据会失真。① 一处腿、两型出口天然都覆盖 |
| **D3** | 环里存什么 | ① 原始 run（方向 + 一个时间戳 + 字节）；② 折好的显示行文本；③ 只存计数不存内容 | **①** | F7 说 1 MiB 流量折成行文本是 5 MB，而同样 1 MiB 原始字节只有 1 MiB；行文本还会把「视图开关是显示期量」这件事焊死在采集期（改档就得重采、采过的字节就没了）。① 让折算留在面板侧，视图形态可调而采集腿不动 |
| **D4** | 容量与丢弃 | ① 有界字节环 256 KiB（文件级常量，同 `kMaxSessionLogBytes` 的先例），逐出最旧 run，丢弃字节数累计上屏；② 无上限；③ 固定保留最后 N 个 run | **①** | ② 在最坏情形（115200 波特连刷）是每会话几十 MB 量级的常驻内存，且没有任何一处告诉用户「你看到的不是全部」；③ 的 run 数量级由设备节奏决定（F3／F10：一次突发可以是 32 KiB 一个 run，也可以是 10 字节一个 run），按条数封顶就等于按设备节奏封顶。① 的 256 KiB ≈ 32 KiB 读块 × 8 次，够看一整屏报文；丢弃必须**上屏**，否则「环里最早那行」会被当成「设备最早发的那行」 |
| **D5** | 行形态（每行几字节、要不要 ASCII 尾栏、要不要文本档） | ① **8 字节/行 + ASCII 尾栏**（54 字符 ≈ 594 dp，卡片 640 dp）；② 8 字节/行、无尾栏（42 字符 ≈ 462 dp，卡片 520 dp 即族内同宽）；③ 16 字节/行 + 尾栏（86 字符 ≈ 946 dp）；④ 再加一档「文本视图」（按会话编码解码后可读文本） | **①** | ③ 被默认窗宽否掉：960 dp 的窗里挂一张 946 dp 的卡等于盖住整个终端（F8）。④ 需要**第二套解码腿**——`Session` 那份解码器是有状态的、跨 run 挂起半截序列，不能逐块复用；另起一份就要求 `Session` 交出编码档，而 GB18030 的汉字在监视卡里被重新解码一次，与网格那份没有任何一致性保证（两条腿会说出两种话）。需求原文只写「十六进制收发视图」，① 的尾栏已经把「可打印 ASCII 就近看一眼」这个真实诉求覆盖了（AT 命令、Modbus ASCII 这类文本协议正落在这一档）。② 少 12 个字符换一个族内同宽卡片，代价是设备回的文本要逐字节在心里折一遍——尾栏才是这一档的主要价值，故不取 |
| **D6** | 时间戳粒度与位置 | ① 每行行首 `[HH:MM:SS.mmm]` 短档 + run 级单点时间戳，格式化在面板重建时做；② 完整日期（F6 那 23 字符）；③ 每个 run 只首行带时间戳、续行缩进；④ 采集线程就把前缀折好 | **①** | ② 吃掉 11 个字符，等于把 8 字节档挤成 5 字节档；串口调试的「过了多久」是秒／毫秒量级，不是「哪一天」。③ 与需求「行首时间戳前缀」的直读不一致，且 `SPEC.FEAT.CONN.11` 已把「每行 + 本地历法」定成会话日志那一档的既有形态，两处不同口径比一处略吵更糟。④ 把 0.204 µs/行的 `localtime` 成本推进读线程（F5），违背 F1 那条「锁内/采集期只留值、不做格式化」的既有纪律；采集期只存一个 `int64` 毫秒值，格式化留给面板那一次重建 |
| **D7** | 采集生命周期 | ① 面板开＝武装；关面板不摘环，`清空` 才清；会话析构／重启自然归零；不做显式「停止采集」；② 关面板即停采；③ 面板上再加一枚显式「停止采集」 | **①** | 「先让它跑，回头再看」是这条需求的主用法：② 会让回看变成空环。常驻代价只有 F4 那半微秒一批的拷贝与 D4 的 256 KiB 上界，代价封顶。③ 在本期没有消费方（「暂停刷新」已经解决「别刷屏」，「清空」已经解决「重新开始」），加了就是第四枚开关。会话重启＝新 `Session` 对象＝不采集，与裁决 7.104「新对象重新上弦」那条口径同形 |
| **D8** | 入口 | ① 命令 `serial.monitor.toggle`（不绑缺省键位），分流判据＝当前标签在 `serial_by_tab` 里有身份（与 `serial.set_baud` 同一条判据）；② 串口标签右键菜单加一条；③ 常驻工具条按钮 | **①** | ② 与 ③ 都是新造一条 UI 通路，而 D5①／7.110 已经为「低频但有明确入口」这件事定过档：藏进命令面板，代价（入口深）在裁决 7.110 D5① 里已经登记过一次并留了「真机走查若不可接受再补视口内入口」的活口——本棒沿用同一活口，不重复登记。命令的 `action` 里现取 `current_tab_session()`：非串口 ⇒ 留痕一句、不武装任何会话 |
| **D9** | 词条与设置面 | ① 零新设置键；新词条一族 `serial.monitor.*`（中英双档），文案一律经 `settings_label(key)`；② 环上限与时间戳档做成设置键 | **①** | F13 那条六处同步的代价与 7.116 D8① 的既有取舍同档；面板局部量重启即回缺省这一条要如实写进 §7 的在册项。② 的第二条理由是 `utest_settings_catalog` 的双向核对表要为它加行、`Absent` 计数要动，而本件没有一个「跨会话一致」的需求方在等它 |

---

## 4 文件布局 / TU 拆分（按 D1–D9 落，四批各自独立提交）

```
include/borealis/session/raw_tap.h   新增 —— 公共头（无 Aurora 类型，与 session_log.h 同层同规）：
                                        enum class RawDirection{Received,Transmitted};
                                        struct RawRun{RawDirection direction; std::int64_t timestamp_ms;
                                                      std::vector<std::byte> bytes;};
                                        class RawTap（自有 mutex、有界字节环、丢弃累计）：
                                          record(direction, timestamp_ms, bytes) / snapshot(max_bytes) /
                                          clear() / dropped_bytes() / total_bytes() / run_count()。
                                        kMaxRawTapBytes = 256 KiB（文件级常量，F13/D4）。
src/session/raw_tap.cpp              新增 —— 环的实现：逐出最旧 run、单 run 超上限即整条拒收并计丢弃。
include/borealis/session/session.h   改 —— 尾追加三枚接缝：enable_raw_tap()（幂等，返回 shared_ptr）、
                                        raw_tap()（观测/装配取数）、以及 ingest/write_out 用到的私有记痕点。
                                        缺省未武装＝空指针＝零成本，既有八个构造点不改一字。
src/session/session.cpp              改 —— RX：`ingest` 出锁后、与 `log_target->append` 同一世代记一笔；
                                        TX：`write_out` 的两个 `connection_->write` 出口收成一处私有
                                        `emit_to_connection()`，折算后的字节在那里记一次
                                        （代价登记：多一枚私有函数，换来「一处采样」这条判据可断言）。
src/ui/serial_monitor_format.{h,cpp} 新增 —— 纯逻辑件（无 Aurora 类型，`serial_format` / `tunnel_format` 同族）：
                                        选项{带时间戳、每行字节数=8}；`monitor_rows(runs, options)`
                                        逐 run 折行 + 跨 run 挂起尾段 + ASCII 尾栏 + `.` 兜底；
                                        时间戳格式化（`localtime_r`/`localtime_s`，`strftime`）也收在这件里。
src/ui/serial_monitor_panel.{h,cpp}  新增 —— 私有头（含框架类型）。Hooks：`snapshot`（取 run 表 + 丢弃数）、
                                        `clear`、`binding_label`（档案名/设备路径）；`tick()` 由装配层
                                        on_frame 驱动、行表逐字段相等不重建；观测面 `visible_rows()` /
                                        `notice()` / `is_open()` / `timestamp_enabled()`；
                                        `set_overlay_host` 搬窗（第八只）。
src/ui/settings_i18n.cpp             改 —— `serial.monitor.*` 中英双档（标题、两枚开关、留痕三句、空态）。
src/main.cpp                         改 —— `serial.monitor.toggle` 命令 + 面板实例 + Hooks + `on_frame` 里
                                        一行 `tick()` + `retarget_panel` / `window_closed` 撤账两处。
tests/unit/utest_raw_tap.cpp         新增 —— 环容量、逐出序、丢弃累计、单 run 超上限、clear、快照上界。
tests/unit/utest_serial_monitor_format.cpp 新增 —— 8 字节档行形态、尾栏 `.` 档、CR/LF/CRLF 与挂起尾段
                                        跨 run 拼接（F10 那条 15+3 的形态）、时间戳开关、毫秒零填充。
tests/unit/utest_session.cpp         改 —— 未武装零成本；武装后 RX 与收到的字节逐字节一致；
                                        TX 记的是**折算后**字节（LF／CRLF／CR 三档各一例，CR 档即原样）。
tests/integration/itest_serial_monitor_panel.cpp 新增 —— 无头真派发：开→行表→暂停→清空→时间戳开关→留痕。
tests/integration/itest_panel_retarget.cpp 改 —— 第八只面板。
codespec/{SPECIFICATIONS,CHANGELOG,PLAN,ARCHITECTURE}.md 改 —— 裁决、版本、清册出账、模块表。
```

**为什么折算件在 `src/ui/` 而不是 `src/session/`**：行形态是显示期决定（D5/D6 的两个开关都在这里生效），
放进 session 层就是让公共头认识「一行几个字节」这种界面量；纯逻辑性由「无 Aurora 类型」保证，与
`ui::serial_format` / `ui::tunnel_format` 完全同族，可无头单测（§4.4 第 20 条）。

### 4.1 批 1 落地时对稿面的三处改口（2026-10-25，按 §4.2 第 12 条以运行时为准回写）

1. **`snapshot()` 不带上界形参**。稿 §4 那句写的是 `snapshot(max_bytes)`，与 §5 那条「D4 的环上界就是
   快照上界，不需要第二个数」本来就是矛盾的；落地取后者——第二个数只会造出「环里还有，但你只取了
   这么多」这种没人要的形态，而它没有任何一处能自证。
2. **`cleared 计数` 不落**。§4 列它的时候假想消费方是「面板区分从未采集与刚清空」，批 1 写完判下：
   清空之后环是空的、`dropped_bytes()` 也归零，面板只看这两枚就够，第三枚计数没有任何一处读它
   （§4.1 第 3 条：不加将来才用的抽象）。真要用到再随批 3 的面板一并开。
3. **快照末尾那段挂起尾段**——「不单独成行」只约束**同一个 run 之内**（那条与后续 run 拼成同一显示行，
   F10 的 15 + 3 那一例）；整个快照结束时如果还有没等到行尾的字节，它**上屏成不完整行**。不留的后果是
   二进制协议（整段可以没有 CR／LF）在卡上一行都不出，而环里明明存着——看不见就是不诚实。它一旦等到
   行尾，下一次重建自动并回同一段，latest-value 的读法没有游标也就不会漏。

**批 1 结项读数**：`ctest --preset linux -E etest_` **123 项全绿**（+2＝`utest_raw_tap`／
`utest_serial_monitor_format`），`borealis_test_runner` 全跑 **1495 案全绿**（+21＝环 9 案 + 折算 12 案）。
非空转按 §4.4 第 22 条以三则变异自证，各转红、还原后 `cmp` 逐字节相同（这三则就是 §6 批 4 清单里的
ⓓ／ⓒ 两则加上折算件的那条行尾变体，本批先把它们做掉，批 4 只做 ⓐ／ⓑ 两则 `Session` 侧的）：
ⓐ 环逐出改按条数（`runs_.size() > 4`）⇒ 环 5 案转红；ⓑ 快照末尾的挂起尾段整条丢掉 ⇒ 折算 7 案转红；
ⓒ 挂起尾段在每个 run 边界就落笔成行（F10 那条判据）⇒ 折算 3 案转红。

---

## 5 数据流与线程模型

```
读线程 ── on_bytes ──► ingest ──┬── 锁内：decoder_->feed → terminal_.feed → collect_damage
                               └── 出锁：raw_tap->record(Received, now_ms(), bytes)   ← 与 log append 同世代
主线程 ── 键入/粘贴/片段 ──► send_text / send_bytes ──► flush ──► write_out（行尾折算）
                                                                  └── raw_tap->record(Transmitted, now_ms(), 折算后)
主线程 ── on_frame ──► SerialMonitorPanel::tick() ──► snapshot Hook ──► RawTap::snapshot(≤ 环上限)
                                                        └── monitor_rows(...) ──► 行表 diff ──► 有变才重建行区
```

- **两把锁不相交**：`RawTap` 自有 `mutex_`，`Session::mutex_` 只护状态机与解码器（架构 §3.4「锁内不做 IO」）。
  `record()` 只在**不持** `Session::mutex_` 的上下文里调用，这条写成头注的硬约束，与 `SessionLog::append` 同。
- **latest-value 而不是事件流水**（§4.5 第 25 条）：面板每帧取「环里现在有什么」，丢帧不影响下一帧正确性；
  不做「本帧新增的 run」那种增量协议——那要引入消费游标，而游标一旦与暂停/清空交错就会说出「少了半屏」。
- **拷贝量级**：一次 `snapshot()` 至多交出 256 KiB（D4 的环上界就是快照上界，不需要第二个数）；
  面板只把最后可见的那几十行折成字符串（F5：60 行 ≈ 14 µs），其余 run 折都不折。
- **武装态的常驻代价**：F4 的 0.604 µs / 32 KiB 加一次 `now()`（F11：11 ns 量级），每批一次；
  `record` 只在指针非空时发生，未武装即 `nullptr` 判一次——与今天的 `log_` 那条同形。

---

## 6 验收判据与分批

| 批 | 交付 | 判据与读数口径 |
|---|---|---|
| 批 1 | `session::RawTap` + `ui::serial_monitor_format` 两件纯逻辑 + 两份 utest | 环逐出/丢弃/快照上界各有案；行形态按 §2 的算术逐字符断言（54 字符那一条写成用例常量）；F10 的「一行跨两 run」有案。**结项读数**：`ctest --preset linux -E etest_` 项数 +2，runner 全案数报实测数 |
| 批 2 | `Session` 的 RX／TX 两笔记痕（含 `emit_to_connection` 那一处收口）+ `utest_session` 扩案 | 未武装零成本、RX 逐字节一致、TX 是折算后字节三档各一案；`emit_to_connection` 收口后**原有行尾折算四案必须仍全绿**（那条是 7.110 D11① 的既有判据，不能被本棒改口径） |
| 批 3 | 面板 + 装配 + 词条双档 + `itest_panel_retarget` 第八只 | 无头真派发：开关联动、暂停、清空、时间戳开关、丢弃留痕各一案；`python3 tools/check/check_display_literals.py` 覆盖闸 PASS（表外文件零引号中文）；`utest_i18n_tiers` 两档 key 集等式仍成立 |
| 批 4 | 变异自证 + 文档回写 + 分层提交 | 至少四则变异各转红并还原：ⓐ `record` 挪进 `Session::mutex_` 临界区（并发/成本类断言）、ⓑ TX 采样改在折算前（行尾那案转红）、ⓒ 挂起尾段不留（F10 那案转红）、ⓓ 环逐出按条数不按字节（D4 那案转红）；回写 `SPECIFICATIONS.md` §7 裁决 + `CHANGELOG.md` 新版本 + `PLAN.md` §4 该条出账 + `codespec/UI_SERIAL.draft.md` §7 那条「延后」改口 |

**判据逐条对应需求原文**：「可选」＝D7（默认不采、开面板才采，未武装会话零成本）；「十六进制」＝D5 的 8 字节
转储行 + 尾栏；「收发」＝D2 两条腿（RX／TX 同屏、方向显式）；「视图」＝D1 的停靠卡片；「行首时间戳前缀」＝
D6（每行一个、短档 + 毫秒、可关）。

**不称「可用」**（§4.4 第 33 条）：§2 那张卡的观感——54 字符在 640 dp 里是否真的不被截断、等宽字形对齐、
毫秒够不够排障、刷屏时读不读得过来——都要人眼；本沙箱无串口真设备（`UI_SERIAL.draft.md` §0 F1），
`etest_serial_device` 那档欠账不变，本稿不新增也不销账。

---

## 7 本期不做（延后 / 在册）

- **滚动回溯与历史窗口**：只显示最近若干行；想看更早的内容得先按「暂停」。在册理由：跟尾与手动滚动要在
  同一只 `Scroll` 上抢位置，形态需要真机手感才能定，先不猜。
- **导出／复制到剪贴板**：与 `SPEC.FEAT.CONN.11` 的落盘腿撞口径——那份已经有「文件、上限、打码器、目录」
  四件套；监视卡若要落盘就该复用那条腿而不是新写第二份，属独立一棒。
- **文本视图（按会话编码解码）**：见 D5 ④ 的第二套解码腿问题；在册。
- **本地／SSH／Telnet 三型接入**：机制在 `Session` 上通用，入口只挂串口标签（需求原文只在这里提十六进制）。
  其余三型的入口若要开，各自单开一棒并在 `PLAN.md` 清册登记；届时 SSH 要回答「哪条通道的字节」这个新问题。
- **逐字节时间戳与报文边界识别**：run 级单点时间戳（F10 的一批就是一次设备突发）已覆盖需求那句「行首前缀」；
  更细的粒度要读线程逐字节取时刻，属另一档性能事件，不预建。
- **环上限与缺省开关档跨重启记忆**：D9 的代价面——重启即回缺省档（带时间戳、跟帧刷新）。
- **关掉采集的显式动作**：D7 ① 的代价面——武装后随会话全程；「清空」是本期唯一的止血手段。
- **真设备与 Windows/MSVC 两档验证**：`etest_serial_device` 待有 `tty`/`dialout` 权限与实机串口线；
  `src/platform/win/serial.cpp` 那腿从未编译（`UI_SERIAL.draft.md` §0 F10 的在册欠账，与本稿同档）。
- **本稿的高保真 SVG/PNG**：与 `UI_SNIPPETS.draft.md` 同口径不落（手工生成的图插一屏会牵动全图版面，
  形态判据以 §2 的算术与草图为准）。

---

## 8 探针复现口径（一次性脚本，不入仓）

- `probe_hexview`：10 万次 32 KiB 拷贝 → 0.604 µs／次；逐 16 字节行折「时间戳 + 十六进制」→ 0.239 µs／行
  （其中 `localtime` + `strftime` 0.204 µs／行）；一行串形 77 字符；1 MiB 流量 → 65536 行 → 5,046,272 字节行文本；
  相邻 `system_clock::now()` 差 min 11 ns／max 3.9 µs。→ F4／F5／F6／F7／F11。
- `probe_serial_chunk`：`pty.openpty()` slave 上先 canonical（400 字节无换行不投递）、后 `cfmakeraw`
  （400 字节一次读全；每 1 ms 各 10 字节的慢速流一批一 run；一条行被切成 15 + 3 字节的两个读块）。→ F10。
- `probe_cell`：链 `libaurora.a` 实调 `render::FontEngine::monospace_cell` / `measure_width`，
  Cascadia Code 11/12/13/14 pt 单格推进 9/9/10/11 dp，77 字符在 14 pt 下 847 dp。→ F8／F9。
- 读源盘点（非运行时）：`src/session/session.cpp` 的 `ingest` / `flush` / `write_out`、
  `src/conn/serial_connection.cpp` 的三个读块常量、`include/borealis/session/session_log.h` 的会话级参数先例、
  `src/ui/{tunnel,snippet,sftp}_panel.*` 与 `tests/integration/itest_panel_retarget.cpp` 的面板族形态。→ F1／F2／F3／F12／F13／F14。
