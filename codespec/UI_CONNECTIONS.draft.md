# 连接管理器 UI 设计稿（`SPEC.FEAT.CONN.07`）— 评审稿

> **状态**：评审稿已收口（2026-10-09）——§3 的 D1–D7 与 §8 五项待决**均已裁决**；
> **D3 由「临时通道」改判为「libssh 真传输」**（CONN.02 由此进本期），新增 **D8**（依赖经 vcpkg 获取）。据此转实现。
> **配套高保真草图**：`codespec/UI_CONNECTIONS.draft.svg`（本稿 §2 的 1–6 标号判据对应其底部标注；SVG 为事实来源，后续改图直接改 SVG）。
> **消费的数据模型**：`SPEC.FEAT.CONN.03` 已落（`include/borealis/conn/profile.h` 的 `Profile`/`ProfileStore`/`parse_ssh_config` ＋ `config::Store` 第五域 `profiles`，裁决 7.93）。
> **本稿不碰代码**：按 AGENTS.md 第 33 条「UI 编写前先出设计图评审」，本稿是实现前置，评审通过后才落 `src/ui/`。

## 1 范围与边界

**本期交付**：连接管理器**侧栏**——档案树（分组 / 收藏 / 搜索）、Quick connect、新建向导（分步表单）、最近连接、以及「导入 `~/.ssh/config`」入口；消费已落的 CONN.03 模型。

**不本期交付**（详见 §7，均为独立任务或延后子项）：真实 SSH 传输（CONN.02）、真实 OS 凭据后端（CONN.09 libsecret/Keychain/CredMan）、SFTP（CONN.04）、隧道（CONN.08）、串口 / Telnet（CONN.05/06）、快捷片段 snippets（CONN.07 明确延后）、ProxyJump（CONN.03 延后）。

**关键前提**：真实 SSH 传输未建，故本期「连接」走**临时通道**（见 D3），UI 形态与查询逻辑与真传输到货后一致，仅 connect handler 在 CONN.02 落地时替换。

## 2 草图与判据对应

草图「屏 2 · 连接与会话侧栏」底部 1–6 标号判据：

| 标号 | 判据 | 对应规格 |
|---|---|---|
| 1 | 侧栏＝档案树＋搜索＋新建；类型图标区分连接种类；条目右侧星标为收藏 | `CONN.07` |
| 2 | Quick connect：`host[:port]` 临时输入即连，不生成档案 | `CONN.03` |
| 3 | 最近连接：五条定长列表，点击即连；与撤销关闭标签（WS.10）栈分开维护 | `CONN.07` |
| 4 | 快捷片段 snippets：标注为**延后子项** | `CONN.07`（M4） |
| 5 | 分步表单：步骤条可回退；类型卡决定后续字段集，四类共用同一向导壳 | `CONN.07` |
| 6 | （其余标号对应分组视图 / 导入入口 / 凭据提示，评审时据 SVG 补齐） | `CONN.03` / `CONN.09` |

## 3 裁决（D1–D7）

| # | 待决 | 选项 | 结论 | 理由 |
|---|---|---|---|---|
| **D1** | 新控件类放哪 | ① `src/ui/connection_sidebar.{h,cpp}` 私有头；② 进 `include/borealis/ui/` | **①** | 裁决 D1①：绘制侧控件含 `au::` 类型，按既有先例（`terminal_view.{h,cpp}`、`search_overlay.{h,cpp}`、`settings_panel.{h,cpp}`）走私有头，不污染公共头 |
| **D2** | 侧栏怎么打开 | 经 `CommandRegistry` 注册 `connections.toggle`（同族 `settings.open` / `search.open`），绑定侧栏显隐＋快捷键；左侧停靠件，非模态浮层 | **注册命令** | 复用现有命令/快捷键体系，不新造入口；`CommandRegistry::all()` 已被 `settings_panel` 消费，机制现成 |
| **D3** | 「连接」动作 | ① Local→本地终端；SSH→本地终端＋自定义命令 `ssh [user@]host [-p port]`（临时通道）；② 本期就搭 libssh 真传输 | **②（libssh 真传输，2026-10-09 改判）** | 初判①复用已落能力最快；改判②：临时通道不能自定义认证与 known_hosts 策略，CONN.04/08/WS.05 全部以真传输为前提，早晚要建；依赖已定走 vcpkg（D8）后②的阻塞解除。CONN.02 由此进本期 |
| **D4** | 最近连接存储 | ① 持久化：`ConnectionSettings` 增 `recent_profiles`（capped 5，存 id＋时间戳），走 `config::Store`；② 仅内存、重启清空 | **①（建议，待决）** | 用户体验要求跨重启保留；① 需动 `store.cpp` 读写＋测试，属可控改动。最终取舍见 §8-Q2 |
| **D5** | 导入入口映射 | 读盘 `~/.ssh/config`→`parse_ssh_config()`→逐条 `ProfileStore::add`（`ssh-import:<pattern>` 确定性 id 天然去重/变更检测）；单向不回写 | **如述** | 模型层已就绪（`parse_ssh_config` 纯函数、不碰文件）；调用方负责读盘与「导入不回写」语义 |
| **D6** | 类型覆盖 | 模型仅 `Local`/`Ssh` 两型；草图四类中的串口/Telnet 行在 M4（CONN.05/06）到货前**灰置/隐藏**，不占位 | **仅 Local+SSH** | 不凭空造未实现的连接类型；灰置避免承诺未到货能力 |
| **D7** | 分组/收藏/搜索呈现 | 三视图「全部 / 收藏 / 按分组」；搜索框调 `ProfileStore::search`；星标切 `favorite`；分组用 `groups` 字段。UI 只消费，查询已在模型层就绪 | **如述** | 把查询逻辑留在纯逻辑模型（已单测），UI 不重算 |

## 4 文件布局 / TU 拆分

```
src/ui/connection_sidebar.h        新增 —— 私有头（D1①），含 au:: 类型；侧栏容器与档案树/搜索/最近/导入
src/ui/connection_sidebar.cpp      新增 —— 装配件，唯一触达 au::Widget/au::Painter 的翻译单元（架构 §9.2 纪律）
src/ui/connection_wizard.h         新增 —— 私有头；新建/编辑分步表单（类型卡＋字段集）
src/ui/connection_wizard.cpp       新增
src/ui/credential_prompt.h         新增 —— 私有头；ask_every_time 原生询问对话框（Q3，取值只喂本次认证回调）
src/ui/credential_prompt.cpp       新增
src/conn/ssh_connection.h          新增 —— 私有头（含 libssh 类型，不入 include/）；`SshConnection : session::Connection`（D3②）
src/conn/ssh_connection.cpp        新增 —— 连接/认证/PTY 通道/尺寸下发/关闭；libssh 阻塞模型 + 独立读线程
src/main.cpp                       修改 —— 注册 `connections.toggle` 命令、持有侧栏、接线 connect handler（Local→本地终端腿，SSH→SshConnection）
src/CMakeLists.txt                 修改 —— 上述 .cpp 编入 `borealis_core`；`find_package(libssh CONFIG)` + 链接 `ssh`
tests/unit/utest_connection_*.cpp  新增 —— 向导字段集/校验、导入去重、SshConnection 参数拼装与状态机（纯逻辑，可无头）
```

> 注：不改变 `include/borealis/**` 任何公共头；不新增框架缺口（向导/侧栏均为「公共 API 可组合出来的体验」）。

## 5 数据流

```
config::Store（第五域 profiles）
      │  LoadReport 还原
      ▼
std::vector<conn::Profile>  ──►  ProfileStore（内存查询视图：find/by_group/favorites/search/add/update/remove）
      │                                  │
      │                                  │ UI 只读消费（D7）
      │                                  ▼
      │                       ConnectionSidebarView（档案树/搜索/收藏/分组/最近/导入按钮）
      │                                  │ 用户动作
      │                                  ├─ 新建/编辑/删除 ──► ProfileStore 变更 ──► config::Store::replace() 落盘
      │                                  ├─ 导入 ──────────► parse_ssh_config() ──► ProfileStore::add（去重）
      │                                  └─ 连接 ──────────► connect handler（D3）
      │                                                   ├ Local → LocalTerminalSpec
      │                                                   └ SSH   → LocalTerminalSpec{ command_line = build_ssh_command(ssh) }
      ▼
Session（本地终端腿，forkpty/ConPTY；真 SSH 传输待 CONN.02）
```

**凭据（CONN.09 衔接）**：SSH 档案的 `SecretHandle` 经 `CredentialStore` 取回——本期运行期用 `InMemoryCredentialStore`（测试）/`UnavailableCredentialStore`（降级询问哨兵），真实 OS 后端留独立任务。若句柄为 `ask_every_time`，见 §8-Q3 的询问 UI 待决。

## 6 验收判据映射

- **CONN.07**：侧栏含档案树＋搜索＋新建向导（分步、类型卡定字段集）；最近连接列表（5 条、与 WS.10 栈分离）。
- **CONN.03**：分组/收藏/搜索（由 `ProfileStore` 支撑）；Quick connect（临时 host:port 即连、不入档案）；`~/.ssh/config` 只读导入（变更检测由确定性 id 去重实现，单向）。
- **CONN.09（衔接）**：UI 不持明文；口令/私钥 passphrase 只经 `SecretHandle` 引用或询问哨兵，落盘结构（经 `credential` 键）避开明文审计。
- **回归**：`utest_profile`（7）与 `utest_config` 第五域往返（22）保持全绿；新增向导/ssh 命令/导入去重单测。

## 7 本期不做（延后）

- snippets（快捷片段，CONN.07 明确 M4）
- ProxyJump（CONN.03 延后）
- 真实 OS 凭据后端 libsecret/Keychain/CredMan（CONN.09，独立任务；libssh 认证所需的取值路径已就绪，仅存储后端延后）
- SFTP（CONN.04）、隧道（CONN.08）、串口/Telnet（CONN.05/06）
- SSH 会话级重连与跳板语义（WS.05 到货时基于 SshConnection 扩展）

## 8 裁决记录（2026-10-09 收口，原「评审待决」）

- **Q1（D3）＝libssh 真传输**：临时通道不能自定义认证与 known_hosts 策略，且 CONN.04/08/WS.05 均以真传输为前提；依赖阻塞经 D8 解除。CONN.02 进本期。
- **Q2（D4）＝持久化进配置**：已落地（`RecentConnection` + `push_recent_connection`，提交 `9b69f95`，`utest_recent` 4 例）。
- **Q3（凭据询问）＝原生询问对话框**：libssh 进程内认证没有「系统 ssh 提示」可复用（原 A 选项只在临时通道下成立）；取值只喂本次认证回调，不落盘不进日志。
- **Q4（快捷键）＝走 PREF.04**：经 `CommandRegistry` 注册 `connections.toggle` 进快捷键体系，默认不绑死键位。
- **Q5（默认显隐）＝默认隐藏、命令/快捷键唤起**：不占主窗口版面，与 settings_panel 同款交互。
