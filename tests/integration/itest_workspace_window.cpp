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
///           六件事值得单独证（判据取自稿 §5 的不变量 1／2 与稿 §0 的 F9／F13）：
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
///              发现键入打不进第二扇窗；
///           ⑤ 命令落点按**派发事实**记：经该窗宿主的一次栏带按下（落点在空白处也算），就把本窗号交给
///              `ui::ActiveWindowTracker`（批 3-C，稿 D7／G42），无上报时回主窗兜底、交叉上报只留
///              最后一次、撤账后不再返回。框架不给活动窗口查询，这条只能自己钉。
///           ⑥ 关窗那一条链的**框架半区**（批 4-B）：用 `HeadlessSurface::set_should_close(true)` 真
///              驱动一帧循环（F24），证「关一扇不退出而另一扇仍在渲染集合里、宿主在帧末被收走」
///              「`on_frame` 那一跳还看得见活宿主、`on_window_closed` 之后看不见（稿 §6 改口的读数依据，
///              同帧内 `on_frame → render_all` 的先后取框架自述的帧序契约，不冒充实测）」「主窗易主时
///              应用级访问器整组跟着走（`set_fallback` 的来由）」「全关时保留主宿主、`on_window_closed`
///              对最后一扇不触发（F19 的代价档）」。装配层那条撤账链本身在 `main.cpp`，无自动化证人
///              （裁决 7.83）。
///
///           关窗案的两档设定：`WindowOptions::max_frames` 给有限值（帧预算取应用级那一份＝第一扇的
///           options），`power_saving=false` 关省电档——无头后端没有 OS 等待通道，省电档下 `wait_once`
///           会取到「无限等待」把 `run()` 挂死（Aurora 侧 `utest_multi_window` 同档处置）。
///
///           像素口径与 `itest_tab_bar` 一致：比 RGBA 四通道含 alpha，Headless 缩放为 1（dp 即像素）。
///           本套件不断言具体色值，只断**哪一扇的哪些像素动了、哪一扇逐位没动**——颜色是实现细节，
///           跨窗隔离才是判据。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "borealis/ui/tab_strip.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/active_window_tracker.h"
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
    /// @param max_frames 写进本半区的 `WindowOptions`；只有**第一扇**那一份会被 `Application` 当成
    ///        帧预算读（`frame_budget_left()` 取的是应用级 `opts_`），第二扇那份是装饰。
    /// @param power_saving `false` 即让帧循环退回忙轮询——无头后端没有 OS 等待通道，省电档下
    ///        `wait_once` 会取「无限等待」把 `run()` 挂死（Aurora 侧 `utest_multi_window` 同档处置）。
    WindowFace(std::string title, int max_frames = -1, bool power_saving = true) {
        options_.size =
            au::Size{.width = static_cast<float>(kWindowWidth), .height = static_cast<float>(kWindowHeight)};
        options_.title = std::move(title);
        options_.max_frames = max_frames;
        options_.power_saving = power_saving;
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
            // 稿 D7 的标签条上报点：本件此刻还不知道自己那扇窗的号（号由宿主登记时才发），故把上报
            // 转给驱动台后装的那枚闭包——与 `main.cpp` 里「先声明闭包、登记之后再赋值」同一形态。
            .note_press = [this]() -> void {
                if (press_reporter_) {
                    press_reporter_();
                }
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

    /// @brief 装上报闭包（宿主登记之后才有窗号，故这一跳只能后补；未装即一次都不报）。
    auto set_press_reporter(std::function<void()> reporter) -> void {
        press_reporter_ = std::move(reporter);
    }

    /// @brief 往**本窗**的浮层栈挂一块遮罩对话框（判据 ③ 的那一笔）。
    auto mount_overlay() -> void {
        overlay_ = std::make_shared<au::Dialog>();
        overlay_->set_content(
            au::confirm("Mask", "cross-window overlay isolation", [](bool) -> void {}));
        static_cast<void>(host_->add_overlay(au::Node{std::static_pointer_cast<au::Widget>(overlay_)}));
        overlay_->show();
    }

    /// @brief 只标脏、不改内容：让本窗在**下一帧**真的出一次帧，而画布逐位不变。
    ///        无头的脏区决策会整段跳过空闲帧（不 `present()` ⇒ `frame_count()` 不动），于是「另一扇还
    ///        在渲染集合里」这件事不能靠计数字段的自然增长来读，得先给它一口脏。
    auto touch() -> void { bar_->mark_needs_paint(); }

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
    std::function<void()> press_reporter_{};  ///< 空即不上报（宿主登记后由驱动台补装）。
};

/// @brief 驱动台：一支 `Application` + 两扇无头窗，逐窗一份 `WindowFace`。
///
/// 成员次序承重：两扇 `face` **先声明**、`app_` **后声明**，于是 `app_` 先析构（宿主连同它接管的
/// `Scene` 一起散），`WindowFace` 后散；反过来的话宿主的场景根会指着已消散的控件树。
/// `app_` 的构造要用第一扇的窗口与场景，那一跳写在成员初始化表里，次序由声明序保证。
class TwoWindowRig {
  public:
    /// @param frame_budget 帧预算，写进第一扇（＝应用级那一份）`WindowOptions::max_frames`；
    ///        `-1` 即不限，只有靠关闭请求驱动的三条关窗案需要给它一个有限值。
    /// @param power_saving `false`＝忙轮询；有限帧预算的案必须关掉省电档，否则 `run()` 里的
    ///        `wait_once` 在无头后端取到「无限等待」而挂死。
    explicit TwoWindowRig(int frame_budget = -1, bool power_saving = true)
        : face_a_(std::make_unique<WindowFace>("A", frame_budget, power_saving)),
          face_b_(std::make_unique<WindowFace>("B", frame_budget, power_saving)),
          app_(face_a_->scene(), face_a_->take_window(), face_a_->options()) {
        id_a_ = app_.main_window();
        id_b_ = app_.open_window(face_b_->take_window(), face_b_->scene(), face_b_->options());
        // tracker 要主窗号，而号在宿主登记之后才有——与生产同一时序（先声明闭包、登记后再赋值）。
        tracker_.emplace(id_a_);
        face_a_->set_press_reporter([this]() -> void { report(id_a_); });
        face_b_->set_press_reporter([this]() -> void { report(id_b_); });
        // 帧环观测点（批 4-B）：`on_frame` 数帧、`on_window_closed` 记宿主回收到的号。二者只在
        // `run()` 里被调用，`render()` 那条直排宿主的路径不经过它们，故现有六案的行为不变。
        app_.set_on_frame([this]() -> void {
            ++frames_observed_;
            if (frame_probe_) {
                frame_probe_();
            }
        });
        app_.set_on_window_closed(
            [this](au::WindowId closed_id) -> void { closed_ids_.push_back(closed_id); });
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

    /// @brief 只标脏不改内容地碰那一扇一下：让它在帧循环的**下一帧**真出一次帧（见 `WindowFace::touch`）。
    auto touch(std::size_t face) -> void { face_ref(face).touch(); }

    /// @brief 该窗当前帧缓冲的 RGBA8 副本（尚未 present 时为空表，断言因此红得可读）。
    [[nodiscard]] auto pixels(std::size_t face) const -> Frame {
        // 宿主已被回收时**报失败**而不是解引用空指针：批 4-B 的案子里「取那一扇的画布」这件事本身就
        // 可能是判据的落空档，裸 `->` 会把一次可读的失败变成整支 runner 段错误。
        au::WindowHost *const owner = host(face);
        AURORA_TEST_REQUIRE_MSG(owner != nullptr, "that window has been reaped, it has no canvas to read");
        au::Window *const window = owner->window();
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

    /// @brief 命令落点读数（稿 D7 那枚记录器的当前值）。
    [[nodiscard]] auto active() const -> au::WindowId { return tracker_->active(); }

    /// @brief 撤账（关窗那一棒的形态，本套件只测记录面，不真关窗）。
    auto forget(au::WindowId id) -> void { tracker_->forget(id); }

    /// @brief 换兜底落点（批 4-B 装配层那一跳的同一枚接缝）。
    auto set_fallback(au::WindowId id) -> void { tracker_->set_fallback(id); }

    /// @brief 请求关闭指定那一扇（稿 §0 的 F24：无头后端没有 × 按钮，`set_should_close` 是唯一
    ///        能把「用户点了关闭」这件事送进帧循环的入口）。
    auto request_close(std::size_t face) -> void {
        au::WindowHost *const target = host(face);
        AURORA_TEST_REQUIRE_MSG(target != nullptr && target->window() != nullptr,
                                "the window is not registered");
        auto *const surface = dynamic_cast<au::HeadlessSurface *>(&target->window()->surface());
        AURORA_TEST_REQUIRE_MSG(surface != nullptr, "the backend is not a HeadlessSurface");
        surface->set_should_close(true);
    }

    /// @brief 真跑帧循环（批 4-B 的关窗案都要走这一跳：宿主回收只发生在 `run()` 的帧末）。
    auto run() -> void { app_.run(); }

    /// @brief 装一帧内的探针（`run()` 之前装；每一帧的 `on_frame` 时刻调一次）。稿 §6 原写的触发点
    ///        就在这一时刻，探针用来把「这一时刻能看见什么」钉成读数而非叙述。
    auto set_frame_probe(std::function<void()> probe) -> void { frame_probe_ = std::move(probe); }

    /// @brief 宿主回收回调收到过的号，按触发序（F19 那一档「最后一扇不触发」就靠这条现形）。
    [[nodiscard]] auto closed_windows() const -> const std::vector<au::WindowId> & { return closed_ids_; }

    /// @brief `run()` 里 `on_frame` 被调过几次（＝装配层轮询 `should_close()` 那一档的可见帧数）。
    [[nodiscard]] auto frames_observed() const -> std::size_t { return frames_observed_; }

    /// @brief 该窗后端已 present 的帧数；宿主已被回收时为 `-1`（这一扇在本层已经没有账了）。
    [[nodiscard]] auto surface_frames(std::size_t face) const -> int {
        const au::WindowHost *const target = host(face);
        if (target == nullptr || target->window() == nullptr) {
            return -1;
        }
        const auto *const surface = dynamic_cast<const au::HeadlessSurface *>(&target->window()->surface());
        return surface == nullptr ? -1 : surface->frame_count();
    }

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

    /// @brief 栏带空白处的落点（越过全部栏位的那个横坐标）：上报点的判据要的就是这一处——
    ///        「碰过本窗」与「命中了哪一格」是两件事，点在空白处也该换落点。
    [[nodiscard]] static auto empty_band_x() -> float {
        return static_cast<float>(kWindowWidth) - 20.0F;
    }

  private:
    static constexpr double kFrameDt = 0.016;

    /// @brief 上报的唯一入口：`tracker_` 在宿主登记之后才 emplace，故派发若早于那一跳只能丢弃。
    auto report(au::WindowId id) -> void {
        if (tracker_) {
            tracker_->report(id);
        }
    }

    [[nodiscard]] static auto buffer_pixels() -> std::size_t {
        return static_cast<std::size_t>(kWindowWidth) * static_cast<std::size_t>(kWindowHeight);
    }

    std::unique_ptr<WindowFace> face_a_;
    std::unique_ptr<WindowFace> face_b_;
    /// @brief 命令落点记录器（稿 D7）：真正的构造发生在宿主登记之后（要主窗号），故此处先占空位。
    ///        声明在 `app_` **之前**：宿主析构里若还有一次派发，上报闭包会来解引用这一枚 optional，
    ///        于是它必须比宿主活得久一点（析构序＝声明序倒序）。
    std::optional<borealis::ui::ActiveWindowTracker> tracker_{};
    /// @brief 关窗观测账（批 4-B）：宿主回收回调收到过的号与 `on_frame` 次数。
    ///        同样声明在 `app_` **之前**——`~Application` 会销毁宿主，观测账必须比它活得久一点。
    std::vector<au::WindowId> closed_ids_{};
    std::size_t frames_observed_ = 0U;
    /// @brief 帧内探针（`on_frame` 里调）：与 `closed_ids_` 同一档生存期理由，须在 `app_` 之前析构完。
    std::function<void()> frame_probe_{};
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

AURORA_TEST_CASE(a_tab_bar_press_moves_the_active_window_record) {
#ifdef AURORA_BACKEND_HEADLESS
    // 批 3-C 的接线证人（`SPEC.FEAT.WS.03` 稿 D7）：框架既不给活动窗口查询、也不把派发来源带进快捷键
    // 闭包（F4／F5、稿 §3 的 G42），于是「命令该落哪一扇」只能由本仓按**派发事实**自记。上面四条判据
    // 证的是两扇窗各画各的，这一条证的是**记的是哪一扇**——三条缺一即落点会错：
    // ① 起点回主窗：没有任何上报时落点是构造期那枚号，兜底而非猜测（第二扇窗还没被碰过时，命令不能
    //    凭空落进它）；
    // ② 最近者胜：交叉上报以最后一次为准，本件不计数也不建栈；
    // ③ 撤账之后不再返回：关掉的那扇窗不留悬空落点，回到兜底；兜底自己关了才返回无效号。
    // 这里走的是**真派发**（`dispatch_pointer` 经该窗宿主），故 `TabBarHooks::note_press` 与控件的
    // 命中序也被顺带证到：栏带内的按下确实进到了那一扇的标签条。
    TwoWindowRig rig;
    AURORA_TEST_REQUIRE(rig.preflight());
    TabNumbering numbering;
    const TabId a_only = numbering.next();
    const TabId b_only = numbering.next();
    AURORA_TEST_REQUIRE(rig.face_ref(0U).add_tab(a_only, U"only"));
    AURORA_TEST_REQUIRE(rig.face_ref(1U).add_tab(b_only, U"only"));
    rig.render_both_reversed();

    AURORA_TEST_CHECK_EQ(rig.active(), rig.id_of(0U));  // ① 未碰过＝兜底落点，而不是「取不到就谁也不做」

    // 落点取栏带**空白处**：这里要钉的是「碰过哪一扇」，不是「命中哪一格」。点空白处既选中不了任何
    // 标签、也关不了栏位，但它仍是一次真实触碰——上报若挂在命中分支里，这一笔就永远不响。
    rig.click(1U, TwoWindowRig::empty_band_x());
    AURORA_TEST_CHECK_EQ(rig.active(), rig.id_of(1U));  // ② B 的栏带被碰过，落点即改
    AURORA_TEST_CHECK_EQ(rig.face_ref(1U).strip().selected().value(), b_only);  // 动作腿没跟着动

    rig.click(0U, TwoWindowRig::empty_band_x());
    AURORA_TEST_CHECK_EQ(rig.active(), rig.id_of(0U));  // ② 交叉上报只留最后一次

    rig.click(1U, TwoWindowRig::empty_band_x());
    rig.forget(rig.id_of(1U));
    AURORA_TEST_CHECK_EQ(rig.active(), rig.id_of(0U));  // ③ 撤账回到兜底，不返回已关的号

    rig.forget(rig.id_of(0U));
    AURORA_TEST_CHECK_EQ(rig.active(), au::AURORA_INVALID_WINDOW_ID);  // ③ 兜底自己没了才报无效号
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(closing_one_window_reaps_it_at_frame_end_without_stopping_the_other) {
#ifdef AURORA_BACKEND_HEADLESS
    // 批 4-B 的第一道证人（稿 §7「关一扇不退出」那条）。稿 §0 的 F21 说的是另一件事：全为**无头宿主**
    // 时框架永不驱动退出——这里两扇都挂着真 `HeadlessSurface` 窗口，关闭请求由 `set_should_close`
    // 送进帧循环（F24），于是「关一扇」这条在自动化面上第一次可跑。帧预算取 3、省电档关掉：无头没有
    // OS 等待通道，省电档下 `wait_once` 会取到「无限等待」把 `run()` 挂死。
    TwoWindowRig rig{3, false};
    AURORA_TEST_REQUIRE(rig.preflight());

    const auto a_before = rig.pixels(0U);
    const int a_frames_before = rig.surface_frames(0U);
    AURORA_TEST_REQUIRE(a_frames_before >= 0);

    rig.request_close(1U);
    // 请求那一刻宿主还在：回收是帧末那一步（F17），不在调用点销毁——稿 D4 要的「关闭否决」窗口期
    // 正是这一截，而框架不给否决入口（F18），本层只能在这截里读、不能在里头痛改。
    AURORA_TEST_CHECK_NOT_NULL(rig.host(1U));
    AURORA_TEST_CHECK_EQ(rig.window_count(), std::size_t{2});
    // 第二帧起给 A 一口脏：无头的脏区决策会整段跳过空闲帧（连 `present()` 都不走，`frame_count()`
    // 自然不动），故「那一扇仍在渲染集合里」只能用「主动标脏 ⇒ 计数字段该涨」来读，而不是等它自己涨。
    rig.set_frame_probe([&rig]() -> void {
        if (rig.frames_observed() >= 2U) {
            rig.touch(0U);
        }
    });

    rig.run();

    AURORA_TEST_CHECK_EQ(rig.window_count(), std::size_t{1});
    AURORA_TEST_CHECK_NULL(rig.host(1U));      // 按窗现取的腿自此取空＝命令落空（装配层的 fail-closed 档）
    AURORA_TEST_CHECK_NOT_NULL(rig.host(0U));
    AURORA_TEST_REQUIRE_EQ(rig.closed_windows().size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(rig.closed_windows().front(), rig.id_of(1U));  // 收到的是**关掉那一扇**的号
    AURORA_TEST_CHECK_EQ(rig.app().main_window(), rig.id_of(0U));       // 还有一扇活着时主窗位不动
    AURORA_TEST_CHECK_GT(rig.frames_observed(), std::size_t{0});
    // 另一扇继续渲染：这是多窗帧循环的核心收益，也是稿 §9 第 3 条「关一扇后另一扇不受扰动」的无头半区。
    AURORA_TEST_CHECK_GT(rig.surface_frames(0U), a_frames_before);
    // 扰动到不了那一扇的画面：它画的是**自己**那份真值源，别人散了它逐位不变。
    AURORA_TEST_CHECK_MSG(TwoWindowRig::identical(a_before, rig.pixels(0U)),
                          "the surviving window's canvas moved when only the other one was reaped");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_frame_callback_still_sees_the_host_the_closed_hook_does_not) {
#ifdef AURORA_BACKEND_HEADLESS
    // 这条钉的是稿 §6 批 4-B **改口**的依据。原写的触发点是「在 `set_on_frame` 里轮询 `should_close()`
    // 并撤账」，看的是 F17 那句「销毁前唯一可观测点」；读数证明这一跳确实看得见它（下面 `seen_alive`
    // ＝第一帧那一次），但也正因为看得见——宿主还在登记表里、本帧的布局与绘制还轮到它——本层的控件
    // hooks 捕的是 `WorkspaceWindow &`，在那一跳里把元素从容器 `erase` 掉就是「为刚被析构的那一格
    // 画一帧」。改到 `on_window_closed` 之后，宿主已从 `hosts_` 摘掉、这一扇再不会被渲染，落点才干净。
    // 「`render_all` 排在 `on_frame` 之后」这一半取的是框架自述的帧序契约（Aurora 主仓
    // `include/aurora/app/application.h` 里 `run()` 的八步注释），本棒不改 Aurora、
    // 也不冒充实测：计数读数分不出同帧内的先后，故这里只报能报的两档。
    TwoWindowRig rig{3, false};
    AURORA_TEST_REQUIRE(rig.preflight());
    rig.request_close(1U);

    std::size_t seen_alive = 0U;
    std::size_t seen_gone = 0U;
    bool hook_had_already_fired = false;
    rig.set_frame_probe([&rig, &seen_alive, &seen_gone, &hook_had_already_fired]() -> void {
        if (rig.host(1U) != nullptr) {
            ++seen_alive;
            return;
        }
        ++seen_gone;
        if (!rig.closed_windows().empty()) {
            hook_had_already_fired = true;
        }
    });
    rig.run();

    AURORA_TEST_CHECK_EQ(rig.frames_observed(), std::size_t{3});  // 关一扇不让帧循环提前退
    AURORA_TEST_CHECK_EQ(seen_alive, std::size_t{1});             // 只有第一帧的 `on_frame` 还看得见它
    AURORA_TEST_CHECK_EQ(seen_gone, std::size_t{2});              // 其后两帧：宿主已在第一帧末被收走
    // 撤账挂点的顺序前提：探针在「宿主已不在」的那些帧里读到的回调账目已经写上去了，
    // 即 `on_window_closed` 排在**下一次** `on_frame` 之前——本层在那一跳里落地对象不会晚于任何读者。
    AURORA_TEST_CHECK_TRUE(hook_had_already_fired);
    AURORA_TEST_REQUIRE_EQ(rig.closed_windows().size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(rig.closed_windows().front(), rig.id_of(1U));
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_main_window_slot_moves_to_a_surviving_window) {
#ifdef AURORA_BACKEND_HEADLESS
    // 主窗易主＝批 4-B 那枚 `set_fallback` 接缝的**来由**（F23：`reap_closed` 末尾把 `main_host_`
    // 交给登记序里活着的第一扇）。不接这一跳，装配层的兜底落点就还指着已经散掉的那一扇，而 `forget`
    // 之后取到的是无效号——命令于是永远落不进那扇唯一还活着的窗。
    TwoWindowRig rig{3, false};
    AURORA_TEST_REQUIRE(rig.preflight());
    rig.request_close(0U);
    rig.run();

    AURORA_TEST_CHECK_NULL(rig.host(0U));
    AURORA_TEST_CHECK_EQ(rig.window_count(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(rig.app().main_window(), rig.id_of(1U));
    // 应用级访问器整组跟着易主（F2 那一条的反面：主宿主换了，`scene()`/`window()` 指的也是新的）。
    AURORA_TEST_CHECK_EQ(&rig.app().scene(), &rig.host(1U)->scene());
    AURORA_TEST_CHECK_EQ(rig.app().window(), rig.host(1U)->window());
    AURORA_TEST_REQUIRE_EQ(rig.closed_windows().size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(rig.closed_windows().front(), rig.id_of(0U));

    // 装配层关窗撤账链后那一跳的形状（`main.cpp` 里第 ③⑤ 步）：撤掉散掉那一扇的账 ⇒ 落点取不到 ⇒
    // 由本层把兜底换成账目里还活着的第一扇 ⇒ 命令重新有落点。
    AURORA_TEST_CHECK_EQ(rig.active(), rig.id_of(0U));  // 起点：没人被碰过 ⇒ 兜底＝构造期主窗号
    rig.forget(rig.id_of(0U));
    AURORA_TEST_CHECK_EQ(rig.active(), au::AURORA_INVALID_WINDOW_ID);
    rig.set_fallback(rig.id_of(1U));
    AURORA_TEST_CHECK_EQ(rig.active(), rig.id_of(1U));
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_last_host_is_retained_so_the_closed_hook_skips_it) {
#ifdef AURORA_BACKEND_HEADLESS
    // F19 的代价档，批 4-B 把它钉成读数而不是叙述：全关时框架**保留主宿主**（使 `scene()`/`focus()`/
    // `window()` 在 `run()` 之后仍可用），被保留的那一扇既不会从 `hosts_` 摘掉，也**不会**触发
    // `on_window_closed`。于是装配层的关窗撤账链在最后一扇上不跑——那一扇的账目留到 `run()` 返回之后
    // 由退出序统一收（本仓 `main.cpp` 末尾那条 `session->close()` 循环）。
    //
    // 「最后一扇」必须在循环**进行中**才关：`run()` 的启动判据是「至少一扇有 Window 且未请求关闭」，
    // 两扇都在进循环之前就请求关闭的话 `run()` 直接返回（连一帧都不排），既看不到回收、也看不到
    // 保留这一档——那是「没启动」而不是「启动了又不退出」，两者不能混为一条读数。
    TwoWindowRig rig{3, false};
    AURORA_TEST_REQUIRE(rig.preflight());
    rig.request_close(1U);  // 第一帧收走 B，主宿主仍是 A
    rig.set_frame_probe([&rig]() -> void {
        if (rig.frames_observed() == 2U) {
            rig.request_close(0U);  // 第二帧起：A 成了唯一一扇，也是「最后一扇」
        }
    });
    rig.run();

    // 回调只报过被摘掉的那一扇：最后一扇走的是「保留」而不是「回收」。
    AURORA_TEST_REQUIRE_EQ(rig.closed_windows().size(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(rig.closed_windows().front(), rig.id_of(1U));
    AURORA_TEST_CHECK_NULL(rig.host(1U));
    // 最后一扇没被摘走，也没报进回收回调。
    AURORA_TEST_CHECK_EQ(rig.window_count(), std::size_t{1});
    AURORA_TEST_CHECK_NOT_NULL(rig.host(0U));
    AURORA_TEST_CHECK_EQ(rig.app().main_window(), rig.id_of(0U));
    // 访问器在 `run()` 之后仍可用＝保留这一只的全部理由（本层若指望这一跳做外部副作用，最后一扇必丢）。
    AURORA_TEST_CHECK_NOT_NULL(rig.app().window());
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
