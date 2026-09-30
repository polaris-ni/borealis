#pragma once

// ============================================================
// OSC 消费的结果形态（include/borealis/term/osc.h）
// ------------------------------------------------------------
// 架构 §5.4：OSC 串不得整体吞掉——`0/2` 标题、`7` 工作目录、`8` 超链接、`52` 剪贴板、
// `133` 命令块各有各的去向。解析与派发在终端状态机（`term::Terminal`），本文件只给
// 「消费成什么」的形态。
//
// 为什么是状态快照而不是事件队列：状态机跑在会话读线程的网格锁内（架构 §3.4），锁内只能
// 留存而不能投递；OSC 的频率是「一次标题设置、一条目录上报」这个量级，主线程按帧取值即够。
// 唯一的动作型 OSC（`52` 写剪贴板）不进快照，走取走语义（`Terminal::take_clipboard_write`），
// 因为剪贴板是主线程的 IO，锁内碰它就是把 IO 算进锁窗口。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace borealis::term {

/// @brief OSC 133 的 shell 集成边界标记（iTerm2 / FinalTerm 语义）。
enum class PromptMarker : std::uint8_t {
    None,             ///< 尚未收到任何标记。
    PromptStart,      ///< `A`：提示符起始。
    PromptEnd,        ///< `B`：提示符结束、输入区起点。
    CommandStart,     ///< `C`：命令行起始。
    CommandExecuted,  ///< `D`：命令执行完毕，参数携退出码。
    OutputStart,      ///< `P`：命令输出起始，参数携 `Cwd=` 等键值。
};

/// @brief 超链接表的容量上限。
///
/// 会话字节流是不可信输入：`OSC 8` 可以无限供给新 URI，表不封顶就是让一条输出吃掉内存。
/// 满表淘汰最旧一条，被淘汰标识在网格里解析不出目标（不可点），而不是指向另一条 URL。
inline constexpr std::size_t kMaxHyperlinks = 1024U;

/// @brief OSC 消费留下的会话侧状态（`SPEC.FEAT.TERM.07`，并为 `SPEC.FEAT.INTEG.01/02` 预埋来源）。
struct OscState {
    std::u32string title;             ///< `OSC 0/2` 最近一次设置的标题；空即未设置。
    std::u32string working_directory; ///< `OSC 7` 最近一次上报的 `file://host/path`（原样留存）。
    PromptMarker prompt_marker = PromptMarker::None;  ///< `OSC 133` 最近一次边界标记。
    std::int32_t command_exit_code = 0;  ///< `133;D;<code>` 的退出码；未收到 `D` 时为 0。
    std::size_t clipboard_write_requests =
        0;  ///< `OSC 52` 写方向请求总数（含被合并进同一待写值的）
    std::size_t unhandled_count = 0;  ///< 未消费的 `OSC` 命令号次数（架构 §5.5 的降级留痕）
};

}  // namespace borealis::term
