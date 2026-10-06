/// 测试类型: unit
/// 目标单元: include/borealis/term/mouse.h + src/term/mouse.cpp
/// 测试说明: 鼠标上报的编码表（`SPEC.FEAT.TERM.06`）：四档层级的蕴含（X10 只报按下、普通加松开、
///           按钮事件加按住拖动、任意事件加无键悬停）、`?1006` 与层级正交而只换编码形态、
///           遗留档与 SGR 档的两处形态差（松开是否携带按键编号、坐标有无一字节上界）、修饰位
///           Ctrl=+16 与 Alt=+8（Shift 不进结构体：它是「让位本地」的覆盖键，见头注）、
///           滚轮的 64/65 编号，以及空值即「本层不发」的那一侧（层级不够）。

#include <cstddef>
#include <optional>
#include <string>

#include "borealis/term/mouse.h"
#include "borealis/term/terminal.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_mouse {

namespace {

using borealis::term::encode_mouse;
using borealis::term::MouseButton;
using borealis::term::MouseEvent;
using borealis::term::MousePhase;
using borealis::term::MouseTracking;
using borealis::term::mouse_tracking;
using borealis::term::TermModes;

/// @brief 按层级置模式位：四档取最高是既成口径，故置到该层级即等于真机上「低档一并被蕴含」。
[[nodiscard]] auto modes_at(MouseTracking level, bool sgr = false) -> TermModes {
    auto modes = TermModes{};
    modes.mouse_x10 = level >= MouseTracking::X10;
    modes.mouse_normal = level >= MouseTracking::PressRelease;
    modes.mouse_button_events = level >= MouseTracking::ButtonEvents;
    modes.mouse_any_events = level >= MouseTracking::AnyEvents;
    modes.mouse_sgr = sgr;
    return modes;
}

/// @brief 编码结果的可比形态：`encode_mouse` 从不出产空串，故空串恰好表示「本层不发」。
[[nodiscard]] auto wire(MousePhase phase, MouseButton button, const TermModes &modes, std::size_t column = 0,
                        std::size_t row = 0, bool control = false, bool alt = false) -> std::string {
    return encode_mouse(
               MouseEvent{
                   .phase = phase, .button = button, .column = column, .row = row, .control = control, .alt = alt},
               modes)
        .value_or(std::string{});
}

/// @brief 遗留档的期望字节：`ESC [ M` 后跟按钮载荷一个字节，行列各占一字节且取 1-based 再加 32。
[[nodiscard]] auto legacy(unsigned button_byte, unsigned one_based_column, unsigned one_based_row) -> std::string {
    std::string bytes;
    bytes.push_back('\x1B');
    bytes.push_back('[');
    bytes.push_back('M');
    bytes.push_back(static_cast<char>(button_byte));
    bytes.push_back(static_cast<char>(32 + one_based_column));
    bytes.push_back(static_cast<char>(32 + one_based_row));
    return bytes;
}

/// @brief SGR 扩展档的期望字节：终止字节 `M` 是按下或拖动、`m` 是松开。
[[nodiscard]] auto sgr(unsigned code, unsigned column, unsigned row, char terminator) -> std::string {
    return std::string("\x1B[<") + std::to_string(code) + ";" + std::to_string(column) + ";" +
           std::to_string(row) + terminator;
}

}  // namespace

AURORA_TEST_CASE(x10_level_encodes_only_the_press) {
    const auto modes = modes_at(MouseTracking::X10);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes), legacy(32, 1, 1));
    // X10 是最初的形态：只有「按下」这一相，松开与运动一概不发。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Release, MouseButton::Left, modes), std::string{});
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::Left, modes), std::string{});
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::None, modes), std::string{});
}

AURORA_TEST_CASE(normal_level_adds_the_release_but_not_motion) {
    const auto modes = modes_at(MouseTracking::PressRelease);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes), legacy(32, 1, 1));
    // 松开的载荷是 3 号位（无键）而非左键：遗留档在这一点上不区分是哪一个键松了。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Release, MouseButton::Left, modes), legacy(35, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::Left, modes), std::string{});
}

AURORA_TEST_CASE(button_events_level_adds_dragged_motion) {
    const auto modes = modes_at(MouseTracking::ButtonEvents);
    // 拖动的位是按钮编号 +32（运动位），与层级蕴含的低两档事件一并可发。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::Left, modes), legacy(64, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::Middle, modes), legacy(65, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::None, modes), std::string{});  // 悬停要再高一档
}

AURORA_TEST_CASE(any_events_level_adds_buttonless_hover) {
    const auto modes = modes_at(MouseTracking::AnyEvents);
    // 无键悬停 = 3 号位 + 运动位 = 35，载荷与「松开左键」同值而靠阶段区分（SGR 档才分得开）。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::None, modes), legacy(67, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes), legacy(32, 1, 1));
}

AURORA_TEST_CASE(the_highest_enabled_level_wins) {
    // 四档是层级而不是并列开关：`?1002` 单独开着也报按下与松开，而不是只报拖动。
    auto modes = TermModes{};
    modes.mouse_button_events = true;
    AURORA_TEST_CHECK_EQ(mouse_tracking(modes), MouseTracking::ButtonEvents);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes), legacy(32, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Release, MouseButton::Left, modes), legacy(35, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::Left, modes), legacy(64, 1, 1));
}

AURORA_TEST_CASE(nothing_is_encoded_while_every_level_is_off) {
    const auto modes = TermModes{};
    AURORA_TEST_CHECK_EQ(mouse_tracking(modes), MouseTracking::Off);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes), std::string{});
    // `?1006` 与层级正交：只换形态改不开报，因此单开它仍然什么都不发。
    auto sgr_only = TermModes{};
    sgr_only.mouse_sgr = true;
    AURORA_TEST_CHECK_EQ(mouse_tracking(sgr_only), MouseTracking::Off);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, sgr_only), std::string{});
}

AURORA_TEST_CASE(ctrl_and_alt_set_sixteen_and_eight) {
    const auto modes = modes_at(MouseTracking::PressRelease);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes, 0, 0, true), legacy(48, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes, 0, 0, false, true), legacy(40, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes, 0, 0, true, true), legacy(56, 1, 1));
    // 运动位与修饰位互不相干：三个位是相加关系，SGR 档同样（拖动要 `?1002` 才报）。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::Left, modes_at(MouseTracking::ButtonEvents),
                             0, 0, true, true),
                         legacy(88, 1, 1));
}

AURORA_TEST_CASE(wheel_buttons_are_sixty_four_and_sixty_five) {
    const auto modes = modes_at(MouseTracking::PressRelease);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::WheelUp, modes), legacy(96, 1, 1));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::WheelDown, modes), legacy(97, 1, 1));
    // 滚轮在 SGR 档走同一份编号，十进制文本只是将编码转成字符。
    const auto sgr_modes = modes_at(MouseTracking::PressRelease, true);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::WheelUp, sgr_modes), sgr(64, 1, 1, 'M'));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::WheelDown, sgr_modes), sgr(65, 1, 1, 'M'));
}

AURORA_TEST_CASE(legacy_coordinates_are_one_based_and_capped_at_the_byte_limit) {
    const auto modes = modes_at(MouseTracking::X10);
    // 结构体取 0-based 可见区行列，上线一律各自 +1。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes, 2, 3), legacy(32, 3, 4));
    // 1-based 224 已落不进一个字节（224 + 32 = 256），故钳到 223；`?1005`/`?1015` 不在需求四档内。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes, 222, 222), legacy(32, 223, 223));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes, 5000, 5000), legacy(32, 223, 223));
}

AURORA_TEST_CASE(sgr_encoding_keeps_the_button_and_has_no_coordinate_cap) {
    const auto modes = modes_at(MouseTracking::AnyEvents, true);
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes), sgr(0, 1, 1, 'M'));
    // 两处与遗留档的形态差：松开保留按键编号、终止字节换成 `m`。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Release, MouseButton::Left, modes), sgr(0, 1, 1, 'm'));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Release, MouseButton::Middle, modes), sgr(1, 1, 1, 'm'));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::Left, modes), sgr(32, 1, 1, 'M'));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Drag, MouseButton::None, modes), sgr(35, 1, 1, 'M'));
    // 坐标是十进制文本参数，没有一字节上界。
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes, 5000, 300), sgr(0, 5001, 301, 'M'));
    AURORA_TEST_CHECK_EQ(wire(MousePhase::Press, MouseButton::Left, modes, 0, 0, true, true), sgr(24, 1, 1, 'M'));
}

AURORA_TEST_CASE(the_tracking_level_is_read_from_the_four_mode_bits) {
    // 视口分流看的是层级，而不是 `encode_mouse` 的空值（后者还兼着「这一事件在该档不报」）。
    AURORA_TEST_CHECK_EQ(mouse_tracking(TermModes{}), MouseTracking::Off);
    auto modes = TermModes{};
    modes.mouse_x10 = true;
    AURORA_TEST_CHECK_EQ(mouse_tracking(modes), MouseTracking::X10);
    modes.mouse_normal = true;
    AURORA_TEST_CHECK_EQ(mouse_tracking(modes), MouseTracking::PressRelease);
    modes.mouse_button_events = true;
    AURORA_TEST_CHECK_EQ(mouse_tracking(modes), MouseTracking::ButtonEvents);
    modes.mouse_any_events = true;
    AURORA_TEST_CHECK_EQ(mouse_tracking(modes), MouseTracking::AnyEvents);
    // 只开高档（真机上程序可以只发 `?1003 h`）也是同一档。
    auto only_top = TermModes{};
    only_top.mouse_any_events = true;
    AURORA_TEST_CHECK_EQ(mouse_tracking(only_top), MouseTracking::AnyEvents);
    AURORA_TEST_CHECK_EQ(mouse_tracking(modes_at(MouseTracking::PressRelease, true)), MouseTracking::PressRelease);
}

}  // namespace borealis::test_cases::utest_mouse
