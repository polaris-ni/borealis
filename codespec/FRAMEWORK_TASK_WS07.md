# FRAMEWORK_TASK_WS07.md — SPEC.FEAT.WS.07 命令面板的框架侧依赖任务书

> 本文件是 Borealis `SPEC.FEAT.WS.07`（命令面板）在 Aurora 框架侧的依赖分析与可派发任务清单。
> 依据：Borealis 对 `aurora/widget/command_palette.h` 与 `aurora/commands.h` 的读源实测（2026-10-08）。
> **结论先行**：框架已具备命令面板的全部核心能力（`CommandPalette` widget、`CommandRegistry`、`command_fuzzy_score`），唯缺**两条 i18n 路径**——搜索框占位符与空态提示的词条化。

---

## 1 框架侧既有能力清册（无需补全）

| 能力项 | 位置 | 现状 |
|:---|:---|:---|
| `CommandPalette` widget | `include/aurora/widget/command_palette.h` | 模态居中浮层 + 半透明遮罩 + 圆角卡片；顶部搜索框（输入即过滤）、中部结果列表（当前项高亮）、底部选中命令的快捷键提示；打开时压入焦点作用域（子树内唯一可聚焦控件是搜索框）；Enter 经搜索框 `on_submit` 执行选中项、Esc/↑/↓ 经打开期临时快捷键绑定接管、Space 只落字不触发执行、点击遮罩关闭；键位分工见类注释第 32–37 行 |
| `CommandRegistry` | `include/aurora/commands.h` | 命令的唯一真源：`add` / `remove` / `clear` / `find` / `all` / `search` / `invoke` / `is_enabled` / `set_enabled`；投影方法 `bind_shortcuts()`（快捷键注册）与 `to_menu_items()`（菜单生成）；模糊检索 `search(query, only_enabled)` 按（得分降序, 标题升序）排序 |
| `command_fuzzy_score` | `include/aurora/commands.h` | 不区分大小写的子序列匹配打分函数：空串匹配全部（返回 0），有效匹配恒 ≥ 0，不匹配返回 -1 |
| `Command` 结构体 | `include/aurora/commands.h` | 含 `id` / `title` / `icon` / `category` / `action` / `default_binding` / `scope` / `enabled` 谓词 / `when_label` 标签九字段，覆盖快捷键、菜单项、命令面板三者的共同数据源 |
| 演示用例 | `examples/demos/demo_command_palette.cpp` | 展示命令注册、三源接线（快捷键 + 菜单投影 + 面板）、Ctrl+Shift+P 唤出的完整流程 |
| 单元测试 | `tests/unit/utest_command_palette.cpp` | 覆盖打开/关闭/过滤/选中移动/执行/点击遮罩关闭/无注册表降级等场景 |

**Borealis 消费现状**：已在 `src/main.cpp` 注册 `settings.open`（Ctrl+,）与 `search.open`（Ctrl+F）两条命令，并调用 `app.commands().bind_shortcuts(app.shortcuts())`；装配层提供 `hooks.commands` 接缝供设置面板的快捷键只读表取快照。

---

## 2 框架侧缺口清单（需补全）

### G38：`CommandPalette` 搜索框占位符硬编码英文

**位置**：`include/aurora/widget/command_palette.h` 第 48 行  
**现状**：构造函数内 `field->set_placeholder("Type a command...");` 为字面量英文，无 i18n 查表路径。  
**影响**：中文界面下搜索框仍显示英文占位符，违 §4.3 第 14 条「字符串字面量一律 ASCII 英文」的例外规则（上屏文案须走词条表）。  
**回货判据**：
1. 占位符改由框架 i18n 查表取得（`LocalizedString` 或等价机制），缺省词条 key 可配或由库内定（如 `"command_palette.placeholder"`）。
2. 应用侧可通过 `CommandPalette::set_placeholder(const std::string &)` 覆盖默认占位符（保持灵活性，与 `TextInput::set_placeholder` 同口径）。
3. 配套用例：断言占位符非空且等于查表结果（或自定义值），变异注入＝把查表 key 改错一位 ⇒ 占位符为空或回退到某兜底文本。

**工作量评估**：小（单点修改 + 一处 setter + 一条用例）。

---

### G39：`CommandPalette` 空态提示硬编码英文

**位置**：`include/aurora/widget/command_palette.h` 第 318 行  
**现状**：`on_paint` 内 `p.draw_text(..., "No matching commands", ...)` 为字面量英文，无 i18n 查表路径。  
**影响**：无匹配结果时面板显示英文空态提示，同上违 §4.3 第 14 条。  
**回货判据**：
1. 空态提示改由框架 i18n 查表取得，缺省词条 key 可配或由库内定（如 `"command_palette.no_results"`）。
2. 应用侧可通过 `CommandPalette::set_empty_message(const std::string &)` 覆盖默认空态提示（保持灵活性）。
3. 配套用例：断言空态提示非空且等于查表结果（或自定义值），变异注入＝把查表 key 改错一位 ⇒ 提示为空或回退到某兜底文本。

**工作量评估**：小（单点修改 + 一处 setter + 一条用例）。

---

## 3 可选增强项（非阻塞，落期见 PLAN.md）

### E1：`CommandPalette` 结果列表虚拟化

**现状**：`max_results_` 缺省 50，超出截断；每帧绘制全部结果行（`on_paint` 第 321–343 行 for 循环）。  
**收益**：支持更大结果集（如 1000+ 条）而不卡顿。  
**代价**：需引入滚动容器或自研可视区域裁剪，工作量中等。  
**建议落期**：M4 之后，待真实场景出现性能瓶颈再优化。

### E2：`CommandPalette` 图标渲染

**现状**：`Command::icon` 字段存在但面板未使用（`on_paint` 只绘制 title 与 category）。  
**收益**：视觉辨识度提升，与菜单项/快捷键表对齐。  
**代价**：需框架提供图标加载与渲染原语（当前 `Painter` 无 `draw_icon` 接口），工作量中等偏大。  
**建议落期**：随框架图标系统落地一并接入。

### E3：`CommandPalette` 分类折叠/分组显示

**现状**：结果列表平铺所有匹配命令，`Command::category` 仅作右侧次级信息列。  
**收益**：大量命令时可按类别快速定位。  
**代价**：需改造结果列表布局逻辑（分组头 + 折叠状态管理），工作量小。  
**建议落期**：M3 远程连接阶段（SSH/串口/Telnet 命令增多后自然需要）。

---

## 4 Borealis 侧待办清单（框架回货后执行）

1. **命令注册**：将 Borealis 全部 UI 动作注册为 `Command`（开标签/分屏/切主题/改设置/连档案/搜索等），每条命令填齐 `id` / `title` / `category` / `action` / `default_binding`。
2. **中文词条**：在 `src/ui/settings_i18n.cpp` 补充命令相关词条（`command_palette.placeholder` / `command_palette.no_results` / 各命令的 `title` 与 `category` 中文名）。
3. **装配层接线**：在 `src/main.cpp` 创建 `CommandPalette` 实例，注入 `app.commands()`，登记 `Ctrl+Shift+P` 快捷键（或直接复用框架 demo 的 `toggle()` 形态）。
4. **主题适配**：确认面板的遮罩色/卡片底色/高亮色与 Borealis 主题系统对接（框架 `CommandPalette::on_paint` 已用 `inherit_theme(ctx)` 取 `theme.background` / `theme.text` / `theme.primary`，理论上零改动）。
5. **真机走查**：验证面板在真实会话下的表现（焦点陷阱是否吞掉发往终端的键、ESC 关闭后会话能否正常接收输入、搜索结果是否含全部注册命令）。

---

## 5 回货验收标准（框架侧）

- [ ] G38 闭合：占位符可经 i18n 查表取得，且有 setter 允许应用侧覆盖；配套用例绿。
- [ ] G39 闭合：空态提示可经 i18n 查表取得，且有 setter 允许应用侧覆盖；配套用例绿。
- [ ] 现有 `utest_command_palette` 套件全绿（回归保护）。
- [ ] `demo_command_palette` 仍可正常构建与运行（示例不崩坏）。
- [ ] 文档回写：`codespec/specification/04-widget.md` 与 `CHANGELOG.md` 更新 G38/G39 条目。

**预期总工作量**：框架侧 2–4 小时（两缺口 + 用例 + 文档）；Borealis 侧 1–2 天（命令注册 + 中文词条 + 装配接线 + 真机走查）。

---

## 6 附录：关键代码位置索引

| 文件 | 行号 | 内容 |
|:---|:---|:---|
| `aurora/include/aurora/widget/command_palette.h` | 48 | 硬编码占位符 `"Type a command..."` |
| `aurora/include/aurora/widget/command_palette.h` | 318 | 硬编码空态提示 `"No matching commands"` |
| `aurora/include/aurora/widget/command_palette.h` | 46–59 | 构造函数（搜索框创建与回调接线） |
| `aurora/include/aurora/widget/command_palette.h` | 129–153 | `open()` 方法（焦点作用域压入 + 键位安装） |
| `aurora/include/aurora/widget/command_palette.h` | 156–170 | `close()` 方法（键位卸载 + 焦点作用域弹出 + 回调触发） |
| `aurora/include/aurora/widget/command_palette.h` | 409–420 | `rebuild_results()` 方法（调用 `commands_->search()` 并截断） |
| `aurora/include/aurora/commands.h` | 全文 | `CommandRegistry` API 与 `command_fuzzy_score` 声明 |
| `borealis/src/main.cpp` | 243–282 | Borealis 现有的命令注册与 `hooks.commands` 接线 |
| `borealis/src/ui/settings_i18n.cpp` | 27, 94 | 现有的 `settings.action.open` / `search.action.open` 中文词条 |

---

*本文档版本 v0.1，创建时间 2026-10-08。框架缺口编号延续 Borealis 附录 A.2 的 G1–G37 序列，故从 G38 起计。*
