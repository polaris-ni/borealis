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

auto resolve_closed_ssh_profile(const std::vector<conn::Profile> &profiles,
                                const ClosedTabSpec &spec) -> std::optional<conn::Profile> {
    if (spec.leg != ClosedTabLeg::Ssh) {
        return std::nullopt;
    }
    // id 命中**且同型**才认配置里那一份：档案被改成别的类型之后不能再拿它拨 SSH，此时
    // 落到快照兜底（没有快照就回空值）。空 id 不参与回查——quick connect 的临时档案从不落表。
    if (!spec.profile_id.empty()) {
        for (const auto &profile : profiles) {
            if (profile.id == spec.profile_id && profile.type == conn::ConnectionType::Ssh) {
                return profile;
            }
        }
    }
    if (!spec.ssh.has_value()) {
        return std::nullopt;
    }
    conn::Profile restored;
    restored.id = spec.profile_id;
    restored.name = spec.name;
    restored.type = conn::ConnectionType::Ssh;
    restored.ssh = *spec.ssh;
    return restored;
}

}  // namespace borealis::ui
