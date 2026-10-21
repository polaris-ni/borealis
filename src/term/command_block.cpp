// ============================================================
// OSC 133 命令块记录实现（src/term/command_block.cpp）
// ------------------------------------------------------------

#include "borealis/term/command_block.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace borealis::term {

auto CommandBlock::last_row() const noexcept -> std::int64_t {
    std::int64_t last = prompt_end_row;
    if (has_output_start) {
        if (completed) {
            // D 在列 0：那一行是下一条命令的提示符，不是本块的输出（裁决 7.123 D4①）。
            last = output_end_column == 0 ? output_end_row - 1 : output_end_row;
        } else {
            last = std::max(output_start_row, prompt_end_row);
        }
    }
    // 零宽输出（C 与 D 同行且 D 在列 0）会把末行算到提示符行之前；块的提示符行仍在。
    return std::max(last, prompt_row);
}

auto CommandBlock::has_copyable_output() const noexcept -> bool {
    if (!completed || superseded || !has_output_start) {
        return false;
    }
    const auto end_row = output_end_column == 0 ? output_end_row - 1 : output_end_row;
    if (end_row < output_start_row) {
        return false;  // D 列 0 且与 C 同行：命令没有输出
    }
    if (end_row == output_start_row && output_end_column != 0 &&
        output_end_column <= output_start_column) {
        return false;  // 半行起点不早于终点：区间为空
    }
    return true;
}

auto CommandBlock::failed() const noexcept -> bool {
    return completed && has_exit_code && exit_code != 0;
}

auto parse_command_exit_code(std::u32string_view text) noexcept
    -> std::optional<std::int32_t> {
    const auto separator = text.find(U';');
    const auto field = text.substr(0, separator == std::u32string_view::npos ? text.size() : separator);
    if (field.empty()) {
        return std::nullopt;
    }
    std::size_t index = 0;
    bool negative = false;
    if (field.front() == U'+' || field.front() == U'-') {
        negative = field.front() == U'-';
        ++index;
    }
    if (index == field.size()) {
        return std::nullopt;  // 只有符号，没有数字
    }

    constexpr std::uint64_t kPositiveLimit = 2147483647ULL;
    constexpr std::uint64_t kNegativeLimit = 2147483648ULL;
    const auto limit = negative ? kNegativeLimit : kPositiveLimit;
    std::uint64_t magnitude = 0;
    for (; index < field.size(); ++index) {
        const char32_t digit = field[index];
        if (digit < U'0' || digit > U'9') {
            return std::nullopt;
        }
        const auto value = static_cast<std::uint64_t>(digit - U'0');
        if (magnitude > (limit - value) / 10U) {
            magnitude = limit;  // 已越界：继续验完剩余字符，避免 `999x` 被当成合法数字
        } else {
            magnitude = magnitude * 10U + value;
        }
    }
    if (negative) {
        if (magnitude >= kNegativeLimit) {
            return std::numeric_limits<std::int32_t>::min();
        }
        return -static_cast<std::int32_t>(magnitude);
    }
    return static_cast<std::int32_t>(std::min(magnitude, kPositiveLimit));
}

auto CommandBlockTracker::on_marker(PromptMarker marker, std::int64_t row, std::size_t column,
                                    std::optional<std::int32_t> exit_code) -> void {
    switch (marker) {
        case PromptMarker::PromptStart: {
            if (open_) {
                blocks_.back().superseded = true;
            }
            CommandBlock block;
            block.serial = next_serial_++;
            block.prompt_row = row;
            block.prompt_end_row = row;
            block.output_start_row = row;
            block.output_end_row = row;
            blocks_.push_back(block);
            if (blocks_.size() > kMaxCommandBlocks) {
                blocks_.erase(blocks_.begin());
            }
            open_ = true;
            break;
        }
        case PromptMarker::PromptEnd:
            if (!open_ || blocks_.back().has_output_start) {
                return;  // 孤立 B，或 C 之后迟到的 B：不改已开的输出起点
            }
            blocks_.back().prompt_end_row = row;
            break;
        case PromptMarker::CommandStart:
            if (!open_ || blocks_.back().has_output_start) {
                return;  // 孤立 C，或同一块里的第二个 C
            }
            blocks_.back().output_start_row = row;
            blocks_.back().output_start_column = column;
            blocks_.back().has_output_start = true;
            break;
        case PromptMarker::CommandExecuted:
            if (!open_) {
                return;  // 孤立 D
            }
            blocks_.back().output_end_row = row;
            blocks_.back().output_end_column = column;
            blocks_.back().exit_code = exit_code.value_or(0);
            blocks_.back().has_exit_code = exit_code.has_value();
            blocks_.back().completed = true;
            open_ = false;
            break;
        case PromptMarker::None:
        case PromptMarker::PromptPart:
            return;  // P 是右提示符分段，不参与命令块（裁决 7.123 D2①）
    }
}

auto CommandBlockTracker::last_completed() const noexcept -> const CommandBlock * {
    for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
        if (it->completed && !it->superseded) {
            return &*it;
        }
    }
    return nullptr;
}

auto CommandBlockTracker::block_at(std::int64_t row) const noexcept -> const CommandBlock * {
    for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
        if (it->superseded || row < it->prompt_row) {
            continue;
        }
        if (row <= it->last_row()) {
            return &*it;
        }
    }
    return nullptr;
}

auto CommandBlockTracker::clear() noexcept -> void {
    blocks_.clear();
    open_ = false;
    // next_serial_ 不重置：界面可能还拿着旧序号，复用会指到一条不相干的块。
}

}  // namespace borealis::term
