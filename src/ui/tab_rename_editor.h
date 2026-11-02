#pragma once

// ============================================================
// 标签就地重命名的胶囊浮层（src/ui/tab_rename_editor.h）
// ------------------------------------------------------------
// 屏 1 余项 (c)：`SPEC.FEAT.WS.01` 的标签条 + 交互稿 `codespec/UI_WORKSPACE_INTERACT.draft.md` 判据 ⑰。
// 私有头（裁决 D1① 同口径）：本件含框架类型，不进 `include/borealis/`。
//
// 分工：坐标只有一处算式，它在 `TabBarWidget::rename_target()`（与 `on_paint` 的名称区、`hit_test_at`
// 的命中同源），本件只消费那一份盒；文本、提交、撤销三件事才归本件。于是「胶囊落在名称那一截」与
// 「名称画在哪儿」不可能分叉，而本件不认识 `TabStrip`。
//
// 四条形态口径（裁决 **7.195**）：
// ① 入口是**双击名区**，不是判据 ⑰ 原文写的 `Ctrl+Shift+R`——那枚键位早被已 shipped 的
//    `session.restart` 吃掉（三处文档与按键面板都以它为准），换绑是破坏性变更。另登记一条**无缺省键**
//    的 `tab.rename` 命令，使 `SPEC.FEAT.PREF.04` 的可重绑面仍然成立（先例＝`workspace.close_window`，
//    裁决 7.161）；判据原文不删，只加更正注记（防伪死链）。
// ② 胶囊是**浮层**而不是子节点：`TabBarWidget` 是叶子控件（不是 `Container`），挂不进子树，故照
//    `SearchOverlay` 那一形态——`au::Popup` 挂本窗 `overlay_host`、内容是一只 `TextInput` 私有子类。
// ③ 三条收口（Enter 提交 / Esc 取消 / 点别处＝失焦提交）**只有一个提交点**，就是
//    `Popup::on_close` 那一条腿：收层即提交，除非那次收层是 Esc 发起的（`cancelled_`）。空串提交＝
//    撤销重命名（裁决 7.43③），本件不判空——判与不判归 `rename` 那一条腿，两处判就是两套口径。
// ④ 失焦那一条**不直接收层**：`Popup::close()` 的次序是 `pop_scope()` → `open_ = false` → `on_close()`，
//    在失焦回调里再呼一次 `close()` 就会二次 `pop_scope()`，弹掉的是**祖先**那一层作用域。故失焦腿只置
//    `pending_close_`，真正的收层交给逐帧 `sync()`——那里已在派发栈外，深度对账归本件自己（同
//    `SearchOverlay` 文件头③ 那条按深度对账的口径）。
//
// 逐帧 `sync()` 的另一条纪律：盒**逐位相同即不动**。`Popup::open_at` 无条件标脏（布局与绘制各一次），
// 每帧照呼就是把「浮层开着的那几十帧」全变成非空闲帧，违 `SPEC.NF.PERF.06` 的 UI 侧纪律（AGENTS §4.5
// 第 25 条）。盒变了也只改宽与锚点、**不重建树**——打字中的值与焦点因此不可能丢。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "aurora/core/types.h"

#include "tab_bar.h"  // RenameTarget / TabBarWidget（胶囊的盒与初值唯一来源）

namespace aurora {
class FocusManager;
class OverlayHost;
class Popup;
class TextInput;
}  // namespace aurora

namespace borealis::ui {

/// @brief 标签条上的就地重命名胶囊（一只定宽定高的 `TextInput` 浮层）。
///
/// 生命周期：装配层**按窗懒建一次、常驻**（与 `SearchOverlay` 同一条 F1-c 口径）；`begin()` 打开或改指
/// 另一格，`sync()` 每帧同步盒与待收层，`close()` 收层。摘浮层只在析构，次序按裁决 7.67。
class TabRenameEditor final {
  public:
    /// @brief 提交腿（装配层注入）：与 `TabBarHooks::rename` 同一条闭包，本件不自建第二份。
    ///
    /// 形参是**码点串**而不是 UTF-8：`TabStrip` 的名字坐标空间就是 `std::u32string`，转换在本件做一次
    /// 而不是让每一处调用方各转一遍。
    using Commit = std::function<void(std::uint64_t id, std::u32string name)>;

    /// @brief 建胶囊：造一只**空内容**的 `Popup` 挂上宿主并装好 `on_close`；胶囊体在第一次 `begin()` 才建。
    /// @param bar 发起本胶囊的那枚标签条（非拥有）：盒与初值只经它的 `rename_target()` 取。
    /// @param host 本窗的浮层宿主（非拥有）：其析构须晚于本件。
    /// @param focus 本窗的焦点管理器（非拥有）：`begin()` 在派发栈外补压作用域用它，按窗取而不是
    ///        `app.focus()`——拿主宿主那一份给第二扇窗接线就是「键入打不进第二扇窗」那条已知形态。
    /// @param commit 提交腿；空即不提交（本件仍会收层，只是不落字）。
    TabRenameEditor(const TabBarWidget &bar, aurora::OverlayHost &host, aurora::FocusManager &focus,
                    Commit commit);
    TabRenameEditor(const TabRenameEditor &other) = delete;
    auto operator=(const TabRenameEditor &other) -> TabRenameEditor & = delete;
    TabRenameEditor(TabRenameEditor &&other) = delete;
    auto operator=(TabRenameEditor &&other) -> TabRenameEditor & = delete;
    ~TabRenameEditor();

    /// @brief 打开胶囊编辑那一格：现取盒与显示名、把文本种进框里、焦点交给它、光标落尾。
    ///
    /// 取不到盒（栏未布局、那一格已不在表里、或名称区宽归零）即**不弹**——猜一个坐标就是把胶囊画在
    /// 错误的位置上。已开着时这一句改指另一格：不重建树、不重复压作用域。
    auto begin(std::uint64_t id) -> void;

    /// @brief 收层：走 `Popup::close()` 那一条真路径，于是提交、作用域对账、状态清零都在同一处。
    auto close() -> void;

    /// @brief 装配层逐帧调一次：处理失焦后的待收层、格消失即收、盒变了就重锚。
    ///
    /// 关闭态第一句就早返回，于是「没在改名」的帧不为这一件花任何代价（`Window::is_idle_frame()` 的
    /// 前提之一）。
    auto sync() -> void;

    /// @brief 是否开着（不另存一份 bool：两处读数迟早会分叉，与 `SearchOverlay::is_open()` 同一条）。
    [[nodiscard]] auto is_open() const noexcept -> bool;

    /// @brief 当前在编辑哪一格；空＝没在编辑（用例与装配层据此判「胶囊这一条腿有没有认领这一格」）。
    [[nodiscard]] auto editing() const noexcept -> std::optional<std::uint64_t> { return id_; }

    /// @brief 浮层本体的只读句柄（用例按 `content_bounds()` 取锚点与实测尺寸）。
    [[nodiscard]] auto popup() const noexcept -> const aurora::Popup * { return popup_.get(); }

    /// @brief 胶囊里那只输入框的只读句柄（用例读 `value()`、按键走真实派发，不从这里动内部状态）。
    [[nodiscard]] auto input() const noexcept -> aurora::TextInput * { return input_.get(); }

  private:
    /// @brief 唯一的收层落点（`Popup::on_close`）：作用域对账、按 `cancelled_` 决定提不提、清三件状态。
    auto on_popup_closed() -> void;

    /// @brief 建胶囊体：`RenameInput` + 七件 chrome 交接 + 定宽定高。
    /// @param target 当前那一格的盒与显示名（初值取 `current_name`）。
    [[nodiscard]] auto build_pill(const RenameTarget &target) -> std::shared_ptr<aurora::TextInput>;

    /// @brief 把盒下发给浮层与输入框；**逐位相同即不动**（见文件头那条 idle-frame 纪律）。
    auto apply_box(const aurora::Rect &box) -> void;

    /// @brief 光标落尾且不落选区（稿那枚光标在名称之后、名下没有高亮档）。
    auto place_caret_at_end() -> void;

    const TabBarWidget &bar_;         ///< 非拥有：盒与初值的唯一来源。
    aurora::OverlayHost &host_;       ///< 非拥有：本件的浮层挂在这里。
    aurora::FocusManager &focus_;     ///< 非拥有：栈外打开时补压作用域用的那个管理器。
    Commit commit_;                   ///< 提交腿：与 `TabBarHooks::rename` 同源的那一条闭包。
    std::shared_ptr<aurora::Popup> popup_;            ///< 常驻浮层（建成即登记，析构才摘除）。
    std::shared_ptr<aurora::TextInput> input_{};      ///< 胶囊体（打开那一次建，重开换档不重建）。
    std::optional<std::size_t> overlay_index_{};      ///< 宿主浮层序号；`add_overlay` 在空宿主上回空。
    std::optional<std::uint64_t> id_{};               ///< 当前编辑那一格；空＝没在编辑。
    std::optional<aurora::Rect> box_{};               ///< 已下发给浮层的那一份盒（逐帧差分的基准）。
    std::size_t scope_depth_before_ = 0;              ///< 打开那一次读到的作用域深度，关闭时按它对账。
    bool pending_close_ = false;    ///< 失焦已提交、等 `sync()` 收层（不能在失焦回调里直接收，见文件头④）。
    bool cancelled_ = false;        ///< 那一次收层是 Esc / 格消失发起的：不提交。
};

}  // namespace borealis::ui
