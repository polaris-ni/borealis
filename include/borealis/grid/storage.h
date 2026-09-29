#pragma once

// ============================================================
// 网格与 scrollback 存储（include/borealis/grid/storage.h）
// ------------------------------------------------------------
// 架构 §4.3：滚动以环形缓冲的 `zero` 偏移模加完成，不搬移行数据；容量默认 10,000 行、
// 上限 100,000（SPEC.FEAT.TERM.04），超出即覆盖最旧行。
//
// 本层只管**存储与滚动**，不解释终端语义（光标、滚动区域、插入删除归终端状态机）；
// 宽度语义也由写入方负责（见 cell.h）。行数变化（PTY 尺寸同步）尚未定策略，
// 见 `codespec/ARCHITECTURE.md` §4.5 与 `TODO(SPEC.FEAT.XFER.01)`。
// ============================================================

#include <cstddef>
#include <vector>

#include "borealis/grid/row.h"

namespace borealis::grid {

/// @brief scrollback 默认容量（行）。
inline constexpr std::size_t kDefaultScrollbackLimit = 10000;
/// @brief scrollback 容量上限（行）。
inline constexpr std::size_t kMaxScrollbackLimit = 100000;

/// @brief 视口 + scrollback 的环形行存储。
///
/// 行索引 0 为**最顶**（最旧可见行），索引 `total_lines() - 1` 为最新行；
/// 视口即最后 `visible_rows()` 行。`zero` 是行 0 在缓冲区内的物理下标。
class Storage {
  public:
    /// @brief 构造一块定尺寸网格。
    /// @param columns 列数。
    /// @param rows 视口行数。
    /// @param scrollback_limit scrollback 容量，超过 `kMaxScrollbackLimit` 时钳到上限。
    Storage(std::size_t columns, std::size_t rows, std::size_t scrollback_limit = kDefaultScrollbackLimit);

    /// @brief 视口行数。
    [[nodiscard]] auto visible_rows() const noexcept -> std::size_t { return rows_; }

    /// @brief 列数。
    [[nodiscard]] auto columns() const noexcept -> std::size_t { return columns_; }

    /// @brief scrollback 容量（不含视口）。
    [[nodiscard]] auto scrollback_limit() const noexcept -> std::size_t { return scrollback_limit_; }

    /// @brief 当前已存行数（scrollback + 视口，不超过 `rows_ + scrollback_limit_`）。
    [[nodiscard]] auto total_lines() const noexcept -> std::size_t { return lines_; }

    /// @brief 缓冲区物理容量（行），仅供诊断与测试。
    [[nodiscard]] auto capacity() const noexcept -> std::size_t { return buffer_.size(); }

    /// @brief 取第 @p index 行（0 为最顶）。
    /// @param index 行号，须 < `total_lines()`。
    /// @return 该行的可变引用。
    [[nodiscard]] auto line(std::size_t index) noexcept -> Row &;

    /// @brief 取第 @p index 行（只读）。
    /// @param index 行号，须 < `total_lines()`。
    /// @return 该行的只读引用。
    [[nodiscard]] auto line(std::size_t index) const noexcept -> const Row &;

    /// @brief 取视口内第 @p index 行（0 为视口顶）。
    /// @param index 视口内行号，须 < `visible_rows()`。
    /// @return 该行的可变引用。
    [[nodiscard]] auto visible_line(std::size_t index) noexcept -> Row &;

    /// @brief 视口内第 @p index 行（只读）。
    /// @param index 视口内行号，须 < `visible_rows()`。
    /// @return 该行的只读引用。
    [[nodiscard]] auto visible_line(std::size_t index) const noexcept -> const Row &;

    /// @brief 上滚 @p count 行：底部补空白行，顶部溢出者进 scrollback（满则覆盖最旧）。
    ///
    /// 只推进 `zero` 偏移并 reset 复用行，不搬移任何行数据——这是选环形缓冲而非
    /// 行容器的全部理由（高频滚动下搬移代价随行数线性增长）。
    /// @param count 上滚行数。
    auto scroll_up(std::size_t count) -> void;

    /// @brief 变更列数：所有行同步改宽，已有 cell 不搬移、不重排（裁决 7.5）。
    /// @param columns 新的列数。
    auto set_columns(std::size_t columns) -> void;

    /// @brief 清空全部内容回到初始状态（主备屏切换、会话重设）。
    auto clear() -> void;

  private:
    /// @brief 行号 → 缓冲区物理下标。
    [[nodiscard]] auto physical(std::size_t index) const noexcept -> std::size_t;

    std::vector<Row> buffer_;
    std::size_t columns_ = 0;
    std::size_t rows_ = 0;
    std::size_t scrollback_limit_ = 0;
    std::size_t lines_ = 0;
    std::size_t zero_ = 0;
};

}  // namespace borealis::grid
