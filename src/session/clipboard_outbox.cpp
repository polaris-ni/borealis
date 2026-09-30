// ============================================================
// OSC 52 剪贴板落地实现（src/session/clipboard_outbox.cpp）
// ------------------------------------------------------------
// 本文件是这条腿上唯一触达框架与系统剪贴板的地方：会话侧只留存文本，取走与写入都发生在
// 主线程的帧边界。
// ============================================================

#include "borealis/session/clipboard_outbox.h"

#include <string>

#include "aurora/app/clipboard.h"
#include "aurora/core/log.h"
#include "borealis/session/session.h"
#include "borealis/term/utf8.h"

namespace borealis::session {

auto ClipboardOutbox::drain(Session &session) -> std::size_t {
    const auto pending = session.take_clipboard_write();
    if (!pending.has_value()) {
        return 0U;
    }
    std::string utf8;
    utf8.reserve(pending->size());
    // 不可表示码点在解码环节就已换成替换字符，这里没有第二层失败面。
    static_cast<void>(term::encode_utf8(*pending, utf8));
    const auto result = aurora::Clipboard::set_text(utf8);
    if (!result.ok()) {
        // 失败只留诊断：远端拿不到回执，抛给帧循环只会让画面为一次剪贴板写买单。
        AURORA_LOG_WARN("session", "OSC 52 clipboard write failed: ", result.error().message);
    }
    return 1U;
}

}  // namespace borealis::session
