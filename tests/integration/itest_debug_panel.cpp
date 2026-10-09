/// 测试类型: integration
/// 目标单元: src/ui/debug_panel.cpp（`SPEC.NF.RELI.01` 的调试面板，三族计数器的上屏形态）
/// 测试说明: 断言的是**面板的呈现与开合接线**，不是计数算式本身（解析降级计数由 `utest_vt_parser`
///           逐条守，「读线程喂入 → 状态机 → 主线程取值」的透传由 `utest_session` 守）。本件要证的是：
///           ① **一份快照画四行**（标题 + 解析 / 解码 / 队列），行里的数字是**用例侧**按快照现算的
///              预期串（`settings_label` 同式各算一遍），取错一个字段或漏代一个模板参数都会转红；
///           ② **空快照只画 empty 那一行**而没有任何计数行；
///           ③ **对话框常驻**：第二次 toggle 关框，但 `overlay_count()` 不动——开合只翻可见性，
///              不在派发栈外动 `remove_overlay`（关闭态的 `Dialog` 不渲染也不参与命中，属框架自陈）；
///           ④ **重开必经 `set_content` 重建**：第二份快照的数字上屏、第一份的逐字消失——面板显示
///              的是「打开那一刻」的快照，滞留旧数就是谎报现状；
///           ⑤⑥ **焦点作用域按深度对账（裁决 7.81② 同款）**：栈外打开自压恰一次、toggle 关闭弹回；
///              而**栈外打开 + 栈内点「关闭」**这一交叉路径里，`close()` 自己弹过一次之后 on_close
///              的对账必须判出「不再欠一次」，双弹会把祖先浮层（如设置面板）的作用域错弹掉；
///           ⑦ **析构把浮层与作用域都还回去**（裁决 7.67 的时序）。
///
///           与 `itest_startup_notice` 同一条宿主构造事实：`OverlayHost` 的浮层序号从「基础内容之后」
///           起算且 `overlay_count()` **不含基础内容**，故宿主必须带一个基础子节点，「弹了一层」读数是 1。
///           显示串取 `Text::accessibility_label()`（未绘制时按缺省 locale 现场解析），故 ①~⑥ 不必开窗。
///           中文一律经词条表在运行期交出，本文件不写中文字面量（§4.3 第 14 条）。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/i18n/localized_string.h"
#include "aurora/widget/button.h"
#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/debug_panel.h"
#include "../../src/ui/settings_i18n.h"

namespace borealis::test_cases::itest_debug_panel {

namespace au = aurora;

using borealis::ui::DebugPanel;
using borealis::ui::DebugSessionSnapshot;

namespace {

/// @brief 造一份「只有字段差异」的快照（④ 的两份现场要能各挪一组数字）。
[[nodiscard]] auto snapshot(std::string title, std::uint64_t seed) -> DebugSessionSnapshot {
    return DebugSessionSnapshot{
        .title = std::move(title),
        .parse_ignored = seed,
        .parse_cancelled = seed + 1U,
        .decode_replaced = seed + 2U,
        .decode_code_points = seed + 3U,
        .encode_unrepresentable = seed + 9U,
        .queue_pending = seed + 4U,
        .queue_peak_pending = seed + 5U,
        .queue_overloads = seed + 6U,
        .queue_merges = seed + 7U,
        .queue_yields = seed + 8U,
    };
}

/// @brief 数值 → 上屏取值串（与实现件同式而**用例侧独立算一遍**，取错即转红）。
[[nodiscard]] auto count(std::uint64_t value) -> au::LocalizedString {
    return au::LocalizedString{std::to_string(value)};
}

/// @brief 按快照现算某一行词条的预期显示串。
[[nodiscard]] auto expected_parse(const DebugSessionSnapshot &s) -> std::string {
    return borealis::ui::settings_label("diagnostics.parse", {count(s.parse_ignored), count(s.parse_cancelled)});
}

[[nodiscard]] auto expected_decode(const DebugSessionSnapshot &s) -> std::string {
    return borealis::ui::settings_label("diagnostics.decode",
                                        {count(s.decode_replaced), count(s.decode_code_points)});
}

[[nodiscard]] auto expected_encode(const DebugSessionSnapshot &s) -> std::string {
    return borealis::ui::settings_label("diagnostics.encode", {count(s.encode_unrepresentable)});
}

[[nodiscard]] auto expected_queue(const DebugSessionSnapshot &s) -> std::string {
    return borealis::ui::settings_label("diagnostics.queue",
                                        {count(s.queue_pending),
                                         count(s.queue_peak_pending),
                                         count(s.queue_overloads),
                                         count(s.queue_merges),
                                         count(s.queue_yields)});
}

[[nodiscard]] auto snapshot_row_count(const DebugSessionSnapshot &) -> std::size_t { return 5U; }

/// @brief 收集一棵子树里全部 `Text` 的显示串（按树序）。
auto collect_labels(const au::Widget &widget, std::vector<std::string> &out) -> void {
    if (const auto *text = dynamic_cast<const au::Text *>(&widget)) {
        out.push_back(text->accessibility_label());
    }
    for (const au::Node &child : widget.child_nodes()) {
        collect_labels(child.widget(), out);
    }
}

[[nodiscard]] auto drawn_labels(const au::Widget &root) -> std::vector<std::string> {
    std::vector<std::string> out;
    collect_labels(root, out);
    return out;
}

[[nodiscard]] auto contains(const std::vector<std::string> &labels, std::string_view needle) -> bool {
    return std::find(labels.begin(), labels.end(), std::string{needle}) != labels.end();
}

/// @brief 造一个「只有基础内容」的浮层宿主（序号哨兵，同 `itest_startup_notice`）。
[[nodiscard]] auto make_host(std::shared_ptr<au::Text> &out_base) -> std::shared_ptr<au::OverlayHost> {
    auto host = std::make_shared<au::OverlayHost>();
    out_base = std::make_shared<au::Text>(
        aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
    static_cast<void>(host->add_overlay(au::Node{out_base}));  // 占住子节点 [0]：基础内容
    return host;
}

}  // namespace

AURORA_TEST_CASE(one_snapshot_draws_its_title_and_all_counter_rows_with_the_given_numbers) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base);
    au::FocusManager focus;
    DebugPanel panel{*host, focus};

    const DebugSessionSnapshot first = snapshot("snap-1", 100U);
    const DebugSessionSnapshot second = snapshot("snap-2", 200U);
    panel.toggle({first, second});
    AURORA_TEST_REQUIRE(panel.is_showing());
    AURORA_TEST_REQUIRE(panel.dialog() != nullptr);

    const std::vector<std::string> labels = drawn_labels(*panel.dialog());
    // 标题行 + 两份快照各五行（标题 + 四计数行）。
    AURORA_TEST_REQUIRE_EQ(labels.size(), 1U + 2U * snapshot_row_count(first));
    AURORA_TEST_CHECK_TRUE(contains(labels, borealis::ui::settings_label("diagnostics.title")));
    for (const DebugSessionSnapshot &s : {first, second}) {
        AURORA_TEST_CHECK_TRUE(contains(labels, s.title));
        AURORA_TEST_CHECK_TRUE(contains(labels, expected_parse(s)));
        AURORA_TEST_CHECK_TRUE(contains(labels, expected_decode(s)));
        AURORA_TEST_CHECK_TRUE(contains(labels, expected_encode(s)));
        AURORA_TEST_CHECK_TRUE(contains(labels, expected_queue(s)));
    }
    // 模板参数没代进去就会留下占位符。
    for (const std::string &label : labels) {
        AURORA_TEST_CHECK_TRUE(label.find("{0}") == std::string::npos);
    }
}

AURORA_TEST_CASE(an_empty_snapshot_list_draws_the_empty_row_and_no_counter_rows) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base);
    au::FocusManager focus;
    DebugPanel panel{*host, focus};

    panel.toggle({});
    AURORA_TEST_REQUIRE(panel.is_showing());
    const std::vector<std::string> labels = drawn_labels(*panel.dialog());
    AURORA_TEST_REQUIRE_EQ(labels.size(), 2U);  // 面板标题 + 空态那一行
    AURORA_TEST_CHECK_TRUE(contains(labels, borealis::ui::settings_label("diagnostics.empty")));
    // 任何一行都不带计数措辞（空态下画一份「全零」的假快照就是谎报有会话）。
    AURORA_TEST_CHECK_TRUE(
        contains(labels, borealis::ui::settings_label("diagnostics.parse", {count(0U), count(0U)})) == false);
}

AURORA_TEST_CASE(toggling_twice_closes_the_dialog_but_keeps_the_overlay_slot) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base);
    au::FocusManager focus;
    DebugPanel panel{*host, focus};

    panel.toggle({snapshot("snap", 1U)});
    AURORA_TEST_REQUIRE(panel.is_showing());
    AURORA_TEST_REQUIRE_EQ(host->overlay_count(), 1U);
    const au::Dialog *opened = panel.dialog();

    panel.toggle({});  // 开着时传入的快照被忽略
    AURORA_TEST_CHECK_FALSE(panel.is_showing());
    AURORA_TEST_CHECK_EQ(host->overlay_count(), 1U);       // 常驻：关框不摘浮层
    AURORA_TEST_CHECK_TRUE(panel.dialog() == opened);  // 也不换句柄
}

AURORA_TEST_CASE(reopening_rebuilds_the_content_from_the_new_snapshot) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base);
    au::FocusManager focus;
    DebugPanel panel{*host, focus};

    const DebugSessionSnapshot stale = snapshot("stale", 7U);
    panel.toggle({stale});
    panel.toggle({});

    const DebugSessionSnapshot fresh = snapshot("fresh", 40U);
    panel.toggle({fresh});
    AURORA_TEST_REQUIRE(panel.is_showing());
    const std::vector<std::string> labels = drawn_labels(*panel.dialog());
    AURORA_TEST_CHECK_TRUE(contains(labels, expected_parse(fresh)));
    AURORA_TEST_CHECK_TRUE(contains(labels, expected_queue(fresh)));
    // 旧快照的每一行都得消失：滞留一个数字就是谎报现状。
    AURORA_TEST_CHECK_FALSE(contains(labels, stale.title));
    AURORA_TEST_CHECK_FALSE(contains(labels, expected_parse(stale)));
    AURORA_TEST_CHECK_FALSE(contains(labels, expected_decode(stale)));
    AURORA_TEST_CHECK_FALSE(contains(labels, expected_queue(stale)));
}

AURORA_TEST_CASE(an_out_of_stack_open_pushes_the_scope_once_and_toggle_close_pops_it_back) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base);
    au::FocusManager focus;
    DebugPanel panel{*host, focus};

    AURORA_TEST_REQUIRE_EQ(focus.scope_depth(), 0U);
    panel.toggle({snapshot("snap", 1U)});
    // 编程调用不在任何派发栈内，`show()` 只置位，本件自补一次（文件头③）。
    AURORA_TEST_CHECK_EQ(focus.scope_depth(), 1U);

    panel.toggle({});
    AURORA_TEST_CHECK_EQ(focus.scope_depth(), 0U);  // toggle 关框同样要把那一次弹回来

    // 再开再关一轮：对账不粘状态。
    panel.toggle({snapshot("snap", 2U)});
    AURORA_TEST_CHECK_EQ(focus.scope_depth(), 1U);
    panel.toggle({});
    AURORA_TEST_CHECK_EQ(focus.scope_depth(), 0U);
}

AURORA_TEST_CASE(destroying_the_panel_while_open_returns_both_the_overlay_and_the_scope) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base);
    au::FocusManager focus;

    {
        DebugPanel panel{*host, focus};
        panel.toggle({snapshot("snap", 1U)});
        AURORA_TEST_REQUIRE(panel.is_showing());
        AURORA_TEST_REQUIRE_EQ(focus.scope_depth(), 1U);
    }
    AURORA_TEST_CHECK_EQ(host->overlay_count(), 0U);
    AURORA_TEST_CHECK_EQ(focus.scope_depth(), 0U);
}

#ifdef AURORA_BACKEND_HEADLESS

namespace {

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 600;

/// @brief 带真实布局与真实指针派发的驱动台（浮层挂在无头窗口的场景根上）。
class Harness {
  public:
    Harness() {
        host_ = make_host(base_);
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    [[nodiscard]] auto attach() -> std::unique_ptr<DebugPanel> {
        return std::make_unique<DebugPanel>(*host_, focus_);
    }

    auto render() -> void { static_cast<void>(window_.present_root(root_)); }

    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const std::vector<au::HitNode> chain = chain_at(x_dp, y_dp);
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    [[nodiscard]] auto chain_at(float x_dp, float y_dp) -> std::vector<au::HitNode> {
        return root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
    }

    /// @brief 在窗口内扫描，取出第一个指定类型的控件与其命中点（框体 480 dp 居中于 900 dp，从框内侧起扫）。
    struct Spot {
        au::Widget *widget = nullptr;
        float x = 0.0F;
        float y = 0.0F;
    };

    [[nodiscard]] auto find_first(std::string_view type_name) -> Spot {
        for (float y = 4.0F; y < static_cast<float>(kWindowHeight) - 4.0F; y += 2.0F) {
            for (float x = 212.0F; x < static_cast<float>(kWindowWidth) - 212.0F; x += 2.0F) {
                au::Widget *widget = hit(x, y);
                if (widget == nullptr || type_name != std::string_view{widget->type_name()}) {
                    continue;
                }
                return Spot{.widget = widget, .x = x, .y = y};
            }
        }
        return Spot{};
    }

    [[nodiscard]] auto overlay_count() const -> std::size_t { return host_->overlay_count(); }

    [[nodiscard]] auto focus_manager() -> au::FocusManager & { return focus_; }

    auto click(float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        for (const au::MouseAction action : {au::MouseAction::Press, au::MouseAction::Release}) {
            event.action = action;
            static_cast<void>(dispatcher_.dispatch_mouse(root_.widget(), event, &focus_));
        }
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
    aurora::EventDispatcher dispatcher_;  ///< 本驱动台私有的连击判定与指针捕获状态。
};

}  // namespace

AURORA_TEST_CASE(a_real_click_on_the_close_button_pops_the_scope_exactly_once_across_the_two_paths) {
    borealis::ui::install_settings_strings();
    Harness h;
    std::unique_ptr<DebugPanel> panel = h.attach();

    // 栈外打开：本件自压一次作用域（深度 0 → 1）。
    panel->toggle({snapshot("snap", 1U)});
    h.render();
    AURORA_TEST_REQUIRE(panel->is_showing());
    AURORA_TEST_REQUIRE_EQ(h.focus_manager().scope_depth(), 1U);

    const Harness::Spot button = h.find_first("Button");
    AURORA_TEST_REQUIRE_MSG(button.widget != nullptr, "no dispatch-reachable close button");

    // 栈内点「关闭」：`Dialog::close()` 自己弹掉那一次（1 → 0），而 on_close 的对账必须判出
    // 「当前深度不再高于那份读数」而不重复弹——多弹的那一次会错弹祖先浮层的作用域。
    h.click(button.x, button.y);
    AURORA_TEST_CHECK_FALSE(panel->is_showing());
    AURORA_TEST_CHECK_EQ(h.focus_manager().scope_depth(), 0U);
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);  // 关框不摘浮层（③ 的派发路径版本）

    // 关掉之后再排一帧：关闭态不渲染也不参与命中，链上不再交回那枚按钮。
    h.render();
    AURORA_TEST_CHECK_TRUE(h.find_first("Button").widget == nullptr);
}

#endif

}  // namespace borealis::test_cases::itest_debug_panel
