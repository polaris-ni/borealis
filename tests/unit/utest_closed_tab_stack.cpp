/// 测试类型: unit
/// 目标单元: include/borealis/ui/closed_tab_stack.h，src/ui/closed_tab_stack.cpp
/// 测试说明: `SPEC.FEAT.WS.10` 的撤销栈（LIFO、空栈弹出回空值、深度 10 满则丢最旧）。
///           栈深判据刻意取「恰好越界的一批」而不是两条：丢错端（丢最新）与整条丢弃
///           （push 直接拒收）在两条例子里都会现形，而只比长度比不出**留下的是哪十个**。

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "borealis/ui/closed_tab_stack.h"
#include "framework/aurora_test.h"

namespace borealis::test_cases::utest_closed_tab_stack {

using borealis::ui::ClosedTabSpec;
using borealis::ui::ClosedTabStack;

namespace {

[[nodiscard]] auto spec_named(std::string name, std::size_t index) -> ClosedTabSpec {
    ClosedTabSpec spec;
    spec.name = std::move(name);
    spec.index_in_strip = index;
    return spec;
}

}  // namespace

AURORA_TEST_CASE(a_new_stack_is_empty_and_pops_nothing) {
    ClosedTabStack stack;
    AURORA_TEST_CHECK_TRUE(stack.empty());
    AURORA_TEST_CHECK_EQ(stack.size(), 0U);
    AURORA_TEST_CHECK_FALSE(stack.pop().has_value());  // 空栈弹出不是崩溃也不是默认构造的幽灵条目
}

AURORA_TEST_CASE(pop_returns_the_most_recently_closed_first) {
    ClosedTabStack stack;
    stack.push(spec_named("first", 0));
    stack.push(spec_named("second", 1));
    stack.push(spec_named("third", 2));

    const auto third = stack.pop();
    AURORA_TEST_REQUIRE_TRUE(third.has_value());
    AURORA_TEST_CHECK_EQ(third->name, "third");
    AURORA_TEST_CHECK_EQ(third->index_in_strip, 2U);

    const auto second = stack.pop();
    AURORA_TEST_REQUIRE_TRUE(second.has_value());
    AURORA_TEST_CHECK_EQ(second->name, "second");

    AURORA_TEST_CHECK_EQ(stack.size(), 1U);
}

AURORA_TEST_CASE(popped_spec_carries_the_connection_fields_through) {
    // 重开侧靠这两个字段重建连接（shell 与启动目录），中途被搬丢即「重开回来落在错的目录」。
    ClosedTabStack stack;
    ClosedTabSpec spec;
    spec.name = "tab A";
    spec.local_shell = "/usr/bin/zsh";
    spec.startup_directory = "/tmp";
    spec.index_in_strip = 4;
    stack.push(std::move(spec));

    const auto got = stack.pop();
    AURORA_TEST_REQUIRE_TRUE(got.has_value());
    AURORA_TEST_CHECK_EQ(got->local_shell, "/usr/bin/zsh");
    AURORA_TEST_CHECK_EQ(got->startup_directory, "/tmp");
    AURORA_TEST_CHECK_EQ(got->index_in_strip, 4U);
    AURORA_TEST_CHECK_EQ(got->name, "tab A");
}

AURORA_TEST_CASE(the_depth_cap_evicts_the_oldest_not_the_newest) {
    ClosedTabStack stack;
    for (std::size_t i = 0; i < 11; ++i) {
        stack.push(spec_named("t" + std::to_string(i), i));
    }
    AURORA_TEST_CHECK_EQ(stack.size(), 10U);  // 第 11 次压入不增长，也**不拒绝**（新的必须进栈）

    // 弹出的次序必须是 t10..t1，被丢掉的那条是 t0（最旧）。
    std::vector<std::string> drained;
    while (const auto got = stack.pop()) {
        drained.push_back(got->name);
    }
    AURORA_TEST_REQUIRE_EQ(drained.size(), 10U);
    AURORA_TEST_CHECK_EQ(drained[0], "t10");
    AURORA_TEST_CHECK_EQ(drained[1], "t9");
    AURORA_TEST_CHECK_EQ(drained[9], "t1");  // 栈底剩的是 t1 而不是 t0 ⇒ 丢的恰是最旧
    AURORA_TEST_CHECK_TRUE(stack.empty());
}

AURORA_TEST_CASE(pushing_after_the_cap_stays_at_the_cap) {
    // 上限是常驻约束而不是「第一次越界」：越界之后继续关标签，栈深恒 10 而内容整体往前挪。
    ClosedTabStack stack;
    for (std::size_t i = 0; i < 20; ++i) {
        stack.push(spec_named("t" + std::to_string(i), i));
    }
    AURORA_TEST_CHECK_EQ(stack.size(), 10U);
    const auto got = stack.pop();
    AURORA_TEST_REQUIRE_TRUE(got.has_value());
    AURORA_TEST_CHECK_EQ(got->name, "t19");
}

}  // namespace borealis::test_cases::utest_closed_tab_stack
