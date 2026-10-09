// ============================================================
// 会话编码族实现（src/term/codec.cpp）
// ------------------------------------------------------------
// 解码的增量口径有一句不能含糊：块边界上的半个序列到底留在哪一侧，iconv 的实现之间并不一致
// （glibc 把残缺尾字节吃进句柄内部状态并回报 EINVAL，另有实现原样退还）。本件两条都兜住——
// 退还的尾巴由本件自存并在下一轮前置，被句柄吃掉的用 `hanging_` 位记住，收尾时哪一侧有货
// 就补一个替换字符。
//
// 非法序列的粒度与 `term/utf8.h` 同一条最大子部分口径：iconv 停在残缺起始处**不消耗它**，
// 本件为它补一个替换字符后手动推进一字节再继续。少推进一步就活锁，多推一步就吞掉后续内容。
//
// 发送方向有两条路：整段一次转换的快路，和逐码点转换的慢路。慢路只在文中确实有不可表示码点
// 时才走（快路一旦失败就整段重来），于是常见的「全部可表示」批次——尤其是粘贴大块文本——
// 不受逐码点往返的代价（AGENTS.md §4.5 第 25 条：帧边界上不做长计算）。
// ============================================================

#include "borealis/term/codec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include <iconv.h>

#include "aurora/core/log.h"

namespace borealis::term {
namespace {

/// @brief 输出分块：与解码侧一次喂入的块量级无关，只是攒够一批再交给严格 UTF-8 腿。
constexpr std::size_t kOutputChunk = 4096;

/// @brief 配置名 → iconv 名的映射。
///
/// 刻意不给 iconv 的别名集合兜底：实测 glibc 认 `CP437` 而**不认** `Latin-1`，libiconv 的
/// 别名面与之又不尽相同，故本表取两边都认的名字（`BIG5` / `IBM437` / `ISO-8859-1`）。
/// Windows 腿这几个名字的可用性尚未实测（沙箱无 MSVC），届时以构建与运行复验出账。
struct EncodingAlias {
    std::string_view config_name;
    std::string_view iconv_name;
};

constexpr std::array<EncodingAlias, 6> kAliases{
    EncodingAlias{"UTF-8", "UTF-8"},
    EncodingAlias{"GB18030", "GB18030"},
    EncodingAlias{"GBK", "GBK"},
    EncodingAlias{"Big5", "BIG5"},
    EncodingAlias{"Latin-1", "ISO-8859-1"},
    EncodingAlias{"CP437", "IBM437"},
};

/// @brief 编码名逐位相等比较（ASCII 大小写不敏感：`utf-8` 与 `UTF-8` 同一档位）。
[[nodiscard]] auto same_name(std::string_view left, std::string_view right) -> bool {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

/// @brief 把配置名折成 iconv 名；映射不到回空。
[[nodiscard]] auto iconv_name_for(std::string_view encoding) -> std::optional<std::string_view> {
    for (const auto &alias : kAliases) {
        if (same_name(encoding, alias.config_name)) {
            return alias.iconv_name;
        }
    }
    return std::nullopt;
}

/// @brief 转换句柄的 RAII 壳。
///
/// 发送侧的句柄在栈上开合；解码侧的一份要交给解码器长期持有，故可移动——移动后源侧置为
/// 无效值，`valid()` 自然为假，不会二次 `iconv_close`。
class IconvHandle {
  public:
    IconvHandle(std::string_view to, std::string_view from)
        : cd_{::iconv_open(std::string{to}.c_str(), std::string{from}.c_str())} {}

    IconvHandle(const IconvHandle &other) = delete;
    auto operator=(const IconvHandle &other) -> IconvHandle & = delete;

    IconvHandle(IconvHandle &&other) noexcept : cd_{other.cd_} { other.invalidate(); }

    auto operator=(IconvHandle &&other) noexcept -> IconvHandle & {
        if (this != &other) {
            close();
            cd_ = other.cd_;
            other.invalidate();
        }
        return *this;
    }

    ~IconvHandle() { close(); }

    [[nodiscard]] auto valid() const noexcept -> bool {
        return cd_ != reinterpret_cast<iconv_t>(-1);
    }

    /// @brief 裸句柄：只在 `iconv()` 调用点使用。
    [[nodiscard]] auto get() const noexcept -> iconv_t { return cd_; }

  private:
    void close() noexcept {
        if (valid()) {
            ::iconv_close(cd_);
        }
        invalidate();
    }

    void invalidate() noexcept { cd_ = reinterpret_cast<iconv_t>(-1); }

    iconv_t cd_;
};

/// @brief 复位句柄内部状态（POSIX：全 NULL 调用即复位，顺带冲刷移位状态）。
auto reset_state(iconv_t cd) noexcept -> void {
    static_cast<void>(::iconv(cd, nullptr, nullptr, nullptr, nullptr));
}

/// @brief UTF-8 档位的解码器：转调既有严格腿，只换一层接缝。
class Utf8LegDecoder final : public SessionDecoder {
  public:
    auto feed(std::span<const std::byte> bytes, CodePointSink &sink) -> void override {
        utf8_.feed(bytes, sink);
    }

    auto finish(CodePointSink &sink) -> void override { utf8_.finish(sink); }

    auto reset() noexcept -> void override { utf8_.reset(); }

    [[nodiscard]] auto stats() const noexcept -> const DecodeStats & override {
        return utf8_.stats();
    }

  private:
    Utf8Decoder utf8_;
};

/// @brief 非 UTF-8 档位的解码器：iconv 出 UTF-8，末段仍走严格腿取码点。
///
/// 末段保留严格腿不是冗余：iconv 的产物按构造就是合法 UTF-8，故那条腿的替换分支在本路径上
/// 不会触发，而码点解析（含跨块切分四字节序列）只有一处实现。
class IconvDecoder final : public SessionDecoder {
  public:
    explicit IconvDecoder(IconvHandle cd) : cd_{std::move(cd)} {}

    IconvDecoder(const IconvDecoder &other) = delete;
    auto operator=(const IconvDecoder &other) -> IconvDecoder & = delete;
    IconvDecoder(IconvDecoder &&other) = delete;
    auto operator=(IconvDecoder &&other) -> IconvDecoder & = delete;

    auto feed(std::span<const std::byte> bytes, CodePointSink &sink) -> void override;

    auto finish(CodePointSink &sink) -> void override;

    auto reset() noexcept -> void override;

    [[nodiscard]] auto stats() const noexcept -> const DecodeStats & override { return stats_; }

  private:
    /// @brief 把 iconv 产出的 UTF-8 交进严格腿。
    auto push_output(const char *begin, std::size_t size, CodePointSink &sink) -> void {
        if (size == 0U) {
            return;
        }
        utf8_.feed({reinterpret_cast<const std::byte *>(begin), size}, sink);
    }

    /// @brief 本件自己补出的替换字符（与严格腿的计数并进同一份统计）。
    auto emit_replacement(CodePointSink &sink) -> void {
        ++own_replaced_;
        sink.on_code_point(kReplacementCharacter);
    }

    auto refresh_stats() noexcept -> void {
        stats_.code_points = utf8_.stats().code_points + own_replaced_;
        stats_.replaced = utf8_.stats().replaced + own_replaced_;
    }

    IconvHandle cd_;
    Utf8Decoder utf8_;
    std::array<char, kOutputChunk> out_{};
    std::vector<std::byte> stash_;   ///< iconv 退还给本件的残缺尾字节
    bool hanging_ = false;           ///< 残缺序列已被句柄吃进内部状态
    std::size_t own_replaced_ = 0;
    DecodeStats stats_{};
};

auto IconvDecoder::feed(std::span<const std::byte> bytes, CodePointSink &sink) -> void {
    std::span<const std::byte> input = bytes;
    std::vector<std::byte> joined;
    if (!stash_.empty()) {
        joined.insert(joined.end(), stash_.begin(), stash_.end());
        joined.insert(joined.end(), bytes.begin(), bytes.end());
        stash_.clear();
        hanging_ = false;  // 尾巴回到本件手里，句柄那一侧是干净的
        input = joined;
    }

    // iconv 的 inbuf 签名是 char**（它只推进指针、不改字节），故此处去 const 是接口历史包袱所致。
    auto *base = const_cast<char *>(reinterpret_cast<const char *>(input.data()));
    std::size_t consumed = 0;
    while (consumed < input.size()) {
        auto *in = base + consumed;
        std::size_t in_left = input.size() - consumed;
        auto *out = out_.data();
        std::size_t out_left = out_.size();
        const auto rc = ::iconv(cd_.get(), &in, &in_left, &out, &out_left);
        consumed = input.size() - in_left;
        push_output(out_.data(), static_cast<std::size_t>(out - out_.data()), sink);
        if (rc != static_cast<std::size_t>(-1)) {
            continue;
        }
        if (errno == E2BIG) {
            continue;  // 输出缓冲写满，下一轮接着转
        }
        if (errno == EINVAL) {
            // 块边界上的半个序列：退还的自存，吃进句柄的记一笔。
            if (in_left > 0U) {
                const auto *rest = reinterpret_cast<const std::byte *>(in);
                stash_.assign(rest, rest + in_left);
                consumed = input.size();
            } else {
                hanging_ = true;
            }
            break;
        }
        if (errno == EILSEQ) {
            emit_replacement(sink);
            // iconv 停在残缺起始处不消耗它，故本件至少推进一字节；显式复位一次防实现留脏状态。
            consumed = std::min(consumed + 1U, input.size());
            reset_state(cd_.get());
            continue;
        }
        AURORA_LOG_WARN("term", "codec: iconv feed stopped with errno ", errno);
        reset_state(cd_.get());
        stash_.clear();
        hanging_ = false;
        break;
    }
    refresh_stats();
}

auto IconvDecoder::finish(CodePointSink &sink) -> void {
    auto *out = out_.data();
    std::size_t out_left = out_.size();
    const auto rc = ::iconv(cd_.get(), nullptr, nullptr, &out, &out_left);
    push_output(out_.data(), static_cast<std::size_t>(out - out_.data()), sink);
    utf8_.finish(sink);  // iconv 的产物本不该留半截，这一句是那条前提的兜底
    const bool incomplete = !stash_.empty() || hanging_ || rc == static_cast<std::size_t>(-1);
    stash_.clear();
    hanging_ = false;
    if (incomplete) {
        emit_replacement(sink);  // 对端在半个序列处收摊：补一个替换字符，最后一行不凭空消失
    }
    refresh_stats();
}

auto IconvDecoder::reset() noexcept -> void {
    reset_state(cd_.get());
    stash_.clear();
    hanging_ = false;
    utf8_.reset();
    refresh_stats();
}

/// @brief UTF-8 档位的发送腿：口径与 `encode_utf8` 逐字节一致，只在不可表示处按策略分岔。
[[nodiscard]] auto encode_utf8_leg(std::u32string_view text, UnrepresentablePolicy policy)
    -> EncodeResult {
    EncodeResult result{};
    for (const char32_t cp : text) {
        const auto value = static_cast<std::uint32_t>(cp);
        const bool representable =
            !(value >= 0xD800U && value <= 0xDFFFU) && value <= 0x10FFFFU;
        if (representable) {
            static_cast<void>(append_utf8(cp, result.bytes));
            continue;
        }
        ++result.unrepresentable;
        // 代理项与越界值连 UTF-8 形态都不存在，故「原样透传」在这一档与「替换」同归。
        if (policy != UnrepresentablePolicy::DropWithNotice) {
            static_cast<void>(append_utf8(kReplacementCharacter, result.bytes));
        }
    }
    return result;
}

/// @brief 把一个码点按目标编码写入 @p out；不可表示回 false。
[[nodiscard]] auto encode_one(iconv_t cd, const std::string &utf8_bytes, std::string &out)
    -> bool {
    auto *in = const_cast<char *>(utf8_bytes.data());
    std::size_t in_left = utf8_bytes.size();
    std::array<char, kOutputChunk> chunk{};
    auto *out_ptr = chunk.data();
    std::size_t out_left = chunk.size();
    const auto rc = ::iconv(cd, &in, &in_left, &out_ptr, &out_left);
    out.append(chunk.data(), static_cast<std::size_t>(out_ptr - chunk.data()));
    if (rc == static_cast<std::size_t>(-1)) {
        reset_state(cd);
        return false;
    }
    return in_left == 0U;
}

/// @brief 码点的 UTF-8 形态；代理项与越界值回空串（连形态都没有的码点走单独一支）。
[[nodiscard]] auto utf8_form(char32_t cp) -> std::string {
    std::string bytes;
    if (append_utf8(cp, bytes)) {
        return {};
    }
    return bytes;
}

/// @brief 逐码点慢路：策略对每个不可表示码点单独生效。
[[nodiscard]] auto encode_per_code_point(iconv_t cd, std::u32string_view text,
                                         UnrepresentablePolicy policy) -> EncodeResult {
    EncodeResult result{};
    std::string replacement_utf8;
    static_cast<void>(append_utf8(kReplacementCharacter, replacement_utf8));
    for (const char32_t cp : text) {
        const auto form = utf8_form(cp);
        if (!form.empty() && encode_one(cd, form, result.bytes)) {
            continue;
        }
        ++result.unrepresentable;
        if (policy == UnrepresentablePolicy::PassThroughUtf8 && !form.empty()) {
            result.bytes.append(form);  // 原样发该码点的 UTF-8 字节，由对端自行处置
            continue;
        }
        if (policy == UnrepresentablePolicy::DropWithNotice) {
            continue;  // 一个字节都不发
        }
        // 替换档，以及「连 UTF-8 形态都没有」的透传档（无原样可发，与替换同归）：
        // 目标编码里连替换字符都没有时退到 `?`——ASCII 在本族五档里恒可表示。
        if (!encode_one(cd, replacement_utf8, result.bytes)) {
            result.bytes.push_back('?');
        }
    }
    return result;
}

}  // namespace

auto make_session_decoder(std::string_view encoding) -> std::unique_ptr<SessionDecoder> {
    const auto name = iconv_name_for(encoding);
    if (!name) {
        AURORA_LOG_WARN("term", "codec: unknown session encoding, falls back to UTF-8");
        return std::make_unique<Utf8LegDecoder>();
    }
    if (same_name(*name, "UTF-8")) {
        return std::make_unique<Utf8LegDecoder>();
    }
    IconvHandle cd{"UTF-8", *name};
    if (!cd.valid()) {
        AURORA_LOG_WARN("term", "codec: session encoding unavailable locally, falls back to UTF-8");
        return std::make_unique<Utf8LegDecoder>();
    }
    return std::make_unique<IconvDecoder>(std::move(cd));
}

auto resolve_encoding_name(std::string_view encoding) -> std::string_view {
    for (const auto &alias : kAliases) {
        if (same_name(encoding, alias.config_name)) {
            return alias.config_name;
        }
    }
    return "UTF-8";
}

auto encode_for_encoding(std::u32string_view text, std::string_view encoding,
                         UnrepresentablePolicy policy) -> EncodeResult {
    const auto name = iconv_name_for(encoding);
    if (!name || same_name(*name, "UTF-8")) {
        return encode_utf8_leg(text, policy);
    }
    if (text.empty()) {
        return EncodeResult{};
    }

    IconvHandle cd{*name, "UTF-8"};
    if (!cd.valid()) {
        // 与解码侧同一条回落：本地没有该档位就按 UTF-8 发，绝不静默发乱码字节。
        return encode_utf8_leg(text, policy);
    }

    // 快路：整段一次转换。文中若含代理项/越界码点，整段的 UTF-8 形态本身就缺那一位，
    // 直接判给慢路（慢路逐码点，看得见那一个）。
    std::string utf8_in;
    utf8_in.reserve(text.size() + text.size() / 2U);
    if (encode_utf8(text, utf8_in) == 0U) {
        std::string bytes;
        bytes.reserve(utf8_in.size());
        auto *in = utf8_in.data();
        std::size_t in_left = utf8_in.size();
        std::array<char, kOutputChunk> chunk{};
        bool all_converted = false;
        while (true) {
            auto *out = chunk.data();
            std::size_t out_left = chunk.size();
            const auto rc = ::iconv(cd.get(), &in, &in_left, &out, &out_left);
            bytes.append(chunk.data(), static_cast<std::size_t>(out - chunk.data()));
            if (rc == static_cast<std::size_t>(-1) && errno == E2BIG) {
                continue;
            }
            all_converted = (rc != static_cast<std::size_t>(-1)) && in_left == 0U;
            break;
        }
        if (all_converted) {
            return EncodeResult{.bytes = std::move(bytes), .unrepresentable = 0U};
        }
        // 快路失败说明文中至少有一个不可表示码点：整段交给慢路重来，策略逐码点生效。
    }
    return encode_per_code_point(cd.get(), text, policy);
}

}  // namespace borealis::term
