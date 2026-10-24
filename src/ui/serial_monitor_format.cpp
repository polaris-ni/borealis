// ============================================================
// 串口监视卡的行折算纯逻辑件实现（src/ui/serial_monitor_format.cpp）
// ------------------------------------------------------------
// 两个纯函数：`monitor_rows`（run 表 → 行表）与 `monitor_timestamp_prefix`（毫秒 → 前缀）。
// 行形态的算术写在头注与稿 §2，这里只兑现它；断言住在 `utest_serial_monitor_format`。
//
// 断点状态机只有一枚旗标 `awaiting_lf`：CR 收口一条行但**先不落笔**，等下一个字节判它是
// CRLF（同一条断点，一个换行）还是新行的开头。这与 `session::fold_to_plain_text` 里那枚
// `newline_after_cr_` 同形（裁决 7.116 D3①），两处都源于 F10 那条实测：真流里 CR 与 LF 成对是常态。
// ============================================================

#include "serial_monitor_format.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <span>
#include <string_view>

namespace borealis::ui {
namespace {

constexpr char kHexDigits[]{'0', '1', '2', '3', '4', '5', '6', '7',
                            '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};  ///< 大写（稿 §2 的行边界）。

/// @brief 一个字节 → 两位大写十六进制，追加进 `out`。
auto append_hex_pair(std::string &out, std::byte value) -> void {
    const auto byte = std::to_integer<unsigned char>(value);
    out += kHexDigits[(byte >> 4U) & 0x0FU];
    out += kHexDigits[byte & 0x0FU];
}

/// @brief 一个字节在尾栏里的形态：ASCII 可打印区原样，其余（0x00–0x1F 与 0x7F–0xFF）一个 `.`。
[[nodiscard]] auto ascii_cell(std::byte value) -> char {
    const auto byte = std::to_integer<unsigned char>(value);
    if (byte >= 0x20U && byte <= 0x7EU) {
        return static_cast<char>(byte);
    }
    return '.';
}

/// @brief 折出 `dump` 段：两空格 + 十六进制段（缺字节补两个空格）+ 两空格 + `|ASCII|`。
///
/// 十六进制段**恒**按 `bytes_per_row` 格铺开，故不足一格的尾行仍与上面的完整行竖向对齐；
/// 尾栏不补齐——右竖线就是它的界，补空格反而抹平「这一行有几个字节」。
[[nodiscard]] auto build_dump(std::span<const std::byte> group, std::size_t bytes_per_row) -> std::string {
    auto dump = std::string{"  "};
    dump.reserve(bytes_per_row * 3U + group.size() + 7U);
    for (std::size_t column = 0; column < bytes_per_row; ++column) {
        if (column != 0U) {
            dump += ' ';
        }
        if (column < group.size()) {
            append_hex_pair(dump, group[column]);
        } else {
            dump += "  ";
        }
    }
    dump += "  |";
    for (const auto byte : group) {
        dump += ascii_cell(byte);
    }
    dump += '|';
    return dump;
}

}  // namespace

auto row_text(const MonitorRow &row) -> std::string {
    auto text = row.timestamp_prefix;
    text += row.direction_label;
    text += row.dump;
    return text;
}

auto monitor_timestamp_prefix(std::int64_t timestamp_ms) -> std::string {
    auto seconds = timestamp_ms / 1000;
    auto millis = timestamp_ms % 1000;
    if (millis < 0) {
        // 时钟回拨给的负余数：向下去一秒、余数摆正，否则前缀的秒位与毫秒位差一整秒。
        millis += 1000;
        --seconds;
    }
    const auto moment = static_cast<std::time_t>(seconds);
    std::tm broken{};
#if defined(_WIN32)
    // WHY 不用 std::localtime：它返回进程级共享静态缓冲，多个会话的读线程会互相改写
    // （`src/session/session_log.cpp` 的 `timestamp_prefix` 同一条口径）。
    localtime_s(&broken, &moment);
#else
    localtime_r(&moment, &broken);
#endif
    char stamp[16]{};
    const auto written = std::strftime(stamp, sizeof(stamp), "[%H:%M:%S", &broken);
    auto text = std::string{stamp, written};
    text += '.';
    text += static_cast<char>('0' + (millis / 100) % 10);  // 毫秒三位零填充
    text += static_cast<char>('0' + (millis / 10) % 10);
    text += static_cast<char>('0' + millis % 10);
    text += "] ";
    return text;
}

auto monitor_rows(const std::vector<session::RawRun> &runs, const MonitorRowOptions &options)
    -> std::vector<MonitorRow> {
    const auto bytes_per_row = options.bytes_per_row == 0U ? kMonitorBytesPerRow : options.bytes_per_row;
    auto rows = std::vector<MonitorRow>{};
    auto line = std::vector<std::byte>{};
    auto line_direction = session::RawDirection::Received;
    auto line_timestamp_ms = std::int64_t{0};
    auto awaiting_lf = false;  ///< 行尾刚收了一个 CR：紧跟的 LF 属于同一条断点，不另起一行。

    // 一条行的处置：按每行字节数切格、逐格成行。方向与时刻挂的是**行首字节那个 run**
    // （D6①：run 级单点时间戳，续行不另取时刻）。
    const auto flush_line = [&] {
        if (line.empty()) {
            awaiting_lf = false;
            return;
        }
        const auto prefix = options.timestamp_prefix ? monitor_timestamp_prefix(line_timestamp_ms)
                                                     : std::string{};
        const char *const label =
            line_direction == session::RawDirection::Received ? "RX" : "TX";
        for (std::size_t offset = 0; offset < line.size(); offset += bytes_per_row) {
            const auto group = std::span<const std::byte>{line.data() + offset,
                                                          std::min(line.size() - offset, bytes_per_row)};
            auto row = MonitorRow{};
            row.direction = line_direction;
            row.timestamp_prefix = prefix;
            row.direction_label = label;
            row.dump = build_dump(group, bytes_per_row);
            rows.push_back(std::move(row));
        }
        line.clear();
        awaiting_lf = false;
    };

    for (const auto &run : runs) {
        for (const auto byte : run.bytes) {
            const auto value = std::to_integer<unsigned char>(byte);
            if (awaiting_lf) {
                awaiting_lf = false;
                if (value == static_cast<unsigned char>('\n')) {
                    line.push_back(byte);  // CRLF 是一条断点：LF 归上一条行
                    flush_line();
                    continue;
                }
                flush_line();  // 孤立 CR 已把上一条行收口，当前字节另起一行
            }
            if (line.empty()) {
                // 行首字节来自哪个 run，这一行就挂哪个方向与哪个时刻。
                line_direction = run.direction;
                line_timestamp_ms = run.timestamp_ms;
            }
            line.push_back(byte);
            if (value == static_cast<unsigned char>('\r')) {
                awaiting_lf = true;  // 先不落笔：下一个字节决定这是 CRLF 还是孤立 CR
            } else if (value == static_cast<unsigned char>('\n')) {
                flush_line();
            }
        }
        // run 到此结束：`line` 里剩下的是挂起尾段，与后续 run 拼成同一段显示行（F10）。
    }
    flush_line();  // 快照末尾的挂起尾段也上屏，见头注的口径 ⑵
    return rows;
}

}  // namespace borealis::ui
