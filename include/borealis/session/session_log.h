#pragma once

// ============================================================
// 会话日志（include/borealis/session/session_log.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.11` 的落盘腿，形态由裁决 7.116 定形：吃**入向码点流**（与状态机同源的那一份，
// D2①），出 UTF-8 纯文本——转义族全丢、CR/LF/CRLF 各折一条换行、BS 与其余 C0 丢弃、TAB 保留（D3①）。
// 默认不记录，开启须用户逐会话显式动作（D1①）；一次开启＝一个文件；越上限**丢前半、后半前移**
// （D9①，文件始终是正常文本而非真环形）；敏感行整行替为 `[redacted]`，词表是「内置 ∪ 用户可配」
// 的并集（D10 改判）。带属性的原生存式与开关的跨重启记忆在册延后（稿 §7）。
//
// 线程口径（AGENTS §4.5 第 25 条）：**重复发生的写点只在一条线程上**——该会话自己的读线程（D5①），
// 顺序天然是到达顺序，故不需要队列也不重排。唯一的例外是「开启那一刻」：confirm、正则编译与
// 一次建文件同在 UI 线程（裁决 7.116⑤），量级是一次 `open`，不是每帧/每分片的 UI 侧 IO。
// 两条线程之间由本件自己的 `io_mutex_` 交接，**不借** `Session::mutex_`——那把锁内不做 IO（架构 §3.4）。
// ============================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

#include "borealis/vt/parser.h"

namespace borealis::session {

/// @brief 单份会话日志的字节上限（D9①）。
///
/// 取文件级常量而不是 schema 键：每枚键都是「schema + store 读写 + 搬运 + 核对表行 + 词条 +
/// 计数」六处同步，而本期没有别的消费方要读它（裁决 7.116⑥）。上限不可配、不轮转多文件在册延后。
inline constexpr std::size_t kMaxSessionLogBytes = 8U * 1024U * 1024U;

/// @brief 内置敏感词表（D10 改判吃下的那一半：缺省就该有缓解腿，不靠用户去填）。
///
/// ASCII 供替选 + `icase`，可直接在含 CJK 的 UTF-8 `std::string` 上命中（稿 §0 的 F13 实测），
/// 不需要搜索侧那套 UTF-16 转换。
inline constexpr std::string_view kBuiltinSecretPattern =
    "password|passwd|token|secret|api[_-]?key|authorization";

/// @brief 命中行的替换文本（D10①「整行命中即整行替为」，不是局部涂抹）。
inline constexpr std::string_view kRedactedLineText = "[redacted]";

/// @brief 打码判定位：内置词表 ∪ 用户模式，编译成**一个**供替选。
///
/// 并成一个而不是逐行跑两遍——读线程每行只做一次 `regex_search`（裁决 7.116⑤），且「两侧都命中
/// 同一行」天然只替一次。
class Redactor final {
  public:
    /// @brief 这一行是否命中敏感模式。
    /// @param line 折好的一行正文（不含时间戳前缀、不含行尾换行符）。
    /// @return 命中即 true，调用方把整行换成 `kRedactedLineText`。
    [[nodiscard]] auto replaces_line(std::string_view line) const -> bool;

  private:
    friend auto compile_redactor(std::string_view user_pattern) -> std::optional<Redactor>;

    /// @brief 只由 `compile_redactor` 构造：非法模式必须走不到这一步，否则「配了而失效」不可见。
    explicit Redactor(std::regex combined_pattern) : pattern_{std::move(combined_pattern)} {}

    std::regex pattern_;  ///< 内置词表与（非空时）用户模式并成的供替选，已带 `icase`。
};

/// @brief 把用户可配的敏感模式与内置词表并成一件打码器（D10 改判 + 连带的 fail-closed 档）。
///
/// 编译点**只有一次**且在开启那一刻（裁决 7.116⑤）：装载期与设置页输入框里都不编译，
/// 逐字符敲的模式不是要执行的东西。
/// @param user_pattern 用户模式（`connection.session_log_pattern`）；空串＝只用内置词表——
///        缺省空不等于「不过滤」，这一档正是为了避开「空模式折成 `()` ⇒ 匹配一切」的陷阱。
/// @return 编译好的打码器；用户模式非法时回 `std::nullopt`，调用方据此**拒绝开启**并回报错误，
///         不退回内置词表静默续记（`std::regex` 在构造期就抛，稿 §0 的 F13 实测 code=5）。
[[nodiscard]] auto compile_redactor(std::string_view user_pattern) -> std::optional<Redactor>;

/// @brief 把一段已解码码点折成纯文本（D3① 的折算表，无 IO、可全量单测，AGENTS 第 20 条）。
///
/// 表外两条口径：⑴ 转义族（`Escape`/`Csi`/`Osc`/`Dcs*`/`Ignored`）整体丢弃，一个字节都不留；
/// ⑵ CR、LF、CRLF 各折成**一条** `\n`——F10 实测真会话里 CR 与 LF 成对出现，孤立 CR（覆盖式
/// 进度条）同样折一条，代价是 `progress` 一类在日志里是一串行而非一行（判据 6 那句「记的是
/// 远端发出的行」承担）。BS 丢弃：日志不是屏幕仿真器，不模拟回退覆盖。
/// @param text 码点流（可以是任意分片；跨调用的序列接续由调用方持有的解析器负责）。
/// @return UTF-8 文本，行分隔符一律 `\n`。
[[nodiscard]] auto fold_to_plain_text(std::u32string_view text) -> std::string;

/// @brief 一次开启动作的全部入参（会话级，装配层在 UI 线程算好交进来）。
///
/// 时间戳档与上限都是**会话级参数**而不进 schema（D8①／D9①），代价同档登记：重启后回到缺省。
struct LogSpec {
    std::string path;  ///< 目标文件路径（UTF-8）；父目录由装配层保证存在，本件不代建目录。
    bool timestamp = true;  ///< 每行加 `[YYYY-MM-DD HH:MM:SS] ` 本地秒前缀（判据 6：按行不按分片）。
    std::optional<Redactor> redactor;  ///< 开启那一刻编译好的打码器（`compile_redactor` 的产物）。
    /// 越限上限。**测试注入档**：装配层一律留缺省 `kMaxSessionLogBytes`，用例靠它把「越限丢前半」
    /// 编排到几百字节上（稿 §6 的「用小上限，非常量」那条判据）。
    std::size_t max_bytes = kMaxSessionLogBytes;
};

/// @brief `Session::enable_logging` 的结果分档。
enum class LogOutcome : std::uint8_t {
    Started,        ///< 文件已就位（首建或续写既有），会话开始落盘。
    AlreadyActive,  ///< 本会话已在记录中：幂等返回，不换文件、不改既有状态。
    CannotOpen,     ///< 建不了/开不了（父目录缺失、权限不足）：会话保持未记录。
};

/// @brief 一个会话的日志落盘件：码点流 → 折算 → 打码 → 时间戳 → 越限覆盖 → 文件。
///
/// 生命周期与会话同长：`disable_logging()` 只**停笔**并 flush 末段，不销毁本件（读线程可能正在
/// 同轮 `append` 里），一次性提示也靠它留存到主线程取走。重开＝换一份新件、新文件（一次开启一个
/// 文件），旧件由 `Session` 那边的 `shared_ptr` 交接后自然析构。
class SessionLog final {
  public:
    /// @brief 按 `spec` 就位：路径不存在则以 0600 原子首建（稿 §0 的 F9），已存在则**续写不截断**。
    ///
    /// 构造期做一次文件系统 IO（裁决 7.116⑤ 的 UI 线程开启动作）；失败不抛，落到 `is_open()==false`。
    /// @param spec 入参形态。
    explicit SessionLog(LogSpec spec);

    /// @brief 析构即停笔：末段 flush 后关文件（`Session` 析构走这条，`close()` 走显式 `stop()`）。
    ~SessionLog();

    SessionLog(const SessionLog &) = delete;
    auto operator=(const SessionLog &) -> SessionLog & = delete;
    SessionLog(SessionLog &&) = delete;
    auto operator=(SessionLog &&) -> SessionLog & = delete;

    /// @brief 构造期是否把文件就位了（此后不变，故不必加锁）。
    [[nodiscard]] auto is_open() const noexcept -> bool { return opened_; }

    /// @brief 此刻是否仍在记录（未停笔、也未因写失败自我禁用）。
    ///
    /// 会话侧的「日志中／已停止」两态文案与菜单项**只读这一处**（D6①）：写失败那次禁用发生在本件
    /// 内部，若开关态另存一份就变成「菜单说在记录、文件里没新字节」的谎话。原子旗标而非复用
    /// `io_mutex_`：主线程每帧都可能读它，不该和读线程的写点抢那把锁。
    [[nodiscard]] auto is_logging() const noexcept -> bool { return logging_.load(std::memory_order_relaxed); }

    /// @brief 喂入一段入向码点并落盘（该会话读线程同轮调用，D5①）。
    ///
    /// 未就位或已停笔即空转：调用方不必先问状态，「停笔之后仍在途的那一轮」正是靠这条空转兜住。
    /// @param text 本分片的码点流（一片可含多行，行边界与分片边界不等价，判据 6）。
    auto append(std::u32string_view text) -> void;

    /// @brief 停笔：把行缓冲里最后那段不完整内容补成一行写出去，随后关文件。
    ///
    /// 幂等——已停笔或从未记录时什么都不做。
    auto stop() -> void;

    /// @brief 取走「写失败并已自我禁用」那一条一次性提示（形态照 `take_unrepresentable_notice`）。
    ///
    /// 取走即空，且每个落盘件只上弦一次；载荷只有那份文件的路径——上弦来源目前只有写失败这一档，
    /// 将来加第二档再扩成合成聚合体（AGENTS 第 3 条，不预先造没消费方的字段）。
    /// @return 待提示的路径；无待提示内容时回 `std::nullopt`。
    auto take_write_failure() -> std::optional<std::string>;

  private:
    /// @brief 把 `pending_text_` 里已完成的行逐行落盘，最后半行留在缓冲等下一片。
    auto drain_lines() -> void;

    /// @brief 一行正文的完整处置：打码 → 时间戳前缀 → 腾地 → 写。
    /// @param line 折好的正文（不含换行符）。
    auto write_line(std::string_view line) -> void;

    /// @brief 越限覆盖：读回文件、丢弃前半、把后半前移到文件头（D9①）。
    /// @return 成功后文件可继续 append；读不回或写不进即 false。
    auto roll_over() -> bool;

    /// @brief 单行本身就超上限时只留它的后半（否则 `roll_over` 无法收敛）。
    /// @param line 行入参就地裁剪。
    auto trim_to_fit(std::string &line) -> void;

    /// @brief 写侧失败的唯一出口：自我禁用 + 上弦一次性提示 + 记诊断。
    auto fail_and_disable(std::string_view where) -> void;

    /// @brief 当前本地时刻的行前缀（`timestamp_` 为真时被调用）。
    [[nodiscard]] auto timestamp_prefix() const -> std::string;

    const LogSpec spec_;  ///< 入参快照；`path` 与上限、时间戳档都在构造期定形，此后只读。
    bool opened_ = false;  ///< 构造期是否就位（此后只读）。

    mutable std::mutex io_mutex_;  ///< UI 线程的开/停与读线程的写在这一点上交接。
    /// 是否仍在记录（停笔与写失败自我禁用都落在这里）。原子而非受 `io_mutex_` 支配：
    /// 主线程读它只为两态文案，不该和读线程的写点抢锁；写侧仍在锁内改，故只需 relaxed。
    std::atomic<bool> logging_{false};
    std::size_t written_bytes_ = 0;  ///< 当前文件字节数，决定要不要越限覆盖。
    std::optional<std::string> write_failure_;  ///< 一次性提示的 latch（取走即空）。

    vt::Parser parser_;  ///< 跨分片的序列接续（一片结束于半条 CSI 时不能把裸字节当正文落盘）。
    std::string pending_text_;  ///< 已折算、尚未凑满一行的 UTF-8 正文。
    bool newline_after_cr_ = false;  ///< 上一条产出是 CR 折的换行：紧跟的 LF 不再折第二次。
    std::ofstream file_;
};

}  // namespace borealis::session
