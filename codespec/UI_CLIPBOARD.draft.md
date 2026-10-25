# 剪贴板读授权设计稿（`SPEC.FEAT.CONN.12`）

> **状态**：**出稿待评审（2026-10-12）**。§3 的 D1–D12 每条给「推荐档」与代价；按人已给出的常存授权（离席期间「遇到问题的按推荐去做」），本棒的 D1–D12 **全部按稿面推荐档自决收口**，收口事实随实现入册为裁决 **7.118**，改判位留在每条的「推荐」列旁，评审回来到此改判即可。
> **上游依据**：`SPEC.FEAT.CONN.12` 原文（`codespec/SPECIFICATIONS.md` §连接域）——三句都是判据：① OSC 52 **读**方向**默认禁止**；② 可在设置中开启，**三态：禁止 / 允许 / 每次询问**；③ **触发读请求被拒时须向会话回写空响应而非静默无响应**（避免远端程序挂起）。写方向的「默认允许」已随 `SPEC.FEAT.TERM.07` 提前落地（裁决 7.21③），本棒不碰。另两条口径来源：裁决 7.9（凭据/隐私类默认关闭的取向）、`codespec/ARCHITECTURE.md` §3.4（临界区内不做 IO）与 §2.3（`include/borealis/` 不得出现框架头）。
> **面板侧的既有预告**：`codespec/UI_SETTINGS.draft.md` §3 第 6 条把「OSC 52 剪贴板读取（三态）」**挡在当时的设置页之外**——理由是「属 `SPEC.FEAT.CONN.12`（M4），schema 无键」，处置是「改在终端页脚注一行说明它随该条落期入 schema」。本棒就是那句脚注的「落期」：键入 schema、控件进终端页、脚注那句同步改口（判据 7）。
> **配套高保真草图**：`codespec/UI_CLIPBOARD.draft.svg`（矢量源）/ `codespec/UI_CLIPBOARD.draft.png`（1× 出图）。图中圈号 1–6 即 §2 的判据标号。SVG 为事实来源，改图直接改 SVG。

---

## 0 地基实测（2026-10-12 本沙箱，POSIX 腿 + 源码与既有用例读数；Windows 腿一律标未验证）

| 号 | 一手事实 | 证据 |
| --- | --- | --- |
| **G1** | **读查询已被识别且已回写空响应**：`Terminal::apply_clipboard` 里 `payload == U"?"` 那一支现在就发 `ESC ] 52 ; c ; BEL`，并留 `TODO(SPEC.FEAT.CONN.12)` 点名「三态落地后『允许』档在这里回写正文」 | `src/term/terminal.cpp` |
| **G2** | 判据 ③「被拒 ⇒ 回写空响应」**已有两层证人**：`utest_terminal_osc` 从状态机侧断应答文本，`utest_session.cpp` 从连接侧断「经写通道出去的那串字节逐字节等于空响应」 | `tests/unit/utest_session.cpp`、`tests/unit/utest_terminal_osc.cpp` |
| **G3** | **载荷切分只切最后一刀**：`OSC 52;<Pc>[;<Pc>…;<Pd>]` 用 `osc::split_last(args, U';')`，`52;c;p;?` 与 `52;?` 都落进「读查询」那一支，Pc 段现在被**丢掉**（回写恒 `c`） | `src/term/terminal.cpp`、`src/term/osc.h` |
| **G4** | **base64 只有解码件、没有编码件**：`osc::decode_base64(std::u32string_view) -> optional<u32string>` 存在且容忍缺 `=` 填充；全仓另一处 base64 是 `conn::key_model` 的**解码**（公钥 blob 用），无编码可复用 | `src/term/osc.h`、`src/conn/key_model.cpp` |
| **G5** | **回写接缝是现成的、且是取走型**：`ResponseSink::on_response(u32string_view)` 只在持网格锁的 `feed` 上下文里被回调，实现方只能登记；`Session::on_response` 追加进 `pending_responses_`，`ingest` 出锁后 `flush(responses)` 编码成字节交连接 | `include/borealis/term/terminal.h`、`src/session/session.cpp`（`ingest` 出锁后 flush 那一腿） |
| **G6** | **剪贴板 IO 全仓只有一个落点，且只能主线程**：`ClipboardOutbox` 的 `drain` / `write` / `read` 三条都注明「只可在主线程调用」；`read()` 在失败与非文本时**回空串**，调用方按「什么都不发」处置 | `include/borealis/session/clipboard_outbox.h`、`src/session/clipboard_outbox.cpp` |
| **G7** | **写方向的「锁内留存、主线程取走」形状可直接照抄**：`clipboard_write_` 是 `optional<u32string>`，`take_clipboard_write()` 取走即清空，同一段输入的多次写**合并成最后一份**；装配层每帧对**全部标签的每个会话**排一次 `outbox.drain` | `include/borealis/term/terminal.h`（`clipboard_write_` 那份成员）、`src/main.cpp` |
| **G8** | **Aurora 的剪贴板只有一格，没有选择位**：POSIX 腿的实现是外壳转调 `xsel --clipboard` / `xclip -selection clipboard`（读也是 `--clipboard`），PRIMARY / SECONDARY 与 `0`–`7` 数组剪贴板在框架侧无处可取；Win32 腿走 `OpenClipboard`。**已现场坐实**：`strace -f -e trace=execve` 跑 `etest_osc_clipboard` 只出现四种串——`xsel --clipboard --output` / `xsel --clipboard --input` / `xclip -selection clipboard -o` / `xclip -selection clipboard`，**没有一条带 `-selection primary`** | `../aurora/src/aurora/app/clipboard.cpp`（POSIX 腿读写那两条命令串；本仓经 `src/session/clipboard_outbox.cpp` 单点消费）；本稿 §8 的 strace 读数 |
| **G9** | **一次剪贴板读是子进程往返，且这台机器上桥不在位**：POSIX 腿的 `get_text()` 是 `read_from_command("xsel --clipboard --output")`——fork + 管道 + 等待，不是内存拷贝。**本沙箱实测**：`xsel` 与 `xclip` **都不存在**（两者均 `execve` 成功而 shell 回 **127**，`clipboard_outbox.cpp` 那条 WRN 把两份 32512 原样打出来），故 `etest_osc_clipboard` 那一条绿的是**「缺桥时断可判的那三条腿」**那一档（G9 的显式探测口径），**不是**「内容真进了系统剪贴板」；一次「命令不存在」的读尝试成本＝两次 `sh -c` 往返，实测 **0.5–0.9 ms/次**（20 次均值，`true` 同法 0.75 ms 作对照）。**有桥时的耗时与 4 MiB 内容的线性度本沙箱无法测**（走查第 ⒋ 条） | `../aurora/src/aurora/app/clipboard.cpp`（`read_from_command` 那一处）；`tests/e2e/etest_osc_clipboard.cpp`（桥可用性的**显式探测**口径）；本稿 §8 的计时与 strace 读数 |
| **G10** | **三态枚举键的四处形态有现成同族**：以 `terminal.right_click` 为样本——枚举定义在绘制侧（`ui::RightClickAction`，`config` 侧是别名）、落盘名表在 `src/config/schema_names.h`、读写两处 `put_enum` / `scope.enumerated`（在 `src/config/store.cpp`）、表单搬运一枚 `Binding`（`form_transfer.cpp`）、目录行 `dropdown(key, page, {三个落盘名}, Wired, Immediate)`（`settings_catalog.cpp`）、中文标签一行（`settings_i18n.cpp`） | 逐条见左 |
| **G11** | **反向核对表是双向闸**：`utest_settings_catalog` 钉「写出的 schema 键 = 表里的行数」＝**64**（会话日志棒刚把 63 改成 64）、`Absent` 行数＝**19**、`Wired ∧ NextSession` 名单是一份显式列表——新键若标 `Wired ∧ Immediate` 则只动**总数**一处计数，不进名单。**批 3-A 落地后总数＝65**，另两条计数不动。**就地更正（同日稍晚，裁决 7.119）**：INTERACT.05 那棒又往终端页加了三行（`link_osc8` / `link_heuristic` / `link_schemes`），现值总数＝**68**、`Absent` 仍 19，另 `kNonPreferenceLeaves` 因 `connection.trusted_url_hosts` 由 4→**5** | `tests/unit/utest_settings_catalog.cpp`（F8 同源那三条断言，地基读数 64／19 → 现值 65／19） |
| **G12** | **状态栏本体尚未落地**：`appearance.status_bar.show_clipboard_policy`（「OSC 52 剪贴板授权态」那一格的**显示位**）在 schema／落盘／表单／词条四处齐全，而 `src/ui/` 里没有任何状态栏绘制件读它（裁决 7.68③ 的「只落盘不广播」档）⇒「授权态上屏」在本棒**没有落点** | `include/borealis/config/settings.h`、`grep -l status_bar src/` 只命中 config 与 settings 侧四件 |
| **G13** | **构造期注入与运行期改档是两条既有先例，取向相反**：`TerminalDefaults`（三条构造期档）明写「运行期改动**不重放既有会话**」；而 `Terminal::set_ambiguous_width` / `set_response_sink` 证明运行期入口也存在。差别在**语义**：Ambiguous 改档重排已上屏格宽是错的，安全闸改档不立即生效是错的 | `include/borealis/term/terminal.h`（`set_ambiguous_width` 与 `set_response_sink` 两枚运行期入口） |
| **G14** | **询问框的既有形状**：多行粘贴警告是**异步 answer 回调**形态（`std::function<void(PastePlan, function<void(bool)>)>` 挂在 `TerminalView::Presentation`，留空即回落模态框），且「没有宿主就问不了 ⇒ 未经确认不替用户按下回车」；`au::confirm(title, message, on_result)` 只吃**一条** message | `src/ui/terminal_view.cpp`、`src/ui/terminal_view.h` |
| **G15** | **装配层的设置广播只到「当前标签」**：`hooks.broadcast` 里折算 `next` 之后只对 `current_tab_id` 那个工作区的每个 pane `apply_appearance` / `apply_interaction_options`——**其它标签的在途会话拿不到改档** | `src/main.cpp` |
| **G16** | 既有判据不被本棒触及：`52;c;` 空载荷是**合法的清空请求**（写方向），`52;c;!!!!` 字母表外载荷不写剪贴板，多段编号只留最后一段载荷——三条都在 `utest_terminal_osc` 里有用例名 | `tests/unit/utest_terminal_osc.cpp` |
| **G17** | **调试面板没有 OSC 族那一行**：`SPEC.NF.RELI.01` 的上屏落点是 F12 浮层 `DebugPanel`，每会话渲染**四族**计数（parse / decode / encode / queue），`DebugSessionSnapshot` 里**没有任何 OSC 字段**——既有 `clipboard_write_requests` 与 `unhandled_count` 从来就只活在 `OscState` 里，可由用例断言、但不上屏。⇒「计数进可观测面板」这句话在仓里**没有落点**，本棒的降级留痕只能落在诊断日志，面板补那一行按 §7 在册 | `src/ui/debug_panel.h`、`src/ui/debug_panel.cpp`（四行一族）、`tests/integration/itest_debug_panel.cpp`（行数为那份快照的契约） |

**读数口径**：G1–G5、G7–G17 是本沙箱一手（读源码 + 跑既有用例）；Win32 腿的 `OpenClipboard` 语义与 0600 那类平台差异未经验证，凡引用者标「推断／未验证」（AGENTS §4.2 第 11 条）。

---

## 1 范围与边界

**本棒交付**（六件，全部对着 §2 的判据）：

1. 三态策略入 schema：`terminal.clipboard_read`，落盘名 `deny` / `allow` / `ask`，**缺省 `deny`**（判据 ①②）。
2. `term` 层一份**纯逻辑件**：`ClipboardReadPolicy` 枚举 + 待答读请求的登记/取走 + `encode_base64`（G4 的缺件），无 IO、可全量单测（AGENTS §4.4 第 20 条）。
3. 状态机的三态分派（D2／D3）：`deny` ⇒ 立刻回写空响应（现状）；`ask` ⇒ 登记待询问；`allow` ⇒ 登记待答。
4. 主线程的应答腿（D3）：`ClipboardOutbox` 取走待答请求，读剪贴板、`encode_base64`、按会话回写 `OSC 52 ; <Pc> ; <base64> ST`；超限与读不到都按拒绝档回写空响应（判据 ③）。
5. 询问的界面腿（D5／D6）：一条异步 answer 钩子（照 G14 的 `confirm_multiline` 形态），任何出口都必回写一条。
6. 设置页终端页那一行 + 该行上方的旁注 + 词条 + `utest_settings_catalog` 计数改口（G10 的四处 + G11 的一处）。

**前提与不做**（详见 §7）：

- **写方向不动**：`TERM.07` 已落、`SPEC.FEAT.CONN.12` 那句「写方向默认允许」已在（G16 那三条用例继续成立）。
- **选择位（`s`/`p`/`0`–`7`）不分别服务**：框架只有一格剪贴板（G8），本棒只回显 Pc 段、内容恒取系统剪贴板。
- **授权态上屏不做**：状态栏本体未落地（G12），那一格显示位继续停在「只落盘」档。
- **「本会话总是允许」的记住、按档案记住、授权审计列表**都不在本期（D5 的代价段）。
- **OSC 52 之外的其它「远端索取本地资源」不纳入同一闸门**（如 OSC 52 的 query 变体之外的 CSI 查询），本棒一把闸只管这一条。

---

## 2 草图与判据对应

草图共四屏（A 设置页那一行 + 旁注 / B 三态分派与询问框 / C 三档降级的可观测形态 / D 批次·证人·在册），1560 × 2290，`convert -background white … -density 96` 出 1× PNG。**一条画图纪律**：本沙箱的 SVG 光栅化器（ImageMagick 内置 MSVG）用的等宽字体**没有中文字形**，`font-family="monospace"` 的行里一旦掺中文就整段丢字 ⇒ 图中 mono 行只放 ASCII（VT 序列、符号名、日志原文），中文一律走默认字体。

屏 A＝设置页终端页那一条下拉（三档候选按落盘名显示，缺省 `deny`）＋该行上方的旁注；屏 B＝`ask` 档弹出的询问框（要说清「哪个远端在要什么」与「不给会怎样」）；屏 C＝超限 / 读不到 / 未答 三种降级的**可观测形态**——左边那条诊断日志是本期唯一真实落点，右边调试面板画的是**本期没有**的 OSC 族那一行（G17：面板四族里从来不含 OSC，`clipboard_read_*` 两枚计数只活在 `OscState`，§7 在册）。

| 圈号 | 判据（需求原文那一句） | 草图上的位置 | 落地的证人 |
| --- | --- | --- | --- |
| 1 | 默认禁止 | 屏 A 下拉的缺省选中位 | `utest_config` 缺省档 + `utest_terminal_osc` 的 deny 分派 |
| 2 | 三态可配、可开启 | 屏 A 三个候选 | `utest_settings_catalog`（行数 +1）+ `utest_form_transfer`（该键往返）|
| 3 | 被拒 ⇒ 回写空响应而非静默 | 屏 B 的「拒绝」按钮与屏 C 的降级三档 | `utest_session`（既有那条 G2 扩成三态矩阵）+ `itest_clipboard_read` |
| 4 | 允许 ⇒ 真的把内容给出去（base64 且逐字节可还原） | 屏 B 的「允许」按钮 | `utest_term_clipboard_read`（`encode_base64` 逐档 + 与解码件往返互逆，D11 已并件）+ `itest_clipboard_read`（经替身剪贴板断回写串） |
| 5 | 询问档不挂起：任何出口都回写 | 屏 B 的「拒绝」按钮与**框一直开着**那一档（批 2 实测：框架的 `Dialog` 没有关闭叉、遮罩点击不触发 `on_close` ⇒ 「不答」的出口是视口析构，不是关框） | `itest_clipboard_read` 的「答否 ⇒ 空响应」「一直不答，析构 ⇒ 空响应」「问不出去 ⇒ 也回写」 |
| 6 | 本地资源不被无限抽走 | 屏 C 的上限留痕 | `utest_term_clipboard_read` 的满档当场回绝档 |

---

## 3 裁决（D1–D12；**人已授权按推荐档自决**，收口即入册 7.118）

| 号 | 待定问题 | 候选 | 推荐＝收口 | 代价与理由 |
| --- | --- | --- | --- | --- |
| **D1** | 键放哪个域 | ① `terminal.clipboard_read`（终端页）；② `connection.clipboard_read`（连接页）；③ 把 `appearance.status_bar.show_clipboard_policy` 那格扩成三态 | **①** | 读它的是 `term` 层的状态机（G1），同族的交互三态（`right_click`、`paste_newlines`）都在终端页（G10），而 `UI_SETTINGS` 稿的脚注位置也写的就是终端页。② 会让 `term`/`session` 反向依赖连接域；③ 把「显示位」当「授权档」用，是把两格语义并成一层。**代价**：需求标识属连接域（CONN.12）而键在 `terminal` 域——这条「标识域 ≠ schema 域」的先例已有（`terminal.encoding` 同时服务串口/SSH/Telnet），在裁决与核对表注释里写明即可 |
| **D2** | 改档什么时候生效 | ① **现读**（改档即刻对全部在途会话生效）；② `NextSession`（照 `TerminalDefaults` 的构造期快照） | **①** | 这一档关掉的是「远端能不能读你的剪贴板」。② 的后果是用户在 A 标签点了「禁止」而 B 标签的远端照样能读——那不是延迟生效，是安全洞；而 G15 证明装配层的广播**本来就只到当前标签**，走② 还得先把广播扩到全部标签（一处既有语义的改动）。① 的落法不新增广播：闸门是一份**跨会话共享的原子值**，状态机在 `apply_clipboard` 里现读（读一枚 atomic 不是 IO，不违反 §3.4）。**代价**：`term` 层多一枚非持有指针（先例是 `set_response_sink` 的同款裸观察指针，生命周期由装配层保证）；未接线即 fail-closed 按 `deny`——这正是想要的失败方向 |
| **D3** | 谁去读剪贴板、在哪儿回写 | ① 状态机只**登记**待答请求，主线程帧边界经 `ClipboardOutbox` 读＋编码＋`Session::send_text` 回写；② 状态机直接读（锁内 IO） | **①** | ② 直接违反架构 §3.4 与 AGENTS §4.5 第 25 条，而 G9 说这次读是**一次子进程往返**——把它放进网格锁等于让一次 `xsel` 阻塞该会话的解码与另一线程的取网格。① 与写方向同构（G7），装配层已有的「每帧对全部标签每个会话排一次 `outbox`」那条腿就是它的位置（G7）。**代价**：应答延后到下一帧（本仓帧口径 16 ms 量级），对「等回应的远端程序」而言无感 |
| **D4** | 同一帧内多条读请求怎么办 | ① 逐条答（按登记序回写，队列有界，满档那一条**当场回空响应**并计数）；② 合并成最后一份（照写方向 `take_clipboard_write`） | **①**，上限 `kMaxClipboardReadRequests = 8`；**实现后改口**：原写「超出丢最旧并计数」，与判据 ③「被拒 ⇒ 有回写」直接冲突（悄悄丢掉＝让先问的那条远端一直等），故满档走拒绝路径而非淘汰路径 写方向能合并是因为它落的是**本地资源**（同一格剪贴板，后写覆盖前写）；读方向是**别人在等自己的那一条**——`52;c;?` 与 `52;p;?` 问的不是同一格（G8 之后我们答的是同一份内容，但 Pc 段回显不同），合并就是让先问的那条永远收不到回应。8 条上限定的是「远端拿一条输出把我们的写腿喷满」的下限。**代价**：`term` 层要一张小队列而不是 `optional` |
| **D5** | `ask` 档的界面形态 | ① 视口内非模态卡（照 TERM.09 的一次性提示卡）；② 模态 `au::confirm` 浮层，逐条一问；③ 全局一问 +「记住」写回设置 | **②** | ② 的异步 answer 形状在仓里已有先例（G14 的 `confirm_multiline`：留空即回落默认、可脱界面用替身断言），而本判据要的正是「不答就什么都不能发生」。① 的非模态卡服务的是「通知」（TERM.09 那次），授权决定不该在用户没看时被下一次点击顺带吃掉。③ 的「记住」是跨会话状态，本期没有它的判据（`§7` 在册）。**代价**：模态一问＝该窗口内其它操作在用户点之前停住；远端连发多条时会话级最多问 8 次（D4 的上限同时是骚扰上限） |
| **D6** | 未答／关框／超时怎么兜底 | ① 一直等到用户答（**任何出口**都回写一条，关框＝拒绝档空响应）；② 5 s 超时按拒绝回写（照 TERM.09 的自收档）；③ 不回写，静等 | **①**，**实现后改口**：「任何出口」按框架实测收窄成三条——是／否／**一直不答**（`au::Dialog` 没有关闭叉，遮罩点击不触发 `on_close`，Aurora 主仓 `widget/dialog.h` 的 `dialog_geom` 段 ⇒ 稿面那句「关框＝拒绝档空响应」在这一枚控件上**没有对应的用户动作**）；而「一直不答」那份欠账的兜底点是**视口析构**而不是「会话析构无需补写」：实测析构次序是视口先、会话后（`Session` 必须先声明才最后析构），所以视口析构时会话仍在世、回写得住 3 案 | 需求判据 ③ 的原话是「避免远端程序挂起」——真正的挂起风险在「问完就把这一条吞了」（③ 档），而不是「多等一会儿」。② 的 5 s 对一次通知合适，对「要不要把剪贴板交给它」这种决定太短，而且会让「用户去泡了杯茶回来」变成一次静默拒绝＋远端拿到空内容，比弹窗骚扰更难诊断。**代价**：`ask` 档下远端的读请求会停在框前直到用户答——这是**授权语义本身**，写进草屏 B 的那句说明与走查第 ⑵ 条；代价的另一面是「关标签」成为唯一的反悔通道，故 §6 判据 ⑤ 把它单独立案 |
| **D7** | 回写内容有上限吗 | ① 有界：`kMaxClipboardResponseBytes = 64 KiB`（UTF-8 字节数），超限按拒绝档回写空响应 + 诊断留痕；② 不设限 | **①** | 剪贴板里可能是几 MB 的日志（复制一次 `cat` 输出就有），base64 再乘 4/3，一条 `send_text` 就是把这份东西喷进该会话的写腿；而远端只问了「给不给」，没承诺收多少。**代价**：超限那一档对用户是「点了允许却没内容」，必须留痕（屏 C 那一档：**只留一条 `AURORA_LOG_WARN`**——`OscState` 那两枚计数住在状态机侧的锁内，主线程的降级档没有安全的加锁落点，为它单开一条计数是半件抽象；调试面板那一行本期本就没有落点，G17 在册），措辞走词条；上限是文件级常量而非键（会话日志那棒的 D9① 同档取向：能不加键就不加） |
| **D8** | Pc 段怎么办 | ① 回显收到的**首段** Pc（无 Pc 段则回 `c`），内容恒取系统剪贴板；② 恒回 `c`（现状）；③ 按 `c`/`s`/`p`/`0-7` 分别取不同选择 | **①** | ③ 在框架侧无处可取（G8：只有一格 CLIPBOARD）——做得出界面但答不出内容，就是留了个「点了什么也不发生」的空位。② 会把 `52;p;?` 的问者答成 `c`，问 tmux 的缓冲区拿到的却是 X11 CLIPBOARD，这是**答错**而不是降级。① 的代价是「回显了选择位而内容同源」这件事本身仍有出入，因此要把这条限制写进头注与 §7 在册（不做选择位分离） |
| **D9** | 拒绝要不要上屏提示 | ① `deny` 档静默回空响应 + 计数进 `OscState`；② 每次拒绝弹一条提示 | **①** | ② 会被远端程序的安装脚本/`viminfo` 探测一类的常规查询打成弹窗骚扰，而「禁止」本就是用户显式选的缺省态（判据 ①）。计数进 `OscState`（与 `clipboard_write_requests`、`unhandled_count` 同族）够排障用，但要把话说准：**这一族计数本期不上屏**——`SPEC.NF.RELI.01` 的调试面板只渲染 parse/decode/encode/queue 四族，`DebugSessionSnapshot` 里没有任何 OSC 字段（G17），既有的 `clipboard_write_requests` 也一样只活在结构体里；`SPEC.NF.RELI.01` 那句「可观测」在本棒由**诊断日志 + 用例断言**承担，面板补 OSC 那一行按 §7 在册。**代价**：用户第一次遇到「tmux 复制没反应」时不会想到是这道闸，而且在屏幕上**查不到** ⇒ 靠设置页那一行上方的旁注赔付 |
| **D10** | 写方向纳入同一把闸吗 | ① 不纳入（本棒只管读）；② 扩成「读/写各一枚三态」 | **①** | 写方向「默认允许」是 `SPEC.FEAT.TERM.07` + 裁决 7.21③ 已落并有用例的既成口径（G16 三条），改它需要新需求裁决而不是顺手扩面（AGENTS §4.1 第 3 条）。**代价**：界面上那一行只说「读取」，措辞不能写成「剪贴板授权」这种涵盖写的样子——词条按 D10 收窄 |
| **D11** | 文件布局与批次 | — | 见 §4 | 三批切法：批 1＝`term` 层纯逻辑（枚举 + 闸门 + 待答队列 + `encode_base64`）与其 utest；批 2＝`Session`/`ClipboardOutbox` 的应答腿 + 询问钩子 + `itest`；批 3＝新键六处同步 + 面板行 + 装配接线 + 文档回写 |
| **D13** | 「问出去的那一条」的跨帧状态住在哪 | ① 装配层：帧循环排请求、经视口钩子问、答（§5 原案）；② 视口：`TerminalView` 在 `on_frame` 里排本会话的待答请求，问与答闭合在同一处 | **②（批 2 开工时改口）** | 判据 ③⑤「任何出口都回写一条」是一条**跨帧**承诺——模态一问要等用户点，而 `src/main.cpp` 不编进测试 runner（裁决 7.83），① 会把这条承诺切成「装配层持有待答清单 + 视口只出一张框」两半，最要紧的两条安全判据于是只剩真机走查一个证人。② 让整条链在 `itest_clipboard_read` 里以替身钩子逐档断字节，且「一直不答」那一档有了代码落点（照 `pending_paste_` 那条已落地先例，G14；批 2 实测这一档的落点是**视口析构**而非关框，见 D6 的改口）。**代价**：视口多一枚 `set_clipboard_gate` 观察指针与一份待问队列（上限由状态机侧那 8 条封住），并且**析构与关标签**都得替没答完的请求回写空响应，否则远端永远等——这一条写进 §6 的判据 ⑤。**口径不变**：视口不做「给不给」的判定，只把闸门那一档翻成「问不问 + 答什么」；读剪贴板仍只有 `ClipboardOutbox` 一处（架构 §3.2） |
| **D12** | 验证形态 | — | 三层：① `utest_term_clipboard_read`（三态分派 × Pc 回显 × 队列上限 × 超限 × `encode_base64` 逐档——base64 那半件的宿主文件就是 `src/term/clipboard_read.cpp`，不为一枚纯函数另立 TU）；② `itest_clipboard_read`（替身剪贴板经 `Presentation` 钩子注入 ⇒ 断回写串逐字节；批 2 落地为 17 案，含**析构撤账**、无宿主、超限／空内容／答否三档降级，以及**不用替身**的生产框命中链两案）；③ 真机走查（有桥机器、真 tmux/vim 发读请求、Win 腿——本机 `xsel`/`xclip` 双双不在位，只能走缺桥那一档，G9）。`etest_` 那条不新写 TU：`etest_osc_clipboard` 已把「桥可用性显式探测」的口径立住（G9），批 3-C 给它补**读方向现场腿两案**（真 shell 发 `ESC ] 52 ; c ; ? BEL` ⇒ 断我们从同一通道回写的字节）。**落地后改口一处**：稿面那句「Windows 腿投放条件不变」不成立——两案长在 `#if !defined(_WIN32)` 里，因为判据要的是**真 shell 往 tty 写、再从屏上收我们回写的字节**，而 POSIX 腿的 forkpty 那条通道在本仓有既有先例（`etest_local_terminal` 的整屏取串口径），Win32 侧没有等价的「真 shell 发 OSC 52 读请求」夹具 ⇒ Win32 读方向腿**未实现**，按 §7 在册而不是假装等价 | — |

---

## 4 文件布局 / TU 拆分（按推荐档收口后据此开工）

```
include/borealis/term/clipboard_read.h      新增 —— 纯逻辑件的契约（不含 Aurora 类型，架构 §2.3）：
                                                  `ClipboardReadPolicy{Deny, Allow, Ask}` 三态枚举（落盘名映射留 `config`，
                                                  与 `ui::RightClickAction` 的「唯一定义处在绘制/逻辑侧、config 取别名」
                                                  同一条先例，G10）；
                                                  `ClipboardReadRequest{std::u32string pc; }` 一条待答请求（G3：Pc 段
                                                  现在是丢掉的，本件把它留下，D8① 的回显要求）；
                                                  `ClipboardGate`（D2① 的现读闸门：一枚 `std::atomic<Policy>` +
                                                  `set()` / `get()`；**未接线即 Deny** 的 fail-closed 口径写进头注）；
                                                  `encode_base64(std::span<const std::byte>) -> std::string`
                                                  （**纯函数**，补 G4 缺的那半：与 `osc::decode_base64` 互逆、标准字母表、
                                                  含 `=` 填充）
src/term/clipboard_read.cpp                 新增 —— 同一件的落地腿：闸门读写 + `encode_base64`；待答队列住在 `Terminal`（D4：有界 8、满档当场回空响应并计数）
                                                  + `encode_base64`
include/borealis/term/osc.h                 修改 —— `OscState` 追加两枚计数：`clipboard_read_requests`、
                                                  `clipboard_read_denied`（D9①；与既有 `clipboard_write_requests` 同族，
                                                  而同族一起只活在结构体里——调试面板本期不加 OSC 那一行，G17 在册）
include/borealis/term/terminal.h            修改 —— `set_clipboard_gate(const ClipboardGate *)`（照 `set_response_sink`
                                                  的非持有观察指针形态）；私有成员 `clipboard_read_pending`（有界队列）；
                                                  `take_clipboard_reads()` 取走型动作（G7 同构，取走即清空）
src/term/terminal.cpp                       修改 —— `apply_clipboard` 的 `payload == "?"` 那一支换成三态分派（D2/D3/D8）：
                                                  闸缺省/`Deny` ⇒ 现有空响应原样回（判据 ①③ 的既有语义一字不动）；
                                                  `Allow` ⇒ 登记请求（Pc 回显留给应答腿）；`Ask` ⇒ 登记待询问；
                                                  同时 `++osc_state_.clipboard_read_requests`
include/borealis/session/session.h          修改 —— **批 2 改口：三枚运行期入口**（稿面初判「两枚」漏了一件事：
                                                  状态机那枚闸门是 `Terminal` 的私有观察指针，装配层手上只有
                                                  `Session`，不转发就永远接不上 ⇒ 漏接线即 `Deny` 那条 fail-closed
                                                  会误伤所有真实会话）。① `set_clipboard_gate(const ClipboardGate *)`
                                                  （转发给状态机，锁内赋值——读线程在 `feed` 期间现读它）；
                                                  ② `take_clipboard_reads()`（转发状态机那份队列，锁内取、锁外交）；
                                                  ③ `answer_clipboard_read(std::u32string_view response)`
                                                  （把「该答的那条 OSC 52 正文」交 `flush`，与读线程那批应答共用
                                                  **同一条**编码与行尾折算腿，不另开写腿）。**不挂 `pending_responses_`**：
                                                  那条缓冲只在读线程喂入时才被冲出去，而「用户点了允许」多半发生在
                                                  会话空闲时——挂进去就是让远端等下一次输出
src/session/session.cpp                     修改 —— 上述三枚的落地；取走与回写都不在锁内做 IO（§3.4），
                                                  锁只护住那个指针本身与那份队列（改档即刻生效靠的是闸门内部那枚
                                                  atomic，不是这条赋值）
include/borealis/session/clipboard_outbox.h 修改 —— `answer_read(Session &, const ClipboardReadRequest &,
                                                  std::optional<std::string_view> content)`：**一条一答**（D4① 逐条答
                                                  要求每条各有回写，故入口按条给而不是一批）。把「已经拿到手的剪贴板
                                                  文本」折算成应答串交回会话（**本件不读剪贴板也不弹窗**——读与问都在
                                                  调用方，落地件只做编码与形状，这样「拒绝/超限/内容为空」三档降级
                                                  能脱界面断言）；上限常量 `kMaxClipboardResponseBytes`（D7①）
src/session/clipboard_outbox.cpp            修改 —— 同一件实现：`encode_base64` 一次、拼 `ESC ] 52 ; <Pc> ; <b64> BEL`、
                                                  超限或空内容或 `content == nullopt` 都退回那条既有空响应（**逐字节
                                                  与现状那条相等**，判据 ①③ 的回归底线），超限另 `AURORA_LOG_WARN`
                                                  一条（D7① 的留痕只有这一处）。返回值是「是否给出了正文」，
                                                  调用方只用于断言与日志，不据它改回写形状。**降级档恒回显 `c`
                                                  而不回显请求的 Pc**（与状态机 `Deny` 那一路同源）：判据 ①③ 要的是
                                                  「与现状那条逐字节相等」，而现状就是恒 `c`——D8① 的回显只加在
                                                  **有正文**那一档，别让一条被拒的应答反过来破底线
src/ui/terminal_view.h                      修改 —— D13② 的宿主：`Presentation` 追加一枚异步询问钩子
                                                  `confirm_clipboard_read`（形态照同结构体里的 `confirm_multiline`，
                                                  G14：留空即回落默认）；`set_clipboard_gate(const ClipboardGate *)`
                                                  （与视口既有的 `set_restart_hook`/`set_logging_hooks` 同一套装配接缝）；
                                                  **批 2 补**只读句柄 `clipboard_read_dialog()`（理由同既有的
                                                  `multiline_warning()`：按钮落点要经真实命中链才知道是哪一枚，
                                                  「答话即收起、收起不摘浮层」除本句柄无处可读）＋ 四件私有账本
                                                  （闸门观察指针 / 待问队列 / 在问的那一条 / 懒建对话框）
src/ui/terminal_view.cpp                    修改 —— `on_frame` 尾部 `drain_clipboard_reads()`（D13②）：取走状态机
                                                  那份队列**只并入**待问队列，分流全在唯一入口 `ask_clipboard_read()`
                                                  ——因为「按闸门现档处置」有两个触发点（帧边界、上一条答完之后），
                                                  而三档都必有一条回写，没有「先收下以后再说」的第四档。
                                                  批 2 实现后的口径改口（三处与稿面初判不同）：
                                                  ① 现读闸门的**唯一**读取点就是 `ask_clipboard_read()`，`Deny` /
                                                     闸门摘除 ⇒ 待问队列**整批当场拒**、`Allow` ⇒ **整批直接读并答掉**
                                                     （稿面初判是「逐条」，实测整批更贴 D2①：改档之后不该再问，
                                                     也不该一帧一条慢慢放）；
                                                  ② `Ask` 一次一问（D5②）：在问的那一条占住账本，答完**不在回调里
                                                     递归再问**，改为 `mark_needs_paint()` 排下一帧——递归会经由
                                                     `Dialog::set_content` 把当前还在栈上的那份内容（本次回调的宿主）
                                                     拆掉，实测正是这一条把「一次一问」的证人打成段错误（§5 纪律 ⑤）；
                                                  ③ 默认问框是模态 `au::Dialog` + `aurora::confirm`（标题/正文走词条），
                                                     出口只有**是/否两枚按钮**：框架实测遮罩点击不触发 `on_close`
                                                     （Aurora 主仓 `widget/dialog.h`），稿面初判那句「是/否/关框/遮罩四个
                                                     出口都答一次」因此改口——「一直不答」是常态而非边角，那份欠账由
                                                     **视口析构**兜底：在问的那一条与待问队列里的每一条**各**补一条
                                                     空响应（D6① 的实际形态）。无宿主可问也当场按拒绝答（与粘贴那一路
                                                     的「不问就不发」相反：这一路问不出去也要回话）
include/borealis/config/settings.h          修改 —— `terminal` 域追加 `ClipboardReadPolicy clipboard_read{Deny}`
                                                  （判据 ① 的缺省禁止；`operator==` 默认逐字段比较照旧成立）
src/config/schema_names.h                   修改 —— `kClipboardReadNames` 三档落盘名表（`deny`/`allow`/`ask`，G10 同族）
src/config/store.cpp                        修改 —— `put_enum` 与 `scope.enumerated` 两处
src/config/form_transfer.cpp                修改 —— 一枚 `Binding`
src/ui/settings_catalog.cpp                 修改 —— `dropdown("terminal.clipboard_read", SettingsPage::Terminal,
                                                  {"deny","allow","ask"}, Wired, Immediate)` 一行（D2① ⇒ **Immediate**，
                                                  不进 `NextSession` 名单），行序紧随 `terminal.right_click`——同族的
                                                  交互三态两条挨着，终端页其余行序不动
src/ui/settings_i18n.cpp                    修改 —— 该行标签、该行上方的旁注一句、询问框两句（标题 + 正文）、
                                                  超限那一档的回报句；`UI_SETTINGS` 稿脚注那句「随该条落期入 schema」
                                                  自此改口为既成事实。**批 2 只落了询问框那两句**（`terminal.clipboard_read_confirm.title`
                                                  / `.body`，正文带 `{0}` 占位填 Pc）——没有词条视口就没有默认问框可弹，
                                                  而设置页那一行的标签与旁注留批 3 与键同步入表（同一意图一处落）。
                                                  **实现后改口（批 3 实测）**：稿面初判那句「三档候选的中文」不成立——
                                                  面板的下拉直接把 `control.choices`（落盘名）交给 `aurora::Dropdown`
                                                  （`src/ui/settings_panel.cpp` 造控件那一处），`terminal.right_click`
                                                  与 `terminal.paste_newlines` 同样是英文候选 ⇒ 本行只补**行标签 + 旁注**
                                                  两句词条，候选维持落盘名。这不是本棒省下来的口子，而是全仓既成事实，
                                                  要做候选本地化得另开一条 UI 判据（§7 在册）。
                                                  正文那一句自带「拒绝会回送一条空响应，远端不会挂起」——不是修辞：
                                                  裁决 D5 要框里说清「不给会怎样」，而批 2 实测这枚 `Dialog` 没有
                                                  关闭叉、遮罩不答 ⇒ 用户能读到的说明只剩这一句（证人：
                                                  `the_question_strings_exist_in_the_setting_table` 按 key 断在表内，
                                                  并断 `{0}` 填进去之后槽位真的换掉了）
src/ui/settings_panel.cpp                   修改 —— `kGroupNotes` 表追加一行：旁注画在**该行自己上方**而不是
                                                  终端页组顶（这一行只罩它自己那一档，挂组顶会把三档后果说成整组的事）；D9 的赔付处
src/main.cpp                                修改 —— 装配（D13② 之后只剩接线）：一枚 app 级 `ClipboardGate`
                                                  （紧挨 `store` 声明 ⇒ 几乎最后析构，会话线程现读它时结构上
                                                  摸不到已消散的对象；初值从已落盘那一档折算）、**每会话
                                                  两接**：`Session::set_clipboard_gate(&gate)`（状态机侧那档分派）＋
                                                  视口 `set_clipboard_gate(&gate)`（帧边界的现读）——同一个对象两个
                                                  持有者，这正是裁决 D13② 说的「一枚闸门同时决定登记与询问」。
                                                  八处构造点**一处不漏**，漏即 fail-closed（漏会话那一接
                                                  更是「查询当场被吞、界面永远不问」）。三处建壳闭包各自覆盖
                                                  全部八处：`create_new_tab_with`（新开格，含本地/SSH/串口/Telnet
                                                  四腿）、`replace_tab_session`（重启腿，含同 TabId 覆盖）、
                                                  `undo_close_tab` 内联那一份（撤销重开旁路，不经前两处 ⇒
                                                  「一处不漏」说的就是这类旁路）。**实现后改口（批 3-B 实测）**：
                                                  ①稿面那句「`hooks.persist` 之后刷新」不够——能改动这一档的入口
                                                  有三条（面板保存、快照回滚、配置导入），后两条整份换掉 `Settings`
                                                  而不走 `persist`，只接一条就会出现「回滚后屏上按新档、闸门还停
                                                  旧档」；落地是一份 `sync_clipboard_gate` 小闭包，三处各调一次。
                                                  ②刷新跟着**内存值**走、不跟落盘成败走：`Store::replace()` 的口径
                                                  是「内存值总是更新、只有落盘可能失败」（`SPEC.FEAT.PREF.07`），
                                                  广播腿读的也是同一份内存值 ⇒ 以「写盘成功」为条件就与广播分叉。
                                                  ③**预览腿刻意不接**（稿面原先把它列进「一处不漏」）：预览那台
                                                  会话由 `SettingsPreview` 在面板内部自建自持，装配层拿不到它的
                                                  会话与视口，而唯一字节源是 `preview_fixture()` 那串常量——里面
                                                  没有 OSC 52 查询，结构上收不到远端请求。接上去反而更坏：视口
                                                  没接 `presentation_` 替身，读的是**真系统剪贴板**，`allow` 档就会
                                                  在帧路径上碰剪贴板（AGENTS §4.5 第 25 条），`ask` 档更会在面板
                                                  里再叠一枚模态框。不接 ⇒ 该视口的 `clipboard_gate_` 空 ⇒ `Deny`，
                                                  fail-closed 的方向正是「读不到」。
                                                  排请求与问答腿都在视口
                                                  `on_frame` 里，装配层不再自持跨帧的待答清单 ⇒ **`perform_close`
                                                  那一路不需要另写撤账**：视口析构就是那条回写的落点（批 2 已把
                                                  这一档做成 itest 的自动证人）。**批 3 有一条承重的析构次序纪律**
                                                  （批 2 实测所得）：`TabWorkspace` 的成员原先是 `workspace` 先、
                                                  `sessions` 后 ⇒ `sessions` 反而**先析构**，眼下不崩全靠
                                                  `WorkspaceView::hooks_` 声明在最前（故最后析构）而 `make_pane` /
                                                  `dispatch_grid` 两枚闭包**按值**捕获了 `shared_ptr<Session>`，
                                                  替视口把会话撑到 `~TerminalView` 之后。批 2 起「视口析构要往会话
                                                  写撤账空响应」，这条撑住关系从多余变成**承重**：把那份值捕获换成
                                                  裸指针（看着更像非拥有接缝）就是 use-after-free，而 itest 换不到
                                                  证人（驱动台的次序是它自己的成员）。落地形态：`sessions` 提到
                                                  `workspace` 之前声明（三处 `TabWorkspace{...}` 的指定初始化符
                                                  随之换序，C++20 要求成员次序一致），两枚闭包的捕获处留一句 WHY；
                                                  次序修正后那层值捕获退居第二道保险，注释写明不许再摘
src/CMakeLists.txt                          修改 —— `src/term/clipboard_read.cpp` 进 `borealis_core` 的**显式**源清单
                                                  （只有 tests 走 GLOB；漏这一行即链接期 undefined reference，
                                                  Telnet 批 2 与会话日志批 1 的同一处坑）
tests/unit/utest_term_clipboard_read.cpp     新增 —— 三态分派逐档 × Pc 回显（含 `52;c;p;?` 与 `52;?` 两档，G3）×
                                                  队列上限（第 9 条当场回空响应、前 8 条留队）× 闸门未接线即 Deny ×
                                                  `encode_base64` 逐档（空、1/2/3 字节余数、CJK 字节序、与
                                                  `osc::decode_base64` 往返互逆）
tests/unit/utest_terminal_osc.cpp            不改 —— 既有那条「读查询回空响应」在批 1 后逐字节保持绿，它就是
                                                  D9① 的回归底线；两枚新计数的断言归新 TU，不在旧 TU 重述
tests/unit/utest_session.cpp                 修改 —— G2 那条既有用例保持（Deny 档，逐字节不变 ⇒ 判据 ① 的底线），
                                                  新增两条：`allowed_osc_52_reads_are_queued_and_answered_on_the_same_channel`
                                                  （Allow 档接线后两条查询留队、取走即清、`answer_clipboard_read` 走
                                                  **同一条写通道**）与 `the_outbox_turns_one_read_request_into_exactly_one_answer`
                                                  （`answer_read` 的四档字节：正文 / `nullopt` / 空串 / 超 64 KiB ⇒
                                                  后三者逐字节同那条既有空响应且返回 false）
tests/integration/itest_clipboard_read.cpp   新增 —— 替身剪贴板 + 替身确认钩子接进 `Presentation`（G14 的接缝就是为
                                                  这类用例留的）⇒ 无头断回写串逐字节。共 **17 案**，按六桶组织（案名见 §6）：
                                                  ① Allow 一帧内问到并答出正文；② 一次一问 + 答完的下一条排在下一帧；
                                                  ③ 三个「必须有回写」的出口（答否 / 视口析构 / 无宿主问不出去）；
                                                  ④ 闸门现读（登记之后改档 ⇒ 整批按**现档**处置，不再问）；
                                                  ⑤ 生产模态框的 Yes/No 经**真实命中链**派发（不靠替身）；
                                                  ⑥ 三档降级（超限 / 剪贴板空 / 答否）与两句词条在表内。
                                                  **驱动台的两条析构纪律**（本棒实测所得，写死在文件头）：
                                                  `Session` 必须**先声明**（最后析构）而 `au::Window` 排在视口/宿主/Node
                                                  之后——`~Window` 释放它对节点树的引用，那份引用可能是 `TerminalView`
                                                  的最后一个持有者，次序反过来就是「视口析构往已死会话回写」的段错误；
                                                  写出的字节记在**用例持有**的记账本里（`FakeConnection` 收一枚
                                                  `std::vector<std::byte> *sink`），否则「析构之后读数」这件事无从成立
tests/unit/utest_config.cpp                  修改 —— 新键的往返等值 + 「缺省 deny」一档（判据 ①，批 3）。
                                                  **落地形态**：`non_default()` 的 terminal 段取 `Ask`（离缺省最远那一档，
                                                  写侧漏键与读侧把缺省当显式值回落都判得出），往返那案由既有的
                                                  **整结构逐字段等值**自动吃进这一格——于是 M1 那种「写侧漏一键」
                                                  一次点亮六条用例（`every_value_survives_a_write_read_round_trip`
                                                  与四条同样以 `non_default()` 为素材的用例）；缺省档由
                                                  `defaults_are_the_first_launch_shape` 逐字锁 `Deny`
tests/unit/utest_form_transfer.cpp           修改 —— 该键的 `Binding` 断言（G10 同族那条枚举搬运写法）：
                                                  `made_up()` 里取 `Ask`、`each_key_takes_the_member_it_names`
                                                  加一句字面量 `expect("terminal.clipboard_read", text("ask"))`。
                                                  真正守三档落盘名的是既有的
                                                  `every_declared_choice_survives_the_config_file`：它按 catalog 的
                                                  `Choice` 行逐档「提交 ⇒ 写盘 ⇒ 读回 ⇒ 再装载」，与本件共用
                                                  `schema_names.h` 那一张表 ⇒ 名表单独改一个字就在这里现形（M2）
tests/unit/utest_settings_catalog.cpp        修改 —— G11 的计数改口：总数 64→**65**；`Absent` 19 不变；
                                                  `Wired ∧ NextSession` 名单不变（该键是 Immediate）。
                                                  另两条既有用例自动接管本行：`table_covers_exactly_the_written_schema_keys`
                                                  锁「写出的 schema 键 = 表里的行」（M1 漏写侧即红），
                                                  `choice_rows_declare_the_whitelists_the_loader_enforces`
                                                  锁「表里声明的候选 = 读侧白名单」（M2 名表分叉即红），
                                                  `grade_columns_match_the_two_physical_boundaries`
                                                  锁「`Wired ∧ Immediate` 的档位划线」（M3 把该行降为 NextSession 即红）
tests/integration/itest_settings_panel.cpp   修改 —— 既有 `a_wired_immediate_change_persists_once_and_broadcasts_once`
                                                  尾部加第二段：提交 `ask` ⇒ 落盘一次、广播一次、`apply_scope`
                                                  为 `PersistAndApplyNow`。这一行的**存在性**与**次序**由该件两条
                                                  通用用例自动覆盖（`every_catalog_key_resolves_to_a_display_label`
                                                  查词条、`the_page_row_table_is_the_catalog_intersection_in_order`
                                                  查画得出且行序＝表序），故本棒不为它单开新案；
                                                  角标为空由那条逐行 `expected_badge` 通用断言锁，不重复写死
                                                  **两处被本棒打红的既有债（都在 `itest_settings_panel`）**：
                                                  ① `a_row_control_still_commits_after_the_row_area_has_been_scrolled`
                                                  与 ② `the_editable_controls_paint_the_chrome_colors_not_the_light_defaults`
                                                  都把「往下滚 8 档＝128 dp」写成「终端页那四枚关闭态开关进带」的手段，
                                                  而本棒新增的一行下拉 + 那一行上方的旁注（实测把旁注撑到四行）改了
                                                  内容高度 ⇒ ① 在 `spot.widget != nullptr` 转红。改法不是补一档常数，
                                                  而是**逐档滚到找着为止**（上限 40 档只防「滚到底也没有」那种真回归），
                                                  与该件文件头自己写的口径一致：「只认滚完之后确实命中一枚关闭态开关，
                                                  不去赌排版算出来的偏移」。② 红在另一处：它的下拉腿取的是**滚动之后**
                                                  第一枚可达 `Dropdown` 的框内分数 `(0.5, 0.12)`，而 `find_first` 量出的
                                                  可达框是**被视口裁过**的那一段——可见框只剩 17 dp 时同一个分数落到
                                                  标签字形上，实测读数 `{243,243,238}` 恰是本件文本色（红得对，但判的
                                                  已经不是 chrome）⇒ 把这一腿移到**滚之前**（offset 0 时终端页第 2 行
                                                  那枚下拉完整可见）。两处的共同教训：**像素与命中类判据的取点，
                                                  一旦挂在「滚了几档」上就是排版的函数**，登记在册
tests/e2e/etest_osc_clipboard.cpp            修改 —— 补**读方向**现场腿两案（`#if !defined(_WIN32)`：真 shell 往 tty 发
                                                  `ESC ] 52 ; c ; ? BEL` ⇒ 断我们从同一通道回写、并在屏上收字节的转义串）。
                                                  Allow 案经 `ClipboardOutbox::read()` 取走请求、交 `answer_read` 应答，并把
                                                  「应答返回值＝桥可用性」断成现场事实（G9 的显式探测口径不变，缺桥时断可判的
                                                  那几条腿而非跳过）；Deny 案断判据 ③——被拒不静默，屏上等到那条空响应。
                                                  等串靠 `stty raw -echo min 0 time 20`（VMIN/VTIME）+ 裸 `dd bs=1 count=64`，
                                                  **不是 GNU `timeout`**：后者把被管命令放进独立进程组，组内读 tty 属后台读、
                                                  直接 SIGTTIN 停住（§8 探针读数）；回写的控制字符经 `sed` 折成 `[ESC]`/`[BEL]`
                                                  字面量，否则原样打印会被自家状态机再吃一遍
```

> 不改既有公共头的可见性与既有成员顺序（AGENTS §4.1 第 6 条）；`Terminal` 多一枚观察指针、`OscState` 多两枚计数属**新增**而非改形。新键 `terminal.clipboard_read` 走的是 G10 那四处 + G11 那一处计数的既有全套，不是顺手扩面。框架侧不预登记新缺口：观察指针接缝（G5）、取走型动作（G7）、异步确认钩子（G14）、有界常量（会话日志 D9① 同档）全有已落地先例。

---

## 5 数据流与线程模型

```
远端 `ESC ] 52 ; c ; ? ST`
  ──连接读线程──► Session::ingest ──► 锁内 Terminal::feed ──► do_osc(52) ──► apply_clipboard("?")
                                                                     │
                                          现读 ClipboardGate（D2①，一枚 atomic，不是 IO）
                                            ├─ Deny / 未接线 ──► emit_response("\x1B]52;c;\x07")   （判据 ①③，现状）
                                            ├─ Allow ─────────► 登记 ClipboardReadRequest{pc}      （队列有界 8）
                                            └─ Ask ──────────► 同上，由主线程决定问不问
                          锁内只登记；应答文本经 ResponseSink → pending_responses_（G5）留存
  ──出锁──► flush(responses) ⇒ 连接                                  ← 空响应那一档在这里就走
                                     ‖
主线程帧边界（`app.set_on_frame` → 每 pane 的 `TerminalView::on_frame`，装配层对**全部标签每只视口**
排一次，G7 同一条腿；D13② ⇒ 排请求与问答闭合在视口里，装配层只接线）
  ├─ session.take_clipboard_reads() ⇒ 并入视口的待问队列（登记序即应答序，D4① 不合并）
  │     └─ `ask_clipboard_read()`：现读闸门 == Ask 且**手上没有在问的那一条**才弹一问（D5②，
  │            经 `Presentation::confirm_clipboard_read`，一次只问一条，其余留在待问队列）
  │            答是 → 读本地剪贴板（`ClipboardOutbox::read`，G9：一次子进程往返）→ 回写正文
  │            答否 → 回写空响应；下一条排在**下一帧**问（就地递归再问会走到 `set_content`，
  │            而它摘掉的正是当前还在栈上的那份对话框内容，含本次回调自身）
  │            无宿主（浮层宿主还没挂上）⇒ 问不出去也**当场按拒绝答**——粘贴那一路是「不问就不发」，
  │            这一路反过来：沉默等于让远端一直等
  │            视口析构 → 在问的那一条与待问队列里的每一条**各**补一条空响应（D6①：任何出口都不欠账；
  │            框架的 `Dialog` 没有关闭叉、遮罩不触发 `on_close` ⇒ 析构就是「不答」那一档的唯一兜底点）
  ├─ 现读闸门 == Allow ⇒ 不问，把待问队列整批直接读并答掉
  ├─ 现读闸门 == Deny / 闸门摘除 ⇒ 不问，把待问队列整批当场拒（登记之后才改档的那一批也照拒——
  │      D2① 现读的代价面），三条出口都必有一条回写
  ├─ ClipboardOutbox::answer_read(session, request, content)   ← 一条一答
  │     ├─ 有内容且 ≤ 64 KiB ⇒ "ESC ] 52 ; <回显的 Pc> ; <base64> BEL" → session.answer_clipboard_read
  │     └─ 拒绝 / 超限 / 空内容 ⇒ 退回既有那条空响应（超限另 `AURORA_LOG_WARN` 一条，D7①，屏 C）
  └─ 回写的字节与读线程那批应答共用同一条出口：`Session::flush` ⇒ 编码 ⇒ 行尾折算 ⇒ 连接
```

**五道纪律**：① 锁内不碰剪贴板、不碰 UI（G9 的那一次子进程往返绝不能进网格锁）；② 状态机不做「答不答」的 IO 判定，只做「登记 / 立刻空响应」的**分派**，内容与编码在主线程；③ 闸门是 app 级**一份**而非每会话一份——这是 D2① 与 G15 联立的唯一解法：不扩广播也能让「改档即刻对全部在途会话生效」；④ 主线程发起的应答**不挂** `pending_responses_`——那条缓冲只在读线程喂入时才冲出去，空闲会话里点了允许就会永远发不出去（D13② 的第二次改口由此而来）；⑤ 答完一条**不在回调里接着问下一条**，改为标脏排下一帧——就地递归会经由 `set_content` 把当前还在栈上的那份对话框内容（本次回调的宿主）拆掉，实现后实测正是这一条把「一次一问」的证人打成了段错误。

---

## 6 验收判据映射

- **默认禁止（判据 ①）**：`utest_config` 的 `defaults_are_the_first_launch_shape` 锁「`Settings{}` 里该键就是 `Deny`」（批 3-A 落地，M5 把缺省改成 `Allow` 只点亮这一条 ⇒ 证人非空转）；`utest_terminal_osc` 锁「闸门未接线 / 显式 Deny 两种来路都回写同一串空响应，逐字节与现状那条相等」——G2 那条既有用例是本判据的**回归底线**，不许改坏。
- **三态可配（判据 ②）**：批 3-A 已全部落地。`utest_settings_catalog` 锁总数 64→65，并由既有的 `choice_rows_declare_the_whitelists_the_loader_enforces` 锁「面板候选＝读侧白名单」；`utest_form_transfer` 的 `every_declared_choice_survives_the_config_file` 逐档走「提交⇒写盘⇒读回⇒再装载」，`utest_config` 的 `every_value_survives_a_write_read_round_trip` 以整结构等值锁该键在 schema 里的往返；`itest_settings_panel` 锁终端页那一行画得出（那条按 catalog 通用遍历的用例）、选得中、改得动（`a_wired_immediate_change_persists_once_and_broadcasts_once` 的第二段），且**候选按落盘名显示**（`deny`/`allow`/`ask`）——稿面那句「三档候选的中文」已按实现改口，见 §4 与 §7。
- **改档即刻生效（D2① 的正面判据，批 2 有证人）**：`itest_clipboard_read` 三案锁「现读」而不是「登记那一档」——`a_policy_change_answers_the_waiting_batch_without_asking`（Ask 登记、面板写盘之后把同一个闸门改成 Deny ⇒ 待问那一条**当场拒**，不再问）、`an_allow_policy_change_answers_the_waiting_batch_at_once`（同法改 Allow ⇒ 整批直接答出正文）、`a_query_registered_before_the_seams_are_wired_still_gets_answered`（装配晚一拍不丢请求：登记时闸门还没接、接线之后照样有人答）。这三案的形态本身就是 D2① 的取舍：改的是**同一个 app 级对象**的读数，不是给每个会话补一次通知。
- **被拒 ⇒ 空响应而非静默（判据 ③）**：批 2 的 `itest_clipboard_read` 把「被拒」的五条来路各做成一案，都断「该会话写出的字节里恰有一条空响应，且没有第二条」——`ask_policy_answers_no_with_the_empty_response`（答否）、`the_view_answers_every_outstanding_request_when_it_dies`（三条查询全没人答 ⇒ 三条空响应，一条不多不少）、`a_view_without_a_host_still_answers_the_question_it_cannot_ask`（问不出去也要回话）、`an_oversize_clipboard_degrades_to_the_empty_response` 与 `an_empty_clipboard_degrades_to_the_empty_response`（超限 / 剪贴板为空 ⇒ 降级）。脱界面的那一半在 `utest_session` 的 `the_outbox_turns_one_read_request_into_exactly_one_answer`：`nullopt`、空串、超 64 KiB 三档的字节与既有那条空响应**逐字节相等**。**批 3-C 再补现场那一半**：`etest_osc_clipboard.a_denied_osc_52_read_query_still_answers_the_real_shell` 让**真 shell** 发一条读请求，断的是「问的那一侧在屏上确实等到 `[ESC]]52;c;[BEL]`」——静默丢弃在替身层只能证字节，在这里能证到对面不会永远等（M-A 的变异读数见 §8）。
- **允许 ⇒ 真给得出且可还原（判据 ④）**：`utest_term_clipboard_read` 锁 `encode_base64` 与 `osc::decode_base64` 往返互逆（含 CJK 与余数三档）；批 2 的 `itest_clipboard_read` 锁「替身剪贴板里那句文本 base64 之后原样出现在回写字节里」——案名 `allow_policy_answers_the_read_within_one_frame`（Allow 档：**一帧之内**问到并答出，不需要第二帧）与 `ask_policy_answers_yes_with_the_local_content`（Ask 档答是），两案都拿 `term::encode_base64` **现算**期望串而不是硬编码，于是编码腿与被测的期望腿不会同时写错。另加 `the_production_dialog_yes_button_answers_through_the_hit_chain` 与 `..._no_button_answers_the_empty_response`：这两案**不用替身**，在对话框内容盒内按阅读序扫 `Button`、经 `hit_test_chain` + `EventDispatcher` 真实派发，证的是生产那枚模态框的按钮次序（Yes 在左）与「答话即收起」确实接在同一个 `answer` 上。**证人分层要说清**：本沙箱 `xsel`/`xclip` 均不在位（G9，两条命令都 127），⇒「真把系统剪贴板交出去」那一档在这里**只有替身证人**，现场腿要等有桥的机器（走查第 ⒋ 条）。**批 3-C 的 Allow 案补的是「通道」那一半而不是「正文」那一半**：`an_osc_52_read_query_from_a_real_shell_is_answered_on_the_same_channel` 证到真 shell 问出去、我们答的转义串原样回到同一通道并进屏（缺桥时 `answer_read` 返回 false ⇒ 回写仍是那条空响应，用例把「应答返回值＝桥可用性」断成现场事实），正文那半件在本沙箱只有替身证人。
- **询问不挂起（判据 ⑤）**：**批 2 实测改口**——框架的 `au::Dialog` 没有独立「关框」通道（内容盒外只有遮罩，而遮罩点击**不触发** `on_close`，Aurora 主仓 `widget/dialog.h` 的 `dialog_geom` 段），`aurora::confirm` 也只有 Yes/No 两枚按钮 ⇒ 需求那句「任何出口都回写」在我们的框上实际只有**三条出口**：答是、答否、**一直不答**。第三条不是「关掉就行」，而是把欠账留到视口析构（关标签 / 关窗 / 退出），故证人按这三条各立一案：`ask_policy_answers_yes_with_the_local_content` / `ask_policy_answers_no_with_the_empty_response` / `an_unanswered_question_is_still_answered_when_the_view_dies`（不答 ⇒ 析构补一条空响应，且**只补一条**——`a_question_answered_twice_writes_one_response` 锁账本只放行第一次答话）。另在 `utest_term_clipboard_read` 锁「第 9 条当场回空响应并计数，前 8 条仍在待答队列里」，避免远端一条输出把我们的待答队列撑爆。
- **上限（判据 ⑥）**：两处上限分开走。**待答队列上限**取文件级常量 `kMaxClipboardReadRequests = 8`（不做测试入参覆盖：8 条本身就是判据要的形状，喂 9 条即可锁），`utest_term_clipboard_read` 锁「第 9 条当场回空响应 + `clipboard_read_denied` +1 + 腾出位置后能再登记」。**应答内容上限**（`kMaxClipboardResponseBytes = 64 KiB`，D7①）在批 2 的 outbox 腿，批 2 的证人是脱界面的 `utest_session.the_outbox_turns_one_read_request_into_exactly_one_answer`（喂一份 > 64 KiB 的正文 ⇒ 字节仍是那条空响应、返回 false）与界面的 `itest_clipboard_read.an_oversize_clipboard_degrades_to_the_empty_response`（替身剪贴板给超限内容 ⇒ 回写恰为一条空响应）。**Warn 那一条留痕不在断言里**：框架的日志宏在测试进程内可注 handler，但本仓既有用例（会话日志、Telnet 各腿）都没有注日志的先例，为这一档单开一套断言基础设施不属本棒的判据 ⇒ 措辞与留痕按走查第 ⑶ 条核。
- **Pc 回显（D8①）**：`utest_term_clipboard_read` 一次锁齐七档（`c`/`p`/`0`/`*` 各回显自身、`52;c;p;?` 回显**首段** `c` 而非末段、`52;?` 无 Pc 段回显 `c`、`zz` 与 64 字长段归一到 `c`）——归一是承重件：Pc 来自不可信输入，原样回写等于替远端把任意字节喷回它自己。
- **既有判据不受影响**：`utest_terminal_osc` 那三条写方向用例（空载荷合法清空、非法载荷不写、多段只留最后载荷）逐条保持绿；G16 是本棒的回归面而不是新功能面。
- **测试读数口径**：结项以 `ctest --preset linux -E etest_` 的**项数**与 `borealis_test_runner` 的**全案数**结项（AGENTS §4.4 第 21 条）；基线是会话日志棒结项的 **81 项／1019 案**（本棒新增两件测试 TU ⇒ 项数 +2）。**批 1 已跑**：`82 项／1031 案`全绿（新增 TU `utest_term_clipboard_read` 12 案）。**批 2 已跑**：`83 项／1050 案`全绿——新增 TU `itest_clipboard_read`（17 案，单跑亦全绿）+ `utest_session` 2 案，项数 +1 是因为批 2 只新增一件 integration TU。**批 3-A 已跑**（新键六处同步 + 设置页那一行 + 三件既有用例加断言）：仍是 `83 项／1050 案`全绿——**项数与案数都不变是本棒的口径而不是漏跑**：批 3-A 一个 TU 都没新增，全部证人长在既有用例体内（`utest_settings_catalog` 的总数 64→65 属于改断言，新增用例反而会把「同族键的表就长这样」这条形状拆成两处）。因此这一棒的非空转只能由 §8 的「批 3-A 变异注入」六则自证，案数增长在这里**不是**证据。装配那几处（闸门两接、`store.settings()` 折算初值、三条入口共用的一次 `sync_clipboard_gate`）仍无自动化证人（裁决 7.83：`src/main.cpp` 不编进 runner），按会话日志棒 §8.1 的手法如实登记。**批 3-B 已跑**（装配接线 + `TabWorkspace` 成员次序）：仍 `83 项／1050 案`全绿、`ctest` 83 项全绿，另加一次真进程冒烟——`build/src/Borealis` 在 `DISPLAY=:0` 下前台跑 4 s 由 `timeout` 收掉（退出码 124＝被杀而非自崩），建壳闭包与闸门初值折算都在那 4 s 里被执行过一遍。**这一棒的非空转拿不出变异读数**：`src/main.cpp` 不在 runner 的编译单元里，把 `set_clipboard_gate` 那一行删掉测试照绿——这正是 7.83 登记的那条限制的形状，故本棒的证据只有「构建通过 + 全量绿 + 真进程不崩」，行为面由批 2 的 17 案 itest 与 §6 的走查清单承担。**批 3-C 已跑**（读方向现场腿两案 + 文档回写）：`ctest --preset linux -E etest_` 仍 **83 项**全绿，`borealis_test_runner` **1050 → 1052 案**全绿（+2 正是新那两案，单跑 `etest_osc_clipboard` 3/3：写方向案 50 ms、Allow 案 2105 ms、Deny 案 2048 ms——两案各等约 2 s 是 `stty ... time 20` 那档 VTIME 到点，不是超时余量）。这一棒的非空转**有**变异读数（§8 的批 3-C 两则），因为两条腿测的是共享代码的行为而不是 `src/main.cpp` 的接线：M-A 摘 deny 分支那条 `emit_response` ⇒ 只有 Deny 案红（30 s 等不到字节），M-B 摘 `answer_clipboard_read` 的 `flush` ⇒ 只有 Allow 案红，两案互为独立证人。**仍未证到的两档如实登记**：⑴ 本沙箱缺桥（G9），Allow 案跑的是 `answered == bridge_available == false` 那条降级腿，「正文/base64 真出得去」那一档在这里没有现场证人，只有替身 itest（走查第 ⑷ 条）；⑵ 走查未做（下面那六条），⇒ 本棒不称「可用」。
- **真机走查**（AGENTS §4.4 第 33 条，未走查不得称「可用」）：⑴ `ask` 档下真 tmux / vim 发读请求，问框出现时该窗口其它操作的手感（D5 的代价面）；⑵ 「去泡杯茶回来」这一档会不会被误认为卡死（D6 的代价面）——批 2 之后这一条有了具体的走查动作：**不答**、直接关标签，看远端是不是拿到一条空响应而不是永远等（框没有关闭叉，关标签就是那一档的出口）；⑶ 复制一段几 MB 的文本再让远端读 ⇒ 超限那一档的留痕读不读得懂；⑷ **本机就是缺桥那一档**（G9 实测 `xsel`/`xclip` 双双回 127）：三态各自的表现须是「远端拿到一条空响应 / 一次询问」而不是挂起；有桥机器的对照档另找一台再走；⑸ Win32 腿：`OpenClipboard` 那侧的读、粘贴板占用、以及 64 KiB 档；⑹ 从「禁止」改到「允许」后**别的标签**立刻能读——这一条是 D2① 的正面判据，也是 G15 那条限制的唯一现场证人。

---

## 7 本期不做（延后 / 在册）

- **下拉候选的本地化**：**批 3 实测改口**——本仓面板把 `control.choices`（就是落盘名 `deny`/`allow`/`ask`）原样交给 `aurora::Dropdown`（`src/ui/settings_panel.cpp` 造下拉那一处），`terminal.right_click` 与 `terminal.paste_newlines` 三档候选同样是英文 ⇒ 稿面初判那句「三档候选的中文」是**文档错误**，本棒按实现改口而不是为这一行单造机制。要做候选本地化是一次全表统一的 UI 扩面（词条表得长出「候选」这一列、`SettingsControl` 要带本地化后的显示串、`itest_settings_panel` 的按文案认行要跟着改），不属 CONN.12 的三句判据 ⇒ 延后并在此在册。
- **选择位分离（`s`/`p`/`0`–`7`）**：框架只有一格 CLIPBOARD（G8），本期只回显 Pc 段。这条要等 Aurora 侧把选择位做成公共 API（按 AGENTS §5.1 分流，本仓不私改），登记为框架缺口候选而非既有能力。
- **「本会话总是允许」的记住、按档案记住、授权审计列表**：D5③ 那一档。没有判据可验收 ⇒ 延后；代价写清楚：`ask` 档下每条读请求都问一次（上限 8 条／帧）。
- **写方向的同一把闸**：D10① 已裁「不纳入」。若将来要做「写也要问」，那是 `SPEC.FEAT.TERM.07` 的新裁决，不是本条的顺手扩面。
- **授权态上屏（状态栏那一格）**：状态栏本体未落地（G12），`appearance.status_bar.show_clipboard_policy` 继续停在「只落盘」档；本棒的留痕落点是**诊断日志**（`AURORA_LOG_WARN`）＋ `OscState` 那两枚计数（D9①）。
- **调试面板补 OSC 族那一行**：F12 那只浮层现在只渲染四族（G17），`clipboard_read_requests` / `clipboard_read_denied` 乃至既有的 `clipboard_write_requests` 都上不了屏。要做就得同时改 `DebugSessionSnapshot`（加字段）、`debug_panel.cpp`（加一行）、装配层取数（`main.cpp` 那份快照）与 `itest_debug_panel` 的行数契约 ⇒ 那是 `SPEC.NF.RELI.01` 的一次扩面，不属 CONN.12 的三句判据 ⇒ 延后并在此在册。
- **超限的可配、上限的按会话档**：D7① 取文件级常量。与「时间戳档/日志上限同档」并列在册——能不加键就不加键。
- **Win32/MSVC 腿的一切说法**（`OpenClipboard` 的读语义、64 KiB 档的实际手感、Pc 回显在 PuTTY 类客户端的兼容性）：本沙箱无 MSVC，标**推断／未验证**（AGENTS §4.2 第 11 条）。**批 3-C 之后这一条多了一处具体的缺口**：读方向的**现场腿**只有 POSIX 一档（两案长在 `#if !defined(_WIN32)` 里，D12 落地改口），于是 Win32 侧「真 shell 问到真回写」这一段连降级档的证人也没有——它现在的证据只有 `utest_terminal_osc` / `utest_session` 那几条脱界面的字节断言（平台无关，走的是同一份共享代码）加真机走查第 ⑸ 条。
- **真机走查全部六条**（§6 末）：未做 ⇒ 本棒结项**不称「可用」**（AGENTS §4.4 第 33 条）。**批 3-B 实测的沙箱边界**：这台机器上两条走查前置都不成立——没有输入注入工具（`xdotool`/`wtype`/`ydotool`/`xte` 全部 MISSING，裁决 7.31① 那句「锁屏会话下 `SendInput` 静默失效」在 POSIX 腿有同样的形态），截屏侧 `import -window root` 在本 Wayland 会话直接挂住不返回（`grim`/`spectacle`/`gnome-screenshot` 亦 MISSING）。⇒ 这六条需要人在交互桌面手动跑，不是本沙箱能补的自动化。

---

## 8 探针复现与裁决记录

探针一次性、落 `$TMPDIR`，**不入仓**；本棒的读数主要取自源码与既有用例（G1–G17 每条都给得出路径与行号），另三条需要现场——**均已跑，读数如下**：

- **G9（一次剪贴板读的成本）——跑完，结论与稿面初判相反**：`which xsel` / `which xclip` 双双 not found；手写计时各跑 20 次 ⇒ `xsel --clipboard --output` 均值 **0.52 ms**（最差 0.80 ms）、`xclip -o -selection clipboard` 均值 **0.57 ms**（最差 0.85 ms）、`/bin/true` 同法 0.75 ms 作对照 ⇒ 一次「桥不在位」的读＝两次 `sh -c` fork/exec/wait，约 **1 ms 量级**。`./build/borealis_test_runner --run etest_osc_clipboard` 单跑一条绿，但其 `clipboard_outbox.cpp` 那条 WRN 打出 `exited with code 32512`（即 127）两份 ⇒ **绿的是「缺桥时断可判的那三条腿」那一档，不是内容真进了剪贴板**。**未测到**：有桥机器的读耗时、4 MiB 内容是否线性变慢（决定 D7 的 64 KiB 数量级）⇒ D7 的量级按「几条 MB 日志常见」推，标**推断／未验证**，走查第 ⑶ 条补。
- **G8（框架只有一格）——跑完，坐实**：`strace -f -e trace=execve` 包住 `borealis_test_runner --run etest_osc_clipboard`，出现过的 execve 只有四条串——`xsel --clipboard --output` / `xsel --clipboard --input` / `xclip -selection clipboard -o` / `xclip -selection clipboard`（各 3 次），**没有一条带 `-selection primary`**；每条都以 `/bin/sh -c --` 起，故「一次读」的真实成本是 fork+shell+等待。
- **G2/G16（既有用例即底线）——跑完，锚点已取**：`utest_terminal_osc` 19 案全绿，读方向那条叫 **`osc_52_read_request_answers_empty_selection`**，写方向三条回归面叫 `osc_52_empty_payload_is_a_valid_clear` / `osc_52_rejects_malformed_payloads` / `osc_52_multiple_clipboard_numbers_keep_last_payload`；`utest_session` 里连接侧那条叫 **`osc_52_read_request_answers_through_write_channel`**。这两枚用例名是本棒的回归底线，批 1/批 2 不许改坏（Deny 档逐字节不变）。
- **G17（调试面板不含 OSC 族）——读源码坐实**：`src/ui/debug_panel.cpp` 每会话渲染 `parse` / `decode` / `encode` / `queue` 四行，`DebugSessionSnapshot` 无 OSC 字段，`itest_debug_panel` 把行数写成契约 ⇒ 「计数进可观测面板」在仓里没有落点，稿面已按此改口（§2 屏 C、D7、D9、§7）。
- **批 1 变异注入（四则，全部变红、还原复绿）**：M1 去掉拒绝档的空响应回写 ⇒ 4 案红；M2 Pc 不归一（原样吐出首段）⇒ 1 案红；M3 满档不再当场回绝 ⇒ 1 案红；M4 base64 填充位错一档 ⇒ 2 案红（`+/8=`、`AAEC` 那两条 RFC 向量）。
- **批 2 变异注入（六则，全部变红、还原复绿，2026-10-12 实测）**——逐则给「注入点 ⇒ 哪条用例转红」（AGENTS §4.4 第 22 条）：

  | 则 | 注入点 | 转红的证人 |
  | --- | --- | --- |
  | M1 | `Session::answer_clipboard_read` 的 `flush(response)` 摘掉（应答不走写通道） | `utest_session` 2 案 + `itest_clipboard_read` 16 案（只有那句词条案不看字节） |
  | M2 | `Session::take_clipboard_reads` 改回 `return {}`（会话不转发读请求） | 同上：`utest_session` 2 案 + `itest` 16 案 |
  | M3 | `ClipboardOutbox::answer_read` 的 `oversize` 恒假（超限不降级） | `utest_session.the_outbox_turns_one_read_request_into_exactly_one_answer` + `itest_clipboard_read.an_oversize_clipboard_degrades_to_the_empty_response` |
  | M4 | 删掉视口析构里对 `clipboard_reads_waiting_` 的撤账循环 | `itest_clipboard_read.the_view_answers_every_outstanding_request_when_it_dies` |
  | M5 | `ask_clipboard_read` 去掉「手上已有在问的那一条」这一闸 | `itest_clipboard_read.a_query_arriving_while_a_question_is_open_waits` |
  | M6 | 闸门现读改成立即放行（`granted` 恒真，Deny 档也答） | `itest_clipboard_read.a_policy_change_answers_the_waiting_batch_without_asking` |

  **M5 的第一轮是空转的**（同一份注入跑完仍全绿）——根因不在注入点而在证人：那两条查询写在同一次 `feed` 里，一帧就整批取走，「在问的那一条占住账本」这条闸根本没有第二个触发点可露。补了 `a_query_arriving_while_a_question_is_open_waits`（框开着时**再投**一条 ⇒ 询问次数必须仍是 1）之后 M5 才转红。这一条按第 22 条的口径登记：**变异自证证的是证人，不是代码**——首轮「5/6 变红」当时若直接结项，就会把「一次一问」留在无证人状态。同时补的三案（析构批量撤账 / 账本只放行第一次答话 / 装配晚一拍不丢请求）各锁一条稿面承诺。

- **批 3-A 变异注入（六则，全部变红、还原复绿，2026-10-12 实测）**——本棒没有新增 TU，故各则钉住「六处同步」里的接缝，逐则给「注入点 ⇒ 哪条用例转红」：

  | 则 | 注入点 | 转红的证人 |
  | --- | --- | --- |
  | M1 | `store.cpp` 的 `terminal_to_json` 摘掉这一键的 `put_enum`（写侧漏键） | `utest_config` 6 案（`every_value_survives_a_write_read_round_trip` 与另五条以 `non_default()` 为素材的整结构等值）+ `utest_form_transfer.every_declared_choice_survives_the_config_file` + `utest_settings_catalog.table_covers_exactly_the_written_schema_keys` |
  | M2 | `schema_names.h` 把 `ask` 写成 `askme`（名表与面板候选分叉） | `utest_settings_catalog.choice_rows_declare_the_whitelists_the_loader_enforces` + `utest_form_transfer.every_declared_choice_survives_the_config_file` + `itest_settings_panel.a_wired_immediate_change_persists_once_and_broadcasts_once` |
  | M3 | `settings_catalog.cpp` 该行 `Immediate` 降为 `NextSession`（生效档位改口） | `utest_settings_catalog.grade_columns_match_the_two_physical_boundaries` + `itest_settings_panel.a_wired_immediate_change_persists_once_and_broadcasts_once`（`apply_scope` 变 `PersistOnly`） |
  | M4 | `form_transfer.cpp` 该枚 `Binding` 的键名拼错一位 | `utest_form_transfer` 5 案（含 `the_bridge_loads_every_catalog_key_and_nothing_else`、`each_key_takes_the_member_it_names`、`writing_the_form_back_rebuilds_that_settings`）+ `itest_settings_panel` 3 案（那一行直接不画了） |
  | M5 | `settings.h` 缺省档 `Deny` 改 `Allow`（判据 ① 被反着写） | `utest_config.defaults_are_the_first_launch_shape`（仅此一条 ⇒ 说明这条断言正是这一档的唯一证人） |
  | M6 | `form_transfer.cpp` 该枚 `Binding` 的 getter 改读 `s.terminal.right_click`（键名没错、域仍是 Text，只是**接错了成员**） | `utest_form_transfer.each_key_takes_the_member_it_names`（另三案 `writing_the_form_back_rebuilds_that_settings`／`only_the_committed_key_moves`／`every_declared_choice_survives_the_config_file` 同批红） |

  M6 是**批 3-A 结项后补落的那句字面量证人**（`expect("terminal.clipboard_read", FormValue::text("ask"))`）的自证：M4 打的是「键不存在」，而「键在、域对、成员接错」这一档形状网（`the_bridge_loads_every_catalog_key_and_nothing_else` 只比行数与域）结构上看不见 ⇒ 只有按 key 取字面量的那条能红。稿件原先把这句写成「已加」而代码里没有，本棒按 AGENTS §4.2 第 12 条把两处对齐（补齐代码，而不是把稿改口）。

  两则是**空转排查**里顺手坐实的：M1 的红蔓延到六条 `utest_config` 用例，是因为该件的往返判据取的是**整结构逐字段等值**——新字段一旦进 `non_default()`，任何一侧漏键都会在每一处以它为素材的用例上现形，这正是 G10 那套「六个同步点」被一张断言网兜住的原因。M5 只红一条则是刻意的对照：缺省档的形状只有 `defaults_are_the_first_launch_shape` 一处证人，所以那一行断言不能省。

- **批 3-A 的既有债现场（两条，非变异而是被本棒真点红的）**：终端页多了一行下拉与一行旁注之后，`itest_settings_panel` 的 `a_row_control_still_commits_after_the_row_area_has_been_scrolled` 与 `the_editable_controls_paint_the_chrome_colors_not_the_light_defaults` 同批转红，根因都是**把判据挂在「滚了几档」上**——前者写死 8 档去够那四枚关闭态开关，后者在滚动之后采下拉框顶的分数点、而框被视口裁到只剩 17 dp，于是读到了标签字形（`{243,243,238}`）。修法一处是逐档滚到找着为止、一处是把那一腿移到滚之前，理由与实测读数见 §4 的 `itest_settings_panel` 条。**教训按第 22 条同一条口径登记**：像素与命中类判据一旦依赖排版，新增一行设置项就会把它打成假红——本棒没有为了变绿而放宽判据，两条都还活着（前者仍要求「滚完之后确实命中一枚关闭态开关」，后者仍在真实帧缓冲上采底色的点）。

- **批 3-C 探针（一次假红查成内核事实，2026-10-12 实测）**：两条读方向腿首跑各 30 s 超时红——屏上等不到我们回写的字节。用三份一次性 pty 探针（`$TMPDIR/probe_read{,2,3}.py`，不入仓）逐档隔离：先排除 echo 与解析（子进程 `stty -a` 读数确认 raw 生效），再定位到**命令包装器**——GNU `timeout` 把被管命令放进**独立进程组**，而该组内的进程读自己的控制 tty 属「后台读」，内核直接 **SIGTTIN 停住**（实测：套 `timeout` 时子进程一个字都不打印，去掉后立即出串）。改用 tty 自己的等待档 `stty raw -echo min 0 time 20`（VMIN=0/VTIME=20 ⇒ 最多 2 s 有界返回）+ 裸 `dd bs=1 count=64`，探针读数 `CHUNK 2.05 b'[ESC]]52;c;YWJj[BEL]'`——一手坐实「真 shell 问 ⇒ 我们答的那串字节原样回到同一通道」。**判据没有为此放宽**：用例里等的仍是逐字节转义串，只是把「等多久」从 `timeout` 换成 VTIME。**教训登记（AGENTS §4.4 第 22 条同一条口径）**：现场腿红的时候先怀疑证人自己的执行环境（进程组、tty 归属、谁在等谁），而不是先把判据改成 SKIP 或放宽到「只要不崩」。

- **批 3-C 变异注入（两则，全部变红、还原复绿，2026-10-12 实测）**——脚本 `$TMPDIR/mutation_check_batch3c.py`（一次性、自带 finally 原文回写，不入仓）：

  | 则 | 注入点 | 转红的证人 |
  | --- | --- | --- |
  | M-A | `src/term/terminal.cpp` deny 分支摘掉 `emit_response(U"\x1B]52;c;\x07")`（被拒改回静默丢弃） | `etest_osc_clipboard.a_denied_osc_52_read_query_still_answers_the_real_shell`（30 s 等不到 `[ESC]]52;c;[BEL]`）——另两案全绿 |
  | M-B | `src/session/session.cpp` 的 `answer_clipboard_read` 摘掉 `flush(response)`（应答不走写通道） | `etest_osc_clipboard.an_osc_52_read_query_from_a_real_shell_is_answered_on_the_same_channel`（30 s 等不到应答）——Deny 案仍绿，因为它走的是 `pending_responses_` 那半件、同一轮 ingest 就冲出 |

  **这两则的形状正是现场腿的价值**：批 2 的 itest 早已把「被拒不静默」断成替身层字节（M1/M2 同档），但只有这两案能在**真 PTY + 真 shell** 上把远端那一侧的等待证出来；而 M-B 打不红 Deny 案、M-A 打不红 Allow 案，说明两条腿各锁一条独立的通道（状态机内联回写 vs 主线程答话后 `flush`），不是一处代码的两个影子证人。还原后 `git diff` 对这两件干净、重建复绿（§6 的 83 项／1052 案读数就是复绿那一次）。

**裁决记录（2026-10-12）**：人已给出常存授权（「按顺序持续推进……后续再遇到问题则你自行按照推荐去做」），故 D1–D12 **全部按 §3 的推荐档收口**，无一条改判需要现场等人；其中三条与稿面初判不同、值得点名的自决位是：D2 选**现读**而不是照 `TerminalDefaults` 的构造期快照（理由：G15 让「改档只到当前标签」成为既有事实，走 `NextSession` 等于给安全闸留一个跨标签的洞）；D4 选**逐条答**而不是照写方向的合并（理由：读方向是别人在等自己那一条）；D6 选**一直等到用户答**而不是 TERM.09 那档 5 s 自收（理由：通知可以超时，授权决定不该被时间替用户拍板）。收口与理由随批 1 落地入册 `SPECIFICATIONS.md` §7 裁决 **7.118**、`CHANGELOG.md` **v0.118**，`PLAN.md` 的 CONN.12 行与 M4 行同步改口。实现按 §4 分三批走：批 1＝`term` 层纯逻辑件与其 utest（不含界面）；批 2＝`Session`/`ClipboardOutbox` 应答腿 + 询问钩子 + `itest`；批 3＝新键六处同步 + 设置页那一行与旁注 + 装配接线 + 文档回写与在册收口。
