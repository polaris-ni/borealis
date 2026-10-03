// ============================================================
// 剪贴板落地实现（src/session/clipboard_outbox.cpp）
// ------------------------------------------------------------
// 本文件是全仓唯一触达框架与系统剪贴板的地方：远端 `OSC 52` 的留存文本与本地选中复制的文本
// 都从这里出去，取走与写入都发生在主线程。
// ============================================================

#include "borealis/session/clipboard_outbox.h"

#include <string>

#include "aurora/app/clipboard.h"
#include "aurora/core/log.h"
#include "borealis/session/session.h"
#include "borealis/term/utf8.h"

namespace borealis::session {

auto ClipboardOutbox::write(std::string_view utf8) -> void {
    // 框架的写入口收 `const std::string &`，故这里物化一次；文本本就整段进剪贴板，无二次拷贝的空间。
    const std::string text{utf8};
    const auto result = aurora::Clipboard::set_text(text);
    if (!result.ok()) {
        // 失败只留诊断：两条来源的写者都拿不到回执（远端程序本就收不到，本地用户也没有对话框），
        // 抛给帧循环只会让画面为一次剪贴板写买单。
        AURORA_LOG_WARN("session", "clipboard write failed: ", result.error().message);
    }
}

auto ClipboardOutbox::drain(Session &session) -> std::size_t {
    const auto pending = session.take_clipboard_write();
    if (!pending.has_value()) {
        return 0U;
    }
    std::string utf8;
    utf8.reserve(pending->size());
    // 不可表示码点在解码环节就已换成替换字符，这里没有第二层失败面。
    static_cast<void>(term::encode_utf8(*pending, utf8));
    write(utf8);
    return 1U;
}

}  // namespace borealis::session
