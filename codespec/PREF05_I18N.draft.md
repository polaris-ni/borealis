# PREF05_I18N.draft.md — `SPEC.FEAT.PREF.05` i18n 双语首版设计稿

> **状态（2026-10-22 出稿）**：批 0＝本稿与 §0 地基实测；**批 1 已落**（表本体收敛：五处死条目、`settings_i18n.h` 那句「十三」改口、`kStrings` 更名 `kStringsZh`、判据 ⑥ 的中文半边随案落地；读数见 §8）。批 2 起为英文档与语言接缝。稿面 §3 的 D1–D12 按人的常存授权（「后续再遇到问题则你自行按照推荐去做」）**全按推荐档 ① 收口**，收口结论逐条写在 §3 的「收口」列里；其中 D2／D5 的形态由 §0 的 F6／F15／F16 定形，出稿即已实测、不留待实现阶段。**D10 的物理落点在批 1 实现时改形一次**（可见结果不变），逐字记在该行「收口」列。
> **本条不称「可用」**（AGENTS.md §4.4 第 33 条）：§6 的走查项一条未做，且本沙箱只有 POSIX 腿——Windows 侧字体能否排出英文档之外的任何形态一律标**推断／未验证**。

---

## 0 地基实测（F1–F18，2026-10-22，POSIX 腿 + Headless；Windows/MSVC 一律标推断／未验证）

本棒形态由下面十八条读数定形。与前几棒不同，本棒的**既有地基不是空的**：词条表、装填点、覆盖闸都已在了，所以 §0 读的全部是「这一套在加第二档时会从哪里断裂」，而不是「有没有接缝」。

| 号 | 读数 | 它决定了什么 |
|:---|:---|:---|
| **F1** | **需求本体只有一句话**：`codespec/SPECIFICATIONS.md` 的 PREF.05 行原文「中文/英文首版双语，经 Aurora `StringTable`」；`PLAN.md` 把它排在 **P2 / M4**，并注明「『命令 id → 中/英词条』的映射表口径已由裁决 7.25⑬ 定死，词条本体随本条」。7.25⑬ 的原文是「**N8 命令 id 一律英文入框架 `CommandRegistry`，中/英词条由本仓 `StringTable` 的映射表维护，新增命令须同步词条**」 | 三件事不用本棒再裁：① 通道就是框架 `StringTable`（不自造第二套翻译机制）；② 命令 id 恒为英文（界面语言只换词条不换 id，快捷键覆盖表因此不受影响）；③ 「新增须同步」是一条**要有自动化证人**的纪律，而不是一句文档口号——判据 ② 就是为它写的 |
| **F2** | **表已存在，但只有一档**：`src/ui/settings_i18n.cpp` 的 `kStrings` 实测 **469 条注册／464 个不同 key**（差 5，见 F8），`install_settings_strings()` 逐条 `table.add(settings_locale(), key, text)` 并把表内缺省档设成 `settings_locale()`；`constexpr std::string_view kTag = "zh"`。全仓对 `aurora::Locale` 的引用**只在这一个 TU**（`grep -rn "Locale\b" src/ include/ \| grep -v settings_i18n` 零命中） | 本棒不是从零建 i18n，而是**给一张已服役的表补第二档**：所有接缝（`settings_text`／`settings_label`／`has_settings_string`／`settings_issue_text`）已在，464 个 key 的命名纪律已在。工作量重心因此落在「英文文案本体」与「档位不再写死」两件事上 |
| **F3** | **中文上屏靠的是一个缺省档 hack，而补上 `en` 档就会把它拆掉**：`settings_i18n.h` 文件头③原文——「框架的 `Text` 在没有 `Provider<Locale>` 时按 `Locale{}`（即 `en`）查表，而 `StringTable::lookup` 的第一级失败会**回落到缺省档的 tag**，故只有把缺省档设成 `zh`，`tr()` 出来的标签才真显中文」；`include/aurora/i18n/string_table.h` 的 `lookup` 实测两级：`loc.tag()` 未命中→`default_.tag()` 再查→`nullopt` | 今天 `Locale{}`＝en 查不到 ⇒ 回落 zh ⇒ 上屏中文。**一旦 en 档有 464 条词条，同一条链就变成「en 命中 ⇒ 上屏英文」**，而命中与否取决于谁先注册、与用户的语言意愿无关。这条是本棒第一个必须正面处理的事实：缺省档不再是「让中文显示出来的技巧」，它从此是**真正的语言选择**（D2／判据 ①） |
| **F4** | **框架有运行期 locale 通道，本仓一处未用**：`include/aurora/widget/provider.h` 里 `using LocaleProvider = Provider<Locale>`，`Provider` 可用 `std::shared_ptr<State<T>>` 构造、`set_value()` 「运行时改写注入值（触发子树刷新）」；`Text::resolved_text(ctx)` 与 `Button::resolved_label(ctx)` 的实现同形——`ctx.environment<Locale>()` 命中则用它、否则回落 `Locale{}`。本仓全仓 `Provider<`／`environment<Locale>` **零命中** | 「运行时切换」在框架侧**不是缺口**，不需要派任务书（对照 AGENTS.md §5.1）。但它在本仓一次没用过，且下一行的读数决定它**不足以**兑现运行期切换 |
| **F5** | **框架内部仍有多处硬编码 `Locale{}`，绕过 environment**：`widget/rich_text.h` 与 `widget/reorderable_list.h` 的解析入口传的是 `Locale{}` 而不是 ctx 里的值；`Text::accessibility_label()` 与 `Button::accessibility_label()` 在「尚未布局或绘制时」的现场解析同样传 `Locale{}` | 即使装了 `LocaleProvider`，**无障碍朗读名与富文本那一面仍按 `Locale{}` 查表**。今天它们靠「缺省档＝zh」兜住，与本仓期望一致；D2 若把缺省档改成随语言而定的值，这几处仍会跟着走（因为它们查的是缺省档），**反而正确**。这条读数的用处是给 §7「不做运行期切换」提供第二条理由：运行期切换要同时补齐框架侧那几处 `Locale{}`，属 Aurora 侧改动，本仓不等不绕 |
| **F6** | **上屏串的两种形态里，急切解析占绝对多数**：`settings_label(key)`（就地解析成 `std::string`）全仓 **249 处调用点、分布在 17 个 TU**，`settings_text(key)`（交框架 `LocalizedString`，绘制期查表）**33 处**（同一枚 grep 口径，均不含定义所在的 `settings_i18n.{h,cpp}`）。前者密集在命令注册（`src/main.cpp` 里十余条 `.title = borealis::ui::settings_label(...)`）、面板与对话框构造、各处角标拼接 | 这是 D2 定形的决定性读数：**运行期换语言不会让那 249 处跟着换**，因为它们已经把结果字符串取走了；要换就得重建命令表、重开面板、重装配每一扇窗。半做即中英混排，比不做更糟。⇒ **语言在进程启动时定档、改后重启生效**（推荐档①），本棒不碰运行期切换 |
| **F7** | **覆盖闸已有，但它只闸「有没有词条」而不闸「哪一档」**：`tests/integration/itest_settings_panel.cpp` 的 `every_catalog_key_resolves_to_a_display_label` 逐个 catalog 键断 `has_settings_string(control.key)` 与非空标签，而 `has_settings_string` 内部写死用 `settings_locale()`（＝zh） | 补上 en 档后，**这条既有案对「漏配英文」完全无感**（它查的是中文档）。判据 ② 因此必须是新的证人形态：两档 key 集合逐字相等 + 每个 key 在两档都非空。F13 说明为什么这是唯一守卫而不是「界面兜底」 |
| **F8** | **表内有五处死条目（实测）**：`settings.shortcut.column.title`／`category`／`binding`／`note` 与 `settings.shortcut.unbound` 在同一张 `kStrings` 里注册两次（快捷键只读表区段给「动作」，快捷键域区段给「动作名」）。`kStrings` 是 `std::map`，initializer-list 构造走 insert 语义 ⇒ **后一份被静默丢弃**，界面生效的是「动作」 | 加 en 档前先收敛这五处，否则两张表各自复制同一处死逻辑。本条是**缺陷修复**而不是本条特性，独立一笔（§4 批 1 的第 0 步）；两份文案的取舍见 D10。**一处形态代价**：从表外看，重复注册会被 map 吸收掉，两档 key 数照样相等 ⇒ 判据 ② 对这一处**天然无感**，所以判据 ⑥ 的证人只能断「这五串逐字等于选定的那一份」，不能断条数 |
| **F9** | **`settings_i18n.h` 有一条注释与实现分叉**：该头 `settings_issue_text` 的注释写「十三个非 `None` 值各有词条」，而 `include/borealis/ui/settings_form.h` 的 `ui::CommitIssue` 实测非 `None` 值**十二枚**，表里也恰是 12 条 `settings.issue.*`，`issue_key()` 的 switch 亦覆盖十二枚且末档 `None` 回空 | 实现是对的、注释是旧的（`.cpp` 里那条注释写的是「十二个」，两处注释自相矛盾）。文档类缺陷，就地改口一笔（§4 批 1），不改可见行为 |
| **F10** | **表外只剩三处上屏中文**（全仓 `src/` 与 `include/` 的引号内含 CJK 实测）：`src/main.cpp` 关标签确认框（「关闭标签」＋「该标签里还有正在运行的进程…」）、`src/ui/terminal_view.cpp` 多行粘贴确认框（「多行粘贴」＋「剪贴板里的内容有 {N} 行…」），两处均已标 `CJK-LITERAL: 上屏文案`；另有一处**不是引号内中文而是直接传给会话的默认标签名** `U"本地终端"`（`src/main.cpp` 建本地标签那一格），而表里**已有同值词条** `connections.type.local` | 双语要真的覆盖界面，这四条必须进表——否则英文界面里会留着两枚中文对话框。成本是四枚 key 加两份文案，不是结构问题。`CJK-LITERAL` 标记随之撤除（进表之后上屏串的真值源在表里，字面量例外也就消失了）；`U"本地终端"` 那处改为复用既有 key，不新增（否则「本地终端」这四个字有两处真值源） |
| **F11** | **OS 平台对话框的标题刻意是英文，且已在册**：`src/main.cpp` 的导出／导入设置与「Choose SSH key directory」三处 `opts.title`，原注释逐字写着「平台对话框的标题由 OS 呈现，不经本仓词条表（S13② 的同一条口径）」 | 本条**不改**这三处（D9）。理由不是省事：让 OS 原生对话框标题去跟一份应用内词条表，是在两套文案纪律里挑一套不稳定的；需求那句「双语」的射程是本仓绘制的界面 |
| **F12** | **八套主题名刻意不在表里**：`itest_settings_panel` 主题卡案的注原文——「名字取的是**存储键名**而不是显示文案（裁决 7.61①）：`BuiltinTheme` 只有 `name` 一个字段，而那句『显示文案另经 `StringTable`』是未落地的规划——词条表里从未登记八套主题名，而查表失败的实测回退是**空串**（G28 那条教训）」 | 主题名（`one-dark` 一类）在本条之后仍不进表：它们是标识符而不是可译短语，翻成「一套深色」反而让用户认不出自己选了哪套。这条既有口径与 F13 合起来是 §7「不进表的三类串」的出处 |
| **F13** | **查表失败的可见形态是空白，不是回落中文**：`LocalizedString::tr(key, args)` 造出的实例 `text` 恒空（该文件头①原话），`StringTable::resolve` 在 `lookup` 两级都未命中时 `return ls.text` | ⇒ 「en 档漏一个 key」的界面后果就是**一处空白标签**（首帧还闪一下）。这就是为什么判据 ② 必须是「两档 key 集合相等」这种**结构式**证人而不是抽样目视；也是为什么本棒不把缺省档留给「另一种语言兜底」——兜底形态是空白，兜不住 |
| **F14** | **没有任何 OS locale 读点**：Aurora 侧 `GetUserDefault*`／`nl_langinfo`／`LC_MESSAGES`／系统语言偏好全仓零命中（`src/aurora/window/x11_surface.cpp` 里那两处 `setlocale(LC_CTYPE, "")` 是为宽字符编码，不是语言）；本仓平台腿（`src/platform/`）亦无任何 locale 接缝 | 「跟随系统」这一档在本仓**落不了**：要么等 Aurora 给公共 API（按 AGENTS.md §5.1 派任务书），要么本仓自开一枚平台接缝。两条都超出 PREF.05 那句「双语」的射程（D3：首版缺省 `zh`、键可改，不做跟随系统） |
| **F15** | **一枚新键＝既有那张六处同步网**：`include/borealis/config/settings.h`（结构体字段）、`src/config/schema_names.h`、`src/config/store.cpp`（读写与装载侧域校验）、`src/config/form_transfer.cpp`（Binding 次序须与 catalog 行序逐位一致）、`src/ui/settings_catalog.cpp`（那一行）、`src/ui/settings_i18n.cpp`（词条），另有 `src/ui/settings_panel.cpp` 的控件分支与两处计数闸（`utest_settings_catalog` 钉的 `keys.size() == 69U` 与 `ConsumerStatus::Absent` 的 `19U`）。先例读数见 `codespec/UI_URL.draft.md` §5 第 ④ 条（键数 65→68、例外叶 4→5、Absent 保持 19） | 加 `appearance.language` 就是这一整套 + 69→70 + 两档各一条词条。这条是 D2 选「启动定档」的**第二条**理由：若要做「面板改完立刻全界面换语言」，除 F6 那 249 处之外还要多开一条运行期接缝，同步面从六处扩到「六处 + 一整套重装配」 |
| **F16** | **生效档位只有两档，语言那一行两档都不是真话**：`include/borealis/ui/settings_catalog.h` 的 `ui::EffectLevel{Immediate, NextSession}`；面板侧唯一消费点是 `settings_panel.cpp` 里 `if (control.effect == EffectLevel::NextSession)` 那一支（挂「下次会话生效」角标） | 语言既不是即时、也不是「下次会话」（会话正在跑，改完不动它；重启才换）。⇒ D5 取**扩一枚 `EffectLevel::NextLaunch`**（新枚举值 + 一处分支 + 一枚新角标词条），而不是把这一行挂进错的档。代价如实登记：这是本条动到 `settings_catalog.h` 这个纯逻辑公共头的唯一原因，判据 ⑤ 钉住「只有语言那一行是 NextLaunch，其余 69 行档位不动」 |
| **F17** | **复数通道框架已有，zh 走 `other`**：`include/aurora/i18n/plural.h` 给 `PluralCategory{Zero,One,Two,Few,Many,Other}`、`plural_category_en`、`plural_category_other_only`；`StringTable::format` 认 `{0, plural, zero=… one=… other=…}` 块。表内带 `{0}` 的模板 **47 条**，其中**计数短语实测 13 条**（`connections.notice.imported`「已导入 {0} 条」、`settings.chain.full`「已达上限 {0} 项…」、`session.reconnect.exhausted`「重试 {0} 次仍失败…」、`terminal.notice.unrepresentable.line`「…里的 {1} 个字符」等） | 英文档若把这 13 条照位置占位直译，产出的就是 "Imported 1 items" 这类错句。zh 侧不受影响（`other_only` 恒走 `other`，故写 plural 块对中文档无害）⇒ D7 取「英文档的计数短语写复数块」，并配一条**独立 utest 证人**（同一 key 在 1／2 两个取值下两档文本），而不是靠目视 |
| **F18** | **开工基线（本棒实测，2026-10-22）**：`ctest --preset linux` **105 项全绿**（`-E etest_` 102 项）、`borealis_test_runner` 全跑 **1295 案全绿**——逐字等于 WS.04 棒的结项读数（裁决 7.129 ⑩），其间本棒**未改一码** | 结项三件齐（ctest 全绿 + runner 案数增量 + 变异证人逐条转红）以这三个数为起点。本条的增量口径是「`utest_i18n_tiers` 新套件 + `itest_settings_panel` 的覆盖案扩形 + `utest_settings_catalog` 的 69→70 与 `NextLaunch` 案」 |

---

## 1 本条要兑现的一件事，与需求没写的部分

需求那句「中文/英文首版双语，经 Aurora `StringTable`」落成**一条链**：表里第二档（`en`）逐 key 补齐英文文案 → 装填函数按启动读到的语言档设缺省 → 面板多一行 `appearance.language`（六处同步 + 一枚新角标档） → 表外那四处上屏中文收进表。链上唯一的**新机制**是「档位不再写死为 `zh`」，其余全是既有接缝的搬运与文案本体。

需求没写而开工前必须定的四件事，本稿的处理：

- **切换何时生效**（D2）：启动定档、重启生效。不是运行期切换——F6 那 249 处急切解析在运行期不会跟着换，F5 那几处框架侧 `Locale{}` 要补的是 Aurora 的账。
- **缺省是哪一档**（D3）：`zh`。全仓现有词条、既有判据、既有走查截图与十余条断言中文上屏的用例都以中文为既成形态；把缺省改 `en` 是拿一次纯审美变更换一片既有用例的改口面。不跟随系统（F14 落不了）。
- **英文覆盖到哪**（D1）：**全量 464 个 key**，不是分区。半覆盖的英文界面是中英夹杂（且缺那一条的可见形态是**空白**，F13），比整屏中文更难用。
- **要不要英文复数**（D7）：要，仅限那 13 条计数短语。框架已有通道（F17），不做就是明知故犯地产错句。

---

## 2 交付判据（①③④⑤⑥ 有无头/纯逻辑证人；② 是结构式证人；⑦ 无证人）

1. **语言档就是上屏档**：`appearance.language` 取 `zh` 时全界面为中文、取 `en` 时为英文，两档由同一条装填腿产出。证人形态＝同一批 key 在两种装填下 `settings_label(key)` 分别命中中／英两档（不比对具体译文，只比对「档位决定命中哪一层」这一事实）。这条挡住的正是 F3 那个反转：**没有它，补上 en 档就等于把界面默认换成英文**。
2. **两档 key 集合逐字相等，且逐 key 两档都非空**：新增套件 `utest_i18n_tiers`（新纯逻辑接缝 `settings_string_keys(tag)` 给用例侧列出某一档的全部 key）断 ①zh 的 key 集与 en 的 key 集排序后逐字相等 ②每个 key 在两档 `lookup` 均有值 ③两档条数同为 464。这条是 F7/F13 那两条读数唯一的结构式守卫，也是 7.25⑬「新增命令须同步词条」第一次有自动化证人。
3. **英文档的计数短语走复数**：那 13 条 key 在数值 1 与 2 下渲染出两个不同串（`one` 与 `other` 分支），中文档在同一 key 同一两个数值下**同串**（`other_only`）。证人独立成案，不与判据 ② 合并——② 红是漏 key，这条红是分支选错。
4. **表外那四条中文消失**：关标签确认框、多行粘贴确认框、默认标签名「本地终端」三处改消费词条；英文档下这三处上屏为英文且**不再需要 `CJK-LITERAL` 标记**（该标记随字面量一起消失，不是加一条例外）。默认标签名复用既有 `connections.type.local`，表内不新增 key。
5. **面板那一行的档位是真话**：`appearance.language` 在 catalog 里恰有一行、`ConsumerStatus::Wired`、`EffectLevel::NextLaunch`；面板上该行挂「重启后生效」角标，其余 69 行的档位与角标逐字不动（证人＝逐行读 `effect` 字段比对，新枚举值只被这一行取用）。
6. **五处死条目收敛后界面按选定那份走**：`settings.shortcut.column.*` 与 `unbound` 各只注册一次，中文档逐字断言为 D10 选定的那五串（表头四列「动作名／分组／当前组合键／标注」＋「未绑定」），且**英文档**同五 key 也逐字断言一份。证人形态是指验字符串而不是指验条数——F8 那条注说了为什么条数类断言对这一处天然无感。
7. **启动读键那一行无自动化证人**：`src/main.cpp` 不进 runner（裁决 7.83 的物理事实），故「`store.settings().appearance.language` → 装填参数」这一句只有编译＋链接＋§6 走查兜底。本条**不称变异自证**这一条，也不为它造一个假宿主（与 WS.04 判据 ⑦ 同档登记）。

判据 ①②③⑤⑥ 全部在纯逻辑／无头通道，④ 的表本体有证人、三处消费点在 `main.cpp` 与视口（视口那处有 `itest_*` 宿主可跑，另两处属 `main.cpp` ⇒ 与 ⑦ 同档），如实登记见 §8。

---

## 3 待人裁决（每条已给推荐 ①；按常存授权全按 ① 收口）

| 号 | 问题 | 推荐档 ① | 备选与代价 | 收口 |
|:---|:---|:---|:---|:---|
| **D1** | 英文覆盖面 | **全量 464 个 key 两档齐全** | ②分区（先外观／终端／连接三域）＝英文界面夹中文，且夹的那一处是**空白**（F13）而不是中文；③只做面板＝命令面板、侧栏、对话框、SFTP／密钥／隧道六面全英文缺席 | 按 ①。分四批按域推（§4），每批都带判据 ② 的证人，所以「推一半就停」在结构上不可能静默发生 |
| **D2** | 切换时机 | **启动定档、重启生效** | ②运行期全量切换＝重建命令表 + 逐窗重装配（F6 那 249 处），且仍漏 F5 那几处框架侧 `Locale{}`；③运行期切换且只切「还没解析的」＝中英混排 | 按 ①。代价逐字登记：面板改语言后**本进程界面不变**，故必须有「重启后生效」那一枚角标（D5） |
| **D3** | 缺省档 | **缺省 `zh`，键值 `zh`／`en` 白名单** | ②缺省 `en`（既有用例断中文上屏者全改口，且需求首版双语的中文侧是既成事实）；③跟随系统（F14：无任何 OS 读点，落不了） | 按 ①。不写盘即等于今日行为，与 `SPEC.FEAT.PREF.06` 零配置可用同口径 |
| **D4** | 表形态 | **两张表同文件相邻**（`kStringsZh` 之后紧跟 `kStringsEn`），`install_settings_strings()` 收一枚语言参数、两档都装、缺省档随参数 | ②按域拆多个文件＝464 条 key 的「两档相等」证人要跨文件聚合；③en 独立 TU＝同 ②，且 `settings_i18n.cpp` 现在是唯一装填点，拆它就是把 F2 那句「全仓 `Locale` 引用只在这一处」主动打破 | 按 ①。文件从约 700 行涨到约 1150 行，换来的是「同一条目两档相邻可并排校对」；行数不是本仓的门禁项 |
| **D5** | 面板那一行的生效档 | **`EffectLevel` 扩一枚 `NextLaunch`**（+一处分支 +一枚角标词条） | ②挂 `NextSession`＝角标说「下次会话生效」而事实是重启，把一条错口径写进界面；③不给角标＝用户改完看不到任何后果说明 | 按 ①。代价如实登记：动了 `settings_catalog.h` 这个纯逻辑公共头的枚举（追加式、成员与可见性不动，与 7.110 D1①／7.112 D1① 同形态） |
| **D6** | `settings_label` 那 249 处 | **一处不改**（继续急切解析） | ②全量改 `settings_text`＝控件侧要能收 `LocalizedString` 才有意义，而 249 处里多数入口（`Command::title`、角标拼接、`placeholder`）本就不收（G28 那棒已把这条口径写进文件头）；③改一半＝第二处真值源 | 按 ①。启动定档下急切解析是**正确的**：解析时点已在装填之后，档位一经定下整进程不变 |
| **D7** | 英文复数 | **13 条计数短语写 `plural` 块**，其余 `{0}` 直译 | ②全不写＝"Imported 1 items"；③全部写＝47 条里 34 条的位置参数根本不是计数（版本号、路径、色值），写复数块是把语法判断塞进不需要它的模板 | 按 ①。中文档同条也一并写成 plural 块（`other_only` 恒走 `other`，行为不变、形态统一，判据 ③ 的中文侧断言由此可证） |
| **D8** | 语言键的域与页 | **`appearance.language`，外观页第一区段** | ②放 `general` 域＝schema 没有这一域（`config::Settings` 四域＝appearance/terminal/connection/shortcuts）；③放终端页＝与终端语义无关 | 按 ①。「界面语言」在既有四域里唯一归属就是外观 |
| **D9** | OS 对话框标题／主题名／命令 id | **三类都不进表**（F11／F12／7.25⑬ 三条既有口径原样维持） | ②标题也翻＝两套文案纪律并存，且 OS 呈现不受本仓字号与字体腿影响；③主题名翻＝用户认不出自己选的哪套 | 按 ①。§7 逐条在册 |
| **D10** | 快捷键表头那五处死条目取舍 | **让「动作名」那一份生效**（列头显示「动作名」），物理落点取靠前那段 | ②让「动作」生效＝丢掉更准的措辞；③两份都留并把容器换成 `std::vector`＝给表加一层「同 key 后者覆盖」的语义，而框架 `add` 本来就是这个语义，多此一举 | 按 ①。**出稿后的就地更正**：稿面原写「删掉靠前那五行、让快捷键域那段生效」，实现时改形——靠前的『快捷键只读表』段注释自陈「表体四列的列名是本件自己的排版量而非 catalog 键」，那才是这五串的真实归属，故**删掉后面那五行、把前面那一份的 `title` 值改成「动作名」**。可见结果与本条原文一致（列头是「动作名」），变的只是五条注册住在哪一段；此改动**可见**（列头四字改三字），故须走 §6 走查第 ⑤ 条并被判据 ⑥ 钉住 |
| **D11** | 表外那四处（D 的落点） | **本条一并收进表** | ②登记在册留给各自域＝英文界面留两枚中文对话框，双语首版名不副实 | 按 ①。四枚 key（其中默认标签名复用既有 key）；`main.cpp` 那两处的消费点无证人，与判据 ⑦ 同档 |
| **D12** | 是否给「语言」加运行期接缝（面板改完提示重开） | **只改角标文案，不做提示条** | ②弹一条「需重启」提示＝为一行设置新起一套提示机制（TERM.09 那张卡的语义是「本仓自己发不出去」，不同档） | 按 ①。判据 ⑤ 那条角标就是全部的用户可见说明 |

---

## 4 落地形态与分批（每批一个分层提交，AGENTS.md §4.6 第 26 条）

| 批 | 内容 | 触及 |
|:---|:---|:---|
| **批 0** | 本稿与 §0 地基实测 | `codespec/PREF05_I18N.draft.md`（新） |
| **批 1** | 表本体收敛：删五处死条目（D10，「动作名」生效、五行住在『快捷键只读表』段）、`settings_i18n.h` 那条「十三」改口（F9）、`kStrings` 更名 `kStringsZh`；判据 ⑥ 的中文半边随本批落（证人放 `itest_settings_panel.cpp`——它是唯一已 include 该私有头的套件，新建 `utest_*` 去引 `src/ui/` 私有头本仓无先例）。本批**行为零变化**，除列头「动作名」一处 | `src/ui/settings_i18n.{h,cpp}` + `tests/integration/itest_settings_panel.cpp` |
| **批 2** | 语言接缝：`install_settings_strings(Language)`（或 `std::string_view tag`）参数化、`settings_locale()` 随启动档位、新增 `settings_string_keys(tag)`、装填两档、缺省档随参数（判据 ①② 的宿主）；`appearance.language` 的结构体字段与 schema 名与 store 读写（六处同步的前三处） | `src/ui/settings_i18n.{h,cpp}` + `include/borealis/config/settings.h` + `src/config/{schema_names.h,store.cpp}` + `tests/unit/utest_i18n_tiers.cpp`（新）+ `tests/unit/utest_config.cpp` |
| **批 3** | 英文文案本体，按域四段推：连接侧（`connections.*`／`keys.*`）→ 面板侧（`settings.*`）→ 终端与会话侧（`terminal.*`／`session.*`／`workspace.*`／`links.*`／`search.*`／`diagnostics.*`／`menu.*`／`tab.*`／`command_palette.*`）→ 连接域与其余（`connection.*`／`serial.*`／`sftp.*`／`tunnel.*`／`shortcuts.*`／`appearance.*`）。每段完即跑判据 ②（未齐即红） | `src/ui/settings_i18n.cpp`（`kStringsEn`） |
| **批 4** | 复数：13 条计数短语两档改写 `plural` 块 + 判据 ③ 的案 | `src/ui/settings_i18n.cpp` + `tests/unit/utest_i18n_tiers.cpp` |
| **批 5** | 面板那一行与 `NextLaunch`：catalog 那一行、`EffectLevel` 追加值、面板角标分支与词条、`form_transfer` 的 Binding 插入位（次序纪律）、`utest_settings_catalog` 的 69→70 与 `Absent` 19 复核 | `include/borealis/ui/settings_catalog.h` + `src/ui/{settings_catalog.cpp,settings_panel.cpp,settings_i18n.cpp}` + `src/config/form_transfer.cpp` + `tests/unit/utest_settings_catalog.cpp` + `tests/unit/utest_form_transfer.cpp` + `tests/integration/itest_settings_panel.cpp` |
| **批 6** | 表外那四处进表（D11）+ 装配腿（启动读键 → 装填参数）+ 判据 ② 扩到「覆盖闸两档都查」 | `src/main.cpp` + `src/ui/terminal_view.cpp` + `src/ui/settings_i18n.cpp` + `tests/integration/itest_settings_panel.cpp` + `tests/integration/itest_startup_notice.cpp`（若列头那处有证人） |
| **批 7** | 文档回写与变异自证：`SPECIFICATIONS.md` 的 PREF.05 现状句 + 新裁决 + `CHANGELOG.md` + `PLAN.md` 的 M4 行 + `ARCHITECTURE.md` 的词条表归属 + `UI_SETTINGS.draft.md` 那一行 | `codespec/` |

批 3 之所以按域切四段而不是一笔：464 条英文文案是一笔里最难复核的体量，而判据 ② 在每一段完就能独立给出「还差多少个 key」的读数——这是本棒能在中途被纠偏的唯一机制。

---

## 5 数据流与不变量

1. **一个进程一种语言**：装填发生在建窗之前（`src/main.cpp` 注释原话「词条必须在任何控件绘制之前装填：查表失败回退空文本」），启动档位一经读定，整进程内 `settings_locale()` 恒等。判据 ① 与 D2 合起来把这条锁成不变量。
2. **两档同 key 集**：`kStringsZh` 与 `kStringsEn` 的 key 集合相等是**判据**而非约定——由 `utest_i18n_tiers` 每次全跑钉住。任何新界面串必须先加两档才能上屏（7.25⑬ 那条纪律自此有证人）。
3. **缺省档＝用户选的那一档**：`set_default_locale` 从「中文上屏的技巧」变成语言选择本身；F3 那个反转由批 2 的参数化消除。
4. **命令 id 恒英文**：`CommandRegistry` 的 id 与快捷键覆盖表不随语言变（7.25⑬），本棒不碰 id；面板与命令面板显示的是词条，落盘的是 id。
5. **域校验只在装载侧**：`appearance.language` 的白名单 `{zh, en}` 由 `store.cpp` 把守（裁决 7.46② 分工），面板与装填侧都不夹紧、不自造回落。域外值＝rejected 留痕 + 回落缺省 `zh`。
6. **词条表是上屏文案的唯一真值源**（对界面而言）：F10 那四处进表后，`src/` 与 `include/` 内不再有引号内 CJK 的上屏串（除词条表本体与用例侧的断言字面量）。这条可以做成一条**门禁式**核对，本棒先以判据 ④ 与 §6 走查覆盖，是否升为常驻脚本留批 7 定（AGENTS.md §4.4 第 22 条要求任何常驻脚本自带变异自证）。

---

## 6 真机走查清单（未走完不得称「可用」，AGENTS.md §4.4 第 33 条）

1. 语言键取 `en` 重启：命令面板、设置四页、连接侧栏、SFTP／密钥／隧道三面、搜索浮层、右键菜单逐屏走查，不得残留中文短语，也不得出现空白标签（F13 的形态）。
2. 语言键取 `zh` 重启：读数与本棒开工前**逐屏相同**（判据 ① 的反向半边）。
3. 英文档下的排版：栏位宽钳 `[96, 240]`（WS.04 F15 同源那条）在英文标签名下不破；设置面板列宽与角标在英文措辞更长处（"Next session" 一类）不换行压字。
4. 复数：`en` 档下导入 1 条与 2 条配置、重连 1 次与 2 次、多行粘贴 1 行与 5 行，各读一遍措辞。
5. 快捷键页列头按 D10 变成「动作名」一侧的英文与中文两档读数。
6. 改语言后不重启：界面不变、该行角标为「重启后生效」（D2／D5 的用户可见半边）。
7. 域外值：手工把配置里的语言值改成 `fr`，重启后回落中文且配置未被写坏（装载侧留痕）。
8. 英文档下关标签与多行粘贴两枚确认框为英文（判据 ④ 的可见半边）。

---

## 7 本期不做（登记在册，不是漏项）

- **运行期切换语言**：由 F5（框架侧多处 `Locale{}`）与 F6（本仓 249 处急切解析）两条实测共同排除。真要做，第一笔在 Aurora 侧（把 `rich_text`／`reorderable_list`／两处 `accessibility_label` 的解析入口收进 `ctx.environment<Locale>()`），第二笔才是本仓装 `LocaleProvider` 与重装配命令表。按 AGENTS.md §5.1，本棒**不预先自建**任何一侧。
- **跟随系统语言**：F14 无 OS 读点。出路是 Aurora 给公共 locale 偏好 API，或本仓 `src/platform/` 新开一枚接缝——两者都不是「双语首版」的必要条件。
- **三类串不进表**：OS 原生对话框标题（F11）、八套主题名（F12）、命令 id（7.25⑬）。
- **阿拉伯语／日语等第三档**：`StringTable` 支持任意 tag，但需求只写中英；新增一档就是新增 464 条文案，取「将来按同一判据 ② 加档」的口径，不在本棒留空壳。
- **复数之外的 i18n 语法面**（性别、格变化、`select` 块）：框架 `format` 只有位置占位与 CLDR plural 两种语法，本棒不自造第三种。
- **`settings.shortcut.*` 之外的表内文案润色**：本棒只改语言档，不改中文措辞。D10 那一处是唯一被授权的中文侧文案变化。

---

## 8 判据与变异计划＋实测读数（结项三件齐：ctest 全绿 + runner 案数增量 + 变异证人逐条转红）

计划六条注入，每条都要指名它打到的**独立**断言；打出零红或一片红的，按等价变异处理并写明等价原因（AGENTS.md §4.4 第 22 条）。

| 号 | 注入 | 预期转红的证人 |
|:---|:---|:---|
| m1 | 装填时把缺省档写死回 `zh`（忽略参数） | 判据 ① 的 `en` 半边 |
| m2 | `kStringsEn` 删掉一条 key | 判据 ② 的「两档 key 集相等」 |
| m3 | 让 `settings_string_keys("en")` 返回 zh 的 key 集 | 判据 ②（证件自身被反证，说明它不是空转） |
| m4 | 13 条计数短语里的 `plural` 块退化为 `other` 单形态 | 判据 ③ 的 `en` 档 1／2 两串不等那条 |
| m5 | catalog 那一行的 `effect` 改成 `Immediate` | 判据 ⑤（角标与档位断言） |
| m6 | 在生效那五行的**前面**重新加一份 `{"settings.shortcut.column.title", "动作"}`（把 F8 的形态注回去；加在后面的重复会被 `std::map` 吸收，打不中任何断言——这正是 F8 那条注说的「条数类断言天然无感」） | 判据 ⑥（表头那五串逐字验） |

**批 1 读数**（表本体收敛那笔，本棒实测）：m6 注入形态＝在生效那五行**前面**加回一份 `{"settings.shortcut.column.title", "动作"}`（即 F8 的原始形态），`ctest --preset linux -R itest_settings_panel` 转红、`--filter five_shortcut` 读数**恰 1 案红**且红的正是 `title` 那一行断言（`"??????"` vs `"?????????"`，即三字对四字）；还原后 `ctest --preset linux` **105/105**、`borealis_test_runner` **1296 案全绿**（开工基线 1295 + 本案）。同批可见变化一处：快捷键只读表列头从「动作」变成「动作名」（D10 选的正是这一份）。

**结项读数**：待补（批 7 回填实测三项数与逐条注入的红案数，含等价变异指名道因）。
