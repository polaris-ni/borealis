#pragma once

// ============================================================
// 网格尺寸的去抖合并：期望行列 → 一次下发（include/borealis/ui/grid_size_debounce.h）
// ------------------------------------------------------------
// `SPEC.FEAT.XFER.01` 的「resize 须去抖（连续拖拽期间合并下发，避免 shell 高频重排）」里与
// widget、与会话无关的那半：一张按 pane 记「最近一次期望行列」的表，加一个尾沿静默窗口。
// 时长与归属层已拍板（裁决 7.47⑩）：**50 ms、落工作区层一处定时器**，因为同一帧里所有 pane
// 一起变，视口层各自去抖会让下发次数变成 N 倍。
//
// 本件刻意**不持时钟**：`now` 一律作入参，于是「静默窗口内连续三次 resize 只发最后一次」这类
// 判据可在无调度器、无帧的环境里逐条断言（AGENTS.md §4.4 第 20 条）。调度侧只剩三条职责：请求
// 时按 `next_deadline` 取消-重排一次 `Scheduler::set_timeout`、到期回调里调 `due`、把产物逐条交给
// `Session::resize`。
//
// 三条决定形态的口径：
// - **表里存两件事**：每 pane 的「最近期望值」与「最近一次实际下发值」。只存前者的话，来回拖
//   把手（A → B → A）会照发一次净变化为零的 resize，而对端 shell 为此重排一遍整屏。
// - **下发时清整张期望表**：净变化为零的条目就地丢弃，不进「已下发」也不留待下一窗——它们与
//   已下发值本就相等。
// - **本件不管 0 行 0 列**（窗口最小化）：那条过滤的真值源在视口（它才知道度量与内边距），两处
//   各判必分叉，故这里只合并拿到的值。
// ============================================================

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "borealis/ui/pane_tree.h"

namespace borealis::ui {

/// @brief 一个 pane 这一帧期望的网格行列（`session::Size` 的形状，但本件不引 `session` 头）。
struct GridSize {
    std::size_t columns = 0;
    std::size_t rows = 0;

    /// @brief 逐字段全等比较（「净变化为零」的判据）。
    [[nodiscard]] auto operator==(const GridSize &other) const noexcept -> bool = default;
};

/// @brief 缺省静默窗口（裁决 7.47⑩ 的 50 ms）：装配侧与用例都取它，而非各写一遍魔数。
inline constexpr std::chrono::milliseconds kGridResizeQuietPeriod{50};

/// @brief 时刻类型：调用方给（缺省用 `std::chrono::steady_clock`），本件不自取。
using Moment = std::chrono::steady_clock::time_point;

/// @brief 按 pane 合并期望行列、到静默窗口尾沿一次交出的那张表。
///
/// 不变量：任一 pane 在期望表里至多一条（后到的覆盖先到的）；静默窗口只在期望表非空时有定义；
/// 期望表为空时 `due` 恒产出空表。
class GridSizeDebounce final {
  public:
    /// @param quiet_period 静默窗口：最后一次请求之后安静这么久才下发。
    explicit GridSizeDebounce(std::chrono::milliseconds quiet_period = kGridResizeQuietPeriod);

    /// @brief 记下一个 pane 的期望行列，并把它的静默窗口**往后推**（尾沿去抖）。
    ///
    /// 与已下发值相等的期望值照样入表：净变化是「下发那一刻」才判得准的事（中途可能又被改回
    /// 原值），此刻就丢掉会让「改回去」那一趟白跑。
    /// @param pane 目标 pane。
    /// @param size 本帧期望行列。
    /// @param now 本次请求的时刻。
    auto request(PaneId pane, GridSize size, Moment now) -> void;

    /// @brief 取到点该下发的那些条：静默窗口未到即空表。
    ///
    /// 产出按 pane 标识升序（同一帧多个 pane 一起变的次序由此固定，断言因此不必依赖插入次序）。
    /// 一次调用即清空整张期望表并把产出的条目记为「已下发」，故同一次调用**不可重入**地只生效一次。
    /// @param now 当前时刻。
    /// @return 待下发的 (pane, 行列) 表；净变化为零的 pane 不在其中。
    auto due(Moment now) -> std::vector<std::pair<PaneId, GridSize>>;

    /// @brief 下一次该醒来的时刻；期望表为空时回空值（调度侧据此决定要不要排定时器）。
    [[nodiscard]] auto next_deadline() const noexcept -> std::optional<Moment>;

    /// @brief 有期望值尚未结算（等价于 `next_deadline()` 有值，用例读它更直白）。
    [[nodiscard]] auto has_pending() const noexcept -> bool;

    /// @brief 忘掉一个 pane 的全部记录（关闭 pane 时用：残留的已下发值会让重开的同号 pane 少发一次）。
    /// @param pane 目标 pane。
    auto forget(PaneId pane) -> void;

  private:
    std::chrono::milliseconds quiet_period_;
    std::map<PaneId, GridSize> expected_;
    std::map<PaneId, GridSize> dispatched_;
    std::optional<Moment> deadline_;
};

}  // namespace borealis::ui
