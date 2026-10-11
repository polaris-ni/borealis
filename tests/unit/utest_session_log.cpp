/// 测试类型: unit
/// 目标单元: include/borealis/session/session_log.h + src/session/session_log.cpp
/// 测试说明: `SPEC.FEAT.CONN.11` 会话日志的纯逻辑腿与文件腿（裁决 7.116 的 D2／D3／D9／D10）——
///           折算表逐档（CRLF／LF／孤立 CR 各折一条换行、BS 丢弃、TAB 保留、其余 C0 与五类转义全丢）、
///           F12 的整屏重绘语义（记的是远端发出的行，不是屏上那一屏）、分片边界不得把转义裸字节漏进
///           正文、时间戳只在行边界插、内置词表 ∪ 用户模式的打码（含并集次序、CJK 相邻行、
///           「非法模式 ⇒ 拒绝编译」的 fail-closed 判定位）、越限丢前半留后半与不越限时的逐字节稳定、
///           写失败自我禁用 + 一次性提示的取走即空、批 2 的两枚纯派生（目录回落与文件名形态）。
///
///           这一层用真文件（`isolation::temp_dir()`）而不是替身连接：被测事实是「落盘件的字节去向」，
///           替身连接那一层归 `itest_session_logging`（稿 §4 的分工：纯折算与文件语义在 utest，
///           会话接线在 itest）。

#include "borealis/session/session_log.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_session_log {

namespace {

using borealis::session::compile_redactor;
using borealis::session::fold_to_plain_text;
using borealis::session::kMaxSessionLogBytes;
using borealis::session::kSessionLogDirectoryName;
using borealis::session::LogSpec;
using borealis::session::make_log_file_name;
using borealis::session::resolve_log_directory;
using borealis::session::SessionLog;

/// 时间戳前缀 `[%Y-%m-%d %H:%M:%S] ` 的长度：`[` + 10 + 空格 + 8 + `]` + 空格。
constexpr std::size_t kPrefixSize = 22U;

/// @brief ASCII 串折成码点流（本件的入向口径是码点，不是字节）。
[[nodiscard]] auto u32(std::string_view text) -> std::u32string {
    std::u32string out;
    for (const char c : text) {
        out.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 本轮用例专属的文件路径（temp_dir 每用例独享，同名文件不会跨用例串味）。
[[nodiscard]] auto log_path(std::string_view name) -> std::string {
    return (std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name})
        .string();
}

/// @brief 读回整份日志文件；不存在时返回空串（由断言自行判定）。
[[nodiscard]] auto read_all(const std::string &path) -> std::string {
    std::ifstream in{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// @brief 按 `\n` 切行（文件总是以换行收尾，故末尾不产生空行）。
[[nodiscard]] auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t start = 0U;
    while (start < text.size()) {
        const auto at = text.find('\n', start);
        if (at == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, at - start));
        start = at + 1U;
    }
    return lines;
}

/// @brief 起一份落盘件的入参：缺省不插时间戳、只用内置词表、上限走常量（用例按需覆盖）。
[[nodiscard]] auto make_spec(std::string_view name, std::size_t max_bytes = kMaxSessionLogBytes,
                             bool timestamp = false, std::string_view user_pattern = {})
    -> LogSpec {
    LogSpec spec;
    spec.path = log_path(name);
    spec.timestamp = timestamp;
    spec.redactor = compile_redactor(user_pattern);
    spec.max_bytes = max_bytes;
    return spec;
}

/// @brief 喂一段码点（本件唯一的写点，即读线程同轮 append 那条腿）。
auto feed(SessionLog &log, std::u32string_view text) -> void { log.append(text); }

/// @brief 喂一段 ASCII（转义序列按源码里的 `\x1B`／`\r` 写成 ASCII，逐字可读）。
auto feed(SessionLog &log, std::string_view ascii) -> void { log.append(u32(ascii)); }

/// @brief 一行确实带本地秒前缀（判据 6 的形态）。
[[nodiscard]] auto has_timestamp(std::string_view line) -> bool {
    return line.size() > kPrefixSize && line.front() == '[' && line[20] == ']' && line[21] == ' ';
}

[[nodiscard]] auto timestamp_of(std::string_view line) -> std::string_view {
    return line.substr(0U, kPrefixSize - 1U);  // 到 `]` 为止，不含尾随空格
}

}  // namespace

AURORA_TEST_CASE(crlf_lf_and_bare_cr_each_fold_into_exactly_one_line) {
    // F10 的读数是决定性的：真会话里 CR 与 LF 成对出现（CRLF 83／CR 83／LF 86），
    // 故「CRLF 折一条」必须先做，而孤立 CR（覆盖式进度条）同样折一条（D3①）。
    AURORA_TEST_CHECK_EQ(fold_to_plain_text(u32("ab\r\ncd")), std::string("ab\ncd"));
    AURORA_TEST_CHECK_EQ(fold_to_plain_text(u32("ab\ncd")), std::string("ab\ncd"));
    AURORA_TEST_CHECK_EQ(fold_to_plain_text(u32("progress\roverwritten")),
                         std::string("progress\noverwritten"));
    // 连续两个孤立 CR 是两次换行，不是「一次的加强版」。
    AURORA_TEST_CHECK_EQ(fold_to_plain_text(u32("a\r\rb")), std::string("a\n\nb"));
    // CR 之后隔着一条 SGR 再来 LF：属性不是内容，不该把一条换行拆成两条。
    AURORA_TEST_CHECK_EQ(fold_to_plain_text(u32("a\r\x1B[0m\nb")), std::string("a\nb"));
}

AURORA_TEST_CASE(escape_families_leave_not_a_single_byte_in_the_log) {
    // 需求那句「剥离 SGR 属性但保留换行结构」：SGR／OSC／ESC charset／DCS 全归零。
    const auto folded = fold_to_plain_text(
        u32("\x1B[01;34mdocs\x1B[0m  \x1B]0;title\a\x1B(B\x1B[0m done\x1BP1;2ucustom\x1B\\\x1B[?25l!"));
    AURORA_TEST_CHECK_EQ(folded, std::string("docs   done!"));
    AURORA_TEST_CHECK_EQ(folded.find('\x1B'), std::string::npos);
    AURORA_TEST_CHECK_EQ(folded.find("[0m"), std::string::npos);
    // 真彩色 SGR 与「止于流尾的半截序列」都不该漏出正文。
    AURORA_TEST_CHECK_EQ(fold_to_plain_text(u32("ok\x1B[38;2;1;2;3mrest")), std::string("okrest"));
    AURORA_TEST_CHECK_EQ(fold_to_plain_text(u32("tail\x1B[1;")), std::string("tail"));
}

AURORA_TEST_CASE(backspace_is_dropped_tab_survives_and_other_c0_goes) {
    // BS 丢弃 = 日志不模拟回退覆盖（D3①）；代价是覆盖式输出留下一串中间态行，由判据 6 那句话承担。
    const auto folded = fold_to_plain_text(u32("a\bb\tx\x07y\x7fz"));
    AURORA_TEST_CHECK_EQ(folded, std::string("ab\txyz"));
    AURORA_TEST_CHECK_EQ(folded.find('\b'), std::string::npos);
    AURORA_TEST_CHECK_EQ(folded.find('\a'), std::string::npos);
    AURORA_TEST_CHECK_EQ(folded.find('\x7F'), std::string::npos);
}

AURORA_TEST_CASE(a_full_screen_redraw_records_every_frame_the_remote_sent) {
    // F12 的裁定：「屏上 1 行／文件 N 行」是**语义**而不是缺陷——剥离档记的是远端发出的行。
    // 这条钉在用例名里，防止后人把 N 当成 bug 修掉（稿 §2 判据 6、裁决 7.116③）。
    std::string frames;
    for (int frame = 0; frame < 40; ++frame) {
        frames += "\x1B[H\x1B[2Jtop line \x1B[K";
        frames += std::to_string(frame);
        frames += "\r\n";
    }
    const auto folded = fold_to_plain_text(u32(frames));
    const auto lines = split_lines(folded);
    AURORA_TEST_CHECK_EQ(lines.size(), 40U);
    AURORA_TEST_CHECK_EQ(folded.find('\x1B'), std::string::npos);
    AURORA_TEST_CHECK_EQ(lines.front(), std::string("top line 0"));
    AURORA_TEST_CHECK_EQ(lines.back(), std::string("top line 39"));
}

AURORA_TEST_CASE(chunk_splits_never_leak_escape_bytes_into_the_body) {
    // 一片可含 3.5 行、也可止于半条 CSI（F10）。跨分片的序列接续由落盘件自己那台解析器负责，
    // 缺了它下一片开头的那半个序列会被当成正文写进文件。
    const std::string stream = "\x1B[01;34mfirst\r\nsecond \x1B[0mline\r\n";
    const std::string whole = fold_to_plain_text(u32(stream));

    SessionLog by_chunk{make_spec("split.log")};
    AURORA_TEST_REQUIRE_TRUE(by_chunk.is_open());
    for (const char c : stream) {
        feed(by_chunk, std::string_view{&c, 1U});  // 每片一字节：最坏的分片
    }
    by_chunk.stop();
    const auto lines = split_lines(read_all(log_path("split.log")));
    AURORA_TEST_REQUIRE_EQ(lines.size(), 2U);
    AURORA_TEST_CHECK_EQ(lines[0], std::string("first"));
    AURORA_TEST_CHECK_EQ(lines[1], std::string("second line"));
    AURORA_TEST_CHECK_EQ(read_all(log_path("split.log")), whole);
}

AURORA_TEST_CASE(timestamp_prefix_lands_on_line_boundaries_only) {
    // 一条分片含 3.5 行 ⇒ 前两条各有前缀、那半行既无前缀也不上盘（判据 6：分片边界≠行边界）。
    SessionLog log{make_spec("timestamp.log", kMaxSessionLogBytes, true)};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    feed(log, "alpha\r\nbeta\n");
    const auto after_first = split_lines(read_all(log_path("timestamp.log")));
    AURORA_TEST_REQUIRE_EQ(after_first.size(), 2U);
    AURORA_TEST_CHECK_TRUE(has_timestamp(after_first[0]));
    AURORA_TEST_CHECK_TRUE(has_timestamp(after_first[1]));
    AURORA_TEST_CHECK_EQ(after_first[0].substr(kPrefixSize), std::string("alpha"));

    feed(log, "gamma");  // 末段没有换行符：留在行缓冲里，不该带前缀上盘
    AURORA_TEST_CHECK_EQ(split_lines(read_all(log_path("timestamp.log"))).size(), 2U);

    log.stop();  // 停笔时末段补成一行，前缀在这一刻才插
    const auto lines = split_lines(read_all(log_path("timestamp.log")));
    AURORA_TEST_REQUIRE_EQ(lines.size(), 3U);
    AURORA_TEST_CHECK_TRUE(has_timestamp(lines[2]));
    AURORA_TEST_CHECK_EQ(lines[2].substr(kPrefixSize), std::string("gamma"));
}

AURORA_TEST_CASE(builtin_vocabulary_redacts_the_whole_line) {
    SessionLog log{make_spec("redact.log")};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    feed(log, "please type your Password: \r\nnothing sensitive here\r\n");
    log.stop();
    const auto lines = split_lines(read_all(log_path("redact.log")));
    AURORA_TEST_REQUIRE_EQ(lines.size(), 2U);
    // 整行替换（D10①），不是把命中的词涂掉——半涂会把上下文留在盘上。
    AURORA_TEST_CHECK_EQ(lines[0], std::string("[redacted]"));
    AURORA_TEST_CHECK_EQ(lines[1], std::string("nothing sensitive here"));
}

AURORA_TEST_CASE(redaction_covers_every_builtin_word_and_cjk_neighbors) {
    // 词表六条逐个过（icase 两档都试），末条是 CJK 相邻行：F13 的第一条读数——ASCII 供替选
    // 在含 CJK 的 UTF-8 行上直接命中，不需要搜索侧那套 UTF-16 转换。
    SessionLog log{make_spec("hits.log")};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    feed(log, "password\r\nPASSWD\r\nTOKEN\r\na secret here\r\nAPI_KEY=1\r\nAuthorization: Bearer\r\n");
    feed(log, U"\u53e3\u4ee4 password \u5728\u6b64\r\n");
    log.stop();
    const auto lines = split_lines(read_all(log_path("hits.log")));
    AURORA_TEST_REQUIRE_EQ(lines.size(), 7U);
    for (const auto &line : lines) {
        AURORA_TEST_CHECK_EQ(line, std::string("[redacted]"));
    }
}

AURORA_TEST_CASE(unmatched_lines_survive_byte_for_byte_including_utf8) {
    // 打码不是改写：不含关键词的行必须逐字节原样，含多字节 UTF-8 的那一行也不能被折损。
    SessionLog log{make_spec("untouched.log")};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    feed(log, U"\u53e3\u4ee4\u6b64\u5904 ok\r\nplain ascii\r\n");
    log.stop();
    // 口令此处 = E5 8F A3 / E4 BB A4 / E6 AD A4 / E5 A4 84，后跟 " ok"。
    AURORA_TEST_CHECK_EQ(read_all(log_path("untouched.log")),
                         std::string("\xE5\x8F\xA3\xE4\xBB\xA4\xE6\xAD\xA4\xE5\xA4\x84 ok\nplain ascii\n"));
}

AURORA_TEST_CASE(user_pattern_adds_to_the_builtin_vocabulary_instead_of_replacing_it) {
    // D10 改判的核心：并集不是装饰——只有用户模式命中的行也得打码。
    SessionLog log{make_spec("union.log", kMaxSessionLogBytes, false, "invoice[-_]no")};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    feed(log, "ref invoice-no-4711 ready\r\ntoken abc\r\nbenign line\r\n");
    log.stop();
    const auto lines = split_lines(read_all(log_path("union.log")));
    AURORA_TEST_REQUIRE_EQ(lines.size(), 3U);
    AURORA_TEST_CHECK_EQ(lines[0], std::string("[redacted]"));  // 只有用户模式命中
    AURORA_TEST_CHECK_EQ(lines[1], std::string("[redacted]"));  // 只有内置词表命中
    AURORA_TEST_CHECK_EQ(lines[2], std::string("benign line"));  // 都不命中 ⇒ 逐字节不变
}

AURORA_TEST_CASE(a_line_hit_from_both_sides_is_redacted_once) {
    // 并集次序的用例（裁决 7.116④）：两侧同命中不得叠成两个 `[redacted]`——并成一个供替选
    // 正是为了让这条天然成立。
    SessionLog log{make_spec("once.log", kMaxSessionLogBytes, false, "password")};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    feed(log, "password password\r\n");
    log.stop();
    AURORA_TEST_CHECK_EQ(read_all(log_path("once.log")), std::string("[redacted]\n"));
}

AURORA_TEST_CASE(illegal_user_pattern_refuses_to_compile) {
    // fail-closed 的判定位（F13 实测 `std::regex` 在构造期就抛 regex_error，code=5）。
    AURORA_TEST_CHECK_FALSE(compile_redactor("(password|token").has_value());
    AURORA_TEST_CHECK_FALSE(compile_redactor("[z-a]").has_value());
    AURORA_TEST_CHECK_FALSE(compile_redactor(")").has_value());
    AURORA_TEST_CHECK_TRUE(compile_redactor("in\\\\voice").has_value());
    // 用户模式必须整条括起来：`x|` 这种尾部分支若拼在同一层供替选上，会让内置的末词吞掉用户的
    // 首分支；括起来之后这条天然成立。
    AURORA_TEST_CHECK_TRUE(compile_redactor("^boom$").has_value());
    // 「非法模式 ⇒ 不建文件、不改开关态」的界面侧半段归装配层（`src/main.cpp` 不编入 runner，
    // 裁决 7.83），本用例锁的是它唯一的可编程判定位。
}

AURORA_TEST_CASE(empty_user_pattern_keeps_the_builtin_vocabulary_only) {
    // 缺省空**不等于**「不过滤」，也不许折成 `()` 那种匹配一切的东西（D10 改判的理由）。
    auto redactor = compile_redactor({});
    AURORA_TEST_REQUIRE_TRUE(redactor.has_value());
    AURORA_TEST_CHECK_FALSE(redactor->replaces_line("an ordinary line"));
    AURORA_TEST_CHECK_FALSE(redactor->replaces_line(""));
    AURORA_TEST_CHECK_TRUE(redactor->replaces_line("your token expires"));
}

AURORA_TEST_CASE(over_the_limit_the_oldest_half_is_dropped_and_the_tail_moves_up) {
    // D9①：丢前半、后半前移，且切点落到换行之后 ⇒ 文件头永远是一条完整的行。
    constexpr std::size_t kLimit = 400U;
    SessionLog log{make_spec("roll.log", kLimit, true)};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    for (int index = 0; index < 40; ++index) {
        feed(log, std::string("line-") + std::to_string(index) + "\r\n");
        const auto size = std::filesystem::file_size(std::filesystem::path{log_path("roll.log")});
        AURORA_TEST_CHECK_LE(size, kLimit);  // 越限是在每一行之前处理的，不会先把文件撑爆
    }
    log.stop();

    const std::string content = read_all(log_path("roll.log"));
    AURORA_TEST_CHECK_LE(static_cast<std::size_t>(content.size()), kLimit);
    const auto lines = split_lines(content);
    AURORA_TEST_REQUIRE_FALSE(lines.empty());
    // 最旧的确实消失了、最新的还在——「覆盖最旧的」而不是「越限即停」。
    AURORA_TEST_CHECK_TRUE(lines.back().find("line-39") != std::string::npos);
    AURORA_TEST_CHECK_TRUE(content.find("line-0\n") == std::string::npos);
    std::string previous;
    for (const auto &line : lines) {
        AURORA_TEST_REQUIRE_TRUE(has_timestamp(line));
        // 界两侧时刻仍单调（判据 6）：定长的 ISO 形态前缀可直接按字典序比。
        AURORA_TEST_CHECK_TRUE(line.find("] line-") != std::string::npos);  // 没有半行开头
        if (!previous.empty()) {
            AURORA_TEST_CHECK(previous.compare(std::string{timestamp_of(line)}) <= 0);
        }
        previous = std::string{timestamp_of(line)};
    }
}

AURORA_TEST_CASE(an_oversized_single_line_keeps_only_its_tail) {
    // 远端一行超长输出（`cat` 一个没有换行的文件）能一口气顶穿上限：不裁这一条，越限覆盖无从收敛。
    SessionLog log{make_spec("huge.log", 100U, false)};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    feed(log, std::string(5000U, 'z') + "\r\n");
    log.stop();
    const std::string content = read_all(log_path("huge.log"));
    AURORA_TEST_CHECK_EQ(static_cast<std::size_t>(content.size()), 100U);
    AURORA_TEST_CHECK_EQ(content, std::string(99U, 'z') + '\n');  // 留的是尾部，不是头部
}

AURORA_TEST_CASE(under_the_limit_the_file_is_byte_for_byte_stable) {
    SessionLog log{make_spec("stable.log", 4096U, false)};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    feed(log, "one\r\ntwo\r\n");
    log.stop();
    AURORA_TEST_CHECK_EQ(read_all(log_path("stable.log")), std::string("one\ntwo\n"));
}

AURORA_TEST_CASE(write_failure_disables_logging_and_arms_a_one_shot_notice) {
    // 造法：把文件从目录里摘掉，越限时那次「读后半」必然失败 ⇒ 走 fail_and_disable 那条腿。
    // 写失败之后不再拖该会话的读线程（D5① 的代价面之一），提示每个落盘件只上弦一次（照 F7 同形）。
    SessionLog log{make_spec("broken.log", 40U, false)};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path{log_path("broken.log")}, ec);

    for (int index = 0; index < 20; ++index) {
        feed(log, std::string("line-") + std::to_string(index) + "\r\n");
    }
    const auto notice = log.take_write_failure();
    AURORA_TEST_CHECK_TRUE(notice.has_value());
    if (notice) {
        AURORA_TEST_CHECK_EQ(*notice, log_path("broken.log"));
    }
    AURORA_TEST_CHECK_FALSE(log.take_write_failure().has_value());  // 取走即空，不重弹
    // 自我禁用的**现读态**：主线程的两态文案读的正是这一处，它撒谎就是「菜单说在记录、文件没新字节」。
    AURORA_TEST_CHECK_FALSE(log.is_logging());

    feed(log, "after disable\r\n");  // 自我禁用之后一律空转
    AURORA_TEST_CHECK_EQ(read_all(log_path("broken.log")).find("after disable"), std::string::npos);
}

AURORA_TEST_CASE(a_missing_parent_directory_leaves_the_log_closed_and_creates_nothing) {
    const auto directory = std::filesystem::path{aurora::testing::isolation::temp_dir()} / "no_such_dir";
    LogSpec spec;
    spec.path = (directory / "orphan.log").string();
    spec.redactor = compile_redactor({});
    SessionLog log{std::move(spec)};
    AURORA_TEST_CHECK_FALSE(log.is_open());
    feed(log, "anything\r\n");
    log.stop();
    AURORA_TEST_CHECK_FALSE(std::filesystem::exists(directory));  // 「取消 ⇒ 什么都不创建」的地基
}

AURORA_TEST_CASE(an_empty_append_is_a_no_op) {
    SessionLog log{make_spec("empty.log")};
    AURORA_TEST_REQUIRE_TRUE(log.is_open());
    log.append({});
    log.stop();
    AURORA_TEST_CHECK_EQ(read_all(log_path("empty.log")), std::string());
}

AURORA_TEST_CASE(stop_is_idempotent_and_a_reopened_log_appends_instead_of_truncating) {
    // 「0600 与不覆盖」的另一半：同一份路径第二次开启必须是续写（稿 §6 那条判据的会话侧）。
    const std::string path = log_path("reopen.log");
    {
        SessionLog first{make_spec("reopen.log")};
        AURORA_TEST_REQUIRE_TRUE(first.is_open());
        AURORA_TEST_CHECK_TRUE(first.is_logging());  // 就位即记录：主线程的两态文案读这一处
        feed(first, "kept\r\n");
        first.stop();
        AURORA_TEST_CHECK_FALSE(first.is_logging());
        first.stop();  // 幂等：第二次连末段补行都不做
        feed(first, "after stop\r\n");               // 停笔之后仍在途的那一轮空转
    }
    AURORA_TEST_CHECK_EQ(read_all(path), std::string("kept\n"));
    SessionLog second{make_spec("reopen.log")};
    AURORA_TEST_REQUIRE_TRUE(second.is_open());
    feed(second, "appended\r\n");
    second.stop();
    AURORA_TEST_CHECK_EQ(read_all(path), std::string("kept\nappended\n"));
}

AURORA_TEST_CASE(an_empty_configured_directory_falls_back_to_the_named_child_of_config_dir) {
    // 判据 7 的回落腿：留空＝配置目录下一层带归属的名字，非空＝原样（不拼不洗）。两条都空没有落点，
    // 装配层据此拒绝开启——宁可拒绝也不猜一个当前目录，那会把日志写进用户找不到的地方。
    const std::string config_dir{aurora::testing::isolation::temp_dir()};
    AURORA_TEST_CHECK_EQ(resolve_log_directory({}, config_dir),
                         (std::filesystem::path{config_dir} / std::string{kSessionLogDirectoryName}).string());
    AURORA_TEST_CHECK_EQ(resolve_log_directory(config_dir, config_dir), config_dir);
    AURORA_TEST_CHECK_EQ(resolve_log_directory({}, {}), std::string{});
}

AURORA_TEST_CASE(the_log_file_name_carries_tab_id_and_start_second_only) {
    // D13①：名字里只有 TabId 与起始 epoch 秒——标签名/档案名进来就要洗名字，那是本该待在
    // `src/platform/` 的平台知识。这条用例把形态钉死，防止后人「顺手」加一段可读名。
    AURORA_TEST_CHECK_EQ(make_log_file_name(7U, 1766448000LL), std::string{"session-7-1766448000.log"});
    AURORA_TEST_CHECK_EQ(make_log_file_name(0U, 0LL), std::string{"session-0-0.log"});
}

}  // namespace borealis::test_cases::utest_session_log
