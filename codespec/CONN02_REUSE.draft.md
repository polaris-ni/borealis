# CONN.02 连接复用（终端标签共享一条 TCP）设计稿

> 状态：草稿（2026-10-24）。本稿只覆盖 `SPEC.FEAT.CONN.02` 的延后子项「同一 profile 的连接复用（多标签共享一条 TCP 连接）」；实现与文档回写分在后续批。所有事实均在本仓当前工作树上实测；未经实测的推断逐条标「推断／未验证」。

## 0 需求与边界

需求原句（`SPECIFICATIONS.md` §4.5 `SPEC.FEAT.CONN.02`）：

> 同一 profile 的**连接复用**（多标签共享一条 TCP 连接）为延后子项。

`PLAN.md` §4「延后子项」的目标阶段为 M4；本仓 M4 的连接类条目已陆续落地，本项按既定顺序开工。

**本条只做终端标签与分屏 pane 的传输复用**。SFTP 浏览器（`SPEC.FEAT.CONN.04`）、隧道（`SPEC.FEAT.CONN.08`）、密钥导出／推送各自的会话仍各自拨号——它们经 `conn/ssh_dial` 自开会话，不经过终端腿的 `SshConnection`，本轮一行不改。跨能力共享一条会话是另一个意图（还要把 SFTP 的请求/应答与终端的通道读循环排进同一事件循环），不在本条需求原文内。

不做：keepalive（另一条在册边界）、ProxyJump（`SPEC.FEAT.CONN.03` 延后子项）、连接池上限与空闲回收策略、连接复用的设置开关（见 D2）。

## 1 地基实测

| 编号 | 事实 | 证据（相对路径 + 章节／符号） |
|:---|:---|:---|
| F1 | 每个 SSH 标签／pane 各构造一份 `SshConnection`，即各拨一条 TCP：新开标签、分屏新格、重启换会话、撤销重开、启动恢复、CLI 转交六条腿都走同一个 `make_ssh_session`。 | `src/main.cpp` 装配段的 `make_ssh_session` / `make_ssh_pane_factory` / `restart_ssh_tab` / `open_ssh_tab`；CLI 与会话恢复两处的调用点同段 |
| F2 | `SshConnection` 一对一持有「一条 `ssh_session` + 一条通道 + 一个读线程」；`write()` 与 `resize()` 由调用线程（UI 线程）直接调 `ssh_channel_write` / `ssh_channel_change_pty_size`，与读线程触碰同一个 libssh 会话。libssh 非线程安全，这是一条既有潜在竞态（头注释为 agent 回调表专门加了递归锁，正是同一约束的旁证）。 | `src/conn/ssh_connection.h` 的线程模型段与私有成员；`src/conn/ssh_connection.cpp` 的 `write` / `resize` |
| F3 | 关闭顺序：`close()` 先置 `closing_` 再 join 读线程，之后 `teardown()` 才释放反向通道 → 通道 → 会话 → 回调表；`close()` 返回后不再回调（`session::Connection` 的契约）。 | `src/conn/ssh_connection.cpp` 的 `close` / `teardown`；`include/borealis/session/connection.h` 的 `Connection::close` 注释 |
| F4 | 自动重连环在连接对象内部：`run_loop` 反复跑「拨号→通道→PTY→shell→读循环」，`ReconnectProgress` 是 latest-value 快照，`retry_now` / `stop_reconnect` 是能力接口 `session::ReconnectControl`。 | `src/conn/ssh_connection.cpp` 的 `run_loop` / `sleep_backoff` / `post_progress`；`src/conn/reconnect.h` |
| F5 | `session::Session` 在构造期一次 `dynamic_cast<ReconnectControl *>` 记住能力接口；重连浮层只读 `reconnect_progress()`，不需要认出连接类型。 | `include/borealis/session/session.h` 的 `reconnect_control_`；`src/session/session.cpp` 的构造段 |
| F6 | 会话层的入向入口 `Session::ingest` 用 `mutex_` 护住解码器、网格与状态机，可从任意后台线程调用；`Session::close` 先 `connection_->close()` 再停会话日志。 | `src/session/session.cpp` 的 `ingest` / `close` / `on_closed` |
| F7 | `ConnectionEvents` 的回调契约是「发生在连接的读线程上，实现方不得触达 UI 状态」；`on_bytes` / `on_closed(reason)` / `on_reconnect_progress` 三面已齐。 | `include/borealis/session/connection.h` 的 `ConnectionEvents` |
| F8 | 共享键需要的全部字段都在 `SshProfile` 与 `SshAlgorithmOverrides` 里：host／port／user／auth_method／identity_file／agent_forwarding／known_hosts_policy／credential，以及 KEX 与主机密钥两张算法表；`RetryPolicy` 来自全局设置，不入档案。 | `include/borealis/conn/profile.h` 的 `SshProfile`；`src/conn/ssh_dial.h` 的 `SshAlgorithmOverrides` |
| F9 | 进程内假服务器支持多条连接、每条连接多个 session 通道，计数面有 `accepted_connections` / `session_channels` / `pty_requests` / `shell_requests`，并有 `drop_connections` 与 `end_shell`。 | `tests/support/fake_ssh_server.{h,cpp}` |
| F10 | 假服务器当前把「任一非 agent 通道关闭」当作整条连接收摊（`peer_closed`），且只记全局最近一份 PTY 尺寸。共享连接要证「关一个通道不关整条」与「逐通道尺寸」，这两处必须先扩。 | `tests/support/fake_ssh_server.cpp` 的 `on_channel_close` / `note_pty` / `last_pty_columns` |
| F11 | 拨号腿的 TCP 超时是 10 s（`SSH_OPTIONS_TIMEOUT`），`ssh_connect` 阻塞；`ssh_channel_open_session` 也是阻塞调用（超时档随会话选项，未单独实测）。 | `src/conn/ssh_dial.cpp` 的 `kConnectTimeoutSeconds` |
| F12 | `alive()` 的现行语义＝`started_ && !closing_ && channel_open_`，即「通道此刻可用」；重连退避期间为 false。`SPEC.FEAT.WS.01` 的关闭前确认依赖它。 | `src/conn/ssh_connection.cpp` 的 `alive`；`include/borealis/session/connection.h` 的 `Connection::alive` |

## 2 目标与非目标

**目标**

1. 同一连接参数的多个终端标签／pane 只建立一条 TCP；服务器侧看到一条连接上的多条 session 通道。
2. 关掉其中一个标签不影响其余标签的链路与通道。
3. 链路丢失时只重拨一条 TCP；重拨成功后给每个仍挂接的标签各开一条新 shell 通道，各自的 PTY 取该标签的最新尺寸。
4. 不改变 `session::Connection` / `ConnectionEvents` / `ReconnectControl` 三个公共面；会话层与 UI 层零改动。
5. 顺带收敛 F2 的既有竞态：所有 libssh 调用只发生在一条 I/O 线程上。

**非目标**

- 跨能力的会话共享（SFTP／隧道／密钥推送）。
- 跨进程复用、连接池上限、空闲与会话回收、keepalive。
- 服务器 `MaxSessions` 超限时的排队（见 §11）。

## 3 决策

### D1 共享粒度：一条 TCP 一个共享会话，标签是通道租约

新增私有件 `src/conn/ssh_shared.{h,cpp}`：

- `SshSharedSession`：一条 libssh 会话、一条 I/O 线程、N 条 shell 通道、N 份租约（每个 `SshConnection` 一份）。
- `SshConnection` 从「持有会话的连接」改为「薄适配器」：仍实现 `session::Connection` 与 `session::ReconnectControl`，但把 `start / write / resize / close / retry_now / stop_reconnect` 转给共享会话；`alive()` 读自己那份原子位。
- 构造签名只追加一个末位缺省参数 `std::shared_ptr<SshSessionRegistry> registry = nullptr`；`nullptr` 时适配器自建一份不登记的共享会话（单标签路径与现有测试的编译面、行为都不变）。

### D2 共享键：生效连接参数全等 + 秘密材料全等

`SshSessionKey` 由以下字段构成：

- `effective_port(profile)`（0 折 22）、`normalized_auth_method(profile)`（空折 `agent`）、host、user、identity_file、agent_forwarding、known_hosts_policy；
- `SshAlgorithmOverrides` 的两张表；
- **秘密材料全等**（`optional<string>` 逐字节相等；两边都无材料也算相等）。

档案 id **不入键**：键表达的是「连到哪里、以谁的身份、用什么协商面」，不是「哪条档案记录」。理由有三：① 档案可被编辑（改 host 后旧标签的快照与新档案必须分键，否则会把旧连接借给新目的地）；② quick connect 的临时档案没有稳定 id；③ 两条内容相同的档案共享一条 TCP 没有安全或语义上的坏处。凭据句柄（`SecretHandle`）同样不入键——判据是**材料**是否相同，不是句柄是否相同。

不同秘密材料永不共享，这是安全边界而不是性能取舍：把两个口令不同的标签接到同一条已认证的连接上，等于让第二个标签绕过它自己的认证材料。

### D3 线程模型：单 I/O 线程 + 有界命令队列

所有 libssh 调用（建会话、拨号、认证、开通道、读写、resize、agent 中继、释放）只发生在共享会话的 I/O 线程上。UI 线程只做两件事：把命令压进队列、读原子位。

- 命令种类：`Attach`（租约 + 事件 + 初始尺寸）、`Detach`、`Write`、`Resize`；`RetryNow` / `StopReconnect` 是原子位，不入队列。
- 队列由一把 mutex 与一个 condition_variable 护住；I/O 线程在无字节可读时以 30 ms 为上限等条件变量（取「有新命令」与「读空转节拍」两者的较早者）。
- **写队列有界**：待发字节总量上限 1 MiB（`kMaxPendingWriteBytes`），超限丢弃新来的一笔并记一条 WARN（每会话一次）。理由：UI 回调路径禁止阻塞 IO（`AGENTS.md` §4.5 第 25 条），而无界队列在链路卡死时会把键入堆成内存。丢弃的语义与现行「`ssh_channel_write` 失败即丢」一致。
- `Write` 必须复制字节：调用方的 span 在 `write()` 返回后不保证存活。
- 单 I/O 线程顺带消掉 F2 的竞态，这是本条的**结构性收益**，不是附带重构：共享会话一旦多通道，`write`／`resize` 并发触碰 libssh 就从小概率变成必然。

### D4 读循环：逐通道读，通道级 EOF 与链路级故障分开

I/O 线程每一拍遍历所有已开通道，逐条 `ssh_channel_read_nonblocking`：

- `bytes > 0`：只投给该租约的 `ConnectionEvents::on_bytes`。
- `SSH_EOF`（先判）或 `0 && is_eof`：该标签的远端正常结束 → 只关这一条通道、擦掉这个租约、投 `on_closed(RemoteExit)`；**链路与其余标签继续**。
- `SSH_ERROR` 或通道已关（无 EOF）：归链路事故 → 收掉全部通道、整条环进退避重拨。通道级"半死"在 libssh 上无法与链路级可靠区分，而一条 TCP 上的协议失步会污染其余通道，故保守按整链处理（与现行单标签语义同向）。

EOF 必须先于「通道已关」判：这是裁决 7.146 抓到的 `SSH_EOF` 缺陷的同一处次序，不能在多通道版本里退回去。

### D5 重连：一条退避环，进度广播给全部挂接标签

- 退避环、分类表、次数与档位全部复用 `conn/reconnect.h` 与 `RetryPolicy`（缺省 3 回、1 s→2 s→4 s），不另写算式。
- 每一档退避投一份 `ReconnectProgress` 给**所有**挂接租约；终态那一份带 `ReconnectStop`；重拨成功回读循环时投全零快照把浮层擦干净（与现行三投递时机一致）。
- 新标签在退避期间挂接：只登记租约（不拨号），并立刻收到最近一份进度快照，浮层文案因此从一开始就正确；重拨成功后与老标签一起开通道。
- `retry_now` 是连接级：任一份租约触发即截断共享退避（下一次分片到点就拨）。
- `stop_reconnect` 也是**连接级**：一条 TCP 不能「一半继续重连、一半不重连」，故任一份租约触发即整条环落 `UserStopped`，终态进度与 `on_closed(UserStopped)` 投给全部挂接标签。代价是一个标签的「停止重连」会同时结束其他标签的重连尝试；这是共享链路的事实语义，登记在案（§11）。

### D6 生命周期：registry 持弱引用，适配器持强引用

- `SshSessionRegistry` 由装配层建一枚（`shared_ptr`），按 D2 的键 + 秘密线性查表；表项只存键与 `weak_ptr<SshSharedSession>`，秘密比较由仍活着的共享会话回答——表里**不留明文**，每次取用顺带剪掉过期项。
- `SshConnection` 在 `start()` 时向 registry 取一份 `shared_ptr` 并挂接；之后不再需要 registry。
- 最后一个租约 `Detach` 后，I/O 线程自行收口（关全部通道 → 断会话 → 释放回调表 → 清秘密材料）并退出；共享会话对象随最后一个 `shared_ptr` 释放。
- 进入终态（重连用尽／不可重连／用户停止）的共享会话把 `accepting_` 置假；registry 不再借出它，新标签得到一条新 TCP——与「这条链路已经没救了」的事实一致。
- 秘密材料从适配器移交给共享会话后，适配器立刻清掉自己那份；共享会话驻留到退出（重拨要重新认证），与裁决 7.99 D7② 的单标签口径同向，只是驻留期从「这个标签活多久」变成「这条共享链路活多久」。

### D7 关闭同步：`Detach` 带 ack，保证「close 返回后不再回调」

`SshConnection::close()` 压入带 ack 的 `Detach` 并等待。I/O 线程处理该命令时按「投 `on_finished` → 进度 `UserStopped` → `on_closed(LocalClose)` → 关通道 → 从租约表删掉 → 置 ack」的顺序走；处理完此前已入队的命令之后才置 ack 的语义由「同一线程顺序处理」保证。因此 `close()` 返回时，该租约的 `events` 指针不会再被 I/O 线程使用，且既有单标签用例「`close()` 后收到 `LocalClose`」的观测面原样保留。`close()` 是幂等的（`call_once`），第二次调用等第一次完成。

若 I/O 线程正卡在拨号里，`Detach` 要等拨号返回才处理——与现行 `close()` join 读线程同一条最坏等待（10 s 连接超时）。

### D8 通道级打开失败只收该标签

拨号成功后为每个租约开通道、请求 PTY、请求 shell。某一条的 open／PTY／shell 失败只给该标签投 `on_closed(Unknown)` 并从租约表删掉，不拖垮其余标签，也不进重拨环（认证已过，重拨只会再拒一回——与裁决 7.99 D2① 对通道段失败的归类同向）。

### D9 尺寸：每个租约记自己的最新值

`resize()` 把新尺寸压进 `Resize` 命令；I/O 线程更新该租约的尺寸并在通道已开时下发 window-change。退避期间收到的新尺寸在下一拍命令处理时落进租约，重拨开通道时直接用它——保住裁决 7.99 D7① 的「断线期间改窗口，重拨按新行列」。命令与拨号之间最后时刻到达的 resize 会在通道开好后补一次 window-change，最终状态仍正确。

### D10 agent 转发：键含开关，请求逐通道，中继逐连接

- `agent_forwarding` 入共享键：开关不同的标签不共享（转发是连接级能力，借一条没开转发的链路等于静默关掉它）。
- 回调表挂在共享会话上（`ssh_new` 之后、`connect` 之前），反向通道由 I/O 线程统一中继到本机 agent；反向通道数与每通道一条 unix socket 的边界不变。
- `auth-agent-req@openssh.com` 对**每条** shell 通道各发一次（OpenSSH 按通道记这个请求）；本机 agent 探路每回拨号只做一次。

### D11 装配：`make_ssh_session` 追加 registry 参数，其余零改动

`src/main.cpp` 在装配段早于 `make_ssh_session` 建一枚 `std::shared_ptr<conn::SshSessionRegistry>`，由 `make_ssh_session` 捕获并作为末位实参传入。新开标签、分屏、重启、撤销重开、启动恢复、CLI 六条腿因此自动共用同一张表。`Session` / `TerminalView` / 重连浮层 / `WS.01` 的关闭前确认都不改。

### D12 无新设置键、无新 UI

复用是缺省行为，不提供开关：需求没有要求可配，且新增键要走「六处同步 + 计数闸」的整套仪式，收益不足。用户唯一可感知的变化是：同档案的第二个标签不再产生第二条服务器连接；一个标签点「停止重连」会停掉共享链路（D5）。

## 4 类型与接缝草案

```cpp
// src/conn/ssh_shared.h（私有头：只有标准类型与 session 公共类型）
namespace borealis::conn {

struct SshSessionKey {
    std::string host;
    int port{22};
    std::string user;
    std::string auth_method;
    std::string identity_file;
    bool agent_forwarding{false};
    KnownHostsPolicy known_hosts_policy{KnownHostsPolicy::AcceptNew};
    std::string kex_algorithms;
    std::string host_key_algorithms;

    [[nodiscard]] static auto of(const SshProfile &profile,
                                 const SshAlgorithmOverrides &algorithms) -> SshSessionKey;
    [[nodiscard]] auto operator==(const SshSessionKey &) const noexcept -> bool = default;
};

class SshSharedSession;

class SshSessionRegistry final {
  public:
    auto acquire(const SshProfile &profile, std::optional<std::string> secret,
                 const SshAlgorithmOverrides &algorithms, RetryPolicy policy)
        -> std::shared_ptr<SshSharedSession>;

  private:
    // 表项**不存秘密**：秘密比较由仍活着的共享会话自己回答（`matches`），
    // 于是「链路已收口、弱引用还在」的空窗期不会在表里留一份明文。
    struct Entry {
        SshSessionKey key;
        std::weak_ptr<SshSharedSession> session;
    };
    std::mutex mutex_;
    std::vector<Entry> entries_;
};

class SshSharedSession final {
  public:
    SshSharedSession(const SshProfile &profile, std::optional<std::string> secret,
                     RetryPolicy policy, SshAlgorithmOverrides algorithms);
    ~SshSharedSession();

    [[nodiscard]] auto matches(const SshSessionKey &key,
                               const std::optional<std::string> &secret) const -> bool;
    [[nodiscard]] auto accepting() const noexcept -> bool;

    auto attach(SshConnection *lease, session::ConnectionEvents &events, session::Size size,
                std::function<void()> on_open, std::function<void()> on_finished) -> bool;
    auto detach(SshConnection *lease) -> void;
    auto write(SshConnection *lease, std::span<const std::byte> bytes) -> void;
    auto resize(SshConnection *lease, session::Size size) -> void;
    auto retry_now() -> void;
    auto stop_reconnect() -> void;

  private:
    // 命令队列、租约表、退避环、agent 状态、libssh 句柄全在实现文件里；
    // 私有头只留 libssh 前向声明与标准类型。
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace borealis::conn
```

`SshConnection` 的公开面（`start` / `write` / `resize` / `close` / `alive` / `retry_now` / `stop_reconnect` 与构造签名）除末位追加 registry 外不变；适配器在 `attach` 时交两枚闭包（`on_open` / `on_finished`）给共享会话，会话只调闭包写适配器的原子位——不用 `friend`，适配器的私有面不出头。

## 5 I/O 线程伪码

```text
io_loop:
  attempt = dial_then_open_all()          // 拨号 + 认证 + 给全部租约开通道
  loop:
    if attempt.linked:
      reason = serve_until_incident()     // 逐通道读 + 处理命令 + 投字节
      close_all_channels()
      if reason == LocalClose: break
    if leases.empty(): break              // 最后一个标签关了：静默收口
    if stop_reconnect or not should_reconnect(reason): break
    delay = reconnect_step(redials + 1, policy)
    if not delay: exhausted = true; break
    broadcast(progress{attempt=redials+1, delay})
    if not sleep_backoff(*delay): break   // 分片里仍处理命令
    ++redials
    teardown_attempt()
    attempt = dial_then_open_all()
  final_reason = closing ? LocalClose : attempt.reason
  if leases not empty:
    accepting = false                     // 与 attach 同一把队列锁
    broadcast(final progress); broadcast(on_closed(final_reason))
  else:
    accepting = false; terminated = true
  drain_queue_acks()                      // 残余 Detach 逐个放行；Attach 一律拒绝
```

`serve_until_incident` 每拍顺序固定为：处理命令 → 逐通道读 → 中继 agent → 等「新命令或 30 ms」。`sleep_backoff` 每 200 ms 一片，片间处理命令；`closing_` / `stop_reconnect_` / `retry_now_` 三枚原子位每片查一次（与现行同口径）。

## 6 关闭序与最坏等待

| 场景 | 行为 | 最坏等待 |
|:---|:---|:---|
| 关一个非末位标签 | `Detach` 处理：关通道、删租约、ack；链路与其余通道不动 | 一拍的 30 ms，或正在进行的拨号 |
| 关末位标签 | 同上，I/O 线程随后自行收口退出 | 同上 + 收口 |
| 链路丢失 | 全部通道收口 → 退避 → 重拨 → 给仍挂接的租约重开通道 | 由退避档决定 |
| 环终态 | 终态进度 + `on_closed(reason)` 投给全部挂接租约；`accepting_` 置假 | — |
| 应用退出 | 各标签按既有次序关闭；末位关掉后 TCP 释放 | 同「关末位标签」 |

## 7 测试计划

### 7.1 纯逻辑单测（新套件 `utest_ssh_session_key`）

1. 零端口与 22 等键；空认证方式与 `agent` 等键。
2. host／port／user／auth／identity_file／agent_forwarding／known_hosts_policy 逐字段不等。
3. 两张算法表逐表不等（含一空一非空、两非空不同）。
4. 秘密材料：都无＝等；同值＝等；异值＝不等；一有一无＝不等。

### 7.2 真协议集成用例（新套件 `itest_posix_ssh_shared_connection`）

共用一台 `FakeSshServer`（banner 非空）与一枚 registry。判据：

| # | 用例 | 判据 |
|:---|:---|:---|
| ① | 两个标签同档案同秘密 | `accepted_connections()==1`、`session_channels()==2`、两份 recorder 各收到 banner、两份 `alive()` 为真 |
| ② | 两条通道互不串流 | A 写 `aaa`、B 写 `bbb`；A 的 recorder 只见到自己的回显，B 同；服务器 `received_text()` 含两串 |
| ③ | 逐标签 PTY 尺寸 | 第二标签挂接后，服务器记录的两条通道初始尺寸分别为 (100,30) 与 (80,24)；只 resize A 到 (120,40)，只 A 的尺寸变 |
| ④ | 关一个不关另一个 | A `close()` 后 A 收 `LocalClose`、B 仍 `alive()` 且再写仍回显；`accepted_connections()==1` |
| ⑤ | 链路丢失只重拨一次 | `drop_connections()` 后两份 recorder 都见到退避进度；`accepted_connections()==2`、`session_channels()==4`、两份都恢复 `alive()` 并能再写 |
| ⑥ | 远端结束只收该标签 | `end_shell()` 后两份都收 `RemoteExit`，`accepted_connections()` 仍为 1、无重拨进度 |
| ⑦ | 不同秘密不共享 | B 用错口令：`accepted_connections()==2`，B 收 `AuthFailed`，A 不受影响 |
| ⑧ | 不同转发开关不共享 | B 的档案 `agent_forwarding=true`：`accepted_connections()==2` |
| ⑨ | 停止重连是连接级 | 掉线进入退避后 A 调 `stop_reconnect()`：两份都收终态 `UserStopped` 与 `on_closed(UserStopped)`；再等也不出现第二条连接 |
| ⑩ | 死会话不被复用 | ①② 两标签全部关闭后，第三个连接同档案：`accepted_connections()==2`（新 TCP） |

### 7.3 假服务器扩展（本条实现的前置）

1. `ChannelState` 增 `closed`；`peer_closed` 改为「全部非 agent 通道都已关闭」——否则③④⑥证不出来（关一个通道会把整条连接收摊）。
2. 逐通道 PTY 尺寸表：session 通道按打开次序编号，记录并更新各自的 (columns, rows)；公开 `pty_size_at(index)`。保留既有 `last_pty_columns/rows` 不动（旧用例编译面不变）。
3. 计数面增 `session_channels()` 的既有语义不变（它只数 `on_channel_open` 的 session 通道，agent 反向通道另有 `agent_channels()`）。

## 8 变异自证计划

| # | 注入 | 应变红的用例 |
|:---|:---|:---|
| M1 | registry 永远新建、不查表 | ①（`accepted_connections()==1`） |
| M2 | 秘密比较恒真 | ⑦ |
| M3 | 键比较忽略 `agent_forwarding` | ⑧ |
| M4 | `Detach` 关掉整条会话 | ④ |
| M5 | 通道 EOF 当链路事故 | ⑥（会重拨） |
| M6 | 进度只投第一份租约 | ⑤ 或 ⑨ 的第二份 recorder |

每支变异单独注入、跑指名用例、还原后复绿；结果记入裁决条目。

## 9 文档回写清单

1. `SPECIFICATIONS.md` §4.5 `SPEC.FEAT.CONN.02`：把「连接复用为延后子项」改判为已落，补落地形态与边界（不共享 SFTP／隧道；`MaxSessions`；stop 是连接级；Win32 未验证）。
2. `SPECIFICATIONS.md` §7：新增裁决（编号取该节最大号 + 1），记录本稿 D1–D12 的收口、测试读数与变异结果。
3. `PLAN.md` §4：删除「同一 profile 的连接复用」这一延后子项行；CONN.02 总行补落地现状。
4. `PLAN.md` M4 行的需求清单若需标注，只改该行尾部现状句式，不动阶段映射。
5. `ARCHITECTURE.md` §3.1：写明「SSH 传输为一条 TCP 一条 I/O 线程、多通道复用；其余连接仍每会话一个读线程」。
6. `ARCHITECTURE.md` §7.2：`ConnectionEvents` 的回调线程措辞从「连接的读线程」改为「连接的 I/O 线程」（SSH 共享会话下一条 I/O 线程服务多条通道）。
7. `CHANGELOG.md`：新版本条目（动机、四段落地形态、读数、边界）。
8. `AGENTS.md` §6：若无长期事项变化则不改。

## 10 交付分批

1. **批 1**：`ssh_shared.{h,cpp}`（键、registry、共享会话、命令队列、退避环）+ `utest_ssh_session_key`；`SshConnection` 改薄适配器（单会话路径先跑通，既有 `itest_posix_ssh_connection` 五案全绿）。
2. **批 2**：假服务器两处扩展 + 新集成套件十案。
3. **批 3**：`src/main.cpp` 建 registry 并接线六条腿；全量构建与测试。
4. **批 4**：变异自证、文档回写、分层提交。

## 11 边界与欠账

- **服务器 `MaxSessions`**：OpenSSH 缺省 10。超过时第 11 条通道 open 失败，按 D8 只收该标签并提示失败；本端不做排队或复用降级（需求未要求）。本沙箱的 libssh 假服务器不设上限，故这条只有推断／未验证。
- **`stop_reconnect` 的连接级语义**：一个标签停止会停掉共享链路的重连（D5）。UI 文案不需要改，但用户可能在另一个标签里看到「已停止」；登记为共享链路的事实语义。
- **共享会话不跨能力**：SFTP／隧道／密钥推送仍各自拨号，服务器侧仍会看到额外连接。需求原文只点名多标签终端。
- **Win32／MSVC 未编译验证**；真机 OpenSSH 与真 agent 签名链仍未验（沿用裁决 7.146 的证人边界）。
- **`restart` 的语义微调**：链路活着时「重启」变成「在共享链路上换一条新通道」；链路已死时仍是一条新 TCP。对用户可观察的结果一致（新 shell），但服务器侧不再有第二条第登录记录。
- **无 keepalive**：空闲会话被中间设备掐断仍不可知；即便共享，也不会因为多个标签而更容易发现。
