/// 测试类型: integration
/// 目标单元: include/borealis/term/terminal.h（解码 → 解析 → 网格 的端到端接缝）
/// 测试说明: 回放真实形态的一帧分屏画面（tests/fixtures/vt/scene_tmux_frame.txt），走完整链路
///           「字节 → UTF-8 解码 → VT 解析 → 终端状态机 → 网格」，断言 SPEC.FEAT.TERM.01 的
///           验收线（框线字符不得显示为乱码字母）、提示符的 SGR 颜色落格、OSC 标题串不上屏而
///           消费成状态（SPEC.FEAT.TERM.07）、非法字节后链路持续、主备屏互不污染（SPEC.FEAT.TERM.03）。
///
///           本用例把宽度判定挂生产的 `UnicodeWidthPolicy`：框线由 `ESC ( 0` 映射而来，其宽度按
///           映射前的 ASCII 字母判定，故整帧仍是单宽排布（SPEC.FEAT.TERM.08、裁决 7.15 的场景分工）。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "borealis/grid/cell.h"
#include "borealis/grid/storage.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"
#include "support/fixture_text.h"
#include "support/paths.h"
#include "support/terminal_feed.h"
#include <memory>

namespace borealis::test_cases::itest_terminal_scene {

namespace {

using borealis::grid::ColorSource;
using borealis::grid::Storage;
using borealis::term::Terminal;
using borealis::term::UnicodeWidthPolicy;

/// @brief 视口某行的码点前缀（框线字符要用码点比对，转成 ASCII 就丢掉被测事实）。
[[nodiscard]] auto cells_of(Storage &grid, std::size_t row, std::size_t count) -> std::u32string {
    const auto &line = grid.visible_line(row);
    std::u32string out;
    for (std::size_t column = 0; column < count && column < line.columns(); ++column) {
        out.push_back(line.cell(column).code_point);
    }
    return out;
}

/// @brief 视口某行前 @p count 列的 ASCII 文本（右侧空白剥掉，免得断言写成数列宽）。
[[nodiscard]] auto text_of(Storage &grid, std::size_t row, std::size_t count) -> std::string {
    const auto &line = grid.visible_line(row);
    std::string out;
    for (std::size_t column = 0; column < count && column < line.columns(); ++column) {
        out.push_back(static_cast<char>(line.cell(column).code_point));
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

/// @brief 夹具首行（含转义还原）；不可读时返回空串，由用例判定。
[[nodiscard]] auto load_scene() -> std::string {
    return borealis::testing::fixtures::read_first_line(
        aurora::testing::paths::under_repo("tests/fixtures/vt/scene_tmux_frame.txt"));
}

/// @brief 整帧回放到状态机；@p stop_before 非空时只喂到该子串之前。
/// @return 解码期间的非法字节替换数（用例据此断言「混入非法字节后链路仍在跑」）。
auto replay(Terminal &terminal, std::string_view scene, std::string_view stop_before) -> std::uint64_t {
    const auto cut = stop_before.empty() ? scene.size() : scene.find(stop_before);
    const auto used = cut == std::string_view::npos ? scene.size() : cut;
    return borealis::testing::feed_bytes(terminal, scene.substr(0, used));
}

}  // namespace

AURORA_TEST_CASE(scene_frame_paints_box_lines_not_letters) {
    const std::string scene = load_scene();
    AURORA_TEST_REQUIRE_MSG(!scene.empty(), "fixture unreadable: tests/fixtures/vt/scene_tmux_frame.txt");
    auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();
    Terminal terminal{20, 5, 5, width_policy};

    // 退出备屏的那条序列之前，画面全在备屏上：先断言框线的落格码点。
    static_cast<void>(replay(terminal, scene, "\x1B[?1049l"));
    AURORA_TEST_CHECK(terminal.modes().alternate_screen);

    const std::u32string top{U'\x250C', U'\x2500', U'\x2500', U'\x2500', U'\x2500', U'\x2500',
                             U'\x2500', U'\x2500', U'\x2500', U'\x2500', U'\x2500', U'\x2510'};
    AURORA_TEST_CHECK(cells_of(terminal.active_grid(), 0, 12) == top);
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(terminal.active_grid().visible_line(0).cell(0).code_point),
                         std::uint32_t{U'\x250C'});  // 独立于整串比对的单点断言

    const std::u32string middle{U'\x2502', U' ', U'p', U'a', U'n', U'e', U' ',
                                U't', U'i', U't', U'l', U'e', U' ', U'\x2502'};
    AURORA_TEST_CHECK(cells_of(terminal.active_grid(), 1, 14) == middle);

    const std::u32string bottom{U'\x2514', U'\x2500', U'\x2500', U'\x2500', U'\x2500', U'\x2500',
                                U'\x2500', U'\x2500', U'\x2500', U'\x2500', U'\x2500', U'\x2518'};
    AURORA_TEST_CHECK(cells_of(terminal.active_grid(), 2, 12) == bottom);
    // 映射只在 `ESC ( 0`/`ESC ( B` 之间生效：`ESC ( B` 之后的字母必须原样落格。
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(terminal.active_grid().visible_line(1).cell(2).code_point),
                         std::uint32_t{U'p'});
}

AURORA_TEST_CASE(scene_prompt_colors_and_osc_title_consumed_into_state) {
    const std::string scene = load_scene();
    AURORA_TEST_REQUIRE_MSG(!scene.empty(), "fixture unreadable: tests/fixtures/vt/scene_tmux_frame.txt");
    auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();
    Terminal terminal{20, 5, 5, width_policy};
    const auto replaced = replay(terminal, scene, "\x1B[?1049l");

    auto &grid = terminal.active_grid();
    AURORA_TEST_CHECK_EQ(text_of(grid, 3, 12), std::string("user:~/proj$"));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(grid.visible_line(3).cell(12).code_point),
                         std::uint32_t{U' '});  // 提示符的尾随空格也要落格
    // `OSC 0` 的标题串不进网格：其后各行仍为空；消费的去向是状态快照（SPEC.FEAT.TERM.07）。
    AURORA_TEST_CHECK_EQ(text_of(grid, 4, 20), std::string(""));
    AURORA_TEST_CHECK(terminal.osc_state().title == U"user@host: ~/proj");
    AURORA_TEST_CHECK_EQ(terminal.osc_state().unhandled_count, std::size_t{0});

    // 提示符的 SGR 必须按来源分开存：索引色留索引，真彩色留 0xRRGGBB（SPEC.FEAT.TERM.02）。
    AURORA_TEST_CHECK_EQ(grid.visible_line(3).cell(0).foreground, std::uint32_t{0x50FA7B});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(grid.visible_line(3).cell(0).foreground_source),
                         static_cast<std::uint32_t>(ColorSource::Rgb));
    AURORA_TEST_CHECK_EQ(grid.visible_line(3).cell(5).foreground, std::uint32_t{4});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(grid.visible_line(3).cell(5).foreground_source),
                         static_cast<std::uint32_t>(ColorSource::Palette));
    AURORA_TEST_CHECK_EQ(grid.visible_line(3).cell(4).foreground, borealis::grid::kColorDefault);

    // 混入的非法字节（0xC0 0xAF）换成替换字符继续渲染：其后的 "ok" 带着绿色前景落格。
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(grid.visible_line(3).cell(13).code_point),
                         std::uint32_t{U'\x00FFFD'});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(grid.visible_line(3).cell(15).code_point), std::uint32_t{U'o'});
    AURORA_TEST_CHECK_EQ(grid.visible_line(3).cell(15).foreground, std::uint32_t{2});
    AURORA_TEST_CHECK(replaced >= std::uint64_t{2});  // 0xC0 与 0xAF 各换一个替换字符
    AURORA_TEST_CHECK(terminal.modes().bracketed_paste);  // `?2004h` 与其余模式一并登记
}

AURORA_TEST_CASE(scene_alt_screen_exit_leaves_main_untouched) {
    const std::string scene = load_scene();
    AURORA_TEST_REQUIRE_MSG(!scene.empty(), "fixture unreadable: tests/fixtures/vt/scene_tmux_frame.txt");
    auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();
    Terminal terminal{20, 5, 5, width_policy};
    replay(terminal, scene, "");  // 整帧，含进出备屏

    AURORA_TEST_CHECK_FALSE(terminal.modes().alternate_screen);
    for (std::size_t row = 0; row < terminal.main_grid().visible_rows(); ++row) {
        AURORA_TEST_CHECK_EQ(text_of(terminal.main_grid(), row, terminal.main_grid().columns()),
                             std::string(""));
    }
    // 备屏进出是整屏位移级的事件，须以整屏脏通知主线程重建副本（架构 §3.4）。
    AURORA_TEST_CHECK(terminal.full_screen_dirty());
    terminal.clear_full_screen_dirty();
    terminal.feed(U"x");
    AURORA_TEST_CHECK_FALSE(terminal.full_screen_dirty());
    AURORA_TEST_CHECK_EQ(text_of(terminal.main_grid(), 0, terminal.main_grid().columns()), std::string("x"));
    // 主屏历史为空：备屏期间产生的行不曾溢出到主屏 scrollback。
    AURORA_TEST_CHECK_EQ(terminal.main_grid().total_lines(), std::size_t{5});
}

}  // namespace borealis::test_cases::itest_terminal_scene
