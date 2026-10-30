#pragma once

// ============================================================
// 屏 1 那扇自绘标题栏（src/ui/app_title_bar.h，私有头）
// ------------------------------------------------------------
// `codespec/UI_WORKSPACE.draft.svg` 屏 1 顶部那条 36 dp 带的落地（`codespec/UI_OVERVIEW.draft.md`
// §2.2「标题栏高 36 dp，四屏一致」），需求腿是 `SPEC.FEAT.WS.06` 那句「窗口标题栏显示当前会话名」。
//
// 形态按人已裁的**「继承框架侧的 TitleBar，在此基础上开发」**（裁决 7.181）：本件派生
// `aurora::TitleBar`，复用的是它的**交互腿**——空白区按下即 `WindowChrome::begin_move()`（必须留在
// Press 派发栈内同步调用，Wayland serial 时效）、空白区双击切换最大化、失焦变暗的 `WindowState`
// 闸口、以及 `serialize_props` 往返；稿面独有的六件（徽标「B」、应用名、上下文串、竖分隔线、
// 三枚窗口钮、F11 提示）由本件在 `on_paint` 里落笔。基类的 `on_paint` **不被调用**：它那一支画的是
// 自己的图标/标题/chips/Adwaita 三枚钮，与本件要落的位置互不相容，故本件把这条带整幅自绘。
//
// 三枚钮刻意**不走**基类的 `title_bar_geometry()`：那条算式给统一 pitch（Adwaita 档盒 30 dp、
// 间距 3 dp），而稿面把「F11」四字插在 ▢ 与 ✕ 之间——非等距，算式表达不了。故本件按稿面逐枚的
// 右缘内缩自定热区，并把基类 `set_window_controls(false)`：否则两套钮会同时吃点击，且基类的
// 悬停 Snap 弹窗（≥400 ms 停在最大化钮上）会在稿面没有的地方开出一块菜单。
//
// 全屏档按人已裁的**「全屏就不挂这条，退出挂回」**：落地形态是**常驻节点自报 0 高**而不是摘除
// ——与横幅「组空时高 0 也不摘除」、状态栏「没有读数那一帧也留着这条」同一条纪律（裁决 7.180
// 抬头），摘除会让根 Column 的首格随几何态起伏，而那一格的增删正是 G34 那一族时序的现场。
// 观感与裁决定的一致：全屏时这条带不占 36 dp、不接点击，退出全屏即回来。
//
// 色值一律取自 `ui::settings_chrome()` 的绘制时读数（两档都通，`codespec/UI_CHROME.draft.md` §1 是
// 唯一色值表）；稿上那三个不在 13 枚 token 里的十六进制值按既有口径改钉最近的在册格并登记偏差
// （底缘那条线取 `card_line` 而不是稿的 `#3A3C4C`，同 7.179④c 与折叠轨那条右沿线）。
//
// 类声明含 Aurora 类型，按裁决 D1① 留 `src/`。
//
// 自描述面**不重写**：基类那张 `WidgetDescriptor` 是类型级属性表，本件只多序列化一枚 `context`
// （往返用例要它），把那张表抄一份过来就是给仓库加第二处漂移面——按 AGENTS §4.1 第 3 条不预做。
// ============================================================

#include <functional>
#include <string>
#include <string_view>

#include "aurora/widget/title_bar.h"

namespace au = aurora;

namespace borealis::ui {

/// @brief 标题栏上那三枚窗口钮的身份（观测面与装配层接管腿共用的枚举）。
enum class AppTitleBarAction : std::uint8_t {
    Minimize,       ///< 最小化（稿面那枚「—」）
    ToggleMaximize, ///< 最大化／还原（稿面那枚「▢」）
    Close,          ///< 关闭（稿面那枚「✕」）
};

/// @brief 屏 1 那条带的上下文串形态：「<目录> · <会话名>」。
///
/// 单独成一支纯函数的理由：分隔符是**上屏文案**的一部分（要标 `CJK-LITERAL`），而装配层按硬规则 14
/// 只许 ASCII 字面量——把折叠留在本件，装配层就只交两截料。目录为空（非本地腿、会话还没报 OSC 7）
/// 即只给名字，两者都空即空串（本件那一格随之不画）。
/// @param directory UTF-8 目录（可空）。
/// @param session UTF-8 会话名（可空）。
/// @return 折叠后的串（全空时为空串）。
[[nodiscard]] auto format_title_context(std::string_view directory, std::string_view session)
    -> std::string;

/// @brief 屏 1 的自绘标题栏（继承 `aurora::TitleBar` 的交互腿，自绘稿面那六件）。
///
/// 继承扩展点：无（`final`）。
/// @note Thread: main-thread only
/// @note Rebuildable: yes, via `set_context`／`set_title`（基类那支）
class AppTitleBar final : public au::TitleBar {
public:
    /// @brief 与装配层的接缝（struct-of-回调，同 `ConnectionSidebar::Hooks` 形态）。
    struct Hooks {
        /// @brief 本件不认识 `config::Store`，也不认识会话：关掉一扇窗在本仓是**要问确认**的动作
        ///        （`SPEC.FEAT.WS.03` 稿 D4／G44 那道闸门）。装配层把 `workspace.close_window` 那条腿
        ///        （同一个 `request_close_window`）填进来；未填时回落到 `WindowChrome::close()`，
        ///        那是无闸门形态（用例、以及不需要确认的面板窗）。
        std::function<void()> request_close;
        /// @brief 三枚钮各命中一次的观测面（**下发之后**才报，故报出去的那一枚已经真的做过）。
        ///
        /// 无头后端把 `minimize()`／`toggle_maximize()` 记成空操作，`WindowChrome` 那条腿在测试里
        /// 读不到结果——这一枚就是本件唯一的效果观测点（与折叠轨的 `note_press` 同一条思路）。
        std::function<void(AppTitleBarAction action)> note_window_action;
    };

    /// @brief 建一条 36 dp 的标题栏（应用名与徽标由本件自绘，上下文串待装配层喂）。
    /// @param hooks 与装配层的接缝；两枚都可空（空即只走框架 chrome、不上报）。
    explicit AppTitleBar(Hooks hooks);

    AppTitleBar(const AppTitleBar &) = delete;
    auto operator=(const AppTitleBar &) -> AppTitleBar & = delete;
    ~AppTitleBar() override;

    /// @brief 控件类型名（控件树/序列化用）。
    /// @return 常量字符串 "AppTitleBar"。
    [[nodiscard]] auto type_name() const -> const char * override { return "AppTitleBar"; }

    /// @brief 设置上下文串（稿面「— src/ui · PowerShell (本地终端)」那一格的真值位）。
    /// @param context UTF-8 串（空串＝这一格不画；本件不自己加「—」以外的前缀）
    /// @return 自身引用（链式调用）
    auto set_context(std::string context) -> AppTitleBar &;

    /// @brief 当前上下文串。
    [[nodiscard]] auto context() const -> const std::string & { return context_; }

    /// @brief 当前这一档这条带占多高（全屏档为 0，其余为 36）。
    ///
    /// 是**上次布局算出的值**而不是常量：判据要断言的就是「全屏那一帧它让位了」，读常量证不了。
    [[nodiscard]] auto height_dp() const noexcept -> double { return height_; }

    /// @brief 稿面那枚「—」的热区（本地坐标 dp，与 `paint_bounds()` 同系；全屏档为空盒）。
    [[nodiscard]] auto minimize_box() const noexcept -> const au::Rect & { return minimize_box_; }
    /// @brief 稿面那枚「▢」的热区（同上）。
    [[nodiscard]] auto maximize_box() const noexcept -> const au::Rect & { return maximize_box_; }
    /// @brief 稿面那枚「✕」的热区（同上）。
    [[nodiscard]] auto close_box() const noexcept -> const au::Rect & { return close_box_; }
    /// @brief 应用名那一段的墨迹盒（本地坐标 dp；空标题为空盒）。
    ///
    /// 左沿恒在稿面的 `x=38`（徽标右缘之外），右端随字宽走——用例比的是这一格，不去读浮层树。
    [[nodiscard]] auto title_box() const noexcept -> const au::Rect & { return title_box_; }
    /// @brief 上下文串那一段的墨迹盒（本地坐标 dp；无串或窄窗放不下时为空盒）。
    ///
    /// 左沿**跟着标题右端走**，不是稿那个固定的 `x=105.2`：105.2 只在「Borealis」这一枚字宽下成立，
    /// 换成更长的会话名就会压住上下文串（偏差登记在裁决 7.181）。
    [[nodiscard]] auto context_box() const noexcept -> const au::Rect & { return context_box_; }
    /// @brief 「F11」提示串的墨迹盒（同上）。
    ///
    /// **只作观测点，不是热区**：稿把它画在 ▢ 与 ✕ 之间但没有画成按钮，且全屏的退出只能经 F11
    /// （这条带在全屏那一档根本不出现）。按运行时基线登记为不可点，与 7.179④d 那条锚点同口径。
    [[nodiscard]] auto hint_box() const noexcept -> const au::Rect & { return hint_box_; }

    /// @brief 序列化：基类字段（height/title/subtitle/window_controls/snap_action_count）之外追加 context。
    /// @param props 目标 JSON 对象
    auto serialize_props(au::Json &props) const -> void override;

    /// @brief 反序列化：基类字段之后还原 context（缺字段保持现值）。
    /// @param props 来源 JSON 对象
    auto deserialize_props(const au::Json &props) -> void override;

    /// @brief 指针事件：三枚钮的热区**先于**基类判定；其余（空白拖拽、双击最大化）原样交基类。
    /// @param e 鼠标事件（局部坐标；命中即置 `is_handled`）
    auto on_pointer_event(au::MouseEvent &e) -> void override;

  protected:
    auto on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size override;
    auto on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void override;

  private:
    /// @brief 全屏档判定：读环境里的 `WindowMode`（`Window::prepare_context` 每帧重注入）。
    [[nodiscard]] auto is_fullscreen() const -> bool;

    /// @brief 按栏宽重算三枚热区与提示盒的本地坐标（全屏档给全空盒）。
    auto layout_controls(float width) -> void;

    /// @brief 一枚钮：经 `WindowChrome` 或接管腿下发，随后上报观测面。
    auto dispatch(AppTitleBarAction action) -> void;

    Hooks hooks_;
    std::string context_;
    double height_ = 0.0;  ///< 上次布局这条带占的高（dp）
    au::Rect title_box_{};
    au::Rect context_box_{};
    au::Rect minimize_box_{};
    au::Rect maximize_box_{};
    au::Rect close_box_{};
    au::Rect hint_box_{};
    const au::Environment *bar_env_ = nullptr;  ///< 事件与绘制阶段读 `WindowChrome`／`WindowMode`
};

}  // namespace borealis::ui
