/// 测试类型: integration
/// 目标单元: 无单一被测件——本套件是 `SPEC.FEAT.WS.03` 批 1 的**第一道门**（稿 `WS03_MULTI_WINDOW`
///           §0 末条「未实测项」与 §6 批 1）：本仓的无头 itest 能否在**同一支 `au::Application`** 下
///           同时开两扇窗、各渲染各的标签条。
/// 测试说明: 多窗与几何持久化是框架既有公共能力（`Application::open_window` /
///           `set_window_geometry_store`，稿 §0 的 F1／F6），Aurora 侧也自有 `utest_multi_window`
///           证多宿主登记与逐窗 `Scene`/`FocusManager` 隔离。但那一条用的是框架自己的 `Text` 根，
///           **本仓的控件从没在两扇无头窗里同时上过屏**。装配层（`src/main.cpp`）不进 runner
///           （裁决 7.83），「两扇窗真能各开各的」在生产路径上只有源码审查＋真机走查，所以先把
///           **机制**钉成可执行的证人，批 2 起逐窗接线的每条判据才有落脚处。
///
///           四件事值得单独证（判据取自稿 §5 的不变量 1／2 与稿 §0 的 F9／F13）：
///           ① 逐窗 present 只动**自己**那扇的缓冲：A 窗加一格标签后把两扇都重排一遍，B 窗整帧
///              **逐位**不变——这条钉的是「跨窗连带」，也是拆分边界（稿 §2）的前置证人；
///           ② 两扇画的是**各自那份真值源**：内容相同时两幅画布逐位相同，各自加各自的格之后立刻分叉，
///              于是「画错根／共用一份 `TabStrip`」两种缺陷都跑不出去（装配层现在只有一份 `TabStrip`
///              与一份 `current_tab_id`，F9／F10）；
///           ③ 浮层按窗挂：往 B 的 `OverlayHost` 挂一块 Dialog 只盖 B 的画布，A 一帧都不动
///              （F13 那句「一窗一浮层栈」目前只是隐含前提，这里把它显形）；
///           ④ 应用级 `scene()`/`focus()`/`window()` 的作用端**恒是主宿主**（F2／F4）：第二扇窗的
///              焦点管理器从应用级入口取不到，于是装配层那四处 `app.focus()`（F3）与捕唯一 surface
///              的唤醒腿（F11）在批 2 必须改按窗取——这条把「不改就错」钉进用例，而不是等真机上
///              发现键入打不进第二扇窗。
///
///           像素口径与 `itest_tab_bar` 一致：比 RGBA 四通道含 alpha，Headless 缩放为 1（dp 即像素）。
///           本套件不断言具体色值，只断**哪一扇的哪些像素动了、哪一扇逐位没动**——颜色是实现细节，
///           跨窗隔离才是判据。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "borealis/ui/tab_strip.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/tab_bar.h"

namespace borealis::test_cases::itest_workspace_window {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::ui::TabBarHooks;
using borealis::ui::TabBarWidget;
using borealis::ui::TabId;
using borealis::ui::TabNamePriority;
using borealis::ui::TabStrip;
using borealis::ui::TabVisual;
using borealis::ui::resolve_tab_name;

constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 120;
/// @brief 栏高（`TabBarWidget` 的 `bar_height_` 档）：命中取这条带的中线，栏带之外两扇都是空的。
constexpr float kBarHeightDp = 42.0F;
/// @brief 一格栏位的最小宽（`tab_min_width_`）：短名一律钳到 96 dp，故第二格起于 96。
constexpr float kTabMinWidthDp = 96.0F;

using Frame = std::vector<std::uint8_t>;

/// @brief 一扇窗的**呈现半区**：稿 §0 的 F9 那段散在 `main()` 栈上的控件，在此收成一份、逐窗一份。
///
/// 栏位的钩子捕获本件地址，构造后不许再搬动（驱动台以 `std::unique_ptr` 持有）。
class WindowFace {
  public:
    explicit WindowFace(std::string title) {
        options_.size =
            au::Size{.width = static_cast<float>(kWindowWidth), .height = static_cast<float>(kWindowHeight)};
        options_.title = std::move(title);
        au::HeadlessOptions headless;
        static_cast<au::WindowOptions &>(headless) = options_;
        auto created = au::create_window(headless);
        AURORA_TEST_REQUIRE_MSG(created.ok(), "headless window creation failed");
        window_ = std::move(created.value());

        bar_ = std::make_shared<TabBarWidget>(TabBarHooks{
            .tabs = [this]() -> std::vector<TabVisual> {
                std::vector<TabVisual> out;
                const std::optional<TabId> selected = strip_.selected();
                for (const auto &tab : strip_.tabs()) {
                    out.push_back(TabVisual{
                        .id = tab.id,
                        .display_name = resolve_tab_name(tab.names, TabNamePriority::ManualWins),
                        .is_selected = selected.has_value() && selected.value() == tab.id,
                        .has_close_button = strip_.count() > 1U,
                        .bell_triggered = tab.bell_triggered,
                        .has_activity = tab.has_activity,
                        .exited = tab.exited,
                    });
                }
                return out;
            },
            .select = [this](std::uint64_t raw_id) -> void {
                const auto id = static_cast<TabId>(raw_id);
                static_cast<void>(strip_.select(id));
                // 与生产同一句（选中即取走两枚事件角标）；本件的判据不碰角标，但接线形态照抄，
                // 免得驱动台自己变成一处「和生产不一样」的样板。
                static_cast<void>(strip_.take_activity(id));
                static_cast<void>(strip_.take_bell_triggered(id));
            },
            .close = [this](std::uint64_t raw_id) -> void {
                static_cast<void>(strip_.close(static_cast<TabId>(raw_id)));
            },
        });
        // 真值源有任何一次实际转移就把绘制侧标脏（裁决 7.91④）：缺这一钩，框架的布局缓存会跳过
        // `on_layout`，钩子里现取的标签列表永远不会更新——跨窗差分就成了一片恒不差的假绿。
        strip_.set_changed_hook([bar = bar_]() -> void {
            bar->mark_needs_layout();
            bar->mark_needs_paint();
        });
        column_->child_nodes_mut().push_back(au::Node{std::static_pointer_cast<au::Widget>(bar_)});
        host_->child_nodes_mut().push_back(au::Node{std::static_pointer_cast<au::Widget>(column_)});
    }

    /// @brief 交出这扇窗的后端窗口（`Application` 的宿主接管所有权）。
    [[nodiscard]] auto take_window() -> std::unique_ptr<au::Window> { return std::move(window_); }

    /// @brief 取这一侧的场景根：根是浮层宿主，与 `main.cpp` 的 `chrome`→`host`→`root_column` 同形。
    [[nodiscard]] auto scene() -> au::Scene {
        return au::Scene{au::Node{std::static_pointer_cast<au::Widget>(host_)}};
    }

    /// @brief 本窗加一格标签（号由外部那支进程级计数器给，稿 §0 的 F10：只增不复用）。
    auto add_tab(TabId id, const std::u32string &name) -> bool { return strip_.add(id, name); }

    /// @brief 往**本窗**的浮层栈挂一块遮罩对话框（判据 ③ 的那一笔）。
    auto mount_overlay() -> void {
        overlay_ = std::make_shared<au::Dialog>();
        overlay_->set_content(
            au::confirm("Mask", "cross-window overlay isolation", [](bool) -> void {}));
        static_cast<void>(host_->add_overlay(au::Node{std::static_pointer_cast<au::Widget>(overlay_)}));
        overlay_->show();
    }

    [[nodiscard]] auto strip() noexcept -> TabStrip & { return strip_; }
    [[nodiscard]] auto options() const noexcept -> const au::WindowOptions & { return options_; }

  private:
    au::WindowOptions options_{};
    std::unique_ptr<au::Window> window_{};
    TabStrip strip_;
    std::shared_ptr<au::OverlayHost> host_ = std::make_shared<au::OverlayHost>();
    std::shared_ptr<au::Column> column_ = std::make_shared<au::Column>();
    std::shared_ptr<TabBarWidget> bar_{};
    std::shared_ptr<au::Dialog> overlay_{};
};

/// @brief 驱动台：一支 `Application` + 两扇无头窗，逐窗一份 `WindowFace`。
///
/// 成员次序承重：两扇 `face` **先声明**、`app_` **后声明**，于是 `app_` 先析构（宿主连同它接管的
/// `Scene` 一起散），`WindowFace` 后散；反过来的话宿主的场景根会指着已消散的控件树。
/// `app_` 的构造要用第一扇的窗口与场景，那一跳写在成员初始化表里，次序由声明序保证。
class TwoWindowRig {
  public:
    TwoWindowRig()
        : face_a_(std::make_unique<WindowFace>("A")),
          face_b_(std::make_unique<WindowFace>("B")),
          app_(face_a_->scene(), face_a_->take_window(), face_a_->options()) {
        id_a_ = app_.main_window();
        id_b_ = app_.open_window(face_b_->take_window(), face_b_->scene(), face_b_->options());
        render(0U);  // 帧 0：布局与绘制都发生在这里，此后两幅缓冲才有东西可比。
        render(1U);
    }

    TwoWindowRig(const TwoWindowRig &) = delete;
    auto operator=(const TwoWindowRig &) -> TwoWindowRig & = delete;

    /// @brief 只排**指定那一扇**的一帧：跨窗连带在这里没有藏身之处。
    auto render(std::size_t face) -> void {
        au::WindowHost *const target = host(face);
        AURORA_TEST_REQUIRE_MSG(target != nullptr, "the window id is not registered");
        static_cast<void>(target->render_frame(kFrameDt));
    }

    /// @brief 两扇都排一遍，序**与登记序相反**（present 若走共享状态，反序会先现形）。
    auto render_both_reversed() -> void {
        render(1U);
        render(0U);
    }

    /// @brief 该窗当前帧缓冲的 RGBA8 副本（尚未 present 时为空表，断言因此红得可读）。
    [[nodiscard]] auto pixels(std::size_t face) const -> Frame {
        au::Window *const window = host(face)->window();
        AURORA_TEST_REQUIRE(window != nullptr);
        const std::uint8_t *const data = window->surface().data();
        if (data == nullptr) {
            return {};
        }
        Frame out(buffer_pixels() * 4U, 0U);
        std::memcpy(out.data(), data, out.size());
        return out;
    }

    /// @brief 在某一扇窗的栏位内实点一次（走**该窗宿主**的派发端与焦点管理器）。
    auto click(std::size_t face, float x_dp) -> void {
        au::WindowHost *const target = host(face);
        AURORA_TEST_REQUIRE(target != nullptr);
        for (const auto action : {au::MouseAction::Press, au::MouseAction::Release}) {
            target->dispatch_pointer(x_dp, kBarHeightDp * 0.5F, action);
        }
        render(face);
    }

    /// @brief 前置条件：Headless 缩放为 1（dp 即像素，栏位坐标才算得出），且两扇都已 present 出缓冲。
    [[nodiscard]] auto preflight() const -> bool {
        for (std::size_t face = 0U; face < 2U; ++face) {
            au::WindowHost *const target = host(face);
            if (target == nullptr || target->window() == nullptr) {
                return false;
            }
            if (target->window()->surface().scale_factor() != 1.0F) {
                return false;
            }
            if (pixels(face).empty()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto host(std::size_t face) const -> au::WindowHost * {
        return app_.window_host(id_of(face));
    }
    [[nodiscard]] auto face_ref(std::size_t face) noexcept -> WindowFace & {
        return face == 0U ? *face_a_ : *face_b_;
    }
    [[nodiscard]] auto id_of(std::size_t face) const noexcept -> au::WindowId {
        return face == 0U ? id_a_ : id_b_;
    }
    [[nodiscard]] auto app() noexcept -> au::Application & { return app_; }
    [[nodiscard]] auto window_count() const -> std::size_t { return app_.window_count(); }

    /// @brief 两帧逐位相同（判据「另一扇整帧不动」比数差分更严：一个通道都不许差）。
    [[nodiscard]] static auto identical(const Frame &left, const Frame &right) -> bool {
        return !left.empty() && left.size() == right.size() &&
               std::memcmp(left.data(), right.data(), left.size()) == 0;
    }

    /// @brief 两帧之间**任何通道**不同的像素个数。
    [[nodiscard]] static auto changed_count(const Frame &before, const Frame &after) -> std::size_t {
        std::size_t out = 0U;
        const std::size_t span = before.size() < after.size() ? before.size() : after.size();
        for (std::size_t index = 0U; index + 3U < span; index += 4U) {
            if (std::memcmp(before.data() + index, after.data() + index, 4U) != 0) {
                ++out;
            }
        }
        return out;
    }

    /// @brief 栏带内、第 `index` 格栏位中线上的采样点横坐标（差分定位用，不比对具体色值）。
    [[nodiscard]] static auto tab_slot_x(std::size_t index) -> float {
        return (static_cast<float>(index) + 0.5F) * kTabMinWidthDp;
    }

  private:
    static constexpr double kFrameDt = 0.016;

    [[nodiscard]] static auto buffer_pixels() -> std::size_t {
        return static_cast<std::size_t>(kWindowWidth) * static_cast<std::size_t>(kWindowHeight);
    }

    std::unique_ptr<WindowFace> face_a_;
    std::unique_ptr<WindowFace> face_b_;
    au::Application app_;
    au::WindowId id_a_ = au::AURORA_INVALID_WINDOW_ID;
    au::WindowId id_b_ = au::AURORA_INVALID_WINDOW_ID;
};

/// @brief 进程级取号器（稿 §0 的 F10：两扇窗共用一支、只增不复用，故彼此不可能撞号）。
class TabNumbering {
  public:
    [[nodiscard]] auto next() -> TabId { return ++id_; }

  private:
    TabId id_ = 0U;
};

}  // namespace

AURORA_TEST_CASE(two_headless_windows_register_and_render_at_all) {
#ifdef AURORA_BACKEND_HEADLESS
    TwoWindowRig rig;
    AURORA_TEST_REQUIRE(rig.preflight());
    // 两扇都登记了，且各有自己的宿主与号；主窗仍是先登记的那一扇。
    AURORA_TEST_CHECK_EQ(rig.window_count(), std::size_t{2});
    AURORA_TEST_CHECK_NE(rig.id_of(0U), au::AURORA_INVALID_WINDOW_ID);
    AURORA_TEST_CHECK_NE(rig.id_of(1U), au::AURORA_INVALID_WINDOW_ID);
    AURORA_TEST_CHECK_NE(rig.id_of(0U), rig.id_of(1U));
    AURORA_TEST_CHECK(rig.host(0U) != rig.host(1U));
    AURORA_TEST_CHECK_NOT_NULL(rig.host(0U)->window());
    AURORA_TEST_CHECK_NOT_NULL(rig.host(1U)->window());
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(adding_a_tab_in_one_window_leaves_the_other_bitwise_untouched) {
#ifdef AURORA_BACKEND_HEADLESS
    TwoWindowRig rig;
    AURORA_TEST_REQUIRE(rig.preflight());
    TabNumbering numbering;

    // 起点：两扇各一格、同名同位。此时两幅画布须**逐位相同**——这条排掉「第二扇根本没在画自己的
    // 根」和「两扇共用一份标签真值源」两种缺陷（后者让下面那句「B 不动」变成恒真的空转）。
    const TabId shared_first = numbering.next();
    AURORA_TEST_REQUIRE(rig.face_ref(0U).add_tab(shared_first, U"same"));
    AURORA_TEST_REQUIRE(rig.face_ref(1U).add_tab(numbering.next(), U"same"));
    rig.render_both_reversed();
    const auto a_before = rig.pixels(0U);
    const auto b_before = rig.pixels(1U);
    AURORA_TEST_REQUIRE_MSG(TwoWindowRig::identical(a_before, b_before),
                            "two identical tab strips painted differently - the per-window face is not faithful");

    // 只给 A 加第二格，然后**两扇都重排**：A 必须动，B 必须逐位不动。
    const TabId a_second = numbering.next();
    AURORA_TEST_REQUIRE(rig.face_ref(0U).add_tab(a_second, U"extra"));
    rig.render_both_reversed();
    AURORA_TEST_CHECK_GT(TwoWindowRig::changed_count(a_before, rig.pixels(0U)), std::size_t{0});
    AURORA_TEST_CHECK_MSG(TwoWindowRig::identical(b_before, rig.pixels(1U)),
                          "window B's buffer moved when only window A gained a tab (cross-window repaint)");
    // 加进去的是 B 的表里没有的那一格：两份真值源各走各的。
    AURORA_TEST_CHECK_EQ(rig.face_ref(0U).strip().count(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(rig.face_ref(1U).strip().count(), std::size_t{1});
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_click_selects_only_in_the_window_that_received_it) {
#ifdef AURORA_BACKEND_HEADLESS
    TwoWindowRig rig;
    AURORA_TEST_REQUIRE(rig.preflight());
    TabNumbering numbering;

    // 每扇两格：`add` 立即选中最新那格，故两扇的起点都停在第二格上——先把它选回第一格，
    // 于是「点第二格」是一次真实转移，而不是一句 no-op 的假绿。
    const TabId a_first = numbering.next();
    const TabId a_second = numbering.next();
    const TabId b_first = numbering.next();
    const TabId b_second = numbering.next();
    AURORA_TEST_REQUIRE(rig.face_ref(0U).add_tab(a_first, U"one"));
    AURORA_TEST_REQUIRE(rig.face_ref(0U).add_tab(a_second, U"two"));
    AURORA_TEST_REQUIRE(rig.face_ref(1U).add_tab(b_first, U"one"));
    AURORA_TEST_REQUIRE(rig.face_ref(1U).add_tab(b_second, U"two"));
    static_cast<void>(rig.face_ref(0U).strip().select(a_first));
    static_cast<void>(rig.face_ref(1U).strip().select(b_first));
    rig.render_both_reversed();
    const auto b_before = rig.pixels(1U);

    rig.click(0U, TwoWindowRig::tab_slot_x(1U));

    AURORA_TEST_CHECK(rig.face_ref(0U).strip().selected().has_value());
    AURORA_TEST_CHECK_EQ(rig.face_ref(0U).strip().selected().value(), a_second);
    AURORA_TEST_CHECK_EQ(rig.face_ref(1U).strip().selected().value(), b_first);
    AURORA_TEST_CHECK_MSG(TwoWindowRig::identical(b_before, rig.pixels(1U)),
                          "the click in window A reached window B's canvas");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(an_overlay_mounted_on_one_window_covers_only_that_window) {
#ifdef AURORA_BACKEND_HEADLESS
    TwoWindowRig rig;
    AURORA_TEST_REQUIRE(rig.preflight());
    TabNumbering numbering;
    AURORA_TEST_REQUIRE(rig.face_ref(0U).add_tab(numbering.next(), U"same"));
    AURORA_TEST_REQUIRE(rig.face_ref(1U).add_tab(numbering.next(), U"same"));
    rig.render_both_reversed();
    const auto a_before = rig.pixels(0U);
    const auto b_before = rig.pixels(1U);
    AURORA_TEST_REQUIRE(TwoWindowRig::identical(a_before, b_before));

    rig.face_ref(1U).mount_overlay();
    rig.render_both_reversed();

    AURORA_TEST_CHECK_GT(TwoWindowRig::changed_count(b_before, rig.pixels(1U)), std::size_t{0});
    AURORA_TEST_CHECK_MSG(TwoWindowRig::identical(a_before, rig.pixels(0U)),
                          "an overlay mounted on B painted into A (the per-window overlay host is not real)");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_application_level_accessors_only_reach_the_main_host) {
#ifdef AURORA_BACKEND_HEADLESS
    // 这条不是「框架性质」的复述，而是批 2 的**开工依据**：装配层现在有四处 `app.focus()`、一条捕
    // 唯一 surface 的唤醒腿（稿 §0 的 F3／F11），它们在第二扇窗里指的仍是主窗。把它钉成用例，
    // 「键入打不进第二扇窗」就必须在自动化面上现形，而不是留给真机走查。
    TwoWindowRig rig;
    AURORA_TEST_REQUIRE(rig.preflight());

    au::Application &app = rig.app();
    AURORA_TEST_CHECK_EQ(&app.scene(), &rig.host(0U)->scene());
    AURORA_TEST_CHECK_NE(&app.scene(), &rig.host(1U)->scene());
    AURORA_TEST_CHECK_EQ(&app.focus(), &rig.host(0U)->focus());
    AURORA_TEST_CHECK_NE(&rig.host(0U)->focus(), &rig.host(1U)->focus());
    // 应用级 `window()` 恒是主宿主那一只；按窗取才拿得到第二扇的 surface。
    AURORA_TEST_CHECK_EQ(app.window(), rig.host(0U)->window());
    AURORA_TEST_CHECK_NE(app.window(), rig.host(1U)->window());

    // 焦点逐窗不跨：给 B 的根装焦点，A 的焦点管理器仍空着（F3 的那条「焦点序仅作用于本宿主」）。
    AURORA_TEST_CHECK_NULL(rig.host(1U)->focus().focused());
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_workspace_window
