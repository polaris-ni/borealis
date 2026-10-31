#pragma once

// ============================================================
// 输入延迟的滑窗读数（include/borealis/ui/input_latency.h）
// ------------------------------------------------------------
// `SPEC.NF.PERF.01` 那句「按键 → 字符上屏端到端 ≤ 50 ms（本机 shell、非负载场景，P95）」里
// **能在屏上读**的那半：一笔一笔的「键入时刻 → 首次上屏时刻」差值进一张有界窗口，窗口取 P95
// 交状态栏那一格（屏 1 稿 `codespec/UI_WORKSPACE.draft.svg` 状态栏那串文本 `输入延迟 P95 12 ms`，
// 开关是 `appearance.status_bar.show_input_latency`，`SPEC.FEAT.PREF.02`）。需求本体不新开：
// 这条是 `SPEC.NF.PERF.01` 的量测腿，不是又多一条需求。裁决与逐条口径见 `SPECIFICATIONS.md` §7。
//
// 本件刻意**不持时钟、不认识会话与控件**：`now` 一律作入参（`grid_size_debounce` 同族先例，
// AGENTS §4.4 第 20 条要的「纯逻辑层可独立单测」）。装配侧只剩三条职责：键入写入会话那一刻
// `note_input_issued`、帧边界内容换源那一刻 `note_output`、逐帧把 `p95()` 折进状态栏那一格。
// §4.5 第 25 条（绘制路径与事件回调禁长计算）因此落在件内部：读数必须 O(1)，于是样本另存一份
// 有序副本，取第 95 位只是算一次索引；插入与淘汰是 O(N)（N ≤ 100），且只发生在**结算那一笔**上，
// 不发生在逐帧读取上。
//
// 三条决定形态的口径（裁决 7.186①②③）：
// - **窗口按样本数、不按时间**（缺省 100 笔）：P95 是次序统计量，时间窗在低频键入时会退化成
//   一两笔样本、读数随打字节奏抖；按笔数则「第 95 位」这条判据可以逐值写死，淘汰的永远是
//   **最旧那一笔**（不是最小那一笔——两个容器各管一头，见 `impl` 的注释）。
// - **超过归因上限的配对丢弃**（缺省 1 s）：起点与终点之间隔着一次无回显等待（`read -s` 的密码、
//   远端卡住、`less` 等按键）时，那一段根本不是「你刚按下那个键」的回应——计进来会让一次 30 s
//   的挂机把 P95 长期钉死。丢弃**单独计数**（`dropped_count()`）交可观测面读，不静默吞。
// - **起点取最近一次未配对的键入**：连打与自动重复让起点不断后移，测的是「末笔键入 → 首次上屏」，
//   与用户「按下最后一个键之后多久看到东西」的体感同源。代价如实登记：整段快速打字的**首笔**
//   延迟不在这份读数的射程里。
//
// 显形口径（裁决 7.186④）：`p95()` 在无样本、或**最后一笔样本已过期**（缺省 5 s）时返回空——
// 状态栏那格的既有纪律是「空文本即不交进测量表」（`status_bar_format.h` 的 N3 省略算式因此自然
// 生效）。挂着一小时前的老读数比什么都不画更坏。
// ============================================================

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace borealis::ui {

/// @brief 缺省样本窗口（笔）：P95 取第 95 个次序统计量的分母上限。
inline constexpr std::size_t kInputLatencySampleCap = 100U;
/// @brief 缺省归因上限：键入到首次上屏超过这么久即判为「不可归因」并丢弃（裁决 7.186②）。
inline constexpr std::chrono::milliseconds kInputLatencyAttributionCap{1000};
/// @brief 缺省过期窗：最后一笔样本距今超过这么久即不再上屏（裁决 7.186④）。
inline constexpr std::chrono::milliseconds kInputLatencyStaleAfter{5000};

/// @brief 「键入 → 首次上屏」的有界样本窗口与它的 P95 读数。
///
/// 不变量：任一时刻至多一笔未配对键入（后到的键入覆盖起点）；样本数恒 ≤ `sample_cap`；
/// 有序副本与时间序副本**同源同长**（同一批样本的两种排法，不存在只进其一的路径）；
/// `p95()` 只读，不改状态。
/// @note Thread: main-thread only（装配层与视口都在主线程调用）
/// @note Rebuildable: yes（换绑会话即新建控件，本件随之新建）
class InputLatency final {
  public:
    /// @param sample_cap 样本窗口上限（笔），0 视为 1——窗口为空就没有读数，也不该出现空窗口。
    /// @param attribution_cap 归因上限：超过即丢弃该笔配对并计一次 `dropped`。
    /// @param stale_after 过期窗：最后一笔样本距今超过它，`p95()` 即返回空。
    explicit InputLatency(
        std::size_t sample_cap = kInputLatencySampleCap,
        std::chrono::milliseconds attribution_cap = kInputLatencyAttributionCap,
        std::chrono::milliseconds stale_after = kInputLatencyStaleAfter) noexcept;

    /// @brief 记一笔键入已写入会话：起点即此刻，后到的覆盖先到的（口径见文件头第三条）。
    /// @param now 键入写入会话的那一刻。
    auto note_input_issued(std::chrono::steady_clock::time_point now) -> void;

    /// @brief 记一次内容上屏：若有未配对键入，就把它结算成一笔样本。
    ///
    /// 一笔键入只配**首个**上屏时刻，结算之后未配对槽位即清空——同一批 damage 之后的每一帧不再
    /// 产生样本，否则连续刷屏会把样本数按帧数而不是按键入数灌满。
    /// @param now 上屏的那一刻。
    /// @return 本笔是否新增一样本（丢弃的超上限配对返回 false，但 `dropped_count()` 随之加一）。
    auto note_output(std::chrono::steady_clock::time_point now) -> bool;

    /// @brief 窗口里这批样本的 P95（最近秩法，`rank = ceil(0.95 × N)`，不做插值）。
    ///
    /// @param now 读数时刻：用于过期判定（口径见文件头第四条）。
    /// @return 无样本或已过期即空；否则为**毫秒整数**（配对差值本身已 `duration_cast` 截断到 ms）。
    [[nodiscard]] auto p95(std::chrono::steady_clock::time_point now) const
        -> std::optional<std::chrono::milliseconds>;

    /// @brief 当前样本数（用例与走查读这一枚判「窗口填到几笔」）。
    [[nodiscard]] auto sample_count() const noexcept -> std::size_t { return time_order_.size(); }

    /// @brief 有未配对键入挂在槽位上（装配层可据此判「这一笔还欠一个上屏」）。
    [[nodiscard]] auto has_pending_input() const noexcept -> bool { return pending_.has_value(); }

    /// @brief 因超过归因上限而被丢弃的配对累计数。
    ///
    /// 只在件内部累计，**不进** `SPEC.NF.RELI.01` 那四族计数（解析降级 / 非法字节 / 背压水位 / 发送侧
    /// 不可表示）——那四族是 F12 面板跨会话快照的账，本枚是单视口的读数，混进去会让面板多一族无来源的数。
    [[nodiscard]] auto dropped_count() const noexcept -> std::size_t { return dropped_; }

  private:
    /// @brief 结算一笔样本：双容器同进同出，满了先淘汰时间序上最旧那一笔。
    auto push_sample(std::chrono::milliseconds sample) -> void;

    std::size_t sample_cap_ = kInputLatencySampleCap;
    std::chrono::milliseconds attribution_cap_ = kInputLatencyAttributionCap;
    std::chrono::milliseconds stale_after_ = kInputLatencyStaleAfter;

    std::optional<std::chrono::steady_clock::time_point> pending_{};  ///< 未配对键入的起点。
    /// 最后一笔样本的结算时刻；只在 `sorted_` 非空时被读（样本只进不除，除淘汰外不减员，故非空即有效）。
    std::chrono::steady_clock::time_point last_sample_at_{};

    std::deque<std::chrono::milliseconds> time_order_{};  ///< 样本按结算先后（淘汰最旧用）。
    std::vector<std::chrono::milliseconds> sorted_{};     ///< 同批样本按值升序（取第 95 位用）。

    std::size_t dropped_ = 0U;
};

}  // namespace borealis::ui
