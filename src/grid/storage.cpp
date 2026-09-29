// ============================================================
// 网格与 scrollback 存储实现（src/grid/storage.cpp）
// ------------------------------------------------------------
// 环形缓冲的行号映射：`physical(i) = (zero + i) % capacity`。上滚只是把 zero 前移并把
// 新落到视口底部的行 reset——行对象始终留在原物理位置，故滚动代价与行数无关。
// ============================================================

#include "borealis/grid/storage.h"

#include <algorithm>

namespace borealis::grid {

Storage::Storage(std::size_t columns, std::size_t rows, std::size_t scrollback_limit)
    : columns_{columns}, rows_{rows}, scrollback_limit_{std::min(scrollback_limit, kMaxScrollbackLimit)} {
    buffer_.reserve(rows_ + scrollback_limit_);
    for (std::size_t i = 0; i < rows_ + scrollback_limit_; ++i) {
        buffer_.emplace_back(columns_);
    }
    lines_ = rows_;
}

auto Storage::physical(std::size_t index) const noexcept -> std::size_t {
    return (zero_ + index) % buffer_.size();
}

auto Storage::line(std::size_t index) noexcept -> Row & { return buffer_[physical(index)]; }

auto Storage::line(std::size_t index) const noexcept -> const Row & { return buffer_[physical(index)]; }

auto Storage::visible_line(std::size_t index) noexcept -> Row & {
    return line(lines_ - rows_ + index);
}

auto Storage::visible_line(std::size_t index) const noexcept -> const Row & {
    return line(lines_ - rows_ + index);
}

auto Storage::scroll_up(std::size_t count) -> void {
    const std::size_t limit = rows_ + scrollback_limit_;
    for (std::size_t i = 0; i < count; ++i) {
        if (lines_ < limit) {
            // 缓冲区尚未填满：新增一行而不覆盖任何既有行。
            ++lines_;
            buffer_[physical(lines_ - 1)].reset();
            continue;
        }
        // 已满：最旧行出列（它所占的物理槽位随即被新的底部行复用）。
        zero_ = (zero_ + 1) % buffer_.size();
        buffer_[physical(lines_ - 1)].reset();
    }
}

auto Storage::set_columns(std::size_t columns) -> void {
    columns_ = columns;
    for (auto &row : buffer_) {
        row.resize(columns_);
    }
}

auto Storage::clear() -> void {
    for (auto &row : buffer_) {
        row.reset();
    }
    zero_ = 0;
    lines_ = rows_;
}

}  // namespace borealis::grid
