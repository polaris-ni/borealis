/// 测试类型: integration
/// 目标单元: src/ui/tab_bar.cpp（标签栏绘制侧）与 src/ui/tab_strip.cpp 的变更钩子
/// 测试说明: 无头帧缓冲上的**帧间像素差分**，验收 `SPEC.FEAT.WS.04` 三枚角标里退出角标的界面腿，
///           以及三枚角标共同依赖的那条通知链（裁决 **7.91**④）。这里断的从来不是「角标算得对不对」
///           （那是 `utest_tab_strip` 的二十余例），而是**它有没有落到屏幕上**：
///           - 「窗口尺寸没变时角标也上得了屏」——框架的布局缓存在「约束未变 ∧ 本控件未被标脏」时直接
///             复用上次尺寸并跳过 `on_layout`，而 `TabBarWidget` 的输入全在 `on_layout` 里经 hooks 现取，
///             所以这条判据钉的就是 `TabStrip` 的变更钩子本身（缺了它，本例第一句 `REQUIRE` 即红）；
///           - 退出角标的形状（6×6 空心方框：周长 20 个像素、内部 16 个像素一个都不动）与它落在栏位
///             右下角、选中格与否都画（裁决 7.91③ 那条「与右上角实心铃铛在位置与形状两档都不撞」——
///             两枚同格点亮时按 y 分成两簇且不相交）；
///           - 点亮与熄灭都是**真转移**才唤醒标签栏（每帧重推同一个值时钩子计数不涨，否则后台标签的
///             输出洪流就是每帧一次整栏重排）；
///           - 退出是逐格的事实而不是整栏的（两格各自点亮且互不牵连）；
///           - 真点一次标签把 BEL 取走并**从屏幕上撤掉**（`SPEC.FEAT.WS.04` 的取走语义；判据是全帧
///             逐位复现点击前那一帧，因此残影、错位、连带改动都红）。
///           本棒（`SPEC.FEAT.WS.04` 的最后一项）追加的**逐标签连接类型图标**也在这里验收，判据形态
///           照旧走差分而不是比色（稿 **7.129** 的 D4／D5／D7 与 §2 判据 ②～⑤）：未登记类型 ⇒ 左槽
///           单一底色（不画也不占位）、登记 ⇒ 栏位宽恰 +18 dp（锚＝右上那颗 BEL 的落位）、图标与三枚
///           角标四带互不相交、逐格类型改动不牵连邻格、四型四帧两两不同。
///           本棒（`SPEC.FEAT.PREF.01`）再追加一条**色板证人**：同一棵标签栏在暗、亮两档各出一帧，
///           栏底逐档等于 `chrome_palette()` 的 `card_bg`，且切回暗档逐位复现首帧（换肤可逆）。
///           像素一律比 RGBA 四通道含 alpha（与 `itest_render_viewport` 同一条口径：Headless 帧底色是
///           全透明黑，只比 RGB 会把「画了个纯黑」与「什么都没画」混为一谈）。
///           角标的**色值**不在这里断：那三档颜色是本棒拍的实现细节（裁决 7.91③），真机目视才判得出
///           「认不认得出那是铃铛」，故这里只按差分定位形状与位置，不把实现里的三个十六进制数复述一遍。

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "borealis/ui/tab_strip.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①：类声明含框架类型，故不进 `include/borealis/`），按相对路径取用，
// 而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_panel.h"  // PREF.01：活动档与唯一色值表（标签栏底取它的 card_bg）
#include "../../src/ui/tab_bar.h"

namespace borealis::test_cases::itest_tab_bar {

namespace {

#ifdef AURORA_BACKEND_HEADLESS

using borealis::ui::TabBarHooks;
using borealis::ui::TabBarWidget;
using borealis::ui::TabId;
using borealis::ui::TabNamePriority;
using borealis::ui::TabStrip;
using borealis::ui::TabVisual;
using borealis::conn::ConnectionType;

constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 64;
/// @brief 栏高与两枚角标的档位（判据文 `codespec/UI_WORKSPACE_INTERACT.draft.md` 面板 5 的规格）。
constexpr std::size_t kBadgeEdgePx = 6U;
constexpr std::size_t kHollowRingPx = kBadgeEdgePx * kBadgeEdgePx - (kBadgeEdgePx - 2U) * (kBadgeEdgePx - 2U);
constexpr std::size_t kFilledDotPx = kBadgeEdgePx * kBadgeEdgePx;
/// @brief 活动角标是 4×4 实心点（裁决 7.82② 的档位），与铃铛那枚 6×6 分档。
constexpr std::size_t kActivityDotPx = 4U * 4U;
/// @brief 栏位钳位区间（`src/ui/tab_bar.cpp` 的 `tab_min_width_`／`tab_max_width_`）：判据 ③ 只在
///        两界之间才谈得上「恰为 18 dp」，故那里各有一枚边界断言（Headless 下 dp＝px）。
constexpr std::size_t kTabMinWidthPx = 96U;
constexpr std::size_t kTabMaxWidthPx = 240U;
/// @brief 铃铛（y 6..11）与退出环（y 30..35）的分簇线：栏高 42 的中点。
constexpr std::size_t kUpperHalfRow = 20U;
/// @brief 类型图标槽的档位（`SPEC.FEAT.WS.04` 的 D4／F9）：字形盒占 x 12..23，实绘行带 21..32
///        （13 pt 探针实测，dp＝px 于 Headless），名称原点由 12 挪到 30 ⇒ 占用总宽 18 dp。
constexpr std::size_t kGlyphSlotX0 = 12U;
constexpr std::size_t kGlyphSlotX1 = 23U;
constexpr std::size_t kGlyphTopRow = 21U;
constexpr std::size_t kGlyphBottomRow = 32U;
constexpr std::size_t kTypeSlotTotal = 18U;

using Frame = std::vector<std::uint8_t>;
using Points = std::set<std::pair<std::size_t, std::size_t>>;

/// @brief 一组像素的外接矩形（含端点）。
struct Box {
    std::size_t x0 = 0;
    std::size_t y0 = 0;
    std::size_t x1 = 0;
    std::size_t y1 = 0;

    [[nodiscard]] auto width() const noexcept -> std::size_t { return x1 - x0 + 1U; }
    [[nodiscard]] auto height() const noexcept -> std::size_t { return y1 - y0 + 1U; }
};

/// @brief 驱动台：`TabStrip`（真值源）+ `TabBarWidget`（绘制）+ 无头窗口 + 焦点管理器 + 实体派发器。
///
/// 接线形态逐字照 `src/main.cpp`：`.tabs` 把 `Tab` 折成 `TabVisual`（含 `exited`），`.select` 在选中
/// 之后把**两枚事件角标**都取走，`TabStrip::set_changed_hook` 里标脏。本件因此测的是这条链，
/// 而不是装配层那几行本身（装配层没有可执行的测试宿主）。
///
/// 成员声明次序即析构次序的倒序：控件与根节点须在 `strip_` 之前消散——钩子持着 `strip_` 的 this。
class Harness {
  public:
    /// @param names 各格的连接档案显示名（缺省一格，够装绝大多数判据；两格及以上的角标逐格独立另配）。
    explicit Harness(std::vector<std::u32string> names = std::vector<std::u32string>{U"alpha"}) {
        // 逐格类型缺省全空＝「装配层未登记」那一档（判据 ②）：既有八案的像素读数因此与本棒之前
        // 逐位相同，图标这条腿不可能悄悄改掉它们的差分值。
        kinds_.assign(names.size(), std::nullopt);
        for (std::size_t index = 0; index < names.size(); ++index) {
            (void)strip_.add(static_cast<TabId>(index), names[index]);
        }
        // 钩子装在 `strip_` 一侧：真值源发生变化时把绘制侧标脏（裁决 7.91④）。
        strip_.set_changed_hook([this, bar = bar_]() -> void {
            ++change_hook_calls_;
            bar->mark_needs_layout();
            bar->mark_needs_paint();
        });
        focus_.set_root(&root_.widget());  // 真点一次标签要走焦点序（缺初始焦点则派发交不出控件）
        render();                          // 帧 0：布局与绘制都发生在这里，此后约束不再变——正是本套件要钉的那个条件。
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 排一帧：present 会跑布局与绘制，而布局缓存只在被标脏时才重跑 `on_layout`。
    auto render() -> void { (void)window_.present_root(root_); }

    /// @brief 推退出位并排帧（装配层的每帧腿就是这一句）。
    auto set_exited(TabId id, bool exited) -> void {
        static_cast<void>(strip_.set_exited(id, exited));
        render();
    }

    auto mark_bell(TabId id) -> void {
        static_cast<void>(strip_.mark_bell_triggered(id));
        render();
    }

    auto mark_activity(TabId id) -> void {
        static_cast<void>(strip_.mark_activity(id));
        render();
    }

    auto clear_activity(TabId id) -> void {
        static_cast<void>(strip_.take_activity(id));
        render();
    }

    auto clear_bell(TabId id) -> void {
        static_cast<void>(strip_.take_bell_triggered(id));
        render();
    }

    /// @brief 推第 `index` 格的连接类型并排帧（`SPEC.FEAT.WS.04`；空＝未登记那一档）。
    ///
    /// 类型不是 `TabStrip` 的真值源而是装配层从 `tab_kind` 表折来的事实（裁决 7.129①），故本驱动台
    /// 另存一份按栏位下标索引的表，`.tabs` 腿照 `src/main.cpp` 那一行取它。
    ///
    /// 生产路径上类型在建标签那一刻就有 ⇒ 首帧即含图标，没有「中途改类型」这件事；本件为了差分需要
    /// 两态对照，故须像 `TabStrip` 的变更钩子那样把绘制侧标脏——不标脏就是布局缓存复用上一帧尺寸并
    /// 跳过 `on_layout`，`.tabs` 根本不重读（这条正是本套件第一条判据钉的那个缓存条件）。
    auto set_kind(std::size_t index, std::optional<conn::ConnectionType> kind) -> void {
        kinds_[index] = kind;
        bar_->mark_needs_layout();
        bar_->mark_needs_paint();
        render();
    }

    /// @brief 在栏位内第 `x_dp` 逻辑 dp 处实发一次 Press+Release（走实体派发器而非静态入口：
    ///        连击判定与指针捕获须隔离在本驱动台）。
    auto click(std::size_t x_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = static_cast<float>(x_dp), .y = 20.0F};
        event.button = au::MouseButton::Left;
        for (const auto action : {au::MouseAction::Press, au::MouseAction::Release}) {
            event.action = action;
            static_cast<void>(pointer_.dispatch_mouse(root_.widget(), event, &focus_));
        }
        render();
    }

    /// @brief 当前帧缓冲的 RGBA8 副本。
    [[nodiscard]] auto pixels() const -> Frame {
        Frame out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    [[nodiscard]] auto strip() noexcept -> TabStrip & { return strip_; }
    [[nodiscard]] auto change_hook_calls() const noexcept -> std::size_t { return change_hook_calls_; }

    /// @brief 两帧之间**任何通道**不同的像素坐标（差分是本套件唯一的定位手段：颜色是实现细节，
    ///        而「哪一格亮了」由差分给出，不必把实现里的三个十六进制数复述成预期）。
    [[nodiscard]] static auto changed(const Frame &before, const Frame &after) -> Points {
        Points out;
        const std::size_t width = buffer_width();
        for (std::size_t index = 0; index + 3U < before.size(); index += 4U) {
            if (std::memcmp(before.data() + index, after.data() + index, 4U) != 0) {
                const std::size_t pixel = index / 4U;
                out.insert({pixel % width, pixel / width});
            }
        }
        return out;
    }

    [[nodiscard]] static auto box_of(const Points &pts) -> Box {
        Box out;
        bool first = true;
        for (const auto &[x, y] : pts) {
            if (first) {
                out = Box{x, y, x, y};
                first = false;
                continue;
            }
            out.x0 = std::min(out.x0, x);
            out.y0 = std::min(out.y0, y);
            out.x1 = std::max(out.x1, x);
            out.y1 = std::max(out.y1, y);
        }
        return out;
    }

    /// @brief 按 y 把差分点集折成「上半栏（铃铛那一带）」与「下半栏（退出环那一带）」两簇。
    [[nodiscard]] static auto split_upper_lower(const Points &pts) -> std::pair<Points, Points> {
        Points upper;
        Points lower;
        for (const auto &point : pts) {
            (point.second < kUpperHalfRow ? upper : lower).insert(point);
        }
        return {upper, lower};
    }

    /// @brief 外接矩形之内、但差分集里没有的像素个数（空心判据：环的内部应当一格都没动）。
    [[nodiscard]] static auto holes_in(const Points &pts) -> std::size_t {
        const Box box = box_of(pts);
        std::size_t holes = 0U;
        for (std::size_t y = box.y0 + 1U; y < box.y1; ++y) {
            for (std::size_t x = box.x0 + 1U; x < box.x1; ++x) {
                if (pts.find({x, y}) == pts.end()) {
                    ++holes;
                }
            }
        }
        return holes;
    }

    /// @brief 反空转前提：差分外的那批点必须在两帧之间逐位相同（由调用方给出候选点集）。
    [[nodiscard]] static auto same_at(const Frame &before, const Frame &after, const Points &pts) -> bool {
        const std::size_t width = buffer_width();
        for (const auto &[x, y] : pts) {
            const std::size_t index = (y * width + x) * 4U;
            if (std::memcmp(before.data() + index, after.data() + index, 4U) != 0) {
                return false;
            }
        }
        return true;
    }

    /// @brief 两簇的公共像素个数（碰撞判据：非零即撞）。判据 ④ 的四簇各与同一基线帧差分而得，
    ///        故「哪两枚在同一条墨迹上」在这里就是一个数，不必先把簇折成外接矩形再比矩形（那会漏掉
    ///        「同带但不相交」与「矩形相交但墨迹不相交」两种情形）。
    [[nodiscard]] static auto overlap(const Points &a, const Points &b) -> std::size_t {
        std::size_t out = 0U;
        for (const auto &point : a) {
            if (b.find(point) != b.end()) {
                ++out;
            }
        }
        return out;
    }

    /// @brief 矩形之内是否只有**一种**像素值（「这里什么都没画」的颜色无关说法：矩形落在单一底色上）。
    [[nodiscard]] static auto is_uniform(const Frame &frame, std::size_t x0, std::size_t y0, std::size_t x1,
                                         std::size_t y1) -> bool {
        const std::size_t width = buffer_width();
        const auto pixel = [&](std::size_t x, std::size_t y) -> const std::uint8_t * {
            return frame.data() + (y * width + x) * 4U;
        };
        for (std::size_t y = y0; y <= y1; ++y) {
            for (std::size_t x = x0; x <= x1; ++x) {
                if (std::memcmp(pixel(x, y), pixel(x0, y0), 4U) != 0) {
                    return false;
                }
            }
        }
        return true;
    }

  private:
    [[nodiscard]] static auto buffer_width() -> std::size_t { return static_cast<std::size_t>(kWindowWidth); }
    [[nodiscard]] static auto buffer_height() -> std::size_t { return static_cast<std::size_t>(kWindowHeight); }

    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        static_cast<void>(surface->begin_frame(kWindowWidth, kWindowHeight));
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    TabStrip strip_;
    /// @brief 逐格连接类型（与 `src/main.cpp` 的 `tab_kind` 表同位，按栏位下标索引）。
    std::vector<std::optional<conn::ConnectionType>> kinds_;
    std::shared_ptr<TabBarWidget> bar_ = std::make_shared<TabBarWidget>(TabBarHooks{
        .tabs = [this]() -> std::vector<TabVisual> {
            std::vector<TabVisual> out;
            const std::optional<TabId> selected = strip_.selected();
            std::size_t index = 0U;
            for (const auto &tab : strip_.tabs()) {
                out.push_back(TabVisual{
                    .id = tab.id,
                    .display_name = borealis::ui::resolve_tab_name(tab.names, TabNamePriority::ManualWins),
                    .is_selected = selected.has_value() && selected.value() == tab.id,
                    .has_close_button = strip_.count() > 1U,  // 末位不给关（裁决 7.43④ 同口径）
                    .bell_triggered = tab.bell_triggered,
                    .has_activity = tab.has_activity,
                    .exited = tab.exited,
                    // 类型由装配层那张表折来（`SPEC.FEAT.WS.04` 判据 ⑦ 说这一行在本仓无自动化证人，
                    // 本件是它的同形替身：被测的是绘制腿，不是这一行的存在性）。
                    .kind = index < kinds_.size() ? kinds_[index] : std::nullopt,
                });
                ++index;
            }
            return out;
        },
        .select = [this](std::uint64_t id) -> void {
            const auto tab_id = static_cast<TabId>(id);
            static_cast<void>(strip_.select(tab_id));
            // 切到该格即把两枚**事件**角标都取走（裁决 7.91⑤⑵）：只清活动会让铃铛在用户已经站在
            // 那一格之后还亮着，而再也没有取走的入口。
            static_cast<void>(strip_.take_activity(tab_id));
            static_cast<void>(strip_.take_bell_triggered(tab_id));
        },
        .close = [this](std::uint64_t id) -> void {
            static_cast<void>(strip_.close(static_cast<TabId>(id)));
        },
    });
    au::Node root_{std::static_pointer_cast<au::Widget>(bar_)};
    au::FocusManager focus_;
    au::EventDispatcher pointer_;  ///< 本驱动台私有的连击判定与指针捕获状态。
    std::size_t change_hook_calls_ = 0U;
};

#else  // 无头后端未编译：HeadlessSurface 不在公共面上，本套件一律跳过而不是空转。

#endif

}  // namespace

AURORA_TEST_CASE(the_bar_background_follows_the_active_chrome_scheme) {
#ifdef AURORA_BACKEND_HEADLESS
    // PREF.01 稿 §4 的像素证人：同一棵标签栏在暗档与亮档各出一帧，栏底逐档等于唯一色值表里
    // 「标签栏底」那一格（`card_bg`；稿 §4 原文写的 `window_bg` 与 §1 表和 D6 冲突，以表和 D6 为准）。
    // 再切回暗档出一帧，须与首帧逐位相同——换肤在绘制侧读的是活动档这个状态量，而不是把一次改色
    // 记进控件。
    struct Restore {
        borealis::ui::ChromeScheme saved = borealis::ui::chrome_scheme();
        ~Restore() { borealis::ui::set_chrome_scheme(saved); }
    } restore;

    borealis::ui::set_chrome_scheme(borealis::ui::ChromeScheme::Dark);
    Harness dark;
    const Frame dark_frame = dark.pixels();
    borealis::ui::set_chrome_scheme(borealis::ui::ChromeScheme::Light);
    Harness light;
    const Frame light_frame = light.pixels();

    // 单格标签最宽 240 dp，x=600 落在栏位之外的空白栏底；y=8 避开选中格的 2 dp 下划线（y 40..41）。
    const auto bar_pixel = [](const Frame &frame) -> std::array<std::uint8_t, 4U> {
        const std::size_t index = (8U * static_cast<std::size_t>(kWindowWidth) + 600U) * 4U;
        return {frame[index], frame[index + 1U], frame[index + 2U], frame[index + 3U]};
    };
    const auto expected = [](const au::Color &color) -> std::array<std::uint8_t, 4U> {
        return {color.r, color.g, color.b, color.a};
    };
    const au::Color dark_bg = borealis::ui::chrome_palette(borealis::ui::ChromeScheme::Dark).card_bg;
    const au::Color light_bg = borealis::ui::chrome_palette(borealis::ui::ChromeScheme::Light).card_bg;
    // 反空转前提：两档栏底真不同，否则下面两句在「读到的其实是同一块」时也会绿。
    AURORA_TEST_CHECK_MSG(!(dark_bg.r == light_bg.r && dark_bg.g == light_bg.g && dark_bg.b == light_bg.b),
                          "the two palettes share one bar background");
    AURORA_TEST_CHECK_MSG(bar_pixel(dark_frame) == expected(dark_bg),
                          "the dark bar bottom is not the dark card_bg");
    AURORA_TEST_CHECK_MSG(bar_pixel(light_frame) == expected(light_bg),
                          "the light bar bottom is not the light card_bg");
    AURORA_TEST_CHECK_MSG(dark_frame != light_frame, "switching the scheme changed nothing on screen");

    borealis::ui::set_chrome_scheme(borealis::ui::ChromeScheme::Dark);
    Harness back;
    AURORA_TEST_CHECK_MSG(back.pixels() == dark_frame,
                          "switching back to dark did not reproduce the first frame");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_exit_badge_reaches_the_screen_without_the_window_being_resized) {
#ifdef AURORA_BACKEND_HEADLESS
    // 这是本套件承重的一例，也是裁决 7.91⑤⑴ 那条潜在缺陷的证人：窗口尺寸从头到尾没变，驱动台
    // 也没有自己标过任何脏，于是「角标上屏」这件事只可能由 `TabStrip` 的变更钩子促成。
    Harness h;
    const auto before = h.pixels();
    static_cast<void>(h.strip().set_exited(0U, true));
    h.render();
    const auto after = h.pixels();

    const Points badge = Harness::changed(before, after);
    AURORA_TEST_REQUIRE_MSG(!badge.empty(),
                            "the exit badge never reached the buffer (no change hook => the layout cache skipped on_layout)");
    AURORA_TEST_CHECK_EQ(h.change_hook_calls(), std::size_t{1});
    // 整帧里只有角标那一段动了：差分点集恰是周长，而不是「连带整栏重排出一片」。
    AURORA_TEST_CHECK_EQ(badge.size(), kHollowRingPx);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_exit_badge_is_a_hollow_ring_at_the_bottom_right) {
#ifdef AURORA_BACKEND_HEADLESS
    Harness h;
    const auto before = h.pixels();
    h.set_exited(0U, true);
    const auto after = h.pixels();
    const Points ring = Harness::changed(before, after);
    AURORA_TEST_REQUIRE(!ring.empty());

    const Box box = Harness::box_of(ring);
    AURORA_TEST_CHECK_EQ(box.width(), kBadgeEdgePx);
    AURORA_TEST_CHECK_EQ(box.height(), kBadgeEdgePx);
    // 空心：6×6 外接矩形内部 16 格一格都没动（改成一枚实心块就是这里红）。
    AURORA_TEST_CHECK_EQ(Harness::holes_in(ring), (kBadgeEdgePx - 2U) * (kBadgeEdgePx - 2U));
    AURORA_TEST_CHECK_EQ(ring.size(), kHollowRingPx);
    // 落在下半栏（右下角），且不给 2 dp 选中下划线让位——环的下沿至少离栏底 6 dp。
    AURORA_TEST_CHECK_GE(box.y0, kUpperHalfRow);
    AURORA_TEST_CHECK_LE(box.y1 + 6U, static_cast<std::size_t>(42U));
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_exit_ring_is_drawn_on_the_selected_tab_too) {
#ifdef AURORA_BACKEND_HEADLESS
    // 与活动角标的分工（裁决 7.82①：活动只在未选中格显形）在这里是可判的：两格里退出环的**形状与
    // 落位**必须一致，只是横坐标随栏位走。选中格不画环，用户就在一个已退出的格子里打字时看不见它。
    Harness selected{std::vector<std::u32string>{U"alpha"}};
    Harness unselected{std::vector<std::u32string>{U"alpha", U"beta"}};  // 新建即选中末位，故 0 号格未选中

    const auto selected_before = selected.pixels();
    const auto unselected_before = unselected.pixels();
    selected.set_exited(0U, true);
    unselected.set_exited(0U, true);

    const Points on_selected = Harness::changed(selected_before, selected.pixels());
    const Points on_unselected = Harness::changed(unselected_before, unselected.pixels());
    AURORA_TEST_REQUIRE(!on_selected.empty());
    AURORA_TEST_REQUIRE(!on_unselected.empty());

    const Box a = Harness::box_of(on_selected);
    const Box b = Harness::box_of(on_unselected);
    AURORA_TEST_CHECK_EQ(a.width(), b.width());
    AURORA_TEST_CHECK_EQ(a.height(), b.height());
    AURORA_TEST_CHECK_EQ(a.y0, b.y0);  // 纵向落位与选中态无关
    AURORA_TEST_CHECK_EQ(on_selected.size(), kHollowRingPx);
    AURORA_TEST_CHECK_EQ(on_unselected.size(), kHollowRingPx);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_bell_dot_and_the_exit_ring_do_not_collide) {
#ifdef AURORA_BACKEND_HEADLESS
    // 裁决 7.91③ 那句「位置与形状两档都不撞」的兑现形态：两枚同格点亮时，差分按 y 折成两簇，
    // 上簇是实心 6×6 铃铛（36 像素、内部无空洞），下簇是空心 6×6 环（20 像素、内部 16 空洞），
    // 且两簇纵向不相交。两枚撞在一起时这里必红。
    Harness h;
    const auto before = h.pixels();
    static_cast<void>(h.strip().mark_bell_triggered(0U));
    static_cast<void>(h.strip().set_exited(0U, true));
    h.render();
    const Points both = Harness::changed(before, h.pixels());
    AURORA_TEST_REQUIRE(!both.empty());

    const auto [upper, lower] = Harness::split_upper_lower(both);
    AURORA_TEST_REQUIRE(!upper.empty());
    AURORA_TEST_REQUIRE(!lower.empty());
    AURORA_TEST_CHECK_EQ(upper.size(), kFilledDotPx);
    AURORA_TEST_CHECK_EQ(lower.size(), kHollowRingPx);
    AURORA_TEST_CHECK_EQ(Harness::holes_in(upper), std::size_t{0U});   // 铃铛是实心档
    AURORA_TEST_CHECK_GT(Harness::box_of(lower).y0, Harness::box_of(upper).y1);  // 纵向不相交
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_real_click_on_a_tab_takes_the_bell_flag_off_the_screen) {
#ifdef AURORA_BACKEND_HEADLESS
    // 裁决 7.91⑤⑵ 那条潜在缺陷的证人：`TabStrip::take_bell_triggered(TabId)` 原先全仓无调用点，
    // 于是⑴ 一修（角标真的上屏了）铃铛就永远灭不掉。判据取「点击那一格之后，整帧逐位复现点击前
    // 那一帧」——残影、错位、连带改动都在这句面前现形。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    h.click(10U);  // 先选中 0 号格：让后面的对照帧与铃铛所在格同处选中态，选中底色不参与本判据
    const auto plain = h.pixels();
    AURORA_TEST_REQUIRE_TRUE(h.strip().selected().has_value());
    AURORA_TEST_CHECK_EQ(h.strip().selected().value(), TabId{0U});

    h.mark_bell(0U);
    const auto ringing = h.pixels();
    const Points bell = Harness::changed(plain, ringing);
    AURORA_TEST_REQUIRE(!bell.empty());
    AURORA_TEST_CHECK_EQ(bell.size(), kFilledDotPx);

    h.click(10U);  // 再点同一格是常见确认手势：选中位没动，但两枚事件角标须被取走
    const auto cleared = h.pixels();
    AURORA_TEST_CHECK_FALSE(h.strip().tabs()[0].bell_triggered);
    AURORA_TEST_CHECK_MSG(cleared == plain, "the bell dot survived the click that took it");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_badge_goes_out_when_the_next_frame_pushes_false) {
#ifdef AURORA_BACKEND_HEADLESS
    // 「投影而不是判据」（裁决 7.91②）在屏幕上的兑现：不留清除入口，会话重启后每帧推来的 false
    // 自己把角标撤掉，且撤掉之后整帧回到点亮前那一帧（逐位相同）。
    Harness h;
    const auto plain = h.pixels();
    h.set_exited(0U, true);
    const auto lit = h.pixels();
    AURORA_TEST_REQUIRE(!Harness::changed(plain, lit).empty());

    h.set_exited(0U, false);
    const auto back = h.pixels();
    AURORA_TEST_CHECK_MSG(back == plain, "the badge left a residue after the flag went false");
    AURORA_TEST_CHECK_EQ(h.change_hook_calls(), std::size_t{2});  // 一亮一灭各一次
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(repushing_the_same_exit_value_never_wakes_the_bar) {
#ifdef AURORA_BACKEND_HEADLESS
    // 装配层的每帧腿对**每个标签**都推一次退出位，活着时每帧都是同一个 false。这一例钉的是那条
    // 成本口径：钩子只在真转移时响一次，于是后台标签的输出洪流不会变成每帧一次整栏重排。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    h.set_exited(0U, false);  // 首帧之后第一次推：值没变，一次都不许响
    AURORA_TEST_CHECK_EQ(h.change_hook_calls(), std::size_t{0});

    const std::size_t lit_calls = [&]() -> std::size_t {
        h.set_exited(0U, true);
        return h.change_hook_calls();
    }();
    AURORA_TEST_CHECK_EQ(lit_calls, std::size_t{1});

    const auto lit = h.pixels();
    for (int frame = 0; frame < 30; ++frame) {
        h.set_exited(0U, true);  // 每帧重推同一个真值
    }
    AURORA_TEST_CHECK_EQ(h.change_hook_calls(), std::size_t{1});
    AURORA_TEST_CHECK_MSG(h.pixels() == lit, "thirty redundant pushes changed the frame");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(two_exited_tabs_light_two_badges_that_do_not_teach_each_other) {
#ifdef AURORA_BACKEND_HEADLESS
    // 逐格独立在屏幕层的兑现：第二格点亮时第一格的环**一个像素都不许动**，且两簇不相交、横向错开
    // 一个栏位宽（而不是叠在同一处）。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    const auto plain = h.pixels();
    h.set_exited(0U, true);
    const Points first_ring = Harness::changed(plain, h.pixels());
    AURORA_TEST_REQUIRE(!first_ring.empty());

    const auto one_lit = h.pixels();
    h.set_exited(1U, true);
    const auto two_lit = h.pixels();
    const Points second_ring = Harness::changed(one_lit, two_lit);
    AURORA_TEST_REQUIRE(!second_ring.empty());
    AURORA_TEST_CHECK_EQ(second_ring.size(), kHollowRingPx);
    // 前一颗在这一步之后逐位未动：第二格的点亮不许牵连第一格。
    AURORA_TEST_CHECK_TRUE(Harness::same_at(one_lit, two_lit, first_ring));

    const Box a = Harness::box_of(first_ring);
    const Box b = Harness::box_of(second_ring);
    AURORA_TEST_CHECK_EQ(a.y0, b.y0);
    AURORA_TEST_CHECK_GT(b.x0, a.x1);  // 后一颗在前一颗之右，两簇不相交
    AURORA_TEST_CHECK_EQ(Harness::changed(plain, two_lit).size(), kHollowRingPx * 2U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(an_unregistered_kind_draws_nothing_and_the_registered_one_lands_in_the_slot) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ②（稿 §2）：`kind` 空 ⇒ 左槽**不画也不占位**（裁决 7.38⑥「缺席的入口整枚不画」同口径，
    // D5）。空名格用来给「这一带根本没有墨」一个颜色无关的证人——名称本身也从 x+12 起画，有名格判不出
    // 「没有占位符」。反空转在后半：同一台驱动台把类型登记上，同一矩形立刻不再单一。
    Harness h{std::vector<std::u32string>{U""}};
    AURORA_TEST_CHECK_TRUE(Harness::is_uniform(h.pixels(), kGlyphSlotX0, kGlyphTopRow, kGlyphSlotX1,
                                               kGlyphBottomRow));

    h.set_kind(0U, ConnectionType::Local);
    AURORA_TEST_CHECK_FALSE(Harness::is_uniform(h.pixels(), kGlyphSlotX0, kGlyphTopRow, kGlyphSlotX1,
                                                kGlyphBottomRow));
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_registered_kind_widens_the_slot_by_exactly_eighteen_dp) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ③：栏位宽＝今日公式 + 18 dp。几何证人取右上角那颗 BEL 的**落位**——它在栏位右端
    // （`x + w - 6`），是 `tab_width()` 的读数锚，而锚本身的大小不变（6×6）。两态各与自己的「铃铛灭」
    // 基线差分，故每簇就是一颗干净的 6×6，横向差恰为槽宽。
    // 名称取中等长度：短名贴 96 下限、长名贴 240 上限，两端都会把槽宽吃掉一截（D4 记的就是这条代价），
    // 只有落在两界之间时「恰为 18 dp」才谈得上。下面两枚边界断言就是「本例没被钳位吃掉」的证人。
    Harness h{std::vector<std::u32string>{U"checkpoint-pipeline"}};
    const auto plain = h.pixels();
    h.mark_bell(TabId{0U});
    const Box without_slot = Harness::box_of(Harness::changed(plain, h.pixels()));
    AURORA_TEST_CHECK_EQ(without_slot.width(), kBadgeEdgePx);
    AURORA_TEST_CHECK_EQ(without_slot.height(), kBadgeEdgePx);
    AURORA_TEST_CHECK_GT(without_slot.x1, kTabMinWidthPx);  // 未落下限：这一格的宽是实测值而不是钳位值

    h.clear_bell(TabId{0U});
    const auto widened = h.pixels();
    h.set_kind(0U, ConnectionType::Local);
    AURORA_TEST_CHECK_FALSE(Harness::changed(widened, h.pixels()).empty());  // 先证「这一动真的发生」
    const auto with_slot = h.pixels();
    h.mark_bell(TabId{0U});
    const Box with_slot_bell = Harness::box_of(Harness::changed(with_slot, h.pixels()));
    AURORA_TEST_CHECK_EQ(with_slot_bell.width(), kBadgeEdgePx);
    AURORA_TEST_CHECK_EQ(with_slot_bell.height(), kBadgeEdgePx);
    AURORA_TEST_CHECK_LT(with_slot_bell.x1, kTabMaxWidthPx);  // 也未顶上限
    AURORA_TEST_CHECK_EQ(with_slot_bell.x0 - without_slot.x0, kTypeSlotTotal);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_icon_and_the_three_badges_land_in_four_disjoint_bands) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ④＝本棒对**既有判据**负责的证人（裁决 7.82／7.91 的地盘）：图标与三枚角标同格并存时，四簇
    // 像素两两不相交。四簇各与**同一基线帧**差分得到（基线里四枚都未点亮），故每簇就是那一枚自己的墨迹；
    // 两簇有公共像素即几何相撞，`overlap()` 把它折成一个数。
    //
    // 三条实测出来的约束：① 角标逐枚点亮再取走，中间每次都回到基线帧，否则簇会互相污染；② 两格都登记
    // 类型 ⇒ 栏位几何在本例内一步都不变（`kind` 从无到有会挪动邻格，那属判据 ⑤ 的另一半）；③ 活动角标
    // 只在**未选中**格显形（裁决 7.82①）而新建即选中末位，故四枚都点在第一格上。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    h.set_kind(0U, ConnectionType::Serial);
    h.set_kind(1U, ConnectionType::Serial);
    const auto base = h.pixels();

    h.mark_bell(TabId{0U});
    const Points bell = Harness::changed(base, h.pixels());
    h.clear_bell(TabId{0U});

    h.mark_activity(TabId{0U});
    const Points activity = Harness::changed(base, h.pixels());
    h.clear_activity(TabId{0U});

    h.set_exited(TabId{0U}, true);
    const Points exit = Harness::changed(base, h.pixels());
    h.set_exited(TabId{0U}, false);

    // 字形簇：同槽宽的两枚字形互差（Local 的 `>` 对 Serial 的 `S`），几何不动 ⇒ 这一簇就是字形本身。
    Harness glyph{std::vector<std::u32string>{U"alpha", U"beta"}};
    glyph.set_kind(0U, ConnectionType::Local);
    const auto local_frame = glyph.pixels();
    glyph.set_kind(0U, ConnectionType::Serial);
    const Points icon = Harness::changed(local_frame, glyph.pixels());

    AURORA_TEST_REQUIRE(!bell.empty());
    AURORA_TEST_REQUIRE(!activity.empty());
    AURORA_TEST_REQUIRE(!exit.empty());
    AURORA_TEST_REQUIRE(!icon.empty());
    AURORA_TEST_CHECK_EQ(bell.size(), kFilledDotPx);
    AURORA_TEST_CHECK_EQ(activity.size(), kActivityDotPx);
    AURORA_TEST_CHECK_EQ(exit.size(), kHollowRingPx);
    AURORA_TEST_CHECK_EQ(Harness::holes_in(exit), (kBadgeEdgePx - 2U) * (kBadgeEdgePx - 2U));
    // 字形留在它自己的槽里：左带 x 12..23、行带 y 21..32（F9 实测 + D4 拍的槽）。
    AURORA_TEST_CHECK_GE(Harness::box_of(icon).x0, kGlyphSlotX0);
    AURORA_TEST_CHECK_LE(Harness::box_of(icon).x1, kGlyphSlotX1);
    AURORA_TEST_CHECK_GE(Harness::box_of(icon).y0, kGlyphTopRow);
    AURORA_TEST_CHECK_LE(Harness::box_of(icon).y1, kGlyphBottomRow);
    // 六对两两不相交（这条红了就是本棒撞了 7.82／7.91 的地盘，或角标之间本就撞了）。
    AURORA_TEST_CHECK_EQ(Harness::overlap(icon, bell), std::size_t{0U});
    AURORA_TEST_CHECK_EQ(Harness::overlap(icon, activity), std::size_t{0U});
    AURORA_TEST_CHECK_EQ(Harness::overlap(icon, exit), std::size_t{0U});
    AURORA_TEST_CHECK_EQ(Harness::overlap(bell, activity), std::size_t{0U});
    AURORA_TEST_CHECK_EQ(Harness::overlap(bell, exit), std::size_t{0U});
    AURORA_TEST_CHECK_EQ(Harness::overlap(activity, exit), std::size_t{0U});
    AURORA_TEST_CHECK_GT(Harness::box_of(icon).y0, Harness::box_of(activity).y1);  // 图标在活动点之下
    AURORA_TEST_CHECK_LT(Harness::box_of(icon).x1, Harness::box_of(exit).x0);      // 与右下退出环横向分离
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(changing_one_tabs_kind_leaves_its_neighbour_bit_for_bit_identical) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ⑤：类型是逐格事实而不是整栏的。这里只在**同槽宽**的两档之间换型（`>` 对 `S` 对 `T`），
    // 栏位几何因此一步不动，差分出来的就只有那一枚字形。第一格的墨迹由它自己换型给出，用作「逐位
    // 相同」的候选点集——这一句红了就是「改一格牵连整栏重排」。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    h.set_kind(0U, ConnectionType::Local);
    h.set_kind(1U, ConnectionType::Local);
    const auto both_local = h.pixels();

    h.set_kind(0U, ConnectionType::Ssh);  // 第一格换型：这一簇就是它的墨迹所在
    const Points first_cell = Harness::changed(both_local, h.pixels());
    AURORA_TEST_REQUIRE(!first_cell.empty());
    h.set_kind(0U, ConnectionType::Local);  // 换回基线

    h.set_kind(1U, ConnectionType::Telnet);  // 第二格换型：第一格须逐位不动
    const auto second_changed = h.pixels();
    AURORA_TEST_CHECK_TRUE(Harness::same_at(both_local, second_changed, first_cell));

    const Points second_cell = Harness::changed(both_local, second_changed);
    AURORA_TEST_REQUIRE(!second_cell.empty());
    AURORA_TEST_CHECK_EQ(Harness::overlap(first_cell, second_cell), std::size_t{0U});
    AURORA_TEST_CHECK_GT(Harness::box_of(second_cell).x0, Harness::box_of(first_cell).x1);  // 后一格在前一格之右
    AURORA_TEST_CHECK_GE(Harness::box_of(second_cell).y0, kGlyphTopRow);  // 两簇都只在文本行带内
    AURORA_TEST_CHECK_LE(Harness::box_of(second_cell).y1, kGlyphBottomRow);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_four_kinds_land_four_pairwise_different_frames) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ① 的屏幕形态（表本体由 `utest_connection_glyph` 逐档钉死）：同名同槽，四档各画各的。
    // 两枚型别折成同一字形（＝7.110 那张两值表的复活形态）在这里现形，而不必等真机走查。
    static constexpr std::array<ConnectionType, 4U> kKinds{ConnectionType::Local,
                                                           ConnectionType::Ssh,
                                                           ConnectionType::Serial,
                                                           ConnectionType::Telnet};
    std::vector<Frame> frames;
    for (const auto kind : kKinds) {
        Harness h;
        h.set_kind(0U, kind);
        frames.push_back(h.pixels());
    }
    for (std::size_t i = 0U; i < frames.size(); ++i) {
        for (std::size_t j = i + 1U; j < frames.size(); ++j) {
            AURORA_TEST_CHECK_MSG(frames[i] != frames[j],
                                  "two connection types rendered the very same frame");
        }
    }
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

}  // namespace borealis::test_cases::itest_tab_bar
