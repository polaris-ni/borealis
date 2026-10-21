// ============================================================
// 快照模型的 JSON 编解码（src/ui/workspace_snapshot.cpp）
// ------------------------------------------------------------
// 本件是稿 F12 那笔在册代价的执行点：`store.cpp` 的 `*_to_json` 与 `ScopeReader` 全在匿名命名
// 空间里，搬出来给第三个 TU 用会同时动「写盘」与「导出件」两条既有判据面（`utest_config` 的往返
// 等值全打在那两处一致性上），属跨意图改动，本棒不做。于是同一份 `SshProfile` 有两处 JSON 形态，
// 分叉由判据 ⑦ 的等式案钉住：`utest_workspace_snapshot` 把同一份档案分别喂给 `config::Store` 的
// 落盘与本件，比两边 `dump` 出的字节——键名、次序、枚举文本任一漂了就红。
//
// 分层：本件在 UI 层，只向下依赖 `conn`（架构 §2.1），**不** include `config` 侧的头——连私有头
// `schema_names.h` 也不引，那枚头属 config 层，UI 反向取用即违依赖方向不变量。所以枚举落盘名在
// 本文件另列一份：这是刻意的重复，不是漏搬。串口那三张取值表例外，仍引用 `conn/serial_model.h`
// 里那一份（裁决 7.110 D4① 的「档位与合法名只有一处定义」）。
//
// 读侧只做**文档**判据：键齐不齐、类型合不合、数组元素类型、计数与深度上界、序位与数组长度的
// 对位。一棵形状自身合不合法（单子层、长度与子数不符、长度域外、序位不是排列）一律交给
// `PaneTree::restore` 的六道自检（稿 F25），本件不重写第二遍——同理，串口档位不在这里复查，
// 开那一格时由 `conn::validate_serial_profile` 那道唯一入口判。
// ============================================================

#include "borealis/ui/workspace_snapshot.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/core/json.h"

#include "borealis/conn/local_terminal.h"
#include "borealis/conn/profile.h"
#include "borealis/ui/pane_tree.h"

#include "conn/serial_model.h"

namespace borealis::ui {
namespace {

namespace au = aurora;

using au::json::Value;

// ---- 顶层与容器键名 ----
constexpr std::string_view kVersionKey = "schema_version";
constexpr std::string_view kWindowsKey = "windows";
constexpr std::string_view kTabsKey = "tabs";
constexpr std::string_view kSelectedTabKey = "selected_tab";
constexpr std::string_view kKindKey = "kind";
constexpr std::string_view kProfileIdKey = "profile_id";
constexpr std::string_view kTitleKey = "title";
constexpr std::string_view kSshKey = "ssh";
constexpr std::string_view kSerialKey = "serial";
constexpr std::string_view kTelnetKey = "telnet";
constexpr std::string_view kPanesKey = "panes";
constexpr std::string_view kShapeKey = "shape";
constexpr std::string_view kFocusedKey = "focused";

// ---- 档案块键名：与 `store.cpp` 的写侧逐字同名同序（判据 ⑦ 守这一份抄写）----
constexpr std::string_view kHostKey = "host";
constexpr std::string_view kPortKey = "port";
constexpr std::string_view kUserKey = "user";
constexpr std::string_view kAuthMethodKey = "auth_method";
constexpr std::string_view kIdentityFileKey = "identity_file";
constexpr std::string_view kAgentForwardingKey = "agent_forwarding";
constexpr std::string_view kKnownHostsPolicyKey = "known_hosts_policy";
constexpr std::string_view kCredentialKey = "credential";
constexpr std::string_view kVaultIdKey = "vault_id";
constexpr std::string_view kLabelKey = "label";
constexpr std::string_view kDevicePathKey = "device_path";
constexpr std::string_view kBaudKey = "baud";
constexpr std::string_view kDataBitsKey = "data_bits";
constexpr std::string_view kStopBitsKey = "stop_bits";
constexpr std::string_view kParityKey = "parity";
constexpr std::string_view kFlowKey = "flow";
constexpr std::string_view kLineEndingKey = "line_ending";
constexpr std::string_view kEncodingKey = "encoding";
constexpr std::string_view kCommandLineKey = "command_line";
constexpr std::string_view kWorkingDirectoryKey = "working_directory";
constexpr std::string_view kEnvironmentKey = "environment";

// ---- 形状键名 ----
constexpr std::string_view kLeafKey = "leaf";
constexpr std::string_view kOrderKey = "order";
constexpr std::string_view kAxisKey = "axis";
constexpr std::string_view kLengthsKey = "lengths";
constexpr std::string_view kChildrenKey = "children";

/// @brief 一个枚举值与它的落盘文本名（形态照 `store.cpp` 的 `EnumName`，两处文本同名由判据 ⑦ 守）。
struct EnumText {
    std::string_view name;
    std::int64_t value;
};

constexpr std::array<EnumText, 4> kConnectionTypeNames{
    {EnumText{"local", static_cast<std::int64_t>(conn::ConnectionType::Local)},
     EnumText{kSshKey, static_cast<std::int64_t>(conn::ConnectionType::Ssh)},
     EnumText{kSerialKey, static_cast<std::int64_t>(conn::ConnectionType::Serial)},
     EnumText{kTelnetKey, static_cast<std::int64_t>(conn::ConnectionType::Telnet)}}};

constexpr std::array<EnumText, 4> kKnownHostsPolicyNames{
    {EnumText{"accept-new", static_cast<std::int64_t>(conn::KnownHostsPolicy::AcceptNew)},
     EnumText{"yes", static_cast<std::int64_t>(conn::KnownHostsPolicy::Yes)},
     EnumText{"no", static_cast<std::int64_t>(conn::KnownHostsPolicy::No)},
     EnumText{"ask", static_cast<std::int64_t>(conn::KnownHostsPolicy::Ask)}}};

constexpr std::array<EnumText, 2> kSecretKindNames{
    {EnumText{"reference", static_cast<std::int64_t>(conn::SecretKind::Reference)},
     EnumText{"ask", static_cast<std::int64_t>(conn::SecretKind::AskEveryTime)}}};

constexpr std::array<EnumText, 2> kPaneAxisNames{
    {EnumText{"horizontal", static_cast<std::int64_t>(PaneAxis::Horizontal)},
     EnumText{"vertical", static_cast<std::int64_t>(PaneAxis::Vertical)}}};

/// @brief SSH 认证方式的合法名：与 `store.cpp` 的 `kSshAuthMethods` 同四个名（它是自由文本字段，
///        故落盘名不带枚举，只能靠这张名单守域）。
constexpr std::array<std::string_view, 4> kSshAuthMethods{
    {"password", "privatekey", "agent", "keyboard-interactive"}};

/// @brief 按枚举值取名；表外（本仓新增档位）回空串，该键于是**不写**——照 `store.cpp` 同一条口径。
[[nodiscard]] auto name_of(std::span<const EnumText> names, std::int64_t value) -> std::string_view {
    for (const auto &entry : names) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return {};
}

/// @brief 按名取值；表外回空值，读侧据此把整份文档拒掉（fail-closed，不回落缺省）。
[[nodiscard]] auto value_of(std::span<const EnumText> names, std::string_view name)
    -> std::optional<std::int64_t> {
    for (const auto &entry : names) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

template <typename T>
auto put(Value &node, std::string_view key, T value) -> void {
    node.set(key, Value(std::move(value)));
}

auto put_enum(Value &node, std::string_view key, std::span<const EnumText> names, std::int64_t value) -> void {
    const auto name = name_of(names, value);
    if (!name.empty()) {
        node.set(key, Value(name));
    }
}

[[nodiscard]] auto credential_to_value(const conn::SecretHandle &secret) -> Value {
    auto node = Value::object();
    put_enum(node, kKindKey, kSecretKindNames, static_cast<std::int64_t>(secret.kind));
    put(node, kVaultIdKey, secret.vault_id);
    put(node, kProfileIdKey, secret.profile_id);
    put(node, kLabelKey, secret.label);
    return node;
}

[[nodiscard]] auto ssh_to_value(const conn::SshProfile &ssh) -> Value {
    auto node = Value::object();
    put(node, kHostKey, ssh.host);
    put(node, kPortKey, static_cast<std::int64_t>(ssh.port));
    put(node, kUserKey, ssh.user);
    put(node, kAuthMethodKey, ssh.auth_method);
    put(node, kIdentityFileKey, ssh.identity_file);
    put(node, kAgentForwardingKey, ssh.agent_forwarding);
    put_enum(node, kKnownHostsPolicyKey, kKnownHostsPolicyNames,
             static_cast<std::int64_t>(ssh.known_hosts_policy));
    node.set(kCredentialKey, credential_to_value(ssh.credential));
    return node;
}

[[nodiscard]] auto serial_to_value(const conn::SerialProfile &serial) -> Value {
    auto node = Value::object();
    put(node, kDevicePathKey, serial.device_path);
    put(node, kBaudKey, serial.baud);
    put(node, kDataBitsKey, serial.data_bits);
    put(node, kStopBitsKey, serial.stop_bits);
    put(node, kParityKey, serial.parity);
    put(node, kFlowKey, serial.flow);
    put(node, kLineEndingKey, serial.line_ending);
    put(node, kEncodingKey, serial.encoding);
    return node;
}

[[nodiscard]] auto telnet_to_value(const conn::TelnetProfile &telnet) -> Value {
    auto node = Value::object();
    put(node, kHostKey, telnet.host);
    put(node, kPortKey, telnet.port);
    return node;
}

/// @brief 写一格的本地有效料：键名与次序照 `store.cpp` 的 `local_profile_to_json`（同一份三件，
///        判据 ⑦ 连这一块一起比）。
[[nodiscard]] auto local_to_value(const conn::LocalTerminalSpec &spec) -> Value {
    auto node = Value::object();
    put(node, kCommandLineKey, spec.command_line);
    put(node, kWorkingDirectoryKey, spec.working_directory);
    auto env = Value::object();
    for (const auto &[key, value] : spec.environment) {
        env.set(key, Value(value));
    }
    node.set(kEnvironmentKey, std::move(env));
    return node;
}

[[nodiscard]] auto shape_to_value(const PaneShape &shape) -> Value {
    auto node = Value::object();
    put(node, kLeafKey, shape.leaf);
    if (shape.leaf) {
        // 叶子只记序位：`PaneId` 是本进程发的号，跨进程存它必漂（稿 F23）。
        put(node, kOrderKey, static_cast<std::int64_t>(shape.order));
        return node;
    }
    put_enum(node, kAxisKey, kPaneAxisNames, static_cast<std::int64_t>(shape.axis));
    auto lengths = Value::array();
    for (const double length : shape.lengths) {
        lengths.push_back(Value(length));
    }
    node.set(kLengthsKey, std::move(lengths));
    auto children = Value::array();
    for (const auto &child : shape.children) {
        children.push_back(shape_to_value(child));
    }
    node.set(kChildrenKey, std::move(children));
    return node;
}

[[nodiscard]] auto tab_to_value(const SnapshotTab &tab) -> Value {
    auto node = Value::object();
    put_enum(node, kKindKey, kConnectionTypeNames, static_cast<std::int64_t>(tab.kind));
    put(node, kProfileIdKey, tab.profile_id);
    put(node, kTitleKey, tab.title);
    // 只写与 kind 匹配的那一块（`profiles_to_json` 同一条口径）：三块并写就是给读侧造出
    // 「哪一块算数」的第二道裁决。
    if (tab.ssh) {
        node.set(kSshKey, ssh_to_value(*tab.ssh));
    } else if (tab.serial) {
        node.set(kSerialKey, serial_to_value(*tab.serial));
    } else if (tab.telnet) {
        node.set(kTelnetKey, telnet_to_value(*tab.telnet));
    }
    auto panes = Value::array();
    for (const auto &spec : tab.panes) {
        panes.push_back(local_to_value(spec));
    }
    node.set(kPanesKey, std::move(panes));
    node.set(kShapeKey, shape_to_value(tab.shape));
    put(node, kFocusedKey, static_cast<std::int64_t>(tab.focused_order));
    return node;
}

[[nodiscard]] auto window_to_value(const SnapshotWindow &window) -> Value {
    auto node = Value::object();
    auto tabs = Value::array();
    for (const auto &tab : window.tabs) {
        tabs.push_back(tab_to_value(tab));
    }
    node.set(kTabsKey, std::move(tabs));
    put(node, kSelectedTabKey, static_cast<std::int64_t>(window.selected_tab));
    return node;
}

[[nodiscard]] auto snapshot_to_value(const WorkspaceSnapshot &snapshot) -> Value {
    auto node = Value::object();
    put(node, kVersionKey, kWorkspaceSnapshotSchemaVersion);
    auto windows = Value::array();
    for (const auto &window : snapshot.windows) {
        windows.push_back(window_to_value(window));
    }
    node.set(kWindowsKey, std::move(windows));
    return node;
}

// ============================================================
// 读侧：严格解析，任一判据不合即整份拒绝
// ============================================================

/// @brief 拒因留痕：只记第一条。
///
/// 结论与「坏在哪一格」无关（整份拒绝本来就是 fail-closed），但日志得说清是哪一格，
/// 否则 D8① 那句 `AURORA_LOG_WARN` 只能写成「布局文件坏了」。
class Reject {
  public:
    /// @brief 记下这一处并返回 false，供判定现场写成 `return reject.at(path)`。
    /// @param path 点号／下标形态的文档路径。
    auto at(std::string_view path) -> bool {
        if (reason_.empty()) {
            reason_ = std::string{"workspace snapshot rejected at "} + std::string{path};
        }
        return false;
    }

    /// @brief 已记下的拒因；未记过时为空。
    [[nodiscard]] auto reason() const noexcept -> const std::string & { return reason_; }

  private:
    std::string reason_;
};

[[nodiscard]] auto qualified(std::string_view path, std::string_view key) -> std::string {
    return path.empty() ? std::string{key} : std::string{path} + "." + std::string{key};
}

[[nodiscard]] auto indexed(std::string_view path, std::size_t index) -> std::string {
    return std::string{path} + "[" + std::to_string(index) + "]";
}

/// @brief 取一个必需子键：键缺失即拒（写侧全字段无条件写，读侧因此没有「缺省」这一档）。
[[nodiscard]] auto required(const Value &node, std::string_view key, std::string_view path, Reject &reject)
    -> const Value * {
    const Value *held = node.at(key);
    if (held == nullptr) {
        reject.at(path);
        return nullptr;
    }
    return held;
}

[[nodiscard]] auto read_text(const Value &node, std::string_view key, std::string_view path, Reject &reject,
                             std::string &out) -> bool {
    const Value *held = required(node, key, path, reject);
    const auto text = held == nullptr ? std::nullopt : held->as_string();
    if (!text) {
        return reject.at(path);
    }
    out = std::string{*text};
    return true;
}

[[nodiscard]] auto read_bool(const Value &node, std::string_view key, std::string_view path, Reject &reject,
                             bool &out) -> bool {
    const Value *held = required(node, key, path, reject);
    const auto flag = held == nullptr ? std::nullopt : held->as_bool();
    if (!flag) {
        return reject.at(path);
    }
    out = *flag;
    return true;
}

/// @brief 取一个整数成员并折进 `int`：类型不符或超 `int` 可表示域都算文档不合。
///
/// 只判**可表示性**，不判取值域——串口档位、端口范围的裁决各有唯一来源（`conn::serial_model.h`
/// 与平台腿），在这里复查就是第二真值源；恢复出的那一格连不上由会话侧报，而不是让整份布局消失。
[[nodiscard]] auto read_int(const Value &node, std::string_view key, std::string_view path, Reject &reject,
                            int &out) -> bool {
    const Value *held = required(node, key, path, reject);
    const auto value = held == nullptr ? std::nullopt : held->as_int();
    if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) {
        return reject.at(path);
    }
    out = static_cast<int>(*value);
    return true;
}

/// @brief 取一个「计数／序位」成员：非负整数的文档判据。
[[nodiscard]] auto read_count(const Value &node, std::string_view key, std::string_view path, Reject &reject,
                              std::size_t &out) -> bool {
    const Value *held = required(node, key, path, reject);
    const auto value = held == nullptr ? std::nullopt : held->as_int();
    if (!value || *value < 0 ||
        static_cast<std::uint64_t>(*value) > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return reject.at(path);
    }
    out = static_cast<std::size_t>(*value);
    return true;
}

[[nodiscard]] auto read_enum(const Value &node, std::string_view key, std::string_view path, Reject &reject,
                             std::span<const EnumText> names, std::int64_t &out) -> bool {
    const Value *held = required(node, key, path, reject);
    const auto text = held == nullptr ? std::nullopt : held->as_string();
    const auto value = text ? value_of(names, *text) : std::nullopt;
    if (!value) {
        return reject.at(path);
    }
    out = *value;
    return true;
}

/// @brief 读枚举并折进目标枚举量：域外文本即文档不合。
template <typename E>
[[nodiscard]] auto read_enum_as(const Value &node, std::string_view key, std::string_view path, Reject &reject,
                                std::span<const EnumText> names, E &out) -> bool {
    std::int64_t value = 0;
    if (!read_enum(node, key, path, reject, names, value)) {
        return false;
    }
    out = static_cast<E>(value);
    return true;
}

/// @brief 进入一个必需的子对象。
[[nodiscard]] auto read_object(const Value &node, std::string_view key, std::string_view path, Reject &reject)
    -> const Value * {
    const Value *held = required(node, key, path, reject);
    if (held == nullptr || !held->is_object()) {
        reject.at(path);
        return nullptr;
    }
    return held;
}

/// @brief 进入一个必需的数组，并当场判它的长度上界（稿 D9①：超界整份拒绝而不是截断）。
[[nodiscard]] auto read_array(const Value &node, std::string_view key, std::string_view path, Reject &reject,
                              std::size_t max_size) -> const Value * {
    const Value *held = required(node, key, path, reject);
    if (held == nullptr || !held->is_array() || held->size() > max_size) {
        reject.at(path);
        return nullptr;
    }
    return held;
}

[[nodiscard]] auto read_credential(const Value &node, std::string_view path, Reject &reject,
                                   conn::SecretHandle &out) -> bool {
    auto secret = conn::SecretHandle{};
    if (!read_enum_as(node, kKindKey, qualified(path, kKindKey), reject, kSecretKindNames, secret.kind)) {
        return false;
    }
    if (!read_text(node, kVaultIdKey, qualified(path, kVaultIdKey), reject, secret.vault_id) ||
        !read_text(node, kProfileIdKey, qualified(path, kProfileIdKey), reject, secret.profile_id) ||
        !read_text(node, kLabelKey, qualified(path, kLabelKey), reject, secret.label)) {
        return false;
    }
    out = secret;
    return true;
}

[[nodiscard]] auto read_ssh(const Value &node, std::string_view path, Reject &reject, conn::SshProfile &out)
    -> bool {
    auto ssh = conn::SshProfile{};
    if (!read_text(node, kHostKey, qualified(path, kHostKey), reject, ssh.host)) {
        return false;
    }
    if (!read_int(node, kPortKey, qualified(path, kPortKey), reject, ssh.port)) {
        return false;
    }
    if (!read_text(node, kUserKey, qualified(path, kUserKey), reject, ssh.user)) {
        return false;
    }
    if (!read_text(node, kAuthMethodKey, qualified(path, kAuthMethodKey), reject, ssh.auth_method)) {
        return false;
    }
    // `auth_method` 是自由文本字段，合法名只有这张表（同 `store.cpp` 的 `kSshAuthMethods`）。
    // 这里取**整份拒绝**而非「留痕 + 保留初值」：外部文档里没有「半个坏值」这一档。
    if (std::ranges::find(kSshAuthMethods, ssh.auth_method) == kSshAuthMethods.end()) {
        return reject.at(qualified(path, kAuthMethodKey));
    }
    if (!read_text(node, kIdentityFileKey, qualified(path, kIdentityFileKey), reject, ssh.identity_file)) {
        return false;
    }
    if (!read_bool(node, kAgentForwardingKey, qualified(path, kAgentForwardingKey), reject,
                   ssh.agent_forwarding)) {
        return false;
    }
    if (!read_enum_as(node, kKnownHostsPolicyKey, qualified(path, kKnownHostsPolicyKey), reject,
                      kKnownHostsPolicyNames, ssh.known_hosts_policy)) {
        return false;
    }
    const Value *credential = read_object(node, kCredentialKey, qualified(path, kCredentialKey), reject);
    if (credential == nullptr || !read_credential(*credential, qualified(path, kCredentialKey), reject,
                                                  ssh.credential)) {
        return false;
    }
    out = ssh;
    return true;
}

[[nodiscard]] auto read_serial(const Value &node, std::string_view path, Reject &reject,
                               conn::SerialProfile &out) -> bool {
    auto serial = conn::SerialProfile{};
    if (!read_text(node, kDevicePathKey, qualified(path, kDevicePathKey), reject, serial.device_path)) {
        return false;
    }
    if (!read_int(node, kBaudKey, qualified(path, kBaudKey), reject, serial.baud)) {
        return false;
    }
    if (!read_int(node, kDataBitsKey, qualified(path, kDataBitsKey), reject, serial.data_bits)) {
        return false;
    }
    if (!read_int(node, kStopBitsKey, qualified(path, kStopBitsKey), reject, serial.stop_bits)) {
        return false;
    }
    // 三张取值表引用 `conn/serial_model.h` 里的那一份（裁决 7.110 D4①），本件不复制档位名。
    const auto named = [&](std::string_view key, std::string &field,
                           std::span<const std::string_view> allowed) -> bool {
        if (!read_text(node, key, qualified(path, key), reject, field)) {
            return false;
        }
        return std::ranges::find(allowed, field) != allowed.end() || reject.at(qualified(path, key));
    };
    if (!named(kParityKey, serial.parity, conn::kSerialParityNames) ||
        !named(kFlowKey, serial.flow, conn::kSerialFlowNames) ||
        !named(kLineEndingKey, serial.line_ending, conn::kSerialLineEndingNames)) {
        return false;
    }
    if (!read_text(node, kEncodingKey, qualified(path, kEncodingKey), reject, serial.encoding)) {
        return false;
    }
    out = serial;
    return true;
}

[[nodiscard]] auto read_telnet(const Value &node, std::string_view path, Reject &reject,
                               conn::TelnetProfile &out) -> bool {
    auto telnet = conn::TelnetProfile{};
    if (!read_text(node, kHostKey, qualified(path, kHostKey), reject, telnet.host)) {
        return false;
    }
    if (!read_int(node, kPortKey, qualified(path, kPortKey), reject, telnet.port)) {
        return false;
    }
    out = telnet;
    return true;
}

[[nodiscard]] auto read_local(const Value &node, std::string_view path, Reject &reject,
                              conn::LocalTerminalSpec &out) -> bool {
    auto spec = conn::LocalTerminalSpec{};
    if (!read_text(node, kCommandLineKey, qualified(path, kCommandLineKey), reject, spec.command_line)) {
        return false;
    }
    if (!read_text(node, kWorkingDirectoryKey, qualified(path, kWorkingDirectoryKey), reject,
                   spec.working_directory)) {
        return false;
    }
    const Value *environment = read_object(node, kEnvironmentKey, qualified(path, kEnvironmentKey), reject);
    if (environment == nullptr) {
        return false;
    }
    for (const auto &entry : environment->entries()) {
        const auto value = entry.value.as_string();
        if (!value) {
            return reject.at(qualified(qualified(path, kEnvironmentKey), entry.key));
        }
        spec.environment.emplace(std::string{entry.key}, std::string{*value});
    }
    out = spec;
    return true;
}

/// @brief 递归折一棵形状，并顺手数叶子与节点（两道上界都只在这一条腿上判）。
///
/// 刻意**不**判「子数≥2、长度与子数相符、长度为正、序位是排列」——那六道是 `PaneTree::restore`
/// 的自检清单（稿 F25），本件判了就是把它抄第二遍，而且抄得比原件松。这里的职责是让折出来的
/// 结构在数值上可数、在深度上可控。
[[nodiscard]] auto read_shape(const Value &node, std::string_view path, Reject &reject, PaneShape &out,
                             std::size_t &leaves, std::size_t &nodes, std::size_t depth) -> bool {
    if (depth > kMaxSnapshotShapeDepth) {
        return reject.at(qualified(path, kChildrenKey));
    }
    ++nodes;
    if (nodes > kMaxSnapshotShapeNodes) {
        return reject.at(path);
    }
    auto shape = PaneShape{};
    bool leaf = false;
    if (!read_bool(node, kLeafKey, qualified(path, kLeafKey), reject, leaf)) {
        return false;
    }
    shape.leaf = leaf;
    if (leaf) {
        if (!read_count(node, kOrderKey, qualified(path, kOrderKey), reject, shape.order)) {
            return false;
        }
        ++leaves;
        out = std::move(shape);
        return true;
    }
    if (!read_enum_as(node, kAxisKey, qualified(path, kAxisKey), reject, kPaneAxisNames, shape.axis)) {
        return false;
    }
    const Value *lengths = read_array(node, kLengthsKey, qualified(path, kLengthsKey), reject,
                                      kMaxSnapshotPanesPerTab);
    if (lengths == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < lengths->size(); ++index) {
        const Value *item = lengths->at(index);
        const auto length = item == nullptr ? std::nullopt : item->as_double();
        if (!length) {
            return reject.at(qualified(path, kLengthsKey));  // 元素类型不符：整份拒绝
        }
        shape.lengths.push_back(*length);
    }
    const Value *children = read_array(node, kChildrenKey, qualified(path, kChildrenKey), reject,
                                       kMaxSnapshotPanesPerTab);
    if (children == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < children->size(); ++index) {
        const Value *item = children->at(index);
        if (item == nullptr || !item->is_object()) {
            return reject.at(indexed(qualified(path, kChildrenKey), index));
        }
        auto child = PaneShape{};
        if (!read_shape(*item, indexed(qualified(path, kChildrenKey), index), reject, child, leaves, nodes,
                        depth + 1)) {
            return false;
        }
        shape.children.push_back(std::move(child));
    }
    out = std::move(shape);
    return true;
}

[[nodiscard]] auto read_tab(const Value &node, std::string_view path, Reject &reject, SnapshotTab &out)
    -> bool {
    auto tab = SnapshotTab{};
    if (!read_enum_as(node, kKindKey, qualified(path, kKindKey), reject, kConnectionTypeNames, tab.kind)) {
        return false;
    }
    if (!read_text(node, kProfileIdKey, qualified(path, kProfileIdKey), reject, tab.profile_id)) {
        return false;
    }
    if (!read_text(node, kTitleKey, qualified(path, kTitleKey), reject, tab.title)) {
        return false;
    }
    // 本地腿的料是 per-pane 的，另三腿的料是 per-tab 的：两块各按 kind 取自己那一块。
    const Value *panes = read_array(node, kPanesKey, qualified(path, kPanesKey), reject,
                                    kMaxSnapshotPanesPerTab);
    if (panes == nullptr) {
        return false;
    }
    if (tab.kind == conn::ConnectionType::Local) {
        for (std::size_t index = 0; index < panes->size(); ++index) {
            const Value *item = panes->at(index);
            if (item == nullptr || !item->is_object()) {
                return reject.at(indexed(qualified(path, kPanesKey), index));
            }
            auto spec = conn::LocalTerminalSpec{};
            if (!read_local(*item, indexed(qualified(path, kPanesKey), index), reject, spec)) {
                return false;
            }
            tab.panes.push_back(std::move(spec));
        }
    } else if (panes->size() != 0U) {
        // 非本地标签带 per-pane 料是「写侧漏了 kind 分流」的形态，读回来只会有一张说不清的表。
        return reject.at(qualified(path, kPanesKey));
    }
    if (tab.kind == conn::ConnectionType::Ssh) {
        auto block = conn::SshProfile{};
        const Value *ssh = read_object(node, kSshKey, qualified(path, kSshKey), reject);
        if (ssh == nullptr || !read_ssh(*ssh, qualified(path, kSshKey), reject, block)) {
            return false;
        }
        tab.ssh = block;
    } else if (tab.kind == conn::ConnectionType::Serial) {
        auto block = conn::SerialProfile{};
        const Value *serial = read_object(node, kSerialKey, qualified(path, kSerialKey), reject);
        if (serial == nullptr || !read_serial(*serial, qualified(path, kSerialKey), reject, block)) {
            return false;
        }
        tab.serial = block;
    } else if (tab.kind == conn::ConnectionType::Telnet) {
        auto block = conn::TelnetProfile{};
        const Value *telnet = read_object(node, kTelnetKey, qualified(path, kTelnetKey), reject);
        if (telnet == nullptr || !read_telnet(*telnet, qualified(path, kTelnetKey), reject, block)) {
            return false;
        }
        tab.telnet = block;
    }
    const Value *shape = read_object(node, kShapeKey, qualified(path, kShapeKey), reject);
    if (shape == nullptr) {
        return false;
    }
    std::size_t leaves = 0;
    std::size_t nodes = 0;
    if (!read_shape(*shape, qualified(path, kShapeKey), reject, tab.shape, leaves, nodes, 0)) {
        return false;
    }
    // 两道**对位**判据：本地料按序位同长、选中格落在现存叶子里。二者都是文档级的一致性，
    // 与「这棵树建不建得起来」那六道无关。
    if (tab.kind == conn::ConnectionType::Local && tab.panes.size() != leaves) {
        return reject.at(qualified(path, kPanesKey));
    }
    if (leaves == 0) {
        return reject.at(qualified(path, kShapeKey));
    }
    if (!read_count(node, kFocusedKey, qualified(path, kFocusedKey), reject, tab.focused_order)) {
        return false;
    }
    if (tab.focused_order >= leaves) {
        return reject.at(qualified(path, kFocusedKey));
    }
    out = std::move(tab);
    return true;
}

[[nodiscard]] auto read_window(const Value &node, std::string_view path, Reject &reject,
                               SnapshotWindow &out) -> bool {
    auto window = SnapshotWindow{};
    const Value *tabs = read_array(node, kTabsKey, qualified(path, kTabsKey), reject, kMaxSnapshotTabsPerWindow);
    if (tabs == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < tabs->size(); ++index) {
        const Value *item = tabs->at(index);
        if (item == nullptr || !item->is_object()) {
            return reject.at(indexed(qualified(path, kTabsKey), index));
        }
        auto tab = SnapshotTab{};
        if (!read_tab(*item, indexed(qualified(path, kTabsKey), index), reject, tab)) {
            return false;
        }
        window.tabs.push_back(std::move(tab));
    }
    if (!read_count(node, kSelectedTabKey, qualified(path, kSelectedTabKey), reject, window.selected_tab)) {
        return false;
    }
    // 空窗（零标签）在写侧不存在：`selected_tab` 无处可取。手改出来的这种文档整份拒。
    if (window.selected_tab >= window.tabs.size()) {
        return reject.at(qualified(path, kSelectedTabKey));
    }
    out = std::move(window);
    return true;
}

[[nodiscard]] auto read_snapshot(const Value &node, Reject &reject, WorkspaceSnapshot &out) -> bool {
    auto snapshot = WorkspaceSnapshot{};
    const Value *version = required(node, kVersionKey, kVersionKey, reject);
    const auto stored = version == nullptr ? std::nullopt : version->as_int();
    // 高于本仓支持 = 未知形态（判据 ②）；低于 1 = 本件从没写过这种文档。两头都整份拒。
    if (!stored || *stored > kWorkspaceSnapshotSchemaVersion || *stored < 1) {
        return reject.at(kVersionKey);
    }
    const Value *windows = read_array(node, kWindowsKey, kWindowsKey, reject, kMaxSnapshotWindows);
    if (windows == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < windows->size(); ++index) {
        const Value *item = windows->at(index);
        if (item == nullptr || !item->is_object()) {
            return reject.at(indexed(kWindowsKey, index));
        }
        auto window = SnapshotWindow{};
        if (!read_window(*item, indexed(kWindowsKey, index), reject, window)) {
            return false;
        }
        snapshot.windows.push_back(std::move(window));
    }
    out = std::move(snapshot);
    return true;
}

}  // namespace

auto WorkspaceSnapshot::to_json() const -> std::optional<std::string> {
    // 紧凑文本：这份文档的读者是自家程序与「实在没法启动时手改一遍」的人，缩进交给落盘那侧
    // （框架 `flush()` 自排版），本件不猜它的排版。
    const auto dumped = au::json::dump(snapshot_to_value(*this));
    if (!dumped.ok()) {
        return std::nullopt;
    }
    return std::string{dumped.value()};
}

auto WorkspaceSnapshot::from_json(std::string_view text) -> SnapshotDecodeResult {
    const auto parsed = au::json::parse(text);
    if (!parsed.ok()) {
        return SnapshotDecodeResult{
            .reason = std::string{"workspace snapshot is not valid JSON: "} +
                      std::string{parsed.error().message}};
    }
    const Value &root = parsed.value();
    if (!root.is_object()) {
        return SnapshotDecodeResult{.reason = "workspace snapshot rejected at top level"};
    }
    Reject reject;
    auto snapshot = WorkspaceSnapshot{};
    if (!read_snapshot(root, reject, snapshot)) {
        return SnapshotDecodeResult{.reason = reject.reason()};
    }
    return SnapshotDecodeResult{.snapshot = std::move(snapshot)};
}

}  // namespace borealis::ui
