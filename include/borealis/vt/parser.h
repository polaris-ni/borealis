#pragma once

// ============================================================
// VT 解析器（include/borealis/vt/parser.h）
// ------------------------------------------------------------
// 自研表驱动状态机（Paul Williams 经典划分的扩展），输入为**已解码的码点流**
// （解码在解析之前，见 `codespec/ARCHITECTURE.md` §3.3），输出为结构化语义单元。
//
// 解析器实例仅由会话读线程持有、置于网格锁外（§3.4），不触达 UI、不做 IO。
// ============================================================

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "borealis/vt/sequence.h"

namespace borealis::vt {

/// @brief 状态机状态（Williams 划分 + 字符串态的 ESC 子态 + APC 族串）。
enum class State : std::uint8_t {
    Ground,
    Escape,
    EscapeIntermediate,
    CsiEntry,
    CsiParam,
    CsiIntermediate,
    CsiIgnore,
    OscString,
    OscStringEscape,
    ApcString,
    ApcStringEscape,
    DcsEntry,
    DcsParam,
    DcsIntermediate,
    DcsIgnore,
    DcsPassthrough,
    DcsPassthroughEscape,
};

/// @brief 解析期诊断计数，供可观测面板消费（`SPEC.NF.RELI.01`）。
struct ParseStats {
    std::uint64_t ignored = 0;   ///< 未识别 / 非法 / 超限被丢弃的序列数
    std::uint64_t cancelled = 0; ///< 被 CAN(0x18) / SUB(0x1A) 打断的序列数
};

/// @brief 语义单元的接收端。
class SequenceSink {
  public:
    virtual ~SequenceSink() = default;

    /// @brief 每解析出一个语义单元回调一次。
    /// @param seq 本轮单元；其内视图仅在本回调期间有效。
    virtual auto on_sequence(const Sequence &seq) -> void = 0;
};

/// @brief 表驱动 VT 解析器：把码点流切成语义单元。
///
/// 未识别序列不中断解析——按「降级而非中止」口径产出 `SequenceKind::Ignored`
/// 并计数（§5.5），解析状态始终可继续消费后续输入。
class Parser {
  public:
    /// @brief 喂入一段已解码的码点流。
    /// @param text 码点流（跨 feed 的序列状态由解析器持有，故可按读缓冲任意切分）。
    /// @param sink 语义单元接收端。
    auto feed(std::u32string_view text, SequenceSink &sink) -> void;

    /// @brief 丢弃进行中的序列并回到 Ground（会话重设、编码切换时调用）。
    auto reset() noexcept -> void;

    /// @brief 当前状态，供诊断与测试断言。
    [[nodiscard]] auto state() const noexcept -> State { return state_; }

    /// @brief 累计诊断计数。
    [[nodiscard]] auto stats() const noexcept -> const ParseStats & { return stats_; }

  private:
    auto clear_sequence() noexcept -> void;
    auto collect(char32_t c) -> void;
    auto add_param(char32_t c) -> void;
    auto append_data(char32_t c) -> void;
    auto flush_put(SequenceSink &sink) -> void;
    auto emit(SequenceKind kind, char32_t c, SequenceSink &sink) -> void;
    auto emit_string(SequenceKind kind, SequenceSink &sink) -> void;
    auto cancel(char32_t c, SequenceSink &sink) -> void;

    State state_ = State::Ground;
    ParseStats stats_{};

    std::vector<Param> params_;
    std::u32string intermediates_;
    std::u32string data_;
    bool overflowed_ = false; ///< 字符串负载超出上限，本序列须整体丢弃
};

}  // namespace borealis::vt
