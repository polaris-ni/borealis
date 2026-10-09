# SSH 断线自动重连设计稿（`SPEC.FEAT.WS.05` 的 SSH 腿）

> **状态**：评审已收口（2026-10-09）——§3 的 D1–D7 经人逐条裁决：**D1–D6 与 D7①②③ 全按推荐，唯一改判在 D7④**——隧道/SFTP 由「不联动」改判为「**本期顺带做 SFTP 掉线重拨**」（细则见 §3 的 D7 行与草图屏 C）。并入 `codespec/SPECIFICATIONS.md` §7 裁决 **7.99**，据此转实现（§4 布局落 `src/`）。
> **配套高保真草图**：`codespec/UI_RECONNECT.draft.svg`（§2 的 1–7 标号判据对应其底部标注；SVG 为事实来源，后续改图直接改 SVG）。
> **已落地的前置**：`SPEC.FEAT.WS.05` 本地腿（dead-session 浮层 + `session.restart`，裁决 **7.86**）、`SPEC.FEAT.CONN.02` SSH 真传输（`src/conn/ssh_connection.{h,cpp}`，裁决 **7.94**）、纯逻辑退避件 `conn::RetryPolicy` / `retry_delay_ms` / `should_retry`（`src/conn/tunnel_model.{h,cpp}`，裁决 **7.96**）。
> **本稿不碰代码**：收口后才落 `src/`。

## 1 范围与边界

**本期交付**：SSH 会话断线后的**自动重连**（次数与间隔可配，D4）、关闭原因的**分类出口**（哪些断线值得自动重试，D2/D3）、重连过程的**可见状态**（视口浮层的重连中态与终态分档，D5）、以及重拨期间的尺寸下发与凭据取用口径（D7①②）。附带两处已落形态的修正：SSH 标签浮层上的「重启」今天会起一格**本地 shell**（D6），SFTP 面板的操作期掉线今天**从不进入可重试态**（D7④ 改判，本期一并做）。

**不本期交付**（§7）：keepalive 心跳（`SPEC.FEAT.CONN.02` 延后子项在册）、状态栏本体（`appearance.status_bar.show_reconnect` 有键无宿主）、known_hosts `Ask` 交互询问（7.94④ 已登记为独立任务）、SSH 连接复用后的多 pane 各自重连记账、mosh 式会话续接、SFTP 传输**续传**（掉线那一单按失败收尾）。

**需求原文两句**（`SPECIFICATIONS.md` 的 `SPEC.FEAT.WS.05`）：「进程退出后保留终端内容供回看（可一键重启）」；「SSH 断线自动重连（次数与间隔可配）」。本稿的 D1 完全落在这两句的交点上——自动重连是**非用户主动**的路径，因此第一句对它的约束比对 7.86 那条「用户点下去的重启」更强。

**现状事实**（本轮逐条读源核过，是 §3 各条裁决的根据）：

1. `src/conn/ssh_connection.cpp` 的读线程把**六条**停机路径（会话分配失败、dial/认证失败、通道打不开、PTY 请求失败、shell 请求失败、Active 期间的读错/EOF/通道被关）全收敛成同一个无参 `events_->on_closed()`，**没有任何原因出口**——「错误分类提示」这条需求（CONN.02 原句）至今没有数据源。
2. `SshConnection::alive()` 是 `started ∧ ¬closing ∧ channel_open_`：通道一掉就回 false，所以标签的退出角标（7.91）与关闭前确认（7.83）在掉线那一刻已经按「无活进程」显形。
3. `SshConnection::resize()` 在 `¬channel_open_` 时**整段早退**，连成员 `size_` 都不更新 ⇒ 断线期间用户改窗口尺寸，下一次拨号拿到的还是旧行列。
4. `run_loop()` 一开头就把 `secret_` move 给 dial 腿并随即 `reset()` ⇒ 连接对象**不持有可重拨的材料**（7.94③ 的「明文不比认证活得久」）。
5. 装配层 `restart_current_tab` 固定按 `connection.local_shell` 起本地会话，并 `ssh_profile_by_tab.erase(id)`（代码注释自称「就此清账」）⇒ 今天在 SSH 标签上点「重启」，得到的是一格本地 shell，标签名却还是档案名。
6. 装配层是**单会话架构**：`ws_hooks.make_pane` 对每个 pane 返回同一份 view/session（源码注释「暂不支持，返回空」前的过渡形态），故本期不存在「同标签另一格被牵连」的场景；D1 的取舍只围绕内容保留与对象换血。
7. 状态栏控件本体未落：`src/ui/` 无该件，`appearance.status_bar.*` 只有 schema 行与设置面板开关，无任何消费点 ⇒ 「重连倒计时」没有既有挂载点，D5 因此不能选「状态栏条目」。
8. SFTP 面板**已有整条重拨腿，只是摸不到**：`SftpPanel::connect_now()` 会现取档案与 `resolve_secret()`、投一条 `Connect`，worker 侧 `disconnect()` 后再 `connect()`（`SftpClient::connect()` 文档那句「不重拨」只约束同一会话内，重拨走的就是这条断线重建路径）；`ConnState::Failed` 与「重试」按钮（词条 `sftp.action.retry`）也都在。**缺的是入口**：`apply_message` 只在 `ConnectDone` 那一支写 `state_`，操作期（列目录/增删改/传输）失败只写 `last_error_` ⇒ 掉线后每次操作都对着死会话再发一遍、逐条回 `Network`/`NotConnected`，状态行换文案但「重试」按钮永远不出现。
9. SFTP 的凭据面**零外扩**：装配层已按标签驻留 `ssh_secret_by_tab`，面板每次经 `Hooks::resolve_secret` 现取（裁决 7.95 D8 既定形态），故 D7④ 这条重拨**不新增**任何明文驻留，与 D1 代价 (a) 不同轴。

## 2 草图与判据对应

草图三屏：屏 A＝自动重连进行中（一标签一格的 SSH 会话，浮层之下保留断线前那一屏）；屏 B＝三档终态；屏 C＝SFTP 面板的掉线重拨三步（D7④ 改判新增）。另有退避序列与设置页三行示意。1–7 标号判据：

| 标号 | 判据 | 对应需求 / 裁决项 |
|---|---|---|
| 1 | 内容不重建、不擦除：掉线→重拨成功这段路径上 `Session` / 网格 / 标签身份一个都不换；浮层之下那一段回看内容仍是断线前最后一屏 | `WS.05`「保留终端内容供回看」（D1 的根据） |
| 2 | 重连中态：卡片两行文案（「连接已断开 · 第 i / M 次自动重连」+「X 秒后重试（首次 1 s，逐次翻倍，钳 30 s）」）+ 两枚按钮「立即重试」「停止重连」 | `WS.05`「自动重连（次数与间隔可配）」（D5①） |
| 3 | 重拨期间 `alive()` 恒 false（远端确实没有活进程）：退出角标与关闭前确认按今天语义走，不因重连而撒谎；重连可见性只走一条 latest-value 状态快照 | `WS.01`/`WS.04` 的既有投影不破（D7③） |
| 4 | 卡片之外的落点仍归选区（命中序沿用裁决 7.86②：浮层按钮排在鼠标上报分流之前，其余照旧）——断线期间旧输出仍可选可复制 | `INTERACT.02`/`03` 的既有语义（D5 代价） |
| 5 | 关闭原因有出口：六条路径归成分类枚举，可重连集与不可重连集由纯逻辑件裁决；出口形态是一条**带默认实现**的 `on_closed(CloseReason)` 重载，其余连接腿与测试替身零改动 | `CONN.02`「错误分类提示」+ `WS.05`（D2/D3） |
| 6 | 边界如实登记：无 keepalive ⇒ 空闲会话被中间设备静默掐断时读循环不报错，掉线不可知、重连不触发；「自动重连」不等于「永不掉线」 | `CONN.02` 延后子项的连带后果（§7） |
| 7 | SFTP 面板的掉线重拨（D7④ 改判本期做）：操作期返回 `Network`/`NotConnected` 即自动重拨一回并复列断线前那个目录，重拨失败落既有 `ConnState::Failed` 与「重试」按钮；一次掉线只重拨一回，凭据经 `resolve_secret` 现取、零新增驻留 | `CONN.04` 的可用性连带（D7④） |

屏 B 的三档终态各自锁一件事：**次数用尽**（退避环走完 ⇒ 终态，把决定权交回用户）；**不可自动重连的原因**（认证失败、主机密钥不符/策略拒绝——重试只会重复失败且可能触发远端锁定，安全语义一律不自动重试）；**远端 shell 正常退出**（EOF 不是断线，沿用 7.86 那档形态，但「重启」的目标按 D6 改成档案）。

屏 C 与屏 A/B **不同轴**：SFTP 没有持续读循环，掉线只能在**下一次操作**返回错误时被发现，所以它要的是一回自动重拨而不是退避环——三步即「发现掉线 → 自动重拨一回（成功复列 / 失败落 Failed 给既有『重试』）」。

## 3 裁决（D1–D7，2026-10-09 收口：全按推荐，唯一改判在 D7④）

| # | 待决 | 选项 | 结论（推荐） | 理由 |
|---|---|---|---|---|
| **D1** | 重连归哪一层 | ① 装配层协调件：`SshConnection` 只报关闭原因，装配层按退避用 `Scheduler::set_timeout` 重建 Connection+Session，复用 7.86④ 那条「同 TabId 就地覆盖 `TabWorkspace`」的动作；② **传输腿内部重拨**：读线程发现掉线后不报 `on_closed`，自己按退避重跑「dial→通道→PTY→shell」那一段，成功后回到读循环，只有用尽/不可重连/主动 close 才报关闭 | **②** | ① 每次自动重连都要换掉 `Session` 对象，而覆盖 `TabWorkspace` 会连带**丢掉断线前的回看内容**——那条内容在 7.86 里是用户**主动**点重启才放弃的，需求那句「保留终端内容供回看」在用户没动手的自动路径上不能破；此外每次掉线都重建会话/视口/OSC 状态/标题名，抖动面远大于收益。② 的代价如实登记：(a) 明文副本要驻留到 `close()`（与 7.94③ 的「认证后即清」相冲，但本仓已有两处同口径外扩——7.96⑥ 隧道为自动重试保留至 stop、装配层为 SFTP 保留 `ssh_secret_by_tab`，本条只是第三处）；(b) 读线程要跑**可中断的分段退避**；(c) 拨号序列要从 `run_loop` 抽成可重入的一支（首次与重拨共用同一段代码，不复制第二份） |
| **D2** | 哪些关闭值得自动重连 | ① 分类表（推荐）：**可重连**＝Active 期间掉线（读错 / 通道被对端关且非 EOF）、dial 阶段的网络类失败（不可达 / 连接超时）；**不可重连**＝认证失败、主机密钥与 known_hosts 不符、策略拒绝核对、远端 shell 正常退出（EOF）、装配层主动 `close()`；② 不分类，凡非主动关一律重拨 | **①** | ② 会把主机密钥变更也退避重拨——与 7.94④「未知即拒绝，绝不静默放行」的保守取向直接冲突，且对认证失败重试可能触发远端账号锁定；分类表本身是无 libssh 类型的纯裁决，可单测锁住 |
| **D3** | 分类怎么传出连接 | ① `include/borealis/session/connection.h` 的 `ConnectionEvents` 增一条**带默认实现**的 `on_closed(CloseReason)` 重载（默认转调无参版）：只有 `SshConnection` 改为调用它，`Session` 覆写它记录状态，ConPTY/forkpty 两腿与全部测试替身零改动；② 构造时由装配层注入一份共享关闭快照对象（公共接口不动，但装配层必须认得具体连接类型，违架构 §7.2「会话层只认 `Connection` 接口」的粒度拍板）；③ 本期不外传，只在连接内部自决是否重拨（UI 拿不到原因 ⇒ 终态浮层只能写「连接失败」一句废话，CONN.02 那句错误分类提示继续无源） | **①** | 这是本仓自有的公共头（不是框架），加一条带默认实现的重载是 additive：既有实现者与替身编译面不变、行为不变。`CloseReason` 同时服务 CONN.02 的「错误分类提示」，本期只消费到浮层文案一档，逐类细化提示留该条 |
| **D4** | 退避参数与键位粒度 | ① **全局三键**落 `settings.connection.ssh`：`reconnect_base_delay_ms{1000}`、`reconnect_max_delay_ms{30000}`、`reconnect_attempts{3}`；② 逐档案可配（扩 `SshProfile` + 向导字段 + store 往返）；③ 全局缺省 + 档案覆盖 | **①** | 需求只说「可配」，未规定数值与粒度。②/③ 要同时动档案模型、向导、落盘与明文审计四处，属另一个意图。缺省**次数取 3**：不限次会把「连不上」变成静默无限重试，用户只能靠肉眼看屏幕不动；用尽后落终态浮层，把决定权交回人。退避算式直接复用 `conn::RetryPolicy` + `retry_delay_ms` / `should_retry`（现成件、`utest_tunnel_model` 已锁过表），代价：SSH 会话侧要包含 `src/conn/tunnel_model.h`（同域，不为它单拆公共头；把 `RetryPolicy` 搬家到 `include/borealis/conn/retry.h` 属未要求的重构，本稿不做） |
| **D5** | 重连状态长什么样 | ① **扩既有 dead-session 浮层为两态**（推荐）：重连中＝两行文案 + 「立即重试」「停止重连」；终态＝回落今天那枚单按钮形态，但文案按 `CloseReason` 分档；② 不画任何东西，只 `AURORA_LOG`；③ 状态栏条目（`show_reconnect` 键位已登记） | **①** | ③ 现在结构上不可选：状态栏本体未落（§1 第 7 条）。② 让用户看不出「屏幕为什么不动」，与自动重连的整个卖点相反。① 复用 7.86 已落的浮层层次（视口绘制序列最后一层）、带底算式（`mix_half`）、命中序（判据 4）与按钮盒观测点，增量是一条状态行文案与第二枚按钮的命中区；代价＝词条若干 + 走查面 +1 |
| **D6** | SSH 标签的「重启」按钮起什么 | ① **本期修**：按 `ssh_profile_by_tab` 重开 SSH（agent/句柄型静默，询问型弹 `credential_prompt`），本地标签照旧走本地腿；② 保留现状，登记延后 | **①** | §1 第 5 条是**已落的生产语义错误**（点 SSH 标签的「重启」得到本地 shell，标签名却还是档案名），而需求那句「可一键重启」在 SSH 会话上本就要求重开 SSH；本条既然要给终态浮层一枚重启出口，把它修对的成本低于再登记一轮。收口后 7.86④ 那句「重启腿固定走本地 shell」的形态须就地更正并写进新裁决 |
| **D7** | 四条连带口径 | ① 重拨用**最新** `size_`：`resize()` 在无通道时仍更新成员、只跳过 `ssh_channel_change_pty_size`（修 §1 第 3 条）；② 凭据取用＝连接对象内驻留到 `close()` 的那份副本（D1 的代价 (a)），不落盘不进日志，停即清（CONN.09）；③ 重拨期间 `alive()` 恒 false、退出角标与关闭前确认按今天语义走，重连可见性只由状态快照表达；④ ~~隧道与 SFTP 面板不随会话重连联动~~ **改判：本期顺带做 SFTP 掉线重拨** | **①②③ 如述；④ 改判「本期做」** | ① 是需求外的真实缺陷顺手在本条修复（同一意图链：重拨要拿得到正确尺寸）；② 把 7.96⑥ 的口径从隧道扩到会话腿；③ 不改公共语义，撒谎的代价是关闭前确认误报「有运行中进程」。**④ 细则（人改判，推荐档原为不联动）**：终端会话的重拨与隧道仍互不牵连（隧道自开独立会话，7.96①），但 SFTP 面板自己的会话掉线要能自己爬起来——(i) 判据＝操作期返回 `SftpError::Network` 或 `NotConnected`（分类落 `src/conn/reconnect`，纯逻辑可单测）；(ii) 命中即由面板自动投一条 `Connect`（同档案、`resolve_secret()` 现取），**一回掉线只重拨一回**（旗标随连接成功复位），不套退避环——§1 第 8 条：这里没有持续读循环，掉线只能被下一次操作发现，退避环无对象可退；(iii) 重拨成功即复列断线前那个 `remote_path_`，用户不必重新进入路径；(iv) 重拨失败落既有 `ConnState::Failed`，出口是既有那枚「重试」按钮（`sftp.action.retry` → `connect_now()`），本条只补「操作期失败也要写 `state_`」这一行迁移；(v) 断线时那一单传输按失败收尾（半成品清理腿既有），**不做续传**（§7）；(vi) SFTP 会话随标签生灭的形态不变（7.95 D8），凭据面零外扩（§1 第 9 条） |

## 4 文件布局 / TU 拆分

```
include/borealis/session/connection.h   修改 —— CloseReason 枚举（LinkLost / DialNetwork / AuthFailed /
                                         HostKeyRejected / RemoteExit / LocalClose / Unknown）+ ConnectionEvents
                                         增带默认实现的 on_closed(CloseReason) 重载（默认转调无参版）
src/conn/reconnect.{h,cpp}              新增 —— 纯逻辑件（无 libssh 类型，先例 ssh_dial.h 的纯函数段）：
                                         should_reconnect(CloseReason) 分类表、下一档延迟与用尽判定
                                         （吃 conn::RetryPolicy + attempt）、终态档位枚举；
                                         D7④ 的 SFTP 侧同处一件：sftp_error_drops_link(SftpError)
                                         （Network / NotConnected 为掉线类，其余不是）
src/conn/ssh_connection.{h,cpp}         修改 —— 拨号→通道→PTY→shell 抽成可重入支；读线程的重拨环（分段
                                         休眠 200 ms 片 + 每片查 closing_）；secret_ 驻留到 close()；
                                         resize() 无通道时仍更新 size_；关闭时按分类报 on_closed(reason)
src/ui/sftp_panel.{h,cpp}               修改 —— D7④：apply_message 里操作期失败若属掉线类即置
                                         ConnState::Failed 并自动投一回 Connect（一回旗标），
                                         重拨成功后复列断线前的 remote_path_
include/borealis/session/session.h      修改 —— 重连状态观测点（latest-value 快照：第几次/等待毫秒/
                                         终态档位）+ 取数入口 + 一条「停止重连」的中止入口
src/session/session.cpp                 修改 —— 覆写 on_closed(CloseReason)：存快照并 wake_frame()
                                         （7.86⑤ 那条「连接生死即唤醒」的例外口径扩到重连状态变更）
src/ui/terminal_view.{h,cpp}            修改 —— dead-session 浮层两态：重连中（两枚按钮 + 状态行）/
                                         终态（一枚按钮，文案按 CloseReason 分档）；按钮盒观测点随之扩
src/ui/settings_i18n.cpp                修改 —— 浮层状态行与两枚按钮、终态分档文案若干条（键名 ASCII、
                                         词条中文，CJK-LITERAL 例外口径同前例）
include/borealis/config/settings.h      修改 —— SshDefaults 增三键（D4）
src/config/store.cpp                    修改 —— 三键读写往返（缺省即旧行为的反面：新键有默认值）
src/config/form_transfer.cpp            修改 —— 三键 ⇄ 表单搬运（一行一键表内追加）
src/ui/settings_catalog.cpp             修改 —— 连接页三行登记（ConsumerStatus::Wired + EffectLevel::Immediate）
src/main.cpp                            修改 —— 「停止重连」「立即重试」两枚动作接线；D6：重启按标签
                                         档案身份分流（SSH → open_ssh_tab，本地 → 现路径），并在重连
                                         成功时不擦 ssh_profile_by_tab 那份身份
tests/unit/utest_reconnect.cpp          新增 —— 分类表逐条（CloseReason 与 SftpError 两张）、退避档位序列与
                                         钳制、用尽判定、终态档位
tests/unit/utest_ssh.cpp                修改 —— 若分类判定落在 ssh_dial 侧则补例（现收在 utest_reconnect，
                                         本条只在有交叉时补）
tests/unit/utest_session.cpp            修改 —— 重连状态快照写入与「状态变更即唤醒」例
tests/unit/utest_config.cpp             修改 —— 三键往返 + 缺省档
tests/integration/itest_render_viewport.cpp 修改 —— 浮层两态落帧、两枚按钮真点击（立即重试/停止重连）、
                                         按钮之外仍是选区、终态文案分档
tests/unit/utest_settings_catalog.cpp   修改 —— 三键白名单登记（缺键即红，既有机制）
```

> 不改 Aurora 侧任何公共 API：浮层是既有自绘路径（7.86/7.81 两次先例）的第三次消费，按裁决 7.13① 预判**框架面真缺口 0**，实现开工时逐条读源复验后再出账。`CloseReason` 落本仓公共头 `include/borealis/session/connection.h`，不是框架类型。

## 5 数据流与线程模型

```
读线程（每会话一条，架构 §3.1）
  dial → channel → PTY → shell → 读循环
    │                     │
    │                     └ 掉线（读错 / 通道被关 / EOF）→ 归类 CloseReason
    │                          ├ 可重连 ∧ attempt < M ──► 分段退避休眠（200 ms 片，每片查 closing_）
    │                          │        └─ 状态快照{attempt, delay_ms} ─► Session（latest-value）─► wake_frame
    │                          │              └─ 重拨（同一段可重入序列，取最新 size_）
    │                          └ 不可重连 ∨ 用尽 ∨ 收到停止令 ─► on_closed(reason) ─► Session 存终态档位
    └ dial 阶段失败 ─► 网络类＝进退避环；认证/主机密钥类＝直接 on_closed(reason)（不重试）

主线程（UI）
  Session 的重连状态快照（原子 latest-value）──每帧读──► TerminalView 合成浮层两态
  用户动作：立即重试 / 停止重连 / 重启（按档案）──装配层闭包──► 连接对象的控制位 + 重新装配
```

- **UI 线程零阻塞 IO**（AGENTS 第 25 条）：退避休眠与重拨全在读线程；跨线程交换的是**合成后的最终值**（第几次 + 还要等多久 + 终态档位），不是事件流水（口径同裁决 7.97 D6 的状态泵）。
- `close()` 的 join 上界：一个分段片（200 ms）+ 一次建连超时（`connection.ssh.connect_timeout_sec`，现缺省 10 s）。后一半是今天既有形态（隧道 `stop()` 同口径，7.96②），本条只新增前一半。
- 「立即重试」不取消当前休眠而是把它**截断**：状态位翻转后下一个分段片即出环重拨 ⇒ 最坏延迟 200 ms，不需要额外同步原语。
- 「停止重连」= 置终止位 + 让环自己落终态（不硬杀读线程），此后浮层只剩「重启（按档案）」一枚出口。
- **SFTP 腿（D7④）在自己的 worker 上闭环**：`SftpPanel` 的单工作线程串行消费请求队列，掉线由某条操作的返回值暴露（`Network`/`NotConnected`）⇒ UI 侧 `apply_message` 见掉线类即写 `state_ = Failed` 并回投一条 `Connect`（一回一次），worker 侧走既有 `disconnect()`+`connect()` 那条重建路径。全程不碰终端会话的读线程，两条腿无共享状态。

## 6 验收判据映射

- **次数与间隔可配**（需求原句）：`utest_config` 三键往返 + `utest_settings_catalog` 白名单 + `form_transfer` 搬运例；设置生效档位＝下一次断线取最新值（拨号那一刻现读）。
- **分类表**：`utest_reconnect` 逐条锁「可重连集 / 不可重连集 / 用尽判定 / 退避序列 1-2-4-8…钳 30 s」；D2 那张表就是它的判据文。
- **内容保留**（D1 的立身根据）：`itest_render_viewport` 补一条——替身连接报掉线后进退避环，断线前那一段网格内容与浮层落帧后的内容**逐位可比**（对象没换血 ⇒ 结构上成立，用例只守别把它改回去）。
- **状态泵只留最新值**：同一 attempt 内多次状态写入不积压，视口读到的是最后一次（`utest_session` 出证人）。
- **重拨取最新尺寸**（D7①）：断线期间调 `resize()`，重拨时新行列交进 dial 腿——沙箱无 sshd，故本期以**替身连接**验到 `Session`/`Connection` 边界；真实 PTY 重排待 sshd etest（同 7.96 代价①）。
- **浮层两态与两枚按钮**：`itest_render_viewport` 真派发（Press→Release 落按钮盒正中 ⇒ 动作计数恰 1；落点之外 ⇒ 仍是选区且动作计数 0），命中序沿用 7.86② 的既有分支。
- **D6 的重启分流**：装配层无可断言纯逻辑件（`main.cpp` 不编入 CTest runner），判据归**真机走查**（同 7.83④ 的口径）；但分类表与两枚动作的可测面全在 §4 那些套件里。
- **SFTP 掉线重拨（D7④）**：可测面只有分类表——`utest_reconnect` 锁「`Network`/`NotConnected` 属掉线类，`PermissionDenied`/`NotFound`/`Cancelled`/`LocalIo`/`Protocol` 不属」。面板侧那一行 `state_` 迁移与一回旗标**无头不可测**：`client_` 是成员而非注入点，`SftpPanel` 今天整件无 itest 覆盖（7.95 落地时的既定形态，只有 `utest_sftp_model`/`utest_sftp_format` 两层纯逻辑），故判据归真机走查；本期不为这一行去开注入面（属未要求的重构）。
- **真机走查**（AGENTS 第 33 条）：浮层观感、两枚按钮落点手感、断线瞬间旧内容与重连后新输出的交接感、真实网络掉线的及时性、SFTP 面板掉线后那一回自动重拨与「重试」出口——未走查不得称「可用」。

## 7 本期不做（延后）

- keepalive 心跳（`CONN.02` 延后子项）——它是「空闲被静默掐断能否被发现」的前置，落期不变；本条边界见判据 6。
- 状态栏本体（`appearance.status_bar.*` 全部键位仍空挂）。
- known_hosts `Ask` 的 UI 询问（7.94④ 登记的独立任务）。
- SSH 连接复用后的多 pane / 多标签共享连接的重连记账（复用一落地，「每标签一条连接」这条前提就作废，本条的归属层须重评）。
- mosh 式会话续接（远端进程与屏幕状态的真续接，需求未提）。
- 逐档案重连档（D4②/③）与错误分类提示的逐类细化（CONN.02 那句只消费到浮层分档文案）。
- SFTP 传输**续传**（D7④ 细则 (v)）：掉线时那一单按失败收尾，本地/远端半成品的既有清理腿负责清干净，用户重发起；断点续传要远端句柄语义与偏移记账，需求未提。
- 隧道与终端会话的重连联动（D7④ 改判只把 SFTP 拉进来，隧道仍自开独立会话、自己那套退避环，7.96①/7.97 D9）。
- Windows/MSVC 腿验证（与既有口径一致，待真机）。

## 8 收口说明

D1–D6 与 D7①②③ 全部按推荐档收口，**唯一改判＝D7④**：由「隧道/SFTP 不随会话重连联动」改判为「本期顺带做 SFTP 掉线重拨」，细则六条已并入 §3 的 D7 行、草图新增屏 C 与判据 7。其中三条会**触动已落形态**，须在收口写回时一并更正：D1 把 7.94③ 的「认证后即清副本」外扩第三处、D3 动本仓公共头 `session/connection.h`、D6 就地更正 7.86④ 那句「重启腿固定走本地 shell」。

收口动作＝`SPECIFICATIONS.md` §7 新裁决（含 WS.05 现状回写）+ `CHANGELOG.md` 记版本 + `PLAN.md` 的 WS.05 行与 M3 行回写，然后按 §4 布局转实现（分三批：纯逻辑件与分类出口 → 读线程重拨环与状态快照 → 浮层两态、键位接线与 SFTP 重拨）。
