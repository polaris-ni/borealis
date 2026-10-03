/// 测试类型: unit
/// 目标单元: include/borealis/ui/pane_tree.h + src/ui/pane_tree.cpp
/// 测试说明: 分屏 pane 树的四义与路由（`SPEC.FEAT.WS.02`、裁决 7.10）：单叶树铺满可用区、切分只把
///           target 那一份一分为二、同轴切分并入父层而不另立一层、换轴切分成嵌套层、关闭后兄弟就地
///           合并且「只剩一子」的层当场塌缩、最后一个 pane 不可关、焦点按阅读序交接、分隔条拖拽钳在
///           最小 pane 尺寸上、等分、陈旧句柄被拒、子矩形边界吸附物理像素且整层无缝无叠、极端空间
///           不足时退成均分，以及方向键的几何路由（跨层、严格居侧、同分取布局首序）。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "borealis/ui/cell_layout.h"
#include "borealis/ui/pane_tree.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_pane_tree {

namespace {

using borealis::ui::ContainerId;
using borealis::ui::LayoutSpec;
using borealis::ui::PaneAxis;
using borealis::ui::PaneBox;
using borealis::ui::PaneDirection;
using borealis::ui::PaneDivider;
using borealis::ui::PaneId;
using borealis::ui::PaneLayout;
using borealis::ui::PaneTree;
using borealis::ui::Rect;
using borealis::ui::route_focus;

/// @brief 一块够宽的可用区：所有算式都按它来推，改一处即全部对照失效。
const Rect kArea{.x = 0.0, .y = 0.0, .width = 800.0, .height = 600.0};

const LayoutSpec kSpec{.divider_dp = 8.0, .min_pane_dp = 48.0, .scale = 1.0};

/// @brief 精确到整物理像素的比较容差：dp 边界是整数像素除回 scale，残差只到 1e-13 量级。
constexpr double kEps = 1e-9;

auto box_of(const PaneLayout &layout, PaneId pane) -> Rect {
    for (const PaneBox &entry : layout.boxes) {
        if (entry.pane == pane) {
            return entry.box;
        }
    }
    return Rect{.x = -1.0, .y = -1.0, .width = -1.0, .height = -1.0};
}

auto index_of(const PaneLayout &layout, PaneId pane) -> std::size_t {
    for (std::size_t i = 0; i < layout.boxes.size(); ++i) {
        if (layout.boxes[i].pane == pane) {
            return i;
        }
    }
    return layout.boxes.size();
}

/// @brief 「整层无缝无叠」的通用判据：按轴排序后每两条之间恰隔一条分隔条，末条贴住父矩形的远端。
auto assert_tiles_the_along_axis(const PaneLayout &layout, bool horizontal, double total,
                                 double divider_dp) -> void {
    std::vector<Rect> boxes;
    for (const PaneBox &entry : layout.boxes) {
        boxes.push_back(entry.box);
    }
    std::sort(boxes.begin(), boxes.end(), [horizontal](const Rect &a, const Rect &b) {
        return horizontal ? a.x < b.x : a.y < b.y;
    });
    // 只有单层（无嵌套）时该判据才成立：嵌套布局的兄弟不共轴，故调用方只在单层用例里用它。
    double cursor = 0.0;
    for (const Rect &box : boxes) {
        AURORA_TEST_CHECK_NEAR(horizontal ? box.x : box.y, cursor, kEps);
        cursor = (horizontal ? box.x + box.width : box.y + box.height) + divider_dp;
    }
    AURORA_TEST_CHECK_NEAR(cursor - divider_dp, total, kEps);
}

}  // namespace

AURORA_TEST_CASE(a_single_pane_tree_fills_the_area_and_has_no_divider) {
    PaneTree tree(7);
    const PaneLayout layout = tree.layout(kArea, kSpec);

    AURORA_TEST_CHECK_EQ(tree.pane_count(), std::size_t{1});
    AURORA_TEST_CHECK_TRUE(tree.has_pane(7));
    AURORA_TEST_CHECK_EQ(tree.focused(), PaneId{7});
    AURORA_TEST_CHECK_EQ(layout.boxes.size(), std::size_t{1});
    AURORA_TEST_CHECK_NEAR(layout.boxes[0].box.x, kArea.x, kEps);
    AURORA_TEST_CHECK_NEAR(layout.boxes[0].box.y, kArea.y, kEps);
    AURORA_TEST_CHECK_NEAR(layout.boxes[0].box.width, kArea.width, kEps);
    AURORA_TEST_CHECK_NEAR(layout.boxes[0].box.height, kArea.height, kEps);
    AURORA_TEST_CHECK_TRUE(layout.dividers.empty());
    // 未分屏就没有「所在层」，等分因此无从谈起。
    AURORA_TEST_CHECK_FALSE(tree.container_of(7).has_value());
}

AURORA_TEST_CASE(the_first_split_makes_one_layer_of_two_siblings) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Vertical, 2));
    AURORA_TEST_CHECK_EQ(tree.panes(), std::vector<PaneId>{1, 2});

    const PaneLayout layout = tree.layout(kArea, kSpec);
    // 竖向层：600 - 1 条分隔条 = 592，两子各 296。
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).height, 296.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).y, 304.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).height, 296.0, kEps);
    AURORA_TEST_CHECK_EQ(layout.dividers.size(), std::size_t{1});
    AURORA_TEST_CHECK_NEAR(layout.dividers[0].box.height, 8.0, kEps);
    AURORA_TEST_CHECK_NEAR(layout.dividers[0].extent_dp, 600.0, kEps);
    // 一条把手属于「层」而不是「某个 pane」，故两 pane 拿到同一个容器标识。
    AURORA_TEST_CHECK_TRUE(tree.container_of(1) == tree.container_of(2));
    assert_tiles_the_along_axis(layout, false, 600.0, 8.0);
}

AURORA_TEST_CASE(a_split_divides_only_the_target_share) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    // 同轴再切：新 pane 作 2 的兄弟插进来，1 的那一份分毫不动。
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Horizontal, 3));

    const PaneLayout layout = tree.layout(kArea, kSpec);
    // 784 = 800 - 2×8；1 占一半（392），2 与 3 分另一半（各 196）。
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).width, 392.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).width, 196.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 3).width, 196.0, kEps);
    AURORA_TEST_CHECK_EQ(layout.dividers.size(), std::size_t{2});
    // 三栏全在同一层：同轴切分不另立一层，所以不会背出两个容器。
    AURORA_TEST_CHECK_TRUE(tree.container_of(1) == tree.container_of(3));
    assert_tiles_the_along_axis(layout, true, 800.0, 8.0);
}

AURORA_TEST_CASE(a_cross_axis_split_nests_a_layer_without_disturbing_the_siblings) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Vertical, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Horizontal, 3));  // 与父层换轴 → 嵌套一层

    const PaneLayout layout = tree.layout(kArea, kSpec);
    AURORA_TEST_CHECK_EQ(tree.pane_count(), std::size_t{3});
    AURORA_TEST_CHECK_EQ(tree.panes(), std::vector<PaneId>{1, 2, 3});
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).width, 800.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).height, 296.0, kEps);
    // 下层是横排层：在自己的 296 高带里左右各 396。
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).x, 0.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).y, 304.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).width, 396.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 3).x, 404.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 3).height, 296.0, kEps);

    AURORA_TEST_CHECK_FALSE(tree.container_of(1) == tree.container_of(2));
    AURORA_TEST_CHECK_TRUE(tree.container_of(2) == tree.container_of(3));
    AURORA_TEST_CHECK_EQ(layout.dividers.size(), std::size_t{2});
    AURORA_TEST_CHECK_TRUE(layout.dividers[0].container != layout.dividers[1].container);
    // 内层的长度基准是它自己那条轴上的长度，不是整窗高度。
    AURORA_TEST_CHECK_NEAR(layout.dividers[1].extent_dp, 800.0, kEps);
}

AURORA_TEST_CASE(closing_a_pane_merges_the_siblings_and_collapses_a_one_child_layer) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Vertical, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Horizontal, 3));
    AURORA_TEST_REQUIRE_TRUE(tree.close(1));  // 根层只剩一个子 → 该层塌缩成那个子

    const PaneLayout layout = tree.layout(kArea, kSpec);
    AURORA_TEST_CHECK_EQ(tree.pane_count(), std::size_t{2});
    AURORA_TEST_CHECK_FALSE(tree.has_pane(1));
    // 塌缩是把外层换成内层，故活下来的层标识是内层那个（外层标识随即作废）。
    AURORA_TEST_CHECK_TRUE(tree.container_of(2) == tree.container_of(3));
    // 塌缩后横排层占满整窗高度，而不是留在原来的下半带里。
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).y, 0.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).height, 600.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 3).width, 396.0, kEps);
    AURORA_TEST_CHECK_EQ(layout.dividers.size(), std::size_t{1});
    assert_tiles_the_along_axis(layout, true, 800.0, 8.0);
    // 塌缩的可见后果：把剩下的关到只剩一个，那个就不能再由本件关掉（单子根会漏掉这一判据）。
    AURORA_TEST_REQUIRE_TRUE(tree.close(2));
    AURORA_TEST_CHECK_FALSE(tree.close(3));
    AURORA_TEST_CHECK_EQ(tree.pane_count(), std::size_t{1});
}

AURORA_TEST_CASE(the_last_pane_in_the_tree_is_not_closable_here) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.close(1));
    AURORA_TEST_CHECK_EQ(tree.focused(), PaneId{2});

    // 关掉最后一个不是关 pane 而是关标签 / 关窗口（SPEC.FEAT.WS.01 / SPEC.FEAT.WS.03）。
    AURORA_TEST_CHECK_FALSE(tree.close(2));
    AURORA_TEST_CHECK_EQ(tree.pane_count(), std::size_t{1});
    AURORA_TEST_CHECK_TRUE(tree.has_pane(2));
    AURORA_TEST_CHECK_EQ(tree.focused(), PaneId{2});
}

AURORA_TEST_CASE(closing_the_focused_pane_hands_focus_to_the_next_in_reading_order) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Horizontal, 3));
    AURORA_TEST_REQUIRE_TRUE(tree.set_focus(2));

    AURORA_TEST_REQUIRE_TRUE(tree.close(2));
    AURORA_TEST_CHECK_EQ(tree.focused(), PaneId{3});  // 阅读序的下一位
    AURORA_TEST_REQUIRE_TRUE(tree.close(3));
    AURORA_TEST_CHECK_EQ(tree.focused(), PaneId{1});  // 末位则交给上一位
    AURORA_TEST_CHECK_FALSE(tree.set_focus(3));       // 不存在的 pane 不接焦点
    AURORA_TEST_CHECK_EQ(tree.focused(), PaneId{1});
}

AURORA_TEST_CASE(divider_drag_moves_only_the_two_bordering_panes) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Horizontal, 3));

    PaneLayout layout = tree.layout(kArea, kSpec);
    AURORA_TEST_REQUIRE_EQ(layout.dividers.size(), std::size_t{2});
    // 拖第一、二栏之间的把手：第三栏的长度逐位不变。
    AURORA_TEST_REQUIRE_TRUE(tree.move_divider(layout.dividers[0], 100.0, kSpec));

    layout = tree.layout(kArea, kSpec);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).width, 492.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).width, 96.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 3).width, 196.0, kEps);
    assert_tiles_the_along_axis(layout, true, 800.0, 8.0);
}

AURORA_TEST_CASE(divider_drag_clamps_at_the_minimum_pane_size) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));

    PaneLayout layout = tree.layout(kArea, kSpec);
    // 往左拖到超过剩余可让出的量：顶到最小值即停，绝不出现 0 宽或负宽的一栏。
    AURORA_TEST_REQUIRE_TRUE(tree.move_divider(layout.dividers[0], -100000.0, kSpec));
    layout = tree.layout(kArea, kSpec);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).width, 48.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).width, 744.0, kEps);

    AURORA_TEST_REQUIRE_TRUE(tree.move_divider(layout.dividers[0], 100000.0, kSpec));
    layout = tree.layout(kArea, kSpec);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).width, 744.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).width, 48.0, kEps);
}

AURORA_TEST_CASE(equalizing_a_layer_gives_every_child_the_same_share) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Horizontal, 3));

    PaneLayout layout = tree.layout(kArea, kSpec);
    AURORA_TEST_REQUIRE_TRUE(tree.move_divider(layout.dividers[0], 200.0, kSpec));

    const ContainerId layer = *tree.container_of(1);
    AURORA_TEST_REQUIRE_TRUE(tree.equalize(layer));
    layout = tree.layout(kArea, kSpec);
    // 784 ÷ 3 不是整数像素的整倍数，故各栏落在 261 / 262 上（舍入余量并进不了均分之外的地方）。
    for (PaneId pane : {PaneId{1}, PaneId{2}, PaneId{3}}) {
        AURORA_TEST_CHECK_NEAR(box_of(layout, pane).width, 784.0 / 3.0, 1.0);
    }
    assert_tiles_the_along_axis(layout, true, 800.0, 8.0);
}

AURORA_TEST_CASE(stale_divider_and_container_handles_are_rejected) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    const PaneLayout layout = tree.layout(kArea, kSpec);
    const PaneDivider divider = layout.dividers[0];
    const ContainerId layer = *tree.container_of(1);

    AURORA_TEST_REQUIRE_TRUE(tree.close(2));  // 该层塌缩，把手与层标识同时失效
    AURORA_TEST_CHECK_FALSE(tree.move_divider(divider, 50.0, kSpec));
    AURORA_TEST_CHECK_FALSE(tree.equalize(layer));
    AURORA_TEST_CHECK_FALSE(tree.equalize(ContainerId{.value = 4242}));
}

AURORA_TEST_CASE(pane_boundaries_snap_to_physical_pixels_and_still_tiled_the_layer) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Horizontal, 3));
    AURORA_TEST_REQUIRE_TRUE(tree.equalize(*tree.container_of(1)));

    // 200% 缩放下的三等分是 522⅔ 物理像素：不吸附就会出现小数 dp 边界（一条发灰的缝），而整层
    // 之和又必须恰等于可用区宽度（余量并进最后一栏），故两件事得同时成立才算数。
    const LayoutSpec spec{.divider_dp = 8.0, .min_pane_dp = 48.0, .scale = 2.0};
    const PaneLayout layout = tree.layout(kArea, spec);
    AURORA_TEST_CHECK_EQ(layout.boxes.size(), std::size_t{3});
    double consumed = 0.0;
    for (const PaneBox &entry : layout.boxes) {
        for (const double edge : {entry.box.x, entry.box.x + entry.box.width}) {
            AURORA_TEST_CHECK_NEAR(edge * 2.0, static_cast<double>(std::lround(edge * 2.0)), kEps);
        }
        AURORA_TEST_CHECK_NEAR(entry.box.x, consumed, kEps);
        consumed = entry.box.x + entry.box.width + 8.0;
    }
    AURORA_TEST_CHECK_NEAR(consumed - 8.0, kArea.width, kEps);
    for (const PaneDivider &entry : layout.dividers) {
        AURORA_TEST_CHECK_NEAR(entry.box.x * 2.0, static_cast<double>(std::lround(entry.box.x * 2.0)),
                               kEps);
        AURORA_TEST_CHECK_NEAR(entry.box.width, 8.0, kEps);
    }
}

AURORA_TEST_CASE(a_layer_too_short_for_every_minimum_divides_equally_instead) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Vertical, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Vertical, 3));

    // 100 dp 高放不下 3×48 dp：退成均分（每格 28 dp），宁可低于最小值也不重叠或负尺寸。
    const Rect area{.x = 0.0, .y = 0.0, .width = 800.0, .height = 100.0};
    const PaneLayout layout = tree.layout(area, kSpec);
    for (PaneId pane : {PaneId{1}, PaneId{2}, PaneId{3}}) {
        AURORA_TEST_CHECK_NEAR(box_of(layout, pane).height, 28.0, kEps);
    }
    assert_tiles_the_along_axis(layout, false, 100.0, 8.0);
}

AURORA_TEST_CASE(a_zero_sized_area_yields_no_negative_rectangles) {
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Vertical, 3));

    const Rect area{.x = 0.0, .y = 0.0, .width = 0.0, .height = 0.0};  // 窗口最小化是真实输入
    const PaneLayout layout = tree.layout(area, kSpec);
    AURORA_TEST_CHECK_EQ(layout.boxes.size(), std::size_t{3});
    for (const PaneBox &entry : layout.boxes) {
        AURORA_TEST_CHECK_GE(entry.box.width, 0.0);
        AURORA_TEST_CHECK_GE(entry.box.height, 0.0);
    }
}

AURORA_TEST_CASE(direction_keys_route_by_geometry_across_nested_layers) {
    // 2×2 的嵌套布局：根层横排，两个子层各自竖排。
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Vertical, 3));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Vertical, 4));

    const PaneLayout layout = tree.layout(kArea, kSpec);
    AURORA_TEST_CHECK_EQ(tree.panes(), std::vector<PaneId>{1, 3, 2, 4});
    // 四格各占一象限：内层是竖排层，故它的子矩形要跟着外层的横轴起点走。
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).x, 0.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 1).y, 0.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 3).x, 0.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 3).y, 304.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).x, 404.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 2).y, 0.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 4).x, 404.0, kEps);
    AURORA_TEST_CHECK_NEAR(box_of(layout, 4).y, 304.0, kEps);
    AURORA_TEST_CHECK_EQ(layout.dividers.size(), std::size_t{3});

    AURORA_TEST_CHECK_EQ(*route_focus(1, PaneDirection::Right, layout), PaneId{2});
    AURORA_TEST_CHECK_EQ(*route_focus(1, PaneDirection::Down, layout), PaneId{3});
    AURORA_TEST_CHECK_EQ(*route_focus(4, PaneDirection::Left, layout), PaneId{3});
    AURORA_TEST_CHECK_EQ(*route_focus(4, PaneDirection::Up, layout), PaneId{2});
    // 左上角往左 / 往上没有候选：焦点原地不动而不是绕到对角。
    AURORA_TEST_CHECK_FALSE(route_focus(1, PaneDirection::Left, layout).has_value());
    AURORA_TEST_CHECK_FALSE(route_focus(1, PaneDirection::Up, layout).has_value());
}

AURORA_TEST_CASE(direction_keys_skip_panes_that_are_not_strictly_on_that_side) {
    // T 形：上面一栏通宽，下面左右两栏。
    PaneTree tree(1);
    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Vertical, 2));
    AURORA_TEST_REQUIRE_TRUE(tree.split(2, PaneAxis::Horizontal, 3));

    const PaneLayout layout = tree.layout(kArea, kSpec);
    // 通宽栏往左 / 右都没有严格居侧的候选（下面两栏在它的下方而非侧方）。
    AURORA_TEST_CHECK_FALSE(route_focus(1, PaneDirection::Left, layout).has_value());
    AURORA_TEST_CHECK_FALSE(route_focus(1, PaneDirection::Right, layout).has_value());
    // 往下时两栏的垂直间隙同为 0、主方向间隙同为一条分隔条 → 同分取布局首序（左栏），结果确定。
    AURORA_TEST_CHECK_EQ(*route_focus(1, PaneDirection::Down, layout), PaneId{2});
    AURORA_TEST_CHECK_LT(index_of(layout, 2), index_of(layout, 3));
    AURORA_TEST_CHECK_EQ(*route_focus(3, PaneDirection::Up, layout), PaneId{1});
    AURORA_TEST_CHECK_EQ(*route_focus(2, PaneDirection::Right, layout), PaneId{3});
}

AURORA_TEST_CASE(split_rejects_reused_and_absent_identities) {
    PaneTree tree(1);
    AURORA_TEST_CHECK_FALSE(tree.split(1, PaneAxis::Horizontal, 1));  // 同一身份不能入库两次
    AURORA_TEST_CHECK_FALSE(tree.split(9, PaneAxis::Horizontal, 2));  // 被切的 pane 不存在
    AURORA_TEST_CHECK_EQ(tree.pane_count(), std::size_t{1});

    AURORA_TEST_REQUIRE_TRUE(tree.split(1, PaneAxis::Horizontal, 2));
    AURORA_TEST_CHECK_FALSE(tree.split(1, PaneAxis::Vertical, 2));  // 新 pane 已在这棵树里
    AURORA_TEST_CHECK_EQ(tree.pane_count(), std::size_t{2});
    assert_tiles_the_along_axis(tree.layout(kArea, kSpec), true, 800.0, 8.0);
}

AURORA_TEST_CASE(depth_is_bounded_only_by_the_area_and_the_divider_width) {
    // 「任意深度」不设上限：连着换轴切下去，每切一刀 pane 数 +1，且布局始终出得了同样多的矩形。
    PaneTree tree(1);
    PaneId next = 2;
    for (std::size_t depth = 0; depth < 9; ++depth) {
        AURORA_TEST_REQUIRE_TRUE(
            tree.split(next - 1, depth % 2 == 0 ? PaneAxis::Horizontal : PaneAxis::Vertical, next));
        ++next;
    }

    const PaneLayout layout = tree.layout(kArea, kSpec);
    AURORA_TEST_CHECK_EQ(tree.pane_count(), std::size_t{10});
    AURORA_TEST_CHECK_EQ(layout.boxes.size(), std::size_t{10});
    // 切到第十格时最内层的长度已经不到一条分隔条：该层的可分长度归零，其子矩形合法地取 0 尺寸，
    // 而不是出现负宽度或把兄弟推到重叠。
    for (const PaneBox &entry : layout.boxes) {
        AURORA_TEST_CHECK_GE(entry.box.width, 0.0);
        AURORA_TEST_CHECK_GE(entry.box.height, 0.0);
    }
}

}  // namespace borealis::test_cases::utest_pane_tree
