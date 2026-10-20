#include "borealis/ui/broadcast.h"

#include <algorithm>
#include <utility>

namespace borealis::ui {

auto BroadcastGroup::add(BroadcastMember member) -> bool {
    if (contains(member)) {
        return false;
    }
    members_.push_back(member);
    return true;
}

auto BroadcastGroup::remove(BroadcastMember member) -> bool {
    const auto found = std::find(members_.begin(), members_.end(), member);
    if (found == members_.end()) {
        return false;
    }
    members_.erase(found);
    return true;
}

auto BroadcastGroup::toggle(BroadcastMember member) -> bool {
    if (remove(member)) {
        return false;
    }
    members_.push_back(member);
    return true;
}

auto BroadcastGroup::remove_tab(TabId tab) -> std::size_t {
    const auto before = members_.size();
    std::erase_if(members_, [tab](const BroadcastMember &member) -> bool { return member.tab == tab; });
    return before - members_.size();
}

auto BroadcastGroup::clear() -> bool {
    if (members_.empty()) {
        return false;
    }
    members_.clear();
    return true;
}

auto BroadcastGroup::contains(BroadcastMember member) const -> bool {
    return std::find(members_.begin(), members_.end(), member) != members_.end();
}

auto BroadcastRouter::attach(TabId tab, Sink sink) -> void {
    // 覆盖式登记而不是拒绝重复：`replace_tab_session` 会把同一标签的工作区整份换掉，端点必须能
    // 换到新工作区，而成员资格（按 `(tab, pane)`）要原样保留——这正是「会话重启后同格仍在组里」。
    sinks_.insert_or_assign(tab, std::move(sink));
}

auto BroadcastRouter::remove_tab(TabId tab) -> void {
    const bool detached = sinks_.erase(tab) > 0U;
    const bool pruned = group_.remove_tab(tab) > 0U;
    if (detached || pruned) {
        notify_changed();
    }
}

auto BroadcastRouter::toggle(TabId tab, PaneId pane) -> bool {
    const bool member = group_.toggle(BroadcastMember{.tab = tab, .pane = pane});
    notify_changed();
    return member;
}

auto BroadcastRouter::remove(TabId tab, PaneId pane) -> void {
    if (group_.remove(BroadcastMember{.tab = tab, .pane = pane})) {
        notify_changed();
    }
}

auto BroadcastRouter::clear() -> void {
    if (group_.clear()) {
        notify_changed();
    }
}

auto BroadcastRouter::contains(TabId tab, PaneId pane) const -> bool {
    return group_.contains(BroadcastMember{.tab = tab, .pane = pane});
}

auto BroadcastRouter::stream_of(BroadcastMember member) const -> const void * {
    const auto sink = sinks_.find(member.tab);
    if (sink == sinks_.end() || !sink->second.stream) {
        return nullptr;
    }
    return sink->second.stream(member.pane);
}

auto BroadcastRouter::unique_size() const -> std::size_t {
    std::vector<const void *> seen;
    std::size_t count = 0;
    for (const BroadcastMember &member : group_.members()) {
        const void *stream = stream_of(member);
        if (stream != nullptr) {
            if (std::find(seen.begin(), seen.end(), stream) != seen.end()) {
                continue;
            }
            seen.push_back(stream);
        }
        ++count;
    }
    return count;
}

auto BroadcastRouter::dispatch_key(TabId tab, PaneId pane, const term::KeyPress &press) -> void {
    fan_out(BroadcastMember{.tab = tab, .pane = pane}, &press, std::u32string_view{});
}

auto BroadcastRouter::dispatch_text(TabId tab, PaneId pane, std::u32string_view text) -> void {
    fan_out(BroadcastMember{.tab = tab, .pane = pane}, nullptr, text);
}

auto BroadcastRouter::set_change_hook(std::function<void()> hook) -> void { change_hook_ = std::move(hook); }

auto BroadcastRouter::fan_out(BroadcastMember source, const term::KeyPress *press, std::u32string_view text)
    -> void {
    // 非成员发出的输入不广播：广播组不是「所有标签的输入都互相镜像」，而是「成员格的输入发给成员」。
    if (!group_.contains(source)) {
        return;
    }
    // 源会话已经由源视口自己发过一份，故先把它计为已送达；同一会话被另一个成员指向时不再重发。
    std::vector<const void *> delivered;
    if (const void *source_stream = stream_of(source); source_stream != nullptr) {
        delivered.push_back(source_stream);
    }
    for (const BroadcastMember &member : group_.members()) {
        if (member == source) {
            continue;
        }
        const auto sink = sinks_.find(member.tab);
        if (sink == sinks_.end()) {
            continue;
        }
        const auto &endpoint = sink->second;
        const void *stream = endpoint.stream ? endpoint.stream(member.pane) : nullptr;
        if (endpoint.stream && stream == nullptr) {
            continue;  // 端点明确回了「已消失」：不向不存在的 pane 盲发
        }
        if (stream != nullptr) {
            if (std::find(delivered.begin(), delivered.end(), stream) != delivered.end()) {
                continue;
            }
            delivered.push_back(stream);
        }
        if (press != nullptr) {
            if (endpoint.key) {
                endpoint.key(member.pane, *press);
            }
        } else if (endpoint.text) {
            endpoint.text(member.pane, text);
        }
    }
}

auto BroadcastRouter::notify_changed() -> void {
    if (change_hook_) {
        change_hook_();
    }
}

}  // namespace borealis::ui
