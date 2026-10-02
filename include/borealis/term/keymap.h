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
// - **数字小键盘随框架回货而可表达**（附录 A.2 的 G16 已闭合）：`KP_*` 键码齐备、NumLock 取
//   修饰位（`KeyPress::num_lock`），`DECKPAM`/`DECKPNM` 两档都有可发之键。口径：应用模式走 SS3
//   数值族（`ESC O p`..`y` 与 `n`/`o`/`j`/`m`/`k`+`l`，照 PuTTY xterm-funky 档的
//   `format_numeric_keypad_key`），常规模式且 NumLock 开时本层不发（字符归文本通道），常规模式
//   且 NumLock 关时把 KP 键折回主键盘等价键位再走既有编码表。SS3 形态没有修饰参数位，故带修饰
//   的数值族键**丢掉修饰**而不是自造形态。
// - **`KP_Enter` 无从单独编码**：框架把小键盘回车并入 `KeyCode::Enter`（keycode.h 的既有决定），
//   故本层发出的仍是主回车的 `\r` 而非应用模式的 `ESC O M`——这是框架键码合并的后果，非本层选择。
// - **Win32 的导航区判别不全**：小键盘导航六键里只有 `KP_Home` 真能与主键盘分开（其余共用虚拟键码），
//   故 NumLock 关闭时主键盘那六个键照常走主键盘形态，本层的降级表只在 `KP_*` 码位上生效。
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

    // ---- 数字小键盘（Keypad）----
    //
    // 与 `aurora::KeyCode` 的 KP_* 段逐值对齐，段首同样写死 100：本段的纪律是「只许往后接、不得
    // 插在中间」，因为消费方持有逐值对齐的 `static_cast`（`src/ui/terminal_view.cpp` 的互转点）。
    // `KP_Enter` 不在此列——框架把它并入 `Enter`，本层因此无从单独编码。
    KP_Insert = 100,   ///< 小键盘 Insert（导航区）。
    KP_Delete,         ///< 小键盘 Delete（导航区）。
    KP_Begin,          ///< 小键盘 5 无 NumLock 时的位（无遗留编码，本层不发）。
    KP_End,            ///< 小键盘 End（导航区）。
    KP_Home,           ///< 小键盘 Home（导航区；Win32 上唯一能与主键盘区分的导航键）。
    KP_Prior,          ///< 小键盘 PageUp（导航区）。
    KP_Next,           ///< 小键盘 PageDown（导航区）。
    KP_Add,            ///< 小键盘 `+`。
    KP_Subtract,       ///< 小键盘 `-`。
    KP_Multiply,       ///< 小键盘 `*`。
    KP_Divide,         ///< 小键盘 `/`。
    KP_Decimal,        ///< 小键盘小数点。
    KP_Separator,      ///< 小键盘分隔符（部分布局的次级位；无遗留编码，本层不发）。
    KP_0,              ///< 小键盘数字 0。
    KP_1,              ///< 小键盘数字 1。
    KP_2,              ///< 小键盘数字 2。
    KP_3,              ///< 小键盘数字 3。
    KP_4,              ///< 小键盘数字 4。
    KP_5,              ///< 小键盘数字 5。
    KP_6,              ///< 小键盘数字 6。
    KP_7,              ///< 小键盘数字 7。
    KP_8,              ///< 小键盘数字 8。
    KP_9,              ///< 小键盘数字 9。
};

/// @brief 一次按键的输入语义单元（修饰态取「该条事件携带的组合」，非独立的键盘状态）。
struct KeyPress {
    KeySym sym = KeySym::Unknown;  ///< 逻辑键位。
    bool shift = false;            ///< Shift 位：决定可打印键的移位字符与 CSI 形态的修饰参数。
    bool control = false;          ///< Ctrl 位。
    bool alt = false;              ///< Alt 位。
    bool meta = false;             ///< Meta 位；与 `alt` 同口径（见文件头注释）。
    bool num_lock = false;         ///< NumLock 锁定态：只在 `KP_*` 键的编码里起作用（框架建模为修饰位）。
};

/// @brief 把一次按键编码成要发给会话的 VT 字节。
///
/// 纯函数：同一入参恒得同一输出，故全部编码表可在无 UI 环境里逐条断言。
/// @param press 按键与其修饰态。
/// @param modes 终端此刻的模式快照——参与编码的是 `cursor_key_app`（DECCKM）与
///              `application_keypad`（DECKPAM）：前者让方向键与 Home/End 走 SS3 而非 CSI，
///              后者决定小键盘发数值族还是让位文本通道（`SPEC.FEAT.INTERACT.01` 的「须生效」腿）。
/// @return 待发送字节；空值表示**本层不编码**（键本身无输入语义、或该组合无遗留编码、
///         或该键的可打印形态归文本输入通道）。
[[nodiscard]] auto encode_key(const KeyPress &press, const TermModes &modes) -> std::optional<std::string>;

/// @brief 本键位是否属数字小键盘段（`KP_*`）。
///
/// 视口据此决定要不要吞掉紧随本次按键的那一条文本事件：Windows 无论 `DECKPAM` 与否都给小键盘
/// 数字发 `WM_CHAR`，应用模式下本层已把它编码成 SS3 数值族，文本通道再发一次字符即双发上屏。
/// 段判据只有本件知道（`KeySym` 与 `KeyCode` 逐值对齐，段界是它的实现细节），故不叫调用方写魔数。
[[nodiscard]] auto is_keypad(KeySym sym) noexcept -> bool;

}  // namespace borealis::term
