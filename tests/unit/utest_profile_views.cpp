/// 测试类型: unit
/// 目标单元: src/conn/profile_views.h（私有头）纯逻辑视图模型
/// 测试说明: 守住 CONN.07 侧栏三视图的排序/过滤确定性、向导草稿校验矩阵、
///           凭据询问计划映射。UI 只画算好的行（设计稿 D7），查询即在此层。

#include "conn/profile_views.h"

#include <string>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_profile_views {

namespace {

[[nodiscard]] auto make_profile(const std::string &id, const std::string &name,
                                conn::ConnectionType type = conn::ConnectionType::Ssh)
    -> conn::Profile {
    auto profile = conn::Profile{};
    profile.id = id;
    profile.name = name;
    profile.type = type;
    if (type == conn::ConnectionType::Ssh) {
        profile.ssh.host = "h.example.com";
    }
    return profile;
}

}  // namespace

AURORA_TEST_CASE(sidebar_rows_sort_by_name_and_filter_by_mode) {
    using conn::SidebarViewMode;

    auto store = conn::ProfileStore{};
    auto zeta = make_profile("p-zeta", "zeta");
    zeta.favorite = true;
    auto alpha = make_profile("p-alpha", "alpha");
    auto mid = make_profile("p-mid", "mid");
    auto local = make_profile("p-local", "build box", conn::ConnectionType::Local);
    store.add(std::move(zeta));
    store.add(std::move(alpha));
    store.add(std::move(mid));
    store.add(std::move(local));

    // 全部视图：名字字典序。
    auto rows = conn::sidebar_rows(store, SidebarViewMode::All, "");
    AURORA_TEST_REQUIRE(rows.size() == 4U);
    AURORA_TEST_CHECK_TRUE(rows[0].name == "alpha");
    AURORA_TEST_CHECK_TRUE(rows[3].name == "zeta");

    // 收藏视图：只留收藏（仍按名字序）。
    rows = conn::sidebar_rows(store, SidebarViewMode::Favorites, "");
    AURORA_TEST_REQUIRE(rows.size() == 1U);
    AURORA_TEST_CHECK_TRUE(rows[0].id == "p-zeta");

    // 搜索：走模型层匹配（大小写不敏感子串），与视图模式正交。
    rows = conn::sidebar_rows(store, SidebarViewMode::All, "ALPH");
    AURORA_TEST_REQUIRE(rows.size() == 1U);
    AURORA_TEST_CHECK_TRUE(rows[0].id == "p-alpha");
}

AURORA_TEST_CASE(sidebar_rows_group_view_orders_by_group_then_name) {
    auto store = conn::ProfileStore{};
    auto a = make_profile("p-a", "alpha");
    a.groups = {"prod"};
    auto b = make_profile("p-b", "bravo");
    b.groups = {"dev"};
    auto c = make_profile("p-c", "charlie");
    c.groups = {"dev"};
    auto d = make_profile("p-d", "delta");  // 无分组 → 殿后
    store.add(std::move(a));
    store.add(std::move(b));
    store.add(std::move(c));
    store.add(std::move(d));

    const auto rows = conn::sidebar_rows(store, conn::SidebarViewMode::Groups, "");
    AURORA_TEST_REQUIRE(rows.size() == 4U);
    AURORA_TEST_CHECK_TRUE(rows[0].group_label == "dev" && rows[0].name == "bravo");
    AURORA_TEST_CHECK_TRUE(rows[1].group_label == "dev" && rows[1].name == "charlie");
    AURORA_TEST_CHECK_TRUE(rows[2].group_label == "prod" && rows[2].name == "alpha");
    AURORA_TEST_CHECK_TRUE(rows[3].group_label.empty() && rows[3].name == "delta");
}

AURORA_TEST_CASE(validate_draft_reports_first_issue_in_order) {
    using conn::ConnectionType;
    using conn::DraftIssue;
    using conn::ProfileDraft;

    auto draft = ProfileDraft{};
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::EmptyName);

    draft.name = "跳板机";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);  // Local 型到此通过

    draft.type = ConnectionType::Ssh;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::EmptyHost);

    draft.host = "gw.example.com";
    draft.port = 0;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::PortOutOfRange);
    draft.port = 70000;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::PortOutOfRange);
    draft.port = 2222;
    draft.auth_method = "gssapi";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::UnknownAuthMethod);
    draft.auth_method = "privatekey";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);
}

AURORA_TEST_CASE(draft_round_trips_through_profile_and_back) {
    using conn::ConnectionType;

    auto draft = conn::ProfileDraft{};
    draft.name = "生产跳板";
    draft.type = ConnectionType::Ssh;
    draft.host = "gw.example.com";
    draft.port = 2222;
    draft.user = "deploy";
    draft.auth_method = "password";
    draft.identity_file = "~/.ssh/id_ed25519";
    draft.agent_forwarding = true;
    draft.known_hosts_policy = conn::KnownHostsPolicy::Yes;

    const auto profile = conn::draft_to_profile(draft, "p-1");
    AURORA_TEST_CHECK_TRUE(profile.id == "p-1");
    AURORA_TEST_CHECK_TRUE(profile.name == "生产跳板");
    AURORA_TEST_CHECK_TRUE(profile.ssh.port == 2222);
    AURORA_TEST_CHECK_TRUE(profile.ssh.agent_forwarding);
    AURORA_TEST_CHECK_TRUE(profile.ssh.known_hosts_policy == conn::KnownHostsPolicy::Yes);

    // 回填：编辑向导拿到与草稿等价的字段集。
    const auto back = conn::profile_to_draft(profile);
    AURORA_TEST_CHECK_TRUE(back.host == draft.host);
    AURORA_TEST_CHECK_TRUE(back.auth_method == draft.auth_method);
    AURORA_TEST_CHECK_TRUE(back.known_hosts_policy == draft.known_hosts_policy);

    // Local 型只搬 local 字段，ssh 段保持缺省。
    auto local_draft = conn::ProfileDraft{};
    local_draft.name = "本机";
    local_draft.command_line = "/bin/zsh";
    const auto local_profile = conn::draft_to_profile(local_draft, "p-2");
    AURORA_TEST_CHECK_TRUE(local_profile.local.command_line == "/bin/zsh");
    AURORA_TEST_CHECK_TRUE(local_profile.ssh.host.empty());
}

AURORA_TEST_CASE(secret_ask_plan_follows_auth_method) {
    using conn::SecretAsk;
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("password") == SecretAsk::Password);
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("privatekey") == SecretAsk::Passphrase);
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("keyboard-interactive") == SecretAsk::Interactive);
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("agent") == SecretAsk::None);
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("") == SecretAsk::None);
}

}  // namespace borealis::test_cases::utest_profile_views
