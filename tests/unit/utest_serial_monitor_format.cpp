/// 测试类型: unit
/// 目标单元: src/ui/serial_monitor_format.{h,cpp}（无 au:: 类型）纯折算件
/// 测试说明: 守住串口监视卡的行形态（稿 codespec/UI_SERIAL_MONITOR.draft.md §2／D5①／D6①）——
///           8 字节档那一行的逐字符串形（满格 54 字符）、十六进制段的大写与定宽补齐、尾栏只承
///           ASCII 可打印区其余一个 `.`、CR／LF／CRLF 各折一条断点、被切在两个 run 里的同一条
///           设备行仍是同一段（F10 实测 15 + 3 那一例）、时间戳开关与毫秒零填充、每行字节数档。
///           面板那侧的开关联动与留痕上屏属 itest_serial_monitor_panel（批 3）。

#include "ui/serial_monitor_format.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/session/raw_tap.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_serial_monitor_format {

namespace {

[[nodiscard]] auto run(session::RawDirection direction, std::int64_t timestamp_ms, std::string_view text)
    -> session::RawRun {
    auto out = session::RawRun{};
    out.direction = direction;
    out.timestamp_ms = timestamp_ms;
    out.bytes.reserve(text.size());
    for (const auto character : text) {
        out.bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return out;
}

[[nodiscard]] auto received(std::string_view text) -> session::RawRun {
    return run(session::RawDirection::Received, 0, text);
}

/// @brief 本机的「当天 hour:minute:second.millisecond」折算成 epoch 毫秒。
///
/// WHY 不写死一个 epoch 值：前缀取的是**本地历法**（D6①），同一个 epoch 毫秒在 UTC+8 与 UTC−5
/// 上是两个钟面时刻；用 `mktime` 从本地 broken-down 反推，断言就跟宿主时区无关了
/// （`SPEC.FEAT.CONN.11` 那条会话日志的本地秒前缀同口径）。
[[nodiscard]] auto local_epoch_ms(int hour, int minute, int second, int millisecond) -> std::int64_t {
    auto broken = std::tm{};
    broken.tm_year = 2026 - 1900;
    broken.tm_mon = 9;  // 10 月 25 日：落在两档验证的任何一个时区里都不是 DST 切换点
    broken.tm_mday = 25;
    broken.tm_hour = hour;
    broken.tm_min = minute;
    broken.tm_sec = second;
    broken.tm_isdst = -1;  // 由宿主判定夏令时，否则跨切换的那一档会差一小时
    return static_cast<std::int64_t>(std::mktime(&broken)) * 1000 + millisecond;
}

/// @brief 十六进制段之后、竖线之前的那串空白：缺的格每格三个字符，再加上竖线前两格。
///
/// 定宽补齐是这条判据本身（不足一格的尾行仍与上面的完整行竖向对齐），写成算式而不是手数空格。
[[nodiscard]] auto gap_to_tail_bar(std::size_t present, std::size_t bytes_per_row) -> std::string {
    return std::string((bytes_per_row - present) * 3U + 2U, ' ');
}

/// @brief 期望的一行：`前缀 + 方向 + 两个空格 + 十六进制 + 补齐 + 尾栏`。
[[nodiscard]] auto expected_line(std::string_view prefix, std::string_view label, std::string_view hex,
                                 std::size_t present, std::string_view tail) -> std::string {
    auto want = std::string{prefix};
    want += label;
    want += "  ";
    want += hex;
    want += gap_to_tail_bar(present, ui::kMonitorBytesPerRow);
    want += '|';
    want += tail;
    want += '|';
    return want;
}

const auto kOptions = ui::MonitorRowOptions{};  ///< 缺省档：8 字节/行 + 带时间戳。

}  // namespace

AURORA_TEST_CASE(a_full_eight_byte_row_is_fifty_four_characters) {
    // 稿 §2 的算术：`[HH:MM:SS.mmm] ` 15 + `RX` 2 + 两空格 2 + 十六进制段 23 + 两空格 2 +
    // `|ASCII|` 10 = 54 字符；14 pt 等宽（F8：单格 11 dp）即 594 dp，容得进 640 dp 的卡片。
    const auto runs = std::vector{
        run(session::RawDirection::Received, local_epoch_ms(12, 0, 0, 345), "ABCDEFGH")};
    const auto rows = ui::monitor_rows(runs, kOptions);
    AURORA_TEST_REQUIRE_EQ(rows.size(), 1U);
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]),
                         expected_line("[12:00:00.345] ", "RX", "41 42 43 44 45 46 47 48", 8,
                                       "ABCDEFGH"));
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]).size(), 54U);
    AURORA_TEST_CHECK_TRUE(rows[0].direction == session::RawDirection::Received);
    // 三段拼接就是那一行：面板把前缀与正文画成默认色、方向画成一列强调色。
    AURORA_TEST_CHECK_EQ(rows[0].timestamp_prefix, "[12:00:00.345] ");
    AURORA_TEST_CHECK_EQ(rows[0].direction_label, "RX");
    AURORA_TEST_CHECK_EQ(rows[0].dump, "  41 42 43 44 45 46 47 48  |ABCDEFGH|");
}

AURORA_TEST_CASE(transmitted_runs_are_labelled_TX_and_keep_the_same_grid) {
    const auto runs =
        std::vector{run(session::RawDirection::Transmitted, local_epoch_ms(12, 0, 0, 500), "AT\r\n")};
    const auto rows = ui::monitor_rows(runs, kOptions);
    AURORA_TEST_REQUIRE_EQ(rows.size(), 1U);
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]),
                         expected_line("[12:00:00.500] ", "TX", "41 54 0D 0A", 4, "AT.."));
    // 不足一格也铺满定宽：短行的尾栏竖线与上面满格行的竖线在同一列上。
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]).size(), 50U);
}

AURORA_TEST_CASE(hex_is_uppercase_and_the_tail_only_carries_printable_ascii) {
    // 稿 §2 的行边界：0x20–0x7E 原样，0x00–0x1F 与 0x7F–0xFF 一律一个 `.`。
    auto first = session::RawRun{};
    first.direction = session::RawDirection::Received;
    first.timestamp_ms = local_epoch_ms(9, 8, 7, 6);
    first.bytes = {std::byte{0x00}, std::byte{0x1F}, std::byte{0x20}, std::byte{0x7E}, std::byte{0x7F},
                   std::byte{0xFF}, std::byte{0xAB}, std::byte{0x0A}};
    const auto rows = ui::monitor_rows(std::vector{first}, kOptions);
    AURORA_TEST_REQUIRE_EQ(rows.size(), 1U);
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]),
                         expected_line("[09:08:07.006] ", "RX", "00 1F 20 7E 7F FF AB 0A", 8,
                                       ".. ~...."));
}

AURORA_TEST_CASE(cr_lf_and_crlf_each_break_exactly_one_line) {
    // `AB\r\nCD\rEF`：CRLF 一条断点、孤立 CR 一条断点、剩下的没有行尾（挂起尾段，最后一案里上屏）。
    const auto runs = std::vector{received("AB\r\nCD\rEF")};
    const auto rows = ui::monitor_rows(runs, ui::MonitorRowOptions{.timestamp_prefix = false});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]), expected_line("", "RX", "41 42 0D 0A", 4, "AB.."));
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[1]), expected_line("", "RX", "43 44 0D", 3, "CD."));
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[2]), expected_line("", "RX", "45 46", 2, "EF"));
}

AURORA_TEST_CASE(a_device_line_split_across_two_runs_stays_one_line) {
    // F10 实测的那条形态：一条 20 字节的设备行被切成 15 + 5 两个读块。
    // 挂起尾段不单独成行、也不丢——它与后续 run 拼成同一段显示行（稿 §2 的「挂起尾段」那一档）。
    const auto runs =
        std::vector{run(session::RawDirection::Received, local_epoch_ms(12, 0, 0, 100),
                        "abcdefghijklmno"),
                    run(session::RawDirection::Received, local_epoch_ms(12, 0, 0, 200), "pqr\r\n")};
    const auto rows = ui::monitor_rows(runs, kOptions);
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);  // 20 字节 = 8 + 8 + 4，而不是 8 + 7 + 3 + 1 那种碎法
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]),
                         expected_line("[12:00:00.100] ", "RX", "61 62 63 64 65 66 67 68", 8,
                                        "abcdefgh"));
    // 第二段被后续 run 补满了：尾字节就是下一个 run 的首字节。
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[1]),
                         expected_line("[12:00:00.100] ", "RX", "69 6A 6B 6C 6D 6E 6F 70", 8,
                                        "ijklmnop"));
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[2]),
                         expected_line("[12:00:00.100] ", "RX", "71 72 0D 0A", 4, "qr.."));
    // 时刻挂的是**行首字节那个 run**（D6①：run 级单点时间戳，续行不另取时刻）。
    for (const auto &row : rows) {
        AURORA_TEST_CHECK_EQ(row.timestamp_prefix, "[12:00:00.100] ");
    }
}

AURORA_TEST_CASE(a_crlf_split_at_the_run_boundary_is_still_one_break) {
    // 上一案的边界变体：CR 落在第一个 run 的末字节、LF 落在第二个 run 的首字节。
    // 若此处按「run 结束即落笔」草率收口，`AB\r` 与 `\n` 会拆成两条行（F10：run 边界与行边界无关）。
    const auto runs = std::vector{received("AB\r"), received("\nCD")};
    const auto rows = ui::monitor_rows(runs, ui::MonitorRowOptions{.timestamp_prefix = false});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);  // 一条 CRLF 收口的行 + 一条挂起的尾段
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]), expected_line("", "RX", "41 42 0D 0A", 4, "AB.."));
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[1]), expected_line("", "RX", "43 44", 2, "CD"));
}

AURORA_TEST_CASE(a_long_run_becomes_several_rows_of_the_same_stamp_and_direction) {
    const auto runs = std::vector{
        run(session::RawDirection::Transmitted, local_epoch_ms(23, 59, 59, 999), "0123456789")};
    const auto rows = ui::monitor_rows(runs, kOptions);  // 10 字节 = 8 + 2
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]),
                         expected_line("[23:59:59.999] ", "TX", "30 31 32 33 34 35 36 37", 8,
                                        "01234567"));
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[1]), expected_line("[23:59:59.999] ", "TX", "38 39", 2, "89"));
    AURORA_TEST_CHECK_TRUE(rows[1].direction == session::RawDirection::Transmitted);
}

AURORA_TEST_CASE(a_line_started_by_TX_keeps_that_label_when_RX_completes_it) {
    // 回显场景：出向那一段没有行尾，入向字节补上它——稿 §2 的「同一显示行」意味着这一段的归属
    // 由**行首**决定（D6① 的 run 级单点也才有唯一解释），中途改标会凭空多出一条断点。
    const auto runs = std::vector{run(session::RawDirection::Transmitted, 1, "AT"),
                                  run(session::RawDirection::Received, 2, "\r\n")};
    const auto rows = ui::monitor_rows(runs, ui::MonitorRowOptions{.timestamp_prefix = false});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 1U);
    AURORA_TEST_CHECK_EQ(rows[0].direction_label, "TX");
    AURORA_TEST_CHECK_TRUE(rows[0].direction == session::RawDirection::Transmitted);
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]), expected_line("", "TX", "41 54 0D 0A", 4, "AT.."));
}

AURORA_TEST_CASE(the_timestamp_switch_is_a_display_time_option) {
    const auto runs = std::vector{
        run(session::RawDirection::Received, local_epoch_ms(12, 0, 0, 345), "ABCDEFGH")};
    const auto without = ui::monitor_rows(runs, ui::MonitorRowOptions{.timestamp_prefix = false});
    AURORA_TEST_REQUIRE_EQ(without.size(), 1U);
    // 关掉前缀即整串短 15 字符：39 字符那一档在 520 dp 卡里也放得下（F8），且一次 localtime 都不调。
    AURORA_TEST_CHECK_EQ(ui::row_text(without[0]),
                         expected_line("", "RX", "41 42 43 44 45 46 47 48", 8, "ABCDEFGH"));
    AURORA_TEST_CHECK_EQ(ui::row_text(without[0]).size(), 39U);
    AURORA_TEST_CHECK_EQ(without[0].dump, "  41 42 43 44 45 46 47 48  |ABCDEFGH|");
    AURORA_TEST_CHECK_TRUE(without[0].timestamp_prefix.empty());
}

AURORA_TEST_CASE(milliseconds_are_zero_padded_and_the_clock_carries_up) {
    AURORA_TEST_CHECK_EQ(ui::monitor_timestamp_prefix(local_epoch_ms(7, 5, 3, 0)), "[07:05:03.000] ");
    AURORA_TEST_CHECK_EQ(ui::monitor_timestamp_prefix(local_epoch_ms(7, 5, 3, 42)), "[07:05:03.042] ");
    // 相邻毫秒的进位：一秒的最后一毫秒与下一秒的第一毫秒相邻，且各 15 字符。
    const auto last_of_second = local_epoch_ms(7, 5, 3, 999);
    const auto first_of_next = local_epoch_ms(7, 5, 4, 0);
    AURORA_TEST_CHECK_EQ(first_of_next - last_of_second, 1);
    AURORA_TEST_CHECK_EQ(ui::monitor_timestamp_prefix(last_of_second), "[07:05:03.999] ");
    AURORA_TEST_CHECK_EQ(ui::monitor_timestamp_prefix(first_of_next), "[07:05:04.000] ");
    AURORA_TEST_CHECK_EQ(ui::monitor_timestamp_prefix(first_of_next).size(), 15U);
    // 本机上「本地零点的前一毫秒」：必须落回 23:59:59.999（负余数向下取整那条腿的另一半）。
    AURORA_TEST_CHECK_EQ(ui::monitor_timestamp_prefix(local_epoch_ms(0, 0, 0, 0) - 1),
                         "[23:59:59.999] ");
}

AURORA_TEST_CASE(bytes_per_row_controls_the_grid_and_zero_falls_back) {
    const auto runs = std::vector{received("0123456789")};  // 10 字节，无行尾
    const auto four =
        ui::monitor_rows(runs, ui::MonitorRowOptions{.bytes_per_row = 4, .timestamp_prefix = false});
    AURORA_TEST_REQUIRE_EQ(four.size(), 3U);  // 4 + 4 + 2
    AURORA_TEST_CHECK_EQ(four[0].dump, "  30 31 32 33  |0123|");
    AURORA_TEST_CHECK_EQ(four[1].dump, "  34 35 36 37  |4567|");
    AURORA_TEST_CHECK_EQ(four[2].dump, "  38 39        |89|");
    // 0 档不另立「一行零字节」的形态：回落缺省 8 字节档。
    const auto fallback =
        ui::monitor_rows(runs, ui::MonitorRowOptions{.bytes_per_row = 0, .timestamp_prefix = false});
    AURORA_TEST_REQUIRE_EQ(fallback.size(), 2U);
}

AURORA_TEST_CASE(an_empty_ring_yields_no_rows_and_a_suspended_tail_is_not_dropped) {
    AURORA_TEST_CHECK_TRUE(ui::monitor_rows(std::vector<session::RawRun>{}, kOptions).empty());
    // 快照末尾那段还没有行尾跟随的字节：不成「完整行」，但**必须上屏**（环里存着却不显示就是说
    // 谎；二进制协议可以整段没有 CR／LF）。等到行尾到来，下一次重建自动把它并回同一段。
    const auto rows = ui::monitor_rows(std::vector{received("READY")},
                                       ui::MonitorRowOptions{.timestamp_prefix = false});
    AURORA_TEST_REQUIRE_EQ(rows.size(), 1U);
    AURORA_TEST_CHECK_EQ(ui::row_text(rows[0]), expected_line("", "RX", "52 45 41 44 59", 5, "READY"));
}

AURORA_TEST_CASE(row_count_equals_the_full_fold_on_every_line_shape) {
    // 计数与折行共用一台扫描器（稿 §9.3）：四条行边界口径只要有一处走岔，这两个数就会分家。
    const auto shapes = std::vector<std::vector<session::RawRun>>{
        {},
        {received("")},
        {received("A")},
        {received("ABCDEFGH")},
        {received("ABCDEFGHI")},
        {received("AB\r\nCD\rEF")},
        {received("AB\r"), received("\nCD")},
        {received("\r\n\r\n")},
        {received("0123456789abcdef0123456789abcdef")},
        {run(session::RawDirection::Transmitted, 1, "AT"), run(session::RawDirection::Received, 2, "\r\n")},
    };
    for (const auto &runs : shapes) {
        AURORA_TEST_CHECK_EQ(ui::monitor_row_count(runs, kOptions), ui::monitor_rows(runs, kOptions).size());
    }
    // 纯二进制（整段无行尾）按每行字节数切：256 KiB → 32768 行，这是环的上界（D4①）。
    auto blob = session::RawRun{};
    blob.bytes.assign(256U * 1024U, std::byte{0xAA});
    AURORA_TEST_CHECK_EQ(ui::monitor_row_count(std::vector{blob}, kOptions), 32768U);
}

AURORA_TEST_CASE(row_window_is_a_slice_of_the_full_fold) {
    auto runs = std::vector<session::RawRun>{};
    for (int index = 0; index < 12; ++index) {
        runs.push_back(run(session::RawDirection::Received, local_epoch_ms(10, 0, 0, index),
                           "line-" + std::to_string(index) + "\r\n"));
    }
    const auto all = ui::monitor_rows(runs, kOptions);
    const auto total = ui::monitor_row_count(runs, kOptions);
    AURORA_TEST_REQUIRE_EQ(all.size(), total);
    AURORA_TEST_CHECK_GT(total, 12U);  // 两位数的行号让每条设备行都是 2 格（9 字节 = 8 + 1）
    for (std::size_t first = 0; first <= total; ++first) {
        for (const std::size_t max : {0U, 1U, 5U, 96U}) {
            const auto window = ui::monitor_row_window(runs, kOptions, first, max);
            const std::size_t begin = std::min(first, total);
            const std::size_t want = std::min(max, total - begin);
            AURORA_TEST_REQUIRE_EQ(window.size(), want);
            for (std::size_t offset = 0; offset < want; ++offset) {
                // 逐字符相等：窗口折行与全折共用同一台扫描器，前缀缓存也不许改变字符串。
                AURORA_TEST_CHECK_EQ(ui::row_text(window[offset]), ui::row_text(all[begin + offset]));
            }
        }
    }
}

AURORA_TEST_CASE(a_window_past_the_end_or_of_zero_rows_is_empty) {
    const auto runs = std::vector{received("hello")};
    const auto total = ui::monitor_row_count(runs, kOptions);
    AURORA_TEST_REQUIRE_EQ(total, 1U);
    AURORA_TEST_CHECK_TRUE(ui::monitor_row_window(runs, kOptions, total, 8U).empty());
    AURORA_TEST_CHECK_TRUE(ui::monitor_row_window(runs, kOptions, total + 5U, 8U).empty());
    AURORA_TEST_CHECK_TRUE(ui::monitor_row_window(runs, kOptions, 0U, 0U).empty());
    AURORA_TEST_CHECK_TRUE(ui::monitor_row_window(std::vector<session::RawRun>{}, kOptions, 0U, 8U).empty());
    const auto clipped = ui::monitor_row_window(runs, kOptions, 0U, 8U);
    AURORA_TEST_REQUIRE_EQ(clipped.size(), 1U);  // 超尾自动截短，不补空行
}

AURORA_TEST_CASE(a_window_inside_one_long_line_keeps_the_line_stamp_and_direction) {
    // 冻结回看最常落在一条长行中间：窗口里每一格都挂**行首** run 的方向与时刻（D6①），
    // 而不是窗口首字节所在的后继 run。
    const auto runs = std::vector{
        run(session::RawDirection::Transmitted, local_epoch_ms(12, 0, 0, 700), "abcdefghijklmnopqrstuvwxyz")};
    AURORA_TEST_CHECK_EQ(ui::monitor_row_count(runs, kOptions), 4U);  // 26 = 8 + 8 + 8 + 2
    const auto window = ui::monitor_row_window(runs, kOptions, 1U, 2U);
    AURORA_TEST_REQUIRE_EQ(window.size(), 2U);
    AURORA_TEST_CHECK_EQ(window[0].direction_label, "TX");
    AURORA_TEST_CHECK_EQ(window[0].timestamp_prefix, "[12:00:00.700] ");
    AURORA_TEST_CHECK_EQ(window[0].dump, "  69 6A 6B 6C 6D 6E 6F 70  |ijklmnop|");
    AURORA_TEST_CHECK_EQ(window[1].dump, "  71 72 73 74 75 76 77 78  |qrstuvwx|");
}

AURORA_TEST_CASE(a_window_starting_mid_line_across_two_runs_keeps_the_first_runs_stamp) {
    // F10 的 15 + 5 那一例取中段窗口：第二格的首字节已经落在第二个 run 里，但方向与时刻仍是行首
    // （第一个 run）那一份——跨 run 的挂起尾段在窗口折行里也必须成立。
    const auto runs =
        std::vector{run(session::RawDirection::Received, local_epoch_ms(12, 0, 0, 100), "abcdefghijklmno"),
                    run(session::RawDirection::Received, local_epoch_ms(12, 0, 0, 200), "pqr\r\n")};
    const auto window = ui::monitor_row_window(runs, kOptions, 1U, 1U);
    AURORA_TEST_REQUIRE_EQ(window.size(), 1U);
    AURORA_TEST_CHECK_EQ(window[0].timestamp_prefix, "[12:00:00.100] ");
    AURORA_TEST_CHECK_EQ(window[0].dump, "  69 6A 6B 6C 6D 6E 6F 70  |ijklmnop|");
    // 窗口从第三格起：`qr\r\n` 这四个字节属于第二个 run，却仍是同一条设备行。
    const auto tail = ui::monitor_row_window(runs, kOptions, 2U, 1U);
    AURORA_TEST_REQUIRE_EQ(tail.size(), 1U);
    AURORA_TEST_CHECK_EQ(tail[0].timestamp_prefix, "[12:00:00.100] ");
    AURORA_TEST_CHECK_EQ(tail[0].dump, "  71 72 0D 0A              |qr..|");
}

AURORA_TEST_CASE(a_window_honours_the_bytes_per_row_override) {
    const auto runs = std::vector{received("0123456789")};  // 10 字节 = 4 + 4 + 2
    const auto options = ui::MonitorRowOptions{.bytes_per_row = 4, .timestamp_prefix = false};
    AURORA_TEST_CHECK_EQ(ui::monitor_row_count(runs, options), 3U);
    const auto window = ui::monitor_row_window(runs, options, 1U, 1U);
    AURORA_TEST_REQUIRE_EQ(window.size(), 1U);
    AURORA_TEST_CHECK_EQ(window[0].dump, "  34 35 36 37  |4567|");
}

}  // namespace borealis::test_cases::utest_serial_monitor_format
