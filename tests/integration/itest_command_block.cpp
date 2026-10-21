/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（命令块三件：菜单复制、失败底色、悬停分隔线）+
///           src/ui/command_block.cpp + src/ui/right_click.cpp
/// 测试说明: 以无头窗口 + 真实指针派发驱动「OSC 133 字节 → `term::CommandBlockTracker` →
///           每帧块表副本 → 菜单条目 / 失败区间 / 分隔线」这条链（`SPEC.FEAT.INTEG.01`，
///           裁决 7.123）。断的是**链路**而非折算件本身（`utest_ui_command_block` 已逐条覆盖
///           行号算术），值得单独证的有八件事：
///           ① 完成块（失败码 1）右键菜单多出「复制上一命令的全部输出」，点击后**下一帧**才写
///              剪贴板，内容恰是 `C → D` 之间的输出（不含提示符与命令行回显）；
///           ② 成功命令（`D;0`）同样可复制，且不铺失败底色；
///           ③ `cd` 型零输出（`C` 与 `D` 同行同列）整条不出现，而不是置灰；
///           ④ 未收 `D` 的开放块没有复制条目，但它仍是可悬停的块（分隔线照画）；
///           ⑤ 失败底色只铺输出行，提示符 / 回显行一枚都没有；选区色带盖在红底之上；
///           ⑥ 分隔线随开关、随悬停、随开关关掉立即消失；`?1000` 档仍跟指针，`?1003` 档
///              整笔 Move 归远端，本层不画；
///           ⑦ `ED 3` 与 `RIS` 之后块表清空：红底消失、菜单条目消失；
///           ⑧ 回看：块首行滚出可见窗后分隔线不画，但右键复制仍取得出整段输出；块整体滚出
///              **存储**后条目才消失。
///
///           「行号不并存」是这组用例的隐含前提：块的绝对行不随回看与溢出改变，存储行与屏幕行
///           每次消费现折算。第 8 条因此把两档滚出分开断——可见窗外仍可复制，存储窗外才不可。
///
///           失败底色与分隔线都用「等于公式色的像素数」判而不是帧间差分：本文件的色板把
///           `basic[1]` 与 `default_background` 拉开，`mix_half` 两次后的红调与 `card_line`
///           都不与正文任何一层撞色，带内出现该值只可能是这两层画的。分隔线还按整行宽
///           断言像素数（少画一列就少一格宽），于是「铺满首行」与「只有 1 物理像素高」
///           同时被钉住。

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
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/settings_panel.h"
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_command_block {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::CellPixels;
using borealis::ui::GridGeometry;
using borealis::ui::LogicalSize;
using borealis::ui::PaletteSpec;
using borealis::ui::Rect;
using borealis::ui::RgbaColor;
using borealis::ui::TerminalView;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kScrollback = 40;
constexpr float kPaddingDp = 4.0F;
/// @brief 与 `itest_url_link` 同一档窗口：本机 Headless 的 14pt 单宽格实测 11x22 dp，640x440
///        给到 57 列 x 19 行——第 8 条要「输出行还在画面里而提示符行已滚出」，19 行才留得出
///        这段错位；格宽也是「分隔线像素数 = 列数 × 格宽」那条期望值的来源。
constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 440;

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

/// @brief 与 `itest_render_viewport` 同一份色板：`basic[1]`（红调）离默认底足够远，失败底色
///        经两次 `mix_half` 后仍是独一无二的一枚像素值；`basic[8]` 则是选区回落的底色。
[[nodiscard]] auto test_palette() -> PaletteSpec {
    auto spec = PaletteSpec{};
    for (std::size_t index = 0; index < spec.basic.size(); ++index) {
        spec.basic[index] = RgbaColor{static_cast<std::uint8_t>(20U + index * 13U),
                                      static_cast<std::uint8_t>(200U - index * 11U),
                                      static_cast<std::uint8_t>(60U + index * 7U), 255U};
    }
    spec.default_foreground = RgbaColor{220U, 221U, 222U, 255U};
    spec.default_background = RgbaColor{17U, 18U, 19U, 255U};
    return spec;
}

/// @brief OSC 133 的完成块字节串：提示符（A/B）→ 命令行回显 → 输出（C/D）。
///
/// `D` 落在最后一行输出的下一行、列 0——命令以换行结尾的常态，于是复制区间正好是每一行输出，
/// 末行的换行由网格折行产生、不进区间（裁决 7.123 D4①）。
/// @param output 输出行（每行一条，写入时各带一个换行）。
/// @param exit_field `D` 的参数段（`0` / `1` / 空串等）。
[[nodiscard]] auto completed_block_bytes(const std::vector<std::string> &output, std::string_view exit_field)
    -> std::string {
    std::string bytes{"\x1b]133;A\x07$ \x1b]133;B\x07run\r\n\x1b]133;C\x07"};
    for (const std::string &line : output) {
        bytes += line;
        bytes += "\r\n";
    }
    bytes += "\x1b]133;D;";
    bytes += exit_field;
    bytes += "\x07";
    return bytes;
}

/// @brief 未收 `D` 的开放块：停在最后一行输出处，块只覆盖已知行。
[[nodiscard]] auto open_block_bytes(const std::vector<std::string> &output) -> std::string {
    std::string bytes{"\x1b]133;A\x07$ \x1b]133;B\x07run\r\n\x1b]133;C\x07"};
    for (std::size_t index = 0; index < output.size(); ++index) {
        bytes += output[index];
        if (index + 1U < output.size()) {
            bytes += "\r\n";
        }
    }
    return bytes;
}

/// @brief 零输出的 `cd` 型块：`C` 与 `D` 同行同列（命令换行之后）。
[[nodiscard]] auto empty_block_bytes(std::string_view exit_field) -> std::string {
    std::string bytes{"\x1b]133;A\x07$ \x1b]133;B\x07" "cd\r\n\x1b]133;C\x07\x1b]133;D;"};
    bytes += exit_field;
    bytes += "\x07";
    return bytes;
}

[[nodiscard]] auto join_lines(const std::vector<std::string> &lines) -> std::string {
    std::string text;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0U) {
            text += '\n';
        }
        text += lines[index];
    }
    return text;
}

/// @brief 驱动台：会话 + 替身连接 + 视口控件 + 浮层宿主 + 无头窗口。
///
/// 剪贴板写接缝换成替身（本文件只读这一条），浮层宿主必须装——菜单条目就在它上面。焦点落在
/// 视口上，于是选区色带取**持焦**那一档，第 5 条的「选区盖红底」才不会因为降级混色而变成
/// 另一枚值。
class Harness {
  public:
    explicit Harness(bool separator = false)
        : palette_(test_palette()),
          window_(make_window()),
          session_(std::make_unique<Session>(own_connection(), kNominalSize, kScrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, palette_, test_font(), ui::Typography{}, kPaddingDp,
                                               std::chrono::milliseconds{500}, options(separator))),
          host_(std::make_shared<au::OverlayHost>(au::Node{std::static_pointer_cast<au::Widget>(view_)})),
          root_(std::static_pointer_cast<au::Widget>(host_)) {
        borealis::ui::install_settings_strings();  // 词条装填幂等（`settings_i18n.h` ②）
        view_->set_overlay_host(*host_);
        auto presentation = TerminalView::Presentation{};
        presentation.clipboard_write = [this](std::string_view text) -> void {
            ++write_calls;
            copied.emplace_back(text);
        };
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

    /// @brief 排帧：`on_frame` 落地攒下的复制请求，再走一遍真实布局与绘制。
    auto render() -> void {
        do {
            view_->on_frame();
            (void)window_.present_root(root_);
        } while (session_->has_damage());
    }

    /// @brief 发一个指针事件（落点取该格中心；行列是**屏幕**坐标，0 基）。
    auto pointer(au::MouseAction action, au::MouseButton button, std::size_t row, std::size_t column) -> void {
        au::MouseEvent event;
        const Rect band = borealis::ui::rect_for(geometry_, row, column, column + 1U);
        event.position = au::Point{.x = static_cast<float>(origin_x_ + band.x + band.width * 0.5),
                                   .y = static_cast<float>(origin_y_ + band.y + band.height * 0.5)};
        event.button = button;
        event.action = action;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 右键单击一格（按下即抬起）；菜单在按下那一刻打开，收起排帧后才可见。
    auto right_click(std::size_t row, std::size_t column) -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Right, row, column);
        pointer(au::MouseAction::Release, au::MouseButton::Right, row, column);
    }

    /// @brief 无键悬停到一格（X11 腿的 Move 恒带 Left，见 `itest_url_link` 的同一注记）。
    auto hover(std::size_t row, std::size_t column) -> void {
        pointer(au::MouseAction::Move, au::MouseButton::Left, row, column);
        render();
    }

    /// @brief 左键拖拽一段行列区间（按下→移动→抬起），区间非空才有可复制的文本。
    auto drag_select(std::size_t row, std::size_t from_column, std::size_t to_column) -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Left, row, from_column);
        pointer(au::MouseAction::Move, au::MouseButton::Left, row, to_column);
        pointer(au::MouseAction::Release, au::MouseButton::Left, row, to_column);
        render();
    }

    /// @brief 运行期换行为口径（S4① 第二条入口）：交进控件后排一帧。
    auto apply_options(TerminalView::InteractionOptions options) -> void {
        view_->apply_interaction_options(std::move(options));
        render();
    }

    /// @brief 在全局窗口 dp 处左键单击一次（菜单条目的真实命中链落点用）。
    ///
    /// 刻意**不**排帧：第 1 条要断「点击回调只攒请求、剪贴板在下一帧才写」这条中间态。
    auto click_at(float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        event.action = au::MouseAction::Press;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
        event.action = au::MouseAction::Release;
        (void)pointer_dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 点菜单第 @p ordinal 条（阅读序），不排帧。
    auto click_menu_item(std::size_t ordinal) -> bool {
        const ButtonSpot spot = find_menu_button(ordinal);
        if (spot.widget == nullptr) {
            return false;
        }
        click_at(spot.x, spot.y);
        return true;
    }

    /// @brief 在菜单外按一下收起菜单（宿主只认按下；抬起不再推进选区）。
    auto dismiss_menu() -> void {
        pointer(au::MouseAction::Press, au::MouseButton::Left, 8U, 5U);
        pointer(au::MouseAction::Release, au::MouseButton::Left, 8U, 5U);
        render();
    }

    struct ButtonSpot {
        au::Widget *widget = nullptr;
        float x = 0.0F;
        float y = 0.0F;
    };

    /// @brief 在给定盒内按阅读序扫 Button，取第 @p ordinal 枚（起点与步长 4 dp，同既有驱动台）。
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

    /// @brief 菜单第 @p ordinal 条的无障碍标签：认条目不认坐标分数（与绘制同源）。
    [[nodiscard]] auto menu_label(std::size_t ordinal) -> std::string {
        const ButtonSpot spot = find_menu_button(ordinal);
        if (spot.widget == nullptr) {
            return {};
        }
        auto *button = dynamic_cast<au::Button *>(spot.widget);
        return button == nullptr ? std::string{} : button->accessibility_label();
    }

    /// @brief 命中链上问「这一点上是谁」（菜单条目落点即由此扫出）。
    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const std::vector<au::HitNode> chain = root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    /// @brief 第 @p row 行、第 [@p first, @p last) 列的整格带内有多少枚像素正好是 @p color。
    [[nodiscard]] auto count_color(std::size_t row, std::size_t first, std::size_t last,
                                   const RgbaColor &color) const -> std::size_t {
        return count_color_in(borealis::ui::rect_for(geometry_, row, first, last), color);
    }

    /// @brief 第 @p row 行上沿一条 1 物理像素的带内，@p color 的像素数（分隔线判据）。
    [[nodiscard]] auto count_top_edge(std::size_t row, const RgbaColor &color) const -> std::size_t {
        Rect band = borealis::ui::rect_for(geometry_, row, 0U, geometry_.columns);
        band.height = 1.0;
        return count_color_in(band, color);
    }

    /// @brief 整帧里 @p color 的像素数（「哪里都没画」那一档用）。
    [[nodiscard]] auto count_everywhere(const RgbaColor &color) const -> std::size_t {
        const auto px = pixels();
        std::size_t seen = 0U;
        for (std::size_t index = 0; index + 3U < px.size(); index += 4U) {
            if (px[index] == color.red && px[index + 1U] == color.green && px[index + 2U] == color.blue &&
                px[index + 3U] == color.alpha) {
                ++seen;
            }
        }
        return seen;
    }

    [[nodiscard]] auto count_color_in(const Rect &local, const RgbaColor &color) const -> std::size_t {
        const auto px = pixels();
        int left = static_cast<int>(origin_x_ + local.x);
        int top = static_cast<int>(origin_y_ + local.y);
        int right = static_cast<int>(origin_x_ + local.x + local.width);
        int bottom = static_cast<int>(origin_y_ + local.y + local.height);
        left = std::clamp(left, 0, static_cast<int>(buffer_width()));
        right = std::clamp(right, 0, static_cast<int>(buffer_width()));
        top = std::clamp(top, 0, static_cast<int>(buffer_height()));
        bottom = std::clamp(bottom, 0, static_cast<int>(buffer_height()));
        std::size_t seen = 0U;
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                const std::size_t index =
                    (static_cast<std::size_t>(y) * buffer_width() + static_cast<std::size_t>(x)) * 4U;
                if (px[index] == color.red && px[index + 1U] == color.green && px[index + 2U] == color.blue &&
                    px[index + 3U] == color.alpha) {
                    ++seen;
                }
            }
        }
        return seen;
    }

    [[nodiscard]] auto pixels() const -> std::vector<std::uint8_t> {
        std::vector<std::uint8_t> out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    /// @brief 失败底色的公式值（与 `paint_row` 同一算式，不复制一个十六进制数）。
    [[nodiscard]] auto failed_tint() const -> RgbaColor {
        return borealis::ui::mix_half(
            borealis::ui::mix_half(palette_.basic[1], palette_.default_background),
            palette_.default_background);
    }

    [[nodiscard]] auto separator_ink() const -> RgbaColor {
        const aurora::Color line = borealis::ui::settings_chrome().card_line;
        const RgbaColor ink{line.r, line.g, line.b, line.a};
        return ink;
    }

    [[nodiscard]] auto selection_ink() const -> RgbaColor {
        return borealis::ui::selection_color(palette_);
    }

    [[nodiscard]] auto row_width(std::size_t row) const -> std::size_t {
        return static_cast<std::size_t>(borealis::ui::rect_for(geometry_, row, 0U, geometry_.columns).width);
    }

    [[nodiscard]] auto menu_open() const -> bool {
        return view_->context_menu() != nullptr && view_->context_menu()->is_open();
    }
    [[nodiscard]] auto written() const -> std::string { return connection_->written_text(); }
    [[nodiscard]] auto geometry() const noexcept -> const GridGeometry & { return geometry_; }

    /// @brief 前置条件：Headless 缩放为 1（dp 即像素），格网容得下本文件用到的行与列。
    [[nodiscard]] auto preflight() const noexcept -> bool {
        return scale_ == 1.0F && geometry_.rows > 12U && geometry_.columns > 30U;
    }

    std::vector<std::string> copied{};  ///< 写进剪贴板的文本，按序。
    std::size_t write_calls = 0;

  private:
    [[nodiscard]] static auto options(bool separator) -> TerminalView::InteractionOptions {
        TerminalView::InteractionOptions options{};
        options.command_block_separator = separator;
        return options;
    }

    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    [[nodiscard]] auto buffer_width() const -> std::size_t {
        return static_cast<std::size_t>(static_cast<float>(kWindowWidth) * scale_);
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

    PaletteSpec palette_{};
    au::Window window_;
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
// 判据 1／2／3：菜单条目与复制内容
// ============================================================

AURORA_TEST_CASE(a_failed_command_copies_only_the_output_at_the_next_frame) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::vector<std::string> output{"file1", "file2"};
    h.feed(completed_block_bytes(output, "1"));
    h.right_click(1U, 1U);
    h.render();
    AURORA_TEST_REQUIRE(h.menu_open());
    // 三条目：复制 / 复制上一命令的全部输出 / 粘贴。认标签钉的是第二条，而不是数序号。
    AURORA_TEST_CHECK_EQ(h.menu_label(1U),
                         borealis::ui::settings_label("menu.item.copy_command_output"));
    AURORA_TEST_CHECK_EQ(h.menu_label(2U), borealis::ui::settings_label("menu.item.paste"));
    AURORA_TEST_REQUIRE(h.click_menu_item(1U));
    // 点击回调只攒请求：剪贴板是 IO，落在下一帧（AGENTS.md §4.5 第 25 条）。
    AURORA_TEST_CHECK_EQ(h.write_calls, 0U);
    AURORA_TEST_CHECK_TRUE(h.copied.empty());
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.copied.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.copied[0], join_lines(output));  // 不含提示符「$ 」与回显「run」
    AURORA_TEST_CHECK_FALSE(h.menu_open());
}

AURORA_TEST_CASE(a_successful_command_keeps_the_copy_row_and_paints_no_failure_tint) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    const std::vector<std::string> output{"ok1", "ok2"};
    h.feed(completed_block_bytes(output, "0"));
    AURORA_TEST_CHECK_EQ(h.count_color(1U, 0U, h.geometry().columns, h.failed_tint()), 0U);
    AURORA_TEST_CHECK_EQ(h.count_color(2U, 0U, h.geometry().columns, h.failed_tint()), 0U);
    h.right_click(1U, 1U);
    h.render();
    AURORA_TEST_REQUIRE(h.menu_open());
    AURORA_TEST_CHECK_EQ(h.menu_label(1U),
                         borealis::ui::settings_label("menu.item.copy_command_output"));
    AURORA_TEST_REQUIRE(h.click_menu_item(1U));
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.copied.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.copied[0], join_lines(output));
}

AURORA_TEST_CASE(a_command_with_no_output_hides_the_row_instead_of_graying_it) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed(empty_block_bytes("0"));
    h.right_click(1U, 1U);
    h.render();
    AURORA_TEST_REQUIRE(h.menu_open());
    // 只有两条底：第二条已是「粘贴」。条目整条不在，而不是画着一条点了没反应的灰项。
    AURORA_TEST_CHECK_EQ(h.menu_label(1U), borealis::ui::settings_label("menu.item.paste"));
    AURORA_TEST_CHECK_FALSE(h.click_menu_item(2U));
}

AURORA_TEST_CASE(an_open_block_has_no_copy_row_but_is_still_hoverable) {
    Harness h(true);  // 分隔线打开：开放块仍是块，悬停层认得它
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed(open_block_bytes({"partial output"}));
    h.hover(1U, 3U);
    AURORA_TEST_CHECK_EQ(h.count_top_edge(0U, h.separator_ink()), h.row_width(0U));
    h.right_click(1U, 3U);
    h.render();
    AURORA_TEST_REQUIRE(h.menu_open());
    AURORA_TEST_CHECK_EQ(h.menu_label(1U), borealis::ui::settings_label("menu.item.paste"));
}

// ============================================================
// 判据 5：失败底色只铺输出行，选区盖在它之上
// ============================================================

AURORA_TEST_CASE(the_failure_tint_covers_the_output_rows_and_the_selection_covers_the_tint) {
    Harness h;
    AURORA_TEST_REQUIRE(h.preflight());
    h.feed(completed_block_bytes({"boom", "trace"}, "1"));
    // 提示符与命令行回显都在第 0 行（`C` 之前），不是输出行：一枚红像素都不该有。
    AURORA_TEST_CHECK_EQ(h.count_color(0U, 0U, h.geometry().columns, h.failed_tint()), 0U);
    AURORA_TEST_CHECK(h.count_color(1U, 0U, h.geometry().columns, h.failed_tint()) > 0U);
    AURORA_TEST_CHECK(h.count_color(2U, 0U, h.geometry().columns, h.failed_tint()) > 0U);

    // 选区从第 1 行第 0 列拖到第 10 列：被选中的前几格里红底让位给选区色，选区外仍是红底。
    h.drag_select(1U, 0U, 10U);
    AURORA_TEST_CHECK(h.count_color(1U, 0U, 5U, h.selection_ink()) > 0U);
    AURORA_TEST_CHECK_EQ(h.count_color(1U, 0U, 5U, h.failed_tint()), 0U);
    AURORA_TEST_CHECK(h.count_color(1U, 20U, 30U, h.failed_tint()) > 0U);
}

// ============================================================
// 判据 6：分隔线的开关、悬停与上报档归属
// ============================================================

AURORA_TEST_CASE(the_separator_follows_the_switch_and_the_hover) {
    const std::vector<std::string> output{"line"};
    {
        Harness off(false);
        AURORA_TEST_REQUIRE(off.preflight());
        off.feed(completed_block_bytes(output, "0"));
        off.hover(1U, 3U);
        AURORA_TEST_CHECK_EQ(off.count_top_edge(0U, off.separator_ink()), 0U);
    }
    Harness on(true);
    AURORA_TEST_REQUIRE(on.preflight());
    on.feed(completed_block_bytes(output, "0"));
    on.hover(1U, 3U);
    // 线宽恒 1 物理像素、铺满首行：少画一列就少一格宽，厚一档就翻倍。
    AURORA_TEST_CHECK_EQ(on.count_top_edge(0U, on.separator_ink()), on.row_width(0U));
    on.hover(5U, 3U);  // 移出块（D 在列 0 时末行是输出行）
    AURORA_TEST_CHECK_EQ(on.count_top_edge(0U, on.separator_ink()), 0U);

    // 设置关掉立即摘：不等到下一次 Move（裁决 7.123 D6①）。
    on.hover(1U, 3U);
    AURORA_TEST_REQUIRE_EQ(on.count_top_edge(0U, on.separator_ink()), on.row_width(0U));
    auto options = TerminalView::InteractionOptions{};
    options.command_block_separator = false;
    on.apply_options(options);
    AURORA_TEST_CHECK_EQ(on.count_top_edge(0U, on.separator_ink()), 0U);
}

AURORA_TEST_CASE(pointer_reporting_decides_whether_the_separator_tracks_the_pointer) {
    // `?1000`：无键 Move 落本地（上报层不收），分隔线照跟。
    {
        Harness h(true);
        AURORA_TEST_REQUIRE(h.preflight());
        h.feed(completed_block_bytes({"line"}, "0"));
        h.feed("\x1b[?1000h");
        h.hover(1U, 3U);
        AURORA_TEST_CHECK_EQ(h.count_top_edge(0U, h.separator_ink()), h.row_width(0U));
    }
    // `?1003`：Move 整笔归远端，本层收不到落点——一条不更新的装饰线不许假装还指着指针。
    {
        Harness h(true);
        AURORA_TEST_REQUIRE(h.preflight());
        h.feed(completed_block_bytes({"line"}, "0"));
        h.feed("\x1b[?1003h");
        h.hover(1U, 3U);
        AURORA_TEST_CHECK_EQ(h.count_top_edge(0U, h.separator_ink()), 0U);
        AURORA_TEST_CHECK_FALSE(h.written().empty());  // 这一笔真的送去了远端
    }
}

// ============================================================
// 判据 7：ED 3 与 RIS 清块
// ============================================================

AURORA_TEST_CASE(erase_display_three_and_ris_drop_the_red_and_the_copy_row) {
    for (const char *clear : {"\x1b[3J", "\x1b" "c"}) {
        Harness h;
        AURORA_TEST_REQUIRE(h.preflight());
        h.feed(completed_block_bytes({"boom"}, "1"));
        AURORA_TEST_CHECK(h.count_color(1U, 0U, h.geometry().columns, h.failed_tint()) > 0U);
        h.right_click(1U, 1U);
        h.render();
        AURORA_TEST_REQUIRE(h.menu_open());
        AURORA_TEST_CHECK_EQ(h.menu_label(1U),
                             borealis::ui::settings_label("menu.item.copy_command_output"));
        h.dismiss_menu();

        h.feed(clear);
        AURORA_TEST_CHECK_EQ(h.count_color(1U, 0U, h.geometry().columns, h.failed_tint()), 0U);
        h.right_click(1U, 1U);
        h.render();
        AURORA_TEST_REQUIRE(h.menu_open());
        AURORA_TEST_CHECK_EQ(h.menu_label(1U), borealis::ui::settings_label("menu.item.paste"));
    }
}

// ============================================================
// 判据 8：回看不漂移——可见窗外仍可复制，存储窗外才不可
// ============================================================

AURORA_TEST_CASE(a_scrolled_block_still_copies_but_stops_drawing_the_separator) {
    Harness h(true);
    AURORA_TEST_REQUIRE(h.preflight());
    std::vector<std::string> output;
    output.reserve(30U);
    for (std::size_t index = 1U; index <= 30U; ++index) {
        output.push_back("out" + (index < 10U ? std::string{"0"} : std::string{}) + std::to_string(index));
    }
    h.feed(completed_block_bytes(output, "0"));
    h.feed("\r\n\r\n\r\n\r\n\r\n");  // 提示符行滚出可见窗，输出尾部仍在画面里
    h.hover(5U, 3U);
    // 块还在、命中还在，但首行已在窗顶之上：分隔线不画（无处可画）。
    AURORA_TEST_CHECK_EQ(h.count_everywhere(h.separator_ink()), 0U);
    h.right_click(5U, 3U);
    h.render();
    AURORA_TEST_REQUIRE(h.menu_open());
    AURORA_TEST_REQUIRE(h.click_menu_item(1U));
    h.render();
    AURORA_TEST_REQUIRE_EQ(h.copied.size(), 1U);
    AURORA_TEST_CHECK_EQ(h.copied[0], join_lines(output));  // 可见窗外的输出行照样在存储里

    // 再喂满一屏多：块整体被挤出存储，复制区间为空，条目随之消失。
    std::string flood;
    for (std::size_t index = 0U; index < 80U; ++index) {
        flood += "x\r\n";
    }
    h.feed(flood);
    h.right_click(5U, 3U);
    h.render();
    AURORA_TEST_REQUIRE(h.menu_open());
    AURORA_TEST_CHECK_EQ(h.menu_label(1U), borealis::ui::settings_label("menu.item.paste"));
}

#else

AURORA_TEST_CASE(headless_backend_is_not_enabled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_command_block
