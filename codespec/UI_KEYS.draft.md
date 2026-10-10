# SSH 密钥管理器 UI 设计稿（`SPEC.FEAT.CONN.10`）

> **状态**：评审已收口（2026-10-10）——§3 的 D1–D12 经人逐条裁决：**D1/D3/D4/D5/D6/D7/D8/D10 按推荐①**，**D2 改判「自选目录入扫面，且可作生成落盘目录」**（细则＝D11 的录入接 `au::file_dialog`），**D9 改判「本期做删除」**（细则＝D12：一次清一对、拒删符号链接、`~/.ssh` 不可移出扫面）。并入 `codespec/SPECIFICATIONS.md` §7 裁决 **7.106**，据此按 §4 分批转实现。**批 1（纯逻辑两件 + platform 腿 + 第七域）已于同日落地＝裁决 7.107**，落地期把本稿三处口径按代码改口（§4 的 `settings_catalog` 那行、§6 的 `utest_config` 那行、名称与注释的两个长度数），并删掉一处永远走不到的分支——详见 §8 末段与稿内标注。**批 2（传输腿两件，含删除腿）已于同日落地＝裁决 7.108**：§0 增 F12–F17 六条实测读数，四处口径按代码改口（判据 6 的失败档数、§4 的弃用抑制作废、「生成三态」归属、`keys_format` 不 include 传输腿头），详见 §8 末段。**批 3（面板与装配，含目录腿与中文词条）已于同日落地＝裁决 7.109**，§4/§5/§6 三处按代码回写（重建触发面三档、目录腿的闸收在面板、「已复制」的留痕来源）；**批 4（文档回写与走查登记）随之收口**——真机走查仍是人工门槛（§6 末条），本条不称「可用」。
> **配套高保真草图**：`codespec/UI_KEYS.draft.svg`（屏 7）/ `.png`；底部 1–7 标号判据对应本稿 §2，SVG 为事实来源。
> **已落地的前置**：`SPEC.FEAT.CONN.02` 传输腿与 `conn/ssh_dial`（裁决 7.94）、`CONN.03` 档案与 `ProfileStore`、`CONN.09` 的 `SecretHandle`/询问件（裁决 7.93，配置目录审计的自动化证人已在 `utest_config`）、`CONN.04` 的 worker 串行队列先例（裁决 7.95）、`CONN.08` 的左停靠卡片面板与「装配层持有运行态」形态（裁决 7.97/7.98）。
> **本条是 M3 主干余下唯一未全部落地的需求条目**（批 1–2 已落，批 3–4 未开工）（`PLAN.md` §4 交接清册 §4.1 所列 CONN.08 真转 etest / WS.05 etest 都要 sshd 环境、TERM.09 提示卡与各 UI 件走查都归人工门槛）。

## 0 本稿的实测地基（libssh 0.12.0 + vcpkg OpenSSL，本机 Linux 腿，2026-10-10 探针两轮：F1–F11 出稿前、F12–F17 随批 2，探针件均已删）

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
| F12 | 0.12 **公共头没有 `ssh_key` 的位数与注释访问器**（`ssh_key_bits` 一类不在公共面；私钥文件本身也不存注释） | 读源 | 位数由本仓从公钥 blob 的 mpint `n` 数出来（`rsa_bits_from_public_base64`，批 1 的纯逻辑件）；注释**只有一个来源**＝`.pub` 第三字段，无 `.pub` 的行注释恒空、不编一个（`export_public_key` 交回的行同理留空） |
| F13 | **无口令必须传 `nullptr`**：`ssh_pki_export_privkey_file_format(key, "", …)` | 本机回 -1 且只留下一份 0 字节文件——空串走的是「以空口令加密」那条分支，不是「不加密」 | `GenerateRequest::passphrase` 的 nullopt 与空串**折成同一档**（`ssh_dial` 那边 `ssh_userauth_privatekey_file` 收空串是另一个函数的口径，别照抄） |
| F14 | D5① 的「临时件 → 截断写入 → rename」成立：对已存在的 0600 临时件调导出函数 | rc=0、写完权限仍是 600（size 395 读数） | 三道闸收成一次 `O_EXCL` 的设计可用；目标路径从未经过「存在且权限过宽」的那一瞬 |
| F15 | 取退出码的**非弃用**形态是 `ssh_channel_get_exit_state(ch, &code, &signal, &core)`，`ssh_channel_get_exit_status` 只是它的转发且带 `SSH_DEPRECATED` | 读源（`channels.c`） | §4 那句「exit status 处沿用 tunnel_client 的定点抑制写法（F10）」按代码**作废**：本腿不新增第二处抑制，7.96④ 那处仍是全仓唯一一处，与 F1 的「不引第二处弃用抑制」对齐。另：`pexit_signal` 是 `strdup` 出来的，传 nullptr 即不取、也就没有一份没人 free 的孤儿 |
| F16 | libstdc++（本机 GCC 15）的 `fs::symlink_status(p, ec)` 对**路径不在场**把 ENOENT 同时报进 `ec` 与 `file_type::not_found` | 写反顺序（先判 `ec`）时 11 条用例里 10 条红——每一次生成都被同名闸误拦 | 「不在场」必须判在 `ec` **之前**；其余错误（权限不足等）才落「判不了」那一档。这条以变异注入为证，不在场与判不了混为一谈是最坏的一类误读 |
| F17 | `ssh_get_fingerprint_hash` 交回的串**只归 `free`**（F9 的补） | 再走 `ssh_string_free_char` 本机当场 double free abort | 释放口径以探针为准，别照 `ssh_string` 一族的惯例去释放它 |

**未在实测射程内**：Windows/MSVC 腿（沙箱无 MSVC，F4/F6/F7 的该腿行为属推断）；真 sshd 环境的推送成功腿（无 sshd，同 7.96① 的在册欠账）——批 2 落地期在本机复核过「`sshd` 不在 PATH、`/usr/sbin/sshd` 与 `/etc/ssh/ssh_host_*` 皆无」，故该腿的两段 exec（读回 + 追加）与「已授权就不发写」那一条在用户侧仍**无可达证据**，能证的只有闸门（命令行不含密钥材料）与拨号档位（`itest_keys_push` 的拒连两用例）。

## 1 范围与边界

**本期交付**＝需求原文四子项，逐句对口：① 生成密钥对（ed25519 优先 / RSA 备选，可设 passphrase）；② `~/.ssh` 密钥列表（**含人改判后追加的自选目录面**，D2③）；③ 公钥导出一键复制；④ 公钥推送至主机（`authorized_keys` 追加，执行通道）。**外加人改判追加的删除腿**（D9②：一次清一对，两段式确认）。

**不在本期**（§7 在册）：改名 / 改口令、把生成物的口令存进 OS 凭据库（`CONN.09` 的独立后端任务未落，`CredentialStore` 现在只有替身）、加载到 agent、证书（`*-cert01`）与 FIDO2/安全密钥（`SSH_PKI_OPTION_SK_*` 一族）、递归扫描与符号链接、Windows 私钥 ACL、生成物回填某档案的 `identity_file`。

**与既有裁决的交界**：推送的凭据链沿用 7.97 D7（句柄直取、`ask_every_time` 弹 `credential_prompt`）；「面板不认识 libssh、对象与运行态在装配层」沿用 7.97 D5①；串行队列沿用 7.95 D7①；目录录入沿用 `settings_panel` 的 `pick_export_path` Hook 形态（面板不碰对话框，装配层调 `au::file_dialog`，**空串＝取消或平台起不来同途**）。**人改判 D2③ 的连带代价**：`Settings` 自此多第七域 `key_dirs`（`std::vector<std::string>`），公共头 + `store.cpp` 往返 + `utest_config` 补例 + `settings_catalog` 白名单四处都要动——出稿时「不加配置键」的那句随改判作废。

## 2 草图与判据对应（屏 7）

草图主屏＝左侧停靠卡片（有一张 SSH 标签在场，但面板不依赖它）+ 三处浮层（生成对话框、推送对话框、删除确认），底部 1–7 标号判据：

| 标号 | 判据 | 对应需求子项 |
|---|---|---|
| 1 | 入口与形态：`keys.open` 命令呼出的**左侧停靠卡片**（520 dp、非模态、默认隐藏），与 `connections.toggle` / `tunnels.open` / `sftp.toggle` 同族不同件；无 SSH 标签也能列表与生成；面板头两枚动作「＋目录」「＋生成密钥」 | ②（D1 拍形态、D2③ 扩目录） |
| 2 | 列表行两行堆叠：首行 名称（basename）+ 类型徽标（`ed25519`/`rsa 3072`）+ 口令锁徽标 + 行内动作「复制」「推送」「⋯」；次行 `SHA256:…` 指纹 + **全路径**（多目录后 basename 会撞，行唯一键＝路径） | ②③（D2 拍形态） |
| 3 | 加密态两档：锁「有口令」/ 空锁「无口令」（F8 的一次探测）；**私钥无配对 `.pub`** 的行给「公钥缺失」态并把动作换成「导出公钥」 | ②（D2①/D3①） |
| 4 | 一键复制：交出行内**完整单行** `ssh-ed25519 AAAA… comment`，经 `session::ClipboardOutbox` 在帧边界落系统剪贴板（7.41 先例），行上方留痕一句「已复制」 | ③（D6 拍形态） |
| 5 | 生成对话框：两卡定字段集（ed25519 缺省；**只有 RSA 卡出位数下拉** 2048/3072/4096，缺省 3072）＋文件名＋注释＋passphrase 双栏（可空、掩码）＋**目标目录（缺省 `~/.ssh`，经「浏览…」取，D2③）**；**同名即拒并红提示，绝不覆盖**（F5）；提交后按钮置灰、行内「生成中」，长计算在 worker（F3） | ①（D4/D5） |
| 6 | 推送对话框：目标＝档案下拉（SSH 子集，同 `tunnel_panel` 的 `profiles` Hook）；步骤态 `拨号 → 认证 → 执行 → 完成/失败`，失败归因随批 2 到货为 `conn::PushFailure` 的**七档**：会话未分配／网络／主机键未信任／认证被拒（前四档由 `conn::DialOutcome` 的四档失败一一映射，该枚举实测五值含 `Ok`）＋ 执行被拒／写回执异常（两段 exec 腿自己的两档）＋ **行不成立**（公钥行拼不出可发的字节，在分配会话**之前**就判掉，因此它不会配一个「已拨号」的步骤态）；**本地判重**：先读回远端 `authorized_keys` 比对，已含同一行就不发写并告知（落地期订正，裁决 7.107 ④d 与 7.108） | ④（D7/D8） |
| 7 | 删除两段式：行内「⋯」→ 确认框**列出要删的具体路径**（私钥连同同名 `.pub` 一次清一对），红字写明「不可逆 + 远端 `authorized_keys` 的公钥不会随之回收」；取消则一物不动 | ②（D9② 改判追加，细则＝D12） |

## 3 裁决（D1–D12，2026-10-10 收口：D1/D3/D4/D5/D6/D7/D8/D10 按推荐①，D2/D9 改判，改判追加 D11/D12）

| # | 待决 | 选项 | 结论（推荐） | 理由 |
|---|---|---|---|---|
| **D1** | 面板形态 | ① 左侧停靠卡片（`keys.open`，520 dp）；② 连接侧栏内第二层页签；③ 设置面板新页；④ 独立对话框（`connection_wizard` 同族） | **①** | ② 的侧栏 320 dp 放不下「指纹 43 字符 + 路径 + 两个行内动作」；③ 设置页是编辑形态，承载不了「生成中 / 推送中」运行态刷新；④ 模态框挡住终端且不留列表，而需求第一子项是**列表**。① 与隧道面板同族，入口登记方式照抄 `tunnels.open` |
| **D2** | 行源与扫描面 | ① `~/.ssh` 单层扫，行源＝「`*.pub` 配对私钥」∪「**无 `.pub` 的私钥**」（后者给「导出公钥」动作）；② 只列 `*.pub`；③ 在 ① 之上**允许用户自选目录** | **③（人已改判，在 ① 之上追加）** | 需求原文是「`~/.ssh` 密钥列表」，只列 `.pub` 会漏掉「私钥在、公钥被手删」这一常见形态且没有出口（① 的这半保留）。细则四条：⑴ 目录表落 `Settings` 第七域 `key_dirs`（`vector<string>`，缺省空＝只扫 `~/.ssh`）；⑵ **`~/.ssh` 恒在扫面内不可移除**（否则「首屏零配置可用」的 `PREF.06` 不成立）；⑶ 自选目录与 `~/.ssh` 同纪律：单层、不递归、不跟符号链接、路径去重，不可达目录**保留条目并行内留痕**（不在装载时偷偷删用户的东西）；⑷ **行唯一键从 basename 换成全路径**——多目录后同名 basename 必然出现，行表与快照的 diff 都按路径。生成目录也可选（对话框那枚「目标目录」，缺省 `~/.ssh`），D5① 的三道闸按所选目录适用；所选生成目录**不入表**，每次回到缺省，以免为一次性的生成动作再开持久化面 |
| **D3** | 加密态是否逐把判定 | ① 逐把判（F8：一次 `import_privkey_file(…, nullptr, …)`，失败即「有口令」，实测 0 ms）；② 不判，行内不显锁徽标 | **①** | 「可设 passphrase」的密钥若列表看不出哪把有口令，用户在连接时才会撞上追问，列表就少了它最该回答的信息。判据只是**探测**不是**解密**（45 ms/把的 KDF 那条路不走） |
| **D4** | 生成对话框字段与线程 | ① 两卡 + RSA 位数下拉 + 文件名 + 注释 + passphrase 双栏，生成在 **worker 串行队列**、面板只读 latest-value 快照；② 同上但 UI 线程直生成；③ 注释不让填（用 F7 的 `user@host`） | **①** | ② 违 AGENTS 第 25 条（F3 实测 RSA-4096 369 ms，且这是 `-O0` 读数，机器弱时更久）；③ 丢掉注释等于丢掉「这把钥匙是干什么的」的唯一人读线索，且 `.pub` 得本仓自写（F7）——注释写进 `.pub` 第三字段，不进私钥文件、不进日志 |
| **D5** | 覆盖闸与私钥权限 | ① 先在目标目录建 **0600 临时文件**（`O_CREAT\|O_EXCL`，platform 腿）→ 令 libssh 截断写入 → `rename` 就位；同名已存在即**拒**并提示改名；② 直接交给 libssh 写目标路径，事后再 `chmod` | **①** | F5 证明覆盖语义在库侧不可拦，②会把「不覆盖」变成检查后写、中间有窗口；F6 证明库侧不管模式，②的事后 `chmod` 留一段 0664 暴露窗（同机用户可读私钥材料）。①把三道闸（不覆盖、0600、原子就位）收在同一次 `O_EXCL` 创建里 |
| **D6** | 公钥复制的交付面 | ① 只一枚「复制」按钮：交完整单行进剪贴板 + 行上方留痕一句；② 另加「另存为文件…」第二出口 | **①** | 需求原文「公钥导出一键复制」——`.pub` 本就与私钥同目录躺着，第二出口是给文件管理器重复干活。（出稿时这条还拿「`file_dialog` 的 POSIX 缺口」当理由；D11 改判后该缺口已按框架分流登记，故本条的取舍理由回到需求原文那一句，结论不变） |
| **D7** | 推送通道 | ① **执行通道**：`ssh_dial` 自开会话 → exec `cat ~/.ssh/authorized_keys` 取回内容本地判重 → 需追加则 exec `sh -c 'umask 077; mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys'`，**公钥行走 stdin**；② SFTP 读-改-写整文件；③ 只生成 `ssh-copy-id` 命令串交用户自跑 | **①** | ② 是整文件重写：远端并发登录写 authorized_keys 会被覆盖，且要求远端可写 `.ssh` 目录权限位；③ 不满足需求「推送至主机」这一动作本身。① 的追加语义天然、公钥不进命令行（不进对端进程表、不进本仓日志）；代价：远端禁 exec（restricted shell）时失败，落 `Failed` 归因而非静默 |
| **D8** | 会话与凭据归属 | ① 推送用**独立会话**（每次操作自开自关，口径同 7.96①「每隧道自开会话」），目标档案与凭据链照 7.97 D7；② 借用当前活动 SSH 标签的既有会话 | **①** | ② 要动 `SshConnection` 的通道所有权模型（一条会话两个持有者），且「没有 SSH 标签就推不了」违背面板的独立入口；① 复用现成 `ssh_dial_and_authenticate`，明文随函数返回消失（7.93），本件不留副本 |
| **D9** | 删除 / 改名 / 改口令 | ① 本期不做，登记延后；② 本期做删除（两段式确认） | **②（人已改判）** | 本期做删除，改名与改口令仍延后（需求四子项没有它们，AGENTS 第 1 条禁推断需求外行为）。细则见 D12 |
| **D10** | `~/.ssh` 目录的来源 | ① 新增 `platform::ssh_user_dir()`（POSIX 读 `HOME`；Win32 读 `USERPROFILE`），面板经装配层注入目录；② 照 `src/main.cpp` 既有那行 `std::getenv("HOME")` 再复制一份 | **①** | 环境变量的取法是平台知识（AGENTS 第 23 条），第二份复制会把它钉进共享路径；`main.cpp` 那行属既成事实，本棒**不动**（第 3 条不等宽重构），登记为观察项随 `CONN.03` 的档案腿一并收 |
| **D11** | 自选目录的录入入口 | ① 面板内文本框粘绝对路径 + 存在性校验（不碰系统对话框）；② 接 `aurora::file_dialog::open_folder()`（公共 API 已在，真实实现仅 Win32，POSIX 腿为内联回退＝等价取消）；③ 目录表挪到设置面板录入 | **②（人已改判）** | 体验上系统选择器最直接，且**本仓已有同形态先例可抄**：`settings_panel` 的 `pick_export_path`/`pick_import_path` 就是「面板不碰对话框、装配层取路径、空串＝取消或平台起不来同途」，`utest`/`itest` 侧还有 `file_dialog::headless_folder_result` 钩子可把接线判据做成无头可证。代价如实登记：POSIX 真选择器缺 ⇒ Linux 腿点「＋目录」等价取消，该缺口按 AGENTS §5.1 走 Aurora 侧分流，缺口单已入 `codespec/FRAMEWORK_TASK_CONN10.md`、并登记为 `SPECIFICATIONS.md` 附录 A.2 的 **G41**（本条不等不绕，落前面板那枚按钮在 Linux 腿就是空响应）。① 被否是让人背粘贴路径的活儿；③ 会把「列表的运行态」与「目录表」拆到两个刷新周期里，且设置页多一域 |
| **D12** | 删除的语义与边界 | ① 一次清一对（私钥连同同名 `.pub`；孤儿私钥行只删本体）；② 只删私钥、`.pub` 留提示让用户自清；③ 逐文件分别删（行内两枚动作） | **①（人已改判，边界从推荐档）** | 一次动作清一对，不留无主公钥行（本期没有公钥单独删除的出口）。四条边界钉死：⑴ **两段式**——「⋯」只呼确认框，确认框列出要删的**具体路径**，取消则一物不动；⑵ **删除不读私钥内容 ⇒ 有口令的钥匙不需要口令就能删**（与「导出公钥」相反的一档，写清以免实现顺手加询问）；⑶ **拒删符号链接**——「删链接」与「删目标」语义歧义，且会让列表变成指向任意文件的口子；⑷ 只删扫描面内、且被识别为密钥的既有路径，删后重扫；失败（不在/权限不足）走红留痕不静默。确认框那句「远端 `authorized_keys` 里的公钥不会随之回收」是需求外的实话，必须上屏 |

## 4 文件布局 / TU 拆分

```
src/conn/key_model.{h,cpp}         新增 —— 纯逻辑，无 libssh / 无 au:: 类型（可无头单测）：
                                        KeyCandidate（**唯一键＝全路径**：path/basename/type/bits/
                                        has_public·public_path/encrypted/fingerprint/comment）与行序（目录表
                                        次序优先、同目录按名、**表外行排最后且不丢**）、名称与注释校验（禁路径
                                        分隔符与控制字符；出稿未给的两个数**落地期取定**＝名称 64 字节、
                                        注释 72 字节，裁决 7.107）、**目录表规范化（去重、~/.ssh 恒在表首且
                                        「移除」请求整条作废、不可达只标不删——可达性是**注入的判定函数**，
                                        纯逻辑层不许 stat，第 23/25 条）**、同名碰撞判定（**只看得见行**：
                                        行源是私钥，孤 `.pub` 不成行，故「无主公钥也占名」那一半归 key_store
                                        的盘上存在性检查，此处不留永远走不到的分支）、authorized_keys 的判重
                                        （按线名+base64 两字段，忽略注释与远端 `from="…"` 选项前缀）与
                                        「该不该追加」计划 plan_append()（远端末行无换行则前置一个换行、
                                        payload 自带尾随换行——与剪贴板那条相反）、公钥单行的拼装
                                        （type+b64+comment，单行、无尾随换行）
src/conn/key_store.{h,cpp}         新增 —— 私有头：libssh PKI 传输腿。多目录扫盘成 KeyCandidate 表、
                                        ssh_pki_generate_key + F4 落盘 + D5① 的临时文件与 rename（按所选目标
                                        目录适用）、从私钥导出公钥（ssh_pki_export_privkey_to_pubkey）、
                                        **删除腿 delete_key_pair（D12：一次清一对、拒删符号链接、只删扫面内
                                        既有路径）**。
                                        **全部阻塞 IO / 长计算，调用方须在 worker 线程**（头注写明，同 sftp_client）
src/conn/key_push.{h,cpp}          新增 —— 私有头：执行通道推送腿（批 2 已落）。ssh_dial 自开会话 + 两段 exec（D7①）；
                                        取退出码走**非弃用**的 `ssh_channel_get_exit_state`（F15，出稿那句
                                        「沿用 tunnel_client 的定点抑制写法」按代码作废，7.96④ 那处仍是全仓唯一
                                        一处抑制）；结果＝`{阶段四档, 失败七档(optional), 已授权布尔}`（判据 6 的
                                        那一行），公钥行只在 stdin 上走、不进命令串也不进日志（D7①）
src/platform/file_secure.h         新增 —— 私钥文件权限腿（AGENTS 第 23 条），批 1 已落：
src/platform/posix/file_secure.cpp      `platform::create_private_file(path)`＝O_WRONLY|O_CREAT|O_EXCL 建空文件
src/platform/win/file_secure.cpp        → **`fchmod(0600)` 无条件钉死**（mode 实参受 umask 支配，本机实测
                                        umask 0277 时只剩 0400）→ 立即关闭，三档 `Created`/`AlreadyExists`/`Error`；
                                        符号链接与目录都算 `AlreadyExists` 且永不跟随，`fchmod` 失败即 `unlink` 回滚；
                                        内容由传输腿截断写入（`open` 对已存在文件忽略 mode 实参，权限因此保住），
                                        最后调用方 `rename` 就位。Win32 腿本期＝建文件后不改 ACL 并 `AURORA_LOG_WARN`
                                        留痕（真机 ACL 归 §7；该腿未经 MSVC 编译验证，属推断/未验证）
src/ui/keys_format.{h,cpp}         新增 —— 枚举→词条 key 的唯一映射（tunnel_format 同族，无头单测）。
                                        **批 1 到货四族**：类型徽标（ed25519 / rsa 3072，三档互异）、
                                        加密两态、公钥缺失、名称与注释校验留痕 + 目录不可达留痕，
                                        外加次行数据段（指纹 + **全路径恒在**）。**批 2 到货三族**：
                                        生成／导出／删除三家 outcome、推送阶段四档、推送失败七档与
                                        「已授权／已追加／留空」三选一通知（`key_push_notice_key` 按
                                        「失败 > 未到完成档 > 成否」取一个）。本件**不 include 传输腿头**，
                                        只前置声明不透明枚举——面板经本件够得着 `scan_keys`／`push_public_key`
                                        的签名，就等于给「动作一律经 Hooks 交回装配层」开了一道门。
                                        **随批 3 到货**：行模型 `key_rows()`（要等运行态快照类型，裁决 7.97 D6① 同族）。
                                        出稿写的「`conn::DialOutcome` 的四档失败 + 生成三态 + 推送四态」按代码改口
                                        （落地期订正见裁决 7.107 ④d 与 7.108）：`DialOutcome` 不直接映射，
                                        由 `key_push` 一一转成 `PushFailure` 的前四档；「生成三态」也不在
                                        `key_store` 的枚举名下——那里到货的是 `GenerateOutcome` 五档（含 `Created`）、
                                        `ExportOutcome` 四档、`DeleteOutcome` 四档，而稿说的「三态」指装配层快照里
                                        那一行的「空闲／进行中／已回投」，是运行态不是枚举，本件不映射它
src/ui/keys_panel.{h,cpp}          新增 —— 左停靠卡片 + 生成对话框 + 推送对话框 + 删除确认；本件不认识
                                        libssh，也不认识 au::file_dialog（D8①/D1①/D11 纪律：目录经 Hook 进出）。
                                        **批 3 落地注记（裁决 7.109）**：另有一行顶行留痕（head＝「无归属在途
                                        任务 > 上一次留痕」的三选一，`key_headline_key`）；重建触发面按件
                                        拆三档——主卡比「行表逐字段全等（`KeyCandidate::operator==`）＋顶行
                                        留痕 key/arg 双比」（动作不改行表只改留痕，单比行表看不见「已复制」）、
                                        推送框比「在途档／阶梯格／留痕」三样**就地重建**（框里没有输入草稿，
                                        重建不打断什么）、生成框**不随 tick 重建**（正在输入的草稿不被动），
                                        其唯一自动出口是「快照离开 Generate 档」的 latch，提交成功即置灰防连点；
                                        目录腿的「不留痕」闸收在本件 `on_add_dir`（空选路整条返回，装配层不备
                                        第二道空闸，D11②）
src/main.cpp                       修改 —— keys.open 命令（照 tunnels.open 写法）、单 worker 串行队列、
                                        latest-value 快照、剪贴板帧边界、凭据链接 credential_prompt、启动不扫盘、
                                        **pick_key_dir 接 au::file_dialog::open_folder()**（照既有
                                        `picked_save_path`/`picked_open_path` 的形态与「空串＝取消或起不来同途」口径）。
                                        **批 3 落地注记（裁决 7.109）**：KeysWorker 在**提交时**发布在途标记（op/op_path
                                        立即可见、UI 即时置灰），任务尾巴把标记交给下一件排队任务或收成 Idle；扫盘腿
                                        收成 `run_keys_scan()` 一个自由函数（normalize → scan → sort，任务尾巴直接调
                                        它而不是捕获栈上 lambda，免悬挂）；「已复制」走 `publish_notice("keys.notice.copied")`
                                        即时留痕（无 conn 枚举可映射，不为它发明第二套枚举）；导出/推送的口令接续＝
                                        `SecretAsk` 按 `secret_ask_for(normalized_auth_method(…))` 取档，回调续交 secret
                                        给 worker 任务，明文只活在那一次调用栈（CONN.09）
include/borealis/config/settings.h 修改 —— **第七域 key_dirs（std::vector<std::string>，缺省空＝只扫 ~/.ssh，
                                        D2③ 的连带代价）**；纯路径串、不含任何凭据材料
src/config/store.cpp               修改 —— key_dirs 域读写与往返；`kDomainKeys` 追加一项
src/ui/settings_catalog.cpp        **不改**——`key_dirs` 不属四页偏好设置（增删改在面板自己在「＋目录」
                                        上做，不在设置面板画控件），故反向核对表里不登记它；改的是
                                        `tests/unit/utest_settings_catalog.cpp` 的 `kNonPreferenceLeaves`
                                        例外数组（与 `profiles.items` / `tunnels.items` 同档加一条
                                        `key_dirs.items`），**键数台账 62 行不变**——出稿时那句
                                        「白名单登记（键数台账随之变动）」把两处混成了一处，落地时按代码改口（裁决 7.107）
codespec/FRAMEWORK_TASK_CONN10.md  新增 —— Aurora 侧缺口单：`file_dialog::open_folder()` 的 POSIX 真实现
                                        （AGENTS §5.1 分流，登记为 SPECIFICATIONS.md 附录 A.2 的 G41；
                                        本仓不等不绕、也不自建文件选择器）
src/CMakeLists.txt                 修改 —— 上述 .cpp 逐条编入 borealis_core（该 TU 列表是显式的，不像
                                        tests 侧走 CONFIGURE_DEPENDS GLOB，漏一行即链接期才炸）
codespec/UI_KEYS.draft.{md,svg,png} 本稿三件套
tests/unit/utest_keys_model.cpp    新增 —— 校验/目录表规范化/碰撞/判重/公钥拼装（无头、无 IO）
tests/unit/utest_keys_store.cpp    新增 —— 扫盘成行、生成四道闸、导出与删除腿（临时目录夹具，无网络）
tests/unit/utest_keys_format.cpp   新增 —— 枚举→词条 key 映射：批 1 四族 + 批 2 三家 outcome 与推送
                                        阶梯／通知三选一，逐值互异、成功档留空（行序与路径唯一键随批 3）
tests/unit/utest_keys_push.cpp     新增 —— 两条 exec 命令常量的闸：命令行不含任何密钥材料形态的长 base64 串
                                        （以 `plan_append` 的 payload 作正对照，防空转）、读腿不含 `>`／`|`
tests/integration/itest_keys_push.cpp 新增 —— 拒连端口（127.0.0.1:1）证拨号档位；不成形的公钥行**不开socket**
tests/integration/itest_keys_panel.cpp 新增 —— 无头真派发：行表随快照刷新、真点复制进 outbox、
                                        同名红提示、生成期按钮置灰、推送步骤态推进与失败归因、
                                        删除两段式（「⋯」只呼框、取消不删）、目录腿经
                                        `file_dialog::headless_folder_result` 钩子证接线
tests/unit/utest_config.cpp        修改 —— 第七域往返补例（缺省空、**逐字往返不去重**、空串元素逐个丢弃并
                                        留 `key_dirs.items[N]` 痕迹、`items` 非数组整键回落、`key_dirs`
                                        不含凭据子串）——去重与可达性**不在装载侧**（那是
                                        `conn::normalize_key_dirs()` 的活儿，同步 IO 不进装载接缝），
                                        出稿把这两条记在本件名下是笔误，落地时改口（裁决 7.107）
```

> 公共头只动 `include/borealis/config/settings.h` 一处（第七域，D2③ 的连带代价）；面板与三条传输腿仍全走私有头（D1① 先例）。除 `key_dirs` 外**不加配置键**。无新框架缺口——卡片/按钮/下拉/掩码输入/浮层全部是 Aurora 公共控件组合，`credential_prompt` 与 `connection_wizard` 先例齐备；`file_dialog` 的 POSIX 腿是**既有**缺口的追加消费方，已按 AGENTS §5.1 分流并出缺口单，本条不自建替代实现。

## 5 数据流与线程模型

```
Settings.key_dirs（第七域）＋ platform::ssh_user_dir()（D10①，~/.ssh 恒在）
      │ 扫盘：逐目录单层、不递归、不跟符号链接、路径去重
      ▼
装配层（main.cpp）
      ├─ KeysWorker：单线程串行队列（7.95 D7① 同族——同一时刻只跑一把钥匙的事）
      │     scan(dirs) ─────────────► vector<KeyCandidate>
      │     generate(spec, dir) ────────► 临时 0600 → libssh 写 → rename（D5①）→ 重扫
      │     export_pub(priv_path) ─► 询问 passphrase（必要时 credential_prompt）→ 写 .pub
      │     push(profile, secret, line)（D7①/D8①）
      │     delete_key_pair(path) ─► 拒删符号链接 → unlink 私钥连同同名 .pub → 重扫（D12）
      │   └─ 每个任务的终值写进 latest-value 快照 {stage, rows, notice, failure}
      │       （mutex + map，只留最新值——7.97 D6① 同一条 AGENTS §25 兑现）
      ├─ 目录腿：面板「＋目录」→ pick_key_dir Hook → 装配层 au::file_dialog::open_folder()
      │            （空串＝取消或平台起不来，同 settings_panel 的 pick_export_path 口径）
      │            → key_model 侧规范化（去重/存在性/~/.ssh 不可移）→ persist 落第七域 → 重扫
      ├─ 剪贴板：行内「复制」→ session::ClipboardOutbox::write()，帧边界 drain（7.41）
      └─ 凭据：目标档案 SecretHandle → 句柄/内存直取；ask_every_time → credential_prompt（7.97 D7）
                    ▲
KeysPanel（UI 线程）：tick() 读快照 → key_rows() 合成 → 行表有变才重建；动作一律经 Hooks 交回装配层
```

- **UI 线程零阻塞**：生成与推送都不在 UI 线程；面板关着 worker 的在途任务照跑完并写快照，回来看见的就是终态（与「关闭面板不停隧道」同一条投影纪律）。
- **系统目录框是唯一例外**：`open_folder()` 由用户主动呼出、是 OS 的模态框，与设置面板导出/导入那两条腿同档（AGENTS §25 禁的是回调里的静默阻塞 IO，用户按下的模态框不在其列）。
- **面板关闭不等 worker**：析构时投一条「跑完当前任务即退」的收尾，`join` 上界＝一次 RSA-4096 生成（F3 读数）或一次 exec 的连接超时；该取舍与 `SftpPanel` 断连同档并写在头注，走查不可接受再改投递。
- **明文面**：passphrase 只活在对话框提交那一次调用栈，随 F4 的实参进 libssh 后即销毁；本件与传输腿都不持副本、不落盘、不进日志（CONN.09 不变量，判据 §6 末条）。
- **批 3 落地注记（裁决 7.109）**：在途标记在**提交时**发布（op/op_path 立即可见、UI 即时置灰），不存在「排队中」那一格的字面量——串行队列里下一件任务的标记由任务尾巴交接；快照的行模型 `key_rows()` 只把标记点在 `op_path` 对上的那一行（顶行与行内动作的措辞分工见 `keys_format` 头注）。
- 启动序**不扫盘**（零配置可用 `PREF.06`：首屏不做同步 IO）；扫盘只发生在 `keys.open` 与每次生成/导出/推送/删除/目录变更之后。
- **删除的落点**：只删「扫描面内、被识别为密钥、且当前确实在场」的路径；目录表本身不因删文件而变动（`~/.ssh` 恒在不可移，其余目录去留归用户自己管）。

## 6 验收判据映射

- **生成密钥对（①）**：ed25519 缺省、RSA 三档；passphrase 可空；产物＝同 basename 的私钥 + `.pub`；D5① 三道闸各有判据（同名拒绝、临时文件 0600、rename 就位），且**按所选目标目录适用**；**ed25519 的真生成允许进无头用例**（F8/F3：亚毫秒、无网络），断言私钥文件模式与 `.pub` 单行三字段与指纹前缀 `SHA256:`；RSA 档在用例里只判参数与计划，不跑秒级生成。
- **列表（②）**：扫盘件是「目录表 → 行表」的纯 IO 函数，用例走临时目录夹具（成对、孤儿私钥、非密钥文件、子目录、**两个目录里同名 basename** 五类干扰项，最后一项证「唯一键＝路径」）；加密两档徽标按 F8 判；孤儿私钥行的「导出公钥」腿有判据。
- **目录腿（D2③/D11）**：`key_dirs` 第七域读写往返归 `utest_config`，判的四件事＝缺省空、**逐字往返不去重**、空串元素逐个丢弃并留 `key_dirs.items[N]` 痕迹、`items` 非数组整键回落（外加既有的不含凭据子串那条审计）；**去重与「不可达条目保留」不在装载侧**，那是 `conn::normalize_key_dirs()` 的活儿（`utest_keys_model` 守），同步 IO 不进装载接缝——出稿把这两条记在本件名下是笔误，落地时改口（裁决 7.107）；面板侧判据＝**给了路径就规范化＋落盘＋重扫，空串就整条不跑且不留痕**，经 `file_dialog::headless_folder_result` 钩子证接线（同 `settings_panel` 的导出腿：只测接线不测产物）；「`~/.ssh` 不可移」有独立判据（试图移除后表内仍在）。**落地注记（裁决 7.109）**：「不留痕」的闸收在面板 `on_add_dir`——空选路整条返回、`add_dir` 不呼，装配层不再备第二道空闸（一个事实一处判）；落盘仍逐字不去重，规范化归扫盘时的 `normalize_key_dirs()`。
- **一键复制（③）**：判据交出的字节＝`type + ' ' + base64 + ' ' + comment` 单行、无尾随换行进 outbox；是否真落系统剪贴板归真机走查（7.41 同口径，本仓只判 outbox 侧）。**落地注记（裁决 7.109）**：那一行的第二字段随扫盘交出（`KeyCandidate::public_base64`，取不出的两档留空串 ⇒ 复制/推送整枚不画），「已复制」留痕由装配层 `publish_notice("keys.notice.copied")` 即时写快照——无 conn 枚举可映射，不为它发明第二套枚举。
- **推送至主机（④）**：本地判重命中即不发写并告知「已在授权表里」——判重本身是 `conn::plan_append()` 的纯逻辑（`utest_keys_model` 守），推送侧只把它映射成 `keys.push.already_authorized`；追加走 stdin，**公钥不出现在命令串**（该判据已写成 `utest_keys_push`：量两条 exec 常量里 base64 字母表的最长连续段，阈值 16 而合法最长串是 `authorized` 的 10，并以 `plan_append` 的 payload 作**正对照**防闸空转）；失败归因按代码为 `conn::PushFailure` **七档**（`DialOutcome` 四档失败一一映射 + 执行被拒／写回执异常 + 行不成立，最后一档在分配会话之前判掉，见判据 6）；取退出码走非弃用的 `ssh_channel_get_exit_state`（F15，出稿计划的定点抑制作废）。**真 sshd 成功腿待 `etest_`**（与 7.96① / 7.100 的在册欠账同一条环境约束；本机复核过无 sshd 与主机密钥），可读回的档位由 `itest_keys_push` 以拒连端口证（拨号档 + 「不成形的行不开 socket」），本棒交付面只到「无头可证」。
- **删除（D9②/D12）**：临时目录夹具里建对 ⇒ 确认后两文件皆无、列表少一行；**「⋯」只呼确认框，未确认前文件仍在**（两段式判据）；符号链接例＝拒绝且红留痕，链接与其目标都还在；**有口令私钥的删除不弹询问**（反向判据，防实现顺手加）；改名/改口令不在本期。
- **安全面（CONN.09 交界）**：配置目录明文审计那条既有自动化用例（F11）在本棒后仍绿——新增的 `key_dirs` 只是路径串、密钥产物落 `~/.ssh` 或所选目录而非配置目录、passphrase 不入 schema；`grep` 判据不因本棒新增第七域而放宽。
- **门禁与测试**：`utest_keys_model` + `utest_keys_format` + `itest_keys_panel` 全绿并入 CTest（前缀纪律，AGENTS 第 18 条）；`ctest --preset linux -E etest_` 项数在此之上 +3；`utest_config` 与 `utest_settings_catalog` 的既有例随第七域一并补/改。**批 2 到货后的实际读数**：本域已有 `utest_keys_model`／`utest_keys_format`／`utest_keys_store`／`utest_keys_push`／`itest_keys_push` 五件在册，`ctest --test-dir build -E etest_` **71 项全绿**；`itest_keys_panel` 与「+3」那条判据随批 3 到货，届时以代码读数回写不预告数字。**批 3 到货后的实际读数（裁决 7.109）**：`itest_keys_panel` 新件 **8 案**（无头真派发），`utest_keys_format` 10 → **16 案**（在途五档、顶行三选一、行内途标记点名、`public_text` 单行、阶梯次序对枚举、口令双栏一致）、`utest_keys_store` 11 → **12 案**（`public_base64` 两档），`ctest --test-dir build -E etest_` **71 → 72 项全绿**，`borealis_test_runner` 全跑 **912 案全绿**。
- **真机走查**（AGENTS 第 33 条，本稿不宣称「可用」）：卡片观感与行密度、passphrase 双栏手感、复制与推送两处留痕看不看得见、RSA-4096 生成期的置灰反馈够不够、**「＋目录」在 Win32 真选择器上的手感（Linux 腿本期是空响应，缺口单在册）**。

## 7 本期不做（延后子项）

- 改名 / 修改口令 / 把 passphrase 存入 OS 凭据库（`CONN.09` 后端任务的到货项）；**删除本期已做**（D9② 改判，细则＝D12）
- 加载到 `ssh-agent`、证书签名请求、FIDO2/安全密钥（`SSH_PKI_OPTION_SK_*` 一族，F1 同头可见但需求未列）
- 递归扫描、跟随符号链接、目录书签/树形视图（D2③ 只到「多目录各自单层」）
- `aurora::file_dialog::open_folder()` 的 POSIX 真实现（框架缺口按 AGENTS §5.1 走 Aurora 主仓分流；缺口单 `codespec/FRAMEWORK_TASK_CONN10.md` 在册、附录 A.2 的 **G41** 已登记，落地前 Linux 腿点「＋目录」等价取消）
- Windows 私钥 ACL（D5① 的 Win32 腿本期只留 WARN，待真机）
- 生成物回填某档案的 `identity_file`（档案编辑已有向导入口，联动属体验增强）
- 真 sshd 的推送成功 etest（环境欠，同 7.96①）

## 8 收口与分批

D1–D12 已于 2026-10-10 经人逐条裁完：**八条按推荐①，D2/D9 改判，改判追加 D11/D12 两条细则**（其中 **D4 worker 线程与 D5 不覆盖 + 0600 + rename 是安全与性能纪律的落点而非口味**）。收口动作＝本稿 §3 转「已裁决」+ 并入 `SPECIFICATIONS.md` §7 裁决 **7.106** + `CHANGELOG.md` 记 **v0.106** + `PLAN.md` 的 CONN.10 行回写 + 缺口单 `FRAMEWORK_TASK_CONN10.md` 出件并登记附录 A.2 的 **G41**（已于 2026-10-10 全部完成）。实现按 §4 布局分四批落：批 1 纯逻辑两件 + platform 腿 + 第七域；批 2 传输腿两件（含删除腿）；批 3 面板与装配（含目录腿）；批 4 文档回写与走查登记。

**批 1 已于 2026-10-10 落地**（裁决 7.107 记四处落地期取定/改口）：第七域 `key_dirs`（`settings.h` +
`store.cpp` 五处 + `utest_config` 补例）、`platform::create_private_file` 两腿 + `utest_file_secure`、
`conn::key_model` + `utest_keys_model` 13 案、`ui::keys_format` + `utest_keys_format` 7 案；
`ctest --test-dir build -E etest_` 由 64 项增至 **67 项**（§6 那条「+3」判据兑现），本机全跑 867 案全绿。
**未随批 1 到货的两处如实登记**：① `keys_format` 只映射已有枚举，生成三态/推送四态与
`conn::DialOutcome` 的四档失败归因随批 2 的阶段枚举到货（该枚举实测五值含 `Ok`，稿初版那句「六档」
把推送腿的「执行被拒／写回执异常」也算进了枚举名下——落地期按代码订正，裁决 7.107 ④d；
现在写占位枚举就是第二真值源，第 3 条）；
② 中文词条随批 3 面板登记进 `settings_i18n`——`utest` 只判 key，批 3 的 itest 按 label 找控件，
漏登记当场转红，不靠记忆兜底。

**批 2 已于 2026-10-10 落地**（两棒：`conn::key_store` 与 `conn::key_push` 各一次提交；裁决 **7.108**
记四处落地期改口与 §0 新增的六条读数 F12–F17）：扫盘成行、生成三道闸、导出公钥、删除一次清一对
（`utest_keys_store` 11 案，临时目录夹具、不碰网络），两段 exec 推送腿（`utest_keys_push` 2 案 +
`itest_keys_push` 2 案），`ui::keys_format` 补三家 outcome 与推送阶梯／通知（本件由 7 案增至 10 案；
`utest_keys_model` 现 16 案）。`ctest --test-dir build -E etest_` **71 项全绿**，`borealis_test_runner`
全跑（含 `etest_`）**897 案全绿**；非空转以 **12 处变异**自证——`key_push` 七处里 **6 处打红**（读腿命令可截断、丢 `umask 077`、追加变覆写、命令串带密钥段、废行闸挪到拨号之后、废行归错档），`keys_format` 五处**全红**（两档共用词条 ×2、成功档给措辞、通知优先级颠倒、缺省快照报喜）。**存活的那一处如实登记**：`report.already_authorized = true` 改成 `false` 无测试可证——它只在真 sshd 的读回腿之后才可达，本机复核无 `sshd` 与主机密钥，故「判重命中不发写」那一档与两段 exec 的成功腿一起欠 `etest_`。四处改口按代码回写本稿：⑴ 判据 6 与 §4 的「`DialOutcome` 四档失败 + 两档＝六档措辞」订正为
`conn::PushFailure` **七档**，其中「行不成立」在分配会话**之前**判掉、因此不配一个已拨号的步骤态；
⑵ §4「exit status 处沿用 tunnel_client 的定点抑制写法（F10）」按 F15 **作废**——本腿取非弃用的
`ssh_channel_get_exit_state`，7.96④ 那处仍是全仓唯一一处弃用抑制；⑶ §4 说的「生成三态」不是 `key_store`
的枚举名下之物（那里到货的是 `GenerateOutcome` 五档／`ExportOutcome` 四档／`DeleteOutcome` 四档），
「三态」指装配层快照里那一行的空闲／进行中／已回投，随批 3；⑷ `keys_format.h` 只前置声明不透明枚举、
**不 include 传输腿头**——面板经映射件够得着 `scan_keys`／`push_public_key` 的签名，就等于给「动作一律经
Hooks 交回装配层」开了一道门。**仍不称「可用」**（第 33 条）：面板一行未写，四个子项与追加的删除腿在用户侧
仍不可达；推送的两段 exec 与「已授权就不发写」缺可达证据（本机复核无 sshd 与主机密钥），待 `etest_`；
Win32 腿未编译验证。

**批 3 已于 2026-10-10 落地、批 4（本稿回写与走查登记）随之收口**（裁决 **7.109** 记七处落地期口径）：
`ui::keys_panel` 四张卡（行表／生成／推送／删除确认）＋顶行留痕，`main.cpp` 装配（单 worker 串行队列、
提交即发在途标记、`keys.open` 命令、启动不扫盘、目录腿、口令接续经 `credential_prompt`），
`settings_i18n` 词条族 73 条，conn 三处补字段（`KeyCandidate::public_base64`＋`operator==`、
`push_stage_index`——复制/推送那一行随扫盘交出、阶梯格序由枚举的主人定）。`itest_keys_panel` 新件
**8 案**（无头真派发：行表随快照、真点复制进 outbox、同名红提示不落盘、生成期置灰＋终值收框、
推送阶梯推进与失败归因、删除两段式、目录腿钩子证接线、空态与不可达留痕上屏）；
`ctest --test-dir build -E etest_` **71 → 72 项全绿**，全跑 **912 案全绿**。非空转以 **5 处变异**自证
**全红**（置灰摘除、空选路闸摘除、推送框重建判据摘除、顶行优先级摘除、行内途标记不点名路径），
另有两处**开发期被 itest 当场抓到的真缺陷**如实登记：生成提交钮的 `shared_ptr` 被 `std::move` 进节点树
后成员置空、置灰调用落空（二次提交双呼，改为只拷不挪）；空选路照常呼 `add_dir`（按裁决收闸在面板，
装配层撤掉第二道空闸）。重建触发面按件拆三档的落地口径与目录腿闸位已回写本稿 §4/§5/§6。
**仍不称「可用」**（第 33 条）：真机走查未做（§6 末条那五项在册——卡片观感与行密度、passphrase 双栏
手感、复制与推送两处留痕、生成期置灰反馈、Win32 真选择器手感）；POSIX「＋目录」等价取消
（`FRAMEWORK_TASK_CONN10.md`、附录 A.2 的 **G41** 在册）；真 sshd 推送成功腿与「判重命中不发写」
欠 `etest_`（同 7.96① / 7.100 的环境欠账）；Win32 腿未编译验证。
