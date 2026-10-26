/// 测试类型: unit
/// 目标单元: include/borealis/ui/monitor_binding.h + src/ui/monitor_binding.cpp（无 au:: 类型）
/// 测试说明: 守住字节监视卡的四档身份折法（`SPEC.FEAT.CONN.05` 五期稿 D38）。四型共用一张卡之后，
///           「标题那格写的是谁」是全仓唯一有证人的地方——装配层那段取料不进 runner（裁决 7.83）。
///           本套件钉五件事：① 串口＝设备路径逐字符原样；② SSH＝`[user@]host[:port]`，缺省端口省略、
///           非缺省写出、空 user 不写前导 `@`；③ Telnet 同口径而缺省端口是 23；④ 本地＝焦点那格的
///           **有效**命令行，空命令行不伪造第二来源的名字而是整格空回；⑤ 料缺失或与 `kind` 不匹配
///           一律回空串（调用方据此整格不画），以及 IPv6 字面量在主机的位置上加方括号。

#include <string>

#include "borealis/conn/local_terminal.h"
#include "borealis/conn/profile.h"
#include "borealis/ui/monitor_binding.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_monitor_binding {

namespace {

using borealis::conn::ConnectionType;
using borealis::ui::MonitorBindingSource;

/// @brief 一条串口料。
[[nodiscard]] auto serial_source(std::string device_path) -> MonitorBindingSource {
    MonitorBindingSource source;
    source.kind = ConnectionType::Serial;
    source.serial = borealis::conn::SerialProfile{.device_path = std::move(device_path)};
    return source;
}

/// @brief 一条 SSH 料；端口按值给，缺省与否由调用方按 `conn::SshProfile` 的初值决定。
[[nodiscard]] auto ssh_source(std::string host, int port, std::string user) -> MonitorBindingSource {
    MonitorBindingSource source;
    source.kind = ConnectionType::Ssh;
    source.ssh = borealis::conn::SshProfile{
        .host = std::move(host), .port = port, .user = std::move(user)};
    return source;
}

/// @brief 一条 Telnet 料。
[[nodiscard]] auto telnet_source(std::string host, int port) -> MonitorBindingSource {
    MonitorBindingSource source;
    source.kind = ConnectionType::Telnet;
    source.telnet = borealis::conn::TelnetProfile{.host = std::move(host), .port = port};
    return source;
}

/// @brief 一条本地料（有效命令行，与 `local_specs_by_pane` 存的那一份同形态）。
[[nodiscard]] auto local_source(std::string command_line) -> MonitorBindingSource {
    MonitorBindingSource source;
    source.kind = ConnectionType::Local;
    source.local = borealis::conn::LocalTerminalSpec{.command_line = std::move(command_line)};
    return source;
}

}  // namespace

AURORA_TEST_CASE(the_serial_leg_shows_the_device_path_verbatim) {
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(serial_source("COM7")),
                         std::string{"COM7"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(serial_source("/dev/ttyUSB0")),
                         std::string{"/dev/ttyUSB0"});
    // 路径里的空格与反斜杠都是设备名的一部分，本件不折、不转义、不截断。
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(serial_source(R"(\\host\Share Port)")),
                         std::string{R"(\\host\Share Port)"});
}

AURORA_TEST_CASE(the_ssh_leg_writes_the_port_only_when_it_is_not_the_default) {
    const int default_port = borealis::conn::SshProfile{}.port;
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("build.internal", default_port, "ci")),
                         std::string{"ci@build.internal"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("build.internal", 2222, "ci")),
                         std::string{"ci@build.internal:2222"});
    // 一份只填了主机、端口从没动过的档案（用户视角的「用缺省端口」）不该在卡上出现端口段。
    MonitorBindingSource untouched;
    untouched.kind = ConnectionType::Ssh;
    untouched.ssh = borealis::conn::SshProfile{.host = "build.internal"};
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(untouched),
                         std::string{"build.internal"});
}

AURORA_TEST_CASE(the_ssh_leg_omits_the_user_prefix_when_no_user_is_set) {
    // 空 user 在 SSH 侧的语义是「以当前用户登录」，写成 `@host` 就是报一个不存在的登录名。
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("gateway.example", 2200, "")),
                         std::string{"gateway.example:2200"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("gateway.example", 22, "")),
                         std::string{"gateway.example"});
}

AURORA_TEST_CASE(the_telnet_leg_omits_its_own_default_port) {
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             telnet_source("rack.local", borealis::conn::TelnetProfile{}.port)),
                         std::string{"rack.local"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(telnet_source("rack.local", 2323)),
                         std::string{"rack.local:2323"});
    // Telnet 没有认证协议，档案里就没有 user 字段：即便调用方误填了 ssh 那块也不该被读出来。
    auto source = telnet_source("rack.local", 2323);
    source.ssh = borealis::conn::SshProfile{.host = "wrong", .port = 22, .user = "nobody"};
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(source),
                         std::string{"rack.local:2323"});
}

AURORA_TEST_CASE(the_local_leg_shows_the_effective_command_line_of_that_pane) {
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(local_source("pwsh.exe -NoLogo")),
                         std::string{"pwsh.exe -NoLogo"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(local_source("/bin/zsh")),
                         std::string{"/bin/zsh"});
    // 空命令行＝取料没取到（探测过的默认 shell 早该在这之前写进有效值），不伪造「本地终端」这种
    // 第二来源的名字；调用方据空串整格不画。
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(local_source("")), std::string{});
}

AURORA_TEST_CASE(an_ipv6_literal_host_is_bracketed_before_the_port) {
    // 与跳链显示同一规则：主机串含 `:` 即按 IPv6 字面量处理，方括号加在主机位置、端口留在括外。
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("2001:db8::1", 2222, "ops")),
                         std::string{"ops@[2001:db8::1]:2222"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("2001:db8::1", 22, "")),
                         std::string{"[2001:db8::1]"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(telnet_source("2001:db8::1", 23)),
                         std::string{"[2001:db8::1]"});
}

AURORA_TEST_CASE(missing_material_answers_an_empty_target) {
    // 默认构造（`kind==Local` 而那块料为空）与四型「kind 有、料没有」的六种错配一律回空串。
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(MonitorBindingSource{}), std::string{});
    for (const ConnectionType kind : {ConnectionType::Local, ConnectionType::Ssh,
                                      ConnectionType::Serial, ConnectionType::Telnet}) {
        MonitorBindingSource bare;
        bare.kind = kind;
        AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(bare), std::string{});
    }
    // 型与块不匹配（说 SSH 却只给了串口料）同样回空：本件不猜「大概连的是谁」。
    auto mismatched = serial_source("COM7");
    mismatched.kind = ConnectionType::Ssh;
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(mismatched), std::string{});
    // 有块但关键字段空：远程两型没有主机就无从谈起，串口则允许路径为空由调用方不画。
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("", 2222, "ops")),
                         std::string{});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(telnet_source("", 2323)), std::string{});
}

AURORA_TEST_CASE(the_default_port_threshold_is_the_profile_default_itself) {
    // 这条不测「22」这个数字，测的是「阈值跟着档案初值走」：档案缺省值若改，卡片上的省略规则
    // 必须同批跟着改，否则公共头第二段那句「单一真值源」就是空的。
    const int ssh_default = borealis::conn::SshProfile{}.port;
    const int telnet_default = borealis::conn::TelnetProfile{}.port;
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("h", ssh_default, "")),
                         std::string{"h"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             ssh_source("h", ssh_default + 1, "")),
                         std::string{"h:"} + std::to_string(ssh_default + 1));
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             telnet_source("h", telnet_default)),
                         std::string{"h"});
    AURORA_TEST_CHECK_EQ(borealis::ui::monitor_binding_target(
                             telnet_source("h", telnet_default + 1)),
                         std::string{"h:"} + std::to_string(telnet_default + 1));
}

}  // namespace borealis::test_cases::utest_monitor_binding
