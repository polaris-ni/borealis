#pragma once

// ============================================================
// pane 树：分屏布局的拓扑、比例与焦点路由（include/borealis/ui/pane_tree.h）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.02` 的「任意」四义（裁决 7.10）里与框架无关的那半：谁与谁同层、每层沿哪个轴排布、
// 各层各占多大一份、方向键该把焦点交给谁。切分与关闭是对这棵树的一次编辑，布局是把树折算成一组
// dp 矩形。**树不含几何记忆**：它只存每层的相对长度，绝对尺寸只在 `layout` 的产物里——窗口 resize、
// pane 增删与分隔条拖拽因此走同一条折算路径，不会出现「界面记了一份尺寸、模型记了另一份」的分叉。
//
// 框架 `Splitter` 是二元分割器（附录 A.1），只能表达「两个子节点 + 一个比例」，而本需求要求每层为
// **多子**容器，故树自研（架构 §8.1）。本件刻意不含 Aurora 类型：绘制侧只经 `layout` 取矩形、经
// `PaneDivider` 取把手，`SPEC.FEAT.XFER.01` 要的行列数由绘制侧把每个矩形交给 `ui::make_geometry`
// 现算，树本身不知道「格」的存在。
//
// 三条不在需求原文里、却决定形态的口径（细则见裁决 7.42）：
// - **同轴切分并入父层而不另立一层**：在一个左右排列的层里再切一刀左右，新 pane 作 target 的兄弟
//   插入；否则三栏布局会背上两层同轴容器，等分与拖拽要跨层折算，且关闭后容易留下单子容器。
// - **切分只动 target 那一份**：把 target 的相对长度一分为二交给新旧两 pane，其余 pane 的尺寸逐位
//   不变——用户切分时预期的是「别动我其他窗口」。
// - **子矩形边界吸附物理像素**：小数 dp 上的相邻边界会被抗锯齿糊成一条发灰的缝隙（与裁决 7.28④
//   同源），舍入余量一律并进最后一个子，于是每层「子矩形之和 + 分隔条」恰等于父矩形。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "borealis/ui/cell_layout.h"

namespace borealis::ui {

/// @brief 一个 pane 的身份，由装配层分配（会话侧以同一值为键）。
///
/// 刻意由调用方给定而非树内自增：切分与关闭会重排树内位置，而会话句柄不能跟着漂。
using PaneId = std::uint64_t;

/// @brief 一个容器层的身份（由树自增分配，永不复用）。
///
/// 分隔条属于「层」而非「某个 pane」：嵌套布局里一条分隔条两侧可能各是一个容器，用相邻 pane 的
/// `PaneId` 表达不出这种边界，故给容器另立一个与 `PaneId` 不同名的句柄类型。
struct ContainerId {
    std::uint64_t value = 0;  ///< 0 为无效值（树内首个容器取 1）。

    /// @brief 逐字段全等比较（陈旧句柄的排除判据）。
    [[nodiscard]] auto operator==(const ContainerId &other) const noexcept -> bool = default;
};

/// @brief 子 pane 的排布轴。
enum class PaneAxis : std::uint8_t {
    Horizontal,  ///< 子 pane 自左而右排布，分隔条竖直。
    Vertical,    ///< 子 pane 自上而下排布，分隔条水平。
};

/// @brief 方向键路由的四个方向（与框架键码无关，由绘制侧把按键折成本表）。
enum class PaneDirection : std::uint8_t {
    Left,
    Right,
    Up,
    Down,
};

/// @brief 一次布局折算的外部参数。
struct LayoutSpec {
    /// @brief 分隔条厚度（dp）。
    ///
    /// 缺省 8 dp 取自视觉稿 `UI_OVERVIEW.draft.md` §2.2 的「分隔条命中区 8 dp」——把手的视觉宽度
    /// 是 32×2 dp，但布局必须按命中区留位，否则拖拽抓不住。
    double divider_dp = 8.0;
    /// @brief 单个 pane 沿切分轴的最小长度（dp）。
    ///
    /// 终端里真正有意义的最小值是「至少还能装下几行几列」，故绘制侧应按 `最小行列数 × 格步长`
    /// 传入而非直接用本缺省值；缺省 48 dp 只是库内的保守下界。
    double min_pane_dp = 48.0;
    double scale = 1.0;  ///< device pixel ratio；边界吸附到 `1 / scale` 的整倍数。

    /// @brief 逐字段全等比较（布局结果断言用）。
    [[nodiscard]] auto operator==(const LayoutSpec &other) const noexcept -> bool = default;
};

/// @brief 一个 pane 在本帧布局里的矩形（逻辑 dp）。
struct PaneBox {
    PaneId pane = 0;
    Rect box{};
};

/// @brief 一个容器层里的一条分隔条。
///
/// 以「相邻两个 pane 的标识」表达边界的形态在本件不成立（嵌套层的两侧可能各是一个容器），故分隔
/// 条由 (容器, 槽位) 唯一确定；槽位是「第 @p slot 个子与第 @p slot+1 个子之间」。
struct PaneDivider {
    ContainerId container{};
    std::size_t slot = 0;                     ///< 该层内的槽位，取值范围 `[0, 子数)`。
    PaneAxis axis{};                          ///< 该层的排布轴（拖拽位移沿此轴）。
    Rect box{};                               ///< 把手矩形（逻辑 dp），即命中区。
    double extent_dp = 0.0;                   ///< 该层沿轴的实际长度（dp），拖拽折算的基准。
};

/// @brief 一次 `layout` 的产物：全部 pane 矩形与全部分隔条矩形。
///
/// 两个表都是**深度优先、自左而右（上层则自上而下）**的次序，故同一层的条目相邻；绘制侧直接按序
/// 铺矩形，方向键路由按序取同分时的首个候选。
struct PaneLayout {
    std::vector<PaneBox> boxes;
    std::vector<PaneDivider> dividers;
};

/// @brief 分屏布局的树：多子容器 + 叶子 pane，每层带沿轴的相对长度。
///
/// 不变量：容器至少两个子（关闭导致只剩一个时该层当场塌缩），根可以是叶子，焦点恒指向一个现存
/// pane。深度不设上限，仅受 `SPEC.FEAT.WS.02` ② 的最小 pane 尺寸约束。
class PaneTree final {
  public:
    /// @brief 以单个 pane 起步的树（未分屏的工作区就是一棵单叶树）。
    /// @param only_pane 首个 pane 的身份。
    explicit PaneTree(PaneId only_pane);
    ~PaneTree();

    PaneTree(const PaneTree &) = delete;
    auto operator=(const PaneTree &) = delete;
    PaneTree(PaneTree &&) noexcept;
    auto operator=(PaneTree &&) noexcept -> PaneTree &;

    /// @brief 当前 pane 数（叶子数）。
    [[nodiscard]] auto pane_count() const -> std::size_t;

    /// @brief 全部 pane 的身份，按深度优先、自左而右（上层则自上而下）的阅读序。
    ///
    /// 阅读序同时是标签页内 `SPEC.FEAT.WS.01` 的切换序与方向键同分时的取首序，故本件必须给出。
    [[nodiscard]] auto panes() const -> std::vector<PaneId>;

    /// @brief 该 pane 是否还在树里（会话退出后装配层要先问这句再关标签）。
    [[nodiscard]] auto has_pane(PaneId pane) const -> bool;

    /// @brief 把叶子 @p target 切一刀，新 pane @p inserted 落在其右侧/下侧。
    ///
    /// 同轴的父层直接并入兄弟而不另立一层；@p inserted 的标识已存在于树里、或 @p target 不是叶子
    /// （即不存在）时返回 false 且树不变。
    /// @param target 被切的 pane。
    /// @param axis 切分轴；与父层同轴时按兄弟插入。
    /// @param inserted 新 pane 的身份，由调用方保证是全新的。
    /// @return 是否完成切分。
    auto split(PaneId target, PaneAxis axis, PaneId inserted) -> bool;

    /// @brief 关闭一个 pane，兄弟就地合并。
    ///
    /// 同层其余子保持各自的相对长度（合并由布局时的归一化完成），父层因此只剩一个子时该层塌缩。
    /// 关掉的是焦点 pane 时，焦点交给阅读序的下一位，末位则交给上一位。
    /// @param pane 待关闭的 pane。
    /// @return 是否完成关闭；树里只剩这一个 pane 时返回 false（最后一个 pane 不归本件关，
    ///         那是关标签 / 关窗口，属 `SPEC.FEAT.WS.01` / `SPEC.FEAT.WS.03`）。
    auto close(PaneId pane) -> bool;

    /// @brief 当前的焦点 pane（恒为现存 pane）。
    [[nodiscard]] auto focused() const noexcept -> PaneId;

    /// @brief 直接指定焦点（鼠标点 pane 时用；方向键走 `route_focus`）。
    /// @param pane 目标 pane。
    /// @return 该 pane 是否存在；不存在时焦点不变。
    auto set_focus(PaneId pane) -> bool;

    /// @brief 某 pane 所在的容器层（键盘等分「当前 pane 所在层」时先问这句）。
    /// @param pane 目标 pane。
    /// @return 容器标识；该 pane 是根叶子（未分屏）时为空值。
    [[nodiscard]] auto container_of(PaneId pane) const -> std::optional<ContainerId>;

    /// @brief 把树折算成 @p area 内的一组矩形。
    ///
    /// 每层先扣掉分隔条厚度，再按相对长度归一化分配，随后把不足 @p min_pane_dp 的子抬到最小值、
    /// 差额从高于最小值的兄弟按比例扣除；当整层连「每个子都取最小值」都放不下时一律均分——
    /// 宁可不满足最小值，也不产生重叠或负尺寸，因为布局必须铺满父矩形（缺口会把窗口底色露出来）。
    /// 本件 const：钳位与吸附都不写回相对长度，故缩放窗口不会让历史拖拽「回弹」。
    /// @param area 分屏可用区（逻辑 dp，不含标签条与状态栏）。
    /// @param spec 分隔条厚度、最小长度与缩放。
    /// @return 深度优先序的 pane 矩形与分隔条矩形表。
    [[nodiscard]] auto layout(const Rect &area, const LayoutSpec &spec) const -> PaneLayout;

    /// @brief 拖拽分隔条：沿轴移动 @p delta_dp（正数把第一个子推大）。
    ///
    /// 位移按该层的实际长度折算成相对长度，且被最小 pane 尺寸钳住；钳不住的那一半直接丢弃，
    /// 于是拖到边界时是「顶住」而非越界。键盘步进（`SPEC.FEAT.WS.02` ③ 的「微调」）与本件同一条
    /// 路径，差别只在位移由调用方给固定值而非指针增量。
    /// @param divider 取自**最近一次** `layout` 的条目；其容器已不存在（切分或关闭后）时返回 false。
    /// @param delta_dp 沿轴的位移（dp，可为负）。
    /// @param spec 与产出 @p divider 的那次布局同参，否则钳位算式对不上。
    /// @return 是否完成调整（位移为 0 或被完全钳掉时也算成功，比例不变）。
    auto move_divider(const PaneDivider &divider, double delta_dp, const LayoutSpec &spec) -> bool;

    /// @brief 把某一层等分（`SPEC.FEAT.WS.02` ③ 的「等分」）。
    /// @param container 容器层标识（可为陈旧值）。
    /// @return 该层是否存在；单子容器与根叶子不存在这一说，故不会收到这样的标识。
    auto equalize(ContainerId container) -> bool;

  private:
    struct Node;  // 树节点：叶子或容器；完整定义只在实现文件里（架构 §2.3 的模块边界）。

    /// @brief 一个容器层沿折算轴的量度结果，`layout` 与 `move_divider` 共用同一条算式。
    ///
    /// 两处必须共用：拖拽钳位若与绘制折算各算一遍，拖到边界时看到的比例就和松手后的不一样。
    struct Along {
        std::int64_t divider_px = 0;      ///< 分隔条厚度（物理像素，已吸附）。
        std::int64_t along_px = 0;        ///< 该层沿轴长度（物理像素）。
        double available_px = 0.0;        ///< 扣掉分隔条后可分给各子的长度。
        double min_px = 0.0;              ///< 生效的最小长度：装不下「每子都取最小」时退成均分。
        std::vector<double> lengths_px;   ///< 与子同长的显示长度（物理像素，和为 @p available_px）。
    };

    [[nodiscard]] static auto contains(const Node &node, PaneId pane) -> bool;
    static auto collect(const Node &node, std::vector<PaneId> &out) -> void;
    /// @brief 该层里包住 @p pane 的直接子的槽位（调用方须已确认该 pane 在这一层之下）。
    [[nodiscard]] static auto child_slot(const Node &container, PaneId pane) -> std::size_t;
    /// @brief 持有该叶子的容器层的标识；该叶子就是根叶子时无层可给。
    [[nodiscard]] static auto container_id_of(const Node &node, PaneId pane)
        -> std::optional<std::uint64_t>;

    [[nodiscard]] auto find_leaf(PaneId pane) -> Node *;
    [[nodiscard]] static auto find_leaf_in(Node &node, PaneId pane) -> Node *;
    /// @brief 该叶子的直接父层；根叶子返回空值。
    [[nodiscard]] auto find_parent(PaneId pane) -> Node *;
    [[nodiscard]] static auto find_parent_in(Node &node, PaneId pane) -> Node *;
    [[nodiscard]] auto find_container(ContainerId id) -> Node *;
    [[nodiscard]] static auto find_container_in(Node &node, ContainerId id) -> Node *;

    /// @brief 在 @p node 子树里移除该叶子，并就地把「只剩一个子」的层塌缩掉。
    [[nodiscard]] static auto close_in(Node &node, PaneId pane) -> bool;
    static auto collapse_if_single(Node &container) -> void;

    /// @brief 按相对长度把 @p available 分给各子，并把低于 @p min_len 者抬到下限（差额从高于下限的兄弟按比例扣）。
    [[nodiscard]] static auto distribute(const std::vector<double> &lengths, double available,
                                         double min_len) -> std::vector<double>;
    [[nodiscard]] static auto measure_along(const Node &container, double along_dp,
                                            const LayoutSpec &spec) -> Along;
    static auto collect_layout(const Node &node, const Rect &area, const LayoutSpec &spec,
                               PaneLayout &out) -> void;

    std::unique_ptr<Node> root_;
    PaneId focused_ = 0;
    std::uint64_t next_container_ = 1;  ///< 容器标识只增不复用：陈旧分隔条因此能被认出而非指向别层。
};

/// @brief 方向键在二维 pane 树上的几何路由（`SPEC.FEAT.WS.02` 的「按几何位置跨层跳转」）。
///
/// 两级排序键：**先垂直方向间隙**（投影重叠者间隙为 0），**再主方向间隙**，同分取布局次序里的首个。
/// 取这个次序而非「欧氏距离最近」，是因为阶梯状布局里垂直错开一整个 pane 的候选，用户感知的是
/// 「不在我这个方向上」，而不是「稍微远一点」；跨层因此自然成立——候选来自布局矩形表，与树的层次无关。
/// @param from 当前 pane。
/// @param direction 方向。
/// @param layout 最近一次 `layout` 的产物（几何事实只在布局里，树不持尺寸）。
/// @return 目标 pane；该方向上没有严格居侧的候选时为空值（焦点原地不动）。
[[nodiscard]] auto route_focus(PaneId from, PaneDirection direction, const PaneLayout &layout)
    -> std::optional<PaneId>;

}  // namespace borealis::ui
