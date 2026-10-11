/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（`OSC 52` 读方向的排请求 / 一问 / 应答腿）+ src/session/session.cpp
///           + src/session/clipboard_outbox.cpp
/// 测试说明: 以无头窗口 + 替身连接 + 替身剪贴板驱动「远端读查询 → 状态机三态分派 → 会话取走 →
///           视口现读闸门 → 一问 → 本地剪贴板 → 应答回写连接」这条链（`SPEC.FEAT.CONN.12`，裁决
///           7.117 的 D2/D4/D5/D6/D13）。三态分派、Pc 归一与队列上限都在 `utest_term_clipboard_read`
///           逐档证过，编码与三档降级在 `utest_session` 逐字节证过；本文件只证**链路**，值得单独
///           断的有五件事：① `Allow` 档一帧之内问到内容并答出去（跨 `Session` 与视口两次取走）；
///           ② `Ask` 档的「一次一问」（D5②）与答完之后下一帧接着问下一条（账本在视口而不在
///           装配层，D13②；就地递归会把正带着本次回调的对话框内容摘掉，故排帧是唯一安全形态）；
///           ③ 判据 ③⑤ 的三个「必须有回写」出口：答否、没宿主可问、视口析构时那条还没答出去的
///           请求（撤账）——三条回的都是同一条逐字节空响应；
///           ④ 闸门的现读口径（D2①）：登记之后改档，答的是**现在**这一档，且帧边界与「上一条答完」
///           两个入口都现读；
///           ⑤ 生产问框的 Yes 是经真实命中链点到的（与多行粘贴那棒同一套 G26 接货证人）。
///           剪贴板读与确认呈现都换成替身，于是全链不需要窗口站与真实剪贴板（AGENTS.md §4.5 第 25
///           条：读剪贴板是 IO，只在帧边界发生，事件回调里一次也不做）。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "borealis/grid/storage.h"
#include "borealis/session/clipboard_outbox.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/clipboard_read.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_clipboard_read {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::session::ClipboardOutbox;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::ClipboardGate;
using borealis::term::ClipboardReadPolicy;
using borealis::term::ClipboardReadRequest;
using borealis::ui::PaletteSpec;
using borealis::ui::TerminalView;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 40;
constexpr float kPaddingDp = 4.0F;
constexpr int kWindowWidth = 420;
constexpr int kWindowHeight = 260;

/// @brief 判据 ③ 的那条空响应，逐字节（`utest_terminal_osc` 的同一底线，这里按同一写法复用）。
constexpr std::string_view kEmptyResponse{"\x1B]52;c;\x07"};

/// @brief 一条读查询（`OSC 52 ; <Pc> ; ?`）。
[[nodiscard]] auto read_query(std::string_view pc) -> std::string {
    return std::string{"\x1B]52;"} + std::string{pc} + ";?\x07";
}

/// @brief 把正文折成期望的应答串（`ESC ] 52 ; <Pc> ; <base64> BEL`）。
[[nodiscard]] auto answer_text(std::string_view pc, std::string_view utf8) -> std::string {
    const auto payload = borealis::term::encode_base64(
        std::as_bytes(std::span<const char>{utf8.data(), utf8.size()}));
    std::string out{"\x1B]52;"};
    out += std::string{pc};
    out += ";";
    out += payload;
    out += "\x07";
    return out;
}

auto width_policy = std::make_shared<borealis::term::SingleWidthPolicy>();

/// @brief 把字节缓冲还原成文本，供应答断言。
[[nodiscard]] auto text_of(const std::vector<std::byte> &bytes) -> std::string {
    std::string out;
    out.reserve(bytes.size());
    for (const std::byte byte : bytes) {
        out.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return out;
}

/// @brief 传输连接替身：写方向原样记账，读方向由驱动台主动投递。
///
/// 记账落在外部那份 `std::vector<std::byte>` 上：判据 ⑤ 要看「视口析构之后回了什么」，而那时
/// 会话已经先于用例结束——缓冲必须由会话之外的东西持有。
class FakeConnection final : public Connection {
  public:
    explicit FakeConnection(std::vector<std::byte> *sink) : sink_{sink} {}

    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override {
        sink_->insert(sink_->end(), bytes.begin(), bytes.end());
    }

    auto resize(Size) -> void override {}

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

  private:
    std::vector<std::byte> *sink_ = nullptr;  ///< 非拥有：记账本归驱动台外面的用例。
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

[[nodiscard]] auto test_font() -> au::Font {
    return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
}

/// @brief 驱动台：闸门 + 会话 + 视口 + 浮层宿主 + 无头窗口。
///
/// 一枚闸门同时接给状态机与视口（装配层给的就是同一个 app 级对象，裁决 D13②）：状态机据它决定
/// 「登记还是当场拒」，视口据它决定「问还是不问」，中间改档则两处的读数一起跟上。
/// 视口与会话都在本类内析构，顺序是「视口先、会话后」——析构撤账那一路要往会话里写。
class Harness {
  public:
    /// @param log 会话写出的字节记账本（须比本类活得久）。
    /// @param policy 闸门初始档。
    /// @param with_host 是否挂浮层宿主（摘掉即「问不出去」那一档）。
    /// @param stub_confirm 是否用替身接住授权一问（置假即走生产的模态对话框）。
    /// @param early_wiring 是否在构造时就装好两条接缝（置假留给「装配晚一步」那一档用例）。
    Harness(std::vector<std::byte> &log, ClipboardReadPolicy policy, bool with_host = true,
            bool stub_confirm = true, bool early_wiring = true)
        : Harness(make_station(log), log, policy, with_host, stub_confirm, early_wiring) {}

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投一段远端字节并排帧到队列见底（`on_frame` 里排读请求）。
    auto feed(std::string_view bytes) -> void {
        connection_->deliver(bytes);
        render();
    }

    /// @brief 只投字节、不排帧：请求已在状态机里登记，而视口还没在帧边界取走。
    auto deliver(std::string_view bytes) -> void { connection_->deliver(bytes); }

    /// @brief 补装两条接缝（剪贴板读 + 授权一问）：给「装配晚一步」那一档用例分两步走。
    auto wire_seams() -> void { install_seams(true); }

    /// @brief 只排帧：投字节的腿已经走完，本函数用来落地帧边界的 IO 与询问。
    auto render() -> void {
        do {
            view_->on_frame();
            (void)window_.present_root(root_);
        } while (session_->has_damage());
    }

    /// @brief 改闸门档（`Immediate` 那条口径的真机形态：面板写盘之后同一个闸门换值）。
    auto set_policy(ClipboardReadPolicy policy) -> void { gate_.set_policy(policy); }

    /// @brief 放行当前那条询问（替身扣下的回调）。
    ///
    /// 先把回调从成员里搬走再调：生产路径答完一条会在下一帧问出下一条并回调本类装钩子那一路，
    /// 若此刻仍在 `held_answer` 里执行，赋值就会把正在栈上的可调用体拆掉。
    auto answer(bool granted) -> void {
        auto held = std::exchange(held_answer, nullptr);
        AURORA_TEST_REQUIRE(static_cast<bool>(held));
        held(granted);
    }

    /// @brief 在对话框内容盒里按阅读序点第 `ordinal` 枚按钮（0 即 Yes）。
    auto click_dialog_button(std::size_t ordinal) -> void {
        const aurora::Dialog *dialog = view_->clipboard_read_dialog();
        AURORA_TEST_REQUIRE(dialog != nullptr);
        const au::Rect box = dialog->paint_bounds();
        std::vector<au::Widget *> seen;
        for (float y = box.origin.y + 4.0F; y < box.bottom(); y += 4.0F) {
            for (float x = box.origin.x + 4.0F; x < box.right(); x += 4.0F) {
                const std::vector<au::HitNode> chain = root_.widget().hit_test_chain(
                    au::Point{.x = x, .y = y},
                    au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                             .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                              .height = static_cast<float>(kWindowHeight)}},
                    au::BuildContext{});
                if (chain.empty()) {
                    continue;
                }
                au::Widget *widget = chain.back().ptr;
                if (widget == nullptr || std::string_view{"Button"} != widget->type_name()) {
                    continue;
                }
                if (std::find(seen.begin(), seen.end(), widget) != seen.end()) {
                    continue;
                }
                seen.push_back(widget);
                if (seen.size() == ordinal + 1U) {
                    au::MouseEvent event;
                    event.position = au::Point{.x = x, .y = y};
                    event.button = au::MouseButton::Left;
                    event.action = au::MouseAction::Press;
                    (void)dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
                    event.action = au::MouseAction::Release;
                    (void)dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
                    return;
                }
            }
        }
        AURORA_TEST_REQUIRE_MSG(false, "dialog button not found");
    }

    [[nodiscard]] auto written() const -> std::string { return text_of(log_); }
    [[nodiscard]] auto read_dialog() const -> const aurora::Dialog * {
        return view_->clipboard_read_dialog();
    }
    [[nodiscard]] auto dialog_open() const -> bool {
        const aurora::Dialog *dialog = read_dialog();
        return dialog != nullptr && dialog->is_open();
    }

    std::string clipboard{};  ///< 替身剪贴板的内容（默认空 = 「剪贴板里没有文本」）。
    std::size_t read_calls = 0;
    std::size_t ask_calls = 0;
    std::u32string asked_pc{};  ///< 最近一次问给用户的选择位。
    std::function<void(bool)> held_answer;

  private:
    /// @brief 真正的组装：替身连接与会话从外面交进来，好让成员声明序能表达析构次序（见 `session_`）。
    Harness(std::pair<FakeConnection *, std::unique_ptr<Session>> station, std::vector<std::byte> &log,
            ClipboardReadPolicy policy, bool with_host, bool stub_confirm, bool early_wiring)
        : session_{std::move(station.second)},
          connection_{station.first},
          gate_{policy},
          log_{log},
          view_{std::make_shared<TerminalView>(*session_, PaletteSpec{}, test_font(),
                                               borealis::ui::Typography{}, kPaddingDp,
                                               std::chrono::milliseconds{500},
                                               TerminalView::InteractionOptions{})},
          host_{std::make_shared<au::OverlayHost>(
              au::Node{std::static_pointer_cast<au::Widget>(view_)})},
          root_{std::static_pointer_cast<au::Widget>(host_)} {
        if (with_host) {
            view_->set_overlay_host(*host_);
        }
        session_->set_clipboard_gate(&gate_);
        view_->set_clipboard_gate(&gate_);
        if (early_wiring) {
            install_seams(stub_confirm);
        }
        session_->start();
        focus_.set_root(&root_.widget());
        focus_.set_focus(view_.get());
        (void)window_.present_root(root_);  // 帧 0：布局发生在这里，对话框的盒据此才有尺寸
    }

    /// @brief 装两条替身接缝：剪贴板读恒装，授权一问按 `stub_confirm` 决定替身还是生产框。
    auto install_seams(bool stub_confirm) -> void {
        auto presentation = TerminalView::Presentation{};
        presentation.clipboard_read = [this]() -> std::string {
            ++read_calls;
            return clipboard;
        };
        if (stub_confirm) {
            presentation.confirm_clipboard_read =
                [this](const ClipboardReadRequest &request, std::function<void(bool)> answer) -> void {
                    ++ask_calls;
                    asked_pc = request.pc;
                    held_answer = std::move(answer);
                };
        }
        view_->set_presentation(std::move(presentation));
    }

    /// @brief 造替身连接与会话：记账本归外面那份 `log`，故会话析构之后仍能读到写出的字节。
    [[nodiscard]] static auto make_station(std::vector<std::byte> &log)
        -> std::pair<FakeConnection *, std::unique_ptr<Session>> {
        auto owned = std::make_unique<FakeConnection>(&log);
        auto *connection = owned.get();
        return {connection, std::make_unique<Session>(std::move(owned), kNominalSize, kScrollback,
                                                     width_policy)};
    }

    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    /// @brief 析构次序是判据 ⑤ 的一部分：`~TerminalView` 要替没答完的请求往会话里回写空响应。
    ///
    /// 于是会话必须**声明在最前**（最后析构），而窗口节点树在它之前拆掉——`window_` 里那份 root 的
    /// 引用是视口的最后一个持有者，把它排在 `session_` 之后即先在 `~Window` 里析构视口。
    std::unique_ptr<Session> session_;
    FakeConnection *connection_;  ///< 非拥有，会话持有。
    ClipboardGate gate_;
    std::vector<std::byte> &log_;  ///< 会话写出字节的记账本，归用例持有（判据 ⑤ 要看析构之后的读数）。
    std::shared_ptr<TerminalView> view_;
    std::shared_ptr<au::OverlayHost> host_;
    au::Node root_;
    au::Window window_ = make_window();  ///< 排在视口与宿主之后：析构时先释放它对节点树的引用。
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;
};

}  // namespace

AURORA_TEST_CASE(allow_policy_answers_the_read_within_one_frame) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Allow};
        h.clipboard = "hi";
        h.feed(read_query("c"));
        // 跨两次取走：状态机 → 会话（`on_frame` 里的 `take_clipboard_reads`）→ 应答回写连接。
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("c", "hi"));
        AURORA_TEST_CHECK_EQ(h.ask_calls, 0U);  // `Allow` 不问
        AURORA_TEST_CHECK_EQ(h.read_calls, 1U);
    }
}

AURORA_TEST_CASE(a_query_registered_before_the_seams_are_wired_still_gets_answered) {
    // 判据 ② 的装配半段：查询先登记、视口的接缝晚一步才装上。取走只发生在帧边界，故「装配晚一拍」
    // 不会把请求丢掉——生产里换 pane 与设置面板的预览腿走的都是这条时序。
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Allow, true, true, false};
        h.deliver(read_query("c"));  // 只投字节：请求已在状态机里，视口还没排过帧
        h.wire_seams();              // 接缝此时才装上
        h.clipboard = "hi";
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("c", "hi"));
        AURORA_TEST_CHECK_EQ(h.read_calls, 1U);
    }
}

AURORA_TEST_CASE(ask_policy_answers_yes_with_the_local_content) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("c"));
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 1U);
        AURORA_TEST_CHECK(h.written().empty());  // 用户没答之前一个字节也不回
        AURORA_TEST_CHECK_EQ(h.read_calls, 0U);  // 许可之后才读：读早一步就把没点头的内容当成了他给的
        h.answer(true);
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("c", "hi"));
        AURORA_TEST_CHECK_EQ(h.read_calls, 1U);
    }
}

AURORA_TEST_CASE(ask_policy_answers_no_with_the_empty_response) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("c"));
        h.answer(false);
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), std::string{kEmptyResponse});
        AURORA_TEST_CHECK_EQ(h.read_calls, 0U);  // 拒绝不读剪贴板
    }
}

AURORA_TEST_CASE(an_unanswered_question_is_still_answered_when_the_view_dies) {
    // 判据 ⑤：任何出口都恰好一条回写。用户既不点是也不点否，直接关掉这个 pane——远端不该一直等。
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("c"));
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 1U);
        AURORA_TEST_CHECK(h.written().empty());
    }
    AURORA_TEST_CHECK_EQ(text_of(log), std::string{kEmptyResponse});
}

AURORA_TEST_CASE(questions_are_asked_one_at_a_time_and_answered_in_order) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("p") + read_query("c"));
        // 一次一问（D5②）：两条都登记了，但只问出去第一条。
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 1U);
        AURORA_TEST_CHECK(h.written().empty());
        AURORA_TEST_CHECK(h.asked_pc == U"p");

        h.answer(true);
        h.render();  // 下一条排在**下一帧**问：就地递归会把正带着本回调的那份对话框内容摘掉
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("p", "hi"));
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 2U);
        AURORA_TEST_CHECK(h.asked_pc == U"c");

        h.answer(false);
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("p", "hi") + std::string{kEmptyResponse});
        // 第二条走的是 `c`：Pc 回显只服务「答给问的那一位」（D8①），而内容恒取系统剪贴板。
        AURORA_TEST_CHECK_EQ(h.read_calls, 1U);
    }
}

AURORA_TEST_CASE(a_query_arriving_while_a_question_is_open_waits) {
    // 问框开着的时候又来一条查询：它只能排在后面，不能把「已经问出去的那一条」的账本顶掉——
    // 顶掉的后果是前一条永远不会再被答，而它的远端还在等（判据 ③⑤）。
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("p"));
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 1U);
        h.feed(read_query("c"));
        AURORA_TEST_CHECK_EQ(h.ask_calls, 1U);
        AURORA_TEST_CHECK(h.written().empty());

        h.answer(true);
        h.render();
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 2U);
        AURORA_TEST_CHECK(h.asked_pc == U"c");
        h.answer(false);
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("p", "hi") + std::string{kEmptyResponse});
    }
}

AURORA_TEST_CASE(a_policy_change_answers_the_waiting_batch_without_asking) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("p") + read_query("c"));
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 1U);
        // 登记之后才改档 ⇒ 答的是现在这一档（D2①）：第一条答是，剩下的整批按 `Deny` 当场回空响应。
        h.set_policy(ClipboardReadPolicy::Deny);
        h.answer(true);
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("p", "hi") + std::string{kEmptyResponse});
        AURORA_TEST_CHECK_EQ(h.ask_calls, 1U);  // 第二条没再问
    }
}

AURORA_TEST_CASE(an_allow_policy_change_answers_the_waiting_batch_at_once) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("p") + read_query("c"));
        h.set_policy(ClipboardReadPolicy::Allow);
        h.answer(true);
        h.render();
        // 改开放档之后，剩下的不再问，直接按现档答掉（同一批出口，只是 `granted` 为真）。
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("p", "hi") + answer_text("c", "hi"));
        AURORA_TEST_CHECK_EQ(h.ask_calls, 1U);
    }
}

AURORA_TEST_CASE(the_view_answers_every_outstanding_request_when_it_dies) {
    // 判据 ⑤ 的第二半：不止「问出去的那一条」，排在待问队列里的同样各有一条回写。
    // 三条请求都没答 ⇒ 三条空响应，一条不少（少一条就是远端永远等不到）。
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("p") + read_query("c") + read_query("s"));
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 1U);  // 一次一问，另外两条还排在视口里
        AURORA_TEST_CHECK(h.written().empty());
    }
    AURORA_TEST_CHECK_EQ(text_of(log), std::string{kEmptyResponse} + std::string{kEmptyResponse} +
                                          std::string{kEmptyResponse});
}

AURORA_TEST_CASE(a_question_answered_twice_writes_one_response) {
    // 账本只放行第一次：闭包里那句「已经答过就返回」是重复派发的唯一防线（连点、或替身被调两回）。
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask};
        h.clipboard = "hi";
        h.feed(read_query("c"));
        AURORA_TEST_REQUIRE_EQ(h.ask_calls, 1U);
        auto held = h.held_answer;  // 留一份副本，第二次走同一条闭包
        h.answer(true);
        AURORA_TEST_REQUIRE(static_cast<bool>(held));
        held(true);
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("c", "hi"));
        AURORA_TEST_CHECK_EQ(h.read_calls, 1U);  // 第二次连剪贴板都不该再读
    }
}

AURORA_TEST_CASE(an_oversize_clipboard_degrades_to_the_empty_response) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Allow};
        h.clipboard = std::string(borealis::session::kMaxClipboardResponseBytes + 1U, 'a');
        h.feed(read_query("c"));
        AURORA_TEST_CHECK_EQ(h.written(), std::string{kEmptyResponse});
        AURORA_TEST_CHECK_EQ(h.read_calls, 1U);  // 读了才发现超限：降级的是应答而不是请求
    }
}

AURORA_TEST_CASE(an_empty_clipboard_degrades_to_the_empty_response) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Allow};
        h.clipboard.clear();  // 用户剪贴板里没有文本
        h.feed(read_query("c"));
        AURORA_TEST_CHECK_EQ(h.written(), std::string{kEmptyResponse});
        AURORA_TEST_CHECK_EQ(h.read_calls, 1U);
    }
}

AURORA_TEST_CASE(a_view_without_a_host_still_answers_the_question_it_cannot_ask) {
    // 判据 ③⑤ 的第三档：没有浮层宿主就问不出去，而这一路不能像粘贴那样「问不了就不作为」。
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask, false, false};
        h.clipboard = "hi";
        h.feed(read_query("c"));
        AURORA_TEST_CHECK_EQ(h.written(), std::string{kEmptyResponse});
        AURORA_TEST_CHECK_EQ(h.read_calls, 0U);
        AURORA_TEST_CHECK(h.read_dialog() == nullptr);
    }
}

AURORA_TEST_CASE(the_production_dialog_yes_button_answers_through_the_hit_chain) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask, true, false};  // 走生产的模态对话框
        h.clipboard = "hi";
        h.feed(read_query("c"));
        AURORA_TEST_REQUIRE(h.dialog_open());
        AURORA_TEST_CHECK(h.written().empty());  // 框开着就还没答
        h.click_dialog_button(0);                // 阅读序第一枚是 Yes
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), answer_text("c", "hi"));
        AURORA_TEST_CHECK_FALSE(h.dialog_open());
    }
}

AURORA_TEST_CASE(the_production_dialog_no_button_answers_the_empty_response) {
    std::vector<std::byte> log;
    {
        Harness h{log, ClipboardReadPolicy::Ask, true, false};
        h.clipboard = "hi";
        h.feed(read_query("c"));
        AURORA_TEST_REQUIRE(h.dialog_open());
        h.click_dialog_button(1);  // No
        h.render();
        AURORA_TEST_CHECK_EQ(h.written(), std::string{kEmptyResponse});
        AURORA_TEST_CHECK_FALSE(h.dialog_open());
    }
}

AURORA_TEST_CASE(the_question_strings_exist_in_the_setting_table) {
    // 问框的标题与正文都走词条表（裁决 7.52 的 S11）：漏配词条时框架回退的是空 text，
    // 界面上就是一个空白标题，故这一条要按 key 直接断，而不是断解析出来的串。
    borealis::ui::install_settings_strings();
    AURORA_TEST_CHECK_TRUE(borealis::ui::has_settings_string("terminal.clipboard_read.title"));
    AURORA_TEST_CHECK_TRUE(borealis::ui::has_settings_string("terminal.clipboard_read.body"));
    // 正文带一个 `{0}` 槽给选择位：填进去之后原文案里的那一位必须换成实测值。
    const std::string body =
        borealis::ui::settings_label("terminal.clipboard_read.body", {aurora::LocalizedString{std::string{"p"}}});
    AURORA_TEST_CHECK(body.find("p") != std::string::npos);
    AURORA_TEST_CHECK(body.find("{0}") == std::string::npos);
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_clipboard_read
