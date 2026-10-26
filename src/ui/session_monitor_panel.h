#pragma once

// ============================================================
// 串口十六进制收发监视面板（src/ui/session_monitor_panel.h）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 那条延后子项「可选十六进制收发视图与行首时间戳前缀」的界面腿，形态由设计稿
// `codespec/UI_SERIAL_MONITOR.draft.md` 的 D1①／D5①／D6①／D7①／D8①／D9①（二期再叠 D10～D18）定形：
// 左侧 **640 dp** 停靠卡片（8 字节/行 = 54 字符，14 pt 等宽单格 11 dp ⇒ 594 dp）、三枚**面板局部**开关
// （时间戳前缀缺省开、暂停刷新缺省关、文本视图缺省关＝稿 §11 D26①）、一枚清空、丢弃计数上屏，
// 零新设置键（D9①：环上限仍是文件级常量，重启即回缺省档；二期的折带窗口是显示期常量，不是第二个上限）。
//
// **本件不认识 `Session`，也不认识串口**（隧道／密钥／SFTP 面板的同族纪律，AGENTS §4.5 第 23 条）：
// 与装配层的接缝就是下面七条 Hooks——`snapshot` 交出环里现存的 run 表与累计丢弃字节、`clear` 交回
// 本期唯一的止血动作、`binding_label` 交出绑定的档案名与设备路径、`encoding_name` 交出那条会话**构造期
// 折好的实际生效编码腿**（稿 D28：文本档解码用，缺席或映射不到即回落 UTF-8），三期再加 `copy_text`／
// `pick_export_path`／`export_text` 三条取出去的腿（稿 §10.2 D22①）。武装（`Session::enable_raw_tap()`）
// 发生在命令腿，故本件不写环（除 `clear`），也不判断「这一格是不是串口」。
//
// 读法是 **latest-value**（稿 §5）：每帧问一次「环里现在有什么」，不做「本帧新增的 run」那种增量协议
// ——游标一旦与暂停／清空交错就会说出「少了半屏」。
//
// 显示期只做三件事（稿 §2／§9.2～§9.4）：
//   ⑴ **折窗口**：环里存着整段历史，行区是 `LazyList`（D10①），但面板只折**当前 96 行窗口**
//      （D11①）：跟尾时窗口恒为环尾 96 行；用户往上翻离开底部即冻结（D12①），此时不读环，
//      只在可见区逼近折带边缘时以可见首行为锚重折并保持偏移。一期的截尾三件
//      （`kMonitorVisibleRowCap` / `kMonitorTailBytes` / `tail_runs`）已删除（D18①）。
//   ⑵ **上色与等宽**：`direction_label` 那一格吃强调色、其余两格正色（稿 §2：RX／TX 只以这两个字母与
//      一列强调色区分），族名 `kDefaultMonospaceFamily`——行形态的算术只在这个族名下成立（F8／F9）。
//   ⑶ **留痕**：丢弃计数 >0 才上屏那句「已丢弃 N 字节」，另有「只在显式导出时才写盘」那句常驻（稿 §1 第一条
//      安全口径经 §10.2 D24① 改口）；冻结时另出「已暂停跟尾」与「回到最新」两枚（D13①），跟尾时都不出现。
//   ⑷ **取出去**（三期，稿 §10）：标题行两枚按钮把 `source_` 按**当前显示档**整份折成文本
//      （`monitor_dump_text`，D19①：与屏面同一台折算件，不另造第二份真值），再交三条 Hook 出去。空环
//      整条不跑（D21①）；本件不碰剪贴板、不起对话框、不写盘（D22①，与 `SettingsPanel::run_export()` 同形）。
//
// 关面板**不摘环**（D7①）：`close()` 只收浮层，那条会话照留痕；重新打开看到的是历史，`清空` 才是归零。
// 绑定由命令腿在「从关闭态打开」那一刻按当时的当前标签写下，之后不跟标签跑——看着 A 的时候切到 B，
// 屏面不该自己换内容。
//
// 私有头（裁决 D1① 同口径）：含框架类型，不进 include/borealis/。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aurora/widget/dialog.h"
#include "aurora/widget/popup.h"

#include "borealis/session/raw_tap.h"
#include "session_monitor_format.h"

namespace aurora {
class LazyList;  ///< 私有头只持句柄：滚动物件在建树期现造，定义留在 .cpp（D10①）。
}  // namespace aurora

namespace borealis::ui {

/// @brief 行区每行的固定高度（dp，稿 D15①）。
///
/// 14 pt 等宽单格高约 19 dp（F8），余 1 dp 作行距；`LazyList::item_extent` 只支持固定行高，而二期所有行
/// 仍是单行等宽，故这枚常量同时是「一屏能看几行」与滚轮几何的唯一来源（缺省窗约 17 行）。
inline constexpr float kMonitorRowExtent = 20.0F;

/// @brief 折带一次折多少行（稿 D11①）。
///
/// 视口约 17 行 + `cache_extent` 200 dp ≈ 10 行 × 两侧，再留余量：96 行在滚出折带之前足够读几屏，
/// 又把「折一次」压到约 23 µs（F5）。窗口之前/之后的行只计数、不建字符串。
inline constexpr std::size_t kMonitorFoldWindowRows = 96U;

/// @brief 可见区离折带边缘不足这么多行就重折（稿 §9.4）。
inline constexpr std::size_t kMonitorFoldEdgeRows = 4U;

/// @brief 重折时在可见首行之上留的行数。
///
/// 必须大于「`cache_extent` 折成的行数（200 dp ÷ 20 dp = 10）+ 折带边缘 4 行」，否则重折后可见区
/// （含缓存）仍贴在折带顶上，下一帧又判一次「逼近边缘」，窗口会在没有用户输入时自己往回滑。
inline constexpr std::size_t kMonitorFoldMarginRows = 16U;

/// @brief 判定「离开底部」的容差（dp）：浮点夹取与布局取整都会让贴底差一丁点。
inline constexpr float kMonitorBottomTolerance = 0.5F;

/// @brief 串口十六进制收发监视卡（面板族第八只瞬态浮层）。
class SessionMonitorPanel {
  public:
    /// @brief 一帧的环投影（稿 §4 那句「`snapshot`（取 run 表 + 丢弃数）」的返回形态）。
    struct Snapshot {
        std::vector<session::RawRun> runs;  ///< 到达序（最旧在前）；未武装、绑定已散即空表。
        std::size_t dropped_bytes = 0U;     ///< 逐出 + 整条拒收之和；>0 才上屏那句留痕。
        /// @brief 环的单调变更计数（D17①）：环满后逐出与写入同量，字节数/条数摘要判不出「有新数据」。
        ///        装配层须**先读 revision 再取 runs**：反序时两次读之间的写入会让新数据被当成旧帧skip 掉。
        std::uint64_t revision = 0U;
        /// @brief 环的现值上限（D16①）：留痕那句的「环上限 N KiB」直接读它，不在面板里写死第二份。
        std::size_t cap_bytes = session::kMaxRawTapBytes;
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
        /// @brief 那条会话**构造期折好的实际生效编码腿名**（稿 §11 D28①，取自 `Session::encoding_name()`）：
        ///        文本档的解码器按它现造。缺席或空串在 `follow_frame()` 里折成 `UTF-8`（稿 D35②：与
        ///        `make_session_decoder` 的既有回落同值，但不在绘制路径上刷 WARN），本件不认识编码表。
        std::function<std::string()> encoding_name;
        /// @brief 把整份行文本交给剪贴板（稿 §10.2 D22①）：本件不碰 OS、不判成败，那条腿在装配层
        ///        （`ClipboardOutbox::write`，与 `keys_panel` 的一键复制同一入口）。
        std::function<void(std::string text)> copy_text;
        /// @brief 让用户点名一个要写出的文件；空串＝取消（同 `SettingsPanel::Hooks::pick_export_path`
        ///        那条口径：取消与对话框起不来同值，整条不跑、连留痕都不写）。
        std::function<std::string()> pick_export_path;
        /// @brief 把文本写到用户点名的路径（稿 D22①／D23①：权限位与原子性在那条腿收）。
        /// @return 空＝成功；否则 ASCII 失败原因——只进日志，上屏的是词条那句（同 `export_config`）。
        std::function<std::optional<std::string>(const std::string &path, const std::string &text)>
            export_text;
    };

    SessionMonitorPanel(class aurora::OverlayHost &host, Hooks hooks);
    SessionMonitorPanel(const SessionMonitorPanel &) = delete;
    auto operator=(const SessionMonitorPanel &) -> SessionMonitorPanel & = delete;
    SessionMonitorPanel(SessionMonitorPanel &&) = delete;
    auto operator=(SessionMonitorPanel &&) -> SessionMonitorPanel & = delete;
    ~SessionMonitorPanel();

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

    /// @brief 帧驱动（装配层 `on_frame` 调用，F14 那套状态泵）：几何判冻结、跟尾读环、窗口重折。
    ///
    /// 三条闸的次序（稿 §9.4）：①「回到最新」/清空后的首帧跳过几何判据（旧列表仍在离底位置，
    /// 直接判会把刚落到底的画面判回冻结）；②暂停态不读环——画面停在按下那枚开关的那一刻，环照人
    /// 在后面写；③冻结态不读环、不重建，只在可见区逼近折带边缘时滑窗。重建仍只在这里发生。
    auto tick() -> void;

    /// @brief 当前该画的行（观测面：当前折出的窗口，不是整环）。
    ///
    /// 与浮层那一份同源，但它只在 `tick()` 里随重建更新：拨开关后要等下一帧才读到新档——「回调里不做
    /// 长计算」（§4.5 第 25 条）要的正是这个次序。跟尾时是环尾至多 96 行。
    [[nodiscard]] auto visible_rows() const -> std::vector<MonitorRow>;

    /// @brief 是否已离底冻结（观测面，D12①）：冻结时行区不追加新数据，提示行与「回到最新」在场。
    [[nodiscard]] auto frozen() const noexcept -> bool { return frozen_; }

    /// @brief 丢弃留痕（观测面）：有丢弃才是那句「已丢弃 N 字节（环上限 N KiB）」，无即空串。
    ///
    /// 「不落盘」那句声明不走这一格：它恒在卡上（常驻一行），集成用例按显示串在派发链上找它。
    [[nodiscard]] auto notice() const -> std::string;

    /// @brief 时间戳前缀档（观测面；缺省开，D6①）。
    [[nodiscard]] auto timestamp_enabled() const noexcept -> bool { return timestamp_prefix_; }

    /// @brief 文本视图档（观测面；缺省关＝十六进制档，稿 §11 D26①）。
    ///
    /// 与另两枚同族：面板局部量，切档只换行形态，不改采集、不改环、不改设置。
    [[nodiscard]] auto text_view_enabled() const noexcept -> bool { return text_view_; }

    /// @brief 这一屏料源的整份行文本（三期观测面，稿 §10.2 D19①／D20①）。
    ///
    /// 与屏面同一份 `MonitorRowOptions`、同一台扫描器，故文件/剪贴板里每一行都能在屏上逐字符找到。
    /// 料源是手里的 `source_` 而不是**按下那一刻重读的环**：暂停/冻结时屏面没变，交出去的东西也不该
    /// 变（D20①）。空环即空串，两枚动作据此整条不跑（D21①）。
    [[nodiscard]] auto dump_text() const -> std::string;

    /// @brief 上一次「取出去」动作的留痕（观测面，稿 §10.3）：无即空串；收到新的环 revision 即消失。
    [[nodiscard]] auto feedback() const -> std::string { return feedback_; }

  private:
    /// @brief 问一次环（Hook 缺席即空投影：本件不许因装配层没装某条腿而崩）。
    [[nodiscard]] auto read_snapshot() const -> Snapshot;

    /// @brief 当前显示档（三枚面板局部量都在这里汇合；窗口折行与整表折行同一口径）。
    [[nodiscard]] auto make_options() const -> MonitorRowOptions;

    /// @brief 几何判冻结（D12①）：离底且不在环尾窗口的底端 ⇒ 冻结；未布局/不足一屏一律视为跟尾。
    [[nodiscard]] auto compute_frozen() const -> bool;

    /// @brief 当前窗口是否已抵环尾（只有「窗口底 == 环底」才允许由滚到底恢复跟尾）。
    [[nodiscard]] auto window_covers_ring_end() const -> bool;

    /// @brief 跟尾帧：revision／上限／丢弃／绑定未变且无待重画即早退（D17①）。
    auto follow_frame() -> void;

    /// @brief 暂停/冻结下的重画：按手里的 `source_` 重折当前窗口，不读环（D14①）。
    auto rebuild_current_view() -> void;

    /// @brief 冻结态滑窗：可见区（含缓存）逼近折带边缘时以可见首行为锚重折，并按行差平移偏移。
    auto slide_frozen_window() -> void;

    /// @brief 记下这份窗口并重建浮层（`scroll_offset` 同时是重建后的落位）。
    auto paint_rows(std::vector<MonitorRow> rows, float scroll_offset) -> void;

    /// @brief 装（或重装）主卡片浮层：蒙版 + 卡片 + `Dialog` 一次造齐，记下宿主给的那枚序号。
    auto install_overlay() -> void;

    /// @brief 摘掉当前那层再装一次（唯一重建入口；由 `tick()` 的状态腿调用）。
    auto refresh_overlay() -> void;
    [[nodiscard]] auto build_overlay() -> std::shared_ptr<aurora::Column>;
    [[nodiscard]] auto build_row(const MonitorRow &row) -> aurora::Node;

    auto on_timestamp_changed(bool next) -> void;
    auto on_text_view_changed(bool next) -> void;
    auto on_pause_changed(bool next) -> void;
    auto on_clear() -> void;
    auto on_latest() -> void;
    auto on_copy() -> void;
    auto on_export() -> void;

    auto clear_state() -> void;

    class aurora::OverlayHost *host_ = nullptr;  ///< 非拥有：当前挂在谁的宿主上（`set_overlay_host` 可换）。
    Hooks hooks_{};
    std::shared_ptr<aurora::Dialog> dialog_{};
    std::optional<std::size_t> overlay_index_{};
    std::shared_ptr<aurora::LazyList> list_{};  ///< 当前浮层里的行区（重建即换新；观察滚动几何用）。
    bool open_ = false;

    // ---- 三枚面板局部量（D9①：零新设置键，重启回这里写的缺省档；稿 §11 D26① 加第三枚）----
    bool timestamp_prefix_ = true;  ///< 关掉即不折前缀，也不调 `localtime`（稿 D6④ 的那半代价）。
    bool paused_ = false;           ///< 暂停＝用户显式停更，仍可按显示档重画。
    bool text_view_ = false;        ///< 缺省关＝十六进制档；开＝按会话编码腿解出的文本档（稿 D26／D27）。
    bool frozen_ = false;           ///< 冻结＝离底自动停更（D12①）；与暂停互相独立（D14①）。
    bool rebuild_pending_ = false;  ///< 暂停/冻结中改了档、清了环或冻结边沿：下一帧补一次重建。
    bool skip_geometry_once_ = false;  ///< 「回到最新」/清空之后的首帧不判几何（旧列表还在离底位置）。
    /// 换过视图档且当下停更：下一次 `rebuild_current_view()` 先按新档重算总行数并复位到这份料源的末尾
    /// （行号坐标系是档特定的，沿用旧行号会指向别段历史——稿 D34）。跟尾态不走这条腿。
    bool rebase_rows_ = false;

    std::uint64_t seen_revision_ = 0U;  ///< 上次收下的环 revision（D17① 的早退基准）。
    std::size_t cap_bytes_ = session::kMaxRawTapBytes;  ///< 环现值上限（D16①；留痕那句的数）。
    std::size_t total_rows_ = 0U;       ///< 当前快照折出的总行数（暂停/冻结时沿用上次的值）。
    std::size_t fold_first_row_ = 0U;   ///< 当前窗口首行在 `total_rows_` 里的行号。
    float pending_scroll_offset_ = 0.0F;  ///< 建树时交给 `LazyList` 的待定偏移（D10① 的落底/保位）。

    std::vector<session::RawRun> source_{};  ///< 上次跟尾收下的整环 run 表：重折与交出去的唯一料源（D20①）。
    std::size_t ring_bytes_ = 0U;            ///< 环里现存字节数（标题行那个计数）。
    std::size_t dropped_bytes_ = 0U;         ///< 累计丢弃字节（`notice()` 的那一句）。
    std::string binding_{};                  ///< 绑定的档案名与设备路径（现取 Hook 的落地副本）。
    /// 文本档解码用的编码腿名（`binding_` 的同族副本：跟绑定一起现取，不是设置现值——稿 D28①）。
    /// 初值是「没问到会话」那一档：Hook 缺席、会话散了都停在这里，与 `make_session_decoder` 的回落同值。
    std::string encoding_{"UTF-8"};
    std::string feedback_{};                 ///< 上一次「取出去」动作的留痕（稿 §10.3；新的 revision 即清）。

    // ---- 上一帧真画出来的那份（diff 基准；与 `tunnel_panel`／`keys_panel` 同形）----
    std::vector<MonitorRow> painted_rows_{};
    std::string painted_binding_{};
    std::size_t painted_ring_bytes_ = 0U;
    std::size_t painted_dropped_bytes_ = 0U;
    std::size_t painted_cap_bytes_ = 0U;
    bool painted_frozen_ = false;
    std::string painted_feedback_{};
};

}  // namespace borealis::ui
