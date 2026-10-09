#pragma once

// ============================================================
// 会话编码族（include/borealis/term/codec.h）
// ------------------------------------------------------------
// `SPEC.FEAT.TERM.09` 的双向口径（裁决 7.16）有两个转换点：读线程一侧「字节流 → 码点」，
// 发送一侧「文本 → 字节流」。UTF-8 档位沿用 `term/utf8.h` 的严格实现（过长编码、代理项、
// 越界值一律拒绝，非法序列按最大子部分替换），其余档位走 iconv——POSIX 腿取系统 libc、
// Windows 腿取 vcpkg libiconv，两腿同一份 `<iconv.h>` API 与同一行为，故本件属共享路径
// 而不进 `src/platform/`（AGENTS.md §4.5 第 23 条的射程是 PTY / 串口 / 传输 / shell 探测 /
// DPI 上报，这里两腿没有任何平台差异可抽象）。
//
// 「配置名 → iconv 名」的映射表在本件里，不依赖两实现各自的别名集合：实测 glibc 认
// `CP437` 而**不认** `Latin-1`，而 libiconv 的别名面与之不尽相同。映射不到的名字回落
// UTF-8 严格腿并在日志留痕——`terminal.encoding` 是自由文本键（装载侧只判类型，见判据文
// §4.1 与 `settings_catalog.cpp` 那条注），拼错的值不能让会话直接失明。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "borealis/term/utf8.h"

namespace borealis::term {

/// @brief 目标编码不可表示某码点时的发送处置（`SPEC.FEAT.TERM.09` 的「可配策略」三档）。
///
/// 三档的共同底线是「不得静默发送乱码字节」：每档都计数，计数进可观测面板
/// （`SPEC.NF.RELI.01`），并另由一次性提示告知（裁决 **7.104** 收口的 `SPEC.FEAT.TERM.09`
/// 提示腿，形态＝视口内非模态卡）。
enum class UnrepresentablePolicy : std::uint8_t {
    Replace,         ///< 缺省：写目标编码里替换字符的形态；该编码连替换字符都没有则写 `?`。
    DropWithNotice,  ///< 跳过该码点，一个字节都不发。
    PassThroughUtf8, ///< 原样发该码点的 UTF-8 字节，由对端自行处置。
};

/// @brief 会话编解码的构造期入参：装配层由配置折算一份，交进每一条会话。
///
/// 生效档位是「下一将会话」：解码器与发送策略都是会话构造期值，运行期改配置不重放既有会话
/// （判据文 §0 边界②，与 `appearance.cursor_shape` 三条同档）。
struct SessionEncoding {
    std::string name{"UTF-8"};  ///< 编码名，取值口径见 `SPECIFICATIONS.md` §4.1；缺省即本地/SSH 默认。
    UnrepresentablePolicy unrepresentable{
        UnrepresentablePolicy::Replace};  ///< 不可表示码点的处置档。
};

/// @brief 解码器接缝：字节流 → 码点流，与 `Utf8Decoder` 同形，好让会话不认识具体档位。
///
/// 会话按 `SessionEncoding::name` 取一份实现（裁决 7.16 的双向口径里只有解码方向需要跨
/// 分片保状态：读线程的块大小由对端与传输层决定，半个序列切在块边界是常态而非异常）。
class SessionDecoder {
  public:
    virtual ~SessionDecoder() = default;

    /// @brief 喂入一段字节。
    /// @param bytes 原始字节（可含任意非法值）。
    /// @param sink 码点接收端，按产出次序逐个回调。
    virtual auto feed(std::span<const std::byte> bytes, CodePointSink &sink) -> void = 0;

    /// @brief 流结束：把仍挂起的残缺序列按替换字符收尾。
    /// @param sink 码点接收端。
    virtual auto finish(CodePointSink &sink) -> void = 0;

    /// @brief 丢弃挂起序列与移位状态（编码切换时调用）；统计不随之清零。
    virtual auto reset() noexcept -> void = 0;

    /// @brief 累计诊断计数（非法字节序列数与码点总数）。
    [[nodiscard]] virtual auto stats() const noexcept -> const DecodeStats & = 0;
};

/// @brief 按编码名造解码器。
///
/// 名字映射不到、或该档位在本地 iconv 里不可用时，回落 UTF-8 严格腿并留 `AURORA_LOG_WARN`
/// 痕：会话宁可继续以 UTF-8 呈现（与配置键落地前的行为逐位相同），也不产出一屏豆腐块。
/// @param encoding 配置里的编码名。
/// @return 非空解码器，其状态从零开始。
[[nodiscard]] auto make_session_decoder(std::string_view encoding)
    -> std::unique_ptr<SessionDecoder>;

/// @brief 把配置里的编码名折成**实际生效腿**的名字（裁决 7.104 的 D6②）。
///
/// 与 `make_session_decoder` 同一张别名表，故「名字」与「真正跑的解码器」不会分叉；映射
/// 不到的名字回 `"UTF-8"`（那条腿实际在跑的就是 UTF-8）。发送侧一次性提示报的是这个名字而不是
/// 配置原样串：填 `GB2312` 的会话若显示 `GB2312`，用户按名字去改档就改了个不相干的字段。
/// 本地档位不可用（iconv 句柄开不出来）的那条回落不在本函数射程内——它要开句柄才有读数，
/// 而本函数是纯表查询、逐次调用零成本。
/// @param encoding 配置里的编码名（ASCII 大小写不敏感，口径同 `make_session_decoder`）。
/// @return 六条腿之一的展示名；映射不到为 `"UTF-8"`。
[[nodiscard]] auto resolve_encoding_name(std::string_view encoding) -> std::string_view;

/// @brief 发送方向的产物。
struct EncodeResult {
    std::string bytes;             ///< 已按目标编码成形的字节。
    std::size_t unrepresentable{}; ///< 被策略处置掉的码点数；0 表示全部可表示。
};

/// @brief 把码点流按目标编码成字节（`SPEC.FEAT.TERM.09` 的发送方向）。
///
/// 线程安全由「每次调用自开自闭转换句柄」保证：发送侧的调用方是主线程（键入、粘贴、输入法
/// commit）与读线程（查询应答）两处，二者可并发。实测一次句柄开合 ~160 ns，相对键入延迟
/// 预算可忽略，故不为此在会话里存一份需要加锁的状态。
/// @param text 码点流。
/// @param encoding 配置里的编码名（回落口径同 `make_session_decoder`）。
/// @param policy 不可表示码点的处置档。
/// @return 字节流与被处置的码点数。
[[nodiscard]] auto encode_for_encoding(std::u32string_view text, std::string_view encoding,
                                       UnrepresentablePolicy policy) -> EncodeResult;

}  // namespace borealis::term
