/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（悬停层、两条打开入口、确认框）+ src/ui/url.cpp + src/ui/right_click.cpp
/// 测试说明: 以无头窗口 + 真实指针派发驱动「屏上字节 → `ui::find_link_at` → 悬停层 / Ctrl+左击 /
///           右键第四条 → `ui::plan_link_open` → 确认一问 → `open_url` 接缝」这条链
///           （`SPEC.FEAT.INTERACT.05` 的界面腿，稿 D4①／D7①／D11①／D12①／D13①）。断的是**链路**
///           而非识别件与决策件本身（两者各有 `utest_ui_url` / `utest_right_click` 逐条覆盖），
///           值得单独证的有八件事：
///           ① 悬停两层同现同隐（游程 accent 线 + 左下目标条），条上显示的恒是**目标全文**而非屏上
///              文字（判据 1／4，OSC 8 那一档的证人）；
///           ② 白名单外的一串整条链路 inert：无线、无条、Ctrl 击不响（判据 5）；
///           ③ Ctrl+左击只在**这一击归本地**时才响，且**不碰选区**（D13①：顺手选词会让随后的
///              copy-on-select 把 URL 塞进剪贴板，那是用户没要的动作）；
///           ④ 三档策略各自的问法（`never` 不问 / `ask_once` 同域第二次不问 / `ask_always` 每次都问），
///              且「记住」写进名单的那枚域与判同域用的是同一枚（D9①：同 host 不同端口仍要问）；
///           ⑤ 「打开链接」进菜单的次序与命中判定（判据 6：按落点不按选区）；
///           ⑥ 上报档让位（判据 7）：`?1000` 与 `?1003` 两档各断「无悬停层、Ctrl 击不响、菜单条目
///              仍在」三件事；
///           ⑦ 提示条的两条非模态纪律：装不下**整条不画**而不截断（判据 3 前半），且它一枚命中盒都
///              不交——条下那一格照常起选区（后半）；
///           ⑧ 生产确认框的三枚按钮**经真实命中链点到**（G26 回货的接货证人）：取消不开、仅本次不
///              开账、记住即写名单；点遮罩既不关闭也不打开（F12）。
///           生产打开腿**永不参与**：`Presentation::open_url` 刻意没有默认实现（稿 D11①／F15 那次真开
///           浏览器的一手事故），本文件每一处都装替身，于是「自动化不开浏览器」由构造保证而非纪律。
///           悬停线用「带内等于 accent 的像素数」判而不是帧间差分：底色与字形全取自缺省色板（各通道
///           皆 0），accent 是 (189,147,249)，带内出现这个值只可能是这一层画的。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/render/font_engine.h"
#include "aurora/widget/button.h"
#include "borealis/grid/storage.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/cell_layout.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/right_click.h"
#include "borealis/ui/url.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/settings_panel.h"
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_url_link {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::CellPixels;
using borealis::ui::GridGeometry;
using borealis::ui::LinkConfirmPolicy;
using borealis::ui::LinkHit;
using borealis::ui::LinkSource;
using borealis::ui::LogicalSize;
using borealis::ui::PaletteSpec;
using borealis::ui::Rect;
using borealis::ui::TerminalView;
using borealis::ui::TrustedLinkHosts;

/// @brief 确认框的入参与答话是**控件的嵌套类型**（只服务这条 UI 腿，不进 `ui::` 命名空间）。
using LinkOpenAnswer = TerminalView::LinkOpenAnswer;
using LinkOpenRequest = TerminalView::LinkOpenRequest;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 40;
constexpr float kPaddingDp = 4.0F;
/// @brief 窗口档：本机 Headless 的 14pt 单宽格实测 **11x22 dp**（诊断量过），故 640x440 给到
///        57 列 x 19 行——「第 40 列还落在格里」与「一段 20 列的游程整条在可见区」两条前置
///        同时成立才有着落，而 `preflight` 的门槛就是按这两枚数写的，故这里是承重常量。
constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 440;
/// @brief 窄窗档：够格放「docs」四个字，却装不下一条长 URI——「让位不画」那一档靠它成立。
constexpr int kNarrowWidth = 140;
/// @brief 提示条的横向内缩（与 `kLinkNoticeInsetDp` 同值）：判据 3 那句「贴左下」的落点。
constexpr double kExpectedInset = 8.0;

auto width_policy = std::make_shared<UnicodeWidthPolicy>();

/// @brief 传输连接替身：记录会话写出的字节，并可主动投递字节（本用例里投递线程即读线程）。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override {
        written.insert(written.end(), bytes.begin(), bytes.end());
    }

    auto resize(Size) -> void override {}

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

    std::vector<std::byte> written;

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

[[nodiscard]] auto test_font() -> au::Font {
    return au::Font{.family = "Cascadia Code", .size_pt = 14.0F, .weight = 400};
}

/// @brief 交互口径：只装链接那一族，其余取缺省。
///
/// 白名单**必须显式给**：`LinkScanOptions::schemes` 缺省为空即「一切不可点」，本文件里「什么都没命中」
/// 若是这么来的，整份用例就成了空转证人（稿判据 5 那句「缺省串由装配层拆好交进来」在这里的体现）。
[[nodiscard]] auto link_options(LinkConfirmPolicy osc8 = LinkConfirmPolicy::AskOnce,
                                LinkConfirmPolicy heuristic = LinkConfirmPolicy::AskOnce,
                                std::string_view csv = "http,https") -> TerminalView::InteractionOptions {
    TerminalView::InteractionOptions options{};
    options.link = TerminalView::LinkOptions{
        .scan = borealis::ui::LinkScanOptions{.schemes = borealis::ui::parse_link_schemes(csv)},
        .osc8 = osc8,
        .heuristic = heuristic,
    };
    return options;
}

/// @brief 驱动台：会话 + 替身连接 + 视口控件 + 浮层宿主 + 无头窗口 + 名单本体 + 两条打开接缝的替身。
class Harness {
  public:
    explicit Harness(const TerminalView::InteractionOptions &options = link_options(), bool with_host = true,
                     bool stub_link = true, int width = kWindowWidth)
        : window_(make_window(width)),
          window_width_(width),
          session_(std::make_unique<Session>(own_connection(), kNominalSize, kScrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, PaletteSpec{}, test_font(), ui::Typography{}, kPaddingDp,
                                               std::chrono::milliseconds{500}, options)),
          host_(std::make_shared<au::OverlayHost>(
              au::Node{std::static_pointer_cast<au::Widget>(view_)})),
          root_(std::static_pointer_cast<au::Widget>(host_)) {
        borealis::ui::install_settings_strings();  // 词条装填幂等（`settings_i18n.h` ②）
        view_->set_trusted_link_hosts(hosts);
        // 名单本体的变化回调：本用例用它数「落盘几次」，而不是真的写文件（IO 归装配层）。
        hosts.set_change_sink([this](std::span<const std::string> snapshot) -> void {
            ++list_writes;
            snapshot_after_write.assign(snapshot.begin(), snapshot.end());
        });
        if (with_host) {
            view_->set_overlay_host(*host_);
        }
        auto presentation = TerminalView::Presentation{};
        // 两条打开接缝：`open_url` **每次都装替身**（它没有默认实现，留空即「只记日志、什么都不开」），
        // `confirm_open_link` 按 @p stub_link 决定是替身扣回调还是走生产自组三按钮框。
        presentation.open_url = [this](std::string_view target) -> void { opened.emplace_back(target); };
        if (stub_link) {
            presentation.confirm_open_link = [this](const LinkOpenRequest &request,
                                                    std::function<void(LinkOpenAnswer)> answer) -> void {
                asks.push_back(request);
                held_answer = std::move(answer);
            };
        }
        view_->set_presentation(std::move(presentation));
        session_->start();
        focus_.set_root(&root_.widget());
        focus_.set_focus(view_.get());
        render();          // 帧 0：布局发生在这里
        prime_geometry();  // 落点要按真实格网换算，而格数只有布局之后才知
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投字节并排帧到队列见底。
    auto feed(std::string_view bytes) -> void {
        connection_->deliver(bytes);
        render();
    }

    /// @brief 把 @p text 放到网格第 @p row 行（0 基）再排帧。
    auto feed_at(std::size_t row, std::string_view text) -> void {
        feed("\x1b[" + std::to_string(row + 1U) + ";1H" + std::string{text});
    }

    /// @brief 一条 OSC 8：屏上文字与声明目标**刻意不同**，判据 1／4 要的就是这一对。
    auto feed_osc8_at(std::size_t row, std::string_view target, std::string_view screen_text) -> void {
        feed("\x1b[" + std::to_string(row + 1U) + ";1H\x1b]8;;" + std::string{target} + "\x07" +
             std::string{screen_text} + "\x1b]8;;\x07");
    }

    /// @brief 排帧：`on_frame` 落地攒下的动作，再走一遍真实布局与绘制。
    auto render() -> void {
        do {
            view_->on_frame();
            (void)window_.present_root(root_);
        } while (session_->has_damage());
    }

    /// @brief 发一个指针事件（落点取该格中心；行列是**屏幕**坐标，0 基）。
    ///
    /// 无键悬停也走这里：框架 X11 腿给 `MotionNotify` 一律填 `button = Left`（稿 F6 的一手读数），
    /// 故「悬停」在本控件看来就是一次没有按下的 Left Move。
    auto pointer(au::MouseAction action, au::MouseButton button, std::size_t row, std::size_t column,
                 au::ModifierKey modifiers = au::ModifierKey::None) -> void {
        au::MouseEvent event;
        const Rect band = borealis::ui::rect_for(geometry_, row, column, column + 1U);
        event.position = au::Point{.x = static_cast<float>(origin_x_ + band.x + band.width * 0.5),
                                   .y = static_cast<float>(origin_y_ + band.y + band.height * 0.5)};
        event.button = button;
        event.action = action;
        event.modifiers = modifiers;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 悬停到某一格并排帧（两层落笔都要在帧里才读得到）。
    auto hover(std::size_t row, std::size_t column) -> void {
        pointer(au::MouseAction::Move, au::MouseButton::Left, row, column);
        render();
    }

    /// @brief 左键单击一格（按下即抬起）；@p ctrl 置真即 Ctrl+左击。
    auto click(std::size_t row, std::size_t column, bool ctrl = false) -> void {
        const auto modifiers = ctrl ? au::ModifierKey::Control : au::ModifierKey::None;
        pointer(au::MouseAction::Press, au::MouseButton::Left, row, column, modifiers);
        pointer(au::MouseAction::Release, au::MouseButton::Left, row, column, modifiers);
        render();
    }

    /// @brief 右键单击一格（按下即抬起）。
    auto right_click(std::size_t row, std::size_t column) -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Right, row, column);
        pointer(au::MouseAction::Release, au::MouseButton::Right, row, column);
        render();
    }

    /// @brief 装上会话日志的两条钩子：菜单先有三条底，才好断第四条插在「粘贴」与「会话日志」之间。
    auto wire_logging_hooks() -> void {
        view_->set_logging_hooks(
            [this]() -> std::optional<bool> {
                ++log_state_reads;
                return log_state;
            },
            [this]() -> void {
                ++log_toggles;
                log_state = !log_state;
            });
    }

    /// @brief 左键拖拽一段行列区间（按下→移动→抬起）：区间非空才有可复制的文本。
    ///
    /// 不用「单击一格」当选区证人：两端重合在 `ui::row_spans` 的口径下就是空表，
    /// `selected_text()` 交出空串，于是那条断言会证一件不存在的事。
    auto drag_select(std::size_t row, std::size_t from_column, std::size_t to_column) -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Left, row, from_column);
        pointer(au::MouseAction::Move, au::MouseButton::Left, row, to_column);
        pointer(au::MouseAction::Release, au::MouseButton::Left, row, to_column);
        render();
    }

    /// @brief 从提示条盒内的分数点按下，拖到指定格再抬起（判据 3「条不吃点击」的驱动腿）。
    ///
    /// 起点落进条内而终点落在条外的另一格：抬起即两端不重合，`row_spans` 才交出非空区间——
    /// 「按下即抬起在同一格」在 `ui::row_spans` 的口径下是**空表**，那种断言会证一件不存在的事。
    auto drag_from_box(const Rect &box, double fx, double fy, std::size_t to_row, std::size_t to_column) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = static_cast<float>(origin_x_ + box.x + box.width * fx),
                                   .y = static_cast<float>(origin_y_ + box.y + box.height * fy)};
        event.button = au::MouseButton::Left;
        event.action = au::MouseAction::Press;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
        pointer(au::MouseAction::Move, au::MouseButton::Left, to_row, to_column);
        pointer(au::MouseAction::Release, au::MouseButton::Left, to_row, to_column);
        render();
    }

    struct ButtonSpot {
        au::Widget *widget = nullptr;
        float x = 0.0F;
        float y = 0.0F;
    };

    /// @brief 在给定盒内按阅读序扫 Button，取第 @p ordinal 枚（起点与步长 4 dp，同右键那份驱动台）。
    [[nodiscard]] auto find_button_in(au::Rect box, std::size_t ordinal) -> ButtonSpot {
        std::vector<au::Widget *> seen;
        for (float y = box.origin.y + 4.0F; y < box.bottom(); y += 4.0F) {
            for (float x = box.origin.x + 4.0F; x < box.right(); x += 4.0F) {
                au::Widget *widget = hit(x, y);
                if (widget == nullptr || std::string_view{"Button"} != widget->type_name()) {
                    continue;
                }
                if (std::find(seen.begin(), seen.end(), widget) != seen.end()) {
                    continue;
                }
                seen.push_back(widget);
                if (seen.size() == ordinal + 1U) {
                    return ButtonSpot{.widget = widget, .x = x, .y = y};
                }
            }
        }
        return ButtonSpot{};
    }

    [[nodiscard]] auto find_menu_button(std::size_t ordinal) -> ButtonSpot {
        const au::Popup *menu = view_->context_menu();
        if (menu == nullptr || !menu->is_open()) {
            return ButtonSpot{};
        }
        return find_button_in(menu->content_bounds(), ordinal);
    }

    [[nodiscard]] auto find_dialog_button(std::size_t ordinal) -> ButtonSpot {
        const aurora::Dialog *dialog = view_->link_open_dialog();
        if (dialog == nullptr || !dialog->is_open()) {
            return ButtonSpot{};
        }
        return find_button_in(dialog->paint_bounds(), ordinal);
    }

    /// @brief 命中链上问「这一点上是谁」（生产对话框的按钮就靠这条通道被点到）。
    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const std::vector<au::HitNode> chain = root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(window_width_),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    /// @brief 在全局窗口 dp 处左键单击一次（遮罩那一击用）。
    auto click_at(float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        event.action = au::MouseAction::Press;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
        event.action = au::MouseAction::Release;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
        render();
    }

    /// @brief @p row 行、[@p first, @p last) 列的整格带内有多少枚像素正好是 accent 色。
    ///
    /// 只数「等于 accent」而不做帧间差分：线宽恒 1 物理像素、落点在基线 +1px（`decoration_rects` 的
    /// `Single` 档），带内出现这个色只可能是悬停那一层画的。
    [[nodiscard]] auto accent_pixels_in(std::size_t row, std::size_t first, std::size_t last) const -> std::size_t {
        const auto px = pixels();
        const Rect band = borealis::ui::rect_for(geometry_, row, first, last);
        const auto accent = borealis::ui::settings_chrome().accent;
        std::size_t seen = 0U;
        const auto top = static_cast<int>(band.y);
        const auto left = static_cast<int>(band.x);
        for (int y = top; y < top + static_cast<int>(band.height); ++y) {
            for (int x = left; x < left + static_cast<int>(band.width); ++x) {
                const std::size_t index = (static_cast<std::size_t>(y) * buffer_width() +
                                           static_cast<std::size_t>(x)) *
                                          4U;
                if (px[index] == accent.r && px[index + 1U] == accent.g && px[index + 2U] == accent.b &&
                    px[index + 3U] == accent.a) {
                    ++seen;
                }
            }
        }
        return seen;
    }

    /// @brief 一段游程的悬停线应有的 accent 像素数：线宽**恒 1 物理像素**，长 = 游程列数 × 单宽。
    ///
    /// 拿它当期望值而不是写死一枚数：这一条断言因此同时证了两件事——线**铺满了整段游程**
    /// （少画一列就少 11 枚）与线**只有一行高**（厚一档就翻倍）。前置里 scale 恒 1，dp 即物理像素。
    [[nodiscard]] auto hover_line_pixels(std::size_t first, std::size_t last) const -> std::size_t {
        return (last - first) * static_cast<std::size_t>(geometry_.cell_width);
    }

    [[nodiscard]] auto pixels() const -> std::vector<std::uint8_t> {
        std::vector<std::uint8_t> out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    [[nodiscard]] auto hover_link() const -> const std::optional<LinkHit> & { return view_->hover_link(); }
    [[nodiscard]] auto notice_box() const -> const std::optional<Rect> & { return view_->link_notice_box(); }
    [[nodiscard]] auto geometry() const noexcept -> const GridGeometry & { return geometry_; }
    [[nodiscard]] auto dialog_open() const -> bool {
        const aurora::Dialog *dialog = view_->link_open_dialog();
        return dialog != nullptr && dialog->is_open();
    }
    [[nodiscard]] auto menu_open() const -> bool {
        return view_->context_menu() != nullptr && view_->context_menu()->is_open();
    }
    [[nodiscard]] auto selected_text() -> std::string { return view_->selected_text(); }
    [[nodiscard]] auto written() const -> std::string { return connection_->written_text(); }
    [[nodiscard]] auto hosts_contains(std::string_view host) const -> bool { return hosts.contains(host); }

    /// @brief 前置条件：Headless 缩放为 1（dp 即像素），且格网容得下本文件用到的行与列。
    ///
    /// 缺省门槛 41 列是承重的那一枚：「行内串外」的悬停落在第 40 列，越界的落点会让那一条
    /// 变成「没人 hover」而不是「hover 到了空处」——两种情形都判 false，证人就成了空转。
    [[nodiscard]] auto preflight(std::size_t min_columns = 41U) const noexcept -> bool {
        return scale_ == 1.0F && geometry_.rows > 11U && geometry_.columns > min_columns;
    }

    TrustedLinkHosts hosts{};             ///< 名单本体：控件经接缝用它，而不是自持一份。
    std::vector<LinkOpenRequest> asks{};  ///< 确认一问收到过的入参，按序。
    std::vector<std::string> opened{};    ///< 真正发出去的打开动作，按序（替身即唯一出口）。
    std::function<void(LinkOpenAnswer)> held_answer;  ///< 替身扣下的答话回调。
    std::vector<std::string> snapshot_after_write{};  ///< 名单回调收到的最后一次整表快照。
    std::size_t list_writes = 0;       ///< 名单变化回调被触发几次（重复「记住」不该再来一次）。
    bool log_state = false;
    std::size_t log_state_reads = 0;
    std::size_t log_toggles = 0;

  private:
    [[nodiscard]] static auto make_window(int width) -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(width, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    [[nodiscard]] auto buffer_width() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(window_width_) * scale_);
    }

    [[nodiscard]] auto buffer_height() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowHeight) * scale_);
    }

    /// @brief 造替身连接：所有权交给会话，裸指针留在本类驱动投递与观测。
    [[nodiscard]] auto own_connection() -> std::unique_ptr<Connection> {
        auto owned = std::make_unique<FakeConnection>();
        connection_ = owned.get();
        return owned;
    }

    /// @brief 帧 0 之后重取格网几何（落点要按格子换算，`ui::rect_for` 与控件同一坐标空间）。
    auto prime_geometry() -> void {
        const au::Rect box = view_->paint_bounds();
        scale_ = window_.surface().scale_factor();
        origin_x_ = static_cast<double>(box.origin.x);
        origin_y_ = static_cast<double>(box.origin.y);
        const auto metrics = au::render::FontEngine::monospace_cell(test_font(), scale_);
        geometry_ = borealis::ui::make_geometry(
            CellPixels{metrics.cell_width_px, metrics.cell_height_px, metrics.ascent_px}, scale_,
            LogicalSize{static_cast<double>(box.size.width), static_cast<double>(box.size.height)}, kPaddingDp);
    }

    au::Window window_;
    int window_width_ = kWindowWidth;
    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    std::shared_ptr<au::OverlayHost> host_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher pointer_dispatcher_;
    GridGeometry geometry_{};
    float scale_ = 1.0F;
    double origin_x_ = 0.0;
    double origin_y_ = 0.0;
};

}  // namespace

// ============================================================
// 判据 1／3／4／5：悬停两层
// ============================================================

AURORA_TEST_CASE(a_hovered_url_draws_the_line_and_a_strip_of_its_target) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "see http://example.com/a now");
    h.hover(2U, 6U);  // 落在串内（`http://` 之后）
    AURORA_TEST_REQUIRE(h.hover_link().has_value());
    AURORA_TEST_CHECK_EQ(h.hover_link()->target, "http://example.com/a");
    AURORA_TEST_CHECK_EQ(static_cast<int>(h.hover_link()->source), static_cast<int>(LinkSource::Heuristic));
    AURORA_TEST_CHECK_EQ(h.hover_link()->span.first_column, 4U);
    AURORA_TEST_CHECK_EQ(h.hover_link()->span.last_column, 24U);
    // 两层都落笔：游程 20 列的 accent 线（1 物理像素高、铺满整段）加一枚贴左下的提示条。
    AURORA_TEST_CHECK_EQ(h.accent_pixels_in(2U, 4U, 24U), h.hover_line_pixels(4U, 24U));
    AURORA_TEST_REQUIRE(h.notice_box().has_value());
    AURORA_TEST_CHECK_EQ(h.notice_box()->x, kExpectedInset);
    AURORA_TEST_CHECK(h.notice_box()->width > 0.0);
}

AURORA_TEST_CASE(moving_off_the_url_takes_both_layers_away) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "see http://example.com/a now");
    h.hover(2U, 6U);
    AURORA_TEST_REQUIRE(h.hover_link().has_value());
    h.hover(2U, 40U);  // 行内串外的空格
    AURORA_TEST_CHECK_FALSE(h.hover_link().has_value());
    AURORA_TEST_CHECK_FALSE(h.notice_box().has_value());
    AURORA_TEST_CHECK_EQ(h.accent_pixels_in(2U, 4U, 24U), 0U);
}

AURORA_TEST_CASE(an_osc8_link_shows_the_declared_target_not_the_screen_text) {
    // 判据 1／4 的那一对：屏上四个字，提示条与命中报的都是声明的目标（稿 F1）。
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_osc8_at(2U, "https://docs.example.com/reference/section-3", "docs");
    h.hover(2U, 1U);
    AURORA_TEST_REQUIRE(h.hover_link().has_value());
    AURORA_TEST_CHECK_EQ(h.hover_link()->target, "https://docs.example.com/reference/section-3");
    AURORA_TEST_CHECK_EQ(static_cast<int>(h.hover_link()->source), static_cast<int>(LinkSource::Osc8));
    // 游程取标识而非屏上文字切出的段：整条四个字都在区间里。
    AURORA_TEST_CHECK_EQ(h.hover_link()->span.first_column, 0U);
    AURORA_TEST_CHECK_EQ(h.hover_link()->span.last_column, 4U);
    AURORA_TEST_CHECK_EQ(h.accent_pixels_in(2U, 0U, 4U), h.hover_line_pixels(0U, 4U));
}

AURORA_TEST_CASE(a_scheme_outside_the_whitelist_stays_inert) {
    // 判据 5：白名单外不是「灰着但还能点」，而是整条链路什么都不做。
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "get ftp://example.com/pub now");
    h.hover(2U, 8U);
    AURORA_TEST_CHECK_FALSE(h.hover_link().has_value());
    AURORA_TEST_CHECK_FALSE(h.notice_box().has_value());
    h.click(2U, 8U, true);
    AURORA_TEST_CHECK_TRUE(h.asks.empty());
    AURORA_TEST_CHECK_TRUE(h.opened.empty());
    // 同一串换成白名单内的 http 就立刻可用：证上一段的「没命中」不是本用例装错了内容。
    auto options = link_options();
    options.link.scan.schemes = borealis::ui::parse_link_schemes("http,ftp");
    Harness wide(options);
    AURORA_TEST_REQUIRE(wide.preflight());
    wide.feed_at(2U, "get ftp://example.com/pub now");
    wide.hover(2U, 8U);
    AURORA_TEST_REQUIRE(wide.hover_link().has_value());
}

// ============================================================
// 判据 2／7 与 D13①：Ctrl+左击那一条入口
// ============================================================

AURORA_TEST_CASE(ctrl_click_asks_once_with_the_target_source_and_host) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "http://example.com/a");
    h.click(2U, 6U, true);
    AURORA_TEST_REQUIRE_EQ(h.asks.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.asks[0].target, "http://example.com/a");
    AURORA_TEST_CHECK_EQ(static_cast<int>(h.asks[0].source), static_cast<int>(LinkSource::Heuristic));
    AURORA_TEST_CHECK_EQ(h.asks[0].host, "http://example.com");
    // 问了没答 ⇒ 什么都不开（稿 D11① 的那条安全侧，也是本需求里唯一不需要撤账的一档）。
    AURORA_TEST_CHECK_TRUE(h.opened.empty());
    AURORA_TEST_REQUIRE(h.held_answer != nullptr);
    h.held_answer(LinkOpenAnswer::OpenOnce);
    AURORA_TEST_REQUIRE_EQ(h.opened.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.opened[0], "http://example.com/a");  // 逐字节：接缝收到的就是目标全文
    AURORA_TEST_CHECK_EQ(h.list_writes, 0U);                    // 「仅本次」不写名单
}

AURORA_TEST_CASE(ctrl_click_leaves_the_selection_alone) {
    // D13①：打开一次不该顺带改一次选区——随后的 copy-on-select 会把 URL 塞进剪贴板，那是没要的动作。
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "http://example.com/a tail");
    h.drag_select(2U, 20U, 23U);  // 先有一段与链接无关的选区，作为「不该动」的观测对象
    const std::string before = h.selected_text();
    AURORA_TEST_REQUIRE(!before.empty());
    h.click(2U, 6U, true);
    AURORA_TEST_REQUIRE_EQ(h.asks.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.selected_text(), before);  // 选区一字未改
    AURORA_TEST_CHECK_TRUE(h.opened.empty());         // 问了没答 ⇒ 不开
    // 对照组：普通拖拽照旧改选区，且照旧**不**问也不开（左击行为逐字节不变，稿 D7①）。
    Harness plain;
    AURORA_TEST_REQUIRE(plain.preflight());
    plain.feed_at(2U, "http://example.com/a tail");
    plain.drag_select(2U, 0U, 3U);
    AURORA_TEST_CHECK_EQ(plain.selected_text(), "http");
    AURORA_TEST_CHECK_TRUE(plain.asks.empty());
    AURORA_TEST_CHECK_TRUE(plain.opened.empty());
}

AURORA_TEST_CASE(remember_and_open_writes_the_host_and_silences_the_second_hit) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "http://example.com/a");
    h.click(2U, 6U, true);
    AURORA_TEST_REQUIRE(h.held_answer != nullptr);
    h.held_answer(LinkOpenAnswer::RememberAndOpen);
    AURORA_TEST_CHECK_EQ(h.opened.size(), 1U);
    AURORA_TEST_CHECK_TRUE(h.hosts_contains("http://example.com"));
    AURORA_TEST_CHECK_EQ(h.list_writes, 1U);  // 落盘闭包恰一次：装配层据此写 `trusted_url_hosts`

    h.click(2U, 6U, true);
    AURORA_TEST_CHECK_EQ(h.asks.size(), 1U);      // 同域第二次不再问
    AURORA_TEST_CHECK_EQ(h.opened.size(), 2U);    // 直接开
    AURORA_TEST_CHECK_EQ(h.list_writes, 1U);      // 重复命中不往名单里添第二份
}

AURORA_TEST_CASE(a_different_port_on_the_same_host_is_asked_again) {
    // D9①：域含端口。只记 host 的实现会把一次「记住」铺满整台机器的所有端口——那条改判在这里有证人。
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "http://example.com:8080/a");
    h.click(2U, 6U, true);
    AURORA_TEST_REQUIRE_EQ(h.asks.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.asks[0].host, "http://example.com:8080");
    AURORA_TEST_REQUIRE(h.held_answer != nullptr);
    h.held_answer(LinkOpenAnswer::RememberAndOpen);
    AURORA_TEST_CHECK_TRUE(h.hosts_contains("http://example.com:8080"));
    AURORA_TEST_CHECK_FALSE(h.hosts_contains("http://example.com"));
    // 换了端口就要重新问：同一行的另一枚 URL，免得「第二次没问」其实是「同域已记住」的假绿。
    h.feed_at(4U, "http://example.com:9090/b");
    h.click(4U, 6U, true);
    AURORA_TEST_REQUIRE_EQ(h.asks.size(), 2U);
    AURORA_TEST_CHECK_EQ(h.asks[1].host, "http://example.com:9090");
}

AURORA_TEST_CASE(the_two_other_policies_never_ask_and_always_ask) {
    auto never = link_options(LinkConfirmPolicy::AskOnce, LinkConfirmPolicy::Never);
    Harness quiet(never);
    AURORA_TEST_REQUIRE(quiet.preflight());
    quiet.feed_at(2U, "http://example.com/a");
    quiet.click(2U, 6U, true);
    AURORA_TEST_CHECK_TRUE(quiet.asks.empty());
    AURORA_TEST_CHECK_EQ(quiet.opened.size(), 1U);

    auto always = link_options(LinkConfirmPolicy::AskOnce, LinkConfirmPolicy::AskAlways);
    Harness noisy(always);
    AURORA_TEST_REQUIRE(noisy.preflight());
    noisy.feed_at(2U, "http://example.com/a");
    noisy.click(2U, 6U, true);
    AURORA_TEST_REQUIRE(noisy.held_answer != nullptr);
    noisy.held_answer(LinkOpenAnswer::RememberAndOpen);  // 即便点了「记住」……
    noisy.click(2U, 6U, true);
    AURORA_TEST_CHECK_EQ(noisy.asks.size(), 2U);         // ……AskAlways 下一次照问（它不读名单）
}

AURORA_TEST_CASE(the_two_policies_are_configured_apart) {
    // 需求那句「信任级别可分别配置」的界面侧：osc8=never ∧ heuristic=ask_always 各走各的闸。
    auto options = link_options(LinkConfirmPolicy::Never, LinkConfirmPolicy::AskAlways);
    Harness h(options);
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "http://plain.example/a");
    h.feed_osc8_at(3U, "http://declared.example/b", "docs");
    h.click(2U, 6U, true);
    h.click(3U, 1U, true);
    AURORA_TEST_CHECK_EQ(h.opened.size(), 1U);  // OSC 8 那一条没问就开了
    AURORA_TEST_CHECK_EQ(h.asks.size(), 1U);    // 启发式那一条问了
    AURORA_TEST_REQUIRE_EQ(h.opened.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.opened[0], "http://declared.example/b");
    AURORA_TEST_CHECK_EQ(h.asks[0].target, "http://plain.example/a");
}

// ============================================================
// 判据 6：右键菜单的第四条
// ============================================================

AURORA_TEST_CASE(the_open_link_row_lands_between_paste_and_logging) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.wire_logging_hooks();
    h.feed_at(2U, "http://example.com/a");
    h.right_click(2U, 6U);
    AURORA_TEST_REQUIRE(h.menu_open());
    // 认条目不认坐标分数：命中链取回第三枚按钮读它的标签（同 `itest_right_click_paste` 的那条纪律）。
    const Harness::ButtonSpot third = h.find_menu_button(2);
    AURORA_TEST_REQUIRE(third.widget != nullptr);
    auto *button = dynamic_cast<au::Button *>(third.widget);
    AURORA_TEST_REQUIRE(button != nullptr);
    AURORA_TEST_CHECK_EQ(button->accessibility_label(), borealis::ui::settings_label("menu.item.open_link"));
    // 第四条把「会话日志」推下去了：第四枚仍是日志条目，点第三条只唤打开腿不唤日志腿。
    h.click_at(third.x, third.y);
    h.render();
    AURORA_TEST_CHECK_EQ(h.log_toggles, 0U);
    AURORA_TEST_REQUIRE_EQ(h.asks.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.asks[0].target, "http://example.com/a");
}

AURORA_TEST_CASE(right_click_off_a_link_keeps_the_three_row_menu) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.wire_logging_hooks();
    h.feed_at(2U, "no address here");
    h.right_click(2U, 6U);
    AURORA_TEST_REQUIRE(h.menu_open());
    // 三条底：0 复制、1 粘贴、2 会话日志——第四条「打开链接」不在，故第三枚仍是日志条目。
    const Harness::ButtonSpot third = h.find_menu_button(2);
    AURORA_TEST_CHECK(third.widget != nullptr);
    AURORA_TEST_CHECK_FALSE(h.hover_link().has_value());  // 没命中就没有第四条，也没有线
}

// ============================================================
// 判据 7：上报档让位
// ============================================================

AURORA_TEST_CASE(pointer_reporting_suppresses_the_hover_layer_and_the_click_entry) {
    // `?1000`（无键 Move 落本地）与 `?1003`（Move 也归上报）两档分开断：F6 那条实测说这两档的
    // **事件归属**不同，而本判据要的是**这一层都不画**——只压装饰层，不改归属。
    for (const char *mode : {"\x1b[?1000h", "\x1b[?1003h"}) {
        Harness h;
        AURORA_TEST_REQUIRE(h.preflight());
        h.feed_at(2U, "http://example.com/a");
        h.feed(mode);
        h.hover(2U, 6U);
        AURORA_TEST_CHECK_FALSE(h.hover_link().has_value());   // 无下划线
        AURORA_TEST_CHECK_FALSE(h.notice_box().has_value());   // 无目标条
        AURORA_TEST_CHECK_EQ(h.accent_pixels_in(2U, 4U, 24U), 0U);
        h.click(2U, 6U, true);
        AURORA_TEST_CHECK_TRUE(h.asks.empty());                // Ctrl 击不响
        AURORA_TEST_CHECK_TRUE(h.opened.empty());
        AURORA_TEST_CHECK(!h.written().empty());               // 那一击整笔送给了远端
        h.right_click(2U, 6U);                                 // 右键恒归本地：条目仍在
        AURORA_TEST_REQUIRE(h.menu_open());
        const Harness::ButtonSpot second = h.find_menu_button(1);
        AURORA_TEST_CHECK(second.widget != nullptr);
    }
}

// ============================================================
// 判据 3：提示条的两条非模态纪律
// ============================================================

AURORA_TEST_CASE(a_target_too_wide_for_the_pane_yields_the_strip_not_a_truncation) {
    // 截断一条 URI 就是破判据 1／4 那句「显示的恒是目标」，故装不下即整条不画。
    // 同一枚 44 字符的目标（实显约 484 dp：量过）在 640 宽窗上摆得下、在 140 窄窗上摆不下，
    // 于是窄窗那一段的「条让位」有一个同内容的正例作非空转证人。
    const auto options = link_options(LinkConfirmPolicy::AskOnce, LinkConfirmPolicy::AskOnce, "http,https");
    constexpr const char *kLongTarget = "https://docs.example.com/reference/section-3";
    Harness narrow(options, true, true, kNarrowWidth);
    AURORA_TEST_REQUIRE(narrow.preflight(10U));
    narrow.feed_osc8_at(2U, kLongTarget, "docs");
    narrow.hover(2U, 1U);
    AURORA_TEST_REQUIRE(narrow.hover_link().has_value());       // 命中照旧（线仍可画）
    AURORA_TEST_CHECK_FALSE(narrow.notice_box().has_value());   // 条让位

    Harness wide(options);
    AURORA_TEST_REQUIRE(wide.preflight());
    wide.feed_osc8_at(2U, kLongTarget, "docs");
    wide.hover(2U, 1U);
    AURORA_TEST_REQUIRE(wide.notice_box().has_value());
}

AURORA_TEST_CASE(the_strip_takes_no_clicks_and_the_cell_under_it_still_selects) {
    // 判据 3 的后半（稿 F9 的「非模态三不」）：提示条一枚命中盒都不交，于是贴着的这一击照常归选区。
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::size_t last_row = h.geometry().rows - 1U;
    h.feed_at(last_row, "http://example.com/a");
    h.hover(last_row, 6U);
    AURORA_TEST_REQUIRE(h.notice_box().has_value());  // 条正压在这一行之上
    // 起点在条内、终点在同行另一格：按下即抬起落在同一格时 `row_spans` 交出空表，那条断言不证任何事。
    h.drag_from_box(*h.notice_box(), 0.5, 0.25, last_row, 0U);
    const std::string selected = h.selected_text();
    AURORA_TEST_REQUIRE(!selected.empty());                        // 条下那一格照常起选区
    AURORA_TEST_CHECK_TRUE(std::string_view{"http://example.com/a"}.starts_with(selected));
    AURORA_TEST_CHECK_TRUE(h.asks.empty());                        // 也不问：条不改变事件归属
    AURORA_TEST_CHECK_TRUE(h.opened.empty());                      // 更不开
}

AURORA_TEST_CASE(new_output_takes_the_line_down) {
    // 判据 5 的另一半：命中存的是**存储行序**，而本帧一有 damage 就可能与内容对不上（保守摘线）。
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "http://example.com/a");
    h.hover(2U, 6U);
    AURORA_TEST_REQUIRE(h.hover_link().has_value());
    h.feed_at(5U, "unrelated line");
    AURORA_TEST_CHECK_FALSE(h.hover_link().has_value());
}

// ============================================================
// 判据 4 与 D11①：生产确认框的三枚按钮经真实命中链点到
// ============================================================

AURORA_TEST_CASE(the_three_dialog_buttons_answer_through_the_hit_chain) {
    Harness cancel(link_options(), true, false);  // 不装 `confirm_open_link` ⇒ 走生产的自组三按钮框
    AURORA_TEST_REQUIRE(cancel.preflight());
    cancel.feed_at(2U, "http://example.com/a");
    cancel.click(2U, 6U, true);
    AURORA_TEST_REQUIRE(cancel.dialog_open());
    // 三枚按阅读序：取消 → 仅本次 → 记住该域并打开（已评审稿面 `UI_COMMANDS.draft.svg` 屏 4）。
    const Harness::ButtonSpot first = cancel.find_dialog_button(0);
    const Harness::ButtonSpot second = cancel.find_dialog_button(1);
    const Harness::ButtonSpot third = cancel.find_dialog_button(2);
    AURORA_TEST_REQUIRE(first.widget != nullptr);
    AURORA_TEST_REQUIRE(second.widget != nullptr);
    AURORA_TEST_REQUIRE(third.widget != nullptr);
    AURORA_TEST_CHECK_EQ(dynamic_cast<au::Button *>(first.widget)->accessibility_label(),
                         borealis::ui::settings_label("links.confirm.cancel"));
    AURORA_TEST_CHECK_EQ(dynamic_cast<au::Button *>(second.widget)->accessibility_label(),
                         borealis::ui::settings_label("links.confirm.open_once"));
    AURORA_TEST_CHECK_EQ(dynamic_cast<au::Button *>(third.widget)->accessibility_label(),
                         borealis::ui::settings_label("links.confirm.remember_and_open"));

    cancel.click_at(first.x, first.y);
    AURORA_TEST_CHECK_TRUE(cancel.opened.empty());   // 取消：什么都不开
    AURORA_TEST_CHECK_FALSE(cancel.dialog_open());   // 答话即收起

    cancel.click(2U, 6U, true);
    AURORA_TEST_REQUIRE(cancel.dialog_open());
    cancel.click_at(cancel.find_dialog_button(1).x, cancel.find_dialog_button(1).y);  // 仅本次
    AURORA_TEST_CHECK_EQ(cancel.opened.size(), 1U);
    AURORA_TEST_CHECK_EQ(cancel.hosts.hosts().size(), 0U);  // 不写名单

    cancel.click(2U, 6U, true);
    AURORA_TEST_REQUIRE(cancel.dialog_open());
    cancel.click_at(cancel.find_dialog_button(2).x, cancel.find_dialog_button(2).y);  // 记住并打开
    AURORA_TEST_CHECK_EQ(cancel.opened.size(), 2U);
    AURORA_TEST_CHECK_TRUE(cancel.hosts_contains("http://example.com"));
    AURORA_TEST_CHECK_EQ(cancel.list_writes, 1U);
    // 同域第三次不再问：写进去的那枚域与判同域用的是同一枚字符串（D9① 的「不能有两处拆法」）。
    cancel.click(2U, 6U, true);
    AURORA_TEST_CHECK_FALSE(cancel.dialog_open());
    AURORA_TEST_CHECK_EQ(cancel.opened.size(), 3U);
}

AURORA_TEST_CASE(a_click_on_the_dialog_scrim_answers_nothing) {
    Harness h(link_options(), true, false);
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "http://example.com/a");
    h.click(2U, 6U, true);
    AURORA_TEST_REQUIRE(h.dialog_open());
    const au::Widget *scrim = h.hit(4.0F, 4.0F);  // 窗角在内容盒之外
    AURORA_TEST_REQUIRE(scrim != nullptr);
    AURORA_TEST_CHECK_EQ(std::string_view{scrim->type_name()}, std::string_view{"Dialog"});
    h.click_at(4.0F, 4.0F);
    AURORA_TEST_CHECK(h.opened.empty());
    AURORA_TEST_CHECK(h.dialog_open());  // 遮罩不等于取消（F12）：框还在等答话，什么都没开
}

AURORA_TEST_CASE(without_a_host_the_question_cannot_be_asked_so_nothing_opens) {
    Harness h(link_options(), false, false);  // 没有浮层宿主，也不装替身
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed_at(2U, "http://example.com/a");
    h.click(2U, 6U, true);
    h.render();
    AURORA_TEST_CHECK_TRUE(h.opened.empty());   // 「问了没答」与「问不出去」同构：最坏就是什么都不开
    AURORA_TEST_CHECK_EQ(h.list_writes, 0U);
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_url_link
