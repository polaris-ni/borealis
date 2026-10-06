/// 测试类型: integration
/// 目标单元: src/ui/startup_notice.cpp（`SPEC.FEAT.PREF.07` 的降级对话框，判据文 §7 的 S12 / S13）
/// 测试说明: 断言的是**接线**而不是装载侧的降级算子（那一条由 `utest_config` 逐值守：先备份再回落、
///           备份未成功则拒绝落盘）。本件值得单独证的是九件事：
///           ① **`LoadOutcome` 四态里只有两条降级态弹**（S12①）：`FirstRun` 与 `Loaded` 既不建对话框
///              也不往宿主加浮层——正常装载的部分键回落另有界面两处文字表达（行尾角标与组顶说明），
///              在启动路径上打断用户是错的；
///           ② **一次启动最多弹一次**：幂等闸在返回值与 `overlay_count()` 两处都要判，第二次调用连
///              对话框句柄都不换（否则「用户刚关掉又弹出来」）；
///           ③ **报告里的三个结构化字段各画一行**：备份路径、两个版本号（文件自报的那一档与本仓支持的
///              那一档）、以及 `writes_refused` 那一行——后者是「不静默清空」在界面上的唯一表达，
///              备份未成功时本会话每一次落盘都会被拒绝，界面上不说就等于骗用户；
///           ④ **`LoadReport::message` 不上界面**（文件头④）：它是 ASCII 英文诊断，而 §4.3 第 14 条的
///              中文例外不含诊断文案，故任何一行显示文字里都不该出现它；
///           ⑤ **回落键清单是「一行一键 + 160 dp 视口 + 可滚」**：装载侧现状是那两条降级路径都在填该表
///              之前就返回（裁决 7.76⑤），故这一段只能由手工构造的 `LoadReport` 驱动——它是结构性不可达
///              而非未测，登记而不伪造现场；
///           ⑥⑦ **真实派发两腿**：点「知道了」只关框、不在派发栈内摘浮层，且一个字节也不写（损坏文件
///              逐字节原样）；遮罩那一点的认领者是 `Dialog` 本身（吸收点击，既不关框也不穿透到基础内容），
///              这与框架 `Dialog` 的「模态必须挡住下层交互」契约同源；
///           ⑧ **模态焦点作用域真的生效**：本件在 `show()` 之后自补一次 `push_scope`（文件头③——装载
///              不在任何派发栈内，框架的 `show()` 因此只走「仅置位」分支），Tab 因此走不出这个框；
///           ⑨ **析构把浮层与焦点作用域都还回去**（裁决 7.67 的时序：先还作用域、再放自持句柄、最后摘浮层）。
///
///           一条测试现场的必要构造：`OverlayHost` 的浮层序号从「基础内容之后」起算，`overlay_count()`
///           亦按 `children_.size() - 1` 回（**基础内容不计入**），故宿主必须带一个基础子节点（生产路径上
///           是终端视口，这里给一个 `Text`），而「弹了一层对话框」的读数是 `1` 不是 `2`。
///           显示串的读点用 `Text::accessibility_label()`
///           而不用 `display_text()`：前者在未绘制时退回按缺省 locale 现场解析（框架 `text.h` 自陈），故
///           ①②③④ 那几条不需要窗口就能判；绘制过的帧里它取的就是缓存的绘制串。中文一律由词条表在
///           运行期交出，本文件不写中文字面量（§4.3 第 14 条）。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/i18n/localized_string.h"
#include "aurora/widget/button.h"
#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/text.h"
#include "borealis/config/store.h"
#include "borealis/ui/palette.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/startup_notice.h"

namespace borealis::test_cases::itest_startup_notice {

namespace au = aurora;

using borealis::config::kSupportedSchemaVersion;
using borealis::config::LoadOutcome;
using borealis::config::LoadReport;
using borealis::config::Store;
using borealis::ui::RgbaColor;
using borealis::ui::StartupNotice;

namespace {

/// @brief 本轮用例的独占目录（框架按用例建目录并在结束时清理）。
[[nodiscard]] auto make_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path{aurora::testing::isolation::temp_dir()} / std::string{name};
}

auto write_file(const std::filesystem::path &path, std::string_view text) -> void {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
}

[[nodiscard]] auto read_file(const std::filesystem::path &path) -> std::string {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// @brief 路径 → UTF-8 文本（用例侧独立折一次，与实现件同一算式而非取它的输出）。
///
/// 不用 `path::string()`：Windows 上它按当前 ANSI 码页转换，非 ASCII 路径会碎成一串问号，判据就会
/// 跟着本机码页漂。
[[nodiscard]] auto u8_of(const std::filesystem::path &path) -> std::string {
    const std::u8string utf8 = path.u8string();
    return std::string{reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

/// @brief 造一份「只有字段差异」的降级报告（③⑤ 两类现场都要能单独挪一个字段）。
[[nodiscard]] auto corrupt_report() -> LoadReport {
    LoadReport report;
    report.outcome = LoadOutcome::RecoveredCorrupt;
    report.message = "config file is not readable json";
    return report;
}

/// @brief 收集一棵子树里全部 `Text` 的显示串（按树序）。
auto collect_labels(const au::Widget &widget, std::vector<std::string> &out) -> void {
    if (const auto *text = dynamic_cast<const au::Text *>(&widget)) {
        out.push_back(text->accessibility_label());
    }
    for (const au::Node &child : widget.child_nodes()) {
        collect_labels(child.widget(), out);
    }
}

[[nodiscard]] auto drawn_labels(const au::Widget &root) -> std::vector<std::string> {
    std::vector<std::string> out;
    collect_labels(root, out);
    return out;
}

/// @brief 判某控件是否在子树里（按指针身份）：⑥ 用它证明点到的那枚按钮确实属这个对话框。
[[nodiscard]] auto in_subtree(const au::Widget &widget, const au::Widget *target) -> bool {
    if (&widget == target) {
        return true;
    }
    for (const au::Node &child : widget.child_nodes()) {
        if (in_subtree(child.widget(), target)) {
            return true;
        }
    }
    return false;
}

/// @brief 在子树里找第一个某类控件（⑤ 的滚动容器：判的是它的量而不是它的可点性，故按树走而非按派发扫）。
[[nodiscard]] auto find_in_subtree(const au::Widget &widget, std::string_view type_name) -> const au::Widget * {
    if (type_name == std::string_view{widget.type_name()}) {
        return &widget;
    }
    for (const au::Node &child : widget.child_nodes()) {
        if (const au::Widget *found = find_in_subtree(child.widget(), type_name); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

/// @brief 造一个「只有基础内容」的浮层宿主（见文件头那条序号哨兵）。
[[nodiscard]] auto make_host(std::shared_ptr<au::Text> &out_base) -> std::shared_ptr<au::OverlayHost> {
    auto host = std::make_shared<au::OverlayHost>();
    out_base = std::make_shared<au::Text>(
        aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
    (void)host->add_overlay(au::Node{out_base});  // 占住子节点 [0]：基础内容
    return host;
}

/// @brief 判一行显示文字是否等于某词条就地解析出的那一条（③④ 的比对基准）。
[[nodiscard]] auto has_label(const std::vector<std::string> &labels, std::string_view key) -> bool {
    return std::find(labels.begin(), labels.end(), borealis::ui::settings_label(key)) != labels.end();
}

/// @brief 同上，但词条带模板参数（版本号与备份路径两行的比对基准）。
///
/// 参数由**用例侧**从 `LoadReport` 现算（而不是取实现件算好的串），故「参数没代进去」「代错一个」都会转红。
[[nodiscard]] auto has_label_with(const std::vector<std::string> &labels,
                                  std::string_view key,
                                  std::vector<au::LocalizedString> args) -> bool {
    return std::find(labels.begin(), labels.end(), borealis::ui::settings_label(key, std::move(args)))
        != labels.end();
}

[[nodiscard]] auto any_contains(const std::vector<std::string> &labels, std::string_view needle) -> bool {
    for (const std::string &label : labels) {
        if (label.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

AURORA_TEST_CASE(first_run_and_loaded_never_pop_the_dialog) {
    borealis::ui::install_settings_strings();
    std::shared_ptr<au::Text> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base);
    au::FocusManager focus;
    StartupNotice notice{*host, focus};

    for (LoadOutcome outcome : {LoadOutcome::FirstRun, LoadOutcome::Loaded}) {
        LoadReport report;
        report.outcome = outcome;
        report.rejected_keys = {"appearance.font_size_pt"};  // 部分键回落也不弹（S12①）
        AURORA_TEST_CHECK_FALSE(notice.show_if_needed(report));
        AURORA_TEST_CHECK_TRUE(notice.dialog() == nullptr);
        AURORA_TEST_CHECK_FALSE(notice.is_showing());
    }
    AURORA_TEST_CHECK_EQ(host->overlay_count(), 0U);  // 一层浮层也没加（基础内容不计入该计数）
    AURORA_TEST_CHECK_EQ(focus.scope_depth(), 0U);    // 没弹过就不该压过作用域
}

AURORA_TEST_CASE(the_writes_refused_row_is_drawn_only_when_backing_up_failed) {
    borealis::ui::install_settings_strings();
    const auto backup = make_path("a.json.corrupt-1");
    std::shared_ptr<au::Text> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base);
    au::FocusManager focus;
    StartupNotice notice{*host, focus};

    LoadReport backed_up = corrupt_report();
    backed_up.corrupt_backup = backup;
    AURORA_TEST_REQUIRE_TRUE(notice.show_if_needed(backed_up));
    const std::vector<std::string> without = drawn_labels(*notice.dialog());
    AURORA_TEST_REQUIRE_FALSE(without.empty());
    AURORA_TEST_CHECK_FALSE(has_label(without, "settings.startup.writes_refused"));

    // 第二份报告只能由第二个件驱动（幂等闸），故这里新建一件而不是复用上面那一个；
    // 除 `writes_refused` 之外的字段逐字相同，于是行数差恰等于那一行。
    std::shared_ptr<au::Text> base2;
    std::shared_ptr<au::OverlayHost> host2 = make_host(base2);
    StartupNotice second{*host2, focus};
    LoadReport refused = backed_up;
    refused.writes_refused = true;
    AURORA_TEST_REQUIRE_TRUE(second.show_if_needed(refused));
    const std::vector<std::string> with = drawn_labels(*second.dialog());
    AURORA_TEST_REQUIRE_EQ(with.size(), without.size() + 1U);
    AURORA_TEST_CHECK_TRUE(has_label(with, "settings.startup.writes_refused"));
}

#ifdef AURORA_BACKEND_HEADLESS

namespace {

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 600;

/// @brief 「这块底色仍属深色 chrome」的判据线（与 `itest_settings_panel` 同一条：两侧都有实测出处）。
constexpr std::uint8_t kChromeFloor = 0x60;

[[nodiscard]] auto chrome_is_dark(const RgbaColor &color) -> bool {
    return color.alpha == 0xFF && color.red <= kChromeFloor && color.green <= kChromeFloor
        && color.blue <= kChromeFloor;
}

/// @brief 带真实布局与真实指针派发的驱动台：对话框挂在无头窗口的场景根上。
class Harness {
  public:
    Harness() {
        host_ = make_host(base_);
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 装上本件（宿主与焦点权威都交给它）。
    [[nodiscard]] auto attach() -> std::unique_ptr<StartupNotice> {
        return std::make_unique<StartupNotice>(*host_, focus_);
    }

    /// @brief 弹框并排一帧（真实布局 + 真实绘制，无头帧缓冲）。
    auto show(StartupNotice &notice, const LoadReport &report) -> void {
        (void)notice.show_if_needed(report);
        render();
    }

    auto render() -> void {
        (void)window_.present_root(root_);
    }

    [[nodiscard]] auto base_widget() -> au::Widget * { return base_.get(); }

    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const std::vector<au::HitNode> chain = chain_at(x_dp, y_dp);
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    [[nodiscard]] auto chain_at(float x_dp, float y_dp) -> std::vector<au::HitNode> {
        return root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
    }

    /// @brief 派发链在这一点上是否包含该控件（⑤ 的滚动容器：更深的最末认领者是行文本）。
    [[nodiscard]] auto chain_contains(const au::Widget *widget, float x_dp, float y_dp) -> bool {
        for (const au::HitNode &node : chain_at(x_dp, y_dp)) {
            if (node.ptr == widget) {
                return true;
            }
        }
        return false;
    }

    /// @brief 量出「派发链在窗口坐标里确实交回这个控件」的矩形（1 dp 步进，含端点）。
    [[nodiscard]] auto reachable_box(au::Widget *widget, float x_dp, float y_dp) -> au::Rect {
        float top = y_dp;
        while (top - 1.0F >= 0.0F && hit(x_dp, top - 1.0F) == widget) {
            top -= 1.0F;
        }
        float bottom = y_dp;
        while (bottom + 1.0F < static_cast<float>(kWindowHeight) && hit(x_dp, bottom + 1.0F) == widget) {
            bottom += 1.0F;
        }
        float left = x_dp;
        while (left - 1.0F >= 0.0F && hit(left - 1.0F, y_dp) == widget) {
            left -= 1.0F;
        }
        float right = x_dp;
        while (right + 1.0F < static_cast<float>(kWindowWidth) && hit(right + 1.0F, y_dp) == widget) {
            right += 1.0F;
        }
        return au::Rect{.origin = au::Point{.x = left, .y = top},
                        .size = au::Size{.width = right - left + 1.0F, .height = bottom - top + 1.0F}};
    }

    /// @brief 在窗口内扫描，取出第一个指定类型的控件以及命中它的那一点与其窗口坐标下的可达框。
    ///
    /// 起点 212 dp：框体宽 480 dp 居中于 900 dp 窗口 ⇒ 框体在 210..690，让扫描从框内侧起，
    /// 免得把遮罩环带上的点当成框内控件。
    struct Spot {
        au::Widget *widget = nullptr;
        float x = 0.0F;
        float y = 0.0F;
        au::Rect box{};
    };

    [[nodiscard]] auto find_first(std::string_view type_name) -> Spot {
        for (float y = 4.0F; y < static_cast<float>(kWindowHeight) - 4.0F; y += 2.0F) {
            for (float x = 212.0F; x < static_cast<float>(kWindowWidth) - 212.0F; x += 2.0F) {
                au::Widget *widget = hit(x, y);
                if (widget == nullptr || type_name != std::string_view{widget->type_name()}) {
                    continue;
                }
                return Spot{.widget = widget, .x = x, .y = y, .box = reachable_box(widget, x, y)};
            }
        }
        return Spot{};
    }

    /// @brief 对话框内容盒在**窗口坐标**下的矩形。
    ///
    /// `Dialog::on_layout` 把居中盒写进 `Node::bounds_`（相对本控件内容区），`on_paint` 再给它加本控件
    /// 的全局原点，故像素取样的坐标空间就是这两者之和。取派发框而不是自己算居中：居中算式属框架。
    [[nodiscard]] auto content_box(StartupNotice &notice) const -> au::Rect {
        const au::Dialog *dialog = notice.dialog();
        AURORA_TEST_REQUIRE_MSG(dialog != nullptr, "no dialog to measure");
        AURORA_TEST_REQUIRE_MSG(!dialog->child_nodes().empty(), "the dialog carries no content node");
        const au::Rect local = dialog->child_nodes()[0].bounds();
        const au::Point origin = dialog->paint_bounds().origin;
        return au::Rect{.origin = au::Point{.x = origin.x + local.origin.x, .y = origin.y + local.origin.y},
                        .size = local.size};
    }

    [[nodiscard]] auto overlay_count() const -> std::size_t { return host_->overlay_count(); }

    [[nodiscard]] auto focus_manager() -> au::FocusManager & { return focus_; }

    auto pointer(au::MouseAction action, float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        event.action = action;
        (void)dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    auto click(float x_dp, float y_dp) -> void {
        pointer(au::MouseAction::Press, x_dp, y_dp);
        pointer(au::MouseAction::Release, x_dp, y_dp);
    }

    auto scroll(float x_dp, float y_dp, float delta_y) -> void {
        au::ScrollEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.delta_y = delta_y;
        (void)au::EventDispatcher::dispatch(root_.widget(), event);
    }

    /// @brief 当前帧缓冲里一点的色（无头 `scale` 恒 1.0，故窗口逻辑 dp 即物理像素下标）。
    [[nodiscard]] auto pixel(float x_dp, float y_dp) const -> RgbaColor {
        const std::uint8_t *data = window_.surface().data();
        if (data == nullptr) {
            return RgbaColor{0U, 0U, 0U, 0U};
        }
        const std::size_t index = (static_cast<std::size_t>(y_dp) * static_cast<std::size_t>(kWindowWidth)
                                   + static_cast<std::size_t>(x_dp))
                                  * 4U;
        return RgbaColor{data[index], data[index + 1U], data[index + 2U], data[index + 3U]};
    }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Text> base_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;  ///< 本驱动台私有的连击判定与指针捕获状态。
};

/// @brief 损坏文件的现场（真走 `Store` 的降级腿，而不是手搓 `LoadReport`）。
constexpr std::string_view kDamage = R"({"schema_version": 1, "appearance": )";
constexpr std::string_view kNewerFile = R"({"schema_version": 999, "appearance": {"theme": "tokyo-night"}})";

}  // namespace

AURORA_TEST_CASE(a_corrupt_config_pops_the_dialog_exactly_once_and_never_the_diagnostic_string) {
    borealis::ui::install_settings_strings();
    const auto file = make_path("corrupt_once.json");
    write_file(file, kDamage);
    const Store store{file};
    AURORA_TEST_REQUIRE_TRUE(store.report().outcome == LoadOutcome::RecoveredCorrupt);
    AURORA_TEST_REQUIRE_FALSE(store.report().message.empty());

    Harness h;
    std::unique_ptr<StartupNotice> notice = h.attach();
    h.show(*notice, store.report());
    AURORA_TEST_CHECK_TRUE(notice->is_showing());
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);  // 只有对话框那一层（基础内容不计入该计数）

    // 第二次调用连句柄都不换：幂等闸判的不只是「不再弹」，还有「不重建」。
    au::Dialog *first = notice->dialog();
    AURORA_TEST_REQUIRE(first != nullptr);
    AURORA_TEST_CHECK_FALSE(notice->show_if_needed(store.report()));
    AURORA_TEST_CHECK_TRUE(notice->dialog() == first);
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);

    const std::vector<std::string> labels = drawn_labels(*first);
    AURORA_TEST_REQUIRE_FALSE(labels.empty());
    // 两条降级态各取自己的标题词条（取错就是弹了另一件事）。
    AURORA_TEST_CHECK_TRUE(has_label(labels, "settings.startup.corrupt.title"));
    AURORA_TEST_CHECK_FALSE(has_label(labels, "settings.startup.version.title"));
    // ASCII 诊断一个字也不上界面（文件头④）：逐行拿它自己给出的整串做反判据。
    for (const std::string &label : labels) {
        AURORA_TEST_CHECK_FALSE(label.empty());
        AURORA_TEST_CHECK_TRUE(label.find(store.report().message) == std::string::npos);
    }
}

AURORA_TEST_CASE(the_backup_path_and_both_schema_versions_reach_the_display_text) {
    borealis::ui::install_settings_strings();

    const auto corrupt = make_path("corrupt_text.json");
    write_file(corrupt, kDamage);
    const Store corrupt_store{corrupt};
    AURORA_TEST_REQUIRE(corrupt_store.report().corrupt_backup.has_value());

    Harness corrupted;
    std::unique_ptr<StartupNotice> notice = corrupted.attach();
    corrupted.show(*notice, corrupt_store.report());
    const std::vector<std::string> corrupt_labels = drawn_labels(*notice->dialog());
    // 备份那一行由**整条路径**代进模板：算式与实现件同（`u8string()`）而各算一遍，取错形态即转红。
    AURORA_TEST_CHECK_TRUE(has_label_with(corrupt_labels,
                                          "settings.startup.backup",
                                          {au::LocalizedString{u8_of(*corrupt_store.report().corrupt_backup)}}));
    AURORA_TEST_CHECK_FALSE(any_contains(corrupt_labels, "{0}"));

    const auto newer = make_path("newer_text.json");
    write_file(newer, kNewerFile);
    const Store newer_store{newer};
    AURORA_TEST_REQUIRE_TRUE(newer_store.report().outcome == LoadOutcome::RecoveredVersion);
    AURORA_TEST_REQUIRE(newer_store.report().stored_schema_version.has_value());

    Harness outdates;
    std::unique_ptr<StartupNotice> second = outdates.attach();
    outdates.show(*second, newer_store.report());
    const std::vector<std::string> version_labels = drawn_labels(*second->dialog());
    // 两个版本号都得报出：只报本程序支持的那一档就说不清差了多少。
    AURORA_TEST_CHECK_TRUE(has_label_with(version_labels,
                                          "settings.startup.version.body",
                                          {au::LocalizedString{std::to_string(
                                               *newer_store.report().stored_schema_version)},
                                           au::LocalizedString{std::to_string(kSupportedSchemaVersion)}}));
    AURORA_TEST_CHECK_FALSE(any_contains(version_labels, "{0}"));
    AURORA_TEST_CHECK_FALSE(any_contains(version_labels, "{1}"));
    AURORA_TEST_CHECK_TRUE(has_label(version_labels, "settings.startup.version.title"));
    AURORA_TEST_CHECK_FALSE(has_label(version_labels, "settings.startup.corrupt.title"));
    AURORA_TEST_CHECK_FALSE(has_label(version_labels, "settings.startup.writes_refused"));
}

AURORA_TEST_CASE(the_rejected_key_list_draws_one_row_per_key_inside_a_scrolled_viewport) {
    borealis::ui::install_settings_strings();
    Harness h;
    std::unique_ptr<StartupNotice> notice = h.attach();

    // 装载侧现状到不了这一段（两条降级路径都在填该表之前返回，裁决 7.76⑤），故现场手工构造。
    LoadReport report = corrupt_report();
    for (int i = 0; i < 12; ++i) {
        report.rejected_keys.push_back("appearance.rejected_key_" + std::to_string(i));
    }
    h.show(*notice, report);

    const auto *scroll = dynamic_cast<const au::Scroll *>(find_in_subtree(*notice->dialog(), "Scroll"));
    AURORA_TEST_REQUIRE_MSG(scroll != nullptr, "the rejected-key list is not a Scroll viewport");
    // 视口 160 dp：不锁高就会随内容长到溢出窗口（`Scroll` 自身取父约束给的视口尺寸，其头注自陈）。
    AURORA_TEST_CHECK_NEAR(scroll->size().height, 160.0F, 1.0F);
    const std::optional<au::AccessibilityScrollRange> range = scroll->accessibility_scroll();
    AURORA_TEST_REQUIRE(range.has_value());
    AURORA_TEST_CHECK_NEAR(range->viewport, 160.0F, 1.0F);
    AURORA_TEST_REQUIRE_GT(range->content, range->viewport);
    AURORA_TEST_REQUIRE_GT(range->max, 0.0);

    // 「可滚」要有派发而不是只有读数：滚轮落进那一段真的推进偏移。
    const float center_x = static_cast<float>(kWindowWidth) * 0.5F;
    const au::Rect viewport_box = scroll->paint_bounds();
    const float center_y = viewport_box.origin.y + viewport_box.size.height * 0.5F;
    AURORA_TEST_REQUIRE_MSG(h.chain_contains(scroll, center_x, center_y),
                            "the sampled point is not inside the list viewport");
    AURORA_TEST_CHECK_NEAR(scroll->offset_y(), 0.0F, 0.01F);  // 前提：还没滚
    h.scroll(center_x, center_y, -64.0F);
    AURORA_TEST_CHECK_GT(scroll->offset_y(), 0.0F);

    // 一行一键且**全量**在树里：视口锁高不等于把清单截断。
    const std::vector<std::string> labels = drawn_labels(*scroll);
    AURORA_TEST_REQUIRE_EQ(labels.size(), report.rejected_keys.size());
    for (std::size_t i = 0; i < report.rejected_keys.size(); ++i) {
        AURORA_TEST_CHECK_EQ(labels[i], report.rejected_keys[i]);
    }
    AURORA_TEST_CHECK_TRUE(has_label(drawn_labels(*notice->dialog()), "settings.startup.rejected"));
}

AURORA_TEST_CASE(a_real_click_on_the_ack_button_closes_the_dialog_without_writing_anything) {
    borealis::ui::install_settings_strings();
    const auto file = make_path("ack.json");
    write_file(file, kDamage);
    const Store store{file};

    Harness h;
    std::unique_ptr<StartupNotice> notice = h.attach();
    h.show(*notice, store.report());

    Harness::Spot spot = h.find_first("Button");
    AURORA_TEST_REQUIRE_MSG(spot.widget != nullptr, "the ack button is not reachable through the dispatch chain");
    AURORA_TEST_REQUIRE(in_subtree(*notice->dialog(), spot.widget));
    h.click(spot.box.origin.x + spot.box.size.width * 0.5F, spot.box.origin.y + spot.box.size.height * 0.5F);

    AURORA_TEST_CHECK_FALSE(notice->is_showing());
    // 关框只在派发栈内改可见性：摘浮层要等出栈（`remove_overlay` 会析构正在派发的子树）。
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);
    // 「不静默清空」的另一半：整条启动提示路径一个字节也不写，损坏文件仍是唯一现场。
    AURORA_TEST_CHECK_EQ(read_file(file), std::string{kDamage});
    AURORA_TEST_REQUIRE(store.report().corrupt_backup.has_value());
    AURORA_TEST_CHECK_EQ(read_file(*store.report().corrupt_backup), std::string{kDamage});
}

AURORA_TEST_CASE(a_click_on_the_scrim_is_absorbed_and_neither_closes_nor_penetrates) {
    borealis::ui::install_settings_strings();
    Harness h;
    std::unique_ptr<StartupNotice> notice = h.attach();
    LoadReport report = corrupt_report();
    report.corrupt_backup = make_path("scrim.json.corrupt-1");
    h.show(*notice, report);

    // 框体宽 480 dp 居中于 900 dp ⇒ 左右各 210 dp 遮罩环带，取其中一点。
    const float scrim_x = 40.0F;
    const float scrim_y = static_cast<float>(kWindowHeight) * 0.5F;
    au::Widget *claimed = h.hit(scrim_x, scrim_y);
    AURORA_TEST_REQUIRE_MSG(claimed == static_cast<au::Widget *>(notice->dialog()),
                            "the scrim point is not claimed by the dialog itself");
    AURORA_TEST_CHECK_TRUE(claimed != h.base_widget());  // 不穿透到基础内容
    h.click(scrim_x, scrim_y);
    AURORA_TEST_CHECK_TRUE(notice->is_showing());
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);
}

AURORA_TEST_CASE(the_modal_scope_keeps_tab_focus_inside_the_box) {
    borealis::ui::install_settings_strings();
    Harness h;
    std::unique_ptr<StartupNotice> notice = h.attach();
    h.show(*notice, corrupt_report());

    au::FocusManager &focus = h.focus_manager();
    // 文件头③：装载不在派发栈内，框架 `show()` 只置位，故作用域由本件自补——这一句判的就是那一补。
    AURORA_TEST_REQUIRE_EQ(focus.scope_depth(), 1U);
    au::Widget *focused = focus.focused();
    AURORA_TEST_REQUIRE_MSG(focused != nullptr, "the dialog did not take keyboard focus");
    AURORA_TEST_REQUIRE(in_subtree(*notice->dialog(), focused));
    AURORA_TEST_CHECK_TRUE(std::string_view{focused->type_name()} == "Button");

    // 框内只有那一枚可停点（`Text` 与容器的 `wants_focus()` 走 `has_input_semantics()`），故 Tab 走不出去。
    for (int i = 0; i < 4; ++i) {
        AURORA_TEST_CHECK_TRUE(focus.move_focus(au::FocusDirection::Forward));
        AURORA_TEST_CHECK_EQ(focus.focused(), focused);
    }
    AURORA_TEST_CHECK_FALSE(focus.has_focus(h.base_widget()));
}

AURORA_TEST_CASE(destroying_the_notice_while_open_returns_the_overlay_and_the_scope) {
    borealis::ui::install_settings_strings();
    Harness h;
    {
        std::unique_ptr<StartupNotice> notice = h.attach();
        h.show(*notice, corrupt_report());
        AURORA_TEST_REQUIRE(notice->is_showing());
        AURORA_TEST_CHECK_EQ(h.focus_manager().scope_depth(), 1U);
        AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);
    }  // 用户还没点「知道了」就退出：析构要把浮层与焦点作用域都还回去。
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 0U);
    AURORA_TEST_CHECK_EQ(h.focus_manager().scope_depth(), 0U);
}

AURORA_TEST_CASE(the_box_paints_on_the_chrome_background_and_not_the_framework_light_default) {
    borealis::ui::install_settings_strings();
    Harness h;
    std::unique_ptr<StartupNotice> notice = h.attach();
    LoadReport report = corrupt_report();
    report.corrupt_backup = make_path("paint.json.corrupt-1");
    h.show(*notice, report);

    // 取点在框体留白带（内边距 24 dp 之内、任何一行文本之外），判的是本件交进修饰链的那份底色。
    const au::Rect box = h.content_box(*notice);
    const float box_x = box.origin.x + box.size.width * 0.5F;
    const float box_y = box.origin.y + 12.0F;
    AURORA_TEST_REQUIRE_MSG(box_x > 0.0F && box_y > 0.0F, "the dialog box is not laid out");
    const RgbaColor inside = h.pixel(box_x, box_y);
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(inside));

    // 与遮罩环带同纵坐标对比：底色的存在性由此可判——去掉 `.background(...)` 两处读数会同值。
    const RgbaColor scrim = h.pixel(40.0F, box_y);
    AURORA_TEST_CHECK_TRUE(scrim.alpha == 0xFF);
    const int delta = std::abs(static_cast<int>(inside.red) - static_cast<int>(scrim.red))
        + std::abs(static_cast<int>(inside.green) - static_cast<int>(scrim.green))
        + std::abs(static_cast<int>(inside.blue) - static_cast<int>(scrim.blue));
    AURORA_TEST_CHECK_GT(delta, 8);
}

#else

AURORA_TEST_CASE(a_corrupt_config_pops_the_dialog_exactly_once_and_never_the_diagnostic_string) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_backup_path_and_both_schema_versions_reach_the_display_text) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_rejected_key_list_draws_one_row_per_key_inside_a_scrolled_viewport) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(a_real_click_on_the_ack_button_closes_the_dialog_without_writing_anything) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(a_click_on_the_scrim_is_absorbed_and_neither_closes_nor_penetrates) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_modal_scope_keeps_tab_focus_inside_the_box) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(destroying_the_notice_while_open_returns_the_overlay_and_the_scope) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_box_paints_on_the_chrome_background_and_not_the_framework_light_default) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif  // AURORA_BACKEND_HEADLESS

}  // namespace borealis::test_cases::itest_startup_notice
