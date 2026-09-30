#pragma once

// ============================================================
// 测试公共设施（tests/support/terminal_feed.h）—— 字节到状态机的链路回放
// ------------------------------------------------------------
// 集成用例要断言的是「整条链」的行为（字节 → UTF-8 解码 → VT 解析 → 状态机 → 网格），
// 直接调 `Terminal::feed` 就把解码那段跳过了，非法字节与过长编码这类事实根本进不了断言。
// 多个集成用例共用同一份接线，避免两处写法漂移。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/term/terminal.h"
#include "borealis/term/utf8.h"

namespace borealis::testing {

/// @brief 解码器的码点出口直接接状态机：生产链路里这两步同在会话读线程上（架构 §3.2）。
class TerminalFeeder final : public borealis::term::CodePointSink {
  public:
    explicit TerminalFeeder(borealis::term::Terminal &terminal) : terminal_{&terminal} {}

    auto on_code_point(char32_t code_point) -> void override {
        terminal_->feed(std::u32string_view{&code_point, 1});
    }

  private:
    borealis::term::Terminal *terminal_ = nullptr;
};

/// @brief 回放原始字节到状态机。
/// @param terminal 目标状态机。
/// @param bytes 原始字节（会话侧读到的那一串）。
/// @return 解码期间的非法字节替换数（用例据此断言「混入非法字节后链路仍在跑」）。
inline auto feed_bytes(borealis::term::Terminal &terminal, std::string_view bytes) -> std::uint64_t {
    std::vector<std::byte> raw;
    for (const char c : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    TerminalFeeder feeder{terminal};
    borealis::term::Utf8Decoder decoder;
    decoder.feed(raw, feeder);
    decoder.finish(feeder);
    return decoder.stats().replaced;
}

/// @brief 把码点文本编码成 UTF-8 后回放到状态机。
///
/// 用例里的非 ASCII 字符写成 `\uXXXX` 转义（源码保持 ASCII，AGENTS.md §4.3 第 14 条），
/// 由本函数负责编成字节，断言侧看到的仍是真实字节流。
/// @param terminal 目标状态机。
/// @param text 码点文本。
/// @return 编码与解码两侧的替换数之和（0 表示整段原样抵达状态机）。
inline auto feed_text(borealis::term::Terminal &terminal, std::u32string_view text) -> std::uint64_t {
    std::string bytes;
    const auto encode_replaced = borealis::term::encode_utf8(text, bytes);
    return static_cast<std::uint64_t>(encode_replaced) + feed_bytes(terminal, bytes);
}

}  // namespace borealis::testing
