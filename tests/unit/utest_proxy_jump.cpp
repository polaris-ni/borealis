/// 测试类型: unit
/// 目标单元: src/conn/proxy_jump.h（私有头）纯逻辑件
/// 测试说明: 守住 CONN.03 延后子项「跳板机（ProxyJump 语义）」的语法层四条口径——
///           `[user@]host[:port]` 逗号串的逐段折算、任一枚坏 token 即整串拒绝、
///           串形的规范化（留空端口与 :22 必须折成同一个串，否则连接复用会分键）、
///           条数上限。真实拨链属网络 I/O，在 itest_ssh_proxy_jump 里见证。

#include "conn/proxy_jump.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_proxy_jump {

namespace {

using borealis::conn::KnownHostsPolicy;
using borealis::conn::SshHop;
using borealis::conn::kMaxProxyHops;
using borealis::conn::parse_proxy_jump_list;
using borealis::conn::render_proxy_jump_list;
using borealis::conn::validate_proxy_hops;

/// @brief 解析并直接取跳链；坏串在断言里以「不该发生」的形式暴露。
[[nodiscard]] auto hops_of(std::string_view text,
                           KnownHostsPolicy policy = KnownHostsPolicy::AcceptNew)
    -> std::vector<SshHop> {
    const auto parsed = parse_proxy_jump_list(text, policy);
    return parsed.has_value() ? *parsed : std::vector<SshHop>{};
}

/// @brief 一条字段齐备的跳（validate 的样板输入）。
[[nodiscard]] auto sample_hop(std::string host = "gw.example.com", int port = 22) -> SshHop {
    auto hop = SshHop{};
    hop.host = std::move(host);
    hop.port = port;
    hop.user = "ops";
    hop.auth_method = "agent";
    return hop;
}

}  // namespace

AURORA_TEST_CASE(a_bare_host_folds_the_port_user_and_method_defaults) {
    const auto hops = hops_of("gw.example.com");
    AURORA_TEST_CHECK_EQ(hops.size(), 1U);
    AURORA_TEST_CHECK_EQ(hops[0].host, "gw.example.com");
    AURORA_TEST_CHECK_EQ(hops[0].port, 22);                      // 留空端口折 22（同 effective_port）。
    AURORA_TEST_CHECK_TRUE(hops[0].user.empty());                // 无 '@' 即不猜用户：交给 SSH 缺省。
    AURORA_TEST_CHECK_EQ(hops[0].auth_method, "agent");          // 本期 UI 不给逐跳凭据入口。
    AURORA_TEST_CHECK_TRUE(hops[0].identity_file.empty());
    AURORA_TEST_CHECK_EQ(static_cast<int>(hops[0].known_hosts_policy),
                         static_cast<int>(KnownHostsPolicy::AcceptNew));
}

AURORA_TEST_CASE(the_list_keeps_the_order_the_user_and_the_explicit_port) {
    const auto hops = hops_of("ops@bastion1:2222,bastion2");
    AURORA_TEST_CHECK_EQ(hops.size(), 2U);
    // 数组第 0 项＝最靠近本机的第一跳（设计稿 D2 的跳序口径，与探针 P4 的实测同型）。
    AURORA_TEST_CHECK_EQ(hops[0].host, "bastion1");
    AURORA_TEST_CHECK_EQ(hops[0].user, "ops");
    AURORA_TEST_CHECK_EQ(hops[0].port, 2222);
    AURORA_TEST_CHECK_EQ(hops[1].host, "bastion2");
    AURORA_TEST_CHECK_EQ(hops[1].port, 22);
    AURORA_TEST_CHECK_TRUE(hops[1].user.empty());
}

AURORA_TEST_CASE(blank_and_none_both_mean_no_chain) {
    AURORA_TEST_CHECK_TRUE(parse_proxy_jump_list("").has_value());
    AURORA_TEST_CHECK_EQ(hops_of("").size(), 0U);
    AURORA_TEST_CHECK_EQ(hops_of("   \t ").size(), 0U);
    AURORA_TEST_CHECK_EQ(hops_of("none").size(), 0U);
    AURORA_TEST_CHECK_EQ(hops_of("NONE").size(), 0U);  // 库侧按 strcasecmp 处理，同口径。
    // 显式关闭之后不许多出跳：`none,bastion` 里 'none' 不是主机名（含 ':' 之外全是字母，
    // 但这条写法的意图本身有歧义），按「两枚 token 各自解析」仍成立——串形里 none 作为
    // 主机名是合法字符串，故这里只见证它没被当成关键字吞掉后半串。
    AURORA_TEST_CHECK_EQ(hops_of("none,bastion").size(), 2U);
}

AURORA_TEST_CASE(any_bad_token_rejects_the_whole_string) {
    for (const std::string_view bad : {std::string_view{"a,,b"},        // 空 token
                                       std::string_view{",bastion"},     // 前导逗号
                                       std::string_view{"bastion,"},     // 拖尾逗号
                                       std::string_view{"@:22"},         // 无主机
                                       std::string_view{"ops@"},         // 无主机
                                       std::string_view{"a@b@c"},        // 两个用户位
                                       std::string_view{"host:0"},       // 端口下界外
                                       std::string_view{"host:70000"},   // 端口上界外
                                       std::string_view{"host:abc"},     // 端口非数字
                                       std::string_view{"host:"},        // 端口缺数字
                                       std::string_view{"ho st"},        // 内含空白
                                       std::string_view{"host/path"},    // 路径混进来
                                       std::string_view{"host%h"},       // %h 宏不展开
                                       std::string_view{"host:22:33"},   // 裸 IPv6 带端口
                                       std::string_view{"host\nexit"}}) {  // 日志注入形态
        AURORA_TEST_CHECK_MSG(!parse_proxy_jump_list(bad).has_value(), std::string{bad});
        // 坏串不留下半条链路：`hops_of` 的兜底空链回写出的串形必然为空。
        AURORA_TEST_CHECK_MSG(render_proxy_jump_list(hops_of(bad)).empty(), std::string{bad});
    }
}

AURORA_TEST_CASE(bracketed_ipv6_is_the_only_way_to_write_a_colon_host) {
    const auto plain = hops_of("[::1]");
    AURORA_TEST_CHECK_EQ(plain.size(), 1U);
    AURORA_TEST_CHECK_EQ(plain[0].host, "::1");
    AURORA_TEST_CHECK_EQ(plain[0].port, 22);

    const auto with_user = hops_of("ops@[::1]:2222");
    AURORA_TEST_CHECK_EQ(with_user.size(), 1U);
    AURORA_TEST_CHECK_EQ(with_user[0].user, "ops");
    AURORA_TEST_CHECK_EQ(with_user[0].host, "::1");
    AURORA_TEST_CHECK_EQ(with_user[0].port, 2222);

    AURORA_TEST_CHECK_FALSE(parse_proxy_jump_list("::1:2222").has_value());  // 没加方括号即两枚 ':'
    AURORA_TEST_CHECK_FALSE(parse_proxy_jump_list("[::1").has_value());      // 方括号不闭合
    AURORA_TEST_CHECK_FALSE(parse_proxy_jump_list("[::1]2222").has_value());  // ']' 后不是 ':端口'
}

AURORA_TEST_CASE(the_chain_stops_at_the_hop_cap) {
    std::string long_chain;
    for (std::size_t i = 0; i < kMaxProxyHops; ++i) {
        if (i > 0) {
            long_chain += ',';
        }
        long_chain += "gw" + std::to_string(i);
    }
    AURORA_TEST_CHECK_EQ(
        parse_proxy_jump_list(long_chain).value_or(std::vector<SshHop>{}).size(), kMaxProxyHops);
    long_chain += ",gw9";
    AURORA_TEST_CHECK_FALSE(parse_proxy_jump_list(long_chain).has_value());  // 第九跳即整串拒
}

AURORA_TEST_CASE(the_string_form_always_writes_the_port_so_two_spellings_fold) {
    // 留空与 :22 必须折成同一个串——连接复用的共享键吃的就是这一枚串（稿 D5）。
    const auto implicit_22 = hops_of("bastion");
    const auto explicit_22 = hops_of("bastion:22");
    AURORA_TEST_CHECK_EQ(render_proxy_jump_list(implicit_22), "bastion:22");
    AURORA_TEST_CHECK_EQ(render_proxy_jump_list(explicit_22), render_proxy_jump_list(implicit_22));
    AURORA_TEST_CHECK_EQ(render_proxy_jump_list({}), "");
    AURORA_TEST_CHECK_EQ(render_proxy_jump_list(hops_of("ops@a:2222,b")), "ops@a:2222,b:22");
    // IPv6 回写要补回方括号，否则下一轮解析会把裸 '::1' 判成两枚 ':'。
    AURORA_TEST_CHECK_EQ(render_proxy_jump_list(hops_of("[::1]")), "[::1]:22");
}

AURORA_TEST_CASE(parse_then_render_then_parse_is_a_fixed_point) {
    const auto once = hops_of("ops@a:2222,b,ops@[::1]");
    const auto text = render_proxy_jump_list(once);
    const auto twice = hops_of(text);
    // 致命断言：条数不等时下面逐字段比会越界，先止住而不是让它读空 vector。
    AURORA_TEST_REQUIRE_EQ(twice.size(), once.size());
    for (std::size_t i = 0; i < once.size(); ++i) {
        AURORA_TEST_CHECK_EQ(twice[i].host, once[i].host);
        AURORA_TEST_CHECK_EQ(twice[i].user, once[i].user);
        AURORA_TEST_CHECK_EQ(twice[i].port, once[i].port);
        AURORA_TEST_CHECK_TRUE(validate_proxy_hops(std::vector<SshHop>{twice[i]}));
    }
    AURORA_TEST_CHECK_EQ(render_proxy_jump_list(twice), text);
}

AURORA_TEST_CASE(validate_hops_matrix_matches_the_parser) {
    AURORA_TEST_CHECK_TRUE(validate_proxy_hops({}));
    AURORA_TEST_CHECK_TRUE(validate_proxy_hops({sample_hop()}));
    AURORA_TEST_CHECK_TRUE(validate_proxy_hops(hops_of("ops@a:2222,b,[::1]:22")));

    auto no_host = sample_hop();
    no_host.host.clear();
    AURORA_TEST_CHECK_FALSE(validate_proxy_hops({no_host}));

    auto zero_port = sample_hop();
    zero_port.port = 0;
    AURORA_TEST_CHECK_FALSE(validate_proxy_hops({zero_port}));
    zero_port.port = 65536;
    AURORA_TEST_CHECK_FALSE(validate_proxy_hops({zero_port}));

    auto control = sample_hop();
    control.user = "op s";
    AURORA_TEST_CHECK_FALSE(validate_proxy_hops({control}));

    AURORA_TEST_CHECK_TRUE(validate_proxy_hops(hops_of(std::string{})));
    std::vector<SshHop> over_cap;
    for (std::size_t i = 0; i <= kMaxProxyHops; ++i) {
        over_cap.push_back(sample_hop("gw" + std::to_string(i)));
    }
    AURORA_TEST_CHECK_FALSE(validate_proxy_hops(over_cap));  // 手改 JSON 那一路同样有闸
}

AURORA_TEST_CASE(the_default_policy_is_written_per_hop) {
    // 导入侧把 Host 块的 StrictHostKeyChecking 传进来（稿 D7），不是整串共用一个值。
    const auto hops = hops_of("a,b", KnownHostsPolicy::Yes);
    AURORA_TEST_CHECK_EQ(hops.size(), 2U);
    for (const auto &hop : hops) {
        AURORA_TEST_CHECK_EQ(static_cast<int>(hop.known_hosts_policy),
                             static_cast<int>(KnownHostsPolicy::Yes));
    }
    const auto accept = hops_of("a", KnownHostsPolicy::AcceptNew);
    AURORA_TEST_CHECK_EQ(static_cast<int>(accept[0].known_hosts_policy),
                         static_cast<int>(KnownHostsPolicy::AcceptNew));
}

}  // namespace borealis::test_cases::utest_proxy_jump
