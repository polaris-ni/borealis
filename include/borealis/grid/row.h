#pragma once

// ============================================================
// 网格行（include/borealis/grid/row.h）
// ------------------------------------------------------------
// 架构 §4.2：行维护「自上次重置以来被修改元素的上界」（occupancy），重置时可跳过干净
// 区间；§4.6：行级记录左右列界，避免整行重绘。
//
// 脏区间按**闭开区间**保存（right 为最后一个脏列 + 1）；无脏时 `dirty_right_ <= dirty_left_`。
//
// 零宽码点（组合符号与格式字符）与超链接都不进 `Cell`——前者不独立占格、后者只在少数行上
// 出现，塞进主结构就是为低频属性抬高整屏常驻内存（架构 §4.1 的侧表口径）。本层按列号升序
// 各存一张行内侧表，于是它们随行移动（滚动、变宽、reset 都是整行操作），且 `Cell` 大小不变。
// ============================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "borealis/grid/cell.h"

namespace borealis::grid {

/// @brief 并入某基础格的一个零宽码点。
struct CombiningMark {
    std::size_t column = 0;     ///< 所并入的基础格列号。
    char32_t code_point = 0;    ///< 零宽码点本身（判定侧已确认它不独立占格）。
};

/// @brief 超链接的标识：由 `borealis::term` 的链接表分配，网格只存数字。
using HyperlinkId = std::uint32_t;

/// @brief 「无超链接」标识：侧表里不为其留表项，链接表也不分配 0 号。
inline constexpr HyperlinkId kNoHyperlink = 0;

/// @brief 某格所挂的超链接。
struct HyperlinkMark {
    std::size_t column = 0;          ///< 格列号。
    HyperlinkId link_id = kNoHyperlink;  ///< 链接标识。
};

/// @brief 一行定宽 cell，带脏区间、占用上界，以及零宽标记与超链接两张侧表。
class Row {
  public:
    /// @brief 单个基础格可携带的组合标记上限。
    ///
    /// 会话字节流是不可信输入：连续投喂零宽码点不得让一行无限膨胀，超上限的标记直接丢弃。
    static constexpr std::size_t kMaxCombiningMarksPerCell = 8U;

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
    ///
    /// 写入即视为「这个基础格换了内容」，其组合标记与超链接一并作废——否则旧字符的零宽码点
    /// 与旧链接会跟着新字形一起上屏。挂链接的写入方（`borealis::term`）随后自行补
    /// `set_hyperlink`。
    /// @param column 列号，须 < `columns()`。
    /// @param value 待写入的 cell。
    auto set(std::size_t column, const Cell &value) noexcept -> void {
        cells_[column] = value;
        clear_combining(column);
        clear_hyperlink(column);
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

    /// @brief 把一个零宽码点并入 @p column 的基础格，并标脏该格。
    ///
    /// 超出 `kMaxCombiningMarksPerCell` 的标记被丢弃（不入侧表、也不报错）：判定与并入次序
    /// 归调用方（`borealis::term`），本层只保证「有界」与「随行移动」。
    /// @param column 基础格列号，须 < `columns()`。
    /// @param code_point 零宽码点。
    auto attach_combining(std::size_t column, char32_t code_point) -> void {
        const auto run = combining_run(column);
        if (run.size() >= kMaxCombiningMarksPerCell) {
            return;
        }
        // 插在同列游程末尾并保持按列号升序——equal_range 的正确性全靠这个序。
        const auto at = std::ranges::upper_bound(combining_, column, {}, &CombiningMark::column);
        combining_.insert(at, CombiningMark{column, code_point});
        mark_dirty(column);
    }

    /// @brief 取并入 @p column 基础格的零宽码点（按并入先后序）。
    /// @param column 基础格列号。
    /// @return 该格的标记区间；无标记时为空。
    [[nodiscard]] auto combining(std::size_t column) const noexcept -> std::span<const CombiningMark> {
        return combining_run(column);
    }

    /// @brief 丢弃并入 @p column 基础格的全部零宽码点。
    /// @param column 基础格列号。
    auto clear_combining(std::size_t column) noexcept -> void {
        const auto [begin, end] = std::ranges::equal_range(combining_, column, {}, &CombiningMark::column);
        combining_.erase(begin, end);
    }

    /// @brief 给某格挂上超链接（或改挂另一个），并标脏该格。
    ///
    /// 一列至多一条表项：`kNoHyperlink` 即摘除，摘除本身不改内容故不额外标脏。
    /// @param column 列号，须 < `columns()`。
    /// @param link_id 链接标识，`kNoHyperlink` 表示无链接。
    auto set_hyperlink(std::size_t column, HyperlinkId link_id) -> void {
        if (link_id == kNoHyperlink) {
            clear_hyperlink(column);
            return;
        }
        const auto at = std::ranges::lower_bound(hyperlinks_, column, {}, &HyperlinkMark::column);
        if (at != hyperlinks_.end() && at->column == column) {
            at->link_id = link_id;
        } else {
            // 侧表按列号升序，插入点即 lower_bound 位置。
            hyperlinks_.insert(at, HyperlinkMark{column, link_id});
        }
        mark_dirty(column);
    }

    /// @brief 取某格所挂的超链接。
    /// @param column 列号。
    /// @return 链接标识；无链接时为 `kNoHyperlink`。
    [[nodiscard]] auto hyperlink(std::size_t column) const noexcept -> HyperlinkId {
        const auto at = std::ranges::lower_bound(hyperlinks_, column, {}, &HyperlinkMark::column);
        return at != hyperlinks_.end() && at->column == column ? at->link_id : kNoHyperlink;
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
        combining_.clear();
        hyperlinks_.clear();
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
        // 截断掉的基础格连同其零宽码点与超链接一起消失：两张侧表都只能挂在还存在的基础格上。
        // 侧表按列号升序，故首个越界项之后全部越界。
        combining_.erase(std::ranges::lower_bound(combining_, columns, {}, &CombiningMark::column),
                         combining_.end());
        hyperlinks_.erase(std::ranges::lower_bound(hyperlinks_, columns, {}, &HyperlinkMark::column),
                          hyperlinks_.end());
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
    /// @brief 并入 @p column 基础格的标记区间（侧表按列号升序，故同列必连续）。
    [[nodiscard]] auto combining_run(std::size_t column) const noexcept -> std::span<const CombiningMark> {
        const auto [begin, end] = std::ranges::equal_range(combining_, column, {}, &CombiningMark::column);
        return std::span<const CombiningMark>{begin, static_cast<std::size_t>(end - begin)};
    }

    /// @brief 并入 @p column 基础格的标记区间（可变版，供插入与擦除）。
    [[nodiscard]] auto combining_run(std::size_t column) noexcept -> std::span<CombiningMark> {
        const auto [begin, end] = std::ranges::equal_range(combining_, column, {}, &CombiningMark::column);
        return std::span<CombiningMark>{begin, static_cast<std::size_t>(end - begin)};
    }

    /// @brief 摘除某格的超链接（写入 cell 时调用：旧链接不得跟着新字形上屏）。
    auto clear_hyperlink(std::size_t column) noexcept -> void {
        if (hyperlinks_.empty()) {
            return;  // 绝大多数行没有链接：先挡掉，免得每格写入都做一次二分
        }
        const auto at = std::ranges::lower_bound(hyperlinks_, column, {}, &HyperlinkMark::column);
        if (at != hyperlinks_.end() && at->column == column) {
            hyperlinks_.erase(at);
        }
    }

    std::vector<Cell> cells_;
    std::vector<CombiningMark> combining_;
    std::vector<HyperlinkMark> hyperlinks_;
    std::size_t occupancy_ = 0;
    std::size_t dirty_left_ = 0;
    std::size_t dirty_right_ = 0;
    bool dirty_ = false;
};

}  // namespace borealis::grid
