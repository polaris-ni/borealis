# FRAMEWORK_TASK_FILEDIALOG_NAME.md — 另存为对话框缺「建议文件名」这一格：两条腿早有现成接缝，只缺 `Options` 的字段

> 本文件是 Borealis `SPEC.FEAT.CONN.05` 串口监视卡三期（复制／导出）在 Aurora 框架侧的候选缺口派发任务书，登记为缺口 **G48**（`codespec/SPECIFICATIONS.md` 附录 A.2；裁决 **7.163**）。消费动机见设计稿 `codespec/UI_SERIAL_MONITOR.draft.md` §10.8 第 1 条与 §10.7 第一条。
>
> **结论先行**：这条不是「三腿实现不了」，而是**接口少一格**——Win32 腿手上就是 `IFileSaveDialog`（`SetFileName` 是它的原生方法），桌面自绘腿本来就画了一枚文件名输入框（`name_input_`），两者今天都只能拿到一个空初值。Borealis 的替身是**把命名完全交给用户**：每次导出都要手打一个有意义的名字。
>
> **派发边界**：一条公共字段 + 三腿的取值口径 + 一条 headless 观察面。不改对话框的其它语义，不动取消/失败的归因（那两条现有口径是对的）。

---

## 1 框架侧既有能力清册（读源实测，2026-10-26 于 `dev-1.0.0-alpha.10`，HEAD `a63bc190`；2026-10-27 于 HEAD `f1c5da35` 复检——`Options` 仍只有 `title` / `initial_dir` / `filters` 三格，headless 面仍无从观察传入的 `Options`，§5 的 `initial_dir` 被忽略同样未修）

| 能力项 | 位置 | 现状 |
|:---|:---|:---|
| 选项面 | `include/aurora/app/file_dialog.h:23-27` 的 `struct Options` | **只有三格**：`title`（空＝平台默认标题）、`initial_dir`（空＝平台默认位置）、`filters`（空＝不限类型）。没有「建议文件名」，也没有「缺省扩展名」 |
| Win32 腿：标题怎么落 | `src/aurora/app/file_dialog_win32.cpp:179-180` | `if (!opts.title.empty()) pfs->SetTitle(aurora::internal::utf8_to_wstr(opts.title).c_str());`——**已有 UTF-8→宽串的既有转换器**，任何新增字符串字段都该走同一枚，而不是自己 `std::wstring(narrow.begin(), narrow.end())` |
| Win32 腿：保存对话框的本体 | 同上，`save_file` 走 `IFileSaveDialog`（注释原话） | `IFileDialog::SetFileName`（Vista+ 即在 `IFileSaveDialog` 的继承面上）与 `IFileDialog::SetDefaultExtension` 都**没被调用**——不是工具链缺 API，是接口没这一格可传 |
| 桌面自绘腿：文件名输入框 | `src/aurora/app/file_dialog_selfdrawn.cpp:342-345` | `name_input_ = std::make_shared<TextInput>(); name_input_->set_placeholder("File name"); name_input_->set_on_changed([](v){ model_.set_save_name(v); });`——**输入框存在**，初值恒空；`save_file` 的返回值是 `join_path(current_dir_, save_name_)`（同文件 `:291-292`）。接缝＝建框时把 `opts.suggested_name` 同时喂给 `model_.set_save_name(...)` 与 `name_input_`（只喂 model 会让框里显示空、只喂 widget 会让判据不成立）。**该腿是真实现而不是回退**——POSIX 桌面腿自绘选择器已于 2026-10-23 回货闭合（缺口 **G41**，裁决 7.138），故本条不是重提旧账 |
| 其余平台回退 | `file_dialog.h:51-52` 的注释自陈 + 其下的内联回退（WASM／Android／BSD） | 内联回退，三格 `Options` 一概不消费（原文：「不做真实选择器（WASM 主线程不可阻塞、文件系统是 MEMFS 而非用户真实磁盘），**仅保钩子语义**」）——新增字段在这条腿上**无事后可做**，写清即可 |
| headless 观察面 | `file_dialog.h` 的 `headless_open_result` / `headless_save_result` / `headless_folder_result` 与 `interactive` | 三枚都是**预设返回值**（钩子优先 → 非交互回空）。**没有任何一条面能看到传进去的 `Options`** ⇒ 今天无法为「建议名有没有被消费」写自动化判据 |

**Borealis 消费现状（代偿）**：`src/main.cpp` 的 `picked_save_path(title, filters)` 只传两格，导出与设置导出各给一句标题与一枚过滤器；文件名由用户在对话框里手打。代价是每次导出都要重打一个名字，且名字打错了（含非法字符）只有 OS 会拦。

---

## 2 缺的那条腿：`Options::suggested_name`（口径写死，实现留框架定）

**甲＝新增一枚字符串字段（推荐，最小形态）**

- `struct Options { std::string title; std::string initial_dir; std::string suggested_name; std::vector<Filter> filters; }`（落点次序由框架定，**建议放在 `initial_dir` 之后**：它是「名字」而不是「位置」，与 `filters` 相邻易误读成过滤器的一部分）。
- **单位与编码口径（本条最需要写死的一句）**：`suggested_name` 与既有两格同语义＝**UTF-8 窄串**；各腿自行转成平台形态（Win32 走 `aurora::internal::utf8_to_wstr`，自绘腿按它已有的文本面直接吃窄串）。框架既有面里 `title` 已是这条口径，新增字段若走别的口径就成了第二套规则。
- 语义：**预选值而不是答案**——用户可以改、可以清空、可以换目录；清空后的归因维持现状（取消＝`Ok(空)`，不是 `Error`）。
- 边界（建议一并写进头注，省得两条腿各解释一次）：⑴ 只取**末段名字**，若传进来带目录分隔符，各腿按「取文件名部分」处置并留 WARN；⑵ 空串＝不表达意图，与不传该字段同形（保持对既有调用方的零行为变化）；⑶ 扩展名不做隐式补全，除非选了下面的乙。

**乙＝再配一枚缺省扩展名（可选增强，非必需）**

- `Options::default_extension`，Win32 映射 `IFileDialog::SetDefaultExtension`，自绘腿映射「用户没打点时补一个」。业界（Chrome、Windows 记事本另存为）都这么做。本仓**不替框架选路**：`filters` 里已有通配模式，框架若愿意从 `filters.front().extensions` 自己推出缺省扩展名，那同样是闭合。

---

## 3 回货判据（headless 即可，不要求真实对话框在场）

1. **观察面先补，否则判据无处可写**：给一条「最后一次传进来的 `Options`」的可见途径——形态由框架定，三选一即可：`inline Options headless_last_options;`（与三枚既有钩子同族、同 `NOLINTBEGIN` 那段的位置）、或 `save_file` 的非交互分支改为经一枚 `std::function<void(const Options&)>` 记录、或把转换抽成可单测的自由函数（Win32 腿的 `suggested_name` → 宽串、自绘腿的 model 初值）。
2. `Options{}`（三格/四格全空）在**全部腿**的行为与今日逐字节相同——新增字段不得改任何既有返回值或取消语义。
3. `suggested_name = "serial-dump.txt"` 时：非交互分支能读到它（判据 1 的形态之一即可证）；自绘腿的 `model_.save_name()` 在建框后即时等于该串，且用户随后改写入的值仍原样回传。
4. **非 ASCII 名逐字到达**：`suggested_name` 取一串 UTF-8 中文（判据里用转义写死字节，如 `"\xE4\xB8\xAD\xE6\x96\x87.dump"`）时，观察面读回的字节与传入**逐字节相等**。这条是 Borealis 侧 F15 实测的直接对应（本机 ACP=936：窄串经 ANSI 码页解会把 `E4 B8 AD` 折成 GBK 花码）——转换做错的方向在本仓已经踩过一次，请框架侧用例把它钉住。
5. 平台三分支的文档回写：`file_dialog.h` 的三分支注释段补一句 `suggested_name` 在哪条腿生效、在 WASM/Android 那条回退上「不消费」的原文。

---

## 4 本仓接线面（回货后，一棒可清）

- `src/main.cpp` 的 `picked_save_path(title, filters)` 加第三形参并透传；串口监视导出送一个与内容同源的缺省名（形态取 `serial-<会话标题或 id>-<epoch>.txt`，与 `SPEC.FEAT.CONN.11` 的 `session-<tab>-<epoch>.log` 同族），设置导出送 `borealis-settings.json`。
- 接线后本仓**不改**判据：`itest_serial_monitor_panel` 那 18 案吃的是 `pick_export_path` 这枚 Hook 的返回值，与对话框内部无关——故回货判据落在框架侧，本仓只需一条走查（§10.6 第 2 条从「文件名缺省值是否合适（本期不合成缺省名）」改成「缺省名是否就是那一份内容」）。
- 闭环节点：附录 A.2 的 G48 行标「已闭合」、裁决 7.163 补记回货形态、`UI_SERIAL_MONITOR.draft.md` §10.7/§10.8 两条出账、`PLAN.md` §6 与本文同步。

---

## 5 相邻读数（登记，不混进本条）

- **`initial_dir` 在 Win32 腿上被忽略**：`src/aurora/app/file_dialog_win32.cpp:125` 的原文注释写的是「预选目录需要 Vista+ 的 `SHCreateItemFromParsingName`，当前 MinGW 工具链未提供」，而**同一个函数体**紧接着的 `:123` 就调了 `SetTitle` ⇒ 「能改标题、不能改目录」在同一处并存。该文件里 `SetFolder` 与 `SHCreateItemFromParsingName` **一次都没出现**（全文件 grep 实测），故 `save_file`（`:180` 有 `SetTitle`）与 `open_folder`（`:225` 同理）也各自忽略 `initial_dir`，只是**连那句注释都没有**。而头注没写这条限制（`Options::initial_dir` 的注释只说「空串＝平台默认位置」，读起来像两腿同能力）。本仓暂未受害（监视卡不预选目录），但它会挡住「上次导出的目录」这类 UX。请框架侧二选一：**要么**接上 `IFileDialog::SetFolder`，**要么**把头注改成「Win32 腿当前不消费 `initial_dir`」——**别让接口描述与实现不一致**，那是最难查的那类缺陷（本仓在 `set_size` 的单位注释上刚报过一次同类，见裁决 7.160⑧）。
- 本条与 §2 是**两个独立缺口**，只是同一次读源顺手取到的；若框架侧愿意一棒做两件事更好，否则本仓另开 G49 登记。

