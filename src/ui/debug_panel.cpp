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
    label->modifier.set(aurora::Modifier{}.fill_max_width());
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(label))};
}

/// @brief 数值 → 上屏取值串（数字不属词条，也不属 ASCII 诊断文案规则）。
[[nodiscard]] auto count_text(std::uint64_t value) -> aurora::LocalizedString {
    return aurora::LocalizedString{std::to_string(value)};
}

/// @brief 一个会话的四行（解析 / 解码 / 发送 / 队列），行标题单独一行。
auto append_session_rows(const DebugSessionSnapshot &session, const aurora::Color color, std::vector<aurora::Node> &out)
    -> void {
    // 标题是装配层**已解析**的显示串（含中文措辞），这里交字面档而不再查表。
    out.push_back(make_text(aurora::LocalizedString{session.title}, color, kBodySizePt, true));
    out.push_back(make_text(settings_text("diagnostics.parse",
                                          {count_text(session.parse_ignored), count_text(session.parse_cancelled)}),
                            color,
                            kBodySizePt));
    out.push_back(make_text(settings_text("diagnostics.decode",
                                          {count_text(session.decode_replaced), count_text(session.decode_code_points)}),
                            color,
                            kBodySizePt));
    out.push_back(make_text(settings_text("diagnostics.encode", {count_text(session.encode_unrepresentable)}),
                            color,
                            kBodySizePt));
    out.push_back(make_text(settings_text("diagnostics.queue",
                                          {count_text(session.queue_pending),
                                           count_text(session.queue_peak_pending),
                                           count_text(session.queue_overloads),
                                           count_text(session.queue_merges),
                                           count_text(session.queue_yields)}),
                            color,
                            kBodySizePt));
}

}  // namespace

DebugPanel::DebugPanel(aurora::OverlayHost &host, aurora::FocusManager &focus) : host_(host), focus_(focus) {}

DebugPanel::~DebugPanel() noexcept {
    // 析构次序＝声明次序的倒序：先还焦点作用域，再放对话框句柄，最后摘浮层（裁决 7.67 的时序）。
    if (scope_pushed_) {
        focus_.pop_scope();
        scope_pushed_ = false;
    }
    dialog_.reset();
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
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
            if (scope_pushed_ && focus_.scope_depth() > scope_depth_before_) {
                focus_.pop_scope();
            }
            scope_pushed_ = false;
        });
        // 宿主必须已有基础内容，否则 `add_overlay` 回空而浮层永远摘不掉（裁决 7.56⑥）。
        // 常驻而不随开合摘建：关闭态的 `Dialog` 不渲染也不参与命中，摘建反而要在派发栈内动子树。
        overlay_index_ = host_.add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    }

    const aurora::Theme chrome = settings_chrome_theme();
    std::vector<aurora::Node> rows;
    if (sessions.empty()) {
        rows.push_back(make_text(settings_text("diagnostics.empty"), chrome.text, kBodySizePt));
    } else {
        rows.reserve(sessions.size() * 4U);
        for (const DebugSessionSnapshot &session : sessions) {
            append_session_rows(session, chrome.text, rows);
        }
    }
    auto list = std::make_shared<aurora::Column>(aurora::ColumnProps{.children = std::move(rows), .gap = 2.0F});
    list->modifier.set(aurora::Modifier{}.fill_max_width());
    auto scroller = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
        .child = aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(list))}});
    scroller->modifier.set(aurora::Modifier{}.fill_max_width().height(kListHeightDp));

    std::vector<aurora::Node> children;
    children.push_back(make_text(settings_text("diagnostics.title"), chrome.text, kTitleSizePt, true));
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(scroller))});
    auto close_button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_text("settings.close"),
        .color = chrome.primary,
        .on_color = chrome.on_primary,
        .min_width = 120.0F});
    close_button->set_on_click([this]() -> void {
        // 只关框，不在派发栈内摘浮层（面板本体与降级对话框同口径）。
        if (dialog_ != nullptr) {
            dialog_->close();
        }
    });
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(close_button))});

    auto box = std::make_shared<aurora::Column>(aurora::ColumnProps{.children = std::move(children), .gap = kGapDp});
    box->modifier.set(aurora::Modifier{}.width(kBoxWidthDp)
                          .background(chrome.background, 8.0F)
                          .padding(aurora::EdgeInsets{
                              .left = kBoxPaddingDp,
                              .top = kBoxPaddingDp,
                              .right = kBoxPaddingDp,
                              .bottom = kBoxPaddingDp}));
    dialog_->set_content(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(box))});

    // 文件头③：调用点可能在派发栈内（快捷键真派发）也可能在栈外（命令面板 / 用例编程调用），
    // 前者 `show()` 自压作用域，后者只置位——按深度差判定，两种入口都不双压。
    scope_depth_before_ = focus_.scope_depth();
    dialog_->show();
    if (focus_.scope_depth() == scope_depth_before_) {
        focus_.push_scope(dialog_.get());
        scope_pushed_ = true;
    }
}

}  // namespace borealis::ui
