#pragma once

// ============================================================
// 状态栏（src/ui/status_bar.h，私有头）
// ------------------------------------------------------------
// 类声明含 Aurora 类型，故按裁决 D1① 留在 `src/`。本件是屏 1 判据 ⑨／⑩ 的载体
//（`codespec/UI_WORKSPACE.draft.svg` 的编号块，稿面把这一条叫「SPEC.FEAT.PREF.02 的即时反馈面」）：
// 一条 28 dp 全宽、常驻每扇窗根 Column 末位的读数条，左端按注册次序排数据格，右端排命令面板锚点。
//
// 四条形态口径：
// - **尺寸与色值只取 chrome 表**：高 28 dp、字号 11.5 pt、底 `card_bg`、四类前景（`text_dim` /
//   `text_faint` / `warning` / `accent`）与分隔线 `card_line`、连接态圆点 `success`，全部来自
//   `codespec/UI_OVERVIEW.draft.md` §2.1／§2.2 那张表（与广播横幅同一条「不随终端主题」口径，稿 N6）；
//   稿面在条上只画过「已连接」那一态，断线与重连按判据 ⑧ 画在 pane 上，故本件没有 danger 档的点；
// - **降级算式不在本件**：哪几格上屏由 `plan_status_bar()`（`status_bar_format.h`）按 N3 那句
//   「按注册次序从右向左省略、不压缩字号」算，本件只把每格的实测宽度交进去、再按结果落笔，
//   并把那份结果以 `plan()` 交出供用例与走查读——被省略项的去处（N3 那句「进右键菜单」）在另一条腿上，
//   `TODO(SPEC.FEAT.PREF.02)` 记在 `plan()` 的注释里；
// - **本件不读配置、不认识会话**：格子的文本、颜色档、有没有圆点与分隔线，都由装配层折进
//   `StatusBarState` 交来（与横幅同一条纪律：文本只有一个真值源）；
// - **不可点、不获焦**：稿判据 ⑩ 说的是「Ctrl+Shift+P 呼出」，锚点因此是提示而不是入口，把提示画成
//   可点按钮就是自造一条入口（本条形态自裁的根据写在裁决里）。
// ============================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/widget/widget.h"

#include "status_bar_format.h"

namespace au = aurora;

namespace borealis::ui {

/// @brief 一格的前景取 chrome 表的哪一枚 token。
///
/// 只有三档：稿面在状态栏上画出过的颜色就是 `mut`（连接那格）、`dim`（普通读数）、`yellow`
///（剪贴板禁止那一档）与 `accent`（右端锚点），而锚点由本件固定用 `accent`，不进这个枚举。
enum class StatusBarTone : std::uint8_t {
    Dim,     ///< `text_dim`——连接态那一格。
    Faint,   ///< `text_faint`——普通读数（稿 §2.1 的 `dim`）。
    Warning, ///< `warning`——需要看一眼的策略档（稿面只画了剪贴板「禁止」）。
};

/// @brief 一格：身份、文本与两处形态旗标。
struct StatusBarSlot {
    /// @brief 该格身份＝配置 leaf 去 `show_` 前缀（`connection` / `encoding` / …）。
    ///
    /// 只用于把 `plan_status_bar()` 的结果对回来源，本件不按它判任何形态。
    std::string_view id;
    std::u32string text;                                    ///< 文本（空串＝这格本轮没有读数，装配层不该交进来）。
    StatusBarTone tone = StatusBarTone::Faint;              ///< 前景档。
    bool leading_dot = false;   ///< 左侧画一枚 4×4 描边方点（稿面的连接态点，`success`）。
    bool leading_separator = false;  ///< 左侧画一条 1.4×16 的 `card_line` 分隔线（稿面把剪贴板那格与前文分开）。

    [[nodiscard]] auto operator==(const StatusBarSlot &other) const noexcept -> bool = default;
};

/// @brief 状态栏的呈现状态：装配层每帧重算一份，值没变即空操作。
struct StatusBarState {
    /// @brief 数据格，**已按注册次序排好、已按十枚开关与「空文本」滤过**。
    std::vector<StatusBarSlot> slots;
    /// @brief 右端锚点（判据 ⑩）的文案；空串即不画，也不为它留宽度。
    std::u32string anchor;

    [[nodiscard]] auto operator==(const StatusBarState &other) const noexcept -> bool = default;
};

/// @brief 状态栏控件（不可获焦、不可点的叶子）。
///
/// 继承扩展点：无（本件是叶子控件，不再被子类化）。
/// @note Thread: main-thread only
/// @note Rebuildable: yes, via `set_state`
class StatusBar final : public au::Widget {
  public:
    StatusBar();

    [[nodiscard]] auto type_name() const -> const char * override { return "StatusBar"; }

    /// @brief 换一份状态；值变才标脏（与横幅同一条纪律，判据文 §4 第 5 条）。
    /// @param state 新状态（与旧值相等时整条空操作）。
    auto set_state(StatusBarState state) -> void;

    [[nodiscard]] auto state() const noexcept -> const StatusBarState & { return state_; }

    /// @brief 最近一次绘制算出的排布结果（哪几格上屏、哪几格被省略）。
    ///
    /// 用例与真机走查读这一枚判「窄窗口下省略到哪一格」；被省略项按 N3 那句还要进右键菜单，
    /// 那一腿未落——`TODO(SPEC.FEAT.PREF.02): 把 plan().omitted 交给终端右键菜单的读数区段`。
    [[nodiscard]] auto plan() const noexcept -> const StatusBarPlan & { return plan_; }

    /// @brief 最近一次绘制时锚点的盒（dp，与框架交来的绘制盒同系＝窗口坐标）；未画锚点时为空盒。
    ///
    /// 与横幅的 `exit_button_box()` 同形：只作观测点，不参与命中（本件根本不参与命中）。
    /// @note 坐标系按运行时实测回填（裁决 7.179④ 的补记，证人在 `itest_status_bar`）：框架把
    /// `Container` 下传的内容盒原样交给 `on_paint`，本件又在盒原点之上摆内缩，因此这一枚读出的是
    /// **窗口 dp**而不是控件局部 dp——`Widget::paint_bounds()` 同系，两者可以逐位对表。
    [[nodiscard]] auto anchor_box() const noexcept -> const au::Rect & { return anchor_box_; }

  private:
    auto on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size override;
    auto on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void override;
    [[nodiscard]] auto wants_click() const -> bool override { return false; }
    [[nodiscard]] auto cursor_shape() const -> std::optional<au::CursorShape> override { return std::nullopt; }

    StatusBarState state_;
    StatusBarPlan plan_{};       ///< 最近一次绘制的排布结果（初值＝空排布）。
    au::Rect anchor_box_{};      ///< 最近一帧的锚点盒（dp，窗口系＝`paint_bounds()` 同系）。
};

}  // namespace borealis::ui
