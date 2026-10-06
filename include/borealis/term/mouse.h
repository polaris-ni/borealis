#pragma once

// ============================================================
// 鼠标上报编码（include/borealis/term/mouse.h）
// ------------------------------------------------------------
// `SPEC.FEAT.TERM.06` 的编码侧：把「此刻哪一档上报模式开着 + 哪一个指针/滚轮事件 + 落在可见区
// 哪一格」翻译成要写进会话的 VT 字节。与 `term::encode_key` 同一分工（架构 §10.2）：这里只有
// 编码表与推导规则，不触达框架事件类型，也不判「这一击该归上报还是该归本地交互」——那条分流是
// 视口的职责（裁决 7.77①②③④）。
//
// 五条口径的由来：
// - **上报档位是层级而不是并列开关**：`?9`(X10) ⊂ `?1000`(普通) ⊂ `?1002`(按钮事件) ⊂
//   `?1003`(任意事件)，四档取**最高**那一档生效（xterm 内部亦记为一个 level），故 `?1002` 单独
//   置位也报按下与松开，而不是只报拖动。`?1006`(SGR 扩展) 与这四档**正交**：它只换编码形态，
//   不改变「报哪些事件」。
// - **右键不在本表**：本仓把右键留给本地三态（复制 / 粘贴 / 菜单，`SPEC.FEAT.INTERACT.03`），
//   故视口永远不会向编码层递来一次右键（裁决 7.77②）。留着一格永不命中的按钮 2 就是人工制品。
// - **Shift 位不进编码**：它是「让位本地」的覆盖键（裁决 7.77①，与 xterm 及业界终端同口径：按着
//   Shift 的点击绕过上报、由终端自己做选择），于是带 Shift 的组合从不出现在要发的字节里。Ctrl 与
//   Alt 照 xterm 取 +16 与 +8；Alt 与 Meta 同口径（`encode_key` 那条），故只有一个 `alt` 字段。
// - **遗留档的坐标钳到 223**：`CSI M` 的行列各占一个字节且要 +32，故 1-based 行列的可表达上界是
//   223。超出的那一档在 xterm 由 `?1005`（UTF-8 坐标）/ `?1015`（urxvt 坐标）扩展，两档都**不在
//   需求原文列出的四档之内**，故不登记、按钳位处理（SGR 档无此限制，走的是十进制文本参数）。
// - **横向滚轮（按钮 66/67）不建模**：本仓视口不消费 `ScrollEvent::delta_x`，需求原文的验收面是
//   「`vim`/`htop` 内滚轮与点击可用」那一族纵向事件。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "borealis/term/terminal.h"

namespace borealis::term {

/// @brief 当前生效的鼠标上报层级（四档取最高）。
enum class MouseTracking : std::uint8_t {
    Off,           ///< 四档全关：指针与滚轮事件全归本地交互。
    X10,           ///< `?9`：只报按下。
    PressRelease,  ///< `?1000`：按下与松开都报。
    ButtonEvents,  ///< `?1002`：再加按住拖动。
    AnyEvents,     ///< `?1003`：再加无键悬停移动。
};

/// @brief 一次指针/滚轮事件在上报协议里的阶段。
enum class MousePhase : std::uint8_t {
    Press,    ///< 按下：`X10` 档及以上都报。
    Drag,     ///< 移动：`button` 是按住的那个键，`None` 即无键悬停（前者须 `ButtonEvents`，后者须 `AnyEvents`）。
    Release,  ///< 松开：须 `PressRelease` 及以上（`X10` 档不报松开）。
};

/// @brief 上报协议里的按键编号来源（按钮 2 的右键不在此列，见文件头注释）。
enum class MouseButton : std::uint8_t {
    None,       ///< 无键：只与 `MousePhase::Drag` 配对，即悬停移动。
    Left,       ///< 左键。
    Middle,     ///< 中键。
    WheelUp,    ///< 滚轮上滚（按钮 64）。
    WheelDown,  ///< 滚轮下滚（按钮 65）。
};

/// @brief 一次要上报的鼠标事件；坐标是**可见区**内的 0-based 行列（编码时各自 +1）。
///
/// 行号刻意取可见区行而不是存储行序：上报协议说的是「终端第几行第几列」，回看窗口里滚到哪一屏
/// 由远端程序自己不知道——本仓把可见区当作它的坐标系（`ui::cell_at_point` 给的正是绘制行号）。
struct MouseEvent {
    MousePhase phase = MousePhase::Press;
    MouseButton button = MouseButton::None;
    std::size_t column = 0;
    std::size_t row = 0;
    bool control = false;  ///< Ctrl 位：+16。
    bool alt = false;      ///< Alt 位（与 Meta 同口径）：+8。
};

/// @brief 取此刻生效的上报层级（四档取最高；`?1006` 不参与层级，它只换编码形态）。
///
/// 视口要单独取用它：判「这一击该不该让位上报」看的是**层级**，而 `encode_mouse` 的空值同时
/// 表达「层级不够」与「该事件在当前层级下不报」两种意思，拿它当分流判据会把后者误读成前者。
/// @param modes 终端此刻的模式快照。
[[nodiscard]] auto mouse_tracking(const TermModes &modes) noexcept -> MouseTracking;

/// @brief 把一个鼠标事件编码成要发给会话的 VT 字节。
///
/// 纯函数：同一入参恒得同一输出，故全部编码表可在无 UI 环境里逐条断言。
/// @param event 事件本身（阶段、按键、可见区行列、修饰态）。
/// @param modes 终端此刻的模式快照——参与编码的是上表的四个层级档与 `mouse_sgr`（`?1006`）。
/// @return 待发送字节；空值表示**本层不发**：上报层级不够，或该事件在当前层级下不被上报。
[[nodiscard]] auto encode_mouse(const MouseEvent &event, const TermModes &modes) -> std::optional<std::string>;

}  // namespace borealis::term
