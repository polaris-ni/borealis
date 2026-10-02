#pragma once

// ============================================================
// 粘贴处置计划（include/borealis/term/paste.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.03` 的发送侧：把「剪贴板里的一段文本 + 此刻是否开着 bracketed paste
// + 三项可配口径」折算成**要按什么顺序、隔多久发哪几块**。这里只有折算规则，不触达框架
// 的定时器与事件循环——排期发生在主线程的绘制侧控件（框架 `Scheduler::set_timeout`），
// 本件保持纯函数以便逐条断言（同 `term::encode_key` 的分工，架构 §10.2）。
//
// 三条口径的由来（细则见裁决 7.33）：
// - **bracketed paste 优先于换行策略**：`?2004` 的语义就是「整段原样交给 shell 判定」，
//   应用侧再过滤或转换行尾等于替 shell 做了它明令保留的判断，且会让 shell 自带的
//   多行确认提示失效。故激活时只加一对包裹序列，文本逐字节不动、也不节流。
// - **多行警告的判据是「待发字节里仍有行尾」**，不是「原文本里有换行」：警告要挡的事故是
//   每一行被 shell 当成一条命令依次执行，而 `Filter` 与 bracketed 两种形态都不会触发它。
//   判据取待发结果，对话框就不会为一个「粘成一行的粘贴」弹警告。
// - **换行切分只认 CR / LF / CRLF**：剪贴板文本的行尾只有这三种形态（Windows 给 CRLF、
//   POSIX 给 LF、老设备给 CR），U+0085 / U+2028 一类 Unicode 行分隔符在终端网格里
//   并不是换行，把它们当断点会把一行内容切成两条命令。
// ============================================================

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace borealis::term {

/// @brief 粘贴文本里换行符的处理策略（`SPEC.FEAT.INTERACT.03`）。
///
/// 唯一定义处：`config::PasteNewlinePolicy` 是本枚举的别名，配置侧不再各自持有一份取值表，
/// 以免出现「两个真值源在枚举值上分叉」。
enum class PasteNewlinePolicy : std::uint8_t {
    AsIs,    ///< 原样发送（缺省：与该条「保留原样为默认」的复制语义同口径）。
    Filter,  ///< 剥掉换行，粘成一行。
    Convert, ///< 换行转成显式行尾序列（与串口 `line_ending` 同一套取值）。
};

/// @brief 显式行尾序列（`SPEC.FEAT.CONN.05` 明写默认 LF——发错设备无响应）。
enum class LineEnding : std::uint8_t {
    Lf,   ///< `\n`。
    Cr,   ///< `\r`。
    Crlf, ///< `\r\n`。
};

/// @brief 一次粘贴的处置口径。
///
/// 由调用方从 `config::TerminalSettings` 搬值：本件不含 `config` 类型（`config/settings.h`
/// 已含 `term/terminal.h`，反向依赖会成模块环）。`line_ending` 的取值来源是连接的行尾设置
/// （串口用 `connection.serial.line_ending`，本地终端与 SSH 用缺省），本件只消费最终值。
struct PasteOptions {
    PasteNewlinePolicy newline{PasteNewlinePolicy::AsIs};  ///< 换行策略；bracketed 激活时不生效。
    LineEnding line_ending{LineEnding::Lf};                ///< 仅 `Convert` 用到。
    std::chrono::milliseconds line_interval{10};           ///< 逐行节流的块间隔；bracketed 激活时不生效。

    /// @brief 逐字段全等比较（配置往返与计划断言用）。
    [[nodiscard]] auto operator==(const PasteOptions &other) const noexcept -> bool = default;
};

/// @brief 计划里的一块：一段文本，以及它**之前**要等待的时间（相对上一块发出之后）。
struct PasteChunk {
    std::u32string text{};                        ///< 待写进会话编码的码点流。
    std::chrono::milliseconds delay{};            ///< 本块前的等待；首块恒 0。

    /// @brief 逐字段全等比较（计划断言用）。
    [[nodiscard]] auto operator==(const PasteChunk &other) const noexcept -> bool = default;
};

/// @brief 一次粘贴的完整处置计划。
struct PastePlan {
    std::size_t line_breaks = 0;  ///< 原文本里的换行数（切分只认 CR / LF / CRLF）。
    bool multiline = false;       ///< 待发文本里仍有行尾：多行粘贴警告的判据。
    bool bracketed = false;       ///< 本次是否走 bracketed paste 包裹形态。
    std::vector<PasteChunk> chunks;

    /// @brief 逐字段全等比较（计划断言用）。
    [[nodiscard]] auto operator==(const PastePlan &other) const noexcept -> bool = default;
};

/// @brief 把剪贴板文本折算成发送计划。
///
/// 纯函数：同一入参恒得同一计划，故三条口径全部可在无 UI 环境里逐条断言。
/// @param text 剪贴板原文（码点流）。
/// @param bracketed_paste `TermModes::bracketed_paste`（`?2004`）此刻是否为真；为真时文本
///                        逐字节原样，换行策略与节流一律不生效。
/// @param options 换行策略、转换目标行尾与块间隔。
/// @return 发送计划；空文本给出空 `chunks`（不发任何东西，也不警告）。
[[nodiscard]] auto plan_paste(std::u32string_view text, bool bracketed_paste, const PasteOptions &options)
    -> PastePlan;

}  // namespace borealis::term
