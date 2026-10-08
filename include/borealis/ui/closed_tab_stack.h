#pragma once

// ============================================================
// 闭包栈（include/borealis/ui/closed_tab_stack.h，公共头）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.10` 的纯逻辑载体：记录最近关闭的标签连接规格与位置，供
// Ctrl+Shift+T 重开时重建会话。本头不含 Aurora 类型也不含 config 类型。
// ============================================================

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

namespace borealis::ui {

/// @brief 闭包标签的连接规格（本地 shell / SSH 档案等）。
///
/// 需求写死「内容不可恢复」，故只保留重建连接所需的最小信息；scrollback 与屏幕内容在关闭时丢弃。
struct ClosedTabSpec {
    std::string name;                                          ///< 关闭前的标签名
    std::size_t index_in_strip = 0;                            ///< 关闭前在标签条的位置
    std::string local_shell;                                   ///< 本地终端的 shell 路径（空＝非本地）
    std::string startup_directory;                             ///< 本地终端的启动目录
    // TODO(SPEC.FEAT.CONN.02): SSH 档案引用字段随 SSH 腿到货追加
    std::chrono::steady_clock::time_point closed_at =          ///< 关闭时间戳（用于超时清理）
        std::chrono::steady_clock::now();
};

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
