// ============================================================
// 鼠标上报编码实现（src/term/mouse.cpp）
// ------------------------------------------------------------
// 字节形态照 xterm 的两档：遗留档 `ESC [ M <cb> <cx> <cy>`（三字节各 +32、行列各一字节故钳到
// 223）与 SGR 扩展档 `ESC [ < <b> ; <col> ; <row> <M|m>`（十进制参数、无坐标上界）。两档共用
// 同一份按钮编号与修饰位算式，只有「松开」的表达方式不同：遗留档把按钮换成 3 号位（松开事件不
// 携带是哪一个键松了），SGR 档保留按键编号而把终止字节换成 `m`。
// ============================================================

#include "borealis/term/mouse.h"

#include <algorithm>
#include <string>

namespace borealis::term {
namespace {

/// @brief 转义字节（与 `keymap.cpp` 同一条写法，编码表到处要用）。
constexpr char kEscape = '\x1B';

/// @brief 遗留档的三字节偏移：按钮与行列各加它才落在可打印区（`CSI M` 后跟的三个字节）。
constexpr unsigned kLegacyOffset = 32;

/// @brief 按住拖动的位：加在按钮编号之上（遗留档与 SGR 档同值）。
constexpr unsigned kMotionBit = 32;

/// @brief 修饰位（xterm 口径：Shift 档不在本件，见头注）。
constexpr unsigned kAltBit = 8;
constexpr unsigned kCtrlBit = 16;

/// @brief 遗留档坐标的可表达上界（1-based）：再加 `kLegacyOffset` 恰为一字节的上限 255。
constexpr std::size_t kLegacyMaxCoordinate = 223;

/// @brief 无键悬停与「松开」在按钮编号里共用的 3 号位。
constexpr unsigned kNoButton = 3;

/// @brief 层级的可比序：四档的高低由枚举取值给出，比较只看这一档足以覆盖「取最高」那条规则。
[[nodiscard]] auto rank(MouseTracking level) noexcept -> unsigned {
    return static_cast<unsigned>(level);
}

/// @brief 按钮编号（不含运动位与修饰位）。
[[nodiscard]] auto button_index(MouseButton button) noexcept -> unsigned {
    switch (button) {
        case MouseButton::Left: return 0;
        case MouseButton::Middle: return 1;
        case MouseButton::WheelUp: return 64;
        case MouseButton::WheelDown: return 65;
        default: return kNoButton;
    }
}

/// @brief 当前层级下这一个事件是否该报。
[[nodiscard]] auto reported(const MouseEvent &event, MouseTracking level) noexcept -> bool {
    if (event.phase == MousePhase::Press) {
        return level != MouseTracking::Off;
    }
    if (event.phase == MousePhase::Release) {
        return rank(level) >= rank(MouseTracking::PressRelease);
    }
    // 拖动（按住移动）须 `?1002`，悬停（无键移动）须再高一档 `?1003`。
    return rank(level) >=
           rank(event.button == MouseButton::None ? MouseTracking::AnyEvents : MouseTracking::ButtonEvents);
}

}  // namespace

auto mouse_tracking(const TermModes &modes) noexcept -> MouseTracking {
    if (modes.mouse_any_events) {
        return MouseTracking::AnyEvents;
    }
    if (modes.mouse_button_events) {
        return MouseTracking::ButtonEvents;
    }
    if (modes.mouse_normal) {
        return MouseTracking::PressRelease;
    }
    if (modes.mouse_x10) {
        return MouseTracking::X10;
    }
    return MouseTracking::Off;
}

auto encode_mouse(const MouseEvent &event, const TermModes &modes) -> std::optional<std::string> {
    const MouseTracking level = mouse_tracking(modes);
    if (!reported(event, level)) {
        return std::nullopt;
    }
    const unsigned modifiers = (event.alt ? kAltBit : 0U) | (event.control ? kCtrlBit : 0U);
    const unsigned motion = event.phase == MousePhase::Drag ? kMotionBit : 0U;
    const unsigned column = static_cast<unsigned>(event.column) + 1U;
    const unsigned row = static_cast<unsigned>(event.row) + 1U;

    std::string bytes;
    bytes.push_back(kEscape);
    bytes.push_back('[');
    if (modes.mouse_sgr) {
        // SGR 档的松开保留按键编号，靠终止字节 `m` 与按下/拖动区分。
        bytes.push_back('<');
        bytes += std::to_string(button_index(event.button) + motion + modifiers);
        bytes += ';';
        bytes += std::to_string(column);
        bytes += ';';
        bytes += std::to_string(row);
        bytes.push_back(event.phase == MousePhase::Release ? 'm' : 'M');
        return bytes;
    }
    // 遗留档：松开把按钮换成 3 号位，行列各占一字节故超出即钳到该档最大可表达值。
    const unsigned button = event.phase == MousePhase::Release ? kNoButton : button_index(event.button);
    bytes.push_back('M');
    bytes.push_back(static_cast<char>(kLegacyOffset + button + motion + modifiers));
    bytes.push_back(static_cast<char>(kLegacyOffset + std::min<unsigned>(column, kLegacyMaxCoordinate)));
    bytes.push_back(static_cast<char>(kLegacyOffset + std::min<unsigned>(row, kLegacyMaxCoordinate)));
    return bytes;
}

}  // namespace borealis::term
