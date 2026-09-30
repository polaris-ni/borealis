#pragma once

// ============================================================
// 网格行（include/borealis/grid/row.h）
// ------------------------------------------------------------
// 架构 §4.2：行维护「自上次重置以来被修改元素的上界」（occupancy），重置时可跳过干净
// 区间；§4.6：行级记录左右列界，避免整行重绘。
//
// 脏区间按**闭开区间**保存（right 为最后一个脏列 + 1）；无脏时 `dirty_right_ <= dirty_left_`。
// ============================================================

#include <cstddef>
#include <vector>

#include "borealis/grid/cell.h"

namespace borealis::grid {

/// @brief 一行定宽 cell，带脏区间与占用上界。
class Row {
  public:
    /// @brief 构造一行空白 cell。
    /// @param columns 列数。
    explicit Row(std::size_t columns) : cells_(columns) {}

    /// @brief 列数。
    [[nodiscard]] auto columns() const noexcept -> std::size_t { return cells_.size(); }

    /// @brief 取指定列的 cell（可变）。
    /// @param column 列号，须 < `columns()`。
    /// @return 该列 cell 的可变引用。
    [[nodiscard]] auto cell(std::size_t column) noexcept -> Cell & { return cells_[column]; }

    /// @brief 取指定列的 cell（只读）。
    /// @param column 列号，须 < `columns()`。
    /// @return 该列 cell 的只读引用。
    [[nodiscard]] auto cell(std::size_t column) const noexcept -> const Cell & { return cells_[column]; }

    /// @brief 写入一个 cell 并登记脏区间与占用上界。
    /// @param column 列号，须 < `columns()`。
    /// @param value 待写入的 cell。
    auto set(std::size_t column, const Cell &value) noexcept -> void {
        cells_[column] = value;
        mark_dirty(column);
    }

    /// @brief 单独登记某列为脏（原地修改 cell 后调用）。
    /// @param column 列号，须 < `columns()`。
    auto mark_dirty(std::size_t column) noexcept -> void {
        if (column + 1U > occupancy_) {
            occupancy_ = column + 1U;
        }
        if (!dirty_ || column < dirty_left_) {
            dirty_left_ = column;
        }
        if (!dirty_ || column + 1U > dirty_right_) {
            dirty_right_ = column + 1U;
        }
        dirty_ = true;
    }

    /// @brief 整行登记为脏（行内容整体搬移后调用：脏区间无法逐列表达「全部变了」）。
    auto mark_dirty_all() noexcept -> void {
        dirty_ = true;
        dirty_left_ = 0;
        dirty_right_ = cells_.size();
    }

    /// @brief 是否有未消费的脏区。
    [[nodiscard]] auto dirty() const noexcept -> bool { return dirty_; }

    /// @brief 脏区左界（含）。
    [[nodiscard]] auto dirty_left() const noexcept -> std::size_t { return dirty_left_; }

    /// @brief 脏区右界（不含）。
    [[nodiscard]] auto dirty_right() const noexcept -> std::size_t { return dirty_right_; }

    /// @brief 消费脏区：交给重绘后清空标记，区间本身保留供调试。
    auto clear_dirty() noexcept -> void { dirty_ = false; }

    /// @brief 占用上界：自上次 reset 以来被写过的最大列 + 1，重置时可跳过其后区间。
    [[nodiscard]] auto occupancy() const noexcept -> std::size_t { return occupancy_; }

    /// @brief 整行恢复空白并清空脏标记与占用上界（滚动复用行时调用）。
    ///
    /// 作为 `std::move` 赋值的源被掏空后仍可安全调用：容器会按 @p columns 补回宽度，
    /// 否则该行残留零宽、后续写入即越界。
    /// @param columns 行宽（与所属网格一致）。
    auto reset(std::size_t columns) -> void {
        cells_.resize(columns);
        reset();
    }

    /// @brief 整行恢复空白并清空脏标记与占用上界（滚动复用行时调用）。
    auto reset() noexcept -> void {
        for (auto &cell : cells_) {
            cell.reset();
        }
        occupancy_ = 0;
        dirty_ = false;
        dirty_left_ = 0;
        dirty_right_ = 0;
    }

    /// @brief 变更列数：已有 cell **不搬移、不重排**（裁决 7.5 不 reflow），
    ///        变窄即截断、变宽则补默认空格。
    /// @param columns 新的列数。
    auto resize(std::size_t columns) -> void {
        cells_.resize(columns);
        if (occupancy_ > columns) {
            occupancy_ = columns;
        }
        if (dirty_left_ > columns) {
            dirty_left_ = columns;
        }
        if (dirty_right_ > columns) {
            dirty_right_ = columns;
        }
        if (dirty_right_ <= dirty_left_) {
            dirty_ = false;
        }
    }

  private:
    std::vector<Cell> cells_;
    std::size_t occupancy_ = 0;
    std::size_t dirty_left_ = 0;
    std::size_t dirty_right_ = 0;
    bool dirty_ = false;
};

}  // namespace borealis::grid
