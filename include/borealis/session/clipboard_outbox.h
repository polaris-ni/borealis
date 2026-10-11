#pragma once

// ============================================================
// 剪贴板落地件（include/borealis/session/clipboard_outbox.h）
// ------------------------------------------------------------
// 状态机跑在会话读线程的网格锁内，那里只能留存待写文本；真正写系统剪贴板是主线程的 IO
// （架构 §3.2、§3.4），故「取走 + 落地」单独一件。声明留在公共头、Aurora 的 `Clipboard`
// 依赖留在实现里——`include/borealis/` 不得出现框架头（架构 §2.3）。
//
// 全仓的剪贴板 IO 只有本类：`drain` 落远端 `OSC 52` 留存的文本，`write` 落本地选中复制的
// 文本，`read` 取本地右键粘贴的文本，三条共用同一条失败口径，且框架头不出实现文件。
// 第四条 `answer_read` 只做编码与降级、不碰 IO：读方向的应答按条给（判据 ③「被拒也要有回写」），
// 把「问」与「读」留在调用方，三档降级才能脱界面断言。
// ============================================================

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "borealis/term/clipboard_read.h"

namespace borealis::session {

class Session;

/// @brief 一条 `OSC 52` 读应答允许带出的最大 UTF-8 字节数（`SPEC.FEAT.CONN.12`，裁决 D7①）。
///
/// 剪贴板里可能是几 MB 的日志，base64 再乘 4/3，而一条应答是一次 `send` 到对端：远端只问了
/// 「给不给」，没承诺收多少。取文件级常量而不是配置键——与会话日志那棒的上限同档取向。
inline constexpr std::size_t kMaxClipboardResponseBytes = 64U * 1024U;

/// @brief 系统剪贴板与会话字节之间的落地件（写方向默认允许，读方向走三态闸，`SPEC.FEAT.CONN.12`）。
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

    /// @brief 读系统剪贴板的 UTF-8 文本（右键粘贴的入口，`SPEC.FEAT.INTERACT.03`）。
    ///
    /// 剪贴板 IO 只有一个落点是本类，读方向亦然——框架头不许出这个文件（架构 §2.3）。
    /// 只可在主线程的帧边界调用（事件回调里不得做 IO，AGENTS.md §4.5 第 25 条）。
    /// @return 剪贴板里的 UTF-8 文本；剪贴板为空、内容非文本或读取失败时回空串，调用方据此
    ///         什么都不发（粘贴一条空文本等于什么都没发生，不需要区分失败原因）。
    [[nodiscard]] static auto read() -> std::string;

    /// @brief 答复**一条** `OSC 52` 读请求（`SPEC.FEAT.CONN.12` 的读方向）。
    ///
    /// 按条而非按批：读方向逐条答（裁决 D4①），每条各有回写，合并就是让先问的那条永远收不到回应。
    /// 本件**不读剪贴板也不弹窗**——内容与许可都由调用方给，这里只做编码与降级，于是
    /// 「拒绝 / 超限 / 内容为空」三档能脱界面断言。三档的出口是同一条既有空响应，逐字节不变。
    /// 只可在主线程调用（`session.answer_clipboard_read` 要写连接）。
    /// @param session 目标会话。
    /// @param request 待答请求（其 Pc 由状态机侧归一，本件原样回显）。
    /// @param content 已经拿到手的本地剪贴板文本；`std::nullopt` 即「这一条被拒」。
    /// @return 是否给出了正文（false ⇒ 回写的是那条空响应）。
    static auto answer_read(Session &session, const term::ClipboardReadRequest &request,
                            std::optional<std::string_view> content) -> bool;
};

}  // namespace borealis::session
