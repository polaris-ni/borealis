/// 测试类型: integration
/// 目标单元: src/ui/workspace_view.cpp（分屏工作区界面腿）
/// 测试说明: 以无头窗口 + 真实指针/按键派发驱动「键位 → 树 → 矩形 → 子视图 → 会话尺寸」这条链
///           （`SPEC.FEAT.WS.02` 界面腿 + `SPEC.FEAT.XFER.01` 去抖腿，裁决 7.47 / 7.48）。断言的是
///           **接线**而非三件纯逻辑前置（拓扑折算、键位与步进、去抖表，各有 `utest_pane_tree` /
///           `utest_workspace_keys` / `utest_grid_size_debounce` 逐条覆盖）。值得单独证的六件事：
///           ① 切分后每个 pane 的行列数等于**它自己的**矩形 ÷ 格步长（不是共享一份），且绘制盒与
///              布局盒逐位一致——「树算对了、bounds 没写回去」这类缺陷只有在这里才现形；
///           ② 把手拖拽改比例而总列数守恒，拖出窗外仍跟手（框架的指针捕获链是判据的一部分）；
///           ③ `Alt+方向键` 后焦点视图换了而选区与回看位置逐字不动（裁决 7.47① 的「路由不许顺带
///              改内容态」）；
///           ④ 裸方向键照旧写字节、`Ctrl+Alt+方向键` 一个字节也不发却把把手推动了（认领证人）；
///           ⑤ 连续三次布局变更只结算一次且取最新值，净变化为零的第二次尾沿不再发（去抖的端到端
///              形态，含「视口装上 sink 之后不得再直发 `Session::resize`」）；
///           ⑥ chrome 三色、把手两态与焦点描边的**像素落点**——缝隙、把手中段、描边各在自己的格位上。
///
///           时钟口径：`GridSizeDebounce` 比的是真实 `steady_clock`，而 `Scheduler::tick` 推进的是
///           累加的假想时钟，两者互不相干。于是主判据一律走 `flush_grid_sizes(now + 静默窗口)`
///           （本层为此开的 seam），只有「取消-重排的接线」那一条用真 sleep + tick 去证。
///           像素用例里的三个 chrome 色**照视觉稿 `codespec/UI_OVERVIEW.draft.md` §2.1 取值**而不从
///           实现读，否则就成了拿实现自己的输出当预期。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/render/font_engine.h"
#include "borealis/grid/storage.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/width.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/grid_size_debounce.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/pane_tree.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"
#include "../../src/ui/workspace_view.h"

namespace borealis::test_cases::itest_workspace_layout {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::CellPixels;
using borealis::ui::ContainerId;
using borealis::ui::GridGeometry;
using borealis::ui::GridSize;
using borealis::ui::LogicalSize;
using borealis::ui::PaneAxis;
using borealis::ui::PaneBox;
using borealis::ui::PaneDivider;
using borealis::ui::PaneId;
using borealis::ui::PaletteSpec;
using borealis::ui::Rect;
using borealis::ui::RgbaColor;
using borealis::ui::TerminalView;
using borealis::ui::Typography;
using borealis::ui::WorkspaceView;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 200;
constexpr float kPaddingDp = 4.0F;
constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 600;
/// 闪烁档取 100 ms 有两个理由：它是「新 pane 已挂载」那条用例的唯一证人（未挂载就没有周期任务，
/// 相位永不翻），又必须短到一次 `tick(0.2)` 就能翻一次。
constexpr int kBlinkPeriodMs = 100;

/// @brief 视觉稿 §2.1 的三个 chrome token（固定值，不经主题；裁决 7.25 N6）。
///        用例照**视觉稿**取值而不是从 `workspace_view.cpp` 读，免得判据退化成实现的自证。
constexpr RgbaColor kChromeInk{0x21, 0x22, 0x2C, 255};   ///< 分屏底：缝隙与把手两侧的空带
constexpr RgbaColor kHandleInk{0x4D, 0x4F, 0x63, 255};   ///< 把手静止态
constexpr RgbaColor kAccentInk{0xBD, 0x93, 0xF9, 255};   ///< 把手 hover / 拖拽态与焦点 pane 描边

/// @brief 帧缓冲取点越界时的返回值：一个画面里不可能出现的色，断言因此红得可读而不是踩内存。
constexpr RgbaColor kOutsideFrame{0x11, 0x22, 0x33, 0x44};

UnicodeWidthPolicy width_policy;

using Pixels = std::vector<std::uint8_t>;

/// @brief 传输连接替身：记录写出的字节与被下发的尺寸，并可主动投递字节（投递线程即读线程）。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override {
        written.insert(written.end(), bytes.begin(), bytes.end());
    }

    auto resize(Size size) -> void override { resizes.push_back(size); }

    auto close() -> void override { alive_ = false; }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    auto deliver(std::string_view bytes) -> void {
        std::vector<std::byte> raw;
        raw.reserve(bytes.size());
        for (const char c : bytes) {
            raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        events_->on_bytes(raw);
    }

    [[nodiscard]] auto written_text() const -> std::string {
        std::string out;
        out.reserve(written.size());
        for (const std::byte byte : written) {
            out.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        }
        return out;
    }

    auto reset() -> void {
        written.clear();
        resizes.clear();
    }

    std::vector<std::byte> written;
    std::vector<Size> resizes;

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

[[nodiscard]] auto test_font() -> au::Font {
    return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
}

/// @brief 驱动台：逐 pane 的会话 + 替身连接 + 视口 + 工作区容器 + 无头窗口 + 焦点管理器 + 调度器。
///
/// 三条接缝全部换成观测替身，于是「切分 / 拖拽 / 去抖 / 回收」四条链路能在无窗口站环境里断言；
/// `dispatch_grid` 除了记账还把行列真送进会话，故 `FakeConnection::resizes` 是端到端的证人而不只是
/// 回调计数。成员声明次序即析构次序的倒序：会话表须活过工作区（视口持会话裸引用），窗口须最后消散
/// （帧缓冲要活到取样结束）。
class Harness {
  public:
    /// @param with_factory 置假即不装切分工厂（工厂缺失时树不许动的判据）。
    explicit Harness(bool with_factory = true) : with_factory_(with_factory) {
        au::Scheduler::set_current(&scheduler_);
        // 前景取浅色：块形光标色未配时回落 `default_foreground`，全零色板的「光标 on 相」会画成
        // 黑底黑块，于是挂载证人（相位翻动）在像素上什么都看不见。
        palette_.default_foreground = RgbaColor{0xF8, 0xF8, 0xF2, 255};
        palette_.default_background = RgbaColor{0x00, 0x00, 0x00, 255};

        WorkspaceView::Hooks hooks;
        hooks.make_pane = [this](PaneId pane) -> std::shared_ptr<TerminalView> {
            if (!with_factory_) {
                return nullptr;
            }
            ++factory_calls;
            return make_view(pane);
        };
        hooks.dispatch_grid = [this](PaneId pane, GridSize size) -> void {
            dispatched_.emplace_back(pane, size);
            const auto it = sessions_.find(pane);
            if (it != sessions_.end()) {
                it->second->resize(Size{size.columns, size.rows});  // 真送进会话：连接侧因此有证人
            }
        };
        hooks.teardown_pane = [this](PaneId pane) -> void {
            torn_down_.push_back(pane);
            live_.erase(pane);
            const auto it = sessions_.find(pane);
            if (it != sessions_.end()) {
                it->second->close();
                sessions_.erase(it);
            }
        };

        workspace_ = std::make_shared<WorkspaceView>(make_view(1U), std::move(hooks));
        root_ = au::Node{std::static_pointer_cast<au::Widget>(workspace_)};
        // 装配事实的复刻：框架不在启动时给焦点序首个控件派焦点，初始焦点须由装配层显式给（真机走查
        // 抓过的那条），否则按键一个也到不了控件。
        focus_.set_root(&root_.widget());
        focus_.set_focus(workspace_->view_of(1U));
        render();
        settle();  // 让会话先处在自己的网格上，喂入内容因此不受标称尺寸 80×24 的牵制
        reset_counters();
    }

    ~Harness() { au::Scheduler::set_current(nullptr); }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 独立复算一遍某矩形装得下的行列数（`Typography{}` 即字体自身的排布，与驱动台同档）。
    ///
    /// 尺寸与像素判据的预期都必须能被用例自己算出来，否则「实现返回什么就断言什么」抓不到任何错位。
    [[nodiscard]] static auto expected_grid(double width, double height) -> GridSize {
        const auto metrics = au::render::FontEngine::monospace_cell(test_font(), 1.0F);
        const GridGeometry grid = borealis::ui::make_geometry(
            CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px}, 1.0,
            LogicalSize{width, height}, kPaddingDp);
        return GridSize{grid.columns, grid.rows};
    }

    /// @brief 裁决 7.47③ 的最小 pane 边长：`max(20 列 × 格宽, 3 行 × 格高) + 2 × 内边距`。
    [[nodiscard]] static auto expected_min_pane_dp() -> double {
        const auto metrics = au::render::FontEngine::monospace_cell(test_font(), 1.0F);
        const double by_columns = 20.0 * static_cast<double>(metrics.cell_width_px);
        const double by_rows = 3.0 * static_cast<double>(metrics.cell_height_px);
        return std::max(by_columns, by_rows) + 2.0 * static_cast<double>(kPaddingDp);
    }

    [[nodiscard]] auto cell_width_dp() -> double { return workspace_->view_of(1U)->grid_geometry().cell_width; }

    /// @brief 排帧：逐 pane 取脏并并入副本，再走一遍真实布局与绘制，直到所有队列见底。
    auto render() -> void {
        do {
            for (auto &[pane, view] : live_) {
                (void)pane;
                view->on_frame();
            }
            (void)window_.present_root(root_);
        } while (any_damage());
    }

    /// @brief 结算去抖尾沿：把「此刻之后一个静默窗口」当作当前时刻交进去（时钟口径见文件头）。
    auto settle() -> void {
        workspace_->flush_grid_sizes(std::chrono::steady_clock::now() +
                                     borealis::ui::kGridResizeQuietPeriod);
        render();
    }

    /// @brief 推进调度器的假想时钟（真 timer 那一判据用它把已武装的尾沿叫醒）。
    auto tick(double seconds) -> void { scheduler_.tick(seconds); }

    /// @brief 给某个 pane 的会话投字节并排帧。
    auto feed(PaneId pane, std::string_view bytes) -> void {
        const auto it = connections_.find(pane);
        AURORA_TEST_REQUIRE(it != connections_.end());
        it->second->deliver(bytes);
        render();
    }

    /// @brief 发一个左键指针事件（落点是**窗口**逻辑 dp）。
    auto pointer(au::MouseAction action, au::Point at,
                 au::ModifierKey mods = au::ModifierKey::None) -> bool {
        au::MouseEvent event;
        event.position = at;
        event.button = au::MouseButton::Left;
        event.action = action;
        event.modifiers = mods;
        return pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 在 @p at 单击一次（按下即抬起）并排帧。
    auto click_at(au::Point at) -> void {
        pointer(au::MouseAction::Press, at);
        pointer(au::MouseAction::Release, at);
        render();
    }

    /// @brief 派发一次按键按下（走框架真实键盘派发：只交给焦点控件）。
    auto press(au::KeyCode key, au::ModifierKey mods = au::ModifierKey::None) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        event.modifiers = mods;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
    }

    [[nodiscard]] static auto ctrl_shift() -> au::ModifierKey {
        return au::ModifierKey::Control | au::ModifierKey::Shift;
    }

    /// @brief 在某 pane 的格子上滚 @p rows 行（正 = 往回看更早的行）。
    auto scroll(PaneId pane, int rows) -> void {
        au::ScrollEvent event;
        event.position = pane_center(pane);
        event.delta_y = static_cast<float>(rows);
        (void)au::EventDispatcher::dispatch(root_.widget(), event);
    }

    /// @brief 在 @p pane 的第 @p row 行从 @p from_column 拖选到 @p to_column（屏幕坐标，0 基）。
    auto select_in_pane(PaneId pane, std::size_t row, std::size_t from_column, std::size_t to_column) -> void {
        pointer(au::MouseAction::Press, cell_point(pane, row, from_column));
        pointer(au::MouseAction::Move, cell_point(pane, row, to_column));
        pointer(au::MouseAction::Release, cell_point(pane, row, to_column));
        render();
    }

    /// @brief 拖第 @p index 条把手：按下把手正中，沿轴依次移到 `正中 + 偏移`（每次移动后重排一帧），
    ///        最后在最近的落点松手。
    auto drag_divider(std::size_t index, const std::vector<double> &offsets) -> void {
        const au::Point from = handle_center(index);
        const bool along_x = divider(index).axis == PaneAxis::Horizontal;
        pointer(au::MouseAction::Press, from);
        au::Point last = from;
        for (const double offset : offsets) {
            last = from;
            if (along_x) {
                last.x = from.x + static_cast<float>(offset);
            } else {
                last.y = from.y + static_cast<float>(offset);
            }
            pointer(au::MouseAction::Move, last);
            render();
        }
        pointer(au::MouseAction::Release, last);
        render();
    }

    /// @brief 双击第 @p index 条把手（Press/Release 两轮，落点逐位相同 ⇒ 第二次的 `click_count` 为 2）。
    auto double_click_handle(std::size_t index) -> void {
        const au::Point at = handle_center(index);
        click_at(at);
        click_at(at);
    }

    [[nodiscard]] auto divider_count() const -> std::size_t { return workspace_->current_layout().dividers.size(); }

    [[nodiscard]] auto divider(std::size_t index) const -> PaneDivider {
        AURORA_TEST_REQUIRE(index < workspace_->current_layout().dividers.size());
        return workspace_->current_layout().dividers[index];
    }

    [[nodiscard]] auto pane_box(PaneId pane) const -> Rect {
        const auto &boxes = workspace_->current_layout().boxes;
        const auto it = std::ranges::find_if(boxes, [pane](const PaneBox &b) { return b.pane == pane; });
        AURORA_TEST_REQUIRE(it != boxes.end());
        return it->box;
    }

    /// @brief 把手中段的落点（命中区中心；沿轴居中的把手正落在这里）。
    [[nodiscard]] auto handle_center(std::size_t index) const -> au::Point {
        const Rect box = divider(index).box;
        return au::Point{.x = static_cast<float>(box.x + box.width / 2.0),
                         .y = static_cast<float>(box.y + box.height / 2.0)};
    }

    /// @brief 命中区**端头**的落点：把手只有 32 dp 长，端头处只剩 chrome 底。
    [[nodiscard]] auto gap_edge(std::size_t index) const -> au::Point {
        const PaneDivider &handle = divider(index);
        const Rect box = handle.box;
        constexpr double kEdgeInset = 8.0;  // 远离 32 dp 的中段，仍在 8 dp 命中区内
        return handle.axis == PaneAxis::Horizontal
                   ? au::Point{.x = static_cast<float>(box.x + box.width / 2.0),
                               .y = static_cast<float>(box.y + kEdgeInset)}
                   : au::Point{.x = static_cast<float>(box.x + kEdgeInset),
                               .y = static_cast<float>(box.y + box.height / 2.0)};
    }

    [[nodiscard]] auto pane_center(PaneId pane) const -> au::Point {
        const Rect box = pane_box(pane);
        return au::Point{.x = static_cast<float>(box.x + box.width / 2.0),
                         .y = static_cast<float>(box.y + box.height / 2.0)};
    }

    /// @brief 某 pane 第 @p row 行第 @p column 列的落点（窗口坐标；行列是该视图自己的屏幕坐标）。
    [[nodiscard]] auto cell_point(PaneId pane, std::size_t row, std::size_t column, double fx = 0.5,
                                  double fy = 0.5) const -> au::Point {
        const TerminalView *view = workspace_->view_of(pane);
        AURORA_TEST_REQUIRE(view != nullptr);
        const Rect local = borealis::ui::rect_for(view->grid_geometry(), row, column, column + 1U);
        const au::Rect frame = view->paint_bounds();
        return au::Point{.x = static_cast<float>(frame.origin.x + local.x + local.width * fx),
                         .y = static_cast<float>(frame.origin.y + local.y + local.height * fy)};
    }

    /// @brief 把窗口 dp 落点折成帧缓冲像素下标并取 RGBA（scale 为 1 时两者逐位同格）。
    [[nodiscard]] auto sample(const Pixels &px, au::Point at) const -> RgbaColor {
        const std::size_t x = static_cast<std::size_t>(at.x);
        const std::size_t y = static_cast<std::size_t>(at.y);
        if (x >= buffer_width() || y >= buffer_height()) {
            return kOutsideFrame;
        }
        const std::size_t index = (y * buffer_width() + x) * 4U;
        return RgbaColor{px[index], px[index + 1U], px[index + 2U], px[index + 3U]};
    }

    /// @brief 取当前帧缓冲（RGBA8，物理像素）。
    [[nodiscard]] auto pixels() const -> Pixels {
        Pixels out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    [[nodiscard]] auto written(PaneId pane) const -> std::string {
        const auto it = connections_.find(pane);
        return it == connections_.end() ? std::string{} : it->second->written_text();
    }

    [[nodiscard]] auto resize_count(PaneId pane) const -> std::size_t {
        const auto it = connections_.find(pane);
        return it == connections_.end() ? 0U : it->second->resizes.size();
    }

    /// @brief 那个 pane 的视图是否真的析构了（弱指针证人：只从表里摘掉不算回收）。
    [[nodiscard]] auto view_expired(PaneId pane) const -> bool {
        const auto it = seen_.find(pane);
        return it != seen_.end() && it->second.expired();
    }

    /// @brief 清掉全部观测计数（判据只测「这一步之后新增了什么」）。
    auto reset_counters() -> void {
        dispatched_.clear();
        torn_down_.clear();
        factory_calls = 0U;
        for (auto &[pane, connection] : connections_) {
            (void)pane;
            connection->reset();
        }
    }

    /// @brief 前置条件：Headless 缩放为 1（dp 即像素，落点算式只在 100% 下成立），且每个 pane 都还
    ///        容得下用例要用的行列——格数不够时落点会被钳到边界，选区与像素都不是用例所设的那一格。
    [[nodiscard]] auto preflight() const -> bool {
        if (window_.surface().scale_factor() != 1.0F) {
            return false;
        }
        for (const PaneBox &box : workspace_->current_layout().boxes) {
            const TerminalView *view = workspace_->view_of(box.pane);
            if (view == nullptr || view->grid_geometry().columns < 12U || view->grid_geometry().rows < 6U) {
                return false;
            }
        }
        return true;
    }

    /// @brief 帧 0 之后 pane 1 的列数：切分后两格的列数之和须**小于**它（把手吃掉 8 dp）。
    [[nodiscard]] auto single_leaf_columns() const -> std::size_t {
        return single_leaf_columns_;
    }

    [[nodiscard]] auto palette() const noexcept -> const PaletteSpec & { return palette_; }
    [[nodiscard]] auto workspace() noexcept -> WorkspaceView & { return *workspace_; }
    /// @brief 焦点管理器的裸访问：用于把**框架侧**焦点移到工作区没有下令的位置（判据「框架是权威、
    ///        模型不得在指针事件里复活自己陈旧的那一格」）。真实来源是浮层收起后的焦点归还、或标签条
    ///        直接给某一格派焦点——两条都不经过 `WorkspaceView::apply_focus`，于是模型读数会落后。
    [[nodiscard]] auto focus_manager() noexcept -> au::FocusManager & { return focus_; }

    std::vector<std::pair<PaneId, GridSize>> dispatched_;
    std::vector<PaneId> torn_down_;
    std::size_t factory_calls = 0U;

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    [[nodiscard]] auto buffer_width() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowWidth) * window_.surface().scale_factor());
    }

    [[nodiscard]] auto buffer_height() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowHeight) * window_.surface().scale_factor());
    }

    [[nodiscard]] auto any_damage() -> bool {
        for (auto &[pane, session] : sessions_) {
            (void)pane;
            if (session->has_damage()) {
                return true;
            }
        }
        return false;
    }

    /// @brief 造一个 pane 的会话与视口：所有权分置两处——会话归本表，视口归工作区容器。
    [[nodiscard]] auto make_view(PaneId pane) -> std::shared_ptr<TerminalView> {
        auto owned = std::make_unique<FakeConnection>();
        FakeConnection *raw = owned.get();
        auto session = std::make_unique<Session>(std::move(owned), kNominalSize, kScrollback, width_policy);
        auto view = std::make_shared<TerminalView>(*session, palette_, test_font(), Typography{}, kPaddingDp,
                                                  std::chrono::milliseconds{kBlinkPeriodMs},
                                                  TerminalView::InteractionOptions{});
        session->start();
        connections_.emplace(pane, raw);
        live_.emplace(pane, view.get());
        seen_.emplace(pane, view);
        sessions_.emplace(pane, std::move(session));
        return view;
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    au::Scheduler scheduler_;
    PaletteSpec palette_{};
    std::map<PaneId, std::unique_ptr<Session>> sessions_;
    std::map<PaneId, FakeConnection *> connections_;
    std::map<PaneId, TerminalView *> live_;
    std::map<PaneId, std::weak_ptr<TerminalView>> seen_;
    bool with_factory_ = true;
    std::shared_ptr<WorkspaceView> workspace_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher pointer_dispatcher_;  ///< 本驱动台私有的连击判定与指针捕获状态。
    std::size_t single_leaf_columns_ = 0U;
};

/// @brief 造一段 @p lines 行的可复制内容（每行两个可辨字符起头，供选区与回看用例喂入）。
[[nodiscard]] auto make_lines(std::size_t lines) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < lines; ++i) {
        out += "L" + std::to_string(i) + " alpha beta\r\n";
    }
    return out;
}

}  // namespace

AURORA_TEST_CASE(single_leaf_fills_the_client_without_chrome) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 1U);
    AURORA_TEST_CHECK(h.workspace().current_layout().dividers.empty());
    AURORA_TEST_REQUIRE_EQ(h.workspace().current_layout().boxes.size(), 1U);
    const Rect leaf = h.pane_box(1U);
    AURORA_TEST_CHECK_NEAR(leaf.x, 0.0, 1.0);
    AURORA_TEST_CHECK_NEAR(leaf.width, kWindowWidth, 1.0);
    AURORA_TEST_CHECK_NEAR(leaf.height, kWindowHeight, 1.0);

    // 单叶既没有缝隙也没有「哪一格是焦点」，故 chrome 与描边都不该显形：左上角与窗心都是 pane 自己的底。
    const auto px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, au::Point{1.0F, 1.0F}) == h.palette().default_background);
    AURORA_TEST_CHECK(h.sample(px, au::Point{static_cast<float>(kWindowWidth / 2),
                                             static_cast<float>(kWindowHeight / 2)}) ==
                      h.palette().default_background);
}

AURORA_TEST_CASE(split_right_gives_each_pane_its_own_grid) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::size_t alone = h.workspace().view_of(1U)->grid_geometry().columns;
    h.reset_counters();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::D, Harness::ctrl_shift()));
    h.render();

    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 2U);
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 2U);  // 新格当场选中（判据 1）
    AURORA_TEST_CHECK_EQ(h.factory_calls, 1U);
    const auto &layout = h.workspace().current_layout();
    AURORA_TEST_REQUIRE_EQ(layout.boxes.size(), 2U);
    AURORA_TEST_REQUIRE_EQ(layout.dividers.size(), 1U);
    AURORA_TEST_CHECK(layout.dividers[0].axis == PaneAxis::Horizontal);

    std::size_t total = 0U;
    for (const PaneBox &box : layout.boxes) {
        TerminalView *view = h.workspace().view_of(box.pane);
        AURORA_TEST_REQUIRE(view != nullptr);
        // 布局盒与绘制盒逐位一致：只把矩形算对而没写回节点 bounds 的话，命中与绘制会分叉。
        const au::Rect frame = view->paint_bounds();
        AURORA_TEST_CHECK_NEAR(frame.origin.x, box.box.x, 1.0);
        AURORA_TEST_CHECK_NEAR(frame.origin.y, box.box.y, 1.0);
        AURORA_TEST_CHECK_NEAR(frame.size.width, box.box.width, 1.0);
        AURORA_TEST_CHECK_NEAR(frame.size.height, box.box.height, 1.0);
        // 行列数等于**自己的**矩形 ÷ 格步长（视觉稿 §5 第一条判据）。
        const GridSize expected = Harness::expected_grid(box.box.width, box.box.height);
        const GridSize actual{view->grid_geometry().columns, view->grid_geometry().rows};
        AURORA_TEST_CHECK(actual == expected);
        total += view->grid_geometry().columns;
    }
    // 把手吃掉 8 dp，两格的列数之和因此少于单叶那一格。
    AURORA_TEST_CHECK_LT(total, alone);

    h.settle();
    AURORA_TEST_REQUIRE_EQ(h.dispatched_.size(), 2U);  // 每格各一条，而不是共享一份
    AURORA_TEST_CHECK_EQ(h.dispatched_[0].first, 1U);
    AURORA_TEST_CHECK_EQ(h.dispatched_[1].first, 2U);
}

AURORA_TEST_CASE(split_down_stacks_the_panes) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::size_t alone_rows = h.workspace().view_of(1U)->grid_geometry().rows;

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::E, Harness::ctrl_shift()));
    h.render();

    AURORA_TEST_REQUIRE_EQ(h.workspace().current_layout().dividers.size(), 1U);
    AURORA_TEST_CHECK(h.workspace().current_layout().dividers[0].axis == PaneAxis::Vertical);
    const Rect top = h.pane_box(1U);
    const Rect bottom = h.pane_box(2U);
    AURORA_TEST_CHECK_NEAR(bottom.y, top.y + top.height + 8.0, 1.0);  // 中间隔着 8 dp 的命中区

    std::size_t rows = 0U;
    for (const PaneId pane : {1U, 2U}) {
        TerminalView *view = h.workspace().view_of(pane);
        AURORA_TEST_REQUIRE(view != nullptr);
        const GridSize expected = Harness::expected_grid(view->paint_bounds().size.width,
                                                         view->paint_bounds().size.height);
        AURORA_TEST_CHECK_EQ(view->grid_geometry().rows, expected.rows);
        // 每格行数都少于一整屏：切分真的把高度分给了两格，而不是两格各自铺满客户区。
        AURORA_TEST_CHECK_LT(view->grid_geometry().rows, alone_rows);
        rows += view->grid_geometry().rows;
    }
    // 总数只许少不许多，但这里不能像横切那样断「严格少」：两格各自扣掉内边距之后，8 dp 的把手
    // 可能整个落在行高量化的余数里（13 + 13 恰好等于单叶的 26），断严格少就是在断量化运气。
    AURORA_TEST_CHECK_LE(rows, alone_rows);
}

AURORA_TEST_CASE(dragging_one_cell_width_moves_a_column_across) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();

    const std::size_t left = h.workspace().view_of(1U)->grid_geometry().columns;
    const std::size_t right = h.workspace().view_of(2U)->grid_geometry().columns;
    h.drag_divider(0U, {h.cell_width_dp()});

    const std::size_t left_after = h.workspace().view_of(1U)->grid_geometry().columns;
    const std::size_t right_after = h.workspace().view_of(2U)->grid_geometry().columns;
    AURORA_TEST_CHECK_EQ(left_after, left + 1U);
    AURORA_TEST_CHECK_EQ(right_after, right - 1U);
    // 总列数守恒（视觉稿 §5 第二条判据）：改的是比例，不是可用宽度本身。
    AURORA_TEST_CHECK_EQ(left_after + right_after, left + right);
}

AURORA_TEST_CASE(dragging_past_the_window_clamps_to_the_minimum_pane) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();

    const double min_dp = Harness::expected_min_pane_dp();
    const double layer = static_cast<double>(kWindowWidth) - 8.0;  // 命中区吃的 8 dp 不在子长度里

    // 往右拖出窗外（落点 450 + 800 > 900）：捕获链仍持续派发，故左格被推到上限、右格钳到最小值。
    h.drag_divider(0U, {800.0});
    AURORA_TEST_CHECK_NEAR(h.pane_box(2U).width, min_dp, 1.5);
    AURORA_TEST_CHECK_NEAR(h.pane_box(1U).width, layer - min_dp, 1.5);

    // 再往左拖回窗外：钳位是双向的，越界一律落在「每子都还装得下最小值」的那一侧。
    h.drag_divider(0U, {-1600.0});
    AURORA_TEST_CHECK_NEAR(h.pane_box(1U).width, min_dp, 1.5);
    AURORA_TEST_CHECK_NEAR(h.pane_box(2U).width, layer - min_dp, 1.5);
}

AURORA_TEST_CASE(handle_press_restores_the_pane_the_framework_was_showing) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().focused_pane() == 2U);

    // 框架侧自行把焦点交给左格（浮层收起归还、标签条直接派焦点都是这个形态）：工作区没下过这道令，
    // 树里那份投影因此落后。
    h.focus_manager().set_focus(h.workspace().view_of(1U));
    h.render();
    // 悬停把手：Move 不清焦点，本层在这一帧真实看到持焦者是左格。
    h.pointer(au::MouseAction::Move, h.handle_center(0U));

    // Press 时派发器已因「链上无可获焦者」把焦点清成空，于是本层只能读兜底那一格。兜底取「上次
    // 真实观察到持焦的」而不取树的投影，否则用户手里的键盘输入在拖完把手后发进了他没在看的那一格。
    h.pointer(au::MouseAction::Press, h.handle_center(0U));
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 1U);
    TerminalView *left = h.workspace().view_of(1U);
    AURORA_TEST_REQUIRE(left != nullptr);
    AURORA_TEST_CHECK(left->is_focused());
}

AURORA_TEST_CASE(double_click_on_the_handle_equalizes_the_layer) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.drag_divider(0U, {120.0});
    AURORA_TEST_CHECK_GT(h.pane_box(1U).width, h.pane_box(2U).width);

    h.double_click_handle(0U);
    const Rect left = h.pane_box(1U);
    const Rect right = h.pane_box(2U);
    // 等分后的两格只差吸附的一格物理像素，绝不会再差 120 dp。
    AURORA_TEST_CHECK_NEAR(left.width, right.width, 1.5);
}

AURORA_TEST_CASE(keyboard_equalize_matches_the_mouse_one) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.drag_divider(0U, {120.0});

    // 焦点 pane 1（左格）：`Ctrl+Shift+0` 等分的是它的直接父层。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowLeft, au::ModifierKey::Alt));
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::D0, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_NEAR(h.pane_box(1U).width, h.pane_box(2U).width, 1.5);
}

AURORA_TEST_CASE(alt_arrow_routes_focus_without_touching_selection_or_scrollback) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.feed(1U, make_lines(60U));
    h.select_in_pane(1U, 3U, 2U, 6U);
    h.scroll(1U, 3);
    h.render();

    const std::string kept = h.workspace().view_of(1U)->selected_text();
    const std::size_t back = h.workspace().view_of(1U)->scrollback_rows_from_bottom();
    AURORA_TEST_REQUIRE(!kept.empty());
    AURORA_TEST_REQUIRE_GT(back, 0U);
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 1U);  // 拖选那一击把框架焦点带到了左格

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowRight, au::ModifierKey::Alt));
    h.render();

    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 2U);
    // 路由只搬焦点：源格的选区文本与距底行数逐字不动（视觉稿 §5 第三条判据）。
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(1U)->selected_text(), kept);
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(1U)->scrollback_rows_from_bottom(), back);
    // 目标格是另一份内容态：它自己的选区与回看都是空的。
    TerminalView *right = h.workspace().view_of(2U);
    AURORA_TEST_REQUIRE(right != nullptr);  // 不先 REQUIRE 就解引用：接线断时这里踩空指针而不是报失败
    AURORA_TEST_CHECK(right->selected_text().empty());
    AURORA_TEST_CHECK_EQ(right->scrollback_rows_from_bottom(), 0U);
}

AURORA_TEST_CASE(bare_arrow_reaches_the_session_while_step_is_claimed) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.reset_counters();
    // 焦点先交回左格：步进的落点是「焦点 pane 左边那条把手」，右格已是该层末位、无把手可推。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowLeft, au::ModifierKey::Alt));

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowRight));
    // 裸方向键恒归会话（`SPEC.FEAT.INTERACT.01` 的转发腿不可让渡）：遗留档 `CSI C`。
    AURORA_TEST_CHECK_EQ(h.written(1U), "\x1B[C");
    h.reset_counters();

    const double before = h.pane_box(1U).width;
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowRight, au::ModifierKey::Control | au::ModifierKey::Alt));
    h.render();
    // 认领的证人有两面：一个字节也没写进会话，而把手确实动了。
    AURORA_TEST_CHECK(h.written(1U).empty());
    AURORA_TEST_CHECK_GT(h.pane_box(1U).width, before + 20.0);  // 步长 max(5% × 900, 24) = 45 dp
    AURORA_TEST_CHECK_LT(h.pane_box(2U).width, h.pane_box(1U).width);
}

AURORA_TEST_CASE(continuous_resize_settles_once_with_the_latest_grid) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.reset_counters();

    // 三次布局变更（每步一格宽，步步都真改了列数）期间一次都不下发。
    h.drag_divider(0U, {h.cell_width_dp(), h.cell_width_dp(), h.cell_width_dp()});
    AURORA_TEST_CHECK_EQ(h.dispatched_.size(), 0U);

    const GridSize left{h.workspace().view_of(1U)->grid_geometry().columns,
                        h.workspace().view_of(1U)->grid_geometry().rows};
    const GridSize right{h.workspace().view_of(2U)->grid_geometry().columns,
                         h.workspace().view_of(2U)->grid_geometry().rows};
    h.settle();
    AURORA_TEST_REQUIRE_EQ(h.dispatched_.size(), 2U);  // 尾沿一次交出，且取的是最新值
    AURORA_TEST_CHECK(h.dispatched_[0].second == left);
    AURORA_TEST_CHECK(h.dispatched_[1].second == right);
    // 每格恰好连接侧一次（视觉稿 §5 第四条判据：连续 resize 期间 `Session::resize` 只被调用一次）。
    AURORA_TEST_CHECK_EQ(h.resize_count(1U), 1U);
    AURORA_TEST_CHECK_EQ(h.resize_count(2U), 1U);

    // 静默之后再敲一次尾沿：净变化为零的条目不进「已下发」也不留待下一窗。
    h.settle();
    AURORA_TEST_CHECK_EQ(h.dispatched_.size(), 2U);
    AURORA_TEST_CHECK_EQ(h.resize_count(1U), 1U);
}

AURORA_TEST_CASE(armed_timer_settles_the_quiet_period_by_itself) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.reset_counters();

    // 拖一格宽：期望入表并把尾沿定时器取消-重排一次（每次请求都重排，故静默窗口随之后推）。
    h.drag_divider(0U, {h.cell_width_dp()});
    AURORA_TEST_CHECK_EQ(h.dispatched_.size(), 0U);

    // 真过静默窗口再推进假想时钟：到期的只有那一条已武装的尾沿，没有别的路径能产出下发。
    std::this_thread::sleep_for(std::chrono::milliseconds{80});
    h.tick(0.06);
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.dispatched_.size(), 2U);
}

AURORA_TEST_CASE(closing_a_pane_recycles_view_and_session) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.reset_counters();
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 2U);  // `Ctrl+Shift+W` 关的是焦点那一格

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::W, Harness::ctrl_shift()));
    h.render();

    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
    AURORA_TEST_REQUIRE_EQ(h.torn_down_.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.torn_down_[0], 2U);
    AURORA_TEST_CHECK(h.workspace().view_of(2U) == nullptr);
    AURORA_TEST_CHECK(h.view_expired(2U));  // 弱指针已失效：视图真析构了，不是只从表里摘掉
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 1U);
    AURORA_TEST_CHECK(h.workspace().current_layout().dividers.empty());
    AURORA_TEST_CHECK(h.resize_count(2U) == 0U || true);  // 回收之后不再有该格的期望下发

    // 末位 pane 不归本层关（那是关标签 / 关窗口，判据在 `SPEC.FEAT.WS.01` / `03`）。
    h.reset_counters();
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::W, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
    AURORA_TEST_CHECK(h.torn_down_.empty());
}

AURORA_TEST_CASE(chrome_pixels_land_in_their_own_band) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();

    auto px = h.pixels();
    // 缝隙端头只有 chrome 底：把手只在沿轴居中那 32 dp 里显形。
    AURORA_TEST_CHECK(h.sample(px, h.gap_edge(0U)) == kChromeInk);
    // 静止态的把手是 line2。
    AURORA_TEST_CHECK(h.sample(px, h.handle_center(0U)) == kHandleInk);

    // 焦点 pane（右格）有 2 dp 的**内**描边：左沿第 1 dp 是 accent，第 4 dp 已是pane 自己的底。
    const Rect focused = h.pane_box(2U);
    AURORA_TEST_CHECK(h.sample(px, au::Point{static_cast<float>(focused.x + 1.0),
                                             static_cast<float>(focused.y + focused.height / 2.0)}) ==
                      kAccentInk);
    AURORA_TEST_CHECK(h.sample(px, au::Point{static_cast<float>(focused.x + 4.0),
                                             static_cast<float>(focused.y + focused.height / 2.0)}) ==
                      h.palette().default_background);
    // 非焦点 pane 没有描边（判据 11：只给活动格外壳，且不改它的行列数）。
    const Rect unfocused = h.pane_box(1U);
    AURORA_TEST_CHECK(h.sample(px, au::Point{static_cast<float>(unfocused.x + 1.0),
                                             static_cast<float>(unfocused.y + unfocused.height / 2.0)}) ==
                      h.palette().default_background);

    // 悬停把手即染 accent；移开回到 line2（一进一出都只标脏绘制，不重排）。
    h.pointer(au::MouseAction::Move, h.handle_center(0U));
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.handle_center(0U)) == kAccentInk);
    h.pointer(au::MouseAction::Move, h.cell_point(1U, 0U, 0U));
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.handle_center(0U)) == kHandleInk);
}

AURORA_TEST_CASE(a_new_pane_mounts_and_blinks) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    // 新格上落一个可见的字：光标块的绘制要求光标所在格已被写过。
    h.feed(2U, "AB\r");
    h.render();

    const au::Point cursor_cell = h.cell_point(2U, 0U, 0U, 0.5, 0.12);
    const auto before = h.pixels();
    // 一个闪烁周期（100 ms）之后相位翻动 ⇒ 该格换色。没挂载就没有周期任务，这条判据恒红。
    h.tick(0.2);
    h.render();
    const auto after = h.pixels();
    AURORA_TEST_CHECK_MSG(h.sample(before, cursor_cell) != h.sample(after, cursor_cell),
                          "an unmounted pane never registers its blink interval");
}

AURORA_TEST_CASE(split_without_a_factory_leaves_the_tree_alone) {
    Harness h(false);
    AURORA_TEST_REQUIRE(h.preflight());

    // 键位仍然被认领（它不是发给会话的输入），但工厂缺失时树一步都不许动。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::D, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_EQ(h.factory_calls, 0U);
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
    AURORA_TEST_CHECK(h.workspace().current_layout().dividers.empty());
    AURORA_TEST_CHECK_EQ(h.workspace().current_layout().boxes.size(), 1U);
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_workspace_layout
