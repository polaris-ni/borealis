# CONN.03 批 6 设计稿：跳链接进 SFTP／隧道／密钥推送三条腿

> 父稿：`codespec/CONN03_PROXYJUMP.draft.md`（D1–D10 已裁决，终端腿按裁决 7.148 结项）。
> 本稿只做父稿 §7 的**批 6**：把 `SshHopChain` 接进剩下三条拨号腿，撤掉 `src/conn/ssh_dial.h`
> 的 `TODO(SPEC.FEAT.CONN.03)`——在册代码挂账就此从两处减到一处（另一处是
> `tests/support/fake_ssh_server.cpp` 的 CONN.02 Win32 腿，不在本稿射程）。
> 新裁决号：**7.153**（落地后回写）。
>
> **评审收口（2026-10-25）**：D1–D7 全部按推荐档裁决（D4 选 A 放行带跳自启；D5 零新增档；
> D6/D7 按稿）。备档 B 一律否决，理由见各条正文。

## 0 需求与边界

- 需求：`SPEC.FEAT.CONN.03` §4 清册唯一明排期的功能棒——带跳档案在 SFTP 面板、隧道、密钥推送
  三条腿上与终端腿走**同一条跳链**，而不是按父稿 D6 拒拨。
- 边界（本稿不碰）：逐跳口令的询问 UI（D8 既定「本期询问只覆盖目标」）；SFTP 面板会话复用
  （CONN.02 另棒）；Win32 `make_hop_pair` 的 loopback 腿（D10，未编译，同批 4/5 边界）；
  任何新 UI 件与新词条。

## 1 地基实测（F 表，全部读码取证）

- **F1 三个拨号点**：`sftp_client.cpp:91`（`SftpClient::connect`：ssh_new→dial→sftp_new/init，
  不交 seed）；`tunnel_client.cpp:352`（`run_loop` 每次重试在循环体内 dial，重试吃留存的
  `secret_` 副本）；`key_push.cpp:237`（`push_public_key`：纯逻辑闸先行，自开会话后 dial）。
  三处 profile 带跳时都落 `HopUnwired` 拒拨。
- **F2 现形证人**：`itest_posix_ssh_proxy_jump.cpp:451` 的 E6 一案逐腿断言拒拨（sftp 回 false +
  `SftpError::Network`；push 停 `PushStage::Dial`；Tunnel 到 `Failed`；且两台假服务器
  `accepted_connections()==0`）。同文件另有 dial 原语案
  `profile_with_jumps_without_seed_refuses_to_connect_directly` 与空链案
  `chain_without_hops_is_a_valid_no_op`。
- **F3 终端腿先例**（`ssh_shared.cpp`）：`std::optional<SshHopChain>` 成员，目标 dial **之前**
  `open(profile, secret_copy.hops, algorithms)`，`seed_fd()` 交 dial；拆除腿 `hop_chain.reset()`
  排在 `ssh_free` **之后**（F17 所有权）；重连＝整链重开（复用同一链对象，`open()` 先收旧链）；
  每次尝试在 bundle 副本上消费。
- **F4 bundle 隐式构造先例**（父稿 D8）：`SshSecretBundle` 有从 `std::optional<std::string>`
  的隐式构造——形参换型时旧调用点**零改动**即可编译，本批沿用同一手法。
- **F5 留档面已齐**：`ssh_secret_by_tab`（main.cpp:1107）自批 4 起存**整套 bundle**
  （target + hops，按跳数对齐）；SFTP 面板的 `Hooks::resolve_secret`（main.cpp:3351）今天只取
  `target` 那一格——材料就在同一格里，只差交出来。
- **F6 补齐接缝**：`with_hop_secrets`（main.cpp:2521 声明、2824 实现体）＝「目标材料已到手 ⇒
  经凭据库补齐 hops ⇒ 回 bundle」的通用件；0 跳同步回调，非跳档案时序零变化；缺材料的
  `Ask`/`None` 跳留空，拨号侧落 `Auth` 带 hop_index（D8 既定口径）。
- **F7 隧道装配**：`launch_tunnel`（main.cpp:3208）只收 target secret；手动启动询问型经
  `pending_tunnel`+`ask_secret` 异步回-call；启动序自启扫描（main.cpp:3297–3323）只拉起
  「autostart ∧ 目标可静默解析」，其余记 `tunnel_skipped` 行内提示——本路径零模态、零阻塞。
- **F8 密钥推送装配**：`keys_hooks.push`（main.cpp:3513）询问型经 `pending_push` 回-call；
  `submit_keys_push`（main.cpp:3419）把档案+材料按值投进 keys worker，回包只有终值。
- **F9 失败档现状**：`SftpError` 无跳板档（建连/认证/算法被拒一律 `Network`，头注既定）；
  `TunnelError` 只有 `DialFailed` 一档盖住建连/主机密钥/认证；`PushFailure` 无独立跳档，
  `key_push.h` 头注写明「`Options`/`Hop`/`HopUnwired` 都并入『连不上』措辞，精确原因在日志」。
- **F10 关闭序硬约束**：seed 端句柄**仍归链所有**（libssh 0.10 `ssh_disconnect` 不关
  `SSH_OPTIONS_FD` 递进去的句柄）——吃 seed 的会话必须 `ssh_free` 之后才许 `chain.close()`，
  父稿 §5 的 F12 探针踩过段错误。

## 2 目标与非目标

目标：⑴ 三条腿对带跳档案按同一条链建链，拨号失败可见「第几跳谈不成」（日志带 hop_index，
口径同终端腿）；⑵ E6 拒拨案翻正为逐腿正向证人；⑶ `ssh_dial.h` 的 TODO 撤账；⑷ 非跳档案
三条腿行为**逐帧不变**（现有 itest 一行不改即过，作为隐式回归网）。

非目标：逐跳询问 UI；面板级跳链选择器；known_hosts 交互新增；Windows 腿实测（见 §7）。

## 3 决策（D 表，逐条请裁）

### D1 统一形态：三条腿无条件建链，0 跳即空操作

每条腿的建链形状与终端腿同构：无条件构造 `SshHopChain` 并 `open()`——0 跳链是**合法空操作**
（`open` 回 true、`seed_fd` 回 0，按直连拨，`chain_without_hops_is_a_valid_no_op` 已有证人）。
于是腿代码对带跳/非跳**同一条路径**，父稿 D6 的「按腿拒拨」口径随接线自然退场；
`HopUnwired` 保留在 dial 层做兜底——接了线的腿永远带 seed 或空链走，只有漏接线的装配才会
再见到这一档。
- 备档 B：各腿保留 `proxy_jumps.empty()` 分支，非跳不碰链对象。多一条分支、多一处
  「分支与链行为漂移」的面，不建议。

### D2 秘密参数换型 `SshSecretBundle`（隐式构造保调用点）

`SftpClient::connect`、`Tunnel` 构造、`push_public_key` 的 secret 形参从
`std::optional<std::string>` 换成 `SshSecretBundle`（带默认值、可由 `optional<string>` 隐式
构造，F4 先例）。旧调用点（CLI 测试、既有 itest、面板装配）**零改动编译**；新接线处传整套。
- 备档 B：追加独立 `hops` 形参。三处签名各多一根与档案跳数耦合的裸向量，bundle 本来就是为
  消这个耦合造的，不建议。

### D3 取料路线：逐腿定死，不新造凭据件

- **SFTP 面板**：`Hooks::resolve_secret` 换型返回 `conn::SshSecretBundle`（装配层直接交
  `ssh_secret_by_tab` 那一整格；标签不在档即回空 bundle）。面板内部 `Request` 的
  `secret` 字段随之换 bundle；worker 侧原样转交 `SftpClient::connect`。**不新增任何取料趟**
  ——材料在开标签那一刻就齐了（F5），重拨现取现格（口径同今天）。
- **隧道**：`launch_tunnel` 实体改为「`with_hop_secrets(ssh_profile, secret, ⇒ 建 Tunnel)」——
  0 跳同步回调，时序与今天一致；带跳时补齐回-call 落主线程再建对象。`Tunnel::secret_` 成员
  换型 bundle，每次重试在副本上消费（含 hops，口径同 `SshConnection`）。
- **密钥推送**：`submit_keys_push` 入口同样先过 `with_hop_secrets` 再投 worker；档案来自下拉
  的档案表，询问型那一支是「ask_secret 回-call ⇒ hop 补齐 ⇒ 提交」**两段异步串联**（§7 风险 3）。

### D4 带跳隧道的启动序自启

- **推荐 A**：放行——自启扫描对「目标可静默解析 ∧ 带跳」的隧道也拉起，逐跳材料经
  `with_hop_secrets` 后台补齐后建 `Tunnel`（补齐回-call 落主线程，`tunnel_entries` 由既有
  捕获引用续命）。启动序仍**零模态、零阻塞等待**：凭据库取料本来就是后台任务，钥匙环解锁
  弹窗是系统级行为，与开档案页（批 4 D8）同一口径。
- 备档 B：带跳一律记 `tunnel_skipped`，手动起再补齐。启动序形态零改动，但「带跳隧道开机即连」
  这一产品语义就没了，且 skipped 的行内提示文案（「需先存凭据」）会说谎——材料可能明明在库
  里。不建议。

### D5 失败档零新增

`SftpError` 不加跳板档（拨号侧一律 `Network`，hop_index 已在 dial 腿与链日志里）；
`TunnelError` 维持 `DialFailed`；`push_public_key` 对 `chain.open()` 的失败按
`failure().outcome` 映射进既有 `PushFailure`——`Network`/`Hop`→`DialNetwork`，
`HostKey`→`DialHostKey`，`Auth`→`DialAuth`（`HopUnwired` 理论不可达，兜底归 `DialNetwork`）。
措辞层不说谎（都是「连不上/谈不拢」），精确到第几跳看日志，口径与父稿 D4 一致。
- 备档 B：`SftpError::Hop` + `PushFailure::HopRefused` 新档 + 新词条。措辞层要为「第几跳」
  再开一档，得连带 i18n 键与面板分支——本批判据不要求，不建议。

### D6 链所有权与关闭序（逐腿）

- **SftpClient**：`std::optional<SshHopChain>` **成员**——链必须活到 `disconnect()`（中继线程
  搬的是整条会话的字节，不只拨号期）；`disconnect()` 与析构都在 `sftp_free`→`ssh_free` 之后
  reset 链。connect 失败就地收口（不留悬着的线程与句柄，链自己的语义）。
- **Tunnel**：`run_loop` 尝试体内**局部**，声明先于会话（退出序＝会话先散、链后收）；退避与
  重拨即整链重开（口径同终端腿 F3）。`stop()` 的 join 上界不变——链的 `close()` 也在 worker
  线程内完成，主线程只 join worker。
- **key_push**：调用框内**局部**，同序：exec 腿跑完 → `ssh_free` → 链收口（声明序保证）。
- 三腿的链都与消费者同 worker 线程（libssh 会话不跨线程，链头注既定）。

### D7 证人翻正与新增（`itest_posix_ssh_proxy_jump.cpp`，复用既有假服务器台）

- E6 案 `sftp_tunnel_and_key_push_legs_refuse_a_profile_with_jumps` **改写为逐腿正向**：
  ⑴ SFTP 经 1 跳 connect 成功且 `list_directory` 见到假服务器摆的文件；⑵ Tunnel 经 1 跳到
  `Active`（bound port 非 0）；⑶ `push_public_key` 经 1 跳回 `Done` 且假服务器侧
  authorized_keys 见到那一行。各腿各带一台跳板＋一台目标的拓扑，断言两台都
  `accepted_connections()>0`（拒拨案的反面判据就此翻正）。
- **保留** dial 层兜底案 `profile_with_jumps_without_seed_refuses_to_connect_directly`
  （D1 的退路口径）。
- 非跳回归＝三条腿既有 itest 不改动全绿（隐式网）；0 跳空链行为已有 `utest`/`itest` 证人。
- 变异自证（第 22 条）：①某腿不交 seed→该腿正向案红；②链在 `ssh_free` 前收口→断言窗口内
  红或崩即证；③D3 隧道 hop 补齐绕开（只传 target）→带跳正向案红。逐条做完逐字节还原。

## 4 接口形态（签名草案）

```cpp
// sftp_client.h
auto connect(const SshProfile &profile, SshSecretBundle bundle,
             const SshAlgorithmOverrides &algorithms = {}) -> bool;
private: std::optional<SshHopChain> hop_chain_;   // disconnect 里 ssh_free 之后 reset

// tunnel_client.h
Tunnel(const SshProfile &profile, SshSecretBundle bundle, TunnelSpec spec,
       RetryPolicy policy = {}, SshAlgorithmOverrides algorithms = {});
private: SshSecretBundle secret_;                 // 每次重试在副本上消费（含 hops）

// key_push.h
[[nodiscard]] auto push_public_key(const SshProfile &profile, SshSecretBundle bundle,
                                   std::string_view line,
                                   const SshAlgorithmOverrides &algorithms = {}) -> PushReport;

// sftp_panel.h Hooks
std::function<conn::SshSecretBundle()> resolve_secret;  // 空 bundle＝无材料；hops 随格交出

// main.cpp
launch_tunnel   = 【实体】with_hop_secrets(ssh, secret, ⇒ 建 Tunnel 并 start)
submit_keys_push = 【实体】with_hop_secrets(ssh, secret, ⇒ keys_worker.submit)
```

`SshSecretBundle` 的隐式构造（F4）让 CLI 冒烟、既有面板装配与旧测试调用点不改一行即编译。

## 5 线程、关闭序与真机走查

- 线程归属零变化：三条腿各自的 worker 线程（SFTP 面板 worker、隧道 worker、keys worker）
  持有链并跑 `open/close`；UI 线程只做装配与 `with_hop_secrets` 的发起与回-call。
- 关闭序表：每腿一行——`sftp_free/ssh_free`（或 exec 收尾+`ssh_free`）→ `chain.close()`；
  违序即 F10 的段错误面，由 D7 的变异②盯。
- 真机走查（规则 33，登记不称「可用」）：SFTP 面板对真跳板浏览+下载；带跳隧道自启+手动；
  密钥推送经真跳板落 authorized_keys。与既有三笔走查欠账并账。

## 6 验收判据

1. `ctest`（非 e2e）全绿 + runner 全绿，读数回写 PLAN §读数行；
2. D7 三个正向新证人绿、E6 拒拨断言不复存在、兜底案保留；
3. 非跳档案三条腿既有测试**未改动**全绿；
4. `grep` 全仓 `TODO(SPEC.` 只剩 `fake_ssh_server.cpp` 一处（CONN.02 Win32 腿）；
5. 裁决 7.153 + CHANGELOG v0.153 + 父稿 §7 批 6 行与 SPECIFICATIONS CONN.03 行回写；
6. 变异三刃各自见红并逐字节还原。

## 7 风险与未验证（如实登记）

1. **Windows 全程未验证**：`make_hop_pair` 的 Win32 loopback 腿未编译（父稿 D10），三腿接链
   在 MSVC 通道要等 Windows 机复核——与批 4/5 同一笔欠账，不新开。
2. 隧道重试持链：每次尝试一根链，退避前 `close()` 收口——`stop()` join 上界仍是「一个轮询拍
   + 一次建连超时」，链的逐跳建连超时叠加在**建连超时**里，最坏等待变长属预期（口径同终端腿）。
3. 询问型+带跳的推送是两段异步串联（ask 回-call ⇒ hop 补齐回-call）：`pending_push` 的生命
   周期与在途去重沿用现件；串联失败档（hop 补齐整趟取料失败）按「逐跳留空、拨号侧现形」，
   与 D8 既定一致。
4. SFTP 面板重拨吃标签留档的**现格**：标签若已用新材料重连，面板下次重拨即用新格——口径
   与今天取 `target` 一格完全一致，无新增驻留面。
5. 推断/未验证：无——本稿全部 F 项读码取证。

## 8 与本仓既有裁决的接缝

- 父稿裁决 7.148（终端腿五批结项）是本批的直接地基；D8 的 bundle 与 `with_hop_secrets` 形状
  原样消费，不改其语义。
- CONN.09（凭据不落明文）：hop 材料只在内存 bundle 与凭据库句柄之间流动，三腿均不留盘；
  「配置目录 grep 无明文凭据」自动化用例不受影响。
- 本批落地后 CONN.03 的 §4 清册功能棒清空，剩余欠账只有走查与 Windows 复核（均非代码棒）。

## 9 交付后记（2026-10-25，裁决 7.153；代码一笔 `2c72dfb`）

落地与稿面的差异逐条如实登记：

- **D7 证人档位订正**：稿面「SFTP 见 `list_directory` 文件、推送达 `Done`」的更强措辞在本沙箱**结构性不可达**——进程内假服务器没有 SFTP subsystem 处理也没有 exec 通道回调（实测：两类请求都被 libssh 默认拒绝，落 `SftpError::Protocol` 与 `PushFailure::ExecRefused`）。正向证人止于「已穿链拨到目标并停在各自档位」（隧道则真到 `Active` 并绑端口）；全腿成功腿并入真机走查欠账（带跳三条）。
- **D2「旧调用点零改动」有一处例外**：裸 `std::nullopt` 实参到 `SshSecretBundle` 需要两步用户定义转换、不成立；`itest_keys_push`／`itest_tunnel_failure` 三处调用点改传 `SshSecretBundle{}`。
- **D3 闭包捕获形态订正**：`launch_tunnel` 与 `submit_keys_push` 的异步回-call 里档案与 spec **按值入闭包**——调用方可能持 `store.settings()` 局部副本里的引用，原件在回-call 归时已不在（稿面按引用捕获的既有先例形态在异步腿上不安全）。
- **run_loop 尾部第二处秘密清空点**（`tunnel_client.cpp` 终态即清）不在稿面清单里，换型时一并改 `secret_ = SshSecretBundle{}`。
- **变异自证台账**：V1（SFTP 拨号不交 seed）⇒ `sftp_leg_dials_the_target_through_the_chain` 红（两断言：档位与目标认证计数）；V3（隧道建链绕开 `bundle.hops`）⇒ `tunnel_leg_reaches_active_over_the_chain` 红；**V2（链在 `ssh_free` 前收口）静默通过**——关闭序在测试侧没有断言窗口（seed 先关后 `ssh_disconnect` 写坏 fd 不炸不红），该序改由源码审查＋父稿 F12 探针担保，**不称证人**。三刃逐字节还原（cmp 无差）后全 suite 复绿。
- **结项读数**：ctest 126/126（套件数持平、无新用例文件）；runner 1532/1532（−1 旧 E6 案＋4 新案）；门禁 PASS（249 文件/540 键，零新词条）；代码侧在册 TODO 两处→一处（只剩 `fake_ssh_server.cpp` 的 CONN.02 Win32 腿）。
- **§6 验收判据逐条对照**：①三腿正向✓（档位档）②一跳板被拒⇒目标零✓③dial 兜底案保留✓④TODO 撤账 grep 只剩一处✓⑤读数全绿✓⑥带跳三条真机走查**未做** ⇒ 本条不称「可用」。
