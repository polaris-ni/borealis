#pragma once

// ============================================================
// 终端状态机（include/borealis/term/terminal.h）
// ------------------------------------------------------------
// 架构 §5：解析器只切结构、不赋语义，语义解释在这里——把 `vt::Sequence` 执行成网格内容、
// 光标与终端模式。本模块运行在会话读线程内，是权威 grid 的**唯一写入方**（架构 §3.4），
// 不触达 UI、不做 IO，故可脱离 UI 单测（AGENTS.md §4.4 第 20 条）。
//
// 覆盖面：`SPEC.FEAT.TERM.01`（Print/Execute/ESC/CSI 显示内核 + 私有模式登记 + 字符集指派 +
// DA1/DSR 查询应答，应答经 `ResponseSink` 交给会话写通道）、
// `SPEC.FEAT.TERM.02`（16/256/真彩色，色值与来源分开存）、`SPEC.FEAT.TERM.03`（主备屏）、
// `SPEC.FEAT.TERM.05`（滚动区域、光标定位/保存恢复/可见性、擦除与插删、制表位）、
// `SPEC.FEAT.TERM.07`（OSC 0/2/7/8/52/133 的消费，结果形态见 `term/osc.h`）、
// `SPEC.FEAT.TERM.08` 的占位机制（格数由注入的 WidthPolicy 给出，本模块不查表）、
// `SPEC.FEAT.RENDER.04` 的状态机前置（DECSCUSR 光标形态与闪烁档；形态绘制归渲染侧）。
// ============================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/storage.h"
#include "borealis/term/charset.h"
#include "borealis/term/osc.h"
#include "borealis/term/width.h"
#include "borealis/vt/parser.h"
#include "borealis/vt/sequence.h"

namespace borealis::term {

/// @brief 光标位置（视口内坐标，0 基；不含 scrollback 行）。
struct Cursor {
    std::size_t row = 0;
    std::size_t column = 0;
};

/// @brief 光标形态（`SPEC.FEAT.RENDER.04` 的三形态，DECSCUSR 的形态档）。
enum class CursorShape : std::uint8_t {
    Block,      ///< 实心块。
    Underline,  ///< 下划线。
    Bar,        ///< 竖线。
};

/// @brief 终端模式的当前取值，DECSET/DECRST 与 ESC 指派的可观察结果。
struct TermModes {
    bool cursor_key_app = false;   ///< `?1` DECCKM：方向键发 SS3 而非 CSI。
    bool insert_mode = false;      ///< `?4` IRM（`CSI 4 h` 与 `CSI ?4 h` 同效）。
    bool origin_mode = false;      ///< `?6` DECOM：定位以滚动区域为基准。
    bool auto_wrap = true;         ///< `?7` DECAWM。
    bool cursor_visible = true;    ///< `?25` DECTCEM。
    bool alternate_screen = false; ///< `?47` / `?1047` / `?1049`。
    bool bracketed_paste = false;  ///< `?2004`。
    bool focus_reporting = false;  ///< `?1004`。
    bool application_keypad = false; ///< `ESC =` / `ESC >`。
    bool utf8_received = false;    ///< `ESC % G` / `ESC % @`：接收侧字符集口径。
    CursorShape cursor_shape = CursorShape::Block; ///< `CSI Ps SP q` DECSCUSR 的形态档。
    bool cursor_blinking = true;   ///< DECSCUSR 的闪烁档；失焦降级归渲染侧，不在此表达。
};

/// @brief DECSTBM 滚动区域（视口内行号，闭区间）。
struct ScrollRegion {
    std::size_t top = 0;
    std::size_t bottom = 0;
};

/// @brief 终端响应回写接缝：查询类序列（DSR / DA1）的应答经此离开状态机。
///
/// 回调发生在 `Terminal::feed` 期间，也就是会话持有网格锁的临界区内——实现方**只能登记**
/// 应答文本，不得在此直接写连接或触达 UI（架构 §3.4「临界区内不做阻塞 IO」）。
/// 焦点上报与鼠标上报（`SPEC.FEAT.TERM.01` / `SPEC.FEAT.TERM.06`）落地时同走本接缝。
class ResponseSink {
  public:
    virtual ~ResponseSink() = default;

    /// @brief 收到一段应答文本（码点流，尚未按会话编码成字节）。
    /// @param response 应答文本，仅在本调用期间有效。
    virtual auto on_response(std::u32string_view response) -> void = 0;
};

/// @brief 语义解释层：把解析出的语义单元执行成网格与模式变更。
class Terminal final : public vt::SequenceSink {
  public:
    /// @brief 建一块终端。
    /// @param columns 列数。
    /// @param rows 视口行数。
    /// @param scrollback_limit 主屏 scrollback 容量；备屏恒为 0（架构 §4.4）。
    /// @param width_policy 宽度判定接缝，生命周期由调用方保证（架构 §6.3）。
    Terminal(std::size_t columns, std::size_t rows, std::size_t scrollback_limit,
             const WidthPolicy &width_policy);

    /// @brief 喂入一段已解码的码点流：内部解析并立即执行其语义。
    /// @param text 码点流（可跨调用任意分片）。
    auto feed(std::u32string_view text) -> void;

    /// @brief 执行一个语义单元；直接喂序列可绕过解析器，供单测定点驱动。
    /// @param seq 语义单元（其内视图仅在本调用期间有效）。
    auto on_sequence(const vt::Sequence &seq) -> void override;

    /// @brief 当前生效的网格（备屏期间为备屏）。
    [[nodiscard]] auto active_grid() noexcept -> grid::Storage &;

    /// @brief 主屏网格（含 scrollback）；备屏内容不进它，退出备屏即恢复原状（`SPEC.FEAT.TERM.03`）。
    [[nodiscard]] auto main_grid() noexcept -> grid::Storage &;

    /// @brief 光标位置。
    [[nodiscard]] auto cursor() const noexcept -> Cursor { return cursor_; }

    /// @brief 终端模式取值。
    [[nodiscard]] auto modes() const noexcept -> const TermModes & { return modes_; }

    /// @brief 滚动区域。
    [[nodiscard]] auto scroll_region() const noexcept -> ScrollRegion { return {region_top_, region_bottom_}; }

    /// @brief 行号与内容的对应关系是否整体变了（整屏滚动、清屏、主备屏切换）。
    ///
    /// 增量快照的读取方据此重建本地副本；行级脏区表达不出「平移了一屏」（架构 §3.4）。
    [[nodiscard]] auto full_screen_dirty() const noexcept -> bool { return full_screen_dirty_; }

    /// @brief 消费整屏脏标记（帧边界调用）。
    auto clear_full_screen_dirty() noexcept -> void { full_screen_dirty_ = false; }

    /// @brief 设定 Ambiguous 类宽度口径（profile 级覆盖，裁决 7.15）。
    /// @param ambiguous 新口径。
    auto set_ambiguous_width(AmbiguousWidth ambiguous) noexcept -> void { ambiguous_ = ambiguous; }

    /// @brief 挂上响应回写接缝（会话层在构造后接线；查询应答在挂上之前一律丢弃）。
    /// @param sink 应答接收端，生命周期由调用方保证，且不短于本终端；传 nullptr 即摘除。
    auto set_response_sink(ResponseSink *sink) noexcept -> void { response_sink_ = sink; }

    /// @brief OSC 消费留下的状态快照（标题、工作目录、命令块边界）。
    [[nodiscard]] auto osc_state() const noexcept -> const OscState & { return osc_state_; }

    /// @brief 解析网格里某个超链接标识的目标。
    ///
    /// 链接表有界（`kMaxHyperlinks`），标识**永不复用**：解析不出即表示该链接已被淘汰规则回收，
    /// 读取方按「不可点」处理，而不是指向另一条 URL。
    /// @param link_id 网格格子上挂的标识（`grid::Row::hyperlink`）。
    /// @return URI 原文；无对应链接时为 `std::nullopt`。
    [[nodiscard]] auto hyperlink_target(grid::HyperlinkId link_id) const
        -> std::optional<std::u32string>;

    /// @brief 取走待写入系统剪贴板的文本（`OSC 52` 写方向）。
    ///
    /// 状态机跑在会话读线程的网格锁内，剪贴板是主线程的 IO，故这里只留存、不落地（架构 §3.4）：
    /// 取走即清空，同一段输入里的多次写只留最后一份（合并而非丢弃）。
    /// @return 待写文本；无待写请求时为 `std::nullopt`（空文本是合法请求，故用 has_value 判别）。
    auto take_clipboard_write() -> std::optional<std::u32string>;

    /// @brief 变更视口尺寸（PTY 尺寸同步下发，`SPEC.FEAT.XFER.01`）。
    ///
    /// 主备屏同步改尺寸：列宽变更不 reflow（裁决 7.5），行数变更只移动窗口边界、
    /// 历史自动收回或溢出（存储层 `set_rows`）。滚动区域与待换行状态随尺寸失效，
    /// 光标与被保存的光标位置钳进新视口，并以整屏脏通知读取方重建副本（架构 §3.4）。
    /// @param columns 新列数。
    /// @param rows 新行数。
    auto resize(std::size_t columns, std::size_t rows) -> void;

    /// @brief 把状态机恢复到上电态（`ESC c` RIS、会话重设、编码切换）。
    auto reset_to_default() -> void;

  private:
    /// @brief 当前写入的网格缓冲区。
    [[nodiscard]] auto buffer() noexcept -> grid::Storage & { return modes_.alternate_screen ? alt_ : main_; }

    /// @brief 视口行数。
    [[nodiscard]] auto rows() const noexcept -> std::size_t;

    /// @brief 列数。
    [[nodiscard]] auto columns() const noexcept -> std::size_t;

    auto do_print(const vt::Sequence &seq) -> void;
    auto do_execute(const vt::Sequence &seq) -> void;
    auto do_escape(const vt::Sequence &seq) -> void;
    auto do_csi(const vt::Sequence &seq) -> void;
    auto designate_charset(std::size_t slot, char32_t final_byte) -> void;
    auto apply_sgr(const vt::Sequence &seq) -> void;
    auto read_extended_color(const vt::Sequence &seq, std::size_t &index, std::uint32_t &color,
                             grid::ColorSource &source) -> bool;
    auto set_dec_mode(std::span<const vt::Param> params, bool enable, bool private_mode) -> void;
    auto set_alternate_screen(std::int32_t mode, bool enable) -> void;

    auto do_osc(const vt::Sequence &seq) -> void;
    auto apply_hyperlink(std::u32string_view args) -> void;
    auto apply_clipboard(std::u32string_view args) -> void;
    auto apply_prompt_marker(std::u32string_view args) -> void;

    /// @brief 在光标处写入一个 cell（含双宽延续格与「覆盖后半格」的清理）。
    auto write_cell(char32_t code_point, std::uint8_t width) -> void;

    /// @brief 把零宽码点并入光标左侧最近的基础格（`SPEC.FEAT.TERM.08` 的 combining 腿）。
    auto attach_combining(char32_t code_point) -> void;

    /// @brief 以当前笔的空白格填充某行的一段列区间（闭开区间）。
    auto fill_blank(std::size_t row, std::size_t first, std::size_t last) -> void;

    /// @brief 擦除显示区（CSI J）。
    auto erase_display(std::int32_t mode) -> void;

    /// @brief 擦除行（CSI K）。
    auto erase_line(std::int32_t mode) -> void;

    /// @brief 行内插入/删除 @p count 个格（`@` / `P` / `X`）；负数表示删除。
    auto shift_cells(std::size_t count, bool insert) -> void;

    /// @brief 光标下移一行，到带底则滚动文本。
    auto line_down() -> void;

    /// @brief 光标上移一行，到带顶则反向滚动文本。
    auto line_up() -> void;

    /// @brief 滚动 `[top, bottom]` 行带；整屏滚动才与 scrollback 相互作用。
    auto scroll_text(std::size_t top, std::size_t bottom, std::size_t count, bool up) -> void;

    /// @brief 把光标移到 (@p row, @p column)，并清掉待换行状态。
    auto move_to(std::size_t row, std::size_t column) -> void;

    /// @brief 光标列位的行内钳制（含双宽字符不得停在延续格上的口径）。
    [[nodiscard]] auto clamp_column(std::size_t column) const noexcept -> std::size_t;

    /// @brief 把一段应答交给回写接缝；未接线时丢弃（单测可只喂序列不接 sink）。
    auto emit_response(std::u32string_view response) -> void;

    auto set_tab_stops_default() noexcept -> void;

    std::array<Charset, 4> designated_{Charset::Ascii, Charset::Ascii, Charset::Ascii, Charset::Ascii};
    grid::Storage main_;
    grid::Storage alt_;
    grid::Cell pen_{};  ///< SGR 模板：下一次写入携带的属性。
    vt::Parser parser_;
    std::vector<bool> tab_stops_;
    const WidthPolicy &width_policy_;
    Cursor cursor_{};
    Cursor saved_main_{};
    Cursor saved_alt_{};
    TermModes modes_{};
    std::size_t region_top_ = 0;
    std::size_t region_bottom_ = 0;
    AmbiguousWidth ambiguous_ = AmbiguousWidth::Narrow;
    ResponseSink *response_sink_ = nullptr;
    OscState osc_state_{};                                    ///< OSC 消费留下的状态快照（架构 §5.4）。
    std::map<grid::HyperlinkId, std::u32string> hyperlinks_;  ///< 标识 → URI；满则淘汰最旧一条。
    /// 链接标识递增分配且**永不复用**：复用会让 scrollback 里的旧链接指向另一条 URL。
    grid::HyperlinkId next_hyperlink_id_ = 1;
    grid::HyperlinkId current_hyperlink_ = grid::kNoHyperlink;  ///< 笔上挂着的链接（`OSC 8` 区间）。
    std::optional<std::u32string> clipboard_write_;  ///< 待写剪贴板文本：锁内留存、主线程取走落地。
    bool pending_wrap_ = false;  ///< 已在行末落字、下一个可打印字符须先换行（DECAWM 的延迟换行）。
    bool full_screen_dirty_ = false;
};

}  // namespace borealis::term
