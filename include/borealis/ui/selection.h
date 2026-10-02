#pragma once

// ============================================================
// 选区归一与选中文本（include/borealis/ui/selection.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.02` 首版交付面（流式拖拽 + 列模式矩形块）与 `SPEC.FEAT.INTERACT.03`
// 的复制腿里「与绘制无关、纯算得清」的那半：两个端点归一成逐行的列区间，再由列区间取文本。
// 高亮矩形（`rect_for` 已有）与鼠标 → 格子的换算归绘制侧，本件刻意不知道像素。
//
// 不含 Aurora 类型（AGENTS.md §4.4 第 20 条）：选中文本的正确性——双宽字符不产半个字、
// 组合符号随基础码点、三项复制变换的先后次序——必须能脱开界面全量单测。
//
// 行号坐标是**调用方给定的存储行序**（`grid::Storage::line` 的索引，0 为最顶），本件不拥有
// 也不换算它。理由有二：① 权威网格由后台读线程持有，主线程只有可见区副本（架构 §3.4），
// 跨可见区的复制必须在会话锁内以存储行序取行；② 副本的行序随回看偏移变，选区若按副本行序
// 存就会在滚动时漂移。于是「选区随 scrollback 滚动跟随」由坐标空间本身表达，本件保持无状态。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "borealis/grid/storage.h"

namespace borealis::ui {

/// @brief 选区端点：一个格子坐标，行号与列号都**含端点**。
struct GridCellPos {
    std::size_t row = 0;
    std::size_t column = 0;

    /// @brief 逐字段全等比较（「单击即无选区」的判据就是两端点重合）。
    [[nodiscard]] auto operator==(const GridCellPos &other) const noexcept -> bool = default;
};

/// @brief 选区形状（`SPEC.FEAT.INTERACT.02` 的「流式」与「列模式」两态）。
enum class SelectionShape : std::uint8_t {
    Stream,  ///< 流式：首尾两行按端点列截断，中间行整行入选。
    Block,   ///< 列模式：每行取同一列区间（矩形块）。
};

/// @brief 一次选区：两个端点与形状。
///
/// 端点次序无所谓（拖拽方向可上可下、可左可右），归一在 `row_spans` 里做；两终点重合即
/// 「单击」，按 `row_spans` 的口径不成选区。
struct Selection {
    GridCellPos anchor{};  ///< 按下鼠标的那一格。
    GridCellPos focus{};   ///< 当前拖到 / 停在的那一格。
    SelectionShape shape{SelectionShape::Stream};

    /// @brief 逐字段全等比较（用例与「选区是否变了」的判据）。
    [[nodiscard]] auto operator==(const Selection &other) const noexcept -> bool = default;
};

/// @brief 一行被选中的列区间（闭开区间 `[first_column, last_column)`）。
struct RowSpan {
    std::size_t row = 0;
    std::size_t first_column = 0;
    std::size_t last_column = 0;

    /// @brief 逐字段全等比较。
    [[nodiscard]] auto operator==(const RowSpan &other) const noexcept -> bool = default;
};

/// @brief 把选区归一成按行号升序的列区间表。
///
/// 列端点按 @p columns 截断（越界是真实输入：改窄后的旧选区）。行号不在此处校验——行数不
/// 是本调用的入参，且 `copy_text` 与绘制侧各有自己的取行边界。
/// @param selection 选区。
/// @param columns 网格列数；为 0 时不出区间。
/// @return 行号升序的区间表；`anchor == focus`（单击）时为空表。
[[nodiscard]] auto row_spans(const Selection &selection, std::size_t columns) -> std::vector<RowSpan>;

/// @brief 复制文本的三项变换开关（`SPEC.FEAT.INTERACT.03`，**均默认关闭以保留原样**）。
///
/// 生效次序固定为「剥离 tmux 边框字符 → 剥离行尾空白 → 合并续行」：边框字符若在行尾会挡住
/// 后两条的判定，故先剥；行尾空白若在反斜杠之后，`foo\   ` 这类续行就永远合不上，故空白先于
/// 续行。调用方从 `config::TerminalSettings` 的三个同名键逐字段搬过来（裁决 7.32②）。
struct CopyOptions {
    bool trim_trailing_space{false};       ///< 剥离每行行尾空白。
    bool smart_line_join{false};           ///< 跨行反斜杠续行智能合并。
    bool strip_tmux_border_chars{false};   ///< 去除 tmux 分屏边框字符。

    /// @brief 逐字段全等比较。
    [[nodiscard]] auto operator==(const CopyOptions &other) const noexcept -> bool = default;
};

/// @brief 取选区的文本：逐行取选中列区间，按 @p options 变换后以 LF 分隔拼接。
///
/// 三点口径（裁决 7.32）：① 行分隔符是 LF 而非所选平台的剪贴板惯例，落剪贴板一侧不翻译；
/// ② 选区左界落在双宽字符的延续格上时**纳入整字符**（指针只能指到那半格，用户要的是那个字）；
/// ③ 行号越界（历史被新输出挤掉）的行不产文本，也不占一个空行。
/// @param storage 权威网格（调用方须持会话锁访问）。
/// @param selection 选区，行号即 `storage` 的行序。
/// @param options 三项复制变换。
/// @return 选中文本；无选中内容时为空串。
[[nodiscard]] auto copy_text(const grid::Storage &storage, const Selection &selection, const CopyOptions &options)
    -> std::string;

}  // namespace borealis::ui
