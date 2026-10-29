#include "debug_panel.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aurora/core/color.h"
#include "aurora/i18n/localized_string.h"
#include "aurora/modifier/modifier.h"
#include "aurora/widget/button.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/text.h"

#include "settings_i18n.h"
#include "settings_panel.h"

namespace borealis::ui {
namespace {

constexpr float kBoxWidthDp = 480.0F;
constexpr float kBoxPaddingDp = 24.0F;
constexpr float kGapDp = 10.0F;
constexpr float kTitleSizePt = 18.0F;
constexpr float kBodySizePt = 14.0F;
/// @brief 计数区的可见高。`Scroll` 容器自身取父约束给出的视口尺寸（其头注自陈），不锁高就会随
///        会话数长到溢出窗口——与降级对话框那条回落键列表同一条物理事实。
constexpr float kListHeightDp = 260.0F;

[[nodiscard]] auto make_text(aurora::LocalizedString text, aurora::Color color, float size_pt, bool bold = false)
    -> aurora::Node {
    auto label = std::make_shared<aurora::Text>(aurora::TextProps{
        .content = std::move(text),
        .font = aurora::Font{.size_pt = size_pt, .weight = bold ? 700 : 400},
        .text_color = color});
    label->set_modifier(aurora::Modifier{}.fill_max_width());
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(label))};
}

/// @brief 计数 → 模板整数参数（数字不属词条，也不属 ASCII 诊断文案规则）。
///
/// 引擎对裸 `{name}` 的整数腿走 `std::to_string`（`src/aurora/i18n/message_engine.cpp` 的
/// `arg_text`），与旧形态自己拼串上屏逐字同形；面板上的计数一律是 `std::uint64_t`，而
/// `MessageArg` 的整数腿只认 `std::int64_t`（与 `double` 腿同候选时必须显式定型），故收在此一处。
[[nodiscard]] auto count_arg(std::string_view name, std::uint64_t value) -> aurora::MessageArg {
    return aurora::MessageArg{std::string{name}, static_cast<std::int64_t>(value)};
}

/// @brief 一个会话的四行（解析 / 解码 / 发送 / 队列），行标题单独一行。
auto append_session_rows(const DebugSessionSnapshot &session, const aurora::Color color, std::vector<aurora::Node> &out)
    -> void {
    // 标题是装配层**已解析**的显示串（含中文措辞），这里交字面档而不再查表。
    out.push_back(make_text(aurora::LocalizedString{session.title}, color, kBodySizePt, true));
    out.push_back(make_text(settings_text("diagnostics.parse",
                                          {count_arg("ignored", session.parse_ignored),
                                           count_arg("interrupted", session.parse_cancelled)}),
                            color,
                            kBodySizePt));
    out.push_back(make_text(settings_text("diagnostics.decode",
                                          {count_arg("bytes", session.decode_replaced),
                                           count_arg("code_points", session.decode_code_points)}),
                            color,
                            kBodySizePt));
    out.push_back(make_text(settings_text("diagnostics.encode", {count_arg("count", session.encode_unrepresentable)}),
                            color,
                            kBodySizePt));
    out.push_back(make_text(settings_text("diagnostics.queue",
                                          {count_arg("pending", session.queue_pending),
                                           count_arg("peak", session.queue_peak_pending),
                                           count_arg("overloads", session.queue_overloads),
                                           count_arg("merges", session.queue_merges),
                                           count_arg("yields", session.queue_yields)}),
                            color,
                            kBodySizePt));
}

}  // namespace

DebugPanel::DebugPanel(aurora::OverlayHost &host, aurora::FocusManager &focus) : host_(&host), focus_(&focus) {}

// 常驻浮层的搬窗（头文件那条判据的落点）：摘与挂都在这一步做完，中间不经过 `toggle()`——后者是「关着就
// 用新快照打开」，搬窗要的恰恰是「别在这里打开」。次序固定为先收作用域再摘浮层：`Dialog::close()` 的
// on_close 腿自己弹过一次，剩下的那一次（从未派发栈内打开过的那种）必须用**旧的**焦点权威弹回去，换了
// 指针再弹就是往另一扇窗的栈里少弹一层。
auto DebugPanel::set_overlay_destination(aurora::OverlayHost &host, aurora::FocusManager &focus) -> void {
    if (host_ == &host && focus_ == &focus) {
        return;
    }
    if (dialog_ != nullptr && dialog_->is_open()) {
        // 与 `StartupNotice` 的唯一差别：本件搬完是**关闭态**。它的作用域记账带着一枚按旧栈深度取的下标
        //（`scope_depth_before_`），带着还开着的框搬过去会让那次比较跨栈；而诊断内容本就是「打开那一刻取
        // 一份快照」，到新窗重按一次 F12 才是它的设计。
        dialog_->close();
    }
    if (scope_pushed_) {
        focus_->pop_scope();
        scope_pushed_ = false;
    }
    const bool had_overlay = overlay_index_.has_value();
    if (had_overlay) {
        host_->remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    host_ = &host;
    focus_ = &focus;
    if (had_overlay && dialog_ != nullptr) {
        overlay_index_ = host_->add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    }
}

// 散窗兜底腿（头文件那条销毁序决定的另一条形态）：三本账一律**清零**而不是摘回——旧宿主与旧焦点栈都随
// 那一扇窗一起散了，这里既无处可摘也无处可弹，能做的只有承认「上一处的东西已经不在了」。
auto DebugPanel::abandon_overlay_destination(aurora::OverlayHost &host, aurora::FocusManager &focus) -> void {
    if (host_ == &host && focus_ == &focus) {
        return;
    }
    scope_pushed_ = false;
    overlay_index_.reset();
    // 句柄必须一起放：否则下一次 `toggle()` 走「句柄已在、只切显隐」那条腿，而切开的这份对话框并没有挂在
    // 新宿主上——面板此后再也不出现。放句柄不会回调 `on_close`（框架只在 `close()` 末尾调它），因此也不会
    // 拿着新焦点权威去少弹一层。
    dialog_.reset();
    host_ = &host;
    focus_ = &focus;
}

DebugPanel::~DebugPanel() noexcept {
    // 析构次序＝声明次序的倒序：先还焦点作用域，再放对话框句柄，最后摘浮层（裁决 7.67 的时序）。
    if (scope_pushed_) {
        focus_->pop_scope();
        scope_pushed_ = false;
    }
    dialog_.reset();
    if (overlay_index_.has_value()) {
        host_->remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
}

auto DebugPanel::toggle(const std::vector<DebugSessionSnapshot> &sessions) -> void {
    if (is_showing()) {
        dialog_->close();
        return;
    }
    if (dialog_ == nullptr) {
        dialog_ = std::make_shared<aurora::Dialog>();
        dialog_->set_on_close([this]() -> void {
            // 只在对账仍欠一次弹出时补弹（文件头③）：`Dialog::close()` 在派发栈内自己弹过的那一次
            // 不留在这里重复处理，而栈外从未弹过的情形由析构兜底。
            if (scope_pushed_ && focus_->scope_depth() > scope_depth_before_) {
                focus_->pop_scope();
            }
            scope_pushed_ = false;
        });
        // 宿主必须已有基础内容，否则 `add_overlay` 回空而浮层永远摘不掉（裁决 7.56⑥）。
        // 常驻而不随开合摘建：关闭态的 `Dialog` 不渲染也不参与命中，摘建反而要在派发栈内动子树。
        overlay_index_ = host_->add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    }

    const aurora::Theme chrome = settings_chrome_theme();
    std::vector<aurora::Node> rows;
    if (sessions.empty()) {
        rows.push_back(make_text(settings_text("diagnostics.empty"), chrome.roles.on_surface, kBodySizePt));
    } else {
        rows.reserve(sessions.size() * 4U);
        for (const DebugSessionSnapshot &session : sessions) {
            append_session_rows(session, chrome.roles.on_surface, rows);
        }
    }
    auto list = std::make_shared<aurora::Column>(aurora::ColumnProps{.children = std::move(rows), .gap = 2.0F});
    list->set_modifier(aurora::Modifier{}.fill_max_width());
    auto scroller = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
        .child = aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(list))}});
    scroller->set_modifier(aurora::Modifier{}.fill_max_width().height(kListHeightDp));

    std::vector<aurora::Node> children;
    children.push_back(make_text(settings_text("diagnostics.title"), chrome.roles.on_surface, kTitleSizePt, true));
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(scroller))});
    auto close_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_text("settings.close"),
        .color = chrome.roles.primary,
        .on_color = chrome.roles.on_primary,
        .min_width = 120.0F});
    close_button->set_on_click([this]() -> void {
        // 只关框，不在派发栈内摘浮层（面板本体与降级对话框同口径）。
        if (dialog_ != nullptr) {
            dialog_->close();
        }
    });
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(close_button))});

    auto box = std::make_shared<aurora::Column>(aurora::ColumnProps{.children = std::move(children), .gap = kGapDp});
    box->set_modifier(aurora::Modifier{}.width(kBoxWidthDp)
                          .background(chrome.roles.surface, 8.0F)
                          .padding(aurora::EdgeInsets{
                              .left = kBoxPaddingDp,
                              .top = kBoxPaddingDp,
                              .right = kBoxPaddingDp,
                              .bottom = kBoxPaddingDp}));
    dialog_->set_content(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(box))});

    // 文件头③：调用点可能在派发栈内（快捷键真派发）也可能在栈外（命令面板 / 用例编程调用），
    // 前者 `show()` 自压作用域，后者只置位——按深度差判定，两种入口都不双压。
    scope_depth_before_ = focus_->scope_depth();
    dialog_->show();
    if (focus_->scope_depth() == scope_depth_before_) {
        focus_->push_scope(dialog_.get());
        scope_pushed_ = true;
    }
}

}  // namespace borealis::ui
