#pragma once

// ============================================================
// 设置面板的界面腿本体（src/ui/settings_panel.h）
// ------------------------------------------------------------
// `codespec/UI_SETTINGS.draft.md` 屏 3 的落地（裁决 7.52 把 S1~S16 全部自拍为建议项）：
// 交付**四页骨架 + 六类通用行控件 + 主题卡区段 + 16 格色板区段 + 字体族选择器 + 回退链区段 + 实时预览盒 + 状态栏开关组与组顶说明 + 快捷键只读表 + 表单落盘与广播接线**，
// 其余按判据文 §8 的分工留给后续棒——损坏配置的启动对话框。
//
// 延后档位在界面上的表达（人已拍板，裁决 7.68③；判据文 S15① / B3-a / C2-a 那句「灰置」据此就地更正）：
// 反向核对表里 `ConsumerStatus::Absent` 的那批行（状态栏十枚、终端页三条、SSH 五项、串口六项）**一律可用**
// ——改动照常过表单校验、照常落盘，只是不广播（`apply_scope()` 把「无消费方 ∧ 即时」折成 `PersistOnly`）。
// 「这条改动当下不会生效」因此只由两处文字承担：行尾的「延后」角标，与落在该组第一行之前的组顶说明
// （`visible_notes()`，措辞须点名是哪个消费方还没开工）。本件不用灰置表达延后，因为把可用控件画成
// 不可用会让「四分类骨架全建」这句在交互面上不可验证。
//
// 预览盒（S7 / 判据 F-a~F-e，裁决 7.66）落在**卡片底部一条 140 dp 的横条**（人已拍板的落位，不是稿面
// 原先那个「右侧」形态），本体是 `SettingsPreview`（`settings_preview.h`：独立内存会话 + 真实视口控件）。
// 面板因此在 `Hooks` 上开三条预览接缝；其中 ①③ 任缺其一即整条横条不画（预览是可选腿，面板既有用例
// 都不必为它建会话，而「装了一半」在结构上不成立），②缺了只是不排下一帧：
// ① `preview_appearance` 交的是**装配层那一份**外观包，而不是面板自己拼的——启动初始构造、运行期即时
//    广播与预览盒三处必须同源（裁决 7.53「构造与广播共用一对搬运函数」的延伸），面板不认识 `config`，
//    自拼一份就是第二条搬运路径，「面板改了某项而预览拿不到该项」正是那一对函数要消除的形态。
// ② `preview_wake` 是排帧请求：夹具在视口的 `on_layout` 里随 `resize` 重投，那一批脏**要下一帧才排**，
//    不唤醒就停在「横条画了但内容还是上一版」。宿主侧的泵（`pump_preview()`）与这条请求配对存在。
// ③ `preview_defaults` 与 ① **成对且缺任一即整条横条不画**（裁决 7.76③）：三条会话初始档（光标形态 /
//    闪烁档 / Ambiguous 口径）是**构造期**注入，预览那条会话建好就不再重建，故它没有 ① 那种运行期补偿。
//    漏装不会报错，只会在同一屏里画出与主视口不同的光标——`Bar` / `Underline` 两档在绘制侧先于失焦
//    降级落笔，而预览恒是失焦态。两条接缝合成一条更省，但那要把 `TerminalDefaults` 塞进外观包，
//    而外观包恰是运行期可换的那一份，构造期注入混进去就成了「看起来能改其实只在建会话时吃」的假档位。
//
// 三条决定形态的框架实测（裁决 7.56，其前两条曾登记为缺口 G26 / G27 并已于同日回货闭合，见裁决 7.57①）：
// ① 浮层必须是**确实写子节点 bounds 的容器**：登记时 `Dialog` 与 `Scroll` 都不写 bounds，挂在
//    `OverlayHost` 上就是抓不住的死浮层。故本件用 `Stack`（遮罩 `Canvas` + 卡片）承载 `Column` 卡片，
//    行区**原本**取 `LazyList`。两条回货后本件**判定不迁回** `Dialog`（内层 `self × 0.8` 约束、遮罩不关面板、
//    `show()` 带模态作用域，三条均与 S1 拍的同窗口非模态浮层不合，见裁决 7.57③）。
//    行区随后由 `LazyList` 改成 `Scroll` + `Column`（裁决 7.60）：`LazyList` 的 `item_extent` 是全局固定
//    行高，而外观页的四类专用区段（主题卡 / 16 格色板 / 字体族 / 回退链）各要占多行，固定行高表达不出来；
//    `Scroll` 在 G27 回货后确实写内容 bounds 且把命中点按 `offset_y_` 折回内容坐标，全量实例化的代价
//    （每页 ≤30 行）因此换来可变行高，本件另配「滚过一段之后真点一行控件即提交」的证人守住这条腿。
// ② `OverlayHost` 给浮层的是**松约束**（min 0、max 自身），而 `Canvas` 的自动尺寸会夹到 100×100，
//    故遮罩层须挂 `Modifier{}.fill_max_size()` 才铺满整窗；卡片用 `LayoutBuilder` 按实际可用尺寸钳位，
//    于是稿面 1500×900 dp 在 960×640 dp 的窗口里不会溢出（S1「同窗口浮层」的物理前提）。
// ③ `Button` 的标签绘制曾绕过 `StringTable::resolve()`（缺口 G28，已回货闭合，见裁决 7.59）：现在框架
//    自己的 `resolved_label()` 就是它 `on_layout` / `on_paint` 的唯一显示串来源，故按钮文案可直接交
//    `LocalizedString`（`settings_text()`）与 `Text` 同源。`settings_label()` 仍留给收 `std::string`
//    的入口（`Text::placeholder`、角标等）——那些入口本就没有查表路径。
//
// 一条曾在册的派发限制（G29 回货只闭合一层，其残段登记为 G30，**该残段亦已于同日回货闭合**，见裁决
// 7.62）：`Dropdown` 的展开选项列画在主框之外而不占布局，而嵌套在容器里的溢出区进不了真实派发链（祖先
// 按 `child.bounds()` 判包含），故「真点一个选项即提交」在回货前写不成判据。回货形态是
// `Widget::extra_hit_box()` 的申报**沿祖先链聚合**（`covers_extra_hit_box()` = 自身申报 ∪ 子树聚合，
// 逐子按 `bounds().origin` 折算），于是面板每一行 `Column → Row → Dropdown` 的三层嵌套都能把孙辈的申报
// 递到祖先那道下降闸，那条现状钉子随之翻成正向行为用例。同批回货另两处：`Dropdown::panel_box` 改为可
// 向上翻转、按窗口限高且可滚；而原缺口里「兄弟行压住面板」那一条不修（`Container::on_paint` 前向绘制而
// 链逆序下降是框架的既有语义），代之以一条**形态约束**——列表行内不得使用「覆盖绘制而不占布局」的控件。
// 接货时另实测到两条，同批回货闭合（裁决 7.64）：**G31** 是派发落点的折算缺一项（祖先的 modifier
// translation 没进 `HitNode.origin`），行内控件收到的本地坐标比其绘制位置偏一个行上内边距，按该坐标反算
// 的档位因此错一格；回货形态是命中侧与绘制侧收敛为同一个 `content_origin`，本件那两条拆开的用例（「行外那
// 一段可达」与「点得准」）各自翻正。**G32** 是 `Node::~Node` 结尾无条件 `set_layout_parent(nullptr)`——
// Node 是可共享句柄，副本析构时控件仍在世，于是任何一次临时句柄析构都抹掉活控件的布局父指针，后代脏标记
// 沿父链上溯时被静默丢弃；回货改由父侧在真正摘除时清，本件为断链多补的那一层祖先脏随之撤除（框架侧把那种
// 「应用侧顺手多标祖先脏」判为掩盖而非修复）。本件不为此自造覆盖层或改写挂载点（不等不绕，裁决 7.13①）。
//
// 外观页两个区段（主题卡 / 16 格色板）的三条口径，都是判据文与代码相撞处（裁决 7.61 拍板）：
// ① 卡片名**逐字取 `themes.h` 的存储键名**（`dracula` 而非 `Dracula`）。判据 A1-a 那句「显示名逐字等于
//    `themes.h`」在代码里没有第三个可指的对象——预置表只有 `name` 一个字段，另立一张中文名表就是视觉稿
//    与 schema 之外的第二份真值源（同 §3 第 1 条「以代码为准」的处置）。
// ② 卡片的四格样例取**前 / 背 / 光标 / `basic[1]`**。A1-a 写的第三格是「强调」，而 `PaletteSpec` 里没有
//    强调色、八套预置的 `ChromeOverride::accent` 又全为空（chrome 覆盖不在首版，裁决 7.26②），故那一格
//    没有数据来源；光标色是同一份色板里既真实又与前后两格拉开对比的那一档。
// ③ A2-a 的「点一格 → 下方给出该格 HEX 输入」实现为**把常驻的那一个输入框指向所点的那格**，而不是每格
//    现建一个输入框——本条真正禁止的是「16 个常驻输入框」，而 `Modifier` 没有可见性位（裁决 7.60③），
//    按创建/销毁去表达「只有选中格有输入框」就得整块重建浮层，那会把滚动偏移与输入焦点一起抹掉。
//
// 三个区段的当前值都**不进控件自己的存储**：每张卡、每一格都是一个读 `form_` 的绘制闭包，改动之后
// 只 `mark_needs_paint()`。于是「面板显示的色」与「表单里的色」在结构上不可能分叉，代价是重绘由本件显式
// 触发而不是由 reactive 值驱动。
//
// 回退链区段（#114 第二棒，判据文 A5-a / 裁决 7.52 的 S8）的五条框架实测决定了它的形态：
// ① `ReorderableList::on_layout` 在**无界约束**下回落 `Size{320, 480}`，而行区给每一行的是紧约束宽度、
//    高度由内容自定；不锁高度就会得到一个 480 dp 的空洞。故区段按 `clamp(条目数, 1, 可见档数) × 行高`
//    显式锁高（条目数在构建闭包里是已知的——`build_row` 按 `form_` 的当前链建它）。
// ② `set_drag_handle(true)` 的那一条 48 dp 手柄带由**列表自留**：`on_hit_test_chain` 对带内的落点返回空表
//    （框架注释自陈：条目自带 `Button` 会消费 Press 令整项拖拽起不来，故手柄带是列表自己的作用域），
//    而列表**不画 grip**。于是每行的上移 / 下移 / 移除三枚按钮必须落在 `width - 48` 之左，带内由本件自绘
//    一幅 grip 点阵。取 `drag_handle(false)`（整项可拖）则条目里的按钮全部抓不到 Press，故不可选。
// ③ 面板的 `Escape` 挂在 `ShortcutScope::Global`，而全局快捷键**先于任何控件**消费（裁决 7.51③ 理由 (a)），
//    故链正被抓在键盘上时 `Escape` 会关掉面板而不是放下那一项。处置是在关闭闭包里先试
//    `cancel_keyboard_grab()`，它返真就原地不动——属**交接而非缺口**（同裁决 7.58 对 chrome setter 的判法）。
// ④ 指针拖拽的落位要经每帧 `tick`（`end_drag()` 只在 `reduce_motion` 或位移不足 0.5 dp 时立即提交），
//    无头通道不跑帧泵，故换位判据一律走上移 / 下移按钮与键盘 Drop 两条同步路径；拖拽归框架自有用例与
//    真机走查，本件不伪造绿灯。
// ⑤ `show` 是**测量输入而不是脏源**：`Widget::layout` 在它为假时直接回零盒（且早于布局缓存那一支），而
//    写它不标脏布局，于是候选池翻转之后祖先按缓存复用「零盒」那次的尺寸，按钮既量不出高度也进不了命中
//    链，故翻转的叶子各补一次 `mark_needs_layout()`（实测见 `refresh_chain_candidates()`）。登记时补的
//    第二处（卡片外层那只 `LayoutBuilder`）是断链之下「叶子那份脏到不了渲染根」的补救，随 **G32** 回货撤除。
//
// 「添加族」不取判据文 S8 ① 写的「下拉」，理由三条（裁决 7.62）：本机字体目录实测 200+ 族，而 G30 回货
// 虽然把浮层做成了可翻转 / 限高 / 可滚，同批却立了「列表行内不得使用覆盖绘制不占布局的控件」那条形态
// 约束——下拉的面板正是不占布局、只靠 `extra_hit_box` 申报才可达的那一类（登记时另有 G31 那条派发坐标
// 漂移，已随同日回货闭合，而这条形态约束仍在），
// 把 200+ 档塞进**行内**下拉既违该约束也不是可判形态；`LazyList` 按序号回收条目而过滤会把命中序号交给
// 派发链；框架的链上限截断发生在绘制侧而对用户不可见（`TextLayoutOpts::with_fallback_chain` 截到
// `AURORA_TEXT_FALLBACK_CHAIN_MAX`）。故换成**过滤输入框 + 固定候选行池**：常驻 N 枚按钮，按键时只改
// `set_label` 与 `show`（`Reactive<bool> show` 为假时尺寸是零盒且不进命中链，零盒仍占布局故不属上面那条
// 形态约束），代价是候选区一次最多列 N 项、且不再是下拉。
//
// 字体族选择器（A4，人已拍板：行内按钮 + 浮层，裁决 7.69）走的是**另一条**形态：行内一枚常驻按钮
// （标签＝实际生效的族名），点击后在场景根的浮层宿主上弹一列候选。这样落恰恰**避开**上面那条形态约束
// ——候选列是 `au::Popup` 挂在 `OverlayHost` 上的一层独立浮层（占布局、由宿主派发），不是画在列表行内
// 而不占布局的覆盖绘制，故不需要 `extra_hit_box` 申报，也不受行盒高度与祖先闸的约束。三条口径：
// ① 候选池＝目录里 `.monospace` 为真的全量族名、族名逐字节字典序，「当前配置值」与
//    `kDefaultMonospaceFamily` 固定置顶两档（同值合一）；超出可视高度靠浮层内部 `Scroll`。不分页、
//    不加过滤框（A4-d）。目录仍只经 `Hooks::families` 取一次（A4-a / S16），本件不第二次枚举。
// ② 回落留痕画在字体行**下方另起一行**，而不是把按钮标签显示成那个不存在的族（A4-b）：生效族名由
//    `ui::choose_font_family` 判，`Configured` 那档没有留痕可写。代价是这一行恒占两行高度，即使
//    配置里的族名可用。
// ③ 浮层的锚点取框架 `Widget::window_bounds()`（A4-e）：它查询时沿布局父链现算、途经滚动宿主即按该
//    宿主的偏移修正，故恒为**窗口逻辑 dp**，配 `size().height` 得下沿，而 `Popup::open_at` 收的正是这一
//    坐标（浮层挂在场景根的宿主上、不在滚动缓冲里）。此前本件为此写过的过渡形态（私有的记锚点子类自记
//    同一次 Press 的坐标对 ＋ 面板自持行区现读 `offset_y()` 折算）**已随 G36 回货按预诺撤除**：那条坐标对
//    给的是**内容坐标系**里的盒原点（实测差值恰等于该 `Scroll` 当时的 `offset_y()`，修复前那一次是 834 对
//    451，差 383），折算就是把框架私有算式复制进本仓。
//    同样不得拿 `paint_bounds()` 当锚点：那是**第三个**坐标系（录制进离屏缓冲时传入的盒原点是
//    `-buffer_origin_y_`，登记 G36 时同帧实测 748 对 826 差 78 dp，而那个量公共面取不到），并且会随重建/未绘制而陈旧。
//    关掉它的四条腿：点候选、`Escape`（先于面板的关闭）、点遮罩（A4-c：遮罩那枚 `clickable` 在浮层开着
//    时**只关浮层**，否则第一次外部点击把整块面板一起撤掉）、关面板。摘除浮层的次序按序号**降序**
//    （先浮层后面板），因为 `remove_overlay` 的界是按当前子节点表算的，先摘靠前的那个会让后面的序号
//    整体前移一格。
//    候选的点击回调里**不**调 `remove_overlay`，只 `close()` 而复用同一只 `Popup`：关掉即 `on_paint` 早返回、
//    `on_hit_test_chain` 回空表，既不可见也不可达，而控件仍由本件持有故不悬垂。就地摘除的形态按框架自陈
//    有「在派发栈内销毁正在派发的子节点」之忧（`OverlayHost::remove_overlay` 走 `children_.erase`，其后派发器
//    还要读该链），本仓实测**未见其崩**——注入那条之后只有一例的浮层计数转红，故此条根据是框架自陈而非
//    本仓抓到的现场；另一条本仓自己的理由是摘了下次还得重新 `add_overlay` 并改序号。
//
// 快捷键页（D 页，判据文 D1-a / D2-a / D3-a，裁决 7.72）是**第五个区段**而不是第六类控件形态：反向核对
// 表里该页只有 `shortcuts.overrides` 一行、其控件形态是 `ReadOnlyTable`，而表体要按**命令**逐行画，故
// 与主题卡 / 色板 / 字体族 / 回退链同样在 `build_row` 的分派处拐出去建。三条口径：
// ① 本件**不做任何比对**：行序、每行的四列文字与冲突标注全部来自 `ui::build_shortcut_rows()`（纯逻辑
//    件，吃 `term::KeyPress`），面板只把算好的行落笔。于是「标注来自实际比对而不是界面自己比字符串」
//    这句 D2-a 有唯一证人——面板拿不到键位语义值，想自己比也比不了。
// ② 表体**自绘**而不借框架 `data_widgets.h` 的 `DataTable`：那三件表控件的 `on_paint` 形参一律不读
//    ctx，故 `Theme` 到不了、色值硬编码浅色（裁决 7.68①），深色面板上用它是第二个 chrome 色值源。
// ③ 该页**没有可交互控件**，故 `visible_rows()` 那一行给 `editable=false` 而 `summary` 留空：前者是
//    诚实（首版只读，无提交入口），后者是「内容已画进区段，摘要即第二份显示形态」（与其余四个区段
//    「可交互行的摘要一律留空」那条通则同族，只是本行的所以是「只读」而非「可交互」）。
//
// 面板不认识 `config`，也不认识 `TerminalView`：装载 / 落盘 / 广播三条接缝由 `Hooks` 交装配层兑现
// （`config/settings.h` 已 include `ui/palette.h`，反向 include 即 `config ⇄ ui` 模块环，与
// `settings_catalog.h` 同一条理由）。于是「即时生效」到底改哪些对象，是装配层的一次快照搬运，
// 面板只负责在提交通过、且该行 `apply_scope()` 为 `PersistAndApplyNow` 时把整份表单交回去。
//
// 文本提交的时机是 S14 的硬约束，兑现为类型而非约定：文本行用本件私有的 `BlurCommitText`
// （`TextInput` 子类，覆写 public virtual 的 `on_focus_change`），**只有失焦与 Enter 才把文本交进
// 表单**，逐字符变化一个字节也不提交——半截输入 `#12` 因此不会落盘也不会生效。
//
// 私有头（裁决 D1① 同口径）：本件含框架类型，不进 `include/borealis/`。
// ============================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aurora/app/shortcuts.h"
#include "aurora/render/painter.h"
#include "aurora/state/state.h"
#include "aurora/theming/theme.h"
#include "aurora/widget/button.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/layout_builder.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/reorderable_list.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"

#include "borealis/ui/font_choice.h"
#include "borealis/ui/settings_catalog.h"
#include "borealis/ui/settings_form.h"
#include "borealis/ui/shortcuts_table.h"
#include "terminal_view.h"  // `Hooks` 的两条预览接缝吃 `TerminalView::Appearance`（同 `workspace_view.h` 的先例）

namespace borealis::ui {

class SettingsPreview;  ///< 卡片底部那条横条的本体（`settings_preview.h`），本件只持其所有权。

/// @brief 本仓缺省 chrome 的那几档色值（面板卡片与搜索浮层条体共用一份表）。
///
/// 判据文 `codespec/UI_SEARCH.draft.md` 的 A1-h 把浮层条体钉在「与设置面板同一条 S5 口径」上——
/// 条内文本、输入框底、chip 选中档一律从那张缺省 chrome 表取，于是这张表必须是**一个可取的量**
/// 而不是 `settings_panel.cpp` 的文件内常量；否则浮层要么 include 那个 cpp 够不着的头、要么在自己
/// 文件里再列一遍同样的七个十六进制数（第二真值源，裁决 7.46② 避开的那一型）。
struct SettingsChrome {
    aurora::Color window_bg{};   ///< 场景根底色（同时是 `accent` 档上的前景色，即 ThemeScope 的 on_primary）。
    aurora::Color card_bg{};     ///< 卡片底 / 浮层条体底。
    aurora::Color card_line{};   ///< 卡片描边 / 控件描边。
    aurora::Color accent{};      ///< 选中档（chip 的开档底、聚焦描边、主按钮）。
    aurora::Color text{};        ///< 正文。
    aurora::Color text_dim{};    ///< 副标题、占位符、关档前景。
    aurora::Color control_bg{};  ///< 输入类控件底与次级按钮底（chip 的关档底）。
};

/// @brief 面板与浮层的 chrome 色值唯一来源（上面那张结构的现值）。
[[nodiscard]] auto settings_chrome() -> SettingsChrome;

/// @brief 面板 chrome 的色值唯一来源，同时是装配层给场景根那层 `ThemeScope` 的主题。
///
/// 刻意**不随终端主题联动**（裁决 7.52 的 S5① / N6：终端配色与界面配色是两件事，切主题时整窗
/// 跟着变会让用户以为丢了设置）。面板内联色与本函数取自同一批文件内常量，故全仓只有一份 chrome 色值。
[[nodiscard]] auto settings_chrome_theme() -> aurora::Theme;

/// @brief 设置面板：挂在场景根浮层宿主上的全屏浮层（S1），装载一份表单副本并就地编辑（S3①）。
///
/// 生命周期：`open()` 装载副本、建浮层并登记 `Escape` 的全局快捷键（S2①）；`close()` 撤浮层并**解绑**
/// 那条快捷键——留着它，面板关闭后 `Escape` 就会静默吞掉发往会话的按键。析构时若还开着同样收口。
class SettingsPanel {
public:
    /// @brief 主题卡区段的一条候选：存储键名 + 该主题的整套色板。
    ///
    /// 由装配层从 `config::builtin_themes()` 搬值（同 `TerminalView` 的字体族目录那条分工，裁决 7.46③）：
    /// 本件含框架类型而 `config/settings.h` 已 include 本域头，面板直接 include 它就是 `config ⇄ ui` 模块环。
    /// 色板整份交出而不是只交名字：卡片样例、切主题时写入的六格、以及「恢复主题默认」的基线都取自它。
    struct ThemeChoice {
        std::string name;          ///< 存储键名，同时就是卡片上显示的那串字（见文件头①）。
        PaletteSpec palette{};     ///< 该主题的整套色值。
    };

    /// @brief 一份配置快照（`SPEC.FEAT.PREF.07` 的滚动档），面板侧形态。
    ///
    /// 交的是路径与本域结构而不是 `config::SnapshotInfo`：面板不 include `config`（模块环，与
    /// `ThemeChoice` / `FontFamilyEntry` 同一条理由）。`path` 原样交回 `Hooks::rollback` 而不被本件
    /// 解析——存储侧只接受自家快照目录里那份名单中的路径（裁决 7.87⑤），面板只是传话人。
    /// 界面上那一行显示的是 `path.filename()` **逐字**（与降级对话框把备份路径逐字上屏同口径，
    /// 裁决 7.76⑤）：把 epoch 折成本地时间要经 `localtime` 一族，那是平台相关分支（AGENTS.md §4.5
    /// 第 23 条），而文件名本身已经带秒。
    struct SnapshotEntry {
        std::filesystem::path path{};  ///< 快照文件路径。
        std::int64_t timestamp_epoch{};  ///< 命名里的 epoch 秒（同一秒内的多份靠序号分先后）。
        std::uint64_t size_bytes{};    ///< 文件字节数；存储侧读不到时为 0。
    };

    /// @brief 面板与存储侧、绘制侧的全部接缝（struct-of-回调，同 `WorkspaceView::Hooks` 的形态）。
    ///
    /// 前三条都按**整份表单**说话而不是按单键：落盘要的是「一次替换 + 一次 flush」，广播要的是
    /// 「一份完整配置搬进控件」，逐键接缝会把落盘拆成 N 次写文件。
    struct Hooks {
        /// @brief 取当前配置的每个落盘叶子键（装配层经 `config::form_entries()` 搬值）。
        std::function<std::vector<FormEntry>()> load;
        /// @brief 把整份表单写回存储。
        /// @return 成功为空；失败为 ASCII 原因（`Store::replace()` 在备份未成功时拒绝落盘）。
        std::function<std::optional<std::string>(const SettingsForm &)> persist;
        /// @brief 把整份表单的当前值搬进运行中的视口（仅「已接线 ∧ 即时」的提交会触发）。
        std::function<void(const SettingsForm &)> broadcast;
        /// @brief 取主题候选（卡片次序即此表的次序，装配层从 `config::builtin_themes()` 搬值）。
        std::function<std::vector<ThemeChoice>()> themes;
        /// @brief 取字体族目录（回退链区段的候选来源，装配层从**主窗口共用那一份**目录搬值，S16）。
        ///
        /// 交的是 `ui::FontFamilyEntry` 而非框架的 `render::FontFamilyInfo`：本件已有这条搬运纪律
        /// （裁决 7.46③，目录只在装配阶段枚举一次，`list_font_families()` 首次调用是同步 IO）。
        /// 候选**不**按等宽性过滤——回退链的存在理由正是「主族缺字时找另一个面」，另一个面不必等宽。
        std::function<std::vector<FontFamilyEntry>()> families;
        /// @brief 取预览盒的外观包（S7 / F-c：与主视口**同一份**，由装配层的 `make_appearance()` 交出）。
        ///
        /// 不装即不画那条横条：预览是可选接缝，面板的既有用例都不必为它建会话。
        std::function<TerminalView::Appearance()> preview_appearance;
        /// @brief 取预览会话的初始档（光标形态 / 闪烁档 / Ambiguous 口径，裁决 7.76③）。
        ///
        /// 与 `preview_appearance` **成对**：三条都是「建会话那一刻取用」的构造期注入，而预览盒的会话
        /// 在面板里建一次就不再重建，故它拿不到运行期入口那份补偿——漏装这一条就画出一个与主视口
        /// 不同的光标（`Bar` / `Underline` 两档在绘制侧**先于**失焦判定落笔）。两条接缝任缺其一即
        /// 整条横条不画，「装了一半」在结构上不成立。
        std::function<term::TerminalDefaults()> preview_defaults;
        /// @brief 请求宿主排下一帧（夹具在视口 `on_layout` 里随 `resize` 重投，那批脏要下一帧才排）。
        std::function<void()> preview_wake;
        /// @brief 取快捷键只读表的行源（装配层从框架 `CommandRegistry::all()` 折成
        ///        `ui::ShortcutCommandEntry`，裁决 7.72②；不装即该页只有表头）。
        ///
        /// 交的是本域形态而不是框架 `Command`：`KeyCombo` → `term::KeyPress` 的互转点因此只有装配层
        /// 一处（面板拿不到键位语义值就无法自己比第二次，而 D2-a 那句「标注来自实际比对」的唯一证人
        /// 就是面板只读得到纯逻辑件算好的那一列）。
        std::function<std::vector<ShortcutCommandEntry>()> commands;

        /// @brief 取当前留存的配置快照，**新→旧**次序且至多 `config::kSnapshotRetention` 份。
        ///
        /// 六条 `SPEC.FEAT.PREF.07` 的接缝**成组**才画动作按钮（`config_actions_ready()`）：面板里不存在
        /// 「一个点了没反应的按钮」（7.38⑥ F-b 的同一口径），故既有用例那些只装前三条的夹具天然不受影响。
        /// 每次进快照视图现取而不缓存：回滚与导入都会顺手把当前现场复制成新一份快照（裁决 7.87②），
        /// 名单在那一次动作里就已经变了。
        std::function<std::vector<SnapshotEntry>()> snapshots;
        /// @brief 回滚到名单里的那一份。
        /// @return 成功为空；失败为 ASCII 英文原因（只进日志，上屏文案一律走词条表，S13②）。
        std::function<std::optional<std::string>(const std::filesystem::path &)> rollback;
        /// @brief 把当前配置导出到给定文件。
        std::function<std::optional<std::string>(const std::filesystem::path &)> export_config;
        /// @brief 从给定文件导入配置并生效。
        std::function<std::optional<std::string>(const std::filesystem::path &)> import_config;
        /// @brief 让用户给出导出的目标文件；**空串＝取消**（与「空串即该来源未设置」同一条让位口径）。
        ///
        /// 取文件这件事**不经**面板：装配层那条腿走平台对话框（`aurora::file_dialog`，起不来时与取消
        /// 同一个空串），面板只判两档——「取消即整条不跑」与「给出路径就把这条路径原样交下去」。落文件
        /// 的判据归 `export_config` 那一条，故本件对它只测接线而不测产物。
        std::function<std::string()> pick_export_path;
        /// @brief 让用户给出要导入的文件；空串＝取消。
        std::function<std::string()> pick_import_path;
    };

    /// @brief 面板当前页上的一行（用例据此核对「面板画的键」与反向核对表一致，判据文 §8 判据①）。
    ///
    /// `badge` 与 `summary` 是这一行的两列文字，交出来而不只画在树上：两者都由 `settings_catalog()`
    /// 的两列正交字段折算，用例判的是「表说了什么」对「面板给了什么」，读浮层树里的 `Text` 反而要把
    /// 排版坐标也算进判据。
    struct VisibleRow {
        std::string key;       ///< 落盘点号路径。
        ControlKind kind{};    ///< 控件形态。
        bool editable{};       ///< 该行的控件是否可交互（`Absent` 行**可**交互而只落盘；false＝首版只读的快捷键表）。
        std::string badge{};   ///< 角标文案（「延后」/「下次会话生效」/两者并列，主题行另挂「自定义」），无角标为空。
        std::string summary{}; ///< 未落地形态的只读值摘要；区段化的行一律留空（内容就在它自己的区段里）。
    };

    SettingsPanel(aurora::OverlayHost &host, aurora::ShortcutRegistry &shortcuts, Hooks hooks);
    SettingsPanel(const SettingsPanel &other) = delete;
    auto operator=(const SettingsPanel &other) -> SettingsPanel & = delete;
    SettingsPanel(SettingsPanel &&other) = delete;
    auto operator=(SettingsPanel &&other) -> SettingsPanel & = delete;
    ~SettingsPanel();

    /// @brief 打开面板：装载表单副本、挂浮层、登记关闭快捷键。已开着则只刷新浮层。
    auto open() -> void;

    /// @brief 关闭面板：撤浮层并解绑 `Escape`。有未落盘的改动照旧保留在副本里（落盘是提交那一刻的事）。
    auto close() -> void;

    /// @brief 面板是否开着。
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 切页：按新页重建浮层。条目数变了可用 `LazyList::set_count` 就地改，但本件仍整块重建——
    ///        序号 → 键的映射随页而变，只改条目数会让旧页的条目按新页的序号复述。
    /// @param page 目标页。
    auto select_page(SettingsPage page) -> void;

    /// @brief 当前页。
    [[nodiscard]] auto current_page() const noexcept -> SettingsPage {
        return page_;
    }

    /// @brief 当前页的行表，次序即面板的排版次序（＝反向核对表内该页的次序）。
    [[nodiscard]] auto visible_rows() const -> std::vector<VisibleRow>;

    /// @brief 当前页画出的组顶说明，次序＝行区次序（判据文 A9-a / C2-a；无说明的页回空表）。
    ///
    /// 交出来而不是只画在树上：`Absent` 那批行现在**可改可落盘**（裁决 7.68③），界面上表示延后的
    /// 只剩两处文字——行尾角标与组顶说明，故「这一组的消费方还没开工」那条判据只能比文字。
    [[nodiscard]] auto visible_notes() const -> std::vector<std::string> {
        return note_labels_;
    }

    /// @brief 面板自持的那份表单副本（用例读它核对「改了又改回来不脏」这类判据）。
    [[nodiscard]] auto form() const noexcept -> const SettingsForm & {
        return form_;
    }

    /// @brief 非文本控件的提交入口（开关 / 步进器 / 下拉 / 色槽按钮的回调都落在这里）。
    /// @param key 落盘点号路径。
    /// @param next 新值。
    /// @return 未通过的原因；通过为 `CommitIssue::None`。
    ///
    /// **不加 `[[nodiscard]]` 是有意的**：控件回调那条路丢弃返回值，而失败原因已在 `after_commit` 里
    /// 写进该行的状态列，界面上不存在「静默失败」的通路；返回值只服务用例的校验断据（判据文 §8）。
    auto commit(std::string_view key, FormValue next) -> CommitIssue;

    /// @brief 文本行的提交入口（S14：失焦或 Enter 才走到这里）。
    /// @param key 落盘点号路径。
    /// @param text 文本框当前内容。
    auto commit_text(std::string_view key, std::string_view text) -> CommitIssue;

    /// @brief 把一个可缺省色槽置为「未配」（A2-b 的另一态）。
    /// @param key 落盘点号路径。
    auto commit_unset(std::string_view key) -> CommitIssue;

    /// @brief 改 16 格色板的一格（A2-a：逐格点选，提交走该行的 HEX 输入框）。
    /// @param key 该行的路径，须为 `ColorTable` 域。
    /// @param slot 格序号 0..15。
    /// @param text 该格的 `#RRGGBB` 文本。
    auto commit_slot(std::string_view key, std::size_t slot, std::string_view text) -> CommitIssue;

    /// @brief 主题卡区段的当前状态，次序＝候选表次序。
    ///
    /// 交出来而不是只画在树上：A1-a 的三条判据（名字逐字、四格样例、选中卡在右下角给勾）都要与
    /// `config::builtin_themes()` 比，读浮层树里的 `Text` 反而要把排版坐标也算进判据。
    /// `samples` 是四格样例的 `#RRGGBB` 文本，次序＝前 / 背 / 光标 / `basic[1]`（文件头②）。
    struct ThemeCardView {
        std::string name{};                  ///< 卡片显示名。
        bool selected{};                     ///< 是否是当前表单点名的那一套。
        std::array<std::string, 4> samples{};
    };

    /// @brief 主题卡区段的当前状态（候选表为空时为空表）。
    [[nodiscard]] auto theme_cards() const -> std::vector<ThemeCardView>;

    /// @brief 当前 palette 六格是否与该主题名对应的预置色板不逐槽相等（判据 A1-b 的「自定义」状态）。
    ///
    /// 主题名不在候选表里时**没有可比基线**，此时判为已改过：界面无法声称当前色板是任何一套的默认。
    [[nodiscard]] auto is_palette_customized() const -> bool;

    /// @brief 第 index 张主题卡的控件（用例据此按真实命中链点它）；越界或该区段未画时为空。
    [[nodiscard]] auto theme_card(std::size_t index) const -> aurora::Widget *;

    /// @brief 第 index 格色板的控件（同上）。
    [[nodiscard]] auto swatch_slot(std::size_t index) const -> aurora::Widget *;

    /// @brief 色板编辑器那个常驻 HEX 输入框的控件（用例读它的 `value()` 判 A2-a 的「当前值」）。
    [[nodiscard]] auto swatch_input() const -> aurora::Widget *;

    /// @brief 色板编辑器当前指向的格序号。
    [[nodiscard]] auto selected_swatch() const noexcept -> std::size_t {
        return selected_swatch_;
    }

    /// @brief 回退链区段的一行：族名与两个可移动方向。
    struct ChainItem {
        std::string family{};   ///< 族名，逐字来自表单里的那一份。
        bool can_move_up{};     ///< 不是首项（首项的上移按钮是禁用态，7.38⑥ F-b 的同一口径）。
        bool can_move_down{};   ///< 不是末项。
    };

    /// @brief 回退链区段的当前状态。
    ///
    /// 与 `theme_cards()` 同一条理由：A5-a 的三条判据（顺序可改、逐行可删、末尾追加）都要与表单里的
    /// 那份链比，读浮层树里的 `Text` 反而要把排版坐标也算进判据。`candidates` 是**已过滤、已排除链内
    /// 族名**之后、真正摆在候选池里的那几档（次序即池内次序），故用例能判「池宽是否随输入变窄」这一类
    /// 只属于本区段的行为。`hint` 是已解析的提示文案（空链 / 已达上限 / 已截断 / 没有匹配四档之一）。
    struct ChainView {
        std::vector<ChainItem> items{};
        std::vector<std::string> candidates{};
        std::string hint{};
        bool at_capacity{};  ///< 链长已达框架上限（`AURORA_TEXT_FALLBACK_CHAIN_MAX`），追加口关死。
    };

    /// @brief 回退链区段的当前状态（区段未画时 `items` 为空）。
    [[nodiscard]] auto chain_view() const -> ChainView;

    /// @brief 第 index 行的上移 / 下移 / 移除按钮（用例据此按真实命中链点它）；越界或未画时为空。
    [[nodiscard]] auto chain_up_button(std::size_t index) const -> aurora::Widget *;
    [[nodiscard]] auto chain_down_button(std::size_t index) const -> aurora::Widget *;
    [[nodiscard]] auto chain_remove_button(std::size_t index) const -> aurora::Widget *;

    /// @brief 候选池第 index 枚按钮（池宽固定，未用的那几枚是隐藏态）；越界或未画时为空。
    [[nodiscard]] auto chain_candidate(std::size_t index) const -> aurora::Widget *;

    /// @brief 候选过滤输入框的控件。
    [[nodiscard]] auto chain_filter_input() const -> aurora::Widget *;

    /// @brief 重排列表本体的控件（用例据此问键盘抓取态——`Escape` 的那条交接腿就落在它身上）。
    [[nodiscard]] auto chain_list() const -> aurora::Widget *;

    /// @brief 字体族选择器的一档状态（A4 的四条判据各取一处）。
    ///
    /// 与 `theme_cards()` / `chain_view()` 同一条理由：`configured` 与 `effective` 分两列交出来，用例
    /// 才能判 A4-b 那句「显示成生效的那族 + 另给回落留痕」而不是「显示成配置里那族」；`choices` 是
    /// 真正摆进浮层的那几档（等宽筛、字典序、置顶之后的次序），故「候选池」与「置顶」两条能按序逐字比。
    struct FamilyView {
        std::string configured{};               ///< 表单里那个族名（逐字，可能是目录里没有的名）。
        std::string effective{};                ///< 实际生效的族名，就是触发按钮的标签。
        std::string notice{};                   ///< 回落留痕文案；无回落时为空串。
        std::vector<std::string> choices{};     ///< 浮层里那一列候选（浮层没开也给出，次序即池内次序）。
        bool popup_open{};                      ///< 候选浮层是否开着。
    };

    /// @brief 字体族选择器的当前状态（区段未画时各列为空）。
    [[nodiscard]] auto family_view() const -> FamilyView;

    /// @brief 字体行那枚常驻触发按钮的控件（用例据此按真实命中链点它）。
    [[nodiscard]] auto font_button() const -> aurora::Widget *;

    /// @brief 浮层第 index 档候选按钮的控件；越界或浮层未建时为空。
    [[nodiscard]] auto font_candidate(std::size_t index) const -> aurora::Widget *;

    /// @brief 回落留痕那一行的 `Text` 控件（未画时为空）。
    ///
    /// 只服务一条几何判据：它不是可点节点，故 `reachable_box` 一类的真实派发量法对它无效，比较的只有
    /// 「与字体行同一坐标空间里的上下关系」。
    [[nodiscard]] auto font_notice_text() const -> aurora::Widget *;

    /// @brief 候选浮层本体的控件（未建时为空）。
    [[nodiscard]] auto font_popup() const -> aurora::Widget *;

    /// @brief 快捷键只读表的一行（判据文 D1-a 的三列 + D2-a 的那一列标注，全是已解析的显示串）。
    ///
    /// 与 `theme_cards()` / `family_view()` 同一条理由交出来而不让用例读浮层树：D1-a 判「三列逐字来自
    /// 注册表」、D2-a 判「标注来自实际比对」，读树里的 `Text` 就得把排版坐标也算进判据；而本区段没有
    /// 一个单元格是可点节点（首版只读，D3-a），`reachable_box` 一类的真实派发量法对它无效。
    struct ShortcutRowView {
        std::string title{};         ///< 动作名列。
        std::string category{};      ///< 分组列。
        std::string binding_text{};  ///< 「当前组合键」列；未绑定显示已解析的「未绑定」文案。
        std::string note{};          ///< 标注列：冲突（可多条）或「延后」；无标注为空。
        bool deferred{};             ///< 孤儿行＝覆盖表里的命令 id 在注册表查无，其标注恒为「延后」而非「无冲突」。
        std::string command_id{};    ///< 命令唯一标识，用于编辑时定位要修改的覆盖条目。
    };

    /// @brief 快捷键只读表当前画出的行，次序＝排版次序（该区段未画时为空表）。
    [[nodiscard]] auto shortcuts_rows() const -> std::vector<ShortcutRowView> {
        return shortcuts_rows_;
    }

    /// @brief 快照视图的一行投影：文件名逐字 + 字节数。
    ///
    /// 交出来而不让用例读浮层树里的 `Text`（与 `theme_cards()` / `chain_view()` / `family_view()` 同一
    /// 条理由）：`SPEC.FEAT.PREF.07` 那句「至多留存 N 份、新的在前」判的是**次序与条数**，读树就得把
    /// 排版坐标也算进判据。而 `name` 之所以是字符串而不是路径：界面上显的就是 `filename()`，用例比的
    /// 也该是界面上那串字（路径的原样交回由 `Hooks::rollback` 那条腿判，裁决 7.87⑤）。
    struct SnapshotRowView {
        std::string name{};      ///< 快照文件名（`path.filename()` 逐字，已带 epoch 秒）。
        std::string size_text{}; ///< 「N B」形态的字节数；存储侧读不到字节时为「0 B」。
    };

    /// @brief 六条 `Hooks` 接缝齐备时界面才画动作按钮（缺任一条一个都不画，7.38⑥ F-b）。
    [[nodiscard]] auto has_config_actions() const noexcept -> bool;

    /// @brief 动作按钮「配置快照」是否正处于按下态（即快照视图在显示）。
    [[nodiscard]] auto showing_snapshots() const noexcept -> bool {
        return showing_snapshots_;
    }

    /// @brief 快照视图当前画出的行，次序＝新→旧（该区段未画时为空表）。
    [[nodiscard]] auto snapshot_rows() const -> std::vector<SnapshotRowView>;

    /// @brief 卡片头部那一行留痕的当前文案（导出 / 导入 / 回滚三处动作各写一句，无动作时为空串）。
    ///
    /// 交的是**已解析的中文文案**而不是词条 key：存储侧交回的失败原因是 ASCII 诊断串，绝不能上屏
    /// （AGENTS.md §4.3 第 14 条），故本件只把「哪一类失败」映射成词条，用例读到的必须是映射之后的产物
    /// ——这样「把 `reason` 原样画上界面」这一类实现错误才有证人（与降级对话框同口径，裁决 7.76⑤）。
    [[nodiscard]] auto config_notice() const -> std::string {
        return config_notice_text_;
    }

    /// @brief 那一行留痕的 `Text` 控件本体（未画时为空）。
    ///
    /// 只服务一条判据：失败那三句走的是**不重建浮层**的那条腿（只换文本），故「只改了模型态、没写上
    /// 屏」这种错在 `config_notice()` 那一份字符串读数上结构上抓不到。与 `font_notice_text()` 同一档。
    [[nodiscard]] auto config_notice_widget() const -> aurora::Widget *;

    /// @brief 头部三枚动作按钮与快照视图的「返回」（未画时为空）。
    [[nodiscard]] auto export_button() const -> aurora::Widget *;
    [[nodiscard]] auto import_button() const -> aurora::Widget *;
    [[nodiscard]] auto snapshots_button() const -> aurora::Widget *;
    [[nodiscard]] auto back_button() const -> aurora::Widget *;

    /// @brief 快照视图第 index 行的「回滚」按钮（越界或该区段未画时为空）。
    [[nodiscard]] auto snapshot_rollback_button(std::size_t index) const -> aurora::Widget *;

    /// @brief 预览盒的视口控件；未装 `Hooks::preview_appearance`、或面板此刻关着时为空（S7 的观测点）。
    ///
    /// 用例要靠它把「外观改动是否落进了预览那条腿」与「改动只进了表单副本」分开断言：预览的行列数、
    /// 以及它自己那一帧的绘制产物都只有从视口本体才读得到（§8 的两条判据各取一处）。
    [[nodiscard]] auto preview_view() const noexcept -> TerminalView *;

    /// @brief 预览那条会话此刻的终端模式；没有预览盒（未装两条接缝、或面板关着）时为空。
    ///
    /// 判据 F-c 断「预览与主视口同源」，而三条构造期注入只落进预览自己那条会话，从 `preview_view()`
    /// 那一侧读不到（`TerminalView::modes_snapshot()` 是它的私有件，本件不为判据把它开成公共入口，
    /// §4.1 第 6 条）。取的是状态机现值而不是面板另存的一份档，故漏装接缝与装错值都能抓到。
    [[nodiscard]] auto preview_modes() -> std::optional<term::TermModes>;

    /// @brief 排预览那一帧：取夹具重投后攒下的脏行并提交（宿主在 `Application::set_on_frame` 里逐帧调）。
    auto pump_preview() -> void;

private:
    /// @brief 按当前页取行表（表内次序即排版次序，只收本页、只收装载成功的键）。
    [[nodiscard]] auto collect_rows() const -> std::vector<const SettingsControl *>;

    /// @brief 撤掉既有浮层（若有）再按当前页重建，并重挂关闭快捷键所需的对象。
    auto rebuild_overlay() -> void;

    /// @brief 建卡片：标题行 + 「左导航 / 右行区」 + 行区。
    [[nodiscard]] auto build_card() -> aurora::Node;

    /// @brief 建一行：标签 + 状态列（角标或本行最近一次提交的失败原因） + 控件。
    /// @param ordinal 该行在当前页的序号（状态列的索引锚）。
    [[nodiscard]] auto build_row(std::size_t ordinal) -> aurora::Node;

    /// @brief 建某行的控件腿：六类通用形态给真控件，四个专用区段在 `build_row` 就已建完，只读表给值摘要。
    [[nodiscard]] auto build_control(const SettingsControl &control, bool editable) -> aurora::Node;

    /// @brief 建一行的表头（标签 + 状态列）；通用行与两个区段共用，故状态列的登记点只有一处。
    /// @param ordinal 该行在当前页的序号（状态列的索引锚）。
    [[nodiscard]] auto build_header(std::size_t ordinal, const SettingsControl &control, bool editable)
        -> std::pair<aurora::Node, aurora::Node>;

    /// @brief 建主题卡区段：表头 + 每行四张卡（`Scroll` 只竖向滚动，故八套必须换行而不是一列八张）。
    [[nodiscard]] auto build_theme_section(std::size_t ordinal) -> aurora::Node;

    /// @brief 建 16 格色板区段：表头 + 每行八格 + 常驻的编辑器行（一格 HEX 输入 + 「恢复主题默认」）。
    [[nodiscard]] auto build_swatch_section(std::size_t ordinal) -> aurora::Node;

    /// @brief 建字体族区段：表头行（标签 + 状态列 + 常驻触发按钮）+ 下方那一行回落留痕（A4-b）。
    ///
    /// 在 `build_row` 的分派处拐出去建，而不是在 `build_control` 里给一只 `Dropdown`：见文件头
    /// 「字体族选择器」那段的三条口径（其中一条正是「行内不得用覆盖绘制不占布局的控件」）。
    [[nodiscard]] auto build_family_section(std::size_t ordinal) -> aurora::Node;

    /// @brief 弹候选浮层：把当前候选池建进浮层并按 anchor 落位（同一只 `Popup` 复用）。
    /// @param anchor 触发按钮在**窗口坐标**里那份盒的下沿（经框架 `Widget::window_bounds()` 现取，A4-e）。
    auto open_family_popup(aurora::Point anchor) -> void;

    /// @brief 收候选浮层：只 `Popup::close()` 而不摘浮层（见文件头那条「派发栈内销毁正在派发的按钮」）。
    auto close_family_popup() -> void;

    /// @brief 点中一档候选：收浮层并把该族名提交进表单（落盘与广播走 `after_commit` 的既有腿）。
    /// @param family 该档的族名（逐字节，即候选池里那一串）。
    auto choose_family(const std::string &family) -> void;

    /// @brief 刷新触发按钮标签与留痕行（区段建好、以及该键提交通过之后各一次）。
    auto refresh_family_views() -> void;

    /// @brief 清掉字体族区段的一切控件句柄与浮层序号（关面板与重建浮层两处，留着就是孤儿句柄）。
    auto clear_family_state() -> void;

    /// @brief 候选池：目录里 `.monospace` 为真的族名按逐字节字典序，再把两档置顶（同值合一，A4-d）。
    [[nodiscard]] auto family_choices() const -> std::vector<std::string>;

    /// @brief 表单里那个族名（该行没装载或不是文本时回空串）。
    [[nodiscard]] auto configured_family() const -> std::string;

    /// @brief 现算回落判定（目录取 `family_catalog_`，本件不第二次枚举，A4-a / S16）。
    [[nodiscard]] auto family_choice() const -> FontFamilyChoice;

    /// @brief 建回退链区段：表头 + 重排列表（每行三枚按钮 + 手柄带）+ 过滤输入框 + 固定候选池 + 提示行。
    ///
    /// 列表条目由本件传给框架的条目构造器产出，故本函数只在区段第一次建时跑；此后的数据变化都走
    /// `rebuild_chain_rows()`（同一批指针重建 + 换高度 + 换提示）。
    [[nodiscard]] auto build_chain_section(std::size_t ordinal) -> aurora::Node;

    /// @brief 把派生态列表按 `families` 重建并按新条目数改区段高度，随后刷新按钮与提示。
    /// @param next 新的链内容（写进 `chain_items_` 之前先算好高度）。
    auto rebuild_chain_rows(std::vector<std::string> next) -> void;

    /// @brief 只刷按钮档与提示行（次序没变时走这一条，避免为改两枚按钮的可用性重建整棵条目树）。
    auto refresh_chain_views() -> void;

    /// @brief 清掉回退链区段的一切派生态（关面板与重建浮层两处，条目指针留着就会读到上一版的孤儿）。
    auto clear_chain_state() -> void;

    /// @brief 把当前列表内容提交进表单（一次结构性改动＝一次落盘 + 一次广播，走 `after_commit` 的既有腿）。
    auto commit_chain() -> void;
    /// @brief 把第 index 项沿方向挪一格（越界即不动，与按钮的禁用态是同一判据的两道）。
    auto move_chain_item(std::size_t index, int delta) -> void;

    /// @brief 移除第 index 项。
    auto remove_chain_item(std::size_t index) -> void;

    /// @brief 追加一个族名（已达上限或已在链内时不动——候选池在那两档本就不显示它）。
    auto append_chain_family(const std::string &family) -> void;

    /// @brief 按过滤文本重算候选池并刷新提示行（只改常驻按钮的标签与显隐，不重建条目）。
    auto refresh_chain_candidates() -> void;

    /// @brief 表单里当前那条链（该行没装载或形态不合时回空表）。
    [[nodiscard]] auto chain_items_from_form() const -> std::vector<std::string>;

    /// @brief 画链条目手柄带的那一条：三行等长的短横点阵（框架的手柄带只留命中区、不画 grip）。
    auto paint_chain_handle(aurora::Painter &painter, const aurora::Rect &box) const -> void;

    /// @brief 建快捷键只读表区段：表头 + 逐命令一行四列（动作名 / 分组 / 当前组合键 / 标注）+ 编辑按钮。
    ///
    /// 自绘而不借框架 `data_widgets.h` 的那三件表控件：它们的 `on_paint` 形参一律不读 ctx，故 `Theme`
    /// 到不了、色值硬编码浅色（裁决 7.68①），沿用主题卡 / 16 格色板 / 回退链三处的自绘先例。
    /// 行内容与标注都取 `ui::build_shortcut_rows()` 一次算好的那张表（本件不自己比第二次键位），
    /// 故本函数只在区段第一次建时跑完就把行投影进 `shortcuts_rows_`。
    [[nodiscard]] auto build_shortcuts_section(std::size_t ordinal) -> aurora::Node;

    /// @brief 打开键位绑定对话框：让用户输入新的组合键并检测冲突。
    /// @param command_id 要编辑的命令 id。
    /// @param row_index 该行在 shortcuts_rows_ 里的索引。
    auto open_binding_dialog(const std::string &command_id, std::size_t row_index) -> void;

    /// @brief 关闭键位绑定对话框：只 `Dialog::close()` 而不摘浮层（派发栈内销毁正在派发的按钮，同 `choose_family()`）。
    auto close_binding_dialog() -> void;

    /// @brief 清掉键位绑定对话框的一切句柄与浮层序号（关面板与重建浮层两处）。
    ///
    /// 对话框必须随那一次浮层重建一起消失：它的输入框与冲突提示里装着**上一版**行投影的现场，而重建之后的
    /// 卡片里那一行可能已经不是同一条命令。序号留在手里还会让下一次 `remove_overlay` 摘到别的节点上。
    auto clear_binding_state() -> void;

    /// @brief 更新冲突提示文本（输入框内容变化时调用）。
    /// @param combo_text 当前输入的组合键文本。
    auto update_conflict_notice(const std::string &combo_text) -> void;

    /// @brief 提交新的键位绑定：写入覆盖表并触发落盘与广播。
    /// @param combo_text 用户输入的组合键文本。
    auto commit_binding(const std::string &combo_text) -> void;

    /// @brief 恢复所有快捷键为默认值：清空覆盖表。
    auto restore_shortcut_defaults() -> void;

    /// @brief 检测给定的组合键是否与已有绑定冲突。
    /// @param combo_text 待检测的组合键文本。
    /// @param exclude_command_id 排除的命令 id（正在编辑的那个）。
    /// @return 冲突的命令标题列表；无冲突时为空。
    [[nodiscard]] auto detect_conflicts(const std::string &combo_text, const std::string &exclude_command_id) const -> std::vector<std::string>;

    /// @brief 设置某个命令的覆盖绑定。
    /// @param command_id 命令 id。
    /// @param combo_text 新的组合键文本。
    auto set_binding_override(const std::string &command_id, const std::string &combo_text) -> void;

    /// @brief 清除某个命令的覆盖绑定（回到默认）。
    /// @param command_id 命令 id。
    auto clear_binding_override(const std::string &command_id) -> void;

    /// @brief 六条 `SPEC.FEAT.PREF.07` 接缝是否齐备（不齐即头部一个动作按钮都不画）。
    ///
    /// 判据是「六条都在」而不是「五条在」：`pick_*_path` 缺席时导出只剩一个点了没反应的按钮，而它在
    /// 集成用例的无头通道里结构上抓不到平台对话框，故宁可不画（7.38⑥ F-b 的同一口径）。
    [[nodiscard]] auto config_actions_ready() const noexcept -> bool;

    /// @brief 进 / 出快照视图：先换态再重建浮层（进入时按当前名单画，回滚与导入会让名单在那一次动作里
    ///        就变——快照取在写之前，裁决 7.87②）。
    auto show_snapshots(bool showing) -> void;

    /// @brief 建快照视图那一列：一句说明 + 逐份一行（文件名 / 字节数 / 「回滚」）；名单为空时只有说明。
    ///
    /// 从 `build_card()` 的行区分派处拐进来，与五个专用区段同一形态（仍复用行区的 `Scroll`，故不为五份
    /// 快照另立一层容器）。派生态指针在本函数开头清一次——`LayoutBuilder` 的闭包每次布局都重跑。
    [[nodiscard]] auto build_snapshot_section() -> std::vector<aurora::Node>;

    /// @brief 一键回滚到名单里第 index 份；成功后重装载副本并让视口与预览跟上。
    auto rollback_to_snapshot(std::size_t index) -> void;

    /// @brief 导出：先取目标文件（取消即整条不跑、连留痕都不写），再落文件并把成败折成一句留痕。
    auto run_export() -> void;

    /// @brief 导入：与导出同一条腿，成功后另走「重装载 + 广播 + 重投」。
    auto run_import() -> void;

    /// @brief 外部改动（回滚 / 导入）之后把面板副本换成存储里那一份，并让视口与预览跟上。
    /// @param notice_key 上屏留痕的词条 key。
    /// @param detail 该句里的 `{0}` 取值（快照文件名或导入来源）；空串即那句没有位置参数。
    ///
    /// 三条腿的顺序是承重项：`form_` 先换（此后每一次落盘与广播都读它），广播交的是**整份**（这一次动的
    /// 键就是全表），预览排在广播之后（它取的是刚生效的那一份外观），最后重建浮层——通用行的控件值是在
    /// 建行时从表单读的，不重建就会显出「表单已是新值、开关还停在旧档」。
    /// 面板原先未落盘的改动在这一步**丢失**，这是「回到那一份现场」的字面代价，界面上由那一行留痕说出来
    /// （裁决 7.87⑦）。新建的表单相对它自己的基线**不脏**，故这里不需要 `note_persisted()`。
    auto reload_from_store(std::string_view notice_key, std::string_view detail) -> void;

    /// @brief 写卡片头部那一行留痕并重绘（一次动作一句话，上一句被覆盖）。
    auto set_config_notice(std::string text) -> void;

    /// @brief 清掉配置动作区的**派生控件句柄**（关面板与重建浮层两处，留着就是上一版按钮的孤儿句柄）。
    ///
    /// 刻意不清那一行留痕的文案与「是否在看快照名单」那两态：它们是模型状态而不是指向已析构控件的指针，
    /// 而换页与回滚之后的重建浮层都要照旧把它们画出来。这两态只在 `close()` 里额外归零。
    auto clear_config_state() -> void;

    /// @brief 画一张主题卡：底色、四格样例、分隔线、描边与选中勾（闭包在绘制时读 `form_`）。
    auto paint_theme_card(aurora::Painter &painter, const aurora::Rect &box, std::size_t index) const -> void;

    /// @brief 画一格色板：该格当前色 + 选中描边（同上一条，读时现取）。
    auto paint_swatch(aurora::Painter &painter, const aurora::Rect &box, std::size_t slot) const -> void;

    /// @brief 切主题（S6）：整份色值六格取新主题的默认值，一次落盘 + 一次广播。
    auto apply_theme(const std::string &name) -> void;

    /// @brief 把色板编辑器指向第 slot 格（A2-a 的「点一格」那一腿）。
    auto select_swatch(std::size_t slot) -> void;

    /// @brief 把选中格恢复成当前主题的默认色（S6① 的逐档恢复）；无基线时按钮处于禁用态，走不到这里。
    auto reset_swatch_to_theme_default() -> void;

    /// @brief 表单里当前那 16 格色板；该行没装载或形态不合时回空表。
    [[nodiscard]] auto palette_table_from_form() const -> std::vector<RgbaColor>;

    /// @brief 当前主题名对应的预置色板；名字不在候选表里时为空（即没有可比基线）。
    [[nodiscard]] auto theme_baseline() const -> const PaletteSpec *;

    /// @brief 表单里 `appearance.theme` 当前点名的那套（没装载或不是文本时为空串）。
    [[nodiscard]] auto current_theme_name() const -> std::string;

    /// @brief 刷新两个区段的一切派生态：卡片与色板重绘、卡名亮度、恢复按钮可用性、主题行「自定义」角标。
    auto refresh_palette_views() -> void;

    /// @brief 让预览盒跟上装配层当前那份外观（S7 / F-c）；未装钩子即不建、也不画那条横条。
    ///
    /// 两条腿合在本函数里：横条进卡片要在 `rebuild_overlay()` **之前**（`build_card()` 按本件是否已持有
    /// 预览决定第三条子节点），而既有预览只换外观、不重建视口（裁决 7.53 的 S4①，重建会把选区与回看
    /// 一起作废，这里同理会把刚投上去的夹具画面抹掉）。换完外观后请求宿主排下一帧（`preview_wake`）。
    auto refresh_preview() -> void;

    /// @brief 建预览盒那一档子节点：固定 140 dp 高、把 `SettingsPreview` 的节点交进卡片列（F-e）。
    [[nodiscard]] auto build_preview_bar() -> aurora::Node;

    /// @brief 把编辑器文本框的内容换成选中格的当前值（只在选中格换了或整份色板换了时调，
    ///        提交之后不调——否则会抹掉用户正在敲的半截文本，S14 的那半条）。
    auto sync_swatch_editor() -> void;

    /// @brief 提交后的公共腿：刷新状态列、按需落盘、按需广播。
    auto after_commit(std::string_view key, const SettingsControl &control, CommitIssue issue) -> void;

    /// @brief 写某行的状态列文案（该行此刻不在可见窗口内、条目已被回收时静默跳过）。
    ///
    /// 形参取 `LocalizedString` 而非显示串：角标是两枚已解析文案的拼接（走字面档），而校验失败原因
    /// 交词条 key 让框架在绘制时就地查表（`settings_issue_text` 的返回形态）。
    auto set_status(std::size_t ordinal, const aurora::LocalizedString &text) -> void;

    /// @brief 该行的角标文案（「延后」/「下次会话生效」/两者并列），无角标时为空串。
    [[nodiscard]] static auto badge_for(const SettingsControl &control) -> std::string;

    /// @brief 该行状态列的文案：`badge_for` 的两枚目录折成角标，主题行再挂 A1-b 的「自定义」。
    [[nodiscard]] auto badge_text(const SettingsControl &control) const -> std::string;

    /// @brief 专用控件行的只读值摘要（本棒不编辑它们，只把当前值如实显示出来）。
    [[nodiscard]] auto value_summary(const SettingsControl &control) const -> std::string;

    /// @brief 一行是否可交互：只有仍未落地的专用表形态在本件是占位。
    ///
    /// `Absent`（有键、全仓无消费方）**不**灰置：人已拍板一律可改、可落盘，延后由行尾「延后」角标与
    /// 组顶说明表达（裁决 7.68③；判据文 S15① / B3-a / C2-a 那句「灰置」据此就地更正）。于是
    /// 「面板把没接线的键画成可用的了」这条误读要有证人，可看的判据是角标与组顶说明而不是控件灰置态。
    [[nodiscard]] static auto is_editable(const SettingsControl &control) -> bool;

    aurora::OverlayHost &host_;       ///< 浮层宿主（装配层的场景根，非拥有）。
    aurora::ShortcutRegistry &shortcuts_;  ///< 快捷键注册表：`Escape` 的登记与解绑处（非拥有）。
    Hooks hooks_{};
    SettingsForm form_{std::vector<FormEntry>{}};  ///< 副本；未打开时是空表。
    SettingsPage page_ = SettingsPage::Appearance;
    bool open_ = false;
    /// 本面板浮层在宿主子节点里的序号（重建时用）。宿主没有基础内容时 `add_overlay` 返回
    /// `std::nullopt`（框架 G29 回货后的口径：那种序号恒被 `remove_overlay` 当基础内容拒收），
    /// 故这里以「无浮层」为缺省，而不是拿 0 当哨兵。
    std::optional<std::size_t> overlay_index_{};
    int escape_binding_ = 0;           ///< `Escape` 绑定的 id；0 = 未登记。
    std::vector<const SettingsControl *> rows_{};  ///< 当前页行表（与 `status_texts_` 同序）。
    std::vector<std::shared_ptr<aurora::Text>> status_texts_{};  ///< 各行状态列控件，按序号。
    std::vector<std::string> note_labels_{};  ///< 当前页画出的组顶说明文字（与行区里的说明节点同序同份）。

    std::vector<ThemeChoice> theme_choices_{};  ///< 每次建浮层时经 `Hooks::themes` 现取（卡片次序即其次序）。
    std::size_t selected_swatch_ = 0;           ///< 色板编辑器当前指向的格，缺省 0 格（编辑器是常驻的）。
    std::vector<std::shared_ptr<aurora::Canvas>> theme_canvases_{};  ///< 主题卡画布，按候选表序。
    std::vector<std::shared_ptr<aurora::Text>> theme_labels_{};      ///< 卡名，与上一条同序（选中态换亮度）。
    std::vector<std::shared_ptr<aurora::Canvas>> swatch_canvases_{}; ///< 16 格画布，按格序。
    std::shared_ptr<aurora::TextInput> swatch_editor_{};             ///< 常驻的 HEX 输入框。
    std::shared_ptr<aurora::Button> swatch_reset_button_{};          ///< 「恢复主题默认」，无基线时禁用。

    std::shared_ptr<aurora::Button> font_trigger_{};  ///< 字体行那枚常驻触发按钮（锚点经框架 `window_bounds()` 现取）。
    std::shared_ptr<aurora::Text> font_notice_{};     ///< 字体行**下方**那一行回落留痕（A4-b）。
    std::shared_ptr<aurora::Popup> font_popup_{};     ///< 候选浮层本体：跨开合复用一只，见文件头③末段。
    std::optional<std::size_t> font_popup_index_{};   ///< 它在宿主子节点里的序号（降序摘除用）。
    std::vector<std::shared_ptr<aurora::Button>> font_candidates_{};  ///< 当前浮层里的候选按钮，按池内序。

    std::vector<FontFamilyEntry> family_catalog_{};  ///< 每次建浮层时经 `Hooks::families` 现取（S16 的同源目录）。
    /// 列表的**数据源**（框架的 `ReorderableList` 直接改写它）；与表单之间以本件为中介，见 `commit_chain()`。
    std::shared_ptr<aurora::State<std::vector<std::string>>> chain_items_{};
    std::shared_ptr<aurora::ReorderableList<std::string>> chain_list_{};  ///< 重排列表本体；`Escape` 的抓取腿问它。
    std::shared_ptr<aurora::Column> chain_list_holder_{};  ///< 包裹列表的那一栏，区段高度挂在它上面。
    std::shared_ptr<aurora::TextInput> chain_filter_{};    ///< 候选过滤框（不进表单，见文件头「添加族」段）。
    std::shared_ptr<aurora::Text> chain_hint_{};           ///< 空链 / 上限 / 截断 / 无匹配四档提示。
    std::vector<std::shared_ptr<aurora::Button>> chain_up_buttons_{};     ///< 逐行三枚按钮，按条目序。
    std::vector<std::shared_ptr<aurora::Button>> chain_down_buttons_{};
    std::vector<std::shared_ptr<aurora::Button>> chain_remove_buttons_{};
    std::vector<std::shared_ptr<aurora::Button>> chain_candidates_{};     ///< 固定池宽的候选按钮。
    std::vector<std::string> candidate_names_{};  ///< 当前池内各档的族名（与上一条同序；空档为空串）。
    std::string chain_filter_text_{};             ///< 过滤框的当前内容（本件持有，表单里没有这一项）。

    /// 快捷键只读表当前画出的行投影（区段建好即写，重建浮层与关面板两处清空）。留着上一版的行等于让
    /// 观测面报出界面上并不存在的行——那与本件其余三处「清派生态」的口径同族。
    std::vector<ShortcutRowView> shortcuts_rows_{};
    std::vector<std::shared_ptr<aurora::Button>> shortcut_edit_buttons_{};  ///< 每行的编辑按钮。

    /// 键位绑定对话框：点击某行的编辑按钮时弹出，让用户输入新的组合键。
    std::shared_ptr<aurora::Dialog> binding_dialog_{};
    std::optional<std::size_t> binding_overlay_index_{};  ///< 它在宿主子节点里的序号（降序摘除用）。
    std::size_t editing_row_index_{};       ///< 正在编辑的行索引。
    std::string editing_command_id_{};      ///< 正在编辑的命令 id。
    std::shared_ptr<aurora::TextInput> binding_input_{};  ///< 对话框里的文本输入框。
    std::shared_ptr<aurora::Text> conflict_notice_{};     ///< 冲突提示文本。

    /// `SPEC.FEAT.PREF.07` 的动作区：卡片头部三枚按钮 + 快照视图那一列。
    /// 名单每次建浮层经 `Hooks::snapshots` 现取（与 `theme_choices_` / `family_catalog_` 同一条分工），
    /// 回滚与导入之后不必另设刷新腿——那两条都以重建浮层收口，重建就是重取。
    std::vector<SnapshotEntry> snapshots_{};
    bool showing_snapshots_ = false;                      ///< 卡片右栏当前显示的是快照名单还是设置行。
    std::string config_notice_text_{};                    ///< 头部那一行留痕的已解析文案（一次动作一句话）。
    std::shared_ptr<aurora::Text> config_notice_{};       ///< 那一行留痕的控件（`LayoutBuilder` 闭包登记）。
    std::shared_ptr<aurora::Button> export_button_{};
    std::shared_ptr<aurora::Button> import_button_{};
    std::shared_ptr<aurora::Button> snapshots_button_{};
    std::shared_ptr<aurora::Button> back_button_{};
    std::vector<std::shared_ptr<aurora::Button>> rollback_buttons_{};  ///< 逐份一枚，按名单序（新→旧）。

    /// 预览盒的本体（S7）。每次 `open()` 现建、`close()` 即销毁：浮层撤掉之后它的控件树已脱离宿主，
    /// 复用一份脱离树的控件正是最难查的那类陈旧态，重建一次的代价只是重投一次夹具。（登记时这里另写了
    /// 「框架只在根变化时遍历挂载，故复用要重挂」那一重根据，随缺口 **G35** 回货失效——运行期追加的子树
    /// 由框架在下一次布局入口补挂，本件不再自备 ctx。）
    std::unique_ptr<SettingsPreview> preview_{};
};

}  // namespace borealis::ui
