/// 测试类型: unit
/// 目标单元: src/conn/profile_views.h（私有头）纯逻辑视图模型
/// 测试说明: 守住 CONN.07 侧栏三视图的排序/过滤确定性、向导草稿校验矩阵（含 CONN.05 的
///           串口四档）、三向分派的字段不串台、凭据询问计划映射，以及 CONN.03 的跳板折算
///           （一跳折成临时档案）、逐跳凭据计划与向导那一行的「坏链禁保存／好链折档／回填
///           规范化串形」。UI 只画算好的行
///           （设计稿 D7），查询即在此层。

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

AURORA_TEST_CASE(proxy_jump_line_refuses_a_bad_chain_and_folds_a_good_one) {
    using conn::ConnectionType;
    using conn::DraftIssue;

    auto draft = conn::ProfileDraft{};
    draft.name = "prod box";
    draft.type = ConnectionType::Ssh;
    draft.host = "gw.example.com";
    draft.port = 2222;
    draft.auth_method = "agent";

    // 空串＝直连（用户没写这一行时的既定事实），不是坏值——否则每张既有 SSH 档案都存不下去。
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);

    // `none` 是显式关闭写法，同样折成空链路（稿 D3 与 OpenSSH 同口径）。
    draft.proxy_jumps_text = "none";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);
    AURORA_TEST_CHECK_TRUE(conn::draft_to_profile(draft, "p-jump").ssh.proxy_jumps.empty());

    // 坏值整串拒（稿 D9）：部分接受＝替用户存下一条他没写过的链路，与 D6「丢了跳板＝绕过它」
    // 同一条理由，故这里禁止保存而不是留好跳。
    draft.proxy_jumps_text = "jump1.example.com:99999";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::BadProxyJump);

    // 顺序即上表：一处坏值不得吃掉前一档的报错——两处都坏时先报认证方式。
    draft.auth_method = "gssapi";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::UnknownAuthMethod);
    draft.auth_method = "agent";

    // 好值折进档案：端口留空折 22、用户名留空跟随缺省（解析只在此处发生一次）。
    draft.proxy_jumps_text = "ops@jump1.example.com:2222, jump2.example.com ";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);
    const auto profile = conn::draft_to_profile(draft, "p-jump");
    AURORA_TEST_REQUIRE(profile.ssh.proxy_jumps.size() == 2U);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[0].user == "ops");
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[0].host == "jump1.example.com");
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[0].port == 2222);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[1].host == "jump2.example.com");
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[1].port == 22);
    AURORA_TEST_CHECK_TRUE(profile.ssh.proxy_jumps[1].user.empty());

    // 超上限（稿 D2）：9 跳是坏值而不是「取前 8」——静默截断同样是造了一条别的链路。
    auto nine = std::string{};
    for (int i = 1; i <= 9; ++i) {
        if (i > 1) {
            nine += ",";
        }
        nine += "j" + std::to_string(i) + ".example.com";
    }
    draft.proxy_jumps_text = nine;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::BadProxyJump);

    // Telnet 型不读这一行（字段集里没有它，串形须保持空）：问题码不越型串台。
    auto telnet = conn::ProfileDraft{};
    telnet.name = "telnet box";
    telnet.type = ConnectionType::Telnet;
    telnet.host = "tn.example.com";
    telnet.proxy_jumps_text = "jump1.example.com:99999";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(telnet) == DraftIssue::None);
    AURORA_TEST_CHECK_TRUE(conn::draft_to_profile(telnet, "p-tn").ssh.proxy_jumps.empty());
}

AURORA_TEST_CASE(editing_a_profile_with_jumps_backfills_the_canonical_line) {
    auto profile = make_profile("p-edit", "via two jumps");
    auto first = conn::SshHop{};
    first.host = "jump1.example.com";
    first.port = 2222;
    first.user = "ops";
    auto second = conn::SshHop{};
    second.host = "jump2.example.com";  // 端口跟随缺省 22
    profile.ssh.proxy_jumps = {first, second};

    const auto draft = conn::profile_to_draft(profile);
    // 回填的是规范化串形（总是写端口）：编辑一次不该把档案里的 `:22` 「改没」。
    AURORA_TEST_CHECK_TRUE(draft.proxy_jumps_text == "ops@jump1.example.com:2222,jump2.example.com:22");

    // 串形再过一次校验与折算回到等价档案（编辑→保存这条腿幂等）。
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == conn::DraftIssue::None);
    AURORA_TEST_CHECK_TRUE(conn::draft_to_profile(draft, profile.id).ssh.proxy_jumps ==
                           profile.ssh.proxy_jumps);

    // 无跳链的档案回填成空串而不是 "none"：向导那一行留空才是「直连」的既有观感。
    auto plain = make_profile("p-plain", "direct");
    AURORA_TEST_CHECK_TRUE(conn::profile_to_draft(plain).proxy_jumps_text.empty());
}

AURORA_TEST_CASE(serial_draft_reports_the_four_issues_in_order) {
    using conn::ConnectionType;
    using conn::DraftIssue;

    auto draft = conn::ProfileDraft{};
    draft.name = "board-debug-port";
    draft.type = ConnectionType::Serial;
    // 空设备名即第一条：向导不许在没有节点名的情况下交回一份串口档案（稿 §2 标号 4）。
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::EmptyDevice);

    draft.serial.device_path = "/dev/ttyUSB0";
    // 表外速率：连接腿不判（4800 照样连得上），向导判——档位表是可选项的唯一来源。
    draft.serial.baud = 4800;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::BaudNotInTable);
    draft.serial.baud = 9600;

    draft.serial.data_bits = 4;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::DataBitsOutOfRange);
    draft.serial.data_bits = 9;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::DataBitsOutOfRange);
    draft.serial.data_bits = 7;

    draft.serial.stop_bits = 3;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::StopBitsOutOfRange);
    draft.serial.stop_bits = 2;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);

    // 表内两端都收：50 与 2000000（F4 实测十四档的两端）。
    draft.serial.baud = 50;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);
    draft.serial.baud = 2000000;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);
}

AURORA_TEST_CASE(serial_draft_round_trips_without_touching_the_other_two_types) {
    using conn::ConnectionType;

    auto draft = conn::ProfileDraft{};
    draft.name = "console";
    draft.type = ConnectionType::Serial;
    draft.serial.device_path = "/dev/ttyS0";
    draft.serial.baud = 57600;
    draft.serial.data_bits = 7;
    draft.serial.stop_bits = 2;
    draft.serial.parity = "even";
    draft.serial.flow = "hardware";
    draft.serial.line_ending = "CRLF";
    draft.serial.encoding = "UTF-8";

    const auto profile = conn::draft_to_profile(draft, "p-serial");
    AURORA_TEST_CHECK_TRUE(profile.type == ConnectionType::Serial);
    AURORA_TEST_CHECK_TRUE(profile.serial == draft.serial);
    // 三向分派不串台：串口档案的 SSH 段保持模型缺省，SSH 询问计划因此不会误触发。
    AURORA_TEST_CHECK_TRUE(profile.ssh.host.empty());
    AURORA_TEST_CHECK_TRUE(profile.ssh.auth_method == "agent");
    AURORA_TEST_CHECK_TRUE(profile.local.command_line.empty());

    const auto back = conn::profile_to_draft(profile);
    AURORA_TEST_CHECK_TRUE(back.serial == draft.serial);

    // 反过来：SSH 草稿不落串口段（串口初值就是档案初值，逐字段全等）。
    auto ssh_draft = conn::ProfileDraft{};
    ssh_draft.name = "gw";
    ssh_draft.type = ConnectionType::Ssh;
    ssh_draft.host = "gw.example.com";
    const auto ssh_profile = conn::draft_to_profile(ssh_draft, "p-ssh");
    AURORA_TEST_CHECK_TRUE(ssh_profile.serial == conn::SerialProfile{});
    AURORA_TEST_CHECK_TRUE(ssh_profile.serial.device_path.empty());
}

AURORA_TEST_CASE(telnet_draft_reuses_the_existing_issue_codes) {
    using conn::ConnectionType;
    using conn::DraftIssue;

    auto draft = conn::ProfileDraft{};
    draft.type = ConnectionType::Telnet;
    // 裁决 7.112 D2①：Telnet 型**零新增问题码**——展示名必填、主机必填、端口区间，三档全是既有码。
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::EmptyName);
    draft.name = "board-console";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::EmptyHost);
    draft.host = "192.168.1.1";
    draft.port = 0;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::PortOutOfRange);
    draft.port = 65536;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::PortOutOfRange);
    draft.port = 23;
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);
    // 认证字段不参与 Telnet 的校验：向导的 Telnet 型不画那三栏（D2①），表外的 auth_method
    // 不该把这条腿挡下——否则「编辑一条老档案」会因为一个屏幕上根本没有的字段而存不回去。
    draft.auth_method = "not-a-method";
    AURORA_TEST_CHECK_TRUE(conn::validate_draft(draft) == DraftIssue::None);
}

AURORA_TEST_CASE(telnet_draft_round_trips_without_touching_the_other_three_types) {
    using conn::ConnectionType;

    auto draft = conn::ProfileDraft{};
    draft.name = "console";
    draft.type = ConnectionType::Telnet;
    draft.host = "gw.example.com";
    draft.port = 2323;

    const auto profile = conn::draft_to_profile(draft, "p-telnet");
    AURORA_TEST_CHECK_TRUE(profile.type == ConnectionType::Telnet);
    AURORA_TEST_CHECK_TRUE(profile.telnet.host == "gw.example.com");
    AURORA_TEST_CHECK_TRUE(profile.telnet.port == 2323);
    // 四向分派不串台：Telnet 档案既不写 SSH 段也不写串口段——凭据询问计划因此不会误触发。
    AURORA_TEST_CHECK_TRUE(profile.ssh.host.empty());
    AURORA_TEST_CHECK_TRUE(profile.ssh.credential == conn::SecretHandle{});
    AURORA_TEST_CHECK_TRUE(profile.serial == conn::SerialProfile{});
    AURORA_TEST_CHECK_TRUE(profile.local.command_line.empty());

    const auto back = conn::profile_to_draft(profile);
    AURORA_TEST_CHECK_TRUE(back.type == ConnectionType::Telnet);
    AURORA_TEST_CHECK_TRUE(back.host == draft.host);
    AURORA_TEST_CHECK_TRUE(back.port == draft.port);

    // 反过来：SSH 草稿不落 telnet 段（telnet 初值就是档案初值，逐字段全等）。
    auto ssh_draft = conn::ProfileDraft{};
    ssh_draft.name = "gw";
    ssh_draft.type = ConnectionType::Ssh;
    ssh_draft.host = "gw.example.com";
    const auto ssh_profile = conn::draft_to_profile(ssh_draft, "p-ssh");
    AURORA_TEST_CHECK_TRUE(ssh_profile.telnet == conn::TelnetProfile{});
}

AURORA_TEST_CASE(secret_ask_plan_follows_auth_method) {
    using conn::SecretAsk;
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("password") == SecretAsk::Password);
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("privatekey") == SecretAsk::Passphrase);
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("keyboard-interactive") == SecretAsk::Interactive);
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("agent") == SecretAsk::None);
    AURORA_TEST_CHECK_TRUE(conn::secret_ask_for("") == SecretAsk::None);
}

AURORA_TEST_CASE(credential_plan_covers_none_stored_and_ask) {
    using conn::CredentialRoute;
    using conn::plan_credential;
    using conn::SecretAsk;
    using conn::SecretHandle;
    using conn::SecretKind;
    using conn::SshProfile;

    conn::InMemoryCredentialStore memory;
    conn::UnavailableCredentialStore unavailable;

    // agent：认证方式说了算，档案里挂着 Reference 也不取。
    SshProfile ssh;
    ssh.auth_method = "agent";
    ssh.credential = SecretHandle::reference("libsecret:p1");
    auto plan = plan_credential(ssh, memory);
    AURORA_TEST_CHECK_TRUE(plan.route == CredentialRoute::None);
    AURORA_TEST_CHECK_TRUE(plan.ask == SecretAsk::None);

    // password + 可用 Reference ⇒ Stored，句柄原样带回。
    ssh.auth_method = "password";
    plan = plan_credential(ssh, memory);
    AURORA_TEST_REQUIRE(plan.route == CredentialRoute::Stored);
    AURORA_TEST_CHECK_TRUE(plan.ask == SecretAsk::Password);
    AURORA_TEST_CHECK_TRUE(plan.handle == ssh.credential);

    // 同一份档案，库不可用 ⇒ Ask（降级红线：绝不假装能取）。
    plan = plan_credential(ssh, unavailable);
    AURORA_TEST_CHECK_TRUE(plan.route == CredentialRoute::Ask);
    AURORA_TEST_CHECK_TRUE(plan.ask == SecretAsk::Password);

    // Reference 但引用为空 ⇒ Ask（空引用不是可取的句柄）。
    ssh.credential = SecretHandle::reference("");
    plan = plan_credential(ssh, memory);
    AURORA_TEST_CHECK_TRUE(plan.route == CredentialRoute::Ask);

    // AskEveryTime 哨兵 ⇒ Ask。
    ssh.credential = SecretHandle::ask_every_time("p1", "label");
    plan = plan_credential(ssh, memory);
    AURORA_TEST_CHECK_TRUE(plan.route == CredentialRoute::Ask);
    AURORA_TEST_CHECK_TRUE(plan.handle.kind == SecretKind::AskEveryTime);

    // privatekey 的询问类型是 Passphrase；Stored 档不因类型而变。
    ssh.auth_method = "privatekey";
    ssh.credential = SecretHandle::reference("libsecret:p1");
    plan = plan_credential(ssh, memory);
    AURORA_TEST_CHECK_TRUE(plan.route == CredentialRoute::Stored);
    AURORA_TEST_CHECK_TRUE(plan.ask == SecretAsk::Passphrase);

    // 未识别认证方式按 agent 归一 ⇒ None。
    ssh.auth_method = "unknown";
    plan = plan_credential(ssh, memory);
    AURORA_TEST_CHECK_TRUE(plan.route == CredentialRoute::None);
}

AURORA_TEST_CASE(hop_profile_folding_carries_no_chain_and_no_forwarding) {
    using conn::hop_profile_of;
    using conn::KnownHostsPolicy;
    using conn::SecretHandle;
    using conn::SshHop;

    auto hop = SshHop{};
    hop.host = "jump.example.com";
    hop.port = 2222;
    hop.user = "ops";
    hop.auth_method = "privatekey";
    hop.identity_file = "/home/ops/.ssh/id_jump";
    hop.known_hosts_policy = KnownHostsPolicy::Yes;
    hop.credential = SecretHandle::reference("libsecret:jump");

    const auto folded = hop_profile_of(hop);
    AURORA_TEST_CHECK_TRUE(folded.host == hop.host);
    AURORA_TEST_CHECK_TRUE(folded.port == hop.port);
    AURORA_TEST_CHECK_TRUE(folded.user == hop.user);
    AURORA_TEST_CHECK_TRUE(folded.auth_method == hop.auth_method);
    AURORA_TEST_CHECK_TRUE(folded.identity_file == hop.identity_file);
    AURORA_TEST_CHECK_TRUE(folded.known_hosts_policy == KnownHostsPolicy::Yes);
    AURORA_TEST_CHECK_TRUE(folded.credential == hop.credential);
    // 稿 D2 的射程边界：跳板不套跳板、也不转发 agent——这两项不是「档案里恰好没写」，
    // 而是折算点强制清零，否则一条 `A→B→C` 会被读成「经 A 到 B，B 再去 D」。
    AURORA_TEST_CHECK_FALSE(folded.agent_forwarding);
    AURORA_TEST_CHECK_TRUE(folded.proxy_jumps.empty());
}

AURORA_TEST_CASE(hop_credential_plan_shares_the_target_discipline) {
    using conn::CredentialRoute;
    using conn::plan_hop_credential;
    using conn::SecretAsk;
    using conn::SecretHandle;
    using conn::SshHop;

    conn::InMemoryCredentialStore memory;
    conn::UnavailableCredentialStore unavailable;

    auto hop = SshHop{};
    hop.auth_method = "agent";
    hop.credential = SecretHandle::reference("libsecret:jump");
    // 认证方式说了算：agent 型跳挂着句柄也不去取（与目标同一条判据）。
    AURORA_TEST_CHECK_TRUE(plan_hop_credential(hop, memory).route == CredentialRoute::None);

    hop.auth_method = "password";
    const auto stored = plan_hop_credential(hop, memory);
    AURORA_TEST_REQUIRE(stored.route == CredentialRoute::Stored);
    AURORA_TEST_CHECK_TRUE(stored.ask == SecretAsk::Password);
    // 取的是**这一跳自己的**句柄：拿目标的句柄去开跳板的锁，两个口令就互换了主人。
    AURORA_TEST_CHECK_TRUE(stored.handle == hop.credential);

    // 库不可用 ⇒ Ask（降级红线同样适用于跳板：绝不假装能取）。
    AURORA_TEST_CHECK_TRUE(plan_hop_credential(hop, unavailable).route == CredentialRoute::Ask);

    // AskEveryTime 哨兵 ⇒ Ask：本期询问 UI 只覆盖目标，装配层据此把该跳留空。
    hop.credential = SecretHandle::ask_every_time("profile-1", "jump");
    const auto asked = plan_hop_credential(hop, memory);
    AURORA_TEST_CHECK_TRUE(asked.route == CredentialRoute::Ask);
    AURORA_TEST_CHECK_TRUE(asked.handle == hop.credential);

    // 引用为空 ⇒ Ask（空引用不是可取的句柄）。
    hop.credential = SecretHandle::reference("");
    AURORA_TEST_CHECK_TRUE(plan_hop_credential(hop, memory).route == CredentialRoute::Ask);
}

AURORA_TEST_CASE(quick_connect_parses_host_port_pairs) {
    // 纯主机 → 22；host:port → 显式端口；两端空白剥掉。
    auto parsed = conn::parse_quick_connect("gw.example.com");
    AURORA_TEST_REQUIRE(parsed.has_value());
    AURORA_TEST_CHECK_TRUE(parsed->first == "gw.example.com");
    AURORA_TEST_CHECK_TRUE(parsed->second == 22);

    parsed = conn::parse_quick_connect("  gw.example.com:2222\t");
    AURORA_TEST_REQUIRE(parsed.has_value());
    AURORA_TEST_CHECK_TRUE(parsed->first == "gw.example.com");
    AURORA_TEST_CHECK_TRUE(parsed->second == 2222);

    // 非数字端口段不猜（是主机名的一部分）；越界端口按整段主机名收。
    parsed = conn::parse_quick_connect("host:name");
    AURORA_TEST_REQUIRE(parsed.has_value());
    AURORA_TEST_CHECK_TRUE(parsed->first == "host:name");
    parsed = conn::parse_quick_connect("host:99999");
    AURORA_TEST_REQUIRE(parsed.has_value());
    AURORA_TEST_CHECK_TRUE(parsed->first == "host:99999" && parsed->second == 22);

    // 空输入不连。
    AURORA_TEST_CHECK_FALSE(conn::parse_quick_connect("   ").has_value());

    // 临时档案：不入库的确定性 id、缺省 agent 认证（无档案即无凭据可引）。
    const auto profile = conn::quick_connect_profile("gw.example.com", 2222);
    AURORA_TEST_CHECK_TRUE(profile.id == "quick:gw.example.com:2222");
    AURORA_TEST_CHECK_TRUE(profile.type == conn::ConnectionType::Ssh);
    AURORA_TEST_CHECK_TRUE(profile.ssh.auth_method == "agent");
    AURORA_TEST_CHECK_TRUE(profile.ssh.port == 2222);
}

}  // namespace borealis::test_cases::utest_profile_views
