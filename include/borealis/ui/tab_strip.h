#pragma once

// ============================================================
// 标签列表：多标签的顺序、选中与三名来源的折算（include/borealis/ui/tab_strip.h）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.01` 的「新建 / 关闭 / 切换 / 重排（拖拽）/ 重命名」里与绘制、与会话无关的那半：
// 顺序本身、当前选中的那一格，以及这一格**该显示哪个名字**。框架 `TabBar` 只覆盖选中切换与
// 关闭（附录 A.1：事件只有 `on_change` / `on_close`，配色还是全局的），所以本件不是包的壳，
// 而是重排、逐标签名称与三名优先级这三处的唯一真值源；栏位宽、图标与关闭按钮的命中都在绘制侧。
//
// 名字有三个来源（`SPEC.FEAT.TERM.07` 的标题消费链路）：连接档案的显示名、`OSC 0/2` 设的标题、
// 用户手动重命名。该条把「手动重命名的优先级更高」一句标了「（可配）」，故优先级是入参而非写死；
// 枚举的**唯一定义处在本头**，`config::TabNamePriority` 是其别名（`RightClickAction` 与
// `PasteNewlinePolicy` 的同一条单一真值源口径，裁决 7.41① / 7.33）——`config` 已含 `ui/palette.h`，
// 本头反向含 `config/settings.h` 就是模块环（架构 §2.3）。本头因此不含 Aurora 也不含 `config` 类型。
//
// 三条不在需求原文里、却决定形态的口径（细则见裁决 7.43）：
// - **空串是「该来源未设置」而非「名字就是空」**：于是 `OSC` 把标题设回空、用户撤销重命名，都是
//   同一句「让位给下一级来源」，不必另立「重置为默认名」这个动作。
// - **重排的目标下标以「其余标签」为基准**：指针落在两格之间时算出的插入位天然不含被拖那格，
//   以原表下标为基准就会在「往上拖」与「往下拖」两个方向上差一格——那是拖拽重排最常错的地方。
// - **末位标签不归本件关**（与 7.42⑤ 的 pane 同口径）：关掉最后一个标签就是关窗口，属
//   `SPEC.FEAT.WS.03`；本件若允许清空，装配层就得自己判「空了没有」，两处各判必分叉。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace borealis::ui {

/// @brief 一个标签的身份，由装配层分配（会话句柄以同一值为键）。
///
/// 刻意由调用方给定而非表内自增：重排会移动位置，而会话句柄不能跟着漂（`ui::PaneId` 同因）。
using TabId = std::uint64_t;

/// @brief 手动重命名与 `OSC` 标题谁优先（配置键 `appearance.tab_name_priority`）。
///
/// 两个取值都对应真实的用户预期，故不是一处「实现差异」：全屏 TUI（`vim` / `htop`）会一路重写
/// 标题，此时会话报的名字才是权威；而把标题当自己地盘的用户不希望切个目录标签就改名。
enum class TabNamePriority : std::uint8_t {
    ManualWins,  ///< 手动重命名压过 `OSC` 标题（缺省，`SPEC.FEAT.TERM.07` 的「优先级更高」）。
    OscWins,     ///< `OSC` 标题压过手动重命名。
};

/// @brief 一个标签在三个名字来源上的当前内容。
///
/// 空串即「该来源未设置」，故 `default_name` 为空是装配层还没给出档案名，此时 `resolve_tab_name`
/// 回空串而不造占位文案——占位文案属绘制侧（且要随语言词条走）。
struct TabNames {
    std::u32string default_name;  ///< 连接档案的显示名（shell / host / 端口等由装配层折成一句话）。
    std::u32string osc_title;     ///< `OSC 0/2` 最近一次设置的标题。
    std::u32string manual_name;   ///< 用户手动重命名的结果。

    /// @brief 逐字段全等比较（名称折算断言用）。
    [[nodiscard]] auto operator==(const TabNames &other) const noexcept -> bool = default;
};

/// @brief 一格标签：身份与三个名字来源，别的都不在（本件不持会话、不持图标与角标）。
///
/// 图标与活动状态属 `SPEC.FEAT.WS.04` 与连接族，「有运行中进程」的判据属会话侧，都不进本件——
/// 一旦在这里存了会话指针，标签列表就再也无法脱离会话单测（AGENTS.md §4.4 第 20 条）。
struct Tab {
    TabId id = 0;
    TabNames names{};
    bool bell_triggered = false;   ///< BEL 触发标记（`SPEC.FEAT.WS.04`）：主线程每帧取走后清零。
    bool has_activity = false;     ///< 是否有新活动（输出更新）：用于活动高亮指示。

    /// @brief 逐字段全等比较（顺序断言用）。
    [[nodiscard]] auto operator==(const Tab &other) const noexcept -> bool = default;
};

/// @brief 三个名字来源按优先级折成一个显示名。
///
/// 纯函数，且是本件唯一的判定点：`TabStrip` 的显示名入口只是「按 id 取出 `TabNames` 再转调本函数」，
/// 于是优先级与「空串即让位」两条口径可在不构造列表的地方逐条断言。
/// @param names 该标签的三个来源。
/// @param priority 手动与 `OSC` 的先后；档案名恒在最后，它是「会话从没报过名字」时的兜底。
/// @return 优先级次序上第一个非空的来源；三者皆空回空串。
[[nodiscard]] auto resolve_tab_name(const TabNames &names, TabNamePriority priority) -> std::u32string;

/// @brief 多标签的顺序、选中与名称折算。
///
/// 不变量：表的次序就是标签自左而右的次序（拖拽重排改的就是它）；**空表是合法状态**（装配层在
/// 建首个标签之前会经历），此时选中为空；非空时选中位恒合法，且切分动作都不留下「没有选中」的
/// 中间态——绘制侧不必判空（除首帧外）。
class TabStrip final {
  public:
    /// @brief 当前标签数。
    [[nodiscard]] auto count() const noexcept -> std::size_t;

    /// @brief 全部标签，按当前次序（自左而右）。
    ///
    /// 出快照而非引用：绘制侧要按序铺栏位，而重排会移动下标；给引用会让调用方持着下标跨一次 `move`。
    [[nodiscard]] auto tabs() const -> std::vector<Tab>;

    /// @brief 该标签是否还在表里（会话退出后装配层要先问这句再关）。
    [[nodiscard]] auto has_tab(TabId id) const -> bool;

    /// @brief 该标签的下标（0 起，自左而右）；不存在回空值。
    [[nodiscard]] auto index_of(TabId id) const -> std::optional<std::size_t>;

    /// @brief 在表尾追加一个标签并**立即选中**它。
    ///
    /// 新建即切换是用户对「开一个新标签」的预期（下一秒就要在里面打字），故不留给调用方补一句
    /// `select`——补与不补会造成两种手感。`id` 已存在时返回 false 且表不变。
    /// @param id 标签身份，由调用方保证是全新的。
    /// @param default_name 连接档案的显示名（可为空，见 `TabNames`）。
    /// @return 是否完成追加。
    auto add(TabId id, std::u32string default_name) -> bool;

    /// @brief 在指定位置插入一个标签并**立即选中**它（WS.10 撤销关闭用）。
    ///
    /// 与 `add()` 的唯一差别是位置可控：重开的标签应插在关闭前的位置，而不是追加到末尾。
    /// @param index 目标位置（`[0, count()]`，等于 `count()` 时等价于追加）。
    /// @param id 标签身份。
    /// @param default_name 连接档案的显示名。
    /// @return 是否完成插入；`id` 已存在或越界时返回 false。
    auto insert_at(std::size_t index, TabId id, std::u32string default_name) -> bool;

    /// @brief 关闭一个标签；关掉的是选中的那格时，选中交给次序上的下一格（末位则交给上一格）。
    ///
    /// 交接下标在**移除前**算，与 `PaneTree::close`（7.42⑥）同一条理由：移除后次序会变，而用户
    /// 预期是「交给原来紧跟着的那一格」。
    /// @param id 待关闭的标签。
    /// @return 是否完成关闭；表里只剩这一个时返回 false（那是关窗口，属 `SPEC.FEAT.WS.03`）。
    auto close(TabId id) -> bool;

    /// @brief 最近一次关闭的标签信息（供 WS.10 撤销关闭用）。
    ///
    /// 只含名称与位置，不含连接规格——后者由装配层从会话侧取并压入闭包栈。
    /// @return 若有关闭记录则返回 {name, index_in_strip}，否则 nullopt。
    struct LastClosedInfo {
        std::u32string name;
        std::size_t index_in_strip = 0;
    };
    [[nodiscard]] auto last_closed() const -> std::optional<LastClosedInfo>;

    /// @brief 当前选中的标签；空表回空值。
    [[nodiscard]] auto selected() const -> std::optional<TabId>;

    /// @brief 直接指定选中（点标签栏某格时用；键盘循环走 `select_relative`）。
    /// @param id 目标标签。
    /// @return 该标签是否存在；不存在时选中位不变。
    auto select(TabId id) -> bool;

    /// @brief 相对当前选中循环切换（`Ctrl+Tab` / `Ctrl+Shift+Tab`）。
    ///
    /// 刻意**首尾相连**而非撞到端点就停：这两个键是按住修饰连按的，停在端点会让连按失效，
    /// 而绕到另一端正是用户在浏览器与 Windows Terminal 里建立的预期。
    /// @param step 前进格数，负数即反向；0 表示原地。
    /// @return 是否发生切换（空表与 `step` 为 0 时返回 false 且选中位不变）。
    auto select_relative(int step) -> bool;

    /// @brief 重排：把 `id` 那格挪到**其余标签**的第 `to_index` 格（拖拽松手落点折成的下标）。
    ///
    /// 下标基准见文件头第二条口径，取值范围 `[0, count()-1]`，越界返回 false 且表不变。
    /// @param id 被移动的标签。
    /// @param to_index 目标下标（不含被拖那格）。
    /// @return 是否完成移动（原地落点也算完成，表不变）。
    auto move(TabId id, std::size_t to_index) -> bool;

    /// @brief 手动重命名；空串即撤销重命名，让位给下一级来源。
    /// @param id 目标标签。
    /// @param name 新名字。
    /// @return 该标签是否存在；不存在时名字不变。
    auto rename(TabId id, std::u32string name) -> bool;

    /// @brief 落 `OSC 0/2` 的标题（状态机侧的产物经会话取回后交这里）；空串即该会话没设标题。
    /// @param id 目标标签。
    /// @param title 标题原文。
    /// @return 该标签是否存在；不存在时名字不变。
    auto set_osc_title(TabId id, std::u32string title) -> bool;

    /// @brief 标记该标签触发了 BEL（`SPEC.FEAT.WS.04`）。
    /// @param id 目标标签。
    /// @return 该标签是否存在；不存在时状态不变。
    auto mark_bell_triggered(TabId id) -> bool;

    /// @brief 消费并重置该标签的 BEL 触发标记（帧边界调用）。
    /// @param id 目标标签。
    /// @return 自上次调用以来是否触发过 BEL。
    auto take_bell_triggered(TabId id) -> bool;

    /// @brief 标记该标签有新活动（输出更新），用于活动高亮指示（`SPEC.FEAT.WS.04`）。
    /// @param id 目标标签。
    /// @return 该标签是否存在；不存在时状态不变。
    auto mark_activity(TabId id) -> bool;

    /// @brief 消费并重置该标签的活动标记（焦点切换到该标签时调用）。
    /// @param id 目标标签。
    /// @return 自上次调用以来是否有活动。
    auto take_activity(TabId id) -> bool;

  private:
    std::vector<Tab> tabs_;
    std::size_t selected_index_ = 0;  ///< 选中位在表内的下标；`tabs_` 非空时恒合法，空表时不生效。
    std::optional<LastClosedInfo> last_closed_;  ///< 最近一次关闭的标签信息（WS.10 撤销用）。
};

}  // namespace borealis::ui
