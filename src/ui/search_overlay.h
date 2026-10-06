#pragma once

// ============================================================
// 终端内搜索的浮层本体（src/ui/search_overlay.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.04` 的呈现腿，判据文 `codespec/UI_SEARCH.draft.md` 的 A ~ F 段落在这里；
// 匹配表、扫描与跳转的算式都在 `include/borealis/ui/search.h` 与视口侧，本件**一次比对也不做**——
// 它只把视口已经算好的那三档状态（条件、计数、是否非法）翻成控件与文字，于是「浮层显示的次数」
// 与「画面高亮的那一份结果」在结构上不可能分叉。
//
// 形态由判据文四条拍下：① 单行条挂在**场景根** `au::OverlayHost` 上（A1-a），锚点是焦点 pane 内容盒
// 的右上角往内 8 dp，故本件认识视口而不反过来——视口侧只留「持有与开关两处」（判据文 §4 第 5 条）；
// ② 懒建一次、常驻复用（F1-c）：`open()` 之后只 `open_at` / `close`，重开时文本与两档由 `view_`
// 逐字读回，本件不存第二份查询；③ 三档宽度按 pane 宽反推（E 段），换档才 `set_content` 重建一棵树；
// ④ 关闭走三条真路径（点「关闭」/ `Escape` / 点浮层外）而 **Enter 恒不关**（F2-a，那是一条负向断言
// 而不是第四条路径），三条腿都汇到 `Popup::close()` 的那一个 `on_close` 回调，故解绑 `Escape` 与清
// 高亮只有一处代码。
//
// 五条框架实测决定了本件的写法：
// ① `Popup::on_layout` 按**无界约束**测内容（其头注自陈「在常规流中占零尺寸」），所以条宽与条高
//    必须由本件的显式 `width` / `height` 下达，否则 E 段那三档尺寸在布局之后不作数。
// ② `Popup::handle_outside_click` 在关闭态直接回 `false`，而 `OverlayHost` 的循环是**从最上层往下**
//    问每一只打开的 `Popup`；视口的 Press 分支早已无条件调过一次 `host_->handle_outside_click(position)`
//    （裁决 7.41③ 那条外部点击自驱），故「点浮层外即关」在浮层这条腿上**不需要新代码**——本件只需
//    保证自己就是宿主上那一只打开的 `Popup`。射程限制照旧登记：落点在视口之外（标签条、把手）时
//    没人代发那一次判定，与已交付的右键菜单同一条限制。
// ③ `open_at` 只在**派发栈内**取到 `current_focus_manager()` 时才压焦点作用域，而本件的打开入口有两处
//    来自栈外（命令层 `search.open` 与用例的编程调用）。栈外取不到就不压，「打开即得焦点」那条判据
//    （§5 第 1 条）因此静默失效，故 `open()` 自己读一次 `FocusManager` 并在缺失时补 `push_scope`——
//    与 #117 启动对话框同一处置（裁决 7.68⑤）。关闭侧的对账**按作用域深度**而不是按「关闭现场是否在
//    栈内」：`Popup::close()` 只在它自己也取到了 `fm` 时才弹，于是「栈内开（它压了）＋ 栈外关（它没弹）」
//    那一条真实组合（`Ctrl+F` 的命令回调开、用例或视口的程序化关）会把作用域永久漏在那里；本件记下打开
//    前的 `scope_depth()`，在 `on_close` 里只要**当前深度还高于那一份读数**就自己弹回来，故两条开合
//    路径的四种组合都不漏也不双弹（裁决 7.81）。
// ④ `TextInput::on_key_event` 的 Enter 分支不区分 Shift，且框架的 `Modifier::clickable` 回调**不携带
//    坐标**；两条合起来意味着 `Shift+Enter` 后退（D1-a）只能由本件私有子类在基类之前认领那一次
//    `Down`，与面板的 `BlurCommitText` 同档（裁决 7.52 的 S14）。
// ⑤ `Widget::set_accessibility_label` 是**显式覆盖**且在无障碍树里优先于由标签推导的那一份
//    （`a11y_tree.h` 的解析次序），于是 A1-d 那两枚 chip 可以「可视标签是 ASCII 符号、朗读标签是
//    中文词条」而不必先造一个 `SymbolButton` 子类去改 accessible name。
//
// 计数那一格（判据 B0 ~ B5）是**一个函数、一条优先级链**，且分母先算、序号后挂：
// `cursor()` 为空时只给分母而不硬编 `1/N`（B1 明写「从 1 起算」，但新扫出的表本来就没有游标，
// 挂一个 1 就是谎报「当前命中在第一段上」）；触顶时分母走 `search.count_cap` 的 `{0}+`，
// `10000` 取自 `ui::kMaxSearchMatches` 而不是界面里写死的那个串。
//
// `Enter` 那一键把判据 D1-a（Enter 前进）与 D4（正则档的提交时机）撞在同一格上，取的是**有待提交的条件
// 就只提交、没有才跳**（裁决 7.81）。「先提交再跳」在两档上都会坏：字面量档的提交把刚扫出的那份表重扫成
// 一张**游标为空**的新表，于是「Enter 前进」永远前进不了；正则档则在新表落地之前先在旧表上跳一格。
//
// 私有头（裁决 D1① 同口径）：本件含框架类型，不进 `include/borealis/`。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "aurora/widget/popup.h"

namespace aurora {
class Button;
class FocusManager;
class ShortcutRegistry;
class Text;
class TextInput;
}  // namespace aurora

namespace borealis::ui {

class TerminalView;  ///< 非拥有：查询条件、匹配表与跳转/关闭入口都归它（`terminal_view.h`）。

/// @brief 浮层的一条档位（判据文 E1 ~ E3）：由焦点 pane 的内容宽度决定摆哪几枚控件。
enum class SearchTier : std::uint8_t {
    Wide,     ///< E1：输入框 + 计数 + 两枚 chip + 两个中文跳转钮 + 关闭，条高 44 dp。
    Compact,  ///< E2：同上，但计数收窄、跳转钮换成 `↑` / `↓` 符号，条高 44 dp。
    TwoRow,   ///< E3：跳两行档（条高 66 dp），跳转钮**不再出现**（判据 E3-a 末句）。
};

/// @brief Ctrl+F 搜索浮层（`SPEC.FEAT.INTERACT.04` 的呈现腿，判据文 A ~ F 段）。
///
/// 生命周期：视口**懒建**本件并常驻（F1-c），构造即把浮层登记进宿主；`open()` 只定位与弹层，
/// `close()` 只收层；析构才摘浮层。摘除次序按裁决 7.67——先弹还在世的焦点作用域、再放自持的控件
/// 句柄、最后 `remove_overlay`，否则那些仍被本件持有的控件会落在框架「活在容器之外被摘走」那一档而逐子刷屏。
class SearchOverlay final {
  public:
    /// @brief 建浮层：建一只**空内容**的 `Popup` 挂上宿主并装好 `on_close`；条体在第一次 `open()` 才建。
    ///
    /// 空内容不是省事：`Popup` 的绘制与命中四入口都在 `!open_ || !child_` 上早返回，故「登记而未打开」
    /// 的浮层结构上是个哑节点，而视口的懒建（F1-c）正是靠这一点——不打开就不必先测 pane 宽。
    ///
    /// `view` 与 `host` 都是**非拥有**引用：前者是本件的唯一状态源，后者的节点树包含本件的浮层，
    /// 故其析构必须晚于本件（与视口的 `set_overlay_host` 同一条约定）。
    /// @param view 挂载它的那个视口。
    /// @param host 场景根的浮层宿主。
    /// @param shortcuts 快捷键登记表（`Escape` 按 F2-a「打开登记、关闭解绑」在两处动它，故登记在 `open()`）。
    /// @param focus 焦点管理器（`open()` 在派发栈外取不到栈内实例时用它补压作用域）。
    SearchOverlay(TerminalView &view, aurora::OverlayHost &host, aurora::ShortcutRegistry &shortcuts,
                  aurora::FocusManager &focus);
    SearchOverlay(const SearchOverlay &other) = delete;
    auto operator=(const SearchOverlay &other) -> SearchOverlay & = delete;
    SearchOverlay(SearchOverlay &&other) = delete;
    auto operator=(SearchOverlay &&other) -> SearchOverlay & = delete;
    ~SearchOverlay();

    /// @brief 打开浮层：按当前 pane 宽定位三档之一、登记 `Escape`、弹层并把焦点交给输入框（A1-a / F2-a / F3-a）。
    ///
    /// 幂等：已开着就只重定位，**不重复压作用域**（`Popup::open_at` 的 `!open_` 守卫只挡它自己那一支，
    /// 而本件补压那一支由「当前深度 ＝ 打开前读数」这一判据把住，见文件头③）。视口尚未布局（取不到
    /// `window_bounds()`）时不弹——判据 A1-a 的锚点是内容盒，猜一个坐标就是把浮层画在错误的位置上。
    auto open() -> void;

    /// @brief 关闭浮层：收层即触发 `on_close`，`Escape` 的解绑与高亮的清理都在那一条路径上。
    auto close() -> void;

    /// @brief 是否开着（`Popup` 的打开态，本件不另存一份 bool 以免两处读出不一致）。
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return popup_ != nullptr && popup_->is_open();
    }

    /// @brief 视口每帧尾部同步一次：翻两枚 chip 的档色、重算计数那一格、开合两枚跳转钮。
    ///
    /// 挂在 `on_frame` 而不是控件自己的绘制里：扫描结果在帧边界才确定（锁内结算），而计数要说的是
    /// **这一帧画面上的那一份结果**；关闭态不需要这些，早返回。
    auto sync_state() -> void;

    /// @brief 视口每帧尾部再同步一次几何：pane 宽变了就换档、锚点变了就重定位（A1-a / E 段）。
    ///
    /// 只在开着时做事；未开时档位与锚点留到下一次 `open()` 现算，于是 resize 期间不会有半档状态。
    /// 盒宽从 `TerminalView::window_bounds()` 现读而不是由视口把刚拿到的那个 `Rect` 交进来：控件在
    /// 自己 `on_layout` 里读的 `bounds()` 是上一帧的值（父侧在量完之后才写 bounds），故布局之后没有
    /// 任何人手里有本帧的盒。代价是 resize 之后的换档晚一帧，登记为判据文的射程限制而不另造补偿。
    auto sync_geometry() -> void;

    /// @brief 当前档位（用例据此断 E 段那三组控件尺寸）。
    [[nodiscard]] auto tier() const noexcept -> SearchTier {
        return tier_;
    }

    /// @brief 条宽（dp）：E1 ~ E3 断的是「控件盒宽」，而用例不必再复制一遍钳位算式。
    [[nodiscard]] auto bar_width() const noexcept -> float {
        return bar_width_;
    }

    /// @brief 浮层本体的只读句柄（用例按 `content_bounds()` 取锚点与尺寸）。
    [[nodiscard]] auto popup() const noexcept -> const aurora::Popup * { return popup_.get(); }

    // 以下七枚是控件级只读观测点，与面板的 `theme_card()` / `swatch_slot()` 同一条理由：判据要按
    // 真实命中链点它们、要读它们的盒宽，而这些都不该由用例自己复制一套布局算式去反推。返回类型取
    // 各控件本型而不是 `Widget *`：本件成员就是按本型持有的，向上转型在头里要靠完整类型，而完整类型
    // 一旦进本私有头就得连 include 三件框架控件头——用例反正要读 `Button::enabled` 与 `Text::content`。
    [[nodiscard]] auto input() const noexcept -> aurora::TextInput * { return input_.get(); }
    [[nodiscard]] auto case_toggle() const noexcept -> aurora::Button * { return case_toggle_.get(); }
    [[nodiscard]] auto regex_toggle() const noexcept -> aurora::Button * { return regex_toggle_.get(); }
    [[nodiscard]] auto prev_button() const noexcept -> aurora::Button * { return prev_.get(); }
    [[nodiscard]] auto next_button() const noexcept -> aurora::Button * { return next_.get(); }
    [[nodiscard]] auto close_button() const noexcept -> aurora::Button * { return close_.get(); }

    /// @brief 计数那一格当前显示的那串字（B0 ~ B5 的判据面）。
    ///
    /// 交解析后的显示串而不是词条 key：判据比的是「用户看得见的」，`2/7` 与 `10000+` 这些形态
    /// 只在解析之后存在，而 key 加参数的组合是第二套需要用例再复算一遍的算式。
    [[nodiscard]] auto count_text() const -> std::string;

  private:
    /// @brief 该档位下应摆的控件集与条宽（E 段的反推公式 `W = clamp(box 宽 − 16, 本档下限, 560)`）。
    /// @param box_width 焦点 pane 内容盒的宽度（dp）。
    /// @return 档位与该档的条宽（dp）。
    [[nodiscard]] static auto tier_for(float box_width) -> std::pair<SearchTier, float>;

    /// @brief 按档位建一棵全新的条体，并把七个控件句柄指向它的各件。
    /// @param tier 目标档位。
    /// @param width 该档的条宽（dp）。
    /// @return 条体节点。
    auto build_bar(SearchTier tier, float width) -> aurora::Node;

    /// @brief 换档：重建条体并交进同一只 `Popup`（同档不换，故打字与滚动都不动树）。
    /// @param next 目标档位。
    /// @param width 该档的条宽（dp）。
    auto retier(SearchTier next, float width) -> void;

    /// @brief 锚点：pane 内容盒右上角往内 8 dp（A1-a）。
    [[nodiscard]] auto anchor_for(const aurora::Rect &box) const -> aurora::Point;

    /// @brief `Escape` 的快捷键回调：只收层。浮层里没有抓取键盘的控件（面板的 `ReorderableList` 那一处
    /// 「先交还抓取」在本件不存在），故不复制那条交接。
    auto handle_escape() -> void;

    /// @brief 唯一的关闭落点：`Popup::on_close`。作用域对账、解绑 `Escape`、清高亮三件事在此收口。
    auto on_popup_closed() -> void;

    /// @brief 把两枚 chip 的前后景色写成当前档（开档 `accent` 底、关档 `control_bg` 底）。
    auto refresh_toggles() -> void;

    /// @brief 两枚 chip 与输入框的现值：一律现读 `view_.search_query()`，本件不存第二份（A1-g / F1-c）。
    auto apply_query(bool case_sensitive, bool regex) -> void;

    /// @brief 输入框每一键：解码后交进视口的查询条件（字面量档由视口置脏）。
    /// @param text UTF-8 原文（框架文本事件给的是 UTF-8）。
    auto on_text_changed(const std::string &text) -> void;

    /// @brief `Enter` 与 `Shift+Enter`：有待提交的条件就只提交（这一次 `Enter` 是 B5 的「应用」），
    ///        否则在已结算的那份表上跳一格（D1-a / D4，取舍见文件末那段）。
    /// @param shift 是否按住 Shift（后退档）。
    auto on_enter(bool shift) -> void;

    TerminalView &view_;                              ///< 非拥有：本件的唯一状态源。
    aurora::OverlayHost &host_;                       ///< 非拥有：本件的浮层挂在这里。
    aurora::ShortcutRegistry &shortcuts_;             ///< 非拥有：`Escape` 的登记表。
    aurora::FocusManager &focus_;                     ///< 非拥有：栈外打开时补压作用域用的那个管理器。
    std::shared_ptr<aurora::Popup> popup_;            ///< 常驻浮层（建成即登记，析构才摘除）。
    std::optional<std::size_t> overlay_index_{};      ///< 宿主浮层序号；`add_overlay` 在空宿主上回空。
    int escape_binding_ = 0;                           ///< `Escape` 的登记句柄（0 ＝ 未登记）。
    std::size_t scope_depth_before_ = 0;               ///< 打开那一次读到的作用域深度，关闭时按它对账。

    SearchTier tier_ = SearchTier::Wide;
    float bar_width_ = 0.0F;
    aurora::Point anchor_{};

    std::shared_ptr<aurora::TextInput> input_;        ///< 输入框（本型是 `SearchInput`，见 .cpp 的私有子类）。
    std::shared_ptr<aurora::Text> count_;             ///< 计数槽（`Text`）。
    std::shared_ptr<aurora::Button> case_toggle_;     ///< `Aa`。
    std::shared_ptr<aurora::Button> regex_toggle_;    ///< `.*`。
    std::shared_ptr<aurora::Button> prev_;            ///< 上一个。
    std::shared_ptr<aurora::Button> next_;            ///< 下一个。
    std::shared_ptr<aurora::Button> close_;           ///< 关闭。
    bool reopen_hint_ = false;                        ///< 重开那一帧的 B5 提示档（判据 F1-c，首次改动查询即清）。

    // `sync_state()` 每帧都跑，而框架的这三处写入口都不自带脏标记：`Button::background()` /
    // `text_color()` 与 `Text::set_content()` 只改值（只有 `set_enabled()` 无条件标脏），于是
    // 「每帧重画浮层」与「每帧把两枚钮重新 enable 一遍」都会让 `Window::is_idle_frame()` 的整帧跳过
    // 在浮层开着的那段时间里彻底失效（违 `SPEC.NF.PERF.06` 的 UI 侧纪律）。计数那一格直接读控件自己
    // 的 `content` 当上一次的值，故这里只有三枚守卫：两枚 chip 的档位与跳转钮的可用态；
    // `build_bar()` 建好一棵新树时把三者一并播种，故换档本身不产生一次多余的重绘。
    bool painted_case_ = false;
    bool painted_regex_ = false;
    std::optional<bool> painted_nav_{};               ///< 两枚跳转钮的可用态（空 ＝ 尚未写过）。
};

}  // namespace borealis::ui
