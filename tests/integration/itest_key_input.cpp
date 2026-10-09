/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（键盘与文本入口）+ src/term/keymap.cpp
/// 测试说明: 以框架真实的事件派发驱动「`KeyEvent` / `TextInputEvent` → 视口控件 → 会话 → 连接字节」
///           这条键入链路（`SPEC.FEAT.INTERACT.01` 的转发腿、`SPEC.FEAT.TERM.09` 的发送方向），
///           观测点是连接替身记录的写出字节。断言的必须是**链路**而非编码表本身（编码表由
///           `utest_keymap` 逐字节覆盖）：控制键经派发器落到连接恰好一字节且不重复、`DECCKM`
///           经**真实状态机**喂入后方向键在 CSI/SS3 间切换、可打印键只经文本通道一份（KeyEvent
///           那一份必须被让掉，否则屏幕上每个字符出现两次）、CJK 按会话编码成 UTF-8、无焦点时
///           按键不着陆。
///
///           `Tab` 与 Alt 系组合**在本文件断言**（框架回货后接回）：派发器的 Tab 有控件优先钩子
///           （`Widget::wants_tab_keys()`），Win32 后端把 `WM_SYSKEY*` 与 `WM_KEY*` 同走一条按键
///           通道（附录 A.2 的 G14/G15 已闭合）。断言的是链路而非编码表字节形态。

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "borealis/grid/storage.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_key_input {

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::Cursor;
using borealis::term::TermModes;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::PaletteSpec;
using borealis::ui::TerminalView;
using borealis::ui::Typography;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 40;
constexpr float kPaddingDp = 4.0F;

auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();

/// @brief 传输连接替身：记录会话写出的字节，并可主动投递字节（本用例里投递线程即读线程）。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override {
        written.insert(written.end(), bytes.begin(), bytes.end());
    }

    auto resize(Size size) -> void override { resizes.push_back(size); }

    auto close() -> void override { alive_ = false; }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    auto deliver(std::string_view bytes) -> void {
        std::vector<std::byte> raw;
        raw.reserve(bytes.size());
        for (const char c : bytes) {
            raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        events_->on_bytes(raw);
    }

    [[nodiscard]] auto written_text() const -> std::string {
        std::string out;
        out.reserve(written.size());
        for (const std::byte byte : written) {
            out.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        }
        return out;
    }

    auto clear_written() -> void { written.clear(); }

    std::vector<std::byte> written;
    std::vector<Size> resizes;

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

/// @brief 驱动台：会话 + 替身连接 + 视口控件 + 根节点 + 焦点管理器。
///
/// 键入链路不经绘制，故没有无头窗口与帧缓冲——焦点路由是这条链路的唯一入口条件。
/// 成员声明次序即析构次序的倒序：焦点管理器与根节点须在会话之前消散（控件持会话裸引用）。
class Harness {
  public:
    Harness()
        : session_(std::make_unique<Session>(own_connection(), kNominalSize, kScrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, PaletteSpec{}, test_font(), Typography{}, kPaddingDp,
                                               std::chrono::milliseconds{500}, TerminalView::InteractionOptions{})),
          root_(std::static_pointer_cast<au::Widget>(view_)) {
        session_->start();
        focus_.set_root(&root_.widget());
        focus_.set_focus(view_.get());
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投递一段字节（喂状态机：`CSI ? 1 h/l` 这类模式位由此来，而不是注入快照）。
    auto feed(std::string_view bytes) -> void { connection_->deliver(bytes); }

    /// @brief 派发一次按键按下，返回派发器给出的「是否被消费」。
    auto press(au::KeyCode key, au::ModifierKey modifiers = au::ModifierKey::None) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        event.modifiers = modifiers;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 派发一次按键抬起（终端忽略它：抬起不得重发一遍字节）。
    auto release(au::KeyCode key, au::ModifierKey modifiers = au::ModifierKey::None) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Up;
        event.modifiers = modifiers;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 派发一次文本输入（后端把可打印字符的真实布局结果交给本通道，UTF-8）。
    auto type(std::string_view utf8) -> bool {
        au::TextInputEvent event;
        event.text = std::string{utf8};
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    [[nodiscard]] auto written() const -> std::string { return connection_->written_text(); }
    auto clear_written() -> void { connection_->clear_written(); }

    /// @brief 焦点开关：无焦点时派发器不该把按键交给任何控件。
    auto set_focused(bool focused) -> void {
        if (focused) {
            focus_.set_root(&root_.widget());
            focus_.set_focus(view_.get());
        } else {
            focus_.clear();
        }
    }

    /// @brief 权威模式快照（只读核对：本用例要确认状态机真的收到了喂入的模式位）。
    [[nodiscard]] auto modes() -> TermModes {
        TermModes snapshot{};
        session_->read(
            [&snapshot](Storage &, const Cursor &, const TermModes &modes) { snapshot = modes; });
        return snapshot;
    }

  private:
    /// @brief 造替身连接：所有权交给会话，裸指针留在本类驱动投递与观测。
    [[nodiscard]] auto own_connection() -> std::unique_ptr<Connection> {
        auto owned = std::make_unique<FakeConnection>();
        connection_ = owned.get();
        return owned;
    }

    [[nodiscard]] static auto test_font() -> au::Font {
        return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
    }

    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    au::Node root_;
    au::FocusManager focus_;
};

}  // namespace

AURORA_TEST_CASE(control_keys_land_on_the_connection_as_one_byte) {
    Harness h;
    // Ctrl+C / Ctrl+Z 是控制字符直通（SPEC.FEAT.INTERACT.01）：恰好一个字节，且派发器把按键
    // 交给控件消费，不留给任何全局语义。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::C, au::ModifierKey::Control));
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\x03", 1U});
    h.clear_written();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, au::ModifierKey::Control));
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\x1A", 1U});
    h.clear_written();

    // 退格发 DEL（0x7F）而非 BS，回车发 CR；这两档是遗留终端的既有约定，发错即后端不收。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Backspace));
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\x7F", 1U});
    h.clear_written();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Enter));
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\r", 1U});
    h.clear_written();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Escape));
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\x1B", 1U});
    // 抬起不重发：一按一松只有一份字节抵达连接。
    h.release(au::KeyCode::Escape);
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\x1B", 1U});
}

AURORA_TEST_CASE(cursor_keys_switch_between_csi_and_ss3_with_decckm) {
    Harness h;
    // 初始为普通模式：方向键走 CSI。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowUp));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[A");
    h.clear_written();

    // `DECCKM` 须经**真实状态机**生效：喂 `CSI ? 1 h` 而不是往控件里塞快照，否则测的是接线假象。
    h.feed("\x1B[?1h");
    AURORA_TEST_REQUIRE(h.modes().cursor_key_app);
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowUp));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1BOA");
    h.clear_written();

    // 应用模式下 Home 同为 SS3；退出后逐位回到 CSI 形态。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Home));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1BOH");
    h.clear_written();

    h.feed("\x1B[?1l");
    AURORA_TEST_REQUIRE(!h.modes().cursor_key_app);
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowUp));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[A");
}

AURORA_TEST_CASE(function_and_editing_keys_use_the_xterm_forms) {
    Harness h;
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::F5));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[15~");
    h.clear_written();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Delete));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[3~");
    h.clear_written();

    // 主键盘 `Insert` 的 `CSI 2~` 腿：框架 G20 回货前 `VK_INSERT` 只能落成 `Unknown`，本例当时
    // 结构上无法成立（裁决 7.30⑤ 的「不等不绕」留桩处）。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Insert));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[2~");
    h.clear_written();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::PageUp));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[5~");
    h.clear_written();

    // 带修饰的功能键走扩展形态（掩码 1+Shift+Alt*2+Ctrl*4）：Shift+F5 → `CSI 15;2 ~`。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::F5, au::ModifierKey::Shift));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[15;2~");
}

AURORA_TEST_CASE(printable_characters_are_sent_once_through_the_text_channel) {
    Harness h;
    AURORA_TEST_REQUIRE(h.type("abc"));
    AURORA_TEST_CHECK_EQ(h.written(), "abc");
    h.clear_written();

    // 可打印键的 KeyEvent 那一份必须让掉：真实后端先给 WM_KEYDOWN 再给 WM_CHAR，两份都发就成
    // 每个字符上屏两次（`SPEC.FEAT.INTERACT.01` 与文本通道的分工即这条边界）。
    AURORA_TEST_REQUIRE(!h.press(au::KeyCode::A));
    AURORA_TEST_CHECK(h.written().empty());

    // CJK-LITERAL: 断言素材 - 中文经文本通道按会话编码发送是 M1 出口判据的输入腿
    AURORA_TEST_REQUIRE(h.type("中"));
    AURORA_TEST_CHECK_EQ(h.written(), "\xE4\xB8\xAD");
    h.clear_written();

    AURORA_TEST_REQUIRE(!h.release(au::KeyCode::A));
    AURORA_TEST_CHECK(h.written().empty());
}

AURORA_TEST_CASE(tab_reaches_the_connection_instead_of_focus_travel) {
    Harness h;
    // `wants_tab_keys()` 不覆写则派发器把 Tab 当焦点遍历消费掉，会话一个字节也收不到（G14 的回货腿）。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Tab));
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\t", 1U});
    h.clear_written();

    // Shift+Tab 走反向形态 `CSI Z`：Tab 的遗留编码与「Shift 改形态」两件事都在链路上过一遍。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Tab, au::ModifierKey::Shift));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[Z");
    h.clear_written();

    // Alt+字母（Win32 的 `WM_SYSKEYDOWN` 腿，G15）：前缀一个 ESC，且不留给快捷键层。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::A, au::ModifierKey::Alt));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B" "a");
}

AURORA_TEST_CASE(application_keypad_switches_through_the_real_state_machine) {
    Harness h;
    // 缺省是常规键盘模式（DECKPNM）：NumLock 开着的小键盘数字归文本通道，按键通道不发。
    AURORA_TEST_CHECK(!h.press(au::KeyCode::KP_7, au::ModifierKey::NumLock));
    AURORA_TEST_CHECK(h.written().empty());
    AURORA_TEST_REQUIRE(h.type("7"));
    AURORA_TEST_CHECK_EQ(h.written(), "7");
    h.clear_written();

    // `ESC =`（DECKPAM）须经**真实状态机**喂入：注入模式快照就测不到状态机那条指派。
    h.feed("\x1B=");
    AURORA_TEST_REQUIRE(h.modes().application_keypad);

    // 应用模式下小键盘数字发 SS3 数值族；Windows 无论哪一档都给同一物理键再发一条 `WM_CHAR`，
    // 故紧随其后的文本事件必须被吞掉一次——两份都发就是屏幕上多一个字符。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::KP_7, au::ModifierKey::NumLock));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1BOw");
    AURORA_TEST_REQUIRE(h.type("7"));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1BOw");
    h.clear_written();

    // 吞一次即止：下一条文本事件属于别的来源，照常发给会话。
    AURORA_TEST_REQUIRE(h.type("8"));
    AURORA_TEST_CHECK_EQ(h.written(), "8");
    h.clear_written();

    // NumLock 关时数字阵是导航语义，DECKPAM 不改它：小键盘 7 即 Home（DECCKM 未开故走 CSI）。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::KP_7));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[H");
    h.clear_written();

    // `ESC >` 回常规模式：小键盘数字重新让位文本通道。
    h.feed("\x1B>");
    AURORA_TEST_REQUIRE(!h.modes().application_keypad);
    AURORA_TEST_CHECK(!h.press(au::KeyCode::KP_8, au::ModifierKey::NumLock));
    AURORA_TEST_CHECK(h.written().empty());
}

AURORA_TEST_CASE(keys_do_not_reach_the_connection_without_focus) {
    Harness h;
    h.set_focused(false);
    // 无焦点：派发器既不该把按键落到会话，也不该把文本落进会话（否则按键会发给「看不见」的标签页）。
    AURORA_TEST_CHECK(!h.press(au::KeyCode::C, au::ModifierKey::Control));
    AURORA_TEST_CHECK(!h.type("abc"));
    AURORA_TEST_CHECK(h.written().empty());

    h.set_focused(true);
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::C, au::ModifierKey::Control));
    AURORA_TEST_CHECK_EQ(h.written(), std::string{"\x03", 1U});
}

AURORA_TEST_CASE(session_encoding_is_applied_to_text_but_not_to_escape_sequences) {
    Harness h;
    // 转义序列已过编码环节（`send_bytes`），文本走会话编码（`send_text`）：两者都写同一条连接，
    // 顺序与投递次序逐位一致，故一次按键加一次上屏的字节能在连接上原样读出。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowDown));
    AURORA_TEST_REQUIRE(h.type("hi"));
    AURORA_TEST_CHECK_EQ(h.written(), "\x1B[Bhi");
}

}  // namespace borealis::test_cases::itest_key_input
