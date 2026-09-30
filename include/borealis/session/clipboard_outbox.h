#pragma once

// ============================================================
// OSC 52 剪贴板落地件（include/borealis/session/clipboard_outbox.h）
// ------------------------------------------------------------
// 状态机跑在会话读线程的网格锁内，那里只能留存待写文本；真正写系统剪贴板是主线程的 IO
// （架构 §3.2、§3.4），故「取走 + 落地」单独一件。声明留在公共头、Aurora 的 `Clipboard`
// 依赖留在实现里——`include/borealis/` 不得出现框架头（架构 §2.3）。
// ============================================================

#include <cstddef>

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
};

}  // namespace borealis::session
