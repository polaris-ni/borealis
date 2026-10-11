// ============================================================
// 剪贴板落地实现（src/session/clipboard_outbox.cpp）
// ------------------------------------------------------------
// 本文件是全仓唯一触达框架与系统剪贴板的地方：远端 `OSC 52` 的留存文本与本地选中复制的文本
// 都从这里出去，本地右键粘贴的文本从这里进来，取走、写入与读取都发生在主线程。
// ============================================================

#include "borealis/session/clipboard_outbox.h"

#include <span>
#include <string>
#include <string_view>

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

auto ClipboardOutbox::read() -> std::string {
    const auto result = aurora::Clipboard::get_text();
    if (!result.ok()) {
        // 与写方向同一条口径：读不到就当剪贴板是空的，调用方什么都不发。剪贴板没有内容格式
        // 是日常状态（复制过图片之后），为它抬一条 warn 会让日志失去信号价值。
        AURORA_LOG_DEBUG("session", "clipboard read unavailable: ", result.error().message);
        return {};
    }
    return result.value();
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

namespace {

/// @brief 把纯 ASCII 的应答骨架升格成码点串。
///
/// 只喂 ASCII 是刻意的：`ESC ] 52 ; <Pc> ; <base64> BEL` 的每一段都出不了 base64 字母表与
/// 状态机侧的 Pc 归一，走一遍解码器反而会给「载荷里有非法字节」留一条并不存在的路径。
[[nodiscard]] auto widen(std::string_view ascii) -> std::u32string {
    std::u32string out;
    out.reserve(ascii.size());
    for (const char c : ascii) {
        out.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 回显用的选择位：状态机侧已归一成单个 ASCII 码点，这里只兜住「来路不对」那一档。
[[nodiscard]] auto pc_of(const term::ClipboardReadRequest &request) -> std::string {
    if (request.pc.size() == 1U && request.pc.front() < 128U) {
        return std::string{static_cast<char>(static_cast<unsigned char>(request.pc.front()))};
    }
    return "c";  // 归一失效即退回系统剪贴板那一位，与空响应的回显同一条
}

}  // namespace

auto ClipboardOutbox::answer_read(Session &session, const term::ClipboardReadRequest &request,
                                  std::optional<std::string_view> content) -> bool {
    static constexpr std::u32string_view kEmptyResponse{U"\x1B]52;c;\x07"};
    // 超限按拒绝处理而不是截断：半截内容比没有内容更糟（远端会把它当完整粘贴）。
    const bool oversize = content.has_value() && content->size() > kMaxClipboardResponseBytes;
    if (oversize) {
        AURORA_LOG_WARN("session", "clipboard read answer dropped: bytes=", content->size(),
                        " limit=", kMaxClipboardResponseBytes);
    }
    if (!content.has_value() || oversize || content->empty()) {
        // 三条降级（拒绝、超限、本地没有文本）共用同一条出口，且这条串逐字节是既有判据。
        session.answer_clipboard_read(kEmptyResponse);
        return false;
    }
    const auto payload = term::encode_base64(
        std::as_bytes(std::span<const char>{content->data(), content->size()}));
    std::string answer;
    answer.reserve(payload.size() + 16U);
    answer += "\x1B]52;";
    answer += pc_of(request);
    answer += ";";
    answer += payload;
    answer += "\x07";
    session.answer_clipboard_read(widen(answer));
    return true;
}

}  // namespace borealis::session
