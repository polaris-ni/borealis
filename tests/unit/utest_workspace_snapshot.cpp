/// 测试类型: unit
/// 目标单元: include/borealis/ui/workspace_snapshot.h + src/ui/workspace_snapshot.cpp
/// 测试说明: 会话恢复快照的三条判据（`SPEC.FEAT.WS.11`，稿 §2 的 ②⑥⑦）：含四腿与多层分屏的样本
///           「模型 → JSON → 模型」全等（相对长度按位比，稿 F24 的同一条口径）、坏文档逐例**整份**
///           拒绝且拒因必然落在被改那一处（顶层类型／缺键／元素类型／枚举域外／计数·深度·节点数超界／
///           序位与数组对位不合，稿 D9① 与 F25 的「文档闸」分工——一棵形状自身合不合法归
///           `PaneTree::restore` 的六道自检，本件判它第二遍就是造第二真值源）、明文凭据进不了这份文档
///           （键名五枚禁列子串 + 两种凭据形态的值侧断言，AGENTS.md §4.5 第 24 条）、同一份档案在
///           `config::Store` 的落盘与本件里写出逐字相同的 JSON（稿 F12 那处在册代价的缓解证人）。

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/core/json.h"

#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/conn/local_terminal.h"
#include "borealis/conn/profile.h"
#include "borealis/ui/pane_tree.h"
#include "borealis/ui/workspace_snapshot.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_workspace_snapshot {

namespace au = aurora;
namespace fs = std::filesystem;

using au::json::Value;
using borealis::config::Settings;
using borealis::config::Store;
using borealis::conn::ConnectionType;
using borealis::conn::InMemoryCredentialStore;
using borealis::conn::KnownHostsPolicy;
using borealis::conn::LocalProfile;
using borealis::conn::LocalTerminalSpec;
using borealis::conn::Profile;
using borealis::conn::SecretHandle;
using borealis::conn::SecretKind;
using borealis::conn::SerialProfile;
using borealis::conn::SshProfile;
using borealis::conn::TelnetProfile;
using borealis::ui::kMaxSnapshotPanesPerTab;
using borealis::ui::kMaxSnapshotShapeDepth;
using borealis::ui::kMaxSnapshotTabsPerWindow;
using borealis::ui::kMaxSnapshotWindows;
using borealis::ui::kWorkspaceSnapshotSchemaVersion;
using borealis::ui::PaneAxis;
using borealis::ui::PaneShape;
using borealis::ui::SnapshotDecodeResult;
using borealis::ui::SnapshotTab;
using borealis::ui::SnapshotWindow;
using borealis::ui::WorkspaceSnapshot;

namespace {

// ============================================================
// 文本与文档件：坏文档逐例都由「合法那份」单点改出，改完仍是合法 JSON
// ============================================================

/// @brief 本轮用例的独占目录（框架按用例建目录并在结束时清理）。
[[nodiscard]] auto make_path(std::string_view name) -> fs::path {
    return fs::path{aurora::testing::isolation::temp_dir()} / std::string{name};
}

[[nodiscard]] auto read_file(const fs::path &path) -> std::string {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// @brief 解析一段 JSON 文本；坏文本回空值（用例据此把「解析失败」与「文档不合」分开断言）。
[[nodiscard]] auto parse(std::string_view text) -> std::optional<Value> {
    const auto parsed = au::json::parse(text);
    return parsed.ok() ? std::optional<Value>{parsed.value()} : std::nullopt;
}

/// @brief 把一棵 JSON 子树按紧凑档写成文本（判据 ⑦ 比的就是这一串字节）。
[[nodiscard]] auto bytes_of(const Value &node) -> std::string {
    const auto dumped = au::json::dump(node);
    AURORA_TEST_REQUIRE(dumped.ok());
    return std::string{dumped.value()};
}

/// @brief 沿一串对象键取子节点；任一级未命中即用例失败（路径都是写死的，取不到就是文档形态变了）。
[[nodiscard]] auto field(const Value &node, std::initializer_list<std::string_view> keys) -> const Value & {
    const Value *held = &node;
    for (std::string_view key : keys) {
        AURORA_TEST_REQUIRE(held != nullptr);
        held = held->at(key);
        AURORA_TEST_REQUIRE_MSG(held != nullptr, "missing json field: " + std::string{key});
    }
    return *held;
}

/// @brief 取数组元素；越界即用例失败（同 `field`，形态前提不成立时后面的等式没有意义）。
[[nodiscard]] auto element(const Value &node, std::size_t index) -> const Value & {
    const Value *held = node.at(index);
    AURORA_TEST_REQUIRE_MSG(held != nullptr, "missing json element at index " + std::to_string(index));
    return *held;
}

/// @brief 在文本里把唯一一处 `from` 换成 `to`（换成空串即「这个键不存在」）。
///
/// 两处都进断言：找不到就是用例的变异点拼错（目标判据根本没被碰到），出现两处就是「改的不是那一槽」。
/// 两种情况下这条坏文档用例都会退化成空转，故宁可让用例当场终止。
[[nodiscard]] auto substituted(std::string_view text, std::string_view from, std::string_view to) -> std::string {
    std::string out{text};
    const auto first = out.find(from);
    AURORA_TEST_REQUIRE_MSG(first != std::string::npos, "mutation target absent: " + std::string{from});
    AURORA_TEST_REQUIRE_MSG(out.find(from, first + 1) == std::string::npos,
                            "mutation target is ambiguous: " + std::string{from});
    out.replace(first, from.size(), to);
    return out;
}

/// @brief 写侧文本；写不出来即用例前提不成立（界外那份文档也得先写得出去，才谈得上被读侧拒）。
[[nodiscard]] auto written(const WorkspaceSnapshot &snapshot) -> std::string {
    const auto text = snapshot.to_json();
    AURORA_TEST_REQUIRE(text.has_value());
    return *text;
}

/// @brief 走一遍「自家写侧写出 → 读侧读回」：判据 ② 的往返腿就是这一句，正例全靠它给结论。
[[nodiscard]] auto reparse(const WorkspaceSnapshot &snapshot) -> SnapshotDecodeResult {
    return WorkspaceSnapshot::from_json(written(snapshot));
}

/// @brief 收集全部对象键名（照 `utest_config` 的凭据扫描同形态，判据 ⑥ 的键名半边用它）。
auto collect_keys(const Value &node, std::vector<std::string> &out) -> void {
    if (node.is_object()) {
        for (const auto &entry : node.entries()) {
            out.emplace_back(entry.key);
            collect_keys(entry.value, out);
        }
        return;
    }
    if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            if (const Value *item = node.at(index); item != nullptr) {
                collect_keys(*item, out);
            }
        }
    }
}

constexpr std::string_view kVersionField = R"("schema_version":1)";
constexpr std::string_view kSelectedTabField = R"("selected_tab":0)";
constexpr std::string_view kEmptyPanesField = R"("panes":[])";
constexpr std::string_view kPanesField =
    R"("panes":[{"command_line":"","working_directory":"","environment":{}}])";
constexpr std::string_view kShapeField = R"("shape":{"leaf":true,"order":0})";
constexpr std::string_view kFocusedField = R"("focused":0)";
constexpr std::string_view kSshBlock =
    R"({"host":"gw.example.com","port":2222,"user":"operator","auth_method":"privatekey",)"
    R"("identity_file":"/home/dev/.ssh/id_ed25519","agent_forwarding":true,)"
    R"("known_hosts_policy":"ask",)"
    R"("credential":{"kind":"reference","vault_id":"vault://borealis/gw","profile_id":"","label":""}})";
constexpr std::string_view kSerialBlock =
    R"({"device_path":"/dev/ttyUSB7","baud":57600,"data_bits":7,"stop_bits":2,)"
    R"("parity":"even","flow":"hardware","line_ending":"CRLF","encoding":"UTF-8"})";
constexpr std::string_view kTelnetBlock = R"({"host":"switch.example.com","port":2323})";

/// @brief 标签文本的七个可替换槽，每槽存的是**整个字段**（含键名）；空槽＝「文档里没有这个键」。
///
/// 「缺键」与「键在而值坏」因此是两种不同的改法，各自的拒因路径也不同：读侧对两者都整份拒绝，
/// 但日志必须说清坏在哪一格，否则稿 D8① 那句 `AURORA_LOG_WARN` 只能写成「布局文件坏了」。
struct TabSlots {
    std::string kind = R"("kind":"local")";
    std::string profile_id = R"("profile_id":"")";
    std::string title = R"("title":"")";
    std::string block;  ///< 档案块字段：`"ssh":{...}` 形态；本地腿为空（它没有标签级档案块）。
    std::string panes = std::string{kPanesField};
    std::string shape = std::string{kShapeField};
    std::string focused = std::string{kFocusedField};
};

/// @brief 把非空字段用 `,` 连起来（空槽跳过），于是「删掉一个键」就是把那一槽置空而不留悬空逗号。
[[nodiscard]] auto joined(std::initializer_list<std::string_view> fields) -> std::string {
    std::string out;
    for (std::string_view one : fields) {
        if (one.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        out += one;
    }
    return out;
}

[[nodiscard]] auto tab_text(const TabSlots &slots) -> std::string {
    return "{" + joined({slots.kind, slots.profile_id, slots.title, slots.block, slots.panes, slots.shape,
                         slots.focused}) + "}";
}

[[nodiscard]] auto tabs_field(std::initializer_list<std::string_view> tabs) -> std::string {
    return std::string{R"("tabs":[)"} + joined(tabs) + "]";
}

[[nodiscard]] auto tabs_field(std::string_view one_tab) -> std::string {
    return tabs_field({one_tab});
}

[[nodiscard]] auto windows_field(std::initializer_list<std::string_view> windows) -> std::string {
    return std::string{R"("windows":[)"} + joined(windows) + "]";
}

[[nodiscard]] auto window_text(std::string_view tabs, std::string_view selected = kSelectedTabField)
    -> std::string {
    return "{" + joined({tabs, selected}) + "}";
}

[[nodiscard]] auto document_text(std::string_view windows, std::string_view version = kVersionField)
    -> std::string {
    return "{" + joined({version, windows}) + "}";
}

/// @brief 把一槽标签文本塞进「一窗一标签」的最小合法文档；版本槽与选中槽同样可置空。
[[nodiscard]] auto document_of(std::string_view tab, std::string_view version = kVersionField,
                               std::string_view selected = kSelectedTabField) -> std::string {
    return document_text(windows_field({window_text(tabs_field(tab), selected)}), version);
}

/// @brief 一槽带档案块的标签：`panes` 换成空表（读侧对非本地标签要求它恒空，那是对位判据）。
[[nodiscard]] auto block_tab(std::string_view kind, std::string_view block_field) -> std::string {
    return tab_text(TabSlots{.kind = std::string{kind},
                             .block = std::string{block_field},
                             .panes = std::string{kEmptyPanesField}});
}

/// @brief 一槽坏形状标签：形状体由调用方整段给出，其余各槽仍是合法形态。
[[nodiscard]] auto shape_tab(std::string_view shape_body) -> std::string {
    return tab_text(TabSlots{.shape = std::string{R"("shape":)"} + std::string{shape_body}});
}

/// @brief 拒因的固定前缀（`Reject::at` 写出的形态：前缀 + 点号／下标路径）。
constexpr std::string_view kRejectedPrefix = "workspace snapshot rejected at ";

/// @brief 单窗单标签文档里那一枚标签的点号路径：坏例的拒因都落在这条路径的某一槽上。
const std::string kTabPath = "windows[0].tabs[0]";

[[nodiscard]] auto rejected(std::string_view path) -> std::string {
    return std::string{kRejectedPrefix} + std::string{path};
}

[[nodiscard]] auto in_tab(std::string_view suffix) -> std::string {
    return kTabPath + std::string{suffix};
}

[[nodiscard]] auto in_pane(std::string_view suffix) -> std::string {
    return in_tab(".panes[0]" + std::string{suffix});
}

// ============================================================
// 档案料：每个字段都取非缺省值，缺省值会把「写侧漏键」读成往返等值
// ============================================================

[[nodiscard]] auto ssh_profile() -> SshProfile {
    auto ssh = SshProfile{};
    ssh.host = "gw.example.com";
    ssh.port = 2222;
    ssh.user = "operator";
    ssh.auth_method = "privatekey";
    ssh.identity_file = "/home/dev/.ssh/id_ed25519";
    ssh.agent_forwarding = true;
    ssh.known_hosts_policy = KnownHostsPolicy::Ask;
    ssh.credential = SecretHandle::reference("vault://borealis/gw");
    return ssh;
}

[[nodiscard]] auto serial_profile() -> SerialProfile {
    auto serial = SerialProfile{};
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

[[nodiscard]] auto telnet_profile() -> TelnetProfile {
    auto telnet = TelnetProfile{};
    telnet.host = "switch.example.com";
    telnet.port = 2323;
    return telnet;
}

[[nodiscard]] auto local_spec(std::string_view directory) -> LocalTerminalSpec {
    auto spec = LocalTerminalSpec{};
    spec.command_line = "/bin/bash -l";
    spec.working_directory = std::string{directory};
    spec.environment.emplace("EDITOR", "vis");
    spec.environment.emplace("TERM", "xterm-256color");
    return spec;
}

/// @brief 把档案表的本地块折成一格的有效料：两者字段同集（快照存有效值，稿 F14），
///        判据 ⑦ 靠它把同一份三件分别喂给 `config::Store` 的落盘与本件。
[[nodiscard]] auto local_spec_from(const LocalProfile &local) -> LocalTerminalSpec {
    auto spec = LocalTerminalSpec{};
    spec.command_line = local.command_line;
    spec.working_directory = local.working_directory;
    spec.environment = local.environment;
    return spec;
}

// ============================================================
// 形状件：深度界与节点数界各要一种「只越那一界」的形状
// ============================================================

/// @brief 一条「每层只把右边那一格再切一刀」的链：`levels` 层容器 + `levels+1` 枚叶子。
///
/// 节点数只有 `2*levels+1`，远在节点界内，故「深度超限」这一例必然只由深度那一界判（稿 D9①／F25）。
/// 叶子序位取 0..levels 的排列，于是它同时是 `PaneTree::restore` 认得的形状。
[[nodiscard]] auto chain(std::size_t levels) -> PaneShape {
    auto node = PaneShape{.leaf = true, .order = levels};
    for (std::size_t level = levels; level > 0U; --level) {
        auto parent = PaneShape{};
        parent.leaf = false;
        parent.axis = PaneAxis::Horizontal;
        parent.lengths = {1.0, 1.0};
        parent.children = {PaneShape{.leaf = true, .order = level - 1U}, std::move(node)};
        node = std::move(parent);
    }
    return node;
}

/// @brief 一棵满二叉树：`depth` 层容器、`2^depth` 枚叶子、`2^(depth+1)-1` 个节点。
///
/// 深度只有 `depth`，故「节点数超限」这一例必然只由节点那一界判。叶子序位按阅读序从 0 递增发出。
/// @param order_cursor 序位发放器；调用方据此知道叶子数（本件用它反推该配几格本地料）。
[[nodiscard]] auto full_tree(std::size_t depth, std::size_t &order_cursor) -> PaneShape {
    if (depth == 0U) {
        auto leaf = PaneShape{};
        leaf.leaf = true;
        leaf.order = order_cursor++;
        return leaf;
    }
    auto left = full_tree(depth - 1U, order_cursor);
    auto right = full_tree(depth - 1U, order_cursor);
    auto node = PaneShape{};
    node.leaf = false;
    node.axis = PaneAxis::Vertical;
    node.lengths = {1.0, 1.0};
    node.children = {std::move(left), std::move(right)};
    return node;
}

/// @brief 一层左右、第三格再上下切一刀：四枚叶子、两层不同轴（判据 ② 那个样本的形状半边）。
[[nodiscard]] auto nested_shape() -> PaneShape {
    auto third = PaneShape{};
    third.leaf = false;
    third.axis = PaneAxis::Vertical;
    third.lengths = {0.75, 0.25};
    third.children = {PaneShape{.leaf = true, .order = 2}, PaneShape{.leaf = true, .order = 3}};
    auto root = PaneShape{};
    root.leaf = false;
    root.axis = PaneAxis::Horizontal;
    root.lengths = {2.0, 0.5, 1.0};
    root.children = {
        PaneShape{.leaf = true, .order = 0},
        PaneShape{.leaf = true, .order = 1},
        std::move(third),
    };
    return root;
}

// ============================================================
// 模型件
// ============================================================

/// @brief 一格本地标签的合法料（单叶形状）：窗数与标签数那两界用它填表。
[[nodiscard]] auto one_local_tab() -> SnapshotTab {
    auto tab = SnapshotTab{};
    tab.panes = {local_spec("/srv")};
    return tab;
}

/// @brief 一扇窗（一个标签）：窗数上界那一例用它填表。
[[nodiscard]] auto one_window() -> SnapshotWindow {
    return SnapshotWindow{.tabs = {one_local_tab()}, .selected_tab = 0};
}

/// @brief 一窗一格的快照，形状、格数与连接腿由调用方给：计数／深度／节点三界都用它造现场。
///
/// 非本地腿用来造「形状越界而不受本地料对位干扰」那一类：读侧要求非本地标签的 `panes` 恒空，
/// 于是那一类的拒因必然落在形状上。
[[nodiscard]] auto single_tab_snapshot(const PaneShape &shape, std::size_t panes,
                                       ConnectionType kind = ConnectionType::Local) -> WorkspaceSnapshot {
    auto tab = SnapshotTab{};
    tab.kind = kind;
    tab.shape = shape;
    for (std::size_t index = 0; index < panes; ++index) {
        tab.panes.push_back(local_spec("/srv/" + std::to_string(index)));
    }
    auto window = SnapshotWindow{};
    window.tabs = {std::move(tab)};
    return WorkspaceSnapshot{.windows = {std::move(window)}};
}

/// @brief 一份含四腿、含多层分屏的样本（稿 §2 判据 ② 的那个样本）。
[[nodiscard]] auto sample_snapshot() -> WorkspaceSnapshot {
    auto local_tab = SnapshotTab{};
    local_tab.kind = ConnectionType::Local;
    local_tab.title = "build box";
    local_tab.panes = {local_spec("/home/dev/src"), local_spec("/home/dev"), local_spec("/var/log"),
                       local_spec("/home/dev/docs")};
    local_tab.shape = nested_shape();
    local_tab.focused_order = 2;  // 无档案可依的那一档：profile_id 留空（稿 F10 的 quick connect 同形态）

    auto ssh_tab = SnapshotTab{};
    ssh_tab.kind = ConnectionType::Ssh;
    ssh_tab.profile_id = "gw";
    ssh_tab.title = "";  // 没手动改过名：重启后落回类型缺省名（稿 D10①）
    ssh_tab.ssh = ssh_profile();
    ssh_tab.shape = nested_shape();
    ssh_tab.focused_order = 3;

    auto serial_tab = SnapshotTab{};
    serial_tab.kind = ConnectionType::Serial;
    serial_tab.profile_id = "console-0";
    serial_tab.title = "console";
    serial_tab.serial = serial_profile();

    auto telnet_tab = SnapshotTab{};
    telnet_tab.kind = ConnectionType::Telnet;
    telnet_tab.profile_id = "sw-2";
    telnet_tab.telnet = telnet_profile();

    // 凭据库不可用那一档（`SPEC.FEAT.CONN.09` 的降级哨兵）：落盘形态与 Reference 同键、只换 `kind`。
    auto asking = ssh_profile();
    asking.host = "old.example.com";
    asking.credential = SecretHandle::ask_every_time("old", "old prompt");
    auto asking_tab = SnapshotTab{};
    asking_tab.kind = ConnectionType::Ssh;
    asking_tab.profile_id = "old";
    asking_tab.title = "old";
    asking_tab.ssh = std::move(asking);

    auto first = SnapshotWindow{};
    first.tabs = {std::move(local_tab), std::move(ssh_tab), std::move(serial_tab), std::move(telnet_tab),
                  std::move(asking_tab)};
    first.selected_tab = 1;

    auto second = SnapshotWindow{};
    second.tabs = {one_local_tab()};

    return WorkspaceSnapshot{.windows = {std::move(first), std::move(second)}};
}

}  // namespace

AURORA_TEST_CASE(a_snapshot_with_four_kinds_and_nested_panes_round_trips) {
    const WorkspaceSnapshot sample = sample_snapshot();
    const auto decoded = reparse(sample);
    AURORA_TEST_CHECK_MSG(decoded.reason.empty(), decoded.reason);
    AURORA_TEST_REQUIRE(decoded.snapshot.has_value());
    AURORA_TEST_CHECK_TRUE(*decoded.snapshot == sample);

    // 写侧确定性：同一份模型两次写出的字节逐字相同——否则「往返全等」会被键序抖动蒙过去。
    AURORA_TEST_CHECK_EQ(written(*decoded.snapshot), written(sample));
}

AURORA_TEST_CASE(relative_proportions_survive_the_document_bit_for_bit) {
    auto sample = sample_snapshot();
    // 非整倍数的比例：归一或吸附任何一处，两次读数就差在 1e-16 上（稿 F24 的同一条理由）。
    sample.windows[0].tabs[0].shape.lengths = {2.0 / 3.0, 0.1, 1.0 - 2.0 / 3.0 - 0.1};
    const auto decoded = reparse(sample);
    AURORA_TEST_REQUIRE(decoded.snapshot.has_value());
    AURORA_TEST_CHECK_TRUE(*decoded.snapshot == sample);

    const auto &lengths = (*decoded.snapshot).windows[0].tabs[0].shape.lengths;
    AURORA_TEST_REQUIRE_EQ(lengths.size(), 3U);
    // 反空转：三槽若被折成等分，上面的全等判断会连着这一条一起红，而不是只剩「看起来一样」。
    AURORA_TEST_CHECK_NE(lengths[0], lengths[1]);
    AURORA_TEST_CHECK_NE(lengths[1], lengths[2]);
}

AURORA_TEST_CASE(an_empty_snapshot_is_the_no_restore_payload) {
    // 「开关关掉」与「上次一扇窗都没留」两档共用这一形态：空 windows 是合法文档，不是坏文件。
    // 恢复料恒空由这份文档表达，而不是由「没有文件」表达（稿 §2 判据 ③ 的快照半边）。
    const auto decoded = reparse(WorkspaceSnapshot{});
    AURORA_TEST_CHECK_MSG(decoded.reason.empty(), decoded.reason);
    AURORA_TEST_REQUIRE(decoded.snapshot.has_value());
    AURORA_TEST_CHECK_TRUE(decoded.snapshot->windows.empty());
    AURORA_TEST_CHECK_TRUE(*decoded.snapshot == WorkspaceSnapshot{});
}

AURORA_TEST_CASE(malformed_documents_are_rejected_whole) {
    struct Case {
        std::string_view name;
        std::string reason;
        std::string text;
    };
    const auto good_tab = tab_text(TabSlots{});
    const std::vector<Case> cases = {
        Case{"top level is an array", rejected("top level"), R"([{"schema_version":1,"windows":[]}])"},
        Case{"top level is a scalar", rejected("top level"), "1"},
        Case{"top level is an empty object", rejected("schema_version"), "{}"},
        Case{"missing schema version", rejected("schema_version"), document_of(good_tab, std::string{})},
        Case{"schema version above supported", rejected("schema_version"),
             document_of(good_tab, R"("schema_version":2)")},
        Case{"schema version below the only one", rejected("schema_version"),
             document_of(good_tab, R"("schema_version":0)")},
        Case{"schema version is text", rejected("schema_version"),
             document_of(good_tab, R"("schema_version":"1")")},
        Case{"schema version is a fraction", rejected("schema_version"),
             document_of(good_tab, R"("schema_version":1.5)")},
        Case{"missing windows", rejected("windows"), document_text(std::string{})},
        Case{"windows is an object", rejected("windows"), document_text(R"("windows":{})")},
        Case{"windows is a scalar", rejected("windows"), document_text(R"("windows":1)")},
        Case{"windows element is not an object", rejected("windows[0]"),
             document_text(R"("windows":[1])")},
        Case{"missing tabs", rejected("windows[0].tabs"),
             document_text(windows_field({window_text(std::string{kSelectedTabField})}))},
        Case{"tabs is an object", rejected("windows[0].tabs"),
             document_text(windows_field({window_text("\"tabs\":{}")}))},
        Case{"tabs element is not an object", rejected(in_tab("")), document_of("0")},
        Case{"empty tab strip", rejected("windows[0].selected_tab"),
             document_text(windows_field({window_text("\"tabs\":[]")}))},
        Case{"missing selected tab", rejected("windows[0].selected_tab"),
             document_of(good_tab, kVersionField, std::string{})},
        Case{"selected tab out of range", rejected("windows[0].selected_tab"),
             document_of(good_tab, kVersionField, R"("selected_tab":1)")},
        Case{"selected tab is text", rejected("windows[0].selected_tab"),
             document_of(good_tab, kVersionField, R"("selected_tab":"0")")},
    };
    for (const Case &one : cases) {
        const auto decoded = WorkspaceSnapshot::from_json(one.text);
        AURORA_TEST_CHECK_MSG(!decoded.snapshot.has_value(), std::string{one.name});
        AURORA_TEST_CHECK_MSG(decoded.reason == one.reason, std::string{one.name} + " -> " + decoded.reason);
    }

    // 反空转：原样那份读得回来，且拒因是逐条对上的路径——否则上面那些红里没有一条能指到坏掉的那一格。
    const auto intact = WorkspaceSnapshot::from_json(document_of(good_tab));
    AURORA_TEST_CHECK_MSG(intact.snapshot.has_value(), intact.reason);
}

AURORA_TEST_CASE(missing_or_wrongly_typed_tab_fields_are_rejected) {
    struct Case {
        std::string_view name;
        std::string reason;
        std::string text;
    };
    const std::vector<Case> cases = {
        Case{"missing kind", rejected(in_tab(".kind")), document_of(tab_text(TabSlots{.kind = ""}))},
        Case{"missing profile id", rejected(in_tab(".profile_id")),
             document_of(tab_text(TabSlots{.profile_id = ""}))},
        Case{"missing title", rejected(in_tab(".title")), document_of(tab_text(TabSlots{.title = ""}))},
        Case{"missing panes", rejected(in_tab(".panes")), document_of(tab_text(TabSlots{.panes = ""}))},
        Case{"missing shape", rejected(in_tab(".shape")), document_of(tab_text(TabSlots{.shape = ""}))},
        Case{"missing focused", rejected(in_tab(".focused")),
             document_of(tab_text(TabSlots{.focused = ""}))},
        Case{"kind is a number", rejected(in_tab(".kind")),
             document_of(tab_text(TabSlots{.kind = R"("kind":0)"}))},
        Case{"kind out of table", rejected(in_tab(".kind")),
             document_of(tab_text(TabSlots{.kind = R"("kind":"ftp")"}))},
        Case{"profile id is a number", rejected(in_tab(".profile_id")),
             document_of(tab_text(TabSlots{.profile_id = R"("profile_id":7)"}))},
        Case{"title is a number", rejected(in_tab(".title")),
             document_of(tab_text(TabSlots{.title = R"("title":7)"}))},
        Case{"panes is an object", rejected(in_tab(".panes")),
             document_of(tab_text(TabSlots{.panes = R"("panes":{})"}))},
        Case{"panes element is not an object", rejected(in_pane("")),
             document_of(tab_text(TabSlots{.panes = R"("panes":[0])"}))},
        Case{"pane missing command line", rejected(in_pane(".command_line")),
             document_of(tab_text(
                 TabSlots{.panes = substituted(kPanesField, R"("command_line":"",)", std::string{})}))},
        Case{"pane missing working directory", rejected(in_pane(".working_directory")),
             document_of(tab_text(TabSlots{
                 .panes = substituted(kPanesField, R"("working_directory":"",)", std::string{})}))},
        Case{"pane environment is an array", rejected(in_pane(".environment")),
             document_of(tab_text(
                 TabSlots{.panes = substituted(kPanesField, R"("environment":{})", R"("environment":[])")}))},
        Case{"pane environment value is a number", rejected(in_pane(".environment.EDITOR")),
             document_of(tab_text(TabSlots{
                 .panes = substituted(kPanesField, R"("environment":{})", R"("environment":{"EDITOR":7})")}))},
        Case{"focused is text", rejected(in_tab(".focused")),
             document_of(tab_text(TabSlots{.focused = R"("focused":"0")"}))},
        Case{"focused negative", rejected(in_tab(".focused")),
             document_of(tab_text(TabSlots{.focused = R"("focused":-1)"}))},
        Case{"focused out of leaves", rejected(in_tab(".focused")),
             document_of(tab_text(TabSlots{.focused = R"("focused":1)"}))},
    };
    for (const Case &one : cases) {
        const auto decoded = WorkspaceSnapshot::from_json(one.text);
        AURORA_TEST_CHECK_MSG(!decoded.snapshot.has_value(), std::string{one.name});
        AURORA_TEST_CHECK_MSG(decoded.reason == one.reason, std::string{one.name} + " -> " + decoded.reason);
    }
}

AURORA_TEST_CASE(malformed_shapes_are_rejected_by_the_document_gates) {
    struct Case {
        std::string_view name;
        std::string reason;
        std::string text;
    };
    const std::vector<Case> cases = {
        Case{"shape is a number", rejected(in_tab(".shape")),
             document_of(tab_text(TabSlots{.shape = "\"shape\":0"}))},
        Case{"leaf missing", rejected(in_tab(".shape.leaf")), document_of(shape_tab(R"({"order":0})"))},
        Case{"leaf is text", rejected(in_tab(".shape.leaf")),
             document_of(shape_tab(R"({"leaf":"true","order":0})"))},
        Case{"leaf order missing", rejected(in_tab(".shape.order")), document_of(shape_tab(R"({"leaf":true})"))},
        Case{"leaf order is text", rejected(in_tab(".shape.order")),
             document_of(shape_tab(R"({"leaf":true,"order":"0"})"))},
        Case{"leaf order is negative", rejected(in_tab(".shape.order")),
             document_of(shape_tab(R"({"leaf":true,"order":-1})"))},
        Case{"container axis missing", rejected(in_tab(".shape.axis")), document_of(shape_tab(R"({"leaf":false})"))},
        Case{"container axis out of table", rejected(in_tab(".shape.axis")),
             document_of(shape_tab(R"({"leaf":false,"axis":"diagonal"})"))},
        Case{"container lengths missing", rejected(in_tab(".shape.lengths")),
             document_of(shape_tab(R"({"leaf":false,"axis":"horizontal"})"))},
        Case{"container length is text", rejected(in_tab(".shape.lengths")),
             document_of(shape_tab(R"({"leaf":false,"axis":"horizontal","lengths":["a"]})"))},
        Case{"container children missing", rejected(in_tab(".shape.children")),
             document_of(shape_tab(R"({"leaf":false,"axis":"horizontal","lengths":[1.0,1.0]})"))},
        Case{"child is not an object", rejected(in_tab(".shape.children[0]")),
             document_of(shape_tab(R"({"leaf":false,"axis":"horizontal","lengths":[1.0,1.0],)"
                                   R"("children":[0,{"leaf":true,"order":0}]})"))},
    };
    for (const Case &one : cases) {
        const auto decoded = WorkspaceSnapshot::from_json(one.text);
        AURORA_TEST_CHECK_MSG(!decoded.snapshot.has_value(), std::string{one.name});
        AURORA_TEST_CHECK_MSG(decoded.reason == one.reason, std::string{one.name} + " -> " + decoded.reason);
    }

    // 六道**形状**自检（单子层、长度与子数不符、序位不是排列……）不归本件判：文档闸只保证折得出来，
    // 建不建得起来归 `PaneTree::restore`（稿 F25）。这一例把那条分工钉住——「树会拒绝、文档挑不出
    // 毛病」的形状必须读得回来；若哪天有人在本件重抄一遍六道，这条就会红。
    const auto single_child_layer = document_of(shape_tab(
        R"json({"leaf":false,"axis":"horizontal","lengths":[1.0],)json"
        R"json("children":[{"leaf":true,"order":0}]})json"));
    const auto folded = WorkspaceSnapshot::from_json(single_child_layer);
    AURORA_TEST_CHECK_MSG(folded.snapshot.has_value(), folded.reason);
}

AURORA_TEST_CASE(out_of_domain_names_in_the_profile_blocks_are_rejected) {
    struct Case {
        std::string_view name;
        std::string document;
        std::string_view from;
        std::string_view to;
        std::string reason;
    };
    const auto ssh = document_of(block_tab(R"("kind":"ssh")", R"("ssh":)" + std::string{kSshBlock}));
    const auto serial = document_of(block_tab(R"("kind":"serial")", R"("serial":)" + std::string{kSerialBlock}));
    const auto telnet = document_of(block_tab(R"("kind":"telnet")", R"("telnet":)" + std::string{kTelnetBlock}));
    const std::vector<Case> cases = {
        Case{"ssh host missing", ssh, R"("host":"gw.example.com",)", std::string{},
             rejected(in_tab(".ssh.host"))},
        Case{"ssh port is text", ssh, R"("port":2222)", R"("port":"2222")", rejected(in_tab(".ssh.port"))},
        Case{"ssh port exceeds int", ssh, R"("port":2222)",
             R"("port":2147483648)", rejected(in_tab(".ssh.port"))},
        Case{"ssh auth method out of table", ssh, R"("auth_method":"privatekey")", R"("auth_method":"keyboard")",
             rejected(in_tab(".ssh.auth_method"))},
        Case{"ssh agent forwarding is text", ssh, R"("agent_forwarding":true)", R"("agent_forwarding":"true")",
             rejected(in_tab(".ssh.agent_forwarding"))},
        Case{"known hosts policy out of table", ssh, R"("known_hosts_policy":"ask")",
             R"("known_hosts_policy":"maybe")", rejected(in_tab(".ssh.known_hosts_policy"))},
        Case{"credential kind out of table", ssh, R"("kind":"reference")", R"("kind":"raw")",
             rejected(in_tab(".ssh.credential.kind"))},
        Case{"credential vault id missing", ssh, R"("vault_id":"vault://borealis/gw",)", std::string{},
             rejected(in_tab(".ssh.credential.vault_id"))},
        Case{"credential block missing", ssh,
             R"(,"credential":{"kind":"reference","vault_id":"vault://borealis/gw","profile_id":"","label":""})",
             std::string{}, rejected(in_tab(".ssh.credential"))},
        Case{"serial device path missing", serial, R"("device_path":"/dev/ttyUSB7",)", std::string{},
             rejected(in_tab(".serial.device_path"))},
        Case{"serial baud is text", serial, R"("baud":57600)", R"("baud":"57600")",
             rejected(in_tab(".serial.baud"))},
        Case{"serial parity out of table", serial, R"("parity":"even")", R"("parity":"zz")",
             rejected(in_tab(".serial.parity"))},
        Case{"serial flow out of table", serial, R"("flow":"hardware")", R"("flow":"zz")",
             rejected(in_tab(".serial.flow"))},
        Case{"serial line ending out of table", serial, R"("line_ending":"CRLF")", R"("line_ending":"zz")",
             rejected(in_tab(".serial.line_ending"))},
        Case{"telnet port missing", telnet, R"(,"port":2323)", std::string{}, rejected(in_tab(".telnet.port"))},
        Case{"telnet host is a number", telnet, R"("host":"switch.example.com")", R"("host":7)",
             rejected(in_tab(".telnet.host"))},
    };
    for (const Case &one : cases) {
        const auto decoded = WorkspaceSnapshot::from_json(substituted(one.document, one.from, one.to));
        AURORA_TEST_CHECK_MSG(!decoded.snapshot.has_value(), std::string{one.name});
        AURORA_TEST_CHECK_MSG(decoded.reason == one.reason, std::string{one.name} + " -> " + decoded.reason);
    }

    // 反空转：三块原样各读得回来，且折出的模型与代码里那份档案逐字段全等——于是上面那些手抄文本与
    // `*_profile()` 是同一份料，改任何一边都会红在这一条上。
    const auto ssh_back = WorkspaceSnapshot::from_json(ssh);
    AURORA_TEST_CHECK_MSG(ssh_back.snapshot.has_value(), ssh_back.reason);
    AURORA_TEST_REQUIRE(ssh_back.snapshot.has_value());
    AURORA_TEST_CHECK_TRUE(ssh_back.snapshot->windows[0].tabs[0] ==
                           SnapshotTab{.kind = ConnectionType::Ssh, .ssh = ssh_profile()});

    const auto serial_back = WorkspaceSnapshot::from_json(serial);
    AURORA_TEST_CHECK_MSG(serial_back.snapshot.has_value(), serial_back.reason);
    AURORA_TEST_REQUIRE(serial_back.snapshot.has_value());
    AURORA_TEST_CHECK_TRUE(serial_back.snapshot->windows[0].tabs[0] ==
                           SnapshotTab{.kind = ConnectionType::Serial, .serial = serial_profile()});

    const auto telnet_back = WorkspaceSnapshot::from_json(telnet);
    AURORA_TEST_CHECK_MSG(telnet_back.snapshot.has_value(), telnet_back.reason);
    AURORA_TEST_REQUIRE(telnet_back.snapshot.has_value());
    AURORA_TEST_CHECK_TRUE(telnet_back.snapshot->windows[0].tabs[0] ==
                           SnapshotTab{.kind = ConnectionType::Telnet, .telnet = telnet_profile()});
}

AURORA_TEST_CASE(kind_and_payload_must_agree_in_a_tab) {
    // 非本地标签带着 per-pane 料是「写侧漏了 kind 分流」的形态：读回来只会有一张说不清的表（稿 D4①）。
    const auto ssh_with_panes =
        document_of(tab_text(TabSlots{.kind = R"("kind":"ssh")", .block = R"("ssh":)" + std::string{kSshBlock}}));
    const auto mixed = WorkspaceSnapshot::from_json(ssh_with_panes);
    AURORA_TEST_CHECK_FALSE(mixed.snapshot.has_value());
    AURORA_TEST_CHECK_EQ(mixed.reason, rejected(in_tab(".panes")));

    // 本地腿没有标签级档案块，SSH 腿必须有：缺块就是坏文档，而不是「按本地腿起一格」。
    const auto ssh_without_block =
        WorkspaceSnapshot::from_json(document_of(block_tab(R"("kind":"ssh")", std::string{})));
    AURORA_TEST_CHECK_FALSE(ssh_without_block.snapshot.has_value());
    AURORA_TEST_CHECK_EQ(ssh_without_block.reason, rejected(in_tab(".ssh")));

    // 串口腿带着 SSH 块（两块的键名集不同，读侧按 kind 只取自己那一块）：于是它读回来是「缺 serial 块」。
    const auto serial_wearing_ssh =
        document_of(block_tab(R"("kind":"serial")", R"("ssh":)" + std::string{kSshBlock}));
    AURORA_TEST_CHECK_EQ(WorkspaceSnapshot::from_json(serial_wearing_ssh).reason, rejected(in_tab(".serial")));
}

AURORA_TEST_CASE(read_side_bounds_reject_the_whole_document) {
    AURORA_TEST_CHECK_EQ(kWorkspaceSnapshotSchemaVersion, 1);
    AURORA_TEST_CHECK_EQ(kMaxSnapshotWindows, 32U);
    AURORA_TEST_CHECK_EQ(kMaxSnapshotTabsPerWindow, 128U);
    AURORA_TEST_CHECK_EQ(kMaxSnapshotPanesPerTab, 128U);
    AURORA_TEST_CHECK_EQ(kMaxSnapshotShapeDepth, 64U);

    // 四道上界各判一界：界内一格恰好写满、界外一格整份拒绝。写侧不设限（稿 D9①），故界外那份文档
    // 由自家写侧产出再交读侧判——这一例走的是真文档，不是手抄文本。
    auto at_window_bound = WorkspaceSnapshot{};
    for (std::size_t index = 0; index < kMaxSnapshotWindows; ++index) {
        at_window_bound.windows.push_back(one_window());
    }
    const auto at_windows = reparse(at_window_bound);
    AURORA_TEST_CHECK_MSG(at_windows.snapshot.has_value(), at_windows.reason);
    auto over_window_bound = at_window_bound;
    over_window_bound.windows.push_back(one_window());
    const auto over_windows = reparse(over_window_bound);
    AURORA_TEST_CHECK_FALSE(over_windows.snapshot.has_value());
    AURORA_TEST_CHECK_EQ(over_windows.reason, rejected("windows"));

    auto crowded = SnapshotWindow{};
    for (std::size_t index = 0; index < kMaxSnapshotTabsPerWindow; ++index) {
        crowded.tabs.push_back(one_local_tab());
    }
    crowded.selected_tab = kMaxSnapshotTabsPerWindow - 1U;
    const auto at_tab_bound = WorkspaceSnapshot{.windows = {crowded}};
    const auto at_tabs = reparse(at_tab_bound);
    AURORA_TEST_CHECK_MSG(at_tabs.snapshot.has_value(), at_tabs.reason);
    auto over_tab_bound = at_tab_bound;
    over_tab_bound.windows[0].tabs.push_back(one_local_tab());
    const auto over_tabs = reparse(over_tab_bound);
    AURORA_TEST_CHECK_FALSE(over_tabs.snapshot.has_value());
    AURORA_TEST_CHECK_EQ(over_tabs.reason, rejected("windows[0].tabs"));

    std::size_t leaves = 0;
    const auto packed_shape = full_tree(7U, leaves);  // 128 枚叶子、255 个节点：两界都恰好不越
    AURORA_TEST_CHECK_EQ(leaves, kMaxSnapshotPanesPerTab);
    const auto at_pane_bound = single_tab_snapshot(packed_shape, leaves);
    const auto at_panes = reparse(at_pane_bound);
    AURORA_TEST_CHECK_MSG(at_panes.snapshot.has_value(), at_panes.reason);
    const auto over_pane_shape = chain(128U);  // 129 枚叶子：先撞格数那一界（panes 读在形状之前）
    const auto over_pane_bound = single_tab_snapshot(over_pane_shape, 129U);
    const auto over_panes = reparse(over_pane_bound);
    AURORA_TEST_CHECK_FALSE(over_panes.snapshot.has_value());
    AURORA_TEST_CHECK_EQ(over_panes.reason, rejected(in_tab(".panes")));

    // 深度：链的节点数只有 2L+1，远在节点界内，故这一例只可能由深度那一界判。
    const auto at_depth_shape = chain(kMaxSnapshotShapeDepth);
    const auto at_depth_bound = single_tab_snapshot(at_depth_shape, kMaxSnapshotShapeDepth + 1U);
    const auto at_depth = reparse(at_depth_bound);
    AURORA_TEST_CHECK_MSG(at_depth.snapshot.has_value(), at_depth.reason);
    const auto over_depth_shape = chain(70U);
    const auto over_depth_bound = single_tab_snapshot(over_depth_shape, 71U);
    const auto over_depth = reparse(over_depth_bound);
    AURORA_TEST_CHECK_FALSE(over_depth.snapshot.has_value());
    AURORA_TEST_CHECK_MSG(over_depth.reason.rfind(rejected(in_tab(".shape")), 0) == 0, over_depth.reason);

    // 节点数：满二叉树八层 256 枚叶子（511 个节点），深度却只有 8，故这一例只可能由节点那一界判。
    // 腿取 SSH：非本地标签的 `panes` 恒空，格数对位那道闸碰不到它。
    std::size_t heavy_leaves = 0;
    const auto heavy_shape = full_tree(8U, heavy_leaves);
    AURORA_TEST_CHECK_EQ(heavy_leaves, 256U);
    auto over_node_bound = single_tab_snapshot(heavy_shape, 0U, ConnectionType::Ssh);
    over_node_bound.windows[0].tabs[0].ssh = ssh_profile();
    const auto over_nodes = reparse(over_node_bound);
    AURORA_TEST_CHECK_FALSE(over_nodes.snapshot.has_value());
    AURORA_TEST_CHECK_MSG(over_nodes.reason.rfind(rejected(in_tab(".shape")), 0) == 0, over_nodes.reason);
}

AURORA_TEST_CASE(no_key_in_a_snapshot_document_carries_a_credential_name) {
    const auto text = written(sample_snapshot());
    const auto parsed = parse(text);
    AURORA_TEST_REQUIRE(parsed.has_value());

    std::vector<std::string> keys;
    collect_keys(*parsed, keys);
    for (const auto &key : keys) {
        for (std::string_view banned : {"password", "passphrase", "secret", "token", "private_key"}) {
            AURORA_TEST_CHECK_MSG(key.find(banned) == std::string_view::npos,
                                  key + " ~ " + std::string{banned});
        }
    }
    // 反空转：扫到凭据块本身，才说明那条循环真判过东西（一份不含凭据的文档能让整条腿空跑）。
    AURORA_TEST_REQUIRE(keys.size() > 20U);
    AURORA_TEST_CHECK_MSG(std::find(keys.begin(), keys.end(), std::string{"credential"}) != keys.end(),
                          "no credential block in the scanned document");
}

AURORA_TEST_CASE(a_stand_in_passphrase_never_reaches_the_written_bytes) {
    // 判据 ⑥ 的值侧：口令只进凭据库，档案与快照里连引用都不该含它。替身串，非真凭据。
    const std::string stand_in = "hunter2-stand-in-value";
    InMemoryCredentialStore vault;
    AURORA_TEST_REQUIRE(vault.available());

    auto stored = ssh_profile();
    stored.credential = vault.store("gw", "gateway prompt", stand_in);
    AURORA_TEST_REQUIRE(stored.credential.kind == SecretKind::Reference);
    AURORA_TEST_CHECK_FALSE(stored.credential.is_plaintext());
    // 库后端也不把明文混进句柄：句柄里只有 `mem:<seq>` 那一串引用。
    AURORA_TEST_CHECK_EQ(stored.credential.vault_id.find(stand_in), std::string::npos);
    AURORA_TEST_CHECK_TRUE(vault.retrieve(stored.credential).has_value());

    // 两种凭据形态各走一次往返：Reference 的 `vault_id` 与 `AskEveryTime` 的询问哨兵（`CONN.09` 的降级档）。
    auto asking = ssh_profile();
    asking.credential = SecretHandle::ask_every_time("old", "old prompt");
    auto snapshot = WorkspaceSnapshot{};
    auto window = SnapshotWindow{};
    window.tabs = {SnapshotTab{.kind = ConnectionType::Ssh, .profile_id = "gw", .ssh = stored},
                   SnapshotTab{.kind = ConnectionType::Ssh, .profile_id = "old", .ssh = asking},
                   one_local_tab()};
    window.selected_tab = 2;
    snapshot.windows = {std::move(window)};

    const auto text = written(snapshot);
    AURORA_TEST_CHECK_EQ(text.find(stand_in), std::string::npos);

    const auto decoded = WorkspaceSnapshot::from_json(text);
    AURORA_TEST_CHECK_MSG(decoded.snapshot.has_value(), decoded.reason);
    AURORA_TEST_REQUIRE(decoded.snapshot.has_value());
    AURORA_TEST_CHECK_TRUE(*decoded.snapshot == snapshot);
    AURORA_TEST_CHECK_EQ(written(*decoded.snapshot).find(stand_in), std::string::npos);

    // 反空转：句柄确实带着库给的那枚引用回来（换形式漏字段也会让「不含明文」这条看起来通过）。
    const auto &restored = (*decoded.snapshot).windows[0].tabs[0];
    AURORA_TEST_REQUIRE(restored.ssh.has_value());
    AURORA_TEST_CHECK_EQ(restored.ssh->credential.vault_id, stored.credential.vault_id);
    AURORA_TEST_CHECK_FALSE(restored.ssh->credential.vault_id.empty());
    const auto &sentinel = (*decoded.snapshot).windows[0].tabs[1];
    AURORA_TEST_REQUIRE(sentinel.ssh.has_value());
    AURORA_TEST_CHECK_TRUE(sentinel.ssh->credential.kind == SecretKind::AskEveryTime);
}

AURORA_TEST_CASE(a_non_finite_proportion_is_refused_by_the_writer) {
    auto snapshot = sample_snapshot();
    snapshot.windows[0].tabs[0].shape.children[2].lengths[0] = std::numeric_limits<double>::quiet_NaN();
    AURORA_TEST_CHECK_FALSE(snapshot.to_json().has_value());
    // 反空转：同一份模型把 NaN 换回有限值就写得出去（框架拒写 NaN／Inf，本件不猜它的替代形态）。
    snapshot.windows[0].tabs[0].shape.children[2].lengths[0] = 0.75;
    AURORA_TEST_CHECK(snapshot.to_json().has_value());
}

AURORA_TEST_CASE(the_profile_blocks_are_the_same_json_the_config_store_writes) {
    // 判据 ⑦：本件自写一份档案块（稿 F12 的在册代价），这条等式案就是那处第二真值源的证人。
    // 比的是两边 `dump` 出的字节——键名、次序、枚举文本任一漂了就红。
    Settings settings{};
    auto local = Profile{};
    local.id = "dev-box";
    local.name = "Dev box";
    local.type = ConnectionType::Local;
    local.local = LocalProfile{.command_line = "/bin/zsh",
                               .working_directory = "/home/dev",
                               .environment = {{"LANG", "en_US.UTF-8"}, {"TERM", "screen"}}};
    auto ssh = Profile{};
    ssh.id = "gw";
    ssh.name = "Gateway";
    ssh.type = ConnectionType::Ssh;
    ssh.ssh = ssh_profile();
    auto serial = Profile{};
    serial.id = "console-0";
    serial.name = "Console";
    serial.type = ConnectionType::Serial;
    serial.serial = serial_profile();
    auto telnet = Profile{};
    telnet.id = "sw-2";
    telnet.name = "Switch";
    telnet.type = ConnectionType::Telnet;
    telnet.telnet = telnet_profile();
    settings.profiles = {local, ssh, serial, telnet};

    const auto file = make_path("profile-shape.json");
    Store store{file};
    AURORA_TEST_CHECK_FALSE(store.replace(settings).has_value());
    const auto config_doc = parse(read_file(file));
    AURORA_TEST_REQUIRE(config_doc.has_value());

    auto snapshot_tab = SnapshotTab{};
    snapshot_tab.kind = ConnectionType::Local;
    snapshot_tab.panes = {local_spec_from(local.local)};
    auto ssh_tab = SnapshotTab{};
    ssh_tab.kind = ConnectionType::Ssh;
    ssh_tab.ssh = ssh_profile();
    auto serial_tab = SnapshotTab{};
    serial_tab.kind = ConnectionType::Serial;
    serial_tab.serial = serial_profile();
    auto telnet_tab = SnapshotTab{};
    telnet_tab.kind = ConnectionType::Telnet;
    telnet_tab.telnet = telnet_profile();
    auto window = SnapshotWindow{};
    window.tabs = {std::move(snapshot_tab), std::move(ssh_tab), std::move(serial_tab), std::move(telnet_tab)};
    auto snapshot = WorkspaceSnapshot{};
    snapshot.windows = {std::move(window)};
    const auto snapshot_doc = parse(written(snapshot));
    AURORA_TEST_REQUIRE(snapshot_doc.has_value());

    const Value &items = field(*config_doc, {"profiles", "items"});
    AURORA_TEST_REQUIRE_EQ(items.size(), 4U);
    const Value &tabs = field(element(field(*snapshot_doc, {"windows"}), 0U), {"tabs"});
    AURORA_TEST_REQUIRE_EQ(tabs.size(), 4U);

    // 本地腿那一块：档案表的 `local` 块 vs 快照里那一格的有效料（同三件、同次序）。
    const Value &local_store = field(element(items, 0U), {"local"});
    const Value &local_snapshot = element(field(element(tabs, 0U), {"panes"}), 0U);
    AURORA_TEST_REQUIRE(local_store.size() >= 3U);  // 反空转：两块都不是空对象，等式才不是空对空
    AURORA_TEST_CHECK_EQ(bytes_of(local_snapshot), bytes_of(local_store));

    const std::vector<std::pair<std::string_view, std::size_t>> blocks{
        {std::string_view{"ssh"}, 1U}, {std::string_view{"serial"}, 2U}, {std::string_view{"telnet"}, 3U}};
    for (const auto &[key, index] : blocks) {
        const Value &store_side = field(element(items, index), {key});
        const Value &snapshot_side = field(element(tabs, index), {key});
        AURORA_TEST_REQUIRE(store_side.is_object());
        AURORA_TEST_REQUIRE(snapshot_side.is_object());
        AURORA_TEST_REQUIRE(store_side.size() >= 2U);
        AURORA_TEST_CHECK_MSG(bytes_of(snapshot_side) == bytes_of(store_side),
                              std::string{key} + " block differs: " + bytes_of(store_side) + " vs " +
                                  bytes_of(snapshot_side));
    }
}

}  // namespace borealis::test_cases::utest_workspace_snapshot
