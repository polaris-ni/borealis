/// 测试类型: integration
/// 目标单元: src/ui/broadcast_banner.cpp + src/ui/terminal_view.cpp（镜腿/角标）+
///           src/ui/workspace_view.cpp（广播接缝）
/// 测试说明: 以无头窗口 + **真实事件派发**驱动「第十四键 → 工作区 → 路由器 → 同伴视口 →
///           同伴会话编码」这条链（`SPEC.FEAT.WS.09`，稿 `codespec/UI_WORKSPACE_BROADCAST.draft.md`
///           §5）。断言的必须是接线与端到端字节，而不是纯逻辑件已经覆盖的集合语义
///           （`utest_broadcast` 逐条测过 `BroadcastGroup` / `BroadcastRouter`）。本文件证六件事：
///           ① 跨标签入组后，一次按键/文本在源格只有一份、同伴各一份，且同伴按**自己的**模式与
///              编码重编码（源 UTF-8 + CSI，同伴 GB18030 + application-cursor）；
///           ② 非成员来源不扇出；命令键被工作区吃掉、一个字节也不广播；`Esc` 照发且不退出组；
///           ③ 同一会话被两个 pane 指向时一次输入只写一份（D7），横幅按去重后的会话计数；
///           ④ 关 pane 撤成员、关整张标签清它的全部成员，横幅与 pill 同步；
///           ⑤ 横幅 0/26、固定 token 与按钮几何的像素落点；pill 只在分屏态画、且不压焦点描边；
///           ⑥ 点击横幅清空整组并把焦点还给最后持焦的 pane，随后键入仍进终端；横幅的那一次按下同时
///              交给装配层的上报点（`SPEC.FEAT.WS.03` 稿 D7），且先于清组判定、抬起不重复报。
///
///           装配层在 `src/main.cpp`，不可从测试直接调用；本驱动台按稿 §4 第 6 条复刻同一条接线
///           （三处构造点同源装四条 Hooks、Sink 三回调现查 `view_of`），因此测的是接缝形态而不是
///           某一份 `main` 的私有闭包。像素判据里的 token 照**稿面**取值而从实现读。

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/codec.h"
#include "borealis/term/width.h"
#include "borealis/ui/broadcast.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/broadcast_banner.h"
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/terminal_view.h"
#include "../../src/ui/workspace_view.h"

namespace borealis::test_cases::itest_workspace_broadcast {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::KeyPress;
using borealis::term::SessionEncoding;
using borealis::term::TerminalDefaults;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::BroadcastBanner;
using borealis::ui::BroadcastBannerHooks;
using borealis::ui::BroadcastBannerState;
using borealis::ui::BroadcastMember;
using borealis::ui::BroadcastRouter;
using borealis::ui::PaletteSpec;
using borealis::ui::PaneId;
using borealis::ui::RgbaColor;
using borealis::ui::TabId;
using borealis::ui::TerminalView;
using borealis::ui::Typography;
using borealis::ui::WorkspaceView;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 200;
constexpr float kPaddingDp = 4.0F;
constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 600;

/// @brief 稿 §2.2 的固定 token（不经终端主题）：横幅底、按钮底/边框、pill 底。
constexpr RgbaColor kBannerBackground{0x3A, 0x2A, 0x14, 255};
constexpr RgbaColor kButtonBackground{0x5A, 0x44, 0x20, 255};
constexpr RgbaColor kPillBackground{0x5A, 0x44, 0x20, 255};
/// @brief 分屏焦点描边色（视觉稿 §2.1，与被测实现同一个固定 token，但取值来自稿面）。
constexpr RgbaColor kAccentInk{0xBD, 0x93, 0xF9, 255};

/// @brief 帧缓冲取点越界时的返回值：一个画面里不可能出现的色，断言因此红得可读而不是踩内存。
constexpr RgbaColor kOutsideFrame{0x11, 0x22, 0x33, 0x44};

using Pixels = std::vector<std::uint8_t>;

/// @brief 传输连接替身：记录写出的字节，并可主动投递字节（投递线程即读线程）。
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

    auto reset() -> void { written.clear(); }

    std::vector<std::byte> written;
    std::vector<Size> resizes;

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

[[nodiscard]] auto test_font() -> au::Font {
    return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
}

/// @brief 一张标签的驱动数据。`sessions` 声明在 `workspace` 之前：视口持会话裸引用，会话须后析构。
struct TabFixture {
    std::vector<std::shared_ptr<Session>> sessions;
    std::map<PaneId, Session *> session_by_pane;
    std::map<PaneId, std::shared_ptr<TerminalView>> views;
    std::set<PaneId> live_panes;
    std::shared_ptr<WorkspaceView> workspace;
};

/// @brief 驱动台：多标签工作区 + 真路由器 + 真横幅 + 无头窗口 + 焦点管理器 + 调度器。
///
/// 装配层的四条接缝在这里按稿 §4 第 6 条复刻：`make_pane` 造独立会话/视口（共享会话档返回同一个
/// 视口），`Sink` 三回调每次现查 `tab_workspaces` 的对应格，成员变更钩子刷新横幅与全部工作区的 pill。
class Rig {
  public:
    Rig() {
        au::Scheduler::set_current(&scheduler_);
        borealis::ui::install_settings_strings();
        palette_.default_foreground = RgbaColor{0xF8, 0xF8, 0xF2, 255};
        palette_.default_background = RgbaColor{0x00, 0x00, 0x00, 255};

        banner_ = std::make_shared<BroadcastBanner>(BroadcastBannerHooks{
            .exit_all = [this]() -> void {
                // 点击横幅 = 清空整组（D8）；只有真的清了组才归还焦点——空组横幅高 0，本不该被命中。
                if (router_.size() > 0U) {
                    router_.clear();
                    restore_focus();
                }
            },
            // 稿 D7 的第三处上报点：生产里这一枚闭包把本窗号交给 `ActiveWindowTracker`，本驱动台没有
            // 窗，故只数次数并**顺手抄下当时的组大小**——上报若挂在清组之后，抄到的就是 0。
            .note_press = [this]() -> void {
                ++banner_press_reports;
                banner_report_group_size = router_.size();
            },
        });
        // 变更钩子是「组成员集合真变」的唯一刷新入口（toggle / remove / remove_tab / clear 四路共用）。
        router_.set_change_hook([this]() -> void { refresh_ui(); });

        root_column_ = std::make_shared<au::Column>();
        root_column_->set_modifier(au::Modifier{}.fill_max_size());
        root_column_->set_cross_axis_alignment(au::CrossAxisAlignment::Stretch);
        root_column_->child_nodes_mut().push_back(au::Node{banner_});
        root_ = au::Node{root_column_};
        focus_.set_root(&root_.widget());
    }

    ~Rig() { au::Scheduler::set_current(nullptr); }

    Rig(const Rig &) = delete;
    auto operator=(const Rig &) -> Rig & = delete;
    Rig(Rig &&) = delete;
    auto operator=(Rig &&) -> Rig & = delete;

    /// @brief 新增一张标签（单 pane 起步）。
    /// @param name 横幅上显示的会话名（ASCII 即可；名字折算属装配层，不在本件判据内）。
    /// @param encoding 该标签会话的编解码档（`SessionEncoding::name`）。
    /// @param shared_view 置真即 `make_pane` 对每个 pane 返回同一个视口/会话（F8 的生产形态复刻）。
    auto add_tab(std::u32string name, std::string encoding = "UTF-8", bool shared_view = false) -> TabId {
        const TabId id = next_tab_++;
        names_[id] = std::move(name);
        shared_view_[id] = shared_view;

        auto first = make_pane(id, 1U, encoding);
        if (shared_view) {
            shared_view_of_[id] = first;
        }

        WorkspaceView::Hooks hooks;
        hooks.make_pane = [this, id, encoding](PaneId pane, PaneId /*origin*/)
            -> std::shared_ptr<TerminalView> {
            if (shared_view_[id]) {
                TabFixture &tab = tabs_[id];
                const std::shared_ptr<TerminalView> shared = shared_view_of_[id];
                tab.session_by_pane[pane] = tab.session_by_pane[1U];
                tab.views[pane] = shared;
                tab.live_panes.insert(pane);
                return shared;
            }
            return make_pane(id, pane, encoding);
        };
        hooks.dispatch_grid = [this, id](PaneId pane, borealis::ui::GridSize size) -> void {
            const auto found = tabs_[id].session_by_pane.find(pane);
            if (found != tabs_[id].session_by_pane.end() && found->second != nullptr) {
                found->second->resize(Size{size.columns, size.rows});
            }
        };
        hooks.teardown_pane = [this, id](PaneId pane) -> void {
            TabFixture &tab = tabs_[id];
            tab.live_panes.erase(pane);
            const auto found = tab.session_by_pane.find(pane);
            if (found != tab.session_by_pane.end() && found->second != nullptr) {
                Session *session = found->second;
                bool shared_with_live_pane = false;
                for (const PaneId live : tab.live_panes) {
                    const auto other = tab.session_by_pane.find(live);
                    if (other != tab.session_by_pane.end() && other->second == session) {
                        shared_with_live_pane = true;
                        break;
                    }
                }
                if (!shared_with_live_pane) {
                    session->close();
                }
            }
            router_.remove(id, pane);  // 关 pane 即撤成员（装配层 `teardown_pane` 的同一条接缝）
        };
        hooks.is_broadcast = [this, id](PaneId pane) -> bool { return router_.contains(id, pane); };
        hooks.toggle_broadcast = [this, id](PaneId pane) -> bool {
            return router_.toggle(id, pane);  // 变更钩子负责刷新横幅与全部 pill
        };
        hooks.mirror_key = [this, id](PaneId pane, const KeyPress &press) -> void {
            router_.dispatch_key(id, pane, press);
        };
        hooks.mirror_text = [this, id](PaneId pane, std::u32string_view text) -> void {
            router_.dispatch_text(id, pane, text);
        };

        auto workspace = std::make_shared<WorkspaceView>(first, std::move(hooks));
        tabs_[id].workspace = workspace;
        attach(id);
        refresh_ui();
        return id;
    }

    /// @brief 切到某张标签：根 Column 只保留横幅 + 当前工作区（D10 的常态形态）。
    auto show(TabId id) -> void {
        const auto found = tabs_.find(id);
        if (found == tabs_.end() || found->second.workspace == nullptr) {
            return;
        }
        current_ = id;
        auto &nodes = root_column_->child_nodes_mut();
        nodes.resize(1);  // 横幅常驻 index 0；工作区按需替换
        au::Node workspace_node{found->second.workspace};
        workspace_node.widget().set_modifier(au::Modifier{}.expand(1.0F));
        nodes.push_back(std::move(workspace_node));
        root_column_->mark_needs_layout();
        root_column_->mark_needs_paint();
        focus_.set_root(&root_.widget());
        if (TerminalView *view = found->second.workspace->view_of(found->second.workspace->focused_pane())) {
            focus_.set_focus(view);
        }
        render();
    }

    /// @brief 复刻装配层的重启腿：换掉某格的会话与视口，格身份与成员资格都不动。
    /// @return 交换是否发生（该格不存在或视口为空即 false）。
    auto replace_pane(TabId tab, PaneId pane, const std::string &encoding = "UTF-8") -> bool {
        const auto found = tabs_.find(tab);
        if (found == tabs_.end() || found->second.workspace == nullptr) {
            return false;
        }
        const std::shared_ptr<TerminalView> next = make_pane(tab, pane, encoding);
        // 旧会话留在 `fixture.sessions` 里直到本函数返回：`replace_view` 内部就地销毁旧视口，
        // 而 `~TerminalView` 要往它背后的会话写撤账空响应（与生产重启腿同一条次序）。生产的重启腿
        // 由派发给菜单/按钮的指针事件带动，故这里以同一个派发上下文包住这一跳：栈外只能落到按根缓存
        // 的兜底管理器，本表用来派发的那份焦点就不会跟着换新视口，判据就测不到真链路。
        au::set_current_focus_manager(&focus_);
        const bool swapped = found->second.workspace->replace_view(pane, next);
        au::set_current_focus_manager(nullptr);
        render();
        return swapped;
    }

    /// @brief 排一帧：先让各视口结算会话提交，再走真实布局与绘制。
    auto render() -> void {        for (auto &[tab, fixture] : tabs_) {
            (void)tab;
            for (auto &[pane, view] : fixture.views) {
                (void)pane;
                if (view != nullptr) {
                    view->on_frame();
                }
            }
        }
        (void)window_.present_root(root_);
    }

    /// @brief 派发一次按键按下（走框架真实键盘派发：只交给焦点控件）。
    auto press(au::KeyCode key, au::ModifierKey modifiers = au::ModifierKey::None) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        event.modifiers = modifiers;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 派发一次文本输入（后端布局结果，UTF-8）。
    auto type(std::string_view utf8) -> bool {
        au::TextInputEvent event;
        event.text = std::string{utf8};
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 在某点单击一次（按下即抬起）并排帧。
    auto click(au::Point at) -> void {
        au::MouseEvent event;
        event.position = at;
        event.button = au::MouseButton::Left;
        event.action = au::MouseAction::Press;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
        event.action = au::MouseAction::Release;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
        render();
    }

    /// @brief 给某格会话投字节（模式位/内容由此来，而不是注入快照）。
    auto feed(TabId tab, PaneId pane, std::string_view bytes) -> void {
        FakeConnection *connection = connection_of(tab, pane);
        if (connection != nullptr) {
            connection->deliver(bytes);
        }
    }

    [[nodiscard]] auto written(TabId tab, PaneId pane) const -> std::string {
        const FakeConnection *connection = connection_of(tab, pane);
        return connection == nullptr ? std::string{} : connection->written_text();
    }

    auto clear_written() -> void {
        for (auto &[session, connection] : connections_) {
            (void)session;
            connection->reset();
        }
    }

    [[nodiscard]] auto view(TabId tab, PaneId pane) const -> TerminalView * {
        const auto found = tabs_.find(tab);
        if (found == tabs_.end()) {
            return nullptr;
        }
        const auto view = found->second.views.find(pane);
        return view == found->second.views.end() ? nullptr : view->second.get();
    }

    [[nodiscard]] auto workspace(TabId tab) -> WorkspaceView & { return *tabs_.at(tab).workspace; }
    [[nodiscard]] auto router() noexcept -> BroadcastRouter & { return router_; }
    [[nodiscard]] auto banner() noexcept -> BroadcastBanner & { return *banner_; }
    [[nodiscard]] auto palette() const noexcept -> const PaletteSpec & { return palette_; }

    /// @brief 横幅退出按钮盒（横幅在根 Column 首位，局部系即窗口系）。
    [[nodiscard]] auto exit_button() const -> au::Rect { return banner_->exit_button_box(); }

    /// @brief 横幅上报次数（`SPEC.FEAT.WS.03` 稿 D7 的第三处上报点，本驱动台只数次数）。
    std::size_t banner_press_reports = 0U;
    /// @brief 最后一次上报时**当时**的组成员数：上报先于清组这条次序的证人（挂在之后就成 0）。
    std::size_t banner_report_group_size = 0U;

    /// @brief 某格 pill 内部的一个落点（窗口坐标）；该格没有 pill 时回空。
    [[nodiscard]] auto pill_point(TabId tab, PaneId pane, double dx, double dy) const -> std::optional<au::Point> {
        const TerminalView *target = view(tab, pane);
        if (target == nullptr) {
            return std::nullopt;
        }
        const std::optional<borealis::ui::Rect> box = target->broadcast_badge_box();
        if (!box.has_value()) {
            return std::nullopt;
        }
        const au::Rect frame = target->paint_bounds();
        return au::Point{.x = frame.origin.x + static_cast<float>(box->x) + static_cast<float>(dx),
                         .y = frame.origin.y + static_cast<float>(box->y) + static_cast<float>(dy)};
    }

    /// @brief 取当前帧缓冲（RGBA8，物理像素）。
    [[nodiscard]] auto pixels() const -> Pixels {
        Pixels out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    /// @brief 把窗口 dp 落点折成帧缓冲像素下标并取 RGBA（headless scale 为 1 时两者逐位同格）。
    [[nodiscard]] auto sample(const Pixels &px, au::Point at) const -> RgbaColor {
        const auto x = static_cast<std::size_t>(at.x);
        const auto y = static_cast<std::size_t>(at.y);
        if (x >= buffer_width() || y >= buffer_height()) {
            return kOutsideFrame;
        }
        const std::size_t index = (y * buffer_width() + x) * 4U;
        return RgbaColor{px[index], px[index + 1U], px[index + 2U], px[index + 3U]};
    }

    [[nodiscard]] static auto ctrl_shift() -> au::ModifierKey {
        return au::ModifierKey::Control | au::ModifierKey::Shift;
    }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    [[nodiscard]] auto buffer_width() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowWidth) * window_.surface().scale_factor());
    }

    [[nodiscard]] auto buffer_height() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowHeight) * window_.surface().scale_factor());
    }

    [[nodiscard]] auto connection_of(TabId tab, PaneId pane) const -> FakeConnection * {
        const auto fixture = tabs_.find(tab);
        if (fixture == tabs_.end()) {
            return nullptr;
        }
        const auto session = fixture->second.session_by_pane.find(pane);
        if (session == fixture->second.session_by_pane.end()) {
            return nullptr;
        }
        const auto connection = connections_.find(session->second);
        return connection == connections_.end() ? nullptr : connection->second;
    }

    /// @brief 造一格独立会话与视口，并把所有权分置两处（会话归表，视口归工作区/本表弱记）。
    [[nodiscard]] auto make_pane(TabId tab, PaneId pane, const std::string &encoding) -> std::shared_ptr<TerminalView> {
        auto owned = std::make_unique<FakeConnection>();
        FakeConnection *raw = owned.get();
        auto session = std::make_shared<Session>(std::move(owned), kNominalSize, kScrollback, width_policy_,
                                                 TerminalDefaults{},
                                                 SessionEncoding{.name = encoding});
        auto view = std::make_shared<TerminalView>(*session, palette_, test_font(), Typography{}, kPaddingDp,
                                                   std::chrono::milliseconds{500},
                                                   TerminalView::InteractionOptions{});
        session->start();

        TabFixture &fixture = tabs_[tab];
        fixture.sessions.push_back(session);
        fixture.session_by_pane[pane] = session.get();
        fixture.views[pane] = view;
        fixture.live_panes.insert(pane);
        connections_[session.get()] = raw;
        return view;
    }

    /// @brief 按标签登记端点；三回调每次现查 `tabs_`，故换会话/换工作区后不需要重登记。
    auto attach(TabId id) -> void {
        router_.attach(
            id, BroadcastRouter::Sink{
                    .stream = [this, id](PaneId pane) -> const void * {
                        const auto found = tabs_.find(id);
                        if (found == tabs_.end() || found->second.workspace == nullptr) {
                            return nullptr;
                        }
                        const TerminalView *target = found->second.workspace->view_of(pane);
                        return target == nullptr ? nullptr : target->session_identity();
                    },
                    .key = [this, id](PaneId pane, const KeyPress &press) -> void {
                        if (TerminalView *target = view(id, pane)) {
                            target->send_broadcast_key(press);
                        }
                    },
                    .text = [this, id](PaneId pane, std::u32string_view text) -> void {
                        if (TerminalView *target = view(id, pane)) {
                            target->send_broadcast_text(text);
                        }
                    }});
    }

    /// @brief 装配层 `refresh_broadcast_ui` 的复刻：名字按加入序、同会话只列一次、计数用去重数。
    auto refresh_ui() -> void {
        BroadcastBannerState state;
        state.active = router_.size() > 0U;
        state.session_count = router_.unique_size();

        std::vector<const void *> seen;
        for (const BroadcastMember &member : router_.members()) {
            const void *identity = router_.stream_of(member);
            if (identity != nullptr) {
                bool duplicate = false;
                for (const void *known : seen) {
                    if (known == identity) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) {
                    continue;  // 同一会话被两格指向：横幅只列一次（D7/D11）
                }
                seen.push_back(identity);
            }
            const auto name = names_.find(member.tab);
            state.names.push_back(name == names_.end() ? std::u32string{} : name->second);
        }
        banner_->set_state(std::move(state));

        for (auto &[tab, fixture] : tabs_) {
            (void)tab;
            if (fixture.workspace != nullptr) {
                fixture.workspace->refresh_broadcast_badges();
            }
        }
    }

    /// @brief 点击横幅后的焦点归还：当前工作区把键盘交回最后持焦的 pane（F10）。
    auto restore_focus() -> void {
        const auto found = tabs_.find(current_);
        if (found != tabs_.end() && found->second.workspace != nullptr) {
            found->second.workspace->restore_keyboard_focus();
        }
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    au::Scheduler scheduler_;
    PaletteSpec palette_{};
    std::shared_ptr<UnicodeWidthPolicy> width_policy_ = std::make_shared<UnicodeWidthPolicy>();
    std::map<const Session *, FakeConnection *> connections_;  ///< 非拥有：连接归会话。
    std::map<TabId, TabFixture> tabs_;
    std::map<TabId, std::u32string> names_;
    std::map<TabId, bool> shared_view_;
    std::map<TabId, std::shared_ptr<TerminalView>> shared_view_of_;
    TabId next_tab_ = 1U;
    TabId current_ = 0U;
    BroadcastRouter router_;
    std::shared_ptr<BroadcastBanner> banner_;
    std::shared_ptr<au::Column> root_column_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher pointer_dispatcher_;  ///< 本驱动台私有的连击判定与指针捕获状态。
};

}  // namespace

AURORA_TEST_CASE(two_tabs_fan_out_with_each_panes_own_mode_and_encoding) {
    Rig rig;
    const TabId alpha = rig.add_tab(U"alpha");
    const TabId beta = rig.add_tab(U"beta", "GB18030");
    const TabId gamma = rig.add_tab(U"gamma");

    // ① 跨标签组装：组允许单成员（D4），故先在第一张标签加入第一名，切到第二张再加入第二名。
    rig.show(alpha);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    AURORA_TEST_CHECK_EQ(rig.router().size(), 1U);
    AURORA_TEST_CHECK(rig.router().contains(alpha, 1U));
    rig.render();
    AURORA_TEST_CHECK(rig.banner().state().active);
    // 单 pane 成员不画 pill（N4）；横幅已把工作区下推 26 dp。
    AURORA_TEST_CHECK_FALSE(rig.view(alpha, 1U)->broadcast_badge_box().has_value());
    AURORA_TEST_CHECK_NEAR(rig.view(alpha, 1U)->paint_bounds().origin.y, 26.0, 1.0);

    rig.show(beta);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_CHECK_EQ(rig.router().size(), 2U);
    AURORA_TEST_CHECK_EQ(rig.router().unique_size(), 2U);
    AURORA_TEST_REQUIRE_EQ(rig.banner().state().names.size(), 2U);
    AURORA_TEST_CHECK(rig.banner().state().names[0] == U"alpha");
    AURORA_TEST_CHECK(rig.banner().state().names[1] == U"beta");
    AURORA_TEST_CHECK_EQ(rig.banner().state().session_count, 2U);

    // ② 同伴按**自己的**模式重编码：beta 开 application-cursor，alpha 仍是普通 CSI。
    rig.feed(beta, 1U, "\x1B[?1h");
    rig.show(alpha);
    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::ArrowUp));
    AURORA_TEST_CHECK_EQ(rig.written(alpha, 1U), "\x1B[A");  // 源格只有一份
    AURORA_TEST_CHECK_EQ(rig.written(beta, 1U), "\x1BOA");   // 同伴的模式生效
    AURORA_TEST_CHECK(rig.written(gamma, 1U).empty());

    // ③ 文本按**自己的**编码：同一个码点（U+4E2D）在 UTF-8 与 GB18030 下字节不同。
    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.type("\xE4\xB8\xAD"));
    AURORA_TEST_CHECK_EQ(rig.written(alpha, 1U), "\xE4\xB8\xAD");
    AURORA_TEST_CHECK_EQ(rig.written(beta, 1U), "\xD6\xD0");
    AURORA_TEST_CHECK(rig.written(gamma, 1U).empty());

    // ④ `Esc` 照常发给会话、且不退出组（D8 的负面证人）。
    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::Escape));
    AURORA_TEST_CHECK_EQ(rig.written(alpha, 1U), "\x1B");
    AURORA_TEST_CHECK_EQ(rig.written(beta, 1U), "\x1B");
    AURORA_TEST_CHECK_EQ(rig.router().size(), 2U);

    // ⑤ 非成员来源不扇出。
    rig.show(gamma);
    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.type("z"));
    AURORA_TEST_CHECK_EQ(rig.written(gamma, 1U), "z");
    AURORA_TEST_CHECK(rig.written(alpha, 1U).empty());
    AURORA_TEST_CHECK(rig.written(beta, 1U).empty());

    // ⑥ 命令键被工作区吃掉：既不发源格也不广播，且真的改了组。
    rig.show(alpha);
    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    AURORA_TEST_CHECK(rig.written(alpha, 1U).empty());
    AURORA_TEST_CHECK(rig.written(beta, 1U).empty());
    AURORA_TEST_CHECK_EQ(rig.router().size(), 1U);
    AURORA_TEST_CHECK_FALSE(rig.router().contains(alpha, 1U));
}

AURORA_TEST_CASE(banner_height_tokens_and_pills_follow_the_group) {
    Rig rig;
    const TabId tab = rig.add_tab(U"alpha");
    rig.show(tab);

    // ① 组空：横幅自报高 0，工作区从窗顶开始；左上角是终端自己的底而不是横幅底。
    AURORA_TEST_CHECK_NEAR(rig.view(tab, 1U)->paint_bounds().origin.y, 0.0, 1.0);
    auto px = rig.pixels();
    AURORA_TEST_CHECK(rig.sample(px, au::Point{2.0F, 2.0F}) == rig.palette().default_background);

    // ② 组非空：横幅 26 dp、固定 token；退出按钮 100×22 且填充按钮底。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_CHECK_NEAR(rig.view(tab, 1U)->paint_bounds().origin.y, 26.0, 1.0);
    px = rig.pixels();
    AURORA_TEST_CHECK(rig.sample(px, au::Point{2.0F, 2.0F}) == kBannerBackground);
    const au::Rect button = rig.exit_button();
    AURORA_TEST_CHECK_NEAR(button.size.width, 100.0, 0.5);
    AURORA_TEST_CHECK_NEAR(button.size.height, 22.0, 0.5);
    AURORA_TEST_CHECK(rig.sample(px, au::Point{button.origin.x + 8.0F, button.origin.y + 11.0F}) ==
                      kButtonBackground);

    // ③ 单 pane 成员仍不画 pill（N4）：屏幕左下角是终端自己的底。
    AURORA_TEST_CHECK_FALSE(rig.view(tab, 1U)->broadcast_badge_box().has_value());
    const au::Rect single = rig.view(tab, 1U)->paint_bounds();
    AURORA_TEST_CHECK(rig.sample(px, au::Point{single.origin.x + 20.0F,
                                               single.origin.y + single.size.height - 17.0F}) ==
                      rig.palette().default_background);

    // ④ 分屏后成员那格出 pill，非成员那格没有；pill 底取稿面 token。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::D, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_REQUIRE_EQ(rig.workspace(tab).pane_count(), 2U);
    AURORA_TEST_CHECK(rig.view(tab, 1U)->broadcast_badge_box().has_value());
    AURORA_TEST_CHECK_FALSE(rig.view(tab, 2U)->broadcast_badge_box().has_value());

    // ⑤ 把焦点格（新 pane）也加入组：两格各有一枚 pill，且 pill 左侧的焦点描边仍可见。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_CHECK(rig.view(tab, 2U)->broadcast_badge_box().has_value());
    px = rig.pixels();
    const std::optional<au::Point> pill = rig.pill_point(tab, 2U, 2.0, 9.0);
    AURORA_TEST_REQUIRE(pill.has_value());
    AURORA_TEST_CHECK(rig.sample(px, *pill) == kPillBackground);
    const au::Rect right = rig.view(tab, 2U)->paint_bounds();
    AURORA_TEST_CHECK(rig.sample(px, au::Point{right.origin.x + 1.0F,
                                               right.origin.y + right.size.height - 17.0F}) == kAccentInk);

    // ⑥ 焦点回左格并退出它：左格 pill 消失、右格保留，成员数同步。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::ArrowLeft, au::ModifierKey::Alt));
    rig.render();
    AURORA_TEST_REQUIRE_EQ(rig.workspace(tab).focused_pane(), 1U);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_CHECK_FALSE(rig.view(tab, 1U)->broadcast_badge_box().has_value());
    AURORA_TEST_CHECK(rig.view(tab, 2U)->broadcast_badge_box().has_value());
    AURORA_TEST_CHECK_EQ(rig.router().size(), 1U);

    // ⑦ 再把左格加回组，然后关掉仍在组里的第二格：路由器撤该 (tab, pane)，剩余成员与横幅计数
    //    同步；余下一格是单 pane，即便仍是成员也不再画 pill（N4）。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    AURORA_TEST_CHECK_EQ(rig.router().size(), 2U);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::ArrowRight, au::ModifierKey::Alt));
    rig.render();
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::W, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_CHECK_EQ(rig.workspace(tab).pane_count(), 1U);
    AURORA_TEST_CHECK_FALSE(rig.router().contains(tab, 2U));
    AURORA_TEST_CHECK(rig.router().contains(tab, 1U));
    AURORA_TEST_CHECK_EQ(rig.banner().state().session_count, 1U);
    AURORA_TEST_CHECK_FALSE(rig.view(tab, 1U)->broadcast_badge_box().has_value());

    // ⑧ 关整张标签：它的全部成员与端点一次清账，横幅回到 0 高。
    rig.router().remove_tab(tab);
    rig.render();
    AURORA_TEST_CHECK_EQ(rig.router().size(), 0U);
    AURORA_TEST_CHECK_FALSE(rig.banner().state().active);
    AURORA_TEST_CHECK_NEAR(rig.view(tab, 1U)->paint_bounds().origin.y, 0.0, 1.0);
    px = rig.pixels();
    AURORA_TEST_CHECK(rig.sample(px, au::Point{2.0F, 2.0F}) == rig.palette().default_background);
}

AURORA_TEST_CASE(restarting_one_pane_keeps_its_broadcast_membership) {
    Rig rig;
    const TabId tab = rig.add_tab(U"alpha");
    rig.show(tab);

    // 左格入组 → 分屏 → 右格入组：两格成员，各自一份独立会话。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::D, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_REQUIRE_EQ(rig.workspace(tab).pane_count(), 2U);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_REQUIRE(rig.router().contains(tab, 2U));
    AURORA_TEST_REQUIRE_EQ(rig.router().size(), 2U);
    const void *before = rig.view(tab, 2U)->session_identity();

    // 重启右格：格身份、拓扑、成员资格与角标都不动，只有会话换人。
    AURORA_TEST_REQUIRE(rig.replace_pane(tab, 2U));
    AURORA_TEST_CHECK_EQ(rig.workspace(tab).pane_count(), 2U);
    AURORA_TEST_CHECK_EQ(rig.router().size(), 2U);
    AURORA_TEST_CHECK(rig.router().contains(tab, 2U));
    AURORA_TEST_CHECK(rig.view(tab, 1U)->broadcast_badge_box().has_value());
    AURORA_TEST_CHECK(rig.view(tab, 2U)->broadcast_badge_box().has_value());
    AURORA_TEST_CHECK_NE(rig.view(tab, 2U)->session_identity(), before);
    AURORA_TEST_CHECK(rig.view(tab, 2U)->is_focused());  // 焦点没跟着旧视口一起散掉（派发用的那份管理器也换了）

    // 焦点回左格后键入：新会话仍是扇出目标，重启没把它漏出组。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::ArrowLeft, au::ModifierKey::Alt));
    rig.render();
    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.type("z"));
    AURORA_TEST_CHECK_EQ(rig.written(tab, 1U), "z");
    AURORA_TEST_CHECK_EQ(rig.written(tab, 2U), "z");
}

AURORA_TEST_CASE(replacing_a_shared_view_is_refused_rather_than_guessed) {
    Rig rig;
    const TabId tab = rig.add_tab(U"alpha", "UTF-8", /*shared_view=*/true);
    rig.show(tab);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::D, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_REQUIRE_EQ(rig.workspace(tab).pane_count(), 2U);
    TerminalView *shared = rig.workspace(tab).view_of(1U);
    AURORA_TEST_REQUIRE(shared != nullptr);
    AURORA_TEST_REQUIRE(rig.workspace(tab).view_of(2U) == shared);  // 两格共用一份视图（F5 的生产形态）
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_REQUIRE_EQ(rig.router().size(), 2U);

    // 子节点靠视图地址反查，共用时无从分辨「该换哪一格」——宁可拒绝，也不把另一格留在悬垂手柄上。
    // 判据只问工作区自己的表：驱动台的 `make_pane` 会先把候选登记进本表，那一份不代表界面腿动了。
    AURORA_TEST_CHECK_FALSE(rig.replace_pane(tab, 2U));
    AURORA_TEST_CHECK_EQ(rig.workspace(tab).pane_count(), 2U);
    AURORA_TEST_CHECK(rig.workspace(tab).view_of(1U) == shared);
    AURORA_TEST_CHECK(rig.workspace(tab).view_of(2U) == shared);
    AURORA_TEST_CHECK_EQ(rig.router().size(), 2U);
    AURORA_TEST_CHECK(rig.router().contains(tab, 2U));
}

AURORA_TEST_CASE(a_shared_session_is_written_once_even_with_two_members) {
    Rig rig;
    const TabId tab = rig.add_tab(U"alpha", "UTF-8", /*shared_view=*/true);
    rig.show(tab);

    // 第一格入组，再分屏：分屏工厂返回同一个视口/会话（生产装配今天的形态，F8）。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::D, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_REQUIRE_EQ(rig.workspace(tab).pane_count(), 2U);
    AURORA_TEST_REQUIRE(rig.view(tab, 1U) == rig.view(tab, 2U));
    AURORA_TEST_CHECK_EQ(rig.view(tab, 1U)->session_identity(), rig.view(tab, 2U)->session_identity());

    // 第二格也入组：成员数 2，但去重后的会话数与横幅列名都只有 1。
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_CHECK_EQ(rig.router().size(), 2U);
    AURORA_TEST_CHECK_EQ(rig.router().unique_size(), 1U);
    AURORA_TEST_CHECK_EQ(rig.banner().state().session_count, 1U);
    AURORA_TEST_REQUIRE_EQ(rig.banner().state().names.size(), 1U);

    // 一次键入只产生一份字节（源格发一份，路由器按会话身份跳过同会话成员）。
    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.type("x"));
    AURORA_TEST_CHECK_EQ(rig.written(tab, 1U), "x");

    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::ArrowUp));
    AURORA_TEST_CHECK_EQ(rig.written(tab, 1U), "\x1B[A");
}

AURORA_TEST_CASE(clicking_the_banner_clears_the_group_and_returns_focus) {
    Rig rig;
    const TabId alpha = rig.add_tab(U"alpha");
    const TabId beta = rig.add_tab(U"beta");
    rig.show(alpha);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.show(beta);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_REQUIRE_EQ(rig.router().size(), 2U);

    rig.show(alpha);
    AURORA_TEST_REQUIRE(rig.view(alpha, 1U)->is_focused());
    const au::Rect button = rig.exit_button();
    rig.click(au::Point{.x = button.origin.x + 50.0F, .y = button.origin.y + 11.0F});

    // 清组、横幅失活、高度归 0；焦点回到该工作区最后持焦的 pane。
    AURORA_TEST_CHECK_EQ(rig.router().size(), 0U);
    AURORA_TEST_CHECK_FALSE(rig.banner().state().active);
    AURORA_TEST_CHECK(rig.view(alpha, 1U)->is_focused());
    AURORA_TEST_CHECK_NEAR(rig.view(alpha, 1U)->paint_bounds().origin.y, 0.0, 1.0);
    const auto px = rig.pixels();
    AURORA_TEST_CHECK(rig.sample(px, au::Point{2.0F, 2.0F}) == rig.palette().default_background);

    // 退出后的第一下键入仍进终端（焦点归还的端到端证人）。
    rig.clear_written();
    AURORA_TEST_REQUIRE(rig.type("q"));
    AURORA_TEST_CHECK_EQ(rig.written(alpha, 1U), "q");
}

AURORA_TEST_CASE(the_banner_reports_the_press_that_cleared_the_group) {
    // 稿 D7 的第三处上报点（`SPEC.FEAT.WS.03` 批 3-C）：装配层用它把本窗号交给
    // `ui::ActiveWindowTracker`，于是「点过 B 窗的横幅」之后，下一条全局快捷键落 B 窗。三条判据：
    // ① 报的是**按下**这一次，一次即够（抬起不再算一次新的触碰）；
    // ② 上报先于清组那条判定：报的那一刻组还是满的。若把上报挪进 `exit_all` 之后，这一枚读数就成 0
    //    ——而「事实」一旦要跟「动作是否成功」挂钩，落点就会在动作落空的那一路停在别的窗；
    // ③ 动作腿不因上报而改变：清组与失活仍照旧。
    Rig rig;
    const TabId alpha = rig.add_tab(U"alpha");
    const TabId beta = rig.add_tab(U"beta");
    rig.show(alpha);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.show(beta);
    AURORA_TEST_REQUIRE(rig.press(au::KeyCode::B, Rig::ctrl_shift()));
    rig.render();
    AURORA_TEST_REQUIRE_EQ(rig.router().size(), 2U);
    AURORA_TEST_CHECK_EQ(rig.banner_press_reports, 0U);  // 装钩不等于报钩：没点过就一次也不报

    rig.show(alpha);
    const au::Rect button = rig.exit_button();
    const au::Point at{.x = button.origin.x + 50.0F, .y = button.origin.y + 11.0F};
    rig.click(at);
    AURORA_TEST_CHECK_EQ(rig.banner_press_reports, 1U);
    AURORA_TEST_CHECK_EQ(rig.banner_report_group_size, 2U);
    AURORA_TEST_CHECK_EQ(rig.router().size(), 0U);

    // ③' 清组后横幅高度归 0、命中不到，同一落点的第二下既不清组也不上报（落点因此不会因空点而漂移）。
    rig.click(at);
    AURORA_TEST_CHECK_EQ(rig.banner_press_reports, 1U);
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_workspace_broadcast
