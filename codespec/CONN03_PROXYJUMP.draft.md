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
| F13 | 进程内假服务器（`SPEC.FEAT.CONN.02` 落下的见证地基）有 session 通道、PTY/shell、agent 反向通道与 `drop_connections`/`end_shell`，但**没有 `direct-tcpip` 回调**，因此现在还当不了跳板；证人需要先扩这一口径。 | `tests/support/fake_ssh_server.{h,cpp}` 的 `FakeSshServerOptions` 与 `serve` 的 server 回调装配段 |
| F14 | `SSH_OPTIONS_FD` 那条分支**不在平台守卫内**（`ssh_connect()` 里 `opts.fd != SSH_INVALID_SOCKET` 独立成一支）⇒ 自建路线的客户端侧是平台中立的；平台相关部分只剩「造一对可对等读写的 socket」这一件事（`socketpair` 是 POSIX 专有）。 | 库侧 `src/client.c` 的 `ssh_connect` 分支次序；POSIX 语义按 `socketpair(2)` |
| F15 | 原生实现还有一条对本仓致命的形态：跳板中继循环检查一个**文件级全局** `int proxy_disconnect`，而 `ssh_disconnect()` 在根会话上会把它置 1、下一次根拨号又把它清 0。⇒ 任一标签断开会叫停**其它标签**仍在跑的跳板中继，多标签正是本仓的核心形态，行为不可预期且无法从外部封堵。 | 库侧 `src/socket.c` 的 `int proxy_disconnect = 0` 与循环内检查、`src/client.c` 的 `if (session->proxy_root) proxy_disconnect = 1;` |

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

代价（如实登记）：我们自己拥有中继线程与其关闭次序（F12），以及一枚新的平台接缝（D10）；`socketpair`＋connector 的中继每跳多一根线程与两次拷贝。

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
    /// 逐跳建立链路；成功后 fd() 交给目标会话。
    auto open(const std::vector<SshHop> &hops,
              const std::vector<std::optional<std::string>> &hop_secrets,
              const SshAlgorithmOverrides &algorithms) -> bool;
    [[nodiscard]] auto failure() const noexcept -> DialFailure;  ///< 档位 + 第几跳。
    [[nodiscard]] auto seed_fd() const noexcept -> std::uintptr_t;
    auto close() -> void;   ///< 逆序收口：join 中继 → 关通道 → 断会话 → 清明文
};
```

- 逐跳的「连接→核对→认证」全部交给 `ssh_dial_and_authenticate()`：跳板 hop 折成一份临时 `SshProfile`（`agent_forwarding` 恒 false，跳板不需要转发 agent——本期不为跳板开这条腿），第一跳不交 seed，其后各跳交上一跳的 fd。
- `ssh_dial.h` 的签名只追加一枚末位缺省参数：`std::uintptr_t seed_fd = 0`（0＝无 seed，走真 TCP）。选 `uintptr_t` 而不是 `socket_t`／`ssh_session_struct*`，是为了让公共面不吃 libssh 与平台类型；`ssh_dial.cpp` 内部再折成 `socket_t` 写进 `SSH_OPTIONS_FD`。
- 失败归因：`DialOutcome` 增两档——`HopUnwired`（消费腿没接线，见 D6）与 `Hop`（跳板侧失败）；`Hop` 具体卡在跳板的哪一档由 `DialFailure{outcome, hop_index}` 携带，hop_index 等于 `hops.size()` 表示目标本身。WS.05 的关闭归因与日志只多一个「第几跳」的数，公共语义不变。
- 中继：每跳一根线程，形态同探针（`ssh_event` + 两枚 connector + `ssh_connector_set_in_fd/out_channel`），线程循环每 10 ms 一拍并检查停止位；`close()` 先置停止位再 join（F12）。

### D5 与连接复用（CONN.02）的关系：跳链属于「链路」，不随标签

- `SshSessionKey` 追加一枚 `std::string proxy_jumps`，取值＝`render_proxy_jump_list(profile.proxy_jumps)`（归一口径与端口折叠都在 D3 里做完，于是「留空端口」与「:22」不会各拨一条链）。
- `same_secret` 的判据从「一份目标秘密」扩为「目标秘密 + 逐跳秘密」逐项全等；`SshSharedSession::matches()` 同步。跳板秘密仍**不进表**（`SshSessionRegistry` 的表项只有键与弱引用，比较时问活着的会话）。
- 跳链与中继线程由**共享会话的 I/O 线程**拥有：拨号时在 I/O 线程上 `open`，链路终态或末位租约收口时 `close`。重拨＝整链重开（跳板也重连）——这与 7.147 已落的「一条退避环在链路级」一致，不新造「跳板级」的环。

### D6 未接线腿的 fail-closed 口径

`sftp_client`／`tunnel_client`／`key_push` 本批不改一行代码，但 `ssh_dial_and_authenticate()` 一旦看到 `profile.proxy_jumps` 非空而 `seed_fd` 未交、链也未建（这三条腿根本不会建链），就记一条 ERROR 并回 `HopUnwired`，**不发起连接**。

理由：静默直连等于绕过用户显式配置的跳板——在 Windows 上这正是原生路线的既有行为（F7），本仓不接受。接线留后续批（§9 批 6），届时三条腿各自持有一份 `SshHopChain`。

### D7 序列化与导入

- `src/config/store.cpp` 的 ssh 段新增 `proxy_jumps` 数组（逐元素写 D2 的七个字段，`credential` 仍只写句柄）。**旧档无该键＝空链路**，读侧缺省即空，不产生迁移。
- 明文凭据审计（`kBannedKeyNames` + `find_credential_key`）必须覆盖新数组的每一元素——新增一条审计用例专门把跳板里的 `secret` 摆进配置树里抓红。
- `parse_ssh_config` 加 `proxyjump` 一臂：逗号拆串后逐 token 走 D3 的语法（`%h` 之类的 OpenSSH 宏本期不展开，遇到含 `%` 的 token 整键丢弃并记一条 WARN——导入是单向只读，宁可少导入也不导错目的地）；hop 的 `auth_method` 取 `agent`、`known_hosts_policy` 跟随同块的 `StrictHostKeyChecking` 折算结果；`ProxyCommand` 继续不导入（在册）。
- **快照与撤销两处都要跟着走（实测点）**：`SPEC.FEAT.WS.11` 的会话快照把 SSH 身份**逐字段抄进 JSON**（`src/ui/workspace_snapshot.cpp` 的 `host`/`port`/`user`/`auth_method`/`identity_file`/`agent_forwarding`/`known_hosts_policy`/`credential` 八个键），所以 `proxy_jumps` 必须同批加进那对编解码——否则「重启恢复」会拿着丢了跳板的快照**直连**，正是 D6 要防的那一形态；缺该键＝空链路（旧快照兼容）。撤销栈的 `ClosedTabSpec` 存的是 `std::optional<conn::SshProfile>` 内存态快照（`include/borealis/ui/closed_tab_stack.h`），随模型自动携带跳链，不需要新增序列化面。

### D8 装配与凭据解析

- 目标与逐跳的秘密在**开标签时一次性解析**，折成 `SshSecretBundle{ std::optional<std::string> target; std::vector<std::optional<std::string>> hops; }`；`src/main.cpp` 的 `ssh_secret_by_tab` 的 value 由 `std::optional<std::string>` 换成该 bundle（「留档副本先取、最后一份 move 走」的既有语义原样保留，重拨／切分／恢复都用同一 bundle）。
- 询问 UI 本期仍只覆盖目标口令。某跳是 `AskEveryTime` 且没有材料 ⇒ 该跳按 `Auth` 失败并记明第几跳（不新造询问链，见 §9）。
- 六条 SSH 腿（新开／分屏／重启／撤销／恢复／CLI 转交）都经 `make_ssh_session` 一处装配，bundle 只需在这一处进 `SshConnection` 的形参（末位缺省参数追加，编译面不变）。

### D9 向导与设置

- SSH 字段集追加一行「跳板机（ProxyJump）」单行输入，占位串形 `jump1.example.com,jump2:2222`；词条 `connections.field.proxy_jump` 中英双档进表（`SPEC.FEAT.PREF.05` 的覆盖闸会盯表外中文）。
- `collect_draft()` 里经 D3 解析；解析失败就在该行给红字并禁止保存——沿用向导既有的端口校验形态，不新增控件类型。
- 侧栏档案行不显示跳链（一行放不下，且「连到哪里」的判据仍以目标主机为准）；quick connect 不接 `-J` 语法。
- **不新增设置键**：跳链是档案属性，不是全局档（与 `kex_algorithms` 那类全局档的区别：后者是「算法面」，前者是「目的地拓扑」）。
- 延后角标：`codespec/UI_OVERVIEW.draft.md` §7 与 `UI_CONNECTIONS.draft.md` 把 ProxyJump 标在「延后能力一律灰置不画成可用态」的清单里，落地后同批改口。

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

// src/conn/ssh_hop_chain.h（私有头）
class SshHopChain final { open(...) / seed_fd() / failure() / close(); };

// src/conn/ssh_shared.h（追加）
struct SshSessionKey { ...; std::string proxy_jumps; };
auto same_secret(const SshSecretBundle &, const SshSecretBundle &) -> bool;
```

`SshConnection` 与 `session::Connection`／`ConnectionEvents`／`ReconnectControl` 三个公共面**一行不改**：跳板是传输腿内部的事，会话层与 UI 层不需要知道有几跳（同 7.147 的口径）。

## 5 线程、关闭序与新增实测点

**建立序**（全部发生在共享会话的 I/O 线程上）：hop0 真 TCP 拨号 → 在 hop0 上 `ssh_channel_open_forward(下一跳)` → 造一对 pipe、起第 1 根中继线程 → hop1 以 `seed_fd` 拨号 → …… → 目标会话以最后一个 `seed_fd` 拨号。

**关闭序**（F12 是硬约束）：目标 `ssh_disconnect` + `ssh_free` → **逆序**对每一跳：置停止位 → join 该跳的中继线程 → `ssh_channel_close`/`free` 转发通道 → `ssh_disconnect`/`ssh_free` 该跳会话 → 关闭 pipe 两端 → 清该跳的秘密副本。任何一步不得越过「先 join 后释放」。

**线程数账**：一条带 N 跳的链路＝1 根 I/O 线程 + N 根中继线程（中继只搬字节、不跑协议状态机之外的活）。0 跳链路保持现状（1 根），本条不改变已落用例的线程形态。

**背压**：中继缓冲在 libssh 的 connector/通道窗口内，本期不设每跳字节上限；在册风险见 §6。

**新增实测点（证人要打的桩）**：

| 编号 | 判据 |
|:---|:---|
| E1 | 带一跳的档案能连通并回显；跳板服务器看到 1 次 `direct-tcpip`，其转发目的＝目标端口。 |
| E2 | 带两跳的档案连通；A 服务器看到转发目的＝B 端口、B 服务器看到转发目的＝目标端口（次序与 F10 同型，自证跳序语义没写反）。 |
| E3 | 跳板认证失败归因：第 2 跳口令错 ⇒ 目标服务器**零**连接、失败档位 `Auth` 且 hop_index=1。 |
| E4 | 跳板主机密钥策略：`Yes` + 陌生跳板 ⇒ `HostKey` 档、hop_index 指到该跳、不发起目标连接；`AcceptNew` ⇒ 首跳成功写入 known_hosts（临时 HOME）。 |
| E5 | 共享与分线：同目标同跳链同秘密的两格共线（跳板服务器 `accepted_connections` 仍为 1）；同目标但跳链为空/非空、或跳板秘密不同 ⇒ 两条独立链路。 |
| E6 | fail-closed：SFTP／隧道／密钥推送任一条腿吃带跳档案 ⇒ `HopUnwired`，目标服务器零连接、跳板服务器零连接。 |
| E7 | 关一格标签不叫停其它标签的跳板链路（对 F15 那条原生缺陷的反向证成：我们自己的停止位只作用于本链）。 |
| E8 | 配置审计：跳板元素里塞 `secret` 明文被 `find_credential_key` 抓住；正常落盘只含句柄。 |
| E9 | 快照往返：带跳档案的采集→落盘→重放**保住跳链**（八键之外的第九键）；无该键的旧快照折成空链路而非报错。 |

## 6 风险与未验证（如实登记）

- **Win32 全链未编译**：`hop_pipe` 的 Win32 腿、`SSH_OPTIONS_FD` 在 Winsock 句柄上的行为、以及 F7 那条守卫结论都只有读码依据；MSVC 档落地前本条不称「可用」。
- **对端是 libssh 假服务器而非真 sshd**：真 sshd 的 `AllowTcpForwarding`／`permit-open` 与 `direct-tcpip` 权限模型不在射程内（真机 sshd 的 etest 是 CONN.08 的既有欠账，同批补）。
- **中继无字节上界**：目标卡死时 connector 的缓冲会随跳板通道窗口涨；本期依赖库的窗口机制兜底，不另加限流。
- **`%h`/`%p` 宏与 `ProxyCommand` 不导入**：导入是单向只读，遇到就整键丢弃并记 WARN。
- **跳板上的 `keyboard-interactive` 多提示**：与目标腿同一条既有边界（单提示一次作答），不因本条改变。

## 7 分批

| 批 | 内容 | 交付判据 |
|:---|:---|:---|
| 批 1 | D3 纯逻辑件 + `utest_proxy_jump`（语法、端口折叠、`none`、整串拒绝、上限、render/parse 往返） | 无头单测全绿，不碰 libssh |
| 批 2 | D2 模型追加 + `config/store.cpp` 序列化与缺档兼容 + 快照那一键（D7 与 E9）+ D7 导入 `proxyjump` + E8 审计扩案 | 档案往返、快照往返与导入用例 |
| 批 3 | D10 `hop_pipe` + D4 `SshHopChain` + `ssh_dial` 的 seed 参数与两档新增 + 假服务器 `direct-tcpip` 扩口（F13） | E1/E2/E3/E4 的 `itest_posix_` 用例 |
| 批 4 | D5 共享键与秘密判据扩容 + D8 bundle 与装配 + D6 三条腿的 fail-closed 现形 | E5/E6/E7 + 全量回归 |
| 批 5 | D9 向导那一行 + 词条双档 + 覆盖闸与体积闸；变异自证；文档回写与分层提交 | 判据逐条回写 `SPECIFICATIONS.md` §7 与 `CHANGELOG.md` |
| 批 6（延后，另起裁决） | 跳板接进 SFTP／隧道／密钥推送三条腿 | 不在本稿射程 |

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
