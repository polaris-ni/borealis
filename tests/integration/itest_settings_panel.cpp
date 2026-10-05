/// 测试类型: integration
/// 目标单元: src/ui/settings_panel.cpp（设置面板的界面腿骨架 + 落盘 / 广播接线）
/// 测试说明: 断言的是**接线**而非四件纯逻辑前置（反向核对表、HEX 解析、表单状态机、搬运件，各有
///           `utest_settings_catalog` / `utest_color_text` / `utest_settings_form` / `utest_form_transfer`
///           逐条覆盖）。`SPEC.FEAT.PREF.02` 的面板本体在本棒只交付「四页骨架 + 六类通用行控件 +
///           落盘与广播」，因此值得单独证的正是那三条接缝能不能对上一张表（裁决 7.52 / 7.56）：
///           ① **面板画的键 ＝ 反向核对表 ∩ 装载成功的键**，且次序照表：装载缺一键、给一个表外的键、
///              或给一个形态族不符的值，面板都**少画一个控件**而不是画一个存不回去的控件；
///           ② **两个正交列折成界面上的两枚角标**：`Absent` 灰置且挂「延后」，`NextSession` 挂
///              「下次会话生效」，`terminal.encoding` 那行两枚并列（判据文 B3-a）；
///           ③ **提交 → 落盘 → 广播的三条腿各在其位**：只有「已接线 ∧ 即时」广播；落盘失败时脏标记
///              留着且不广播（面板不谎报已存）；改了又改回来既不落盘也不广播；
///           ④ **S14 的提交时机**：文本框逐字符改（`set_value`）一个字节也不提交，失焦那一刻才交；
///              半截 HEX 被表单拒后值一字未动、文件也没写；
///           ⑤ **`Escape` 随开随绑、随关随解**，且作用域是 `Global`（挂 `Focus` 会在焦点落在遮罩层时
///              失灵，而面板关闭后留着它就更糟——会话的 `Esc` 被静默吞掉）；
///           ⑥ **浮层真的铺满整窗且抓得住**（裁决 7.56⑤ 的框架实测）：无头窗口走真实布局与真实指针
///              派发，卡片外那一点命中的是遮罩层本身，卡片内那一点命中的是卡片里的控件，点遮罩即关
///              面板。`Dialog` / `Scroll` 那两类不写 bounds 的容器（缺口 G26 / G27）在本用例里会直接
///              表现为「命中不到、点了不关」。
///           ⑦ **下拉的选项走真实派发**（缺口 G29 的接货复验，裁决 7.59）：选项面板是「覆盖绘制不占布局」
///              的区域，回货前它进不了真实派发的命中链（祖先按 `child.bounds()` 判包含），本套件当时以此
///              留一条现状钉子。框架补上 `Widget::extra_hit_box()` 与祖先下降闸的合并判定后，钉子翻成正向
///              判据：盒外那一段选项带按链能命中该 `Dropdown`，其上的真实单击选中另一档、收起面板、只落盘
///              不广播。**回货只闭合一层**——追加盒只并「直接子」的申报，孙辈的申报不随祖先上传，故面板伸出
///              所在行之外的那一段仍不可达，本套件另留一条钉子（新缺口 G30，含「控件申报覆盖、控件兼容入口也认、
///              唯独祖先闸不认」的形态证明）。
///           ⑧ **按钮标签由框架查表**（缺口 G28 的接货复验，裁决 7.59）：本件那三枚按钮原样交回
///              `LocalizedString` 之后，显示串仍是词条表给的那一条；查表没发生就回退到实例自己的 `text`，
///              而 `tr()` 造出的实例那份 text 恒空。
///           ⑨ **行区是滚动容器而不是固定行高的列表**（裁决 7.60）：`LazyList` 的 `item_extent` 是**全局**
///              一档，表达不出 #114 那种「主题卡 / 16 格色板 / 下拉」高低不等的行，故行区取 `Scroll` +
///              `Column` 全量实例化。换容器就把坐标模型换掉了——`Scroll` 的内容子节点 bounds 是**内容坐标**
///              （命中时经 `offset_y_` 换算），于是本套件的每一处探针都改成按真实派发结果量窗口坐标
///              （`Harness::reachable_box`），并新增一条「滚过一段之后真点一行开关即提交」的证人：它是
///              G27 回货（滚动容器的内容进得了命中链）在面板生产路径上的消费腿。
///
///           一条测试现场的必要构造：`OverlayHost` 的浮层序号是从「基础内容之后」起算的
///           （`add_overlay` 返回 `children_.size() - 1`，回货后宿主无基础内容时返回 `std::nullopt`），
///           故宿主**必须**带一个基础子节点——生产路径上那是终端视口，本用例给一个 `Text`。

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aurora/aurora.h"
#include "aurora/app/shortcuts.h"
#include "aurora/event/dispatcher.h"
#include "aurora/event/focus.h"
#include "aurora/i18n/string_table.h"
#include "aurora/widget/button.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "borealis/config/form_transfer.h"
#include "borealis/config/settings.h"
#include "borealis/ui/color_text.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/right_click.h"
#include "borealis/ui/settings_catalog.h"
#include "borealis/ui/settings_form.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/settings_panel.h"

namespace borealis::test_cases::itest_settings_panel {

namespace {

using borealis::config::Settings;
using borealis::ui::ApplyScope;
using borealis::ui::CommitIssue;
using borealis::ui::ConsumerStatus;
using borealis::ui::ControlKind;
using borealis::ui::EffectLevel;
using borealis::ui::FormEntry;
using borealis::ui::FormValue;
using borealis::ui::RgbaColor;
using borealis::ui::SettingsControl;
using borealis::ui::SettingsPage;
using borealis::ui::SettingsPanel;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 600;
/// 卡片与窗口边的最小留白（`settings_panel.cpp` 的 `kCardMarginDp`）：窗口 900×600 时卡片是
/// `clamp(900-32, 480, 1040) × clamp(600-32, 320, 680)` ＝ 868×568，居中后四周各留 16 dp。
/// 遮罩命中用例就取这个环带里的一点。
constexpr float kCardEdgeDp = 16.0F;

/// CJK-LITERAL: locale-output - 断言的是面板上那两枚角标的**最终文案**，换成英文就等价于
/// 「面板用了别的词条 key」，而那正是本判据要抓的错。词条 key 与表列的对应另由 `i18n` 那条用例守。
constexpr std::string_view kBadgeDeferred = "延后";
constexpr std::string_view kBadgeNextSession = "下次会话生效";

/// @brief 反向核对表里某一页的键序列（表内次序即面板的排版次序）。
[[nodiscard]] auto catalog_keys(SettingsPage page) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const SettingsControl &control : borealis::ui::settings_catalog()) {
        if (control.page == page) {
            out.push_back(control.key);
        }
    }
    return out;
}

/// @brief 在当前页的行表里按路径取一行。
/// @param rows 面板给出的行表。
/// @param key 落盘点号路径。
/// @return 命中为该行；面板没画这个键时为空。
[[nodiscard]] auto find_row(const std::vector<SettingsPanel::VisibleRow> &rows, std::string_view key)
    -> const SettingsPanel::VisibleRow * {
    for (const SettingsPanel::VisibleRow &row : rows) {
        if (row.key == key) {
            return &row;
        }
    }
    return nullptr;
}

/// @brief 面板当前页画出的键序列（行表 → 键表）。
[[nodiscard]] auto drawn_keys(const SettingsPanel &panel) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const SettingsPanel::VisibleRow &row : panel.visible_rows()) {
        out.push_back(row.key);
    }
    return out;
}

/// @brief 把键序列里的某一条摘掉（既用于造「装载缺一键」，也用于算预期）。
auto drop_key(std::vector<std::string> &keys, std::string_view doomed) -> void {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == doomed) {
            keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

/// @brief 逐行比较两个键序列（不匹配时把两侧都打出来，否则一条尺寸判据看不出是哪一格错位）。
auto check_key_sequence(std::string_view what, const std::vector<std::string> &got,
                        const std::vector<std::string> &want) -> void {
    AURORA_TEST_REQUIRE_MSG(got.size() == want.size(),
                            std::string{what} + ": drew " + std::to_string(got.size()) + " rows, want " +
                                std::to_string(want.size()));
    for (std::size_t i = 0; i < got.size() && i < want.size(); ++i) {
        AURORA_TEST_CHECK_MSG(got[i] == want[i],
                              std::string{what} + " index " + std::to_string(i) + ": got " + got[i] + ", want " +
                                  want[i]);
    }
}

/// @brief 存储侧与绘制侧的三条接缝替身：只数「被调了几次」并留下最后一次搬到的配置。
///
/// 落盘走真实的 `config::apply_form()`，于是一条判据同时过「表单 → 成员」的搬运腿；基线随成功落盘
/// 推进，和真实 `Store` 一样——重开面板该读到的是最新的那份，而不是面板自己留着的那份。
class StoreProbe {
public:
    std::size_t load_calls = 0;
    std::size_t persist_calls = 0;
    std::size_t broadcast_calls = 0;
    bool fail_persist = false;
    Settings base{};                    ///< 存储侧的当前配置（成功落盘后推进）。
    std::vector<Settings> persisted{};  ///< 每次落盘真正写出去的那份，按序。
    std::vector<Settings> broadcast{};  ///< 每次广播搬出去的那份，按序。
    /// @brief 装载脚本；非空即取代默认的 `form_entries(base)`。
    ///
    /// 「面板与表分叉」的三种现场都只能由装载侧造（少一键 / 表外的键 / 形态族不符），
    /// 而三条接缝都按整份表单说话，故这里换的是**装载**这一条腿而不是逐键接缝。
    std::function<std::vector<FormEntry>()> load_script;

    /// @brief 交出一副挂到本替身上的 `Hooks`。
    ///
    /// 三个闭包都按 `this` 取值而不是按建立时的快照，故 `hooks()` 交出之后仍可改 `load_script` 与
    /// `fail_persist`——面板每次 `open()` 才走装载腿，分叉现场因此能在同一个面板实例上逐次注入。
    [[nodiscard]] auto hooks() -> SettingsPanel::Hooks {
        return SettingsPanel::Hooks{
            .load =
                [this]() -> std::vector<FormEntry> {
                    ++load_calls;
                    if (load_script) {
                        return load_script();
                    }
                    return borealis::config::form_entries(base);
                },
            .persist =
                [this](const borealis::ui::SettingsForm &form) -> std::optional<std::string> {
                    ++persist_calls;
                    if (fail_persist) {
                        return std::string{"backup_failed"};  // ASCII 诊断（AGENTS.md §4.3 第 14 条）
                    }
                    Settings next = borealis::config::apply_form(form, base);
                    persisted.push_back(next);
                    base = next;
                    return std::nullopt;
                },
            .broadcast =
                [this](const borealis::ui::SettingsForm &form) -> void {
                    ++broadcast_calls;
                    broadcast.push_back(borealis::config::apply_form(form, base));
                },
        };
    }

    /// @brief 清掉三个计数（判据只测「这一步之后新增了什么」）。
    auto reset_counters() -> void {
        load_calls = 0;
        persist_calls = 0;
        broadcast_calls = 0;
    }
};

/// @brief 按存储侧当前配置造装载名单（用例在此之上做「少一键 / 多一键 / 错一型」的三种分叉）。
[[nodiscard]] auto entries_of(const Settings &base) -> std::vector<FormEntry> {
    return borealis::config::form_entries(base);
}

/// @brief 在装载名单里按路径取值；没有该键即回 nullptr（用例只改写已有的那条）。
[[nodiscard]] auto entry_for(std::vector<FormEntry> &entries, std::string_view key) -> FormEntry * {
    for (FormEntry &entry : entries) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

/// @brief 造一个「只有基础内容」的浮层宿主（见文件头那条序号哨兵）。
/// @param out_base 基础内容控件，须活到宿主之后。
/// @param out_root 场景根节点（宿主自身），交 `present_root` 与派发入口用。
[[nodiscard]] auto make_host(std::shared_ptr<au::Widget> &out_base, au::Node &out_root)
    -> std::shared_ptr<au::OverlayHost> {
    auto host = std::make_shared<au::OverlayHost>();
    out_base = std::make_shared<au::Text>(
        aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
    out_root = au::Node{out_base};
    (void)host->add_overlay(au::Node{out_base});  // 占住子节点 [0]：基础内容
    return host;
}

}  // namespace

/// @brief 判一行的角标（用例侧独立折一次，取的是表的两列而非面板的算式）。
[[nodiscard]] auto expected_badge(const SettingsControl &control) -> std::string {
    std::string out;
    if (control.consumer != ConsumerStatus::Wired) {
        out += kBadgeDeferred;
    }
    if (control.effect == EffectLevel::NextSession) {
        if (!out.empty()) {
            out += " · ";
        }
        out += kBadgeNextSession;
    }
    return out;
}

AURORA_TEST_CASE(every_catalog_key_resolves_to_a_display_label) {
    borealis::ui::install_settings_strings();

    for (const SettingsControl &control : borealis::ui::settings_catalog()) {
        AURORA_TEST_REQUIRE_MSG(borealis::ui::has_settings_string(control.key), control.key + " has no locale entry");
        AURORA_TEST_CHECK_MSG(!borealis::ui::settings_label(control.key).empty(), control.key + " label is empty");
    }

    for (std::string_view key : {"settings.title", "settings.subtitle", "settings.close", "settings.action.open",
                                 "settings.action.unset", "settings.page.appearance", "settings.page.terminal",
                                 "settings.page.connection", "settings.page.shortcuts", "settings.badge.deferred",
                                 "settings.badge.next_session"}) {
        AURORA_TEST_REQUIRE_MSG(borealis::ui::has_settings_string(key), std::string{key} + " missing");
    }

    // 提交未通过的十三个原因全都要有词条：面板把失败写进状态列时只有这一条查表路径，缺一个就是空标签。
    for (CommitIssue issue : {CommitIssue::UnknownKey, CommitIssue::NotLoaded, CommitIssue::ReadOnly,
                              CommitIssue::DomainMismatch, CommitIssue::TextNotAccepted, CommitIssue::MalformedNumber,
                              CommitIssue::NotIntegral, CommitIssue::OutOfRange, CommitIssue::NotAChoice,
                              CommitIssue::MalformedColor, CommitIssue::UnsetNotAllowed, CommitIssue::SlotOutOfRange,
                              CommitIssue::TableSizeWrong}) {
        const au::LocalizedString text = borealis::ui::settings_issue_text(issue);
        AURORA_TEST_CHECK_MSG(
            !text.resolve(&au::default_string_table(), borealis::ui::settings_locale()).empty(),
            "issue " + std::to_string(static_cast<int>(issue)) + " has no locale entry");
    }
    AURORA_TEST_CHECK_MSG(borealis::ui::settings_issue_text(CommitIssue::None)
                              .resolve(&au::default_string_table(), borealis::ui::settings_locale())
                              .empty(),
                          "CommitIssue::None must yield no text");
}

AURORA_TEST_CASE(the_page_row_table_is_the_catalog_intersection_in_order) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};

    panel.open();
    AURORA_TEST_REQUIRE_EQ(probe.load_calls, 1U);
    AURORA_TEST_CHECK_TRUE(panel.is_open());
    AURORA_TEST_CHECK_TRUE(panel.form().load_report().clean());

    std::size_t total = 0;
    for (SettingsPage page : {SettingsPage::Appearance, SettingsPage::Terminal, SettingsPage::Connection,
                              SettingsPage::Shortcuts}) {
        panel.select_page(page);
        check_key_sequence("page rows", drawn_keys(panel), catalog_keys(page));
        total += drawn_keys(panel).size();
    }
    AURORA_TEST_CHECK_EQ(total, borealis::ui::settings_catalog().size());
}

AURORA_TEST_CASE(unloaded_and_mistyped_keys_are_dropped_not_drawn) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};

    // 三种「面板与表分叉」的现场各来一次：少一行的装载、一条表外的路径、一个形态族不符的值。
    // 三者都只换装载腿，故同一个面板开合三轮即可——判据是「少画一个控件」而不是「面板崩了」。

    // ① 装载缺一键：那一格不画，其余逐字照表。
    probe.load_script = [&probe]() -> std::vector<FormEntry> {
        std::vector<FormEntry> entries = entries_of(probe.base);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].key == "appearance.font_family") {
                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        return entries;
    };
    panel.open();
    std::vector<std::string> want = catalog_keys(SettingsPage::Appearance);
    drop_key(want, "appearance.font_family");
    check_key_sequence("appearance rows with one unloaded key", drawn_keys(panel), want);
    AURORA_TEST_REQUIRE_EQ(panel.form().load_report().missing_keys.size(), 1U);
    AURORA_TEST_CHECK_EQ(panel.form().load_report().missing_keys.front(), std::string{"appearance.font_family"});
    panel.close();

    // ② 表外的键：不画，行表与全表逐字一致（面板画了 schema 外的控件就是判据文判据①要抓的错）。
    probe.load_script = [&probe]() -> std::vector<FormEntry> {
        std::vector<FormEntry> entries = entries_of(probe.base);
        entries.push_back(FormEntry{"appearance.not_a_real_key", FormValue::text("x")});
        return entries;
    };
    panel.open();
    check_key_sequence("appearance rows with one unknown key", drawn_keys(panel),
                       catalog_keys(SettingsPage::Appearance));
    AURORA_TEST_REQUIRE_EQ(panel.form().load_report().unknown_keys.size(), 1U);
    AURORA_TEST_CHECK_EQ(panel.form().load_report().unknown_keys.front(), std::string{"appearance.not_a_real_key"});
    panel.close();

    // ③ 形态族不符：实数档的字号收到整数 → 该行不画，且留下的是可指认的记录而非静默丢掉。
    probe.load_script = [&probe]() -> std::vector<FormEntry> {
        std::vector<FormEntry> entries = entries_of(probe.base);
        if (FormEntry *row = entry_for(entries, "appearance.font_size_pt"); row != nullptr) {
            row->value = FormValue::integral(16);
        }
        return entries;
    };
    panel.open();
    want = catalog_keys(SettingsPage::Appearance);
    drop_key(want, "appearance.font_size_pt");
    check_key_sequence("appearance rows with one mistyped key", drawn_keys(panel), want);
    AURORA_TEST_REQUIRE_EQ(panel.form().load_report().mistyped_keys.size(), 1U);
    AURORA_TEST_CHECK_EQ(panel.form().load_report().mistyped_keys.front(), std::string{"appearance.font_size_pt"});
    AURORA_TEST_CHECK_TRUE(panel.form().value("appearance.font_size_pt") == nullptr);
}

AURORA_TEST_CASE(deferred_and_next_session_rows_carry_their_badges) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};

    panel.open();
    for (SettingsPage page : {SettingsPage::Appearance, SettingsPage::Terminal, SettingsPage::Connection,
                              SettingsPage::Shortcuts}) {
        panel.select_page(page);
        for (const SettingsPanel::VisibleRow &row : panel.visible_rows()) {
            AURORA_TEST_TRACE(row.key);
            const SettingsControl *control = borealis::ui::find_settings_control(row.key);
            AURORA_TEST_REQUIRE(control != nullptr);
            AURORA_TEST_CHECK_EQ(row.badge, expected_badge(*control));
            AURORA_TEST_CHECK_EQ(row.editable, control->consumer != ConsumerStatus::Absent &&
                                                   control->kind != ControlKind::ThemePicker &&
                                                   control->kind != ControlKind::SwatchGrid &&
                                                   control->kind != ControlKind::FontDropdown &&
                                                   control->kind != ControlKind::FamilyList &&
                                                   control->kind != ControlKind::ReadOnlyTable);
        }
    }

    panel.select_page(SettingsPage::Appearance);
    const auto rows = panel.visible_rows();
    AURORA_TEST_CHECK_EQ(find_row(rows, "appearance.sidebar_collapsed")->badge, std::string{kBadgeDeferred});
    AURORA_TEST_CHECK_FALSE(find_row(rows, "appearance.sidebar_collapsed")->editable);
    AURORA_TEST_CHECK_TRUE(find_row(rows, "appearance.cursor_shape")->editable);  // 接缝未开 ≠ 灰置
    AURORA_TEST_CHECK_EQ(find_row(rows, "appearance.cursor_shape")->badge,
                         std::string{kBadgeDeferred} + " · " + std::string{kBadgeNextSession});
    AURORA_TEST_CHECK_EQ(find_row(rows, "appearance.font_size_pt")->badge, std::string{});

    panel.select_page(SettingsPage::Terminal);
    const auto terminal_rows = panel.visible_rows();
    // 两枚角标并列的唯一现场：全仓无消费方 ∧ 下次会话生效。
    AURORA_TEST_CHECK_EQ(find_row(terminal_rows, "terminal.encoding")->badge,
                         std::string{kBadgeDeferred} + " · " + std::string{kBadgeNextSession});
    AURORA_TEST_CHECK_EQ(find_row(terminal_rows, "terminal.scrollback_limit")->badge, std::string{kBadgeNextSession});
    AURORA_TEST_CHECK_EQ(find_row(terminal_rows, "terminal.right_click")->badge, std::string{});
}

AURORA_TEST_CASE(dedicated_control_rows_show_a_readonly_summary) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    panel.select_page(SettingsPage::Appearance);
    const auto rows = panel.visible_rows();
    // 16 格色板：本棒不给可点的格子，但当前值必须如实显示（否则用户看不到自己现在是什么配色）。
    const SettingsPanel::VisibleRow *grid = find_row(rows, "appearance.palette.basic");
    AURORA_TEST_REQUIRE(grid != nullptr);
    AURORA_TEST_CHECK_FALSE(grid->editable);
    AURORA_TEST_CHECK_EQ(grid->summary.size(), 16U * 7U + 15U);
    AURORA_TEST_CHECK_EQ(grid->summary.substr(0, 7), borealis::ui::color_to_hex(probe.base.appearance.palette.basic[0]));
    AURORA_TEST_CHECK_EQ(find_row(rows, "appearance.theme")->summary, probe.base.appearance.theme);
    AURORA_TEST_CHECK_EQ(find_row(rows, "appearance.font_family")->summary, probe.base.appearance.font_family);
    // 空回退链的摘要也是空串：它是「不注入按族链」而不是「链上有一个空族名」。
    AURORA_TEST_CHECK_TRUE(find_row(rows, "appearance.font_fallback_chain")->summary.empty());
    // 可交互行的摘要一律留空——值在它自己的控件里，两处同时显示就是第二个真值源。
    // 只判这一个方向：占位行的摘要可以是空串（缺省值本就是空文本的 FreeText 与空回退链都是）。
    for (const SettingsPanel::VisibleRow &row : rows) {
        if (!row.editable) {
            continue;
        }
        AURORA_TEST_TRACE(row.key);
        AURORA_TEST_CHECK_TRUE(row.summary.empty());
    }

    panel.select_page(SettingsPage::Shortcuts);
    const auto shortcut_rows = panel.visible_rows();
    AURORA_TEST_REQUIRE_EQ(shortcut_rows.size(), 1U);
    AURORA_TEST_CHECK_FALSE(shortcut_rows[0].editable);
    AURORA_TEST_CHECK_EQ(shortcut_rows[0].summary, std::string{"0"});  // 覆盖表条数（缺省空表）
}

AURORA_TEST_CASE(a_wired_immediate_change_persists_once_and_broadcasts_once) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    AURORA_TEST_REQUIRE_EQ(panel.commit("terminal.right_click", FormValue::text("paste")), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_REQUIRE_EQ(probe.broadcast.size(), 1U);
    AURORA_TEST_CHECK_TRUE(probe.persisted[0].terminal.right_click == borealis::ui::RightClickAction::Paste);
    AURORA_TEST_CHECK_TRUE(probe.broadcast[0].terminal.right_click == borealis::ui::RightClickAction::Paste);
    AURORA_TEST_CHECK_FALSE(panel.form().has_unsaved_changes());
    AURORA_TEST_CHECK_TRUE(panel.form().apply_scope("terminal.right_click") == ApplyScope::PersistAndApplyNow);
}

AURORA_TEST_CASE(a_persist_only_change_writes_but_does_not_broadcast) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    // 已接线 ∧ 下次会话：写盘但不广播（`scrollback` 是建会话那一刻取用的）。
    AURORA_TEST_REQUIRE_EQ(panel.commit("terminal.scrollback_limit", FormValue::integral(5000)), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_CHECK_EQ(probe.persisted[0].terminal.scrollback_limit, std::size_t{5000});
    AURORA_TEST_CHECK_TRUE(panel.form().apply_scope("terminal.scrollback_limit") == ApplyScope::PersistOnly);

    // 全仓无消费方 ∧ 即时：同样只落盘——没有消费方就没有可广播的对象。
    probe.reset_counters();
    AURORA_TEST_REQUIRE_EQ(panel.commit("appearance.sidebar_collapsed", FormValue::boolean(false)),
                           CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_CHECK_FALSE(probe.persisted.back().appearance.sidebar_collapsed);
}

AURORA_TEST_CASE(a_failed_persist_keeps_the_dirty_state_and_broadcasts_nothing) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    probe.fail_persist = true;
    AURORA_TEST_REQUIRE_EQ(panel.commit("appearance.font_size_pt", FormValue::real(18.0)), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    // 唯一现场没被写坏，脏标记因此必须留着：面板不能谎报已存。
    AURORA_TEST_CHECK_TRUE(panel.form().is_dirty("appearance.font_size_pt"));
    AURORA_TEST_CHECK_TRUE(panel.form().has_unsaved_changes());
    AURORA_TEST_CHECK_EQ(probe.base.appearance.font_size_pt, 14.0);
    AURORA_TEST_CHECK_TRUE(probe.persisted.empty());

    // 恢复落盘后的一次提交把整份表单（含上一条未存的改动）一次写出去，两个脏键一起清零；
    // 而广播看的是**本次提交那一行**的档位：行高是「已接线 ∧ 即时」，故一并广播（广播的是整份表单，
    // 其中也带着那条先前未生效的字号改动）。
    probe.fail_persist = false;
    probe.reset_counters();
    AURORA_TEST_REQUIRE_EQ(panel.commit("appearance.font_line_height", FormValue::real(1.2)), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);
    AURORA_TEST_CHECK_FALSE(panel.form().has_unsaved_changes());
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_CHECK_EQ(probe.persisted[0].appearance.font_size_pt, 18.0);
    AURORA_TEST_CHECK_EQ(probe.persisted[0].appearance.font_line_height, 1.2);
    AURORA_TEST_REQUIRE_EQ(probe.broadcast.size(), 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast[0].appearance.font_size_pt, 18.0);
}

AURORA_TEST_CASE(a_reverted_change_neither_persists_nor_broadcasts) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    // ① 把当前值原样再交一次：校验通过但不脏，故既不写盘也不广播——面板不在「没有任何变化」时惊动存储。
    AURORA_TEST_REQUIRE_EQ(panel.commit("terminal.right_click", FormValue::text("context_menu")), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_CHECK_FALSE(panel.form().has_unsaved_changes());

    // ② 改了又改回来，而中间那次落盘失败：基线没动，于是这一对提交合起来没有要写的东西。
    //    （落盘成功后基线随即推进，「改回来」就成了相对基线的一次真改动，见上一条用例。）
    probe.fail_persist = true;
    AURORA_TEST_REQUIRE_EQ(panel.commit("terminal.right_click", FormValue::text("paste")), CommitIssue::None);
    AURORA_TEST_CHECK_TRUE(panel.form().is_dirty("terminal.right_click"));
    probe.fail_persist = false;
    probe.reset_counters();
    AURORA_TEST_REQUIRE_EQ(panel.commit("terminal.right_click", FormValue::text("context_menu")), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_CHECK_FALSE(panel.form().has_unsaved_changes());
    // 唯一现场因此仍是原样：那次失败的写盘没有把 `paste` 留在任何一侧。
    AURORA_TEST_CHECK_TRUE(probe.persisted.empty());
    AURORA_TEST_CHECK_TRUE(probe.base.terminal.right_click == borealis::ui::RightClickAction::ContextMenu);
}

AURORA_TEST_CASE(a_malformed_hex_commit_moves_nothing_and_writes_nothing) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    const RgbaColor before = probe.base.appearance.palette.cursor_color.value_or(RgbaColor{});
    AURORA_TEST_CHECK_EQ(panel.commit_text("appearance.palette.cursor", "#12"), CommitIssue::MalformedColor);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_REQUIRE(panel.form().value("appearance.palette.cursor") != nullptr);
    AURORA_TEST_CHECK_TRUE(panel.form().value("appearance.palette.cursor")->as_color() == before);

    // 八位带 alpha 的形态同样判非法：面板收了它就等于另立一套落盘形态（判据文 A2-d / S14）。
    AURORA_TEST_CHECK_EQ(panel.commit_text("appearance.palette.cursor", "#12ab34ff"), CommitIssue::MalformedColor);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);

    AURORA_TEST_CHECK_EQ(panel.commit_text("appearance.palette.cursor", "#12ab34"), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_REQUIRE(probe.persisted[0].appearance.palette.cursor_color.has_value());
    AURORA_TEST_CHECK_TRUE(*probe.persisted[0].appearance.palette.cursor_color ==
                           borealis::ui::color_from_hex("#12ab34").value());
}

AURORA_TEST_CASE(an_unsettable_color_slot_clears_while_a_required_one_refuses) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    AURORA_TEST_REQUIRE_EQ(panel.commit_unset("appearance.palette.cursor"), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_FALSE(probe.persisted[0].appearance.palette.cursor_color.has_value());
    // 「未配」在值上必须与「配成黑色」不同：空串是装载侧的畸形值（回落并留痕），不是用户点名的未配。
    const FormValue *cleared = panel.form().value("appearance.palette.cursor");
    AURORA_TEST_REQUIRE(cleared != nullptr);
    AURORA_TEST_CHECK_TRUE(cleared->is_unset_color());
    AURORA_TEST_CHECK_FALSE(cleared->as_color().has_value());

    probe.reset_counters();
    AURORA_TEST_CHECK_EQ(panel.commit_unset("appearance.palette.foreground"), CommitIssue::UnsetNotAllowed);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_FALSE(panel.form().is_dirty("appearance.palette.foreground"));
}

AURORA_TEST_CASE(escape_closes_the_panel_and_unbinds_itself) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};

    auto escape = [](bool down) -> au::KeyEvent {
        au::KeyEvent event;
        event.key = static_cast<int>(au::KeyCode::Escape);
        event.action = down ? au::KeyAction::Down : au::KeyAction::Up;
        return event;
    };

    AURORA_TEST_CHECK_EQ(shortcuts.count(), 0U);
    AURORA_TEST_CHECK_FALSE(shortcuts.handle(escape(true), false));

    panel.open();
    AURORA_TEST_REQUIRE_EQ(shortcuts.count(), 1U);
    const au::ShortcutBinding binding = shortcuts.bindings().front();
    AURORA_TEST_CHECK_EQ(binding.combo.to_string(), std::string{"Escape"});
    AURORA_TEST_CHECK_TRUE(binding.scope == au::ShortcutScope::Global);  // 焦点落在遮罩层时也关得掉
    AURORA_TEST_CHECK_EQ(host->overlay_count(), 1U);

    // `Global` 档：没有焦点控件也照样消费（判据「作用域不是 Focus」的形态）。
    AURORA_TEST_CHECK_TRUE(shortcuts.handle(escape(true), false));
    AURORA_TEST_CHECK_FALSE(panel.is_open());
    AURORA_TEST_CHECK_EQ(host->overlay_count(), 0U);
    AURORA_TEST_CHECK_EQ(shortcuts.count(), 0U);
    // 关掉之后 `Escape` 必须原样回到会话：留着它，vim 与 tmux 的 `Esc` 就被静默吞掉了。
    AURORA_TEST_CHECK_FALSE(shortcuts.handle(escape(true), true));

    panel.open();
    AURORA_TEST_CHECK_EQ(shortcuts.count(), 1U);
    AURORA_TEST_CHECK_EQ(host->overlay_count(), 1U);  // 重开不叠第二层浮层
}

AURORA_TEST_CASE(reopening_reloads_the_copy_from_the_store) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};

    panel.open();
    AURORA_TEST_REQUIRE_EQ(panel.commit("appearance.font_size_pt", FormValue::real(16.0)), CommitIssue::None);
    panel.close();
    AURORA_TEST_CHECK_EQ(probe.load_calls, 1U);

    // 另一处（编辑配置文件、或另一屏的建档表单）改了存储：重开读到的是存储，不是面板留着的旧副本。
    probe.base.appearance.font_size_pt = 20.0;
    panel.open();
    AURORA_TEST_CHECK_EQ(probe.load_calls, 2U);
    AURORA_TEST_REQUIRE(panel.form().value("appearance.font_size_pt") != nullptr);
    AURORA_TEST_CHECK_EQ(*panel.form().value("appearance.font_size_pt")->as_real(), 20.0);
    AURORA_TEST_CHECK_FALSE(panel.form().has_unsaved_changes());
}

#ifdef AURORA_BACKEND_HEADLESS

namespace {

/// @brief 命中到一个控件以及命中它的那一点（遮罩用例要拿「真的命中过」的那个点复判，而不是猜一个窗心坐标：
///        行与行之间有间隙，落在间隙上命中的是遮罩层，那种点既证不了卡片可命中也证不了 bounds 写进去了）。
struct HitSpot {
    au::Widget *widget = nullptr;
    float x = 0.0F;
    float y = 0.0F;
    /// 派发链在**窗口坐标**里确实交回该控件的那块矩形（由 `Harness::reachable_box` 量出）。
    /// 记它而不是记控件的 `paint_bounds()`：行区是 `Scroll` 之后，内容子节点的 bounds 是**内容坐标**，
    /// 拿它当窗口坐标用就会差一个「视口顶边 − 滚动偏移」，而那个量正是本件要在运行期变的。
    au::Rect box{.origin = au::Point{.x = -1.0F, .y = -1.0F}, .size = au::Size{.width = 0.0F, .height = 0.0F}};
};

/// @brief 「这块底色仍属深色 chrome」的判据线（三通道都不亮于它，且像素不透明）。
///
/// 线的两侧都有实测出处：框架各控件的浅色缺省里**最低**的一档是开关关闭态轨道 `{180,180,180}`，
/// 本仓 chrome 里**最深**的一档输入底是 `kControlBg{40,42,54}`，故这条钳位既能把每一处浅色缺省读成红，
/// 又不会把本仓自己的深色底读成红。不透明那一半是把「帧缓冲取不到」和「底色够暗」分开的手段。
constexpr std::uint8_t kChromeFloor = 0x60;

[[nodiscard]] auto chrome_is_dark(const RgbaColor &color) -> bool {
    return color.alpha == 0xFF && color.red <= kChromeFloor && color.green <= kChromeFloor
        && color.blue <= kChromeFloor;
}

/// @brief 带真实布局与真实指针派发的驱动台：面板挂在无头窗口的场景根上。
///
/// 只在这里需要窗口——其余用例判的都是行表与三条接缝，它们不依赖布局。本类的存在是因为
/// 「浮层铺满整窗且抓得住」这一条只能在布局之后、由命中测试与派发来说话。
class Harness {
public:
    Harness() {
        host_ = std::make_shared<au::OverlayHost>();
        base_ = std::make_shared<au::Text>(
            aurora::TextProps{.content = std::string{"base"}, .text_color = au::Color{0, 0, 0, 0xFF}});
        (void)host_->add_overlay(au::Node{base_});
        root_ = au::Node{std::static_pointer_cast<au::Widget>(host_)};
        focus_.set_root(&root_.widget());
        render();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 装好面板（挂在宿主上）。
    /// @param probe 存储侧替身。
    /// @return 面板的所有权（用例期间活着即可）。
    [[nodiscard]] auto attach(StoreProbe &probe) -> std::unique_ptr<SettingsPanel> {
        return std::make_unique<SettingsPanel>(*host_, shortcuts_, probe.hooks());
    }

    /// @brief 打开面板并排帧（装载、建浮层、落 bounds 三步都在这一次里发生）。
    auto open(SettingsPanel &panel) -> void {
        panel.open();
        render();
    }

    /// @brief 排一帧：真实布局 + 真实绘制（无头帧缓冲）。
    auto render() -> void {
        (void)window_.present_root(root_);
    }

    /// @brief 命中测试：这一点上的最深控件（根在原点，故窗口逻辑 dp 即根局部坐标）。
    ///
    /// 取的是**命中链**而非兼容入口 `hit_test`：后者语义为「命中即止」，而两类滚动容器（`Scroll` 的
    /// `on_hit_test`、`LazyList` 的同名覆写）为了让滚轮落在整视口上而刻意返回自身。真实指针派发走链，
    /// 判据必须与派发同源，否则行区里的每一个控件都会被我读成「抓不住」。
    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const std::vector<au::HitNode> chain = chain_at(x_dp, y_dp);
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    /// @brief 派发链的完整一段（由浅入深），滚动用例要从中取「行区那个 `Scroll`」这一层。
    [[nodiscard]] auto chain_at(float x_dp, float y_dp) -> std::vector<au::HitNode> {
        return root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
    }

    /// @brief 行区的滚动容器：沿命中链取第一个 `Scroll`（链上没有则空）。
    [[nodiscard]] auto row_scroll(float x_dp, float y_dp) -> au::Scroll * {
        for (const au::HitNode &node : chain_at(x_dp, y_dp)) {
            auto *scroll = dynamic_cast<au::Scroll *>(node.ptr);
            if (scroll != nullptr) {
                return scroll;
            }
        }
        return nullptr;
    }

    /// @brief 量出「派发链在窗口坐标里确实交回这个控件」的矩形（1 dp 步进，含端点）。
    ///
    /// 下拉的两处探针与 chrome 的两处取色都由本函数量的框推，而**不**由控件 `paint_bounds().origin` 推：
    /// 行区改成 `Scroll` + `Column` 之后，内容子节点的 bounds 是**内容坐标**（框架 `scroll.h` 的「几何与命中
    /// 契约」一节自陈这与 `LazyList` / `GridView` 的视口坐标模型不同，后者写 `index * extent - offset`，
    /// 而 `Scroll` 命中时把局部点**加上** `offset_y_` 换算回去），拿它当窗口坐标点去派发或取色，就会差一个
    /// 「视口顶边 − 滚动偏移」——偏移还是本件要在运行期改的量。本函数只在真实派发结果上量，不引入第二个
    /// 坐标假设；它同时也就是「该控件在视口内可见的那一段」，故裁掉的正是肉眼不可见的那一段。
    /// @param widget 期望命中的控件（矩形以「这一行/列上命中它」为准）。
    /// @param x_dp 已知命中它的一点的横坐标（由 `find_first` 给出，从这里向四侧展开）。
    /// @param y_dp 已知命中它的一点的纵坐标。
    /// @return 窗口坐标下的可达矩形；四向各自止步于「不再命中该控件」或窗口边界。
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
        return au::Rect{
            .origin = au::Point{.x = left, .y = top},
            .size = au::Size{.width = right - left + 1.0F, .height = bottom - top + 1.0F}};
    }

    /// @brief 在窗口内扫描，取出第一个指定类型的控件**以及命中它的那一点与其窗口坐标下的可达框**。
    ///
    /// 浮层树是面板私有的，行区又是 `Scroll`（内容整体实例化，不做回收），故这里不另开观测点，而是问框架
    /// 的命中测试「这一点上是谁」。步长 4 dp 足够格住 56 dp 的行高，且落点在控件盒内而不压边界；起点 220 dp
    /// 跳过左导航列（宽 200 dp），免把导航按钮当成行区控件。
    /// @param room_below_dp 该控件的**可达框**下沿到视口下沿之间还要有的余量（dp）：要往下探覆盖绘制区的
    ///        用例（下拉的选项面板）须取一个够格的行，否则探到的点出了视口。**这一参数只是扫描时的挑行
    ///        启发**——实测把它按内容坐标算仍不转红（被挑中的那行两种读数都在视口内），故「探点在视口内」
    ///        那句反空转前提写在各用例里作为断言，而不是靠本参数的算术守住。
    /// @param accept 附加筛选（按控件实例）：chrome 用例要的是「关闭态的开关」，同类型的另一档底色
    ///        是强调色，拿它判「底色不亮」会把正确实现读成红。
    [[nodiscard]] auto find_first(std::string_view type_name, float room_below_dp = 0.0F,
                                  const std::function<bool(au::Widget *)> &accept = {}) -> HitSpot {
        for (float y = kCardEdgeDp + 4.0F; y < static_cast<float>(kWindowHeight) - kCardEdgeDp; y += 4.0F) {
            for (float x = 220.0F; x < static_cast<float>(kWindowWidth) - kCardEdgeDp; x += 4.0F) {
                au::Widget *widget = hit(x, y);
                if (widget == nullptr || type_name != widget->type_name()) {
                    continue;
                }
                if (accept && !accept(widget)) {
                    continue;
                }
                const au::Rect box = reachable_box(widget, x, y);
                if (box.bottom() + room_below_dp > static_cast<float>(kWindowHeight) - kCardEdgeDp) {
                    continue;
                }
                return HitSpot{.widget = widget, .x = x, .y = y, .box = box};
            }
        }
        return HitSpot{};
    }

    /// @brief 取当前帧缓冲里一点的色（无头 `scale` 恒 1.0，故窗口逻辑 dp 即物理像素下标）。
    ///
    /// 缓冲区缺失时返回 `alpha == 0` 的色，而 chrome 判据把「非透明」算在内，故不会把空缓冲读成「底色够暗」。
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

    /// @brief 一个可达框内 (fx, fy) 分数处的像素色，并先钉住「这一点的归属就是那个控件」。
    ///
    /// 取色点一律由 `HitSpot` 里量出来的**窗口坐标**框推，而不是控件的 `paint_bounds()`：行区是 `Scroll`
    /// 之后那份 bounds 是内容坐标，直接当帧缓冲下标就会读到视口之外（卡片底、甚至遮罩）的像素。而
    /// `chrome_is_dark` 是「不亮」这一条松判据——卡片底与导航列本就够暗，取错点的读数照样是绿的，那种绿
    /// 既守不到本件 chrome 也守不到框架缺省。故这里把「取色点归该控件所有」做成取色的**前提**：坐标空间
    /// 一旦用错，本例立刻转红而不是静默放宽。
    [[nodiscard]] auto probe(const HitSpot &spot, double fx, double fy) -> RgbaColor {
        const float x = spot.box.origin.x + static_cast<float>(spot.box.size.width * fx);
        const float y = spot.box.origin.y + static_cast<float>(spot.box.size.height * fy);
        AURORA_TEST_REQUIRE_MSG(hit(x, y) == spot.widget, "the sampled pixel is not claimed by the control under test");
        return pixel(x, y);
    }

    /// @brief 发一个真实指针事件（按下即抬起由调用方各发一次）。
    auto pointer(au::MouseAction action, float x_dp, float y_dp) -> void {
        au::MouseEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.button = au::MouseButton::Left;
        event.action = action;
        (void)dispatcher_.dispatch_mouse(root_.widget(), event, &focus_);
    }

    /// @brief 在一点上单击一次。
    auto click(float x_dp, float y_dp) -> void {
        pointer(au::MouseAction::Press, x_dp, y_dp);
        pointer(au::MouseAction::Release, x_dp, y_dp);
    }

    /// @brief 在一点上发一次真实滚轮事件（`delta_y` 正方向为向上滚，即偏移减小；框架 `ScrollViewport` 的符号约定）。
    auto scroll(float x_dp, float y_dp, float delta_y) -> void {
        au::ScrollEvent event;
        event.position = au::Point{.x = x_dp, .y = y_dp};
        event.delta_y = delta_y;
        (void)au::EventDispatcher::dispatch(root_.widget(), event);
    }

    [[nodiscard]] auto overlay_count() const -> std::size_t { return host_->overlay_count(); }

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
    au::ShortcutRegistry shortcuts_;
    aurora::EventDispatcher dispatcher_;  ///< 本驱动台私有的连击判定与指针捕获状态。
};

}  // namespace

AURORA_TEST_CASE(the_scrim_covers_the_whole_window_and_a_real_click_closes_the_panel) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_REQUIRE_EQ(h.overlay_count(), 1U);
    // 卡片外的那一圈环带命中的是遮罩层本身（`Canvas`），而不是宿主的基础内容 `Text`：
    // 遮罩没铺满整窗时这一格会落到 `Text` 或空，而那一刻面板窗口边缘就是抓不住的死区（G26 / G27 的病灶）。
    au::Widget *scrim = h.hit(4.0F, static_cast<float>(kWindowHeight) * 0.5F);
    AURORA_TEST_REQUIRE(scrim != nullptr);
    AURORA_TEST_CHECK_EQ(std::string_view{scrim->type_name()}, std::string_view{"Canvas"});
    // 卡片之内命中的是卡片自己的控件而不是遮罩：`Stack` 自顶层向下命中，而命中之所以可能，前提是
    // 容器把 bounds 写给了子节点——取一个**实际命中过**的点复判，不猜窗心坐标（行间隙会落到遮罩上）。
    const HitSpot inside = h.find_first("Text");
    AURORA_TEST_REQUIRE(inside.widget != nullptr);
    AURORA_TEST_CHECK_EQ(h.hit(inside.x, inside.y), inside.widget);
    AURORA_TEST_CHECK_NE(std::string_view{inside.widget->type_name()}, std::string_view{"Canvas"});

    // 点遮罩即关：`close()` 在派发中途把正在运行的那棵子树从宿主上摘掉，框架的命中链按节点持 keepalive，
    // 故这是合法现场而非悬垂（`deliver_chain` 的注释明写此为「on_click 回调重建页面」而设）。
    h.click(4.0F, static_cast<float>(kWindowHeight) * 0.5F);
    h.render();
    AURORA_TEST_CHECK_FALSE(panel->is_open());
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 0U);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);  // 点遮罩是关闭，不是提交
}

AURORA_TEST_CASE(a_text_row_commits_only_when_focus_leaves) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    const HitSpot spot = h.find_first("TextInput");
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    auto *box = dynamic_cast<au::TextInput *>(spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    // 认行：外观页第一个文本框是「默认前景色」，初值就是那份配置的 HEX。
    const std::string initial = borealis::ui::color_to_hex(probe.base.appearance.palette.default_foreground);
    AURORA_TEST_REQUIRE_EQ(box->value(), initial);

    // 逐字符（`set_value` 走的是控件自己的文本通道）：表单一个字节也不收，文件也不写。
    box->set_value("#12ab34");
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_REQUIRE(panel->form().value("appearance.palette.foreground") != nullptr);
    AURORA_TEST_CHECK_TRUE(panel->form().value("appearance.palette.foreground")->as_color() ==
                           probe.base.appearance.palette.default_foreground);

    // 失焦那一刻才交（`BlurCommitText` 覆写的是框架的 public virtual `on_focus_change`）。
    box->on_focus_change(false);
    h.render();
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);  // 前景色是「已接线 ∧ 即时」
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_CHECK_TRUE(probe.persisted[0].appearance.palette.default_foreground ==
                           borealis::ui::color_from_hex("#12ab34").value());
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());
}

/// @brief G29 的接货复验（可达的那一段）：展开的下拉选项面板进了真实派发链，于是「真点一个选项即提交」
///        写成正向判据。
///
/// 本例曾是「框架现状不支持什么」的钉子（钉子在裁决 7.57④⑤，CHANGELOG v0.57 / v0.58 在册并明写「回货后
/// 必须转红」）。回货形态是 `Widget::extra_hit_box(ctx)` 把「画在自身布局盒之外、仍归本件接管」的那片矩形
/// **交给祖先的下降闸**（`child.bounds().contains(local) || child.covers_extra_hit_box(local - origin, ctx)`），
/// 而 `Dropdown` 的覆写取的就是它与 `on_hit_test` 同源的那一份 `panel_box()`（缺省 `nullopt` ⇒ 未覆写的控件
/// 与改动前逐位等价）。于是本例两头都翻向正向：按链走场景根**能**命中该 `Dropdown`，且那一点上的真实单击
/// 选中 0 号档、收起面板、并把提交交回表单。
///
/// 目标选项固定取 0 号，故装载基线预置成 `Wide`（1 号）；探的那一点取在自身布局盒**之外**的那一段选项带里
/// （回货前那一段永远进不了链，盒内的那一段本来就可达，拿它翻正向等于什么都没翻）。落盘而非广播是因为这一行
/// `terminal.ambiguous_width` 是「接缝待开 ∧ 下次会话生效」——`apply_scope()` 把它折成只落盘（判据文 B3-a），
/// 所以面板改了它也不该动运行中的视口。选项行高 26 dp 是框架缺省且本件未改（`set_item_height` 未被调用）。
/// 本例只闭合到「那一格仍属所在行」为止，再往外的残段见下一条用例。
AURORA_TEST_CASE(clicking_an_open_dropdown_option_in_its_own_extra_hit_box_commits_G29) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    probe.base.terminal.ambiguous_width = borealis::term::AmbiguousWidth::Wide;  // 让 0 号档成为「改变取值」的那一档
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);
    // 下拉行集中在终端页，面板默认落在外观页，故先翻页再探。
    panel->select_page(SettingsPage::Terminal);
    h.render();
    // 认行：终端页只有 `terminal.ambiguous_width` 是两档下拉（`paste_newlines` / `right_click` 各三档，
    // 而 `long_line` / `bell` 因未接线只画只读摘要、根本不出 `Dropdown`）。
    const HitSpot spot = h.find_first("Dropdown", 40.0F, [](au::Widget *widget) -> bool {
        const auto *dropdown = dynamic_cast<au::Dropdown *>(widget);
        return dropdown != nullptr && dropdown->option_count() == 2U;
    });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    auto *dropdown = dynamic_cast<au::Dropdown *>(spot.widget);
    AURORA_TEST_REQUIRE(dropdown != nullptr);
    AURORA_TEST_REQUIRE_EQ(dropdown->selected_index(), 1);  // 预置的 Wide 经表单搬进了控件的选中位

    // 开：点主框那一点——它在布局盒之内，命中链可达，所以「展开」这一步是真实的。
    h.click(spot.x, spot.y);
    h.render();
    AURORA_TEST_REQUIRE(dropdown->is_open());

    const au::Rect layout = dropdown->paint_bounds();  // 只取它的**尺寸**：那是控件自身坐标空间里的量
    const float option_x = spot.box.origin.x + layout.size.width * 0.5F;
    // 探的那一点取在自身布局盒**之外**的第一条选项带里：行给下拉的紧约束是 40 dp，而面板贴着框架的
    // `box_height_`＝30 dp 之后铺开，故盒下沿再往下的 8 dp 仍属第一条选项、也仍属那一行。
    // 纵坐标以 `spot.box`（窗口坐标，闭态量得）为上沿基准，而非 `layout.origin`（内容坐标）。
    const au::Point local{.x = layout.size.width * 0.5F, .y = layout.size.height + 4.0F};
    const float option_y = spot.box.origin.y + local.y;
    const au::BuildContext ctx{};

    // 量的可达框与控件自报的盒高相差不到 1 dp（`reachable_box` 以 1 dp 步进的量化余量），故下面按窗口坐标
    // 点出去的那一探针，与该点在控件自身坐标空间里的申报 `local`，指的是同一片区域。相差更多只可能是这一
    // 行被视口或某个祖先裁掉了一部分，那时本例的前提就不再成立，而不是「判据红」。
    AURORA_TEST_REQUIRE(std::abs(spot.box.size.height - layout.size.height) <= 1.0F);
    // 反空转前提：探针点在视口之内（视口下沿＝卡片下沿＝窗口下沿减 `kCardEdgeDp`）。`find_first` 的 40 dp
    // 余量只是让它别挑到底部那一行，而**判据**是这一句——少了它，「祖先闸不认」就可能被读成 `Scroll`
    // 的视口裁剪，那种绿测的是裁剪而不是派发。
    AURORA_TEST_REQUIRE_MSG(option_y < static_cast<float>(kWindowHeight) - kCardEdgeDp,
                            "the probe point is outside the scroll viewport");
    // 三条前提逐条钉住，免得正向判据退化成「碰巧命中一个盒内的点」：
    // ① 这一点由该控件自己申报在追加命中盒之内（G29 的那份声明就在这里）。
    AURORA_TEST_REQUIRE_MSG(dropdown->covers_extra_hit_box(local, ctx), "the open panel does not cover the probed point");
    // ② 且它在控件的布局盒之外——回货前祖先只按布局盒判包含，那一段永远进不了链。
    AURORA_TEST_REQUIRE_MSG(local.y >= layout.size.height, "the probed point is inside the widget's own layout box");
    // ③ 按真实派发链走场景根**能**命中该控件（回货前恒不成立，是翻转本例的直接证据）。
    AURORA_TEST_REQUIRE_MSG(h.hit(option_x, option_y) == static_cast<au::Widget *>(dropdown),
                            "the dispatch chain still does not reach the dropdown through its ancestor");

    // 那一点上的真实单击：选中 0 号、收起、提交交回表单，并按该行的生效档位只落盘不广播。
    h.click(option_x, option_y);
    h.render();
    AURORA_TEST_CHECK_EQ(dropdown->selected_index(), 0);
    AURORA_TEST_CHECK_FALSE(dropdown->is_open());
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    // 落点归属由「写出去的那一份配置」说话，而不是由排版次序推断。
    AURORA_TEST_CHECK_TRUE(probe.persisted[0].terminal.ambiguous_width == borealis::term::AmbiguousWidth::Narrow);
    AURORA_TEST_REQUIRE(panel->form().value("terminal.ambiguous_width") != nullptr);
    AURORA_TEST_CHECK_TRUE(*panel->form().value("terminal.ambiguous_width")->as_text() == std::string{"narrow"});
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());  // 落盘成功后脏标记已推进
}

/// @brief G29 的回货只闭合了一层：面板伸出**所在行之外**的那一段仍进不了派发链（新登记缺口 G30）。
///
/// 追加命中盒的申报是**逐层只问直接子**的，孙辈的申报不随祖先上传。本件的嵌套是
/// `Scroll → Column → Row → Dropdown`（行区容器由 `LazyList` 改为 `Scroll` + `Column` 之后，`Dropdown`
/// 从「直接子」降成「孙辈」，而 `Column` 的下降闸问的是那一行 `Row` 自己的申报），故面板只有「还落在
/// 该行 bounds 内」的那一段可点：行高 56 dp、上下内边距各 8 dp，布局盒下沿再往下 8 dp 就到行的尽头，
/// 而面板贴着盒下沿还有整条一条选项（高 26 dp）。本例取 +12 dp 处，即行下沿之外 4 dp。
///
/// 三条断言合起来是**这条缺口的形态证明**而不是「没点上」的观测：控件申报覆盖、控件自己的兼容入口也认，
/// 唯独祖先下降闸不认。追加盒沿祖先链并成子树并集回货后本例必须转红。
///
/// 一条反空转前提已经做成断言：探点在视口下沿之上（`find_first` 的 40 dp 余量只是挑行启发，见其参数注），
/// 故它测的是「祖先闸不认」而不是 `Scroll` 的视口裁剪——后者会把肉眼不可见的那一段一并判成不可达，那种绿是假绿。
AURORA_TEST_CASE(the_option_panel_below_the_enclosing_row_is_still_not_dispatch_reachable_G30) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    probe.base.terminal.ambiguous_width = borealis::term::AmbiguousWidth::Wide;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);
    panel->select_page(SettingsPage::Terminal);
    h.render();
    const HitSpot spot = h.find_first("Dropdown", 40.0F, [](au::Widget *widget) -> bool {
        const auto *dropdown = dynamic_cast<au::Dropdown *>(widget);
        return dropdown != nullptr && dropdown->option_count() == 2U;
    });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    auto *dropdown = dynamic_cast<au::Dropdown *>(spot.widget);
    AURORA_TEST_REQUIRE(dropdown != nullptr);

    h.click(spot.x, spot.y);
    h.render();
    AURORA_TEST_REQUIRE(dropdown->is_open());

    const au::Rect layout = dropdown->paint_bounds();  // 只取它的**尺寸**：那是控件自身坐标空间里的量
    const float option_x = spot.box.origin.x + layout.size.width * 0.5F;
    const au::Point local{.x = layout.size.width * 0.5F, .y = layout.size.height + 12.0F};
    const float option_y = spot.box.origin.y + local.y;
    const au::BuildContext ctx{};

    AURORA_TEST_REQUIRE(std::abs(spot.box.size.height - layout.size.height) <= 1.0F);
    // 反空转：探针点在视口之内（少了这一句，下面那句「不可达」就可能测的是 `Scroll` 的视口裁剪而非祖先闸）。
    AURORA_TEST_REQUIRE_MSG(option_y < static_cast<float>(kWindowHeight) - kCardEdgeDp,
                            "the probe point is outside the scroll viewport");
    // 反空转：探点上有控件认领（视口之下或卡片之外的点也会「不是下拉」，那种绿证的是裁剪而不是闸）。
    AURORA_TEST_REQUIRE_MSG(h.hit(option_x, option_y) != nullptr, "the probe point is claimed by nobody");
    AURORA_TEST_REQUIRE_MSG(dropdown->covers_extra_hit_box(local, ctx), "the open panel does not cover the probed point");
    // 兼容入口与祖先闸共用同一份 `panel_box()`，故它认——「控件认、闸不认」的分叉正是本条缺口的内容。
    AURORA_TEST_REQUIRE_MSG(dropdown->hit_test(local, layout, ctx) == static_cast<au::Widget *>(dropdown),
                            "the widget's own hit test does not claim the point either");
    AURORA_TEST_CHECK_MSG(h.hit(option_x, option_y) != static_cast<au::Widget *>(dropdown),
                          "G30 closed: the extra hit box now propagates up the ancestor chain");
    // 既成事实的另一半：面板画在那里、点了没反应——既不选中也不提交。
    h.click(option_x, option_y);
    h.render();
    AURORA_TEST_CHECK_EQ(dropdown->selected_index(), 1);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
}

/// @brief 行区滚下去之后仍然点得动：滚动偏移非零时，按量出来的新位置真点一行开关即提交。
///
/// 行区容器由 `LazyList` 换成 `Scroll` + `Column` 是为 #114 那些高低不等的行（固定 `item_extent` 表达不出
/// 「主题卡 + 16 格色板 + 下拉」同列混排），代价是内容子节点的 bounds 从此是**内容坐标**，命中须经
/// `offset_y_` 换算回内容空间（框架 `scroll.h` 的「几何与命中契约」一节）。那条换算只有框架自己的用例守，
/// 故本件在生产路径上补这一条：它同时是 G27 回货（滚动容器的内容进得了真实命中链）在面板上的消费证人，
/// 也是 #114 每一根的落地前提——外观页 30 行 × 56 dp 本来就放不下，不滚就没有「看不见的行」这件事。
///
/// 刻意**不**按键位取控件：滚动之后视口里第一枚关闭态开关是哪一行的哪个键，是排版与偏移的函数而不是本件的
/// 契约，故这里只认「提交确实走完了表单与落盘」那一段，键名交给既有的按键用例去守。
AURORA_TEST_CASE(a_row_control_still_commits_after_the_row_area_has_been_scrolled) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    const HitSpot spot = h.find_first(
        "Switch", 0.0F, [](au::Widget *widget) -> bool {
            auto *sw = dynamic_cast<au::Switch *>(widget);
            return sw != nullptr && !sw->value();
        });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    au::Scroll *area = h.row_scroll(spot.x, spot.y);
    AURORA_TEST_REQUIRE(area != nullptr);
    AURORA_TEST_REQUIRE_EQ(area->offset_y(), 0.0F);

    // 三档 × 框架缺省 `step` 16 dp ＝ 内容上移 48 dp（负 `delta_y` 是往下滚，符号约定同 `ScrollViewport`）。
    for (int notch = 0; notch < 3; ++notch) {
        h.scroll(spot.x, spot.y, -1.0F);
    }
    h.render();
    AURORA_TEST_CHECK_GT(area->offset_y(), 0.0F);
    // 内容真的在窗口里挪了：同一个坐标上现在认领的不是那枚开关（挪开了，或落在行间隙上）。
    // 少了这一句，本例就只是「读到一个非零偏移」而没有证到派发面跟着挪。
    AURORA_TEST_REQUIRE(h.hit(spot.x, spot.y) != spot.widget);

    const HitSpot moved = h.find_first(
        "Switch", 0.0F, [](au::Widget *widget) -> bool {
            auto *sw = dynamic_cast<au::Switch *>(widget);
            return sw != nullptr && !sw->value();
        });
    AURORA_TEST_REQUIRE(moved.widget != nullptr);
    auto *toggle = dynamic_cast<au::Switch *>(moved.widget);
    AURORA_TEST_REQUIRE(toggle != nullptr);
    const float switch_x = moved.box.origin.x + moved.box.size.width * 0.5F;
    const float switch_y = moved.box.origin.y + moved.box.size.height * 0.5F;
    AURORA_TEST_REQUIRE_MSG(h.hit(switch_x, switch_y) == moved.widget,
                            "the measured box center is not dispatch-reachable after scrolling");

    h.click(switch_x, switch_y);
    h.render();
    AURORA_TEST_CHECK_TRUE(toggle->value());
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());  // 落盘成功后脏标记已推进
}

/// @brief chrome 色值交接：六类通用行控件必须吃本仓自己的深色 chrome，而不是框架控件的浅色主题缺省。
///
/// S5 立的是「界面配色不随**终端主题**联动」，不是「随控件缺省」：框架侧 `TextInput` 的聚焦态底色缺省
/// `{245,248,255}`、`SpinBox` / `Dropdown` 的框底 `255`、开关关闭态轨道 `{180,180,180}` 都是浅色时代的常数，
/// 落在深色卡片上最坏的一处是**白底白字**（本件文本色是 `kText`，近白）。本例逐件在真实帧缓冲上取一个
/// 「一定是底色」的点（盒内靠上，避开字形与 1 dp 描边），断它不亮。
///
/// 两处刻意的前提：开关只取**关闭态**那一枚（开启态轨道是本仓的强调色 `kAccent{189,147,249}`，拿「不亮」
/// 判它会把正确实现读成红）；下拉放在切页之后问，而切页是整块重建，故此前各例的控件指针到那一句就不可再用。
AURORA_TEST_CASE(the_editable_controls_paint_the_chrome_colors_not_the_light_defaults) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    // 文本行：未聚焦与聚焦两态都得是深色底（框架的浅色缺省正是聚焦那一态最亮）。
    const HitSpot text_spot = h.find_first("TextInput");
    AURORA_TEST_REQUIRE(text_spot.widget != nullptr);
    auto *box = dynamic_cast<au::TextInput *>(text_spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(text_spot, 0.9, 0.15)));
    h.click(text_spot.x, text_spot.y);
    h.render();
    // 没真的聚焦上就根本没走聚焦态那条绘制分支，这一句是本例的前提而不是附带观察。
    AURORA_TEST_REQUIRE(box->is_focused());
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(text_spot, 0.9, 0.15)));

    // 步进器：框底（数值文本从 y=8 起、箭头区在右侧 22 dp 之内，故取盒内靠上的中部）。
    const HitSpot spin_spot = h.find_first("SpinBox");
    AURORA_TEST_REQUIRE(spin_spot.widget != nullptr);
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(spin_spot, 0.5, 0.12)));

    // 开关关闭态轨道：缺省 {180,180,180} 是全部浅色缺省里最低的一档，仍必须被这条线抓住。
    const HitSpot toggle_spot = h.find_first(
        "Switch", 0.0F, [](au::Widget *widget) -> bool {
            auto *sw = dynamic_cast<au::Switch *>(widget);
            return sw != nullptr && !sw->value();
        });
    AURORA_TEST_REQUIRE(toggle_spot.widget != nullptr);
    AURORA_TEST_CHECK_EQ(static_cast<int>(toggle_spot.widget->paint_bounds().size.height), 24);
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(toggle_spot, 0.8, 0.5)));

    // 下拉主框（选项面板与主框共用同一份 `box_color_`，故主框这一读也守住了展开的那一片）。
    panel->select_page(SettingsPage::Terminal);
    h.render();
    const HitSpot dropdown_spot = h.find_first("Dropdown");
    AURORA_TEST_REQUIRE(dropdown_spot.widget != nullptr);
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(dropdown_spot, 0.5, 0.12)));
}

/// @brief G28 的接货复验：按钮标签交回框架的 i18n 查表，本件不再预先解析成 `std::string`。
///
/// 回货形态是 `Button::resolved_label(ctx)` 成为它 `on_layout` / `on_paint` 的**唯一**显示串来源
/// （解析结果连同实测宽高一起缓存进 `cached_display_text_`），`accessibility_label()` 复用同一份缓存，
/// 故本件那三枚按钮（四枚导航、关闭、恢复主题默认）可以原样收 `LocalizedString`。
///
/// 本例守的是**按钮交 `LocalizedString` 之后仍有人查表**：框架不查（回货前 `paint_label` 直读
/// `label.text`）或本件写错 key，显示串都是空串——`settings_text()` 走 `tr()`，而查表失败时框架回退到
/// 实例自己的 `text`，那份 text 恒空。它不区分「框架查表」与「本件预先解析成 `std::string` 再交出」两种
/// 形态（两者给出同一个显示串），故这里以「显示串逐字等于本件按同一张表查出的那一条」为判据，
/// 而不伪造一条只抓后者的断言。
AURORA_TEST_CASE(a_button_label_comes_from_the_framework_string_table_G28) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    const HitSpot spot = h.find_first("Button");
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    auto *button = dynamic_cast<au::Button *>(spot.widget);
    AURORA_TEST_REQUIRE(button != nullptr);

    // 布局与绘制已跑过一帧，故这里读到的就是 `resolved_label()` 缓存下来的那一份。
    const std::string shown = button->accessibility_label();
    AURORA_TEST_CHECK_FALSE(shown.empty());  // 框架查表失败回退 `LocalizedString::text`，而 `tr()` 的 text 恒空
    AURORA_TEST_CHECK_MSG(shown.find("settings.") == std::string::npos, "the button shows a raw locale key");
    int matched = 0;
    std::string matched_key{};
    for (std::string_view key : {"settings.close", "settings.action.unset", "settings.page.appearance",
                                 "settings.page.terminal", "settings.page.connection", "settings.page.shortcuts"}) {
        if (shown == borealis::ui::settings_label(key)) {
            ++matched;
            matched_key = std::string{key};
        }
    }
    AURORA_TEST_REQUIRE_MSG(matched == 1, "the button label matches none or several of the panel's entries: " + shown);
    // 扫到的是卡片右上角那一枚（行区里的「恢复主题默认」在更下方，且导航列被扫描起点 220 dp 排除在外）。
    AURORA_TEST_CHECK_EQ(matched_key, std::string{"settings.close"});
}

#else

AURORA_TEST_CASE(the_scrim_covers_the_whole_window_and_a_real_click_closes_the_panel) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(a_text_row_commits_only_when_focus_leaves) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(clicking_an_open_dropdown_option_in_its_own_extra_hit_box_commits_G29) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_option_panel_below_the_enclosing_row_is_still_not_dispatch_reachable_G30) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(a_row_control_still_commits_after_the_row_area_has_been_scrolled) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_editable_controls_paint_the_chrome_colors_not_the_light_defaults) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(a_button_label_comes_from_the_framework_string_table_G28) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_settings_panel
