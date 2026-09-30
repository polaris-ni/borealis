#pragma once

// ============================================================
// 终端状态机（include/borealis/term/terminal.h）
// ------------------------------------------------------------
// 架构 §5：解析器只切结构、不赋语义，语义解释在这里——把 `vt::Sequence` 执行成网格内容、
// 光标与终端模式。本模块运行在会话读线程内，是权威 grid 的**唯一写入方**（架构 §3.4），
// 不触达 UI、不做 IO，故可脱离 UI 单测（AGENTS.md §4.4 第 20 条）。
//
// 覆盖面：`SPEC.FEAT.TERM.01`（Print/Execute/ESC/CSI 显示内核 + 私有模式登记 + 字符集指派）、
// `SPEC.FEAT.TERM.02`（16/256/真彩色，色值与来源分开存）、`SPEC.FEAT.TERM.03`（主备屏）、
// `SPEC.FEAT.TERM.05`（滚动区域、光标定位/保存恢复/可见性、擦除与插删、制表位）、
// `SPEC.FEAT.TERM.08` 的占位机制（格数由注入的 WidthPolicy 给出，本模块不查表）。
// ============================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/storage.h"
#include "borealis/term/charset.h"
#include "borealis/term/width.h"
#include "borealis/vt/parser.h"
#include "borealis/vt/sequence.h"

namespace borealis::term {

/// @brief 光标位置（视口内坐标，0 基；不含 scrollback 行）。
struct Cursor {
    std::size_t row = 0;
    std::size_t column = 0;
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
};

/// @brief DECSTBM 滚动区域（视口内行号，闭区间）。
struct ScrollRegion {
    std::size_t top = 0;
    std::size_t bottom = 0;
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

    /// @brief 在光标处写入一个 cell（含双宽延续格与「覆盖后半格」的清理）。
    auto write_cell(char32_t code_point, std::uint8_t width) -> void;

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
    bool pending_wrap_ = false;  ///< 已在行末落字、下一个可打印字符须先换行（DECAWM 的延迟换行）。
    bool full_screen_dirty_ = false;
};

}  // namespace borealis::term
