// ============================================================
// VT 解析器实现（src/vt/parser.cpp）
// ------------------------------------------------------------
// 表驱动状态机：状态 × 码点类别 →（动作, 下一状态）。状态划分沿用 Paul Williams
// 的经典形态，并补两个自研子态：字符串态里的 ESC 子态（识别 ST 的 `ESC \` 两字节
// 形式）与 APC/SOS/PM 串（内容一律丢弃，仅保持同步）。
//
// 输入是**已解码的码点**，故 UTF-8 / GB18030 的差异在解码层消化（§3.3），
// 解析器对 CJK 码点一视同仁地按可打印字符处理。
// ============================================================

#include "borealis/vt/parser.h"

#include <cstddef>

namespace borealis::vt {
namespace {

/// @brief 码点类别：转移表的列。
enum class ByteClass : std::uint8_t {
    kC0,
    kBell,
    kCancel,
    kEsc,
    kDel,
    kIntermediate,
    kParamByte,
    kPrivatePrefix,
    kFinalByte,
    kBackslash,
    kLBracket,
    kRBracket,
    kDcsStart,
    kApcStart,
    kCsi8,
    kDcs8,
    kOsc8,
    kSt8,
    kC1Exec,
    kPrint,
};

/// @brief 转移动作。
enum class Action : std::uint8_t {
    kNone,
    kClear,
    kCollect,
    kParam,
    kPrint,
    kExecute,
    kEscDispatch,
    kCsiDispatch,
    kOscPut,
    kOscDispatch,
    kDcsHook,
    kDcsPut,
    kDcsUnhook,
    kIgnore,
    kCancel,
};

struct Transition {
    Action action;
    State next;
};

using enum Action;
using enum State;

constexpr std::size_t kStateCount = 17;
constexpr std::size_t kClassCount = 20;

// clang-format off
constexpr Transition kTransitions[kStateCount][kClassCount] = {
    // Ground：可打印字符（含已解码的 CJK）一律 print；C0/C1 立即执行；ESC 与 8-bit 起始符进入对应收集态。
    {
        {kExecute, Ground}, {kExecute, Ground}, {kNone, Ground}, {kClear, Escape},
        {kNone, Ground}, {kPrint, Ground}, {kPrint, Ground}, {kPrint, Ground},
        {kPrint, Ground}, {kPrint, Ground}, {kPrint, Ground}, {kPrint, Ground},
        {kPrint, Ground}, {kPrint, Ground}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kNone, Ground}, {kExecute, Ground}, {kPrint, Ground},
    },
    // Escape：中间字节继续收集；`[` `]` `P` 与 APC 族转入各自串态；其余终结符收尾。
    {
        {kExecute, Escape}, {kExecute, Escape}, {kCancel, Ground}, {kClear, Escape},
        {kNone, Escape}, {kCollect, EscapeIntermediate}, {kEscDispatch, Ground}, {kEscDispatch, Ground},
        {kEscDispatch, Ground}, {kEscDispatch, Ground}, {kClear, CsiEntry}, {kClear, OscString},
        {kClear, DcsEntry}, {kClear, ApcString}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kNone, Ground}, {kExecute, Escape}, {kEscDispatch, Ground},
    },
    // EscapeIntermediate：再收中间字节，遇终结符收尾；字符集指派（`ESC ( 0`）即此路径。
    {
        {kExecute, EscapeIntermediate}, {kExecute, EscapeIntermediate}, {kCancel, Ground}, {kClear, Escape},
        {kNone, EscapeIntermediate}, {kCollect, EscapeIntermediate}, {kEscDispatch, Ground}, {kEscDispatch, Ground},
        {kEscDispatch, Ground}, {kEscDispatch, Ground}, {kEscDispatch, Ground}, {kEscDispatch, Ground},
        {kEscDispatch, Ground}, {kEscDispatch, Ground}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kNone, Ground}, {kExecute, EscapeIntermediate}, {kEscDispatch, Ground},
    },
    // CsiEntry：参数字节起参数、私有前缀收进 intermediates、终结符收尾。
    {
        {kExecute, CsiEntry}, {kExecute, CsiEntry}, {kCancel, Ground}, {kClear, Escape},
        {kNone, CsiEntry}, {kCollect, CsiIntermediate}, {kParam, CsiParam}, {kCollect, CsiParam},
        {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kCsiDispatch, Ground},
        {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kIgnore, Ground}, {kExecute, CsiEntry}, {kIgnore, CsiIgnore},
    },
    // CsiParam：累积参数与子参数；参数之后再出现私有前缀属非法，转忽略态。
    {
        {kExecute, CsiParam}, {kExecute, CsiParam}, {kCancel, Ground}, {kClear, Escape},
        {kNone, CsiParam}, {kCollect, CsiIntermediate}, {kParam, CsiParam}, {kIgnore, CsiIgnore},
        {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kCsiDispatch, Ground},
        {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kIgnore, Ground}, {kExecute, CsiParam}, {kIgnore, CsiIgnore},
    },
    // CsiIntermediate：已收中间字节后不能再出参数，非法即吞到终结符为止。
    {
        {kExecute, CsiIntermediate}, {kExecute, CsiIntermediate}, {kCancel, Ground}, {kClear, Escape},
        {kNone, CsiIntermediate}, {kCollect, CsiIntermediate}, {kIgnore, CsiIgnore}, {kIgnore, CsiIgnore},
        {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kCsiDispatch, Ground},
        {kCsiDispatch, Ground}, {kCsiDispatch, Ground}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kIgnore, Ground}, {kExecute, CsiIntermediate}, {kIgnore, CsiIgnore},
    },
    // CsiIgnore：吞掉本序列直到终结符，产出 Ignored 而非中断解析（§5.5 降级口径）。
    {
        {kExecute, CsiIgnore}, {kExecute, CsiIgnore}, {kCancel, Ground}, {kClear, Escape},
        {kNone, CsiIgnore}, {kNone, CsiIgnore}, {kNone, CsiIgnore}, {kNone, CsiIgnore},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kExecute, CsiIgnore}, {kNone, CsiIgnore},
    },
    // OscString：BEL 或 ST 收尾；ESC 转入子态以识别两字节 ST。
    {
        {kNone, OscString}, {kOscDispatch, Ground}, {kCancel, Ground}, {kNone, OscStringEscape},
        {kNone, OscString}, {kOscPut, OscString}, {kOscPut, OscString}, {kOscPut, OscString},
        {kOscPut, OscString}, {kOscPut, OscString}, {kOscPut, OscString}, {kOscPut, OscString},
        {kOscPut, OscString}, {kOscPut, OscString}, {kNone, OscString}, {kNone, OscString},
        {kNone, OscString}, {kOscDispatch, Ground}, {kNone, OscString}, {kOscPut, OscString},
    },
    // OscStringEscape：仅 `\` 构成 ST，其余视为损坏 → 整串丢弃。
    {
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kOscDispatch, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
    },
    // ApcString：SOS/PM/APC 内容一律丢弃，仅保持同步。
    {
        {kNone, ApcString}, {kIgnore, Ground}, {kCancel, Ground}, {kNone, ApcStringEscape},
        {kNone, ApcString}, {kNone, ApcString}, {kNone, ApcString}, {kNone, ApcString},
        {kNone, ApcString}, {kNone, ApcString}, {kNone, ApcString}, {kNone, ApcString},
        {kNone, ApcString}, {kNone, ApcString}, {kNone, ApcString}, {kNone, ApcString},
        {kNone, ApcString}, {kIgnore, Ground}, {kNone, ApcString}, {kNone, ApcString},
    },
    // ApcStringEscape：同 OSC 子态，只认 ST。
    {
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
    },
    // DcsEntry：与 CSI 同构，终结符后转入 passthrough 而非回 Ground。
    {
        {kExecute, DcsEntry}, {kExecute, DcsEntry}, {kCancel, Ground}, {kClear, Escape},
        {kNone, DcsEntry}, {kCollect, DcsIntermediate}, {kParam, DcsParam}, {kCollect, DcsParam},
        {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough},
        {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kIgnore, Ground}, {kExecute, DcsEntry}, {kIgnore, DcsIgnore},
    },
    // DcsParam：累积参数；非法前缀转忽略态。
    {
        {kExecute, DcsParam}, {kExecute, DcsParam}, {kCancel, Ground}, {kClear, Escape},
        {kNone, DcsParam}, {kCollect, DcsIntermediate}, {kParam, DcsParam}, {kIgnore, DcsIgnore},
        {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough},
        {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kIgnore, Ground}, {kExecute, DcsParam}, {kIgnore, DcsIgnore},
    },
    // DcsIntermediate：中间字节之后不再收参数。
    {
        {kExecute, DcsIntermediate}, {kExecute, DcsIntermediate}, {kCancel, Ground}, {kClear, Escape},
        {kNone, DcsIntermediate}, {kCollect, DcsIntermediate}, {kIgnore, DcsIgnore}, {kIgnore, DcsIgnore},
        {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough},
        {kDcsHook, DcsPassthrough}, {kDcsHook, DcsPassthrough}, {kClear, CsiEntry}, {kClear, DcsEntry},
        {kClear, OscString}, {kIgnore, Ground}, {kExecute, DcsIntermediate}, {kIgnore, DcsIgnore},
    },
    // DcsIgnore：吞到终结符后直接回 Ground（不再进 passthrough）。
    {
        {kExecute, DcsIgnore}, {kExecute, DcsIgnore}, {kCancel, Ground}, {kClear, Escape},
        {kNone, DcsIgnore}, {kNone, DcsIgnore}, {kNone, DcsIgnore}, {kNone, DcsIgnore},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kExecute, DcsIgnore}, {kNone, DcsIgnore},
    },
    // DcsPassthrough：负载原样透传，仅 ST 收尾（BEL 不终止 DCS）。
    {
        {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough}, {kCancel, Ground}, {kNone, DcsPassthroughEscape},
        {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough},
        {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough},
        {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough},
        {kDcsPut, DcsPassthrough}, {kDcsUnhook, Ground}, {kDcsPut, DcsPassthrough}, {kDcsPut, DcsPassthrough},
    },
    // DcsPassthroughEscape：仅 `\` 构成 ST。
    {
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kDcsUnhook, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
        {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground}, {kIgnore, Ground},
    },
};
// clang-format on

/// @brief 参数上限：与 xterm 一致的钳制口径，避免超长数字串溢出。
constexpr std::int32_t kMaxParamValue = 65535;
/// @brief 中间字节上限：超过即丢弃后续中间字节，序列仍会被终结符结束。
constexpr std::size_t kMaxIntermediate = 4;
/// @brief 字符串负载上限：无终结符的 OSC/DCS 不能无界增长（损坏输出会真实发生）。
constexpr std::size_t kMaxStringLength = 1U << 20;
/// @brief DCS 负载的交付块大小：满块即交付一次，避免逐字符回调。
constexpr std::size_t kDcsPutBlockSize = 4096;

/// @brief 码点归类。
/// @param c 已解码的码点。
/// @return 该码点所属的转移表列。
auto classify(char32_t c) noexcept -> ByteClass {
    if (c <= 0x1FU) {
        if (c == 0x1BU) {
            return ByteClass::kEsc;
        }
        if (c == 0x18U || c == 0x1AU) {
            return ByteClass::kCancel;
        }
        if (c == 0x07U) {
            return ByteClass::kBell;
        }
        return ByteClass::kC0;
    }
    if (c <= 0x7FU) {
        switch (c) {
            case U'[':
                return ByteClass::kLBracket;
            case U']':
                return ByteClass::kRBracket;
            case U'P':
                return ByteClass::kDcsStart;
            case U'X':
            case U'^':
            case U'_':
                return ByteClass::kApcStart;
            case U'\\':
                return ByteClass::kBackslash;
            case 0x7FU:
                return ByteClass::kDel;
            default:
                break;
        }
        if (c <= 0x2FU) {
            return ByteClass::kIntermediate;
        }
        if (c <= 0x3BU) {
            return ByteClass::kParamByte;
        }
        if (c <= 0x3FU) {
            return ByteClass::kPrivatePrefix;
        }
        return ByteClass::kFinalByte;
    }
    if (c <= 0x9FU) {
        switch (c) {
            case 0x9BU:
                return ByteClass::kCsi8;
            case 0x90U:
                return ByteClass::kDcs8;
            case 0x9DU:
                return ByteClass::kOsc8;
            case 0x9CU:
                return ByteClass::kSt8;
            default:
                return ByteClass::kC1Exec;
        }
    }
    return ByteClass::kPrint;
}

/// @brief 状态在转移表中的行号。
/// @param s 当前状态。
/// @return 行下标。
constexpr auto row(State s) noexcept -> std::size_t { return static_cast<std::size_t>(s); }

}  // namespace

auto Parser::feed(std::u32string_view text, SequenceSink &sink) -> void {
    for (const char32_t c : text) {
        const auto column = static_cast<std::size_t>(classify(c));
        const Transition t = kTransitions[row(state_)][column];
        switch (t.action) {
            case Action::kNone:
                break;
            case Action::kClear:
                clear_sequence();
                break;
            case Action::kCollect:
                collect(c);
                break;
            case Action::kParam:
                add_param(c);
                break;
            case Action::kPrint:
                emit(SequenceKind::Print, c, sink);
                break;
            case Action::kExecute:
                emit(SequenceKind::Execute, c, sink);
                break;
            case Action::kEscDispatch:
                emit(SequenceKind::Escape, c, sink);
                break;
            case Action::kCsiDispatch:
                emit(SequenceKind::Csi, c, sink);
                break;
            case Action::kOscPut:
                append_data(c);
                break;
            case Action::kOscDispatch:
                emit_string(SequenceKind::Osc, sink);
                break;
            case Action::kDcsHook:
                data_.clear();
                overflowed_ = false;
                emit(SequenceKind::DcsHook, c, sink);
                break;
            case Action::kDcsPut:
                append_data(c);
                if (data_.size() >= kDcsPutBlockSize) {
                    flush_put(sink);
                }
                break;
            case Action::kDcsUnhook:
                // 负载按块交付：大块 DCS（sixel 一类）不必逐字符回调，收尾前把余量补齐。
                flush_put(sink);
                emit_string(SequenceKind::DcsUnhook, sink);
                break;
            case Action::kIgnore:
                ++stats_.ignored;
                emit(SequenceKind::Ignored, c, sink);
                break;
            case Action::kCancel:
                cancel(c, sink);
                break;
        }
        state_ = t.next;
    }
}

auto Parser::reset() noexcept -> void {
    clear_sequence();
    state_ = State::Ground;
}

auto Parser::clear_sequence() noexcept -> void {
    params_.clear();
    intermediates_.clear();
    data_.clear();
    overflowed_ = false;
}

auto Parser::collect(char32_t c) -> void {
    if (intermediates_.size() < kMaxIntermediate) {
        intermediates_ += c;
    }
}

auto Parser::add_param(char32_t c) -> void {
    if (c == U';') {
        // 前导分号（`CSI ;31m`）意味着第一个参数缺省，故先补一个空参数再起新的。
        if (params_.empty()) {
            params_.emplace_back();
        }
        params_.emplace_back();
        return;
    }
    if (params_.empty()) {
        params_.emplace_back();
    }
    auto &param = params_.back();
    if (c == U':') {
        param.sub.emplace_back(0);
        return;
    }
    if (param.sub.empty()) {
        param.sub.emplace_back(0);
    }
    auto &value = param.sub.back();
    value = value <= (kMaxParamValue - 9) / 10 ? value * 10 + static_cast<std::int32_t>(c - U'0') : kMaxParamValue;
}

auto Parser::append_data(char32_t c) -> void {
    if (data_.size() >= kMaxStringLength) {
        overflowed_ = true;
        return;
    }
    data_ += c;
}

auto Parser::flush_put(SequenceSink &sink) -> void {
    if (data_.empty()) {
        return;
    }
    emit(SequenceKind::DcsPut, 0, sink);
    data_.clear();
}

auto Parser::emit(SequenceKind kind, char32_t c, SequenceSink &sink) -> void {
    const bool carries_structure = kind == SequenceKind::Escape || kind == SequenceKind::Csi ||
                                   kind == SequenceKind::DcsHook;
    const bool carries_data = kind == SequenceKind::DcsPut;
    const Sequence seq{
        .kind = kind,
        // 结构化序列的终结符已由 final_byte 承载，code_point 留 0 避免同一信息两份表示。
        .code_point = carries_structure ? 0 : c,
        .params = carries_structure ? std::span<const Param>{params_.data(), params_.size()}
                                    : std::span<const Param>{},
        .intermediates = carries_structure ? std::u32string_view{intermediates_} : std::u32string_view{},
        .data = carries_data ? std::u32string_view{data_} : std::u32string_view{},
        .final_byte = carries_structure ? c : 0,
    };
    sink.on_sequence(seq);
}

auto Parser::emit_string(SequenceKind kind, SequenceSink &sink) -> void {
    if (overflowed_) {
        ++stats_.ignored;
        const Sequence seq{.kind = SequenceKind::Ignored, .code_point = 0};
        sink.on_sequence(seq);
        return;
    }
    const Sequence seq{.kind = kind, .code_point = 0, .data = data_};
    sink.on_sequence(seq);
}

auto Parser::cancel(char32_t c, SequenceSink &sink) -> void {
    if (state_ == State::Ground) {
        return;
    }
    ++stats_.cancelled;
    const Sequence seq{.kind = SequenceKind::Ignored, .code_point = c};
    sink.on_sequence(seq);
}

}  // namespace borealis::vt
