# CHANGELOG.md — Borealis 需求规格书变更历史

> 本文件记录 [`SPECIFICATIONS.md`](SPECIFICATIONS.md) 的版本演进，并保存**旧纯数字需求编号 → 新标识**的完整映射。
> 规格书正文只述需求，不含优先级与交付分期（那部分属 [`PLAN.md`](PLAN.md)）。本文件是历史记录：早期版本条目沿用其**当时**的优先级与里程碑口径原文，不做回填改写，以便对照每一次调整的取舍依据。
> 现行需求标识规范见 [`SPECIFICATIONS.md`](SPECIFICATIONS.md) §1.4。

## v0.91（2026-10-08）**`SPEC.FEAT.WS.04` 的断线 / 退出角标（本地腿）落地：`Tab::exited` 是每帧推入的投影而没有清除入口，同批修掉两处让已落角标从未上屏的潜在缺陷**（`include/borealis/ui/tab_strip.h` + `src/ui/tab_strip.cpp` + `src/ui/tab_bar.{h,cpp}` + `src/main.cpp` + `tests/unit/utest_tab_strip.cpp` + `tests/integration/itest_tab_bar.cpp`（新），判据入册为裁决 **7.91**）

**动机**：`SPEC.FEAT.WS.04` 的三条视觉提示里，v0.82 落了活动绿点与 BEL 红点两枚（裁决 7.82③），断线 / 退出角标一直是未落档，而它才是多标签工作区里唯一「用户不看会话内容也知道那一格已经死了」的信号。本条落的是**本地腿**：判据 `Session::alive()` 已在册，装配层的 `has_live_session` lambda 也已在关闭前确认那一棒落完（裁决 7.83），故本条不新起判据源，只把它折进标签栏。**这一腿是本仓自己的活而不是框架侧一腿**：判定、折叠、上屏全在本仓，Aurora 只提供 `Painter::draw_rect` 这一落笔原语。

**决定形态的口径**：⑴ **`exited` 是投影而不是判据，故刻意没有「清除」那一半**——装配层每帧 `tab_strip.set_exited(tab_id, !has_live_session(tab_id))`，会话重启后下一帧推进来的就是 `false`。这与同文件里 `mark_bell_triggered` / `take_bell_triggered` 那对**取走型**事件位是相反形态，区别在于「发生过一次 BEL」不可重放而「现在是什么状态」可重放；给状态位加一个 `clear_exited()` 就是第二个真值源。⑵ **折叠口径「该标签全部 pane 已退出 ⇒ 角标」正是裁决 7.47⑧ 那条关闭前确认判据（任一 pane 仍活着 ⇒ 要问）的否定式**，且两处共用同一个 lambda，因此「角标说已退出而关闭时弹确认」这类自相矛盾在结构上不可能出现——两个界面各判一次必然分叉，这一条与 7.42⑤ / 7.43⑤ 的「两处各判必分叉」同族。⑶ **徽标形态取右下角空心 6 dp 琥珀环且选中格也画**：绿点按裁决 7.82①「只在未选中格显形」，而环相反，因为用户在已退出的格子里打字时正该看见它已退出；空心描边与右上角实心铃铛在**位置与形状两档都不撞**（`draw_rect` 描边实测周长 20 格、内部 16 格不动，实心档 36 格），两枚同格点亮时按 y 分成两簇且不相交。色值是本棒拍的实现细节，不进判据。⑷ **`TabStrip::set_changed_hook` 是角标上屏的唯一通路**：框架的布局缓存在「约束未变 ∧ 本控件未被标脏」时直接复用上次尺寸并跳过 `on_layout`，而 `TabBarWidget` 的输入全在 `on_layout` 里经 hooks 现取，于是窗口尺寸不变时角标永远到不了屏幕；钩子在每次**真转移**时 `mark_needs_layout()` + `mark_needs_paint()`，而 `set_exited` 对同值直接返回，因为会话活着时每帧都推同一个 `false`，不挡就是每帧一次整栏重排。**取 push 而不取每帧 poll `revision()`** 的理由：用户驱动的改名 / 重排 / 切换要立刻刷新，帧回调里逐格量文本宽度违反 AGENTS.md §4.5 第 25 条，且后台标签的输出洪流会把灯点亮一次变成每秒六十次整栏重排。⑸ **两处潜在缺陷随接线现形并同批修掉**（登记为独立条目而非静默补丁）：⑴ 此前没有任何东西把 `TabBarWidget` 标脏，于是**已经落完的铃铛与活动两枚角标从未真正上过屏**（它们的判据一直只到模型层）；⑵ `TabStrip::take_bell_triggered(TabId)` 原先**全仓无调用点**，⑴ 一修铃铛就永远灭不掉，故 `.select` 补齐「切到该格即把两枚事件角标都取走」。

**代价**：① `Tab::exited` 是投影缓存，装配层漏推一帧就是陈旧位（该风险由「帧泵覆盖全部标签」这一既有形态承担，裁决 7.82②(c)）；② 钩子是一次性回调而非订阅表，将来出现第二个观察者就得改形态——当下没有第二个消费者，按 §4.1 第 3 条不预建抽象；③ **逐标签连接类型图标与可听 beep 仍未落**：前者要等 SSH / 串口族有种类可画（本仓当前只有本地终端一种连接），后者在 M5 观察池且须显式开 `AURORA_ENABLE_AUDIO`；④ 角标的**色值**在本仓判据里不进任何断言，「认不认得出那是环 / 铃铛」只能真机目视，本条不宣称「可用」。

**验收**：`utest_tab_strip` 28 → **30 例（+2）**（退出位是无清除入口的投影、钩子只在真转移时响的十步台账）；`itest_tab_bar` **新套件八例**（`AURORA_BACKEND_HEADLESS` 帧缓冲上的**帧间像素差分**，断的不是「角标算得对不对」而是**它有没有落到屏幕上**：窗口尺寸未变时角标仍上屏且钩子恰响 1 次、环是空心 6×6 且落下半栏、选中格同样画、与铃铛两簇纵向不相交、真点一次标签把铃铛取走且整帧逐位复现点击前那一帧、推 `false` 即撤标且不留残影、30 次冗余重推不唤醒标签栏、两格各自点亮且互不牵连）。像素一律比 RGBA 四通道含 alpha（与 `itest_render_viewport` 同口径：无头帧底色是全透明黑，只比 RGB 会把「画了个纯黑」与「什么都没画」混为一谈）。非 e2e 通道 `ctest -E etest_` **47 → 48 项全绿**（6.97 s）。**八条变异注入各有指名红行**：M1（驱动台不装变更钩子）→ 8 例全红；M2（`set_exited` 摘掉 `notify_changed()`）→ 6 红，而相撞那例与真点标签那例**仍绿**，机制是同一次手势里另一枚角标自己的通知把这一帧捎上了屏——**一枚 notify 能掩盖另一枚的缺失**，故 M2 的承重证人是余下六例；M3（去同值早返回）→ 1 红；M4（环根本不画）→ 6 红；M5（`draw_rect` 改 `fill_rect`）→ 5 红；M6（用例侧 `.select` 不取走铃铛）→ 1 红；M7（环只画未选中格）→ 6 红；M8（环移到铃铛那一带）→ 2 红。每次变异后全树 grep 变异标记为空并复跑 8/8 绿。**本条未复跑吞吐门禁**并给理由（角标的绘制仍在标签栏那一次 `on_paint` 里，每帧成本恰为一次位测试，基准四场景不含标签栏压力测项）。

**落点**：`codespec/SPECIFICATIONS.md` §4.2 的 `SPEC.FEAT.WS.04` 落地现状句与 §7 的裁决 **7.91**、`codespec/PLAN.md` 的 §8 M2 行与接缝账、`AGENTS.md` §2 与 §6、代码与测试如上。

## v0.90（2026-10-08）**`SPEC.NF.PLAT.01` 出口判据的第三腿＝Linux 缩放上报核验完成：G12 的残留单腿由三条收窄为 X11 一条，该条缺陷口径改判为「不上报 ∧ 不重取」，而据该仓自陈分类判为其射程内的已知限制故**本条不派新任务书、只收窄该后端的判据形态**（本条零代码改动；读源件为 Aurora 主仓 `include/aurora/window/surface.h`、`src/aurora/app/window_host.cpp`、`src/aurora/window/{glfw,wayland,x11}_surface.cpp`、`include/aurora/window/x11_surface.h` 与该仓 `codespec/specification/08-tooling.md` §8.2；判据入册为裁决 **7.90**）

**动机**：`PLAN.md` §2 的 M2 出口判据把 Linux 那一句写成「同批功能与指标达标（**含缩放上报腿**与 PTY/默认 shell 探测）」，是三条腿而不是一条。v0.89 落完前两条，缩放上报那一腿只以「本仓换算路径已单源故不构成新阻塞」带过（该条代价 ⑤），**没有核验**。本棒补这次核验：不跑图形会话也能核验的部分是实现面的调用点是否存在，而这一项正是需求那句「150% / 175% / 200% 及跨屏拖动后无错位」能不能成立的前提。

**决定形态的口径**：⑴ **以实现面调用点为判据，而不是以框架文档措辞为判据**（AGENTS.md §4.2 第 12 条）。读到的框架侧两处自陈互不相容——`codespec/specification/08-tooling.md` 已把 GLFW 与 Wayland 记为已具备上报，而同仓 `07-environment-modifier.md` 仍写三条皆缺；本条以实现面为准，且**该仓两处不同步不属本仓处置面**（不代它改文档、也不据其一否认另一）。⑵ **公共 API 那条腿未变**：`Surface::set_scale_change_handler` 注册、后端经 `notify_scale_change` 上报、Headless 侧 `emit_scale_change` 供回放，且 `window_host.cpp` 里已有把它接到 `WindowHost::on_scale_changed()` 的消费点——2026-09-29 那次复核的 API 面结论至今成立，过期的只是「三后端未接线」那半句。⑶ **X11 的缺陷比登记时读到的更宽，故这是改判而非重复登记**：它的缩放值只有一处赋值（构造期 `detect_scale(Display*)` 解析 `Xft.dpi`、钳 `[0.5, 4.0]`），此后没有任何运行期重取路径（xsettings / `Xft.dpi` 变更、跨到另一块不同 DPI 的 X11 屏都不反映），公共头亦只覆写 `scale_factor()` 而无通知通道。于是「补一条上报调用」不足以让本条判据通过——**取数与通知两条都得补**，只做后者界面上仍是陈旧的 `1.0`。**同一处 `08-tooling.md` §8.2 另有本条据以处置的自陈**：X11 那一档写着「**不适用，非缺口**」——`detect_scale()` 读的是 X 资源管理器的 `Xft.dpi`（进程级全局、运行期不变），X11 核心没有 per-monitor DPI 概念，要做 per-monitor 须另接 XRandR（该仓判为**新特性而非补通知**），运行期改 `Xft.dpi` 不被感知则记为「已知限制」。⑷ **处置：接受该分类，不派新任务书**——裁决 7.13① 派的是「本仓需要而框架缺的原语」，此处框架已就同一事实给出范围裁决，本仓不替框架把它重新立项；据此把 RENDER.05 那句验收在 X11 后端落成可判形态（启动档可判；「运行期即时改档」需 xsettings 监听、「跨屏异缩放」需 XRandR，两档都在新特性射程内故移出该后端判据集合，改档后的可观测形态只有重建窗口／重启应用后取到新值）。本仓的边界同时划清：不自造 X11 侧监听（不订阅 xsettings、不轮询 `Xft.dpi`、不自己处理 `XRandR` / `XDG_OUTPUT`），因为那会把平台缩放知识复制进应用侧，违 §5 第 2 条「本仓不长期持有框架分叉」；也不以轮询比对 `scale_factor()` 来模拟通知，那是把框架的取数缺陷伪装成已解决。若将来人裁决要求 X11 运行期感知，那是一张**新特性任务书**而不是 G12 的残留腿。

**代价**：① **同一句验收在三个 Linux 后端上的成色不再相同**：Wayland 与 GLFW 各按其调用点判，X11 只判启动档——这是主动划出的射程而不是未结项，代价是「运行期改档即时生效」这一档在本平台的覆盖变窄；② GLFW 与 Wayland 两条的「调用点存在」是**读源结论而非本仓读数**（本环境无桌面会话，跑不出一次真实缩放变更），故本条只宣称调用点在两后端存在，**不宣称两腿已验证**，二者同归既有的 Linux 侧真机走查欠账；③ 本仓消费侧零改动是结论也是代价——它意味着本轮没有新增任何证人，判据的成色全部来自读源，按裁决 7.49⑥ 的口径如实登记而不伪造绿灯。

**验收**：**本条零代码改动故无测试、无变异自证**（同上口径），非 e2e 通道 `ctest -E etest_` 维持在册 **47 项全绿**且未复跑，吞吐门禁同样未复跑（无生产改动）。入册的读源件是三条：`src/aurora/window/glfw_surface.cpp` 的 content-scale 回调（去重后上报）、`src/aurora/window/wayland_surface.cpp` 的 buffer-scale 决策点（配该仓 `select_wayland_buffer_scale()` 与 `utest_wayland_output_scale`）、`src/aurora/window/x11_surface.cpp` 的 `d.scale = detect_scale(d.dpy)` 单处赋值（全文件唯一写点）＋ `include/aurora/window/x11_surface.h` 只覆写 `scale_factor()`、第四条是该仓 `codespec/specification/08-tooling.md` §8.2 的「缩放变化上报的跨后端现状」段（本条据其 X11 那一档作处置，据其 GLFW / Wayland 两档核对调用点形态）。

**落点**：`codespec/SPECIFICATIONS.md` §4.2 的 `SPEC.FEAT.RENDER.05` 现状段、附录 A.1 的「DPI 缩放变更上报」行、附录 A.2 的 G12 行、附录 A.3 的 G12 承诺段，与 §7 的 **7.90**（表头日期段同步）；`codespec/PLAN.md` 的 `SPEC.FEAT.RENDER.05` 与 `SPEC.NF.PLAT.01` 两行、观察池的 G12 残留单腿行、§8 的 M2 现状行（交付段与未交付段各一处）；`AGENTS.md` §6 的 Linux 等价那条；裁决 7.90 的结论是该腿据此关闭为「框架射程内的已知限制」，故**无任务书产出**。

## v0.89（2026-10-08）**`SPEC.NF.PLAT.01` 的 Linux 等价核验完成：POSIX PTY 腿六条实测缺陷修复 ＋ 三条真机 e2e 改为两腿共用同一批判据**（`src/platform/posix/forkpty_connection.cpp` + `include/borealis/conn/local_terminal.h` + `include/borealis/config/settings.h` + `tests/unit/utest_posix_local_terminal.cpp` + `tests/e2e/{etest_local_terminal,etest_key_forwarding,etest_osc_clipboard}.cpp` + `cmake/BorealisTests.cmake` + `CMakePresets.json`；判据入册为裁决 **7.89**）

**动机**：v0.62 那条 POSIX 腿（`forkpty` ＋ `$SHELL` 探测）落地时只验到「能起一个会话并把输出喂上网格」，`SPEC.NF.PLAT.01` 要的是**全量需求等价**而需求那句把平台差异限定在「PTY／串口／传输层实现与默认 shell 探测」。本棒把三条真机 e2e 从 Windows 单腿改写成单源双腿，实测逼出六条缺陷，其中三条是「Linux 上看着能用、判据其实不成立」那一型：master 侧的 `cfmakeraw` 因 Linux 上 master 与 slave **共用同一份 termios** 而把子进程的行规程一并清掉（Enter 不再提交，bash 自己做行编辑故把症状藏住）；算好的环境块从未交到子进程（`execvp` 吃继承环境，`TERM` / `COLORTERM` 与 profile 覆盖全失效）；启动目录不存在的失败被吞在子进程里（用户得到一把忽略配置的 shell 且无留痕，而 Windows 腿是整个启动失败）。

**决定形态的口径**：⑴ 等价性的**判据形态**是「同一份公共判据在两腿各自成立」，素材、句式、失败信息逐字共用，只在「喂哪条命令行」与「谁算那串 base64」两处按平台分叉。⑵ 两腿共用判据的核心算式是「该行以素材结尾 ∧ 不含命令动词」（`wait_for_output`）：cmd 把提示符与命令打在同行，dash 把提示符 `$ ` 留在输出那一行行首，故整行相等只在 Windows 腿成立。⑶ 命令行切分补 `tokenize_command_line`（单引号全字面、双引号内 `\` 只逃 `"` 与 `\`、引号外 `\` 逃下一字符、未闭合走到串尾、成对空引号仍算一个参数），使同一条配置命令行在两腿拆出同一个 argv。⑷ 环境交递取「fork 与 exec 之间 `::environ = envp.data()`」而不是 `setenv` 循环或 `execve`：前者表达不出「移除一条继承项」，后者丢掉 PATH 搜索。⑸ 启动目录经 fork 之前 `open(..., O_RDONLY|O_DIRECTORY|O_CLOEXEC)` ＋ 子进程 `fchdir` 取得与 `CreateProcessW` 的错误等价。⑹ `winsize` 钳位 `[1, 32767]` 与 Win32 腿的 `COORD` 钳位**同界**；读线程改 `poll` 50 ms 尾沿（关掉 fd 不唤醒阻塞中的 `poll`）并在 `state_mutex_` 内取 fd。⑺ 探测链 `$SHELL`（须为存在且可执行的绝对路径）→ `/bin/bash` → `/bin/sh`，兑现裁决 7.19④ 末句的「posix 侧须同口径」。

**代价**：① 「不含动词」那一句在 **POSIX 腿是等价注入**（实测去掉仍绿——dash 在没有已提交行时把下一个提示符追加在同一行行尾，`ends_with` 一条已把回显行排除），该句只在 Windows 腿承重；② `winsize` 钳位与 50 ms 尾沿在本通道**无证人**（前者需要一次超 32767 列的网格而下发侧结构上产生不出，后者属空闲 CPU 占用类量而本仓无该门禁）；③ `OSC 52` 落进**系统**剪贴板那一腿在本机**取不到读数**——Aurora 的 Linux 后端经 `xsel` / `xclip`，两者皆无 ⇒ 写入以 exit 32512 稳定失败；装包属改共享主机、不在本任务授权射程内，故不装包也不静默跳过，改为显式探测该桥并分两档断（可用则比回读明文，不可用则断「写入可复现地失败」），其余三腿在两档下照常断；④ **串口与 SSH / Telnet 传输在两腿都尚无实现**，故需求那句对后两者当下是空真，本条登记该事实而不写「Linux 串口已等价」；⑤ 余下一项 Linux 等价的框架面条目是附录 A.2 的 **G12 残留单腿**（X11 / Wayland / GLFW 未调用 DPI 缩放上报），本仓换算路径已单源故不构成新阻塞；⑥ Linux 侧真机走查本环境无桌面会话亦未做，故本条只结「等价判据在两腿都成立」，**不宣称 Linux 上「可用」**。

**验收**：`etest_local_terminal` **9 例**、`etest_key_forwarding` **2 例**、`etest_osc_clipboard` **1 例**在 Linux/GCC ＋ Ninja 上全绿；`utest_posix_local_terminal` 0 → **6 例**（探测链次序、伪 `$SHELL` 回落、未设／空串回落、bash 优先于 `sh`、探测产物无需加引号、PTY 环境注入默认值），两条变异注入各有指名红行（去 `$SHELL` 的 existence 判定 → 回落那一例；把 bash 档改成 `sh` → 次序那一例**单独**红）；命令行切分件不另配单测——它的证人是 POSIX 侧 e2e 本身，该腿把一次性命令整段用单引号包住（`/bin/sh -c '…'`，内含 `$`、`;`、`(`），不认单引号即九例全红。非 e2e 通道 `ctest -E etest_` **47 项全绿**，构建无告警。构建面两条配套：`cmake/BorealisTests.cmake` 按文件名把平台专属探测腿套件只编进自己那一侧（理由不是编译不过，而是注册式框架按文件 stem 匹配——`#ifdef` 掏空用例会让 runner 拿到「no test case matched」的红灯）；`CMakePresets.json` 补 `linux` / `linux-bench` 的 configure 与 build 预设及一条 `linux` test 预设。

**落点**：`src/platform/posix/forkpty_connection.cpp`（六处修复的现场）、`include/borealis/conn/local_terminal.h`（探测链两腿并列、POSIX 侧无需加引号的形态、工厂 `@return` 补 POSIX 腿）、`include/borealis/config/settings.h`（`local_shell` 的探测链注释补 POSIX 档）、三条 `tests/e2e/etest_*` 的单源双腿改写、`tests/unit/utest_posix_local_terminal.cpp`、`cmake/BorealisTests.cmake`、`CMakePresets.json`；判据文 `codespec/SPECIFICATIONS.md` §7 的 **7.89**、`codespec/PLAN.md` 的 `SPEC.NF.PLAT.01` 行与 M2 现状行、`AGENTS.md` §2 的 `src/platform/posix/` 行与 §6 的「Windows 腿」措辞与 e2e 那句。

## v0.88（2026-10-08）**`SPEC.FEAT.PREF.04` 的重绑对话框接进派发链：「编辑」与「确定」两只死按钮同批修掉，`ReadOnlyTable` 的「只读」改判为入口形态**（`src/ui/settings_panel.{h,cpp}` + `src/ui/settings_form.cpp` + `include/borealis/ui/settings_form.h` + `include/borealis/ui/settings_catalog.h` + `src/ui/settings_i18n.cpp` + `tests/integration/itest_settings_panel.cpp` + `tests/unit/utest_settings_form.cpp`，判据入册为裁决 **7.88**）

**动机**：v0.82 交付快捷键重绑时只写了模型侧与浮层构建，`open_binding_dialog()` 从头到尾**没有把那棵树挂进场景根**，故快捷键表每行的「编辑」按钮点了什么都没发生——既不渲染也不可命中（`Dialog::show()` 只置 `open_` 并标脏），违「不给一个点了没反应的按钮」那条口径（裁决 7.38⑥ F-b），且该区段**没有任何集成用例覆盖**。修好挂载之后又抓到第二条：`SettingsForm::store()` 对 `ControlKind::ReadOnlyTable` 整行拒改，于是对话框里那枚「确定」同样是死按钮。后一条不是测试写错——`probe.persist_calls` 的读数 `0 vs 1` 是本棒唯一一次「用例把实现逼出来」的现场。

**决定形态的口径**：⑴ **挂载点取宿主浮层而不取卡片**：卡片是 `LayoutBuilder` 的产物、每次 `rebuild_overlay()` 都会整块换掉，把对话框挂进卡片就得跟着重建走；挂 `host_.add_overlay` 之后序号只与「本层比候选浮层后加」有关，故摘除一律**降序**（`clear_binding_state()` 排在 `clear_family_state()` 之前，颠倒了那个序号就前移一格、此后摘到别的节点）。运行期追加的子树由框架在下一次布局入口补挂（裁决 **7.71**），本仓**不自备 `BuildContext`**。⑵ **模态层的吸收由本仓补而不是框架代劳**：`Dialog::on_hit_test_chain` 只在**内容盒之外**返回自身那一条，盒内**原样交回内容的子树链**；`Column` 无自身可点语义时那条链是**空表**，而 `OverlayHost` 的链逆序下降遇空表就继续往下问——于是落在卡片留白处的那一击穿透模态层打到面板行区（实测：卡片内一处留白 → 链尾是行区 `Scroll` 而非本对话框）。按裁决 **7.61③**「可点语义必须落在绘制者身上」，底色与一枚空 `clickable` 都挂在那位绘制者身上，不另铺一层透明板；按裁决 **7.13②** 这属公共 API 可组合，**不新增框架缺口**（附录 A.2 的开放缺口维持 G38 / G39 / G40）。⑶ **`ReadOnlyTable` 的「只读」改判为入口形态而不是键不可写**：`accepts_text()` 不收它、色槽与未配按域挡下、`validate()` 要求形态族与 `OverrideMap` 相符，于是**结构上只剩一条**能写它的路径（`commit_value(key, FormValue::overrides(...))`），这已足够兑现 S9 / D3-a 那句「不给会失灵的按钮」。`CommitIssue::ReadOnly` 因此成为不可达枚举并整体删除（枚举 + `settings.issue.read_only` 词条 + `issue_key()` 分支 + 两处测试引用），提交未通过的原因由十三档改**十二档**——留一个抓不到的枚举就是留一条假契约。⑷ **`Escape` 的交接由四层改五层**，对话框排最里：它自己那层遮罩吸收点击而**不**代劳关闭（框架 `set_on_close` 只在按钮那一路触发），故键盘是它唯一的出口，与快照视图那一档同形。⑸ **句柄释放按裁决 7.67 补一处**：`shortcut_edit_buttons_` 原先只在 `build_shortcuts_section()` 里清，摘浮层那一刻那些按钮正落在 G34 的「活在容器之外被摘走」那一档；补两句 `clear()` 后全套该告警 **45 → 18 行**，余量逐条归因（每例一次真点关对话框＝裁决 **7.70④** 那条既有形态；`TerminalView` × 5 与 `Canvas` 是 7.67 的在册残余），**未消音、未改级别、未给框架打补丁**。

**代价**：① 装配层启动重放那一腿**照旧按 G40 阻塞**（裁决 **7.84**），本棒只把「改得动、落得下」这一段接通，故用户重绑的组合键在下一次启动之前仍不生效；② 「只读表」这一改判意味着快捷键页的行编辑能力从此由**入口形态**而不是由枚举挡住，若日后新增别的 `ReadOnlyTable` 行，其「只读」同样只由 `accepts_text()` 与域判定承担，不再有整行拒改那道闸；③ `main.cpp` 不编入 CTest runner，对话框的呈现观感、输入框手感与冲突提示可读性归**真机走查**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条**不宣称快捷键重绑「可用」**；④「点浮层外即关」对对话框这一层仍受裁决 **7.81** 那条射程限制（落点在视口之外的标签条、把手时没有人代发）。

**验收**：`itest_settings_panel` 59 → **63 例**（新四例全须 `AURORA_BACKEND_HEADLESS`：真点「编辑」⇒ 浮层数 2 且 `Dialog` 进真实派发链、卡片留白处全扫描无穿透、底层那一行「编辑」不可达；真点「确定」⇒ **落盘 1 次广播 0 次**（`shortcuts.overrides` 是 `Absent ∧ Immediate` ⇒ `PersistOnly`）且写入内容逐字为新组合键、对话框与浮层序号一并消失；「取消」与 `Escape` 两腿不提交而面板存活、重开复用同一层、两次 `Escape` 先对话框后面板；遮罩那一击既不关对话框也不关面板）、`utest_settings_form` **13 例**（原 `read_only_rows_refuse_edits_entirely` 改写为 `the_override_table_takes_only_the_value_its_own_control_gives`），非 e2e 通道 `ctest -E etest_` **47 项全绿**（Linux/gcc + Ninja + 无头后端；新例落在既有 CTest 项内故项数不变）。**五条变异台账**：M1 删挂载 → 新四例全红；M2 删吸收板 → 只第一例红在「无穿透」那一句；M3 恢复整行拒改 → 单元例 + 集成第二例红而其余 62 例绿；M4 删 `shortcut_edit_buttons_.clear()` → **63 例全绿**，其证人是告警行数 `18 vs 45` 而非任何红行（按裁决 **7.49⑥** 如实登记为读数，不伪造断言）；M5 删 `clear_binding_state()` → 第二例 `2 vs 1` 与第三例 `1 vs 0`。全部已还原并核验。

**落点**：`src/ui/settings_panel.{h,cpp}`（`add_overlay` 挂载、`binding_overlay_index_`、`clear_binding_state()`、对话框内容板的底色 + 空 `clickable`、`Escape` 五层交接、两处 `shortcut_edit_buttons_.clear()`）、`src/ui/settings_form.cpp` + `include/borealis/ui/settings_form.h`（删整行拒改与 `CommitIssue::ReadOnly`）、`include/borealis/ui/settings_catalog.h`（`ReadOnlyTable` 注释就地更正）、`src/ui/settings_i18n.cpp`（词条十三 → 十二档）、`tests/unit/utest_settings_form.cpp` 与 `tests/integration/itest_settings_panel.cpp`；`codespec/SPECIFICATIONS.md` §7 的 7.88 与其 §2 的 PREF.04 落地现状段就地更正、`codespec/PLAN.md` 的 M2 进度行、`AGENTS.md` §6 与 §2。
## v0.87（2026-10-08）**`SPEC.FEAT.PREF.07` 的后两条腿已落：滚动 N=5 份快照 ＋ 面板一键回滚 ＋ 本地导出导入**（`include/borealis/config/store.h` + `src/config/store.cpp` + `tests/unit/utest_config.cpp` ＋ `src/ui/settings_panel.{h,cpp}` + `src/ui/settings_i18n.cpp` + `src/main.cpp` + `tests/integration/itest_settings_panel.cpp`，判据入册为裁决 **7.87**）

**动机**：`SPEC.FEAT.PREF.07` 一句里装了四件事，前三件早已在册（原子写归框架 `flush()`、损坏文件先备份再回落默认见裁决 7.26④、降级对话框见 **7.76**），唯余「保留最近 N 份配置快照可回滚」与「支持本地导出/导入用于换机迁移」两件至今未落（当年的留痕 `TODO(SPEC.FEAT.PREF.07)` 已随裁决 7.76 的降级对话框一并撤除，事项本身仍在 `PLAN.md` §8 的未落清单里）。人已拍板形态为「滚动 **N=5** 份快照 ＋ 面板**一键回滚**」。

**决定形态的口径**：⑴ **快照取在写之前而不是写之后**——`replace()` 落盘前把当时的配置文件复制成一份快照，于是名单首项恒是「这次即将被覆盖掉的那一份现场」，面板的「回滚到上一次」因此落在一个**真实存在过**的状态上；回滚本身就是一条普通写、走同一条复制，不必为回滚另设应急备份。撞名（epoch 只到秒）即加序号，而序号取**该秒已有尾段的最大值 + 1** 而不是第一个空位——淘汰删掉的恰是这一秒里序号最低那格，复用它等于把刚拍的「最新现场」命名为最旧档，下一次落盘的淘汰会立刻把它删掉。首次落盘没有现场可拍，那不是失败也不记成快照；拍不成只 `AURORA_LOG_WARN` 留痕、**不拦下用户的保存**。⑵ **读侧只有一条**（装载 / 回滚 / 导入共用同一个 `read_settings`）而**写侧的顶层文档也只有一份**（版本号 + 四域，同时是给 `Preferences` 的写入内容与导出件正文）——快照与导出件本来就是同一 schema 的文档，两处各列一遍四域，导出件就会落后于 schema；「导出的文件能被 `import_settings()` 原样装回」那条判据的根据正是这一份文档。名单每次进视图**现取而不缓存**，因为回滚与导入都会顺手把当前现场拍成新一份快照，那一次动作里名单就已经变了。⑶ **回滚只接受自家快照**：候选集取「本目录内、按 `.snapshot-` 命名形态存在」的文件名单，一次 membership 检查同时钉住**目录、命名形态与存在性**三件事——放行任一路径就是把「回滚」变成「覆盖任意文件」。⑷ **外部文件按不可信输入整体拒绝**，与装载侧那套「逐键回落 + 留痕」的宽容**刻意相反**：非法 JSON / 顶层非对象 / 四域全无 / `schema_version` 缺失或高于本仓支持 / **任一层**键名撞禁列名单，五种形态一律回 ASCII 原因且内存值与配置文件一字未动。根据是自家损坏文件要「仍然起得来」，而把一份手滑的外部文件读成「一堆默认值」再落盘，正是需求那句「绝不静默清空」要挡的事；取值域之外的**单个值**在导入路照旧回落（那是版本之间正常的演进），顶层结构不合才是「这根本不是一份配置」。凭据按 7.26⑥ 结构上就不在 `Settings` 内，故禁列名单对自家写出的文件是**永不开启的分支**，它守的是外部交回的那一份——需求那句「凭据句柄不导出」由此从「内存里没有」升级为「导不出也进不来」。⑸ **导出件自己走一次「临时文件 + rename」**：目标路径由用户点名而框架的锁与临时文件只管自家配置文件，这一小段不经 `Preferences`；导出不该留下写一半的文件，那是换机迁移的唯一一份素材。⑹ **面板侧三枚动作按钮走「六条接缝成组」闸门**而不是逐条问空：`pick_*_path`（让用户点名文件那两条腿）缺席时导出就只剩一枚点了没反应的按钮，而按钮自己不知道缺的是哪条腿（判据文 D3-a / 7.38⑥ F-b 的「不给会失灵的按钮」，这里把成组范围从一枚扩到三枚）。回填漏斗同样只有一条（`reload_from_store()`，回滚与导入共用）：`form_` 先换 → 广播交**整份**表单（这一次动的键就是全表）→ 预览排其后 → 最后重建浮层（通用行的控件值建行时从表单读，不重建就显出「表单已是新值、开关还停在旧档」）。单点即回滚**不设二次确认**，根据是 ⑴——点错一格最坏是「回到上一次」，而那一次现场本来就在名单里。⑺ **显示形态两处刻意不做**：快照名取 `path.filename()` 逐字而**不用 `localtime`** 折成本地日期（平台假设不得渗入共享路径，AGENTS.md §4.5 第 23 条），字节数列取 `"{N} B"` 而**不在界面上说出保留档数**（`kSnapshotRetention` 是 config 侧常量，跨进面板就是 `config ⇄ ui` 模块环，7.32① 同因）。名单是右栏行区的一种态而不是浮层（`build_snapshot_section()` 顶替 `row_nodes`），故它没有「点外面即关」那条腿，`Escape` 是它唯一的键盘出口，且是面板 `Escape` **四层交接**里的一层。动作失败上屏的只有本仓词条表里那一句通用文案，`Store` 交回的 ASCII 原因走日志（§4.3 第 14 条）；取消选择整条不跑、**连留痕都不写**。

**代价**：三处如实登记。① 导出载的是**存储里那一份**而不是面板中未提交的编辑（`export_settings()` 是 `const` 且只读自家 `Impl`，面板那条腿结构上碰不到表单）；② 回滚与导入都会**丢弃面板原先未落盘的改动**，这是「回到那一份现场」的字面代价，由 header 那一行动作留痕说出来；③ 需求原文「档案 + 设置」的**档案一腿属 `SPEC.FEAT.CONN.03`（M3）且至今没有存储形态**，故首版导出件只载四域设置，那句射程差就地登记在 `SPEC.FEAT.PREF.07` 的落地现状段。**真机走查三处未做**（平台文件对话框的呈现与取消手感、快照名单的滚动手感与「一键回滚不二次确认」是否让人安心、动作留痕那一行的可读性；会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条**不宣称「可用」**。**框架面真缺口 0**：按裁决 7.13① 逐条读 Aurora 当日活动分支公共头与实现体复验，`Preferences` 只管自家文件，而 `file_dialog` 的 `save_file` / `open_file` 与 `Result` 均在公共 API 上且**暴露无头钩子**——本棒登记时一度写下的「取消那条腿结构上测不到」注释据实测撤除，快照与导出件两条腿全在本仓文件系统算式内，属 7.13② 可组合故不出任务书；附录 A.2 的开放缺口维持 **G37 / G38 / G39 / G40** 四条不变。

**验收**：`utest_config` 12 → **22 例**（新 10 例：首次落盘不拍快照而第二次保住前一份现场 / 保留档数与新→旧次序 / 外来文件不进名单 / 回滚装回旧现场且当前现场仍在 / 非自家路径一律拒 / 导出件与配置文件同构 / 导出件原样装回等值 / 不安全文档整份拒绝且一字未动 / 导入的域外值照旧回落 / 配置目录内任一文件都不带凭据键名）；`itest_settings_panel` 49 → **59 例**（新 10 例全须 `AURORA_BACKEND_HEADLESS`：六条不齐时一枚按钮都不画 / 名单视图只投影而既不碰盘也不碰两个视口 / 空表那句「暂无快照」且无回滚口 / 真点中间那一行的「回滚」即装回**那一份**并整表重载 / 被拒的回滚原地不动且界面只有通用那一句 / 取消导出不跑任何东西并留着上一句留痕 / 成功的导出点名的正是那枚文件而表单一个值都没提交 / 成功的导入重载副本并整份广播一次 / 被拒的导入留住每个值且零广播 / `Escape` 先离开名单视图再关面板）。13 条 `settings.config.*` / `settings.snapshot.*` 词条入表，非 e2e 通道 `ctest -E etest_` 维持 **47 项全绿**（新增的 20 例落在既有两项 CTest 内故项数不变）。**十四条变异注入十三条各有指名红行**（m1 闸门只判五条腿 → 成组那例；m2 名单投影次序反转、m3 零字节把尺寸文本画空 → 投影那例；m4 去掉空表文案 → 空例；m5 忽略点击下标恒取首项 → 回滚那例；m6 被拒仍继续重载、m7 把 Store 的 ASCII 原因画上界面 → 被拒回滚那例；m8 取消也写留痕 → 取消那例；m9 导出腿顺带广播 → 取消与导出两例；m11 失败路径不碰控件只标脏 → 四例；m12 漏斗不广播 → 回滚与导入两例；m13 被拒的导入仍重载 → 拒绝导入那例；m14 `Escape` 不再离开名单 → 交接那例），**m10（留痕文本排在重建之后）是等价注入**并按裁决 7.49⑥ 如实登记而不伪造证人（卡片是 `LayoutBuilder`，闭包在下一次布局才读模型态，两种次序给出同一像素；次序仍保留，因为「先改模型再重建」是这条漏斗唯一的读法）。全部变异已还原并核验（0 个变异标记残留）。本棒未复跑吞吐门禁并给理由（新代码只在面板打开与点动作按钮时生效，导出/导入是文件 IO 而非每帧路径，基准四场景不含面板）。

**落点**：`include/borealis/config/store.h`（`kSnapshotRetention` / `SnapshotInfo` / `snapshots()` / `rollback_to()` / `export_settings()` / `import_settings()` ＋ 文件头那句「本层只做框架不做的**三件事**」）、`src/config/store.cpp`（快照命名与滚动、membership 闸、共用的读侧与顶层文档、禁列名单与信封校验、临时文件 + rename）、`src/ui/settings_panel.{h,cpp}`（`SnapshotEntry` / 六条 `Hooks` 腿 / 三枚按钮与留痕行 / `build_snapshot_section()` / `reload_from_store()` / `Escape` 四层交接）、`src/ui/settings_i18n.cpp`（13 条词条）、`src/main.cpp`（`picked_save_path` / `picked_open_path` 与六条腿的装配；`config/store.h` 的 `TODO(SPEC.FEAT.PREF.07)` 早在裁决 7.76 那棒即已消除，本棒落的是它指向的那两件事）、`tests/unit/utest_config.cpp`（新 10 例）、`tests/integration/itest_settings_panel.cpp`（新 10 例与 `snapshot_fixture` / `reveal_button` / `enter_snapshot_view` / `painted_notice` 四件夹具）、`SPECIFICATIONS.md` §4.6 的 `SPEC.FEAT.PREF.07` 落地现状句与 §7 裁决 **7.87**、`codespec/UI_SETTINGS.draft.md` §1 范围句与 §4E 的 E1-a 与其 §8 落地清单、`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.07` 行与 §8 的 M2 段、本文件 v0.87、`AGENTS.md` §2 与 §6。

## v0.86（2026-10-08）**`SPEC.FEAT.WS.05` 本地腿「一键重启」已落：dead-session 浮层 ＋ `session.restart` 命令 ＋ `Session::on_closed` 的唤醒即边沿**（`src/ui/terminal_view.{h,cpp}` + `src/session/session.cpp` + `src/main.cpp` + `src/ui/settings_i18n.cpp` + `tests/integration/itest_render_viewport.cpp` + `tests/unit/utest_session.cpp`，判据入册为裁决 **7.86**）

**动机**：v0.82 交付多标签批次时把 WS.05 明列为未落项——本地终端进程退出后终端内容虽然还在（架构 §7.3 的旧内容保留），但用户既看不到「进程已退出」的显式提示，也没有把「回看内容 → 重新起一个会话」这一步接起来的入口；只能关掉那一格再新建一格，且新建格的位置与名字都不再是原来那一格。人已拍板形态为「『重启』浮层按钮，同时注册 `session.restart` ＋ 快捷键（如 `Ctrl+Shift+R`）。代价：视口要多一层」。

**决定形态的口径**：⑴ **浮层是绘制序列的最后一层**（在光标层与回看指示条之后，`paint_restart_overlay`）；带底走 `ui::mix_half(default_background, black)` 而不是固定 alpha，因为「哪一档主题」由 `apply_appearance` 运行期决定（裁决 7.38 选区失焦那条同源）；卡片描边 / 文字 / 按钮底色分别取 `settings_chrome()` 的 `card_line` / `text` / `accent`（该入口随裁决 7.81 从 `settings_panel.cpp` 提为公开，本条是其第二次消费）。⑵ **命中判定排在鼠标上报分流之前**——会话已断时上报档是陈旧的远端状态、不再有任何进程会读到它，先判上报就把这一笔让给了不存在的接收方；按钮落点之外仍走选区，需求那句「保留终端内容供回看」正要求内容**仍可选可复制**。对照取 `e.local_position` 与 `restart_button_box_` 同一空间（控件本地 dp），承的是裁决 7.64②③ G31 回货那条 `content_origin` 已是绘制与命中三处唯一原点算式，本件不再补 `bounds.origin`。⑶ **`session.restart` 命令与浮层按钮同源一份闭包**——`restart_current_tab` 声明在 `wire_view` 之前、按引用捕获，两处入口都指向它，「按钮重开」与「快捷键重开」结构上不可能分叉；`Ctrl+Shift+R` 与 `ui::workspace_key_bindings()` 十二条分屏保留位无一同形。⑷ **重启动作＝同 TabId 就地覆盖 `TabWorkspace`**（`Session` 持 mutex 不可拷贝不可移动 ⇒ 新 `shared_ptr<Session>` + 新 `TerminalView`）；`tab_strip` 不动顺序也不动名、`current_tab_id` 一格不动、闭包栈不入不取（那是 WS.10 的账），于是「同一格回看内容被同一格重新起用」这条语义**由结构表达**而不是靠用户辨别两格同名标签；命令与目录一律从**当前**配置读（用户可能已改过 `local_shell` / `startup_directory`）。⑸ **`Session::on_closed` 是无提交唤醒那条路径的唯一例外**：批量输入路径「这一轮没有产出提交就不排帧」是省空帧的既有判据，而连接生死本身就是要让主线程看一眼的状态变更——不唤醒则进程干净退出（无残留半截序列且最后一次 flush 已排在上一帧边界之后）成了无提交的一轮，浮层要等到下一次别的唤醒才显形，症状即「过了一晚上终于看到进程早挂了」。⑹ **未装钩子即不画按钮**（而不是画一枚点了没反应的按钮），与 7.41 的「无宿主 ⇒ 问不了 ⇒ 不粘」同一条判据；视口构造点与既有用例零改动。

**代价**：SSH 断线自动重连那一腿随 `SPEC.FEAT.CONN.02` 落期，本条只交本地腿；真机走查未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①）——浮层呈现观感、按钮落点是否够大、以及重启瞬间旧回看与新会话的交接感三处只能人工判，故**不宣称一键重启「可用」**。**框架面真缺口 0**（按裁决 7.13① 逐条读 Aurora 当日活动分支公共头复验：绘制层次、指针派发、`mix_half` 与 chrome 交接全在既有语义内，属 7.13② 可组合故不出任务书）。

**验收**：`itest_render_viewport` **+4 例**（现 38；存活态不画浮层、干净退出后浮层落帧、真点按钮正中即钩子恰响 1 次、按钮之外的 Press→Move→Release 拖拽仍是选区），`utest_session` **+1 例**（`clean_close_wakes_the_frame_even_with_no_pending_damage`），非 e2e 通道 `ctest -E etest_` 维持 **47 项全绿**（5.77 s）；**四条变异注入各有指名红行**（M1 删 `on_closed` 的 `wake_frame()` → 只唤醒例外那一例红；M2 视口双短路改成 `if (true)` → dead-overlay 落帧与不画两例同红；M3 删 Press 分支的按钮命中判定 → 只真点那一例红；M4 把按钮盒对照改成 `if (true)` → 只按钮外拖拽那一例红），每次重链前先删 `build/**/*.ilk`，全部变异已还原并 `grep -c MUTATION` 双确认 0。本棒未复跑吞吐门禁并给理由（新代码只在会话生死那一帧与浮层绘制里生效，绘制与布局算式零改动，基准四场景不含浮层交互）。

**落点**：`src/session/session.cpp`（`on_closed` 唤醒与 WHY 注）、`src/ui/terminal_view.{h,cpp}`（`set_restart_hook` / `session_dead` / `restart_button_box` 三处公共面 ＋ `paint_restart_overlay` 与两个成员 ＋ Press 分支的命中判定）、`src/main.cpp`（`restart_current_tab` 声明与实现、`wire_view` 里挂 `set_restart_hook`、`session.restart` 命令注册），`src/ui/settings_i18n.cpp`（`session.restart.{title,hint,button}` 三条中文词条）、`tests/integration/itest_render_viewport.cpp`（FakeConnection 的 `close_and_notify`、Harness 的 `pointer_at` / `install_restart_counter` / `kill_session_and_render` / `restart_button_box` / `restart_hook_calls` 与四例）、`tests/unit/utest_session.cpp`（唤醒例外那一例与套件头注就地更正）、`SPECIFICATIONS.md` §4.4 的 `SPEC.FEAT.WS.05` 那句落地现状与 §7 裁决 **7.86**、`codespec/PLAN.md` 的 WS.05 行与 M2 交付状态、本文件 v0.86、`AGENTS.md` §6。

## v0.85（2026-10-08）**`SPEC.NF.PKG.01` 打包分发**：人已裁决本轮延后（判据入册为裁决 **7.85**）

**动机**：M2 出口判据含「产出可分发安装包」一档，但开工该件需要人拍三件本仓**不能自行择一**的事——打包器选型（NSIS / MSIX / 便携 zip，三条路的用户体验、签名与更新模型各不相同而需求原文未指名）、CI 基建（本仓 §6 已在册「无工作流」，`tools/check/*` 门禁脚本已落但无可跑它的流水线，而需求原文把「进 CI」写成验收判据）、`assets/` 下的 LICENSE 与三方许可文本汇编（目录尚未创建）。按 AGENTS.md §4.1 第 2 条「等开工指令」，本轮不进入。

**决定形态的口径**：⑴ **裁决 7.12 那条 vcpkg 前置不撤销**——三方依赖经 `find_package` + vcpkg、Aurora 经 `add_subdirectory` 走源码树，是 PKG.01 开工时的技术前提，本轮只是把「产出可双击安装的包」这一交付档从 M2 移出。⑵ **M2 出口判据「可分发安装包」本轮不满足**——PLAN.md §8 的 M2 行把该件从「未交付」改标「本轮延后」，其余 M2 交付档（多标签、命令面板、三条资源门禁、调试面板、快捷键重绑、WS.01 关闭确认）不受影响。⑶ **无代码改动、无测试、无回读**——按裁决 7.49⑥ 的等价注入口径如实登记而不伪造绿灯。

**落点**：`SPECIFICATIONS.md` §4.3 的 `SPEC.NF.PKG.01` 那句落地现状、§7 裁决 **7.85**、`codespec/PLAN.md` 的 PKG.01 行与 §8 M2 段、本文件 v0.85、`AGENTS.md` §6。

## v0.84（2026-10-08）**`SPEC.FEAT.PREF.04` 的装配层启动重放腿按实测判为框架缺口 G40 阻塞**（`src/main.cpp` 留 `TODO`、判据入册为裁决 **7.84**，附录 A.2 开放缺口 2 → 3）

**动机**：v0.82 交付 PREF.04 时把「装配层启动重放覆盖表」明列为未落项，本轮按人 2026-10-08 在 AskUserQuestion 上的拍板「装配层 `bind_shortcuts` 之前一次重放」开工，却在读源阶段撞到一条框架侧公共 API 形态缺口。

**决定形态的口径**：⑴ **两条实现路径都被否证**：路径 A 是把覆盖表文本反解成 `KeyCombo` 再重写 `Command::default_binding` 或调注册表 setter——`include/aurora/commands.h` 十五条入口（`add` / `remove` / `clear` / `count` / `contains` / `find` / `all` / `is_enabled` / `set_enabled` / `invoke` / `search` / `to_json` / `bind_shortcuts` / `to_menu_items` / `shortcuts`）中「改绑定」只能靠**重注册完整 `Command`**，等于本仓再抄一份元数据。路径 B 是绕开 `CommandRegistry` 直接把覆盖挂到 `ShortcutRegistry`——`include/aurora/app/shortcuts.h:70` 只有 `KeyCombo::to_string()` 而无 `from_string` / `parse`，`key_name` 单向表藏在实现文件；本仓自造文本→键位的反向解析就是**第二真值源**（裁决 7.72 立身之本正是「比对吃 `term::KeyPress` 而**不**自造键名反查表」）。⑵ **按 AGENTS.md §5 第 2 条「不长期持有框架分叉」+ 第 5 条「不凭训练记忆假设 API 存在」**，缺口类别属「渲染与事件链路上的公共 API 形态」（裁决 7.13①），按**不等不绕**派发 Aurora 侧补而不绕。⑶ **代码处只留一行 `TODO(SPEC.FEAT.PREF.04)` 而不写占位实现**（§4.1 第 3、7 条）；装配层不编入测试 runner，本轮无测试也无变异自证，按 7.49⑥ 如实登记。⑷ **两条可接受的回货形态**（详见 `codespec/FRAMEWORK_TASK_PREF04.md`）：**甲**＝公共头补 `KeyCombo::from(std::string_view) -> std::optional<KeyCombo>` 且与 `to_string()` 逐字节往返等值；**乙**＝`CommandRegistry` 补只换绑定的入口 `set_binding(command_id, KeyCombo)`。本仓接货复验形态在裁决 7.84⑤ 里写死。

**代价**：面板改一次组合键 → 落盘成功、当次会话内以表单值为准显示与比对，但**实际生效**要等到下一次重启（装配层重挂在回货前结构上做不到）；该代价与 v0.82「重启后生效需装配层按 `shortcuts.overrides` 重挂绑定」的欠账是同一件事，本轮只是把「欠一腿」升级为「欠一腿且已登记阻塞缺口」。真机走查照旧未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①）。

**验收**：构建通过；非 e2e 通道 `ctest -E etest_` 维持 **47 项全绿**（本条只新增一行 `TODO` 与文档，无代码逻辑改动）。

**落点**：`src/main.cpp` 的一行 `TODO(SPEC.FEAT.PREF.04)`、`SPECIFICATIONS.md` §4.3 该需求句的落地现状指针与 §7 裁决 **7.84**、附录 A.2 的 **G40** 行、`codespec/FRAMEWORK_TASK_PREF04.md`（新）、`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.04` 行与 §8 M2 段、本文件 v0.84、`AGENTS.md` §6。

## v0.83（2026-10-08）**`SPEC.FEAT.WS.01` 的关闭前确认（有运行中进程时）已落：装配层按 7.47⑧ 判据呼一次确认框、答 Yes 才关整张标签**（`src/main.cpp`、判据入册为裁决 **7.83**）

**动机**：v0.82 交付多标签批次时把「关闭前确认（有运行中进程时）」明列为未落项——标签条的 close 钮当时无条件走「摘标签 → 记闭包栈 → 销毁工作区与会话」，于是用户在 vim / build / ssh 还在跑的时候点一下 × 就把进程静默杀掉了。判据早在裁决 7.47⑧ 拍死，本条只是把它接上装配层。

**决定形态的口径**：⑴ **判据一字照取 7.47⑧**——「该标签**任一** pane 的会话进程仍在（`Session::alive()`）」即呼确认框，**一次确认关整张标签**（逐 pane 追问在多 pane 标签上是 N 个对话框；只看选中 pane 会静默杀掉未选中 pane 里跑着的任务），全部已退出则直接关、不再问。⑵ **关闭动作收敛成单一 `perform_close` 闭包**：原先「确认」与「直接关」两条路径各写一遍「摘标签 → 记闭包栈 → 销毁工作区 → 必要时切相邻标签」，两份就会分叉（典型症状是「确认后关闭漏记闭包栈，撤销关闭找不回那一张」）；现两条路径共用同一支，差别只在要不要先问一句。⑶ **确认框与多行粘贴确认同形态**——懒挂一只常驻 `au::Dialog`（首次 `host->add_overlay` 之后复用同一实例换文案），内容取框架 `au::confirm(title, message, bool_callback)`，答话闭包先 `close()` 再按 `accepted` 决定是否 `perform_close`；生存期用成员 `shared_ptr` 跨过 Show→答话那一段，与 `TerminalView::ask_multiline_warning` 同一条持有理由。**代价如实登记**：`src/main.cpp` 是 `add_executable(borealis main.cpp)` 而**不编入** `borealis_core`，故 CTest runner 结构上抓不到装配层这条路径；判据本体是 `any_of(alive)` ＋「呼出一次对话框」，抽一枚纯逻辑件来测只是把 `any_of` 再测一遍（会话存活判定本身已由 `utest_session` 覆盖），按裁决 7.49⑥ 的等价注入口径**不伪造绿灯**，其证人归真机走查。

**验收**：构建通过；非 e2e 通道 `ctest -E etest_` 维持 **47 项全绿**（本条未新增用例，理由见上）。

**未落与代价**：真机走查未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①）——确认框呈现、答 No 后标签仍在、答 Yes 后整张关且进闭包栈可撤销，三处只能人工判，故不宣称 WS.01「可用」。

**文档回写**：本条、裁决 **7.83** 与 §7 标题段日期范围、`SPEC.FEAT.WS.01` 那条的落地现状句、`codespec/PLAN.md` 的 M2 未交付清单与 `SPEC.FEAT.WS.01` 行、`AGENTS.md` §6。

## v0.82（2026-10-08）**M2 工作区多标签批次：标签条界面腿 ＋ 装配层多会话化 ＋ WS.04/06/07/10 ＋ PREF.04 重绑 ＋ RELI.01 调试面板 ＋ PERF.03–05 资源门禁，同批登记框架缺口 G38 / G39**（`src/ui/tab_bar.{h,cpp}` 新增、`include/borealis/ui/closed_tab_stack.h` + `src/ui/closed_tab_stack.cpp` 新增、`src/ui/debug_panel.{h,cpp}` 新增、`include/borealis/ui/tab_strip.h` + `src/ui/tab_strip.cpp`、`include/borealis/term/terminal.h` + `src/term/terminal.cpp`、`include/borealis/session/session.h` + `src/session/session.cpp`、`src/ui/settings_panel.{h,cpp}` + `src/ui/settings_i18n.cpp`、`src/main.cpp`、`src/CMakeLists.txt`、`tools/CMakeLists.txt` + `tools/bench/{startup_time,memory_usage,idle_cpu}.cpp` + `tools/check/check_startup_gate.{sh,ps1}` / `check_memory_gate.sh` / `check_idle_cpu_gate.sh`、`tests/unit/utest_closed_tab_stack.cpp` 新增 + `tests/unit/utest_tab_strip.cpp` / `utest_terminal.cpp` / `utest_session.cpp` 新例 + `tests/integration/itest_debug_panel.cpp` 新增 + `tests/integration/itest_settings_panel.cpp` 重绑新例，判据入册为裁决 **7.82**）

**动机**：v0.81 之前装配层始终持单会话——`ui::TabStrip`、`ui::PaneTree`、`ui::TerminalView` 三件各自齐备却从未在同一棵树上接起来，标签条与分屏对用户不可见。本条覆盖五提交（WS.07 命令面板装配 `e626918`、三条资源门禁 `057554e`、多标签批次 `ae45559`、调试面板 `9fbbdbb`、快捷键重绑 `66aec8c`）一并出账，因为「装配层多会话化」「帧泵范围」「每视图浮层挂接」是同一棵树上的三件事，拆开记账会留下互相引用的空洞。

**决定形态的口径**：⑴ **标签条界面腿**（`src/ui/tab_bar.{h,cpp}`，私有头 TU 同裁决 D1①）：栏位宽按显示名实测钳 `[96, 240]` dp、溢出走横向偏移、拖拽阈值 4 dp 才进重排、就地重命名是 `TextInput` 子类补 `Esc` 与失焦且**空串提交即撤销重命名**（7.43① 的界面兑现）、close 钮 hover/活动两态；铃铛红点与活动绿点**只画在未选中格**上，由 `TabStrip` 新增的 `mark_bell_triggered` / `take_bell_triggered` / `mark_activity` / `take_activity` 供数（取走即清零，选中格的 activity 在切中时消费）。⑵ **装配层「标签 → pane 树 → 每 pane 会话与视图」**（`TabWorkspace{workspace, sessions}`，场景根 `Stack` child[0]＝标签条、child[1]＝工作区），随批修掉三处装配缺陷：(a) 每个视图须经 `set_overlay_host()` ＋ `set_search_dependencies()`——原先只有首标签一只视口拿到搜索依赖而宿主从未挂上，**右键菜单、多行粘贴确认与搜索浮层在其余 pane 上结构上弹不起来**；(b) 挂接收敛成单一 `wire_view` 闭包，新建标签与撤销关闭重开两条路径共用；(c) 帧泵范围为**全部**标签的会话（裁决 7.47⑩），隐藏标签的 OSC 52 / BEL / 活动标记与标题不再停摆。⑶ **WS.04**：`term::Terminal::trigger_bell` 锁内登记不做 IO，`Session::take_bell_triggered()` 每帧取走折进标签条；可听 beep、连接类型图标与断线角标未落。⑷ **WS.10**：`ui::ClosedTabStack`（公共头纯逻辑件，上限 10、满则丢最旧、共享单调 `TabId`）＋ `TabStrip::insert_at` 回插原位置；本地终端腿已落，SSH 档案腿随 `SPEC.FEAT.CONN.02`。⑸ **WS.06**：`workspace.toggle_fullscreen` 绑 F11（ESC 不退出，避免与会话内 ESC 冲突）；窗口标题按选中格 `resolve_tab_name` 逐帧**差分**写回。⑹ **RELI.01 面板腿**：`ui::DebugPanel` 绑 F12（`diagnostics.open`），**打开那一刻**跨全部标签的会话取快照（解析/解码/队列三族计数器的读点只在主线程那一次调用里碰锁，不进每帧路径）；**崩溃留存一腿待人裁决**——信号钩子 / dump / 自写诊断的选型与跨平台形态未拍，代码处以 `TODO(SPEC.NF.RELI.01)` 留痕，本仓不自行择一实现。⑺ **PREF.04**：每行「修改」钮呼出重绑浮层，**录入形态是文本输入组合键而非键序抓取**（框架公共面无「按下即得 `KeyCombo`」的抓取器，代价在此登记），文本→`KeyCombo` 反解析由本件自持单向表且键名以注册表显示串为准；空绑定＝清除覆盖、恢复默认＝一次提交空覆盖表；冲突比对走 `ui::build_shortcut_rows` 同一算式（只比 Shift/Ctrl/Alt/Meta 四位，7.72）而面板侧不复算；**装配层启动重放覆盖表的一腿未落**。⑻ **PERF.03–05 三条资源门禁**：三枚独立可执行 ＋ bash 门禁脚本为主（本机无 PowerShell）、ps1 并存供 Windows 腿，**不挂 CTest**（裁决 7.34 同口径）；实测 startup 12.8–18.4 ms（进程内段，为下界）、空载 RSS 11.5 MB / 10k 行灌注后 21.7 MB（线 150 MB 照需求原文；空载 50 MB 是回归守卫数并在脚本头注明非 spec 线）、空闲 CPU 0.002%（线 1%）；三条脚本均按 §4.4 第 22 条以变异注入自证非空转（改阈值 / 改 spec 键 / 删指标各转红，未变异样本 PASS）。

**WS.07 一并出账与 G38 / G39 的登记**：`command_palette.open`（`Ctrl+Shift+P`，`Global`）经框架 `CommandPalette` 挂在 `OverlayHost` 呼出，当前注册动作六条（`settings.open` / `search.open` / `command_palette.open` / `workspace.toggle_fullscreen` / `tab.undo_close` / `diagnostics.open`），命令标题走本仓词条表。读源实测登记两条开放缺口：**G38**（`CommandPalette` 搜索框占位符硬编码英文 `"Type a command..."`，无 i18n 查表路径也无覆盖入口）与 **G39**（空态提示硬编码英文 `"No matching commands"`，同病灶），**附录 A.2 开放缺口 0 → 2**（G37 已于本日随 `child_content_origin()` 回货闭合，见其行）；可派发任务书见 `codespec/FRAMEWORK_TASK_WS07.md`（各三条回货判据：查表取得 ＋ setter 覆盖 ＋ 配套用例与变异自证），本仓在 `src/main.cpp` 留 `TODO(SPEC.FEAT.WS.07)` 待回货后调用 `set_placeholder()` / `set_empty_state()` 交中文词条——**回货前命令面板的汉化结构上只完成一半**。

**验收**：`utest_closed_tab_stack` 新套件五例、`itest_debug_panel` 新套件七例、`utest_tab_strip` +3 例、`utest_terminal` +2 例（bell 计数）、`utest_session` +2 例、`itest_settings_panel` 重绑新例；非 e2e 通道 `ctest -E etest_` **47 项全绿**；三条门禁脚本未变异样本全 PASS。

**未落与代价**：真机走查（拖拽重排与就地重命名 IME 手感、重绑浮层与冲突提示可读性、全屏焦点落位、三条资源项的真机腿与失焦降频那一腿无头不可判）；**RELI.01 崩溃留存待人裁决**；WS.10 的 SSH 档案腿随 `SPEC.FEAT.CONN.02`；WS.04 的连接类型图标、断线角标与可听 beep；WS.01 的关闭前确认（有运行中进程时）未落；PREF.04 的装配层启动重放覆盖表；WS.07 词条汉化待 G38/G39 回货。故本批**不宣称工作区「可用」**。**未复跑吞吐门禁**并给理由（新代码在标签条/浮层/门禁工具上，不进每帧绘制与布局路径，基准四场景不含多标签与浮层交互）。

**文档回写**：本条、裁决 **7.82** 与 §7 标题段日期范围、`SPEC.FEAT.WS.01` / `04` / `06` / `07` / `10`、`SPEC.FEAT.PREF.04`、`SPEC.NF.PERF.03`–`05`、`SPEC.NF.RELI.01` 各条落地现状句、`SPEC.FEAT.INTERACT.04` 那句 G37 现状就地更正、附录 A.2 的 **G38 / G39** 两行与 G37 行末句、`codespec/PLAN.md` 的 M2 行与接缝账、`AGENTS.md` §2 / §6。

## v0.81（2026-10-07）**`SPEC.FEAT.INTERACT.04` 的第五半：浮层本体 ＋ 视口侧持有与开关 ＋ 装配层登记，同批登记框架缺口 G37**（`src/ui/search_overlay.{h,cpp}` 新增、`src/ui/terminal_view.{h,cpp}`、`src/ui/settings_panel.{h,cpp}`、`src/ui/settings_i18n.cpp`、`src/main.cpp`、`src/CMakeLists.txt`、`tests/integration/itest_search_overlay.cpp` 新增，判据入册为裁决 **7.81**）

**动机**：v0.80 交出的表与高亮「打不开、没处输、跳不了」——`ui::search` 每帧产出区间而没有任何浮层承载查询输入与跳转，命中层叠也没有调用方。本条覆盖三棒（浮层本体 `d5b1bff`、装配层 `search.open` + `Ctrl+F` 登记 `a98444b`、集成用例 `7e767c4`），因为「关闭的单一漏斗」与「作用域怎么对账」是同一条路径上的两件事。

**六条决定形态的口径**：⑴ **「点浮层外即关」不需新代码**——视口 Press 分支早已无条件调 `OverlayHost::handle_outside_click(position)`（裁决 7.41③），射程限制＝落点在视口之外（标签条、把手）时没有人代发。条内三档宽度由 `tier_for` 按盒宽现算（`usable = box 宽 − 2 × 8`，`usable ≥ 496` → Wide（宽取 `clamp(usable, 496, 560)`）、`≥ 440` → Compact（同式取下限 440）、否则 TwoRow（两行，条高 44 → 66 dp）），锚点＝pane 内容盒右上角往内 8 dp（`anchor_for`）。⑵ **作用域机制按深度对账**：`open()` 先记 `scope_depth_before_`，`open_at` 之后**深度没变才**补 `push_scope`（两处入口——命令层与用例编程调用——都在派发栈外，`open_at` 取不到 `current_focus_manager()`）；关闭侧在 `on_close` 里只要当前深度还高于那一份读数就自己弹回，且整段挂在 `is_open()` 前提之下（不设前提会把祖先浮层——设置面板——压着的作用域错弹掉）。两条开合路径 × 四种组合都不漏也不双弹。⑶ **Enter 的取舍＝「有待提交的条件就只提交、没有才跳」**：`advance_search()` 先问 `view_.submit_search_query()`，返真就停在提交。「先提交再跳」在两档都坏且机制不同——字面量档会把你刚扫出的表重扫成**游标为空**的新表（永远前进不了）、正则档会在新表落地前先在旧表跳一格（跳的是过期匹配）。⑷ **计数链六态是一个函数一条优先级链**（B0 ~ B5，分母先算、序号后挂）；两档措辞取自两条不同词条——`search.pending_enter`＝「按 Enter 应用」**只在正则档说**（字面量档每键即扫，说「按 Enter」是谎报）、`search.reopen_hint`＝「按 Enter 搜索」**只在重开守旧文本时说**，结构上不会互相冒充。⑸ **chip 双轨**：可见文字走 `Aa` / `.*`（ASCII），朗读名走中文词条经 `set_accessibility_label` 一处兑现（A1-d）；Compact 及更窄档跳转钮取 `↑` / `↓`、Wide 档取中文。⑹ **尺寸一律走控件级尺寸意图**（`view_.height(px(...))`）而不挂修饰链——`Widget::layout` 跑完整条修饰链之后仍按 `width_` / `height_` 覆写 `size_`，挂链上的 `.height` 被**静默吃掉**（裁决 7.66① 实测）。`SearchInput` 私有子类在基类之前认领 `Shift+Enter`（＝后退，D1-a；基类 Enter 分支不区分 Shift，与面板 `BlurCommitText` 同档、裁决 7.52 S14）；两枚 chip 用 `Button` 选中态底色自绘而**不用**框架 `Switch`（禁用档色值硬编码浅色，裁决 7.68②）；关闭是单一漏斗 `on_popup_closed()`（点「关闭」/ `Escape` / 点浮层外三条真路径都汇到它），**Enter 恒不关**（F2-a 是负向断言而非第四条路径）；条体懒建、建后常驻（F1-c），重开只重定位并把输入框文本与两档由 `view_` 逐字读回。

**G37 的登记与读数**：本棒按实测登记 **G37**——`Widget::window_bounds()` 对 `Popup` 内容**结构性看不到锚点平移**（anchor 只活在 `Popup` 自己的布局 / 绘制 / 命中钩子），读数＝输入框 `window_bounds().origin` **{8,11}** 对真实盒 **{20,23}**、差恰是锚点 **{12,12}**；症状是「按该读数取点落不到控件上、`type()` 的字符被吞（基类 `Widget::on_text_input` 缺省即 `is_handled = true`）」。**本仓生产路径不受影响**（浮层锚点取的是焦点 pane 的 `TerminalView`，不在 `Popup` 内），受影响的是集成用例对 `Popup` 内容的取点，已走真命中链规避（`right_spot_of()`，两处注释留痕）；任务书随本条在会话内产出（回货判据三条与变异自证要求见附录 A.2 的 G37 行），开放缺口 0 → 1。

**三处脏卫生与一处新公开入口**：`Button::background()` / `text_color()` 与 `Text::set_content()` 只改值**不带脏标记**（只有 `set_enabled()` 无条件标脏）⇒ 两枚 chip 的档色与两枚跳转钮的可用态以三枚 `painted_case_` / `painted_regex_` / `painted_nav_` 守卫挡重写，计数槽直接读控件自己的 `content` 与将写值比较（不另立第四枚守卫）；`build_bar()` 建新树时三枚守卫一并播种。`SettingsChrome` / `settings_chrome()` 从 `settings_panel.cpp` 的文件内常量**提为公开入口**（判据文 A1-h 要求那张表是一个可取的量，否则浮层要么够不着那个头、要么在自己文件里再列一遍同样七个十六进制数＝第二真值源，裁决 7.46②）。

**两处台账更正**：词条 **11 → 12 条**（另补 `search.action.open`——F1-a 的命令标题，不在判据文 §4 第 6 条那十一句之内）；**TU 序号更正四处**（`search_overlay` 是**第五个**触达 `au::Painter` / `au::Widget` 的 TU——`src/ui/search_overlay.cpp` 头注、`codespec/UI_SEARCH.draft.md`、`SPECIFICATIONS.md` 7.78⑨、`AGENTS.md` §6 各一处；`CHANGELOG.md` 按历史照录不动）。

**m1~m9 变异台账**（逐条各有指名红行）：m1 删 `open()` 的栈外补 `push_scope` 回退块 → w4 `written() == "" vs "\x1b"`；m2 删 `shortcuts_.remove(escape_binding_)` → w4 `1 vs 0` 且 `2 vs 1`、w5 `1 vs 0`；m3 分母扁平化 → w8 `"10020" vs "10000+"`；m4 删 `view_.submit_search_query()` → w3 `1 vs 2`、w9 `1 vs 2`；m5 `usable = box_width`（丢两侧留白）→ w1 `-4 vs 12`（差 16）、`512 vs 496`；m6 删 `view_.close_search()` → w5 `matches() == nullptr is false`、w13 同；m7 删 `sync_state()` 里两句 `set_enabled` → w5 `next_button()->enabled is false`；m8 删关闭按钮 `set_on_click` → w5 `is_open() true`、`1 vs 0`；m9 `anchor_for` 丢边缘内边距 → w1 `20 vs 12`、`4 vs 12`（差恰 8）。全部已还原并核验（0 标记、工作区 diff 空），每次重链前删 `build/**/*.ilk`。

**验收**：新套件 `tests/integration/itest_search_overlay.cpp` **十五例**全须 `AURORA_BACKEND_HEADLESS`（真指针派发 + `au::OverlayHost`），15/15 全绿（377.38 ms）；非 e2e 通道 `ctest -E etest_` 44 → **45 项全绿**（31.17 s）。装配侧两件（`search.open` ＋ `Ctrl+F` 的 `Global` 登记——`Ctrl+F` 由快捷键层在控件消费之前拦截、**不进会话**，需求原文写死了这个组合，vim 的整屏翻页因此被拿走，代价登记在判据文 §3 D8）不在套件射程内、形态由提交交付。

**未落与代价**：**真机走查六处**（chip 符号是否认得出 / `↑` `↓` 图标档可辨性 / 命中居中滚动的跟手感 / `Ctrl+F` 被快捷键层截走后 vim 用户的接受度 / 极窄 pane 上无跳转按钮 / IME 组合态），故**不宣称搜索「可用」**；**未复跑吞吐门禁**并给理由（新代码只在浮层打开与按键时生效，不进每帧绘制与布局路径，基准四场景不含浮层交互）。

**文档回写**：本条、裁决 **7.81** 与 7.78⑨ / 7.79⑥ / 7.80⑨ 三处「附录 A.2 维持开放缺口 0」及三处「未落清单」就地更正、§4.3 该需求句指针、附录 A.2 的 **G37** 行与附录头复核日期、`codespec/UI_SEARCH.draft.md` §0 / §4 第 5、6 条 / §5、`codespec/PLAN.md` 的 `SPEC.FEAT.INTERACT.04` 行与 §6 / §8、`AGENTS.md` §2 / §6。

## v0.80（2026-10-07）**`SPEC.FEAT.INTERACT.04` 的第四半：命中高亮的绘制侧层叠、跳转落位与程序化滚动写入口（浮层本体未落）**（`include/borealis/ui/cell_layout.h` + `src/ui/cell_layout.cpp`、`src/ui/terminal_view.{h,cpp}`、`tests/unit/utest_cell_layout.cpp`、`tests/integration/itest_render_viewport.cpp`、`tests/integration/itest_search_viewport.cpp` 新增，判据入册为裁决 **7.80**）

**动机**：v0.79 交出的是一张「没人读」的匹配表——扫描接缝每帧产出区间，而绘制侧既没有把区间折进色带的入参形态，也没有把命中行滚进可见窗的写入口。本条覆盖两棒（绘制侧前置 `7965f3b` + 六例单元证人 `7354ad8`、视口侧层叠与跳转 `811423b` + 七例行为证人与六例像素证人 `29937a0`），因为「三层底色折叠在哪一处」与「谁当 C1-a 相撞判据的证人」是同一批口径。浮层本体（`src/ui/search_overlay.{h,cpp}`）与 `search.open` / `Ctrl+F` / `Escape` 的登记仍属 #161，本条刻意不含。

**四条决定形态的口径**：⑴ **相撞那格谁赢只有一处算式**：`ui::RowBands`（本行命中列区间切片 + 游标那一段 + 选中区间 + 三档最终底色）与 `ui::background_at(bands, column)` 是「主题底 → 命中 → 选中」的唯一折叠点，色带层与块形光标三段式的第三段**共用它**；两份算式就会分叉。折叠必须发生在切 run **之前**——若各换一次底，`min_contrast` 的前景重合成就是按**输掉的那档**底色算的，高亮强度会随「这一格恰在命中段边缘」而抖，run 边界也随之再切一刀。既有的四参 `layout_row(row, spec, RowSpan, RgbaColor)` 入口**收进** `RowBands` 一条腿而不并列保留（无命中时逐字段等价）。⑵ **三档底色一律由调用方给最终值**，失焦降级与 `ui::mix_half` 不进 `cell_layout`——这是判据 A1-f 那条不对称（**命中不随失焦降级而选区随**，与裁决 7.38① D3① 相反）唯一的可表达形态。⑶ **命中切片是视图而不是快照**：上限档一屏可达 10,000 段（≈240 KB），逐行拷进帧就是每帧多一次整表分配；表按 (行, 列) 有序，故按 `.row` 二分切本行那一段。⑷ **`scroll_row_into_view` 写的是滚动内核的状态而不是临时偏移**：窗内那一档完全不动画面（D1-b），窗外才按 `目标行 − ⌊rows/2⌋` 写 `offset_y` 并钳在 `[0, max_offset]`（D2-a/b）。两条承重事实是 `offset_y` 与 `mirror_.window_top()` **同一个量**（距底＝`max_offset − offset_y`，`window_top`＝总行数 − rows − 距底，两式相减即之），以及写状态才让 `reproject` 在下一帧把新距底保持下去——那才是「回看位置随之真改变」。

**一处真缺陷与一处判据空洞**：① `run_search_scan` 的两个换表出口原先都不标绘制脏，于是高亮只在别的脏源（光标移动、选区、滚动）恰好顺带标脏时才上屏，**纯输入那一帧是干净的**，症状即用户看到的「打了字却什么都没变」；同批 `close_search()` 一并清 `search_dirty_`（打字与排帧之间关掉浮层时，那张已排上的扫描必须在下一帧被撤掉，否则画面亮出一份用户已经关掉的结果）。② 去掉「空串出口」那一处标脏时在册 37 + 7 例**全绿**——没有一例在撤表之后再看像素。按裁决 7.43⑦ 的口径这是**判据空洞**而不是等价注入，处置是补 `clearing_the_query_takes_the_highlight_back_off_the_screen`（先 REQUIRE 高亮确实落上，再删空查询、断该格回到底色且与无查询帧差集为空），**重跑同一变异**后恰该例转红。

**两条判据写法入册**：C1-b（只换底色不换前景笔形）以**竖直有墨跨度相等**为证人而不是比像素（AA 灰度在两侧不同底色上不可逐位比）；C2-a/b 要在块形光标下**放一个整格字形**（U+2588）才把两条判据读成恰好可判，且那条反空转前提的 REQUIRE 按裁决 7.65 的顺序教训排在被检事实**之后**。

**验收**：`utest_cell_layout` 41 → **49 例**、`itest_render_viewport` 32 → **38 例**（新增六例像素证人）、新套件 `tests/integration/itest_search_viewport.cpp` **七例**（跳转落位与关闭残留的行为证人）；**非 e2e 通道 `ctest -E etest_` 43 → 44 项全绿**（40.85 s）。七条变异注入各有指名红行：`background_at` 去选中优先 → 10 例红（含 C1-a）／去游标档 → 3 例红／光标第三段不折命中 → 1 例红／`scroll_row_into_view` 去「可见即返回」→ 本套件 D1-b 那一例加绘制侧三例红／`close_search` 去清脏 → 1 例红而绘制套件 37 例绿／`run_search_scan` 去换表标脏 → 2 例红（A1-a 与 A1-e 的像素证人）／去空串出口标脏 → 见上条②。每次重链前先删 `build/**/*.ilk`，并**在每次构建前 grep 全树确认只剩一个变异标记**——本轮此前吃过一次「上一条变异未还原就跑下一条」的假读数，纪律因此补这一句。未复跑吞吐门禁并给理由：新增的是色带层内的一次二分与光标那一段的一次查表，绘制与布局算式零改动，基准四场景不含浮层交互。

**未落**（任务 #161）：`src/ui/search_overlay.{h,cpp}` 浮层本体与 11 条 `search.*` 词条、`search.open` + `Ctrl+F` 的装配层登记、`Escape` 的登记与解绑、E 面板三档宽度的降级形态——故本条**不宣称搜索「可用」**。**框架面真缺口 0**（三层底色、区间切片、滚动内核写入口全在本仓；浮层锚点按裁决 7.73 取公共入口 `Widget::window_bounds()`，「点外部即关」按裁决 7.41③ / 7.70③ 由本仓在 Press 分支自驱，开关 chip 用 `Button` 选中态底色自绘而**不用**框架 `Switch`——后者禁用档色值硬编码浅色，裁决 7.68②），附录 A.2 维持开放缺口 0。

**落点**：`include/borealis/ui/cell_layout.h`、`src/ui/cell_layout.cpp`、`src/ui/terminal_view.{h,cpp}`、`tests/unit/utest_cell_layout.cpp`、`tests/integration/itest_render_viewport.cpp`、`tests/integration/itest_search_viewport.cpp`（新）；文档回写＝`codespec/SPECIFICATIONS.md` 的 §7 裁决 7.80 与 7.79⑥ 末句就地更正及其 §4.3 该需求句的进度指针、`codespec/UI_SEARCH.draft.md` §0 与 §4 第 1 / 2 / 3 / 4 条（那四处「只收一段 `RowSpan`」「现无程序化写入口」随本棒过期）、`codespec/PLAN.md` 的 `SPEC.FEAT.INTERACT.04` 行与 §8 的 M2 段、`AGENTS.md` §2 与 §6。

## v0.79（2026-10-07）**`SPEC.FEAT.INTERACT.04` 的第三半：视口侧扫描接缝与两档节流的接线（浮层界面腿未落）**（`src/ui/terminal_view.{h,cpp}`、`tests/integration/itest_search_scan.cpp` 新增，判据入册为裁决 **7.79**）

**动机**：v0.78 交出的匹配表是「给一份权威网格与一个查询、回一批存储行序的区间」，而它离界面还差三条接线：7.78⑥ 拍板的**主线程临界区内一次扫**、7.78③ 拍板的**顶边位移折算**，以及 7.78① 那条实测代价（宽匹配式一次 1623.5 ms）要求的**正则档只在 `Enter` 提交时扫**。本条覆盖两棒（接缝 `cc0c96d`、九例集成证人与变异自证 `bb99d64`），因为「节流接成状态还是分支」与「谁当节流的证人」是同一批口径，拆开写就把接线形态与判据成色割成两处。浮层本体（`src/ui/search_overlay.{h,cpp}`）与高亮绘制仍属 #161，本棒刻意不含。

**三条决定形态的口径**：⑴ **扫描调用点在 `Session::read` 那次短临界区内、且排在 `mirror_.apply(...)` 之后**——`ui::search` 吃权威网格（出锁没有第二个一致快照，副本只含视口那几行），而新表的顶边基准 `mirror_.dropped_lines()` 由副本带出，故两者必须前后相接；取早一格的后果不是崩溃而是**同一次位移被算两遍**（既进新表行号、又进同帧折算增量）。⑵ **两档节流接成状态而不是分支**：`set_search_query` 只在字面量档或空文本置脏，正则档只由 `submit_search_query()` 置脏，于是「除这一句以外没有任何路径让正则档扫」成为件的结构保证；`on_frame` 每帧至多结算一次。**空文本是正则档唯一例外**（两档都要在这一帧清掉高亮），而清表**不进**扫描计数——「一轮只扫一次」以计数为据，把清表算进扫描就测成了「有没有人按过键」。⑶ **三份状态各守一句物理事实**：`search_query_`（输入框里的）与 `search_scanned_`（产出当前表的那份）分开存，B5 的「按 Enter 搜索」占位文案由它们的差驱动；`search_invalid_` 把 B4（非法保留旧表与行号）与 B5（不再催一次 `Enter`）拆成两句可断的事实——编译失败时**仍记** `search_scanned_`、只不换表，故「已按过 Enter」与「旧表还在」同时成立，且该标志不粘住下一次合法提交。新表的基准与选区的基准**各记一份**（两份表在不同时刻产生，共用一个读数会把其中一份折算两次）。

**一条判据空洞以变异实跑暴露并按 7.43⑦ 补例，而不是 declare equivalent**：把 ⑴ 的次序反过来（扫描放在 `mirror_.apply` 之前）时在册八例**全绿**——那八例里重扫与「推走顶边的新输出」从不落在同一帧。处置是补 `a_rescan_that_lands_with_new_output_translates_exactly_once`（置脏后不排帧、再喂一行新输出、同一帧结算），并让预期值**独立于实现的滚动次数**：该例取 `scrollback_limit = 0` 的形态，存储里只剩视口那 24 行，最新一行恒在第 23 行，故「折算两次」必然把它读到第 22 行而红（`{22} vs {23}`），补例后其余八例仍绿。教训是**次序类判据须有一例把两个动作压进同一帧**，否则「分处两帧」的写法对它是结构上无证的。

**验收**：新套件 `itest_search_scan` **九例**（新注册一项，非 e2e 通道自此 **43 项**全绿，40.44 s），断的是**视口的调用纪律**而非匹配算式本身（区间形状与双宽整字符由 `utest_search` 逐条覆盖）；九条变异注入各有指名红行（去清脏 → ③⑥⑧ 三例 `{2} vs {1}`／去同值早返回 → 只 ⑧／删正则档节流 → ②③／把计数提到空串判定之前 → ③／失败分支不记 `search_scanned_` → ④／新表不对齐基准 → ⑥ `{5} vs {10}`／跳过折算调用 → ⑥ `{2,15} vs {10}`／节流条件退化成 `!regex` → ③／扫描点前移 → ⑨ `{22} vs {23}`），**无等价注入在册**，每次重链前先删 `build/**/*.ilk`。查询条件命中的内容一律经**真实字节流**投进网格而非注入快照，故测的是接线不是替身行为；本套件不经绘制、不需窗口也不需 `AURORA_BACKEND_HEADLESS`（帧序由 `on_frame()` 手工驱动）。界面腿**不**另配时间断言（AGENTS.md §4.4 第 21 条），200 ms 那条仍在 `tools/bench` 第四场景与门禁 B-8。一处环境读数留痕：首轮全通道 `utest_form_transfer` 单例失败而单独复跑即过（`build/` 与并行 agent 共享，判为产物争用而非回归）。

**未落**（任务 #161）：命中高亮的绘制（`ui::layout_row` 的命中区间入参，且「命中 → 选中」的层序须发生在切 run 之前）、把存储行滚进可见区的**程序化写入口**、`src/ui/search_overlay.{h,cpp}` 与 `search.open` / `Ctrl+F` / `Escape` 的登记与解绑、`advance_search()`、11 条 `search.*` 词条——故本条**不宣称搜索「可用」**。**框架面真缺口 0**（本件三份材料——权威网格、码点文本、正则引擎——全在本仓与标准库），附录 A.2 维持开放缺口 0；未复跑吞吐门禁并给理由（新代码只在有脏帧里扫一次表，绘制与布局算式零改动，基准四场景不含浮层交互）。

**落点**：`src/ui/terminal_view.{h,cpp}`、`tests/integration/itest_search_scan.cpp`（新）；文档回写＝`codespec/SPECIFICATIONS.md` 的 §7 裁决 7.79 与 7.78⑥ 末句就地更正及其 §4.3 该需求句的指针、`codespec/PLAN.md` 的 `SPEC.FEAT.INTERACT.04` 行与 §8 的 M2 段、`AGENTS.md` §2 与 §6。

## v0.78（2026-10-07）**`SPEC.FEAT.INTERACT.04` 终端内搜索的前两半：匹配表纯逻辑件 ＋ 首次搜索时间门禁 ＋ 浮层判据文与九面板视觉稿（界面腿未落）**（`include/borealis/ui/search.h` + `src/ui/search.cpp` 新增、`tests/unit/utest_search.cpp` 新增、`tools/bench/render_throughput.cpp`、`tools/check/perf_baseline.json`、`tools/check/check_perf_gates.ps1`、`codespec/UI_SEARCH.draft.{md,svg,png}` 新增，判据入册为裁决 **7.78**）

**动机**：Ctrl+F 浮层要的三件事（全部匹配高亮、前后跳转、匹配计数）有一个公共前提——「给定查询交回一批整格对齐的区间」，而这条算术此前不在任何件里。留在绘制侧就只能靠真像素反推模型行为，而判据分档、匹配表上限、顶边位移折算三类口径都必须脱开 UI 单测（AGENTS.md §4.4 第 20 条）。需求那句「100,000 行下首次搜索响应 ≤ 200 ms（P95）」是时间判据，本仓一律走 `tools/bench` + `tools/check` 门禁而不入 CTest（裁决 7.23③ / 7.34 同口径）。开工前四条口径已由人拍板（正则引擎选型、执行侧、行坐标空间、性能验收形态，全部取建议项，见裁决 7.78①③⑤⑥），界面腿的画面判据按常驻口径「UI 编写前先出设计图评审」另出判据文与九面板稿。**本条覆盖五棒**（匹配件 `20a315e`、单测 `1664c8e`、基准场景 `5cae9f1`、门禁 B-8 `4f8fd20`、判据文与图 `781956f`），因为它们共用同一批口径，拆开写就把「两档为什么差两个数量级」与「门禁只钉哪一档」割成两处。

**两条由实测而非审美推出的口径**：⑴ **`std::basic_regex<char32_t>` 是未定义类型**（本工具链只为 `char` 与 `wchar_t` 特化 `regex_traits`），故正则档必须先把每行折成 UTF-16 序列（`sizeof(wchar_t) == 2`）。同一份 100,000 行 × 80 列网格、MSVC `/O2` 的四组读数：字面量档宽匹配 **2.1 ms**、正则窄式 **37.2 ms**、正则宽式 **1623.5 ms**，而**纯码点→wchar 转换那一趟就 11.5 ms**。两档差两个数量级，于是 200 ms 那条只钉字面量档——把宽匹配式钉进门禁等于钉一条假的毫秒线，真正守正则档的是「只在 `Enter` 提交与两枚开关翻转时扫」那条节流（7.78⑥）。⑵ **大小写开关在两档都只折 ASCII 字母**（实测 `std::wregex` 带 `icase` 也不互认 `Ä` 与 `ä`），两档对同一查询才给出同一批匹配；非 ASCII 字母的折叠本件**不承诺**，并以一条指名红行钉成判据边界（7.78②）。

**匹配表的四条止损**：区间恒为**整格对齐的闭开区间**（双宽字符要么整字符匹配要么完全不匹配，裁决 7.32② 同口径），匹配文本口径与复制腿同源（跳过延续格、**行尾连续空白填充不算内容**、零宽匹配不产区间）——不剥填充的后果实打实，网格每行定宽而 ` *` 一类式子会在每条终端输出上爆一串无意义高亮，「匹配计数」就没有意义；`kMaxSearchMatches = 10000` **按行判**且到顶即停，故触顶时表长可超上限一格行的量，`truncated` 说的是「外面还有没有没人知道」而 `count()` 恒是**下限**（会话字节流是不可信输入，界面上的兑现形态是 `10000+` 那一个 `+`）；`search()` 回 `std::nullopt` **当且仅当**正则档编译失败，「表达式非法」与「0 个匹配」绝不合并，浮层据此保留上一份结果；`SearchMatches` 构造私有、`search()` 是唯一产出口（`friend`），故「扫描失败而手里握着半成品表」结构上不存在。行坐标沿用**存储行序**，`translate_rows(rows_up)` 与 `ui::translate_selection_rows` 共用同一算式与同一输入（两次 `dropped_lines()` 读数之差），界面腿每帧折算因此**零新增锁**；差别在逐条丢弃与「游标所指那条被顶出即归空」，游标是表内下标而非行号，`advance` 首尾相连（裁决 7.43）。DECSTBM 带内位移那条 **7.39③ 欠项照旧在册、不由本件消除**。

**门禁 B-8 的测量契约**：夹具 `[100028, 86, 1000]`（存储行数 × 列数 × 匹配条数）随读数一并入档并成为**第三把硬判**（与 `build_config` / `grid` 同族）——扫描成本随行数与列宽线性变，而匹配条数决定会不会提前停止，**触顶那次反而更快**，故夹具漂移能把一次回归读成一次提速。三处刻意的夹具形态：needle 取含数字的字面量且每 100 行嵌一枚（正文只有 a-z 伪随机字母，结构上产不出它）；1,000 条刻意落在上限以下；每行**留最后一格不写**（行尾填充正是 `content_right` 每行都要折回跳过的那段，填满就把这一格的成本抹掉了）。九次**冷扫、不热身**——「首次」本身就是被测量。三次独立进程 p95 **33.499 / 34.237 / 32.533 ms**（峰谷跨度 5.2%、逆向侧 2.2%），绝对线 200.0 **与** 相对线 10% 两条都判（捕获期抖动窄，故不必像 B-2 / B-6 那样关相对那条）；`--search-runs=1` 的对照三次 **32.093 / 33.197 / 31.351**，与九次档均值差 2%，故聚合并没把首触成本摊平掉——「九次冷扫会不会摊平」是实测校验而非假设。均值档与轮数档不入门禁（同一信号锁两次）。基准侧**不走会话**：被测主体是权威网格上的一次全量扫描，加一把互斥锁只会把锁的开销混进读数，生产形态（主线程临界区内一次扫）由界面腿的集成用例守。

**验收**：`utest_search` **25 例**（新注册一项，非 e2e 通道自此 **42 项全绿**，本轮复跑 40.70 s），**十三条变异注入各有指名红行**，每次重链前先删 `build/**/*.ilk`；**两条无红行按裁决 7.49⑥ 如实登记而不伪造证人**——去掉 `rows_up == 0` 早返回是**等价注入**（25 例全绿，该句价值在界面腿每帧调用的成本而非行为判据），去掉零宽匹配那一判据**不是红行而是整进程 fastfail**（`last = position + length - 1` 在无匹配长度时下溢，被 Debug 的 `_ITERATOR_DEBUG_LEVEL=2` 当越界读抓住，与裁决 7.72⑧ 那条「越界读把读数变成一个退出码」同族），登记它是要让下一棒把这种退出码**读成抓到了**而不是读成测试挂了。门禁侧另六条变异自证非空转（p95=250 红在绝对线、p95=40 红在**相对**线、`search_fixture` 改触顶档红在契约、缺指标红在名漂移、缺 `search_fixture` 红在二进制陈旧、基线缺该键红在重捕获），未变异样本 **8/8 PASS**；按 §4.4 第 22 条门禁脚本不为其单配单元测试。

**判据文自拍九处**：`codespec/UI_SEARCH.draft.md` 的 **D1~D8 全部取建议项**并逐条写清代价（沿用裁决 7.38① / 7.47 / 7.52 先例），九面板以 SVG 入库为事实来源（展开态两态 / 计数六态 / 相撞三态 / 跳转两态 / 宽度三档 / 判据清单十八条），其中「最小合法 pane 228 dp ≥ 200 dp 故没有第四档」是一条能被算出来的边界。**未落三项**：⑴ **浮层界面腿**（`src/ui/search_overlay.{h,cpp}`＝第四个触达 `au::Painter` / `au::Widget` 的 TU，私有头形态同裁决 D1①；连同视口侧的持有与开关、`search.open` + `Ctrl+F` 的注册与 `Escape` 的登记**和解绑**、D2/D6 需要的「把存储行 X 滚进可见窗且尽量居中」的**程序化写入口**——现无）；⑵ **绘制侧入参形态**（`ui::layout_row` 今天只收一段选中区间，而命中是每行 0~N 段且两档强度，三档底色会在一条边界上多切一刀 run，故层序「命中 → 选中」必须发生在切 run 之前）；⑶ **真机走查照旧未做**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①）——chip 符号是否认得出、`↑↓` 图标档可辨性、居中滚动跟手、`Ctrl+F` 被快捷键层截走后 vim 用户的接受度、极窄 pane 上无跳转按钮，故本条**不宣称搜索「可用」**。**框架面真缺口 0**（按裁决 7.13① 逐条读到 Aurora 当日活动分支的公共头与实现体：本件三份材料——权威网格、码点文本、正则引擎——全在本仓与标准库，无一处调用 Aurora API），故不出任务书，附录 A.2 维持开放缺口 0。

**一句 `scrollback` 上限的实测补注**：`grid::kMaxScrollbackLimit = 100000` 正是需求那句 200 ms 的档位，基准因此灌注 `kMaxScrollbackLimit + 视口行 + 8` 行以保顶边**真的钳在上限**而非差几行。

**落点**：`include/borealis/ui/search.h`、`src/ui/search.cpp`、`src/CMakeLists.txt`、`tests/unit/utest_search.cpp`、`tools/bench/render_throughput.cpp`、`tools/check/perf_baseline.json`、`tools/check/check_perf_gates.ps1`、`codespec/UI_SEARCH.draft.{md,svg,png}`；文档回写＝`codespec/SPECIFICATIONS.md` 的 `SPEC.FEAT.INTERACT.04` 指针与 §7 的 7.78、`codespec/PLAN.md` 的 `SPEC.FEAT.INTERACT.04` 行与 §8 的 M2 未交付段、`AGENTS.md` §2 与 §6。

## v0.77（2026-10-07）**`SPEC.FEAT.TERM.06` 鼠标上报全链落地：编码件 + 模式登记 + 视口分流（`TODO(SPEC.FEAT.TERM.06)` 就此消除）**（`include/borealis/term/mouse.h` + `src/term/mouse.cpp` 新增、`src/ui/terminal_view.{h,cpp}`、`tests/integration/itest_mouse_report.cpp` 新增，判据入册为裁决 **7.77**）

**动机**：`?9` / `?1000` / `?1002` / `?1003` / `?1006` / `?1007` 六条私有模式此前既不被状态机登记、也没有编码表，而视口的 `on_scroll` 挂着 `TODO(SPEC.FEAT.TERM.06)`——远端程序（`vim` / `htop` 的滚轮与点击、`less` / `more` 的翻页）向本仓要上报能力时什么都收不到。本条覆盖三棒（编码件 `c9e715d`、单测 `ee697c2`、界面腿与集成用例本棒），因为它们共用同一批口径，拆开写就会把「层级怎么推」与「谁让位给谁」割成两处。**开工前按裁决 7.13① 逐条读到 Aurora 当日活动分支实现体：真缺口 0**（`MouseEvent::modifiers`＝G18、`ScrollEvent::modifiers`、`Widget::window_bounds()`＝G36 与真实派发链路全在公共 API 上），故不出任务书，附录 A.2 维持开放缺口 0。

**编码件的三条口径**（`include/borealis/term/mouse.h` + `src/term/mouse.cpp`）：⑴ **上报档位是层级而不是并列开关**——四档取最高生效，模式表因此只存六个独立 bool，「当前哪一档」由 `term::mouse_tracking()` **读时推得**；DECSET/DECRST 是逐条独立到来的，把层级写进模式表就成了第二份真值源。`?1006`(SGR) 与四档正交，只换编码形态。⑵ **两条清档级联照 xterm**：`?9 h` 清掉高一档三位（低档 setup 即回到「只报按下」），`?1000 l` 清掉 `?1002` / `?1003`（程序退出普遍只补一条 `?1000 l`，不级联就等于上报没关：滚轮继续发 64/65 而本地回看接不回来）；**反向不级联**（`?1002 l` 不动 `?1000`）。⑶ **右键不进编码表、Shift 位不进编码**：前者因为本仓把右键留给本地三态（裁决 7.77②），留一格永不命中的按钮 2 就是人工制品；后者因为 Shift 是「让位本地」的覆盖键（7.77①），与 xterm 及业界终端同口径。遗留档坐标钳到 223（`CSI M` 的行列各占一字节且要 +32），`?1005` / `?1015` 不在需求原文四档内故不登记；横向滚轮（66/67）不建模，因视口不消费 `ScrollEvent::delta_x`。

**界面腿的四处让位判据**（`src/ui/terminal_view.{h,cpp}`）：`takes_pointer_by_report()` 按序短路——**右键与中键永不进上报**；已经在途的按下决定**整笔手势**的归属（按住左键后中途松开 Shift 不该把一笔拖拽切成两半，反向同理）；层级关着或按着 Shift 一律让位本地；新的按下在此之后即归上报；没有在途按下的移动只在 `?1003` 归上报，而**抬起在任何档都不归**（那可能是 Shift 覆盖期间的本地按下松了手，补一条孤儿松开会让远端以为某个键还按着，`vim` 因此停在 visual 选择态）。滚轮是**四档短路**：`Ctrl` 字号缩放 → 鼠标上报 → 备屏 alternate scroll → 本地回看，任一档吃掉即置 `e.remaining_y = 0` 并认领。运动上报**按格子去重**（一帧多次 `Move` 会让 `vim` 的视觉滚动收到一串同格事件），按下与松开不去重。**分流判据不得用 `encode_mouse()` 的空值**：该空值同时表达「层级不够」与「该事件在本档不报」，拿它分流会把后者误读成前者（`?1000` 档的一次 `Move` 因此掉回本地去推选区）；同理**认领事件与「本档真的发了字节」是两件事**。

**两处只有读框架实现体才现形的物理事实**：⑴ **四后端都把 `MouseAction::Move` 的按键盖成 `MouseButton::Left`**（属既有语义而非缺口），故「无键悬停」（`?1003`）与「按住拖动」（`?1002`）的区别只能由本层自己的在途态 `reported_press_` 区分——照事件按键编码就把悬停报成拖动（少 `0x20` 那一位）。首轮写法（非 Press 且无在途按压一律判本地）因此让 `?1003` 在结构上退化成 `?1002`，自查 `any_events_level_reports_buttonless_hover_motion` 那一例时现形并已改。⑵ **`ScrollEvent` 只有窗口坐标**：派发器给滚轮事件的入口**不平移** `e.position`（与 `MouseEvent::local_position` 不同源），故落点折回格子须经 `Widget::window_bounds()` 现算自身窗口盒再减，再交与选区同一个换算件 `ui::cell_at_point`；上报协议吃的是**可见区**行列而非选区用的存储行序（远端不知道本地滚到了哪一屏）。`?1007` 的缺省是**开**（照 xterm 资源缺省），否则 `less`/`more` 里滚轮一条都不发——它们不开上报档也不发 `?1007 h`，对「终端默认给方向键」的依赖正是该缺省的前提；翻页形态经 `term::encode_key` 取，故 `DECCKM` 的 CSI/SS3 分流与真实方向键同源而不另写一串字节。

**一条设计改口**：模式快照**按事件现取**（`modes_snapshot()`），不在 `on_frame` 里缓存。首轮写的是「帧里刷一份 `modes_` 成员」，根据不成立——`on_scroll` 与 `on_pointer_event` 都发生在帧之外，缓存就会用到上一帧的档位；而既有的 `on_key_event` 本来就每次按键现取。改回同一形态后**没有新增任何成员**。

**一条判据空洞按 7.43⑦ 补而不判成「等价」**：变异「松开不清在途按下」（去掉 `reported_press_ = std::nullopt;`）在十六条用例下**全绿**，初看像 7.49⑥ 那种等价注入，实为需求那句「上报模式与本地选择交互自动切换」**没有证人**——没有任何用例在同一驱动台上先走一笔整笔归上报的手势、再走一笔 Shift 本地手势，于是粘住的在途态让本地选区**永远回不来**（真实缺陷，不是测不到的无害改动）。补 `a_local_selection_recovers_after_a_fully_reported_gesture` 后该变异转红且**只有它**转红（读数 16 绿 1 红 → 补例后 1 红 → 还原 17 绿）。教训：等价判定须先问「这条判据要守的行为有没有一条用例把它和相邻状态区分开」，而不是先看红绿。

**验收**：`utest_mouse` **十一例**（四档各自报什么、取最高档、四档全关不编码含 `?1006` 单开亦不报、`Ctrl`/`Alt` 的 +16/+8、滚轮 64/65、遗留档 1-based 与 223 钳位、SGR 档保留按键编号与终止字节 `m` 且坐标无上界）、`utest_terminal` +3 例（现 42 例：六条模式各自落位、`?9 h` 清高档、`?1000 l` 级联且反向不级联）、新套件 `itest_mouse_report` **十七例**（真指针与真滚轮派发 + 无头窗口：`?1000` 报按下松开而永不报移动、X10 只报按下、`?1002` 拖动位、`?1003` 无键悬停、按格去重、SGR 形态逐字节、Shift 两向覆盖与整笔不半切、上报与本地选区的来回、右键在上报档下仍走本地三态、滚轮每档一条且不滚本地、缩放撞界让位给上报、无上报档时照旧回看、备屏 `?1007` 翻页随 `DECCKM` 经真实状态机、`?1007 l` 之后滚轮归本地）。非 e2e 通道 `ctest -E etest_` **41 项全绿**（新经 `CONFIGURE_DEPENDS` 自动注册 1 项）；九条变异注入各有转红证人（去悬停腿 → 2 例、去在途按下腿 → 4 例、去 Shift 腿 → 2 例、右键进上报 → 1 例、去去重 → 1 例、无档也发滚轮 → 3 例、去备屏翻页腿 → 2 例、一档只发一条 → 1 例、松开不清在途 → 见上一条），每次重链前先删 `build/**/*.ilk`（`/INCREMENTAL` 会让 exe 链进陈旧代码而假绿），变异已全量还原。**未复跑吞吐门禁并给理由**：新代码只在指针与滚轮事件里发字节，绘制与布局算式零改动，而基准三场景（逐行滚动 / 强制整屏重绘 / `cat` 灌注）不含任何指针与滚轮事件。**真机走查未做**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①）：`vim`/`htop` 内滚轮与点击的手感、`less`/`more` 翻页是否跟手、Shift 覆盖的肌肉记忆三处只能人工判，故**不宣称「可用」**。**落点**：`include/borealis/term/mouse.h`、`src/term/mouse.cpp`、`include/borealis/term/terminal.h`、`src/term/terminal.cpp`、`src/CMakeLists.txt`、`src/ui/terminal_view.{h,cpp}`、`tests/{unit/utest_mouse.cpp,unit/utest_terminal.cpp,integration/itest_mouse_report.cpp}`；文档回写＝`codespec/SPECIFICATIONS.md` 的 `SPEC.FEAT.TERM.06` 指针与 §7 的 7.77、`codespec/ARCHITECTURE.md` §9.5 那句「尚未接入的滚轮转发」就地更正、`codespec/RENDER_VIEWPORT.draft.md` 四处「本棒不做 / TODO 仍在」就地更正、`codespec/PLAN.md` 的 `SPEC.FEAT.TERM.06` 行、`AGENTS.md` §2 与 §6。

## v0.76（2026-10-06）**#117：三条会话侧构造期注入接缝 + 启动降级对话框（两处 TODO 就此消除）**（`src/ui/startup_notice.{h,cpp}` 新增 + `src/main.cpp` + `include/borealis/term/terminal.h` + `tests/integration/itest_startup_notice.cpp` 新增，判据入册为裁决 **7.76**，`SPEC.FEAT.PREF.02` 的功能面至此齐备）

**动机**：`codespec/UI_SETTINGS.draft.md` §7 那两处 TODO 是面板交付面最后的两块——`include/borealis/config/store.h` 的 `TODO(SPEC.FEAT.PREF.07)`（「绝不静默清空」要求的降级对话框）与 `src/main.cpp` 的 `TODO(SPEC.FEAT.PREF.02)`（三条会话侧注入接缝）。开工前按常驻口径已于裁决 **7.68** 完成框架依赖梳理（真缺口 0，三条限制均属公共 API 可组合），本棒据此**不等不绕、不出任务书**；同批 G36 已随 Aurora `416b27bc` 回货，附录 A.2 的开放缺口维持 **0**（G1 ~ G36 全部闭合）。

**腿①：三条注入落成一份聚合体而不是一条接缝三个形参**。`term::TerminalDefaults{cursor_shape, cursor_blinking, ambiguous_width}` 作为 `Terminal` / `Session` 构造的**带缺省值尾形参**——缺省即库自己的缺省档，故既有构造点与既有用例一字不改；而三条的取值域与生效档位是同一句话（运行期改动不重放既有会话），拆成三个入参就让「漏传一条」成为静默行为。装配层新增 `make_terminal_defaults()`，与 `make_appearance()` 同档「一处折算、两处复用」。**RIS 语义是本腿的判据本体**：`ESC c` 恢复到的是**本会话的初始档**而不是库的硬编码缺省（一次 `reset` 不该静默抹掉用户配置），用例因此写成三段式「注入互异档 → 远端 `CSI 5 q` 与 `set_ambiguous_width` 改当前档 → RIS 回到注入档」——只断末句的实现（RIS 干脆不动 `modes_`）也能全绿，中间那句改档是承重前提。`set_ambiguous_width` 的头注随之补「改**当前**口径、不写回 `TerminalDefaults`、不回头重排已上屏格宽」，这正是该键在面板上挂「下次会话生效」而非「即时」的根据。反向核对表三行改判 `Wired ∧ NextSession`，而 `SeamPending` **档位保留、当前行集归零**（判据断空集而非删列：这一档结构上会继续出现，删档就让下一棒无处登记）。

**腿①的预览腿：初始档与外观包成对，缺任一即整条横条不画**。`Hooks::preview_defaults` 与 `preview_appearance` 并列成对，于是「装了外观而漏了初始档」结构上不成立；两条不合成一条，因为外观包恰是**运行期可换**的那一份，构造期注入混进去就成了「看起来能改、其实只在建会话时吃」的假档位。承重的物理事实：`Bar` / `Underline` 两档在 `paint_cursor` 里**先于**失焦降级落笔，而预览恒是失焦态——不注入就在同一屏画出与主视口不同的两种光标。`settings_preview.h` 头注那句「预览里的光标恒是失焦的空心形态」据此就地更正为**只对块形成立**。观测面新增 `SettingsPanel::preview_modes()` → `SettingsPreview::modes()`（取状态机现值而非面板另存的一份档，故漏装与装错都抓得到；非 `const`，因 `Session::read` 要消费行脏标记）。

**腿②：`au::Dialog` + 本仓自建 content（判据文 §7 按 7.68④ 改回原拍形态）**。`LoadReport` 补齐两处结构化内容：`stored_schema_version`（版本过高那一路文件自报的那一档——只报本程序支持的档位就说不清差了多少）与 `writes_refused`（备份未成功即拒绝写、唯一现场原样保留；界面上必须说出这一点，否则用户以为改动能存住）。`kSupportedSchemaVersion` 随之从 `store.cpp` 的私有常量**上公共头**——「支持哪一档」要出现在界面上，让 UI 自抄一份就是第二真值源。四条决定形态的 WHY 全写进 `startup_notice.h` 的文件头：⑴ 收整份 `LoadReport` 而非散字段（`src/main.cpp` 没有测试面，「哪两态弹」的分支落在本件才可能被集成用例真判）；⑵ 不用 `alert` / `confirm`（按钮标签是字面量、标题正文收 `std::string`，整条链无 i18n 入口，与 S13① 相违）；⑶ `FocusManager` 由装配层注入且 `show()` 后本件自补一次 `push_scope`——装载不在派发栈内，`show()` 只走「仅置位」分支，而框架那条 `resolve_focus_manager(Widget &)` 会**惰性造按根缓存的兜底实例**，压进它就等于压进一个没人读的栈；⑷ `message` **不上界面**（ASCII 英文诊断上中文界面即违 AGENTS.md §4.3 第 14 条），故 `show_if_needed()` 回布尔、由调用方决定是否写日志，「哪两态弹」因此只有一个判定点。`rejected_keys` 那一段按装载侧现状**结构上到不了**（两条降级路都在填该表之前 return），仍照 S13① 落实现并由用例手工构造的 `LoadReport` 驱动。

**本棒最该留痕的两条观测面事实**：⑴ `OverlayHost::overlay_count()` **不含基础内容**（Aurora `popup.h:345` 的 `children_.empty() ? 0 : size() - 1`），故「弹了一层对话框」的读数是 `1` 而不是 `2`——首跑因此**五例同红**，处置是读实该算式然后把七处期望各减 1，而不是给宿主再补一个基础子节点。⑵ 在「知道了」回调里 `remove_overlay`（派发栈内销毁正在派发的子树）的变异注入**未崩**，只让浮层计数那一例转红；据此裁决 7.69 在册的「派发栈内销毁自身」那句担忧的根据改写为**框架自陈而非本仓抓到的现场**（与 7.70④ 同口径，本件照旧不在回调里摘浮层）。析构时序按裁决 **7.67**：先还焦点作用域 → 再放本件自持的对话框句柄 → 最后 `remove_overlay`。

**两条无证人（按裁决 7.49⑥ 如实登记而不伪造绿灯）**：⑴ 「遮罩吸收点击」是框架 `Dialog::on_hit_test_chain` 的行为，本仓没有注入点，用例只能断该事实；⑵ `FocusManager::pop_scope()` 有 `scopes_.empty()` 早返回守卫（Aurora `focus.cpp:149-158`），故「去掉 `set_on_close` 清标记」是**等价变异**，其代价（多弹一次空栈 → WARN 行数）本仓无断言日志文本的设施（与 7.67 同档）。

**验收**：`utest_terminal` ＋**两例**（播种与复位回注入档）、`utest_settings_catalog` 的档位例按上述改判、`itest_settings_panel` 48 → **49 例**、新套件 `itest_startup_notice` **10 例**（二例纯模型断言 + 八例须 `AURORA_BACKEND_HEADLESS`），非 e2e 通道 `ctest -E etest_` 38 → **39 项全绿**（41.34 s，新经 `CONFIGURE_DEPENDS` 自动注册）。十二次变异注入各有转红证人，关键读数：`should_show` 恒真 → `1 vs 0`／去幂等闸 → 浮层计数 `2 vs 1`／漏 `writes_refused` 行 → `3 vs 4`／漏备份行与版本号交空 → 备份例红／只画前 3 个回落键 → `88 vs 160`／去掉列表 `.height(160)` 锁高 → `315 vs 160`（视口＝内容高，故锁高是真证人而非排版）／回调里 `remove_overlay` → `0 vs 1` 且未崩／去自补 `push_scope` → **两例**红／析构不摘浮层 → `1 vs 0`／把 `message` 画上界面 → 诊断串例红／去框底色 → 像素例 `delta 0 vs 8`。每次重链前删 `build/**/*.ilk`，变异已全量还原。**未复跑吞吐门禁并给理由**：本件只在启动路径与建会话那一刻生效，不进每帧绘制与布局路径，基准三场景不含对话框。**真机走查未做**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），降级对话框的呈现与「知道了」的手感归人工，故本条不宣称面板「可用」。`SPEC.FEAT.PREF.02` 的功能面至此齐备，余下只有真机走查（#130 面板行区那一腿）与 §7 余下的会话侧注入接缝。**落点**：`include/borealis/term/terminal.h`、`src/term/terminal.cpp`、`include/borealis/session/session.h`、`src/session/session.cpp`、`include/borealis/config/store.h`、`src/config/store.cpp`、`src/main.cpp`、`src/ui/startup_notice.{h,cpp}`、`src/ui/settings_panel.{h,cpp}`、`src/ui/settings_preview.{h,cpp}`、`src/ui/settings_i18n.cpp`、`include/borealis/ui/settings_catalog.h`、`src/ui/settings_catalog.cpp`、`src/CMakeLists.txt`、`tests/`（四份）；文档回写＝`codespec/SPECIFICATIONS.md` §7 的 7.76、`codespec/UI_SETTINGS.draft.md` §2 三行与 §6 第 13 条及其 §7 两处 TODO 结项与 §8 现状段、`codespec/ARCHITECTURE.md` §9.2 那句「对话框落点 TODO」的就地更正、`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.02` 行、`AGENTS.md` §2 与 §6。

## v0.75（2026-10-06）**#116 第三棒：快捷键只读表的界面腿、装配层行源与验收**（`src/ui/settings_panel.{h,cpp}` + `src/ui/settings_i18n.cpp` + `src/main.cpp` + `tests/integration/itest_settings_panel.cpp`，判据入册为裁决 **7.75**，#116 三件至此齐备）

**动机**：裁决 **7.72** 那条纯逻辑件（`ui::build_shortcut_rows()`，已提交 `ce19bfc`）留下界面腿 #148 / #149。本棒把 7.72 的三条拍板口径接到界面上：面板开 `Hooks::commands` 接缝、`build_shortcuts_section()` 自绘四列、装配层从框架 `CommandRegistry::all()` 折算行源，并以三条集成用例＋七次变异自证结项。**§4 的 A1 ~ A5 与 D 页五处专用区段至此全部落完**，`SPEC.FEAT.PREF.02` 余下只有 #117（降级对话框 + 三条会话侧构造期注入接缝）与真机走查。

**接线形态三条**：⑴ 面板的 `Hooks::commands` 与 `themes` / `families` 走同一条「打开现场取快照」口径，`build_shortcuts_section()` **只把纯逻辑件的结论逐行落笔**——行源、三列取数与冲突比对一律不在面板侧重算，判据 D2-a 那句「标注来自实际比对」的根据正是这个分工（面板拿不到键位语义值时它自己也无从比）。⑵ 装配层的 `key_press_of()`（`src/main.cpp`）是全仓**框架 `KeyCombo` → `term::KeyPress` 的唯一互转点**：`sym` 与四个可按位逐位取，锁定态照搬而不屏蔽（屏蔽归比对件，与框架 `KeyCombo::matches` 在 G25 回货后的口径逐位一致，裁决 7.51①）。⑶ 一条**承重同源约束**：「当前组合键」那一格的显示串（框架 `KeyCombo::to_string()`）与送进比对的那个 `KeyCombo` 必须**同行取自同一份**；分两次取会让界面上的串与比掉的键位不再同源，D2-a 就成了空文。用例侧以「文本相同而键位差一档 ⇒ 不标注」「文本不同而键位相同 ⇒ 互标」两对行把它变成可抓的红。表体沿用裁决 7.68① 的自绘先例（主题卡 / 色板 / 回退链已是三次），**不用** `data_widgets.h` 那三件 `on_paint` 硬编码浅色的表控件。

**只读的兑现形态随之升级**：由本体棒的「只读摘要占位行」升级为「**表体已画出而没有一个提交入口**」——`ReadOnlyTable` 在 `is_editable()` 仍判假，而四列已进树且全是 `Text`（无可点节点），故真点表体不落盘、不广播、不变脏；这一条按**真实派发落点**守（落点由派发链扫出，最深节点须是 `Text` 且祖先含行区 `Scroll`，扫不到即 `REQUIRE` 转红而不静默放宽）。延后档位按 **7.68③** 通则一律可用而只落盘，本区段因此无灰置档，延后只由组顶说明一处文字表达（新词条 `settings.note.shortcuts`，措辞点名「键位重绑尚未开工」）。

**本棒最该留痕的一条：两处 `clear()` 互冗余，等价变异要靠补断言才长出鉴别力**。行投影的清理在册有三处（`close()`、`rebuild_overlay()`、区段开头），后两处**单删任一处都是等价注入**（首轮 48 例全绿）。按裁决 7.49⑥ 的口径这不伪造证人，但「等价」本身是个可判的事实缺口：补一条「换页离开再回来仍是同一批行」的往返断言之后，**两处同删**才红在新加那一行（读数 `12 vs 6`）。根据与裁决 7.61 在主题卡那一区撞过的同一条——卡片是 `LayoutBuilder`，闭包在每次布局都会重跑，不清就让观测面报出界面上并不存在的行。

**验收**：`itest_settings_panel` 45 → **48 例**（headless 27 → 30；三例均须 `AURORA_BACKEND_HEADLESS`，因行投影只在布局闭包里填，没有真实布局帧就恒空），非 e2e 通道 `ctest -E etest_` **38 项全绿**（30.21 s）。七次变异注入的读数＝覆盖表不接线 → 只行源例红／不传保留位 → 只比对例红／标注引用命令 id 而非动作名 → 只比对例红／区段不投影 → 三例同红（45 绿）／单删一处 clear 等价／两处同删 → 行源例红在往返那一行；每次重链前删 `build/**/*.ilk`，变异已全量还原。**「表体进了树」不新开观测点**，改测行区自然高增量（`accessibility_scroll()->content` 的差分，3 行 × (24 + 6) dp，容差 1 dp，测试侧常量另立字面量），且不复述框架算式（§5 第 2 条）。**判据边界如实登记两处**：标注里两档并列的分隔符、以及四列的**列宽与可读性**都无像素证人，归真机走查欠账。未复跑吞吐门禁并给理由：新代码只在面板打开与点该按钮时生效，不进每帧绘制与布局路径，基准三场景不含面板。真机走查照旧未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。附录 A.2 的开放缺口维持 **0**（G1 ~ G36 全部闭合），本棒零框架依赖。**落点**：`src/ui/settings_panel.{h,cpp}`、`src/ui/settings_i18n.cpp`、`src/main.cpp`、`tests/integration/itest_settings_panel.cpp`；文档回写＝`codespec/SPECIFICATIONS.md` §7 的 7.75 与 7.72（标题行及 ⑨ 就地更正）、`codespec/UI_SETTINGS.draft.md` §2 快捷键域表行与 §4 D3-a 的就地更正及其 §8 两条现状、`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.02` 行、`AGENTS.md` §2 计数与 §6。

## v0.74（2026-10-06）**接 G36 回货：A4 字体族浮层为绕开它而写的两处过渡形态按预诺撤除，锚点改取框架公共入口 `Widget::window_bounds()`**（判据入册为裁决 **7.73**）

**动机**：G36 是 #114 A4 本体棒实测登记的框架公共 API 缺口（裁决 7.70③ 派发，任务书随裁决在会话内产出），人已明示「G36 已完成，请更新代码」。本棒是**接货复验**而不是新功能：Aurora 当日活动分支 `416b27bc` 到货后，本仓此前为绕开该缺口而写的两处过渡形态**必须撤除**——预诺原文在册（G36 行末与裁决 7.70③）：「`AnchorButton` 自记同一次 Press 的坐标对 ＋ 面板自持 `row_area_` 现读 `offset_y()` 折算，回货即撤这两处，与 G32 的补偿同口径」。

**回货形态**（读其公共头与 `widget.cpp` 实现体而得，不是只读声明面）：新增 `Widget::window_bounds() const -> std::optional<Rect>`，**查询时**沿 `layout_parent_` 现算、恒窗口逻辑 dp、不读任何绘制期缓存；新增父侧虚钩子 `Widget::scroll_content_offset(cb.origin, out)`（缺省零，**只有 `Scroll` 覆写**为 `{0, -offset_y_}`，`LazyList` / `LazyRow` / `GridView` 的偏移已参与子布局故保持缺省）——「谁提供偏移修正」由此是容器的显式声明而不是基类按类型猜测，而 `buffer_origin_y_` **刻意不参与**（缓冲录制锚点，扣它就会二次偏移）。递推式 `origin = origin + cb.origin + scroll_delta + tf.translation` 与 `Container::on_paint` 的下降式逐字同构而方向相反，`tf` 按父实际用于绘制的盒尺寸 `cb.size` 重算（与 G31 那条 `content_origin` 同源）；几何权威取父侧 `Node::bounds_` 而非控件自报的 `size_`；负守卫一律 `nullopt`（`show` 为假 / 从未测量 / 按地址在 `child_nodes()` 找不到该子），为此新增 `has_measured_` 一个 bool 而不是几何字段，以区分「从未布局」与「布局过但零尺寸」。`paint_bounds()` / `paint()` 形参 / `paint_bounds_` / `focus_bounds()` 四处注释随实修改口（回货判据③ 兑现）。

**本棒最该留痕的一条：回货判据② 被框架自己实测否证并改口**（裁决 7.73②）。登记时写的「该入口与派发链 `HitNode.origin` 同源」在该仓落地时不成立——`Scroll` 后代上的 `HitNode.origin` **本身不是窗口坐标**（＝视口原点 + 内容盒原点，未扣 `offset_y_`），其文档对照读数「真窗口位 60 / `window_bounds()` 60 / `HitNode.origin.y` 260」，故「逐位等于 `HitNode.origin`」与「语义为窗口绝对盒」两条**互斥**，判据改成「逐位等于独立复算的真窗口位」。顺带查明 `HitNode.origin` 缺 `offset_y_` 是一条**活的派发缺陷**（滚动容器内控件的 `local_position` 整体多一个 `offset_y_`），已记进该仓 `05-event-navigation.md` 待另开提案而本次不动。**对本仓判据写法的直接后果**：出账证人只能以**派发落点**（`e.position` 本身即窗口坐标）为基准，即既有的 `Harness::reachable_box()`；而旧过渡形态吃的恰好是这条缺陷路径（`position − local_position` 落在内容坐标系），撤除后本仓不再依赖它。

**撤除点两处、零新增算式**：`src/ui/settings_panel.cpp` 的私有 `AnchorButton` 子类**整段退役**（全仓无残留），`SettingsPanel` 自持行区 `Scroll` 句柄的 `row_area_` 成员与其赋值一并删除；锚点改为一行取 `anchor->window_bounds()`、配 `size().height` 得下沿，查不到有效窗口盒（未测量 / `show` 为假）就**不弹**——宁可不弹也不猜位置。实现侧净减 **47 行**，本仓自此不持有任何「控件 → 窗口坐标」的私有折算，也不再把 `scroll.h` 那条几何契约复制进应用侧（§5 第 2 条）。

**验收**：既有那条真点锚点例 `clicking_the_font_trigger_anchors_a_popup_at_that_press_below_the_button`（用例名保留在册引用不改，以免裁决 7.70 的引用成死链）**判据未动而照旧绿**，即新入口与过渡形态给出同一落点；该例补两句出账证人——⑴ `window_bounds()` 与该控件的派发可达框逐位相符（容差 1 dp＝采样步长），⑵ 同帧同一控件的 `paint_bounds().origin.y` 与窗口框**必然不相当**（差值 > 1 dp，既挡住「锚点改回 `paint_bounds()`」那一类回退，也钉住本例吃的确实是新入口）。**两次变异各抓一处**：把本仓实现退回 `paint_bounds()` ⇒ 两例红；在 Aurora 树临时去掉递推里的 `scroll_content_offset` 修正 ⇒ 同样两例红（读数 `826 vs 442`、`864 vs 481`，第三条用例红在「no font candidate is dispatch-reachable」）。全量构建通过，`itest_settings_panel` **45 例全绿**、非 e2e 通道 `ctest -E etest_` **38 项全绿**（30.06 s）；每次重链前 `find build -name '*.ilk' -delete`。变异已全量还原，该树 `git status --porcelain` 为空、HEAD 仍 `416b27bc`，**本仓不提交 Aurora 任何改动**。**未复跑吞吐门禁**并给理由：新入口只在面板打开与点该按钮时被查询一次，不在每帧绘制与布局路径上，基准三场景不含面板。**G36 出账后 `SPECIFICATIONS.md` 附录 A.2 的开放缺口为 0**（G1 ~ G36 全部闭合）。真机走查照旧未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），字体族浮层的选档手感仍在欠账清单上，故本条不宣称面板「可用」。

**落点**：`src/ui/settings_panel.{h,cpp}`、`tests/integration/itest_settings_panel.cpp`；文档回写＝`codespec/SPECIFICATIONS.md` §7 的 7.73 与 7.70③ 的闭合句、附录 A.2 的 G36 行结项（含「回货结论」续行），`codespec/UI_SETTINGS.draft.md` §4 A4-e 与 §6 标题行、§8 现状段，`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.02` 行与 §6 缺口表，`AGENTS.md` §6。

## v0.73（2026-10-06）**#116 第二棒：快捷键只读表的行源与冲突比对落成纯逻辑件**（`include/borealis/ui/shortcuts_table.h` + `src/ui/shortcuts_table.cpp`，人已拍板的三条口径同批入册，判据入册为裁决 **7.72**）

**动机**：`SPEC.FEAT.PREF.02` 的 D 页（快捷键页）首版**只读**（裁决 7.52 的 S9：重绑需要键位录制件与 `shortcuts.overrides` 的消费方，两者都不在本件射程内）。开工前人已在 AskUserQuestion 上拍板三条口径（行源、孤儿行、冲突比对的两半），本棒把那句话落成可单测的纯逻辑件，界面腿留后续（#148 / #149）。

**三条拍板口径与它们的根据**：⑴ **行源是注册表而不是覆盖表**——`CommandRegistry::all()` 逐字交进来（装配层折成 `ShortcutCommandEntry`），覆盖表只贡献**孤儿行**（配置里写了命令 id 而注册表查无此命令）。两条实测事实决定了不能反过来：框架 `bind_shortcuts` 逐字登记注册表的 `default_binding`，而 `ShortcutBinding` **不带命令 id**，故那张表结构上无法把「哪条命令被覆盖成什么」投影回命令行；且覆盖表当下**无消费方**（裁决 7.27② 同源），把一条未生效的覆盖画成一行就是谎报生效。已注册命令的覆盖条目因此**既不产生行也不改写那一行**。⑵ **冲突比对吃 `term::KeyPress` 而不是文本**——`KeyCombo::to_string()` 只用于显示，公共面**没有**它的反向解析（`key_name` 是单向表），在界面上比字符串就必须自造一张键名反查表（第二真值源，违 §5 第 2 条）。故显示文本与比对值由装配层从**同一个 `KeyCombo` 同行取出**（`ShortcutCommandEntry{binding_text, binding}` 分成两次取就是让二者各走一条路），比对值与行表**平行存放**，面板只读得到算好的那一列——这正是判据文 D2-a「标注来自实际比对」的证人形态。孤儿行因此 `comparable=false`、**恒不参与比对**，如实呈现为「延后」而不是「无冲突」。⑶ **保留位含本仓十二条分屏键位**——那些键位不经 `ShortcutRegistry`（`workspace_keys.h` 文件头的三条派发理由），而同一个组合键在派发上是快捷键层先消费（裁决 7.51③ 理由 (a)）；于是一条命令若与分屏键位同形，实际生效的是快捷键而**分屏命令按不到**，这一格必须标冲突，不能留给用户自己去试。

**一条修饰位口径是三处承重点**：比对只取 `sym` 与 Shift / Ctrl / Alt / Meta 四个**可按住的**位，`num_lock` 不参与（裁决 7.51①），与 `ui::workspace_command` 及框架 `KeyCombo::matches` **逐位一致**。三处任一改成「整字节相等」都会让这张表报出派发上并不存在的冲突。件为此新增 `WorkspaceKeyBinding` 与 `workspace_key_bindings()`：它**不另列一份修饰常量**，只把 `kBindings` 折成公共形态，故新增键位时「列表」与「认领集」结构上不可能分叉（`utest_workspace_keys` 两条新例即此判据）。**行序**：注册行照注册序在前、孤儿行按覆盖表自身次序在后，面板不自排第二次。

**验收**：`tests/unit/utest_shortcuts_table.cpp` **十二例**（行源与次序三例、比对域三例、冲突两半四例、空表与未绑定两例）+ `utest_workspace_keys` 九例→**十一例**；**八条变异注入各有转红证人**（把 `num_lock` 放进比对／去掉 Shift 位／只单向标注→四例同红／已注册命令的覆盖也建行／把孤儿行标成可比对／不与保留位比对→两例／`has_conflict()` 只判表内一半／把孤儿行标成已注册），每次重链前删 `build/**/*.ilk`。非 e2e 通道 `ctest -E etest_` **37 → 38 项全绿**（新注册一项）。未复跑吞吐门禁并给理由：纯逻辑件，不进任何绘制与布局路径。

**两条以实测登记的坑（判据写法与工具链，裁决 7.72⑧）**：⑴ 索引 `conflicts_with[0]` 之前**必须**先 `AURORA_TEST_REQUIRE_EQ(size(), 1U)`——Debug 档（`_ITERATOR_DEBUG_LEVEL=2`）的越界读是 `0xC0000409` fastfail，会把「变异被抓到」的读数从**一条指名用例**变成**一个退出码**（本棒首轮实测如此，与裁决 7.54 那条「行数判据须先 `REQUIRE` 再逐格比」同口径）。⑵ 脚本做变异注入时 `shutil.copy` 备份 + `os.replace` 还原会**保留备份文件的旧 mtime**，ninja 据此跳过重编，于是「已还原之后的构建」仍是变异体（实测表现为一条断言假红而源文件逐字已还原）；纪律补一条：**还原后 `touch` 被改文件并确认重编确实发生**。

**落点**：`include/borealis/ui/shortcuts_table.h`、`src/ui/shortcuts_table.cpp`、`include/borealis/ui/workspace_keys.h` + `src/ui/workspace_keys.cpp`、`src/CMakeLists.txt`、`tests/unit/utest_shortcuts_table.cpp`、`tests/unit/utest_workspace_keys.cpp`；文档回写＝`codespec/SPECIFICATIONS.md` §7 的 7.72、`codespec/UI_SETTINGS.draft.md` §4 D1-a / D2-a / D3-a 的取数口径更正与 §8 现状段、`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.02` 行，`AGENTS.md` §2 与 §6。**界面腿未落**（#148 / #149：面板 `Hooks::commands` 接缝、`build_shortcuts_section()` 自绘四列、装配层 `app.commands().all()` 折算与框架 `KeyCombo` → `term::KeyPress` 的唯一互转点），故本条不宣称快捷键页「可用」。

## v0.72（2026-10-06）**#116 第一棒补账：延后档位改由两处文字表达（`Absent` 行不再灰置）＋ 状态栏十枚开关组与三条组顶说明落地**（代码已在 `aa9d654` / `89fe339` 落库，本条随裁决 **7.74** 同批出账）

**动机**：裁决 **7.68③** 人已拍板——反向核对表里 `ConsumerStatus::Absent` 的那批行（状态栏十枚、终端页三条、SSH 五项、串口六项）**一律可用而只落盘**。本条把该口径的代码落地补齐在册（先前两次提交只写了提交信息，文档欠账由本条清偿）。

**口径转移的落点是「能不能点」而不是「生效条件」**：`apply_scope()` 本就把「无消费方 ∧ 即时」折成 `PersistOnly`（裁决 7.52 的 S3 那一半），故本棒不动该算式、也不新增广播判定；`SettingsPanel::is_editable()` 撤掉 `Absent` 那条早返回，「这条改动当下不会生效」因此在界面上只剩两处文字承担——行尾「延后」角标与落在该组第一行之前的**组顶说明**。不灰置的根据写进 `settings_panel.h` 文件头：把可用控件画成不可用，会让「四分类骨架全建」那句在交互面上**不可验证**。三条说明各点名**哪个消费方还没开工**（状态栏关闭件 / SSH 连接族 / 串口连接族）而不是泛称「延后」，词条落 `settings_i18n.cpp` 的 `settings.note.*`，插入位置由 `kGroupNotes` 的 `{first_key, note_key}` 对表在 `build_card()` 的行循环里按行表次序决定，不另数一遍页。

**观测点 `visible_notes()` 是这条口径的直接后果**：延后既然只剩文字，判据就只能比文字；而 `Text` **无可点语义**，真实派发量法对它无效（裁决 7.61⑤ 的命中链物理），故新开该只读观测点（与 `theme_cards()` / `chain_view()` 同一写法）而不是伪造像素断言。

**验收**：`itest_settings_panel` 44 → **45 例**（须 `AURORA_BACKEND_HEADLESS`），判据是「点得动 + 只落盘不广播 + 两处文字」而非灰置态；通则 `row.editable == (kind != ReadOnlyTable)` 同步改判（`sidebar_collapsed` 同为 `Absent` 故断 TRUE）。取样**不按 key 取控件**（面板没有 per-key 观测点，再开一个是第 5 处观测面），改为滚到内容下沿后挑**开着态**的 `Switch`，身份由**落盘差分**钉住（状态栏组必须变、同为 `Absent` 的 `sidebar_collapsed` 必须不变）——误取已接线开关会让广播计数与差分同红，没有假绿通道。变异自证：把灰置那一支加回 ⇒ 行落进占位分支、链上没有 `Switch`、`find_first` 回空，**两条用例同红**。**一处判据边界如实登记**（按 7.49⑥）：「这段说明文字确实进了控件树」结构上抓不到，归真机走查欠账。

**落点**：`src/ui/settings_panel.{h,cpp}`、`src/ui/settings_i18n.cpp`、`tests/integration/itest_settings_panel.cpp`；文档回写＝`codespec/SPECIFICATIONS.md` §7 的 7.74，`codespec/UI_SETTINGS.draft.md` §1 表行 31 / §2 注 / §4 B3-a / C2-a / A9-a 与 §5 S15① 那几处「灰置」措辞的就地更正，`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.02` 行，`AGENTS.md` §6。

## v0.71（2026-10-06）**接 G35 回货：三处「调用方自备 `BuildContext`」的运行期挂载补偿据此撤除，并补一条挂载计数控件作面板腿的唯一证人**（判据入册为裁决 **7.71**）

**动机**：G35 是 #115 预览盒棒读源实测登记的框架运行期挂载语义缺口（裁决 7.66⑥ 派发，任务书随裁决在会话内产出），人已明示「G35 已完成，G36 进行中」。本棒是**接货复验**而不是新功能：Aurora 当日活动分支 `9202ec46` 到货后，本仓此前为绕开该缺口而写的三处补偿**按 §5 第 2 条「不长期持有框架分叉」必须撤除**——框架回货的口径写在公共头上（`add_overlay` 的文档注释直接声明「运行期追加的子树由框架在下一次布局入口以父侧 ctx 补挂，调用方无须自备 `BuildContext`」），本仓再自备一份 ctx 去 `mount` 就是把框架的生命周期动作搬进应用侧。

**回货形态**（四读公共头与其 `src/aurora/widget/widget.cpp` 实现体而得，不是只读声明面）：`BuildContext` 增 `std::uint64_t host_id`（0 ＝ 未声明宿主）；`Widget::mount` 的幂等判据由「是否已挂载」这一个布尔换成**宿主身份**（同宿主跳过、换宿主先 `unmount()` 再挂），并新增只在挂载成功时写入的 `mount_ctx_`；新增 `Widget::unmount()` 与 `virtual on_unmount(ctx)`，与 `on_mount` 逐处对称，而**容器移除子项时不代调**是一条负向契约；新增 `note_pending_mount()` ＋ `virtual flush_pending_mounts(ctx)`，消费点在 `Widget::layout` 入口且位于 `show` 判定与布局缓存判定**之前**（补挂是生命周期动作，不该被「当前不可见」或「本帧无需重排」跳过）。登记待补挂的追加口共六处（`Container::add` / `adopt_children` / `set_children`、`OverlayHost::add_overlay`、`TabBar::add_tab`，另 `SingleChild` / `LayoutBuilder` 同批改列），文档回写落在该仓 `codespec/specification/04-widget.md` §2.3.1 并新增其自有用例。

**撤除点三处、零新增生产代码**：裁决 7.49④ 那一条（`WorkspaceView` 的 `on_mount` 覆写 + `mounted_` / `mount_ctx_` 两个成员 + `split_pane` 里补挂那三行）、裁决 7.66⑥ 那一条（`SettingsPreview::ensure_mounted()` 的声明与定义 + 面板 `build_preview_bar()` 的调用点）、以及 `itest_settings_panel` 里对照视口在**用例侧**的那一次调用。

**本棒唯一需要新写的是那条证人，而它必须写的理由是两条腿的成色不对称**：工作区腿**早有**行为证人（`itest_workspace_layout.a_new_pane_mounts_and_blinks`——一个闪烁周期后光标格像素相位翻动，「没挂载就没有周期任务，这条判据恒红」），撤除补偿后它仍绿即证明框架的 flush 真接住了 `Container::add`；面板／浮层腿在无头通道**结构上抓不到挂载**（7.66⑦ 的 M6 已在册：`on_mount` 只影响闪烁档与主题订阅，两者都不进本套件任何用例的判据），于是「43 例全绿」在该腿上**不是**证人。补的形态是一枚最小 `au::Widget` 子类（只覆写 `on_mount` / `on_unmount` 计数）经 `Harness::add_overlay` 走面板 `open()` 所用的**同一条**公共入口，五句断言各守一件事：追加而未排布局 ⇒ 0（**反空转前提**，缺这一句则「`add_overlay` 当场就挂上」那种实现照样让后四句全绿）、排一帧 ⇒ 恰 1、再排一帧 ⇒ 仍 1 **且退订 0**（宿主身份幂等）、嵌套孙辈 ⇒ 同 1（补挂是递归的）、运行期第二次追加另一棵 ⇒ 前者仍 1 而后者 1（补挂不是整树重来）。

**一条在册边界的改口而不撤销**：M6 那句「去掉补偿是等价注入、39 例全绿」随回货改口为「在无头通道仍判不到闪烁与主题，但**挂载动作本身现在可判**」；故 7.66⑦ 登记的欠账照旧在册——预览横条那一只视口的闪烁档与主题跟随仍无直接证人，归真机走查。**一条刻意留空的取舍**：面板的复用件（`font_popup_`、回退链候选按钮池、行控件句柄）在同宿主下由 `mount` 的宿主身份判据跳过重挂、旧订阅因此仍然有效，故本仓**不**为它们写 `unmount()`（无头通道 `BuildContext{}` 的 `host_id` 为 0，走同一条 same_host 分支）；按「不添加不可能发生路径上的防御代码」的口径这一处留空而不是留一条兜底退订。

**验收**：`itest_settings_panel` 43 → **44 例全绿**（其中 26 例须 `AURORA_BACKEND_HEADLESS`，另 26 条 `#else` SKIP 桩）、`itest_workspace_layout` **17 例全绿**、非 e2e 通道 `ctest -E etest_` **37 项全绿**（`100% tests passed out of 37`，42.06 s）；每次重链前 `find build -name '*.ilk' -delete`。**两条证人都经变异实测**（在 Aurora 树临时去掉 `Container::add` 与 `OverlayHost::add_overlay` 两处 `note_pending_mount()`）：面板腿新例转红（读数 `leaf->mounts` **0 vs 1**，`nested` 与 `second` 同）且**同套件其余 43 例全绿**，工作区腿同一次变异下 `a_new_pane_mounts_and_blinks` **单独**转红（其余 16 例绿）——即两条证人各自承重、互不冗余。变异已全量还原，该树 `git status --porcelain` 为空、HEAD 仍 `9202ec46`，本仓不提交 Aurora 任何改动。**未复跑吞吐门禁**并给理由：本棒只撤除三处补偿、新增一例测试，绘制与布局算式零改动，基准三场景不含面板。**G35 出账后 `SPECIFICATIONS.md` 附录 A.2 的开放缺口只剩 G36**（框架侧进行中，回货即撤本仓 `AnchorButton` 与 `row_area_` 折算那处过渡形态）。真机走查照旧未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。

**落点**：`src/ui/workspace_view.{h,cpp}`、`src/ui/settings_preview.{h,cpp}`、`src/ui/settings_panel.{h,cpp}`、`tests/integration/itest_settings_panel.cpp`（文件头⑪ 段 + 新例 + SKIP 桩）；文档回写＝`codespec/SPECIFICATIONS.md` §7 的 7.71 与附录 A.2 的 G35 行、`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.02` 行与 §6 缺口表、`codespec/UI_SETTINGS.draft.md` §8 那两处补偿口径（7.49④ / 7.66⑥）的就地更正、`AGENTS.md` §6。

## v0.70（2026-10-06）**#114 余件 A4（字体族选择器）本体落地：行内常驻按钮 + `au::Popup` 候选浮层；同批以实测否证 7.69③ 那句「与 `paint_bounds()` 同值」并登记 G36**（判据入册为裁决 **7.70**）

**动机**：A4 是 #114 的最后一件，形态与候选池次序已由人在同日拍板（裁决 7.69①②），本棒就是把那句话落成代码：`src/ui/settings_panel.{h,cpp}` 的字体族区段 + `tests/integration/itest_settings_panel.cpp` 的四例 + 一条新词条。

**交付形态**：一行两列——`AnchorButton`（本件私有的 `aurora::Button` 子类，`min_width = 240 dp`）标签是**生效族**而不是配置族（A4-b 禁止的正是把配置里那族直接显示出来，唯一的写入点是 `refresh_family_views()`），其**下方另起一行**是回落留痕；浮层是场景根 `OverlayHost` 上**跨开合复用的一只** `au::Popup`，内容 `Scroll` 锁高 `min(池档数, 8) × 30 dp`、宽 `260 dp`（7.69④ 那条松约束的兑现形态：`Popup` 在常规流占零尺寸且用松约束测内容，不锁高就溢出窗口）。候选池 `family_choices()`＝等宽筛 → `std::sort` **逐字节**字典序 → 置顶两档且**只在确实命中池时**置顶，同值靠「找到即 erase」合一。排序刻意不做大小写折叠：框架的族名匹配本就逐字节、区分大小写（裁决 7.46③），排序口径与匹配口径不一致会让用户找不到自己配的那一族。关闭四条腿各由一条用例守：点候选、`Escape`（在面板关闭**之前**）、点遮罩（A4-c）、关面板，摘除按序号**降序**。

**本棒最该留痕的一条：一处自己写下的口径被实测推翻**（裁决 7.70②）。7.69③ 与两处代码注释都断言「`position − local_position` 即得自身**全局**内容盒原点」且「该差的绝对值与 `paint_bounds()` 同源同值」；临时探针同帧、同一控件、滚动偏移非零的实测读数是 `paint_bounds().origin.y` **748** 对「派发对 + `offset_y()`」的 **826**，差 **78 dp**——**不成立**。机制在 `Scroll` 的离屏录制：录制进缓冲时传入的盒原点是 `{0, -buffer_origin_y_}`，故 `paint_bounds()` 在**缓冲坐标系**；派发链给子的 `global.origin` 是「视口窗口原点 + 内容盒原点」（不扣 `offset_y_`），故派发对是**窗口原点 + 内容坐标**。二者之差含 `buffer_origin_y_`，而该量无 getter、不进 `serialize_props`，**公共面取不到**。据此按 §4.2 第 12 条以运行时为准改口三处（`settings_panel.h` 文件头③、`AnchorButton` 文档、`build_family_section()` 行内注释）并就地更正 7.69③ 原文（旧句保留、注明更正落点）。探针断言本身已撤，不入库。

**随之登记 G36**（裁决 7.70③，附录 A.2 新增行，任务书已在会话内产出）：这条实测把 7.69 的「真缺口 0」推进成一条**公共 API 形态缺口**——框架对「事后问一个控件要它的窗口盒」不仅没有入口，其公共面交出的两个几何读数还分处两个坐标系且差额不可由公共面取得；而本仓的过渡形态必须**自持那只 `Scroll` 句柄**并知道「派发对是内容坐标」这一只在框架头注与私有算式里出现的模型。回货判据四条与「回货即撤 `AnchorButton` 与 `row_area_`」的承诺写在 G36 行。**不等不绕**：不自算 `buffer_origin_y_`、不改挂载点、不把 `Popup` 换成自绘覆盖层。

**一条「框架自陈的隐患」未被实测复现，据此改口**（裁决 7.70④）：7.56 起在册的理由「候选点击回调里调 `remove_overlay` 会在派发栈内销毁正在派发的子节点」——注入之后套件**没崩**，只有 A4-d 那条「浮层关掉而不摘除」的计数断言转红（`1 vs 2`）。故根据统一改写成「那是框架自陈的所有权模型之忧，不是本仓抓到的现场」，保留该形态的第二条理由是纯本仓的：摘了下次还得重新 `add_overlay` 并改序号。

**验收**：`itest_settings_panel` 39 → **43 例**（新增四例全在 `AURORA_BACKEND_HEADLESS` 块内，另四条 `#else` 同文案 SKIP 桩）——A4-e 锚点（反空转前提 `offset_y() > 0`，浮层上沿贴那一次 Press 的按钮下沿、按池宽铺开、留痕在其下）、A4-d 真实点击一档即提交且只关浮层（再开时池子按用例侧独立重算的次序重置顶）、A4-c 遮罩与 `Escape` 先于面板被交还、模型例五档现场（缺省 / 置顶两档 / 目录无那族 / 度量非等宽不入池 / 提交后标签与留痕跟着走）。**七条变异注入各有转红证人**：锚点不折滚动偏移（＝修复前形态）→ **两例红**（`864 vs 481` ＋「候选不可达」）；选完即摘浮层 → 1 红；遮罩去 `is_open()` 前置 → 1 红；`Escape` 去同一前置 → 1 红；去等宽筛 → 1 红（`7 vs 6` 且 `"Arial" vs "Arial"`）；去置顶 → **两例红**；锚点不接按钮下沿 → 1 红（`442 vs 481`）。**等价一条如实登记**（7.49⑥ 口径，不伪造证人）：去掉 `refresh_family_views()` 的 `mark_needs_layout()` → 43 例全绿，因为那两列**从控件本体读**而不是从布局宽度读。**两条判据边界在册**（7.70⑥）：模型例只能在无头通道做（卡片是 `LayoutBuilder` 闭包、只在布局期建，故每段先排一帧；本件析构不撤自己的浮层，每段末尾显式 `close()`）；留痕那行是 `Text`，不可点也不含可点后代，故 7.60② 那套 `reachable_box()` 量法对它无效，只比同一坐标空间里的上下关系。非 e2e 通道 **37 项全绿**；未复跑吞吐门禁（新代码只在面板打开与浮层开合时生效，不进每帧绘制与布局路径，基准三场景不含面板）。**真机走查照旧未做**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），字体族浮层的手感在欠账清单上，故本条不宣称面板「可用」。

**落点**：`src/ui/settings_panel.{h,cpp}`、`src/ui/settings_i18n.cpp`（`settings.font.fallback` 一条词条，两个位置参数是**取值**而非 key 故交字面档）、`tests/integration/itest_settings_panel.cpp`；`codespec/SPECIFICATIONS.md` §7 的 7.70 与 7.69③ 就地更正、附录 A.2 的 **G36** 行，`codespec/UI_SETTINGS.draft.md` §4 A4-b~A4-e 的落地现状与 §6 第 14 条，`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.02` 行与 §6，`AGENTS.md` §6。**#114 至此四件专用区段齐备**（主题卡 / 16 格色板 / 回退链 / 字体族），余下在册的是 #116 / #117 / #130 与 G35 / G36 的接货复验。

## v0.69（2026-10-06）**#114 余件 A4（`appearance.font_family` 的候选入口）形态拍板与框架面读源：真缺口 0，浮层锚点改走事件坐标对**（不出任务书、零代码改动，判据文 §6 由十三条改十四条）

**动机**：常驻口径是「做任务 N 的时候提前梳理任务 N+1 的框架依赖项，我提前让其他 Agent 补全，不阻塞」。A4 是 #114 的最后一件，也是设置面板里**唯一**一件「形态待人拍板」的控件（裁决 7.62⑤ 把它留在册上，理由随 G31 回货已从三处技术限制收窄为一条框架形态约束）。本棒按裁决 7.13① 逐条读到 Aurora 当日活动分支 `34712290` 的公共头与 `widget.cpp` / `event/dispatcher.cpp` 实现体（7.55 那条教训：只读声明面会把「公共 API 组合得出来」误判成缺口）。

**结论先说**：**真缺口 0**，故本棒不出任务书、附录 A.2 不新增行（开放缺口照旧只剩 G35），零代码改动；两条形态由人拍板后落进判据文。判的根据是本棒读到的一条**框架自己也未解决**的问题加一条**公共 API 上现成的等价坐标**（见下两条）。

**形态拍板（判据文 §4 新增 A4-c）**：**行内一枚常驻按钮（标签是当前生效族名）＋ 点击经场景根 `OverlayHost` 弹一列候选浮层**，而不是行内 `au::Dropdown`。根据是 7.62⑤ 在册那条框架形态约束「列表行内不得使用覆盖绘制不占布局的控件」（该仓 `05-event-navigation.md` §3.2.2），而面板每一行都在行区列表内；本形态**不是绕开**该约束，而是落在它射程之外——行内按钮正常占布局，覆盖绘制发生在场景根的浮层里。三条代价如实登记：浮层定位由本仓自驱（框架无「控件 → 窗口绝对盒」入口）、「点外部即关」由本仓自驱（`OverlayHost::handle_outside_click()` 在 Aurora 全仓无生产调用点，7.41③ 那条继续成立）、**面板遮罩那枚「点空白处关面板」的 `clickable` 须连带改成「浮层开着时只关浮层」**，否则第一次外部点击把浮层与面板一起撤掉。z 序不用本仓处理：`OverlayHost` 的 `on_paint` 前向而 `on_hit_test_chain` 逆序，字体族浮层在面板浮层之后 add 即天然在最上层。

**候选池与次序拍板（新增 A4-d）**：**等宽全集 + 族名逐字节字典序 + 当前值与 `ui::kDefaultMonospaceFamily` 置顶两档**（同值合一），超出可视高度靠浮层内部 `Scroll`。这条补的是本稿登记时那句「本机 200+ 族如何挑选与排序没有依据」：依据是 `list_font_families(monospace_only = true)` 已把 200+ 收窄到 20+ 档，而字典序 + 当前值置顶让「确认现在用哪一族」与「找回并改回去」两条动作各一步可达。**不分页、不加过滤框**，与 A5-a 那池的分工差异是本池只「全集里选一档」、那一池要「从全集里找一族加进链」。

**本棒最该留痕的一条：锚点坐标系**（判据文新增 §6 第 14 条与 A4-e）。框架公共面**没有**「控件 → 窗口绝对盒」的入口（`Widget` 只有 `size()` / `paint_bounds()` / `focus_bounds()` / `dirty_bounds()`，`Node::bounds()` 是父写入的局部盒；全仓 grep `global_rect` / `window_rect` / `absolute_bounds` / `local_to_global` / `to_global` 在 `include/aurora` 只命中 `command_palette.h` 的一个**私有静态** `to_global(local, bounds)`），而 `paint_bounds()` 在离屏缓冲（`Scroll` 内容即其一）内是**缓冲帧**读数——框架在三处自陈该限制（`widget.h` 的 `focus_bounds_` 注、`a11y_tree.h` 同款注、`scroll.h` 的「几何与命中契约」节），其自有 UIA / ATSPI 桥亦直接取 `paint_bounds()` 而带同一限制，即**框架自己也没解决「事后问一个控件要它的屏幕盒」**；它对同一问题的自解形态是**派发期把偏移透传下去**（G30 回货那条 `ancestor_offset`）而非事后查询。**因此本仓禁止**用 `paint_bounds()` 做应用侧「内容坐标 + 滚动偏移」折算（`Scroll` 的缓冲原点不公开，折算等于把框架私有算式复制进本仓，违 §5 第 2 条且属 G31 / G32 同族病灶）。**改用的公共路径**是派发器本来就交出的坐标对：`deliver_chain` 逐节点写 `e.local_position = 全局点 − 该节点 origin`，而那个 origin 自 G31 回货起与绘制原点逐位相等（7.64②③④），于是控件在自家 `on_pointer_event` 的 Press 分支里按 `position − local_position` 即得**自身全局内容盒原点**，配 `size().height` 得下沿。代价：`Modifier::clickable` 的回调**不携带坐标**且公共面无带坐标的指针修饰钩子，故锚点必须由本仓私有 `Button` 子类自取（与 `BlurCommitText` 同档），且 `Button::on_pointer_event` 在禁用态吞掉事件、委托基类那一句要照抄。

**两条形态限制随之在册**：⑴ `au::Popup::on_layout` 用**松约束**测内容（max 取父 max 或 4096）且在常规流占零尺寸 ⇒ 20+ 档候选列**须自带 `Scroll` 锁高**否则溢出窗口——这是 7.62② 那条 `Dropdown::panel_box` 限制在 Popup 形态下的对应物，解法是包 `Scroll` 而不是改挂载点或换成自绘覆盖层；⑵ 一条与 7.68⑤ **同源而后果相反**的派发栈事实：`Popup::open_at` 只在 `current_focus_manager() != nullptr` 时压焦点作用域，而该上下文只在派发栈内有效——**从按钮 Press 回调里 `open_at` 恰好落在栈内**，故 Tab 焦点真关在浮层内，#117 那条「启动路径上取不到作用域」的限制不适用于本件；`Popup::close()` 自带 `open_` 守卫，故面板程序化关闭不产生悬垂弹栈。

**G35 的射程随本棒复核并再收窄一处**：运行期 `add_overlay` 不 `mount` 只影响「在 `on_mount` 里做订阅」的控件，而 `Popup` 与其内容按钮**都不**在 `on_mount` 里订阅 ⇒ **A4 不被 G35 阻塞**，可与 #115 / #116 并行开工。

**§6 计数与两处现状段就地改口**：该节标题由「十三条」改**十四条**并注明第 14 条属同日 A4 字体族浮层棒；第 10 条末句「本稿不预先拍 A4」随人的拍板作废（沿革段按原文保留、不再代表待决状态）；§8 两处「A4 照旧待人拍板」的现状段各加「该句按当时现状照录、同日即过期」的注（§2 的 `font_family` 行形态列同批改口）。

**验收与代价**：本棒零代码改动故**无测试、无变异自证**，按裁决 7.49⑥ 的口径如实登记而不伪造绿灯；非 e2e 通道维持在册 37 项全绿且未复跑（本棒只读框架源码与本仓文档），吞吐门禁同样未复跑。**真机走查照旧未做**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），A4 本体开工后仍不宣称面板「可用」。落点：`codespec/SPECIFICATIONS.md` §7 的 7.69 与其 §7 表头、`codespec/UI_SETTINGS.draft.md` §2 / §4 A4-c~A4-e / §6 第 14 条 / §8，`codespec/PLAN.md` 的 `SPEC.FEAT.PREF.02` 行，`AGENTS.md` §6。

## v0.68（2026-10-06）**#116 / #117 的框架依赖提前梳理：真缺口 0 加三条形态限制**（不出任务书、零代码改动，并就地更正判据文 §7 那条降级对话框的宿主形态）（裁决 7.68）

**动机**：人 2026-10-06 直接问过「116和117的框架依赖梳理出来了吗」，且常驻口径是「做任务 N 的时候提前梳理任务 N+1 的框架依赖项，我提前让其他 Agent 补全，不阻塞」。本棒就是把这句话落到 #116（快捷键只读表 + 状态栏十枚开关）与 #117（降级对话框 + 三条会话侧构造期注入接缝）上。

**结论先说**：按裁决 7.13① 逐条读到 Aurora 当日活动分支 `34712290` 的实现体（不是只读声明面——7.55 那条教训就是声明面判「属可组合」而实现面是缺口），**真缺口 0**，附录 A.2 不新增行，开放缺口照旧只剩 **G35**；因此**本棒不出任务书**。三条限制连同各自的规避形态写进 `codespec/UI_SETTINGS.draft.md` §6 的第 11 ~ 13 条。

**第一条限制（#116 的表）**：`DataTable` / `TreeView` / `ListView` 三件的 `on_paint` 形参一律写成 `/*ctx*/`，`Theme` / `ThemeScope` 到不了它们，实现体的五处色值全是硬编码浅色，而公共面没有色值 setter、没有 `set_enabled`、列定义只能走构造，标签又是 `std::string` 故无 i18n 路径。不派框架的理由是**同一条判据的在册先例**：7.52 就地更正那条里 `MenuBar` 的下拉同样硬编码浅色、不随 `Theme`，当时的处置是登记事实而不派框架；且本仓已三次用自绘行表交付同类形态（主题卡 / 16 格色板 / 回退链，裁决 7.61 / 7.62）。

**第二条限制（#116 的开关）**：`Switch` 的**禁用态那一组**色值硬编码浅灰（`switch.h:396-399`）且四件色 setter 只作用于启用态，于是在深色界面上「灰置档比可用档更亮」——后果与直觉相反。它不构成障碍：S10 与 S15① 拍的形态本就是「可改可落盘 + 延后角标」（`apply_scope()` 把 `Absent ∧ Immediate` 折成 `PersistOnly`），暗色观感由那四件 setter 交出而不走 `set_enabled(false)`；真需要禁用档时 `paint_track` / `paint_thumb` 是该文件自陈的 protected virtual 扩展点，子类覆写属裁决 7.13② 的组合形态。**附带一条同棒实测**：禁用态 `on_pointer_event` 先置 `is_handled` 再 return，即吞掉点击而不冒泡，故「点它不动」的证人只能判计数——与裁决 7.62④ 那条「`Button` 未覆写命中入口、禁用态仍在链上」同族，两条合起来才是「禁用态」的完整判据写法。

**第三条限制引出一次就地更正（#117）**：`Dialog` 的遮罩是硬编码 `Color(0,0,0,128)`（深色启动路径正合适，故本仓不需要它可配），而 `aurora::alert` / `confirm` 的按钮标签是字面量 `"OK"` / `"Yes"` / `"No"`、标题正文是 `const std::string &`，整条链没有 i18n 入口，与 S13 的「按 `LoadOutcome` 取本仓中文词条」不合。于是该稿 §7 那句「G26 / G27 回货后经判定仍不迁回 `au::Dialog`」**收窄为只适用于面板浮层**：7.57③ 那三条根据（`self × 0.8` 内层约束、遮罩不关、`show()` 带模态作用域）全是面板排版的理由，而一处启动路径上的模态提示要的恰好是这三条的反面，且 **S12① 原本拍的就是 `au::Dialog`**——回货后内容与长列表都进得了命中链，形态因此改回原拍，只是自建 content Node（`Text` 交 `LocalizedString`：`text.h:236 resolved_text(ctx)` 走查表并随 `LocaleProvider` 取区域，说明 G28 只挡 `Button` 不挡本件；长列表包 `au::Scroll`，与面板行区同口径，那句「用 `LazyList`」按裁决 7.60① 一并更正）。

**一条只有读派发栈才现形的物理事实**（本棒最该留痕的一条）：`Dialog::show()` 的模态焦点作用域依赖 `current_focus_manager()`，而它只在派发栈内有效（`dispatcher.cpp:185` 与 `window_host.cpp:218 / :254` 配对设置，该文件自陈「退出时复原」），故 `main` 里直接 `show()` 走的是「无焦点管理器时降级为仅置位」分支，`push_scope` 不发生、Tab 焦点不关在框内；框架给非派发栈动作的文档化通道是 `resolve_focus_manager(Widget&)`（沿 `layout_parent()` 上溯取或建按根缓存的兜底实例）。同时写清**「遮罩吸收点击」与「焦点作用域」是两件事**——G26 回货那条链入口在遮罩区回 `{this}` 不依赖作用域，登记这句是为了防止把「没有 Tab 关在框内」误读成「对话框没生效」。

**G35 的射程随本棒复核并收窄**：运行期 `add_overlay` 不 `mount` 只影响「在 `on_mount` 里做订阅」的控件。实测三处不受影响——`Dropdown::env_` 另有 `on_layout`（:385）与 `on_paint`（:404）两处兜底写入；`Provider` 的注入环境沿 `on_paint` 的 `child_ctx.env` 逐层下传（`provider.h:125-130`），而本仓 chrome `ThemeScope` 挂在**场景根**（`src/main.cpp:151`，先于任何浮层）；`Scroll` 根本不覆写 `on_mount`。故 **#116 / #117 都不被 G35 阻塞**，附录 A.2 与 `PLAN.md` §6 的 G35 行同批补这句影响面。

**验收与代价**：本棒零代码改动故**无测试、无变异自证**，按裁决 7.49⑥ 的口径如实登记而不伪造绿灯；非 e2e 通道维持在册 37 项全绿且未复跑（本棒只读框架源码与本仓文档），吞吐门禁同样未复跑。

## v0.67（2026-10-06）**G34 接货复验与出账**：那条「断链要可见」的告警改按「子件是否在容器之外仍被持有」分档，本仓撤销登记时的判别式建议、并把自身句柄的释放顺序改到摘树之前（`itest_settings_panel` 噪声 5090 → 1740 → 9 行）（裁决 7.67）

**动机**：G34 由另一 agent 在 Aurora 侧完成并推送，人已通知「G34 已完成并推送了」，本棒按裁决 7.13① 的接货复验口径出账
**本棒性质**是复验而不是新功能。G34 是诊断通道缺陷，其「真断链时出现、正常析构时不出现」两条判据的**内容断言**在 Aurora 侧（该仓
`utest_layout_parent_chain` 八条），本仓没有断言日志文本的测试设施也不为此新造一套，故复验落在**噪声量级 + 残留能否逐条归因**上
（按套件统计 `layout parent detached` 出现次数）。

**回货形态**（Aurora `25e9f0a6`）：`detach_all_children_layout_parent()` 不再对每个子节点无条件告警，而按「被摘子节点是否在容器之外
仍被持有」分档——随容器正常销毁**静默清指针**（该清的照旧清），活在容器之外被摘走仍发 WARN；`Node` 新增 inline 只读 `use_count()`；
文案改按该仓日志宏的可变参数拼接形态，`%s` 不再原样输出而由类型名进正文。回货判据 ③（插值）与 ② 的后半（两档分野写进该仓
`04-widget.md` §2.3，并就地更正那句被实测推翻的「真正销毁控件的路径不经本函数」）成立。

**一条判别式口径修正——本仓登记时的建议不成立**：裁决 7.64⑦ 与附录 A.2 的回货判据 ② 都写着 `Node::use_count() == 1`，实测该写法
会把 `SingleChild` 系包装件的正常销毁继续误报：`child_nodes_mut()` 对 `SingleChild` 返回的是 `child_view_mut_`（`child_` 的**拷贝副本**，
另有 `child_view_` 惰性缓存），无外部持有时 `Container` 报 1 而 `SingleChild` 报 2。该仓因此改判「**自身持有份数**」口径
（`child_owned_ref_count()` 缺省 1、`SingleChild` 覆写算上两份视图副本，`use_count() <= 它` 即判无外部持有）。
「与既有 a11y 唯一所有权守卫同口径」这半句仍成立，失效的是常数 `1` 这个具体写法。**结论以实测登记：跨仓建议的判别式必须以消费方的
实测基线为准**——登记时本仓只量过面板卡片那种 `Container` 形态，没量过 `SingleChild`。

**三次读数**（同一批 39 例）：接货前 **5090** 行 → 接货后 **1740** 行 → 本仓时序自补后 **9** 行。登记时那条 3696 行是 33 例的现场量
（其后 #115 补了 5 例），同一算式、两个现场。**剩余 9 行逐条可归因**（新语义下**就该**出现）：`TerminalView` × 5 ＝ 预览横条在每次换页
重建时把同一只视口从旧卡片摘走再挂进新卡片；`Button` × 3 ＝ 回退链候选池里被 `show = false` 收起、仍被面板自持的那几枚追加按钮；
`Canvas` × 1 ＝ 同批的主题卡画布。回货判据 ① 因此由**读数的结构**证明：正常销毁路径那 1740 行归零。

**本仓侧的时序自补不是消音**：`close()` 与 `rebuild_overlay()` 原本**先** `host_.remove_overlay()`、**后**清本仓自持的派生控件句柄，
于是旧卡片子树析构那一刻 `status_texts_` / `theme_canvases_` / `theme_labels_` / `swatch_canvases_` / `swatch_editor_` /
`swatch_reset_button_` 与回退链各件仍活着 ⇒ 按回货后的语义这些**正是**「活在容器之外被摘走」。把清句柄整体提到摘浮层之前，噪声降到 9 行。
G34 在册的三条约束（不改级别、不在应用侧消音、不给 `~Container` 打补丁）全程未破。

**一条自我更正（形态比结论更值得留）**：第一次把 `preview_.reset()` 一并加进 `rebuild_overlay()` 的清句柄批次，`itest_settings_panel`
**5 例同红**——预览盒由 `open()` 的 `refresh_preview()` 建好且**要随页切换存活**（`select_page()` 只调 `rebuild_overlay()`），
故那句在 `rebuild_overlay()` 里等于把刚建好的横条抹掉；第二次挪到 `remove_overlay()` **之后**仍错（同一原因，与位置无关）。最终处置＝从
`rebuild_overlay()` **完全删除**，只在 `close()` 保留并置于摘浮层之后（`ui::TerminalView` 持会话的裸引用，旧树的 `Node` 在摘除前仍握着它）。
**结论：「先放自持句柄、再摘那棵树」这条时序只在句柄与该树同生同灭时成立；跨浮层存活的句柄必须留在最长的那一条路径（`close()`）上，
否则时序优化就变成生命周期破坏。** 同批另一处：`close()` 里曾把清句柄嵌进 `if (overlay_index_.has_value())`，会让「面板已开而无浮层序号」
那一档不清句柄，已改为无条件前置。

**验收**：`itest_settings_panel` **39 例全绿**（18.34 s）、非 e2e 通道 `ctest -E etest_` **37 项全绿**；其余套件读数
`itest_right_click_paste` 10 行 / `itest_workspace_layout` 1 行 / `itest_render_viewport` 0 行（用例分别 20 / 17 / 32 全绿）。
**本棒无变异自证并给理由**：改的是析构与摘除顺序，其判据是日志行数而非任何断言；两次实测读数（1740 → 9）即其自证，而那次 5 例同红
正是「顺序改错会被既有测试抓到」的实证。**未复跑吞吐门禁**（面板打开路径，不进每帧绘制与布局）。同批一条 lint `34712290` 随货到达
（`LazyRow::accessibility_scroll` 的 cast-init 改 `auto`），与本条判据无关。**附录 A.2 的开放缺口自此只剩 G35**；真机走查照旧未做
（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。

。

## v0.66（2026-10-06）**#115 实时预览盒落地 + 登记 G35**：卡片底部那条 140 dp 横条走的是真实绘制路径，同批实测出一条尺寸 precedence、两条「判据结构上不可能成立」的框架缺省与一条运行期挂载语义缺口（裁决 7.66）

**动机**：S7 与判据 F-a~F-e 是 #115 的全部交付面，也是判据文 §8 集成判据里最后两条「两个视图」的前置——改 palette 一格 →
预览盒与主视口的**同一格色**逐位变化；改字号 → 两个视图各自按自身矩形重算行列数且**不出现半格**。预览盒落位已由人拍板
＝**卡片底部一条 140 dp 高的横条**（F-e，裁决 7.64⑩）。

**不写假预览**（N5 的兑现形态）：新件 `src/ui/settings_preview.{h,cpp}`（私有头，裁决 D1① 同口径）是「内存连接 + 独立
`session::Session` + 真实 `ui::TerminalView`」三件套。夹具 `preview_fixture()` 交出的是一串**字节**而不是绘制指令，走解码 →
解析 → 状态机 → 网格 → 既有的五层绘制序列上屏，于是那条判据判的是**接线**而不是替身行为；第 4 行三格底色取
`basic[1]/[2]/[4]`，就是为了「同一格色逐位相等」有一格可指，且该常量交出来供用例拿**同一份素材**喂第二个预览实例。面板侧
两条接缝 `Hooks::preview_appearance`（不装即不画）与 `Hooks::preview_wake`：装配层用**与主视口同一对** `make_appearance()`
搬运函数喂它，`Application::set_on_frame` 里加 `panel.pump_preview()` 排掉「尺寸一变就重发夹具」那条腿产出的脏。三条
交互缺省（不派焦点、无浮层宿主、`copy_on_select` 恒假）都是**防摸到用户的东西**：焦点被抢则面板输入框失灵，右键有宿主则
真弹菜单，选中即复制则泵帧时写系统剪贴板（违 §4.5 第 25 条）。代价是预览里的光标恒是失焦的空心形态——那是口径而非缺陷。

**本棒唯一的真缺陷是尺寸 precedence，且修法只有一条**：横条高度只能经**控件级尺寸意图**下达，挂在修饰链上的
`Modifier{}.height(140)` 被**静默吃掉**。读 Aurora `Widget::layout` 实测得根因：该函数在跑完整条修饰链之后还要按
`width_` / `height_` 的强类型意图**再次覆写 `size_`**（该段自陈「显式盒最终尺寸严格等于设定值」），而 `ui::TerminalView`
构造时自宣 `width/height = fill()`，于是高轴的 Expand 把链上钉好的 140 改回「父级剩余高」。实测读数：意图缺省时横条占满
header 以下整段（**506 dp**）并把行区 viewport 压成 0 高。对照视口同因——用例那条修饰链 `.width(400).height(200)` 同样被吃掉，
表现为对照视口铺满整窗、把横条盖在下面，`find_first` 因此一台预览都探不到；那不是用例写错，而是同一处 precedence 的第二次
现形。**由此登记一条框架文档措辞不准确**（按裁决 7.13② 留本仓、只入册不出任务书）：`Modifier` 头注把链式尺寸修饰与控件级
`width()` / `height()` 意图说成**正交**，而实际是测量之后的覆写、且冲突时没有任何留痕。

**「发一段文本看有没有人认领」这条判据结构上不成立**：`Widget::on_text_input` 的**基类缺省即 `is_handled = true`**，派发器只把
文本投给 `fm.focused()`，而点击时 `focus_target_of(chain)` 会把焦点交给命中链上**最近的可获焦祖先**（实测本面板是卡片外层
`Column`，那一位会把任何文本吞掉）——于是「点完横条再发文本、判它无人认领」恒真是被认领，拿它当 F-a 的证人是**假红**（变异
M2 复证）。可判的等价形态是**「预览从不进 Tab 序」**：候选集是框架私有的 `collect_focusable`，只能经公开入口
`move_focus(Forward)` 绕一圈（用例绕 24 步）断其间从不落到预览视图。**结论以变异实跑登记：凡以「事件有没有人认领」作判据，
须先读基类缺省是否已消费，否则测的是派发器投给了谁，而不是被测件的行为。**

**新登记 G35（任务书随裁决 7.66⑥ 在会话内产出）**：运行期新增的子树不经 `mount(ctx)`——`OverlayHost::add_overlay` 只做
`push_back` + 标脏布局、`TabBar::add_tab` 同形，而框架自家五个容器都是「父侧持 `on_layout` 的那份 ctx、新增子树即时
`mount(ctx)`」；后果是运行期挂进浮层或标签栏的控件其 `on_mount` 订阅（本仓视口的闪烁档与主题订阅）永不注册。同病灶第二条腿
是 `Widget::mounted_` **只有置真路径、从不置 false** ⇒ `remove_overlay` 之后订阅不退。本仓**不等不绕也不自消订阅**，改由
`SettingsPreview::ensure_mounted(ctx)` 在卡片构建时补一次挂载（幂等）——与裁决 7.49④ 那条「`present_root` 只在根变化时遍历
挂载」是同一物理事实的第二处现形。

**两条判据写法层面的自我更正**：① 去掉 `ensure_mounted`（变异 M6）之后 **39 例全绿**——无头通道里 `on_mount` 只影响闪烁档与
主题订阅，两者都不进本件判据，故该补偿在本通道**结构上判不到**；按裁决 7.49⑥ 保留承重补偿而不伪造证人，并把「横条的闪烁与
主题跟随」归入真机走查欠账。② `Harness::reachable_box` 的 `size` 是**含端点计数**（`right − left + 1`），故
`origin.y + size.height` 比真正的最后一个命中点大 1 dp；`find_first` 的 `room_below_dp` 门槛原拿它当末点用，于是**与卡片下沿
齐平的控件被整体跳过**——而预览横条按 F-e 正是要齐平（实测：可达框末点 584、`bottom()` 585、门槛 584）。已把门槛改成减 1 dp。
这是裁决 7.60④ 那条「挑样启发不是判据」的姊妹项：挑样启发写错会把整档控件从扫描里剔掉，且症状与「该控件不可达」完全同形。

**验收**：`itest_settings_panel` 34 → **39 例全绿**（新增五例：接缝没装就不画横条；预览行列数由它**自己的矩形**派生且只装
整数格、字号 14→24 之后同一矩形里行列严格变少（F-d / §8 判据②）；横条 140 dp 且行区 viewport 恰少 140 而 content 不变——
量具取 G33 回货的 `accessibility_scroll()` 两个几何量，锚点页取终端页因外观页首屏可见带里没有步进器；真点横条既不夺键盘也
不进 Tab 序且不提交任何值；同一夹具的第二个预览实例与横条在同一格色上逐位同变），非 e2e 通道 `ctest -E etest_` **37 项全绿**；七轮变异
注入各有处置（M1 高度退回修饰链 → 140 dp 那条、同变格那条与 whole-cells 那条转红；M2 假红复证；M3 / M4 / M5a / M5b / M7 各有证人，其中 M7 去掉
`hooks_.preview_wake()` 被既有接缝用例抓到，故该腿有证人；M6 等价已如实登记），每次重链前删 `.ilk`，全部变异已还原。
**本棒未复跑吞吐门禁**并给理由：预览盒只在面板打开时构造与泵帧，不进基准三场景，绘制与布局算式零改动。真机走查照旧未做
（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），且 F-e 那条「扁条按 14 pt 约 5~7 行、示范面收窄」的代价只能人工判，
故本条不宣称面板「可用」。

---

## v0.65（2026-10-05）**G33 接货复验出账 + 撤销一条临时取证口径**：一条无障碍读数改由两个几何量算出，本仓在消费面补一条证人并把「不采信该读数」就地废止（裁决 7.65）

**动机**：v0.63 由面板行区真机走查棒登记的那条已证 UIA 缺陷（`IScrollProvider::get_VerticalViewSize` 报
`max/(max+1)*100`，跨度越大越接近 100% 即与真相反向）同日随 Aurora 当日活动分支 `4397c82e` 到货。本棒做接货复验、
按预诺出账，并兑现一件登记时写下的**临时口径的撤销**。

**回货形态是「补两个量」而不是「改一行算式」**——这正是附录 A.2 那行写明的修法前置：`AccessibilityScrollRange` 从
`{min, max, position}` 扩成 `{min, max, position, viewport, content}`（按值聚合初始化，旧三字段写法源兼容；头注给不变量
`max − min == content − viewport`），算式收敛为平台中立纯函数 `compute_vertical_view_size(range) = clamp(viewport / content
× 100, 0, 100)`，`content <= 0 ∨ viewport <= 0` 报 100（「无跨度＝全部可见」那一档语义本来就对，登记时也已这么写）；
`Scroll::accessibility_scroll()` 以布局期已知的 `viewport_h_` / `content_h_` 填充，`LazyList` / `GridView` 同批，`LazyRow`
纵轴不可滚报 viewport＝content（⇒100），Win32 桥在无滚动语义的节点直接报 100。旧算式里那个既不当量纲也不当哨兵的
`+1.0` 从此不存在于任何调用点。

**本仓接线面 0 行，而这不是「无事可做」而是「复验只能落在用例」**：全仓 grep 无 `accessibility_scroll` /
`AccessibilityScrollRange` 读点——辅助技术经框架直接读控件，本仓不自建 UIA 桥。于是复验的落点只能是集成用例，且
刻意**不复制框架的算式测试**（§5 第 2 条：不在应用侧持有框架的分叉，也不把框架的单测重写一遍）。新用例
`the_row_area_reports_a_viewport_over_content_ratio_to_the_a11y_channel_G33` 只断消费面三件事：① **两个量的来源**是本仓
几何（`viewport` ＝行区控件自身 `paint_bounds().size.height`、`max − min == content − viewport`）；② **读数的量级**远离
旧档（本仓现场实测视口 506 dp / 内容 672 dp / 可滚跨度 166 dp ⇒ 新式 **75.2976%** 对旧式 `max/(max+1)` **99.4012%**；
登记时那次真机读数 99.2126% 反解恰为 126/127，同一算式、两个现场量，两条都对——旧式在两处都把「还有 166 dp 可滚」
报成「几乎全部可见」）；③ 滚三档（框架 `ScrollProps::step` 缺省 16 dp × 3）之后**只有 `position` 走而两个量逐字不动**
（这是回货判据 ③ 在本仓的消费面形态：`VerticalPercent` 说看到哪儿、`VerticalViewSize` 说看到多少，二者不得同动）。

**一条判据写法教训以变异实跑登记**：变异 B（把 `Scroll` 的 `.viewport` 填成 `content_h_`，即「**算式正确而输入陈旧**」，
正是登记时那次把走查推断带偏的形态）**首轮红在鉴别力前提那一行**（`REQUIRE_GT(content, viewport)`，672 vs 672），而真正
想守的「`viewport` 是不是本控件盒高」那两句根本没机会跑。根因是 `REQUIRE_*` 会终止用例：把**前提**排在**被检事实**之前，
就等于让前提的失败遮蔽事实的失败；且无跨度时新旧两式都报 100，量级那条判别结构上也失去鉴别力。处置是把 ① 那两句
`CHECK_NEAR` 提到两条 `REQUIRE_GT` **之前**并就地写 WHY，复跑读数 `672 vs 506`（同源那句）与 `166 vs 0`（框架头注那条
不变量）——输入陈旧这一类现在有一条**指名来源**的红行，而不是一行「content 不大于 viewport」把三种病因混在一起。
另一条变异（A＝纯函数退回 `max/(max+1)*100`）两行红。两次都在还原后转绿，每次重链前 `find build -name '*.ilk' -delete`。

**撤销一条临时口径**：裁决 **7.63⑥** 结尾那句「本仓走查与用例一律**不采信**该读数，改以无头侧
`accessibility_scroll()->max` 与派发结果独立量」随回货**就地废止**，#130 那条走查的取证通道恢复可用。
**#130 本身仍不结项**：未结的是真机**真实滚轮**落该行区再看像素那一腿（须前台，7.63④），且 7.63⑤ 那条「G27 消费例
断的是派发而不是像素」的判据成色欠账照旧在册，与本条无关——别把「读数修好了」读成「走查结项了」。

**两条环境事实入册以免下棒重复排查**：① 本仓 `build/` 与并行 agent 共享同一目录，本轮两次构建失败
（`serialization.cpp.obj: Permission denied` 与 `RC1109 error creating .../manifest.res`）经 `tasklist` 查
`cl/link/rc/ninja` 均无本仓进程后判为**争用 + 陈旧产物**，删掉那一个 `manifest.res` 即恢复，**不做整树清理**；
② **G34 由另一 agent 在 Aurora 侧在办**，故本棒在那棵树只做临时变异并已全量还原（`git status --porcelain` 空、
HEAD 仍 `4397c82e`），本仓不提交 Aurora 任何改动。

**验收**：构建全量通过；`itest_settings_panel` 33 → **34 例全绿**；非 e2e 通道 `ctest -E etest_` **37 项全绿**（44.18 s；
CTest 项数不变而用例数 +1）。§2 的测试行随本条就地更正两处既有漂移（「设置面板本体三十三例」→ 三十四例、
「其中十一例须 `AURORA_BACKEND_HEADLESS`」→ 十七例，实测 `#else` 分支 17 条 SKIP 桩）。**本棒未复跑吞吐门禁**并给理由：
新读数只在面板打开与滚动批次里被读，不进每帧绘制与布局路径，基准三场景不含面板。真机走查照旧未做
（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。

---

## v0.64（2026-10-05）**G31 / G32 接货复验出账 + 登记 G34**：一条钉子按预诺翻成正向行为用例，一层断链补偿按预诺撤除，同批复验撞出一条诊断告警缺陷（裁决 7.64）

**动机**：v0.62 登记的两条缺口（G31 派发 origin 不含祖先的 modifier 平移、G32 `Node::~Node()` 抹断仍存活控件的布局父链）同日随
Aurora 当日活动分支 `1b3fe58c` 到货。本棒做接货复验并兑现两条登记时写下的预诺：G31 的钉子「回货后差值归零即令其转红」，
G32 的补偿「回货后若成冗余随本条一并撤除并在裁决里记账」。

**G31 的复验结论是「同源了，故本仓 0 行改动」**：回货形态是一条匿名件 `content_origin(widget_origin, tf) = widget_origin +
tf.translation` 同时成为绘制侧（`render_into` → `paint_content`）与命中侧（`Widget::hit_test` / `hit_test_chain` 的
`self_box.origin`）的唯一原点算式，`HitNode.origin` 与交给 `covers_own_extra_hit_box` 的 `ancestor_offset` 都取它，
而 `Align` / `Offset` / `Padding` 三种平移一律由 `Modifier::transform` 折进同一份 `translation`，即三条回货判据逐条对上。
本仓面板从未自算平移补偿，故生产代码零改动，出账动作只有**翻判据**：
`the_dispatched_local_position_of_a_padded_row_child_is_shifted_G31` →
`a_real_click_below_the_enclosing_row_picks_the_option_under_it_G31`（真点行外那一段的第一条选项带即选中该档、收起面板、
只落盘 1 次广播 0 次），G30 例里「行的记录 origin ＝ 其内容顶」由登记时的差 8 dp 转为逐位相符（容差 1 dp）。
**「可达」与「点准」各由一条用例守的分工经变异实测有效**：把命中侧那两处 `content_origin(bounds.origin, tf)` 单点退回
`bounds.origin` 而**不动绘制侧**那一处，两例各在自己那一句转红（origin 报 `134 vs 142`、差值恰 8 dp，且 `selected_index()`
停在原档、落盘 0 次）；若两侧同退则是**等价变异**（两边一起挪，关系照样成立），故变异点必须单侧——这一点写进裁决以免下棒重复踩。

**G32 的复验结论是「撤补偿，且登记时那句口径是错的」**：`Node::~Node()` 不再清 `layout_parent_`，摘除全部改走父侧
（`detach_child_layout_parent` 只在 `child->layout_parent() == this` 时清以免误清别人的父链，
`detach_all_children_layout_parent()` 遍历可写的 `child_nodes_mut()`；调用点是 `~Container` / `~SingleChild` 的
**析构体首行**加 `remove_child` / `adopt_children` / `set_children` / `set_child` 四条换子路径——放基类 `~Widget`
会因派生部分已析构而分派到空实现、静默漏清）。本仓据此让 `refresh_chain_candidates()` 里那层卡片祖先补脏退役
（`card_builder_` 成员、赋值与头注一并删除），**撤除后 `itest_settings_panel` 33 例全绿**：两条候选用例都在一次真实滚动
批次之后才按指针身份取候选按钮，故父链不再被抹断、那层补脏不再承重。变异自证反向也做了——再去掉叶子那一处
`mark_needs_layout()` 则 `typing_in_the_chain_filter_narrows_the_pool_without_touching_the_form` 与
`the_candidate_append_is_open_until_the_chain_reaches_the_framework_capacity` **同红**，即「`show` 是测量输入而不是脏源」
那条承重补脏保留。**一处自我更正入册**：G32 登记时本行所写的「那只 `LayoutBuilder` 的补脏是为了 `show` 翻转立刻生效、
而不是为了让脏爬上断掉的父链」与裁决 7.62③④ 及代码注释②自陈相反；实测结论是两处各守一件事，已在附录 A.2 该行就地更正。
该仓的变异自证要求（「只在应用侧顺手多标一层祖先脏而不清断链 ⇒ 转红」）与本仓这次撤除是同一句话的两侧。

**同批复验撞出一条诊断缺陷，登记 G34**：`detach_all_children_layout_parent()` 由容器析构体首行调用，而那一刻子控件仍被
容器持有、尚未析构，于是 `detach_child_layout_parent` 对**每一个子节点**都走进告警分支——与该函数自己的注释
（「被清的是仍存活控件的父指针，真正销毁控件的路径不经本函数」）正好相反，即 G32 的回货判据 ② 只做到「断链可见」而没做到
「只在异常时可见」。实测行数（同一二进制，按 `layout parent detached` 计数）：`itest_settings_panel` 33 例 **3696 行**、
`itest_right_click_paste` 77 行、`itest_workspace_layout` 32 行，而 `utest_config` 与 `itest_render_viewport` 各 0 行——
刷屏量与「反复重建 widget 子树」的次数成正比，足以把一次真断链埋在噪声里。同处第二条小缺陷：`AURORA_LOG_WARN` 是类型安全
可变参数**拼接**而非 printf，故文案里的 `%s` 原样输出、`child->type_name()` 附在整句末尾（实测以 `... render rootText`
结尾）。本仓按 §5「不等不绕」**不消音、不改级别、不给 `~Container` 打补丁**，可派发任务书已随裁决 7.64⑦ 在会话内产出
（判别式建议取既有 a11y 守卫同口径的 `Node::use_count() == 1`）。

**两处排期判据随之改口**：#114 余件 A4（字体族下拉）待人拍板的理由从「一条形态约束 + G31 漂移」收窄为**只剩那条形态约束**
（外加「本机 200+ 族如何挑选与排序在两侧都无依据」，裁决 7.64⑨）；#130 面板行区真机走查的三条候选机制里
「布局父链断裂」一条随 G32 回货被结构上排除，未结那一腿（真机真实滚轮）不变（裁决 7.64⑧）。#115 实时预览盒的落位已由人
拍板取**卡片底部横条 140 dp**，其代价（卡片高度预算与行区可视行数减少）与形态写进裁决 7.64⑩ 并在
`UI_SETTINGS.draft.md` §1 / §4 就地更正。

**验收**：`itest_settings_panel` **33 例全绿**，非 e2e 通道 `ctest -E etest_` **37 项全绿**（44.56 s）；三次变异注入各有
转红证人（命中侧单点退回 → G31 例与 G30 例各红一句；去叶子补脏 → 两条候选用例同红），每次重链前删 `build/**/*.ilk`。
Aurora 树复验后已还原为 `1b3fe58c` 且 `git status --porcelain` 为空。**未复跑吞吐门禁**（本棒只在面板打开与结构变更时
生效，不进每帧绘制与布局路径，基准三场景不含面板）。真机走查照旧未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），
故本条不宣称面板「可用」。

**改动面**：生产代码 `src/ui/settings_panel.{h,cpp}`（撤补偿，只减不增）、用例 `tests/integration/itest_settings_panel.cpp`
（翻判据 + 文件头 ⑦⑩ 两段改写）、文档（`SPECIFICATIONS.md` §7 裁决 7.64 与附录 A.2 的 G31 / G32 改闭合形态 + 新增 G34 行、
`PLAN.md` §6 三条与 §2 的 `PREF.02` 行、`AGENTS.md` §6 缺口账目与本棒条目、本条、`UI_SETTINGS.draft.md` §1 / §4）。
#114 余件（A4 待拍）与 #115 / #116 / #117、#130 走查结项、G33 / G34 回货复验照旧在册。

---

## v0.63（2026-10-05）**面板行区真机走查开一轮未结 + 登记 G33**：三条假设被读源与无头实测排除，一条无障碍读数缺陷被证成缺口，本棒零代码改动（裁决 7.63）

**动机**：v0.62 那句「真机走查未做」在会话解锁后开跑。走查面取的是设置面板行区的滚动——它是 #114 三棒里唯一一条「无头断的是派发、真机才断得出像素」的判据。

**结论一条：本棒不结项，也不登记产品缺陷。** UIA `IScrollProvider::Scroll` 驱动终端页行区，`verticalPercent` 从 0 走到 100 而窗口像素逐位不变；三条候选机制全部被排除——(a) 布局父链断裂（G32 那类）经无头上溯证伪，链的终点是 `OverlayHost`；(b) 脏区记错坐标空间经 `dirty_bounds()` 与 `paint_bounds()` 同源比对证伪；(c) Display List 冻结经读源证伪（`Window::run_paint` 的 partial-clip 分支显式 `set_skip_dl_record(true)` 后整树重绘）。剩下的唯一决定性实验是真机**真实滚轮**落该行区，而前台被一个非本进程的窗口挡住（`activate_window` 三次 `input_failed`），面板重开同样须前台走 `Ctrl+,`，故该腿登记在册（任务 #130）而不是拿推断结项。

**取证通道先自证**：WGC 截屏在同一进程实例上先拍到浮层四页、后拍到浮层关闭后的终端，故「画面没变」不能推给取像陈旧（本机既有 `PrintWindow` 字节陈旧的教训属另一通道）。

**一条判据成色欠账就地登记**：G27 消费例断的是派发（偏移非零、命中认领者已换）而非像素，于是「偏移动了而屏幕不动」这一类现状在本仓通道结构上抓不到；#130 结项时须补一条像素证人。

**已证缺陷登记 G33**：Aurora 的 `UiaNodeProvider::get_VerticalViewSize` 返回 `max/(max+1)*100`（`max` ＝内容 − 视口），不是 UIA 规定的「视口占内容的比例」，实测 99.2126% 反解恰为 126/127 而无头独立量得本仓行区跨度正是 126 dp。**这条读数当场把本棒的一次推断带偏过**（「视口≈内容 ⇒ 没什么可滚」），故它不是纸面洁癖而是会改判走查结论的缺陷；修法须先给 `accessibility_scroll()` 补视口/内容两个量，任务书已在会话内产出。同文件 `get_VerticalPercent` 算式正确、`get_HorizontalViewSize` 恒 100 属自陈，均不在本条射程。

**改动面**：只有文档（`SPECIFICATIONS.md` §7 裁决 7.63 与附录 A.2 的 G33 行、`PLAN.md` §6 的 G33 行、本条）。零代码、零用例、未复跑构建与门禁（无代码可跑）。#114 余件（字体族下拉待拍 A4）与 #115 / #116 / #117 照旧在册。

---

## v0.62（2026-10-05）**回退链重排区段（S8 / A5）落地 + G30 接货复验出账**：面板第四类专用区段可编辑，同批到货把「追加命中盒」升级为子树聚合，于是可达之后新撞出一条坐标病灶与一条所有权缺陷（裁决 7.62）

**动机**：#114 的第二棒。判据文 S8 拍的是「`ReorderableList` + 添加族入口」，A5-a 拍的是「链条目达框架上限即关死追加口」，
而 v0.56 把回退链留成只读值摘要的占位行。开工时同批到货的还有 **G30**（Aurora 当日活动分支 `dev-1.0.0-alpha.9.uat.3` 的
`3db3e578`：追加命中盒沿祖先链聚合 + 面板翻转限高与几何读数改走权威通道），故本棒一并做接货复验并按预诺翻那条钉子。

- **回退链区段的四条框架实测决定了形态**（7.62①）：⑴ `ReorderableList::on_layout` 在**无界约束**下回落 `Size{320, 480}`，
  而行区给每一行紧约束宽 + 高度自定 ⇒ 不锁高就是一个 480 dp 的空洞，故区段按 `clamp(条目数, 1, 可见档数) × 32 dp` 显式锁高；
  ⑵ `set_drag_handle(true)` 的那条 48 dp 手柄带由**列表自留**（`on_hit_test_chain` 对带内落点返回空表，框架注释自陈条目自带
  `Button` 会消费 Press 令整项拖拽起不来）而列表**不画 grip** ⇒ 三枚按钮落在 `width - 48` 之左、带内由本件自绘点阵；
  取 `drag_handle(false)` 则条目里的按钮全部抓不到 Press，故不可选；⑶ 面板的 `Escape` 挂 `ShortcutScope::Global` 而全局快捷键
  **先于任何控件**消费（7.51③ 理由 (a)），故链正被抓在键盘上时 `Escape` 会关掉面板——处置是关闭闭包里先试
  `cancel_keyboard_grab()`，返真即原地不动，属**交接而非缺口**（同 7.58 对 chrome setter 的判法）；⑷ 指针拖拽落位要经每帧 `tick`
  （`end_drag()` 只在 `reduce_motion` 或位移不足 0.5 dp 时立即提交），无头通道不跑帧泵 ⇒ 换位判据一律走上移 / 下移按钮与键盘
  Drop 两条同步路径，拖拽归框架自有用例与真机走查，本件不伪造绿灯。
- **G30 的回货形态是「查询深度」而不是「控件有没有申报」**（7.62②）：`covers_extra_hit_box()` 从「只问直接子自己申报」升级为
  「以本控件为根的**子树**是否覆盖此局部点」（自身申报 ∪ 逐子按 `bounds().origin` 折算下探，折算式与 `on_hit_test_chain` 的下降式
  逐字同构）；自身入链另走恒 O(1) 的 `covers_own_extra_hit_box`（把中间容器一并拽进链会改变链的组成）；三入口都带**有默认值**的
  `ancestor_offset`，故 `Dropdown` 的翻转判据拿得到全局顶边而不读绘制期的 `focus_bounds_`（那份在显示列表缓存命中时被跳过写入）。
  `Dropdown::PanelGeometry` 由此可上翻、可限高、面板内部可滚。登记时量过的两个探点（行盒下沿 `+4 dp` 可达 / `+12 dp` 不可达）
  现在**都**进链，那条残段钉子因此按预诺翻成正向行为用例，并保留「探点确在所在行之外」的反空转前提。
- **框架对 z 序不对称给的是形态约束而不是修法**（7.62②⑤）：`on_paint` 前向绘制而链逆序下降，覆盖绘制的面板仍会被**更晚绘制**的
  兄弟行压住；`05-event-navigation.md` §3.2.2 据此立「**列表行内不得使用覆盖绘制不占布局的控件**」，推荐替代是挂 `OverlayHost` /
  `Popup`。这条约束与本仓**既有的 5 行 `Dropdown`** 以及待拍的 A4 相撞，本棒**登记而不自解**：不改挂载点、不把下拉换成 `Popup`
  （那属另一种产品形态），A4 的「本稿不预先拍」原样保留，只是待拍的理由从「三处技术限制」换成「一条形态约束 + 一条 G31 漂移」。
- **两处框架物理事实各挡了一次「改了就以为生效」**（7.62③）：⑴ `Widget::show` 是**测量输入而不是脏源**——`Widget::layout` 先登记
  `layout_parent_`、再在 `show == false` 时回零盒**早返回**（早于缓存判定），写 `show` 不触发任何脏，故候选池翻转后祖先按缓存复用
  旧尺寸；⑵ **只标叶子到不了渲染根、标场景根也不够**——脏标记沿 `layout_parent_` 上溯且遇 relayout boundary 即止，而整树重排只穿透
  「真正跑了 `on_layout` 的节点」的直接子链接，缓存有效的中间层早早返回、不重测子树，故首轮「只标场景根」的修法**实测无效**
  （推断被实测推翻）。落成两处补脏（被翻转的按钮 + 卡片外层那只 `LayoutBuilder`，新增只读成员 `card_builder_`），两次变异注入
  各去掉一处都转红，证明两处都承重。
- **新登记两条缺口并按 7.13① 各出一份可派发任务书**（7.62④）：**G31**——祖先的 modifier 平移没并进命中链的逐节点 origin
  （绘制侧 `render_into` 加 `tf.translation`，命中侧只加 `bounds.origin + cb.origin`，而下降时的包含判定用的却是已减掉平移的坐标），
  于是本仓每一行那 8 dp 上内边距没进 `HitNode.origin`，`Dropdown` 按 `local.y` 反算选项序号**整体下移一格**，症状从「点不到」变成
  「点不准」；证人**刻意拆两条**（可达的正半 / 点不准的现状钉子），合成一例就会在任一腿回货时不知道该翻哪一半。**G32**——
  `aurora::Node::~Node()` 结尾**无条件** `set_layout_parent(nullptr)`，而 `Node` 是该文件自陈的**可共享句柄**（只有 a11y 事件用
  `use_count() == 1` 守卫），于是一只临时句柄析构就把仍存活控件的布局父链抹断，此后该支的脏标记在上溯时被**静默丢弃**
  （StrictMode 关闭；框架自己的断言把这一类命名为「the layout parent chain is broken」）。实测触发点是面板滚动批次之后卡片
  `Column` 的 `layout_parent()` 变空。本件不为断链做长期补偿（§5 第 2 条），那处补标 `LayoutBuilder` 是为「翻转立刻生效」。
- **一条登记时口径的实测更正**（7.62⑥）：`Button` **没有**覆写任何命中入口（`button.h` 只有 `wants_click()` 与 `on_pointer_event`），
  故**禁用态按钮仍在命中链里、照样扫得到**，7.61 尾段那句「扫不到它」不成立。后果是「点它不动」不能以「派发扫描取不到」为据，
  只能真点一次并判**计数不变**（落盘 / 广播各 1 次不动），且须另断「浮层仍在」以排除「点击被遮罩接走」这一同数不同因。

**验收**：`tests/integration/itest_settings_panel.cpp` **26 → 33 例**（链区段六例：区段按 `form_` 当前链投影且提示文案随长度走、
上移 / 下移 / 移除各一次提交、任一次结构变更只落盘 1 次广播 1 次、过滤输入只收窄候选池而**不动表单**、达框架上限
`AURORA_TEXT_FALLBACK_CHAIN_MAX`（8）即关死追加口（A5-a）、`Escape` 先交还键盘抓取；另 G31 现状钉子一例，与按预诺翻正的 G30
正例；各例在 `#else` 分支留同名 `AURORA_TEST_SKIP` 占位），非 e2e 通道维持 **37 项全绿**；三条变异注入各有唯一证人
（去掉 `card_builder_` 的补脏 / 去掉被翻转按钮的补脏 / 去掉达上限即关死）。每次重链前删 `build/*.ilk`。
**未复跑吞吐门禁**：新代码只在面板打开与链结构变更时生效，不进每帧绘制与布局路径，基准三场景不含面板。
判据文 `codespec/UI_SETTINGS.draft.md` 的 A5 / S8 两条就地更正（含「添加族」入口由「下拉」改取常驻候选池的理由与那条形态约束），
A4 的「本稿不预先拍」保留。真机走查仍未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），且面板既有 5 行 `Dropdown` 与新立的
形态约束尚未了结，故本条不宣称面板「可用」；#114 余件（字体族下拉待拍板）与 #115 / #116 / #117 照旧在册。

---

## v0.61（2026-10-05）**外观页 A1 / A2 两个专用区段落地**（八张主题卡 + 16 格色板 + S6 的整份重置与逐槽恢复默认）：判据文两处措辞按代码现状就地更正，并随这次真点击实测登记一条命中链物理事实与两条空转证人（裁决 7.61）

**动机**：#114 的第一棒。面板本体第一棒（v0.56）把主题行与色板行留成**只读值摘要的占位行**，判据文 A1 / A2 两条要的
是可交互的主题卡与逐格色板；S6「切主题＝整份 palette 取新主题默认 + 每槽一个『恢复主题默认』」在 v0.53 把两条运行期
入口备好之后，剩下的就只是面板侧这一段行为。开工前按裁决 7.13① 逐条读 Aurora 当日活动分支公共头复验，**真缺口 0**
（两段都是「画布 + 文本 + 一只常驻输入框 + 一枚按钮」的公共 API 组合，属裁决 7.13② 的「留在本仓」；G30 只挡下拉的溢出区，
本棒没有下拉）。

- **判据文 A1-a 的两处措辞按代码现状就地更正**（7.61②③）：「卡名的显示名」＝**存储键名**（`dracula`），因为 `themes.h` 的预置表
  只有 `name` 一个字段、显示文案走 StringTable 是未落地规划、而**G28 实测到「框架查表失败的回退是空串」**——用查表取卡名会把
  「词条缺失」呈现成「一张没有名字的卡」，另立一张显示名表则是 schema 之外的第二份真值源；「第三格＝强调」在 `PaletteSpec` 里
  无对应字段且八套预置的 `ChromeOverride::accent` 全为空（裁决 7.26②），故那一格取**光标色**（未配回落 `default_foreground`）。
  代价如实登记：界面出现英文小写名，中文显示名属后续 i18n 工作。
- **A2-a 实现为「把那只常驻输入框指向所点的那格」**（7.61④）：`Modifier` 没有可见性位（裁决 7.60③），按创建/销毁表达
  「只有选中格有输入框」就得整块重建浮层，把滚动偏移与输入焦点一起抹掉；给 16 格各配常驻框又正是本条禁止的形态。落成
  `selected_swatch_` 一个整数 + `sync_swatch_editor()` 一条单向同步，故「编辑器的文本」与「表单里那一格的值」结构上不可能分叉。
  **两条代价**：选中格首版**不画**「指向」标记与格序号（用户只能靠「恢复默认之后哪一格回位」反证）；本件为判据开四只读观测点
  （`theme_cards` / `swatch_slot` / `swatch_input` / `selected_swatch`），代价是生产面多一组装配层永不调用的访问器，而**忘记
  `mark_needs_paint()` 这类纯绘制陈旧在像素层之外结构上抓不到**。
- **一条命中链物理事实随真点击现形**（7.61⑤）：`Widget::hit_test_chain` 只收「自身可点、或含可点后代」的节点，故**纯展示的
  `Canvas` 永远不会被真实派发交回**——第一次写「真点一张主题卡」时链上的认领者是卡名 `Text` 的祖先 `Stack`，画布那一档根本不可达。
  处置是把同一个 `clickable` 闭包挂在**画布自己身上**（它已 `fill_max_size` 铺满整张卡，落点在卡名、样例还是留白上都命中它），
  外层 `Stack` 那份保留；派发按 deepest→root 冒泡且 `is_handled` 即 `break`，故两层各挂一份同一闭包不会双触发。**推论**：凡
  「画出来的区段又要能点」，可点语义必须落在绘制者身上；判据侧要认领者一律按链取，与裁决 7.56⑧ / 7.60② 同族。
- **两条空转证人的实例，各配一次「先绿后红」的实测**（7.61⑥）：⑴ 判据比「动作之后的值 vs 存储侧当前值」时快照**必须取在动作之前**
  ——替身的 `base` 会随**成功落盘**推进，于是「切主题顺手改掉三枚用户开关」这种实现首轮**全绿**（实现把两边一起改了），加固成点前
  快照后才转红；⑵ 「恢复错格」要结构可见，前提是先**真点**另一格把编辑器移开（缺省指向 0 格，而「恢复 0 格」在缺省现场下恰好
  与「恢复选中格」同一），且改动那一步必须走**编辑器 + 失焦**的真实通路而不是 `commit_slot()` 的编程入口——后者不动编辑器文本，
  「恢复之后编辑器必须回位」那句判据就被「编辑器一直是基线值」蒙过去。两处各实测过一次绿，按裁决 7.49⑥ 的口径不结项而去找空转处。
- **一条基线语义的澄清**（7.61⑦）：**成功落盘会把基线推进到刚写出的那份**，故「改回原值」相对新基线是一次真改动（多一次落盘）；
  `a_reverted_change_neither_persists_nor_broadcasts` 判「不多发」是因为它中间那次落盘**失败**。两处判据的差别只在这一个变量上。
- **S6 与无基线现场的兑现形态**（7.61⑧⑨）：`apply_theme()` 一次写完**六键**（主题名 + `palette.basic` 整表 + 前景 / 背景 / 光标 / 选区）、
  整份表单一次落盘、成功且档位即时时一次广播，而三枚用户开关**一字不碰**（`themes.cpp` 一套都不带，故不由主题派生）；任一键被表单
  拒绝即整次既不落盘也不广播，原因写进主题行状态列。「恢复主题默认」只动当前指向那一格，其余 15 格逐格比成「改动前那份落盘值」。
  「自定义」按 A1-b 挂**主题行状态列**且判定覆盖全部**五档 palette 槽**（16 格 + 四张单格槽；只比 16 格的实现首轮为绿）；无基线现场由
  用例侧清空候选表制造，按钮经框架 `set_enabled(false)` 灰置且点击不落盘，禁用态读框架 `wants_click()` 而不为面板开第二道观测面。

**验收**：`tests/integration/itest_settings_panel.cpp` 20 → **26 例**（六例新用例里两例是纯模型断言（卡的顺序/名字/样例/选中态唯一、
「自定义」角标的归属与覆盖域），四例须 `AURORA_BACKEND_HEADLESS`（真点一张卡、真点一格、真点「恢复主题默认」、无基线现场的禁用态；
`#else` 分支按既有 `AURORA_TEST_SKIP` 形态留同名占位），非 e2e 通道维持 **37 项全绿**；**十二条**实现侧变异注入各有转红证人（卡名换成显示名形态 / 样例取错档 /
选中态不唯一 / 六键写漏一档 / 逐键 flush 而非整份一次 / 切主题连开关一起改 / 点格不移编辑器 / 恢复错格 / 无基线仍可点 / 角标另起一卡 /
只比 16 格的自定义判定 / 恢复或切档之后编辑器不回位），其中三条首轮为绿、加固后转红（开关快照那一处、⑵ 的两处各一次）。
每次重链前删 `build/*.ilk`。**未复跑吞吐门禁**：本棒代码只在面板打开时生效、不进每帧绘制与布局路径，基准三场景不含面板。
判据文 `codespec/UI_SETTINGS.draft.md` §2 的 A1-a / A1-b / A2-a 三条就地更正，§8 的「切主题的 S6 整份重置」一项由未落转为已落。
真机走查仍未做（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」；#114 余两件（字体族下拉——形态仍待拍且其
20+ 档候选正同时踩 **G30** 的残段与 `Dropdown::panel_box` 无上限那条限制、回退链重排表 S8）与 #115 / #116 / #117 照旧在册。

---
## v0.60（2026-10-05）**面板行区容器由 `LazyList` 改为 `Scroll` + `Column`**：#114 的可变行高前置，并随这次换容器实测登记一条坐标模型陷阱与两条判据写法教训（裁决 7.60）

**动机**：#114（外观页四类专用区段：主题卡 / 16 格色板 / 字体族 / 回退链）的区段各自要占多行高度，而 `LazyList` 的 `item_extent`
是**全局**固定行高，结构上表达不出混排。框架真缺口 0（G30 已另行派发，与本棒无涉）：`Scroll` + `Column` 属裁决 7.13② 的
「公共 API 可组合」，且滚动容器的内容进得了真实命中链已由 **G27** 回货闭合（裁决 7.57③），故本棒不等不绕、也不登记缺口。
代价是每页 ≤30 行一次全量实例化（无回收），换来可变行高。**本棒零新增功能**，交付面＝一处容器替换、一处随之暴露的真缺陷、
以及用例侧的坐标来源重建。

- **换容器把坐标模型一起换掉了**（7.60②）：框架在 `scroll.h` 的「几何与命中契约」里自陈两条不对称——`Scroll` 的内容子节点
  bounds 是**内容坐标**（命中时把局部点**加上** `offset_y_` 换算回去），`LazyList` / `GridView` / `LazyRow` 写的是**视口坐标**
  （`index × extent − offset`）。于是本套件里「拿 `paint_bounds().origin` 当窗口坐标用」的每一处，换容器之后都偏一个「视口顶边 −
  滚动偏移」。处理是不再信任任何坐标假设：新增 `Harness::reachable_box()`，以 1 dp 步长在**真实派发结果**上量出该控件的窗口坐标
  可达矩形，探针点 / 点击点 / 取色点全由它推；`paint_bounds()` 此后只用于取**尺寸**。
- **判据写法教训之一：松判据会被坐标错误静默吞掉**（7.60③）——把取色点退回 `paint_bounds().origin` 做变异注入，**20 例全绿**。
  原因是 `chrome_is_dark` 判的是「不亮」，而卡片底、导航列、遮罩全都够暗，取错点的读数照样满足判据。故把「取色点归该控件所有」
  升级为取色的**前提**（`probe()` 内先 `REQUIRE(hit(x, y) == spot.widget)` 再读像素），加固之后同一条变异注入恰在该例转红。
  这条自我更正顺带问出了 v0.58 那四处像素证人的成色：**像素判据的成色取决于取点来源，来源必须由派发链自证**。
- **判据写法教训之二：挑样启发不是判据**（7.60④）——`find_first` 的 `room_below_dp` 形参注释原写着「否则判据测的是视口裁剪而不是
  派发」，而把它的余量算式退回内容坐标做变异注入**同样全绿**：被挑中的那一行两种读数都在视口内，该参数只是「扫描时挑哪一行」
  的启发，结构上守不住注释里那句话。故那句反空转前提改为写进两条下拉用例正文的显式断言，形参注释就地更正。
- **顺带修掉一处真缺陷**（7.60⑤）：卡片重建尾部的 `status_texts_.assign(rows_.size(), nullptr)` 是 `LazyList` **懒构建**时代的配套
  （清空后由条目构建器逐行重登记），换全量实例化之后构建与这句话在同一次布局里先后发生，于是它把刚登记的行状态列指针一律抹掉。
  已删除并就地留 WHY 注。
- **G30 的形态随嵌套加深一格而量值未变**（7.60⑥）：`Scroll → Column → Row → Dropdown` 之后 `Dropdown` 从「直接子」降成曾孙，而那条
  缺口的机制（闸只问直接子自己的申报）与量到的两条边界（自身盒下沿 `+4 dp` 可达、`+12 dp` 不可达）**逐字照旧**，任务书无需改写；
  附录 A.2 的 G30 行只就地更正嵌套路径。
- **新增一例 G27 的生产路径消费证人**：`a_row_control_still_commits_after_the_row_area_has_been_scrolled`——三档滚轮之后偏移非零、
  同一点上的认领者已换（这一句是本例的承重前提，少了它就只是「读到一个非零偏移」）、按量出来的新位置真点一行开关即落盘且脏标记推进。
- **验收**：`tests/integration/itest_settings_panel.cpp` 19 → **20 例**，非 e2e 通道维持 **37 项全绿**。七条变异注入读数——**转红四条**：
  开关关闭态轨道退回框架浅色缺省 `{180,180,180}`（只 chrome 例红）／`ScrollProps::step = 0`（只新增那一例的两行红）／G30 探点
  `+12 → +4`（只钉子例的三行红）／G29 探点退回自身盒内（只正向例的前提红）；**等价三条并如实登记**（按 7.49⑥ 不伪造证人）：
  去掉行区 `Scroll` 的 `.expand()`（框架「容器自身取父约束给出的视口尺寸」，该 modifier 在几何上本就不承重）／取色点退回内容坐标
  （加固后才转红）／`room_below_dp` 退回内容坐标。每次重链前删 `build/*.ilk`。
- **未复跑吞吐门禁并给理由**：行区构建只在面板打开时发生，不进每帧绘制与布局路径，绘制算式零改动；基准三场景不含面板。
- **仍待人工 / 未落**：#114 的四类专用区段本体、#115 预览盒、#116 状态栏开关组与快捷键只读表、#117 降级对话框与三条会话侧构造期
  注入接缝、**G30 回货**，以及面板的**真机走查**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。

---

## v0.59（2026-10-04）**G28 / G29 回货的接货复验**：面板三枚按钮原样交回 `LocalizedString`、下拉的钉子翻成正向行为判据，同时读出「**回货只闭合一层**」并新登记 **G30**（追加命中盒不沿祖先链上传）（判据入册为裁决 **7.59**）

**动机**：v0.58 收尾时在册的两条待回货同日到货（Aurora 分支 `dev-1.0.0-alpha.9.uat.3` 的 `7e15577c` 闭合 G28、`15d9aee2` 闭合 G29；同批 G26 / G27 的 `ec0af2ef` 已在 v0.57 复验）。接货复验判的不是「代码到了没有」而是**本仓那条判据现在能不能翻成正向行为**，而 G29 一翻就翻出半个结论：回货形态只把**直接子**的申报并进了祖先那道闸，孙辈的申报不上传，于是面板里「嵌在行中的下拉」仍有一段画得出而点不到。按裁决 7.13① 的「不等不绕」，这段残段登记为 **G30** 并当场出任务书，本件不自造覆盖层、不改挂载点、不把下拉换成 `Popup`。

- **G29 的回货形态逐条读实**（7.59②）：新增 `Widget::extra_hit_box(ctx)` 且缺省 `nullopt`（未覆写的控件与改动前**逐位等价**，这是本条能安全落地的关键）＋非虚公共入口 `covers_extra_hit_box(local, ctx)`；祖先的下降闸在**两条入口**里都并进了该申报（`Container::on_hit_test` 与 `Container::on_hit_test_chain` 逆序、`LazyList::on_hit_test_chain` **前向**以与绘制顺序一致）。`Dropdown` 的覆写取它与 `on_hit_test` **同一份** `panel_box()` 算式，故「控件申报」与「控件自认」结构上不可能分叉。v0.56 那条「浮层可命中只能按链判、按兼容入口判会永久假阴」的判据边界**仍然成立**，只是链里现在也认追加盒。
- **钉子翻正向：真点一个选项即选中并落盘**（7.59③）：终端页 `terminal.ambiguous_width` 那一行的下拉，在展开后按链命中其**自身布局盒之外**的选项带、真实单击即选中另一档、收起面板、并把提交交回表单。落盘 1 次而广播 0 次不是遗漏，是该行「接缝待开 ∧ 下次会话生效」经 `apply_scope()` 折成只落盘（判据文 B3-a）。三条前提逐条 `REQUIRE` 钉住（申报覆盖该点、该点在自身盒外、链上确实命中该控件），缺任一条后面的行为断言就会测到别的东西；装载基线刻意预置成 1 号档 `Wide` 而点 0 号档，否则「点了但值没变」与「根本没点到」在断言上同形（框架对同值不发回调）。
- **只闭合一层，且量出了边界**（7.59③④）：行盒 `56 dp`、上下内边距各 `8 dp`、行给下拉的紧约束盒 `40 dp`、面板贴着框架 `box_height_`＝`30 dp` 铺开、选项行高 `26 dp` 未改 ⇒ 探点在盒下沿 `+4 dp` 可达，在 `+12 dp`（越过行盒下沿）不可达。残段那条新用例写的是**形态证明**而不是「没点上」：控件申报覆盖（真）＋控件兼容入口也认（真）＋**唯独祖先闸不认**——三句合起来才是 G30 的内容。追加盒沿链聚合成子树并集回货后本例必须转红。
- **同病灶另两条随 G30 一并登记**（7.59⑤）：① `Container::on_paint` 前向绘制而 `on_hit_test_chain` 逆序下降 ⇒ 列表里后画的兄弟行把先画的下拉面板**压住**，「点得到而看不见」与「看得见而点不到」并存；② `Dropdown::panel_box` 无上限、不滚动、不向上翻转，长候选列直接溢出窗口——**外观页的字体族下拉（20+ 档）正踩这一条**，故 #114 开工前须先有 G30 的结论或人的裁决。
- **G28 的回货与一处「证人守不到什么」的实跑登记**（7.59⑥⑦）：`Button::resolved_label(ctx)` 成为该控件 `on_layout` / `paint_label` 的**唯一**显示串来源（解析结果连同实测宽高缓存进 `cached_display_text_`），`accessibility_label()` 复用同一份缓存，故面板三枚按钮撤掉规避、交回 `LocalizedString`，`settings_label()` 只留给本就收 `std::string` 的入口（`Command::title`、`Text::placeholder`、角标）。**失败模式经实测是空串而非「显示原始 key」**（查表失败回退实例自身 `text`，而 `tr()` 造的实例那份恒空），故证人以「非空 ＋ 逐字等于本件按同一张表查出的六条之一且命中数为 1」为判据。**一条边界不靠推理**：把按钮标签改回本件预先解析的 `std::string` 形态（回货前的写法），该套件 **19 例全绿**——两种形态给出同一显示串，故本例守的是「交出 `LocalizedString` 之后仍有人查表」，不是回货形态本身；按 7.49⑥ 的口径如实登记而不伪造只抓后者的断言。
- **接线形态与一处消音**：面板的浮层序号成员改 `std::optional<std::size_t>` 并按 `has_value()` 守卫（`add_overlay` 的返回值随回货改口径，无基础内容时给 `nullopt`，「0 既是哨兵又是合法序号」那次撞号就此由框架侧消除）；用例里两处「占住基础内容」的 `add_overlay` 调用按本仓既有 `(void)` 口径消掉 `[[nodiscard]]` 告警。
- **验收**：`tests/integration/itest_settings_panel.cpp` 17 → **19 例**（正向行为例 ＋ G30 残段钉子 ＋ G28 标签查表例，各带 `#else` SKIP twin），非 e2e 通道 **37 项全绿**。**四条变异注入各有唯一证人且各自转红**：正向例探点退回 `+12`（只该例的祖先链前提那一行红）／残段例探点退到 `+4`（钉子那一行红，且「既不选中也不提交」两句随之转红，证「不可达」不是同义反复）／不交 `Dropdown::set_on_change`（只落盘计数与落盘内容两行红）／关闭按钮 key 写错一位（只 G28 例的非空与命中数两行红）。每次重链前删 `build/*.ilk`。**未复跑吞吐门禁**：本棒改动只在面板打开时生效、不进每帧绘制路径，绘制与布局算式零改动。
- **仍待人工 / 未落**：**G30 回货**（任务书已随本条在会话内输出；回货前外观页与终端页的下拉只有「仍属所在行」那一段可点，长候选列另有溢出窗口那一挡）、#114 专用控件（主题卡 / 16 格色板 + 逐槽恢复默认 / 字体族下拉 / 回退链重排——其中字体族下拉的形态取决于 G30）、#115 预览盒、#116 状态栏开关组与快捷键只读表、#117 降级对话框与三条会话侧构造期注入接缝、以及面板的**真机走查**（含 v0.58 登记的那五档无像素证人的色值），故本条不宣称「可用」（会话锁屏下 `SendInput` 静默失效，裁决 7.31①）。

## v0.58（2026-10-04）设置面板的 **chrome 色交接**落地（S5 的本仓侧那一半）：四件框架控件十五个色值 setter 一次交接齐，并登记两处只有读实现体才现形的物理事实与一条**判据边界**（只有四处底色有像素证人）（判据入册为裁决 **7.58**）

**动机**：v0.57⑥ 那条自我更正（`Switch` 的关态轨道色并非硬编码，setter 全在公共 API 上）把「面板控件的 chrome 色交接」从「等框架回货」改判为**本仓侧工作**，按裁决 7.13② 留本仓、不出任务书。本棒兑现它，并把兑现过程中撞到的两条物理事实与一条判据边界如实登记。同棒并带附录 A.2 的 **G29 行**一处口径 sharpening：链路径的 `self_hit` 那个式子**不查几何**，而兼容入口 `on_hit_test` 在派发路径上**从不被调用**，故控件自陈的外扩命中盒对派发一句都不生效——修复须让「自身判定」与「祖先下降门」改问同一个命中盒钩子（任务书 v3 的落点据此更明确）。

- **一处交接、没有第二条通道**（7.58①）：`build_control()` 是面板给四件框架控件着色的唯一地点，控件只由该行构造一次，故结构上不存在「构造时一套色、运行期另一套色」的分叉。
- **一处真缺陷落在既有 15 例的判据面之外**（7.58②）：框架 `TextInput` 的聚焦态底色缺省 `{245,248,255}` 近白，而本件文本色 `kText{248,248,242}` 同样近白——不显式给就是**聚焦那一刻白底白字**。既有面板用例从不发聚焦点击，这条自 v0.56 落地起无人照到。处置是把聚焦态底色与未聚焦同取 `kControlBg`，聚焦**只靠 `kAccent` 描边分档**，于是 `set_focused_border_color` 随之前者为承重件而非装饰。
- **行的紧约束会把开关拉成白饼**（7.58③）：行盒 56 dp 扣掉上下各 8 dp 内边距后给子项的交叉轴是**紧约束 40 dp**，而 `Switch::paint_thumb` 的滑块直径按自身盒高算（`d = height − 2×inset`），于是未锁高度的开关是 36 dp 白色圆盘、轨道只剩两侧细边。这不是框架缺陷（控件按盒定形是其既有语义），处置在应用侧 `Modifier{}.height(24.0F)`；像素判据把「盒高 == 24」写成**前提**而非结论，因为底色探针全落在盒内分数坐标上，盒形一变就测到别的东西。**这条是本轮唯一由新用例当场抓到并修好的问题**（首版实现下该例红，探针解码出的色是 `{248,248,242}` 即滑块本身）。
- **判据线两侧都有实测出处**（7.58④）：框架各控件浅色缺省里**最低**一档是开关关态轨道 `{180,180,180}`，本仓 chrome 里**最深**一档输入底是 `kControlBg{40,42,54}`，故 `kChromeFloor = 0x60` 这条「三通道都不亮于它**且不透明**」的钳位既能把每一处浅色缺省读成红，也不会把本仓自己的深色底读成红；`alpha == 0xFF` 那一半把「帧缓冲取不到」与「底色够暗」分开（缺缓冲时探针返回 `alpha == 0`）。
- **判据边界（本件的核心代价，不得读成「色交接已守住」）**（7.58⑤）：交接了**十五个**色值 setter，而新例只守得住其中**四处底色**（`TextInput` 未聚焦 / 聚焦、`SpinBox` 框底、`Dropdown` 主框、`Switch` 关态轨道）。余下五档各有结构性不可判的理由：`border_color` / `focused_border_color` 是 **1 dp** 描边，且 `kCardLine` 的蓝道 `99` 恰在钳位之上，盒内任一点要么测到描边要么测到底色、无法两全；`text_color` / `arrow_color` / `thumb_color` 是**墨色**，与底色在同一条「不亮」判据里互相冒充；`cursor_color` 随闪烁相位（内置 500 ms）变；`placeholder_color` 只在空值时绘制而行控件恒有值。这五档只由代码本身保证，回归风险形态是「将来加控件时漏一个 setter」，登记为**真机走查项**而非测试守住（与 7.49⑥、7.56⑨ 那两条「不伪造证人」同口径）。
- **两条刻意前提，缺一会把正确实现读成红**（7.58⑥）：开关只取**关闭态**那一枚（开启态轨道是本仓强调色 `kAccent{189,147,249}`，蓝道 249 高于钳位——为此给 `Harness::find_first` 新增按实例的 accept 谓词）；`Dropdown` 在**切页之后**才问，而切页是整块重建，此前各例拿到的控件指针到那一句即不可再用。用例里 `is_focused()` 那句是本例前提而非附带观察：没真的聚焦就没走聚焦态那条绘制分支。
- **验收**：`tests/integration/itest_settings_panel.cpp` 16 → **17 例**（新例及其 `#else` SKIP twin；驱动台另补 `pixel()` / `probe()` 两个取帧缓冲的观测点），非 e2e 通道 **37 项全绿**。**六条变异注入各有唯一证人且各自转红**：不交 `focused_background` / 不交 `TextInput` 底 / 不交 `SpinBox` 底 / 不交 `Dropdown` 主框 / 不交 `Switch` 关态轨道 / 去掉 `Modifier{}.height(24.0F)`（最后一条失败读数 `Which is: 40 vs 24`）。每次重链前删 `build/**/*.ilk`。**未复跑吞吐门禁**：新增 setter 只在面板打开时生效、不进每帧绘制路径，绘制与布局算式零改动。
- **仍待人工 / 未落**：G29 的回货（回货前面板的下限行按只读值摘要呈现，钉子用例 `a_dropdown_option_below_the_layout_box_is_not_dispatch_reachable_G29` 届时必须转红）、G28 回货后 `settings_label()` → `settings_text()`、#114 专用控件（主题卡 / 16 格色板 + 逐槽恢复默认 / 字体族下拉 / 回退链重排）、#115 预览盒、#116 状态栏开关组与快捷键只读表、#117 降级对话框与三条会话侧构造期注入接缝、以及面板的**真机走查**（含本件那五档无像素证人的色值），故本条不宣称「可用」。

## v0.57（2026-10-04）**G26 / G27 回货的接货复验**：多行粘贴确认框补三条走生产 `Dialog` 的真实点击腿，面板浮层判定**不**迁回 `Dialog`；外观页专用控件棒的框架面读源另登 **G29**（覆盖绘制区的派发不可达），并附一次**病灶口径的自我更正**（判据入册为裁决 **7.57**）

**动机**：v0.55 派发的 G26 / G27 同日回货（Aurora 分支 `dev-1.0.0-alpha.9.uat.3` 的 `ec0af2ef`）。接货不是「拉一下代码就完」——那两条登记的后果之一是**既有已落地路径**（多行粘贴确认框）的 Yes/No 永不可点，而它的既有全部用例都以替身接缝绕开了真对话框，于是回货本身也需要走生产形态的证人来判它是否真的闭合。同一棒还承接了「还有没有其他框架需要补充的点」这次审计：把外观页四个专用控件（主题卡 / 色板 / 字体族下拉 / 回退链重排表）所要用的浮层与命中语义逐条读实现体，读出一条新缺口 **G29** 与四条**不构成缺口**的结论。

- **回货形态**（7.57①）：`Dialog::on_layout` 在打开态把居中内容盒写入 `set_bounds`，且与 `on_paint` **共用同一次折算**（两处各算一遍就会分叉，这正是 G26 的病灶）；`on_hit_test_chain` 在内容盒内下降、遮罩区回 `{this}`、关闭态回 `{}`，且遮罩**刻意不**触发 `on_close_`。`Scroll` 另覆写链入口并把局部命中点**加上** `offset_y_` 折回内容坐标（其类注释自陈「钉驻绘制刻意不把偏移烘焙进 bounds」，故换算落在命中侧）。同批另有 `LazyList::set_count(int)` 到货，本仓那条「切页须整表重建」的在册理由随之失效一半（下半句仍成立：同一序号在另一页对应另一个键）。
- **证人走生产呈现形态而不走替身**（7.57②）：给 `TerminalView` 开只读观测点 `multiline_warning()`（与既有 `context_menu()` 同一条理由——按钮位置由框架 `Column`/`Row` 折算，用例拿不到对话框的盒就只能自造一套按钮尺寸算式去猜哪一枚是 Yes），于是三例得以落成真点击：真点 **Yes** 即把剪贴板原文按块序发完并收起（浮层仍挂 1 个，收起不等于摘掉）、真点 **No** 一个字节不发且再右键会重新问、点**遮罩**既不答话也不穿透（`Dialog` 自身在链上吸收）。三例认「哪一枚是 Yes / No」以 `accessibility_label()` 为据而非纯按序号，只按序号就会把「框架换了按钮次序」读成通过。**变异自证**：删答话后的 `close()` → Yes / No 两例红而遮罩例绿（它本就不测收起）；删 `show()` → 三例全红；删 `add_overlay()` → 三例连同既有那条「等答话」例同红。
- **判定：面板浮层不迁回 `au::Dialog`**（7.57③）：三条根据——`Dialog` 给内容的是 `self × 0.8` 的**内层约束**而面板要的是整窗遮罩 + 按可用尺寸钳 `[1040, 680]` dp 的卡片；其遮罩不触发 `on_close_` 而 S1 拍的是「点遮罩即关」；`show()` 带 `FocusManager::push_scope` 的模态作用域而 S1 明确是同窗口**非模态**浮层。既有 15 例证人的挂载点是 `Stack` + `Column` + `LazyList`，迁回即全部重写而判据不变，属「为回货而回货」。故 G26 / G27 就此**出账**，而 7.55④ 那句「回货后若改用 `Dialog` 需再动一次面板的挂载点」就地更正为**不需要**；多行粘贴确认框同理保持现形态。
- **新登记 G29，并附一条自我更正**（7.57④）：本棒初判写成「控件只在兼容入口 `on_hit_test` 自陈覆盖区，链入口没覆写」，无头实测把它**推翻**——`Container::on_hit_test` 与 `Container::on_hit_test_chain` 两条入口都在递归前按 `child.bounds().contains(local)` 判包含，故从场景根出发时**两个入口同样进不到**那块溢出区（同一探点在两入口给出同一个**非**下拉控件）。真正的不对称是**根 vs 嵌套**：挂在根上时 `Widget::hit_test_chain` 以 `self_hit = wants_click() && !hit_shrunk` 让控件自身入链，溢出区侥幸可达——症状随挂载位置而变，因此修复落点必须在**祖先那道闸**，任务书 v3 的变异自证要求据此写成「只补控件侧 `on_hit_test_chain` 而不动闸 ⇒ 新增用例仍必须红」。同批并入第二条：`OverlayHost::add_overlay()` 在空宿主上返回 `children_.size()-1` 恰为 **0**，而 `remove_overlay(0)` 以「0 = 基础内容」拒绝，于是空宿主的首个浮层永远摘不掉（本仓的规避是给宿主恒装一个基础子节点，故不阻塞本仓、对「宿主初始为空」的使用者是硬伤）。
- **一条现状钉子而非行为判据**（7.57⑤）：`itest_settings_panel` 补 `a_dropdown_option_below_the_layout_box_is_not_dispatch_reachable_G29`，分两头断——按局部坐标直接问控件**会**命中（声明在），按场景根走链**不**命中（派发不到），且真点那一点既不选中也不收起。**它随 G29 回货必须转红**，届时改成「真点一个选项即提交」；若面板先改走浮层自绘选项（规避件），它同样转红，那是删钉子的信号而不是 bug。变异自证：把探点从盒外挪进盒内 → 那一处 `CHECK_NE` 转红，证明这条判据测的是边界而不是常量。
- **四条非缺口 + 一处本仓侧自我更正**（7.57⑥）：`DatePicker` / `TimePicker` / `ColorPicker` 覆写 `wants_click()` 且在自身盒内绘制故可达；`ReorderableList` 是 `Container` 子类、自带链覆写与把手命中带、会写子 bounds，并暴露 `is_dragging()` / `drag_index()` / `set_drag_handle()` / `item_count()` 与键盘重排，**#114 的回退链重排表前置成立**；`ThemeScope : Provider<Theme>` 的 `inherit_theme(ctx)` 取最近祖先，故「样例卡随终端主题、chrome 不随」可分树实现；`Canvas` 的 100×100 自动尺寸回落是文档化事实且可控制。**更正**：v0.56 在册的「`Switch` 关态轨道色硬编码、无 setter」不成立——`set_inactive_color` / `set_thumb_color` / `set_border` / `set_track_size` 都在公共 API 上，只有**禁用态**色值硬编码，于是「面板控件的 chrome 色交接」属**本仓侧工作**而非缺口。实测真正的色值盲区是 `Dropdown` 的四色（主框/边框/文本/箭头缺省为浅色档）与 `TextInput` 的 `focused_background_`（缺省近白，配深色文本会在聚焦那一刻白底白字）。
- **验收**：`itest_right_click_paste` 17 → **20 例**、`itest_settings_panel` 15 → **16 例**，非 e2e 通道 **37 项全绿**；每次重链前删 `build/**/*.ilk`（Debug 预设 `/INCREMENTAL` 的陈旧 exe 陷阱）。本棒无绘制与布局算式改动，**未复跑吞吐门禁**。
- **仍待人工 / 未落**：G29 的回货（本棒已出任务书 v3；回货前面板的下限行按只读值摘要呈现，其可编辑本体属 #114）、G28 回货后 `settings_label()` → `settings_text()`、面板控件的 chrome 色交接（本仓侧，含 `TextInput` 聚焦白底白字那一处真缺陷）、#115~#117、以及面板的**真机走查**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称「可用」。

---

## v0.56（2026-10-04）设置面板**本体第一棒**落地：四页骨架 + 六类通用行控件 + 落盘/广播三条接缝的兑现形态、词条表用框架 i18n 的三个必要装填条件，并新登记 **G28**（`Button` 标签绘制不经 i18n 查表）

**动机**：v0.50~v0.54 落了面板的五件纯逻辑前置，v0.55 把浮层形态的前提读实（G26 / G27 与回货前的 `Stack` + `LazyList` 组合）。本棒是 `SPEC.FEAT.PREF.02` 的界面腿本体第一棒：把「四页骨架 + 通用行控件 + 三条接缝」做出来并可验证，专用控件（主题卡 / 16 格色板 / 字体族下拉 / 回退链重排表 / 快捷键只读表）按裁决 7.52 的分工留给后续棒，在本棒是**只读值摘要的占位行**。判据入册为裁决 **7.56**。

- **面板不认识 `config` 也不认识 `TerminalView`**（7.56①②）：装载 / 落盘 / 广播三条接缝以 `Hooks{load, persist, broadcast}` 交装配层兑现，后两条形参是**整份表单**而不是单键——逐键接缝会把一次面板会话拆成 N 次 `Store::replace()`（每次一个文件写），而落盘的本意是「一次替换 + 一次 flush」。广播的档位判定**按行**、落盘**按整份**：只看本次提交那一行的 `apply_scope()`，广播出去的却是整份表单。
- **脏标记的比较基准是「最后一次成功落盘的那份」**（7.56③）：于是「改 → 落盘成功 → 再改回来」是**第二次写**而不是撤销第一次，用例拆两条腿各证一半。面板没有 undo，也没有把磁盘回滚的义务——这条边界不写清就会被读成「撤销应回退文件」。
- **浮层的两条物理前提**（7.56④）：`OverlayHost` 给浮层的是**松约束**而 `Canvas` 的自动尺寸夹到 100×100，故遮罩层必须挂 `Modifier{}.fill_max_size()` 才铺满整窗（不挂就是稿上「点窗口边缘关不掉」那块死区）；`add_overlay()` 返回 `children_.size()-1`，**空宿主时恰为 0**，与面板「无浮层」哨兵 `overlay_index_ == 0` 相撞，故驱动台必须给宿主一个基础子节点。卡片经 `LayoutBuilder` 钳 `[1040, 680]` dp，1500×900 dp 的稿面在 960×640 dp 窗口里不溢出。
- **词条表用框架 i18n，三个装填条件缺一不可**（7.56⑤，`src/ui/settings_i18n.{h,cpp}`）：词条 key **就是落盘点号路径**（面板不维护第二张「键 → 标签」表，而框架查表失败回退 `text`、`tr()` 的 `text` 恒空，故「新增一行忘配词条」的后果必须落在用例的覆盖率判据上而不是界面上的静默空标签）；`install_settings_strings()` **必须在任何控件绘制之前**；**必须把 `Locale{"zh"}` 设成表的缺省档**——框架 `Text` 无 `Provider<Locale>` 时按 `Locale{}`（即 `en`）查表，而 `lookup` 是「首级 tag 未命中才回落缺省档」，不设就是全空串。
- **新登记 G28：`Button` 的标签绘制不经 i18n 查表**（7.56⑥，附录 A.2）：`ButtonProps::label` 是 `LocalizedString`，而 `widgets_paint.cpp` 的 `paint_label` 两处取的都是 `label.get().text`，同件 `accessibility_label()` 却走 `resolve(&default_string_table(), ...)` 并自陈「与 `paint_label` 所绘一致」——该句实测不成立，后果是**屏幕空白而朗读出译文**。按裁决 7.13① 属渲染路径上的公共 API 不一致，**任务书已在本会话内产出**。本棒规避形态：按钮文案一律 `settings_label()` 就地解析、`Text` 一侧仍交 `LocalizedString`；`Switch` / `SpinBox` / `Dropdown` / `TextInput` 的文本属性本就是 `std::string`（无查表路径，不属本条）。**G28 不阻塞面板**。
- **打开入口走命令层而 `Escape` 不走**（7.56⑦，架构 §11.2）：`au::Command{id="settings.open", default_binding=Ctrl+,, scope=Global}` 经 `app.commands().add()` + `bind_shortcuts(app.shortcuts())` 登记，快捷键 / 将来的菜单项 / 命令面板共用同一真源；面板的 `Escape` 另经 `ShortcutRegistry` 直接登记，因为命令层的绑定是启动期一次性而那条 `Escape` 必须在关闭时**解绑**（留着就静默吞掉发往会话的 `Esc`，vim / tmux 是受害者）。
- **一条判据写法纪律**（7.56⑧）：浮层行区控件的可命中性只能用 `Widget::hit_test_chain(...)` 判，不能用兼容入口 `hit_test()`——`LazyList::on_hit_test` 刻意返回自身（其注释自陈「滚轮须落在整视口」），真实派发走 `on_hit_test_chain` 才下降到活条目。本棒一度据此怀疑是自己写坏了布局。**这不是框架缺口**（两条入口的语义差异是自陈设计），但判据不与派发同源就会永久假阴，与 7.55③ 那条「替身测的是接线而不是框架」并列在册。
- **验收与变异自证**（7.56⑨）：`tests/integration/itest_settings_panel.cpp` **15 例**（13 例走行表与三条接缝，2 例须 `AURORA_BACKEND_HEADLESS`：遮罩铺满整窗且真实点击关闭、文本行失焦才提交），`utest_settings_catalog` 随新增的单位列（`SettingsControl::unit`，只给视觉稿逐字画出的四行、内边距那行不留）补例；非 e2e 通道 **37 项全绿**。**十二条变异注入各有唯一证人且各自转红**（每次重链前删 `build/**/*.ilk`）：脏标记守卫 / `note_persisted` / 广播档位门 / `Escape` 由 `Global` 改 `Focus` / 关闭不解绑 / 遮罩不 `fill_max_size` / 失焦不提交 / 切页不过滤 / 延后角标那一腿 / 装载行过滤 / `Absent` 不置灰 / 「未配」退化成空文本。**一条诚实登记的探针局限**：把「失焦才提交」的反面注入（`set_on_changed` 也接 `commit_text`）**不会转红**，因为框架 `TextInput::set_value` 不触发 `on_changed_`（只有键盘编辑与 `accessibility_replace_text` 触发）；故「逐字符不提交」那一腿只有正面证人而无变异证人，按裁决 7.49⑥ 的等价注入同档登记，不伪造绿灯。
- **装配层随本棒补两条搬运**：`make_appearance()` / `make_interaction()` 从配置装出视口的外观包与交互项，**启动时的初始构造与面板的即时广播共用同一对函数**，于是「改字号立刻生效」与「启动时就是这个字号」结构上不可能分叉。
- **本棒未复跑吞吐门禁，理由要说准**：门禁的被测主体是**上屏层自身的每帧成本**（裁决 7.34），而 `tools/bench/render_throughput.cpp` 与既有像素用例各自**自建场景**，都不经 `src/main.cpp` 的场景根，故本棒的上屏侧改动（生产场景根多包一层 `ThemeScope`、面板 TU 只在打开时被读写）**不在被测量路径内**——复跑只会得到同一份未改动代码的读数。视口与网格的算式零改动。
- **仍待人工 / 未落**：外观页四个专用控件的可编辑本体（#114）、右侧实时预览盒（#115，S7）、状态栏开关组与快捷键只读表（#116）、损坏配置的启动对话框与三条会话侧构造期注入接缝（#117，`TODO(SPEC.FEAT.PREF.07)` 与 §7 那条）、G26 / G27 的回货与随之改回的浮层挂载点、以及面板的**真机走查**（取色、拖拽重排链、逐槽恢复默认三处手感；会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称「可用」。

---

## v0.55（2026-10-04）面板本体开工前的框架侧**实现面**复核：判据文 §6 的「真缺口 0」更正为 **2**，新登记 **G26**（`Dialog` 内容不可点）与 **G27**（`Scroll` 内容不可点且偏移不进命中判定），并连带在册一条已落地路径的缺陷（判据入册为裁决 **7.55**）

**动机**：`SPEC.FEAT.PREF.02` 的四件纯逻辑前置（v0.50~v0.54）已全部落地，面板本体开工的唯一框架侧依据是裁决 7.52 的 §6 那句「本棒向框架派发的真缺口数 = 0」。开工第一步是把那节结论所依据的**每一件浮层与滚动容器读到实现体**——因为面板的形态（全屏浮层 + 可滚行区 + 模态对话框）恰好全部落在那两件控件上。读源结果与该结论相反。

- **判据文那一节的方法缺陷要说清，否则下一次还会犯**（7.55①）：§6 逐条读的是**声明面**（公共头有没有这件入口、那个字段），而这两条要读到**实现体**才现形。`Dialog(Node)` / `Scroll(ScrollProps{.child})` 的 API 看起来完全够用，断裂在 `on_layout` 少写一笔 `Node::set_bounds`。框架的几何权威只在 `Node::bounds_`（其 `widget.cpp` 布局尾注自陈「几何权威完全收敛到 `Node::bounds_`……位置由父节点经 `Node::set_bounds` 写入」「Widget 不再持有任何几何缓存」），派发权威 `Container::on_hit_test_chain` 按 `child.bounds()` 判包含，故**容器不写这笔就「画得出而点不到」**；绘制仍正确是因为这两个容器各自在 `on_paint` 里现算了几何（Dialog 居中、Scroll 离屏缓冲平移），于是**视觉与命中各用一份几何**。据此得到一条口径：**凡新增「框架控件能否承载交互」的判据，必须读到该控件的 `on_layout` / `on_paint` / `on_hit_test*` 三处，只读声明面不足以支撑「属可组合」的结论**。
- **实测形态与正对照**（7.55②）：一次性无头探针（400×300 dp、`scale` 1.0，用完即删不入库，与既有的 MinGW 一次性探针同口径）。Dialog 侧内容 Column 的绘制盒是原点 x=171.5、宽 57 dp，而该子节点 `bounds().size.width == 0`；在绘制盒中心经真实 `au::EventDispatcher` 发 Press/Release，按钮回调不触发；该点命中链长度**恰为 0**（连 Dialog 自己都未入链，因为容器把自身入链的前提是「后代链非空」）。Scroll 侧其子节点 bounds 是 **0×0**，而内容 Column 自身及其第一子的 bounds 完好（宽 400 dp）——断裂恰在容器→内容那一层，内容子树内部几何是好的；命中链最深者是 `"Scroll"` 而非那个按钮。**正对照**：同一份内容根直接挂会写 bounds 的 `Column`，同一条派发路径**能**命中。没有这一例，两个「不触发」都可能只是探针接线不对，故正对照是这条实测的一部分而非附注。
- **连带在册一条已落地路径的缺陷，并把它定性为判据空洞**（7.55③）：`src/ui/terminal_view.cpp` 的多行粘贴确认框取 `au::Dialog` + `aurora::confirm`，其 Yes/No 永不可点；命中链为空还意味着该次点击**穿透**到对话框下方的视口，症状是「想点确认却起了一个选区」。既有 `itest_right_click_paste` 的相关用例全部经 `Presentation::confirm_multiline` 替身绕过真对话框，所以这条自 2026-10-03 落地起在册而无人守。教训：**替身接缝测的是接线而不是框架**，浮层类件须至少留一条走生产呈现形态的点击腿。修法（同一套 `Stack` + `Column` 组合 + 补一条真实点击腿）是独立意图，登记为紧随其后的单独一棒，**本棒不顺手修**。
- **回货前的形态拍板**（7.55④）：面板浮层与 §7 的降级对话框改用**确实写 bounds 的容器**组合——`Stack`（遮罩层 + 卡片层，`Alignment` + `StackFit::Expand`）承载 `Column` 卡片，行区取 `LazyList`（写 bounds 且覆写链下降）而非 `Scroll`。按裁决 7.13② 这属「用现有公共 API 组合出来的交互体验」而**不是**框架分叉，故不违 AGENTS.md §5 第 2 条；G26 / G27 因此**不阻塞面板开工**。代价如实登记：多一层本仓自持的浮层组合，回货后若改回 `Dialog` 要再动一次挂载点，故面板把浮层构造收在一处而不散落。
- **两条框架侧任务书已在本会话内产出**（供另一 agent 执行，按裁决 7.13① 派发）：G26 的回货判据是「`on_layout` 打开态把居中内容盒写入 `set_bounds`，且 `on_paint` 与命中链**共用同一次折算**」+ 两条无头用例（点内容中心按钮 → 回调触发；点遮罩区不落进内容）；G27 是三条（写 bounds、覆写链并减去 `offset_y_`、含「滚到 `offset=200` 后点击仍命中视觉那一行」的用例）。本仓**不等不绕**：面板用 ④ 的组合形态推进，两条回货后只改挂载点不改判据。
- **文档回写**：`codespec/SPECIFICATIONS.md` §7 新增裁决 **7.55**（五条 + 任务书去向）并把该节标题的追加范围补到 7.55；附录 A.2 新增 **G26 / G27** 两行（各含原始事实、实测读数、影响面、阻塞条目与回货判据）；`codespec/PLAN.md` §6 新增两行并标「不阻塞 `SPEC.FEAT.PREF.02` 开工」；`codespec/UI_SETTINGS.draft.md` §6 标题与结论句就地更正（原「真缺口 0」）、第 8 条整条划废并改写、§7 降级对话框那句「包进 `au::Scroll` 再交入 `Dialog`」作废并改指 §6 第 8 条末句的新形态。`AGENTS.md` §6 的现状条目随面板本体那一棒一并回填（本棒只有文档，无代码改动）。
- **验收**：本棒零代码改动，非 e2e 通道维持 **36 项全绿**（探针文件已删除，`git status` 只剩并行在途的标签条棒四处未跟踪文件与 `src/CMakeLists.txt` 那一行，本棒不触碰）。**无新用例可配**：探针要证的两个事实属于框架，回货后由 Aurora 侧的无头用例长期守；本仓一侧的证人（面板行区可点、确认框真实点击）随面板本体与那条 `fix:` 棒落地，届时按 §4.4 补 `itest_*`。版本脚注 v0.54 → v0.55。

---

## v0.54（2026-10-04）设置面板实现棒第五件：**`Settings ⇄ FormValue` 的搬运件**落成，7.52 在册那条「本件守不到两个同域成员互换」的单向边界由此闭合（落地该稿 §8 补出的第四件前置，判据入册为裁决 **7.54**）

**动机**：v0.50~v0.52 落了 §8 的三件纯逻辑前置，v0.53 落了 S4 的两条运行期入口，面板本体开工前只剩一件事没有载体：**把 `config::Settings` 的成员搬进搬出 `ui::SettingsForm`**。表单件按**落盘点号路径**放值、不认识成员，故 v0.52 如实登记了一条边界：「两个同域成员互换」（`appearance.font_line_height` 与 `appearance.font_letter_spacing_dp` 都是实数）在它结构上抓不到，当时的证人是 §8 集成那条像素差分。那条判据太重也太窄（只覆盖色板一格），本棒把搬运本身落成可单测件，边界因此改由逐键判据守。

- **落点在 config 侧，而不是面板界面腿**（裁决 7.54①）：`config::form_entries()` / `config::apply_form()`（`include/borealis/config/form_transfer.h` + `src/config/form_transfer.cpp`）。放 ui 侧就是 `config ⇄ ui` 模块环（`config/settings.h` 已 include `ui/palette.h` 等本域头，7.32① / 7.41① 同因），`config → ui` 才是允许方向；于是 `ui` 侧照旧只认「路径 + 自己的 `FormValue`」，**成员名在全仓只出现在这一处**。
- **一行 = 一个键的两个方向**是本件唯一的防分叉手段（7.54②）：`Binding{key, get, set}` 逐叶子键一行、次序照 `ui::settings_catalog()`（58 行 = 外观 30 / 终端 12 / 连接 15 / 快捷键 1），取侧与写侧共用同一行，键 ↔ 成员的对应只写一次，两个方向不可能各指一个成员。
- **分工守住三处不越界**（7.54③）：**不复检取值域**（7.46② 的归属，域由装载侧与表单件把守，本件只折算类型，形态不合的值不改基线）；**未装载的键保留基线**（表单给不出值的键——缺键与错型键——写回时不动 `base`，因为「面板少画一个控件」不等于「把该项设置抹掉」）；**不碰存储、不判脏**（`apply_form()` 取当前值而非脏键名单，未改的键写回同值故幂等，「要不要落盘」仍归面板的 `has_unsaved_changes()` / `note_persisted()` / `Store::replace()`，即 7.52 的 S3①）。
- **枚举落盘名从装载侧迁出成私有头**（7.54④）：那批「枚举值 ↔ JSON 文本名」原先只住在 `src/config/store.cpp` 的匿名命名空间，本件要同一批名字，留在原处就是让第二处自己再列一遍，而落盘名一旦分叉，面板选中的档经 `replace()` 写回再读就成了别的档（正是 7.46② 反复避开的第二真值源）。故迁进 `src/config/schema_names.h`（`EnumName` + 七张表 + `value_of` / `name_of`），**刻意不进 `include/borealis/`**——公共头上开一份枚举名表等于把它变成第二份契约，而契约是 `settings.h` 的字段与落盘 JSON。两条未命中口径与装载侧同向：`name_of` 未命中给空串即**该键不写盘**、`value_of` 未命中**保留基线**。
- **判据按方向配齐三件、另两条跨件，其中第三件是那一类错误的唯一观测通道**（7.54⑤）：取侧逐键字面量（一份逐键互异的配置摊出 58 个显式预期）、写侧逐键唯一移动（只提交一键，其余 57 键一字不动）、**布尔两族直接读写 `Settings` 成员名**（布尔只有两档值，前两件对「整行互换的两枚同值布尔」结构上抓不到——互换后每个键看到的值仍与预期一致——故 20 行探针表逐键点名，取侧与写侧各一例）；跨件两条是「下拉候选经**配置文件**往返」（本件与 `store.cpp` 共用那张名表，拼写漂移在读回时现形，并同时转红 `utest_settings_catalog` 的白名单例，即名表单源的跨套件证人）与「未配色槽往返成未配而不是黑色」（7.27③ 的 A2-b 在搬运层的兑现）。
- **验收**：`tests/unit/utest_form_transfer.cpp` **十例**（覆盖关系与装载报告恒干净／取侧 58 键字面量／整份写回等值重建／只提交一键／布尔两族各一例／未配色槽往返／未装载键保留基线／候选经配置文件往返／快捷键覆盖表两方向往返）。**八条变异注入各有转红证人**：取侧互换两个实数行（四例红，而 `utest_settings_form` **全绿**——正是本件补上的那条边界的直接证据）／写侧互换（回读三例红）／布尔整对互换而键不变（取侧逐键例 + 两族各一例红）／删一行绑定（覆盖关系例红并点名 `terminal.word_delimiters`）／落盘名漂移 `osc_wins`→`osc`（本套件三例 + `utest_settings_catalog` 白名单例）／色板取侧反序（四例红）／未配取侧回落黑色（三例红）／未配写侧折成黑色（两例红）。每次重链前删 `build/**/*.ilk`。**一条用例写法教训**：行数判据须先 `AURORA_TEST_REQUIRE_EQ` 再逐格按序比——`AURORA_TEST_CHECK_EQ` 不终止用例，删一行绑定之后的 `entries[index]` 是越界读，变异读数会成一次崩溃而不是一个红用例（首轮实测如此，加固后才是干净的红）。非 e2e 通道 **36 项全绿**（本套件新注册一项）。**本棒未复跑吞吐门禁**：新增 TU 不在任何绘制与布局路径上（只在面板打开时被读写一次），算式零改动。
- **框架缺口 0**（按裁决 7.13① 复核）：本件的三份材料（键路径、值形态、配置成员）全在本仓，**无一处调用 Aurora API**，故本棒不属「渲染与事件链路上的缺口」也不属「大型 BUG」，**不出任务书**；面板本体的框架面复核已在 #111 做过并登记为真缺口 0（裁决 7.52⑥）。
- **文档回写**：`codespec/SPECIFICATIONS.md` §7 新增裁决 **7.54**（七条：落点、一行两方向、三处不越界、名表迁私有头、判据三件加两条跨件、验收与八条变异、用例写法教训与两处代价）并把该节标题的追加范围补到 7.54（含上一棒漏记的 7.53）、`SPEC.FEAT.PREF.02` 的落地现状改为「四件纯逻辑前置」；`codespec/UI_SETTINGS.draft.md` §8 新增 **④** 并写清五点处置，同时把 ③ 里那句「成员搬进搬出发生在面板界面腿、其证人只是集成那条像素判据」就地更正为指向 ④；`codespec/ARCHITECTURE.md` §11.1 在该表之后新增一段说明搬运件的 config 侧落点、模块环理由与 `schema_names.h` 的私有头形态；`AGENTS.md` §2 的 `include/borealis/config/` 与 `src/config/` 两处清单及测试行入册、§6 新增本棒现状条目；`codespec/PLAN.md` §3 的 `SPEC.FEAT.PREF.02` 行改为「四件纯逻辑前置已全部落地，余下只剩面板本体与 §7 两处 TODO」。就地更正两处代码注释：`include/borealis/ui/settings_form.h` 与 `tests/unit/utest_settings_form.cpp` 的说明头（该边界已由 ④ 的取侧与写侧逐键证人守住）。
- **仍待人工 / 未落**：**面板本体** `src/ui/settings_panel.{h,cpp}`（第三个触达 `au::Painter` / `au::Widget` 的 TU：S1 浮层、四页骨架与延后角标、S2 的 `Global` Escape、S5 的 `ThemeScope`、S6 整份重置与逐槽恢复默认、S7 真实绘制路径预览盒、S8 回退链 `ReorderableList`、S10 状态栏十开关、S12/S13 损坏配置启动对话框）、`config/store.h` 的 `TODO(SPEC.FEAT.PREF.07)`、`src/main.cpp` 的三个会话侧注入接缝，以及面板的**真机走查**（取色、拖拽重排链、逐槽恢复默认三处手感；会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。版本脚注 v0.53 → v0.54。

---

## v0.53（2026-10-04）设置面板实现棒第四件：**S4 的两条运行期更新入口**落成（落地裁决 7.52 的 S4，判据入册为裁决 **7.53**）

**动机**：v0.50~v0.52 落完 `codespec/UI_SETTINGS.draft.md` §8 的三件纯逻辑前置，剩下的第一块实现就是 7.52 边界 (a) 点名那条——「全仓**没有任何给面板用的运行期更新接缝**，`ui::TerminalView` 的七个外观与交互入参全部挂在构造函数上」。面板要兑现人已拍板② 的「外观域 + 终端域全量即时」，就必须先有这两条入口；本棒只落接缝与其判据，面板本体仍在其后。

- **载体是一份 `Appearance` 聚合体，构造与运行期更新共用**（裁决 7.53①）：六项（`palette` / `ref_font` / `typography` / `padding_dp` / `blink_period` / `font_fallback_chain`）一次给全，于是「构造吃七参、更新吃另一套字段」的第二条通道不存在；既有的七参构造保留为**委托适配器**（六处构造点零改动，含并行在途的装配层），面板能造出 `Appearance` 之后装配点改取聚合形态。另两案的否掉理由在册：**重建视口**会丢选区、回看偏移、焦点与主线程的 `ScreenMirror` 副本（都不是可重放的量），**「下次启动生效」**违 `SPEC.FEAT.PREF.02` 的「修改即时生效」。
- **三条代价逐条落成动作而非口头约定**（7.53②③④）：① 换链必须紧接触发度量重取——框架的 `with_fallback_chain(...)` 给的是**整份**排版选项，装上即把量化字距与固定格推进清成缺省，故 `install_fallback_chain` 之后紧跟 `metrics_stale_ = true`，让 7.50③ 那条「三者同源」的不变量在运行期同样成立；② 闪烁周期走「取消-重排」整条换掉，且**刻意不设** `active()` 前置守卫（`TimerHandle::cancel()` 对未注册句柄幂等、析构本就无条件 `cancel()`，加守卫反而让「挂载时无调度器、调度器后到」那一档永远注册不上）；③ 改内边距或字号**只标脏**，下发仍走 `on_layout` → `request_grid_size` → 注入的 sink，静默窗口与合并都留在工作区层（7.47⑩）——框架的布局闸门是 `first_frame_ || layout_dirty_ || size_changed || !dirty_boundaries_.empty()`，纯绘制脏不重排，故 `mark_needs_layout()` 是承重那一句。
- **交互项入口只换值、不标脏**（7.53⑤）：`copy_on_select` / `copy` / `word_delimiters` / `paste` / `right_click` 五项逐条实测都是**用取时现读**，无一项参与绘制；标脏只会让画面无谓重画一遍，故本入口不置任何脏位（这条把「即时生效」与「重绘一帧」两件事分开）。
- **验收**：`itest_render_viewport` **+5 例**（现 32：调色板槽重绘且选区文本与回看距底行数一字不动／字号 + 内边距同改后格网重取、无半格且只下发一次且下发值等于用例侧独立复算的格网／装链后「档位 + 量化字距 == 几何格步长 × scale」照旧成立／周期改档后每 tick 恰好翻一相，并在缺省周期下先断 200 ms 内一格不翻以作参照／复制变换下一次读即生效且存下的端点不被回头改写）、`itest_right_click_paste` **+1 例**（现 17：运行期把三态从 `CopyOnSelect` 换成 `ContextMenu`，只改「下一次右键」的处置——既不丢既有选区也不补发上一次的动作）、`itest_workspace_layout` **+1 例**（现 17：面板里连改两次字号，静默窗口内 0 次下发、尾沿一次交出最新格、另一格 0 次）。**十条变异注入各有转红证人**：删 `install_fallback_chain` 调用（链例红）／删 `metrics_stale_`（字号例 + 链例两红）／删 `mark_needs_layout()`（同上两红）／删 `ref_font_` 存值（字号例红）／删 `padding_dp_` 存值（字号例红）／删 `if (blink_changed)` 的重注册（闪烁例红）／删 `blink_timer_.cancel()`（闪烁例红，旧句柄未取消会在旧周期到期那一 tick 多翻一相）／删 `options_` 存值（视口交互例 + 右键例，跨两套件）／删 `spec_` 存值（调色板例红）／`request_grid_size` 绕过 sink 直发（工作区新例 + 既有三例红）。每次重链前删 `build/**/*.ilk`。**一句等价分支的处置反转**：曾写的 `if (!blink_timer_.active()) return;` 守卫在删前后各跑一遍全绿，而删除后覆盖更广（多出「调度器后到」那一档可注册），故它不是被证住的分支而是被证为多余的分支，按 7.49⑥ 同口径直接删除而非留档伪造证人。非 e2e 通道 **35 项全绿**。**本棒未复跑吞吐门禁**并给理由：新入口不在每帧路径上（只由面板触发），绘制与布局算式零改动。
- **框架缺口 0**（按裁决 7.13① 逐条读 Aurora 当日活动分支公共头复验）：`TimerHandle::cancel()` / `active()`、`Scheduler::set_interval`、`TextLayoutOpts::with_fallback_chain`、`Widget::mount` 的 `mounted_` 幂等、布局闸门的四个条件与 `mark_needs_layout` 的传播全部在既有语义内，属裁决 7.13② 的「公共 API 已可组合」，故本棒**不出任务书**。
- **文档回写**：`codespec/SPECIFICATIONS.md` §7 新增裁决 **7.53**（七条：载体、三条代价的落地动作、交互入口不标脏、验收与十条变异、驱动台口径）并在 `SPEC.FEAT.PREF.02` 补一段落地现状（S4 已落、面板本体未落）；`codespec/UI_SETTINGS.draft.md` §5 的 S4 标注已落与四条落地形态、§8 集成判据的前三条标注**主视口腿与分屏腿**已有证人（「两个视图」里的预览盒那一半、切主题的 S6 整份重置、损坏配置的启动对话框三条明写仍待面板本体）；`codespec/ARCHITECTURE.md` §9.2 的接缝清单补两条运行期入口与其状态迁移边界；`AGENTS.md` §2 / §6 入册本棒现状；`codespec/PLAN.md` §3 的 `SPEC.FEAT.PREF.02` 行改为「S4 已落，余下是面板本体与降级对话框」。
- **仍待人工 / 未落**：面板本体 `src/ui/settings_panel.{h,cpp}`（第三个触达 `au::Painter` / `au::Widget` 的 TU，含 `Settings ⇄ FormValue` 的搬运以闭合那条「两个同域成员互换」的单向边界）、`config/store.h` 的 `TODO(SPEC.FEAT.PREF.07)` 与 `src/main.cpp` 的三个会话侧注入接缝、S7 的实时预览盒、§8 集成判据余下那条「遇损坏配置启动弹且只弹一次」，以及面板与运行期改档的**真机走查**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。版本脚注 v0.52 → v0.53。

---

## v0.52（2026-10-04）设置面板实现棒第三件：**表单状态机**落成可单测件（落地裁决 7.52 的 §8 判据③，三件纯逻辑前置至此齐备）

**动机**：v0.50 与 v0.51 落了 `codespec/UI_SETTINGS.draft.md` §8 三件纯逻辑前置的第②、①件，本棒落第③件——裁决 7.52 的 S3① 拍板「本仓自持一份 `Settings` 副本 + 显式 `Store::replace()`」，其代价明写「表单状态机（副本、脏标记、逐键落盘时机）归本仓自研并有单测」；而 7.52⑤ 的框架实测给了它必须自研的理由：**框架 `Preferences::binding` 是单向投递视图，不写回存储**，故运行期「改了哪些键、要不要落盘、改完广播给谁」在公共 API 上没有可依赖的形态，属裁决 7.13② 的「可用现有公共 API 组合出来，留在本仓」。本件落完，面板本体（该稿 §7 的开工项）三件前置齐备。

- **三个名词各自的形态（本件的全部实质）**：**哪些键改了**＝`is_dirty()` 比「当前值 vs 最后一次成功落盘的那份」，**按值而非按提交次数**——用户把字号 14 改成 16 再改回 14，该键不脏，面板关闭时因此不写盘，也不会把「改了又改回来」的中间值广播给运行中的视口；**要不要落盘**＝`has_unsaved_changes()` 决定 `Store::replace()` 值不值得调，而 `note_persisted()` **只在它返回空值（成功）后**调——`config/store.h` 的既有口径是「损坏备份未成功就拒绝落盘」，那时脏标记必须留着，否则面板谎报已存；**生效档位**＝`apply_scope()` 把该稿 §2 那两个正交字段折成**两个动作**：只有「已接线 ∧ 即时」给出 `PersistAndApplyNow`（有可广播的对象），`Absent ∧ Immediate`（状态栏十个开关）与 `Wired ∧ NextSession`（`terminal.scrollback_limit` / `connection.local_shell` / `connection.startup_directory`）同归 `PersistOnly`，依据是 §4 B3-a 的原句「两条标签不冲突」。折成动作而不是四值档位是因为「接缝待开」与「延后」在**动作**上本就无差别，硬做四值就是让面板替本件决定一个不存在的行为。
- **三条不产生第二真值源的接缝**：① **本头不含 Aurora 也不含 `config` 类型**（与 v0.51 那件同一条模块环理由），键一律用**落盘点号路径**指代、值用本件自己的八档形态族 `FormValue`、行向 `ui::settings_catalog()` 现查，于是面板、本件、反向核对件三者共用同一套地址；② **校验式复用既有的两处真值源**——区间与白名单逐键取自那张表（其区间已由 v0.51 的判据逐键交回装载侧复核），色值文本走 v0.50 那件的 `ui::color_from_hex`，故 `#12ab34ff` 这类「面板接受而存不回去」的形态在表单同样判非法（A2-d 的可执行形态）；③ **「未配」是一次显式动作**（`commit_unset_color`）而不是空串——裁决 7.27③ 区分「显式 `null`＝用户点名的未配」与「畸形值＝回落并留痕」，空串属后者，而 A2-b 要的「未配与配成黑色必须可区分」在值上兑现为 `std::optional<RgbaColor>` 的空值与 `RgbaColor{}` 之别，且只有 `OptionalHexInput` 那两行可收，必填色槽收到未配回 `UnsetNotAllowed`。
- **S14 的提交时机在接口形态上兑现**（本件与判据文的主要接缝）：文本入口只有 `commit_text` 一条，且按**控件形态**把关——`accepts_text` 只放行 HEX / 自由文本 / 数值步进三形态，下拉、开关、色板整表、族名链与只读表的候选由控件自己给出、走 `commit_value`。面板若在逐字符变化处就调本入口，半截输入（`#12`）会以 `MalformedColor` 被拒且**值一字不动**，于是「失焦或 Enter 才交进来」不是约定而有可判定形态；给下拉开文本入口就是允许面板绕过表里声明的白名单（该例结构上抓得到，见变异 M7）。`FreeText` 行**原样收字符不做转义**（B2-b：断点集里引号、反斜杠、竖线都是合法内容，`connection.*` 的路径允许含空格），其 `choices` 是建议项而非白名单，本件因此不据它硬校验。
- **装载还做一次结构核对**（`FormLoadReport` 三张名单，形态照 `config::LoadReport`）：表里有而没给值、给了但形态族与该行不符、路径不在表里，各列一张单且该键在表单里没有值——面板据此**少画一个控件**而不是画一个存不回去的控件。`rows_` 的次序照反向核对表，故 `dirty_keys()` 的返回次序就是面板的排版次序（角标与「未保存」提示逐行取用不必再排序）。
- **一条判据边界如实登记**：`config::Settings` 的成员搬进搬出发生在面板界面腿（该稿 §7 的开工项），本件按点号路径取放值，因此**守不到「两个同域成员互换」**（`appearance.font_line_height` 与 `appearance.font_letter_spacing_dp` 同为实数，路径与形态族都各自成立）。不伪造判据：那条事实的证人是 §8 集成那条「改 palette 一格 → 预览盒与主视口的同一格色逐位变化」，边界同时写进该稿 §8 与 `utest_settings_form` 的说明头。
- **框架缺口 0**（按裁决 7.13① 逐条读 Aurora 当日活动分支公共头复验）：本件要的三份材料（取值域、白名单、色值式子）在本仓已齐（v0.50 / v0.51），框架侧「无运行期写回 binding」是既成形态而非缺口——单向投递视图本就是它的模型，写回由本仓显式调 `Store::replace()`（S3① 已拍板），故本棒**不出任务书**。
- **验收**：`tests/unit/utest_settings_form.cpp` **十三例**（整表装载无结构漂移、结构漂移三张名单点名且被点名的键无值、数值区间逐键（两端收、越界拒且值不动）、白名单逐名收＋越界名拒、HEX 文本与装载侧同式且八位 alpha 必拒＋空串属畸形值不属未配、`FreeText` 原样收字符且建议项不作白名单、数字文本区分「不成个数字」与「整数档收到小数点」、色板逐格提交且整表必须 16 格、族名链保序且空表是值而非未配、只读行三种入口全拒、脏标记按值不按点击次数、`dirty_keys()` 次序照表、只有「已接线 ∧ 即时」广播）。**十二条变异注入各有证人**：删必填色槽的未配判定（`hex_input_...` 红）／闭区间改开区间（`numeric_ranges_...` 红，并连带脏标记那一例——它把值改回装载基准即区间下界）／删白名单判定（`choice_whitelists_...`）／删只读判定（`read_only_rows_...`）／删色板格数判定（`color_table_...`）／`note_persisted()` 置空（`dirty_marks_...`）／放行下拉的文本提交（`free_text_...`）／数字文本不判尾串（`numeric_text_...`，`12abc` 与 `5.0` 都会被当成合法整数）／装载不再核形态族（`structure_drift_...`）／生效档位不折叠成两动作（`only_wired_immediate_rows_broadcast`）／脏键次序不照表（`dirty_keys_follow_the_catalog_layout_order`）／把空链当成未配（`family_chain_...`）。每次重链前删 `build/**/*.ilk`（`msvc` 预设带 `/INCREMENTAL`，陈旧 ilk 会让变异假绿）。非 e2e 通道 **35 项全绿**（在册 34 项 + 本套件；`utest_tab_strip_layout` 与标签条集成例属并行在途的标签条棒、本棒不为它背书）。**本棒未复跑吞吐门禁**：本件不进任何绘制路径，`borealis_core` 里它是只在面板打开时被读写一次的表单。
- **文档回写**：`codespec/UI_SETTINGS.draft.md` §8 的第③件标注已落并写清六点处置（含 S14 的接口兑现与那条单向边界）；`codespec/ARCHITECTURE.md` §9.2 的纯逻辑件清单补 `settings_form`、§11.1 在该表之后新增一段说明本件的三个名词与两处真值源复用；`AGENTS.md` §2 的 `include/borealis/ui/` 与 `src/ui/` 两处清单及测试行入册、§6 新增本棒现状条目；`codespec/PLAN.md` §3 的 `SPEC.FEAT.PREF.02` 行改为「三件纯逻辑前置已全部落地，余下是面板本体与 S4 的两条运行期入口」。未新增裁决——本件交付的形态（副本、显式写回、按值判脏、两动作折叠、未配作显式动作）全部落在 7.52 的 S3① / S14 / A2-b / B3-a 与 7.27③ 已拍板口径内。
- **仍待人工 / 未落**：S4 的两条运行期更新入口（外观包 + `InteractionOptions`，须一并触发整格度量重取、`layout_opts_` 重建、闪烁 `TimerHandle` 取消重注册与既有去抖下发）、面板本体 `src/ui/settings_panel.{h,cpp}`（第三个触达 `au::Painter` / `au::Widget` 的 TU，含 `Settings ⇄ FormValue` 的搬运——那条单向边界由此闭合）、`config/store.h` 的 `TODO(SPEC.FEAT.PREF.07)` 与 `src/main.cpp` 的三个会话侧注入接缝、§8 集成五条（走 `HeadlessSurface`），以及面板的**真机走查**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。版本脚注 v0.51 → v0.52。

---

## v0.51（2026-10-04）设置面板实现棒第二件：schema ↔ 控件的**反向核对件**落成可执行表（落地裁决 7.52 的 §8 判据①）

**动机**：v0.50 落了 `codespec/UI_SETTINGS.draft.md` §8 三件纯逻辑前置的第②件（色值文本形态），本棒落第①件——那张 §2 反向核对表。它当时**只是一张文档表**，而文档表的分叉只有靠人读才能发现：日后新增一个配置键、或面板漏画一个控件、或面板把区间抄宽了一格，都不会有任何东西转红。裁决 7.26① 那句「面板落地须以 schema 反向核对键名，不得另起一套」要的是一条**结构上不可分叉**的判据，故本棒把它落成 `ui::settings_catalog()`（`include/borealis/ui/settings_catalog.h` + `src/ui/settings_catalog.cpp`，58 行 × 五列）并配 `tests/unit/utest_settings_catalog.cpp`，让「面板该画什么」由代码与 schema 互证。

- **基准取真正落盘的 JSON，而不是 `Settings` 的字段名**（判据① 的双向兑现）：证人用 `Store::replace(全量非默认 Settings)` 写出一份文件，剥掉 `schema_version` 与框架 `__aurora_preference_meta__` 后把剩下的**叶子键集**与表里的键集**双向**比对——落盘有而表里没有＝面板漏画控件，表里有而落盘没有＝面板画了一个存不回去的控件（其失败消息把两侧的键集全列出来，缺哪一格一眼可见）。这条基准比「反射 `Settings` 字段」强在一处：可缺省槽写 `null`、空数组也要落盘、`shortcuts.overrides` 是数组形态而非映射（裁决 7.27①②）这类「内存里有而落盘里没有」的形态差异不会让判据假绿。表里新增一键，探针**自动**覆盖它，无需另动用例。
- **取值域逐键交回装载侧复核**：表声明的每个数值区间各写「取下界 / 取上界 / 越界一格」三份批量探针文件（一次覆盖全表），断「域内值不出现在 `LoadReport::rejected_keys`、域外值必须出现」；每个 `Choice` 行的白名单按档位下标逐名写一遍，另加一份全 `__not_a_declared_value__` 的探针。于是「面板抄了一份比装载侧更宽或更窄的区间」结构上抓得到，而区间照抄 `src/config/store.cpp` 的 `scope.real(...)` / `scope.integer(...)` 那两个数，面板因此不产生第二个真值源（7.46② 的分工在界面上的兑现）。色值行另有一条独立判据：7 字符 `#RRGGBB` 不被拒、8 字符（多一个 alpha 位）**全被拒**，即 A2-d「所有色值输入框都没有 alpha 位」的可执行形态；`terminal.encoding` 那六个名字则按 **建议项**登记（`FreeText` 行写越界名仍不被拒），故面板不得据表硬校验。
- **两条档位做成正交字段**（一处建模抉择，依据是判据文 §4 B3-a 的原句「两条标签不冲突：前者说没有消费方，后者说有消费方时的档位」）：文档里那一列四值档位表达不出 `terminal.encoding` 的「延后 + 下次会话」，故拆成 `ConsumerStatus{Wired, SeamPending, Absent}` × `EffectLevel{Immediate, NextSession}`，并由用例钉死两处**物理边界**的点名集合——`SeamPending` 恰等于 §7 的三条会话侧注入（`cursor_shape` / `cursor_blinking` / `ambiguous_width`），`Wired ∧ NextSession` 恰等于 §0 边界② 的三条（`scrollback_limit` / `local_shell` / `startup_directory`），多一格少一格都转红。
- **三条刻意不做的形态**：① **不含 Aurora 也不含 `config` 类型**（`config/settings.h` 已 include `ui/palette.h` 等件，反向 include 即模块环，`right_click.h` / `tab_strip.h` 同因），于是每行只用**落盘点号路径**指代键——判据文 §2 表按成员名速记的 `palette.cursor_color` / `selection_color` 在表里写作 `appearance.palette.cursor` / `.selection`（该差异已就地补进 §2 的表头注）；② **不复制候选清单**（主题名归 `config/themes.h`、字体族名归装配阶段那一份框架目录，S16），表里若各列一遍就是换预置时必漂的那一分叉；③ **不做「撤销项」那一列**——§3 那四处「图上画了、schema 没有键」的控件本就不在叶子键集内，双向比对天然排除它们，再列一份撤销清单就是第二个需要人维护的名单。**一条判据边界如实登记**（已写进该稿 §8）：取值域那几列是**单向**可证的，表比装载侧宽会转红（写了就被拒、留痕），表比装载侧窄**抓不到**（只是面板少画一个合法选项，装载侧不报错，而装载侧的白名单是 `store.cpp` 的文件内私有表、公共面无「列出某键全部合法名」的入口）——少画选项由人评审时看到并补表，本件不伪造一个抓不到的判据。
- **验收**：`tests/unit/utest_settings_catalog.cpp` **八例**（双向键集、键名唯一且每行可按路径寻址且页与路径首段一致、数值区间两端与越界、白名单逐名与越界名、色值 alpha 位、建议项不被装载侧校验、五列字段互洽（域 ↔ 控件形态的全表 switch、`SwatchGrid` / `FamilyList` / `ReadOnlyTable` 各恰一行、两个可缺省色槽必须是 `OptionalHexInput`、非数值行不得夹带区间、非 Choice/FreeText 行不得夹带白名单）、两列档位的两个点名集合与 Absent 计数）。**七条变异注入各有证人**：删一行（双向例红且失败消息点名少的那格）／加一个 schema 外的键（双向例 58↔59 + Absent 计数 28↔29 两证）／把 `font_size_pt` 上界放宽一格（数值例红）／往 `bell` 白名单塞一个装载侧不认的名（下拉例红并点名该键）／把一条 `SeamPending` 改成 `Wired`（档位例两处点名集合同红）／去掉某个可缺省色槽的 `optional` 标记（字段互洽例红）／把一个色值行改判成自由文本（色值例 + 建议项例同红）。每次重链前删 `build/**/*.ilk`（`msvc` 预设带 `/INCREMENTAL`，陈旧 ilk 会让变异假绿）。非 e2e 通道 **34 项全绿**（在册 33 项 + 本套件；`utest_tab_strip_layout` 属并行在途的标签条棒、本棒不为它背书）。**本棒未复跑吞吐门禁**：该表不进任何绘制路径，`borealis_core` 里是一张只在装配与面板期读一次的静态表。
- **文档回写**：`codespec/UI_SETTINGS.draft.md` 的 §8 判据① 标注已落并写清上述三点处置与那条单向边界，§2 表头补「成员名速记 vs 落盘点号路径」一句；`codespec/ARCHITECTURE.md` §9.2 的纯逻辑件清单补 `settings_catalog`（含「不含 `config` 类型」这条环上边界），§11.1 新增一段说明该表的判据形态与正交两列的根据；`AGENTS.md` §2 的 `include/borealis/ui/` 与 `src/ui/` 两处清单及测试行入册，§6 新增本棒现状条目；`codespec/PLAN.md` §3 的 `SPEC.FEAT.PREF.02` 行改为「三件前置已落两件」。未新增裁决——本棒交付的是 7.52 §8 已拍板的形态，正交两列与「撤销项不做成行」两处建模选择在 `SPECIFICATIONS.md` 附录无冲突条款，其根据（B3-a 原句）已就地引用。
- **仍待人工 / 未落**：该稿 §8 第③件（表单状态机：副本、脏标记、逐键落盘时机、S14 的提交时机）、S4 的两条运行期更新入口、面板本体、`config/store.h` 的 `TODO(SPEC.FEAT.PREF.07)`、`src/main.cpp` 的三个会话侧注入接缝，以及面板的**真机走查**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。版本脚注 v0.50 → v0.51。

---

## v0.50（2026-10-04）设置面板实现棒第一件：色值文本形态提为**单一判定点**（落地裁决 7.52 的 S14）

**动机**：v0.49 的判据文把面板本体拆成三件纯逻辑前置（该稿 §8），其中第②件「HEX ↔ `ui::RgbaColor` 的解析与格式化」有一条硬约束——S14 明写「**校验式必须与装载侧同一条**」。而那条式子当时是 `src/config/store.cpp` 的三份**文件内私有实现**（格式化 / 取一个十六进制位 / 解析），面板若要收它只能复制或另写一份，两处各自演化就是配置里的第二真值源。本棒先落这一件，是为了让后续的面板本体开工时**没有第二个真值源可建**。

- **迁出形态与不变量（7.52 S14 的兑现）**：`ui::color_to_hex` / `ui::color_from_hex`（`include/borealis/ui/color_text.h` + `src/ui/color_text.cpp`，公共头不含 Aurora 类型，AGENTS.md §4.4 第 20 条同口径）；`src/config/store.cpp` 删三份私有实现改消费该件，**落盘形态逐字节不变**——大写十六进制、恰 7 字符、`#` 前缀必带、大小写不敏感读回。**alpha 不参与这条形态**是刻意的：终端色带不做透明混合（`ui::contrast_ratio` 同口径），故解析产物恒 `alpha == 255`、格式化也不输出 alpha 位；将来要让透明度可配，先改的是 `PaletteSpec` 的落盘形态而不是本件的位数。
- **验收**：`tests/unit/utest_color_text.cpp` **九例**（大写与 7 字符形态、通道次序 r/g/b 各取唯一档、alpha 不参与格式化、大小写不敏感、半字节边界 `0x00 / 0x0F / 0xF0 / 0xFF` 的往返等值、缺前缀 / 位数不合 / 前后空白 / 非十六进制位 / `#RRGGBBAA` / `rgb()` 式 / 空串七类拒绝）。**四条变异注入各有证人**：接受 8 位（唯一证人 `rejects_eight_digit_form`）、不要求 `#` 前缀（`rejects_missing_hash`）、格式化通道次序对调（本件三例 + `utest_config` 的往返与损坏备份两例同红，**后者是装载侧确实消费本件的证人**）、格式化输出小写（本件三例红而 `utest_config` 全绿——往返对大小写不敏感，故大写档只能由本套件守）。非 e2e 通道 **33 项全绿**（在册 32 项 + 本套件；`utest_tab_strip_layout` 属并行在途的标签条棒）。每次重链前删 `build/**/*.ilk`（`msvc` 预设带 `/INCREMENTAL`，陈旧 ilk 会让变异假绿）。
- **文档回写**：`codespec/UI_SETTINGS.draft.md` §1 的「色值输入形态」行与 §5 S14 改为指向该件（原判据「与装载侧同一条」由结构保证而非约定），§8 的第②件标注已落；`codespec/ARCHITECTURE.md` §9.2 的纯逻辑件清单补 `color_text`，§11.1 那条色值落盘形态边界补「唯一判定点已落」；`AGENTS.md` §2 的 `include/borealis/ui/` 与 `src/ui/` 两处清单及测试行入册，§6 新增本棒现状条目。未新增裁决——本棒不产生与既有口径冲突的决定，交付的是 7.52 S14 已拍板的形态。
- **仍待人工 / 未落**：该稿 §8 另两件前置（schema ↔ 控件反向核对件、表单状态机）、S4 的两条运行期更新入口、面板本体、`config/store.h` 的 `TODO(SPEC.FEAT.PREF.07)`、`src/main.cpp` 的三个会话侧注入接缝，以及面板的**真机走查**（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条不宣称面板「可用」。版本脚注 v0.49 → v0.50。

---

## v0.49（2026-10-04）设置面板的判据文前置落地（裁决 7.52，S1~S16 自拍 + 框架真缺口 0，并就地更正一条在册的 `MenuItem` 实测结论）

**动机**：`SPEC.FEAT.PREF.02` 是全部延后 UI 入口的唯一落点（字体族 / 行高 / 字距 / 回退链 / 标签名优先级 / 右键三态 / 三项复制变换 / 断点集 / 闪烁周期，以及 `TODO(SPEC.FEAT.PREF.07)` 的降级对话框），而屏 3 的视觉稿自 2026-10-01 入库起**只有图没有文**。人已拍板四条并定下开工前置「先补判据文并自拍，再动代码」，本棒据此出 `codespec/UI_SETTINGS.draft.md`（§0 性质与两条物理边界 / §1 全局参数 / §2 反向核对表 / §3 与 SVG 的九处出入 / §4 逐页判据 / §5 S1~S16 / §6 框架实测 / §7 两处 TODO / §8 验收 / §9 出图），结论入册为裁决 **7.52**；本棒**零代码改动**。

- **控件清单的来源（7.52①）**：不是从 SVG 上数出来的，而是从 `include/borealis/config/settings.h` **反向核对**出来的（裁决 7.26① 明令面板不得另起一套键名）——四域逐键给出「页 / 控件 / 取值域 / 消费方现状 / 生效档位」五列，取值域**照抄装载侧 `src/config/store.cpp` 实际把守的区间**（行高 `[1.0,3.0]`、字距 `[0.0,8.0]`、内边距 `[0.0,64.0]`、闪烁周期 `[50,5000]`、对比度 `[1.0,21.0]`、scrollback `[0,100000]` 恰等于 `grid::kMaxScrollbackLimit`、波特率 `[1,4000000]`），故面板不产生第二个真值源。「有键无消费方」与「无键」两档延后分别处置（S15），凭据按 `SPEC.FEAT.CONN.09` 不入表、面板因此没有任何密码输入框。
- **两条物理边界（7.52②，「修改即时生效」这条判据在本棒的实际含义）**：① **本仓当下没有任何给面板用的运行期更新接缝**——`ui::TerminalView` 的七个外观与交互入参全挂构造函数，既有四条接缝（`set_overlay_host` / `set_presentation` / `set_grid_size_sink` / `set_key_pre_filter`）没有一条能改颜色或字号，故给视口开两条运行期入口（一条收「外观包」、一条收 `InteractionOptions`）是**本棒的开工项而非既有能力**（S4）；② `scrollback_limit` / `connection.local_shell` / `connection.startup_directory` 与 §7 的三条会话侧缺省是**建会话那一刻取用**，运行期改它们不重放既有会话，判据因此写「下次会话生效」而不谎报即时（S11，人已拍板②）。
- **与 SVG 的九处出入（7.52③，以本稿为准）**：主题卡第 7、8 张名字与 `config/themes.h` 的八套不符（以代码为准）；第五类「高级（韧性/日志）」、「跟随系统浅色」（框架 `MediaQuery` 无亮度字段，即**没有数据源**）、「多行粘贴警告」（`SPEC.FEAT.INTERACT.03` 写成固有行为，块间隔按裁决 7.33④ 是库内常量且明写「暂不开配置键」）、「OSC 52 读授权三态」（属 `SPEC.FEAT.CONN.12`，schema 无键）四处撤销；语言下拉转灰置延后；回退链从静态串改可重排列表（S8）；快捷键页首版只读（S9）；右侧预览盒从手绘静态图改**真实绘制路径**（N5）。**一处「文有图无」**是状态栏 10 个开关（裁决 7.25⑧ 明写入 `borealis::config` 且入口随设置面板，SVG 未画），定在外观页末段并登记为配图落后项。
- **S1~S16 自拍（7.52④，全取建议项，沿用裁决 7.38① / 7.47 的先例）**：形态是**同窗口在既有场景根 `au::OverlayHost` 上加一层全屏浮层**（S1，代价是面板遮住主终端、即时生效的可见兑现由预览盒承担）；关闭通道打开时向 `ShortcutRegistry` 注册一条 `Global` 的 `Escape`、关闭时 `remove`（S2，框架 `CommandPalette` 自己就这么做）；状态与写回是本仓自持 `Settings` 副本 + 显式 `Store::replace()`（S3，框架 `binding` 是单向投递视图不写回存储）；chrome 由本仓给场景根装一份 `ThemeScope`（S5，其 `Theme` 只有五个令牌且**故意不随终端主题联动**，N6）；切主题整份 palette 取新值 + 每槽「恢复主题默认」（S6，落盘是最终值故无逐槽账本可查）；预览用独立 `Session` + 夹具文本（S7）；降级对话框按 `LoadOutcome` 在启动路径弹一次、文案走本仓词条而 `LoadReport::message` 只进日志（S12 / S13，`message` 是 ASCII 英文诊断，属 AGENTS.md §4.3 第 14 条）；HEX 输入用 `TextInput` + `FormField` 校验且**校验式必须与装载侧同一条**（S14，`store.cpp` 只收 7 字符 `#RRGGBB` 且写回不输出 alpha，面板收 8 位就是另立一套落盘形态，据此新增判据 A2-d「所有色值输入框都没有 alpha 位」）；字体族目录与主窗口共用装配阶段那一份（S16，裁决 7.46③）。
- **框架真缺口 0（7.52⑤）**：按裁决 7.13① 逐条读 Aurora 当日活动分支的公共头复验，八条形态限制**都不落在渲染链路与事件链路的类别里**——「运行期换主窗口根」**有**公共入口（`Application::window()` 回主窗口、`present_root(Node&)` 是公开成员），限制在生命周期成本（只在根变化时重挂、焦点不自动回来）而非可达性；带 Binding 构造重载的控件恰为四个且写回不成立；无取色器；`SpinBox` 不能键入数字；`Dropdown` 无运行期 `set_options`；`MediaQuery` 无亮度；`ShortcutRegistry` 的 `bindings()` 副本不含 ID 故「暂停全部绑定」要由 `clear()` + `CommandRegistry::bind_shortcuts()` 组合；`Dialog` **没有** `with_scrollable_body()`，须由本仓把列表包进 `au::Scroll` 再作内容节点交入。八条皆可由公共 API 组合，属裁决 7.13② 的「留在本仓」。
- **一条在册实测结论的就地更正（7.52⑥，本条的主要交付）**：2026-10-02 在册的「框架 `Modifier::context_menu` 与 `MenuItem` 只有状态模型、**没有渲染与点击派发**」前半不成立——`MenuItem` **有**渲染与点击派发，载体是 `widget/menu_bar.h` 的下拉（`on_paint` 逐项画分隔线 / 标签 / 勾选 / 置灰色，Press 分支按等分行高折序号并调 `item.on_click()`，`wants_click()` 恒 `true`）。须更正的是两处限定：派发只在 `MenuBar` 自己的 `dropdown_bounds()` 矩形内（框架没有「任意位置的右键菜单」这件控件），且那条下拉色值**硬编码浅色**（白底 `255,255,255,255` / 正文 `30,30,30` / disabled `170,170,170`）**不随 `Theme` / `ThemeScope`**，故 7.41③ 的置灰档在深色界面上本就不可用。`Modifier::context_menu` / `ContextMenuNode` 那一腿确实只有模型；`OverlayHost::handle_outside_click()` 在 Aurora 全仓仍无生产调用点（复核**不变**）。据此本仓右键菜单取 `au::Popup` + 一列 `au::Button` 的形态**不变**，理由改为「框架渲染但不随主题、且没有任意位置的弹出件」。
- **验收**：本棒无代码改动故无变异自证，构建面与测试面维持现状（非 e2e 通道 **32 项全绿**，其中 `utest_tab_strip_layout` 属并行在途的标签条棒、本棒未触碰也不为它背书）。判据文的验收面已在该稿 §8 定死待实现棒落地：三件纯逻辑前置各补 `utest_*`（schema ↔ 控件的反向核对件、HEX 文本 ↔ `ui::RgbaColor` 的解析与格式化、表单状态机含 S14 的提交时机），集成面走 `HeadlessSurface` 五条（改 palette 一格后预览盒与主视口同格色逐位变化、改字号后两视图行列数各按自身矩形重算且不出半格、改 `right_click` 后下一次右键的意图与菜单项随新值走、切主题后整份 palette 按 S6 重置且「未配」槽回落新主题、遇损坏配置启动弹且只弹一次且备份路径进文案且不静默清空）。
- **仍待人工 / 未落**：面板本体与 S4 的两条运行期入口、`config/store.h` 的 `TODO(SPEC.FEAT.PREF.07)` 与 `src/main.cpp` 的三个会话侧构造期注入接缝（`cursor_shape` / `cursor_blinking` / `ambiguous_width`）、面板的**真机走查**（取色、拖拽重排链、逐槽恢复默认三处手感）——会话锁屏下 `SendInput` 静默失效（裁决 7.31①），本条不宣称「可用」。
- 文档回写：新增 `codespec/UI_SETTINGS.draft.md`（判据文）；`SPECIFICATIONS.md` §7 增裁决 7.52（六条 + 代价 + 验收）、§7 追加范围改为「7.49–7.52 于 2026-10-04」、7.41③ 与附录 A.1 的「菜单/快捷键」行按 7.52⑥ 就地更正（原文保留、加限定）；`ARCHITECTURE.md` §9.2 那条「框架只建模不代劳」改写为「不代劳**任意位置的**弹出派发」并补两处限定，§11.1 新增两条本棒实测边界（「跟随 `ThemeScope`」在本仓目前是空话——从未给场景根装过，chrome token 表须由本仓装配阶段注入；色值不经配置往返故面板不得有 alpha 位）；`AGENTS.md` §6 的右键菜单那句同口径更正并新增本棒现状条目；`PLAN.md` §3 的 `SPEC.FEAT.PREF.02` 行补「判据文前置已落、面板本体未落」、§8 的「网格行数变化」行补去抖腿落地现状与「验证现状」读数（2026-10-04 复核、非 e2e 32 项）。版本脚注 v0.48 → v0.49。

---

## v0.48（2026-10-04）G25 回货接货复验并**修正一句在册的迁移前置**（裁决 7.51，A.2 开放缺口清零）

**动机**：裁决 7.47② 为定「本棒键位走哪条派发通道」而读源实测出的 **G25**（`KeyCombo::matches` 的修饰位整字节相等对锁定态位不可用，NumLock 开着时应用内快捷键整层不匹配）于 2026-10-04 回货——Aurora 当日活动分支 `dev-1.0.0-alpha.9.uat.2` 的 `cc2b65e5`（匹配屏蔽锁定态位）/ `72699290`（该位随消息流推进且不被幻影防护抹掉）/ `f6a4ca7b`（该仓文档回写）三条。本棒接货复验，结论入册为裁决 **7.51**（三条 + 代价 + 验收），并**修正一句在册结论**；本棒**零代码改动**（消费侧零改动本身就是验收）。

- **匹配腿的形态（7.51①）**：`event/event.h` 新增 `AURORA_MODIFIER_PRESSABLE_MASK`（Shift | Control | Alt | Meta）与 `AURORA_MODIFIER_LOCK_MASK`（当前只有 `NumLock`）两条 namespace 级 `inline constexpr std::uint8_t`，并自陈二者须是 `ModifierKey` 全部已定义位的**无余划分**（新增锁定类位须并入锁定位掩码，否则落进两掩码之外的缝隙继续污染按位比较），该恒等式由该仓 `utest_shortcuts` 的位集用例钉住；`KeyCombo::matches` 改为两侧各取「可按住位」子集后再比。四条该仓刻意保留的口径本仓无异议：仍按**逐位相等**而非「包含」判定、注册侧带锁定态位的组合依然**静默表达不出来**（无 assert / 日志 / `Result`）、不给 `KeyCombo` 加 per-combo 的 NumLock 字段、不新增匹配档位。
- **Win32 陈旧值腿与本仓的零改动（7.51②）**：`detail::ModifierKeyTracker` 拆成按住态与锁定态两份账（`lock_bit_for` 新增、`bit_for` **刻意**不含 `VK_NUMLOCK`、`apply` 三段分派且**只在 `down == true` 翻转**、`seed()` 按掩码拆分混装读数、`clear()` **只清按住态**、`get()` 回并集），故 `KeyEvent::modifiers` 的对外形状逐位不变；本仓全部修饰位读点都是**单 bit 测试**（折 `term::KeyPress` 的五位、`Ctrl` 缩放、`Alt` 块选），接线面恰为 **0 行**。实际受益是 `SPEC.FEAT.INTERACT.01` 的 keypad 两档分流（裁决 7.36②）不再读激活时刻的陈旧值——旧缺陷的症状是「激活时 NumLock 关、期间打开」后 `KP_7` **既**发 `CSI H`（Home）**又**随 `WM_CHAR '7'` 上屏，屏幕读作 `7ABC` 而正确形态 `AB7C`。
- **一句修正（7.51③，本条的主要交付）**：原句「**G25 回货后键位可原样迁回 `ShortcutRegistry` 而不动任何判据**」散见九处（`SPECIFICATIONS.md` 的 7.47② 代价段 / A.2 的 G25 行 / A.3 尾段三处、`PLAN.md` §6 的 G25 行、`ARCHITECTURE.md` §3.5、`UI_WORKSPACE_INTERACT.draft.md` 差距 6、`workspace_keys.h` 文件头、`AGENTS.md` 的 §2 目录表格行与 §6 缺口账目条目；另 A.1 那条与该稿 D2 行虽不含此句亦同因本条改写），它把 (b) 当成了唯一前置，实测**不成立**——**(a) 腿照旧拦**（`set_key_pre_handler` 仍是「命中即消费、不再向焦点控件派发」，`ShortcutScope::Focus` 的判据仍是「本宿主有任一焦点控件」这个 **bool**，故就地重命名的 `TextInput` 持焦期间 `Alt+→` 与 `Ctrl+Shift+0` 依旧被抢走，与 7.47⑦ / 7.41 不容）；**(c) 腿也仍真**（赋值即替换），只是它拦的是「应用侧自装窗口 pre-handler」那条备选方案。据此 `SPEC.FEAT.WS.01` / `02` 的键位**照旧不经框架快捷键层**，仍走焦点 pane 的 `KeyPreFilter`；新口径登记为「本件只比四位与回货后框架 `matches` **逐位一致**，故未来迁移只是通道改道，`utest_workspace_keys` 那条 NumLock 判据届时由唯一证人降为冗余守卫（保留不删）」。
- **验收**：构建 87/87 目标全成，非 e2e 通道 **32 项全绿**（在册 31 项 + `utest_tab_strip_layout`，后者属并行在途的标签条棒、本棒未触碰也不为它背书）；e2e 三条 2 绿，`etest_osc_clipboard` 以 `OpenClipboard, GetLastError=5` 失败并**以独立的 PowerShell `Set-Clipboard` 探针复现同一失败**，判为环境（会话锁屏，裁决 7.31①）而非回归——本次 Aurora diff 只及 `event/event.h`、`app/shortcuts.h`、`window/detail/win32_modifiers.h`、一个 demo 与该仓自有测试及其文档回写，不含任何剪贴板路径文件。本棒无代码改动故**无变异自证**；一句结论的修正以九处文档就地更正为准（原文保留、不覆写）。
- **仍待人工 / 未落**：7.51② 的**小键盘真机腿**（解锁后目视，判据与两种屏幕读数已在册；本轮 `activate_window` 报「未能把目标窗置前」，前台窗类名 `Windows.UI.Core.CoreWindow`）；`SPEC.FEAT.PREF.04` 的键位可重绑开工前须先判 (a) 腿是否已由框架侧解决。附录 A.2 至此**开放缺口清零**，下一份可派发任务书不再来自框架缺口账目，而随 `SPEC.FEAT.WS.01` 的标签条界面腿与 `SPEC.FEAT.PREF.02` 的设置面板。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.51（三条 + 代价 + 验收）、§7 追加范围改为「7.49–7.51 于 2026-10-04」、`SPEC.FEAT.WS.02` 实现约束段的 G25 括注改写、附录 A.1 的「快捷键的派发位置与匹配口径」行按回货改写（两条仍成立的理由与一条已闭合的判定分写）、A.2 的 G25 行改标**已闭合**（原三条事实与原处置段保留、那句「回货后可原样迁回」就地作废并注明）、A.3 尾段补回货记录；`ARCHITECTURE.md` §3.5 的三条理由改写成「只剩两条成立」；`PLAN.md` §6 的 G25 出账（划删线 + 回货形态 + 迁移前置修正）；`UI_WORKSPACE_INTERACT.draft.md` 的 D2 与差距 6 各补一句复核结论；`AGENTS.md` 的 §6「框架侧开放缺口」账目改为清零并新增本棒现状条目、§2 目录表格行里 `workspace_keys.h` 那句迁移前置同口径就地更正；`include/borealis/ui/workspace_keys.h` 文件头同口径更正。版本脚注 v0.47 → v0.48。

---

## v0.47（2026-10-04）G23 / G24 回货接线：按族缺字回退链可配、固定格推进档位进绘制侧（裁决 7.50）

**动机**：裁决 7.46 收尾时把字体棒剩下的两条框架缺口 **G23**（按族回退链无入口）与 **G24**（批量文本入口无「按网格强制单格推进」的档位）按 7.13① 派发 Aurora 侧，本仓当时**不等不绕**——回退链以「框架全局回退已足以让汉字显形」的形态交付、且**刻意不登记消费不到的配置键**，列位对齐由应用侧的 **run 断跑** 承担。两条已于 2026-10-04 回货（Aurora 当日活动分支 `dev-1.0.0-alpha.9.uat.2` 的 `6cfbe88d`，同批 `0b9781de` / `3d453562` / `d0cb1e47` 是其后三条 CI 修腿），本棒把它们接进本仓，落地口径入册为裁决 **7.50**（七条 + 代价 + 验收）。

- **回退链的登记与承载（`feat(config)` + `feat(ui)`，7.50①②③）**：新键 `appearance.font_fallback_chain`（有序字符串数组，缺省空表＝不注入按族链、只走框架的全局默认链）落 `appearance` 域——回货后「消费不到的键」这一前置条件消失，故登记，而其 **UI 入口**仍随 `SPEC.FEAT.PREF.02`。链只接绘制侧那条入口（`TextLayoutOpts::font_fallback_chain`，承载形态是「定长数组 + 长度」以保持字面类型），在 `TerminalView` 构造时经 `with_fallback_chain(span)` 装一次进成员 `layout_opts_`；不选 `aurora::Font` 是因为本仓的 `Font` 随字号与缩放反复重构造，而链是「一次配置、每帧共用」。超上限交框架入口截断（保留前 N 项、顺序语义不变），本仓只在配置项数越界时 `AURORA_LOG_WARN` 留痕而不自行夹取。**空数组也写出该键**（省掉整键会在装载时被父作用域拍平，「清空这条链」的意图就丢了），读侧留痕粒度到**逐元素**（`appearance.font_fallback_chain[1]`）——单项畸形只丢该项，整键回落会把一个错字放大成整条链消失。
- **固定格推进档位（`feat(ui)`，7.50④）**：`cell_metrics()` 在整格度量刷新处把 `fixed_cell_advance_px` 取为**未含字距**的原始 `monospace_cell(...).cell_width_px`（物理 px 且已含 scale）；框架把 `letter_spacing` / `word_spacing` 叠加在该档位之上，取回填后的 `typed_.cells.width_px` 会把字距算两遍。**双宽断跑随之改口径**：固定档让每个字形只推进一格，双宽字形因此**必须**独自成跑，`ui::layout_row` 的断跑从「消除累积漂移的唯一手段」降为「与档位互补的形态」（`cell_layout.cpp` 的成因注释已按此改写）；G24 的两处局限里「双宽字形自身推进不等于两格」由档位消除，「含 CJK 的行的 run 数量随密度上升」仍在。
- **验收（`test(ui)`）**：`utest_config` **12 例**（+1，全量往返等值入 `next`、缺省空表、逐元素丢弃与整键回落两种形态）；`itest_render_viewport` **27 例**（+2）——`configured_fallback_chain_survives_the_metric_refresh` 断「缺省空链 / 两个互异族名顺序保持 / 缩放重取度量后链仍在原地」，`fixed_cell_advance_is_the_raw_grid_width_and_follows_the_zoom` 断档位取原始格宽且「档位 + 量化字距 == 几何格步长 × scale」，`fallback_face_glyphs_do_not_shift_the_following_columns` 是无头通道里唯一能同时命中 G23（回退面落笔）与 G24（推进量）两条的回退面证人。非 e2e 通道 **31 项全绿**。六条变异各有证人：**删整条档位赋值**（像素证人 + 接线件两例红）、**改取含字距的回填格宽**（接线件 15 对 11，既有的字距像素例同红）、**只在首次赋值**（接线件缩放那一腿 11 对 13 红）、**量级减半**（接线件三处断言 + 字距与双宽两例像素红）、**构造处不装链**（只 `configured_fallback_chain_survives_the_metric_refresh` 的缩放前后两处断言红，同套件其余 26 例全绿——链内容不体现在 RGBA 上，接线件是这条腿唯一的证人）、**读侧整键回落而非逐项丢弃**（只 `chain_entries_drop_individually_not_wholesale` 的三处断言红，同套件其余 11 例全绿）。**一条执行侧的坑如实登记**：`msvc` 预设（Debug）的连接选项含 `/INCREMENTAL`，首轮变异注入后 ninja 正常重编 `.obj` 并重打 `borealis_core.lib`、而 exe 仍链进陈旧代码，**变异会假绿**；本轮起每次重链前删 `.ilk`，故上述每条红/绿都是删ilk 后实测的读数。
- **两条不可判按边界登记而非当作已通过（7.50⑤⑥）**：本机无头 `scale` 恒 1.0，dp / px 混用这类**量纲**误差在本通道结构上抓不到，只能靠「与框架同算式现算值逐位相等」锁数值；缺字是否**显形**判据在框架 shaping 侧，本仓只断列位（真机目视随裁决 7.31① 的走查解锁）。像素证人因此**只断第 2、3 列**：第 1 列在档位开合两侧都非零（关 116 / 开 46），因为回退面的 ① 字形比一格宽、墨迹越出自己那格压进邻格窗口，那是字形宽度而非推进误差（同 7.40⑨ 的边界）。
- **吞吐复验（`msvc-bench` 优化档，7.50⑦）**：七档全 PASS，网格仍 28×86、构建档仍 optimized；三次独立进程取中位为 `scroll_frame_ms_mean` 1.857（参考 1.799）、`cat_frame_ms_mean` 1.929（参考 1.841）、`cat_fps` 518.286（参考 543.117）、`cat_mb_per_s` 29.793（参考 30.565），偏离全落在 10% 相对窗内即按抖动处理，**不改基线也不放松门禁**。
- **仍待人工 / 未落**：回退链与两个排版量的 **UI 入口**随 `SPEC.FEAT.PREF.02`；缺字显形与 CJK 上屏观感属真机走查（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），本条不宣称「可用」。**G25 同日复核仍开放**（`KeyCombo::matches` 仍为修饰位整字节相等，Win32 的 `detail::ModifierKeyTracker::bit_for` 仍无 `VK_NUMLOCK` 分支、该位只随窗口激活播种），故 `SPEC.FEAT.WS.01` / `02` 的键位照旧不经框架 `ShortcutRegistry`；下一份可派发任务书即它。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.50（七条 + 代价 + 验收）、`SPEC.FEAT.RENDER.02` 的「落地现状」段改写（回退链子项转「已交付」、网格对齐改两条互补腿）、附录 A.1 的「排版选项的覆盖面」与「字体注册/回退」两行按回货改写、A.2 的 G23 / G24 两行改标**已闭合**（原事实与原处置段保留并加删线，「本仓不登记消费不到的键」那句撤销在行内注明）、A.2 的 G25 行补 2026-10-04 复核仍开放的实测结论（`bit_for` 仍无 `VK_NUMLOCK` 分支，`async_modifiers()` 只作激活期播种的取数来源）、A 的复核日期补 2026-10-04；`ARCHITECTURE.md` §9.4 回写排版选项的同源装配；`AGENTS.md` §6 新增本棒现状条目并改写「框架侧开放缺口」账目（G23 / G24 出账、G25 标为唯一开放项）；`PLAN.md` §6 的 G23 / G24 出账。版本脚注 v0.46 → v0.47。

---

## v0.46（2026-10-04）pane 容器界面腿 `ui::WorkspaceView` 落地并配十六例无头证人（裁决 7.49）

**动机**：裁决 7.42 / 7.43 / 7.48 三根棒的「未落」项都点名同一件事——`ui::WorkspaceView` 的接线（按键入口的前置过滤、把手命中与拖拽、按 `layout` 落子矩形、去抖定时器的取消-重排）。本棒兑现它，把 `SPEC.FEAT.WS.02` 的四义与「另需」两条接成可用的分屏交互，并把 `SPEC.FEAT.XFER.01` 的去抖腿落到工作区层。落地时撞到六处原裁决未覆盖的事实，入册为裁决 **7.49**。

- **界面腿本体（`feat(ui)`）**：新增 `src/ui/workspace_view.{h,cpp}`（私有头，裁决 D1①：类声明含框架类型，故不进 `include/borealis/`），是 `ui::PaneTree` + `ui::workspace_command` / `ui::divider_step` + `ui::GridSizeDebounce` 三件的宿主，**只做接线不重述算式**。公开面是 `Hooks{make_pane, dispatch_grid, teardown_pane}` 三条装配层接缝 + 用例观测点（`pane_count` / `focused_pane` / `view_of` / `current_layout` / `flush_grid_sizes`）；绘制四层按视觉稿顺序铺 chrome 底 → 各 pane 自身 → 把手中段 → 焦点 pane 的 2 dp `accent` 内描边。chrome 三色（`#21222C` 分屏底 / `#4D4F63` 把手静止 / `#BD93F9` hover 与描边）是**文件内固定 token** 而非主题档（裁决 7.25 N6 的兑现），故用例把这三个值硬编码而不从实现读。
- **焦点读数的权威归属（7.49①，本棒最该留下的一条）**：`focused_pane()` 先遍历视图问框架的 `is_focused()`，全员为假时取 `last_focused_`（最后一次**真实观察到**持焦的那一格），而**不**回落 `PaneTree::focused()`。两条实测决定次序：`TerminalView` 在自己的 Press 分支置 `is_handled`，祖先收不到 pane 上的点击，故指针点选无法在本层事件里同步模型焦点；派发器在把手 Press **之前**就把焦点清成空（命中链上只剩不可获焦的本层），那一刻恰是「用户手里的键盘输入落在哪一格」最需要答案的一刻。兜底取陈旧投影的症状是「拖完把手后字发进了他没在看的那一格」。**该分支由且仅由 `handle_press_restores_the_pane_the_framework_was_showing` 守住**（变异「回落模型投影」此前全套仍绿——判据空洞，补齐后恰该例转红）。
- **三条框架侧的接线约束（7.49②③④）**：`on_layout` 必须把 `layout()` 产物里的每个子矩形 `set_bounds` 回节点——框架 `Container::on_hit_test_chain` 的默认实现按 `child.bounds()` 判包含并带出子节点全局 origin，不写回则指针事件根本到不了 pane 视图；每个视图以**紧约束**（`min == max`）布局，视口不得按需收缩，否则绘制落点与命中落点分叉。拖拽按沿轴**增量**折算（每次 `Move` 取当前读数减上一次读数），故去抖 / resize / `set_bounds` 在两次派发之间重排矩形也不影响跟手；本层因此只存 `DividerKey` 身份而不存 `PaneDivider` 副本（副本的 `extent_dp` 会陈旧，而钳位要求与 `move_divider` 同一次折算，7.42⑦）。切分出来的新视图须显式 `mount`——`Window::present_root` 只在**根变化**时挂载一次，而切分不改根，不补挂载则该视图 `on_mount` 里的闪烁档与主题订阅永不注册。
- **最小 pane 补内边距（7.49⑤，补 7.47③ 漏项）**：`max(20 列 × 格宽, 3 行 × 格高) + 2 × padding`，取当帧所有格的最大值（一条把手两侧共用同一层下限）；`padding` 是 `ui::GridGeometry` 的既有字段。首帧度量未就绪（`cell_width <= 0`）的那一格**跳过**而非贡献 0——0 会让拖拽毫无钳位；一个视图都没度量时回落 `LayoutSpec{}` 的库内保守下界。
- **一条「等价变异体」的如实登记（7.49⑥）**：去抖的「每次重排先 `cancel()`」注入后**全套仍绿**——`GridSizeDebounce::due(now)` 自按时刻把关，陈旧回调早来一次只问到一张尚未到期的空表，故在**发出的 resize 序列**上与「只重排不取消」逐条相同。本棒不伪造用例，改在 `arm_debounce` 注释与本条写明其价值在**生命周期**（`TimerHandle` 只置取消标志，而每次重排覆盖 `debounce_timer_`；若前几个句柄留在调度器里，本对象消散后那些闭包仍会回调 `this`）。定时器那一腿的自动化判据缺位，须真机走查或框架侧给出可观测的待处理定时器数。
- **一条判据写法的教训（7.49 验收段）**：竖切（两格上下排列）的「总行数守恒」不能照抄横切那条例子断「严格少」——8 dp 的把手可能整个落在行高量化的余数里（`13 + 13` 恰等于单叶的 `26`），断严格少就是在断量化运气；改为「每格严格少于一整屏 + 总数不许多」。
- **视口的两条注入接缝（同一条 `feat(ui)`）**：`TerminalView` 新增 `GridSizeSink`（期望行列交给注入的 sink，**缺省仍直发**，故既有像素与集成用例零改动）与 `KeyPreFilter`（按键前置过滤，命中即认领、不发会话），并开三个只读观测点 `grid_geometry()` / `font_size_pt()` / `scrollback_rows_from_bottom()`。前者是 `SPEC.FEAT.XFER.01` 去抖腿的落点，后者是裁决 7.47②「本棒键位不经 `ShortcutRegistry`」的实现形态。
- **验收（`test(ui)`）**：`tests/integration/itest_workspace_layout.cpp` **十六例**——视觉稿 `UI_WORKSPACE_INTERACT.draft.md` §5 的四条界面判据逐条落地（各格行列数＝自己的矩形 ÷ 格步长、拖把手改比例而总列数守恒、`Alt+方向键` 后焦点视图变了而源格选区文本与距底行数逐字不动、连续 resize 期间尺寸只结算一次），另覆盖 chrome 三色逐位、把手 hover / 拖拽 / 等分三态、焦点描边的落点与仅分屏态、关闭后焦点交接、越界钳位、陈旧句柄被拒、新 pane 挂载与回收、把手命中链只含自身、裸方向键恒归会话（`CSI C`）。非 e2e 通道 **31 项全绿**。五条变异注入各有预期证人转红：不落子矩形（六例）／焦点兜底改取模型投影（一例）／拖拽改按按下时刻绝对差值（五例）／新 pane 不补挂载（一例）／前置过滤恒不认领（八例）；第六条（去抖 `cancel`）即 7.49⑥ 的等价变异体。变异 F 首轮触发段错误且日志为空，以 `--filter=` 逐例 bisect 定位到一处**用例自身**的裸解引用（接线断时踩空指针而不是报失败），改为先 `AURORA_TEST_REQUIRE` 再取成员。
- **仍待人工 / 未落**：装配层 `src/main.cpp` 仍持单会话、未把 `WorkspaceView` 挂上窗口，故本棒交付的是**可测的界面腿本体而非用户可见的分屏**；拖把手跟手、`Alt+→` 连按、`Ctrl+Alt+方向键` 是否被系统或显卡热键吞、焦点描边的肉眼观感属真机走查（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），本条**不宣称「可用」**。标签条界面腿（#90）与装配层多会话化（#91）随下一棒。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.49（六条 + 代价 + 验收）、追加范围改为「7.37–7.48 于 2026-10-03、7.49–7.50 于 2026-10-04 追加」（7.48 原误记在 10-04 一列，按 `git log` 实测日期订正）、`SPEC.FEAT.WS.02` 与 `SPEC.FEAT.XFER.01` 各加「落地现状」段（含「真机判据未验」的明确边界）、附录 A.2 的 G25 行末删除一段误粘进去的 G24 原文（该重复是 v0.45 那棒的提交缺陷，非本轮改动引入）；`ARCHITECTURE.md` §9.2 把「唯一触达 `au::Painter` / `au::Widget` 的实现收口在 `terminal_view.cpp` 一个翻译单元」订正为「绘制侧的实现收口在 `src/ui/` 的两个翻译单元」；`AGENTS.md` §2 目录表与 §6 现状回填；`PLAN.md` §8 的 `WS.02` 界面腿状态改判；`codespec/UI_WORKSPACE_INTERACT.draft.md` §4 的「与现状的差距」按实测逐条标注已达 / 未达；`include/borealis/ui/workspace_keys.h` 文件头的键位总表引用由 `§4` 订正为 `§2` 的面板 4（§4 实为「与现状的差距」，原引用是死链）。版本脚注 v0.45 → v0.46。

---

## v0.45（2026-10-03）分屏与标签条的交互态视觉稿出稿并自拍十一处判据（裁决 7.47，新登记 G25）

**动机**：裁决 7.42 / 7.43 各把「界面腿开工前先补一版分屏与标签条的交互视觉稿」列为前置（AGENTS.md §4.4 的「UI 编写前先出设计图评审」），本棒兑现它——交付的是**稿与判据，零代码改动**。屏 1（`codespec/UI_WORKSPACE.draft.svg`）画的是静止态（各控件在什么位置），本稿补它没有的那一维：**状态之间怎么转**（切一刀、拖一下、`Alt+→` 之后长什么样）。人不在场，按「不确定即取最优解并回写裁决」的授权自拍 D1~D11 全部取建议项，入册为裁决 **7.47**（十一条 + 代价 + 验收）；定「本棒键位走哪条派发通道」时读 Aurora 当日活动分支实测出的修饰态缺陷新登记 **G25** 并派发。

- **稿的形态（`docs`）**：`codespec/UI_WORKSPACE_INTERACT.draft.{md,svg,png}` 三件，九面板 = ①切分 / 并入 / 塌缩五幅小窗 ②把手五态（静止 / hover / 拖拽 / 钳位 / 等分）③焦点标识与 `Alt+方向键` 路由（含「严格居侧」负例）④键位总表（十行，本稿唯一键位来源）⑤标签条五态（×2 放大绘制以便看清 6 dp 圆角与 20 dp 命中区，规格仍按 dp 读）⑥就地重命名三口 ⑦关闭确认与末位标签 ⑧栏位宽与溢出滚动 ⑨25 条判据清单（双列）。逐格矢量法与其余四稿同源，出图脚本一次性不入库、**入库的 SVG 源即事实来源**。
- **一条需求解释随本条落定（7.47①）**：`SPEC.FEAT.WS.02` 原文只写「方向键路由」，而 `SPEC.FEAT.INTERACT.01` 要求方向键完整转发至 PTY——`vim` / `tmux` 的导航键不可让渡，故本条解释为 **`Alt+方向键`**，裸方向键恒归会话。`Ctrl+方向键` 与部分 shell 的词跳转冲突，不取。
- **派发通道的三条读源实测（7.47②，本棒最该留下的东西）**：`Application::open_window` 给每个宿主装 `Window::set_key_pre_handler` 转 `ShortcutRegistry::handle`，而该钩子的文档语义就是「命中即消费，**不再向焦点控件派发**」⇒ 快捷键先于任何控件看到 `KeyEvent`，就地重命名的编辑器持焦时会被抢走键，且 `ShortcutScope::Focus` 的判据只是「本宿主有任一焦点控件」这个 bool、不是「哪个控件」；`KeyCombo::matches` 的修饰态是**整字节相等**而 `ModifierKey::NumLock = 1U << 4U` 与四个可按住的位同处一掩码、四后端均盖章 ⇒ 锁定态下快捷键整层不匹配（**G25**）；`set_key_pre_handler` 是 `std::function` 赋值的**替换**语义，应用侧装上就顶掉框架那条。三条合起来判死了「本棒键位不经 `ShortcutRegistry`」，改由焦点 pane 的按键入口前置过滤，键位绑定表是纯逻辑件且只比 Shift / Ctrl / Alt / Meta 四位，故 G25 回货后键位可原样迁回而不动任何判据。
- **其余九处口径（7.47③–⑪）**：最小 pane 按 `max(20 列 × 格宽, 3 行 × 格高)` 现算（固定 dp 在 6 pt 与 72 pt 下差一个数量级，`LayoutSpec::min_pane_dp` 的 48 dp 只是库内下界）；等分＝把手双击 + `Ctrl+Shift+0`、步进＝`Ctrl+Alt+方向键` 且与拖拽**共用** `move_divider` / `equalize` 一条算式；切分新 pane 挂新会话（复制输出属 `SPEC.FEAT.WS.09`）；栏位宽实测钳 `[96, 240]` dp、溢出走横向偏移而非压缩（压缩会让「实测宽」成第二真值源）；就地重命名用框架 `TextInput` 的子类补 Esc 与失焦两钩子、空串提交即撤销重命名（裁决 7.43③ 的让位规则，不另立「恢复默认名」）；关闭确认取「任一 pane `Session::alive()`」且一次确认关整张标签；焦点标识取 2 dp `accent` **内**描边且仅分屏态（内描边不改行列数，故焦点态变化不触发尺寸下发）；尺寸去抖 50 ms 落**工作区层**（视口层各自去抖会让下发次数 ×N），`TerminalView` 因此把期望行列交给注入的 sink 而非自己直发；帧泵范围为全部 pane 每帧都排（只排选中标签会让隐藏标签的脏队列积压到背压上限、切回来先看到旧帧）。
- **G25 的登记与派发**：`KeyCombo::matches` 整字节相等对锁定态位不可用（Windows 键盘 NumLock 常开 ⇒ `SPEC.FEAT.PREF.04` 的应用内快捷键在该锁定态下永不触发），且 Win32 的 `ModifierKeyTracker::bit_for` **无** `VK_NUMLOCK` 分支，该位只随激活 `seed()`、只随失活 `clear()`，故激活期间用户切换 NumLock 不更新它（此条为读源结论，真机表现未走查，文中已标注）。按裁决 7.13① 派发 Aurora 侧补「公共 API + 单测 + 文档回写」，可派发任务书在对话内交付（决策钉死、含门禁与回写落点）；本仓**不等不绕**、不私改匹配算式也不轮询物理锁定态自造第二份修饰真相。
- **验收**：本棒零代码改动，故**无新增用例**；非 e2e 通道维持 **28 项全绿**（构建与测试面未动），吞吐门禁无涉及。稿自身的验收面写在 `UI_WORKSPACE_INTERACT.draft.md` §5——纯逻辑侧只有键位绑定表与「期望行列 → 去抖后一次下发」两件补 `utest_*`，界面腿侧以 `itest_render_viewport` 与新计划的 `itest_workspace_layout` 断「切分后各 pane 行列各等于自己的矩形 ÷ 格步长」「拖拽改比例而总列数守恒」「`Alt+方向键` 后焦点视图变了而选区与滚动位置不动」「连续 resize 期间 `Session::resize` 只被调用一次」「隐藏标签排两帧后切回来内容与权威网格一致」。
- **仍待人工**：拖把手跟手、`Alt+→` 连按、重排导引线的落点直觉、就地重命名的 IME 组合态属真机走查（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），本条**不宣称任何界面腿「可用」**——本条结的是判据，不是实现。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.47（十一条 + 代价 + 验收）、§7 追加范围补到 `7.37–7.47`、`SPEC.FEAT.WS.01` / `02` / `XFER.01` 三条各在实现约束段末补「交互态判据已出稿并拍板」的落点引用（含方向键触发键那条解释）、附录 A.1 新增「快捷键的派发位置与匹配口径」一行（把三条读源实测写进去以免下棒重查）、A.2 新增 G25 行、A.3 补 G25 派发句、版本脚注 v0.44 → v0.45；`UI_OVERVIEW.draft.md` 的分工表新增本稿一行并把过期的「D1~D7 待拍板」订正为「已并入裁决 7.38①」、§1 清单新增「5 分屏与标签条的交互态」屏；`ARCHITECTURE.md` §8.1 / §8.2 回写界面腿的接缝形态；`PLAN.md` §6 的 G25 入账与本棒收口为「判据已定、界面腿待开工」；`AGENTS.md` §6 现状回填。**另把 v0.44 那棒漏填的 `PLAN.md` §8 一并订正**（渲染第二棒入「已落地」清单并从「未开工」处摘除、`XFER.01` 去抖与 `WS.01`/`02` 界面腿两处状态改判为「已拍板待实现」、验证现状的 CTest 计数按 `ctest -N` 实测由 27 项改为 28 项并补入 `utest_font_choice` 与像素用例的 24 例）。

---

## v0.44（2026-10-03）字体与排版量第二棒：G22 接线闭合、双宽断跑修正、实测网格入测量契约（裁决 7.46，新登记 G23 / G24）

**动机**：v0.43 收尾时明写「开放缺口只剩 G22，其实测复验与接线形态另棒做」，本棒兑现它，并把 `SPEC.FEAT.RENDER.02` 的剩余交付面（字号含 Ctrl+滚轮、行高/字距可调、CJK 缺字回退不破坏网格）一次接完。既定切分是「视口：font 参数化 + Ctrl+滚轮缩放 + 度量重建 + 像素用例」与「装配层接线 + 回退证人用例」两段，落地过程中六处需要拍板，新增裁决 **7.46**（六条 + 代价 + 验收）。

- **排版件的量化吸附（`feat(ui)` + `test(ui)`，7.46①）**：`ui::apply_typography` 把行高倍数与字距折进物理像素——字距先 `lround(dp × scale)` 取整再**回填** `TypedMetrics::letter_spacing_dp`（整数 px ÷ scale），绘制侧的 `TextLayoutOpts::letter_spacing` **必须**取回填值而非配置原值；行步长 `lround(height_px × line_height)`，余量按 `glyph_top_px` 记上半、余数归行盒下沿，`ascent_px` 随上半 leading 写回，于是 `decoration_rects` 与光标块零改动跟到新基线。框架的 `letter_spacing` 只加在相邻字形之间（整串 n−1 次）且无「按格推进」档位，故只有整数像素字距才让第 k 个字形恰好停在第 k 列左沿。
- **配置两键与取值域归属（`feat(config)` + `test(config)`，7.46②）**：`appearance.font_line_height`（缺省 1.0，域 `[1.0, 3.0]`）与 `appearance.font_letter_spacing_dp`（缺省 0.0，域 `[0.0, 8.0]`）入 schema，域判定只在 `config::Store` 读侧经 `scope.real(key, fallback, lo, hi)` 把守（裁决 7.27③ 既有形态），`ui::apply_typography` 因此不含任何 `clamp`——两处各夹一遍就会给出两个不同的「最大值」。
- **字体族回落判定件（`feat(ui)` + `test(ui)`，7.46③）**：`ui::choose_font_family(configured, catalog)` 出 `{family, verdict}` 三态（`Configured / NotMonospace / Unlisted`），内置族名 `kDefaultMonospaceFamily` 收敛为**单源**（配置默认值与回落值同取自该头）。两条框架实测口径决定其形状：族名匹配逐字节精确且区分大小写（故不自造宽匹配），枚举与 `resolve_faces` **同源**（故「不在列表里」即「用了也只落到非等宽默认链」，不需第二处探测）。装配层只在启动时取一次目录（首次调用递归扫字体目录并逐个开 face，同步 IO），非 `Configured` 走 `AURORA_LOG_WARN` 而非对话框；取全量而非 `monospace_only` 子集，是为了让「装了但非等宽」与「压根没这个族」两档降级可分别留痕。`TODO(SPEC.FEAT.RENDER.02)` 桩就此消除。
- **视口的字号缩放与度量重建（`feat` + `test`，7.46⑤）**：`on_scroll` 按 `ScrollEvent::modifiers` 的 Ctrl 位分流（G18 回货字段在选区之外的第二个消费点，本仓不轮询物理按键态），步长恒 ±1 pt、钳 `[6, 72]`（本仓自定，需求与视觉稿未给数值），**到界值即返回 false 把那一档让回本地回看**；字号与 scale 共用同一条 `metrics_stale_` 重取路径，故 `SPEC.FEAT.RENDER.02` 的字号调整与 `RENDER.05` 的「缩放变更后重算且不裂」是同一条判据。`font_size_pt()` 是用例的观测点（步长与钳位这一条算式不该从像素反推）。
- **双宽字形自成一跑（`fix(ui)` + `test(ui)`，7.46④——本棒真正的缺陷修正）**：框架每字形的推进是 HarfBuzz 给出的**该 face 自己的**物理 px advance，无等宽覆盖；实测 Cascadia Code 14 pt 格宽 11 px（两格 22 px）而回退面汉字推进 19 px，于是同一 run 内紧随双宽字形之后的字形整体挪位且**逐字累积**。修法是 `layout_row` 遇延续格即断跑（延续格不进文本也不画豆腐块，宽度由基础格那段占住），下一段从自己的格左沿重新起排。**不改框架推进**的原因：那属渲染路径私改（AGENTS.md §5 第 3 条），而断点是纯切分边界的决定、本就在应用侧 run 表里。既有那例「双宽不占列」的断言随之按新口径改写为两段（不是回退实现）。
- **实测网格成为吞吐测量契约的一部分（`chore` ×2，7.46⑥）**：`borealis_bench` 产物新增 `grid` 字段、`tools/check/check_perf_gates.ps1` 对基线 `capture.grid` 逐样本硬判（缺字段即 FAIL——拿不同格数的读数比大小是无声的错误比较）。起因是本棒网格由 `24x73` 变 `28x86`：Aurora 侧内嵌并注册 Cascadia Code 使 14 pt 整格从 13x26 变 11x22 px，同一 960×640 dp 窗口格数 +37.4%，帧时类指标 +42% 而**每格成本仅 +3.7%**（0.720 → 0.747 µs，在 ±10% 窗内）。这不是回归而是每帧工作量变大，故按 G19 先例**整体重捕获**七档参考值、在 `capture.supersedes` 留三代痕迹，**未放松任何门禁线**。归因实验在册：回退 run 断点、去掉排版选项重载都不改变读数，排除了「本仓代码变慢」。
- **验收**：`utest_font_choice` 八例（四档判定 + 四种误拼一律 `Unlisted` + 空目录仍给内置族名 + 配内置族本身判 `Configured`）、`utest_cell_layout` 41 例（排版件的恒等 / 取整 / 基线跟随七例，含双宽断跑的列区间口径与「每个双宽字形都断一次跑」）、`utest_config` 的两键域外回落与「默认 `font_family` 恒等于 `ui::kDefaultMonospaceFamily`」的同源证人、`itest_render_viewport` 24 例（行高 / 字距 / 中性档 / Ctrl+滚轮 / 钳位五例以 `monospace_cell` + `apply_typography` + `make_geometry` **独立复算**格网当预期，第 24 例逐列比窗口像素）；非 e2e 通道 **28 项全绿**。变异自证共八条：去掉等宽位判据 → 两例、回落改成逐字透传用户原名 → 六例、去 `glyph_top` / 批量入口丢字距 / 字距不进列步长 / 缩放不置 stale / 到界仍吞事件 → 各只转红一例、去掉断跑的 `flush` → 两处列位判据全断、只在首个双宽格断跑 → 第二腿转红。吞吐门禁按 28×86 重捕获后复跑 3 次 **7/7 PASS**，且新门禁腿**以真实陈旧样本自证**（重捕获前用旧基线实跑即刻转红，无需人为注入）。一处判据写法的教训随本棒在册：跨两套格网比像素必须用**锚格左沿的同尺寸窗口**（新增 `cell_window`），用「带宽 = 各自格宽」的 `cell_band` 会在字距例里首版误判一次。
- **新登记两条缺口并派发 Aurora 侧（裁决 7.13①，任务书在对话内交付）**：**G23** 缺字回退链无法按族配置（`Font` 无回退链字段，`resolve_faces` 拼的是全局默认链，`add_default_face` 只往 `""` 与 `"sans-serif"` 两键挂面且不产生族名）——本仓因此**不登记消费不到的配置键**，`RENDER.02` 的回退链腿以「框架全局回退已足以让汉字显形」的形态交付；**G24** 批量文本入口无「按网格强制单格推进」档位（`TextLayoutOpts` 只有四项且整批共用）——不阻塞交付，断跑已消除累积漂移，登记是为了把「双宽字形自身仍占 19 px 而非 22 px」与「每帧 run 数随 CJK 密度上升」两处局限留在册。
- **仍待人工**：CJK 上屏观感、字体族切换的肉眼判据、Ctrl+滚轮缩放手感（会话锁屏下 `SendInput` 静默失效，裁决 7.31①），故本条**不宣称 `SPEC.FEAT.RENDER.02` 的「呈现为字形而非豆腐块」已验**——本棒结的是「列位与光标位置一致」那一半（自动化可判），豆腐块与否属框架 shaping。字体族与排版量的 UI 入口随 `SPEC.FEAT.PREF.02`。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.46（六条 + 代价 + 验收）、追加范围补到 `7.37–7.46`、`SPEC.FEAT.RENDER.02` 段末新增「落地现状」段（两半验收的分工）、附录 A.1 新增「系统等宽字体族枚举」与「排版选项的覆盖面」两行并把回退链的全局性写进原行、A.2 的 G22 行改闭合形态（原始事实保留 + 回货与约束 + 接线面）并新增 G23 / G24 两行、A.3 补 G22 闭合与两条新派发、版本脚注 v0.43 → v0.44；`AGENTS.md` §6 现状回填；`PLAN.md` §6 的 G22 行改闭合、G23 / G24 入账、`RENDER.02` 收口为「两半已交付、UI 入口与回退链可配待后棒」；`ARCHITECTURE.md` §9.2 补字体与排版量的取用形态与「双宽字形自成一跑」的切分规则。

---

**动机**：v0.42 的动机段明写「G21 / G22 的复验结论另棒做」，本棒兑现 G21。Aurora 侧把建窗期的 DPI 取数改为按「窗口即将落位的显示器」，本仓**消费侧零改动**复验，判据是「≠100% DPI 屏上请求 `960 x 640` dp，首帧客户区逐位等于 `960 x 640` dp」——这一档此前正是首屏行列数的唯一阻塞项。落定过程新增裁决 **7.45**（三条 + 代价 + 验收），本棒无代码改动，只结复验与文档。

- **回货形态（读 Aurora 当日活动分支实测）**：`CreateWindowExA` **之前**按即将传入的窗口矩形调 `refresh_scale(&wa)` 取换算基准，句柄就绪后再调一次 `refresh_scale()`，两处**同一函数**（G17 的病根正是「cached 成员与两处现调各记各的账」，合一后不再发散），物理尺寸统一经 `to_physical(logical_size)` 换算。
- **复验读数（7.45②）**：本机 144 DPI（150%）屏，装配层请求 `960 x 640` dp，`GetDpiForWindow = 144` 且 `GetClientRect` 给出 **1440 x 960 物理 = 逐位等于请求 dp × scale**；登记时的病灶读数是含不可见边框 `982 x 696` 物理（折回 dp 仅 `655 x 464`，即请求值被原样当物理像素用掉）。WGC 截屏另证终端底色铺满客户区、PowerShell 首帧正常。两读数的差即本条证人。
- **一条取数陷阱（7.45①，本棒最该留下的东西）**：先取的 UIA / 无障碍侧窗口 bounds 报 `975 x 677`，据此几乎判定「仍差 1.5 倍」——该值实为外框 `1462 x 1016 ÷ 1.5` 的 **DPI 虚拟化逻辑值**，把它当物理像素判就会把已闭合的判据读反。此后凡尺寸类真机判据一律走 Win32 直调（`GetClientRect` + `GetDpiForWindow` 成对给出），不采信 UIA bounds。
- **本仓零改动的依据（7.45③）**：装配层始终只按 dp 给 `WindowOptions::size`，行列数由 `TerminalView::on_layout` 从框架给的可见 dp 派生，换算点全在框架侧——这正是裁决 7.36⑤ 登记时「回货零改动、首屏尺寸一闭环上屏即自洽」预期的兑现。
- **验收**：无新增用例（判据是应用外取数，不进任何自动化通道，故只在文档留读数，7.45 代价段）；非 e2e 通道维持 **27 项全绿**，吞吐门禁无涉及。
- **仍待人工**：真机键入与手感腿因会话锁屏仍不可验（`activate_window` 报 `foregroundHwnd: 0`，裁决 7.31① 的可用面判据），故本条**不宣称 `SPEC.FEAT.INTERACT.01` 全绿**，只结尺寸与覆盖两腿。
- **开放缺口只剩 G22**：`SPEC.FEAT.RENDER.02` 的字体族枚举腿。其实测复验与接线形态另棒做（G22 的框架侧数据源已到货，消费即下一棒）。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.45（三条 + 代价 + 验收）、§7 追加范围补到 `7.37–7.45`、`SPEC.FEAT.INTERACT.01` 段末把 G20 / G21 改为「双双闭合」并留真机读数、附录 A.1 的 DPI 行残留腿改闭合、附录 A.2 的 G17 行末与 G21 行头/行末改闭合形态（原始事实保留 + 回货与复验 + 取数陷阱）、附录 A.3 补 G21 闭合句、版本脚注 v0.42 → v0.43；`AGENTS.md` §6 现状回填（已闭合缺口补 G21、开放缺口只剩 G22、真机走查条的「首屏那一档」收口）；`PLAN.md` §6 的 G21 行改闭合、M1 行与尺寸接缝行随之收口。

---

## v0.42（2026-10-03）主键盘 `Insert` 的回货接线（附录 A.2 的 G20 闭合，裁决 7.44）

**动机**：`SPEC.FEAT.INTERACT.01` 的键入腿在 2026-10-02 完成 G14 / G15 / G16 回货接线后，在册的欠账只剩「同批复验新登记的 G20」一条——本仓的编码层早已把 `Insert` 的字节形态写对（波浪号族 `2` 号位），缺的只是框架侧那个键码。2026-10-03 复货核实 Aurora 侧已补键码与四后端映射，本棒接完这最后一处折算，并顺手把复货核实到的形态差异如实登记（G21 / G22 的复验结论另棒做，本条只结 G20）。落定过程新增裁决 **7.44**（三条 + 代价 + 验收）。

- **接线面只有两处（`feat(term)`，7.44①）**：`term::KeySym` 加同值同段的 `Insert = 123`、`special_form()` 加一个 `case` 落到 `.tilde = 2`，编码算式与修饰掩码一律不动（`CSI 2;mask~` 本就已实现）。框架侧的形态是 `KeyCode::Insert = 123` 作**后补显式初值段**接在 `KP_9`（122）之后，映射四条：Win32 `VK_INSERT`、GLFW `GLFW_KEY_INSERT`、X11 keysym `0xFF63`（Wayland 的 xkb keysym 走同一张表）。视口的事件折算是无差别的 `static_cast`，故本仓**无需**新增分发分支——这也正是「接线面只在键码折算处」的由来。
- **后补段的逐值对齐纪律（7.44②）**：`Insert` 语义上属「编辑 / 导航」段却不插回该段——既有段靠隐式连号，中间插一项会让其后全部取值整体位移，而两侧互转就是 `static_cast`，位移不报错、只会全盘静默错位。对齐用例自此对**每个后补段写死一个显式数值锚点**（`KP_Insert` 的 100、`Insert` 的 123）：只比「`static_cast` 往返相等」抓不到两侧一起挪的整段位移。
- **`Insert` 与 `KP_Insert` 并存且不可互换（7.44③）**：X11 / Wayland 因两区 keysym 各占独立码点（`0xFF63` / `0xFF9E`）而天然可分；Win32 两区共用同一 `VK_INSERT`、来处只在 `lParam` 扫描码里，而该判据在其余导航键上已被框架实测证伪（裁决 7.37② 的「共用码位不可分」），GLFW 的键码表只有一个 `INSERT` 常量——故两后端的**小键盘导航区恒给主档**。本仓因此不自造扫描码二次判定：两区的遗留编码本就同为 `CSI 2~`，二次判定只会让 Win32 走上一条与其他后端不可对齐的私有口径（框架头注释亦把分流责任交给消费方，要区分请自行读 `ModifierKey::NumLock`）。
- **验收（`test(term)`）**：`utest_keymap`（11 例，含 123 锚点与 `2` 号位的裸键 / Shift / Ctrl 三形态）+ `itest_key_input`（8 例，新增 `h.press(KeyCode::Insert)` → `\x1B[2~`），非 e2e 通道 **27 项全绿**；两次变异自证各与其证人——删 `special_form()` 的 `case` → 单元一例红**且集成例的 `press()` 直接返回 false**（未编码即未消费，链路整条失效，故证据落在两层而非只在单元层）；把 `Insert` 的值改成 121（撞上 `KP_8`）→ 对齐 / 编码 / 段谓词三例红，其中编码那一例红的机制是 `is_keypad()` 前置于 `special_form()`，同值即被改道到小键盘档。
- **提交形态**（三个分层本地提交）：`feat(term)` 键码折算与编码 `case` / `test(term)` 逐字节与链路断言 / `docs` 裁决 7.44 与 G20 闭合回写。
- **仍待人工**：`Insert` 的**真机对端接受度与手感**（锁屏会话下 `SendInput` 静默失效，裁决 7.31① 的可用面判据），与 `Tab` / Alt 系 / 小键盘同档未结；本条只结「事件链上有该键 + 字节形态正确」两层，不宣称 `Insert` 在对端程序里行为正确。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.44（三条 + 代价 + 验收）、§7 追加范围补到 `7.37–7.44`、附录 A.1 增「主键盘 `Insert` 键码与四后端映射（G20 关闭）」一行并修掉 G16 行末的「仍未建模」、附录 A.2 的 G20 行改闭合形态（原始事实保留 + 回货形态）、附录 A.3 与本条同一路径的叙述补 G20 闭合、`SPEC.FEAT.INTERACT.01` 的框架现状段末追复货接线一句、版本脚注 v0.41 → v0.42；`AGENTS.md` §6 现状回填（键入腿的 `Insert` 子腿闭合、真机腿仍待人工、开放缺口减为 G21 / G22）；`PLAN.md` §6 的 G20 行改闭合、M1 行与按键接缝行随之收口。

---

## v0.41（2026-10-03）标签列表纯逻辑件：顺序、选中与三名来源折算（`SPEC.FEAT.WS.01`，裁决 7.43）
**动机**：v0.40 的「仍待落」里点名 `SPEC.FEAT.WS.01` 的标签列表件是工作区层的另一块纯逻辑前置，本棒兑现它。框架 `TabBar` 实测只覆盖选中切换与关闭（附录 A.1：事件只有 `on_change` / `on_close`，配色还是全局的），既无拖拽重排也无逐标签图标与状态角标——所以本件不是它的壳，而是「第几格是谁、该显示哪个名字、下一步切到谁」的唯一真值源；栏位宽、图标与关闭按钮命中都留给绘制侧。落定过程新增裁决 **7.43**（九条 + 代价 + 验收）。

- **单一真值源与配置键（`feat(ui)` + `feat(config)`，7.43①②）**：`TabNamePriority` 的唯一定义处落在 `include/borealis/ui/tab_strip.h`，`config::TabNamePriority` 只是别名（`RightClickAction` 7.41① 与 `PasteNewlinePolicy` 7.33 同一条口径）——优先级判定发生在纯逻辑件，而 `config` 已含 `ui/palette.h`，本头反向含 `config/settings.h` 就是模块环（架构 §2.3）。`SPEC.FEAT.TERM.07` 把「手动重命名的优先级更高」明标「（可配）」，故新键 `appearance.tab_name_priority`（落盘名 `manual_wins` / `osc_wins`，缺省前者）；归**外观域**而非会话域，因为它只决定标签显示什么、不决定会话行为。一条 `is_same_v` 单测锁「配置字段就是该枚举本身而非第二张表」。
- **三名折算（7.43③⑨）**：三个来源（档案显示名、`OSC 0/2` 标题、手动重命名）折成一个显示名由自由函数 `resolve_tab_name` 单点给出，`TabStrip` 的显示名入口只是「按 id 取来源再转调」。**空串是「该来源未设置」而非「名字就是空」**，于是 OSC 把标题设回空与用户撤销重命名是同一句「让位给下一级来源」，不必另立「重置为默认名」这个动作；三者皆空回空串而**不造占位文案**（占位文案属绘制侧且要随语言词条走）。名字用 `std::u32string`，与 `term::OscState::title` 同一坐标空间，接缝零转码。
- **重排的基准（7.43④）**：`move(id, to_index)` 的目标下标以**其余标签**为基准（`[0, count()-1]`，越界拒绝且表不变）。指针落在两格之间时算出的插入位天然不含被拖那格；以原表下标为基准就会在「往上拖」与「往下拖」两个方向上差一格——那是拖拽重排最常错的地方，故本条以方向对称的两例钉住。`move` 与 `close` 都以**身份**为锚保住选中位（下标在这场移动里不作数）。
- **末位与新建（7.43⑤⑥）**：表里只剩一个标签时 `close()` 返回 false——关掉最后一个标签就是关窗口，属 `SPEC.FEAT.WS.03`（与 7.42⑤ 的 pane 同口径），本件若允许清空，装配层就得自己判「空了没有」，两处各判必分叉。`add` 追加表尾并**立即选中**（「开一个新标签」下一秒就要在里面打字，留给调用方补一句 `select` 会造成两种手感）；`id` 重复被拒且**不覆盖原有名字**（否则装配层的一次重复调用会静默改写已有标签的档案名）。
- **选中交接与循环切换（7.43⑦⑧）**：关掉选中的那格时交接在**移除之前**的次序里算——中间格交给下一格、末位退给上一格（7.42⑥ 同一条理由）；关掉未选中的格时，只有它在选中位**之前**才整体左移。`select_relative` 首尾相连（`Ctrl+Tab` / `Ctrl+Shift+Tab` 是按住修饰连按的，撞到端点就停会让连按失效，绕到另一端才是浏览器与 Windows Terminal 建立的预期），且返回值语义是「**是否发生了切换**」：整圈回来、单标签的任何步长、空表与 `step == 0` 一律 false 且状态不变。
- **验收（`test(ui)`）**：新建 `tests/unit/utest_tab_strip.cpp` **25 例**（空表无选中、追加即选中、身份重复被拒、选中格关闭的两档交接、未选中格关闭的两侧分工、末位不可关、按身份选中与不存在被拒、相对切换的两端环绕 / 多步 / 整圈 / 单标签、重排的两方向同基准 / 两端 / 原地 / 选中保持 / 越界与不存在被拒、三名折算的缺省档与 `osc_wins` 档 / 空串逐级让位 / 经 `TabStrip` 写入、配置字段与枚举同型）；`utest_config` 十一例把新键的**缺省档、全量往返等值、畸形值只让该键回落并留痕**三处入册。非 e2e 通道 **27 项全绿**（新增一条 CTest 项，`cmake/BorealisTests.cmake` 按 glob 自动挂载）。本件为纯逻辑、不在绘制路径上，故未重跑吞吐门禁。
- **七条变异自证，其中一条抓出判据空洞**：空串不让位 → 两例；重排基准改成原表下标 → 三例；去掉整圈判据 → 一例；去掉末位不可关 → 一例；交接改成取上一格 → 一例；配置读侧漏接线 → 两例。**第六条当时全套仍绿**——没有任何用例关闭「选中位之后」的未选格，于是「之后不挪」这一半结构上测不到，而漏这条的后果是选中位被推出表尾、`selected()` 读出不存在的标签（无条件 `--selected_index_` 还会在选中位为 0 时下溢成 `SIZE_MAX`）。补 `closing_a_later_unselected_tab_leaves_the_selection_alone` 后该变异才由该例转红，据此写进 7.43⑦ 的代价段。
- **提交形态**（四个分层本地提交）：`feat(ui)` 标签列表件（`tab_strip.{h,cpp}` 与 CMake 挂载）/ `feat(config)` `tab_name_priority` 键的别名与落盘读回 / `test(ui)` 二十五例与配置接线守卫 / `docs` 裁决 7.43 与现状回写。
- **仍待落**：`SPEC.FEAT.WS.01` 的**界面腿**（栏位宽度与溢出滚动、逐标签连接类型图标与活动角标、拖拽落点折 `to_index`、关闭按钮命中）与「关闭前确认（有运行中进程时）」——按「UI 编写前先出设计图评审」的规矩须先补标签栏与分屏的交互视觉稿；`tab_name_priority` 的 UI 入口随 `SPEC.FEAT.PREF.02`；`SPEC.FEAT.WS.04` 的图标与角标判据属会话侧，刻意不入本件（存了会话指针就无法脱会话单测，AGENTS.md §4.4 第 20 条）。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.43（九条 + 代价 + 验收）、§7 追加分组范围补到 `7.37–7.43`、`SPEC.FEAT.WS.01` 新增实现约束段指向 7.43①–⑨、版本脚注 v0.40 → v0.41；`ARCHITECTURE.md` §8.1 记 `TabStrip` 与框架 `TabBar` 的分工；`AGENTS.md` §2 目录表补 `ui/tab_strip.{h,cpp}` 与 `tests/unit/utest_tab_strip.cpp`、§6 现状回填；`PLAN.md` M1 行与待接接缝表同步。

---

## v0.40（2026-10-03）pane 树纯逻辑件：分屏四义的拓扑、比例与方向键路由（`SPEC.FEAT.WS.02`，裁决 7.42）

**动机**：M1 的出口判据里只剩工作区层整块未落，而 `SPEC.FEAT.WS.02` 的原文自己就写着「应用侧 pane 树需自研，这是本需求的主要工作量所在」——框架 `Splitter` 是二元分割器（附录 A.1），既不能嵌套也没有多子层，7.37③ 已核实这条属「公共 API 可组合、留本仓」。本棒交付的是这棵树**与绘制无关的那一半**：谁与谁同层、每层沿哪个轴、各层各占多大一份、方向键把焦点交给谁；界面腿（pane 容器 widget、把手拖拽、焦点描边、逐 pane 下发尺寸）另棒做，那条要先出视觉稿评审。落定过程新增裁决 **7.42**（十一条 + 代价 + 验收）。

- **形态（`feat(ui)`，7.42①②③）**：`ui::PaneTree` 是「多子容器 + 叶子」树，公开面只有 `split / close / focus / container_of / layout / move_divider / equalize` 与自由函数 `ui::route_focus`，公共头不含 Aurora 类型。三件结构决定：同轴切分**并入父层作兄弟**而不另立一层（否则三栏背两层同轴容器，等分与拖拽要跨层折算）；切分**只把 target 那一份一分为二**，其余子的相对长度逐字不动（用户预期是「别动我其他窗口」）；每层存**相对长度而非归一化分数**且**树不持几何记忆**——绝对尺寸只在 `layout()` 的产物里，故把手的 dp 位移直接折得进去、无需反解，resize / 增删 / 拖拽三条路径共用同一条折算。
- **关闭与焦点（7.42④⑤⑥）**：同层只剩一个子时该层**当场塌缩**（外层换成它唯一的子）；树里最后一个 pane **不归本件关**（那是关标签 / 关窗口，`SPEC.FEAT.WS.01` / `03`），`close()` 返回 false 而非把树清空。焦点交接在**移除前**的阅读序里算，取下一位、末位则取上一位——移除后序会变，而用户预期是「交给原来紧跟着的那一格」；该阅读序同时是标签内切换序与方向键同分取首序的来源，故由 `panes()` 单点给出。
- **折算与钳位（7.42⑦⑧⑨）**：`layout()` 是 const 且与 `move_divider()` 共用 `measure_along` + `distribute`，**钳位与像素吸附都不写回相对长度**，于是缩放窗口不会让历史拖拽「回弹」（两处各算一遍就会出现「拖时一个比例、松手另一个比例」）。常规钳位是两阶段（低于下限者抬到下限，差额按高于下限者的多余量比例扣），**整层连「每子都取最小」都放不下时一律均分**——宁可不满足最小值也不产生重叠或负尺寸，因为布局必须铺满父矩形，缺口会把窗口底色露出来。子矩形边界**吸附整数物理像素**且舍入余量并进最后一个子（与 7.28④ 同源：小数 dp 上的相邻边界会被抗锯齿糊成一条发灰的缝）。
- **标识与路由（7.42⑩⑪）**：分隔条用 `(ContainerId, slot)` 而非相邻两个 `PaneId`——嵌套层的一条把手两侧可能各是一个容器，pane 标识表达不出这种边界；容器标识只增不复用，故切分 / 关闭后残存的陈旧句柄能被认出并拒绝而不是指向另一层。方向键路由两级排序键「先垂直间隙（投影重叠者 0）、再主方向间隙、同分取布局次序首个」，且候选须**严格居侧**：跨过当前 pane 的那一格（整行铺底的兄弟）不在「这个方向」上，排除而非钳到最近；无候选即返回空值，焦点原地不动而不是绕到对角。
- **与 `SPEC.FEAT.XFER.01` 的分工**：树只出 dp 矩形，每个 pane 的行列数由绘制侧对它的矩形现算 `ui::make_geometry`，故**树不知道「格」的存在**，「切分 / 缩放 / 拖拽后向受影响会话重发尺寸」的责任在界面腿。`LayoutSpec::divider_dp` 缺省 8 dp 取自视觉稿 §2.2 的**命中区**（把手视觉宽 32×2 dp，但布局须按命中区留位否则抓不住）；`min_pane_dp` 缺省 48 dp 只是库内保守下界，界面腿须按「最小行列数 × 格步长」传入。
- **验收（`test(ui)`）**：新建 `tests/unit/utest_pane_tree.cpp` **18 例**（单叶铺满、切分只动 target、同轴并层、换轴嵌套、关闭合并与塌缩、最后一个不可关、焦点按阅读序交接、拖拽只动相邻两格、拖拽钳在最小值、等分、陈旧句柄被拒、边界吸附且整层铺满、空间不足退均分、零尺寸窗口不出负矩形、方向键跨层路由、严格居侧、身份重复 / 不存在被拒、任意深度），非 e2e 通道 **26 项全绿**。像素吸附那一例特意取 200% 缩放的三等分（522⅔ 物理像素）——800 dp @1.5 配 ¼ 权重的算式全都落在整数像素上，换任何一组「刚好整除」的参数这一例就抓不到漏吸附。
- **四条变异自证与一条实测教训**：把手长度不并进游标 → 四例转红；宽度绕过像素吸附 → 吸附与等分两例转红；去掉塌缩 → 塌缩例与「最后一个不可关」例转红；去掉严格居侧判据 → 两条路由例转红。首轮 18 例里有一例直接**崩溃**（`0xc0000409`，`*std::optional` 空值的调试期检查），根因不是测试写法而是实现：竖排子层的矩形原点把「沿轴起点」加到了 x 上（横排分支正确、竖排应加到 y），**单叶层与「横包竖」两种形态都抓不到，只有「横层里嵌竖层」的 2×2 布局才露馅**——症状是某一格的 y 坐标跑到窗外。该形态自此由 2×2 用例的四格坐标钉住，并写进 7.42 的验收段。
- **提交形态**（三个分层本地提交）：`feat(ui)` pane 树件与 CMake 挂载 / `test(ui)` 十八例单元 / `docs` 裁决 7.42 与现状回写。
- **仍待落**：`SPEC.FEAT.WS.02` 的界面腿（pane 容器 widget、把手拖拽命中、焦点 pane 2 dp 描边、方向键接线、逐 pane `Session::resize` 下发与去抖合并）——按「UI 编写前先出设计图评审」的规矩须先补一版分屏交互的视觉稿；`SPEC.FEAT.WS.01` 标签列表件（含 OSC 标题 > 手动重命名 > profile 名的名称优先级）是同一层的另一块纯逻辑前置；`SPEC.FEAT.WS.08` 的 pane 缩放（zoom）不在本件表达范围。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.42（十一条 + 代价 + 验收）、§7 追加分组范围补到 `7.37–7.42`、`SPEC.FEAT.WS.02` 的实现约束段末改指 7.42①–⑪、版本脚注 v0.39 → v0.40；`ARCHITECTURE.md` §8.1 记 pane 树的形态与「树不持几何记忆」的分工；`AGENTS.md` §2 目录表补 `ui/pane_tree.{h,cpp}` 与 `tests/unit/utest_pane_tree.cpp`、§6 现状回填；`PLAN.md` M1 行与待接接缝表同步。

---

## v0.39（2026-10-03）右键三态与粘贴呈现腿：上下文菜单、多行警告与逐块节流（`SPEC.FEAT.INTERACT.03`，裁决 7.41）

**动机**：v0.38 把选区界面腿本体接完，`SPEC.FEAT.INTERACT.03` 剩下的就是右键这一侧：三态可配（菜单 / 粘贴 / 选中即复制）、上下文菜单本体、多行粘贴警告、按 `PastePlan` 的逐块排期。这一段要裁决的仍不是画图，而是三处结构问题——`RightClickAction` 的定义该放哪一层、剪贴板 IO 落在事件回调还是帧边界、以及「没有浮层宿主就问不了确认」时到底发不粘贴。落地前实测了 Aurora 的 `Popup` / `OverlayHost` / `Dialog` 三个公共件的真实形态（框架只建模、不代劳派发），据此新增裁决 **7.41**（六条，其中 ③ 明确**不登记框架缺口**）。

- **右键决策件与枚举单源迁移（`feat(ui)`，7.41①②）**：`RightClickAction` 的唯一定义处从 `config/settings.h` 迁入 `include/borealis/ui/right_click.h`，配置侧改 `using ui::RightClickAction;`（落盘的 `context_menu / paste / copy_on_select` 名称→值映射逐字节不变，一条 `is_same_v` 单测锁同型）。理由是判定发生在绘制侧的纯逻辑件里，枚举留在 `config` 就得让 `ui/right_click.h` 反向含 `config/settings.h`，而 `config` 已含 `ui/palette.h`——模块环（7.32① 同因）。处置是纯函数 `plan_right_click(action, has_selection) -> RightClickPlan{intent, items}`，意图四值 `None / Copy / Paste / Menu`；`CopyOnSelect` 无选区回 `None`（空写会覆盖用户剪贴板原有内容），`Paste` 态与选区无关；`ContextMenu` 首版两项，无选区时「复制」`enabled=false` 即**置灰而非点击后静默失败**（7.38⑥ F-b）。
- **菜单与确认全部用框架公共 API 组合（`feat(ui)`，7.41③）**：装配层场景根换成 `au::OverlayHost`（`src/main.cpp`），视口内 `au::Popup` 承载一列 `au::Button`（`min_width` 定死条目宽度、`corner_radius=0` 让相邻条目拼成整板、置灰档取「前景向底色各半」而非框架缺省灰——后者在深色主题下比正文还亮，灰显反成了强调），多行警告用 `au::Dialog` + `aurora::confirm`。**实测两处框架现状**：`Modifier::context_menu` 与 `MenuItem` 只有状态模型、没有渲染与点击派发；`OverlayHost::handle_outside_click(Point)` 在 Aurora 全仓无人调用，即框架不代劳把外部点击派给浮层，须应用侧在指针入口自己驱动——本仓在 `on_pointer_event` 的 Press 分支先问宿主，命中即消费该事件（点菜单外不落到选区）。按裁决 7.13② 这属「用现有公共 API 组合得出的交互体验」，**留在本仓、不登记缺口**。浮层懒建一次并常驻，之后只 `open_at` / `close`。
- **多行先确认后发送（`feat(ui)`，7.41④）**：`confirm_multiline` 是异步接缝（`void(const PastePlan &, function<void(bool)>)`），计划扣在 `pending_paste_` 里等用户；放行闭包以「持有值已被取走」为幂等判据，故第二次 `true` 不再发第二遍。**无宿主 ⇒ 问不了 ⇒ 不粘贴**（`pending_paste_.reset()`）——「什么都不发生」在这里是正确结果而非吞事件，未经确认绝不替用户按下回车。
- **剪贴板读方向与帧边界纪律（`feat(session)` + `feat(ui)`，7.41⑤）**：`session::ClipboardOutbox` 新增 `read()`（`get_text` 失败只记诊断、返回空，与 `drain()` 的落地失败同口径），全仓触达系统剪贴板的翻译单元仍只有这一个，读写并列其中。视口侧 `on_pointer_event` / `on_menu_command` 只置 `copy_pending_` / `paste_pending_` 一个 bool，真正的 IO 在下一帧 `on_frame` 的 `flush_copy_request` / `flush_paste_request`（AGENTS.md §4.5 第 25 条，与 7.40⑥ 同一条纪律）。视口为此开 `Presentation{clipboard_read, clipboard_write, confirm_multiline}` 三接缝，**字段留空即回落生产实现**，故集成用例测的是接线本身而不是替身行为。
- **逐块节流的排期（`feat(ui)`，7.41⑥）**：经 `Scheduler::set_timeout`，排期时刻取 `chunks[i].delay` 的**累积值**（计划里的延迟是「相对上一块」，7.33⑤）。`Scheduler::current()` 是 thread_local、无运行中 App 时恒 `nullptr`，此时按块次序一次发完（块边界与内容不变），所以无头用例锁「分了几块、每块发什么」而非「隔了多久」，节流那一腿的真机判据随人工走查。
- **验收（`test(ui)`）**：`utest_right_click` **8 例**（三态 × 有无选区的分流、菜单条目与 `enabled`、配置别名同型）+ 新建 `itest_right_click_paste` **16 例**（真指针派发 + `Popup` 真实命中测试 + 帧边界的中间态 + 多行确认的扣与放），非 e2e 通道 **25 项全绿**（新增一条 CTest 项，`cmake/BorealisTests.cmake` 按 glob 自动挂载）。菜单条目的定点用 `context_menu()->content_bounds()` 的高度分数（1/4＝复制、3/4＝粘贴），为此给视口开一个只读 `context_menu()` 观测点——条目宽度由 `min_width` 定死而非文字撑开，用例拿不到内容盒就只能复刻一套按钮尺寸算式。
- **四条变异自证与一处判据空洞**：粘贴改到回调里读剪贴板 → 帧边界两例转红；多行判据短路 → 三例转红；去掉外部点击关菜单 → 关菜单那一例转红；**置灰改成恒可用 → 只有置灰那一例转红，且这是自查补出来的**——原断言只比剪贴板读写计数，而无选区时即便条目可用，`flush_copy_request` 也因子串为空而早退，于是「置灰＝不可点」这件事结构上测不到；补的判据是「点它既不复制也不粘贴、**连菜单都还开着**」（禁用按钮不消费点击，真码保持 open；MUT 下 `on_menu_command` 关菜单 → 转红）。另记一条工具教训：首轮变异注入写成行尾注释使 `right_click.cpp` 编译失败，而那次「16 例全绿」其实是**旧二进制**的读数——改用块内注释并重建后才拿到真实读数。
- **吞吐时间门禁**：复跑 7 档全 PASS，最紧的 B-5 `cat_frame_ms_mean` 三次中位 1.424 ms 对相对线 1.456 ms——场景根多了一层 `OverlayHost` 布局，未破线故**不改基线**。
- **提交形态**（五个分层本地提交）：`feat(ui)` 右键决策件与枚举单源迁移 / `feat(session)` 剪贴板读方向 / `feat(ui)` 视口右键、菜单与粘贴排期（含装配层浮层宿主）/ `test(ui)` 八例单元 + 十六例集成 / `docs` 裁决 7.41 与现状回写。
- **仍待人工**：右键菜单、多行警告与逐行节流的**真机走查未做**（本棒只有无头集成断言，不宣称「可用」）；三态的 **UI 入口仍缺**，因为 `SPEC.FEAT.PREF.02` 设置面板未开工（键早已在 schema 内，7.38⑥ 所述「无消费方」自此闭合）；缺省 10 ms 的块间隔是否丢字待真机校准（7.33⑤ 已在册）。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.41（六条 + 代价 + 验收）、§7 追加分组范围补到 `7.37–7.41`、`SPEC.FEAT.INTERACT.03` 末句改指 7.41①–⑥、版本脚注 v0.38 → v0.39；`ARCHITECTURE.md` §9.2 记场景根 `OverlayHost` 与浮层层序、§10.2 记剪贴板读写与粘贴排期都落在帧边界；`AGENTS.md` §2 目录表补 `ui/right_click.{h,cpp}` 与新集成用例、§6 现状回填；`PLAN.md` 本条落期同步。

---

## v0.38（2026-10-03）选区界面腿本体：指针状态机、高亮绘制与复制落地（`SPEC.FEAT.INTERACT.02` / `03`，裁决 7.40）

**动机**：v0.36 / v0.37 两根把口径与纯逻辑件备齐了，界面腿于是只剩「把件接起来」这一段——而这一段真正需要裁决的不是画图，是**接缝落在哪一层**：复制取文本要不要新开会话侧 API、行号换算量藏在谁身上、断点上的退化按哪一端分。这几处原稿没写或写反，落地时逐条实测纠正，故新增裁决 **7.40**（九条，其中 ① 是 7.38⑥ 的改判）。

- **复制接缝改判（7.40①）**：7.38⑥ 原写「`Session::copy_text` 在既有短临界区内取文本」，落地时改为视口在**同一次** `Session::read` 临界区内调 `ui::copy_text`，出锁后经 `session::ClipboardOutbox::write(utf8)` 落系统剪贴板。理由：会话层收下 `ui::Selection` / `ui::CopyOptions` 就是 `session` 反向认识 `ui`（架构 §2.3 的模块方向），而选区知识本就只属于主线程与绘制侧；临界区没有变长（同一次锁内多走几步纯逻辑），锁外做 IO 的纪律也没变。`ClipboardOutbox` 因此新增一个静态写入口，与 `drain()`（`OSC 52` 那条来源）共用同一个失败口径（`AURORA_LOG_WARN`，不留半截状态），全仓剪贴板写点仍只有一个。
- **指针状态机（`feat(ui)`）**：覆写 `on_pointer_event`，`Press` 记格并据 `click_count` 定粒度（单击逐格 / 双击按词 / 三击按行），`Move` 推进当前端点，`Release` 收口并按 `copy_on_select` 置一次性待复制标志。两条来自实测的形态值得记：① **Word / Line 粒度须先把两个区间折成阅读序的外沿**再存（7.40③），照端点格直存会让三击后向上拖只选中起始行的第一列；② **断点上的退化按两端分工**（7.40④，细化 7.39④）——按下端退化保住「双击断点不成选区」，拖拽端退化保住「从断点起步往外拖仍能扩」，不区分就得前者新增空选区状态、后者在断点上卡死。列模式取 `MouseEvent::modifiers` 的 Alt 位（7.40⑤），这是 **G18 回货字段在本仓的第一个消费点**，派发器只透传不推断，故不自造轮询物理按键态的兜底。
- **高亮绘制与行号换算（7.40②⑧）**：屏幕行 ↔ 存储行的唯一换算量是 `session::ScreenMirror::window_top()`（v0.37 那根已带出，本棒接上消费点）；逐行选中区间表**每帧重算**而非只在选区变化时算——列数随 resize 变，而流式首行的选中右界正是列数（7.38① D5①）。`ui::layout_row` 的选中区间入参在绘制侧生效：被选格换 `palette.selection` 底色（未配回落 `basic[8]`）、前景按 `min_contrast` 重合成，失焦态取与默认底色各半混合。块形光标压在选中格上时，三段式的第三段按**选区色**重画底（视觉稿 C2-a 的层序：光标层在选区层之上），否则那一格会在整片选区里露出一个未选中底色的空洞。
- **copy-on-select 落在帧边界（7.40⑥）**：`Release` 只置 `copy_pending_`，实际剪贴板写在下一帧 `on_frame` 落地——AGENTS.md §4.5 第 25 条「事件回调内不得做阻塞 IO」；判据不丢：`set_selection` 无论成不成都标脏，故抬起后必有一帧可排。
- **主备屏与复位的选区作废（7.40⑦）**：视口侧不加「是不是备屏」判定，直接复用 `Storage::clear()` 的顶边推进（兑现 7.39②）——状态机换屏必过 `clear()`，选区随之折算到顶端之外并塌成作废；另设模式判定只会与 7.39③ 的「带内滚动不补偿」互相打脸。
- **验收（`test(ui)`）**：`itest_render_viewport` 新增 **8 例**（该套件现 18 例全绿），是本仓第二批真像素断言也是选区腿的主证据：拖扫的高亮不越行（整帧差集只落在被扫那一行）、流式首行到行尾且末行截到焦点列、失焦态逐色等于各半混合、Alt 块拖的矩形文本、双击选词与「断点上不成选区」、三击整行与向上拖扩整行、高亮随新输出跟到上一行、`CopyOptions` 随 `InteractionOptions` 抵达复制腿。指针事件走驱动台私有的**实体 `au::EventDispatcher`**（静态入口共用进程内单例，上一个用例的点击时刻与落点会让本次 `click_count` 从 2 起算），且多击按 Press/Release 交替实发（`click_count` 由派发器在派发前覆写，手写该字段无效）；持焦态由 Press 的焦点归属自然产生而非手工 `set_focused(true)`。复制腿断 `selected_text()` 的文本与变换，不经系统剪贴板 IO。
- **两次变异自证 + 一条判据边界（7.40⑨）**：去掉 `window_top()` 换算则八例全转红；把外沿折半改成只作端点次序对调，则双击与三击两例转红而其余六例不受影响。首轮有 3 例失败暴露出一条框架事实：**把一行按选中边界重切 run 后，同一 run 内后续字形的亚像素覆盖会改变**（框架按 run 盒左缘起排），故「未选格逐位不变」只能对**无字形**的格断言，带字形的未选格改断「该格带色回到 default 底色」——外溢仍抓得到，但不把框架 shaping 的性质算成选区缺陷。另两处失败是本棒自己的错：CSI 光标定位漏 `;`（`[31H` 而非 `[3;1H`）、行粒度 `selected_text()` 按 D5① 含行尾填充而期望串未补。
- **门禁复跑记录一条噪声事实**：`cat_frame_ms_mean` 首跑三次中位 1.457 对相对线 1.456 转红，独立复跑同一二进制得 1.411 转绿（其余六档两次均 PASS）。该档读数正落在 ±10% 窗边界上，故**不据此改基线也不放松门禁**，写进 7.40 验收段以免下一棒把一次红灯误当成选区层的性能回归。非 e2e 通道 **23 项全绿**。
- **提交形态**（三个分层本地提交）：`feat(session)` 剪贴板落地件的写入口 / `feat(ui)` 界面腿本体（含装配层搬 `terminal.word_delimiters` 与 copy 三键入 `InteractionOptions`）/ `test(ui)` 八例像素与复制判据。
- **仍待人工**：真机走查的拖拽跟手、跨滚动条回看时的选区跟随、copy-on-select 是否真落系统剪贴板——本棒只有无头像素与文本断言，未宣称「可用」。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.40（九条 + 代价 + 验收，含 ① 对 7.38⑥ 的改判）、§7 追加分组范围补到 `7.37–7.40`、`SPEC.FEAT.INTERACT.02` / `03` 两条末句改指 7.40 的实际接缝、版本脚注 v0.37 → v0.38；`ARCHITECTURE.md` §9.2 的绘制层序补选区层与光标层的相对次序、§10.2 记 G18 字段的消费点；`UI_SELECTION.draft.md` §4 的落地状态与 §5 的判据按实测订正；`AGENTS.md` §2 / §6 与 `PLAN.md` 现状同步。

---

## v0.37（2026-10-03）选区界面腿的纯逻辑前置：顶边位移计数、漂移折算件与双击选词（`SPEC.FEAT.INTERACT.02`、`SPEC.FEAT.TERM.04`，裁决 7.39）

**动机**：裁决 7.38⑤ 把「scrollback 溢出时选区跟着内容走」判成界面腿落地前必须消除的欠项，而它需要的不是选区侧多写一条 if——存储要能回答「同一份内容的行号整体走了多远」，主线程那份副本要能在没有锁的情况下拿到那个读数（副本与权威网格只在 `apply` 的临界区内见面）。本棒先把这两件前置和双击选词一起做成纯逻辑件，界面腿的选区状态机于是只剩「事件 → 格子 → anchor/focus → `row_spans`」一条直线，不含任何需要现场推导的口径。

- **`grid::Storage::dropped_lines()`：带符号的顶边净位移**（`feat(grid)`，7.39①）。动顶边的四条路径逐一记账——`scroll_up` 溢出 +1、`set_rows` 的溢出 +dropped 与顶部补空白 −blanks、`scroll_down` 无历史可收回时 −1；`clear()` 按现存行数一次性推进（7.39②，使旧选区折算到顶端之外而非贴着新缓冲继续高亮）；`scroll_region_*` 的带内位移**不记**，其不可由单一全局偏移表达的理由与消除落点作为显式欠项入 7.39③。原 7.38⑤ 写的「单调只增」据此修订：只记挤出的话，后两条路径会让选区朝反方向错位，且窗口反复变高变矮时误差累积不归零。
- **`session::ScreenMirror` 携带读数**（`feat(session)`，7.39⑤）：`apply()` 在既有临界区顶部快照权威网格的 `dropped_lines()`，整窗重建与逐行并入两条路径共用同一入口故都带出；新增访问器是主线程唯一取数点，界面腿每帧折算零新增锁（架构 §3.4）。
- **`ui::translate_selection_rows`**（`feat(ui)`）：两端行号各减差值，单端越界钳到 0，**整段被推出顶端时塌成两端重合**——复用 7.32③ 既有的「单击即无选区」判据而不新增空选区状态，故 `ui::Selection` / `row_spans` 的 API 与既有 13 例零改动。7.38 代价条款预告的「贴顶空选区锚点 + 复制空文本」就此改为「作废」（复制出一串空白与指着别人的内容都不如什么都不选）。
- **`ui::word_span_at`（双击选词）**：界定符串是 UTF-8 原文并**按整码点比较**——`é` 的续字节与 `©` 的编码相撞，逐字节判定会选错词，故复用 `term::Utf8Decoder`（严格 UTF-8，半截序列按替换字符收尾）而非自造解码。空格与制表恒断点；双宽延续格按其**基础格**判定，整字符要么全入选区要么全不选（7.32② 同口径）。缺省界定符集不抄进测试，直取 `config::TerminalSettings{}` 的真实键值。
- **一处口径冲突按规则 11 回写，未择一静默实现**：已提交的 7.38③ 末句「界内为空（点的是界定符本身）时只选那一个界定符格——与 xterm 一致」、`include/borealis/config/settings.h` 注释「界内为空即每个界定符各自成词」、本实现的「落点在断点上即不成选区」是三种互不一致的表述。裁决 **7.39④** 取后者并给理由（xterm 那条是把落点处的非字组扩张成一整段，而本仓未写入的列全是空格填充，行宽可达数百列，照搬就会选中整行填充并经 `copy_text` 复制出一串空白；「扩到相邻字」由拖拽承担而非双击），同时订正 `settings.h` 的表述为「空集＝只有空格与制表断词」（`a=b` 双击选中整段），并把 7.38⑤ 的「单调只增」与其代价条款各以修订形态入 7.39①②；旧编号旧条款一律原文保留。
- **验收**：`utest_grid_storage` **36 例**（+5：四条路径各自记账、变高补空白的负项、`clear()` 推进全部现存行、region scroll 不动顶边）、`utest_screen_mirror` **10 例**（+1：两条并入路径都带出读数并与存储对齐）、`utest_selection` **24 例**（+11：选词 7 条含整码点比较与双宽两半同解、折算 3 条含塌成单击、加一条真实溢出的端到端证人——`Storage{4,2,2}` 饱和滚动后不折算复制到 `"   "`、按 `dropped_lines()` 折算后复制回当初选中的 `"kee"`）。非 e2e 通道 **23 项全绿**（用例并入既有三套件，CTest 项数不变）。
- **变异自证三条**：① 删 `breaks_word` 的「延续格重定向到基础格」→ `word_span_at_does_not_lean_on_the_continuation_of_a_wide_break` 与 `word_span_at_selects_a_whole_wide_glyph_from_either_half` 双红；② 删 `translate_selection_rows` 的塌合分支 → `translate_collapses_a_selection_that_drifted_entirely_off` 独红；③ 删 `clear()` 的顶边推进 → `clear_counts_every_stored_line_as_gone` 独红。① 还顺带抓到一处**空转正的测试 helper**：既有的 `put_wide` 把延续格码点留成空格，而生产形态（`src/term/terminal.cpp`）写的是 `code_point = 0`，于是「按基础格判定」这条分支删掉也测不到——新增按生产形态造格的 helper 后该分支才有证人。三次注入改回即全绿。
- 文档回写：`SPECIFICATIONS.md` §7 增裁决 7.39（五条口径 + 代价）、`SPEC.FEAT.INTERACT.02` 的指针补齐、版本脚注 v0.36 → v0.37；`AGENTS.md` §6 的存储、会话副本与选区三条回填；`UI_SELECTION.draft.md` §4 的「scrollback 溢出时选区漂移」一行标记为已由本件消除。**界面腿本体未开工**：选区状态机接线、高亮绘制（`layout_row` 的选中区间入参）、`Session::copy_text` 与右键三态、`HeadlessSurface` 的两帧像素差分随下一棒落地。

---

## v0.36（2026-10-03）选区界面腿的口径裁决与选区色/界定符入配置（`SPEC.FEAT.INTERACT.02`、`SPEC.FEAT.INTERACT.03`、`SPEC.FEAT.PREF.01`，裁决 7.38）

**动机**：`SPEC.FEAT.INTERACT.02` 的界面腿是当时唯一没有框架依赖的下一棒，而它的七个视觉决策点（`codespec/UI_SELECTION.draft.md` §3 的 D1~D7）与四条实现口径（色值来源、选词界定符、滚动漂移补偿、copy-on-select 时机）都还没有定论。人离开数小时并授权「口径不确定时取最优解并回写裁决」，故本轮先自拍再落码，而不是把棒停在等人拍板上。

- **新增裁决 7.38**（六条：D1~D7 全取建议项 / 八套选区色的来源与回落档 / 界定符可配及其缺省集 / 三击选行 / 漂移补偿的形态 / copy-on-select 的时机与写入接缝，含各自的代价）。其中 **D1① 底色替换 + 前景按 `min_contrast` 重合成**、**D5① 选中右界截到网格列数**（与 `row_spans` 的闭开区间同口径，绘制与复制因此共用同一张区间表）、**D4① 选区盖过尚未开工的搜索与 hover 层**（现在定序只为把层序写进架构 §9.2，免得两棒各画一层后打架）。
- **7.38② 的色值不臆造**：八套一律取各主题官方色板里的既有档位（dracula `#44475A`、nord `#434C5E`、solarized-dark `#073642`、one-dark `#3E4451`、gruvbox-dark `#504945`、monokai `#49483E`、campbell `#767676`、tokyo-night `#414868`），并在表行注明每一档的出处；后三组是**移植惯例而非规范文本**，如与官方 normative 值不符须开新裁决修订而不改本行编号。未配时的回落定为同色板 `basic[8]`（bright black）而非新算式；失焦态混合按 `(a + b) / 2` 向下取整定死，使聚焦/失焦两帧的差异恰为一个表达式。
- **7.38③ 的界定符集合按不变量而非抄表验收**：缺省是 ASCII 可见标点全集（32 个），与 xterm `charClass` 的默认非字组**同旨而不逐码等同**——xterm 把 `;` 归入字组，本仓取为断点（shell 以 `;` 分隔参数，双击选中单个参数更符合直觉）；`_` 随 xterm 取为断点，故双击 `foo_bar` 选中 `foo`，要整段须从本键删掉 `_`。该口径随落码一并写进 `utest_config`（长度 32 + 不得含字母数字与空格 + 路径分隔位必须在集内），不逐字符抄默认值当断言。
- **代码腿（`feat(config)`、`feat(ui)`）**：`ui::PaletteSpec` 增可缺省 `selection_color`（与 `cursor_color` 同形态：黑色选中底是合法配置，「没配」必须可区分），取用经 `ui::selection_color()` 回落 `basic[8]`，`ui::mix_half()` 是失焦混合的唯一定义处；`config::themes` 的原始表增 selection 档；`config::TerminalSettings` 增 `word_delimiters` 键，store 侧写 `palette.selection`（null＝未配，与「配了黑色」可区分）并读回随**文件内点名主题**回落。三者都不参与逐格合成——`utest_palette` 有一条反向守卫锁住「选区色不得进 `resolve`」，理由是「一格属不属于选区」是区间级事实而非该格的内容（裁决 7.32②）。
- **验收**：`utest_palette` +3 例（回落档、可配黑色、不参与合成、各半混合的向下取整与 alpha），`utest_themes` 在既有一例内增「八套的选区色必须落定且与底色可辨」，`utest_config` +1 例并扩展三处（往返等值含两个新键、缺键留痕含 `appearance.palette.selection`、显式 `null` 时取用侧回落到点名主题的 `basic[8]`）。非 e2e 通道 **23 项全绿**。**变异自证**：删去 `selection` 与 `word_delimiters` 两条写侧键，三条用例（往返等值、光标色形态例、降级后仍可写盘例）同时转红。
- **本条不含界面腿本体**：选区状态机、高亮绘制、`Session::copy_text` 与右键三态、`HeadlessSurface` 的像素差分验收随下一棒落地；`AGENTS.md` §6 的**配置面**已随本条回填（两个新键与测试例数），架构 §9.2 的层序与 `PLAN.md` 的进度回填留在界面腿那一棒一并兑现。

---

## v0.35（2026-10-03）下一棒排期前的框架面复核：G22 入册与后端不对称的机制修正（`SPEC.FEAT.RENDER.02`、`SPEC.FEAT.INTERACT.01`，裁决 7.37）

**动机**：给「字体族 / 设置面板 / 选区界面腿」排期之前，须逐条读 Aurora 公共头确认框架面——按记忆断言现状会把已经能做的事判成等框架，也会把「不可分」误写成「无原始码」而把任务书派错方向。

- **新登记 G22**（附录 A.2、`PLAN.md` §6）：框架没有「列出可用字体族」的公共入口（`register_*` / `resolve_faces` 全部要先知道 family 名，系统字体目录扫描藏在实现内），故 `SPEC.FEAT.RENDER.02` 的「系统等宽字体枚举」腿与 `SPEC.FEAT.PREF.02` 的字体族下拉无数据源。首版按需求已定的那一半交付（内置 Cascadia Code 为默认 + 可配回退链），枚举腿待回货接并以 `TODO(SPEC.FEAT.RENDER.02)` 留痕；**不复制框架的字体扫描、不手写族名表**（`AGENTS.md` §5 第 2 条）。
- **机制修正**（7.36②(f) 与附录 A.1 / A.2 的叙述，旧编号旧条款保留）：原文「Win32 与 GLFW 无对应原始码」不准确——GLFW 确无 `KP_*` 导航常量（两区在库内已合并成同一组 `GLFW_KEY_*`）；Win32 是两区**共用同一组 `VK`**、来处只在 `lParam` 扫描码里，其映射表自陈六键中只有 `Home` 真可分（主键盘 `0x47` / 小键盘 `E0 4E`），其余恒给主键码；`VK_INSERT` 只在注释里出现、没有 `case` 分支，X11 / Wayland 的 keysym 表未收录主键盘 Insert 的 `0xFF63`。**结论与影响面不变**，但 G20 的任务书必须按「共用码位不可分」写，否则对方会在 `wParam` 上找区分依据而白跑一趟。
- **已核实不构成缺口的框架面**写进裁决 7.37③：字形缓存按 DPI 失效重建（`GlyphAtlas::clear()` + `FontEngine::shape_cache_clear()`）、preedit 就地绘制与候选窗定位（`display_caret_x` / `hit_test_char_inclusive` 带 scale、`set_composition_caret_provider` 喂 `ImmSetCandidateWindow`）、表单控件（`checkbox` / `switch` / `slider` / `dropdown` / `form` / `pickers` / `dialog` / `toast` / `popup`）；`WS.01` 的拖拽重排与逐标签图标/角标、`WS.02` 的嵌套分屏与方向键焦点路由属可组合的交互体验，按 §5 第 1 条留本仓。
- 文档回写：`SPECIFICATIONS.md` 附录 A.2 增 G22 行、§7 增裁决 7.37 并补齐追加分组；`PLAN.md` §6 增 G22 行；`AGENTS.md` §6 现状同步；版本脚注 v0.34 → v0.35。**本条只动文档，未改代码**（非 e2e 通道仍 23 项全绿，未重跑构建）。

---

## v0.34（2026-10-02）键盘与 DPI 回货接线、吞吐复验与两条新登记缺口（`SPEC.FEAT.INTERACT.01`、`SPEC.NF.PERF.02`，裁决 7.36）

**动机**：裁决 7.30⑤ / 7.31③ / 7.33⑧ / 7.35⑤ 先后派发给 Aurora 侧的六条缺口（G14–G19）在同一天全部回货。当时立的分工是「编码层先把字节形态写对，回货接的是派发侧」，本棒即那次接线，外加两套复验（真机尺寸腿、优化档吞吐），并把复验过程中新撞到的两条如实登记。

- **键盘三腿接线**（`feat(term)`）：`Tab` / `Shift+Tab` 经新到货的 `Widget::wants_tab_keys()` 落到视口（该钩子计入 `has_input_semantics()`，覆写后容器不会因「自身不接收输入」被移出键盘焦点序）；`WM_SYSKEY*` 与 `WM_KEY*` 合流后 Alt 系组合键进入事件链，本仓消费侧零改动（编码层早已按 Alt 与 Meta 同口径写对字节）；`KeyCode::KP_*`（`KP_Insert = 100` 连号至 `KP_9`）与 `ModifierKey::NumLock` 建模后，`DECKPAM`/`DECKPNM` 的模式位终于有可发之键。派发侧的 `TODO(SPEC.FEAT.INTERACT.01)` 桩就此消除。
- **keypad 两档编码口径**（`DECKPAM`/`DECKPNM` × NumLock）定死：需求只要求「keypad 应用模式须生效」而未给数值表，故表源取 **PuTTY `format_numeric_keypad_key` 的 xterm-funky 档**。导航区六键与 NumLock 无关、数字阵只在 NumLock 关闭时取导航形态、应用模式下其余数值与运算键发 SS3 字母族（`0-9` → `p`–`y`、`.` → `n`、`/` → `o`、`*` → `j`、`-` → `m`，`+` 因 VT100 上占**两个**物理位而由 Shift 二选一 `l`/`k`），SS3 字母形态没有修饰参数位故除 Shift 外的修饰一律不发。Win32 上有一条专有风险：NumLock 开着的小键盘键必然紧随一个 `WM_CHAR`（Windows 不看 `DECKPAM`），视口以一次性吞字防止同一物理键上屏两次。
- **验收扩到 `utest_keymap` 11 例 / `itest_key_input` 8 例**（`test(term)`）：keypad 两档逐字节 + `DECCKM`/`DECKPAM` 经**真实状态机**生效。**真机腿仍待人工**——锁屏会话下 `SendInput` 静默失效（裁决 7.31① 的可用面判据），本轮不宣称键入「可用」，也不为 `Tab` / keypad 新增 e2e 去断言一个当前验不了的东西。
- **G17 真机尺寸腿复验达成**：以 WGC 截屏 + 标题栏按钮 UIA `Invoke` 走启动 / 最大化 / 还原三态，终端底色均铺满客户区（旧「网格只覆盖约 `1/1.5`」症状消失），实测格宽 15.3 物理 px = 10.2 dp @150%，即格度量与窗口 bounds 同一 scale；Aurora 侧原定验收（解除 `etest_smoke_render.cpp` / `etest_multi_window.cpp` 两处 known-scale-gap SKIP）已改为真断言。**尺寸腿就此闭合，键入腿未闭**。
- **G19 吞吐复验与基线整体重捕获**（`perf(tools)`）：框架侧取「由三表首项导出的单宽下界」一次比较替代三次二分（判定结果逐码点不变），本仓消费侧零改动，按裁决 7.35③ 在 `msvc-bench` 优化档整体重捕获——写侧纯链观测 25.9 → **约 41 MB/s**，`cat_mb_per_s` 的 B-7 参考值 20.627 → **28.888**，七档门禁重跑全绿。
- **门禁相对线的判据细化**（裁决 7.36③，细化 7.35④ 的表述）：按**逆向侧偏离量**判、不按峰谷跨度判——B-7 三次调用 27.334 / 28.888 / 31.420 的峰谷是 14.2% 而逆向侧只有 5.4%，故 10% 相对线保留；若按跨度判就会把唯一能看见写侧退化的档位请出门禁（帧时类门禁对「生产端变慢」天然盲）。
- **复验新撞两条登记并派发**：**G20**（`KeyCode` 只有 `KP_Insert`、未建模**主键盘** `Insert`，Win32 后端亦无 `VK_INSERT` 映射，故遗留档的 `CSI 2~` 在 Win32 永无从产出——本仓波浪号族 `2` 号位已实现并被 `KP_Insert` 与 NumLock 关的 `KP_0` 走过，回货接一行键码折算）；**G21**（建窗时刻 `refresh_scale()` 在 `hwnd` 尚空时调 `GetDpiForWindow(nullptr)` 得 0，而「回落 `GetDpiForSystem`」挂在函数指针为空的分支上故结构不可达 → `scale = 1.0` → `WindowOptions::size` 的逻辑 dp 被按物理像素用掉，实测 960 dp 请求得 655 dp 窗口）。**G21 不并回 G17**：两者成因不同（三方记账发散 vs 建窗期无句柄可依），且 G17 的原定验收已按其口径达成——复用已闭合编号会让「闭合」失去判据。
- **G18 的字段到货而消费点未落地**：`MouseEvent` / `ScrollEvent` 的 `modifiers` 已在公共 API 上，本仓尚无读取处——它解锁的两腿（Alt+拖拽列模式、Ctrl+滚轮字号）随 `SPEC.FEAT.INTERACT.02` 界面腿与 `SPEC.FEAT.RENDER.02` 开工，回货只解除框架依赖、不改分期（裁决 7.29⑤ 同口径）。
- **新增裁决 7.36**（五条：接线面只在消费侧 / keypad 两档编码口径 / 相对线按逆向侧判 / G17 复验与键入腿待人工 / G20+G21 的登记与不合并理由）。文档回写：`SPECIFICATIONS.md` 的 `SPEC.FEAT.INTERACT.01` 框架现状段按「回货复验」重写、附录 A.1 增五行（`Tab` 钩子、Alt 系进链、`KP_*` 与 NumLock、指针修饰态、DPI 单一真值源）、A.2 的 G14–G19 六行标闭合并原文保留其登记时事实、新增 G20 / G21 两行、A.3 的派发叙述同步；`ARCHITECTURE.md` §3.5 / §9.6 / §10.1 / §13 / §15.3、`PLAN.md` §6 / §8、`AGENTS.md` §2 与 §6 一并回填；版本脚注 v0.33 → v0.34。非 e2e 通道 **23 项全绿**（用例并入既有套件，CTest 项数不变）。

---

## v0.33（2026-10-02）吞吐基准的构建档契约与写侧瓶颈归因（`SPEC.NF.PERF.02`，裁决 7.35）

**动机**：v0.32 在册的「灌入吞吐约 0.7 MB/s 的写侧缺陷」是本棒要了结的对象——先做归因量具，再按证据决定修瓶颈还是改结论。实测结论是后者：**那个数字量的是构建档，不是产品**；裁决 7.34⑦ 的「写侧缺陷限制」前提不成立，据实改判而不是把缺陷锁进基线。

- **写侧归因阶梯**（`borealis_bench --write-side`）：同一份素材依次过三档——纯链（解码 → 解析 → 状态机 → 网格，单线程无锁）、过会话（+ 互斥、脏行扫描、背压队列，主线程只 drain 不排帧）、`cat` 端到端（+ 并发排帧），三数直接相减即归因。实测优化档 **25.9 / 24.3 / 20.6 MB/s**（会话侧约 6%、帧并发约 15%），Debug 档 1.1 / 1.3 / 0.7 MB/s，**放大倍数约 20–30 倍**。
- **构建档入测量契约**：`build_config`（`debug` / `optimized`，按 `_DEBUG` 派生）写进 JSON 顶层与窗口行，基线记 `capture.build_config`，门禁对「样本缺标记」与「档名与基线不一致」一律硬 FAIL；新增 `msvc-bench` 预设（`RelWithDebInfo` → `build-bench/`）作为门禁默认读取的构建目录——「靠人记住该用哪个构建目录」不构成契约。
- **基线整体重捕获**（旧 Debug 捕获以 `capture.supersedes` 留痕，不原地改写）：优化档三次中位 `cat` 736.828 fps、滚动帧 1.263219 ms / p95 1.4689 ms、灌注帧 1.357168 ms / p95 1.6155 ms、全屏重绘 1.262 ms、灌入 20.627 MB/s。
- **相对线随新抖动重算**：`cat_mb_per_s`（逐次跨度 10.3%，但门禁判的三次中位在四次调用里只漂 2.0%）**进门禁，新增 B-7**——需求未给它命名数值，但生产端变慢只会让帧数变少，帧时类门禁看不见；`cat_frame_ms_p95`（18.7%）从相对线改为只走绝对线 **22.222 ms**（即 `1000/45`，需求自己的帧预算）；`full_redraw_ms`（8.8%）仍只走绝对线（10% 相对线只剩 1.2% 余量，判的是机器）；两档归因阶梯与派生量保持 ungated。
- **瓶颈定位在框架宽度判定，探针已撤销**：把 `aurora::unicode_cell_width` 临时换成「`cp < 0x00A1` 即单宽」的可证明等价短路（三表首项 `0x00AD` / `0x1100` / `0x00A1`）后纯链 25.9 → **44.7 MB/s**，即宽度判定占纯链约四成耗时；按裁决 7.13① 与 `AGENTS.md` §5 第 2 条（不长期持有框架分叉）撤销探针、不在应用侧复制框架的宽度表，补全登记为附录 A.2 的 **G19** 并派发 Aurora 侧。
- **四项观测项在册不动**：逐行滚动每帧走整屏重建（`Terminal::scroll_text` 整屏分支置 `full_screen_dirty_`，证人＝`scroll_frame_ms_mean` 1.263 ms ≈ `full_redraw_ms` 1.262 ms）、`grid::Row::reset()` 不用 `occupancy_` 上界、零宽与超链接侧表的空表守卫、每条 CSI 解析一个 `std::vector` 子参数。四者都缺「改了就转红」的判据，动它们须各以其基准数字结项。
- **非空转自证**（AGENTS.md §4.4 第 22 条）：真实 Debug 样本、删去 `build_config`、把标记改成不存在的档名、`cat_mb_per_s` 压到 18.0（B-7 线以下）四条变异各自转红；改回即 7/7 PASS。非 e2e 通道 23 项仍全绿。
- **新增裁决 7.35**（六条口径，改判 7.34⑤⑦）；`SPEC.NF.PERF.02` 条目末点出该裁决；附录 A.2 增 G19 行、A.3 的派发叙述同步。文档回写：`ARCHITECTURE.md` §13 第 4/5 条、`PLAN.md` 的观察池与吞吐门禁行、`AGENTS.md` §2 的 `tools/` 行与 §6 现状；版本脚注 v0.32 → v0.33。

---

## v0.32（2026-10-02）渲染吞吐基准与时间门禁落地（`SPEC.NF.PERF.02`，裁决 7.34）

**动机**：`SPEC.NF.PERF.02` 的验收手段一直缺位——上屏层主路径已在册（裁决 7.29 定稿、`itest_render_viewport.cpp` 十例像素守住正确性），但「吞吐」与「回归门禁」两条只有判据没有量具，`AGENTS.md` §4.4 第 21 条因此无法兑现（不许以「应该更快了」结项）。本棒补的是量具与红线，不改渲染路径本身。

- **新增 `tools/bench/render_throughput.cpp`（独立可执行 `borealis_bench`）**：三场景——逐行滚动（每帧一行脏）、强制整屏重绘（`ESC[2J ESC[H` + 写满整屏）、`cat` 10 MB 灌注（后台线程 64 KB 块喂替身连接，主线程排到队列见底）。行内容固定种子、四类混合（256 色 / 粗体+下划线 / CJK 双宽 / 纯文本），默认配置与装配层同源（960×640 dp、dracula、Cascadia 14pt，实测网格 24×73）。
- **测量污染的一条教训**写进裁决：主线程在无脏时仍轮询排帧，会与灌字节的读线程抢会话锁，实测把灌注 p95 从 10.7 ms 抬到 116 ms——那不是被测成本，是探针自身的负载；改法是无脏即让出、fps 取 `1000/mean`（无 vsync 环境）。
- **构建挂载**：`tools/CMakeLists.txt` + 顶层 `BOREALIS_BUILD_BENCH`（缺省 ON）。基准**不挂 CTest**——注册式测试框架禁自定义 `main()`，且时间类读数随机器负载浮动，进测试运行器即制造随机红灯；`borealis_test_runner` 的 23 项通道未变动。
- **新增 `tools/check/perf_baseline.json` + `check_perf_gates.ps1`**：绝对线照需求原文（灌注 ≥ 45 fps、全屏重绘 ≤ 100 ms），相对线 10% 只对抖动窄于该窗口的指标开；聚合取三次独立进程的中位，基线 `reference` 按同规则记录。**`cat_mb_per_s` 刻意不进门禁**——灌入吞吐现由写侧缺陷限制（每块输入重扫全视口脏行），锁进基线等于把缺陷当契约，那是另一棒的性能调查项。
- **非空转自证**（AGENTS.md §4.4 第 22 条）：三条变异注入各自转红——灌注 fps 压到相对线以下、全屏重绘抬过绝对线、基线指标名改成不存在的键（抓指标名漂移）。实跑一次全绿：fps 94.7 / 滚动 6.99 ms / 灌注 10.56 ms / 全屏重绘 12.65 ms。
- **CI 腿未落**：本仓当前无 CI 工作流基建，脚本以退出码与 ASCII 表输出对 CI 就绪，需求原文的「进 CI」随该基建落地一并兑现，本条不声称已闭环。
- **新增裁决 7.34**（八条口径）；`SPEC.NF.PERF.02` 条目末点出该裁决；§7 标题的追加分组补齐 7.30–7.34（此前只列到 7.29，属登记漏项）。文档回写：`PLAN.md` 的吞吐门禁项、`ARCHITECTURE.md` 的性能观测落点、`AGENTS.md` §2 的 `tools/` 行与 §6 现状；版本脚注 v0.31 → v0.32。

---

## v0.31（2026-10-02）选区高亮的 UI 目标态视觉稿出稿（D1~D7 待拍板）

**动机**：界面腿的三件（选区状态机、高亮绘制、剪贴板与右键三态）里，只有高亮绘制带视觉决策——底色是替换还是叠加、色值存在哪、失焦怎么降级、选中与反色/光标块谁盖谁。这些不能从换算件与 `row_spans` 的口径顺带推定，且按既有规矩 UI 动码前先出稿评审（`AGENTS.md` §4.1 第 2 条的开工纪律），故本棒产出的是**稿**而不是绘制代码。

- **新增 `codespec/UI_SELECTION.draft.md` + `.svg` + `.png`**：六组面板 A1/A2（流式选区聚焦与失焦）、B1（双宽延续格、combining、回看边界）、C1（选中叠加在 SGR 反色 / 亮色粗体 / 下划线 / 删除线 / 暗淡之上）、C2（光标块与被选格同格）、D1~D3（三套主题的 selection 槽）、E1（列模式块），F 组是三条文字判据（选中即复制无额外视觉、右键菜单不新增项、多行粘贴警告属对话框稿不属本稿）。逐格矢量法与 `RENDER_UI.draft.md` §0 同源，格参数沿用其 §1（11×22 dp、基线 18、装饰线 1 物理像素）。
- **七条待拍板 D1~D7 各带建议项与代价**，其中三条有硬约束：① D1 的「色带之上、字形之下」是**底色替换**，因为 alpha 叠加路径依赖 `Painter::blend_rect` 的舍入口径，而该口径本仓**未实测**（选它就得先补实测，否则像素断言只能降级为弱断言）；② D2 的 selection 色走**主题表新增槽**而非 `ui::resolve` 里硬编码，理由是设置面板已有「选区色」一项（`SPEC.FEAT.PREF.01`），无槽则面板无处落；③ D3 失焦态用各半混合而不是新增第二个色值槽。
- **本稿刻意不含的部分**：只出三套主题的色值（Dracula / Nord / One-Dark，取各主题公开色板既有的「当前行 / 高亮」槽），余下五套待拍板时补齐，**稿内不臆造未给出的色值**；列模式（面板 E）只定形态不定触发（Alt 态判定被 G18 挡住，裁决 7.33⑧）；scrollback 饱和后的选区漂移属存储侧单调行号（裁决 7.32②），不在视觉范围。
- **不新增裁决**：D1~D7 未拍板即无结论可记，拍板后再并一条裁决（先例：`RENDER_UI.draft.md` 的 U1~U5 收口成裁决 7.25）。本条只登记「稿已出、门禁位置从『无稿』移到『待拍板』」。
- **文档回写**：`UI_OVERVIEW.draft.md` 的三文档分工表新增本稿一行、§5 未画范围把「文本选择与列模式」改为「稿已出待拍板」；`ARCHITECTURE.md` §10.2 的「余下未落」高亮绘制项点出稿路径；`PLAN.md` §8 的 M1 未开工项与「鼠标 → 选区 → 复制文本」接缝行同步；`AGENTS.md` §6 未落清单同步；版本脚注 v0.30 → v0.31。
- **代价与边界**：出图用的生成脚本是一次性工具，不入库（与 `UI_OVERVIEW.draft.md` §6 第 5 条同口径），**入库的 SVG 源即事实来源**，后续修改直接改 SVG；本棒零代码改动，构建与测试面未变动（非 e2e 通道仍 23 项全绿）；需求条目数量（66 条）、标识体系、优先级与分期结构均未变动。

---

## v0.30（2026-10-02）指针落点 → 格子序号的换算件落地（`SPEC.FEAT.INTERACT.02` 界面腿第一件）

**动机**：选区界面腿里唯一不含视觉决策的一环是坐标换算——dp 与 px 的边界是本仓最容易算错的算术（v0.20 已认下这条），故它与 `make_geometry`、`rect_for` 同层落成纯逻辑件并进单测；余下三腿（选区状态机、高亮绘制、剪贴板与右键三态）都带视觉或对话框形态，须以视觉稿为前置，不能靠换算件的口径顺带推定。

- **`ui::cell_at_point(geometry, x, y) -> std::optional<GridCellPos>`**（`include/borealis/ui/cell_layout.h` + `src/ui/cell_layout.cpp`）：收框架指针事件的逻辑 dp（`MouseEvent::local_position` 同口径），出 `ui::selection.h` 既有的 `GridCellPos`，于是界面腿只剩「事件 → 本函数 → 推进 anchor/focus → `row_spans`」。三点口径：① 与 `rect_for` **同一坐标空间、同为绘制行号**，回看偏移仍由调用方在取行时折算，几何层不含历史行；② **越界钳位而非丢事件**——框架在 Press 时 `SetCapture`，拖出窗口后 Move 仍持续到达，返回空值会让选区在窗口边缘内缩一格，内边距带同理归最近的一格；③ 只有行列数为 0（字体未就绪、窗口最小化）才回空值，钳位发生在转成无符号**之前**（负浮点转 `std::size_t` 是未定义行为，不是「回绕成很大列号」那种看似能跑的巧合）。
- **不新增裁决**：换算的坐标空间与行号口径沿用裁决 7.32（选区存存储行序）与架构 §9.2（dp/px 单源换算），本条只把「鼠标 → 格子」从欠项划掉；`GridCellPos` 由 `ui/selection.h` 提供、`cell_layout.h` 复用，而非新造同形结构，避免一层字段搬运（两头的模块依赖仍单向，`selection.h` 不含框架与 `config` 类型）。
- **测试**：`tests/unit/utest_cell_layout.cpp` 由 24 例增至 **28 例**（格边界归右下、内边距带归原点、负 dp 与远端越界钳到边缘格、空网格回空值）。非 e2e 通道 **23 项全绿**（新增用例并入既有套件，CTest 项数不变）。变异自证两次：去掉内边距减法 → 1 例转红；去掉钳位 → 2 例转红且失败值显示 `18446744073709551615`，坐实 ③ 的未定义行为判据。
- **文档回写**：`ARCHITECTURE.md` §10.2 新增「已落地的落点换算」段，并把「余下未落」清单按职责重列（状态机 / 高亮绘制 / 剪贴板与右键 / `Session::copy_text` / 粘贴排期与警告）；`PLAN.md` §8 的「鼠标 → 选区 → 复制文本」接缝行与未开工清单去掉「鼠标换算」欠项；`AGENTS.md` §2 目录表与 §6 现状快照同步。
- **代价与边界**：① 本件不含修饰态判定，Alt+拖拽列模式的触发位仍被 G18 挡住（裁决 7.33⑧），形状参数由调用方给定；② 只处理控件本地 dp，窗口 → 控件的坐标归属由框架的 `local_position` 负责，本件不再二次平移；③ scrollback 饱和后选区相对内容下移一行的欠项与本件无关（属存储侧单调行号，裁决 7.32②）。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是 `SPEC.FEAT.INTERACT.02` 界面腿的首件落地。

---

## v0.29（2026-10-02）粘贴处置计划的纯逻辑件与 G18 登记（裁决 7.33）

**动机**：`SPEC.FEAT.INTERACT.03` 的粘贴腿排在对话框与主线程定时器之上，但「发什么、分几块、块间等多久、要不要警告」四条全是可脱界面定死的纯折算。先把这层落成公共件，界面侧接的只是排期与呈现，回调里就不会长出口径。同时为下一棒（选区高亮 / 右键三态 / 粘贴警告）实测一遍框架现状，把实测到的唯一缺口入册。

- **新增裁决 7.33（粘贴处置计划的口径与换行策略的单一真值源）**：① 形态＝纯逻辑件 `term::plan_paste` → `PastePlan{line_breaks, multiline, bracketed, chunks}`，本件不持时间也不碰 IO，排期归主线程（框架 `Scheduler::set_timeout`）；② **bracketed paste 优先于换行策略**——`?2004` 为真时只加一对 `ESC[200~ … ESC[201~`，`Filter` / `Convert` 与节流一并让位（该模式的语义就是整段交给 shell 判定），空文本连包裹也不发；③ 断点只认 CR / LF / CRLF 且 CRLF 算一个换行，U+0085 / U+2028 / U+001C 不是断点（当断点就会把一条命令拆成两条执行）；④ **多行警告判据取待发结果**而非剪贴板原文；⑤ 块切分与节流细则（首块 `delay` 恒 0、其后每块前等一个间隔，缺省间隔 10 ms 为内置常量、暂不开配置键）；⑥ 单一真值源迁移；⑦ 验收与变异自证；⑧ 界面腿的框架现状实测清单。
- **单一真值源迁移**：`PasteNewlinePolicy` 的定义从 `config/settings.h` 移入 `term/paste.h`，配置侧改为 `using term::PasteNewlinePolicy`（与既有的 `term::CursorShape`、`term::AmbiguousWidth` 同口径）；落盘的 `as_is / filter / convert` 名称→枚举值映射逐字节不变，`utest_config` 的往返等值仍绿。新增 `term::LineEnding{Lf, Cr, Crlf}` 作为串口 `line_ending`（`SPEC.FEAT.CONN.05`，默认 LF）的代码侧形态，串口腿落地时由配置字符串映射过来。
- **代码落地**：`include/borealis/term/paste.h` + `src/term/paste.cpp`（`PasteNewlinePolicy` / `LineEnding` / `PasteOptions` / `PasteChunk` / `PastePlan` / `plan_paste`），经 `src/CMakeLists.txt` 编入 `borealis_core`。
- **测试**：`tests/unit/utest_paste.cpp` 11 例（三策略的块切分与行尾形态、bracketed 原样与不节流、警告判据取自待发结果、非断点字符、空文本、块间隔从第二块起、配置别名同型），非 e2e 通道 **23 项全绿**。变异自证：把 CRLF 的断点长度改为 1（即 CRLF 算两个换行）后三例转红，改回即全绿。
- **新增缺口 G18（附录 A.2）：指针与滚轮事件不携带修饰键位**。实测事实：`KeyEvent` 有 `modifiers` 而 `MouseEvent` / `ScrollEvent` 没有，全库无「查询当前修饰态」的公共 API，Win32 的 `handle_mouse(HWND, UINT, LPARAM)` 连 `WPARAM` 都不接，`MK_SHIFT` / `MK_CONTROL` 等键位标记在源头丢弃；且鼠标消息按 Win32 约定本就不带 Alt，须现取 `GetKeyState(VK_MENU)`。影响面：`SPEC.FEAT.INTERACT.02` 的列模式触发位、`SPEC.FEAT.RENDER.02` 的 Ctrl+滚轮缩放、`SPEC.FEAT.INTERACT.05` 的 Ctrl+点击。属事件链路（裁决 7.13①），已按既有形态派发 Aurora 侧补全；本仓在回货前不自造替代判定，列模式与 Ctrl+滚轮两腿暂不落地。
- **同批实测为非缺口的框架原语**（记入裁决 7.33⑧，避免下一棒重复排查）：指针捕获（Win32 在 Press 时 `SetCapture`，拖出窗口仍收 Move/Release）、`click_count`、`CursorShape::IBeam` 与 `Widget::cursor_shape()` 虚钩子、`Modifier::context_menu` + `MenuItem`（含 `enabled` / `checkable` / `shortcut_text`）、`Dialog` / `alert` / `confirm`（模态焦点作用域）、`Clipboard::get_text`、`Painter::fill_rect` / `blend_rect` 的源 alpha 混合、`Scheduler::set_timeout`、`TextCompositionEvent`。
- **文档回写**：`SPECIFICATIONS.md` §7 新增裁决 7.33、附录 A.2 新增 G18 行、版本脚注 v0.28 → v0.29；`ARCHITECTURE.md` §10.2 补粘贴件形态与界面腿余下清单；`PLAN.md` §6 缺口表新增 G18、§8 的 M1 落地清单与「鼠标 → 选区 → 复制文本」接缝行更新、测试计数 22 → 23；`AGENTS.md` §2 目录表与 §6 现状快照同步。
- **代价与边界**：块数与原文本行数同阶（一次算全的计划而非拉取式生成器），一万行的粘贴即一万块排期加一份等长拷贝；`Convert` 的目标行尾本件只收最终值，其配置来源留待设置面板裁决；本件不含界面，故剪贴板读取、警告对话框、逐行排期与 copy-on-select / 右键三态均未落，界面侧须先出视觉稿评审。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是 `SPEC.FEAT.INTERACT.03` 粘贴腿的**首版纯逻辑腿落地**。

---

## v0.28（2026-10-02）选区归一与选中文本的纯逻辑件（裁决 7.32）

**动机**：`SPEC.FEAT.INTERACT.02` 与 `SPEC.FEAT.INTERACT.03` 的首版面要求流式拖拽、矩形块（列模式）与三项默认关闭的复制变换。其中高亮绘制、鼠标事件与剪贴板写入属界面侧（且按既有规矩须先出视觉稿评审），而「两个端点 → 逐行列区间 → 选中文本」这一半与像素无关、却是最容易算错的一半（双宽字符被切成半格、历史溢出后的越界行、三项变换谁先谁后）。故先把它落成脱界面的纯逻辑件并配满单测，界面棒接的是它的产物而不是它的算术。

- **新增裁决 7.32（选区归一与选中文本的口径）**：① **形态＝纯逻辑件**，公共头既不含 Aurora 类型也不含 `config` 类型（`config/settings.h` 已含 `ui/palette.h`，反向依赖即成模块环），故三项复制变换以本件自己的 `ui::CopyOptions` 表达、由调用方从 `config::TerminalSettings` 的三个同名键搬值。② **行号坐标是调用方给定的存储行序**（`grid::Storage::line` 的索引），本件不拥有也不换算：权威网格在后台读线程，跨可见区的复制必须在会话锁内按存储行序取行，而主线程 `ScreenMirror` 的行号随回看偏移变，用它存选区就会在滚动时漂移——「选区随 scrollback 跟随」因此由坐标空间本身表达。**同时把一条边界记成显式欠项**：scrollback 饱和后再来新输出会挤掉最旧行、其余行索引整体减一，此时活着的选区相对内容下移一行；消除须给存储加单调行号，归接线的下一棒。③ 归一判据：两端点重合（单击）不成选区而出空表；流式按 (行, 列) 字典序定首尾，首行从起点列到行尾、末行从行首截到焦点列、中间行整行，故拖拽方向无关；列模式每行取同一列区间；列端点按网格宽度截断。④ 取文本三条：行以 **LF** 分隔且落剪贴板一侧不翻译（行尾的唯一决策点留在粘贴策略与串口 `line_ending`）；左界落在双宽延续格上时**整字符纳入**且延续格永不产第二个字符；越界行不产文本也**不补空行**。⑤ 三项变换的**生效次序固定**为「剥 tmux 细线制表符 → 剥行尾空白 → 合并反斜杠续行」（前一步的产物是后一步的判据），各自口径定死：行尾空白只认 U+0020、续行只认**奇数个**行尾反斜杠（`foo\\` 是字面反斜杠，合并它就把一条完整命令切开）、边框集取 tmux 默认形态的 11 个细线码点且**纯边框行整行丢弃**。⑥ 验收十三例。
- **代码落地**：`include/borealis/ui/selection.h` + `src/ui/selection.cpp`（`ui::GridCellPos` / `SelectionShape` / `Selection` / `RowSpan` / `CopyOptions` / `row_spans` / `copy_text`），经 `src/CMakeLists.txt` 编入 `borealis_core`。
- **测试**：`tests/unit/utest_selection.cpp` 13 例（归一 5、取文本 4、变换与次序 4），经 `GLOB CONFIGURE_DEPENDS` 自动入 CTest，非 e2e 通道 **22 项全绿**。首跑两处失败都是**期望值写错而非实现错**——流式首行的右界本就到行尾（`{0,3,4}` 是把「首尾两行都按端点截断」想窄了），以及边框用例把文本写到列数之外触发 `Row::cell` 的越界 fast-fail；改正后通过，未动实现。
- **文档回写**：`SPECIFICATIONS.md` §7 新增裁决 7.32 与版本脚注 v0.28；`ARCHITECTURE.md` §10.2 补已落地形态与余下腿；`PLAN.md` §8 新增「选区归一与选中文本」行并把 `SPEC.FEAT.INTERACT.02/03` 的余下项列全；`AGENTS.md` §2 目录表与 §6 现状快照。
- **代价与边界**：① 本件不含界面，故鼠标 → 格子换算、选区高亮绘制、剪贴板写入与 copy-on-select / 右键三态均未落，且界面侧须先出视觉稿；② ⑤ 的边框是「按字符集删」而非「认出 tmux 界面再删」，用箱线画的表格与进度条在开启该项时一并被剥，故该项默认关闭；③ ② 的漂移在 scrollback 饱和前不存在，但它是长期跑日志时的常态。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是 `SPEC.FEAT.INTERACT.02` / `03` 的**首版纯逻辑腿落地**，两条需求的界面腿仍在建。

---

## v0.27（2026-10-02）真机目视走查首轮闭环：修掉装配层初始焦点、登记 G17（裁决 7.31）

**动机**：`SPEC.FEAT.INTERACT.01` 的键盘映射一棒留下一条书面未闭环项——「自动化证据覆盖到字节被对端正确解释，光标跟手与键入画面表现须由人目视」（裁决 7.30⑥）。本轮把它结掉一半，产出两条此前结构上抓不到的结论：**一个装配缺陷**（视口从未获焦 ⇒ 真机上按键全哑，而集成用例以替身连接观测字节、不经真实焦点序，故测试全绿），**一个框架缺口**（DPI 缩放真值源不统一 ⇒ 最大化/还原后网格只覆盖客户区约 1/1.5）。走查本身受会话锁屏限制，可用面与不可用面须如实分离记录，以免「画面没变」被误读成行为不变量成立。

- **新增裁决 7.31（真机走查首轮结论）**：① **锁屏下的可用面**——`SetForegroundWindow` / `SendInput` 静默失效（`LockApp` / `LogonUI` 在跑）故键入与光标跟手不可验；WGC 截屏与非客户区标题栏按钮的 UIA `Invoke`（最小化 / 最大化 / 关闭）可用，故尺寸变更与渲染覆盖可验；**不用注入消息冒充真实点击**去结键入腿。② **装配层必须显式派初始焦点**——框架只在模态 `FocusManager::push_scope` 里 `set_focus(cands.front())`，启动路径不给焦点序首个控件派焦点，故 `src/main.cpp` 于 `Application` 构造后调 `app.focus().set_focus(view.get())`；像素证人为「未派焦点时光标恒是失焦空心描边块且不翻相位，补上后成闪烁实心块」；派发刻意留在装配层而非控件 `on_mount`（后者依赖 thread-local 的 `current_focus_manager()`，挂载前不保证可取）。③ **登记 G17**——`BuildContext::scale_factor` 与 `Painter::scale()` 在 144 DPI 机器上恒 `1.0`，而同一窗口逻辑尺寸随路径给出 `960x640` / `2560x1369.33` / `640x426.667` 三个互不相容值（后两者带 `1.5` 的除法与乘法痕迹），故最大化后网格覆盖不足；处置按裁决 7.13①「不等不绕」，由 Aurora 侧收敛为单一缩放真值源（建窗 / `WM_SIZE` / 帧缓冲 / 上报四面同源），成因一侧**在 Aurora 仓自有记录**（`codespec/specification/08-tooling.md` §8.2 的遗留库层缺口：`Impl::scale` 在成员初始化列表取值、早于 `enable_dpi_awareness()`，与 `handle_size` / `handle_getminmaxinfo` 现调的 `dpi_scale()` 及读 cached 成员的 `set_size` / 鼠标坐标构成三方发散），本仓的实测数值与该机制逐位吻合，等于在消费者侧指认该缺口并给出「解除其两个 e2e SKIP 守卫即验收」的判据。④ **仍未闭环**——键入观感、光标跟手、滚动回看手感三项须解锁后由人目视，本轮不宣称「可用」。⑤ 裁决 7.29 的四条框架补全**已推送至 Aurora 远端**，`AGENTS.md` §6 的「本地提交，未推送」表述作废。
- **新增缺口 G17（附录 A.2）并回填 A.3**：本仓换算路径已单源（只经 `ctx.scale_factor`，格度量走 `FontEngine::monospace_cell(font, scale)`），故该腿回货后本仓零改动；阻塞 `SPEC.FEAT.RENDER.01` / `RENDER.05` / `XFER.01` 的真机验收腿。
- **文档回写**：`SPECIFICATIONS.md` 裁决 7.31 + A.2 的 G17 行 + A.3 的同类派发路径 + 附录 A 复核日期与版本脚注（v0.27）；`ARCHITECTURE.md` §3.5 的装配规则（初始焦点）、§9.6 与 §15.3 的缺口清单；`PLAN.md` §6 缺口表与 §8 验证现状（真机走查记「半结」并列出三项待人工复核）；`AGENTS.md` §6 现状快照。
- **代价**：修复只一行，代价在它暴露的盲区——**测试面与真机面的接缝不在断言里而在焦点序里**，故此后凡「键能不能着陆」类需求，验收须含一条经真实 `Application` 焦点序的用例或走查项；G17 的登记让 `SPEC.FEAT.RENDER.05` 的 Windows 真机腿重新出现框架阻塞（G12 关闭的是**通知 API 已具备**，本条是**框架内部各处取数未收敛**，两者是不同事实）。

---

## v0.26（2026-10-02）键盘映射首版落地：编码表 / 通道分工 / 三条派发出去的框架缺口（裁决 7.30）

**动机**：上屏第一棒之后应用「看得见但打不了字」——`SPEC.FEAT.INTERACT.01` 同时卡着 M1 的两条出口判据（「中文可输入可显示」与光标跟手走查）。这一棒的实质工作不在字节编码（遗留档口径是xterm 四十年的稳定约定），而在**两条输入通道的分工**与**撞到的三个框架缺口怎么打桩**：可打印键若同时由 `KeyEvent` 与 `TextInputEvent` 发出，屏幕上每个字符出现两次；`Tab` 与 Alt 系在框架侧就到不了控件，绕不绕是本仓必须回答的问题。落地过程中实测出「`Ctrl+Alt` 系该由谁吃掉」这一条并非编码问题而是派发顺序问题（框架在派发到控件之前先跑快捷键表），遂一并写进裁决。

- **新增裁决 7.30（键盘映射的编码口径、通道分工与打桩边界）**：① 编码表是**纯逻辑件**（`term::encode_key(KeyPress, TermModes) -> optional<string>`，公共头不含 Aurora 类型），`term::KeySym` 与 `aurora::KeyCode` **逐值对齐**、互转即 `static_cast`，对齐本身由一条单测逐个枚举值锁住；**返回空是语义不是失败**（该键不归本件编码）。② 遗留档口径定死：控制码＝大写折叠后 `@`..`_` 取 `& 0x1F`（`Ctrl+Space`＝NUL、`Ctrl+Backspace`＝`0x08`、`Ctrl+Enter`＝CR）、退格发 **DEL** 而非 BS、`Shift+Tab`＝`ESC[Z`、方向键与 Home/End 随 `DECCKM` 在 CSI/SS3 间切换、F1–F4 恒 SS3 字母族而 F5–F12 / Delete / PageUp / PageDown 属波浪号族、带修饰改走 `CSI 1;mask<letter>` / `CSI n;mask~` 且 `mask = 1 + Shift + Alt*2 + Ctrl*4`、Alt 与 Meta 同口径。kitty / `modifyOtherKeys` 是需求原文的延后观察项，故「无遗留编码可发」的键不发新式序列。③ **通道分工**：无 Alt/Meta 的可打印键归 `TextInputEvent`（框架在组合期已丢弃残余 `WM_CHAR`）并走 `Session::send_text` 的**会话编码**，转义序列已过编码环节故走 `send_bytes`；实测确认 Win32 后端在 `ch < 0x20` 时**不发**文本事件（`handle_char` → `on_char` 首行显式让位，注释自陈「控制字符交给 `KeyEvent` 处理」），与本件分工无冲突——这条边界由集成用例锁住。④ **`Ctrl+Alt` 系一律不编码**：该系与 UI 快捷键冲突需求交配置裁决，而框架 `Application` 在按键到达控件**之前**先跑 `ShortcutRegistry`，故让快捷键层独占，设置面板落地后覆盖表由此驱动。⑤ **撞 G14 / G15 / G16 不等不绕**（裁决 7.13①、7.24③）：编码层先把 `Tab`、Alt 系、小键盘的字节形态写对（接回即用），派发侧留 `TODO(SPEC.FEAT.INTERACT.01)` 桩；不自造窗口消息钩子、不改派发顺序、不在应用侧私补 `KeyCode`；`DECKPAM`/`DECKPNM` 有模式位而无可发之键，该腿验收随 G16 关闭移动。⑥ **验收分三层不重叠**：编码表逐字节（`utest_keymap` 7 例）→ 链路形状与模式生效（`itest_key_input` 6 例，`DECCKM` 经**真实状态机**喂入而非注入快照）→ 真机对端接受度（`etest_key_forwarding` 2 例，cmd.exe 的行编辑与命令提交）。
- **代码落地**（`borealis_core` 新增一张公共头与一个实现文件）：`include/borealis/term/keymap.h`（`KeySym` / `KeyPress` / `encode_key`）+ `src/term/keymap.cpp`（US 布局成对字符表、特殊键的字母族与波浪号族两形态、控制码折叠、修饰掩码拼装）；`src/ui/terminal_view.{h,cpp}` 新增 `on_key_event` / `on_text_input` 两个入口与 `wants_navigation_keys()` / `wants_activation_keys()` 两个钩子声明（不声明则方向键被派发器当几何焦点导航吃掉、Enter 被直接当 `activate()` 消费，终端收不到按键），模式快照经既有的 `Session::read` 短临界区取用（新增私有成员 `modes_snapshot()`），不新增跨线程协调点。
- **测试**：`tests/unit/utest_keymap.cpp` 7 例——枚举逐值对齐框架 `KeyCode`（含未知值 4096 不发）、控制码三特例与区间外不发、`DECCKM` 两形态、功能键与编辑键的 xterm 形态、Alt / Meta 同口径、可打印键归文本通道、纯修饰键与未映射键不发；`tests/integration/itest_key_input.cpp` 6 例——控制键经真实派发器落到替身连接恰好一字节且抬起不重发、`CSI ? 1 h/l` 喂入后方向键与 Home 在 SS3/CSI 间切换、功能键与带修饰扩展形态、可打印字符只经文本通道一份（`KeyEvent` 那一份必须让掉）且 CJK 按会话编码成 UTF-8、无焦点时按键不着陆、转义与文本混发顺序逐位保持；`tests/e2e/etest_key_forwarding.cpp` 2 例（真机 ConPTY，交互桌面会话内跑，裁决 7.19⑤）——键入 + 回车提交命令并以**整行相等的输出行**为据（回显行含提示符，子串匹配会把正在键入的那一行也算命中）、三个 DEL 后接字尾证明退格字节被行编辑接受（发成 BS 或未发即整行不符，非空转）。首跑即 6/6 通过，其中一例的**期望值**经实测改正（`Home` 带 Shift+Ctrl 的掩码是 6 而非 5：`1 + Shift + Ctrl*4`）。非 e2e 的 21 项全绿（MSVC + Ninja，新增两套件经 `GLOB CONFIGURE_DEPENDS` 自动入 CTest）；e2e 累计三项（本批 2 + 既有 1 类）实测通过。
- **框架侧三条缺口入册**：`SPECIFICATIONS.md` 附录 A.2 新增 G14（`match_shortcut` 的 `KeyCategory::Tab` 分支无条件 `move_focus` 并消费，方向键与 Enter/Space 各有控件优先钩子而 `Tab` 没有；后端侧无阻拦，`VK_TAB` 已映射进常规按键链）、G15（`handle_syskey` 只 `mods.apply` 推进修饰态便把 `WM_SYSKEYDOWN/UP` 交回 `DefWindowProcA`，不产生 `KeyEvent`，Alt 系在 Win32 上永不到达控件）、G16（`KeyCode` 枚举注释自陈小键盘 KP_0-9 未建模、映射到 `Unknown`，`KP_Enter` 并入 `Enter`，无 NumLock 键码或态）。三者均按裁决 7.13① 派发 Aurora 侧以「公共 API + 单测 + 文档回写」补全，A.3 反哺承诺补该派发说明。
- **文档连带回写**（本批）：`SPECIFICATIONS.md` §4.3 的 `SPEC.FEAT.INTERACT.01` 补「框架现状（2026-10-02 实测）」段、§7 新增裁决 7.30、A.2 增三行、A.3 补派发行、文末当前版本号改 v0.26；`ARCHITECTURE.md` §3.3 输入路径由「未接线」改为已落地形态（事件入口 → 编码 → 会话两条写通道）、§9.6 事件链现状补 G14/G15/G16、§15.3 缺口表同步；`PLAN.md` §8 的 M1 已落地清单与验证现状更新；`AGENTS.md` §2 的 `term/keymap.{h,cpp}` 与两个测试文件入表、§6 现状快照新增键盘映射条并把「键入未接」改为其余项。
- **代价与边界**：① 遗留档口径不含 kitty 协议，故超 26 键的加修饰组合（`Ctrl+Shift+F13` 一类）不发；② `KeySym` 与框架枚举的对齐是**约定而非类型系统保证**，以一条逐值单测换一次脱 UI 的可测性；③ `Tab` / Alt 系 / 小键盘三条腿在本仓**编码已备、派发未到**，框架回货前 `SPEC.FEAT.INTERACT.01` 的「完整转发」只能算部分达成，集成与 e2e 用例**刻意不断言「收不到」**（否则回货当天变成假失败）；④ 真机目视走查（键入时光标跟手、CJK 上屏观感）仍未闭环，自动化证据只到「字节被对端正确解释」；⑤ IME 组合态就地渲染（`SPEC.FEAT.INTERACT.06`）不在本棒，本批只保证 committed 文本经会话编码发出，preedit 绘制与候选窗定位另立一棒。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是 `SPEC.FEAT.INTERACT.01` 的**首版代码落地**与三条实测缺口的**登记派发**，该条语义未改，只是从「未开工」变成「遗留档口径可验收、三条腿等框架回货」。

---

## v0.25（2026-10-02）上屏第一棒主路径落地 + 框架四条缺口回货（裁决 7.28 / 7.29）

**动机**：本批把两件原本分期的事撞在同一天完成，且互为前提。其一是渲染第一棒（`SPEC.FEAT.RENDER.01` / `03` / `04`）的绘制腿——纯逻辑前置在 v0.20 / v0.21 已各自单测，缺的只是落到 `aurora::Painter` 的那一层；其二是裁决 7.24③ 派发给 Aurora 侧的四条缺口（G13 批量文本 opts、G2 连击序号、G9 OS 级全局热键、G11 系统通知）同日以公共 API 落地并推送。二者交在**斜体**上：分流是等 G13 才撤的过渡件，撤销又必须与像素回归同批（7.24 的代价条款），否则「批量入口丢 opts」会静默把斜体画成正体——同字号下肉眼几乎看不出，逻辑层又无从观测。落地前还实测出下划线档位的 SGR 编码映射与裁决 7.25④ 错位，须先定死编码再画笔形。

- **新增裁决 7.28（下划线四档的 SGR 编码与笔形口径）**：① 映射定死——`SGR 4`（无子参数）与 `4:1` 单线、`4:0` **关**（不是单线）、`4:2` **双线**、`4:3` **波浪**，`SGR 24` 与 `SGR 0` / 空参数 `CSI m` 清档位；此前 7.25④ 与 `RENDER_UI.draft.md` V7 把 4:2 / 4:3 的语义写反了。② `SGR 21` 取 ECMA-48 的「双线」，xterm 把它实现成「关粗体」是其自身历史分歧（关粗体标准写法是 `22`，本仓 `22` 不变）；后果明写：对按 xterm 口径发 `1 … 21` 的旧程序会多画一条双线且粗体不消，有意取 ECMA-48 一侧。③ 枚举**只四档** `{None, Single, Double, Curly}`，`4:4` / `4:5` 按单线呈现且不进枚举（加档位＝实现需求未覆盖的行为），降级也**不挂计数器**——留痕件在消费方落地前是死代码。④ 笔形：线宽 1 物理像素 = `1/scale` dp，单线在基线下 1px，双线在 +1px / +3px，波浪以 **4 物理像素**为一周期做三角折线，且**相位锚在该行的绝对像素 x 而非 run 左沿**（锚在 run 左沿会让相邻 run 在边界处错位，而 run 切分本就随样式全等而变，错位即判失败）。
- **新增裁决 7.29（四条缺口的回货形态与 G13 分流的撤销）**：① G13 关闭，`Painter::draw_text_runs` 新增 `(runs, opts)` / `(runs, aa_mode, opts)` 两重载，单参重载改委托实现故历史调用点逐位不变；框架**刻意不做 per-run opts**（免与 `Font` 的样式语义重叠成两条矛盾来源），故本仓按「是否带排版选项」把一行分两批下调用（每行最多两次批量调用）。② G2 关闭，`MouseEvent::click_count`（1..3，Release / Move 恒 1）由 `EventDispatcher::dispatch_mouse` 派发前**集中**判定、后端不参与，阈值取库内常量（500ms / 4dp）而**未接系统双击速度**（跨平台手感一致优先），判据取 `position` 而非随命中链改写的 `local_position`，只有 Press 参与计数。③ G9 关闭，`OsHotkeyRegistry` + 值语义 `OsHotkeyHandle`，无后端平台 `enabled()` 为 `false` 且 `add()` 恒返回机器可见错误码（不静默 no-op），命中**不在消息泵内执行动作**，只入内部队列由帧循环 `drain_pending()` 排空（回调可能重建页面，泵内重入会把布局切到半途）。④ G11 关闭，`NotificationCenter::notify` + 激活回传 + `pump_events()`，单测走「仅记录」后端。⑤ 撤销的验证义务与**像素判据的成立形态**：「同一格先正体再 `SGR 3` 比两帧」**不成立**（加斜体本身改变 run 切分，丢 opts 时差分仍非零 → 空转），跨行对照同理失效（行间亚像素相位本身即差异源），成立形态是「同一行、同格底色、同一字形的两格对照 + 按整格宽取带以保持相位一致」。G9 / G11 的消费者（`SPEC.FEAT.WS.12` / `SPEC.FEAT.INTEG.03`）仍在观察池——**回货只解除框架依赖，不改落期与优先级**。
- **网格与绘制前置（`grid/cell.h` + `term/terminal.cpp` + `ui/{palette,cell_layout}.{h,cpp}`）**：新增 `grid::UnderlineStyle` 四档并由 `Cell::underline` 承载（档位有独立取值域不塞位掩码；新字段吃掉的是既有的对齐空档，`sizeof` 不变，架构 §4.7 的 10k 行常驻预算），`ui::CellPaint.underline` 同步改枚举——它本就参与 run 合并判据，故单线与双线自动不并段；`apply_sgr` 按 7.28 的映射解析 `4` / `4:0`…`4:5` / `21` / `24`（子参数取 `sub[1]`，dotted / dashed 与域外码归单线，不抬枚举也不留计数器）；新增纯逻辑件 `ui::decoration_rects` 产出落笔矩形——线宽 1 物理像素，四边经「× scale 取整再 ÷ scale」吸附像素边界（落在小数 dp 上会被抗锯齿糊成发灰的 2px），波浪逐像素列成段且相位锚在**不含内边距**的网格像素 x，于是相邻 run 在切分边界处波形连续；`make_geometry` 增 `padding_dp` 入参并按「(可视 dp − 2×内边距) ÷ 格宽」取行列数，`GridGeometry` 带 `scale` / `padding`、`rect_for` 原点含内边距，色带、装饰与光标共用一套坐标（边带吃满可视区时行列数为 0，与「窗口最小化」同形）。以上刻意不含 Aurora 类型，全无框架依赖即可验完。
- **可见区副本（`session/screen_mirror.{h,cpp}`，架构 §3.4 的落点）**：`ScreenMirror::apply(grid::Storage &, span<const Damage>, size_t back_rows)` 只做整行并入与行号换算、不含框架类型，绘制侧因此只剩翻译。行号换算唯一输入是 `back_rows`（距底行数）；整窗重建的三个触发条件是尺寸变、可见窗起点变（回看偏移与 D6① 推窗）、本帧含整屏脏。`apply` 收**可变**网格而非设计稿原写的 `const`——脏标记是写侧 `collect_damage` 的扫描依据，「消费必须与并入同处」，稿 §4 已就地更正；副本行抄完即 `clear_dirty`（脏态属于「待重读」，抄进副本会指认不存在的新内容）。
- **帧唤醒（`session/session.{h,cpp}` 的 `set_frame_wake`）**：归 `Session` 而非 `DamageQueue`（`push_rows` 在网格锁临界区内，跨线程 post 进锁与「锁内不做 IO」相抵；也不选超时轮询，那会让输入延迟在 `wait_events` 阻塞期间不可控）。两个调用点：ingest 一轮批量输入**只唤醒一次**且必须在队列成型之后（否则唤醒的那一帧取不到提交、空排一帧），resize 的整屏脏同样唤醒、无效尺寸整条路径不走。句柄锁内取副本、锁外调用。**消除 `TODO(SPEC.FEAT.RENDER.01)`**，其中残留的 `au::post_to_main` 表述按裁决 7.23ⓒ 一并去掉（该符号只在 `aurora::detail` 下存在）。
- **视口控件（`src/ui/terminal_view.{h,cpp}`，全仓唯一触达 `aurora::Painter` / `au::Widget` 的翻译单元）**：类声明留 `src/` 而不进 `include/borealis/`（裁决 D1①，含此头即含框架头，公共头的「不含 Aurora 类型」约束因此不破洞）。四层叠序＝整盒铺底 → 色带 → 每行批量文本（含斜体两批）→ 装饰与光标。实测口径三条：**绘制集合是 `clip_bounds()` ∩ 可见行全体**且 ① 层每次 `on_paint` 都整盒铺底（框架的局部裁剪帧会对裁剪区 `clear_rect` 归零基底，「只画脏行」在像素层不成立也不必要，未变行重画逐位相同故差集仍 ⊆ 提交行集）；**回看不借用框架的内容平移**（基类 `scroll_viewport_` 只当「距顶行数」容器，`step` 为 1 行，声明 `overflow_strategy(Scroll)` 会让框架把行数当 dp 加进绘制盒）；**闪烁挂 `Scheduler::set_interval` 而非覆写 `tick`**（基类 `tick` 有手势短路），焦点态直接取 `is_focused()`、不覆写 `on_focus_change`，`wants_focus_ring()` 为 `false`（终端用光标形态表达焦点态，环会污染像素判据）。**交付后自审补的一处缺陷**：块形光标的三段式第三段（按该格合成底色重画字形）用的是逐片段 `draw_text`，撤销分流时只改了 ③ 的批量入口、漏给了这一段的 `TextLayoutOpts`，于是「光标停在斜体格上」那格会静默退化成正体——整帧其余部分逐位相同，肉眼与既有八例都发现不了。补 `opts.italic = paint.italic`（与 ③ 同源）并加第十例，判据形态是「同一行相邻两格、同字形、一正一斜，块形光标逐帧移到其上」＋先 REQUIRE 光标色确实落了笔（否则该例因另一条路径空转）。
- **测试**：`utest_terminal.cpp` 十档 SGR 逐条对上 7.28 的映射（走真解析器，故子参数收法一并验）并锁「`SGR 24` 只关档位、粗体与前景仍在笔上」与「`SGR 0` 连档位一起复位」；`utest_palette.cpp` 补「档位原样抵达绘制意图且不改色合成」（反向守卫是同样位置改前景必变）；`utest_cell_layout.cpp` 补档位参与 run 合并（单线与双线不得并段）、单线 / 双线 / 波浪 / 删除线在 2× 与 1.5× 两种缩放下逐段核落点与宽高、波浪跨 run 的相位连续性（用 7 px 宽的格构造——4 的整数倍宽的格在任何列边界都是相位 0，测不出锚点差别）、以及内边距扣减后的行列数与原点；`utest_screen_mirror.cpp` 9 例逐条锁副本稿 §4 的判据（增量帧只并脏行、整屏脏与尺寸变更换窗、侧表随行搬运、距底偏移下的落点与越界丢弃、脏标记消费）；`utest_session.cpp` 补帧唤醒 5 例（一轮多提交只唤醒一次、唤醒时队列已成型、只改模式的输入不唤醒、resize 唤醒一次且无效尺寸不唤醒、换句柄立即生效）；`tests/integration/itest_render_viewport.cpp` 10 例为**本仓首批真像素断言**（`HeadlessSurface` 两帧 RGBA8 差分）——底色/色带/装饰/光标走 `fill_rect` 不透明快速路径可断言逐位相等，文本是 AA 灰度只断言有墨/无墨，判据一律含 alpha 四通道。三条判据写进用例而非留给推断：**差集 ⊆ 本帧提交行 ∪ 上一帧光标行**（旧光标格回到默认底色且非光标色＝残影判据）、**格心是字形所在不是底色所在**（断底色须取格顶/格底，取格心会把 `B` 的中横读成「底色不对」）、**D6①「距底恒定」不等于画面静止**（新输出把可见窗上推一行，故断 `row_band(grown, i) == row_band(again, i + 1)`）。「裁剪盒外不画」**不做**像素断言（`dirty_bounds()` 非虚、控件只能整盒标脏，裁的是绘制循环不是帧缓冲），备屏不可回看与双宽延续格带笔形、零宽不另占一格亦各有一例。斜体两例各以**变异注入自证非空转**：批量入口一例（两边都进不带 opts 的批）与块形光标重画一例（把第三段的 `opts.italic` 写死 `false`）分别 FAIL 于自己那条 `count_diff > 0`，其余八例不动。非 e2e 的 19 项全绿（MSVC + Ninja）；e2e 两项未跑（同 v0.24 口径）。
- **文档连带回写**（本批）：`SPECIFICATIONS.md` §7 新增裁决 7.28 / 7.29、A.1 批量文本行改写并补连击序号 / OS 级全局热键 / 跨平台系统通知三行、A.2 的 G2 / G9 / G11 / G13 四行标「已关闭」（保留原始事实与行位防伪死链）、A.3 反哺承诺补「四条已按本条形态进入公共 API」、文末当前版本号由 v0.24 改 v0.25；`RENDER_VIEWPORT.draft.md` 按实现逐段更正（钩子表、`apply` 签名与脏标记归属、几何与 `resize` 落点、绘制集合、`reproject` 伪码与 D6① 判据形态、③ 层的两批形态与光标三段式的 opts 纪律、§9 验收表改为「用例名 + 实测手段」并记「同格两帧 / 跨行对照」两类空转写法、§10 回写清单标已执行、§11 推断项逐条转实测结论）；`RENDER_UI.draft.md` V7 与 §1 的编码照 7.28 改写；`ARCHITECTURE.md` §3.3 输出路径图与「已落地形态」段按帧唤醒 + `ScreenMirror` 改写、§3.2 标注已接线、§9.2 标题改「视口控件的取用形态与绘制序列（已落地）」并补五层序列 / 两批 opts / 光标三段式 / 裁剪无像素可观测面、§9.5 补 D5②③④ 与 D6① 的实测形态、§9.6 改写为「渲染与文本选择链路无框架阻塞项」、§10.2 与 §15.3 缺口表按四条闭合更新、§16 的 C（多击接入方式）随 G2 闭合从待定表移除；`PLAN.md` §2（M0 收口、阻塞粒度、渲染棒内序现状）、§5 观察池两行（G9 / G11 依赖解除而落期不变）、§6 缺口表四行、§8（M0 状态、M1 已落地清单与未开工清单、新增「网格 → 主线程副本 → 视口绘制」接缝行、会话→平台连接与网格行数变化两行的余下项、验证现状 19 项与像素判据的适用边界）；`AGENTS.md` §2 的 `src/ui/terminal_view.{h,cpp}`、`session/screen_mirror.{h,cpp}` 与 `itest_render_viewport.cpp` 入表（并写明绘制侧控件刻意无公共头是裁决 D1① 而非欠项）、§6 现状快照新增「渲染上屏第一棒已落」条并把缺口、尺寸来源、帧唤醒、零宽叠字四处由「未接线」改为已接线 + 剩余项。
- **代价与边界**：① 含斜体的行付两次批量调用的固定开销，换回同 `Font` 共用字体面解析与行高度量（此前是每个斜体片段一次 `draw_text`）；② `ScreenMirror` 的行级增量并入**不驱动跳行**，只省下每帧整可见窗的拷贝——行级脏模型的落点是「标脏决策」而非「行过滤」，绘制仍遍历裁剪内全部可见行；③ 闪烁周期与内边距本批内置为构造参数，可配项随设置面板那一棒接（`SPEC.FEAT.PREF.02`）；④ `SPEC.FEAT.TERM.06` 上报模式下的滚轮转发与 alternate scroll 不在本棒，`on_scroll` 留 `TODO` 且恒走本地回看；⑤ G2 / G9 / G11 回货但本仓暂无消费者，`SPEC.FEAT.INTERACT.02` 的双击选词腿只是不再被框架阻塞；⑥ 真机走查（`Borealis.exe` 目视复核）与吞吐基准的时间门禁（`PLAN.md` 裁决 7.23③）尚未执行，像素断言目前只在 Headless 后端成立。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是**裁决 7.23④ 第一棒的代码落地**与**框架现状的实测回货登记**，`SPEC.FEAT.RENDER.01` / `03` / `04` 的语义未改，只是从「范围与判据已定」变成「像素可验收」。

---

## v0.24（2026-10-02）配置层代码与单测落地：框架点号路径模型的三条硬约束（裁决 7.27）

**动机**：裁决 7.26 给的是配置层的**范围与分期**，落地时才发现「怎么落」这件事上有三条约束来自 Aurora `Preferences` 的实现而非本仓的选择，且都在首轮单测里以失败形态暴露（写进去读不回来、留痕莫名其妙多一条）。这三条不写进裁决，设置面板与「便携模式」两处都会重踩一遍。本批同时是 `SPEC.FEAT.PREF.01` 的色值腿、`SPEC.FEAT.PREF.03` 的落盘腿与 `SPEC.FEAT.PREF.07` 的损坏降级腿的**代码落地**（三者按 7.26 提前到 M1）。

- **新增裁决 7.27（配置层落地的四条实测约束）**：① **框架的点号路径模型**——`Preferences::reconcile()` 先把整棵树 `flatten()` 成「点号复合键 → 叶子值」的平面表，再按点号 `resolve_set()` 重建嵌套，**只有数组是叶子**。后果：**对象键一律不得含点号**（`terminal.new_tab` 这类命令 id 作键会被拆成三层嵌套对象），**空对象落不了盘**（拍平时不产生条目，重建后即消失，于是「读回一份空映射」必然假性报缺键）。② 因此 `shortcuts.overrides` 的落盘形态是 `[{command, combo}, ...]` 数组，内存模型仍是「命令 id → 组合键」映射；元素缺键或类型不符只丢该元素并留痕其下标 `shortcuts.overrides[<i>]`，不让整表失效。③ **可缺省色值区分「缺键」与「显式 null」**：`palette.cursor` 缺键 → 回落**本文件点名主题**的光标色并留痕（否则 one-dark 这类光标色 ≠ 前景色的主题会被画成前景色，主题设置形同被忽略）；显式 null → 用户点名的「未配」，绘制侧按裁决 7.25③ 回落 `default_foreground`、不留痕。开关与阈值不属色值，回落一律随 `Settings{}`，主题表不留开关的第二真值源。④ **`ui::PaletteSpec` 必须自带默认比较**：C++20 不为类隐式声明 `==`，缺了它 `config::Settings` 的 defaulted `==` 被**静默删除**，而「写盘再读回逐域等值」正是 PREF.03 的验收判据。
- **代码落地**（`borealis_core` 新增三个实现文件，公共头三张）：`config/settings.h` + `src/config/settings.cpp`＝**默认值唯一定义处**，四域（外观 / 终端 / 连接 / 快捷键）全量字段以类内初始值写成，`Settings{}` 即 PREF.06 的零配置形态；调色板不在类内展开 16 色而由 `AppearanceSettings` 构造时转调主题表（色值有第二处定义就会与主题表漂移），并把最小对比度阈值定为 WCAG AA 的 4.5（开关缺省关闭，开它的人要的正是「达标」）。`config/themes.{h,cpp}`＝**色值唯一来源**，八套预置（dracula 缺省居首 + nord / solarized-dark / one-dark / gruvbox-dark / monokai / campbell / tokyo-night），主题名即存储键（小写连字符，显示文案另经词条表），`theme_palette()` 对未收录名回落缺省主题而不是返回全零（黑底黑字比「主题不对」更难排查），chrome 覆盖字段八套全空（首版无消费方，裁决 7.25⑪）。`config/store.{h,cpp}`＝**与框架的唯一读写接缝**（`Preferences` 藏在 PIMPL 内，本头刻意不含 Aurora 类型）：`LoadOutcome` 四态（首次 / 正常 / 解析失败已降级 / 版本高于支持已降级）+ `LoadReport`（`rejected_keys` 点号路径、`unknown_keys`、备份路径、ASCII 说明）交 UI 侧；内部 `ScopeReader` 按作用域读取（缺键 / 类型不符 / 域外三类不合一律回落并留痕点号路径，父作用域整体缺失只留痕父键一次），**枚举的「值 ↔ 文本名」读写共用同一张 `EnumName` 表**；键名字面量在读写两侧各写一遍，其漂移由「全量往返等值 + 双留痕皆空」这条用例守住（本批就是它抓出 ② 的形态问题）。整数与浮点带域界、受限文本带取值域，域外一律回落默认并留痕；备份 `*.corrupt-<epoch 秒>` 早于任何写入，且**备份未成功时 `replace()` 拒绝落盘**（内存值照常更新、原因返回）——覆盖掉唯一现场的降级不叫降级。首次启动不写盘（裁决 7.26⑤）。
- **测试**：`tests/unit/utest_themes.cpp` 5 例（≥8 套且键名合规、名字互不相同、缺省主题居首、每套前景≠背景且 0/7 两档可分、光标色必落定、开关与 chrome 不留进表以免成为第二真值源、未收录名回落缺省主题）；`tests/unit/utest_config.cpp` 10 例（默认值形态、首启不产生文件、**四域全量往返等值且双留痕皆空**、未配光标色不等于黑色光标、满篇坏值仍起得来且逐键留痕、整域缺失只留痕父键一次、色值段损坏时回落本文件点名的主题、损坏 JSON 先备份再回落且备份逐字节原样、高版本同样降级、落盘文件顶层恰是四域 + 版本键且**递归扫全树键名不含 password / passphrase / secret / token / private_key**）。`utest_palette.cpp` 补第 12 例锁住「光标色不参与单格合成」（裁决 7.25③：它是整屏取用，接进 `resolve()` 就等于让一格内容决定光标颜色），并带反向守卫以免该断言空转。两个新套件经 `GLOB CONFIGURE_DEPENDS` 自动入 CTest，无需改 `cmake/BorealisTests.cmake`。非 e2e 的 17 项全绿（MSVC + Ninja）；e2e 两项未跑（`etest_osc_clipboard` 受本机 `OpenClipboard` 返 5 阻塞，成因见 v0.20 / v0.21）。
- **文档连带回写**（本批）：`SPECIFICATIONS.md` §7 新增裁决 7.27、附录 A.1 持久化行补「点号路径模型」的实测限定、A.2 的 G7 行标注「应用侧自校验已按此执行」；`ARCHITECTURE.md` §11.3 增落盘形态三条硬约束与 `borealis::config` 三个文件的职责边界；`AGENTS.md` §2 的 `include/borealis/config/` 与 `src/config/` 由「计划 / 待建」改已落地、§6 现状快照同步；`PLAN.md` §2 现状行同步。
- **顺带纠正一处文档漂移**：`SPECIFICATIONS.md` 文末「版本与变更历史」的当前版本号自 v0.12 起未随本文件推进（本文件已至 v0.23），按规则 12 以实际值改为 **v0.24**。
- **代价与边界**：① ①②两条约束来自框架既有的持久化模型，本仓只能适配——改它属 Aurora 公共 API 形态，须按裁决 7.13① 走派发路径，不在本批；② 数组形态比映射冗长，换来的是「命令 id 可含任意字符」与「空表留得下来」；③ schema 全量意味着**字段先于消费方存在**（无消费方的键占多数），设置面板落地时须以本 schema 反向核对键名；④ 降级提示的对话框尚未落地，`LoadReport` 是出口、落点标注 `TODO(SPEC.FEAT.PREF.07)`；⑤ 留痕只作诊断，不做自动修复也不合并重复键；⑥「拷贝损坏文件失败」的分支未造用例（无法稳定注入，不为不可能路径写防御）。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是裁决 7.26 的**执行**与三条实测约束的**入册**，`SPEC.FEAT.PREF.01` `SPEC.FEAT.PREF.03` `SPEC.FEAT.PREF.07` 的语义未改，只是从「范围已定」变成「代码可验收」。迁移链形态仍挂 `ARCHITECTURE.md` §16 F，本批不闭合。

---

## v0.23（2026-10-02）视觉稿与界面草图全部收口、配置层范围与分期前置（裁决 7.25 / 7.26）

**动机**：上屏层的绘制判据与 `borealis::config` 的 schema 范围都卡在同一批未决项上——`RENDER_UI.draft.md` 的 U1~U4 决定「画什么算对」，`UI_OVERVIEW.draft.md` 的 N1~N8 决定「界面件绑定在哪」，而 PREF.03 的落盘形态与校验深度决定「第一棒的色值从哪来」。本轮一次性把十二项交给人拍板并全部闭合，另加配置层四项与设置面板的分期前置；本批**无代码改动**，代码随下一批按这些结论落地。

- **新增裁决 7.25（U1~U5 + N1~N8 的拍板结论）**：U1 回看位置指示条**画**（视口右侧 2 dp 一块）；U2 视口内边距**四周 4 dp 且入配置**（可改 0 回贴边），行列数算式改为「（可视 dp − 左右内边距）÷ 格宽」；U3 `ui::PaletteSpec` **增 `cursor_color`**、缺省回落 `default_foreground`；U4 双线/波浪下划线**本棒落地**（`CellPaint.underline` 由 `bool` 扩为档位枚举 + 状态机解析 `SGR 21 / 4:3 / 4:4`，笔形以 `fill_rect` 在应用侧合成）；U5 沿用裁决 7.24④。界面层：N1 侧栏折叠缺省（展开态入配置）、N2 窄标签态状态角标优先于类型图标、N3 **状态栏每项一个开关 + UI 入口**（缺省全开，宽度不足时按注册次序从右向左省略并进右键菜单）、N4 pane 底部角标仅分屏态显示、N5 设置页预览用真实绘制路径、N6 UI chrome 不随终端主题（结构预留可选覆盖字段）、N7 右键菜单单层 + 分隔为主而低频簇收二级子菜单（一级可视项 ≤10）、N8 命令 id 英文入框架而中/英词条由本仓 `StringTable` 映射表维护。
- **新增裁决 7.26（配置层首版范围与分期）**：① schema 按 PREF.02 的**四分类建全量**（外观 / 终端 / 连接 / 快捷键），UI 未接线的键先有默认值、无消费方——本条与本仓「不引入将来可能用到的结构」的通则冲突，以人的裁决为准；② **≥8 套预置配色随首版落地、缺省 Dracula**（PREF.01 的色值腿提前，理由是第一棒没有具体色值就没有可判定的目标像素）；③ **单文件 `borealis.json` + 按域 `group()` 嵌套 + 顶层 `schema_version`**，框架元数据在 `__aurora_preference_meta__` 下对应用侧不可见，故自校验只看用户数据树（附录 A.2 G7）；④ **PREF.07 的损坏降级线提前**：未知键只记录不报错，缺键/类型不符/域外回落默认并记诊断，JSON 解析失败或版本高于本仓支持则**先备份 `*.corrupt-<epoch 秒>` 再回落默认**（备份必须早于任何 `flush`），提示以 `LoadOutcome` 交 UI、对话框落点 `TODO(SPEC.FEAT.PREF.07)`，快照与导出导入留 M2；⑤ **首次启动不写盘**，用户第一次变更才 `flush`；⑥ **凭据不入 schema**（连接域只放非敏感默认值，密码与 passphrase 到 CONN.09 只存 OS 句柄）；⑦ **PREF.02 设置面板前置**到上屏两棒之后。
- **文档连带回写**（本批）：`RENDER_UI.draft.md` §1 全局参数表（内边距/光标色/下划线三档/行列数算式）、§2 面板 B 的 V7 补三档笔形判据与面板 E 的 U1 结论、§3 差距表新增 **G-8（`make_geometry` 无内边距项）与 G-9（全仓无具体色值）**并改写 G-1/G-3 的处置、§4 由「待拍板」改为「拍板结论」、§5 记录配图批注已知落后于文字判据；`UI_OVERVIEW.draft.md` §4 改为结论表、§6 落点清单逐条标已执行、稿头状态改「评审已收口」；`ARCHITECTURE.md` §9.2 加内边距与光标色/指示条/下划线档位四条、§11.1 加预置配色与 chrome 口径、§11.2 加词条映射、§11.3 加配置层四处执行形态、新增 **§11.5 界面层的绑定量**、§16 F 行标注首版行为已定；`PLAN.md` §2 的 M1/M2 涉及需求与「配置层与面板的棒位」新增段（**配置层排在渲染第一棒之前**）、§3 的 PREF.01/02/05/07 四行改注、§8 待裁决段收掉 B 并把 F 收窄为「迁移链仍未决」。
- **顺带纠正两处文档与裁决不符**：`PLAN.md` §7 的接线表仍写「经 `au::post_to_main` 唤醒主线程」，与裁决 7.23ⓒ（`Window::surface().request_wake()` + `Application::set_on_frame`，不依赖 `detail` 符号）冲突，已按裁决改写；同段把 §16 B 列为「仍待拍板」而该条已于 2026-10-01 拍板，一并收掉。
- **代价与边界**：① U4 使渲染第一棒范围比裁决 7.23④ 更宽（多一份状态机解析 + 三档笔形），该扩张由裁决 7.25④ 承担；② schema 全量意味着**字段先于消费方存在**，设置面板落地时须以 schema 为准反向核对键名，不得另起一套；③ N6 与 N3 各留了一个「预留字段 / 缺省全开」的口子，它们的 UI 入口都系在 PREF.02 上，面板若再延期这两个结论就退化为纯配置项；④ 配图的 SVG/PNG 批注仍是评审前口径（生图脚本未入库，本轮不手改 SVG、不重出 PNG），**以文档文字为准**。
- 需求条目数量（66 条）与标识体系未变动；本次变动的是**分期与判据**：`SPEC.FEAT.PREF.01` 的色值腿、`SPEC.FEAT.PREF.07` 的损坏降级腿提前，`SPEC.FEAT.PREF.02` 前置，四条需求本身的语义未改，`SPEC.FEAT.RENDER.03` 的下划线三档与 `SPEC.FEAT.PREF.01` 的光标色原本就在需求本体里、此前只是缺实现与缺字段。

---

## v0.22（2026-10-01）框架缺口复核与补全分工：新增 G13、修正斜体结论（裁决 7.24）

**动机**：上屏层即将消费 Aurora 的绘制原语，而 `SPECIFICATIONS.md` 附录 A 是「某一时点的复核结论」（`AGENTS.md` §4.1 第 5 条明确它可能过期）。本轮对 Aurora 当日活动分支逐项实测，一处既有推断被证伪、三条缺口确认仍开放，须把「谁补、本仓怎么取用」定死，否则渲染棒会按错误前提排工。

- **新增裁决 7.24**（四条）：① G2（`MouseEvent` 无 `click_count`）、G9（无 OS 级全局热键）、G11（无跨平台系统通知 API）**复核仍开放**，G9 原列的「应用侧直调 `RegisterHotKey` / X11 grab」候选作废——实测 `ShortcutScope::Global` 的语义是「应用内不限焦点」而非系统级，OS 级注册只能进框架；② **新增 G13**，并把「框架无斜体能力」的推断修正为「能力在引擎层已具备（`TextLayoutOpts.italic` 走 FreeType shear，`FontEngine::draw_text_runs` 静态入口与逐片段 `Painter::draw_text` 的带 opts 重载都收 opts），缺的只是批量公共入口 `Painter::draw_text_runs(span)` 的一层透传」，该项框架工作量因此缩小；③ 四条缺口的**补全由 Aurora 侧承担**（裁决 7.13① 的公共 API + 单测 + 文档回写路径），本轮已作为可派发任务书移交框架侧，本仓不私挂分叉、不自算多击、不直调平台热键与通知 API；④ 本仓撞缺口的取用规则——现有公共 API 能表达就当日用（斜体与字距走逐片段带 opts 重载，绘制侧按「是否带排版选项」分流，G13 关闭后撤销分流并复跑像素回归），公共 API 表达不出的才留观察池并不开工。
- **`RENDER_UI.draft.md` 的 U5 就此收口**：原候选 ①（混合入口）与 ②（等框架补透传）合并为「先 ①、②落地后撤销」，差距表 G-2 行改写为实测形态。**仍待拍板的只剩 U1（回看指示条）/ U2（视口内边距）/ U3（光标色来源）/ U4（双线·波浪下划线是否本棒）**，四屏应用界面草图的 N1~N8 亦同批待结论。
- **文档连带回写**（本批无代码改动）：附录 A.1 批量文本绘制行加 G13 限定、A.2 的 G2 / G9 / G11 行补复核结论与新 G13 行、A.3 反哺承诺把四条并入同一路径；`PLAN.md` §2 的 M0 出口判据与「M0 → M1 阻塞粒度」段改为「G2 与 G13，其中 G13 不阻塞第一棒」，§6 缺口表补 G13 行与派发状态，§8 的 M0 行同步；`AGENTS.md` §6 现状快照加写 G13 与四条缺口的分工。
- **代价与边界**：① 分流形态是过渡件，G13 关闭时的撤销必须与像素回归同批，否则斜体段的判据会静默失效；② 「桩」只打在入口合并上——`SGR 3` 自本棒起就按真斜体验收，不接受「先画成不斜」的降级；③ 本批只确认缺口存在与分工，Aurora 侧的落地形态（`click_count` 的阈值归属、OS 级注册表与 `ShortcutRegistry` 的关系、通知的后端降级档位）以框架侧实际公共 API 为准，届时按裁决 7.13① 与规则 12 复核并回填本文。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次为**框架现状的实测复核与缺口登记**，`SPEC.FEAT.RENDER.03` `SPEC.FEAT.INTERACT.02` `SPEC.FEAT.WS.12` `SPEC.FEAT.INTEG.03` 的语义未改，只把「谁补、怎么取用」写成可执行分工。

---

## v0.21（2026-10-01）DECSCUSR 光标形态进状态机：`SPEC.FEAT.RENDER.04` 的最后一块纯逻辑前置

**动机**：`SPEC.FEAT.RENDER.04` 的三形态写明「随 DECSCUSR 切换」，而状态机此前不解释 `CSI Ps SP q`——编辑器与 shell 换提示符时的光标形态请求会静默落到「未识别终结符」分支被忽略，绘制侧再怎么画也只有块状。v0.20 的边界⑤把这一笔单独列为绘制棒之前的前置项，本批就是它；落完之后渲染第一棒在状态机侧不再有欠账。

- **状态机侧（`include/borealis/term/terminal.h` + `src/term/terminal.cpp`）**：新增 `term::CursorShape`（`Block` / `Underline` / `Bar`）与 `TermModes` 的 `cursor_shape`、`cursor_blinking` 两字段。`do_csi` 的 `q` 分支以**空格中间字节为识别标志**——缺了它 `CSI 5q` 是另一条序列，照单全收的话任何以 `q` 结尾的未实现序列都会改掉光标形态；带私有前缀的 `CSI ? Ps SP q` 一并接受（xterm 同口径）。档位表照 xterm：1/2 块、3/4 下划线、5/6 竖线，奇数闪烁、偶数静止；**缺省 Ps、显式 0 与越界档位整档回落默认闪烁块，而不是保留旧值**——设备发出未定义档位通常意在复位，留着上一档会让形态取决于历史输入。`reset_to_default()`（`ESC c`）随 `modes_ = {}` 一并复位，无需额外分支。会话侧零改动：`Session` 把整份 `TermModes` 交给读取回调，新字段自动抵达未来的绘制方。
- **测试**：`tests/unit/utest_terminal.cpp` 新增 2 例（六档形态与闪烁逐档断言、缺省与越界档位的回落、空格中间字节的识别标志与私有前缀写法）并给既有的 RIS 复位用例补上光标形态两条断言。非 e2e 的 15 项全绿（MSVC + Ninja）；e2e 两项里 `etest_local_terminal` 绿，`etest_osc_clipboard` 仍停在 `OpenClipboard` 返 5，成因与判定见 v0.20，与本批无关。
- **代价与边界**：① 本批只表达**意图**，不画光标：形态与闪烁的像素呈现、以及 `SPEC.FEAT.RENDER.04` 的「可配置闪烁频率」与「失焦降级为空心/静止」全在绘制棒，闪烁节奏按裁决 7.23④ 由框架既有的 `Widget::tick(now)` 驱动，不新起线程；② 闪烁档只随 DECSCUSR 变，`CSI ? 12 h`（DEC 光标闪烁开关）一类未实现也未建模，遇到即维持当前档；③ 未知 Ps 的回落选择是本仓自定的实现细节——`SPEC.FEAT.RENDER.04` 未规定越界行为，改成「保留旧值」须同时动测试。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是裁决 7.23④ 第一棒的状态机收尾，`SPEC.FEAT.RENDER.04` 的语义未改，只把「随 DECSCUSR 切换」变成状态机里可观察的取值。

---

## v0.20（2026-10-01）上屏层两块纯逻辑前置落地：颜色合成与整格几何 / run 切分（SPEC.FEAT.RENDER.01、SPEC.FEAT.RENDER.03）

**动机**：裁决 7.23 把上屏主路径分成两半——无框架依赖的算式，与唯一触达 `au::Painter` / `au::Widget` 的绘制翻译单元。先把前者落定，是因为最容易算错的两处（SGR 颜色的合成次序、`monospace_cell` 物理像素与 `TextRun` 逻辑 dp 的换算）都必须钉在测试里，而不是等绘制侧接上后从像素反推；后者只剩层叠顺序与裁剪过滤。

- **`ui::palette`**（`include/borealis/ui/palette.h` + `src/ui/palette.cpp`，`SPEC.FEAT.RENDER.03`）：`PaletteSpec`（16 基本色 + 默认前景背景 + bold-is-bright 与最小对比度两个开关及其阈值）经 `resolve(grid::Cell, PaletteSpec) -> CellPaint` 出最终绘制意图。合成次序定死为**亮色档 → 暗淡 → 反色 → 最小对比度**：反色若排在亮色档之前，粗体亮色档的格子反色后会把基色当亮色用；最小对比度必须最后，因为前三步都可能把颜色拉近背景。256 色的 16..231 走 6×6×6 立方、232..255 走 24 阶灰的 xterm 固定式子，不受主题影响故不入库配置。两处口径值得单独记录：① **暗淡按「向底色靠拢一半」而非减亮度**——底色不受本仓控制（主题与真彩色背景都可能比前景更暗），减亮度在浅色主题上会把暗淡字推成高对比的深色字，语义正好相反；② **最小对比度只动前景**，朝与背景对比更强的那一极做整数档位二分（256 档）取「达标的最浅插值」，不一路推到纯白纯黑；两侧极端的对比都达不到阈值时取较强一侧，不为凑数去改背景——背景是整屏色带的基准，动它会让相邻格的底色互不一致。对比度按 sRGB 线性化（IEC 61966-2-1 分段式）的 WCAG 相对亮度。
- **`ui::cell_layout`**（`include/borealis/ui/cell_layout.h` + `src/ui/cell_layout.cpp`，`SPEC.FEAT.RENDER.01` 与裁决 7.23②）：`make_geometry(CellPixels, scale, LogicalSize) -> GridGeometry` 把整像素度量折成 dp 步长并给出行列数（向下取整、装不满一格仍给一格、度量或可视尺寸不可用则行列 0），这就是 `SPEC.FEAT.XFER.01` 一直缺的「尺寸来源算式」，widget 侧取值与去抖仍待接线；除法容差 `1e-9` 经实测必需——7 px 在 1.5× 下是无限二进制小数，可视宽由同一次除法反乘回来时比值差出 1e-15 量级，无容差就少算一整列一整行。`layout_row(grid::Row, PaletteSpec) -> vector<StyleRun>` 按**样式全等**（前景 / 背景 / 字重三者）合并到行，于是色带矩形与文本片段共用同一批切分边界、区间表只维护一张；双宽延续格不进文本但留在列区间内（字形才占得满两格宽）；combining 取 `grid::Row` 侧表**拼在基础码点之后随同一段文本**送出（裁决 7.23ⓑ，叠字与零推进由框架 shaping 承担）；文本按 `TextRun.text` 的口径在本层就编成 UTF-8，绘制侧不再二次编码。文本、装饰、色带三者皆无的区间整段丢弃（全空白文本、无下划线与删除线、底色等于主题默认底色），一屏空格的常见形态就此收敛成零条 run。
- **测试**：`tests/unit/utest_palette.cpp` 11 例（三段索引式子、色值按来源解析、bold-is-bright 的四类不适用形态、暗淡与反色次序、不可见保留底色、WCAG 三组对照值、最小对比度的「达标即止」与不可达时取较强一侧及其 alpha 不变、开关门控）；`tests/unit/utest_cell_layout.cpp` 14 例（scale 换算、整格计数与至少一格、非整除缩放的取整容差、不可用度量与最小化窗口、矩形映射、空白行不产段、同样式合并、切分边界共用、字重分裂、双宽延续格不进文本、combining 并字、不可见段丢弃而底色保留、裸下划线的空格段仍需交绘制侧、纯色带段）。两个新套件与既有用例一并入 CTest：非 e2e 的 15 项全绿（MSVC + Ninja）；e2e 两项里 `etest_local_terminal` 绿，`etest_osc_clipboard` 在本机两次（含投放到交互桌面会话）都停在 `OpenClipboard` 返 5——同机 PowerShell 读剪贴板同样报错，属系统级占用而非代码回归（e2e 的会话要求见裁决 7.19⑤）。
- **代价与边界**：① 本批**不含任何绘制代码**，`src/ui/terminal_view.cpp` 与帧唤醒在下一棒，故渲染层在屏幕上仍不可见；② `include/borealis/ui/` 两个头刻意不含 Aurora 类型，`CellPixels` 与框架 `render::CellMetrics`、`RgbaColor` 与 `au::Color` 的互转点全留绘制 TU，本层用 `int32_t` 与 `double` 是刻意的口径隔离；③ 颜色合成的三处数字（暗淡一半、最小对比度 256 档插值、256 色的式子）属本仓自定的实现细节——`SPEC.FEAT.RENDER.03` 只要求「可配开关」，调整它们必须同时动测试；④ `StyleRun` 是每帧每行的临时结构，切分成本随行宽线性，颜色频繁交替的行会把 run 数推到格数量级，这是裁决 7.23② 已认下的代价，优化面在切分而非框架原语；⑤ `SPEC.FEAT.RENDER.04` 光标三形态所需的 DECSCUSR（`CSI SP q`）在状态机侧仍未实现，是绘制棒之前的单独一笔。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是裁决 7.23②④ 的第一块落地，`SPEC.FEAT.RENDER.01` 与 `SPEC.FEAT.RENDER.03` 的语义未改，只把「色带与片段共用切分边界」从裁决文字变成可执行实现。

---

## v0.19（2026-10-01）上屏层的取用形态拍板：视口自管、run 按样式合并、吞吐挂门禁（裁决 7.23）

**动机**：G1 三腿闭合后 `SPEC.FEAT.RENDER.01` 不再有框架阻塞，渲染层开工前须先把形态定死；而实测 Aurora 头文件时撞到四处「需求与架构都没写、但会决定正确性」的现实——① 框架局部帧只把裁剪盒压进 `Painter` 裁剪栈，`on_paint` 收到的仍是**全量 bounds**，于是「单帧只重绘变更单元格」只能由绘制侧自行过滤，不是框架替我们裁好；② `monospace_cell` 给物理像素而 `TextRun.box` 原点收逻辑 dp（裁决 7.22②的刻意例外），两者混用即整屏偏移一个 scale 倍数；③ 跨线程唤醒主线程的公开入口是 `Window::surface().request_wake()`，而架构 §3.2 原写的 `post_to_main` 实测只存在于 `aurora::detail` 命名空间；④ 框架 `Scroll` 容器不能表达终端视口（无公开视口高度 getter、无惯性、整屏 blit 与「行级脏 + 后台权威网格」正交），§9.5 的待定项就此必须拍板。四处一律以裁决与实测口径回写，不留到代码里各凭理解。

- **新增裁决 7.23**（四条 + 三处实测口径）：① 视口与 scrollback **自管**，用 `Widget` 内置的 `ScrollViewport` 内核，通用件按「去掉终端字样后仍成立」的判据经附录 A.3 反哺 Aurora；② run 切分粒度＝**按样式全等合并到行**，且色带矩形与文本片段共用同一批切分边界（双宽延续格是空串，不产片段也不画豆腐块）；③ **渲染吞吐基准挂时间门禁**（`SPEC.NF.PERF.02` 原文维持，劣化 >10% 即 FAIL），并写明它与裁决 7.22③ 的改判不冲突——两处锁的成本主体不同；④ **第一棒＝`SPEC.FEAT.RENDER.01` + `SPEC.FEAT.RENDER.03` + `SPEC.FEAT.RENDER.04` 主路径**，`RENDER.02` 字体族与缩放重建随第二棒。实测口径三条记在 ② 之后：局部帧裁剪盒读取方式、combining 随同 run 文本并字（框架 shaping 负责叠字，`Painter` 无逐字形入口）、唤醒只走公共 `request_wake` 不依赖 `detail` 符号。
- **文档连带回写**（本批无代码改动）：`ARCHITECTURE.md` §9.2 补「取用形态细则」（px↔dp 换算式、一个参考 `Font` 取一次度量的纪律、一帧的层叠顺序、`src/ui/terminal_view.cpp` 作为全仓唯一触达 `au::Painter` / `au::Widget` 的翻译单元、`cell_layout` 与 `palette` 保持无框架依赖以便全量单测）；§9.5 由**待定**改为拍板形态并写明反哺通道；§16 移除 B 行（该表要求「每项拍板后写入正文并从本表移除」）；§3.2 的唤醒口径按实测改写。`PLAN.md` §2 新增「M1 渲染腿的棒内序」段，§3 的 `SPEC.FEAT.RENDER.01–05` 五行标注棒次。
- **验收映射**（随第一棒落地，本批只定判据形态）：脏行重绘用 `HeadlessSurface` 两帧全像素差分，断言差异行集 ⊆ 队列提交行集；属性合成走 `palette` 纯函数单测加一条像素用例；光标的三形态与闪烁按像素位置断言，闪烁由框架既有的 `Widget::tick(now)` 驱动而非新起线程；双宽与 combining 复用 `tests/fixtures/` 回放夹具并把「列位与光标一致」从逻辑层延伸到像素层。
- **代价与边界**：① 自管视口意味着惯性滚动与滑动手感要应用侧自写，换来的是行级脏与后台权威网格两条前提不被容器截断；② 「色带与片段共用切分边界」在颜色频繁交替的行里会把 run 数推到格数量级，届时优化面在切分而非框架原语；③ 吞吐门禁的基线由首批实测固定，跨机性能差异须以同机回归为准（门禁锁相对劣化，不锁绝对值）；④ `RENDER.02` 后移使第一棒的汉字显示依赖系统等宽字体的缺字回退，若目标机上取不到 CJK 面则表现为豆腐块——这正是它单列第二棒的原因。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次为「待定项拍板 + 实测口径回写」，`SPEC.FEAT.RENDER.01–04` 的语义未改，只补执行形态。

---

## v0.18（2026-10-01）G1 绘制两腿在框架侧闭合：批量文本片段与等宽整格度量（裁决 7.22）

**动机**：附录 A.2 G1 列的三条腿里，宽度判定腿已随裁决 7.20 闭合，剩下两条都卡在同一处——`Painter::draw_text` 的三个重载全是**字符串粒度**，终端一帧要按样式切成几十到几百个片段，只能逐段调用；而等宽网格需要的「第 k 列/行的整像素起点」框架只给浮点自然度量，应用侧自行折算就落在非整数 scale 上逐格各自取整，列位随列号漂移。按裁决 7.13 类别 ①（影响渲染链路与公共 API 形态的缺口）本仓不等不绕，两腿须在 Aurora 侧以公共 API 落地。落地过程定死四条形态与判据选择，其中「验收判据」一条被实测结果推翻，故回写成裁决 7.22 而不是照原承诺交差。

- **新增裁决 7.22**（四条）：① 批量原语落**在 run 层**（片段 = 同属性文本 + 区域 + `Font` + `Color`），不暴露 `ShapedGlyph`、不含 cell / 终端语义，网格模型与脏行 diff 仍在本仓；② 整格度量**以函数形态进框架、排布留应用侧**，刻意**不给 `TextLayoutOpts` 增设「整格模式」**——那会把终端排版语义塞进通用文本层；③ **验收判据由时间门禁改判为逐位一致**（实测收益落在环境抖动内，无稳定阈值可锁红灯，见下）；④ 本仓**不因本批改动任何代码**，两原语随 `SPEC.FEAT.RENDER.01` 上屏层落地时直接消费，架构 §9.2 的「应用侧自绘 cell 网格」过渡备路径就此不再启用。
- **框架侧（跨仓，Aurora 分支 `dev-1.0.0-alpha.9.uat.2` 提交 `ec71a3aa` 功能、`182fb11e` 测试、`ad8142d9` 基准、`64afbfa4` 文档，均在本地未推送）**：新增 `Painter::draw_text_runs(std::span<const render::TextRun>)` 与 `render::FontEngine::monospace_cell(Font, scale) -> render::CellMetrics{cell_width_px, cell_height_px, ascent_px}`，契约文档写进 Aurora 主仓 `codespec/specification/03-layout-render.md` §8.1 与 §8.2，`CHANGELOG.md` 记入 alpha.9 的 Added。取整口径与实绘同源：格宽取参考字形集 `{'0', U+2500}` 的整像素 hinted advance 最大值（含制表符是因为它在部分字体里比数字宽，只量数字会让边框压进相邻格），行高与基线取绘制侧同一次 `line_height_px` / `ascender_px` 的 snap 值。批量化省的是每次调用的字体面解析（`family#weight` 堆键）、像素尺寸与行高度量（各含一次 `FT_Set_Pixel_Sizes`），落笔算式一字未动，故输出与逐片段调用**逐位相同**；录制态逐片段各落一条 `DrawText` 命令，三个 RHI 回放后端与 `CmdKind` 零改动。
- **测试**（均在 Aurora 侧）：`tests/unit/utest_painter.cpp` 新增 3 例，判据是「整批一次」与「逐片段各一次」两张画布的**全像素差分为 0**（其中一例第 3 片段换字重，覆盖相邻片段 `Font` 变化时重建派生上下文的分支），另覆盖空数组无操作；`tests/unit/utest_font_engine.cpp` 新增 4 例（三值恒正且随字号单调、ascent/height 与绘制侧落笔基线逐位一致、格宽不小于参考字形集内每个字形的整像素 advance、按 `x = col * cell_width_px` 摆放 `U+2500` 串时最左墨迹列零漂移）。`tools/bench/bench_render.cpp` 新增一屏规模场景（24 行 × 12 同属性短片段 = 288 片段，9 组「逻辑尺寸 × scale」两轮）作观测。Aurora 全量 `ctest` 333/333 绿（含 17 道 `check_*` 门禁：核心层边界、API 预算与 schema 同步、代码 / 文档同步、字面量语言、命名与版本一致性等），`format-check` 892 文件全合规，`lint` 537 TU 仅剩 4 条本批未触碰文件的存量 findings（`include/aurora/widget/provider.h`、`src/aurora/inspector/inspector_server.cpp`、`tests/unit/utest_inspector_server.cpp`，非本批引入）。
- **代价与边界**：① **原承诺的吞吐门禁未兑现**——附录 A.3 反哺清单原文要求「网格吞吐基准挂性能回归门禁（劣化 >10% 即 FAIL）」，实测批量入口相对逐片段为中位约 −1%、区间 −8.5%…+8%，因该场景成本主体是字形 blit 与图集查找而非被省下的固定开销，无稳定阈值可锁，故基准两行降级为观测项、改由像素级用例守门（此即裁决 7.22③）；② `FontEngine::draw_text_runs` 的片段原点收**逻辑 dp**，与该引擎其余绘制入口收预缩放物理像素的约定刻意不同，理由与代价写在 Aurora 头文件与 §8.2，本仓消费时**不要再自行乘 scale**；③ 本批只闭合绘制两腿，G1 的宽度判定腿虽已闭合，零宽标记**仍只存不绘**，combining 上屏须随 `SPEC.FEAT.RENDER.01` 取 `grid::Row` 侧表合成字形；④ 字重切换带来的 1px advance 差要求同一网格固定用一个参考 `Font` 取一次度量，这属应用侧排布纪律，框架不代持。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次是「框架缺口闭合 + 原验收判据实测改判」，`SPEC.FEAT.RENDER.01` 与 `SPEC.FEAT.RENDER.05` 的语义未改，只把附录 A.1 两行、A.2 G1 行改写为已关闭，并把架构 §9.2 从过渡备路径改述为「就绪后的取用形态」。

---

## v0.17（2026-09-30）OSC 由整体吞掉改为结构化消费，`OSC 52` 写方向真落系统剪贴板（裁决 7.21）

**动机**：`SPEC.FEAT.TERM.07` 的消费腿此前是状态机里的一句 `TODO(SPEC.FEAT.TERM.07)`——OSC 串按协议合法地不进气，但既不产生产物也不留痕迹，「标题没生效」这类排障无从下手。本批把 `0/2` 标题、`7` 工作目录、`8` 超链接、`52` 剪贴板、`133` 命令块一并改成消费成结构化产物；其中 `7` / `133` 本属 `SPEC.FEAT.INTEG.01/02`（P2/M4），按用户裁决只作**来源预埋**，`52` 按裁决真写系统剪贴板，并把整条腿在 PLAN.md 标为提前落地。形态上的关键约束来自架构 §3.4：状态机跑在会话读线程的网格锁内，锁内只能留存不能投递，而剪贴板是主线程的 IO。

- **新增裁决 7.21**（五条）：① 产物形态是**状态快照 + 一个取走型动作**而非事件队列；② 超链接不进 `Cell` 主结构，走 `grid::Row` 侧表 + 有界链接表（`kMaxHyperlinks = 1024`，标识递增且永不复用，满表淘汰最旧，后果限定为「不可点」而非指向另一条 URL）；③ `OSC 52` 写方向按「锁内留存 → `Session::take_clipboard_write()` 合并取走 → 主线程 `ClipboardOutbox::drain()` 转调 Aurora `Clipboard::set_text`」落地，读方向 `52;c;?` 默认禁止但**回写空响应**；④ 提前落地范围与其「来源预埋」边界；⑤ 未消费命令号计入 `unhandled_count`，不得整体吞掉不留痕。
- **本仓代码落地**：`include/borealis/term/osc.h`（`OscState`、`PromptMarker`、`kMaxHyperlinks`）、`src/term/osc.{h,cpp}`（OSC 字段切分与**严格** base64 解码——字母表外字符、截断、填充后载荷一律判非法，解出的字节再走一遍 `Utf8Decoder`）、`grid::Row` 的「列号 → `HyperlinkId`」侧表（`set` 即清链、`reset`/截断/整行搬移随行走）、`Terminal::do_osc` 的命令号派发与 `osc_state()` / `hyperlink_target()` / `take_clipboard_write()`、`Session` 的三处加锁访问器、`include/borealis/session/clipboard_outbox.h` + `src/session/clipboard_outbox.cpp`（全仓唯一触达 `aurora::Clipboard` 的翻译单元，公共头不含框架头）。`TODO(SPEC.FEAT.TERM.07)` 就此消除；`TODO(SPEC.FEAT.INTEG.01/02)`、`TODO(SPEC.FEAT.CONN.12)` 留在各自消费点。
- **测试**：新增 `tests/unit/utest_terminal_osc.cpp`（19 例：`0/1/2` 标题、`7` 原文留存、`8` 区间括出与 params 段切分、覆盖/擦除清链、SGR 不截断区间、双宽只挂前半格、有界表淘汰后旧标识解析不出而历史格仍挂该标识、`52` 解码/合并/空载荷合法/三类非法载荷、`52;c;?` 的空应答、`133` 各边界与退出码、未识别命令号计数后链路照常、RIS 清零全部产物）；`tests/unit/utest_grid_storage.cpp` 补侧表 5 例；`tests/unit/utest_session.cpp` 补 4 例（含「字节 → 解码 → 解析 → 状态机 → 网格侧表 → 会话取值」的整链断言与多次写的合并语义）；`tests/integration/itest_terminal_scene.cpp` 的 OSC 断言从「标题串被吞」改为「不进网格且消费成状态」；新增 `tests/e2e/etest_osc_clipboard.cpp` 用真机 ConPTY 让 powershell 发出 `OSC 52`，回读系统剪贴板校验明文（断言素材是 base64，明文不经屏幕），并校验「取走即清空」与用例结束归还用户既有剪贴板内容——须在有窗口站/桌面的交互会话投放（裁决 7.19⑤）。CTest 共 15 项全绿（MSVC + Ninja）。
- **代价与边界**：① 快照只留**最近一次**，`133` 的完整命令块区间（起止附着到网格行）须由 `SPEC.FEAT.INTEG.01` 落地时补结构，`7` 的目录继承与远端降级语义同归 `SPEC.FEAT.INTEG.02`；② 剪贴板只取最终值，「多次写各自生效」的追加型用法不在本档覆盖内；③ `OSC 52` 的三态授权（`SPEC.FEAT.CONN.12`）与 `OSC 4/10/110` 调色板族仍只计数不消费；④ 主线程 `drain()` 的调用点随帧调度接线（`TODO(SPEC.FEAT.RENDER.01)`），当前由消费方按帧轮询；⑤ 剪贴板是用户共享状态，e2e 用例先读回、结束原样归还。
- 需求条目数量（66 条）、标识体系与优先级/阶段的**结构**均未变动；本次按裁决 7.21④ 只在 PLAN.md §3 的「阶段」列加子句级提前落地标注（`TERM.07`、`CONN.12`、`INTEG.01/02` 四行），并在 §8 现状表新增「OSC 消费 → 会话产物」一行。

---

## v0.16（2026-09-30）码点宽度判定由框架交付，双宽占位与 combining 一并接线（裁决 7.20）

**动机**：`SPEC.FEAT.TERM.08` 的宽度腿此前只到「机制可用」——状态机经注入接缝取格数，但生产侧挂的是「一律单宽」的缺省实现，CJK 双宽占位不生效，combining 更是无处可去。按裁决 7.13 类别 ①（影响渲染链路的框架缺口）**本仓不等不绕**，判定表须先在 Aurora 侧以公共 API 落地；而落地时实测出一个规格书没写到的分叉：Ambiguous 区间**包含**组合区段（U+0300–U+036F 一类），于是「先判宽度还是先判零宽」成了会决定正确性的选择，连同零宽码点在网格里怎么存一起回写成裁决 7.20。

- **框架侧（跨仓，Aurora 分支 `dev-1.0.0-alpha.9.uat.2` 提交 `9efe3968`，未推送）**：新增 `aurora/core/unicode_width.h` 的 `unicode_cell_width(char32_t, AmbiguousWidthMode) -> 0 / 1 / 2`，一个原语同时给出零宽（General_Category Mn/Me/Cf）与 East Asian Width（W/F 双宽）两判据，内部固定「先零宽、再宽度」的次序，Ambiguous 口径以入参给出（裁决 7.15 的要求）。数据由 UCD 18.0.0 实测导出为零宽 379 段 / Wide+Fullwidth 126 段 / Ambiguous 179 段的 constexpr 区间表，**不把上游数据文件复制进仓**，许可声明记入 Aurora `THIRD_PARTY_LICENSES.md` 第 9 节，能力面写入 `codespec/specification/01-core.md` §7.3，单测 `tests/unit/utest_unicode_width.cpp`（含全码点扫描：结果恒 ≤2 且 Wide 口径不小于 Narrow 口径）。
- **本仓代码落地**：`term::UnicodeWidthPolicy`（声明在 `include/borealis/term/width.h`、实现在 `src/term/width.cpp`，因本仓公共头不含 Aurora 头）替换生产侧的单宽缺省；`grid::Row` 新增「基础格列号 → 零宽标记序列」侧表（`CombiningMark`、`attach_combining` / `combining` / `clear_combining`、单格上限 `kMaxCombiningMarksPerCell = 8`），落实架构 §4.1 早就写明却无载体的低频属性外置；状态机 `do_print` 在宽度为 0 时不写格、不推进光标、不触发换行，把码点并入光标左侧最近的基础格（跳过双宽延续格挂到前半格）。`SingleWidthPolicy` 退为测试用常数注入值。
- **测试**：新增 `tests/unit/utest_width_policy.cpp`（口径类断言：W/F 恒双宽、Ambiguous 随口径 1/2、零宽先于宽度、中性单宽）与 `tests/integration/itest_unicode_width.cpp`（全链路字节流跑 `SPEC.FEAT.TERM.08` 的验收线：CJK 双宽占位与延续格、**同一份**含 Ambiguous 的输出编一次字节喂两台终端、行末放不下的双宽字符整体换行不留半格、combining 并入/不推进光标/落双宽前半格/行首丢弃/行末不触发换行/覆盖即清除）；`tests/unit/utest_grid_storage.cpp` 补侧表六例（并入序、脏标记、只清本列、上限、截断与复位、随整行搬移）；`tests/integration/itest_terminal_scene.cpp` 改挂 `UnicodeWidthPolicy` 并把「单宽缺省」的旧注释改写为映射前判定口径的说明；字节→状态机的接线从场景用例下沉到 `tests/support/terminal_feed.h` 以免两处漂移。CTest 共 13 项全绿（MSVC + Ninja）。
- **代价与边界**：① 判定表版本随框架升级，本仓不持表也不复制数据，故本仓宽度用例只断言「口径」不断言具体码点归属，UCD 变更由 Aurora 侧承担；② 零宽标记**只存不绘**——上屏层落地（`SPEC.FEAT.RENDER.01`）时须按 §4.1 取侧表合成字形，否则 combining 在屏幕上仍不可见；③ 上限 8 之外的标记静默丢弃，与「不可信输入须有界」相比损失的是极端字素簇的保真度；④ 字素簇切分与 Emoji 呈现仍不属本条（`SPEC.FEAT.TERM.08` 的延后观察项）。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次为「框架缺口闭合 + 需求空洞补裁决」，`SPEC.FEAT.TERM.08` 的语义未改，附录 A.2 G1 的「影响需求」列补登该条（此前是推断，现为实测）。

---

## v0.15（2026-09-30）本地终端连接在 Win32 侧落地，会话首次由真实 PTY 驱动（裁决 7.19）

**动机**：会话层之上六层纯逻辑都由测试替身驱动，`SPEC.FEAT.CONN.01` 的 Windows 腿（ConPTY）是 M1 出口判据「替换 Windows Terminal」的唯一硬前置。落地过程撞到四处 API 语义空白——宿主标准句柄的传递、伪终端管道端的归属、`ClosePseudoConsole` 与子进程的关系、环境块的合成与排序——任一处漏掉都表现为「进程在跑但屏幕空白」或「关标签卡死」，且都不在需求与架构文档里，故先实现后实测再回写成裁决。

- **新增裁决 7.19**：①②③④ 即上述四处口径（①`STARTF_USESTDHANDLES` + 三句柄置空；②交给伪终端的两端在子进程建好后关掉本进程那份，否则输出管道永不 EOF、读线程回收不了；③关标签即补 `TerminateProcess`；④环境按「继承 → PTY 默认 → profile 覆盖」合成并排序），另附两条实测事实（宽字符环境块必须带 `CREATE_UNICODE_ENVIRONMENT`，否则任何非空 `lpEnvironment` 都返回 87；`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 的 `lpValue` 传 HPCON 本身而非其地址，传地址子进程以 `0xC0000142` 失败），并给出 ⑤「e2e 用例只能在能访问窗口站/桌面的交互会话中运行」这条运行前提。
- **代码落地**（全部在 `src/platform/win/`，Win32 类型不外泄，裁决 7.11）：`conn::LocalTerminalSpec` 与 `make_local_terminal_connection` 工厂（`include/borealis/conn/local_terminal.h`，本仓第一个 `conn` 域公共头）、`platform::ConptyConnection`（`CreatePseudoConsole` + 属性表挂子进程 + 单读线程 + 有界关停）、`default_shell_command_line()` 的 Windows 探测链（PowerShell → cmd → WSL，对应 `SPEC.FEAT.CONN.01`）、UTF-8 ↔ UTF-16 转换工具 `win_text`；`src/CMakeLists.txt` 以 `if (WIN32)` 增补平台源。
- **测试**：新增 `tests/e2e/etest_local_terminal.cpp`（本仓首个 e2e，8 个用例）以真实 ConPTY 驱动全链路，断言落在权威网格而非原始字节：自定义命令输出上屏、`TERM`/`COLORTERM` 注入与 profile 覆盖、启动目录、键入往返 + 尺寸变更后流不中断、3000 行突发下队列水位不越界且末行可达、关闭后进程不在且内容保留（`SPEC.FEAT.CONN.01`、`SPEC.FEAT.XFER.01` 会话启动腿、`SPEC.NF.PERF.06` 真机腿）。CTest 共 11 项全绿。
- **代价与边界**：③ 的「关标签即终结」意味着后台跑长任务再关标签会丢进程，与 Windows Terminal 同口径但须随 `SPEC.FEAT.WS.01` 的关闭前确认一起在 UI 侧提示；⑤ 使本仓 e2e 无法在无控制台的受限环境里跑（该环境下的失败不是实现有误）；默认 shell 探测链、POSIX 侧的 `$SHELL` 腿与 SSH 族连接仍未开工。真机走查「替换 Windows Terminal 日常使用」与 `vim`/`tmux` 的 TUI 验收仍待渲染层。

---

## v0.14（2026-09-30）会话抽象粒度拍板，输出背压与查询回写落口径（裁决 7.18）

**动机**：状态机之后要接「会话回写」这一棒，撞上文档里三处空白——架构 §16 A 的会话抽象粒度仍列三案待拍板、`SPEC.NF.PERF.06` 只说「合并而非丢弃」却没说队列条目是什么粒度、`SPEC.FEAT.TERM.01` 要求 DA1/DSR「正确响应」却没定应答该报哪些能力号。三者都会决定公共接口形态与可观测行为，不该由实现自行择一，故先由用户裁决粒度、再落代码。

- **新增裁决 7.18**：① 会话接口取**基础接口 + 能力接口组合**（`Connection` 五方法 + `ConnectionEvents` 反向通道，SSH 独有面到落期以独立能力接口增补）；② 背压队列的条目是**视口行区间提交**而非字节/cell 值，队列满按**并宽区间**做到合并而非丢弃，且**行脏标记由消费侧清除**（写侧只登记，否则列级增量取不到、`SPEC.FEAT.RENDER.01` 的单帧只重绘变更单元格落空）；③ 查询应答**只报能力档位 62**，不报 132 列 / sixel / ReGIS / 打印机附加能力号，DA2 与 `CSI ? 6 n` 按「宁可不答也不答一份错格式」忽略。
- **架构文档连带回写**：§7.2 由「待定」改为拍板形态并把 §16 的 A 行移除（该表要求「每项拍板后写入正文并从本表移除」）；§3.4 补会话层持锁口径（解码 + `feed` 同处临界区、出锁后才 IO 与入队、应答锁内登记锁外回写）；§3.6 补提交粒度、合并规则、锁序「先队列后网格」与默认预算；§5.2 补 DSR/DA1 的应答取值与不报附加能力号的理由。
- **代码落地**：`include/borealis/session/{connection,damage_queue,session}.h` 与 `src/session/{damage_queue,session}.cpp`（编入 `borealis_core`）、`term::ResponseSink` 接缝与 `Terminal::set_response_sink`、`Session::resize` 串通存储层与连接侧下发；`TODO(SPEC.FEAT.TERM.01)` 的两处查询响应占位据此消除，OSC 消费仍留 `TODO(SPEC.FEAT.TERM.07)`，帧调度通知留 `TODO(SPEC.FEAT.RENDER.01)`（平台层 ConPTY 与上屏层是下一棒）。
- 需求条目数量（66 条）与标识体系未变动；本次为「空洞补裁决 + 待定项拍板」，不改任何既有需求语义。真机验收（`vim`/`tmux` 的 DA1/DSR 往返、`yes` 类无限输出的背压表现）须待 ConPTY 落地，当前由 `Connection` 测试替身在单测中驱动。

---

## v0.13（2026-09-30）窗口行数变化口径落定（裁决 7.17）

**动机**：终端状态机落地后要给 PTY 尺寸同步（`SPEC.FEAT.XFER.01`）接线，才发现规格书只裁决了**列宽**变化不 reflow（7.5），而「视口行数增减时已有行与 scrollback 历史怎么处理」在文档里是空的，代码侧只留 `TODO(SPEC.FEAT.XFER.01)`。三种取向（从历史收回 / 直接丢弃 / 固定总容量）对「能回滚多少行」的承诺差别很大，不该由实现自行择一，故先落裁决再落代码。

- **新增裁决 7.17**：行数变化按**移动窗口边界、底部锚定**处理——变高从 scrollback 顶部收回历史填满，变矮时顶行溢出进历史，超出容量从最旧端丢弃，历史不足时顶部补空白；并明确 **scrollback 的配置容量不随视口变高而缩水**，代价（变高必然扩容重分配、须靠 XFER.01 去抖压频）写入该条。
- **`SPEC.FEAT.TERM.04` 就地补指针**：原句只指向裁决 7.5（列宽），现同时指向 7.17，免得「默认 10,000 行」被读成随窗口高度浮动的值。需求语义未变，仅补交叉引用。
- **`ARCHITECTURE.md` §4.5 连带**：补行数变化的窗口边界语义、环拉直的原因（容量模数一变，跨旧边界的行号会错位），并点明尺寸变化属整屏位移级事件、一律以整屏脏通知副本重建（§3.4），以及终端模式随尺寸收敛的三条（DECSTBM 带复位、制表位按新列宽重建、光标与 DECSC 保存位置一并钳制）。
- **§7 标题的追加区间**改为 7.16–7.17。
- 需求条目数量（66 条）、标识体系、优先级与分期结构均未变动；本次为需求空洞补裁决，不改任何既有需求语义。

---

## v0.12（2026-09-30）审查整改：引用形态、口径矛盾与架构落点补全

**动机**：审查报告剩余项全量整改。本批**不改动任何需求语义**，优先级与阶段主体不变（PLAN.md 仅做子句级拆分标注），内容为引用形态修正、文档间口径矛盾消除与已排期需求的架构落点补全。

- **引用形态与事实口径修正**：§3.3 第 4 条「Aurora 规则 §5.8」改为「Aurora 主仓 `AGENTS.md` §5 规则 8」（该规则实测存在——「禁止直接触达标准输出」，原引用未指明文档名且 `§5.8` 形态易误读为不存在的子章节号）；`SPEC.NF.A11Y.01` 与 `PLAN.md` 观察池对 Aurora `ACCESSIBILITY_DESIGN` 草案的引用改为已实测存在的 Aurora `codespec/ARCHITECTURE.md` §8.5（Aurora 自身规则禁引 draft 文档）；附录 A.2 G2 的「五后端」补明确口径（win32 / x11 / wayland / glfw / macos，headless 经同一 dispatcher 路径用于单测）；§4.1 `SPEC.FEAT.TERM.06`「less/less」笔误改「less/more」；§7 标题的裁决基线日期按 CHANGELOG 各版本实际落定日期拆分（7.1–7.4 于 2026-09-22、7.5–7.10 于 2026-09-23）。
- **`ARCHITECTURE.draft.md` 口径矛盾消除**：§1.3 命名空间清单补 `borealis::platform`（与 §2.1 一致，标注纯实现层无公共头）；§4.4 删除「alt screen 独立 scrollback」的自相矛盾表述，改为「备屏不与主屏 scrollback 混排、备屏本身无回滚（xterm 惯例，滚轮由 alternate scroll 承接）」。
- **架构落点补全**（此前一批已排期需求在架构文档无归属）：§3.1 补单实例转交通道指针；§3.4 补「主线程读取 cell 内容方式未定」指针；§9.2 过渡自绘标注适用条件（M0 完成前应用侧不开工，本节为 G1 延期时的预研备路径）；新增 §7.5「SSH 之上的子系统归属」（SFTP / 隧道 / 公钥推送 / 档案 / 凭据 / 连接管理器 UI / 会话日志的模块归属；系统通知依赖 G11 属观察池）与 §8.4「其余工作区能力归属」（多窗口 / 全屏 / pane 缩放 / 广播输入）；§10.3 搜索补持锁口径指针；新增 §10.6「无障碍（延后项的落点约束）」（自绘网格的语义树钩子须应用侧实现）；§7.2 会话抽象增列第三案「基础接口 + 能力接口组合」；§16 待定清单增列 D（网格所有权与主线程读取方式，三案：持锁遍历 / 每帧快照 / 双副本）、E（单实例转交通道：命名管道 / 本地 socket / `WM_COPYDATA`）、F（schema 版本升级策略），A 案候选补第三案。
- **`PLAN.md` 分期标注修正**（子句拆分，优先级与阶段主体不变）：`SPEC.FEAT.WS.05` 原整条落 M2，但其「SSH 断线自动重连」子句依赖 M3 的 SSH 本体（M3 出口判据本就含「重连」）→ 标注为「M2（本地终端腿）+ M3（SSH 断线重连腿）」，M3 涉及需求同步补该腿；`SPEC.FEAT.INTERACT.03`（P0 / M1，含「选区复制」）依赖文本选择基本面，而 `SPEC.FEAT.INTERACT.02` 原整条落 M2 → 流式拖拽 / 矩形块基本面（不依赖框架缺口 G2，见 `ARCHITECTURE.draft.md` §9.6）前移 M1，双击 / 三击等其余仍留 M2（拆分模式沿用 `SPEC.NF.PLAT.01`「M1（Windows）+ M2（Linux）」先例）。
- **仓库卫生**：`.gitignore` 补 `/.workbuddy/`（AI 协作本地记录目录，此前 untracked 且未被忽略）。
- 需求条目数量（66 条）、标识体系、优先级与阶段主体均未变动；本批为引用 / 口径 / 落点修正，不涉及需求语义。

---

## v0.11（2026-09-30）中文输入与显示链路补全（裁决 7.16）

**动机**：规格书审查发现中文场景有三处需求空洞——此前只覆盖「显示宽度」（`SPEC.FEAT.TERM.08`），漏掉「输入」与「字形可得性」两个维度；而首要用户为中文开发者、串口默认编码为 GB18030（裁决 7.6），三者均属日常刚需而非增量特性。框架侧能力经实测均已具备，故本次为**需求侧补全，不新增 G 编号**。

- **新增 `SPEC.FEAT.INTERACT.06` 输入法（IME）**：preedit 就地渲染于光标单元格、候选窗按光标屏幕坐标定位、组合中间态不发往会话、commit 文本经会话编码写入。按 §1.4 第 1 条取 INTERACT 域最大序号 + 1（原为 05）。连带回填 §2.1 能力域表（终端交互改 `01–06` 并补「输入法」）与 §4.3 小节标题；附录 A.1 的 IME 行此前是唯一「箭头后无需求标识」的行（因无处可指），现指向本条。
- **`SPEC.FEAT.RENDER.02` 补 CJK 缺字回退链**：内置 Cascadia Code **不含汉字字形**，无回退则 `SPEC.FEAT.TERM.09` 的「GB18030 串口输出正确显示」验收必然呈现豆腐块。补默认回退至系统等宽 CJK 字体、回退链顺序可配、回退后仍按双宽占位而不破坏网格对齐，并补验收判据；§2.1 网格渲染行同步补「CJK 缺字回退」。
- **`SPEC.FEAT.TERM.09` 编码口径由单向改双向**：原文与 §1.3 术语表均只定义解码方向（字节流 → 文本），但 `SPEC.FEAT.CONN.05` 串口（默认 GB18030）需输入中文并发送行尾序列，用户输入必须**反向编码**为会话编码字节，否则设备侧收到乱码。现补发送方向（覆盖键入、IME commit、粘贴、片段发送）与「目标编码不可表示字符」的可配策略（默认替换 + 一次性提示，不静默发乱码），§1.3 术语表同步改为双向定义，验收判据补「设备侧收到合法 GB18030 序列」。
- **新增裁决 7.16** 记录上述三项的理由与代价；§7 标题的追加区间改为 7.11–7.16。
- **`PLAN.md` 连带**：`SPEC.FEAT.INTERACT.06` 赋 **P0 / M1**（首个交付阶段在本地终端输入中文即触发，不可延后），M1 出口判据补「中文可输入可显示」；M4 出口判据补「中文按 GB18030 编码发送」；§3 总表增行，计数由 65 条改为 **66 条**、P0 由 22 条改为 **23 条**。`SPEC.FEAT.RENDER.02` 与 `SPEC.FEAT.TERM.09` 的既有优先级与阶段不变（本次仅为其增补子句）。
- **`ARCHITECTURE.draft.md` 连带**：§3.5 输入路径补「文本 → 会话编码」环节与输入法路径；§6.1 补双向口径；§6.2 由「解码失败处理」改为「转换失败处理」并补编码方向策略；新增 §10.5 输入法（点明自绘网格下框架不会自动把 cell 内容暴露给输入法，且坐标换算须与 DPI 口径一致）；§11.1 补 CJK 缺字回退链。

---

## v0.10（2026-09-29）修正对框架 `TabBar` 能力的高估，映射表名称笔误

- **附录 A.1 新增「标签栏」行**：`TabBar`（`widget/tab_bar.h`）经实测只有 `on_change` / `on_close` 两个事件，属性为选中序号、栏高、四个**全局**配色、指示器粗细、字号与内边距；**无拖拽重排、无逐标签图标、无逐标签状态角标**。此前 A.1 未收录该控件，导致 `ARCHITECTURE.draft.md` §8.1 写下「框架 `tab_bar` 可直接承载标签栏」这一未经实测的乐观结论——与 `SPEC.FEAT.WS.02` 对 `Splitter` 的告警（不得因框架已有控件而低估工作量）属同一类错误，故按同一口径补录能力边界。
- **连带修正 `ARCHITECTURE.draft.md` §8.1**：改为「`TabBar` 仅覆盖选中切换与关闭」，并点明 `SPEC.FEAT.WS.01` 的拖拽重排与图标、`SPEC.FEAT.WS.04` 的逐标签状态角标须应用侧自研。
- **映射表 `#44` 名称修正**：原写「近乎零成本」，应为「命令面板」（`SPEC.FEAT.WS.07`）。已用 v0.6 提交原文核对——其 §2.1 工作区行为 `#18–#23 #44–#49`，对应 `SPEC.FEAT.WS.01–12`，故 `#44` 确为 WS.07；「近乎零成本」是附录 A.1 中描述 WS.07 复用成本的措辞串入了名称列。「历史条目原文照录」原则约束的是叙述性版本条目，映射表为 v0.7 生成的工具表，属笔误可修。
- 需求条目数量（65 条）、标识体系、裁决结论与分期结构均未变动；本次为事实修正与笔误修正，不涉及需求语义。

---

## v0.9（2026-09-29）工作区目录改名落地，命名例外条款删除

- 由人在 IDE 会话外把工作区目录改名为 `borealis`，裁决 7.14 设定的「目录名为唯一例外，改名后须同步回填」条件就此达成，按该条自身要求删除例外表述。
- **裁决 7.14 就地修订**：编号保留（撤销会让其他处的 `7.14` 引用变成伪死链），正文改为「目录与文档自称均不再出现旧名」并说明例外条款已失效及原因。
- **裁决 7.1 括注收回**：7.1 曾以括注形式指向 7.14 的例外，现只述产品名本身。
- **旧名指涉清零**：本文开头括注、7.1、7.14 与 `AGENTS.md` §1 命名行的四处已清理；全仓（含 `PLAN.md`）grep 确认无残留。
- 需求条目数量（65 条）、标识体系与分期结构均未变动；本次为纯文档口径回填，不涉及需求语义。

---

## v0.8（2026-09-29）Ambiguous 宽度默认值落定

- **新增裁决 7.15**：East Asian Width 的 Ambiguous 类字符**默认按单宽（窄）**，并提供 **profile 级覆盖为双宽**。理由与场景分工见该条——UTF-8 本机/SSH 场景取单宽与主流终端一致，GB18030/GBK 串口等少数场景以覆盖项解决，故不把默认值翻到对多数场景错误的一侧。
- **SPEC.FEAT.TERM.08 就地修订**：写入上述默认值与覆盖粒度（覆盖项与 SPEC.FEAT.TERM.09 的会话编码项协同），并补出验收判据（两种配置下光标列位与占位一致、不出现半格错位）。
- **附录 A.2 G1 连带约束**：框架宽度判定原语的**入参须含 Ambiguous 宽度模式**，不得在框架内硬编码单/双宽——否则应用侧只能绕开公共 API 自行查表，违反「渲染路径不私改」的约束。
- 标识体系、需求条目数量（65 条）与分期结构均未变动；本次为需求语义增量。

---

## v0.7（2026-09-29）需求标识改造与「纯需求」化

**动机**：两项结构性指令——① 规格书应为纯粹的需求描述，不得承载优先级（`P0`…）与任务分期（`M1`…），计划类内容另立文档；② 不得用纯数字作需求标识，因为插入新需求会导致整体编号偏移，交叉引用一改就是百余处。

**改动**：

- **标识体系落地**：新增 §1.4 需求标识规范，格式 `SPEC.<类别>.<域>.<NN>`（类别 `FEAT`/`NF`；FEAT 域 `TERM` `RENDER` `INTERACT` `XFER` `WS` `CONN` `PREF` `INTEG`，NF 域 `PERF` `PLAT` `RELI` `PKG` `A11Y`），并附 5 条稳定性规则：只追加不复用不重排、改域即换号且映射入本文件、引用一律写全、同域连续可写区间、更名不改号。
- **全量重编号**：65 条需求（`SPEC.FEAT.*` 55 条 + `SPEC.NF.*` 10 条）由 `#1`–`#65` 改为其域内语义标识，映射见下方「旧需求编号 → 新标识」表。重编号以脚本机械执行并对每条替换断言，正文与附录的交叉引用零残留（含 §2.1 覆盖表、§7 裁决、附录 A.1/A.2/A.3）。
- **优先级与分期移出正文**：新建 [`PLAN.md`](PLAN.md)，承接优先级定义、阶段划分与出口判据、需求优先级总表、延后子项清册、观察池、框架缺口补入排期、跨平台分期决策。
- **正文去标记**：约 30 处内联 `P0`–`P3`/`M0`–`M5` 措辞改写为语义表述（「延后子项」「延后观察项」「分期见 PLAN.md」），需求条目只保留「本项边界」不含「落期」；§6 由里程碑表改为指向 PLAN.md 的指针，章节号序列保持不变以免 §6 之后的锚点漂移。
- **裁决连带调整**：7.10 重定为「工作区下限」的需求侧表述；7.11 只保留「平台层自始接口隔离」这一需求约束，交付分期移入 PLAN.md §7；7.12 措辞去里程碑化。
- **表格压缩**：§2.1 覆盖表与 §4 各小节标题改用区间写法（如 `SPEC.FEAT.TERM.01–08`），并标注非功能需求共 10 条不入能力域表。
- **尾部收敛**：原「## 版本」小节整体迁入本文件，规格书尾部只留当前版本号与指向本文件的指针。

**影响**：纯结构性迁移，**无任何需求语义变更**——每条需求的验收判据、边界与裁决结论与 v0.6 一致。

---

## 旧需求编号 → 新标识

| 旧编号 | 新标识 | 名称 |
|:---|:---|:---|
| `#1` | `SPEC.FEAT.TERM.01` | VT 解析器 |
| `#2` | `SPEC.FEAT.TERM.02` | 颜色支持 |
| `#3` | `SPEC.FEAT.TERM.03` | 主/备屏幕缓冲 |
| `#4` | `SPEC.FEAT.TERM.04` | Scrollback 回滚 |
| `#5` | `SPEC.FEAT.TERM.05` | 滚动区域与光标控制 |
| `#6` | `SPEC.FEAT.TERM.06` | 鼠标上报 |
| `#7` | `SPEC.FEAT.TERM.07` | OSC 集成 |
| `#8` | `SPEC.FEAT.TERM.08` | 宽字符 |
| `#9` | `SPEC.FEAT.RENDER.01` | 等宽网格渲染 |
| `#10` | `SPEC.FEAT.RENDER.02` | 字体 |
| `#11` | `SPEC.FEAT.RENDER.03` | 属性渲染 |
| `#12` | `SPEC.FEAT.RENDER.04` | 光标 |
| `#13` | `SPEC.FEAT.INTERACT.01` | 键盘映射 |
| `#14` | `SPEC.FEAT.INTERACT.02` | 文本选择 |
| `#15` | `SPEC.FEAT.INTERACT.03` | 复制粘贴 |
| `#16` | `SPEC.FEAT.INTERACT.04` | 终端内搜索 |
| `#17` | `SPEC.FEAT.INTERACT.05` | URL 检测 |
| `#18` | `SPEC.FEAT.WS.01` | 多标签页 |
| `#19` | `SPEC.FEAT.WS.02` | 任意分屏 |
| `#20` | `SPEC.FEAT.WS.03` | 多窗口 |
| `#21` | `SPEC.FEAT.WS.04` | 标签状态提示 |
| `#22` | `SPEC.FEAT.WS.05` | 会话生命周期 |
| `#23` | `SPEC.FEAT.WS.06` | 全屏 |
| `#24` | `SPEC.FEAT.CONN.01` | 本地终端 |
| `#25` | `SPEC.FEAT.CONN.02` | SSH 连接 |
| `#26` | `SPEC.FEAT.CONN.03` | SSH 档案管理 |
| `#27` | `SPEC.FEAT.CONN.04` | SFTP 浏览器 |
| `#28` | `SPEC.FEAT.CONN.05` | 串口终端 |
| `#29` | `SPEC.FEAT.CONN.06` | Telnet |
| `#30` | `SPEC.FEAT.CONN.07` | 连接管理器 UI |
| `#31` | `SPEC.FEAT.PREF.01` | 主题 |
| `#32` | `SPEC.FEAT.PREF.02` | 设置面板 |
| `#33` | `SPEC.FEAT.PREF.03` | 持久化 |
| `#34` | `SPEC.FEAT.PREF.04` | 快捷键系统 |
| `#35` | `SPEC.FEAT.PREF.05` | i18n |
| `#36` | `SPEC.FEAT.PREF.06` | 零配置可用 |
| `#37` | `SPEC.NF.PERF.01` | 输入延迟 |
| `#38` | `SPEC.NF.PERF.02` | 渲染吞吐 |
| `#39` | `SPEC.NF.PERF.03` | 启动时间 |
| `#40` | `SPEC.NF.PERF.04` | 内存 |
| `#41` | `SPEC.NF.PLAT.01` | 跨平台 |
| `#42` | `SPEC.NF.RELI.01` | 可观测性 |
| `#43` | `SPEC.NF.A11Y.01` | 无障碍基线 |
| `#44` | `SPEC.FEAT.WS.07` | 命令面板 |
| `#45` | `SPEC.FEAT.WS.08` | Pane 缩放 |
| `#46` | `SPEC.FEAT.WS.09` | 广播输入 |
| `#47` | `SPEC.FEAT.WS.10` | 撤销关闭标签 |
| `#48` | `SPEC.FEAT.WS.11` | 会话恢复 |
| `#49` | `SPEC.FEAT.WS.12` | Quick Terminal |
| `#50` | `SPEC.FEAT.CONN.08` | SSH 隧道 |
| `#51` | `SPEC.FEAT.CONN.09` | 凭据安全存储 |
| `#52` | `SPEC.FEAT.CONN.10` | 密钥管理器 |
| `#53` | `SPEC.FEAT.CONN.11` | 会话日志 |
| `#54` | `SPEC.FEAT.INTEG.01` | OSC 133 命令块 |
| `#55` | `SPEC.FEAT.INTEG.02` | OSC 7 工作目录 |
| `#56` | `SPEC.FEAT.INTEG.03` | 系统通知 |
| `#57` | `SPEC.NF.PKG.01` | 打包分发 |
| `#58` | `SPEC.FEAT.INTEG.04` | CLI 启动参数 |
| `#59` | `SPEC.FEAT.XFER.01` | PTY 尺寸同步 |
| `#60` | `SPEC.FEAT.RENDER.05` | 缩放适配 |
| `#61` | `SPEC.FEAT.TERM.09` | 字符编码 |
| `#62` | `SPEC.NF.PERF.06` | 输出背压 |
| `#63` | `SPEC.FEAT.PREF.07` | 配置韧性 |
| `#64` | `SPEC.FEAT.CONN.12` | 剪贴板访问授权 |
| `#65` | `SPEC.NF.PERF.05` | 空闲资源占用 |

---

## 历史版本（v0.1–v0.6，原文照录）

> 以下条目为各版本当时的记录原文，其中出现的 `P0`–`P3`/`M0`–`M5` 是历史口径（现行定义见 [`PLAN.md`](PLAN.md)）；需求编号已随 v0.7 统一替换为语义标识，历史事实本身未作改写。

- v0.1（2026-09-20）：初稿。
- v0.2（2026-09-22）：业界查漏补缺（对照 Windows Terminal / Termius / XShell / WezTerm / iTerm2 / SecureCRT）——新增 SPEC.FEAT.WS.07、SPEC.FEAT.WS.08、SPEC.FEAT.WS.09、SPEC.FEAT.WS.10、SPEC.FEAT.WS.11、SPEC.FEAT.WS.12、SPEC.FEAT.CONN.08、SPEC.FEAT.CONN.09、SPEC.FEAT.CONN.10、SPEC.FEAT.CONN.11、SPEC.FEAT.INTEG.01、SPEC.FEAT.INTEG.02、SPEC.FEAT.INTEG.03、SPEC.NF.PKG.01、SPEC.FEAT.INTEG.04（命令面板、pane 缩放、广播输入、会话恢复、SSH 隧道、凭据安全存储、密钥管理、会话日志、snippets、OSC 133/7 集成、CLI 参数、打包分发等）；修订 SPEC.FEAT.TERM.01（bracketed paste/focus reporting）、SPEC.FEAT.TERM.06（alternate scroll）、SPEC.FEAT.RENDER.03（bold-is-bright/最小对比度）、SPEC.FEAT.INTERACT.02/SPEC.FEAT.INTERACT.03（智能选择、quick select、copy-on-select、右键行为）、SPEC.FEAT.CONN.01（TERM 注入/自定义命令）、SPEC.FEAT.CONN.02（keyboard-interactive/agent 转发）；裁剪表补 6 项（自动更新、Warp AI、Sixel、复用域、tmux control、Triggers）；缺口表补 G9–G11（全局热键/音频/通知）；里程碑扩 M5 观察池。
- v0.3（2026-09-22）：四项裁决落定（§7 改为已裁决）——产品名 Borealis、SSH 库 libssh2（接口抽象隔离）、默认字体内置 Cascadia Code、scrollback 默认截断；同步回填标题、SPEC.FEAT.TERM.04 SPEC.FEAT.RENDER.02 SPEC.FEAT.CONN.02 SPEC.NF.PKG.01。
- v0.4（2026-09-22）：规格书审查查漏补缺（对照 Aurora 主仓源码逐项实测 + 业界能力面；该行原引的审查报告 `REVIEW_SPECIFICATIONS_v0.3.md` 经 2026-09-29 全仓核查并不存在，其结论已全部并入本文，故不再作为引用目标）——
  - **新增 7 条**：SPEC.FEAT.XFER.01 PTY 尺寸同步（P0）、SPEC.NF.PERF.06 输出背压（P0）、SPEC.FEAT.RENDER.05 缩放适配（P1）、SPEC.FEAT.PREF.07 配置韧性（P1）、SPEC.FEAT.TERM.09 字符编码（P1）、SPEC.FEAT.CONN.12 剪贴板访问授权（P2）、SPEC.NF.PERF.05 空闲资源占用（P1）；
  - **就地修订 13 条**：SPEC.FEAT.TERM.01（DEC Special Graphics 字符集、`DECAWM`/`DECOM`/`IRM`、`DA1`/`DSR`）、SPEC.FEAT.TERM.07（OSC 标题消费链路与优先级）、SPEC.FEAT.INTERACT.01（`DECCKM`/keypad 应用模式）、SPEC.FEAT.INTERACT.03（复制语义三开关）、SPEC.FEAT.INTERACT.04（100k 行搜索指标）、SPEC.FEAT.INTERACT.05（打开前确认与协议白名单）、SPEC.FEAT.WS.02（任意分屏四义 + Splitter 二元约束 + 焦点路由自研）、SPEC.FEAT.WS.06 SPEC.FEAT.WS.10（P2 改标 P1）、SPEC.FEAT.WS.01（一度改标 P1，见 v0.5 回改 P0）、SPEC.FEAT.CONN.02（连接复用/旧算法兼容）、SPEC.FEAT.CONN.05（发送行尾序列/十六进制视图）、SPEC.FEAT.WS.07（改复用框架 `CommandPalette`）、SPEC.FEAT.CONN.09（审计范围界定）、SPEC.FEAT.CONN.11（默认关闭 + 风险提示 + 敏感过滤）；
  - **legend 修订**：P2 的交付窗口由「M4」放宽为「M3-M4，允许因主题内聚随 M3 一并交付」（SSH 族的 SPEC.FEAT.CONN.04 SPEC.FEAT.CONN.08 SPEC.FEAT.CONN.10 原本即落 M3）；M5 行的 `SPEC.FEAT.WS.04(beep)` 改为非编号写法，避免被解析为 SPEC.FEAT.WS.04 整体进 M5；
- v0.5（2026-09-23）：M1 工作区下限上调（裁决 7.10）——**SPEC.FEAT.WS.01 多标签页由 P1 回改为 P0、SPEC.FEAT.WS.02 分屏由 P1 改 P0，二者均由 M2 前移至 M1**；SPEC.FEAT.WS.02 重写为「任意分屏」并按**任意方向 / 任意深度 / 任意比例 / 任意 pane 数**四义界定，明确每一层为多子 pane 容器而非两两嵌套，并点明框架 `Splitter` 仅二元、pane 树须自研；与 SPEC.FEAT.XFER.01 PTY 尺寸同步标注强耦合。里程碑连带调整：M1 主题改为「多标签 + 任意分屏的本地终端」并收 SPEC.FEAT.WS.01 SPEC.FEAT.WS.02，M2 主题改为「工作区打磨」并移出 SPEC.FEAT.WS.01 SPEC.FEAT.WS.02；新增裁决 7.10 记录该决策及其工作量代价。
  - **新增裁决 5 项**：7.5 不 reflow、7.6 串口默认 GB18030、7.7 单实例转交落点、7.8 单进程模型、7.9 日志默认关闭；
  - **里程碑重排**：M1 收 SPEC.FEAT.XFER.01 SPEC.NF.PERF.06，M2 收 SPEC.FEAT.RENDER.05 SPEC.FEAT.PREF.07 SPEC.NF.PERF.05，M3 收 SPEC.FEAT.TERM.09，M4 收 SPEC.FEAT.CONN.12，M5 观察池收 SPEC.NF.A11Y.01（无障碍基线不再悬空）；
  - **附录 A**：路径改相对主仓根目录、引用改符号名（去除全部 `file:line` 锚点）、修正 4 处位置（`commands.h` / `splitter.h` / `theme_scope.h` / `storage.h`）、补 `CommandPalette` 与可拖拽 `Splitter` 两项高价值可复用能力、补 X11/Wayland IME 与 `PerfOverlay`、新增 G12 待评估项。
- v0.6（2026-09-29）：开工前口径对齐（框架现状按 Aurora 当日活动分支复核实测）——
  - **新增裁决 4 项**：7.11 跨平台交付分期（SPEC.NF.PLAT.01 保持 P0，M1 只交付 Windows、Linux 等价随 M2，并要求平台层从 M1 起即接口隔离）、7.12 应用侧三方依赖经 `find_package` + vcpkg（含 `x64-windows-static-md` 的 CRT 约束，标注为未经真机验证的推断）、7.13 框架缺口按类别分流（渲染与事件链路先补框架，交互体验类留应用侧）、7.14 命名统一为 `borealis`（修订 7.1 的「目录名不变」括注，工作区目录名列为唯一例外）；
  - **附录 A 事实修正 3 处**：G10 音频关闭（`AudioContext`/`AudioBuffer` 图 API 恒编译，WASAPI 与 ALSA 后端已实现，须经 `AURORA_ENABLE_AUDIO` 开启）、G12 DPI 通知关闭（`Surface::set_scale_change_handler` + Win32 `WM_DPICHANGED` 已接线，Linux 三后端未接线故留单腿）、A.1 相应新增「音频播放」「DPI 缩放变更上报」两行复用能力；
  - **G1/G2 形态落定**：G1 = `Painter` 批量文本 run 原语 + 等宽整像素 cell 度量 + East Asian Width 判定进框架（网格模型与颜色合成留应用侧，原语只收合成后的最终值），并记录 `FontEngine` 的 dp 测量与物理光栅宽度偏差为不可复用现有链路的依据；G2 = `click_count` 框架统一自算、五后端一致；A.3 反哺承诺据此改写（基准由「候选用例」升为随 M0 落地并挂 10% 回归门禁）；
  - **里程碑连带**：M0 出口判据补度量/EAW/基准门禁并将「G1–G3」更正为「G1–G2」（G3 属应用域，本就不进框架）；M1 主题标注 Windows、收 SPEC.NF.PLAT.01 的 Windows 侧；M2 主题改为「工作区打磨 + Linux 等价」并收 SPEC.NF.PLAT.01 与 SPEC.FEAT.RENDER.05 的 Linux 缩放腿；M5 行的缺口清单由 G9/G10/G11 收窄为 G9/G11；
  - **就地修订 2 条**：SPEC.FEAT.WS.04（可听铃声的能力缺口已消除，仅余排期取舍，优先级不动）、SPEC.FEAT.RENDER.05（框架现状与残留单腿）；SPEC.NF.PLAT.01 补交付窗口说明；
  - **文档缺陷修正 2 处**：A.2 表头声明 5 列而全部数据行为 4 列（「缺口」列本就内含事实依据），表头改为 4 列以消除渲染错位；v0.4 版本行所引的 `REVIEW_SPECIFICATIONS_v0.3.md` 全仓不存在，已就地标注为失效引用（与本仓即将沿用的「引用必须可达」口径一致）。
