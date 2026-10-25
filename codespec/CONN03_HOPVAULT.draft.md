# CONN.03 批 8 设计稿 — 跳板答案回存（逐跳 vault 键）

> 需求标识：`SPEC.FEAT.CONN.03`（回存子项）＋ `SPEC.FEAT.CONN.09`（凭据安全存储的口径主人）。
> 来源：裁决 **7.154**④「跳板答案只在内存槽位里走完这一次连接……逐跳 vault 键（一档案多键位＋乐观写回＋审计扩形）另起一棒，入 PLAN §4 清册在册」；`PLAN.md` §4 清册该行落期「M4 之后，随 `SPEC.FEAT.CONN.09`」。
> 前序：批 5＝`codespec/CONN03_PROXYJUMP.draft.md`（跳链语法层与档案字段），批 6＝`CONN03_PROXYJUMP_LEGS.draft.md`（三腿接进跳链），批 7＝`CONN03_ASKHOP_QUICKJ.draft.md`（串问泵与 `-J`）。本稿只写**回存**这一件事。
> 状态：**已落地（2026-10-26，裁决 7.155）**——D1~D8 按本稿推荐档自裁执行（AGENTS.md §4.1 第 2 条的常存授权），落地读数与两处口径订正见 §5。

---

## 0 事实地基（逐条读源，2026-10-26）

| # | 事实 | 出处 |
|:---|:---|:---|
| F1 | 每一跳**已有自己的凭据句柄位**：`SshHop::credential`（`SecretHandle`，与目标同型）。取料侧早已消费它——`plan_hop_credential` 把跳折成临时档案走 `plan_credential` 同一条口径，`Stored` 即去库取。 | `include/borealis/conn/profile.h` SshHop；`src/conn/profile_views.{h,cpp}` |
| F2 | 跳的 `credential` **序列化往返已在**：`ssh_hop_to_json` 整写 `credential_to_json`（含 `vault_id`），导出腿已把跳句柄剥成询问哨兵（`portable_credential_to_json`）。⇒ 配置层零 schema 改动；本棒要缺的只是「有谁把 Reference 写进去」。 | `src/config/store.cpp`（ssh_hop_to_json / CredentialProjection） |
| F3 | 句柄编码是「前缀 + 档案 id」单段：`credential_vault_id(prefix, profile_id)`；`retrieve`/`erase` 把解码出的整段当 OS 库的查询键（libsecret 的 `profile_id` 属性值 / CredMan 的 `borealis/<id>` 目标名）。⇒ **整段可当不透明键位用**：复合键无需改平台两腿与逆解析。 | `src/platform/credential_store.{h,cpp}`；`src/platform/{posix,win}/credential_store.cpp` |
| F4 | 串问泵答完一个 Ask 跳只写内存槽位（`hop_ask->bundle.hops[index]`），`remember_profile_secret` 只认目标档案 id；明文即弃（批 7 D4 的在册代价）。泵手里有 `SshProfile` 形参但**没有档案 id**——回存需要装配层把 id 带进来（四个调用点都在 `src/main.cpp`，那里都有 `tab_profile_id`/`profile.id` 可取）。 | `src/main.cpp`（with_hop_secrets / HopAskSession / on_secret） |
| F5 | 目标腿回存先例（CONN.09 D6）：提交后**先乐观写句柄落盘**（`store.replace(next)`），再 `au::async` 异步入库；入库失败不回滚句柄——下次 `retrieve` 落空自然回询问。失败只 WARN 留痕。 | `src/main.cpp` remember_profile_secret |
| F6 | 跳链唯一规范化点 `render_proxy_jump_list` 逐项输出 `user@host:port`，但**user 为空时省略 `user@`**（与批 7 问句 `{subject}` 同口径）⇒ `host:22` 这一个串既是「无名用户」也是「名为 host 的用户」，直接拿它当键位会撞键。 | `src/conn/proxy_jump.cpp` render_proxy_jump_list |
| F7 | 自启闸判据「目标 ∧ 每一跳都可静默」按 `plan_hop_credential` 现读——跳一旦有了 `Reference` 且库可用即算 `Stored`＝静默。**回存落地⇒带跳档案下一程可进开机自启**，这是本棒的连带行为变化（批 7 D8 侧的同一判据，零新代码）。 | `src/conn/profile_views.cpp` hop_secrets_silent；`src/main.cpp` 自启扫描 |
| F8 | 审计面现状：`find_credential_key` + `kBannedKeyNames` 对配置目录三枚 `Preferences` 全文件双查（键名 + 明文哨兵值）并自证非空转（`utest_credential_audit`）；`vault_id` 是不透明引用、不是禁键。逐跳键形态同 F3（整段仍是引用串）⇒ **审计面天然覆盖新键位**，扩形＝补用例而非补规则。 | `src/config/store.cpp`；`tests/unit/utest_credential_audit.cpp` |
| F9 | quick connect 折出的跳一律 agent 认证（批 7 D5），不触发逐跳询问；其档案 id 带 `quick:` 前缀且**不入 `settings.profiles`** ⇒ `remember_profile_secret` 式「档案不在档即整段跳过」的闸天然挡住它。 | `src/conn/profile_views.cpp` quick_connect_profile |
| F10 | `restart_ssh_tab` 的 bundle 只继承**留档里已有的** `hops`（`ssh_secret_by_tab`），不重走缝；重启前那趟重拨若经询问链拿到答案，当前不回存任何一跳（`on_secret` 的重启支只调 `remember_profile_secret` 目标腿）。 | `src/main.cpp` restart_ssh_tab / on_secret |

**推断/未验证**：本稿无新实测——F1~F10 全部为读源结论；判据执行走既有 Linux 通道（Win32/MSVC 自 2026-10-08 未复核，同批 4/5/6/7 欠）。

---

## 1 决策项（D1~D8，每条给推荐档与代价）

### D1 键位形态：逐跳 vault 键怎么编码

- **① 推荐——按规范化串形复合，档案级键不动**：`hop_key = <profile_id> + "#" + <token>`，`token`＝F6 那一项的规范化串形且**user 为空时保留前导 `@`**（`@host:port`，消除 F6 撞键）；整段经 F3 的不透明通道交给库。档案级键仍是裸 `<profile_id>` ⇒ 两型键位同库并存、逆解析与平台两腿零改动。
  判据：同一档案内改链序/删中间跳**不掉已存答案**（键随身份走，不随下标走）；不同档案各用各的键（不给「全机共享 gw 口令」留暗道，见②）。
- ② 备选（仅身份、跨档案共享）：`token` 单独作全局键位——同一台跳板只问一次。隐私面更强（口令按主机归属），但它把「档案边界」当不存在，且与 F3 的 `profile_id` 查询面混居，清理语义复杂。
- ③ 备选（档案 + 下标）：`<profile_id>#<k>`——最省事，但链一编辑（插/删跳）答案集体错挂，不可接受。

**代价（①）**：向导那一行以串形为唯一编辑面（批 5 口径），保存即整体替换 `proxy_jumps` ⇒ `credential` 随串形匹配保留；串形里改 `user@` 前缀＝换身份＝旧答案作废重问，这是刻意的（口令属于那个 `user@host:port`）。

### D2 问句与答话的对应：泵内槽位带键位

`HopAskSession` 的并行数组加一列 `keys`（与 `queue`/`kinds`/`subjects` 同序），答落槽位时同批记下「这一答该回存到哪个键位」；`with_hop_secrets` 形参加档案 id（空串＝临时档案，跳过回存，见 D5）。泵本体仍零落盘动作——它只**转交**答话与键位，落盘在 `on_secret` 出口侧（问句漏斗与回存闸分工不变，同批 7 D1 的「泵只搬运」纪律）。

### D3 回存时机与失败档：**答齐一批、一次落盘**（推荐）vs 逐答即存

- **推荐**：串问会话 `finish(filled)` 放行时（即这一次连接材料齐了），把本串**答过的**那几跳（`Ask` 答话；`Stored` 取回跳不在内，它们已有句柄）一次写回：同一次 `store.settings()` 副本内改多个 `hop.credential` → 一次 `store.replace` → 每键一枚 `au::async` 入库。取消整串（`nullopt`）⇒ 一个都不存——半截链的口令不回存，与批 7 D1「取消任一跳整次作罢」同语义。
- 逐答即存的差别只在「答到第 k 题时崩了/锁死了前 k-1 题存不存」；选一批的根据：落盘次数少（一条链一次 `replace`）、且「整次作罢＝整次不存」口径最干净。
- **代价（登记在册）**：乐观写句柄先于入库（F5 同档），**答完即关程序**会留下有句柄无秘密的空挂——下一程 `Stored` 取空不二次询问（批 7 D2 在册偏差），该跳落 `Auth` 带 `hop_index`。目标腿今天就有同一档代价，本棒不为其另造机制。

### D4 审计面扩形（裁决 7.154④ 点名的第三件）

规则零改动（F8：`find_credential_key` 只扫 JSON **键名**，`vault_id` 是值，复合键天然过闸）。落地只补①；②复核现状后**不补新案**（口径订正，见 §5②）：

- ①`utest_credential_audit` 的 `build_production_artifacts` 给 `audit-ssh` 挂两枚带复合键句柄的跳（有名/无名各一），主案断言配置文件全文出现该复合键，且禁键名与明文哨兵双查照旧为空——扩形是「不误报、不回归」的证人。
- ②~~`utest_config` 补一条带 hop `credential` Reference 的配置往返逐字段全等（此前无证人）~~：**订正**——`utest_config::non_default()` 早已给第一跳带 `SecretHandle::reference("vault://demo-hop-one")`，回滚案的 `store.settings() == first` 与导入导出往返案都在逐字段比较里覆盖了跳句柄，「此前无证人」是稿面误判。

### D5 不回存的三档闸（复用既有闸，不新造判据）

①档案不在 `settings.profiles`（quick connect、已删档案）——`remember_*` 式现查找不到即整段跳过（F9）；②库不可用（`available()==false`）同⑤目标腿现闸；③`auth_method` 不需要材料的跳（agent／未识别回落 ⇒ `plan.route==None`）根本不进询问队列，无从回存。

### D6 档案删除时的库清理

**推荐本期不做**：删档案时 `settings.profiles` 记录消失⇒全仓再无任何句柄引用那些键位（句柄只存在于档案与其跳里），取用面结构不可达；代价＝OS 库里留孤儿条目（钥匙环界面可见），与目标腿**现役行为逐字相同**（CONN.09 自陈「无清理入口」在册）。另造「按档案 id 前缀清库」需要枚举面（libsecret search / CredReadList），本棒不扩平台 API。

### D7 重拨/重启腿（F10）

**推荐本期不做**：重启与断线重拨沿用「留档 bundle 的 hops」现语义；只有新开连接的四条缝调用点（开标签、隧道拉起、密钥推送、启动重放）带档案 id 并回存。理由：`on_secret` 的重启支手上只有 `TabId + SshProfile`，加回存要先补一跳「答案→键位」的记账，收益（省一次重问）与改动面不成比——且重启前用户已在同一会话里答过一次，材料本来就在留档 bundle 里。在册为可观察代价。

### D8 证人分布与变异自证（按 §4.4 第 18/22 条）

- 纯逻辑新件（键位折算，见 §2）→ 新 `utest` 套件：无名用户补 `@`、有名与无名不撞键、含 NUL 拒编（回空串＝调用方跳存）、同输入同键（幂等）、档案 id 空拒编。
- `itest_config`/`utest_config`：D4②往返案。
- `utest_credential_audit`：D4①扩形案。
- 回存决策折成纯逻辑件 `plan_hop_writeback`：`utest_hop_vault` 覆盖「键位对得上且仍 Ask 的跳才写」「Stored 跳不覆写」「链在询问期间被改（host／port 换身份）不写」「越界下标／空键位／无答案跳过」「非 SSH 与空 id 整段回空」「计划件不改档案」。**`src/main.cpp` 不进 runner（裁决 7.83）**——泵槽位与「置句柄 → replace → 入库」三步搬运仍只有源码审查＋真机走查；但「写哪几跳」这条核心判据自此有指名证人。
- 变异自证四刃（全部打在可执行件上）：V a 撤空用户补 `@`（身份键案红）；V b 撤 `plan_hop_writeback` 的「仍 Ask」闸（`Stored` 跳被覆写案红）；V c 撤键位复核（询问期间改链案红）；V d 撤非 SSH 类型闸（Local 残料案红）。逐字节还原复绿。**取消整串不回存**是 `on_cancel` 的控制流，无自动化证人；与泵槽位、「置句柄 → replace → 入库」三步搬运一并归源码审查＋真机走查（裁决 7.83 句式）。

---

## 2 落点清单（裁后开工次序）

| 批 | 内容 | 文件 |
|:---|:---|:---|
| 批 1（纯逻辑） | `hop_vault_key(profile_id, hop)` 键位折算件（含补 `@` 与拒编档）；`plan_hop_vault_keys(ssh, store, profile_id)`——按 `plan_hop_credential` 同口径筛出「本次会走 Ask 的跳」及其键位（泵槽位与回存共用同一张表，两处不各算各的）；`plan_hop_writeback(profile, store, answered, hop_secrets)`——答齐后「该写哪几跳」的唯一判据（键位复核、仍 Ask、有明文，返回下标/键位/标签/明文），把原本只能留在 `main.cpp` 的决策做成可单测件。三件一并挂 `src/conn/profile_views.{h,cpp}`（它已是凭据计划的唯一主人）。`utest` 新案。 | `src/conn/…` + `tests/unit/…` |
| 批 2（装配） | `HopAskSession` 加 `keys` 列；`with_hop_secrets` 形参加档案 id（四调用点各交自家的 `profile.id`/`tab_profile_id`）；`finish(filled)` 放行处新增 `remember_hop_secrets(profile_id, 答过的 (index, key))`——调用 `plan_hop_writeback` 后只做三步搬运（置句柄 → 一次 `replace` → 逐键 async）。 | `src/main.cpp` |
| 批 3（审计与回写） | D4 两案 + 文档回写（SPECIFICATIONS §7 新裁决 + CHANGELOG v0.155 + PLAN §4 清册该行结项 + `profile_views.h` 那句「本期不回存」注释改口）。 | `tests/…` + `codespec/…` |

**无新 UI、无新设置键、无新词条**（询问件复用批 7 的 `{subject}` 现文案；label 取该跳规范化串形，属动态数据不触词条闸）。

---

## 3 验收判据（结项时逐条对账）

1. 带两条 Ask 跳的档案首程连上后，`settings.profiles` 里那两跳 `credential` 为 `Reference`，键位逐字节＝D1① 折算；配置文件全文无明文（审计用例守）。
2. 二程同档案：`hop_secrets_silent` 现读为真 ⇒ 若该档案在自启射程则开机静默拉起（F7 连带档，真机走查判据）；手动连接零重问。
3. 链序调整（中间插一跳）：原两跳答案不丢（D1① 判据）；新插的 Ask 跳单独补问一次。
4. 取消任一跳：整次作罢且一个键位都不写（D3）。
5. `ctest -E etest_` 全绿、runner 全绿、门禁双闸 PASS；四刃变异各有指名红（D8）。
6. 真机走查在册项（§4.4 第 33 条，未做不称「可用」）：①首程串问两跳→二程直连不重问；②钥匙环/CredMan 界面里看到逐跳条目；③删档案后孤儿条目仍在（D6 代价显形）；④Win32/MSVC 腿编译与实测（欠账同前批）。

---

## 4 在册不做（留档）

跨档案共享口令（D1②）、档案删除清库（D6）、重拨腿回存（D7）、`~/.ssh/config` 导入跳板的回存差异（导入档案走同一闸，无特例）、设置面板展示「哪些跳已存口令」（无需求原文）。

---

## 5 落地读数与口径订正（2026-10-26，裁决 7.155）

### 5.1 三批落地形态

批 1 三件与批 2 装配按 §2 原样落地，无形态改动；批 2 的一处事实修正：`with_hop_secrets` 的**恢复腿**调用点形参是 `const SshProfile *`（`connect_profile_to` 的档案有时是栈上快照），首版误写 `profile->id` 编不过，改交 `item.profile_id`——恰是 D5① 要的口径（装配层先按 id 回查 `settings.profiles`，quick connect 与已删档案在那一跳跳过回存）。批 2 的三步搬运落成 `src/main.cpp` 的 `remember_hop_secrets`：档案回查 → `plan_hop_writeback` → 逐条折算 `credential_vault_id(prefix, key)` 置 `Reference` → 一次 `store.replace` → 逐键 `au::async` 入库。

### 5.2 判据读数

- 构建：MSVC（VS 2026）+ Ninja 主通道，`cmake --build --preset msvc --parallel` 通过（`main.cpp` 触 COFF 分节上限 C1128 ⇒ `borealis` 目标补 `/bigobj`，见 §5.3②）。
- `ctest --preset msvc -E etest_` **118/118 全绿**（Windows 腿无 9 个 POSIX 专属套件）。
- 无头 runner **1471 案 / 1468 绿**：三红全是 `etest_libsecret_credential_store`（Windows 无 secret-service 真后端，该套件属 e2e 真后端面，不入 `-E etest_` 基线）——本棒未触碰该文件。
- 门禁：`check_agents_size`、`check_display_literals`（作为 ctest 两个用例）随全量复跑 PASS；本棒零新词条、零新设置键。
- 变异自证四刃（打在 `src/conn/profile_views.cpp`，逐字节还原后 md5 复核复绿）：
  - V a 撤空用户补 `@` ⇒ `hop_vault_key_is_addressed_by_identity_not_by_index` 红（1/7）；
  - V b 撤「当前仍 Ask」闸 ⇒ `hop_writeback_takes_still_ask_hops_with_matching_keys` 红（1/7）；
  - V c 撤键位复核 ⇒ `hop_writeback_refuses_a_chain_edited_while_asking` 红（1/7）；
  - V d 撤非 SSH 类型闸 ⇒ `hop_writeback_refuses_non_ssh_and_unanswerable_entries` 红（1/7）。
- 落地期抓到并修掉的一处**证人空转**：D8 原案的 `Stored` 跳条目在 `hop_secrets` 里给的是 `nullopt`，撤掉 Ask 闸后该条目先在「有明文」闸被拦下，V b 实测**不红**。补法是给 `stored` 那格塞一枚明文（`"pw-stored"`），让 Ask 闸成为该跳唯一的拦截点——之后 V b 转红。这正是第 22 条「门禁须以变异自证非空转」的反向应用：证人本身也被变异验过一遍。
- 审计扩形（D4①）：`utest_credential_audit` 的 `build_production_artifacts` 给 `audit-ssh` 挂两枚带复合键句柄的跳（有名 `operator@bastion.example.invalid:22` 与无名 `bastion2.example.invalid:2200`），主案断言配置文件全文出现该复合键、禁键名与明文哨兵双查照旧为空；规则零改动（F8 复核属实：`find_credential_key` 只扫 JSON 键名，`vault_id` 是值）。
- **无自动化证人的在册面**（裁决 7.83 句式）：`src/main.cpp` 的泵 `keys`/`answered` 槽位、`remember_hop_secrets` 三步搬运、`on_cancel` 的「取消整串不回存」控制流——均不编入 runner，由源码审查＋真机走查担保。

### 5.3 两处口径订正

① **D4② 是稿面误判，已就地划除**（见 §1 D4② 删除线）：`utest_config::non_default()` 早已给第一跳带 `SecretHandle::reference("vault://demo-hop-one")`，回滚案与导入导出往返案都在逐字段比较里覆盖跳句柄，「此前无证人」不成立，批 3 **不补新案**。

② **§0 的「判据执行走既有 Linux 通道」被本棒现实取代**：本棒全程在 Windows/MSVC 腿执行（同一工作机的主通道），这是该腿自 2026-10-08 以来**第一次完整编译**，因此掺入了与回存无关但必须同批修掉的跨平台欠账——Win32 腿首次编译的 4 处硬错误（`crash_handler.cpp` 的 `const HANDLE` 取址类型、`tcp.cpp` 的 `addrinfoA` 标签名与 `const u_long` 取址、`serial.cpp` 的 `std::milliseconds` 拼写、`src/CMakeLists.txt` 为 `main.cpp` 触顶的 C1128 补 `/bigobj`）与 4 处 Windows 专属行为差（`codec.cpp` 的 GBK→CP936——libiconv 的 GBK 拒收 0x80 而 glibc 把 CP936 当 GBK 别名；`tcp.cpp` 监听改 `SO_EXCLUSIVEADDRUSE`——Win32 的 `SO_REUSEADDR` 允许二活监听；`utest_workspace_snapshot.cpp` 的 `path::string_type` 宽窄差；`itest_keys_panel.cpp` 的空钩子会弹真对话框、须 `interactive=false` 走取消腿），另有两处 Windows 通道读数订正（`itest_tunnel_failure` 的空端口拨号单次耗时 10 s ⇒ 等待预算 60 s；会话日志测试的「删开着的文件」在 Win32 不成立 ⇒ 改以只读权限注入写失败）。这些改动与回存同棒，但**意图独立**，按 §4.6 第 26 条单独成笔（裁决 7.156）；Win32 腿的这次「重新启用」读数与之同批入册。


