# Telnet 终端 UI 与连接腿设计稿（`SPEC.FEAT.CONN.06`）

> **状态**：**评审已收口（2026-10-11）**——§3 的 D1–D12 经人裁决：**D6 / D7 按推荐档，D10 改判「本期顺带做 NAWS」**，其余九条（D1–D5、D8、D9、D11、D12）按稿面推荐档收口（稿里每条都已写明推荐档的理由与代价，人未改判）。据此并入 `codespec/SPECIFICATIONS.md` §7 裁决 **7.112**，转实现按 §4 布局分三批走（批 1 档案模型 + 协商纯逻辑件 → 批 2 连接腿 + 平台 `set_nodelay` → 批 3 向导第四型 + 装配）。
> **D10 改判的连带回写**（本稿六处已同步改口）：§2 新增判据 8、§3 D10 行、§4 的 `telnet_negotiation` 与 `telnet_connection` 两行（`resize()` 自此**不是**空操作）、§5 数据流多一条尺寸下发腿、§6 新增 NAWS 验收条目与走查第 ⑷ 条改口、§7 把 NAWS 移出「不做」清单。
> **实现期的口径更正（裁决 7.113，2026-10-11）**：批 1 落地时按 §0 新增的 **F14** 实测读数发现，本稿原写的 NAWS 载荷次序「`<rows hi lo><cols hi lo>`」**与线上格式相反**——RFC 1073 §2 的语法行与 §6 的例子（80 列×24 行 ⇒ `0 80 0 24`）、加上本机 GNU inetutils 2.7 客户端在 20 行×60 列 pty 上实测发出的 `FF FA 1F 00 3C 00 14 FF F0`，三者一致指向**宽（列）在前、高（行）在后，各大端两字节**。代码按实测落（`utest_telnet_negotiation` 的组包用例锁住），本稿 §2／§3／§5／§6 一并照此改口（AGENTS §4.2 第 12 条：冲突以代码运行时为准）。同一条更正顺手改掉 §7 把 RFC 1073 说成「四字节宽度扩展」的错引——RFC 1073 就是 NAWS 本体，只有 `2+2` 字节形态。
> **实现进度**：**三批全部落地**（2026-10-11。批 1 两笔提交＝档案第四型进 `profile.h`/`store.cpp`/`profile_views`，与 `conn::telnet_negotiation` 纯逻辑件及其 `utest`；批 2 两笔提交＝`platform::TcpStream::set_nodelay` 两腿 + `conn::telnet_connection` 连接腿（含尺寸腿）与环回 `itest_telnet_connection` 十三案，入册裁决 **7.114**；批 3 两笔提交＝向导第四型（四张类型卡 + Telnet 三行字段集 + 端口留空即 23）与 `src/main.cpp` 装配（会话构造 + 连接与重启分流），入册裁决 **7.115**）⇒ 第四张类型卡可点、Telnet 档案建得出来、会话上得了屏，**界面路径自此可达**。**但 §6 的六条真机走查一条都没做，且装配那三处分流无自动化证人（`src/main.cpp` 不编入 CTest runner，裁决 7.83）⇒ 本稿仍一律不称「可用」**（AGENTS §4.4 第 33 条）。
> **配套高保真草图**：`codespec/UI_TELNET.draft.svg`（本稿 §2 的 1–8 标号判据即图中圈号，各贴在所属屏旁；SVG 为事实来源，后续改图直接改 SVG）。
> **上游依据**：`SPEC.FEAT.CONN.06` 原文（基础 Telnet 客户端，不含 TN3270；选项协商覆盖 ECHO / SGA / TTYPE；编码按 `SPEC.FEAT.TERM.09` 可配）、裁决 7.6（本地终端 / SSH / **Telnet** 缺省 UTF-8，串口才吃 GB18030）、7.99（`session::CloseReason` 与重连分类表）、7.103（`term::SessionEncoding` 已具备）、7.110 D1①/D11①（类型域追加式扩型的先例 + 写出侧一处行尾折算点）、7.96（平台 TCP 腿的 IPv4 收窄）、7.94（向导两步式与侧栏连接族）。
> **既成地基（本棒只消费，不新建）**：`src/platform/tcp.h` 的 `tcp_connect` / `TcpStream`（POSIX + Win32 两腿已在，出稿时以 `utest_tcp_loopback` 4 例为证；批 2 为 `set_nodelay` 追加一枚 ⇒ 现 5 例）、`session::Connection` 五件、`src/conn/reconnect.{h,cpp}` 的退避环与 `should_reconnect` 分类、`Session` 的构造期 `encoding` / `line_ending` 两枚入参、`ui::connection_wizard` 的字段集分派（出稿时三张类型卡，批 3 按本稿 §2 标号 1 追加第四张 ⇒ 现四张，这一处是本棒**改**它而非只消费）。

---

## 0 地基实测（2026-10-11 本沙箱，POSIX 腿 + Python 探针；Windows 腿一律标未验证）

形态不是推演出来的，下列每条都有本机读数（探针是一次性脚本，不入仓；复现口径见 §8）。

| # | 读数 | 对本稿的约束 |
|---|---|---|
| **F1** | 本机**无 telnetd**：`telnetd` 不在 PATH、无 `/usr/sbin/in.telnetd`、23 端口无监听；只有客户端 `/usr/bin/telnet`（GNU inetutils 2.7） | **真服务器腿在本环境不可达**：`etest_telnet_server` 与「真设备/真服务器的协商手感」只能归真机（§6/§7）。但见 F2——本棒可证面比串口那棒**更强**，不弱 |
| **F2** | 环回假服务端可起：`bind(127.0.0.1:0)` + `listen` + `accept`，端口由内核择定；两端逐字节可查 | **整条 IAC 协商腿可在环回上逐字节证**（串口受 F1 只能拿 pty 替身，Telnet 没有这种限制）。故本棒的 itest 不靠任何协议近似 |
| **F3** | 掉线两形态读数**分明**：对端 `SO_LINGER(1,0)` 关 ⇒ 本端 `recv` 回 **ECONNRESET(104)**；对端有序 FIN ⇒ 本端 `recv` 回 **0 字节（EOF）** | 归因两档在既有 `TcpReadOutcome` 上有唯一映射（`Closed`／`Error`，`src/platform/posix/tcp.cpp` 的 `PosixTcpStream::read` 分档处实测代码），**不需为 Telnet 新增平台档位**（D9 的根据） |
| **F4** | 半关（对端只 `shutdown(SHUT_WR)`）时本端 `recv` 得 EOF，而**写侧仍连续两次 `sendall` 都不报错**（字节进内核缓冲）；本仓 `TcpStream::write` 带 `MSG_NOSIGNAL`（`src/platform/posix/tcp.cpp` 的写腿那一处），半关写不会 `SIGPIPE` 杀进程 | **写侧不可当掉线判据**：掉线一律从读侧归因。写失败要等内核 RST 回来才显形，所以「已发出」不等于「对端收到」，界面上不得据写成功报「已送达」 |
| **F5** | 一次 `recv` 里可同时混排三条命令 + 文本：`[255,251,1, 255,252,3, 255,253,24, 'login: ']` | 解析必须**按字节走状态机**，不许按行、按块、按「一条命令一次读」切（判据 5，`utest_telnet_negotiation` 逐字节喂的根据） |
| **F6** | IAC 命令**确实跨 read 断裂**：两端 `TCP_NODELAY` + 写读交替时，一条 `IAC WILL ECHO` 以 `[255] [251] [1]` 三次读各 1 字节到达 | 状态机必须可暂停／续跑，且**未完成的命令段绝不进网格**——否则半个命令的 0xFF/0xFB 会被当数据解码成乱码格（判据 5 的另一半） |
| **F7** | 数据段里的字面 0xFF 以 `FF FF` 转义原样往返；子协商载荷里也可嵌转义（`IAC SB TTYPE 0 'X' FF FF 'Y' IAC SE` 完整到达） | 反转义是逐字节的，且要同时作用于**命令段与 SB 载荷**两处；发送侧本仓必须自己把数据里的 0xFF 写成 `FF FF`（D11 的契约内容） |
| **F8** | **参考客户端连上后一个字节都不发**：GNU inetutils 2.7 在 pty 与 `stdin=DEVNULL` 两跑下，等满 2 s 环回服务端均收 0 字节 | 「全被动应答」是现实可行的形态（不发起也能连），但同时证明**双方都等就双双沉默**：对端也是「等对方先动」的实现时，本端不主动 `DO` 就永远拿不到 ECHO/SGA 的结论（D3 的根据） |
| **F9** | 建连失败两态可读：拒连 ⇒ errno **111 ECONNREFUSED**；对不可路由地址（`10.255.255.1:23`）以给定 0.4 s 档**超时返回**（本仓 `tcp_connect` 走非阻塞 + `poll(timeout)`，见 `src/platform/posix/tcp.cpp` 的 `tcp_connect` 超时段） | 两类建连失败都落在既有腿上，`CloseReason::DialNetwork` 可直接归因；超时档是调用方给的一个整数 ⇒ 需要定它从哪来（§5 装配口径） |
| **F10** | `localhost` 经 `getaddrinfo(AF_INET)` 可解析；本仓 `tcp_connect` 的 hints **钉死 AF_INET**（`src/platform/posix/tcp.cpp` 的 `tcp_connect` hints 处） | **纯 IPv6 主机名连不上**——这是裁决 7.96 的既定收窄，本棒沿用不改（§7 登记为在册代价，不另开一档） |
| **F11** | 本仓**两条 TCP 腿都不设 `TCP_NODELAY`**（全仓 grep 无匹配）；环回上 ACK 即时，无从复现 Nagle 与 delayed-ACK 的最坏叠加 | 交互式终端是逐键往返的，Nagle 的有无直接影响手感。「不设 NODELAY 时最坏多吃一档 40 ms 级延迟」属**推断／未验证**（AGENTS §4.2 第 11 条），真机走查实测；据此立 D6 |
| **F12** | 本沙箱无 MSVC、无 Win32 头可编 | §4 的 Win 侧一切说法（含 `src/platform/win/tcp.cpp` 既有件与本棒可能的追加）标**推断／未验证**（AGENTS §4.2 第 11 条），待真机复测回写 |
| **F13** | 空闲读超时不吞数据：无写者时带超时的读按 Timeout 档返回，之后再写的数据仍完整可读 | 读循环可以用「带超时读」做心跳与终止位检查（但等待不许占着写侧要取的锁——串口 F11 那条锁纪律在此同样成立） |
| **F14** | **NAWS 载荷的字节序 = 宽在前、高在后，各大端两字节**：环回假服务端向 GNU inetutils 2.7 客户端发 `IAC DO NAWS`，客户端在 `TIOCSWINSZ` 设成 20 行×60 列的 pty 上回 `FF FB 1F FF FA 1F 00 3C 00 14 FF F0`（即 `WILL NAWS` + `SB NAWS <0 60><0 20>`）；与 RFC 1073 §2 的语法行 `IAC SB NAWS WIDTH[1] WIDTH[0] HEIGHT[1] HEIGHT[0] IAC SE` 及 §6 例子（80×24 ⇒ `0 80 0 24`）同读数 | 判据 8 的组包次序按此落（`0x00 0x3C` 在前是**列**）；本稿原写的「rows 在前」是稿面错处，按裁决 7.113 改口。**RFC 1073 即 NAWS 本体**，只有 `2+2` 字节形态、无「四字节宽度扩展」——§7 那条错引一并改掉 |

---

## 1 范围与边界

**本期交付**：

1. Telnet **档案**进连接模型（`ConnectionType` 第四档 + `TelnetProfile`）与**向导**第四张类型卡（`SPEC.FEAT.CONN.07` 的分步表单自此有第四型）。
2. **IAC 协商状态机**（纯逻辑、逐字节可断言）：`ECHO`(1) / `SGA`(3) / `TTYPE`(24) 三项的发起、应答、子协商载荷与 `FF FF` 转义（`CONN.06` 原文点名的三项），**外加 `NAWS`(31) 的尺寸下发**（D10 改判后并入，判据 8）。
3. **Telnet 会话连接腿**：`tcp_connect` → 协商 → 净数据读线程 → 写 → 关闭，接进既有 `session::Connection` 抽象（AGENTS §4.5 第 23 条：socket 知识只在 `src/platform/`，本棒沿用既有 `platform::tcp` 不新增平台件）。
4. **断开重连**：Telnet 的掉线归因进既有 `session::CloseReason`，复用 `src/conn/reconnect.{h,cpp}` 的分类与退避（不新写一套重连环，口径同 SSH / 串口两腿）。
5. **编码按 `TERM.09` 可配**：会话构造吃既有 `make_session_encoding(settings)`（`src/main.cpp` 的装配函数，本地/SSH/串口三腿同源），缺省 UTF-8（裁决 7.6）。

**不本期交付**（§7）：TN3270（需求原文即排除）、`NAWS` 之外的其余选项（`BIN`/`LFLOW`/`NEW-ENVIRON`/`GA`/`EOF`/`SUSP` 等）、本地回显、IPv6、真 telnetd 的 etest、quick connect 语法的 Telnet 分支、逐标签连接类型图标、真机走查。

**关键前提（口径不是本稿新造的）**：

- **Telnet 没有任何设置域键**：`include/borealis/config/settings.h` 的 `connection` 域只有 `local_shell` / `startup_directory` / `ssh.*` / `serial.*` / `session_log*`，grep 全仓的 "telnet" 只命中注释与文档。本棒是否新增键由 D5 定，**推荐不新增**（缺省端口的先例是向导里硬编码的 22：`src/ui/connection_wizard.cpp` 的 `ConnectionWizard::collect_draft()`）。
- 类型枚举的落盘形态是一张 `EnumName` 表（`src/config/store.cpp` 的 `kConnectionTypeNames`，现列 `local` / `ssh` / `serial`）⇒ 追加 `telnet` 一档要同时补这张表，装载侧对表外名的容错沿用既有那档（裁决 7.26④ 的宽容口径）。
- `profile_views` 的 `validate_draft` 已有 `EmptyHost` / `PortOutOfRange` 两档问题码（`src/conn/profile_views.cpp` 的 `validate_draft`）⇒ Telnet 型的校验**零新增问题码**，只在四向分派里补一条腿。
- `platform::TcpStream` 只有两个实现（`PosixTcpStream` / `WinTcpStream`），全仓没有第三方派生类或测试替身 ⇒ 若 D6 选「加一枚 `set_nodelay()`」，改面就是两腿 + 一处调用，不牵动 `utest_tcp_loopback`。

---

## 2 草图与判据对应

草图五屏（A 向导第四型 / B 上线协商时序 / C 跨分片与转义 / D 会话在途与掉线两档 / E 侧栏与不在本期面），图中圈号 1–8 即下表判据；屏 B/C 的字节序列是**实测读数**（F5/F6/F7），屏 D 的注记档位是示意：

| 标号 | 判据 | 对应需求 |
|---|---|---|
| 1 | 向导步骤 0 的类型卡自此**四张**（Local / SSH / Serial / **Telnet**）；步骤 1 的字段集由类型卡决定，Telnet 型**不显示** SSH 的 user/认证/known_hosts 三栏，也不显示串口九栏 | `CONN.07`「按连接类型分步表单」（D1/D2） |
| 2 | Telnet 字段集三行：展示名、主机、端口（端口留空＝23，与 SSH 留空＝22 同一条硬编码口径）；校验只走 `validate_draft` 既有的 EmptyName / EmptyHost / PortOutOfRange 三档 | `CONN.06`（D2） |
| 3 | 连上即发两条**发起**命令：`IAC DO ECHO`、`IAC DO SGA`（各 3 字节）；`TTYPE` 不主动发起，只在收到 `IAC DO TTYPE` 时答 | `CONN.06`「协商覆盖 ECHO / SGA / TTYPE」（D3） |
| 4 | **应答表**一处一份：`WILL x` → 我方支持则 `DO x` 否则 `DONT x`；`DO x` → 我方支持则 `WILL x` 否则 `WONT x`；`IAC DO TTYPE` → `WILL TTYPE` 紧跟 `IAC SB TTYPE IS "XTERM" IAC SE`；表外选项一律拒绝，且**同一立场只答一次**（对端重复问不得再答，防协商风暴） | `CONN.06`（D3/D4） |
| 5 | 净数据与命令段的分界：`[FF][FB][01]` 三次读合成一条命令、一条读里混排三命令 + `login: ` 都照常处理；`FF FF` 还原成**一个** 0xFF 字节交给网格；未完成的命令段**绝不进** `on_bytes` | `CONN.06`（D11，实测 F5/F6/F7） |
| 6 | 会话在途：视口按 `terminal.encoding` 解码（缺省 UTF-8，裁决 7.6）；键入 Enter 落地为 **CRLF**（走 `Session::write_out()` 那唯一一处折算点，裁决 7.110 D11① 的现成品），`SGA` 的结论不改字节 | `CONN.06`「编码可配」+ 基础可用性（D5/D8） |
| 7 | 掉线两档**分开显示**：读侧 EOF ⇒ `RemoteExit`，「远端已退出」一句、**不进重拨环**、网格保留供回看；读侧 ECONNRESET/错误 ⇒ `LinkLost`，进既有退避环，浮层两态沿用 `codespec/UI_RECONNECT.draft.md` 已裁形态 | `CONN.06` 隐含 + `WS.05`（D9，实测 F3） |
| 8 | **尺寸下发（NAWS，D10 改判后新增）**：会话尺寸变化 ⇒ 发 `IAC SB NAWS <cols hi> <cols lo> <rows hi> <rows lo> IAC SE`（**宽在前、高在后**，各大端两字节，选项 31——次序按 F14 实测与 RFC 1073 §6）；建立后**首发一次**当前尺寸，此后只在值**变化**时发，同值去重（比对源是「上一次真的发出去的」，拖回旧值仍要发）；载荷里的 0xFF 按 F7 的口径转义成 `FF FF`；`resize()` 自此**不是**空操作 | `CONN.06`「基础客户端」+ `WS.02` 分屏尺寸语义（D10②，人改判） |

---

## 3 裁决（D1–D12，2026-10-11 人已逐条裁完；下表「结论」列即裁决结果，理由与代价随裁决一并入册为 7.112）

| # | 待定项 | 选项 | 结论 | 理由与代价 |
|---|---|---|---|---|
| **D1** | Telnet 是否进**档案模型** | ① `ConnectionType` 追加 `Telnet` + 新 `TelnetProfile{host, port=23}`，`Profile` 加 `telnet` 子结构，JSON 读写与 `EnumName` 表一并追加；② 不入档案，只做一次性入口 | **①** | ② 与侧栏/最近连接/收藏那一族全部接不上，每次重连重敲主机名；且 `CONN.03` 的类型域本就把四类连接并列。代价＝追加式扩型（照裁决 7.110 D1① 串口那一条的先例）：公共头 `include/borealis/conn/profile.h` 只加一档枚举值与一个子结构，**不动既有成员与可见性**（AGENTS §4.1 第 6 条）；`store.cpp` 的两处按型分派、`profile_views` 的三处按型分派、`utest_config` 的往返与坏字段回退都得补第四腿 |
| **D2** | 向导第四型的字段集 | ① 三行（展示名 / 主机 / 端口）；② 再加用户名与口令（Termius 式「自动登录」）；③ 再加行尾与编码两行 | **①** | ② 要造的是一条**没有消费方的凭据链**：Telnet 没有认证协议，登录提示由远端程序给，「存进档案的口令」只能靠「自动敲 login: / Password: 序列」兑现——那是脚本功能，`CONN.06` 原文没有，且会牵动 `CONN.09` 的凭据存储与明文审计（本棒不碰）。③ 的编码已由全局档可配（D5），行尾由 D8 定固定档，存进档案只复制「改设置不回溯在档」那个坑（串口 D4① 的代价面）。代价：三张卡变四张卡的行宽要不要换行属绘制判据，无头断不了，留 §6 走查 |
| **D3** | 协商发起方 | ① 连上即发 `DO ECHO` + `DO SGA`（一次），`TTYPE` 被动应答；② 全被动应答（只答不发）；③ 主动全套（再带上我方选项的 `WILL`） | **①** | F8 实测参考客户端**一个字节都不发**——② 因此可行，但它同时证明「双方都等」的组合会双双沉默，而 ECHO 缺省态是「未定」，不发起就永远不知道对端会不会回显。③ 要我方 `WILL` 一个真实选项，而本端能真正执行的只有 `TTYPE`（且它只该被问时答），凭空 `WILL` 就是发对端不认的声明。代价：多两个上线字节；个别老设备对未请求的 `DO` 的反应属**推断／未验证**（本沙箱无 telnetd，F1），走查第 ⑴ 条实测 |
| **D4** | 表外选项的处置与「答几次」 | ① 一律拒绝（`DO x`→`WONT x`；`WILL x`→`DONT x`；`SB x … SE` 照吞到 `SE` 不处理），且每个选项**同一立场只答一次**（状态位记住已答，对端重复问不再答）；② 表外一律沉默不回；③ 每次收到都重答一遍 | **①** | ② 会让对端一直等在协商里（RFC 854 的「拒绝未支持项」正是为终止这场等待）；③ 在对端有重试逻辑时形成**应答风暴**——两个方向都靠「已答」位封死。本端能真正执行的只有三项，其余拒绝是唯一诚实的立场。代价：对把 `WONT` 当冒犯的极端老设备不友好（本环境无从取证，标**推断／未验证**）；「已答」状态位要能随重拨**清零**（重连后同一台服务器可能改口），这条得进用例 |
| **D5** | 设置域是否新增键 | ① 不新增，端口缺省硬编码 23（同 SSH 的 22）、编码吃既有 `terminal.encoding`；② 新增 `connection.telnet.{port,encoding}` 两键并同步进 schema + 反向核对表 | **①** | ② 要为「新建档案时缺省填什么」这一件事开两枚键，走一整套 schema/装载/核对表/往返（串口那棒开 `flow` 键是因为**需求原文点名流控**，`CONN.06` 没点名 Telnet 的默认端口）；且需求说的「编码按 `TERM.09` 可配」的入口本来就是 `terminal.encoding`（裁决 7.6 明写 Telnet 缺省 UTF-8 ＝ 全局档，串口才有类型缺省 GB18030）。代价：设置页没有「Telnet 默认端口」一项，端口只能在向导里手敲——与 SSH 型现状同档，不算倒退 |
| **D6** | 交互延迟与 `TCP_NODELAY` | ① 给 `platform::TcpStream` 追加一枚 `set_nodelay()`，只在 Telnet 腿呼；② 本期不动平台腿，走查后再说；③ 在 `tcp_connect` 里全局设上 | **①** | F11：两条腿都不设 NODELAY。逐键往返的交互终端与隧道转发泵的需求相反——③ 会顺手改掉既有隧道的线上行为（无谓的风险面，违背「一个改动一个意图」）。① 是「本棒立即要用」而非「将来才用」：`TcpStream` 只有两个实现（§1 前提），改面＝两腿各一次 `setsockopt` + Telnet 腿一处调用，隧道腿不呼 ⇒ 既有转发逐字节不变。② 的问题是明知有延迟缺陷还把它推给走查。代价：Win 腿未经编译验证（F12）；`set_nodelay()` 是抽象类上的**追加纯虚函数**，两腿都必须实现否则编译不过（POSIX 腿本沙箱可编，Win 腿待真机）。**（人裁按推荐档 ①，2026-10-11）** |
| **D7** | 对端回 `WONT ECHO`（不回显）时的处置 | ① 不做本地回显：结论只记在状态位里，不改行为、不出提示；② 连接腿把已写字节回环进自己的 `on_bytes`（真本地回显）；③ 在界面提示「对端不回显，输入不可见」 | **①** | `CONN.06` 只要求「协商覆盖 ECHO」，不要求本地回显。② 要在会话侧新造一条「输入可见」通道：转义序列（方向键不该被回显成字面 `^[[A`）、CR 的回显要补 LF、编码半字符要各自处理——一处没对齐就是半截实现（AGENTS §4.1 第 3 条）。③ 是给一个本棒不解决的事实配一句常驻措辞。代价：真遇到 `WONT ECHO` 的服务器，用户看不见自己敲的字（busybox/主流 telnetd 都 `WILL ECHO`，故属边缘）；走查第 ⑶ 条如实实测，届时按到货的消费方单开，不在本稿预建。**（人裁按推荐档 ①）** |
| **D8** | 发送行尾 | ① 装配层给 Telnet 会话传 `term::LineEnding::CRLF`（**固定档，不可配**），复用 `Session::write_out()` 那唯一一处折算点；② 档案存 `line_ending` 字段可配；③ 不折算（恒 CR，与本地/SSH 同档） | **①** | Telnet NVT 的行结束就是 CR LF（RFC 854 口径）。③ 赌对端宽容，而本仓在串口侧已定过同一纪律：「不许指望规程改写字节，自己拼」（串口 F8）。② 的需求不存在——`CONN.06` 没提行尾，行尾可配是 `CONN.05` 那条。① 的成本是一枚构造入参，且折算点与四条锁用例都在（裁决 7.110 D11① 落的现成品），不算新抽象。代价：固定档意味着个别只认 CR 的老设备需要改档才能用（推断，走查第 ⑵ 条）；真要可配时消费点已在，不必现在预建 |
| **D9** | 掉线归因与重连 | ① 读侧 EOF→`RemoteExit`（不重拨）、读侧 `Error`（含 ECONNRESET）→`LinkLost`（重拨）、建连失败（拒连/超时/DNS 失败）→`DialNetwork`（重拨）；② 照串口那档口径：EOF 也归 `LinkLost` 进重拨环 | **①** | Telnet 是网络登录会话：telnetd 在远端进程 `exit` 后关 TCP 是**正常结束**，`should_reconnect(RemoteExit) == false`（`src/conn/reconnect.cpp` 的 `should_reconnect` 分类表实测）正是「登录退出后不要无限重拨」。串口那棒把 EOF 归 `LinkLost` 是因为串口根本没有「远端进程」这个概念（裁决 7.110 D7① 第 1 条代价口径）——**两棒的分野是协议语义，不是抄漏**。代价：本条与 7.110 D7① 故意不同，须在裁决里写清分野，否则后人会误读成不一致；F4 的半关形态（recv 得 EOF 但写侧仍成功）也归这一档，界面按「远端已退出」处理 |
| **D10** | `NAWS`（窗口尺寸协商，选项 31）要不要本期顺带 | ① 不做：`TelnetConnection::resize()` 是**有意的空操作**（串口那棒同形态先例）；② 做：`resize()` → `IAC SB NAWS <cols hi> <cols lo> <rows hi> <rows lo> IAC SE`（宽在前，按 F14），并处理载荷里的 `0xFF` 转义 | **②（人改判，非稿面推荐）** | 稿面原推荐是 ①（`CONN.06` 原文只点名三项，按 AGENTS §4.1 第 1 条不自行扩面），**人裁决改为 ②**：尺寸停在 80×24 时，分屏与大窗口下的远端全屏程序（`vi`、`top`）排版即失真，而本仓的分屏（`WS.02`）与窗口缩放本就会呼 `resize()`——改判的收益在既有产品形态里**立刻可感**，不是「将来才用」的抽象。代价（本条实际吃下的）：① 新增一档**客户端发起**的子协商，`telnet_negotiation` 要有 NAWS 的组包与转义（F7：SB 载荷同样要转义），判据多出 §2 的第 8 条；② 尺寸跨线程传递必须走 latest-value 快照且**同值去重**，否则拖动分屏会形成协商风暴（AGENTS §4.5 第 25 条：UI 线程不得在事件/绘制路径上写 socket）；③ 重拨后「已发尺寸」的记忆要清零，与 D4① 的「已答」位同档处理；④ 本沙箱无 telnetd（F1），真服务器对 NAWS 的反应以**走查第 ⑷ 条**实测，本稿不预判。超过 65535 的尺寸不发（F14：RFC 1073 只有 `2+2` 字节形态，本仓不存在那种终端）；对端 `DONT NAWS` 只记状态位、**不**据此停发（本件按判据 8 的无条件下发，与 RFC 1073 §2「服务器可禁止后续子协商」那句的差集已在实现注释里登记，真服务器反应同样归走查第 ⑷ 条） |
| **D11** | TU 拆分与协商件的契约形态 | ① 纯逻辑 `conn/telnet_negotiation.{h,cpp}`：输入字节段 → 输出「净数据段 + 待回字节 + 选项状态位快照」，无 socket、无 `au::` 类型；② 合成一件 `conn/telnet_connection.{h,cpp}`，解析器以私有成员藏在腿里 | **①** | 需求核心（ECHO/SGA/TTYPE 协商）恰好就是**逐字节可断言**的那一档：转义、跨分片、混排、子协商、拒绝表、重复请求不重答、NAWS 组包与去重，七类用例全都不需要 socket。串口那棒（`serial_model` / `serial_connection`）与 SFTP 那棒（`sftp_model` / `sftp_client`）都是同一分法，`conn` 层的既有纪律（AGENTS §4.4 第 20 条）。② 会让这些断言全都要经过环回 socket 才能跑，测试面变脆。代价：契约要写准——「状态机不许吞掉未完成的命令段」「SB 载荷里的 `FF FF` 也要反转义」（F7）都得是显式断言，而不是腿里的隐式习惯 |
| **D12** | 验证形态 | 三层：① `utest_telnet_negotiation`——逐字节喂的解析/转义/应答表/发起序列/不重答/NAWS 组包与去重；② `itest_telnet_connection`——环回假服务端（真 socket）证双向协商字节、净数据进 `on_bytes`、EOF 与 ECONNRESET 两档归因、拒连档、可重拨腿、尺寸变化上线的字节；③ `etest_telnet_server`——真 telnetd，本沙箱按 F1 **写不出来**，登记在册 | 如述（人裁按稿面） | F2 使 ② 的强度**高于**串口那棒的 pty 替身：协议两端都是真 socket，无需任何近似。同时不拿环回假服务端冒充真服务器宣布「与设备对接可用」（F1 挡住的就是这一档）。代价：假服务端脚本要落在测试 TU 里（同 `utest_tcp_loopback` 的环回自建自连形态），协商用例的服务端一侧是写死的应答表，对真 telnetd 的行为覆盖以走查与 etest 补齐 |

---

## 4 文件布局 / TU 拆分（裁决后按此开工）

```
include/borealis/conn/profile.h        修改 —— ConnectionType 追加 Telnet；新 TelnetProfile {host, port=23}；
                                                Profile 加 telnet 子结构（D1①，追加式，不动既有成员/可见性）
src/conn/telnet_negotiation.{h,cpp}    新增 —— 纯逻辑 IAC 状态机（无 socket、无 au::，可无头逐字节单测）：
                                                FF FF 转义/反转义、命令段与数据段分界、SB…SE 载荷、
                                                ECHO/SGA/TTYPE 应答表、上线发起序、同一立场只答一次（D4①/D11①）、
                                                NAWS 组包 = 大端两字节行列 + 载荷内 0xFF 转义 + 同值去重（D10②）
src/conn/telnet_connection.{h,cpp}     新增 —— 私有头（D11①，口径同 serial_connection.h）：
                                                session::Connection 的 Telnet 实现 = platform::TcpStream +
                                                读线程 + telnet_negotiation + 归因表 telnet_close_reason()
                                                + 复用 conn/reconnect.h 退避环（D9①）；resize() 按 D10②
                                                把尺寸交 latest-value 槽、由读线程发 SB NAWS…SE（**不是**空操作），
                                                重拨时清「已发尺寸」记忆
src/CMakeLists.txt                     修改 —— `borealis_core` 的源列表是显式清单（只有 tests 走 GLOB），
                                                新增 `conn/telnet_negotiation.cpp` 要手写进去，否则链接期
                                                undefined reference（批 2 的 `conn/telnet_connection.cpp` 同此）
src/conn/profile.cpp                   修改 —— `ProfileStore::search()` 的主机命中覆盖 Telnet 型（档案的 JSON
                                                读写**不在这件里**，住在 `src/config/store.cpp`——出稿时这行写错了，按代码回写）
src/conn/profile_views.{h,cpp}         修改 —— validate_draft / draft_to_profile / profile_to_draft 补第四腿
                                                （问题码零新增：EmptyName + EmptyHost + PortOutOfRange，D2①）
src/config/store.cpp                   修改 —— EnumName 表追加 {"telnet", …}；profiles 的 telnet 段读写；
                                                装载侧对表外 type 名沿用既有容错档
src/platform/tcp.h                     修改（D6①）—— TcpStream 追加 set_nodelay()（纯虚，两腿各实现）
src/platform/posix/tcp.cpp             修改（D6①）—— setsockopt(IPPROTO_TCP, TCP_NODELAY)
src/platform/win/tcp.cpp               修改（D6①）—— 同上一枚；**未经 MSVC 编译验证**（F12）
src/ui/connection_wizard.{h,cpp}       修改（批 3 之一，已落）—— 类型卡四张；Telnet 字段集三行；端口留空＝23（同 22 的先例，
                                                且落成**一处**共用构造）；点 Telnet 卡即预填 23，让「留空＝23」在屏上可见
src/main.cpp                           修改（批 3 之二，已落）—— 装配：Telnet 会话构造吃 make_session_encoding(settings)（D5①）
                                                与固定 term::LineEnding::Crlf（D8①，落成文件级常量 `kTelnetLineEnding`，
                                                会话折送入参与视口粘贴档同源）；连接与重启分流补 Telnet 腿，
                                                撤账随 `perform_close` 同批，`telnet_by_tab` 只存档案不存连接指针。
                                                **原写「tcp_connect 的超时档取一常量」按实际改口**：那一档已在批 2 落进
                                                连接腿（`src/conn/telnet_connection.cpp` 的 `kConnectTimeoutMs`），
                                                装配层不再另开一档。本文件的三处分流与那枚常量**无自动化证人**
                                                （`src/main.cpp` 不编入 runner，裁决 7.83）——见 §6 末段
src/ui/settings_i18n.cpp               修改（批 3 之一，已落）—— 新词条**只有一枚** `connections.type.telnet`；
                                                原写「Telnet 字段名/留痕的新词条族」按实际收窄：三行字段集的展示名/主机/
                                                端口标签键早已存在。另一处是**改口而非新增**——`issue_empty_host`
                                                去掉「SSH」型名（两型共用同一档问题码，D2① 的零新增问题码）
src/ui/settings_catalog.cpp            **不动** —— 本棒不新增设置域键（D5①），故反向核对表零改；
                                                「写出的 schema 键必须逐行有控件」那道闸因此不受影响
tests/unit/utest_telnet_negotiation.cpp        新增 —— D12①，逐字节喂的七类断言（含 NAWS 组包/去重/重拨清零）
tests/integration/itest_telnet_connection.cpp  新增 —— D12②，环回假服务端 + 真 socket
tests/unit/utest_config.cpp            修改 —— telnet 档案 JSON 往返 + 坏字段逐项落回
tests/unit/utest_profile_views.cpp     修改 —— 第四腿的校验顺序与草稿往返
tests/integration/itest_connection_wizard.cpp  修改（批 3 之一，已落）—— 四张类型卡真点 + Telnet 三行字段集（并断别的型那
                                                十三枚标签一枚都不出现）+ 端口留空＝23 + 三档问题码逐档上屏不关框；
                                                套件自此 7 → 10 案
tests/unit/utest_tcp_loopback.cpp      修改（D6①）—— set_nodelay 的环回证人。**落地时按实际改口**：
                                                原写「只证调用不报错与**选项回读**」，而 `platform::TcpStream`
                                                的抽象面上没有 `getsockopt`，回读做不到（为此加一枚选项查询
                                                = 为一条腿扩整个平台抽象面，第 3 条不做）。实落三条：两端呼
                                                都回 true 且幂等、设过之后 12 字节往返逐字节照旧、`close()`
                                                之后呼回 false
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
      │        ├─ 尺寸腿（D10②，人改判新增）：UI 的 resize(rows, cols) → 连接腿的 **latest-value 尺寸槽**
      │        │          └─ 读线程取到「与上次已发不同」的值才发 IAC SB NAWS <cols hi lo><rows hi lo> IAC SE
      │        │             （宽在前·大端两字节，F14；载荷里的 0xFF 转义成 FF FF；同值不发；重拨后清零 ⇒ UI 线程零 IO）
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
- **锁的持有面**：socket 锁内**不做带超时的等待**——串口 F11 那条「本沙箱对持锁休眠的秒级惩罚」读数在 TCP 腿同样适用，真机上也违背架构 §3.4「锁内不做 IO」。落法（批 2 已按此实现）：读循环在锁内只做**超时 0 档的非阻塞探测**，20 ms 的空闲等待留在锁外，于是锁的常驻持有者只可能是一次探测或一次写。判据不变——**UI 线程的 `write()` 不得排在一个读分片之后**。
  - **这条判据本棒未做变异自证**（稿面原写「实现期以变异自证：把带超时读塞进锁内必须让某条用例转红」，按实际改口，AGENTS §4.2 第 12 条）：环回上键入线程等一个 20 ms 读分片是**观测不到后果**的——没有第二把锁争用的现场，也没有时延断言。故本棒的四条变异（§6 末条）不含这一条，该判据的实证落在两处：① 代码形态（`stream_mutex_` 内只有 `kProbeTimeoutMs = 0` 与一次 `write`）；② 真机走查第 ⑹ 条的逐键手感。把它写成「已证」是假读数。
- **协商状态只在读线程**：`telnet_negotiation` 的实例由连接腿私有持有，不跨线程读它；给界面的只有「会话是否已就绪」与 `ReconnectProgress` 那两枚既有快照通道（latest-value，AGENTS §4.5 第 25 条）。选项状态位（ECHO 开/关等）本期**没有 UI 消费方**（D7①），故不投快照、不落浮层——这是「不加将来才用的抽象」的直接落点。
- **跨线程的唯一入口是尺寸槽**（D10②）：`TelnetConnection::resize(rows, cols)` 由 UI 线程呼，只做一件事——把这对整数写进 latest-value 槽（不取 socket 锁、不做 IO、不排队列，同值直接覆盖）；真正组包与上线由读线程在下一轮完成。这条是改判 NAWS 唯一新增的跨线程面，其余方向与串口/SSH 两腿同构。
- 发送方向：`Session::send_text` / `send_bytes` → 编码 → `write_out()` 一处行尾折算（CRLF，D8①）→ `TelnetConnection::write()` → **数据段里的 0xFF 必须转义成 `FF FF`** 再上线（F7，`negotiation` 的写出侧一半）。转义发生在连接腿而不是 `Session`：SSH/本地腿没有这层规程，把协议细节渗进会话层就违背分层。
- 掉线判定**一律从读侧**（F4：半关时写侧仍成功，写成功不等于对端收到）。`write()` 返回 false 只记日志并置终止位，不据此出「远端已退出」的措辞。

---

## 6 验收判据映射

- **协商覆盖 ECHO / SGA / TTYPE**（需求原文第一条）：`utest_telnet_negotiation` 锁三件事——上线发起的两条命令逐字节、应答表逐档（`WILL ECHO`／`WONT ECHO`／`DO SGA`／`WONT SGA`／`DO TTYPE` → `WILL TTYPE` + `SB TTYPE IS "XTERM" SE`）、表外选项的拒绝与**同一立场只答一次**（D4①，含重拨后清零那一档）；`itest_telnet_connection` 在真 socket 上证对端确实收到那串字节（口径同串口那棒的「对端逐字节收到」读数）。
- **基础客户端可用**（连接/收发/关闭）：`itest_telnet_connection` 证「环回假服务端发净文本 ⇒ 网格自第 0 格起逐码点对齐」与「本端写一条命令 ⇒ 服务端收到的字节与预期逐字节全等」两向；`tcp_connect` 失败（拒连 errno 111，F9）证归因落 `DialNetwork` 且不假装会话已建立。
- **编码按 `TERM.09` 可配**：装配层把 `make_session_encoding(settings)` 交进 Telnet 会话构造（与本地/SSH 同一枚现成件，D5①），`itest_telnet_connection` 以对端写多字节序列 ⇒ 网格落子的形态证解码链吃的是该档；裁决 7.6 的「Telnet 缺省 UTF-8」在装配折算处锁住，**不新增设置键**。
- **发送行尾 CRLF**（D8①）：`utest_session` 既有四条折算用例覆盖折算点本身；本棒加一条「Telnet 会话的 Enter 落 CRLF 且键表面仍产一个 CR」的组装断言，`itest_telnet_connection` 在收端锁最后半段。
- **掉线与重连**：`itest_telnet_connection` 证 EOF→`RemoteExit`→**不**进重拨环（分类表零改动，且终态档位就是 `RemoteExit`）、ECONNRESET→`LinkLost`→投 attempt≥1 的环内快照并**真的把同一份档案再拨一回**（第二回受理在现场，中途掉线期间 `on_closed` 一份都不投＝会话不闪断）；「停止重连」截断正等待的退避档，终态落 `UserStopped` 而归因仍是掉线那回的 `LinkLost`。重拨成功时把进度快照擦回「不在环里」这一步由既有投递时机负责（`post_progress(ReconnectProgress{})`），本棒没有为它单断言——`saw_redial()` 与 `closes()==0` 是它的间接读数。
- **跨分片与转义**（判据 5）：`utest_telnet_negotiation` 以「同一段字节流，分别按 1 字节 / 随机分片 / 整段三种喂法喂入，净数据与待回字节必须逐字节全等」锁 F5/F6/F7 的形态等价性——这条是本棒最容易出真缺陷的地方，也是把它拆成纯逻辑件（D11①）的全部理由。
- **界面腿（判据 1/2）**：**已落**（批 3 之一，裁决 7.115）。`itest_connection_wizard` 的无头真点击用例覆盖稿面列的四件事——四张类型卡真点、Telnet 型**不画** SSH 的 user/认证/known_hosts 三栏也不画串口九栏（判据取「三行 label 全在 + 其余十三枚别的型的标签一枚都不在 + `TextInput` 恰三只 + `Dropdown` 零只」）、三行字段集产值进草稿、端口留空＝23（并锁「填值优先于缺省」那一腿）、`validate_draft` 三档问题码逐档上屏且不关框。**预填读的是该型缺省**：点 Telnet 卡即把端口行显示成 23，「留空＝23」这条判据因此在屏幕上可见（不预填就会显示草稿结构体带着的 22）。四张卡的**行宽好不好看**属绘制判据，无头断不了，留下面走查第 ⑸ 条。
- **尺寸下发 NAWS**（判据 8，D10② 人改判新增）：`utest_telnet_negotiation` 锁组包形态——`window_size_command(40, 130)`（40 行×130 列）⇒ 待回字节恰为 `IAC SB NAWS 0x00 0x82 0x00 0x28 IAC SE`（**宽在前**，各大端两字节，按 F14；RFC 1073 §6 的 80×24 ⇒ `0 80 0 24` 同读数）、尺寸与上一次**真的发出去的**相同则不发（拖回旧值仍要发）、载荷里撞上 0xFF 要转义、越界与 0 值不发且不记账、重拨后再发；`itest_telnet_connection` 在真 socket 上证环回服务端确实收到这串字节（100 列×40 行 ⇒ `FF FA 1F 00 64 00 28 FF F0`，同值再给一次线上**一个字节都不多**，改成 80×24 就又上一回）。
  - 原写「并证『UI 线程呼 `resize()` 时读线程尚未取走也不阻塞 UI』」按实际改口：**不阻塞这一半没有断言**（环回上量不出 20 ms 级的排队差值，见 §5 那条）。`resize()` 零 IO、零 socket 锁是**代码形态**（它只写 `size_mutex_` 护着的一对整数），itest 证的是另一半——槽里的值确实上线，含「拨号之前就给尺寸 ⇒ 连上那一轮首发」这条用例。
- **测试读数口径**：交付时以 `ctest --test-dir build -E etest_` 的**项数与 runner 全案数**结项（AGENTS §4.4 第 21 条不认「测了很多」），变异注入按第 22 条自证非空转。稿面原列的三条承重与实落结果：① 摘掉 `FF FF` 反转义 ⇒ 批 1 的 `utest` 3 案转红（已证）；② 把未完成的命令段漏进 `on_bytes` ⇒ 等价性与断裂案转红（已证：批 1 用「解析状态不跨读保留」这一枚变异打到等价性案，批 2 的 itest 另以「半条命令停在解析器里时网格为空」把同一事实钉在真 socket 上）；③ 把 NAWS 的大端字节序写反 ⇒ 组包用例转红（已证，批 1 utest 3 案／批 2 itest 2 案）；④ 摘同值去重 ⇒ 组包用例转红（已证）。批 2 另加三条只属于连接腿的承重：**摘掉写出侧 `escape_outbound` ⇒ 转义案转红**、**把 EOF 归成 `LinkLost` ⇒ 归因表案与 EOF 案双双转红**（D9① 的分野有两处在读，不是单点）、**尺寸槽改成只在建连时取一次 ⇒ 缩放案转红**。登记为「未自证／无证人」的承重有两条：§5 那条锁纪律（理由同处已写），以及批 3 装配侧的三处分流与那枚行尾常量——`src/main.cpp` 不编入 CTest runner（裁决 7.83 的物理事实，7.92④ 就同类情形登记在先），故 §2 判据 1/2 的界面腿有十案真点击证人、而「档案建出来后点它能不能连上」这一棒只有编译＋链接与真机走查兜。批 3 的界面腿另按第 22 条自证**四则变异各转红**：摘类型卡的 23 预填 ⇒ 2 案红；Telnet 落进 Local 那两行 ⇒ 3 案红；留空折成 22 ⇒ 1 案红；摘第四张卡 ⇒ 3 案红。
- **真机走查**（AGENTS §4.4 第 33 条，未走查不得称「可用」）：⑴ 真 telnetd 上发起 `DO ECHO`/`DO SGA` 有没有异常反应（F8 只证参考客户端沉默，不证服务器）；⑵ 只认 CR 的老设备在固定 CRLF 档下是否可用（D8 的代价面）；⑶ 遇到 `WONT ECHO` 的服务器时「看不见输入」的可接受度（D7 的代价面）；⑷ 真 telnetd 上 **NAWS 的实际观感**（D10② 的实测补账）：分屏/缩放后远端全屏程序是否按新尺寸重排、服务器对客户端主动发起的子协商有无异常反应——本沙箱无 telnetd（F1），这条只能真机取数；⑸ 四张类型卡的行宽与文案；⑹ 开 `TCP_NODELAY` 前后的逐键手感差值（D6 的实测补账，本沙箱无从复现）。

---

## 7 本期不做（延后 / 在册）

- **TN3270**：`CONN.06` 原文即排除。
- **`NAWS`(31) 之外的其余选项**：`NAWS` 已按 D10②（人改判）**并入本期交付**（判据 8 / §5 尺寸腿），不在此列；`BIN`(10)、`LFLOW`(19)、`NEW-ENVIRON`(39)、`GA`、`EOF`(237)、`SUSP`(238)、二进制传输透传等一概不实现；表外一律走 D4① 的拒绝档。
- **超过 65535 的窗口尺寸**：不发。原写「RFC 1073 的四字节宽度扩展不做」是错引——RFC 1073 就是 **NAWS 本体**（选项 31），载荷只有 `WIDTH[1] WIDTH[0] HEIGHT[1] HEIGHT[0]` 这八个字节、没有加长形态（F14 的语法行读数）。实际终端不存在 65535 列，越界的尺寸按判据 8 的「不发也不记账」处理，真遇到超长再加减。
- **本地回显**（D7① 的代价面）：对端 `WONT ECHO` 时不替它回显，也不出提示。
- **IPv6**（F10）：`tcp_connect` 的 hints 钉死 AF_INET，纯 IPv6 主机名连不上——裁决 7.96 的既定收窄，本棒沿用不改，另开需换平台抽象。
- **真 telnetd 的 etest**（F1）：`etest_telnet_server` 待有可用 telnetd 的环境补，与 `etest_serial_device`、待 sshd 的 CONN.08/10 两处欠账同档登记。
- **Windows/MSVC 腿验证**（F12）：本沙箱无 MSVC；`src/platform/win/tcp.cpp` 既有件与本棒的 `set_nodelay` 追加均未经编译验证。
- **quick connect 的 Telnet 分支**：`conn::parse_quick_connect` / `quick_connect_profile` 现属 SSH 语义（`src/conn/profile_views.cpp` 的 `parse_quick_connect` / `quick_connect_profile`，裁决 7.94 落的那件，建的是 SSH 档案）。给 Telnet 加语法要先定「前缀词法」，属新交互契约，本棒不扩面（在册，随 `CONN.07` 的 snippets 余腿一并看）。
- **逐标签连接类型图标**：随 `SPEC.FEAT.WS.04` 余腿单开（口径同裁决 7.110 D12①）。代价：Telnet 标签本期与本地/SSH 标签画得一样，识别靠档案名。
- **终端类型可配**（TTYPE 报什么串）：`XTERM` 收在 `telnet_negotiation` 的一枚常量。理由是本仓没有 `TERM` 类型设置域（§1 前提），而 `CONN.06` 只要求「协商覆盖 TTYPE」，不要求报什么。代价：设备侧若按别的类型发序列则解释不合（走查第 ⑴ 条一并看）。
- **`CR NUL` 数据段转义**（RFC 854 要求裸 CR 后跟 NUL）：不实现。本沙箱无真服务器可判其必要性，且主流 telnetd 宽容——属**推断／未验证**（AGENTS §4.2 第 11 条），`etest_telnet_server` 落地时取读数再决定。
- **保活（keepalive）与半开检测**：`SO_KEEPALIVE`、应用层心跳都不做。F4 已说明写侧在半关时不报错，故「对端静默死亡」在本期表现为「一直等」，直到用户自己关标签——如实登记，不预建检测。
- **设置域键**：本棒不新增（D5①），故 `settings_catalog` 与 `utest_settings_catalog` 那张核对表零改动。

---

## 8 裁决记录与探针复现

- **2026-10-11 出稿**：§0 的地基实测已跑完（F1–F13），§3 的 D1–D12 送人逐条裁决。**裁决前不落 `src/` 代码**（AGENTS §4.1 第 2 条）。
- **2026-10-11 收口**：人已裁完——**D6／D7 按稿面推荐档 ①**，**D10 改判为 ②（本期顺带做 NAWS）**，其余九条（D1–D5、D8、D9、D11、D12）按推荐档收口。据此入册 `codespec/SPECIFICATIONS.md` §7 裁决 **7.112** 与 `codespec/CHANGELOG.md` v0.112，改判的六处连带回写已落在本稿（见文首状态块）。
- **2026-10-11 批 1 落地 + 一处口径按实测改口**：两笔提交——档案第四型（`include/borealis/conn/profile.h` 追加 `Telnet` 与 `TelnetProfile{host, port=23}`、`src/config/store.cpp` 的 `EnumName` 表与 telnet 段读写、`src/conn/profile_views.{h,cpp}` 的第四腿、`src/conn/profile.cpp` 的主机搜索）与协商纯逻辑件（`src/conn/telnet_negotiation.{h,cpp}` + `tests/unit/utest_telnet_negotiation.cpp`，10 案）。落地时按新增的 **F14** 读数把 NAWS 载荷次序改成**宽在前**，稿面四处一并改口，入册裁决 **7.113**／v0.113。结项读数：`ctest --preset linux -E etest_` **78 项全绿**、`borealis_test_runner` **963 案全绿**（§4.4 第 21 条）；非空转四条变异各自转红（摘 `FF FF` 反转义 3 案红／解析状态不跨读保留 ⇒ 等价性与断裂案红／宽高次序写反 3 案红／摘同值去重 1 案红，第 22 条）。
- **2026-10-11 批 2 落地（连接腿 + 平台 `set_nodelay` + 环回 itest）**：两笔提交——① `src/platform/tcp.h` 的 `TcpStream` 追加纯虚 `set_nodelay()`，POSIX 腿 `setsockopt(IPPROTO_TCP, TCP_NODELAY)`、Win 腿同形但**未经 MSVC 编译**（F12），`utest_tcp_loopback` 补一枚用例（两端回 true 且幂等／设后 12 字节往返照旧／`close()` 后回 false）；② `src/conn/telnet_connection.{h,cpp}` 私有头连接腿（稿 §4 那一行的形态：`TcpStream` + 读线程 + 协商件 + `telnet_close_reason()` 归因表 + 复用 `conn/reconnect.h` 退避环），`src/CMakeLists.txt` 手写进源列表，`tests/integration/itest_telnet_connection.cpp` **十三案**在真环回 socket 上证协议面。结项读数：`ctest --preset linux -E etest_` **79 项全绿**、`borealis_test_runner` **977 案全绿**。四则变异各打红、还原后 `cmp` 逐字节相同：摘写出侧转义 1 案红／NAWS 宽高次序写回 2 案红（另带动 `utest_telnet_negotiation` 4 案红）／EOF 归成 `LinkLost` 2 案红／尺寸槽只在建连时取一次 1 案红。
  - **RST 的造法**（本棒最值得留档的一处）：`platform::TcpStream` 不暴露 fd，`SO_LINGER(1,0)` 那条探针路径要用裸 socket——那会把平台知识带进 tests（AGENTS §4.5 第 23 条）。改用内核本就有的行为：**一条 socket 带着未读字节被关闭即发 RST**，故假服务端「只吞发起序的前 4 字节」再关（后 2 字节留在接收队列）⇒ 本端 `recv` 得 ECONNRESET ⇒ 归 `LinkLost` 进环；同一台替身随后回到 `accept`，第二回受理就是「按同一份档案重拨并真连上」的直接证人，不需要第二台服务端。
  - **一处替身的析构顺序是硬约束**：事件记录替身必须**声明在连接之前**，它才比连接活得久。顺序倒了，通过的用例毫无影响、**失败**的用例会在栈展开里让读线程撞上已销毁的替身——实测 `pure virtual method called` 掀掉整个 runner，红线退化成没有读数的崩溃。串口那棒（`itest_posix_serial_connection`）同形的五处一并改正。这不是风格问题：第 22 条的变异自证全靠「红线可读」才成立。
  - `set_nodelay` 在连接腿的调用**没有字节级证人**：环回上 ACK 即时，Nagle 的有无观测不出差别（F11），故本棒只证「拨号后确实呼过、失败只记日志不改归因」，手感差值留真机走查第 ⑹ 条。这是如实登记，不是已证。
- **2026-10-11 批 3 落地（向导第四型 + `src/main.cpp` 装配 + 词条族）**：三笔提交——① 向导（`src/ui/connection_wizard.{h,cpp}` + `src/ui/settings_i18n.cpp` + `tests/integration/itest_connection_wizard.cpp`）：类型卡四张，步骤 1 的 Telnet 字段集三行，主机／端口两行**与 SSH 共用一处构造**（`add_host_and_port_rows()`），端口留空折成「该型缺省」；② 装配（`src/main.cpp`）：`conn::TelnetConnection` 进 `Session`，编码吃既有 `make_session_encoding(settings)`，行尾取文件级常量 `kTelnetLineEnding = Crlf`，`connect_profile`／`restart_current_tab` 两处分流 + `perform_close` 撤账，`telnet_by_tab` 只存档案；③ 本条文档回写（裁决 **7.115**）。读数：`ctest --preset linux -E etest_` 79 项全绿（批 3 不新增测试 TU），`borealis_test_runner` 全跑 980 案全绿（977 ＋ 向导三案）。
  - **两处「不崩但静默走错腿」的分派陷阱**（本棒最值得留档的一处）：`build_content()` 的字段集分派原形是 `if (Ssh) … else if (Serial) … else {Local}`，第四型若不写**显式** `else if (Telnet)`，就会静默拿到「命令／工作目录」两行——界面腿的用例靠「别的型那十三枚标签一枚都不许出现」把它钉住（变异⇒ 3 案红）。装配侧同一形状：Telnet 腿必须**挡在** `secret_ask_for()` 之前，缺这一支不崩而是拿空白 `profile.ssh`（`auth_method` 缺省 "agent" ⇒ `SecretAsk::None`）去拨空主机 SSH，标签名却还是 Telnet 档案名。两处的共同症状都是**界面照常、连接悄悄换了型**，所以判据必须写「不许出现什么」而不是「出现了什么」。
  - **预填与「留空即缺省」是两条判据，不是一条**：`collect_draft()` 里留空折 23 只保证产值，而字段集显示的仍是草稿结构体带着的 22——用户看不见「留空＝23」这条口径。据此在点类型卡那一刻把 `draft_.port` 预填成该型缺省（症状出现在「先编辑 SSH 档案、再切 Telnet」那条腿上）。
  - **控件不在场时交回 `nullopt` 而非折缺省**：端口行的读取折成 `std::optional<int>`——编辑别的型时那两只控件根本没建，若一律折成缺省，一次「切类型再切回来」就会把用户没碰过的主机与端口清空。
  - **一处测试面的错**（不是 `src/` 缺陷）：用例初版往预填的 "23" 后面直接敲 "2323"，`collect_draft()` 折出 232323 ⇒ 落 `PortOutOfRange`，红线看着像实现的错、其实是测试自己没清场。改成先 `End` 加两次退格再输入，并把「字段内容先断一次」写进用例——断字段内容比断结果更早暴露这类错。
  - **装配那一行无自动化证人**：`src/main.cpp` 不编入 CTest runner（裁决 7.83 的物理事实），故批 3 的变异自证全部落在向导侧，装配侧的分流／行尾常量／超时档只由编译＋链接与 §6 走查兜。这条如实登记在 §6 末段与裁决 7.115⑦，**不称已证**。
- **本稿最需要人判的三条与其结果**：D6（要不要动 `platform::TcpStream` 加 `set_nodelay`——**裁：加，只 Telnet 腿呼**）、D7（对端不回显时本期不做本地回显，用户可能看不见自己敲的字——**裁：不做也不提示**）、D10（NAWS 是否本期顺带——**裁：改判为做**，代价与本节 §2 判据 8／§5 尺寸腿／§6 组包用例一并生效）。
- 探针复现口径（一次性脚本，不入仓，全在沙箱临时目录）：
  - `telnet_probe.py`：环回假服务端起停（F2）、协商命令与服务端文本的到达形态、客户端发起字节被对端逐字节收到（判据 3/4）、`FF FF` 在数据段的往返（F7）、有序 FIN 的 EOF 形态（F3 的一半）、空闲读超时不吞数据（F13）。
  - `telnet_probe2.py`：逐字节 `recv` 下的命令断裂（F6 的弱形态）、拒连 errno 111（F9）、`localhost` 经 AF_INET 解析（F10）、无协商直接灌数据 burst（假服务端可纯数据）。
  - `telnet_probe3.py`：`TCP_NODELAY` + 写读交替下 `[255] [251] [1]` 三次读各 1 字节（F6 的确证）、对不可路由地址的 connect 以 0.40 s 超时返回（F9 的另一半）。
  - `telnet_probe4.py`：`SO_LINGER(1,0)` 的 **ECONNRESET(104)** 读数（F3 的另一半）、半关后写侧仍连续成功（F4）、一条 `recv` 混排三命令 + 文本（F5）、`SB … FF FF … SE` 载荷（F7）、参考客户端（GNU inetutils 2.7）连上后 2 s 内**零字节**（F8，pty 与 `stdin=DEVNULL` 两跑一致）。
  - `naws_probe.py`（批 1 落地时补，F14）：环回假服务端向 GNU inetutils 2.7 客户端发 `IAC DO NAWS`，客户端跑在 `TIOCSWINSZ` 设成 20 行×60 列的 pty 上，服务端收到 `FF FB 1F FF FA 1F 00 3C 00 14 FF F0` ⇒ 载荷次序＝宽在前、各大端两字节（与 RFC 1073 §2 语法行、§6 的 80×24 ⇒ `0 80 0 24` 同读数）。
  - **读数只在本沙箱成立**：换到真 telnetd / 真设备后 F8（对端是否先动）、D8（行尾档是否被接受）、D10（NAWS 发出去之后远端是否真的按新尺寸重排）三档须重取并回写本稿。
- 实现分批（裁决已落，按此走，与串口那棒的三批同构）：**批 1**＝档案模型（`profile.h` / `store.cpp` / `profile_views` / `utest_config`）+ `conn/telnet_negotiation` 纯逻辑件与其 `utest`（含 D10② 的 NAWS 组包与去重）；**批 2**＝`conn/telnet_connection`（含尺寸腿）+ `platform::TcpStream::set_nodelay`（D6①）+ 环回 `itest`；**批 3**＝向导第四型 + `main.cpp` 装配 + `itest_connection_wizard` 补例 + 词条族。
