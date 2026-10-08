#include "borealis/ui/closed_tab_stack.h"

namespace borealis::ui {

void ClosedTabStack::push(ClosedTabSpec spec) {
    // 上限 10：满则丢弃最旧的（deque 前端）
    if (stack_.size() >= kMaxDepth) {
        stack_.pop_front();
    }
    stack_.push_back(std::move(spec));
}

auto ClosedTabStack::pop() -> std::optional<ClosedTabSpec> {
    if (stack_.empty()) {
        return std::nullopt;
    }
    auto spec = std::move(stack_.back());
    stack_.pop_back();
    return spec;
}

}  // namespace borealis::ui
