/// 测试类型: e2e
/// 目标单元: src/ui/terminal_view.cpp 的键盘/文本入口 → src/term/keymap.cpp → 真实 PTY
/// 测试说明: `SPEC.FEAT.INTERACT.01` 的**真机腿**：按键经框架事件派发落到视口控件，编码成 VT 字节
///           后会话写连接，子进程侧的**行编辑与命令提交**就是这些字节被正确解释的证据——cmd.exe
///           的键盘钩子只认真正的控制码（退格要 0x7F、回车要 CR），发错一个字节这行命令就不会成型。
///           集成用例（`itest_key_input`）用替身连接逐字节核对了编码，本文件核对的是对端接受度，
///           两者不重叠。
///
///           断言一律取权威网格里「用户看到的那一行」，不比原始字节（ConPTY 输出夹光标定位与回显）。
///
///           两条平台腿只差交互 shell 的素材形态：Windows 取 cmd.exe，POSIX 取 /bin/sh。判据逐字
///           共用，这正是 `SPEC.NF.PLAT.01` 要的等价核验（2026-10-08 补上 POSIX 腿，裁决 7.89）：
///           退格发 DEL 这一条在 POSIX 侧由**终端行规程的 VERASE**接受，回车发 CR 由 **ICRNL** 折成
///           换行提交，与 cmd 自己的键盘钩子是两套完全不同的接受机制。
///
///           本文件依赖真实 PTY，须在有窗口站/桌面的交互会话里跑（裁决 7.19⑤）。

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "borealis/conn/local_terminal.h"
#include "borealis/grid/storage.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::etest_key_forwarding {

namespace {

using borealis::conn::LocalTerminalSpec;
using borealis::grid::Storage;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::Cursor;
using borealis::term::SingleWidthPolicy;
using borealis::term::TermModes;
using borealis::ui::PaletteSpec;
using borealis::ui::TerminalView;
using borealis::ui::Typography;

#if defined(_WIN32)
/// 交互 shell：cmd.exe 自己实现行编辑，故 DEL 与 CR 由它接受。
constexpr std::string_view kInteractiveShell = "cmd.exe";
#else
/// 交互 shell：/bin/sh 交终端行规程编辑，VERASE 缺省即 DEL、EOL 缺省认 CR 与 LF。
constexpr std::string_view kInteractiveShell = "/bin/sh";
#endif

/// 两例的命令动词：输出行不含它，回显行含它——这是排除「键盘还在键入那一行」的判据材料。
constexpr std::string_view kCommandVerb = "echo";

constexpr Size kViewport{120U, 30U};
constexpr std::size_t kScrollback = 200U;
constexpr float kPaddingDp = 4.0F;
constexpr auto kBlinkPeriod = std::chrono::milliseconds{500};

/// 等一条输出上屏的上限：真机 shell 启动本身是秒级，留足余量以免偶发超时。
constexpr auto kSettleTimeout = std::chrono::milliseconds{20000};
constexpr auto kPollInterval = std::chrono::milliseconds{20};

SingleWidthPolicy width_policy;

/// @brief 网格某视口行的文本（行尾空白剥掉：右侧空格是网格填充不是内容）。
///
/// 只取 ASCII：本用例的素材全是 ASCII，非 ASCII 折成空格可避免 UTF-8 半截字节混进比对。
[[nodiscard]] auto row_text(Storage &grid, std::size_t row) -> std::string {
    const auto &line = grid.visible_line(row);
    auto text = std::string{};
    for (std::size_t column = 0; column < line.columns(); ++column) {
        const auto code_point = line.cell(column).code_point;
        text.push_back(code_point < 0x80U ? static_cast<char>(code_point) : ' ');
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

/// @brief 驱动台：真实 ConPTY 会话 + 视口控件 + 焦点路由，按键只从事件入口进。
///
/// 控件在这里只当**事件目标**用：绘制不发生（没有窗口），故本用例观察的是网格内容而非像素。
/// 声明次序即析构次序的倒序：控件与焦点管理器须在会话之前消散。
class Harness {
  public:
    explicit Harness(const std::string &command_line) {
        auto spec = LocalTerminalSpec{};
        spec.command_line = command_line;
        session_ = std::make_unique<Session>(
            conn::make_local_terminal_connection(spec, kViewport), kViewport, kScrollback, width_policy);
        view_ = std::make_shared<TerminalView>(*session_, PaletteSpec{}, test_font(), Typography{}, kPaddingDp,
                                            kBlinkPeriod, TerminalView::InteractionOptions{});
        root_ = std::make_unique<au::Node>(std::static_pointer_cast<au::Widget>(view_));
        focus_.set_root(&root_->widget());
        focus_.set_focus(view_.get());
        session_->start();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    auto press(au::KeyCode key, au::ModifierKey modifiers = au::ModifierKey::None) -> void {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        event.modifiers = modifiers;
        (void)au::EventDispatcher::dispatch(root_->widget(), event, focus_);
    }

    auto type(std::string_view utf8) -> void {
        au::TextInputEvent event;
        event.text = std::string{utf8};
        (void)au::EventDispatcher::dispatch(root_->widget(), event, focus_);
    }

    /// @brief 逐字符投文本事件：真实后端对每个可打印字符各给一次，批量投一次只测得出另一件事。
    auto type_each(std::string_view utf8) -> void {
        for (const char character : utf8) {
            type(std::string_view{&character, 1U});
        }
    }

    /// @brief 等 @p text 的**命令输出行**：该行以它结尾、且不含 @p command_word（命令的那个动词）。
    ///
    /// 子串匹配会把用户正在键入的那一行也算命中，故判据须把回显行排除；但两侧的回显行与输出行
    /// 并不同形——cmd 把提示符与命令打在同行（`C:\…>echo x`）而输出行逐字就是 `x`，POSIX 的 dash
    /// 把提示符 `$ ` 留在输出那一行的行首。所以整行相等只在 Windows 腿成立，而「以素材结尾 ∧ 不含
    /// 动词」在两腿都只可能是输出行，且比子串强：回显行含动词，被第二条排除。
    [[nodiscard]] auto wait_for_output(std::string_view text, std::string_view command_word)
        -> std::optional<std::string> {
        return wait_for([&text, &command_word](const std::string &line) {
            const auto ends_with = line.size() >= text.size() &&
                                   line.compare(line.size() - text.size(), text.size(), text) == 0;
            return ends_with && line.find(command_word) == std::string::npos;
        });
    }

    /// @brief 轮询整个可见区直到某行满足 @p matches，返回该行；超时返回空。
    [[nodiscard]] auto wait_for(const std::function<bool(const std::string &)> &matches)
        -> std::optional<std::string> {
        const auto deadline = std::chrono::steady_clock::now() + kSettleTimeout;
        while (std::chrono::steady_clock::now() < deadline) {
            static_cast<void>(session_->drain_damage());
            std::optional<std::string> hit;
            session_->read([&hit, &matches](Storage &grid, const Cursor &, const TermModes &) -> void {
                for (std::size_t row = 0; row < grid.visible_rows() && !hit.has_value(); ++row) {
                    const auto text = row_text(grid, row);
                    if (matches(text)) {
                        hit = text;
                    }
                }
            });
            if (hit.has_value()) {
                return hit;
            }
            std::this_thread::sleep_for(kPollInterval);
        }
        return std::nullopt;
    }

    auto close() -> void { session_->close(); }

  private:
    [[nodiscard]] static auto test_font() -> au::Font {
        return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
    }

    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    std::unique_ptr<au::Node> root_;
    au::FocusManager focus_;
};

}  // namespace

AURORA_TEST_CASE(typed_keys_and_enter_submit_a_command_on_the_pty) {
    Harness h{std::string{kInteractiveShell}};
    h.type_each("echo borealis-key-submit");
    // 回车经 KeyEvent 编码成 CR：两侧的提交都只认它（cmd 自己的行编辑 / POSIX 的 ICRNL），
    // 命令的**输出行**即提交成功的证据（回显行含动词，被 wait_for_output 的第二条排除）。
    h.press(au::KeyCode::Enter);
    const auto line = h.wait_for_output("borealis-key-submit", kCommandVerb);
    AURORA_TEST_REQUIRE_MSG(line.has_value(), "typed keys never produced command output on the PTY");
    h.close();
}

AURORA_TEST_CASE(backspace_key_edits_the_line_on_the_pty) {
    Harness h{std::string{kInteractiveShell}};
    h.type_each("echo borealis-bs-abc");
    // 退格发 DEL(0x7F) 而非 BS(0x08)：cmd 的行编辑只把 DEL 当删除，POSIX 侧 VERASE 的缺省字符也
    // 正是 DEL，三个 DEL 后接 "xyz" 才是最终行。
    for (std::size_t attempt = 0; attempt < 3U; ++attempt) {
        h.press(au::KeyCode::Backspace);
    }
    h.type_each("xyz");
    h.press(au::KeyCode::Enter);
    const auto line = h.wait_for_output("borealis-bs-xyz", kCommandVerb);
    AURORA_TEST_REQUIRE_MSG(line.has_value(), "the DEL bytes did not edit the line on the PTY");
    h.close();
}

}  // namespace borealis::test_cases::etest_key_forwarding
