/// 测试类型: integration
/// 目标单元: src/ui/status_bar.cpp（状态栏件的布局/绘制腿）+ src/ui/status_bar_format.cpp（经它消费）
/// 测试说明: 以无头窗口 + 真实布局与绘制驱动「状态 → 测宽 → 降级算式 → 落笔」这条链
///           （`SPEC.FEAT.PREF.02` 的即时反馈面，屏 1 判据 ⑨／⑩，裁决 7.179）。本件补的正是
///           7.179⑧ 登记的那一处已知未覆盖——装配层 `refresh_status_bar_ui()` 的取数与分档由既有
///           面板用例与真机走查夹住，而「把 `StatusBar` 挂上无头窗、按实测宽度验 `plan()` 与
///           `anchor_box()`」此前没有证人。断言落在**控件与真实布局/绘制链路的接缝**上，不重测
///           `utest_status_bar_format` 已铺满的纯算式（省略序、几何、三个格式化件，十二案）。四件事：
///           ① 28 dp 高、横向填满、贴窗底；条顶那条 1 dp 线与条身各取 chrome 表的一枚 token（像素读数）；
///           ② 宽窗下每格都上屏、右端锚点钉在右内缩 16 dp 处，且数据格的最右沿不让进锚点左侧的格距；
///           ③ 窄窗下按注册次序从右向左省略、保住的永远是注册次序的前缀（不跳着放），锚点连自己都
///              放不下时让位并留下 `right_anchor_dropped` 的读数；
///           ④ 空文本的格不占省略序的位置，圆点／分隔线的前缀宽度确实计进了实占宽度；
///           ⑤ 顺带钉住观测点的坐标系：`anchor_box()` 与 `paint_bounds()` 同系（窗口 dp），
///              而不是控件局部 dp——`status_bar.h` 那句「本控件局部系」按这一实测改口（§4.2 第 12 条）。
///
///           驱动台照 `itest_connection_sidebar` 的精简腿：无头 `HeadlessSurface` + 根 `Column`
///           （与装配层 `rebuild_root_children()` 同形：一格 expand(1) 的填充件在前、状态栏在末位），
///           故本条同时把「末位 28 dp 不参与 expand 分母」这件事钉进布局读数。

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/widget/containers.h"
#include "framework/aurora_test.h"

// 私有头（裁决 D1① 同口径），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_panel.h"
#include "../../src/ui/status_bar.h"

namespace borealis::test_cases::itest_status_bar {

namespace {

namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowHeight = 240;
constexpr int kWideWidth = 1400;  ///< 稿屏 1 那一幅的宽度量级：八格加锚点全放得下。
/// @brief 注册次序的期望串（十枚里本件交的八格，次序按 `status_bar_registration_order()`）。
constexpr std::string_view kMockOrder =
    "connection,encoding,grid_size,font_size,theme,scrollback,clipboard_policy,input_latency";
constexpr double kBarHeightDp = 28.0;
constexpr double kRightInsetDp = 16.0;
constexpr double kGapDp = 20.0;
constexpr double kTextLineHeightDp = 16.0;

/// @brief 稿屏 1 那条上实画的八格（判据 ⑨），次序即装配层按注册次序交来的次序。
///
/// 文本取稿面的 ASCII 形态（`82x24` 那格在实现里由词条拼出乘号，本件只需要长度可比）。
auto mock_state(bool with_anchor = true) -> ui::StatusBarState {
    ui::StatusBarState state;
    state.slots = {
        ui::StatusBarSlot{.id = "connection",
                          .text = U"root@web-01:22",
                          .tone = ui::StatusBarTone::Dim,
                          .leading_dot = true,
                          .leading_separator = false},
        ui::StatusBarSlot{.id = "encoding", .text = U"UTF-8"},
        ui::StatusBarSlot{.id = "grid_size", .text = U"82x24"},
        ui::StatusBarSlot{.id = "font_size", .text = U"14 pt"},
        ui::StatusBarSlot{.id = "theme", .text = U"theme Dracula"},
        ui::StatusBarSlot{.id = "scrollback", .text = U"scrollback 10,000"},
        ui::StatusBarSlot{.id = "clipboard_policy",
                          .text = U"clipboard: deny",
                          .tone = ui::StatusBarTone::Warning,
                          .leading_dot = false,
                          .leading_separator = true},
        ui::StatusBarSlot{.id = "input_latency", .text = U"P95 12 ms"},
    };
    if (with_anchor) {
        state.anchor = U"Ctrl+Shift+P";
    }
    return state;
}

[[nodiscard]] auto nearly(double a, double b, double tolerance = 0.51) -> bool {
    return std::abs(a - b) < tolerance;
}

/// @brief 把身份表拼成可读串，断言红时能直接看出省略落在哪一格。
[[nodiscard]] auto join_ids(const std::vector<std::string_view> &ids) -> std::string {
    std::string out;
    for (const std::string_view id : ids) {
        if (!out.empty()) {
            out += ',';
        }
        out.append(id);
    }
    return out;
}

using Pixels = std::vector<std::uint8_t>;

/// @brief 无头窗口 + 真布局 + 真绘制（`itest_connection_sidebar` 精简腿，根改 Column）。
class Harness {
  public:
    explicit Harness(int width, int height = kWindowHeight) : window_(make_window(width, height)) {
        // 与装配层同形：`OverlayHost` 是场景根，其子节点 [0] 放满幅 `root_column`，
        // 列内常规内容（expand 1）在前、状态栏在末位（`rebuild_root_children()` 的那一张表）。
        host_ = std::make_shared<au::OverlayHost>();
        column_ = std::make_shared<au::Column>();
        column_->set_modifier(au::Modifier{}.fill_max_size());
        column_->set_cross_axis_alignment(au::CrossAxisAlignment::Stretch);
        // 填充件用空 `Column` 而不是 `au::Text`：Text 的 `on_layout` 回的是自身文本高，`expand(1)`
        // 分给它的那 212 dp 它不接，于是状态栏会停在 y=28 而不是窗底——装配层那一格是 `WorkspaceView`，
        // 它按 `c.max` 回，本件要复刻的是后者。
        auto filler = std::make_shared<au::Column>();
        filler->set_modifier(au::Modifier{}.expand(1.0F).fill_max_size());
        bar_ = std::make_shared<ui::StatusBar>();
        auto &nodes = column_->child_nodes_mut();
        nodes.push_back(au::Node{filler});
        nodes.push_back(au::Node{bar_});
        host_->child_nodes_mut().push_back(au::Node{column_});
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;

    auto render() -> void { (void)window_.present_root(root_); }

    [[nodiscard]] auto bar() const -> ui::StatusBar & { return *bar_; }

    [[nodiscard]] auto pixels() const -> Pixels {
        Pixels out(static_cast<std::size_t>(window_.size().width) *
                       static_cast<std::size_t>(window_.size().height) * 4U,
                   0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    /// @brief 取一帧缓冲像素（headless 与 scale 1 下 dp 与像素逐位同格）。
    [[nodiscard]] auto sample(const Pixels &px, double x_dp, double y_dp) const -> au::Color {
        const auto x = static_cast<std::size_t>(x_dp);
        const auto y = static_cast<std::size_t>(y_dp);
        if (x >= static_cast<std::size_t>(window_.size().width) ||
            y >= static_cast<std::size_t>(window_.size().height)) {
            return au::Color{0x11, 0x22, 0x33, 0x44};  // 越界值：画面里不可能出现的色
        }
        const std::size_t index =
            (y * static_cast<std::size_t>(window_.size().width) + x) * 4U;
        return au::Color{px[index], px[index + 1U], px[index + 2U], px[index + 3U]};
    }

  private:
    [[nodiscard]] static auto make_window(int width, int height) -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(width, height);
        return au::Window{std::move(surface)};
    }

    au::Window window_;
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Column> column_;
    std::shared_ptr<ui::StatusBar> bar_;
    au::Node root_;
};

}  // namespace

// ------------------------------------------------------------
// 判据② ＋ 判据⑨的尺寸面：宽窗八格全上屏、锚点钉右、指标线在条顶
// ------------------------------------------------------------
AURORA_TEST_CASE(the_strip_is_28_dp_tall_sits_at_the_window_bottom_and_pins_the_anchor_right) {
    Harness h{kWideWidth};
    ui::StatusBar &bar = h.bar();
    bar.set_state(mock_state());
    h.render();
    h.render();

    // ⑴ 高是常量 28 dp、横向填满，且排在填充件之下贴着窗底（末位不参与 expand 分母）。
    AURORA_TEST_CHECK_TRUE(nearly(bar.size().height, kBarHeightDp));
    AURORA_TEST_CHECK_TRUE(nearly(bar.size().width, kWideWidth));
    const au::Rect frame = bar.paint_bounds();
    AURORA_TEST_CHECK_TRUE(nearly(frame.origin.y, kWindowHeight - kBarHeightDp));
    AURORA_TEST_CHECK_TRUE(nearly(frame.size.height, kBarHeightDp));

    // ⑵ 像素读数：条顶那 1 dp 线与条身各取 chrome 表的一枚（与稿 `#3A3C4C` 的偏差登记在 7.179④）。
    const ui::SettingsChrome chrome = ui::settings_chrome();
    const Pixels px = h.pixels();
    AURORA_TEST_CHECK_TRUE(h.sample(px, 120.0, frame.origin.y + 0.0) == chrome.card_line);
    AURORA_TEST_CHECK_TRUE(h.sample(px, 120.0, frame.origin.y + kBarHeightDp - 2.0) == chrome.card_bg);

    // ⑶ 宽窗下没有一格被省略，且次序就是交进来的次序。
    const ui::StatusBarPlan &plan = bar.plan();
    AURORA_TEST_CHECK_EQ(plan.shown.size(), 8U);
    AURORA_TEST_CHECK_TRUE(plan.omitted.empty());
    AURORA_TEST_CHECK_TRUE(join_ids(plan.shown) == kMockOrder);
    AURORA_TEST_CHECK_FALSE(plan.right_anchor_dropped);

    // ⑷ 锚点钉在右内缩 16 dp 处、文本行垂直居中（盒原点＝绘制盒原点＋内缩，与 `frame` 同系）；
    //     数据格的最右沿不让进它左侧那个格距。
    const au::Rect anchor = bar.anchor_box();
    AURORA_TEST_CHECK_TRUE(anchor.size.width > 0.0F);
    AURORA_TEST_CHECK_TRUE(nearly(anchor.size.height, kTextLineHeightDp));
    AURORA_TEST_CHECK_TRUE(nearly(anchor.origin.x + anchor.size.width, kWideWidth - kRightInsetDp));
    AURORA_TEST_CHECK_TRUE(
        nearly(anchor.origin.y, frame.origin.y + (kBarHeightDp - kTextLineHeightDp) / 2.0));
    AURORA_TEST_CHECK_TRUE(plan.content_width_dp + kGapDp <= anchor.origin.x + 0.5);
}

// ------------------------------------------------------------
// 判据③：窄窗按注册次序从右向左省略，保住的是前缀；锚点放不下时让位
// ------------------------------------------------------------
AURORA_TEST_CASE(a_narrow_window_drops_rightmost_slots_in_registration_order_and_never_skips) {
    const std::vector<int> widths{kWideWidth, 900, 520, 380, 260};
    std::size_t previous_shown = 8U;
    bool saw_omission = false;
    for (const int width : widths) {
        Harness h{width};
        ui::StatusBar &bar = h.bar();
        bar.set_state(mock_state());
        h.render();
        h.render();

        const ui::StatusBarPlan &plan = bar.plan();
        // ⑴ 单调：窗口越窄，上屏的格数不增。
        AURORA_TEST_CHECK_TRUE(plan.shown.size() <= previous_shown);
        previous_shown = plan.shown.size();

        // ⑵ 次序不漂：上屏的永远是注册次序的前缀，省略项紧接着它排下去——两表拼起来逐格复现。
        AURORA_TEST_CHECK_EQ(plan.shown.size() + plan.omitted.size(), 8U);
        const std::string shown = join_ids(plan.shown);
        AURORA_TEST_CHECK_TRUE(std::string_view{kMockOrder}.substr(0, shown.size()) == shown);

        // ⑶ 放得下的格连同前缀与格距一起放得下：实占宽度不超过整条减去右内缩。
        AURORA_TEST_CHECK_TRUE(plan.content_width_dp <= static_cast<double>(width) - kRightInsetDp);

        // ⑷ 锚点仍画时，数据格的最右沿不进它左侧那个格距；锚点被让掉时盒必须为空。
        if (plan.right_anchor_dropped) {
            AURORA_TEST_CHECK_TRUE(nearly(bar.anchor_box().size.width, 0.0));
            AURORA_TEST_CHECK_TRUE(plan.shown.size() >= 1U);  // 让掉锚点正是为了给数据格腾地方
        } else {
            AURORA_TEST_CHECK_TRUE(bar.anchor_box().size.width > 0.0F);
            AURORA_TEST_CHECK_TRUE(plan.content_width_dp + kGapDp <= bar.anchor_box().origin.x + 0.5);
        }
        saw_omission = saw_omission || !plan.omitted.empty();
    }
    // ⑸ 这套宽度里最窄那一档确实省略了，而且省的不是首格（否则「保住前缀」这条没有意义）。
    AURORA_TEST_CHECK_TRUE(saw_omission);
    {
        Harness h{260};
        h.bar().set_state(mock_state());
        h.render();
        h.render();
        AURORA_TEST_CHECK_TRUE(h.bar().plan().shown.size() < 8U);
        AURORA_TEST_CHECK_TRUE(!h.bar().plan().shown.empty());
    }
}

// ------------------------------------------------------------
// 判据③的下沿：锚点只在「它自己挡住了一切」时才让位，宽度回头它就回来
// ------------------------------------------------------------
AURORA_TEST_CASE(the_anchor_yields_only_when_it_blocks_every_slot_and_returns_with_the_width) {
    ui::StatusBarState state;
    state.slots = {ui::StatusBarSlot{.id = "connection", .text = U"up"}};
    state.anchor = U"Ctrl+Shift+P";

    Harness tight{128};
    tight.bar().set_state(state);
    tight.render();
    tight.render();
    AURORA_TEST_CHECK_TRUE(tight.bar().plan().right_anchor_dropped);
    AURORA_TEST_CHECK_TRUE(nearly(tight.bar().anchor_box().size.width, 0.0));
    AURORA_TEST_CHECK_EQ(tight.bar().plan().shown.size(), 1U);

    Harness looser{260};
    looser.bar().set_state(std::move(state));
    looser.render();
    looser.render();
    AURORA_TEST_CHECK_FALSE(looser.bar().plan().right_anchor_dropped);
    AURORA_TEST_CHECK_TRUE(looser.bar().anchor_box().size.width > 0.0F);
    AURORA_TEST_CHECK_EQ(looser.bar().plan().shown.size(), 1U);
}

// ------------------------------------------------------------
// 判据④：空文本的格不占位置；前缀宽度计进实占宽度
// ------------------------------------------------------------
AURORA_TEST_CASE(an_empty_slot_takes_no_place_and_prefixes_are_charged_to_the_content_width) {
    Harness h{kWideWidth};
    ui::StatusBar &bar = h.bar();
    ui::StatusBarState state = mock_state();
    state.slots[1].text.clear();  // `encoding` 本轮没有读数
    bar.set_state(state);
    h.render();
    h.render();

    // ⑴ 空文本既不上屏也不进省略表——否则关掉一项会把后一项无声挤出屏。
    const std::string ids = join_ids(bar.plan().shown) + "|" + join_ids(bar.plan().omitted);
    AURORA_TEST_CHECK_TRUE(ids.find("encoding") == std::string::npos);
    AURORA_TEST_CHECK_EQ(bar.plan().shown.size() + bar.plan().omitted.size(), 7U);

    // ⑵ 圆点（4+5）与分隔线（1.4+12.6）的宽度确实由 `prefix_dp` 计进实占宽度。
    const double with_prefixes = bar.plan().content_width_dp;
    ui::StatusBarState plain = state;
    plain.slots[0].leading_dot = false;
    plain.slots[6].leading_separator = false;
    bar.set_state(plain);
    h.render();
    const double without_prefixes = bar.plan().content_width_dp;
    AURORA_TEST_CHECK_TRUE(nearly(with_prefixes - without_prefixes, 23.0, 0.01));
}

// ------------------------------------------------------------
// 判据⑩的形态面：不可获焦（也不认点击）
// ------------------------------------------------------------
AURORA_TEST_CASE(the_strip_never_takes_focus) {
    Harness h{kWideWidth};
    AURORA_TEST_CHECK_FALSE(h.bar().focusable());
}

}  // namespace borealis::test_cases::itest_status_bar
