# SSH 密钥管理器 UI 设计稿（`SPEC.FEAT.CONN.10`）

> **状态**：**待评审**（2026-10-10 出稿）。§3 的 D1–D10 逐条给推荐档与代价，**本稿零 `src/` 代码改动**；经人逐条裁决后并入 `codespec/SPECIFICATIONS.md` §7 新裁决 + `CHANGELOG.md` 记版本 + `PLAN.md` 的 CONN.10 行回写，然后按 §4 布局落 `src/`。
> **配套高保真草图**：`codespec/UI_KEYS.draft.svg`（屏 7）/ `.png`；底部 1–6 标号判据对应本稿 §2，SVG 为事实来源。
> **已落地的前置**：`SPEC.FEAT.CONN.02` 传输腿与 `conn/ssh_dial`（裁决 7.94）、`CONN.03` 档案与 `ProfileStore`、`CONN.09` 的 `SecretHandle`/询问件（裁决 7.93，配置目录审计的自动化证人已在 `utest_config`）、`CONN.04` 的 worker 串行队列先例（裁决 7.95）、`CONN.08` 的左停靠卡片面板与「装配层持有运行态」形态（裁决 7.97/7.98）。
> **本条是 M3 主干余下唯一未开工的需求条目**（`PLAN.md` §4 交接清册 §4.1 所列 CONN.08 真转 etest / WS.05 etest 都要 sshd 环境、TERM.09 提示卡与各 UI 件走查都归人工门槛）。

## 0 本稿的实测地基（libssh 0.12.0 + vcpkg OpenSSL，本机 Linux 腿，2026-10-10 一次性探针，探针件已删）

| # | 实测事实 | 读数 | 对本稿的影响 |
|---|---|---|---|
| F1 | 生成入口的非弃用形态是 `ssh_pki_generate_key(type, ssh_pki_ctx, &key)`；`ssh_pki_generate` 在 0.12 已带 `SSH_DEPRECATED` | ed25519 / rsa 各档 rc=0 | 走 ctx 形态，不引 7.96④ 之外的第二处弃用抑制 |
| F2 | RSA 位数经 `ssh_pki_options`：`ssh_pki_ctx_options_set(ctx, SSH_PKI_OPTION_RSA_KEY_SIZE, &bits)` | rc=0 | D4 只需一枚位数下拉，无第二真值源 |
| F3 | 生成耗时（`-O0` 本机，量级参考） | ed25519 <1 ms；RSA-2048 72 ms；3072 167 ms；4096 369 ms | RSA 是百毫秒级长计算 ⇒ **必须出 UI 线程**（AGENTS §4.5 第 25 条），D4① |
| F4 | 带口令落盘：`ssh_pki_export_privkey_file_format(key, "pp", nullptr, nullptr, path, SSH_FILE_FORMAT_OPENSSH)` | rc=0，文件头 `-----BEGIN OPENSSH PRIVATE KEY-----`，内层 `aes128-cbc` + `bcrypt` | 口令可直接实参传入，**不需要 `ssh_auth_callback`** ⇒ 明文不必进 C 回调栈 |
| F5 | **该函数对已存在路径静默改写**（无 O_EXCL 语义）；无口令走同一函数（passphrase 传 `nullptr`） | 覆写实测成功 | 「不覆盖既有私钥」这道闸只能本仓做，D5① |
| F6 | **落盘模式受 umask 支配** | 本机 umask 002 ⇒ 私钥落成 `-rw-rw-r--`（0664） | 私钥权限必须本仓收，D5②；OpenSSH 对可被同组写的私钥会拒用 |
| F7 | `ssh_pki_export_pubkey_file` 写 `<type> <base64> user@host\n`——注释段由 libssh 自取 | 本机读数尾段 `polaris@ubuntu` | 要用户自填注释就得本仓自写 `.pub`（一行三字段），D4③ |
| F8 | 加密态判定：对带口令私钥调 `ssh_pki_import_privkey_file(path, nullptr, nullptr, nullptr, &k)` | 回 -1，**0 ms**（不跑 KDF）；带正确口令导入回 0、45 ms | 一次调用即可判「有没有口令」且代价可忽略 ⇒ D3① 逐把探测成立；**本稿不逐把解密**（45 ms/把且属凭据敏感操作） |
| F9 | 指纹：`ssh_get_publickey_hash(SSH_PUBLICKEY_HASH_SHA256)` + `ssh_get_fingerprint_hash` | `SHA256:` + 43 字符 base64，两型皆有；释放走 `ssh_clean_pubkey_hash` ＋ `free` | 列表次行的指纹串有唯一来源，不自算哈希 |
| F10 | 执行通道齐备：`ssh_channel_new/open_session/request_exec/write/send_eof/read_timeout/is_eof/close`；`ssh_channel_get_exit_status` **带 `SSH_DEPRECATED`** | 读源 | 推送腿成立（D7）；取 exit status 沿用 7.96④ 的定点抑制先例，不新建纪律 |
| F11 | 配置目录明文审计（`CONN.09`）已有自动化证人 `utest_config::no_file_written_into_the_config_directory_carries_a_credential_name` | 在册 | 本棒产物落 `~/.ssh` 而非配置目录、passphrase 不入 schema ⇒ **不触该审计面**（§6 末条把这句钉成判据） |

**未在实测射程内**：Windows/MSVC 腿（沙箱无 MSVC，F4/F6/F7 的该腿行为属推断）；真 sshd 环境的推送成功腿（无 sshd，同 7.96① 的在册欠账）。

## 1 范围与边界

**本期交付**＝需求原文四子项，逐句对口：① 生成密钥对（ed25519 优先 / RSA 备选，可设 passphrase）；② `~/.ssh` 密钥列表；③ 公钥导出一键复制；④ 公钥推送至主机（`authorized_keys` 追加，执行通道）。

**不在本期**（§7 在册）：删除 / 改名 / 改口令、把生成物的口令存进 OS 凭据库（`CONN.09` 的独立后端任务未落，`CredentialStore` 现在只有替身）、加载到 agent、证书（`*-cert01`）与 FIDO2/安全密钥（`SSH_PKI_OPTION_SK_*` 一族）、多目录与自选目录、Windows 私钥 ACL、生成物回填某档案的 `identity_file`。

**与既有裁决的交界**：推送的凭据链沿用 7.97 D7（句柄直取、`ask_every_time` 弹 `credential_prompt`）；「面板不认识 libssh、对象与运行态在装配层」沿用 7.97 D5①；串行队列沿用 7.95 D7①；**本条不需要新的 schema 键**（D8），故 `utest_settings_catalog` 的键数与白名单台账一字不动。

## 2 草图与判据对应（屏 7）

草图主屏＝左侧停靠卡片（有一张 SSH 标签在场，但面板不依赖它）+ 两处浮层（生成对话框、推送对话框），底部 1–6 标号判据：

| 标号 | 判据 | 对应需求子项 |
|---|---|---|
| 1 | 入口与形态：`keys.open` 命令呼出的**左侧停靠卡片**（520 dp、非模态、默认隐藏），与 `connections.toggle` / `tunnels.open` / `sftp.toggle` 同族不同件；无 SSH 标签也能列表与生成 | ②（D1 拍形态） |
| 2 | 列表行两行堆叠：首行 名称（basename）+ 类型徽标（`ed25519`/`rsa 3072`）+ 口令锁徽标 + 行内动作「复制」「推送」；次行 `SHA256:…` 指纹 + 文件路径 | ②③（D2 拍形态） |
| 3 | 加密态两档：🔒「有口令」/ 无锁「无口令」（F8 的一次探测）；**私钥无配对 `.pub`** 的行给「公钥缺失」态并把动作换成「导出公钥」 | ②（D2②/D3①） |
| 4 | 一键复制：交出行内**完整单行** `ssh-ed25519 AAAA… comment`，经 `session::ClipboardOutbox` 在帧边界落系统剪贴板（7.41 先例），行上方留痕一句「已复制」 | ③（D6 拍形态） |
| 5 | 生成对话框：两卡定字段集（ed25519 缺省；**只有 RSA 卡出位数下拉** 2048/3072/4096，缺省 3072）＋文件名＋注释＋passphrase 双栏（可空、掩码）；**同名即拒并红提示，绝不覆盖**（F5）；提交后按钮置灰、行内「生成中」，长计算在 worker（F3） | ①（D4/D5） |
| 6 | 推送对话框：目标＝档案下拉（SSH 子集，同 `tunnel_panel` 的 `profiles` Hook）；步骤态 `拨号 → 认证 → 执行 → 完成/失败`，失败归因走 `conn::DialOutcome` 六档文案；**本地判重**：先读回远端 `authorized_keys` 比对，已含同一行就不发写并告知 | ④（D7/D8） |

## 3 裁决（D1–D10，**待裁**）

| # | 待决 | 选项 | 结论（推荐） | 理由 |
|---|---|---|---|---|
| **D1** | 面板形态 | ① 左侧停靠卡片（`keys.open`，520 dp）；② 连接侧栏内第二层页签；③ 设置面板新页；④ 独立对话框（`connection_wizard` 同族） | **①** | ② 的侧栏 320 dp 放不下「指纹 43 字符 + 路径 + 两个行内动作」；③ 设置页是编辑形态，承载不了「生成中 / 推送中」运行态刷新；④ 模态框挡住终端且不留列表，而需求第一子项是**列表**。① 与隧道面板同族，入口登记方式照抄 `tunnels.open` |
| **D2** | 行源与扫描面 | ① `~/.ssh` 单层扫，行源＝「`*.pub` 配对私钥」∪「**无 `.pub` 的私钥**」（后者给「导出公钥」动作）；② 只列 `*.pub`；③ 允许用户自加目录 | **①** | 需求原文是「`~/.ssh` 密钥列表」，只列 `.pub` 会漏掉「私钥在、公钥被手删」这一常见形态且没有出口；③ 多目录要新的持久化面（与 D8 冲突）。不递归、不跨目录、不跟随符号链接（防把整盘当密钥列表） |
| **D3** | 加密态是否逐把判定 | ① 逐把判（F8：一次 `import_privkey_file(…, nullptr, …)`，失败即「有口令」，实测 0 ms）；② 不判，行内不显锁徽标 | **①** | 「可设 passphrase」的密钥若列表看不出哪把有口令，用户在连接时才会撞上追问，列表就少了它最该回答的信息。判据只是**探测**不是**解密**（45 ms/把的 KDF 那条路不走） |
| **D4** | 生成对话框字段与线程 | ① 两卡 + RSA 位数下拉 + 文件名 + 注释 + passphrase 双栏，生成在 **worker 串行队列**、面板只读 latest-value 快照；② 同上但 UI 线程直生成；③ 注释不让填（用 F7 的 `user@host`） | **①** | ② 违 AGENTS 第 25 条（F3 实测 RSA-4096 369 ms，且这是 `-O0` 读数，机器弱时更久）；③ 丢掉注释等于丢掉「这把钥匙是干什么的」的唯一人读线索，且 `.pub` 得本仓自写（F7）——注释写进 `.pub` 第三字段，不进私钥文件、不进日志 |
| **D5** | 覆盖闸与私钥权限 | ① 先在目标目录建 **0600 临时文件**（`O_CREAT\|O_EXCL`，platform 腿）→ 令 libssh 截断写入 → `rename` 就位；同名已存在即**拒**并提示改名；② 直接交给 libssh 写目标路径，事后再 `chmod` | **①** | F5 证明覆盖语义在库侧不可拦，②会把「不覆盖」变成检查后写、中间有窗口；F6 证明库侧不管模式，②的事后 `chmod` 留一段 0664 暴露窗（同机用户可读私钥材料）。①把三道闸（不覆盖、0600、原子就位）收在同一次 `O_EXCL` 创建里 |
| **D6** | 公钥复制的交付面 | ① 只一枚「复制」按钮：交完整单行进剪贴板 + 行上方留痕一句；② 另加「另存为文件…」第二出口 | **①** | 需求原文「公钥导出一键复制」——`.pub` 本就与私钥同目录躺着，第二出口是给文件管理器重复干活，且要接 `file_dialog`（其 POSIX 真实现是 `CONN.04` 已在册的框架缺口腿） |
| **D7** | 推送通道 | ① **执行通道**：`ssh_dial` 自开会话 → exec `cat ~/.ssh/authorized_keys` 取回内容本地判重 → 需追加则 exec `sh -c 'umask 077; mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys'`，**公钥行走 stdin**；② SFTP 读-改-写整文件；③ 只生成 `ssh-copy-id` 命令串交用户自跑 | **①** | ② 是整文件重写：远端并发登录写 authorized_keys 会被覆盖，且要求远端可写 `.ssh` 目录权限位；③ 不满足需求「推送至主机」这一动作本身。① 的追加语义天然、公钥不进命令行（不进对端进程表、不进本仓日志）；代价：远端禁 exec（restricted shell）时失败，落 `Failed` 归因而非静默 |
| **D8** | 会话与凭据归属 | ① 推送用**独立会话**（每次操作自开自关，口径同 7.96①「每隧道自开会话」），目标档案与凭据链照 7.97 D7；② 借用当前活动 SSH 标签的既有会话 | **①** | ② 要动 `SshConnection` 的通道所有权模型（一条会话两个持有者），且「没有 SSH 标签就推不了」违背面板的独立入口；① 复用现成 `ssh_dial_and_authenticate`，明文随函数返回消失（7.93），本件不留副本 |
| **D9** | 删除 / 改名 / 改口令 | ① 本期不做，登记延后；② 本期做删除（两段式确认） | **①** | 需求四子项没有删除，AGENTS 第 1 条禁「推断需求外行为」；且删除私钥不可逆、连带 `authorized_keys` 里的残留无法回收，做成半截比不做更容易出事。② 若要，须各立判据与文案（`CONN.10` 的第二棒） |
| **D10** | `~/.ssh` 目录的来源 | ① 新增 `platform::ssh_user_dir()`（POSIX 读 `HOME`；Win32 读 `USERPROFILE`），面板经装配层注入目录；② 照 `src/main.cpp` 既有那行 `std::getenv("HOME")` 再复制一份 | **①** | 环境变量的取法是平台知识（AGENTS 第 23 条），第二份复制会把它钉进共享路径；`main.cpp` 那行属既成事实，本棒**不动**（第 3 条不等宽重构），登记为观察项随 `CONN.03` 的档案腿一并收 |

## 4 文件布局 / TU 拆分

```
src/conn/key_model.{h,cpp}         新增 —— 纯逻辑，无 libssh / 无 au:: 类型（可无头单测）：
                                        KeyCandidate（basename/type/bits/comment/fingerprint/有无私钥·公钥/
                                        加密态/路径）与行序折叠、名称与注释校验（禁路径分隔符与控制字符、
                                        长度上限）、同名碰撞判定、authorized_keys 的判重与「该不该追加」计划
                                        plan_append()、公钥单行的拼装（type+b64+comment）
src/conn/key_store.{h,cpp}         新增 —— 私有头：libssh PKI 传输腿。扫目录成 KeyCandidate 表、
                                        ssh_pki_generate_key + F4 落盘 + D5① 的临时文件与 rename、
                                        从私钥导出公钥（ssh_pki_export_privkey_to_pubkey）。
                                        **全部阻塞 IO / 长计算，调用方须在 worker 线程**（头注写明，同 sftp_client）
src/conn/key_push.{h,cpp}          新增 —— 私有头：执行通道推送腿。ssh_dial 自开会话 + 两段 exec（D7①）；
                                        exit status 处沿用 tunnel_client 的定点抑制写法（F10）；四态结果回投
src/platform/file_secure.h         新增 —— 私钥文件权限腿（AGENTS 第 23 条）：
src/platform/posix/file_secure.cpp      `create_private(path)`＝O_CREAT|O_EXCL|0600 的裸 fd 打开后关闭，
src/platform/win/file_secure.cpp        供传输腿把文件交给自己已建好的 0600 路径；Win32 腿本期＝建文件后
                                        不改 ACL 并 AURORA_LOG_WARN 留痕（真机 ACL 归 §7）
src/ui/keys_format.{h,cpp}         新增 —— 枚举→词条 key 的唯一映射（DialOutcome / 推送四态 / 生成三态 /
                                        加密两态）+ 行模型 key_rows()（tunnel_format 同族，无头单测）
src/ui/keys_panel.{h,cpp}          新增 —— 左停靠卡片 + 生成对话框 + 推送对话框；不认识 libssh（D8①/D1① 纪律）
src/main.cpp                       修改 —— keys.open 命令（照 tunnels.open 写法）、单 worker 串行队列、
                                        latest-value 快照、剪贴板帧边界、凭据链接 credential_prompt、启动不扫盘
src/CMakeLists.txt                 修改 —— 上述 .cpp 编入 borealis_core（显式源列表，见 HANDOFF §3）
codespec/UI_KEYS.draft.{md,svg,png} 本稿三件套
tests/unit/utest_keys_model.cpp    新增 —— 校验/碰撞/判重/公钥拼装（无头、无 IO）
tests/unit/utest_keys_format.cpp   新增 —— 四态与两档徽标的词条 key 映射、行序
tests/integration/itest_keys_panel.cpp 新增 —— 无头真派发：行表随快照刷新、真点复制进 outbox、
                                        同名红提示、生成期按钮置灰、推送步骤态推进与失败归因
```

> 不改 `include/borealis/**`（面板与传输腿都走私有头，D1① 先例）；**不加配置键**（D8 无、D2 无）；无新框架缺口——卡片/按钮/下拉/掩码输入/浮层全部是 Aurora 公共控件组合，`credential_prompt` 与 `connection_wizard` 的先例齐备。

## 5 数据流与线程模型

```
~/.ssh（platform::ssh_user_dir()，D10①）
      │ 扫盘：目录单层、不递归、不跟符号链接
      ▼
装配层（main.cpp）
      ├─ KeysWorker：单线程串行队列（7.95 D7① 同族——同一时刻只跑一把钥匙的事）
      │     scan(dir) ─────────────► vector<KeyCandidate>
      │     generate(spec) ────────► 临时 0600 → libssh 写 → rename（D5①）→ 重扫
      │     export_pub(priv_path) ─► 询问 passphrase（必要时 credential_prompt）→ 写 .pub
      │     push(profile, secret, line)（D7①/D8①）
      │   └─ 每个任务的终值写进 latest-value 快照 {stage, rows, notice, failure}
      │       （mutex + map，只留最新值——D6① of 7.97 同一条 AGENTS §25 兑现）
      ├─ 剪贴板：行内「复制」→ session::ClipboardOutbox::write()，帧边界 drain（7.41）
      └─ 凭据：目标档案 SecretHandle → 句柄/内存直取；ask_every_time → credential_prompt（7.97 D7）
                    ▲
KeysPanel（UI 线程）：tick() 读快照 → key_rows() 合成 → 行表有变才重建；动作一律经 Hooks 交回装配层
```

- **UI 线程零阻塞**：生成与推送都不在 UI 线程；面板关着 worker 的在途任务照跑完并写快照，回来看见的就是终态（与「关闭面板不停隧道」同一条投影纪律）。
- **面板关闭不等 worker**：析构时投一条「跑完当前任务即退」的收尾，`join` 上界＝一次 RSA-4096 生成（F3 读数）或一次 exec 的连接超时；该取舍与 `SftpPanel` 断连同档并写在头注，走查不可接受再改投递。
- **明文面**：passphrase 只活在对话框提交那一次调用栈，随 F4 的实参进 libssh 后即销毁；本件与传输腿都不持副本、不落盘、不进日志（CONN.09 不变量，判据 §6 末条）。
- 启动序**不扫盘**（零配置可用 `PREF.06`：首屏不做同步 IO）；扫盘只发生在 `keys.open` 与每次生成/导出/推送之后。

## 6 验收判据映射

- **生成密钥对（①）**：ed25519 缺省、RSA 三档；passphrase 可空；产物＝同 basename 的私钥 + `.pub`；D5① 三道闸各有判据（同名拒绝、临时文件 0600、rename 就位）；**ed25519 的真生成允许进无头用例**（F8/F3：亚毫秒、无网络），断言私钥文件模式与 `.pub` 单行三字段与指纹前缀 `SHA256:`；RSA 档在用例里只判参数与计划，不跑秒级生成。
- **列表（②）**：扫盘件是「目录 → 行表」的纯 IO 函数，用例走临时目录夹具（含成对、孤儿私钥、非密钥文件、子目录四类干扰项）；加密两档徽标按 F8 判；孤儿私钥行的「导出公钥」腿有判据。
- **一键复制（③）**：判据交出的字节＝`type + ' ' + base64 + ' ' + comment` 单行、无尾随换行进 outbox；是否真落系统剪贴板归真机走查（7.41 同口径，本仓只判 outbox 侧）。
- **推送至主机（④）**：本地判重命中即不发写并告知「已在授权表里」；追加走 stdin，**公钥不出现在命令串**（该判据写成用例断言 exec 命令常量里没有 base64 段）；四态推进与 `DialOutcome` 六档归因文案；**真 sshd 成功腿待 `etest_`**（与 7.96① / 7.100 的在册欠账同一条环境约束），本棒交付面只到「无头可证」。
- **安全面（CONN.09 交界）**：配置目录明文审计那条既有自动化用例（F11）在本棒后仍绿——本棒不落任何新键、产物不落配置目录；`grep` 判据不因本棒出现新文件而放宽。
- **门禁与测试**：`utest_keys_model` + `utest_keys_format` + `itest_keys_panel` 全绿并入 CTest（前缀纪律，AGENTS 第 18 条）；`ctest --preset linux -E etest_` 项数在此之上 +3。
- **真机走查**（AGENTS 第 33 条，本稿不宣称「可用」）：卡片观感与行密度、passphrase 双栏手感、复制与推送两处留痕看不看得见、RSA-4096 生成期的置灰反馈够不够。

## 7 本期不做（延后子项）

- 删除 / 改名 / 修改口令 / 把 passphrase 存入 OS 凭据库（`CONN.09` 后端任务的到货项）
- 加载到 `ssh-agent`、证书签名请求、FIDO2/安全密钥（`SSH_PKI_OPTION_SK_*` 一族，F1 同头可见但需求未列）
- 多目录 / 自选目录 / 递归扫描（D2③）
- Windows 私钥 ACL（D5① 的 Win32 腿本期只留 WARN，待真机）
- 生成物回填某档案的 `identity_file`（档案编辑已有向导入口，联动属体验增强）
- 真 sshd 的推送成功 etest（环境欠，同 7.96①）

## 8 评审待决汇总

D1–D10 逐条推荐如上，其中 **D4（worker 线程）与 D5（不覆盖 + 0600 + rename）是安全与性能纪律的落点而非口味**，D2②/D6②/D7②③/D9②/D10② 各有代价已写在理由列。若均按推荐裁决，收口动作＝本稿 §3 改「已裁决」+ 并入 `SPECIFICATIONS.md` §7 新裁决 + `CHANGELOG.md` 记版本 + `PLAN.md` 的 CONN.10 行回写，然后按 §4 布局分批落 `src/`（批 1 纯逻辑两件 + platform 腿；批 2 传输腿两件；批 3 面板与装配；批 4 文档回写）。
