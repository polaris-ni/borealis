# CONN.03 跳板机（ProxyJump 语义）设计稿

> 状态：草稿（2026-10-24）。本稿只覆盖 `SPEC.FEAT.CONN.03` 的延后子项「跳板机（ProxyJump 语义）」；实现与文档回写分在后续批。所有事实均在本仓当前工作树或本机 vcpkg 档上实测；未经实测的推断逐条标「推断／未验证」。

## 0 需求与边界

需求原句（`codespec/SPECIFICATIONS.md` §4.5 `SPEC.FEAT.CONN.03`）：

> 连接档案：分组/收藏/搜索、quick connect（临时 host:port 输入即连）、`~/.ssh/config` 只读导入（变更检测，单向）；**跳板机（ProxyJump 语义）为延后子项**。

`codespec/PLAN.md` §4 给该子项的目标阶段是 M4，CONN.02 的连接复用（同一张表的另一条 M4 延后项）已于 2026-10-24 落地（裁决 7.147），本条按既定顺序开工。

本稿交付：档案可登记 1..N 跳、终端腿按跳链建连并逐跳按各自的主机密钥策略裁决、跳链进入连接复用的共享键与秘密判据、`~/.ssh/config` 的 `ProxyJump` 键能被导入、向导给一行输入。

本稿不做（逐条去向见 §9）：`ProxyCommand`（外部命令路线）、逐跳凭据的询问 UI、跳板与 SFTP／隧道／密钥推送三条腿的接线（本稿只给它们的 fail-closed 口径，见 D6）、`LocalForward`／`RemoteForward` 与跳板的组合、跳板链路上的字节背压上界。

## 1 地基实测

| 编号 | 事实 | 证据（相对路径 + 章节／符号） |
|:---|:---|:---|
| F1 | `SshProfile` 现有八个字段（host／port／user／auth_method／identity_file／agent_forwarding／known_hosts_policy／credential），**没有任何跳板位**；`Profile` 的外壳也不含链路概念。加跳板必须动公共头 `include/borealis/conn/profile.h`，是追加式扩字段（既有成员与次序不动）。 | `include/borealis/conn/profile.h` 的 `SshProfile` |
| F2 | 「建会话选项 → 连接 → 主机密钥核对 → 认证」已经是一条共用腿：四个消费方（终端共享会话、SFTP、隧道、密钥推送）全部调 `ssh_dial_and_authenticate()`，跳链的接入点因此**只有这一处**加一处链对象的所有权。 | `src/conn/ssh_dial.cpp` 的 `ssh_dial_and_authenticate`；`src/conn/ssh_shared.cpp` / `src/conn/sftp_client.cpp` / `src/conn/tunnel_client.cpp` / `src/conn/key_push.cpp` 的调用点 |
| F3 | 连接复用的共享判据是 `SshSessionKey`（生效参数全等）再叠 `same_secret`（秘密逐字节全等）。**跳链不入键就会错借**：同一目的地经直连与经跳板是两条语义不同的链路；跳板秘密不比较就是把两个跳板凭据不同的标签接到同一条已认证链路上。 | `src/conn/ssh_shared.h` 的 `SshSessionKey` / `same_secret` / `SshSessionRegistry::acquire` |
| F4 | `~/.ssh/config` 的导入器是「纯函数 + 键白名单」，`ProxyJump` 那一行现在就落在末尾的注释分支上被丢弃（「ProxyJump 等其余键本切片延后」）。新增 `proxyjump` 键只需在 `else if` 链里加一臂。 | `src/conn/profile.cpp` 的 `parse_ssh_config` |
| F5 | 向导的 SSH 字段集是「主机／端口／用户／认证方式／私钥」五行加展示名一行；`collect_draft()` 按类型分流取值，没有跳板栏，也没有逐跳的凭据入口。 | `src/ui/connection_wizard.cpp` 的 `collect_draft` / 字段构造段 |
| F6 | libssh 0.12.0 **有**原生 ProxyJump：`SSH_OPTIONS_PROXYJUMP`（逗号分隔 `[user@]host[:port]`，特殊值 `none` 关闭）与 `SSH_OPTIONS_PROXYJUMP_CB_LIST_APPEND`（`struct ssh_jump_callbacks_struct{userdata, before_connection, verify_knownhost, authenticate}`）。该 struct **没有 `size` 字段**，`ssh_callbacks_init()` 直接编译不过；库只把指针存进会话链表，故回调结构与其 `userdata` 必须比会话活得久。 | 本机 vcpkg 档 `libssh/libssh.h` 的 `ssh_options_e`、`libssh/callbacks.h` 的 `ssh_jump_callbacks_struct`（探针首版按 `ssh_callbacks_init` 写即编译失败，实测） |
| F7 | 原生实现的**射程只到 POSIX**：`ssh_socket_connect_proxyjump()` 与其 jump 线程整段落在 `#ifndef _WIN32` + `#ifdef HAVE_PTHREAD` 内，且 `ssh_connect()` 里那条 `else if (proxy_jumps 非空)` 分支被同一对守卫包着。⇒ 在 Win32 上设了 `SSH_OPTIONS_PROXYJUMP` 不会报错，而是**落到直连分支**（fail-open）。本仓主通道是 MSVC＋Win32。 | libssh 0.12.0 上游 `src/socket.c` 的 `ssh_socket_connect_proxyjump`（守卫起止）与 `src/client.c` 的 `ssh_connect` 分支；本机未编译 Win32 档 ⇒ 该条结论按源码守卫记为「读码所得，未实测」 |
| F8 | 原生实现给每跳 `ssh_new()` 一份**全新会话**，除日志级别外不继承根会话的任何选项（超时、算法表、known_hosts 路径、凭据都不带）。唯一的注入口是 `before_connection` 回调。 | libssh 0.12.0 上游 `src/socket.c` 的 `jump_thread_func` 起始段与 `ssh_socket_connect_proxyjump` 里的 `SSH_OPTIONS_LOG_VERBOSITY` 传递 |
| F9 | 原生缺省的两处策略与本仓模型不兼容：跳板主机密钥走库内 `verify_knownhost()`（要求 `SSH_KNOWN_HOSTS_OK`，**不接受新主机**），跳板认证走 `ssh_userauth_publickey_auto()`（只用 agent／默认私钥，不吃口令）。探针 P2：不给任何回调时 `ssh_connect` 直接失败（`Connection reset by peer`），跳板服务器的 `verify_knownhost` 现场报 `SSH_KNOWN_HOSTS_UNKNOWN(0)`。 | 探针（本棒一次性件，输出随本表登记）；库侧 `src/socket.c` 的默认分支 |
| F10 | 原生多跳的**次序与回调对应关系已实测**：`ProxyJump="A,B"` 时 A 是最靠近本机的第一跳——A 的服务器收到 1 次 `direct-tcpip` 且转发目的＝B 的端口，B 的转发目的＝目标端口（与 OpenSSH 语义一致）。回调表按**跳序 append** 即正确：两跳各用自己的口令认证并整链成功（库对 jumps 与 cbs 两张表都用 `ssh_list_prepend`，方向因此天然对齐）。 | 探针 P4；库侧 `src/config.c` 的 `ssh_config_parse_proxy_jump`（prepend 注释「因为我们要递归 proxy jump」）与 `src/socket.c` 的两次 `ssh_list_pop_head` |
| F11 | **自建路线端到端跑通**（探针 P1，同一台机器、同一份 vcpkg 静态库）：跳板会话正常「连接→认证」→ `ssh_channel_open_forward(ch,"127.0.0.1",目标端口,"127.0.0.1",65535)` → `socketpair(AF_UNIX,SOCK_STREAM)` → 自建中继线程（一枚 `ssh_event` 挂两枚 connector：`in_fd/out_channel` 与 `in_channel/out_fd`）→ 目标会话 `SSH_OPTIONS_FD = pair[0]` 后 `ssh_connect` + password 认证 + PTY/shell 全通，回显往返 OK。跳板服务器侧用 server 回调 `channel_open_request_direct_tcpip_function` 接受并中继即可，libssh 的 server 腿原生支持该回调。 | 探针 P1 输出；本机 vcpkg 档 `libssh/callbacks.h` 的 `ssh_channel_open_request_direct_tcpip_callback`、`libssh/server.h` |
| F12 | 自建路线有一条**硬次序约束**（探针踩过）：中继线程持有跳板会话与通道指针，必须**先 join 中继线程**再 `ssh_channel_free`／`ssh_free`，否则进程段错误。探针首版按「先拆会话后停线程」写即崩。 | 探针 P1 首版崩溃与修正后的通过读数 |
| F13 | ~~假服务器没有 `direct-tcpip` 回调，当不了跳板~~ **批 3 已扩口**：`FakeSshServerOptions::allow_direct_tcpip`（缺省关——普通服务器不该代收转发，关掉即「跳板拒绝转发」那条失败档的证人），开则按回调真 `connect()` 到目的地并双向搬字节；事实面新增 `direct_tcpip_requests()`（含被拒的请求：拒拨也要数得到）与 `forwarded_destinations()`（`host:port` 按到达次序，E2 的跳序证人）。 | 落地件 `tests/support/fake_ssh_server.{h,cpp}`；证人 `itest_posix_ssh_proxy_jump` |
| F14 | `SSH_OPTIONS_FD` 那条分支**不在平台守卫内**（`ssh_connect()` 里 `opts.fd != SSH_INVALID_SOCKET` 独立成一支）⇒ 自建路线的客户端侧是平台中立的；平台相关部分只剩「造一对可对等读写的 socket」这一件事（`socketpair` 是 POSIX 专有）。 | 库侧 `src/client.c` 的 `ssh_connect` 分支次序；POSIX 语义按 `socketpair(2)` |
| F15 | 原生实现还有一条对本仓致命的形态：跳板中继循环检查一个**文件级全局** `int proxy_disconnect`，而 `ssh_disconnect()` 在根会话上会把它置 1、下一次根拨号又把它清 0。⇒ 任一标签断开会叫停**其它标签**仍在跑的跳板中继，多标签正是本仓的核心形态，行为不可预期且无法从外部封堵。 | 库侧 `src/socket.c` 的 `int proxy_disconnect = 0` 与循环内检查、`src/client.c` 的 `if (session->proxy_root) proxy_disconnect = 1;` |
| F16 | libssh 0.12 的 POSIX `ssh_get_user_home_dir_internal()` **先** `getpwuid_r(getuid())`，只有失败才回退 `getenv("HOME")` ⇒ 「临时 `$HOME`」隔离不了 known_hosts；`ssh_session_update_known_hosts()` 会写进真机 `~/.ssh/known_hosts`（`ssh_mkdirs` 连 `.ssh` 目录一起建）。 | 库侧 `src/misc.c` 的 `ssh_get_user_home_dir_internal()` 与 `src/knownhosts.c` 的写盘路径 |
| F17 | `SSH_OPTIONS_FD` 的写入处是 `session->opts.fd = *x & 0xffff;`——**句柄被截成 16 位**；且 `ssh_disconnect()` **不关**递进去的 fd。⇒ POSIX 的小整数 fd 侥幸安全（本仓集成件实测通过），Win32 的 `SOCKET` 是 64 位核句，截断风险真实存在；seed 句柄的关闭责任**落在本仓**（`SshHopChain::close()` 逆序关两端）。 | 库侧 `src/options.c` 的 `SSH_OPTIONS_FD` 分支、`src/ssh.c` 的 `ssh_disconnect()` |

## 2 目标与非目标

**目标**

1. 一张 SSH 档案能表达「经 1..N 跳到达目标」，逐跳有自己的 host/port/user/认证方式/主机密钥策略/凭据句柄。
2. 终端腿按跳链建连：第一跳走真 TCP，其后每一跳经上一跳的 `direct-tcpip` 通道，目标会话用 `SSH_OPTIONS_FD`；逐跳的主机密钥裁决与认证都走本仓既有的策略口径（`policy_accepts` + `ssh_dial_and_authenticate`）。
3. 跳链进入连接复用的共享键与秘密判据，不破坏 CONN.02 已落的语义。
4. `~/.ssh/config` 的 `ProxyJump` 键导入为跳链（单向、只读，与既有导入同形态）。
5. 向导一行输入（OpenSSH 语法），档案 JSON 落盘只含凭据句柄、不含明文。
6. 没接线的消费腿在带跳档案上 **fail-closed**：拒拨并说明原因，绝不静默直连。

**非目标**

- 逐跳口令的询问 UI 与「跳板链上再套 agent 转发」的组合验证。
- `ProxyCommand`／外部 `ssh -W` 路线。
- 跳板链路的字节背压上界与限速。
- 跳板与 SFTP／隧道／密钥推送的接线（留本稿 §9 的后续批）。

## 3 决策

### D1 路线：自建 `direct-tcpip` 跳链，不采用库的原生 ProxyJump

四条实测事实共同否决原生（F7 Win32 静默直连＝fail-open；F8 每跳策略不继承；F9 缺省不接受新主机且只认 publickey；F15 进程级全局 `proxy_disconnect` 造成跨标签串扰），而 F11 已经证明自建路线在同一份库上跑得通。

自建路线的结构性收益：逐跳复用 `ssh_dial_and_authenticate()`，于是**每一跳都吃本仓自己的主机密钥策略、认证方式、算法覆盖项与日志口径**，失败归因也与单腿同型；跳链与中继线程的所有权落在我们自己的 I/O 线程上，可 join、可清空文（CONN.09 的「明文不比认证活得久」在原生路线里无法兑现——回调 `userdata` 的存活期由库的分离线程决定，我们既看不到也管不住）。

代价（如实登记）：我们自己拥有中继线程与其关闭次序（F12），以及一枚新的平台接缝（D10）；`socketpair`＋中继线程的形态每跳多一根线程与两次拷贝。

### D2 数据模型：平铺一跳 `SshHop`，档案持 `std::vector<SshHop> proxy_jumps`

```cpp
struct SshHop {
    std::string host;
    int port{22};
    std::string user;
    std::string auth_method{"agent"};
    std::string identity_file;
    KnownHostsPolicy known_hosts_policy{KnownHostsPolicy::AcceptNew};
    SecretHandle credential{};
    [[nodiscard]] auto operator==(const SshHop &) const noexcept -> bool = default;
};
```

不复用 `SshProfile` 承载跳：那需要一个 `std::vector<SshProfile>` 自成员，递归类型 + 类内 `= default` 的比较是纯负担，而「跳板本身不再套跳板」本来就是本期的射程边界（跳序由数组表达，不递归）。

**跳序＝数组序**，第 0 项是最靠近本机的第一跳（F10 的 OpenSSH 语义）。`SshProfile` 只追加这一枚末位成员，既有八项不动，`operator== = default` 继续成立。

条数上限 `kMaxProxyHops = 8`：解析侧与保存侧两处都拒（向导里误粘一大串的可能性远多于真用八跳）。

### D3 纯逻辑件：`src/conn/proxy_jump.{h,cpp}`（解析／规范化／校验／串形）

- `parse_proxy_jump_list(std::string_view) -> std::optional<std::vector<SshHop>>`：OpenSSH 的逗号分隔 `[user@]host[:port]`；端口留空折 22；`none`＝空链路（与库的语义同名，也便于导入侧表达「显式关闭」）；任一枚 token 非法即**整串拒绝**，不做部分接受（部分接受会让用户以为跳板生效了）。
- `render_proxy_jump_list(hops) -> std::string`：规范化串形（`user@host:port` 逐项逗号连接），同时用于向导回填、共享键的字符串位、导入去重与日志。
- `validate_proxy_hops(hops) -> bool`：条数上限、host 非空、port 1..65535。

该层完全不碰 libssh，也不碰平台（AGENTS.md §4.4 第 20 条的「纯逻辑可独立单测」）。`SshHop` 的 `auth_method`／`known_hosts_policy` 由解析器给缺省（`agent`／跟随所在档案的策略），逐跳覆盖的写入口本期只有 JSON 档案与 `~/.ssh/config` 导入。

### D4 拨号腿：`SshHopChain` 拥有「跳板会话 + 中继线程 + 交出的 fd」

新增私有件 `src/conn/ssh_hop_chain.{h,cpp}`（libssh 类型只以前向声明指针出现，同 D1① 纪律）：

```cpp
class SshHopChain final {
  public:
    SshHopChain();                    // 构造与析构都只在实现文件里定义（见下）
    ~SshHopChain();
    /// 逐跳建立链路；成功后 seed_fd() 交给目标会话。
    auto open(const SshProfile &target,
              const std::vector<std::optional<std::string>> &hop_secrets,
              const SshAlgorithmOverrides &algorithms) -> bool;
    [[nodiscard]] auto failure() const noexcept -> DialFailure;  ///< 档位 + 第几跳。
    [[nodiscard]] auto seed_fd() const noexcept -> std::uintptr_t;
    auto close() -> void;   ///< 逆序收口：join 中继 → 关通道 → 断会话 → 关句柄对
};
```

落地时对稿面签名做了两处修正，都是「签名表达不出被调方要做的决定」那一类：

- **`open()` 的形参是目标档案，不是裸跳链**。最后一跳的转发目的地＝目标的 host 与生效端口，而这份信息只存在于 `SshProfile` 上；`proxy_jumps` 是它的成员，所以形参换成档案本身，跳链从里读。副带收益：目标的 `effective_port()` 折叠（22／0）只有一处口径。
- **构造与析构都不在头里 `= default`**。pimpl 的 `unique_ptr<Impl>` 要等 `Impl` 是完全类型才能生成析构；头里默认会让第一个包含本头的用例直接撞上「不完整类型」。已实测（用例侧的编译错误就是这条的证人）。

- 逐跳的「连接→核对→认证」全部交给 `ssh_dial_and_authenticate()`：跳板 hop 折成一份临时 `SshProfile`（`agent_forwarding` 恒 false，跳板不需要转发 agent——本期不为跳板开这条腿），第一跳不交 seed，其后各跳交上一跳的 fd。
- `ssh_dial.h` 的签名只追加一枚末位缺省参数：`std::uintptr_t seed_fd = 0`（0＝无 seed，走真 TCP）。选 `uintptr_t` 而不是 `socket_t`／`ssh_session_struct*`，是为了让公共面不吃 libssh 与平台类型；`ssh_dial.cpp` 内部再折成 `socket_t` 写进 `SSH_OPTIONS_FD`。
- 失败归因：`DialOutcome` 增两档——`HopUnwired`（消费腿没接线，见 D6）与 `Hop`（跳板侧失败）；`Hop` 具体卡在跳板的哪一档由 `DialFailure{outcome, hop_index}` 携带，hop_index 等于 `hops.size()` 表示目标本身。WS.05 的关闭归因与日志只多一个「第几跳」的数，公共语义不变。
- 中继（落地形态与稿面原写不同，偏差已实测确认）：每跳一根线程，**不采用探针里的 `ssh_event` + 两枚 `ssh_connector`**，而是一个手写的双向泵——方向一 `seed_side.read(…, 10 ms)`（这一读即一拍），方向二 `ssh_channel_poll_timeout` + `ssh_channel_read_nonblocking` 后写回 seed；任一方向到尽头即退，线程退出前关掉本地这一端。理由三条，逐条都有证人：
  1. `ssh_event` 只盯 libssh 自己认识的 socket，**不认识本仓替客户端连出去的第二条 TCP**（假服务器的转发腿正是这条），方向二必须自己每拍自扫；
  2. 库的 `ssh_connector` 族在隧道腿里已按弃用处理，且一对 connector 只能服务一条通道，与「停止位 + join」的关闭序（F12）配合不起来；
  3. 同一形态在仓内已有先例并已过真协议证人——`src/conn/tunnel_client.cpp` 的 `pump_channel()`（按拍轮询、双向搬运、任一方向收摊即退）。
  线程循环仍每 10 ms 一拍并检查停止位；`close()` 先置停止位再 join（F12）。

### D5 与连接复用（CONN.02）的关系：跳链属于「链路」，不随标签

- `SshSessionKey` 追加一枚 `std::string proxy_jumps`，取值＝`render_proxy_jump_list(profile.proxy_jumps)`（归一口径与端口折叠都在 D3 里做完，于是「留空端口」与「:22」不会各拨一条链）。
- `same_secret` 的判据从「一份目标秘密」扩为「目标秘密 + 逐跳秘密」逐项全等；`SshSharedSession::matches()` 同步。跳板秘密仍**不进表**（`SshSessionRegistry` 的表项只有键与弱引用，比较时问活着的会话）。
- 跳链与中继线程由**共享会话的 I/O 线程**拥有：拨号时在 I/O 线程上 `open`，链路终态或末位租约收口时 `close`。重拨＝整链重开（跳板也重连）——这与 7.147 已落的「一条退避环在链路级」一致，不新造「跳板级」的环。
- **落地形态（批 4 实测）**：`SshSessionKey::proxy_jumps` 与 `same_secret(bundle, bundle)` 按原文逐字落；逐跳秘密存在 `SshSharedSession::Impl` 内、与目标秘密同受一枚 `secret_mutex` 管，表项仍只有键与弱引用（稿面「跳板秘密不进表」成立）。`open` 在 `dial_then_open_all()` 里、目标 `ssh_new()` 之后且在 `ssh_dial_and_authenticate()` 之前——建立序即 §5 那一条；`close` 落在 `teardown_attempt()` 末段，**在目标会话 `ssh_free` 之后**（否则就是取用已关的 seed 句柄，F17 那条所有权在代码里以注释固化）。重拨复用同一枚 `hop_chain`（`open()` 自己先收旧链），每轮重拨从锁下取一份秘密副本，`secret` 本体留着给下一轮。

### D6 未接线腿的 fail-closed 口径

`sftp_client`／`tunnel_client`／`key_push` 本批不改一行代码，但 `ssh_dial_and_authenticate()` 一旦看到 `profile.proxy_jumps` 非空而 `seed_fd` 未交、链也未建（这三条腿根本不会建链），就记一条 ERROR 并回 `HopUnwired`，**不发起连接**。

理由：静默直连等于绕过用户显式配置的跳板——在 Windows 上这正是原生路线的既有行为（F7），本仓不接受。接线留后续批（§9 批 6），届时三条腿各自持有一份 `SshHopChain`。**批 6 回写（裁决 7.153）**：三条腿已各自建链，本闸退为 dial 层兜底档——只兜「漏接线」，不再是三腿的现行口径。

**落地形态（批 4 实测）**：三条腿仍不改一行代码——`HopUnwired` 从 `ssh_dial_and_authenticate()` 的返回值现形为各腿自己的失败档：SFTP ⇒ `connect()` 回 `false` 且 `last_error() == SftpError::Network`；隧道 ⇒ 首轮拨号即失败，经退避环收在 `TunnelState::Failed`（`max_attempts=1` 时零 Backoff）；密钥推送 ⇒ `PushStage::Dial` 且 `failure` 有值。三腿各证一次落在 `itest_posix_ssh_proxy_jump.sftp_tunnel_and_key_push_legs_refuse_a_profile_with_jumps`，末尾以两侧假服务器 `accepted_connections() == 0` 收口（拒拨＝不发起连接，而不是连上再断）。

### D7 序列化与导入

- `src/config/store.cpp` 的 ssh 段新增 `proxy_jumps` 数组（逐元素写 D2 的七个字段，`credential` 仍只写句柄；总写，空链路写空数组）。**旧档无该键＝空链路**，读侧缺省即空，不产生迁移。读侧坏值口径与 `read_ssh` 其余各键一致：**逐项留痕 + 回落结构初值**，绝不「丢这一键」——元素坏就留一枚空元素占位（`rejected_keys` 里指着那一格），超上限也只留痕不截断，合法性闸在拨号侧（D3 的 `validate_proxy_hops`）。理由同 D6：丢了跳板就等于直连。
- 明文凭据审计（`kBannedKeyNames` + `find_credential_key`）必须覆盖新数组的每一元素——新增一条审计用例专门把跳板里的 `secret` 摆进配置树里抓红。导出件的句柄剥离同批扩到逐跳（`SPEC.FEAT.PREF.07` 那条哨兵形态）：跳板凭据同样换成「每次询问」，`profile_id` 回指**所属档案**（跳板自己没有档案条目）。
- `parse_ssh_config` 加 `proxyjump` 一臂：逗号拆串后逐 token 走 D3 的语法；`%h` 之类的 OpenSSH 宏本期不展开，含 `%` 的 token 即语法坏。**坏链的取舍是「整块不导入」而不是「丢这一键」**（比稿面原写的「整键丢弃并记 WARN」更保守一档，且本件是无日志的纯逻辑件——少导入一条如实反映在导入条数上，而留下一条「以为走跳板、实则直连」的档案无人能看见）。hop 的 `auth_method` 取 `agent`、`known_hosts_policy` 跟随同块的 `StrictHostKeyChecking` 折算结果（值原样留到成块时才解析，故两种书写次序同判）；`ProxyCommand` 继续不导入（在册）。
- **快照与撤销两处都要跟着走（实测点）**：`SPEC.FEAT.WS.11` 的会话快照把 SSH 身份**逐字段抄进 JSON**（`src/ui/workspace_snapshot.cpp` 的 `host`/`port`/`user`/`auth_method`/`identity_file`/`agent_forwarding`/`known_hosts_policy`/`credential` 八个键），所以 `proxy_jumps` 必须同批加进那对编解码——否则「重启恢复」会拿着丢了跳板的快照**直连**，正是 D6 要防的那一形态；缺该键＝空链路（旧快照兼容，这一键是模型追加而非文档形态改版，是本件唯一允许缺键的一格），键在则整段严格、坏元素整份拒绝。键的**位置**也要与 `store.cpp` 同（`credential` 之后）：判据 ⑦ 的字节等式案比的是两边 `dump` 的原文，键序漂了就红。撤销栈的 `ClosedTabSpec` 存的是 `std::optional<conn::SshProfile>` 内存态快照（`include/borealis/ui/closed_tab_stack.h`），随模型自动携带跳链，不需要新增序列化面。

### D8 装配与凭据解析

- 目标与逐跳的秘密在**开标签时一次性解析**，折成 `SshSecretBundle{ std::optional<std::string> target; std::vector<std::optional<std::string>> hops; }`；`src/main.cpp` 的 `ssh_secret_by_tab` 的 value 由 `std::optional<std::string>` 换成该 bundle（「留档副本先取、最后一份 move 走」的既有语义原样保留，重拨／切分／恢复都用同一 bundle）。
- 询问 UI 本期仍只覆盖目标口令。某跳是 `AskEveryTime` 且没有材料 ⇒ 该跳按 `Auth` 失败并记明第几跳（不新造询问链，见 §9）。
- 六条 SSH 腿（新开／分屏／重启／撤销／恢复／CLI 转交）都经 `make_ssh_session` 一处装配，bundle 只需在这一处进 `SshConnection` 的形参（末位缺省参数追加，编译面不变）。

**落地形态（批 4 实测，两处与稿面不同）**：

- 形参不是「末位缺省追加」而是**换型**：`make_ssh_session(const SshProfile &, SshSecretBundle)`。走通的理由是 `SshSecretBundle` 有一枚从 `std::optional<std::string>` 出发的隐式构造，所以只吃目标口令的既有调用点（CLI 转交、SFTP 面板）不必改写；`ssh_secret_by_tab` 的 value 确实换成了 bundle，「留档副本先取、最后一份 move 走」原样保留（`build_restored_tab` 里 `retained_bundle` 与逐叶 `material` 那一对）。
- 逐跳取料新增一处接缝 `with_hop_secrets(profile, target_secret, done)`：六条腿的装配点都早于 `credential_store` 的声明，故沿用本仓既有的「先声明槽、后写实现」形态（同 `restore_secret_sink`／`launch_tunnel`）。跳链为空或无可取句柄 ⇒ **同步回调**，不带跳板的档案时序与批 4 之前完全一致；有句柄才走 `au::async` 后台取料，取料闭包只捕获 store 副本与句柄表（不持 UI 对象）。恢复泵因此多出**第二个挂起点**（逐跳补齐），与既有的询问挂起共用「游标 + 放行口」那一套；放行口是一次性的（move-and-clear），且不能在 `pump_restore` 自身调用栈里被赋成 `pump_restore`——那等于销毁正在执行的闭包。
- `hop → SshProfile` 的折叠与「这一跳该取哪个凭据句柄」的计划收在 `src/conn/profile_views.{h,cpp}` 一处主人（`hop_profile_of`／`plan_hop_credential`）：链侧（`ssh_hop_chain.cpp`）与装配侧（`main.cpp`）共用同一份折算，避免「凭据计划」在两处各写一遍而漂移。

### D9 向导与设置

- SSH 字段集追加一行「跳板机（ProxyJump）」单行输入，占位串形 `jump1.example.com,jump2:2222`；词条 `connections.field.proxy_jump` 中英双档进表（`SPEC.FEAT.PREF.05` 的覆盖闸会盯表外中文）。
- `collect_draft()` 里经 D3 解析；解析失败就在该行给红字并禁止保存——沿用向导既有的端口校验形态，不新增控件类型。
- 侧栏档案行不显示跳链（一行放不下，且「连到哪里」的判据仍以目标主机为准）；quick connect 不接 `-J` 语法。
- **不新增设置键**：跳链是档案属性，不是全局档（与 `kex_algorithms` 那类全局档的区别：后者是「算法面」，前者是「目的地拓扑」）。
- 延后角标：`codespec/UI_OVERVIEW.draft.md` §2.3（延后能力清单）与 §3（树行的禁用态注记）、`UI_CONNECTIONS.draft.md` §1／§7 把 ProxyJump 标在「延后能力一律灰置不画成可用态」的清单里，落地后同批改口。

落地形态（批 5 实测，三处与稿面不同）：

- **草稿存串形而不是存解析好的跳链**：`ProfileDraft::proxy_jumps_text`（`src/conn/profile_views.h`）。理由不是省一次转换，而是坏值得有地方待着——解析放在纯逻辑层（`validate_draft` 判定、`draft_to_profile` 折算各一次调用同一枚 `parse_proxy_jump_list`），向导不碰语法，于是「坏链禁保存」这条判据可无头断言（`utest_profile_views`），而 UI 侧只需证「值收进了草稿、留痕行上了屏」。
- **红字位置改口**：稿面写「在该行给红字」，向导实际**没有逐行错误槽**这一形态——端口那一档、串口四档、Telnet 三档都走底部那条常驻留痕行（`notice_`），本行沿用同一条走法，新增问题码 `DraftIssue::BadProxyJump` 与词条 `connections.wizard.issue_proxy_jump`。按 AGENTS.md §4.2 第 12 条以代码运行时为准回填，不为此再造一个控件形态。
- **词条是三枚而不是一枚**：`connections.field.proxy_jump`（行标签）、`connections.proxy_jump.placeholder`（占位串形）、`connections.wizard.issue_proxy_jump`（留痕文案）。占位符也进表是因为本仓所有 placeholder 都经 `settings_label()` 取（覆盖闸 `tools/check/check_display_literals.py` 的口径：表外中文在英文档里不可见）。
- 编辑回填用 `render_proxy_jump_list` 的**规范化串形**（总是写端口）：留空与 `:22` 折成同一个值，串形也收敛成同一个串，「编辑一次把 `:22` 改没」因此不可能。
- 稿面另外三条照旧未动：侧栏档案行不显示跳链、quick connect 不接 `-J`、不新增设置键（跳链是档案属性而非全局档）。

### D10 平台接缝

新增 `src/platform/hop_pipe.{h,cpp}`：`make_hop_pair() -> std::optional<HopPair>`（两个可对等读写的句柄）。

- POSIX 腿：`socketpair(AF_UNIX, SOCK_STREAM, 0, ...)`（探针 P1 实测形态）。
- Win32 腿：回环 TCP 一对（监听 `127.0.0.1:0` → `connect` → `accept`，端口由内核择定），避免依赖 Winsock 的 AF_UNIX 支持面。本棒**只实现并验证 POSIX 腿**，Win32 腿按同形态写出但**未编译验证**——与 `SPEC.NF.RELI.01` 的 Win32 腿同一条在册边界（AGENTS.md §4.5 第 23 条：平台假设只许待在 `src/platform/` 内）。

## 4 接口形态（签名草案）

```cpp
// include/borealis/conn/profile.h（追加，不改既有成员）
struct SshHop { ... };                                  // D2
struct SshProfile { /* 既有八项 */
    std::vector<SshHop> proxy_jumps{};                  // 末位追加
};

// src/conn/proxy_jump.h（纯逻辑，无 libssh、无平台）
inline constexpr std::size_t kMaxProxyHops = 8;
[[nodiscard]] auto parse_proxy_jump_list(std::string_view)
    -> std::optional<std::vector<SshHop>>;
[[nodiscard]] auto render_proxy_jump_list(const std::vector<SshHop> &) -> std::string;
[[nodiscard]] auto validate_proxy_hops(const std::vector<SshHop> &) -> bool;

// src/conn/ssh_dial.h（两处追加）
enum class DialOutcome : std::uint8_t { ..., Hop, HopUnwired };
struct DialFailure { DialOutcome outcome; std::size_t hop_index; };
auto ssh_dial_and_authenticate(const SshProfile &, std::optional<std::string> secret,
                               ssh_session_struct *, const SshAlgorithmOverrides & = {},
                               std::uintptr_t seed_fd = 0) -> DialOutcome;

// src/conn/ssh_hop_chain.h（私有头；构造/析构只在实现文件里定义）
class SshHopChain final {
    auto open(const SshProfile &target,
              const std::vector<std::optional<std::string>> &hop_secrets,
              const SshAlgorithmOverrides &algorithms) -> bool;
    [[nodiscard]] auto failure() const noexcept -> DialFailure;
    [[nodiscard]] auto seed_fd() const noexcept -> std::uintptr_t;
    auto close() -> void;
};

// src/platform/hop_pipe.h（私有头，POSIX 腿已实测，Win32 腿未编译）
class HopPipeEnd final { read(span, timeout_ms) / write(span) / handle() / close(); };
[[nodiscard]] auto make_hop_pair() -> std::optional<HopPair>;  // {session_end, relay_end}

// src/conn/ssh_shared.h（追加）
struct SshSessionKey { ...; std::string proxy_jumps; };
auto same_secret(const SshSecretBundle &, const SshSecretBundle &) -> bool;
```

`SshConnection` 与 `session::Connection`／`ConnectionEvents`／`ReconnectControl` 三个公共面**一行不改**：跳板是传输腿内部的事，会话层与 UI 层不需要知道有几跳（同 7.147 的口径）。批 4 实测：`session::Connection` 等三个公共面确实未动；`src/conn/ssh_connection.h` 与 `src/conn/ssh_shared.h` 都是私有头，前者构造形参由 `std::optional<std::string>` 换成 `SshSecretBundle`（见 D8 的落地改口）。

## 5 线程、关闭序与新增实测点

**建立序**（全部发生在共享会话的 I/O 线程上）：hop0 真 TCP 拨号 → 在 hop0 上 `ssh_channel_open_forward(下一跳)` → 造一对 pipe、起第 1 根中继线程 → hop1 以 `seed_fd` 拨号 → …… → 目标会话以最后一个 `seed_fd` 拨号。

**关闭序**（F12 是硬约束）：目标 `ssh_disconnect` + `ssh_free` → **逆序**对每一跳：置停止位 → join 该跳的中继线程 → `ssh_channel_close`/`free` 转发通道 → `ssh_disconnect`/`ssh_free` 该跳会话 → 关闭 pipe 两端 → 清该跳的秘密副本。任何一步不得越过「先 join 后释放」。

**两条落地补注**（批 3 实测）：

- **seed 句柄的所有权不随递出而转移**（F17）：`ssh_disconnect()` 不关 `SSH_OPTIONS_FD` 吃进去的 fd，所以 `close()` 里「关 pipe 两端」这一步是必需的而不是冗余的；相应地，**吃 seed 的会话必须先断开**，`close()` 才关句柄——集成件里的 `ChainDial` 夹具靠成员声明次序（目标会话在链路之后）把这条序变成析构序，属结构性保证而非人工纪律。
- **转发通道不吃 `end_shell()`**：假服务器的 `end_shell()` 是对「所有 session 通道」发 EOF 的收场旋钮，跳板链路的转发通道也是它遍历到的通道；若一并收掉，用例里正在使用的链路会被无故终止。落地口径是 `end_channels()` 排除 `agent` 与 `forward` 两类通道——`end_shell()` 只收终端腿，转发腿的终结另有形态（`drop_connections()`）。

**线程数账**：一条带 N 跳的链路＝1 根 I/O 线程 + N 根中继线程（中继只搬字节、不跑协议状态机之外的活）。0 跳链路保持现状（1 根），本条不改变已落用例的线程形态。

**背压**：中继（D4 落地形态＝手写双向泵）每跳一份 16 KiB 工作缓冲，通道侧的堆积上限仍是 libssh 的通道窗口；本期不设每跳字节上限，在册风险见 §6。

**新增实测点（证人要打的桩）**：

| 编号 | 判据 |
|:---|:---|
| E1 | 带一跳的档案能连通并回显；跳板服务器看到 1 次 `direct-tcpip`，其转发目的＝目标端口。**批 3 已落**：`itest_posix_ssh_proxy_jump.one_hop_chain_connects_and_forwards_to_the_target_port`。 |
| E2 | 带两跳的档案连通；A 服务器看到转发目的＝B 端口、B 服务器看到转发目的＝目标端口（次序与 F10 同型，自证跳序语义没写反）。**批 3 已落**：`two_hop_chain_forwards_in_profile_order`（逐项比对 `forwarded_destinations()`；变异「跳序写反」打红两案）。 |
| E3 | 跳板认证失败归因：第 2 跳口令错 ⇒ 目标服务器**零**连接、失败档位 `Auth` 且 hop_index=1。**批 3 已落**：`second_hop_auth_failure_is_attributed_to_that_hop`（变异「hop_index 抹平成 0」打红）。 |
| E4 | 跳板主机密钥策略：`Yes` + 陌生跳板 ⇒ `HostKey` 档、hop_index 指到该跳、不发起目标连接。**批 3 已落**：`jump_host_key_policy_refuses_unknown_key_before_target`（首跳与次跳两式归因）。**后半句改口径**：「`AcceptNew` ⇒ 首跳写入 known_hosts（临时 HOME）」**不入集成件**——F16 实测 libssh 的主目录取 `getpwuid(2)` 的 `pw_dir`，临时 `$HOME` 拦不住，写盘会污染真机的 `~/.ssh/known_hosts`；策略判定本身由 `utest_ssh` 的 `policy_accepts` 无头见证，真机写盘腿归 CONN.08 的 etest 欠账。 |
| E4b | 拨号侧两条补充边界（批 3 顺带落）：`profile.proxy_jumps` 非空而 seed 未接 ⇒ `HopUnwired` 且**不发起连接**（D6 的 dial 级证人 `profile_with_jumps_without_seed_refuses_to_connect_directly`）；0 跳链路是合法空操作且 `close()` 幂等（`chain_without_hops_is_a_valid_no_op`）。 |
| E5 | 共享与分线：同目标同跳链同秘密的两格共线（跳板服务器 `accepted_connections` 仍为 1）；同目标但跳链为空/非空、或跳板秘密不同 ⇒ 两条独立链路。**批 4 已落**：`itest_posix_ssh_proxy_jump.chain_shape_and_hop_secret_split_the_link` 与 `two_tabs_over_the_same_chain_share_one_jump_connection`。变异「`key.proxy_jumps` 不入键」→ 单测与集成件各打红一案；变异「`same_secret` 去掉 hops」→ 各打红一案。 |
| E6 | fail-closed：SFTP／隧道／密钥推送任一条腿吃带跳档案 ⇒ `HopUnwired`，目标服务器零连接、跳板服务器零连接。**批 4 已落**：`sftp_tunnel_and_key_push_legs_refuse_a_profile_with_jumps`（三腿各按自己的失败档现形，见 D6 的落地形态）；变异「D6 闸取反」与 E4b 那一案同时打红。**批 6 订正（裁决 7.153）**：该案已删旧翻正——三腿各按正向档位案作证（假服务器无 subsystem／exec 处理，证人止于 `Protocol`／`ExecRefused`／`Active` 档位）；本档在 dial 层的证人改由 `profile_with_jumps_without_seed_refuses_to_connect_directly` 保留。 |
| E7 | 关一格标签不叫停其它标签的跳板链路（对 F15 那条原生缺陷的反向证成：我们自己的停止位只作用于本链）。**批 4 已落**：`closing_one_tab_does_not_stop_the_chain_of_the_others`——关掉第一格后第二格的 shell 仍在回显、跳板服务器仍只有 1 枚连接。 |
| E8 | 配置审计：跳板元素里塞 `secret` 明文被 `find_credential_key` 抓住；正常落盘只含句柄。**批 2 已落**：`utest_config.an_unsafe_document_is_refused_whole_and_changes_nothing`（跳板元素里的 `passphrase` 撞禁列名单那一案）与 `an_ssh_proxy_chain_round_trips_and_a_legacy_file_carries_none`（往返只带句柄、旧档无该键＝空链路）。 |
| E9 | 快照往返：带跳档案的采集→落盘→重放**保住跳链**（八键之外的第九键）；无该键的旧快照折成空链路而非报错。**批 2 已落**：`utest_workspace_snapshot.a_proxy_chain_absent_is_an_empty_chain_and_a_bad_one_rejects_the_whole_document`（缺键＝空链、坏元素整份拒绝，含 `kProxyJumpsSlot` 那一组坏值表）；带链采集→重放的等值断言在 `the_profile_blocks_are_the_same_json_the_config_store_writes` 与四型往返案里。 |
| E10 | 向导那一行（D9）：SSH 字段集画它、坏链那一下保存**只出留痕行且不交出档案**、好链折进 `SshProfile::proxy_jumps`、留空＝直连、编辑回填规范化串形且保存幂等。**批 5 已落**：`itest_connection_wizard.the_ssh_jump_line_refuses_a_bad_chain_and_saves_a_good_one`（四档）＋同套件判据①把 `connections.field.proxy_jump` 纳入「串口屏不出现的 SSH 栏」；纯逻辑侧 `utest_profile_views.proxy_jump_line_refuses_a_bad_chain_and_folds_a_good_one`（含顺序档与 9 跳上限）与 `editing_a_profile_with_jumps_backfills_the_canonical_line`。 |

## 6 风险与未验证（如实登记）

- **Win32 全链未编译**：`hop_pipe` 的 Win32 腿、`SSH_OPTIONS_FD` 在 Winsock 句柄上的行为、以及 F7 那条守卫结论都只有读码依据；MSVC 档落地前本条不称「可用」。
- **对端是 libssh 假服务器而非真 sshd**：真 sshd 的 `AllowTcpForwarding`／`permit-open` 与 `direct-tcpip` 权限模型不在射程内（真机 sshd 的 etest 是 CONN.08 的既有欠账，同批补）。
- **中继的缓冲形态**：手写双向泵（D4 落地改口）每跳一份 16 KiB 工作缓冲，通道侧的堆积上限仍是 libssh 的通道窗口；目标卡死时跳板通道的窗口会涨满并反压到 seed 侧（`ssh_channel_write` 阻塞）。本期不设每跳字节上限，依赖库的窗口机制兜底。
- **Win32 的 seed 句柄截断是真实风险**（F17）：`SSH_OPTIONS_FD` 把传入句柄 `& 0xffff`，POSIX 的小整数 fd 侥幸无损，Winsock 的 64 位 `SOCKET` 核句不保证——`hop_pipe` 的 Win32 腿（回环 TCP 一对）拿到的正是这种核句。本仓无法在库外封堵，只能在 MSVC 档实测时把它列为第一条要打的桩（`SSH_OPTIONS_FD` 之后 `ssh_connect` 是否报 `Network`）。
- **known_hosts 的写盘位置不可被 `$HOME` 重定向**（F16）：测试夹具因此不能拿「临时 HOME」当隔离手段，`AcceptNew` 那一条腿只留在无头策略判定层（见 E4 的改口径），真机写盘与首连提示归 CONN.08 的 sshd etest。
- **收口期有一条正常的写失败日志**：目标会话先断、该跳中继线程尚未退出时，方向二仍可能往已关的 seed 端写一次，实测读到的是一条 `[WRN] hop pipe write failed: Broken pipe`。这是 F12 关闭序允许竞态（不是越序），保留 WARN 而不静默——真机走查若见这一条，属预期。
- **`%h`/`%p` 宏与 `ProxyCommand` 不导入**：导入是单向只读，宏未展开即语法坏，按上一条口径整块不导入（本件无日志面，代价是坏链那条档案在导入结果里少一条，用户看得见条数、看不见原因）。
- **同一块里多行 `ProxyJump` 的取舍次序未实测**：OpenSSH 是「后一行覆盖前一行」还是「逐行累加成一条链」没有本机依据；本件沿用自家解析器的既有口径**后值覆盖**（`src/conn/profile.cpp` 的逐键赋值），真机 sshd/OpenSSH 行为留待 CONN.08 那批真 sshd 端到端一并核。
- **窗口期已从「静默直连」收到「只剩三条腿没接线」**：批 4 之后终端腿已把 `SshHopChain` 接进装配（`src/conn/ssh_shared.cpp` 的 `dial_then_open_all()` 在 I/O 线程上开链，见 D5 的落地形态），产品六条 SSH 腿都不会再拿带跳的档案静默直连。**剩下的缺口只有 SFTP／隧道／密钥推送三条腿**（§7 批 6），它们的现形行为是「拒拨 + 一条 ERROR／各自的失败档」而不是「连错链路」，并有 `itest_posix_ssh_proxy_jump` 的 E6 一案逐腿盯着。**批 5 之后向导那一行也上了**（D9／E10）：用户现在能从 UI 造出链路，而这三腿吃带跳档案时仍按各自的失败档现形。真机走查前，那一行的观感（占位串形是否够用、坏链文案是否指得回那一行）仍未证。
- **装配闭包没有自动化证人**（批 4 登记）：`src/main.cpp` 的 `with_hop_secrets`、恢复泵的第二个挂起点、以及「留档 bundle 按跳数对齐」都在 UI 装配闭包里，本仓无头测试打不到 `main.cpp`。为此做的补偿是两处：`profile_views` 的折叠与计划在 `utest_profile_views` 有无头证人；「hops 未按跳数对齐 ⇒ 不能共线」这一条补进了 `chain_shape_and_hop_secret_split_the_link`（`SshSecretBundle{target}` 的 `hops == {}` 与 `hops == [nullopt, nullopt]` 判为不同键）。真机走查前，装配层的逐跳取料顺序与恢复泵的续跑仍属**未验证**。
- **`with_hop_secrets` 的迟到结果没有 `expired()` 闸**：既有房屋里凭据询问的 continuation 持 `std::weak_ptr<CredentialPrompt>` 并以 `prompt.expired()` 挡迟到结果；跳板取料的 continuation 只捕获 store 副本与句柄表，收尾闸门用的是「恢复泵自己的 `restore_done`/`restore_aborted`」。**在册依据**：Aurora `state/async.h` 注明主线程投递器缺失时 `then` 会在 worker 线程同步执行，本仓 `src/main.cpp` 的 `fetch_ports` 注释同述——取料闭包不持 UI 对象正是为了在这两种执行序下都安全。窗口销毁后 `ssh_secret_by_tab` 那一层的留档语义未证，随真机走查一并核。
- **重启那一腿沿用留档 hops**：`restart_ssh_tab()` 只重解析目标口令，逐跳材料从留档 bundle 原样带过来（不重新问凭据库）。代价是跳板口令在运行期被外部改掉时，重启仍用旧副本；好处是不新增第二次异步。批 5 之后若询问 UI 扩到逐跳，这一条要一起改口。
- **跳板上的 `keyboard-interactive` 多提示**：与目标腿同一条既有边界（单提示一次作答），不因本条改变。

## 7 分批

| 批 | 内容 | 交付判据 |
|:---|:---|:---|
| 批 1 | D3 纯逻辑件 + **D2 的 `SshHop` 类型本体**（解析件的返回类型即它，只能与它同批落公共头）+ `utest_proxy_jump`（语法、端口折叠、`none`、整串拒绝、上限、render/parse 往返） | 无头单测全绿，不碰 libssh |
| 批 2 | D2 的档案末位追加 `proxy_jumps` + `config/store.cpp` 序列化与缺档兼容 + 快照那一键（D7 与 E9）+ D7 导入 `proxyjump` + E8 审计扩案 | 档案往返、快照往返与导入用例 |
| 批 3 | D10 `hop_pipe` + D4 `SshHopChain` + `ssh_dial` 的 seed 参数与两档新增 + 假服务器 `direct-tcpip` 扩口（F13） | **已交付**：E1/E2/E3/E4（含改口径的 E4b）共 6 案 `itest_posix_ssh_proxy_jump` 全绿、5 轮无 flaky；四条变异自证各自打红（seed 腿失效→2 案、D6 闸取反→1 案、跳序写反→2 案、hop_index 抹平→2 案）；全量 ctest 121/121、无头 runner 1463/1463 |
| 批 4 | D5 共享键与秘密判据扩容 + D8 bundle 与装配 + D6 三条腿的 fail-closed 现形 | **已交付**：E5/E6/E7 共 4 案新落，`itest_posix_ssh_proxy_jump` 10 案全绿；`utest_ssh_session_key` 6 案、`utest_profile_views` 13 案全绿；全量 ctest 121/121。三条变异自证各自打红（`key.proxy_jumps` 不入键→单测 1 案 + 集成 1 案、`same_secret` 去 hops→单测 1 案 + 分线一案、D6 闸取反→E4b 与 E6 两案）；第四条变异（去掉 bundle 的 `hops.resize`）**无自动化证人**，缺口已登记 §6 |
| 批 5 | D9 向导那一行 + 词条双档 + 覆盖闸与体积闸；变异自证；文档回写与分层提交 | **已交付**：E10 落（`itest_connection_wizard` 11 案、`utest_profile_views` 15 案全绿，全量 ctest 121/121、无头 runner 1474/1474（本批新增 3 案）；覆盖闸 `tools/check/check_display_literals.py` PASS（表外 240 文件无引号内中文），两档 key 集等式与体积闸（`utest_i18n_tiers` 9 案）自动吃下新增的三枚键。六条变异自证各自打红：去掉 `validate_draft` 的 `BadProxyJump` 闸→单测 1 案 + 集成 1 案（且那一下保存会交出**空链路档案**，即稿 D9 要防的静默直连）、去掉 `profile_to_draft` 的回填→单测 1 案 + 集成 1 案、去掉 `collect_draft` 的收值→集成 1 案、那一行不挂进字段集→集成 1 案、`draft_to_profile` 不折链→单测 2 案 + 集成 1 案、问题码折错文案档→集成 1 案 |
| 批 6（另起裁决 **7.153**，子稿 `CONN03_PROXYJUMP_LEGS.draft.md`） | 跳板接进 SFTP／隧道／密钥推送三条腿 | **已交付（2026-10-25）**：三腿各自无条件建链后交 seed 拨目标（0 跳＝合法空操作），秘密参数换型 `SshSecretBundle`，`itest_posix_ssh_proxy_jump` 10→13 案；D6 的拒拨闸退为 dial 层兜底档，`ssh_dial.h` 的 TODO 撤账。带跳三条真机走查未做 |

## 8 与本仓既有裁决的接缝

- 7.147（连接复用）：D5 是它的追加判据，不改它的关闭序与租约语义。
- 7.99（自动重连与关闭归因）：`DialOutcome` 多两档，`WS.05` 的「除 Network 外不重拨」口径不变——`Hop` 档里的 `Network` 子档同样值得重拨，故重拨判据读 `DialFailure.outcome` 而不是新造规则。
- 7.93／7.116（凭据与档案落盘）：跳板只多一组句柄字段，明文凭据审计的覆盖面按 E8 扩。
- D1①（私有头纪律）：`ssh_hop_chain.h` 与 `hop_pipe.h` 都是私有头，libssh／平台类型不进 `include/`。

## 9 不做与延后（逐项去向）

| 项 | 去向 |
|:---|:---|
| `ProxyCommand`（外部命令） | 不进本条需求原文；如未来要，走 `SPEC.FEAT.CONN.02` 的新延后项，另起标识 |
| 逐跳凭据的询问 UI（`AskEveryTime` 的跳） | `SPECIFICATIONS.md` §4.5 的 CONN.03 延后句追加一项，随 CONN.09 的询问件一起排 |
| 跳板接进 SFTP／隧道／密钥推送 | 本稿 §7 批 6 |
| quick connect 的 `-J` 语法 | 随 `SPEC.FEAT.CONN.03` 的 quick connect 后续扩展 |
| 真机 sshd 的 `direct-tcpip` 许可面 | 随 CONN.08 的 etest 欠账 |
| 每跳字节背压上界 | 观察池（与 `SPEC.NF.PERF.06` 的输出口径一并评估） |
