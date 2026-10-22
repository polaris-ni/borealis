/// 测试类型: unit
/// 目标单元: 配置目录的三枚 `Preferences`（`borealis.json` / `windows.json` / `workspace.json`）
/// 测试说明: `SPEC.FEAT.CONN.09` 的「配置目录 grep 无明文凭据」自动化腿（稿 D9／D10）。
///           三枚落盘件同目录生成后，逐文件扫两件事：①禁列键名（password／passphrase／secret／
///           token／private_key）；②明文哨兵值。②是 F8 的缺口——旧审计只查键名，一份把口令塞进
///           `label` 或 `vault_id` 的坏文件照样过闸。
///           自证段先种一份键名泄漏与一份值泄漏，证明两条扫描臂各自抓得到，再删掉它们证明生产
///           产物干净——否则「扫不到」可能只是扫描器扫了空集合（AGENTS.md §4.4 第 22 条）。
///           全部无头；只碰临时目录里的 JSON 与框架的 `.lock` 件。

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/app/window_geometry.h"
#include "aurora/core/json.h"

#include "borealis/config/settings.h"
#include "borealis/config/store.h"
#include "borealis/conn/profile.h"
#include "borealis/ui/pane_tree.h"
#include "borealis/ui/workspace_snapshot.h"
#include "config/window_geometry_store.h"
#include "config/workspace_store.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_credential_audit {

namespace au = aurora;
namespace fs = std::filesystem;

using borealis::config::kConfigFileName;
using borealis::config::kWindowGeometryFileName;
using borealis::config::kWorkspaceFileName;
using borealis::config::kWorkspaceLayoutKey;
using borealis::config::Settings;
using borealis::config::Store;
using borealis::config::WindowGeometryStore;
using borealis::config::WorkspaceStore;
using borealis::conn::ConnectionType;
using borealis::conn::InMemoryCredentialStore;
using borealis::conn::Profile;
using borealis::conn::SecretHandle;
using borealis::ui::PaneShape;
using borealis::ui::SnapshotTab;
using borealis::ui::SnapshotWindow;
using borealis::ui::WorkspaceSnapshot;

namespace {

/// @brief 两枚明文哨兵：口令与 passphrase 各一，形态足够像真凭据而不会撞上任何合法文本。
constexpr std::string_view kPasswordSentinel = "AURORA-AUDIT-PASSWORD-8f31c2";
constexpr std::string_view kPassphraseSentinel = "AURORA-AUDIT-PASSPHRASE-4b7d9e";

/// @brief 禁列键名。与 `store.cpp` 的导入闸各自维护：审计是独立口径，不拿被测件的名单自证。
constexpr std::string_view kBannedKeyNames[] = {"password", "passphrase", "secret", "token", "private_key"};

/// @brief 用例自己的独占目录。
[[nodiscard]] auto fixture_dir(std::string_view name) -> fs::path {
    const auto path = fs::path{aurora::testing::isolation::temp_dir()} / std::string{name};
    std::error_code ec;
    fs::create_directories(path, ec);
    return path;
}

[[nodiscard]] auto read_file(const fs::path &path) -> std::string {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

auto write_file(const fs::path &path, std::string_view text) -> void {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
}

/// @brief 目录里的全部常规文件（框架的 `.lock` 件也在内，扫描时按名豁免）。
[[nodiscard]] auto directory_files(const fs::path &dir) -> std::vector<fs::path> {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto &item : fs::directory_iterator{dir, ec}) {
        if (item.is_regular_file()) {
            out.emplace_back(item.path());
        }
    }
    std::ranges::sort(out);
    return out;
}

auto collect_keys(const au::json::Value &node, std::vector<std::string> &out) -> void {
    if (node.is_object()) {
        for (const auto &entry : node.entries()) {
            out.emplace_back(entry.key);
            collect_keys(entry.value, out);
        }
        return;
    }
    if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            if (const au::json::Value *element = node.at(index); element != nullptr) {
                collect_keys(*element, out);
            }
        }
    }
}

/// @brief 一次目录审计：扫过的 JSON 文件数与全部违规（违规为空才是干净）。
struct AuditReport {
    std::size_t scanned{};
    std::vector<std::string> violations;
};

/// @brief 逐文件扫禁列键名与明文哨兵值。
///
/// 非 JSON 只允许 `.lock` 形态：框架的跨进程 advisory 锁件是唯一豁免；自家产物不再是 JSON
/// 必须报违规，否则解析失败会被读成「没什么可扫的」。
[[nodiscard]] auto audit_directory(const fs::path &dir) -> AuditReport {
    AuditReport report;
    for (const auto &entry : directory_files(dir)) {
        const auto name = entry.filename().string();
        const auto text = read_file(entry);
        for (const std::string_view sentinel : {kPasswordSentinel, kPassphraseSentinel}) {
            if (text.find(sentinel) != std::string::npos) {
                report.violations.push_back(name + ": plaintext sentinel " + std::string{sentinel});
            }
        }
        const auto parsed = au::json::parse(text);
        if (!parsed.ok()) {
            if (!name.ends_with(".lock")) {
                report.violations.push_back(name + ": not JSON (only .lock files may be non-JSON)");
            }
            continue;
        }
        ++report.scanned;
        std::vector<std::string> keys;
        collect_keys(parsed.value(), keys);
        for (const auto &key : keys) {
            for (const std::string_view banned : kBannedKeyNames) {
                if (key.find(banned) != std::string::npos) {
                    report.violations.push_back(name + ": key " + key + " contains " + std::string{banned});
                }
            }
        }
    }
    return report;
}

[[nodiscard]] auto joined(const std::vector<std::string> &lines) -> std::string {
    std::string out;
    for (const auto &line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

[[nodiscard]] auto mentions(const std::vector<std::string> &lines, std::string_view needle) -> bool {
    return std::ranges::any_of(lines, [needle](const std::string &line) {
        return line.find(needle) != std::string::npos;
    });
}

/// @brief 一份 SSH 档案：`credential` 由调用方给（Reference 或降级哨兵）。
[[nodiscard]] auto ssh_profile(std::string id, SecretHandle credential) -> Profile {
    auto profile = Profile{};
    profile.id = std::move(id);
    profile.name = profile.id;
    profile.type = ConnectionType::Ssh;
    profile.ssh.host = "audit.example.invalid";
    profile.ssh.port = 2222;
    profile.ssh.user = "operator";
    profile.ssh.auth_method = "password";
    profile.ssh.credential = std::move(credential);
    return profile;
}

/// @brief 生产形态的一组产物：秘密留在库里，档案里只留句柄。
struct Artifacts {
    InMemoryCredentialStore vault;
    SecretHandle password_handle;
};

/// @brief 按生产形态生成三枚 `Preferences` 的全部写入产物。
///
/// `borealis.json` 走真 `Store`：两次提交（第二笔落一份快照）→ 回滚一笔（再落一份快照）→
/// 导出一份；`windows.json` 走几何件；`workspace.json` 走布局件且带一份含 SSH 块（及其句柄）的快照。
/// 返回秘密库与口令句柄：句柄已写进档案，故扫描覆盖到「带凭据字段的文档」而不是空 schema。
[[nodiscard]] auto build_production_artifacts(const fs::path &dir) -> Artifacts {
    InMemoryCredentialStore vault;
    const auto password_handle = vault.store("audit-ssh", "audit password", std::string{kPasswordSentinel});
    const auto passphrase_handle =
        vault.store("audit-key", "audit passphrase", std::string{kPassphraseSentinel});

    auto settings = Settings{};
    settings.appearance.theme = "nord";
    settings.profiles.push_back(ssh_profile("audit-ssh", password_handle));
    settings.profiles.push_back(ssh_profile("audit-key", passphrase_handle));
    settings.profiles.push_back(
        ssh_profile("audit-ask", SecretHandle::ask_every_time("audit-ask", "audit prompt")));

    Store store{dir / kConfigFileName};
    AURORA_TEST_REQUIRE_MSG(!store.replace(settings).has_value(), "first replace failed");
    AURORA_TEST_REQUIRE_MSG(!store.replace(settings).has_value(), "second replace failed");
    AURORA_TEST_REQUIRE(!store.snapshots().empty());
    AURORA_TEST_REQUIRE_MSG(!store.rollback_to(store.snapshots()[0].path).has_value(), "rollback failed");
    AURORA_TEST_REQUIRE_MSG(!store.export_settings(dir / "export.json").has_value(), "export failed");

    WindowGeometryStore geometry{dir / kWindowGeometryFileName};
    auto geometry_value = au::WindowGeometry{};
    geometry_value.origin = au::Point{.x = 120.0F, .y = 80.0F};
    geometry_value.size = au::Size{.width = 1280.0F, .height = 720.0F};
    geometry_value.mode = au::WindowMode::Normal;
    geometry_value.display_id = 0;
    au::save_window_geometry(*geometry.preferences(), borealis::config::window_persist_id(0U), geometry_value);
    AURORA_TEST_REQUIRE_MSG(!geometry.flush().has_value(), "windows.json flush failed");

    auto tab = SnapshotTab{};
    tab.kind = ConnectionType::Ssh;
    tab.profile_id = "audit-ssh";
    tab.ssh = settings.profiles.front().ssh;
    tab.shape = PaneShape{};  // 单叶：未分屏的那一档。
    auto window = SnapshotWindow{};
    window.tabs = {tab};
    window.selected_tab = 0;
    auto snapshot = WorkspaceSnapshot{};
    snapshot.windows = {window};
    const auto layout_text = snapshot.to_json();
    AURORA_TEST_REQUIRE_MSG(layout_text.has_value(), "snapshot writer produced no document");
    const auto layout = au::json::parse(*layout_text);
    AURORA_TEST_REQUIRE_MSG(layout.ok(), "snapshot writer produced an unparseable document");

    WorkspaceStore workspace{dir / kWorkspaceFileName};
    workspace.preferences()->set(std::string{kWorkspaceLayoutKey}, layout.value());
    AURORA_TEST_REQUIRE_MSG(!workspace.flush().has_value(), "workspace.json flush failed");

    return Artifacts{.vault = std::move(vault), .password_handle = password_handle};
}

}  // namespace

AURORA_TEST_CASE(the_three_preferences_leave_no_plaintext_credential_in_the_config_directory) {
    const auto dir = fixture_dir("config_dir");
    const auto artifacts = build_production_artifacts(dir);

    const auto report = audit_directory(dir);
    AURORA_TEST_CHECK_MSG(report.violations.empty(), joined(report.violations));
    // 当前文件 + 两份快照 + 导出件 + windows.json + workspace.json：集合非空才说明上面真判过东西。
    AURORA_TEST_REQUIRE_MSG(report.scanned >= 6U, "scanned=" + std::to_string(report.scanned));

    // 生产产物确实带着凭据句柄：否则「扫不到明文」可能只是三份文档里压根没有凭据字段。
    const auto settings_text = read_file(dir / kConfigFileName);
    AURORA_TEST_CHECK_MSG(settings_text.find(artifacts.password_handle.vault_id) != std::string::npos,
                          settings_text);
    const auto workspace_text = read_file(dir / kWorkspaceFileName);
    AURORA_TEST_CHECK_MSG(workspace_text.find(artifacts.password_handle.vault_id) != std::string::npos,
                          workspace_text);
    // 而库里那两枚明文一枚都不在盘上（值扫描的正面判据；键名臂由下面一条自证用例守）。
    AURORA_TEST_CHECK(settings_text.find(kPasswordSentinel) == std::string::npos);
    AURORA_TEST_CHECK(settings_text.find(kPassphraseSentinel) == std::string::npos);
}

AURORA_TEST_CASE(the_auditor_catches_a_seeded_key_and_value_leak) {
    const auto dir = fixture_dir("seeded");

    // 先证明生产产物是干净的（与上一条同方向的基线）。
    const auto artifacts = build_production_artifacts(dir);
    AURORA_TEST_CHECK_MSG(audit_directory(dir).violations.empty(), joined(audit_directory(dir).violations));

    // 种一份键名泄漏与一份值泄漏，各打一条扫描臂：同一份坏文件里混着两条只会证明「至少抓到一个」。
    const auto key_leak = dir / "leak-key.json";
    const auto value_leak = dir / "leak-value.json";
    write_file(key_leak, R"({"profiles":[{"password":"not-a-real-secret"}]})");
    write_file(value_leak, R"({"note":")" + std::string{kPasswordSentinel} + R"("})");

    const auto caught = audit_directory(dir);
    AURORA_TEST_CHECK_MSG(mentions(caught.violations, "password"), joined(caught.violations));
    AURORA_TEST_CHECK_MSG(mentions(caught.violations, kPasswordSentinel), joined(caught.violations));
    AURORA_TEST_CHECK_MSG(mentions(caught.violations, "leak-key.json"), joined(caught.violations));
    AURORA_TEST_CHECK_MSG(mentions(caught.violations, "leak-value.json"), joined(caught.violations));

    // 删掉两份泄漏后回到干净：证明上面的「抓到」不是扫描器对任何文件都报违规。
    std::error_code ec;
    fs::remove(key_leak, ec);
    fs::remove(value_leak, ec);
    const auto clean = audit_directory(dir);
    AURORA_TEST_CHECK_MSG(clean.violations.empty(), joined(clean.violations));
    AURORA_TEST_CHECK(clean.scanned >= 6U);
    (void)artifacts;
}

}  // namespace borealis::test_cases::utest_credential_audit
