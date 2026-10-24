/// 测试类型: integration（POSIX 腿）
/// 目标单元: src/conn/ssh_hop_chain.{h,cpp} + src/conn/ssh_dial.{h,cpp} 的 seed 腿 +
///           src/platform/posix/hop_pipe.cpp
/// 测试说明: `SPEC.FEAT.CONN.03` 跳板机（稿 D1「自建 direct-tcpip 跳链」／D4／D6／D10）的
///           **真协议**端到端证人。对端是 2–3 台进程内假 SSH 服务器（跳板那几台开了
///           `allow_direct_tcpip`，于是它们真的替客户端把字节搬到下一台），链路两端跑的
///           是真 libssh 会话。证六件事（稿 §5 的 E1/E2/E3/E4 + D6 + 0 跳边界）：
///           ① E1 一跳：目标经跳链连通并回显；跳板服务器 1 次 `direct-tcpip`，目的地＝
///              目标端口；目标服务器收到 1 条连接与 1 条 shell。
///           ② E2 两跳：A 的转发目的地＝B 端口、B 的转发目的地＝目标端口（跳序写反立刻现形）。
///           ③ E3 归因：第 2 跳认证被拒 ⇒ `open()` 回 false、档位 `Auth` 且 hop_index=1，
///              已建好的第 1 跳就地收口，目标服务器**零**连接。
///           ④ E4 主机密钥：跳板策略 `Yes` + 陌生密钥 ⇒ 档位 `HostKey`、hop_index 指到那一跳
///              （一跳与二跳各证一次），目标服务器零连接。
///           ⑤ D6 fail-closed：档案带跳链而调用方给不了 seed ⇒ 记 `HopUnwired` 且**不发起**
///              连接（两侧服务器都是零连接），绝不静默直连。
///           ⑥ 0 跳链路是合法空操作：`open()` 回 true、`seed_fd()` 回 0。
///
///           不碰真机的 `~/.ssh/known_hosts`：跳板一律给策略 `No`（不核对、也不写盘），只有
///           ④ 的 `Yes` 走「陌生即拒」那条同样不写盘的路径——`AcceptNew` 的写盘腿在测试进程里
///           隔不干净（libssh 的主目录取 `getpwuid(2)` 的 `pw_dir`，`$HOME` 只是查不到时的回退，
///           实测依据见稿 §6），故不入本件。
///
///           无 Win32 腿：假服务器与 hop_pipe 的 Win32 腿均未编译验证（稿 §6 第一条），
///           文件名 `_posix_` 让 cmake/BorealisTests.cmake 在 Windows 构建里排除本套件。

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libssh/libssh.h>

#include "borealis/conn/profile.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1① 的先例）：跳链与拨号腿都在 src/ 树内。
#include "conn/ssh_dial.h"
#include "conn/ssh_hop_chain.h"
#include "support/fake_ssh_server.h"

namespace borealis::test_cases::itest_posix_ssh_proxy_jump {

namespace {

using borealis::conn::DialOutcome;
using borealis::conn::KnownHostsPolicy;
using borealis::conn::SshAlgorithmOverrides;
using borealis::conn::SshHop;
using borealis::conn::SshHopChain;
using borealis::conn::SshProfile;
using borealis::testing::FakeSshServer;
using borealis::testing::FakeSshServerOptions;
using borealis::testing::fake_ssh_profile;
using borealis::testing::wait_until;

/// @brief 全链路共用的口令（每台假服务器都接受它，除非用例明说拒绝认证）。
constexpr std::string_view kSecret = "secret";

/// @brief 写进远端 shell 再指望它原样回来的一行字：证「字节真过了整条链」。
constexpr std::string_view kProbe = "through-the-jump";

/// @brief RAII 会话：用例线程持有；稿 §5 的关闭序要求它先于 `SshHopChain::close()` 收口。
class Session final {
  public:
    Session() : handle_{ssh_new()} {}
    ~Session() {
        if (handle_ != nullptr) {
            ssh_disconnect(handle_);
            ssh_free(handle_);
        }
    }
    Session(const Session &) = delete;
    auto operator=(const Session &) -> Session & = delete;

    [[nodiscard]] auto get() const -> ssh_session_struct * { return handle_; }

  private:
    ssh_session_struct *handle_{nullptr};
};

/// @brief 一跳折成档案里的一项；策略缺省 `No`（不核对，也不写真机的 known_hosts）。
[[nodiscard]] auto make_hop(const FakeSshServer &server,
                            KnownHostsPolicy policy = KnownHostsPolicy::No) -> SshHop {
    auto hop = SshHop{};
    hop.host = "127.0.0.1";
    hop.port = server.port();
    hop.user = "tester";
    hop.auth_method = "password";
    hop.known_hosts_policy = policy;
    return hop;
}

/// @brief 一台当得起跳板的假服务器：转发开关是它与普通服务器唯一的区别。
[[nodiscard]] auto jump_options(bool reject_auth = false) -> FakeSshServerOptions {
    auto options = FakeSshServerOptions{};
    options.allow_direct_tcpip = true;
    options.reject_auth = reject_auth;
    return options;
}

/// @brief 目标服务器：开回显并给一条 banner（banner 只用于确认 shell 真起来了）。
[[nodiscard]] auto target_options() -> FakeSshServerOptions {
    auto options = FakeSshServerOptions{};
    options.banner = "ok\r\n";
    return options;
}

/// @brief 逐跳都填同一份口令。
[[nodiscard]] auto secrets(std::size_t count) -> std::vector<std::optional<std::string>> {
    auto out = std::vector<std::optional<std::string>>{};
    out.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        out.emplace_back(std::string{kSecret});
    }
    return out;
}

/// @brief 端口拼成假服务器记的目的地串形（`host:port`）。
[[nodiscard]] auto destination_of(const FakeSshServer &server) -> std::string {
    return "127.0.0.1:" + std::to_string(server.port());
}

/// @brief 在已认证的会话上开一条 session 通道、要 PTY 与 shell，写一行并等它回显。
///
/// 只在用例线程上跑（libssh 会话不跨线程）；通道在本函数内收干净。
/// @return false＝通道/PTY/shell 任一步谈不成，或回显没在期限内回来。
[[nodiscard]] auto shell_round_trip(ssh_session_struct *session) -> bool {
    auto *channel = ssh_channel_new(session);
    if (channel == nullptr) {
        return false;
    }
    const auto teardown = [&channel] {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
    };
    if (ssh_channel_open_session(channel) != SSH_OK ||
        ssh_channel_request_pty_size(channel, "xterm", 80, 24) != SSH_OK ||
        ssh_channel_request_shell(channel) != SSH_OK) {
        teardown();
        return false;
    }
    if (ssh_channel_write(channel, kProbe.data(), static_cast<uint32_t>(kProbe.size())) <= 0) {
        teardown();
        return false;
    }
    // 回显要穿「目标服务器 → 目标会话 → 用例」，逐跳中继一拍 10 ms，故给足等待。
    auto seen = std::string{};
    auto buffer = std::array<char, 256>{};
    const auto done = wait_until(
        [&] {
            const auto available = ssh_channel_poll_timeout(channel, 50, 0);
            if (available > 0) {
                const auto got = ssh_channel_read_nonblocking(
                    channel, buffer.data(), static_cast<uint32_t>(buffer.size()), 0);
                if (got > 0) {
                    seen.append(buffer.data(), static_cast<std::size_t>(got));
                }
            }
            return seen.find(kProbe) != std::string::npos;
        },
        8000);
    teardown();
    return done;
}

/// @brief 一次「建链 + 拨目标」的现场（`SshHopChain` 与 `Session` 都不可搬动，故就地组装）。
class ChainDial final {
  public:
    explicit ChainDial(const SshProfile &profile,
                       std::vector<std::optional<std::string>> hop_secrets)
        : profile_{profile}, hop_secrets_{std::move(hop_secrets)} {
        opened_ = chain_.open(profile_, hop_secrets_, SshAlgorithmOverrides{});
        if (opened_) {
            target_outcome_ =
                ssh_dial_and_authenticate(profile_, std::string{kSecret}, session_.get(),
                                         SshAlgorithmOverrides{}, chain_.seed_fd());
        }
    }
    ChainDial(const ChainDial &) = delete;
    auto operator=(const ChainDial &) -> ChainDial & = delete;

    /// @brief 稿 §5 的关闭序：会话（含其通道）先收，之后才轮到链路的逆序收口。
    ///        本类的析构顺序（session_ 先于 chain_）已经保证了这点，显式 close 只为可读性。
    auto close() -> void { chain_.close(); }

    [[nodiscard]] auto opened() const -> bool { return opened_; }
    [[nodiscard]] auto target_outcome() const -> DialOutcome { return target_outcome_; }
    [[nodiscard]] auto failure() const -> DialOutcome { return chain_.failure().outcome; }
    [[nodiscard]] auto failure_hop() const -> std::size_t { return chain_.failure().hop_index; }
    [[nodiscard]] auto seed_fd() const -> std::uintptr_t { return chain_.seed_fd(); }
    [[nodiscard]] auto session() const -> ssh_session_struct * { return session_.get(); }

  private:
    SshProfile profile_{};
    std::vector<std::optional<std::string>> hop_secrets_{};
    SshHopChain chain_{};
    Session session_{};
    bool opened_{false};
    DialOutcome target_outcome_{DialOutcome::Ok};
};

/// @brief 带 @p hop_count 台跳板的目标档案（跳板服务器按「最靠近本机」的次序给出）。
[[nodiscard]] auto target_profile(const FakeSshServer &target, const std::vector<SshHop> &hops)
    -> SshProfile {
    auto profile = fake_ssh_profile(target.port());
    profile.proxy_jumps = hops;
    return profile;
}

}  // namespace

AURORA_TEST_CASE(one_hop_chain_connects_and_forwards_to_the_target_port) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    auto dial = ChainDial{target_profile(target, {make_hop(jump)}), secrets(1)};
    AURORA_TEST_REQUIRE_MSG(dial.opened(), "one-hop chain did not open");
    AURORA_TEST_CHECK_MSG(dial.target_outcome() == DialOutcome::Ok,
                          "target dial over the chain seed failed");
    AURORA_TEST_CHECK_NE(dial.seed_fd(), std::uintptr_t{0});

    // 判据 ①：字节真的往返了一次（跳板在中间搬了两向）。
    AURORA_TEST_REQUIRE_MSG(shell_round_trip(dial.session()), "no echo through the one-hop chain");
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(target.session_channels(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(jump.session_channels(), std::size_t{0});

    // 判据 ①（跳板侧）：一次转发，目的地＝目标端口；跳板自己被认证了一次。
    AURORA_TEST_CHECK_EQ(jump.direct_tcpip_requests(), std::size_t{1});
    AURORA_TEST_REQUIRE_EQ(jump.forwarded_destinations().size(), std::size_t{1});
    AURORA_TEST_CHECK_STREQ(jump.forwarded_destinations()[0], destination_of(target));
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{1});

    dial.close();
    AURORA_TEST_CHECK_EQ(dial.seed_fd(), std::uintptr_t{0});
}

AURORA_TEST_CASE(two_hop_chain_forwards_in_profile_order) {
    FakeSshServer first{jump_options()};
    FakeSshServer second{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(first.ok() && second.ok() && target.ok(),
                            "fake ssh servers could not listen");

    auto dial = ChainDial{target_profile(target, {make_hop(first), make_hop(second)}), secrets(2)};
    AURORA_TEST_REQUIRE_MSG(dial.opened(), "two-hop chain did not open");
    AURORA_TEST_CHECK_MSG(dial.target_outcome() == DialOutcome::Ok,
                          "target dial over the two-hop seed failed");
    AURORA_TEST_REQUIRE_MSG(shell_round_trip(dial.session()), "no echo through the two-hop chain");

    // 判据 ②：两台跳板各收到一次转发，目的地按「档案序＝跳序」串成 first→second→target。
    //         跳序若在实现里写反，这两条串立刻对不上（稿 F10 的语义证人）。
    AURORA_TEST_REQUIRE_EQ(first.forwarded_destinations().size(), std::size_t{1});
    AURORA_TEST_CHECK_STREQ(first.forwarded_destinations()[0], destination_of(second));
    AURORA_TEST_REQUIRE_EQ(second.forwarded_destinations().size(), std::size_t{1});
    AURORA_TEST_CHECK_STREQ(second.forwarded_destinations()[0], destination_of(target));
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{1});

    dial.close();
}

AURORA_TEST_CASE(second_hop_auth_failure_is_attributed_to_that_hop) {
    FakeSshServer first{jump_options()};
    FakeSshServer second{jump_options(/*reject_auth=*/true)};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(first.ok() && second.ok() && target.ok(),
                            "fake ssh servers could not listen");

    auto dial = ChainDial{target_profile(target, {make_hop(first), make_hop(second)}), secrets(2)};

    // 判据 ③：失败落在第 2 跳（下标 1），档位是那一跳自己的 `Auth`；已建好的第 1 跳就地收口。
    AURORA_TEST_CHECK_FALSE(dial.opened());
    AURORA_TEST_CHECK_MSG(dial.failure() == DialOutcome::Auth, "failure is not the auth bucket");
    AURORA_TEST_CHECK_EQ(dial.failure_hop(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(dial.seed_fd(), std::uintptr_t{0});
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(second.auth_attempts(), std::size_t{1});

    dial.close();
}

AURORA_TEST_CASE(jump_host_key_policy_refuses_unknown_key_before_target) {
    FakeSshServer first{jump_options()};
    FakeSshServer second{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(first.ok() && second.ok() && target.ok(),
                            "fake ssh servers could not listen");

    // 判据 ④（首跳）：`Yes` + 一次性密钥＝陌生，拨号在核对处停住，目标一次都没被碰。
    auto head =
        ChainDial{target_profile(target, {make_hop(first, KnownHostsPolicy::Yes)}), secrets(1)};
    AURORA_TEST_CHECK_FALSE(head.opened());
    AURORA_TEST_CHECK_MSG(head.failure() == DialOutcome::HostKey, "not the host-key bucket");
    AURORA_TEST_CHECK_EQ(head.failure_hop(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(first.auth_attempts(), std::size_t{0});
    head.close();

    // 判据 ④（次跳）：hop_index 必须指到出事的那一跳，而不是恒等于 0。
    auto tail = ChainDial{
        target_profile(target, {make_hop(first), make_hop(second, KnownHostsPolicy::Yes)}),
        secrets(2)};
    AURORA_TEST_CHECK_FALSE(tail.opened());
    AURORA_TEST_CHECK_MSG(tail.failure() == DialOutcome::HostKey, "not the host-key bucket");
    AURORA_TEST_CHECK_EQ(tail.failure_hop(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(first.direct_tcpip_requests(), std::size_t{1});
    tail.close();
}

AURORA_TEST_CASE(profile_with_jumps_without_seed_refuses_to_connect_directly) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    // 判据 ⑤（稿 D6）：带跳链的档案落到一条建不了链的腿上＝拒拨，而不是「退回直连」。
    //         这条腿在批 4 接进共享表之前，正是 SFTP／隧道／密钥推送三条腿的现形口径。
    const auto profile = target_profile(target, {make_hop(jump)});
    auto session = Session{};
    const auto outcome =
        ssh_dial_and_authenticate(profile, std::string{kSecret}, session.get(),
                                  SshAlgorithmOverrides{}, /*seed_fd=*/0);
    AURORA_TEST_CHECK_MSG(outcome == DialOutcome::HopUnwired, "not the fail-closed bucket");
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{0});
}

AURORA_TEST_CASE(chain_without_hops_is_a_valid_no_op) {
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(target.ok(), "fake ssh server could not listen");

    // 判据 ⑥：0 跳不建链路、seed 保持 0，调用方按直连拨——本件的其余用例都靠这条边界兜底。
    const auto profile = fake_ssh_profile(target.port());
    auto chain = SshHopChain{};
    AURORA_TEST_REQUIRE_TRUE(chain.open(profile, {}, SshAlgorithmOverrides{}));
    AURORA_TEST_CHECK_EQ(chain.seed_fd(), std::uintptr_t{0});
    AURORA_TEST_CHECK_MSG(chain.failure().outcome == DialOutcome::Ok, "empty chain reported a bug");
    chain.close();
    chain.close();  // 幂等：重复收口不该碰已经关掉的句柄。
}

}  // namespace borealis::test_cases::itest_posix_ssh_proxy_jump
