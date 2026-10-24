# 串口终端 UI 与连接腿设计稿（`SPEC.FEAT.CONN.05`）

> **状态**：**评审已收口（2026-10-11）**——§3 的 D1–D13 经人裁决为**全按推荐档**（一句「按你推荐的来」收口，无一条改判），并入 `codespec/SPECIFICATIONS.md` §7 裁决 **7.110**，据此转实现（§4 布局落 `src/`，按批 1 纯逻辑与平台腿 → 批 2 连接腿 → 批 3 UI 与装配三批走）。
> **配套高保真草图**：`codespec/UI_SERIAL.draft.svg`（本稿 §2 的 1–7 标号判据即图中圈号，各贴在所属屏旁；SVG 为事实来源，后续改图直接改 SVG）。
> **出稿时不碰代码**（AGENTS §4.1 第 2 条的 UI 件节奏：先出稿评审再实现）；2026-10-11 裁决落地后转实现，实现期若与稿面口径分叉，按 AGENTS §4.2 第 12 条以**代码运行时**为准回写本稿。
> **实现进度**：批 1（档案模型 + `conn/serial_model` + `platform/serial` 两腿 + `flow` 键）**已落**，POSIX 腿在 pty 替身设备上有证人（`utest_posix_serial_platform`，14 档速率回读逐档相符）；Win32 腿按 F10 只到「写完未编译验证」。两处与稿面的出入见 §4（`flow` 行随批 1 进核对表、串口档案往返落在 `utest_config`）。批 2（`conn/serial_connection` + `Session` 写出侧行尾折算 + 替身设备 `itest_posix_serial_connection`）**已落**，落地时的两条新读数（F11 端口锁的持有面、F12 替身节点消失后的 ENOENT）与三处稿面出入按 AGENTS §4.2 第 12 条回写在本稿 §0/§4/§5/§6/§8。批 3（向导第三型＝三张类型卡 + 串口九行字段集 + 设备三件套、`ui::serial_format`、`main.cpp` 的串口会话构造/枚举快照/重启分流/`serial.set_baud` 命令、核对表七行改判）**已落**，`src/main.cpp` 的两条 `TODO(SPEC.FEAT.CONN.05)` 全部闭合；落地期的四处拍板与三处稿面出入按 AGENTS §4.2 第 12 条回写在本稿 §2/§4/§7/§8，入册裁决 **7.111**。**仍欠真设备 `etest_serial_device`（F1 挡住）与 §6 六条真机走查 ⇒ 本稿一律不称「可用」**（第 33 条）。
> **上游依据**：`SPEC.FEAT.CONN.05` 原文（枚举 / 波特率·数据位·停止位·校验·流控完整配置 / 断开重连 / 连接中热改波特率 / 默认编码 GB18030 / 发送行尾可配 CR·LF·CRLF 默认 LF / 十六进制视图与行首时间戳＝延后子项）、裁决 7.6（串口缺省 GB18030）、7.16（双向编码）、7.33⑥（`term::LineEnding` 是串口行尾的代码侧形态）、7.99（`session::CloseReason` 与重连分类表）、7.103（`term::SessionEncoding` 已具备）。

---

## 0 地基实测（2026-10-11 本沙箱，POSIX 腿；Windows 腿一律标未验证）

形态不是推演出来的，下列每条都有本机读数（探针是一次性脚本，不入仓；复现口径见 §8）。

| # | 读数 | 对本稿的约束 |
|---|---|---|
| **F1** | `/dev/ttyS0`…`/dev/ttyS15` 节点**存在**，但属组 `tty`(gid 20) 而当前进程的补充组为 `4,24,27,30,46,100,111,114,1000` ⇒ `open()` 回 **EACCES(13)**；本沙箱无 sudo | **真设备腿在本环境不可达**：etest 与「参数真的下到了设备」这一档只能归真机（§6），代码侧只能证到调用序与折算 |
| **F2** | `/sys/class/tty/<name>/type` **只有串口类节点有**：`ttyS0`→`4`（16550A）、`ttyS1..3`→`0`（PORT_UNKNOWN）、虚拟 tty（`tty1`）**无该文件** | 枚举侧「是不是串口」的可用判据＝`type` 文件存在，而不是名字前缀白名单 |
| **F3** | 本机**无** `/dev/ttyUSB*`、`/dev/ttyACM*` 节点，**无** `/dev/serial/by-id` 目录 | USB 串口的友好名取法（`../device/../product`）与 by-id 别名只能登记形态，本环境不可实测 |
| **F4** | 以 `pty.openpty()` 的 slave **路径**重开作替身设备：`tcsetattr(TCSANOW)` 接受全部标准档位 `B50 / B300 / B1200 / B9600 / B19200 / B38400 / B57600 / B115200 / B230400 / B460800 / B921600 / B1000000 / B1500000 / B2000000`，回读 `c_ispeed`/`c_ospeed` 逐档一致 | 「波特率写下去并回读相符」在替身设备上**可证**，故热改波特率的腿有无头证人 |
| **F5** | 同一替身设备上：`CS6`/`CS7` 与「只置 `PARENB`」的 `tcsetattr` **直接 EINVAL(22)**；`CS5`/`CS8` 接受但回读 `c_cflag` 与写入值不等；`CSTOPB` 接受 | **帧格式（数据位/校验/停止位）在无真设备时不可判**：本稿只把它们折成平台侧位组合（纯逻辑可单测折算），不宣称「设备侧生效」 |
| **F6** | 替身设备上 `CRTSCTS`（硬件流控）、`IXON|IXOFF`（软件流控）、`CLOCAL & ~HUPCL` 三组 setattr 均接受且回读位一致 | 流控三档的「写下去」可证（itest），「设备侧真的拉线」不可证 |
| **F7** | `TIOCMGET` 在替身设备上回 **ENOTTY** | 调制解调器线状态（CTS/DSR/RI 显示）**无替身可证**，本稿不做线状态显示（§7） |
| **F8** | 替身设备写方向：slave 写 `a\rb\n` → master 读，`ONLCR` 开与关**读数逐字节相同**（均原样 `a\rb\n`） | **发送行尾不得指望设备规程改写**，由本仓自己拼字节（D11 的根据） |
| **F9** | 替身设备读方向：无写者时 `read()` 返回 **0 字节（EOF）**；master 关闭后 slave `read()` 仍是 **EOF 而不是 EIO** | 替身的掉线形态只有 EOF；真 USB 串口拔出常见 EIO。两条都得进掉线归因（D7） |
| **F10** | 本沙箱无 MSVC、无 Win32 头可编 | §4 的 Win32 侧一切说法（`CreateFile("\\\\.\\COMn")`、`DCB`、`GetCommProperties`、`QueryDosDevice`）标**推断／未验证**（AGENTS §4.2 第 11 条），待真机复测回写 |
| **F11** | 实现期读数（2026-10-11，批 2）：读循环**在端口锁内做 20 ms 定时读**时，同进程另一线程问端口（`current_baud()` / `write()`）实测等锁 **1.9–34 s**；同一份代码在 `strace -f` 下 20 ms 就答完。用两线程「持锁 sleep 20 ms × 200 次 + 另一线程取锁」的 30 行最小复现（不含本仓任何代码）同样复现，而「取锁后立即放」的不持锁版平均 0 ms、最长 0 ms | **本沙箱对「持锁休眠」的调度惩罚是秒级**，故连接腿的读循环不许在锁内做定时等待（§5 的锁纪律据此定形，架构 §3.4「锁内不做 IO」在串口腿的读法）；真机上这一改同样把 UI 线程的最长等待从一个读分片降到一次非阻塞探测 |
| **F12** | 替身设备的 master 关闭后，`/dev/pts<N>` 节点**随即消失**：按原路径重开回 **ENOENT(2)**（本沙箱确定读数，多次一致） | 掉线重拨用例的第二次打开必然落 `NoNode`→`Unknown`→不可重拨。itest 那条用例因此**不锁终态档位**，只锁三条与机器无关的事实：投过 attempt≥1 的环内快照、`on_closed` 只投一次、终态不是「用户停止」 |

---

## 1 范围与边界

**本期交付**：

1. 串口**档案**进连接模型（`SPEC.FEAT.CONN.03` 的类型域加一档）与**向导**第三张类型卡（`SPEC.FEAT.CONN.07` 的「按连接类型分步表单」自此有第三型）。
2. 串口**设备枚举**（本端可选口 + 可达性标注 + 友好名）与**完整参数配置**（波特率 / 数据位 / 停止位 / 校验 / 流控）。
3. 串口**会话连接腿**：打开→置参→读线程→写→关闭，接进既有 `session::Connection` 抽象（AGENTS §4.5 第 23 条：termios / Win32 只在 `src/platform/`）。
4. **断开重连**：串口掉线归因进既有 `session::CloseReason`，复用 `src/conn/reconnect.{h,cpp}` 的分类与退避（不新写一套重连环）。
5. **连接中热改波特率**（需求明写）：入口、生效判据与失败回报（D5/D6）。
6. **默认编码 GB18030** 接线（`SPEC.FEAT.TERM.09` 那条「串口默认档」的在册尾巴自此闭合）与**发送行尾可配**（消 `src/main.cpp:156` 的 `TODO(SPEC.FEAT.CONN.05)`）。

**不本期交付**（§7）：十六进制收发视图、行首时间戳前缀（需求原文即标延后子项）、调制解调器线状态显示、设备热插拔后台监听、真机走查与真设备 etest。

**关键前提（口径不是本稿新造的）**：

- 设置域的六个键 `connection.serial.{baud,data_bits,stop_bits,parity,line_ending,encoding}` **已存在**（裁决 7.26① 全量建 schema），现状在反向核对表里是 `ConsumerStatus::Absent`（`src/ui/settings_catalog.cpp:304-337`）——本棒就是把这六行改判 `Wired`，并按 D3 追加第七键 `flow`。
- 「流控」**不在现有 schema 里**（`config::SerialDefaults` 只有 baud/data_bits/stop_bits/parity/line_ending/encoding 六成员，`include/borealis/config/settings.h:187-197`），而需求明写「流控完整配置」⇒ 必须加键，见 D3。
- 键盘映射的字节表由 `utest_keymap` 逐字节锁死（裁决 7.30），行尾折算**不得改那张表**，见 D11。

---

## 2 草图与判据对应

草图五屏（A 向导字段集 / B 设备下拉展开态 / C 热改波特率双框 / D 掉线与重连两态 / E 侧栏与临时连接），图中圈号 1–7 即下表判据；B/D/E 屏内设备名与读数为**示意**（本机无 USB 口，见 F3）：

| 标号 | 判据 | 对应需求 |
|---|---|---|
| 1 | 向导步骤 0 的类型卡自此三张（Local / SSH / **Serial**）；步骤 1 的字段集由类型卡决定，串口型**不显示** host/port/user/认证四栏 | `CONN.07`「按连接类型分步表单」（D1/D2） |
| 2 | 串口字段集九行：展示名、设备、波特率、数据位、停止位、校验、流控、发送行尾、编码；设备行是「下拉 + 刷新 + 自定义路径」三件套，其余八行一律**循环按钮**（沿用 `auth_button_` 那枚四档循环件的先例，不在对话框里投行内 `Dropdown`）。**批 3 落地补**：三件套的两档互斥，产值只取**当前可见的那一行**（切档即重建、退场那份不留值）⇒ 手输后切回列表＝设备名清空＝保存当场报「设备必填」 | `CONN.05`「完整配置」（D2/D4） |
| 3 | 设备下拉每行＝设备名 + 友好名 + 可达性；不可达项**照常列出但置灰**并带归因（权限 / 暂不可开），绝不在列表里玩「看不见就是没有」。**批 3 落地补**：置灰经框架文档化的 `paint_item` 覆写（只改文本色），点选否决经 `set_on_change` 回退；颜色对不对眼属绘制判据，无头断不了，留 §6 走查第 ⑵ 条 | `CONN.05`「串口枚举」（D8/D9） |
| 4 | 校验走 `validate_draft` 的串口分支：新增四档问题码（设备必填、波特率必须在本仓档位表内、数据位 5..8、停止位 1..2），首个问题折成一行可读文案上屏，**不静默置灰保存钮** | `CONN.05`（D3/D4） |
| 5 | 连接中热改波特率的入口是命令 `serial.set_baud`：焦点 pane 是串口会话⇒单选档位对话框；不是串口会话⇒一句「当前会话不是串口」的提示，**不改任何状态** | `CONN.05`「连接中热改波特率」（D5） |
| 6 | 热改的回报走**回读比对**：`tcsetattr` 后立刻 `tcgetattr`，档位相符才报「已切到 {0}」，不相符报「设备侧拒绝：{0}」且界面**回到原档位**（不假装成功，AGENTS §4.4 第 21 条的同一条纪律用在单键上） | `CONN.05`（D6，代价见 F5） |
| 7 | 掉线与重连沿用 `UI_RECONNECT.draft.md` 已裁的视口浮层两态，只换归因文案（串口没有「远端退出」这一档）；重连成功即**重开设备并按档案重新置参**，不改口称「续传」 | `CONN.05`「断开重连」（D7） |

---

## 3 裁决（D1–D13，2026-10-11 收口：**全按推荐档，无一条改判**）

| # | 待决 | 选项 | 结论（推荐） | 理由与代价 |
|---|---|---|---|---|
| **D1** | 串口是否进**档案模型** | ① `ConnectionType` 追加 `Serial` 一档 + 新 `SerialProfile`，`Profile` 加 `serial` 子结构，JSON 读写与确定性 id 一并扩；② 不入档案，只做「临时串口连接」一次性入口 | **①** | ② 满足不了 `CONN.03` 侧栏与 `CONN.05`「完整配置」的可复用性（每次重连重敲九个字段），也拿不到 `SPEC.FEAT.CONN.05` 的「按 profile」语义。代价：`Profile` 是公共头（`include/borealis/conn/profile.h`）的**追加式**扩型——不动既有成员与可见性（AGENTS §4.1 第 6 条），只加一档枚举值与一个子结构；串口无凭据，天然过 `CONN.09` 的明文审计 |
| **D2** | 设备选择控件形态 | ① 设备行＝`Dropdown`（枚举快照）+ 一枚「刷新」+ 一档「自定义…」落 `TextInput`；② 纯文本框手输路径；③ 只给下拉不给手输 | **①** | ③ 在 F1 这类「枚举得到节点但无权限」或驱动未挂 `/sys` 的环境里等于把用户关在门外；② 放弃了 `CONN.05` 第一条「串口枚举」。代价：下拉是**行内 Dropdown**，而 Aurora 有条既定形态约束「列表行内不得使用覆盖绘制不占布局的控件」（`PLAN.md` §6 的 G30 回货自陈）——向导是 `au::Dialog` 自建 content 而非 `LazyList` 行区，且 `itest_settings_panel` 已有行内 `Dropdown` 真点选先例（G29/G30/G31 三条回货的证人就落在这上面），故不新增缺口；「自定义…」档与手输框互斥切换需一次就地重建，口径同 `keys_panel` 的推送框就地重建（裁决 7.109⑤） |
| **D3** | 流控键 | ① 新增 `connection.serial.flow`，取值 `none`/`hardware`/`software`，缺省 `none`，同步进 `SerialDefaults` 与反向核对表；② 本期不做流控 | **①** | ② 直接违背 `CONN.05` 原文「流控完整配置」。F6 实测流控两档在替身设备上写得下去、回读得到，故可证面够。代价：schema 加一键＝`utest_config` 往返补例 + `utest_settings_catalog` 改判（该键从 `Absent` 起即 `Wired`），装载侧对既有 `borealis.json` 缺该键回 `none`（不升 `schema_version`：缺键即默认是既定宽容口径，裁决 7.26④） |
| **D4** | 档案字段与设置域默认值的关系 | ① 新建串口档案时把 `settings.connection.serial` 六（+D3 一）键**灌成具体值**存进档案，之后档案独立；② 档案存「跟随默认」哨兵，运行期再解析 | **①** | ② 多一个第二态，「这个会话此刻到底跑什么参数」要跨两处解析（回看与排障都要问两个源），且 `CONN.05` 的热改语义必须有一处唯一落点。代价：设置页改默认值不回溯已在档的串口档案（面板上那句「下次会话生效」的口径本就如此，裁决 7.76 同档）；F5 里 `data_bits` 取值 5..8 而替身设备只肯收 CS5/CS8 ⇒ 档位表**不由 UI 现编**，收在 `conn::serial_model` 的一份常量表里（D6 的校验源与 Win 腿的折算源同一份） |
| **D5** | 热改波特率的入口 | ① 命令 `serial.set_baud`（命令面板 + 无键位绑定）呼单选档位对话框；② 串口会话的视口内常驻工具条；③ 编辑档案对话框加「应用到在途会话」钮 | **①** | ② 要为一条低频动作给每块串口 pane 加一条常驻高度，与「终端区寸土寸金」相反且无先例；③ 把「落盘的档案」和「在途会话的当前参数」两套语义焊在一个钮上，档案名与波特率同时可能脏。① 复用已落的 `Hooks::commands` 与命令面板族（`search.open`/`tunnels.open`/`keys.open` 同族）。代价：入口藏在命令面板里，新手找不到——按嵌入式用户实际（换速率是刻意动作，不是随手调）判定可接受，真机走查若不可接受再补视口内入口 |
| **D6** | 热改的生效判据与失败处置 | ① `tcsetattr` 返回 0 即报成功；② **setattr 后立刻 getattr 回读比对**，相符才算成功并把新档写进「会话当前参数」，不相符/报错即回报拒绝且界面回到原档 | **②** | ① 在 F5 那类「内核收下但没落」的情形上会说谎，而本条的判据恰好就是「设备侧现在跑几波特」，错了整屏乱码。② 是唯一能自证的做法。代价：真机走查必须验「拒绝档」的手感（回读不符时用户看到什么）；`PLAN.md` §4 无此项，故登记在册（§6） |
| **D7** | 串口掉线的归因映射 | ① 打开失败与读侧失败各按 errno 分档映射到既有 `CloseReason`（EACCES/EPERM/ENODEV→`Unknown` 即不可重拨；ENXIO/EBUSY/节点暂缺→`DialNetwork`；读侧 EIO→`LinkLost`，读侧 EOF→`LinkLost`）；② 给 `CloseReason` 新增串口专属档位 | **①** | ② 要动公共头枚举并重写 `should_reconnect` 分类表与 `utest_reconnect` 全表，而该枚举的既定纪律是「分类只是事实归因，新增档不改义」（`session/connection.h:29-31`）；① 复用现成六档，`reconnect.h` 零改动。代价与两处口径须人认可：⒈ **串口的 EOF 归 `LinkLost` 而非 `RemoteExit`**——串口没有「远端进程正常结束」这个概念，EOF 只代表设备没了；而 F9 说替身设备的掉线形态恰恰只有 EOF，这一档映射也是无头腿能证重连的前提。⒉ 不可自愈类（权限、节点根本不存在）落 `Unknown`，按裁决 7.99 D2① 的「未知即保守不可重拨」——不给 `EACCES` 无限重拨的机会 |
| **D8** | 枚举的时机与线程 | ① 只在「向导打开设备行」「点刷新」「启动时装配层取一次」三处，经既有串行 worker 投递，UI 线程零目录 IO；② 后台轮询定时枚举，热插拔自动上屏 | **①** | ② 为一条低频动作常驻一个定时器并在后台反复 stat 文件系统，且热插拔自动改列表会把用户正在点的下拉换掉（焦点与选中态乱）。代价：拔掉 USB 线再插上，下拉里**不会自己出现**，得点刷新——按 F1/F3（本环境根本没有可测的 USB 口）判断，本期不做后台监听（§7 登记为延后子项） |
| **D9** | 不可达设备的显示 | ① 照列 + 置灰 + 一句归因（`无权限` / `暂时打不开`）；② 从列表里过滤掉 | **①** | ② 在 F1 这台机器上会得到**一张空列表**（16 个 ttyS 全在、全不可开）——用户无从分辨「没有串口」与「有但没权限」，这是嵌入式调试最常见的坑（用户不在 `dialout`/`tty` 组）。代价：置灰项的行文案要新词条族（`keys_format` 的四族形态先例，随本棒补） |
| **D10** | 会话编码的接线 | 串口会话构造吃 `term::SessionEncoding{.name = 档案 encoding}`（缺省 GB18030，裁决 7.6），本地/SSH 恒 UTF-8 不变；`terminal.encoding` 两键现状已 `Wired`，本棒只多一条「按连接类型取缺省」的折算 | 如述 | 需求两处明写（`CONN.05`「默认编码 GB18030」+ `TERM.09` 验收）。代价：`PLAN.md` 里「串口默认 GB18030 随串口条落期」那条正式出账，`TERM.09` 行的相应文字随批 4 回写 |
| **D11** | 发送行尾的覆盖范围与折算点 | ① 折算点在**会话侧一处**：键入 Enter 与粘贴 `Convert` 与片段发送三路都取「本会话行尾」，`term::encode_key` 的字节表逐字节不动（仍产 CR），由 `Session` 在写出前把**单独一个 CR 字节**折成选定序列；② 折算点在**连接腿**（serial `write()` 里换 CR）；③ 只作用于粘贴，键入 Enter 恒 CR | **①** | ② 会改写任意来源的 0x0D（含二进制粘贴与设备回显抑制逻辑），且 LF 档下语义含混；③ 只治粘贴不治键入，而嵌入式调试的主场景就是「手敲一条命令按回车」——F8 实测设备规程不碰这批字节，所以本仓不改就永远没响应。代价：`Session` 需要一条「本连接行尾」的入参（构造期取用，生效档 `NextSession`，与 `SessionEncoding` 同一颗构造注入口径），且须一条用例锁死「键表产 CR 而串口会话落地为 CRLF」的两段事实 |
| **D12** | 逐标签连接类型图标（`SPEC.FEAT.WS.04` 的在册未落项） | ① 不在本棒，随 WS.04 余腿单开；② 本棒一并落三档图标 | **①** | 一个改动一个意图（AGENTS §4.1 第 3 条）：本棒的意图是「串口能连能配」，图标是标签栏的绘制事项。代价：串口标签本期与本地标签画得一样，识别靠档案名——真机走查时按这条如实说明。**〔2026-10-22 已出账，裁决 7.129：`SPEC.FEAT.WS.04` 的余腿落地，串口格自此画 `S`（`ui::connection_type_glyph()` 那张表），本行那句「与本地标签画得一样」不再是现状；§6 的走查第 ⑵ 条即这一档的肉眼验收，仍未做〕** |
| **D13** | 验证形态（F1 把真设备挡住了） | 三层：① `utest_serial_model`——参数→平台位组合的折算表、行尾字节拼接、枚举行的排序与可达性投影、档位表常量；② `itest_serial_connection`——以 `pty.openpty()` 的 slave **路径**作替身设备，证 open/置参/收发/热改/关闭的**调用序与回读**（按 F4/F6 可证、按 F5 帧格式**不判**、按 F7 线状态**不判**）；③ `etest_serial_device`——真设备/真 USB 口的腿，本沙箱按 F1 **写不出来**，登记在册 | 如述 | 不拿替身设备冒充真设备宣布「参数已生效」（F5 明确它连 `CS7` 都拒），也不因真设备不可达就跳过无头可证的两层。代价：`itest_serial_connection` 的 POSIX-only 属性——须按 `PLAN.md` §6 那套「平台专属测试 TU 按文件名只编进自己那一侧」的既有纪律挂进构建（Windows 腿待真机） |

---

## 4 文件布局 / TU 拆分（裁决后按此开工）

```
include/borealis/conn/profile.h        修改 —— ConnectionType 追加 Serial；新 SerialProfile
                                                {device_path, baud, data_bits, stop_bits, parity,
                                                 flow, line_ending, encoding}；Profile 加 serial 子结构
src/conn/profile.cpp                   修改 —— 串口档案的 JSON 读写与确定性 id（只追加，不动既有键）
src/conn/serial_model.{h,cpp}          新增 —— 纯逻辑件（无 au:: / 无 termios 类型，可无头单测）：
                                                波特率档位表（F4 实测十四档）、参数取值域校验、
                                                SerialSpec ⇄ 参数码、枚举行排序与可达性投影、
                                                行尾字节拼接（CR/LF/CRLF 三档）
src/conn/serial_connection.{h,cpp}     新增 —— 私有头（D1① 先例：平台类型仅前向声明）：
                                                session::Connection 的串口实现，读线程 + 断线归因（D7）
                                                + 热改波特率（D6 回读比对）。读循环的锁纪律按 F11 定形：
                                                `port_mutex_` 内只做一次超时 0 档的非阻塞探测，
                                                空闲等待留在锁外（与 SSH 腿 read_nonblocking 同款）
src/platform/serial.h                  新增 —— 平台抽象（口径同 src/platform/tcp.h）：
                                                list_ports() / open+configure / write / read / set_baud / close
src/platform/posix/serial.cpp          新增 —— termios 腿（O_NOCTTY|O_NONBLOCK、CLOCAL、raw 六件、
                                                CRTSCTS / IXON|IXOFF、tcsetattr+tcgetattr 回读、EIO/EOF 分档）
src/platform/win/serial.cpp            新增 —— Win32 COMM 腿（CreateFile/DCB/EscapeCommFunction/BuildCommDCB），
                                                **未经 MSVC 编译验证**（F10，AGENTS §4.2 第 11 条标推断）
src/ui/connection_wizard.{h,cpp}       修改 —— 类型卡三张；串口型字段集九行（循环按钮族 + 设备三件套）
                                                批 3 落地补两条口径：① 设备下拉的产值取**当前可见的那一行**
                                                （列表档与手输档互斥、切换即重建，退场那份不留值 ⇒
                                                切回列表＝设备名清空＝当场报「设备必填」）；② 置灰只覆写
                                                框架文档化的 `paint_item`（改文本色），点选否决落在
                                                `set_on_change` 的回退上——`select()` 非虚且同 index 不回调
src/ui/serial_format.{h,cpp}           新增 —— 纯函数小头（无 au::，先例 sftp_format/keys_format）：
                                                参数→文案、CloseReason→串口文案、置灰归因→文案
                                                **批 3 落地时第三族不落**（掉线浮层按 `ReconnectStop`
                                                分档而不按 `CloseReason` 分档，串口侧无该消费方；§7 登记）
src/main.cpp                           修改 —— 装配：serial.set_baud 命令、枚举 worker 投递与快照、
                                                串口会话构造吃 encoding/line_ending、消 line_ending 那条 TODO
include/borealis/config/settings.h     修改 —— SerialDefaults 增 flow（D3）
include/borealis/session/session.h     修改 —— Session 构造吃「本连接行尾」（D11；与 SessionEncoding 同注入口径）
src/config/store.cpp                   修改 —— serial 域读写增 flow 键 + profiles 的 serial 子结构
src/ui/settings_catalog.cpp            修改 —— connection.serial.* 六行 Absent→Wired（批 3），
                                                flow 行已随批 1 进表：写出的 schema 键必须逐行有控件，
                                                该闸是 `utest_settings_catalog.table_covers_exactly_
                                                the_written_schema_keys`，故行不能押到批 3
src/CMakeLists.txt                     修改 —— 上述 .cpp 编入 borealis_core（显式源列表）
tests/unit/utest_serial_model.cpp      新增 —— 折算表 / 取值域 / 行尾拼接 / 枚举投影 / 档位常量
tests/integration/itest_posix_serial_connection.cpp 新增 —— 替身设备调用序与回读（POSIX-only 编入）。
                                                文件名带 `_posix_` 才走 `cmake/BorealisTests.cmake` 的
                                                平台过滤，而那条过滤原先只挡 `utest_posix_`/`utest_win_`
                                                前缀，本批把它加宽成 `(utest|itest)_(posix|win)_`
tests/unit/utest_posix_serial_platform.cpp 新增（批 1 落点）—— 平台腿本身的替身证人：十四档
                                                set_baud 回读相符（F4）、无码速率报 ConfigRejected、
                                                双向字节与超时档、关掉写端后的 EOF（F9）、
                                                枚举非空且含不可达项（F1/F2）；文件名带 `_posix_`
                                                走 `cmake/BorealisTests.cmake` 的平台过滤
tests/unit/utest_config.cpp            修改 —— flow 键往返 + 串口档案 JSON 往返与坏字段逐项落回
                                                （原列 `utest_profile_serial` 的往返腿在此落：`utest_profile.cpp`
                                                头注已写明「落盘往返由 utest_config 守」，不另起一份；
                                                validate_draft 的串口四档仍随批 3 的 ProfileDraft 扩字段出）
tests/integration/itest_connection_wizard.cpp 新增（批 3 落地改口：本仓此前**没有**这一件，稿面原先误记「修改」）
                                                —— 类型卡三张与串口字段集九行的无头真点击，七条用例
                                                锁判据 1/2/3/4/5 的界面腿；行文本经框架自带的
                                                `serialize_props` 读（`Dropdown` 无逐行文本的公共读点，
                                                不为测试新增产品 API）；`Dropdown` 子类构造**须把
                                                `initial` 转发给基类**，否则编辑串口档案时下拉恒显占位、
                                                保存把 `device_path` 清空（批 3 由用例⑥ 抓到的真缺陷）
```

> 不改既有公共头的可见性与既有成员顺序（AGENTS §4.1 第 6 条）。框架侧**不预登记新缺口**：向导所需控件（`Dialog`/`Column`/`Dropdown`/`Button`/`TextInput`）与命令面板族全部有已落地的消费先例；若实现期撞到新的框架形态限制，按 AGENTS §5.1 类别分流另立 G 号并出任务书，不在本稿预写。

---

## 5 数据流与线程模型

```
config::Store（connection.serial 域：默认值灌入新建档案，D4；profiles 域的 serial 子结构，D1）
      │
装配层（main.cpp）
      ├─ 枚举：向导开设备行 / 点刷新 / 启动取一次（D8）── 串行 worker ──► platform::list_ports()
      │        └─ latest-value 快照（vector<SerialPortInfo>）→ 下拉只读最新一份（AGENTS §4.5 第 25 条）
      ├─ 连接：conn::SerialConnection(profile.serial, reconnect_policy)   ← 两参：档案 + 退避档（D7/D13）
      │        │   编码与发送行尾**不在连接腿**：装配层把它们交进 `Session` 的构造入参（D10/D11）
      │        ├─ platform::serial_open + 置参（读线程；start() 立即返回，UI 线程零阻塞 IO）
      │        ├─ 读线程 ──on_bytes──► Session（现有链：解码→状态机→网格→副本）
      │        └─ 读侧 EIO/EOF ──on_closed(CloseReason)──► conn::should_reconnect（D7）
      │                                          └─ 可重拨：既有退避环（reconnect.h，不新写）
      ▼                                                       └─ ReconnectProgress → 视口浮层两态（判据 7）
UI（connection_wizard 字段集 / serial_format 文案 / serial.set_baud 对话框）
      │ 动作经 Hooks 交回装配层（热改、连接、保存档案）
      └─ 热改：装配层呼 SerialConnection::set_baud ── 平台 setattr+getattr 回读（D6）── 结果回快照
```

- 读线程纪律照 `ssh_dial` / `forkpty_connection` / `tunnel_client` 三处先例：打开与置参在 worker，读在独立线程，`close()` 有 join 上界。
- **端口锁的持有面**（F11 读数定形）：读循环在 `port_mutex_` 内只做一次超时 0 档的非阻塞探测，空闲的 20 ms 等待留在锁外。串口腿的 `write`/`set_baud`/`current_baud` 都由 UI 线程呼，锁里若留着定时等待，它们就排队在一个读分片之后——本沙箱实测那是秒级（F11），真机上也违背架构 §3.4「锁内不做 IO」。
- 「会话当前波特率」的唯一真值源在 `SerialConnection`（热改成功才更新），面板每帧读快照投影；档案里的 `baud` 是**下次连接的缺省**，不是当前值——两个量必须分开展示，否则热改后用户以为档案没存（D6 的代价面）。装配层因此取 `current_baud()` 而不取 `profile.baud`（批 3 的接线口径）。
- D11 的折算发生在 `Session::write_out()` 这**一处**写出出口：`send_text`（编码产物）与 `send_bytes`（键表产的转义字节，Enter 的那一个 CR 走的就是这条路）都收到它。未给行尾档的会话（本地/SSH 的全部既有构造点）逐字节原样下发，收到的字节一寸不动；只折**单独一个 CR**，紧跟 LF 的那个 CR 属于一对 CRLF 故留着——否则 Windows 剪贴板与 bracketed 粘贴会被改成两个行尾（`term::plan_paste` 明写那两种形态逐字节不动）。

---

## 6 验收判据映射

- **串口枚举**：`utest_serial_model` 锁「有 `type` 文件即串口类候选」（F2）的投影与稳定排序；`itest_posix_serial_connection` 以替身路径证「枚举结果的 path 能 open」；不可达项照列置灰（D9）在 itest 里以「全不可达⇒列表非空且每行带归因」锁死。
- **完整参数配置**：`utest_serial_model` 逐档锁参数→平台位组合的折算（POSIX 侧 termios 位、Win 侧 DCB 字段），取值域校验锁 `DraftIssue` 四档；**设备侧是否生效**按 F5 明写不可判，故本条只到「写得下去、回读得到、折算表逐档有证人」。
- **断开重连**：`itest_posix_serial_connection` 证 EOF→`LinkLost`→`should_reconnect` 为真→按档案重开并重置参（D7 的两处代价口径各一条用例）；不可自愈类（模拟 EACCES）证**不**进重拨环。按 F12 的读数，重开一条已消失的替身路径必落 `NoNode`，故那条用例锁的是「投过环内快照 + `on_closed` 只投一次 + 终态不是用户停止」三条机器无关事实，不锁终态档位。
- **连接中热改波特率**：`itest_posix_serial_connection` 在替身设备上证「相符⇒更新当前档」与「不相符⇒回报拒绝且当前档不改脏」两分支（D6②）。**界面观测面**（批 3 接线口径）：热改成功后面板取 `current_baud()`；打开成功那一刻当前档＝档案档（`utest_posix_serial_platform` 锁平台腿回读，`itest_posix_serial_connection` 锁传下来的那一份）。
- **默认编码 GB18030**：串口会话构造吃 `encoding` 缺省 GB18030，`utest` 锁折算；喂进现场的证人已落在 `itest_posix_serial_connection`（对端写 GB18030 两字节「中」⇒网格第 0 格落成 U+4E2D），走的是串口会话自己的解码链，不借 `itest_render_viewport` 的本地腿通道。
- **发送行尾可配**：`utest_serial_model` 锁三档拼接；`utest_session` 四条锁折算（单个 CR 落地成选定档、已有的 CRLF 不被折成两个行尾、未给档的会话逐字节不动、Enter 的键表面仍产一个 CR ∧ 串口会话落地 CRLF）；`itest_posix_serial_connection` 在替身设备收端锁最后半段。`src/main.cpp:156` 的 `TODO(SPEC.FEAT.CONN.05)` 随批 3 消除。
- **向导界面腿（判据 1/2/3/4/5）**：`itest_connection_wizard` 的无头真派发用例（本棒落地时七条，Telnet 三案随 `SPEC.FEAT.CONN.06` 批 3 追加后现十条）——四张类型卡真点且串口型不画 SSH 五栏（1）、设置域七键在建草稿那一刻灌成具体值并原样交回（2 的 D4 半边）、每次开向导只发一次枚举而就地重建不发（2/3 的 D8 半边，断言的是后台取数次数）、不可达行照列且 `select()` 被否决回上一次可选行（3）、列表档与手输档互斥且**可见行产值**（2）、四档问题码逐档上屏且不关框不交档（4）、枚举回投只重建开着的那一行串口设备行（3）。**置灰的颜色本身不在断言面内**（无头无从比色），留在下面走查第 ⑵ 条。

- **测试读数口径**：交付时以 `ctest --test-dir build -E etest_` 的**项数与全案数**结项（AGENTS §4.4 第 21 条不认「应该更快了」，本棒同样不认「测了很多」），变异注入按第 22 条自证非空转。
- **真机走查**（AGENTS §4.4 第 33 条，未走查不得称「可用」）：⑴ 下拉在真实 USB 口上的友好名好不好认；⑵ 置灰归因文案够不够排障（F1 那种「有节点没权限」）；⑶ 热改拒绝档的手感与「档案值 vs 当前值」两处显示分不分得清；⑷ GB18030 串口会话里中文上屏观感与键入中文发到设备侧的字节（设备端回显，需真板子）；⑸ 拔线掉线与重连浮层的交接感；⑹ 六条 `connection.serial.*` 键在设置页改默认值后新建档案灌入是否合直觉（D4 的代价面）。

---

## 7 本期不做（延后 / 在册）

- **十六进制收发视图**与**行首时间戳前缀**：`CONN.05` 原文即标延后子项 ⇒ 随本棒在 `PLAN.md` §4 清册登记，目标阶段「M4 之后」。**〔2026-10-25 另立设计稿 `codespec/UI_SERIAL_MONITOR.draft.md`（地基实测 F1–F14 与 D1–D9 同日收口），并按该稿 §4 分四批同日全部落地＝裁决 7.149：`session::RawTap` 的 256 KiB 采集环 + `Session` 的 RX／TX 两笔记痕 + `ui::serial_monitor_format` 折算件 + 面板族第八只 `ui::SerialMonitorPanel` + 命令 `serial.monitor.toggle`，默认不采、零新设置键、不落盘。`PLAN.md` §4 那条清册行据此改口为「已结项」，本卡自身的六项延后（滚动回溯／导出与复制／按编码解码的文本视图档／三型入口／逐字节时间戳与报文边界／环上限与缺省档跨重启记忆）随该稿 §7 另立在册。本稿 §0 F1 与 F10 那两条环境欠账原样成立 ⇒ 真机走查仍未做，本条不称「可用」。〕**
- **设备热插拔后台监听**（D8 的代价面）：不轮询、不自动刷新下拉。
- **调制解调器线状态显示**（F7：替身设备 ENOTTY，本环境无从证；需求亦未要求）。
- **真设备 etest**（F1）：`etest_serial_device` 待有 `tty`/`dialout` 权限与实机串口线的环境补，与 CONN.08/10 那两处待 sshd 的欠账同档登记。
- **Windows/MSVC 腿验证**（F10）：`src/platform/win/serial.cpp` 以头文件与官方 API 形态写出，编译与真机复测待真机。
- **`POLLHUP`/`POLLERR` 无 `POLLIN` 那种「半开」读数**：平台腿的 `wait_for` 只认请求的那一位，故此类读数按超时档处理，读侧不会因此结束。本沙箱**无从造出这一形态**——替身设备的掉线一律是 EOF（F9），节点消失后重开一律 ENOENT（F12），两条都走得到 `Closed`/`NoNode`。真串口上是否出现「插着但不回话」的形态属**推断／未验证**（AGENTS §4.2 第 11 条），`etest_serial_device` 落地时一并取读数再决定是否给读侧加一档硬错归因；本棒不预建。
- **逐标签连接类型图标**（D12）：随 `SPEC.FEAT.WS.04` 余腿单开。**〔已于 2026-10-22 出账，裁决 7.129——本条从「未落」清单移除，串口格的图标是 `S`，字形与侧栏同源一张表；本棒的示意图不追画（理由同 `codespec/UI_WORKSPACE_ZOOM.draft.md` §5 那条：手工生成的图插一行会牵动全图版面）〕**
- **RS-485 半双工、Lin/Modem 协议、流控下的吞吐调优**：需求未提，不预建抽象。
- **`ui::serial_format` 的「`CloseReason`→串口文案」一族不落**（§4 原先列了三族，批 3 只落两族）：掉线浮层是 `codespec/UI_RECONNECT.draft.md` 已裁的通用两态，其措辞按 `session::ReconnectStop` 分档而**不按 `CloseReason` 分档**，串口侧没有「按 `CloseReason` 出文案」的消费方——按 AGENTS §4.1 第 3 条「不加将来才用的抽象」不预建。代价如实登记：若后续要给串口单独一套掉线措辞，届时按到货的消费方新开，而不是现在就写一份没人读的表。
- **设备行「可见行产值」之外的第二种口径不落**：手输档切回列表档即清空设备名（见 §4 的批 3 补注），不保留「看不见但仍在草稿里」的值。代价：用户在两档之间来回切换要重敲一次路径。

---

## 8 裁决记录与探针复现

- **2026-10-11 收口**：D1–D13 经人裁为**全按推荐档**（无一条改判），入册 `SPECIFICATIONS.md` §7 裁决 **7.110**、`CHANGELOG.md` v0.110，`PLAN.md` 的 M3–M5 行与本稿状态同步改口。实现按 §4 布局分三批走：批 1＝档案模型 + `conn::serial_model` + `platform::serial` 两腿 + `flow` 键；批 2＝`conn::serial_connection` + `Session` 行尾折算 + 替身设备 `itest`；批 3＝向导第三型 + `ui::serial_format` + `main.cpp` 装配。
- **收口时点最需人判、已按推荐落定的三条**（留此备查，实现期若手感不合一律按 AGENTS §4.2 第 12 条以运行时为准回写）：D5（入口＝命令 `serial.set_baud`，藏在命令面板）、D7（串口 EOF 归 `LinkLost` 而**不**新增 `CloseReason` 枚举档；不可自愈类落 `Unknown` 即不重拨）、D11（折算点放 `Session` 写出侧一处，一处影响三路发送）。
- 探针复现口径（一次性脚本，不入仓）：§0 的 F1–F9 由 `probe_serial.py` / `probe_serial2.py` 两趟跑出（`os.getgroups()`、`/sys/class/tty/*/type`、`pty.openpty()` slave 路径重开 + `termios.tcsetattr` 逐档、`fcntl.ioctl(TIOCGSERIAL/TIOCMGET)`、写读环回与 EOF/EIO 分档）。**读数只在本沙箱成立**，换机器（真串口、`dialout` 组成员）后 F1/F5 两档须重取并回写本稿。
- **2026-10-11 批 2 落地**：`src/conn/serial_connection.{h,cpp}`（读线程 + 归因表 `serial_close_reason()` + 复用 `conn/reconnect.h` 的退避环 + `set_baud`/`current_baud` 两枚热改口，`resize()` 是有意空操作）、`Session` 的写出侧一处折算（`include/borealis/session/session.h` 尾追加 `line_ending` 入参，缺省 `nullopt`＝不折算），替身设备证人 `tests/integration/itest_posix_serial_connection.cpp` 七条，`utest_session` 四条折算用例。结项读数：`ctest --preset linux -E etest_` **75 项全绿**（新增 `itest_posix_serial_connection` 一项），runner **938 案全绿**（基线 927 + 11），该套件 403 ms。
  - 与稿面的三处出入已按 AGENTS §4.2 第 12 条回写：① §4 的 Session 公共头路径（稿写 `term/session.h`，实际在 `include/borealis/session/session.h`）；② §4/§6 的 itest 文件名（要带 `_posix_` 才走平台过滤，故 `cmake/BorealisTests.cmake` 那条过滤加宽成 `(utest|itest)_(posix|win)_`）；③ §5 的 `SerialConnection` 构造式（编码与行尾不在连接腿，是 `Session` 的构造入参）。
  - F11 的复现件：`mutex_probe.cpp`（两线程：持锁 sleep 20 ms × 200 次 vs 另一线程取锁计时）与 `mutex_probe2.cpp`（同形但取锁后立即放，作对照）——两份都在沙箱临时目录，不含本仓代码，跑出来是 2–4 s vs 0 ms。批 2 的实现原先在锁内做 20 ms 定时读，正是踩在这一读数上：整套 itest 从 403 ms 涨到 75–113 s，而 `strace -f` 下同一份代码 204 ms 跑完。

- **2026-10-11 批 3 落地**：`src/ui/connection_wizard.{h,cpp}`（类型卡三张、串口九行＝设备三件套 + 七枚循环按钮、四档问题码上屏、`reload_ports` 的四处空操作闸）、`src/ui/serial_format.{h,cpp}`（新，`connections.serial.*` 词条的唯一产出方）、`src/main.cpp`（串口会话构造吃 `encoding`/`line_ending`、枚举 latest-value 快照与两处投递、重启按档案分流、`serial.set_baud` 命令与两只共用一只 `Dialog` 的热改框）、`src/conn/profile_views.{h,cpp}`（`ProfileDraft.serial` + `DraftIssue` 串口四档 + 三向分派补 Serial 腿）、核对表七行 `Absent`→`Wired`。结项读数：`ctest --preset linux -E etest_` **77 项全绿**（+1＝`itest_connection_wizard`），runner 全跑 **950 案全绿**（批 2 后 938，+12），该套件 1.76 s。入册裁决 **7.111**、`CHANGELOG.md` v0.111。
  - 与稿面的三处出入已按 AGENTS §4.2 第 12 条回写：① §4 把 `itest_connection_wizard` 记成「修改」，本仓此前**没有这一件**（实为新增，七条用例）；② §4 的 `serial_format` 三族只落两族，「`CloseReason`→串口文案」没有消费方（§7 登记代价）；③ §4 的 `DeviceDropdown` 落地补一条硬口径——子类构造**必须**把 `initial` 转发给基类，否则编辑串口档案时下拉恒显占位、保存把 `device_path` 清空（实现期由用例⑥ 当场抓到的真缺陷，不是稿面口径问题）。
  - 两条落地期才见分的口径（§2 标号 2/3 与 §4 已回写）：**设备行产值取当前可见的那一行**（互斥切换即重建，退场那份不留值）；**置灰经 `paint_item` 扩展点、否决经 `set_on_change` 回退**——`Dropdown::select()` 非虚且同 index 不回调，所以这不是框架缺口而是应用侧组合（AGENTS §5 第 3 条）。
  - 一次枚举的闸门经变异判为死重量并删除：`ports_requested_` 整条摘除后七条用例**全绿**（该标志不承载行为），反向把一次发起塞进 `build_content()` 则用例③ 由 1 次涨到 4/5/9 次转红（承重的断言是「重建不得再发后台取数」的计数）。据此按 AGENTS §4.1 第 3 条收掉该抽象。另一处变异：摘掉置灰行的 `set_on_change` 否决 ⇒ 用例④ 转红。
