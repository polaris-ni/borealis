/// 测试类型: unit
/// 目标单元: include/borealis/cli/startup.h + src/cli/startup.cpp
/// 测试说明: `SPEC.FEAT.INTEG.04` 的纯逻辑面——语法解析（缺省 new-tab、两个子命令互斥、
///           选项顺序无关、缺值 / 重复 / 未知 / 位置参数拒绝、--help 本地消费、值上限与 NUL）、
///           档案解析（id → 精确名 → 唯一大小写不敏感名；重名不取第一条）、以及切分与 `--cwd`
///           的策略档位。本套件不碰文件系统、不建窗。

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/cli/startup.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_cli_startup {

namespace {

using borealis::cli::parse_startup_args;
using borealis::cli::resolve_profile;
using borealis::cli::StartupAction;
using borealis::cli::StartupParse;
using borealis::cli::StartupRequest;
using borealis::cli::startup_policy_error;
using borealis::cli::startup_usage;
using borealis::conn::ConnectionType;

auto parse(std::initializer_list<std::string> args) -> StartupParse {
    const std::vector<std::string> storage{args};
    return parse_startup_args(storage);
}

[[nodiscard]] auto rejects(std::initializer_list<std::string> args) -> bool {
    const StartupParse parsed = parse(args);
    return !parsed.request.has_value() && !parsed.help && !parsed.error.empty();
}

[[nodiscard]] auto make_profile(std::string id, std::string name, ConnectionType type)
    -> borealis::conn::Profile {
    borealis::conn::Profile profile;
    profile.id = std::move(id);
    profile.name = std::move(name);
    profile.type = type;
    return profile;
}

}  // namespace

AURORA_TEST_CASE(default_invocation_is_new_tab_with_empty_fields) {
    const StartupParse parsed = parse({});
    AURORA_TEST_REQUIRE_TRUE(parsed.request.has_value());
    AURORA_TEST_CHECK(parsed.request->action == StartupAction::NewTab);
    AURORA_TEST_CHECK(parsed.request->profile.empty());
    AURORA_TEST_CHECK(parsed.request->cwd.empty());
    AURORA_TEST_CHECK(!parsed.request->new_window);
    AURORA_TEST_CHECK(parsed.error.empty());
    AURORA_TEST_CHECK(!parsed.help);
}

AURORA_TEST_CASE(subcommands_select_action_and_are_mutually_exclusive) {
    const StartupParse tab = parse({"new-tab"});
    AURORA_TEST_REQUIRE_TRUE(tab.request.has_value());
    AURORA_TEST_CHECK(tab.request->action == StartupAction::NewTab);

    const StartupParse split = parse({"split-pane"});
    AURORA_TEST_REQUIRE_TRUE(split.request.has_value());
    AURORA_TEST_CHECK(split.request->action == StartupAction::SplitPane);

    AURORA_TEST_CHECK(rejects({"new-tab", "split-pane"}));
    AURORA_TEST_CHECK(rejects({"split-pane", "new-tab"}));
    AURORA_TEST_CHECK(rejects({"new-tab", "new-tab"}));
}

AURORA_TEST_CASE(options_parse_in_any_order) {
    const StartupParse parsed =
        parse({"--profile", "dev", "--new-window", "--cwd", "/tmp/work", "split-pane"});
    AURORA_TEST_REQUIRE_TRUE(parsed.request.has_value());
    AURORA_TEST_CHECK(parsed.request->action == StartupAction::SplitPane);
    AURORA_TEST_CHECK(parsed.request->profile == "dev");
    AURORA_TEST_CHECK(parsed.request->cwd == "/tmp/work");
    AURORA_TEST_CHECK(parsed.request->new_window);

    const StartupParse reordered =
        parse({"split-pane", "--cwd", "/tmp/work", "--new-window", "--profile", "dev"});
    AURORA_TEST_REQUIRE_TRUE(reordered.request.has_value());
    AURORA_TEST_CHECK(*reordered.request == *parsed.request);
}

AURORA_TEST_CASE(missing_duplicate_unknown_and_positional_arguments_are_rejected) {
    AURORA_TEST_CHECK(rejects({"--profile"}));
    AURORA_TEST_CHECK(rejects({"--cwd"}));
    AURORA_TEST_CHECK(rejects({"--profile", ""}));
    AURORA_TEST_CHECK(rejects({"--cwd", ""}));
    AURORA_TEST_CHECK(rejects({"--profile", "a", "--profile", "b"}));
    AURORA_TEST_CHECK(rejects({"--cwd", "/a", "--cwd", "/b"}));
    AURORA_TEST_CHECK(rejects({"--unknown"}));
    AURORA_TEST_CHECK(rejects({"-p"}));
    AURORA_TEST_CHECK(rejects({"unexpected"}));
}

AURORA_TEST_CASE(help_wins_and_never_produces_a_request) {
    const StartupParse parsed = parse({"--help"});
    AURORA_TEST_CHECK(parsed.help);
    AURORA_TEST_CHECK(!parsed.request.has_value());
    AURORA_TEST_CHECK(parsed.error.empty());

    // 用法在本地消费：与后面那枚坏参数无关，仍按 help 收口。
    const StartupParse with_noise = parse({"--profile", "dev", "--help", "--unknown"});
    AURORA_TEST_CHECK(with_noise.help);
    AURORA_TEST_CHECK(!with_noise.request.has_value());
    AURORA_TEST_CHECK(with_noise.error.empty());
}

AURORA_TEST_CASE(value_cap_nul_and_empty_values_are_rejected) {
    const std::string at_cap(borealis::cli::kMaxStartupValueBytes, 'a');
    const StartupParse accepted = parse({"--profile", at_cap});
    AURORA_TEST_REQUIRE_TRUE(accepted.request.has_value());
    AURORA_TEST_CHECK(accepted.request->profile.size() == at_cap.size());

    AURORA_TEST_CHECK(rejects({"--profile", at_cap + "a"}));
    const std::string with_nul{"a\0b", 3};
    AURORA_TEST_CHECK(rejects({"--profile", with_nul}));
}

AURORA_TEST_CASE(profiles_resolve_by_id_exact_name_and_unique_folded_name) {
    std::vector<borealis::conn::Profile> profiles;
    profiles.push_back(make_profile("p1", "Dev", ConnectionType::Local));
    profiles.push_back(make_profile("p2", "prod", ConnectionType::Ssh));

    const auto by_id = resolve_profile(profiles, "p1");
    AURORA_TEST_REQUIRE_TRUE(by_id.profile != nullptr);
    AURORA_TEST_CHECK(by_id.profile->id == "p1");
    AURORA_TEST_CHECK(by_id.error.empty());

    const auto by_name = resolve_profile(profiles, "Dev");
    AURORA_TEST_REQUIRE_TRUE(by_name.profile != nullptr);
    AURORA_TEST_CHECK(by_name.profile->id == "p1");

    const auto by_folded = resolve_profile(profiles, "PROD");
    AURORA_TEST_REQUIRE_TRUE(by_folded.profile != nullptr);
    AURORA_TEST_CHECK(by_folded.profile->id == "p2");

    const auto missing = resolve_profile(profiles, "lab");
    AURORA_TEST_CHECK(missing.profile == nullptr);
    AURORA_TEST_CHECK(missing.error.find("not found") != std::string::npos);
}

AURORA_TEST_CASE(duplicate_names_are_ambiguous_not_first_wins) {
    std::vector<borealis::conn::Profile> profiles;
    profiles.push_back(make_profile("p1", "Dev", ConnectionType::Local));
    profiles.push_back(make_profile("p2", "prod", ConnectionType::Ssh));
    profiles.push_back(make_profile("p3", "DEV", ConnectionType::Serial));

    // 精确名仍唯一定位；大小写折叠后两条命中必须拒绝，不能取登记次序里的第一条。
    const auto exact = resolve_profile(profiles, "Dev");
    AURORA_TEST_REQUIRE_TRUE(exact.profile != nullptr);
    AURORA_TEST_CHECK(exact.profile->id == "p1");

    const auto folded = resolve_profile(profiles, "dev");
    AURORA_TEST_CHECK(folded.profile == nullptr);
    AURORA_TEST_CHECK(folded.error.find("ambiguous") != std::string::npos);

    const auto exact_upper = resolve_profile(profiles, "DEV");
    AURORA_TEST_REQUIRE_TRUE(exact_upper.profile != nullptr);
    AURORA_TEST_CHECK(exact_upper.profile->id == "p3");
}

AURORA_TEST_CASE(duplicate_ids_are_ambiguous) {
    std::vector<borealis::conn::Profile> profiles;
    profiles.push_back(make_profile("same", "One", ConnectionType::Local));
    profiles.push_back(make_profile("same", "Two", ConnectionType::Local));

    const auto resolved = resolve_profile(profiles, "same");
    AURORA_TEST_CHECK(resolved.profile == nullptr);
    AURORA_TEST_CHECK(resolved.error.find("ambiguous") != std::string::npos);
}

AURORA_TEST_CASE(id_match_wins_over_another_profiles_display_name) {
    std::vector<borealis::conn::Profile> profiles;
    profiles.push_back(make_profile("shared", "First", ConnectionType::Local));
    profiles.push_back(make_profile("p2", "shared", ConnectionType::Ssh));

    const auto resolved = resolve_profile(profiles, "shared");
    AURORA_TEST_REQUIRE_TRUE(resolved.profile != nullptr);
    AURORA_TEST_CHECK(resolved.profile->id == "shared");
    AURORA_TEST_CHECK(resolved.profile->name == "First");
}

AURORA_TEST_CASE(policy_lets_local_split_through_and_refuses_nonlocal_profile) {
    constexpr std::string_view kSplitProfileRefusal =
        "split-pane --profile needs a per-pane credential prompt";
    const std::array<ConnectionType, 3> kRemoteTypes{ConnectionType::Ssh, ConnectionType::Serial,
                                                     ConnectionType::Telnet};

    StartupRequest request;
    request.action = StartupAction::SplitPane;
    // 每 pane 一会话落地后（`SPEC.FEAT.WS.02` 稿 §7）裸切分与 `--cwd` 切分都放开：「当前标签是不是本地
    // 腿」是运行期事实，由装配层判，纯逻辑件不看。
    AURORA_TEST_CHECK(startup_policy_error(request, std::nullopt).empty());
    request.cwd = "/tmp/work";
    AURORA_TEST_CHECK(startup_policy_error(request, std::nullopt).empty());
    AURORA_TEST_CHECK(startup_policy_error(request, ConnectionType::Local).empty());

    // Local 档案切分放开；非 Local 维持拒绝，且拒绝语说的是此刻真实成立的那一条。
    request.profile = "dev";
    AURORA_TEST_CHECK(startup_policy_error(request, ConnectionType::Local).empty());
    for (const ConnectionType remote : kRemoteTypes) {
        AURORA_TEST_CHECK(startup_policy_error(request, remote) == kSplitProfileRefusal);
    }
    // 类型未知＝调用方没解析过 `--profile`，按非本地处置（宁可拒错，不能让新格等同步工厂给不出的材料）。
    AURORA_TEST_CHECK(startup_policy_error(request, std::nullopt) == kSplitProfileRefusal);

    // `--cwd` 只对本地语义生效那一条不变，且与动作无关。
    request.profile.clear();
    for (const StartupAction action : {StartupAction::SplitPane, StartupAction::NewTab}) {
        request.action = action;
        AURORA_TEST_CHECK(startup_policy_error(request, std::nullopt).empty());
        AURORA_TEST_CHECK(startup_policy_error(request, ConnectionType::Local).empty());
        for (const ConnectionType remote : kRemoteTypes) {
            AURORA_TEST_CHECK(!startup_policy_error(request, remote).empty());
        }
    }
    // 没有 `--cwd` 时新开远端标签本就是正路；那条切分拒绝也只跟着 `split-pane` 走。
    request.cwd.clear();
    request.profile = "dev";
    for (const ConnectionType remote : kRemoteTypes) {
        AURORA_TEST_CHECK(startup_policy_error(request, remote).empty());
    }
    request.action = StartupAction::SplitPane;
    AURORA_TEST_CHECK(startup_policy_error(request, ConnectionType::Ssh) == kSplitProfileRefusal);
}

AURORA_TEST_CASE(usage_is_ascii_and_names_both_subcommands) {
    const std::string_view usage = startup_usage();
    AURORA_TEST_CHECK(usage.find("Usage:") != std::string_view::npos);
    AURORA_TEST_CHECK(usage.find("new-tab") != std::string_view::npos);
    AURORA_TEST_CHECK(usage.find("split-pane") != std::string_view::npos);
    for (const char ch : usage) {
        const auto byte = static_cast<unsigned char>(ch);
        AURORA_TEST_CHECK(byte < 0x80U);
    }
}

}  // namespace borealis::test_cases::utest_cli_startup
