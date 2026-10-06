#include "startup_notice.h"

#include <filesystem>
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

#include "borealis/config/store.h"
#include "settings_i18n.h"
#include "settings_panel.h"

namespace borealis::ui {
namespace {

/// @brief 排版量（dp 与 pt）。框宽取固定值而不是对话框给的 0.8 宽：正文因此有确定的换行宽度，
///        不会因窗口宽窄把同一句话排成不同行数。
constexpr float kBoxWidthDp = 480.0F;
constexpr float kBoxPaddingDp = 24.0F;
constexpr float kGapDp = 12.0F;
constexpr float kTitleSizePt = 18.0F;
constexpr float kBodySizePt = 14.0F;
/// @brief 回落键列表的可见高。`Scroll` 容器自身取父约束给出的视口尺寸（其头注自陈），不锁高就会随
///        内容长到溢出窗口——这与 `Dropdown::panel_box` 那条无上限限制是同一族物理事实。
constexpr float kRejectedListHeightDp = 160.0F;

/// @brief 路径 → 上屏文本。
///
/// 走 `u8string()` 而不是 `string()`：后者在 Windows 上按当前 ANSI 码页转换，非 ASCII 路径会碎成一串
/// 问号，而框架的文本入口要的是 UTF-8。路径是**取值**不是字面量，故不受 ASCII 字面量规则约束。
[[nodiscard]] auto path_text(const std::filesystem::path &path) -> std::string {
    const std::u8string utf8 = path.u8string();
    return std::string{reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

/// @brief 造一段只读文本（标题、正文与值行共用同一入口）。
/// @param text 待显示串：词条经 `settings_text()` 交出、算出来的值直接包 `LocalizedString`。
/// @param color 文字色（一律取 chrome 主题的那一份，见 `settings_chrome_theme()`）。
/// @param size_pt 字号。
/// @param bold 是否加粗（只有标题用）。
[[nodiscard]] auto make_text(aurora::LocalizedString text,
                             aurora::Color color,
                             float size_pt,
                             bool bold = false) -> aurora::Node {
    auto label = std::make_shared<aurora::Text>(aurora::TextProps{
        .content = std::move(text),
        .font = aurora::Font{.size_pt = size_pt, .weight = bold ? 700 : 400},
        .text_color = color});
    label->modifier.set(aurora::Modifier{}.fill_max_width());
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(label))};
}

/// @brief 该不该弹：`LoadOutcome` 四态里只有两条降级态弹（判据文 §7，裁决 7.26④）。
///
/// `FirstRun` 是「压根没有配置文件」，`Loaded` 是「读出来了（可能部分键回落）」，两者都不该在启动路径上
/// 打断用户；回落键的清单另有界面的两处文字表达（行尾角标与组顶说明），见 `SettingsPanel::visible_notes()`。
[[nodiscard]] auto should_show(config::LoadOutcome outcome) -> bool {
    return outcome == config::LoadOutcome::RecoveredCorrupt || outcome == config::LoadOutcome::RecoveredVersion;
}

}  // namespace

StartupNotice::StartupNotice(aurora::OverlayHost &host, aurora::FocusManager &focus)
    : host_(host), focus_(focus) {}

StartupNotice::~StartupNotice() noexcept {
    // 析构次序＝声明次序的倒序：先还焦点作用域，再放本件自持的对话框句柄，最后摘浮层。
    // 后两条的顺序是 G34 回货那条时序（裁决 7.67）——先摘树再放句柄会让那些控件正落在
    // 「活在容器之外被持有」那一档，此后每子刷一条 WARN。
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

auto StartupNotice::show_if_needed(const config::LoadReport &report) -> bool {
    if (shown_ || !should_show(report.outcome)) {
        return false;
    }
    shown_ = true;

    const aurora::Theme chrome = settings_chrome_theme();
    std::vector<aurora::Node> children;
    if (report.outcome == config::LoadOutcome::RecoveredCorrupt) {
        children.push_back(
            make_text(settings_text("settings.startup.corrupt.title"), chrome.text, kTitleSizePt, true));
        children.push_back(make_text(settings_text("settings.startup.corrupt.body"), chrome.text, kBodySizePt));
    } else {
        children.push_back(
            make_text(settings_text("settings.startup.version.title"), chrome.text, kTitleSizePt, true));
        // 两个版本号都是**取值**：文件自报的那一档可能高于本仓能读的最高档，只报本程序支持的档位
        // 就说不清差了多少，而 `LoadReport::message` 是 ASCII 诊断、不能上界面（文件头④）。
        children.push_back(make_text(settings_text("settings.startup.version.body",
                                                   {aurora::LocalizedString{
                                                        report.stored_schema_version
                                                            ? std::to_string(*report.stored_schema_version)
                                                            : std::string{}},
                                                    aurora::LocalizedString{std::to_string(
                                                        config::kSupportedSchemaVersion)}}),
                                     chrome.text,
                                     kBodySizePt));
    }
    if (report.corrupt_backup.has_value()) {
        children.push_back(make_text(settings_text("settings.startup.backup",
                                                   {aurora::LocalizedString{path_text(*report.corrupt_backup)}}),
                                     chrome.text,
                                     kBodySizePt));
    }
    if (report.writes_refused) {
        // 「不静默清空」的界面表达：备份未成功时本会话拒绝写这个文件，界面上必须说出这一点，
        // 否则用户以为改动能存住，而此后每一次落盘都会被拒绝。
        children.push_back(
            make_text(settings_text("settings.startup.writes_refused"), chrome.text, kBodySizePt));
    }
    if (!report.rejected_keys.empty()) {
        children.push_back(
            make_text(settings_text("settings.startup.rejected"), chrome.text, kBodySizePt));
        std::vector<aurora::Node> keys;
        keys.reserve(report.rejected_keys.size());
        for (const std::string &key : report.rejected_keys) {
            keys.push_back(make_text(aurora::LocalizedString{key}, chrome.text, kBodySizePt));
        }
        auto list = std::make_shared<aurora::Column>(
            aurora::ColumnProps{.children = std::move(keys), .gap = 2.0F});
        list->modifier.set(aurora::Modifier{}.fill_max_width());
        auto scroller = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
            .child = aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(list))}});
        scroller->modifier.set(aurora::Modifier{}.fill_max_width().height(kRejectedListHeightDp));
        children.push_back(
            aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(scroller))});
    }

    auto ack = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_text("settings.startup.ack"),
        .color = chrome.primary,
        .on_color = chrome.on_primary,
        .min_width = 120.0F});
    ack->set_on_click([this]() -> void {
        // 只关框，不在派发栈内摘浮层：`remove_overlay` 会析构正在派发的子树（A4 与面板同口径）。
        if (dialog_ != nullptr) {
            dialog_->close();
        }
    });
    children.push_back(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(ack))});

    auto box = std::make_shared<aurora::Column>(aurora::ColumnProps{.children = std::move(children), .gap = kGapDp});
    box->modifier.set(aurora::Modifier{}.width(kBoxWidthDp)
                          .background(chrome.background, 8.0F)
                          .padding(aurora::EdgeInsets{
                              .left = kBoxPaddingDp,
                              .top = kBoxPaddingDp,
                              .right = kBoxPaddingDp,
                              .bottom = kBoxPaddingDp}));

    dialog_ = std::make_shared<aurora::Dialog>(aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(box))});
    // `close()` 在派发栈内（用户点「知道了」）自己会弹一次作用域，此后本件就不该再弹；
    // 回调排在 `close()` 的弹栈之后，故这里清标记正是那个配对点。
    dialog_->set_on_close([this]() -> void { scope_pushed_ = false; });
    // 宿主必须已有基础内容，否则 `add_overlay` 回 `nullopt` 而浮层永远摘不掉（裁决 7.56⑥）。
    // 交进去的是同一份 `shared_ptr` 的副本而非对话框的拷贝：本件要继续持有它（`is_showing()` 与
    // 「知道了」回调都读它），而 `Node` 持的正是这份所有权句柄。
    overlay_index_ = host_.add_overlay(aurora::Node{std::static_pointer_cast<aurora::Widget>(dialog_)});
    dialog_->show();
    // 文件头③：装载发生在 `main` 早期、不在任何派发栈内，`show()` 因此只置位而不压作用域。
    // 补这一次，Tab 焦点才关在框内、焦点落在「知道了」上。
    focus_.push_scope(dialog_.get());
    scope_pushed_ = true;
    return true;
}

}  // namespace borealis::ui
