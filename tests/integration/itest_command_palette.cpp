/// 测试类型: integration
/// 目标单元: src/ui/settings_i18n.cpp 的命令面板两条词条 ＋ src/main.cpp 的装配处形态
/// 测试说明: G38 / G39 回货（Aurora `7b4f61fe`）后的**接货复验**，裁决 **7.92**。断的从来不是框架的
///           三档优先级（文本覆盖 > 按 key 查表 > 兜底字面量）——那是 Aurora 自有 `utest_command_palette`
///           的射程（AGENTS.md §5 第 2 条：本仓不复述框架算式）。本件只断**本仓这一侧接上了没有**：
///           - 两条词条确实被面板读到，而不是静默回落框架那两条英文字面量（key 一漂移就是这个症状，
///             而漂移**没有任何编译期痕迹**，故第二例把 key 名字本身写成判据而不只比字符串）；
///           - 解析出的占位符确实落到内置搜索框那一只控件上（裁决 **7.91** 的教训：模型层断言全绿而
///             屏幕上从未出现过，因为没有任何东西把绘制侧标脏）；
///           - **上屏**：改一次词条 → 屏幕上出现两簇分开的墨迹变化，上簇归占位符、下簇归空态提示。
///             本例只断「本仓这一侧接上了没有」的像素形态：覆盖词表里那一档的取值后，借框架**已支持**的
///             `set_placeholder_key` / `set_empty_message_key` 触发重解析（构造期与 `open()` 都不查表，
///             故不能靠 `close()+open()` 把改动送上屏——这是框架现状，运行期切语言须显式重解析），证
///             「改了词条 ⇒ 那一带的墨迹跟着换」。
///           **构建期词表落地后的两处形态变化**（本轮适配）：① 框架那两个 builtin 名字（`command_palette.
///           placeholder` / `.no_results`）在 `gen_i18n` 里是保留名，本仓词表**不能占用**，故本仓另立
///           `command_palette.field_placeholder` / `.empty_message` 两条 app 档 key，装配处必须显式
///           `set_placeholder_key`／`set_empty_message_key` 换掉那两个 Id——本件的驱动台与 `src/main.cpp`
///           同式，这一换就是「接上了没有」的全部实质。② 中文一律出自词表在运行期交出，本文件不写中文
///           字面量（§4.3 第 14 条）。无头后端是否有 CJK 面不影响判据：两条帧之间只要有一侧显形就有色差，
///           本例断的是「换了词条 ⇒ 那一带的墨迹跟着换」，不是「汉字画得好不好看」。`src/main.cpp`
///           不编入 CTest runner（裁决 7.83 那条物理事实），装配处那两行无自动化证人，代价如实登记
///           在裁决 **7.92**④。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/i18n/locale.h"
#include "aurora/i18n/message_catalog.h"
#include "aurora/widget/command_palette.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "framework/aurora_test.h"
// 构建期生成的词表头（`src/CMakeLists.txt` 的 `aurora_i18n_generate`）：本件要的正是那两条 app 档 Id。
#include "messages_zh.hpp"
// 私有头（裁决 D1①），按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"

namespace borealis::test_cases::itest_command_palette {

namespace au = aurora;

namespace {

using Palette = au::CommandPalette;

/// @brief 装配处同款的面板实例；注册表交 `nullptr` ⇒ 结果表恒空 ⇒ 空态提示那一行才是画面上唯一的列表内容。
///
/// 两条上屏文案的 Id 在这里换成本仓词表那两条 app 档 key：框架自带的两个 builtin 名字是保留名，
/// 本仓词表装不进去（`gen_i18n` 直接拒收），不换就一直读着框架那两条英文字面量。
[[nodiscard]] auto make_palette() -> std::shared_ptr<Palette> {
    auto palette = std::make_shared<Palette>(nullptr);
    palette->set_placeholder_key(borealis::msg::id::AURORA_COMMAND_PALETTE_FIELD_PLACEHOLDER);
    palette->set_empty_message_key(borealis::msg::id::AURORA_COMMAND_PALETTE_EMPTY_MESSAGE);
    return palette;
}

/// @brief 把某条词条在**本仓所用那一档 locale** 上临时改成给定文本（复验「查表是否真发生」）。
auto overwrite_entry(std::string_view key, std::string_view text) -> void {
    static_cast<void>(
        au::default_catalog().add(borealis::ui::settings_locale(), key, std::string{text}));
}

/// @brief 本档两条上屏文案的原值（覆盖之后要按它复原，别让后续用例读到被改过的那一份）。
struct PaletteStrings {
    std::string placeholder;
    std::string empty_message;
};

[[nodiscard]] auto current_palette_strings() -> PaletteStrings {
    return {borealis::ui::settings_label("command_palette.field_placeholder"),
            borealis::ui::settings_label("command_palette.empty_message")};
}

auto restore(const PaletteStrings &values) -> void {
    overwrite_entry("command_palette.field_placeholder", values.placeholder);
    overwrite_entry("command_palette.empty_message", values.empty_message);
}

/// @brief 走进一棵子树找那只内置搜索框（公共面无「取字段控件」的入口，故按树序扫类型）。
[[nodiscard]] auto find_field(const au::Widget &widget) -> const au::TextInput * {
    if (const auto *field = dynamic_cast<const au::TextInput *>(&widget)) {
        return field;
    }
    for (const au::Node &child : widget.child_nodes()) {
        if (const auto *found = find_field(child.widget()); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

}  // namespace

AURORA_TEST_CASE(both_strings_come_from_the_registered_entries_rather_than_the_fallbacks) {
    static_cast<void>(borealis::ui::install_settings_strings());
    const std::shared_ptr<Palette> palette = make_palette();

    // 面板问的是本仓那两条 key，而不是框架自带的两个 builtin Id。
    AURORA_TEST_CHECK_EQ(palette->placeholder_key(),
                         borealis::msg::id::AURORA_COMMAND_PALETTE_FIELD_PLACEHOLDER);
    AURORA_TEST_CHECK_EQ(palette->empty_message_key(),
                         borealis::msg::id::AURORA_COMMAND_PALETTE_EMPTY_MESSAGE);
    AURORA_TEST_CHECK_EQ(au::default_catalog().canonical_name(palette->placeholder_key()),
                         std::string_view{"command_palette.field_placeholder"});
    AURORA_TEST_CHECK_EQ(au::default_catalog().canonical_name(palette->empty_message_key()),
                         std::string_view{"command_palette.empty_message"});
    // 本档（缺省中文）那两句都在词表里且非空；回落症状就是屏幕上留英文兜底串，而英文兜底串与本仓
    // 英文档的取值**逐字相同**，故「读到了 app 档」只能由「本档的取值不等于那两句英文」来证。
    const std::string placeholder = borealis::ui::settings_label("command_palette.field_placeholder");
    const std::string empty_message = borealis::ui::settings_label("command_palette.empty_message");
    AURORA_TEST_CHECK_FALSE(placeholder.empty());
    AURORA_TEST_CHECK_FALSE(empty_message.empty());
    AURORA_TEST_CHECK_TRUE(placeholder != std::string{Palette::AURORA_DEFAULT_PLACEHOLDER_TEXT});
    AURORA_TEST_CHECK_TRUE(empty_message != std::string{Palette::AURORA_DEFAULT_EMPTY_MESSAGE_TEXT});
    // 面板那两枚 getter 此刻按 `Locale{}`（＝英文档）解析：装配处注入 `LocaleProvider` 之后绘制侧
    // 才跟着用户档走，那一路由下面 `#ifdef AURORA_BACKEND_HEADLESS` 的像素例证；这里只挡空串。
    AURORA_TEST_CHECK_FALSE(palette->placeholder().empty());
    AURORA_TEST_CHECK_FALSE(palette->empty_message().empty());
}

AURORA_TEST_CASE(the_registered_entry_names_are_the_very_keys_the_app_asks_for) {
    // key 由本仓命名（框架那两个 builtin 名是保留名，词表占用不了），装配处必须显式换 Id；
    // 这一例钉的是「换的是哪两条名字」而不是译文：改名而装配处没跟着换，面板就又读回英文兜底串。
    static_cast<void>(borealis::ui::install_settings_strings());
    const std::shared_ptr<Palette> unwired = std::make_shared<Palette>(nullptr);
    AURORA_TEST_CHECK_EQ(unwired->placeholder_key(), Palette::AURORA_DEFAULT_PLACEHOLDER_KEY);
    const std::shared_ptr<Palette> palette = make_palette();

    AURORA_TEST_CHECK_NE(palette->placeholder_key(), Palette::AURORA_DEFAULT_PLACEHOLDER_KEY);
    AURORA_TEST_CHECK_NE(palette->empty_message_key(), Palette::AURORA_DEFAULT_EMPTY_MESSAGE_KEY);
    AURORA_TEST_CHECK_TRUE(borealis::ui::has_settings_string("command_palette.field_placeholder"));
    AURORA_TEST_CHECK_TRUE(borealis::ui::has_settings_string("command_palette.empty_message"));
}

AURORA_TEST_CASE(the_resolved_placeholder_lands_on_the_search_field_widget) {
    // 面板的 getter 与真正落笔的那只控件是两次赋值；缺了同步就只剩 getter 绿而屏幕上没有。
    static_cast<void>(borealis::ui::install_settings_strings());
    const std::shared_ptr<Palette> palette = make_palette();

    const au::TextInput *field = find_field(*palette);
    AURORA_TEST_REQUIRE_MSG(field != nullptr, "the palette has no text input child");
    AURORA_TEST_CHECK_EQ(field->accessibility_label(), palette->placeholder());
    AURORA_TEST_CHECK_FALSE(field->accessibility_label().empty());
}

#ifdef AURORA_BACKEND_HEADLESS

namespace {

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 600;

using Frame = std::vector<std::uint8_t>;
using Points = std::set<std::pair<std::size_t, std::size_t>>;

/// @brief 一组差分点的外接矩形（含端点）。
struct Box {
    std::size_t x0 = 0;
    std::size_t y0 = 0;
    std::size_t x1 = 0;
    std::size_t y1 = 0;
};

/// @brief 一条墨迹带的纵向跨度（本例只判「上簇归谁、下簇归谁」，横向范围无判据故不记）。
struct Band {
    std::size_t y0 = 0;
    std::size_t y1 = 0;
};

/// @brief 按 y 把差分点折成「连续若干行算一簇」的名单（簇之间至少隔一整行没动）。
///
/// 不能拿 `Points` 的迭代序当行序：那是按 (x, y) 的字典序，x 一变 y 就折回去。
[[nodiscard]] auto bands_of(const Points &pts) -> std::vector<Band> {
    std::set<std::size_t> rows;
    for (const auto &point : pts) {
        rows.insert(point.second);
    }
    std::vector<Band> out;
    for (const std::size_t y : rows) {
        if (out.empty() || y > out.back().y1 + 1U) {
            out.push_back(Band{y, y});
        } else {
            out.back().y1 = y;
        }
    }
    return out;
}

/// @brief 带真实布局与绘制的驱动台：面板作为浮层挂在 `OverlayHost` 上（装配处同款形态）。
class Harness {
  public:
    Harness() {
        host_ = std::make_shared<au::OverlayHost>();
        base_ = std::make_shared<au::Text>(
            aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
        // 浮层序号从「基础内容之后」起算且 `overlay_count()` 不含基础内容，故宿主须带一个基础子节点。
        static_cast<void>(host_->add_overlay(au::Node{base_}));
        // 语言档注入腿（`settings_i18n.h` 文件头④）：绘制侧按环境里的 `Locale` 查目录，不注入就按
        // `Locale{}`（＝英文档）解析——与装配处 `make_window_scene` 同款，本例要的正是用户那一档。
        auto locale_scope = std::make_shared<au::LocaleProvider>(
            borealis::ui::settings_locale(), au::Node{std::static_pointer_cast<au::Widget>(host_)});
        root_ = au::Node{std::static_pointer_cast<au::Widget>(std::move(locale_scope))};
        focus_.set_root(&root_.widget());
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    auto attach(std::shared_ptr<Palette> palette) -> void {
        palette_ = std::move(palette);
        static_cast<void>(host_->add_overlay(std::static_pointer_cast<au::Widget>(palette_)));
    }

    auto render() -> void { static_cast<void>(window_.present_root(root_)); }


    /// @brief 最近一次 `render()` 是否被框架整帧跳过（idle 帧）。
    [[nodiscard]] auto idle() const -> bool { return window_.is_idle_frame(); }

    [[nodiscard]] auto palette() -> Palette & { return *palette_; }

    /// @brief 当前帧缓冲的 RGBA8 副本（含 alpha：无头帧底是全透明黑，只比 RGB 会把「画了个纯黑」
    ///        与「什么都没画」混为一谈，同 `itest_tab_bar` / `itest_render_viewport` 的口径）。
    [[nodiscard]] auto pixels() const -> Frame {
        Frame out(buffer_width() * buffer_height() * 4U, 0U);
        const std::uint8_t *data = window_.surface().data();
        if (data != nullptr) {
            std::memcpy(out.data(), data, out.size());
        }
        return out;
    }

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

  private:
    [[nodiscard]] static auto buffer_width() -> std::size_t { return static_cast<std::size_t>(kWindowWidth); }
    [[nodiscard]] static auto buffer_height() -> std::size_t { return static_cast<std::size_t>(kWindowHeight); }

    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        static_cast<void>(surface->begin_frame(kWindowWidth, kWindowHeight));
        return au::Window{std::move(surface)};
    }

    au::Window window_ = make_window();  ///< 最先声明、最后析构：帧缓冲须活到取样结束。
    std::shared_ptr<au::OverlayHost> host_;
    std::shared_ptr<au::Text> base_;
    std::shared_ptr<Palette> palette_;
    au::Node root_;
    au::FocusManager focus_;
};

}  // namespace

AURORA_TEST_CASE(each_entry_change_moves_its_own_band_of_ink_on_the_screen) {
    static_cast<void>(borealis::ui::install_settings_strings());
    // 中文副本先取：后面的覆盖会改掉词表里那一档的取值，复原时要按取下的这一份写回去。
    const PaletteStrings zh = current_palette_strings();
    Harness h;
    h.attach(make_palette());

    // 未打开帧应含基础内容的墨迹（帧缓冲若全零，后面一切差分都恒空）。
    h.render();
    const Frame closed = h.pixels();
    AURORA_TEST_CHECK_MSG(std::any_of(closed.begin(), closed.end(), [](std::uint8_t b) { return b != 0U; }),
                          "the closed frame has no ink at all");

    // open 后这一帧框架当作要重绘（idle 读数为假），画面上多出遮罩与卡片。
    h.palette().open();
    h.render();
    const Frame chinese = h.pixels();
    AURORA_TEST_CHECK_FALSE(h.idle());
    AURORA_TEST_CHECK_FALSE(Harness::changed(closed, chinese).empty());
    AURORA_TEST_REQUIRE_MSG(h.palette().is_open(), "the palette did not stay open after the first frame");

    // 只改占位符那一簇：经受支持的 `set_placeholder_key` 触发框架重解析（构造期与 `open()` 都不查表，
    // 故必须走 setter 而非 `close()+open()`）。改的是词表里那一条取值，故仍验「查表发生」，而不会把覆盖
    // 字面量冻进控件（`set_placeholder` 才走那条，本例不取）。
    overwrite_entry("command_palette.field_placeholder", Palette::AURORA_DEFAULT_PLACEHOLDER_TEXT);
    h.palette().set_placeholder_key(borealis::msg::id::AURORA_COMMAND_PALETTE_FIELD_PLACEHOLDER);
    h.render();
    AURORA_TEST_CHECK_FALSE(h.idle());
    AURORA_TEST_CHECK_EQ(h.palette().placeholder(), std::string{Palette::AURORA_DEFAULT_PLACEHOLDER_TEXT});
    Points placeholder_only = Harness::changed(chinese, h.pixels());
    AURORA_TEST_REQUIRE_FALSE(placeholder_only.empty());

    // 再只改空态提示那一簇：此时两簇都动，且它们是隔开的两处（一处墨迹说明其中一条根本没上屏）。
    overwrite_entry("command_palette.empty_message", Palette::AURORA_DEFAULT_EMPTY_MESSAGE_TEXT);
    h.palette().set_empty_message_key(borealis::msg::id::AURORA_COMMAND_PALETTE_EMPTY_MESSAGE);
    h.render();
    const Points both = Harness::changed(chinese, h.pixels());
    const std::vector<Band> bands = bands_of(both);
    AURORA_TEST_REQUIRE_MSG(bands.size() >= 2U, "the two strings do not reach two separate bands");
    AURORA_TEST_CHECK_TRUE(Harness::box_of(placeholder_only).y1 < bands[1].y0);

    // 回退占位符到中文：剩下的差分只落在上簇（空态那簇不动）。
    overwrite_entry("command_palette.field_placeholder", zh.placeholder);
    h.palette().set_placeholder_key(borealis::msg::id::AURORA_COMMAND_PALETTE_FIELD_PLACEHOLDER);
    h.render();
    const Points empty_only = Harness::changed(chinese, h.pixels());
    AURORA_TEST_REQUIRE_FALSE(empty_only.empty());
    AURORA_TEST_CHECK_TRUE(Harness::box_of(empty_only).y0 > bands[0].y1);

    // 复原进程级词表（覆盖只改运行期那一条，构建期产物不动）并把面板重新解析回中文帧，
    // 避免污染后续用例。
    restore(zh);
    h.palette().set_placeholder_key(borealis::msg::id::AURORA_COMMAND_PALETTE_FIELD_PLACEHOLDER);
    h.palette().set_empty_message_key(borealis::msg::id::AURORA_COMMAND_PALETTE_EMPTY_MESSAGE);
    h.render();
    AURORA_TEST_CHECK_MSG(h.pixels() == chinese, "the restored table did not give the original frame back");
}

#else  // 无头后端未编译：HeadlessSurface 不在公共面上，本例跳过而不是空转。

AURORA_TEST_CASE(each_entry_change_moves_its_own_band_of_ink_on_the_screen) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_command_palette
