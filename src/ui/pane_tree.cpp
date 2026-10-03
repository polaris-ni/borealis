#include "borealis/ui/pane_tree.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>
#include <vector>

namespace borealis::ui {
namespace {

/// @brief dp 比较的容差：边界是整数像素除回 scale，同值的两次相加会留下 1e-16 量级的尾巴，
///        没有它「恰好贴边」的候选会被判成「不在这个方向上」。
constexpr double kEdgeTolerance = 1e-9;

[[nodiscard]] auto to_pixel(double value_dp, double scale) noexcept -> std::int64_t {
    return std::lround(value_dp * scale);
}

[[nodiscard]] auto to_dp(std::int64_t value_px, double scale) noexcept -> double {
    return scale > 0.0 ? static_cast<double>(value_px) / scale : static_cast<double>(value_px);
}

/// @brief 沿轴方向的起点：横排取 x，竖排取 y。
[[nodiscard]] auto axis_start(const Rect &box, bool horizontal) noexcept -> double {
    return horizontal ? box.x : box.y;
}

/// @brief 沿轴方向的长度。
[[nodiscard]] auto axis_size(const Rect &box, bool horizontal) noexcept -> double {
    return horizontal ? box.width : box.height;
}

}  // namespace

/// @brief 树节点：叶子持 pane 身份，容器持排布轴、各子的相对长度与子节点。
///
/// 相对长度只在本层内可比（布局时按该层实际长度归一），故嵌套层之间不存在单位换算。存「长度」而非
/// 归一化分数，是为了让分隔条拖拽的 dp 位移直接折得进去而无需反解。
struct PaneTree::Node {
    PaneId pane = 0;
    bool container = false;
    PaneAxis axis = PaneAxis::Horizontal;
    std::uint64_t id = 0;  ///< 容器专用（即 `ContainerId::value`）；叶子恒 0。
    std::vector<double> lengths;
    std::vector<std::unique_ptr<Node>> children;
};

PaneTree::PaneTree(PaneId only_pane) {
    root_ = std::make_unique<Node>();
    root_->pane = only_pane;
    focused_ = only_pane;
}

PaneTree::~PaneTree() = default;

PaneTree::PaneTree(PaneTree &&) noexcept = default;

auto PaneTree::operator=(PaneTree &&) noexcept -> PaneTree & = default;

auto PaneTree::pane_count() const -> std::size_t {
    return panes().size();
}

auto PaneTree::panes() const -> std::vector<PaneId> {
    std::vector<PaneId> out;
    collect(*root_, out);
    return out;
}

auto PaneTree::has_pane(PaneId pane) const -> bool {
    return contains(*root_, pane);
}

auto PaneTree::split(PaneId target, PaneAxis axis, PaneId inserted) -> bool {
    if (target == inserted || contains(*root_, inserted)) {
        return false;
    }
    if (find_leaf(target) == nullptr) {
        return false;
    }

    auto fresh = std::make_unique<Node>();
    fresh->pane = inserted;

    auto *parent = find_parent(target);
    if (parent == nullptr) {
        // target 是根叶子：整棵树换成一个容器层，两个子各占一半。
        auto branch = std::make_unique<Node>();
        branch->container = true;
        branch->axis = axis;
        branch->id = next_container_++;
        branch->lengths = {0.5, 0.5};
        branch->children.push_back(std::move(root_));
        branch->children.push_back(std::move(fresh));
        root_ = std::move(branch);
        return true;
    }

    const auto slot = child_slot(*parent, target);
    if (parent->axis == axis) {
        // 与父层同轴：作兄弟插入，只把 target 那一份一分为二，其余子的长度逐字不动。
        const double share = parent->lengths[slot] / 2.0;
        parent->lengths[slot] = share;
        parent->lengths.insert(parent->lengths.begin() + static_cast<std::ptrdiff_t>(slot) + 1, share);
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(slot) + 1,
                                std::move(fresh));
        return true;
    }

    auto branch = std::make_unique<Node>();
    branch->container = true;
    branch->axis = axis;
    branch->id = next_container_++;
    branch->lengths = {0.5, 0.5};
    branch->children.push_back(std::move(parent->children[slot]));
    branch->children.push_back(std::move(fresh));
    parent->children[slot] = std::move(branch);
    return true;
}

auto PaneTree::close(PaneId pane) -> bool {
    if (!root_->container) {
        return false;  // 树里只剩这一个 pane：最后一个不归本件关（关标签 / 关窗口属 WS.01 / WS.03）。
    }
    // 焦点交接要在移除**之前**的阅读序里算：移除后序会变，而用户预期是「交给原来紧跟着的那一格」。
    const auto order = panes();
    const bool was_focused = focused_ == pane;
    std::size_t order_index = order.size();
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == pane) {
            order_index = i;
            break;
        }
    }

    if (!close_in(*root_, pane)) {
        return false;  // 该 pane 不在树里，它也不在上面的阅读序里，故焦点交接无从谈起。
    }

    if (was_focused && order.size() > 1) {
        focused_ = order_index + 1 < order.size() ? order[order_index + 1] : order[order_index - 1];
    }
    return true;
}

auto PaneTree::focused() const noexcept -> PaneId {
    return focused_;
}

auto PaneTree::set_focus(PaneId pane) -> bool {
    if (!contains(*root_, pane)) {
        return false;
    }
    focused_ = pane;
    return true;
}

auto PaneTree::container_of(PaneId pane) const -> std::optional<ContainerId> {
    if (const auto id = container_id_of(*root_, pane)) {
        return ContainerId{.value = *id};
    }
    return std::nullopt;
}

auto PaneTree::layout(const Rect &area, const LayoutSpec &spec) const -> PaneLayout {
    PaneLayout out;
    collect_layout(*root_, area, spec, out);
    return out;
}

auto PaneTree::move_divider(const PaneDivider &divider, double delta_dp, const LayoutSpec &spec) -> bool {
    Node *container = find_container(divider.container);
    if (container == nullptr || container->children.size() < 2 ||
        divider.slot + 1 >= container->children.size()) {
        return false;
    }
    const auto measured = measure_along(*container, divider.extent_dp, spec);
    const double before = measured.lengths_px[divider.slot];
    const double after = measured.lengths_px[divider.slot + 1];
    const double sum = before + after;
    if (sum <= 0.0) {
        return false;  // 该层长度为 0（窗口最小化是真实输入），拖了也没有可显示的位移。
    }

    const double delta_px = static_cast<double>(to_pixel(delta_dp, spec.scale));
    // 钳在最小 pane 尺寸上：拖过头的那一截丢弃，于是顶到边界时是「停住」而非越界。
    const double moved = std::clamp(delta_px, measured.min_px - before, after - measured.min_px);

    // 位移只在这两子之间转移，二者之和不变，故同层其余子的相对份额不受影响。
    const double k = (container->lengths[divider.slot] + container->lengths[divider.slot + 1]) / sum;
    container->lengths[divider.slot] = (before + moved) * k;
    container->lengths[divider.slot + 1] = (after - moved) * k;
    return true;
}

auto PaneTree::equalize(ContainerId container) -> bool {
    Node *node = find_container(container);
    if (node == nullptr || node->children.size() < 2) {
        return false;
    }
    std::fill(node->lengths.begin(), node->lengths.end(), 1.0);
    return true;
}

auto PaneTree::contains(const Node &node, PaneId pane) -> bool {
    if (!node.container) {
        return node.pane == pane;
    }
    return std::any_of(node.children.begin(), node.children.end(),
                       [&pane](const auto &child) { return contains(*child, pane); });
}

auto PaneTree::collect(const Node &node, std::vector<PaneId> &out) -> void {
    if (!node.container) {
        out.push_back(node.pane);
        return;
    }
    for (const auto &child : node.children) {
        collect(*child, out);
    }
}

auto PaneTree::child_slot(const Node &container, PaneId pane) -> std::size_t {
    for (std::size_t i = 0; i < container.children.size(); ++i) {
        if (contains(*container.children[i], pane)) {
            // 直接子节点里能包住该 pane 的就是这一格。
            return i;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

auto PaneTree::container_id_of(const Node &node, PaneId pane) -> std::optional<std::uint64_t> {
    if (!node.container) {
        return std::nullopt;
    }
    for (const auto &child : node.children) {
        if (!child->container && child->pane == pane) {
            return node.id;
        }
        if (const auto deeper = container_id_of(*child, pane)) {
            return deeper;
        }
    }
    return std::nullopt;
}

auto PaneTree::find_leaf(PaneId pane) -> Node * {
    return find_leaf_in(*root_, pane);
}

auto PaneTree::find_leaf_in(Node &node, PaneId pane) -> Node * {
    if (!node.container) {
        return node.pane == pane ? &node : nullptr;
    }
    for (auto &child : node.children) {
        if (Node *found = find_leaf_in(*child, pane)) {
            return found;
        }
    }
    return nullptr;
}

auto PaneTree::find_parent(PaneId pane) -> Node * {
    return find_parent_in(*root_, pane);
}

auto PaneTree::find_parent_in(Node &node, PaneId pane) -> Node * {
    if (!node.container) {
        return nullptr;
    }
    for (auto &child : node.children) {
        if (!child->container && child->pane == pane) {
            return &node;
        }
    }
    for (auto &child : node.children) {
        if (Node *found = find_parent_in(*child, pane)) {
            return found;
        }
    }
    return nullptr;
}

auto PaneTree::find_container(ContainerId id) -> Node * {
    return find_container_in(*root_, id);
}

auto PaneTree::find_container_in(Node &node, ContainerId id) -> Node * {
    if (!node.container) {
        return nullptr;
    }
    if (node.id == id.value) {
        return &node;
    }
    for (auto &child : node.children) {
        if (Node *found = find_container_in(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

auto PaneTree::close_in(Node &node, PaneId pane) -> bool {
    if (!node.container) {
        return false;
    }
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        Node &child = *node.children[i];
        if (!child.container && child.pane == pane) {
            node.children.erase(node.children.begin() + static_cast<std::ptrdiff_t>(i));
            node.lengths.erase(node.lengths.begin() + static_cast<std::ptrdiff_t>(i));
            collapse_if_single(node);
            return true;
        }
        if (child.container && close_in(child, pane)) {
            return true;
        }
    }
    return false;
}

auto PaneTree::collapse_if_single(Node &container) -> void {
    if (container.children.size() != 1) {
        return;
    }
    // 只剩一个子就把该层换成它：单子容器既没有可拖的分隔条，也会让等分与方向路由跨层空转。
    std::unique_ptr<Node> sole = std::move(container.children[0]);
    const bool was_container = sole->container;
    container.container = was_container;
    container.axis = sole->axis;
    container.id = sole->id;
    container.pane = sole->pane;
    container.lengths = std::move(sole->lengths);
    container.children = std::move(sole->children);
}

auto PaneTree::distribute(const std::vector<double> &lengths, double available, double min_len)
    -> std::vector<double> {
    const auto n = lengths.size();
    std::vector<double> out(n, 0.0);
    if (n == 0) {
        return out;
    }
    double total = 0.0;
    for (const double length : lengths) {
        total += length > 0.0 ? length : 0.0;
    }
    if (total <= 0.0) {
        std::fill(out.begin(), out.end(), available / static_cast<double>(n));
        return out;
    }
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = available * (lengths[i] > 0.0 ? lengths[i] : 0.0) / total;
    }
    // 两阶段钳位：低于下限的抬到下限，差额按「高于下限者的多余量」比例从其余子处扣。
    double deficit = 0.0;
    double spare = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i] < min_len) {
            deficit += min_len - out[i];
            out[i] = min_len;
        } else {
            spare += out[i] - min_len;
        }
    }
    if (deficit <= 0.0) {
        return out;
    }
    if (deficit >= spare) {
        // 整层连「每子都取下限」都装不下：均分即最优，宁可不满足下限也不产生重叠或负尺寸。
        std::fill(out.begin(), out.end(), available / static_cast<double>(n));
        return out;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double surplus = out[i] - min_len;
        if (surplus > 0.0) {
            out[i] -= deficit * surplus / spare;
        }
    }
    return out;
}

auto PaneTree::measure_along(const Node &container, double along_dp, const LayoutSpec &spec)
    -> Along {
    const auto n = container.children.size();
    Along measured;
    measured.divider_px = std::max<std::int64_t>(0, to_pixel(spec.divider_dp, spec.scale));
    measured.along_px = std::max<std::int64_t>(0, to_pixel(along_dp, spec.scale));
    const double available = static_cast<double>(measured.along_px) -
                             static_cast<double>(n - 1) * static_cast<double>(measured.divider_px);
    measured.available_px = available > 0.0 ? available : 0.0;
    double min_px = static_cast<double>(to_pixel(spec.min_pane_dp, spec.scale));
    if (measured.available_px < static_cast<double>(n) * min_px) {
        min_px = measured.available_px / static_cast<double>(n);
    }
    measured.min_px = min_px;
    measured.lengths_px = distribute(container.lengths, measured.available_px, min_px);
    return measured;
}

auto PaneTree::collect_layout(const Node &node, const Rect &area, const LayoutSpec &spec,
                              PaneLayout &out) -> void {
    if (!node.container) {
        out.boxes.push_back(PaneBox{.pane = node.pane, .box = area});
        return;
    }
    const bool horizontal = node.axis == PaneAxis::Horizontal;
    const auto measured = measure_along(node, axis_size(area, horizontal), spec);
    const auto n = node.children.size();

    const double cross_start = axis_start(area, !horizontal);
    const double cross_size = axis_size(area, !horizontal);
    const double along_start = axis_start(area, horizontal);

    // 边界一律吸附到整数物理像素：小数 dp 上的相邻边界会被抗锯齿糊成一条发灰的缝（与裁决 7.28④ 同源）。
    // 最后一个子的右界直接取该层右界，舍入余量并进它，于是「子矩形之和 + 分隔条」恰等于父矩形。
    double cursor = 0.0;
    std::int64_t previous_end_px = 0;
    for (std::size_t i = 0; i < n; ++i) {
        cursor += measured.lengths_px[i];
        const std::int64_t end_px = i + 1 == n ? measured.along_px : std::lround(cursor);
        const std::int64_t start_px = i == 0 ? 0 : previous_end_px + measured.divider_px;
        const double start_dp = to_dp(std::min(start_px, end_px), spec.scale);
        const double size_dp = std::max(0.0, to_dp(end_px, spec.scale) - start_dp);
        // 分隔条也占沿轴长度，故它要并进游标：漏算就让下一条子矩形短了一条把手，末子再吃掉余量。
        cursor += static_cast<double>(measured.divider_px);

        Rect child_box{};
        if (horizontal) {
            child_box = Rect{.x = along_start + start_dp,
                             .y = cross_start,
                             .width = size_dp,
                             .height = cross_size};
        } else {
            // 竖排层的沿轴是 y、横轴是 x：`along_start` 只能加在 y 上，x 取该层的横轴起点。
            child_box = Rect{.x = cross_start,
                             .y = along_start + start_dp,
                             .width = cross_size,
                             .height = size_dp};
        }
        collect_layout(*node.children[i], child_box, spec, out);

        if (i + 1 < n) {
            const double divider_dp = to_dp(measured.divider_px, spec.scale);
            const double divider_start_dp = to_dp(end_px, spec.scale);
            Rect divider_box{};
            if (horizontal) {
                divider_box = Rect{.x = along_start + divider_start_dp,
                                   .y = cross_start,
                                   .width = divider_dp,
                                   .height = cross_size};
            } else {
                divider_box = Rect{.x = cross_start,
                                   .y = along_start + divider_start_dp,
                                   .width = cross_size,
                                   .height = divider_dp};
            }
            out.dividers.push_back(PaneDivider{.container = ContainerId{.value = node.id},
                                               .slot = i,
                                               .axis = node.axis,
                                               .box = divider_box,
                                               .extent_dp = axis_size(area, horizontal)});
        }
        previous_end_px = end_px;
    }
}

auto route_focus(PaneId from, PaneDirection direction, const PaneLayout &layout)
    -> std::optional<PaneId> {
    const bool horizontal = direction == PaneDirection::Left || direction == PaneDirection::Right;
    const bool forward = direction == PaneDirection::Right || direction == PaneDirection::Down;

    const PaneBox *current = nullptr;
    std::size_t current_index = 0;
    for (std::size_t i = 0; i < layout.boxes.size(); ++i) {
        if (layout.boxes[i].pane == from) {
            current = &layout.boxes[i];
            current_index = i;
            break;
        }
    }
    if (current == nullptr) {
        return std::nullopt;
    }

    const double from_start = axis_start(current->box, horizontal);
    const double from_end = from_start + axis_size(current->box, horizontal);
    const double from_cross_start = axis_start(current->box, !horizontal);
    const double from_cross_end = from_cross_start + axis_size(current->box, !horizontal);

    std::optional<PaneId> best;
    std::tuple<double, double, std::size_t> best_key{std::numeric_limits<double>::infinity(),
                                                     std::numeric_limits<double>::infinity(), 0};
    for (std::size_t i = 0; i < layout.boxes.size(); ++i) {
        if (i == current_index) {
            continue;
        }
        const Rect &box = layout.boxes[i].box;
        const double candidate_start = axis_start(box, horizontal);
        const double candidate_end = candidate_start + axis_size(box, horizontal);
        // 候选必须严格居侧：跨过当前 pane 的那一格（例如整行铺底的兄弟）不在「这个方向」上。
        if (forward ? candidate_start < from_end - kEdgeTolerance
                    : candidate_end > from_start + kEdgeTolerance) {
            continue;
        }
        const double primary_gap = forward ? std::max(0.0, candidate_start - from_end)
                                           : std::max(0.0, from_start - candidate_end);
        const double candidate_cross_start = axis_start(box, !horizontal);
        const double candidate_cross_end = candidate_cross_start + axis_size(box, !horizontal);
        const double cross_gap =
            std::max({0.0, std::max(from_cross_start, candidate_cross_start) -
                                std::min(from_cross_end, candidate_cross_end)});
        // 先比垂直间隙（投影重叠者为 0），再比主方向间隙；同分取布局次序里的首个，故结果确定。
        const std::tuple<double, double, std::size_t> key{cross_gap, primary_gap, i};
        if (key < best_key) {
            best_key = key;
            best = layout.boxes[i].pane;
        }
    }
    return best;
}

}  // namespace borealis::ui
