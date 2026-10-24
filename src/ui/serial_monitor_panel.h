#pragma once

// ============================================================
// 串口十六进制收发监视面板（src/ui/serial_monitor_panel.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 那条延后子项「可选十六进制收发视图与行首时间戳前缀」的界面腿，形态由设计稿
// `codespec/UI_SERIAL_MONITOR.draft.md` 的 D1①／D5①／D6①／D7①／D8①／D9① 定形：左侧 **640 dp** 停靠
// 卡片（8 字节/行 = 54 字符，14 pt 等宽单格 11 dp ⇒ 594 dp；16 字节档要 946 dp，等于在 960 dp 的窗里
// 盖住整个终端）、两枚**面板局部**开关（时间戳前缀缺省开、暂停刷新缺省关）、一枚清空、丢弃计数上屏，
// 零新设置键（D9①：环上限与显示行上限都是文件级常量，重启即回缺省档）。
//
// **本件不认识 `Session`，也不认识串口**（隧道／密钥／SFTP 面板的同族纪律，AGENTS §4.5 第 23 条）：
// 与装配层的全部接缝就是下面三条 Hooks——`snapshot` 交出环里现存的 run 表与累计丢弃字节、`clear` 交回
// 本期唯一的止血动作、`binding_label` 交出绑定的档案名与设备路径。武装（`Session::enable_raw_tap()`）
// 发生在命令腿，故本件不写环（除 `clear`），也不判断「这一格是不是串口」。
//
// 读法是 **latest-value**（稿 §5）：每帧问一次「环里现在有什么」，不做「本帧新增的 run」那种增量协议
// ——游标一旦与暂停／清空交错就会说出「少了半屏」。
//
// 显示期只做三件事（稿 §2／§5）：
//   ⑴ **截尾**：只把环尾 `kMonitorTailBytes` 那一段字节交给折算件，再留最后 `kMonitorVisibleRowCap`
//      行。256 KiB 的环全折是 32768 行，而 640 dp 高的窗里画得下的只有二十几行——画出去也得靠滚动
//      才看得见，跟尾视图停在半屏旧数据上更糟（稿 §7 那条「滚动回溯在册」的同一件事）。
//   ⑵ **上色与等宽**：`direction_label` 那一格吃强调色、其余两格正色（稿 §2：RX／TX 只以这两个字母与
//      一列强调色区分），族名 `kDefaultMonospaceFamily`——行形态的算术只在这个族名下成立（F8／F9）。
//   ⑶ **留痕**：丢弃计数 >0 才上屏那句「已丢弃 N 字节」，另有一句「本面板不落盘」常驻（稿 §1 第一条
//      安全口径：设备流量可能含口令回显，不持久化是代价而不是缺陷）。
//
// 关面板**不摘环**（D7①）：`close()` 只收浮层，那条会话照留痕；重新打开看到的是历史，`清空` 才是归零。
// 绑定由命令腿在「从关闭态打开」那一刻按当时的当前标签写下，之后不跟标签跑——看着 A 的时候切到 B，
// 屏面不该自己换内容。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 include/borealis/。
// ============================================================

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"

#include "borealis/session/raw_tap.h"
#include "serial_monitor_format.h"

namespace borealis::ui {

/// @brief 行区一次画出的行数上限（稿 §5：只折「最后可见的那几十行」）。
///
/// 取 24 而不是 60：14 pt 行高约 20 dp，960×640 dp 的缺省窗里扣掉标题行、开关行与两行留痕，行区容得下
/// 的就是二十几行（F8 的换算）。超出这一档的行不折、不画，也就不进帧预算。
inline constexpr std::size_t kMonitorVisibleRowCap = 24U;

/// @brief 折行前从环尾取走的字节预算。
///
/// `行数上限 × 每行字节数` 是**下界保证**而非上界：环尾只要有这么些字节，折出来就恒 ≥ 上限行数
/// （二进制段没有行尾时 8 字节恰好一行），不够这么多才画得出少于上限的一屏。截断只发生在 **run 边界**，
/// 故不会出现半个字节；一条设备行可能被截在中间，那是「只显示最近若干行」这条在册代价的必然形态。
inline constexpr std::size_t kMonitorTailBytes = kMonitorVisibleRowCap * kMonitorBytesPerRow;

/// @brief 串口十六进制收发监视卡（面板族第八只瞬态浮层）。
class SerialMonitorPanel {
  public:
    /// @brief 一帧的环投影（稿 §4 那句「`snapshot`（取 run 表 + 丢弃数）」的返回形态）。
    struct Snapshot {
        std::vector<session::RawRun> runs;  ///< 到达序（最旧在前）；未武装、绑定已散即空表。
        std::size_t dropped_bytes = 0U;     ///< 逐出 + 整条拒收之和；>0 才上屏那句留痕。
    };

    /// @brief 与装配层的全部接缝（struct-of-回调，同 `TunnelPanel::Hooks` 形态）。
    struct Hooks {
        /// @brief 现取环投影（latest-value，不持锁、不做 IO：装配层经 `Session::raw_tap()` 拿那份
        ///        自有关锁的环）。未武装即空表——「判一次就走」的零成本那半在会话侧，不在这里。
        std::function<Snapshot()> snapshot;
        /// @brief 清空环（本期唯一的止血手段，D7①；同时把丢弃累计一起归零，见 `RawTap::clear`）。
        std::function<void()> clear;
        /// @brief 绑定的档案名与设备路径（稿 §2 标题行那一格；空串＝绑定已失去身份）。
        std::function<std::string()> binding_label;
    };

    SerialMonitorPanel(class aurora::OverlayHost &host, Hooks hooks);
    SerialMonitorPanel(const SerialMonitorPanel &) = delete;
    auto operator=(const SerialMonitorPanel &) -> SerialMonitorPanel & = delete;
    SerialMonitorPanel(SerialMonitorPanel &&) = delete;
    auto operator=(SerialMonitorPanel &&) -> SerialMonitorPanel & = delete;
    ~SerialMonitorPanel();

    /// @brief 换浮层宿主（`SPEC.FEAT.WS.03` 稿 D6「面板搬窗」，本件是面板族**第八只**）：面板是 app 级
    ///        单实例，浮层要挂在发起命令的那一扇窗上；装配层在打开腿与关窗撤账腿各调一次。
    ///
    /// 已挂在别的宿主上时先 `close()`——摘的是**旧宿主**上自己记的那枚序号。**环不受影响**：留痕在那条
    /// 会话的 `Session` 上，本件只是它的一份投影（与 D5①「关闭面板不停隧道」同一条口径）。
    /// @param host 新增浮层宿主，**非拥有**：它持有本件的浮层节点，析构次序须晚于本件。
    auto set_overlay_host(aurora::OverlayHost &host) -> void;

    /// @brief 现在挂在谁的宿主上（装配层据此判断「这一扇散了，本件要搬」；nullptr＝从未挂过）。
    [[nodiscard]] auto overlay_host() const noexcept -> aurora::OverlayHost * { return host_; }

    /// @brief 开卡（未开时；已开着就只跟一帧数，不重挂浮层——命令重复唤起不该闪一下）。
    auto open() -> void;

    /// @brief 收卡：只摘浮层与本件的副本，**不摘环**（D7①）。
    auto close() -> void;

    auto toggle() -> void {
        open_ ? close() : open();
    }

    [[nodiscard]] auto is_open() const noexcept -> bool {
        return open_;
    }

    /// @brief 帧驱动（装配层 `on_frame` 调用，F14 那套状态泵）：读环、行表有变才重建。
    ///
    /// 暂停态不读环——画面停在按下那枚开关的那一刻，环照人在后面写（稿 §1「面板动作」里
    /// 「刷新（自动跟帧）／暂停」就是这一条腿）。暂停中改了时间戳档或按了清空，仍补一次重画：
    /// 「别刷屏」不该变成「改档看不见」。
    auto tick() -> void;

    /// @brief 当前该画的行（观测面：由 `source_` 与两枚显示档折出，判据比这份而不读浮层树）。
    ///
    /// 与浮层那一份同源，故拨开关后立刻就能读出无前缀的行，而不必等一帧——真正的重建排在下一帧，
    /// 那才是「回调里不做长计算」（§4.5 第 25 条）要的次序。
    [[nodiscard]] auto visible_rows() const -> std::vector<MonitorRow>;

    /// @brief 丢弃留痕（观测面）：有丢弃才是那句「已丢弃 N 字节（环上限 256 KiB）」，无即空串。
    ///
    /// 「不落盘」那句声明不走这一格：它恒在卡上（常驻一行），集成用例按显示串在派发链上找它。
    [[nodiscard]] auto notice() const -> std::string;

    /// @brief 时间戳前缀档（观测面；缺省开，D6①）。
    [[nodiscard]] auto timestamp_enabled() const noexcept -> bool { return timestamp_prefix_; }

  private:
    /// @brief 问一次环（Hook 缺席即空投影：本件不许因装配层没装某条腿而崩）。
    [[nodiscard]] auto read_snapshot() const -> Snapshot;

    /// @brief 收一帧环投影进本件状态（截尾 + 计数 + 绑定），再判要不要重建。
    auto apply(const Snapshot &snapshot) -> void;

    /// @brief 行表／绑定／计数与上一帧画出的那份有差别时重建浮层（并清「待重画」旗标）。
    auto maybe_refresh() -> void;

    /// @brief 用当前显示档把 `source_` 折成行表（行数截到上限）。
    [[nodiscard]] auto fold_source() const -> std::vector<MonitorRow>;

    /// @brief 装（或重装）主卡片浮层：蒙版 + 卡片 + `Dialog` 一次造齐，记下宿主给的那枚序号。
    auto install_overlay() -> void;

    /// @brief 摘掉当前那层再装一次（`open()` 之外的唯一重建入口；由 `maybe_refresh()` 调用）。
    auto refresh_overlay() -> void;
    [[nodiscard]] auto build_overlay() -> std::shared_ptr<aurora::Column>;
    [[nodiscard]] auto build_row(const MonitorRow &row) -> aurora::Node;

    auto on_timestamp_changed(bool next) -> void;
    auto on_pause_changed(bool next) -> void;
    auto on_clear() -> void;

    auto clear_state() -> void;

    class aurora::OverlayHost *host_ = nullptr;  ///< 非拥有：当前挂在谁的宿主上（`set_overlay_host` 可换）。
    Hooks hooks_{};
    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};
    bool open_ = false;

    // ---- 两枚面板局部量（D9①：零新设置键，重启回这里写的缺省档）----
    bool timestamp_prefix_ = true;  ///< 关掉即不折前缀，也不调 `localtime`（稿 D6④ 的那半代价）。
    bool paused_ = false;           ///< 暂停＝不读环，仍可按显示档重画。
    bool repaint_pending_ = false;  ///< 暂停中改了档或清了环：下一帧补一次重建。

    std::vector<session::RawRun> source_{};  ///< 截尾后那段 run（≤ `kMonitorTailBytes`）：重折的唯一料源。
    std::size_t ring_bytes_ = 0U;            ///< 环里现存字节数（标题行那个计数；含未画出的部分）。
    std::size_t dropped_bytes_ = 0U;         ///< 累计丢弃字节（`notice()` 的那一句）。
    std::string binding_{};                  ///< 绑定的档案名与设备路径（现取 Hook 的落地副本）。

    // ---- 上一帧真画出来的那份（diff 基准；与 `tunnel_panel`／`keys_panel` 同形）----
    std::vector<MonitorRow> painted_rows_{};
    std::string painted_binding_{};
    std::size_t painted_ring_bytes_ = 0U;
    std::size_t painted_dropped_bytes_ = 0U;
};

}  // namespace borealis::ui
