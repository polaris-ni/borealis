# SSH 隧道管理 UI 设计稿（`SPEC.FEAT.CONN.08`）— 评审稿

> **状态**：评审稿（2026-10-09）——§3 的 D1–D9 逐条给推荐裁决，**待评审收口后转实现**（AGENTS.md 第 22 条 UI 件节奏：先出设计图评审，稿入 `codespec/`）。
> **配套高保真草图**：`codespec/UI_TUNNEL.draft.svg`（本稿 §2 的 1–6 标号判据对应其底部标注；SVG 为事实来源，后续改图直接改 SVG）。
> **已落地的前置**：纯逻辑 `src/conn/tunnel_model.{h,cpp}`（定义校验/同侧冲突/五态状态机/指数退避，`utest_tunnel_model` 6 例）、SOCKS5 纯解析 `src/conn/tunnel_socks.{h,cpp}`（5 例）、传输腿 `src/conn/tunnel_client.{h,cpp}`（每隧道自开会话 + 工作线程串行受理 + 退避重连，`itest_tunnel_failure` 2 例）、平台 TCP 腿 `src/platform/tcp.h` 双实现（`utest_tcp_loopback` 4 例）。裁决 7.96 在册。
> **本稿不碰代码**：评审通过后才落 `src/ui/`。

## 1 范围与边界

**本期交付**：隧道管理界面——隧道列表（定义行 + 状态提示）、逐条启动/停止、新建/编辑对话框（三式表单 + 校验与冲突提示）、删除（运行中先停）、`-R` 择定端口回报显示、失败退避的过程可见（原因 + 第几次重试）。隧道定义的持久化随本稿裁决（D3）。

**不本期交付**（§7）：开机自动启动、逐条重试策略编辑、隧道流量统计、并发多连接受理（裁决 7.96② 延后子项）、真转发成功腿 etest（待 sshd 环境，裁决 7.96 代价① 在册）。

**关键前提（需求原文）**：「隧道独立于终端会话管理（可开隧道不开终端）」——面板**不挂在任何 SSH 标签上**，与 SFTP 面板（随标签生灭，裁决 7.95 D8）是相反的生命周期形态；每隧道自开独立会话（裁决 7.96①），列表启停不要求任何标签存在。

## 2 草图与判据对应

草图主屏（无任何 SSH 标签打开时的隧道面板，五态各有实例）+ 一处浮层（新建/编辑对话框），底部 1–6 标号判据：

| 标号 | 判据 | 对应需求 |
|---|---|---|
| 1 | 入口与形态：`tunnels.open` 命令呼出的**左侧停靠卡片**（非模态、默认隐藏），不依赖任何标签——无 SSH 标签时可启停 | `CONN.08`（D1 拍形态） |
| 2 | 列表行两行堆叠：首行 名称 + 形态徽标（`-L`/`-R`/`-D`）+ 状态徽标 + 启停按钮；次行 监听点 → 目标 + 承载档案 | `CONN.08`（D2 拍形态） |
| 3 | 状态提示五态色：Stopped 灰 / Dialing 蓝 / Active 绿 / Backoff 琥珀 / Failed 红；Backoff 行内显示原因与「第 N 次重试 · Xs 后」，Failed 显示终态原因（`TunnelError` 映射文案） | `CONN.08`「失败自动重试与状态提示」 |
| 4 | `-R` 的 0 端口：配置显示「由服务器择定」，Active 后行内换成回报的实际端口（`TunnelEvents::bound_port`） | `CONN.08` |
| 5 | 新建/编辑对话框：三式类型卡决定字段集（Dynamic 隐去 target 两栏并置灰）；校验走 `validate_tunnel_spec` + `specs_conflict`，冲突行内红提示 | `CONN.08`（D5 拍归属） |
| 6 | 删除与停止：运行中隧道删除前先呼「停止并删除」确认；停止即 `stop()`（join 上界一个轮询拍 + 一次建连超时，传输腿既定纪律） | `CONN.08`「列表启停」 |

## 3 裁决（D1–D9，均给推荐，待评审收口）

| # | 待决 | 选项 | 结论（推荐） | 理由 |
|---|---|---|---|---|
| **D1** | 面板形态 | ① 独立左侧停靠卡片（`tunnels.open` 命令唤起，宽 520 dp，与连接侧栏同族不同件）；② 连接侧栏内第二层页签（档案 ⇄ 隧道）；③ 设置面板新页 | **①** | ② 的侧栏 320 dp 放不下「监听→目标+档案+状态」的信息量，且档案与隧道生命周期不同（隧道不随连接动作生灭）；③ 设置页是模态编辑形态，承载不了运行态实时刷新。① 保持「隧道是独立管理层」的需求语义，入口与 `connections.toggle`/`sftp.toggle` 同族 |
| **D2** | 行形态 | ① 两行堆叠（首行名称+徽标+状态+按钮，次行监听→目标+档案）；② 单行多列表格 | **①** | 520 dp 内单行放不下五列且端口串长（`127.0.0.1:5432 → db01.internal:5432`）；两行堆叠与侧栏档案行、SFTP 条目行同族，行高一致 |
| **D3** | 持久化 | ① `config::Settings` 第六域 `tunnels`（`std::vector<conn::TunnelSpec>`，`store.cpp` 读写往返 + `utest_config` 补例 + `settings_catalog` 白名单登记）；② 本期不落盘（内存态） | **①** | 「列表启停」的管理对象若重启即丢则形同虚设；`TunnelSpec` 全字段是纯值（无凭据，凭据只经 `profile_id` 引用，天然过 CONN.09 明文审计）。裁决 7.96 代价④「持久化形态归 UI 切片裁决」即指本条 |
| **D4** | 编辑形态 | ① 独立对话框（`au::Dialog` 自建 content，同族 `connection_wizard` 先例）；② 行内就地编辑 | **①** | 字段有形态联动（Dynamic 隐 target）与逐字段校验，行内编辑放不下；向导先例的「类型卡→字段集」结构直接复用 |
| **D5** | 启停归属 | ① 装配层持有 `map<id, unique_ptr<conn::Tunnel>>` + 事件 sink，面板只经 Hooks（start/stop/load/persist）进出；② 面板自持隧道对象 | **①** | 面板不触达 libssh/socket（口径同 SFTP 面板「本件不认识 libssh」，D1① 私有头先例的接缝纪律）；隧道要活过面板开关（关面板不停隧道），对象所有权必须在装配层 |
| **D6** | 状态泵 | ① `TunnelEvents` sink 写每隧道的原子快照（state/error/bound_port/attempt），面板在既有 `on_frame` 里逐行读快照合成行表（只留最新值）；② 有界消息队列逐事件投递 | **①** | 状态是**可覆盖的量**（最新值即全部事实），队列形态反而要处理积压合并；AGENTS §4.5 第 25 条「交换合成后的最终值」在原子快照下天然成立，`Tunnel::state()` 本就是原子读、`bound_listen_port()` 同。`attempt`（第几次重试）由 sink 在 Backoff 事件上自增 |
| **D7** | 凭据链 | 启动时按 `spec.profile_id` 从档案表解析 `SecretHandle`：OS 句柄/内存态直接取值；`ask_every_time` 弹 `credential_prompt`（先例已落）。明文只进 `conn::Tunnel` 的内存副本、随 stop 清空 | **如述** | 裁决 7.96⑥ 已定驻留口径（自动重试需重复拨号的必然代价），本条只是把它接到 UI；不落盘不进日志（CONN.09） |
| **D8** | 开机自动启动 | ① 本期不做，登记延后；② `TunnelSpec` 加 `autostart` 字段并随启动恢复 | **①** | 需求原文只写「列表启停、失败自动重试与状态提示」，未要求开机恢复；② 涉及启动时序与凭据询问的阻塞面（ask_every_time 档案无法静默重拨），独立立项更干净 |
| **D9** | 重试策略 | ① 全局缺省 `RetryPolicy{base 1s, cap 30s, 不限次}`（模型现缺省），逐条可配延后；② 对话框暴露三字段 | **①** | 需求只说「失败自动重试」；退避参数是运维调优项，首版给一档缺省即可，Failed 终态仍可由人再启（模型 `max_attempts==0` 恒可重试的既定口径） |

## 4 文件布局 / TU 拆分

```
src/ui/tunnel_panel.h              新增 —— 私有头（D1① 先例：connection_sidebar 同款）；含 au:: 类型
src/ui/tunnel_panel.cpp            新增 —— 停靠卡片装配件：列表行/启停/删除确认/编辑对话框呼出
src/ui/tunnel_format.{h,cpp}       新增 —— 纯函数小头（无 au:: 类型，先例 sftp_format）：
                                         五态→文案档、TunnelError→文案、行模型 tunnel_rows()、
                                         监听点/目标串折叠（-R 0 端口「由服务器择定」形态）
src/main.cpp                       修改 —— 注册 tunnels.open 命令（同 connections.toggle 族）、持有
                                         map<id, Tunnel> + 原子快照 sink（D5/D6）、D7 凭据链接线
include/borealis/config/settings.h 修改 —— Settings 第六域 tunnels（D3）
src/config/store.cpp               修改 —— tunnels 域读写与往返
src/CMakeLists.txt                 修改 —— 上述 .cpp 编入 borealis_core
tests/unit/utest_tunnel_format.cpp 新增 —— 文案映射/行模型/端口折叠无头单测
tests/integration/itest_tunnel_panel.cpp 新增 —— 无头真派发：行表随快照刷新、启停按钮呼 Hooks、
                                         冲突红提示、删除确认两段式
tests/unit/utest_config.cpp        修改 —— 第六域往返补例
```

> 不改 `include/borealis/**` 其余公共头；无新框架缺口（面板全部用 Aurora 公共控件组合，Dialog/Scroll/Button/TextInput 先例齐备）。`utest_settings_catalog` 的白名单登记随 D3 一并补。

## 5 数据流与线程模型

```
config::Store（第六域 tunnels，D3）
      │ 装载整表 vector<TunnelSpec>
      ▼
装配层（main.cpp）
      ├─ TunnelRunner：map<id, unique_ptr<conn::Tunnel>>（D5）
      │     start(spec) ──按 profile_id 解析凭据（D7，必要时弹 credential_prompt）
      │     │            ──► conn::Tunnel(profile, secret, spec) .start(sink)
      │     └─ 每 Tunnel 一个 Sink{atomic<TunnelState>, atomic<TunnelError>,
      │                            atomic<int> bound_port, atomic<int> attempt}（D6）
      │  工作线程 ──on_tunnel_state──► Sink 写快照（只留最新值，AGENTS §25）
      ▼
TunnelPanel（UI 线程：on_frame 读快照 → tunnel_rows() 合成 → 行区重建/局部刷新）
      │ 用户动作（启停/新建/编辑/删除）经 Hooks 交回装配层
      └─ 定义变更（增/删/改）── persist ──► config::Store::replace() 落盘
```

- 面板关闭 ≠ 隧道停止：Sink 与 Tunnel 对象都在装配层，面板只是投影（D5 的根据）。
- UI 线程零阻塞 IO（AGENTS §25）：启停动作里 `stop()` 的 join 上界（200 ms 轮询拍 + 一次建连超时）在**装配层工作侧**消化——首版取舍：`stop()` 直接在 UI 线程调用（与 `SftpPanel` 析构断连同档，秒级最坏情形罕见且已有注释在册），若走查不可接受再改投递。
- 冲突校验在**提交时**做（`has_conflict` 整表闸），列表刷新不重复算。

## 6 验收判据映射

- **列表启停**：每行一枚启停按钮，状态徽标随快照刷新（D6）；停止即 `Tunnel::stop()`。
- **失败自动重试与状态提示**：Backoff 行内「原因 + 第 N 次重试 · Xs 后」（`retry_delay_ms` 现算），Failed 显示终态原因；重试穷尽缺省不存在（D9① 不限次）。
- **可开隧道不开终端**：面板不持任何标签引用；草图主屏即「零 SSH 标签」态。
- **三式覆盖**：`-L`/`-D` 本端监听、`-R` 远端监听（0 端口择定回报，判据 4）；编辑对话框三卡形态决定字段集（判据 5）。
- **持久化**：`utest_config` 第六域往返 + `settings_catalog` 白名单；配置目录明文审计用例（CONN.09）覆盖 tunnels 域（只存 `profile_id` 引用）。
- **测试**：`utest_tunnel_format`（新增）+ `itest_tunnel_panel`（新增）全绿；面板真机走查按 AGENTS 第 33 条在实现后执行（未走查不得称「可用」）。

## 7 本期不做（延后）

- 开机自动启动（D8①）；逐条重试策略编辑（D9①）
- 隧道流量统计 / 连接数计数（需求未提）
- 并发多连接受理（裁决 7.96② 延后子项，随 CONN.02 连接复用一并评估）
- `-R` 服务器择定端口的主动查询（只在 Active 事件回报值，不二次探测）
- Windows/MSVC 腿验证（与既有口径一致，待真机）

## 8 评审待决汇总

D1–D9 逐条推荐如上；若均按推荐裁决，收口动作＝本稿 §3 改「已裁决」+ 并入 `SPECIFICATIONS.md` §7 新裁决 + `CHANGELOG.md` 记版本 + `PLAN.md` CONN.08 行回写，然后按 §4 布局落 `src/ui/`。
