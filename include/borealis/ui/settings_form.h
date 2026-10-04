#pragma once

// ============================================================
// 设置面板的表单状态机（include/borealis/ui/settings_form.h）
// ------------------------------------------------------------
// 裁决 7.52 的 S3① 拍板「本仓自持一份副本 + 显式 `Store::replace()`」，其代价明写是「表单状态机
// （副本、脏标记、逐键落盘时机）归本仓自研并有单测」；该稿 §8 把它列为面板本体的第三件纯逻辑前置。
// 本件回答的正是那条代价里的三个名词：**哪些键改了**（脏标记按值比较而非按提交次数计数）、
// **要不要落盘**（`has_unsaved_changes()` 与落盘成功后的 `note_persisted()`——`replace()` 在备份
// 未成功时拒绝写文件并返回原因，那时脏标记必须留着，否则面板谎报已存）、
// **生效档位**（`apply_scope()`：只有「已接线 ∧ 即时」才有运行期广播对象，其余三档改动只落盘）。
//
// 三条刻意形态：
// ① 本头不含 Aurora 类型，也**不含 `config` 类型**（与 `settings_catalog.h` 同一条理由：
//    `config/settings.h` 已 include 本域的 `ui/palette.h`，反向 include 即 `config ⇄ ui` 模块环）。
//    于是键一律用**落盘点号路径**指代，值用本件自己的 `FormValue`，行一律向 `ui::settings_catalog()`
//    现查——面板、本件、反向核对件三者共用同一套地址。把 `config::Settings` 的成员搬进搬出落在
//    搬运件 `config/form_transfer.cpp`（该稿 §8 的 ④）。**本件守不到「两个同域成员互换」**
//    （`appearance.font_line_height` 与 `appearance.font_letter_spacing_dp` 都是实数），那条边界
//    由搬运件的取侧与写侧逐键证人守住（裁决 7.54），本件不因此认识成员。
// ② **校验式复用既有的两个真值源**，本件不新造第三条：取值域与区间来自 `ui::settings_catalog()`
//    （其区间逐字照抄装载侧 `src/config/store.cpp`），色值文本来自 `ui::color_from_hex`（S14 要求
//    「校验式必须与装载侧同一条」）。所以 `#12ab34ff` 这类「面板接受而存不回去」的形态在此同样判非法。
// ③ **未配是一次显式动作**（`commit_unset_color`），不是空串：装载侧区分「显式 `null`＝用户点名的
//    未配」与「畸形值＝回落并留痕」（裁决 7.27③），空串属后者。界面上「未配」与「配成黑色」必须
//    可区分（该稿 A2-b），故两者在值上也必须不同。
//
// 文本提交的时机归 S14：框架 `TextInput` 的文本只在**失焦或 Enter** 时才交进本件，逐字符不提交
// （半截输入 `#12` 既不落盘也不生效）。校验不过时本件**不改值**，只回一个 `CommitIssue`——中文词条
// 由本仓 `StringTable` 按该枚举取（裁决 7.25⑬），本件因此不产文案，也不产 ASCII 诊断串。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "borealis/ui/palette.h"
#include "borealis/ui/settings_catalog.h"

namespace borealis::ui {

/// @brief 快捷键覆盖表的一条（该稿 S9：快捷键页首版只读，故本件只展示不编辑）。
///
/// 用结构而不是映射：落盘形态是 `[{command, combo}, ...]` 数组（裁决 7.27②），本件照其形态承载，
/// 顺序即落盘顺序，不在这里按命令重新排序。
struct ShortcutOverride {
    std::string command{};  ///< 命令 id（一律 ASCII 英文）。
    std::string combo{};    ///< 组合键文本，形态照框架 `KeyCombo::to_string()`。

    /// @brief 逐字段全等比较（脏标记按值比较的判据）。
    [[nodiscard]] auto operator==(const ShortcutOverride &other) const noexcept -> bool = default;
};

namespace detail {

/// @brief `FormValue` 的存储形态；八档形态族与 `ValueDomain` 的对应见 `FormValue::holds`。
using FormValueStorage = std::variant<bool,
                                      std::int64_t,
                                      double,
                                      std::string,
                                      std::optional<RgbaColor>,
                                      std::vector<RgbaColor>,
                                      std::vector<std::string>,
                                      std::vector<ShortcutOverride>>;

}  // namespace detail

/// @brief 表单里一个键的值。构造一律走下面的具名工厂，没有隐式转换。
///
/// 工厂而非 `FormValue{true}` / `FormValue{1}` 这类隐式形态：若两者能建成同一个形态，「按行声明的域
/// 核对装载值」就失去判据，而 `Integral` 行收到实数恰好是面板把步进器接错行的现场。
class FormValue {
public:
    /// @brief 开关（`Boolean`）。
    [[nodiscard]] static auto boolean(bool value) -> FormValue;
    /// @brief 整数（`Integral`）；区间不在构造时校验，由提交处按该行的 `numeric` 判。
    [[nodiscard]] static auto integral(std::int64_t value) -> FormValue;
    /// @brief 实数（`Real`）。
    [[nodiscard]] static auto real(double value) -> FormValue;
    /// @brief 文本（`Choice` 的落盘文本名与 `FreeText` 的原样字符共用本形态）。
    [[nodiscard]] static auto text(std::string value) -> FormValue;
    /// @brief 色值（`ColorText` 的已配态）。
    [[nodiscard]] static auto color(RgbaColor value) -> FormValue;
    /// @brief 「未配」（`ColorText` 的未配态，只有光标与选区两槽合法，见 A2-b）。
    [[nodiscard]] static auto unset_color() -> FormValue;
    /// @brief 色板整表（`ColorTable`）；格数不在构造时校验，由提交处判恰为 16。
    [[nodiscard]] static auto color_table(std::vector<RgbaColor> value) -> FormValue;
    /// @brief 有序族名链（`FamilyChain`）。
    [[nodiscard]] static auto text_list(std::vector<std::string> value) -> FormValue;
    /// @brief 快捷键覆盖表（`OverrideMap`）。
    [[nodiscard]] static auto overrides(std::vector<ShortcutOverride> value) -> FormValue;

    /// @brief 判本值的形态族是否与该行的取值域相配。
    ///
    /// 只按**形态族**判：`Choice` 与 `FreeText` 同为文本，故本件不在此判「这个名字在不在白名单里」——
    /// 那是提交处的事。色值行的「未配」与「已配」同族，必填行因此要另判（`UnsetNotAllowed`）。
    [[nodiscard]] auto holds(ValueDomain domain) const noexcept -> bool;

    /// @brief 取布尔档；形态不符为空。
    [[nodiscard]] auto as_boolean() const noexcept -> std::optional<bool>;
    /// @brief 取整数档；形态不符为空。
    [[nodiscard]] auto as_integral() const noexcept -> std::optional<std::int64_t>;
    /// @brief 取实数档；形态不符为空。
    [[nodiscard]] auto as_real() const noexcept -> std::optional<double>;
    /// @brief 取文本档；形态不符为空。
    [[nodiscard]] auto as_text() const -> std::optional<std::string>;
    /// @brief 取色值档的已配值；未配或形态不符为空（先 `holds(ColorText)` 再取）。
    [[nodiscard]] auto as_color() const noexcept -> std::optional<RgbaColor>;
    /// @brief 判是否为色值档且处于「未配」态。
    [[nodiscard]] auto is_unset_color() const noexcept -> bool;
    /// @brief 取色板整表档；形态不符为空。
    [[nodiscard]] auto as_color_table() const -> std::optional<std::vector<RgbaColor>>;
    /// @brief 取有序族名链档；形态不符为空。
    [[nodiscard]] auto as_text_list() const -> std::optional<std::vector<std::string>>;
    /// @brief 取快捷键覆盖表档；形态不符为空。
    [[nodiscard]] auto as_overrides() const -> std::optional<std::vector<ShortcutOverride>>;

    /// @brief 逐形态全等比较（脏标记的判据；色值的 alpha 参与，与 `RgbaColor` 同口径）。
    [[nodiscard]] auto operator==(const FormValue &other) const noexcept -> bool = default;

private:
    explicit FormValue(detail::FormValueStorage value);

    detail::FormValueStorage value_;
};

/// @brief 一次提交没通过的原因；`None` 即通过。
///
/// 每个值都对应一条「界面腿自己判不出、或判了就会与装载侧分叉」的规则，故逐条在册。
enum class CommitIssue : std::uint8_t {
    None,             ///< 通过。
    UnknownKey,       ///< 路径不在反向核对表里（面板画了 schema 外的控件）。
    NotLoaded,        ///< 路径在表里但表单没装载它的值（装载名单与表分叉）。
    ReadOnly,         ///< 该行是只读形态（快捷键页首版，S9 / D3-a：不给会失灵的按钮）。
    DomainMismatch,   ///< 值的形态族与该行声明的域不符；`Integral` 行收到实数也落在这里。
    TextNotAccepted,  ///< 该行的控件形态不收文本提交（开关、下拉、色板整表、族名链、只读表）。
    MalformedNumber,  ///< 文本不是良构数字。
    NotIntegral,      ///< 整数档收到带小数点的文本。
    OutOfRange,       ///< 越出该行照抄装载侧的闭区间。
    NotAChoice,       ///< 文本不在该行的白名单里（逐字节精确、区分大小写，同装载侧）。
    MalformedColor,   ///< 不是 7 字符 `#RRGGBB`；八位带 alpha 的形态同样落在这里（A2-d）。
    UnsetNotAllowed,  ///< 必填色槽收到「未配」：只有光标与选区两槽可缺省（裁决 7.27③）。
    SlotOutOfRange,   ///< 色板格序号越出 0..15。
    TableSizeWrong,   ///< 色板整表提交不是 16 格（装载侧对此整键回落）。
};

/// @brief 一次改动生效的传导范围（§2 表「生效档位」列的可执行形态）。
enum class ApplyScope : std::uint8_t {
    PersistOnly,        ///< 只落盘：下次会话生效、接缝待开、全仓无消费方三档都归这里。
    PersistAndApplyNow, ///< 落盘并向运行中的视口广播：该行「已接线 ∧ 即时」。
};

/// @brief 按反向核对表的两个正交列折算传导范围。
///
/// 「延后」与「下次会话」是两条正交的标签（该稿 §4 B3-a 的原句），这里折叠成**动作**而不是折叠成
/// 一个四值档位：没有消费方就没有可广播的对象，故 `Absent ∧ Immediate`（状态栏那十个开关）与
/// `Wired ∧ NextSession`（`terminal.scrollback_limit` 等三条）给出的都是「只落盘」。
/// @param control 表里的一行。
[[nodiscard]] auto apply_scope(const SettingsControl &control) noexcept -> ApplyScope;

/// @brief 一次提交的结果：是否通过、未通过的原因、通过后该广播还是只落盘。
struct CommitOutcome {
    CommitIssue issue{CommitIssue::None};      ///< 原因；`None` 即通过。
    ApplyScope scope{ApplyScope::PersistOnly}; ///< 仅当通过时可信。

    /// @brief 判是否通过（通过则值已写入并标脏）。
    [[nodiscard]] auto accepted() const noexcept -> bool {
        return issue == CommitIssue::None;
    }
};

/// @brief 装载进表单的一条键值（聚合初始化；`FormValue` 无默认构造，值必须来自具名工厂）。
struct FormEntry {
    std::string key;  ///< 落盘点号路径。
    FormValue value;  ///< 该键的当前值。
};

/// @brief 装载时的结构核对结果；三张名单都应为空，非空即「面板与 schema 分叉」的现场。
///
/// 形态照 `config::LoadReport`：未知的键只记录不报错，缺的键与错型的键记录后该键在表单里没有值，
/// 面板因此少画一个控件，而不是画一个存不回去的控件。
struct FormLoadReport {
    std::vector<std::string> unknown_keys{};  ///< 表里没有的路径。
    std::vector<std::string> missing_keys{};  ///< 表里有、但装载时没给值的路径。
    std::vector<std::string> mistyped_keys{}; ///< 给了值但形态族与该行不符的路径。

    /// @brief 判三张名单是否全空。
    [[nodiscard]] auto clean() const noexcept -> bool {
        return unknown_keys.empty() && missing_keys.empty() && mistyped_keys.empty();
    }
};

/// @brief 面板自持的那份设置副本（S3①）。
///
/// 面板打开时装载一次，之后每个控件的当前值都从这里读。本件不碰存储：落盘由面板在提交通过后显式调
/// `Store::replace()`，并在它返回空值（成功）后调 `note_persisted()`。
class SettingsForm {
public:
    /// @brief 装载表单副本。
    /// @param entries 每个落盘叶子键的当前值；次序无关，缺项与多项都记进 `load_report()`。
    explicit SettingsForm(std::vector<FormEntry> entries);

    /// @brief 装载时的结构核对结果。
    [[nodiscard]] auto load_report() const noexcept -> const FormLoadReport &;

    /// @brief 取一个键的当前值（控件显示它）。
    /// @param key 落盘点号路径。
    /// @return 命中为值指针；该键不在表里、或没有被装载时为空。
    [[nodiscard]] auto value(std::string_view key) const -> const FormValue *;

    /// @brief 判该键当前值是否与「最后一次落盘的那份」不同（要不要落盘的判据）。
    /// @param key 落盘点号路径。
    ///
    /// 按**值**判而非按提交次数判：用户把字号从 14 改成 16 再改回 14，本键不脏，于是面板关闭时不写盘，
    /// 也不会把「改了又改回来」的中间值广播给运行中的视口。
    [[nodiscard]] auto is_dirty(std::string_view key) const -> bool;

    /// @brief 全部脏键，次序照反向核对表（表内次序即面板的排版次序）。
    [[nodiscard]] auto dirty_keys() const -> std::vector<std::string>;

    /// @brief 判是否存在任何脏键（`Store::replace()` 值不值得调）。
    [[nodiscard]] auto has_unsaved_changes() const -> bool;

    /// @brief 记一次成功落盘：把「最后一次落盘的那份」推为当前值。
    ///
    /// 只在 `replace()` 返回空值后调用。它拒绝落盘时不调用，脏标记于是留着，面板可以继续提示「有改动
    /// 未保存」而不是谎报已存（`config/store.h` 文件头那条：备份未成功就不写文件）。
    auto note_persisted() -> void;

    /// @brief 取该键的传导范围。
    /// @param key 落盘点号路径。
    /// @return 该键在表里时为其范围，否则为空。
    [[nodiscard]] auto apply_scope(std::string_view key) const -> std::optional<ApplyScope>;

    /// @brief 提交一个非文本控件的值（开关、步进器、下拉、色板整表、族名链）。
    /// @param key 落盘点号路径。
    /// @param next 新值。
    /// @return 通过则值已写入并标脏；不通过则值一字未动。
    [[nodiscard]] auto commit_value(std::string_view key, FormValue next) -> CommitOutcome;

    /// @brief 提交一个文本框的内容（S14：失焦或 Enter 才调本入口）。
    /// @param key 落盘点号路径。
    /// @param text 文本框的当前内容。
    ///
    /// 按该行的域解释文本：色值行走 `ui::color_from_hex`，两档数值判良构数字，`FreeText` **原样收字符
    /// 且不做任何转义**（该稿 B2-b：断点集里引号、反斜杠、竖线都是合法内容，`connection.*` 的路径允许
    /// 含空格）。其余形态族回 `TextNotAccepted`——下拉与开关的候选由控件给出，走 `commit_value`。
    [[nodiscard]] auto commit_text(std::string_view key, std::string_view text) -> CommitOutcome;

    /// @brief 改色板的一格（A2-a：逐格点选而不是 16 个常驻输入框）。
    /// @param key 该行的路径，须为 `ColorTable` 域。
    /// @param slot 格序号 0..15。
    /// @param text 该格的 `#RRGGBB` 文本。
    [[nodiscard]] auto commit_color_slot(std::string_view key, std::size_t slot, std::string_view text)
        -> CommitOutcome;

    /// @brief 把一个可缺省的色槽置为「未配」（A2-b 的两态之一）。
    /// @param key 落盘点号路径。
    [[nodiscard]] auto commit_unset_color(std::string_view key) -> CommitOutcome;

private:
    /// @brief 四个提交入口的公共腿：查行、判只读、判形态、按域校验，通过后落值并给出传导范围。
    /// @param key 落盘点号路径。
    /// @param next 候选值（文本入口已把文本折算成值）。
    [[nodiscard]] auto store(std::string_view key, FormValue next) -> CommitOutcome;

    /// @brief 按该行的域校验候选值（区间、白名单、色板格数、必填色槽的「未配」）。
    [[nodiscard]] static auto validate(const SettingsControl &control, const FormValue &next) -> CommitIssue;

    struct Row {
        std::string key;       ///< 落盘点号路径。
        FormValue current;     ///< 面板上正在显示的值。
        FormValue persisted;   ///< 最后一次成功落盘的那份；`note_persisted()` 把它推为 `current`。
    };

    /// @brief 按路径取装载行。
    [[nodiscard]] auto find_row(std::string_view key) -> Row *;

    /// @brief 按路径取装载行（常量形态）。
    [[nodiscard]] auto find_row(std::string_view key) const -> const Row *;

    std::vector<Row> rows_{};  ///< 只含装载成功且形态相配的键，次序照反向核对表（表内次序即排版次序）。
    FormLoadReport report_{};
};

}  // namespace borealis::ui
