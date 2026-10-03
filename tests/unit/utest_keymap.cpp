/// 测试类型: unit
/// 目标单元: include/borealis/term/keymap.h + src/term/keymap.cpp
/// 测试说明: 键盘映射的编码表（`SPEC.FEAT.INTERACT.01`）：控制字符推导与大写折叠、DECCKM 切换
///           方向键与 Home/End 的 SS3/CSI 基形态、功能键与编辑键的 xterm 形态、修饰参数的
///           `1 + shift + alt*2 + ctrl*4` 掩码、Alt 与 Meta 同走 ESC 前缀、Ctrl+Alt 系交回快捷键层、
///           可打印键在无 Alt/Meta 时让位文本通道，小键盘两档（DECKPAM 的 SS3 数值族与常规模式按
///           NumLock 分派的导航/让位），以及本层键位枚举与框架 `KeyCode` 的取值对齐
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

/// @brief 直接给按键与模式的入口：小键盘的用例布尔成组，用位置参数会读不成句。
[[nodiscard]] auto encode_with(const KeyPress &press, const TermModes &modes) -> std::string {
    const auto out = encode_key(press, modes);
    return out.value_or(std::string{});
}

/// @brief 编码结果的可比形态：`encode_key` 从不出产空串，故空串恰好表示「本层不编码」。
[[nodiscard]] auto enc(KeySym sym, bool shift = false, bool control = false, bool alt = false,
                       bool meta = false, bool cursor_key_app = false) -> std::string {
    auto modes = TermModes{};
    modes.cursor_key_app = cursor_key_app;
    return encode_with(KeyPress{.sym = sym, .shift = shift, .control = control, .alt = alt, .meta = meta},
                       modes);
}

/// @brief 小键盘的编码入口：`num_lock` 取框架的修饰位口径（按键事件携带的锁定态）。
[[nodiscard]] auto kp(KeySym sym, bool num_lock, bool app_keypad, bool shift = false,
                      bool cursor_key_app = false, bool control = false, bool alt = false) -> std::string {
    const auto modes = TermModes{.cursor_key_app = cursor_key_app, .application_keypad = app_keypad};
    return encode_with(KeyPress{.sym = sym,
                                 .shift = shift,
                                 .control = control,
                                 .alt = alt,
                                 .num_lock = num_lock},
                       modes);
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

    // 小键盘段逐项锁：框架把段首写死成 100 并只许往后追加，本层跟着镜像；顺序错一格就会把
    // 小键盘的某个键译成另一个键（`static_cast` 不会报错，只有这里能发现）。
    const auto lock = [](KeySym sym, aurora::KeyCode code) {
        AURORA_TEST_CHECK_EQ(static_cast<int>(sym), static_cast<int>(code));
    };
    lock(KeySym::KP_Insert, aurora::KeyCode::KP_Insert);
    lock(KeySym::KP_Delete, aurora::KeyCode::KP_Delete);
    lock(KeySym::KP_Begin, aurora::KeyCode::KP_Begin);
    lock(KeySym::KP_End, aurora::KeyCode::KP_End);
    lock(KeySym::KP_Home, aurora::KeyCode::KP_Home);
    lock(KeySym::KP_Prior, aurora::KeyCode::KP_Prior);
    lock(KeySym::KP_Next, aurora::KeyCode::KP_Next);
    lock(KeySym::KP_Add, aurora::KeyCode::KP_Add);
    lock(KeySym::KP_Subtract, aurora::KeyCode::KP_Subtract);
    lock(KeySym::KP_Multiply, aurora::KeyCode::KP_Multiply);
    lock(KeySym::KP_Divide, aurora::KeyCode::KP_Divide);
    lock(KeySym::KP_Decimal, aurora::KeyCode::KP_Decimal);
    lock(KeySym::KP_Separator, aurora::KeyCode::KP_Separator);
    lock(KeySym::KP_0, aurora::KeyCode::KP_0);
    lock(KeySym::KP_1, aurora::KeyCode::KP_1);
    lock(KeySym::KP_2, aurora::KeyCode::KP_2);
    lock(KeySym::KP_3, aurora::KeyCode::KP_3);
    lock(KeySym::KP_4, aurora::KeyCode::KP_4);
    lock(KeySym::KP_5, aurora::KeyCode::KP_5);
    lock(KeySym::KP_6, aurora::KeyCode::KP_6);
    lock(KeySym::KP_7, aurora::KeyCode::KP_7);
    lock(KeySym::KP_8, aurora::KeyCode::KP_8);
    lock(KeySym::KP_9, aurora::KeyCode::KP_9);
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::KP_Insert), 100);  // 段首锚点：两侧都写死的值

    // 主键盘 `Insert` 是框架的**后补显式初值段**（G20），取 `KP_9`（122）之后的下一格而非插回
    // 「编辑/导航」段——插回中间会让其后全部取值整体位移，而互转即 `static_cast`，不报错只错位。
    lock(KeySym::Insert, aurora::KeyCode::Insert);
    AURORA_TEST_CHECK_EQ(static_cast<int>(KeySym::Insert), 123);  // 段首锚点：两侧都写死的值
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
    // 主键盘 `Insert` 与 `KP_Insert` 是同一波浪号族的 `2` 号位（框架 G20 回货；Win32 / GLFW 的
    // 小键盘导航区也恒给主档，故本档才是该键在三后端上的唯一产出形态）。
    AURORA_TEST_CHECK_EQ(enc(KeySym::Insert), esc("[2~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::PageUp), esc("[5~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::PageDown), esc("[6~"));

    // 字母族带修饰把编码号换成 `1;mask` 前缀，波浪号族在编码号后追加 `;mask`。
    AURORA_TEST_CHECK_EQ(enc(KeySym::F1, true), esc("[1;2P"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::F4, false, true), esc("[1;5S"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Delete, true), esc("[3;2~"));
    AURORA_TEST_CHECK_EQ(enc(KeySym::Insert, true), esc("[2;2~"));    // Shift+Insert（xterm 的 paste 位）
    AURORA_TEST_CHECK_EQ(enc(KeySym::Insert, false, true), esc("[2;5~"));
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

AURORA_TEST_CASE(keypad_in_application_mode_uses_the_ss3_numeric_family) {
    // 应用模式（`ESC =`，DECKPAM）：数字阵 `0`–`9` 是 SS3 的 `p`–`y`，小数点 `n`，四则运算
    // `/` `*` `-` `+` 是 `o` `j` `m` `k`（表照 PuTTY 的 xterm-funky 档）。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_0, true, true), esc("Op"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_1, true, true), esc("Oq"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_7, true, true), esc("Ow"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_9, true, true), esc("Oy"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Decimal, true, true), esc("On"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Divide, true, true), esc("Oo"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Multiply, true, true), esc("Oj"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Subtract, true, true), esc("Om"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Add, true, true), esc("Ok"));
    // `+` 覆盖的位置在 VT100 上是两个键，故由 Shift 二选一（`k` / `l`）。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Add, true, true, true), esc("Ol"));
    // 运算键与 NumLock 无关：它们在平台上本就是运算键位，只有数字阵随锁定态改语义。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Add, false, true), esc("Ok"));
    // SS3 形态没有修饰参数位：除 Shift 外的修饰只能丢掉，而不是自造一套形态。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_7, true, true, false, false, false, true), esc("Ow"));
    // 导航区与 Begin/分隔符不受 DECKPAM 影响：前者按导航表，后者无遗留形态。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Home, true, true), esc("[H"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Begin, true, true), std::string{});
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Separator, true, true), std::string{});
    // Ctrl+Alt 系归快捷键层，小键盘也一样不编码。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_7, true, true, false, false, true, true), std::string{});
}

AURORA_TEST_CASE(keypad_in_numeric_mode_with_numlock_defers_to_the_text_channel) {
    // 常规模式（`ESC >`）且 NumLock 开：数字与运算字符的真实形态归文本通道，本层一发就双份上屏。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_7, true, false), std::string{});
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_0, true, false), std::string{});
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Decimal, true, false), std::string{});
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Add, true, false), std::string{});
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Divide, true, false), std::string{});
    // 导航区六键与锁定态无关：它们没有字符形态，本就由本层编码。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Insert, true, false), esc("[2~"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Delete, true, false), esc("[3~"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Prior, true, false), esc("[5~"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Next, true, false), esc("[6~"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_End, true, false), esc("[F"));
}

AURORA_TEST_CASE(keypad_navigation_when_numlock_is_off) {
    // NumLock 关时数字阵是 5x3 导航阵（0=Ins、1=End、2=下、3=PgDn、4=左、5=Begin、6=右、
    // 7=Home、8=上、9=PgUp、.=Del）：折回主键盘等价形态，而不是新表。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_0, false, false), esc("[2~"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_1, false, false), esc("[F"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_2, false, false), esc("[B"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_3, false, false), esc("[6~"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_4, false, false), esc("[D"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_5, false, false), std::string{});  // Begin 无遗留形态
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_6, false, false), esc("[C"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_7, false, false), esc("[H"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_8, false, false), esc("[A"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_9, false, false), esc("[5~"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Decimal, false, false), esc("[3~"));
    // 折回的是同一张表，故 DECCKM 与修饰参数一并生效。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_7, false, false, false, true), esc("OH"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_8, false, false, true), esc("[1;2A"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Home, false, false, false, false, true), esc("[1;5H"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_3, false, false, false, false, false, true), esc("[6;3~"));
    // 导航区六键同形（锁定态不改它们）。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Home, false, false), esc("[H"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_End, false, false), esc("[F"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Insert, false, false), esc("[2~"));
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Next, false, false), esc("[6~"));
    // 常规模式下运算键无导航形态：不发（遗留档没有它们的编码）。
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Add, false, false), std::string{});
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Subtract, false, false), std::string{});
    AURORA_TEST_CHECK_EQ(kp(KeySym::KP_Begin, false, false), std::string{});
}

AURORA_TEST_CASE(keypad_segment_predicate_covers_only_the_keypad_range) {
    // 视口用它决定「本层已发字节，紧随其后的文本事件要吞一次」，故段界必须精确。
    AURORA_TEST_CHECK(term::is_keypad(KeySym::KP_Insert));
    AURORA_TEST_CHECK(term::is_keypad(KeySym::KP_0));
    AURORA_TEST_CHECK(term::is_keypad(KeySym::KP_9));
    AURORA_TEST_CHECK(!term::is_keypad(KeySym::D0));
    AURORA_TEST_CHECK(!term::is_keypad(KeySym::Delete));
    AURORA_TEST_CHECK(!term::is_keypad(KeySym::F12));
    AURORA_TEST_CHECK(!term::is_keypad(KeySym::Unknown));
    AURORA_TEST_CHECK(!term::is_keypad(static_cast<KeySym>(99)));    // 段前一格
    AURORA_TEST_CHECK(!term::is_keypad(KeySym::Insert));             // 段后一格（G20 回货的主键盘档，走波浪号族）
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
