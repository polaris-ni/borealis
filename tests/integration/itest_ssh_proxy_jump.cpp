/// 测试类型: integration（两通道）
/// 目标单元: src/conn/ssh_hop_chain.{h,cpp} + src/conn/ssh_dial.{h,cpp} 的 seed 腿 +
///           src/platform/{posix,win}/hop_pipe.cpp + 三条拨号腿（sftp_client／tunnel_client／
///           key_push）的接链形态（`SPEC.FEAT.CONN.03` 批 6）
/// 测试说明: `SPEC.FEAT.CONN.03` 跳板机（稿 D1「自建 direct-tcpip 跳链」／D4／D6／D10）的
///           **真协议**端到端证人。对端是 2–3 台进程内假 SSH 服务器（跳板那几台开了
///           `allow_direct_tcpip`，于是它们真的替客户端把字节搬到下一台），链路两端跑的
///           是真 libssh 会话。证十件事（稿 §5 的 E1/E2/E3/E4 + D6/E6 + 0 跳边界 + E5 两条 + E7 + F17 宽度闸）：
///           ① E1 一跳：目标经跳链连通并回显；跳板服务器 1 次 `direct-tcpip`，目的地＝
///              目标端口；目标服务器收到 1 条连接与 1 条 shell。
///           ② E2 两跳：A 的转发目的地＝B 端口、B 的转发目的地＝目标端口（跳序写反立刻现形）。
///           ③ E3 归因：第 2 跳认证被拒 ⇒ `open()` 回 false、档位 `Auth` 且 hop_index=1，
///              已建好的第 1 跳就地收口，目标服务器**零**连接。
///           ④ E4 主机密钥：跳板策略 `Yes` + 陌生密钥 ⇒ 档位 `HostKey`、hop_index 指到那一跳
///              （一跳与二跳各证一次），目标服务器零连接。
///           ⑤ D6/E6 兜底与翻正（批 6）：dial 原语带跳链不交 seed 仍记 `HopUnwired` 且**不发起**
///              连接（漏接线的兜底档）；三条腿则**翻正**为经链拨通——SFTP 与密钥推送走完跳链与
///              目标认证、各自停在假服务器不应的子系统／exec 请求上（本件假服务器不说 SFTP 与
///              exec，成功面归真机走查，在册边界），隧道腿直接爬到 `Active`；跳板认证谈不成时
///              目标依旧零连接（腿上的 E3 等价面）。
///           ⑥ 0 跳链路是合法空操作：`open()` 回 true、`seed_fd()` 回 0。
///           ⑦ E5 共线：同目标同跳链同秘密的两格只拨一次（跳板与目标各 1 条连接、目标 2 条
///              通道、跳板 1 次 `direct-tcpip`），回显各归各格不串。
///           ⑧ E5 分键：registry 只比不拨——空链、跳板口令异、目标口令异、跳板主机异（这一条
///              只靠 `proxy_jumps` 入键）都借不到那条链路。
///           ⑨ E7 关一格只收它自己的通道：跳板连接数不变、另一格照常收发（原生实现做不到，F15）。
///           ⑩ F17 宽度闸：seed 句柄宽过库的 16 位存储格时，dial 在发起任何协议之前拒拨
///              （照原值递进去会被折成另一枚看起来合法的句柄＝跑在别人的连接上）。
///
///           真协议链路的读环在共享会话里跑（I/O 线程），故 ⑦⑨ 也是「跳链 + 多标签」这条
///           组合形态唯一的证人。
///
///           不碰真机的 `~/.ssh/known_hosts`：跳板一律给策略 `No`（不核对、也不写盘），只有
///           ④ 的 `Yes` 走「陌生即拒」那条同样不写盘的路径——`AcceptNew` 的写盘腿在测试进程里
///           隔不干净（libssh 的主目录取 `getpwuid(2)` 的 `pw_dir`，`$HOME` 只是查不到时的回退，
///           实测依据见稿 §6），故不入本件。
///
///           两通道同一份：裸句柄全在两条平台接缝里各按后端分腿——假服务器那处见
///           `tests/support/fake_ssh_server.cpp`，`platform::make_hop_pair` 的 POSIX 腿走
///           `socketpair`、Win32 腿走回环 TCP 一对（`src/platform/win/hop_pipe.cpp`，本件是它的
///           第一位消费者，2026-10-27 起两通道同编同跑）。断言不看句柄形状，只看线上事实。

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libssh/libssh.h>

#include "borealis/conn/profile.h"
#include "borealis/session/connection.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1① 的先例）：跳链与拨号腿都在 src/ 树内。
#include "conn/key_push.h"
#include "conn/reconnect.h"
#include "conn/sftp_client.h"
#include "conn/ssh_connection.h"
#include "conn/ssh_dial.h"
#include "conn/ssh_hop_chain.h"
#include "conn/ssh_shared.h"
#include "conn/tunnel_client.h"
#include "support/fake_ssh_server.h"

namespace borealis::test_cases::itest_ssh_proxy_jump {

namespace {

using borealis::conn::DialOutcome;
using borealis::conn::KnownHostsPolicy;
using borealis::conn::PushStage;
using borealis::conn::SftpClient;
using borealis::conn::SftpError;
using borealis::conn::SshAlgorithmOverrides;
using borealis::conn::SshHop;
using borealis::conn::SshHopChain;
using borealis::conn::RetryPolicy;
using borealis::conn::SshConnection;
using borealis::conn::SshProfile;
using borealis::conn::SshSecretBundle;
using borealis::conn::SshSessionRegistry;
using borealis::conn::Tunnel;
using borealis::conn::TunnelError;
using borealis::conn::TunnelEvents;
using borealis::conn::TunnelKind;
using borealis::conn::TunnelSpec;
using borealis::conn::TunnelState;
using borealis::session::CloseReason;
using borealis::session::ConnectionEvents;
using borealis::session::ReconnectProgress;
using borealis::session::Size;
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

/// @brief 短退避档：掉线到重拨之间只等 50 ms（与共享复用的集成件同口径）。
constexpr RetryPolicy kFastPolicy{.base_ms = 50, .cap_ms = 100, .max_attempts = 3};

[[nodiscard]] auto bytes_of(std::string_view text) -> std::vector<std::byte> {
    auto out = std::vector<std::byte>{};
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return out;
}

/// @brief 终端腿的事件记录替身。**声明顺序是硬约束**：必须比连接活得久（I/O 线程在
///        `close()` 的 ack 之前还会投终态回调，连接持的是这里的裸指针）。
class TabRecorder final : public ConnectionEvents {
  public:
    auto on_bytes(std::span<const std::byte> bytes) -> void override {
        const std::lock_guard lock{mutex_};
        for (const auto byte : bytes) {
            received_.push_back(static_cast<char>(static_cast<unsigned char>(byte)));
        }
    }

    auto on_closed() -> void override { record_close(CloseReason::Unknown); }

    auto on_closed(CloseReason reason) -> void override { record_close(reason); }

    auto on_reconnect_progress(const ReconnectProgress &) -> void override {}

    [[nodiscard]] auto text() -> std::string {
        const std::lock_guard lock{mutex_};
        return received_;
    }

    [[nodiscard]] auto closes() -> std::size_t {
        const std::lock_guard lock{mutex_};
        return reasons_.size();
    }

  private:
    auto record_close(CloseReason reason) -> void {
        const std::lock_guard lock{mutex_};
        reasons_.push_back(reason);
    }

    std::mutex mutex_;
    std::string received_{};
    std::vector<CloseReason> reasons_{};
};

/// @brief 一跳链路的秘密档：目标与跳板都吃同一份口令（假服务器的接受面）。
[[nodiscard]] auto chain_bundle(const SshProfile &profile) -> SshSecretBundle {
    auto bundle = SshSecretBundle{std::optional<std::string>{std::string{kSecret}}};
    bundle.hops = secrets(profile.proxy_jumps.size());
    return bundle;
}

/// @brief 建一个走跳链的终端标签：档案带一跳，秘密按 `chain_bundle` 逐跳填齐。
[[nodiscard]] auto make_chain_tab(const SshProfile &profile, SshSecretBundle bundle,
                                  const std::shared_ptr<SshSessionRegistry> &registry, Size size)
    -> std::unique_ptr<SshConnection> {
    return std::make_unique<SshConnection>(profile, std::move(bundle), size, kFastPolicy,
                                           SshAlgorithmOverrides{}, registry);
}

/// @brief 等到目标服务器上真的起了 @p expected 条 shell（跳链通了的终端判据）。
[[nodiscard]] auto wait_target_shell(FakeSshServer &target, SshConnection &tab,
                                     std::size_t expected) -> bool {
    return wait_until(
        [&] { return target.shell_requests() >= expected && tab.alive(); }, 8000);
}

/// @brief 隧道事件的黑洞记录器：本件只等 `Tunnel::state()` 的终态，事件序的证人在
///        `itest_tunnel_failure`。**声明顺序是硬约束**：必须比 `Tunnel` 活得久——
///        工作线程在 `stop()` 收口之前还会投回调，而回调握的是这里的裸引用。
class TunnelSink final : public TunnelEvents {
  public:
    auto on_tunnel_state(TunnelState /*state*/, TunnelError /*error*/, int /*bound_port*/)
        -> void override {}
};

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

    // 判据 ⑤ 的兜底（稿 D6）：`HopUnwired` 由此退为**漏接线的兜底档**——批 6 起四条腿都接了
    // 链，只有装配漏交 seed 才会再见到这一档；两台服务器零连接证拒拨不是「静默直连」。
    const auto profile = target_profile(target, {make_hop(jump)});
    auto session = Session{};
    const auto outcome =
        ssh_dial_and_authenticate(profile, std::string{kSecret}, session.get(),
                                  SshAlgorithmOverrides{}, /*seed_fd=*/0);
    AURORA_TEST_CHECK_MSG(outcome == DialOutcome::HopUnwired, "not the fail-closed bucket");
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{0});
}

AURORA_TEST_CASE(a_seed_wider_than_the_library_slot_is_refused_before_connect) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    // 稿 F17 的宽度闸（libssh 0.12.2 `src/options.c` 实测：`SSH_OPTIONS_FD` 落的是
    // `session->opts.fd = *x & 0xffff`）：宽句柄被折掉高位后仍是一枚看起来合法的句柄，而它
    // 可能是本进程另一条活连接的句柄——那是**串线**，不是「这次拨号失败」。故 dial 在发起任何
    // 协议之前拒；两台服务器零连接既是「没连」的证人，也是「没拿别人的句柄去连」的证人。
    const auto profile = target_profile(target, {make_hop(jump)});
    auto session = Session{};
    const auto outcome =
        ssh_dial_and_authenticate(profile, std::string{kSecret}, session.get(),
                                  SshAlgorithmOverrides{}, /*seed_fd=*/0x10000U);
    AURORA_TEST_CHECK_MSG(outcome == DialOutcome::SeedTruncated, "not the seed-width bucket");
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{0});
}

AURORA_TEST_CASE(sftp_leg_dials_the_target_through_the_chain) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    // 判据 ⑤ 的翻正（批 6 D7，E6 的反面）：SFTP 腿不再拒拨——跳链建立、目标在 seed 上拨通并
    // 认证，本件假服务器**不说 SFTP 子系统**（在册边界，同 exec 腿留给真 sshd 走查那条），于是
    // 唯一停在 `sftp_init` 的拒绝上。判据的关键不是那个 false，而是两台服务器都被拨到、转发
    // 恰好一次——静默直连（稿 F7 的原生行为）会叫跳板那侧零连接。
    const auto profile = target_profile(target, {make_hop(jump)});
    auto sftp = SftpClient{};
    AURORA_TEST_CHECK_FALSE(sftp.connect(profile, chain_bundle(profile)));
    AURORA_TEST_CHECK_MSG(sftp.last_error() == SftpError::Protocol,
                          "sftp must survive dial and stop at the subsystem");
    AURORA_TEST_CHECK_FALSE(sftp.connected());
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(jump.direct_tcpip_requests(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(target.auth_attempts(), std::size_t{1});
}

AURORA_TEST_CASE(key_push_leg_dials_the_target_through_the_chain) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    // 判据 ⑤ 的翻正（批 6 D7）：推送腿走过拨号与认证，停在假服务器不应的 exec 请求上
    // （`ExecRefused` 而非旧口径的 `Dial` 格）——阶梯走到第三格就是「链上拨通了」的现形。
    const auto profile = target_profile(target, {make_hop(jump)});
    const auto report = borealis::conn::push_public_key(
        profile, chain_bundle(profile), "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5 tester@example");
    AURORA_TEST_CHECK_MSG(report.stage == PushStage::Exec, "key push stopped before the exec leg");
    AURORA_TEST_CHECK_TRUE(report.failure.has_value());
    AURORA_TEST_CHECK_MSG(report.failure.value_or(conn::PushFailure::DialNetwork) ==
                              conn::PushFailure::ExecRefused,
                          "push failure bucket is not the refused-exec one");
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(jump.direct_tcpip_requests(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{1});
}

AURORA_TEST_CASE(tunnel_leg_reaches_active_over_the_chain) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    // 判据 ⑤ 的翻正（批 6 D7）：隧道经一跳链拨通目标并走到 `Active`（本地监听起来即可，
    // 不需要服务器侧转发面）。旧口径是拒拨后钳一次重试落 `Failed`——如今 `Active` 就是接线本身。
    const auto profile = target_profile(target, {make_hop(jump)});
    auto spec = TunnelSpec{};
    spec.id = "batch6-active";
    spec.name = "batch6-active";
    spec.kind = TunnelKind::Local;
    spec.listen_port = 28083;
    spec.target_host = "127.0.0.1";
    spec.target_port = 80;
    auto sink = TunnelSink{};
    auto tunnel = Tunnel(profile, chain_bundle(profile), spec, RetryPolicy{});
    tunnel.start(sink);
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return tunnel.state() == TunnelState::Active; }, 8000),
                            "the tunnel never came up over the chain");
    AURORA_TEST_CHECK_EQ(tunnel.bound_listen_port(), 28083);
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(jump.direct_tcpip_requests(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{1});
    tunnel.stop();
}

AURORA_TEST_CASE(hop_auth_failure_keeps_the_sftp_leg_off_the_target) {
    FakeSshServer jump{jump_options(/*reject_auth=*/true)};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    // 判据 ③ 在腿上的等价面（批 6）：跳板认证谈不成 ⇒ 目标一次都不该被碰。档位落 `Network`
    // （D5：不新开档），精确原因（hop_index=0）在链日志里——真机上看「第几跳谈不成」就在那行。
    const auto profile = target_profile(target, {make_hop(jump)});
    auto sftp = SftpClient{};
    AURORA_TEST_CHECK_FALSE(sftp.connect(profile, chain_bundle(profile)));
    AURORA_TEST_CHECK_MSG(sftp.last_error() == SftpError::Network, "hop failure bucket");
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(jump.auth_attempts(), std::size_t{1});
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

AURORA_TEST_CASE(two_tabs_over_the_same_chain_share_one_jump_connection) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    auto registry = std::make_shared<SshSessionRegistry>();
    const auto profile = target_profile(target, {make_hop(jump)});
    const auto bundle = chain_bundle(profile);

    // Recorder 必须先于连接声明（I/O 线程会在 ack 前投终态回调，连接持裸指针）。
    TabRecorder first;
    TabRecorder second;
    auto first_tab = make_chain_tab(profile, bundle, registry, Size{.columns = 100, .rows = 30});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_target_shell(target, *first_tab, 1), "first shell never came up");
    auto second_tab = make_chain_tab(profile, bundle, registry, Size{.columns = 80, .rows = 24});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_target_shell(target, *second_tab, 2), "second shell never came up");

    // 判据 ⑦（稿 E5）：同目标、同跳链、同秘密的两格共线——跳板与目标都只被拨一次。
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(target.accepted_connections(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(target.session_channels(), std::size_t{2});
    // 跳板那一条连接上只有一条转发通道（链路属于连接，不随标签翻倍）。
    AURORA_TEST_CHECK_EQ(jump.direct_tcpip_requests(), std::size_t{1});

    // 两条通道各走自己的字节：回显只进各自的记录器，另一格的字节不许串进来。
    // 目标会先发 banner，故按「含自己的 / 不含对方的」判，不比对全串。
    first_tab->write(bytes_of("aaa"));
    second_tab->write(bytes_of("bbb"));
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return target.received_text().find("aaa") != std::string::npos; }, 3000),
        target.received_text());
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.text().find("aaa") != std::string::npos; },
                                       3000),
                            first.text());
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return second.text().find("bbb") != std::string::npos; }, 3000),
        second.text());
    AURORA_TEST_CHECK(first.text().find("bbb") == std::string::npos);
    AURORA_TEST_CHECK(second.text().find("aaa") == std::string::npos);

    first_tab->close();
    second_tab->close();
}

AURORA_TEST_CASE(chain_shape_and_hop_secret_split_the_link) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    auto registry = std::make_shared<SshSessionRegistry>();
    const auto chained = target_profile(target, {make_hop(jump)});
    const auto plain = fake_ssh_profile(target.port());  // 同目标，但没有跳板。

    // 判据 ⑧（稿 E5）：分键只看「链路形状 + 逐跳秘密」，且只比不拨（不挂接的 acquire 不拨号）。
    const auto base = registry->acquire(chained, chain_bundle(chained), {}, kFastPolicy);
    AURORA_TEST_REQUIRE(base != nullptr);
    // 同链同秘密 ⇒ 同一条链路对象。
    AURORA_TEST_CHECK_TRUE(registry->acquire(chained, chain_bundle(chained), {}, kFastPolicy) ==
                           base);
    // 跳链为空 ⇒ 另一条链路（合并它们等于把带跳板的标签送进直连，正是 D6 要防的形态）。
    AURORA_TEST_CHECK_TRUE(
        registry->acquire(plain, SshSecretBundle{std::string{kSecret}}, {}, kFastPolicy) != base);
    // 跳板口令不同 ⇒ 也分键：让第二格借用别人认证过的链路就是绕过它自己的材料。
    auto other_hop_secret = chain_bundle(chained);
    other_hop_secret.hops = {std::optional<std::string>{std::string{"other-jump-secret"}}};
    AURORA_TEST_CHECK_TRUE(
        registry->acquire(chained, other_hop_secret, {}, kFastPolicy) != base);
    // 只有目标口令不同同样分键（既有的 CONN.02 判据在扩型后仍成立）。
    auto other_target_secret = chain_bundle(chained);
    other_target_secret.target = std::optional<std::string>{std::string{"other-target-secret"}};
    AURORA_TEST_CHECK_TRUE(
        registry->acquire(chained, other_target_secret, {}, kFastPolicy) != base);
    // 跳板主机不同、秘密完全相同 ⇒ 只有 `proxy_jumps` 这一列能把它们分开（`same_secret`
    // 在这里帮不上忙）。少这一列入键，第二格就会挂进别人那条经另一台跳板认证好的链路。
    FakeSshServer other_jump{jump_options()};
    AURORA_TEST_REQUIRE_MSG(other_jump.ok(), "the second jump server could not listen");
    const auto other_chain = target_profile(target, {make_hop(other_jump)});
    AURORA_TEST_CHECK_TRUE(
        registry->acquire(other_chain, chain_bundle(other_chain), {}, kFastPolicy) != base);
    // 逐跳向量按跳数对齐（稿 D5）：同一条链，`hops=[口令]` 与 `hops=[]` 是两份不同的料——
    // 合并它们等于让「没取到跳板材料」的那一格挂进别人认证好的链路。
    auto unaligned = SshSecretBundle{std::optional<std::string>{std::string{kSecret}}};
    AURORA_TEST_CHECK_TRUE(registry->acquire(chained, unaligned, {}, kFastPolicy) != base);
}

AURORA_TEST_CASE(closing_one_tab_does_not_stop_the_chain_of_the_others) {
    FakeSshServer jump{jump_options()};
    FakeSshServer target{target_options()};
    AURORA_TEST_REQUIRE_MSG(jump.ok() && target.ok(), "fake ssh servers could not listen");

    auto registry = std::make_shared<SshSessionRegistry>();
    const auto profile = target_profile(target, {make_hop(jump)});
    const auto bundle = chain_bundle(profile);

    TabRecorder first;
    TabRecorder second;
    auto first_tab = make_chain_tab(profile, bundle, registry, Size{.columns = 80, .rows = 24});
    first_tab->start(first);
    AURORA_TEST_REQUIRE_MSG(wait_target_shell(target, *first_tab, 1), "first shell never came up");
    auto second_tab = make_chain_tab(profile, bundle, registry, Size{.columns = 80, .rows = 24});
    second_tab->start(second);
    AURORA_TEST_REQUIRE_MSG(wait_target_shell(target, *second_tab, 2), "second shell never came up");

    // 判据 ⑨（稿 E7）：关一格只收它自己的通道，跳板链路与另一格继续——
    // 这正是原生实现做不到的那条（F15：文件级 `proxy_disconnect` 会叫停别的标签）。
    first_tab->close();
    AURORA_TEST_REQUIRE_MSG(wait_until([&] { return first.closes() >= 1; }, 3000),
                            "first close was not reported");
    AURORA_TEST_CHECK_TRUE(second_tab->alive());
    AURORA_TEST_CHECK_EQ(jump.accepted_connections(), std::size_t{1});

    second_tab->write(bytes_of("still-here"));
    // 目标带 banner，故只判「这一格收到了自己那行」。
    AURORA_TEST_REQUIRE_MSG(
        wait_until([&] { return second.text().find("still-here") != std::string::npos; }, 3000),
        second.text());

    second_tab->close();
}

}  // namespace borealis::test_cases::itest_ssh_proxy_jump
