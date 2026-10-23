# 快捷片段 UI 设计稿（`SPEC.FEAT.CONN.07` 的 snippets 延后子项）

> **状态**：设计稿待评审（2026-10-23）。按 `AGENTS.md` §4.2 第 11 条，评审收口后以裁决形式并入
> `SPECIFICATIONS.md` §7，再按 §4 分批转实现。当前**未动代码**。
> **需求原文**（`SPECIFICATIONS.md` §2 `SPEC.FEAT.CONN.07`）：「快捷片段（snippets，Termius/XShell
> 语义）：常用命令片段库（分组/搜索/参数占位），单击或快捷键发送至当前会话（延后子项）」。
> **已落地的前置**：连接侧栏与档案存储（`SPEC.FEAT.CONN.03`，裁决 7.94）、左停靠卡片面板族
> （`tunnels.open`／`keys.open`／`sftp.toggle`）、`Session::send_text` 的统一发送路径（`SPEC.FEAT.TERM.09`）、
> 配置七域与导出/导入同一条读侧（`SPEC.FEAT.PREF.07`）、命令层与快捷键层（`SPEC.FEAT.WS.07`）。
> **配套草图**：本期不出高保真 SVG，形态直接复用密钥面板的 520 dp 左停靠卡片；屏面判据见 §2。

## 0 本稿的实测地基（本机 Linux 腿，2026-10-23，读源与既有测试；未新增运行时探针）

| # | 实测事实 | 读数 | 对本稿的影响 |
|---|---|---|---|
| F1 | 仓内 `grep -rn "snippet"` 在 `src/`、`include/`、`tests/` 零命中 | 无既有实现 | 本期是全新增量，不存在迁移旧形态 |
| F2 | `config::Settings` 现为七域；结构域 `profiles`/`tunnels`/`key_dirs` 统一落成顶层对象 `{ "items": [...] }` | `include/borealis/config/settings.h`、`src/config/store.cpp` | snippets 作第 8 域，沿用同一形态，`schema_version` 不升 |
| F3 | `Session::send_text(std::u32string_view)` 的头注明写「键入、输入法 commit、粘贴与快捷片段都走这里，不得绕过去直接发字节」，并做**一处**行尾折算 | `include/borealis/session/session.h` | 片段只经这条路径发送；`enter` 追加的 `\n` 也由会话折算，面板不自行拼 `\r\n` |
| F4 | `TerminalView` 的公开发送面只有 `send_broadcast_key` / `send_broadcast_text`；UTF-8→码点的 `to_code_points` 是 `terminal_view.cpp` 的匿名命名空间函数 | `src/ui/terminal_view.{h,cpp}` | 需新增一个公开的 UTF-8 发送接缝（批 3），内部复用同一个解码件，不复制第二份 |
| F5 | `TextInput` 有 `set_value` / `set_on_changed` / `set_on_submit` / `set_placeholder`，单行；`Dialog::show()` 压焦点作用域并自动聚焦首个可聚焦控件 | Aurora `text_input.h`、`dialog.h` | 编辑框与占位符填充框都可用现成控件；搜索框可拿 Enter 作为「唯一候选发送」 |
| F6 | `Dropdown` 收「选项串数组 + 初始序号」，`set_on_change(int)` 回序号；`Switch` 收 `Reactive<bool>` 或 `set_on_changed` | Aurora `dropdown.h`、`switch.h`；`keys_panel.cpp` 先例 | 分组过滤与「发送后回车」开关都不需要新控件 |
| F7 | `CommandRegistry::add` 同 id 覆盖且保序，命令可带 `default_binding`；全仓 `src/` 未见 `KeyCode::K` 的绑定 | Aurora `commands.h`；全仓 grep | `snippets.open` 用 `Ctrl+Shift+K`，与保留键表无冲突 |
| F8 | 活动窗当前标签的焦点格取法已有先例：`command_tab_id()` → `tab_workspaces` → `workspace->view_of(workspace->focused_pane())` | `src/main.cpp` 的 `quick_select.open` 落点 | 片段只发焦点格，不广播、不猜 1 号格 |
| F9 | 面板是 app 级单实例；打开前 `retarget_panel(panel)` 搬窗，关窗链里 `retire_panel(&panel)` 撤账 | `src/main.cpp` 的 `keys.open` 与 `window_closed` 钩子 | snippets 面板照抄这两条腿，不新增窗口生命周期机制 |
| F10 | `Store::replace` 返回 `std::optional<std::string>`；新域要同步四处：`kDomainKeys`、`read_settings`、`settings_to_json`、`replace` 的 `prefs.set` | `src/config/store.cpp` | 批 2 的改动面是枚举清楚的四处，外加两份测试 |
| F11 | `utest_settings_catalog` 把数组整体算一个叶子，结构域当前在 `kNonPreferenceLeaves` 登记 `profiles.items`/`tunnels.items`/`key_dirs.items` | `tests/unit/utest_settings_catalog.cpp` | 须追加 `snippets.items`，否则双向核对表转红 |
| F12 | `shortcuts.overrides` 的启动重放仍待 G40；`main.cpp` 留有 `TODO(SPEC.FEAT.PREF.04)` | `src/main.cpp`、`codespec/FRAMEWORK_TASK_PREF04.md` | 本期只交付**面板级**快捷键；逐片段绑键在册延后，免得做一个重启即丢的假功能 |
| F13 | 配置目录审计按**键名**与明文哨兵查凭据，不审查用户自由文本 | `tests/unit/utest_credential_audit.cpp` | 片段正文是用户自由文本，**可以**包含任何内容；编辑框须明示「按纯文本落盘，不要写密码/令牌」 |

**未在实测射程内**：Windows/MSVC 构建与真机走查（沙箱无 MSVC、无交互桌面）。本稿的 UI 判据全部走
无头真布局 + 真派发，与密钥面板同一条验证口径；「可用」结论留待人工走查。

## 1 范围与边界

**本期交付**＝需求原文四个动作，逐句对口：

1. **片段库**：新增/编辑/删除命令片段，落 `snippets` 域，重启不丢。
2. **分组**：每条片段可带一个分组名（自由字符串，空＝未分组）；面板用下拉过滤。
3. **搜索**：按名称、分组、正文做大小写不敏感的包含匹配；与分组过滤取交集。
4. **参数占位**：正文里的 `{{name}}` 在发送前弹填充框，按名替换；无占位符直接发送。
5. **发送**：面板内单击行内「发送」把正文送到**活动标签焦点 pane** 的会话；`snippets.open`
   快捷键唤起面板后，搜索框回车在**唯一候选**时发送。

**不在本期**（§5 在册）：逐片段自定义快捷键、多行正文编辑器、分组改名/删除/排序、片段排序拖拽、
发送历史、变量默认值/转义/条件、shell 引号处理、把片段注入远端 shell 之外的目标（如 SFTP 路径栏）、
跨设备同步。

**与既有裁决的交界**：

- 存储走 `Settings` 第 8 域，与 `profiles`/`tunnels`/`key_dirs` 同一条「结构域不进设置面板」口径；
  `export_settings()`/`import_settings()` 因此自动覆盖，不另造片段专用文件。
- 发送只经 `Session::send_text`（F3），不绕过编码与行尾折算；不碰 `send_bytes` 的转义通道。
- 面板不认识 `Session`/`TerminalView`：装配层经 Hook 交出「发到焦点格」这一动作（7.97 D5① 同族）。
- 快捷键只登记打开面板这一条；逐片段绑键等 `shortcuts.overrides` 重放（G40）到货后再议（F12）。

## 2 屏面与判据对应

主屏＝左侧 520 dp 停靠卡片（非模态、默认隐藏），行区可滚动；二级浮层三张：新建/编辑框、
占位符填充框、删除确认框。

| 区域 | 判据 | 对应需求子项 |
|---|---|---|
| 标题行 | 标题「快捷片段」+ 行数 + 「新建」+「关闭」；无片段时行区画空态一句 | 片段库 |
| 搜索行 | 单行输入框，键入即时过滤；匹配名称/分组/正文，ASCII 大小写不敏感；回车只在**恰一条**候选时发送，否则留痕「候选 N 条，请再收窄」 | 搜索 + 快捷键发送 |
| 分组过滤 | 下拉：`全部分组` + 现存分组（首现序去重）；选中即过滤，与搜索取交集 | 分组 |
| 片段行 | 首行：分组徽标（未分组画 `--`）+ 名称 + 「发送」「编辑」「删除」；次行：正文预览（单行截断）+ 占位符个数徽标 | 片段库 + 发送 |
| 占位符填充框 | 每个唯一占位名一行输入；「发送」按填写值渲染后发出，「取消」一物不动；空值合法 | 参数占位 |
| 新建/编辑框 | 名称、分组、正文三个单行输入 + 「发送后回车」开关；红提示就地校验；保存即落盘 | 片段库 |
| 删除确认框 | 两段式：列出片段名；取消一物不动，确认才落盘 | 片段库 |
| 顶行留痕 | 「已发送：<名称>」/「当前没有可发送的会话」/ 保存失败；只占一行 | 发送 |

## 3 裁决（D1–D6，待评审）

| # | 待决 | 选项 | 结论（推荐） | 理由 |
|---|---|---|---|---|
| **D1** | 面板形态 | ① 独立左侧停靠卡片 `SnippetPanel`（520 dp，`snippets.open`）；② 连接侧栏的第四视图；③ 设置页新页；④ 模态对话框 | **①** | ② 侧栏 320 dp 放不下「搜索 + 分组 + 正文预览 + 三个动作」，且侧栏语义是「连接」不是「会话内命令」；③ 设置页是低频编辑形态，片段是高频发送形态，且发送需要终端在场；④ 模态框挡住终端输出，违背「看一眼输出再点下一条」的使用节奏。① 与密钥/隧道面板同族，生命周期与搬窗链都现成 |
| **D2** | 存储形态 | ① `Settings` 第 8 域 `snippets`，`{items:[{id,name,group,body,enter}]}`；② 独立 `snippets.json`；③ 复用 `shortcuts.overrides` 一类的映射 | **①** | ② 要另造落盘件、导出/导入、审计三条腿，且与「一份配置」的产品口径分叉；③ 映射表达不了名称/分组/正文三字段。① 与 `profiles`/`tunnels` 同构，`schema_version` 不升，旧文件缺域即回空表（与 `key_dirs` 同一条缺省回落）。**顺序即显示序**：新条目尾插，本期不做排序 |
| **D3** | 发送落点 | ① 活动窗当前标签的**焦点 pane**；② 1 号 pane；③ 广播到所有 pane | **①** | 需求说「发送至当前会话」——用户正看着哪一格，就该进哪一格；② 在分屏里会把命令送进看不见的会话；③ 是 `SPEC.FEAT.WS.09` 广播的语义，片段不继承。发送成功后面板**不自动关闭**，便于连续发送；无活动 pane 时留痕且不发 |
| **D4** | 占位符语法 | ① `{{name}}`，名限 ASCII 字母/数字/下划线/点/连字符，首现序去重；填充框逐名输入，替换全部出现；未闭合或非法名按字面保留；② `${name}`；③ 位置参数 `$1`/`$2` | **①** | `{{}}` 在 shell 命令里极少自然出现，不必转义；`${}` 与 shell 变量正面冲突（用户想发远端变量时会被本仓抢走）；位置参数可读性差且改名即错位。**不做**默认值、转义与引号处理：填进去的字节就是发出去的字节（需求没有这些，`AGENTS.md` 第 1 条不推断） |
| **D5** | 编辑与删除 | ① 行内「编辑」「删除」两枚按钮；新建/编辑框三个单行输入 + `enter` 开关；删除两段式确认；每次保存立即 `Store::replace`；② 只做新建与发送，编辑/删除走改配置文件；③ 删除不确认 | **①** | 需求原文是「常用命令片段库」，没有增删改就不能算库；② 把用户赶去手改 JSON，违背 CONN.07 的 UI 本体；③ 误点会丢一条用户资产，密钥面板的删除确认是现成先例。校验：名称 1–64 字节且不含控制字符，分组 0–32 字节且不含控制字符，正文 1–8192 字节；落盘失败留痕且内存态不变 |
| **D6** | 快捷键面 | ① 只做 `snippets.open`（`Ctrl+Shift+K`）+ 面板内回车唯一候选发送；逐片段绑键延后；② 本期就给每条片段绑一个全局键；③ 不做快捷键，只靠命令面板 | **①** | 逐片段键要写进 `shortcuts.overrides` 并在启动时重放，而重放待 G40（F12）；本期做出来会是「重启即丢」的假功能。① 已满足需求「快捷键发送至当前会话」的最小闭环：一键唤起 → 输入 → 回车。`Ctrl+Shift+K` 全仓未占用（F7） |

## 4 文件布局 / TU 拆分

```
include/borealis/conn/snippet.h     新增 —— 公共纯逻辑头（无 Aurora 类型、无文件系统）：
                                        Snippet{id,name,group,body,enter}；
                                        Placeholder（名 + 起止位置）；
                                        snippet_placeholders(body)、render_snippet(body, values)、
                                        snippet_groups(snippets)、snippet_rows(snippets, query, group)
                                        （返回下标，保持存储序）、next_snippet_id(snippets)、
                                        validate_snippet(name, group, body)、find_snippet(snippets, id)
src/conn/snippet.cpp                新增 —— 上述纯逻辑的实现。占位符扫描一次完成，
                                        非法/未闭合整段按字面跳过；搜索只做 ASCII 折叠。
include/borealis/config/settings.h  改 —— 第 8 域 `std::vector<conn::Snippet> snippets{}`；
                                        注释写明「结构域、不进设置面板、顺序即显示序」。
src/config/store.cpp                改 —— 四处同步：kDomainKeys、read_snippets（逐元素容错）、
                                        snippets_to_json、settings_to_json / replace 两处写出。
src/ui/terminal_view.{h,cpp}        改 —— 公开 `send_text_utf8(std::string_view)`：复用
                                        匿名命名空间的 to_code_points，再交 Session::send_text。
src/ui/snippet_panel.{h,cpp}        新增 —— 私有头（含 Aurora 类型）。Hooks：load / persist /
                                        send。面板持有本地副本，搜索与分组过滤走纯逻辑；
                                        行区用可替换子节点的 Column，键入时不重建搜索框。
tests/unit/utest_snippet.cpp        新增 —— 纯逻辑判据（占位符/渲染/过滤/分组/id/校验）。
tests/unit/utest_config.cpp         改 —— snippets 域往返、坏元素留痕、缺域回落、七域列表改八域。
tests/unit/utest_settings_catalog.cpp 改 —— kNonPreferenceLeaves 加 `snippets.items`；叶子计数随动。
tests/integration/itest_snippet_panel.cpp 新增 —— 无头真派发判据（见 §5）。
src/ui/settings_i18n.cpp            改 —— 双语词条（snippets.*）。
src/main.cpp                        改 —— Hooks 装配、`snippets.open`（Ctrl+Shift+K）、
                                        on_frame 无需 tick、关窗 retire_panel。
codespec/{SPECIFICATIONS,CHANGELOG,PLAN,ARCHITECTURE}.md 改 —— 裁决、版本、分期、模块表。
```

**发送接缝的所有权**：面板经 `send` Hook 交出一段**已渲染的 UTF-8 文本**；装配层取焦点 pane 并调
`TerminalView::send_text_utf8`，成功回 `true`、无会话回 `false`。面板不持有 `Session` 指针，
也不决定行尾（`enter` 只是给正文尾追加一个 `\n`，折算归 `Session`，F3）。

## 5 验收判据与分批

**批 0（本文档）**：设计稿落盘、评审收口、口径并入 §7；docs 单笔提交。

**批 1（纯逻辑）**：`conn::Snippet` 与六个纯函数；`utest_snippet` 至少覆盖：
`{{name}}` 首现序与重复去重；非法名/未闭合按字面；渲染替换全部出现且缺值保留原 token；
分组首现序与空分组排除；搜索三字段、ASCII 折叠、与分组交集；`next_snippet_id` 跳过已占用号；
名称/分组/正文三档校验的边界字节数。

**批 2（持久化）**：第 8 域四处同步 + `utest_config`。判据：
默认 `Settings{}` 的 snippets 为空且 `missing_domains_are_reported_once_each` 由 7 改 8；
写入一条含中文正文与 `enter=true` 的片段后读回逐字段相等；`items` 非数组留痕 `snippets`；
数组里的非对象元素留痕 `snippets.items[i]` 且其余条目保留；导出件包含同形域、导入后相等。

**批 3（面板与发送）**：`SnippetPanel` + `TerminalView::send_text_utf8` + `itest_snippet_panel`。
无头判据：
① 打开面板后出现全部行；在搜索框键入后只剩匹配行；切分组下拉后再次收窄。
② 点击「发送」把正文原样交回 Hook；`enter=true` 的片段多交一个 `\n`；Hook 回 `false` 时留痕
「当前没有可发送的会话」且面板仍开着。
③ 含 `{{host}}` 的片段点击发送先出填充框；填值后「发送」交出渲染串；取消则 Hook 零调用。
④ 新建：填名称/分组/正文/开关 → 保存交回整表且新 id 为 `snip-N`；名称为空时红提示且不交回。
⑤ 编辑：改正文后保存，id 不变、同表其余条目逐字段不变。
⑥ 删除：第一段只开确认框、整表不变；确认后条目消失并交回整表。
⑦ 搜索框回车：候选为 1 时发送它；候选为 0 或 >1 时零发送并留痕。
⑧ 发送口令/令牌样子的文本不被本仓改写或加引号（原样字节），编辑框有明文警告一句。

**批 4（装配与结项）**：`snippets.open`、Hooks、搬窗/撤账、双语词条、四份文档回写、
变异自证（至少四处：过滤字段、占位符替换、焦点 pane 取法、enter 追加）与分层提交。
真机走查与 Windows/MSVC 验证仍是人工门槛，本棒不称「可用」。

## 6 在册边界（本期不做，逐条可查）

1. **逐片段快捷键**：等 `shortcuts.overrides` 启动重放（G40，F12）；本期只有 `snippets.open`。
2. **多行正文**：Aurora `TextInput` 是单行；本期正文限一行。手改配置文件塞入的换行会原样发出
   （含 `enter` 再追加一个），本仓不替用户改写。
3. **分组管理**：分组只是片段的字符串字段；改名/删除分组、拖拽排序、组内排序都不在本期。
4. **占位符高级语义**：无默认值、无转义、无类型与引号处理；`{{name}}` 替换是纯文本替换。
5. **搜索**：ASCII 大小写折叠；非 ASCII 按字节比较，不做 Unicode case folding。
6. **片段正文的明文**：片段按纯文本进 `borealis.json`；编辑框固定显示「不要写密码/令牌」警告。
   本仓不扫描用户正文（扫描自由文本会制造假阳性与第二套凭据口径）。
7. **发送目标**：只发焦点 pane 的会话；没有活动标签/焦点格时留痕不发，不自动新建标签。
8. **真机走查**：键盘、输入法、DPI、Windows 腿均未验证；走查通过前不称「可用」。
