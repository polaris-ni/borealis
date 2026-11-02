// 标签就地重命名胶囊的浮层腿证人（SPEC.FEAT.WS.01 余项 (c)，判据入册为裁决 7.195）。
//
// 本套件只管**浮层本体**：打开的形态（盒、初值、焦点、作用域）、三条收口各走哪一条路（Enter 提交 /
// Esc 取消 / 点别处＝失焦提交）、改指另一格与同一格再双击的两档、那一格消失时不提交地收层、盒跟着
// 名称挪的逐帧腿，以及胶囊在像素上确实是「卡上开的一个洞」（底走 `card_bg`、描边与光标走 `accent`）。
// 双击入口那一条腿（哪一次点击会认领、哪一次不会）在 `itest_tab_bar`，因为那是标签条自己的命中算式。
//
// 装配层的 `tab.rename` 命令登记与逐窗 `sync()` 接线不在本套件范围内（`src/main.cpp` 不链进测试目标，
// 裁决 7.83），由真机走查自证（AGENTS.md §4.6 第 33 条）。断言一律经真实派发（`EventDispatcher`）与
// 真实焦点作用域（`FocusManager`），像素腿走 `HeadlessSurface` 的 RGBA8 帧缓冲（第 19 条）。

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
#include "aurora/event/keycode.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text_input.h"
#include "borealis/ui/tab_strip.h"
#include "framework/aurora_test.h"

// 私有头（裁决 D1①：类声明含框架类型，故不进 `include/borealis/`），按相对路径取用。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/settings_panel.h"  // chrome 色板：胶囊的底与描边按 token 断言
#include "../../src/ui/tab_bar.h"
#include "../../src/ui/tab_rename_editor.h"

namespace borealis::test_cases::itest_tab_rename_editor {

#ifdef AURORA_BACKEND_HEADLESS

namespace {

namespace au = aurora;

using borealis::conn::ConnectionType;
using borealis::ui::RenameTarget;
using borealis::ui::SettingsChrome;
using borealis::ui::TabBarHooks;
using borealis::ui::TabBarWidget;
using borealis::ui::TabId;
using borealis::ui::TabNamePriority;
using borealis::ui::TabRenameEditor;
using borealis::ui::TabStrip;
using borealis::ui::TabVisual;
using borealis::ui::resolve_tab_name;

constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 64;
constexpr std::size_t kCardTopRowPx = 4U;       ///< 卡顶内缩 4，而选中格的 accent 带占行 4..5（像素例的反空转基准）。
constexpr std::size_t kStripLeftInsetPx = 8U;
constexpr std::size_t kNameLeftInsetPx = 12U;   ///< 卡片文字内缩（稿：卡左沿 8 → 无图标时的名称 20）。
constexpr std::size_t kTypeSlotTotalPx = 18U;   ///< 类型图标槽（字形盒 12 ＋ 间距 6）。
constexpr std::size_t kPillHeightPx = 22U;
constexpr std::size_t kSingleTabWidthPx = 96U;

using Frame = std::vector<std::uint8_t>;
using Points = std::set<std::pair<std::size_t, std::size_t>>;
using Commit = std::pair<std::uint64_t, std::u32string>;

[[nodiscard]] auto to_u32(const char *text) -> std::u32string {
    std::u32string out;
    for (const unsigned char ch : std::string_view{text}) {
        out.push_back(static_cast<char32_t>(ch));
    }
    return out;
}

/// @brief 活动档的色板：胶囊的底与描边按 token 断言，不写十六进制数（同 `itest_tab_bar` 的传统）。
[[nodiscard]] auto Chrome() -> SettingsChrome {
    return borealis::ui::chrome_palette(borealis::ui::chrome_scheme());
}

/// @brief 驱动台：`TabStrip` ＋ 标签条 ＋ 浮层宿主 ＋ 无头窗口 ＋ 焦点 ＋ 派发器 ＋ 胶囊本体。
///
/// 现场形态照 `src/main.cpp`：胶囊挂在**本窗**的 `OverlayHost` 上、盒只向 `TabBarWidget::rename_target()`
/// 取、提交腿与 `TabBarHooks::rename` 是同一条闭包（这里落到 `TabStrip::rename` 并记一笔流水）。
///
/// 成员声明次序即析构次序的倒序：`editor_` 排在 `host_`／`focus_`／`bar_` 之后 ⇒ 它先消散，而那一次
/// 消散要摘浮层（`host_.remove_overlay`）并对账焦点作用域，两件都还活着（裁决 7.67 的次序在这里的形态）。
class Harness final {
  public:
    explicit Harness(std::vector<std::u32string> names = std::vector<std::u32string>{U"alpha"}) {
        kinds_.assign(names.size(), std::nullopt);
        for (std::size_t index = 0; index < names.size(); ++index) {
            static_cast<void>(strip_.add(static_cast<TabId>(index), names[index]));
        }
        strip_.set_changed_hook([this, bar = bar_]() -> void {
            bar->mark_needs_layout();
            bar->mark_needs_paint();
        });
        focus_.set_root(&root_.widget());
        static_cast<void>(borealis::ui::install_settings_strings());  // 胶囊的无障碍名取自词表，未装则读到空串
        editor_ = std::make_unique<TabRenameEditor>(*bar_, *host_, focus_,
                                                    [this](std::uint64_t id, std::u32string name) -> void {
                                                        commits_.emplace_back(id, std::move(name));
                                                        static_cast<void>(strip_.rename(static_cast<TabId>(id),
                                                                                         commits_.back().second));
                                                    });
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 一帧：装配层那句逐帧 `sync()` 之后 `present_root`，次序与真机一致（先对账盒与待收层，再排布局）。
    auto render() -> void {
        editor_->sync();
        static_cast<void>(window_.present_root(root_));
    }

    /// @brief 打开胶囊编辑那一格并排一帧（装配层 `.begin_rename` 与 `tab.rename` 两条腿都落在这里）。
    auto begin(TabId id) -> void {
        editor_->begin(static_cast<std::uint64_t>(id));
        render();
    }

    /// @brief 推第 `index` 格的连接类型并排帧（名称区左沿跟着类型槽挪，胶囊的盒须跟着挪）。
    auto set_kind(std::size_t index, std::optional<ConnectionType> kind) -> void {
        kinds_[index] = kind;
        bar_->mark_needs_layout();
        bar_->mark_needs_paint();
        render();
    }

    [[nodiscard]] auto editor() const -> const TabRenameEditor & { return *editor_; }
    [[nodiscard]] auto strip() -> TabStrip & { return strip_; }
    [[nodiscard]] auto host() -> au::OverlayHost & { return *host_; }
    [[nodiscard]] auto focused() -> au::Widget * { return focus_.focused(); }
    [[nodiscard]] auto input_focused() const -> bool {
        return editor_->input() != nullptr && editor_->input() == focus_.focused();
    }
    [[nodiscard]] auto scope_depth() const -> std::size_t { return focus_.scope_depth(); }
    [[nodiscard]] auto idle_frame() const -> bool { return window_.is_idle_frame(); }
    [[nodiscard]] auto commits() const -> const std::vector<Commit> & { return commits_; }
    [[nodiscard]] auto overlay_count() const -> std::size_t { return host_->overlay_count(); }

    /// @brief 栏自己折出的盒（胶囊的唯一坐标来源），与浮层实测的 `content_bounds()` 对账用。
    [[nodiscard]] auto target(TabId id) const -> std::optional<RenameTarget> {
        return bar_->rename_target(static_cast<std::uint64_t>(id));
    }
    [[nodiscard]] auto pill_box() const -> au::Rect { return editor_->popup()->content_bounds(); }
    [[nodiscard]] auto pill_value() const -> std::string {
        return editor_->input() != nullptr ? editor_->input()->value() : std::string{};
    }
    /// @brief 显示名（走 `TabStrip` 的折算，与栏上看到的同一枚数）。
    [[nodiscard]] auto display_name(TabId id) -> std::u32string {
        for (const auto &tab : strip_.tabs()) {
            if (tab.id == id) {
                return resolve_tab_name(tab.names, TabNamePriority::ManualWins);
            }
        }
        return {};
    }

    auto type(std::string_view text) -> void {
        for (const char ch : text) {
            au::TextInputEvent event;
            event.text = std::string(1U, ch);
            static_cast<void>(au::EventDispatcher::dispatch(root_.widget(), event, focus_));
        }
        render();
    }

    auto press(au::KeyCode key) -> void {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        static_cast<void>(au::EventDispatcher::dispatch(root_.widget(), event, focus_));
        render();
    }

    /// @brief 在栏上某列（带中线）实点一次：焦点因此离开胶囊 ⇒ 失焦那一条腿。
    auto click(std::size_t x_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = static_cast<float>(x_dp), .y = 21.0F};
        event.button = au::MouseButton::Left;
        for (const auto action : {au::MouseAction::Press, au::MouseAction::Release}) {
            event.action = action;
            static_cast<void>(pointer_.dispatch_mouse(root_.widget(), event, &focus_));
        }
        render();
    }

    [[nodiscard]] auto pixels() const -> Frame {
        Frame out(static_cast<std::size_t>(kWindowWidth) * static_cast<std::size_t>(kWindowHeight) * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

    /// @brief 某色道落在闭区间 `[x0, x1] × [y0, y1]` 里的墨迹坐标。
    [[nodiscard]] static auto ink_in(const Frame &frame, const au::Color &ink, std::size_t x0,
                                     std::size_t x1, std::size_t y0, std::size_t y1) -> Points {
        Points out;
        const std::size_t width = static_cast<std::size_t>(kWindowWidth);
        for (std::size_t pixel = 0; pixel + 3U < frame.size(); pixel += 4U) {
            const std::size_t index = pixel;
            if (frame[index] != static_cast<std::uint8_t>(ink.r) ||
                frame[index + 1U] != static_cast<std::uint8_t>(ink.g) ||
                frame[index + 2U] != static_cast<std::uint8_t>(ink.b) ||
                frame[index + 3U] != static_cast<std::uint8_t>(ink.a)) {
                continue;
            }
            const std::size_t cell = index / 4U;
            const std::size_t x = cell % width;
            const std::size_t y = cell / width;
            if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
                out.insert({x, y});
            }
        }
        return out;
    }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        static_cast<void>(surface->begin_frame(kWindowWidth, kWindowHeight));
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    TabStrip strip_;
    std::vector<std::optional<ConnectionType>> kinds_;
    std::shared_ptr<TabBarWidget> bar_ = std::make_shared<TabBarWidget>(TabBarHooks{
        .tabs = [this]() -> std::vector<TabVisual> {
            std::vector<TabVisual> out;
            const std::optional<TabId> selected = strip_.selected();
            std::size_t index = 0U;
            for (const auto &tab : strip_.tabs()) {
                out.push_back(TabVisual{
                    .id = tab.id,
                    .display_name = resolve_tab_name(tab.names, TabNamePriority::ManualWins),
                    .is_selected = selected.has_value() && selected.value() == tab.id,
                    .has_close_button = strip_.count() > 1U,
                    .bell_triggered = tab.bell_triggered,
                    .has_activity = tab.has_activity,
                    .exited = tab.exited,
                    .kind = index < kinds_.size() ? kinds_[index] : std::nullopt,
                });
                ++index;
            }
            return out;
        },
        .select = [this](std::uint64_t id) -> void {
            static_cast<void>(strip_.select(static_cast<TabId>(id)));
        },
        .close = [this](std::uint64_t id) -> void {
            static_cast<void>(strip_.close(static_cast<TabId>(id)));
        },
        // 本件的现场不需要这三条腿：入口在 `itest_tab_bar` 那一款判，双击直接打给 `editor_->begin()`。
        .move = nullptr,
        .add_new = nullptr,
        .undo_close = nullptr,
        .note_press = nullptr,
    });
    std::shared_ptr<au::OverlayHost> host_ =
        std::make_shared<au::OverlayHost>(au::Node{std::static_pointer_cast<au::Widget>(bar_)});
    au::Node root_{std::static_pointer_cast<au::Widget>(host_)};
    au::FocusManager focus_;
    au::EventDispatcher pointer_;
    std::unique_ptr<TabRenameEditor> editor_{};  ///< 在构造体里建：它要 `bar_`／`host_`／`focus_` 三件都在世。
    std::vector<Commit> commits_{};
};

#else  // 无头后端未编译：HeadlessSurface 不在公共面上，本套件一律跳过而不是空转。

#endif

}  // namespace

AURORA_TEST_CASE(opening_the_pill_lands_on_the_name_and_takes_focus) {
#ifdef AURORA_BACKEND_HEADLESS
    // 打开的四件形态一次判尽：盒＝栏给的那一份、初值＝现名、焦点＝框、作用域多压一层（裁决 **7.195**
    // 头②③）。`begin()` 在无头里走的是**派发栈外**那一支（`Popup::open_at` 取不到焦点管理器 ⇒ 不压），
    // 故最后一句钉的正是本件自己补压的那一次。
    Harness h;
    const std::size_t depth = h.scope_depth();
    const std::optional<RenameTarget> target = h.target(TabId{0U});
    AURORA_TEST_REQUIRE_TRUE(target.has_value());

    h.begin(TabId{0U});

    AURORA_TEST_CHECK_TRUE(h.editor().is_open());
    AURORA_TEST_REQUIRE_TRUE(h.editor().editing().has_value());
    AURORA_TEST_CHECK_EQ(h.editor().editing().value(), std::uint64_t{0U});
    const au::Rect pill = h.pill_box();
    AURORA_TEST_CHECK_EQ(pill.origin.x, target->box.origin.x);
    AURORA_TEST_CHECK_EQ(pill.origin.y, target->box.origin.y);
    // 高必须实测等于稿那 22 dp：`TextInput` 的缺省高是「文本测量 ＋ 上下内边距」，12 pt 上会读出另一档数。
    AURORA_TEST_CHECK_EQ(pill.size.height, static_cast<float>(kPillHeightPx));
    AURORA_TEST_CHECK_EQ(pill.size.width, target->box.size.width);
    AURORA_TEST_CHECK_EQ(h.pill_value(), std::string{"alpha"});  // 初值＝未截断的现名
    AURORA_TEST_CHECK_TRUE(h.input_focused());
    AURORA_TEST_CHECK_EQ(h.scope_depth(), depth + 1U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(enter_commits_the_edited_name_and_gives_the_scope_back) {
#ifdef AURORA_BACKEND_HEADLESS
    // 收口一（Enter）：提交一次、落在真值源上、作用域退回打开前那一层。空提交点只有 `on_close` 那一处，
    // 故这里若读到两笔流水，就是待收层被走成了两条腿（裁决 7.195 头③④）。
    Harness h;
    const std::size_t depth = h.scope_depth();
    h.begin(TabId{0U});
    h.type("!");
    AURORA_TEST_CHECK_EQ(h.pill_value(), std::string{"alpha!"});

    h.press(au::KeyCode::Enter);

    AURORA_TEST_CHECK_FALSE(h.editor().is_open());
    AURORA_TEST_CHECK_FALSE(h.editor().editing().has_value());
    AURORA_TEST_REQUIRE_EQ(h.commits().size(), std::size_t{1U});
    AURORA_TEST_CHECK_EQ(h.commits()[0U].first, std::uint64_t{0U});
    AURORA_TEST_CHECK_EQ(h.commits()[0U].second, to_u32("alpha!"));
    AURORA_TEST_CHECK_EQ(h.display_name(TabId{0U}), to_u32("alpha!"));  // 落库的是码点串
    AURORA_TEST_CHECK_EQ(h.scope_depth(), depth);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(escape_leaves_the_name_alone_and_gives_the_scope_back) {
#ifdef AURORA_BACKEND_HEADLESS
    // 收口二（Esc）：打进去的字作废。本件把那次收层标成「取消」，而判据 ⑰ 那句「Esc 撤销」的唯一读数
    // 就是**没有**提交（裁决 7.195 头③：三条收口共用同一个提交点，靠旗标分岔）。
    Harness h;
    const std::size_t depth = h.scope_depth();
    h.begin(TabId{0U});
    h.type("zz");

    h.press(au::KeyCode::Escape);

    AURORA_TEST_CHECK_FALSE(h.editor().is_open());
    AURORA_TEST_CHECK_TRUE(h.commits().empty());
    AURORA_TEST_CHECK_EQ(h.display_name(TabId{0U}), to_u32("alpha"));
    AURORA_TEST_CHECK_EQ(h.scope_depth(), depth);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_click_off_the_pill_commits_exactly_once) {
#ifdef AURORA_BACKEND_HEADLESS
    // 收口三（点别处）：**只有**失焦那一条腿，且它不在派发栈里收层。这条是本例的主承重：框架的
    // `Popup::close()` 次序是 `pop_scope` → `open_ = false` → `on_close`，在失焦回调里再呼一次就会二次
    // `pop_scope`——第二次弹掉的是**祖先**那一层作用域，故深度读数比「提交次数」更灵敏（裁决 7.195 头④）。
    // 为此浮层的 `dismiss_on_outside_click` 必须关掉，否则同一次 Press 里浮层自己收一层、失焦再收一层。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    const std::size_t depth = h.scope_depth();
    h.begin(TabId{0U});
    h.type("x");

    h.click(kStripLeftInsetPx + kSingleTabWidthPx + 20U);  // 第二格的卡片上：栏位命中，非浮层

    AURORA_TEST_CHECK_FALSE(h.editor().is_open());
    AURORA_TEST_REQUIRE_EQ(h.commits().size(), std::size_t{1U});
    AURORA_TEST_CHECK_EQ(h.commits()[0U].first, std::uint64_t{0U});
    AURORA_TEST_CHECK_EQ(h.commits()[0U].second, to_u32("alphax"));
    AURORA_TEST_CHECK_EQ(h.scope_depth(), depth);
    AURORA_TEST_CHECK_FALSE(h.input_focused());
    AURORA_TEST_CHECK_EQ(h.strip().selected().value(), TabId{1U});  // 点别处照旧是那一次选中
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_pill_never_eats_a_press_that_belongs_to_the_pane) {
#ifdef AURORA_BACKEND_HEADLESS
    // 「点别处」为什么不交给浮层自己（裁决 7.195 头②）：视口的 Press 分支每次都无条件问过宿主一次
    // （`src/ui/terminal_view.cpp` 里那句 `host_->handle_outside_click(e.position)`，菜单与搜索浮层共用同
    // 一条入口），而框架的实现是「命中框外且 `dismiss_on_outside_click` 为真 ⇒ `close()` 并**回 true**」，
    // 调用方据 true 把这一击标成已处理并 `return`。于是胶囊开着时点一下终端，那一击会被吃掉：不起始选区、
    // 不落光标——而「该收层」这件事失焦那条腿本来就会做。故本件关掉浮层自己的认领，只留失焦一条路。
    Harness h;
    h.begin(TabId{0U});
    h.type("q");
    const au::Point on_the_pane{.x = 300.0F, .y = 40.0F};  // 胶囊盒外的一击（盒在栏那一截）
    AURORA_TEST_CHECK_FALSE(h.host().handle_outside_click(on_the_pane));  // 回 false ⇒ 这一击仍归 pane
    AURORA_TEST_CHECK_TRUE(h.editor().is_open());  // 浮层自己不收层：收层只有失焦那一条腿
    AURORA_TEST_CHECK_TRUE(h.commits().empty());

    // 反空转：同一个入口在框架缺省配置下**确实**会吃掉这一击——否则上面那句绿在「宿主恒回 false」上。
    auto plain = std::make_shared<au::Popup>();
    plain->open_at(au::Point{.x = 400.0F, .y = 40.0F});
    static_cast<void>(h.host().add_overlay(au::Node{std::static_pointer_cast<au::Widget>(plain)}));
    AURORA_TEST_CHECK_TRUE(h.host().handle_outside_click(on_the_pane));
    AURORA_TEST_CHECK_FALSE(plain->is_open());     // 关掉的是缺省那一枚
    AURORA_TEST_CHECK_TRUE(h.editor().is_open());  // 胶囊不受影响，字也还在
    AURORA_TEST_CHECK_EQ(h.pill_value(), std::string{"alphaq"});
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_pill_reseeds_when_it_is_pointed_at_another_tab) {
#ifdef AURORA_BACKEND_HEADLESS
    // 开着时改指另一格（`tab.rename` 再按一次，或双击了别的格）：换 id、重种现名、**不重建树也不重复
    // 压作用域**（裁决 7.195 头②：树只建一次，作用域只压打开那一次）。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    const std::size_t depth = h.scope_depth();
    h.begin(TabId{0U});
    h.type("x");  // 第一格里的字：改指之后不该被提交，也不该被当成第二格的初值

    h.begin(TabId{1U});

    AURORA_TEST_CHECK_TRUE(h.editor().is_open());
    AURORA_TEST_REQUIRE_TRUE(h.editor().editing().has_value());
    AURORA_TEST_CHECK_EQ(h.editor().editing().value(), std::uint64_t{1U});
    AURORA_TEST_CHECK_EQ(h.pill_value(), std::string{"beta"});
    AURORA_TEST_CHECK_TRUE(h.input_focused());
    AURORA_TEST_CHECK_EQ(h.scope_depth(), depth + 1U);
    const std::optional<RenameTarget> second = h.target(TabId{1U});
    AURORA_TEST_REQUIRE_TRUE(second.has_value());
    AURORA_TEST_CHECK_EQ(h.pill_box().origin.x, second->box.origin.x);  // 盒跟着第二格走

    h.press(au::KeyCode::Enter);

    AURORA_TEST_REQUIRE_EQ(h.commits().size(), std::size_t{1U});  // 第一格那笔没提交
    AURORA_TEST_CHECK_EQ(h.commits()[0U].first, std::uint64_t{1U});
    AURORA_TEST_CHECK_EQ(h.commits()[0U].second, to_u32("beta"));
    AURORA_TEST_CHECK_EQ(h.scope_depth(), depth);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_second_double_click_on_the_same_tab_keeps_what_was_typed) {
#ifdef AURORA_BACKEND_HEADLESS
    // 同一格再双击：只重锚，不重种文本、不夺焦点（否则用户刚打的两笔被初值抹掉）。这一支与上一例的
    // 分工就靠「值有没有被换回现名」这一句分开。
    Harness h;
    const std::size_t depth = h.scope_depth();
    h.begin(TabId{0U});
    h.type("23");

    h.begin(TabId{0U});

    AURORA_TEST_CHECK_EQ(h.pill_value(), std::string{"alpha23"});
    AURORA_TEST_REQUIRE_TRUE(h.editor().editing().has_value());
    AURORA_TEST_CHECK_EQ(h.editor().editing().value(), std::uint64_t{0U});
    AURORA_TEST_CHECK_EQ(h.scope_depth(), depth + 1U);  // 第二次打开没再压一层
    AURORA_TEST_CHECK_TRUE(h.commits().empty());
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_closed_tab_takes_the_pill_down_without_committing) {
#ifdef AURORA_BACKEND_HEADLESS
    // 编辑中那一格被关闭：收层而**不**提交——把没按 Enter 的字写进一格已经消失的标签是凭空造状态
    // （裁决 7.195）。这里同时钉住逐帧 `sync()` 的一帧滞后：`rename_target()` 读的是栏的布局缓存，
    // 故第一帧只把缓存刷新、第二帧才认不出那一格。装配层那句逐帧 `sync()` 因此不是可有可无的装饰。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    const std::size_t depth = h.scope_depth();
    h.begin(TabId{0U});
    h.type("z");

    static_cast<void>(h.strip().close(TabId{0U}));
    h.render();  // 栏的 `cached_tabs_` 在这一帧刷新
    AURORA_TEST_CHECK_TRUE(h.editor().is_open());
    h.render();  // 下一帧 `sync()` 取不到盒 ⇒ 按取消收层

    AURORA_TEST_CHECK_FALSE(h.editor().is_open());
    AURORA_TEST_CHECK_TRUE(h.commits().empty());
    AURORA_TEST_CHECK_EQ(h.scope_depth(), depth);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_pill_follows_the_name_when_its_slot_moves) {
#ifdef AURORA_BACKEND_HEADLESS
    // 逐帧腿的第二档：盒变了就重锚，而**不重建树**——打字中的值与焦点必须活着（裁决 7.195 头②那句
    // 「改盒不改树」）。这里挪的是类型图标槽（`SPEC.FEAT.WS.04`）：名称左沿从 20 挪到 38，胶囊得跟着挪，
    // 否则它就压在图标上而不是压在名字上。
    // 实测到的是一帧滞后，且真机同形（裁决 7.195 登记）：`rename_target()` 读栏的布局缓存，而装配层那句
    // `sync()` 排在**本帧 `present_root` 之前**（Aurora 主仓 `include/aurora/app/application.h` 里 `on_frame_`
    // 的注记「在 present_root 前调用」，`src/main.cpp` 的逐帧腿就是它）。故槽在这一帧的 present 里才挪位，
    // 胶囊在下一帧的 `sync()` 里才跟上——本例因此读两帧，而不是把滞后当成缺陷。
    Harness h;
    h.begin(TabId{0U});
    h.type("7");
    AURORA_TEST_CHECK_EQ(h.pill_box().origin.x, static_cast<float>(kStripLeftInsetPx + kNameLeftInsetPx));

    h.set_kind(0U, ConnectionType::Local);

    const std::optional<RenameTarget> target = h.target(TabId{0U});
    AURORA_TEST_REQUIRE_TRUE(target.has_value());
    AURORA_TEST_CHECK_EQ(target->box.origin.x,
                         static_cast<float>(kStripLeftInsetPx + kNameLeftInsetPx + kTypeSlotTotalPx));
    // 滞后的那一帧：栏已挪位、胶囊还停在旧盒上（读反了就是「胶囊跟着 present 走」的假象）。
    AURORA_TEST_CHECK_EQ(h.pill_box().origin.x, static_cast<float>(kStripLeftInsetPx + kNameLeftInsetPx));
    h.render();  // 下一帧的 `sync()` 取到新盒 ⇒ 重锚
    AURORA_TEST_CHECK_EQ(h.pill_box().origin.x, target->box.origin.x);
    AURORA_TEST_CHECK_EQ(h.pill_box().origin.y, target->box.origin.y);
    AURORA_TEST_CHECK_TRUE(h.editor().is_open());
    AURORA_TEST_CHECK_EQ(h.pill_value(), std::string{"alpha7"});  // 重锚没抹掉输入
    AURORA_TEST_CHECK_TRUE(h.input_focused());
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_steady_frame_with_the_pill_open_stays_idle) {
#ifdef AURORA_BACKEND_HEADLESS
    // 逐帧腿的那条性能纪律（裁决 7.195，`SPEC.NF.PERF.06`／AGENTS §4.5 第 25 条）：`Popup::open_at`
    // 无条件标脏（布局与绘制各一次），每帧照呼就把「胶囊开着的几十帧」全变成非空闲帧。故 `apply_box`
    // 以「盒逐位相同即不动」为闸门，而这里在**读**闸门的效果而不是它的存在：胶囊开着、盒没变的一帧，
    // `is_idle_frame()` 须为真（把闸门抹掉，这一句就是 false——那正是变异要打的靶）。
    // 两声反钳排在后，防的是「绿在根本没在画」上：栏重排那一帧与胶囊真重锚那一帧都必须读到非空闲。
    // 后者隔了一帧才出现，理由与上一例同（`sync()` 排在本帧 `present_root` 之前，读的是上一帧的布局缓存）。
    Harness h;
    h.begin(TabId{0U});
    h.render();
    AURORA_TEST_CHECK_TRUE(h.idle_frame());

    h.set_kind(0U, ConnectionType::Local);
    AURORA_TEST_CHECK_FALSE(h.idle_frame());  // 反钳①：栏在这一帧重排（胶囊还没跟上）
    h.render();
    AURORA_TEST_CHECK_FALSE(h.idle_frame());  // 反钳②：胶囊在这一帧的 `sync()` 里重锚 ⇒ 标脏
    h.render();
    AURORA_TEST_CHECK_TRUE(h.idle_frame());   // 盒定了 ⇒ 闸门生效，开着的胶囊不再制造非空闲帧
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_pill_is_a_hole_in_the_card_with_an_accent_border) {
#ifdef AURORA_BACKEND_HEADLESS
    // 像素腿（裁决 7.195 那两处色档）：胶囊底走 `card_bg`（稿 `#191A21` 那一格在 13 枚 token 里没有，
    // 与 `sel` 同档登记），描边与光标走 `accent`。判法取**选中格内部**那一块：卡片底是 `control_bg`，
    // 故卡里出现的 `card_bg` 只能是胶囊那一枚；而卡顶那条 2 dp accent 带在行 4..5，取行 6 之下的 accent
    // 就只剩胶囊自己的描边。开盒前后各取一帧，两句「之前空、之后有墨」都是反空转。
    Harness h;
    const Frame before = h.pixels();
    h.begin(TabId{0U});
    const std::optional<RenameTarget> target = h.target(TabId{0U});
    AURORA_TEST_REQUIRE_TRUE(target.has_value());
    const Frame after = h.pixels();

    const std::size_t x0 = static_cast<std::size_t>(target->box.origin.x);
    const std::size_t x1 = static_cast<std::size_t>(target->box.origin.x + target->box.size.width) - 1U;
    const std::size_t y0 = static_cast<std::size_t>(target->box.origin.y);
    const std::size_t y1 = y0 + kPillHeightPx - 1U;
    // 反空转的前提要先钉住：盒顶在卡顶 accent 带（行 4..5）之下，故盒内读到的 accent 只可能是胶囊自己的
    // 描边；否则本例会绿在「带子延伸进盒」上。
    AURORA_TEST_REQUIRE_GT(y0, kCardTopRowPx + 1U);

    AURORA_TEST_CHECK_TRUE(Harness::ink_in(before, Chrome().card_bg, x0, x1, y0, y1).empty());
    AURORA_TEST_CHECK_TRUE(!Harness::ink_in(after, Chrome().card_bg, x0, x1, y0, y1).empty());
    AURORA_TEST_CHECK_TRUE(Harness::ink_in(before, Chrome().accent, x0, x1, y0 + 1U, y1 - 1U).empty());
    AURORA_TEST_CHECK_TRUE(!Harness::ink_in(after, Chrome().accent, x0, x1, y0 + 1U, y1 - 1U).empty());
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

}  // namespace borealis::test_cases::itest_tab_rename_editor
