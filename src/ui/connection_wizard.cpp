// ============================================================
// 连接新建/编辑向导实现（src/ui/connection_wizard.cpp）
// ------------------------------------------------------------
// 步骤 0＝四张类型卡（Local / SSH / Serial / Telnet，`UI_SERIAL.draft.md` D1① 与
// `UI_TELNET.draft.md` D1①/D2①），步骤 1＝
// 该类型的字段集（串口九行见该稿 §2 标号 2：展示名是输入框、设备行是「下拉 + 刷新 +
// 自定义」三件套、其余七行一律循环按钮；Telnet 只有展示名/主机/端口三行，见
// `UI_TELNET.draft.md` §2 判据 1；SSH 在私钥路径之后多一行跳板机——填的是串形原文，
// 坏值不就地画红而是走下面那条留痕行，见 `CONN03_PROXYJUMP.draft.md` D9）。
// 参数与主机密钥策略用循环按钮（行内不用覆盖
// 绘制不占布局的控件，settings_panel 文件头那条形态约束同源；向导里的设备下拉是
// D2① 明裁的行外控件，同 `itest_settings_panel` 的行内下拉先例）。校验失败只在 notice
// 行给文案，不弹二级对话框。
// ============================================================

#include "connection_wizard.h"

#include <array>
#include <optional>
#include <utility>

#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/text.h"

#include "serial_format.h"
#include "settings_i18n.h"
#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

/// @brief 四档认证方式的循环序（模型注释的口径：password / privatekey / agent /
///        keyboard-interactive）。
constexpr std::array<std::string_view, 4> kAuthCycle{"agent", "password", "privatekey",
                                                     "keyboard-interactive"};

/// @brief 主机密钥策略的循环序（AcceptNew 为模型缺省）。
constexpr std::array<conn::KnownHostsPolicy, 4> kPolicyCycle{
    conn::KnownHostsPolicy::AcceptNew, conn::KnownHostsPolicy::Yes, conn::KnownHostsPolicy::No,
    conn::KnownHostsPolicy::Ask};

/// @brief 数据位/停止位/编码的循环档（与设置域 `ValueDomain` 的区间一致：5..8、1..2）。
constexpr std::array<int, 4> kDataBitsCycle{5, 6, 7, 8};
constexpr std::array<int, 2> kStopBitsCycle{1, 2};
constexpr std::array<std::string_view, 2> kEncodingCycle{"GB18030", "UTF-8"};

/// @brief 两型的主机端口缺省（裁决 7.112 D2①：端口留空＝该型缺省，同一条硬编码口径，
///        不为它开设置键——`settings_catalog` 因此零改动，D5①）。
constexpr int kDefaultSshPort = 22;
constexpr int kDefaultTelnetPort = 23;

/// @brief 端口行的读取：空串折成该型缺省；控件不在场（编辑别的型）时交回 nullopt。
///
/// 「留空＝缺省」而不是「留空＝非法」是向导既有的宽容口径：SSH 那型一直如此，Telnet 照同
/// 一条走（`validate_draft` 只在落成的整数值越界时判 `PortOutOfRange`）。
[[nodiscard]] auto port_from_input(const std::shared_ptr<aurora::TextInput> &input,
                                   int blank_default) -> std::optional<int> {
    if (input == nullptr) {
        return std::nullopt;
    }
    const auto text = input->value();
    if (text.empty()) {
        return blank_default;
    }
    return static_cast<int>(std::strtol(text.c_str(), nullptr, 10));  // NOLINT: 界面口径宽容
}

/// @brief 认证方式 → 显示串。
///
/// 落盘与 `conn::kSshAuthMethods` 用的是 `keyboard-interactive`（连字符），而词条 key 走 gen_i18n 的
/// 标识符口径（连字符翻成下划线，见 `assets/i18n/messages_zh.json`），故组合 key 前就地折算这一处。
[[nodiscard]] auto auth_display(const std::string &method) -> std::string {
    std::string key{"connections.auth."};
    key.reserve(key.size() + method.size());
    for (const char c : method) {
        key.push_back(c == '-' ? '_' : c);
    }
    return settings_label(key);
}

/// @brief 三个文本参数的循环按钮标签：措辞唯一出自 `serial_format`（本件不 switch 第二套）。
[[nodiscard]] auto parity_display(const std::string &name) -> std::string {
    return settings_label(serial_parity_key(name));
}

[[nodiscard]] auto flow_display(const std::string &name) -> std::string {
    return settings_label(serial_flow_key(name));
}

[[nodiscard]] auto line_ending_display(const std::string &name) -> std::string {
    return settings_label(serial_line_ending_key(name));
}

[[nodiscard]] auto number_display(int value) -> std::string {
    return std::to_string(value);  // 波特率/数据位/停止位是数据段，原样上屏（AGENTS §4.3 第 14 条）。
}

/// @brief 循环推进：命中当前档取下一档；表外值落回 fallback。
///
/// 表外值走得进来是因为**档位表只约束可选项、不判档案非法**（`serial_model` 的
/// `validate_serial_profile` 只查区间）：手工写进 `borealis.json` 的 4800 在设备上照样能设，
/// 编辑那条档案时循环钮显示原始 4800，点一次即回到表内缺省档。
template <typename T, std::size_t N>
[[nodiscard]] auto next_cycling(const std::array<T, N> &table, const T &current, const T &fallback)
    -> T {
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i] == current) {
            return table[(i + 1) % table.size()];
        }
    }
    return fallback;
}

/// @brief 设备下拉：不可达行置灰（裁决 7.110 D9①「照列 + 置灰 + 归因」）。
///
/// 只改文本色，走的是框架文档化的 `paint_item` 继承扩展点（Aurora 主仓
/// `codespec/widget/dropdown` 那两条 protected 虚函数），不碰渲染路径本体（AGENTS §5 第 3 条）。
/// 点击不可达行的否决不在这里——`select()` 不是虚函数，否决落在 `set_on_change` 的回退上。
class DeviceDropdown final : public aurora::Dropdown {
  public:
    DeviceDropdown(std::vector<std::string> options, std::vector<bool> selectable, int initial,
                   aurora::Color dim_color)
        : aurora::Dropdown(std::move(options), initial), selectable_{std::move(selectable)},
          dim_color_{dim_color} {}

  protected:
    auto paint_item(aurora::Painter &p, std::size_t index, const aurora::Rect &item, bool selected,
                    const aurora::Font &f, aurora::Color accent, aurora::Color text)
        -> void override {
        const bool grayed = index >= selectable_.size() || !selectable_[index];
        aurora::Dropdown::paint_item(p, index, item, selected, f, accent, grayed ? dim_color_ : text);
    }

  private:
    std::vector<bool> selectable_{};
    aurora::Color dim_color_{};
};

[[nodiscard]] auto policy_display(conn::KnownHostsPolicy policy) -> std::string {
    switch (policy) {
        case conn::KnownHostsPolicy::AcceptNew:
            return settings_label("connections.policy.accept_new");
        case conn::KnownHostsPolicy::Yes:
            return settings_label("connections.policy.yes");
        case conn::KnownHostsPolicy::No:
            return settings_label("connections.policy.no");
        case conn::KnownHostsPolicy::Ask:
            return settings_label("connections.policy.ask");
    }
    return {};
}

[[nodiscard]] auto issue_key(conn::DraftIssue issue) -> std::string_view {
    switch (issue) {
        case conn::DraftIssue::EmptyName:
            return "connections.wizard.issue_empty_name";
        case conn::DraftIssue::EmptyHost:
            return "connections.wizard.issue_empty_host";
        case conn::DraftIssue::PortOutOfRange:
            return "connections.wizard.issue_port";
        case conn::DraftIssue::UnknownAuthMethod:
            return "connections.wizard.issue_auth";
        case conn::DraftIssue::BadProxyJump:
            return "connections.wizard.issue_proxy_jump";
        case conn::DraftIssue::EmptyDevice:
            return "connections.wizard.issue_device";
        case conn::DraftIssue::BaudNotInTable:
            return "connections.wizard.issue_baud";
        case conn::DraftIssue::DataBitsOutOfRange:
            return "connections.wizard.issue_data_bits";
        case conn::DraftIssue::StopBitsOutOfRange:
            return "connections.wizard.issue_stop_bits";
        case conn::DraftIssue::None:
            break;
    }
    return "connections.wizard.issue_empty_name";
}

/// @brief 「标签 + 控件」一行的装配（标签走词条表；控件可以是输入框或循环按钮）。
[[nodiscard]] auto make_field_row(const std::string &label, aurora::Node control)
    -> aurora::Node {
    const auto chrome = settings_chrome();
    auto label_text = std::make_shared<aurora::Text>(
        aurora::TextProps{.content = label, .text_color = chrome.text_dim});
    label_text->set_modifier(aurora::Modifier{}.width(96.0F));
    auto row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(label_text))},
            std::move(control),
        },
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    });
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(row))};
}

[[nodiscard]] auto make_input(float width) -> std::shared_ptr<aurora::TextInput> {
    const auto chrome = settings_chrome();
    auto input = std::make_shared<aurora::TextInput>();
    input->set_background(chrome.control_bg);
    input->set_border_color(chrome.card_line);
    input->set_focused_border_color(chrome.accent);
    input->set_text_color(chrome.text);
    input->set_modifier(aurora::Modifier{}.width(width));
    return input;
}

}  // namespace

ConnectionWizard::ConnectionWizard(aurora::OverlayHost &host, Hooks hooks)
    : host_{&host}, hooks_{std::move(hooks)} {}

// 先摘后换（头文件的判据）：`close()` 摘的是记在 `overlay_index_` 里的**旧宿主**序号，换指针之后这一步
// 就变成拿旧序号去摘新增宿主的子节点。换完不重挂——建层只有 `build_and_show()` 一条腿，搬窗不开第二形态。
auto ConnectionWizard::set_overlay_host(aurora::OverlayHost &host) -> void {
    if (host_ == &host) {
        return;
    }
    close();
    host_ = &host;
}

ConnectionWizard::~ConnectionWizard() {
    close();
}

auto ConnectionWizard::open_new() -> void {
    close();
    draft_ = conn::ProfileDraft{};
    editing_id_.clear();
    step_ = 0;
    build_and_show();
}

auto ConnectionWizard::open_edit(const conn::Profile &profile) -> void {
    close();
    draft_ = conn::profile_to_draft(profile);
    editing_id_ = profile.id;
    step_ = 1;  // 编辑直进字段步骤：类型不再改（改类型＝删了重建，语义更清晰）。
    if (profile.type == conn::ConnectionType::Serial) {
        request_port_scan();  // D8①：开设备行取一次，不等用户点刷新。
    }
    build_and_show();
}

auto ConnectionWizard::request_port_scan() -> void {
    if (hooks_.refresh_ports != nullptr) {
        hooks_.refresh_ports();
    }
}

auto ConnectionWizard::reload_ports() -> void {
    // 枚举回来时向导可能已经关了、也可能正停在别的步骤或手输档；四种情况下都没有要重建的下拉。
    if (!open_ || step_ != 1 || custom_device_ || draft_.type != conn::ConnectionType::Serial) {
        return;
    }
    collect_draft();  // 就地重建：先收文本字段，别让重建丢掉未提交的输入。
    build_and_show();
}

auto ConnectionWizard::collect_draft() -> void {
    draft_.name = name_input_ != nullptr ? name_input_->value() : draft_.name;
    if (draft_.type == conn::ConnectionType::Ssh) {
        draft_.host = host_input_ != nullptr ? host_input_->value() : draft_.host;
        draft_.user = user_input_ != nullptr ? user_input_->value() : draft_.user;
        draft_.identity_file = identity_input_ != nullptr ? identity_input_->value()
                                                          : draft_.identity_file;
        draft_.proxy_jumps_text = jump_input_ != nullptr ? jump_input_->value()
                                                         : draft_.proxy_jumps_text;
        if (const auto port = port_from_input(port_input_, kDefaultSshPort)) {
            draft_.port = *port;
        }
    } else if (draft_.type == conn::ConnectionType::Telnet) {
        // Telnet 只有主机与端口两枚产值字段（展示名在公共那行）：没有 user/认证/私钥三栏，
        // 也没有串口那些循环钮（裁决 7.112 D2① 的「字段集三行、不造没有消费方的凭据链」）。
        draft_.host = host_input_ != nullptr ? host_input_->value() : draft_.host;
        if (const auto port = port_from_input(port_input_, kDefaultTelnetPort)) {
            draft_.port = *port;
        }
    } else if (draft_.type == conn::ConnectionType::Serial) {
        // 设备值只从**当前可见的那一行**取（切档时另一份控件已退场）：留着看不见的那串才算说谎，
        // 清空则让「设备必填」在留痕行上当场说给用户听。循环按钮那七档在点击时就已写进草稿，这里不收钮。
        if (custom_device_) {
            if (device_input_ != nullptr) {
                draft_.serial.device_path = device_input_->value();
            }
        } else if (device_dropdown_ != nullptr) {
            const int index = device_dropdown_->selected_index();
            const auto port_index = static_cast<std::size_t>(index) - 1U;
            if (index < 1 || port_index >= ports_.size()) {
                draft_.serial.device_path.clear();  // 首行＝「请选择设备」；越界行号是旧快照的残留
            } else {
                draft_.serial.device_path = ports_[port_index].path;
            }
        }
    } else {
        draft_.command_line = command_input_ != nullptr ? command_input_->value()
                                                        : draft_.command_line;
        draft_.working_directory =
            workdir_input_ != nullptr ? workdir_input_->value() : draft_.working_directory;
    }
}

auto ConnectionWizard::save() -> void {
    collect_draft();
    const auto issue = conn::validate_draft(draft_);
    if (issue != conn::DraftIssue::None) {
        if (notice_ != nullptr) {
            const auto chrome = settings_chrome();
            notice_->set_content(settings_label(issue_key(issue)));
            notice_->text_color = chrome.accent;
            notice_->mark_needs_paint();
        }
        return;  // 不关框：用户就地改。
    }
    const auto id = editing_id_.empty() ? "profile:" + draft_.name : editing_id_;
    auto profile = conn::draft_to_profile(draft_, id);
    close();
    if (hooks_.on_save != nullptr) {
        hooks_.on_save(profile);
    }
}

auto ConnectionWizard::build_content() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};

    auto title = std::make_shared<aurora::Text>(aurora::TextProps{
        .content = settings_label(editing_id_.empty() ? "connections.wizard.title_new"
                                                      : "connections.wizard.title_edit"),
        .text_color = chrome.text,
    });
    title->set_modifier(aurora::Modifier{}.fill_max_width());
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(title))});

    if (step_ == 0) {
        // 步骤 0：四张类型卡（裁决 7.110 D1① 落第三型、7.112 D1① 落第四型）。
        auto make_type_card = [&](conn::ConnectionType type, std::string_view label_key)
            -> aurora::Node {
            auto card = std::make_shared<aurora::Button>(aurora::ButtonProps{
                .label = settings_label(label_key),
                .color = chrome.control_bg,
                .on_color = chrome.text,
                .border_color = chrome.accent,
                .border_width = 1.0F,
                .min_width = 120.0F,
                .min_height = 64.0F,
            });
            card->set_on_click([this, type]() -> void {
                draft_.type = type;
                if (type == conn::ConnectionType::Serial) {
                    // D4①：设置域的串口默认值在「建草稿那一刻」灌进来，档位表仍只有 serial_model
                    // 那一份；串口型不查凭据库（CONN.09 的明文审计天然过）。
                    if (hooks_.serial_defaults != nullptr) {
                        draft_.serial = hooks_.serial_defaults();
                    }
                    request_port_scan();
                } else if (type == conn::ConnectionType::Telnet) {
                    // 端口那行在建草稿那一刻就显示该型缺省 23，而不是草稿结构体带着的 22：
                    // 「留空＝23」这条口径要能在界面上看见，否则用户清空后才拿到 23、留着 22
                    // 就直接连错端口（裁决 7.112 D2①）。
                    draft_.port = kDefaultTelnetPort;
                }
                step_ = 1;
                build_and_show();  // 换步重建：字段集随类型卡变（设计稿标号 5）。
            });
            return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card))};
        };
        auto card_row = std::make_shared<aurora::Row>(aurora::RowProps{
            .children = {make_type_card(conn::ConnectionType::Local, "connections.type.local"),
                         make_type_card(conn::ConnectionType::Ssh, "connections.type.ssh"),
                         make_type_card(conn::ConnectionType::Serial, "connections.type.serial"),
                         make_type_card(conn::ConnectionType::Telnet, "connections.type.telnet")},
            .gap = 12.0F,
        });
        children.push_back(
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(card_row))});
    } else {
        // 步骤 1：字段集。名称四型共用；其余字段按类型卡决定（SSH / Serial / Telnet / Local 四套）。
        name_input_ = make_input(240.0F);
        name_input_->set_value(draft_.name);
        children.push_back(make_field_row(settings_label("connections.field.name"), aurora::Node{std::static_pointer_cast<aurora::Widget>(name_input_)}));

        // 主机与端口两行的形态只有一处：SSH 与 Telnet 共用同一对成员句柄与同一份
        // 「留空＝该型缺省」口径（`collect_draft` 那侧同一条 `port_from_input`）。
        auto add_host_and_port_rows = [&]() -> void {
            host_input_ = make_input(240.0F);
            host_input_->set_value(draft_.host);
            children.push_back(make_field_row(settings_label("connections.field.host"), aurora::Node{std::static_pointer_cast<aurora::Widget>(host_input_)}));

            port_input_ = make_input(240.0F);
            port_input_->set_value(std::to_string(draft_.port));
            children.push_back(make_field_row(settings_label("connections.field.port"), aurora::Node{std::static_pointer_cast<aurora::Widget>(port_input_)}));
        };

        if (draft_.type == conn::ConnectionType::Ssh) {
            add_host_and_port_rows();
            user_input_ = make_input(240.0F);
            user_input_->set_value(draft_.user);
            children.push_back(make_field_row(settings_label("connections.field.user"), aurora::Node{std::static_pointer_cast<aurora::Widget>(user_input_)}));

            // 认证方式：循环按钮（标签即当前档；行内下拉违形态约束，settings_panel 文件头）。
            auth_button_ = std::make_shared<aurora::Button>(aurora::ButtonProps{
                .label = auth_display(draft_.auth_method),
                .color = chrome.control_bg,
                .on_color = chrome.text,
                .border_color = chrome.card_line,
                .border_width = 1.0F,
                .min_width = 160.0F,
            });
            auth_button_->set_on_click([this]() -> void {
                collect_draft();  // 先收文本字段，别让循环按钮丢掉未提交的输入。
                for (std::size_t i = 0; i < kAuthCycle.size(); ++i) {
                    if (kAuthCycle[i] == draft_.auth_method) {
                        draft_.auth_method = std::string{kAuthCycle[(i + 1) % kAuthCycle.size()]};
                        break;
                    }
                }
                auth_button_->set_label(auth_display(draft_.auth_method));  // 换源自带两脏。
            });
            children.push_back(
                make_field_row(settings_label("connections.field.auth"), aurora::Node{std::static_pointer_cast<aurora::Widget>(auth_button_)}));

            identity_input_ = make_input(240.0F);
            identity_input_->set_value(draft_.identity_file);
            children.push_back(
                make_field_row(settings_label("connections.field.identity"), aurora::Node{std::static_pointer_cast<aurora::Widget>(identity_input_)}));

            // 跳板机那一行（`SPEC.FEAT.CONN.03` 稿 D9）：单行输入、串形原文，形态与上面几行同
            // 一件控件（不新增控件类型）。坏值不就地画红而是落到下面那条常驻留痕行——向导没有
            // 「逐行错误槽」这一形态，端口那一行也是同一走法。
            jump_input_ = make_input(240.0F);
            jump_input_->set_value(draft_.proxy_jumps_text);
            jump_input_->set_placeholder(settings_label("connections.proxy_jump.placeholder"));
            children.push_back(
                make_field_row(settings_label("connections.field.proxy_jump"), aurora::Node{std::static_pointer_cast<aurora::Widget>(jump_input_)}));
        } else if (draft_.type == conn::ConnectionType::Serial) {
            // 串口字段集（稿 §2 标号 2）：设备行是「下拉 + 刷新 + 自定义」三件套，其余六行
            // 循环按钮；展示名那行两型共用，已在上面。
            auto add_cycle_row = [&](std::string_view label_key,
                                     std::shared_ptr<aurora::Button> &slot, std::string initial,
                                     std::function<std::string()> advance) -> void {
                slot = std::make_shared<aurora::Button>(aurora::ButtonProps{
                    .label = std::move(initial),
                    .color = chrome.control_bg,
                    .on_color = chrome.text,
                    .border_color = chrome.card_line,
                    .border_width = 1.0F,
                    .min_width = 160.0F,
                });
                auto button = slot;
                button->set_on_click([this, button, advance = std::move(advance)]() -> void {
                    collect_draft();  // 先收文本字段，别让循环按钮丢掉未提交的输入（auth_button_ 同口径）。
                    button->set_label(advance());  // 换源自带两脏。
                });
                children.push_back(make_field_row(
                    settings_label(label_key),
                    aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(button))}));
            };

            ports_ = hooks_.current_ports != nullptr
                         ? hooks_.current_ports()
                         : std::vector<conn::SerialPortInfo>{};
            auto device_controls = std::vector<aurora::Node>{};
            if (custom_device_) {
                device_input_ = make_input(200.0F);
                device_input_->set_value(draft_.serial.device_path);
                device_controls.push_back(aurora::Node{
                    std::static_pointer_cast<aurora::Widget>(device_input_)});
            } else {
                auto options =
                    std::vector<std::string>{settings_label("connections.device.placeholder")};
                auto selectable = std::vector<bool>{true};
                auto initial = 0;
                for (std::size_t i = 0; i < ports_.size(); ++i) {
                    const auto &port = ports_[i];
                    options.push_back(serial_device_line(
                        port, settings_label(serial_access_key(port.access))));
                    selectable.push_back(conn::serial_port_is_selectable(port));
                    if (port.path == draft_.serial.device_path) {
                        initial = static_cast<int>(i) + 1;
                    }
                }
                device_dropdown_ = std::make_shared<DeviceDropdown>(std::move(options),
                                                                    std::move(selectable), initial,
                                                                    chrome.text_dim);
                device_dropdown_->set_box_color(chrome.control_bg);
                device_dropdown_->set_border_color(chrome.card_line);
                device_dropdown_->set_text_color(chrome.text);
                device_dropdown_->set_modifier(aurora::Modifier{}.width(240.0F));
                last_selectable_port_ = initial;
                // 不可达行的否决：基类 `select()` 不是虚函数，回退只能落在变更回调上
                // （`vetoing_port_choice_` 防自召——回退本身又会进一次 on_change）。
                device_dropdown_->set_on_change([this](int index) -> void {
                    if (vetoing_port_choice_) {
                        return;
                    }
                    const auto port_index = static_cast<std::size_t>(index) - 1U;
                    if (index == 0 ||
                        (index >= 1 && port_index < ports_.size() &&
                         conn::serial_port_is_selectable(ports_[port_index]))) {
                        last_selectable_port_ = index;  // 首行可选但不产值（校验判「设备必填」）。
                        return;
                    }
                    vetoing_port_choice_ = true;
                    device_dropdown_->select(last_selectable_port_);
                    vetoing_port_choice_ = false;
                });
                device_controls.push_back(aurora::Node{
                    std::static_pointer_cast<aurora::Widget>(device_dropdown_)});
            }

            auto make_side_button = [&](std::string_view label_key, std::function<void()> run)
                -> aurora::Node {
                auto button = std::make_shared<aurora::Button>(aurora::ButtonProps{
                    .label = settings_label(label_key),
                    .color = chrome.control_bg,
                    .on_color = chrome.text,
                    .border_color = chrome.card_line,
                    .border_width = 1.0F,
                    .min_width = 72.0F,
                });
                button->set_on_click([this, run = std::move(run)]() -> void {
                    collect_draft();  // 就地重建前先把输入框的值收进草稿。
                    run();
                });
                return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(button))};
            };
            device_controls.push_back(make_side_button("connections.action.refresh_devices",
                                                      [this]() -> void { request_port_scan(); }));
            device_controls.push_back(make_side_button(
                custom_device_ ? "connections.action.pick_from_list"
                               : "connections.action.custom_path",
                [this]() -> void {
                    custom_device_ = !custom_device_;
                    build_and_show();  // 互斥切换需一次就地重建（口径同 keys_panel 的推送框）。
                }));
            auto device_row = std::make_shared<aurora::Row>(aurora::RowProps{
                .children = std::move(device_controls),
                .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
                .gap = 6.0F,
            });
            children.push_back(make_field_row(
                settings_label("connections.field.device"),
                aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(device_row))}));

            add_cycle_row("connections.field.baud", baud_button_,
                          number_display(draft_.serial.baud), [this]() -> std::string {
                              draft_.serial.baud = next_cycling(
                                  conn::kSerialBaudRates, draft_.serial.baud,
                                  conn::SerialProfile{}.baud);
                              return number_display(draft_.serial.baud);
                          });
            add_cycle_row("connections.field.data_bits", data_bits_button_,
                          number_display(draft_.serial.data_bits), [this]() -> std::string {
                              draft_.serial.data_bits = next_cycling(kDataBitsCycle,
                                                                     draft_.serial.data_bits, 5);
                              return number_display(draft_.serial.data_bits);
                          });
            add_cycle_row("connections.field.stop_bits", stop_bits_button_,
                          number_display(draft_.serial.stop_bits), [this]() -> std::string {
                              draft_.serial.stop_bits = next_cycling(kStopBitsCycle,
                                                                     draft_.serial.stop_bits, 1);
                              return number_display(draft_.serial.stop_bits);
                          });
            add_cycle_row("connections.field.parity", parity_button_,
                          parity_display(draft_.serial.parity), [this]() -> std::string {
                              draft_.serial.parity = std::string{next_cycling(
                                  conn::kSerialParityNames,
                                  std::string_view{draft_.serial.parity},
                                  conn::kSerialParityNames[0])};
                              return parity_display(draft_.serial.parity);
                          });
            add_cycle_row("connections.field.flow", flow_button_,
                          flow_display(draft_.serial.flow), [this]() -> std::string {
                              draft_.serial.flow = std::string{next_cycling(
                                  conn::kSerialFlowNames, std::string_view{draft_.serial.flow},
                                  conn::kSerialFlowNames[0])};
                              return flow_display(draft_.serial.flow);
                          });
            add_cycle_row("connections.field.line_ending", line_ending_button_,
                          line_ending_display(draft_.serial.line_ending),
                          [this]() -> std::string {
                              draft_.serial.line_ending = std::string{next_cycling(
                                  conn::kSerialLineEndingNames,
                                  std::string_view{draft_.serial.line_ending},
                                  conn::kSerialLineEndingNames[0])};
                              return line_ending_display(draft_.serial.line_ending);
                          });
            add_cycle_row("connections.field.encoding", encoding_button_,
                          draft_.serial.encoding, [this]() -> std::string {
                              // 编码名是数据段（`term::SessionEncoding` 的入参），原样上屏。
                              draft_.serial.encoding = std::string{next_cycling(
                                  kEncodingCycle, std::string_view{draft_.serial.encoding},
                                  kEncodingCycle[0])};
                              return draft_.serial.encoding;
                          });
        } else if (draft_.type == conn::ConnectionType::Telnet) {
            // Telnet 的字段集到此为止：三行＝展示名 + 主机 + 端口（裁决 7.112 D2①）。不画
            // SSH 的 user/认证/私钥三栏（Telnet 无认证协议，登录提示由远端程序给，「存进档案
            // 的口令」没有消费方），也不画串口那七行循环钮。
            add_host_and_port_rows();
        } else {
            command_input_ = make_input(240.0F);
            command_input_->set_value(draft_.command_line);
            children.push_back(
                make_field_row(settings_label("connections.field.command"), aurora::Node{std::static_pointer_cast<aurora::Widget>(command_input_)}));

            workdir_input_ = make_input(240.0F);
            workdir_input_->set_value(draft_.working_directory);
            children.push_back(
                make_field_row(settings_label("connections.field.workdir"), aurora::Node{std::static_pointer_cast<aurora::Widget>(workdir_input_)}));
        }
    }

    // 校验留痕行（常驻：通过时为空，失败时给可读文案）。
    notice_ = std::make_shared<aurora::Text>(aurora::TextProps{.content = {}, .text_color = chrome.accent});
    notice_->set_modifier(aurora::Modifier{}.fill_max_width());
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(notice_)});

    // 按钮行：步骤 0 只有「取消」；步骤 1 是「保存 + 取消」。
    auto cancel_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("connections.action.cancel"),
        .color = chrome.control_bg,
        .on_color = chrome.text,
        .border_color = chrome.card_line,
        .border_width = 1.0F,
        .min_width = 80.0F,
    });
    cancel_btn->set_on_click([this]() -> void {
        close();
        if (hooks_.on_cancel != nullptr) {
            hooks_.on_cancel();
        }
    });

    auto row_children = std::vector<aurora::Node>{
        aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(cancel_btn))}};
    if (step_ == 1) {
        auto save_btn = std::make_shared<aurora::Button>(aurora::ButtonProps{
            .label = settings_label("connections.action.save"),
            .color = chrome.accent,
            .on_color = chrome.window_bg,
            .min_width = 80.0F,
        });
        save_btn->set_on_click([this]() -> void { save(); });
        row_children.push_back(
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(save_btn))});
    }
    auto button_row = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(row_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 12.0F,
    });
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(button_row))});

    auto content = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    content->set_modifier(aurora::Modifier{}
                              .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                              .background(chrome.card_bg, 8.0F)
                              .border(1.0F, chrome.card_line)
                              .clickable([]() -> void {}));
    return content;
}

auto ConnectionWizard::build_and_show() -> void {
    if (dialog_ != nullptr) {
        // 换步/换值重建：先摘旧层再挂新层（序号按新建算，不留旧句柄）。
        if (overlay_index_.has_value()) {
            host_->remove_overlay(*overlay_index_);
            overlay_index_.reset();
        }
        dialog_.reset();
    }
    dialog_ = std::make_shared<aurora::Dialog>(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(build_content())});
    overlay_index_ = host_->add_overlay(
        aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    dialog_->show();
    open_ = true;
}

auto ConnectionWizard::close() -> void {
    if (!open_) {
        return;
    }
    if (dialog_ != nullptr) {
        dialog_->close();
    }
    clear_state();
}

auto ConnectionWizard::clear_state() -> void {
    if (overlay_index_.has_value()) {
        host_->remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    name_input_.reset();
    host_input_.reset();
    port_input_.reset();
    user_input_.reset();
    identity_input_.reset();
    jump_input_.reset();
    command_input_.reset();
    workdir_input_.reset();
    device_input_.reset();
    device_dropdown_.reset();
    auth_button_.reset();
    baud_button_.reset();
    data_bits_button_.reset();
    stop_bits_button_.reset();
    parity_button_.reset();
    flow_button_.reset();
    line_ending_button_.reset();
    encoding_button_.reset();
    notice_.reset();
    ports_.clear();
    custom_device_ = false;
    vetoing_port_choice_ = false;
    last_selectable_port_ = 0;
    step_ = 0;
    open_ = false;
}

}  // namespace borealis::ui
