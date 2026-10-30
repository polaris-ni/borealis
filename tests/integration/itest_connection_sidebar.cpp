// ============================================================
// 连接管理器侧栏 quick connect 集成用例（tests/integration/itest_connection_sidebar.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.03` 批 7（稿 `CONN03_ASKHOP_QUICKJ.draft.md` D5/D6/D7）的侧栏接线腿：
// `-J` 好值 ⇒ `hooks_.connect` 收到跳链形状正确、id 带 `quick:` 前缀的临时档案；坏值 ⇒
// 不发起连接且留痕行上屏（一次一句话，机制同导入条数）。切分与折算的判据本体在
// utest_profile_views，这里锁的是「UI 只消费」那一侧的两条行为承诺。
// 驱动台形态照 itest_connection_wizard 的 Harness（`present_root` 排真实布局 +
// `hit_test_chain` 取命中 + EventDispatcher 发真事件）；串问泵腿在 main（裁决 7.83），
// 无自动化证人，按既句式源码审查＋真机走查担保。侧栏改常驻列后（裁决 7.180），这台把它的
// `slot_widget()` 摆进根 Row 第一格——不再是从 `OverlayHost` 摘挂的那张卡片，本文件因此多一条
// `SPEC.FEAT.CONN.07` 的形态面判据（两态同槽、宽度真让位、开合与落盘的分工）。
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"

// 私有头（裁决 D1① 同口径），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/connection_sidebar.h"
#include "../../src/ui/settings_i18n.h"

namespace borealis::test_cases::itest_connection_sidebar {

namespace {

namespace conn = borealis::conn;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 760;
constexpr double kRailWidthDp = 34.0;    ///< 折叠态那条图标轨的宽（屏 1 判据 ③，`src/ui/sidebar_rail.cpp`）。
constexpr double kPanelWidthDp = 320.0;  ///< 展开态同一条列的宽（`UI_OVERVIEW.draft.md` §2.2 与屏 2）。

/// @brief 布局读数比到半 dp：浮点宽度经 `Constraints::constrain` 与 dp/px 往返，逐位等值不是判据。
[[nodiscard]] auto nearly(double a, double b, double tolerance = 0.51) -> bool {
    return std::abs(a - b) < tolerance;
}

/// @brief 装配层替身：档案表 + connect 调用留痕（struct-of-回调同族）。
struct Probe {
    std::vector<conn::Profile> profiles;
    std::vector<conn::Profile> connected;
    std::vector<bool> collapsed_writes;

    [[nodiscard]] auto hooks() -> ui::ConnectionSidebar::Hooks {
        auto out = ui::ConnectionSidebar::Hooks{};
        out.load = [this]() -> std::vector<conn::Profile> { return profiles; };
        out.connect = [this](const conn::Profile &profile) -> void {
            connected.push_back(profile);
        };
        out.persist_collapsed = [this](bool collapsed) -> void {
            collapsed_writes.push_back(collapsed);
        };
        return out;
    }
};

struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
};

/// @brief 无头窗口 + 真布局 + 真指针派发的驱动台（itest_connection_wizard 同族精简腿）。
///
/// 根是一枚 `Row`，与装配层 `rebuild_root_children()` 那一格同形：侧栏的常驻槽位在**第一格**、
/// 其余宽度归终端区替身（裁决 7.180 后侧栏不再是从浮层宿主摘进摘出的卡片，所以这台也不再需要
/// `OverlayHost`）。
class Harness {
  public:
    Harness() {
        row_ = std::make_shared<au::Row>();
        row_->set_modifier(au::Modifier{}.fill_max_size());
        row_->set_cross_axis_alignment(au::CrossAxisAlignment::Stretch);
        // 终端区替身：吃掉轨/面板之外的全部宽度，让「面板把布局压窄」这件事在命中扫描里可见。
        terminal_ = std::make_shared<au::Column>();
        terminal_->set_modifier(au::Modifier{}.expand(1.0F).fill_max_size());
        auto &nodes = row_->child_nodes_mut();
        nodes.push_back(au::Node{terminal_});
        root_ = au::Node{std::static_pointer_cast<au::Widget>(row_)};
        focus_.set_root(&root_.widget());
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;

    /// @brief 建这一扇窗的那一条常驻列，并把槽位摆进根 Row 第一格（装配层在建窗时只做一次）。
    [[nodiscard]] auto attach(Probe &probe) -> std::unique_ptr<ui::ConnectionSidebar> {
        auto sidebar = std::make_unique<ui::ConnectionSidebar>(probe.hooks());
        auto &nodes = row_->child_nodes_mut();
        nodes.insert(nodes.begin(), au::Node{sidebar->slot_widget()});
        row_->mark_needs_layout();
        render();
        return sidebar;
    }

    /// @brief 终端区替身当前占宽（判「同一条列变宽」是不是**占布局**——浮层形态下这一格从不让位）。
    [[nodiscard]] auto terminal_width() const -> double {
        return static_cast<double>(terminal_->size().width);
    }

    auto render() -> void {
        static_cast<void>(window_.present_root(root_));
    }

    /// @brief 排两帧（单帧只够入树，布局落 bounds 要第二帧）。
    auto settle() -> void {
        render();
        render();
    }

    /// @brief 展开这一列并排两帧。
    auto open(ui::ConnectionSidebar &sidebar) -> void {
        sidebar.open();
        settle();
    }

    [[nodiscard]] auto chain_at(float x_dp, float y_dp) -> std::vector<au::HitNode> {
        return root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
    }

    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const auto chain = chain_at(x_dp, y_dp);
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    auto pointer(au::MouseAction action, float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        event.action = action;
        static_cast<void>(dispatcher_.dispatch_mouse(root_.widget(), event, &focus_));
    }

    auto click(float x_dp, float y_dp) -> void {
        pointer(au::MouseAction::Press, x_dp, y_dp);
        pointer(au::MouseAction::Release, x_dp, y_dp);
    }

    /// @brief 单击一个 Spot（先确认派发链在这一点仍交回它——点空不算点中）。
    auto click(const HitSpot &spot) -> void {
        AURORA_TEST_REQUIRE_MSG(spot.widget != nullptr, "the control is not dispatch-reachable");
        AURORA_TEST_REQUIRE_MSG(hit(spot.x, spot.y) == spot.widget,
                                "the probe point no longer claims the widget");
        click(spot.x, spot.y);
    }

    /// @brief 点一处并排两帧（留痕行随整块重建上屏，要重画才看得到）。
    auto click_and_settle(const HitSpot &spot) -> void {
        click(spot);
        render();
        render();
    }

    /// @brief 向当前焦点控件逐字符发真实文本输入。
    auto type(std::string_view text) -> bool {
        bool handled = false;
        for (const char ch : text) {
            au::TextInputEvent event;
            event.text = std::string(1U, ch);
            handled = au::EventDispatcher::dispatch(root_.widget(), event, focus_) || handled;
        }
        return handled;
    }

    /// @brief 扫描派发链，找第 nth 个满足 accept 的控件（行序＝扫描序，4 dp 步长）。
    [[nodiscard]] auto find(std::string_view type_name, std::size_t nth,
                            const std::function<bool(au::Widget *)> &accept = {}) -> HitSpot {
        std::vector<au::Widget *> seen;
        for (float y = 4.0F; y < static_cast<float>(kWindowHeight) - 4.0F; y += 4.0F) {
            for (float x = 20.0F; x < static_cast<float>(kWindowWidth) - 20.0F; x += 4.0F) {
                au::Widget *widget = hit(x, y);
                if (widget == nullptr || type_name != widget->type_name() ||
                    std::find(seen.begin(), seen.end(), widget) != seen.end()) {
                    continue;
                }
                if (accept && !accept(widget)) {
                    continue;
                }
                seen.push_back(widget);
                if (seen.size() == nth) {
                    return HitSpot{.widget = widget, .x = x, .y = y};
                }
            }
        }
        return HitSpot{};
    }

    /// @brief 按解析后的显示标签找按钮（`accessibility_label` 与绘制同源）。
    [[nodiscard]] auto find_button(const std::string &label, std::size_t nth = 1) -> HitSpot {
        return find("Button", nth, [&label](au::Widget *widget) -> bool {
            const auto *button = dynamic_cast<const au::Button *>(widget);
            return button != nullptr && button->accessibility_label() == label;
        });
    }

    /// @brief 按显示标签找正文 Text（留痕行的可见性判据走这条）。
    [[nodiscard]] auto find_text(const std::string &label) -> HitSpot {
        return find("Text", 1, [&label](au::Widget *widget) -> bool {
            const auto *text = dynamic_cast<const au::Text *>(widget);
            return text != nullptr && text->accessibility_label() == label;
        });
    }

    /// @brief quick connect 那一行的输入框（全树第一枚 TextInput：标号 2 排在搜索框之前）。
    [[nodiscard]] auto quick_input_spot() -> HitSpot { return find("TextInput", 1); }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        static_cast<void>(surface->begin_frame(kWindowWidth, kWindowHeight));
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<au::Row> row_;
    std::shared_ptr<au::Column> terminal_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;
};

/// @brief 向 quick connect 行灌一句话（点击抢焦，再逐字符发文本事件）。
auto type_quick(Harness &h, std::string_view text) -> void {
    h.click(h.quick_input_spot());
    AURORA_TEST_CHECK_TRUE(h.type(text));
}

auto click_connect(Harness &h) -> void {
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.connect")));
}

}  // namespace

// ------------------------------------------------------------
// 判据③（裁决 7.180 的形态面）：常驻列两态占同一个槽位，宽度是真的让位而不是压上去
// ------------------------------------------------------------
AURORA_TEST_CASE(the_resident_slot_is_the_rail_when_collapsed_and_the_panel_when_open) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto sidebar = h.attach(probe);

    // ⑴ 起步折叠（§4 N1）：槽里是那条 34 dp 的轨，面板内容此刻还没建；建列本身不是「改档」，故不写盘。
    AURORA_TEST_CHECK_FALSE(sidebar->is_open());
    h.settle();
    AURORA_TEST_CHECK_TRUE(nearly(sidebar->slot_widget()->size().width, kRailWidthDp));
    AURORA_TEST_CHECK_TRUE(nearly(h.terminal_width(), kWindowWidth - kRailWidthDp));
    AURORA_TEST_CHECK_TRUE(probe.collapsed_writes.empty());

    // ⑵ 展开＝同一条列变宽：槽 320 dp、终端区让掉多出来的那 286 dp（旧浮层形态下这一格一寸不让），
    //     落盘记一次「不折叠」。
    sidebar->open();
    h.settle();
    AURORA_TEST_CHECK_TRUE(sidebar->is_open());
    AURORA_TEST_CHECK_TRUE(nearly(sidebar->slot_widget()->size().width, kPanelWidthDp));
    AURORA_TEST_CHECK_TRUE(nearly(h.terminal_width(), kWindowWidth - kPanelWidthDp));
    AURORA_TEST_REQUIRE_EQ(probe.collapsed_writes.size(), 1U);
    AURORA_TEST_CHECK_FALSE(probe.collapsed_writes.front());

    // ⑶ 配置是**来向**：`set_collapsed` 折回轨，却一回也不写盘——广播腿把自己再喂回去就是落盘风暴。
    sidebar->set_collapsed(true);
    h.settle();
    AURORA_TEST_CHECK_FALSE(sidebar->is_open());
    AURORA_TEST_CHECK_TRUE(nearly(sidebar->slot_widget()->size().width, kRailWidthDp));
    AURORA_TEST_CHECK_EQ(probe.collapsed_writes.size(), 1U);

    // ⑷ 轨上三枚的落点（人已裁的「三个视图直达」）：切到该视图并展开，面板 chips 与轨上亮的那一枚同源。
    sidebar->expand_to(conn::SidebarViewMode::Favorites);
    h.settle();
    AURORA_TEST_CHECK_TRUE(sidebar->is_open());
    AURORA_TEST_CHECK_TRUE(sidebar->view_mode() == conn::SidebarViewMode::Favorites);
    AURORA_TEST_CHECK_TRUE(sidebar->rail_active_view() == conn::SidebarViewMode::Favorites);
    AURORA_TEST_CHECK_TRUE(nearly(sidebar->slot_widget()->size().width, kPanelWidthDp));
    AURORA_TEST_CHECK_EQ(probe.collapsed_writes.size(), 2U);

    // ⑸ 面板标题行那一枚「收起」走的是同一条腿：折回轨并记一次折叠（旧语义「关掉浮层」的口径转移处）。
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.close")));
    AURORA_TEST_CHECK_FALSE(sidebar->is_open());
    AURORA_TEST_CHECK_TRUE(nearly(sidebar->slot_widget()->size().width, kRailWidthDp));
    AURORA_TEST_REQUIRE_EQ(probe.collapsed_writes.size(), 3U);
    AURORA_TEST_CHECK_TRUE(probe.collapsed_writes.back());
}

// ------------------------------------------------------------
// 判据①（稿 D5/D7）：`-J` 好值 ⇒ 临时档案带跳链形状交回装配层
// ------------------------------------------------------------
AURORA_TEST_CASE(quick_line_with_jump_tail_connects_as_temporary_profile) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto sidebar = h.attach(probe);
    h.open(*sidebar);

    type_quick(h, "target.example -J gw1:2222,deploy@core.example");
    click_connect(h);

    AURORA_TEST_REQUIRE_MSG(probe.connected.size() == 1, "a good -J line must connect exactly once");
    const auto &profile = probe.connected.front();
    AURORA_TEST_CHECK_TRUE(profile.id == "quick:target.example:22");
    AURORA_TEST_CHECK_TRUE(profile.type == conn::ConnectionType::Ssh);
    AURORA_TEST_REQUIRE(profile.ssh.proxy_jumps.size() == 2);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[0].host == "gw1" &&
                           profile.ssh.proxy_jumps[0].port == 2222);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[1].host == "core.example" &&
                           profile.ssh.proxy_jumps[1].user == "deploy" &&
                           profile.ssh.proxy_jumps[1].port == 22);
    // 临时档案缺省 agent 认证（不入库那条纪律原样继承，带跳亦然）。
    AURORA_TEST_CHECK_TRUE(profile.ssh.auth_method == "agent");
}

// ------------------------------------------------------------
// 判据②（稿 D6/D7）：坏跳表 ⇒ 不连 + 留痕行上屏（一次一句话）
// ------------------------------------------------------------
AURORA_TEST_CASE(bad_jump_list_refuses_and_paints_notice_line) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto sidebar = h.attach(probe);
    h.open(*sidebar);

    // 「bad hop」含空白，语法层整串拒 —— 坏值不可见比连错更糟，故必须留痕且不连。
    type_quick(h, "target.example -J bad hop");
    click_connect(h);

    AURORA_TEST_CHECK_TRUE(probe.connected.empty());
    const auto notice = h.find_text(ui::settings_label("connections.notice.quick_jump_bad"));
    AURORA_TEST_REQUIRE_MSG(notice.widget != nullptr, "the refusal must paint a notice line");
}

}  // namespace borealis::test_cases::itest_connection_sidebar
