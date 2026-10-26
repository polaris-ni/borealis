#pragma once

// ============================================================
// Quick Terminal 的纯逻辑面（include/borealis/ui/quake.h）
// ------------------------------------------------------------
// `SPEC.FEAT.WS.12` 里与「运行期窗口开关」无关、且框架回货后不必重写的两半：贴边目标矩形
// 折算与显隐意图状态机。显隐/置顶两腿今天没有框架入口，登记为缺口 G47（任务书
// `codespec/FRAMEWORK_TASK_WS12_QUAKE.md`），装配腿因此挂起——本头是它的输入与输出契约。
// 公共头不含框架类型（AGENTS §2）：几何用本仓自有的单位显式命名结构，字段名把单位写死。
// ============================================================

#include <chrono>
#include <cstdint>
#include <optional>

namespace borealis::ui {

/// @brief 帘子贴哪条边；`Top` 是下拉式（Guake / Yakuake 缺省档），其余三边是 Tilda 的可配边。
enum class QuakeAnchor : std::uint8_t {
    Top,
    Bottom,
    Left,
    Right,
};

/// @brief 一块显示器的工作区（**物理像素**，与框架 `app::Display::work_area` 同一坐标系：
///          已扣任务栏，原点可为负——副屏在主屏左侧或上方时就是负）。
struct QuakeWorkArea {
    double origin_x = 0.0;
    double origin_y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

/// @brief 折算目标矩形的入参。
struct QuakeGeometryRequest {
    QuakeWorkArea work_area{};
    /// @brief 该屏的 DPI 缩放（dpi/96）。尺寸输出的 dp 由它反算；非正值按 1.0 处理（见
    ///        `quake_target_rect` 的钳位口径）——装载侧给一个 0 不能把窗口折成 0 宽。
    double scale_factor = 1.0;
    QuakeAnchor anchor = QuakeAnchor::Top;
    /// @brief **跨边**方向的占比（`Top`/`Bottom` 档是宽，`Left`/`Right` 档是高）。缺省占满。
    double cross_ratio = 1.0;
    /// @brief **落下**方向的占比（`Top`/`Bottom` 档是高，`Left`/`Right` 档是宽）。缺省一半。
    double along_ratio = 0.5;
};

/// @brief 落位目标：原点物理像素（喂框架 `set_position`），尺寸逻辑 dp（喂框架 `set_size`）。
///
/// 两者单位**故意不同**，因为框架那两口的实现口径就不同：`set_position` 直传
/// `SetWindowPos`（物理 px），`set_size` 先 `to_physical()` 再按装饰补偿（逻辑 dp 客户区，
/// 其头注释写「物理像素外框」是错的，已随 G47 §4 报框架）。把换算收在这里，装配层就没有
/// 混单位的机会（本仓踩过 dp/px 边界：见绘制侧格度量那条口径）。
struct QuakeTarget {
    double origin_x = 0.0;  ///< 物理像素。
    double origin_y = 0.0;  ///< 物理像素。
    double width_dp = 0.0;  ///< 逻辑 dp。
    double height_dp = 0.0; ///< 逻辑 dp。
};

/// @brief 占比的合法闭区间下界：帘子不得塌成零宽/零高。
inline constexpr double kQuakeRatioMin = 0.05;
/// @brief 占比的合法闭区间上界（工作区本身）。
inline constexpr double kQuakeRatioMax = 1.0;

/// @brief 比例 → 贴边矩形。跨边方向居中、落下方向贴工作区边（贴 `work_area` 不是 `bounds`：
///          Windows 下任务栏不该被终端盖住）。
///
/// 占比先夹进 [`kQuakeRatioMin`, `kQuakeRatioMax`]，`scale_factor` 非正回落 1.0；工作区非正
/// 尺寸原样得 0（装配层须自己回落到默认屏，本件不猜屏）。纯函数、无状态、无 IO。
/// @param request 工作区、缩放、贴边与两向占比。
/// @return 落位目标（原点物理像素、尺寸逻辑 dp）。
[[nodiscard]] auto quake_target_rect(const QuakeGeometryRequest &request) -> QuakeTarget;

/// @brief 显隐状态（`QuakeToggle` 视角的「应当是什么」，不是 OS 实测态）。
enum class QuakeState : std::uint8_t {
    Hidden,
    Shown,
};

/// @brief 状态机对装配层的一次输出：去做这件事，或什么都不做。
enum class QuakeIntent : std::uint8_t {
    None,
    Show,
    Hide,
};

/// @brief 呼出/隐藏的稳定时长：本件自己发起一次显隐后，这段时间内的失焦事件一律吞掉。
inline constexpr std::chrono::milliseconds kQuakeFocusSettle{250};

/// @brief Quick Terminal 的显隐意图状态机。
///
/// 存在的理由只有一条：**热键与「失焦自动隐藏」会互相打架**。Tilix 的 quake 模式记录在案的
/// 故障是「按热键隐藏后窗口自己弹回」——隐藏让窗口失焦，失焦又走回显示判定那条路。解法不是
/// 计数也不是加锁，而是「本件自己发起的显隐之后的这段时间里不认失焦事件」。
/// 状态随**意图**翻转（幂等：已是 `Shown` 时再收热键出 `None`，不出第二次 `Show`）；装配层
/// 若发现框架没接住这次意图，须自己回滚状态——本件不猜「OS 现在到底可见吗」。
/// @note Thread: main-thread only（与热键回调同线程，无内部锁）
class QuakeToggle {
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    /// @brief 构造。
    /// @param auto_hide_on_focus_lost 失焦即隐（D7：缺省开，业界有人关，故必须可关）。
    /// @param focus_settle 焦点稳定时长；装配层可从配置给，缺省 `kQuakeFocusSettle`。
    explicit QuakeToggle(bool auto_hide_on_focus_lost = true,
                         std::chrono::milliseconds focus_settle = kQuakeFocusSettle) noexcept
        : auto_hide_on_focus_lost_(auto_hide_on_focus_lost),
          focus_settle_(focus_settle.count() < 0 ? std::chrono::milliseconds{0} : focus_settle) {}

    /// @brief 当前状态。
    /// @return 最近一次意图翻转后的 `QuakeState`；构造时为 `Hidden`。
    [[nodiscard]] auto state() const noexcept -> QuakeState { return state_; }

    /// @brief 是否**有**过渡窗在途（不判时刻；判某时刻是否被吞取 `ignoring_focus_at`）。
    /// @return 设过上沿而未显式清除时为 `true`。
    [[nodiscard]] auto ignoring_focus() const noexcept -> bool { return ignore_until_.has_value(); }

    /// @brief 是否正处于吞失焦事件的过渡窗内。
    /// @param now 判定时刻。
    /// @return 有过渡窗且 `now` 未过其上沿时为 `true`。
    [[nodiscard]] auto ignoring_focus_at(TimePoint now) const noexcept -> bool {
        return ignore_until_.has_value() && now < ignore_until_.value();
    }

    /// @brief 设置失焦自动隐藏开关（装配层从配置读取后灌入）。
    /// @param on 开则为 `true`。
    auto set_auto_hide_on_focus_lost(bool on) noexcept -> void { auto_hide_on_focus_lost_ = on; }

    /// @brief 当前失焦自动隐藏开关。
    /// @return 开关值。
    [[nodiscard]] auto auto_hide_on_focus_lost() const noexcept -> bool { return auto_hide_on_focus_lost_; }

    /// @brief 全局热键命中（呼出或收回，同一键）。
    /// @param now 命中时刻；同时用作过渡窗的起点。
    /// @return 状态为 `Hidden` 时出 `Show`、为 `Shown` 时出 `Hide`；每次有效意图都重开过渡窗。
    [[nodiscard]] auto on_hotkey(TimePoint now) -> QuakeIntent;

    /// @brief 窗口失焦（由框架的窗口状态上报转来）。
    /// @param now 失焦时刻。
    /// @return 仅在「开关开 ∧ 当前 `Shown` ∧ 不在过渡窗内」时出 `Hide`，其余一律 `None`。
    [[nodiscard]] auto on_focus_lost(TimePoint now) -> QuakeIntent;

  private:
    QuakeState state_ = QuakeState::Hidden;  ///< 当前意图态（非 OS 实测态）。
    bool auto_hide_on_focus_lost_ = true;    ///< 失焦即隐开关。
    std::chrono::milliseconds focus_settle_{kQuakeFocusSettle};  ///< 过渡窗时长（负值构造时归零）。
    std::optional<TimePoint> ignore_until_;  ///< 过渡窗上沿；空＝不在过渡窗内。
};

}  // namespace borealis::ui
