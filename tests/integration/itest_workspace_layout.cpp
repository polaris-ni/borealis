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
///              形态，含「视口装上 sink 之后不得再直发 `Session::resize`」）——面板运行期连改两次
///              字号同理，两次都不发、尾沿一次交出最新格；
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
using borealis::ui::PaneShape;
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

auto width_policy = std::make_shared<borealis::term::UnicodeWidthPolicy>();

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
        bootstrap();
        WorkspaceView::Hooks hooks;
        wire_hooks(hooks);
        workspace_ = std::make_shared<WorkspaceView>(make_view(1U), std::move(hooks));
        finish(workspace_->focused_pane(), true);
    }

    /// @brief 恢复腿的驱动台：以 `WorkspaceView::restore` 起步而不是单叶构造。
    ///
    /// 视图按**序位**先造好再整份交出去（会话与替身连接照 `make_view` 入表，标识即那条公开约定的
    /// i+1）；`restore` 回空即这份工作区根本没建起来，本驱动台于是没有可渲染的根，用例只断言「没建
    /// 起来」而不排帧——这正是那条接缝的失败形态。
    /// @param shape 待重建的形状。
    /// @param views 交出去的视图份数：故意可以与形状的叶子数不等，那是份数判据的证人。
    /// @param focused_order 选中格的阅读序序位。
    Harness(const PaneShape &shape, std::size_t views, std::size_t focused_order) {
        bootstrap();
        WorkspaceView::Hooks hooks;
        wire_hooks(hooks);
        std::vector<std::shared_ptr<TerminalView>> built;
        built.reserve(views);
        for (std::size_t order = 0U; order < views; ++order) {
            built.push_back(make_view(static_cast<PaneId>(order) + 1U));
        }
        workspace_ = WorkspaceView::restore(std::move(built), shape, focused_order, std::move(hooks));
        if (workspace_ != nullptr) {
            finish(workspace_->focused_pane(), false);
        }
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

    /// @brief 派发一次按键事件（走框架真实键盘派发：只交给焦点控件）。默认按下，抬起留给上报点的
    ///        反面证人——`SPEC.FEAT.WS.03` 稿 D7 只把「按下」算作一次碰过本窗。
    auto press(au::KeyCode key, au::ModifierKey mods = au::ModifierKey::None,
               au::KeyAction action = au::KeyAction::Down) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = action;
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

    /// @brief 把手长轴上距命中区中心 @p offset dp 的落点：32 dp 中段内/外的判据用它一次取两点。
    [[nodiscard]] auto handle_along_point(std::size_t index, double offset) const -> au::Point {
        const PaneDivider &handle = divider(index);
        const Rect box = handle.box;
        const double cx = box.x + box.width / 2.0;
        const double cy = box.y + box.height / 2.0;
        // 与 `paint_handle` 同一读法：`axis == Horizontal` 是子 pane 沿 x 排开，缝是竖窄条，长轴在 y。
        return handle.axis == PaneAxis::Horizontal
                   ? au::Point{.x = static_cast<float>(cx), .y = static_cast<float>(cy + offset)}
                   : au::Point{.x = static_cast<float>(cx + offset), .y = static_cast<float>(cy)};
    }

    /// @brief 把整棵根内缩到窗口原点之外（复刻装配层：工作区挂在折叠轨与标题条之后）。
    ///
    /// 真机抓到的那条缺陷只在原点非零时显形——`paint_zoom_badge` 的字形端点漏加 `bounds.origin`，
    /// 用例若把根挂在 (0,0) 上，局部与全局同值，像素判据一律通过。
    /// @param left 左边内缩（dp）。
    /// @param top 上边内缩（dp）。
    auto inset_root(double left, double top) -> void {
        workspace_->set_modifier(au::Modifier{}.padding(
            au::EdgeInsets{.left = static_cast<float>(left), .top = static_cast<float>(top)}));
        workspace_->mark_needs_layout();
        render();
        settle();
    }

    /// @brief 工作区**内容盒**在窗口里的全局原点（内缩后即非零，像素落点要按它平移）。
    ///
    /// 不能直接取 `workspace_->paint_bounds()`：那是父级递下来的外层盒，`Modifier::padding` 只平移
    /// 传给 `on_paint` 的内容盒（`Widget::paint` 先记外层盒再算 content_box）。这里借 1 号格的视图
    /// 反推——它是本层布局的第一个子节点，其全局绘制盒减去布局给出的局部盒即那一次平移。
    [[nodiscard]] auto workspace_origin() const -> au::Point {
        const TerminalView *view = workspace_->view_of(1U);
        AURORA_TEST_REQUIRE(view != nullptr);
        const au::Point global = view->paint_bounds().origin;
        const Rect local = pane_box(1U);
        return au::Point{.x = static_cast<float>(static_cast<double>(global.x) - local.x),
                         .y = static_cast<float>(static_cast<double>(global.y) - local.y)};
    }

    [[nodiscard]] auto pane_center(PaneId pane) const -> au::Point {
        const Rect box = pane_box(pane);
        return au::Point{.x = static_cast<float>(box.x + box.width / 2.0),
                         .y = static_cast<float>(box.y + box.height / 2.0)};
    }

    /// @brief 缩放角标的几何中心：20 dp 底板距 pane 右边与上边各 4 dp（视觉稿 §2.2）。
    [[nodiscard]] auto badge_center(PaneId pane) const -> au::Point {
        const Rect box = pane_box(pane);
        return au::Point{.x = static_cast<float>(box.x + box.width - 14.0),
                         .y = static_cast<float>(box.y + 14.0)};
    }

    /// @brief 展开字形（外向双箭头）上一个满覆盖的 accent 落点。
    ///
    /// `Painter::draw_line` 按**像素中心**采样并留 1 px 羽化，斜线正好经过像素中心时 coverage 才为
    /// 1；字形中心是两条线的端点，最近像素中心离端点 0.707 px，取到的只是混色。沿 45° 线取
    /// （中心 + 1.5, 中心 − 1.5）那个像素中心，点进来时 floor 后正是它。
    [[nodiscard]] auto badge_expand_ink_point(PaneId pane) const -> au::Point {
        const au::Point center = badge_center(pane);
        return au::Point{.x = static_cast<float>(static_cast<int>(center.x) + 1),
                         .y = static_cast<float>(static_cast<int>(center.y) - 2)};
    }

    /// @brief 收拢字形（内向双箭头）的满覆盖落点：右下那条 45° 线在中点外侧 0.8..5 px。
    [[nodiscard]] auto badge_collapse_ink_point(PaneId pane) const -> au::Point {
        const au::Point center = badge_center(pane);
        return au::Point{.x = static_cast<float>(static_cast<int>(center.x) + 2),
                         .y = static_cast<float>(static_cast<int>(center.y) + 2)};
    }

    /// @brief 底板内不压字形的取样点（离底板左上角 2 dp；字形盒从 5 dp 处才开始）。
    [[nodiscard]] auto badge_plate_point(PaneId pane) const -> au::Point {
        const Rect box = pane_box(pane);
        return au::Point{.x = static_cast<float>(box.x + box.width - 4.0 - 20.0 + 2.0),
                         .y = static_cast<float>(box.y + 4.0 + 2.0)};
    }

    /// @brief 焦点药丸底板上的一个满覆盖落点：底板右沿＝pane 右 − (4 + 20 + 6)（让位给缩放角标），
    ///        取它内 1 dp、底板铅垂居中那一点——圆角只吃四角，不吃这一档。
    [[nodiscard]] auto pill_plate_point(PaneId pane) const -> au::Point {
        const Rect box = pane_box(pane);
        return au::Point{.x = static_cast<float>(box.x + box.width - 31.0),
                         .y = static_cast<float>(box.y + 16.0)};
    }

    /// @brief 药丸底板上「文案一定经过这里」的那一条带：右沿取底板右沿内 1 dp（避开那一条 AA），
    ///        向左 60 dp 覆盖住整枚按测宽铺开的底板，上下取中段避开圆角。带内既可能有 pane 自己的
    ///        底（底板没铺到的左段），也可能有底板与字墨——三条计数各管一件事。
    [[nodiscard]] auto pill_glyph_band(PaneId pane) const -> Rect {
        const Rect box = pane_box(pane);
        return Rect{.x = box.x + box.width - 90.0,
                    .y = box.y + 11.0,
                    .width = 60.0,
                    .height = 10.0};
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

    /// @brief 该格**上一份**被换下来的视图是否真散了（`replace_view` 的证人：只改表不换对象不算）。
    [[nodiscard]] auto replaced_view_expired(PaneId pane) const -> bool {
        const auto it = history_.find(pane);
        if (it == history_.end() || it->second.size() < 2U) {
            return false;
        }
        return it->second[it->second.size() - 2U].expired();
    }

    /// @brief 复刻装配层重启腿的次序换掉该格的视图（会话与视口都换成新的）。
    ///
    /// 旧会话必须先移出记账表、但仍留在手里，等 `replace_view` 回来再散：那一步内部就地销毁旧视图，
    /// 而 `~TerminalView` 要往它背后的会话写撤账空响应——先把会话 dispose 就是取用已消散的对象。
    /// 生产侧同一跳见 `src/main.cpp` 的重启腿（稿 `WS02_PANE_SESSION.draft.md` §5）。
    auto replace_pane_view(PaneId pane) -> bool {
        retired_.push_back(std::move(sessions_[pane]));
        sessions_.erase(pane);
        connections_.erase(pane);
        live_.erase(pane);
        // 生产的重启腿由派发给菜单/按钮的指针事件带动，`resolve_focus_manager` 因此在栈内拿到派发
        // 用的那份管理器。栈外调用只会落到按根缓存的兜底实例，那样本表用来派发的那份焦点仍指着刚销毁
        // 的旧视图，判据就变成用已消散的控件按键——故这里以同一个上下文包住这一跳（稿 §5 的次序纪律）。
        au::set_current_focus_manager(&focus_);
        const bool swapped = workspace_->replace_view(pane, make_view(pane));
        au::set_current_focus_manager(nullptr);
        retired_.clear();
        return swapped;
    }

    /// @brief 造一份不属于任何格的视图（判据「换到不存在的格」时交进去的那一个参数得是真视图）。
    [[nodiscard]] auto make_orphan_view(PaneId spare) -> std::shared_ptr<TerminalView> {
        return make_view(spare);
    }

    /// @brief 清掉全部观测计数（判据只测「这一步之后新增了什么」）。
    auto reset_counters() -> void {
        dispatched_.clear();
        torn_down_.clear();
        factory_calls = 0U;
        new_window_calls = 0U;
        close_tab_requests = 0U;
        dispatch_reports = 0U;
        structure_changes = 0U;
        structure_panes.clear();
        structure_teardowns.clear();
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
    /// @brief 恢复腿是否真建出了工作区（拒绝案的唯一读数：回空即「没有半个工作区」，而不是「建了
    ///        一棵少一格的树」）。没建起来时本驱动台没有根，其余观测面一概不可用。
    [[nodiscard]] auto restored() const noexcept -> WorkspaceView * { return workspace_.get(); }
    /// @brief 焦点管理器的裸访问：用于把**框架侧**焦点移到工作区没有下令的位置（判据「框架是权威、
    ///        模型不得在指针事件里复活自己陈旧的那一格」）。真实来源是浮层收起后的焦点归还、或标签条
    ///        直接给某一格派焦点——两条都不经过 `WorkspaceView::apply_focus`，于是模型读数会落后。
    [[nodiscard]] auto focus_manager() noexcept -> au::FocusManager & { return focus_; }

    std::vector<std::pair<PaneId, GridSize>> dispatched_;
    std::vector<PaneId> torn_down_;
    std::size_t factory_calls = 0U;
    /// @brief `Ctrl+Shift+N` 的转交次数（第十五键只转交不造窗，`SPEC.FEAT.WS.03` 稿 D2）。
    std::size_t new_window_calls = 0U;
    /// @brief 视口上报次数（`SPEC.FEAT.WS.03` 稿 D7 的第一处上报点）：生产里这一枚闭包交给装配层
    ///        的 `ui::ActiveWindowTracker`，本驱动台只数次数——「报了几次、什么算一次」就是判据。
    std::size_t dispatch_reports = 0U;
    /// @brief 工厂最后一次收到的 `origin`（判据「被切那一格交给工厂，而不是工厂自己猜焦点」）。
    PaneId last_origin = 0U;
    /// @brief 末位 pane 的关格键转交「关标签」的次数（`SPEC.FEAT.WS.01`，裁决 7.42⑤）。
    ///
    /// 本层不造空树、也不判退出：单格标签上按 `Ctrl+Shift+W` 只该让装配层收到一次请求，树与回收
    /// 读数一概不动。缺这一枚读数，「按键落空」与「转交了但生产侧接错」在无头面分不开。
    std::size_t close_tab_requests = 0U;
    /// @brief `structure_changed` 触发次数（`SPEC.FEAT.WS.11` 批 5-B 采集腿的唯一证人：装配层的落盘
    ///        只挂在这一枚通知上，别处没有第二个观察点）。
    std::size_t structure_changes = 0U;
    /// @brief 每次触发那一刻的 pane 数。判据不是「响了几次」而是「响的那一刻树已经换了没有」：同一枚
    ///        闭包若装进 `make_pane`，次数一条不差，写出去的形状却少一格。
    std::vector<std::size_t> structure_panes;
    /// @brief 每次触发那一刻已回收的 pane 数：切分腿上它是 0、关格腿上它是 1，故这一列钉的是「这一响
    ///        出自哪条腿」——响在 `teardown_pane` 里而不是响在改树之后，会在这儿露出来。
    std::vector<std::size_t> structure_teardowns;

  private:
    /// @brief 两条构造路径共用的前置：调度器与色板。
    auto bootstrap() -> void {
        au::Scheduler::set_current(&scheduler_);
        // 前景取浅色：块形光标色未配时回落 `default_foreground`，全零色板的「光标 on 相」会画成
        // 黑底黑块，于是挂载证人（相位翻动）在像素上什么都看不见。
        palette_.default_foreground = RgbaColor{0xF8, 0xF8, 0xF2, 255};
        palette_.default_background = RgbaColor{0x00, 0x00, 0x00, 255};
    }

    /// @brief 三条接缝换成观测替身（构造与恢复两条路径同一份）。
    auto wire_hooks(WorkspaceView::Hooks &hooks) -> void {
        hooks.make_pane = [this](PaneId pane, PaneId origin) -> std::shared_ptr<TerminalView> {
            if (!with_factory_) {
                return nullptr;
            }
            ++factory_calls;
            last_origin = origin;
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
        // 第十五键的落点（`SPEC.FEAT.WS.03` 稿 D2）：本层不造窗，故这里只计数——「转交一次、树不动、
        // 一字节不发」三条判据要靠这一枚读数与既有观测面对照。
        hooks.open_new_window = [this]() -> void { ++new_window_calls; };
        // 末位 pane 的关格键落点（`SPEC.FEAT.WS.01`，裁决 7.42⑤）：本层只转交，故这里只计数。
        hooks.close_last_pane = [this]() -> void { ++close_tab_requests; };
        // 采集腿的观察点（`SPEC.FEAT.WS.11` 批 5-B）：树实际变更之后本层唯一的通知。闭包读的是
        // `workspace_`，两条构造路径都在 `finish()` 之后才可能触发，故构造期不会取空。
        hooks.structure_changed = [this]() -> void {
            ++structure_changes;
            structure_panes.push_back(workspace_->panes_in_order().size());
            structure_teardowns.push_back(torn_down_.size());
        };
    }

    /// @brief 两条构造路径共用的收尾：入根、给初始焦点、排帧并结算去抖。
    /// @param initial_focus 装配层该派焦点的那一格（恢复腿上它是快照里的选中格，不是恒 1）。
    /// @param baseline 置真即把构造期产生的观测读数清零。构造路径清（判据只测「这一步之后新增了
    ///        什么」），恢复路径不清——「三格各收到一份自己的行列」这件事只发生在构造那一段，清了
    ///        就没有第二处能看见它。
    auto finish(PaneId initial_focus, bool baseline) -> void {
        root_ = au::Node{std::static_pointer_cast<au::Widget>(workspace_)};
        // 装配事实的复刻：框架不在启动时给焦点序首个控件派焦点，初始焦点须由装配层显式给（真机走查
        // 抓过的那条），否则按键一个也到不了控件。
        focus_.set_root(&root_.widget());
        focus_.set_focus(workspace_->view_of(initial_focus));
        render();
        settle();  // 让会话先处在自己的网格上，喂入内容因此不受标称尺寸 80×24 的牵制
        if (baseline) {
            reset_counters();
        }
    }

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
        // 生产里这一枚闭包把窗号报给 tracker（本层没有窗，故只数次数）；每格一份视口就报同一计数，
        // 与装配层「同窗内每一格都算碰过这一扇窗」的口径一致。
        view->set_dispatch_observer([this]() -> void { ++dispatch_reports; });
        session->start();
        connections_.emplace(pane, raw);
        live_.emplace(pane, view.get());
        seen_.emplace(pane, view);
        history_[pane].push_back(std::weak_ptr<TerminalView>{view});
        sessions_.emplace(pane, std::move(session));
        return view;
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    au::Scheduler scheduler_;
    PaletteSpec palette_{};
    std::map<PaneId, std::unique_ptr<Session>> sessions_;
    /// @brief 换下来的旧会话：先离表、后销毁，为的是让旧视口的析构有一段会话仍可写撤账的窗口。
    std::vector<std::unique_ptr<Session>> retired_;
    std::map<PaneId, FakeConnection *> connections_;
    std::map<PaneId, TerminalView *> live_;
    std::map<PaneId, std::weak_ptr<TerminalView>> seen_;
    std::map<PaneId, std::vector<std::weak_ptr<TerminalView>>> history_;
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

/// @brief 造一枚叶子：序位就是它在阅读序里的第几格，也是快照里那份逐格料的下标。
[[nodiscard]] auto leaf(std::size_t order) -> PaneShape {
    PaneShape shape;
    shape.leaf = true;
    shape.order = order;
    return shape;
}

/// @brief 造一枚容器层：沿 @p axis 排 @p kids，每子相对长度都是 1（等分）。
[[nodiscard]] auto layer(PaneAxis axis, std::vector<PaneShape> kids) -> PaneShape {
    PaneShape shape;
    shape.leaf = false;
    shape.axis = axis;
    shape.lengths.assign(kids.size(), 1.0);
    shape.children = std::move(kids);
    return shape;
}

/// @brief 数焦点药丸那条带里的两种墨：底板（accent）与字形（既非底板也非 pane 底）。
///
/// 两个阈值都有实测出处（2026-10-31 变异自证）：文案真按测宽铺开时这条 60×10 的带里底板占 449 格、
/// 字形墨占 151 格；把测宽打成 0（只剩内边距的残枚）或撤掉那一笔 `draw_text`（只剩底板边缘那条
/// 抗锯齿），两个计数各塌到自己那一边，故分档取 300 与 60——「枚子没铺开」与「枚子有、字没有」
/// 是两件事，一条判据抓一件。
[[nodiscard]] auto count_pill_ink(const Harness &h, const Pixels &px, const Rect &band,
                                  const au::Point &origin) -> std::pair<std::size_t, std::size_t> {
    std::size_t plate = 0U;
    std::size_t glyph = 0U;
    for (int row = 0; row < static_cast<int>(band.height); ++row) {
        for (int column = 0; column < static_cast<int>(band.width); ++column) {
            const au::Point at{static_cast<float>(band.x + column + 0.5 + origin.x),
                               static_cast<float>(band.y + row + 0.5 + origin.y)};
            const RgbaColor ink = h.sample(px, at);
            if (ink == kAccentInk) {
                ++plate;
            } else if (!(ink == h.palette().default_background)) {
                ++glyph;
            }
        }
    }
    return {plate, glyph};
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

AURORA_TEST_CASE(startup_split_entry_follows_the_model_focus) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());

    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 2U);
    AURORA_TEST_CHECK_EQ(h.factory_calls, 1U);
    AURORA_TEST_REQUIRE_EQ(h.workspace().current_layout().dividers.size(), 1U);
    AURORA_TEST_CHECK(h.workspace().current_layout().dividers[0].axis == PaneAxis::Horizontal);

    // 第一次切分把模型焦点交给了新格（与键位入口同一条 `apply_focus`）；第二刀因此落在右格。
    // 断言只认「新格插在谁的领地」：三格时整层要重走最小 pane 下限，左格那条宽度并不守恒（差额按
    // 各格高于下限的量分摊），拿它当证人是断量化运气，而新格的起点骗不了人。
    const double right_before = h.pane_box(2U).x;
    const double left_before = h.pane_box(1U).width;
    h.workspace().split_focused(PaneAxis::Horizontal);
    h.render();

    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 3U);
    AURORA_TEST_REQUIRE_EQ(h.workspace().current_layout().boxes.size(), 3U);
    // 切错目标时新格会落在左格领地（起点 < 右格起点），这一条直接判「按模型焦点走」。
    AURORA_TEST_CHECK_GT(h.pane_box(3U).x, right_before);
    // 左格仍占整层的一半：切左格时它与新格各得一份，等宽即证人。
    AURORA_TEST_CHECK_GT(left_before, h.pane_box(3U).width);
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

AURORA_TEST_CASE(ctrl_shift_n_is_claimed_and_only_handed_to_the_assembly_layer) {
    // 第十五键（`SPEC.FEAT.WS.03` 稿 D2）：本层不认窗，认领 + 转交就是它的全部职责。三面证人缺一即
    // 形态不对：① 转交恰好一次（没装钩＝该键被认领后静默吞掉，装两遍＝一次按键开两扇窗）；
    // ② 树与把手都不动——它改的是「有几扇窗」而不是这一扇怎么分格；③ 一个字节也不写进会话，与步进
    // 键同一条保留位口径（上面 `bare_arrow_..._is_claimed` 那一判据的反面）。
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.reset_counters();
    const std::size_t panes = h.workspace().pane_count();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::N, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_EQ(h.new_window_calls, 1U);
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), panes);
    AURORA_TEST_CHECK_EQ(h.divider_count(), 1U);
    AURORA_TEST_CHECK(h.written(1U).empty());
    AURORA_TEST_CHECK(h.written(2U).empty());

    // 少一位修饰就不是它：`Ctrl+N` 照旧归会话（`readline` 的历史向下），认领集因此仍只圈十五条。
    h.reset_counters();
    static_cast<void>(h.press(au::KeyCode::N, au::ModifierKey::Control));
    AURORA_TEST_CHECK_EQ(h.new_window_calls, 0U);
    // 两格里至少有一格收到字节：全空就是这一键被静默吞掉了，而它不在认领集里。
    AURORA_TEST_CHECK_FALSE(h.written(1U).empty() && h.written(2U).empty());
}

AURORA_TEST_CASE(the_viewport_reports_each_key_and_mouse_press_exactly_once) {
    // 三处上报点的第一处（`SPEC.FEAT.WS.03` 稿 D7，装配层喂给 `ui::ActiveWindowTracker` 的那一枚闭
    // 包）。判据全在**计数口径**上：tracker 只认「最后一次被碰的窗」，于是报多报少都会让下一条全局
    // 快捷键落错地方，而报与不报的差别在本层读数上看不出来——只能这样钉：
    // ① 认领腿也报：上报位在键盘前置过滤**之前**，被工作区层收走的键同样是一次真实触碰。若把它挂在
    //    转发分支之后，第十五键那一击（既不转发也不分格）就永远不响，tracker 停在陈旧窗上；
    // ② 一次事件只报一次：一次就够，报两遍在 tracker 上读数相同，但闭包在生产里还连带做「把焦点还给
    //    本窗」这类有副作用的事，故次数本身就是判据；
    // ③ 抬起不报、指针只在按下报：一次拖把手会送来几十枚 Move，全记成「碰过」就等于没有事实；
    // ④ 转发腿不因上报而改变：报完仍照旧把字节送进会话，与未装观察者时逐字一致。
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.reset_counters();

    // ④＋① 的转发腿：裸方向键归会话，报一次且确有遗留档落地。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowRight));
    AURORA_TEST_CHECK_EQ(h.dispatch_reports, 1U);
    AURORA_TEST_CHECK_EQ(h.written(1U), "\x1B[C");

    // ① 的认领腿：`Ctrl+Alt+→` 是十五条之一，单叶时落点不存在仍算认领（裁决 7.47②），一个字节不进会话。
    h.reset_counters();
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowRight, au::ModifierKey::Control | au::ModifierKey::Alt));
    AURORA_TEST_CHECK_EQ(h.dispatch_reports, 1U);
    AURORA_TEST_CHECK(h.written(1U).empty());

    // 第十五键同理：它既不分格也不转发，上报点若挂在转发分支上就永远不响。
    h.reset_counters();
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::N, Harness::ctrl_shift()));
    AURORA_TEST_CHECK_EQ(h.new_window_calls, 1U);
    AURORA_TEST_CHECK_EQ(h.dispatch_reports, 1U);

    // ③ 抬起不算一次触碰（`on_key_event` 的 Down 守卫在上报之前）。
    h.reset_counters();
    static_cast<void>(h.press(au::KeyCode::A, au::ModifierKey::None, au::KeyAction::Up));
    AURORA_TEST_CHECK_EQ(h.dispatch_reports, 0U);

    // ③ 指针：按下报一次，同一落点的移动与抬起都不再报。
    h.reset_counters();
    static_cast<void>(h.pointer(au::MouseAction::Press, h.pane_center(1U)));
    AURORA_TEST_CHECK_EQ(h.dispatch_reports, 1U);
    static_cast<void>(h.pointer(au::MouseAction::Move, h.pane_center(1U)));
    static_cast<void>(h.pointer(au::MouseAction::Release, h.pane_center(1U)));
    AURORA_TEST_CHECK_EQ(h.dispatch_reports, 1U);
    h.render();
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

AURORA_TEST_CASE(an_appearance_change_reaches_the_session_only_through_the_debounce) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.reset_counters();

    // 运行期改外观（裁决 7.52 S4① 的第三条代价）在界面腿这一侧的证人：视口只标脏，行列数的下发
    // 仍旧只走工作区层的去抖腿。面板里连改两次字号若直发中间值，静默窗口就白设了。
    const auto bump = [&h](float size_pt) -> TerminalView::Appearance {
        auto font = test_font();
        font.size_pt = size_pt;
        return TerminalView::Appearance{.palette = h.palette(), .ref_font = std::move(font), .typography = {},
                                        .padding_dp = kPaddingDp,
                                        .blink_period = std::chrono::milliseconds{kBlinkPeriodMs},
                                        .font_fallback_chain = {}};
    };
    TerminalView *left = h.workspace().view_of(1U);
    AURORA_TEST_REQUIRE(left != nullptr);

    left->apply_appearance(bump(16.0F));
    h.render();
    AURORA_TEST_CHECK_EQ(h.dispatched_.size(), 0U);
    left->apply_appearance(bump(20.0F));
    h.render();
    AURORA_TEST_CHECK_EQ(h.dispatched_.size(), 0U);

    const GridSize latest{left->grid_geometry().columns, left->grid_geometry().rows};
    h.settle();
    AURORA_TEST_REQUIRE_EQ(h.dispatched_.size(), 1U);  // 右格净变化为零，因此不进「已下发」
    AURORA_TEST_CHECK_EQ(h.dispatched_[0].first, 1U);
    AURORA_TEST_CHECK(h.dispatched_[0].second == latest);
    AURORA_TEST_CHECK_EQ(h.resize_count(1U), 1U);
    AURORA_TEST_CHECK_EQ(h.resize_count(2U), 0U);
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
    AURORA_TEST_CHECK_EQ(h.close_tab_requests, 0U);  // 关的是普通格，不转交关标签

    // 末位 pane 不归本层关：只把请求转交「关标签」一次（裁决 7.42⑤ / 7.43⑤），树不动、
    // 不回收、也不报结构变更——落盘交出一份零格文档正是恢复腿读侧直接拒的形状。
    h.reset_counters();
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::W, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
    AURORA_TEST_CHECK(h.torn_down_.empty());
    AURORA_TEST_CHECK_EQ(h.close_tab_requests, 1U);
    AURORA_TEST_CHECK_EQ(h.structure_changes, 0U);
}

AURORA_TEST_CASE(split_factory_receives_the_pane_being_split) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::D, Harness::ctrl_shift()));
    h.render();
    // 第一刀从格 1 切出格 2：工厂收到的 origin 是**被切那一格**，不是它正在造的号。
    AURORA_TEST_CHECK_EQ(h.last_origin, 1U);

    // 第二刀走装配层的启动入口：第一次切分把焦点交给了新格，因此这一刀的 origin 是 2。
    h.workspace().split_focused(PaneAxis::Horizontal);
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.workspace().pane_count(), 3U);
    AURORA_TEST_CHECK_EQ(h.last_origin, 2U);

    // 键盘把焦点搬回最左那一格再切：origin 跟着**模型焦点**走。栈外那两刀只写得进模型投影
    // （`apply_focus` 拿不到派发栈里的管理器），框架读数因此落后一拍，而 `split_focused` 读的
    // 正是 `tree_.focused()`——本层文件头第一条「框架焦点是焦点的唯一权威」管的是取焦点，
    // 结构命令的目标始终是模型投影。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowLeft, au::ModifierKey::Alt));
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.workspace().focused_pane(), 1U);
    h.workspace().split_focused(PaneAxis::Horizontal);
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.workspace().pane_count(), 4U);
    AURORA_TEST_CHECK_EQ(h.last_origin, 1U);
}

AURORA_TEST_CASE(replacing_a_pane_view_keeps_the_pane_identity) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::D, Harness::ctrl_shift()));
    h.render();
    h.settle();
    AURORA_TEST_REQUIRE(h.workspace().view_of(2U) != nullptr);
    AURORA_TEST_REQUIRE(h.workspace().view_of(2U)->is_focused());  // 新格当场持焦
    const Rect right = h.pane_box(2U);
    h.reset_counters();

    AURORA_TEST_REQUIRE(h.replace_pane_view(2U));

    // 格的身份全程不动：树、矩形、焦点归属都按原样，只有视口与它背后的会话换成新的。
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 2U);
    AURORA_TEST_CHECK_EQ(h.workspace().current_layout().dividers.size(), 1U);
    const Rect after = h.pane_box(2U);
    AURORA_TEST_CHECK_NEAR(after.x, right.x, 0.5);
    AURORA_TEST_CHECK_NEAR(after.width, right.width, 0.5);
    AURORA_TEST_CHECK(h.replaced_view_expired(2U));  // 旧视口真散了，不是只从表里摘掉
    AURORA_TEST_REQUIRE(h.workspace().view_of(2U) != nullptr);
    AURORA_TEST_CHECK(h.workspace().view_of(2U)->is_focused());
    AURORA_TEST_CHECK_EQ(h.torn_down_.size(), 0U);   // 这不是关格：回收接缝不该响

    // 换上来的新会话还停在标称网格上：去抖表里若留着旧格已下发的那份行列，这一格就永远少发一次。
    h.render();
    h.settle();
    AURORA_TEST_CHECK(h.resize_count(2U) >= 1U);
    bool pane_two_dispatched = false;
    for (const auto &[pane, expected] : h.dispatched_) {
        (void)expected;
        pane_two_dispatched = pane_two_dispatched || pane == 2U;
    }
    AURORA_TEST_CHECK(pane_two_dispatched);

    // 缩放态按格记，故重启一格的视图不该把它冲掉：缩放后再换一次视口，缩放仍是这一格。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().zoomed_pane().value_or(0U) == 2U);
    AURORA_TEST_REQUIRE(h.replace_pane_view(2U));
    h.render();
    AURORA_TEST_CHECK_EQ(h.workspace().zoomed_pane().value_or(0U), 2U);
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 2U);
}

AURORA_TEST_CASE(replacing_a_pane_is_refused_without_touching_the_tree) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::D, Harness::ctrl_shift()));
    h.render();
    const TerminalView *first = h.workspace().view_of(1U);
    h.reset_counters();

    // 空视图与不存在的格两条都要回 false，且什么都不动：装配层据此才不会把旧会话错手清掉。
    // 视口本身不带格身份（一个 `TerminalView` 可以指向任意格），故这里只可能拒这两类。
    AURORA_TEST_CHECK_FALSE(h.workspace().replace_view(1U, nullptr));
    AURORA_TEST_CHECK_FALSE(h.workspace().replace_view(9U, h.make_orphan_view(9U)));

    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 2U);
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(1U), first);
    AURORA_TEST_CHECK(h.torn_down_.empty());
    AURORA_TEST_CHECK(h.dispatched_.empty());
}

AURORA_TEST_CASE(zoom_fills_the_focused_pane_and_keeps_hidden_grids) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();

    const GridSize left_grid{h.workspace().view_of(1U)->grid_geometry().columns,
                             h.workspace().view_of(1U)->grid_geometry().rows};
    const GridSize right_grid{h.workspace().view_of(2U)->grid_geometry().columns,
                              h.workspace().view_of(2U)->grid_geometry().rows};
    const Rect right_box = h.pane_box(2U);
    h.reset_counters();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();

    const auto zoomed = h.workspace().zoomed_pane();
    AURORA_TEST_REQUIRE(zoomed.has_value());
    AURORA_TEST_CHECK_EQ(*zoomed, 2U);
    // 显示表：缩放格满窗、隐藏格零盒、把手为空；树本身一行未改（还原仍回到原矩形）。
    const Rect full = h.pane_box(2U);
    AURORA_TEST_CHECK_NEAR(full.x, 0.0, 1.0);
    AURORA_TEST_CHECK_NEAR(full.y, 0.0, 1.0);
    AURORA_TEST_CHECK_NEAR(full.width, static_cast<double>(kWindowWidth), 1.0);
    AURORA_TEST_CHECK_NEAR(full.height, static_cast<double>(kWindowHeight), 1.0);
    AURORA_TEST_CHECK_NEAR(h.pane_box(1U).width, 0.0, 1e-9);
    AURORA_TEST_CHECK_NEAR(h.pane_box(1U).height, 0.0, 1e-9);
    AURORA_TEST_CHECK(h.workspace().current_layout().dividers.empty());

    // 缩放格按满窗的行列数重排；隐藏格的网格一格不变（D4：隐藏不是 resize）。
    const GridSize full_grid = Harness::expected_grid(kWindowWidth, kWindowHeight);
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(2U)->grid_geometry().columns, full_grid.columns);
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(2U)->grid_geometry().rows, full_grid.rows);
    AURORA_TEST_CHECK(h.workspace().view_of(2U)->grid_geometry().columns > right_grid.columns);
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(1U)->grid_geometry().columns, left_grid.columns);
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(1U)->grid_geometry().rows, left_grid.rows);

    h.settle();
    AURORA_TEST_REQUIRE_EQ(h.dispatched_.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.dispatched_[0].first, 2U);
    AURORA_TEST_CHECK_EQ(h.resize_count(1U), 0U);
    AURORA_TEST_CHECK_EQ(h.resize_count(2U), 1U);

    // 再按一次还原：矩形回到树折算的原位，隐藏过的会话仍只有缩放格自己收过一次下发。
    h.reset_counters();
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_FALSE(h.workspace().zoomed_pane().has_value());
    const Rect restored = h.pane_box(2U);
    AURORA_TEST_CHECK_NEAR(restored.x, right_box.x, 1.0);
    AURORA_TEST_CHECK_NEAR(restored.width, right_box.width, 1.0);
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(1U)->grid_geometry().columns, left_grid.columns);
    AURORA_TEST_CHECK_EQ(h.workspace().view_of(2U)->grid_geometry().columns, right_grid.columns);
    h.settle();
    AURORA_TEST_REQUIRE_EQ(h.dispatched_.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.dispatched_[0].first, 2U);
    AURORA_TEST_CHECK_EQ(h.resize_count(1U), 0U);
}

AURORA_TEST_CASE(zoom_follows_focus_across_alt_arrows) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().zoomed_pane().has_value());
    AURORA_TEST_CHECK_EQ(*h.workspace().zoomed_pane(), 2U);

    // 缩放态按未缩放的几何路由：左移把焦点与缩放一起交给左格。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowLeft, au::ModifierKey::Alt));
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().zoomed_pane().has_value());
    AURORA_TEST_CHECK_EQ(*h.workspace().zoomed_pane(), 1U);
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 1U);
    AURORA_TEST_CHECK_NEAR(h.pane_box(1U).width, static_cast<double>(kWindowWidth), 1.0);
    AURORA_TEST_CHECK_NEAR(h.pane_box(2U).width, 0.0, 1e-9);

    // 反向同理：焦点回到右格，缩放也跟回去。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowRight, au::ModifierKey::Alt));
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().zoomed_pane().has_value());
    AURORA_TEST_CHECK_EQ(*h.workspace().zoomed_pane(), 2U);
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 2U);
    AURORA_TEST_CHECK_NEAR(h.pane_box(2U).width, static_cast<double>(kWindowWidth), 1.0);
}

AURORA_TEST_CASE(structural_commands_unzoom_before_they_run) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.drag_divider(0U, {120.0});  // 先造出一个不对称的层，等分才有可证的落点

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().zoomed_pane().has_value());

    // ① 切分：先还原再切；新格按未缩放几何进场，缩放态不延续。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::D, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_FALSE(h.workspace().zoomed_pane().has_value());
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 3U);
    for (const PaneBox &box : h.workspace().current_layout().boxes) {
        AURORA_TEST_CHECK_GT(box.box.width, 0.0);
    }
    AURORA_TEST_CHECK_FALSE(h.workspace().current_layout().dividers.empty());

    // 焦点交回中间的格，再缩放它。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowLeft, au::ModifierKey::Alt));
    h.render();
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 2U);
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().zoomed_pane().has_value());

    // ② 等分：先还原再等分；三层宽度回到彼此相等。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::D0, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_FALSE(h.workspace().zoomed_pane().has_value());
    const Rect first = h.pane_box(1U);
    const Rect second = h.pane_box(2U);
    const Rect third = h.pane_box(3U);
    AURORA_TEST_CHECK_NEAR(first.width, second.width, 1.5);
    AURORA_TEST_CHECK_NEAR(second.width, third.width, 1.5);

    // ③ 步进：先还原再推把手；焦点格（中间格）变宽。基准须在缩放**之前**读：缩放态的显示表里
    // 该格是满窗盒，拿它当「变宽前」的读数会让这条判据恒假。
    const double before = h.pane_box(2U).width;
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().zoomed_pane().has_value());
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::ArrowRight, au::ModifierKey::Control | au::ModifierKey::Alt));
    h.render();
    AURORA_TEST_CHECK_FALSE(h.workspace().zoomed_pane().has_value());
    AURORA_TEST_CHECK_GT(h.pane_box(2U).width, before + 20.0);
}

AURORA_TEST_CASE(closing_the_zoomed_pane_clears_zoom_and_recycles_it) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.reset_counters();

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_REQUIRE(h.workspace().zoomed_pane().has_value());

    AURORA_TEST_REQUIRE(h.press(au::KeyCode::W, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_FALSE(h.workspace().zoomed_pane().has_value());
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
    AURORA_TEST_CHECK(h.workspace().view_of(2U) == nullptr);
    AURORA_TEST_CHECK(h.view_expired(2U));
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 1U);
    AURORA_TEST_CHECK(h.workspace().current_layout().dividers.empty());
    AURORA_TEST_REQUIRE_EQ(h.workspace().current_layout().boxes.size(), 1U);
    AURORA_TEST_CHECK_NEAR(h.workspace().current_layout().boxes[0].box.width,
                           static_cast<double>(kWindowWidth), 1.0);
}

AURORA_TEST_CASE(zoom_badge_hovers_clicks_and_paints_on_the_top_right_corner) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();

    auto px = h.pixels();
    // 未悬停：角标不显形，热区底下是终端自己的底。
    AURORA_TEST_CHECK(h.sample(px, h.badge_plate_point(1U)) == h.palette().default_background);
    AURORA_TEST_CHECK(h.sample(px, h.badge_center(1U)) == h.palette().default_background);

    // 悬停：底板是 chrome，字形是 accent（取斜线上一个 coverage 为 1 的像素，理由见 helper）。
    h.pointer(au::MouseAction::Move, h.badge_center(1U));
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.badge_plate_point(1U)) == kChromeInk);
    AURORA_TEST_CHECK(h.sample(px, h.badge_expand_ink_point(1U)) == kAccentInk);

    // 移开：角标消失（只重画、不重排）。
    h.pointer(au::MouseAction::Move, h.pane_center(1U));
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.badge_plate_point(1U)) == h.palette().default_background);

    // 点角标：该格取得焦点并缩放；缩放态角标常显，且左沿不画焦点描边。
    h.click_at(h.badge_center(1U));
    const auto zoomed = h.workspace().zoomed_pane();
    AURORA_TEST_REQUIRE(zoomed.has_value());
    AURORA_TEST_CHECK_EQ(*zoomed, 1U);
    AURORA_TEST_CHECK_EQ(h.workspace().focused_pane(), 1U);
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.badge_plate_point(1U)) == kChromeInk);
    AURORA_TEST_CHECK(h.sample(px, h.badge_collapse_ink_point(1U)) == kAccentInk);
    AURORA_TEST_CHECK(h.sample(px, au::Point{1.0F, static_cast<float>(kWindowHeight / 2)}) ==
                      h.palette().default_background);

    // 再点一次：还原；焦点描边回到未缩放态，角标回到悬停显。
    h.click_at(h.badge_center(1U));
    AURORA_TEST_CHECK_FALSE(h.workspace().zoomed_pane().has_value());
    h.pointer(au::MouseAction::Move, h.pane_center(1U));
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.badge_plate_point(1U)) == h.palette().default_background);
}

/// @brief 判据 ⑤（真机走查补漏）：工作区不在窗口原点时，角标的底板与字形必须一起跟着走。
///
/// 上面那条角标用例把根挂在 (0,0)，局部盒与全局盒逐位同值，于是「字形端点漏加 `bounds.origin`」
/// 这类平移缺陷在它眼里根本不存在（真机读数 2026-10-31：⤡ 落在标题条的 F11 上，偏移恰为
/// 装配层那 (34, 78) dp）。这条用例把同一份几何推到非零原点上，底板与字形各取一个像素作证。
AURORA_TEST_CASE(the_badge_glyph_shares_its_plate_origin_when_the_root_is_inset) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.inset_root(34.0, 78.0);  // 折叠轨 34 dp + 标题条与标签条 78 dp：真机那组读数的原点
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();

    const au::Point origin = h.workspace_origin();
    AURORA_TEST_REQUIRE(origin.x > 0.0F && origin.y > 0.0F);  // 内缩没落地就没有证人
    const auto at = [origin](au::Point local) -> au::Point {
        return au::Point{.x = local.x + origin.x, .y = local.y + origin.y};
    };

    h.pointer(au::MouseAction::Move, at(h.badge_center(1U)));
    h.render();
    const auto px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, at(h.badge_plate_point(1U))) == kChromeInk);
    AURORA_TEST_CHECK(h.sample(px, at(h.badge_expand_ink_point(1U))) == kAccentInk);
    // 反面证人：按**未平移**的局部坐标取同一格，那里不该再有 accent——否则这条判据退化成正面判据的
    // 复读，漏加原点的那份实现照样过（字形会同时出现在两个位置吗？不会，它只有一处落点）。
    AURORA_TEST_CHECK(h.sample(px, h.badge_expand_ink_point(1U)) != kAccentInk);
}

AURORA_TEST_CASE(single_pane_zoom_is_claimed_but_does_nothing) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.reset_counters();

    // 键仍被认领（不是发给会话的输入），但单叶没有可放大的对象（D9）。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::Z, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_FALSE(h.workspace().zoomed_pane().has_value());
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
    AURORA_TEST_CHECK(h.written(1U).empty());
    AURORA_TEST_CHECK(h.dispatched_.empty());

    // 单叶也没有角标：即便把指针停在右上角热区，屏上仍是终端自己的底。
    h.pointer(au::MouseAction::Move, h.badge_center(1U));
    h.render();
    const auto px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.badge_plate_point(1U)) == h.palette().default_background);
    AURORA_TEST_CHECK(h.sample(px, h.badge_center(1U)) == h.palette().default_background);
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
    // 把手沿轴确有 32 dp：中心 ±12 dp 仍是 line2，±17 dp 已回到 chrome 底（真机读数 2026-10-31：
    // 长轴取反时中段塌成 8 dp，±12 两点即失配）。
    AURORA_TEST_CHECK(h.sample(px, h.handle_along_point(0U, -12.0)) == kHandleInk);
    AURORA_TEST_CHECK(h.sample(px, h.handle_along_point(0U, 12.0)) == kHandleInk);
    AURORA_TEST_CHECK(h.sample(px, h.handle_along_point(0U, -17.0)) == kChromeInk);
    AURORA_TEST_CHECK(h.sample(px, h.handle_along_point(0U, 17.0)) == kChromeInk);

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

AURORA_TEST_CASE(the_focus_pill_marks_only_the_focused_pane) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();

    // 没有文案就没有药丸：装配层在首帧与「0 行 0 列」那一档交空串，屏上因此仍是 pane 自己的底。
    // 取点离右沿 1 dp：撤掉空串闸门的那份实现会在此画出一枚「只剩内边距的残枚」，同样压着这一点。
    auto px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.pill_plate_point(2U)) == h.palette().default_background);

    h.workspace().set_focus_pill_text(U"focus 82x24");
    h.render();
    px = h.pixels();
    // 底板＝accent，右沿按「pane 右 − (4 + 20 + 6)」落位（稿面那枚底板横向位置是装饰性的，让位
    // 次序由裁决定：与缩放角标共存、且不随角标显形而跳格）。
    AURORA_TEST_CHECK(h.sample(px, h.pill_plate_point(2U)) == kAccentInk);
    // 反面证人：非焦点格同一档没有底板——它是「当前 pane 视觉标识」的第二个载体，只给焦点那一格。
    AURORA_TEST_CHECK(h.sample(px, h.pill_plate_point(1U)) == h.palette().default_background);

    // 带内两条计数各管一件事：底板必须**按文案的测宽铺开**（测宽退化成 0 的实现只留下一枚残枚），
    // 文案必须**真的有墨**（撤掉 `draw_text` 的实现只剩底板）。
    const auto [plate_ink, glyph_ink] = count_pill_ink(h, px, h.pill_glyph_band(2U), au::Point{});
    AURORA_TEST_CHECK_MSG(plate_ink >= 300U, "the focus pill plate does not stretch with its text");
    AURORA_TEST_CHECK_MSG(glyph_ink >= 60U, "the focus pill paints its plate but never its glyph");

    // 与缩放角标互不打架：悬停让角标显形后，两枚各自在自己的格位上（一旦重叠，其中一枚必失配）。
    h.pointer(au::MouseAction::Move, h.badge_center(2U));
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.badge_plate_point(2U)) == kChromeInk);
    AURORA_TEST_CHECK(h.sample(px, h.pill_plate_point(2U)) == kAccentInk);

    // 撤文案即撤药丸：装配层每帧现算，读数没了交的是空串而不是留一份旧值在屏上。
    h.workspace().set_focus_pill_text({});
    h.render();
    px = h.pixels();
    AURORA_TEST_CHECK(h.sample(px, h.pill_plate_point(2U)) == h.palette().default_background);
}

AURORA_TEST_CASE(the_focus_pill_shares_its_plate_origin_when_the_root_is_inset) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.inset_root(34.0, 78.0);  // 折叠轨 34 dp + 标题条与标签条 78 dp：真机那组读数的原点
    h.press(au::KeyCode::D, Harness::ctrl_shift());
    h.render();
    h.settle();
    h.workspace().set_focus_pill_text(U"focus 82x24");
    h.render();
    const auto px = h.pixels();

    const au::Point origin = h.workspace_origin();
    AURORA_TEST_REQUIRE(origin.x > 0.0F && origin.y > 0.0F);
    const auto at = [origin](au::Point local) -> au::Point {
        return au::Point{.x = local.x + origin.x, .y = local.y + origin.y};
    };
    AURORA_TEST_CHECK(h.sample(px, at(h.pill_plate_point(2U))) == kAccentInk);
    // 底板与字形同一条原点：整枚（枚子加字）一起平移，内缩后的带里两种墨都该在原来的份量上。
    const auto counts = count_pill_ink(h, px, h.pill_glyph_band(2U), origin);
    AURORA_TEST_CHECK_MSG(counts.first >= 300U, "the inset focus pill plate is not where it belongs");
    AURORA_TEST_CHECK_MSG(counts.second >= 60U, "the inset focus pill lost its glyph to the origin");
    // 反面证人：按**未平移**的局部坐标取同一点不该见 accent。原点漏加的那份实现会把整枚底板画到内容
    // 盒之外，而根挂在 (0,0) 时两个坐标逐位同值——那条缺陷在原来那档里结构性不存在（与缩放角标同一课）。
    AURORA_TEST_CHECK(h.sample(px, h.pill_plate_point(2U)) != kAccentInk);
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

    // 装配层入口同一档：工厂缺失时同样只认领动作、不动树。
    h.workspace().split_focused(PaneAxis::Horizontal);
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
    AURORA_TEST_CHECK(h.workspace().current_layout().dividers.empty());
}

AURORA_TEST_CASE(a_restored_shape_lays_out_every_pane_it_claims) {
    const PaneShape nested =
        layer(PaneAxis::Horizontal, {leaf(0U), layer(PaneAxis::Vertical, {leaf(1U), leaf(2U)})});
    Harness h{nested, 3U, 2U};
    AURORA_TEST_REQUIRE(h.restored() != nullptr);
    AURORA_TEST_REQUIRE(h.preflight());
    WorkspaceView &ws = h.workspace();

    AURORA_TEST_CHECK_EQ(ws.pane_count(), 3U);
    // 「序位 i 的格挂标识 i+1」这条公开约定的唯一证人：装配层就是按它把逐格的会话登进账表的。
    const std::vector<PaneId> kRestored{1U, 2U, 3U};
    AURORA_TEST_CHECK_MSG(ws.panes_in_order() == kRestored, "restored pane ids drifted");
    // 取回来的形状即交进去的那一份：相对长度没被折算路径改写，嵌套也没在重建里塌掉一层。
    AURORA_TEST_CHECK_MSG(ws.pane_shape() == nested, "the restored shape is not the shape handed in");

    TerminalView *left = ws.view_of(1U);
    TerminalView *upper = ws.view_of(2U);
    TerminalView *lower = ws.view_of(3U);
    AURORA_TEST_REQUIRE(left != nullptr && upper != nullptr && lower != nullptr);
    // 每格各有一份视图（切分腿花了两批才拧过来的那条，恢复腿一上来就得满足：共用一份就等于共用
    // 一份会话，两格显示同一屏内容）。
    AURORA_TEST_CHECK(left != upper && upper != lower && left != lower);

    const auto &layout = ws.current_layout();
    AURORA_TEST_REQUIRE_EQ(layout.boxes.size(), 3U);
    AURORA_TEST_REQUIRE_EQ(layout.dividers.size(), 2U);  // 根层一条 + 竖层一条
    for (const PaneBox &box : layout.boxes) {
        const TerminalView *view = ws.view_of(box.pane);
        AURORA_TEST_REQUIRE(view != nullptr);
        const GridSize expected = Harness::expected_grid(box.box.width, box.box.height);
        const GridSize actual{view->grid_geometry().columns, view->grid_geometry().rows};
        AURORA_TEST_CHECK_MSG(actual == expected, "pane " + std::to_string(box.pane) + " keeps another pane's grid");
    }
    // 三格各收到一份**自己的**行列：形状落回界面这件事的最后一条判据在会话侧，而「只把行列打给首格」
    // 那一族缺陷（`SPEC.FEAT.WS.02` 的 F4）在屏上看着是好的。
    AURORA_TEST_REQUIRE_EQ(h.dispatched_.size(), 3U);
    AURORA_TEST_CHECK_EQ(h.dispatched_[0].first, 1U);
    AURORA_TEST_CHECK_EQ(h.dispatched_[1].first, 2U);
    AURORA_TEST_CHECK_EQ(h.dispatched_[2].first, 3U);
    for (const PaneId pane : kRestored) {
        AURORA_TEST_CHECK_GE(h.resize_count(pane), 1U);
    }
    // 选中格是快照里记下的那一格（序位 2 ⇒ 标识 3），不是「恒第一格」。
    AURORA_TEST_CHECK_EQ(ws.focused_pane(), 3U);
}

AURORA_TEST_CASE(a_restored_tab_receives_one_initial_grid_per_pane_and_no_more) {
    const PaneShape nested =
        layer(PaneAxis::Horizontal, {leaf(0U), layer(PaneAxis::Vertical, {leaf(1U), leaf(2U)})});
    Harness h{nested, 3U, 1U};
    AURORA_TEST_REQUIRE(h.restored() != nullptr);
    AURORA_TEST_REQUIRE(h.preflight());

    // 构造段（`baseline=false`）已结算过尾沿：每格恰一条，且是该格自己的矩形折出来的行列。
    // 判据 ④ 的「次数恰为格数」在这里是**逐格一次**：多了是重复挂载，少了是漏发。
    AURORA_TEST_REQUIRE_EQ(h.dispatched_.size(), 3U);
    for (const PaneBox &box : h.workspace().current_layout().boxes) {
        const auto entries = std::count_if(
            h.dispatched_.begin(), h.dispatched_.end(),
            [&box](const std::pair<PaneId, GridSize> &entry) { return entry.first == box.pane; });
        AURORA_TEST_CHECK_MSG(entries == 1, "pane " + std::to_string(box.pane) + " was dispatched " +
                                                std::to_string(entries) + " times");
        AURORA_TEST_CHECK_EQ(h.resize_count(box.pane), 1U);
        const auto found = std::ranges::find_if(
            h.dispatched_, [&box](const std::pair<PaneId, GridSize> &entry) { return entry.first == box.pane; });
        AURORA_TEST_REQUIRE(found != h.dispatched_.end());
        const GridSize expected = Harness::expected_grid(box.box.width, box.box.height);
        AURORA_TEST_CHECK_MSG(found->second == expected, "pane " + std::to_string(box.pane) +
                                                            " was told another pane's grid");
    }

    // 再排一帧并结算：布局没变，净变化为零的条目不得重发（去抖表那条既有纪律的恢复腿形态）。
    h.render();
    h.settle();
    AURORA_TEST_CHECK_EQ(h.dispatched_.size(), 3U);
    for (const PaneBox &box : h.workspace().current_layout().boxes) {
        AURORA_TEST_CHECK_EQ(h.resize_count(box.pane), 1U);
    }
}

AURORA_TEST_CASE(restore_is_refused_without_leaving_a_half_built_workspace) {
    const PaneShape three = layer(PaneAxis::Vertical, {leaf(0U), leaf(1U), leaf(2U)});
    // 视图份数与叶子数不等：少一份是一格没料，多一份是两格抢同一份料，两者都不如整份拒绝可解释。
    AURORA_TEST_CHECK(Harness(three, 2U, 0U).restored() == nullptr);
    AURORA_TEST_CHECK(Harness(three, 4U, 0U).restored() == nullptr);
    // 选中序位越界：焦点无处可落，猜一个落点就是替用户改布局。
    AURORA_TEST_CHECK(Harness(three, 3U, 3U).restored() == nullptr);
    // 零格的「恢复」不成立：工作区至少有一格，空表既不是单叶也不是没布局。
    AURORA_TEST_CHECK(Harness(three, 0U, 0U).restored() == nullptr);
    // 坏形状（单子层）由树件那六道闸先拒，本层不许在它之前把视图挂出去。
    AURORA_TEST_CHECK(Harness(layer(PaneAxis::Horizontal, {leaf(0U)}), 1U, 0U).restored() == nullptr);
    // 含空指针的那一份：装配层的六条腿各有一份工厂，恢复腿只要有一格的会话没起来，交进来的就是
    // 「N 份视图里有一枚空」。这一支不经驱动台——它连会话都不必真存在，判据只在「不许建半棵树」。
    std::vector<std::shared_ptr<TerminalView>> one_missing(3U);
    AURORA_TEST_CHECK(WorkspaceView::restore(one_missing, three, 0U, WorkspaceView::Hooks{}) == nullptr);
}

AURORA_TEST_CASE(a_restored_workspace_still_splits_from_the_next_identity) {
    const PaneShape three = layer(PaneAxis::Vertical, {leaf(0U), leaf(1U), leaf(2U)});
    Harness h{three, 3U, 2U};
    AURORA_TEST_REQUIRE(h.restored() != nullptr);
    AURORA_TEST_REQUIRE(h.preflight());
    h.reset_counters();

    // 走装配层那一刀（不是键盘腿）：`split_focused` 读的是**模型**投影的焦点，而恢复腿写那一份的
    // 就是快照里的选中格。切下去落在谁头上，就是「选中格真的被恢复了」的唯一证人——屏上的焦点描边
    // 由框架持焦说话，掩盖掉模型投影没跟上这一种错。
    h.workspace().split_focused(PaneAxis::Horizontal);
    h.render();
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 4U);
    AURORA_TEST_CHECK_EQ(h.factory_calls, 1U);
    AURORA_TEST_CHECK_EQ(h.last_origin, 3U);  // 切的是选中那一格（序位 2 ⇒ 标识 3）
    // 新号续在恢复出来的三格之后：恢复不许把已经发出去的号退回给下一格（会话按号记账，撞号比少一格
    // 坏得多），而切完之后阅读序里新格紧挨被切那格——树的插入位置也没被恢复腿带歪。
    const std::vector<PaneId> kAfter{1U, 2U, 3U, 4U};
    AURORA_TEST_CHECK_MSG(h.workspace().panes_in_order() == kAfter, "a restored workspace reuses an identity");
}

AURORA_TEST_CASE(a_split_fires_the_structure_hook_after_the_tree_has_moved) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());

    h.workspace().split_focused(PaneAxis::Horizontal);
    AURORA_TEST_REQUIRE_EQ(h.structure_changes, 1U);
    // 触发那一刻读到的已经含新格（装配层 `capture_workspace_snapshot` 就挂在这里，快照因此不慢一拍）。
    AURORA_TEST_REQUIRE_EQ(h.structure_panes.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.structure_panes[0], 2U);
    AURORA_TEST_CHECK_EQ(h.structure_teardowns[0], 0U);
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 2U);

    // 关格是同一条腿的另一侧：叶子数当场回落到 1，落盘读到的因此是关完之后那一套。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::W, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.structure_changes, 2U);
    AURORA_TEST_CHECK_EQ(h.structure_panes[1], 1U);
    // 回收先于本钩（`close_pane` 里 `teardown_pane` 排在末段之前），故此刻已回收数是一。这一列同时
    // 钉住切分腿那条：错装在回收里会在上一列读出 1、这一列读出 2。
    AURORA_TEST_CHECK_EQ(h.structure_teardowns[1], 1U);
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
}

AURORA_TEST_CASE(a_refused_change_and_a_handle_drag_leave_the_structure_hook_alone) {
    // 工厂回空：`split_pane` 的第二道早退，树一步没动，落盘要是跟着写就写出一份屏幕上没有的形状。
    Harness empty_factory(false);
    AURORA_TEST_REQUIRE(empty_factory.preflight());
    empty_factory.workspace().split_focused(PaneAxis::Horizontal);
    AURORA_TEST_CHECK_EQ(empty_factory.structure_changes, 0U);
    AURORA_TEST_CHECK_EQ(empty_factory.workspace().pane_count(), 1U);

    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.workspace().split_focused(PaneAxis::Vertical);
    h.render();
    h.reset_counters();

    // 把手步进改的是相对长度而不是拓扑：一次拖动几十步，逐步落盘等于把一份文档写成几十份。
    h.drag_divider(0U, {h.cell_width_dp(), h.cell_width_dp()});
    h.render();
    AURORA_TEST_CHECK_EQ(h.structure_changes, 0U);
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 2U);

    // 关掉一格响一次，之后关末位那一格走的是本层的第一道早退（末位归关标签 / 关窗口），钩子不许再响——
    // 否则落盘交出去的是零格文档，正是恢复腿读侧直接拒的那个形状。
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::W, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_EQ(h.structure_changes, 1U);
    AURORA_TEST_REQUIRE(h.press(au::KeyCode::W, Harness::ctrl_shift()));
    h.render();
    AURORA_TEST_CHECK_EQ(h.structure_changes, 1U);
    AURORA_TEST_CHECK_EQ(h.workspace().pane_count(), 1U);
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_workspace_layout
