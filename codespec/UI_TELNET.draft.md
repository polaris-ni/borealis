# Telnet 终端 UI 与连接腿设计稿（`SPEC.FEAT.CONN.06`）

> **状态**：**待评审**（2026-10-11 出稿）。§3 的 D1–D12 每条给推荐档，等人逐条裁决；裁决前**不动代码**（AGENTS §4.1 第 2 条的 UI 件节奏：先出稿评审再实现）。裁决并入 `codespec/SPECIFICATIONS.md` §7 与 `CHANGELOG.md` 后转实现。
> **配套高保真草图**：`codespec/UI_TELNET.draft.svg`（本稿 §2 的 1–7 标号判据即图中圈号，各贴在所属屏旁；SVG 为事实来源，后续改图直接改 SVG）。
> **上游依据**：`SPEC.FEAT.CONN.06` 原文（基础 Telnet 客户端，不含 TN3270；选项协商覆盖 ECHO / SGA / TTYPE；编码按 `SPEC.FEAT.TERM.09` 可配）、裁决 7.6（本地终端 / SSH / **Telnet** 缺省 UTF-8，串口才吃 GB18030）、7.99（`session::CloseReason` 与重连分类表）、7.103（`term::SessionEncoding` 已具备）、7.110 D1①/D11①（类型域追加式扩型的先例 + 写出侧一处行尾折算点）、7.96（平台 TCP 腿的 IPv4 收窄）、7.94（向导两步式与侧栏连接族）。
> **既成地基（本棒只消费，不新建）**：`src/platform/tcp.h` 的 `tcp_connect` / `TcpStream`（POSIX + Win32 两腿已在，`utest_tcp_loopback` 4 例为证）、`session::Connection` 五件、`src/conn/reconnect.{h,cpp}` 的退避环与 `should_reconnect` 分类、`Session` 的构造期 `encoding` / `line_ending` 两枚入参、`ui::connection_wizard` 的三张类型卡与字段集分派。

---

## 0 地基实测（2026-10-11 本沙箱，POSIX 腿 + Python 探针；Windows 腿一律标未验证）

形态不是推演出来的，下列每条都有本机读数（探针是一次性脚本，不入仓；复现口径见 §8）。

| # | 读数 | 对本稿的约束 |
|---|---|---|
| **F1** | 本机**无 telnetd**：`telnetd` 不在 PATH、无 `/usr/sbin/in.telnetd`、23 端口无监听；只有客户端 `/usr/bin/telnet`（GNU inetutils 2.7） | **真服务器腿在本环境不可达**：`etest_telnet_server` 与「真设备/真服务器的协商手感」只能归真机（§6/§7）。但见 F2——本棒可证面比串口那棒**更强**，不弱 |
| **F2** | 环回假服务端可起：`bind(127.0.0.1:0)` + `listen` + `accept`，端口由内核择定；两端逐字节可查 | **整条 IAC 协商腿可在环回上逐字节证**（串口受 F1 只能拿 pty 替身，Telnet 没有这种限制）。故本棒的 itest 不靠任何协议近似 |
| **F3** | 掉线两形态读数**分明**：对端 `SO_LINGER(1,0)` 关 ⇒ 本端 `recv` 回 **ECONNRESET(104)**；对端有序 FIN ⇒ 本端 `recv` 回 **0 字节（EOF）** | 归因两档在既有 `TcpReadOutcome` 上有唯一映射（`Closed`／`Error`，`src/platform/posix/tcp.cpp:84-94` 实测代码），**不需为 Telnet 新增平台档位**（D9 的根据） |
| **F4** | 半关（对端只 `shutdown(SHUT_WR)`）时本端 `recv` 得 EOF，而**写侧仍连续两次 `sendall` 都不报错**（字节进内核缓冲）；本仓 `TcpStream::write` 带 `MSG_NOSIGNAL`（`posix/tcp.cpp:104`），半关写不会 `SIGPIPE` 杀进程 | **写侧不可当掉线判据**：掉线一律从读侧归因。写失败要等内核 RST 回来才显形，所以「已发出」不等于「对端收到」，界面上不得据写成功报「已送达」 |
| **F5** | 一次 `recv` 里可同时混排三条命令 + 文本：`[255,251,1, 255,252,3, 255,253,24, 'login: ']` | 解析必须**按字节走状态机**，不许按行、按块、按「一条命令一次读」切（判据 5，`utest_telnet_negotiation` 逐字节喂的根据） |
| **F6** | IAC 命令**确实跨 read 断裂**：两端 `TCP_NODELAY` + 写读交替时，一条 `IAC WILL ECHO` 以 `[255] [251] [1]` 三次读各 1 字节到达 | 状态机必须可暂停／续跑，且**未完成的命令段绝不进网格**——否则半个命令的 0xFF/0xFB 会被当数据解码成乱码格（判据 5 的另一半） |
| **F7** | 数据段里的字面 0xFF 以 `FF FF` 转义原样往返；子协商载荷里也可嵌转义（`IAC SB TTYPE 0 'X' FF FF 'Y' IAC SE` 完整到达） | 反转义是逐字节的，且要同时作用于**命令段与 SB 载荷**两处；发送侧本仓必须自己把数据里的 0xFF 写成 `FF FF`（D11 的契约内容） |
| **F8** | **参考客户端连上后一个字节都不发**：GNU inetutils 2.7 在 pty 与 `stdin=DEVNULL` 两跑下，等满 2 s 环回服务端均收 0 字节 | 「全被动应答」是现实可行的形态（不发起也能连），但同时证明**双方都等就双双沉默**：对端也是「等对方先动」的实现时，本端不主动 `DO` 就永远拿不到 ECHO/SGA 的结论（D3 的根据） |
| **F9** | 建连失败两态可读：拒连 ⇒ errno **111 ECONNREFUSED**；对不可路由地址（`10.255.255.1:23`）以给定 0.4 s 档**超时返回**（本仓 `tcp_connect` 走非阻塞 + `poll(timeout)`，`posix/tcp.cpp:235-252`） | 两类建连失败都落在既有腿上，`CloseReason::DialNetwork` 可直接归因；超时档是调用方给的一个整数 ⇒ 需要定它从哪来（§5 装配口径） |
| **F10** | `localhost` 经 `getaddrinfo(AF_INET)` 可解析；本仓 `tcp_connect` 的 hints **钉死 AF_INET**（`posix/tcp.cpp:223`） | **纯 IPv6 主机名连不上**——这是裁决 7.96 的既定收窄，本棒沿用不改（§7 登记为在册代价，不另开一档） |
| **F11** | 本仓**两条 TCP 腿都不设 `TCP_NODELAY`**（全仓 grep 无匹配）；环回上 ACK 即时，无从复现 Nagle 与 delayed-ACK 的最坏叠加 | 交互式终端是逐键往返的，Nagle 的有无直接影响手感。「不设 NODELAY 时最坏多吃一档 40 ms 级延迟」属**推断／未验证**（AGENTS §4.2 第 11 条），真机走查实测；据此立 D6 |
| **F12** | 本沙箱无 MSVC、无 Win32 头可编 | §4 的 Win 侧一切说法（含 `src/platform/win/tcp.cpp` 既有件与本棒可能的追加）标**推断／未验证**（AGENTS §4.2 第 11 条），待真机复测回写 |
| **F13** | 空闲读超时不吞数据：无写者时带超时的读按 Timeout 档返回，之后再写的数据仍完整可读 | 读循环可以用「带超时读」做心跳与终止位检查（但等待不许占着写侧要取的锁——串口 F11 那条锁纪律在此同样成立） |

---

## 1 范围与边界

**本期交付**：

1. Telnet **档案**进连接模型（`ConnectionType` 第四档 + `TelnetProfile`）与**向导**第四张类型卡（`SPEC.FEAT.CONN.07` 的分步表单自此有第四型）。
2. **IAC 协商状态机**（纯逻辑、逐字节可断言）：`ECHO`(1) / `SGA`(3) / `TTYPE`(24) 三项的发起、应答、子协商载荷与 `FF FF` 转义（`CONN.06` 原文点名的三项）。
3. **Telnet 会话连接腿**：`tcp_connect` → 协商 → 净数据读线程 → 写 → 关闭，接进既有 `session::Connection` 抽象（AGENTS §4.5 第 23 条：socket 知识只在 `src/platform/`，本棒沿用既有 `platform::tcp` 不新增平台件）。
4. **断开重连**：Telnet 的掉线归因进既有 `session::CloseReason`，复用 `src/conn/reconnect.{h,cpp}` 的分类与退避（不新写一套重连环，口径同 SSH / 串口两腿）。
5. **编码按 `TERM.09` 可配**：会话构造吃既有 `make_session_encoding(settings)`（`src/main.cpp:188`），缺省 UTF-8（裁决 7.6）。

**不本期交付**（§7）：TN3270（需求原文即排除）、`NAWS` 等其余选项、本地回显、IPv6、真 telnetd 的 etest、quick connect 语法的 Telnet 分支、逐标签连接类型图标、真机走查。

**关键前提（口径不是本稿新造的）**：

- **Telnet 没有任何设置域键**：`include/borealis/config/settings.h` 的 `connection` 域只有 `local_shell` / `startup_directory` / `ssh.*` / `serial.*` / `session_log*`，grep 全仓的 "telnet" 只命中注释与文档。本棒是否新增键由 D5 定，**推荐不新增**（缺省端口的先例是向导里硬编码的 22：`src/ui/connection_wizard.cpp:226`）。
- 类型枚举的落盘形态是一张 `EnumName` 表（`src/config/store.cpp:69-70`，现列 `local` / `ssh` / `serial`）⇒ 追加 `telnet` 一档要同时补这张表，装载侧对表外名的容错沿用既有那档（裁决 7.26④ 的宽容口径）。
- `profile_views` 的 `validate_draft` 已有 `EmptyHost` / `PortOutOfRange` 两档问题码（`src/conn/profile_views.cpp:84-90`）⇒ Telnet 型的校验**零新增问题码**，只在四向分派里补一条腿。
- `platform::TcpStream` 只有两个实现（`PosixTcpStream` / `WinTcpStream`），全仓没有第三方派生类或测试替身 ⇒ 若 D6 选「加一枚 `set_nodelay()`」，改面就是两腿 + 一处调用，不牵动 `utest_tcp_loopback`。

---

## 2 草图与判据对应

草图五屏（A 向导第四型 / B 上线协商时序 / C 跨分片与转义 / D 会话在途与掉线两档 / E 侧栏与不在本期面），图中圈号 1–7 即下表判据；屏 B/C 的字节序列是**实测读数**（F5/F6/F7），屏 D 的注记档位是示意：

| 标号 | 判据 | 对应需求 |
|---|---|---|
| 1 | 向导步骤 0 的类型卡自此**四张**（Local / SSH / Serial / **Telnet**）；步骤 1 的字段集由类型卡决定，Telnet 型**不显示** SSH 的 user/认证/known_hosts 三栏，也不显示串口九栏 | `CONN.07`「按连接类型分步表单」（D1/D2） |
| 2 | Telnet 字段集三行：展示名、主机、端口（端口留空＝23，与 SSH 留空＝22 同一条硬编码口径）；校验只走 `validate_draft` 既有的 EmptyName / EmptyHost / PortOutOfRange 三档 | `CONN.06`（D2） |
| 3 | 连上即发两条**发起**命令：`IAC DO ECHO`、`IAC DO SGA`（各 3 字节）；`TTYPE` 不主动发起，只在收到 `IAC DO TTYPE` 时答 | `CONN.06`「协商覆盖 ECHO / SGA / TTYPE」（D3） |
| 4 | **应答表**一处一份：`WILL x` → 我方支持则 `DO x` 否则 `DONT x`；`DO x` → 我方支持则 `WILL x` 否则 `WONT x`；`IAC DO TTYPE` → `WILL TTYPE` 紧跟 `IAC SB TTYPE IS "XTERM" IAC SE`；表外选项一律拒绝，且**同一立场只答一次**（对端重复问不得再答，防协商风暴） | `CONN.06`（D3/D4） |
| 5 | 净数据与命令段的分界：`[FF][FB][01]` 三次读合成一条命令、一条读里混排三命令 + `login: ` 都照常处理；`FF FF` 还原成**一个** 0xFF 字节交给网格；未完成的命令段**绝不进** `on_bytes` | `CONN.06`（D11，实测 F5/F6/F7） |
| 6 | 会话在途：视口按 `terminal.encoding` 解码（缺省 UTF-8，裁决 7.6）；键入 Enter 落地为 **CRLF**（走 `Session::write_out()` 那唯一一处折算点，裁决 7.110 D11① 的现成品），`SGA` 的结论不改字节 | `CONN.06`「编码可配」+ 基础可用性（D5/D8） |
| 7 | 掉线两档**分开显示**：读侧 EOF ⇒ `RemoteExit`，「远端已退出」一句、**不进重拨环**、网格保留供回看；读侧 ECONNRESET/错误 ⇒ `LinkLost`，进既有退避环，浮层两态沿用 `codespec/UI_RECONNECT.draft.md` 已裁形态 | `CONN.06` 隐含 + `WS.05`（D9，实测 F3） |

---

## 3 待决（D1–D12，等人逐条裁决）

| # | 待决 | 选项 | 推荐 | 理由与代价 |
|---|---|---|---|---|
| **D1** | Telnet 是否进**档案模型** | ① `ConnectionType` 追加 `Telnet` + 新 `TelnetProfile{host, port=23}`，`Profile` 加 `telnet` 子结构，JSON 读写与 `EnumName` 表一并追加；② 不入档案，只做一次性入口 | **①** | ② 与侧栏/最近连接/收藏那一族全部接不上，每次重连重敲主机名；且 `CONN.03` 的类型域本就把四类连接并列。代价＝追加式扩型（照裁决 7.110 D1① 串口那一条的先例）：公共头 `include/borealis/conn/profile.h` 只加一档枚举值与一个子结构，**不动既有成员与可见性**（AGENTS §4.1 第 6 条）；`store.cpp` 的两处按型分派、`profile_views` 的三处按型分派、`utest_config` 的往返与坏字段回退都得补第四腿 |
| **D2** | 向导第四型的字段集 | ① 三行（展示名 / 主机 / 端口）；② 再加用户名与口令（Termius 式「自动登录」）；③ 再加行尾与编码两行 | **①** | ② 要造的是一条**没有消费方的凭据链**：Telnet 没有认证协议，登录提示由远端程序给，「存进档案的口令」只能靠「自动敲 login: / Password: 序列」兑现——那是脚本功能，`CONN.06` 原文没有，且会牵动 `CONN.09` 的凭据存储与明文审计（本棒不碰）。③ 的编码已由全局档可配（D5），行尾由 D8 定固定档，存进档案只复制「改设置不回溯在档」那个坑（串口 D4① 的代价面）。代价：三张卡变四张卡的行宽要不要换行属绘制判据，无头断不了，留 §6 走查 |
| **D3** | 协商发起方 | ① 连上即发 `DO ECHO` + `DO SGA`（一次），`TTYPE` 被动应答；② 全被动应答（只答不发）；③ 主动全套（再带上我方选项的 `WILL`） | **①** | F8 实测参考客户端**一个字节都不发**——② 因此可行，但它同时证明「双方都等」的组合会双双沉默，而 ECHO 缺省态是「未定」，不发起就永远不知道对端会不会回显。③ 要我方 `WILL` 一个真实选项，而本端能真正执行的只有 `TTYPE`（且它只该被问时答），凭空 `WILL` 就是发对端不认的声明。代价：多两个上线字节；个别老设备对未请求的 `DO` 的反应属**推断／未验证**（本沙箱无 telnetd，F1），走查第 ⑴ 条实测 |
| **D4** | 表外选项的处置与「答几次」 | ① 一律拒绝（`DO x`→`WONT x`；`WILL x`→`DONT x`；`SB x … SE` 照吞到 `SE` 不处理），且每个选项**同一立场只答一次**（状态位记住已答，对端重复问不再答）；② 表外一律沉默不回；③ 每次收到都重答一遍 | **①** | ② 会让对端一直等在协商里（RFC 854 的「拒绝未支持项」正是为终止这场等待）；③ 在对端有重试逻辑时形成**应答风暴**——两个方向都靠「已答」位封死。本端能真正执行的只有三项，其余拒绝是唯一诚实的立场。代价：对把 `WONT` 当冒犯的极端老设备不友好（本环境无从取证，标**推断／未验证**）；「已答」状态位要能随重拨**清零**（重连后同一台服务器可能改口），这条得进用例 |
| **D5** | 设置域是否新增键 | ① 不新增，端口缺省硬编码 23（同 SSH 的 22）、编码吃既有 `terminal.encoding`；② 新增 `connection.telnet.{port,encoding}` 两键并同步进 schema + 反向核对表 | **①** | ② 要为「新建档案时缺省填什么」这一件事开两枚键，走一整套 schema/装载/核对表/往返（串口那棒开 `flow` 键是因为**需求原文点名流控**，`CONN.06` 没点名 Telnet 的默认端口）；且需求说的「编码按 `TERM.09` 可配」的入口本来就是 `terminal.encoding`（裁决 7.6 明写 Telnet 缺省 UTF-8 ＝ 全局档，串口才有类型缺省 GB18030）。代价：设置页没有「Telnet 默认端口」一项，端口只能在向导里手敲——与 SSH 型现状同档，不算倒退 |
| **D6** | 交互延迟与 `TCP_NODELAY` | ① 给 `platform::TcpStream` 追加一枚 `set_nodelay()`，只在 Telnet 腿呼；② 本期不动平台腿，走查后再说；③ 在 `tcp_connect` 里全局设上 | **①** | F11：两条腿都不设 NODELAY。逐键往返的交互终端与隧道转发泵的需求相反——③ 会顺手改掉既有隧道的线上行为（无谓的风险面，违背「一个改动一个意图」）。① 是「本棒立即要用」而非「将来才用」：`TcpStream` 只有两个实现（§1 前提），改面＝两腿各一次 `setsockopt` + Telnet 腿一处调用，隧道腿不呼 ⇒ 既有转发逐字节不变。② 的问题是明知有延迟缺陷还把它推给走查。代价：Win 腿未经编译验证（F12）；`set_nodelay()` 是抽象类上的**追加纯虚函数**，两腿都必须实现否则编译不过（POSIX 腿本沙箱可编，Win 腿待真机） |
| **D7** | 对端回 `WONT ECHO`（不回显）时的处置 | ① 不做本地回显：结论只记在状态位里，不改行为、不出提示；② 连接腿把已写字节回环进自己的 `on_bytes`（真本地回显）；③ 在界面提示「对端不回显，输入不可见」 | **①** | `CONN.06` 只要求「协商覆盖 ECHO」，不要求本地回显。② 要在会话侧新造一条「输入可见」通道：转义序列（方向键不该被回显成字面 `^[[A`）、CR 的回显要补 LF、编码半字符要各自处理——一处没对齐就是半截实现（AGENTS §4.1 第 3 条）。③ 是给一个本棒不解决的事实配一句常驻措辞。代价：真遇到 `WONT ECHO` 的服务器，用户看不见自己敲的字（busybox/主流 telnetd 都 `WILL ECHO`，故属边缘）；走查第 ⑶ 条如实实测，届时按到货的消费方单开，不在本稿预建 |
| **D8** | 发送行尾 | ① 装配层给 Telnet 会话传 `term::LineEnding::CRLF`（**固定档，不可配**），复用 `Session::write_out()` 那唯一一处折算点；② 档案存 `line_ending` 字段可配；③ 不折算（恒 CR，与本地/SSH 同档） | **①** | Telnet NVT 的行结束就是 CR LF（RFC 854 口径）。③ 赌对端宽容，而本仓在串口侧已定过同一纪律：「不许指望规程改写字节，自己拼」（串口 F8）。② 的需求不存在——`CONN.06` 没提行尾，行尾可配是 `CONN.05` 那条。① 的成本是一枚构造入参，且折算点与四条锁用例都在（裁决 7.110 D11① 落的现成品），不算新抽象。代价：固定档意味着个别只认 CR 的老设备需要改档才能用（推断，走查第 ⑵ 条）；真要可配时消费点已在，不必现在预建 |
| **D9** | 掉线归因与重连 | ① 读侧 EOF→`RemoteExit`（不重拨）、读侧 `Error`（含 ECONNRESET）→`LinkLost`（重拨）、建连失败（拒连/超时/DNS 失败）→`DialNetwork`（重拨）；② 照串口那档口径：EOF 也归 `LinkLost` 进重拨环 | **①** | Telnet 是网络登录会话：telnetd 在远端进程 `exit` 后关 TCP 是**正常结束**，`should_reconnect(RemoteExit) == false`（`src/conn/reconnect.cpp:15-29` 实测分类表）正是「登录退出后不要无限重拨」。串口那棒把 EOF 归 `LinkLost` 是因为串口根本没有「远端进程」这个概念（裁决 7.110 D7① 第 1 条代价口径）——**两棒的分野是协议语义，不是抄漏**。代价：本条与 7.110 D7① 故意不同，须在裁决里写清分野，否则后人会误读成不一致；F4 的半关形态（recv 得 EOF 但写侧仍成功）也归这一档，界面按「远端已退出」处理 |
| **D10** | `NAWS`（窗口尺寸协商，选项 31）要不要本期顺带 | ① 不做：`TelnetConnection::resize()` 是**有意的空操作**（串口那棒同形态先例）；② 做：`resize()` → `IAC SB NAWS <rows> <cols> IAC SE`，并处理 >65535 的 `0xFF` 转义 | **①** | `CONN.06` 原文点名三项选项，没点 NAWS；需求先行（AGENTS §4.1 第 1 条）不自行扩面。② 的诱惑是「反正就在同一台状态机上」，但它要新增一档**客户端发起**的子协商 + 服务端可能回带的形态，判据也得跟着扩。代价：分屏/大窗口下远端全屏程序（`vi`、`top`）按 80×24 重排——嵌入式串口式控制台场景不受影响，但这条要**如实写进验收与走查**（走查第 ⑷ 条），不是「做了但没说」 |
| **D11** | TU 拆分与协商件的契约形态 | ① 纯逻辑 `conn/telnet_negotiation.{h,cpp}`：输入字节段 → 输出「净数据段 + 待回字节 + 选项状态位快照」，无 socket、无 `au::` 类型；② 合成一件 `conn/telnet_connection.{h,cpp}`，解析器以私有成员藏在腿里 | **①** | 需求核心（ECHO/SGA/TTYPE 协商）恰好就是**逐字节可断言**的那一档：转义、跨分片、混排、子协商、拒绝表、重复请求不重答，六类用例全都不需要 socket。串口那棒（`serial_model` / `serial_connection`）与 SFTP 那棒（`sftp_model` / `sftp_client`）都是同一分法，`conn` 层的既有纪律（AGENTS §4.4 第 20 条）。② 会让这些断言全都要经过环回 socket 才能跑，测试面变脆。代价：契约要写准——「状态机不许吞掉未完成的命令段」「SB 载荷里的 `FF FF` 也要反转义」（F7）都得是显式断言，而不是腿里的隐式习惯 |
| **D12** | 验证形态 | 三层：① `utest_telnet_negotiation`——逐字节喂的解析/转义/应答表/发起序列/不重答；② `itest_telnet_connection`——环回假服务端（真 socket）证双向协商字节、净数据进 `on_bytes`、EOF 与 ECONNRESET 两档归因、拒连档、可重拨腿；③ `etest_telnet_server`——真 telnetd，本沙箱按 F1 **写不出来**，登记在册 | 如述 | F2 使 ② 的强度**高于**串口那棒的 pty 替身：协议两端都是真 socket，无需任何近似。同时不拿环回假服务端冒充真服务器宣布「与设备对接可用」（F1 挡住的就是这一档）。代价：假服务端脚本要落在测试 TU 里（同 `utest_tcp_loopback` 的环回自建自连形态），协商用例的服务端一侧是写死的应答表，对真 telnetd 的行为覆盖以走查与 etest 补齐 |

---

## 4 文件布局 / TU 拆分（裁决后按此开工）

```
include/borealis/conn/profile.h        修改 —— ConnectionType 追加 Telnet；新 TelnetProfile {host, port=23}；
                                                Profile 加 telnet 子结构（D1①，追加式，不动既有成员/可见性）
src/conn/telnet_negotiation.{h,cpp}    新增 —— 纯逻辑 IAC 状态机（无 socket、无 au::，可无头逐字节单测）：
                                                FF FF 转义/反转义、命令段与数据段分界、SB…SE 载荷、
                                                ECHO/SGA/TTYPE 应答表、上线发起序、同一立场只答一次（D4①/D11①）
src/conn/telnet_connection.{h,cpp}     新增 —— 私有头（D11①，口径同 serial_connection.h）：
                                                session::Connection 的 Telnet 实现 = platform::TcpStream +
                                                读线程 + telnet_negotiation + 归因表 telnet_close_reason()
                                                + 复用 conn/reconnect.h 退避环（D9①）；resize() 按 D10① 是
                                                有意的空操作
src/conn/profile.cpp                   修改 —— telnet 子结构的 JSON 读写与确定性 id（只追加键）
src/conn/profile_views.{h,cpp}         修改 —— validate_draft / draft_to_profile / profile_to_draft 补第四腿
                                                （问题码零新增：EmptyName + EmptyHost + PortOutOfRange，D2①）
src/config/store.cpp                   修改 —— EnumName 表追加 {"telnet", …}；profiles 的 telnet 段读写；
                                                装载侧对表外 type 名沿用既有容错档
src/platform/tcp.h                     修改（D6①）—— TcpStream 追加 set_nodelay()（纯虚，两腿各实现）
src/platform/posix/tcp.cpp             修改（D6①）—— setsockopt(IPPROTO_TCP, TCP_NODELAY)
src/platform/win/tcp.cpp               修改（D6①）—— 同上一枚；**未经 MSVC 编译验证**（F12）
src/ui/connection_wizard.{h,cpp}       修改 —— 类型卡四张；Telnet 字段集三行；端口留空＝23（同 22 的先例）
src/main.cpp                           修改 —— 装配：Telnet 会话构造吃 make_session_encoding(settings)（D5①）
                                                与固定 LineEnding::CRLF（D8①）；tcp_connect 的超时档取一常量
                                                （口径同 tunnel_client 的 kLocalConnectTimeoutMs）；连接与
                                                重启分流补 Telnet 腿
src/ui/settings_i18n.cpp               修改 —— connections.type.telnet 与 Telnet 字段名/留痕的新词条族
src/ui/settings_catalog.cpp            **不动** —— 本棒不新增设置域键（D5①），故反向核对表零改；
                                                「写出的 schema 键必须逐行有控件」那道闸因此不受影响
tests/unit/utest_telnet_negotiation.cpp        新增 —— D12①，逐字节喂的六类断言
tests/integration/itest_telnet_connection.cpp  新增 —— D12②，环回假服务端 + 真 socket
tests/unit/utest_config.cpp            修改 —— telnet 档案 JSON 往返 + 坏字段逐项落回
tests/unit/utest_profile_views.cpp     修改 —— 第四腿的校验顺序与草稿往返
tests/integration/itest_connection_wizard.cpp  修改 —— 四张类型卡真点 + Telnet 三行字段集 + 端口留空＝23
tests/unit/utest_tcp_loopback.cpp      修改（D6①）—— set_nodelay 的环回证人（只证调用不报错与选项回读）
```

> 不改既有公共头的可见性与既有成员顺序（AGENTS §4.1 第 6 条）。框架侧**不预登记新缺口**：向导所需控件（`Dialog`/`Row`/`Column`/`TextInput`）与退避环、重连浮层、`TcpStream` 全部有已落地消费先例；`platform::TcpStream` 追加成员属**本仓自己的平台抽象**（不是 Aurora 的面），按 AGENTS §5 第 3 条的同一口径处理。若实现期撞到新的框架形态限制，按 AGENTS §5.1 类别分流另立 G 号并出任务书，不在本稿预写。

---

## 5 数据流与线程模型

```
config::Store（profiles 域：telnet 子结构，D1①；terminal.encoding 全局档，D5①）
      │
装配层（main.cpp）
      ├─ 连接：conn::TelnetConnection(profile.telnet, reconnect_policy())   ← 两参：档案 + 退避档（D9①）
      │        │   编码与发送行尾**不在连接腿**：装配层把它们交进 `Session` 的构造入参（D5①/D8①）
      │        ├─ start()：platform::tcp_connect(host, port, kConnectTimeoutMs) 在后台线程上跑
      │        │          └─ 成功后立刻 stream->set_nodelay()（D6①，只有这条腿呼）
      │        ├─ 上线发起：negotiation 给出待回字节（IAC DO ECHO / IAC DO SGA，D3①）
      │        ├─ 读线程循环：stream->read(buf, timeout) ── 净数据 ──on_bytes──► Session（现有链：
      │        │                 解码→状态机→网格→副本）；命令段与半个命令**绝不进** on_bytes（D11①）
      │        │                 （协商应答经同一线程的 stream->write 发出）
      │        ├─ read 档 Closed(EOF) ──on_closed(RemoteExit)──► should_reconnect == false（D9①）
      │        └─ read 档 Error(ECONNRESET) ──on_closed(LinkLost)──► 既有退避环 ──► ReconnectProgress
      │                                                                     └─ 视口浮层两态（判据 7）
      ▼
UI（connection_wizard 第四型三行 / 侧栏档案行 / 重连浮层沿用已裁形态）
```

- 读线程纪律照 `ssh_dial` / `forkpty_connection` / `serial_connection` / `tunnel_client` 四处先例：建连与读在**同一后台线程**，`start()` 立即返回，UI 线程零网络 IO（AGENTS §4.5 第 25 条）。
- **锁的持有面**：socket 锁内**不做带超时的等待**——串口 F11 那条「本沙箱对持锁休眠的秒级惩罚」读数在 TCP 腿同样适用，真机上也违背架构 §3.4「锁内不做 IO」。做法口径：读循环取「一次带超时读」时不与写侧争同一把长持有锁，或降到 0 档非阻塞探测 + 锁外短等；判据不变——**UI 线程的 `write()` 不得排在一个读分片之后**（实现期以变异自证：把带超时读塞进锁内必须让某条用例转红）。
- **协商状态只在读线程**：`telnet_negotiation` 的实例由连接腿私有持有，不跨线程读它；给界面的只有「会话是否已就绪」与 `ReconnectProgress` 那两枚既有快照通道（latest-value，AGENTS §4.5 第 25 条）。选项状态位（ECHO 开/关等）本期**没有 UI 消费方**（D7①），故不投快照、不落浮层——这是「不加将来才用的抽象」的直接落点。
- 发送方向：`Session::send_text` / `send_bytes` → 编码 → `write_out()` 一处行尾折算（CRLF，D8①）→ `TelnetConnection::write()` → **数据段里的 0xFF 必须转义成 `FF FF`** 再上线（F7，`negotiation` 的写出侧一半）。转义发生在连接腿而不是 `Session`：SSH/本地腿没有这层规程，把协议细节渗进会话层就违背分层。
- 掉线判定**一律从读侧**（F4：半关时写侧仍成功，写成功不等于对端收到）。`write()` 返回 false 只记日志并置终止位，不据此出「远端已退出」的措辞。

---

## 6 验收判据映射

- **协商覆盖 ECHO / SGA / TTYPE**（需求原文第一条）：`utest_telnet_negotiation` 锁三件事——上线发起的两条命令逐字节、应答表逐档（`WILL ECHO`／`WONT ECHO`／`DO SGA`／`WONT SGA`／`DO TTYPE` → `WILL TTYPE` + `SB TTYPE IS "XTERM" SE`）、表外选项的拒绝与**同一立场只答一次**（D4①，含重拨后清零那一档）；`itest_telnet_connection` 在真 socket 上证对端确实收到那串字节（口径同串口那棒的「对端逐字节收到」读数）。
- **基础客户端可用**（连接/收发/关闭）：`itest_telnet_connection` 证「环回假服务端发净文本 ⇒ 网格自第 0 格起逐码点对齐」与「本端写一条命令 ⇒ 服务端收到的字节与预期逐字节全等」两向；`tcp_connect` 失败（拒连 errno 111，F9）证归因落 `DialNetwork` 且不假装会话已建立。
- **编码按 `TERM.09` 可配**：装配层把 `make_session_encoding(settings)` 交进 Telnet 会话构造（与本地/SSH 同一枚现成件，D5①），`itest_telnet_connection` 以对端写多字节序列 ⇒ 网格落子的形态证解码链吃的是该档；裁决 7.6 的「Telnet 缺省 UTF-8」在装配折算处锁住，**不新增设置键**。
- **发送行尾 CRLF**（D8①）：`utest_session` 既有四条折算用例覆盖折算点本身；本棒加一条「Telnet 会话的 Enter 落 CRLF 且键表面仍产一个 CR」的组装断言，`itest_telnet_connection` 在收端锁最后半段。
- **掉线与重连**：`itest_telnet_connection` 证 EOF→`RemoteExit`→**不**进重拨环（分类表零改动）、ECONNRESET→`LinkLost`→投 attempt≥1 的环内快照；重拨成功回到读循环时擦快照（既有三处投递时机，裁决 7.99）。
- **跨分片与转义**（判据 5）：`utest_telnet_negotiation` 以「同一段字节流，分别按 1 字节 / 随机分片 / 整段三种喂法喂入，净数据与待回字节必须逐字节全等」锁 F5/F6/F7 的形态等价性——这条是本棒最容易出真缺陷的地方，也是把它拆成纯逻辑件（D11①）的全部理由。
- **界面腿（判据 1/2）**：`itest_connection_wizard` 补无头真点击用例——四张类型卡真点、Telnet 型不画 SSH 的 user/认证/known_hosts 三栏也不画串口九栏、三行字段集产值进草稿、端口留空＝23、`validate_draft` 三档问题码逐档上屏且不关框。四张卡的**行宽好不好看**属绘制判据，无头断不了，留下面走查第 ⑸ 条。
- **测试读数口径**：交付时以 `ctest --test-dir build -E etest_` 的**项数与 runner 全案数**结项（AGENTS §4.4 第 21 条不认「测了很多」），变异注入按第 22 条自证非空转——本棒至少要证两条承重：摘掉 `FF FF` 反转义 ⇒ 转义用例转红；把未完成的命令段漏进 `on_bytes` ⇒ 等价性用例转红。
- **真机走查**（AGENTS §4.4 第 33 条，未走查不得称「可用」）：⑴ 真 telnetd 上发起 `DO ECHO`/`DO SGA` 有没有异常反应（F8 只证参考客户端沉默，不证服务器）；⑵ 只认 CR 的老设备在固定 CRLF 档下是否可用（D8 的代价面）；⑶ 遇到 `WONT ECHO` 的服务器时「看不见输入」的可接受度（D7 的代价面）；⑷ 大窗口下远端全屏程序按 80×24 重排的可接受度（D10 的代价面）；⑸ 四张类型卡的行宽与文案；⑹ 开 `TCP_NODELAY` 前后的逐键手感差值（D6 的实测补账，本沙箱无从复现）。

---

## 7 本期不做（延后 / 在册）

- **TN3270**：`CONN.06` 原文即排除。
- **ECHO / SGA / TTYPE 之外的全部选项**：`NAWS`(31，见 D10)、`BIN`(10)、`LFLOW`(19)、`NEW-ENVIRON`(39)、`GA`、`EOF`(237)、`SUSP`(238)、二进制传输透传等一概不实现；表外一律走 D4① 的拒绝档。
- **本地回显**（D7① 的代价面）：对端 `WONT ECHO` 时不替它回显，也不出提示。
- **IPv6**（F10）：`tcp_connect` 的 hints 钉死 AF_INET，纯 IPv6 主机名连不上——裁决 7.96 的既定收窄，本棒沿用不改，另开需换平台抽象。
- **真 telnetd 的 etest**（F1）：`etest_telnet_server` 待有可用 telnetd 的环境补，与 `etest_serial_device`、待 sshd 的 CONN.08/10 两处欠账同档登记。
- **Windows/MSVC 腿验证**（F12）：本沙箱无 MSVC；`src/platform/win/tcp.cpp` 既有件与本棒的 `set_nodelay` 追加均未经编译验证。
- **quick connect 的 Telnet 分支**：`conn::parse_quick_connect` / `quick_connect_profile` 现属 SSH 语义（`src/conn/profile_views.cpp:172-211`，裁决 7.94 落的那件，建的是 SSH 档案）。给 Telnet 加语法要先定「前缀词法」，属新交互契约，本棒不扩面（在册，随 `CONN.07` 的 snippets 余腿一并看）。
- **逐标签连接类型图标**：随 `SPEC.FEAT.WS.04` 余腿单开（口径同裁决 7.110 D12①）。代价：Telnet 标签本期与本地/SSH 标签画得一样，识别靠档案名。
- **终端类型可配**（TTYPE 报什么串）：`XTERM` 收在 `telnet_negotiation` 的一枚常量。理由是本仓没有 `TERM` 类型设置域（§1 前提），而 `CONN.06` 只要求「协商覆盖 TTYPE」，不要求报什么。代价：设备侧若按别的类型发序列则解释不合（走查第 ⑷ 条一并看）。
- **`CR NUL` 数据段转义**（RFC 854 要求裸 CR 后跟 NUL）：不实现。本沙箱无真服务器可判其必要性，且主流 telnetd 宽容——属**推断／未验证**（AGENTS §4.2 第 11 条），`etest_telnet_server` 落地时取读数再决定。
- **保活（keepalive）与半开检测**：`SO_KEEPALIVE`、应用层心跳都不做。F4 已说明写侧在半关时不报错，故「对端静默死亡」在本期表现为「一直等」，直到用户自己关标签——如实登记，不预建检测。
- **设置域键**：本棒不新增（D5①），故 `settings_catalog` 与 `utest_settings_catalog` 那张核对表零改动。

---

## 8 裁决记录与探针复现

- **2026-10-11 出稿**：§0 的地基实测已跑完（F1–F13），§3 的 D1–D12 待逐条裁决。**裁决前不落 `src/` 代码**（AGENTS §4.1 第 2 条）。
- **本稿最需要人判的三条**（其余多为形态细节）：D6（要不要动 `platform::TcpStream` 加 `set_nodelay`——它把一条「本棒立刻要用」的延迟修正压在既有平台抽象上）、D7（对端不回显时本期不做本地回显，用户可能看不见自己敲的字）、D10（不做 NAWS，大窗口下远端全屏程序按 80×24 重排）。
- 探针复现口径（一次性脚本，不入仓，全在沙箱临时目录）：
  - `telnet_probe.py`：环回假服务端起停（F2）、协商命令与服务端文本的到达形态、客户端发起字节被对端逐字节收到（判据 3/4）、`FF FF` 在数据段的往返（F7）、有序 FIN 的 EOF 形态（F3 的一半）、空闲读超时不吞数据（F13）。
  - `telnet_probe2.py`：逐字节 `recv` 下的命令断裂（F6 的弱形态）、拒连 errno 111（F9）、`localhost` 经 AF_INET 解析（F10）、无协商直接灌数据 burst（假服务端可纯数据）。
  - `telnet_probe3.py`：`TCP_NODELAY` + 写读交替下 `[255] [251] [1]` 三次读各 1 字节（F6 的确证）、对不可路由地址的 connect 以 0.40 s 超时返回（F9 的另一半）。
  - `telnet_probe4.py`：`SO_LINGER(1,0)` 的 **ECONNRESET(104)** 读数（F3 的另一半）、半关后写侧仍连续成功（F4）、一条 `recv` 混排三命令 + 文本（F5）、`SB … FF FF … SE` 载荷（F7）、参考客户端（GNU inetutils 2.7）连上后 2 s 内**零字节**（F8，pty 与 `stdin=DEVNULL` 两跑一致）。
  - **读数只在本沙箱成立**：换到真 telnetd / 真设备后 F8（对端是否先动）、D8（行尾档是否被接受）、D10（尺寸不协商的实际观感）三档须重取并回写本稿。
- 实现分批（裁决后按此走，与串口那棒的三批同构）：**批 1**＝档案模型（`profile.h` / `store.cpp` / `profile_views` / `utest_config`）+ `conn/telnet_negotiation` 纯逻辑件与其 `utest`；**批 2**＝`conn/telnet_connection` + `platform::TcpStream::set_nodelay`（按 D6）+ 环回 `itest`；**批 3**＝向导第四型 + `main.cpp` 装配 + `itest_connection_wizard` 补例 + 词条族。
