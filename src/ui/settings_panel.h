#pragma once

// ============================================================
// 设置面板的界面腿本体（src/ui/settings_panel.h）
// ------------------------------------------------------------
// `codespec/UI_SETTINGS.draft.md` 屏 3 的落地第一棒（裁决 7.52 把 S1~S16 全部自拍为建议项）：
// 本棒交付**四页骨架 + 六类通用行控件 + 表单落盘与广播接线**，其余按判据文 §8 的分工留给后续棒——
// 外观页的四个专用控件（主题卡 / 16 格色板 / 字体族下拉 / 回退链重排表）、右侧实时预览盒、连接页与
// 状态栏开关组、快捷键只读表、以及损坏配置的启动对话框。
//
// 三条决定形态的框架实测（裁决 7.56，其前两条曾登记为缺口 G26 / G27 并已于同日回货闭合，见裁决 7.57①）：
// ① 浮层必须是**确实写子节点 bounds 的容器**：登记时 `Dialog` 与 `Scroll` 都不写 bounds，挂在
//    `OverlayHost` 上就是抓不住的死浮层。故本件用 `Stack`（遮罩 `Canvas` + 卡片）承载 `Column` 卡片，
//    行区取 `LazyList`。两条回货后本件**判定不迁回** `Dialog`（内层 `self × 0.8` 约束、遮罩不关面板、
//    `show()` 带模态作用域，三条均与 S1 拍的同窗口非模态浮层不合，见裁决 7.57③）。
// ② `OverlayHost` 给浮层的是**松约束**（min 0、max 自身），而 `Canvas` 的自动尺寸会夹到 100×100，
//    故遮罩层须挂 `Modifier{}.fill_max_size()` 才铺满整窗；卡片用 `LayoutBuilder` 按实际可用尺寸钳位，
//    于是稿面 1500×900 dp 在 960×640 dp 的窗口里不会溢出（S1「同窗口浮层」的物理前提）。
// ③ `Button` 的标签绘制曾绕过 `StringTable::resolve()`（缺口 G28，已回货闭合，见裁决 7.59）：现在框架
//    自己的 `resolved_label()` 就是它 `on_layout` / `on_paint` 的唯一显示串来源，故按钮文案可直接交
//    `LocalizedString`（`settings_text()`）与 `Text` 同源。`settings_label()` 仍留给收 `std::string`
//    的入口（`Text::placeholder`、角标等）——那些入口本就没有查表路径。
//
// 一条曾在册的派发限制（缺口 G29，已回货但**只闭合一层**，见裁决 7.59）：`Dropdown` 的展开选项列画在主框
// 之外而不占布局，而嵌套在容器里的溢出区进不了真实派发链（祖先按 `child.bounds()` 判包含），故「真点一个
// 选项即提交」在回货前写不成判据。框架回货形态是 `Widget::extra_hit_box()` 钩子加祖先那一层的合并门
// （`child.bounds().contains(...) || child.covers_extra_hit_box(...)`），`Dropdown` 已覆写该钩子，于是面板
// 「还落在所在行 bounds 内」的那一段选项现走真实派发，那条钉子用例随之翻成正向行为用例。门只问**直接子**
// 自己的申报、不随祖先上传，而面板每行都是 `LazyList → Row → Dropdown` 的三层嵌套，故伸出行下沿之外的残段
// 仍不可达——登记为 **G30**，本件不为此自造覆盖层或改写挂载点（不等不绕，裁决 7.13①）。
//
// 面板不认识 `config`，也不认识 `TerminalView`：装载 / 落盘 / 广播三条接缝由 `Hooks` 交装配层兑现
// （`config/settings.h` 已 include `ui/palette.h`，反向 include 即 `config ⇄ ui` 模块环，与
// `settings_catalog.h` 同一条理由）。于是「即时生效」到底改哪些对象，是装配层的一次快照搬运，
// 面板只负责在提交通过、且该行 `apply_scope()` 为 `PersistAndApplyNow` 时把整份表单交回去。
//
// 文本提交的时机是 S14 的硬约束，兑现为类型而非约定：文本行用本件私有的 `BlurCommitText`
// （`TextInput` 子类，覆写 public virtual 的 `on_focus_change`），**只有失焦与 Enter 才把文本交进
// 表单**，逐字符变化一个字节也不提交——半截输入 `#12` 因此不会落盘也不会生效。
//
// 私有头（裁决 D1① 同口径）：本件含框架类型，不进 `include/borealis/`。
// ============================================================

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/app/shortcuts.h"
#include "aurora/theming/theme.h"
#include "aurora/widget/popup.h"
#include "aurora/widget/text.h"

#include "borealis/ui/settings_catalog.h"
#include "borealis/ui/settings_form.h"

namespace borealis::ui {

/// @brief 面板 chrome 的色值唯一来源，同时是装配层给场景根那层 `ThemeScope` 的主题。
///
/// 刻意**不随终端主题联动**（裁决 7.52 的 S5① / N6：终端配色与界面配色是两件事，切主题时整窗
/// 跟着变会让用户以为丢了设置）。面板内联色与本函数取自同一批文件内常量，故全仓只有一份 chrome 色值。
[[nodiscard]] auto settings_chrome_theme() -> aurora::Theme;

/// @brief 设置面板：挂在场景根浮层宿主上的全屏浮层（S1），装载一份表单副本并就地编辑（S3①）。
///
/// 生命周期：`open()` 装载副本、建浮层并登记 `Escape` 的全局快捷键（S2①）；`close()` 撤浮层并**解绑**
/// 那条快捷键——留着它，面板关闭后 `Escape` 就会静默吞掉发往会话的按键。析构时若还开着同样收口。
class SettingsPanel {
public:
    /// @brief 面板与存储侧、绘制侧的全部接缝（struct-of-回调，同 `WorkspaceView::Hooks` 的形态）。
    ///
    /// 三条都按**整份表单**说话而不是按单键：落盘要的是「一次替换 + 一次 flush」，广播要的是
    /// 「一份完整配置搬进控件」，逐键接缝会把落盘拆成 N 次写文件。
    struct Hooks {
        /// @brief 取当前配置的每个落盘叶子键（装配层经 `config::form_entries()` 搬值）。
        std::function<std::vector<FormEntry>()> load;
        /// @brief 把整份表单写回存储。
        /// @return 成功为空；失败为 ASCII 原因（`Store::replace()` 在备份未成功时拒绝落盘）。
        std::function<std::optional<std::string>(const SettingsForm &)> persist;
        /// @brief 把整份表单的当前值搬进运行中的视口（仅「已接线 ∧ 即时」的提交会触发）。
        std::function<void(const SettingsForm &)> broadcast;
    };

    /// @brief 面板当前页上的一行（用例据此核对「面板画的键」与反向核对表一致，判据文 §8 判据①）。
    ///
    /// `badge` 与 `summary` 是这一行的两列文字，交出来而不只画在树上：两者都由 `settings_catalog()`
    /// 的两列正交字段折算，用例判的是「表说了什么」对「面板给了什么」，读浮层树里的 `Text` 反而要把
    /// 排版坐标也算进判据。
    struct VisibleRow {
        std::string key;       ///< 落盘点号路径。
        ControlKind kind{};    ///< 控件形态。
        bool editable{};       ///< 该行的控件是否可交互（`Absent` 与专用控件的占位行都是 false）。
        std::string badge{};   ///< 角标文案（「延后」/「下次会话生效」/两者并列），无角标为空。
        std::string summary{}; ///< 占位行的只读值摘要；可交互行为空（值就在它自己的控件里）。
    };

    SettingsPanel(aurora::OverlayHost &host, aurora::ShortcutRegistry &shortcuts, Hooks hooks);
    SettingsPanel(const SettingsPanel &other) = delete;
    auto operator=(const SettingsPanel &other) -> SettingsPanel & = delete;
    SettingsPanel(SettingsPanel &&other) = delete;
    auto operator=(SettingsPanel &&other) -> SettingsPanel & = delete;
    ~SettingsPanel();

    /// @brief 打开面板：装载表单副本、挂浮层、登记关闭快捷键。已开着则只刷新浮层。
    auto open() -> void;

    /// @brief 关闭面板：撤浮层并解绑 `Escape`。有未落盘的改动照旧保留在副本里（落盘是提交那一刻的事）。
    auto close() -> void;

    /// @brief 面板是否开着。
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 切页：按新页重建浮层。条目数变了可用 `LazyList::set_count` 就地改，但本件仍整块重建——
    ///        序号 → 键的映射随页而变，只改条目数会让旧页的条目按新页的序号复述。
    /// @param page 目标页。
    auto select_page(SettingsPage page) -> void;

    /// @brief 当前页。
    [[nodiscard]] auto current_page() const noexcept -> SettingsPage {
        return page_;
    }

    /// @brief 当前页的行表，次序即面板的排版次序（＝反向核对表内该页的次序）。
    [[nodiscard]] auto visible_rows() const -> std::vector<VisibleRow>;

    /// @brief 面板自持的那份表单副本（用例读它核对「改了又改回来不脏」这类判据）。
    [[nodiscard]] auto form() const noexcept -> const SettingsForm & {
        return form_;
    }

    /// @brief 非文本控件的提交入口（开关 / 步进器 / 下拉 / 色槽按钮的回调都落在这里）。
    /// @param key 落盘点号路径。
    /// @param next 新值。
    /// @return 未通过的原因；通过为 `CommitIssue::None`。
    ///
    /// **不加 `[[nodiscard]]` 是有意的**：控件回调那条路丢弃返回值，而失败原因已在 `after_commit` 里
    /// 写进该行的状态列，界面上不存在「静默失败」的通路；返回值只服务用例的校验断据（判据文 §8）。
    auto commit(std::string_view key, FormValue next) -> CommitIssue;

    /// @brief 文本行的提交入口（S14：失焦或 Enter 才走到这里）。
    /// @param key 落盘点号路径。
    /// @param text 文本框当前内容。
    auto commit_text(std::string_view key, std::string_view text) -> CommitIssue;

    /// @brief 把一个可缺省色槽置为「未配」（A2-b 的另一态）。
    /// @param key 落盘点号路径。
    auto commit_unset(std::string_view key) -> CommitIssue;

private:
    /// @brief 按当前页取行表（表内次序即排版次序，只收本页、只收装载成功的键）。
    [[nodiscard]] auto collect_rows() const -> std::vector<const SettingsControl *>;

    /// @brief 撤掉既有浮层（若有）再按当前页重建，并重挂关闭快捷键所需的对象。
    auto rebuild_overlay() -> void;

    /// @brief 建卡片：标题行 + 「左导航 / 右行区」 + 行区。
    [[nodiscard]] auto build_card() -> aurora::Node;

    /// @brief 建一行：标签 + 状态列（角标或本行最近一次提交的失败原因） + 控件。
    /// @param ordinal 该行在当前页的序号（状态列的索引锚）。
    [[nodiscard]] auto build_row(std::size_t ordinal) -> aurora::Node;

    /// @brief 建某行的控件腿：六类通用形态给真控件，五类专用形态给只读的值摘要（占位）。
    [[nodiscard]] auto build_control(const SettingsControl &control, bool editable) -> aurora::Node;

    /// @brief 提交后的公共腿：刷新状态列、按需落盘、按需广播。
    auto after_commit(std::string_view key, const SettingsControl &control, CommitIssue issue) -> void;

    /// @brief 写某行的状态列文案（该行此刻不在可见窗口内、条目已被回收时静默跳过）。
    ///
    /// 形参取 `LocalizedString` 而非显示串：角标是两枚已解析文案的拼接（走字面档），而校验失败原因
    /// 交词条 key 让框架在绘制时就地查表（`settings_issue_text` 的返回形态）。
    auto set_status(std::size_t ordinal, const aurora::LocalizedString &text) -> void;

    /// @brief 该行的角标文案（「延后」/「下次会话生效」/两者并列），无角标时为空串。
    [[nodiscard]] static auto badge_for(const SettingsControl &control) -> std::string;

    /// @brief 专用控件行的只读值摘要（本棒不编辑它们，只把当前值如实显示出来）。
    [[nodiscard]] auto value_summary(const SettingsControl &control) const -> std::string;

    /// @brief 一行是否可交互：`Absent` 一律灰置，专用控件形态在本棒是占位。
    [[nodiscard]] static auto is_editable(const SettingsControl &control) -> bool;

    aurora::OverlayHost &host_;       ///< 浮层宿主（装配层的场景根，非拥有）。
    aurora::ShortcutRegistry &shortcuts_;  ///< 快捷键注册表：`Escape` 的登记与解绑处（非拥有）。
    Hooks hooks_{};
    SettingsForm form_{std::vector<FormEntry>{}};  ///< 副本；未打开时是空表。
    SettingsPage page_ = SettingsPage::Appearance;
    bool open_ = false;
    /// 本面板浮层在宿主子节点里的序号（重建时用）。宿主没有基础内容时 `add_overlay` 返回
    /// `std::nullopt`（框架 G29 回货后的口径：那种序号恒被 `remove_overlay` 当基础内容拒收），
    /// 故这里以「无浮层」为缺省，而不是拿 0 当哨兵。
    std::optional<std::size_t> overlay_index_{};
    int escape_binding_ = 0;           ///< `Escape` 绑定的 id；0 = 未登记。
    std::vector<const SettingsControl *> rows_{};  ///< 当前页行表（与 `status_texts_` 同序）。
    std::vector<std::shared_ptr<aurora::Text>> status_texts_{};  ///< 各行状态列控件，按序号。
};

}  // namespace borealis::ui
