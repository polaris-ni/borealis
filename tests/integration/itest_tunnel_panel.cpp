// ============================================================
// 隧道管理面板集成用例（tests/integration/itest_tunnel_panel.cpp）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.08 面板腿（UI_TUNNEL.draft.md §4 的「无头真派发」判据）：
// 行表随快照刷新、启停按钮经派发链呼 Hooks、校验/冲突拒绝落盘、删除两段式。
// 驱动台形态照 itest_settings_panel 的 Harness（同一套「present_root 排真实
// 布局 + hit_test_chain 取命中 + EventDispatcher 发真事件」三步），只是精简到
// 本件用得到的腿：按显示标签找按钮、按行序找输入框、真点击聚焦后打字。
//
// 判据一律走面板交出的观测面（visible_rows / editor_open / notice），浮层树
// 只用于「按钮真点得到」这一条派发腿——行序变了重排版会挪位置，按坐标断言会
// 把排版改动误报成功能回归。
// ============================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/button.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/tunnel_format.h"
#include "../../src/ui/tunnel_panel.h"

namespace borealis::test_cases::itest_tunnel_panel {

namespace {

namespace conn = borealis::conn;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 760;

/// @brief 装配层替身：定义表、快照、六条 Hooks 的调用留痕。
struct Probe {
    std::vector<conn::TunnelSpec> specs;
    std::map<std::string, ui::TunnelRuntime> runtimes;
    std::vector<conn::Profile> profiles;
    std::vector<std::string> start_calls;
    std::vector<std::string> stop_calls;
    std::vector<std::vector<conn::TunnelSpec>> persisted;

    [[nodiscard]] auto hooks() -> ui::TunnelPanel::Hooks {
        ui::TunnelPanel::Hooks out;
        out.load = [this]() -> std::vector<conn::TunnelSpec> { return specs; };
        out.persist = [this](const std::vector<conn::TunnelSpec> &table) -> bool {
            persisted.push_back(table);
            return true;
        };
        out.profiles = [this]() -> std::vector<conn::Profile> { return profiles; };
        out.start = [this](const conn::TunnelSpec &spec) -> void { start_calls.push_back(spec.id); };
        out.stop = [this](const std::string &id) -> void { stop_calls.push_back(id); };
        out.snapshot = [this]() -> std::map<std::string, ui::TunnelRuntime> { return runtimes; };
        return out;
    }
};

/// @brief 夹具：两条定义（-L 占 1080 / -D 占 1081）+ 一份可静默解析的 SSH 档案。
[[nodiscard]] auto make_probe() -> Probe {
    Probe probe;
    conn::TunnelSpec a;
    a.id = "tun-a";
    a.name = "mirror-db";
    a.kind = conn::TunnelKind::Local;
    a.listen_port = 1080;
    a.target_host = "db01.internal";
    a.target_port = 5432;
    a.profile_id = "prof-ssh";
    conn::TunnelSpec b;
    b.id = "tun-b";
    b.name = "socks";
    b.kind = conn::TunnelKind::Dynamic;
    b.listen_port = 1081;
    b.profile_id = "prof-ssh";
    probe.specs = {a, b};
    conn::Profile ssh;
    ssh.id = "prof-ssh";
    ssh.name = "srv";
    ssh.type = conn::ConnectionType::Ssh;
    ssh.ssh.host = "example.org";
    probe.profiles = {ssh};
    return probe;
}

struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
};

/// @brief 无头窗口 + 真布局 + 真指针派发的驱动台（itest_settings_panel 同族精简腿）。
class Harness {
  public:
    Harness() {
        host_ = std::make_shared<au::OverlayHost>();
        base_ = std::make_shared<au::Text>(
            aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
        (void)host_->add_overlay(au::Node{base_});  // 占住子节点 [0]：基础内容。
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;

    [[nodiscard]] auto attach(Probe &probe) -> std::unique_ptr<ui::TunnelPanel> {
        return std::make_unique<ui::TunnelPanel>(*host_, probe.hooks());
    }
    auto render() -> void {
        static_cast<void>(window_.present_root(root_));
    }

    /// @brief 打开面板并排两帧（浮层入树 + 布局落 bounds）。
    auto open(ui::TunnelPanel &panel) -> void {
        panel.open();
        render();
        render();
    }

    /// @brief 排帧后按 tick 走一帧状态泵（快照改动经这里进行表）。
    auto tick(ui::TunnelPanel &panel) -> void {
        panel.tick();
        render();
        render();
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

    [[nodiscard]] auto press(au::KeyCode key) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
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

    /// @brief 按解析后的显示标签找按钮（accessibility_label 与绘制同源，见 button.h i18n 契约）。
    [[nodiscard]] auto find_button(const std::string &label, std::size_t nth = 1) -> HitSpot {
        return find("Button", nth, [&label](au::Widget *widget) -> bool {
            const auto *button = dynamic_cast<const au::Button *>(widget);
            return button != nullptr && button->accessibility_label() == label;
        });
    }

    /// @brief 按显示标签找正文 Text（冲突红提示的可见性判据走这条）。
    [[nodiscard]] auto find_text(const std::string &label) -> HitSpot {
        return find("Text", 1, [&label](au::Widget *widget) -> bool {
            const auto *text = dynamic_cast<const au::Text *>(widget);
            return text != nullptr && text->accessibility_label() == label;
        });
    }

    /// @brief 按行序取第 nth 枚输入框（编辑器字段次序＝构造次序，判据只点已知的那几格）。
    [[nodiscard]] auto find_input(std::size_t nth) -> HitSpot {
        return find("TextInput", nth);
    }

    [[nodiscard]] auto focus_name() -> std::string {
        auto *w = focus_.focused();
        return w == nullptr ? std::string{"none"} : std::string{w->type_name()};
    }

    [[nodiscard]] auto overlay_count() const -> std::size_t {
        return host_->overlay_count();
    }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        static_cast<void>(surface->begin_frame(kWindowWidth, kWindowHeight));
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Text> base_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;
};

}  // namespace

// ------------------------------------------------------------
// 判据①（稿 §4）：行表随快照刷新（D6①）
// ------------------------------------------------------------
AURORA_TEST_CASE(rows_follow_snapshot_across_ticks) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK(rows[0].spec.id == "tun-a");
    AURORA_TEST_CHECK(rows[0].runtime.state == conn::TunnelState::Stopped);

    probe.runtimes["tun-a"].state = conn::TunnelState::Active;
    probe.runtimes["tun-a"].bound_port = 8730;
    h.tick(*panel);
    rows = panel->visible_rows();
    AURORA_TEST_CHECK(rows[0].runtime.state == conn::TunnelState::Active);
    // 无快照的定义照旧 Stopped（tunnel_rows 缺省腿）。
    AURORA_TEST_CHECK(rows[1].runtime.state == conn::TunnelState::Stopped);

    // 面板关着 tick 是空操作——隧道对象归装配层，行表只在开时随泵走。
    panel->close();
    h.render();
    probe.runtimes["tun-b"].state = conn::TunnelState::Dialing;
    panel->tick();
    AURORA_TEST_CHECK(!panel->is_open());
}

// ------------------------------------------------------------
// 判据②：启停按钮经真实派发呼 Hooks（标号 1/6 的接缝腿）
// ------------------------------------------------------------
AURORA_TEST_CASE(toggle_buttons_dispatch_to_hooks) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto start_spot = h.find_button(ui::settings_label("tunnel.action.start"));
    AURORA_TEST_REQUIRE_MSG(start_spot.widget != nullptr, "the start button is not dispatch-reachable");
    h.click(start_spot);
    AURORA_TEST_REQUIRE_EQ(probe.start_calls.size(), 1U);
    AURORA_TEST_CHECK(probe.start_calls[0] == "tun-a");

    // 快照转 Dialing（运行中）后同一行的按钮换成「停止」——再点它呼 stop 腿。
    probe.runtimes["tun-a"].state = conn::TunnelState::Dialing;
    h.tick(*panel);
    auto stop_spot = h.find_button(ui::settings_label("tunnel.action.stop"));
    AURORA_TEST_REQUIRE_MSG(stop_spot.widget != nullptr, "the stop button is not dispatch-reachable");
    h.click(stop_spot);
    AURORA_TEST_REQUIRE_EQ(probe.stop_calls.size(), 1U);
    AURORA_TEST_CHECK(probe.stop_calls[0] == "tun-a");
}

// ------------------------------------------------------------
// 判据③：校验与冲突在提交时拒绝（标号 5：红提示 + 不落盘）
// ------------------------------------------------------------
AURORA_TEST_CASE(conflict_is_shown_and_save_refused) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto new_spot = h.find_button(ui::settings_label("tunnel.action.new"));
    AURORA_TEST_REQUIRE(new_spot.widget != nullptr);
    h.click(new_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK_TRUE(panel->editor_open());

    // 字段行序：名称、监听地址、监听端口、目标主机、目标端口、重试三格。
    auto name_spot = h.find_input(1);
    AURORA_TEST_REQUIRE(name_spot.widget != nullptr);
    h.click(name_spot);
    static_cast<void>(h.type("dup"));
    auto port_spot = h.find_input(3);
    AURORA_TEST_REQUIRE(port_spot.widget != nullptr);
    h.click(port_spot);
    static_cast<void>(h.type("1080"));  // 与 tun-a 抢同一监听点（127.0.0.1:1080）。
    auto host_spot = h.find_input(4);
    AURORA_TEST_REQUIRE(host_spot.widget != nullptr);
    h.click(host_spot);
    static_cast<void>(h.type("db02.internal"));
    auto tport_spot = h.find_input(5);
    AURORA_TEST_REQUIRE(tport_spot.widget != nullptr);
    h.click(tport_spot);
    static_cast<void>(h.type("6666"));

    std::string values;
    for (std::size_t i = 1; i <= 5; ++i) {
        auto spot = h.find_input(i);
        auto *ti = dynamic_cast<au::TextInput *>(spot.widget);
        values += "<" + (ti ? ti->value() : std::string{"?"}) + ">";
    }
    auto focused = h.focus_name();
    auto save_spot = h.find_button(ui::settings_label("tunnel.editor.action.save"));
    AURORA_TEST_REQUIRE(save_spot.widget != nullptr);
    h.click(save_spot);
    h.render();
    h.render();

    // 红提示在场（文案就是 tunnel_format 之外唯一的一处冲突措辞）。
    auto notice_spot = h.find_text(ui::settings_label("tunnel.editor.issue_conflict"));
    AURORA_TEST_REQUIRE_MSG(notice_spot.widget != nullptr,
                            "the conflict notice is not shown on the dispatch chain");
    AURORA_TEST_CHECK_TRUE(panel->editor_open());  // 拒绝关闭：草稿还在。
    AURORA_TEST_CHECK_EQ(probe.persisted.size(), 0U);  // 未落盘。
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 2U);
    AURORA_TEST_CHECK(panel->editor_draft().name == "dup");  // 观测面：控件值已折进草稿。

    // 改端口避让后同一份草稿可保存（编辑→改→保存的合法腿）。红提示占了一行，
    // 保存钮已下移——重取坐标，不复用旧点。
    h.click(port_spot);
    static_cast<void>(h.press(au::KeyCode::End));
    for (int i = 0; i < 4; ++i) {
        static_cast<void>(h.press(au::KeyCode::Backspace));
    }
    static_cast<void>(h.type("1099"));
    save_spot = h.find_button(ui::settings_label("tunnel.editor.action.save"));
    AURORA_TEST_REQUIRE_MSG(save_spot.widget != nullptr, "the save button moved out of reach");
    h.click(save_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->editor_open());
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_CHECK_EQ(probe.persisted.back().size(), 3U);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 3U);
}

// ------------------------------------------------------------
// 判据④：删除两段式（标号 6：运行中先确认，停止态直接删）
// ------------------------------------------------------------
AURORA_TEST_CASE(delete_is_two_stage_for_running_rows) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    probe.runtimes["tun-a"].state = conn::TunnelState::Active;
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto delete_spot = h.find_button(ui::settings_label("tunnel.action.delete"));
    AURORA_TEST_REQUIRE(delete_spot.widget != nullptr);
    h.click(delete_spot);  // 第一行是运行中的 tun-a。
    h.render();
    h.render();
    AURORA_TEST_CHECK_TRUE(panel->delete_confirm_open());
    AURORA_TEST_CHECK_EQ(probe.persisted.size(), 0U);  // 确认前不落盘。

    auto confirm_spot = h.find_button(ui::settings_label("tunnel.confirm.action"));
    AURORA_TEST_REQUIRE_MSG(confirm_spot.widget != nullptr,
                            "the stop-and-delete button is not dispatch-reachable");
    h.click(confirm_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->delete_confirm_open());
    AURORA_TEST_REQUIRE_EQ(probe.stop_calls.size(), 1U);
    AURORA_TEST_CHECK(probe.stop_calls[0] == "tun-a");
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_CHECK_EQ(probe.persisted.back().size(), 1U);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 1U);
}

AURORA_TEST_CASE(stopped_row_deletes_without_confirm) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();  // 两条都是 Stopped（无快照）。
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto delete_spot = h.find_button(ui::settings_label("tunnel.action.delete"));
    AURORA_TEST_REQUIRE(delete_spot.widget != nullptr);
    h.click(delete_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->delete_confirm_open());  // 停止态：一段式，确认框不出现。
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_CHECK_EQ(probe.persisted.back().size(), 1U);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 1U);
}

// ------------------------------------------------------------
// 判据⑤：-R 0 端口的择定回报进上屏串（标号 4）
// ------------------------------------------------------------
AURORA_TEST_CASE(remote_zero_port_line_folds_bound_port) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    conn::TunnelSpec r;
    r.id = "tun-r";
    r.name = "reverse";
    r.kind = conn::TunnelKind::Remote;
    r.listen_port = 0;
    r.target_host = "127.0.0.1";
    r.target_port = 873;
    r.profile_id = "prof-ssh";
    probe.specs = {r};
    conn::Profile ssh;
    ssh.id = "prof-ssh";
    ssh.name = "srv";
    ssh.type = conn::ConnectionType::Ssh;
    probe.profiles = {ssh};

    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 1U);
    std::string pending_line = ui::tunnel_endpoint_line(rows[0]);
    AURORA_TEST_CHECK(pending_line.find(":0") != std::string::npos);
    // 「由服务器择定」注记的显示串在链上（词条本体由 utest 的覆盖率判据守住）。
    auto note_spot = h.find_text(ui::settings_label("tunnel.listen.server_picked"));
    AURORA_TEST_REQUIRE_MSG(note_spot.widget != nullptr, "the server-picked note is not drawn");

    probe.runtimes["tun-r"].state = conn::TunnelState::Active;
    probe.runtimes["tun-r"].bound_port = 8730;
    h.tick(*panel);
    rows = panel->visible_rows();
    std::string active_line = ui::tunnel_endpoint_line(rows[0]);
    AURORA_TEST_CHECK(active_line.find(":8730") != std::string::npos);
    auto chosen_spot = h.find_text(ui::settings_label("tunnel.listen.server_chosen"));
    AURORA_TEST_REQUIRE_MSG(chosen_spot.widget != nullptr, "the server-chosen note is not drawn");
}

// ------------------------------------------------------------
// 判据⑤（稿标号 5）：编辑对话框的承载档案下拉与自动启动开关真点即改草稿
//（D8②——开关拨动不重建对话框，字段集不随它变）
// ------------------------------------------------------------
AURORA_TEST_CASE(profile_dropdown_and_autostart_switch_write_the_draft) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    // 第二枚 SSH 档案：下拉得有两项可选，「选到哪一条」才是可判的事实。
    conn::Profile second;
    second.id = "prof-ssh-2";
    second.name = "srv2";
    second.type = conn::ConnectionType::Ssh;
    second.ssh.host = "second.example.org";
    probe.profiles.push_back(second);

    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    auto edit_spot = h.find_button(ui::settings_label("tunnel.action.edit"));
    AURORA_TEST_REQUIRE_MSG(edit_spot.widget != nullptr, "the edit button is not dispatch-reachable");
    h.click(edit_spot);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->editor_open());
    AURORA_TEST_CHECK(panel->editor_draft().profile_id == "prof-ssh");

    auto box_spot = h.find("Dropdown", 1);
    AURORA_TEST_REQUIRE_MSG(box_spot.widget != nullptr, "the profile dropdown is not reachable");
    auto *box = dynamic_cast<au::Dropdown *>(box_spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    AURORA_TEST_CHECK_EQ(box->option_count(), 2U);
    const auto box_bounds = box->window_bounds();
    AURORA_TEST_REQUIRE(box_bounds.has_value());

    // 点主框展开，再点下沿那一档选项行（面板画在布局盒之外，命中须靠控件自己
    // 声明的追加盒——点得到才证明下拉真能用，而不只是真画出来了）。
    h.click(box_spot);
    h.render();
    h.render();
    // 取样点取面板右缘往内：下拉面板横跨 240 dp，左半是压在「自动启动」那一行
    // 的开关上的（同行控件的布局盒会抢命中），右半才是面板独占的区域。
    const float item_x = box_bounds->origin.x + box_bounds->size.width - 12.0F;
    // 第二档（下标 1）＝主框下沿往下一整行再取行中：第一档还是当前那条档案，
    // 点它选不出任何变化，判据要的是「换了一条」。
    const float item_y = box_bounds->origin.y + box_bounds->size.height + 26.0F + 13.0F;
    auto *row_widget = h.hit(item_x, item_y);
    AURORA_TEST_REQUIRE_MSG(row_widget == box,
                            std::string{"row claims: "} + (row_widget == nullptr ? "null" : row_widget->type_name()));
    h.click(item_x, item_y);
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(box->selected_index(), 1);
    AURORA_TEST_CHECK(panel->editor_draft().profile_id == "prof-ssh-2");

    auto switch_spot = h.find("Switch", 1);
    AURORA_TEST_REQUIRE_MSG(switch_spot.widget != nullptr, "the autostart switch is not reachable");
    auto *sw = dynamic_cast<au::Switch *>(switch_spot.widget);
    AURORA_TEST_REQUIRE(sw != nullptr);
    AURORA_TEST_CHECK_FALSE(sw->value());
    h.click(switch_spot);
    h.render();
    h.render();
    AURORA_TEST_CHECK(sw->value());
    AURORA_TEST_CHECK(panel->editor_draft().autostart);
    AURORA_TEST_CHECK(panel->editor_open());  // 拨开关不重建对话框：草稿与控件都还在原地。

    // 保存即整表落盘，草稿里的档案与自启一并进去（D8② 的字段腿）。
    auto save_spot = h.find_button(ui::settings_label("tunnel.editor.action.save"));
    AURORA_TEST_REQUIRE(save_spot.widget != nullptr);
    h.click(save_spot);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    const auto &saved = probe.persisted.back();
    AURORA_TEST_REQUIRE_EQ(saved.size(), 2U);
    AURORA_TEST_CHECK(saved[0].profile_id == "prof-ssh-2");
    AURORA_TEST_CHECK(saved[0].autostart);
}

// ------------------------------------------------------------
// 判据⑥：同名新建各自拿到唯一 id（id 是快照与启停的记账键，共用即串台）
// ------------------------------------------------------------
AURORA_TEST_CASE(same_name_new_tunnels_get_distinct_ids) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    // 填一份「名称 dup + 指定监听端口」的最小合法定义（Local 式要有 target 两栏）
    // 并存下去。两次的监听端口不同，冲突闸不管它们——撞的只是名字。
    auto fill_and_save = [&](const char *port) -> void {
        auto new_spot = h.find_button(ui::settings_label("tunnel.action.new"));
        AURORA_TEST_REQUIRE(new_spot.widget != nullptr);
        h.click(new_spot);
        h.render();
        h.render();
        auto name_spot = h.find_input(1);
        AURORA_TEST_REQUIRE(name_spot.widget != nullptr);
        h.click(name_spot);
        static_cast<void>(h.type("dup"));
        auto port_spot = h.find_input(3);
        AURORA_TEST_REQUIRE(port_spot.widget != nullptr);
        h.click(port_spot);
        static_cast<void>(h.type(port));
        auto host_spot = h.find_input(4);
        AURORA_TEST_REQUIRE(host_spot.widget != nullptr);
        h.click(host_spot);
        static_cast<void>(h.type("db02.internal"));
        auto tport_spot = h.find_input(5);
        AURORA_TEST_REQUIRE(tport_spot.widget != nullptr);
        h.click(tport_spot);
        static_cast<void>(h.type("6666"));
        auto save_spot = h.find_button(ui::settings_label("tunnel.editor.action.save"));
        AURORA_TEST_REQUIRE_MSG(save_spot.widget != nullptr, "the save button is not reachable");
        h.click(save_spot);
        h.render();
        h.render();
    };
    fill_and_save("1099");
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    fill_and_save("1098");
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 2U);

    const auto &saved = probe.persisted.back();
    AURORA_TEST_REQUIRE_EQ(saved.size(), 4U);  // 原有两条 + 两条同名新建。
    AURORA_TEST_CHECK(saved[2].id == "tunnel:dup");
    AURORA_TEST_CHECK(saved[3].id == "tunnel:dup#2");
    AURORA_TEST_CHECK(saved[2].name == saved[3].name);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 4U);
}

}  // namespace borealis::test_cases::itest_tunnel_panel
