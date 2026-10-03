#pragma once

// ============================================================
// 剪贴板落地件（include/borealis/session/clipboard_outbox.h）
// ------------------------------------------------------------
// 状态机跑在会话读线程的网格锁内，那里只能留存待写文本；真正写系统剪贴板是主线程的 IO
// （架构 §3.2、§3.4），故「取走 + 落地」单独一件。声明留在公共头、Aurora 的 `Clipboard`
// 依赖留在实现里——`include/borealis/` 不得出现框架头（架构 §2.3）。
//
// 全仓的剪贴板写点只有本类：`drain` 落远端 `OSC 52` 留存的文本，`write` 落本地选中复制的
// 文本，两条来源共用同一条失败口径。
// ============================================================

#include <cstddef>
#include <string_view>

namespace borealis::session {

class Session;

/// @brief 把会话里待写的 `OSC 52` 文本落到系统剪贴板（写方向默认允许，`SPEC.FEAT.CONN.12`）。
class ClipboardOutbox {
  public:
    /// @brief 取走并落地待写文本；无待写请求即空转。
    ///
    /// 只可在主线程调用。落地失败只记诊断、不向上抛：远端程序本就收不到回执，让一次剪贴板写
    /// 打断帧循环更不是降级而是故障。
    /// @param session 目标会话。
    /// @return 本次落地的请求条数（0 或 1；同段输入的多次写在会话侧已合并成一条）。
    auto drain(Session &session) -> std::size_t;

    /// @brief 直接把 UTF-8 文本写系统剪贴板（本地选中复制，`SPEC.FEAT.INTERACT.03`）。
    ///
    /// 与 `drain` 的差别只在来源：那条取远端 `OSC 52` 留存的文本，本条是用户在本仓选中的内容。
    /// 两者共用同一条失败口径（只记诊断、不向上抛），故全仓的剪贴板写点仍只有本文件一处。
    /// 只可在主线程调用（事件回调里不得做 IO，AGENTS.md §4.5 第 25 条）。
    /// @param utf8 待写入文本，原样落剪贴板、不按平台惯例翻译行分隔符（裁决 7.32①）。
    static auto write(std::string_view utf8) -> void;
};

}  // namespace borealis::session
