// ============================================================
// 标签列表实现（src/ui/tab_strip.cpp）
// ------------------------------------------------------------
// 纯逻辑：无 Aurora 类型、无 IO、无时钟，故每条判据都可在无 UI 环境里断言。
// 形态与口径见公共头与裁决 7.43。
// ============================================================

#include "borealis/ui/tab_strip.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace borealis::ui {

auto resolve_tab_name(const TabNames &names, TabNamePriority priority) -> std::u32string {
    // 「空串即让位」是三个来源共用的唯一规则；档案名排在最后，故它为空时结果就是空串。
    const auto &first = priority == TabNamePriority::OscWins ? names.osc_title : names.manual_name;
    const auto &second = priority == TabNamePriority::OscWins ? names.manual_name : names.osc_title;
    if (!first.empty()) {
        return first;
    }
    if (!second.empty()) {
        return second;
    }
    return names.default_name;
}

auto TabStrip::count() const noexcept -> std::size_t {
    return tabs_.size();
}

auto TabStrip::tabs() const -> std::vector<Tab> {
    return tabs_;
}

auto TabStrip::index_of(TabId id) const -> std::optional<std::size_t> {
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

auto TabStrip::has_tab(TabId id) const -> bool {
    return index_of(id).has_value();
}

auto TabStrip::add(TabId id, std::u32string default_name) -> bool {
    if (has_tab(id)) {
        return false;
    }
    tabs_.push_back(Tab{.id = id, .names = TabNames{.default_name = std::move(default_name)}});
    selected_index_ = tabs_.size() - 1;  // 新建即切换。
    return true;
}

auto TabStrip::insert_at(std::size_t index, TabId id, std::u32string default_name) -> bool {
    if (has_tab(id)) {
        return false;
    }
    if (index > tabs_.size()) {
        return false;  // 越界：合法范围 [0, count()]，等于 count() 时等价于追加。
    }
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(index),
                 Tab{.id = id, .names = TabNames{.default_name = std::move(default_name)}});
    selected_index_ = index;  // 插入即选中。
    return true;
}

auto TabStrip::close(TabId id) -> bool {
    const auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    if (tabs_.size() <= 1) {
        return false;  // 最后一个不归本件关（关窗口属 SPEC.FEAT.WS.03）。
    }

    // 交接要在移除**之前**的次序里定：关掉的是中间格时下一格顶上来（下标不变），关掉末位时退一格。
    const std::size_t from = *at;
    const bool was_selected = from == selected_index_;
    // 记录关闭信息供 WS.10 撤销用：名称与位置都在移除前取，否则次序会变。
    last_closed_ = LastClosedInfo{tabs_[from].names.manual_name.empty()
                                      ? (tabs_[from].names.osc_title.empty() ? tabs_[from].names.default_name
                                                                             : tabs_[from].names.osc_title)
                                      : tabs_[from].names.manual_name,
                                  from};
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
    if (was_selected) {
        selected_index_ = std::min(from, tabs_.size() - 1);  // 移除后至少还剩一格，故 `- 1` 不空转。
    } else if (from < selected_index_) {
        --selected_index_;  // 选中的那格因为前面少了一格而整体左移。
    }
    return true;
}

auto TabStrip::selected() const -> std::optional<TabId> {
    if (tabs_.empty()) {
        return std::nullopt;
    }
    return tabs_[selected_index_].id;
}

auto TabStrip::select(TabId id) -> bool {
    const auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    selected_index_ = *at;
    return true;
}

auto TabStrip::select_relative(int step) -> bool {
    if (step == 0 || tabs_.empty()) {
        return false;
    }
    const auto size = static_cast<int>(tabs_.size());
    // 取模两次而不是一次：`step` 可以是任意负数（连按多次反向），而 C++ 的 `%` 结果随被除数符号。
    auto target = (static_cast<int>(selected_index_) + step) % size;
    if (target < 0) {
        target += size;
    }
    // 整圈回来与单标签下的任何步长都落在原格：返回值是「是否发生切换」，故这种情形算没切。
    if (target == static_cast<int>(selected_index_)) {
        return false;
    }
    selected_index_ = static_cast<std::size_t>(target);
    return true;
}

auto TabStrip::move(TabId id, std::size_t to_index) -> bool {
    const auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    // 目标基准是「移除被拖那格之后」的次序，故合法上界是 count-1（即落到表尾）。
    if (to_index >= tabs_.size()) {
        return false;
    }

    const std::size_t from = *at;
    const TabId selected_id = tabs_[selected_index_].id;  // 先记身份：下标在这场移动里不作数。
    if (from == to_index) {
        return true;  // 原地落点。
    }

    const Tab moved = tabs_[from];
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to_index), moved);

    // 选中位跟着被选中那格走：重排是本件唯一会让选中格换下标的动作，故按身份重新定下标。
    // 这里不查 `index_of` 而直接扫一遍，是因为本件此刻恒有一格命中（没删任何标签），查不到即
    // 不变量被破坏，写出来会多一条不可能路径的分支。
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].id == selected_id) {
            selected_index_ = i;
            break;
        }
    }
    return true;
}

auto TabStrip::rename(TabId id, std::u32string name) -> bool {
    const auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    tabs_[*at].names.manual_name = std::move(name);
    return true;
}

auto TabStrip::set_osc_title(TabId id, std::u32string title) -> bool {
    const auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    tabs_[*at].names.osc_title = std::move(title);
    return true;
}

auto TabStrip::last_closed() const -> std::optional<LastClosedInfo> {
    return last_closed_;
}

auto TabStrip::mark_bell_triggered(TabId id) -> bool {
    auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    tabs_[*at].bell_triggered = true;
    return true;
}

auto TabStrip::take_bell_triggered(TabId id) -> bool {
    auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    bool was = tabs_[*at].bell_triggered;
    tabs_[*at].bell_triggered = false;
    return was;
}

auto TabStrip::mark_activity(TabId id) -> bool {
    auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    tabs_[*at].has_activity = true;
    return true;
}

auto TabStrip::take_activity(TabId id) -> bool {
    auto at = index_of(id);
    if (!at.has_value()) {
        return false;
    }
    bool was = tabs_[*at].has_activity;
    tabs_[*at].has_activity = false;
    return was;
}

}  // namespace borealis::ui
