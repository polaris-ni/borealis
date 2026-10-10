// ============================================================
// SSH 密钥管理面板实现（src/ui/keys_panel.cpp）
// ------------------------------------------------------------
// 形态要点对照设计稿标号（裁决 7.106 D1–D12，稿 §2）：左侧 520 dp 停靠卡片（1），
// 两行堆叠行（2：首行 徽标+名称+口令档+动作，次行 指纹+全路径），加密两态与公钥缺失
// （3），一键复制（4），生成对话框两卡定字段集＋同名即拒＋提交置灰（5），推送对话框
// 档案下拉＋四格阶梯＋留痕（6），删除两段式确认（7）。
//
// 本件一次文件系统都不碰：所有动作经 Hooks 交回装配层的串行队列，界面只读快照
// （D4①/D5①/D6①）。措辞全部来自 `keys_format` 的映射 + `settings_label`（7.25⑬）。
// ============================================================

#include "keys_panel.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "aurora/widget/alignment.h"
#include "aurora/widget/button.h"
#include "aurora/widget/canvas.h"
#include "aurora/widget/containers.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/stack.h"
#include "aurora/widget/text.h"

#include "settings_i18n.h"
#include "settings_panel.h"  // settings_chrome

namespace borealis::ui {

namespace {

/// @brief RSA 位数三档（判据 5 的下拉行源；次序即下拉次序，与 `conn::RsaBits` 同值）。
constexpr std::array<std::uint16_t, 3> kRsaBits{2048U, 3072U, 4096U};

[[nodiscard]] auto make_text(std::string content, aurora::Color color)
    -> std::shared_ptr<aurora::Text> {
    return std::make_shared<aurora::Text>(
        aurora::TextProps{.content = std::move(content), .text_color = color});
}

[[nodiscard]] auto make_button(std::string label, aurora::Color bg, aurora::Color fg,
                               float min_width, std::function<void()> on_click,
                               aurora::Color line, float line_width = 1.0F)
    -> std::shared_ptr<aurora::Button> {
    auto button = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = std::move(label),
        .color = bg,
        .on_color = fg,
        .border_color = line,
        .border_width = line_width,
        .min_width = min_width,
    });
    button->set_on_click(std::move(on_click));
    return button;
}

[[nodiscard]] auto node(std::shared_ptr<aurora::Widget> widget) -> aurora::Node {
    return aurora::Node{std::move(widget)};
}

[[nodiscard]] auto make_input(float width) -> std::shared_ptr<aurora::TextInput> {
    const auto chrome = settings_chrome();
    auto input = std::make_shared<aurora::TextInput>();
    input->set_background(chrome.control_bg);
    input->set_border_color(chrome.card_line);
    input->set_focused_border_color(chrome.accent);
    input->set_text_color(chrome.text);
    input->modifier.set(aurora::Modifier{}.width(width));
    return input;
}

/// @brief 行内/框内一枚定宽短标（溢出交给 Text 自己的 Clip，行右侧的按钮才点得到）。
[[nodiscard]] auto chip(std::string text, aurora::Color color, float width) -> aurora::Node {
    auto widget = make_text(std::move(text), color);
    widget->modifier.set(aurora::Modifier{}.width(width));
    return node(std::move(widget));
}

/// @brief 「标签 + 控件」一行（同隧道面板与向导的形态；标签列宽一致）。
[[nodiscard]] auto field_row(std::string label, aurora::Node control) -> aurora::Node {
    return node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {chip(std::move(label), settings_chrome().text_dim, 110.0F),
                     std::move(control)},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    }));
}

/// @brief 把词条 key（可空）解析成显示串；空 key 回空串，调用方据此不占一行。
[[nodiscard]] auto wording(std::string_view key, std::string_view arg = {}) -> std::string {
    if (key.empty()) {
        return {};
    }
    if (arg.empty()) {
        return settings_label(key);
    }
    return settings_label(key, {aurora::LocalizedString{std::string{arg}}});
}

/// @brief 按路径在行表里找那一行（删除确认要复述它配对 `.pub` 的具体路径）。
[[nodiscard]] auto find_row(const std::vector<KeyRow> &rows, const std::string &path)
    -> const KeyRow * {
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [&](const auto &row) { return row.key.path == path; });
    return it == rows.end() ? nullptr : &*it;
}

[[nodiscard]] auto input_node(const std::shared_ptr<aurora::TextInput> &input) -> aurora::Node {
    return node(std::static_pointer_cast<aurora::Widget>(input));
}

}  // namespace

KeysPanel::KeysPanel(aurora::OverlayHost &host, Hooks hooks)
    : host_{host}, hooks_{std::move(hooks)} {}

KeysPanel::~KeysPanel() {
    close_generate();
    close_push();
    close_delete_confirm();
    clear_state();
}

auto KeysPanel::open() -> void {
    if (open_) {
        refresh_overlay();
        return;
    }
    if (hooks_.snapshot != nullptr) {
        apply_snapshot(hooks_.snapshot());
    }
    // 开面板即请装配层扫一次盘（稿 §5：扫盘只发生在 `keys.open` 与每次动作之后，启动序不扫）。
    // 首帧因此可能是空表——那正是「没有 SSH 标签也能开」与「零配置首屏不做同步 IO」
    // （PREF.06）两条同时要的形态。
    if (hooks_.request_scan != nullptr) {
        hooks_.request_scan();
    }
    painted_rows_ = key_rows(snapshot_.rows, snapshot_);
    painted_notice_key_ = snapshot_.notice_key;
    painted_notice_arg_ = snapshot_.notice_arg;
    open_ = true;
    install_overlay();
}

auto KeysPanel::close() -> void {
    if (!open_) {
        return;
    }
    close_generate();
    close_push();
    close_delete_confirm();
    if (dialog_ != nullptr) {
        dialog_->close();
    }
    clear_state();
}

auto KeysPanel::tick() -> void {
    if (!open_) {
        return;
    }
    if (hooks_.snapshot != nullptr) {
        apply_snapshot(hooks_.snapshot());
    }
    const auto rows = key_rows(snapshot_.rows, snapshot_);
    // 生成本档结束＝那个对话框的唯一自动出口：置灰的提交钮等的就是这一刻（判据 5）。
    // 用户自己取消过则标志早已清掉，这里不替他做主。
    if (awaiting_generate_ && snapshot_.op != KeysOpKind::Generate) {
        awaiting_generate_ = false;
        close_generate();
    }
    const bool notice_changed = snapshot_.notice_key != painted_notice_key_ ||
                                snapshot_.notice_arg != painted_notice_arg_;
    if (push_open_) {
        // 推送对话框的内容（在途档、阶梯当前格与归因留痕）随快照就地重建：框里没有
        // 输入草稿，重建不打断什么（换卡节奏同生成框）。主卡片此时照旧不动。
        if (snapshot_.op != painted_push_state_.op ||
            snapshot_.push_step != painted_push_state_.push_step ||
            snapshot_.notice_key != painted_push_state_.notice_key ||
            snapshot_.notice_arg != painted_push_state_.notice_arg) {
            rebuild_push();
        }
        return;
    }
    if (rows == painted_rows_ && !notice_changed) {
        return;
    }
    // 生成框/确认框在场时不重建主卡片：浮层次序会变、正在输入的草稿也会被打断
    //（隧道面板同条纪律；painted_*_ 留在旧值，框收掉的那一帧自然补重建）。
    if (generate_open_ || delete_confirm_open_) {
        return;
    }
    painted_rows_ = rows;
    painted_notice_key_ = snapshot_.notice_key;
    painted_notice_arg_ = snapshot_.notice_arg;
    refresh_overlay();
}

auto KeysPanel::visible_rows() const -> std::vector<KeyRow> {
    return key_rows(snapshot_.rows, snapshot_);
}

auto KeysPanel::apply_snapshot(const KeysSnapshot &snapshot) -> void {
    snapshot_ = snapshot;
    notice_text_ = wording(key_headline_key(snapshot_), snapshot_.notice_arg);
}

[[nodiscard]] auto KeysPanel::default_directory() const -> std::string {
    // 目录表首条恒为 `~/.ssh`（`normalize_key_dirs()` 的不变量，D2③ 细则⑵）；表空＝拿不到
    // HOME 的极端现场，此时交空串，让提交侧按「请求不成立」拒掉而不是猜一个目录出来。
    return snapshot_.dirs.empty() ? std::string{} : snapshot_.dirs.front().path;
}

auto KeysPanel::install_overlay() -> void {
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    const auto chrome = settings_chrome();
    auto mask = std::make_shared<aurora::Canvas>(
        [c = chrome](aurora::Painter &painter, const aurora::Rect &box) -> void {
            painter.fill_rect(box, c.window_bg);
        });
    mask->modifier.set(
        aurora::Modifier{}.fill_max_size().clickable([this]() -> void { close(); }));
    auto card = build_overlay();
    auto layer = std::make_shared<aurora::Stack>(
        std::vector<aurora::Node>{node(std::static_pointer_cast<aurora::Widget>(std::move(mask))),
                                  node(std::static_pointer_cast<aurora::Widget>(std::move(card)))},
        aurora::Alignment::CenterLeft);
    dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(layer))));
    overlay_index_ = host_.add_overlay(node(std::static_pointer_cast<aurora::Widget>(dialog_)));
    dialog_->show();
}

auto KeysPanel::refresh_overlay() -> void {
    if (!open_) {
        return;
    }
    install_overlay();
}

auto KeysPanel::build_row(const KeyRow &row) -> aurora::Node {
    const auto chrome = settings_chrome();
    const bool busy = row.op != KeysOpKind::Idle;
    const auto public_text = row.public_text();

    // ---- 首行：类型徽标 + 名称 + 口令档 + 缺失／在途措辞 + 动作组 ----
    auto line1_children = std::vector<aurora::Node>{};
    const auto badge = key_type_badge(row.key);
    // `Unknown` 那一档没有徽标可言（`key_type_badge` 回空串是判据，不是漏填），但留一整格
    // 空白会让人以为行没画完，故交一串 ASCII 短横——它不是措辞，不需要词条。
    line1_children.push_back(chip(badge.empty() ? std::string{"--"} : badge, chrome.accent, 58.0F));
    line1_children.push_back(chip(row.key.basename, chrome.text, 100.0F));
    // 加密两态都有短标（D3① 的理由句：「看不出哪把有口令」正是这条裁决要消灭的那件事）。
    line1_children.push_back(chip(wording(key_passphrase_key(row.key)), chrome.text_dim, 56.0F));
    // 「公钥缺失」与「在途措辞」共用一格：在途优先——那一行正被点名，缺失与否都是下一帧的事。
    const auto note_key = busy ? key_op_kind_key(row.op) : key_public_state_key(row.key);
    if (!note_key.empty()) {
        line1_children.push_back(
            chip(wording(note_key), busy ? chrome.accent : chrome.text_dim, 88.0F));
    }

    if (!busy && !public_text.empty() && hooks_.copy_line != nullptr) {
        line1_children.push_back(node(make_button(settings_label("keys.action.copy"),
                                                  chrome.control_bg, chrome.text_dim, 40.0F,
                                                  [this, copy = row]() -> void { on_copy(copy); },
                                                  chrome.card_line)));
    }
    if (!busy && !public_text.empty() && hooks_.push != nullptr) {
        line1_children.push_back(node(make_button(settings_label("keys.action.push"),
                                                  chrome.control_bg, chrome.text_dim, 40.0F,
                                                  [this, path = row.key.path]() -> void {
                                                      open_push(path);
                                                  },
                                                  chrome.card_line)));
    }
    // 「公钥缺失」那一把的出口就是这一枚（稿 §2 判据 3）：把私钥的公共部分写成 `.pub`。
    // 两族之外的算法族（`public_text()` 为空、但 `.pub` 在场）拿不到这一枚——它的行压根
    // 拼不出可发的字节，导出腿同样交不出东西（`key_store.h` 头注那一档）。
    if (!busy && !row.key.has_public && hooks_.export_public != nullptr) {
        line1_children.push_back(node(make_button(settings_label("keys.action.export"),
                                                  chrome.accent, chrome.window_bg, 56.0F,
                                                  [this, path = row.key.path]() -> void {
                                                      on_export(path);
                                                  },
                                                  chrome.card_line)));
    }
    if (hooks_.delete_key != nullptr) {
        line1_children.push_back(node(make_button(settings_label("keys.action.delete"),
                                                  chrome.control_bg, chrome.text_dim, 40.0F,
                                                  [this, path = row.key.path]() -> void {
                                                      on_delete(path);
                                                  },
                                                  chrome.card_line)));
    }

    // ---- 次行：指纹 + 全路径（行唯一键的可见兑现，D2③ 细则⑷）----
    auto secondary = make_text(key_secondary_line(row.key), chrome.text_dim);
    secondary->modifier.set(aurora::Modifier{}.width(330.0F));
    auto line1 = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(line1_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 6.0F,
    });
    auto line2 = std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::vector<aurora::Node>{node(std::move(secondary))},
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    });
    auto stacked = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = {node(std::move(line1)), node(std::move(line2))},
        .gap = 2.0F,
    });
    return node(std::static_pointer_cast<aurora::Widget>(std::move(stacked)));
}

[[nodiscard]] auto KeysPanel::build_overlay() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};
    const bool busy = snapshot_.op != KeysOpKind::Idle;

    // ---- 标题行：标题 + 行数 + 「添加目录」 + 「生成密钥」 + 关闭 ----
    auto count = make_text(settings_label("keys.header.count",
                                          {aurora::LocalizedString{
                                              std::to_string(painted_rows_.size())}}),
                           chrome.text_dim);
    auto header_children = std::vector<aurora::Node>{
        node(make_text(settings_label("keys.title"), chrome.text)), node(std::move(count))};
    if (hooks_.add_dir != nullptr && hooks_.pick_key_dir != nullptr) {
        auto add = make_button(settings_label("keys.action.add_dir"), chrome.control_bg,
                               chrome.text_dim, 64.0F, [this]() -> void { on_add_dir(); },
                               chrome.card_line);
        add->set_enabled(!busy);  // 队列串行：目录变更要排重扫的队，不在跑着的时候叠一次。
        header_children.push_back(node(std::move(add)));
    }
    if (hooks_.generate != nullptr) {
        auto gen = make_button(settings_label("keys.action.generate"), chrome.accent,
                               chrome.window_bg, 72.0F, [this]() -> void { open_generate(); },
                               chrome.card_line);
        gen->set_enabled(!busy);
        header_children.push_back(node(std::move(gen)));
    }
    header_children.push_back(node(make_button(settings_label("keys.action.close"),
                                               chrome.control_bg, chrome.text_dim, 48.0F,
                                               [this]() -> void { close(); }, chrome.card_line)));
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(header_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    })));

    // ---- 顶行留痕（判据 4 的「已复制」、判据 5 的「生成中」、各家失败都走这一格）----
    if (!notice_text_.empty()) {
        auto notice = make_text(notice_text_, chrome.text_dim);
        notice->modifier.set(aurora::Modifier{}.fill_max_width());
        children.push_back(node(std::move(notice)));
    }

    // ---- 不可达目录留痕（D2③ 细则⑶：条目保留、行内说得出口）----
    for (const auto &dir : snapshot_.dirs) {
        const auto note_key = key_dir_note_key(dir);
        if (note_key.empty()) {
            continue;
        }
        auto note = make_text(settings_label(note_key, {aurora::LocalizedString{dir.path}}),
                              chrome.text_dim);
        note->modifier.set(aurora::Modifier{}.fill_max_width());
        children.push_back(node(std::move(note)));
    }

    // ---- 行区（标号 1/2）----
    auto rows_column = std::make_shared<aurora::Column>(aurora::ColumnProps{.gap = 8.0F});
    for (const auto &row : painted_rows_) {
        rows_column->add(build_row(row));
    }
    if (painted_rows_.empty()) {
        rows_column->add(node(make_text(settings_label("keys.empty"), chrome.text_dim)));
    }
    auto scroll = std::make_shared<aurora::Scroll>(aurora::ScrollProps{
        .child = node(std::static_pointer_cast<aurora::Widget>(std::move(rows_column))),
        .step = 32.0F,
    });
    scroll->modifier.set(aurora::Modifier{}.fill_max_width().expand());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(std::move(scroll))));

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(520.0F)
                           .fill_max_height()
                           .padding(aurora::EdgeInsets{12.0F, 12.0F, 12.0F, 12.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto KeysPanel::on_add_dir() -> void {
    if (hooks_.pick_key_dir == nullptr || hooks_.add_dir == nullptr) {
        return;
    }
    // 空串＝取消**或**平台对话框起不来（D11② 的代价：Linux 腿本期等价取消），两档同途：
    // 整条腿不到装配层（add_dir 不呼、重扫不起），判据的「不留痕」就落在这一格。
    const auto picked = hooks_.pick_key_dir();
    if (picked.empty()) {
        return;
    }
    hooks_.add_dir(picked);
}

auto KeysPanel::on_copy(const KeyRow &row) -> void {
    const auto line = row.public_text();
    if (line.empty() || hooks_.copy_line == nullptr) {
        return;  // 拼不出行的那两档压根不会画出这枚钮；这一道防的是按钮与快照错帧。
    }
    hooks_.copy_line(line);
}

auto KeysPanel::on_export(const std::string &path) -> void {
    if (hooks_.export_public != nullptr) {
        hooks_.export_public(path);  // 要不要问口令是装配层按行的 `encrypted` 判的（D12⑵ 反面）。
    }
}

auto KeysPanel::on_delete(const std::string &path) -> void {
    open_delete_confirm(path);  // 判据 7：这枚钮只呼确认框，一物都不动。
}

// ============================================================
// 生成对话框（判据 5，D4①/D5①）
// ============================================================

auto KeysPanel::open_generate() -> void {
    close_generate();
    draft_ = GenerateDraft{};
    draft_.directory = default_directory();
    type_index_ = 0;  // ed25519 缺省（需求原文「优先」那一档）。
    bits_index_ = 1;  // 3072（稿 §2 判据 5 的缺省档）。
    draft_.rsa_bits = kRsaBits[bits_index_];
    rebuild_generate();
}

auto KeysPanel::rebuild_generate() -> void {
    close_generate();
    auto editor = build_generate();
    generate_dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(editor))));
    generate_overlay_ =
        host_.add_overlay(node(std::static_pointer_cast<aurora::Widget>(generate_dialog_)));
    generate_dialog_->show();
    generate_open_ = true;
}

auto KeysPanel::close_generate() -> void {
    if (!generate_open_) {
        return;
    }
    if (generate_dialog_ != nullptr) {
        generate_dialog_->close();
    }
    if (generate_overlay_.has_value()) {
        host_.remove_overlay(*generate_overlay_);
        generate_overlay_.reset();
    }
    generate_dialog_.reset();
    name_input_.reset();
    comment_input_.reset();
    dir_input_.reset();
    passphrase_input_.reset();
    passphrase_again_input_.reset();
    generate_notice_.reset();
    generate_submit_.reset();
    generate_open_ = false;
    awaiting_generate_ = false;  // 用户自己收的框：完成时不再替他关一次（tick 里那条判据随之空转）。
}

[[nodiscard]] auto KeysPanel::build_generate() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    draft_.rsa = type_index_ == 1U;
    auto children = std::vector<aurora::Node>{};

    auto title = make_text(settings_label("keys.generate.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::move(title)));

    // ---- 两卡定字段集（判据 5：只有 RSA 卡出位数下拉；换卡＝就地重建，同隧道三式）----
    auto card_button = [&chrome, this](std::size_t index, std::string_view key) -> aurora::Node {
        const auto selected = index == type_index_;
        return node(make_button(
            settings_label(key), selected ? chrome.accent : chrome.control_bg,
            selected ? chrome.window_bg : chrome.text, 120.0F,
            [this, index]() -> void {
                collect_draft();
                type_index_ = index;
                rebuild_generate();
            },
            chrome.card_line));
    };
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {card_button(0, "keys.kind.ed25519"), card_button(1, "keys.kind.rsa")},
        .gap = 8.0F,
    })));

    if (draft_.rsa) {
        draft_.rsa_bits = kRsaBits[bits_index_];  // 没动过下拉也要拿到缺省那一档。
        auto bits_box = std::make_shared<aurora::Dropdown>(
            std::vector<std::string>{std::to_string(kRsaBits[0]), std::to_string(kRsaBits[1]),
                                     std::to_string(kRsaBits[2])},
            static_cast<int>(bits_index_));
        bits_box->set_box_color(chrome.control_bg);
        bits_box->set_border_color(chrome.card_line);
        bits_box->set_text_color(chrome.text);
        bits_box->set_arrow_color(chrome.text_dim);
        bits_box->set_accent_color(chrome.accent);
        bits_box->set_on_change([this](int index) -> void {
            if (index < 0 || static_cast<std::size_t>(index) >= kRsaBits.size()) {
                return;
            }
            bits_index_ = static_cast<std::size_t>(index);
            draft_.rsa_bits = kRsaBits[static_cast<std::size_t>(index)];
        });
        bits_box->modifier.set(aurora::Modifier{}.width(150.0F));
        children.push_back(
            field_row(settings_label("keys.generate.field.bits"),
                      node(std::static_pointer_cast<aurora::Widget>(std::move(bits_box)))));
    }

    name_input_ = make_input(240.0F);
    name_input_->set_value(draft_.basename);
    children.push_back(
        field_row(settings_label("keys.generate.field.name"), input_node(name_input_)));

    comment_input_ = make_input(240.0F);
    comment_input_->set_value(draft_.comment);
    children.push_back(
        field_row(settings_label("keys.generate.field.comment"), input_node(comment_input_)));

    // ---- 目标目录 + 「浏览」（D2③：生成目录也可选，选了**不入表**，每次回到缺省）----
    dir_input_ = make_input(220.0F);
    dir_input_->set_value(draft_.directory);
    auto dir_children = std::vector<aurora::Node>{
        chip(settings_label("keys.generate.field.directory"), chrome.text_dim, 110.0F),
        input_node(dir_input_)};
    if (hooks_.pick_key_dir != nullptr) {
        dir_children.push_back(node(make_button(settings_label("keys.generate.browse"),
                                                chrome.control_bg, chrome.text_dim, 56.0F,
                                                [this]() -> void {
                                                    if (hooks_.pick_key_dir == nullptr ||
                                                        dir_input_ == nullptr) {
                                                        return;
                                                    }
                                                    const auto picked = hooks_.pick_key_dir();
                                                    if (!picked.empty()) {
                                                        dir_input_->set_value(picked);
                                                    }
                                                },
                                                chrome.card_line)));
    }
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(dir_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 8.0F,
    })));

    // ---- 口令双栏（判据 5：可空、掩码）----
    passphrase_input_ = make_input(240.0F);
    passphrase_input_->set_obscure_text(true);
    children.push_back(
        field_row(settings_label("keys.generate.field.passphrase"),
                  input_node(passphrase_input_)));
    passphrase_again_input_ = make_input(240.0F);
    passphrase_again_input_->set_obscure_text(true);
    children.push_back(
        field_row(settings_label("keys.generate.field.passphrase_again"),
                  input_node(passphrase_again_input_)));

    // ---- 红提示（空＝无）----
    generate_notice_ = make_text(std::string{}, aurora::colors::AURORA_RED);
    generate_notice_->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(generate_notice_)));

    // ---- 生成 / 取消 ----
    generate_submit_ = make_button(settings_label("keys.generate.action.save"), chrome.accent,
                                   chrome.window_bg, 72.0F, [this]() -> void { save_generate(); },
                                   chrome.card_line);
    auto cancel = make_button(settings_label("keys.generate.action.cancel"), chrome.control_bg,
                              chrome.text, 64.0F, [this]() -> void { close_generate(); },
                              chrome.card_line);
    // 提交钮的 shared_ptr 留一份在成员里（save_generate 收尾要置灰），只拷不挪进树。
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(generate_submit_), node(std::move(cancel))},
        .gap = 8.0F,
    })));

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(440.0F)
                           .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    return card;
}

auto KeysPanel::collect_draft() -> void {
    draft_.basename = name_input_ != nullptr ? name_input_->value() : draft_.basename;
    draft_.comment = comment_input_ != nullptr ? comment_input_->value() : draft_.comment;
    draft_.directory = dir_input_ != nullptr ? dir_input_->value() : draft_.directory;
    draft_.passphrase =
        passphrase_input_ != nullptr ? passphrase_input_->value() : draft_.passphrase;
    draft_.passphrase_again = passphrase_again_input_ != nullptr
                                  ? passphrase_again_input_->value()
                                  : draft_.passphrase_again;
    if (draft_.rsa && bits_index_ < kRsaBits.size()) {
        draft_.rsa_bits = kRsaBits[bits_index_];
    }
}

auto KeysPanel::save_generate() -> void {
    collect_draft();
    // 四道闸都是纯逻辑的（`key_model` 与 `keys_format`），因此这里能红提示而不是等 worker
    // 跑一趟回来才说「不成立」；传输腿那头还有第五道（盘上的孤 `.pub` 也占名，D5①），
    // 那一半不在本件能力内——这里看得见的是行，不是盘（`would_collide_with_rows` 头注）。
    const auto name_issue = conn::validate_key_name(draft_.basename);
    const auto comment_issue = conn::validate_comment(draft_.comment);
    const auto mismatch = key_passphrase_mismatch_key(draft_.passphrase, draft_.passphrase_again);
    std::string issue;
    if (name_issue != conn::NameIssue::None) {
        issue = wording(key_name_issue_key(name_issue));
    } else if (comment_issue != conn::CommentIssue::None) {
        issue = wording(key_comment_issue_key(comment_issue));
    } else if (!mismatch.empty()) {
        issue = wording(mismatch);
    } else if (draft_.directory.empty()) {
        // 空目录串与传输腿的「请求不成立」同一档（那里压根不碰盘），不给同一件事造第二种说法。
        issue = settings_label("keys.generate.invalid_request");
    } else if (conn::would_collide_with_rows(snapshot_.rows, draft_.directory, draft_.basename)) {
        issue = settings_label("keys.generate.name_taken");  // 同名即拒，绝不覆盖（F5/D5①）。
    }
    if (!issue.empty()) {
        if (generate_notice_ != nullptr) {
            generate_notice_->set_content(issue);
        }
        return;  // 无效草稿不提交（校验在提交时做，同隧道面板）。
    }
    if (hooks_.generate != nullptr) {
        hooks_.generate(draft_);
    }
    if (generate_submit_ != nullptr) {
        generate_submit_->set_enabled(false);  // 判据 5：提交后置灰，防连点两次。
    }
    awaiting_generate_ = true;  // 快照离开 Generate 时由 tick 收这个框。
}

// ============================================================
// 推送对话框（判据 6，D7①/D8①）
// ============================================================

auto KeysPanel::open_push(std::string path) -> void {
    close_push();
    push_path_ = std::move(path);
    push_profile_index_ = 0;
    push_profile_id_.clear();
    rebuild_push();
}

auto KeysPanel::rebuild_push() -> void {
    close_push();
    auto editor = build_push();
    push_dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(editor))));
    push_overlay_ =
        host_.add_overlay(node(std::static_pointer_cast<aurora::Widget>(push_dialog_)));
    push_dialog_->show();
    push_open_ = true;
}

auto KeysPanel::close_push() -> void {
    if (!push_open_) {
        return;
    }
    if (push_dialog_ != nullptr) {
        push_dialog_->close();
    }
    if (push_overlay_.has_value()) {
        host_.remove_overlay(*push_overlay_);
        push_overlay_.reset();
    }
    push_dialog_.reset();
    push_notice_.reset();
    push_profiles_.clear();
    push_open_ = false;
}

[[nodiscard]] auto KeysPanel::build_push() -> std::shared_ptr<aurora::Column> {
    const auto chrome = settings_chrome();
    auto children = std::vector<aurora::Node>{};

    auto title = make_text(settings_label("keys.push.title"), chrome.text);
    title->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::move(title)));

    // ---- 目标那一行的复述：推送的是哪把钥匙，得上屏说清 ----
    const auto it = std::find_if(snapshot_.rows.begin(), snapshot_.rows.end(),
                                 [&](const auto &row) { return row.path == push_path_; });
    if (it != snapshot_.rows.end()) {
        auto target = make_text(it->basename + "  " + it->fingerprint, chrome.text_dim);
        target->modifier.set(aurora::Modifier{}.fill_max_width());
        children.push_back(node(std::move(target)));
    }

    // ---- 目标档案下拉（SSH 子集；行源每次重建现取，同隧道面板的承载档案腿）----
    push_profiles_.clear();
    if (hooks_.profiles != nullptr) {
        for (auto &profile : hooks_.profiles()) {
            if (profile.type == conn::ConnectionType::Ssh) {
                push_profiles_.push_back(std::move(profile));
            }
        }
    }
    push_profile_index_ = 0;
    auto names = std::vector<std::string>{};
    for (std::size_t i = 0; i < push_profiles_.size(); ++i) {
        if (push_profiles_[i].id == push_profile_id_) {
            push_profile_index_ = i;
        }
        names.push_back(push_profiles_[i].name);
    }
    if (!push_profiles_.empty()) {
        push_profile_id_ = push_profiles_[push_profile_index_].id;  // 不写回不存在的档案引用。
    }
    auto box = std::make_shared<aurora::Dropdown>(std::move(names),
                                                  static_cast<int>(push_profile_index_));
    box->set_box_color(chrome.control_bg);
    box->set_border_color(chrome.card_line);
    box->set_text_color(chrome.text);
    box->set_arrow_color(chrome.text_dim);
    box->set_accent_color(chrome.accent);
    box->set_placeholder(settings_label("keys.push.no_profile"));
    box->set_enabled(!push_profiles_.empty());
    box->set_on_change([this](int index) -> void {
        if (index < 0 || static_cast<std::size_t>(index) >= push_profiles_.size()) {
            return;
        }
        push_profile_index_ = static_cast<std::size_t>(index);
        push_profile_id_ = push_profiles_[push_profile_index_].id;
    });
    box->modifier.set(aurora::Modifier{}.width(240.0F));
    children.push_back(
        field_row(settings_label("keys.push.field.profile"),
                  node(std::static_pointer_cast<aurora::Widget>(std::move(box)))));

    // ---- 阶梯四格（判据 6）：当前格由快照那个整数指（次序的来源是 `push_stage_index`）----
    const auto ladder = key_push_ladder();
    auto ladder_children = std::vector<aurora::Node>{};
    for (std::size_t i = 0; i < ladder.size(); ++i) {
        const auto current = snapshot_.push_step == static_cast<int>(i);
        const auto passed = snapshot_.push_step > static_cast<int>(i);
        ladder_children.push_back(chip(settings_label(ladder[i]),
                                       current ? chrome.accent
                                               : (passed ? chrome.text : chrome.text_dim),
                                       80.0F));
    }
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = std::move(ladder_children),
        .flex = aurora::Flex{.cross_axis = aurora::CrossAxisAlignment::Center},
        .gap = 6.0F,
    })));

    // ---- 留痕（失败七档／已在授权表里／已追加；措辞唯一来源是 `keys_format`）----
    push_notice_ = make_text(wording(snapshot_.notice_key, snapshot_.notice_arg), chrome.text_dim);
    push_notice_->modifier.set(aurora::Modifier{}.fill_max_width());
    children.push_back(node(std::static_pointer_cast<aurora::Widget>(push_notice_)));

    // ---- 推送 / 关闭 ----
    const bool pushing = snapshot_.op == KeysOpKind::Push;
    auto push_button = make_button(settings_label("keys.push.action"), chrome.accent,
                                   chrome.window_bg, 72.0F,
                                   [this]() -> void {
                                       if (hooks_.push != nullptr && !push_profile_id_.empty() &&
                                           !push_path_.empty()) {
                                           hooks_.push(push_path_, push_profile_id_);
                                       }
                                   },
                                   chrome.card_line);
    push_button->set_enabled(!pushing && !push_profiles_.empty());
    auto close_button = make_button(settings_label("keys.push.action.close"), chrome.control_bg,
                                     chrome.text, 64.0F, [this]() -> void { close_push(); },
                                     chrome.card_line);
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::move(push_button)), node(std::move(close_button))},
        .gap = 8.0F,
    })));

    auto card = std::make_shared<aurora::Column>(aurora::ColumnProps{
        .children = std::move(children),
        .gap = 8.0F,
    });
    card->modifier.set(aurora::Modifier{}
                           .width(440.0F)
                           .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                           .background(chrome.card_bg, 0.0F)
                           .border(1.0F, chrome.card_line)
                           .clickable([]() -> void {}));
    painted_push_state_ = snapshot_;  // 重建判据的基准（tick 比的是在途档/阶梯格/留痕三样）。
    return card;
}

// ============================================================
// 删除确认（判据 7，D12⑴ 两段式）
// ============================================================

auto KeysPanel::open_delete_confirm(std::string path) -> void {
    close_delete_confirm();
    pending_delete_path_ = std::move(path);
    const auto chrome = settings_chrome();

    // 确认框列出**具体路径**（D12⑴）：一次清一对的两条都上屏，用户点的才是自己认得的那对。
    const auto *row = find_row(painted_rows_, pending_delete_path_);
    auto line_of = [&chrome](std::string text) -> aurora::Node {
        auto widget = make_text(std::move(text), chrome.text);
        widget->modifier.set(aurora::Modifier{}.fill_max_width());
        return node(std::move(widget));
    };
    auto children = std::vector<aurora::Node>{
        line_of(settings_label("keys.delete.title")),
        line_of(pending_delete_path_)};
    if (row != nullptr && row->key.has_public && !row->key.public_path.empty()) {
        children.push_back(line_of(row->key.public_path));
    }
    children.push_back(node(make_text(settings_label("keys.delete.warn"),
                                      aurora::colors::AURORA_RED)));

    auto confirm = std::make_shared<aurora::Button>(aurora::ButtonProps{
        .label = settings_label("keys.delete.action"),
        .color = aurora::colors::AURORA_RED,  // 危险红（sftp D6① 与隧道确认同口径）
        .on_color = chrome.window_bg,
        .min_width = 80.0F,
    });
    confirm->set_on_click([this]() -> void {
        const auto path_copy = pending_delete_path_;
        close_delete_confirm();  // 先收框：删除的成败由下一帧的快照与行表说。
        if (hooks_.delete_key != nullptr) {
            hooks_.delete_key(path_copy);
        }
    });
    auto cancel = make_button(settings_label("keys.delete.action.cancel"), chrome.control_bg,
                              chrome.text, 64.0F, [this]() -> void { close_delete_confirm(); },
                              chrome.card_line);
    children.push_back(node(std::make_shared<aurora::Row>(aurora::RowProps{
        .children = {node(std::move(confirm)), node(std::move(cancel))},
        .gap = 8.0F,
    })));

    auto content =
        std::make_shared<aurora::Column>(aurora::ColumnProps{.children = std::move(children),
                                                             .gap = 8.0F});
    content->modifier.set(aurora::Modifier{}
                              .width(420.0F)
                              .padding(aurora::EdgeInsets{16.0F, 16.0F, 16.0F, 16.0F})
                              .background(chrome.card_bg, 0.0F)
                              .border(1.0F, chrome.card_line)
                              .clickable([]() -> void {}));
    confirm_dialog_ = std::make_shared<aurora::Dialog>(
        node(std::static_pointer_cast<aurora::Widget>(std::move(content))));
    confirm_overlay_ =
        host_.add_overlay(node(std::static_pointer_cast<aurora::Widget>(confirm_dialog_)));
    confirm_dialog_->show();
    delete_confirm_open_ = true;
}

auto KeysPanel::close_delete_confirm() -> void {
    if (!delete_confirm_open_) {
        return;
    }
    if (confirm_dialog_ != nullptr) {
        confirm_dialog_->close();
    }
    if (confirm_overlay_.has_value()) {
        host_.remove_overlay(*confirm_overlay_);
        confirm_overlay_.reset();
    }
    confirm_dialog_.reset();
    pending_delete_path_.clear();
    delete_confirm_open_ = false;
}

auto KeysPanel::clear_state() -> void {
    if (overlay_index_.has_value()) {
        host_.remove_overlay(*overlay_index_);
        overlay_index_.reset();
    }
    dialog_.reset();
    painted_rows_.clear();
    open_ = false;
}

}  // namespace borealis::ui
