#pragma once

// ============================================================
// OSC 133 命令块记录（include/borealis/term/command_block.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTEG.01`：把 `A/B/C/D` 四个边界标记攒成一份有界的块历史，供「复制上一命令
// 的全部输出」、失败命令输出行高亮与悬停分隔线消费。解析与游标换算不在这里——`Terminal`
// 负责把视口游标折成**绝对行**再喂进来，本文件只做状态推进与只读查询（纯逻辑，可脱界面单测）。
//
// 三条决定形态的 WHY：
//
//   1. **存绝对行而不是存储行 / 视口行**：`grid::Storage` 的存储行号会随 scrollback 溢出整体
//      前移，视口行号会随上下滚动改变；只有 `storage_index + dropped_lines` 在同一份内容存活
//      期间恒定（裁决 7.38⑤ / 7.39② 的同一坐标系）。块表因此不受回看、resize 与溢出影响，
//      界面每次消费时用当下的 `dropped_lines` 折回存储行即可。
//   2. **`P` 不是输出起点**：OSC 133 的 `P` 是 Prompt Part（右提示符分段），`C` 才是输出起点。
//      把 `P` 当输出起点会让复制出来的第一行带上提示符，正是需求要排除的内容（裁决 7.123 D2①）。
//      本记录件只认 `A/B/C/D`，`PromptPart` 一律忽略。
//   3. **容量有界、序号不复用**：命令块由远端字节流驱动，不封顶就是让一条输出吃掉内存；
//      满表淘汰最旧。序号递增且清空后继续递增，避免界面手里的旧序列号在清空后指到新块。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "borealis/term/osc.h"

namespace borealis::term {

/// @brief 命令块表的容量上限。
inline constexpr std::size_t kMaxCommandBlocks = 256U;

/// @brief 一条命令块的行区间与退出码。
///
/// 行号全部是**绝对行**（`storage_index + dropped_lines`）；列号是 0 基视口列。
/// `PromptStart` 开块，`PromptEnd` 记输入区起点，`CommandStart` 开输出起点，
/// `CommandExecuted` 收块。未收 `D` 的块 `completed == false`，被新 `A` 顶掉时 `superseded == true`。
struct CommandBlock {
    std::uint64_t serial = 0;             ///< 递增序号；清空后继续递增，不复用。
    std::int64_t prompt_row = 0;          ///< `A` 时刻的绝对行（提示符首行）。
    std::int64_t prompt_end_row = 0;      ///< `B` 时刻的绝对行；无 `B` 时等于 `prompt_row`。
    std::int64_t output_start_row = 0;    ///< `C` 时刻的绝对行。
    std::size_t output_start_column = 0;  ///< `C` 时刻的列。
    std::int64_t output_end_row = 0;      ///< `D` 时刻的绝对行。
    std::size_t output_end_column = 0;    ///< `D` 时刻的列。
    std::int32_t exit_code = 0;           ///< `D` 的退出码；`has_exit_code == false` 时无意义。
    bool has_output_start = false;        ///< 是否收到过 `C`。
    bool has_exit_code = false;           ///< `D` 是否带出了可解析的数字。
    bool completed = false;               ///< 是否收到过 `D`。
    bool superseded = false;              ///< 未收 `D` 就被新 `A` 顶掉。

    /// @brief 逐字段全等比较（界面每帧比较块表变化、测试断言用）。
    [[nodiscard]] auto operator==(const CommandBlock &) const noexcept -> bool = default;

    /// @brief 块在绝对行空间里的末行（含）。
    ///
    /// `D` 在列 0 表示「命令以换行结尾」，那一行是下一条命令的提示符，不属于本块；列 > 0 时
    /// 该行前半段是输出，算在块内。开放块只覆盖已知的提示符 / 输出起点行，末尾未知时不外推。
    [[nodiscard]] auto last_row() const noexcept -> std::int64_t;

    /// @brief 是否有一条非空的 `C → D` 输出区间可供复制。
    ///
    /// `D` 在列 0 且与 `C` 同行时区间为空（命令无输出），不出现菜单条目；列 > 0 时只取半行。
    [[nodiscard]] auto has_copyable_output() const noexcept -> bool;

    /// @brief 是否失败：已完成、带退出码且退出码非 0（裁决 7.123 D11① / D12①）。
    [[nodiscard]] auto failed() const noexcept -> bool;
};

/// @brief 按 `D` 的参数解析退出码：可选 `+`/`-`、至少一位数字、越界钳到 `int32` 边界。
///
/// 只读第一个分号前的字段（`D;1;foo` 取 `1`）；空字段、纯符号、夹非数字都回 `std::nullopt`——
/// 「无码」与「码为 0」必须分得开，失败高亮与复制可用性都依赖这一条。
/// @param text `133;D` 之后的参数段。
/// @return 解析出的退出码；无有效数字时为 `std::nullopt`。
[[nodiscard]] auto parse_command_exit_code(std::u32string_view text) noexcept
    -> std::optional<std::int32_t>;

/// @brief 命令块状态机：按 `A/B/C/D` 推进一份有界块历史。
///
/// 只保留**最近一次**开放块（`A` 之后尚未收到 `D` 的那条）；`B/C/D` 落到这条上，孤立标记忽略。
/// 新 `A` 会把旧的开放块标 `superseded`，不参与复制与高亮。`PromptPart`（`P`）不产块、不改块。
class CommandBlockTracker {
  public:
    /// @brief 喂一个边界标记。
    /// @param marker 边界种类；`PromptPart` / `None` 被忽略。
    /// @param row 标记时刻的绝对行。
    /// @param column 标记时刻的列。
    /// @param exit_code `CommandExecuted` 的退出码；`std::nullopt` 表示 `D` 未带数字。
    auto on_marker(PromptMarker marker, std::int64_t row, std::size_t column,
                   std::optional<std::int32_t> exit_code = std::nullopt) -> void;

    /// @brief 当前块表（由旧到新）。
    [[nodiscard]] auto blocks() const noexcept -> const std::vector<CommandBlock> & { return blocks_; }

    /// @brief 最近一条已完成且未被顶掉的块；没有时回 `nullptr`。
    [[nodiscard]] auto last_completed() const noexcept -> const CommandBlock *;

    /// @brief 包含绝对行 @p row 的块；同一行被新旧两块共享时取较新的一条。
    ///
    /// 开放块只覆盖已知行，不向未来外推；`superseded` 块不参与命中。
    /// @param row 绝对行。
    /// @return 命中的块；没有时回 `nullptr`。
    [[nodiscard]] auto block_at(std::int64_t row) const noexcept -> const CommandBlock *;

    /// @brief 清空全部块（`ED 3` / RIS）；序号继续递增，避免旧序列号指到新块。
    auto clear() noexcept -> void;

    /// @brief 当前块数。
    [[nodiscard]] auto size() const noexcept -> std::size_t { return blocks_.size(); }

  private:
    std::vector<CommandBlock> blocks_;
    std::uint64_t next_serial_ = 1;
    bool open_ = false;
};

}  // namespace borealis::term
