# CONN.03 批 7 设计稿：逐跳凭据询问（AskEveryTime 的那一跳）+ quick connect 的 `-J` 语法

> 父稿：`codespec/CONN03_PROXYJUMP.draft.md`（§9 把这两条列为去向：逐跳询问 UI「随 CONN.09
> 的询问件一起排」、`-J` 语法「随 quick connect 后续扩展」）。PLAN §4 清册第二行在册至今。
> 批 6（裁决 7.153）已让四腿全走 `with_hop_secrets` 缝——本稿就把这条缝里「Ask 跳留空」的
> 最后一块降级面补成询问，并把 quick connect 接进跳链语法。
> 新裁决号：**7.154**（落地后回写）。
>
> **评审收口（2026-10-25）**：D1–D8 逐条经人裁「全部按推荐档」——D3 取 A（零新询问词条）、
> D7 保留新建 `itest_connection_sidebar` 套件、D8 自启判据扩到逐跳。备档一律否决，理由见各条正文。

## 0 需求与边界

- 需求：`SPEC.FEAT.CONN.03` §4 清册次行的两条延后子项——①逐跳凭据的询问 UI（`AskEveryTime`
  的那一跳），随 `SPEC.FEAT.CONN.09` 的询问件；②quick connect 的 `-J` 语法。
- 边界（本稿不碰）：跳板答案的回存（句柄与 vault 键要另起一棒，见 D4）；向导跳链行的逐跳
  认证方式配置（现文法只出 agent 跳）；`ProxyCommand`；Win32 腿；SSH 之外的询问面。

## 1 地基实测（F 表，全部读码取证）

- **F1 「Ask 跳留空」的在册口径**：`with_hop_secrets` 实现体注释明写「`Ask`／`None` 的那几跳
  留空：本期询问 UI 只覆盖目标口令，缺材料的跳板在拨号侧落 `Auth` 并带 hop_index」
  （`src/main.cpp` 2818–2823）；`plan_hop_credential`（`src/conn/profile_views.h`）同句。
  本稿要撤的就是这句「本期不做」。
- **F2 询问件形态**：`ui::CredentialPrompt` 是 **app 级单实例**——`Hooks{on_secret, on_cancel}`
  两出口、`ask(SecretAsk kind, profile_name)` 单口令输入框（掩码、明文即取即弃）、按窗搬浮层
  （`set_overlay_host`，WS.03 D6）。标题三 key `connections.credential.{password,passphrase,
  interactive}_title` 都只插 `{subject}` 一档（`src/ui/credential_prompt.cpp` 26–39）。
  装配层有**唯一漏斗** `ask_secret`（`src/main.cpp` 3006：先 `retarget_panel` 再 `ask`），
  `on_secret` 按 `pending_*` 五支认领（2936–2982）、`on_cancel` 全清（2983–3000）。
- **F3 缝的消费面**：`with_hop_secrets`（声明 2521、实现 2824）签名
  `(profile, optional<string> target_secret, done(bundle))`，消费者四处：`open_ssh_tab`（2645）、
  `launch_tunnel`（3216）、`submit_keys_push`、恢复泵（5346）。Stored 跳走后台一趟
  `retrieve`，Ask/None 跳留空槽位；`hops` 恒按跳数对齐（D5 分键纪律）。
- **F4 逐跳询问的先例形态**：恢复泵已有「挂起 + 放行」两台——目标询问的
  `restore_waiting_for_secret`/`restore_secret_sink`（5321–5340）与跳板取料的
  `restore_awaiting_hops`/`restore_hop_release`（5344–5365，一次性放行、取用即清）。
  逐跳串问就是把这个形态从「一次」推成「按档案序 k=0..n-1 的循环」。
- **F5 quick connect 现状**：`parse_quick_connect`（`src/conn/profile_views.cpp`）宽容口径——
  剥空白、尾冒号折 22、端口段非纯数字/越界**整段按主机名收（不猜）**；侧栏 `connect_quick`
  （`src/ui/connection_sidebar.cpp` 163）对 `nullopt`（空输入）**静默不连**；
  `quick_connect_profile` 的 id 带 `quick:` 前缀、缺省 agent 认证、不入库。
- **F6 语法层唯一主人**：`parse_proxy_jump_list`（`src/conn/proxy_jump.h`）——
  `[user@]host[:port]` 逗号串、上限 `kMaxProxyHops = 8`、任一枚坏 token **整串拒**、
  `none`＝显式空链路。`-J` 的跳表段必须走它，不得另造解析。
- **F7 侧栏留痕线**：`notice_`（`src/ui/connection_sidebar.h` 119「一次一句话，空＝无」）由
  `settings_label("connections.notice.imported", …)` 填充（cpp 178）、dim 色渲染（344–349）、
  重开页即清（68）。坏值反馈有现成载体。
- **F8 回存腿只认档案**：`remember_profile_secret`（2903）对 `profile_id` 空（quick/临时档案）
  或不在 `settings.profiles` 的整段跳过；vault 键一档案一枚（`credential_vault_id(prefix,
  profile_id)`），**逐跳没有键位**。
- **F9 Ask 跳从哪来**：`SshHop.credential` 逐跳句柄在册（`include/borealis/conn/profile.h` 124），
  `plan_hop_credential` 折成临时档案走与目标同一条 `plan_credential` 判据——「auth 需要材料 ∧
  （无句柄 ∨ `AskEveryTime` ∨ 库不可用）」⇒ `Ask`。向导跳链行只出 agent 跳，所以本档期
  Ask 跳的现实来源是**凭据库不可用时的口令型档案**（含手工/历史 JSON），不是新造形态。
- **F10 自启扫描只看目标**：启动序拉起的判据是「autostart ∧ **目标**可静默解析」，其余记
  `tunnel_skipped`——**逐跳的静默判据不在其中**（`tunnel_hooks.start` 的手动支同）。带跳档案
  若目标 agent、跳 Ask，自启会拉起后在缝里首次弹问——启动序出现模态，违 D4（批 6）立的
  「零模态」口径。这是 D8 的问题形态。

## 2 目标与非目标

目标：⑴带跳档案里 `plan.route == Ask` 的那几跳按档案序逐跳串问，答齐才拨、取消即整次作罢；
②quick connect 接受 ` -J <跳表>` 尾段，坏跳表就地留痕且不连、好跳表折进临时档案照常连接；
③非 Ask 跳与 0 跳档案、无 `-J` 输入的时序**逐帧不变**（既有用例一行不改即过，作隐式回归网）。

非目标：跳板答案回存（D4）；向导逐跳认证方式；多跳一表同问的新 widget；`-J` 的全 argv 文法。

## 3 决策（D 表，逐条请裁）

### D1 串问泵的位置：`with_hop_secrets` 实现体内，`done` 换 `optional<bundle>`

**推荐**：泵写进实现体（`src/main.cpp` 2824 那处），四个消费者**免费**获得逐跳询问——缝的
签名只把 `done` 的形参换成 `std::optional<SshSecretBundle>`（`nullopt`＝任一跳被取消，
整次取料作罢）。四处调用点各自处理取消：`open_ssh_tab` 静默返回（同目标询问取消的现语义）、
隧道手动支丢弃（同 `pending_tunnel` 取消）、恢复泵走既有 abort 支（中止余下重放，与
`restore_secret_sink(nullopt)` 同形）、`submit_keys_push` 不投。
**备档**：只接档案连接那一支——四腿口径又分叉，批 6 刚收的「同一条缝」白费。
**代价**：`done` 换型波及四调用点；泵在 main ⇒ 无自动化证人（7.83 句式在册，见 §5）。

### D2 询问时序：Stored 先取、Ask 后问，档案序 k=0..n-1 一题一题

**推荐**：维持现有「后台一趟取 Stored」在前；回程后对 `plan.route==Ask` 的跳按档案序逐跳
`ask_secret(kind, subject_k)`——上一答（或库取回）落进槽位才发下一问；全部齐了才 `done`。
目标若也是询问型，仍旧先问目标再进缝（现时序），**不合并成同框**。
**备档**：多输入框一表同问——新 widget、新布局、与单实例 `CredentialPrompt` 的按窗搬浮层
纪律冲突，否。
**代价**：n 跳最多 n 次弹问（上限 8）；每次弹问都在上一拍的回调栈外，无递归风险（先例＝恢复泵）。

### D3 问句文案：复用三枚 title key，`{subject}` 携带「user@host:port · 第 k/n 跳」

**推荐**：零新询问词条；subject 串由装配层拼（主机名/端口是动态数据不是字面量，不触门禁
CJK 闸）；`user` 空时省 `user@`；i18n 只有 §D6 那一枚新键。
**备档**：新增 `connections.credential.hop_*_title` 三 key——措辞可控性更强，但 540→543 且
两档同涨。
**代价**：「第 k/n 跳」的措辞进不了 ARB（拼在装配层，英文词表纪律下只能 ASCII 或复用现有
key 的模板位）——**注意**：装配层拼串若要中文「第 k/n 跳」须新 key 带 `{index}/{total}` 模板，
与「零新词条」矛盾。故本条实为两档：A＝subject 只放 `user@host:port`（跳序靠问句次序自然
表达，零新词条）；B＝新增一枚 `connections.credential.hop_suffix`（"第 {index}/{total} 跳"）
拼进 subject（+1 key ×2 档）。**推荐 A**，B 备档。

### D4 跳板答案回存：本期不做，入延后子项在册

**推荐**：答案只在内存 bundle 槽位里走完这一次连接（与 `CredentialPrompt`「本件不留副本」
同纪律）；`remember_profile_secret` 不动。逐跳 vault 键（一档案多键位）、乐观写回、
`find_credential_key` 审计面扩形是独立一棒，回写 PLAN §4 清册。
**备档**：本稿顺带做——违反最小意图 diff，否。
**代价**：库不可用环境下每次带跳连接都要逐跳重问（这本来就是 AskEveryTime 的语义）。

### D5 `-J` 文法：尾段 `-J`／`--jump` + 既有语法件，坏跳表整行拒

**推荐**：新纯逻辑件 `parse_quick_connect_line(text)`——以**最后一个**空白分隔的 `-J`/`--jump`
token 切两段：前段交既有 `parse_quick_connect`（宽容口径原样继承），后段交
`parse_proxy_jump_list`（上限/坏 token/`none` 全随语法层）；返回
`pair<optional<QuickConnect>, QuickJumpIssue>`，`Issue ∈ {None, BadJumpList}`，空输入＝无值
无问题（沿用静默）。`quick_connect_profile(host, port, jumps = {})` 加形参，折进
`profile.ssh.proxy_jumps`。`-J` 只认字面两形态、**大小写敏感**（「不猜」口径）。
**备档**：全 argv 形（`-J` 可在任意位、`user@` 端口逐跳继承规则）——文法面陡增，收益与
quick connect「输入即连」的轻量初衷相悖，否。
**代价**：`host -J gw 22.0.0.1` 这类把 `-J` 塞主机段中间的写法不支持（留痕可见，不是静默错）。

### D6 坏值反馈：复用侧栏 `notice_` 留痕线，新增一枚词条

**推荐**：`Issue==BadJumpList` ⇒ `notice_ = settings_label("connections.notice.quick_jump_bad")`
且不发起连接（一次一句话、重开页即清，全部沿用现机制）；空输入仍静默（现语义不动）。
**备档**：A＝向导式底部常驻红字——侧栏没有常驻红字布局，新件；B＝静默——「不连」不可见
比连错更糟，否。
**代价**：dim 色不是红，警示弱于向导先例；ARB **540→541**（两档各 +1 键，门禁 key 集等式照守）。

### D7 证人面：utest 扩案 + 新 `itest_connection_sidebar` 套件

**推荐**：纯逻辑腿（切分/折档案/拒串/退化兼容）扩 `utest_profile_views`；侧栏接线腿新建
`tests/integration/itest_connection_sidebar.cpp` 两案——`-J` 好值 ⇒ `hooks_.connect` 收到
`proxy_jumps` 形状正确且 id 带 `quick:` 前缀的档案；坏值 ⇒ `connect` 未被调且 notice 行非空
（`HeadlessSurface` 驱动，无 sshd 依赖）。串问泵腿在 main（7.83）无自动化证人，按既句式
源码审查＋真机走查担保；`with_hop_secrets` 的 `done` 换型由编译器钉调用点。
**备档**：侧栏不配 itest（只靠 utest 纯逻辑）——第 18 条要求新公共逻辑接 CTest，切分件已有
utest，但 `connect_quick` 的「不连+留痕」是行为承诺，值得线上证人。
**代价**：ctest 注册 +1 套件（126→127 全绿、注册 130→131）。

### D8 带跳隧道的自启判据扩到逐跳（修 F10 的模态泄漏）

**推荐**：启动扫描的手动/自启两支共用「目标 ∧ **每一跳**都可静默解析」才放行；任一跳
`route==Ask` ⇒ 记 `tunnel_skipped` 行内提示、**不弹问**——启动序零模态（批 6 D4 A 档口径的
补全：那次只审了目标）。手动启动不受影响（本就走 `ask_secret`，人在等框）。
**备档**：自启也允许串问（缝里弹）——开机连跳 n 个模态框糊脸，违背「自启路径绝不弹框」
的在册纪律（CONN.08 D7），否。
**代价**：`with_hop_secrets` 需要一个「只问/不问」的闸参数（或泵前预检后不问的快查函数），
四处调用点里只有自启支置「不问」。

## 4 接口形态（签名草案）

```cpp
// src/conn/profile_views.h —— quick connect 的 -J 腿（纯逻辑）
struct QuickConnect {
    std::string host;
    int port{22};
    std::vector<SshHop> jumps{};      // 空＝直连；语法层唯一主人的折算结果
};
enum class QuickJumpIssue : std::uint8_t { None, BadJumpList };
[[nodiscard]] auto parse_quick_connect_line(std::string_view text)
    -> std::pair<std::optional<QuickConnect>, QuickJumpIssue>;
[[nodiscard]] auto quick_connect_profile(std::string host, int port,
                                         std::vector<SshHop> jumps = {}) -> Profile;

// src/main.cpp —— 缝的取消语义（D1）
auto with_hop_secrets = std::function<void(
    const conn::SshProfile &, std::optional<std::string>,
    std::function<void(std::optional<conn::SshSecretBundle>)>)>{};
// 泵前快查（D8）：任一 Ask ⇒ false（纯逻辑，复用 plan_hop_credential，可进 utest）
[[nodiscard]] auto hop_secrets_silent(const conn::SshProfile &, const conn::CredentialStore &) -> bool;

// src/ui/connection_sidebar.cpp —— connect_quick 改走 parse_quick_connect_line（D6 留痕）
```

## 5 线程、关闭序与真机走查

- 串问泵全在主线程回调链上（`ask_secret` 漏斗 → `on_secret` → 下一问），不新增线程；
  Stored 取料的后台腿维持原样。取消＝整次作废，无半途拨号可关——关闭序无新面。
- 真机走查登记 **3 条**（本批新增）：①库不可用（或 AskEveryTime 档）带跳档案连接 ⇒ 逐跳
  串问、答齐连通、中途取消不拨；②quick connect ` -J` 好值连通形状正确；③坏值留痕不连。
  Windows 腿照例未验证在册。

## 6 验收判据

1. 带跳档案含 Ask 跳：逐跳串问按档案序、一题一答；任一取消 ⇒ 整次不发起连接（两台服务器
   零连接的形态无 sshd 不可自动化，走查 ① 担保；日志见 hop 序）。
2. 无 Ask 跳（含 0 跳）：`with_hop_secrets` 时序逐帧不变——既有全部 itest 一行不改即过。
3. quick connect：`host -J gw1,gw2:2222` ⇒ 临时档案 `proxy_jumps` 两枚、端口折法随语法层；
   坏跳表 ⇒ 留痕一句话 + 不连；`-j`（小写）按主机段宽容收（不猜）；无 `-J` 输入行为不变。
4. 自启带跳 ∧ 任一跳 Ask ⇒ `tunnel_skipped`、启动序零弹框（D8）。
5. ARB 两档各 +1 键（541），门禁 key 集等式 PASS；`hop_secrets_silent` 配 utest 案。
6. 读数预期：ctest **127/127**（注册 131）、runner 相应增量、走查 3 条入册 ⇒ 本条不称「可用」。

## 7 风险与未验证（如实登记）

- 串问泵在 main（7.83）无自动化证人——与批 4/6 同款边界；变异自证只能打到纯逻辑两刃与
  侧栏两案，泵本体锚在审查。
- 逐跳询问与多窗口「搬窗」串联：每问都过 `ask_secret` 漏斗即自带 retarget，风险低但走查
  ①须跨两扇窗各看一眼。
- `done` 换 `optional` 的调用点复核：恢复泵把「无材料」与「取消」两义都收敛到 abort 支，
  语义合并是否吞掉「取料失败继续空 hops」的现档——落地时逐点核对，必要时区分两出口。
- 无 Win32 腿实测；真机走查 3 条未做前不得称「可用」（第 33 条）。

## 8 与本仓既有裁决的接缝

- CONN.09 询问件（7.93/7.95）：本稿是「每次询问」降级腿从目标扩到逐跳，弹问纪律
  （掩码、即取即弃、按窗搬浮层）原样继承；回存不做（D4）与该条「句柄乐观写回」的档案级
  键位设计互不侵犯。
- 批 6 裁决 7.153：四腿共用 `with_hop_secrets` 是本稿 D1 的立足点；`done` 换型是批 6 签名
  的一次受控演进，编译器钉死全部调用点。
- 语法层唯一主人（7.148）：`-J` 段零新解析，全部折算交 `parse_proxy_jump_list`。
- i18n（7.150）：新键只加 `connections.notice.quick_jump_bad` 一枚（D3 选 A 则询问措辞零新增），
  ARB 单一真值源由门禁守。
- 7.83：main 装配腿无证人的担保句式沿用。

## 9 交付后记（2026-10-26，裁决 7.154；代码两笔 `5b2bd6f` → `1951082`）

- **D1 落地形态**：泵为 `main` 里的单槽 `HopAskSession`（queue/kinds/subjects/bundle/done），
  声明先于 `prompt_hooks`；`on_secret` 最先认领泵（与目标腿各 pending 互斥成立——泵在途时
  不可能同时挂着别的问句），`on_cancel` 先 `done(nullopt)` 再清其余 pending。新串问覆盖旧串问
  的形态与 `pending_profile` 覆盖先例同纪律：旧那次连接静默作罢（单实例漏斗下没有第二个现场）。
- **§7 第 3 条风险复核结论**：两出口未合并——「Stored 取料失败」仍交回 `value(bundle)`
  （hops 留空、拨号侧现形），只有「任一询问跳取消」才交 `nullopt`；恢复泵把 `nullopt` 收敛到
  abort 支、把空 hops 的 value 照常放行建格，与现档语义一致。
- **在册偏差（登记为语义而非缺陷）**：`Stored` 取回空**不再二次询问**——兜底询问是目标腿的语义
  （句柄在档＝用户选过「不回存问」那一档），该跳落 `Auth` 带 `hop_index`。
- **泵与搬窗**：每一问都过 `ask_secret` 唯一漏斗 ⇒ retarget 自带；恢复泵的目标腿按窗
  `retarget_panel_to` 那支不变，跳问发生在重放中时落当前窗（开机重放单窗形态下无观察歧义，
  走查 ①跨窗看一眼即可证伪）。
- **变异台账（第 22 条）**：批 A 三刃（切分段序颠倒／marker 识别改子串／跳链不折进档案）各有
  指名红；批 B 三刃 V1（撤 `notice_` 赋值）⇒ `bad_jump_list_refuses_and_paints_notice_line` 红、
  V2（坏值照连）⇒ 同案红、V3（跳链丢件）⇒ `quick_line_with_jump_tail_connects_as_temporary_profile`
  红；逐字节还原（md5 核验）后复绿。**泵本体与自启闸在 `src/main.cpp`，无自动化证人**（7.83
  句式：源码审查＋真机走查担保），`done` 换型由编译器钉死四调用点。
- **判据对照（§6）**：①–⑤ 机器侧全落（ctest **127/127**、runner **1538/1538**、ARB **541** 键
  门禁 PASS）；①带跳串问、③`-J` 即连、④自启闸三条真机走查**一条未做** ⇒ 本条**不称「可用」**
  （走查欠账并入父稿 `CONN03_PROXYJUMP.draft.md` 的走查清册）。
