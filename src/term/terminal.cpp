// ============================================================
// 终端状态机实现（src/term/terminal.cpp）
// ------------------------------------------------------------
// 语义解释的唯一落点：解析器给的 Sequence 在这里变成网格内容、光标与模式（架构 §5.1）。
// 坐标一律**视口内 0 基**；序列参数是 1 基，出入本模块时各自换算，不做「行号 +1 存起来」
// 那类混合口径。
// ============================================================

#include "borealis/term/terminal.h"

#include <algorithm>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include "borealis/term/charset.h"
#include "osc.h"

namespace borealis::term {

namespace {

/// @brief 制表位的默认间距（列）。
constexpr std::size_t kDefaultTabInterval = 8;

/// @brief 24-bit 色值的单通道上界。
constexpr std::int32_t kChannelMax = 255;

/// @brief xterm 调色板的索引上界。
constexpr std::int32_t kPaletteMax = 255;

/// @brief 拼出 0xRRGGBB。
[[nodiscard]] auto make_rgb(std::int32_t red, std::int32_t green, std::int32_t blue) noexcept -> std::uint32_t {
    const auto channel = [](std::int32_t value) noexcept {
        return static_cast<std::uint32_t>(std::clamp(value, 0, kChannelMax));
    };
    return (channel(red) << 16U) | (channel(green) << 8U) | channel(blue);
}

/// @brief 调色板索引钳制后转色值字段。
[[nodiscard]] auto make_palette(std::int32_t index) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(std::clamp(index, 0, kPaletteMax));
}

/// @brief 把非负整数按十进制追加进应答文本（MSVC 的 `<string>` 不提供 `std::to_u32string`）。
auto append_decimal(std::u32string &out, std::size_t value) -> void {
    for (const char digit : std::to_string(value)) {
        out.push_back(static_cast<char32_t>(digit));
    }
}

/// @brief DECSCUSR 的形态档：3/4 下划线、5/6 竖线，其余（1/2）为块。
/// @param style `CSI Ps SP q` 的 Ps，调用方保证在 1..6 之内。
/// @return 对应形态。
[[nodiscard]] constexpr auto cursor_shape_for(std::int32_t style) noexcept -> CursorShape {
    switch (style) {
        case 3:
        case 4:
            return CursorShape::Underline;
        case 5:
        case 6:
            return CursorShape::Bar;
        default:
            return CursorShape::Block;
    }
}

/// @brief `SGR 4:x` 子参数的下划线笔形（映射与取舍见裁决 7.28①③）。
/// @param code 子参数值；`SGR 4` 的平参数写法由调用方按 1 传入。
/// @return 对应档位；dotted(4)、dashed(5) 与域外码都落单线。
[[nodiscard]] constexpr auto underline_style_for(std::int32_t code) noexcept -> grid::UnderlineStyle {
    switch (code) {
        case 0:
            return grid::UnderlineStyle::None;
        case 2:
            return grid::UnderlineStyle::Double;
        case 3:
            return grid::UnderlineStyle::Curly;
        default:
            return grid::UnderlineStyle::Single;
    }
}

}  // namespace

Terminal::Terminal(std::size_t columns, std::size_t rows, std::size_t scrollback_limit,
                   const WidthPolicy &width_policy, TerminalDefaults defaults)
    : main_{columns, rows, scrollback_limit},
      alt_{columns, rows, 0},
      width_policy_{width_policy},
      defaults_{defaults},
      region_bottom_{rows - 1} {
    modes_.cursor_shape = defaults.cursor_shape;
    modes_.cursor_blinking = defaults.cursor_blinking;
    ambiguous_ = defaults.ambiguous_width;
    tab_stops_.assign(columns, false);
    set_tab_stops_default();
}

auto Terminal::feed(std::u32string_view text) -> void {
    parser_.feed(text, *this);
}

auto Terminal::active_grid() noexcept -> grid::Storage & { return buffer(); }

auto Terminal::main_grid() noexcept -> grid::Storage & { return main_; }

auto Terminal::rows() const noexcept -> std::size_t {
    return modes_.alternate_screen ? alt_.visible_rows() : main_.visible_rows();
}

auto Terminal::columns() const noexcept -> std::size_t {
    return modes_.alternate_screen ? alt_.columns() : main_.columns();
}

auto Terminal::on_sequence(const vt::Sequence &seq) -> void {
    switch (seq.kind) {
        case vt::SequenceKind::Print:
            do_print(seq);
            break;
        case vt::SequenceKind::Execute:
            do_execute(seq);
            break;
        case vt::SequenceKind::Escape:
            do_escape(seq);
            break;
        case vt::SequenceKind::Csi:
            do_csi(seq);
            break;
        case vt::SequenceKind::Osc:
            do_osc(seq);
            break;
        case vt::SequenceKind::DcsHook:
        case vt::SequenceKind::DcsPut:
        case vt::SequenceKind::DcsUnhook:
            // TODO(SPEC.FEAT.TERM.01): DCS 负载（DECRQPS 一类）尚未消费。
            break;
        case vt::SequenceKind::Ignored:
            break;  // 计数归解析器（架构 §5.5）
    }
}

auto Terminal::do_print(const vt::Sequence &seq) -> void {
    const char32_t code_point = seq.code_point;
    // 宽度按**字符集映射前**的码点判定：框线表把 0x5F–0x7E 重映射成 U+2500 一类箱线字符，
    // 那些码点在 East Asian Width 里属 Ambiguous，Ambiguous=Wide 的 profile 会把边框画成
    // 半格错位——线条字符恒单宽才是既有终端的既成事实（裁决 7.15 的场景分工）。
    const std::uint8_t width = width_policy_.width_of(code_point, ambiguous_);
    if (width == 0U) {
        // 零宽码点不占格也不推进光标，更不得触发换行——它并进光标左侧的基础格（SPEC.FEAT.TERM.08）。
        // 不经字符集映射：DEC 表只重定义 0x5F–0x7E，其中无零宽码点。
        attach_combining(code_point);
        return;
    }
    if (modes_.auto_wrap && pending_wrap_) {
        cursor_.column = 0;
        line_down();
        pending_wrap_ = false;
    }
    if (modes_.auto_wrap && cursor_.column + width > columns()) {
        cursor_.column = 0;
        line_down();
    }
    if (modes_.insert_mode) {
        shift_cells(width, true);
    }
    // GL 槽位取 G0：SO/SI 换槽的 GR 指派尚未落地（见 do_execute）。
    write_cell(map_charset(designated_[0], code_point), width);
    if (cursor_.column + width >= columns()) {
        cursor_.column = columns() - 1;
        pending_wrap_ = modes_.auto_wrap;
    } else {
        cursor_.column += width;
        pending_wrap_ = false;
    }
}

auto Terminal::do_execute(const vt::Sequence &seq) -> void {
    switch (seq.code_point) {
        case U'\x08':  // BS
            if (cursor_.column > 0) {
                --cursor_.column;
            }
            pending_wrap_ = false;
            break;
        case U'\x09': {  // HT：下一个制表位，无则停在最后一列
            std::size_t target = columns() - 1;
            for (std::size_t column = cursor_.column + 1; column < columns(); ++column) {
                if (column < tab_stops_.size() && tab_stops_[column]) {
                    target = column;
                    break;
                }
            }
            cursor_.column = target;
            pending_wrap_ = false;
            break;
        }
        case U'\x0A':  // LF
        case U'\x0B':  // VT
        case U'\x0C':  // FF
            line_down();
            break;
        case U'\x0D':  // CR
            cursor_.column = 0;
            pending_wrap_ = false;
            break;
        case U'\x0E':  // SO
        case U'\x0F':  // SI
            // TODO(SPEC.FEAT.TERM.01): SO/SI 切换 GL 用 G1/G0，本模块只消费 G0 槽位。
            break;
        default:
            break;  // 含 BEL：其视觉与可听提示归 SPEC.FEAT.WS.04，此处不产副作用
    }
}

auto Terminal::do_escape(const vt::Sequence &seq) -> void {
    if (!seq.intermediates.empty()) {
        switch (seq.intermediates[0]) {
            case U'(':
                designate_charset(0, seq.final_byte);
                return;
            case U')':
                designate_charset(1, seq.final_byte);
                return;
            case U'*':
                designate_charset(2, seq.final_byte);
                return;
            case U'+':
                designate_charset(3, seq.final_byte);
                return;
            case U'%':
                // 只登记接收侧口径：G 为 UTF-8、@ 为 Latin-1。框线映射照旧生效——
                // UTF-8 模式下 ESC ( 0 仍能画线条，这是 xterm 的既成行为。
                modes_.utf8_received = seq.final_byte == U'G';
                return;
            default:
                return;  // ESC SP 一类的未支持组合整体忽略
        }
    }
    switch (seq.final_byte) {
        case U'7':  // DECSC
            if (modes_.alternate_screen) {
                saved_alt_ = cursor_;
            } else {
                saved_main_ = cursor_;
            }
            break;
        case U'8':  // DECRC
            cursor_ = modes_.alternate_screen ? saved_alt_ : saved_main_;
            pending_wrap_ = false;
            break;
        case U'D':  // IND
            line_down();
            break;
        case U'E':  // NEL
            cursor_.column = 0;
            line_down();
            break;
        case U'M':  // RI
            line_up();
            break;
        case U'=':
            modes_.application_keypad = true;
            break;
        case U'>':
            modes_.application_keypad = false;
            break;
        case U'c':  // RIS
            reset_to_default();
            break;
        default:
            break;
    }
}

auto Terminal::do_csi(const vt::Sequence &seq) -> void {
    const auto params = seq.params;
    const bool private_mode = !seq.intermediates.empty() && seq.intermediates.front() == U'?';
    // 相对纵向移动限定在滚动区域内：带状态行/命令行的 TUI 靠 DECSTBM 表达「正文区」，
    // 光标跑出带外就会把它们盖掉。绝对定位仍按整屏解释（除 DECOM）。
    const std::size_t band_top = region_top_;
    const std::size_t band_bottom = region_bottom_;
    // 序列参数是 1 基，且显式写 0 与缺省同义（`CSI 0 G` 等同 `CSI G`），故先取下界再转 0 基。
    const auto one_based = [&params](std::size_t position, std::size_t fallback) noexcept {
        const auto value = std::max(1, vt::param_or(params, position, static_cast<std::int32_t>(fallback)));
        return static_cast<std::size_t>(value) - 1;
    };
    // 参数缺省与上界钳制：一次取参数不许超过屏大小，免得后续加减溢出。
    const auto bounded = [&params](std::size_t position, std::size_t fallback, std::size_t upper) noexcept {
        const auto value = std::max(1, vt::param_or(params, position, static_cast<std::int32_t>(fallback)));
        return std::min(static_cast<std::size_t>(value), upper);
    };

    switch (seq.final_byte) {
        case U'A': {  // CUU
            const auto count = bounded(0, 1, rows());
            cursor_.row = cursor_.row > band_top + count ? cursor_.row - count : band_top;
            pending_wrap_ = false;
            break;
        }
        case U'B': {  // CUD
            const auto count = bounded(0, 1, rows());
            cursor_.row = std::min(cursor_.row + count, band_bottom);
            pending_wrap_ = false;
            break;
        }
        case U'C': {  // CUF
            const auto count = bounded(0, 1, columns());
            cursor_.column = std::min(cursor_.column + count, columns() - 1);
            pending_wrap_ = false;
            break;
        }
        case U'D': {  // CUB
            const auto count = bounded(0, 1, columns());
            cursor_.column = cursor_.column > count ? cursor_.column - count : 0;
            pending_wrap_ = false;
            break;
        }
        case U'E': {  // CNL
            const auto count = bounded(0, 1, rows());
            cursor_.row = std::min(cursor_.row + count, band_bottom);
            cursor_.column = 0;
            pending_wrap_ = false;
            break;
        }
        case U'F': {  // CPL
            const auto count = bounded(0, 1, rows());
            cursor_.row = cursor_.row > band_top + count ? cursor_.row - count : band_top;
            cursor_.column = 0;
            pending_wrap_ = false;
            break;
        }
        case U'G':  // CHA
            cursor_.column = clamp_column(one_based(0, 1));
            pending_wrap_ = false;
            break;
        case U'd':  // VPA
        case U'e':  // VPA 的同义写法
            cursor_.row = modes_.origin_mode ? std::min(band_top + one_based(0, 1), band_bottom)
                                             : std::min(one_based(0, 1), rows() - 1);
            pending_wrap_ = false;
            break;
        case U'H':  // CUP
        case U'f':  // HVP
        case U'`': {  // HPA：只给列
            const auto row = one_based(0, 1);
            const auto column = one_based(1, 1);
            cursor_.row = modes_.origin_mode ? std::min(band_top + row, band_bottom) : std::min(row, rows() - 1);
            cursor_.column = seq.final_byte == U'`' ? clamp_column(row) : clamp_column(column);
            pending_wrap_ = false;
            break;
        }
        case U'J':  // ED
            erase_display(vt::param_or(params, 0, 0));
            break;
        case U'K':  // EL
            erase_line(vt::param_or(params, 0, 0));
            break;
        case U'X': {  // ECH
            const auto count = bounded(0, 1, columns());
            fill_blank(cursor_.row, cursor_.column, std::min(cursor_.column + count, columns()));
            break;
        }
        case U'@':  // ICH
            shift_cells(bounded(0, 1, columns()), true);
            break;
        case U'P':  // DCH
            shift_cells(bounded(0, 1, columns()), false);
            break;
        case U'L':  // IL：区域内插空行，不进 scrollback
            if (cursor_.row >= band_top && cursor_.row <= band_bottom) {
                buffer().scroll_region_down(cursor_.row, band_bottom, bounded(0, 1, rows()));
            }
            break;
        case U'M':  // DL
            if (cursor_.row >= band_top && cursor_.row <= band_bottom) {
                buffer().scroll_region_up(cursor_.row, band_bottom, bounded(0, 1, rows()));
            }
            break;
        case U'S':  // SU
            scroll_text(band_top, band_bottom, bounded(0, 1, rows()), true);
            break;
        case U'T':  // SD
            scroll_text(band_top, band_bottom, bounded(0, 1, rows()), false);
            break;
        case U'r': {  // DECSTBM
            const auto top = one_based(0, 1);
            const auto bottom = one_based(1, rows());
            if (top < bottom && bottom < rows()) {
                region_top_ = top;
                region_bottom_ = bottom;
                // DECSTBM 复位制表位是 VT 既有行为：不清的话 HT 会跳到新区域里的旧停位。
                set_tab_stops_default();
                move_to(modes_.origin_mode ? region_top_ : 0, 0);
            }
            break;
        }
        case U'h':  // DECSET / SET
            set_dec_mode(params, true, private_mode);
            break;
        case U'l':  // DECRST / RESET
            set_dec_mode(params, false, private_mode);
            break;
        case U'm':  // SGR
            apply_sgr(seq);
            break;
        case U's':  // SCOSC
            if (modes_.alternate_screen) {
                saved_alt_ = cursor_;
            } else {
                saved_main_ = cursor_;
            }
            break;
        case U'u':  // SCORC
            cursor_ = modes_.alternate_screen ? saved_alt_ : saved_main_;
            pending_wrap_ = false;
            break;
        case U'q': {  // DECSCUSR
            // 中间字节必须是空格：`CSI q`（无中间字节）与带其它中间字节的写法不是本序列，按未识别忽略。
            if (seq.intermediates.empty() || seq.intermediates.back() != U' ') {
                break;
            }
            const auto style = vt::param_or(params, 0, 0);
            // 档位表照 xterm：1/2 块、3/4 下划线、5/6 竖线，奇数闪烁、偶数静止。
            const bool defined = style >= 1 && style <= 6;
            // `CSI 0 q` 与越界档位整档回落默认闪烁块，而不是保留旧值——设备发未定义档位通常意在复位，
            // 留着上一档会让形态取决于历史输入。
            modes_.cursor_shape = defined ? cursor_shape_for(style) : CursorShape::Block;
            modes_.cursor_blinking = !defined || (style % 2) == 1;
            break;
        }
        case U'I':  // HTS：在光标列设定制表位（SPEC.FEAT.TERM.05 的制表位三项之一）
            if (cursor_.column < tab_stops_.size()) {
                tab_stops_[cursor_.column] = true;
            }
            break;
        case U'g': {  // TBC
            const auto mode = vt::param_or(params, 0, 0);
            if (mode == 0 && cursor_.column < tab_stops_.size()) {
                tab_stops_[cursor_.column] = false;
            } else if (mode == 3) {
                std::fill(tab_stops_.begin(), tab_stops_.end(), false);
            }
            break;
        }
        case U'n': {  // DSR
            if (private_mode) {
                break;  // `CSI ? 6 n`（DEC 定位器光标报告）未实现：宁可不答，也不答一份错格式
            }
            const auto report = vt::param_or(params, 0, 0);
            if (report == 5) {
                emit_response(U"\x1B[0n");  // 无故障；终端无自检语义，恒按可用应答
            } else if (report == 6) {
                // 报告口径是**整屏**视口坐标（1 基），与 DECOM 原点模式无关：vim 用它校准光标行。
                std::u32string position = U"\x1B[";
                append_decimal(position, cursor_.row + 1U);
                position += U';';
                append_decimal(position, clamp_column(cursor_.column) + 1U);
                position += U'R';
                emit_response(position);
            }
            break;
        }
        case U'c':  // DA1
            // 只报能力档位 62（VT220 + 高级视频选项），与 `SPEC.FEAT.TERM.01` 声明的
            // xterm/VT100/VT220 兼容口径一致。132 列、sixel/ReGIS、打印机这类附加能力号
            // 一律不报：前者是 §2.2 裁剪项，误报会让 vim/tmux 走我们没实现的分支。
            // `CSI > c`（DA2）与 `CSI ? c` 不在本需求覆盖内，忽略。
            if (seq.intermediates.empty()) {
                emit_response(U"\x1B[?62c");
            }
            break;
        default:
            break;  // 未识别终结符：忽略而不中断（架构 §5.5）
    }
}

auto Terminal::emit_response(std::u32string_view response) -> void {
    if (response_sink_ != nullptr) {
        response_sink_->on_response(response);
    }
}

auto Terminal::designate_charset(std::size_t slot, char32_t final_byte) -> void {
    Charset charset = Charset::Ascii;
    // 未实现的字符集退回 ASCII 而非中断：ncurses 常指派 DEC Supplemental 一类我们没做的表。
    designated_[slot] = charset_from_designator(final_byte, charset) ? charset : Charset::Ascii;
}

auto Terminal::apply_sgr(const vt::Sequence &seq) -> void {
    if (seq.params.empty()) {
        pen_ = grid::Cell{};  // `CSI m` 等价 `CSI 0 m`
        return;
    }
    for (std::size_t index = 0; index < seq.params.size(); ++index) {
        const auto value = seq.params[index].value_or(0);
        std::uint32_t color = 0;
        grid::ColorSource source = grid::ColorSource::Default;
        if ((value == 38 || value == 48 || value == 58) &&
            read_extended_color(seq, index, color, source)) {
            if (value == 38) {
                pen_.foreground = color;
                pen_.foreground_source = source;
            } else if (value == 48) {
                pen_.background = color;
                pen_.background_source = source;
            }
            // 58 是下划线颜色：下划线专用样式未建模，参数照消费以免后续串错位。
            continue;
        }
        switch (value) {
            case 0:
                pen_ = grid::Cell{};
                break;
            case 1:
                pen_.flags |= grid::kFlagBold;
                break;
            case 2:
                pen_.flags |= grid::kFlagDim;
                break;
            case 3:
                pen_.flags |= grid::kFlagItalic;
                break;
            case 4: {
                // 笔形只在子参数写法里出现（`4:0`…`4:5`）；平参数 `CSI 4 m` 即单线。
                const auto &param = seq.params[index];
                pen_.underline = underline_style_for(param.sub.size() > 1U ? param.sub[1] : 1);
                break;
            }
            case 5:
            case 6:
                pen_.flags |= grid::kFlagBlink;
                break;
            case 7:
                pen_.flags |= grid::kFlagReverse;
                break;
            case 8:
                pen_.flags |= grid::kFlagHidden;
                break;
            case 9:
                pen_.flags |= grid::kFlagStrike;
                break;
            case 21:
                // ECMA-48 的双线；xterm 把 21 实现成「关粗体」是它的历史分歧，标准写法是 22（裁决 7.28②）。
                pen_.underline = grid::UnderlineStyle::Double;
                break;
            case 53:
                break;  // overline 未建模
            case 22:
                pen_.flags &= ~static_cast<grid::CellFlags>(grid::kFlagBold | grid::kFlagDim);
                break;
            case 23:
                pen_.flags &= ~grid::kFlagItalic;
                break;
            case 24:
                pen_.underline = grid::UnderlineStyle::None;
                break;
            case 25:
                pen_.flags &= ~grid::kFlagBlink;
                break;
            case 27:
                pen_.flags &= ~grid::kFlagReverse;
                break;
            case 28:
                pen_.flags &= ~grid::kFlagHidden;
                break;
            case 29:
                pen_.flags &= ~grid::kFlagStrike;
                break;
            case 39:
                pen_.foreground = grid::kColorDefault;
                pen_.foreground_source = grid::ColorSource::Default;
                break;
            case 49:
                pen_.background = grid::kColorDefault;
                pen_.background_source = grid::ColorSource::Default;
                break;
            default:
                if (value >= 30 && value <= 37) {
                    pen_.foreground = make_palette(value - 30);
                    pen_.foreground_source = grid::ColorSource::Palette;
                } else if (value >= 40 && value <= 47) {
                    pen_.background = make_palette(value - 40);
                    pen_.background_source = grid::ColorSource::Palette;
                } else if (value >= 90 && value <= 97) {
                    pen_.foreground = make_palette(value - 90 + 8);
                    pen_.foreground_source = grid::ColorSource::Palette;
                } else if (value >= 100 && value <= 107) {
                    pen_.background = make_palette(value - 100 + 8);
                    pen_.background_source = grid::ColorSource::Palette;
                }
                // 其余（10–19 字体选择、26 文本书写方向等）未建模，忽略即可。
                break;
        }
    }
}

auto Terminal::read_extended_color(const vt::Sequence &seq, std::size_t &index, std::uint32_t &color,
                                   grid::ColorSource &source) -> bool {
    const auto &param = seq.params[index];
    // 子参数写法 `38:2::r:g:b`（含色彩空间缺省段）与 `38:2:r:g:b` 都要吃：解析器把整串收成
    // 一格的 sub 列表，通道值恒在**末三格**、模式位在 sub[1]。判定只看 sub 长度——平参数式
    // （`38;5;123`）每格也带单元素 sub，用「sub 非空」区分会把两种写法一起漏掉。
    if (param.sub.size() > 1) {
        const auto kind = param.sub[1];
        if (kind == 2 && param.sub.size() >= 5) {
            const auto tail = param.sub.size() - 3;
            color = make_rgb(param.sub[tail], param.sub[tail + 1], param.sub[tail + 2]);
            source = grid::ColorSource::Rgb;
            return true;
        }
        if (kind == 5 && param.sub.size() >= 3) {
            color = make_palette(param.sub[2]);
            source = grid::ColorSource::Palette;
            return true;
        }
        return false;
    }
    // 平参数写法 `38;2;r;g;b` / `38;5;n`：颜色占掉后续参数格，index 必须同步推进，
    // 否则下一轮循环会把 r 当独立 SGR 码解释。
    const auto kind = vt::param_or(seq.params, index + 1, -1);
    if (kind == 2 && index + 4 < seq.params.size()) {
        color = make_rgb(vt::param_or(seq.params, index + 2, 0), vt::param_or(seq.params, index + 3, 0),
                         vt::param_or(seq.params, index + 4, 0));
        source = grid::ColorSource::Rgb;
        index += 4;
        return true;
    }
    if (kind == 5 && index + 2 < seq.params.size()) {
        color = make_palette(vt::param_or(seq.params, index + 2, 0));
        source = grid::ColorSource::Palette;
        index += 2;
        return true;
    }
    return false;
}

auto Terminal::set_dec_mode(std::span<const vt::Param> params, bool enable, bool private_mode) -> void {
    for (const auto &param : params) {
        const auto mode = param.value_or(0);
        if (!private_mode) {
            if (mode == 4) {
                modes_.insert_mode = enable;  // ANSI IRM 与 `?4` 同效
            }
            continue;  // 其余 ANSI 模式集（12/20 等）未落地
        }
        switch (mode) {
            case 1:
                modes_.cursor_key_app = enable;
                break;
            case 4:
                modes_.insert_mode = enable;
                break;
            case 6:
                if (modes_.origin_mode != enable) {
                    modes_.origin_mode = enable;
                    // DECOM 切换即复位光标：原点定义变了，沿用旧坐标会解释成另一个位置。
                    move_to(enable ? region_top_ : 0, 0);
                }
                break;
            case 7:
                modes_.auto_wrap = enable;
                if (!enable) {
                    pending_wrap_ = false;
                }
                break;
            case 25:
                modes_.cursor_visible = enable;
                break;
            case 1004:
                modes_.focus_reporting = enable;
                break;
            case 2004:
                modes_.bracketed_paste = enable;
                break;
            case 47:
            case 1047:
            case 1048:
            case 1049:
                set_alternate_screen(mode, enable);
                break;
            default:
                break;  // 鼠标上报（?1000/?1002/?1003/?1006）随 SPEC.FEAT.TERM.06 落地
        }
    }
}

auto Terminal::set_alternate_screen(std::int32_t mode, bool enable) -> void {
    if (mode == 1048) {
        // 只存取光标，不换屏（?1049 的复合语义里由本状态机自行处理光标）。
        if (enable) {
            if (modes_.alternate_screen) {
                saved_alt_ = cursor_;
            } else {
                saved_main_ = cursor_;
            }
        } else {
            cursor_ = modes_.alternate_screen ? saved_alt_ : saved_main_;
            pending_wrap_ = false;
        }
        return;
    }
    if (enable == modes_.alternate_screen) {
        if (enable) {
            alt_.clear();
            full_screen_dirty_ = true;
        }
        return;
    }
    if (enable) {
        if (mode == 1049) {
            saved_main_ = cursor_;
        }
        alt_.clear();
        modes_.alternate_screen = true;
        region_top_ = 0;
        region_bottom_ = rows() - 1;
        cursor_ = {};
        pending_wrap_ = false;
    } else {
        modes_.alternate_screen = false;
        alt_.clear();  // 退出即清空：备屏不进 scrollback，也不留到下次进入（SPEC.FEAT.TERM.03）
        region_top_ = 0;
        region_bottom_ = rows() - 1;
        if (mode == 1049) {
            cursor_ = saved_main_;
        } else {
            move_to(cursor_.row, cursor_.column);
        }
        pending_wrap_ = false;
    }
    full_screen_dirty_ = true;
}

auto Terminal::do_osc(const vt::Sequence &seq) -> void {
    const auto command = osc::split_command(seq.data);
    if (!command.has_value()) {
        ++osc_state_.unhandled_count;
        return;  // 连命令号都拆不出来的串同样留痕：整体吞掉会让「标题没生效」这类排障无从下手
    }
    const auto &[code, args] = *command;
    switch (code) {
        case 0:  // 图标名 + 标题两字段的老形态：只认标题（图标名连 xterm 自己都不再呈现）
        case 2:
            osc_state_.title.assign(args);
            break;
        case 1:  // 仅图标名：消费掉，不上屏也不留状态
            break;
        case 7:
            // URI 原样留存：`file://host/path` 的 host 归属判定与目录继承是 shell 集成的事。
            // TODO(SPEC.FEAT.INTEG.02): 新标签/分屏继承当前会话目录，SSH 会话在远端目录语义下
            // 降级为不继承。
            osc_state_.working_directory.assign(args);
            break;
        case 8:
            apply_hyperlink(args);
            break;
        case 52:
            apply_clipboard(args);
            break;
        case 133:
            apply_prompt_marker(args);
            break;
        default:
            // 未消费的命令号（`4`/`10`/`110` 那一族调色板与颜色设置）计数进可观测面板，
            // 不中断解析（架构 §5.5）。
            ++osc_state_.unhandled_count;
            break;
    }
}

auto Terminal::apply_hyperlink(std::u32string_view args) -> void {
    // `8;<params>;<uri>`：URI 自身可含 `;`，故只切第一刀，其后的全部内容是 URI。
    const auto fields = osc::split_first(args, U';');
    // params 段（`id=` 一类）不参与标识分配：同 id 归组只服务 hover 与点击高亮，等有消费者再做。
    if (!fields.found || fields.tail.empty()) {
        current_hyperlink_ = grid::kNoHyperlink;  // `8;;` 与光杆 `8` 都是「链接区间到此结束」
        return;
    }
    if (hyperlinks_.size() >= kMaxHyperlinks) {
        hyperlinks_.erase(hyperlinks_.begin());  // 标识递增，故 begin() 就是最旧一条
    }
    const auto link_id = next_hyperlink_id_++;
    hyperlinks_.emplace(link_id, std::u32string{fields.tail});
    current_hyperlink_ = link_id;
}

auto Terminal::apply_clipboard(std::u32string_view args) -> void {
    // `52;<Pc>[;<Pc>…;<Pd>]`：剪贴板编号可以多段给出，载荷恒在最后一段（base64 表里没有 `;`）。
    const auto fields = osc::split_last(args, U';');
    const auto payload = fields.found ? fields.tail : args;
    if (payload == U"?") {
        // 读方向默认禁止（`SPEC.FEAT.CONN.12`）：回写空响应而非静默无响应，否则远端程序等不到
        // 答复会一直挂着。
        // TODO(SPEC.FEAT.CONN.12): 三态授权（禁止/允许/每次询问）落地后，「允许」档在此把本地
        // 剪贴板内容回写成 `OSC 52 ; <Pc> ; <base64>`。
        emit_response(U"\x1B]52;c;\x07");
        return;
    }
    const auto text = osc::decode_base64(payload);
    if (!text.has_value()) {
        return;  // 载荷非法就不写：把一份错内容塞进用户剪贴板比不写更糟
    }
    // 锁内只留存，剪贴板落地归主线程（架构 §3.4）；同段输入的多次写只留最后一份。
    clipboard_write_ = std::move(*text);
    ++osc_state_.clipboard_write_requests;
}

auto Terminal::apply_prompt_marker(std::u32string_view args) -> void {
    // `133;<A|B|C|D|P>[;<该种自己的参数>]`
    // TODO(SPEC.FEAT.INTEG.01): 命令块要附着到网格行上（「复制上一命令的全部输出」、失败命令
    // 高亮），当前只留最近一次边界；`P` 段的 `Cwd=` 与 OSC 7 同源，也在那一棒统一解析。
    const auto fields = osc::split_first(args, U';');
    const auto marker = fields.head.empty() ? U'\0' : fields.head.front();
    switch (marker) {
        case U'A':
            osc_state_.prompt_marker = PromptMarker::PromptStart;
            break;
        case U'B':
            osc_state_.prompt_marker = PromptMarker::PromptEnd;
            break;
        case U'C':
            osc_state_.prompt_marker = PromptMarker::CommandStart;
            break;
        case U'D':
            osc_state_.prompt_marker = PromptMarker::CommandExecuted;
            osc_state_.command_exit_code =
                osc::decimal(fields.tail, std::numeric_limits<std::int32_t>::max());
            break;
        case U'P':
            osc_state_.prompt_marker = PromptMarker::OutputStart;
            break;
        default:
            ++osc_state_.unhandled_count;
            break;
    }
}

auto Terminal::hyperlink_target(grid::HyperlinkId link_id) const -> std::optional<std::u32string> {
    if (link_id == grid::kNoHyperlink) {
        return std::nullopt;
    }
    const auto found = hyperlinks_.find(link_id);
    return found == hyperlinks_.end() ? std::nullopt
                                      : std::optional<std::u32string>{found->second};
}

auto Terminal::take_clipboard_write() -> std::optional<std::u32string> {
    auto text = std::move(clipboard_write_);
    clipboard_write_.reset();
    return text;
}

auto Terminal::write_cell(char32_t code_point, std::uint8_t width) -> void {
    auto &row = buffer().visible_line(cursor_.row);
    // 落在双宽字符的后半格上时连前半格一起清掉，否则残留半格错位（SPEC.FEAT.TERM.08 验收线）。
    if (cursor_.column > 0 && row.cell(cursor_.column).is_wide_continuation()) {
        fill_blank(cursor_.row, cursor_.column - 1, cursor_.column);
    }
    grid::Cell cell = pen_;
    cell.code_point = code_point;
    cell.width = width;
    cell.flags = pen_.flags & ~static_cast<grid::CellFlags>(grid::kFlagWideContinuation);
    row.set(cursor_.column, cell);
    // 链接挂在笔上而不是 cell 的 SGR 属性里（`CSI m` 不清链接）：区间只由 `OSC 8` 括出。
    // 双宽字符只挂前半格——延续格本就既不停留也不承载内容。
    if (current_hyperlink_ != grid::kNoHyperlink) {
        row.set_hyperlink(cursor_.column, current_hyperlink_);
    }
    if (width >= 2U && cursor_.column + 1 < columns()) {
        grid::Cell continuation = pen_;
        continuation.code_point = 0;
        continuation.width = 0;
        continuation.flags = pen_.flags | grid::kFlagWideContinuation;
        row.set(cursor_.column + 1, continuation);
    }
}

auto Terminal::attach_combining(char32_t code_point) -> void {
    auto &row = buffer().visible_line(cursor_.row);
    // 基础格是「光标刚刚走过的那一格」。光标停在刚写过的格上而非其右侧有两种来路：延迟换行
    // 待置位（DECAWM=on 落满行末时列被夹住）、或关自动换行后行末的写入覆盖同格不推进。
    const auto standing_on_last =
        pending_wrap_ || (!modes_.auto_wrap && cursor_.column + 1U >= columns());
    if (cursor_.column == 0U && !standing_on_last) {
        return;  // 行首左侧无格可并：标记丢弃（会话字节流不可信，不得越界）
    }
    auto base = standing_on_last ? cursor_.column : cursor_.column - 1U;
    // 双宽字符的光标落在延续格上，零宽码点须并入它的前半格而不是后半格。
    while (base > 0U && row.cell(base).is_wide_continuation()) {
        --base;
    }
    row.attach_combining(base, code_point);
}

auto Terminal::fill_blank(std::size_t row_index, std::size_t first, std::size_t last) -> void {
    auto &row = buffer().visible_line(row_index);
    const auto begin = std::min(first, columns());
    const auto end = std::min(last, columns());
    // 擦除格沿用当前笔的背景色：vim 先 SGR 铺背景再擦行，擦除回落默认底色就会闪白。
    grid::Cell blank{};
    blank.background = pen_.background;
    blank.background_source = pen_.background_source;
    for (std::size_t column = begin; column < end; ++column) {
        row.set(column, blank);
    }
}

auto Terminal::erase_display(std::int32_t mode) -> void {
    switch (mode) {
        case 0:
            fill_blank(cursor_.row, cursor_.column, columns());
            for (std::size_t row = cursor_.row + 1; row <= region_bottom_; ++row) {
                fill_blank(row, 0, columns());
            }
            break;
        case 1:
            for (std::size_t row = region_top_; row < cursor_.row; ++row) {
                fill_blank(row, 0, columns());
            }
            fill_blank(cursor_.row, 0, std::min(cursor_.column + 1, columns()));
            break;
        case 2:
        case 3:
            for (std::size_t row = 0; row < rows(); ++row) {
                fill_blank(row, 0, columns());
            }
            if (mode == 3 && !modes_.alternate_screen) {
                main_.clear();  // xterm 口径：ED 3 连 scrollback 一起清（备屏无历史可清）
                full_screen_dirty_ = true;
            }
            break;
        default:
            break;
    }
}

auto Terminal::erase_line(std::int32_t mode) -> void {
    switch (mode) {
        case 0:
            fill_blank(cursor_.row, cursor_.column, columns());
            break;
        case 1:
            fill_blank(cursor_.row, 0, std::min(cursor_.column + 1, columns()));
            break;
        case 2:
            fill_blank(cursor_.row, 0, columns());
            break;
        default:
            break;
    }
}

auto Terminal::shift_cells(std::size_t count, bool insert) -> void {
    const auto cols = columns();
    if (count == 0U || cursor_.column >= cols) {
        return;
    }
    auto &row = buffer().visible_line(cursor_.row);
    const auto moved = std::min(count, cols - cursor_.column);
    if (insert) {
        // 保住能留下的最左 `cols - cursor - moved` 格，自右向左搬，腾出的**光标处**交给 fill_blank
        // （ICH 插的是光标处的空列，被推出行尾的内容直接丢失）。
        const auto movable = cols - cursor_.column - moved;
        for (std::size_t offset = movable; offset > 0; --offset) {
            const auto source = cursor_.column + offset - 1;
            row.set(source + moved, row.cell(source));
        }
        fill_blank(cursor_.row, cursor_.column, cursor_.column + moved);
    } else {
        for (std::size_t index = cursor_.column; index + moved < cols; ++index) {
            row.set(index, row.cell(index + moved));
        }
        // DCH 删掉光标处的格，行尾补出等量空格。
        fill_blank(cursor_.row, cols - moved, cols);
    }
}

auto Terminal::line_down() -> void {
    if (cursor_.row >= region_bottom_) {
        scroll_text(region_top_, region_bottom_, 1, true);
    } else if (cursor_.row + 1 < rows()) {
        ++cursor_.row;
    }
    pending_wrap_ = false;
}

auto Terminal::line_up() -> void {
    if (cursor_.row <= region_top_) {
        scroll_text(region_top_, region_bottom_, 1, false);
    } else if (cursor_.row > 0) {
        --cursor_.row;
    }
    pending_wrap_ = false;
}

auto Terminal::scroll_text(std::size_t top, std::size_t bottom, std::size_t count, bool up) -> void {
    if (count == 0U) {
        return;
    }
    if (top == 0 && bottom + 1 == buffer().visible_rows()) {
        // 整屏滚动才与 scrollback 相互作用；行级脏区表达不出「平移一屏」，改由整屏脏通知（§3.4）。
        up ? buffer().scroll_up(count) : buffer().scroll_down(count);
        full_screen_dirty_ = true;
        return;
    }
    up ? buffer().scroll_region_up(top, bottom, count) : buffer().scroll_region_down(top, bottom, count);
}

auto Terminal::move_to(std::size_t row, std::size_t column) -> void {
    cursor_.row = std::min(row, rows() - 1);
    cursor_.column = clamp_column(column);
    pending_wrap_ = false;
}

auto Terminal::clamp_column(std::size_t column) const noexcept -> std::size_t {
    return std::min(column, columns() - 1);
}

auto Terminal::set_tab_stops_default() noexcept -> void {
    for (std::size_t column = 0; column < tab_stops_.size(); ++column) {
        tab_stops_[column] = (column % kDefaultTabInterval) == 0U;
    }
}

auto Terminal::reset_to_default() -> void {
    main_.clear();
    alt_.clear();
    pen_ = grid::Cell{};
    modes_ = {};
    // RIS 回到的是本会话的初始档而不是库的硬缺省：一次 `reset` 不该静默抹掉用户配置（裁决 7.76）。
    modes_.cursor_shape = defaults_.cursor_shape;
    modes_.cursor_blinking = defaults_.cursor_blinking;
    designated_.fill(Charset::Ascii);
    cursor_ = {};
    saved_main_ = {};
    saved_alt_ = {};
    region_top_ = 0;
    region_bottom_ = main_.visible_rows() - 1;
    ambiguous_ = defaults_.ambiguous_width;
    osc_state_ = {};
    hyperlinks_.clear();
    next_hyperlink_id_ = 1;  // 网格已清空，标识可以从头分配
    current_hyperlink_ = grid::kNoHyperlink;
    clipboard_write_.reset();
    pending_wrap_ = false;
    parser_.reset();
    set_tab_stops_default();
    full_screen_dirty_ = true;
}

auto Terminal::resize(std::size_t columns, std::size_t rows) -> void {
    if (columns == 0 || rows == 0) {
        return;  // 无效尺寸：下发的只能是正数，收到 0 说明调用方算错了，改网格会越界
    }
    if (columns == main_.columns() && rows == main_.visible_rows()) {
        return;  // 去抖后仍可能收到同一尺寸，无变化就不该把 UI 副本整体判废
    }
    const bool width_changed = columns != main_.columns();
    main_.set_columns(columns);
    main_.set_rows(rows);
    alt_.set_columns(columns);
    alt_.set_rows(rows);
    if (width_changed) {
        // 制表位挂在列上：列数一变旧位置就失去意义，按默认间距整表重建。
        tab_stops_.assign(columns, false);
        set_tab_stops_default();
    }
    cursor_ = {std::min(cursor_.row, rows - 1), clamp_column(cursor_.column)};
    saved_main_ = {std::min(saved_main_.row, rows - 1), clamp_column(saved_main_.column)};
    saved_alt_ = {std::min(saved_alt_.row, rows - 1), clamp_column(saved_alt_.column)};
    region_top_ = 0;
    region_bottom_ = rows - 1;  // DECSTBM 的带随尺寸失效（xterm 同口径）
    pending_wrap_ = false;
    full_screen_dirty_ = true;
}

}  // namespace borealis::term
