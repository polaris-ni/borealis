// ============================================================
// 键盘输入映射实现（src/term/keymap.cpp）
// ------------------------------------------------------------
// 编码表照 xterm 的遗留形态（方向键与功能键的 CSI/SS3 族、metaSendsEscape 的缺省档），因为
// `SPEC.FEAT.INTERACT.01` 圈定的就是这一档；kitty 协议与 `modifyOtherKeys` 是延后观察项，故
// 「无遗留编码的组合」一律返回空而不是自造一套新形态——空值即「本层不消费」，交回框架的
// 快捷键层或文本通道。
// ============================================================

#include "borealis/term/keymap.h"

#include <string>

#include "borealis/term/utf8.h"

namespace borealis::term {
namespace {

/// @brief 转义字节：编码表到处要用，写成常量免得逐处记数值。
constexpr char kEscape = '\x1B';

/// @brief 退格的上屏口径：遗留终端发 DEL（0x7F）而非 BS（0x08），Ctrl+退格才是 0x08。
constexpr char kDelete = '\x7F';

/// @brief US 布局的「未移位 / 移位」成对字符；Ctrl 与 Alt 形态都由它派生。
struct KeyPair {
    char32_t plain;
    char32_t shifted;
};

/// @brief 字母形态在无修饰时的基形态。
enum class LetterBase : std::uint8_t {
    Ss3,   ///< 恒走 SS3（F1–F4）。
    App,   ///< 随 DECCKM 切换：应用模式 SS3、常规模式 CSI（方向键与 Home/End）。
};

/// @brief 编辑/功能键的编码形态：字母族用一个 `letter`，波浪号族用一个 `tilde` 编码号。
struct SpecialForm {
    LetterBase base = LetterBase::Ss3;
    char32_t letter = 0;
    int tilde = 0;
};

/// @brief 可打印键的成对字符（含空格）；其余键返回空。
[[nodiscard]] auto printable(KeySym sym) -> std::optional<KeyPair> {
    const int code = static_cast<int>(sym);
    if (code >= static_cast<int>(KeySym::A) && code <= static_cast<int>(KeySym::Z)) {
        const int offset = code - static_cast<int>(KeySym::A);
        return KeyPair{.plain = static_cast<char32_t>('a' + offset),
                       .shifted = static_cast<char32_t>('A' + offset)};
    }
    switch (sym) {
        case KeySym::D0: return KeyPair{.plain = U'0', .shifted = U')'};
        case KeySym::D1: return KeyPair{.plain = U'1', .shifted = U'!'};
        case KeySym::D2: return KeyPair{.plain = U'2', .shifted = U'@'};
        case KeySym::D3: return KeyPair{.plain = U'3', .shifted = U'#'};
        case KeySym::D4: return KeyPair{.plain = U'4', .shifted = U'$'};
        case KeySym::D5: return KeyPair{.plain = U'5', .shifted = U'%'};
        case KeySym::D6: return KeyPair{.plain = U'6', .shifted = U'^'};
        case KeySym::D7: return KeyPair{.plain = U'7', .shifted = U'&'};
        case KeySym::D8: return KeyPair{.plain = U'8', .shifted = U'*'};
        case KeySym::D9: return KeyPair{.plain = U'9', .shifted = U'('};
        case KeySym::Space: return KeyPair{.plain = U' ', .shifted = U' '};
        case KeySym::Minus: return KeyPair{.plain = U'-', .shifted = U'_'};
        case KeySym::Equal: return KeyPair{.plain = U'=', .shifted = U'+'};
        case KeySym::LeftBracket: return KeyPair{.plain = U'[', .shifted = U'{'};
        case KeySym::RightBracket: return KeyPair{.plain = U']', .shifted = U'}'};
        case KeySym::Backslash: return KeyPair{.plain = U'\\', .shifted = U'|'};
        case KeySym::Semicolon: return KeyPair{.plain = U';', .shifted = U':'};
        case KeySym::Quote: return KeyPair{.plain = U'\'', .shifted = U'"'};
        case KeySym::Comma: return KeyPair{.plain = U',', .shifted = U'<'};
        case KeySym::Period: return KeyPair{.plain = U'.', .shifted = U'>'};
        case KeySym::Slash: return KeyPair{.plain = U'/', .shifted = U'?'};
        case KeySym::Backquote: return KeyPair{.plain = U'`', .shifted = U'~'};
        default: return std::nullopt;
    }
}

/// @brief 编辑/功能键的形态表（Delete/PageUp/PageDown 与 F5–F12 属波浪号族，其余属字母族）。
[[nodiscard]] auto special_form(KeySym sym) -> std::optional<SpecialForm> {
    switch (sym) {
        case KeySym::ArrowUp: return SpecialForm{.base = LetterBase::App, .letter = U'A'};
        case KeySym::ArrowDown: return SpecialForm{.base = LetterBase::App, .letter = U'B'};
        case KeySym::ArrowRight: return SpecialForm{.base = LetterBase::App, .letter = U'C'};
        case KeySym::ArrowLeft: return SpecialForm{.base = LetterBase::App, .letter = U'D'};
        case KeySym::Home: return SpecialForm{.base = LetterBase::App, .letter = U'H'};
        case KeySym::End: return SpecialForm{.base = LetterBase::App, .letter = U'F'};
        case KeySym::F1: return SpecialForm{.base = LetterBase::Ss3, .letter = U'P'};
        case KeySym::F2: return SpecialForm{.base = LetterBase::Ss3, .letter = U'Q'};
        case KeySym::F3: return SpecialForm{.base = LetterBase::Ss3, .letter = U'R'};
        case KeySym::F4: return SpecialForm{.base = LetterBase::Ss3, .letter = U'S'};
        case KeySym::Delete: return SpecialForm{.tilde = 3};
        case KeySym::PageUp: return SpecialForm{.tilde = 5};
        case KeySym::PageDown: return SpecialForm{.tilde = 6};
        case KeySym::F5: return SpecialForm{.tilde = 15};
        case KeySym::F6: return SpecialForm{.tilde = 17};
        case KeySym::F7: return SpecialForm{.tilde = 18};
        case KeySym::F8: return SpecialForm{.tilde = 19};
        case KeySym::F9: return SpecialForm{.tilde = 20};
        case KeySym::F10: return SpecialForm{.tilde = 21};
        case KeySym::F11: return SpecialForm{.tilde = 23};
        case KeySym::F12: return SpecialForm{.tilde = 24};
        default: return std::nullopt;
    }
}

/// @brief 不带 Ctrl 时的回车 / Tab / 退格 / Esc 形态；其余键返回空。
[[nodiscard]] auto plain_form(KeySym sym, bool shift) -> std::optional<std::string> {
    switch (sym) {
        case KeySym::Enter: return std::string(1, '\r');
        case KeySym::Tab:
            // Shift+Tab 是遗留的反向焦点/回退形态，与 Tab 本身不同码（xterm `backarrow` 之外的通例）。
            return shift ? std::string{kEscape, '[', 'Z'} : std::string(1, '\t');
        case KeySym::Backspace: return std::string(1, kDelete);
        case KeySym::Escape: return std::string(1, kEscape);
        default: return std::nullopt;
    }
}

/// @brief Ctrl 形态：`@`–`_` 区间的字符按「大写折叠后取低 5 位」得控制码，另有两个特例。
///
/// 区间外（如 `/` `:` `.`）在遗留编码里没有对应控制值，kitty 协议又属延后项，故返回空而非自造。
[[nodiscard]] auto control_form(KeySym sym, bool shift) -> std::optional<std::string> {
    if (sym == KeySym::Enter) {
        return std::string(1, '\r');  // Ctrl+Enter 无独立遗留编码，按回车发（Windows Terminal 同档）
    }
    if (sym == KeySym::Backspace) {
        return std::string(1, '\x08');
    }
    const auto pair = printable(sym);
    if (!pair) {
        return std::nullopt;
    }
    char32_t ch = shift ? pair->shifted : pair->plain;
    if (ch == U' ') {
        return std::string(1, '\x00');  // Ctrl+Space = NUL
    }
    if (ch >= U'a' && ch <= U'z') {
        ch -= 32;  // 控制码按大写折叠：Ctrl+a 与 Ctrl+Shift+A 同为 0x01
    }
    if (ch < U'@' || ch > U'_') {
        return std::nullopt;
    }
    return std::string(1, static_cast<char>(static_cast<unsigned>(ch) & 0x1FU));
}

/// @brief 把字母族与波浪号族按修饰参数拼成序列。
///
/// 掩码口径照 xterm：`1 + shift + alt * 2 + ctrl * 4`，为 1（无修饰）时省略参数段。
[[nodiscard]] auto encode_special(const SpecialForm &form, int mask, const TermModes &modes) -> std::string {
    if (form.tilde != 0) {
        std::string out{kEscape, '['};
        out += std::to_string(form.tilde);
        if (mask > 1) {
            out += ';';
            out += std::to_string(mask);
        }
        out += '~';
        return out;
    }
    const char letter = static_cast<char>(form.letter);
    const bool ss3 =
        form.base == LetterBase::Ss3 || (form.base == LetterBase::App && modes.cursor_key_app);
    if (mask == 1 && ss3) {
        return std::string{kEscape, 'O', letter};
    }
    std::string out{kEscape, '['};
    if (mask > 1) {
        out += "1;";
        out += std::to_string(mask);
    }
    out += letter;
    return out;
}

}  // namespace

auto encode_key(const KeyPress &press, const TermModes &modes) -> std::optional<std::string> {
    switch (press.sym) {
        case KeySym::Unknown:
        case KeySym::Shift:
        case KeySym::Control:
        case KeySym::Alt:
        case KeySym::Meta:
            return std::nullopt;  // 未映射键与「按下修饰键本身」都不发东西
        default:
            break;
    }
    const bool alt = press.alt || press.meta;
    const int mask = 1 + (press.shift ? 1 : 0) + (alt ? 2 : 0) + (press.control ? 4 : 0);

    if (press.control) {
        // Ctrl+Alt（含 Meta）系无遗留编码，且这类组合正是 UI 快捷键的常用档：交回框架的快捷键层
        // （它在派发到控件之前拦截），冲突由用户改绑裁决——`SPEC.FEAT.INTERACT.01`、架构 §9.6。
        if (alt) {
            return std::nullopt;
        }
        if (const auto form = special_form(press.sym)) {
            return encode_special(*form, mask, modes);  // Ctrl+方向键一类走带参数的 CSI 形态
        }
        return control_form(press.sym, press.shift);
    }

    if (const auto form = special_form(press.sym)) {
        return encode_special(*form, mask, modes);
    }
    if (const auto plain = plain_form(press.sym, press.shift)) {
        if (alt) {
            return std::string{kEscape} + *plain;  // metaSendsEscape 档：Alt/Meta 前缀一个 ESC
        }
        return plain;
    }
    if (const auto pair = printable(press.sym)) {
        if (alt) {
            std::string out{kEscape};
            static_cast<void>(append_utf8(press.shift ? pair->shifted : pair->plain, out));
            return out;
        }
        return std::nullopt;  // 无 Alt/Meta 的可打印键归文本通道（真实大小写与布局只有它知道）
    }
    return std::nullopt;
}

}  // namespace borealis::term
