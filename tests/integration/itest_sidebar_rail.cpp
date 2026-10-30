// ============================================================
// 侧栏折叠轨集成用例（tests/integration/itest_sidebar_rail.cpp）
// ------------------------------------------------------------
// 屏 1 判据 ③ 那条 34 dp 常驻图标列（`codespec/UI_WORKSPACE.draft.svg`＋裁决 7.180）。
// 断言落在**控件与真实布局/绘制/派发链路的接缝**上：无头 `HeadlessSurface` + 根 `Row`
// （与改判后的装配层同形：轨在首位、分栏列在其后占 `expand(1)`），故本条同时钉住
// 「轨宽自报 34 dp 且不参与 `expand` 分母」这一读点。四件事：
// ① 34 dp × 整列高、贴窗左沿，三枚槽盒按 6/30/30/30 折位且与 `slot_at()` 同一台算式；
// ② 条身取活动档 `card_bg`、右沿一条 1 dp `card_line`（两档各读一次，证明色值来自表而非常量）；
// ③ 当前视图那一枚取 `text_dim`、另两枚取 `text_faint`——三枚身份按人已裁的「三个视图直达」；
// ④ 左键 Press 先报「碰过这一扇窗」再按槽序交回视图；轨上任何一点都不漏给身后的分栏列。
//
// 像素判读口径：描边是覆盖式抗锯齿，1.4 dp 的斜段与圆弧落在像素上的最大覆盖只有
// 0.6～1.0，故**不能**按「某像素逐通道等于 token」判色。这里把每枚像素解成
// `card_bg + t·(ink − card_bg)`，取对两枚候选 ink 残差更小的那一枚作归类（两枚 ink 与
// `card_bg` 不成比例，故该分类可分），并按此统计槽内的亮/暗墨迹。右沿那条 `card_line`
// 在两档下都会落进 `text_dim` 射线的邻域，所以扫描盒宽刻意止于第 33 列之前。
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/containers.h"
#include "framework/aurora_test.h"

// 私有头（裁决 D1① 同口径），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/conn/profile_views.h"
#include "../../src/ui/settings_panel.h"
#include "../../src/ui/sidebar_rail.h"

namespace borealis::test_cases::itest_sidebar_rail {

namespace {

namespace conn = borealis::conn;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 1200;  ///< 屏 1 那一幅的宽度量级。
constexpr int kWindowHeight = 768;  ///< 稿那条图标列的 `h=768`。
constexpr double kRailWidthDp = 34.0;
constexpr double kHairlineWidthDp = 1.0;
constexpr double kSlotTopInsetDp = 6.0;
constexpr double kSlotHeightDp = 30.0;
constexpr double kMinInkDelta = 10.0;  ///< 离底不足这个距离的像素当作没落墨（覆盖 0 的底）。

[[nodiscard]] auto nearly(double a, double b, double tolerance = 0.51) -> bool {
    return std::abs(a - b) < tolerance;
}

using Pixels = std::vector<std::uint8_t>;

/// @brief 装配层替身：三枚视图直达的来向与「碰过这扇窗」的留痕。
struct Probe {
    std::vector<conn::SidebarViewMode> views;
    std::size_t presses = 0U;

    [[nodiscard]] auto hooks() -> ui::SidebarRailHooks {
        ui::SidebarRailHooks out;
        out.select_view = [this](conn::SidebarViewMode mode) -> void { views.push_back(mode); };
        out.note_press = [this]() -> void { ++presses; };
        return out;
    }
};

/// @brief 一帧里一枚槽盒内的墨迹归类：落在 `dim` 射线附近／落在 `faint` 射线附近的像素各几枚。
struct InkTally {
    std::size_t dim = 0U;
    std::size_t faint = 0U;
};

/// @brief 把像素按「离底的偏移更像哪一枚 ink」归类（覆盖式 AA 下不能逐位等值，见文件头）。
[[nodiscard]] auto classify(const au::Color &pixel, const au::Color &bg, const au::Color &dim,
                            const au::Color &faint) -> int {
    const double vx = static_cast<double>(pixel.r) - bg.r;
    const double vy = static_cast<double>(pixel.g) - bg.g;
    const double vz = static_cast<double>(pixel.b) - bg.b;
    if (std::sqrt(vx * vx + vy * vy + vz * vz) < kMinInkDelta) {
        return -1;  // 未落墨
    }
    // 残差＝该偏移偏离「ink 射线」的距离；射线参数截到 [0,1]（覆盖不可能为负或超过满墨）。
    const auto away = [&](const au::Color &ink) -> double {
        const double dx = static_cast<double>(ink.r) - bg.r;
        const double dy = static_cast<double>(ink.g) - bg.g;
        const double dz = static_cast<double>(ink.b) - bg.b;
        const double norm = dx * dx + dy * dy + dz * dz;
        const double t = norm > 0.0 ? std::clamp((vx * dx + vy * dy + vz * dz) / norm, 0.0, 1.0) : 0.0;
        const double rx = vx - t * dx;
        const double ry = vy - t * dy;
        const double rz = vz - t * dz;
        return std::sqrt(rx * rx + ry * ry + rz * rz);
    };
    return away(dim) < away(faint) ? 0 : 1;
}

/// @brief 无头窗口 + 真布局 + 真绘制 + 真指针派发的驱动台（`itest_status_bar` 的精简腿，根改 Row）。
class Harness {
  public:
    explicit Harness(Probe &probe, int width = kWindowWidth, int height = kWindowHeight)
        : window_{make_window(width, height)}, rail_{std::make_shared<ui::SidebarRail>(probe.hooks())} {
        host_ = std::make_shared<au::OverlayHost>();
        row_ = std::make_shared<au::Row>();
        row_->set_modifier(au::Modifier{}.fill_max_size());
        row_->set_cross_axis_alignment(au::CrossAxisAlignment::Stretch);
        // 轨在首位、分栏列在后（改判后的 `rebuild_root_children()` 那一层就是这条 Row）。
        auto filler = std::make_shared<au::Column>();
        filler->set_modifier(au::Modifier{}.expand(1.0F).fill_max_size());
        auto &nodes = row_->child_nodes_mut();
        nodes.push_back(au::Node{rail_});
        nodes.push_back(au::Node{filler});
        host_->child_nodes_mut().push_back(au::Node{row_});
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
        render();  // 单帧只够入树，布局落 bounds 要第二帧（与侧栏用例同一条纪律）
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;

    auto render() -> void { (void)window_.present_root(root_); }

    /// @brief 换档之后叫醒一次重绘。
    ///
    /// 装配层那一腿是靠 `ThemeScope` 背后那枚 `State<Theme>` 置脏整棵子树（`chrome_theme_state.set()`）；
    /// 本件不吃框架主题令牌、每次重绘现读 `settings_chrome()`，因此这里只标脏自己。证的仍是同一件事：
    /// 色值是**绘制时**取的，不是建树那一刻刻进控件的常量。
    auto repaint_after_scheme_switch() -> void {
        rail_->mark_needs_paint();
        render();
        render();
    }

    [[nodiscard]] auto rail() const -> ui::SidebarRail & { return *rail_; }

    [[nodiscard]] auto width() const noexcept -> int { return window_.size().width; }
    [[nodiscard]] auto height() const noexcept -> int { return window_.size().height; }

    [[nodiscard]] auto pixels() const -> Pixels {
        Pixels out(static_cast<std::size_t>(width()) * static_cast<std::size_t>(height()) * 4U, 0U);
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
        if (x >= static_cast<std::size_t>(width()) || y >= static_cast<std::size_t>(height())) {
            return au::Color{0x11, 0x22, 0x33, 0x44};  // 越界值：画面里不可能出现的色
        }
        const std::size_t index = (y * static_cast<std::size_t>(width()) + x) * 4U;
        return au::Color{px[index], px[index + 1U], px[index + 2U], px[index + 3U]};
    }

    /// @brief 一枚槽盒内的墨迹统计；扫描止于右沿那条线之前（理由见文件头）。
    [[nodiscard]] auto ink_in_slot(const Pixels &px, std::size_t index, const ui::SettingsChrome &chrome) const
        -> InkTally {
        InkTally tally;
        const au::Rect box = rail_->slot_box(index);
        const auto x_begin = static_cast<std::size_t>(box.origin.x);
        const auto x_end = std::min(static_cast<std::size_t>(box.origin.x + box.size.width) -
                                        static_cast<std::size_t>(kHairlineWidthDp),
                                    static_cast<std::size_t>(width()));
        const auto y_begin = std::min(static_cast<std::size_t>(box.origin.y),
                                      static_cast<std::size_t>(height()));
        const auto y_end = std::min(static_cast<std::size_t>(box.origin.y + box.size.height),
                                    static_cast<std::size_t>(height()));
        for (std::size_t y = y_begin; y < y_end; ++y) {
            for (std::size_t x = x_begin; x < x_end; ++x) {
                const int kind = classify(sample(px, static_cast<double>(x), static_cast<double>(y)),
                                          chrome.card_bg, chrome.text_dim, chrome.text_faint);
                if (kind == 0) {
                    ++tally.dim;
                } else if (kind == 1) {
                    ++tally.faint;
                }
            }
        }
        return tally;
    }

    /// @brief 命中链在这一层交回的是哪一枚控件（轨是否挡住了身后那一列）。
    [[nodiscard]] auto hit(double x_dp, double y_dp) -> au::Widget * {
        const auto chain = root_.widget().hit_test_chain(
            au::Point{.x = static_cast<float>(x_dp), .y = static_cast<float>(y_dp)},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(width()),
                                      .height = static_cast<float>(height())}},
            au::BuildContext{});
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    auto pointer(au::MouseAction action, au::MouseButton button, double x_dp, double y_dp) -> bool {
        au::MouseEvent event;
        event.position = au::Point{.x = static_cast<float>(x_dp), .y = static_cast<float>(y_dp)};
        event.button = button;
        event.action = action;
        return dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 点轨中心那一列的第 `index` 枚槽（y 取槽心，避开边界折位的舍入）。
    auto click_slot(std::size_t index) -> void {
        const double y = kSlotTopInsetDp + static_cast<double>(index) * kSlotHeightDp +
                         kSlotHeightDp / 2.0;
        static_cast<void>(pointer(au::MouseAction::Press, au::MouseButton::Left, kRailWidthDp / 2.0, y));
    }

  private:
    [[nodiscard]] static auto make_window(int width, int height) -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(width, height);
        return au::Window{std::move(surface)};
    }

    au::Window window_;  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<ui::SidebarRail> rail_;
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Row> row_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;
};

}  // namespace

// ------------------------------------------------------------
// 判据①：34 dp × 整列高、贴左沿，三枚槽与折位同一台算式
// ------------------------------------------------------------
AURORA_TEST_CASE(the_rail_is_34_dp_wide_spans_the_column_and_holds_three_view_slots) {
    Probe probe;
    Harness h{probe};
    ui::SidebarRail &rail = h.rail();

    // ⑴ 宽自报常量、高跟随交叉轴；横向不占 `expand` 分母（后一格才是拉伸的那一格）。
    AURORA_TEST_CHECK_TRUE(nearly(rail.size().width, kRailWidthDp));
    AURORA_TEST_CHECK_TRUE(nearly(rail.size().height, kWindowHeight));
    const au::Rect frame = rail.paint_bounds();
    AURORA_TEST_CHECK_TRUE(nearly(frame.origin.x, 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(frame.origin.y, 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(frame.size.width, kRailWidthDp));

    // ⑵ 三枚槽＝三档视图，次序自上而下（换序会让「亮的那一枚」与展开后面板选中的视图错位）。
    const auto &order = ui::sidebar_rail_slot_order();
    AURORA_TEST_CHECK_TRUE(order[0] == conn::SidebarViewMode::All);
    AURORA_TEST_CHECK_TRUE(order[1] == conn::SidebarViewMode::Favorites);
    AURORA_TEST_CHECK_TRUE(order[2] == conn::SidebarViewMode::Groups);
    AURORA_TEST_CHECK_TRUE(rail.active_view() == conn::SidebarViewMode::All);

    // ⑶ 槽盒：首格顶内缩 6 dp、单格 30 dp、通宽 34 dp，与绘制框同系（窗口 dp）。
    for (std::size_t index = 0; index < order.size(); ++index) {
        const au::Rect box = rail.slot_box(index);
        AURORA_TEST_CHECK_TRUE(nearly(box.origin.x, 0.0));
        AURORA_TEST_CHECK_TRUE(nearly(box.origin.y, kSlotTopInsetDp + static_cast<double>(index) * kSlotHeightDp));
        AURORA_TEST_CHECK_TRUE(nearly(box.size.width, kRailWidthDp));
        AURORA_TEST_CHECK_TRUE(nearly(box.size.height, kSlotHeightDp));
    }
    // ⑷ 越界的观测点给空盒，不是脏值。
    AURORA_TEST_CHECK_TRUE(nearly(rail.slot_box(order.size()).size.width, 0.0));

    // ⑸ 折位算式与槽盒同源：格内任一点折回本格，格外（顶内缩之上、末格之下）没有格。
    AURORA_TEST_CHECK_EQ(rail.slot_at(2.0), order.size());
    AURORA_TEST_CHECK_EQ(rail.slot_at(kSlotTopInsetDp), 0U);
    AURORA_TEST_CHECK_EQ(rail.slot_at(kSlotTopInsetDp + kSlotHeightDp - 0.2), 0U);
    AURORA_TEST_CHECK_EQ(rail.slot_at(kSlotTopInsetDp + kSlotHeightDp), 1U);
    AURORA_TEST_CHECK_EQ(rail.slot_at(kSlotTopInsetDp + 2.0 * kSlotHeightDp + 15.0), 2U);
    AURORA_TEST_CHECK_EQ(rail.slot_at(kSlotTopInsetDp + 3.0 * kSlotHeightDp), order.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        const au::Rect box = rail.slot_box(index);
        AURORA_TEST_CHECK_EQ(rail.slot_at(static_cast<double>(box.origin.y + box.size.height) - 1.0), index);
    }
}

// ------------------------------------------------------------
// 判据②：条身与右沿取活动档的两枚 token（两档各读一次）
// ------------------------------------------------------------
AURORA_TEST_CASE(the_rail_paints_the_active_schemes_card_body_and_one_dp_right_hairline) {
    Probe probe;
    Harness h{probe};

    // 暗档（进程缺省档）：槽之下的空白段就是纯条身，右沿一列是那条 1 dp 线。
    const ui::SettingsChrome dark = ui::settings_chrome();
    const Pixels before = h.pixels();
    AURORA_TEST_CHECK_TRUE(h.sample(before, 16.0, 400.0) == dark.card_bg);
    AURORA_TEST_CHECK_TRUE(h.sample(before, 32.0, 400.0) == dark.card_bg);
    AURORA_TEST_CHECK_TRUE(h.sample(before, 33.0, 400.0) == dark.card_line);
    AURORA_TEST_CHECK_TRUE(h.sample(before, 33.0, 700.0) == dark.card_line);
    // 轨外第一列归身后的分栏列，不该被本件涂色。
    AURORA_TEST_CHECK_FALSE(h.sample(before, 34.0, 400.0) == dark.card_line);

    ui::set_chrome_scheme(ui::ChromeScheme::Light);
    h.repaint_after_scheme_switch();
    const ui::SettingsChrome light = ui::settings_chrome();
    const Pixels after = h.pixels();
    AURORA_TEST_CHECK_TRUE(h.sample(after, 16.0, 400.0) == light.card_bg);
    AURORA_TEST_CHECK_TRUE(h.sample(after, 33.0, 400.0) == light.card_line);
    // 两档读数不同 ⇒ 色值确实每次重绘现取活动档，而不是把建树时的十六进制刻进控件。
    AURORA_TEST_CHECK_FALSE(light.card_bg == dark.card_bg);
    AURORA_TEST_CHECK_FALSE(h.sample(after, 16.0, 400.0) == h.sample(before, 16.0, 400.0));
    ui::set_chrome_scheme(ui::ChromeScheme::Dark);  // 活动档是进程态，别把它留给下一个用例
}

// ------------------------------------------------------------
// 判据③：当前视图那一枚 text_dim、另两枚 text_faint，随 set_active_view 迁移
// ------------------------------------------------------------
AURORA_TEST_CASE(the_active_view_icon_is_the_brighter_ink_and_the_other_two_are_faint) {
    Probe probe;
    Harness h{probe};
    ui::SidebarRail &rail = h.rail();
    const ui::SettingsChrome chrome = ui::settings_chrome();
    constexpr std::size_t kSlotCount = 3U;

    // ⑴ 缺省亮第一枚（`All`）：那一枚只有 dim 墨迹，另两枚只有 faint——三枚都落了墨，不是空白列。
    for (std::size_t index = 0; index < kSlotCount; ++index) {
        const InkTally tally = h.ink_in_slot(h.pixels(), index, chrome);
        if (index == 0U) {
            AURORA_TEST_CHECK_TRUE(tally.dim > 0U);
            AURORA_TEST_CHECK_EQ(tally.faint, 0U);
        } else {
            AURORA_TEST_CHECK_TRUE(tally.faint > 0U);
            AURORA_TEST_CHECK_EQ(tally.dim, 0U);
        }
    }

    // ⑵ 换档后亮的那一枚跟着迁移，且同一时刻只有一枚亮（本件的唯一状态读点就是当前视图档）。
    for (const conn::SidebarViewMode mode : {conn::SidebarViewMode::Favorites, conn::SidebarViewMode::Groups}) {
        rail.set_active_view(mode);
        h.render();
        const std::size_t active = static_cast<std::size_t>(mode);
        std::size_t dim_slots = 0U;
        std::size_t faint_slots = 0U;
        for (std::size_t index = 0; index < kSlotCount; ++index) {
            const InkTally tally = h.ink_in_slot(h.pixels(), index, chrome);
            dim_slots += tally.dim > 0U ? 1U : 0U;
            faint_slots += tally.faint > 0U ? 1U : 0U;
            if (index == active) {
                AURORA_TEST_CHECK_TRUE(tally.dim > 0U);
                AURORA_TEST_CHECK_EQ(tally.faint, 0U);
            } else {
                AURORA_TEST_CHECK_TRUE(tally.faint > 0U);
                AURORA_TEST_CHECK_EQ(tally.dim, 0U);
            }
        }
        AURORA_TEST_CHECK_EQ(dim_slots, 1U);
        AURORA_TEST_CHECK_EQ(faint_slots, kSlotCount - 1U);
    }

    rail.set_active_view(conn::SidebarViewMode::All);
}

// ------------------------------------------------------------
// 判据③的接缝面：点击＝视图直达；上报先于命中；轨不吃键盘也不漏事件
// ------------------------------------------------------------
AURORA_TEST_CASE(a_press_on_the_rail_notes_the_window_then_asks_for_that_view) {
    Probe probe;
    Harness h{probe};
    const auto &order = ui::sidebar_rail_slot_order();

    // ⑴ 三枚各点一次：来向逐枚等于槽序表，且一次点击只发一条（Press 触发，Release 不再报）。
    for (std::size_t index = 0; index < order.size(); ++index) {
        const std::size_t before_views = probe.views.size();
        const std::size_t before_presses = probe.presses;
        h.click_slot(index);
        AURORA_TEST_REQUIRE_MSG(probe.views.size() == before_views + 1U, "one press must ask once");
        AURORA_TEST_CHECK_TRUE(probe.views.back() == order[index]);
        AURORA_TEST_CHECK_EQ(probe.presses, before_presses + 1U);
    }

    // ⑵ 轨上任何一点都被本件消费：命中链末位是轨（含槽间空隙与轨下半段），身后的列拿不到。
    AURORA_TEST_CHECK_TRUE(std::string_view{h.hit(17.0, 120.0)->type_name()} == "SidebarRail");
    AURORA_TEST_CHECK_TRUE(std::string_view{h.hit(17.0, 700.0)->type_name()} == "SidebarRail");

    // ⑶ 槽外的空白段仍算「碰过这一扇窗」，但不问视图（上报与命中分账，与横幅 D7 同一条）。
    const std::size_t views = probe.views.size();
    const std::size_t presses = probe.presses;
    static_cast<void>(h.pointer(au::MouseAction::Press, au::MouseButton::Left, 17.0, 700.0));
    AURORA_TEST_CHECK_EQ(probe.presses, presses + 1U);
    AURORA_TEST_CHECK_EQ(probe.views.size(), views);

    // ⑷ 右键与移动不触发视图直达，也不上报（本件只认左键 Press）。
    static_cast<void>(h.pointer(au::MouseAction::Press, au::MouseButton::Right, 17.0, 21.0));
    static_cast<void>(h.pointer(au::MouseAction::Move, au::MouseButton::Left, 17.0, 21.0));
    AURORA_TEST_CHECK_EQ(probe.presses, presses + 1U);
    AURORA_TEST_CHECK_EQ(probe.views.size(), views);

    // ⑸ 常驻标识列不该吃键盘：不可获焦，否则点完轨敲的第一句落进 chrome。
    AURORA_TEST_CHECK_FALSE(h.rail().focusable());
}

}  // namespace borealis::test_cases::itest_sidebar_rail
