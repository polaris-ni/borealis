// ============================================================
// 会话日志（src/session/session_log.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.11` 的落盘腿，需求那句「纯文本（剥离 SGR 属性但保留换行结构）」在这里兑现：
// 折算表住在 `fold_to_plain_text`（纯函数，AGENTS 第 20 条要的「纯逻辑层可独立单测」），
// 行缓冲 / 打码 / 时间戳 / 越限覆盖 / 一次性提示住在 `SessionLog`。
//
// 两条纪律决定了形态：
//   ⑴ 写点只有一个（`append`/`stop`），且重复发生的那条只在**该会话自己的读线程**上（D5①）——
//      顺序天然是到达顺序，不需要队列也不需要重排；有界队列那两个现成先例都是**丢弃型**，
//      而日志丢一行比慢一点严重得多。
//   ⑵ `Session::mutex_` 的临界区内不做 IO（架构 §3.4），故本件自带 `io_mutex_`，
//      UI 线程的开/停与读线程的写在这把锁上交接。
// ============================================================

#include "borealis/session/session_log.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <utility>

#include "aurora/core/log.h"
#include "borealis/term/utf8.h"
#include "platform/file_secure.h"  // 0600 原子首建那条腿（F9）；平台知识不外溢（第 23 条）

namespace borealis::session {

namespace {

/// @brief 折算的接收端：把语义单元落成 UTF-8 正文，转义族一个字节都不留。
///
/// WHY 输出与 CR/LF 合并位都在**调用方**而非本件内：纯函数版用局部变量，落盘件用成员——
/// 同一张表两处跑，分表就会分叉（AGENTS 第 3 条不加抽象，第 12 条要求口径只有一份）。
class FoldSink final : public vt::SequenceSink {
  public:
    /// @param out 折算产物缓冲（追加写，UTF-8，行分隔符一律 `\n`）。
    /// @param newline_after_cr 上一条产出是否为 CR 折出的换行（跨分片留存）。
    FoldSink(std::string &out, bool &newline_after_cr) : out_{out}, newline_after_cr_{newline_after_cr} {}

    auto on_sequence(const vt::Sequence &seq) -> void override {
        switch (seq.kind) {
            case vt::SequenceKind::Print:
                newline_after_cr_ = false;
                static_cast<void>(term::append_utf8(seq.code_point, out_));
                return;
            case vt::SequenceKind::Execute:
                handle_control(seq.code_point);
                return;
            case vt::SequenceKind::Escape:
            case vt::SequenceKind::Csi:
            case vt::SequenceKind::Osc:
            case vt::SequenceKind::DcsHook:
            case vt::SequenceKind::DcsPut:
            case vt::SequenceKind::DcsUnhook:
            case vt::SequenceKind::Ignored:
                return;  // D3①：转义族全丢。SGR 是属性不是内容，剥掉它们才叫「纯文本」。
        }
    }

  private:
    /// @brief C0/C1 的处置表（D3①）：TAB 留、CR/LF 各折一条换行、BS 与其余控制码丢弃。
    auto handle_control(char32_t cp) -> void {
        switch (cp) {
            case U'\t':
                newline_after_cr_ = false;
                out_.push_back('\t');
                return;
            case U'\r':
                // 孤立 CR 也折一条换行（覆盖式进度条每帧一行，可 grep）；CRLF 的合并位交给
                // newline_after_cr_，紧跟的 LF 不再折第二次。
                out_.push_back('\n');
                newline_after_cr_ = true;
                return;
            case U'\n':
                if (newline_after_cr_) {
                    newline_after_cr_ = false;  // CRLF 是一条换行，不是两条（F10：两者成对出现）
                    return;
                }
                out_.push_back('\n');
                return;
            case U'\b':
                return;  // 丢弃：日志不是屏幕仿真器，模拟回退覆盖等于再造一台状态机
            default:
                return;  // 其余 C0 与 C1（含 DEL）一律丢弃
        }
    }

    std::string &out_;
    bool &newline_after_cr_;
};

}  // namespace

auto Redactor::replaces_line(std::string_view line) const -> bool {
    // `.` 不跨 `\r`（F13 实测），而行已经按 D3 折成不含控制码的正文，故行内匹配天然安全。
    return std::regex_search(line.cbegin(), line.cend(), pattern_);
}

auto compile_redactor(std::string_view user_pattern) -> std::optional<Redactor> {
    std::string source{kBuiltinSecretPattern};
    if (!user_pattern.empty()) {
        // 用户模式必须整条括起来并：直接拼 `a|b|` + 用户的 `x|y` 会让内置的最后一个词吞掉用户的
        // 第一个分支，且用户的 `^` 锚点会失位。括起来才是「并集」而不是「续在同一层供替选上」。
        source += "|(";
        source.append(user_pattern);
        source += ')';
    }
    try {
        return Redactor{std::regex{source, std::regex::icase}};
    } catch (const std::regex_error &error) {
        // 非法模式不回退成内置词表：过滤是缓解腿，「用户显式配了而失效」必须可见（裁决 7.116⑤）。
        AURORA_LOG_WARN("session", "session log pattern rejected: ", std::string_view{user_pattern},
                        " - ", error.what());
        return std::nullopt;
    }
}

auto fold_to_plain_text(std::u32string_view text) -> std::string {
    std::string out;
    bool newline_after_cr = false;
    vt::Parser parser;  // 局部一件：纯函数的输入是「一整段」，不需要跨调用接续
    FoldSink sink{out, newline_after_cr};
    parser.feed(text, sink);
    return out;
}

auto resolve_log_directory(std::string_view configured_dir, std::string_view config_dir) -> std::string {
    // 非空即原样用：用户写的绝对路径不拼、不规范化、不判相对——相对路径按进程当前目录解释，那是
    // 宿主的口径而不是本件该猜的事。
    if (!configured_dir.empty()) {
        return std::string{configured_dir};
    }
    if (config_dir.empty()) {
        return {};  // 连配置目录都取不到 ⇒ 没有落点，交给装配层拒绝开启
    }
    return (std::filesystem::path{std::string{config_dir}} / std::string{kSessionLogDirectoryName}).string();
}

auto make_log_file_name(std::uint64_t tab_id, std::int64_t start_epoch_seconds) -> std::string {
    return "session-" + std::to_string(tab_id) + "-" + std::to_string(start_epoch_seconds) + ".log";
}

SessionLog::SessionLog(LogSpec spec) : spec_{std::move(spec)} {
    switch (platform::create_private_file(spec_.path)) {
        case platform::PrivateFileOutcome::Created:
            break;
        case platform::PrivateFileOutcome::AlreadyExists:
            // 续写而不是截断：一次开启一个文件，同名只可能是「同一秒里重开」或上一次留下的旧文件，
            // 覆盖别人的日志不是本件的权利（稿 §6「0600 与不覆盖」那条判据）。
            break;
        case platform::PrivateFileOutcome::Error:
            return;  // 父目录不存在、权限不足——调用方据 `is_open()` 返回 CannotOpen
    }
    file_.open(spec_.path, std::ios::app | std::ios::binary);
    if (!file_.is_open()) {
        return;
    }
    std::ifstream existing{spec_.path, std::ios::binary | std::ios::ate};
    written_bytes_ = existing ? static_cast<std::size_t>(existing.tellg()) : 0U;
    opened_ = true;
    logging_ = true;
}

SessionLog::~SessionLog() { stop(); }

auto SessionLog::append(std::u32string_view text) -> void {
    if (text.empty()) {
        return;
    }
    const std::lock_guard lock{io_mutex_};
    if (!opened_ || !logging_) {
        return;  // 停笔之后仍在途的那一轮（读线程拷码点与 UI 置位交错）在这里空转
    }
    FoldSink sink{pending_text_, newline_after_cr_};
    parser_.feed(text, sink);  // 跨分片的半条序列由成员解析器接着吃，裸字节不会漏进正文
    drain_lines();
    if (logging_) {
        static_cast<void>(file_.flush());  // 每片一次：日志的价值在崩溃后还在，F14 量过这一档的成本
        if (file_.fail()) {
            fail_and_disable("flush");
        }
    }
}

auto SessionLog::stop() -> void {
    std::lock_guard lock{io_mutex_};
    if (!opened_ || !logging_) {
        return;
    }
    logging_ = false;
    if (!pending_text_.empty()) {
        // 末段没有换行符也要成行——「关闭标签 ⇒ 末段已 flush 完整」这条判据要的是内容不缺席。
        const auto line = std::move(pending_text_);
        pending_text_.clear();
        write_line(line);
    }
    static_cast<void>(file_.flush());
    file_.close();
}

auto SessionLog::take_write_failure() -> std::optional<std::string> {
    const std::lock_guard lock{io_mutex_};
    auto notice = std::move(write_failure_);
    write_failure_.reset();
    return notice;
}

auto SessionLog::drain_lines() -> void {
    std::size_t start = 0U;
    while (true) {
        const auto break_at = pending_text_.find('\n', start);
        if (break_at == std::string::npos) {
            break;
        }
        write_line(std::string_view{pending_text_}.substr(start, break_at - start));
        start = break_at + 1U;
    }
    pending_text_.erase(0U, start);  // 只有凑满一行的才落盘，最后半行等下一片（判据 6）
}

auto SessionLog::write_line(std::string_view line) -> void {
    std::string payload;
    if (spec_.redactor && spec_.redactor->replaces_line(line)) {
        payload.assign(kRedactedLineText);  // 整行替换，不做局部涂抹（D10①）
    } else {
        payload.assign(line);
    }
    if (spec_.timestamp) {
        payload.insert(0U, timestamp_prefix());
    }
    trim_to_fit(payload);
    if (written_bytes_ + payload.size() + 1U > spec_.max_bytes && !roll_over()) {
        fail_and_disable("roll over");
        return;
    }
    file_ << payload << '\n';
    if (file_.fail()) {
        fail_and_disable("write");
        return;
    }
    written_bytes_ += payload.size() + 1U;
}

auto SessionLog::trim_to_fit(std::string &line) -> void {
    // 单行（远端一行超长输出，或整屏重绘攒出的一坨）就能顶穿上限：不裁它，roll_over 无从收敛。
    // 留 1 字节给行尾换行符，越限判据因此是「文件 ≤ 上限」而不是「≤ 上限 + 1」。
    const std::size_t budget = spec_.max_bytes > 1U ? spec_.max_bytes - 1U : 0U;
    if (line.size() <= budget) {
        return;
    }
    const std::size_t cut = line.size() - budget;
    const auto keep_from = line.find('\n', cut);  // 行内不该有 \n，找到也只会是空行边界
    line.erase(0U, keep_from == std::string::npos ? cut : keep_from + 1U);
}

auto SessionLog::roll_over() -> bool {
    file_.close();
    std::string content;
    {
        std::ifstream in{spec_.path, std::ios::binary};
        if (!in) {
            return false;
        }
        content.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }
    // 丢前半、留后半，且切点落到换行之后——文件头不能是半行，否则「正常文本」这条卖点就没了。
    const std::size_t half = content.size() / 2U;
    const auto seam = content.find('\n', half);
    const auto tail = seam == std::string::npos ? std::string_view{content}.substr(half)
                                                : std::string_view{content}.substr(seam + 1U);
    {
        std::ofstream out{spec_.path, std::ios::binary | std::ios::trunc};
        if (!out) {
            return false;
        }
        out.write(tail.data(), static_cast<std::streamsize>(tail.size()));
        if (out.fail()) {
            return false;
        }
    }
    written_bytes_ = tail.size();
    file_.open(spec_.path, std::ios::app | std::ios::binary);
    return file_.is_open();
}

auto SessionLog::fail_and_disable(std::string_view where) -> void {
    logging_ = false;  // 自我禁用：一次写失败之后不再拖该会话的读线程
    file_.close();
    if (!write_failure_) {
        write_failure_ = spec_.path;  // 一次性提示的上弦（形态照 notice_armed_，取走即空）
    }
    AURORA_LOG_WARN("session", "session log disabled after write failure at: ", where, " - ",
                    spec_.path);
}

auto SessionLog::timestamp_prefix() const -> std::string {
    const auto now = std::chrono::system_clock::now();
    const std::time_t moment = std::chrono::system_clock::to_time_t(now);
    std::tm broken{};
#if defined(_WIN32)
    // WHY 不用 std::localtime：它返回进程级共享静态缓冲，多个会话的读线程会互相改写；
    // localtime_s 是 <ctime> 的既定可重入形态（同 `src/ui/sftp_format.cpp` 那条口径——
    // 这是时刻格式化而非平台子系统，AGENTS §4.5 第 23 条的边界针对 PTY/串口/传输那一族）。
    localtime_s(&broken, &moment);
#else
    localtime_r(&moment, &broken);
#endif
    char stamp[32]{};
    const auto written = std::strftime(stamp, sizeof(stamp), "[%Y-%m-%d %H:%M:%S] ", &broken);
    return std::string{stamp, written};
}

}  // namespace borealis::session
