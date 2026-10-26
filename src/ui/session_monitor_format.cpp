// ============================================================
// 字节监视卡的行折算纯逻辑件实现（src/ui/session_monitor_format.cpp）
// ------------------------------------------------------------
// 两个纯函数：`monitor_rows`（run 表 → 行表）与 `monitor_timestamp_prefix`（毫秒 → 前缀）。
// 行形态的算术写在头注与稿 §2，这里只兑现它；断言住在 `utest_session_monitor_format`。
//
// 断点状态机只有一枚旗标 `awaiting_lf`：CR 收口一条行但**先不落笔**，等下一个字节判它是
// CRLF（同一条断点，一个换行）还是新行的开头。这与 `session::fold_to_plain_text` 里那枚
// `newline_after_cr_` 同形（裁决 7.116 D3①），两处都源于 F10 那条实测：真流里 CR 与 LF 成对是常态。
//
// 四期（稿 §11）加的是**行内容**的第二种形态而不是第二台机器：`RowScanner` 只认字节，
// 文本档换的只是「一段字节 → `dump`」那一步（`build_dump` ↔ 解码后折文本）与每行的字节预算。
// ============================================================

#include "session_monitor_format.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "borealis/term/codec.h"
#include "borealis/term/utf8.h"

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

/// @brief 文本档的码点接收端：码点 → 追加进 `text`（稿 §11 D29 的三条形态口径只写在这里）。
///
/// 判定吃**码点值**而不是字节值：解码之后才有「这一格是不是一条控制码」这个问题可言，同一份字节在
/// GB18030 里是汉字、在 UTF-8 里可能是非法序列，按字节判会把中文档整屏折成点。
class RowTextSink final : public term::CodePointSink {
  public:
    auto on_code_point(char32_t cp) -> void override {
        if (cp == U'\r' || cp == U'\n') {
            return;  // 断点归 `RowScanner` 判：跟着行尾进来的那两个字节在文本面上不出现（D29）
        }
        if (cp <= 0x1FU || (cp >= 0x7FU && cp <= 0x9FU)) {
            text += '.';  // C0／DEL／C1 与十六进制尾栏同一条口径（D29①）
            return;
        }
        // 含 U+FFFD：编码不匹配是真信号不是噪声，原样上屏（走查 §11.6 第 4 条读的就是它）。
        static_cast<void>(term::append_utf8(cp, text));
    }

    std::string text{};  ///< 已折出的那一段文本（`dump` 的前导两空格由调用方补）。
};

/// @brief 只吞不吐的接收端：窗口之前的字节要过解码器推状态，但不建字符串。
class DiscardSink final : public term::CodePointSink {
  public:
    auto on_code_point(char32_t) -> void override {}
};

/// @brief 选项里的一行字节预算：hex 档吃 `bytes_per_row`（0 档回落缺省），文本档吃固定预算。
///
/// 文本档刻意**按字节**而不是按码点切（稿 D27）：`monitor_row_count` 因此不需要解码就能给出真实总行数，
/// 两档共用一台扫描器这条纪律才不破——按码点切就必须先解完整段才知道行落在哪。
[[nodiscard]] auto effective_bytes_per_row(const MonitorRowOptions &options) -> std::size_t {
    if (options.view == MonitorView::Text) {
        return kMonitorTextRowBytes;
    }
    return options.bytes_per_row == 0U ? kMonitorBytesPerRow : options.bytes_per_row;
}

/// @brief 从 `from` 起找最近的 CR 或 LF（找不到就是 `size`）。
///
/// 用 `memchr` 而不是逐字节比较：二进制段（整段没有行尾）是本卡最常见的料，
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

    /// @brief 消费者是否说了「停」（窗口折满即真）。
    ///
    /// 文本档据此判要不要给解码器收尾：只有**扫到料源尽头**的那一次才有「末尾挂着半字序列」这件事，
    /// 中途停扫的那份料源本来就没折完，收尾反而会在窗口末行多造一个替换字符。
    [[nodiscard]] auto aborted() const noexcept -> bool { return aborted_; }

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
    const bool text_view = options.view == MonitorView::Text;
    // 每份料源新建一枚解码器，**不复用会话那只活的 `decoder_`**（稿 D28）：它与读线程共锁、状态已被
    // 线上流消耗，把显示挂在解码进度上会让两次重建给出不同结果。`make_session_decoder` 恒回非空
    // （映射不到回落 UTF-8 腿），故下面的解引用不需要判空。代价是每次重建开合一次 iconv 句柄
    // （实测 ~160 ns，见 `codec.h` 发送侧那条同族口径），缺省 UTF-8 腿连句柄都不开。
    auto decoder = text_view ? term::make_session_decoder(options.encoding) : nullptr;
    auto discard_sink = DiscardSink{};
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
        const bool in_window = index >= first_row && rows.size() < max_rows;
        if (text_view && !in_window) {
            // 窗口之前的字节仍要过一遍解码器：多字节序列可能被字节预算切在行界上，跳过那几行就等于
            // 让窗口首行少半个字（稿 D31 认下的那条成本，hex 档没有它）。
            decoder->feed(bytes, discard_sink);
        }
        if (in_window) {
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
            if (text_view) {
                auto sink = RowTextSink{};
                decoder->feed(bytes, sink);
                row.dump = "  ";
                row.dump += std::move(sink.text);
            } else {
                row.dump = build_dump(bytes, bytes_per_row);
            }
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
    if (text_view && !scanner.aborted() && !rows.empty()) {
        // 料源末尾那段挂起的半字序列按替换字符收尾，落在最后一行上：头注口径 ⑵ 在文本面的落地——
        // 环里存着却不显示的字节等于说谎。收不住的情形只有「中途停扫」，那一支上面已排除。
        auto sink = RowTextSink{};
        decoder->finish(sink);
        rows.back().dump += std::move(sink.text);
    }
    return rows;
}

auto monitor_rows(const std::vector<session::RawRun> &runs, const MonitorRowOptions &options)
    -> std::vector<MonitorRow> {
    // 与窗口折行同一台扫描器：行边界口径只写一遍，`monitor_row_count` 与全折的行数因此恒等。
    return monitor_row_window(runs, options, 0U, std::numeric_limits<std::size_t>::max());
}

auto monitor_dump_text(const std::vector<session::RawRun> &runs, const MonitorRowOptions &options)
    -> std::string {
    // 交出去的文本＝屏面文本的拼接，一行不多一行不少（稿 §10.2 D19①）。这里**不**分块折窗口：
    // `monitor_row_window` 每次都要从头扫，按窗口循环会把一次导出变成 O(行数²/窗口)；全折一次的峰值
    // 是满环那 ≈1.8 MB 行文本（F7 换算）加同量拼接串，发生在一次显式点击而不是热路径，可接受。
    const auto rows = monitor_rows(runs, options);
    auto out = std::string{};
    // 每行 56 字符＝十六进制缺省档（稿 §2 的算术）+ 分隔符；文本档一行可到百余字符，但这一格只是
    // 少几次扩容的提示，按哪个档都不影响结果。
    out.reserve(rows.size() * 56U);
    auto first = true;
    for (const auto &row : rows) {
        // 分隔符按「是不是第一条」判，而不是按「串目前空不空」判：`row_text()` 现在恒非空，但那条
        // 事实不是本件的前提，行形态若哪天允许空串，第一条的空行不该被吞掉。
        if (!first) {
            out.push_back('\n');
        }
        first = false;
        out += row_text(row);
    }
    return out;
}

}  // namespace borealis::ui
