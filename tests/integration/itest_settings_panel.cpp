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
///
///           一条测试现场的必要构造：`OverlayHost` 的浮层序号是从「基础内容之后」起算的
///           （`add_overlay` 返回 `children_.size() - 1`），故宿主**必须**带一个基础子节点——生产路径
///           上那是终端视口，本用例给一个 `Text`。空宿主的返回序号是 0，而 0 正是面板「未登记」的哨兵值。

#include <cstddef>
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
    host->add_overlay(au::Node{out_base});  // 占住子节点 [0]：基础内容
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
};

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
        host_->add_overlay(au::Node{base_});
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
    /// 取的是**命中链**而非兼容入口 `hit_test`：后者语义为「命中即止」，而 `LazyList` 为了让滚轮落在
    /// 整视口而刻意让它返回自身（其 `on_hit_test` 上方有注释说明）。真实指针派发走链，判据必须与派发
    /// 同源，否则行区里的每一个控件都会被我读成「抓不住」。
    [[nodiscard]] auto hit(float x_dp, float y_dp) -> au::Widget * {
        const std::vector<au::HitNode> chain = root_.widget().hit_test_chain(
            au::Point{.x = x_dp, .y = y_dp},
            au::Rect{.origin = au::Point{.x = 0.0F, .y = 0.0F},
                     .size = au::Size{.width = static_cast<float>(kWindowWidth),
                                      .height = static_cast<float>(kWindowHeight)}},
            au::BuildContext{});
        return chain.empty() ? nullptr : chain.back().ptr;
    }

    /// @brief 在窗口内扫描，取出第一个指定类型的控件**以及命中它的那一点**。
    ///
    /// 浮层树是面板私有的，`LazyList` 还会回收窗口外的条目，故这里不另开观测点，而是问框架的命中
    /// 测试「这一点上是谁」。步长 4 dp 足够格住 56 dp 的行高，且落点在控件盒内而不压边界；起点 220 dp
    /// 跳过左导航列（宽 200 dp），免把导航按钮当成行区控件。
    [[nodiscard]] auto find_first(std::string_view type_name) -> HitSpot {
        for (float y = kCardEdgeDp + 4.0F; y < static_cast<float>(kWindowHeight) - kCardEdgeDp; y += 4.0F) {
            for (float x = 220.0F; x < static_cast<float>(kWindowWidth) - kCardEdgeDp; x += 4.0F) {
                au::Widget *widget = hit(x, y);
                if (widget != nullptr && type_name == widget->type_name()) {
                    return HitSpot{.widget = widget, .x = x, .y = y};
                }
            }
        }
        return HitSpot{};
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

#else

AURORA_TEST_CASE(the_scrim_covers_the_whole_window_and_a_real_click_closes_the_panel) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(a_text_row_commits_only_when_focus_leaves) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_settings_panel
