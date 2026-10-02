#pragma once

// ============================================================
// 键盘输入映射（include/borealis/term/keymap.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.01` 的编码侧：把「哪个逻辑键 + 哪些修饰键 + 终端此刻的模式」翻译成
// 要写进会话的 VT 字节。这里只有编码表与推导规则，不触达框架事件类型——`aurora::KeyCode`
// 到 `term::KeySym` 的互转发生在绘制侧那一个翻译单元（架构 §2.3 的「公共头不含 Aurora 类型」）。
//
// 编码口径取遗留（xterm 兼容）形态，理由与边界：
// - **可打印字符不经本层**：无 Ctrl/Alt 的可打印键由框架的文本输入通道给出真实字符（大小写、
//   布局、输入法都归它），本层对这类按键返回空。控制字符在 Win32 后端刻意不进文本通道
//   （`ch < 0x20` 被丢弃并交回 `KeyEvent`），故 `Ctrl+字母` 一类必须由本层编码，不会双发。
// - **kitty keyboard protocol / `modifyOtherKeys` 是延后观察项**（需求原文），故无修饰的
//   功能键以外的组合只在「有遗留标准编码」时产出，其余返回空而非自造编码。
// - **Alt 与 Meta 同口径**（ESC 前缀 / 修饰位取 alt 位）：遗留编码不区分二者，xterm 把 Super
//   也归入 meta；需求要求「完整转发 Meta 组合键」，取同一形态是唯一的可编码选择。
// - **数字小键盘无从表达**：框架键码模型未收录 KP 键与 NumLock 态（`aurora/event/keycode.h`
//   的注释明写 KP_0-9 落 `Unknown`），故 `DECKPAM`/`DECKPNM` 在本棒只有模式位可读而无可发之键
//   （登记为附录 A.2 的 G16）。
// ============================================================

#include <cstdint>
#include <optional>
#include <string>

#include "borealis/term/terminal.h"

namespace borealis::term {

/// @brief 终端输入侧关心的逻辑键位。
///
/// **取值与 `aurora::KeyCode` 逐一对齐**，互转即一次 `static_cast`；对齐由单元用例逐条断言，
/// 故框架侧若插入新键位使两者错位，测试即失败而不是静默把键译成另一个键。
enum class KeySym : int {  // NOLINT(*-enum-size)
    Unknown = 0,  ///< 未映射键：本层不编码。

    A,  ///< 字母键 A（物理键位，大小写共用）。
    B,  ///< 字母键 B。
    C,  ///< 字母键 C。
    D,  ///< 字母键 D。
    E,  ///< 字母键 E。
    F,  ///< 字母键 F。
    G,  ///< 字母键 G。
    H,  ///< 字母键 H。
    I,  ///< 字母键 I。
    J,  ///< 字母键 J。
    K,  ///< 字母键 K。
    L,  ///< 字母键 L。
    M,  ///< 字母键 M。
    N,  ///< 字母键 N。
    O,  ///< 字母键 O。
    P,  ///< 字母键 P。
    Q,  ///< 字母键 Q。
    R,  ///< 字母键 R。
    S,  ///< 字母键 S。
    T,  ///< 字母键 T。
    U,  ///< 字母键 U。
    V,  ///< 字母键 V。
    W,  ///< 字母键 W。
    X,  ///< 字母键 X。
    Y,  ///< 字母键 Y。
    Z,  ///< 字母键 Z。

    D0,  ///< 主键盘数字行 0。
    D1,  ///< 主键盘数字行 1。
    D2,  ///< 主键盘数字行 2。
    D3,  ///< 主键盘数字行 3。
    D4,  ///< 主键盘数字行 4。
    D5,  ///< 主键盘数字行 5。
    D6,  ///< 主键盘数字行 6。
    D7,  ///< 主键盘数字行 7。
    D8,  ///< 主键盘数字行 8。
    D9,  ///< 主键盘数字行 9。

    Escape,  ///< Esc 键。
    Enter,   ///< 回车键（框架侧把小键盘回车并入本键）。
    Tab,     ///< Tab 键。
    Backspace,  ///< 退格键。
    Delete,  ///< Delete 键（向前删除）。
    Space,   ///< 空格键。

    ArrowLeft,   ///< 左方向键。
    ArrowRight,  ///< 右方向键。
    ArrowUp,     ///< 上方向键。
    ArrowDown,   ///< 下方向键。

    Shift,   ///< Shift 修饰键自身：按下它不产生输入。
    Control, ///< Ctrl 修饰键自身。
    Alt,     ///< Alt 修饰键自身。
    Meta,    ///< 系统键自身（Windows 键 / Super / Cmd）。

    Home,     ///< Home 键。
    End,      ///< End 键。
    PageUp,   ///< PageUp 键。
    PageDown, ///< PageDown 键。

    Minus,       ///< `-` / `_`。
    Equal,       ///< `=` / `+`。
    LeftBracket, ///< `[` / `{`。
    RightBracket,///< `]` / `}`。
    Backslash,   ///< `\` / `|`。
    Semicolon,   ///< `;` / `:`。
    Quote,       ///< `'` / `"`。
    Comma,       ///< `,` / `<`。
    Period,      ///< `.` / `>`。
    Slash,       ///< `/` / `?`。
    Backquote,   ///< `` ` `` / `~`。

    F1,   ///< 功能键 F1。
    F2,   ///< 功能键 F2。
    F3,   ///< 功能键 F3。
    F4,   ///< 功能键 F4。
    F5,   ///< 功能键 F5。
    F6,   ///< 功能键 F6。
    F7,   ///< 功能键 F7。
    F8,   ///< 功能键 F8。
    F9,   ///< 功能键 F9。
    F10,  ///< 功能键 F10。
    F11,  ///< 功能键 F11。
    F12,  ///< 功能键 F12。
};

/// @brief 一次按键的输入语义单元（修饰态取「该条事件携带的组合」，非独立的键盘状态）。
struct KeyPress {
    KeySym sym = KeySym::Unknown;  ///< 逻辑键位。
    bool shift = false;            ///< Shift 位：决定可打印键的移位字符与 CSI 形态的修饰参数。
    bool control = false;          ///< Ctrl 位。
    bool alt = false;              ///< Alt 位。
    bool meta = false;             ///< Meta 位；与 `alt` 同口径（见文件头注释）。
};

/// @brief 把一次按键编码成要发给会话的 VT 字节。
///
/// 纯函数：同一入参恒得同一输出，故全部编码表可在无 UI 环境里逐条断言。
/// @param press 按键与其修饰态。
/// @param modes 终端此刻的模式快照——只有 `cursor_key_app`（DECCKM）参与编码：应用模式下方向键
///              与 Home/End 走 SS3 而非 CSI（`SPEC.FEAT.INTERACT.01` 的「须生效」腿）。
/// @return 待发送字节；空值表示**本层不编码**（键本身无输入语义、或该组合无遗留编码、
///         或该键的可打印形态归文本输入通道）。
[[nodiscard]] auto encode_key(const KeyPress &press, const TermModes &modes) -> std::optional<std::string>;

}  // namespace borealis::term
