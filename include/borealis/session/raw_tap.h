#pragma once

// ============================================================
// 原始字节留痕环（include/borealis/session/raw_tap.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 那条延后子项「可选十六进制收发视图」的**采集腿**，形态由设计稿
// `codespec/UI_SERIAL_MONITOR.draft.md` 的 D2①／D3①／D4① 定形：环里存**原始 run**
// （一个方向 + 一个时刻 + 一段字节），不存折好的行文本——稿 §0 的 F7 实测 1 MiB 设备流量
// 折成行文本是 5 MB，而同样 1 MiB 原始字节还是 1 MiB；行文本只在面板重建那一刻存在。
//
// 采样点只有两处，且都在 `Session`（D2①）：RX 取 `ingest` 出锁后那一份原始字节，TX 取
// `write_out` **行尾折算之后**那一份。故本件既不解析协议、也不认识编码与行尾：它只是
// 「线上此刻跑过哪些字节」的一份有界留存。
//
// 线程口径（AGENTS §4.5 第 25 条，与 `SessionLog` 同形）：本件**自带** `mutex_`，
// 不借 `Session::mutex_`——那把锁内不做拷贝以外的耗时事（架构 §3.4）。调用方必须在
// **不持** `Session::mutex_` 的上下文里调 `record()`（稿 §5 第一条硬约束）。
//
// 不持久化（稿 §1 那条安全口径）：环只在内存，不进会话日志文件、不进崩溃转储、不进配置
// 目录——设备流量可能含口令回显。代价如实登记：会话结束即消失，「回看」只在这条会话活着时成立。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <vector>

namespace borealis::session {

/// @brief 环的字节上限（D4①：256 KiB ≈ 32 KiB 读块 × 8 次，够看一整屏报文）。
///
/// 取文件级常量而不是 schema 键（F13：`kMaxSessionLogBytes` 那条先例）——每枚键都是
/// 「schema + store 读写 + 搬运 + 核对表行 + 词条 + 计数」六处同步，而本件没有一个
/// 「跨会话一致」的需求方在等它。这个数**同时就是** `snapshot()` 的交出上界，不留第二个数。
inline constexpr std::size_t kMaxRawTapBytes = 256U * 1024U;

/// @brief 一条 run 的方向（稿 §2：视图里 RX／TX 是两个字母，不是两句中文）。
enum class RawDirection : std::uint8_t {
    Received,     ///< 入向：该会话读线程交给 `Session::ingest` 的那一份字节。
    Transmitted,  ///< 出向：`Session::write_out` 折算后交 `connection_->write` 的那一份字节。
};

/// @brief 环里的一条记录：一个方向、一个时刻、一段原始字节。
///
/// 时刻是 **run 级单点**（D6①／F11）：一批字节取一次 `system_clock`，不逐字节取时刻——
/// F10 实测「一批就是设备的一次突发」，逐字节粒度既没有语义也没有证人。毫秒取**本地历法前**
/// 的 epoch 毫秒值，格式化留给面板那一次重建（稿 §3 D6④：采集期不做格式化）。
struct RawRun {
    RawDirection direction = RawDirection::Received;
    std::int64_t timestamp_ms = 0;  ///< Unix epoch 毫秒（本地时区的换算在显示期才做）。
    std::vector<std::byte> bytes;   ///< 原始字节，空 run 不会进环（`record` 直接拒）。
};

/// @brief 一条会话的原始字节留痕环：有界、逐出最旧、丢弃累计。
///
/// 生命周期与会话同长：武装由 `Session::enable_raw_tap()` 决定，未武装即根本没有本件实例，
/// 采集点在指针为空时判一次就走（稿 §5 的「未武装零成本」）。会话重启＝新 `Session` 对象＝
/// 不采集，与裁决 7.104「新对象重新上弦」那条口径同形（D7①）。
class RawTap final {
  public:
    /// @brief 建环。
    /// @param max_bytes 环的字节上限；装配层一律留缺省 `kMaxRawTapBytes`，
    ///        用例靠它把「逐出与丢弃」编排到几十字节上（照 `LogSpec::max_bytes` 的测试注入档）。
    explicit RawTap(std::size_t max_bytes = kMaxRawTapBytes);

    RawTap(const RawTap &) = delete;
    auto operator=(const RawTap &) -> RawTap & = delete;
    RawTap(RawTap &&) = delete;
    auto operator=(RawTap &&) -> RawTap & = delete;

    /// @brief 记一笔：一段原始字节连同它的方向与那一刻。
    ///
    /// 三条边界口径（都是「你看到的不是全部」必须说清楚的那几条）：
    ///   ⑴ 空片段**不入环**（零字节的 run 只会让行区多一条无内容的行）；
    ///   ⑵ 单段就超上限即**整条拒收**并把它的全长计入丢弃——不做半条留存，截一半就是伪造设备节奏；
    ///   ⑶ 越界时逐出的是**最旧**的整条 run（按字节不按条数，D4①／F3：run 的长短由设备节奏决定，
    ///       按条数封顶等于按设备节奏封顶）。
    /// @param direction 入向还是出向。
    /// @param timestamp_ms 该批的 epoch 毫秒（调用方取时刻，本件不碰时钟）。
    /// @param bytes 原始字节（本件就地拷贝，调用方可以立刻复用自己的缓冲）。
    /// @note 不得在持 `Session::mutex_` 的临界区内调用（稿 §5）：那把锁护状态机与解码器，
    ///       本件自有 `mutex_`，两把锁不相交才不会把采集成本算进渲染路径。
    auto record(RawDirection direction, std::int64_t timestamp_ms, std::span<const std::byte> bytes)
        -> void;

    /// @brief 交出环里现存的 run 表，**按到达顺序**（最旧在前）。
    ///
    /// 一次性交出值拷贝而非引用：面板每帧取「环里现在有什么」（latest-value，稿 §5 第二条），
    /// 不做增量协议——增量游标一旦与暂停／清空交错就会说出「少了半屏」。交出量至多
    /// `max_bytes()`，故这里不需要第二个上界参数。
    [[nodiscard]] auto snapshot() const -> std::vector<RawRun>;

    /// @brief 清空环（面板那枚「清空」按钮，也是本期唯一的止血手段，D7①）。
    ///
    /// 丢弃累计**一起归零**：清空之后那一屏是新的开始，再挂着「已丢弃 N 字节」就是在说
    /// 一屏已经看不到的数据。
    auto clear() -> void;

    /// @brief 环内现存字节数（不含已逐出的、也不含被拒收的）。
    [[nodiscard]] auto total_bytes() const -> std::size_t;

    /// @brief 环内现存 run 条数。
    [[nodiscard]] auto run_count() const -> std::size_t;

    /// @brief 累计丢弃字节数（逐出 + 整条拒收两档之和；>0 才上屏那句留痕，稿 §2）。
    [[nodiscard]] auto dropped_bytes() const -> std::size_t;

    /// @brief 环状态的变更计数（单调递增；`record` 与 `clear` 任一次实际改动各推进一次）。
    ///
    /// 面板拿它当「这一帧要不要重折」的闸（稿 §9.2 D17①）：环满之后逐出与写入同量，
    /// 「字节数 + run 条数」可以完全不变而内容已经整体前移——只有单调计数不会漏掉这种稳态。
    /// 空片段（`record` 的早退路）不改环，故也不推进。
    /// @return 自构造以来的变更次数。
    [[nodiscard]] auto revision() const -> std::uint64_t;

    /// @brief 构造期定形的上限，此后只读（故不必加锁）。
    [[nodiscard]] auto max_bytes() const noexcept -> std::size_t { return max_bytes_; }

  private:
    mutable std::mutex mutex_;  ///< 与读线程／主线程的交接点；`Session::mutex_` 不参与。
    std::deque<RawRun> runs_;   ///< 到达序，最旧在 front；逐出即 pop_front。
    std::size_t max_bytes_ = kMaxRawTapBytes;
    std::size_t held_bytes_ = 0;   ///< 不变量：恒 ≤ `max_bytes_`（拒收与逐出共同守住）。
    std::size_t dropped_bytes_ = 0;
    std::uint64_t revision_ = 0;   ///< 任何可观测状态变化各推进一次（见 `revision()`）。
};

}  // namespace borealis::session
