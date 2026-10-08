#pragma once

// ============================================================
// 调试面板（src/ui/debug_panel.h）
// ------------------------------------------------------------
// `SPEC.NF.RELI.01` 的「调试面板可查」那一半：VT 解析器未知序列计数、非法字节序列计数
// （`SPEC.FEAT.TERM.09`）与背压水位（`SPEC.NF.PERF.06`）三族计数器一并上界面。
// 三条决定形态的 WHY：
//
//   1. **本件不认识 `Session`**：交进来的是 `DebugSessionSnapshot` 值聚合体，行源折算归装配层
//      （`src/main.cpp` 遍历 `tab_workspaces`）。面板若 include 会话头，ui→session 这条向就为一只
//      只读浮层而固化，而计数器的取数本就发生在主线程的一次快照上。
//   2. **对话框常驻、开合只翻可见性**：与 `StartupNotice` 的「一次启动弹一次」不同，本件可反复开关。
//      关闭态的 `Dialog` 不渲染、布局回零盒、不参与命中（框架头注自陈），故浮层槽位常驻没有呈现代价；
//      换来的是不在派发栈内动 `add_overlay` / `remove_overlay`（A4 与面板同口径）。内容每次打开经
//      `set_content` 重建——面板显示的是**打开那一刻**的快照，重开必须重取。
//   3. **焦点作用域按深度对账**：`Dialog::show()` 只在派发栈内压作用域，而 `F12` 那条命令的调用点
//      可能在栈内（快捷键派发）也可能在栈外（命令面板 / 用例编程调用），两种组合都要成立。故打开前
//      记 `scope_depth_before_`，`show()` 后深度没变才自补一次 `push_scope`；关闭侧只在「当前深度仍
//      高于那份读数」时弹回（与搜索浮层同一对账式，裁决 7.81②）。
//
// 数值一律是**取值**（`std::to_string`），上屏文案一律走本仓词条表（§4.3 第 14 条的中文例外）。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/event/focus.h"
#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"  ///< `OverlayHost` 声明在该头

namespace borealis::ui {

/// @brief 一个会话的计数器快照（`SPEC.NF.RELI.01` 的三族读数）。
///
/// 值聚合体：装配层从 `Session::parse_stats()` / `decode_stats()` / `queue_stats()` 逐字段搬来，
/// 面板拿到的是不可变快照，之后不再读任何会话状态。
struct DebugSessionSnapshot {
    std::string title;               ///< 行标题（装配层折好的取值，如「标签 3 · 分屏 1」的已解析串）
    std::uint64_t parse_ignored = 0;
    std::uint64_t parse_cancelled = 0;
    std::uint64_t decode_replaced = 0;
    std::uint64_t decode_code_points = 0;
    std::uint64_t queue_pending = 0;
    std::uint64_t queue_peak_pending = 0;
    std::uint64_t queue_overloads = 0;
    std::uint64_t queue_merges = 0;
    std::uint64_t queue_yields = 0;
};

/// @brief F12 呼出的只读诊断浮层：打开即取一份计数器快照。
class DebugPanel {
  public:
    /// @brief 绑定浮层宿主与焦点权威（装配层交 `app.focus()`，同 `StartupNotice`）。
    DebugPanel(aurora::OverlayHost &host, aurora::FocusManager &focus);

    DebugPanel(const DebugPanel &other) = delete;
    auto operator=(const DebugPanel &other) -> DebugPanel & = delete;
    DebugPanel(DebugPanel &&other) = delete;
    auto operator=(DebugPanel &&other) -> DebugPanel & = delete;
    ~DebugPanel() noexcept;

    /// @brief 开合切换：关着就用这份快照打开，开着就关掉。
    /// @param sessions 打开那一刻的计数器快照（开着时传入被忽略）。
    auto toggle(const std::vector<DebugSessionSnapshot> &sessions) -> void;

    /// @brief 当前承载的对话框（首次打开前为空）。
    [[nodiscard]] auto dialog() const noexcept -> aurora::Dialog * { return dialog_.get(); }

    /// @brief 面板当前是否开着。
    [[nodiscard]] auto is_showing() const noexcept -> bool { return dialog_ != nullptr && dialog_->is_open(); }

  private:
    aurora::OverlayHost &host_;
    aurora::FocusManager &focus_;
    std::shared_ptr<aurora::Dialog> dialog_;
    std::optional<std::size_t> overlay_index_;
    std::size_t scope_depth_before_ = 0;
    bool scope_pushed_ = false;  ///< 本件自补过作用域且尚未弹出（文件头③）
};

}  // namespace borealis::ui
