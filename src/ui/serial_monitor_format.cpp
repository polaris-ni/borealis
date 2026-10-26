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
#include <cstring>
#include <ctime>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

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

/// @brief 选项里的每行字节数：0 档回落缺省（与一期 `monitor_rows` 同一口径）。
[[nodiscard]] auto effective_bytes_per_row(const MonitorRowOptions &options) -> std::size_t {
    return options.bytes_per_row == 0U ? kMonitorBytesPerRow : options.bytes_per_row;
}

/// @brief 从 `from` 起找最近的 CR 或 LF（找不到就是 `size`）。
///
/// 用 `memchr` 而不是逐字节比较：二进制段（整段没有行尾）是串口监视最常见的料，
/// 一次向量化扫描比逐字节分支快一个量级；行尾之后的逐字节处理只发生在真正的断点处。
[[nodiscard]] auto next_line_break(const std::byte *data, std::size_t from, std::size_t size) -> std::size_t {
    const void *const cr = std::memchr(data + from, '\r', size - from);
    const void *const lf = std::memchr(data + from, '\n', size - from);
    std::size_t next = size;
    if (cr != nullptr) {
        next = static_cast<std::size_t>(static_cast<const std::byte *>(cr) - data);
    }
    if (lf != nullptr) {
        next = std::min(next, static_cast<std::size_t>(static_cast<const std::byte *>(lf) - data));
    }
    return next;
}

/// @brief 行扫描器：run 表 → 逐行交给 `consume`（返回 false 即停止）。
///
/// 四条行边界口径只在这里写一遍（稿 §2／§4.1③）：CRLF 一条断点、孤立 CR 一条断点、
/// 挂起尾段跨 run 拼接、快照末尾的不完整行也上屏。满格的行一凑满就交出（不等行尾），
/// 故挂起缓冲恒 ≤ 每行字节数；窗口之前的行由调用方只计数、不建字符串。
template <typename Consumer>
class RowScanner {
  public:
    RowScanner(std::size_t bytes_per_row, Consumer &consume)
        : bytes_per_row_{bytes_per_row}, consume_{consume} {
        pending_.reserve(bytes_per_row_);
    }

    /// @brief 吃进一条 run。
    auto feed(const session::RawRun &run) -> void {
        const std::byte *const data = run.bytes.data();
        const std::size_t size = run.bytes.size();
        std::size_t pos = 0;
        while (pos < size && !aborted_) {
            const std::size_t segment_end = next_line_break(data, pos, size);
            if (segment_end > pos) {
                // 走到这里说明 `data[pos]` 既不是 CR 也不是 LF；若上一段以 CR 收尾，那它就是
                // 孤立 CR（CRLF 的 LF 会落在 `segment_end == pos` 那一档）：先把上一条行收口，
                // 否则这一段会被拼进上一行（`AB\rCD` 会折成一行）。
                if (awaiting_lf_) {
                    flush_line();
                }
                begin_line(run);
                append(std::span<const std::byte>{data + pos, segment_end - pos});
                pos = segment_end;
                if (aborted_ || pos >= size) {
                    break;
                }
            }
            const auto character = std::to_integer<unsigned char>(data[pos]);
            if (awaiting_lf_) {
                awaiting_lf_ = false;
                if (character == static_cast<unsigned char>('\n')) {
                    begin_line(run);
                    append(std::span<const std::byte>{data + pos, 1});
                    flush_line();
                    ++pos;
                    continue;
                }
                flush_line();  // 孤立 CR 已把上一条行收口，当前字节另起一行
            }
            begin_line(run);
            append(std::span<const std::byte>{data + pos, 1});
            if (character == static_cast<unsigned char>('\r')) {
                awaiting_lf_ = true;
            } else {
                flush_line();
            }
            ++pos;
        }
    }

    /// @brief 收尾：快照末尾那段挂起尾段也交出去（不完整行上屏，见头注口径 ⑵）。
    auto finish() -> void {
        if (!aborted_) {
            flush_line();
        }
    }

  private:
    /// @brief 行首才定方向与时刻：行内分格与跨 run 续行都沿用行首那一份（D6①）。
    auto begin_line(const session::RawRun &run) -> void {
        if (line_started_) {
            return;
        }
        line_started_ = true;
        direction_ = run.direction;
        timestamp_ms_ = run.timestamp_ms;
    }

    /// @brief 追加一段字节，满格即交一行。
    auto append(std::span<const std::byte> bytes) -> void {
        while (!bytes.empty() && !aborted_) {
            const std::size_t take = std::min(bytes_per_row_ - pending_.size(), bytes.size());
            pending_.insert(pending_.end(), bytes.begin(),
                            bytes.begin() + static_cast<std::ptrdiff_t>(take));
            bytes = bytes.subspan(take);
            if (pending_.size() == bytes_per_row_) {
                emit();
            }
        }
    }

    /// @brief 交出挂起缓冲里的那一段（满格或行尾的最后一段）。
    auto emit() -> void {
        if (pending_.empty()) {
            return;
        }
        if (!consume_(direction_, timestamp_ms_, std::span<const std::byte>{pending_})) {
            aborted_ = true;
            return;
        }
        pending_.clear();
    }

    /// @brief 行尾：交出最后一段并结束这一行。
    auto flush_line() -> void {
        if (aborted_) {
            return;
        }
        emit();
        pending_.clear();
        line_started_ = false;
        awaiting_lf_ = false;
    }

    std::size_t bytes_per_row_;
    Consumer &consume_;
    std::vector<std::byte> pending_{};
    session::RawDirection direction_ = session::RawDirection::Received;
    std::int64_t timestamp_ms_ = 0;
    bool line_started_ = false;
    bool awaiting_lf_ = false;  ///< 行尾刚收了一个 CR：紧跟的 LF 属于同一条断点。
    bool aborted_ = false;      ///< 消费者说窗口已满：停止扫描。
};

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

auto monitor_row_count(const std::vector<session::RawRun> &runs, const MonitorRowOptions &options)
    -> std::size_t {
    auto count = std::size_t{0};
    // 计数只关心「交了几行」：字节内容交给扫描器自己的挂起缓冲，消费者不看也不留。
    auto consume = [&count](session::RawDirection, std::int64_t,
                            std::span<const std::byte>) -> bool {
        ++count;
        return true;
    };
    auto scanner = RowScanner<decltype(consume)>{effective_bytes_per_row(options), consume};
    for (const auto &run : runs) {
        scanner.feed(run);
    }
    scanner.finish();
    return count;
}

auto monitor_row_window(const std::vector<session::RawRun> &runs, const MonitorRowOptions &options,
                        std::size_t first_row, std::size_t max_rows) -> std::vector<MonitorRow> {
    if (max_rows == 0U) {
        return {};
    }
    const std::size_t bytes_per_row = effective_bytes_per_row(options);
    auto rows = std::vector<MonitorRow>{};
    // 窗口通常是一屏多一点（面板取 96 行）；reserve 只按这个量级给，不按 max_rows。
    rows.reserve(std::min<std::size_t>(max_rows, 128U));
    auto index = std::size_t{0};
    // 同一条设备行内的各格共用一份前缀：省掉重复的 localtime/strftime（F5：0.204 µs/次）。
    auto cached_timestamp_ms = std::int64_t{0};
    auto cached_prefix = std::string{};
    auto has_cached_prefix = false;
    auto consume = [&](session::RawDirection direction, std::int64_t timestamp_ms,
                       std::span<const std::byte> bytes) -> bool {
        if (index >= first_row && rows.size() < max_rows) {
            auto prefix = std::string{};
            if (options.timestamp_prefix) {
                if (!has_cached_prefix || cached_timestamp_ms != timestamp_ms) {
                    cached_prefix = monitor_timestamp_prefix(timestamp_ms);
                    cached_timestamp_ms = timestamp_ms;
                    has_cached_prefix = true;
                }
                prefix = cached_prefix;
            }
            auto row = MonitorRow{};
            row.direction = direction;
            row.timestamp_prefix = std::move(prefix);
            row.direction_label =
                direction == session::RawDirection::Received ? "RX" : "TX";
            row.dump = build_dump(bytes, bytes_per_row);
            rows.push_back(std::move(row));
        }
        ++index;
        return rows.size() < max_rows;  // 窗口折满即停止扫描（其后的行不必再看）
    };
    auto scanner = RowScanner<decltype(consume)>{bytes_per_row, consume};
    for (const auto &run : runs) {
        scanner.feed(run);
    }
    scanner.finish();
    return rows;
}

auto monitor_rows(const std::vector<session::RawRun> &runs, const MonitorRowOptions &options)
    -> std::vector<MonitorRow> {
    // 与窗口折行同一台扫描器：行边界口径只写一遍，`monitor_row_count` 与全折的行数因此恒等。
    return monitor_row_window(runs, options, 0U, std::numeric_limits<std::size_t>::max());
}

}  // namespace borealis::ui
