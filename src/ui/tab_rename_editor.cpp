// ============================================================
// 标签就地重命名胶囊实现（src/ui/tab_rename_editor.cpp）
// ------------------------------------------------------------
// 第六个触达 `au::Painter` / `au::Widget` 的翻译单元（私有头形态同裁决 D1①）。本件只做「盒的搬运 +
// 三条收口的对账」：坐标一律向 `TabBarWidget::rename_target()` 取，改名落库只有 `commit_` 一条腿。
// ============================================================

#include "tab_rename_editor.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "aurora/core/log.h"
#include "aurora/core/types.h"
#include "aurora/event/event.h"
#include "aurora/event/focus.h"
#include "aurora/event/keycode.h"
#include "aurora/modifier/modifier.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text_input.h"
#include "borealis/term/utf8.h"
#include "settings_i18n.h"
#include "settings_panel.h"

namespace borealis::ui {
namespace {

/// @brief 码点接收端：把解出的码点依次收进串里（与 `search_overlay.cpp` / 视口侧同名件同形——那是
///        匿名命名空间里的文件内件，跨 TU 复用就得把它提进公共头，而本件只需要这一次转换）。
class StringCollector final : public term::CodePointSink {
  public:
    explicit StringCollector(std::u32string &into) : into_(&into) {}

    auto on_code_point(char32_t code_point) -> void override { into_->push_back(code_point); }

  private:
    std::u32string *into_ = nullptr;
};

/// @brief 输入框给的 UTF-8 → 码点流（`TabStrip` 的名字坐标空间）。
[[nodiscard]] auto to_code_points(std::string_view utf8) -> std::u32string {
    std::u32string out;
    out.reserve(utf8.size());  // 纯 ASCII 时即为上界
    StringCollector collector{out};
    term::Utf8Decoder decoder;
    decoder.feed(std::as_bytes(std::span<const char>{utf8.data(), utf8.size()}), collector);
    return out;
}

/// @brief 码点串 → 输入框吃的 UTF-8（把显示名种进框里）。
[[nodiscard]] auto to_utf8(const std::u32string &code_points) -> std::string {
    std::string out;
    // 码点是本件自己从 UTF-8 解出来的，故「不可表示码点数」那个返回值结构上没有消费者。
    static_cast<void>(term::encode_utf8(code_points, out));
    return out;
}

/// @brief 把共享指针包成节点（与搜索浮层、设置面板同一形态）。
template <typename W>
[[nodiscard]] auto node(std::shared_ptr<W> widget) -> aurora::Node {
    return aurora::Node{std::static_pointer_cast<aurora::Widget>(std::move(widget))};
}

/// @brief 胶囊里那只输入框的私有子类：只为认下 `Esc` 与「失焦」这两条收口。
///
/// 框架 `TextInput` 的键盘逻辑只有 Enter 提交与退格，既没有 `Escape` 分支也没有失焦回调，而判据 ⑰ 的
/// 三条收口里两条正是这两件事。与搜索浮层的 `SearchInput`、设置面板的 `BlurCommitText` 同一档接法
/// （裁决 7.52 的 S14：失焦才提交也是这么接的）。
class RenameInput final : public aurora::TextInput {
  public:
    /// @brief 以属性聚合与两条收口回调构造。
    /// @param props 输入框属性（初值与字号；占位不给——空名就该是一只空框，而占位还会充当无障碍名）。
    /// @param on_escape `Esc` 那一条腿（取消：置 `cancelled_` 后待收层）。
    /// @param on_blur 失焦那一条腿（提交：只置待收层旗标，真正收层交逐帧 `sync()`，见头文件头④）。
    RenameInput(const aurora::TextInputProps &props, std::function<void()> on_escape,
                std::function<void()> on_blur)
        : aurora::TextInput(props), on_escape_(std::move(on_escape)), on_blur_(std::move(on_blur)) {}

    /// @brief 键盘派发：`Esc` 由本件吃掉并转成「取消」，其余一律交回基类编辑逻辑。
    /// @param e 键盘事件（消费时置 `is_handled`）。
    auto on_key_event(aurora::KeyEvent &e) -> void override {
        if (e.action == aurora::KeyAction::Down && e.key == static_cast<int>(aurora::KeyCode::Escape) &&
            is_focused()) {
            e.is_handled = true;
            on_escape_();
            return;
        }
        aurora::TextInput::on_key_event(e);
    }

    /// @brief 焦点变更：失焦即那条提交腿的起点。基类实现照走，聚焦态绘制才跟着换档。
    /// @param focused true = 获得焦点；false = 失去焦点。
    auto on_focus_change(bool focused) -> void override {
        aurora::TextInput::on_focus_change(focused);
        if (!focused) {
            on_blur_();
        }
    }

  private:
    std::function<void()> on_escape_;
    std::function<void()> on_blur_;
};

}  // namespace

TabRenameEditor::TabRenameEditor(const TabBarWidget &bar, aurora::OverlayHost &host,
                                 aurora::FocusManager &focus, Commit commit)
    : bar_(bar), host_(host), focus_(focus), commit_(std::move(commit)) {
    popup_ = std::make_shared<aurora::Popup>();
    popup_->set_on_close([this]() -> void { on_popup_closed(); });
    // 「点别处」这一条**不**交给浮层自己：`Popup::handle_outside_click` 命中框外就呼 `close()`，于是
    // 它和 `FocusManager` 的失焦腿在同一次 Press 里各收一次层，第二次 `pop_scope` 弹掉的是祖先那一层。
    // 关掉它之后，点别处只剩失焦这一条路（`EventDispatcher` 在派发 Press 前就把焦点给了新目标），
    // 收口回到「只有 `on_close` 一个提交点」那条口径上。
    popup_->set_dismiss_on_outside_click(false);
    // 建成即登记、胶囊体留到第一次 `begin()`：`Popup` 的每个入口都在 `!open_ || !child_` 上早返回，
    // 故一只未打开且无内容的浮层是哑节点（与搜索浮层 F1-c 的常驻同一条理由）。
    overlay_index_ = host_.add_overlay(node(popup_));
}

TabRenameEditor::~TabRenameEditor() {
    // 裁决 7.67 的次序：先收还在世的作用域，再放本件自持的控件句柄，最后摘浮层。
    // 这里**不**呼 `popup_->close()`：那会在析构序列里走一次 `on_close` ⇒ 提交一条用户从没打算提交的
    // 名字。窗口关闭时浮层随宿主一起消失，本件的提交腿也就到此为止。
    if (is_open() && focus_.scope_depth() > scope_depth_before_) {
        focus_.pop_scope();
    }
    input_.reset();
    popup_.reset();
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
}

auto TabRenameEditor::begin(std::uint64_t id) -> void {
    const std::optional<RenameTarget> target = bar_.rename_target(id);
    if (!target.has_value()) {
        // 取不到盒即不弹：猜一个坐标就是把胶囊画在错误的位置上（栏还没布局、那一格已不在表里、或名称
        // 区被角标簇挤到零宽，三档都不该弹出一只错位或零宽的框）。
        AURORA_LOG_WARN("tab_rename", "no pill box for this tab yet, rename not opened");
        return;
    }
    if (is_open()) {
        if (id_.has_value() && *id_ == id) {
            // 同一格再双击：只重锚，不重种文本也不夺焦点——用户可能已经打了几笔，重种初值就是把输入抹掉。
            apply_box(target->box);
            return;
        }
        // 改指另一格（胶囊开着时按 `tab.rename`，或双击了别的格）：换 id、重种现名、光标落尾。
        // 不重建树、不重复压作用域——树还是那一棵，作用域还是那一次打开压的那一层。
        id_ = id;
        if (input_ != nullptr) {
            input_->set_value(to_utf8(target->current_name));
        }
        apply_box(target->box);
        focus_.set_focus(input_.get());
        place_caret_at_end();
        return;
    }
    id_ = id;
    input_ = build_pill(*target);
    popup_->set_content(node(input_));
    box_ = std::nullopt;  // 浮层刚建，还没拿到任何一档尺寸：下一次 `apply_box` 必须下发
    // 打开前的深度是对账的基准（头文件头④）：`Popup::open_at` 只在派发栈内才压作用域，故栈外打开时由
    // 本件补压那一次——与搜索浮层、#117 启动对话框同一条处置（裁决 7.68⑤）。
    scope_depth_before_ = focus_.scope_depth();
    apply_box(target->box);  // 内含 `open_at`：那一次打开就是本件唯一的打开入口
    if (focus_.scope_depth() == scope_depth_before_) {
        focus_.push_scope(popup_.get());
    }
    // `push_scope` 会把焦点移进子树首个可聚焦控件，而胶囊体只有这一只控件——仍然要自己点名，与
    // 「打开即得焦点」那条口径一致（搜索浮层同一处实测）。
    focus_.set_focus(input_.get());
    place_caret_at_end();
}

auto TabRenameEditor::close() -> void {
    popup_->close();  // 三条真路径都汇到 `on_popup_closed()`；未开着时框架自己守卫 `open_`
}

auto TabRenameEditor::sync() -> void {
    if (!is_open()) {
        // 关闭态第一句就早返回；待收旗标随之作废（已经在别处收过层），不留到下一次打开。
        pending_close_ = false;
        return;
    }
    if (pending_close_) {
        pending_close_ = false;
        close();  // 已在派发栈外：这一次 `pop_scope` 是本件唯一的一次，深度对账落在 `on_popup_closed()`
        return;
    }
    const std::optional<RenameTarget> target =
        id_.has_value() ? bar_.rename_target(*id_) : std::nullopt;
    if (!target.has_value()) {
        // 那一格没了（被关闭）或名称区归零：收层而**不**提交——把框里没按 Enter 的字写进一格已经消失的
        // 标签是凭空造状态。栏未测量（最小化那一档）也落到这里，同样按「这一格不在屏幕上」处理。
        cancelled_ = true;
        close();
        return;
    }
    apply_box(target->box);
}

auto TabRenameEditor::is_open() const noexcept -> bool {
    return popup_ != nullptr && popup_->is_open();
}

auto TabRenameEditor::on_popup_closed() -> void {
    // 按深度对账而不是按「关闭现场是否在派发栈内」：`Popup::close()` 只在它自己也取到了焦点管理器时才
    // 弹，四条开合组合里恰有两条会漏——按打开前的读数补齐，四种组合都不漏也不双弹（与搜索浮层同一条）。
    if (focus_.scope_depth() > scope_depth_before_) {
        focus_.pop_scope();
    }
    // 状态快照先取、后清，再决定提不提：`commit_` 那一条闭包会推 `TabStrip::rename` ⇒ 栏重排 ⇒ 下一次
    // `sync()` 重取盒，此时本件必须已经回到「没在编辑」那一档，否则那次重排里的一次失焦会二次提交。
    const std::optional<std::uint64_t> id = id_;
    const std::string text = input_ != nullptr ? input_->value() : std::string{};
    const bool cancelled = cancelled_;
    id_.reset();
    box_.reset();
    pending_close_ = false;
    cancelled_ = false;
    if (cancelled || !id.has_value() || !commit_) {
        return;  // `Esc` / 那一格消失：不提交
    }
    commit_(*id, to_code_points(text));  // 空串交 `rename` 那条腿判「撤销」（裁决 7.43③），本件不判空
}

auto TabRenameEditor::build_pill(const RenameTarget &target) -> std::shared_ptr<aurora::TextInput> {
    const SettingsChrome chrome = settings_chrome();
    auto input = std::make_shared<RenameInput>(
        aurora::TextInputProps{.value = to_utf8(target.current_name), .font_size = kRenamePillFontPt},
        [this]() -> void {
            cancelled_ = true;      // 这一次收层是 `Esc` 发起的：`on_popup_closed()` 据此不提交
            pending_close_ = true;  // 收层交逐帧 `sync()`，不在键盘派发栈里收
        },
        [this]() -> void { pending_close_ = true; });
    // Enter 那条腿不在这儿：基类的 Enter 分支会呼 `set_on_submit`，故只登记一次待收层（三条收口共用
    // `on_close` 那一个提交点，见头文件头③）。
    input->set_on_submit([this](const std::string &) -> void { pending_close_ = true; });

    // chrome 交接（裁决 7.58 的同一判法：框架输入类控件的缺省色是浅色档，暗档卡片上必须自己交）。
    input->set_text_color(chrome.text);
    // 稿的胶囊底 `#191A21` 与栏底同一枚色值——它读作「卡上开的一个洞」，而不是又一层控件。13 枚 token
    // 里没有这一格（§2.1 表里那格叫 `input`，代码侧尚无消费者，同 `sel` 的处境，裁决 7.187 的欠项），
    // 故取栏底今日实际落的那一枚 `card_bg`（PREF.01 的像素证人钉的就是它），把「胶囊＝栏底透过卡」这层
    // 关系留住，而不是为一只浮层新增第 14 枚 token。
    input->set_background(chrome.card_bg);
    input->set_focused_background(chrome.card_bg);  // 聚焦态不换底：稿只画了一档底
    // 边框两档都给 accent：胶囊只在获焦时存在，但失焦到 `sync()` 收层之间还有一帧未聚焦的画档，
    // 那一帧若跟缺省灰或主题 primary 就会闪一下。聚焦态线宽由框架强制加粗到 1.5 dp（稿的 `sw="1"`
    // 与实测的差登记于此，不改框架）。
    input->set_border_color(chrome.accent);
    input->set_focused_border_color(chrome.accent);
    input->set_border_width(kRenamePillBorderDp);
    input->set_corner_radius(kRenamePillRadiusDp);
    input->set_cursor_color(chrome.accent);  // 稿那枚光标 `fill=#BD93F9`
    // 选区高亮的缺省色是半透明蓝（`{80,120,220,90}`），在暗档胶囊上读起来是第三套色。取 accent 同色、
    // 同缺省那一档 alpha：只换色道，不透明度沿用框架实测过的那一档，IME 的 preedit 高亮不动（稿无此档）。
    input->set_selection_color(aurora::Color{chrome.accent.r, chrome.accent.g, chrome.accent.b, 90});
    // 稿：胶囊左沿 38 → 内文本左沿 44，即左右各 6 dp；上下给 0——高由修饰链钉死 22 dp，而 `TextInput`
    // 在内容盒内竖向居中，上下内边距只会把可居中的余量吃掉。
    input->set_padding(aurora::EdgeInsets{.left = kRenamePillTextInsetDp,
                                          .top = 0.0F,
                                          .right = kRenamePillTextInsetDp,
                                          .bottom = 0.0F});
    // 占位不给（见上），无障碍名因此要单独点名，否则屏幕阅读器读到的是空串。
    input->set_accessibility_label(settings_label("tab.action.rename"));
    return input;
}

auto TabRenameEditor::apply_box(const aurora::Rect &box) -> void {
    if (box_.has_value() && *box_ == box) {
        return;  // 逐位相同即不动（头文件文件头那条 idle-frame 纪律：`open_at` 无条件标脏）
    }
    box_ = box;
    if (input_ != nullptr) {
        // 宽与高都必须由修饰链显式下达：`Popup::on_layout` 按无界约束测内容，而稿断的是胶囊宽＝名称区
        // 那一截；`TextInput::on_layout` 的缺省高是「文本测量 + 上下内边距」，12 pt 上会读出另一档数。
        input_->set_modifier(aurora::Modifier{}.width(box.size.width).height(kRenamePillHeightDp));
    }
    popup_->open_at(box.origin);  // 已开着时这一句只改锚点；未开着时它就是那唯一的一次打开
}

auto TabRenameEditor::place_caret_at_end() -> void {
    if (input_ == nullptr) {
        return;
    }
    // 端点按 UTF-8 字节偏移传：`accessibility_set_selection` 自己解码成码点模型并把越界端点夹到文本尾，
    // 而码点数不会超过字节数，故 `value().size()` 是一个「必然落在末尾」的上界；两端重合即清空选区、
    // 光标落位（稿那枚光标在名称之后，名下没有高亮档）。
    const std::size_t end = input_->value().size();
    input_->accessibility_set_selection(end, end);
}

}  // namespace borealis::ui
