/// 测试类型: integration
/// 目标单元: include/borealis/ui/workspace_snapshot.h + src/ui/workspace_view.cpp（恢复链的接线半边）
/// 测试说明: 以一份「上次的布局」走完 `WorkspaceSnapshot::to_json` → `from_json` → 逐标签
///           `WorkspaceView::restore` 这条链（`SPEC.FEAT.WS.11` 批 6，判据 ①④ 的接线半边）。
///           判据 ① 在纯逻辑层与界面层各有证人（`utest_pane_tree` 的形状往返、
///           `itest_workspace_layout` 的形状落回界面）；本件补的是**两者之间那一跳**：写出去的
///           文档经一次文本往返后，新宿主拿到的标签数、连接类型、逐格料、格数、焦点与形状仍与
///           落盘前逐字一致。判据 ④ 的「每格恰一次初始尺寸」在 `itest_workspace_layout` 另有
///           一条同档用例；本件以同一份快照样本逐标签复算，证明四种连接腿的恢复路径都走同一条
///           去抖尾沿，而不是只有本地腿接上了 sink。
///
///           装配层（`src/main.cpp` 的读快照 → 开窗 → 重放与凭据重取）结构性无自动化证人
///           （稿 F21、判据 ⑤，`main.cpp` 不编入 runner）。本件不造假宿主去替它：标签条的
///           显示名、选中切换与窗口创建都落在那一层，这里只断「交到界面层手里的那份料」。
///
///           窗口尺寸固定 900×600、缩放 1：每格的行列由用例自己按字体度量复算，不从实现读，
///           否则「尺寸下发」的断言就退化成实现的自证。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/render/font_engine.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/width.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/grid_size_debounce.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/pane_tree.h"
#include "borealis/ui/workspace_snapshot.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"
#include "../../src/ui/workspace_view.h"

namespace borealis::test_cases::itest_workspace_restore {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::conn::ConnectionType;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::ui::GridSize;
using borealis::ui::PaneAxis;
using borealis::ui::PaneId;
using borealis::ui::PaneShape;
using borealis::ui::PaletteSpec;
using borealis::ui::SnapshotTab;
using borealis::ui::SnapshotWindow;
using borealis::ui::TerminalView;
using borealis::ui::Typography;
using borealis::ui::WorkspaceSnapshot;
using borealis::ui::WorkspaceView;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 200;
constexpr float kPaddingDp = 4.0F;
constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 600;
constexpr int kBlinkPeriodMs = 100;

auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();

/// @brief 传输连接替身：只记被下发的尺寸（判据 ④ 的会话侧证人）。
class RecordingConnection final : public Connection {
  public:
    auto start(ConnectionEvents & /*events*/) -> void override { alive_ = true; }

    auto write(std::span<const std::byte> /*bytes*/) -> void override {}

    auto resize(Size size) -> void override { resizes.push_back(size); }

    auto close() -> void override { alive_ = false; }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    std::vector<Size> resizes;

  private:
    bool alive_ = false;
};

[[nodiscard]] auto test_font() -> au::Font {
    return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
}

/// @brief 独立复算一个矩形装得下的行列数（与绘制侧同一档字体度量，预期不抄实现输出）。
[[nodiscard]] auto expected_grid(double width, double height) -> GridSize {
    const auto metrics = au::render::FontEngine::monospace_cell(test_font(), 1.0F);
    const borealis::ui::GridGeometry grid = borealis::ui::make_geometry(
        borealis::ui::CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px}, 1.0,
        borealis::ui::LogicalSize{width, height}, kPaddingDp);
    return GridSize{grid.columns, grid.rows};
}

/// @brief 一条快照标签的恢复宿主：视图、会话、无头窗口与工作区一条龙。
///
/// 成员声明次序即析构次序的倒序：窗口最先声明、最后消散（帧缓冲要活到断言结束）；工作区持视图，
/// 故须在会话表之前散掉——`~TerminalView` 要往背后的会话回写撤账空响应。
class TabHost final {
  public:
    explicit TabHost(const SnapshotTab &tab) {
        borealis::ui::PaneTree probe{1U};
        if (!probe.restore(tab.shape, 1U)) {
            return;  // 坏形状：恢复失败的唯一读数，测试侧据此断言「没建起来」
        }
        leaves_ = probe.pane_count();
        std::vector<std::shared_ptr<TerminalView>> views;
        views.reserve(leaves_);
        for (std::size_t order = 0U; order < leaves_; ++order) {
            auto connection = std::make_unique<RecordingConnection>();
            RecordingConnection *raw = connection.get();
            auto session =
                std::make_unique<Session>(std::move(connection), kNominalSize, kScrollback, width_policy);
            auto view = std::make_shared<TerminalView>(
                *session, palette_, test_font(), Typography{}, kPaddingDp,
                std::chrono::milliseconds{kBlinkPeriodMs}, TerminalView::InteractionOptions{});
            session->start();
            connections_.push_back(raw);
            sessions_.push_back(std::move(session));
            views.push_back(std::move(view));
        }
        WorkspaceView::Hooks hooks;
        hooks.dispatch_grid = [this](PaneId pane, GridSize size) -> void {
            dispatches_.emplace_back(pane, size);
            sessions_[static_cast<std::size_t>(pane - 1U)]->resize(Size{size.columns, size.rows});
        };
        workspace_ = WorkspaceView::restore(std::move(views), tab.shape, tab.focused_order, std::move(hooks));
        if (workspace_ == nullptr) {
            return;
        }
        root_ = au::Node{std::static_pointer_cast<au::Widget>(workspace_)};
        present();
        settle();
    }

    [[nodiscard]] auto restored() const noexcept -> WorkspaceView * { return workspace_.get(); }
    [[nodiscard]] auto leaves() const noexcept -> std::size_t { return leaves_; }
    [[nodiscard]] auto dispatches() const noexcept -> const std::vector<std::pair<PaneId, GridSize>> & {
        return dispatches_;
    }
    [[nodiscard]] auto resize_count(PaneId pane) const -> std::size_t {
        const std::size_t index = static_cast<std::size_t>(pane - 1U);
        return index < connections_.size() ? connections_[index]->resizes.size() : 0U;
    }
    [[nodiscard]] auto resize_size(PaneId pane, std::size_t index) const -> std::optional<Size> {
        const std::size_t slot = static_cast<std::size_t>(pane - 1U);
        if (slot >= connections_.size() || index >= connections_[slot]->resizes.size()) {
            return std::nullopt;
        }
        return connections_[slot]->resizes[index];
    }
    [[nodiscard]] auto workspace() noexcept -> WorkspaceView & { return *workspace_; }

    /// @brief 再排一帧（不结算去抖）：用于「重复呈现不得重发」的判据。
    auto present() -> void {
        if (workspace_ != nullptr) {
            (void)window_.present_root(root_);
        }
    }

    /// @brief 结算去抖尾沿：把「此刻之后一个静默窗口」当作当前时刻交进去，再排一帧。
    auto settle() -> void {
        if (workspace_ == nullptr) {
            return;
        }
        workspace_->flush_grid_sizes(std::chrono::steady_clock::now() +
                                     borealis::ui::kGridResizeQuietPeriod);
        present();
    }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到断言结束。
    PaletteSpec palette_{};
    std::vector<std::unique_ptr<Session>> sessions_;
    std::vector<RecordingConnection *> connections_;
    std::shared_ptr<WorkspaceView> workspace_;
    au::Node root_;
    std::size_t leaves_ = 0U;
    std::vector<std::pair<PaneId, GridSize>> dispatches_;
};

/// @brief 造一枚叶子：序位就是它在阅读序里的第几格。
[[nodiscard]] auto leaf(std::size_t order) -> PaneShape {
    PaneShape shape;
    shape.leaf = true;
    shape.order = order;
    return shape;
}

/// @brief 造一枚容器层；长度由调用方给，便于让往返带上非缺省比例。
[[nodiscard]] auto layer(PaneAxis axis, std::vector<PaneShape> kids, std::vector<double> lengths) -> PaneShape {
    PaneShape shape;
    shape.leaf = false;
    shape.axis = axis;
    shape.lengths = std::move(lengths);
    shape.children = std::move(kids);
    return shape;
}

[[nodiscard]] auto local_spec(std::string_view directory) -> borealis::conn::LocalTerminalSpec {
    auto spec = borealis::conn::LocalTerminalSpec{};
    spec.command_line = "/bin/bash -l";
    spec.working_directory = std::string{directory};
    spec.environment.emplace("EDITOR", "vis");
    return spec;
}

[[nodiscard]] auto ssh_profile() -> borealis::conn::SshProfile {
    auto ssh = borealis::conn::SshProfile{};
    ssh.host = "gw.example.com";
    ssh.port = 2222;
    ssh.user = "operator";
    ssh.auth_method = "privatekey";
    ssh.identity_file = "/home/dev/.ssh/id_ed25519";
    ssh.agent_forwarding = true;
    ssh.known_hosts_policy = borealis::conn::KnownHostsPolicy::Ask;
    ssh.credential = borealis::conn::SecretHandle::reference("vault://borealis/gw");
    return ssh;
}

[[nodiscard]] auto serial_profile() -> borealis::conn::SerialProfile {
    auto serial = borealis::conn::SerialProfile{};
    serial.device_path = "/dev/ttyUSB7";
    serial.baud = 57600;
    serial.data_bits = 7;
    serial.stop_bits = 2;
    serial.parity = "even";
    serial.flow = "hardware";
    serial.line_ending = "CRLF";
    serial.encoding = "UTF-8";
    return serial;
}

[[nodiscard]] auto telnet_profile() -> borealis::conn::TelnetProfile {
    auto telnet = borealis::conn::TelnetProfile{};
    telnet.host = "switch.example.com";
    telnet.port = 2323;
    return telnet;
}

/// @brief 一份两扇窗、四腿俱全的样本：第一扇四标签（本地三格嵌套 + SSH 两格 + 串口 + Telnet），
///        第二扇单标签单格（窗序与「恢复几扇」的接线在装配层，本件只验逐标签料的形态）。
[[nodiscard]] auto sample_snapshot() -> WorkspaceSnapshot {
    const PaneShape local_shape = layer(
        PaneAxis::Horizontal,
        {leaf(0U), layer(PaneAxis::Vertical, {leaf(1U), leaf(2U)}, {0.75, 0.25})}, {2.0, 0.5});

    auto local_tab = SnapshotTab{};
    local_tab.kind = ConnectionType::Local;
    local_tab.title = "build box";
    local_tab.panes = {local_spec("/home/dev/src"), local_spec("/home/dev"), local_spec("/var/log")};
    local_tab.shape = local_shape;
    local_tab.focused_order = 2U;

    auto ssh_tab = SnapshotTab{};
    ssh_tab.kind = ConnectionType::Ssh;
    ssh_tab.profile_id = "gw";
    ssh_tab.ssh = ssh_profile();
    ssh_tab.shape = layer(PaneAxis::Horizontal, {leaf(0U), leaf(1U)}, {1.0, 3.0});
    ssh_tab.focused_order = 1U;

    auto serial_tab = SnapshotTab{};
    serial_tab.kind = ConnectionType::Serial;
    serial_tab.profile_id = "console-0";
    serial_tab.title = "console";
    serial_tab.serial = serial_profile();

    auto telnet_tab = SnapshotTab{};
    telnet_tab.kind = ConnectionType::Telnet;
    telnet_tab.profile_id = "sw-2";
    telnet_tab.telnet = telnet_profile();

    auto first = SnapshotWindow{};
    first.tabs = {std::move(local_tab), std::move(ssh_tab), std::move(serial_tab), std::move(telnet_tab)};
    first.selected_tab = 1U;  // 选中 SSH 那一格：恢复腿按 selected_tab 取焦点在装配层

    auto second = SnapshotWindow{};
    auto second_tab = SnapshotTab{};
    second_tab.kind = ConnectionType::Local;
    second_tab.panes = {local_spec("/srv")};
    second.tabs = {std::move(second_tab)};
    second.selected_tab = 0U;

    return WorkspaceSnapshot{.windows = {std::move(first), std::move(second)}};
}

/// @brief 样本第一扇窗的标签类型，按标签条次序。
constexpr ConnectionType kFirstWindowKinds[] = {ConnectionType::Local, ConnectionType::Ssh,
                                                ConnectionType::Serial, ConnectionType::Telnet};

}  // namespace

AURORA_TEST_CASE(a_saved_layout_rebuilds_every_tab_and_its_panes_in_a_new_host) {
    const WorkspaceSnapshot saved = sample_snapshot();
    const std::optional<std::string> text = saved.to_json();
    AURORA_TEST_REQUIRE(text.has_value());

    // 新宿主读回：解码出的是一份独立的值，全文等值即「写侧漏键 / 读侧丢字段」的总闸。
    const borealis::ui::SnapshotDecodeResult decoded = WorkspaceSnapshot::from_json(*text);
    AURORA_TEST_REQUIRE_MSG(decoded.snapshot.has_value(), decoded.reason);
    AURORA_TEST_CHECK_MSG(*decoded.snapshot == saved, "the document lost a field on its way through JSON");
    const WorkspaceSnapshot &reloaded = *decoded.snapshot;

    AURORA_TEST_REQUIRE_EQ(reloaded.windows.size(), 2U);
    AURORA_TEST_REQUIRE_EQ(reloaded.windows[0].tabs.size(), 4U);
    AURORA_TEST_REQUIRE_EQ(reloaded.windows[0].selected_tab, 1U);
    AURORA_TEST_REQUIRE_EQ(reloaded.windows[1].tabs.size(), 1U);
    AURORA_TEST_REQUIRE_EQ(reloaded.windows[1].selected_tab, 0U);

    // 标签数、连接类型与手动标题在这条链上逐字回来（显示名的折算在装配层，题面不在这里）。
    for (std::size_t index = 0U; index < reloaded.windows[0].tabs.size(); ++index) {
        AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[index].kind, kFirstWindowKinds[index]);
    }
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[0].title, std::string{"build box"});
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[1].title, std::string{});
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[2].title, std::string{"console"});

    // 档案块与逐格有效料按类型各归各位：SSH 的句柄仍是引用而非明文，串口/Telnet 的档案还在。
    AURORA_TEST_REQUIRE(reloaded.windows[0].tabs[1].ssh.has_value());
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[1].ssh->host, std::string{"gw.example.com"});
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[1].ssh->credential.kind,
                         borealis::conn::SecretKind::Reference);
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[1].ssh->credential.vault_id,
                         std::string{"vault://borealis/gw"});
    AURORA_TEST_REQUIRE(reloaded.windows[0].tabs[2].serial.has_value());
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[2].serial->device_path, std::string{"/dev/ttyUSB7"});
    AURORA_TEST_REQUIRE(reloaded.windows[0].tabs[3].telnet.has_value());
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[3].telnet->host, std::string{"switch.example.com"});
    AURORA_TEST_REQUIRE_EQ(reloaded.windows[0].tabs[0].panes.size(), 3U);
    AURORA_TEST_CHECK_EQ(reloaded.windows[0].tabs[0].panes[1].working_directory, std::string{"/home/dev"});

    // 逐标签把料交回界面层：格数、阅读序身份、形状与模型焦点都得对得上。
    for (const SnapshotWindow &window : reloaded.windows) {
        for (const SnapshotTab &tab : window.tabs) {
            TabHost host{tab};
            AURORA_TEST_REQUIRE_MSG(host.restored() != nullptr, "a valid tab was refused by the workspace layer");
            WorkspaceView &ws = host.workspace();
            AURORA_TEST_CHECK_EQ(ws.pane_count(), host.leaves());
            AURORA_TEST_CHECK_MSG(ws.pane_shape() == tab.shape,
                                  "the shape handed back is not the one saved");

            std::vector<PaneId> expected_ids;
            expected_ids.reserve(host.leaves());
            for (std::size_t order = 0U; order < host.leaves(); ++order) {
                expected_ids.push_back(static_cast<PaneId>(order) + 1U);
            }
            AURORA_TEST_CHECK_MSG(ws.panes_in_order() == expected_ids, "restored pane identities drifted");
            // 选中格按序位落回标识，不是「恒第一格」：三格标签选序位 2、两格标签选序位 1。
            AURORA_TEST_CHECK_EQ(ws.focused_pane(), static_cast<PaneId>(tab.focused_order) + 1U);
            for (const PaneId pane : expected_ids) {
                AURORA_TEST_CHECK(ws.view_of(pane) != nullptr);
            }
            // 每格一件视图：共用一份就是两格显示同一屏（切分腿花了整批才拧过来的那条）。
            for (std::size_t left = 0U; left < host.leaves(); ++left) {
                for (std::size_t right = left + 1U; right < host.leaves(); ++right) {
                    AURORA_TEST_CHECK(ws.view_of(static_cast<PaneId>(left) + 1U) !=
                                      ws.view_of(static_cast<PaneId>(right) + 1U));
                }
            }
            // 判据 ④：恢复的每一格都收到过一次初始尺寸（逐标签复算，覆盖四种连接腿）。
            AURORA_TEST_CHECK_EQ(host.dispatches().size(), host.leaves());
            for (const PaneId pane : expected_ids) {
                AURORA_TEST_CHECK_EQ(host.resize_count(pane), 1U);
            }
        }
    }
}

AURORA_TEST_CASE(a_restored_tab_dispatches_one_initial_grid_per_pane_and_never_twice) {
    const WorkspaceSnapshot saved = sample_snapshot();
    AURORA_TEST_REQUIRE_EQ(saved.windows.size(), 2U);
    const SnapshotTab &tab = saved.windows[0].tabs[0];  // 本地三格：嵌套形状把比例也带上

    TabHost host{tab};
    AURORA_TEST_REQUIRE(host.restored() != nullptr);
    AURORA_TEST_REQUIRE_EQ(host.leaves(), 3U);
    AURORA_TEST_CHECK_EQ(host.dispatches().size(), 3U);

    WorkspaceView &ws = host.workspace();
    const auto &layout = ws.current_layout();
    AURORA_TEST_REQUIRE_EQ(layout.boxes.size(), 3U);
    for (const borealis::ui::PaneBox &box : layout.boxes) {
        // 每格恰一条、且是该格自己的矩形折出来的行列（不是共享一份、也不是首格那一份）。
        AURORA_TEST_REQUIRE_EQ(host.resize_count(box.pane), 1U);
        const std::optional<Size> size = host.resize_size(box.pane, 0U);
        AURORA_TEST_REQUIRE(size.has_value());
        const GridSize expected = expected_grid(box.box.width, box.box.height);
        AURORA_TEST_CHECK_MSG(size->columns == expected.columns && size->rows == expected.rows,
                              "pane " + std::to_string(box.pane) + " was told another pane's grid");
        // 视图自己的网格读数与下发值一致：屏上那格与告诉会话的那格是同一份。
        const TerminalView *view = ws.view_of(box.pane);
        AURORA_TEST_REQUIRE(view != nullptr);
        AURORA_TEST_CHECK_EQ(view->grid_geometry().columns, expected.columns);
        AURORA_TEST_CHECK_EQ(view->grid_geometry().rows, expected.rows);
    }

    // 再呈现一帧并结算：布局没变，净变化为零的条目不得重发（去抖表那条既有纪律）。
    host.present();
    host.settle();
    AURORA_TEST_CHECK_EQ(host.dispatches().size(), 3U);
    for (const borealis::ui::PaneBox &box : layout.boxes) {
        AURORA_TEST_CHECK_EQ(host.resize_count(box.pane), 1U);
    }
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_workspace_restore
