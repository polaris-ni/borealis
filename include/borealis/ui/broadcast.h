#pragma once

// ============================================================
// 广播组：成员集合与跨标签扇出（include/borealis/ui/broadcast.h）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.09` 的纯逻辑半：谁在组里、一次输入该发给谁。视觉（26 dp 横幅与 pane 底部 pill）、
// 视口重编码与装配层端点登记都不在这里，见 `codespec/UI_WORKSPACE_BROADCAST.draft.md`。
//
// 三条决定形态的口径：
// - **成员是 `(TabId, PaneId)` 复合身份**：`PaneId` 在每个工作区内从 1 起、只增不复用，单用它会
//   在跨标签时撞号；`TabId` 由装配层分配，恰好是跨标签的唯一键。
// - **扇出按会话身份去重，而不是按成员**：本仓装配层的分屏工厂今天仍返回首视图（WS.02 的欠账），
//   两个 pane 可能指向同一会话；按成员各发一份会让一次键入变成两次写入（`rm -rf` 跑两遍）。
//   端点因此要交一个只用于判等的身份读数（`Sink::stream`），本件不解引用它。
// - **纯逻辑、无 Aurora 类型**：本件只依赖 `term::KeyPress` 与两个整数标识，故扇出策略可在没有
//   窗口、没有真实会话的用例里逐条断言；视口与标签的生命周期留在装配层。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string_view>
#include <vector>

#include "borealis/term/keymap.h"
#include "borealis/ui/pane_tree.h"
#include "borealis/ui/tab_strip.h"

namespace borealis::ui {

/// @brief 广播组的一名成员：标签 + 该标签内的工作区 pane。
struct BroadcastMember {
    TabId tab = 0;
    PaneId pane = 0;

    /// @brief 逐字段全等比较（集合判据与用例断言用）。
    [[nodiscard]] auto operator==(const BroadcastMember &other) const noexcept -> bool = default;
};

/// @brief 组内成员的有序集合：插入序即横幅的列名序，重复加入不改变次序。
///
/// 允许只有一名成员：跨标签组装必须先在第一张标签里留下第一名成员，否则切到第二张标签时组已经
/// 散了。单成员期间扇出为空，行为与普通输入完全相同。
class BroadcastGroup final {
  public:
    /// @brief 加入一名成员；已在组内时返回 false 且次序不变。
    /// @param member 目标成员。
    auto add(BroadcastMember member) -> bool;

    /// @brief 移除一名成员。
    /// @param member 目标成员。
    /// @return 是否真的移除了一项。
    auto remove(BroadcastMember member) -> bool;

    /// @brief 切换一名成员的资格（第十四键的语义）。
    /// @param member 目标成员。
    /// @return 调用后该成员**是否在组内**。
    auto toggle(BroadcastMember member) -> bool;

    /// @brief 移除某标签的全部成员（关标签时一次清账）。
    /// @param tab 目标标签。
    /// @return 被移除的成员数。
    auto remove_tab(TabId tab) -> std::size_t;

    /// @brief 清空整组。
    /// @return 清空前的组是否非空。
    auto clear() -> bool;

    /// @brief 该成员是否在组内。
    /// @param member 目标成员。
    [[nodiscard]] auto contains(BroadcastMember member) const -> bool;

    /// @brief 当前成员数。
    [[nodiscard]] auto size() const noexcept -> std::size_t { return members_.size(); }

    /// @brief 组是否为空。
    [[nodiscard]] auto empty() const noexcept -> bool { return members_.empty(); }

    /// @brief 成员表，按加入次序。
    [[nodiscard]] auto members() const noexcept -> const std::vector<BroadcastMember> & { return members_; }

  private:
    std::vector<BroadcastMember> members_;
};

/// @brief 跨标签扇出：登记每个标签的送达端点，按组把一次输入发给其余成员。
///
/// 不拥有任何视口或会话：`Sink` 的三条回调由装配层用弱引用闭包造出，本件只存可调用体。这样
/// 工作区被替换（会话重启）时不会在路由器里留下悬空指针；装配层漏了撤账也只会静默不达。
class BroadcastRouter final {
  public:
    /// @brief 一个标签的送达端点。
    struct Sink {
        /// @brief 该 pane 当前会话的身份读数，只用于判等；回 nullptr 表示端点已消失。
        ///        未提供该回调时本件不做去重（视为每个成员各是一个独立会话）。
        std::function<const void *(PaneId)> stream;
        /// @brief 把一次按键交给该 pane 的会话（由视口按**自己的**模式重编码）。
        std::function<void(PaneId, const term::KeyPress &)> key;
        /// @brief 把一段已提交文本交给该 pane 的会话（由视口按**自己的**编码重发）。
        std::function<void(PaneId, std::u32string_view)> text;
    };

    /// @brief 登记/覆盖一个标签的端点；成员资格不变（会话重启后工作区整份换掉走这条）。
    /// @param tab 目标标签。
    /// @param sink 端点；空回调即该腿静默。
    auto attach(TabId tab, Sink sink) -> void;

    /// @brief 撤掉一个标签的端点并移除它的全部成员（关标签时调用）。
    /// @param tab 目标标签。
    auto remove_tab(TabId tab) -> void;

    /// @brief 切换一名成员的资格。
    /// @param tab 成员所属标签。
    /// @param pane 成员 pane。
    /// @return 调用后该成员是否在组内。
    auto toggle(TabId tab, PaneId pane) -> bool;

    /// @brief 移除一名成员（关 pane 时调用）。
    /// @param tab 成员所属标签。
    /// @param pane 成员 pane。
    auto remove(TabId tab, PaneId pane) -> void;

    /// @brief 清空整组（横幅点击的语义）；端点登记保留。
    auto clear() -> void;

    /// @brief 该 pane 是否在组内。
    [[nodiscard]] auto contains(TabId tab, PaneId pane) const -> bool;

    /// @brief 成员表，按加入次序。
    [[nodiscard]] auto members() const noexcept -> const std::vector<BroadcastMember> & {
        return group_.members();
    }

    /// @brief 成员数（不是去重后的会话数）。
    [[nodiscard]] auto size() const noexcept -> std::size_t { return group_.size(); }

    /// @brief 某成员当前会话的身份读数；端点缺失或已消失时回 nullptr。
    /// @param member 目标成员。
    [[nodiscard]] auto stream_of(BroadcastMember member) const -> const void *;

    /// @brief 去重后的会话数：同一会话被两个成员指向时只算一个。
    [[nodiscard]] auto unique_size() const -> std::size_t;

    /// @brief 一次按键的扇出：只接受**成员**来源，跳过源成员与同会话成员。
    /// @param tab 来源标签。
    /// @param pane 来源 pane。
    /// @param press 已由来源视口编码成功的那一次按键；同伴会按自己的模式重编码。
    auto dispatch_key(TabId tab, PaneId pane, const term::KeyPress &press) -> void;

    /// @brief 一次已提交文本的扇出；边界与 `dispatch_key` 相同。
    /// @param tab 来源标签。
    /// @param pane 来源 pane。
    /// @param text 已提交文本；同伴会按自己的编码重发。
    auto dispatch_text(TabId tab, PaneId pane, std::u32string_view text) -> void;

    /// @brief 装「成员集合真的变了」的钩子；装配层据此刷新横幅与 pill。
    ///
    /// 只在 `toggle` / `remove` / `remove_tab` / `clear` 造成实际变化时触发一次。`attach` 不算变化。
    /// @param hook 变更回调；传空函数即撤销。
    auto set_change_hook(std::function<void()> hook) -> void;

  private:
    /// @brief 共同的扇出路径：`press` 非空走按键腿，否则走文本腿。
    auto fan_out(BroadcastMember source, const term::KeyPress *press, std::u32string_view text) -> void;

    /// @brief 成员集合真变时叫醒钩子（钩子为空即什么都不做）。
    auto notify_changed() -> void;

    std::map<TabId, Sink> sinks_;
    BroadcastGroup group_;
    std::function<void()> change_hook_;
};

}  // namespace borealis::ui
