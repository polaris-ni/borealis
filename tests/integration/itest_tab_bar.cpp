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
///           像素一律比 RGBA 四通道含 alpha（与 `itest_render_viewport` 同一条口径：Headless 帧底色是
///           全透明黑，只比 RGB 会把「画了个纯黑」与「什么都没画」混为一谈）。
///           角标的**色值**不在这里断：那三档颜色是本棒拍的实现细节（裁决 7.91③），真机目视才判得出
///           「认不认得出那是铃铛」，故这里只按差分定位形状与位置，不把实现里的三个十六进制数复述一遍。

#include <algorithm>
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

constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 64;
/// @brief 栏高与两枚角标的档位（判据文 `codespec/UI_WORKSPACE_INTERACT.draft.md` 面板 5 的规格）。
constexpr std::size_t kBadgeEdgePx = 6U;
constexpr std::size_t kHollowRingPx = kBadgeEdgePx * kBadgeEdgePx - (kBadgeEdgePx - 2U) * (kBadgeEdgePx - 2U);
constexpr std::size_t kFilledDotPx = kBadgeEdgePx * kBadgeEdgePx;
/// @brief 铃铛（y 6..11）与退出环（y 30..35）的分簇线：栏高 42 的中点。
constexpr std::size_t kUpperHalfRow = 20U;

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
    std::shared_ptr<TabBarWidget> bar_ = std::make_shared<TabBarWidget>(TabBarHooks{
        .tabs = [this]() -> std::vector<TabVisual> {
            std::vector<TabVisual> out;
            const std::optional<TabId> selected = strip_.selected();
            for (const auto &tab : strip_.tabs()) {
                out.push_back(TabVisual{
                    .id = tab.id,
                    .display_name = borealis::ui::resolve_tab_name(tab.names, TabNamePriority::ManualWins),
                    .is_selected = selected.has_value() && selected.value() == tab.id,
                    .has_close_button = strip_.count() > 1U,  // 末位不给关（裁决 7.43④ 同口径）
                    .bell_triggered = tab.bell_triggered,
                    .has_activity = tab.has_activity,
                    .exited = tab.exited,
                });
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

}  // namespace borealis::test_cases::itest_tab_bar
