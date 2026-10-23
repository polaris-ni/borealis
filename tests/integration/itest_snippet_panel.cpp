// ============================================================
// 快捷片段面板集成用例（tests/integration/itest_snippet_panel.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.07` 延后子项的界面腿（稿 `codespec/UI_SNIPPETS.draft.md` §5 批 3 判据
// ①–⑧）：行表随搜索与分组收窄、真点发送（`enter` 追加一枚换行、无会话留痕）、占位符
// 填充框的发送与取消、新建/编辑/删除的整表交回与两段式确认、搜索框回车的唯一候选、
// 以及正文原样交付与编辑框的明文警告。
//
// 驱动台形态照 itest_keys_panel / itest_tunnel_panel 的 Harness（HeadlessSurface 排真实
// 布局 + `hit_test_chain` 取命中 + EventDispatcher 发真事件）。判据一律走面板交出的
// 观测面（visible_rows / editor_draft / fill_names / pending_delete_id / notice），
// 浮层树只用于「控件真点得到」这一条派发腿。
// ============================================================

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/button.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "borealis/conn/snippet.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/snippet_panel.h"

namespace borealis::test_cases::itest_snippet_panel {

namespace {

namespace conn = borealis::conn;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 760;

/// @brief 装配层替身：片段表 + 三条接缝的调用留痕（struct-of-回调同族）。
struct Probe {
    std::vector<conn::Snippet> items;
    std::vector<std::vector<conn::Snippet>> persisted;
    std::vector<std::string> sent;
    bool persist_ok = true;
    bool send_ok = true;

    [[nodiscard]] auto hooks() -> ui::SnippetPanel::Hooks {
        ui::SnippetPanel::Hooks out;
        out.load = [this]() -> std::vector<conn::Snippet> { return items; };
        out.persist = [this](const std::vector<conn::Snippet> &next) -> bool {
            persisted.push_back(next);
            if (!persist_ok) {
                return false;  // 落盘失败：内存表照旧（D5 的「失败则内存态不变」）。
            }
            items = next;
            return true;
        };
        out.send = [this](const std::string &payload) -> bool {
            sent.push_back(payload);
            return send_ok;
        };
        return out;
    }
};

[[nodiscard]] auto snippet(std::string id, std::string name, std::string group, std::string body,
                           bool enter = false) -> conn::Snippet {
    conn::Snippet out;
    out.id = std::move(id);
    out.name = std::move(name);
    out.group = std::move(group);
    out.body = std::move(body);
    out.enter = enter;
    return out;
}

struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
};

/// @brief 无头窗口 + 真布局 + 真指针派发的驱动台（itest_keys_panel 同族）。
class Harness {
  public:
    Harness() {
        host_ = std::make_shared<au::OverlayHost>();
        base_ = std::make_shared<au::Text>(
            aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
        (void)host_->add_overlay(au::Node{base_});  // 占住子节点 [0]：基础内容。
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;

    [[nodiscard]] auto attach(Probe &probe) -> std::unique_ptr<ui::SnippetPanel> {
        return std::make_unique<ui::SnippetPanel>(*host_, probe.hooks());
    }

    auto render() -> void { static_cast<void>(window_.present_root(root_)); }

    /// @brief 打开面板并排两帧（浮层入树 + 布局落 bounds）。
    auto open(ui::SnippetPanel &panel) -> void {
        panel.open();
        render();
        render();
    }

    [[nodiscard]] auto chain_at(float x_dp, float y_dp) -> std::vector<au::HitNode> {
        return root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
    }

    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const auto chain = chain_at(x_dp, y_dp);
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    auto pointer(au::MouseAction action, float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        event.action = action;
        static_cast<void>(dispatcher_.dispatch_mouse(root_.widget(), event, &focus_));
    }

    auto click(float x_dp, float y_dp) -> void {
        pointer(au::MouseAction::Press, x_dp, y_dp);
        pointer(au::MouseAction::Release, x_dp, y_dp);
    }

    /// @brief 单击一个 Spot（先确认派发链在这一点仍交回它——点空不算点中）。
    auto click(const HitSpot &spot) -> void {
        AURORA_TEST_REQUIRE_MSG(spot.widget != nullptr, "the control is not dispatch-reachable");
        AURORA_TEST_REQUIRE_MSG(hit(spot.x, spot.y) == spot.widget,
                                "the probe point no longer claims the widget");
        click(spot.x, spot.y);
    }

    /// @brief 点进输入框并按帧落定，随后由调用方向当前焦点发文本。
    auto focus_input(const HitSpot &spot) -> void {
        click(spot);
        render();
        render();
    }

    /// @brief 向当前焦点控件逐字符发真实文本输入。
    auto type(std::string_view text) -> bool {
        bool handled = false;
        for (const char ch : text) {
            au::TextInputEvent event;
            event.text = std::string(1U, ch);
            handled = au::EventDispatcher::dispatch(root_.widget(), event, focus_) || handled;
        }
        return handled;
    }

    [[nodiscard]] auto press(au::KeyCode key) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    /// @brief 扫描派发链，找第 nth 个满足 accept 的控件（行序＝扫描序，4 dp 步长）。
    [[nodiscard]] auto find(std::string_view type_name, std::size_t nth,
                            const std::function<bool(au::Widget *)> &accept = {}) -> HitSpot {
        std::vector<au::Widget *> seen;
        for (float y = 4.0F; y < static_cast<float>(kWindowHeight) - 4.0F; y += 4.0F) {
            for (float x = 20.0F; x < static_cast<float>(kWindowWidth) - 20.0F; x += 4.0F) {
                au::Widget *widget = hit(x, y);
                if (widget == nullptr || type_name != widget->type_name() ||
                    std::find(seen.begin(), seen.end(), widget) != seen.end()) {
                    continue;
                }
                if (accept && !accept(widget)) {
                    continue;
                }
                seen.push_back(widget);
                if (seen.size() == nth) {
                    return HitSpot{.widget = widget, .x = x, .y = y};
                }
            }
        }
        return HitSpot{};
    }

    /// @brief 按解析后的显示标签找按钮（accessibility_label 与绘制同源）。
    [[nodiscard]] auto find_button(const std::string &label, std::size_t nth = 1) -> HitSpot {
        return find("Button", nth, [&label](au::Widget *widget) -> bool {
            const auto *button = dynamic_cast<const au::Button *>(widget);
            return button != nullptr && button->accessibility_label() == label;
        });
    }

    /// @brief 按显示标签找正文 Text（红提示与留痕的可见性判据走这条）。
    [[nodiscard]] auto find_text(const std::string &label) -> HitSpot {
        return find("Text", 1, [&label](au::Widget *widget) -> bool {
            const auto *text = dynamic_cast<const au::Text *>(widget);
            return text != nullptr && text->accessibility_label() == label;
        });
    }

    /// @brief 按行序取第 nth 枚输入框（字段次序＝构造次序；对话框的遮罩挡住下层）。
    [[nodiscard]] auto find_input(std::size_t nth) -> HitSpot { return find("TextInput", nth); }

    [[nodiscard]] static auto input_value(const HitSpot &spot) -> std::string {
        const auto *input = dynamic_cast<const au::TextInput *>(spot.widget);
        return input != nullptr ? input->value() : std::string{};
    }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        static_cast<void>(surface->begin_frame(kWindowWidth, kWindowHeight));
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Text> base_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;
};

/// @brief 搜索框清空（点进去、End、逐字退格）。
auto clear_search(Harness &h, std::size_t bytes) -> void {
    auto search = h.find_input(1);
    AURORA_TEST_REQUIRE_MSG(search.widget != nullptr, "the search input is not reachable");
    h.focus_input(search);
    static_cast<void>(h.press(au::KeyCode::End));
    for (std::size_t index = 0; index < bytes; ++index) {
        static_cast<void>(h.press(au::KeyCode::Backspace));
    }
}

}  // namespace

// ------------------------------------------------------------
// 判据①：行表随搜索词与分组过滤收窄（搜索不重建搜索框本身）
// ------------------------------------------------------------
AURORA_TEST_CASE(search_and_group_narrow_the_row_list) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.items = {snippet("snip-0", "greet", "ops", "echo hello"),
                   snippet("snip-1", "uptime", "ops", "uptime"),
                   snippet("snip-2", "disk", "local", "df -h")};
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_REQUIRE(panel->is_open());
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 3U);
    AURORA_TEST_CHECK(panel->query().empty());
    AURORA_TEST_CHECK(panel->group().empty());

    // 真点搜索框再打两个字：只剩 body 里带 up 的那一条。
    auto search = h.find_input(1);
    AURORA_TEST_REQUIRE_MSG(search.widget != nullptr, "the search input is not reachable");
    h.focus_input(search);
    AURORA_TEST_CHECK_TRUE(h.type("up"));
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(panel->query(), "up");
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 1U);
    AURORA_TEST_CHECK(panel->visible_rows()[0].id == "snip-1");

    // 清空后换分组下拉：点主框展开，再点下沿第二档（下标 1＝ops）。
    clear_search(h, 2);
    h.render();
    h.render();
    AURORA_TEST_CHECK(panel->query().empty());
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 3U);

    auto box_spot = h.find("Dropdown", 1);
    AURORA_TEST_REQUIRE_MSG(box_spot.widget != nullptr, "the group dropdown is not reachable");
    auto *box = dynamic_cast<au::Dropdown *>(box_spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    AURORA_TEST_CHECK_EQ(box->option_count(), 3U);  // 全部分组 + ops + local
    const auto box_bounds = box->window_bounds();
    AURORA_TEST_REQUIRE(box_bounds.has_value());

    h.click(box_spot);
    h.render();
    h.render();
    // 取样点贴面板右缘：下拉面板横跨 160 dp，左侧与首行的「发送/编辑/删除」同排，
    // 那些按钮的布局盒会抢命中；右缘这一列只有面板自己申报。
    const float item_x = box_bounds->origin.x + box_bounds->size.width - 6.0F;
    const float item_y = box_bounds->origin.y + box_bounds->size.height + 26.0F + 13.0F;
    AURORA_TEST_REQUIRE_MSG(h.hit(item_x, item_y) == box,
                            std::string{"row claims: "} +
                                (h.hit(item_x, item_y) == nullptr ? "null"
                                                                 : h.hit(item_x, item_y)->type_name()));
    h.click(item_x, item_y);
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(box->selected_index(), 1);
    AURORA_TEST_CHECK_EQ(panel->group(), "ops");
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 2U);

    // 两重过滤的交集为空：可见行清空且画的是「没有匹配」那一句。
    auto search_again = h.find_input(1);
    AURORA_TEST_REQUIRE(search_again.widget != nullptr);
    h.focus_input(search_again);
    static_cast<void>(h.type("disk"));
    h.render();
    h.render();
    AURORA_TEST_CHECK(panel->visible_rows().empty());
    auto empty = h.find_text(ui::settings_label("snippets.empty.filtered"));
    AURORA_TEST_REQUIRE_MSG(empty.widget != nullptr, "the filtered-empty note is not drawn");
}

// ------------------------------------------------------------
// 判据②：真点发送交出正文；`enter` 多交一个换行；无会话留痕且面板不收
// ------------------------------------------------------------
AURORA_TEST_CASE(send_delivers_body_and_enter_appends_one_newline) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.items = {snippet("snip-0", "uptime", "", "uptime"),
                   snippet("snip-1", "deploy", "", "make deploy", true)};
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto first_send = h.find_button(ui::settings_label("snippets.action.send"), 1);
    AURORA_TEST_REQUIRE_MSG(first_send.widget != nullptr, "the send button is not reachable");
    h.click(first_send);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.sent.size(), 1U);
    AURORA_TEST_CHECK(probe.sent[0] == "uptime");
    AURORA_TEST_CHECK(panel->is_open());
    AURORA_TEST_CHECK(panel->notice() ==
                      ui::settings_label("snippets.notice.sent",
                                         {aurora::LocalizedString{std::string{"uptime"}}}));
    AURORA_TEST_CHECK(h.find_text(ui::settings_label(
                          "snippets.notice.sent",
                          {aurora::LocalizedString{std::string{"uptime"}}}))
                          .widget != nullptr);

    auto second_send = h.find_button(ui::settings_label("snippets.action.send"), 2);
    AURORA_TEST_REQUIRE_MSG(second_send.widget != nullptr, "the second send button is not reachable");
    h.click(second_send);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.sent.size(), 2U);
    AURORA_TEST_CHECK(probe.sent[1] == "make deploy\n");  // 只多这一枚，别的不加。

    // 发送失败（没有可发送的会话）：不关面板、留痕，前两次的血本不动。
    probe.send_ok = false;
    auto retry = h.find_button(ui::settings_label("snippets.action.send"), 1);
    AURORA_TEST_REQUIRE_MSG(retry.widget != nullptr, "the send button moved out of reach");
    h.click(retry);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.sent.size(), 3U);
    AURORA_TEST_CHECK(panel->is_open());
    AURORA_TEST_CHECK(panel->notice() == ui::settings_label("snippets.notice.no_session"));
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("snippets.notice.no_session")).widget != nullptr);
}

// ------------------------------------------------------------
// 判据③：含占位符先开填充框；填值后交出渲染串；取消则零发送
// ------------------------------------------------------------
AURORA_TEST_CASE(placeholder_fill_renders_before_send_and_cancel_is_silent) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.items = {snippet("snip-0", "ping", "net", "ping {{host}} -c {{count}}")};
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto send_spot = h.find_button(ui::settings_label("snippets.action.send"), 1);
    AURORA_TEST_REQUIRE_MSG(send_spot.widget != nullptr, "the send button is not reachable");
    h.click(send_spot);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->fill_open());
    AURORA_TEST_CHECK(probe.sent.empty());
    const auto &names = panel->pending_fill_names();
    AURORA_TEST_REQUIRE_EQ(names.size(), 2U);
    AURORA_TEST_CHECK(names[0] == "host");
    AURORA_TEST_CHECK(names[1] == "count");

    auto host_input = h.find_input(1);
    AURORA_TEST_REQUIRE_MSG(host_input.widget != nullptr, "the first fill input is not reachable");
    h.focus_input(host_input);
    static_cast<void>(h.type("example.org"));
    auto count_input = h.find_input(2);
    AURORA_TEST_REQUIRE_MSG(count_input.widget != nullptr, "the second fill input is not reachable");
    h.focus_input(count_input);
    static_cast<void>(h.type("3"));

    auto fill_send = h.find_button(ui::settings_label("snippets.fill.action.send"));
    AURORA_TEST_REQUIRE_MSG(fill_send.widget != nullptr, "the fill send button is not reachable");
    h.click(fill_send);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->fill_open());
    AURORA_TEST_REQUIRE_EQ(probe.sent.size(), 1U);
    AURORA_TEST_CHECK(probe.sent[0] == "ping example.org -c 3");

    // 取消腿：再开一次填充框，填一处后取消——Hook 一次都不该被调。
    auto reopen = h.find_button(ui::settings_label("snippets.action.send"), 1);
    AURORA_TEST_REQUIRE_MSG(reopen.widget != nullptr, "the send button moved out of reach");
    h.click(reopen);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->fill_open());
    auto cancel = h.find_button(ui::settings_label("snippets.fill.action.cancel"));
    AURORA_TEST_REQUIRE_MSG(cancel.widget != nullptr, "the fill cancel button is not reachable");
    h.click(cancel);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->fill_open());
    AURORA_TEST_CHECK_EQ(probe.sent.size(), 1U);
}

// ------------------------------------------------------------
// 判据④：新建交回整表且 id 为 snip-N；空名红提示不交回；落盘失败不改内存
// ------------------------------------------------------------
AURORA_TEST_CASE(new_snippet_saves_a_full_table_and_rejects_bad_drafts) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.items = {snippet("snip-0", "greet", "ops", "echo hi")};
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto new_spot = h.find_button(ui::settings_label("snippets.action.new"));
    AURORA_TEST_REQUIRE_MSG(new_spot.widget != nullptr, "the new button is not reachable");
    h.click(new_spot);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->editor_open());

    auto name_input = h.find_input(1);
    AURORA_TEST_REQUIRE_MSG(name_input.widget != nullptr, "the name input is not reachable");
    h.focus_input(name_input);
    static_cast<void>(h.type("deploy"));
    auto group_input = h.find_input(2);
    AURORA_TEST_REQUIRE_MSG(group_input.widget != nullptr, "the group input is not reachable");
    h.focus_input(group_input);
    static_cast<void>(h.type("ops"));
    auto body_input = h.find_input(3);
    AURORA_TEST_REQUIRE_MSG(body_input.widget != nullptr, "the body input is not reachable");
    h.focus_input(body_input);
    static_cast<void>(h.type("make deploy"));

    auto enter_spot = h.find("Switch", 1);
    AURORA_TEST_REQUIRE_MSG(enter_spot.widget != nullptr, "the enter switch is not reachable");
    h.click(enter_spot);

    auto save = h.find_button(ui::settings_label("snippets.editor.action.save"));
    AURORA_TEST_REQUIRE_MSG(save.widget != nullptr, "the save button is not reachable");
    h.click(save);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->editor_open());
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted[0].size(), 2U);
    AURORA_TEST_CHECK(probe.persisted[0][0].id == "snip-0");
    AURORA_TEST_CHECK(probe.persisted[0][1].id == "snip-1");  // 稳定句柄：新号接最大已用号。
    AURORA_TEST_CHECK(probe.persisted[0][1].name == "deploy");
    AURORA_TEST_CHECK(probe.persisted[0][1].group == "ops");
    AURORA_TEST_CHECK(probe.persisted[0][1].body == "make deploy");
    AURORA_TEST_CHECK_TRUE(probe.persisted[0][1].enter);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 2U);

    // 空名：红提示就地拒绝，落盘 Hook 零调用，框不收。
    h.click(h.find_button(ui::settings_label("snippets.action.new")));
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->editor_open());
    auto empty_body = h.find_input(3);
    AURORA_TEST_REQUIRE(empty_body.widget != nullptr);
    h.focus_input(empty_body);
    static_cast<void>(h.type("x"));
    h.click(h.find_button(ui::settings_label("snippets.editor.action.save")));
    h.render();
    h.render();
    AURORA_TEST_CHECK(panel->editor_open());
    AURORA_TEST_CHECK_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("snippets.issue.empty_name")).widget != nullptr);

    // 落盘失败：内存副本不动、框不收、就地留痕（D5 的失败腿）。
    auto retry_name = h.find_input(1);
    AURORA_TEST_REQUIRE(retry_name.widget != nullptr);
    h.focus_input(retry_name);
    static_cast<void>(h.type("backup"));
    probe.persist_ok = false;
    auto failing_save = h.find_button(ui::settings_label("snippets.editor.action.save"));
    AURORA_TEST_REQUIRE_MSG(failing_save.widget != nullptr, "the save button moved out of reach");
    h.click(failing_save);
    h.render();
    h.render();
    AURORA_TEST_CHECK(panel->editor_open());
    AURORA_TEST_CHECK_EQ(probe.persisted.size(), 2U);
    AURORA_TEST_CHECK_EQ(probe.persisted[1].size(), 3U);  // 交回的候选表含新条目。
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 2U);  // 但内存里的表没被它替换成三行。
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("snippets.notice.save_failed")).widget != nullptr);

    probe.persist_ok = true;
    auto ok_save = h.find_button(ui::settings_label("snippets.editor.action.save"));
    AURORA_TEST_REQUIRE_MSG(ok_save.widget != nullptr, "the save button moved out of reach");
    h.click(ok_save);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->editor_open());
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 3U);
}

// ------------------------------------------------------------
// 判据⑤：编辑保留 id 与其余条目，只改草稿里的字段
// ------------------------------------------------------------
AURORA_TEST_CASE(edit_keeps_the_id_and_the_other_rows) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    const auto untouched = snippet("snip-1", "uptime", "", "uptime", true);
    probe.items = {snippet("snip-0", "greet", "ops", "echo hi"), untouched};
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto edit = h.find_button(ui::settings_label("snippets.action.edit"), 1);
    AURORA_TEST_REQUIRE_MSG(edit.widget != nullptr, "the edit button is not reachable");
    h.click(edit);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->editor_open());
    AURORA_TEST_CHECK(Harness::input_value(h.find_input(1)) == "greet");
    AURORA_TEST_CHECK(Harness::input_value(h.find_input(2)) == "ops");
    AURORA_TEST_CHECK(Harness::input_value(h.find_input(3)) == "echo hi");

    auto body_input = h.find_input(3);
    AURORA_TEST_REQUIRE(body_input.widget != nullptr);
    h.focus_input(body_input);
    static_cast<void>(h.press(au::KeyCode::End));
    static_cast<void>(h.type(" there"));

    auto save = h.find_button(ui::settings_label("snippets.editor.action.save"));
    AURORA_TEST_REQUIRE_MSG(save.widget != nullptr, "the save button is not reachable");
    h.click(save);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->editor_open());
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted[0].size(), 2U);
    AURORA_TEST_CHECK(probe.persisted[0][0].id == "snip-0");  // id 不变。
    AURORA_TEST_CHECK(probe.persisted[0][0].name == "greet");
    AURORA_TEST_CHECK(probe.persisted[0][0].body == "echo hi there");
    AURORA_TEST_CHECK(probe.persisted[0][1] == untouched);  // 同表其余条目逐字段不变。
}

// ------------------------------------------------------------
// 判据⑥：删除两段式——第一段只开确认框，确认后才交回余表
// ------------------------------------------------------------
AURORA_TEST_CASE(delete_is_two_step_and_persists_the_remaining_table) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.items = {snippet("snip-0", "greet", "ops", "echo hi"),
                   snippet("snip-1", "uptime", "", "uptime")};
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto del = h.find_button(ui::settings_label("snippets.action.delete"), 1);
    AURORA_TEST_REQUIRE_MSG(del.widget != nullptr, "the delete button is not reachable");
    h.click(del);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->delete_confirm_open());
    AURORA_TEST_CHECK(panel->pending_delete_id() == "snip-0");
    AURORA_TEST_CHECK(probe.persisted.empty());  // 第一段不落盘。
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 2U);

    // 第一段的取消腿：一物不动。
    auto cancel = h.find_button(ui::settings_label("snippets.delete.action.cancel"));
    AURORA_TEST_REQUIRE_MSG(cancel.widget != nullptr, "the delete cancel is not reachable");
    h.click(cancel);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->delete_confirm_open());
    AURORA_TEST_CHECK(probe.persisted.empty());
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 2U);

    // 第二段：确认后交回余表，条目从可见行消失，面板不关。
    h.click(h.find_button(ui::settings_label("snippets.action.delete"), 1));
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->delete_confirm_open());
    auto confirm = h.find_button(ui::settings_label("snippets.delete.action.confirm"));
    AURORA_TEST_REQUIRE_MSG(confirm.widget != nullptr, "the delete confirm is not reachable");
    h.click(confirm);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->delete_confirm_open());
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted[0].size(), 1U);
    AURORA_TEST_CHECK(probe.persisted[0][0].id == "snip-1");
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 1U);
    AURORA_TEST_CHECK(panel->visible_rows()[0].id == "snip-1");
    AURORA_TEST_CHECK(panel->is_open());
}

// ------------------------------------------------------------
// 判据⑦：搜索框回车只在唯一候选时发送；0 或 >1 留痕不发送
// ------------------------------------------------------------
AURORA_TEST_CASE(enter_sends_only_a_unique_candidate) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    probe.items = {snippet("snip-0", "uptime", "", "uptime"),
                   snippet("snip-1", "disk", "", "df -h")};
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto search = h.find_input(1);
    AURORA_TEST_REQUIRE_MSG(search.widget != nullptr, "the search input is not reachable");
    h.focus_input(search);
    static_cast<void>(h.type("up"));
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 1U);
    AURORA_TEST_CHECK_TRUE(h.press(au::KeyCode::Enter));
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.sent.size(), 1U);
    AURORA_TEST_CHECK(probe.sent[0] == "uptime");

    // 零候选：回车不发送，留痕条数。
    clear_search(h, 2);
    static_cast<void>(h.type("zz"));
    h.render();
    h.render();
    AURORA_TEST_CHECK(panel->visible_rows().empty());
    static_cast<void>(h.press(au::KeyCode::Enter));
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(probe.sent.size(), 1U);
    AURORA_TEST_CHECK(panel->notice() ==
                      ui::settings_label("snippets.notice.candidates",
                                         {aurora::LocalizedString{std::string{"0"}}}));

    // 两条候选：同样不发送。
    clear_search(h, 2);
    static_cast<void>(h.type("i"));
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 2U);
    static_cast<void>(h.press(au::KeyCode::Enter));
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(probe.sent.size(), 1U);
    AURORA_TEST_CHECK(panel->notice() ==
                      ui::settings_label("snippets.notice.candidates",
                                         {aurora::LocalizedString{std::string{"2"}}}));
}

// ------------------------------------------------------------
// 判据⑧：正文原样交付（不加引号、不改写），编辑框有明文警告
// ------------------------------------------------------------
AURORA_TEST_CASE(bodies_are_delivered_verbatim_and_the_editor_warns) {
    static_cast<void>(ui::install_settings_strings());
    const std::string body = "PASSWORD=hunter2";
    Probe probe;
    probe.items = {snippet("snip-0", "secret", "", body)};
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto send_spot = h.find_button(ui::settings_label("snippets.action.send"), 1);
    AURORA_TEST_REQUIRE_MSG(send_spot.widget != nullptr, "the send button is not reachable");
    h.click(send_spot);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE_EQ(probe.sent.size(), 1U);
    AURORA_TEST_CHECK(probe.sent[0] == body);  // 原样字节：不加引号、不转义、不换算。

    auto edit = h.find_button(ui::settings_label("snippets.action.edit"), 1);
    AURORA_TEST_REQUIRE_MSG(edit.widget != nullptr, "the edit button is not reachable");
    h.click(edit);
    h.render();
    h.render();
    AURORA_TEST_REQUIRE(panel->editor_open());
    // 正文按纯文本落盘这件事，编辑框里有一句固定的警告。
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("snippets.editor.plain_text_warning")).widget !=
                      nullptr);
    auto cancel = h.find_button(ui::settings_label("snippets.editor.action.cancel"));
    AURORA_TEST_REQUIRE_MSG(cancel.widget != nullptr, "the editor cancel is not reachable");
    h.click(cancel);
    h.render();
    h.render();
    AURORA_TEST_CHECK(!panel->editor_open());
    AURORA_TEST_CHECK(probe.persisted.empty());
}

}  // namespace borealis::test_cases::itest_snippet_panel
