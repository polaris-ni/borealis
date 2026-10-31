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
// - 「＋ 新建」与「⌄ 最近关闭」两枚固定右端、不参与溢出滚动（屏 1 稿右端簇，裁决 7.183）。
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

#include "borealis/conn/profile.h"  // ConnectionType（类型图标的唯一枚举，`SPEC.FEAT.WS.04`）

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
    /// @brief 连接类型（`SPEC.FEAT.WS.04` 的逐标签图标）；空＝不画也不占位。
    ///
    /// 与上面三枚不同：这是一枚**恒定事实**而不是每帧现推的投影，故住在绘制侧输入壳里而不是
    /// `ui::Tab`（那份头注释把类型点名挡在 `TabStrip` 门外，裁决 7.129① 同口径）。空值只可能来自
    /// 装配层漏登记——按裁决 7.38⑥「缺席的入口整枚不画」处理：不给占位符，也不留那一截槽宽。
    std::optional<conn::ConnectionType> kind{};
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
    /// @brief 新建标签（"＋ 新建"入口触发）。
    std::function<void()> add_new;
    /// @brief 重开最近关闭的标签（"⌄ 最近关闭"入口触发，`SPEC.FEAT.WS.10`）。
    ///
    /// 屏 1 那条稿画的是「⌄ ＋ 最近关闭」——那枚向下箭头在一幅静默稿里读作「一摞候选」，本条今天
    /// 兑现的是 `tab.undo_close` 那一条腿（弹闭包栈的**栈顶**一张），没有下拉列表。差额登记在裁决
    /// 7.183，而不是把箭头抹掉：箭头留着，是因为这一枚入口的真值源确是一摞（`ClosedTabStack`），
    /// 只是取用只到第一张。留空即不画（与 `SPEC.FEAT.WS.04` 的类型槽同一档「缺席的入口整枚不画」）。
    std::function<void()> undo_close;
    /// @brief 本栏收到一次指针 `Press`（先于命中测试，故命中空白栏也报）。
    ///
    /// `SPEC.FEAT.WS.03` 稿 D7 的「标签条」上报点：装配层用它把本窗号交给 `ActiveWindowTracker`，
    /// 于是「在 B 窗的标签条上点过一下」之后，`Ctrl+F` 这类全局快捷键知道该落哪一扇。空即不报。
    std::function<void()> note_press;
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

    /// @brief 交入两枚右端入口的显示串（装配层从 i18n 词表取，本件不在绘制路径查表）。
    ///
    /// 语言档在进程内只装填一次（`src/main.cpp` 的 `install_settings_strings` 那一处，改档重启后生效），
    /// 故这两枚串是「建树时定一次」的输入而不是每帧现读的投影——与状态栏那十枚键每帧差分同档的写法，
    /// 在这里反而会把词表查询拖进绘制路径（AGENTS §4.5 第 25 条）。
    /// @param new_tab_label 「新建」那一枚的文字；空串即只画 `＋` 字形（末位标签那种窄栏的退化档）。
    /// @param recent_label 「最近关闭」那一枚的文字；本件不用它决定画不画——那由 `hooks_.undo_close`
    ///        是否在位决定（缺席的入口整枚不画，`SPEC.FEAT.WS.04` 类型槽同口径）。
    auto set_action_labels(std::string new_tab_label, std::string recent_label) -> TabBarWidget &;

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

    /// @brief 右端两枚入口的外接盒与落笔位（widget 本地坐标 dp，纵向通栏）。
    struct RightCluster {
        au::Rect new_tab_box;       ///< 「＋ 新建」的命中盒（字形 + 间距 + 文字）。
        au::Rect undo_close_box;    ///< 「⌄ 最近关闭」的命中盒；`hooks_.undo_close` 不在位时为空盒。
        au::Rect new_label_box;     ///< 「新建」的落笔盒。
        au::Rect recent_label_box;  ///< 「最近关闭」的落笔盒。
        au::Point plus_center{};    ///< `＋` 两线的交点。
        au::Point chevron_center{}; ///< `⌄` 折线的中点（底点在中心下方一半高）。
    };
    /// @brief 由栏宽与两枚串宽折出右端簇（绘制与命中测试共用一枚算式，不各持一份）。
    [[nodiscard]] auto right_cluster(float width) const -> RightCluster;

    // Widget 虚钩子
    auto on_layout(const au::Constraints &c, const au::BuildContext &ctx) -> au::Size override;
    auto on_paint(au::Painter &p, const au::Rect &bounds, const au::BuildContext &ctx) -> void override;
    auto on_pointer_event(au::MouseEvent &e) -> void override;
    [[nodiscard]] auto wants_click() const -> bool override { return true; }

    TabBarHooks hooks_;

    /// @brief 右端两枚入口的显示串（由 `set_action_labels` 交入，本件不查词表）。
    std::string new_tab_label_;
    std::string recent_label_;

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
