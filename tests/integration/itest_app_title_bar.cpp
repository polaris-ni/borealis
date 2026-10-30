// ============================================================
// 屏 1 自绘标题栏集成用例（tests/integration/itest_app_title_bar.cpp）
// ------------------------------------------------------------
// 屏 1 顶部那条 36 dp 带的落地判据（`codespec/UI_WORKSPACE.draft.svg`＋裁决 7.181）。
// 断言同样落在**控件与真实布局/绘制/派发链路的接缝**上：无头 `HeadlessSurface` + 根 `Column`
// （与装配层同形：带在首位、其下那一格占 `expand(1)`），故本条同时钉住「带高自报 36 dp 且不参与
// `expand` 分母」这一读点。五件事：
// ① 36 dp × 通栏、贴窗顶，三枚钮的热区按稿面右锚内缩（123／94／27）、统一 28 dp 宽、通栏高，
//    彼此不重叠且留得下「F11」那一格；
// ② 条身／底缘线／竖线／徽标取活动档的四枚 token，两档各读一次（证明色值来自表而非常量）；
// ③ 四段墨迹各归各的 ink 档：标题 `text`、上下文与提示 `text_faint`、三枚钮 `text_dim`；
// ④ 左键 Press 命中哪一枚就报哪一枚，✕ 走的是接管腿（确认闸门）而不是 `WindowChrome::close()`，
//    空白区那一按交回基类（不报任何动作），本件不吃键盘；
// ⑤ 全屏档这条带自报 0 高、不落墨、不接点击，退出即回来（人已裁的「全屏就不挂这条」）。
//
// 像素判读口径沿用折叠轨那一条：描边与字形是覆盖式抗锯齿，不能按「某像素逐通道等于 token」判色，
// 故把每枚像素解成 `bg + t·(ink − bg)` 并按对两枚候选 ink 的残差归类。这里 `bg` 是条身 `card_bg`，
// 候选 ink 逐段不同（见各段注释）；徽标那一枚是**填充**圆，故中心区可直接等值比较。
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/containers.h"
#include "framework/aurora_test.h"

// 私有头（裁决 D1① 同口径），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/app_title_bar.h"
#include "../../src/ui/settings_panel.h"

namespace borealis::test_cases::itest_app_title_bar {

namespace {

namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 1500;  ///< 屏 1 那一幅的宽度量级（右锚内缩因此可直接按稿面比）。
constexpr int kWindowHeight = 900;
constexpr double kBarHeightDp = 36.0;
constexpr double kHairlineWidthDp = 1.0;
constexpr double kButtonHitWidthDp = 28.0;
constexpr double kMinimizeCenterInsetDp = 123.0;
constexpr double kMaximizeCenterInsetDp = 94.0;
constexpr double kCloseCenterInsetDp = 27.0;
constexpr double kSeparatorInsetDp = 150.0;
constexpr double kTitleLeftDp = 38.0;
constexpr double kBadgeCenterXDp = 20.0;
constexpr double kBadgeCenterYDp = 18.0;
constexpr double kHintRightInsetDp = 70.0;
constexpr double kMinInkDelta = 10.0;  ///< 离底不足这个距离的像素当作没落墨（覆盖 0 的底）。

constexpr int kFullscreenWidth = 1920;   ///< 全屏档的假想显示器幅面（见 `Harness::set_mode` 的驱动口径）。
constexpr int kFullscreenHeight = 1080;

[[nodiscard]] auto nearly(double a, double b, double tolerance = 0.51) -> bool {
    return std::abs(a - b) < tolerance;
}

using Pixels = std::vector<std::uint8_t>;

/// @brief 装配层替身：三枚钮的来向与「本窗关窗请求」的留痕。
struct Probe {
    std::vector<ui::AppTitleBarAction> actions;
    std::size_t closes = 0U;

    [[nodiscard]] auto hooks() -> ui::AppTitleBar::Hooks {
        ui::AppTitleBar::Hooks out;
        out.request_close = [this]() -> void { ++closes; };
        out.note_window_action = [this](const ui::AppTitleBarAction action) -> void {
            actions.push_back(action);
        };
        return out;
    }
};

/// @brief 把像素按「离底的偏移更像哪一枚 ink」归类（覆盖式 AA 下不能逐位等值，见文件头）。
/// @return 0＝更像 `first`，1＝更像 `second`，-1＝未落墨。
[[nodiscard]] auto classify(const au::Color &pixel, const au::Color &bg, const au::Color &first,
                            const au::Color &second) -> int {
    const double vx = static_cast<double>(pixel.r) - bg.r;
    const double vy = static_cast<double>(pixel.g) - bg.g;
    const double vz = static_cast<double>(pixel.b) - bg.b;
    if (std::sqrt(vx * vx + vy * vy + vz * vz) < kMinInkDelta) {
        return -1;  // 未落墨
    }
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
    return away(first) < away(second) ? 0 : 1;
}

/// @brief 无头窗口 + 真布局 + 真绘制 + 真指针派发的驱动台（`itest_sidebar_rail` 的同形腿，根换 Column）。
class Harness {
  public:
    Harness(Probe &probe_in, int width = kWindowWidth, int height = kWindowHeight)
        : window_{make_window(width, height, &raw_)},
          bar_{std::make_shared<ui::AppTitleBar>(probe_in.hooks())} {
        host_ = std::make_shared<au::OverlayHost>();
        column_ = std::make_shared<au::Column>();
        column_->set_modifier(au::Modifier{}.fill_max_size());
        column_->set_cross_axis_alignment(au::CrossAxisAlignment::Stretch);
        // 带在首位、其余那一格在后（改判后的 `rebuild_root_children()` 那层就是这个次序）。
        auto filler = std::make_shared<au::Column>();
        filler->set_modifier(au::Modifier{}.expand(1.0F).fill_max_size());
        auto &nodes = column_->child_nodes_mut();
        nodes.push_back(au::Node{bar_});
        nodes.push_back(au::Node{filler});
        host_->child_nodes_mut().push_back(au::Node{column_});
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
        render();  // 单帧只够入树，布局落 bounds 要第二帧（与折叠轨同一条纪律）
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;

    auto render() -> void { (void)window_.present_root(root_); }

    /// @brief 换几何态之后排两帧：真实通道上进/出全屏**总是带着窗口尺寸一起变**（Win32 那侧是
    ///        `WM_SIZE` → `present_request`），而尺寸不变、无脏的那一帧会被脏追踪判成 idle 整帧跳过
    ///        ——`on_layout` 根本不会被重跑，那是另一条腿的事。故本驱动照生产的样子把表面一并改档，
    ///        几何态则同时写给表面（`set_window_mode` 那枚 seam）与 `Window` 快照（`prepare_context`
    ///        每帧注入环境的就是后者；真实应用里由 `WindowHost` 从 notify 链代写，本替身没有宿主）。
    auto set_mode(const au::WindowMode mode) -> void {
        const bool full = mode == au::WindowMode::FullScreen;
        raw_->set_window_mode(mode);
        window_.set_window_mode(mode);
        static_cast<void>(raw_->begin_frame(full ? kFullscreenWidth : kWindowWidth,
                                           full ? kFullscreenHeight : kWindowHeight));
        render();
        render();
    }

    /// @brief 换档之后叫醒一次重绘（同折叠轨那一条：本件不吃框架主题令牌，绘制时现读
    ///        `settings_chrome()`，故只标脏自己；证的仍是「色值是绘制时取的」）。
    auto repaint_after_scheme_switch() -> void {
        bar_->mark_needs_paint();
        render();
        render();
    }

    [[nodiscard]] auto bar() const -> ui::AppTitleBar & { return *bar_; }

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

    /// @brief 一段盒内的墨迹统计：两枚候选 ink 各归类到几枚像素。
    struct InkTally {
        std::size_t first = 0U;
        std::size_t second = 0U;
    };

    [[nodiscard]] auto ink_in_box(const Pixels &px, const au::Rect &box, const au::Color &bg,
                                  const au::Color &first, const au::Color &second) const -> InkTally {
        InkTally tally;
        if (box.size.width <= 0.0F || box.size.height <= 0.0F) {
            return tally;
        }
        const auto x_begin = std::max<std::size_t>(0U, static_cast<std::size_t>(box.origin.x));
        const auto x_end = std::min(static_cast<std::size_t>(box.origin.x + box.size.width),
                                    static_cast<std::size_t>(width()));
        const auto y_begin = std::max<std::size_t>(0U, static_cast<std::size_t>(box.origin.y));
        const auto y_end = std::min(static_cast<std::size_t>(box.origin.y + box.size.height),
                                    static_cast<std::size_t>(height()));
        for (std::size_t y = y_begin; y < y_end; ++y) {
            for (std::size_t x = x_begin; x < x_end; ++x) {
                const int kind = classify(sample(px, static_cast<double>(x), static_cast<double>(y)),
                                          bg, first, second);
                if (kind == 0) {
                    ++tally.first;
                } else if (kind == 1) {
                    ++tally.second;
                }
            }
        }
        return tally;
    }

    /// @brief 命中链在这一层交回的是哪一枚控件（带是否挡住了身后那一格）。
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

    /// @brief 按稿面右锚内缩点某一枚钮的中心。
    auto click_inset(double center_inset) -> void {
        static_cast<void>(pointer(au::MouseAction::Press, au::MouseButton::Left,
                                  static_cast<double>(width()) - center_inset, kBarHeightDp / 2.0));
    }

  private:
    /// @brief 建一扇无头窗，并把表面的**非持有指针**回写给出参（`Window::surface()` 只给 const 引用，
    ///        而无头的 `set_window_mode` / `begin_frame` 两枚 seam 都要写权限——全屏驱动得靠它们）。
    [[nodiscard]] static auto make_window(int width, int height, au::HeadlessSurface **out_raw)
        -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        *out_raw = surface.get();
        (void)surface->begin_frame(width, height);
        return au::Window{std::move(surface)};
    }

    au::HeadlessSurface *raw_ = nullptr;  ///< 必须先于 `window_` 声明：地址要活到析构，写入要早于默认初始化。
    au::Window window_;                   ///< 最先构造、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<ui::AppTitleBar> bar_;
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Column> column_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;
};

}  // namespace

// ------------------------------------------------------------
// 判据①：36 dp × 通栏、贴窗顶；三枚热区右锚、非等距、互不重叠
// ------------------------------------------------------------
AURORA_TEST_CASE(the_band_is_36_dp_tall_full_width_and_anchors_three_non_overlapping_controls) {
    Probe probe;
    Harness h{probe};
    ui::AppTitleBar &bar = h.bar();

    // ⑴ 高是自报的常量、宽跟随交叉轴（根 Column 的 Stretch 给整幅）；纵向不占 `expand` 分母。
    AURORA_TEST_CHECK_TRUE(nearly(bar.height_dp(), kBarHeightDp));
    AURORA_TEST_CHECK_TRUE(nearly(bar.size().height, kBarHeightDp));
    AURORA_TEST_CHECK_TRUE(nearly(bar.size().width, kWindowWidth));
    const au::Rect frame = bar.paint_bounds();
    AURORA_TEST_CHECK_TRUE(nearly(frame.origin.x, 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(frame.origin.y, 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(frame.size.height, kBarHeightDp));

    // ⑵ 三枚热区：统一 28 dp 宽、通栏 36 dp 高，中心按稿面右锚内缩 123／94／27（非等距＝F11 那一格）。
    const double right = static_cast<double>(h.width());
    const std::vector<std::pair<std::reference_wrapper<const au::Rect>, double>> controls{
        {std::cref(bar.minimize_box()), kMinimizeCenterInsetDp},
        {std::cref(bar.maximize_box()), kMaximizeCenterInsetDp},
        {std::cref(bar.close_box()), kCloseCenterInsetDp},
    };
    for (const auto &[boxed, center_inset] : controls) {
        const au::Rect &box = boxed.get();
        AURORA_TEST_CHECK_TRUE(nearly(box.size.width, kButtonHitWidthDp));
        AURORA_TEST_CHECK_TRUE(nearly(box.size.height, kBarHeightDp));
        AURORA_TEST_CHECK_TRUE(nearly(box.origin.y, 0.0));
        AURORA_TEST_CHECK_TRUE(nearly(static_cast<double>(box.origin.x) + kButtonHitWidthDp / 2.0,
                                      right - center_inset));
    }

    // ⑶ 自左至右是「最小化／最大化／关闭」，且互不重叠（重叠就会有一枚永远点不到）。
    AURORA_TEST_CHECK_TRUE(bar.minimize_box().origin.x < bar.maximize_box().origin.x);
    AURORA_TEST_CHECK_TRUE(bar.maximize_box().origin.x < bar.close_box().origin.x);
    AURORA_TEST_CHECK_TRUE(bar.minimize_box().right() <= bar.maximize_box().origin.x + 0.01F);
    AURORA_TEST_CHECK_TRUE(bar.maximize_box().right() <= bar.close_box().origin.x + 0.01F);
    // 关闭那一枚不越右缘（稿的 ✕ 包络右缘距窗右 23 dp）。
    AURORA_TEST_CHECK_TRUE(bar.close_box().right() <= static_cast<float>(right));

    // ⑷ 标题左沿恒在稿面的 x=38（徽标右缘之外），提示串左沿距右缘 70；上下文串还没喂料即空盒。
    AURORA_TEST_CHECK_TRUE(nearly(bar.title_box().origin.x, kTitleLeftDp));
    AURORA_TEST_CHECK_TRUE(bar.title_box().size.width > 0.0F);
    AURORA_TEST_CHECK_TRUE(nearly(bar.hint_box().origin.x, right - kHintRightInsetDp));
    AURORA_TEST_CHECK_TRUE(bar.hint_box().size.width > 0.0F);
    AURORA_TEST_CHECK_TRUE(nearly(bar.context_box().size.width, 0.0));
    AURORA_TEST_CHECK_TRUE(bar.context().empty());
}

// ------------------------------------------------------------
// 判据②：条身／底缘／竖线／徽标取活动档的四枚 token（两档各读一次）
// ------------------------------------------------------------
AURORA_TEST_CASE(the_band_paints_the_active_schemes_body_hairline_separator_and_badge) {
    Probe probe;
    Harness h{probe};

    const ui::SettingsChrome dark = ui::settings_chrome();
    const Pixels before = h.pixels();
    // 条身：字形行（y≈8..28）之外的空白段就是纯条身。
    AURORA_TEST_CHECK_TRUE(h.sample(before, 600.0, 32.0) == dark.card_bg);
    // 底缘那条 1 dp 线取 `card_line`（稿的 `#3A3C4C` 不在 13 枚 token 里，同 7.179④c 口径）。
    AURORA_TEST_CHECK_TRUE(h.sample(before, 600.0, 35.0) == dark.card_line);
    // 竖分隔线（稿 `#4D4F63`＝`card_line`，在册）。
    AURORA_TEST_CHECK_TRUE(h.sample(before, static_cast<double>(h.width()) - kSeparatorInsetDp, 18.0) ==
                           dark.card_line);
    // 徽标是填充圆：圆心外 5 dp 处（半径 7 之内、羽化边之外）就是 `accent`。
    AURORA_TEST_CHECK_TRUE(h.sample(before, kBadgeCenterXDp - 5.0, kBadgeCenterYDp) == dark.accent);
    // 带外第一行归身后那一格，不该被本件涂色（条身只到 y=36）。
    AURORA_TEST_CHECK_FALSE(h.sample(before, 600.0, 40.0) == dark.card_bg);

    ui::set_chrome_scheme(ui::ChromeScheme::Light);
    h.repaint_after_scheme_switch();
    const ui::SettingsChrome light = ui::settings_chrome();
    const Pixels after = h.pixels();
    AURORA_TEST_CHECK_TRUE(h.sample(after, 600.0, 32.0) == light.card_bg);
    AURORA_TEST_CHECK_TRUE(h.sample(after, 600.0, 35.0) == light.card_line);
    AURORA_TEST_CHECK_TRUE(h.sample(after, kBadgeCenterXDp - 5.0, kBadgeCenterYDp) == light.accent);
    // 两档读数不同 ⇒ 色值确实每次重绘现取活动档，而不是把建树时的十六进制刻进控件。
    AURORA_TEST_CHECK_FALSE(light.card_bg == dark.card_bg);
    AURORA_TEST_CHECK_FALSE(h.sample(after, 600.0, 32.0) == h.sample(before, 600.0, 32.0));
    ui::set_chrome_scheme(ui::ChromeScheme::Dark);  // 活动档是进程态，别把它留给下一个用例
}

// ------------------------------------------------------------
// 判据③：四段墨迹各归各的 ink 档（标题／上下文／钮／提示）
// ------------------------------------------------------------
AURORA_TEST_CASE(the_four_text_runs_split_across_the_three_ink_tiers) {
    Probe probe;
    Harness h{probe};
    ui::AppTitleBar &bar = h.bar();
    bar.set_context("/home/user/project");
    h.render();
    const ui::SettingsChrome chrome = ui::settings_chrome();
    const Pixels px = h.pixels();

    // ⑴ 标题＝`text`（最亮那一档），对照候选取 `text_dim`：分类只需两枚候选可分。
    const auto title = h.ink_in_box(px, bar.title_box(), chrome.card_bg, chrome.text, chrome.text_dim);
    AURORA_TEST_CHECK_TRUE(title.first > 0U);

    // ⑵ 三枚钮的字形＝`text_dim`，对照候选取 `text_faint`（稿面钮 `#8B93A7`／提示 `#62728A`）。
    for (const au::Rect &box : {bar.minimize_box(), bar.maximize_box(), bar.close_box()}) {
        const auto ink = h.ink_in_box(px, box, chrome.card_bg, chrome.text_dim, chrome.text_faint);
        AURORA_TEST_CHECK_MSG(ink.first > 0U, "every control draws its glyph");
    }

    // ⑶ 提示串＝`text_faint`，且与竖分隔线（`card_line`）同幅内互不遮挡：提示在竖线之右。
    AURORA_TEST_CHECK_TRUE(bar.hint_box().origin.x >
                           static_cast<float>(h.width()) - static_cast<float>(kSeparatorInsetDp));
    const auto hint = h.ink_in_box(px, bar.hint_box(), chrome.card_bg, chrome.text_faint, chrome.text);
    AURORA_TEST_CHECK_TRUE(hint.first > 0U);

    // ⑷ 上下文串＝`text_faint`，左沿**跟着标题右端走**（稿那个固定 x=105.2 只在「Borealis」这枚字宽
    //     下成立，换串就会压住标题——裁决 7.181 登记的那处按算式落地）。
    AURORA_TEST_CHECK_TRUE(nearly(bar.context_box().origin.x,
                                  static_cast<double>(bar.title_box().right()) + 14.0, 1.0));
    const auto context = h.ink_in_box(px, bar.context_box(), chrome.card_bg, chrome.text_faint,
                                      chrome.text);
    AURORA_TEST_CHECK_TRUE(context.first > 0U);

    bar.set_context("");
    h.render();
}

// ------------------------------------------------------------
// 判据③的接缝面：三枚钮各报各的；✕ 走确认闸门；空白区不吃动作
// ------------------------------------------------------------
AURORA_TEST_CASE(a_press_on_a_control_reports_that_action_and_close_uses_the_gate) {
    Probe probe;
    Harness h{probe};
    ui::AppTitleBar &bar = h.bar();

    // ⑴ 三枚各点一次：来向逐枚等于所点的那一枚，且一次点击只发一条（Press 触发，Release 不再报）。
    const std::vector<std::pair<double, ui::AppTitleBarAction>> wants{
        {kMinimizeCenterInsetDp, ui::AppTitleBarAction::Minimize},
        {kMaximizeCenterInsetDp, ui::AppTitleBarAction::ToggleMaximize},
        {kCloseCenterInsetDp, ui::AppTitleBarAction::Close},
    };
    for (const auto &[inset, action] : wants) {
        const std::size_t before = probe.actions.size();
        const std::size_t closes = probe.closes;
        h.click_inset(inset);
        AURORA_TEST_REQUIRE_MSG(probe.actions.size() == before + 1U, "one press must ask once");
        AURORA_TEST_CHECK_TRUE(probe.actions.back() == action);
        const bool is_close = action == ui::AppTitleBarAction::Close;
        AURORA_TEST_CHECK_EQ(probe.closes, closes + (is_close ? 1U : 0U));
    }
    // 三枚之外没有任何动作：✕ 只经接管腿报一次。
    AURORA_TEST_CHECK_EQ(probe.closes, 1U);

    // ⑵ 热区之内即被本件消费：命中链末位是这条带（含钮位与带内任何一点），身后那一格拿不到。
    AURORA_TEST_CHECK_TRUE(std::string_view{h.hit(600.0, 18.0)->type_name()} == "AppTitleBar");
    AURORA_TEST_CHECK_TRUE(std::string_view{
        h.hit(static_cast<double>(h.width()) - kCloseCenterInsetDp, 4.0)->type_name()} == "AppTitleBar");

    // ⑶ 空白区那一按交回基类（拖拽移动那条腿），本件不报任何动作。
    const std::size_t actions = probe.actions.size();
    static_cast<void>(h.pointer(au::MouseAction::Press, au::MouseButton::Left, 600.0, 18.0));
    AURORA_TEST_CHECK_EQ(probe.actions.size(), actions);
    AURORA_TEST_CHECK_EQ(probe.closes, 1U);

    // ⑷ 右键与移动不算：只认左键 Press。
    static_cast<void>(h.pointer(au::MouseAction::Press, au::MouseButton::Right,
                               static_cast<double>(h.width()) - kCloseCenterInsetDp, 18.0));
    static_cast<void>(h.pointer(au::MouseAction::Move, au::MouseButton::Left,
                                static_cast<double>(h.width()) - kMinimizeCenterInsetDp, 18.0));
    AURORA_TEST_CHECK_EQ(probe.actions.size(), actions);
    AURORA_TEST_CHECK_EQ(probe.closes, 1U);

    // ⑸ 常驻标识条不该吃键盘：不可获焦，否则点完标题栏敲的第一句落进 chrome 而不是任何 pane。
    AURORA_TEST_CHECK_FALSE(bar.focusable());
}

// ------------------------------------------------------------
// 判据④⑤：全屏自报 0 高、不落墨、不接点击；退出即回来
// ------------------------------------------------------------
AURORA_TEST_CASE(fullscreen_gives_the_band_up_and_returning_puts_it_back) {
    Probe probe;
    Harness h{probe};
    ui::AppTitleBar &bar = h.bar();
    const ui::SettingsChrome chrome = ui::settings_chrome();

    // ⑴ 进全屏：高度归零（不是摘除节点，与横幅／状态栏同一条常驻纪律），四枚热区全空盒。
    h.set_mode(au::WindowMode::FullScreen);
    AURORA_TEST_CHECK_TRUE(nearly(bar.height_dp(), 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(bar.minimize_box().size.width, 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(bar.maximize_box().size.width, 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(bar.close_box().size.width, 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(bar.title_box().size.width, 0.0));
    AURORA_TEST_CHECK_TRUE(nearly(bar.size().height, 0.0));

    // ⑵ 不落墨：徽标那一枚（填充实心，可直接等值比较）不在原位了。
    const Pixels off = h.pixels();
    AURORA_TEST_CHECK_FALSE(h.sample(off, kBadgeCenterXDp - 5.0, kBadgeCenterYDp) == chrome.accent);

    // ⑶ 不接点击：原✕位那一下不报任何动作（那一格现在是身后工作区的，本件不该截胡）。
    const std::size_t actions = probe.actions.size();
    const std::size_t closes = probe.closes;
    h.click_inset(kCloseCenterInsetDp);
    AURORA_TEST_CHECK_EQ(probe.actions.size(), actions);
    AURORA_TEST_CHECK_EQ(probe.closes, closes);

    // ⑷ 退出全屏：36 dp 回来、墨回来、热区回来（人已裁的「全屏就不挂这条，退出挂回」）。
    h.set_mode(au::WindowMode::Normal);
    AURORA_TEST_CHECK_TRUE(nearly(bar.height_dp(), kBarHeightDp));
    AURORA_TEST_CHECK_TRUE(nearly(bar.close_box().size.width, kButtonHitWidthDp));
    const Pixels on = h.pixels();
    AURORA_TEST_CHECK_TRUE(h.sample(on, kBadgeCenterXDp - 5.0, kBadgeCenterYDp) == chrome.accent);
    h.click_inset(kMinimizeCenterInsetDp);
    AURORA_TEST_CHECK_EQ(probe.actions.size(), actions + 1U);
    AURORA_TEST_CHECK_TRUE(probe.actions.back() == ui::AppTitleBarAction::Minimize);
}

// ------------------------------------------------------------
// 判据②的接缝面：上下文串的折叠形态与序列化往返
// ------------------------------------------------------------
AURORA_TEST_CASE(the_context_folds_directory_and_name_and_round_trips_through_props) {
    // ⑴ 折叠形态（装配层只交两截料，分隔符留在本件：硬规则 14 那一条的落点）：
    //     有目录＝「目录 · 名字」；无目录＝只给名字；两者皆空＝空串（那一格不画）。
    AURORA_TEST_CHECK_TRUE(ui::format_title_context("/srv/build", "prod (ssh)") ==
                           std::string{"/srv/build · prod (ssh)"});
    AURORA_TEST_CHECK_TRUE(ui::format_title_context("", "PowerShell") == std::string{"PowerShell"});
    AURORA_TEST_CHECK_TRUE(ui::format_title_context("/tmp", "") == std::string{"/tmp"});
    AURORA_TEST_CHECK_TRUE(ui::format_title_context("", "").empty());

    // ⑵ `set_context` 是幂等的观察面：同串再喂一次不改值、不改形态（逐帧现算的那条腿靠这一条不标脏）。
    Probe probe;
    Harness h{probe};
    ui::AppTitleBar &bar = h.bar();
    static_cast<void>(bar.set_context("alpha"));
    AURORA_TEST_CHECK_TRUE(bar.context() == "alpha");
    static_cast<void>(bar.set_context("alpha"));
    AURORA_TEST_CHECK_TRUE(bar.context() == "alpha");

    // ⑶ 序列化往返：基类那五项（height/title/subtitle/window_controls/snap_action_count）之外多带的
    //     `context` 必须回得来——窗口布局快照读的就是这一组字段。
    auto props = au::Json::object();
    bar.serialize_props(props);
    ui::AppTitleBar restored{ui::AppTitleBar::Hooks{}};
    restored.deserialize_props(props);
    AURORA_TEST_CHECK_TRUE(restored.context() == "alpha");
    AURORA_TEST_CHECK_TRUE(nearly(restored.height_dp(), 0.0));  // 未布局那一档就是 0（观测的是算出的值）

    // ⑷ 基类的 `window_controls` 在往返后仍是 false：三枚钮归本件，留着基类那三支就会两套热区同时吃点击。
    AURORA_TEST_CHECK_TRUE(restored.window_controls() == false);
    AURORA_TEST_CHECK_TRUE(restored.title() == "Borealis");
}

}  // namespace borealis::test_cases::itest_app_title_bar
