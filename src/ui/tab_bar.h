#pragma once

// ============================================================
// 标签栏绘制侧控件（src/ui/tab_bar.h）
// ------------------------------------------------------------
// 私有头（裁决 D1①）：类声明含 Aurora 类型，故不进 include/borealis/。
//
// 职责：把 `ui::TabStrip` 的真值源折成可点击、可拖拽、可重命名的界面。
// 不持 TabStrip 指针（否则就是模块环），而是经 `Hooks{tabs, select, close, rename, move}`
// 交装配层兑现——本件只负责「画出来 + 命中测试 + 事件派发」。
//
// 五条形态口径（判据文 §3 D6/D7/D9）：
// - 栏位宽按显示名实测钳 [96, 240] dp，溢出走横向偏移而非压缩；
// - 关闭钮只在 hover 与活动格显形，命中区＝栏位右端 20 dp；
// - 拖拽重排有 4 dp 阈值，原格留半透明占位、两格间画落点导引线；
// - 就地重命名用 TextInput 子类补 Esc 与失焦提交，空串即撤销；
// - 「＋」固定右端、不参与溢出滚动。
// ============================================================

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "aurora/core/color.h"
#include "aurora/core/font.h"
#include "aurora/render/font_engine.h"
#include "aurora/render/painter.h"
#include "aurora/widget/widget.h"

namespace au = aurora;

namespace borealis::ui {

/// @brief 一个标签在界面上的呈现信息（由装配层从 TabStrip 折来）。
struct TabVisual {
    std::uint64_t id = 0;           ///< 标签身份（与 TabStrip::TabId 同源）。
    std::u32string display_name;    ///< 按优先级折算后的显示名。
    bool is_selected = false;       ///< 是否当前选中。
    bool has_close_button = true;   ///< 是否显示关闭钮（末位标签通常不给关，但那是装配层的决定）。
    bool bell_triggered = false;    ///< BEL 触发标记（`SPEC.FEAT.WS.04`）：需显示铃铛角标。
    bool has_activity = false;      ///< 是否有新活动（输出更新）：用于活动高亮指示。
    /// @brief 会话是否已全部退出（`SPEC.FEAT.WS.04` 的断线/退出角标）。
    ///
    /// 与上面两枚**事件**角标的分工：那是「刚发生过什么」，这是「现在是什么状态」，故选中格也要画
    /// （用户在一个已退出的格子上打字时，正该看见它已退出）。
    bool exited = false;
};

/// @brief 标签栏的装配层接缝。
struct TabBarHooks {
    /// @brief 取全部标签的视觉信息。
    std::function<std::vector<TabVisual>()> tabs;
    /// @brief 选中某标签。
    std::function<void(std::uint64_t id)> select;
    /// @brief 关闭某标签。
    std::function<void(std::uint64_t id)> close;
    /// @brief 重命名某标签（空串即撤销）。
    std::function<void(std::uint64_t id, std::u32string name)> rename;
    /// @brief 移动某标签到目标位置（to_index 以"其余标签"为基准）。
    std::function<void(std::uint64_t id, std::size_t to_index)> move;
    /// @brief 新建标签（"＋"按钮触发）。
    std::function<void()> add_new;
};

/// @brief 标签栏绘制侧控件。
///
/// 继承扩展点：无（本件是叶子控件，不再被子类化）。
/// @note Thread: main-thread only
/// @note Rebuildable: yes, via hooks
class TabBarWidget final : public au::Widget {
  public:
    /// @param hooks 装配层接缝（必须全填，否则静默不响应）。
    explicit TabBarWidget(TabBarHooks hooks);

    [[nodiscard]] auto type_name() const -> const char * override { return "TabBarWidget"; }

  private:
    /// @brief 单个标签的宽度（含内边距与关闭钮）。
    [[nodiscard]] auto tab_width(const TabVisual &tab) const -> float;

    /// @brief 命中测试：返回命中的标签 ID 与是否在关闭钮上。
    struct HitResult {
        std::optional<std::uint64_t> tab_id;
        bool on_close_button = false;
    };
    [[nodiscard]] auto hit_test_at(float x_dp) const -> HitResult;

    /// @brief 计算溢出偏移量（确保选中标签可见）。
    [[nodiscard]] auto compute_scroll_offset(float available_width) const -> float;

    // Widget 虚钩子
    auto on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size override;
    auto on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void override;
    auto on_pointer_event(au::MouseEvent &e) -> void override;
    [[nodiscard]] auto wants_click() const -> bool override { return true; }

    TabBarHooks hooks_;

    // 布局期缓存
    std::vector<TabVisual> cached_tabs_;
    float bar_height_ = 42.0F;       ///< 栏高（含上下内边距）。
    float tab_min_width_ = 96.0F;    ///< 栏位最小宽。
    float tab_max_width_ = 240.0F;   ///< 栏位最大宽。
    float close_zone_ = 20.0F;       ///< 关闭钮命中区宽。
    float drag_threshold_ = 4.0F;    ///< 拖拽阈值。

    // 拖拽状态
    struct DragState {
        std::uint64_t dragged_id = 0;  ///< 被拖标签 ID。
        float press_x = 0.0F;          ///< Press 时的 x 坐标。
        float current_x = 0.0F;        ///< 当前拖拽 x 坐标。
        bool is_dragging = false;      ///< 是否已进入拖拽态。
        std::optional<std::size_t> drop_index;  ///< 落点下标（以"其余标签"为基准）。
    };
    std::optional<DragState> drag_;

    // 就地重命名状态
    struct RenameState {
        std::uint64_t target_id = 0;
        std::u32string original_name;
        // 编辑器由装配层注入，本件只持引用。
    };
    std::optional<RenameState> rename_;

    // hover 状态
    std::optional<std::uint64_t> hovered_tab_id_;

    // 溢出滚动偏移
    float scroll_offset_ = 0.0F;
};

}  // namespace borealis::ui
