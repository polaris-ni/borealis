#pragma once

// ============================================================
// 终端视口控件（src/ui/terminal_view.h，私有头）
// ------------------------------------------------------------
// 裁决 D1①：类声明留在 `src/` 而不进 `include/borealis/`——含此头即含框架头，架构 §2.3 的
// 「公共头不含 Aurora 类型」会因此破洞。消费方只有 `src/ui/terminal_view.cpp`（与
// `workspace_view.cpp` 并列的两个触达 `aurora::Painter` 的翻译单元之一）、`src/main.cpp`（装配点）
// 与按相对路径取用本头的渲染用例。
//
// 本层只做翻译与落笔：颜色由 `ui::resolve`、run 由 `ui::layout_row`、dp 几何由
// `ui::make_geometry`、可见行内容由 `session::ScreenMirror` 判好、选区的逐行区间由
// `ui::row_spans` 与 `ui::word_span_at` 判好，这里按层叠顺序下调用。
// ============================================================

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/app/scheduler.h"
#include "aurora/core/font.h"
#include "aurora/render/font_engine.h"
#include "aurora/widget/widget.h"
#include "borealis/grid/row.h"
#include "borealis/session/connection.h"
#include "borealis/session/screen_mirror.h"
#include "borealis/session/session.h"
#include "borealis/term/keymap.h"
#include "borealis/term/mouse.h"
#include "borealis/term/paste.h"
#include "borealis/term/terminal.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/grid_size_debounce.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/right_click.h"
#include "borealis/ui/search.h"
#include "borealis/ui/selection.h"

namespace aurora {
class Dialog;
class FocusManager;
class OverlayHost;
class Popup;
class ShortcutRegistry;
}  // namespace aurora

namespace borealis::ui {

class SearchOverlay;  ///< 搜索浮层的本体（`search_overlay.h`），本控件懒建并常驻（判据 F1-c）。

/// @brief 终端网格的自绘视口（`SPEC.FEAT.RENDER.01` / `03` / `04` 的绘制腿）。
///
/// 回看不借用框架的内容平移：基类 `scroll_viewport_` 只当「距顶行数」的容器（`step` 为 1
/// 行），绘制取的是**行偏移换算**，故本控件不声明 `overflow_strategy(Scroll)`——那会让框架
/// 把 `offset_y` 当 dp 平移量加进绘制盒，单位与语义都对不上（架构 §9.5）。
class TerminalView final : public aurora::LeafWidget {
  public:
    /// @brief 选区与复制行为的可配置项（裁决 7.38① 的界面腿入参）。
    ///
    /// 刻意不复用 `config::TerminalSettings`：`config` 含 `ui::PaletteSpec`，视口反向依赖它会
    /// 成模块环（架构 §2.3），与 `ui::CopyOptions` 由调用方搬值同一条理由。
    struct InteractionOptions {
        std::string word_delimiters;  ///< 双击选词的断点集（配置键 `terminal.word_delimiters` 原样搬入）。
        bool copy_on_select{false};   ///< 选中即复制（`SPEC.FEAT.INTERACT.03`，默认关闭）。
        CopyOptions copy{};           ///< 三项复制变换，均默认关闭以保留原样。
        RightClickAction right_click{RightClickAction::ContextMenu};  ///< 右键三态（配置键 `terminal.right_click`）。
        term::PasteOptions paste{};   ///< 粘贴处置口径（换行策略、行尾与块间隔）。
    };

    /// @brief 视口的三条外部接缝：剪贴板读写与多行粘贴的确认呈现。
    ///
    /// 默认（字段为空）即生产路径的实现；换成替身是为了让「右键 → 剪贴板 → 会话字节」这条链
    /// 能在无窗口站、无真实剪贴板的环境里断言（AGENTS.md §4.4 第 19 条）。字段留空即回落默认，
    /// 是为了让装配层只声明它真正替换的那一条。
    struct Presentation {
        /// @brief 读剪贴板文本；空即 `session::ClipboardOutbox::read`。
        std::function<std::string()> clipboard_read;
        /// @brief 写剪贴板文本；空即 `session::ClipboardOutbox::write`。
        std::function<void(std::string_view)> clipboard_write;
        /// @brief 多行粘贴的确认呈现；空即模态对话框。
        ///
        /// 是异步形态而非 `bool`：对话框要等用户点，放行与否经 `answer` 回调给出，未回调即取消。
        std::function<void(const term::PastePlan &, std::function<void(bool)> answer)> confirm_multiline;
    };

    /// @brief 视口的外观包：绘制与排版要用的那六项（裁决 7.52 的 S4①）。
    ///
    /// 构造与运行期更新共用**同一个载体**，而不是让运行期入口另带一份名单：面板从配置装出的
    /// 外观既要喂给新建的 pane（装配层多会话化），也要广播给运行中的每个 pane，两条路径若各抄
    /// 一套入参，「面板改了某项而新建的 pane 拿不到该项」就只能在真机上看见。
    struct Appearance {
        PaletteSpec palette{};                    ///< 调色板（含光标色与选区色）。
        aurora::Font ref_font{};                  ///< 参考字体：整格度量只由它取（架构 §9.2）。
        Typography typography{};                  ///< 行高与字距（`SPEC.FEAT.RENDER.02`）。
        float padding_dp = 0.0F;                  ///< 视口四周内边距（裁决 7.25②），可配为 0。
        std::chrono::milliseconds blink_period{500};  ///< 光标闪烁周期（配置键 `appearance.cursor_blink_period_ms`）。
        std::vector<std::string> font_fallback_chain{};  ///< 缺字回退链的族名序列，顺序即语义。
    };

    /// @brief 组装视口。
    /// @param session 权威会话，**非拥有**：装配层的局部对象析构次序须保证它活得比本控件久。
    /// @param appearance 外观包；取值域由配置侧把守，本层不夹取（裁决 7.46②）。
    /// @param options 选区与复制的可配置行为；由装配层从 `config::TerminalSettings` 搬值。
    TerminalView(session::Session &session, Appearance appearance, InteractionOptions options);

    /// @brief 逐形参形态的构造（把七项包进 `Appearance` 后转调上一条）。
    ///
    /// 存在的唯一理由是既有的六个装配/用例调用点；一条委托语句把值交进同一个载体，故本控件仍
    /// 只有**一处**存这些值的路径。设置面板那棒（`SPEC.FEAT.PREF.02`）按配置装出 `Appearance`
    /// 之后，装配点应改走上一条，本形参表随之消失。
    TerminalView(session::Session &session, PaletteSpec palette, aurora::Font ref_font, Typography typography,
                 float padding_dp, std::chrono::milliseconds blink_period, InteractionOptions options,
                 std::vector<std::string> font_fallback_chain = {});

    TerminalView(const TerminalView &) = delete;
    auto operator=(const TerminalView &) -> TerminalView & = delete;
    TerminalView(TerminalView &&) = delete;
    auto operator=(TerminalView &&) -> TerminalView & = delete;

    /// @brief 注销闪烁任务：句柄只置取消标志，故析构期安全（与框架 `Timer` 控件同形态）。
    ~TerminalView() override;

    /// @brief 帧边界排帧：先取脏行提交、再在临界区并入本地副本，最后按需标脏。
    ///
    /// 由 `Application::set_on_frame` 在 `present_root` 之前调用（架构 §3.2）。顺序固定为
    /// 「先取队列、再读网格」——反过来会与读线程构成 ABBA 死锁（`session.h` 头注释）。
    auto on_frame() -> void;

    /// @brief 取选区文本（`SPEC.FEAT.INTERACT.03` 的复制腿）：在既有短临界区内按存储行序取行，
    ///        出锁才返回。copy-on-select 与右键菜单的「复制」项共用本入口（裁决 7.40）。
    /// @return 选中文本，三项变换已按入参应用；无选区时为空串。
    [[nodiscard]] auto selected_text() -> std::string;

    /// @brief 挂上浮层宿主：右键菜单（`au::Popup`）与多行粘贴确认（`au::Dialog`）都作为它的浮层
    ///        子节点存在，本控件只在按需打开时向它 `add_overlay`（裁决 7.41③）。
    ///
    /// 装配层的场景根因此是宿主而非本控件；不设这一步则右键只走直接动作两态，菜单态不发任何东西。
    /// @param host 浮层宿主，**非拥有**：它持有本控件的节点，析构次序须晚于本控件。
    auto set_overlay_host(aurora::OverlayHost &host) -> void;

    /// @brief 搜索浮层的两条框架依赖：`Escape` 的登记表与焦点管理器（判据文 §4 第 5 条的视口侧）。
    ///
    /// 与 `set_overlay_host` 分开而不并入：宿主那一条是右键那一棒既有的接缝（本控件的父节点链），
    /// 而本控件不替浮层持有快捷键层——「打开即登记 `Escape`、关闭即解绑」的两处动作都在浮层本体里，
    /// 故它需要的是登记表本身而不是「能注册快捷键」的抽象。两者都在装配阶段交，建浮层那一刻读齐。
    /// @param shortcuts 进程内快捷键登记表。
    /// @param focus 焦点管理器（浮层在派发栈外打开时用它补压焦点作用域）。
    auto set_search_dependencies(aurora::ShortcutRegistry &shortcuts, aurora::FocusManager &focus) -> void;

    /// @brief 替换剪贴板与确认的默认实现（用例注入替身；缺省不装即走生产路径）。
    /// @param presentation 三条接缝，字段留空即保持默认。
    auto set_presentation(Presentation presentation) -> void;

    /// @brief 工作区层的两条注入接缝：期望行列的去向、以及「这一键归谁」的判定。
    ///
    /// 各对应裁决 7.47 的一条判据：⑩ 的尺寸去抖落工作区层（同一帧里所有 pane 一起变，视口层
    /// 各自去抖会让下发次数变成 N 倍），② 的本棒键位不经框架快捷键层、改在焦点 pane 的按键入口
    /// 前置过滤。两条都是 `std::function` 而非虚函数：工作区层用闭包捕获 pane 标识，控件因此
    /// 不需要认识分屏树。
    using GridSizeSink = std::function<void(GridSize expected)>;
    using KeyPreFilter = std::function<bool(const term::KeyPress &press)>;

    /// @brief 换掉「期望行列」的去向；装上即不再直发 `Session::resize`。
    ///
    /// 缺省（未装）仍直发，故既有像素与集成用例零改动。0 行 0 列与「同值不重发」两条过滤留在
    /// 本层——它们的真值源是本控件的度量与内边距，工作区层判不了（裁决 7.48③）。
    /// @param sink 接收者。
    auto set_grid_size_sink(GridSizeSink sink) -> void;

    /// @brief 装上按键前置过滤；返回 true 即该键已被消费，不再编码发会话。
    ///
    /// 只问 `Down` 那一支：重复与抬起都不该被工作区层认领。命中即置 `is_handled`，未命中的键
    /// 照原路走 `term::encode_key`——`SPEC.FEAT.INTERACT.01` 的转发腿不可让渡。
    /// @param filter 判定者；入参与 `term::encode_key` 吃的是同一个 `term::KeyPress`。
    auto set_key_pre_filter(KeyPreFilter filter) -> void;

    /// @brief 运行期换外观（裁决 7.52 的 S4①，`SPEC.FEAT.PREF.02` 的「修改即时生效」）。
    ///
    /// **不重建视口**，故选区、回看偏移、焦点态与 `ScreenMirror` 副本全部原样保留——重建会把
    /// 这些一起作废（该稿 S4 对「改完重建视口」这一选项的否决理由）。三条连带动作各有代价：
    /// 回退链是整份排版选项的载体（框架没有 per-field 的链 setter），装上即把量化字距与固定格
    /// 推进清成默认值，故必须紧接 `metrics_stale_ = true`，让下一次取度量把三者一起写回同一份
    /// `layout_opts_`（裁决 7.50 的同源不变量）；闪烁周期改动要取消旧句柄再按新周期注册；
    /// 内边距与字号改动会改行列数，故只标脏让下一次 `on_layout` 经既有的 `GridSizeSink` 下发
    /// （去抖在工作区层，裁决 7.47⑩），而不是在这里直发一个中间值。
    /// @param appearance 新的外观包（与构造时同一个载体）。
    auto apply_appearance(Appearance appearance) -> void;

    /// @brief 运行期换选区、复制与粘贴的行为口径（S4① 的第二条入口）。
    ///
    /// 五项都在**用取时现读**（`word_delimiters` 在 `span_of`、`copy_on_select` 在抬起分支、
    /// `copy` 在 `selected_text`、`right_click` 在 `on_right_click`、`paste` 在 `flush_paste_request`），
    /// 故本入口只换值、不标脏——没有一项参与绘制，标脏只会让画面无谓地重画一遍。既有选区的端点
    /// 是按**当时**的粒度折进外沿存下的，断点集改动不会回头重折已存的选区（用户改的是「下一次双击」，
    /// 不是「上一次选中」）。
    /// @param options 新的行为口径。
    auto apply_interaction_options(InteractionOptions options) -> void;

    /// @brief 当前生效的闪烁周期（毫秒）——S4 的接线观测点。
    ///
    /// 用例要靠它把「改动是否落到注册那条腿」与「改动只是存进了成员」分开断言，而像素那边
    /// 断的是相位是否按新周期翻动。
    [[nodiscard]] auto blink_period() const noexcept -> std::chrono::milliseconds { return blink_period_; }

    /// @brief 右键菜单浮层的只读句柄，从未打开过菜单即空指针（用例的落点观测点）。
    ///
    /// 条目宽度由 `ButtonProps::min_width` 定死而非文字撑开，用例拿不到内容盒就只能自己复制一套
    /// 按钮尺寸算式去猜哪一行是「复制」，而算式猜错时「点到了哪一项」就成了未知量。
    [[nodiscard]] auto context_menu() const noexcept -> const aurora::Popup *;

    /// @brief 多行粘贴确认对话框的只读句柄，从未弹过警告即空指针（用例的落点与收起观测点）。
    ///
    /// 同 `context_menu()` 的理由：按钮位置由框架的 `Column`/`Row` 折算，用例拿不到对话框的盒就
    /// 只能自己复制一套按钮尺寸算式去猜哪一枚是「Yes」；而「答话之后对话框自己收起」这条判据
    /// 除本句柄无处可读（浮层仍挂在宿主上，`overlay_count()` 不因关闭而变）。
    [[nodiscard]] auto multiline_warning() const noexcept -> const aurora::Dialog *;

    /// @brief 本帧的整格几何（dp 步长、行列数与内边距）——工作区层据此现算最小 pane 尺寸。
    ///
    /// 判据是 `max(20 列 × 格宽, 3 行 × 格高)`（裁决 7.47③），而格宽与内边距只有本控件知道；
    /// 让工作区层自己再算一遍度量就成了第二个真值源。
    [[nodiscard]] auto grid_geometry() const noexcept -> const GridGeometry & { return geometry_; }

    /// @brief 回看位置距底的行数（0 = 贴底）：焦点路由「滚动位置不动」判据的观测点。
    ///
    /// 取距底而非内核的 `offset_y`：前者是用户意图（裁决 D6①），随输出与 resize 都不变，
    /// 断言因此只测「路由没有碰它」这一件事。
    [[nodiscard]] auto scrollback_rows_from_bottom() const -> std::size_t;

    /// @brief 当前生效的字号（pt）：Ctrl+滚轮缩放的观测点。
    ///
    /// 缩放是运行期状态（持久化的入口随 `SPEC.FEAT.PREF.02` 的设置面板），用例要靠它把「步长与
    /// 钳位」这一条算式单独断言，而不是从像素反推字号。
    [[nodiscard]] auto font_size_pt() const noexcept -> float { return ref_font_.size_pt; }

    /// @brief 本帧交进框架的排版选项（回退链、量化字距与固定格推进档位）——接线的观测点。
    ///
    /// 用例要靠它把「配置 → 控件 → 框架排版选项」这一段单独断言：三项都从像素反推不出来，
    /// 而档位那一项的单位（物理 px 且已含 scale）写错时像素只是整体挪位，看不出错在何处。
    [[nodiscard]] auto layout_options() const noexcept -> const aurora::render::TextLayoutOpts & {
        return layout_opts_;
    }

    /// @brief 交进当前查询条件（浮层输入框每键一次）；只在**字面量档**置脏。
    ///
    /// 正则档不随打字重扫（裁决 7.78① 的实测代价：宽匹配式一次 1623.5 ms，挂在每次按键之后就是
    /// 冻结单线程 UI，违 AGENTS.md §4.5 第 25 条），要扫由调用方走 `submit_search_query()`。
    /// 与上一次完全相同的条件不置脏，故连排若干帧不会重复扫同一份查询。
    /// @param query 当前条件（文本 + 两枚开关）。
    auto set_search_query(SearchQuery query) -> void;

    /// @brief 提交当前条件并安排一次重扫：`Enter` 与两枚开关的翻转走这里（判据 D4）。
    auto submit_search_query() -> void;

    /// @brief 当前查询条件：浮层重开时把文本与两档逐字读回（判据 F1-c）。
    [[nodiscard]] auto search_query() const noexcept -> const SearchQuery & { return search_query_; }

    /// @brief 当前匹配表；从未成功扫过即空指针。绘制侧按可见行过滤而不拷表（上限档整表 240 KB）。
    [[nodiscard]] auto search_matches() const noexcept -> const SearchMatches * {
        return search_matches_.has_value() ? &*search_matches_ : nullptr;
    }

    /// @brief 上一次扫描是否因正则编译失败而作废（判据 B4「表达式非法」，此时旧表原样留着）。
    [[nodiscard]] auto search_pattern_invalid() const noexcept -> bool { return search_invalid_; }

    /// @brief 输入框里的文本是否还没扫（判据 B5：正则档打字后未提交）。
    ///
    /// 空文本不算「还没扫」——那一档显示的是占位「—」而不是「按 Enter 搜索」（判据 A2-a / B0）。
    [[nodiscard]] auto search_pending_submit() const noexcept -> bool {
        return !search_query_.text.empty() && !(search_query_ == search_scanned_);
    }

    /// @brief 迄今真正扫过几次：节流判据的观测点（「一轮只扫一次」以本计数为据，不看像素）。
    [[nodiscard]] auto search_scan_count() const noexcept -> std::size_t { return search_scans_; }

    /// @brief 把游标推到下一个（或上一个）匹配，并让那一格落在画面正中（判据 D1-a / D2-a）。
    ///
    /// 首尾相连是 `SearchMatches::advance` 的既有口径，本入口只负责「跳完之后看得见」：目标行已在
    /// 可见窗内时画面完全不动（D1-b），否则按 `目标行 − ⌊rows/2⌋` 改回看位置并钳在 `[0, max_offset]`
    /// 内——改的是内核的 `offset_y` 本身，故这是一次用户可见的滚动而不是一帧的临时偏移（D2-a）。
    /// @param direction 前进或后退。
    auto advance_search(SearchDirection direction) -> void;

    /// @brief 关闭浮层：清匹配表与全部高亮，但**保留查询文本与回看位置**（判据 D7 / D2-c）。
    ///
    /// 文本留着是 F1-c 的前提（重开时逐字读回），而「不自动重扫」落成把已扫条件记回空——于是重开的
    /// 那一帧 `search_pending_submit()` 为真，浮层显示 B5 的「按 Enter 搜索」而不是上一份结果的计数。
    auto close_search() -> void;

    /// @brief 打开搜索浮层：第一次在这里懒建，此后**常驻**（判据 F1-c）。
    ///
    /// 视口侧只管「持有」与「开关」两件事：条体的搭法、`Escape` 的登记与解绑、关闭时对模型的清场
    /// 都在浮层本体里（判据文 §4 第 5 条）。开着时再调一次只重新落位，不重建条体（否则输入焦点与
    /// 已打的文本一起丢）。
    ///
    /// 宿主与两条依赖任一没给即不建也不开——那是没接浮层的装配形态（集成用例里的替身宿主），
    /// 而不是「建了但哑掉」的浮层：没有宿主就无处承载，没有快捷键表就关不掉自己。
    auto open_search() -> void;

    /// @brief 浮层本体的只读观测点；从未打开过即空指针（用例据此判「懒建」与「常驻」两档）。
    [[nodiscard]] auto search_overlay() const noexcept -> const SearchOverlay * {
        return search_overlay_.get();
    }

  protected:
    /// @brief 撑满父级，并在此重取整格几何与下发行列尺寸（`SPEC.FEAT.XFER.01` 的 UI 取值腿）。
    [[nodiscard]] auto on_layout(const aurora::Constraints &c, const aurora::BuildContext &ctx)
        -> aurora::Size override;

    /// @brief 四层绘制序列：默认底色 → 色带 → 每行一次批量文本 → 装饰与光标。
    auto on_paint(aurora::Painter &p, const aurora::Rect &bounds, const aurora::BuildContext &ctx)
        -> void override;

    [[nodiscard]] auto type_name() const -> const char * override;

    /// @brief 终端视口自带键盘输入，显式留在 Tab 焦点序内（基类默认即 true）。
    [[nodiscard]] auto wants_focus() const -> bool override;

    /// @brief 不画框架的统一焦点环：终端用光标形态表达焦点态，环是 chrome 且会污染像素判据。
    [[nodiscard]] auto wants_focus_ring() const -> bool override;

    /// @brief 成为滚轮目标但不借用框架的内容平移绘制（见类注释）。
    [[nodiscard]] auto wants_scroll() const -> bool override;

    /// @brief 滚轮四档分流：Ctrl 缩放 → 鼠标上报 → 备屏 alternate scroll → 本地回看
    ///        （`SPEC.FEAT.TERM.06`，裁决 7.77③）；本地回看驱动 `scroll_viewport_` 并把未吸收的
    ///        余量上冒给更浅的可滚动祖先。
    auto on_scroll(aurora::ScrollEvent &e) -> void override;

    /// @brief 按键 → VT 字节 → 会话（`SPEC.FEAT.INTERACT.01` 的转发腿）。
    auto on_key_event(aurora::KeyEvent &e) -> void override;

    /// @brief 文本输入通道：框架给出的真实字符（含布局与大小写）按会话编码写入。
    auto on_text_input(aurora::TextInputEvent &e) -> void override;

    /// @brief 指针入口：上报模式开着且未被 Shift 覆盖时先把这一笔交给上报通道
    ///        （`SPEC.FEAT.TERM.06`，裁决 7.77①②）；否则按下/拖动/抬起 → `ui::cell_at_point` →
    ///        选区端点推进（`SPEC.FEAT.INTERACT.02`，裁决 7.38① / 7.40），右键按三态处置
    ///        （`SPEC.FEAT.INTERACT.03`，裁决 7.41）。
    ///
    /// 不委派基类实现：那条路径只服务 Clickable / Gesture / ContextMenu 修饰链，本控件一样没有。
    auto on_pointer_event(aurora::MouseEvent &e) -> void override;

    /// @brief 方向键归终端自己用：不声明则派发器把它们当几何焦点导航吃掉，光标无法移动。
    [[nodiscard]] auto wants_navigation_keys() const -> bool override;

    /// @brief Enter/Space 先给键盘入口：否则派发器直接调用 `activate()`，终端收不到回车。
    [[nodiscard]] auto wants_activation_keys() const -> bool override;

    /// @brief Tab 先给键盘入口：否则派发器把它当焦点遍历消费，终端永远收不到制表符（vim 一类靠它）。
    [[nodiscard]] auto wants_tab_keys() const -> bool override;

    /// @brief 挂载时向运行中的 `Scheduler` 注册闪烁周期（无 App 运行时不注册）。
    auto on_mount(const aurora::BuildContext &ctx) -> void override;

    /// @brief 关闭显示列表与布局缓存：内容每帧可变，缓存只会掩盖脏行并取的缺陷。
    [[nodiscard]] auto can_cache_display_list() const -> bool override;

    [[nodiscard]] auto can_cache_layout() const -> bool override;

  private:
    /// @brief 拖拽的取格粒度：单击逐格、双击按词、三击按行（裁决 7.38①③④）。
    enum class DragMode : std::uint8_t {
        Cell,
        Word,
        Line,
    };

    /// @brief 帧内取用的光标快照：只收绘制需要的那五个字段，且要可比（`term::Cursor` 无 `==`）。
    struct CursorState {
        std::size_t row = 0;
        std::size_t column = 0;
        term::CursorShape shape = term::CursorShape::Block;
        bool visible = true;
        bool blinking = true;

        [[nodiscard]] constexpr auto operator==(const CursorState &) const noexcept -> bool = default;
    };

    /// @brief 取（并按需重取）给定缩放下、叠上排版可调量之后的整格度量。
    ///
    /// 重取的触发有两处：缩放变了（框架那条），或字体与排版入参变过（`metrics_stale_`，Ctrl+滚轮
    /// 缩放是运行期唯一的改点）。度量只在 `on_layout` 取，故重取的代价不进口令路径。
    [[nodiscard]] auto cell_metrics(float scale) -> const TypedMetrics &;

    /// @brief 文本落笔相对行盒顶的下移量（dp）＝上半 leading。
    ///
    /// 框架按「行盒顶 + 该字体自身 ascender」定位基线，而行高调大后多出的空白在字盒之上，故文本盒
    /// 须整体下移这么多；色带与装饰不吃它（`apply_typography` 已把基线折进 `ascent_px`，两者自动跟随）。
    [[nodiscard]] auto glyph_top_dp() const noexcept -> double;

    /// @brief Ctrl+滚轮的字号缩放：一档 ±1 pt，落在 [6, 72] 内；到界值即不吃事件，让回看照常冒泡。
    /// @param delta_rows 滚轮增量的垂直分量（上为正 = 放大）。
    /// @return 是否改变了字号。
    [[nodiscard]] auto zoom_font_size(float delta_rows) -> bool;

    /// @brief 行列数真变了才下发给会话；0 行或 0 列（窗口最小化）不下发。
    auto request_grid_size() -> void;

    /// @brief 取模式快照（短临界区内只取值）：按键编码要按 DECCKM 定方向键走 SS3 还是 CSI，按
    ///        DECKPAM 定小键盘发数值族还是让位文本通道。
    [[nodiscard]] auto modes_snapshot() -> term::TermModes;

    /// @brief 这一笔指针事件归上报还是归本地交互（裁决 7.77①②③）——分流判据的全部实质。
    ///
    /// 规则按序短路：右键与中键**永不**进上报（本地三态与中键粘贴的语义不可让渡）；已经在途的按下
    /// 决定**整笔手势**的归属（按住左键再松开 Shift 不该把这笔拖拽切成两半）；层级关着或按着 Shift
    /// 一律让位本地（Shift 覆盖是上报开着时本地选区唯一的入口）；新的按下在此之后即归上报；没有
    /// 在途按下的移动只在 `?1003`（任意事件档）才归上报，而**抬起一律不归**——那可能是 Shift 覆盖
    /// 期间的本地按下松了手，补一条孤儿松开会让远端以为某个键还按着。
    /// 本判据不能用 `encode_mouse` 的空值：它同时表达「层级不够」与「该事件在本档不报」，拿它分流
    /// 会把后者误读成前者（`?1000` 档的一次 Move 因此会掉回本地去推选区）。
    /// @param e 框架的指针事件（取按键、动作与修饰态）。
    /// @param level 此刻生效的上报层级（由 `term::mouse_tracking` 从模式快照取）。
    [[nodiscard]] auto takes_pointer_by_report(const aurora::MouseEvent &e, term::MouseTracking level) const
        -> bool;

    /// @brief 指针事件的上报腿：按阶段编码并写会话，同时维护在途按下与上一次运动落点。
    ///
    /// 运动上报**按格子去重**（裁决 7.77④）：高分屏与高频采样下框架一帧能给出多次 Move，逐次上报
    /// 会让远端程序（`vim` 的视觉滚动、`htop` 的菜单跟踪）收到一串同格事件。按下与松开不去重。
    /// 按住的键由本层自己的在途态定而不问事件携带的 `button`：四后端都把 Move 盖成左键，照它编码
    /// 就把「无键悬停」报成了拖动。认领与「本档真的发了字节」无关：`?1000` 档不报 Move，但也不能
    /// 让它去推选区。
    /// @param e 框架的指针事件（就地置 `is_handled`）。
    /// @param modes 此刻的模式快照（编码档位与 SGR 形态由它定）。
    auto report_pointer(aurora::MouseEvent &e, const term::TermModes &modes) -> void;

    /// @brief 滚轮的上报腿：每一档发一条按钮 64 / 65 事件（按下形态，两档协议都无「松开」）。
    /// @param e 框架的滚轮事件（取落点与修饰态）。
    /// @param modes 此刻的模式快照。
    /// @param button `WheelUp`（`delta_y` 为正）或 `WheelDown`。
    /// @param notches 档位（已按事件增量取整且至少一档）。
    /// @return 有没有可报的落点；false 时调用方继续问下一档，而不是把这一档吞掉。
    auto report_wheel(const aurora::ScrollEvent &e, const term::TermModes &modes, term::MouseButton button,
                      int notches) -> bool;

    /// @brief 备屏的 alternate scroll 腿（`?1007`，`SPEC.FEAT.TERM.06` 的 less/more 翻页）：
    ///        每一档发一条方向键，形态由 `term::encode_key` 按 `DECCKM` 定 SS3 或 CSI。
    /// @param modes 此刻的模式快照。
    /// @param up 上滚为真（翻回更早的内容 = 方向键上）。
    /// @param notches 档位。
    /// @return 编码层有没有给出字节。
    auto page_alternate_screen(const term::TermModes &modes, bool up, int notches) -> bool;

    /// @brief 把窗口逻辑 dp 落点折成**可见区**行列（上报协议的坐标系，不是选区用的存储行序）。
    ///
    /// 滚轮事件只有 `position`（窗口坐标）而没有 `local_position`——派发器不平移它，故这里经
    /// `Widget::window_bounds()` 现算自身窗口盒再交既有的换算件。该入口是框架自陈的**低频事后查询**
    /// （上溯布局父链），只在滚轮那一刻走，不进绘制路径。
    /// @param window_point 窗口逻辑 dp 点。
    /// @return 可见区格子；字体未就绪、窗口最小化或尚未布局过时回空。
    [[nodiscard]] auto report_cell(aurora::Point window_point) const -> std::optional<GridCellPos>;

    /// @brief 把要发的字节交进会话：空值即本层不发（编码层已判过层级与事件形态）。
    auto send_report(const std::optional<std::string> &bytes) -> void;

    /// @brief 每帧重投影（裁决 D6①「距底恒定」）：返回本次生效的距底行数。
    [[nodiscard]] auto reproject(std::size_t total_lines, std::size_t rows) -> std::size_t;

    /// @brief 闪烁到期：只有配了闪烁档且持焦时才翻相位并重绘。
    auto on_blink_tick() -> void;

    /// @brief 把回退链装进排版选项并按需留痕一次截断（构造与运行期更新共用本入口）。
    /// @param chain 按优先级排列的族名序列；超过框架的链容量上限时**保留前 N 项**、顺序不变
    ///        （截断对用户不可见，故留痕一次）。空表即不注入按族链，只走框架的全局默认链。
    auto install_fallback_chain(std::vector<std::string> chain) -> void;

    /// @brief 按当前闪烁周期重注册周期任务；没有活跃句柄时不动（未挂载即由 `on_mount` 注册）。
    auto reregister_blink_timer() -> void;

    /// @brief 画一个屏幕行：色带、批量文本（含斜体分流）与装饰线。
    auto paint_row(aurora::Painter &p, const aurora::Rect &bounds, std::size_t screen_row) -> void;

    /// @brief 画光标（三形态 + 失焦降级 + 闪烁 off 相）。
    auto paint_cursor(aurora::Painter &p, const aurora::Rect &bounds, std::size_t rows) -> void;

    /// @brief 画回看位置的指示条（视觉稿 U1）：贴底时不画。
    auto paint_scroll_indicator(aurora::Painter &p, const aurora::Rect &bounds, std::size_t rows) -> void;

    /// @brief 取某存储行在当前可见窗里的那一行；已滚出可见窗（含被 scrollback 挤出顶端）时回空。
    [[nodiscard]] auto mirror_line_of(std::size_t storage_row) const noexcept -> const grid::Row *;

    /// @brief 一格在其拖拽粒度下对应的列区间（行号已折成存储行序）。
    ///
    /// Word 粒度落在空格、制表或界定符上时回空（裁决 7.39④）；Line 粒度取整行含行尾空白（D5①）。
    [[nodiscard]] auto span_of(const GridCellPos &storage_cell, DragMode mode) const -> std::optional<RowSpan>;

    /// @brief 把「按下那一格」与「当前那一格」按拖拽粒度折成选区并落库。
    ///
    /// Word / Line 粒度须先把两个区间折成阅读序的外沿：`row_spans` 的流式归一只认两个端点格，
    /// 照端点格存会让三击向上拖只选中起始行的第一列（裁决 7.40）。
    auto extend_selection(const GridCellPos &current_storage, aurora::ModifierKey modifiers) -> void;

    /// @brief 落选区：按区间表判定，两端重合（区间表为空）即清空；两种形态都标脏。
    auto set_selection(const Selection &selection) -> void;

    /// @brief 把选区端点重折成逐行区间表；区间表为空即作废选区（裁决 7.39② 的塌成单击）。
    ///
    /// 每帧重算一次而不是只在选区变化时算：列数会随 resize 变，而首行的选中右界正是列数（D5①）。
    auto refresh_selection_bands() -> void;

    /// @brief 存储行号对应的选中列区间；本行无选中格时回空（区间表按行号升序，故二分）。
    [[nodiscard]] auto band_at(std::size_t storage_row) const -> std::optional<RowSpan>;

    /// @brief 选区底色：持焦取 selection 槽，失焦按同色向默认底色各半混合（裁决 7.38① D3①）。
    [[nodiscard]] auto selection_ink() const noexcept -> RgbaColor;

    /// @brief 本行的两层区间与三档底色，一次交出（绘制序列的 ② 色带层与光标第三段共用）。
    ///
    /// 命中切片只取本行那一段而不是整张表：上限档的匹配表是 10,000 条 ≈ 240 KB，逐帧整份搬进
    /// 每一行的入参就是把一次搜索的成本摊到每帧每行（判据文 §4 第 1 条）。三档底色里只有选区那档
    /// 随焦点降级，命中两档**不降级**（判据 A1-f：浮层一打开视口必然失焦，若跟着降级，正在搜的
    /// 这一次反倒成了画面最弱的一档）。
    /// @param screen_row 可见窗内的行号（0 = 顶行）。
    /// @return 交进 `ui::layout_row` 的三层入参；无命中且无选区时各字段即「什么都不画」。
    [[nodiscard]] auto bands_for_screen_row(std::size_t screen_row) const -> RowBands;

    /// @brief 存储行对应的命中列区间切片；本行没有命中即空。表按 (行, 列) 升序，故两次二分。
    [[nodiscard]] auto hit_slice(std::size_t storage_row) const noexcept -> std::span<const RowSpan>;

    /// @brief 把存储行滚进可见窗并尽量居中；已在窗内则一个像素都不动（判据 D1-b）。
    auto scroll_row_into_view(std::size_t storage_row) -> void;

    /// @brief 把顶边位移的增量折算进选区（裁决 7.38⑤ / 7.39①⑤：整段被推出顶端即作废）。
    auto compensate_selection_drift() -> void;

    /// @brief 在既有短临界区内结算一次搜索扫描（裁决 7.78⑥ 的执行侧）。
    ///
    /// 只在 `search_dirty_` 时被 `on_frame` 调用，且调用点就在 `Session::read` 的那一次锁内：
    /// `ui::search` 吃的是权威网格，出锁就没有第二个一致快照可用（与 `selected_text` 的持锁先例
    /// 同一条路径），故本入口不新开线程、不做网格快照，一轮输入只扫一次。
    /// @param grid 锁内的权威网格。
    auto run_search_scan(grid::Storage &grid) -> void;

    /// @brief 把顶边位移的增量折算进匹配表（与选区共用 `mirror_` 带出的同一份读数，零新增锁）。
    auto compensate_search_drift() -> void;

    /// @brief 落地攒下的 copy-on-select 请求：在帧边界做 IO，不在事件回调里做（AGENTS.md §4.5 第 25 条）。
    auto flush_copy_request() -> void;

    /// @brief 右键按下：按 `plan_right_click` 的意图分发到菜单 / 直接复制 / 直接粘贴（裁决 7.41）。
    auto on_right_click(aurora::MouseEvent &e) -> void;

    /// @brief 用公共 API 组合出上下文菜单（`au::Popup` 承载一列 `au::Button`）并挂到宿主上。
    ///
    /// 框架的 `Modifier::context_menu` 只有状态模型、没有渲染与点击派发，而这一套组合足以在应用侧
    /// 做出可断言的菜单，故按裁决 7.13② 留在本仓而不登记缺口（裁决 7.41③）。
    auto open_context_menu(aurora::Point where, const std::vector<MenuItem> &items) -> void;

    /// @brief 菜单条目被点到：先收菜单，再把动作攒到帧边界（与 copy-on-select 同一条 IO 纪律）。
    auto on_menu_command(MenuCommand command) -> void;

    /// @brief 帧边界落地粘贴：读剪贴板 → `term::plan_paste` → 多行先确认 → 按计划排期发送。
    auto flush_paste_request() -> void;

    /// @brief 多行粘贴的默认确认呈现：宿主的模态对话框（`au::Dialog` + `au::confirm`）。
    auto ask_multiline_warning(const term::PastePlan &plan, std::function<void(bool)> answer) -> void;

    /// @brief 按计划把各块交进会话：块间延迟经 `Scheduler::set_timeout` 累积排期。
    auto send_paste_plan(const term::PastePlan &plan) -> void;

    session::Session *session_ = nullptr;  ///< 非拥有。
    PaletteSpec spec_{};
    aurora::Font ref_font_{};
    Typography typography_{};
    float padding_dp_ = 0.0F;
    std::chrono::milliseconds blink_period_{500};
    InteractionOptions options_{};

    session::ScreenMirror mirror_;
    GridGeometry geometry_{};
    TypedMetrics typed_{};
    /// @brief 与本帧排版量同源的排版选项：按族回退链在构造时装好，字距随度量刷新写入。
    ///
    /// 两处落笔点共用这一份，是为了让「链漏进一处、字距漏进另一处」不可能发生（光标第三段必须
    /// 与批量那次同源，否则停在同一格上的字形会换一种排布）。绘制侧按行取一份副本（链的承载形态
    /// 是框架的定长数组，缺省链为空故副本通常只是几个标量）。
    aurora::render::TextLayoutOpts layout_opts_{};
    float metrics_scale_ = -1.0F;  ///< `typed_` 所属的缩放；与入参不等即重取度量。
    bool metrics_stale_ = true;    ///< 字体或排版入参变过（Ctrl+滚轮缩放是唯一改点）；取用即清。

    CursorState cursor_{};
    CursorState painted_cursor_{};  ///< 上次标脏时的光标，用于判断「本帧到底有没有可画的东西」。
    std::size_t total_lines_ = 0;   ///< scrollback + 视口的总行数，指示条与回看窗口都要用。

    bool blink_on_ = true;
    aurora::TimerHandle blink_timer_;
    session::Size requested_size_{};  ///< 上次交出去的行列（直发或交给 sink 都算），同值不重发。
    /// @brief 一次性吞掉紧随其后的文本事件：应用模式的小键盘数字已由按键通道发成 SS3，而 Windows
    ///        无论 `DECKPAM` 都给同一物理键再发一条 `WM_CHAR`（`term::is_keypad` 的判据）。
    bool swallow_next_text_ = false;

    std::optional<Selection> selection_;  ///< 端点是**存储行序**（`ui/selection.h` 的坐标约定）。
    std::vector<RowSpan> bands_{};        ///< `row_spans` 的产物，行号升序；绘制与复制共用这一张表。
    GridCellPos pressed_cell_{};          ///< 按下那一格（同为存储行序），拖拽的两端之一。
    DragMode drag_mode_ = DragMode::Cell;
    bool dragging_ = false;
    /// @brief 上报在途的按下是哪一个键（框架四后端都把 Move 盖成 `MouseButton::Left`，故悬停与拖动
    ///        只能由本层自己的在途态区分，不能问事件携带的按键；SGR 档的松开还要靠它带回按键编号）。
    std::optional<term::MouseButton> reported_press_{};
    /// @brief 上一次运动上报落在哪一格（**可见区**行号，与上报协议同一坐标系，不是选区用的存储行序）。
    ///        只用于按格去重（裁决 7.77④），按下与松开都把它清掉。
    std::optional<GridCellPos> reported_motion_{};
    bool copy_pending_ = false;  ///< 抬起已发生、复制留到下一帧的 `on_frame` 落地。
    std::int64_t dropped_baseline_ = 0;  ///< 上次折算选区时的顶边位移读数（裁决 7.39⑤）。

    /// @brief 搜索的状态：条件、产出当前表的那份条件、表本身、表自己的顶边位移基准，另加脏 / 非法 /
    ///        计数三个闸门。
    ///
    /// `search_scanned_` 与 `search_query_` 分开存，是为了让「输入框里的文本已经不是画面上的高亮」
    /// 这一句物理事实可读（判据 B5）；两份条件各记一次位移基准（`dropped_baseline_` 给选区、本条给
    /// 表），因为重扫时刻与选区建立时刻并不同一条时间线，共用一个基准会让刚扫完的表被折算两次。
    SearchQuery search_query_{};
    SearchQuery search_scanned_{};
    std::optional<SearchMatches> search_matches_{};
    std::int64_t search_dropped_baseline_ = 0;
    bool search_dirty_ = false;    ///< 本帧要扫（字面量档每键置脏、正则档只在提交时置脏）。
    bool search_invalid_ = false;  ///< 上次扫描因表达式非法而保留旧表（判据 B4）。
    std::size_t search_scans_ = 0; ///< 实际扫描次数；空文本的清表不计（判据 A2-a）。

    /// @brief 浮层本体的三条外部依赖（均非拥有，装配阶段经 `set_overlay_host` 与
    ///        `set_search_dependencies` 交）：建浮层那一刻一次读齐。
    aurora::ShortcutRegistry *shortcuts_ = nullptr;
    aurora::FocusManager *focus_ = nullptr;
    /// @brief 懒建并常驻的搜索浮层（判据 F1-c）；析构时随本控件一起收，故其解绑与摘浮层在
    ///        `~SearchOverlay` 里做完。声明在含 `host_` 的那一组之前，因为「持有」与「开关」是本控件
    ///        的两件事，而菜单与警告对话框是右键那一棒的既有形态。
    std::unique_ptr<SearchOverlay> search_overlay_{};

    Presentation presentation_{};         ///< 剪贴板与多行确认的三条接缝（空字段即生产实现）。
    GridSizeSink grid_size_sink_;         ///< 空即直发 `Session::resize`（未挂工作区层的形态）。
    KeyPreFilter key_pre_filter_;         ///< 空即不认领任何键（单 pane 装配的形态）。
    aurora::OverlayHost *host_ = nullptr; ///< 非拥有：装配层给的浮层宿主，本控件的父节点。
    std::shared_ptr<aurora::Popup> menu_;                    ///< 懒建的菜单浮层（建成后一直是宿主的浮层子节点）。
    std::shared_ptr<aurora::Dialog> multiline_warning_;      ///< 懒建的多行粘贴确认对话框。
    bool paste_pending_ = false;  ///< 粘贴已请求、剪贴板读取留到下一帧的 `on_frame` 落地。
    std::optional<term::PastePlan> pending_paste_;  ///< 等用户确认的处置计划（对话框放行才发送）。
    std::vector<aurora::TimerHandle> paste_timers_; ///< 在途的逐块节流任务；析构时统一取消。
};

}  // namespace borealis::ui
