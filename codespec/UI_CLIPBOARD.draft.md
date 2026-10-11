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
| **G11** | **反向核对表是双向闸**：`utest_settings_catalog` 钉「写出的 schema 键 = 表里的行数」＝**64**（会话日志棒刚把 63 改成 64）、`Absent` 行数＝**19**、`Wired ∧ NextSession` 名单是一份显式列表——新键若标 `Wired ∧ Immediate` 则只动**总数**一处计数，不进名单 | `tests/unit/utest_settings_catalog.cpp`（F8 同源那三条断言，现值 64／19） |
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
6. 设置页终端页那一行 + 组顶旁注 + 词条 + `utest_settings_catalog` 计数改口（G10 的四处 + G11 的一处）。

**前提与不做**（详见 §7）：

- **写方向不动**：`TERM.07` 已落、`SPEC.FEAT.CONN.12` 那句「写方向默认允许」已在（G16 那三条用例继续成立）。
- **选择位（`s`/`p`/`0`–`7`）不分别服务**：框架只有一格剪贴板（G8），本棒只回显 Pc 段、内容恒取系统剪贴板。
- **授权态上屏不做**：状态栏本体未落地（G12），那一格显示位继续停在「只落盘」档。
- **「本会话总是允许」的记住、按档案记住、授权审计列表**都不在本期（D5 的代价段）。
- **OSC 52 之外的其它「远端索取本地资源」不纳入同一闸门**（如 OSC 52 的 query 变体之外的 CSI 查询），本棒一把闸只管这一条。

---

## 2 草图与判据对应

草图共四屏（A 设置页那一行 + 旁注 / B 三态分派与询问框 / C 三档降级的可观测形态 / D 批次·证人·在册），1560 × 2290，`convert -background white … -density 96` 出 1× PNG。**一条画图纪律**：本沙箱的 SVG 光栅化器（ImageMagick 内置 MSVG）用的等宽字体**没有中文字形**，`font-family="monospace"` 的行里一旦掺中文就整段丢字 ⇒ 图中 mono 行只放 ASCII（VT 序列、符号名、日志原文），中文一律走默认字体。

屏 A＝设置页终端页那一条下拉（三态，缺省「禁止」）＋组顶旁注；屏 B＝`ask` 档弹出的询问框（要说清「哪个远端在要什么」与「不给会怎样」）；屏 C＝超限 / 读不到 / 未答 三种降级的**可观测形态**——左边那条诊断日志是本期唯一真实落点，右边调试面板画的是**本期没有**的 OSC 族那一行（G17：面板四族里从来不含 OSC，`clipboard_read_*` 两枚计数只活在 `OscState`，§7 在册）。

| 圈号 | 判据（需求原文那一句） | 草图上的位置 | 落地的证人 |
| --- | --- | --- | --- |
| 1 | 默认禁止 | 屏 A 下拉的缺省选中位 | `utest_config` 缺省档 + `utest_terminal_osc` 的 deny 分派 |
| 2 | 三态可配、可开启 | 屏 A 三个候选 | `utest_settings_catalog`（行数 +1）+ `utest_form_transfer`（该键往返）|
| 3 | 被拒 ⇒ 回写空响应而非静默 | 屏 B 的「拒绝」按钮与屏 C 的降级三档 | `utest_session`（既有那条 G2 扩成三态矩阵）+ `itest_clipboard_read` |
| 4 | 允许 ⇒ 真的把内容给出去（base64 且逐字节可还原） | 屏 B 的「允许」按钮 | `utest_term_clipboard_read`（`encode_base64` 逐档 + 与解码件往返互逆，D11 已并件）+ `itest_clipboard_read`（经替身剪贴板断回写串） |
| 5 | 询问档不挂起：任何出口都回写 | 屏 B 的关闭叉与遮罩点击 | `itest_clipboard_read` 的「未答即关 ⇒ 空响应」「取消 ⇒ 空响应」 |
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
| **D6** | 未答／关框／超时怎么兜底 | ① 一直等到用户答（**任何出口**都回写一条，关框＝拒绝档空响应）；② 5 s 超时按拒绝回写（照 TERM.09 的自收档）；③ 不回写，静等 | **①** | 需求判据 ③ 的原话是「避免远端程序挂起」——真正的挂起风险在「问完就把这一条吞了」（③ 档），而不是「多等一会儿」。② 的 5 s 对一次通知合适，对「要不要把剪贴板交给它」这种决定太短，而且会让「用户去泡了杯茶回来」变成一次静默拒绝＋远端拿到空内容，比弹窗骚扰更难诊断。会话析构那一档无需补写：连接同时断了。**代价**：`ask` 档下远端的读请求会停在框前直到用户答——这是**授权语义本身**，写进草屏 B 的那句说明与走查第 ⑵ 条 |
| **D7** | 回写内容有上限吗 | ① 有界：`kMaxClipboardResponseBytes = 64 KiB`（UTF-8 字节数），超限按拒绝档回写空响应 + 诊断留痕；② 不设限 | **①** | 剪贴板里可能是几 MB 的日志（复制一次 `cat` 输出就有），base64 再乘 4/3，一条 `send_text` 就是把这份东西喷进该会话的写腿；而远端只问了「给不给」，没承诺收多少。**代价**：超限那一档对用户是「点了允许却没内容」，必须留痕（屏 C 那一档：诊断日志 `AURORA_LOG_WARN` 一条 + `OscState` 计数；调试面板那一行本期没有落点，G17 在册），措辞走词条；上限是文件级常量而非键（会话日志那棒的 D9① 同档取向：能不加键就不加） |
| **D8** | Pc 段怎么办 | ① 回显收到的**首段** Pc（无 Pc 段则回 `c`），内容恒取系统剪贴板；② 恒回 `c`（现状）；③ 按 `c`/`s`/`p`/`0-7` 分别取不同选择 | **①** | ③ 在框架侧无处可取（G8：只有一格 CLIPBOARD）——做得出界面但答不出内容，就是留了个「点了什么也不发生」的空位。② 会把 `52;p;?` 的问者答成 `c`，问 tmux 的缓冲区拿到的却是 X11 CLIPBOARD，这是**答错**而不是降级。① 的代价是「回显了选择位而内容同源」这件事本身仍有出入，因此要把这条限制写进头注与 §7 在册（不做选择位分离） |
| **D9** | 拒绝要不要上屏提示 | ① `deny` 档静默回空响应 + 计数进 `OscState`；② 每次拒绝弹一条提示 | **①** | ② 会被远端程序的安装脚本/`viminfo` 探测一类的常规查询打成弹窗骚扰，而「禁止」本就是用户显式选的缺省态（判据 ①）。计数进 `OscState`（与 `clipboard_write_requests`、`unhandled_count` 同族）够排障用，但要把话说准：**这一族计数本期不上屏**——`SPEC.NF.RELI.01` 的调试面板只渲染 parse/decode/encode/queue 四族，`DebugSessionSnapshot` 里没有任何 OSC 字段（G17），既有的 `clipboard_write_requests` 也一样只活在结构体里；`SPEC.NF.RELI.01` 那句「可观测」在本棒由**诊断日志 + 用例断言**承担，面板补 OSC 那一行按 §7 在册。**代价**：用户第一次遇到「tmux 复制没反应」时不会想到是这道闸，而且在屏幕上**查不到** ⇒ 靠设置页那句组顶旁注赔付 |
| **D10** | 写方向纳入同一把闸吗 | ① 不纳入（本棒只管读）；② 扩成「读/写各一枚三态」 | **①** | 写方向「默认允许」是 `SPEC.FEAT.TERM.07` + 裁决 7.21③ 已落并有用例的既成口径（G16 三条），改它需要新需求裁决而不是顺手扩面（AGENTS §4.1 第 3 条）。**代价**：界面上那一行只说「读取」，措辞不能写成「剪贴板授权」这种涵盖写的样子——词条按 D10 收窄 |
| **D11** | 文件布局与批次 | — | 见 §4 | 三批切法：批 1＝`term` 层纯逻辑（枚举 + 闸门 + 待答队列 + `encode_base64`）与其 utest；批 2＝`Session`/`ClipboardOutbox` 的应答腿 + 询问钩子 + `itest`；批 3＝新键六处同步 + 面板行 + 装配接线 + 文档回写 |
| **D12** | 验证形态 | — | 三层：① `utest_term_clipboard_read`（三态分派 × Pc 回显 × 队列上限 × 超限 × `encode_base64` 逐档——base64 那半件的宿主文件就是 `src/term/clipboard_read.cpp`，不为一枚纯函数另立 TU）；② `itest_clipboard_read`（替身剪贴板经 `Presentation` 钩子注入 ⇒ 断回写串逐字节；含未答即关、取消、超限三档降级）；③ 真机走查（有桥机器、真 tmux/vim 发读请求、Win 腿——本机 `xsel`/`xclip` 双双不在位，只能走缺桥那一档，G9）。`etest_` 那条不新写：`etest_osc_clipboard` 已把「桥可用性显式探测」的口径立住（G9），本棒批 3 给它补一条**读方向**的现场腿（真 shell 发 `printf '\033]52;c;?\a'` ⇒ 断我们回写的字节），Windows 腿投放条件不变 | — |

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
include/borealis/session/session.h          修改 —— 两枚运行期入口：`take_clipboard_reads()`（转发状态机那份队列，
                                                  锁内取、锁外交）、`answer_clipboard_read(std::u32string_view response)`
                                                  （把「该答的那条 OSC 52 正文」走**同一**应答通道回写，复用 G5 的
                                                  `pending_responses_` 折算与编码，不另开一条写腿）
src/session/session.cpp                     修改 —— 上述两枚的落地；取走与回写都不在锁内做 IO（§3.4）
include/borealis/session/clipboard_outbox.h 修改 —— `answer_reads(Session &, const std::string &utf8)`：把「已经拿到手的
                                                  剪贴板文本」折算成应答串并交回会话（**本件不读剪贴板也不弹窗**——
                                                  读与问都在装配层，落地件只做编码与形状，这样「超限/为空/非法」三档
                                                  降级能脱界面断言）；上限常量 `kMaxClipboardResponseBytes`（D7①）
src/session/clipboard_outbox.cpp            修改 —— 同一件实现：`encode_base64` 一次、拼 `ESC ] 52 ; <Pc> ; <b64> BEL`、
                                                  超限即退回空响应并 `AURORA_LOG_WARN`
src/ui/terminal_view.h                      修改 —— `Presentation` 追加一枚异步询问钩子 `confirm_clipboard_read`
                                                  （形态照同结构体里的 `confirm_multiline`，G14：留空即回落默认）
src/ui/terminal_view.cpp                    修改 —— 默认实现：模态 `au::confirm`（标题/正文走词条），
                                                  **任何出口**（是/否/关框/遮罩）都调一次 `answer(false/true)`（D6①）
include/borealis/config/settings.h          修改 —— `terminal` 域追加 `ClipboardReadPolicy clipboard_read{Deny}`
                                                  （判据 ① 的缺省禁止；`operator==` 默认逐字段比较照旧成立）
src/config/schema_names.h                   修改 —— `kClipboardReadNames` 三档落盘名表（`deny`/`allow`/`ask`，G10 同族）
src/config/store.cpp                        修改 —— `put_enum` 与 `scope.enumerated` 两处
src/config/form_transfer.cpp                修改 —— 一枚 `Binding`
src/ui/settings_catalog.cpp                 修改 —— `dropdown("terminal.clipboard_read", SettingsPage::Terminal,
                                                  {"deny","allow","ask"}, Wired, Immediate)` 一行（D2① ⇒ **Immediate**，
                                                  不进 `NextSession` 名单），行序紧随 `terminal.right_click`——同族的
                                                  交互三态两条挨着，终端页其余行序不动
src/ui/settings_i18n.cpp                    修改 —— 该行标签、三档候选的中文、组顶旁注一句、询问框两句（标题 + 正文）、
                                                  超限那一档的回报句；`UI_SETTINGS` 稿脚注那句「随该条落期入 schema」
                                                  自此改口为既成事实
src/ui/settings_panel.cpp                   修改 —— 终端页的组顶旁注表 `kGroupNotes` 追加一行（D9 的赔付处）
src/main.cpp                                修改 —— 装配：一枚 app 级 `ClipboardGate`（`store.settings()` 折算初值、
                                                  `hooks.persist` 之后刷新）、每会话 `set_clipboard_gate(&gate)`（八处
                                                  构造点 + 预览腿**一处不漏**，漏即 fail-closed）、帧边界对每个会话
                                                  `take_clipboard_reads()` ⇒ 询问（`ask` 档经视口那枚钩子）⇒
                                                  `ClipboardOutbox::answer_reads`；`perform_close` 撤账口径同 serial/telnet 表
src/CMakeLists.txt                          修改 —— `src/term/clipboard_read.cpp` 进 `borealis_core` 的**显式**源清单
                                                  （只有 tests 走 GLOB；漏这一行即链接期 undefined reference，
                                                  Telnet 批 2 与会话日志批 1 的同一处坑）
tests/unit/utest_term_clipboard_read.cpp     新增 —— 三态分派逐档 × Pc 回显（含 `52;c;p;?` 与 `52;?` 两档，G3）×
                                                  队列上限（第 9 条当场回空响应、前 8 条留队）× 闸门未接线即 Deny ×
                                                  `encode_base64` 逐档（空、1/2/3 字节余数、CJK 字节序、与
                                                  `osc::decode_base64` 往返互逆）
tests/unit/utest_terminal_osc.cpp            不改 —— 既有那条「读查询回空响应」在批 1 后逐字节保持绿，它就是
                                                  D9① 的回归底线；两枚新计数的断言归新 TU，不在旧 TU 重述
tests/unit/utest_session.cpp                 修改 —— G2 那条既有用例保持（Deny 档），新增「`answer_clipboard_read` 走
                                                  同一写通道」与「`Allow` 档经 outbox 折算出的字节」两条
tests/integration/itest_clipboard_read.cpp   新增 —— 替身剪贴板 + 替身确认钩子接进 `Presentation`（G14 的接缝就是为
                                                  这类用例留的）⇒ 逐档断回写串：`Allow` 出正文、`Ask` 答是出正文／
                                                  答否出空响应／**不答即关框也出空响应**（判据 ③⑤）、超限出空响应、
                                                  剪贴板为空出空响应
tests/unit/utest_config.cpp                  修改 —— 新键的往返等值 + 「缺省 deny」一档（判据 ①）
tests/unit/utest_form_transfer.cpp           修改 —— 该键的 `Binding` 断言（G10 同族那条枚举搬运写法）
tests/unit/utest_settings_catalog.cpp        修改 —— G11 的计数改口：总数 64→**65**；`Absent` 19 不变；
                                                  `Wired ∧ NextSession` 名单不变（该键是 Immediate）
tests/e2e/etest_osc_clipboard.cpp            修改 —— 补一条**读方向**现场腿（真 shell 发 `52;c;?` ⇒ 断我们回写的字节；
                                                  桥可用性仍按 G9 的显式探测口径，缺桥时断「可判的那三条腿」而非跳过）
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
主线程帧边界（`app.set_on_frame`，装配层对**全部标签每个会话**排一次，G7 同一条腿）
  ├─ session.take_clipboard_reads() ⇒ 非空即逐条处置（D4① 不合并）
  │     ├─ policy == Ask ⇒ 经视口的 confirm_clipboard_read 弹一问（D5②）
  │     │      答是 → 读本地剪贴板（`ClipboardOutbox::read`，G9：一次子进程往返）
  │     │      答否 / 关框 / 遮罩 → 视作拒绝（D6①：**任何出口都回写一条**）
  │     └─ policy == Allow ⇒ 直接读
  ├─ ClipboardOutbox::answer_reads(session, utf8)
  │     ├─ 长度 ≤ 64 KiB ⇒ "ESC ] 52 ; <回显的 Pc> ; <base64> BEL" → session.answer_clipboard_read(...)
  │     └─ 超限 / 空内容 ⇒ 退回空响应 + `AURORA_LOG_WARN`（D7①，屏 C）
  └─ 回写的字节仍走 G5 那条：登记 → 出锁 flush ⇒ 连接
```

**三道纪律**：① 锁内不碰剪贴板、不碰 UI（G9 的那一次子进程往返绝不能进网格锁）；② 状态机不做「答不答」的 IO 判定，只做「登记 / 立刻空响应」的**分派**，内容与编码在主线程；③ 闸门是 app 级**一份**而非每会话一份——这是 D2① 与 G15 联立的唯一解法：不扩广播也能让「改档即刻对全部在途会话生效」。

---

## 6 验收判据映射

- **默认禁止（判据 ①）**：`utest_config` 锁「新载入的 Preferences 里该键缺省 `deny`」；`utest_terminal_osc` 锁「闸门未接线 / 显式 Deny 两种来路都回写同一串空响应，逐字节与现状那条相等」——G2 那条既有用例是本判据的**回归底线**，不许改坏。
- **三态可配（判据 ②）**：`utest_settings_catalog` 锁总数 64→65；`utest_form_transfer` + `utest_config` 锁该键三档落盘名的往返；`itest_settings_panel` 锁终端页那一行画得出、选得中、改得动（该页行序不变）。
- **被拒 ⇒ 空响应而非静默（判据 ③）**：`itest_clipboard_read` 三档——答否、不答即关框、超限——都断「该会话写出的字节里恰有一条空响应，且没有第二条」。
- **允许 ⇒ 真给得出且可还原（判据 ④）**：`utest_term_clipboard_read` 锁 `encode_base64` 与 `osc::decode_base64` 往返互逆（含 CJK 与余数三档）；`itest_clipboard_read` 锁「替身剪贴板里那句文本 base64 之后原样出现在回写字节里」。**证人分层要说清**：本沙箱 `xsel`/`xclip` 均不在位（G9，两条命令都 127），⇒「真把系统剪贴板交出去」那一档在这里**只有替身证人**，现场腿要等有桥的机器（走查第 ⒋ 条）。
- **询问不挂起（判据 ⑤）**：同上「不答即关框」那一档；另在 `utest_term_clipboard_read` 锁「第 9 条当场回空响应并计数，前 8 条仍在待答队列里」，避免远端一条输出把我们的待答队列撑爆。
- **上限（判据 ⑥）**：两处上限分开走。**待答队列上限**取文件级常量 `kMaxClipboardReadRequests = 8`（不做测试入参覆盖：8 条本身就是判据要的形状，喂 9 条即可锁），`utest_term_clipboard_read` 锁「第 9 条当场回空响应 + `clipboard_read_denied` +1 + 腾出位置后能再登记」。**应答内容上限**（`kMaxClipboardResponseBytes = 64 KiB`，D7①）在批 2 的 outbox 腿，由 `itest_clipboard_read` 锁「超限 ⇒ 空响应 + 一条 warn」。
- **Pc 回显（D8①）**：`utest_term_clipboard_read` 一次锁齐七档（`c`/`p`/`0`/`*` 各回显自身、`52;c;p;?` 回显**首段** `c` 而非末段、`52;?` 无 Pc 段回显 `c`、`zz` 与 64 字长段归一到 `c`）——归一是承重件：Pc 来自不可信输入，原样回写等于替远端把任意字节喷回它自己。
- **既有判据不受影响**：`utest_terminal_osc` 那三条写方向用例（空载荷合法清空、非法载荷不写、多段只留最后载荷）逐条保持绿；G16 是本棒的回归面而不是新功能面。
- **测试读数口径**：结项以 `ctest --preset linux -E etest_` 的**项数**与 `borealis_test_runner` 的**全案数**结项（AGENTS §4.4 第 21 条）；基线是会话日志棒结项的 **81 项／1019 案**（本棒新增两件测试 TU ⇒ 项数 +2）。**批 1 已跑，读数如下**：`82 项／1031 案`全绿（新增 TU `utest_term_clipboard_read` 12 案）。变异注入四则全部变红、还原复绿：M1 去掉拒绝档的空响应回写 ⇒ 4 案红；M2 Pc 不归一（原样吐出首段）⇒ 1 案红；M3 满档不再当场回绝 ⇒ 1 案红；M4 base64 填充位错一档 ⇒ 2 案红（`+/8=`、`AAEC` 那两条 RFC 向量）。变异注入按第 22 条逐则给「注入点 ⇒ 哪条用例转红」，装配那几处（闸门接线、广播后刷新、帧边界排请求）无自动化证人（裁决 7.83 同档）按会话日志棒 §8.1 的手法如实登记。
- **真机走查**（AGENTS §4.4 第 33 条，未走查不得称「可用」）：⑴ `ask` 档下真 tmux / vim 发读请求，问框出现时该窗口其它操作的手感（D5 的代价面）；⑵ 「去泡杯茶回来」这一档会不会被误认为卡死（D6 的代价面）；⑶ 复制一段几 MB 的文本再让远端读 ⇒ 超限那一档的留痕读不读得懂；⑷ **本机就是缺桥那一档**（G9 实测 `xsel`/`xclip` 双双回 127）：三态各自的表现须是「远端拿到一条空响应 / 一次询问」而不是挂起；有桥机器的对照档另找一台再走；⑸ Win32 腿：`OpenClipboard` 那侧的读、粘贴板占用、以及 64 KiB 档；⑹ 从「禁止」改到「允许」后**别的标签**立刻能读——这一条是 D2① 的正面判据，也是 G15 那条限制的唯一现场证人。

---

## 7 本期不做（延后 / 在册）

- **选择位分离（`s`/`p`/`0`–`7`）**：框架只有一格 CLIPBOARD（G8），本期只回显 Pc 段。这条要等 Aurora 侧把选择位做成公共 API（按 AGENTS §5.1 分流，本仓不私改），登记为框架缺口候选而非既有能力。
- **「本会话总是允许」的记住、按档案记住、授权审计列表**：D5③ 那一档。没有判据可验收 ⇒ 延后；代价写清楚：`ask` 档下每条读请求都问一次（上限 8 条／帧）。
- **写方向的同一把闸**：D10① 已裁「不纳入」。若将来要做「写也要问」，那是 `SPEC.FEAT.TERM.07` 的新裁决，不是本条的顺手扩面。
- **授权态上屏（状态栏那一格）**：状态栏本体未落地（G12），`appearance.status_bar.show_clipboard_policy` 继续停在「只落盘」档；本棒的留痕落点是**诊断日志**（`AURORA_LOG_WARN`）＋ `OscState` 那两枚计数（D9①）。
- **调试面板补 OSC 族那一行**：F12 那只浮层现在只渲染四族（G17），`clipboard_read_requests` / `clipboard_read_denied` 乃至既有的 `clipboard_write_requests` 都上不了屏。要做就得同时改 `DebugSessionSnapshot`（加字段）、`debug_panel.cpp`（加一行）、装配层取数（`main.cpp` 那份快照）与 `itest_debug_panel` 的行数契约 ⇒ 那是 `SPEC.NF.RELI.01` 的一次扩面，不属 CONN.12 的三句判据 ⇒ 延后并在此在册。
- **超限的可配、上限的按会话档**：D7① 取文件级常量。与「时间戳档/日志上限同档」并列在册——能不加键就不加键。
- **Win32/MSVC 腿的一切说法**（`OpenClipboard` 的读语义、64 KiB 档的实际手感、Pc 回显在 PuTTY 类客户端的兼容性）：本沙箱无 MSVC，标**推断／未验证**（AGENTS §4.2 第 11 条）。
- **真机走查全部六条**（§6 末）：未做 ⇒ 本棒结项**不称「可用」**（AGENTS §4.4 第 33 条）。

---

## 8 探针复现与裁决记录

探针一次性、落 `$TMPDIR`，**不入仓**；本棒的读数主要取自源码与既有用例（G1–G17 每条都给得出路径与行号），另三条需要现场——**均已跑，读数如下**：

- **G9（一次剪贴板读的成本）——跑完，结论与稿面初判相反**：`which xsel` / `which xclip` 双双 not found；手写计时各跑 20 次 ⇒ `xsel --clipboard --output` 均值 **0.52 ms**（最差 0.80 ms）、`xclip -o -selection clipboard` 均值 **0.57 ms**（最差 0.85 ms）、`/bin/true` 同法 0.75 ms 作对照 ⇒ 一次「桥不在位」的读＝两次 `sh -c` fork/exec/wait，约 **1 ms 量级**。`./build/borealis_test_runner --run etest_osc_clipboard` 单跑一条绿，但其 `clipboard_outbox.cpp` 那条 WRN 打出 `exited with code 32512`（即 127）两份 ⇒ **绿的是「缺桥时断可判的那三条腿」那一档，不是内容真进了剪贴板**。**未测到**：有桥机器的读耗时、4 MiB 内容是否线性变慢（决定 D7 的 64 KiB 数量级）⇒ D7 的量级按「几条 MB 日志常见」推，标**推断／未验证**，走查第 ⑶ 条补。
- **G8（框架只有一格）——跑完，坐实**：`strace -f -e trace=execve` 包住 `borealis_test_runner --run etest_osc_clipboard`，出现过的 execve 只有四条串——`xsel --clipboard --output` / `xsel --clipboard --input` / `xclip -selection clipboard -o` / `xclip -selection clipboard`（各 3 次），**没有一条带 `-selection primary`**；每条都以 `/bin/sh -c --` 起，故「一次读」的真实成本是 fork+shell+等待。
- **G2/G16（既有用例即底线）——跑完，锚点已取**：`utest_terminal_osc` 19 案全绿，读方向那条叫 **`osc_52_read_request_answers_empty_selection`**，写方向三条回归面叫 `osc_52_empty_payload_is_a_valid_clear` / `osc_52_rejects_malformed_payloads` / `osc_52_multiple_clipboard_numbers_keep_last_payload`；`utest_session` 里连接侧那条叫 **`osc_52_read_request_answers_through_write_channel`**。这两枚用例名是本棒的回归底线，批 1/批 2 不许改坏（Deny 档逐字节不变）。
- **G17（调试面板不含 OSC 族）——读源码坐实**：`src/ui/debug_panel.cpp` 每会话渲染 `parse` / `decode` / `encode` / `queue` 四行，`DebugSessionSnapshot` 无 OSC 字段，`itest_debug_panel` 把行数写成契约 ⇒ 「计数进可观测面板」在仓里没有落点，稿面已按此改口（§2 屏 C、D7、D9、§7）。

**裁决记录（2026-10-12）**：人已给出常存授权（「按顺序持续推进……后续再遇到问题则你自行按照推荐去做」），故 D1–D12 **全部按 §3 的推荐档收口**，无一条改判需要现场等人；其中三条与稿面初判不同、值得点名的自决位是：D2 选**现读**而不是照 `TerminalDefaults` 的构造期快照（理由：G15 让「改档只到当前标签」成为既有事实，走 `NextSession` 等于给安全闸留一个跨标签的洞）；D4 选**逐条答**而不是照写方向的合并（理由：读方向是别人在等自己那一条）；D6 选**一直等到用户答**而不是 TERM.09 那档 5 s 自收（理由：通知可以超时，授权决定不该被时间替用户拍板）。收口与理由随批 1 落地入册 `SPECIFICATIONS.md` §7 裁决 **7.118**、`CHANGELOG.md` **v0.118**，`PLAN.md` 的 CONN.12 行与 M4 行同步改口。实现按 §4 分三批走：批 1＝`term` 层纯逻辑件与其 utest（不含界面）；批 2＝`Session`/`ClipboardOutbox` 应答腿 + 询问钩子 + `itest`；批 3＝新键六处同步 + 设置页那一行与旁注 + 装配接线 + 文档回写与在册收口。
