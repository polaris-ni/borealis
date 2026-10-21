# PREF05_I18N.draft.md — `SPEC.FEAT.PREF.05` i18n 双语首版设计稿

> **状态（2026-10-22 出稿）**：批 0＝本稿与 §0 地基实测；**批 1 已落**（表本体收敛：五处死条目、`settings_i18n.h` 那句「十三」改口、`kStrings` 更名 `kStringsZh`、判据 ⑥ 的中文半边随案落地；读数见 §8）。批 2 起为英文档与语言接缝。稿面 §3 的 D1–D12 按人的常存授权（「后续再遇到问题则你自行按照推荐去做」）**全按推荐档 ① 收口**，收口结论逐条写在 §3 的「收口」列里；其中 D2／D5 的形态由 §0 的 F6／F15／F16 定形，出稿即已实测、不留待实现阶段。**D10 的物理落点在批 1 实现时改形一次**（可见结果不变），逐字记在该行「收口」列。
> **本条不称「可用」**（AGENTS.md §4.4 第 33 条）：§6 的走查项一条未做，且本沙箱只有 POSIX 腿——Windows 侧字体能否排出英文档之外的任何形态一律标**推断／未验证**。

---

## 0 地基实测（F1–F19，2026-10-22，POSIX 腿 + Headless；Windows/MSVC 一律标推断／未验证）

本棒形态由下面十九条读数定形。与前几棒不同，本棒的**既有地基不是空的**：词条表、装填点、覆盖闸都已在了，所以 §0 读的全部是「这一套在加第二档时会从哪里断裂」，而不是「有没有接缝」。**F19 是落地阶段（批 2 开工）才读得到的一条**（一次性探针，探针件跑完即删、不入册），按 AGENTS.md §4.2 第 11 条追加在本节而不是藏在 §4 的批次表里——它改的是批 2 的**必需项清单**。

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
| **F19** | **落地阶段读数（批 2 开工探针，跑完即删）**：往表里注册**一枚** `Locale{}`（＝`en`）档的 `settings.close`，其余全部仍是中文档，`ctest --preset linux` 立刻**一项转红**、runner 侧**恰 1 案红**——红的是 `itest_settings_panel.a_button_label_comes_from_the_framework_string_table_G28`，读数 `the button label matches none or several of the panel's entries: PROBE_EN_CLOSE`。F3 推出来的那一件事在帧上兑现了：`Button::resolved_label(ctx)` 取的是 `ctx.environment<Locale>()`，而测试宿主与生产现场都没有装 `LocaleProvider` ⇒ 走回落档 `Locale{}`＝`en`，**命中 en 档而不是缺省档 zh** | 三件事因此定形：**① 装 `au::LocaleProvider` 不是「运行期切换语言」的装饰，而是「补了英文档之后中文档仍然显中文」的必要条件**（批 2 的必需项，与语言键同批）；**② 本案是全仓目前唯一会为此转红的证人**——其余用例断的是 `settings_label()` 的返回值（显式带 `settings_locale()`），换档时对它们毫无影响，所以「没红」不等于「界面正确」，像素类案更是只看两帧之差而不知其语言；**③ 探针只注册一枚 key 就能定位到证人，说明这条读数的成本是接线而非文案**：本棒的接线面＝生产那一棵 `ThemeScope` 之外的一层 ＋ 用例侧的驱动台 |

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

1. **语言档就是上屏档**：`appearance.language` 取 `zh` 时全界面为中文、取 `en` 时为英文，两档由同一条装填腿产出。证人形态＝同一批 key 在两种装填下 `settings_label(key)` 分别命中中／英两档（不比对具体译文，只比对「档位决定命中哪一层」这一事实），**外加 F19 那半条**：绘制侧的文案（框架 `Text`／`Button` 经 ctx 解析的那一路）也随档位走，其证人只能是装了 `LocaleProvider` 的驱动台上取 `accessibility_label()` 之类的**解析后显示串**——G28 那一案就是现成的宿主（F19 读的正是它先红）。这条挡住的正是 F3 那个反转：**没有它，补上 en 档就等于把界面默认换成英文**。（落地形态到批 3 段 2 定为三件：等式案、金样案、进程 locale 案——第一件与装填腿共享 `source_tier()` 读点，对 m1 失明，故补第二件，见 `§4.1` 第 5／6 条。）（**批 3 段 4 更正**：F19 那半条的证人形态要撤——摘掉驱动台那枚 provider 实测 **0 红**，`§4.1` 第 7 条给出等价链条，G28 那一案自此退出本条证人列；本条的自动化半边只剩那三件，绘制侧那半边与判据 ⑦ 同档（只有编译＋链接＋§6 走查）。）
2. **两档 key 集合逐字相等，且逐 key 两档都非空**：新增套件 `utest_i18n_tiers`（新纯逻辑接缝 `settings_string_keys(tag)` 给用例侧列出某一档的全部 key）断 ①zh 的 key 集与 en 的 key 集排序后逐字相等 ②每个 key 在两档 `lookup` 均有值 ③两档条数同为 464。这条是 F7/F13 那两条读数唯一的结构式守卫，也是 7.25⑬「新增命令须同步词条」第一次有自动化证人。
3. **英文档的计数短语走复数**：那 13 条 key 在数值 1 与 2 下渲染出两个不同串（`one` 与 `other` 分支），中文档在同一 key 同一两个数值下**同串**（`other_only`）。证人独立成案，不与判据 ② 合并——② 红是漏 key，这条红是分支选错。（**批 4 落地期更正**：13 条里只有 5 条在英文侧是数词紧邻可数名词的形态，其余 8 条写不出 `one`／`other` 的差异，故本条的范围与「不同串／同串」的可观测形态按 `§4.1` 第 8 条收窄；证人随批 4 落 `the_count_phrases_decline_in_english_but_not_in_chinese`。）
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
| **D4** | 表形态 | **两张表同文件相邻**（`kStringsZh` 之后紧跟 `kStringsEn`），`install_settings_strings()` 收一枚语言参数、两档都装、缺省档随参数 | ②按域拆多个文件＝464 条 key 的「两档相等」证人要跨文件聚合；③en 独立 TU＝同 ②，且 `settings_i18n.cpp` 现在是唯一装填点，拆它就是把 F2 那句「全仓 `Locale` 引用只在这一处」主动打破 | 按 ①。文件从约 700 行涨到约 1150 行，换来的是「同一条目两档相邻可并排校对」；行数不是本仓的门禁项。**落地期改形（批 2）**：原写「两档都装」改为**只装选定那一档**（`§4.1` 第 2 条）——两档齐装会让 F5 那几处写死 `Locale{}` 的解析口在中文档下命中英文层，正是 F19 探针实测到的那一件事；只装选定档并把缺省档随参数，`Locale{}` 的第一级 miss 才回落得到用户真正选的那一档。判据 ② 的「两档 key 集相等」因此读**源表**（新接缝 `settings_string_keys(Language)`），不读装填后的 `StringTable`。 |
| **D5** | 面板那一行的生效档 | **`EffectLevel` 扩一枚 `NextLaunch`**（+一处分支 +一枚角标词条） | ②挂 `NextSession`＝角标说「下次会话生效」而事实是重启，把一条错口径写进界面；③不给角标＝用户改完看不到任何后果说明 | 按 ①。代价如实登记：动了 `settings_catalog.h` 这个纯逻辑公共头的枚举（追加式、成员与可见性不动，与 7.110 D1①／7.112 D1① 同形态） |
| **D6** | `settings_label` 那 249 处 | **一处不改**（继续急切解析） | ②全量改 `settings_text`＝控件侧要能收 `LocalizedString` 才有意义，而 249 处里多数入口（`Command::title`、角标拼接、`placeholder`）本就不收（G28 那棒已把这条口径写进文件头）；③改一半＝第二处真值源 | 按 ①。启动定档下急切解析是**正确的**：解析时点已在装填之后，档位一经定下整进程不变 |
| **D7** | 英文复数 | **13 条计数短语写 `plural` 块**，其余 `{0}` 直译 | ②全不写＝"Imported 1 items"；③全部写＝47 条里 34 条的位置参数根本不是计数（版本号、路径、色值），写复数块是把语法判断塞进不需要它的模板 | 按 ①。中文档同条也一并写成 plural 块（`other_only` 恒走 `other`，行为不变、形态统一，判据 ③ 的中文侧断言由此可证；**批 4 实测：中文侧的块与非块渲染逐字同串，m4_zh 注入 0 红，故中文侧那半边只有形态收益、没有证人，见 `§4.1` 第 8 条与 §8 批 4 读数**）。13 条按落地期收窄为「5 条成块 + 8 条不成块」，逐枚理由见 §8 批 4 读数 |
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
| **批 2** | **接缝与那一行**（原表把这一行拆到批 2／批 5 两处，现合并，理由见 §4.1 第 1 条）：① 语言接缝——`install_settings_strings(Language)` 参数化 + 一枚收 `StringTable &` 的同名重载（装进指定表，判据 ①② 的无副作用宿主）、`settings_locale()` 随启动档位、新增 `settings_string_keys(Language)`；**只装选定那一档**（D4 落地期改形，见该行）② `appearance.language` 的六处同步全套：结构体字段 + `kInterfaceLanguageNames` 与 `language_tag()` + store 读写 + `form_transfer` 的 Binding（次序随 catalog）+ catalog 那一行 + 中文词条两行 ③ `EffectLevel::NextLaunch` 与面板角标分支 ④ **装 `au::LocaleProvider`**（F19 的必需项：生产那一层场景根 ＋ 用例侧驱动台各装一枚）⑤ 装配腿＝启动读键 → 装填参数（原表写在批 6，随 ① 同批才有产出方）⑥ 判据 ①⑤ 的证人与两处计数闸 69→70 | `include/borealis/{config/settings.h,ui/settings_catalog.h}` + `src/config/{schema_names.h,store.cpp,form_transfer.cpp}` + `src/ui/{settings_i18n.{h,cpp},settings_catalog.cpp,settings_panel.cpp}` + `src/main.cpp` + `tests/unit/utest_i18n_tiers.cpp`（新）+ `tests/unit/{utest_config.cpp,utest_settings_catalog.cpp}` + `tests/integration/itest_settings_panel.cpp` |
| **批 3** | 英文文案本体，按域四段推：连接侧（`connections.*`／`keys.*`）→ 面板侧（`settings.*`）→ 终端与会话侧（`terminal.*`／`session.*`／`workspace.*`／`links.*`／`search.*`／`diagnostics.*`／`menu.*`／`tab.*`／`command_palette.*`）→ 连接域与其余（`connection.*`／`serial.*`／`sftp.*`／`tunnel.*`／`shortcuts.*`／`appearance.*`）。**判据 ② 的等式与 m7 随本批最后一段落定**（§4.1 第 3 条；段 4 实测：等式案成立，m7 是等价变异 ⇒ 判据 ① 的绘制侧半边无证人，见 `§4.1` 第 7 条），判据 ③ 的复数形态随批 4；四段中途的「还差多少 key」用一次性探针读数 | `src/ui/settings_i18n.cpp`（`kStringsEn`）+ `tests/unit/utest_i18n_tiers.cpp` |
| **批 4** | 复数：13 条计数短语两档改写 `plural` 块 + 判据 ③ 的案（落地期按 `§4.1` 第 8 条收窄为「5 条成块 + 8 条不成块」，判据 ③ 那一案同时记录「两串不等」这一句对 m4 空转的事实） | `src/ui/settings_i18n.cpp` + `tests/unit/utest_i18n_tiers.cpp` |
| **批 5** | **（取消）** 内容即上面批 2 的 ②③⑥ 与批 3 的判据 ② —— 六处同步跨批会让 `utest_settings_catalog` 的双向比对先红，而面板角标分支不能与它自己的生效档分家（§4.1 第 1 条）。编号按「只追加、不重排」留位不复用 | — |
| **批 6** | 表外那四处进表（D11）+ 判据 ② 扩到「覆盖闸两档都查」 | `src/main.cpp` + `src/ui/terminal_view.cpp` + `src/ui/settings_i18n.cpp` + `tests/integration/itest_settings_panel.cpp` + `tests/integration/itest_startup_notice.cpp`（若列头那处有证人） |
| **批 7** | 文档回写与变异自证：`SPECIFICATIONS.md` 的 PREF.05 现状句 + 新裁决 + `CHANGELOG.md` + `PLAN.md` 的 M4 行 + `ARCHITECTURE.md` 的词条表归属 + `UI_SETTINGS.draft.md` 那一行 | `codespec/` |

批 3 之所以按域切四段而不是一笔：464 条英文文案是一笔里最难复核的体量，而判据 ② 在每一段完就能独立给出「还差多少个 key」的读数——这是本棒能在中途被纠偏的唯一机制。

### 4.1 批次边界的落地期更正（批 2 开工时定，批 3 段 1 补第 5 条、段 2 补第 6 条、段 4 补第 7 条、批 4 补第 8 条；AGENTS.md §4.2 第 11／12 条）

八条读数让上面那张表不能照原样执行。**改的只有批次切分、装填形态与证人形态**，§2 的七条判据与 §3 的十二条裁决一律不动——第 8 条是本前言的第一次例外，改的是判据 ③ 的范围而不是它的口径。

1. **一枚新键的六处同步不能跨批**（原表把 `appearance.language` 的结构体/schema/store 放批 2，把 catalog 行、`form_transfer` 的 Binding 与两处计数闸放批 5）。`utest_settings_catalog.table_covers_exactly_the_written_schema_keys` 拿**落盘叶子集**与 **catalog 键集**做排序后的双向逐字比对，只落前三处的中间态就是「落盘里有而表里没有」⇒ 该案当场转红，批 2 便无法以「全绿 + 本地提交」结项（AGENTS.md §4.4 第 33 条）。面板角标分支同理：catalog 那一行的生效档一旦先挂 `NextSession`，就是把 D5 已裁掉的错口径画上界面再改一次。故六处同步 + `NextLaunch` + 角标分支 + 计数闸**整体并入批 2**，原批 5 取消。
2. **「两档都装」改为「只装选定那一档」**（D4 的落地期改形，逐字记在该行）。理由与 F5／F19 同源：`lookup` 的第一级用调用方给的 tag，而框架内那几处不经 ctx 的解析口写死 `Locale{}`（＝`en`）——两档齐装会让它们**在中文档下命中英文层**，即把「未装 provider」的那一处重新变成 F19 的事故。只装选定档 + 缺省档随参数，才让「回落」落回用户选的那一档。
3. **判据 ②③ 与 m7 的常驻证人排在英文档本体之后**：英文档为空表时，「两档 key 集相等」「同一 key 两档文本不同」「摘掉 provider 会红」三类断言**全部空转**（AGENTS.md §4.4 第 22 条要求证人非空转）。故批 2 的 `utest_i18n_tiers` 只落判据 ① 的那一半（同一批 key 在两种装填下分别命中中／英两档——本批只有中文档有内容，故这一半的形式是「装填腿与 `settings_string_keys` 一致」＋缺省档随参数），三类常驻案随批 3 落。批 3 内部四段的「还差多少 key」以**一次性探针**读数（跑完即删、不入册，与 F19 同一条口径）。
4. **附带更正批 1 那句注的口径**：`tests/unit/` 引 `src/ui/` 的私有头**有先例**——`utest_connection_glyph.cpp` 引 `ui/connection_glyph.h`，runner 的 include 路径含 `src/`（`cmake/BorealisTests.cmake` 那条 `target_include_directories(... PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")`）。批 1 那句「`itest_settings_panel` 是唯一已 include 该私有头的套件」当时对 `settings_i18n.h` 本体为真，但不构成新建 `utest_i18n_tiers` 的障碍，故该套件按原表落地。
5. **证人形态改成等式（批 3 段 1 开工时就地更正，AGENTS.md §4.2 第 11 条）**：批 2 落的那两句「另一档查不到」在英文档还是空表时成立，而**第一段本体一落就必然翻转**——正是第 3 条自己禁止的那一类断言。新形态是「**装填表的读数＝源表模板**」的等式：新增私有接缝 `settings_string_text(Language, key)`（读源表，该档没这一 key 回空串），用例把「按另一档的 tag 去查也只能拿回选定那一档」写成逐 key 等式，于是这条证人在英文档从 0 条长到 466 条的全过程中形态不变，且它比原句更强——等式案还能抓到「模板是空串」这种 `lookup` 报命中而 `settings_string_text` 报缺失的中间态（见 §8 段 1 读数的 p2）。判据 ② 同口径拆成**子集**先行（`every_english_entry_has_a_non_empty_chinese_twin`：英文档每一 key 都在中文档有非空孪生条目），反向等式与「两档 key 集逐字相等」随段 4 落齐一起补。§8 的 m1／m3 落点随本次改形重述。
6. **等式证人对 m1 失明，故补一份不共享读点的金样案（批 3 段 2 实测，AGENTS.md §4.4 第 22 条）**：段 2 复跑第 5 条改形后的注入，发现 m1 的两种形态（只写死 tag／连源表一起写死）在 `installing_registers_only_the_selected_tier` 上都是 **0 条读数红**——等式右边经由 `settings_string_text()` 读的就是被变异写死的同一个 `source_tier()`，两侧同串即自反成立。段 1 那笔「m1 全形态 459 条红」的读数按本条更正（该探针不可复现，且段 2 的复现结果与它相反），m1 的落点自此改记为：全形态⇒金样案＋进程 locale 案，弱形态⇒进程 locale 案。补的案是 `installed_tiers_answer_templates_pinned_in_the_test`：五对期望串**抄在测试里**（不经被测接缝），代价是改措辞要同步改金样，收益是 m1 由「读点共享」变成「可抓事实」。同一批里 m1 弱形态不再是等价变异（段 1 记的 0 红随之作废）：进程级 `settings_locale()` 的 tag 是唯一观测面，而它确实被写死了。
7. **`LocaleProvider` 在四段落齐后仍然不可观测，m7 自此按等价变异结项（批 3 段 4 实测，AGENTS.md §4.4 第 22 条）**：段 3 末那句「m7 的红案数随段 4 落齐一起补」落空了——摘掉 `itest_settings_panel` 的 `Harness` 里那枚 provider（绘制侧从此按 `Locale{}`＝`en` 查表）之后，全量 runner **1305 案 0 红**。等价原因的链条是完整的：装填腿只装选定那一档（`§4.1` 第 2 条）⇒ 表里根本没有另一档的条目 ⇒ `en` 那一级的 miss 必然回落到表的缺省档，而缺省档随参数＝用户选的那一档 ⇒ 有无 provider 答回同一串。也就是说 **F19 那处事故的前提是「两档齐装」，第 2 条的改形把事故的前提一并消掉了**，本稿 §5 第 7 条「现场与驱动台都装 provider」那条不变量自此只有 §6 走查与编译期可观测，不能称变异自证；判据 ① 的绘制侧半边（F19 那半条）因此**没有自动化证人**，G28 那一案不是它的证人（该预期自本条作废，见 §8 m7 行）。同一条改形还让 **m3 退化为等价变异**：两档 key 集逐字相等之后，`settings_string_keys(En)` 返回中文档那份与返回英文档那份是同一份数据，注入后 **0 红**；它的自证角色由 m2（删一枚 key ⇒ 等式案 1 案 2 条）与 p1（加一枚另一档没有的 key ⇒ 2 案 2 条）接任，两者自段 4 起都有牙。
8. **判据 ③ 的范围在批 4 落地期收窄为 5 枚（本小节前言那句「§2 的七条判据一律不动」的第一次例外，AGENTS.md §4.2 第 11 条）**：判据 ③ 原句是「那 13 条 key 在数值 1 与 2 下渲染出两个不同串」。逐枚核过调用点参数语义后的事实是：13 枚里只有 **5 枚**在英文侧是「数词紧邻可数名词」的形态（`connections.notice.imported`／`keys.header.count`／`workspace.broadcast.title`／`session.reconnect.exhausted`／`terminal.notice.unrepresentable.line`），其余 **8 枚**写不出 `one=`／`other=` 的差异——序数与分母、单位、索引标签、纯数字、以及「名词在前、数在后」的报告式语序（逐枚理由见 §8 批 4 读数那笔）。让那 8 枚满足判据原句要先改英文措辞（如把 `{0} running` 改成 `{0} tunnel running`），那是 D7 明令排除的润色，不是复数形态。故判据 ③ 自本条起写作：**5 枚成块词条在英文档 1／2 两值剥掉计数之后仍须不等，13 枚计数词条在中文档剥掉计数之后须逐字相等**。证人 `the_count_phrases_decline_in_english_but_not_in_chinese` 的形态与红案数随批 4 记在 §8。附带登记的证人形态教训：**「1／2 两串不等」这一句本身对 m4 空转**——计数就在串里，块退化到只剩 `other=` 时 "1 keys" 与 "2 keys" 照样不等，实测 m4_all 的那 2 条红全部来自金样而非该句；有牙的形态是「剥掉计数之后仍不等」（见 §8 m4 行）。

---

## 5 数据流与不变量

1. **一个进程一种语言**：装填发生在建窗之前（`src/main.cpp` 注释原话「词条必须在任何控件绘制之前装填：查表失败回退空文本」），启动档位一经读定，整进程内 `settings_locale()` 恒等。判据 ① 与 D2 合起来把这条锁成不变量。
2. **两档同 key 集**：`kStringsZh` 与 `kStringsEn` 的 key 集合相等是**判据**而非约定——由 `utest_i18n_tiers` 每次全跑钉住，读的是**源表**（`settings_string_keys(Language)`）而不是装填后的 `StringTable`，因为装填腿只装一档（D4 落地期改形）。任何新界面串必须先加两档才能上屏（7.25⑬ 那条纪律自此有证人）。常驻案随英文档本体一起落（§4.1 第 3 条）。
3. **缺省档＝用户选的那一档**：`set_default_locale` 从「中文上屏的技巧」变成语言选择本身；F3 那个反转的消除形态是**「另一档根本没装进表」**而不是「参数化后再覆盖」——表里只有选定那一档时，`Locale{}` 的第一级 miss 必然回落到用户选的档，于是连 F5 那几处不经 ctx 的解析口都跟着正确。
4. **命令 id 恒英文**：`CommandRegistry` 的 id 与快捷键覆盖表不随语言变（7.25⑬），本棒不碰 id；面板与命令面板显示的是词条，落盘的是 id。
5. **域校验只在装载侧**：`appearance.language` 的白名单 `{zh, en}` 由 `store.cpp` 把守（裁决 7.46② 分工），面板与装填侧都不夹紧、不自造回落。域外值＝rejected 留痕 + 回落缺省 `zh`。
6. **词条表是上屏文案的唯一真值源**（对界面而言）：F10 那四处进表后，`src/` 与 `include/` 内不再有引号内 CJK 的上屏串（除词条表本体与用例侧的断言字面量）。这条可以做成一条**门禁式**核对，本棒先以判据 ④ 与 §6 走查覆盖，是否升为常驻脚本留批 7 定（AGENTS.md §4.4 第 22 条要求任何常驻脚本自带变异自证）。
7. **绘制侧文案的语言由注入的 `Locale` 决定，而不是由表的缺省档决定**（F19）：`au::LocaleProvider` 一旦装上，`Text`／`Button` 走注入值；未装则一律按 `Locale{}`＝`en` 查，**en 档存在即命中**。于是本条的不变量是「现场与驱动台都装 provider」，而表的缺省档只兜住那几处不经 ctx 的解析口（F5）。这条是批 2 与批 3 的次序约束：provider 先落，英文档后落，反过来的中间态就是中英混排。（**批 3 段 4 更正**：第 2 条的「只装选定那一档」把这条的观测面一并消掉了——摘掉 provider 实测 **0 红**，见 `§4.1` 第 7 条。于是本条剩下的事实是「装上它，且它不可观测」：provider 只在「两档齐装」的未来形态里才决定文案，本棒保留它是因为 §5 第 3 条那条回落链要靠表的缺省档，而不是靠 ctx。）

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

计划七条注入（表随批 2 补到 m7），每条都要指名它打到的**独立**断言；打出零红或一片红的，按等价变异处理并写明等价原因（AGENTS.md §4.4 第 22 条）。

| 号 | 注入 | 预期转红的证人 |
|:---|:---|:---|
| m1 | 装填时把缺省档写死回 `zh`（忽略参数）。**批 3 段 1 起重述**：只把 tag／缺省档写死而源表仍随参数是一种形态，**连源表一起**写死（`locale_for` 与 `source_tier` 两处）是另一种，两种都要测。**批 3 段 2 起重述落点**：等式案 `installing_registers_only_the_selected_tier` 对两种形态都是 0 红（读点与被测函数共享，见 `§4.1` 第 6 条），故本行的证人列作废，改由下两案分抓 | 全形态⇒`installed_tiers_answer_templates_pinned_in_the_test` ＋ `the_process_locale_moves_with_the_installed_language`；弱形态⇒后者单独抓 |
| m2 | `kStringsEn` 删掉一条 key | 判据 ② 的「两档 key 集相等」。段 1–3 期间该证人未立项 ⇒ 0 红；段 4 立了 `the_two_tiers_share_the_same_key_space` 之后本行自此有牙 |
| m3 | 让 `settings_string_keys("en")` 返回 zh 的 key 集 | 判据 ②（证件自身被反证，说明它不是空转）。**批 3 段 4 起退化为等价变异**：两档 key 集逐字相等之后返回哪一份都是同一份数据，注入后 0 红，自证角色由 m2／p1 接任（`§4.1` 第 7 条） |
| m4 | 计数短语里的 `plural` 块退化为 `other` 单形态（原计划 13 条，落地期按 `§4.1` 第 8 条收窄为成块的那 5 条）。**批 4 实测后重述证人**：预期的「1／2 两串不等」那一句**不是本行的落点**（数在串里，退化后 "1 keys"≠"2 keys"，实测 m4_all 的 2 条红全来自金样），有牙的是「剥掉计数之后仍不等」那一句 | `the_count_phrases_decline_in_english_but_not_in_chinese`：摘一枚⇒1 案 2 条，摘五枚⇒1 案 7 条，改错分支名词⇒1 案 2 条（逐条见下面的批 4 读数）；中文侧的对应注入 m4_zh **0 红**，按等价变异结项 |
| m5 | catalog 那一行的 `effect` 改成 `Immediate` | 判据 ⑤（角标与档位断言） |
| m6 | 在生效那五行的**前面**重新加一份 `{"settings.shortcut.column.title", "动作"}`（把 F8 的形态注回去；加在后面的重复会被 `std::map` 吸收，打不中任何断言——这正是 F8 那条注说的「条数类断言天然无感」） | 判据 ⑥（表头那五串逐字验） |
| m7 | 摘掉场景根外那枚 `LocaleProvider`（批 2 的接线撤回，其余不动） | 判据 ① 的绘制侧半边（F19 已实测：这条注入在 en 档有内容时**必然**打红 G28 那一案；批 3 之前 en 档为空，故 m7 只登记形态、红案数随批 3 收尾复跑给）。**批 3 段 4 复跑后本列作废**：摘掉驱动台那枚 provider 后全量 runner **0 红**，等价原因见 `§4.1` 第 7 条——只装选定那一档让 `Locale{}` 那一级必然 miss 并回落到用户那一档，有无 provider 答回同一串。判据 ① 的绘制侧半边自此**无自动化证人**，与判据 ⑦ 同档登记 |

**批 1 读数**（表本体收敛那笔，本棒实测）：m6 注入形态＝在生效那五行**前面**加回一份 `{"settings.shortcut.column.title", "动作"}`（即 F8 的原始形态），`ctest --preset linux -R itest_settings_panel` 转红、`--filter five_shortcut` 读数**恰 1 案红**且红的正是 `title` 那一行断言（`"??????"` vs `"?????????"`，即三字对四字）；还原后 `ctest --preset linux` **105/105**、`borealis_test_runner` **1296 案全绿**（开工基线 1295 + 本案）。同批可见变化一处：快捷键只读表列头从「动作」变成「动作名」（D10 选的正是这一份）。

**批 2 读数**（接缝与那一行那笔，本棒实测）。三条注入各自打到**独立**断言，逐条红案数如下。

- **m1**（装填腿忽略参数、写死中文档）：`ctest --preset linux -R utest_i18n_tiers` **恰 1 案红**，红的是 `installing_registers_only_the_selected_tier`，案内逐条报 **466** 条 `zh entry leaked into the en table`——即中文档全部 key 都出现在按 `en` 装填的表里。这一案就是判据 ① 装填腿的半边证人，也是 D4 那条「只装选定那一档」改形的直接守卫（`§4.1` 第 2 条）。（**批 3 段 1 起作废**：那两句「查不到」的断言已按 `§4.1` 第 5 条改成等式，m1 需要两处一起写死才打得到，新读数见下面的段 1 那笔。）
- **m3**（`settings_string_keys(en)` 返回中文档 key 集）：**2 案红**——`the_on_disk_name_is_the_locale_tag`（按落盘名 `en` 在英文表里逐条解析不中，因为那张表其实根本没装英文档）与 `installing_registers_only_the_selected_tier`（反方向那句 `en entry leaked into the zh table` 被同一份 key 集打中）。m3 打的是**证件自身**，故这两案红正好说明判据 ①② 的读点不是空转。（段 1 复跑仍 2 案红，但第二条换成 `every_english_entry_has_a_non_empty_chinese_twin`，逐条见下。）
- **m5**（catalog 那一行的 `effect`）：两种越界形态各打不同断言，故都记。改 `Immediate` ⇒ **2 案红**（`utest_settings_catalog` 的 `language_row_is_a_wired_next_launch_choice` 与 `itest_settings_panel` 的 `deferred_and_next_session_rows_carry_their_badges`——角标为空而期望「重启后生效」）；改 `NextSession` ⇒ **3 案红**，除上面两案外还多出 `itest_settings_panel` 的 `grade_columns_match_the_two_physical_boundaries`。第三案不在判据 ⑤ 的预期证人列里，是**额外证人**：那一案手抄了「Wired ∧ NextSession」的 20 键全集，新行一旦挂进 `NextSession` 就让实际集合涨到 21，于是「新档行加错生效档」结构上抓得住（§4.1 第 1 条末尾那句「面板角标分支不能与它自己的生效档分家」由此有证）。
- **m7**（摘掉场景根外那枚 `LocaleProvider`）：**本批仍打不红，且等价原因已实测定位**——摘掉后绘制侧按 `Locale{}`（＝`en`）查表，而 `lookup` 的第一级 miss 回落到表的缺省档 tag，那一 tag 随装填参数＝`zh`，于是文案照旧是中文。这条与 `§4.1` 第 3 条同源：判据 ②③ 与 m7 的常驻证人一律排在英文档本体之后（批 3）。批 2 只把**接线**做完：`src/main.cpp` 与 `itest_settings_panel` 的 `Harness` 各装了一枚 `au::LocaleProvider(settings_locale(), …)`，驱动台那半边从此与生产同形态，红案数随批 3 收尾复跑给。

还原后 `ctest --preset linux` **106/106**、`ctest --preset linux -E etest_` **103/103**、`borealis_test_runner` **1302 案全绿**（批 1 基线 1296 ＋ 新套件 `utest_i18n_tiers` 5 案 ＋ 新 catalog 案 1）。中文词条 **464 → 466**（`appearance.language` 与 `settings.badge.next_launch`）；`settings_catalog` 键数 **69 → 70**、`Absent` 计数闸 **19 → 19** 不动（新行是 `Wired`）。

两处连带更正如实登记。① 外观页多一行把设置浮层的可用高度吃掉一档，`itest_settings_panel` 的字体候选案先红在「no font candidate is dispatch-reachable」上。改的是**用例侧的挑选启发**而不是实现：`reveal_font_trigger` 增加 `room_below_dp` 形参，只由真要探候选的那一例传 `120.0F`，其余三处保持缺省 `0.0F`——第一版曾在函数内硬编码这 120 dp，结果把锚点案的 A4-b 那条（通知条必须画在触发器下方）打红，形参化之后 `ctest -R itest_settings_panel` 64/64 绿。② 生产侧（`src/main.cpp`）那枚 `LocaleProvider` 注入在本通道**没有自动化证人**：runner 不装配 `main.cpp`（裁决 7.83），故 §5 第 7 条「现场与驱动台都装 provider」这一不变量只有编译＋链接＋§6 走查第 ①② 条兜底，与判据 ⑦ 同档登记；本通道亦无真机，§6 全部条项**未走查**，批 2 不称「可用」。

**批 3 段 1 读数**（英文文案本体第一段，本棒实测）。英文档从空表变成 **146 条**（`connections.*` 73 ＋ `keys.*` 73，段内序与中文表逐条对齐），未译余数 **320**（一次性探针读数，跑完即删）。本段先改了一次**证人形态**（`§4.1` 第 5 条）：批 2 那两句「另一档查不到」在本段本体一落地就必然翻转，故改成等式——新增私有接缝 `settings_string_text(Language, key)` 读**源表**，用例逐 key 比「装填后的表按另一档 tag 查回的模板＝选定那一档自己的模板（或两级全 miss）」。改形之后重跑注入，六条读数如下（每条测完即还原，全绿数取在最后）。

- **m1 的弱形态**（只把 `locale_for`／`set_default_locale` 写死 `zh`，源表仍随参数）：**0 红**。按等价变异登记，等价原因：装进表的条目本就是英文模板，只是挂在 `zh` 那一格，等式两边同串 ⇒ 不可观测。原表那句「忽略参数」的含糊处由此暴露，已在 m1 行重述。（**批 3 段 2 复跑作废**：英文档有内容之后这一形态在进程 locale 案转红，见下面的段 2 那笔。）
- **m1 的全形态**（locale 与源表**两处**都写死 `zh`，即重述后的口径）：**1 案红**（`installing_registers_only_the_selected_tier`），案内 **459 条**等式断言红。459＝466−7，少的那 7 条两档同字（`connections.type.ssh`／`type.telnet`／`serial.line_ending.lf`／`.cr`／`.crlf`／`keys.kind.ed25519`／`.rsa`），等式下两边同串，等价原因同上且**逐条指名**。另两案（`the_on_disk_name_...`、`the_process_locale_moves_...`）在该注入下仍绿：tag 与进程档确实跟着参数走，只有条目来源被写死。（**批 3 段 2 复跑更正**：同一注入下等式案实测 **0 红**——等式右边经由 `settings_string_text()` 读的就是被写死的同一个 `source_tier()`；红的是进程 locale 案与补出来的金样案。段 1 那 459 条不可复现，以段 2 那笔为准，见 `§4.1` 第 6 条。）
- **m1b（对偶，本段新增）**：locale 随参数、**源表取反**（选英文时装中文模板）⇒ **3 案红**（`installing_registers_only_the_selected_tier`、`the_on_disk_name_is_the_locale_tag`、`the_process_locale_moves_with_the_installed_language`），案内 **918** 条等式断言红（两条腿各 459）。这一形态批 2 那句「查不到」抓不到——另一档查得到东西并不等于「泄漏」，它是等式改形的直接产出。（**批 3 段 2 复跑**：同一注入是 **4 案红 / 13 条读数**，等式案那 918 条不在了——源表取反时等式两侧同串，红的是金样案的两向 10 条、`the_on_disk_name_...` 1 条、③ 那条腿 1 条与进程 locale 案 1 条。）
- **m3**（`settings_string_keys(en)` 返回中文档 key 集）：**2 案红**——`the_on_disk_name_is_the_locale_tag`（按落盘名 `en` 查不到中文档独有的 320 条）与新增的 `every_english_entry_has_a_non_empty_chinese_twin`（按那 466 条 key 逐条问英文档模板，320 条为空）。案数与批 2 同为 2 但**换了一条**：原先那条吃的是「en entry leaked into the zh table」，它随「查不到」形态一起退出（`§4.1` 第 5 条），故批 2 那两行读数按本段形态作废。
- **p1**（子集案的自证）：英文表多一枚中文档没有的 key（`{"keys.zbogus", "Bogus"}`）⇒ **恰 1 案红**（子集案），案内 1 条 `the en tier has a key the zh tier never heard of`。这条挡住的正是「拼错的 key 上屏是空白而不是错字」（F13）。
- **p2**（同案的第二个自证）：把 `keys.empty` 的英文模板改成空串 ⇒ **2 案红**（子集案报「空串占位」，等式案报「`lookup` 报命中而源表读数是空串」这一中间态）。附带一条形态教训：在表尾**追加** `{"keys.empty", ""}` 打不到任何断言——`std::map` 会静默丢弃后写的那一份（F8 同因），故探针改的是既有那一条而不是加一份新的。

还原后 `ctest --preset linux` **106/106**、`ctest --preset linux -E etest_` **103/103**、`borealis_test_runner` **1303 案全绿**（批 2 基线 1302 ＋ 新案 1；`utest_i18n_tiers` 由 5 案变 6 案）。判据 ② 的反向等式与「两档 key 集逐字相等」仍未立（`§4.1` 第 3／5 条），随段 4 落齐一起补；m2 因此继续只登记形态、不出红案数。

**批 3 段 2 读数**（面板侧 `settings.*` 本体那笔，本棒实测）。英文档由 146 条长成 **223 条**（本段 `settings.*` 77 条，段内序与中文表逐条一致），未译余数 **243**。本段补了一个证人（`§4.1` 第 6 条）：`installed_tiers_answer_templates_pinned_in_the_test` 把五对期望串**抄在测试里**（`settings.title`／`settings.badge.next_launch`／`settings.chain.filter`／`connections.action.import`／`keys.header.count`，两向各断一次），`utest_i18n_tiers` 由 6 案变 **7 案**。复跑七条注入，逐条红案数如下（每条测完即还原）。

- **m1 的弱形态**（只把 `locale_for` 写死 `zh`）：**1 案红 / 1 条**（`the_process_locale_moves_with_the_installed_language` 报 `settings_locale().tag()` 是 `zh`）。段 1 那笔「0 红／等价变异」按本次复跑作废：进程档那半边一直可观测，只是当时没打到它。
- **m1 的全形态**（locale 与源表两处都写死）：**2 案红 / 6 条**——同一案再加金样案的 5 条（逐 pin 报「装英文档却答回中文模板」）。等式案在两种形态下都是 **0 条读数红**（读点与被测函数共享），段 1 那笔 459／918 的等式读数由此判为不可复现，本段的金样案就是它的替代。
- **m1b（源表取反）**：**4 案红 / 13 条**（金样案 10 条两向、`the_on_disk_name_is_the_locale_tag` 1 条、`installing_...` 的 ③ 那条腿 1 条、进程 locale 案 1 条红在 `has_settings_string("appearance.language")`）。
- **m2**（`kStringsEn` 删一枚 key）：本段删的是 `settings.close`，仍 **0 红**，与段 1 同因：判据 ② 的反向等式排在段 4（`§4.1` 第 3 条），本案今天还咬不到它。
- **m3**（`settings_string_keys(en)` 返回中文档 key 集）：**2 案红 / 244 条**——`the_on_disk_name_...` 1 条 ＋ `every_english_entry_has_a_non_empty_chinese_twin` 243 条。这 243 与本段探针的未译余数**同数**，两条读数互证。
- **p1**（英文表多一枚中文档没有的 key）：**恰 1 案红 / 1 条**（子集案报 `never heard of`）。
- **p2**（改既有英文条目为空串，本段取 `settings.chain.no_match`）：**2 案红 / 2 条**（子集案报空串占位，等式案报「`lookup` 命中而源表读数空」这一中间态）。
- **m5／m6／m7**：本段不动 catalog 也不动接线，故不复跑；m7 的红案数仍按 `§4.1` 第 3 条排在四段落齐后。

同批可见事实两处：两档同字（对等式与金样两侧都无分辨力）由段 1 的 7 条增至 **10 条**，新增的正是 `settings.chain.up`／`down`／`remove` 那三枚字形；金样案挑的 5 枚因此刻意避开同字条目。还原后 `ctest --preset linux` **106/106**、`ctest --preset linux -E etest_` **103/103**、`borealis_test_runner` **1304 案全绿**（段 1 基线 1303 ＋ 本案）。判据 ② 的反向等式、③ 的复数形态与 m7 仍随段 4；`settings.*` 之外的三条旁注（`settings.note.session_log`／`clipboard_read`／`link_confirm`）在中文表里是多行拼接条目，本段按同一形态保持拼接，故一次性探针的严格单行正则会漏计它们（与批 2 那条 462／466 的教训同源）。

**批 3 段 3 读数**（终端与会话侧本体那笔，本棒实测）。英文档由 223 条长成 **316 条**（本段 93 条：`search.*` 12／`workspace.*` 8／`tab.*` 1／`session.*` 22／`terminal.*` 22（提示卡 2 ＋ 设置行 20）／`menu.*` 4／`links.*` 12／`command_palette.*` 4／`diagnostics.*` 8），未译余数 **150**＝段 4 那一笔。本段**不新增案**（证人上一段已补齐，本段只让它们的逐 key 读数变厚），复跑七条注入如下（每条测完即还原）。

- **m1 全形态**：**2 案红 / 6 条**（金样案 5 ＋ 进程 locale 案 1）；等式案仍 0 红，与段 2 同因（`§4.1` 第 6 条）。
- **m1 弱形态**：**1 案红 / 1 条**（进程 locale 案）。
- **m1b（源表取反）**：**4 案红 / 13 条**，与段 2 逐案同数（金样 10 ＋ `the_on_disk_name_...` 1 ＋ 等式案的 ③ 腿 1 ＋ 进程 locale 案 1）。
- **m2**（删英文档一枚 key，本段取 `search.prev`）：仍 **0 红**，等价原因照旧：反向等式排在段 4（`§4.1` 第 3 条）。
- **m3**：**2 案红 / 151 条**——`the_on_disk_name_...` 1 条 ＋ 子集案 150 条，后者与本段探针的未译余数**同数**（段 2 那笔是 243，本段随本体落地降到 150，这条读数自此就是「还差多少」的常驻形态）。
- **p1**（英文表加一枚中文档没有的 key，取 `search.probe.en_only_key`）：**恰 1 案红 / 1 条**。
- **p2**（改既有英文条目为空串，取 `tab.action.undo_close`）：**2 案红 / 2 条**（子集案报空串占位，等式案报中间态）。

同批两处可见事实：两档同字的条目由 10 增至 **12**（新增 `links.confirm.uri` 与 `search.count_cap`，两枚都是纯位置参数模板 `{0}` / `{0}+`，本来就没有可译的东西），金样案的 5 枚 pin 仍避开它们。英文侧的**措辞对齐**在本段有一处必须与真标签同源的硬约束：`terminal.notice.unrepresentable.action` 那句路径三段（`Settings > Terminal > …`）逐字取 `settings.page.terminal` 与 `terminal.unrepresentable` 的英文档读数，故该枚行名定的是 "Unrepresentable character policy"，路径尾串必须与它一字不差。还原后 `ctest --preset linux` **106/106**、`ctest --preset linux -E etest_` **103/103**、`borealis_test_runner` **1304 案全绿**（与段 2 同数，本段无新案）。判据 ② 的反向等式、③ 的复数形态与 m7 的红案数一律随段 4 落齐一起补。

**批 3 段 4 读数**（英文本体最后一段那笔，本棒实测）。英文档由 316 条长成 **466 条**，与中文表逐 key 相等（本段 150 条：`tunnel.*` 52／`sftp.*` 41／`appearance.*` 31／`connection.*` 20／`serial.set_baud.*` 5／`shortcuts.overrides` 1），未译余数 **0**，一次性探针的「还差多少」自此没有读数可给。本段立判据 ② 的**等式**常驻案 `the_two_tiers_share_the_same_key_space`（断两档 key 集排序后逐字相等 ＋ 逐 key 两档模板均非空），`utest_i18n_tiers` 由 7 案变 **8 案**；等式那条断言带一个案内的 `first_difference` 定位串（只报首个分叉位置与两侧那两枚 key），因为 466 条的表只回一句「不相等」没法定位。复跑七条注入，逐条红案数如下（每条测完即还原；红案数取全量 runner，含 `itest_*`）。

- **m1 全形态**：**2 案红 / 6 条**（金样案 5 ＋ 进程 locale 案 1），与段 2／段 3 同数；等式案仍 0 红（`§4.1` 第 6 条的读点共享未变）。
- **m1 弱形态**：**1 案红 / 1 条**（进程 locale 案），同段 2／段 3。
- **m1b（源表取反）**：**10 案红 / 71 条**——金样案 10 条、`itest_settings_panel` 4 案 55 条（角标案独占 47 条）、`itest_command_palette` 3 案 4 条、`itest_tunnel_panel` 2 案 2 条。段 2／段 3 那笔是 4 案 / 13 条，本段的厚度变化**不是新增证人而是同一批证人的读数变多**：选中文档时装进表的其实是英文档，于是所有断言「界面上那串中文」的无头用例第一次真的读到英文。这条就是判据 ② 那半边（英文档有本体）存在的意义，也说明 `utest_i18n_tiers` 之外的 itest 面板案是 m1b 的主落点。等式案在本注入下仍 0 红——它读源表，而源表取反后两档 key 集照样相等。
- **m2（`kStringsEn` 删一枚 key，本段仍取 `search.prev`）**：**1 案红 / 2 条**（等式案：一条红在集合不相等且定位串直接点名 `search.prev`，一条红在「该 key 的英文模板为空」）。段 1–3 那三笔的「仍 0 红，等价原因：反向等式排在段 4」到此收口，本行自此有牙。
- **m3（`settings_string_keys(En)` 返回中文档 key 集）**：**0 红**。按等价变异结项，等价原因见 `§4.1` 第 7 条（两档 key 集逐字相等 ⇒ 返回哪一份都是同一份数据）；段 1–3 那两案红的读数随本体落齐一起作废，自证角色由 m2／p1 接任。
- **p1（英文表加一枚中文档没有的 key，取 `search.probe.en_only_key`）**：**2 案红 / 2 条**（子集案 `never heard of` ＋ 等式案的定位串）。段 2／段 3 那笔是 1 案 / 1 条，多出的正是等式案。
- **p2（改既有英文条目为空串，取 `tab.action.undo_close`）**：**3 案红 / 3 条**（子集案报空串占位、等式案报「该 key 的英文模板为空」、装填腿的档位身份案报「`lookup` 命中而源表读数空」那一中间态）。段 2／段 3 那笔是 2 案 / 2 条。
- **m7（摘掉 `itest_settings_panel` 的 `Harness` 那枚 `LocaleProvider`，其余不动）**：**0 红**（全量 runner 1305 案全绿）。这是本段唯一**预期落空**的注入：§8 那行原写「en 档有内容时必然打红 G28 那一案」，实测与它相反，等价链条与影响面（判据 ① 的绘制侧半边自此无自动化证人、§5 第 7 条那条不变量只剩走查）逐字记在 `§4.1` 第 7 条。生产侧 `src/main.cpp` 那枚自批 2 起就没有宿主（裁决 7.83），本次同样无从复跑。

同批两处可见事实：两档文本逐字相同的条目由 12 增至 **13**（新增 `tunnel.kind.dynamic` = `SOCKS5 -D`，本来就是一串协议名与开关），金样案的 5 枚 pin 仍避开同字条目；`kStringsEn` 的头注释按落齐后的形态改写，并给表内那两类共九枚非 ASCII 字面量补了 `CJK-LITERAL: locale-output`（三枚字形条目 ＋ 六枚 `" · "` 分隔符，后者沿 `serial_format.cpp`／`settings_panel.cpp` 同类别已标的既定视觉形态）。还原后 `ctest --preset linux` **106/106**、`ctest --preset linux -E etest_` **103/103**、`borealis_test_runner` **1305 案全绿**（段 3 基线 1304 ＋ 等式案 1）。判据 ③ 的复数形态仍随批 4；§6 走查在本通道一律未做，批 3 不称「可用」。

**批 4 读数**（复数形态那笔，本棒实测）。F17 记「带 `{0}` 的模板 47 条，其中计数短语 13 条」，当时没逐枚列名；本批按**调用点参数语义**把它们点齐并分成两堆，成块的是英文侧「数词紧邻可数名词」的 **5 枚**，两档同批改写：

| key | 计数参数（实测调用点） | 英文侧形态 |
|:---|:---|:---|
| `connections.notice.imported` | `connection_sidebar.cpp` 的导入条数 | `Imported {0, plural, one={0} profile other={0} profiles}` |
| `keys.header.count` | `keys_panel.cpp` 的 `painted_rows_.size()` | `{0, plural, one={0} key other={0} keys}` |
| `workspace.broadcast.title` | `broadcast_banner.cpp` 的 `state_.session_count`（第二参数位） | `…, {1, plural, one={1} session other={1} sessions}` |
| `session.reconnect.exhausted` | `terminal_view.cpp` 的 `progress->attempt` | `Still failing after {0, plural, one={0} try other={0} tries} · stopped` |
| `terminal.notice.unrepresentable.line` | `terminal_view.cpp` 的 `unrepresentable_notice_->count`（第二参数位） | `…{1, plural, one={1} character other={1} characters} in this block`（超 120 列，拆相邻字面量两行） |

中文侧同 5 枚写成 `other`-only 块（`已导入 {0, plural, other={0} 条}` 等）。**其余 8 枚不成块，逐枚理由**：`settings.chain.full`「已达上限 {0} 项」与 `settings.chain.truncated`「链长超过上限 {0}」——英文是 `The limit of {0}`，数词后没有名词；`workspace.broadcast.pane`「窗格 {0}」＝`Pane {0}` 是索引标签，名词在数词前且恒单数；`session.reconnect.line`「第 {0} / {1} 次」＝`reconnect {0} of {1}` 是序数加分母；`session.reconnect.countdown`「{0} 秒后」＝`in {0} s` 是单位；`tunnel.row.retry_wait`「第 {0} 次重试 · {1}s 后」两枚参数各是序数与单位；`tunnel.header.running`「运行中 {0}」＝`{0} running` 数词后无名词；`search.count_cap`「{0}+」是纯数字触顶标记。让它们变格要先改英文措辞（如 `{0} tunnel running`），那是 D7 排除的润色而非复数形态，故按 `§4.1` 第 8 条收窄判据。`diagnostics.*` 那一类「名词在前、数在后」的报告式短语（`Unknown sequences {0}`、`overloads {2}`）本就不在 F17 的 13 枚里，同一理由。

证人 `the_count_phrases_decline_in_english_but_not_in_chinese` 走真解析链路（`StringTable::resolve`，`lookup` 只回模板），5 枚各渲染 n=1／n=2 两值，填充位取不含数字的 `"A"`。四条注入的读数：

- **m4_one**（摘 `keys.header.count` 的 `one=` 分支）：**1 案 / 2 条**——「剥掉计数之后仍不等」那一句（`"1 keys"` 剥完得 `"2 keys"`，与 n=2 同串）＋ 金样 `"1 key"`。n=2 那两条金样仍绿：`other=` 分支没动。
- **m4_all**（五枚全摘 `one=`）：**1 案 / 7 条**＝五枚各 1 条「剥完仍相等」＋ 2 条 n=1 金样。这是本行的正式读数。
- **m4_noun**（`one={0} key` 写成 `one={0} keys`，形状对、文本错）：**1 案 / 2 条**（同上两句）。金样那半因此不是装饰：它守的是分支文本，形态断言守的是分支存在。
- **m4_zh**（中文侧五枚块全部退回裸 `{0}`）：**0 红**，按等价变异结项。等价原因指名：`plural_category` 对 `language=="zh"` 恒返回 `Other`，块与非块渲染逐字同串，全量 1306 案（含 `itest_*` 里断中文上屏的那些）无一能分辨——**这 0 红同时是「中文侧改块行为不变」的正证**，中文侧那半边自此只有形态收益、没有证人（已回写 D7 行）。
- 同批复跑判据 ② 的两侧未受影响：两档 key 集仍 466 对 466 逐字相等，改写只动模板值不动 key；两档同字条目仍 **13** 枚（5 枚成块条目的两档文本本来就不同）。

还原后 `ctest --preset linux` **106/106**、`ctest --preset linux -E etest_` **103/103**、`borealis_test_runner` **1306 案全绿**（段 4 基线 1305 ＋ 判据 ③ 案 1）。§6 走查在本通道一律未做，批 4 不称「可用」。

**结项读数**：待补（批 7 回填实测三项数与逐条注入的红案数，含等价变异指名道因）。
