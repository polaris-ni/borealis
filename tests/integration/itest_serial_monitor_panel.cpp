// ============================================================
// 串口监视面板集成用例（tests/integration/itest_serial_monitor_panel.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 两条延后子项的面板腿（UI_SERIAL_MONITOR.draft.md §6 批 3 那五行判据）：
// 行表随环刷新、时间戳开关联动、暂停中不读环、清空是唯一止血、丢弃留痕上屏。
// 驱动台形态照 itest_tunnel_panel 的 Harness（present_root 排真实布局 + hit_test_chain 取命中 +
// EventDispatcher 发真事件），精简到本件用得到的那几条腿。
//
// 判据一律走面板交出的观测面（visible_rows / notice / timestamp_enabled / is_open），浮层树只用于
// 「按钮与开关真点得到」这一条派发腿——行区重建会挪位置，按坐标断言会把排版改动误报成功能回归。
//
// 「快照被问了几次」（`snapshot_calls`）是本件独有的证人：暂停这条腿的判据不是「画面没变」，而是
// 「压根没去读环」——只看行表的话，「读了但算出同一份」与「没读」两种实现都给同一个读数。
// ============================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/button.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "borealis/session/raw_tap.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/serial_monitor_format.h"
#include "../../src/ui/serial_monitor_panel.h"
#include "../../src/ui/settings_i18n.h"

namespace borealis::test_cases::itest_serial_monitor_panel {

namespace {

namespace session = borealis::session;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 760;

/// @brief 装配层替身：一帧环投影 + 绑定串 + 三条腿的调用留痕。
struct Probe {
    ui::SerialMonitorPanel::Snapshot snapshot{};
    std::string binding;
    std::size_t snapshot_calls = 0U;
    std::size_t clear_calls = 0U;
    std::size_t binding_calls = 0U;

    [[nodiscard]] auto hooks() -> ui::SerialMonitorPanel::Hooks {
        ui::SerialMonitorPanel::Hooks out;
        out.snapshot = [this]() -> ui::SerialMonitorPanel::Snapshot {
            ++snapshot_calls;
            return snapshot;
        };
        out.clear = [this]() -> void {
            ++clear_calls;
            snapshot = ui::SerialMonitorPanel::Snapshot{};
        };
        out.binding_label = [this]() -> std::string {
            ++binding_calls;
            return binding;
        };
        return out;
    }
};

/// @brief 一条 run：给定方向、时刻与 ASCII 字面量折出的字节。
[[nodiscard]] auto make_run(const session::RawDirection direction, const std::int64_t timestamp_ms,
                            const std::string &text) -> session::RawRun {
    auto bytes = std::vector<std::byte>{};
    for (const char ch : text) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return session::RawRun{.direction = direction, .timestamp_ms = timestamp_ms, .bytes = std::move(bytes)};
}

/// @brief 两条缺省 run：一条入向一行设备文本、一条出向短命令（够占两行、又不撞 24 行上限）。
[[nodiscard]] auto make_probe() -> Probe {
    Probe probe;
    probe.snapshot.runs = {make_run(session::RawDirection::Received, 1'700'000'000'000LL, "OK\r\n"),
                           make_run(session::RawDirection::Transmitted, 1'700'000'001'000LL, "AT\r\n")};
    probe.binding = "srv /dev/ttyUSB0";
    return probe;
}

struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
};

/// @brief 无头窗口 + 真布局 + 真指针派发的驱动台（itest_tunnel_panel 同族精简腿）。
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

    [[nodiscard]] auto attach(Probe &probe) -> std::unique_ptr<ui::SerialMonitorPanel> {
        return std::make_unique<ui::SerialMonitorPanel>(*host_, probe.hooks());
    }
    auto render() -> void {
        static_cast<void>(window_.present_root(root_));
    }

    /// @brief 打开面板并排两帧（浮层入树 + 布局落 bounds）。
    auto open(ui::SerialMonitorPanel &panel) -> void {
        panel.open();
        render();
        render();
    }

    /// @brief 排帧后走一帧状态泵（环里多了字节经这里进行表）。
    auto tick(ui::SerialMonitorPanel &panel) -> void {
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

    /// @brief 按显示标签找正文 Text（留痕与常驻声明的可见性判据走这条）。
    [[nodiscard]] auto find_text(const std::string &label) -> HitSpot {
        return find("Text", 1, [&label](au::Widget *widget) -> bool {
            const auto *text = dynamic_cast<const au::Text *>(widget);
            return text != nullptr && text->accessibility_label() == label;
        });
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

/// @brief 时间戳前缀那 15 个字符（`[HH:MM:SS.mmm] `）在不在行上——两档开关各自的判据。
[[nodiscard]] auto has_prefix(const std::vector<ui::MonitorRow> &rows) -> bool {
    for (const auto &row : rows) {
        if (!row.timestamp_prefix.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ------------------------------------------------------------
// 判据①（稿 §4 标号 1／§6 批 3 第一行）：行表随环刷新，面板不在场时零成本
// ------------------------------------------------------------
AURORA_TEST_CASE(rows_follow_the_ring_across_ticks) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK(rows[0].direction_label == "RX");
    AURORA_TEST_CHECK(rows[1].direction_label == "TX");
    // 行文本与折算件同源：面板不自己拼第二份十六进制（`row_text` 就是那一行该显示的东西）。
    AURORA_TEST_CHECK(ui::row_text(rows[0]).find("4F 4B 0D 0A") != std::string::npos);
    AURORA_TEST_CHECK(ui::row_text(rows[0]).find("|OK..|") != std::string::npos);

    // 环里多一条 run ⇒ 下一帧多一行（latest-value，不做增量游标）。
    probe.snapshot.runs.push_back(make_run(session::RawDirection::Received, 1'700'000'002'000LL, "DONE\r\n"));
    h.tick(*panel);
    rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);

    // 面板关着 tick 是空操作：连环都不问（稿 §5「采集常驻、显示按需」的那半）。
    panel->close();
    h.render();
    const std::size_t calls_before = probe.snapshot_calls;
    probe.snapshot.runs.push_back(make_run(session::RawDirection::Received, 1'700'000'003'000LL, "X"));
    panel->tick();
    AURORA_TEST_CHECK(!panel->is_open());
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_before);
}

// ------------------------------------------------------------
// 判据②（稿 D6①）：时间戳开关真点即联动——前缀整列消失，行内容本身不动
// ------------------------------------------------------------
AURORA_TEST_CASE(timestamp_switch_rewrites_the_rows) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_CHECK_TRUE(panel->timestamp_enabled());  // 缺省开（D6①）。
    AURORA_TEST_CHECK_TRUE(has_prefix(panel->visible_rows()));

    auto switch_spot = h.find("Switch", 1);
    AURORA_TEST_REQUIRE_MSG(switch_spot.widget != nullptr, "the timestamp switch is not reachable");
    auto *sw = dynamic_cast<au::Switch *>(switch_spot.widget);
    AURORA_TEST_REQUIRE(sw != nullptr);
    AURORA_TEST_CHECK_TRUE(sw->value());

    h.click(switch_spot);
    AURORA_TEST_CHECK_FALSE(sw->value());
    // 回调只改状态、不当场重建（§4.5 第 25 条）：重建排在下一帧。
    h.tick(*panel);
    AURORA_TEST_CHECK_FALSE(panel->timestamp_enabled());
    const auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK_FALSE(has_prefix(rows));
    AURORA_TEST_CHECK(rows[0].direction_label == "RX");
    AURORA_TEST_CHECK(ui::row_text(rows[0]).find("4F 4B 0D 0A") != std::string::npos);
}

// ------------------------------------------------------------
// 判据③（稿 §1「暂停刷新」）：暂停中不读环，改档仍补一次重画
// ------------------------------------------------------------
AURORA_TEST_CASE(pause_stops_reading_the_ring) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto switch_spot = h.find("Switch", 2);
    AURORA_TEST_REQUIRE_MSG(switch_spot.widget != nullptr, "the pause switch is not reachable");
    auto *sw = dynamic_cast<au::Switch *>(switch_spot.widget);
    AURORA_TEST_REQUIRE(sw != nullptr);
    AURORA_TEST_CHECK_FALSE(sw->value());  // 缺省关（D7①）。

    h.click(switch_spot);
    AURORA_TEST_CHECK_TRUE(sw->value());
    h.tick(*panel);

    const std::size_t calls_while_painted = probe.snapshot_calls;
    const std::vector<ui::MonitorRow> frozen = panel->visible_rows();
    probe.snapshot.runs.push_back(make_run(session::RawDirection::Received, 1'700'000'004'000LL, "MORE\r\n"));
    h.tick(*panel);
    // 「压根没去读环」而不是「读了但算出同一份」：这条断言把两种实现分开。
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_while_painted);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), frozen.size());
}

// ------------------------------------------------------------
// 判据④（稿 D7①）：清空是本期唯一的止血动作
// ------------------------------------------------------------
AURORA_TEST_CASE(clear_button_is_the_only_bleed_stop) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 2U);

    // 关面板**不**清环，也不呼清空腿（D7①：重开看到的是历史）。
    panel->close();
    h.render();
    panel->open();
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(probe.clear_calls, 0U);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 2U);

    auto clear_spot = h.find_button(ui::settings_label("serial.monitor.action.clear"));
    AURORA_TEST_REQUIRE_MSG(clear_spot.widget != nullptr, "the clear button is not dispatch-reachable");
    h.click(clear_spot);
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.clear_calls, 1U);
    AURORA_TEST_CHECK_TRUE(panel->visible_rows().empty());
    // 空态那句画出来了（判据走派发链：本件不许把「等待数据」写成第二处措辞）。
    auto empty_spot = h.find_text(ui::settings_label("serial.monitor.empty"));
    AURORA_TEST_REQUIRE_MSG(empty_spot.widget != nullptr, "the empty state is not drawn");
}

// ------------------------------------------------------------
// 判据⑤（稿 D4①）：丢弃必须上屏——不报这一句，「最早那行」就会被当成「设备最早发的那行」
// ------------------------------------------------------------
AURORA_TEST_CASE(dropped_bytes_are_shown_and_cleared_with_the_ring) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    // 无丢弃即没有那句话（留痕恒空 = 无话可说，`notice()` 的族语义）。
    AURORA_TEST_CHECK(panel->notice().empty());
    probe.snapshot.dropped_bytes = 4096U;
    h.tick(*panel);
    const std::string dropped = panel->notice();
    AURORA_TEST_CHECK_TRUE(dropped.find("4096") != std::string::npos);
    AURORA_TEST_CHECK_TRUE(dropped.find("256") != std::string::npos);
    auto dropped_spot = h.find_text(dropped);
    AURORA_TEST_REQUIRE_MSG(dropped_spot.widget != nullptr, "the drop notice is not drawn on the chain");

    // 清空连丢弃累计一起归零：那一屏已经是新开始，再挂着 N 就是在说看不到的数据。
    auto clear_spot = h.find_button(ui::settings_label("serial.monitor.action.clear"));
    AURORA_TEST_REQUIRE(clear_spot.widget != nullptr);
    h.click(clear_spot);
    h.tick(*panel);
    AURORA_TEST_CHECK(panel->notice().empty());
    AURORA_TEST_CHECK(h.find_text(dropped).widget == nullptr);
}

// ------------------------------------------------------------
// 判据⑥（稿 §2 标题行 + §1 安全口径）：绑定随身份来去，「不落盘」那句常驻
// ------------------------------------------------------------
AURORA_TEST_CASE(binding_follows_the_identity_and_the_disclaimer_stays) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto binding_spot = h.find_text(probe.binding);
    AURORA_TEST_REQUIRE_MSG(binding_spot.widget != nullptr, "the binding label is not drawn");
    auto privacy_spot = h.find_text(ui::settings_label("serial.monitor.notice.privacy"));
    AURORA_TEST_REQUIRE_MSG(privacy_spot.widget != nullptr, "the never-persist note is not drawn");

    // 绑定失去身份（标签散了／非串口）就整格不画，而不是画一句「未知」；卡片其余部分照旧。
    probe.binding.clear();
    h.tick(*panel);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("serial.monitor.title")).widget != nullptr);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("serial.monitor.notice.privacy")).widget != nullptr);
    // 标题行的字节计数走的是词条，两档都有模板（覆盖闸之外唯一的一条同源判据）。
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("serial.monitor.bytes", {{"count", std::int64_t{8}}}))
                          .widget != nullptr);
}

}  // namespace borealis::test_cases::itest_serial_monitor_panel
