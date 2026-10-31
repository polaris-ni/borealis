/// 测试类型: integration
/// 目标单元: src/ui/tab_bar.cpp（标签栏绘制侧）与 src/ui/tab_strip.cpp 的变更钩子
/// 测试说明: 无头帧缓冲上的**像素证人**，验收 `SPEC.FEAT.WS.04` 那三枚状态角标在屏幕上的形态与落位、
///           标签条右端那两枚入口（`＋ 新建` 与 `⌄ 最近关闭`），以及三枚角标共同依赖的那条通知链
///           （裁决 **7.91**④；三枚的形态与配色按屏 1 稿改判，裁决 **7.183**）。这里断的从来不是
///           「角标算得对不对」（那是 `utest_tab_strip` 的二十余例），而是**它有没有落到屏幕上、
///           落在哪一格、走的是哪一个色道**：
///           - 「窗口尺寸没变时角标也上得了屏」——框架的布局缓存在「约束未变 ∧ 本控件未被标脏」时直接
///             复用上次尺寸并跳过 `on_layout`，而 `TabBarWidget` 的输入全在 `on_layout` 里经 hooks 现取，
///             所以这条判据钉的就是 `TabStrip` 的变更钩子本身（缺了它，本例第一句 `REQUIRE` 即红）；
///           - 三枚角标同形（屏 1 稿的 `circle r=3`）、同带（栏位竖向中线）、右锚，只靠**色道**与**簇内
///             次序**分档：断线＝`danger`、BEL＝`warning`、活动＝`success`，按这个优先级向左排、pitch 10。
///             定位一律按「逐位等于该色道的像素」取芯——`fill_sector` 四条边各 1 px 羽化，墨迹外接框随
///             抗锯齿档浮动，把面积写成等号就是钉框架的实现（裁决 7.183 记的这条口径）；
///           - 点亮与熄灭都是**真转移**才唤醒标签栏（每帧重推同一个值时钩子计数不涨，否则后台标签的
///             输出洪流就是每帧一次整栏重排）；退出是逐格的事实而不是整栏的（两格各自点亮且互不牵连）；
///           - 真点一次标签把 BEL 取走并**从屏幕上撤掉**（`SPEC.FEAT.WS.04` 的取走语义；判据是全帧
///             逐位复现点击前那一帧，因此残影、错位、连带改动都红）；
///           - 选中格的活动位**既不落墨也不占位**（裁决 7.82① 那句「只在未选中格显形」在绘制腿与
///             `lit_badge_count` 里各写了一遍，两处一旦分叉，名称就会按一枚根本没画的角标让位）：
///             判据同样取整帧逐位相同，另配一枚同名的无角标驱动台证明占位真的在起作用；
///           - 右端两枚入口**画在哪儿与点哪儿算共用同一枚算式**（`right_cluster`）：`＋` 的墨迹中心实发
///             一次点击即走 `add_new`，稿那个 16 dp 右沿内缩带不吃任何点击，`⌄ 最近关闭` 只在装配层给了
///             撤销腿时才成枚地出现（裁决 7.183：稿画的是「最近关闭」列表入口，今日落的是单步撤销）；
///           - 三枚**夹缝常量各有自己的墨迹证人**（裁决 7.183 记的变异自证补口）：`＋` 与它的文字之间
///             8 dp、`⌄` 与它的文字之间 5 dp、撤销腿缺席时 `＋` 回到稿那个裸右沿内缩档。前两枚之所以
///             必须单独钉，是因为「盒与墨迹同源」那一类判据对它们**免疫**——常量一改，落笔盒与墨迹
///             一起挪，差分与命中都看不出分叉（实测存活变异）；判据形态因此只剩「墨迹比墨迹」与
///             「墨迹比稿算出的坐标」两种，取的是后者（比色只判得出 `⌄`，`＋` 那 1.6 dp 描边没有原色芯）。
///           本棒（`SPEC.FEAT.WS.04` 的最后一项）追加的**逐标签连接类型图标**也在这里验收，判据形态
///           照旧走差分而不是比色（稿 **7.129** 的 D4／D5／D7 与 §2 判据 ②～⑤）：未登记类型 ⇒ 左槽
///           单一底色（不画也不占位）、登记 ⇒ 栏位宽恰 +18 dp（锚＝右锚那颗角标的落位，本棒把它从
///           「右上角 6×6 实心」改成带中线的圆点，锚换按色道取）、图标与三枚角标互不相交、逐格类型
///           改动不牵连邻格、四型四帧两两不同。
///           `SPEC.FEAT.PREF.01` 那条**色板证人**照旧：同一棵标签栏在暗、亮两档各出一帧，栏底逐档等于
///           `chrome_palette()` 的 `card_bg`，且切回暗档逐位复现首帧（换肤可逆）。
///           像素一律比 RGBA 四通道含 alpha（与 `itest_render_viewport` 同一条口径：Headless 帧底色是
///           全透明黑，只比 RGB 会把「画了个纯黑」与「什么都没画」混为一谈）。

#include <algorithm>
#include <array>
#include <cmath>
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
#include "../../src/ui/settings_panel.h"  // PREF.01：活动档与唯一色值表（标签栏底取它的 card_bg）
#include "../../src/ui/tab_bar.h"

namespace borealis::test_cases::itest_tab_bar {

namespace {

#ifdef AURORA_BACKEND_HEADLESS

using borealis::ui::ChromeScheme;
using borealis::ui::SettingsChrome;
using borealis::ui::TabBarHooks;
using borealis::ui::TabBarWidget;
using borealis::ui::TabId;
using borealis::ui::TabNamePriority;
using borealis::ui::TabStrip;
using borealis::ui::TabVisual;
using borealis::conn::ConnectionType;

constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 64;
/// @brief 栏高与状态角标簇的档位（屏 1 稿 `codespec/UI_WORKSPACE.draft.svg` 那三枚 `circle r=3`：
///        栏带 y 36..78、圆心 cy=57 即带中线、圆心距栏位右端 27；Headless 下 dp＝px）。
constexpr std::size_t kBarHeightPx = 42U;
constexpr std::size_t kBadgeRadiusPx = 3U;
constexpr std::size_t kBadgeAnchorInsetPx = 27U;
constexpr std::size_t kBadgePitchPx = 10U;
/// @brief 角标芯（逐位等于该色道的那批像素）的实测档＝**4×4**，允许 +1：`fill_sector` 半径 3 的实心
///        圆只在内核四格上是原色，四条边各 1 px 羽化（探针 2026-10-31 实测芯 x 67..70／y 19..22、
///        差分簇 x 66..71／y 18..23＝6×6）。把芯写成 6×6 或把墨迹面积写成等号就是钉抗锯齿实现
///        （裁决 7.183 记的这条口径）。
constexpr std::size_t kBadgeCoreMinPx = 4U;
constexpr std::size_t kBadgeCoreMaxPx = 5U;
/// @brief 芯的左沿相对圆心的实测偏移（圆心在连续坐标 69.0 ⇒ 芯占像素列 67..70）。
constexpr std::size_t kBadgeCoreLeftOfCentrePx = 2U;
/// @brief 单格 96 dp 栏位（短名贴住 `tab_min_width_` 钳位）上 rank-0 角标的圆心横坐标。
constexpr std::size_t kSingleTabWidthPx = 96U;
/// @brief 栏位钳位区间（`src/ui/tab_bar.cpp` 的 `tab_min_width_`／`tab_max_width_`）：判据 ③ 只在
///        两界之间才谈得上「恰为 18 dp」，故那里各有一枚边界断言。
constexpr std::size_t kTabMinWidthPx = 96U;
constexpr std::size_t kTabMaxWidthPx = 240U;
/// @brief 类型图标槽的档位（`SPEC.FEAT.WS.04` 的 D4／F9）：字形盒占 x 12..23，实绘行带 21..32
///        （13 pt 探针实测，dp＝px 于 Headless），名称原点由 12 挪到 30 ⇒ 占用总宽 18 dp。
constexpr std::size_t kGlyphSlotX0 = 12U;
constexpr std::size_t kGlyphSlotX1 = 23U;
constexpr std::size_t kGlyphTopRow = 21U;
constexpr std::size_t kGlyphBottomRow = 32U;
constexpr std::size_t kTypeSlotTotal = 18U;
/// @brief 右端入口簇的档位（稿：`＋` 折线包络 x 1352..1362 即 10 dp、两簇之间 31 dp、文字右缘距栏
///        右端 16 dp）。以下五枚是 `src/ui/tab_bar.cpp` 那五个同名常量的读数，探针实测过它们折出的
///        圆心（2026-10-31：栏宽 640、无文字、有撤销腿 ⇒ `＋` 圆心实发在 575＝640−65）。
constexpr std::size_t kClusterRightPadPx = 16U;
constexpr std::size_t kClusterGapPx = 31U;
constexpr std::size_t kPlusSizePx = 10U;
constexpr std::size_t kChevronWidthPx = 8U;
constexpr std::size_t kChevronToLabelGapPx = 5U;
/// @brief `＋` 圆心的两档内缩量：右端那一枚在位时，箭位（8）与箭–字夹缝（5）一并占住栏右那截，
///        `＋` 因此向左让出 13 dp；不在位时那一整枚缺席，`＋` 回到稿那个 16 dp 右沿内缩档。
constexpr std::size_t kPlusCenterInsetWithUndoPx =
    kClusterRightPadPx + kChevronWidthPx + kChevronToLabelGapPx + kClusterGapPx + kPlusSizePx / 2U;
constexpr std::size_t kPlusCenterInsetNoUndoPx =
    kClusterRightPadPx + kClusterGapPx + kPlusSizePx / 2U;
/// @brief `＋` 右缘 → 其文字左沿的稿面夹缝（1362→1370）。与 `kChevronToLabelGapPx` 同一条来路：
///        判据按**稿的数**独立折出预期落位，而代码里那枚常量若改，落笔盒与墨迹一起动、墨迹当场与
///        这个预期分叉——盒与点同源的那几枚常量（右沿内缩、簇间距）单靠自身折不出证人，正因如此
///        每一枚夹缝都得有一枚「墨迹 vs 墨迹」或「墨迹 vs 稿算坐标」的证人。
constexpr std::size_t kPlusToLabelGapPx = 8U;
/// @brief 两枚文字的字号档（`src/ui/tab_bar.cpp` 的 `kNewLabelFontPt`／`kRecentLabelFontPt` 读数）：
///        判据要自己量文字宽，故字号也得在测试侧写死一份——这两枚改了而墨迹判据不动，说明改的只是
///        字号而不是夹缝，属另一档证人（文字有没有落墨由 `the_recent_close_entry_…` 那条把）。
constexpr double kNewLabelFontPt = 12.0;
constexpr double kRecentLabelFontPt = 11.0;
/// @brief 入口簇那两枚字形/文字的纵向行带（中线 21 上下各 5，`＋` 的包络即 10 dp；文字盒按
///        `measure_height` 居中在同一中线上，取 15..27 是它的实测内切）。
constexpr std::size_t kClusterBandTop = 16U;
constexpr std::size_t kClusterBandBottom = 26U;
constexpr std::size_t kLabelBandTop = 15U;
constexpr std::size_t kLabelBandBottom = 27U;

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

/// @brief 活动档的那一份色板——三枚角标的判据从此不写十六进制数，而是问「走的是哪一格 token」。
///
/// 本套件缺省跑在暗档（跟随系统的覆盖只发生在真机装配层），而暗档三枚 token 彼此不同、也与 `accent`
/// ／`text` 不同，故「按色道定位」在这里是唯一的：改配色即改色道，证人当场转红。
[[nodiscard]] auto Chrome() -> SettingsChrome { return borealis::ui::chrome_palette(borealis::ui::chrome_scheme()); }

/// @brief 与绘制侧同一枚量：`FontEngine` 量出的文字宽（dp；Headless 下 dp＝px）。
///
/// 入口簇那两枚夹缝的证人靠它把稿的坐标折成预期横坐标——代码里那枚常量变了，这里的预期**不变**
/// （测试侧按稿写死），于是墨迹与预期当场分叉。判据只比坐标、不比像素数，故字体度量本身浮动
/// （换字体、换 hinting 档）只会让两枚盒一起挪，不会凭空转红。
[[nodiscard]] auto text_width_px(const std::string &utf8, const double size_pt) -> double {
    if (utf8.empty()) {
        return 0.0;
    }
    au::Font font;
    font.size_pt = static_cast<float>(size_pt);
    return static_cast<double>(au::render::FontEngine::measure_width(utf8, font));
}

/// @brief 把一帧里某色道的墨迹裁到横区间 `[x0, x1]`（含端点）。
///
/// `text_dim` 在同一帧里同时是 `＋`、`⌄`、`新建` 与未选中格名称的落笔色 ⇒ 单靠色道分不开这几簇，
/// 故这里按横坐标裁。区间由调用方按稿算出的坐标给，判据的承重句是「裁出来的那一簇的外接框等于预期」。
[[nodiscard]] auto clip_x(const Points &pts, const std::size_t x0, const std::size_t x1) -> Points {
    Points out;
    for (const auto &point : pts) {
        if (point.first >= x0 && point.first <= x1) {
            out.insert(point);
        }
    }
    return out;
}

[[nodiscard]] auto to_u32(const char *text) -> std::u32string {
    std::u32string out;
    for (const unsigned char ch : std::string_view{text}) {
        out.push_back(static_cast<char32_t>(ch));
    }
    return out;
}

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
    /// @param undo_entry 是否给装配层那条撤销腿（`⌄ 最近关闭` 的开关）：缺省给，`false` 即模拟「今日
    ///        这条窗没有可撤销的关闭记录」那一档——本件按裁决 7.183 的「留空即整枚不画」判它。
    explicit Harness(std::vector<std::u32string> names = std::vector<std::u32string>{U"alpha"},
                     bool undo_entry = true) : undo_entry_{undo_entry} {
        // 逐格类型缺省全空＝「装配层未登记」那一档（判据 ②）：既有八案的像素读数因此与本棒之前
        // 逐位相同，图标这条腿不可能悄悄改掉它们的差分值。
        kinds_.assign(names.size(), std::nullopt);
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

    auto clear_activity(TabId id) -> void {
        static_cast<void>(strip_.take_activity(id));
        render();
    }

    auto clear_bell(TabId id) -> void {
        static_cast<void>(strip_.take_bell_triggered(id));
        render();
    }

    /// @brief 推第 `index` 格的连接类型并排帧（`SPEC.FEAT.WS.04`；空＝未登记那一档）。
    ///
    /// 类型不是 `TabStrip` 的真值源而是装配层从 `tab_kind` 表折来的事实（裁决 7.129①），故本驱动台
    /// 另存一份按栏位下标索引的表，`.tabs` 腿照 `src/main.cpp` 那一行取它。
    ///
    /// 生产路径上类型在建标签那一刻就有 ⇒ 首帧即含图标，没有「中途改类型」这件事；本件为了差分需要
    /// 两态对照，故须像 `TabStrip` 的变更钩子那样把绘制侧标脏——不标脏就是布局缓存复用上一帧尺寸并
    /// 跳过 `on_layout`，`.tabs` 根本不重读（这条正是本套件第一条判据钉的那个缓存条件）。
    auto set_kind(std::size_t index, std::optional<conn::ConnectionType> kind) -> void {
        kinds_[index] = kind;
        bar_->mark_needs_layout();
        bar_->mark_needs_paint();
        render();
    }

    /// @brief 交入两枚入口的显示串并排帧（装配层那句 `set_action_labels` 的对应腿）。
    ///
    /// 标签只改绘制，不改布局（右端簇的盒宽在 `right_cluster` 里现算），故这里不必标脏布局；但为了
    /// 两态对照的**确定性**仍把布局也标一次：不标就会撞上本套件第一条判据钉的那个缓存条件，把「文字
    /// 变了却没重画」这种真缺陷读成缓存问题。
    auto set_labels(const std::string &new_tab_label, const std::string &recent_label) -> void {
        static_cast<void>(bar_->set_action_labels(new_tab_label, recent_label));
        bar_->mark_needs_layout();
        bar_->mark_needs_paint();
        render();
    }

    /// @brief 在栏位内第 `x_dp` 逻辑 dp、带中线处实发一次 Press+Release（走实体派发器而非静态入口：
    ///        连击判定与指针捕获须隔离在本驱动台）。
    auto click(std::size_t x_dp) -> void { click_at(x_dp, kBarHeightPx / 2U); }

    /// @brief 同上，但纵坐标由调用方给——右端那两枚入口的「热区通栏而字形只占中线」那条判据要用它。
    auto click_at(std::size_t x_dp, std::size_t y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = static_cast<float>(x_dp), .y = static_cast<float>(y_dp)};
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
    [[nodiscard]] auto add_new_calls() const noexcept -> std::size_t { return add_new_calls_; }
    [[nodiscard]] auto undo_close_calls() const noexcept -> std::size_t { return undo_close_calls_; }
    [[nodiscard]] auto note_press_calls() const noexcept -> std::size_t { return note_press_calls_; }

    /// @brief 两帧之间**任何通道**不同的像素坐标（差分是本套件传统的定位手段：颜色曾经是实现细节，
    ///        而现在角标按 token 走色道、比色反而成了更强的证人，差分留给「整帧有没有连带改动」）。
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

    /// @brief 一帧里**逐位等于**给定色道（RGBA 四通道）的像素坐标。
    ///
    /// 实心圆点的芯由此定位：`fill_sector` 只在覆盖率 1 的像素上落原色，边上各羽化 1 px ⇒ 这批点就是
    /// 那一枚自己的一笔，不受同色道之外任何墨迹（选中底、下划线、名称）牵连。
    [[nodiscard]] static auto color_points(const Frame &frame, const au::Color &ink) -> Points {
        Points out;
        const std::size_t total = buffer_width() * buffer_height();
        for (std::size_t pixel = 0; pixel < total; ++pixel) {
            const std::size_t index = pixel * 4U;
            if (frame[index] == static_cast<std::uint8_t>(ink.r) &&
                frame[index + 1U] == static_cast<std::uint8_t>(ink.g) &&
                frame[index + 2U] == static_cast<std::uint8_t>(ink.b) &&
                frame[index + 3U] == static_cast<std::uint8_t>(ink.a)) {
                out.insert({pixel % buffer_width(), pixel / buffer_width()});
            }
        }
        return out;
    }

    [[nodiscard]] static auto count_color(const Frame &frame, const au::Color &ink) -> std::size_t {
        return color_points(frame, ink).size();
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

    /// @brief 两簇的公共像素个数（碰撞判据：非零即撞）。判据 ④ 的几簇各与同一基线帧差分而得，
    ///        故「哪两枚在同一条墨迹上」在这里就是一个数，不必先把簇折成外接矩形再比矩形（那会漏掉
    ///        「同带但不相交」与「矩形相交但墨迹不相交」两种情形）。
    [[nodiscard]] static auto overlap(const Points &a, const Points &b) -> std::size_t {
        std::size_t out = 0U;
        for (const auto &point : a) {
            if (b.find(point) != b.end()) {
                ++out;
            }
        }
        return out;
    }

    /// @brief 矩形之内是否只有**一种**像素值（「这里什么都没画」的颜色无关说法：矩形落在单一底色上）。
    [[nodiscard]] static auto is_uniform(const Frame &frame, std::size_t x0, std::size_t y0, std::size_t x1,
                                         std::size_t y1) -> bool {
        const std::size_t width = buffer_width();
        const auto pixel = [&](std::size_t x, std::size_t y) -> const std::uint8_t * {
            return frame.data() + (y * width + x) * 4U;
        };
        for (std::size_t y = y0; y <= y1; ++y) {
            for (std::size_t x = x0; x <= x1; ++x) {
                if (std::memcmp(pixel(x, y), pixel(x0, y0), 4U) != 0) {
                    return false;
                }
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
    /// @brief 逐格连接类型（与 `src/main.cpp` 的 `tab_kind` 表同位，按栏位下标索引）。
    std::vector<std::optional<conn::ConnectionType>> kinds_;
    bool undo_entry_ = true;  ///< 须在 `bar_` 之前初始化：构造钩子表时就要读它。
    std::shared_ptr<TabBarWidget> bar_ = std::make_shared<TabBarWidget>(TabBarHooks{
        .tabs = [this]() -> std::vector<TabVisual> {
            std::vector<TabVisual> out;
            const std::optional<TabId> selected = strip_.selected();
            std::size_t index = 0U;
            for (const auto &tab : strip_.tabs()) {
                out.push_back(TabVisual{
                    .id = tab.id,
                    .display_name = borealis::ui::resolve_tab_name(tab.names, TabNamePriority::ManualWins),
                    .is_selected = selected.has_value() && selected.value() == tab.id,
                    .has_close_button = strip_.count() > 1U,  // 末位不给关（裁决 7.43④ 同口径）
                    .bell_triggered = tab.bell_triggered,
                    .has_activity = tab.has_activity,
                    .exited = tab.exited,
                    // 类型由装配层那张表折来（`SPEC.FEAT.WS.04` 判据 ⑦ 说这一行在本仓无自动化证人，
                    // 本件是它的同形替身：被测的是绘制腿，不是这一行的存在性）。
                    .kind = index < kinds_.size() ? kinds_[index] : std::nullopt,
                });
                ++index;
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
        .add_new = [this]() -> void { ++add_new_calls_; },
        .undo_close = undo_entry_ ? std::function<void()>{[this]() -> void { ++undo_close_calls_; }}
                                  : std::function<void()>{},
        // 稿 D7 的第一处上报点：栏带上任何一次 Press 都算碰过这一扇窗，命中与否无关。
        .note_press = [this]() -> void { ++note_press_calls_; },
    });
    au::Node root_{std::static_pointer_cast<au::Widget>(bar_)};
    au::FocusManager focus_;
    au::EventDispatcher pointer_;  ///< 本驱动台私有的连击判定与指针捕获状态。
    std::size_t change_hook_calls_ = 0U;
    std::size_t add_new_calls_ = 0U;
    std::size_t undo_close_calls_ = 0U;
    std::size_t note_press_calls_ = 0U;
};

/// @brief 单枚角标的芯：外接档、圆心横纵、以及**芯的像素个数**（羽化后的原色像素）。
///
/// 羽化让「面积」随圆心落在像素格里的相位浮动，故这里只在**墨迹确实存在**的前提下报数，各判据按
/// 实测档写死时都留了余量；真正承重的是圆心与色道两档。
struct Dot {
    Box box{};
    std::size_t pixels = 0U;
    double center_x = 0.0;
    double center_y = 0.0;

    [[nodiscard]] auto has_core_at(std::size_t x, std::size_t y, std::size_t tol) const -> bool {
        return std::abs(static_cast<double>(center_x) - static_cast<double>(x)) <= static_cast<double>(tol) &&
               std::abs(static_cast<double>(center_y) - static_cast<double>(y)) <= static_cast<double>(tol);
    }
};

/// @brief 按色道取一帧里那一枚角标的芯（调用方须先证该色道确实有墨）。
[[nodiscard]] auto core_of(const Points &pts) -> Dot {
    Dot out;
    out.box = Harness::box_of(pts);
    out.pixels = pts.size();
    out.center_x = static_cast<double>(out.box.x0) + static_cast<double>(out.box.width() - 1U) / 2.0;
    out.center_y = static_cast<double>(out.box.y0) + static_cast<double>(out.box.height() - 1U) / 2.0;
    return out;
}

#else  // 无头后端未编译：HeadlessSurface 不在公共面上，本套件一律跳过而不是空转。

#endif

}  // namespace

AURORA_TEST_CASE(the_bar_background_follows_the_active_chrome_scheme) {
#ifdef AURORA_BACKEND_HEADLESS
    // PREF.01 稿 §4 的像素证人：同一棵标签栏在暗档与亮档各出一帧，栏底逐档等于唯一色值表里
    // 「标签栏底」那一格（`card_bg`；稿 §4 原文写的 `window_bg` 与 §1 表和 D6 冲突，以表和 D6 为准）。
    // 再切回暗档出一帧，须与首帧逐位相同——换肤在绘制侧读的是活动档这个状态量，而不是把一次改色
    // 记进控件。
    struct Restore {
        ChromeScheme saved = borealis::ui::chrome_scheme();
        ~Restore() { borealis::ui::set_chrome_scheme(saved); }
    } restore;

    borealis::ui::set_chrome_scheme(ChromeScheme::Dark);
    Harness dark;
    const Frame dark_frame = dark.pixels();
    borealis::ui::set_chrome_scheme(ChromeScheme::Light);
    Harness light;
    const Frame light_frame = light.pixels();

    // 单格标签最宽 240 dp，x=600 落在栏位之外的空白栏底；y=8 避开选中格的 2 dp 下划线（y 40..41）。
    // 本棒右端那枚 `＋` 的墨迹占 x 565..581（竖笔到 y 27），取 y=8 这一行因此仍是纯栏底。
    const auto bar_pixel = [](const Frame &frame) -> std::array<std::uint8_t, 4U> {
        const std::size_t index = (8U * static_cast<std::size_t>(kWindowWidth) + 600U) * 4U;
        return {frame[index], frame[index + 1U], frame[index + 2U], frame[index + 3U]};
    };
    const auto expected = [](const au::Color &color) -> std::array<std::uint8_t, 4U> {
        return {color.r, color.g, color.b, color.a};
    };
    const SettingsChrome dark_bg = borealis::ui::chrome_palette(ChromeScheme::Dark);
    const SettingsChrome light_bg = borealis::ui::chrome_palette(ChromeScheme::Light);
    // 反空转前提：两档栏底真不同，否则下面两句在「读到的其实是同一块」时也会绿。
    AURORA_TEST_CHECK_MSG(!(dark_bg.card_bg.r == light_bg.card_bg.r &&
                                dark_bg.card_bg.g == light_bg.card_bg.g &&
                                dark_bg.card_bg.b == light_bg.card_bg.b),
                          "the two palettes share one bar background");
    AURORA_TEST_CHECK_MSG(bar_pixel(dark_frame) == expected(dark_bg.card_bg),
                          "the dark bar bottom is not the dark card_bg");
    AURORA_TEST_CHECK_MSG(bar_pixel(light_frame) == expected(light_bg.card_bg),
                          "the light bar bottom is not the light card_bg");
    AURORA_TEST_CHECK_MSG(dark_frame != light_frame, "switching the scheme changed nothing on screen");

    borealis::ui::set_chrome_scheme(ChromeScheme::Dark);
    Harness back;
    AURORA_TEST_CHECK_MSG(back.pixels() == dark_frame,
                          "switching back to dark did not reproduce the first frame");
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

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
    // 整帧里动的只有角标那一枚：短名「alpha」远在芯之左（本档未登记类型 ⇒ 名称自 x 12 起画，芯占
    // x 67..70），于是「连带整栏重排出一片」在这里必红。
    const Box box = Harness::box_of(badge);
    AURORA_TEST_CHECK_LE(box.width(), kBadgeCoreMaxPx + 2U);
    AURORA_TEST_CHECK_LE(box.height(), kBadgeCoreMaxPx + 2U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_exit_badge_is_a_dot_on_the_draft_anchor) {
#ifdef AURORA_BACKEND_HEADLESS
    // 屏 1 稿那三枚 `circle r=3` 的落位（裁决 7.183）：圆心在栏位右端内缩 27 dp、带中线（42 的一半
    // = 21），色道走 `danger`。旧档那枚「右下角 6×6 空心环」在这里三处同时转红：纵向（旧档在下半栏
    // 而非中线）、形状（空心环的芯不含圆心）、色道（旧档走 `warning`）。
    Harness h;
    const auto before = h.pixels();
    h.set_exited(0U, true);
    const auto after = h.pixels();
    const SettingsChrome chrome = Chrome();

    const Points ring = Harness::changed(before, after);
    AURORA_TEST_REQUIRE(!ring.empty());
    const Points danger = h.color_points(after, chrome.danger);
    AURORA_TEST_REQUIRE(!danger.empty());
    // 差分簇**就是**那一枚 `danger` 点：色道唯一 ⇒ 两簇的芯重合。
    AURORA_TEST_CHECK_EQ(Harness::overlap(ring, danger), danger.size());

    const Dot dot = core_of(danger);
    AURORA_TEST_CHECK_GE(dot.box.width(), kBadgeCoreMinPx - 1U);
    AURORA_TEST_CHECK_LE(dot.box.width(), kBadgeCoreMaxPx);
    AURORA_TEST_CHECK_GE(dot.box.height(), kBadgeCoreMinPx - 1U);
    AURORA_TEST_CHECK_LE(dot.box.height(), kBadgeCoreMaxPx);
    AURORA_TEST_CHECK_MSG(dot.has_core_at(kSingleTabWidthPx - kBadgeAnchorInsetPx, kBarHeightPx / 2U, 1U),
                          "the exit dot is not on the draft anchor (27 dp from the tab's right edge, mid-row)");
    // 实心档：芯里有圆心那一格（旧档的空心环正是靠这一句判出来）。
    AURORA_TEST_CHECK_GE(dot.pixels, (kBadgeRadiusPx - 1U) * (kBadgeRadiusPx - 1U));
    // 与另外两枚 token 分道：同一帧里 `warning`／`success` 一格都没有。
    AURORA_TEST_CHECK_EQ(h.count_color(after, chrome.warning), std::size_t{0});
    AURORA_TEST_CHECK_EQ(h.count_color(after, chrome.success), std::size_t{0});
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_exit_dot_is_drawn_on_the_selected_tab_too) {
#ifdef AURORA_BACKEND_HEADLESS
    // 与活动角标的分工（裁决 7.82①：活动只在未选中格显形）在这里是可判的：两格里退出点的**形状与
    // 落位**必须一致，只是横坐标随栏位走。选中格不画点，用户就在一个已退出的格子里打字时看不见它。
    Harness selected{std::vector<std::u32string>{U"alpha"}};
    Harness unselected{std::vector<std::u32string>{U"alpha", U"beta"}};  // 新建即选中末位，故 0 号格未选中

    const auto selected_before = selected.pixels();
    const auto unselected_before = unselected.pixels();
    selected.set_exited(0U, true);
    unselected.set_exited(0U, true);
    const auto selected_after = selected.pixels();
    const auto unselected_after = unselected.pixels();

    const Points on_selected = Harness::changed(selected_before, selected_after);
    const Points on_unselected = Harness::changed(unselected_before, unselected_after);
    AURORA_TEST_REQUIRE(!on_selected.empty());
    AURORA_TEST_REQUIRE(!on_unselected.empty());

    const Box a = Harness::box_of(on_selected);
    const Box b = Harness::box_of(on_unselected);
    AURORA_TEST_CHECK_EQ(a.width(), b.width());
    AURORA_TEST_CHECK_EQ(a.height(), b.height());
    AURORA_TEST_CHECK_EQ(a.y0, b.y0);  // 纵向落位与选中态无关
    // 两档各走 `danger` 色道（选中底 `control_bg` 与下划线 `accent` 都吃不掉它，这一句同时是
    // 「选中格仍画」的色道证人）。
    const au::Color danger = Chrome().danger;
    AURORA_TEST_CHECK_GT(selected.count_color(selected_after, danger), std::size_t{0U});
    AURORA_TEST_CHECK_GT(unselected.count_color(unselected_after, danger), std::size_t{0U});
    // 0 号格在两档里同宽同位（一格的栏位与两格的 0 号栏位都钳在 96 dp 下限），故圆心横坐标也相同。
    AURORA_TEST_CHECK_EQ(a.x0, b.x0);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_exit_and_bell_dots_pack_leftward_in_priority_order) {
#ifdef AURORA_BACKEND_HEADLESS
    // 屏 1 稿把三枚画成同形同带的圆点，分档只靠色道与次序（裁决 7.183）：断线最占眼位、占稿那个
    // 锚位，BEL 次之向左一枚。旧档「右上实心 vs 右下空心」的两簇按 y 分家的情形自此不再可能——
    // 两枚撞在同一格里时这里必红（`overlap` 非零）。
    Harness h;
    const auto before = h.pixels();
    static_cast<void>(h.strip().mark_bell_triggered(0U));
    static_cast<void>(h.strip().set_exited(0U, true));
    h.render();
    const auto after = h.pixels();
    const SettingsChrome chrome = Chrome();

    const Points both = Harness::changed(before, after);
    AURORA_TEST_REQUIRE(!both.empty());
    const Points exit_pts = h.color_points(after, chrome.danger);
    const Points bell_pts = h.color_points(after, chrome.warning);
    AURORA_TEST_REQUIRE(!exit_pts.empty());
    AURORA_TEST_REQUIRE(!bell_pts.empty());

    const Dot exit_dot = core_of(exit_pts);
    const Dot bell_dot = core_of(bell_pts);
    AURORA_TEST_CHECK_EQ(exit_dot.box.x0, kSingleTabWidthPx - kBadgeAnchorInsetPx - kBadgeCoreLeftOfCentrePx);
    AURORA_TEST_CHECK_EQ(bell_dot.box.x0, exit_dot.box.x0 - kBadgePitchPx);  // 向左一枚 pitch
    AURORA_TEST_CHECK_EQ(Harness::overlap(exit_pts, bell_pts), std::size_t{0U});
    // 同带：两枚圆心的纵坐标相同（羽化档也一样，故比的是外接框而非像素集合）。
    AURORA_TEST_CHECK_EQ(exit_dot.box.y0, bell_dot.box.y0);
    AURORA_TEST_CHECK_EQ(exit_dot.box.y1, bell_dot.box.y1);
    // 动过的像素只有这两枚的墨迹（含羽化边）：两簇并起来恰是差分集。
    Points ink = exit_pts;
    ink.insert(bell_pts.begin(), bell_pts.end());
    AURORA_TEST_CHECK_EQ(Harness::overlap(both, ink), ink.size());
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_three_dot_cluster_stays_clear_of_the_squeezed_name) {
#ifdef AURORA_BACKEND_HEADLESS
    // 三枚同亮的退化档（`lit_badge_count` 那条让位式）：选中格的活动位不显形，故本档亮两枚，最左一枚
    // 的芯左沿实测在 57（圆心 59＝栏位右端 96 − 锚 27 − 一枚 pitch 10），名称那一笔到不了它。
    // 这里用**同色道之外**的名称墨迹做证人：名称色 `text` 在选中格上是正文白，`text` 点集的最右者就是
    // 名称右端。
    Harness h;
    h.set_exited(0U, true);
    h.mark_bell(0U);
    h.strip().mark_activity(0U);  // 选中格的活动位不显形（裁决 7.82①），故这一句只证「让位按点亮数算」
    h.render();
    const auto frame = h.pixels();
    const SettingsChrome chrome = Chrome();

    const Points danger = h.color_points(frame, chrome.danger);
    const Points warning = h.color_points(frame, chrome.warning);
    AURORA_TEST_REQUIRE(!danger.empty());
    AURORA_TEST_REQUIRE(!warning.empty());
    // 选中格只亮两枚（活动被让掉），故簇宽就是一枚 pitch。
    const Dot exit_dot = core_of(danger);
    const Dot bell_dot = core_of(warning);
    AURORA_TEST_CHECK_EQ(bell_dot.box.x0, exit_dot.box.x0 - kBadgePitchPx);
    AURORA_TEST_CHECK_EQ(Harness::overlap(danger, warning), std::size_t{0U});
    // 名称那一笔不越进角标带：`text` 的最右像素仍在栏位左半。
    const Points name = h.color_points(frame, chrome.text);
    AURORA_TEST_REQUIRE(!name.empty());
    AURORA_TEST_CHECK_LT(Harness::box_of(name).x1, bell_dot.box.x0);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_selected_cells_activity_neither_lands_nor_lets_place) {
#ifdef AURORA_BACKEND_HEADLESS
    // 「占位与落墨是同一句话」（`lit_badge_count` 的头注释，裁决 7.82① 的屏幕形态）在这一格里两头都可判：
    // 选中格的活动位既不落墨，也不许从名称的可截断宽里扣那一枚 pitch。判据取**整帧逐位相同**——
    // 该格本已亮着断线与 BEL 两枚（名称长到截断线就落在角标带上，占位真的在起作用），此后只推活动位：
    //  - 绘制腿若按「活动即落墨」画（去掉 `!tab.is_selected`），多出一枚 `success` 点 ⇒ 帧变；
    //  - 计数控件若按「活动即占位」算（去掉同一句），簇宽从两枚涨到三枚、名称少截 10 dp ⇒ 帧变。
    // 两头都不必写像素数、也不必赌字体度量：这个常量在这里是双向可判的。
    Harness h{std::vector<std::u32string>{U"alpha-bravo-charlie-delta"}};
    h.set_exited(0U, true);
    h.mark_bell(0U);
    const auto before = h.pixels();
    const SettingsChrome chrome = Chrome();

    // 反空转在前：两枚确实落墨，且第三枚（活动）按裁决 7.82① 不在——缺了这一句，「整帧相同」就可能
    // 是「什么都没画」的另一种写法。
    const Points danger = Harness::color_points(before, chrome.danger);
    const Points warning = Harness::color_points(before, chrome.warning);
    AURORA_TEST_REQUIRE(!danger.empty());
    AURORA_TEST_REQUIRE(!warning.empty());
    AURORA_TEST_REQUIRE_EQ(Harness::count_color(before, chrome.success), std::size_t{0U});
    // 占位这条腿也要真的在起作用，否则「计数控件多算一枚」在屏幕上根本体现不出来。证人取同名的
    // **无角标**那一格：名称墨迹的右沿在没有角标时更远 ⇒ 角标簇确实从可截断宽里扣了东西。
    Harness bare{std::vector<std::u32string>{U"alpha-bravo-charlie-delta"}};
    const Box dotted_name = Harness::box_of(Harness::color_points(before, chrome.text));
    const Box bare_name = Harness::box_of(Harness::color_points(bare.pixels(), chrome.text));
    AURORA_TEST_REQUIRE_GT(dotted_name.x1, std::size_t{0U});
    AURORA_TEST_REQUIRE_GT(bare_name.x1, std::size_t{0U});
    AURORA_TEST_REQUIRE_LT(dotted_name.x1, bare_name.x1);
    // 名称墨迹始终留在最左一枚角标的左沿之外（本例只有一格 ⇒ 未选中态不成立，活动不落墨）。
    AURORA_TEST_CHECK_LT(dotted_name.x1, Harness::box_of(warning).x0);

    h.mark_activity(0U);  // 同一格、同一选中态，只多推一位活动
    const auto after = h.pixels();
    AURORA_TEST_REQUIRE_TRUE(h.strip().selected().has_value());
    AURORA_TEST_CHECK_TRUE(h.strip().tabs()[0].has_activity);  // 真值源确实收到了，屏幕才该无动于衷
    AURORA_TEST_CHECK_MSG(after == before,
                          "the selected cell's activity either drew a dot or squeezed the name");
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
    // 本棒之后这一簇就是 `warning` 那一枚的墨迹：色道证人取**芯**（逐位等于该档的那批像素），差分簇
    // 含羽化边、比芯大一圈（探针实测 4×4 芯对 6×6 簇），故这里断的是「芯整枚落在差分簇里」而不是
    // 两个数相等——后者等于把抗锯齿档钉成判据（裁决 7.183）。
    const Points bell_core = Harness::color_points(ringing, Chrome().warning);
    AURORA_TEST_REQUIRE(!bell_core.empty());
    AURORA_TEST_CHECK_EQ(Harness::overlap(bell, bell_core), bell_core.size());

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
    // 逐格独立在屏幕层的兑现：第二格点亮时第一格的点**一个像素都不许动**，且两簇不相交、横向错开
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
    AURORA_TEST_CHECK_LE(Harness::box_of(second_ring).width(), kBadgeCoreMaxPx + 2U);
    // 前一颗在这一步之后逐位未动：第二格的点亮不许牵连第一格。
    AURORA_TEST_CHECK_TRUE(Harness::same_at(one_lit, two_lit, first_ring));

    const Box a = Harness::box_of(first_ring);
    const Box b = Harness::box_of(second_ring);
    AURORA_TEST_CHECK_EQ(a.y0, b.y0);
    AURORA_TEST_CHECK_GT(b.x0, a.x1);  // 后一颗在前一颗之右，两簇不相交
    // 两枚同色道 ⇒ 芯的像素数是单枚的整数倍（本档相位相同，实测两倍）。
    const std::size_t single = h.count_color(one_lit, Chrome().danger);
    AURORA_TEST_CHECK_EQ(h.count_color(two_lit, Chrome().danger), single * 2U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(an_unregistered_kind_draws_nothing_and_the_registered_one_lands_in_the_slot) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ②（稿 §2）：`kind` 空 ⇒ 左槽**不画也不占位**（裁决 7.38⑥「缺席的入口整枚不画」同口径，
    // D5）。空名格用来给「这一带根本没有墨」一个颜色无关的证人——名称本身也从 x+12 起画，有名格判不出
    // 「没有占位符」。反空转在后半：同一台驱动台把类型登记上，同一矩形立刻不再单一。
    //
    // 本棒之后 x 12..23 这一带仍是**纯底**（右端那枚 `＋` 在 570 之外，角标簇在栏位右端），故两态判据
    // 一字未改。
    Harness h{std::vector<std::u32string>{U""}};
    AURORA_TEST_CHECK_TRUE(Harness::is_uniform(h.pixels(), kGlyphSlotX0, kGlyphTopRow, kGlyphSlotX1,
                                               kGlyphBottomRow));

    h.set_kind(0U, ConnectionType::Local);
    AURORA_TEST_CHECK_FALSE(Harness::is_uniform(h.pixels(), kGlyphSlotX0, kGlyphTopRow, kGlyphSlotX1,
                                                kGlyphBottomRow));
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_registered_kind_widens_the_slot_by_exactly_eighteen_dp) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ③：栏位宽＝今日公式 + 18 dp。几何证人取右锚那颗 BEL 的**芯**——它的圆心在 `x + w - 27`，
    // 是 `tab_width()` 的读数锚，而锚本身的大小不变（r=3）。两态各按色道取芯，故长名被角标挤掉的
    // 那截省略号不参与本判据（本棒之前它靠差分定位，锚是 6×6 实心点）。
    // 名称取中等长度：短名贴 96 下限、长名贴 240 上限，两端都会把槽宽吃掉一截（D4 记的就是这条代价），
    // 只有落在两界之间时「恰为 18 dp」才谈得上。下面两枚边界断言就是「本例没被钳位吃掉」的证人。
    Harness h{std::vector<std::u32string>{to_u32("checkpoint-pipeline")}};
    h.mark_bell(TabId{0U});
    const Points without_slot_pts = h.color_points(h.pixels(), Chrome().warning);
    AURORA_TEST_REQUIRE(!without_slot_pts.empty());
    const Box without_slot = Harness::box_of(without_slot_pts);
    AURORA_TEST_CHECK_GE(without_slot.width(), kBadgeCoreMinPx - 1U);
    AURORA_TEST_CHECK_LE(without_slot.width(), kBadgeCoreMaxPx);
    AURORA_TEST_CHECK_GT(without_slot.x1, kTabMinWidthPx);  // 未落下限：这一格的宽是实测值而不是钳位值

    h.clear_bell(TabId{0U});
    const auto widened = h.pixels();
    h.set_kind(0U, ConnectionType::Local);
    AURORA_TEST_CHECK_FALSE(Harness::changed(widened, h.pixels()).empty());  // 先证「这一动真的发生」
    h.mark_bell(TabId{0U});
    const Points with_slot_pts = h.color_points(h.pixels(), Chrome().warning);
    AURORA_TEST_REQUIRE(!with_slot_pts.empty());
    const Box with_slot = Harness::box_of(with_slot_pts);
    AURORA_TEST_CHECK_LE(with_slot.width(), kBadgeCoreMaxPx);
    AURORA_TEST_CHECK_LT(with_slot.x1, kTabMaxWidthPx);  // 也未顶上限
    AURORA_TEST_CHECK_EQ(with_slot.x0 - without_slot.x0, kTypeSlotTotal);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_new_tab_entry_hits_the_plus_it_paints) {
#ifdef AURORA_BACKEND_HEADLESS
    // 屏 1 稿右端那枚 `＋`（裁决 7.183：`＋` 与「新建」是同一枚入口的两笔）。证人分四段：① 墨迹落在
    // 稿那锚折出的圆心上；② 圆心实发一次点击即走 `add_new` 而不走撤销；③ 向右那截箭位只走撤销——两枚
    // 盒不相交，串台即此红；④ 稿那 16 dp 右沿内缩带**不吃**点击（否则栏右端那条空白成了第二热区）。
    //
    // ① 的墨迹证人按「该矩形不再等于栏底」取，不按色道：`＋` 是 1.6 dp 描边，探针实测**没有**逐位等于
    // `text_dim` 的芯（四边全在半覆盖档上），比色在这枚上判不出东西，而 `⌄` 那 1.4 dp 折线偏有——
    // 两枚的档不同，正是「面积不能写成等号」那条口径的又一处实证。
    const std::size_t plus_x = static_cast<std::size_t>(kWindowWidth) - kPlusCenterInsetWithUndoPx;
    Harness h;
    h.set_labels("", "");  // 只留字形，不留文字：右端簇自此就是那一枚 `＋` 加一枚 `⌄`
    const Frame frame = h.pixels();
    AURORA_TEST_CHECK_FALSE(Harness::is_uniform(frame, plus_x - kPlusSizePx / 2U, kBarHeightPx / 2U - 5U,
                                                plus_x + kPlusSizePx / 2U, kBarHeightPx / 2U + 5U));
    // 反空转在后：同一帧里稿那 16 dp 内缩带是一整片栏底 ⇒ 上一句的墨不是「空白里随便什么」。
    AURORA_TEST_CHECK_TRUE(Harness::is_uniform(frame, static_cast<std::size_t>(kWindowWidth) - kClusterRightPadPx + 2U,
                                               12U, static_cast<std::size_t>(kWindowWidth) - 1U, 30U));

    h.click(plus_x);
    AURORA_TEST_CHECK_EQ(h.add_new_calls(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(h.undo_close_calls(), std::size_t{0});

    h.click_at(plus_x, 4U);  // 字形之外、盒之内：仍算这一枚入口
    AURORA_TEST_CHECK_EQ(h.add_new_calls(), std::size_t{2});

    h.click(static_cast<std::size_t>(kWindowWidth) - 2U);  // 内缩带之内：什么都不该发生
    AURORA_TEST_CHECK_EQ(h.add_new_calls(), std::size_t{2});
    AURORA_TEST_CHECK_EQ(h.undo_close_calls(), std::size_t{0});

    h.click(static_cast<std::size_t>(kWindowWidth) - kClusterRightPadPx - 4U);  // 箭位那截盒
    AURORA_TEST_CHECK_EQ(h.undo_close_calls(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(h.add_new_calls(), std::size_t{2});
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_recent_close_entry_appears_only_with_the_undo_hook) {
#ifdef AURORA_BACKEND_HEADLESS
    // 「留空即整枚不画」的屏幕形态（裁决 7.183）：`⌄ 最近关闭` 是**一枚入口的两笔**，装配层没给撤销腿
    // 时两者一起缺席——箭头与文字都不落墨，热区也不吃点击。两台驱动台同宽同标签，唯一差别就是那枚
    // `std::function` 在不在位。
    Harness without{std::vector<std::u32string>{U"alpha"}, false};
    without.set_labels("New", "Recent");
    Harness with{std::vector<std::u32string>{U"alpha"}, true};
    with.set_labels("New", "Recent");
    const SettingsChrome chrome = Chrome();

    // 「最近关闭」那行字走 `text_faint`（稿 `#62728A`）：本档 `text_faint` 全仓只此一处，故这一色道
    // 就是「这一枚有没有画」的颜色无关替身。
    const std::size_t faint_without = without.count_color(without.pixels(), chrome.text_faint);
    const std::size_t faint_with = with.count_color(with.pixels(), chrome.text_faint);
    AURORA_TEST_CHECK_EQ(faint_without, std::size_t{0});
    AURORA_TEST_CHECK_GT(faint_with, std::size_t{0});

    // 同一档差异不能只由文字给出：箭头与文字是一枚入口的两笔，缺席时**整条带**都 bare。探针实测不在位
    // 那一档 `＋ 新建` 盒的右沿＝640−16−31＝593，故 595 起至栏右端在缺席档是一整片栏底、在位档不再单一。
    const std::size_t band_x0 = static_cast<std::size_t>(kWindowWidth) - kClusterRightPadPx - kClusterGapPx + 2U;
    AURORA_TEST_CHECK_TRUE(Harness::is_uniform(without.pixels(), band_x0, 12U,
                                               static_cast<std::size_t>(kWindowWidth) - 1U, 30U));
    AURORA_TEST_CHECK_FALSE(Harness::is_uniform(with.pixels(), band_x0, 12U,
                                                static_cast<std::size_t>(kWindowWidth) - 1U, 30U));

    // 热区同理：撤销腿不在位时那一带不吃任何点击（`add_new` 也不吃——它在自己盒之右的空白里）。
    without.click(static_cast<std::size_t>(kWindowWidth) - 20U);
    AURORA_TEST_CHECK_EQ(without.undo_close_calls(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(without.add_new_calls(), std::size_t{0});

    with.click(static_cast<std::size_t>(kWindowWidth) - 20U);
    AURORA_TEST_CHECK_EQ(with.undo_close_calls(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(with.add_new_calls(), std::size_t{0});
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_plus_sits_the_drafts_eight_dp_left_of_the_label_it_paints) {
#ifdef AURORA_BACKEND_HEADLESS
    // 稿那枚「`＋` 右缘 1362 → 文字左沿 1370」的 8 dp 夹缝（裁决 7.183）。这一档容易整枚地漏判，
    // 因为 `right_cluster` 里`＋` 的位置与文字的位置**由同一枚常量折出**、且方向相反（`total` 含它、
    // 文字盒左沿 = 字形右缘 + 它），故「盒与墨迹同源」那一类证人（命中测试、圆心和稿锚的差）对它
    // 全部免疫——实测的存活变异就是这么来的。这里的证人因此只有一条路：**拿墨迹比墨迹**——
    // 字形包络与文字墨迹之间那截必须是纯栏底，而两枚盒的左沿各自按稿的数（测试侧写死的 8）算。
    const double undo_right = static_cast<double>(kWindowWidth) - static_cast<double>(kClusterRightPadPx);
    const double recent_w = text_width_px("Recent", kRecentLabelFontPt);
    const double new_w = text_width_px("New", kNewLabelFontPt);
    const double undo_left =
        undo_right - recent_w - static_cast<double>(kChevronToLabelGapPx + kChevronWidthPx);
    const double new_left =
        undo_left - static_cast<double>(kClusterGapPx + kPlusSizePx + kPlusToLabelGapPx) - new_w;
    const double glyph_right = new_left + static_cast<double>(kPlusSizePx);
    const double label_left = glyph_right + static_cast<double>(kPlusToLabelGapPx);
    const auto at = [](const double v) -> std::size_t { return static_cast<std::size_t>(std::lround(v)); };

    Harness h;
    h.set_labels("New", "Recent");
    const Frame frame = h.pixels();

    // 反空转在前：两枚都真的落了墨，否则下面那句「夹缝是纯栏底」就是「这里什么都没画」的另一种写法。
    AURORA_TEST_CHECK_FALSE(Harness::is_uniform(frame, at(new_left), kClusterBandTop, at(glyph_right),
                                               kClusterBandBottom));
    AURORA_TEST_CHECK_FALSE(Harness::is_uniform(frame, at(label_left), kLabelBandTop,
                                               at(label_left + new_w), kLabelBandBottom));
    // 夹缝：字形包络右沿与文字落笔盒左沿之间（各让 2 px 给描边羽化与字形左侧羽化）是纯栏底。
    AURORA_TEST_CHECK_TRUE(Harness::is_uniform(frame, at(glyph_right) + 2U, kLabelBandTop,
                                              at(label_left) - 2U, kLabelBandBottom));
    // 字形左沿之外那一截同样是纯栏底（稿：`＋` 包络左沿之前是 31 dp 的簇间距）。两句合起来是一对
    // 括号——夹缝常量若归零，字形整枚右移 8 dp 落进右括号那条带里，当场转红；字形画得偏左也同样红。
    // 取「非某色道的墨」而不是「某色道的墨迹外接框」：`＋` 是 1.6 dp 描边，实测**没有**逐位等于
    // `text_dim` 的芯（本套件上面那条口径，裁决 7.183），比色在这枚上判不出东西。
    AURORA_TEST_CHECK_TRUE(Harness::is_uniform(frame, at(new_left) - 6U, kClusterBandTop,
                                              at(new_left) - 2U, kClusterBandBottom));
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_chevron_sits_the_drafts_five_dp_left_of_the_label_it_paints) {
#ifdef AURORA_BACKEND_HEADLESS
    // 「⌄ 最近关闭」那两笔之间稿量出的 5 dp 夹缝（箭头右缘 1433 → 文字左沿 1438）。这一档比 `＋` 那枚
    // 好判：**两笔不同色道**——箭头随 `text_dim`（与 `＋` 同档），文字走 `text_faint`（稿 `#62728A`），
    // 而本档全栏只此一处用 `text_faint` ⇒ 墨迹天然分家，直接比两簇的外接框。
    //
    // 关键是箭头的落位**只**由这枚夹缝决定，而文字盒右锚在 `undo_right − 文字宽` 上、与夹缝无关：
    // 夹缝归零时文字一格不动、箭头整枚向右挪 5 dp，四句坐标断言当场转红（存活变异的实测来源）。
    const double undo_right = static_cast<double>(kWindowWidth) - static_cast<double>(kClusterRightPadPx);
    const double recent_w = text_width_px("Recent", kRecentLabelFontPt);
    const double chevron_left = undo_right - recent_w - static_cast<double>(kChevronToLabelGapPx + kChevronWidthPx);
    const double chevron_right = chevron_left + static_cast<double>(kChevronWidthPx);
    const double label_left = undo_right - recent_w;
    const auto at = [](const double v) -> std::size_t { return static_cast<std::size_t>(std::lround(v)); };

    Harness h;
    h.set_labels("New", "Recent");
    const Frame frame = h.pixels();
    const SettingsChrome chrome = Chrome();

    const Points label = Harness::color_points(frame, chrome.text_faint);
    AURORA_TEST_REQUIRE(!label.empty());
    const Box lb = Harness::box_of(label);
    const Points chevron = clip_x(Harness::color_points(frame, chrome.text_dim), at(chevron_left) - 4U,
                                  at(chevron_right) + 4U);
    AURORA_TEST_REQUIRE(!chevron.empty());
    const Box cb = Harness::box_of(chevron);

    // 两笔各自压在稿算的坐标上（±2 给 1.4 dp 描边的羽化与字形左侧 bearings）。
    AURORA_TEST_CHECK_GE(cb.x0, at(chevron_left) - 2U);
    AURORA_TEST_CHECK_LE(cb.x0, at(chevron_left) + 2U);
    AURORA_TEST_CHECK_GE(cb.x1, at(chevron_right) - 2U);
    AURORA_TEST_CHECK_LE(cb.x1, at(chevron_right) + 2U);
    AURORA_TEST_CHECK_GE(lb.x0, at(label_left) - 2U);
    AURORA_TEST_CHECK_LE(lb.x1, at(undo_right) + 2U);
    // 夹缝本身：箭头墨迹右沿到文字墨迹左沿，实测档 = 稿的 5 dp 减去两侧羽化，取 [2, 8] 这一带。
    AURORA_TEST_CHECK_GE(lb.x0, cb.x1 + 2U);
    AURORA_TEST_CHECK_LE(lb.x0, cb.x1 + kChevronToLabelGapPx + 3U);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_plus_moves_to_the_bare_right_inset_when_no_undo_entry_lands) {
#ifdef AURORA_BACKEND_HEADLESS
    // 撤销腿不在位时，`＋` 回稿那个「文字右缘距栏右端 16 dp」的裸内缩档（640−52＝588），而不是在位档
    // 的 575。两档差 13 dp＝箭位 8 加夹缝 5，远在字体噪声之外，故这一例是**门控本身**的证人：
    // `right_cluster` 里那句 `if (hooks_.undo_close)` 一旦恒真，缺席档的箭位照旧占住 13 dp、
    // `＋` 整枚挪到在位档的锚上——两句反空转配对当场互斥（实测存活变异 p08 的来路：绘制腿另有
    // 一重同名门控，所以墨迹判据只能由**位置**给出，不能由「有没有落墨」给出）。
    Harness without{std::vector<std::u32string>{U"alpha"}, false};
    without.set_labels("", "");  // 只留字形：右端簇自此就是孤零零一枚 `＋`
    const Frame frame = without.pixels();
    const std::size_t bare = static_cast<std::size_t>(kWindowWidth) - kPlusCenterInsetNoUndoPx;
    const std::size_t parked = static_cast<std::size_t>(kWindowWidth) - kPlusCenterInsetWithUndoPx;
    const std::size_t half = kPlusSizePx / 2U;

    AURORA_TEST_CHECK_FALSE(Harness::is_uniform(frame, bare - half, kClusterBandTop, bare + half,
                                                kClusterBandBottom));
    AURORA_TEST_CHECK_TRUE(Harness::is_uniform(frame, parked - half, kClusterBandTop, parked + half,
                                               kClusterBandBottom));
    // 热区与墨迹同源（`right_cluster` 一处算式）：点这一格即新建，且在位档那一格什么都不吃。
    without.click(bare);
    AURORA_TEST_CHECK_EQ(without.add_new_calls(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(without.undo_close_calls(), std::size_t{0});
    without.click(parked);
    AURORA_TEST_CHECK_EQ(without.add_new_calls(), std::size_t{1});
    AURORA_TEST_CHECK_EQ(without.undo_close_calls(), std::size_t{0});
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(a_click_in_the_bar_blank_still_notes_the_press_but_starts_no_action) {
#ifdef AURORA_BACKEND_HEADLESS
    // 稿 D7 那条事实上报（`SPEC.FEAT.WS.03`）与两枚入口的边界：空白栏底吃 `note_press`（本件用计数
    // 钩子替身），但**不**新建、不撤销。这一例把右端 16 dp 内缩带之外、两枚盒之外的空白坐实成
    // 「什么都不是」，以免本棒那两枚盒画大了一号却没人发现。
    Harness h;
    h.set_labels("New", "Recent");
    static_cast<void>(h.pixels());
    h.click(static_cast<std::size_t>(kWindowWidth) - kClusterRightPadPx + 8U);  // 内缩带之外、栏右端之内
    AURORA_TEST_CHECK_EQ(h.add_new_calls(), std::size_t{0});
    AURORA_TEST_CHECK_EQ(h.undo_close_calls(), std::size_t{0});
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_icon_and_the_three_badges_land_in_four_disjoint_bands) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ④＝本棒对**既有判据**负责的证人（裁决 7.82／7.91 的地盘）：图标与三枚角标同格并存时互不相交。
    // 本棒之后三枚同带（都在中线），故「不相交」这一档由**同色道两两不重叠**与**横坐标按 pitch 分开**
    // 两句话共同担住，而图标与角标之间仍是横向分离的两截。
    //
    // 三条实测出来的约束：① 三枚一起点亮时簇会向左排，故这里逐枚点亮再取走、每簇各与同一基线帧差分；
    // ② 两格都登记类型 ⇒ 栏位几何在本例内一步都不变（`kind` 从无到有会挪动邻格，那属判据 ⑤ 的另一半）；
    // ③ 活动角标只在**未选中**格显形（裁决 7.82①）而新建即选中末位，故三枚都点在第一格上——这里取
    // 三枚**同亮**那一帧按色道分簇，比逐枚差分更省一次布局扰动。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    h.set_kind(0U, ConnectionType::Serial);
    h.set_kind(1U, ConnectionType::Serial);
    const auto base = h.pixels();
    static_cast<void>(h.strip().set_exited(0U, true));
    static_cast<void>(h.strip().mark_bell_triggered(0U));
    static_cast<void>(h.strip().mark_activity(0U));
    h.render();
    const auto lit = h.pixels();
    const SettingsChrome chrome = Chrome();

    const Points danger = h.color_points(lit, chrome.danger);
    const Points warning = h.color_points(lit, chrome.warning);
    const Points success = h.color_points(lit, chrome.success);
    AURORA_TEST_REQUIRE(!danger.empty());
    AURORA_TEST_REQUIRE(!warning.empty());
    AURORA_TEST_REQUIRE(!success.empty());

    // 字形簇：同槽宽的两枚字形互差（Local 的 `>` 对 Serial 的 `S`），几何不动 ⇒ 这一簇就是字形本身。
    Harness glyph{std::vector<std::u32string>{U"alpha", U"beta"}};
    glyph.set_kind(0U, ConnectionType::Local);
    const auto local_frame = glyph.pixels();
    glyph.set_kind(0U, ConnectionType::Serial);
    const Points icon = Harness::changed(local_frame, glyph.pixels());
    AURORA_TEST_REQUIRE(!icon.empty());

    // 三枚同带 ⇒ 纵向档位一致（这是「同形同带」的兑现，取代旧档那条「按 y 分成两簇」）。
    const Box exit_box = Harness::box_of(danger);
    const Box bell_box = Harness::box_of(warning);
    const Box activity_box = Harness::box_of(success);
    AURORA_TEST_CHECK_EQ(bell_box.y0, exit_box.y0);
    AURORA_TEST_CHECK_EQ(activity_box.y0, exit_box.y0);
    // 横向按优先级向左排：断线占锚位、BEL 向左一枚、活动再向左一枚。
    AURORA_TEST_CHECK_EQ(bell_box.x0, exit_box.x0 - kBadgePitchPx);
    AURORA_TEST_CHECK_EQ(activity_box.x0, bell_box.x0 - kBadgePitchPx);
    AURORA_TEST_CHECK_EQ(Harness::overlap(danger, warning), std::size_t{0U});
    AURORA_TEST_CHECK_EQ(Harness::overlap(warning, success), std::size_t{0U});
    AURORA_TEST_CHECK_EQ(Harness::overlap(danger, success), std::size_t{0U});
    // 字形留在它自己的槽里：左带 x 12..23、行带 y 21..32（F9 实测 + D4 拍的槽）。
    AURORA_TEST_CHECK_GE(Harness::box_of(icon).x0, kGlyphSlotX0);
    AURORA_TEST_CHECK_LE(Harness::box_of(icon).x1, kGlyphSlotX1);
    AURORA_TEST_CHECK_GE(Harness::box_of(icon).y0, kGlyphTopRow);
    AURORA_TEST_CHECK_LE(Harness::box_of(icon).y1, kGlyphBottomRow);
    // 图标与最左一枚角标横向分离（本棒之后两者同带，故只谈得上横向这一档）。
    AURORA_TEST_CHECK_LT(Harness::box_of(icon).x1, activity_box.x0);
    // 反空转：这一帧里动的墨迹只有三枚角标（图标两态都登记同一档，不动）。
    static_cast<void>(base);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(changing_one_tabs_kind_leaves_its_neighbour_bit_for_bit_identical) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ⑤：类型是逐格事实而不是整栏的。这里只在**同槽宽**的两档之间换型（`>` 对 `S` 对 `T`），
    // 栏位几何因此一步不动，差分出来的就只有那一枚字形。第一格的墨迹由它自己换型给出，用作「逐位
    // 相同」的候选点集——这一句红了就是「改一格牵连整栏重排」。
    Harness h{std::vector<std::u32string>{U"alpha", U"beta"}};
    h.set_kind(0U, ConnectionType::Local);
    h.set_kind(1U, ConnectionType::Local);
    const auto both_local = h.pixels();

    h.set_kind(0U, ConnectionType::Ssh);  // 第一格换型：这一簇就是它的墨迹所在
    const Points first_cell = Harness::changed(both_local, h.pixels());
    AURORA_TEST_REQUIRE(!first_cell.empty());
    h.set_kind(0U, ConnectionType::Local);  // 换回基线

    h.set_kind(1U, ConnectionType::Telnet);  // 第二格换型：第一格须逐位不动
    const auto second_changed = h.pixels();
    AURORA_TEST_CHECK_TRUE(Harness::same_at(both_local, second_changed, first_cell));

    const Points second_cell = Harness::changed(both_local, second_changed);
    AURORA_TEST_REQUIRE(!second_cell.empty());
    AURORA_TEST_CHECK_EQ(Harness::overlap(first_cell, second_cell), std::size_t{0U});
    AURORA_TEST_CHECK_GT(Harness::box_of(second_cell).x0, Harness::box_of(first_cell).x1);  // 后一格在前一格之右
    AURORA_TEST_CHECK_GE(Harness::box_of(second_cell).y0, kGlyphTopRow);  // 两簇都只在文本行带内
    AURORA_TEST_CHECK_LE(Harness::box_of(second_cell).y1, kGlyphBottomRow);
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

AURORA_TEST_CASE(the_four_kinds_land_four_pairwise_different_frames) {
#ifdef AURORA_BACKEND_HEADLESS
    // 判据 ① 的屏幕形态（表本体由 `utest_connection_glyph` 逐档钉死）：同名同槽，四档各画各的。
    // 两枚型别折成同一字形（＝7.110 那张两值表的复活形态）在这里现形，而不必等真机走查。
    static constexpr std::array<ConnectionType, 4U> kKinds{ConnectionType::Local,
                                                           ConnectionType::Ssh,
                                                           ConnectionType::Serial,
                                                           ConnectionType::Telnet};
    std::vector<Frame> frames;
    for (const auto kind : kKinds) {
        Harness h;
        h.set_kind(0U, kind);
        frames.push_back(h.pixels());
    }
    for (std::size_t i = 0U; i < frames.size(); ++i) {
        for (std::size_t j = i + 1U; j < frames.size(); ++j) {
            AURORA_TEST_CHECK_MSG(frames[i] != frames[j],
                                  "two connection types rendered the very same frame");
        }
    }
#else
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
#endif
}

}  // namespace borealis::test_cases::itest_tab_bar
