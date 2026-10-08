# FRAMEWORK_TASK_PREF04.md — SPEC.FEAT.PREF.04 快捷键重绑在 Aurora 框架侧的可派发任务书

> 本文件是 Borealis `SPEC.FEAT.PREF.04`（快捷键可重绑）在 Aurora 框架侧的依赖分析与回货判据。
> 依据：Borealis 对 `include/aurora/commands.h` 与 `include/aurora/app/shortcuts.h` 的读源实测（2026-10-08）。
> **结论先行**：框架已具备快捷键的**存储、匹配、注册表**三条腿（`ShortcutRegistry` + `KeyCombo` + `Command::default_binding` + `CommandRegistry::bind_shortcuts`），唯缺**「启动时按外部覆盖表重放绑定」所需的入口**——`CommandRegistry` 无「改绑定而不动元数据」的 setter，`KeyCombo` 无「字符串 → 键位」的反向解析。这两处任一到位，Borealis 侧的重放一腿即通。
> 登记形态见 `codespec/SPECIFICATIONS.md` §7 裁决 **7.84** 与附录 A.2 的 **G40** 行；本仓代码处留痕在 `src/main.cpp` 的 `TODO(SPEC.FEAT.PREF.04)`。

---

## 1 框架侧既有能力清册（无需补全）

| 能力项 | 位置 | 现状 |
|:---|:---|:---|
| `KeyCombo` 结构 | `include/aurora/app/shortcuts.h` | 表示「修饰位 + 主键」的组合键，含 `to_string() -> std::string` 显示串；`matches(KeyEvent)` 只比 Shift / Ctrl / Alt / Meta 四位（锁定位不参与），与裁决 7.51 那条回货形态一致 |
| `ShortcutRegistry` | `include/aurora/app/shortcuts.h` | 应用内快捷键登记表：`add` / `remove` / `find_by_combo` / `dispatch` |
| `Command` 结构体 | `include/aurora/commands.h` | `default_binding` 是 `std::optional<KeyCombo>`；一条命令的绑定源在此 |
| `CommandRegistry` | `include/aurora/commands.h` | `add`（同 id **覆盖**语义、保注册次序）/ `remove` / `find` / `all` / `bind_shortcuts(ShortcutRegistry&)` 全套；后者把每条命令的 `default_binding` **逐条投射**进 `ShortcutRegistry` |

**Borealis 消费现状**：设置面板的重绑浮层把新绑定写入 `config::ShortcutsSettings::overrides`（`std::map<std::string, std::string>`，command id → 组合键文本），落盘形态经 `src/config/store.cpp` 走 `[{command, combo}, ...]` 数组（裁决 7.27① 的点号路径模型）。**装配层启动那一刻把这份表回灌进 `ShortcutRegistry`** 是本件唯一未接的一腿。

---

## 2 缺的两条入口（任缺其一即阻塞）

### 2.1 缺口路径 A：`CommandRegistry::set_binding(command_id, KeyCombo)`

- **形态**：给 `CommandRegistry` 加一个入口，只改指定 id 那条命令的 `default_binding`，**不动**其他字段（`title` / `icon` / `action` / `category` / `scope` / `enabled` 谓词 / `when_label`），也不改变注册次序。
- **返回值**：`bool`（命令 id 不存在时返 false），或 `std::optional<...>`；消费者据此判断某条覆盖是否有对应命令。
- **消费方**：Borealis 装配层在 `bind_shortcuts` 之前遍历 `overrides`，把每条 combo 文本经路径 B 的解析器转 `KeyCombo` 再调本入口。

### 2.2 缺口路径 B：`KeyCombo::from(std::string_view) -> std::optional<KeyCombo>`

- **形态**：反向解析器，与既有 `to_string()` **逐字节往返等值**。允许的组合键串形态就是 `to_string()` 目前输出的那些（如 `"Ctrl+Shift+P"` / `"Alt+Tab"` / `"F5"`），不额外支持别的写法（避免形成第二显示串规范）。
- **修饰次序**：解析器**不要求**输入与 `to_string()` 完全同序，但**产出**的 `KeyCombo` 走同一份修饰位与主键；往返等值判据是 `to_string(from(s)) == s` 恒成立（对由 `to_string` 生成的那批串）。
- **畸形串**：返回 `std::nullopt`，绝不回落成「无修饰 + 空主键」之类的假有效值——Borealis 装配层要按「一条被拒不影响其余」处理。
- **非 ASCII**：主键名一律 ASCII（与 `key_name` 表同源，本仓不重述那张表的字面量），非 ASCII 输入直接判非法。

**两条路径的分工**：路径 B 负责「文本 → `KeyCombo`」的语义，路径 A 负责「已注册命令的绑定改一下」。二者组合起来覆盖 PREF.04 装配层的需要；单独任一路径都不够。框架可择一实现（只走 A + 把 `KeyCombo` 反解留在 Borealis，只走 B + 让本仓重注册完整 `Command`），但**任一路径若把「反向解析」放在 Borealis 侧就是本仓自造第二真值源**（违裁决 7.72 与 AGENTS.md §5 第 2 条）——故本仓优先请求**两条一起回货**。

---

## 3 回货判据（三条）

1. **往返等值**：`KeyCombo::from(KeyCombo::to_string())` 与原始 `KeyCombo` **逐位相等**（修饰位、主键），且对**所有已注册键名**与**所有修饰子集**都成立。基准由框架侧自测；Borealis 侧的复验以「装配层重放之后，从 `ShortcutRegistry` 读回的组合键与面板落盘的字符串**逐字节相符**」为判据。
2. **畸形输入拒绝**：空串、只有分隔符、未知键名、`"Ctrl+"` 尾巴、`"ctrl+p"`（框架串是小写还是首字母大写由 `to_string` 决定；不符其字面形态一律回 `nullopt` 而不「大小写宽容」）——四种以上畸形各回 `nullopt`；不得静默回落到「Ctrl + 空主键」之类假有效值。
3. **绑定 setter 不动元数据**：`set_binding(id, combo)` 之后 `find(id)` 给出的 `Command`，除 `default_binding` 之外**逐字段与调用前相等**（含 `title` / `action` / `enabled` 谓词的对象身份或语义等价），且 `all()` 的注册次序不变。

---

## 4 配套用例（框架侧）

- `utest_key_combo_roundtrip`：一批代表 `KeyCombo`（覆盖 `F1` ~ `F12`、字母族、数字族、`KP_*`、`Insert` / `Delete` / 导航键、以及 Shift / Ctrl / Alt / Meta 的四位组合），断「`to_string` → `from` → `matches` 自身」全等。
- `utest_key_combo_rejects_malformed`：畸形输入四类以上，各回 `nullopt`。
- `utest_command_registry_set_binding_preserves_metadata`：注册一条完整命令、`set_binding` 换组合键、`find` 回来除 `default_binding` 外逐字段相等，`all()` 次序不变。
- `itest_bind_shortcuts_with_overrides`（示例）：模拟应用侧的启动重放，注册表 + 覆盖表混合灌入 `ShortcutRegistry`，断最终 dispatch 语义按覆盖表生效。

---

## 5 本仓接货复验形态

- **装配层**：`src/main.cpp` 在 `app.commands().bind_shortcuts(app.shortcuts())` 之前遍历 `config::ShortcutsSettings::overrides`：
  - 对每条 `(command_id, combo_text)`：`KeyCombo::from(combo_text)` 解析；解析失败只 `AURORA_LOG_WARN` 留痕该条而其余照旧（不得让畸形条目挡住启动）。
  - 成功即 `app.commands().set_binding(command_id, *parsed)`。命令 id 不存在时同样留痕。
- **接货复验的变异自证**：
  - m1 装配层不接入 `overrides` → 覆盖不生效而其余照常；
  - m2 一条覆盖文本畸形 → **只该条**留痕不生效而其余覆盖仍生效；
  - m3 一条覆盖的 command id 未注册 → 只该条留痕且不 crash；
  - m4 框架侧把 `set_binding` 改成连带重写 `title` → `find(id)` 断言红（框架自有用例，本仓复验只需读 `bind_shortcuts` 之后 `ShortcutRegistry` 的 dispatch 语义）。
- **本仓落点**：撤除 `src/main.cpp` 的 `TODO(SPEC.FEAT.PREF.04)`，接入上述重放；`SPECIFICATIONS.md` 的 PREF.04 落地现状句与 §7 裁决 7.84 就地更正为「已闭合」；附录 A.2 的 G40 行改为「已闭合」；CHANGELOG 补一条接货复验版本。

---

## 6 工作量评估

框架侧新增：
- `KeyCombo::from` 一个静态入口，配一份与 `key_name` **同源**的反查（即由 `key_name` 生成，不另列新表）。约 60 行 + 一份单测。
- `CommandRegistry::set_binding` 一个入口。约 20 行 + 一份单测。
- 文档回写：`include/aurora/app/shortcuts.h` 与 `include/aurora/commands.h` 头注、`codespec/specification/` 相应节。约 20 行文档。

**总体评估**：**小**（不新增架构组件、不改既有派发路径、纯补两个入口）。
