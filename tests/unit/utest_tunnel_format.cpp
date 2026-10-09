/// 测试类型: unit
/// 目标单元: src/ui/tunnel_format.{h,cpp}（无 au:: 类型）纯格式化件
/// 测试说明: 守住 CONN.08 面板的「最后一步」纯函数——行模型配对、五态/错误
///           词条 key 全档覆盖、三式端点串折叠（-R 0 端口的择定形态）、
///           Backoff 等待秒数取整。真派发与像素属 itest_tunnel_panel。

#include "ui/tunnel_format.h"

#include <map>
#include <string>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_tunnel_format {

namespace {

[[nodiscard]] auto spec(conn::TunnelKind kind, int listen_port, std::string target_host = "db01.internal",
                        int target_port = 5432) -> conn::TunnelSpec {
    conn::TunnelSpec s;
    s.id = "t-" + std::string{ui::tunnel_kind_badge(kind)};
    s.kind = kind;
    s.listen_port = listen_port;
    s.target_host = std::move(target_host);
    s.target_port = target_port;
    return s;
}

}  // namespace

AURORA_TEST_CASE(tunnel_rows_pairs_snapshots_and_defaults_to_stopped) {
    auto a = spec(conn::TunnelKind::Local, 8080);
    auto b = spec(conn::TunnelKind::Dynamic, 1080);
    std::map<std::string, ui::TunnelRuntime> runtimes;
    auto rt = ui::TunnelRuntime{};
    rt.state = conn::TunnelState::Active;
    rt.bound_port = 0;
    runtimes[a.id] = rt;

    const auto rows = ui::tunnel_rows({a, b}, runtimes);
    AURORA_TEST_REQUIRE(rows.size() == 2U);
    AURORA_TEST_CHECK_TRUE(rows[0].spec == a);
    AURORA_TEST_CHECK_TRUE(rows[0].runtime.state == conn::TunnelState::Active);
    // 无快照的定义按 Stopped 呈现（重启后/未起过）。
    AURORA_TEST_CHECK_TRUE(rows[1].runtime.state == conn::TunnelState::Stopped);
    AURORA_TEST_CHECK_FALSE(rows[1].runtime.autostart_skipped);
}

AURORA_TEST_CASE(state_and_error_keys_cover_every_enum) {
    AURORA_TEST_CHECK_TRUE(ui::tunnel_state_key(conn::TunnelState::Stopped) == "tunnel.state.stopped");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_state_key(conn::TunnelState::Dialing) == "tunnel.state.dialing");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_state_key(conn::TunnelState::Active) == "tunnel.state.active");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_state_key(conn::TunnelState::Backoff) == "tunnel.state.backoff");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_state_key(conn::TunnelState::Failed) == "tunnel.state.failed");

    AURORA_TEST_CHECK_TRUE(ui::tunnel_error_key(conn::TunnelError::None).empty());
    AURORA_TEST_CHECK_TRUE(ui::tunnel_error_key(conn::TunnelError::DialFailed) == "tunnel.error.dial_failed");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_error_key(conn::TunnelError::BindFailed) == "tunnel.error.bind_failed");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_error_key(conn::TunnelError::RemoteRefused) == "tunnel.error.remote_refused");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_error_key(conn::TunnelError::ChannelLost) == "tunnel.error.channel_lost");

    AURORA_TEST_CHECK_TRUE(ui::tunnel_kind_badge(conn::TunnelKind::Local) == "-L");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_kind_badge(conn::TunnelKind::Remote) == "-R");
    AURORA_TEST_CHECK_TRUE(ui::tunnel_kind_badge(conn::TunnelKind::Dynamic) == "-D");
}

AURORA_TEST_CASE(endpoint_line_folds_three_kinds) {
    // CJK-LITERAL: 上屏文案 - 断言里的箭头即被测的端点串折叠形态（设计稿判据 2/4），
    // 换成 ASCII 让被测事实与实现字面量脱钩。
    auto local = ui::TunnelRow{spec(conn::TunnelKind::Local, 5432), ui::TunnelRuntime{}};
    AURORA_TEST_CHECK_MSG(ui::tunnel_endpoint_line(local) == "127.0.0.1:5432 → db01.internal:5432",
                          ui::tunnel_endpoint_line(local));

    auto dynamic = ui::TunnelRow{spec(conn::TunnelKind::Dynamic, 1080), ui::TunnelRuntime{}};
    AURORA_TEST_CHECK_MSG(ui::tunnel_endpoint_line(dynamic) == "SOCKS5 127.0.0.1:1080",
                          ui::tunnel_endpoint_line(dynamic));

    // Remote：地址段留空（服务器侧），0 端口未回报显示 :0。
    auto remote = ui::TunnelRow{spec(conn::TunnelKind::Remote, 0, "127.0.0.1", 873), ui::TunnelRuntime{}};
    AURORA_TEST_CHECK_MSG(ui::tunnel_endpoint_line(remote) == ":0 → 127.0.0.1:873",
                          ui::tunnel_endpoint_line(remote));
    // Active 回报实际端口后换成择定值（判据 4）。
    remote.runtime.bound_port = 8730;
    AURORA_TEST_CHECK_MSG(ui::tunnel_endpoint_line(remote) == ":8730 → 127.0.0.1:873",
                          ui::tunnel_endpoint_line(remote));
    // 非 0 的固定远端端口原样显示。
    auto fixed = ui::TunnelRow{spec(conn::TunnelKind::Remote, 9000, "127.0.0.1", 9000), ui::TunnelRuntime{}};
    AURORA_TEST_CHECK_MSG(ui::tunnel_endpoint_line(fixed) == ":9000 → 127.0.0.1:9000",
                          ui::tunnel_endpoint_line(fixed));
}

AURORA_TEST_CASE(endpoint_note_key_only_for_remote_zero_port) {
    auto remote = ui::TunnelRow{spec(conn::TunnelKind::Remote, 0, "127.0.0.1", 873), ui::TunnelRuntime{}};
    AURORA_TEST_CHECK_TRUE(ui::tunnel_endpoint_note_key(remote) == "tunnel.listen.server_picked");
    remote.runtime.state = conn::TunnelState::Active;
    AURORA_TEST_CHECK_TRUE(ui::tunnel_endpoint_note_key(remote) == "tunnel.listen.server_chosen");
    remote.runtime.bound_port = 8730;
    AURORA_TEST_CHECK_TRUE(ui::tunnel_endpoint_note_key(remote) == "tunnel.listen.server_chosen");

    auto fixed = ui::TunnelRow{spec(conn::TunnelKind::Remote, 9000), ui::TunnelRuntime{}};
    AURORA_TEST_CHECK_TRUE(ui::tunnel_endpoint_note_key(fixed).empty());
    auto local = ui::TunnelRow{spec(conn::TunnelKind::Local, 5432), ui::TunnelRuntime{}};
    AURORA_TEST_CHECK_TRUE(ui::tunnel_endpoint_note_key(local).empty());
}

AURORA_TEST_CASE(retry_wait_rounds_up_only_in_backoff) {
    auto row = ui::TunnelRow{spec(conn::TunnelKind::Local, 8080), ui::TunnelRuntime{}};
    AURORA_TEST_CHECK_EQ(ui::tunnel_retry_wait_s(row), 0);  // Stopped 不显示等待

    row.runtime.state = conn::TunnelState::Backoff;
    row.runtime.attempt = 1;
    AURORA_TEST_CHECK_EQ(ui::tunnel_retry_wait_s(row), 1);   // 1000ms
    row.runtime.attempt = 3;
    AURORA_TEST_CHECK_EQ(ui::tunnel_retry_wait_s(row), 4);   // 4000ms
    row.runtime.attempt = 6;
    AURORA_TEST_CHECK_EQ(ui::tunnel_retry_wait_s(row), 30);  // 钳到 cap

    // 逐条策略（D9②）：改 spec.retry 即改等待读数。
    row.spec.retry.base_ms = 500;
    row.spec.retry.cap_ms = 1500;
    row.runtime.attempt = 5;
    AURORA_TEST_CHECK_EQ(ui::tunnel_retry_wait_s(row), 2);  // 500*16=8000 → 钳 1500 → 向上取整 2s

    row.runtime.state = conn::TunnelState::Failed;
    AURORA_TEST_CHECK_EQ(ui::tunnel_retry_wait_s(row), 0);
}

}  // namespace borealis::test_cases::utest_tunnel_format
