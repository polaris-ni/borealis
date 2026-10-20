/// 测试类型: unit
/// 目标单元: include/borealis/ui/broadcast.h + src/ui/broadcast.cpp
/// 测试说明: 广播组的成员集合与跨标签扇出（`SPEC.FEAT.WS.09`，裁决 7.121）。组侧断插入序、幂等、
///           切换与整标签清账；路由侧断只认成员来源、源格不重收、同会话去重、端点缺失/消失即跳过、
///           重登记不动成员资格、变更钩子只在真变时触发，以及按键与文本两条腿的边界一致。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/term/keymap.h"
#include "borealis/ui/broadcast.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_broadcast {

namespace {

using borealis::term::KeyPress;
using borealis::term::KeySym;
using borealis::ui::BroadcastGroup;
using borealis::ui::BroadcastMember;
using borealis::ui::BroadcastRouter;
using borealis::ui::PaneId;
using borealis::ui::TabId;

/// @brief 按键简写：字段与视口转发时拿到的 `term::KeyPress` 同形。
[[nodiscard]] constexpr auto key(KeySym sym, bool shift = false, bool control = false) -> KeyPress {
    return KeyPress{.sym = sym, .shift = shift, .control = control};
}

/// @brief 一次扇出的记账：只记 pane 与载荷，够断「发给谁、发了几次、发的是什么」。
struct Deliveries {
    std::vector<PaneId> key_targets;
    std::vector<KeyPress> key_presses;
    std::vector<PaneId> text_targets;
    std::vector<std::u32string> text_payloads;
};

/// @brief 给一个标签造端点：`session` 是会话身份读数，`nullptr` 即「该 pane 的会话已消失」。
[[nodiscard]] auto make_sink(const void *session, Deliveries &out) -> BroadcastRouter::Sink {
    return BroadcastRouter::Sink{
        .stream = [session](PaneId) -> const void * { return session; },
        .key =
            [&out](PaneId pane, const KeyPress &press) {
                out.key_targets.push_back(pane);
                out.key_presses.push_back(press);
            },
        .text =
            [&out](PaneId pane, std::u32string_view text) {
                out.text_targets.push_back(pane);
                out.text_payloads.emplace_back(text);
            },
    };
}

/// @brief 端点没有身份读数的那一档：`stream` 缺省即不做去重，也不会被当成「已消失」。
[[nodiscard]] auto make_anonymous_sink(Deliveries &out) -> BroadcastRouter::Sink {
    BroadcastRouter::Sink sink = make_sink(nullptr, out);
    sink.stream = nullptr;
    return sink;
}

}  // namespace

AURORA_TEST_CASE(group_keeps_insertion_order_and_ignores_duplicates) {
    BroadcastGroup group;
    AURORA_TEST_CHECK(group.empty());
    AURORA_TEST_CHECK(group.add(BroadcastMember{.tab = 1, .pane = 11}));
    AURORA_TEST_CHECK(group.add(BroadcastMember{.tab = 2, .pane = 21}));
    AURORA_TEST_CHECK_FALSE(group.add(BroadcastMember{.tab = 1, .pane = 11}));

    // 重复加入不改变次序：横幅按插入序列名，后一次点击不该把该格挪到末尾。
    AURORA_TEST_REQUIRE_EQ(group.size(), 2U);
    AURORA_TEST_CHECK_EQ(group.members()[0].tab, 1U);
    AURORA_TEST_CHECK_EQ(group.members()[0].pane, 11U);
    AURORA_TEST_CHECK_EQ(group.members()[1].tab, 2U);
    AURORA_TEST_CHECK_EQ(group.members()[1].pane, 21U);
}

AURORA_TEST_CASE(toggle_flips_membership_and_reports_the_state_after_the_call) {
    BroadcastGroup group;
    const BroadcastMember member{.tab = 1, .pane = 11};
    AURORA_TEST_CHECK(group.toggle(member));
    AURORA_TEST_CHECK(group.contains(member));
    AURORA_TEST_CHECK_FALSE(group.toggle(member));
    AURORA_TEST_CHECK_FALSE(group.contains(member));
    AURORA_TEST_CHECK(group.empty());
}

AURORA_TEST_CASE(remove_tab_prunes_only_that_tab_and_returns_the_count) {
    BroadcastGroup group;
    group.add(BroadcastMember{.tab = 1, .pane = 11});
    group.add(BroadcastMember{.tab = 1, .pane = 12});
    group.add(BroadcastMember{.tab = 2, .pane = 21});

    AURORA_TEST_CHECK_EQ(group.remove_tab(1), 2U);
    AURORA_TEST_REQUIRE_EQ(group.size(), 1U);
    AURORA_TEST_CHECK_EQ(group.members()[0].tab, 2U);
    AURORA_TEST_CHECK_EQ(group.remove_tab(9), 0U);
}

AURORA_TEST_CASE(clear_reports_whether_anything_was_removed) {
    BroadcastGroup group;
    AURORA_TEST_CHECK_FALSE(group.clear());
    group.add(BroadcastMember{.tab = 1, .pane = 11});
    AURORA_TEST_CHECK(group.clear());
    AURORA_TEST_CHECK(group.empty());
}

AURORA_TEST_CASE(router_fans_out_to_every_other_member_across_tabs) {
    BroadcastRouter router;
    Deliveries one;
    Deliveries two;
    router.attach(1, make_anonymous_sink(one));
    router.attach(2, make_anonymous_sink(two));
    router.toggle(1, 11);
    router.toggle(2, 21);

    const KeyPress press = key(KeySym::Enter);
    router.dispatch_key(1, 11, press);

    // 源格由视口自己发过一份，路由不再回投；另一张标签的成员收一份。
    AURORA_TEST_CHECK(one.key_targets.empty());
    AURORA_TEST_REQUIRE_EQ(two.key_targets.size(), 1U);
    AURORA_TEST_CHECK_EQ(two.key_targets[0], 21U);
    AURORA_TEST_CHECK_EQ(two.key_presses[0].sym, press.sym);

    router.dispatch_key(2, 21, key(KeySym::B, true));
    AURORA_TEST_REQUIRE_EQ(one.key_targets.size(), 1U);
    AURORA_TEST_CHECK_EQ(one.key_targets[0], 11U);
    AURORA_TEST_CHECK_EQ(one.key_presses[0].sym, KeySym::B);
    AURORA_TEST_CHECK(one.key_presses[0].shift);
}

AURORA_TEST_CASE(router_ignores_sources_outside_the_group) {
    BroadcastRouter router;
    Deliveries one;
    Deliveries two;
    router.attach(1, make_anonymous_sink(one));
    router.attach(2, make_anonymous_sink(two));
    router.toggle(1, 11);
    router.toggle(2, 21);

    // 非成员格的输入不广播：组不是「所有标签互相镜像」，否则误点一下就把命令送进生产会话。
    router.dispatch_key(3, 31, key(KeySym::Enter));
    router.dispatch_text(3, 31, U"rm -rf /\n");
    AURORA_TEST_CHECK(one.key_targets.empty());
    AURORA_TEST_CHECK(two.key_targets.empty());
    AURORA_TEST_CHECK(one.text_targets.empty());
    AURORA_TEST_CHECK(two.text_targets.empty());
}

AURORA_TEST_CASE(members_sharing_one_session_receive_the_input_once) {
    BroadcastRouter router;
    Deliveries one;
    Deliveries two;
    Deliveries three;
    int shared_session = 0;
    int other_session = 0;
    router.attach(1, make_sink(&shared_session, one));
    router.attach(2, make_sink(&shared_session, two));
    router.attach(3, make_sink(&other_session, three));
    router.toggle(1, 11);
    router.toggle(2, 21);
    router.toggle(3, 31);

    // 装配层两格指向同一会话时，按成员各发一份会让一次键入写两遍；按会话去重后只到一份。
    router.dispatch_key(1, 11, key(KeySym::Enter));
    AURORA_TEST_CHECK(two.key_targets.empty());
    AURORA_TEST_REQUIRE_EQ(three.key_targets.size(), 1U);
    AURORA_TEST_CHECK_EQ(three.key_targets[0], 31U);

    // 反向同理：源格与另一成员共享会话，但该成员恰好是源自身，不得回投。
    router.dispatch_key(3, 31, key(KeySym::Enter));
    AURORA_TEST_REQUIRE_EQ(one.key_targets.size(), 1U);
    AURORA_TEST_CHECK_EQ(one.key_targets[0], 11U);
    AURORA_TEST_CHECK(two.key_targets.empty());
}

AURORA_TEST_CASE(router_skips_members_without_a_live_endpoint) {
    BroadcastRouter router;
    Deliveries source;
    Deliveries live;
    Deliveries vanished;
    int live_session = 0;
    router.attach(1, make_anonymous_sink(source));
    // 标签 2 没有端点：跨标签切走后工作区尚未登记，这一格只能静默不达。
    // 标签 3 的端点明确回了「会话已消失」：不许向不存在的 pane 盲发。
    router.attach(3, make_sink(nullptr, vanished));
    router.attach(4, make_sink(&live_session, live));
    router.toggle(1, 11);
    router.toggle(2, 21);
    router.toggle(3, 31);
    router.toggle(4, 41);

    router.dispatch_key(1, 11, key(KeySym::Enter));
    AURORA_TEST_CHECK(source.key_targets.empty());
    AURORA_TEST_CHECK(vanished.key_targets.empty());
    AURORA_TEST_REQUIRE_EQ(live.key_targets.size(), 1U);
    AURORA_TEST_CHECK_EQ(live.key_targets[0], 41U);
}

AURORA_TEST_CASE(reattaching_a_tab_replaces_the_endpoint_but_keeps_membership) {
    BroadcastRouter router;
    Deliveries before;
    Deliveries after;
    Deliveries replacement;
    router.attach(1, make_anonymous_sink(before));
    router.attach(2, make_anonymous_sink(after));
    router.toggle(1, 11);
    router.toggle(2, 21);

    // 会话重启会把工作区整份换掉，同格仍在组里；旧端点收不到新会话之外的任何东西。
    router.attach(1, make_anonymous_sink(replacement));
    AURORA_TEST_CHECK(router.contains(1, 11));
    router.dispatch_key(2, 21, key(KeySym::Enter));
    AURORA_TEST_CHECK(before.key_targets.empty());
    AURORA_TEST_REQUIRE_EQ(replacement.key_targets.size(), 1U);
    AURORA_TEST_CHECK_EQ(replacement.key_targets[0], 11U);
}

AURORA_TEST_CASE(change_hook_fires_only_when_the_member_set_really_changes) {
    BroadcastRouter router;
    std::size_t changes = 0;
    router.set_change_hook([&changes] { ++changes; });

    router.attach(1, BroadcastRouter::Sink{});
    AURORA_TEST_CHECK_EQ(changes, 0U);  // 换端点不刷新横幅

    router.toggle(1, 11);
    router.toggle(1, 11);
    router.toggle(1, 11);
    AURORA_TEST_CHECK_EQ(changes, 3U);

    router.remove(1, 12);  // 本就不在组内
    AURORA_TEST_CHECK_EQ(changes, 3U);
    router.remove(1, 11);
    AURORA_TEST_CHECK_EQ(changes, 4U);

    router.clear();  // 已空
    AURORA_TEST_CHECK_EQ(changes, 4U);
    router.toggle(1, 11);
    router.clear();
    AURORA_TEST_CHECK_EQ(changes, 6U);

    router.toggle(1, 11);
    router.remove_tab(1);
    AURORA_TEST_CHECK_EQ(changes, 8U);
}

AURORA_TEST_CASE(dispatch_text_fans_out_committed_text_with_the_same_boundary) {
    BroadcastRouter router;
    Deliveries one;
    Deliveries two;
    router.attach(1, make_anonymous_sink(one));
    router.attach(2, make_anonymous_sink(two));
    router.toggle(1, 11);
    router.toggle(2, 21);

    router.dispatch_text(1, 11, U"systemctl restart nginx\n");
    AURORA_TEST_CHECK(one.text_targets.empty());
    AURORA_TEST_REQUIRE_EQ(two.text_targets.size(), 1U);
    AURORA_TEST_CHECK_EQ(two.text_targets[0], 21U);
    AURORA_TEST_CHECK_EQ(two.text_payloads[0], std::u32string(U"systemctl restart nginx\n"));

    // 文本腿不向按键腿混发：两条通道在视口里编码方式不同，混发会多送一遍。
    AURORA_TEST_CHECK(one.key_targets.empty());
    AURORA_TEST_CHECK(two.key_targets.empty());
}

AURORA_TEST_CASE(unique_size_counts_distinct_sessions_not_members) {
    BroadcastRouter router;
    Deliveries scratch;
    int first = 0;
    int second = 0;
    router.attach(1, make_sink(&first, scratch));
    router.attach(2, make_sink(&first, scratch));
    router.attach(3, make_sink(&second, scratch));
    router.toggle(1, 11);
    router.toggle(2, 21);
    router.toggle(3, 31);
    router.toggle(4, 41);  // 标签 4 没有端点

    AURORA_TEST_CHECK_EQ(router.size(), 4U);
    // 两个成员指向同一会话只算一个；没有身份读数的成员各算一个（不知道就不能合并）。
    AURORA_TEST_CHECK_EQ(router.unique_size(), 3U);
    AURORA_TEST_CHECK_EQ(router.stream_of(BroadcastMember{.tab = 1, .pane = 11}), &first);
    AURORA_TEST_CHECK_EQ(router.stream_of(BroadcastMember{.tab = 4, .pane = 41}), nullptr);
}

}  // namespace borealis::test_cases::utest_broadcast
