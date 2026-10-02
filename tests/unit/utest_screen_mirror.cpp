/// 测试类型: unit
/// 目标单元: include/borealis/session/screen_mirror.h
/// 测试说明: 主线程可见区副本的并入与行号换算——首次整窗取、增量只并脏行、整屏脏与尺寸变更
///           与可见窗起点变更都触发整窗重建、侧表随行搬运、距底偏移下的脏行落点与越界丢弃、
///           距底恒定推窗（裁决 7.23① 的 D6①）、偏移越界截断、脏标记一律被消费、
///           顶边位移读数随两条并入路径一起快照（选区漂移补偿的基准，裁决 7.38⑤ / 7.39①）、
///           可见窗顶的绝对行号随回看与溢出更新（选区存储行序 ↔ 屏幕行的唯一换算量，裁决 7.40）
///           （架构 §3.4 / §9.5，SPEC.FEAT.RENDER.01 的状态机前置）。

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/grid/storage.h"
#include "borealis/session/damage_queue.h"
#include "borealis/session/screen_mirror.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_screen_mirror {

namespace {

using borealis::grid::Cell;
using borealis::grid::Row;
using borealis::grid::Storage;
using borealis::session::Damage;
using borealis::session::ScreenMirror;

/// @brief 只带码点的 cell。
[[nodiscard]] auto glyph(char32_t code_point) -> Cell {
    Cell cell{};
    cell.code_point = code_point;
    return cell;
}

/// @brief 给视口某行的首列盖一个可识别字母（副本对不对只看首列，其余列保持空白）。
auto stamp(Storage &storage, std::size_t viewport_row, char letter) -> void {
    storage.visible_line(viewport_row).set(0, glyph(static_cast<char32_t>(letter)));
}

/// @brief 行首字母。
[[nodiscard]] auto head(const Row &row) -> char {
    return static_cast<char>(row.cell(0).code_point);
}

/// @brief 滚一轮真实输出：底部行盖章，整屏上移一行、底部补空白。
auto push_line(Storage &storage, char letter) -> void {
    stamp(storage, storage.visible_rows() - 1U, letter);
    storage.scroll_up(1U);
}

/// @brief 建一块 4 列 × 3 行的网格（scrollback 给足，测试里不让它饱和）。
[[nodiscard]] auto make_storage() -> Storage {
    return {4, 3, 10};
}

/// @brief 单条提交的 span 写法糖。
[[nodiscard]] auto one(std::size_t first, std::size_t last, bool full_screen = false)
    -> std::vector<Damage> {
    return {Damage{first, last, full_screen}};
}

}  // namespace

AURORA_TEST_CASE(first_apply_takes_the_whole_window) {
    auto storage = make_storage();
    stamp(storage, 0U, 'X');
    stamp(storage, 1U, 'Y');
    stamp(storage, 2U, 'Z');

    ScreenMirror mirror;
    // 首帧只提交了一行脏，副本仍须是整窗——否则未并过的行全是空白。
    mirror.apply(storage, one(1U, 1U), 0U);

    AURORA_TEST_CHECK_EQ(mirror.rows(), 3U);
    AURORA_TEST_CHECK_EQ(mirror.columns(), 4U);
    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), 'X');
    AURORA_TEST_CHECK_EQ(head(mirror.line(1U)), 'Y');
    AURORA_TEST_CHECK_EQ(head(mirror.line(2U)), 'Z');
}

AURORA_TEST_CASE(incremental_frame_only_merges_dirty_rows) {
    auto storage = make_storage();
    stamp(storage, 0U, 'X');
    stamp(storage, 1U, 'Y');
    stamp(storage, 2U, 'Z');
    ScreenMirror mirror;
    mirror.apply(storage, one(0U, 2U), 0U);

    // 第 0 行改了内容却没提交，第 2 行提交了：副本只能看到第 2 行变。
    storage.visible_line(0U).set(0U, glyph(U'Q'));
    storage.visible_line(2U).set(0U, glyph(U'R'));
    mirror.apply(storage, one(2U, 2U), 0U);

    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), 'X');
    AURORA_TEST_CHECK_EQ(head(mirror.line(2U)), 'R');
    // 若增量路径退化成了整屏重取，第 0 行就会跟着变成 Q。
    AURORA_TEST_CHECK_NE(head(mirror.line(0U)), 'Q');
}

AURORA_TEST_CASE(full_screen_commit_rebuilds_every_row) {
    auto storage = make_storage();
    stamp(storage, 0U, 'X');
    ScreenMirror mirror;
    mirror.apply(storage, one(0U, 2U), 0U);

    storage.visible_line(0U).set(0U, glyph(U'Q'));
    mirror.apply(storage, one(0U, 0U, true), 0U);

    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), 'Q');
}

AURORA_TEST_CASE(side_tables_travel_with_the_row) {
    auto storage = make_storage();
    auto &line = storage.visible_line(1U);
    line.set(0U, glyph(U'a'));
    line.attach_combining(0U, U'\x0301');
    line.set(2U, glyph(U'c'));
    line.set_hyperlink(2U, 7U);

    ScreenMirror mirror;
    mirror.apply(storage, std::span<const Damage>{}, 0U);

    // 零宽码点与超链接都在行内侧表上，副本只抄 cells_ 就丢字。
    const auto marks = mirror.line(1U).combining(0U);
    AURORA_TEST_REQUIRE_EQ(marks.size(), 1U);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(marks[0].code_point),
                         static_cast<std::uint32_t>(U'\x0301'));
    AURORA_TEST_CHECK_EQ(mirror.line(1U).hyperlink(2U), 7U);
}

AURORA_TEST_CASE(size_change_rebuilds_the_window) {
    auto storage = make_storage();
    stamp(storage, 0U, 'X');
    stamp(storage, 1U, 'Y');
    stamp(storage, 2U, 'Z');
    ScreenMirror mirror;
    mirror.apply(storage, one(0U, 2U), 0U);

    // 行数变了却没有提交（真实路径里 resize 会带整屏脏）：副本必须跟着换窗而不是留着旧行。
    storage.set_rows(2U);
    mirror.apply(storage, std::span<const Damage>{}, 0U);

    AURORA_TEST_CHECK_EQ(mirror.rows(), 2U);
    // 底部锚定：新的两行就是原视口的下两行。
    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), 'Y');
    AURORA_TEST_CHECK_EQ(head(mirror.line(1U)), 'Z');
}

AURORA_TEST_CASE(back_rows_shifts_commits_down_and_drops_the_invisible) {
    auto storage = make_storage();
    for (const char letter : {'A', 'B', 'C', 'D'}) {
        push_line(storage, letter);
    }
    // 距底 1 行：可见窗是绝对行 3..5，即 B、C、D；视口行本身是 C、D、空白。
    ScreenMirror mirror;
    mirror.apply(storage, std::span<const Damage>{}, 1U);
    AURORA_TEST_REQUIRE_EQ(mirror.rows(), 3U);
    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), 'B');
    AURORA_TEST_CHECK_EQ(head(mirror.line(1U)), 'C');
    AURORA_TEST_CHECK_EQ(head(mirror.line(2U)), 'D');

    storage.visible_line(0U).set(0U, glyph(U'c'));
    storage.visible_line(2U).set(0U, glyph(U'f'));
    // 提交仍然按视口行号来：视口行 0 落屏幕行 1，视口行 2 落屏幕行 3（可见窗之外）。
    mirror.apply(storage, one(0U, 0U), 1U);
    mirror.apply(storage, one(2U, 2U), 1U);

    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), 'B');
    AURORA_TEST_CHECK_EQ(head(mirror.line(1U)), 'c');
    AURORA_TEST_CHECK_EQ(head(mirror.line(2U)), 'D');
    // 画面上不可见不等于没消费：脏标记留着就会被 collect_damage 反复上报。
    AURORA_TEST_CHECK_FALSE(storage.visible_line(2U).dirty());
}

AURORA_TEST_CASE(distance_from_bottom_is_kept_when_new_output_arrives) {
    auto storage = make_storage();
    for (const char letter : {'A', 'B', 'C', 'D'}) {
        push_line(storage, letter);
    }
    ScreenMirror mirror;
    mirror.apply(storage, std::span<const Damage>{}, 1U);
    AURORA_TEST_REQUIRE_EQ(head(mirror.line(0U)), 'B');

    // D6①「距底恒定」：回看态里新输出一行，画面整体上移而距底行数不变。
    // 这一帧没有任何行级提交——换源的线索只有「可见窗起点跟着 total 走了」，故断言的正是这条判据。
    push_line(storage, 'E');
    mirror.apply(storage, std::span<const Damage>{}, 1U);

    AURORA_TEST_CHECK_EQ(mirror.back_rows(), 1U);
    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), 'C');
    AURORA_TEST_CHECK_EQ(head(mirror.line(1U)), 'D');
    AURORA_TEST_CHECK_EQ(head(mirror.line(2U)), 'E');
}

AURORA_TEST_CASE(back_rows_clamps_to_available_history) {
    auto storage = make_storage();
    for (const char letter : {'A', 'B', 'C', 'D'}) {
        push_line(storage, letter);
    }
    ScreenMirror mirror;
    // 距底 99 行超出可回看范围（total - rows = 4）：按可回看范围截断，越界不回绕。
    mirror.apply(storage, std::span<const Damage>{}, 99U);

    AURORA_TEST_CHECK_EQ(mirror.back_rows(), 4U);
    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), ' ');
    AURORA_TEST_CHECK_EQ(head(mirror.line(1U)), ' ');
    AURORA_TEST_CHECK_EQ(head(mirror.line(2U)), 'A');
}

AURORA_TEST_CASE(consumed_rows_lose_their_dirty_marks) {
    auto storage = make_storage();
    ScreenMirror mirror;
    storage.visible_line(0U).set(0U, glyph(U'a'));
    storage.visible_line(1U).set(0U, glyph(U'b'));
    mirror.apply(storage, one(0U, 1U), 0U);

    AURORA_TEST_CHECK_FALSE(storage.visible_line(0U).dirty());
    AURORA_TEST_CHECK_FALSE(storage.visible_line(1U).dirty());
    // 副本自己不带脏态：那是权威网格「待重读」的记号，抄过来只会指认不存在的新内容。
    AURORA_TEST_CHECK_FALSE(mirror.line(0U).dirty());

    storage.visible_line(2U).set(0U, glyph(U'c'));
    mirror.apply(storage, one(0U, 0U, true), 0U);
    // 整屏脏走整窗重建，视口行的脏一并消费掉（否则下一帧还要重读一遍）。
    AURORA_TEST_CHECK_FALSE(storage.visible_line(2U).dirty());
}

AURORA_TEST_CASE(window_top_is_the_storage_row_underneath_screen_rows) {
    auto storage = make_storage();
    for (const char letter : {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L'}) {
        push_line(storage, letter);
    }
    // 容量 13（视口 3 + 历史 10）此时已满：total 不再涨，而屏幕行 0 的绝对行号仍随内容前移。
    ScreenMirror mirror;
    mirror.apply(storage, std::span<const Damage>{}, 0U);
    AURORA_TEST_REQUIRE_EQ(mirror.rows(), 3U);
    AURORA_TEST_CHECK_EQ(mirror.window_top(), storage.total_lines() - mirror.rows());
    AURORA_TEST_CHECK_GT(mirror.window_top(), 0U);
    // 逐行对照「屏幕行 i 的内容 == 绝对行 window_top + i 的内容」：选区端点存存储行序、绘制取
    // 屏幕行，这条等式是两者之间唯一的换算关系，错一格就是高亮与复制文本差一行。
    for (std::size_t row = 0; row < mirror.rows(); ++row) {
        AURORA_TEST_CHECK_EQ(head(mirror.line(row)), head(storage.line(mirror.window_top() + row)));
    }
    const auto bottom_row_head = head(mirror.line(2U));

    // 回看两行：整窗换源，读数随重建一起带出来，且同一份内容只是换了屏幕行号。
    mirror.apply(storage, std::span<const Damage>{}, 2U);
    AURORA_TEST_CHECK_EQ(mirror.window_top(), storage.total_lines() - mirror.rows() - 2U);
    AURORA_TEST_CHECK_EQ(head(mirror.line(0U)), head(storage.line(mirror.window_top())));
    // 视口最后一行（绝对行 12）回看两行后落在屏幕行 2 之上一格：新的屏幕行 2 是绝对行 10，
    // 正是刚才新的屏幕行 0——两个读数必须指向同一份内容。
    AURORA_TEST_CHECK_NE(bottom_row_head, head(mirror.line(0U)));
    AURORA_TEST_CHECK_NE(head(mirror.line(0U)), ' ');
}

AURORA_TEST_CASE(dropped_lines_snapshot_carried_by_both_merge_paths) {
    auto storage = make_storage();
    ScreenMirror mirror;
    mirror.apply(storage, one(0U, 0U), 0U);
    AURORA_TEST_CHECK_EQ(mirror.dropped_lines(), std::int64_t{0});

    // 容量 13（视口 3 + 历史 10）：第 11 次推送起每多一行就挤掉最旧一行，顶边开始推进。
    for (std::size_t index = 0; index < 12U; ++index) {
        push_line(storage, static_cast<char>('A' + index));
    }
    // 整窗重建路径：可见窗起点随 total 走了，换源靠重建，读数也得跟着带出来。
    mirror.apply(storage, std::span<const Damage>{}, 0U);
    AURORA_TEST_CHECK_EQ(mirror.dropped_lines(), std::int64_t{2});

    // 增量并入路径：再推一行后 total 没变（已饱和），本帧不重建，读数仍须刷新。
    push_line(storage, 'Z');
    mirror.apply(storage, one(2U, 2U), 0U);
    AURORA_TEST_CHECK_EQ(mirror.dropped_lines(), std::int64_t{3});
    AURORA_TEST_CHECK_EQ(mirror.dropped_lines(), storage.dropped_lines());
}

}  // namespace borealis::test_cases::utest_screen_mirror
