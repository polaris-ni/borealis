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
///              不广播。**回货只闭合了一层**——追加盒当时只并「直接子」的申报，孙辈的申报不随祖先上传，故
///              面板伸出所在行之外的那一段仍不可达，本套件据此另留一条钉子（新缺口 G30）。G30 的回货把这一句
///              翻正：`covers_extra_hit_box()` 升级为**子树聚合**（自身 ∪ 逐子节点按 `bounds().origin` 折算递归，
///              折算与 `on_hit_test_chain` 的下降式同构），那一段因此**可达**（`the_option_panel_below_the_
///              enclosing_row_now_reaches_the_dispatch_chain_G30`）。可达之后曾仍**点不准**：祖先的内边距平移
///              没进 `HitNode.origin`，派发器据以本地化的坐标比该控件的绘制位置大 8 dp，`Dropdown` 按本地纵
///              坐标反算的选项序号因此整体下移一格（缺口 **G31**）。G31 的回货把命中侧与绘制侧收敛为**同一个
///              `content_origin`**（`bounds.origin + 修饰链平移`），故那条现状钉子翻成
///              `a_real_click_below_the_enclosing_row_picks_the_option_under_it_G31`：真点行外那一段的第一条
///              选项带即选中该档并走完整条提交腿。两条用例各判一层（可达 / 点得准），合成一例就会在只回货
///              其一时不知道该翻哪一半。
///           ⑧ **按钮标签由框架查表**（缺口 G28 的接货复验，裁决 7.59）：本件那三枚按钮原样交回
///              `LocalizedString` 之后，显示串仍是词条表给的那一条；查表没发生就回退到实例自己的 `text`，
///              而 `tr()` 造出的实例那份 text 恒空。
///           ⑨ **行区是滚动容器而不是固定行高的列表**（裁决 7.60）：`LazyList` 的 `item_extent` 是**全局**
///              一档，表达不出 #114 那种「主题卡 / 16 格色板 / 下拉」高低不等的行，故行区取 `Scroll` +
///              `Column` 全量实例化。换容器就把坐标模型换掉了——`Scroll` 的内容子节点 bounds 是**内容坐标**
///              （命中时经 `offset_y_` 换算），于是本套件的每一处探针都改成按真实派发结果量窗口坐标
///              （`Harness::reachable_box`），并新增一条「滚过一段之后真点一行开关即提交」的证人：它是
///              G27 回货（滚动容器的内容进得了命中链）在面板生产路径上的消费腿。
///           ⑩ **回退链重排区段**（裁决 7.52 的 S8，#114 第二棒）：这一行的值是一个**有序族名数组**，
///              控件形态因此不是「一个输入框」而是三件事——链内条目（可上移 / 下移 / 移除）、按输入过滤
///              的候选池、以及四档提示文案（空链 / 已达上限 / 已被截断 / 没有匹配）。判据取四条腿：
///              条目次序与可用性标记**从表单读回**（表单是权威，控件只是它的投影）、一次结构变更
///              **恰落盘一次并恰广播一次**（该键 `Wired ∧ Immediate`）、超出框架上限的链**照全量画**而只
///              关掉追加口（静默截断会把用户配的族抹掉）、以及 `Escape` 在键盘抓取态下**先交还抓取**而
///              不关面板。指针拖拽换位在无头环境不可判（框架 `end_drag` 只在 `reduce_motion` 或位移不足
///              半格时立即结算），故换位判据一律走按钮与键盘两条同步路径。另有两条实测随本段入册：
///              **`show` 翻转不标脏布局**（框架把它当测量输入，写它不失效任何缓存），于是候选池必须由
///              调用方补布局脏，而本套件的候选用例就是那一次补脏的证人。补的处数随回货从两处降为一处：
///              登记时只标叶子那份脏**到不了渲染根**（一次滚动之后 `Node::~Node` 无条件清子节点的布局父
///              指针，缺口 **G32**），故还要多标一层卡片外层 `LayoutBuilder`；G32 回货后断链不复存在，
///              撤掉那层祖先补脏两例照旧绿（变异证明：去掉叶子补脏则两例同红），而框架侧把「应用侧顺手
///              多标祖先脏」判为掩盖而非修复，故补偿随之撤除；另**禁用态的
///              `Button` 照样在命中链里**（框架只在 `wants_click()` 上分档，`Button` 没有覆盖任何命中
///              入口），故「达上限时点不动」的证人是真点一次并判它既不落盘也不广播，另加一条「浮层还在」
///              ——点击被遮罩接走也会关掉面板，那一档计数同样不变。登记时本段写作「禁用按钮不进命中链、
///              扫不到它」，该句实测不成立并已就地更正。
///
///           一条测试现场的必要构造：`OverlayHost` 的浮层序号是从「基础内容之后」起算的
///           （`add_overlay` 返回 `children_.size() - 1`，回货后宿主无基础内容时返回 `std::nullopt`），
///           故宿主**必须**带一个基础子节点——生产路径上那是终端视口，本用例给一个 `Text`。

#include <algorithm>
#include <chrono>
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
#include "aurora/render/font_engine.h"
#include "aurora/widget/button.h"
#include "aurora/widget/dropdown.h"
#include "aurora/widget/reorderable_list.h"
#include "aurora/widget/scroll.h"
#include "aurora/widget/switch.h"
#include "aurora/widget/text.h"
#include "aurora/widget/text_input.h"
#include "borealis/config/form_transfer.h"
#include "borealis/config/settings.h"
#include "borealis/config/themes.h"
#include "borealis/ui/color_text.h"
#include "borealis/ui/palette.h"
#include "borealis/ui/right_click.h"
#include "borealis/ui/settings_catalog.h"
#include "borealis/ui/settings_form.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/settings_i18n.h"
#include "../../src/ui/settings_panel.h"
#include "../../src/ui/settings_preview.h"

namespace borealis::test_cases::itest_settings_panel {

namespace {

using borealis::config::Settings;
using borealis::ui::ApplyScope;
using borealis::ui::CommitIssue;
using borealis::ui::ConsumerStatus;
using borealis::ui::ControlKind;
using borealis::ui::EffectLevel;
using borealis::ui::FontFamilyEntry;
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

/// 通用控件行的两个几何量（`src/ui/settings_panel.cpp` 的 `kRowExtentDp` 与 `build_row` 里那份 `EdgeInsets`
/// 的上下档）：下面两条下拉用例要把探点摆在「所在行**之外**」，而行的可达框只到控件自己的盒下沿为止（行高
/// 56 ＝ 内边距 8 + 控件 40 + 内边距 8），故行顶与行底必须由这两个量算出来而不是靠量——量到的是控件的框，
/// 不是行的框。写常数而不是现问控件：行高与内边距是面板自己的排版契约（判据文 §1），改它就该让用例转红。
constexpr float kRowExtentDp = 56.0F;
constexpr float kRowPaddingDp = 8.0F;

/// CJK-LITERAL: locale-output - 断言的是面板上那两枚角标的**最终文案**，换成英文就等价于
/// 「面板用了别的词条 key」，而那正是本判据要抓的错。词条 key 与表列的对应另由 `i18n` 那条用例守。
constexpr std::string_view kBadgeDeferred = "延后";
constexpr std::string_view kBadgeNextSession = "下次会话生效";
/// CJK-LITERAL: locale-output - 同上，A1-b 的「自定义」是第三枚角标，且它不来自表列而是来自「色板
/// 与该主题默认不逐槽相等」这条状态，故只能按文案判。
constexpr std::string_view kBadgeCustomized = "自定义";

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

/// @brief 从真实主题表造候选（卡片次序即该表次序，与装配层 `src/main.cpp` 同一搬法）。
///
/// 判据 A1-a 要「卡名逐字来自主题表」，用例侧因此拿同一份表独立比，而不是把名字抄进断言。
[[nodiscard]] auto builtin_theme_choices() -> std::vector<SettingsPanel::ThemeChoice> {
    std::vector<SettingsPanel::ThemeChoice> out;
    for (const borealis::config::BuiltinTheme &theme : borealis::config::builtin_themes()) {
        out.push_back(SettingsPanel::ThemeChoice{.name = std::string{theme.name}, .palette = theme.palette});
    }
    return out;
}

/// 回退链容量的真值源在框架头（面板与本套件各取一次而不互抄，与裁决 7.50 的同一分工）：「已达上限」
/// 与「超出上限」两档提示都把这一个数字经词条的位置参数 `{0}` 交出去。
constexpr std::size_t kChainCapacity = aurora::render::AURORA_TEXT_FALLBACK_CHAIN_MAX;

/// @brief 把链的视图折成纯族名序列（逐位比次序时不必在每条断言里重复写 `.family`）。
[[nodiscard]] auto chain_families(const std::vector<SettingsPanel::ChainItem> &items) -> std::vector<std::string> {
    std::vector<std::string> out;
    out.reserve(items.size());
    for (const SettingsPanel::ChainItem &item : items) {
        out.push_back(item.family);
    }
    return out;
}

/// @brief 回退链的一条判据写法：次序即语义（回退顺序），故逐位比而不是比集合。
auto check_chain_sequence(std::string_view what, const std::vector<std::string> &got,
                          const std::vector<std::string> &want) -> void {
    check_key_sequence(what, got, want);
}

/// @brief 用例侧独立折一份外观包（与 `src/main.cpp` 的 `make_appearance()` 同一算式，但刻意不共享代码）。
///
/// 判据 F-c 说的是「预览与主视口取同一份外观」，而它真正可判的那一半是**两处各自折算仍逐位相同**：用例
/// 直接调装配层那份函数就只会证到「同一个函数被调了两次」。字体族在这里取配置原值（与既有像素用例同一
/// 族名，环境无关），不像装配层那样过 `choose_font_family`——那条腿已由 `utest_font_choice` 与真机走查
/// 各自守，本件要的只是「同一份 `Settings` 折出同一份 `Appearance`」。
[[nodiscard]] auto test_appearance(const Settings &settings) -> borealis::ui::TerminalView::Appearance {
    borealis::ui::TerminalView::Appearance appearance;
    appearance.palette = settings.appearance.palette;
    appearance.ref_font = au::Font{.family = settings.appearance.font_family,
                                   .size_pt = static_cast<float>(settings.appearance.font_size_pt),
                                   .weight = 400};
    appearance.typography = borealis::ui::Typography{
        .line_height = settings.appearance.font_line_height,
        .letter_spacing_dp = settings.appearance.font_letter_spacing_dp,
    };
    appearance.padding_dp = settings.appearance.viewport_padding_dp;
    appearance.blink_period = std::chrono::milliseconds{settings.appearance.cursor_blink_period_ms};
    appearance.font_fallback_chain = settings.appearance.font_fallback_chain;
    return appearance;
}

/// @brief 存储侧与绘制侧的接缝替身：只数「被调了几次」并留下最后一次搬到的配置。
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
    /// @brief 交回面板的主题候选（缺省即真实 `config::builtin_themes()` 的整套）。
    ///
    /// 清空它就是造出「当前主题名不在候选表里」的现场：没有可比基线，A1-b 的「自定义」与
    /// 「恢复主题默认」的禁用态都由这一档决定。
    std::vector<SettingsPanel::ThemeChoice> theme_choices = builtin_theme_choices();
    /// @brief 交回面板的字体族目录（缺省是一小张确定表，与装配层交「真实目录」同一形态）。
    ///
    /// 用例侧要的只是「候选池随输入收窄」与「链内族名被排除」两条可判行为，故这里给一张小表而不是
    /// 真去枚举系统字体目录（那是同步 IO，且内容随机器变）。清空它就是造出「一个候选都没有」的现场。
    std::vector<FontFamilyEntry> family_catalog{
        FontFamilyEntry{.family = "Cascadia Code", .monospace = true},
        FontFamilyEntry{.family = "Consolas", .monospace = true},
        FontFamilyEntry{.family = "DejaVu Sans Mono", .monospace = true},
        FontFamilyEntry{.family = "Fira Code", .monospace = true},
        FontFamilyEntry{.family = "JetBrains Mono", .monospace = true},
        FontFamilyEntry{.family = "Noto Sans Mono", .monospace = true},
    };
    /// 预览的两条接缝装不装（S7 的「不装即不画」）：必须在 `hooks()` 之前设，因为装的是**闭包本身**
    /// 而不是一个在调用时才读的 bool——判据要证的是「接缝缺席时面板一个会话都不建」。
    bool with_preview = false;
    std::size_t preview_wake_calls = 0;  ///< `preview_wake` 被叫过几次（夹具重投的尾沿脏靠它）。

    /// @brief 交出一副挂到本替身上的 `Hooks`。
    ///
    /// 各闭包都按 `this` 取值而不是按建立时的快照，故 `hooks()` 交出之后仍可改 `load_script`、
    /// `fail_persist` 与 `theme_choices`——装载腿每次 `open()` 走一次、候选腿每次重建浮层走一次，
    /// 分叉现场因此能在同一个面板实例上逐次注入。
    [[nodiscard]] auto hooks() -> SettingsPanel::Hooks {
        SettingsPanel::Hooks out = SettingsPanel::Hooks{
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
            .themes =
                [this]() -> std::vector<SettingsPanel::ThemeChoice> {
                    return theme_choices;
                },
            .families =
                [this]() -> std::vector<FontFamilyEntry> {
                    return family_catalog;
                },
            .preview_appearance = nullptr,
            .preview_wake = nullptr,
        };
        if (with_preview) {
            out.preview_appearance = [this]() -> borealis::ui::TerminalView::Appearance {
                return test_appearance(base);
            };
            out.preview_wake = [this]() -> void {
                ++preview_wake_calls;
            };
        }
        return out;
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
            // 主题卡与 16 格色板在 #114 第一棒落成可交互区段，回退链在第二棒落成重排区段，
            // 故这里只剩「字体族下拉（形态待裁决）」与「快捷键只读表」两类专用形态仍是占位。
            AURORA_TEST_CHECK_EQ(row.editable, control->consumer != ConsumerStatus::Absent &&
                                                   control->kind != ControlKind::FontDropdown &&
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
    // 仍未落地的专用形态：如实显示当前值，而不是给一个「点了没反应」的控件（S9 / D3-a 同口径）。
    const SettingsPanel::VisibleRow *family = find_row(rows, "appearance.font_family");
    AURORA_TEST_REQUIRE(family != nullptr);
    AURORA_TEST_CHECK_FALSE(family->editable);
    AURORA_TEST_CHECK_EQ(family->summary, probe.base.appearance.font_family);
    // 主题卡、16 格色板与回退链三处已落成区段：可交互，且摘要留空——当前值在它们自己的控件里，行上
    // 再显示一份就是第二个真值源。回退链那条空链显示的也不是「空摘要」而是「空列表 + 一句提示」，
    // 故它满足的是下面那条「可交互行摘要一律留空」的通则，这里只把它从占位行的名单里摘出来。
    AURORA_TEST_CHECK_TRUE(find_row(rows, "appearance.theme")->editable);
    AURORA_TEST_CHECK_TRUE(find_row(rows, "appearance.palette.basic")->editable);
    AURORA_TEST_CHECK_TRUE(find_row(rows, "appearance.font_fallback_chain")->editable);
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

/// @brief 判据 A1-a：主题卡的次序、名字与四格样例都逐字来自 `config::builtin_themes()`。
///
/// 名字取的是**存储键名**而不是显示文案（裁决 7.61①）：`BuiltinTheme` 只有 `name` 一个字段，而那句
/// 「显示文案另经 `StringTable`」是未落地的规划——词条表里从未登记八套主题名，而查表失败的实测回退是
/// **空串**（G28 那条教训）。自造一份名表就是第二个真值源，故卡片显示的就是存储键。
/// 四格样例由用例侧按同一份色板**独立折一次**（前 / 背 / 光标 / `basic[1]`，光标未配按裁决 7.25③ 回落
/// 前景色，即裁决 7.61② 那条「强调格无数据来源」的处置），而不是取面板算好的那一份，否则「第三格取错
/// 槽」这类错误结构上抓不到。
AURORA_TEST_CASE(the_theme_cards_follow_the_builtin_theme_table_in_order) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    const auto themes = borealis::config::builtin_themes();
    const auto cards = panel.theme_cards();
    AURORA_TEST_REQUIRE_EQ(cards.size(), themes.size());
    std::size_t selected = 0;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        AURORA_TEST_TRACE(std::string{themes[i].name});
        AURORA_TEST_CHECK_EQ(cards[i].name, std::string{themes[i].name});
        const borealis::ui::PaletteSpec &palette = themes[i].palette;
        const std::vector<std::string> want{
            borealis::ui::color_to_hex(palette.default_foreground),
            borealis::ui::color_to_hex(palette.default_background),
            borealis::ui::color_to_hex(palette.cursor_color.value_or(palette.default_foreground)),
            borealis::ui::color_to_hex(palette.basic[1])};
        for (std::size_t j = 0; j < want.size(); ++j) {
            AURORA_TEST_CHECK_EQ(cards[i].samples[j], want[j]);
        }
        if (cards[i].selected) {
            ++selected;
            AURORA_TEST_CHECK_EQ(cards[i].name, probe.base.appearance.theme);
        }
    }
    AURORA_TEST_CHECK_EQ(selected, 1U);  // 选中态唯一，且就是表单点名的那一套
    // 缺省配置就是 dracula 的那一份，所以既没有「自定义」角标也没有任何色差（判据 A1-b 的反面）。
    AURORA_TEST_CHECK_FALSE(panel.is_palette_customized());
    AURORA_TEST_CHECK_TRUE(find_row(panel.visible_rows(), "appearance.theme")->badge.empty());
}

/// @brief 判据 A1-b：改一格色板就在**主题行**挂「自定义」，改回来它就消失，且它不是第十张卡。
AURORA_TEST_CASE(a_changed_slot_puts_customized_on_the_theme_row_not_a_tenth_card) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    const std::size_t card_count = panel.theme_cards().size();
    AURORA_TEST_REQUIRE(card_count > 0U);
    const auto table = panel.form().value("appearance.palette.basic")->as_color_table();
    AURORA_TEST_REQUIRE(table.has_value());
    const std::vector<RgbaColor> before = *table;

    AURORA_TEST_REQUIRE_EQ(panel.commit_slot("appearance.palette.basic", 5U, "#0A0B0C"), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);  // 色板行是「已接线 ∧ 即时」
    AURORA_TEST_CHECK_TRUE(panel.is_palette_customized());
    AURORA_TEST_CHECK_EQ(find_row(panel.visible_rows(), "appearance.theme")->badge, std::string{kBadgeCustomized});
    AURORA_TEST_CHECK_EQ(panel.theme_cards().size(), card_count);  // 「自定义」不另起一张卡

    // 改回来：角标与色差都消失，但**落盘会再多一次**——成功落盘会把基线推进到刚写出的那份，
    // 于是「改回原值」相对新基线是一次真改动（与 `a_reverted_change_neither_persists_nor_broadcasts`
    // ② 的注释同一条口径，那里之所以不多发是因为中间那次落盘失败了）。
    AURORA_TEST_REQUIRE_EQ(panel.commit_slot("appearance.palette.basic", 5U,
                                             borealis::ui::color_to_hex(before[5])),
                           CommitIssue::None);
    AURORA_TEST_CHECK_FALSE(panel.is_palette_customized());
    AURORA_TEST_CHECK_TRUE(find_row(panel.visible_rows(), "appearance.theme")->badge.empty());
    AURORA_TEST_CHECK_EQ(panel.theme_cards().size(), card_count);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 2U);
    AURORA_TEST_CHECK_FALSE(panel.form().is_dirty("appearance.palette.basic"));

    // 四张单格槽（前景 / 背景 / 光标 / 选区）也参与「自定义」判定，不是只有那 16 格：把光标色改成
    // 主题默认之外的一档，角标必须挂上——只比 16 格的实现会在这一条上静默放行。
    const FormValue *cursor_before = panel.form().value("appearance.palette.cursor");
    AURORA_TEST_REQUIRE(cursor_before != nullptr);
    const std::optional<RgbaColor> cursor = cursor_before->as_color();
    AURORA_TEST_REQUIRE(cursor.has_value());
    AURORA_TEST_REQUIRE_EQ(panel.commit("appearance.palette.cursor",
                                        FormValue::color(borealis::ui::RgbaColor{
                                            .red = static_cast<std::uint8_t>(cursor->red == 0xFF ? 0x00 : 0xFF),
                                            .green = cursor->green,
                                            .blue = cursor->blue})),
                           CommitIssue::None);
    AURORA_TEST_CHECK_TRUE(panel.is_palette_customized());
    AURORA_TEST_CHECK_EQ(find_row(panel.visible_rows(), "appearance.theme")->badge, std::string{kBadgeCustomized});
}

/// @brief 回退链区段把表单里的那份链**逐位**投影成条目，并按链长给出四档提示中的相应一档。
///
/// 判据取「视图 ＝ 表单的投影」而不是「视图 ＝ 控件树的状态」：链的唯一权威是 `form_`（裁决 7.52 的 S3①），
/// 次序即语义（回退顺序），故逐位比而不是比集合。四档提示里两档带位置参数（上限数），用例侧按同一个
/// 框架常量独立复算，而不是把数字抄进断言。
AURORA_TEST_CASE(the_chain_section_projects_the_form_and_the_hint_tracks_its_length) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    probe.base.appearance.font_fallback_chain = {"Fira Code", "Consolas", "Cascadia Code"};
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    const SettingsPanel::ChainView chain = panel.chain_view();
    check_chain_sequence("chain entries", chain_families(chain.items),
                         {"Fira Code", "Consolas", "Cascadia Code"});
    // 首项没有「上移」、末项没有「下移」——与 `move_chain_item()` 的越界不动是同一判据的两道（7.38⑥ F-b）。
    AURORA_TEST_CHECK_FALSE(chain.items[0].can_move_up);
    AURORA_TEST_CHECK_TRUE(chain.items[0].can_move_down);
    AURORA_TEST_CHECK_TRUE(chain.items[2].can_move_up);
    AURORA_TEST_CHECK_FALSE(chain.items[2].can_move_down);
    AURORA_TEST_CHECK_FALSE(chain.at_capacity);
    AURORA_TEST_CHECK_TRUE(chain.hint.empty());  // 有链、未达上限、没有过滤文本：无提示

    // 空链那一档：它是「不注入按族链」而不是「链上有一个空族名」，故提示必须显式说出来。
    AURORA_TEST_CHECK_EQ(panel.commit("appearance.font_fallback_chain", FormValue::text_list({})), CommitIssue::None);
    AURORA_TEST_CHECK_TRUE(panel.chain_view().items.empty());
    AURORA_TEST_CHECK_EQ(panel.chain_view().hint, borealis::ui::settings_label("settings.chain.empty"));

    // 恰达上限：追加口关死，提示说的是「已达上限」。
    std::vector<std::string> full;
    for (std::size_t i = 0; i < kChainCapacity; ++i) {
        full.push_back("Family " + std::to_string(i));
    }
    AURORA_TEST_CHECK_EQ(panel.commit("appearance.font_fallback_chain", FormValue::text_list(full)),
                         CommitIssue::None);
    AURORA_TEST_REQUIRE_EQ(panel.chain_view().items.size(), kChainCapacity);
    AURORA_TEST_CHECK_TRUE(panel.chain_view().at_capacity);
    AURORA_TEST_CHECK_EQ(panel.chain_view().hint,
                         borealis::ui::settings_label("settings.chain.full",
                                                      {au::LocalizedString{std::to_string(kChainCapacity)}}));

    // 超出上限：本件**不裁数据**（擅自裁到前 N 项会让用户改别的一行时把存储里那几条静默抹掉），
    // 故这里既判「全量仍在」也判「提示换档」，两档必须互异——只判非空会被「提示写错档」读成绿。
    full.push_back("Family over");
    AURORA_TEST_CHECK_EQ(panel.commit("appearance.font_fallback_chain", FormValue::text_list(full)),
                         CommitIssue::None);
    AURORA_TEST_REQUIRE_EQ(panel.chain_view().items.size(), kChainCapacity + 1U);
    AURORA_TEST_CHECK_TRUE(panel.chain_view().at_capacity);
    AURORA_TEST_CHECK_EQ(panel.chain_view().hint,
                         borealis::ui::settings_label("settings.chain.truncated",
                                                      {au::LocalizedString{std::to_string(kChainCapacity)}}));
    AURORA_TEST_CHECK_TRUE(panel.chain_view().hint != borealis::ui::settings_label(
                             "settings.chain.full", {au::LocalizedString{std::to_string(kChainCapacity)}}));
}

/// @brief 一次改链 ＝ 一次落盘 + 一次即时广播（该键 `Wired ∧ Immediate`，判据文 A5-a 的那条计数线）。
///
/// 「切主题实现成逐键提交就是六次写文件」那条主题卡判据在回退链上的对应形态：一次结构性改动若拆成
/// 逐键提交，运行中的视口就会被 N 份半成品链各广播一次。
AURORA_TEST_CASE(a_chain_structural_change_persists_once_and_broadcasts_once) {
    borealis::ui::install_settings_strings();
    StoreProbe probe;
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;
    SettingsPanel panel{*host, shortcuts, probe.hooks()};
    panel.open();

    const SettingsControl *control = borealis::ui::find_settings_control("appearance.font_fallback_chain");
    AURORA_TEST_REQUIRE(control != nullptr);
    // 前置条件：这条判据之所以能同时要求「广播一次」，是因为该键既已接线又属即时生效档。
    AURORA_TEST_CHECK_EQ(borealis::ui::apply_scope(*control), ApplyScope::PersistAndApplyNow);

    AURORA_TEST_CHECK_EQ(panel.commit("appearance.font_fallback_chain",
                                       FormValue::text_list({"Consolas", "Cascadia Code"})),
                         CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);
    AURORA_TEST_CHECK_FALSE(panel.form().has_unsaved_changes());
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    check_chain_sequence("what actually got written", probe.persisted.front().appearance.font_fallback_chain,
                         {"Consolas", "Cascadia Code"});
    AURORA_TEST_REQUIRE_EQ(probe.broadcast.size(), 1U);
    check_chain_sequence("what the viewport was handed", probe.broadcast.front().appearance.font_fallback_chain,
                         {"Consolas", "Cascadia Code"});

    // 把同一条链原样再交一次：脏标记按值比较，故这一次既不写盘也不广播（链形态上的 S3①）。
    // 这里不写「改回装载的那一份」——成功落盘已把基线推进到刚写出的那份，改回去相对新基线是一次真改动
    // （那一条腿由 `a_reverted_change_neither_persists_nor_broadcasts` 以失败落盘守住基线不动的形态）。
    AURORA_TEST_CHECK_EQ(panel.commit("appearance.font_fallback_chain",
                                      FormValue::text_list({"Consolas", "Cascadia Code"})),
                         CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);
    AURORA_TEST_CHECK_FALSE(panel.form().has_unsaved_changes());

    // 清空是相对基线的真改动：再落一次，且写出去的那份就是空链。
    AURORA_TEST_CHECK_EQ(panel.commit("appearance.font_fallback_chain", FormValue::text_list({})), CommitIssue::None);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 2U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 2U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 2U);
    check_chain_sequence("the emptied chain", probe.persisted[1].appearance.font_fallback_chain, {});
}

/// @brief 预览的两条接缝是**可选腿**：不装就一张横条都没有，装了才建会话，关闭即销毁。
///
/// S7 的代价（面板持有一份不经 `main` 装配的连接替身）只应在装配层真想要预览时才付，故本例判三件事：
/// 接缝缺席 ⇒ 面板既有行为一字不变；接缝在场 ⇒ 每次 `open()` 现建一份、`close()` 立即释放（浮层撤掉之后
/// 那棵控件树已脱离宿主，留一份「看起来还挂在树上」的视口是最难查的陈旧态）；以及 `preview_wake` 至少被
/// 叫过一次——夹具随 `Session::resize` 重投发生在视口的 `on_layout` 里，那批脏**要下一帧才排**，少了这一
/// 次唤醒，横条画得出来而内容停在上一版。
AURORA_TEST_CASE(the_preview_bar_exists_only_when_the_assembly_wires_its_seams) {
    borealis::ui::install_settings_strings();
    au::Node root;
    std::shared_ptr<au::Widget> base;
    std::shared_ptr<au::OverlayHost> host = make_host(base, root);
    au::ShortcutRegistry shortcuts;

    StoreProbe bare;
    {
        SettingsPanel panel{*host, shortcuts, bare.hooks()};
        panel.open();
        AURORA_TEST_CHECK_TRUE(panel.preview_view() == nullptr);
        AURORA_TEST_CHECK_EQ(bare.preview_wake_calls, 0U);
        panel.close();
    }

    StoreProbe wired;
    wired.with_preview = true;
    {
        SettingsPanel panel{*host, shortcuts, wired.hooks()};
        panel.open();
        AURORA_TEST_REQUIRE(panel.preview_view() != nullptr);
        AURORA_TEST_CHECK_GT(wired.preview_wake_calls, 0U);
        panel.close();
        AURORA_TEST_CHECK_TRUE(panel.preview_view() == nullptr);
        panel.open();
        // 「同一副面板重开之后拿到的是新实例」一句**不判指针身份**：`close()` 刚释放的堆块会被下一次
        // `make_unique` 原样复用，实测地址逐位相同，故身份既不能证真也不能证伪（裁决 7.66 的判据边界）。
        // 「close 即销毁」由上一句 `== nullptr` 独立守住，「重开又建」由本句 `!= nullptr` 守住。
        AURORA_TEST_REQUIRE(panel.preview_view() != nullptr);
        panel.close();
    }
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

    /// @brief 布局 → 排预览的脏 → 再绘制：预览横条的内容**要两帧**才成形（S7 的 `preview_wake` 腿）。
    ///
    /// 夹具是在视口的 `on_layout` 里随 `Session::resize` 重投的，那一批脏行排进 `DamageQueue` 之后由
    /// `pump()`（= `TerminalView::on_frame`）提交并标脏绘制，故单靠 `render()` 只能画出「一条空的
    /// 140 dp 横条」。装配层在 `Application::set_on_frame` 里逐帧调 `pump_preview()` 就是为了这一拍，
    /// 用例侧不泵就等于测另一条路径。
    /// @param panel 面板（预览本体归它持有）。
    /// @param extra 额外要泵的对照视口（§8 判据① 的第二台预览，面板不认识它）；可空。
    auto pump_and_render(SettingsPanel &panel, borealis::ui::SettingsPreview *extra = nullptr) -> void {
        render();
        panel.pump_preview();
        if (extra != nullptr) {
            extra->pump();
        }
        render();
    }

    /// @brief 把一棵控件树作为浮层加进场景根（§8 判据① 的对照视口走这条，而不是挂进卡片里）。
    ///
    /// 后加的浮层画在先加者之上，故对照视口压在卡片之上；它与卡片底部那条横条的矩形不相交，因此
    /// 横条上的取点与派发仍归面板自己的预览。
    /// @param node 该控件树的句柄（宿主持其所有权，本件只交节点）。
    auto add_overlay(au::Node node) -> void {
        (void)host_->add_overlay(std::move(node));
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
                // 门槛比的是**最后一个命中点**而不是 `box.bottom()`：后者按可达框的「含端点计数」约定（尺寸
                // ＝ 末点 − 首点 + 1）恰好越过真正的末点 1 dp，于是与卡片下沿齐平的那一条横条会被整体跳过
                // ——而预览横条按 F-e 正是要齐平（早先的实测读数：可达框末点 584、`bottom()` 585、门槛 584）。
                if (box.origin.y + box.size.height - 1.0F + room_below_dp > static_cast<float>(kWindowHeight) - kCardEdgeDp) {
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

    /// @brief 按**窗口坐标**取一个像素，同样先钉住归属（`probe` 的分数形态按格算不方便的场合用它）。
    ///
    /// 预览格心的取点由该视口自己的 `GridGeometry` 折算而来（见 `cell_center`），是浮点坐标而不是框内
    /// 分数，故这里要一个按点取色并自证归属的入口。归属不成立即转红：横条与卡片之外只隔着遮罩，取错
    /// 一点就会读到遮罩底色，而「两处采样逐位相等」那种判据最怕的就是两边都读到同一个错的东西。
    [[nodiscard]] auto probe_point(au::Widget *widget, float x_dp, float y_dp) -> RgbaColor {
        AURORA_TEST_REQUIRE_MSG(hit(x_dp, y_dp) == widget, "the sampled pixel is not claimed by the control under test");
        return pixel(x_dp, y_dp);
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

    /// @brief 面板登记的快捷键表（`Escape` 的交接腿要经它派发，与 `open()` 登记的是同一份）。
    [[nodiscard]] auto shortcuts() -> au::ShortcutRegistry & { return shortcuts_; }

    /// @brief 量出该控件可达框的心点（先确认「派发链在这一点上确实交回它」）。
    ///
    /// 真实点击的坐标一律由此推，而不是控件自己的 `paint_bounds()`：行区是 `Scroll` 之后那份 bounds 是
    /// 内容坐标（见 `reachable_box` 的注），拿它当窗口坐标点就会点错一行。
    /// @param widget 期望命中的控件。
    /// @param x_dp 已知命中它的一点的横坐标（`find_first` 给出）。
    /// @param y_dp 已知命中它的一点的纵坐标。
    /// @return 框心（窗口坐标）；该控件不可达时为空。
    [[nodiscard]] auto pointer_to(au::Widget *widget, float x_dp, float y_dp) -> std::optional<au::Point> {
        if (widget == nullptr) {
            return std::nullopt;
        }
        const au::Rect box = reachable_box(widget, x_dp, y_dp);
        const au::Point center{.x = box.origin.x + box.size.width * 0.5F, .y = box.origin.y + box.size.height * 0.5F};
        AURORA_TEST_REQUIRE_MSG(hit(center.x, center.y) == widget, "the computed center is not claimed by the widget");
        return center;
    }

    /// @brief 向当前焦点控件发一段真实文本输入（不经 `set_value()`——那条路不触发 `on_changed`）。
    ///
    /// 框架的 `TextInputEvent` 只投递给 `FocusManager::focused()`，故调用前须先真点击那一格把焦点交过去；
    /// 焦点不在本件上时派发返回 false，用例据此转红而不是静默写进别的控件。
    /// @param text UTF-8 文本片段（逐字符喂入以走真实编辑路径）。
    /// @return 是否被焦点控件消费。
    [[nodiscard]] auto type(std::string_view text) -> bool {
        bool handled = false;
        for (const char ch : text) {
            au::TextInputEvent event;
            event.text = std::string(1U, ch);
            handled = au::EventDispatcher::dispatch(root_.widget(), event, focus_) || handled;
        }
        return handled;
    }

    /// @brief 焦点管理器（回退链的键盘抓取腿要把焦点交给列表本体，与 `src/main.cpp` 派初始焦点同一条入口）。
    [[nodiscard]] auto focus_manager() -> au::FocusManager & { return focus_; }

    /// @brief 向当前焦点控件发一次真实按键（`Down`；键盘重排与文本腿都经这条派发）。
    ///
    /// 走 `EventDispatcher` 而不是直接调控件的 `on_key_event`：后者绕过了「本控件是否认领这一键」那道闸
    /// （`wants_navigation_keys()` / `wants_activation_keys()`），而回退链的键盘腿恰恰由那道闸决定。
    /// @param key 框架键码。
    /// @return 是否被焦点控件消费。
    [[nodiscard]] auto press(au::KeyCode key) -> bool {
        au::KeyEvent event;
        event.key = static_cast<int>(key);
        event.action = au::KeyAction::Down;
        return au::EventDispatcher::dispatch(root_.widget(), event, focus_);
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
    au::ShortcutRegistry shortcuts_;
    aurora::EventDispatcher dispatcher_;  ///< 本驱动台私有的连击判定与指针捕获状态。
};

/// 回退链那一行的三种按钮，按本件交出的指针取（不是按坐标取：坐标要经区段高度与滚动偏移两处换算）。
enum class ChainButton {
    Up,
    Down,
    Remove,
};

/// @brief 把链区段的按钮送进派发可见带，并交回「真实派发命中它」的那一点与可达框。
///
/// 外观页在 #114 之后被主题卡与 16 格色板两个区段把通用行整体下推，回退链排在两者之后，故 offset 0 的
/// 可见带里根本没有它（`a_row_control_still_commits_after_the_row_area_has_been_scrolled` 在同一现象下
/// 写过一条滚轮腿）。**滚到哪一档不是判据**，所以这里每滚一段就重新做一次真实派发扫描，命中即止；
/// 一次都扫不到时返回空 `HitSpot`，由调用点的 `REQUIRE(spot.widget != nullptr)` 转红。
/// 一轮 12 格（无头环境一格实测 16 dp ⇒ 192 dp）而上限 20 轮：七条条目时过滤框已深到约 1300 dp，
/// 原来五轮（960 dp）就够不着了，而步长不会跳过目标——可见带 506 dp、链区段里最小的一件控件也有 32 dp，
/// 二者之和远大于 192 dp。
/// @param h 驱动台。
/// @param panel 面板（按钮指针表在每次结构变更后重建，故按当前值现取）。
/// @param which 取哪一档按钮。
/// @param index 条目序号（与链内次序同序）。
/// @return 该按钮的命中点与窗口坐标下的可达框；未找到时 `widget` 为空。
[[nodiscard]] auto reveal_chain_button(Harness &h, SettingsPanel &panel, ChainButton which, std::size_t index)
    -> HitSpot {
    HitSpot spot;
    for (int attempt = 0; attempt < 20 && spot.widget == nullptr; ++attempt) {
        au::Widget *const want = which == ChainButton::Up ? panel.chain_up_button(index)
                                   : which == ChainButton::Down ? panel.chain_down_button(index)
                                                                : panel.chain_remove_button(index);
        if (want != nullptr) {
            spot = h.find_first("Button", 0.0F, [want](au::Widget *widget) -> bool { return widget == want; });
        }
        if (spot.widget == nullptr) {
            for (int notch = 0; notch < 12; ++notch) {
                h.scroll(450.0F, 300.0F, -1.0F);  // 负方向是往下滚（`ScrollViewport` 的符号约定）
            }
            h.render();
        }
    }
    return spot;
}

/// @brief 链区段的候选池按钮：同样要先把它送进可见带（池在列表之下，比按钮那一段更靠下）。
[[nodiscard]] auto reveal_chain_candidate(Harness &h, SettingsPanel &panel, std::size_t slot) -> HitSpot {
    HitSpot spot;
    for (int attempt = 0; attempt < 20 && spot.widget == nullptr; ++attempt) {
        au::Widget *const want = panel.chain_candidate(slot);
        if (want != nullptr) {
            spot = h.find_first("Button", 0.0F, [want](au::Widget *widget) -> bool { return widget == want; });
        }
        if (spot.widget == nullptr) {
            for (int notch = 0; notch < 12; ++notch) {
                h.scroll(450.0F, 300.0F, -1.0F);
            }
            h.render();
        }
    }
    return spot;
}

/// @brief 把链区段的过滤框送进可见带并交回那一点（过滤框在列表之左下一档，滚动量与按钮那一段同源）。
[[nodiscard]] auto reveal_chain_filter(Harness &h, SettingsPanel &panel) -> HitSpot {
    HitSpot spot;
    for (int attempt = 0; attempt < 20 && spot.widget == nullptr; ++attempt) {
        au::Widget *const want = panel.chain_filter_input();
        if (want != nullptr) {
            spot = h.find_first("TextInput", 0.0F, [want](au::Widget *widget) -> bool { return widget == want; });
        }
        if (spot.widget == nullptr) {
            for (int notch = 0; notch < 12; ++notch) {
                h.scroll(450.0F, 300.0F, -1.0F);
            }
            h.render();
        }
    }
    return spot;
}

/// 预览夹具里那三格 16 色基本色**底色**档的落点（`src/ui/settings_preview.cpp` 的 `build_fixture()` 第 4
/// 行：`└──────┘` 之后依次是 `ESC[41m`（`basic[1]`）、`42m`（`basic[2]`）、`44m`（`basic[4]`））。
/// 判据① 要的是「同一格色逐位变化」，而底色格是实心填充、格内无字形，像素可逐位指认，故选它而不是
/// 前景色档（前景是 AA 灰度文本，只能断「有墨/无墨」）。行号与列号在此写成常量并与夹具同源注释锁死。
constexpr std::size_t kFixtureSwatchRow = 3U;
constexpr std::size_t kFixtureSwatchColumn = 8U;
constexpr std::size_t kFixtureSwatchSlot = 1U;  ///< `basic[1]`：夹具里第一格底色所用的色板槽位。

/// 横条高度（判据文 F-e，人已拍板的落位）。**刻意取字面量而不是 `settings_panel.cpp` 的
/// `kPreviewHeightDp`**：那是实现自己的输出，拿它当预期就只剩「常量与常量相等」；本件判的是
/// 「卡片底部那一条 140 dp」这条界面上的量，故在这里独立写一遍。
constexpr float kPreviewBarHeightDp = 140.0F;

/// 对照视口的矩形（§8 判据① 的第二台预览）：宽 400 高 200 dp 是刻意与横条**不同**的两个数——
/// 两台视口的行列数因此必然不同，而「同一格色」仍须逐位相等，这才判得到「同源」而不是「同尺寸」。
constexpr float kControlWidthDp = 400.0F;
constexpr float kControlHeightDp = 200.0F;

/// @brief 按视口自己的网格几何把 (行, 列) 折回**窗口坐标**下的格心。
///
/// 取点的基准是派发量出来的可达框（不是 `paint_bounds()`，行区是 `Scroll` 之后那套 bounds 属内容坐标，
/// 见 `reachable_box` 的注），而 G31 回货之后链上报的 origin 与绘制原点逐位相符，故可达框即该视口的
/// 绘制矩形。内边距按 `ui::GridGeometry` 的既有字段**只入原点、不入步长**（裁决 7.25②）。无头 `scale`
/// 恒 1.0，故 dp 直接当帧缓冲下标用。
/// @param spot 该视口的命中点与可达框。
/// @param view 该视口本体（取其当前那份几何）。
/// @param row 网格行号（0 基，即视口内的屏幕行）。
/// @param column 网格列号（0 基）。
/// @return 格心（窗口坐标）。
[[nodiscard]] auto cell_center(const HitSpot &spot, const borealis::ui::TerminalView &view, std::size_t row,
                               std::size_t column) -> au::Point {
    const borealis::ui::GridGeometry &geometry = view.grid_geometry();
    return au::Point{
        .x = spot.box.origin.x + static_cast<float>(geometry.padding + (static_cast<double>(column) + 0.5) * geometry.cell_width),
        .y = spot.box.origin.y + static_cast<float>(geometry.padding + (static_cast<double>(row) + 0.5) * geometry.cell_height),
    };
}

/// @brief 判「按这块矩形只装得下整数格」：行列数恰是**装得下的最大整数**，再多一格就溢出。
///
/// 判据文 §8 判据② 那句「不出现半格」的可执行形态，且它是 F-d（预览行列数由自身矩形派生）的正面证人：
/// 行数若是按别的矩形（卡片高、窗口高）算出来的，这两条不等式就会在横条上破。容差 2 dp 只用于吸收
/// `reachable_box` 的 1 dp 步进量化；一格高约 19 dp @14 pt，差一整档远在容差之外，故鉴别力不被放宽。
/// @param box 该视口的可达框（窗口坐标）。
/// @param geometry 该视口当前的网格几何。
/// @param tag 失败信息里的轴名。
/// @return 两条不等式都成立。
[[nodiscard]] auto fits_whole_cells(const au::Rect &box, const borealis::ui::GridGeometry &geometry,
                                    std::string_view tag) -> bool {
    const double room_width = box.size.width - 2.0 * geometry.padding;
    const double room_height = box.size.height - 2.0 * geometry.padding;
    const double rows = static_cast<double>(geometry.rows);
    const double columns = static_cast<double>(geometry.columns);
    const bool ok = rows * geometry.cell_height <= room_height + 2.0 &&
        (rows + 1.0) * geometry.cell_height > room_height - 2.0 && columns * geometry.cell_width <= room_width + 2.0 &&
        (columns + 1.0) * geometry.cell_width > room_width - 2.0;
    if (!ok) {
        AURORA_TEST_TRACE(std::string{tag} + ": " + std::to_string(box.size.width) + "x" +
                          std::to_string(box.size.height) + " / " + std::to_string(geometry.columns) + "x" +
                          std::to_string(geometry.rows) + " / " + std::to_string(geometry.cell_width) + "x" +
                          std::to_string(geometry.cell_height) + " / pad " + std::to_string(geometry.padding));
    }
    return ok;
}

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

    // 排除常驻的色板编辑器：它也是一只 `TextInput`，且排在前景色那行之前，不排掉就探到了它。
    const HitSpot spot = h.find_first("TextInput", 0.0F, [&panel](au::Widget *widget) -> bool {
        return widget != panel->swatch_input();
    });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    auto *box = dynamic_cast<au::TextInput *>(spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    // 认行：外观页第一个「非编辑器」文本框是「默认前景色」，初值就是那份配置的 HEX。
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

/// @brief G30 的回货复验（正向）：面板伸出**所在行之外**的那一段进得了派发链。
///
/// 本例曾是「G29 的回货只闭合一层」的钉子（裁决 7.59⑤ 在册，并明写「追加盒沿祖先链并成子树并集回货后
/// 本例必须转红」）。回货形态是 `covers_extra_hit_box()` 从「只问直接子自己申报」升级为**子树聚合**：
/// 自身那份申报 ∪ 逐子节点按 `bounds().origin` 折算后递归问同一入口，而折算式与 `on_hit_test_chain` 的
/// 下降式逐字同构。本件的嵌套是 `Scroll → Column → Row → Dropdown`，登记时门就断在 `Column` / `Row` 那两层
/// 不申报（控件说自己可达、祖先说不可达），故面板只有仍落在行盒内的那一段可达。聚合之后孙辈的申报随祖先
/// 上传，那一段因此可达——本例判的正是这一条，而且**只判这一条**。
///
/// 为什么把「真实单击即提交」那一半留给下一条用例而不是写在本例里：回货闭合的是「可达」，那一点上的点击
/// 落进哪一条选项带归**派发本地化**管，而后者当时撞出一处新病灶（附录 A.2 的 **G31**：祖先的内边距平移
/// 没进 `HitNode.origin`）。两件事各自有证人，合成一例就会在 G31 回货时不知道该翻哪一半——现在它回货了，
/// 于是本例翻的是「行的记录 origin 与子节点绘制顶同源」那一句，下一例翻的是「真点即选中所点那一档」。
///
/// 三条前提照旧逐条钉住，只是第三条换了方向：① 控件自己申报覆盖；② 探点既在自身布局盒**之外**、也在
/// **所在行的可达框之外**（行高 56 dp、上下内边距各 8 dp；仍属本行的那一段由上一条 G29 用例守，拿它翻
/// 正向等于什么都没翻）；③ 探点在**视口**之内——`Scroll` 的聚合覆写带视口钳位，视口外那段的不可达测的是
/// 裁剪而不是祖先链，那种红/绿都算假。回货同批给 `Dropdown` 加了限高 / 翻转 / 滚动，本行两档共 52 dp 远在
/// 视口高之内，故本例判的仍是「那一段可达」而不是滚动偏移。
AURORA_TEST_CASE(the_option_panel_below_the_enclosing_row_now_reaches_the_dispatch_chain_G30) {
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
    AURORA_TEST_REQUIRE_EQ(dropdown->selected_index(), 1);

    // 闭态先量出所在行：沿派发链从最深一格往回找最近的一个 `Row`（行区的嵌套是 `Scroll → Column → Row`）。
    // 取它的记录 origin 当**行给子节点的内容顶**。G31 回货后这一读数与绘制同源（`content_origin`＝
    // bounds origin + 修饰链平移，行那 8 dp 上内边距已经折进去），于是它与下拉的绘制顶**逐字相等**；
    // 登记时两者差的恰是那份内边距，故下面那两条断言在回货前后各成立一侧。
    const std::vector<au::HitNode> closed = h.chain_at(spot.x, spot.y);
    const au::HitNode *row_node = nullptr;
    for (std::size_t i = closed.size(); i-- > 1;) {
        if (std::string_view{closed[i].ptr->type_name()} == std::string_view{"Row"}) {
            row_node = &closed[i];
            break;
        }
    }
    AURORA_TEST_REQUIRE(row_node != nullptr);
    const float row_top = row_node->origin.y;

    h.click(spot.x, spot.y);
    h.render();
    AURORA_TEST_REQUIRE(dropdown->is_open());

    const au::Rect layout = dropdown->paint_bounds();  // 只取它的**尺寸**：那是控件自身坐标空间里的量
    const float option_x = spot.box.origin.x + layout.size.width * 0.5F;
    const au::Point local{.x = layout.size.width * 0.5F, .y = layout.size.height + 12.0F};
    const float option_y = spot.box.origin.y + local.y;
    const au::BuildContext ctx{};

    AURORA_TEST_REQUIRE(std::abs(spot.box.size.height - layout.size.height) <= 1.0F);
    // 行的记录 origin ＝ 下拉的绘制顶：行的 8 dp 上内边距已经折进 `HitNode.origin`（G31 的回货形态）。
    // 登记时这一句差 8 dp，而那 8 dp 正是 G31 的病灶量；两侧的断言各自只能成立一侧，故本例在回货前红、
    // 回货后绿，不是同义反复。
    AURORA_TEST_REQUIRE_MSG(std::abs(spot.box.origin.y - row_top) <= 1.0F,
                            "the enclosing row's recorded origin is not its content top");
    // 反空转：探点在视口之内（视口下沿＝卡片下沿＝窗口下沿减 `kCardEdgeDp`）。
    AURORA_TEST_REQUIRE_MSG(option_y < static_cast<float>(kWindowHeight) - kCardEdgeDp,
                            "the probe point is outside the scroll viewport");
    // 反空转：探点确在**所在行之外**——少了这一句，本例就退化成重复上一条 G29 用例守的那一段。行给子项的
    // 内容高是 56 − 2×8 ＝ 40 dp，而 `row_top` 如今就是内容顶，故行外界的算式随同源这条一起就地更正。
    AURORA_TEST_REQUIRE_MSG(option_y > row_top + kRowExtentDp - 2.0F * kRowPaddingDp,
                            "the probe point is still inside the enclosing row");
    AURORA_TEST_REQUIRE_MSG(dropdown->covers_extra_hit_box(local, ctx), "the open panel does not cover the probed point");
    AURORA_TEST_REQUIRE_MSG(local.y >= layout.size.height, "the probed point is inside the widget's own layout box");

    // 回货前后唯一翻转的那一句：登记时按链走场景根拿到的是祖先（行），回货后是这只下拉自己。
    AURORA_TEST_REQUIRE_MSG(h.hit(option_x, option_y) == static_cast<au::Widget *>(dropdown),
                            "the extra hit box still does not propagate up the ancestor chain");
    // 而且是以「祖先开闸 + 自身入链」两段的合取进来的：链上仍留着那一行作为它的祖先。
    const std::vector<au::HitNode> open = h.chain_at(option_x, option_y);
    AURORA_TEST_REQUIRE_EQ(open.back().ptr, static_cast<au::Widget *>(dropdown));
    AURORA_TEST_REQUIRE(std::any_of(open.begin(), open.end(),
                                    [row_node](const au::HitNode &node) { return node.ptr == row_node->ptr; }));
    // 面板未被限高/翻转（回货同批新增的两条）：本行两档共 52 dp 远在一个 506 dp 的视口之内。
    AURORA_TEST_REQUIRE_EQ(dropdown->selected_index(), 1);
    AURORA_TEST_CHECK_TRUE(dropdown->is_open());
}

/// @brief G31 的回货复验（正向）：行外那一段上的真实单击，选中的是**眼睛看到的那一档**。
///
/// 本例曾是现状钉子（裁决 7.62⑤ 在册，并明写「回货后差值归零、本例转红，正半随 G30 那条一起翻」）。
/// 回货形态是命中链与绘制**共用同一个 `content_origin`**：`Widget::hit_test` / `hit_test_chain` 的
/// `self_box.origin` 取 `bounds.origin + tf.translation`，而 `render_into` 给子节点的正是这同一份平移量，
/// `Modifier::transform` 把 Padding / PaddingEdges / Align / Offset 一律折进 `translation`（框架据此禁止
/// 命中侧再写一份「要不要加内边距」）。
///
/// 判据的承重那一句是**选项序号**而不是「点得动」——可达由上一条用例守。`Dropdown` 按派发器写来的本地纵
/// 坐标反算序号，本行的量值是算好的：行给下拉的紧约束盒 40 dp、面板从框架自己的 `box_height_` ＝ 30 dp
/// 起铺、选项行高 26 dp ⇒ 第一条带占窗口坐标 [绘制顶 + 30, 绘制顶 + 56)。探点取 `绘制顶 + 40 + 12`，即
/// 第一条带内距其下沿 4 dp 处。若 origin 仍缺行那 8 dp 上内边距，控件收到的本地纵坐标就是 60 而落进第二条
/// 带 [56, 82)，而第二条带恰是**当前已选**的那档（`Wide`）——于是「少一份平移」的实现在本例里表现为
/// 序号一动不动、面板不收起、也不落盘，与回货后的读数结构上不可能同时为绿。这就是本例的非空转根据。
///
/// 行为那一半判四条：真点之后 ① 选中的是第一条（`narrow`）、② 面板收起、③ 恰一次落盘且**零广播**
/// （该键 `Wired ∧ NextSession`，判据文 §4 B3-a 的两条标签不冲突）、④ 表单读回 `narrow` 且脏标记随落盘
/// 成功推进。origin 与绘制顶同源另判一句，且以**真实派发量出来的可达框**为基准而不是取实现的输出。
AURORA_TEST_CASE(a_real_click_below_the_enclosing_row_picks_the_option_under_it_G31) {
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
    AURORA_TEST_REQUIRE_EQ(dropdown->selected_index(), 1);

    h.click(spot.x, spot.y);
    h.render();
    AURORA_TEST_REQUIRE(dropdown->is_open());

    const au::Rect layout = dropdown->paint_bounds();  // 只取它的**尺寸**：那是控件自身坐标空间里的量
    const float option_x = spot.box.origin.x + layout.size.width * 0.5F;
    const au::Point local{.x = layout.size.width * 0.5F, .y = layout.size.height + 12.0F};
    const float option_y = spot.box.origin.y + local.y;
    const au::BuildContext ctx{};

    // 三条前提：可达、在自身布局盒之外、且申报覆盖为真（缺任何一条，下面的单击就测不到「行外那一段」）。
    AURORA_TEST_REQUIRE_MSG(local.y >= layout.size.height, "the probed point is inside the widget's own layout box");
    AURORA_TEST_REQUIRE_MSG(dropdown->covers_extra_hit_box(local, ctx), "the open panel does not cover the probed point");
    const std::vector<au::HitNode> open = h.chain_at(option_x, option_y);
    AURORA_TEST_REQUIRE_EQ(open.back().ptr, static_cast<au::Widget *>(dropdown));
    // 同源的那一句：链上写的 origin 与真实派发量出来的绘制顶一致（登记时差 8 dp，即行的上内边距）。
    AURORA_TEST_CHECK_NEAR(open.back().origin.y, spot.box.origin.y, 1.0F);

    h.click(option_x, option_y);
    h.render();
    AURORA_TEST_CHECK_EQ(dropdown->selected_index(), 0);
    AURORA_TEST_CHECK_FALSE(dropdown->is_open());
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    AURORA_TEST_CHECK_TRUE(probe.persisted[0].terminal.ambiguous_width == borealis::term::AmbiguousWidth::Narrow);
    AURORA_TEST_REQUIRE(panel->form().value("terminal.ambiguous_width") != nullptr);
    AURORA_TEST_CHECK_TRUE(*panel->form().value("terminal.ambiguous_width")->as_text() == std::string{"narrow"});
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());
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
///
/// 滚轮落点取终端页第 0 行那枚步进器：外观页在 #114 之后被两个区段（主题卡 + 16 格色板）把通用控件行整体
/// 下推，offset 0 的可见带里根本没有开关，而本例要的开关在终端页只往下滚几档就在带内。落点本身要的是
/// 「确实落在行区内」这一条，故由 `row_scroll()` 在派发链上现问而不是猜一个坐标。
AURORA_TEST_CASE(a_row_control_still_commits_after_the_row_area_has_been_scrolled) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);
    panel->select_page(SettingsPage::Terminal);
    h.render();

    const HitSpot anchor = h.find_first("SpinBox");
    AURORA_TEST_REQUIRE(anchor.widget != nullptr);
    au::Scroll *area = h.row_scroll(anchor.x, anchor.y);
    AURORA_TEST_REQUIRE(area != nullptr);
    AURORA_TEST_REQUIRE_EQ(area->offset_y(), 0.0F);

    // 先把开关送进可见带（八档 × 16 dp ＝ 内容上移 128 dp），再按本例的判据往下走。
    for (int notch = 0; notch < 8; ++notch) {
        h.scroll(anchor.x, anchor.y, -1.0F);
    }
    h.render();
    const HitSpot spot = h.find_first(
        "Switch", 0.0F, [](au::Widget *widget) -> bool {
            auto *sw = dynamic_cast<au::Switch *>(widget);
            return sw != nullptr && !sw->value();
        });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);

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
/// 判它会把正确实现读成红）；步进器与开关两腿放在**终端页**，因为外观页 #114 的两个区段把第 8 行以后的
/// 通用控件推出了可见带，而终端页第一行就是步进器。开关在那一页也排在第 8 行之后，故先真滚若干档再探——
/// 判据只认「滚完之后确实命中一枚关闭态开关」，不去赌排版算出来的偏移。
AURORA_TEST_CASE(the_editable_controls_paint_the_chrome_colors_not_the_light_defaults) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    // 文本行：未聚焦与聚焦两态都得是深色底（框架的浅色缺省正是聚焦那一态最亮）。
    // 排除常驻的色板编辑器：它同属「本件交出 chrome」的那一处，但判据要认行，故取前景色那一框。
    const HitSpot text_spot = h.find_first("TextInput", 0.0F, [&panel](au::Widget *widget) -> bool {
        return widget != panel->swatch_input();
    });
    AURORA_TEST_REQUIRE(text_spot.widget != nullptr);
    auto *box = dynamic_cast<au::TextInput *>(text_spot.widget);
    AURORA_TEST_REQUIRE(box != nullptr);
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(text_spot, 0.9, 0.15)));
    h.click(text_spot.x, text_spot.y);
    h.render();
    // 没真的聚焦上就根本没走聚焦态那条绘制分支，这一句是本例的前提而不是附带观察。
    AURORA_TEST_REQUIRE(box->is_focused());
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(text_spot, 0.9, 0.15)));

    panel->select_page(SettingsPage::Terminal);
    h.render();

    // 步进器：框底（数值文本从 y=8 起、箭头区在右侧 22 dp 之内，故取盒内靠上的中部）。
    const HitSpot spin_spot = h.find_first("SpinBox");
    AURORA_TEST_REQUIRE(spin_spot.widget != nullptr);
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(spin_spot, 0.5, 0.12)));

    // 开关关闭态轨道：缺省 {180,180,180} 是全部浅色缺省里最低的一档，仍必须被这条线抓住。
    // 往下滚 8 档（框架缺省 `step` 16 dp ＝ 内容上移 128 dp）把终端页第 8 行那四枚开关送进可见带。
    au::Scroll *area = h.row_scroll(spin_spot.x, spin_spot.y);
    AURORA_TEST_REQUIRE(area != nullptr);
    for (int notch = 0; notch < 8; ++notch) {
        h.scroll(spin_spot.x, spin_spot.y, -1.0F);
    }
    h.render();
    AURORA_TEST_CHECK_GT(area->offset_y(), 0.0F);
    const HitSpot toggle_spot = h.find_first(
        "Switch", 0.0F, [](au::Widget *widget) -> bool {
            auto *sw = dynamic_cast<au::Switch *>(widget);
            return sw != nullptr && !sw->value();
        });
    AURORA_TEST_REQUIRE(toggle_spot.widget != nullptr);
    AURORA_TEST_CHECK_EQ(static_cast<int>(toggle_spot.widget->paint_bounds().size.height), 24);
    AURORA_TEST_CHECK_TRUE(chrome_is_dark(h.probe(toggle_spot, 0.8, 0.5)));

    // 下拉主框（选项面板与主框共用同一份 `box_color_`，故主框这一读也守住了展开的那一片）。
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

/// @brief 判据 S6 / A1-a 的点击腿：真点一张卡就把那一套的色值整份写进表单，一次落盘 + 一次即时广播。
///
/// 切主题落 **6 个键**（`appearance.theme` + 16 格 + 前景 / 背景 / 光标 / 选区），而三枚用户开关
/// （`bold_is_bright` / `min_contrast_enabled` / `min_contrast`）**不由主题派生**（`config/themes.cpp`
/// 一套都不带），故它们必须一字不动。本例因此同时守两头：把切主题实现成「逐键提交六次」就是六次写文件，
/// 实现成「连开关一起换」就抹掉了用户自己的选择。
///
/// 落点是卡上的画布（外层 `Stack` 收点击，派发 deepest→root 冒泡到它），故点样例格与点卡名一样生效。
AURORA_TEST_CASE(clicking_a_theme_card_writes_six_keys_persists_once_and_broadcasts_once) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    const auto themes = borealis::config::builtin_themes();
    AURORA_TEST_REQUIRE(themes.size() > 1U);
    const std::size_t target = 1U;  // 缺省主题居首（dracula），第二张就是「换一套」的那个动作
    au::Widget *card = panel->theme_card(target);
    AURORA_TEST_REQUIRE(card != nullptr);
    const HitSpot spot = h.find_first("Canvas", 0.0F, [card](au::Widget *widget) -> bool {
        return widget == card;
    });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    const float center_x = spot.box.origin.x + spot.box.size.width * 0.5F;
    const float center_y = spot.box.origin.y + spot.box.size.height * 0.5F;
    AURORA_TEST_REQUIRE_MSG(h.hit(center_x, center_y) == card,
                            "the measured card center is not dispatch-reachable");

    // 三枚开关的**点前快照**：`StoreProbe::base` 会在成功落盘后推进（那就是存储侧的当前值），
    // 所以拿点后的 `probe.base` 比等于让实现自己出题——「切主题连开关一起改」那种实现会把两边一起改掉，
    // 判据就此空转。快照取在点击之前。
    const bool bold_before = probe.base.appearance.palette.bold_is_bright;
    const bool contrast_before = probe.base.appearance.palette.min_contrast_enabled;
    const double threshold_before = probe.base.appearance.palette.min_contrast;

    const std::size_t persisted_before = probe.persist_calls;
    const std::size_t broadcast_before = probe.broadcast_calls;
    h.click(center_x, center_y);
    h.render();

    const borealis::ui::PaletteSpec &next = themes[target].palette;
    AURORA_TEST_REQUIRE(panel->form().value("appearance.theme") != nullptr);
    AURORA_TEST_CHECK_EQ(*panel->form().value("appearance.theme")->as_text(), std::string{themes[target].name});
    const auto table = panel->form().value("appearance.palette.basic")->as_color_table();
    AURORA_TEST_REQUIRE(table.has_value());
    AURORA_TEST_REQUIRE_EQ(table->size(), next.basic.size());
    for (std::size_t slot = 0; slot < table->size(); ++slot) {
        AURORA_TEST_CHECK_EQ((*table)[slot], next.basic[slot]);
    }
    AURORA_TEST_CHECK_TRUE(*panel->form().value("appearance.palette.foreground") ==
                           FormValue::color(next.default_foreground));
    AURORA_TEST_CHECK_TRUE(*panel->form().value("appearance.palette.background") ==
                           FormValue::color(next.default_background));
    // 可缺省的两格按「未配 / 配了」两态比，而不是把未配折成某个色（A2-b 的同一口径）。
    const auto expect_slot = [](const std::optional<RgbaColor> &color) -> FormValue {
        return color.has_value() ? FormValue::color(*color) : FormValue::unset_color();
    };
    AURORA_TEST_CHECK_TRUE(*panel->form().value("appearance.palette.cursor") == expect_slot(next.cursor_color));
    AURORA_TEST_CHECK_TRUE(*panel->form().value("appearance.palette.selection") ==
                           expect_slot(next.selection_color));

    // 三枚用户开关一字未动：它们不由主题派生（对比的是点前快照）。
    AURORA_TEST_CHECK_TRUE(*panel->form().value("appearance.palette.bold_is_bright") ==
                           FormValue::boolean(bold_before));
    AURORA_TEST_CHECK_TRUE(*panel->form().value("appearance.palette.min_contrast_enabled") ==
                           FormValue::boolean(contrast_before));
    AURORA_TEST_CHECK_TRUE(*panel->form().value("appearance.palette.min_contrast") ==
                           FormValue::real(threshold_before));

    // 一次落盘 + 一次广播（逐键提交会写成 6 次，那是本例要抓的形态）。
    AURORA_TEST_CHECK_EQ(probe.persist_calls, persisted_before + 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, broadcast_before + 1U);
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());
    AURORA_TEST_CHECK_FALSE(panel->is_palette_customized());  // 换到预置套即回到「非自定义」
    // 编辑器的「当前值」跟着整份换色：它指着 0 格，而 0 格刚被换成新主题的默认色。
    auto *editor = dynamic_cast<au::TextInput *>(panel->swatch_input());
    AURORA_TEST_REQUIRE(editor != nullptr);
    AURORA_TEST_CHECK_EQ(editor->value(), borealis::ui::color_to_hex(next.basic[0]));
    const auto cards = panel->theme_cards();
    AURORA_TEST_REQUIRE_EQ(cards.size(), themes.size());  // 次序与张数都没变
    std::size_t selected = 0;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].selected) {
            ++selected;
            AURORA_TEST_CHECK_EQ(i, target);
        }
    }
    AURORA_TEST_CHECK_EQ(selected, 1U);
}

/// @brief 判据 A2-a：点一格 → 那只常驻编辑器换指向并给出该格当前值；失焦那一刻只改那一格。
///
/// 判据文明令禁止「16 个常驻输入框」，本件只有一只编辑器，代价是「当前值」与输入框共用同一控件
/// （裁决 7.61③：`Modifier` 没有可见性位，按创建 / 销毁表达「换指向」要整块重建浮层，会抹掉滚动偏移与
/// 输入焦点）。于是后半条判据必须**逐格比**落盘内容：其余 15 格一字不动才是「点一格改一格」。
AURORA_TEST_CASE(clicking_a_swatch_slot_repoints_the_editor_and_blur_commits_only_that_slot) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_REQUIRE_EQ(panel->selected_swatch(), 0U);
    auto *editor = dynamic_cast<au::TextInput *>(panel->swatch_input());
    AURORA_TEST_REQUIRE(editor != nullptr);
    const auto table = panel->form().value("appearance.palette.basic")->as_color_table();
    AURORA_TEST_REQUIRE(table.has_value());
    const std::vector<RgbaColor> before = *table;
    AURORA_TEST_CHECK_EQ(editor->value(), borealis::ui::color_to_hex(before[0]));

    au::Widget *tile = panel->swatch_slot(3U);
    AURORA_TEST_REQUIRE(tile != nullptr);
    const HitSpot spot = h.find_first("Canvas", 0.0F, [tile](au::Widget *widget) -> bool {
        return widget == tile;
    });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    h.click(spot.box.origin.x + spot.box.size.width * 0.5F, spot.box.origin.y + spot.box.size.height * 0.5F);
    h.render();

    AURORA_TEST_CHECK_EQ(panel->selected_swatch(), 3U);
    AURORA_TEST_CHECK_EQ(editor->value(), borealis::ui::color_to_hex(before[3]));
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);  // 点一格是换指向，不是提交

    editor->set_value("#0A0B0C");
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);  // 逐字符不提交（S14 同一条口径）
    editor->on_focus_change(false);
    h.render();

    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    const auto &written = probe.persisted[0].appearance.palette.basic;
    AURORA_TEST_REQUIRE_EQ(written.size(), before.size());
    for (std::size_t slot = 0; slot < written.size(); ++slot) {
        AURORA_TEST_CHECK_EQ(written[slot], slot == 3U ? borealis::ui::color_from_hex("#0A0B0C").value()
                                                       : before[slot]);
    }
}

/// @brief S6① 的逐档恢复：改过选中格之后按「恢复主题默认」只把那一格换回基线色。
AURORA_TEST_CASE(the_reset_button_restores_only_the_selected_slot) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    const auto table = panel->form().value("appearance.palette.basic")->as_color_table();
    AURORA_TEST_REQUIRE(table.has_value());
    // 先**真点**第 5 格把编辑器指过去，再改那一格：编辑器的缺省指向是 0 格，若不先移开，
    // 「恢复错格」（把 0 格当成选中格恢复）这类实现在本例里结构与不可见——恢复 0 格恰好也是那一格。
    au::Widget *tile = panel->swatch_slot(5U);
    AURORA_TEST_REQUIRE(tile != nullptr);
    const HitSpot tile_spot = h.find_first("Canvas", 0.0F, [tile](au::Widget *widget) -> bool {
        return widget == tile;
    });
    AURORA_TEST_REQUIRE(tile_spot.widget != nullptr);
    h.click(tile_spot.box.origin.x + tile_spot.box.size.width * 0.5F,
            tile_spot.box.origin.y + tile_spot.box.size.height * 0.5F);
    h.render();
    const std::size_t slot = panel->selected_swatch();  // 编辑器当前指向的那一格
    AURORA_TEST_REQUIRE_EQ(slot, 5U);
    // 改那一格走**编辑器 + 失焦**这条真实通路，而不是 `commit_slot` 的编程入口：后者不动编辑器的文本，
    // 于是「恢复之后编辑器必须回位」那句判据会被「编辑器一直是基线值」蒙过去（变异自证实测过一次）。
    auto *editor = dynamic_cast<au::TextInput *>(panel->swatch_input());
    AURORA_TEST_REQUIRE(editor != nullptr);
    editor->set_value("#0A0B0C");
    editor->on_focus_change(false);
    h.render();
    AURORA_TEST_REQUIRE(panel->is_palette_customized());
    AURORA_TEST_REQUIRE_EQ(editor->value(), "#0A0B0C");  // 改过之后编辑器里就是那一个新值

    au::Button *reset = nullptr;
    const HitSpot spot = h.find_first("Button", 0.0F, [&reset](au::Widget *widget) -> bool {
        auto *button = dynamic_cast<au::Button *>(widget);
        if (button == nullptr) {
            return false;
        }
        if (button->accessibility_label() != borealis::ui::settings_label("settings.action.theme_default")) {
            return false;
        }
        reset = button;
        return true;
    });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    AURORA_TEST_REQUIRE(reset != nullptr);
    AURORA_TEST_REQUIRE_MSG(reset->wants_click(), "a theme with a baseline must offer the per-slot reset");

    const std::size_t persisted_before = probe.persist_calls;
    h.click(spot.box.origin.x + spot.box.size.width * 0.5F, spot.box.origin.y + spot.box.size.height * 0.5F);
    h.render();

    AURORA_TEST_CHECK_EQ(probe.persist_calls, persisted_before + 1U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 2U);
    const auto &restored = probe.persisted[1].appearance.palette.basic;
    const auto &changed = probe.persisted[0].appearance.palette.basic;
    for (std::size_t i = 0; i < restored.size(); ++i) {
        AURORA_TEST_CHECK_EQ(restored[i], i == slot ? borealis::config::theme_palette(probe.base.appearance.theme)
                                                              .basic[i]
                                                    : changed[i]);
    }
    AURORA_TEST_CHECK_FALSE(panel->is_palette_customized());
    AURORA_TEST_CHECK_TRUE(find_row(panel->visible_rows(), "appearance.theme")->badge.empty());
    // 编辑器的「当前值」跟着回位：恢复的是那一格的值，而那一格正被编辑器指着。
    AURORA_TEST_CHECK_EQ(editor->value(),
                         borealis::ui::color_to_hex(
                             borealis::config::theme_palette(probe.base.appearance.theme).basic[slot]));
}

/// @brief 无基线的现场（当前主题名不在候选表里）：角标照挂，但「恢复主题默认」禁用且点了不落盘。
///
/// 这是 7.38⑥ F-b「不给一个会失灵的按钮」在本件的那一档：框架的禁用态降级绘制并忽略点击
/// （`Button::wants_click()` 即 `enabled && on_click` 那句），故这里既判形态也判既成事实。
AURORA_TEST_CASE(an_unlisted_theme_name_disables_the_reset_button) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    probe.theme_choices.clear();  // 候选表交空＝没有可比基线，无从谈「主题默认」
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_CHECK_TRUE(panel->theme_cards().empty());
    AURORA_TEST_CHECK_TRUE(panel->is_palette_customized());
    AURORA_TEST_CHECK_EQ(find_row(panel->visible_rows(), "appearance.theme")->badge, std::string{kBadgeCustomized});

    au::Button *reset = nullptr;
    const HitSpot spot = h.find_first("Button", 0.0F, [&reset](au::Widget *widget) -> bool {
        auto *button = dynamic_cast<au::Button *>(widget);
        if (button == nullptr) {
            return false;
        }
        if (button->accessibility_label() != borealis::ui::settings_label("settings.action.theme_default")) {
            return false;
        }
        reset = button;
        return true;
    });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    AURORA_TEST_REQUIRE(reset != nullptr);
    AURORA_TEST_CHECK_FALSE(reset->wants_click());

    h.click(spot.box.origin.x + spot.box.size.width * 0.5F, spot.box.origin.y + spot.box.size.height * 0.5F);
    h.render();
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());
}

/// @brief 真点链行的下移 / 上移 / 移除三枚按钮：次序当场改变，且一次结构变更恰落盘一次、恰广播一次。
///
/// 换位判据为什么落在按钮而不在拖拽：无头通道不跑帧泵，框架 `ReorderableList::end_drag()` 只在
/// `reduce_motion` 或位移不足半格时立即结算，指针拖拽的落位因此在本通道不可判（文件头⑩）。按钮与键盘
/// 是两条同步路径，本例走按钮、`escape_releases_...` 那条走键盘。
///
/// 三枚按钮全落在 48 dp 手柄带之左（`set_drag_handle(true)` 让列表把右侧那条带收作自己的起拖区，带内的
/// 落点连条目都拿不到），故这里按**指针身份**取按钮而不是按坐标猜：`reveal_chain_button` 在真实派发上量
/// 出可达框，行区是 `Scroll` 之后内容子节点的 bounds 是内容坐标，拿它当窗口坐标点就会点错一行。
///
/// 首项的上移只判形态（`Button::wants_click()` 即 `enabled && on_click`，禁用者不进命中链，故"真点它"
/// 在结构上不存在——同 `an_unlisted_theme_name_disables_the_reset_button` 的口径，不伪造点击）。
/// 每次结构变更后条目都重建，按钮指针表跟着换序，故下一枚要重新按身份找。
AURORA_TEST_CASE(clicking_the_chain_move_buttons_reorders_and_removes_with_one_commit_each) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    probe.base.appearance.font_fallback_chain = {"Fira Code", "Consolas", "Cascadia Code"};
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    AURORA_TEST_REQUIRE_MSG(panel->chain_list() != nullptr, "the chain section was not built");
    AURORA_TEST_REQUIRE(panel->chain_up_button(0) != nullptr);
    AURORA_TEST_CHECK_FALSE(panel->chain_up_button(0)->wants_click());  // 首项无「上移」
    AURORA_TEST_REQUIRE(panel->chain_down_button(0) != nullptr);
    AURORA_TEST_CHECK_TRUE(panel->chain_down_button(0)->wants_click());

    const HitSpot down_first = reveal_chain_button(h, *panel, ChainButton::Down, 0);
    AURORA_TEST_REQUIRE_MSG(down_first.widget != nullptr, "the chain's down button is not dispatch-reachable");
    h.click(down_first.x, down_first.y);
    h.render();
    check_chain_sequence("after moving the head down", chain_families(panel->chain_view().items),
                         {"Consolas", "Fira Code", "Cascadia Code"});
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);  // 该键 `Wired ∧ Immediate`：一次改链一次广播
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    // 落点归属由写出去的那一份配置说话（次序即回退顺序，故逐位比）。
    check_chain_sequence("what the move wrote", probe.persisted.front().appearance.font_fallback_chain,
                         {"Consolas", "Fira Code", "Cascadia Code"});
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());

    const HitSpot up_second = reveal_chain_button(h, *panel, ChainButton::Up, 1);
    AURORA_TEST_REQUIRE_MSG(up_second.widget != nullptr, "the rebuilt row's up button is not dispatch-reachable");
    h.click(up_second.x, up_second.y);
    h.render();
    check_chain_sequence("moved back", chain_families(panel->chain_view().items),
                         {"Fira Code", "Consolas", "Cascadia Code"});
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 2U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 2U);

    const HitSpot remove_head = reveal_chain_button(h, *panel, ChainButton::Remove, 0);
    AURORA_TEST_REQUIRE_MSG(remove_head.widget != nullptr, "the rebuilt row's remove button is not dispatch-reachable");
    h.click(remove_head.x, remove_head.y);
    h.render();
    check_chain_sequence("after removing the head", chain_families(panel->chain_view().items),
                         {"Consolas", "Cascadia Code"});
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 3U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 3U);
    // 可用性是「序号 × 条目数」的函数：移除之后新末项的下移跟着禁用，而首项的上移仍禁用。
    AURORA_TEST_REQUIRE(panel->chain_down_button(1) != nullptr);
    AURORA_TEST_CHECK_FALSE(panel->chain_down_button(1)->wants_click());
    AURORA_TEST_CHECK_TRUE(panel->chain_view().items[1].can_move_up);
    AURORA_TEST_CHECK_FALSE(panel->chain_view().items[1].can_move_down);
}

/// @brief 过滤框：键入只收窄候选池，内容**永不进表单**；点一条候选才是一次结构变更。
///
/// 三条腿各钉一件事：
/// ① 键入本身既不落盘也不广播（`chain_filter_` 是普通 `TextInput` 而非失焦提交的 `BlurCommitText`——
///    失焦提交会把一串族名当成配置值写进表单，文件头「添加族」段）；
/// ② 已在链内的族名**不再给第二次追加口**（本例的目录里含 `"co"` 的族有三档，池里只有两档，
///    第三档 `Fira Code` 正在链内）；
/// ③ 刚追加成功的那一档随即从池里消失（同一个排除判据的动态形态），于是「过滤非空 ∧ 零档」把提示推到
///    「没有匹配」那一档（与「空链」「已达上限」「已被截断」互异，四档提示的互异性由
///    `the_chain_section_projects_the_form_and_the_hint_tracks_its_length` 判）。
///
/// 键入走真实 `TextInputEvent` 派发（先真点过滤框把焦点交过去），因为框架 `TextInput::set_value()`
/// 不触发 `on_changed`——那条通道写进去本件读不到，判据就成了假的（裁决 7.56 在册的那条边界）。
AURORA_TEST_CASE(typing_in_the_chain_filter_narrows_the_pool_without_touching_the_form) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    probe.base.appearance.font_fallback_chain = {"Fira Code"};
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    const HitSpot filter = reveal_chain_filter(h, *panel);
    AURORA_TEST_REQUIRE_MSG(filter.widget != nullptr, "the chain's filter box is not dispatch-reachable");
    h.click(filter.x, filter.y);  // 焦点交给过滤框（`TextInputEvent` 只投给 focused）
    h.render();
    AURORA_TEST_REQUIRE_MSG(h.type("co"), "the filter box did not take the typed text");
    h.render();

    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());
    const SettingsPanel::ChainView filtered = panel->chain_view();
    check_chain_sequence("the pool narrowed to the catalog's matches outside the chain", filtered.candidates,
                         {"Cascadia Code", "Consolas"});
    check_chain_sequence("typing left the chain alone", chain_families(filtered.items), {"Fira Code"});
    AURORA_TEST_CHECK_TRUE(filtered.hint.empty());  // 有匹配：四档提示都不该亮

    // 再收窄一格：`consol` 只剩 `Consolas` 一档（池宽随输入变窄是本区段的行为，不是框架的）。
    AURORA_TEST_REQUIRE_MSG(h.type("nsol"), "the filter box did not take the second text run");
    h.render();
    check_chain_sequence("the pool narrowed again", panel->chain_view().candidates, {"Consolas"});
    AURORA_TEST_CHECK_TRUE(panel->chain_view().hint.empty());
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);

    // 点第一条：这一次才是结构变更（落盘 + 广播各一次）。
    const HitSpot candidate = reveal_chain_candidate(h, *panel, 0);
    AURORA_TEST_REQUIRE_MSG(candidate.widget != nullptr, "the candidate button is not dispatch-reachable");
    auto *candidate_button = dynamic_cast<au::Button *>(candidate.widget);
    AURORA_TEST_REQUIRE(candidate_button != nullptr);
    AURORA_TEST_CHECK_TRUE(candidate_button->wants_click());
    h.click(candidate.x, candidate.y);
    h.render();
    check_chain_sequence("appending the picked candidate", chain_families(panel->chain_view().items),
                         {"Fira Code", "Consolas"});
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);
    AURORA_TEST_REQUIRE_EQ(probe.persisted.size(), 1U);
    check_chain_sequence("what the append wrote", probe.persisted.front().appearance.font_fallback_chain,
                         {"Fira Code", "Consolas"});

    // 刚追加的那一档随即从池里消失（② 那条排除腿的动态形态），于是过滤非空而零档 ⇒ 提示换到「没有匹配」。
    // 用户面对空池子时得能分辨「没筛中」与「链已满」，四档提示因此互异。
    AURORA_TEST_CHECK_TRUE(panel->chain_view().candidates.empty());
    AURORA_TEST_CHECK_EQ(panel->chain_view().hint, borealis::ui::settings_label("settings.chain.no_match"));
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);  // 提示换档不是第二次提交
}

/// @brief 追加口在链长到达框架上限那一档关死：上限之下一格点得动，恰达上限时同一档真点既不落盘也不广播。
///
/// 判据写成**一对**而不是各判一半，是因为「达上限时点不动」单独一条有两条假绿路径：池子被算成空（本例
/// 先判 `candidates` 仍列出那一档），或压根没滚到那一段（本例先在上限之下真点成功过一次）。
/// 上限之那一格真点生效后，`Consolas` 进了链因而从池里消失（② 那条排除腿），池子剩下两档且**仍列出**
/// ——只是禁用。一条口径在此更正（登记时本例写作「禁用态的按钮按 `wants_click()` 就不进命中链」，实测不
/// 成立）：`Button` 没有覆盖任何命中入口，框架只在派发那一步按 `wants_click()` 分档，故禁用按钮照样在链
/// 里、照样扫得到。于是「点了不动」的证人只能是**真点一次**并判它既不落盘也不广播，另加一条「浮层还在」
/// ——点击若被遮罩层接走就会关掉面板，那一档计数同样不变，是一条假绿路径。
AURORA_TEST_CASE(the_candidate_append_is_open_until_the_chain_reaches_the_framework_capacity) {
    borealis::ui::install_settings_strings();
    Harness h;
    StoreProbe probe;
    std::vector<std::string> nearly_full;
    for (std::size_t i = 0; i + 1U < kChainCapacity; ++i) {
        nearly_full.push_back("Family " + std::to_string(i));
    }
    probe.base.appearance.font_fallback_chain = nearly_full;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    const HitSpot filter = reveal_chain_filter(h, *panel);
    AURORA_TEST_REQUIRE_MSG(filter.widget != nullptr, "the chain's filter box is not dispatch-reachable");
    h.click(filter.x, filter.y);
    h.render();
    AURORA_TEST_REQUIRE_MSG(h.type("co"), "the filter box did not take the typed text");
    h.render();
    // 目录里含 "co" 的三档都不在链上（链上叫 `Family i`），故池里三档、上限之下全部可点。
    check_chain_sequence("the pool below the cap", panel->chain_view().candidates,
                         {"Cascadia Code", "Consolas", "Fira Code"});
    AURORA_TEST_CHECK_FALSE(panel->chain_view().at_capacity);

    const HitSpot pick = reveal_chain_candidate(h, *panel, 1);
    AURORA_TEST_REQUIRE_MSG(pick.widget != nullptr, "a candidate is not dispatch-reachable below the cap");
    h.click(pick.x, pick.y);
    h.render();
    nearly_full.push_back("Consolas");
    check_chain_sequence("the append landed at the tail", chain_families(panel->chain_view().items), nearly_full);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);

    // 恰达上限：追加口关死（A5-a）。`Consolas` 已进链故从池里消失，剩下两档仍列出但都不吃点击。
    AURORA_TEST_REQUIRE(panel->chain_view().items.size() == kChainCapacity);
    AURORA_TEST_CHECK_TRUE(panel->chain_view().at_capacity);
    check_chain_sequence("the pool at the cap still lists matches", panel->chain_view().candidates,
                         {"Cascadia Code", "Fira Code"});
    AURORA_TEST_CHECK_EQ(panel->chain_view().hint,
                         borealis::ui::settings_label("settings.chain.full",
                                                      {au::LocalizedString{std::to_string(kChainCapacity)}}));
    for (std::size_t slot = 0; slot < 2U; ++slot) {
        AURORA_TEST_REQUIRE(panel->chain_candidate(slot) != nullptr);
        AURORA_TEST_CHECK_FALSE(panel->chain_candidate(slot)->wants_click());
        const HitSpot dead = reveal_chain_candidate(h, *panel, slot);
        AURORA_TEST_REQUIRE_MSG(dead.widget != nullptr, "the disabled candidate is not dispatch-reachable");
        h.click(dead.x, dead.y);
        h.render();
    }
    check_chain_sequence("the disabled clicks changed the chain", chain_families(panel->chain_view().items),
                         nearly_full);
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);  // 点击没被遮罩接走（那会关掉面板，计数同样不动）
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);  // 上面那一次追加之后没有第二次提交
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);
    AURORA_TEST_CHECK_FALSE(panel->form().has_unsaved_changes());
}

/// @brief 链正被抓在键盘上时 `Escape` 先交还抓取而不关面板（S8 那条交接腿，文件头③）。
///
/// 判据的根据是框架的两条既有语义相叠：面板的 `Escape` 挂在 `ShortcutScope::Global`，而全局快捷键
/// **先于任何控件**消费（裁决 7.51③ 理由 (a)）；`ReorderableList::on_key_event` 里那一档
/// `Escape → cancel_keyboard_grab()` 因此永远轮不到执行。不先问一句的话，用户正按着 Space 搬一项时
/// 敲 `Escape` 会连面板一起关掉，而那一项还悬在半空。本例把这一次交接走成真实按键：焦点交给列表本体
/// （与 `src/main.cpp` 派初始焦点同一条入口），`Space` 经派发器落到控件的抓取路径。
///
/// 抓取与取消都不动数据，故全程 persist / broadcast 恒 0；`cancel_keyboard_grab()` 会把光标放回抓取前
/// 的位置，链的次序逐字不变。交还之后第二次 `Escape` 才关面板并解绑（登记与解绑那一腿由非无头通道的
/// `escape_closes_the_panel_and_unbinds_itself` 守，本例只判交接那一句之后的第二次）。
AURORA_TEST_CASE(escape_releases_a_keyboard_chain_grab_before_it_closes_the_panel) {
    Harness h;
    StoreProbe probe;
    probe.base.appearance.font_fallback_chain = {"Fira Code", "Consolas", "Cascadia Code"};
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);

    auto *list = dynamic_cast<au::ReorderableList<std::string> *>(panel->chain_list());
    AURORA_TEST_REQUIRE(list != nullptr);
    AURORA_TEST_REQUIRE_MSG(list->keyboard_reorder(), "the framework's keyboard reorder path is off");

    h.focus_manager().set_focus(panel->chain_list());
    h.render();
    // 获焦即把键盘光标落到首个可见项（框架 `on_focus_change` 的语义，不是本件放的），故这里判前提而非设值。
    AURORA_TEST_REQUIRE_MSG(list->keyboard_index() >= 0, "focusing the chain list did not place the keyboard cursor");

    AURORA_TEST_REQUIRE_MSG(h.press(au::KeyCode::Space), "the chain list did not claim the activation key");
    AURORA_TEST_REQUIRE(list->is_keyboard_grabbed());
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);  // 抓取不是提交
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);

    auto escape = []() -> au::KeyEvent {
        au::KeyEvent event;
        event.key = static_cast<int>(au::KeyCode::Escape);
        event.action = au::KeyAction::Down;
        return event;
    }();

    // 面板闭包里那句 `cancel_keyboard_grab()` 是承重的：返真即原地不动，面板与浮层都还在。
    AURORA_TEST_REQUIRE(h.shortcuts().handle(escape, false));
    AURORA_TEST_CHECK_TRUE(panel->is_open());
    AURORA_TEST_CHECK_FALSE(list->is_keyboard_grabbed());
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);
    AURORA_TEST_CHECK_EQ(h.shortcuts().count(), 1U);  // 解绑只发生在关面板那一刻
    check_chain_sequence("the cancelled grab left the order alone", chain_families(panel->chain_view().items),
                         {"Fira Code", "Consolas", "Cascadia Code"});
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);

    // 没有抓取的那一次才关面板：交接腿不能把 `Escape` 永久吞在链上。
    AURORA_TEST_REQUIRE(h.shortcuts().handle(escape, false));
    AURORA_TEST_CHECK_FALSE(panel->is_open());
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 0U);
    AURORA_TEST_CHECK_EQ(h.shortcuts().count(), 0U);
}

/// @brief 行区交回无障碍通道的滚动读数是「视口占内容的比例」，而不是旧式把跨度当分母（G33 回货的消费腿）。
///
/// 登记这条缺口的正是 #130 那次真机走查：`IScrollProvider::get_VerticalViewSize` 报 99.2126%，反解恰是
/// 126/127（旧式 `max/(max+1)*100`），而无头侧独立量得本仓行区的可滚跨度正是 126 dp——那条读数当场把走查
/// 带偏成「视口≈内容 ⇒ 没什么可滚」，于是裁决 7.63⑥ 一度规定本仓**不采信**该读数。回货给
/// `AccessibilityScrollRange` 补了 `viewport` / `content` 两个量，UIA 侧的分母第一次是真内容高。
///
/// 本例钉两件事，缺一就对那次误判没有免疫力：① **两个量的来源**是本仓这一控件的几何（`viewport` 等于行区
/// 自身的盒高、`max - min` 等于 `content - viewport`）——算式正确而输入陈旧仍是错的读数，而这只有消费侧能验；
/// ② **读数的量级**与旧式分离（在有真跨度的行区上，比值式必然低于 `max/(max+1)` 那一档）。
/// 第三条断「滚动只动 `position`、两个量不动」，即 `VerticalPercent` 说「看到哪儿」而 `VerticalViewSize` 说
/// 「看到多少」这两句在本仓消费面上互不串味（回货判据 ③ 的语义自洽）。
AURORA_TEST_CASE(the_row_area_reports_a_viewport_over_content_ratio_to_the_a11y_channel_G33) {
    Harness h;
    StoreProbe probe;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);
    panel->select_page(SettingsPage::Terminal);
    h.render();

    // 锚点与行区容器的取法沿用 G27 那条消费腿：行区是 `Scroll`，故沿真实派发链现问而不猜坐标。
    const HitSpot anchor = h.find_first("SpinBox");
    AURORA_TEST_REQUIRE(anchor.widget != nullptr);
    au::Scroll *area = h.row_scroll(anchor.x, anchor.y);
    AURORA_TEST_REQUIRE(area != nullptr);

    const auto range = area->accessibility_scroll();
    AURORA_TEST_REQUIRE_MSG(range.has_value(), "the row area exposes no scroll range at all");

    // ① 两个量与本仓几何同源：视口＝行区控件自身的盒高，跨度＝内容 − 视口（框架头注那条不变量）。
    //    这两句排在跨度前提**之前**，因为「算式正确而输入陈旧」正是登记时那次误判的形态，鉴别力要单独观测。
    AURORA_TEST_CHECK_NEAR(range->viewport, area->paint_bounds().size.height, 1.0);
    AURORA_TEST_CHECK_NEAR(range->max - range->min, range->content - range->viewport, 1.0);

    // 前提：这是一份**真有跨度**的读数。无跨度时新旧两式都报 100，量级那条判别就失去鉴别力。
    AURORA_TEST_REQUIRE_GT(range->max, 50.0);
    AURORA_TEST_REQUIRE_GT(range->content, range->viewport);
    AURORA_TEST_CHECK_EQ(range->min, 0.0);

    // ② UIA 那一条读数取 viewport/content，而**不是**登记时的 max/(max+1)。
    const double view_size = au::compute_vertical_view_size(*range);
    AURORA_TEST_CHECK_NEAR(view_size, range->viewport / range->content * 100.0, 1e-3);
    const double stale_reading = range->max / (range->max + 1.0) * 100.0;  // 99.2% 那一档的旧式
    AURORA_TEST_CHECK_LT(view_size, stale_reading);
    AURORA_TEST_TRACE(std::to_string(range->viewport) + " / " + std::to_string(range->content) + " / " +
                      std::to_string(range->max) + " / " + std::to_string(view_size) + " / " +
                      std::to_string(stale_reading));

    // ③ 滚三档（框架缺省 `step` 16 dp × 3）之后只有 `position` 走，两个量逐字不动。
    const double position_before = range->position;
    for (int notch = 0; notch < 3; ++notch) {
        h.scroll(anchor.x, anchor.y, -1.0F);  // 负方向是往下滚（`ScrollViewport` 的符号约定）
    }
    h.render();
    const auto scrolled = area->accessibility_scroll();
    AURORA_TEST_REQUIRE(scrolled.has_value());
    AURORA_TEST_CHECK_GT(scrolled->position, position_before);
    AURORA_TEST_CHECK_NEAR(scrolled->viewport, range->viewport, 0.5);
    AURORA_TEST_CHECK_NEAR(scrolled->content, range->content, 0.5);
    AURORA_TEST_CHECK_NEAR(au::compute_vertical_view_size(*scrolled), view_size, 0.5);
}

/// @brief 预览是一条 140 dp 的横条，挂在卡片底部、将行区的视口正好压短 140 dp（F-e）。
///
/// 「卡片总高不变」这条只能问**坐标空间之外**的量：卡片本身由外层 `LayoutBuilder` 钳定，横条是它的
/// 第三个子节点，所以真正会变的是行区那一段的高度。G33 那条回货给行区的无障碍滚动读数补了 `viewport`
/// 与 `content` 两个量，且其 `viewport` 就是行区控件自身的盒高（同源断言已在那一例里钉过），故本例拿
/// 它当量具而**不比任何 bounds**：装预览前后 `content` 一字未动（行表没变）、`viewport` 少 140。
/// 另两条判的是界面事实：横条的可达框高约 140 dp（按常量高，不按内容算），以及它在行区之下。
///
/// 取锚点走**终端页**：外观页在 #114 之后被主题卡与 16 格色板两段把通用行整体下推，offset 0 的可见带里没有
/// 步进器（与 `a_row_control_still_commits_after_the_row_area_has_been_scrolled` 同一条既有在册现象）。
AURORA_TEST_CASE(the_preview_bar_is_a_140_dp_strip_below_the_row_area_and_shortens_its_viewport) {
    Harness h;

    StoreProbe bare;  // 不装预览接缝：这是「扣了 140 之前」的那一侧读数
    std::unique_ptr<SettingsPanel> panel_without = h.attach(bare);
    h.open(*panel_without);
    panel_without->select_page(SettingsPage::Terminal);
    h.render();
    const HitSpot no_bar = h.find_first("TerminalView");
    AURORA_TEST_CHECK_TRUE(no_bar.widget == nullptr);  // 不装即不画（S7 的可选腿）
    const HitSpot anchor_before = h.find_first("SpinBox");
    AURORA_TEST_REQUIRE(anchor_before.widget != nullptr);
    au::Scroll *area_before = h.row_scroll(anchor_before.x, anchor_before.y);
    AURORA_TEST_REQUIRE(area_before != nullptr);
    const auto before = area_before->accessibility_scroll();
    AURORA_TEST_REQUIRE(before.has_value());
    panel_without->close();
    h.render();

    StoreProbe wired;
    wired.with_preview = true;
    std::unique_ptr<SettingsPanel> panel_with = h.attach(wired);
    h.open(*panel_with);
    panel_with->select_page(SettingsPage::Terminal);
    h.pump_and_render(*panel_with);
    const HitSpot bar = h.find_first("TerminalView");
    AURORA_TEST_REQUIRE(bar.widget != nullptr);
    AURORA_TEST_CHECK_NEAR(bar.box.size.height, kPreviewBarHeightDp, 2.0);

    const HitSpot anchor_after = h.find_first("SpinBox");
    AURORA_TEST_REQUIRE(anchor_after.widget != nullptr);
    au::Scroll *area_after = h.row_scroll(anchor_after.x, anchor_after.y);
    AURORA_TEST_REQUIRE(area_after != nullptr);
    const auto with = area_after->accessibility_scroll();
    AURORA_TEST_REQUIRE(with.has_value());

    AURORA_TEST_CHECK_NEAR(before->viewport - with->viewport, kPreviewBarHeightDp, 2.0);
    AURORA_TEST_CHECK_NEAR(with->content, before->content, 2.0);  // 卡片总高不变 ⇒ 行表内容一字未动
    // 「底部」判的是顺序：横条在行区之下（`Column` 按 children 次序落位，横条是第三个子节点）。
    const au::Rect area_box = area_after->paint_bounds();
    AURORA_TEST_TRACE(std::string{"area bottom "} + std::to_string(area_box.bottom()) + " / bar top " +
                      std::to_string(bar.box.origin.y));
    AURORA_TEST_CHECK_MSG(bar.box.origin.y >= area_box.bottom() - 2.0, "the preview strip must sit below the row area");
}

/// @brief 预览里的点击既不夺键盘也不提交任何东西（F-a 的「用户会话不被接管」在界面腿上的那一半）。
///
/// 判据文 F-a 说的是「独立会话 + 真实视口，而用户自己的会话不被接管」，其实现前提有三条（文件头③），
/// 本例把三条折成一次真实点击的可观察后果：① 视口不可获焦 ⇒ 点它之后焦点序里仍没有它，且沿 Tab 序绕一圈
/// 也落不到它身上（宿主开关的一票否决在键盘通道上同样成立）；② 预览的连接 `write()` 是空实现且
/// `copy_on_select` 恒假 ⇒ 一次点击既不产选区复制也不向任何对端发字节；③ 落盘与广播计数为 0，即「看着
/// 像控件」不等于「是一次提交」。第四条断「点横条不把面板关掉」：遮罩的关面板腿按命中归属判，而横条
/// 在卡片之内，那一刻它必须自己claim 住这一点（裁决 7.49① 的 `is_handled`）。
AURORA_TEST_CASE(clicking_the_preview_bar_neither_steals_the_keyboard_nor_commits_anything) {
    Harness h;
    StoreProbe probe;
    probe.with_preview = true;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);
    h.pump_and_render(*panel);

    borealis::ui::TerminalView *view = panel->preview_view();
    AURORA_TEST_REQUIRE(view != nullptr);
    AURORA_TEST_CHECK_FALSE(view->focusable());  // ① 的前提：一票否决的宿主开关（框架 §4.2）
    const HitSpot bar = h.find_first("TerminalView", 0.0F, [view](au::Widget *widget) -> bool { return widget == view; });
    AURORA_TEST_REQUIRE(bar.widget != nullptr);
    const float center_x = bar.box.origin.x + bar.box.size.width * 0.5F;
    const float center_y = bar.box.origin.y + bar.box.size.height * 0.5F;

    probe.reset_counters();
    h.click(center_x, center_y);
    h.render();

    AURORA_TEST_CHECK_TRUE(panel->is_open());  // ④ 横条在卡片之内：这一点归它，不归遮罩
    AURORA_TEST_CHECK_EQ(h.overlay_count(), 1U);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 0U);
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 0U);
    AURORA_TEST_CHECK_NE(h.focus_manager().focused(), static_cast<au::Widget *>(view));
    // ① 的另一半只能问**焦点序**：基类 `Widget::on_text_input` 的缺省就是 `is_handled = true`，故「发一段
    // 文本看有没有人认领」这条判据在结构上恒真（点完横条后焦点按 `focus_target_of(chain)` 落在外层可获焦
    // 祖先 `Column` 上，那一位会把任何文本吞掉），拿它当「预览不夺键盘」的证人是个假红。可判的等价形态是
    // 「预览从不进 Tab 序」——候选集即框架私有的 `collect_focusable`，本件只能经公开入口 `move_focus` 绕一圈。
    bool preview_in_tab_order = false;
    for (int step = 0; step < 24; ++step) {
        h.focus_manager().move_focus(au::FocusDirection::Forward);
        preview_in_tab_order = preview_in_tab_order || h.focus_manager().has_focus(view);
    }
    AURORA_TEST_CHECK_FALSE(preview_in_tab_order);
}

/// @brief 同一份夹具喂两台视口：改一格色板 ⇒ 预览盒与对照视口的那一格色逐位一起变（§8 判据①）。
///
/// 这条判据的本体是「预览盒与主视口同一格色逐位变化」，而判它必须在**两台结构同源的视口**之间比：
/// 第二台是本件按同一份 `preview_fixture()` 与同一份 `Appearance` 造的对照视口（挂在同一场景根的
/// 浮层上，矩形刻意与横条不相交故互不夺派发）。两台尺寸不同 ⇒ 行列数不同 ⇒ 「同一格」只能按各自的
/// 网格几何折回窗口坐标（`cell_center`），这排除了「拿同一串像素下标比两台画面」那种假绿。
///
/// 三句缺一都不算守住：⑴ 改之前两处逐位相等且**等于该槽位的色板值**——两处都读到同一个错东西（例如
/// 都读到遮罩底）时，只比「相等」的那句照样是绿的；⑵ 改之后两处仍逐位相等；⑶ 改之后的读数等于新值
/// 且与旧值不同（否则「色带根本没跟着 palette 走」被「两处一起不动」这个正确但无关的形态掩盖）。
AURORA_TEST_CASE(the_preview_bar_and_a_second_viewport_of_the_same_fixture_shift_the_same_cell_together) {
    Harness h;
    StoreProbe probe;
    probe.with_preview = true;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);
    h.pump_and_render(*panel);

    // 对照视口：与横条同一份夹具、同一份外观，故 F-c 的「同源」在此落成同一份折算的两次独立调用。
    auto control = std::make_unique<borealis::ui::SettingsPreview>(test_appearance(probe.base));
    // 尺寸同样只能经**控件级意图**下达（与 `build_preview_bar()` 那条生产注释同源）：挂在修饰链上的
    // `.width(400).height(200)` 会被 `ui::TerminalView` 自宣的 `fill()` 意图在测量之后覆写掉，实测对照
    // 视口铺满整窗并把横条压在下面（`find_first` 因此一台预览也探不到）。
    control->view().width(au::px(kControlWidthDp));
    control->view().height(au::px(kControlHeightDp));
    control->ensure_mounted(au::BuildContext{});  // 与面板那条补偿同源（G35：宿主不在运行期挂新浮层）
    h.add_overlay(control->node());
    h.pump_and_render(*panel, control.get());

    borealis::ui::TerminalView *bar = panel->preview_view();
    AURORA_TEST_REQUIRE(bar != nullptr);
    const HitSpot bar_spot = h.find_first("TerminalView", 0.0F, [bar](au::Widget *widget) -> bool { return widget == bar; });
    AURORA_TEST_REQUIRE(bar_spot.widget != nullptr);
    const HitSpot control_spot =
        h.find_first("TerminalView", 0.0F, [control = control.get()](au::Widget *widget) -> bool {
            return widget == static_cast<au::Widget *>(&control->view());
        });
    AURORA_TEST_REQUIRE(control_spot.widget != nullptr);

    // 前提：夹具那一格在两台视口里都还在屏上（横条按 F-e 只有 6~7 行，示范面本就收窄）。
    const borealis::ui::GridGeometry &bar_grid = bar->grid_geometry();
    const borealis::ui::GridGeometry &control_grid = control->view().grid_geometry();
    AURORA_TEST_REQUIRE_MSG(bar_grid.rows > kFixtureSwatchRow && bar_grid.columns > kFixtureSwatchColumn,
                            "the fixture's swatch cell is not inside the preview bar");
    AURORA_TEST_REQUIRE_MSG(control_grid.rows > kFixtureSwatchRow && control_grid.columns > kFixtureSwatchColumn,
                            "the fixture's swatch cell is not inside the control viewport");
    AURORA_TEST_CHECK_NE(bar_grid.rows, control_grid.rows);  // 两台尺寸不同 ⇒ 比的是「同源」而不是「同像素」

    const au::Point bar_cell = cell_center(bar_spot, *bar, kFixtureSwatchRow, kFixtureSwatchColumn);
    const au::Point control_cell = cell_center(control_spot, control->view(), kFixtureSwatchRow, kFixtureSwatchColumn);
    const RgbaColor baseline = test_appearance(probe.base).palette.basic[kFixtureSwatchSlot];
    const RgbaColor before_bar = h.probe_point(bar, bar_cell.x, bar_cell.y);
    const RgbaColor before_control = h.probe_point(&control->view(), control_cell.x, control_cell.y);
    AURORA_TEST_CHECK(before_bar == before_control);
    AURORA_TEST_CHECK(before_bar == baseline);

    // 改的是色板 `basic[1]` 那一格：走面板的提交入口，横条那条腿由 `refresh_preview()` 换外观。
    const RgbaColor edited{9U, 214U, 41U, 255U};
    AURORA_TEST_REQUIRE_EQ(panel->commit_slot("appearance.palette.basic", kFixtureSwatchSlot, "#09D629"),
                           CommitIssue::None);
    control->apply(test_appearance(probe.base));  // 对照视口代表主视口：装配层那边收到的是同一次广播
    h.pump_and_render(*panel, control.get());

    const RgbaColor after_bar = h.probe_point(bar, bar_cell.x, bar_cell.y);
    const RgbaColor after_control = h.probe_point(&control->view(), control_cell.x, control_cell.y);
    AURORA_TEST_CHECK(after_bar == after_control);
    AURORA_TEST_CHECK(after_bar == edited);
    AURORA_TEST_CHECK_FALSE(after_bar == before_bar);
    AURORA_TEST_CHECK_EQ(probe.persist_calls, 1U);   // 一格改动经的是整份表单那一次落盘（裁决 7.61③）
    AURORA_TEST_CHECK_EQ(probe.broadcast_calls, 1U);
}

/// @brief 预览的行列数由它**自己的矩形**派生且只装整数格；字号 14→24 之后同一矩形里行列严格变少（F-d / §8 判据②）。
///
/// F-d 说的是「预览自己的行列数从自己的框算」，而它可判的形态是两半：① 该框里行列数是**装得下的最大
/// 整数**（再多一格就溢出 ⇒ 不出现半格，即 §8 判据② 后半句），② 换一个字号就换一个格步长，于是同一个
/// 140 dp 的框里行数必然变少。第②半也是「运行期换外观不重建视口」那条口径（裁决 7.53 S4①）的证人：
/// 横条的控件实例在改动前后必须是**同一个指针**，否则「行列数跟着变」就成了「新建了一个视口」。
AURORA_TEST_CASE(the_preview_grid_is_whole_cells_in_its_own_rect_and_follows_the_font_size) {
    Harness h;
    StoreProbe probe;
    probe.with_preview = true;
    std::unique_ptr<SettingsPanel> panel = h.attach(probe);
    h.open(*panel);
    h.pump_and_render(*panel);

    borealis::ui::TerminalView *bar = panel->preview_view();
    AURORA_TEST_REQUIRE(bar != nullptr);
    const HitSpot spot = h.find_first("TerminalView", 0.0F, [bar](au::Widget *widget) -> bool { return widget == bar; });
    AURORA_TEST_REQUIRE(spot.widget != nullptr);
    AURORA_TEST_CHECK_NEAR(spot.box.size.height, kPreviewBarHeightDp, 2.0);
    AURORA_TEST_REQUIRE_MSG(fits_whole_cells(spot.box, bar->grid_geometry(), "before"), "half cells in the preview rect");
    const borealis::ui::GridGeometry before = bar->grid_geometry();
    AURORA_TEST_REQUIRE_GT(before.rows, 0U);
    AURORA_TEST_REQUIRE_GT(before.columns, 0U);

    AURORA_TEST_REQUIRE_EQ(panel->commit("appearance.font_size_pt", FormValue::real(24.0)), CommitIssue::None);
    h.pump_and_render(*panel);

    AURORA_TEST_CHECK_EQ(panel->preview_view(), bar);  // 同一实例：换档走的是运行期入口而不是重建
    const HitSpot grown = h.find_first("TerminalView", 0.0F, [bar](au::Widget *widget) -> bool { return widget == bar; });
    AURORA_TEST_REQUIRE(grown.widget != nullptr);
    AURORA_TEST_REQUIRE_MSG(fits_whole_cells(grown.box, bar->grid_geometry(), "after"), "half cells after the font change");
    const borealis::ui::GridGeometry &after = bar->grid_geometry();
    AURORA_TEST_CHECK_GT(after.cell_height, before.cell_height);
    AURORA_TEST_CHECK_GT(after.cell_width, before.cell_width);
    AURORA_TEST_CHECK_LT(after.rows, before.rows);
    AURORA_TEST_CHECK_LT(after.columns, before.columns);
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

AURORA_TEST_CASE(the_option_panel_below_the_enclosing_row_now_reaches_the_dispatch_chain_G30) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(a_real_click_below_the_enclosing_row_picks_the_option_under_it_G31) {
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

AURORA_TEST_CASE(clicking_a_theme_card_writes_six_keys_persists_once_and_broadcasts_once) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(clicking_a_swatch_slot_repoints_the_editor_and_blur_commits_only_that_slot) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_reset_button_restores_only_the_selected_slot) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(an_unlisted_theme_name_disables_the_reset_button) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(clicking_the_chain_move_buttons_reorders_and_removes_with_one_commit_each) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(typing_in_the_chain_filter_narrows_the_pool_without_touching_the_form) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_candidate_append_is_open_until_the_chain_reaches_the_framework_capacity) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(escape_releases_a_keyboard_chain_grab_before_it_closes_the_panel) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_row_area_reports_a_viewport_over_content_ratio_to_the_a11y_channel_G33) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_preview_bar_is_a_140_dp_strip_below_the_row_area_and_shortens_its_viewport) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(clicking_the_preview_bar_neither_steals_the_keyboard_nor_commits_anything) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_preview_bar_and_a_second_viewport_of_the_same_fixture_shift_the_same_cell_together) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

AURORA_TEST_CASE(the_preview_grid_is_whole_cells_in_its_own_rect_and_follows_the_font_size) {
    AURORA_TEST_SKIP("AURORA_BACKEND_HEADLESS not enabled, HeadlessSurface is not compiled");
}

#endif

}  // namespace borealis::test_cases::itest_settings_panel
