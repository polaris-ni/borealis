# CONN.09 真实 OS 凭据后端（设计稿）

> `SPEC.FEAT.CONN.09` 的余腿：凭据模型与询问降级已落（裁决 7.93／7.94），本稿接的是
> 「经 OS 凭据库存储（Windows Credential Manager / Linux libsecret / macOS Keychain）」。
> 判据文见 `SPECIFICATIONS.md` 的 CONN.09 行与 AGENTS.md §4.5 第 24 条。
> 日期：2026-10-22。目标裁决号：7.132；目标版本：v0.132。

## 0 地基实测

| 编号 | 读数 | 证据 |
|:---|:---|:---|
| F1 | 本机有 `libsecret-1.so.0` 运行库，无开发头；`gnome-keyring-daemon` 在跑，`org.freedesktop.secrets` 由它持有 | `ldconfig -p`；`busctl --user list` |
| F2 | Secret Service 有两个集合：`session` 与 `login`；`login` 是 `default` 别名且**未锁** | `busctl --user call ... ReadAlias`；`get-property ... Locked` |
| F3 | 不装开发包也能用：`dlopen("libsecret-1.so.0")` + 自声明 ABI，`secret_password_store_sync` → `lookup_sync` → `clear_sync` 三步成功 | `/tmp/secret_probe.c` 一次性探针，未入库 |
| F4 | `SecretSchema` 的 ABI 是 `name`、`flags`、`attributes[32]`、`reserved`、七个保留指针；`secret_password_*_sync` 的形参次序见 libsecret 0.21.7 头文件 | 下载 `libsecret-1-dev` 的 `.deb` 解包读头，未安装 |
| F5 | 空字符串口令与 UTF-8 口令都能往返；`lookup_sync` 对「命中空口令」回**非空指针的空串**，对「未命中」回 `NULL` | 探针第二版 |
| F6 | 仓库当前没有任何生产代码构造 `SecretHandle::reference`；`connect_profile` 对 Reference 与 AskEveryTime 一律询问 | `src/main.cpp`、`src/conn/profile.cpp` |
| F7 | `CredentialStore` 只有 `available`／`store`／`retrieve` 三入口；两个替身分别覆盖内存与「永远不可用」 | `include/borealis/conn/profile.h` |
| F8 | 配置目录审计用例已存在，但只扫 `borealis.json` 及其快照／导出件，且只查**键名**，不查值；不含 `windows.json` 与 `workspace.json` | `tests/unit/utest_config.cpp` 的 `no_file_written_into_the_config_directory_carries_a_credential_name` |
| F9 | 本沙箱无 MSVC／Win32 头；CredMan 腿只能按公开 API 形状写，标「推断／未验证」 | 与既有 Win32 腿同档 |

## 1 范围

**做**：

1. 平台层新增真实 `CredentialStore` 两腿：POSIX（libsecret）与 Win32（CredMan）。
2. 句柄编码与解析件；`CredentialStore` 补 `erase`（删除档案所持秘密的能力，测试清理也用它）。
3. 装配层：连接档案时先按 Reference 句柄取料，命中即连、未命中才询问；询问提交后把料存进
   OS 库并回写档案句柄。
4. 配置目录审计扩到三枚 `Preferences`，按键名与**明文哨兵值**双查，并自证扫描非空转。

**不做**（在册，见 §7）：

- macOS Keychain（本机无法构建）。
- 隧道、密钥导出／推送、会话恢复三处询问腿的「存取回写」；它们本轮仍按每次询问降级。
- 档案删除时的秘密清理（当前没有删除档案的界面路径）。
- 句柄跨机迁移（OS 库是本机设施，`vault_id` 不保证跨平台可解）。

## 2 形态

### 2.1 平台工厂与私有头

`src/platform/credential_store.h`（私有头，不进公共面）：

```cpp
[[nodiscard]] auto make_credential_store() -> std::unique_ptr<conn::CredentialStore>;
[[nodiscard]] auto credential_vault_id(std::string_view prefix,
                                       std::string_view profile_id) -> std::string;
[[nodiscard]] auto credential_vault_profile(std::string_view vault_id,
                                            std::string_view prefix) -> std::optional<std::string>;
```

- 前两件由 `src/platform/credential_store.cpp` 提供，纯字符串、可单测。
- 工厂按平台编入 `src/platform/posix/credential_store.cpp` 或 `src/platform/win/credential_store.cpp`。
- 前缀：POSIX `libsecret:`、Win32 `credman:`。`vault_id` 只含档案 id，不含任何秘密。

### 2.2 POSIX 腿

- 构造时 `dlopen("libsecret-1.so.0", RTLD_NOW)`，解析
  `secret_password_store_sync`／`lookup_sync`／`clear_sync`／`secret_password_free`，
  以及 `libglib-2.0.so.0` 的 `g_error_free`。任一缺 ⇒ `available() == false`，
  `store` 回 `AskEveryTime`、`retrieve` 回空。
- 自声明最小 ABI（`SecretSchema` 等），不引入构建期依赖；**ABI 依据是 F4 的实际头文件**。
- schema 名 `org.borealis.Connection`，唯一属性 `profile_id`；集合取 `default`。
- `store` 成功 ⇒ `Reference{libsecret:<profile_id>}`；失败 ⇒ 记 WARN（只带档案 id 与
  GError 文案）后回 `AskEveryTime`，**不保存**。
- `retrieve` 只认本腿前缀；命中空串也回 `optional{""}`（F5 的区分是承重的）。
- `erase` 调 `clear_sync`；前缀不符回 false。
- 线程：`dlopen` 句柄只读；同步调用可跨线程，装配层仍把它放到后台任务里跑（§3 D8）。

### 2.3 Win32 腿

- `CredWriteW`／`CredReadW`／`CredDeleteW`（`CRED_TYPE_GENERIC`，
  `CRED_PERSIST_LOCAL_MACHINE`，目标名 `borealis/<profile_id>`），
  blob 为秘密的 UTF-8 字节，不经宽字符。
- 目标名经 `platform::win_text.h` 的 `to_wide` 折宽；空 id 或含 NUL 拒绝。
- blob 上限取 `CRED_MAX_CREDENTIAL_BLOB_SIZE`（5120 字节），超限记 WARN 回 `AskEveryTime`。
- 本沙箱无 MSVC，**未编译未验证**；链接面所需 `advapi32` 与既有 Win32 库同一行补。

### 2.4 `CredentialStore::erase`

```cpp
[[nodiscard]] virtual auto erase(const SecretHandle &handle) -> bool = 0;
```

三个实现：内存替身从 `vault_` 擦除；不可用替身回 false；两腿按前缀调底层删除。
理由：测试往返必须能收走自己写进用户钥匙环的条目；否则每次跑 `etest_` 都在真实
钥匙环里留垃圾。生产删除档案的路径当前不存在，故只登记能力、不接线。

## 3 决策

| 编号 | 决策 | 理由与代价 |
|:---|:---|:---|
| D1 | **dlopen 而不是构建期链接 libsecret** | 本机无开发头（F1），且引入 vcpkg 的 glib/libsecret 会显著拖慢 Linux 构建；代价是 ABI 自声明，须在注释里钉住依据版本（F4），并在升级时重核 |
| D2 | 句柄只编码档案 id，秘密不参与 | `vault_id` 会落进 `borealis.json`，必须是可公开的引用；秘密只在 `store` 调用栈里存在 |
| D3 | 空口令与「取不到」分开 | 空 passphrase 是合法输入；`optional{""}` 与 `nullopt` 两条分支在询问件里语义不同（F5） |
| D4 | `available()` 只表示「符号齐备」，不探活服务 | 探活要在启动路径做一次 D-Bus 往返；真正的失败由 `store`／`retrieve` 当场降级，不另设缓存真值源 |
| D5 | 存储失败不写明文、不改档案句柄 | 需求红线；失败后下一次连接仍询问 |
| D6 | 询问提交后**先乐观写句柄、再异步入库** | 句柄是确定性的（D2），无需等库回话；入库失败时下一次 `retrieve` 落空即回询问，不会误用旧秘密 |
| D7 | 只接线「档案连接」这一条生产腿 | 隧道／密钥／恢复三条询问腿的输入不是持久档案或没有句柄字段（§7）；一次只改一个意图，避免半截实现 |
| D8 | `retrieve` 与 `store` 放后台任务 | AGENTS.md §4.5 第 25 条：钥匙环可能弹解锁框并阻塞；事件回调只发起任务，结果回主线程再开标签或询问 |
| D9 | 审计用例扩到三枚 `Preferences` + 明文哨兵 | F8 的现状只查键名与一份文件；本条需求原文是「全配置目录 grep 无任何明文凭据」 |
| D10 | 审计用例自带「种一份泄漏再抓」的自证段 | 防止扫描器只扫空集合；与 AGENTS.md §4.4 第 22 条同一条纪律 |

## 4 装配接线

### 4.1 连接档案

`connect_profile` 的 SSH 分支改为：

1. `ask == None` ⇒ 照旧直连。
2. `handle.kind == Reference` 且前缀非空 ⇒ 后台 `retrieve`；命中 ⇒ `open_ssh_tab(profile, secret)`。
3. 未命中／不可用／AskEveryTime ⇒ 挂 `pending_profile` 并询问。

后台结果的回投用 `au::async(...).then(...)`（Application 已装主线程投递器）。回调以
`credential_prompt` 的 `weak_ptr` 作存活性哨兵；同一档案 id 在途时忽略重复点击。

### 4.2 询问提交

`pending_profile` 与 `pending_restart` 两支在拿到明文后：

1. 若库 `available()`，把档案的 `ssh.credential` 置为确定性 Reference 并 `store.replace()`
   落盘；
2. `au::async` 把明文存进 OS 库（失败只 WARN）；
3. 照旧用明文开连接／重拨。

不存的情况：档案不在 `store.settings().profiles` 里（quick connect、临时档案）、
库不可用、明文来自隧道／密钥／恢复腿。

## 5 测试

| 层 | 套件 | 判据 |
|:---|:---|:---|
| 纯逻辑 | `utest_credential_store`（新） | 前缀编码／解析往返；非法前缀回空；`InMemoryCredentialStore` 的 `erase`；句柄不含秘密 |
| 纯逻辑 | `utest_profile_views`（扩） | 凭据计划四档：None／Stored／Ask（Reference 未命中）／Ask（AskEveryTime）；Reference 但库不可用 ⇒ Ask |
| 真后端 | `etest_libsecret_credential_store`（新，POSIX） | 真 Secret Service 上 store→retrieve 逐字节相等；覆盖写；空串；`erase` 后取不到；错误前缀不命中。Windows 侧由 CMake 过滤掉该文件 |
| 审计 | `utest_credential_audit`（新） | 三枚 `Preferences` 同目录落盘后，全目录逐文件扫：禁列键名 + 明文哨兵值；先种一份泄漏证明扫描能抓到，再删掉证明生产产物干净 |

## 6 分批

- **批 0（本稿）**：地基实测与设计稿，单笔 docs 提交。
- **批 1**：平台两腿 + `erase` + 编码件 + 单测 + `etest_libsecret_credential_store`。
- **批 2**：凭据计划件 + 装配接线（连接与询问回存）。
- **批 3**：审计扩案、变异自证、四处文档回写与结项。

## 7 边界与在册

- macOS Keychain 未实现（本机无构建环境）。
- Win32 CredMan 腿未编译、未实测；一切 Windows 侧说法标「推断／未验证」。
- 隧道、密钥导出／推送、会话恢复、SFTP 重拨仍走「每次询问」；其中恢复腿在下一轮可复用
  `plan_credential`，本轮不接。
- 句柄是本机设施：把 `borealis.json` 搬到另一台机器后，Reference 取不到即自动回询问；
  这是 fail-closed，不是缺陷。
- 没有「清除已存秘密」的界面入口；`erase` 目前只被测试使用。
- 真机走查（钥匙环弹窗、锁定集合下的询问手感）本轮仍不做，故 CONN.09 不称「可用」。
