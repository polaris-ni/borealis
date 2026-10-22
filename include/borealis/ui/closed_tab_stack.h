#pragma once

// ============================================================
// 闭包栈（include/borealis/ui/closed_tab_stack.h，公共头）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.10` 的纯逻辑载体：记录最近关闭的标签连接规格与位置，供
// Ctrl+Shift+T 重开时重建会话。本头不含 Aurora 类型也不含 config 类型；
// 档案侧只认 `conn/profile.h` 的领域类型（同 `workspace_snapshot.h`）。
// ============================================================

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "borealis/conn/profile.h"

namespace borealis::ui {

/// @brief 闭包标签走哪条连接腿（需求原文：本地终端重连进程、SSH 按档案重连）。
enum class ClosedTabLeg : std::uint8_t {
    Local,  ///< 本地终端：按 `local_shell` / `startup_directory` 重起。
    Ssh,    ///< SSH：按 `profile_id` 回查档案重拨；档案已删则用 `ssh` 快照兜底。
};

/// @brief 闭包标签的连接规格（本地 shell / SSH 档案）。
///
/// 需求写死「内容不可恢复」，故只保留重建连接所需的最小信息；scrollback 与屏幕内容在关闭时丢弃。
struct ClosedTabSpec {
    std::string name;                                          ///< 关闭前的标签名（UTF-8）
    std::size_t index_in_strip = 0;                            ///< 关闭前在标签条的位置
    std::string local_shell;                                   ///< 本地终端的 shell 路径（空＝非本地）
    std::string startup_directory;                             ///< 本地终端的启动目录
    ClosedTabLeg leg = ClosedTabLeg::Local;                    ///< 连接腿；只有 Local / Ssh 两档
    std::string profile_id;                                    ///< SSH 档案 id；本地腿为空
    std::optional<conn::SshProfile> ssh;                       ///< SSH 档案快照（已删档案与 quick connect 的兜底）
    std::chrono::steady_clock::time_point closed_at =          ///< 关闭时间戳（用于超时清理）
        std::chrono::steady_clock::now();
};

/// @brief 撤销重开该用哪一份 SSH 档案：id 命中且同型优先，未命中回快照。
///
/// 单一判定点（与 `workspace_snapshot` 的 id／字段双写同一口径）：档案还在就以配置里那一份为准
/// （改名、改主机后撤销回来的是**现在**的档案），档案已删或 quick connect 那种不落表的临时档案
/// 才用关闭时的快照。非 SSH 腿与无快照可兜底时回空值——调用方据此不做任何事。
[[nodiscard]] auto resolve_closed_ssh_profile(const std::vector<conn::Profile> &profiles,
                                              const ClosedTabSpec &spec)
    -> std::optional<conn::Profile>;

/// @brief 闭包栈：上限 10，满则丢弃最旧的条目。
class ClosedTabStack {
public:
    /// @brief 压入一个刚关闭的标签。
    void push(ClosedTabSpec spec);

    /// @brief 弹出最近关闭的标签（若栈空返回 nullopt）。
    [[nodiscard]] auto pop() -> std::optional<ClosedTabSpec>;

    /// @brief 栈是否为空。
    [[nodiscard]] auto empty() const -> bool { return stack_.empty(); }

    /// @brief 当前栈深。
    [[nodiscard]] auto size() const -> std::size_t { return stack_.size(); }

private:
    static constexpr std::size_t kMaxDepth = 10;
    std::deque<ClosedTabSpec> stack_;  // 前端是最旧，后端是最新
};

}  // namespace borealis::ui
