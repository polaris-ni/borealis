/// 测试类型: unit
/// 目标单元: include/borealis/term/terminal.h
/// 测试说明: 终端状态机的语义解释——打印与自动换行、C0 执行与制表位、滚动区域与区域内/整屏
///           滚动（含 scrollback 相互作用）、IL/DL/ICH/DCH/ECH、ED/EL、SGR（16/256/真彩与
///           两种子参数写法、下划线四档的编码映射见裁决 7.28）、DEC 私有模式登记、字符集指派、主备屏、宽字符占位与
///           Ambiguous 覆盖口径、DECSCUSR 光标形态档位、RIS 复位、会话初始档的播种与复位回注入档
///           （SPEC.FEAT.PREF.02 的三条构造期注入，裁决 7.76②）、尺寸变更与整屏脏标记
///           （SPEC.FEAT.TERM.01 / .02 / .03 / .05 / .08，SPEC.FEAT.XFER.01 与
///           SPEC.FEAT.RENDER.04 的状态机前置）。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "borealis/grid/cell.h"
#include "borealis/grid/storage.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_terminal {

namespace {

using borealis::grid::ColorSource;
using borealis::grid::Storage;
using borealis::grid::UnderlineStyle;
using borealis::term::AmbiguousWidth;
using borealis::term::CursorShape;
using borealis::term::SingleWidthPolicy;
using borealis::term::Terminal;
using borealis::term::TerminalDefaults;
using borealis::term::WidthPolicy;

/// @brief 桩宽度判定：只认本用例用到的两个码点。
///
/// 把 G1 的 East Asian Width 表搬进测试就变成「测副本而非被测物」，故此处只留最小判据：
/// 「中」恒双宽、「±」按 Ambiguous 口径取值（架构 §6.3 的注入接缝）。
class StubWidthPolicy final : public WidthPolicy {
  public:
    [[nodiscard]] auto width_of(char32_t code_point, AmbiguousWidth ambiguous) const noexcept
        -> std::uint8_t override {
        if (code_point == U'\x4E2D') {
            return 2U;
        }
        if (code_point == U'\x00B1') {
            return ambiguous == AmbiguousWidth::Wide ? 2U : 1U;
        }
        return 1U;
    }
};

SingleWidthPolicy narrow_only;
StubWidthPolicy stub_width;

/// @brief 建一台 10 列 × 3 行、scrollback 5 行的终端，并按调用方给的初始档播种。
///
/// `defaults` 有缺省值：既有六十个构造点因此一字不改，而本会话初始档那两条用例要的正是「注入一份
/// 与库缺省档互异的档」——取库缺省档时「装错」与「没装」在读数上无法区分。
[[nodiscard]] auto make_terminal(const WidthPolicy &policy, TerminalDefaults defaults = {}) -> Terminal {
    return {10, 3, 5, policy, defaults};
}

/// @brief 视口某行的可见文本（行尾空格剥掉，免得断言写成数列宽）。
[[nodiscard]] auto row_text(Terminal &term, std::size_t row) -> std::string {
    const auto &grid = term.active_grid();
    const auto &line = grid.visible_line(row);
    std::string out;
    for (std::size_t column = 0; column < line.columns(); ++column) {
        out.push_back(static_cast<char>(line.cell(column).code_point));
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

/// @brief 视口某行的码点串（框线字符一类非 ASCII 断言用）。
[[nodiscard]] auto row_code_points(Terminal &term, std::size_t row, std::size_t count) -> std::u32string {
    const auto &line = term.active_grid().visible_line(row);
    std::u32string out;
    for (std::size_t column = 0; column < count && column < line.columns(); ++column) {
        out.push_back(line.cell(column).code_point);
    }
    return out;
}

/// @brief 视口某行某列的 cell。
[[nodiscard]] auto cell_at(Terminal &term, std::size_t row, std::size_t column) -> const borealis::grid::Cell & {
    return term.active_grid().visible_line(row).cell(column);
}

/// @brief 把三行分别写成 "AAA"/"BBB"/"CCC"，供行级搬移类断言。
auto fill_three_rows(Terminal &term) -> void {
    term.feed(U"AAA\r\nBBB\r\nCCC");
}

}  // namespace

AURORA_TEST_CASE(print_advances_cursor_and_defers_wrap) {
    // 落满行末并不立刻换行（xterm 的「行末技巧」）：越界只压在最后一列，下一帧可覆盖它。
    auto term = make_terminal(narrow_only);
    term.feed(U"abc");
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{0});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{3});
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("abc"));

    term.feed(U"0123456789ab");
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("abc0123456"));
    AURORA_TEST_CHECK_EQ(row_text(term, 1), std::string("789ab"));
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{1});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{5});
}

AURORA_TEST_CASE(wrapped_text_continues_on_next_line) {
    auto term = make_terminal(narrow_only);
    term.feed(U"0123456789ab");
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("0123456789"));
    AURORA_TEST_CHECK_EQ(row_text(term, 1), std::string("ab"));
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{1});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{2});
}

AURORA_TEST_CASE(autowrap_off_overwrites_last_column) {
    // `CSI ?7 l` 后行末不再换行，第 11 个字符覆盖最后一列（DECAWM 的验收面）。
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[?7l0123456789X");
    AURORA_TEST_CHECK_FALSE(term.modes().auto_wrap);
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("012345678X"));
    AURORA_TEST_CHECK_EQ(row_text(term, 1), std::string(""));
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{9});
}

AURORA_TEST_CASE(control_codes_cr_bs_and_newline) {
    auto term = make_terminal(narrow_only);
    term.feed(U"abcd\rXY");            // CR 归零后覆盖前两列
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("XYcd"));
    term.feed(U"\x08Z");               // BS 退一列再落字
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("XZcd"));
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{2});
    term.feed(U"\n");                  // LF 下移一行
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{1});
}

AURORA_TEST_CASE(tab_uses_default_stops_and_tbc_clears_them) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\tX");
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("        X"));  // 默认每 8 列一个停位

    term.feed(U"\x1B[H\x1B[3g\tY");  // TBC 3：清空全部停位，HT 只能走到最后一列
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("        XY"));
}

AURORA_TEST_CASE(hts_installs_stop_at_cursor) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[1;6H\x1B[I\x1B[H\tX");  // 在第 5 列设停位，回行首后 HT 应停在那里
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("     X"));
}

AURORA_TEST_CASE(line_feed_at_bottom_pushes_line_into_scrollback) {
    auto term = make_terminal(narrow_only);
    term.feed(U"1\r\n2\r\n3\r\n4");
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("2"));
    AURORA_TEST_CHECK_EQ(row_text(term, 2), std::string("4"));
    const auto &main_grid = term.main_grid();
    AURORA_TEST_CHECK_EQ(main_grid.total_lines(), std::size_t{4});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(main_grid.line(0).cell(0).code_point),
                         std::uint32_t{U'1'});
}

AURORA_TEST_CASE(reverse_index_pulls_line_back_from_scrollback) {
    auto term = make_terminal(narrow_only);
    term.feed(U"1\r\n2\r\n3\r\n4");       // 已顶出一行历史
    term.feed(U"\x1B[H\x1BM");            // 光标到带顶后 RI：把历史收回来
    AURORA_TEST_CHECK_EQ(term.main_grid().total_lines(), std::size_t{3});
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("1"));
    AURORA_TEST_CHECK_EQ(row_text(term, 2), std::string("3"));
}

AURORA_TEST_CASE(decstbm_scrolls_within_region_only) {
    // 区域滚动不得进 scrollback：带外行是状态行/命令行，只有整屏滚动才推历史。
    auto term = Terminal{10, 4, 5, narrow_only};
    term.feed(U"top\r\naaa\r\nbbb\r\nbtm");
    term.feed(U"\x1B[2;3r");
    AURORA_TEST_CHECK_EQ(term.scroll_region().top, std::size_t{1});
    AURORA_TEST_CHECK_EQ(term.scroll_region().bottom, std::size_t{2});
    const auto lines_before = term.main_grid().total_lines();

    term.feed(U"\x1B[3;1Hxxx\r\nyyy");  // 带底写满后喂 LF：带内上滚，带外两行不动
    AURORA_TEST_CHECK_EQ(term.main_grid().total_lines(), lines_before);
    AURORA_TEST_CHECK_FALSE(term.full_screen_dirty());
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("top"));
    AURORA_TEST_CHECK_EQ(row_text(term, 1), std::string("xxx"));
    AURORA_TEST_CHECK_EQ(row_text(term, 2), std::string("yyy"));
    AURORA_TEST_CHECK_EQ(row_text(term, 3), std::string("btm"));
}

AURORA_TEST_CASE(il_dl_insert_and_delete_lines_inside_region) {
    auto term = make_terminal(narrow_only);
    fill_three_rows(term);
    term.feed(U"\x1B[1;1H\x1B[2L");       // IL 2：顶部插两行空行，底部内容被推出
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string(""));
    AURORA_TEST_CHECK_EQ(row_text(term, 1), std::string(""));
    AURORA_TEST_CHECK_EQ(row_text(term, 2), std::string("AAA"));

    term.feed(U"\x1B[1;1H\x1B[1M");       // DL 1：整体上移一行
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string(""));
    AURORA_TEST_CHECK_EQ(row_text(term, 1), std::string("AAA"));
}

AURORA_TEST_CASE(ich_dch_and_ech_shift_cells_in_line) {
    auto term = make_terminal(narrow_only);
    term.feed(U"abcd");
    term.feed(U"\x1B[1;2H\x1B[2@");       // ICH 2：光标处插两格空，右侧整体右移
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("a  bcd"));

    term.feed(U"\x1B[1;1H\x1B[3P");       // DCH 3：删掉三格，左侧内容左移
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("bcd"));

    term.feed(U"\x1B[1;1H\x1B[2X");       // ECH 2：擦除但不搬移
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("  d"));
}

AURORA_TEST_CASE(erase_display_and_erase_line_modes) {
    auto term = make_terminal(narrow_only);
    fill_three_rows(term);
    term.feed(U"\r\nD");                    // 越界换行：首行进 scrollback
    AURORA_TEST_CHECK_EQ(term.main_grid().total_lines(), std::size_t{4});

    term.feed(U"\x1B[1;2H\x1B[0K");        // EL 0：光标到行尾
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("B"));

    term.feed(U"\x1B[2;2H\x1B[1K");        // EL 1：行首到光标（含光标格）
    AURORA_TEST_CHECK_EQ(row_text(term, 1), std::string("  C"));

    term.feed(U"\x1B[2J");                  // ED 2：整屏清空，历史仍在
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string(""));
    AURORA_TEST_CHECK_EQ(row_text(term, 2), std::string(""));
    AURORA_TEST_CHECK_EQ(term.main_grid().total_lines(), std::size_t{4});

    term.feed(U"\x1B[3J");                  // ED 3：连 scrollback 一起清（xterm 口径）
    AURORA_TEST_CHECK_EQ(term.main_grid().total_lines(), std::size_t{3});
}

AURORA_TEST_CASE(sgr_sets_flags_and_basic_colors) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[1;34;41mX");
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).flags),
                         static_cast<std::uint32_t>(borealis::grid::kFlagBold));
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 0).foreground, std::uint32_t{4});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).foreground_source),
                         static_cast<std::uint32_t>(ColorSource::Palette));
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 0).background, std::uint32_t{1});

    term.feed(U"\x1B[0mY");                // SGR 0 复位整支笔
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 1).flags), std::uint32_t{0});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 1).foreground_source),
                         static_cast<std::uint32_t>(ColorSource::Default));

    term.feed(U"\x1B[90mZ");               // 高亮前景 90–97 落到调色板 8–15
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 2).foreground, std::uint32_t{8});
}

AURORA_TEST_CASE(sgr_extended_colors_both_notations) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[38;5;123mA");
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 0).foreground, std::uint32_t{123});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).foreground_source),
                         static_cast<std::uint32_t>(ColorSource::Palette));

    term.feed(U"\x1B[38;2;10;20;30mB");    // 平参数式真彩色
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 1).foreground, std::uint32_t{0x0A141E});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 1).foreground_source),
                         static_cast<std::uint32_t>(ColorSource::Rgb));

    term.feed(U"\x1B[38:2::12:34:56mC");   // 子参数式（含色彩空间缺省段）
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 2).foreground, std::uint32_t{0x0C2238});

    term.feed(U"\x1B[48:5:99mD");          // 子参数式的背景色
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 3).background, std::uint32_t{99});
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 3).background_source),
                         static_cast<std::uint32_t>(ColorSource::Palette));
}

AURORA_TEST_CASE(sgr_underline_styles_follow_the_measured_mapping) {
    auto term = make_terminal(narrow_only);
    // 编码映射照裁决 7.28①②：`4:0` 是关、`4:2` 是双线、`4:3` 是波浪，`21` 取 ECMA-48 的双线；
    // dotted 与 dashed 需求未覆盖，按单线呈现且不进枚举（7.28③）。
    const std::pair<std::u32string_view, UnderlineStyle> table[] = {
        {U"\x1B[4m", UnderlineStyle::Single},      {U"\x1B[4:0m", UnderlineStyle::None},
        {U"\x1B[4:1m", UnderlineStyle::Single},    {U"\x1B[4:2m", UnderlineStyle::Double},
        {U"\x1B[4:3m", UnderlineStyle::Curly},     {U"\x1B[4:4m", UnderlineStyle::Single},
        {U"\x1B[4:5m", UnderlineStyle::Single},    {U"\x1B[21m", UnderlineStyle::Double},
        {U"\x1B[24m", UnderlineStyle::None},       {U"\x1B[m", UnderlineStyle::None},
    };
    for (std::size_t column = 0; column < 10U; ++column) {
        term.feed(table[column].first);
        term.feed(U"x");
        AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, column).underline),
                             static_cast<std::uint32_t>(table[column].second));
    }
}

AURORA_TEST_CASE(sgr_underline_clear_leaves_the_rest_of_the_pen) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[1;31;4m\x1B[24m");  // 关下划线只关下划线：粗体与前景仍在这支笔上
    term.feed(U"a");
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).underline),
                         static_cast<std::uint32_t>(UnderlineStyle::None));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).flags),
                         static_cast<std::uint32_t>(borealis::grid::kFlagBold));
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 0).foreground, std::uint32_t{1});

    term.feed(U"\x1B[4:3m\x1B[0m");  // `SGR 0` 连档位一起复位
    term.feed(U"b");
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 1).underline),
                         static_cast<std::uint32_t>(UnderlineStyle::None));
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 1).flags), std::uint32_t{0});
}

AURORA_TEST_CASE(dec_private_modes_are_registered) {
    auto term = make_terminal(narrow_only);
    // 一序列多模式：DECSET/DECRST 的参数是列表，只认首个会把 vim/tmux 的批量开关漏掉。
    // 这些模式默认全为「关」，故必须先 `h` 再 `l`，两个方向各断一次才不是空转。
    term.feed(U"\x1B[?1;4;6;25;1004;2004h");
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_key_app);
    AURORA_TEST_CHECK_TRUE(term.modes().insert_mode);
    AURORA_TEST_CHECK_TRUE(term.modes().origin_mode);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_visible);
    AURORA_TEST_CHECK_TRUE(term.modes().focus_reporting);
    AURORA_TEST_CHECK_TRUE(term.modes().bracketed_paste);

    term.feed(U"\x1B[?1;4;6;25;1004;2004l");
    AURORA_TEST_CHECK_FALSE(term.modes().cursor_key_app);
    AURORA_TEST_CHECK_FALSE(term.modes().insert_mode);
    AURORA_TEST_CHECK_FALSE(term.modes().origin_mode);
    AURORA_TEST_CHECK_FALSE(term.modes().cursor_visible);
    AURORA_TEST_CHECK_FALSE(term.modes().focus_reporting);
    AURORA_TEST_CHECK_FALSE(term.modes().bracketed_paste);
}

AURORA_TEST_CASE(mouse_report_modes_are_registered) {
    // `SPEC.FEAT.TERM.06` 的六条模式位。`?1007` 的缺省档是「开」（裁决 7.77⑤），故它与其他五条
    // 的方向相反：先 `l` 再 `h` 两个方向各断一次，否则测到的是缺省值而非序列效果。
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[?9;1000;1002;1003;1006h");
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_x10);
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_normal);
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_button_events);
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_any_events);
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_sgr);

    term.feed(U"\x1B[?9l\x1B[?1000l\x1B[?1002l\x1B[?1003l\x1B[?1006l");
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_x10);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_normal);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_button_events);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_any_events);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_sgr);

    AURORA_TEST_CHECK_TRUE(term.modes().alternate_scroll);
    term.feed(U"\x1B[?1007l");
    AURORA_TEST_CHECK_FALSE(term.modes().alternate_scroll);
    term.feed(U"\x1B[?1007h");
    AURORA_TEST_CHECK_TRUE(term.modes().alternate_scroll);
}

AURORA_TEST_CASE(x10_setup_clears_the_higher_mouse_levels) {
    // 蕴含关系落在**写**的一侧：`?9h` 会把三个高档清掉，于是读侧取最高的层级恰是 X10。
    // 不清的话，程序接着发一条 `?9 h` 想退到「只报按下」，实际仍是 `?1002` 在报拖动。
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[?1002;1003h");
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_button_events);
    term.feed(U"\x1B[?9h");
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_x10);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_normal);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_button_events);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_any_events);
    // `?9 l` 只关自己那一档，不去动从未开过的高档（清档只在置位那一侧发生）。
    term.feed(U"\x1B[?9l");
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_x10);
}

AURORA_TEST_CASE(clearing_the_normal_level_ends_mouse_reporting) {
    // 程序退出时普遍只补一条 `?1000 l`（vim 与 htop 皆然）。留着 1002/1003 就等于上报没关：
    // 滚轮会继续发按钮 64/65，而本地回看再也接不回来——这条清档规则的全部根据就在这里。
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[?1000;1002;1003h");
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_any_events);
    term.feed(U"\x1B[?1000l");
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_normal);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_button_events);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_any_events);
    // 反向不成立：单独关 `?1002` 不动 `?1000`，按下与松开仍要报（xterm 亦只降一层）。
    term.feed(U"\x1B[?1000;1002h\x1B[?1002l");
    AURORA_TEST_CHECK_TRUE(term.modes().mouse_normal);
    AURORA_TEST_CHECK_FALSE(term.modes().mouse_button_events);
}

AURORA_TEST_CASE(insert_mode_shifts_before_print) {
    auto term = make_terminal(narrow_only);
    term.feed(U"abcd");
    term.feed(U"\x1B[?4h\x1B[1;2HXY");     // IRM：写入位置腾出等量空格，右侧内容右移
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("aXYbcd"));
}

AURORA_TEST_CASE(origin_mode_addresses_region) {
    auto term = Terminal{10, 4, 5, narrow_only};
    term.feed(U"\x1B[2;3r\x1B[?6h");       // 区域 [1,2] + DECOM：原点即区域左上
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{1});
    term.feed(U"\x1B[1;5H");
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{1});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{4});
    term.feed(U"x");
    term.feed(U"\x1B[?6l\x1B[1;1HA");      // 退出原点模式后按整屏解释
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{0});
}

AURORA_TEST_CASE(save_and_restore_cursor) {
    auto term = make_terminal(narrow_only);
    // `\x1B7` 会被十六进制转义贪婪吞成 U+01B7，故 ESC 用八进制 `\033` 起头。
    term.feed(U"\x1B[2;3H\0337\x1B[1;1H\0338");   // ESC 7 / ESC 8
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{1});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{2});
    term.feed(U"\x1B[3;4H\x1B[s\x1B[1;1H\x1B[u");  // CSI s / CSI u 同效
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{2});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{3});
}

AURORA_TEST_CASE(decscusr_maps_style_levels) {
    auto term = make_terminal(narrow_only);
    // 默认即「闪烁块」，故每一档都先从已知态偏离再断言，否则测到的是缺省值而非序列效果。
    term.feed(U"\x1B[2 q");  // 静止块
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Block);
    AURORA_TEST_CHECK_FALSE(term.modes().cursor_blinking);

    term.feed(U"\x1B[3 q");  // 闪烁下划线
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Underline);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_blinking);

    term.feed(U"\x1B[4 q");  // 静止下划线
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Underline);
    AURORA_TEST_CHECK_FALSE(term.modes().cursor_blinking);

    term.feed(U"\x1B[5 q");  // 闪烁竖线
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Bar);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_blinking);

    term.feed(U"\x1B[6 q");  // 静止竖线
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Bar);
    AURORA_TEST_CHECK_FALSE(term.modes().cursor_blinking);

    term.feed(U"\x1B[1 q");  // 闪烁块
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Block);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_blinking);

    // 缺省 Ps 与显式 0 同义（xterm：按标准复位成闪烁块），越界档位同理——不得停在上一档。
    term.feed(U"\x1B[6 q\x1B[ q");
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Block);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_blinking);
    term.feed(U"\x1B[5 q\x1B[9 q");
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Block);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_blinking);
}

AURORA_TEST_CASE(decscusr_needs_space_intermediate) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[3 q");
    // 空格中间字节是 DECSCUSR 的识别标志：缺了它 `CSI 5 q` 是别的序列（我们未识别即忽略），
    // 若照单全收，任何以 q 结尾的未实现序列都会改掉光标形态。
    term.feed(U"\x1B[5q");
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Underline);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_blinking);
    // 带私有前缀的写法 xterm 一并接受。
    term.feed(U"\x1B[?6 q");
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Bar);
    AURORA_TEST_CHECK_FALSE(term.modes().cursor_blinking);
}

AURORA_TEST_CASE(dec_special_graphics_paints_box_lines) {
    // `ESC ( 0` 之后的 lqqk 必须变成箱线码点，`ESC ( B` 之后恢复字母（SPEC.FEAT.TERM.01 验收线）。
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B(0lqqk\x1B(Bqq");
    const std::u32string expected{U'\x250C', U'\x2500', U'\x2500', U'\x2510', U'q', U'q'};
    AURORA_TEST_CHECK(row_code_points(term, 0, 6) == expected);
}

AURORA_TEST_CASE(esc_percent_g_records_utf8_and_keeps_line_drawing) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B%G");
    AURORA_TEST_CHECK_TRUE(term.modes().utf8_received);
    term.feed(U"\x1B(0l");
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).code_point),
                         std::uint32_t{U'\x250C'});  // UTF-8 模式下框线仍生效
    term.feed(U"\x1B%@");
    AURORA_TEST_CHECK_FALSE(term.modes().utf8_received);
}

AURORA_TEST_CASE(alternate_screen_leaves_main_and_history_untouched) {
    auto term = make_terminal(narrow_only);
    fill_three_rows(term);
    const auto lines_before = term.main_grid().total_lines();

    term.feed(U"\x1B[?1049h");
    AURORA_TEST_CHECK_TRUE(term.modes().alternate_screen);
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string(""));
    term.feed(U"ALT");
    AURORA_TEST_CHECK_EQ(term.main_grid().total_lines(), lines_before);  // 备屏不进 scrollback

    term.feed(U"\x1B[?1049l");
    AURORA_TEST_CHECK_FALSE(term.modes().alternate_screen);
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("AAA"));          // 退出即恢复主屏原状
    AURORA_TEST_CHECK_EQ(row_text(term, 2), std::string("CCC"));
}

AURORA_TEST_CASE(cursor_is_restored_after_1049_exit) {
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[2;5H\x1B[?1049h\x1B[1;1H\x1B[?1049l");
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{1});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{4});
}

AURORA_TEST_CASE(wide_characters_pair_with_continuation_cells) {
    auto term = make_terminal(stub_width);
    term.feed(U"\x4E2D\x4E2Dx");
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 0).width, std::uint8_t{2});
    AURORA_TEST_CHECK_TRUE(cell_at(term, 0, 1).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(cell_at(term, 0, 2).width, std::uint8_t{2});
    AURORA_TEST_CHECK_TRUE(cell_at(term, 0, 3).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 4).code_point),
                         std::uint32_t{U'x'});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{5});
}

AURORA_TEST_CASE(overwriting_tail_cell_clears_its_leader) {
    // 光标停在双宽字符的后半格时，前半格必须一起清空，否则留下半格错位（SPEC.FEAT.TERM.08 验收）。
    auto term = make_terminal(stub_width);
    term.feed(U"\x4E2DX");
    term.feed(U"\x1B[1;2H.");  // 落在延续格上
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).code_point),
                         std::uint32_t{U' '});
    AURORA_TEST_CHECK_FALSE(cell_at(term, 0, 0).is_wide_continuation());
}

AURORA_TEST_CASE(ambiguous_width_override_changes_occupancy) {
    // 同一份含 Ambiguous 字符的输出：默认单宽、覆盖后双宽，且光标列位与占位一致（裁决 7.15）。
    auto narrow = make_terminal(stub_width);
    narrow.feed(U"\x00B1x");
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(narrow, 0, 1).code_point), std::uint32_t{U'x'});
    AURORA_TEST_CHECK_EQ(narrow.cursor().column, std::size_t{2});

    auto wide = make_terminal(stub_width);
    wide.set_ambiguous_width(AmbiguousWidth::Wide);
    wide.feed(U"\x00B1x");
    AURORA_TEST_CHECK_TRUE(cell_at(wide, 0, 1).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(wide, 0, 2).code_point), std::uint32_t{U'x'});
    AURORA_TEST_CHECK_EQ(wide.cursor().column, std::size_t{3});
}

AURORA_TEST_CASE(single_width_policy_keeps_cjk_in_one_cell) {
    // 常数注入值不受 Unicode 版本影响：单宽口径下双宽字符各占一格，内容不得丢失或错位。
    // 真实判定的双宽占位由 itest_unicode_width 端到端覆盖。
    auto term = make_terminal(narrow_only);
    term.feed(U"\x4E2D\x4E2D");
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).code_point),
                         std::uint32_t{U'\x4E2D'});
    AURORA_TEST_CHECK_FALSE(cell_at(term, 0, 1).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{2});
}

AURORA_TEST_CASE(ris_restores_power_on_state) {
    auto term = make_terminal(narrow_only);
    term.feed(U"AAA\r\nBBB\x1B[?7l\x1B[1m\x1B[2;3r\x1B[5 q");
    term.feed(U"\033c");
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string(""));
    AURORA_TEST_CHECK_TRUE(term.modes().auto_wrap);
    AURORA_TEST_CHECK_EQ(term.scroll_region().bottom, std::size_t{2});
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{0});
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Block);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_blinking);
    term.feed(U"X");
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 0).flags), std::uint32_t{0});
}

AURORA_TEST_CASE(injected_session_defaults_seed_the_modes_and_the_ambiguous_width) {
    // 判据文 §7 的三条构造期注入（裁决 7.76②）：光标形态与闪烁档进 `modes_`，Ambiguous 口径进宽度
    // 判定的入参。三条都取与库缺省档**互异**的值，否则「没装接缝」与「装了同样的值」读数相同。
    auto term = make_terminal(stub_width, TerminalDefaults{
                                              .cursor_shape = CursorShape::Bar,
                                              .cursor_blinking = false,
                                              .ambiguous_width = AmbiguousWidth::Wide,
                                          });
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Bar);
    AURORA_TEST_CHECK_FALSE(term.modes().cursor_blinking);
    term.feed(U"\x00B1x");  // 「±」由桩按 Ambiguous 口径给宽，「中」那条腿与本档无关
    AURORA_TEST_CHECK_TRUE(cell_at(term, 0, 1).is_wide_continuation());
    AURORA_TEST_CHECK_EQ(static_cast<std::uint32_t>(cell_at(term, 0, 2).code_point), std::uint32_t{U'x'});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{3});
}

AURORA_TEST_CASE(ris_restores_the_injected_defaults_rather_than_the_library_ones) {
    // 一次 `reset` 不该静默抹掉用户配置：注入档就是本会话的初始态（裁决 7.76② 的 Q1）。
    // 中间那句远端改档是承重前提——若只断「RIS 之后仍是注入档」，实现里把 RIS 写成不动 `modes_`
    // 也能全绿，那测的是「构造播种」而不是「复位到注入档」。
    auto term = make_terminal(stub_width, TerminalDefaults{
                                              .cursor_shape = CursorShape::Underline,
                                              .cursor_blinking = false,
                                              .ambiguous_width = AmbiguousWidth::Wide,
                                          });
    term.feed(U"\x1B[5 q");  // DECSCUSR：远端此刻对形态与闪烁档有话语权
    term.set_ambiguous_width(AmbiguousWidth::Narrow);  // Ambiguous 同构：运行期改档也不该越过 reset
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Bar);
    AURORA_TEST_CHECK_TRUE(term.modes().cursor_blinking);

    term.feed(U"\033c");
    AURORA_TEST_CHECK_EQ(term.modes().cursor_shape, CursorShape::Underline);
    AURORA_TEST_CHECK_FALSE(term.modes().cursor_blinking);
    term.feed(U"\x00B1");
    AURORA_TEST_CHECK_TRUE(cell_at(term, 0, 1).is_wide_continuation());
}

AURORA_TEST_CASE(full_screen_dirty_marks_row_identity_changes_only) {
    auto term = make_terminal(narrow_only);
    AURORA_TEST_CHECK_FALSE(term.full_screen_dirty());

    term.feed(U"\x1B[H\x1B[2J");   // 清屏只改 cell 内容，不动行号对应关系
    AURORA_TEST_CHECK_FALSE(term.full_screen_dirty());

    term.feed(U"1\r\n2\r\n3\r\n4");  // 整屏滚动：行号与内容的对应关系变了
    AURORA_TEST_CHECK_TRUE(term.full_screen_dirty());
    term.clear_full_screen_dirty();
    AURORA_TEST_CHECK_FALSE(term.full_screen_dirty());
}

AURORA_TEST_CASE(unknown_sequences_do_not_break_printing) {
    // 未识别序列与设备查询被消费掉而不中断后续输出（架构 §5.5 的降级口径）。
    auto term = make_terminal(narrow_only);
    term.feed(U"\x1B[?99h\x1B[5n\x1B[c\x1B]0;title\x07ok");
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("ok"));
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{2});
}

AURORA_TEST_CASE(resize_pulls_history_back_into_view) {
    // 变高时底部锚定：被顶进 scrollback 的历史重新回到视口，输出顺序不变。
    Terminal term{4, 2, 5, narrow_only};
    term.feed(U"1\r\n2\r\n3\r\n4");
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("3"));

    term.resize(4, 4);

    AURORA_TEST_CHECK_EQ(term.active_grid().visible_rows(), std::size_t{4});
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("1"));
    AURORA_TEST_CHECK_EQ(row_text(term, 3), std::string("4"));
    AURORA_TEST_CHECK_TRUE(term.full_screen_dirty());  // 行号与内容的对应关系整体变了
}

AURORA_TEST_CASE(resize_narrows_columns_without_reflow) {
    // 列宽变更沿用裁决 7.5：变窄即截断，已有行不重排到新宽度。
    Terminal term{10, 3, 5, narrow_only};
    term.feed(U"abcdefgh");

    term.resize(4, 3);

    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("abcd"));
    AURORA_TEST_CHECK_EQ(term.active_grid().columns(), std::size_t{4});
}

AURORA_TEST_CASE(resize_resets_scroll_region_and_clamps_cursor) {
    // 滚动区域与光标挂在尺寸上：resize 后带必须回到全屏，光标不得留在界外。
    Terminal term{10, 5, 5, narrow_only};
    term.feed(U"\x1B[2;4r\x1B[5;10H");
    AURORA_TEST_CHECK_EQ(term.scroll_region().bottom, std::size_t{3});

    term.resize(6, 3);

    AURORA_TEST_CHECK_EQ(term.scroll_region().top, std::size_t{0});
    AURORA_TEST_CHECK_EQ(term.scroll_region().bottom, std::size_t{2});
    AURORA_TEST_CHECK_EQ(term.cursor().row, std::size_t{2});
    AURORA_TEST_CHECK_EQ(term.cursor().column, std::size_t{5});
    term.feed(U"x");  // 钳位失效就会在此越界
    AURORA_TEST_CHECK_EQ(row_text(term, 2), std::string("     x"));
}

AURORA_TEST_CASE(resize_to_same_size_leaves_no_damage) {
    // 去抖后仍可能收到同一尺寸（SPEC.FEAT.XFER.01）；无变化却判废副本会让每帧整屏重建。
    Terminal term{10, 3, 5, narrow_only};
    term.feed(U"x");
    term.clear_full_screen_dirty();

    term.resize(10, 3);

    AURORA_TEST_CHECK_FALSE(term.full_screen_dirty());
    AURORA_TEST_CHECK_EQ(row_text(term, 0), std::string("x"));
}

}  // namespace borealis::test_cases::utest_terminal
