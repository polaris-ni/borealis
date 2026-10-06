#pragma once

// ============================================================
// 启动降级提示（src/ui/startup_notice.h）
// ------------------------------------------------------------
// `SPEC.FEAT.PREF.07` 的损坏降级线在界面上的那一半（裁决 7.26④ 提前到首版，形态随裁决 7.68④ / 7.76⑤）。
// 四条决定形态的 WHY：
//
//   1. **收 `const config::LoadReport &` 而不是收一份散字段**：「`LoadOutcome` 四态里哪两态弹」是本件的
//      判据本体，而 `src/main.cpp` 没有测试面（装配层不被任何用例驱动）。分支落在本件才可能被集成用例
//      真判。本头是 `src/` 下的私有头（裁决 D1① 同口径），`config` 侧从不 include `src/ui/*`，
//      故 ui→config 这一向不构成模块环（环禁只钉 `include/borealis/` 的公共头）。
//   2. **`au::Dialog` + 本仓自建 content，而不用 `aurora::alert` / `confirm`**：那两个工厂的按钮标签是
//      字面量、标题正文收 `const std::string &`，整条链无 i18n 入口，与 S13①「按 `LoadOutcome` 取本仓
//      中文词条」相违（裁决 7.68⑤ 的实测结论）。
//   3. **`FocusManager` 由装配层显式注入**：`Dialog::show()` 只在 `current_focus_manager() != nullptr`
//      时压焦点作用域，而那个线程局部只在**派发栈内**有效；装载发生在 `main` 早期，`show()` 因此只走
//      「仅置位」分支，Tab 焦点不关在框内。框架给栈外动作用的通道是 `resolve_focus_manager(Widget &)`，
//      但它会惰性造一份「按根缓存的进程级兜底实例」——那份可能不是 `app.focus()`，压进它就等于压进
//      一个没人读的栈。故由装配层交 `app.focus()` 进来，本件在 `show()` 之后自行补一次 `push_scope`。
//   4. **`LoadReport::message` 不上界面**：它是 ASCII 英文诊断（AGENTS.md §4.3 第 14 条的诊断文案不属
//      中文例外），显示在中文界面上即违那条规则（S13①）；调用方拿本件的返回值决定要不要把它写进日志。
//      同理 `rejected_keys` 那一段按装载侧现状**结构上到不了**（`RecoveredCorrupt` 与 `RecoveredVersion`
//      两条路都在填该表之前就返回），仍照 S13① 落实现并由用例手工构造的 `LoadReport` 驱动（裁决 7.76⑤）。
//
// 浮层的挂载与撤除沿用面板那条时序（裁决 7.67）：先放本件自持的控件句柄，再 `remove_overlay`。
// ============================================================

#include <cstddef>
#include <memory>
#include <optional>

#include "aurora/event/focus.h"
#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"  ///< `OverlayHost` 声明在该头

#include "borealis/config/store.h"

namespace borealis::ui {

/// @brief 启动路径上的降级对话框：装载结论交进来，该弹就弹且只弹一次。
///
/// 生命周期归装配层（一个栈上的局部对象）：它持有浮层序号与对话框句柄，析构即把浮层摘掉。
class StartupNotice {
  public:
    /// @brief 绑定浮层宿主与焦点权威。
    /// @param host 场景根的 `OverlayHost`（面板、右键菜单与预览浮层共用同一个宿主）。
    /// @param focus 窗口的焦点管理器（装配层交 `app.focus()`；见文件头③）。
    StartupNotice(aurora::OverlayHost &host, aurora::FocusManager &focus);

    StartupNotice(const StartupNotice &other) = delete;
    auto operator=(const StartupNotice &other) -> StartupNotice & = delete;
    StartupNotice(StartupNotice &&other) = delete;
    auto operator=(StartupNotice &&other) -> StartupNotice & = delete;
    ~StartupNotice() noexcept;

    /// @brief 按装载结论弹框。
    ///
    /// 幂等：一次启动进程里最多弹一次，第二次调用不做任何事。`FirstRun` 与 `Loaded` 一律不弹
    /// （正常装载的部分键回落只留痕在日志与「设置」面板里，不该在启动路径上打断用户）。
    /// @param report `config::Store::report()` 的装载结论。
    /// @return 本次调用是否真的弹了（调用方据此把 `message` 写进日志，「哪两态弹」因此只有一个判定点）。
    auto show_if_needed(const config::LoadReport &report) -> bool;

    /// @brief 当前承载的对话框（未弹过时为空）。
    [[nodiscard]] auto dialog() const noexcept -> aurora::Dialog * {
        return dialog_.get();
    }

    /// @brief 对话框当前是否开着（用户按「知道了」之后为假）。
    [[nodiscard]] auto is_showing() const noexcept -> bool {
        return dialog_ != nullptr && dialog_->is_open();
    }

  private:
    aurora::OverlayHost &host_;
    aurora::FocusManager &focus_;
    std::shared_ptr<aurora::Dialog> dialog_;
    std::optional<std::size_t> overlay_index_;
    bool scope_pushed_ = false;  ///< 本件压过焦点作用域且尚未弹出（`close()` 在派发栈内自己弹）
    bool shown_ = false;         ///< 本次启动已经弹过（幂等闸）
};

}  // namespace borealis::ui
