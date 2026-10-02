// ============================================================
// 粘贴处置计划的实现（src/term/paste.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.03` 的发送侧折算，三条口径见公共头文件头与裁决 7.33。
// ============================================================

#include "borealis/term/paste.h"

#include <algorithm>
#include <utility>

namespace borealis::term {
namespace {

constexpr char32_t kCarriageReturn = U'\r';
constexpr char32_t kLineFeed = U'\n';

/// @brief bracketed paste 的一对包裹序列（DECSET 2004，`SPEC.FEAT.TERM.01`）。
constexpr std::u32string_view kBracketStart = U"\x1B[200~";
constexpr std::u32string_view kBracketEnd = U"\x1B[201~";

/// @brief 取 @p at 处换行的长度：CRLF 算一个换行，否则单字符。
/// @param text 原文本。
/// @param at CR 或 LF 的下标。
/// @return 该换行占的码点数（1 或 2）。
[[nodiscard]] auto break_length(std::u32string_view text, std::size_t at) -> std::size_t {
    if (text[at] == kCarriageReturn && at + 1 < text.size() && text[at + 1] == kLineFeed) {
        return 2U;
    }
    return 1U;
}

/// @brief 定位下一个换行；找不到返回 `npos`。
/// @param text 原文本。
/// @param from 起始下标。
[[nodiscard]] auto next_break(std::u32string_view text, std::size_t from) -> std::size_t {
    for (std::size_t index = from; index < text.size(); ++index) {
        if (text[index] == kCarriageReturn || text[index] == kLineFeed) {
            return index;
        }
    }
    return std::u32string_view::npos;
}

/// @brief 行尾取值的码点形态。
[[nodiscard]] auto ending_text(LineEnding ending) -> std::u32string_view {
    switch (ending) {
    case LineEnding::Lf:
        return std::u32string_view{U"\n", 1};
    case LineEnding::Cr:
        return std::u32string_view{U"\r", 1};
    case LineEnding::Crlf:
        return std::u32string_view{U"\r\n", 2};
    }
    return std::u32string_view{U"\n", 1};
}

/// @brief 按原分隔符切块：块含其后的换行，末段（无换行跟随）不含。
/// @param plan 目标计划，块追加到其尾部。
/// @param text 原文本。
auto append_verbatim_chunks(PastePlan &plan, std::u32string_view text) -> void {
    std::size_t start = 0;
    while (start < text.size()) {
        const auto at = next_break(text, start);
        auto chunk = PasteChunk{};
        if (at == std::u32string_view::npos) {
            chunk.text = std::u32string{text.substr(start)};
            plan.chunks.push_back(std::move(chunk));
            return;
        }
        const auto length = break_length(text, at);
        chunk.text = std::u32string{text.substr(start, at - start + length)};
        plan.chunks.push_back(std::move(chunk));
        start = at + length;
    }
}

/// @brief 按选定行尾重拼：每个换行都换成 @p ending，末段无换行跟随则不附。
/// @param plan 目标计划。
/// @param text 原文本。
/// @param ending 转换目标行尾。
auto append_converted_chunks(PastePlan &plan, std::u32string_view text, LineEnding ending) -> void {
    std::size_t start = 0;
    while (start < text.size()) {
        const auto at = next_break(text, start);
        auto chunk = PasteChunk{};
        if (at == std::u32string_view::npos) {
            chunk.text = std::u32string{text.substr(start)};
            plan.chunks.push_back(std::move(chunk));
            return;
        }
        const auto length = break_length(text, at);
        chunk.text = std::u32string{text.substr(start, at - start)};
        chunk.text.append(ending_text(ending));
        plan.chunks.push_back(std::move(chunk));
        start = at + length;
    }
}

}  // namespace

auto plan_paste(std::u32string_view text, bool bracketed_paste, const PasteOptions &options) -> PastePlan {
    auto plan = PastePlan{};
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == kCarriageReturn || text[index] == kLineFeed) {
            ++plan.line_breaks;
            index += break_length(text, index) - 1;  // CRLF 只算一个换行
        }
    }
    plan.bracketed = bracketed_paste;
    if (text.empty()) {
        return plan;  // 空文本既不发东西也不警告
    }

    if (bracketed_paste) {
        // 原样透传：行尾归 shell 判定，应用侧不重排也不节流。
        auto chunk = PasteChunk{};
        chunk.text = std::u32string{kBracketStart};
        chunk.text.append(text);
        chunk.text.append(kBracketEnd);
        plan.chunks.push_back(std::move(chunk));
        return plan;
    }
    switch (options.newline) {
    case PasteNewlinePolicy::AsIs:
        append_verbatim_chunks(plan, text);
        break;
    case PasteNewlinePolicy::Filter: {
        auto filtered = std::u32string{};
        filtered.reserve(text.size());
        for (const auto cp : text) {
            if (cp != kCarriageReturn && cp != kLineFeed) {
                filtered.push_back(cp);
            }
        }
        if (!filtered.empty()) {
            plan.chunks.push_back(PasteChunk{std::move(filtered), {}});
        }
        break;
    }
    case PasteNewlinePolicy::Convert:
        append_converted_chunks(plan, text, options.line_ending);
        break;
    }

    // 判据取自待发结果而非原文本：换行已被剥光的粘贴不会逐行执行，不该弹多行警告。
    plan.multiline = std::any_of(plan.chunks.begin(), plan.chunks.end(), [](const PasteChunk &chunk) {
        return chunk.text.find_first_of(U"\r\n") != std::u32string::npos;
    });

    // 首块立即发，其后每块前等一个间隔；间隔为 0 即退化成不分次节流。
    for (std::size_t index = 1; index < plan.chunks.size(); ++index) {
        plan.chunks[index].delay = options.line_interval;
    }
    return plan;
}

}  // namespace borealis::term
