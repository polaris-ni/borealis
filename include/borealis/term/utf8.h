#pragma once

// ============================================================
// UTF-8 双向编解码（include/borealis/term/utf8.h）
// ------------------------------------------------------------
// 会话编码族的第一员：架构 §6.1 把 UTF-8 定为本地终端与 SSH 的默认编码，
// §3.3 把「解码」放在 VT 解析之前，故本模块只做字节流与码点流的互相转换，
// 不解释任何终端语义。
//
// 解码口径（架构 §6.2）：非法字节序列按替换字符处理，**不中断解析、不污染后续**，
// 并计数进可观测面板（SPEC.NF.RELI.01）。非法判定用严格 UTF-8：拒绝过长编码、
// 代理项（U+D800–DFFF）与超出 U+10FFFF 的值，与 Unicode 推荐实践一致——宽松解析会让
// 损坏输出变成静默错字，而终端是「看输出排障」的工具，宁可显式替换。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace borealis::term {

/// @brief 替换字符：非法字节序列与不可表示码点的统一去向。
inline constexpr char32_t kReplacementCharacter = U'\xFFFD';

/// @brief 解码期诊断计数，供可观测面板消费（`SPEC.NF.RELI.01`）。
struct DecodeStats {
    std::uint64_t replaced = 0;   ///< 被替换字符取代的非法字节序列数
    std::uint64_t code_points = 0; ///< 产出的码点总数（含替换字符）
};

/// @brief 码点接收端。
class CodePointSink {
  public:
    virtual ~CodePointSink() = default;

    /// @brief 每解码出一个码点回调一次。
    /// @param cp 码点；非法序列处为 `kReplacementCharacter`。
    virtual auto on_code_point(char32_t cp) -> void = 0;
};

/// @brief UTF-8 解码器：字节流 → 码点流，状态可跨 `feed` 分片保持。
///
/// 会话读线程按任意块大小喂入都可得到同一结果；块边界切断多字节序列时不产出中间态，
/// 待续字节到齐后才产出码点。
class Utf8Decoder {
  public:
    /// @brief 喂入一段字节。
    /// @param bytes 原始字节（可含任意非法值）。
    /// @param sink 码点接收端。
    auto feed(std::span<const std::byte> bytes, CodePointSink &sink) -> void;

    /// @brief 流结束：把仍挂起的不完整序列按替换字符收尾（会话关闭、编码切换时调用）。
    /// @param sink 码点接收端。
    auto finish(CodePointSink &sink) -> void;

    /// @brief 丢弃挂起序列与统计之外的状态（编码切换时调用）。
    auto reset() noexcept -> void;

    /// @brief 是否正挂着不完整序列（供诊断：长时间挂起意味着对端输出异常）。
    [[nodiscard]] auto has_pending() const noexcept -> bool { return pending_ != 0; }

    /// @brief 累计诊断计数。
    [[nodiscard]] auto stats() const noexcept -> const DecodeStats & { return stats_; }

  private:
    /// @brief 判定并开启一个起始字节；非法起始（孤立续字节、过长起始、越界起始）产出替换字符。
    /// @param b 起始字节。
    /// @param sink 码点接收端。
    auto begin_sequence(std::uint8_t b, CodePointSink &sink) -> void;

    /// @brief 产出替换字符并清空挂起序列。
    /// @param sink 码点接收端。
    auto emit_replacement(CodePointSink &sink) -> void;

    DecodeStats stats_{};
    std::uint32_t pending_ = 0;   ///< 还需的续字节数
    std::uint32_t code_point_ = 0; ///< 累积中的码点值
    std::uint8_t second_min_ = 0x80; ///< 第二字节下界（挡过长编码）
    std::uint8_t second_max_ = 0xBF; ///< 第二字节上界（挡代理项与越界）
    bool first_continuation_ = false; ///< 下一个续字节是否为第二字节（受上下界约束）
};

/// @brief 把一个码点编码为 UTF-8 追加到 @p out。
/// @param cp 待编码码点。
/// @param out 字节输出，追加写入。
/// @return 是否被替换：代理项与超出 U+10FFFF 的值不可表示，写替换字符并返回 true。
[[nodiscard]] auto append_utf8(char32_t cp, std::string &out) -> bool;

/// @brief 把整段文本编码为 UTF-8。
/// @param text 码点流。
/// @param out 字节输出，追加写入。
/// @return 被替换的码点数（0 表示全部可表示）。
[[nodiscard]] auto encode_utf8(std::u32string_view text, std::string &out) -> std::size_t;

}  // namespace borealis::term
