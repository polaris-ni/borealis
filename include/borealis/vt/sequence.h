#pragma once

// ============================================================
// VT 序列的结构化表示（include/borealis/vt/sequence.h）
// ------------------------------------------------------------
// 解析器只把码点流切成结构化单元，**不赋予语义**——语义解释归终端状态机
// （`codespec/ARCHITECTURE.md` §5.1）。因此本文件只有「结构」没有「行为」。
// ============================================================

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace borealis::vt {

/// @brief 解析器输出的语义单元种类。
enum class SequenceKind : std::uint8_t {
    Print,     ///< 可打印字符（含空格与已解码的 CJK 码点）
    Execute,   ///< C0 / C1 控制码，立即执行
    Escape,    ///< ESC 序列：可选中间字节 + 终结符
    Csi,       ///< CSI 序列：参数 + 可选中间字节（含私有前缀）+ 终结符
    Osc,       ///< OSC 字符串：负载为原始字符串，命令号未拆分
    DcsHook,   ///< DCS 起始：参数 + 中间字节 + 终结符已就绪，其后跟随负载
    DcsPut,    ///< DCS 负载片段
    DcsUnhook, ///< DCS 结束
    Ignored,   ///< 未识别 / 被取消 / 超限的序列，仅用于诊断计数
};

/// @brief CSI 与 DCS 的一个参数。
///
/// `;` 分隔出一级参数，`:` 分隔出子参数（SGR 真彩色 `38:2::r:g:b` 一类）。
/// 子参数表为空即「缺省」，按 xterm 惯例取调用方给定的默认值。
struct Param {
    std::vector<std::int32_t> sub;

    /// @brief 取子参数表的首个值；缺省时返回 @p fallback。
    /// @param fallback 参数缺省时的取值。
    /// @return 首个子参数值或 @p fallback。
    [[nodiscard]] auto value_or(std::int32_t fallback) const noexcept -> std::int32_t {
        return sub.empty() ? fallback : sub.front();
    }

    /// @brief 该参数是否为缺省（未给出任何数字）。
    [[nodiscard]] auto is_default() const noexcept -> bool { return sub.empty(); }
};

/// @brief 按位取参数值，越界按缺省处理。
/// @param params 参数表。
/// @param index 参数序号（从 0 起）。
/// @param fallback 该参数缺省或越界时的取值。
/// @return 解析出的参数值。
[[nodiscard]] inline auto param_or(std::span<const Param> params, std::size_t index,
                                   std::int32_t fallback) noexcept -> std::int32_t {
    return index < params.size() ? params[index].value_or(fallback) : fallback;
}

/// @brief 解析器产出的一个语义单元。
///
/// ⚠️ `params` / `intermediates` / `data` 均为指向解析器内部缓冲的视图，
/// **仅在本轮回调期间有效**；需跨回调留存者自行拷贝。
struct Sequence {
    SequenceKind kind = SequenceKind::Print;
    char32_t code_point = 0;                       ///< Print / Execute 的码点；Ignored 时为触发该结果的码点
    std::span<const Param> params;                 ///< CSI / DCS 参数表
    std::u32string_view intermediates;             ///< 中间字节，含 CSI 私有前缀（`?` `<` `=` `>`）
    std::u32string_view data;                      ///< OSC 负载或 DCS 负载片段
    char32_t final_byte = 0;                       ///< ESC / CSI / DCS 的终结符
};

}  // namespace borealis::vt
