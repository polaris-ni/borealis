// ============================================================
// 字节监视面板集成用例（tests/integration/itest_session_monitor_panel.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 两条延后子项的面板腿（UI_SERIAL_MONITOR.draft.md §6 批 3 那五行判据）：
// 行表随环刷新、时间戳开关联动、暂停中不读环、清空是唯一止血、丢弃留痕上屏、绑定与常驻声明；
// 二期（稿 §9）另四腿：历史超过折带窗口仍可回看、离底冻结与回底恢复、「回到最新」双解、冻结中清空；
// 三期（稿 §10）再加六腿：复制交整环而不是窗口、导出先问路径再交同一份文本、取消与空环整条不跑、
// 写盘失败上屏中性那句、暂停中导出的是这一屏、两枚按钮只在对应腿在场时画；
// 四期（稿 §11）再加七腿：第三枚开关真点即换行形态且拨回来是同一份料源、文本档复制交出去的是屏上
// 那一份文本、解码腿按 hook 报的那条腿走、hook 缺席整条回落 UTF-8 不崩、冻结中换档回到这份料源的末尾、
// 暂停中换档只重画不读环、暂停中换档在两档行数不等时复位到这份料源的末尾。
// 最后那一条是 D34②那条重排腿**唯一**的证人，故刻意用 62 字节的宽行：短行在两档折出同样的行数，
// 「沿用旧行号」与「按新档重算」给出同一份窗口，把那条腿整段删掉 24 案仍全绿（批 C 变异自证实测到）。
// 驱动台形态照 itest_tunnel_panel 的 Harness（present_root 排真实布局 + hit_test_chain 取命中 +
// EventDispatcher 发真事件），精简到本件用得到的那几条腿。
//
// 判据一律走面板交出的观测面（visible_rows / notice / timestamp_enabled / text_view_enabled / is_open），
// 浮层树只用于「按钮与开关真点得到」这一条派发腿——行区重建会挪位置，按坐标断言会把排版改动误报成功能回归。
//
// 「快照被问了几次」（`snapshot_calls`）是本件独有的证人：暂停这条腿的判据不是「画面没变」，而是
// 「压根没去读环」——只看行表的话，「读了但算出同一份」与「没读」两种实现都给同一个读数。
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/widget/button.h"
#include "aurora/widget/lazy_list.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "borealis/session/raw_tap.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/session_monitor_format.h"
#include "../../src/ui/session_monitor_panel.h"
#include "../../src/ui/settings_i18n.h"

namespace borealis::test_cases::itest_session_monitor_panel {

namespace {

namespace session = borealis::session;
namespace ui = borealis::ui;
namespace au = aurora;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 760;

/// @brief 一帧环投影 + 绑定串 + 七条腿的调用留痕。
struct Probe {
    ui::SessionMonitorPanel::Snapshot snapshot{};
    std::string binding;
    std::size_t snapshot_calls = 0U;
    std::size_t clear_calls = 0U;
    std::size_t binding_calls = 0U;
    // ---- 四期文本视图的解码腿（稿 §11 D28①）：交的是「那条会话构造期折好的腿名」 ----
    std::string encoding = "UTF-8";    ///< 面板按它现造解码器；换会话即换值，改设置不换。
    std::size_t encoding_calls = 0U;
    bool provide_encoding_leg = true;  ///< 关掉即装配层没装这条腿：文本档该整条回落 UTF-8。
    // ---- 三期三条「取出去」腿（稿 §10.2 D22①）：面板只管交文本，成败由这里的返回值演 ----
    std::size_t copy_calls = 0U;
    std::size_t pick_calls = 0U;
    std::size_t export_calls = 0U;
    std::string copied_text;   ///< 交给剪贴板腿的那一份。
    std::string export_path;   ///< 写盘腿收到的路径（＝`picked_path` 的现值）。
    std::string exported_text;  ///< 写盘腿收到的文本（应与 `copied_text` 一字不差，稿 D19①）。
    std::string picked_path = "D:/dumps/serial.txt";  ///< 空串＝用户在原生对话框里点了取消。
    std::optional<std::string> export_reason;         ///< 有值＝装配腿那条写盘链失败。
    bool provide_copy_leg = true;    ///< 关掉即「装配层没装这条腿」：那枚按钮不该画出来。
    bool provide_export_leg = true;  ///< 同上，管 `pick_export_path` 与 `export_text` 两条。

    /// @brief 环里多一条 run：`revision` 是面板的早退基准（D17①），替身必须像 `RawTap` 一样每改必推。
    auto record(session::RawRun run) -> void {
        snapshot.runs.push_back(std::move(run));
        ++snapshot.revision;
    }

    [[nodiscard]] auto hooks() -> ui::SessionMonitorPanel::Hooks {
        ui::SessionMonitorPanel::Hooks out;
        out.snapshot = [this]() -> ui::SessionMonitorPanel::Snapshot {
            ++snapshot_calls;
            return snapshot;
        };
        out.clear = [this]() -> void {
            ++clear_calls;
            snapshot = ui::SessionMonitorPanel::Snapshot{};
        };
        out.binding_label = [this]() -> std::string {
            ++binding_calls;
            return binding;
        };
        if (provide_encoding_leg) {
            out.encoding_name = [this]() -> std::string {
                ++encoding_calls;
                return encoding;
            };
        }
        if (provide_copy_leg) {
            out.copy_text = [this](std::string text) -> void {
                ++copy_calls;
                copied_text = std::move(text);
            };
        }
        if (provide_export_leg) {
            out.pick_export_path = [this]() -> std::string {
                ++pick_calls;
                return picked_path;
            };
            out.export_text = [this](const std::string &path, std::string text)
                -> std::optional<std::string> {
                ++export_calls;
                export_path = path;
                exported_text = std::move(text);
                return export_reason;
            };
        }
        return out;
    }
};

/// @brief 一条 run：给定方向、时刻与 ASCII 字面量折出的字节。
[[nodiscard]] auto make_run(const session::RawDirection direction, const std::int64_t timestamp_ms,
                            const std::string &text) -> session::RawRun {
    auto bytes = std::vector<std::byte>{};
    for (const char ch : text) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return session::RawRun{.direction = direction, .timestamp_ms = timestamp_ms, .bytes = std::move(bytes)};
}

/// @brief 两条缺省 run：一条入向一行设备文本、一条出向短命令（够占两行、又不撞 24 行上限）。
[[nodiscard]] auto make_probe() -> Probe {
    Probe probe;
    probe.snapshot.runs = {make_run(session::RawDirection::Received, 1'700'000'000'000LL, "OK\r\n"),
                           make_run(session::RawDirection::Transmitted, 1'700'000'001'000LL, "AT\r\n")};
    probe.snapshot.revision = 1U;  // 环里已有两条：revision 不能停在「空环」的 0。
    probe.binding = "srv /dev/ttyUSB0";
    return probe;
}

struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
};

/// @brief 无头窗口 + 真布局 + 真指针派发的驱动台（itest_tunnel_panel 同族精简腿）。
class Harness {
  public:
    Harness() {
        host_ = std::make_shared<au::OverlayHost>();
        base_ = std::make_shared<au::Text>(
            aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
        (void)host_->add_overlay(au::Node{base_});  // 占住子节点 [0]：基础内容。
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;

    [[nodiscard]] auto attach(Probe &probe) -> std::unique_ptr<ui::SessionMonitorPanel> {
        return std::make_unique<ui::SessionMonitorPanel>(*host_, probe.hooks());
    }
    auto render() -> void {
        static_cast<void>(window_.present_root(root_));
    }

    /// @brief 打开面板并排两帧（浮层入树 + 布局落 bounds）。
    auto open(ui::SessionMonitorPanel &panel) -> void {
        panel.open();
        render();
        render();
    }

    /// @brief 排帧后走一帧状态泵（环里多了字节经这里进行表）。
    auto tick(ui::SessionMonitorPanel &panel) -> void {
        panel.tick();
        render();
        render();
    }

    [[nodiscard]] auto chain_at(float x_dp, float y_dp) -> std::vector<au::HitNode> {
        return root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
    }

    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const auto chain = chain_at(x_dp, y_dp);
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    auto pointer(au::MouseAction action, float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        event.action = action;
        static_cast<void>(dispatcher_.dispatch_mouse(root_.widget(), event, &focus_));
    }

    auto click(float x_dp, float y_dp) -> void {
        pointer(au::MouseAction::Press, x_dp, y_dp);
        pointer(au::MouseAction::Release, x_dp, y_dp);
    }

    /// @brief 单击一个 Spot（先确认派发链在这一点仍交回它——点空不算点中）。
    auto click(const HitSpot &spot) -> void {
        AURORA_TEST_REQUIRE_MSG(spot.widget != nullptr, "the control is not dispatch-reachable");
        AURORA_TEST_REQUIRE_MSG(hit(spot.x, spot.y) == spot.widget,
                                "the probe point no longer claims the widget");
        click(spot.x, spot.y);
    }

    /// @brief 扫描派发链，找第 nth 个满足 accept 的控件（行序＝扫描序，4 dp 步长）。
    [[nodiscard]] auto find(std::string_view type_name, std::size_t nth,
                            const std::function<bool(au::Widget *)> &accept = {}) -> HitSpot {
        std::vector<au::Widget *> seen;
        for (float y = 4.0F; y < static_cast<float>(kWindowHeight) - 4.0F; y += 4.0F) {
            for (float x = 20.0F; x < static_cast<float>(kWindowWidth) - 20.0F; x += 4.0F) {
                au::Widget *widget = hit(x, y);
                if (widget == nullptr || type_name != widget->type_name() ||
                    std::find(seen.begin(), seen.end(), widget) != seen.end()) {
                    continue;
                }
                if (accept && !accept(widget)) {
                    continue;
                }
                seen.push_back(widget);
                if (seen.size() == nth) {
                    return HitSpot{.widget = widget, .x = x, .y = y};
                }
            }
        }
        return HitSpot{};
    }

    /// @brief 按解析后的显示标签找按钮（accessibility_label 与绘制同源，见 button.h i18n 契约）。
    [[nodiscard]] auto find_button(const std::string &label, std::size_t nth = 1) -> HitSpot {
        return find("Button", nth, [&label](au::Widget *widget) -> bool {
            const auto *button = dynamic_cast<const au::Button *>(widget);
            return button != nullptr && button->accessibility_label() == label;
        });
    }

    /// @brief 按显示标签找正文 Text（留痕与常驻声明的可见性判据走这条）。
    [[nodiscard]] auto find_text(const std::string &label) -> HitSpot {
        return find("Text", 1, [&label](au::Widget *widget) -> bool {
            const auto *text = dynamic_cast<const au::Text *>(widget);
            return text != nullptr && text->accessibility_label() == label;
        });
    }

    [[nodiscard]] auto overlay_count() const -> std::size_t {
        return host_->overlay_count();
    }

    /// @brief 树里当前那只行区（行区每次重建都会换新对象，故每步现取，不缓存句柄）。
    [[nodiscard]] auto find_list() -> au::LazyList * {
        return const_cast<au::LazyList *>(find_list_in(root_.widget()));
    }

  private:
    [[nodiscard]] static auto find_list_in(const au::Widget &widget) -> const au::LazyList * {
        if (const auto *list = dynamic_cast<const au::LazyList *>(&widget); list != nullptr) {
            return list;
        }
        const au::LazyList *found = nullptr;
        widget.for_each_child([&found](const au::Widget &child) -> void {
            if (found == nullptr) {
                found = find_list_in(child);
            }
        });
        return found;
    }

    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        static_cast<void>(surface->begin_frame(kWindowWidth, kWindowHeight));
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Text> base_;
    au::Node root_;
    au::FocusManager focus_;
    aurora::EventDispatcher dispatcher_;
};

/// @brief 时间戳前缀那 15 个字符（`[HH:MM:SS.mmm] `）在不在行上——两档开关各自的判据。
[[nodiscard]] auto has_prefix(const std::vector<ui::MonitorRow> &rows) -> bool {
    for (const auto &row : rows) {
        if (!row.timestamp_prefix.empty()) {
            return true;
        }
    }
    return false;
}

/// @brief `R007` 这种三位零填充行标签：`line_run` 与行文本尾栏共用这一份，免得两边各拼一次。
[[nodiscard]] auto line_tag(int index) -> std::string {
    auto tag = std::string{"R"};
    tag += static_cast<char>('0' + (index / 100) % 10);
    tag += static_cast<char>('0' + (index / 10) % 10);
    tag += static_cast<char>('0' + index % 10);
    return tag;
}

/// @brief 一行 `R000\r\n`：折出来恰好一行，ASCII 尾栏是 `|R000..|`（行号可直读）。
[[nodiscard]] auto line_run(int index, std::int64_t timestamp_ms) -> session::RawRun {
    return make_run(session::RawDirection::Received, timestamp_ms, line_tag(index) + "\r\n");
}

auto append_lines(Probe &probe, int first, int count) -> void {
    for (int index = first; index < first + count; ++index) {
        probe.record(line_run(index, 1'700'000'000'000LL + index));
    }
}

auto fill_lines(Probe &probe, int count) -> void { append_lines(probe, 0, count); }

/// @brief 一条**超过文本档字节预算**的设备行（`R000` + 56 个 `x` + CRLF＝62 字节）。
///
/// 两档的行号坐标系因此真的分开：hex 档 8 字节/行 ⇒ ⌈62/8⌉=8 行，文本档 36 字节/行 ⇒ ⌈62/36⌉=2 行。
/// 「换档即重算行数」那条腿（稿 D34②）只有在两档行数不等时才有证人——短行两档行数相同，
/// 沿用旧行号与重排给出同一份窗口，变异注入打不红（批 C 实测：去掉 `rebase_rows_` 分支 24 案全绿）。
[[nodiscard]] auto wide_line_run(int index, std::int64_t timestamp_ms) -> session::RawRun {
    return make_run(session::RawDirection::Received, timestamp_ms,
                    line_tag(index) + std::string(56U, 'x') + "\r\n");
}

auto fill_wide_lines(Probe &probe, int count) -> void {
    for (int index = 0; index < count; ++index) {
        probe.record(wide_line_run(index, 1'700'000'000'000LL + index));
    }
}

/// @brief 行表里有没有带这个行号标签的行（文本档的 `dump` 直接含标签，不看 hex 尾栏）。
[[nodiscard]] auto shows_tag(const std::vector<ui::MonitorRow> &rows, int index) -> bool {
    const std::string tag = line_tag(index);
    return std::any_of(rows.begin(), rows.end(), [&tag](const ui::MonitorRow &row) -> bool {
        return ui::row_text(row).find(tag) != std::string::npos;
    });
}

/// @brief 这一行是不是 `line_tag(index)`（比十六进制段好读的尾栏判据）。
[[nodiscard]] auto row_is(const ui::MonitorRow &row, int index) -> bool {
    return ui::row_text(row).find("|" + line_tag(index) + "..|") != std::string::npos;
}

/// @brief 行表里有没有这一行。
[[nodiscard]] auto shows_line(const std::vector<ui::MonitorRow> &rows, int index) -> bool {
    return std::any_of(rows.begin(), rows.end(), [index](const ui::MonitorRow &row) -> bool {
        return row_is(row, index);
    });
}

/// @brief 把行区滚到顶再排两帧：首帧判冻结并保位重画，次帧才按「可见首行 - 16 行」重折窗口。
auto scroll_to_top(Harness &h, ui::SessionMonitorPanel &panel) -> void {
    if (auto *list = h.find_list(); list != nullptr) {
        list->set_scroll_offset(0.0F);
    }
    h.tick(panel);
    h.tick(panel);
}

/// @brief 交出去的文本里有几行（按 `\n` 数，**不看**结尾有没有换行）。
///
/// 结尾多带一个换行就会多算出一行，正好被「与总行数相等」那条判据抓住——所以这里不给「末尾剪掉」的通融。
[[nodiscard]] auto dump_line_count(const std::string &text) -> std::size_t {
    if (text.empty()) {
        return 0U;
    }
    std::size_t count = 1U;
    for (const char ch : text) {
        if (ch == '\n') {
            ++count;
        }
    }
    return count;
}

/// @brief 第 index 行在不在交出去的文本里（`|R000..|` 那种直读尾栏）。
[[nodiscard]] auto dump_shows_line(const std::string &text, int index) -> bool {
    return text.find("|" + line_tag(index) + "..|") != std::string::npos;
}

/// @brief 文本档那一行的正文段（`MonitorRow::dump`）：两空格 + 行号标签，没有竖线尾栏。
///
/// 换档后 `|R000..|` 那种锚点就不存在了（那是 hex 段的界），文本段的界是行尾本身（稿 §11.3）。
[[nodiscard]] auto text_row_is(const ui::MonitorRow &row, int index) -> bool {
    return row.dump == "  " + line_tag(index);
}

/// @brief 真点第三枚开关（文本视图，稿 D26①）并确认它翻了位。
///
/// 序号 3 是刻意的：集成用例按 `find("Switch", N)` 的树序取件，前两枚的序号不能因新增而挪位。
auto toggle_text_view(Harness &h) -> void {
    auto spot = h.find("Switch", 3);
    AURORA_TEST_REQUIRE_MSG(spot.widget != nullptr, "the text view switch is not reachable");
    auto *sw = dynamic_cast<au::Switch *>(spot.widget);
    AURORA_TEST_REQUIRE(sw != nullptr);
    const bool before = sw->value();
    h.click(spot);
    AURORA_TEST_CHECK(sw->value() != before);
}

}  // namespace

// ------------------------------------------------------------
// 判据①（稿 §4 标号 1／§6 批 3 第一行）：行表随环刷新，面板不在场时零成本
// ------------------------------------------------------------
AURORA_TEST_CASE(rows_follow_the_ring_across_ticks) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK(rows[0].direction_label == "RX");
    AURORA_TEST_CHECK(rows[1].direction_label == "TX");
    // 行文本与折算件同源：面板不自己拼第二份十六进制（`row_text` 就是那一行该显示的东西）。
    AURORA_TEST_CHECK(ui::row_text(rows[0]).find("4F 4B 0D 0A") != std::string::npos);
    AURORA_TEST_CHECK(ui::row_text(rows[0]).find("|OK..|") != std::string::npos);

    // 环里多一条 run ⇒ 下一帧多一行（latest-value，不做增量游标）。
    probe.record(make_run(session::RawDirection::Received, 1'700'000'002'000LL, "DONE\r\n"));
    h.tick(*panel);
    rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);

    // 面板关着 tick 是空操作：连环都不问（稿 §5「采集常驻、显示按需」的那半）。
    panel->close();
    h.render();
    const std::size_t calls_before = probe.snapshot_calls;
    probe.record(make_run(session::RawDirection::Received, 1'700'000'003'000LL, "X"));
    panel->tick();
    AURORA_TEST_CHECK(!panel->is_open());
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_before);
}

// ------------------------------------------------------------
// 判据②（稿 D6①）：时间戳开关真点即联动——前缀整列消失，行内容本身不动
// ------------------------------------------------------------
AURORA_TEST_CASE(timestamp_switch_rewrites_the_rows) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_CHECK_TRUE(panel->timestamp_enabled());  // 缺省开（D6①）。
    AURORA_TEST_CHECK_TRUE(has_prefix(panel->visible_rows()));

    auto switch_spot = h.find("Switch", 1);
    AURORA_TEST_REQUIRE_MSG(switch_spot.widget != nullptr, "the timestamp switch is not reachable");
    auto *sw = dynamic_cast<au::Switch *>(switch_spot.widget);
    AURORA_TEST_REQUIRE(sw != nullptr);
    AURORA_TEST_CHECK_TRUE(sw->value());

    h.click(switch_spot);
    AURORA_TEST_CHECK_FALSE(sw->value());
    // 回调只改状态、不当场重建（§4.5 第 25 条）：重建排在下一帧。
    h.tick(*panel);
    AURORA_TEST_CHECK_FALSE(panel->timestamp_enabled());
    const auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 2U);
    AURORA_TEST_CHECK_FALSE(has_prefix(rows));
    AURORA_TEST_CHECK(rows[0].direction_label == "RX");
    AURORA_TEST_CHECK(ui::row_text(rows[0]).find("4F 4B 0D 0A") != std::string::npos);
}

// ------------------------------------------------------------
// 判据③（稿 §1「暂停刷新」）：暂停中不读环，改档仍补一次重画
// ------------------------------------------------------------
AURORA_TEST_CASE(pause_stops_reading_the_ring) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto switch_spot = h.find("Switch", 2);
    AURORA_TEST_REQUIRE_MSG(switch_spot.widget != nullptr, "the pause switch is not reachable");
    auto *sw = dynamic_cast<au::Switch *>(switch_spot.widget);
    AURORA_TEST_REQUIRE(sw != nullptr);
    AURORA_TEST_CHECK_FALSE(sw->value());  // 缺省关（D7①）。

    h.click(switch_spot);
    AURORA_TEST_CHECK_TRUE(sw->value());
    h.tick(*panel);

    const std::size_t calls_while_painted = probe.snapshot_calls;
    const std::vector<ui::MonitorRow> frozen = panel->visible_rows();
    probe.record(make_run(session::RawDirection::Received, 1'700'000'004'000LL, "MORE\r\n"));
    h.tick(*panel);
    // 「压根没去读环」而不是「读了但算出同一份」：这条断言把两种实现分开。
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_while_painted);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), frozen.size());
}

// ------------------------------------------------------------
// 判据④（稿 D7①）：清空是本期唯一的止血动作
// ------------------------------------------------------------
AURORA_TEST_CASE(clear_button_is_the_only_bleed_stop) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 2U);

    // 关面板**不**清环，也不呼清空腿（D7①：重开看到的是历史）。
    panel->close();
    h.render();
    panel->open();
    h.render();
    h.render();
    AURORA_TEST_CHECK_EQ(probe.clear_calls, 0U);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 2U);

    auto clear_spot = h.find_button(ui::settings_label("session.monitor.action.clear"));
    AURORA_TEST_REQUIRE_MSG(clear_spot.widget != nullptr, "the clear button is not dispatch-reachable");
    h.click(clear_spot);
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.clear_calls, 1U);
    AURORA_TEST_CHECK_TRUE(panel->visible_rows().empty());
    // 空态那句画出来了（判据走派发链：本件不许把「等待数据」写成第二处措辞）。
    auto empty_spot = h.find_text(ui::settings_label("session.monitor.empty"));
    AURORA_TEST_REQUIRE_MSG(empty_spot.widget != nullptr, "the empty state is not drawn");
}

// ------------------------------------------------------------
// 判据⑤（稿 D4①）：丢弃必须上屏——不报这一句，「最早那行」就会被当成「设备最早发的那行」
// ------------------------------------------------------------
AURORA_TEST_CASE(dropped_bytes_are_shown_and_cleared_with_the_ring) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    // 无丢弃即没有那句话（留痕恒空 = 无话可说，`notice()` 的族语义）。
    AURORA_TEST_CHECK(panel->notice().empty());
    probe.snapshot.dropped_bytes = 4096U;
    h.tick(*panel);
    const std::string dropped = panel->notice();
    AURORA_TEST_CHECK_TRUE(dropped.find("4096") != std::string::npos);
    AURORA_TEST_CHECK_TRUE(dropped.find("256") != std::string::npos);
    auto dropped_spot = h.find_text(dropped);
    AURORA_TEST_REQUIRE_MSG(dropped_spot.widget != nullptr, "the drop notice is not drawn on the chain");

    // 清空连丢弃累计一起归零：那一屏已经是新开始，再挂着 N 就是在说看不到的数据。
    auto clear_spot = h.find_button(ui::settings_label("session.monitor.action.clear"));
    AURORA_TEST_REQUIRE(clear_spot.widget != nullptr);
    h.click(clear_spot);
    h.tick(*panel);
    AURORA_TEST_CHECK(panel->notice().empty());
    AURORA_TEST_CHECK(h.find_text(dropped).widget == nullptr);
}

// ------------------------------------------------------------
// 判据⑥（稿 §2 标题行 + §1 安全口径）：绑定随身份来去，「不落盘」那句常驻
// ------------------------------------------------------------
AURORA_TEST_CASE(binding_follows_the_identity_and_the_disclaimer_stays) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto binding_spot = h.find_text(probe.binding);
    AURORA_TEST_REQUIRE_MSG(binding_spot.widget != nullptr, "the binding label is not drawn");
    auto privacy_spot = h.find_text(ui::settings_label("session.monitor.notice.privacy"));
    AURORA_TEST_REQUIRE_MSG(privacy_spot.widget != nullptr, "the never-persist note is not drawn");

    // 绑定失去身份（标签或焦点格散了）就整格不画，而不是画一句「未知」；卡片其余部分照旧。
    probe.binding.clear();
    h.tick(*panel);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.title")).widget != nullptr);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.notice.privacy")).widget != nullptr);
    // 标题行的字节计数走的是词条，两档都有模板（覆盖闸之外唯一的一条同源判据）。
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.bytes", {{"count", std::int64_t{8}}}))
                          .widget != nullptr);
}

// ------------------------------------------------------------
// 判据（五期稿 §12.4 新增「卡与格同生死」）：入口从串口一型扩到四型后，绑定那格的料按**开卡那一刻的
// 焦点格**去取（稿 D37），于是「那一格散了」成了失身份的第二条原因（原先只有「标签散了」一条）。
// 面板侧的判据因此与型无关，只剩两条：装配层交回空串即整格不画；交回的串是 `COM1` 还是
// `ops@build.example:2222` 都不改变卡片其余部分——「换型不换卡」是四型入口敢不改本件的前提。
// ------------------------------------------------------------
AURORA_TEST_CASE(the_binding_cell_dies_with_the_focused_pane) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();  // 缺省那格是串口形态：`档案名 设备路径`
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    AURORA_TEST_REQUIRE_MSG(h.find_text(probe.binding).widget != nullptr,
                            "the serial-shaped binding is not drawn");

    // 换型：同一张卡、同一个位置，装配层按 `tab_kind` 现读到 SSH 那一档的 target 半串（稿 D38②）。
    // 本件不该为此认得任何型——这条判据红了就说明四型的显示口径漏进了面板（裁决 7.83 的反面）。
    const std::string ssh_binding = "srv ops@build.example:2222";
    probe.binding = ssh_binding;
    h.tick(*panel);
    AURORA_TEST_CHECK(h.find_text(ssh_binding).widget != nullptr);
    AURORA_TEST_CHECK(h.find_text("srv /dev/ttyUSB0").widget == nullptr);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.notice.privacy")).widget !=
                      nullptr);

    // 焦点格散了而标签还在：装配层交回空串，于是整格不画（不写「未知」也不伪造一个身份），
    // 卡片其余部分照旧——这一支是「卡与格同生死」在面板侧唯一能看到的那半。
    probe.binding.clear();
    h.tick(*panel);
    AURORA_TEST_CHECK(h.find_text(ssh_binding).widget == nullptr);
    AURORA_TEST_CHECK(h.find_text("srv /dev/ttyUSB0").widget == nullptr);
    // 空绑定不许折成一个兜底身份：卡片上出现的字符串只准来自装配层交进来的那一条腿。
    AURORA_TEST_CHECK(h.find_text("unknown").widget == nullptr);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.title")).widget != nullptr);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.notice.privacy")).widget !=
                      nullptr);
    AURORA_TEST_CHECK(!panel->visible_rows().empty());  // 环还是那条环：散格不牵连行区
}

// ------------------------------------------------------------
// 判据⑦（稿 D11①／D12①）：200 行只折 96 行，往上翻能一路翻到环头；冻结中不读环
// ------------------------------------------------------------
AURORA_TEST_CASE(history_beyond_the_fold_window_is_reachable_by_scrolling_back) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 200);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    h.tick(*panel);  // 消化 open() 的「首帧不判几何」豁免，进入跟尾常态。

    // 跟尾：窗口是环尾 96 行，落位在最底。
    auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), ui::kMonitorFoldWindowRows);
    AURORA_TEST_CHECK(row_is(rows.front(), 104));
    AURORA_TEST_CHECK(row_is(rows.back(), 199));
    auto *list = h.find_list();
    AURORA_TEST_REQUIRE_MSG(list != nullptr, "the scrollable row area is not in the tree");
    AURORA_TEST_REQUIRE(list->max_scroll_offset() > 0.0F);
    AURORA_TEST_CHECK(list->scroll_offset() >= list->max_scroll_offset() - ui::kMonitorBottomTolerance);

    // 往上翻一屏：首帧判冻结并保位重画，次帧以「可见首行 - 16 行」重折。重折后屏上首行不动——
    // 窗口前移 16 行，偏移同步减 16 × 行高。
    const std::size_t calls_at_bottom = probe.snapshot_calls;
    scroll_to_top(h, *panel);
    rows = panel->visible_rows();
    AURORA_TEST_CHECK_TRUE(panel->frozen());
    AURORA_TEST_CHECK(row_is(rows.front(), 88));  // 104 - kMonitorFoldMarginRows
    list = h.find_list();
    AURORA_TEST_REQUIRE(list != nullptr);
    AURORA_TEST_CHECK(std::abs(list->scroll_offset() -
                               static_cast<float>(ui::kMonitorFoldMarginRows) * ui::kMonitorRowExtent) <
                      ui::kMonitorBottomTolerance);

    // 一路翻到环头：每轮窗口再退 16 行。全程不读环（冻结就是「别动我这一屏」）。
    for (int guard = 0; guard < 12 && !shows_line(panel->visible_rows(), 0); ++guard) {
        scroll_to_top(h, *panel);
    }
    rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), ui::kMonitorFoldWindowRows);
    AURORA_TEST_CHECK(row_is(rows.front(), 0));
    AURORA_TEST_CHECK(row_is(rows.back(), 95));
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_at_bottom);

    // 窗口已抵环头便不再滑：用户自己滚到 0 就看见第一条。
    list = h.find_list();
    AURORA_TEST_REQUIRE(list != nullptr);
    list->set_scroll_offset(0.0F);
    h.tick(*panel);
    AURORA_TEST_CHECK(list->scroll_offset() == 0.0F);
    AURORA_TEST_CHECK(row_is(panel->visible_rows().front(), 0));
}

// ------------------------------------------------------------
// 判据⑧（稿 §9.4 滑窗向下那半）：冻结中滚到窗口底只是继续下翻，窗口抵环尾才许解冻
// ------------------------------------------------------------
AURORA_TEST_CASE(scrolling_down_a_frozen_window_pages_instead_of_unfreezing) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 200);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    h.tick(*panel);  // 同判据⑦：先消化 open() 的几何豁免。

    scroll_to_top(h, *panel);  // 冻结在中段窗口（88..183）。
    AURORA_TEST_REQUIRE(panel->frozen());
    AURORA_TEST_REQUIRE(row_is(panel->visible_rows().front(), 88));
    const std::size_t calls_while_frozen = probe.snapshot_calls;

    // 滚到窗口底：at_bottom 为真，但窗口底（183）不是环底（200）——只是继续往下翻。
    auto *list = h.find_list();
    AURORA_TEST_REQUIRE(list != nullptr);
    list->set_scroll_offset(list->max_scroll_offset());
    h.tick(*panel);
    AURORA_TEST_CHECK_TRUE(panel->frozen());
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_while_frozen);
    // 窗口落点由「可见首行 - 16 行」与「窗口须盖到环尾」两头上限夹住：这里是环尾窗口 104..199。
    AURORA_TEST_CHECK(row_is(panel->visible_rows().front(), 104));
    AURORA_TEST_CHECK(row_is(panel->visible_rows().back(), 199));

    // 窗口已抵环尾，再滚到窗口底才解冻（几何判据的两个合取项此时都成立）。
    list = h.find_list();
    AURORA_TEST_REQUIRE(list != nullptr);
    list->set_scroll_offset(list->max_scroll_offset());
    h.tick(*panel);
    AURORA_TEST_CHECK_FALSE(panel->frozen());
    AURORA_TEST_CHECK(probe.snapshot_calls > calls_while_frozen);
}

// ------------------------------------------------------------
// 判据⑨（稿 D12①／D13①）：离底即冻结且不自动吸回；滚回窗口底（窗口已抵环尾）才恢复跟尾
// ------------------------------------------------------------
AURORA_TEST_CASE(leaving_the_bottom_freezes_the_view_and_the_bottom_resumes_following) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 80);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 80U);
    AURORA_TEST_CHECK_FALSE(panel->frozen());

    scroll_to_top(h, *panel);
    AURORA_TEST_CHECK_TRUE(panel->frozen());
    AURORA_TEST_REQUIRE_MSG(
        h.find_text(ui::settings_label("session.monitor.notice.frozen")).widget != nullptr,
        "the freeze hint is not drawn");
    AURORA_TEST_REQUIRE_MSG(
        h.find_button(ui::settings_label("session.monitor.action.latest")).widget != nullptr,
        "the jump-to-latest button is not drawn");

    // 冻结中环照人写：面板既不读，也不追加——「设备停了」与「你看的是旧屏」必须能区分。
    const std::size_t calls_while_frozen = probe.snapshot_calls;
    append_lines(probe, 80, 5);
    h.tick(*panel);
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_while_frozen);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 80U);
    AURORA_TEST_CHECK(row_is(panel->visible_rows().back(), 79));

    // 滚回窗口底（窗口已抵环尾）＝用户确认「我要看最新的」：解冻并立刻补上积压的 5 行。
    auto *list = h.find_list();
    AURORA_TEST_REQUIRE(list != nullptr);
    list->set_scroll_offset(list->max_scroll_offset());
    h.tick(*panel);
    AURORA_TEST_CHECK_FALSE(panel->frozen());
    AURORA_TEST_CHECK(probe.snapshot_calls > calls_while_frozen);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 85U);
    AURORA_TEST_CHECK(row_is(panel->visible_rows().back(), 84));
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.notice.frozen")).widget == nullptr);
    AURORA_TEST_CHECK(h.find_button(ui::settings_label("session.monitor.action.latest")).widget == nullptr);

    // 恢复跟尾后新数据照常追加。
    append_lines(probe, 85, 1);
    h.tick(*panel);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 86U);
    AURORA_TEST_CHECK(row_is(panel->visible_rows().back(), 85));
}

// ------------------------------------------------------------
// 判据⑩（稿 D14①）：「回到最新」是冻结与暂停的共同出口——下一帧直接重读环并落底
// ------------------------------------------------------------
AURORA_TEST_CASE(jump_to_latest_clears_pause_and_freeze) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 80);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto pause_spot = h.find("Switch", 2);
    AURORA_TEST_REQUIRE_MSG(pause_spot.widget != nullptr, "the pause switch is not reachable");
    h.click(pause_spot);
    scroll_to_top(h, *panel);
    AURORA_TEST_CHECK_TRUE(panel->frozen());

    const std::size_t calls_while_stopped = probe.snapshot_calls;
    append_lines(probe, 80, 5);
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_while_stopped);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 80U);

    auto latest_spot = h.find_button(ui::settings_label("session.monitor.action.latest"));
    AURORA_TEST_REQUIRE_MSG(latest_spot.widget != nullptr,
                            "the jump-to-latest button is not dispatch-reachable");
    h.click(latest_spot);
    h.tick(*panel);

    AURORA_TEST_CHECK_FALSE(panel->frozen());
    AURORA_TEST_CHECK(probe.snapshot_calls > calls_while_stopped);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 85U);
    AURORA_TEST_CHECK(row_is(panel->visible_rows().back(), 84));
    AURORA_TEST_CHECK(h.find_button(ui::settings_label("session.monitor.action.latest")).widget == nullptr);
    // 暂停开关被一起解掉（D14①：这两档说的是同一件事——「别停」）。
    auto resumed = h.find("Switch", 2);
    AURORA_TEST_REQUIRE(resumed.widget != nullptr);
    auto *pause_switch = dynamic_cast<au::Switch *>(resumed.widget);
    AURORA_TEST_REQUIRE(pause_switch != nullptr);
    AURORA_TEST_CHECK_FALSE(pause_switch->value());
}

// ------------------------------------------------------------
// 判据⑪（稿 D7①／D14①）：冻结中清空——唯一止血动作连冻结一起归零，落回空态
// ------------------------------------------------------------
AURORA_TEST_CASE(clear_from_a_frozen_view_lands_on_the_empty_state) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 80);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    scroll_to_top(h, *panel);
    AURORA_TEST_REQUIRE(panel->frozen());
    AURORA_TEST_REQUIRE(!panel->visible_rows().empty());

    auto clear_spot = h.find_button(ui::settings_label("session.monitor.action.clear"));
    AURORA_TEST_REQUIRE_MSG(clear_spot.widget != nullptr, "the clear button is not dispatch-reachable");
    h.click(clear_spot);
    h.tick(*panel);

    AURORA_TEST_CHECK_FALSE(panel->frozen());
    AURORA_TEST_CHECK_TRUE(panel->visible_rows().empty());
    AURORA_TEST_CHECK_EQ(probe.clear_calls, 1U);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.empty")).widget != nullptr);
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.notice.frozen")).widget == nullptr);
    AURORA_TEST_CHECK(h.find_button(ui::settings_label("session.monitor.action.latest")).widget == nullptr);
}

// 判据⑦（稿 §10.2 D19①／D21①／D22①）：复制把**整个环**交出去，而不是当前折带窗口
// ------------------------------------------------------------
AURORA_TEST_CASE(copy_hands_the_whole_ring_instead_of_the_fold_window) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 120);  // 超过 96 行折带窗口（D11①）：环头那 24 行不在屏上。
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 96U);
    AURORA_TEST_REQUIRE_FALSE(shows_line(panel->visible_rows(), 0));

    auto copy_spot = h.find_button(ui::settings_label("session.monitor.action.copy"));
    AURORA_TEST_REQUIRE_MSG(copy_spot.widget != nullptr, "the copy button is not dispatch-reachable");
    h.click(copy_spot);
    h.tick(*panel);

    AURORA_TEST_CHECK_EQ(probe.copy_calls, 1U);
    AURORA_TEST_CHECK_EQ(ui::monitor_row_count(probe.snapshot.runs, ui::MonitorRowOptions{}), 120U);
    // 交出去的是整环那 120 行，含屏上没有的环头；末尾不多带一个换行（多带就会数出 121 行）。
    AURORA_TEST_CHECK_EQ(dump_line_count(probe.copied_text), 120U);
    AURORA_TEST_CHECK_TRUE(dump_shows_line(probe.copied_text, 0));
    AURORA_TEST_CHECK_TRUE(dump_shows_line(probe.copied_text, 119));
    AURORA_TEST_CHECK_EQ(probe.copied_text, panel->dump_text());
    // 留痕那句当场出现（「复制了什么」不该让用户去粘贴板上验，业界同态）。
    const auto sentence =
        ui::settings_label("session.monitor.notice.copied", {{"count", std::int64_t{120}}});
    AURORA_TEST_CHECK_EQ(panel->feedback(), sentence);
    AURORA_TEST_CHECK(h.find_text(sentence).widget != nullptr);
}

// ------------------------------------------------------------
// 判据⑧（稿 §10.2 D22①／§10.3）：导出先问路径、再把**同一份文本**交给写盘腿
// ------------------------------------------------------------
AURORA_TEST_CASE(export_asks_a_path_then_hands_the_same_text) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 3);
    probe.picked_path = "D:/dumps/serial.txt";
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto export_spot = h.find_button(ui::settings_label("session.monitor.action.export_file"));
    AURORA_TEST_REQUIRE_MSG(export_spot.widget != nullptr, "the export button is not dispatch-reachable");
    h.click(export_spot);
    h.tick(*panel);

    AURORA_TEST_CHECK_EQ(probe.pick_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.export_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.export_path, probe.picked_path);
    // 与剪贴板同一台折算件、同一份显示档：两条腿拿到的文本一字不差（D19① 的「不第二份真值」）。
    AURORA_TEST_CHECK_EQ(dump_line_count(probe.exported_text), 3U);
    AURORA_TEST_CHECK_EQ(probe.exported_text, panel->dump_text());
    const auto sentence = ui::settings_label("session.monitor.notice.exported",
                                             {{"count", std::int64_t{3}},
                                              {"path", probe.picked_path}});
    AURORA_TEST_CHECK_EQ(panel->feedback(), sentence);
    // 路径逐字上屏（取值而不是字面量，同 `settings.config.exported` 那条口径）。
    AURORA_TEST_CHECK(h.find_text(sentence).widget != nullptr);
}

// ------------------------------------------------------------
// 判据⑨（稿 §10.2 D21①＋`run_export` 那条口径）：取消与空环都整条不跑，连留痕都不写
// ------------------------------------------------------------
AURORA_TEST_CASE(cancelling_the_path_or_an_empty_ring_runs_neither_leg) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 2);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    // 先留一句「已复制」，好让「取消导出什么都不改」这件事有证人。
    auto copy_spot = h.find_button(ui::settings_label("session.monitor.action.copy"));
    AURORA_TEST_REQUIRE_MSG(copy_spot.widget != nullptr, "the copy button is not dispatch-reachable");
    h.click(copy_spot);
    h.tick(*panel);
    const std::string copied_sentence = panel->feedback();
    AURORA_TEST_REQUIRE(!copied_sentence.empty());

    probe.picked_path.clear();  // 用户在原生保存框里点了取消。
    auto export_spot = h.find_button(ui::settings_label("session.monitor.action.export_file"));
    AURORA_TEST_REQUIRE_MSG(export_spot.widget != nullptr, "the export button is not dispatch-reachable");
    h.click(export_spot);
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.export_calls, 0U);
    AURORA_TEST_CHECK_EQ(panel->feedback(), copied_sentence);  // 上一句还在 ⇒ 这一次什么都没发生。

    // 空环：两枚都点了都不该发起（不起对话框、不碰剪贴板、不留痕）。
    auto clear_spot = h.find_button(ui::settings_label("session.monitor.action.clear"));
    AURORA_TEST_REQUIRE_MSG(clear_spot.widget != nullptr, "the clear button is not dispatch-reachable");
    h.click(clear_spot);
    h.tick(*panel);
    AURORA_TEST_REQUIRE_TRUE(panel->visible_rows().empty());
    AURORA_TEST_REQUIRE_TRUE(panel->dump_text().empty());
    const std::size_t picks_before = probe.pick_calls;
    const std::size_t copies_before = probe.copy_calls;
    h.click(h.find_button(ui::settings_label("session.monitor.action.copy")));
    h.click(h.find_button(ui::settings_label("session.monitor.action.export_file")));
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.copy_calls, copies_before);
    AURORA_TEST_CHECK_EQ(probe.pick_calls, picks_before);  // 连对话框都没替用户开一次。
    AURORA_TEST_CHECK_TRUE(panel->feedback().empty());     // 清空把上一句动作的话一起带走。
}

// ------------------------------------------------------------
// 判据⑩（稿 §10.2 D22①）：写盘腿回原因 ⇒ 上屏中性那句，屏面不动
// ------------------------------------------------------------
AURORA_TEST_CASE(a_failed_write_shows_the_neutral_sentence_and_keeps_the_screen) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 4);
    probe.export_reason = std::string{"cannot open target"};  // ASCII 诊断串只进日志，不上屏。
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    const auto rows_before = panel->visible_rows();

    auto export_spot = h.find_button(ui::settings_label("session.monitor.action.export_file"));
    AURORA_TEST_REQUIRE_MSG(export_spot.widget != nullptr, "the export button is not dispatch-reachable");
    h.click(export_spot);
    h.tick(*panel);

    AURORA_TEST_CHECK_EQ(probe.export_calls, 1U);
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), rows_before.size());
    const auto sentence = ui::settings_label("session.monitor.notice.export_failed");
    AURORA_TEST_CHECK_EQ(panel->feedback(), sentence);
    AURORA_TEST_CHECK(h.find_text(sentence).widget != nullptr);
    AURORA_TEST_CHECK(h.find_text(probe.export_path).widget == nullptr);  // 失败那句不带路径。
}

// ------------------------------------------------------------
// 判据⑪（稿 §10.2 D20①）：暂停中导出交的是**这一屏**，不是环里更新的字节
// ------------------------------------------------------------
AURORA_TEST_CASE(export_while_paused_hands_what_is_on_screen) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();  // 两条：OK / AT
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto pause_spot = h.find("Switch", 2);
    AURORA_TEST_REQUIRE_MSG(pause_spot.widget != nullptr, "the pause switch is not dispatch-reachable");
    h.click(pause_spot);
    h.tick(*panel);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 2U);

    probe.record(line_run(900, 1'700'000'009'000LL));  // 采集不停：环里多了一条，屏面不该动。
    h.tick(*panel);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 2U);

    auto export_spot = h.find_button(ui::settings_label("session.monitor.action.export_file"));
    AURORA_TEST_REQUIRE_MSG(export_spot.widget != nullptr, "the export button is not dispatch-reachable");
    h.click(export_spot);
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.export_calls, 1U);
    AURORA_TEST_CHECK_EQ(dump_line_count(probe.exported_text), 2U);
    AURORA_TEST_CHECK_FALSE(dump_shows_line(probe.exported_text, 900));  // 屏上没有的字节不进文件。
}

// ------------------------------------------------------------
// 判据⑫（面板族纪律＋稿 D22①）：两枚按钮只在对应腿在场时画
// ------------------------------------------------------------
AURORA_TEST_CASE(the_take_out_buttons_appear_only_with_their_legs) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    AURORA_TEST_CHECK(
        h.find_button(ui::settings_label("session.monitor.action.copy")).widget != nullptr);
    AURORA_TEST_CHECK(
        h.find_button(ui::settings_label("session.monitor.action.export_file")).widget != nullptr);
    panel->close();
    h.render();

    probe.provide_copy_leg = false;
    probe.provide_export_leg = false;
    auto second = h.attach(probe);
    h.open(*second);
    AURORA_TEST_CHECK(
        h.find_button(ui::settings_label("session.monitor.action.copy")).widget == nullptr);
    AURORA_TEST_CHECK(
        h.find_button(ui::settings_label("session.monitor.action.export_file")).widget == nullptr);
    // 没装新腿时这张卡的其余部分照旧：清空与关闭两枚还在（一枚都不该跟着消失）。
    AURORA_TEST_CHECK(
        h.find_button(ui::settings_label("session.monitor.action.clear")).widget != nullptr);
}

// ------------------------------------------------------------
// 判据⑬（稿 §10.3）：动作那句让位给新数据——上一句余音不该一直挂着
// ------------------------------------------------------------
AURORA_TEST_CASE(a_new_run_clears_the_action_notice) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto copy_spot = h.find_button(ui::settings_label("session.monitor.action.copy"));
    AURORA_TEST_REQUIRE_MSG(copy_spot.widget != nullptr, "the copy button is not dispatch-reachable");
    h.click(copy_spot);
    h.tick(*panel);
    const std::string sentence = panel->feedback();
    AURORA_TEST_REQUIRE(!sentence.empty());
    AURORA_TEST_REQUIRE(h.find_text(sentence).widget != nullptr);

    probe.record(make_run(session::RawDirection::Received, 1'700'000'007'000LL, "MORE\r\n"));
    h.tick(*panel);
    AURORA_TEST_CHECK_TRUE(panel->feedback().empty());
    AURORA_TEST_CHECK(h.find_text(sentence).widget == nullptr);  // 那句真的从卡上消失了，不只是观测面变了。
    // 数据照常在走：那句消失不是「面板停了」。
    AURORA_TEST_CHECK_EQ(panel->visible_rows().size(), 3U);
}

// ------------------------------------------------------------
// 判据⑭（稿 §11 D26①）：第三枚开关真点即换行形态，拨回来看的还是同一份料源
// ------------------------------------------------------------
AURORA_TEST_CASE(text_switch_rewrites_the_rows_and_a_round_trip_keeps_the_source) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 3);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_CHECK_FALSE(panel->text_view_enabled());  // 缺省关＝今天那档 hex（稿 D26①）。
    // 第三枚的词条真在双语表里、且真画到了树上。判据取「非空」而不是「不等于键名」：缺键时
    // `settings_label` 回退的是空串而非键（`settings_i18n.h` 文件头①），后者是永真的空转证人。
    const std::string toggle_label = ui::settings_label("session.monitor.toggle.text_view");
    AURORA_TEST_REQUIRE_MSG(!toggle_label.empty(), "the text view switch has no vocabulary entry");
    AURORA_TEST_REQUIRE_MSG(h.find_text(toggle_label).widget != nullptr,
                            "the text view switch label is not drawn");
    auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);
    AURORA_TEST_CHECK_TRUE(shows_line(rows, 0));

    toggle_text_view(h);
    h.tick(*panel);
    AURORA_TEST_CHECK_TRUE(panel->text_view_enabled());
    rows = panel->visible_rows();
    // 一行 6 字节 < 文本档的字节预算 ⇒ 两档行数相同，变的只是行形态（一台扫描器，稿 D27①）。
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);
    for (int index = 0; index < 3; ++index) {
        AURORA_TEST_CHECK_TRUE(text_row_is(rows[static_cast<std::size_t>(index)], index));
    }
    // 三枚开关各自独立：换视图档不动时间戳档，也不动方向那一格。
    AURORA_TEST_CHECK_TRUE(has_prefix(rows));
    AURORA_TEST_CHECK(rows[0].direction_label == "RX");

    toggle_text_view(h);
    h.tick(*panel);
    AURORA_TEST_CHECK_FALSE(panel->text_view_enabled());
    rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);
    AURORA_TEST_CHECK_TRUE(shows_line(rows, 2));  // 拨回来仍是 hex，且是**同一份**料源
}

// ------------------------------------------------------------
// 判据⑮（稿 D30①）：文本档复制交出去的就是屏上那一份文本，不是 hex 段
// ------------------------------------------------------------
AURORA_TEST_CASE(the_text_view_copies_the_text_that_is_on_screen) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    toggle_text_view(h);
    h.tick(*panel);
    auto copy_spot = h.find_button(ui::settings_label("session.monitor.action.copy"));
    AURORA_TEST_REQUIRE_MSG(copy_spot.widget != nullptr, "the copy button is not dispatch-reachable");
    h.click(copy_spot);
    h.tick(*panel);

    AURORA_TEST_REQUIRE_EQ(probe.copy_calls, 1U);
    const std::string &text = probe.copied_text;
    AURORA_TEST_CHECK(text.find("4F 4B") == std::string::npos);  // 交出去的不是十六进制段
    AURORA_TEST_CHECK(text.find("RX  OK") != std::string::npos);
    // 同一台折算件、同一份显示档（D19①）：屏上每一行都在交出去的那一份里逐字符出现。
    for (const auto &row : panel->visible_rows()) {
        AURORA_TEST_CHECK(text.find(ui::row_text(row)) != std::string::npos);
    }
}

// ------------------------------------------------------------
// 判据⑯（稿 D28①）：解码腿取的是 hook 报的那条腿，不是装配层再算一次的现值
// ------------------------------------------------------------
AURORA_TEST_CASE(the_text_view_decodes_with_the_leg_the_hook_reports) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    probe.encoding = "GB18030";
    probe.record(make_run(session::RawDirection::Received, 1'700'000'004'000LL, "\xD6\xD0\xCE\xC4\r\n"));
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    const std::size_t calls_before = probe.encoding_calls;
    toggle_text_view(h);
    h.tick(*panel);
    AURORA_TEST_CHECK(probe.encoding_calls > calls_before);  // 这条腿与绑定同路：每帧现取一次
    const auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE(!rows.empty());
    // 面板把 hook 的值原样交给折算件：解出的是那两个汉字，不是四枚替换字符。
    AURORA_TEST_CHECK_EQ(rows.back().dump, "  \xE4\xB8\xAD\xE6\x96\x87");
}

// ------------------------------------------------------------
// 判据⑰（稿 §11.4）：装配层没装这条腿时不崩、不空屏，整条回落 UTF-8
// ------------------------------------------------------------
AURORA_TEST_CASE(the_text_view_falls_back_when_the_encoding_leg_is_absent) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe = make_probe();
    probe.provide_encoding_leg = false;
    probe.record(make_run(session::RawDirection::Received, 1'700'000'004'000LL, "\xD6\xD0\xCE\xC4\r\n"));
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    toggle_text_view(h);
    h.tick(*panel);
    AURORA_TEST_CHECK_TRUE(panel->is_open());
    const auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 3U);
    // 回落 UTF-8 ⇒ 那四个字节逐字节非法，四枚替换字符原样上屏（画成看不见就等于说谎，稿 D29）。
    AURORA_TEST_CHECK_EQ(rows.back().dump,
                         "  \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
}

// ------------------------------------------------------------
// 判据⑱（稿 D34）：冻结中换档＝回到这份料源的末尾，而不是沿旧行号跳去另一段历史
// ------------------------------------------------------------
AURORA_TEST_CASE(switching_the_view_while_frozen_returns_to_the_tail_of_this_source) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 80);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    scroll_to_top(h, *panel);
    AURORA_TEST_REQUIRE_TRUE(panel->frozen());
    AURORA_TEST_REQUIRE_TRUE(row_is(panel->visible_rows().front(), 0));

    toggle_text_view(h);
    h.tick(*panel);
    AURORA_TEST_CHECK_FALSE(panel->frozen());  // 换档即解除冻结（与「回到最新」同一条腿）
    const auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 80U);  // 80 行 < 折带窗口，整份都在屏上
    AURORA_TEST_CHECK_TRUE(text_row_is(rows.back(), 79));
    AURORA_TEST_CHECK_TRUE(text_row_is(rows.front(), 0));
    // 冻结提示与「回到最新」那枚跟着消失：不是只有观测面变了。
    AURORA_TEST_CHECK(h.find_text(ui::settings_label("session.monitor.notice.frozen")).widget == nullptr);
}

// ------------------------------------------------------------
// 判据⑲（稿 D14① 不动／D34）：暂停中换档只重画这一屏，压根不去读环
// ------------------------------------------------------------
AURORA_TEST_CASE(switching_the_view_while_paused_redraws_without_reading_the_ring) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_lines(probe, 5);
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);

    auto pause_spot = h.find("Switch", 2);
    AURORA_TEST_REQUIRE_MSG(pause_spot.widget != nullptr, "the pause switch is not reachable");
    h.click(pause_spot);
    h.tick(*panel);

    const std::size_t calls_while_paused = probe.snapshot_calls;
    toggle_text_view(h);
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_while_paused);  // 「别读新环」这条不放松
    const auto rows = panel->visible_rows();
    AURORA_TEST_REQUIRE_EQ(rows.size(), 5U);
    AURORA_TEST_CHECK_TRUE(text_row_is(rows.front(), 0));
    AURORA_TEST_CHECK_TRUE(text_row_is(rows.back(), 4));
}

// ------------------------------------------------------------
// 判据⑳（稿 D34②）：暂停中换档、且两档行数真的不等时——复位到这份料源的末尾
// ------------------------------------------------------------
AURORA_TEST_CASE(switching_the_view_while_paused_rebases_to_the_tail_of_this_source) {
    static_cast<void>(ui::install_settings_strings());
    Probe probe;
    fill_wide_lines(probe, 100);  // 62 字节的设备行：hex 档 800 行 / 文本档 200 行
    Harness h;
    auto panel = h.attach(probe);
    h.open(*panel);
    AURORA_TEST_REQUIRE_EQ(panel->visible_rows().size(), 96U);  // 折带窗口顶格，行号坐标系是 hex 的

    auto pause_spot = h.find("Switch", 2);
    AURORA_TEST_REQUIRE_MSG(pause_spot.widget != nullptr, "the pause switch is not reachable");
    h.click(pause_spot);
    h.tick(*panel);
    const std::size_t calls_while_paused = probe.snapshot_calls;

    toggle_text_view(h);
    h.tick(*panel);
    AURORA_TEST_CHECK_EQ(probe.snapshot_calls, calls_while_paused);  // ⑳ 不放松⑲那条「别读新环」
    const auto rows = panel->visible_rows();
    // 沿用旧行号（hex 的 704 起步）在文本档的 200 行坐标系里越过了料源尽头 ⇒ 空屏；重排才给得出这一屏。
    AURORA_TEST_REQUIRE_MSG(!rows.empty(), "the paused view switch folded an empty window");
    AURORA_TEST_CHECK_EQ(rows.size(), 96U);
    AURORA_TEST_CHECK_TRUE(shows_tag(rows, 99));    // 屏尾是这份料源的最后一条
    AURORA_TEST_CHECK_FALSE(shows_tag(rows, 0));    // 不是从旧行号指向的那一段开始的
}

}  // namespace borealis::test_cases::itest_session_monitor_panel
