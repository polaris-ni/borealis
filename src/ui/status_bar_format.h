#pragma once

// ============================================================
// 状态栏的排布纯逻辑件（src/ui/status_bar_format.h）
// ------------------------------------------------------------
// 只做一件能被全量断言的事：给定「注册次序 + 每格占位 + 可用宽度」，算出哪几格上屏、哪几格被省略。
// 判据来自 `codespec/UI_OVERVIEW.draft.md` §4 的 N3（并入裁决 7.25）：缺省全开；窗口不足以容纳已开
// 条目时按**注册次序从右向左省略**、不做字号压缩——所以本件的形参里没有任何字号档位，只有宽度；
// 也所以保住的永远是「注册次序的一个前缀」而不是「任意子集」：允许跳着放就会让同一格在换窗时忽现忽隐。
//
// 不含 `aurora` 类型（`session_monitor_format` / `serial_format` / `keys_format` 同族先例，
// AGENTS §4.4 第 20 条要的「纯逻辑层可独立单测」）：每格多宽由调用方测好交进来，本件不调字体。
//
// 两格的身份不是本件的事：哪一格带圆点、哪一格前面画分隔线，都是绘制侧的形态，折算成
// `prefix_dp` 交进来即可。被省略项的去向（N3 那句「进右键菜单」）在另一条腿上，本件只交出那份集合。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace borealis::ui {

/// @brief 一格在状态栏里的占位测量。
struct StatusBarSlotMeasure {
    /// @brief 该格的身份 = 配置 leaf 名去掉 `show_` 前缀（`connection` / `encoding` / …）。
    ///
    /// 用串而不是枚举：注册次序的唯一来源是 `include/borealis/config/settings.h` 的字段次序，
    /// 将来给某格补数据源（重连倒计时、光标行列、输入延迟）不该改本件。
    std::string_view id;
    double text_width_dp = 0.0;  ///< 文本实测宽（逻辑 dp）。
    double prefix_dp = 0.0;      ///< 该格左边的附加物宽（圆点、分隔线），**不含**与左邻的格间距。

    /// @brief 逐字段全等比较（用例里比对整份测量表用）。
    [[nodiscard]] auto operator==(const StatusBarSlotMeasure &other) const noexcept -> bool = default;
};

/// @brief 一次排布的可调几何（逻辑 dp）。
struct StatusBarGeometry {
    double left_inset_dp = 16.0;   ///< 首格前的内缩（与广播横幅的 16 同档）。
    double gap_dp = 20.0;          ///< 相邻两格之间的间距。
    double right_inset_dp = 16.0;  ///< 末格（或右端固定件）之后的内缩。
    double reserved_right_dp = 0.0; ///< 右端固定件及其左侧间距的总宽（命令面板锚点那一格）。

    /// @brief 逐字段全等比较。
    [[nodiscard]] auto operator==(const StatusBarGeometry &other) const noexcept -> bool = default;
};

/// @brief 排布结果。
struct StatusBarPlan {
    /// @brief 上屏格的身份，**保持注册次序**。
    std::vector<std::string_view> shown;
    /// @brief 被省略格的身份，**保持注册次序**（右键菜单那一腿按这份取文案）。
    std::vector<std::string_view> omitted;
    /// @brief `shown` 实际占掉的宽度，含左内缩与各格前缀与格间距，不含右端固定件与右内缩。
    double content_width_dp = 0.0;
    /// @brief 右端固定件被让掉的那一帧为真：连锚点都放不下时锚点不画，格子该给的宽度仍然不给。
    bool right_anchor_dropped = false;
};

/// @brief 按注册次序取最长的放得下的前缀，余下的进 `omitted`。
///
/// 判据（N3）：一格「放得下」指的是它连同自己的前缀与与左邻的间距一起放得下，且右端固定件
/// 与右内缩仍然有位置。没有任何一格会被截短、也没有任何字号参与运算。
/// @param slots 已按注册次序排好的测量（空文本的格**不该出现在这里**，由调用方滤掉）。
/// @param available_dp 状态栏整条的可用宽度（逻辑 dp，通常等于窗口客户区宽）。
/// @param geometry 内缩、间距与右端固定件的占位。
/// @return 上屏/省略两份身份表与实占宽度。
[[nodiscard]] auto plan_status_bar(const std::vector<StatusBarSlotMeasure> &slots,
                                   double available_dp,
                                   const StatusBarGeometry &geometry = {}) -> StatusBarPlan;

/// @brief 注册次序的唯一来源：`config::StatusBarSettings` 的字段次序（十枚开关，缺省全开）。
///
/// 本件因此不认识 `config` 类型（那是 `src/config/` 的依赖面），只把那份次序以串表交出；
/// 装配层按这份次序造 `StatusBarSlotMeasure`，配置里关掉哪一枚就少交哪一格——次序与开关
/// 因此不会漂成两份账。
[[nodiscard]] auto status_bar_registration_order() -> const std::vector<std::string_view> &;

/// @brief 整数按三位一组插千分位逗号：`10000` → `10,000`。
///
/// 稿屏 1 那一格逐字写作 `scrollback 10,000`（`UI_WORKSPACE.draft.svg` 的状态栏文本），而分组本身
/// 不是可译短语（千分位在两种语言里同一形态），故不占词条表的格子。分隔符是 ASCII 逗号。
/// @param value 待分组的非负整数。
/// @return 分组后的十进制串。
[[nodiscard]] auto group_thousands(std::uint64_t value) -> std::string;

/// @brief 字号档的显示形：整档不带小数（`14.0` → `14`），半档留一位（`13.5` → `13.5`）。
///
/// 取值域是 `appearance.font_size_pt` 的 `[6, 72]`（`settings_catalog` 那一行），故十分位就是全部
/// 需要的精度——稿屏 1 画的是 `14 pt`，把 `14.0` 原样上屏就是自造一个稿上没有的形态。
/// @param pt 字号（磅）。
/// @return 不含单位后缀的数字串（后缀走词条 `status_bar.font`）。
[[nodiscard]] auto format_font_size(double pt) -> std::string;

}  // namespace borealis::ui
