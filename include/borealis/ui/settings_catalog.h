#pragma once

// ============================================================
// 设置面板的 schema 反向核对表（include/borealis/ui/settings_catalog.h）
// ------------------------------------------------------------
// `codespec/UI_SETTINGS.draft.md` §2 那张表的可执行形态：`config::Settings` 的每一个**落盘叶子键**
// 在这里恰有一行，每行给「页 / 控件形态 / 取值域 / 消费方现状 / 生效档位」五列（该稿 §8 的判据①）。
// 它的价值不是给面板省掉一段 switch，而是让「面板画的键」与「schema 里真有的键」在结构上不可能分叉：
// 证人 `tests/unit/utest_settings_catalog.cpp` 拿 `Store::replace()` 真正写出的 JSON 做基准双向比对，
// 并把表里声明的取值域**逐键交给装载侧复核**（域内值必须无留痕、域外值必须留痕）。
//
// 三条刻意形态：
// ① 本头不含 Aurora 类型，也**不含 `config` 类型**——`config/settings.h` 已经 include `ui/palette.h`
//     等件，反向 include 即 `config ⇄ ui` 模块环（`RightClickAction` / `TabNamePriority` 同一条理由）。
//     于是本表只能用**落盘点号路径**指代键，而不是用成员指针或键的枚举。
// ② 路径与取值域逐字照抄 `src/config/store.cpp`：路径即 `LoadReport::rejected_keys` 用的那套点号路径，
//     区间即 `scope.real(...)` / `scope.integer(...)` 实际把守的那两个数（裁决 7.46② 的分工：域由装载侧
//     判，面板与绘制侧都不夹取）。面板因此不产生第二个真值源。
// ③ 本表不含**显示文案**。词条归本仓 `StringTable` 的「命令 id / 键路径 → 词条」映射（裁决 7.25⑬），
//     把中文标签写进纯逻辑表就是让面板绕过词条表。
//
// 单位（pt / × / dp）与页内区段的分组也不在本表：本棒只交付 §8 要求的五列，面板落期若要按区段排版，
// 那时再补字段并由同一批用例把守，不留「有字段无证人」的空位。
// ============================================================

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace borealis::ui {

/// @brief 设置面板的页（裁决 7.26① 的四分类，与 `config::Settings` 的四个域一一对应）。
enum class SettingsPage : std::uint8_t {
    Appearance,  ///< 外观页。
    Terminal,    ///< 终端页。
    Connection,  ///< 连接页。
    Shortcuts,   ///< 快捷键页。
};

/// @brief 控件形态。延后与灰置的处置**不在本枚举里**（那由 `consumer` 给出，见 S15），
///          否则「同一件事的两种表达」会在表里自相矛盾。
enum class ControlKind : std::uint8_t {
    ThemePicker,      ///< 主题卡（8 张 + 「任一槽被改过」的自定义状态，S6）。
    SwatchGrid,       ///< 16 格色板，逐格点选而非 16 个常驻输入框（A2-a）。
    HexInput,         ///< 单个 HEX 输入（校验式即 `ui::color_from_hex`，S14）。
    OptionalHexInput, ///< HEX 输入 + 「未配」档：裁决 7.27③ 的两态在界面上的兑现（A2-b）。
    Toggle,           ///< 开关。
    NumberStep,       ///< 数值步进器，区间取自本行的取值域。
    Dropdown,         ///< 有限档位的下拉。
    FontDropdown,     ///< 等宽字体族下拉：候选来自装配阶段那一份目录，不在本表里（S16）。
    FamilyList,       ///< 有序族名重排列表 + 末尾「添加族」下拉（S8）。
    TextInput,        ///< 自由文本输入（含原样收字符、不做转义的断点集，B2-b）。
    ReadOnlyTable,    ///< 只读表（S9：快捷键页首版不给会失灵的按钮）。
};

/// @brief 取值域的类型。**这一列的实质是「装载侧把不把守」**：`Choice` 与两档数值由装载侧判域，
///          `FreeText` 只判类型——于是它决定面板能否对用户输入硬校验（`terminal.encoding` 的
///          六个名字是建议项，照 `Choice` 那样校验就把「先接 GB18030」这类取值挡在面板外）。
enum class ValueDomain : std::uint8_t {
    Boolean,    ///< 二值。
    Integral,   ///< 整数，闭区间见 `numeric`，装载侧把守。
    Real,       ///< 实数，闭区间见 `numeric`，装载侧把守。
    Choice,     ///< 白名单，`choices` 逐字等于落盘文本名，装载侧把守。
    ColorText,  ///< `#RRGGBB`；**alpha 不经配置往返**，故面板不得给 alpha 位（A2-d）。
    ColorTable, ///< 16 个 `#RRGGBB`；元素数不足或任一格形态不合则整键回落。
    FreeText,    ///< 装载侧只判类型不判域。此时 `choices` 若给出，是**建议项**而非白名单。
    FamilyChain, ///< 有序族名数组；长度上限由框架截断并留痕，本表不写那个数（公共头不含框架常量）。
    OverrideMap, ///< `[{command, combo}]` 数组形态（命令 id 里的点会被框架路径模型拆开，裁决 7.27②）。
};

/// @brief 消费方现状（§2 表「消费方现状」列的三档）。
enum class ConsumerStatus : std::uint8_t {
    Wired,        ///< 已有消费方在取这个值。
    SeamPending,  ///< 有能取值的对象但接缝未开：判据文 §7 点名的三条会话侧注入。
    Absent,       ///< 有键、全仓无消费方：画控件、显示当前值、灰置 + 「延后」角标（S15 的第一档）。
};

/// @brief 生效档位（§2 表「生效档位」列；与「有无消费方」是**两条正交**的事实，
///          B3-a 的「`encoding` 同时挂延后与下次会话两角标」即由两列各自给出）。
enum class EffectLevel : std::uint8_t {
    Immediate,   ///< 即时：本棒开的运行期更新入口（S4）之后可见。
    NextSession, ///< 下次会话：构造期取用，运行期改动不重放既有会话（判据文 §0 的物理边界②）。
};

/// @brief 数值闭区间；两端照抄装载侧把守的数，不是面板自定的钳位。
struct NumericDomain {
    double min{};
    double max{};
};

/// @brief 一行＝一个落盘叶子键在面板上的形态。
struct SettingsControl {
    std::string key;             ///< 点号路径，与落盘键名逐字相同（亦即 `rejected_keys` 里的形态）。
    SettingsPage page{};         ///< 归属页；必须与路径首段的域一致。
    ControlKind kind{};          ///< 控件形态。
    ValueDomain domain{};        ///< 取值域类型。
    NumericDomain numeric{};     ///< 仅 `Integral` / `Real` 有意义。
    std::vector<std::string> choices{};  ///< `Choice` 是白名单；`FreeText` 是建议项；其余为空。
    ConsumerStatus consumer{};   ///< 消费方现状。
    EffectLevel effect{};        ///< 生效档位。

    /// @brief 判是否数值键（决定 `numeric` 是否可信）。
    [[nodiscard]] auto is_numeric() const noexcept -> bool {
        return (domain == ValueDomain::Integral) || (domain == ValueDomain::Real);
    }
};

/// @brief 全表：`config::Settings` 的每一个落盘叶子键恰一行。
///
/// 表内顺序即面板的排版顺序（同页内按本表次序铺设），故新增键必须插在该域的正确区段里，
/// 而不是追加在表尾。
[[nodiscard]] auto settings_catalog() -> const std::vector<SettingsControl> &;

/// @brief 按点号路径取一行。
/// @param key 落盘点号路径。
/// @return 命中行；路径不在 schema 内时为空指针（调用方据此报错而不是静默造一行）。
[[nodiscard]] auto find_settings_control(std::string_view key) -> const SettingsControl *;

}  // namespace borealis::ui
