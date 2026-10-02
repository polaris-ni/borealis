/// 测试类型: unit
/// 目标单元: include/borealis/term/keymap.h + src/term/keymap.cpp
/// 测试说明: 键盘映射的编码表（`SPEC.FEAT.INTERACT.01`）：控制字符推导与大写折叠、DECCKM 切换
///           方向键与 Home/End 的 SS3/CSI 基形态、功能键与编辑键的 xterm 形态、修饰参数的
///           `1 + shift + alt*2 + ctrl*4` 掩码、Alt 与 Meta 同走 ESC 前缀、Ctrl+Alt 系交回快捷键层、
///           可打印键在无 Alt/Meta 时让位文本通道，以及本层键位枚举与框架 `KeyCode` 的取值对齐
///           （互转是一次 `static_cast`，错位即译成另一个键，故逐条锁住）。

#include <cstdint>
#include <string>
#include <string_view>

#include "aurora/event/keycode.h"
#include "borealis/term/keymap.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_keymap {

namespace {

using borealis::term::encode_key;
using borealis::term::KeyPress;
using borealis::term::KeySym;
using borealis::term::TermModes;

/// @brief 编码结果的可比形态：`encode_key` 从不出产空串，故空串恰好表示「本层不编码」。
[[nodiscard]] auto enc(KeySym sym, bool shift = false, bool control = false, bool alt = false,
                       bool meta = false, bool cursor_key_app = false) -> std::string {
    auto modes = TermModes{};
    modes.cursor_key_app = cursor_key_app;
    const auto out =
        encode_key(KeyPress{.sym = sym, .shift = shift, .control = control, .alt = alt, .meta = meta}, modes);
    return out.value_or(std::string{});
}

/// @brief 单字节序列（控制字符一档）。
[[nodiscard]] auto one(char byte) -> std::string { return std::string(1, byte); }

/// @brief 转义开头的序列。
[[nodiscard]] auto esc(std::string_view body) -> std::string { return std::string("\x1B") + std::string(body); }

}  // namespace

AURORA_TEST_CASE(key_sym_values_are_aligned_with_the_framework_key_codes) {
    // 绘制侧的互转是 `static_cast<int>`，因此两侧必须逐值相等；框架插入新键位就会在这里错位。
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Unknown), static_cast<int>(aurora::KeyCode::Unknown));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::A), static_cast<int>(aurora::KeyCode::A));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Z), static_cast<int>(aurora::KeyCode::Z));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::D0), static_cast<int>(aurora::KeyCode::D0));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::D9), static_cast<int>(aurora::KeyCode::D9));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Escape), static_cast<int>(aurora::KeyCode::Escape));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Enter), static_cast<int>(aurora::KeyCode::Enter));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Tab), static_cast<int>(aurora::KeyCode::Tab));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Backspace), static_cast<int>(aurora::KeyCode::Backspace));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Delete), static_cast<int>(aurora::KeyCode::Delete));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Space), static_cast<int>(aurora::KeyCode::Space));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::ArrowLeft), static_cast<int>(aurora::KeyCode::ArrowLeft));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::ArrowRight), static_cast<int>(aurora::KeyCode::ArrowRight));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::ArrowUp), static_cast<int>(aurora::KeyCode::ArrowUp));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::ArrowDown), static_cast<int>(aurora::KeyCode::ArrowDown));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Shift), static_cast<int>(aurora::KeyCode::Shift));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Control), static_cast<int>(aurora::KeyCode::Control));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Alt), static_cast<int>(aurora::KeyCode::Alt));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Meta), static_cast<int>(aurora::KeyCode::Meta));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Home), static_cast<int>(aurora::KeyCode::Home));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::End), static_cast<int>(aurora::KeyCode::End));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::PageUp), static_cast<int>(aurora::KeyCode::PageUp));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::PageDown), static_cast<int>(aurora::KeyCode::PageDown));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Minus), static_cast<int>(aurora::KeyCode::Minus));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Equal), static_cast<int>(aurora::KeyCode::Equal));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::LeftBracket), static_cast<int>(aurora::KeyCode::LeftBracket));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::RightBracket), static_cast<int>(aurora::KeyCode::RightBracket));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Backslash), static_cast<int>(aurora::KeyCode::Backslash));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Semicolon), static_cast<int>(aurora::KeyCode::Semicolon));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Quote), static_cast<int>(aurora::KeyCode::Quote));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Comma), static_cast<int>(aurora::KeyCode::Comma));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Period), static_cast<int>(aurora::KeyCode::Period));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Slash), static_cast<int>(aurora::KeyCode::Slash));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Backquote), static_cast<int>(aurora::KeyCode::Backquote));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::F1), static_cast<int>(aurora::KeyCode::F1));
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::F12), static_cast<int>(aurora::KeyCode::F12));
}

AURORA_TEST_CASE(control_keys_produce_the_legacy_control_bytes) {
    AURORA_TEST_CHECK_EQ(enc(KeySym::C, false, true), one('\x03'));  // Ctrl+C
    AURORA_TEST_CHECK_EQ(enc(KeySym::Z, false, true), one('\x1A'));  // Ctrl+Z
    AURORA_TEST_CHECK_EQ(enc(KeySym::A, false, true), one('\x01'));
    // 大写折叠：Shift 改的是字符而非控制码，Ctrl+Shift+A 仍是 0x01。
    AURORA_TEST_CHECK_EQ(enc(KeySym::A, true, true), one('\x01'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Space, false, true), one('\x00'));  // Ctrl+Space = NUL
    AURORA_TEST_CHECK_EQ(enc(KeySym::LeftBracket, false, true), one('\x1B'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Backslash, false, true), one('\x1C'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::RightBracket, false, true), one('\x1D'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::D6, true, true), one('\x1E'));   // Ctrl+Shift+6 = Ctrl+^
    AURORA_TEST_CHECK_EQ(enc(KeySym::Minus, true, true), one('\x1F'));  // Ctrl+Shift+- = Ctrl+_
    AURORA_TEST_CHECK_EQ(enc(KeySym::D2, true, true), one('\x00'));   // Ctrl+Shift+2 = Ctrl+@
    AURORA_TEST_CHECK_EQ(enc(KeySym::Enter, false, true), one('\r'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Backspace, false, true), one('\x08'));
    // 区间外的标点与 Tab/Esc 的 Ctrl 形态无遗留编码：交回快捷键层，不自造（kitty 档属延后项）。
    AURORA_TEST_CHECK_EQ(enc(KeySym::Slash, false, true), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Semicolon, true, true), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Tab, false, true), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Escape, false, true), std::string{});
    // Ctrl+Alt 系是 UI 快捷键的常用档，一律不编码（冲突由配置改绑裁决）。
    AURORA_TEST_CHECK_EQ(enc(KeySym::C, false, true, true), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowRight, false, true, true), std::string{});
}

AURORA_TEST_CASE(cursor_keys_switch_between_csi_and_ss3_with_decckm) {
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowUp), esc("[A"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowDown), esc("[B"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowRight), esc("[C"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowLeft), esc("[D"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Home), esc("[H"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::End), esc("[F"));

    // DECCKM（`CSI ? 1 h`）应用模式：方向键与 Home/End 改走 SS3。
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowUp, false, false, false, false, true), esc("OA"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowLeft, false, false, false, false, true), esc("OD"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Home, false, false, false, false, true), esc("OH"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::End, false, false, false, false, true), esc("OF"));

    // 有修饰时回到 CSI 形态并带修饰参数（应用模式也一样）：掩码 1 + shift + alt*2 + ctrl*4。
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowUp, true), esc("[1;2A"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowUp, false, true), esc("[1;5A"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowUp, false, false, true), esc("[1;3A"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowUp, true, true), esc("[1;6A"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Home, true, true), esc("[1;6H"));  // Shift+Ctrl：掩码 1+1+4
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowDown, true, false, false, false, true), esc("[1;2B"));
}

AURORA_TEST_CASE(function_and_editing_keys_use_the_xterm_forms) {
    AURORA_TEST_CHECK_EQ(enc(KeySym::F1), esc("OP"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F2), esc("OQ"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F3), esc("OR"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F4), esc("OS"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F5), esc("[15~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F6), esc("[17~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F7), esc("[18~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F8), esc("[19~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F9), esc("[20~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F10), esc("[21~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F11), esc("[23~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F12), esc("[24~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Delete), esc("[3~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::PageUp), esc("[5~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::PageDown), esc("[6~"));

    // 字母族带修饰把编码号换成 `1;mask` 前缀，波浪号族在编码号后追加 `;mask`。
    AURORA_TEST_CHECK_EQ(enc(KeySym::F1, true), esc("[1;2P"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F4, false, true), esc("[1;5S"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Delete, true), esc("[3;2~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::PageUp, false, true), esc("[5;5~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F12, true, false, true), esc("[24;4~"));  // Shift+Alt：掩码 1+1+2
    // 应用模式不影响 F1–F4（它们与 DECCKM 无关），也不影响波浪号族。
    AURORA_TEST_CHECK_EQ(enc(KeySym::F1, false, false, false, false, true), esc("OP"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Delete, false, false, false, false, true), esc("[3~"));
}

AURORA_TEST_CASE(alt_and_meta_both_prefix_one_escape) {
    AURORA_TEST_CHECK_EQ(enc(KeySym::A, false, false, true), esc("a"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::A, true, false, true), esc("A"));  // 移位字符照真实字符给
    AURORA_TEST_CHECK_EQ(enc(KeySym::Enter, false, false, true), esc("\r"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Backspace, false, false, true), std::string("\x1B\x7F"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Escape, false, false, true), std::string("\x1B\x1B"));
    // Meta 与 Alt 同口径：遗留编码不区分二者，而需求要求 Meta 组合也转发。
    AURORA_TEST_CHECK_EQ(enc(KeySym::A, false, false, false, true), enc(KeySym::A, false, false, true));
    AURORA_TEST_CHECK_EQ(enc(KeySym::ArrowLeft, false, false, false, true), esc("[1;3D"));
    // 编辑/功能键的 Alt 形态走掩码参数，不再叠 ESC 前缀（否则同一修饰有两种写法）。
    AURORA_TEST_CHECK_EQ(enc(KeySym::PageUp, false, false, true), esc("[5;3~"));
}

AURORA_TEST_CASE(printable_keys_without_alt_defer_to_the_text_channel) {
    // 真实字符（大小写、键盘布局、输入法）只有框架的文本输入通道知道，本层不得替它决定。
    AURORA_TEST_CHECK_EQ(enc(KeySym::A), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::A, true), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::D1), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Slash, true), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Space), std::string{});

    // 非字符键仍由本层给：回车是 CR，退格是 DEL（不是 BS），Esc 是 ESC。
    AURORA_TEST_CHECK_EQ(enc(KeySym::Enter), one('\r'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Enter, true), one('\r'));  // Shift+Enter 无独立遗留编码
    AURORA_TEST_CHECK_EQ(enc(KeySym::Backspace), one('\x7F'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Escape), one('\x1B'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Tab), one('\t'));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Tab, true), esc("[Z"));  // 反向 Tab
}

AURORA_TEST_CASE(modifier_only_and_unmapped_keys_produce_nothing) {
    AURORA_TEST_CHECK_EQ(enc(KeySym::Shift), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Control), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Alt), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Meta), std::string{});
    AURORA_TEST_CHECK_EQ(enc(KeySym::Unknown), std::string{});
    // 框架未建模的键（数字小键盘一类）落到映射之外的整数值：同样不得产出字节。
    AURORA_TEST_CHECK_EQ(enc(static_cast<KeySym>(4096)), std::string{});
}

}  // namespace borealis::test_cases::utest_keymap
