# UI_INTEG_NOTIFY.draft.md — `SPEC.FEAT.INTEG.03` 系统通知（OSC 9 / OSC 777 转 toast）设计稿

> **状态**：**三批已全部落地并结项（2026-10-22，裁决 7.128）**——§3 的 D1–D12 逐条按推荐档收口，人在开工令里写明「后续再遇到问题则你自行按照推荐去做」，代价写在行内。**§4 的批 1／批 2 已按此转实现**，两处稿面口径被开工后的实测推翻，已在原地更正（D4／D9 的 tag 与签名、§2 第 8 条的可证范围）并回写裁决 7.128。
> **本条无界面本体**（AGENTS.md §4.4 第 22 条那句「UI 编写前先出设计图评审」针对的是画面判据；本条的产物是 OS 通知，仓内不新增任何可画的东西），故无草图、无 SVG 源；可断言面全部落在无头通道（§8）。
> 需求原文（`codespec/SPECIFICATIONS.md` 的 `SPEC.FEAT.INTEG.03`）：**OSC 9 / OSC 777 通知转系统 toast（长命令结束提醒、远端程序消息）；依赖系统通知 API（框架缺口 G11）**。

---

## 0 地基实测（F1–F14 只读源与只读探针；F15／F16 是批 2 开工后撞到的两处，稿面就地更正，2026-10-22）

| 号 | 读数 | 它决定了什么 |
|:---|:---|:---|
| **F1** | **框架侧通知面早已齐备且全是公共 API**（Aurora 主仓 `include/aurora/app/notification.h`）：`Notification{title, body, tag, urgency, timeout_ms}` 是纯值类型，`NotificationCenter` 给 `notify()` / `set_on_notification_activated()` / `last_notification()` / `clear_last_notification()` / `pump_events()` / `emit_activated()` / `install_recording_backend()` / `remove_recording_backend()` 八枚入口；`Application::notify()` 与 `Application::set_on_notification_activated()` 只是**静态转发**，头注释自陈「二者只转调这里的静态方法，不复制任何平台逻辑」 | G11 无需再派、也无从再派。本条的全部工作量在**本仓两条腿**：term 层的消费与主线程的转发 |
| **F2** | **`notify` 是 main-thread only**：头注释的线程栏明写 `notify` / `set_on_notification_activated` / `emit_activated` / `pump_events` 「均在调用线程直行」，括号里补了一句「内部记录区有互斥保护，**仅供跨线程读取 `last_notification`**」 | 投递只能在主线程帧边界发生（AGENTS.md §4.5 第 25 条同一口径）。那把互斥是给观测面用的，**不构成放宽生产路径的理由** |
| **F3** | **激活回传的排空已由框架代劳**：Aurora `include/aurora/app/application.h` 的 `step_frame()` 里，`NotificationCenter::pump_events()` 排在 `os_hotkeys_.drain_pending()` 之后、`on_frame_()` **之前**，同段注释给的口径是「回调可能重建页面 / 触发重排，在消息泵内重入会把布局与绘制切到半途的状态」 | 本仓不必也不该自己排通知事件；若将来接激活回调，其回调时点在本仓每帧逻辑**之前**，这一点要写进接法说明 |
| **F4** | **失败一律机器可见**：`notify()` 的一切失败统一 `NotificationPostFailed`，`ErrorParams{{"detail", ...}}`，语义是「永久失败、不可重试」；头注释写「调用方可据此退化为自己 UI 内的提示条；不抛异常、不静默 no-op」。Linux 是三层运行期降级：① `dlopen("libnotify.so.4")` ② `dlopen("libdbus-1.so.3")` 手写 `org.freedesktop.Notifications.Notify` ③ `popen("notify-send")` 兜底（③ 不支持激活回调）；**三层全缺才回失败**；macOS 与其它平台本轮直接失败 | 「投不投得出去」是可判的，不需要本仓自己探测桌面环境；也意味着**在 macOS 上这条链必然走失败档**（登记为边界，不额外处理） |
| **F5** | **无头主观测面是单槽而不是队列**：`last_notification()` 在**所有**平台都维护（含投递失败的调用），配套 `clear_last_notification()` 回到「从未通知过」；头注释自陈这是「Headless 与自动化测试的主观测面」 | 用例必须**逐案先 clear**，否则上一案的残留会让本案假绿；本仓也不能把它当账本用（只有一格） |
| **F6** | **`install_recording_backend()` 是进程级开关**：装了之后 `notify()` 只更新记录、不触达任何系统通知服务，`remove_recording_backend()` 恢复平台实现，返回 `bool` 且注释说明「保留返回值以对齐 `Clipboard::install_test_backend` 的注入面形态」 | 集成用例装它 ⇒ 在装有真桌面的开发机上也能验证字段往返而不真弹（F12 那台机器就是这种机器）。**不装就是真投递**，属 §6 走查而非用例 |
| **F7** | **`tag` 有双重语义，其中一重就是节流阀**：注释写 `tag` 既是「去重/替换标识（同一 tag 的后续通知在支持去重的服务器上替换前一条）」，又是「激活回调回传的键」；空串由实现层替换为稳定占位键。`urgency` 三档映射 Win32 的「是否静音 / 是否遵守专注时段」与 XDG 的 `urgency=0/1/2`（Critical 不受专注时段抑制），`timeout_ms` 取 `0` 即交给平台默认 | 这一条直接决定 D3／D4：**tag 按会话取恒定值**，刷屏场景的抑制由 OS 侧的「替换」完成，而不是由本仓造队列再排队。反之若逐条换 tag，节流就退化成「本仓丢多少条」的算术 |
| **F8** | **本仓今天两条腿都是零**：`src/term/terminal.cpp` 的 `do_osc` 分表只有 `0/1/2`、`1`、`7`、`8`、`52`、`133` 六支，命令号 9 与 777 落进 `default` ⇒ `++osc_state_.unhandled_count`（架构 §5.5 的降级留痕），既不投递也不留内容；`src/` 全树 grep 无 `NotificationCenter` 触点 | 需求那句「转 toast」目前**没有任何实现面**。现状句要按「在册未开工」写实，不能沿用观察池里那句「落期不变」了事 |
| **F9** | **锁纪律已给出一条同形先例**：`include/borealis/term/osc.h` 的头注释与架构 §5.4 定死「状态机跑在会话读线程的网格锁内，**锁内只能留存而不能投递**；唯一的动作型 OSC（`52` 写剪贴板）不进快照，走取走语义」；会话侧的取走家族（`take_clipboard_write` / `take_clipboard_reads` / `take_bell_triggered` / `take_unrepresentable_notice` / `take_logging_notice`）就是这条纪律的产物；`session::ClipboardOutbox` 则是「公共头不含框架类型、框架头只出现在实现文件」的现成形态（架构 §2.3） | 通知与 `52` 写方向同档（主线程 OS IO），故**必须**做成取走型而不是快照，且**必须**另起一枚落地件。同时：osc.h 与架构 §5.4 那句「唯一的动作型 OSC」在本棒之后不再成立，两处要就地改口 |
| **F10** | **单条 OSC 串的载荷可达 1 MiB**：`src/vt/parser.cpp` 的 `kMaxStringLength = 1U << 20`，超限只置 `overflowed_` 并停止追加，`emit_string()` 首行见该标志即整串按 `Ignored` 交付（计入 `stats_.ignored`） | 「远端程序发的正文」是**不可信输入**，长度上限由解析器给到 1 MiB 码点。OS 通知正文按 MiB 计既无意义也拖累通知服务 ⇒ 消费侧必须再截一刀（D10） |
| **F11** | **逐会话排帧的挂点已在**：`src/main.cpp` 的 `set_on_frame` 里那条「排帧范围是**全部标签**每帧都排」的循环内已有 `for (const auto &[pane, tag_session] : workspace.sessions_by_pane)`，其中并排着 `outbox.drain(*tag_session)`、`take_logging_notice` + `AURORA_LOG_WARN`、`take_bell_triggered` 三件；同段注释还留着一句账——只排当前标签的后果是「别的标签的 `OSC 52` 永远留在队列里」 | 本条的接线是**多一行调用**而不是新建一条循环；且那句「永远留在队列里」的教训对通知同样成立：漏排就是某些标签的通知永不上屏 |
| **F12** | **本机（Linux 通道）的通知服务在册**——读数来自只读探针：会话总线上 `org.freedesktop.Notifications` 的 `NameHasOwner` 回 `(true,)`（`gdbus`），`libnotify.so.4` 与 `libdbus-1.so.3` 各在 `ldconfig` 里有一枚，`/usr/bin/notify-send` 在位，`XDG_CURRENT_DESKTOP=ubuntu:GNOME`、`WAYLAND_DISPLAY=wayland-0` | F1 的三层降级在本机**第一层就可达**，所以 Linux 侧的真投递没有环境障碍；但「真弹出来、肉眼看得见」仍是人工判据（F6），本棒不向用户桌面投递任何一条通知 |
| **F13** | **码点串 → UTF-8 的转换是现成件**：`term::encode_utf8(std::u32string_view, std::string &)`（`include/borealis/term/utf8.h`），且 `ClipboardOutbox::drain()` 已用同一条路径并注明「不可表示码点在解码环节就已换成替换字符，这里没有第二层失败面」；`Notification` 的字段是 `std::string` | 通知腿不引入新的编码问题：升格一次即可，失败面与 52 同档 |
| **F14** | **不落新键就不碰三处计数闸**：`tests/unit/utest_settings_catalog.cpp` 现钉 `keys.size() == 69U` 与 `ConsumerStatus::Absent` 行数 `== 19U`；一枚新键的代价是「`config/settings.h` + `store.cpp` + `form_transfer.cpp` + `settings_catalog.cpp` + `settings_i18n.cpp` + `main.cpp::make_interaction` 六处同步」外加两处计数改口（既有稿 F14 的原话：漏一处即「面板能改、视口不认」或「能存不能读」） | 本条按 D5／D11 推荐档落 ⇒ **不加开关、不加档位键**，69 与 19 两个数逐字不变。需求原文没有「可配」二字，这是特性不是漏项 |
| **F15** | **（批 2 实测，推翻 D4／D9 的签名）`Session` 公共面没有任何标识**：`ui::PaneId` 是 `std::uint64_t` 且「由装配层分配」，**每棵 pane 树内**首格挂 1、新号从 2 起只增不复用；`ui::TabId` 同型，由 `src/main.cpp` 的进程级 `next_tab_id` 只增分配（那行注释的理由与裁决 7.42⑩ 同一条）。于是「会话级恒定 id」在本仓**不存在**，唯一不撞且恒定的组合是 `(TabId, PaneId)`，而这一对只在装配层 `sessions_by_pane` 那一格循环里同时可得 | D4 的 tag 与 D9 的 `drain(Session &)` 都必须扩成 `drain(Session &, tab_id, pane_id)`。稿面原写法不是错在语义而是错在**假设了一件不存在的东西**；代价是落地件多收两枚整数、装配那一行的注释要写清号从何来（已写）。用例侧的对应证人＝同标签两格各拿自己的 tag |
| **F16** | **（批 2 实测，推翻 §2 第 8 条）判据 ⑦ 在本通道无自动化证人**：装了 `install_recording_backend()` 之后 `notify()` 只更新记录并**恒回成功**，无头回退档同样回成功（`notification.h` 的 Headless 段原文），本沙箱又无失败注入面 | §2 第 8 条那句「上述七条全部在无头通道可证」按实测**收窄为六条**；⑦ 的担保改为源码审查 + §6 走查第 ① 条在**没有**通知服务的机器上（或 macOS，F4）的自然命中。这不是遗漏而是**观测面的物理边界**，与裁决 7.83 那类「`main.cpp` 不进 runner」的欠账同档登记 |

---

## 1 本条要兑现的三件事，与需求没写的部分

需求那句话拆成本仓的三件事：**①`OSC 9` / `OSC 777` 在状态机里被认出来并消费成结构化产物**（不再是 `unhandled_count` 上的一次加一）；**②那条产物在主线程帧边界投给框架通知面**（`NotificationOutbox` 一枚落地件，框架头只出实现文件）；**③投递失败与不可信载荷两条边界都有可判的口径**（不重试、不阻断帧循环、正文截一刀）。

需求没写而开工前必须定的四件事，本稿的处理是：

- **「长命令结束提醒」这一档由谁发**（D11）：需求把它写在括号里当用途举例。本条**只落 9 / 777 的转发**，「命令结束」这件事由 shell 集成侧发一条 `OSC 777`（或由 INTEG.01 的 133 边沿另做一档自动通知），本仓不自造触发器——AGENTS.md §4.1 第 1 条「不推断需求外行为」。
- **要不要开关**（D5）：不要。默认行为就是需求要的行为。
- **正在前台看着这扇窗时要不要通知**（D7）：要通知（恒发），因为框架不给 OS 级激活态查询，而 xterm / iTerm2 的既有语义也是恒发。这条边界与 G42 同源，但**不碰 G42 的接货点**（见 §5 第 4 条）。
- **点击通知要不要把窗带到前台**（§7）：本期不接激活回调。`tag` 回传的键要能解析回「哪一扇窗的哪个标签」，而那条解析正是 G42 待回货的东西——本条不替它预造代偿。

---

## 2 交付判据（①～⑥ 有无头证人；⑦ 经 F16 实测无证人，⑧ 是本条的口径而非判据，§8 给注入位）

1. `ESC ] 9 ; <text> BEL` 之后的**第一次帧边界排帧**投出一条通知，正文＝`<text>`（超长按 D10 截断），标题按 D2 的兜底链取。
2. `ESC ] 777 ; notify ; <title> ; <body>` 投出一条通知且 `title` / `body` 各归其位；**`body` 内的 `;` 不被截**（body 取「首个 `;` 之后的整段」）。
3. 命令号 777 而首段不是 `notify`（大小写不敏感的 ASCII 比较）⇒ **整条丢弃**，并计入 `unhandled_count`，不投递、不留待发槽。
4. 同一会话在同一帧窗口内到两条通知 ⇒ 只投递**后到**的那一条，`notification_superseded` 计 1（D3／D4 的合并语义）。
5. 取走即清：一次排帧投一条，无待发时 `drain` 返回 0 且不触达通知面（与 52 写方向同档的「不空转」判据）。
6. 空正文（`9 ;` 之后什么都没有）⇒ **不发**，不产出空白 toast；该次仍计入 `notification_requests`，留痕在计数里而不是屏幕上。
7. 投递失败（`NotificationPostFailed`）⇒ 一条 `AURORA_LOG_WARN` 留 `detail` 原文，不重试、不抬异常、不阻断帧循环，返回值如实报 0 落地。
8. 判据 ①～⑥ **全部在无头通道可证**：`install_recording_backend()` 之后不触达任何系统通知服务，断言只走 `last_notification()` + `clear_last_notification()`（F5／F6）。**⑦ 除外**——记录后端与无头回退都把投递判成成功，本通道构造不出失败（F16，批 2 实测后就地收窄；原写法是「上述七条」）。

---

## 3 待人裁决（每条已给推荐①；按常存授权全按①收口）

| 号 | 议题 | ① 推荐 | 其余档 | 推荐档的理由与代价 |
|:---|:---|:---|:---|:---|
| **D1** | 9 / 777 的**载荷口径** | `9`＝首个 `;` 之后的**整段**作正文；`777`＝首段须为 `notify`，其后第一段为 `title`、**余下整段**为 `body` | ② `777` 严格按三段切（`body` 里的 `;` 被截）；③ 只认 `9`，`777` 继续记 unhandled | ② 会把「正文含分号」这条日常输入截断——正文是不可信输入而分号在其中极常见；③ 直接砍掉需求点名的第二家形态。① 复用现成的 `osc::split_first`（`tail` 的定义就是「分隔符之后的**全部**剩余」），两跳即得，代价是多一条「首段须 `notify`」的拒绝用例（判据 ③） |
| **D2** | `9` 没有标题字段，`Notification::title` 填什么 | 会话标题非空则用它（`OscState::title`，`OSC 0/2` 的最近一次值），空则回 ASCII `"Borealis"` | ② 恒空串（交给桌面按应用名兜底）；③ 恒 `"Borealis"` | ② 依赖各桌面对空标题的兜底策略，行为跨平台分叉而本仓无法无头断言；③ 会把「哪个会话在提醒」这一层信息压掉，而多会话是本仓的常态。① 的代价：标题是**该会话最近一次的标题**而不是「发通知那一刻的标题」，二者在同一帧内可能差一步——如实登记，不做二次取时点 |
| **D3** | 待发产物的形态 | **每会话一枚「最近一条」**（`std::optional<PendingNotification>`，后到覆盖先到并计一次 `superseded`），取走即空 | ② 每会话有界队列（上限 8，满则丢最旧）；③ 逐条入队不设上限 | ② 要再写一套队列与其丢弃判据，而 F7 说明 OS 侧的**同 tag 替换**已经在做同一件事——两条不同正文在同 tag 下的可见结果就是「只显示最新」，排队只是把同一个结果延后若干帧交付；③ 直接违反「会话字节流是不可信输入」这条既有纪律（`osc.h` 里超链接表 1024 上限的理由原文）。① 与 `52` 写方向同形（F9），代价一句话：**同一会话同一帧窗口内的两条不同提醒只发后者**，且这本来就是 tag 替换会给出的结果 |
| **D4** | `tag` 取什么 | 会话级稳定值：`borealis-session-<session id>`（ASCII） | ② 逐条递增 tag；③ 留空串交给实现层占位键 | ② 让「替换」失效，刷屏变成逐条弹（F7）；③ 虽然实现层给了占位键，但那枚键**不由本仓掌控**，激活回传（将来）与去重行为都无法断言。① 的代价：同一会话的多条提醒在支持去重的服务器上合并成一条不断更新的横幅——**这正是本条要的节流**（与 D3 同一条口径），且跨会话之间互不覆盖。**【批 2 实测补口径，F15】** `<session id>` 这一枚在本仓**不存在**：`Session` 公共面无任何标识，`PaneId` 只在本标签的树内只增不复用，故唯一恒定且不撞的是 `(TabId, PaneId)` 这一对，tag 实测形态为 `borealis-session-<tab>.<pane>`（分隔符取 `.`，免得与前缀里的 `-` 混成一串肉眼分不出段的号） |
| **D5** | `urgency` 与 `timeout_ms` | 一律 `Normal` + `timeout_ms = 0`（交给平台默认） | ② 按正文关键词升 `Critical`；③ 加两枚 schema 键 | ② 是对不可信输入做语义解释（关键词表由谁定？误判的代价是抢走专注时段，F4 里 `Critical` 明确「不受专注时段抑制」）；③ 吃下 F14 那整套六处同步而需求没要求「可配」。① 的代价：所有通知同一紧急度，远端程序无法表达「这条很重要」——而 9 / 777 的公开形态里本来就没有紧急度字段 |
| **D6** | 投递失败怎么降级 | `AURORA_LOG_WARN` 逐条留 `detail`，返回值如实报 0；**不在 UI 里另起提示** | ② 退化为本仓内嵌提示条（照 `unrepresentable_notice` 那张卡的形态）；③ 静默 | ③ 与 F4「不静默 no-op」的框架口径相反；② 要给一条**远端程序发起的**提醒再做一层界面状态机，且要新槽位、新帧判据、新词条——需求没要求，`SPEC.FEAT.TERM.09` 那张一次性卡的形态是为「本仓自己发不出去」设计的，语义不同。① 的代价：无桌面环境（含 macOS，F4）时用户得不到任何屏上反馈，登记为 §6 走查与 §7 延后项 |
| **D7** | 用户正看着这扇窗时要不要照发 | **恒发** | ② 仅当窗口失焦才发 | ② 的判据拿不到：框架既无「当前活动窗口」查询，也无 OS 级前台态读数（`SPEC.FEAT.WS.03` 的稿 §0 已实测登记，即 G42 的同一处缺口）；用「本仓记录的派发事实」冒充前台态就是把推断写进投递决策，而误判的代价是**该提醒的那条没提醒**。① 与 xterm / iTerm2 的既有语义一致（它们恒发），代价是前台时多一条横幅，归 §6 第 1 条肉眼判 |
| **D8** | 会话已退出／标签已关而待发槽里有货 | **随会话一起撤账**（对象析构即货消，不补投） | ② 摘账前抢投一次 | ② 要给「会话已没了」这件事再想一遍「提醒给谁看」，而提醒的对象正是一个已经不存在的会话；① 与 CONN.12 那条「析构撤账」的既有口径同档（裁决 7.118 的 D6 那条改判的反面——那里撤账是为了**必有回写**，这里无回写承诺）。代价：命令刚结束就关掉那个标签的用户看不到那条提醒，而他已经不需要它了 |
| **D9** | 转发腿挂在哪 | **`session::NotificationOutbox::drain(Session &) -> std::size_t`**（批 2 实测扩为 `drain(Session &, tab_id, pane_id)`，理由见 F15 与 D4 的补口径），调用点＝装配层 `set_on_frame` 里那条逐会话循环（F11，紧挨 `outbox.drain`） | ② 挂 `TerminalView::on_frame()`（视口侧） | ② 让「框架头 + 系统 IO」出现在 UI 的 TU 里，而 F9 那条纪律给这类落地件的形态是「独立一枚 outbox、公共头不含框架类型」；① 与 52 写方向共用同一次排帧、同一个「全部标签每帧都排」的范围（漏排的后果那注释已经写过）。代价：`src/main.cpp` 不进 CTest runner（裁决 7.83），那一行本身**无自动化证人**，故链路的证人落在 `itest_notifications` 直调 `drain`，装配那一行归源码审查 + §6 走查（§8 末条如实登记） |
| **D10** | 正文／标题的截断长度 | **正文 512 码点、标题 128 码点**，文件级常量不入 schema；超出即在截断处补 ASCII `"..."`（计入三枚字符） | ② 200 / 50；③ 不截，交给通知服务 | ③ 把 F10 那 1 MiB 的载荷原样交给 DBus 与桌面服务，长度不由本仓定就等于长度不设防；② 的 200 对一条 shell 提示而言过短（`git status` 的一句话摘要就超）。① 的依据：toast 的可见长度本来就远小于 512，取这一档只为「有界」而不是为「显示得下」；代价是一篇长日志的正文看不全——而**看不全的正文不该走 toast**，那是会话日志（`SPEC.FEAT.CONN.11`）的射程 |
| **D11** | 「长命令结束提醒」这句要不要本仓造触发器 | **不造**：本条只落 9 / 777 转发 | ② 顺带做「`OSC 133;D` 边沿 + 运行时长超阈值 ⇒ 自动发一条」 | ② 要新增阈值键（F14 六处同步）、要定义「运行多久算长」、且其判据源（133 的 D 边沿）属 `SPEC.FEAT.INTEG.01` 已经交付完的射程——在同一条需求里塞第二条触发器会让两条的验收面纠缠。① 的代价：纯裸 shell（无 shell 集成）不会有自动提醒，需用户配一行 prompt hook 发 `OSC 777`；按 AGENTS.md §4.2 第 11 条，这一档解读**回写成裁决**而不改需求原文 |
| **D12** | 计数与可观测 | 给 `term::OscState` 加两枚计数：`notification_requests`（识别为通知的条数，含被丢弃的空正文）与 `notification_superseded`（同会话被后到覆盖的条数）；**调试面板不加行** | ② 不加计数；③ 加进 `DebugPanel` 第四族 | ② 会让判据 ④ 与 ⑥ 没有可断言的落点（屏上什么都不留的两条只能靠计数自证）；③ 要动 `ui::DebugPanel` 的快照聚合体与装配层组装，而面板的三族计数器面向的是「解析/解码/不可表示」这类**内部降级**，通知是对外投递，语义不同档。① 的代价：两枚计数只被用例与诊断日志读，面板上看不见（登记为 §7 延后） |

---

## 4 落地形态与分批（每批一个分层提交）

**新增件（三件）**：

- `include/borealis/term/notification.h`（公共头，**不含框架类型**）：`enum class NotificationKind { TerminalMessage, NotifyExtension }`（9 与 777 的来源留痕，供用例与日志分辨）、`struct PendingNotification { std::u32string title; std::u32string body; NotificationKind kind; }`、两枚长度常量、`NotificationDraft`（**实测落名 `draft_notification(std::int32_t code, std::u32string_view args, std::u32string_view session_title)`**，标题兜底链要吃的会话标题作第三形参递进来，才保得住本件的纯函数性）一类的纯切分入口（D1 的拒绝档在这里出结果）。
- `src/term/notification.cpp`（实现 `parse_notification` + 截断 + 空正文丢弃；进 `src/CMakeLists.txt` 的 `borealis_core` 逐文件源列表——F：该列表**不是 GLOB**，漏一处即链接期断裂）。
- `include/borealis/session/notification_outbox.h` + `src/session/notification_outbox.cpp`：`class NotificationOutbox { static auto drain(Session &, std::uint64_t tab_id, std::uint64_t pane_id) -> std::size_t; }`（**实测签名多收两枚整数**，理由见 F15 与 D4），**全仓唯一触达 `aurora::NotificationCenter` 的 TU**（框架头只出实现文件，架构 §2.3 同一条）。

**改现有件**：`include/borealis/term/osc.h`（加两枚计数 + 头注释那句「唯一的动作型 OSC」就地改口为两条）、`src/term/terminal.cpp`（`do_osc` 加 `case 9` / `case 777` 两支 + 待发槽成员 + `take_notification()`）、`include/borealis/term/terminal.h`（取走入口与成员）、`include/borealis/session/session.h` + `src/session/session.cpp`（`take_notification()` 转发，与 `take_clipboard_write` 并排）、`src/main.cpp`（F11 那条循环里加一行 `drain`；失败日志那行实测落在落地件内而不是装配层，D6① 的「只 WARN」由 `notification_outbox.cpp` 自己兑现，装配层因此只多那一行）。

**用例**：`tests/unit/utest_term_notification.cpp`（新，D1 的切分与拒绝、截断、空正文、计数）；`tests/unit/utest_terminal_osc.cpp` 扩案（9 / 777 进分表、取走即清、覆盖计数）；`tests/integration/itest_notifications.cpp`（新，harness 照 `itest_clipboard_read` 的 `FakeConnection::deliver` + `Session` 形态，装 `install_recording_backend()`，断言 F5 那枚单槽的字段往返与逐会话排帧范围）。

**分批**：

1. **批 1＝term 消费腿**：新公共头 + `src/term/notification.cpp` + `do_osc` 两支 + 待发槽与取走入口 + 两枚计数 + `utest_term_notification` 与 `utest_terminal_osc` 扩案。判据 ①②③④⑤⑥ 的**语义半区**在此批可证（脱界面）。
2. **批 2＝落地件与装配**：`NotificationOutbox`（含 `encode_utf8` 升格与 tag 组装）+ CMake 源列表 + `src/main.cpp` 那一行 + `itest_notifications`（**实测 12 案**，稿面原写「八案」是估的）。判据 ①⑤⑦⑧ 的**链路半区**在此批可证——其中 ⑦ 经 F16 实测**不可证**，故此处可证的实为 ①⑤⑧ 三条。
3. **批 3＝文档回写与在册收口（本笔）**：`SPECIFICATIONS.md` 的 `INTEG.03` 现状句 + 新裁决 **7.128**（含 D11 那句用途的解读与 F15／F16 两条实测改口）、附录 A.1 与 A.2 的 G11 两处指针、`PLAN.md` 的 §3 INTEG.03 行与 §5 观察池该行出账及 §8 的 M5 一句、`ARCHITECTURE.md` §5.4／§5.5 与 §7 归属行、§15 缺口表那一行（G9／G11 那格）（含「唯一的动作型 OSC」改口为两条）、`CHANGELOG.md` v0.128。**§2.3 那三条不变量清单不动**：实测该节今天只有「平台类型不外泄 / 终端层不依赖 UI / UI 不读网格私有布局」三条，「公共头不含框架类型」从未在那里成文——§5.4 与 `clipboard_outbox` 沿用的都是这一处编号，本笔不改既有引用（免得一处规矩两个出处），只把落点登记在 **§2.2 的 `include/borealis/session/` 目录行**（两份 outbox 并列一句），投递腿的完整叙述在 §5.4 的落地形态段内。

**结项读数口径**：本通道（Linux GCC + POSIX + Headless）`ctest --preset linux -E etest_` 全绿 ＋ `borealis_test_runner` 案数增量 ＋ §8 变异逐条转红；**Win32 / MSVC 腿未编译未实测，一律标推断／未验证**（§4.2 第 11 条）。

---

## 5 数据流与不变量

1. **字节流 → 状态机 → 待发槽 → 帧边界 → 框架**：`Session` 读线程在网格锁内 `feed` ⇒ `do_osc` 认出 9 / 777 ⇒ 折成 `PendingNotification` 存进**一枚** optional；主线程每帧在装配层那条逐会话循环里 `NotificationOutbox::drain(session, tab_id, pane_id)` 取走（取走即空）⇒ `encode_utf8` 升格 ⇒ `au::NotificationCenter::notify`。**锁内不做任何投递与 IO**（F9）。
2. **单槽 ＋ 同 tag ＝ 两级节流**：本仓只留最近一条（D3），OS 侧同 tag 再合并一次（D4）。不变量：**任一会话在一帧窗口内至多投递一条**，且**同一会话的任一时刻至多一条在册通知**（由 tag 保证）。
3. **长度有界**：正文 512 / 标题 128 码点（D10）。不可信输入的两处上限——解析器的 1 MiB（F10）与本层的截断——**互不替代**：前者保内存，后者保对外投递的体量。
4. **不依赖窗口身份**：投递决策里不含「哪扇窗在前台」，产物里也不带窗号／标签号（D7）。因此本条与 **G42**（快捷键落点）与 **G44**（关闭否决）**没有接缝**：既不读 `ui::ActiveWindowTracker`，也不改 `src/main.cpp` 的 `target_window` 解析器，更不碰关闭链——回货时这两条的接线面与本条正交。将来若接激活回调（§7），才第一次需要「通知 → 哪一扇窗」的映射，届时按 G42 的回货形态做，本条不预造。
5. **失败不回滚、不重试**（D6、F4 的「永久失败不可重试」语义）：一次 `drain` 投一条，投失败即了结，待发槽不回填——回填就是重试，而重试语义已被框架否掉。
6. **撤账即了结**（D8）：`Session` 析构带走待发槽，与 `TabWorkspace` 那批既有析构次序同档，不需要新的摘账动作。

---

## 6 真机走查清单（未走完不得称「可用」，AGENTS.md §4.4 第 33 条）

1. 在真桌面上让远端（或本机 shell 里一行 `printf '\033]777;notify;Build done;42 tests passed\a'`）发一条 `777`：横幅出现、标题与正文各在其位、`urgency=Normal` 的驻留表现。
2. 连发两条不同正文（同会话）：肉眼确认是**同一条在更新**而不是叠两条（D4 的 tag 替换语义），并确认桌面端没有把两条都排队显示——本条的节流档就是按这一读数定的，读数不符即回改 D3／D4。
3. 中文正文（`printf '\033]9;构建完成\a'`）：UTF-8 往返在真实通知服务上不出现问号或截字（判据 ① 的对外可见面，非字面量判据）。
4. 超长正文（1 MiB 串里取 5 KiB 一段）：通知服务不卡、屏上看到的是截断后的那句（F10／D10 的真投递面）。
5. 前台看着这扇窗时它自己发一条：确认「恒发」这一档的手感可接受（D7 的代价肉眼判）。
6. 关标签／退出程序瞬间有待发通知：确认没有崩溃、也没有「弹一条指向已经不存在的会话」的错觉（D8）。
7. Win32 托盘气球腿（F1 注释里的 `Shell_NotifyIconW` + `NIF_INFO`）：本机无 MSVC 通道，**未编译未实测**，该档整条待 Windows 机器。

---

## 7 本期不做（登记在册，不是漏项）

点击通知把对应窗口带到前台（需「通知 tag → 窗／标签」映射，属 G42 射程，见 §5 第 4 条）；投递失败时的内嵌提示条（D6②）；`DebugPanel` 里给通知计数加行（D12③）；`OSC 10`／iTerm2 的 `OSC 1337` 通知扩展、`OSC 777` 的 `popup` 等其它子命令；「长命令结束自动提醒」的本仓触发器（D11）；按窗聚合与逐窗静音。

---

## 8 判据与变异计划＋实测读数（结项三件齐：ctest 全绿 + runner 案数增量 + 变异证人逐条转红）

| 注入 | 做法 | 预期转红的证人 |
|:---|:---|:---|
| m1 | `do_osc` 里删 `case 9` 支（落回 `default`） | `utest_terminal_osc` 的 9 分支案 + `itest_notifications` 的判据 ① 案 |
| m2 | `777` 的首段**不再校验** `notify`（任意首段都当通知） | `utest_term_notification` 的拒绝档案（判据 ③） |
| m3 | 截断改成一刀不截（常量放大到 `size_t` 上限） | `utest_term_notification` 的截断案（D10 的判据） |
| m4 | 覆盖时不 `++notification_superseded` | 判据 ④ 的计数案 |
| m5 | `take_notification()` 取走不清槽 | 「取走即清」案（判据 ⑤）——与 52 那棒同一条变异族 |
| m6 | `NotificationOutbox::drain` 不做 `encode_utf8` 而直接把码点串按字节塞进 `std::string` | `itest_notifications` 的中文/多字节往返案（走查第 3 条的无头替身） |
| m7 | 空正文不再丢弃而照发 | 判据 ⑥ 的「不发空白 toast」案 |
| m8 | 装配层那一行 `drain` 删掉 | **无自动化证人**（`src/main.cpp` 不进 runner，裁决 7.83 的老边界），如实登记不冒充读数；担保＝源码审查 + §6 走查第 1 条 |
| m9 | （批 2 追加）`notification_tag` 只取 `tab_id`，丢掉格号 | 首案的 tag 断言 + 同标签两格那案 |
| m10 | （批 2 追加）无待发时不提前返回，仍组一条空通知投出去 | 「空转不触达通知面」案、空正文案、非 `notify` 拒绝案、同标签两格案 |

**实测读数（逐条注入后跑门，注入已全部撤除并复跑绿：CTest 101/101、runner 1287/1287）**：

- **批 1**：m1 → `utest_terminal_osc` 6 红；m2 → 2 红；m3 → 4 红；m4 → 1 红；m5 → 2 红；m5-b（RIS 不撤待发槽，本批新加断言的专属证人）→ 1 红；m7 → 2 红。
- **批 2 复跑 m1**（链路用例落地之后）：`utest_terminal_osc` 6 红 **+** `itest_notifications` 8 红＝**14** 枚证人；同批 `utest_term_notification` 16 案**全绿**，这不是空转而是分层的事实——纯件不经 `do_osc` 分表，删一支分表当然打不到它。
- **m6** → 恰好 `the_body_reaches_the_channel_as_utf8_bytes` 一案红。**等价变异（指名原因）**：其余 11 案在注入下仍绿，因为它们的素材全是 ASCII，而 ASCII 档下「码点值」与「字节值」同值，两种写法产出同一串字节；只有非 ASCII 素材能把这条链分开，故本案必须带一枚四字节码点（`U+1F600`，写成十六进制转义以守 §4.3 第 14 条）。
- **m9** → 2 红（首案的 tag 断言 + `two_sessions_on_the_same_tab_get_different_tags`）。
- **m10** → 4 红。**等价变异（指名原因）**：`a_pending_notification_dies_with_the_session` 在注入下仍绿，因为该案在 `retire_session()` 之后不再排帧，注入改的那条「无待发也照投」路径物理上走不到——它的证人不是这条注入，而是「析构后没有任何东西再调 `drain`」这一结构事实。
- **m8** → 无自动化证人，如实登记（见上表与本条，不冒充读数）。

等价变异若出现须指名等价原因（项目口径）：**批 1 预计「没有」，实测出现两处（m6、m10 各一条），已在上面指名**。除此之外八条注入各打得到独立断言。
