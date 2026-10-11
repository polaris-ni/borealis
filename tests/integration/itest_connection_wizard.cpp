// ============================================================
// 连接向导第三型（串口）集成用例（tests/integration/itest_connection_wizard.cpp）
// ------------------------------------------------------------
// SPEC.FEAT.CONN.05 批 3 的 UI 腿（`codespec/UI_SERIAL.draft.md` §2 标号 1–4 + 裁决 7.110
// D2①/D4①/D8①/D9①）：类型卡三张真点击、串口字段集九行的行集与缺省灌入、设备行「下拉 +
// 刷新 + 自定义」三件套的互斥切换与枚举发起时机、不可达项照列带归因且点不动、
// `validate_draft` 串口四档问题码逐档上屏、枚举止回投只在开着的串口步骤重建下拉。
// 驱动台形态照 itest_keys_panel 的 Harness（`present_root` 排真实布局 + `hit_test_chain`
// 取命中 + EventDispatcher 发真事件）；判据走向导自己交出的观测面（Hooks 收到的档案、
// 词条文案、Dropdown 的选中位与选项串），浮层树只用于「控件真点得到」这一条派发腿。
// 下拉选项行内的**置灰色**属绘制判据，按 AGENTS §4.4 第 33 条留真机走查（本沙箱无 USB 口，
// 见该稿 §0 F3）；这里锁的是「不可达行照列、选中被否决、产值不落进档案」三条机器可判事实。
// ============================================================

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/core/json.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"

// 私有头（裁决 D1① 同口径），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/connection_wizard.h"
#include "../../src/ui/serial_format.h"
#include "../../src/ui/settings_i18n.h"

namespace borealis::test_cases::itest_connection_wizard {

namespace {

namespace conn = borealis::conn;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 760;

[[nodiscard]] auto port(std::string path, conn::SerialPortAccess access) -> conn::SerialPortInfo {
    auto info = conn::SerialPortInfo{};
    info.path = std::move(path);
    info.access = access;
    return info;
}

/// @brief 装配层替身：枚举快照 + 三条串口钩子的调用留痕（struct-of-回调同族）。
struct Probe {
    std::vector<conn::SerialPortInfo> ports;
    conn::SerialProfile defaults{};
    std::vector<conn::Profile> saved;
    std::vector<std::string> device_lines;
    int cancels = 0;
    int refresh_requests = 0;

    [[nodiscard]] auto hooks() -> ui::ConnectionWizard::Hooks {
        ui::ConnectionWizard::Hooks out;
        out.on_save = [this](const conn::Profile &profile) -> void { saved.push_back(profile); };
        out.on_cancel = [this]() -> void { ++cancels; };
        out.current_ports = [this]() -> std::vector<conn::SerialPortInfo> { return ports; };
        out.refresh_ports = [this]() -> void { ++refresh_requests; };
        out.serial_defaults = [this]() -> conn::SerialProfile { return defaults; };
        return out;
    }
};

struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
};

/// @brief 无头窗口 + 真布局 + 真指针派发的驱动台（itest_keys_panel 同族精简腿）。
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

    [[nodiscard]] auto attach(Probe &probe) -> std::unique_ptr<ui::ConnectionWizard> {
        return std::make_unique<ui::ConnectionWizard>(*host_, probe.hooks());
    }

    auto render() -> void {
        static_cast<void>(window_.present_root(root_));
    }

    /// @brief 排两帧：浮层入树 + 布局落 bounds（单帧只够入树，点位量不到）。
    auto open(ui::ConnectionWizard &wizard) -> void {
        wizard.open_new();
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

    /// @brief 点一处并排两帧（换步/就地重建都要重画才看得到新行集）。
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

    /// @brief 向当前焦点控件发一个真实按键（清端口框走这条：`set_value()` 不触发编辑语义）。
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

    /// @brief 按解析后的显示标签找按钮（`accessibility_label` 与绘制同源）。
    [[nodiscard]] auto find_button(const std::string &label, std::size_t nth = 1) -> HitSpot {
        return find("Button", nth, [&label](au::Widget *widget) -> bool {
            const auto *button = dynamic_cast<const au::Button *>(widget);
            return button != nullptr && button->accessibility_label() == label;
        });
    }

    /// @brief 按显示标签找正文 Text（字段行的标签与校验留痕的可见性判据走这条）。
    [[nodiscard]] auto find_text(const std::string &label) -> HitSpot {
        return find("Text", 1, [&label](au::Widget *widget) -> bool {
            const auto *text = dynamic_cast<const au::Text *>(widget);
            return text != nullptr && text->accessibility_label() == label;
        });
    }

    /// @brief 树里该类型控件的**个数**（按指针去重：一次采样扫到同一控件的多个点只算一枚）。
    [[nodiscard]] auto count_of(std::string_view type_name) -> std::size_t {
        std::vector<au::Widget *> seen;
        for (float y = 4.0F; y < static_cast<float>(kWindowHeight) - 4.0F; y += 4.0F) {
            for (float x = 20.0F; x < static_cast<float>(kWindowWidth) - 20.0F; x += 4.0F) {
                au::Widget *widget = hit(x, y);
                if (widget != nullptr && type_name == widget->type_name() &&
                    std::find(seen.begin(), seen.end(), widget) == seen.end()) {
                    seen.push_back(widget);
                }
            }
        }
        return seen.size();
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

/// @brief 步过类型卡：新建 → 点「串口终端」→ 串口字段集上屏。
auto enter_serial_step(Harness &h, ui::ConnectionWizard &wizard) -> void {
    h.open(wizard);
    h.click_and_settle(h.find_button(ui::settings_label("connections.type.serial")));
}

/// @brief 取设备下拉的选项串（`serialize_props` 是框架公共读面：置灰判据的文案侧证据在这里）。
[[nodiscard]] auto dropdown_options(au::Dropdown &dropdown) -> std::vector<std::string> {
    auto props = au::Json::object();
    dropdown.serialize_props(props);
    const auto *options = props.find("options");
    auto out = std::vector<std::string>{};
    if (options == nullptr) {
        return out;
    }
    for (std::size_t i = 0; i < options->size(); ++i) {
        out.push_back(std::string{options->as_or_at<std::string>(i, {})});
    }
    return out;
}

/// @brief 一条串口档案（编辑腿的入口只有 `open_edit`，故用例自备档案）。
[[nodiscard]] auto serial_profile(std::string name, conn::SerialProfile serial) -> conn::Profile {
    auto profile = conn::Profile{};
    profile.id = "profile:" + name;
    profile.name = std::move(name);
    profile.type = conn::ConnectionType::Serial;
    profile.serial = std::move(serial);
    return profile;
}

/// @brief 一条 Telnet 档案（`SPEC.FEAT.CONN.06` 批 3 的编辑腿入口同样是 `open_edit`）。
[[nodiscard]] auto telnet_profile(std::string name, std::string host, int port) -> conn::Profile {
    auto profile = conn::Profile{};
    profile.id = "profile:" + name;
    profile.name = std::move(name);
    profile.type = conn::ConnectionType::Telnet;
    profile.telnet.host = std::move(host);
    profile.telnet.port = port;
    return profile;
}

/// @brief 第 nth 个 TextInput 的当前值（字段行的产值与回填都从这一处取读数）。
[[nodiscard]] auto text_at(Harness &h, std::size_t nth) -> std::string {
    auto spot = h.find("TextInput", nth);
    AURORA_TEST_REQUIRE_MSG(spot.widget != nullptr, "the input row is missing");
    const auto *input = dynamic_cast<const au::TextInput *>(spot.widget);
    AURORA_TEST_REQUIRE(input != nullptr);
    return input->value();
}

}  // namespace

// ------------------------------------------------------------
// 判据①（稿 §2 标号 1/2）：四张类型卡 + 串口字段集九行，SSH 四栏不出现
// ------------------------------------------------------------
AURORA_TEST_CASE(four_type_cards_land_and_serial_opens_the_nine_rows) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto wizard = h.attach(probe);
    h.open(*wizard);

    // 步骤 0 自此四张（Local / SSH / Serial / Telnet，裁决 7.110 D1① 与 7.112 D1①）：
    // 四张都真点得到——第四张是本棒新增的入口，点不到就是 Telnet 档案仍建不出来。
    for (const auto *key : {"connections.type.local", "connections.type.ssh",
                            "connections.type.serial", "connections.type.telnet"}) {
        AURORA_TEST_REQUIRE_MSG(h.find_button(ui::settings_label(key)).widget != nullptr, key);
    }

    h.click_and_settle(h.find_button(ui::settings_label("connections.type.serial")));

    // 九行标签逐行上屏（展示名 + 八行参数）。
    for (const auto *key : {"connections.field.name", "connections.field.device",
                            "connections.field.baud", "connections.field.data_bits",
                            "connections.field.stop_bits", "connections.field.parity",
                            "connections.field.flow", "connections.field.line_ending",
                            "connections.field.encoding"}) {
        AURORA_TEST_REQUIRE_MSG(h.find_text(ui::settings_label(key)).widget != nullptr, key);
    }
    // SSH 专属四栏不出现（稿 §2 标号 1 的后半句：字段集由类型卡决定）。
    for (const auto *key : {"connections.field.host", "connections.field.port",
                            "connections.field.user", "connections.field.auth",
                            "connections.field.identity"}) {
        AURORA_TEST_CHECK_MSG(h.find_text(ui::settings_label(key)).widget == nullptr, key);
    }

    // 设备行＝「下拉 + 刷新 + 自定义」三件套（D2①），且整屏只有这一枚 Dropdown。
    AURORA_TEST_REQUIRE_MSG(h.find("Dropdown", 1).widget != nullptr, "the device dropdown is missing");
    AURORA_TEST_CHECK_EQ(h.count_of("Dropdown"), 1U);
    AURORA_TEST_CHECK_TRUE(h.find_button(ui::settings_label("connections.action.refresh_devices")).widget !=
                           nullptr);
    AURORA_TEST_CHECK_TRUE(h.find_button(ui::settings_label("connections.action.custom_path")).widget !=
                           nullptr);
    // 其余七行一律循环按钮（同口径先例：`auth_button_`）。
    AURORA_TEST_CHECK_TRUE(h.find_button(std::string{"115200"}).widget != nullptr);
    AURORA_TEST_CHECK_TRUE(h.find_button(ui::settings_label("connections.serial.parity.none")).widget !=
                           nullptr);
    AURORA_TEST_CHECK_TRUE(h.find_button(ui::settings_label("connections.serial.flow.none")).widget !=
                           nullptr);
}

// ------------------------------------------------------------
// 判据②（D4①）：设置域默认值在建草稿那一刻灌进来，保存交出的是档案值而非哨兵
// ------------------------------------------------------------
AURORA_TEST_CASE(new_serial_draft_is_seeded_from_settings_defaults_and_saves_back) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.defaults = conn::SerialProfile{
        .device_path = {},
        .baud = 9600,
        .data_bits = 7,
        .stop_bits = 2,
        .parity = "even",
        .flow = "software",
        .line_ending = "CRLF",
        .encoding = "GB18030",
    };
    Harness h;
    auto wizard = h.attach(probe);
    enter_serial_step(h, *wizard);

    // 灌入的可观测面就是那七枚循环钮的首帧标签（表内值→词条，数据段原样上屏）。
    AURORA_TEST_REQUIRE_MSG(h.find_button(std::string{"9600"}).widget != nullptr, "baud not seeded");
    AURORA_TEST_REQUIRE_MSG(h.find_button(std::string{"7"}).widget != nullptr, "data bits not seeded");
    AURORA_TEST_REQUIRE_MSG(h.find_button(std::string{"2"}).widget != nullptr, "stop bits not seeded");
    AURORA_TEST_CHECK_TRUE(h.find_button(ui::settings_label("connections.serial.parity.even")).widget !=
                           nullptr);
    AURORA_TEST_CHECK_TRUE(h.find_button(ui::settings_label("connections.serial.flow.software")).widget !=
                           nullptr);
    AURORA_TEST_CHECK_TRUE(
        h.find_button(ui::settings_label("connections.serial.line_ending.crlf")).widget != nullptr);
    AURORA_TEST_CHECK_TRUE(h.find_button(std::string{"GB18030"}).widget != nullptr);

    // 设备必填：走「自定义…」手输一档（F1 环境里枚举项全不可开，这一档是唯一的产值路）。
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.custom_path")));
    auto device_input = h.find("TextInput", 2);
    AURORA_TEST_REQUIRE_MSG(device_input.widget != nullptr, "the custom device field is missing");
    h.click(device_input);
    static_cast<void>(h.type("/dev/ttyUSB7"));
    auto name_input = h.find("TextInput", 1);
    AURORA_TEST_REQUIRE_MSG(name_input.widget != nullptr, "the name field is missing");
    h.click(name_input);
    static_cast<void>(h.type("board"));

    h.click_and_settle(h.find_button(ui::settings_label("connections.action.save")));

    AURORA_TEST_REQUIRE_EQ(probe.saved.size(), 1U);
    const auto &saved = probe.saved[0];
    AURORA_TEST_CHECK_TRUE(saved.type == conn::ConnectionType::Serial);
    // 交出的是**具体值**而非「跟随默认」哨兵（D4① 的判据），逐字段与灌入源同值。
    auto expected = probe.defaults;
    expected.device_path = "/dev/ttyUSB7";
    AURORA_TEST_CHECK_TRUE(saved.serial == expected);
    AURORA_TEST_CHECK_TRUE(!wizard->is_open());  // 保存即收框
}

// ------------------------------------------------------------
// 判据③（D8①）：一次打开只发一次枚举；就地重建不重复触发，刷新钮才再发
// ------------------------------------------------------------
AURORA_TEST_CASE(the_device_row_enumerates_once_per_wizard_open) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto wizard = h.attach(probe);

    h.open(*wizard);
    // 停在类型卡时不取设备：枚举只花在用得上的那一格。
    AURORA_TEST_CHECK_EQ(probe.refresh_requests, 0);

    h.click_and_settle(h.find_button(ui::settings_label("connections.type.serial")));
    AURORA_TEST_CHECK_EQ(probe.refresh_requests, 1);

    // 就地重建的入口（自定义⇄列表）不得再发一次后台取数；循环钮同理只改标签。
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.custom_path")));
    AURORA_TEST_CHECK_EQ(probe.refresh_requests, 1);
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.pick_from_list")));
    AURORA_TEST_CHECK_EQ(probe.refresh_requests, 1);
    auto parity_spot = h.find_button(ui::settings_label("connections.serial.parity.none"));
    AURORA_TEST_REQUIRE_MSG(parity_spot.widget != nullptr, "the parity cycle button is missing");
    h.click_and_settle(parity_spot);
    AURORA_TEST_CHECK_EQ(probe.refresh_requests, 1);
    AURORA_TEST_CHECK_TRUE(h.find_button(ui::settings_label("connections.serial.parity.even")).widget !=
                           nullptr);

    // 「刷新」是显式再取：先把发起位清掉，再走同一条腿。
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.refresh_devices")));
    AURORA_TEST_CHECK_EQ(probe.refresh_requests, 2);

    // 关掉再开＝新一次打开：又一次，而不是累积成零。
    wizard->close();
    enter_serial_step(h, *wizard);
    AURORA_TEST_CHECK_EQ(probe.refresh_requests, 3);
}

// ------------------------------------------------------------
// 判据④（D9① + D2① 否决）：不可达项照列带归因，选中被回退且产值不落进档案
// ------------------------------------------------------------
AURORA_TEST_CASE(unreachable_ports_stay_listed_and_refuse_selection) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.ports = {port("/dev/ttyUSB0", conn::SerialPortAccess::Openable),
                   port("/dev/ttyS0", conn::SerialPortAccess::Denied),
                   port("/dev/ttyS1", conn::SerialPortAccess::Busy)};
    Harness h;
    auto wizard = h.attach(probe);
    enter_serial_step(h, *wizard);

    auto box_spot = h.find("Dropdown", 1);
    AURORA_TEST_REQUIRE_MSG(box_spot.widget != nullptr, "the device dropdown is not reachable");
    auto *box = dynamic_cast<au::Dropdown *>(box_spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    // 首行是「请选择设备」占位（产值为空，校验据此判「设备必填」），其后逐行照列。
    AURORA_TEST_CHECK_EQ(box->option_count(), probe.ports.size() + 1U);

    const auto options = dropdown_options(*box);
    AURORA_TEST_REQUIRE_EQ(options.size(), box->option_count());
    const auto denied_label = ui::settings_label(ui::serial_access_key(conn::SerialPortAccess::Denied));
    const auto busy_label = ui::settings_label(ui::serial_access_key(conn::SerialPortAccess::Busy));
    AURORA_TEST_CHECK_TRUE(options[0].find(denied_label) == std::string::npos);
    // 不可达行**在列表里**且带归因（D9①：不玩「看不见就是没有」）。
    AURORA_TEST_CHECK_TRUE(options[1] == std::string{"/dev/ttyUSB0"});
    AURORA_TEST_CHECK_TRUE(options[2].rfind("/dev/ttyS0", 0U) == 0U);
    AURORA_TEST_CHECK_MSG(options[2].find(denied_label) != std::string::npos, "denied row lost its attribution");
    AURORA_TEST_CHECK_TRUE(options[3].rfind("/dev/ttyS1", 0U) == 0U);
    AURORA_TEST_CHECK_MSG(options[3].find(busy_label) != std::string::npos, "busy row lost its attribution");

    // 主框真点击开合（这条是派发腿；选项行的点选经 `select()`——框架的
    // `on_pointer_event` 落到选项上时呼的就是同一枚 `select`）。
    h.click(box_spot);
    h.render();
    AURORA_TEST_CHECK_TRUE(box->is_open());
    h.click(box_spot);
    h.render();
    AURORA_TEST_CHECK_FALSE(box->is_open());

    // 选中不可达行 ⇒ 回退到上一可选位（首行），产值不落进档案。
    box->select(2);
    AURORA_TEST_CHECK_EQ(box->selected_index(), 0);
    box->select(3);
    AURORA_TEST_CHECK_EQ(box->selected_index(), 0);
    // 选中可达行 ⇒ 落值。
    box->select(1);
    AURORA_TEST_CHECK_EQ(box->selected_index(), 1);

    auto name_input = h.find("TextInput", 1);
    AURORA_TEST_REQUIRE_MSG(name_input.widget != nullptr, "the name field is missing");
    h.click(name_input);
    static_cast<void>(h.type("usb0"));
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.save")));

    AURORA_TEST_REQUIRE_EQ(probe.saved.size(), 1U);
    AURORA_TEST_CHECK_TRUE(probe.saved[0].serial.device_path == "/dev/ttyUSB0");
}

// ------------------------------------------------------------
// 判据⑤（D2① 的互斥切换）：设备值只由**当前可见那一行**决定，切档不静默留隐值
// ------------------------------------------------------------
AURORA_TEST_CASE(custom_path_and_list_are_exclusive_and_the_visible_row_owns_the_value) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.ports = {port("/dev/ttyUSB0", conn::SerialPortAccess::Openable)};
    Harness h;
    auto wizard = h.attach(probe);
    enter_serial_step(h, *wizard);

    h.click_and_settle(h.find_button(ui::settings_label("connections.action.custom_path")));
    // 互斥：手输档上屏时下拉**不在树里**（两套产值控件不同时存在）。
    AURORA_TEST_CHECK_EQ(h.count_of("Dropdown"), 0U);
    auto device_input = h.find("TextInput", 2);
    AURORA_TEST_REQUIRE_MSG(device_input.widget != nullptr, "the custom device field is missing");
    h.click(device_input);
    static_cast<void>(h.type("/dev/ttyXRUSB0"));

    auto back_spot = h.find_button(ui::settings_label("connections.action.pick_from_list"));
    AURORA_TEST_REQUIRE_MSG(back_spot.widget != nullptr, "the toggle back to the list is missing");
    h.click_and_settle(back_spot);
    auto box_spot = h.find("Dropdown", 1);
    AURORA_TEST_REQUIRE_MSG(box_spot.widget != nullptr, "the dropdown did not come back");
    auto *box = dynamic_cast<au::Dropdown *>(box_spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    AURORA_TEST_CHECK_EQ(h.count_of("TextInput"), 1U);  // 手输框随切档退场
    // 那份手输名不在枚举里 ⇒ 主框显示占位；此时保存就是「设备必填」，而不是把看不见的那串存进去。
    AURORA_TEST_CHECK_EQ(box->selected_index(), 0);
    auto name_input = h.find("TextInput", 1);
    AURORA_TEST_REQUIRE_MSG(name_input.widget != nullptr, "the name field is missing");
    h.click(name_input);
    static_cast<void>(h.type("xr"));
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.save")));
    AURORA_TEST_CHECK_EQ(probe.saved.size(), 0U);
    AURORA_TEST_CHECK_TRUE(wizard->is_open());
    AURORA_TEST_REQUIRE_MSG(
        h.find_text(ui::settings_label("connections.wizard.issue_device")).widget != nullptr,
        "the empty-device issue did not reach the notice line");

    // 从列表选一行 ⇒ 产值交回该行的 path（两处入口同一个落点，不各存一份）。
    box->select(1);
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.save")));
    AURORA_TEST_REQUIRE_EQ(probe.saved.size(), 1U);
    AURORA_TEST_CHECK_TRUE(probe.saved[0].serial.device_path == "/dev/ttyUSB0");
    AURORA_TEST_CHECK_FALSE(wizard->is_open());
}

// ------------------------------------------------------------
// 判据⑥（稿 §2 标号 4）：串口四档问题码逐档折成一行可读文案，不静默置灰保存钮
// ------------------------------------------------------------
AURORA_TEST_CASE(each_serial_draft_issue_lands_its_own_notice_line) {
    static_cast<void>(ui::install_settings_strings());

    struct Expectation {
        conn::SerialProfile serial;
        std::string_view issue_key;
    };

    Probe probe;
    // 编辑腿里设备行仍是「可见行产值」，故给出档案用的那枚可达节点，让每一例只脏它自己要测的那一档。
    probe.ports = {port("/dev/ttyUSB0", conn::SerialPortAccess::Openable)};
    Harness h;
    auto wizard = h.attach(probe);

    // 四档按 `validate_draft` 的声明次序逐一造境：每例只脏一个字段，其余字段合法。
    auto no_device = conn::SerialProfile{};  // device_path 空（其余字段取模型缺省，全合法）
    auto bad_baud = conn::SerialProfile{};
    bad_baud.device_path = "/dev/ttyUSB0";
    bad_baud.baud = 4800;  // 区间内但不在档位表：判「向导里不可选」，不判档案非法
    auto bad_data_bits = conn::SerialProfile{};
    bad_data_bits.device_path = "/dev/ttyUSB0";
    bad_data_bits.data_bits = 9;
    auto bad_stop_bits = conn::SerialProfile{};
    bad_stop_bits.device_path = "/dev/ttyUSB0";
    bad_stop_bits.stop_bits = 3;
    const std::vector<Expectation> cases{
        Expectation{no_device, "connections.wizard.issue_device"},
        Expectation{bad_baud, "connections.wizard.issue_baud"},
        Expectation{bad_data_bits, "connections.wizard.issue_data_bits"},
        Expectation{bad_stop_bits, "connections.wizard.issue_stop_bits"},
    };

    for (const auto &case_row : cases) {
        auto profile = serial_profile("bad", case_row.serial);
        wizard->open_edit(profile);
        h.render();
        h.render();
        // 编辑直进字段步骤且**不改设备**，所以点保存就是纯校验腿。
        auto save_spot = h.find_button(ui::settings_label("connections.action.save"));
        AURORA_TEST_REQUIRE_MSG(save_spot.widget != nullptr, "the save button is missing");
        h.click_and_settle(save_spot);
        AURORA_TEST_CHECK_EQ(probe.saved.size(), 0U);  // 不关框也不交出档案
        AURORA_TEST_CHECK_TRUE(wizard->is_open());
        AURORA_TEST_REQUIRE_MSG(
            h.find_text(ui::settings_label(case_row.issue_key)).widget != nullptr,
            std::string{case_row.issue_key}.c_str());
        wizard->close();
        h.render();
    }
}

// ------------------------------------------------------------
// 判据⑦（D8① 的止回投）：枚举结果只在「开着的串口设备行」重建下拉
// ------------------------------------------------------------
AURORA_TEST_CASE(port_results_rebuild_only_the_open_serial_device_row) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.ports = {port("/dev/ttyUSB0", conn::SerialPortAccess::Openable)};
    Harness h;
    auto wizard = h.attach(probe);

    // 关着时回投是空操作：worker 什么时候回来不由 UI 决定。
    const std::size_t overlays_when_closed = h.overlay_count();
    wizard->reload_ports();
    AURORA_TEST_CHECK_EQ(h.overlay_count(), overlays_when_closed);

    // 开着但停在类型卡：不重建（重建会把用户正在看的两张卡换掉）。
    h.open(*wizard);
    const std::size_t overlays_at_cards = h.overlay_count();
    probe.ports.push_back(port("/dev/ttyS0", conn::SerialPortAccess::Denied));
    wizard->reload_ports();
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(h.overlay_count(), overlays_at_cards);

    // 串口设备行开着：进那一行读的就是**当时**那份快照（类型卡期间回来的那条也算已到手）。
    h.click_and_settle(h.find_button(ui::settings_label("connections.type.serial")));
    auto *box = dynamic_cast<au::Dropdown *>(h.find("Dropdown", 1).widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    AURORA_TEST_CHECK_EQ(box->option_count(), 3U);
    probe.ports.push_back(port("/dev/ttyUSB1", conn::SerialPortAccess::Openable));
    wizard->reload_ports();
    h.render();
    h.render();
    box = dynamic_cast<au::Dropdown *>(h.find("Dropdown", 1).widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    AURORA_TEST_CHECK_EQ(box->option_count(), 4U);

    // 手输档开着：快照回来不动那一行（换了就丢掉用户正在敲的输入框）。
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.custom_path")));
    const std::size_t overlays_while_typing = h.overlay_count();
    probe.ports.push_back(port("/dev/ttyUSB2", conn::SerialPortAccess::Openable));
    wizard->reload_ports();
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(h.overlay_count(), overlays_while_typing);
    AURORA_TEST_CHECK_EQ(h.count_of("Dropdown"), 0U);
}

// ------------------------------------------------------------
// 判据⑧（`SPEC.FEAT.CONN.06` 批 3，稿 §6 界面腿）：Telnet 字段集只有三行，端口行显示该型缺省
// ------------------------------------------------------------
AURORA_TEST_CASE(telnet_opens_three_rows_only_and_prefills_the_type_default_port) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto wizard = h.attach(probe);
    h.open(*wizard);
    // 停在类型卡时不取设备：Telnet 型既没有设备行也不画串口那三件套。
    AURORA_TEST_CHECK_EQ(probe.refresh_requests, 0);

    h.click_and_settle(h.find_button(ui::settings_label("connections.type.telnet")));

    // 三行标签逐行上屏（展示名 + 主机 + 端口，裁决 7.112 D2①）。
    for (const auto *key : {"connections.field.name", "connections.field.host",
                            "connections.field.port"}) {
        AURORA_TEST_REQUIRE_MSG(h.find_text(ui::settings_label(key)).widget != nullptr, key);
    }
    // 另外三型的行一条都不画（稿 §6：不画 SSH 的 user/认证/私钥，也不画串口九行与本地两行）。
    for (const auto *key : {"connections.field.user", "connections.field.auth",
                            "connections.field.identity", "connections.field.device",
                            "connections.field.baud", "connections.field.data_bits",
                            "connections.field.stop_bits", "connections.field.parity",
                            "connections.field.flow", "connections.field.line_ending",
                            "connections.field.encoding", "connections.field.command",
                            "connections.field.workdir"}) {
        AURORA_TEST_CHECK_MSG(h.find_text(ui::settings_label(key)).widget == nullptr, key);
    }
    AURORA_TEST_CHECK_EQ(h.count_of("TextInput"), 3U);   // 只有这三行的落笔控件
    AURORA_TEST_CHECK_EQ(h.count_of("Dropdown"), 0U);    // 设备下拉不在这一型里
    // 端口行显示**该型**缺省 23，而不是 `ProfileDraft` 结构体带着的 22：留着 22 直接保存
    // 就会连到 SSH 的端口上去，「留空＝23」这条口径得先在界面上看得见。
    AURORA_TEST_CHECK_EQ(text_at(h, 3), std::string{"23"});
    AURORA_TEST_CHECK_EQ(text_at(h, 2), std::string{});
}

// ------------------------------------------------------------
// 判据⑨（稿 §6）：三行产值进档案；端口留空＝23，填了就用填的那个数
// ------------------------------------------------------------
AURORA_TEST_CASE(the_telnet_rows_save_back_a_profile_and_a_blank_port_falls_back_to_23) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto wizard = h.attach(probe);

    auto enter_telnet = [&]() -> void {
        h.open(*wizard);
        h.click_and_settle(h.find_button(ui::settings_label("connections.type.telnet")));
        auto name_spot = h.find("TextInput", 1);
        AURORA_TEST_REQUIRE_MSG(name_spot.widget != nullptr, "the name field is missing");
        h.click(name_spot);
        static_cast<void>(h.type("lab-box"));
        auto host_spot = h.find("TextInput", 2);
        AURORA_TEST_REQUIRE_MSG(host_spot.widget != nullptr, "the host field is missing");
        h.click(host_spot);
        static_cast<void>(h.type("10.0.0.5"));
    };

    // 第一档：端口不动 ⇒ 交回预填的 23。
    enter_telnet();
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.save")));
    AURORA_TEST_REQUIRE_EQ(probe.saved.size(), 1U);
    AURORA_TEST_CHECK_TRUE(probe.saved[0].type == conn::ConnectionType::Telnet);
    AURORA_TEST_CHECK_TRUE(probe.saved[0].telnet.host == "10.0.0.5");
    AURORA_TEST_CHECK_EQ(probe.saved[0].telnet.port, 23);
    AURORA_TEST_CHECK_FALSE(wizard->is_open());  // 保存即收框

    // 第二档：把端口**清空** ⇒ 留空折成该型缺省 23（同 SSH 那型的 22 一条口径）。
    enter_telnet();
    auto port_spot = h.find("TextInput", 3);
    AURORA_TEST_REQUIRE_MSG(port_spot.widget != nullptr, "the port field is missing");
    h.click(port_spot);
    static_cast<void>(h.press(au::KeyCode::End));
    for (int i = 0; i < 2; ++i) {
        static_cast<void>(h.press(au::KeyCode::Backspace));
    }
    AURORA_TEST_CHECK_EQ(text_at(h, 3), std::string{});
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.save")));
    AURORA_TEST_REQUIRE_EQ(probe.saved.size(), 2U);
    AURORA_TEST_CHECK_EQ(probe.saved[1].telnet.port, 23);

    // 第三档：填了就用填的那个数（防「缺省值永远赢」这一枚变异）。先清掉预填的两位再敲，
    // 否则接上去就成了 232323，落在端口区间外——那是校验档，不是这一档要读的东西。
    enter_telnet();
    auto typed_port = h.find("TextInput", 3);
    AURORA_TEST_REQUIRE_MSG(typed_port.widget != nullptr, "the port field is missing");
    h.click(typed_port);
    static_cast<void>(h.press(au::KeyCode::End));
    for (int i = 0; i < 2; ++i) {
        static_cast<void>(h.press(au::KeyCode::Backspace));
    }
    static_cast<void>(h.type("2323"));
    AURORA_TEST_CHECK_EQ(text_at(h, 3), std::string{"2323"});
    h.click_and_settle(h.find_button(ui::settings_label("connections.action.save")));
    AURORA_TEST_REQUIRE_EQ(probe.saved.size(), 3U);
    AURORA_TEST_CHECK_EQ(probe.saved[2].telnet.port, 2323);
}

// ------------------------------------------------------------
// 判据⑩（稿 §6）：Telnet 的三档问题码逐档折成一行可读文案，不关框也不交出档案
// ------------------------------------------------------------
AURORA_TEST_CASE(each_telnet_draft_issue_lands_its_own_notice_line) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    Harness h;
    auto wizard = h.attach(probe);

    // 三档按 `validate_draft` 的声明次序逐一造境（每例只脏一个字段，其余合法）。
    const std::vector<std::pair<conn::Profile, std::string_view>> cases{
        {telnet_profile("", "10.0.0.5", 23), "connections.wizard.issue_empty_name"},
        {telnet_profile("lab", "", 23), "connections.wizard.issue_empty_host"},
        {telnet_profile("lab", "10.0.0.5", 70000), "connections.wizard.issue_port"},
    };
    for (const auto &[profile, issue_key] : cases) {
        wizard->open_edit(profile);
        h.render();
        h.render();
        // 编辑腿的回填：主机与端口两行显示档案里的值（回填错了下面的留痕就成了假读数）。
        AURORA_TEST_CHECK_EQ(text_at(h, 3), std::to_string(profile.telnet.port));
        auto save_spot = h.find_button(ui::settings_label("connections.action.save"));
        AURORA_TEST_REQUIRE_MSG(save_spot.widget != nullptr, "the save button is missing");
        h.click_and_settle(save_spot);
        AURORA_TEST_CHECK_EQ(probe.saved.size(), 0U);
        AURORA_TEST_CHECK_TRUE(wizard->is_open());  // 不关框：用户就地改
        AURORA_TEST_REQUIRE_MSG(
            h.find_text(ui::settings_label(issue_key)).widget != nullptr,
            std::string{issue_key}.c_str());
        wizard->close();
        h.render();
    }
}

}  // namespace borealis::test_cases::itest_connection_wizard
