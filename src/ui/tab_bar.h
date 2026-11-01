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
// 六条形态口径（判据文 §3 D6/D7/D9）：
// - 栏位宽按显示名实测钳 [96, 240] dp，溢出走横向偏移而非压缩；
// - 卡片是 38 dp 高、6 dp 圆角的一格，顶内缩 4 底贴齐、首格左内缩 8、相邻零间距，选中态另在**卡顶**画
//   2 dp accent（稿值与判据文 §1 表同值；accent 取卡顶而非判据 ⑭ 的「下划线」，裁决 7.187）；
// - 关闭钮只在 hover 与活动格显形，命中区＝栏位右端 20 dp；
// - 拖拽重排有 4 dp 阈值，被拖格跟手、原格留半透明占位、两格之间画 2 dp `accent` 落点导引线，松手按
//   那条缝折成「其余标签」基准的目标下标（判据 ⑯ 兑现，裁决 7.190：导引线与落点同出一枚算式）；
// - 就地重命名用 TextInput 子类补 Esc 与失焦提交，空串即撤销；
// - 「＋ 新建」与「⌄ 最近关闭」两枚固定右端、不参与溢出滚动（屏 1 稿右端簇，裁决 7.183）：这一句在
//   代码里兑现成「标签条的右界＝入口簇左沿」，偏移、命中、裁剪三处共用同一枚算式（裁决 7.188）。
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
    /// @param x_dp 指针的 widget 本地横坐标。
    /// @param right_edge 标签条的右界（`strip_right_edge`）——界外不属于任何一格，故溢出时末格
    ///        不再把右端入口的点击抢走（绘制腿裁在同一条界上）。
    [[nodiscard]] auto hit_test_at(float x_dp, float right_edge) const -> HitResult;

    /// @brief 计算溢出偏移量（确保选中标签可见，且整条标签层停在入口簇左沿之前）。
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

    /// @brief 标签条的右界＝入口簇的左沿（折到 `[0, width]` 内）。
    ///
    /// 三处共用同一枚：`compute_scroll_offset` 的可用宽、`hit_test_at` 的界外不判标签、`on_paint`
    /// 的标签层裁剪——溢出滚动时末格既不能压在 `＋ 新建` 的墨迹上，也不能抢走那一截的点击（裁决 7.188）。
    /// 界取簇左沿而**不自造间隙**：稿只给了「末格 → 入口簇」之外的那些数。
    [[nodiscard]] static auto strip_right_edge(const RightCluster &cluster, float width) -> float;

    /// @brief 一格在当前布局里的落位（widget 本地 dp，已扣掉溢出偏移）。
    struct Slot {
        std::uint64_t id = 0;
        float x0 = 0.0F;  ///< 左沿。
        float x1 = 0.0F;  ///< 右沿（连续坐标，等于 `x0 + 格宽`）。
    };

    /// @brief 按当前布局逐格折出落位；`skip_id` 那一格缺席。
    ///
    /// 落点下标以「其余标签」为基准（裁决 7.43④），故折算落点时要把被拖那格剔出去；Press 那一档不剔
    /// 任何格（它要的是被拖格**自己**的左沿）。起点与 `hit_test_at`、`on_paint` 同取 `kStripLeftInsetDp`
    /// 减溢出偏移，故三者不会各自持一份走位算式。
    [[nodiscard]] auto strip_slots(const std::optional<std::uint64_t> &skip_id) const -> std::vector<Slot>;

    /// @brief 一次落点折算的两个出口：目标下标与导引线的落笔横坐标。
    struct DropTarget {
        std::size_t index = 0;    ///< 目标下标（以「其余标签」为基准，取值 `[0, count-1]`）。
        float guide_x = 0.0F;     ///< 那条缝的横坐标（导引线以它为中心落笔）。
    };

    /// @brief 把「落点中心」折成 `TabStrip::move` 的目标下标与两格之间那条缝。
    ///
    /// 判据 ⑯ 那句「松手按落点中心折成目标下标」在这里一次折完：下标与导引线同出一枚算式，故导引线
    /// 指向哪条缝、松手就落到那条缝（裁决 7.190）。
    /// @param center_x 被拖格当前的中心横坐标（widget 本地 dp）。
    /// @param right_edge 标签条的右界（`strip_right_edge`）：中心与缝都先钳进界内，导引线不出界。
    /// @param dragged_id 被拖那格，它不进「其余标签」。
    /// @return 没有其余标签时（单标签窗）为空——无格可重排，也就不画导引线。
    [[nodiscard]] auto resolve_drop(float center_x, float right_edge, std::uint64_t dragged_id) const
        -> std::optional<DropTarget>;

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
        /// @brief 按下点相对被拖格左沿的偏移（「跟手」就是把它平移回当前指针位）。
        ///
        /// 须在 `hooks_.select` **之前**从 `strip_slots` 取：选中会改溢出偏移，之后再量左沿读到的是已经
        /// 挪过的那一档，被拖格会当场跳一截（裁决 7.190）。
        float grab_dx = 0.0F;
        float dragged_width = 0.0F;  ///< 被拖格宽：落点中心 = 跟手左沿 + 半格宽。
        /// @brief 当前落点（下标 + 导引线横坐标）；单标签窗没有「其余标签」，故为空。
        std::optional<DropTarget> drop;
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
