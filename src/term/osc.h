#pragma once

// ============================================================
// OSC 串的字段切分与载荷解码（src/term/osc.h）
// ------------------------------------------------------------
// 只在状态机内部使用，故留在 src/、不进 `include/borealis/`。这里只有「一串码点 → 字段」
// 的纯切分与解码，不解释任何终端语义——语义派发归 `Terminal::do_osc`。
// ============================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace borealis::term::osc {

/// @brief 按首个分隔符切出的两段。
struct Fields {
    std::u32string_view head;   ///< 分隔符之前的首段。
    std::u32string_view tail;   ///< 分隔符之后的**全部**剩余：标题与 URI 自身可含分隔符。
    bool found = false;         ///< 是否确有分隔符。
};

/// @brief 按 @p separator 切一次（首段 + 其余整段）。
/// @param text 待切文本。
/// @param separator 分隔码点。
/// @return 切分结果；无分隔符时 `head` 为全文、`tail` 为空、`found` 为假。
[[nodiscard]] auto split_first(std::u32string_view text, char32_t separator) noexcept -> Fields;

/// @brief 按 @p separator 切一次（其余整段 + 末段）：`OSC 52` 的载荷是最后一段。
/// @param text 待切文本。
/// @param separator 分隔码点。
/// @return 切分结果；无分隔符时 `head` 为空、`tail` 为全文、`found` 为假。
[[nodiscard]] auto split_last(std::u32string_view text, char32_t separator) noexcept -> Fields;

/// @brief 拆出 OSC 的命令号与其后的整段参数。
/// @param data OSC 负载（形如 `8;;http://x`）。
/// @return 命令号与首个 `;` 之后的参数；负载不以十进制数字开头时为 `std::nullopt`。
[[nodiscard]] auto split_command(std::u32string_view data) noexcept
    -> std::optional<std::pair<std::int32_t, std::u32string_view>>;

/// @brief 取十进制无符号整数字段的首值（越界即钳到 @p limit）。
/// @param text 数字串（可含其余内容，只读前缀）。
/// @param limit 上界。
/// @return 解析值；前缀无数字时为 0。
[[nodiscard]] auto decimal(std::u32string_view text, std::int32_t limit) noexcept -> std::int32_t;

/// @brief 解 `OSC 52` 的 base64 载荷，并按 UTF-8 还原成码点串。
///
/// 载荷是「再编码一层」的 UTF-8 字节：OSC 串本身已由会话解码成码点，base64 解出来的字节
/// 须再走一次解码才是文本内容。容忍缺失的 `=` 填充，出现字母表外字符即判非法。
/// @param payload base64 载荷。
/// @return 解出的文本；载荷非法（含只剩一个 6 位组的截断载荷）时为 `std::nullopt`。
[[nodiscard]] auto decode_base64(std::u32string_view payload) -> std::optional<std::u32string>;

}  // namespace borealis::term::osc
