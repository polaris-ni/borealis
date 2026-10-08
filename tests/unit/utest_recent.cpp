/// 测试类型: unit
/// 目标单元: include/borealis/config/settings.h + src/config/settings.cpp
/// 测试说明: `SPEC.FEAT.CONN.07` 最近连接的纯逻辑登记（去重置顶、定长截断、空 id 忽略）。
///           落盘往返由 `utest_config` 的 `every_value_survives_a_write_read_round_trip` 守
///           （其 `non_default()` 已含最近连接条目），本件只验登记逻辑本身，不碰文件系统。

#include <cstdint>
#include <string>

#include "borealis/config/settings.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_recent {

using borealis::config::ConnectionSettings;
using borealis::config::kRecentConnectionLimit;
using borealis::config::push_recent_connection;
using borealis::config::RecentConnection;

AURORA_TEST_CASE(push_recent_prepends_and_dedups_by_id) {
    ConnectionSettings connection{};
    push_recent_connection(connection, "alpha", 100);
    push_recent_connection(connection, "beta", 200);
    AURORA_TEST_CHECK_EQ(connection.recent.size(), 2U);
    AURORA_TEST_CHECK_EQ(connection.recent[0].profile_id, std::string{"beta"});
    AURORA_TEST_CHECK_EQ(connection.recent[1].profile_id, std::string{"alpha"});

    // 同一档案再次连接：摘掉旧条目再置顶，语义是「最近用过的顺序」而非「首次用过的顺序」。
    push_recent_connection(connection, "alpha", 300);
    AURORA_TEST_CHECK_EQ(connection.recent.size(), 2U);
    AURORA_TEST_CHECK_EQ(connection.recent[0].profile_id, std::string{"alpha"});
    AURORA_TEST_CHECK_EQ(connection.recent[0].used_at, static_cast<std::int64_t>(300));
    AURORA_TEST_CHECK_EQ(connection.recent[1].profile_id, std::string{"beta"});
}

AURORA_TEST_CASE(push_recent_keeps_at_most_the_limit) {
    ConnectionSettings connection{};
    for (std::size_t i = 0; i < kRecentConnectionLimit + 3U; ++i) {
        push_recent_connection(connection, "p" + std::to_string(i), static_cast<std::int64_t>(1000 + i));
    }
    AURORA_TEST_CHECK_EQ(connection.recent.size(), kRecentConnectionLimit);
    // 截断丢的是最旧的尾部，最新的一定留在表头。
    AURORA_TEST_CHECK_EQ(connection.recent.front().profile_id, std::string{"p7"});
    AURORA_TEST_CHECK_EQ(connection.recent.back().profile_id, std::string{"p3"});
}

AURORA_TEST_CASE(push_recent_ignores_an_empty_profile_id) {
    ConnectionSettings connection{};
    push_recent_connection(connection, "", 100);
    AURORA_TEST_CHECK_TRUE(connection.recent.empty());
    push_recent_connection(connection, "alpha", 100);
    push_recent_connection(connection, "", 200);
    AURORA_TEST_CHECK_EQ(connection.recent.size(), 1U);
    AURORA_TEST_CHECK_EQ(connection.recent[0].profile_id, std::string{"alpha"});
}

AURORA_TEST_CASE(recent_starts_empty_and_carries_no_credential) {
    const ConnectionSettings connection{};
    AURORA_TEST_CHECK_TRUE(connection.recent.empty());
    AURORA_TEST_CHECK_EQ(kRecentConnectionLimit, 5U);
    // 最近连接只存 id 与时间戳：结构上就没有承载明文的字段（CONN.09）。
    const RecentConnection item{"alpha", 100};
    AURORA_TEST_CHECK_EQ(item.profile_id, std::string{"alpha"});
    AURORA_TEST_CHECK_EQ(item.used_at, static_cast<std::int64_t>(100));
}

}  // namespace borealis::test_cases::utest_recent
