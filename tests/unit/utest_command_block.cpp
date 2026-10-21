/// 测试类型: unit
/// 目标单元: include/borealis/term/command_block.h + src/term/command_block.cpp
/// 测试说明: OSC 133 命令块记录件（`SPEC.FEAT.INTEG.01`）：A/B/C/D 的正常推进、`P` 不参与块、
///           新 A 顶掉未收 D 的旧块、孤立标记忽略、容量 256 淘汰最旧且序号不复用、清空后序号继续、
///           绝对行命中区间（D 列 0 不含末行 / 列 > 0 取半行）、复制可用性与失败判定，
///           以及带符号退出码的解析与钳制（裁决 7.123 D2①/D4①/D11①）。

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "borealis/term/command_block.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_command_block {

namespace {

using borealis::term::CommandBlock;
using borealis::term::CommandBlockTracker;
using borealis::term::kMaxCommandBlocks;
using borealis::term::parse_command_exit_code;
using borealis::term::PromptMarker;

/// @brief 喂一条完整块：A/B/C/D。
auto complete_block(CommandBlockTracker &tracker, std::int64_t prompt_row,
                    std::int64_t output_row, std::int64_t end_row, std::size_t end_column,
                    std::optional<std::int32_t> exit_code = std::int32_t{0}) -> void {
    tracker.on_marker(PromptMarker::PromptStart, prompt_row, 0);
    tracker.on_marker(PromptMarker::PromptEnd, prompt_row, 6);
    tracker.on_marker(PromptMarker::CommandStart, output_row, 0);
    tracker.on_marker(PromptMarker::CommandExecuted, end_row, end_column, exit_code);
}

}  // namespace

AURORA_TEST_CASE(a_complete_block_records_all_boundaries_and_the_exit_code) {
    CommandBlockTracker tracker;
    tracker.on_marker(PromptMarker::PromptStart, 10, 0);
    tracker.on_marker(PromptMarker::PromptEnd, 10, 7);
    tracker.on_marker(PromptMarker::CommandStart, 11, 0);
    tracker.on_marker(PromptMarker::CommandExecuted, 13, 0, std::int32_t{1});

    AURORA_TEST_CHECK_EQ(tracker.size(), std::size_t{1});
    const auto &block = tracker.blocks().front();
    AURORA_TEST_CHECK_EQ(block.serial, std::uint64_t{1});
    AURORA_TEST_CHECK_EQ(block.prompt_row, std::int64_t{10});
    AURORA_TEST_CHECK_EQ(block.prompt_end_row, std::int64_t{10});
    AURORA_TEST_CHECK_EQ(block.output_start_row, std::int64_t{11});
    AURORA_TEST_CHECK_EQ(block.output_start_column, std::size_t{0});
    AURORA_TEST_CHECK_EQ(block.output_end_row, std::int64_t{13});
    AURORA_TEST_CHECK_EQ(block.output_end_column, std::size_t{0});
    AURORA_TEST_CHECK_EQ(block.exit_code, std::int32_t{1});
    AURORA_TEST_CHECK_TRUE(block.has_output_start);
    AURORA_TEST_CHECK_TRUE(block.has_exit_code);
    AURORA_TEST_CHECK_TRUE(block.completed);
    AURORA_TEST_CHECK_FALSE(block.superseded);
    AURORA_TEST_CHECK_TRUE(block.failed());
    AURORA_TEST_CHECK_TRUE(block.has_copyable_output());
    AURORA_TEST_CHECK_EQ(block.last_row(), std::int64_t{12});  // D 列 0 的那行不属于块
    AURORA_TEST_CHECK(tracker.last_completed() == &block);
}

AURORA_TEST_CASE(prompt_part_does_not_touch_the_block_or_its_output_start) {
    CommandBlockTracker tracker;
    tracker.on_marker(PromptMarker::PromptStart, 10, 0);
    tracker.on_marker(PromptMarker::CommandStart, 11, 0);
    const auto before = tracker.blocks().front();

    tracker.on_marker(PromptMarker::PromptPart, 11, 4);
    tracker.on_marker(PromptMarker::PromptPart, 12, 0);

    AURORA_TEST_CHECK_EQ(tracker.size(), std::size_t{1});
    const auto &block = tracker.blocks().front();
    AURORA_TEST_CHECK_EQ(block.output_start_row, before.output_start_row);
    AURORA_TEST_CHECK_EQ(block.output_start_column, before.output_start_column);
    AURORA_TEST_CHECK_FALSE(block.completed);
}

AURORA_TEST_CASE(a_new_prompt_supersedes_an_open_block_and_last_completed_skips_it) {
    CommandBlockTracker tracker;
    complete_block(tracker, 10, 11, 12, 0);  // 第一条已完成
    tracker.on_marker(PromptMarker::PromptStart, 20, 0);
    tracker.on_marker(PromptMarker::CommandStart, 21, 0);  // 第二条未收 D
    tracker.on_marker(PromptMarker::PromptStart, 22, 0);  // 新提示符顶掉它

    AURORA_TEST_CHECK_EQ(tracker.size(), std::size_t{3});
    AURORA_TEST_CHECK_TRUE(tracker.blocks()[1].superseded);
    AURORA_TEST_CHECK_FALSE(tracker.blocks()[1].completed);
    AURORA_TEST_CHECK_FALSE(tracker.blocks()[1].has_copyable_output());
    AURORA_TEST_CHECK(tracker.last_completed() == &tracker.blocks()[0]);
    AURORA_TEST_CHECK(tracker.block_at(21) == nullptr);  // 被顶掉的块不参与命中
}

AURORA_TEST_CASE(orphan_markers_without_a_prompt_are_ignored) {
    CommandBlockTracker tracker;
    tracker.on_marker(PromptMarker::PromptEnd, 1, 4);
    tracker.on_marker(PromptMarker::CommandStart, 2, 0);
    tracker.on_marker(PromptMarker::CommandExecuted, 3, 0, std::int32_t{1});
    AURORA_TEST_CHECK_EQ(tracker.size(), std::size_t{0});
    AURORA_TEST_CHECK(tracker.last_completed() == nullptr);
    AURORA_TEST_CHECK(tracker.block_at(2) == nullptr);
}

AURORA_TEST_CASE(a_second_output_start_or_a_late_prompt_end_is_ignored) {
    CommandBlockTracker tracker;
    tracker.on_marker(PromptMarker::PromptStart, 10, 0);
    tracker.on_marker(PromptMarker::CommandStart, 11, 3);
    tracker.on_marker(PromptMarker::PromptEnd, 12, 9);   // C 之后迟到的 B
    tracker.on_marker(PromptMarker::CommandStart, 13, 0);  // 同一块里的第二个 C

    const auto &block = tracker.blocks().front();
    AURORA_TEST_CHECK_EQ(block.prompt_end_row, std::int64_t{10});
    AURORA_TEST_CHECK_EQ(block.output_start_row, std::int64_t{11});
    AURORA_TEST_CHECK_EQ(block.output_start_column, std::size_t{3});
}

AURORA_TEST_CASE(d_without_a_number_has_no_exit_code_and_is_not_a_failure) {
    CommandBlockTracker tracker;
    tracker.on_marker(PromptMarker::PromptStart, 10, 0);
    tracker.on_marker(PromptMarker::CommandStart, 11, 0);
    tracker.on_marker(PromptMarker::CommandExecuted, 12, 0);

    const auto &block = tracker.blocks().front();
    AURORA_TEST_CHECK_TRUE(block.completed);
    AURORA_TEST_CHECK_FALSE(block.has_exit_code);
    AURORA_TEST_CHECK_EQ(block.exit_code, std::int32_t{0});
    AURORA_TEST_CHECK_FALSE(block.failed());
    AURORA_TEST_CHECK_TRUE(block.has_copyable_output());
}

AURORA_TEST_CASE(a_signed_nonzero_code_marks_the_block_failed) {
    CommandBlockTracker tracker;
    tracker.on_marker(PromptMarker::PromptStart, 10, 0);
    tracker.on_marker(PromptMarker::CommandStart, 11, 0);
    tracker.on_marker(PromptMarker::CommandExecuted, 12, 0, std::int32_t{-1});

    AURORA_TEST_CHECK_TRUE(tracker.blocks().front().has_exit_code);
    AURORA_TEST_CHECK_EQ(tracker.blocks().front().exit_code, std::int32_t{-1});
    AURORA_TEST_CHECK_TRUE(tracker.blocks().front().failed());
}

AURORA_TEST_CASE(copyable_output_follows_the_cursor_range) {
    CommandBlockTracker no_output;  // C 与 D 同行且 D 在列 0：命令没有输出
    no_output.on_marker(PromptMarker::PromptStart, 10, 0);
    no_output.on_marker(PromptMarker::CommandStart, 10, 9);
    no_output.on_marker(PromptMarker::CommandExecuted, 10, 0);
    AURORA_TEST_CHECK_FALSE(no_output.blocks().front().has_copyable_output());

    CommandBlockTracker half_line;  // D 在列 > 0：半行算输出
    half_line.on_marker(PromptMarker::PromptStart, 10, 0);
    half_line.on_marker(PromptMarker::CommandStart, 10, 0);
    half_line.on_marker(PromptMarker::CommandExecuted, 10, 5);
    AURORA_TEST_CHECK_TRUE(half_line.blocks().front().has_copyable_output());

    CommandBlockTracker empty_half_line;  // 起点不早于终点：区间为空
    empty_half_line.on_marker(PromptMarker::PromptStart, 10, 0);
    empty_half_line.on_marker(PromptMarker::CommandStart, 10, 5);
    empty_half_line.on_marker(PromptMarker::CommandExecuted, 10, 5);
    AURORA_TEST_CHECK_FALSE(empty_half_line.blocks().front().has_copyable_output());

    CommandBlockTracker no_c;
    no_c.on_marker(PromptMarker::PromptStart, 10, 0);
    no_c.on_marker(PromptMarker::CommandExecuted, 11, 0);
    AURORA_TEST_CHECK_FALSE(no_c.blocks().front().has_copyable_output());
}

AURORA_TEST_CASE(block_at_excludes_the_d_row_at_column_zero_and_prefers_the_newer_block) {
    CommandBlockTracker tracker;
    complete_block(tracker, 10, 11, 12, 0);
    AURORA_TEST_CHECK(tracker.block_at(10) == &tracker.blocks()[0]);
    AURORA_TEST_CHECK(tracker.block_at(11) == &tracker.blocks()[0]);
    AURORA_TEST_CHECK(tracker.block_at(12) == nullptr);  // D 列 0：下一条命令的提示符行

    // D 在列 > 0 时该行属于旧块，但新块的提示符也从同一行开始：命中取较新的一条。
    complete_block(tracker, 12, 13, 13, 3);
    AURORA_TEST_CHECK(tracker.block_at(12) == &tracker.blocks()[1]);
    AURORA_TEST_CHECK(tracker.block_at(13) == &tracker.blocks()[1]);
    AURORA_TEST_CHECK(tracker.block_at(14) == nullptr);
}

AURORA_TEST_CASE(open_blocks_only_cover_the_rows_they_know) {
    CommandBlockTracker tracker;
    tracker.on_marker(PromptMarker::PromptStart, 10, 0);
    tracker.on_marker(PromptMarker::PromptEnd, 10, 6);
    tracker.on_marker(PromptMarker::CommandStart, 12, 0);
    AURORA_TEST_CHECK_EQ(tracker.blocks().front().last_row(), std::int64_t{12});
    AURORA_TEST_CHECK(tracker.block_at(12) == &tracker.blocks().front());
    AURORA_TEST_CHECK(tracker.block_at(13) == nullptr);  // 末尾未知，不向未来外推
    AURORA_TEST_CHECK(tracker.last_completed() == nullptr);
}

AURORA_TEST_CASE(the_table_evicts_the_oldest_and_never_reuses_a_serial) {
    CommandBlockTracker tracker;
    for (std::int64_t index = 0; index < 300; ++index) {
        complete_block(tracker, index * 2, index * 2 + 1, index * 2 + 1, 0);
    }
    AURORA_TEST_CHECK_EQ(tracker.size(), kMaxCommandBlocks);
    AURORA_TEST_CHECK_EQ(tracker.blocks().front().serial, std::uint64_t{45});
    AURORA_TEST_CHECK_EQ(tracker.blocks().back().serial, std::uint64_t{300});
    AURORA_TEST_CHECK_EQ(tracker.blocks().back().prompt_row, std::int64_t{598});

    tracker.clear();
    AURORA_TEST_CHECK_EQ(tracker.size(), std::size_t{0});
    tracker.on_marker(PromptMarker::PromptStart, 1000, 0);
    AURORA_TEST_CHECK_EQ(tracker.blocks().front().serial, std::uint64_t{301});
}

AURORA_TEST_CASE(clearing_drops_blocks_and_keeps_the_open_state_closed) {
    CommandBlockTracker tracker;
    tracker.on_marker(PromptMarker::PromptStart, 10, 0);
    tracker.clear();
    tracker.on_marker(PromptMarker::CommandStart, 11, 0);  // 清空后的孤立 C
    AURORA_TEST_CHECK_EQ(tracker.size(), std::size_t{0});
    AURORA_TEST_CHECK(tracker.last_completed() == nullptr);
}

AURORA_TEST_CASE(exit_code_parser_accepts_an_optional_sign_and_only_the_first_field) {
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"0").value(), std::int32_t{0});
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"7").value(), std::int32_t{7});
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"+7").value(), std::int32_t{7});
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"-1").value(), std::int32_t{-1});
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"12;tail").value(), std::int32_t{12});
    AURORA_TEST_CHECK_FALSE(parse_command_exit_code(U"").has_value());
    AURORA_TEST_CHECK_FALSE(parse_command_exit_code(U"-").has_value());
    AURORA_TEST_CHECK_FALSE(parse_command_exit_code(U"+").has_value());
    AURORA_TEST_CHECK_FALSE(parse_command_exit_code(U"abc").has_value());
    AURORA_TEST_CHECK_FALSE(parse_command_exit_code(U"12x").has_value());
    AURORA_TEST_CHECK_FALSE(parse_command_exit_code(U" 1").has_value());
}

AURORA_TEST_CASE(exit_code_parser_clamps_at_the_int32_bounds) {
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"2147483647").value(),
                         std::numeric_limits<std::int32_t>::max());
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"2147483648").value(),
                         std::numeric_limits<std::int32_t>::max());
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"99999999999999999999").value(),
                         std::numeric_limits<std::int32_t>::max());
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"-2147483648").value(),
                         std::numeric_limits<std::int32_t>::min());
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"-2147483649").value(),
                         std::numeric_limits<std::int32_t>::min());
    AURORA_TEST_CHECK_EQ(parse_command_exit_code(U"-99999999999999999999").value(),
                         std::numeric_limits<std::int32_t>::min());
}

}  // namespace borealis::test_cases::utest_command_block
