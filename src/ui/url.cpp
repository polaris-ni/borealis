// ============================================================
// URL 识别与打开决策（src/ui/url.cpp）
// ------------------------------------------------------------
// 三件事都只读行内既有结构、只算字符串：OSC 8 取「同标识的连续游程」（网格侧表按列存，不是一份
// 区间表，稿 F2），启发式取「自带字符表里的连续游程 + 游程内的 scheme 起点」（刻意不借断词符，
// 稿 D3①），决策取「两档策略 + 同域名单」的折算。故每条口径都能脱开界面进单测（AGENTS §4.4 第 20 条）。
// ============================================================

#include "borealis/ui/url.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "borealis/grid/cell.h"
#include "borealis/grid/row.h"
#include "borealis/term/utf8.h"

namespace borealis::ui {
namespace {

constexpr std::string_view kSchemeSeparator{"://"};
constexpr std::string_view kWwwPrefix{"www."};
constexpr std::string_view kHttpScheme{"http"};

/// @brief URL 允许字符表里的 ASCII 标点档（RFC 3986 的 reserved + unreserved 去掉 `%` 之外项）。
///
/// 空格、`<>"`、反斜杠、`^`、竖线与两个大括号外的空白类都不在表内：它们要么在真 URL 里必须百分号
/// 编码，要么就是「这条串到此为止」的自然边界（稿判据 2 的 `|jq` 尾巴就断在竖线上）。
constexpr std::string_view kUrlPunctuation{"-._~:/?#[]@!$&'()*+,;=%"};

/// @brief 非 ASCII 里**断**游程的码点区段：标点、全角标点与空白类。
///
/// 表取「默认放行 + 区段排除」而非「默认排除 + 放行白名单」，因为屏上的路径可以整段是非 ASCII
/// （`https://a.cn/中文路径`），而可显示的标点区段才是有限的：这样判据 2 那句「双宽字符不截断
/// 游程」与「中文路径进游程、句号 `。` 断开」同时成立，不必维护一张无限长的放行表。
/// 以码点区间建表而非字面量：字面量要把非 ASCII 字符写进源文件（AGENTS §4.4 第 14 条）。
struct CodePointRange {
    char32_t first = 0;
    char32_t last = 0;
};

constexpr std::array<CodePointRange, 8> kBreakingRanges{
    CodePointRange{0x0080U, 0x009FU},  // C1 控制
    CodePointRange{0x00A0U, 0x00A0U},  // 不换行空格
    CodePointRange{0x2000U, 0x206FU},  // 通用标点（含各类破折号、引号、零宽类）
    CodePointRange{0x3000U, 0x303FU},  // CJK 标点与占位（句号 `。` 在此）
    CodePointRange{0xFF01U, 0xFF0FU},  // 全角 ASCII 标点（`！` 到 `／`）
    CodePointRange{0xFF1AU, 0xFF20U},  // 全角 `：；＜＝＞？＠`
    CodePointRange{0xFF3BU, 0xFF40U},  // 全角 `［＼］＾＿｀`
    CodePointRange{0xFF5BU, 0xFF65U},  // 全角 `｛｜｝～` 与半角片假名标点
};

/// @brief 行尾修剪掉的 ASCII 标点（稿 D3①：尾部 `.,;:!?'\"` 剔出）。
///
/// 双引号本来就不进游程（不在字符表内），列在这里只为与稿面逐字一致，不额外制造规则。
constexpr std::string_view kTrailingPunctuation{".,;:!?'\""};

/// @brief 括号配对修剪用的「闭符 → 开符」对。
constexpr std::array<std::pair<char32_t, char32_t>, 3> kCloserPairs{
    std::pair<char32_t, char32_t>{U')', U'('},
    std::pair<char32_t, char32_t>{U']', U'['},
    std::pair<char32_t, char32_t>{U'}', U'{'},
};

[[nodiscard]] auto is_ascii_letter(char32_t code_point) noexcept -> bool {
    return (code_point >= U'a' && code_point <= U'z') || (code_point >= U'A' && code_point <= U'Z');
}

[[nodiscard]] auto is_ascii_digit(char32_t code_point) noexcept -> bool {
    return code_point >= U'0' && code_point <= U'9';
}

[[nodiscard]] auto is_ascii_alnum(char32_t code_point) noexcept -> bool {
    return is_ascii_letter(code_point) || is_ascii_digit(code_point);
}

[[nodiscard]] auto to_ascii_lower(char32_t code_point) noexcept -> char32_t {
    return code_point >= U'A' && code_point <= U'Z' ? code_point + 32U : code_point;
}

[[nodiscard]] auto ascii_lowered(std::string_view text) -> std::string {
    std::string out{text};
    for (auto &byte : out) {
        byte = static_cast<char>(to_ascii_lower(static_cast<char32_t>(static_cast<unsigned char>(byte))));
    }
    return out;
}

/// @brief 某码点是否是 URL 的组成字符（断游程者除外）。
[[nodiscard]] auto is_url_code_point(char32_t code_point) noexcept -> bool {
    if (code_point < 0x21U) {
        return false;  // C0 控制与空格
    }
    if (code_point < 0x80U) {
        return is_ascii_alnum(code_point) ||
               kUrlPunctuation.find(static_cast<char>(code_point)) != std::string_view::npos;
    }
    return std::ranges::none_of(kBreakingRanges,
                               [code_point](const CodePointRange &range) {
                                   return code_point >= range.first && code_point <= range.last;
                               });
}

[[nodiscard]] auto is_trailing_punctuation(char32_t code_point) noexcept -> bool {
    return code_point < 0x80U &&
           kTrailingPunctuation.find(static_cast<char>(code_point)) != std::string_view::npos;
}

/// @brief 落点列折算到基础格列：双宽字符的延续格不承载字符，也不挂标识（稿 F2）。
[[nodiscard]] auto base_column(const grid::Row &row, std::size_t column) noexcept -> std::size_t {
    while (column > 0U && row.cell(column).is_wide_continuation()) {
        --column;
    }
    return column;
}

/// @brief 某列承载的码点（延续格按其基础格取，故双宽整字符要么全进游程要么全不进）。
[[nodiscard]] auto code_point_at(const grid::Row &row, std::size_t column) noexcept -> char32_t {
    return row.cell(base_column(row, column)).code_point;
}

/// @brief 取一列区间的原文（UTF-8）；延续格跳过，字符已由基础格写出一次。
[[nodiscard]] auto text_of(const grid::Row &row, std::size_t first, std::size_t last) -> std::string {
    std::string text;
    for (std::size_t column = first; column < last; ++column) {
        if (row.cell(column).is_wide_continuation()) {
            continue;
        }
        static_cast<void>(term::append_utf8(row.cell(column).code_point, text));
    }
    return text;
}

/// @brief 列 @p column 起是否逐字符等于 @p ascii（大小写不敏感，且不得越出 @p limit）。
[[nodiscard]] auto matches_prefix(const grid::Row &row, std::size_t column, std::size_t limit,
                                 std::string_view ascii) -> bool {
    for (std::size_t offset = 0U; offset < ascii.size(); ++offset) {
        const auto at = column + offset;
        if (at >= limit) {
            return false;
        }
        const auto code_point = code_point_at(row, at);
        if (code_point >= 0x80U || to_ascii_lower(code_point) != static_cast<char32_t>(ascii[offset])) {
            return false;
        }
    }
    return true;
}

/// @brief 游程内第一个「像 URL 起点」的位置。
struct UrlStart {
    std::size_t column = 0;       ///< 起点列。
    std::size_t prefix_length = 0;  ///< 前导长度：`scheme://` 或 `www.` 的字符数。
    bool implied_http = false;    ///< `www.` 档：目标要补 `http://` 前缀（稿 D2① 的那次「猜」）。
};

/// @brief scheme 是否命中白名单（表项须由 `parse_link_schemes` 归一过来，故此处只小写目标侧）。
[[nodiscard]] auto scheme_allowed(std::string_view target, const LinkScanOptions &options) -> bool {
    const auto at = target.find(kSchemeSeparator);
    if (at == std::string_view::npos || at == 0U) {
        return false;
    }
    const auto scheme = ascii_lowered(target.substr(0, at));
    return std::ranges::find(options.schemes, scheme) != options.schemes.end();
}

/// @brief 在 `[first, limit)` 游程内找 URL 起点；找不到即这一整段不是链接。
///
/// 起点须落在「前导非字母数字」处，于是 `xhttps://a` 与提示符里的 `user@host:` 都不误报（后者
/// 另有一层：它压根不带 `://`）。`www.` 只在 `http` 于白名单内时才认——那一档就是拿 `http://`
/// 前缀参与白名单判定（稿 D2①）。
[[nodiscard]] auto find_url_start(const grid::Row &row, std::size_t first, std::size_t limit,
                                 const LinkScanOptions &options) -> std::optional<UrlStart> {
    const auto http_allowed = std::ranges::find(options.schemes, std::string{kHttpScheme}) !=
                              options.schemes.end();
    for (std::size_t column = first; column < limit; ++column) {
        if (column != first && is_ascii_alnum(code_point_at(row, column - 1U))) {
            continue;  // 前导是字母数字 ⇒ 这只是某个词的一部分
        }
        for (const auto &scheme : options.schemes) {
            if (scheme.empty() || !matches_prefix(row, column, limit, scheme)) {
                continue;
            }
            const auto after_scheme = column + scheme.size();
            if (!matches_prefix(row, after_scheme, limit, kSchemeSeparator)) {
                continue;
            }
            if (after_scheme + kSchemeSeparator.size() >= limit) {
                continue;  // `://` 之后须还有字符：日志里光秃秃的 `https://` 不成链接
            }
            return UrlStart{column, scheme.size() + kSchemeSeparator.size(), false};
        }
        if (http_allowed && matches_prefix(row, column, limit, kWwwPrefix) &&
            column + kWwwPrefix.size() < limit && is_ascii_alnum(code_point_at(row, column + kWwwPrefix.size()))) {
            return UrlStart{column, kWwwPrefix.size(), true};
        }
    }
    return std::nullopt;
}

/// @brief 数一列区间里某个码点出现几次（括号配对的计数判据，稿 D3① 要求它可断言而非正则）。
[[nodiscard]] auto count_in_run(const grid::Row &row, std::size_t first, std::size_t last, char32_t code_point)
    -> std::size_t {
    std::size_t seen = 0U;
    for (std::size_t column = first; column < last; ++column) {
        seen += code_point_at(row, column) == code_point ? 1U : 0U;
    }
    return seen;
}

/// @brief 行尾修剪：反复剥尾部的 `.,;:!?` 一类标点与**不配对**的右括号，直到稳定。
///
/// 两规则必须交替跑：`https://a/x).` 先剥句号才轮到那枚孤立右括号，反之就漏。下界留在起点 +1，
/// 于是整段永不会被修成空串——「修到什么算完」由调用方的长度校验判，而不是这里。
[[nodiscard]] auto trim_tail(const grid::Row &row, std::size_t first, std::size_t last) -> std::size_t {
    auto end = last;
    for (;;) {
        const auto before = end;
        while (end > first + 1U && is_trailing_punctuation(code_point_at(row, end - 1U))) {
            --end;
        }
        for (const auto &[closer, opener] : kCloserPairs) {
            if (end <= first + 1U || code_point_at(row, end - 1U) != closer) {
                continue;
            }
            if (count_in_run(row, first, end, closer) > count_in_run(row, first, end, opener)) {
                --end;
                break;
            }
        }
        if (end == before) {
            return end;
        }
    }
}

/// @brief 该 scheme 的缺省端口（空串即「无缺省端口可折叠」，稿 D9① 的折叠腿）。
[[nodiscard]] auto default_port(std::string_view scheme) -> std::string_view {
    if (scheme == "http") {
        return "80";
    }
    if (scheme == "https") {
        return "443";
    }
    return {};
}

[[nodiscard]] auto all_ascii_digits(std::string_view text) -> bool {
    for (const auto byte : text) {
        if (byte < '0' || byte > '9') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto is_scheme_body(std::string_view scheme) -> bool {
    for (const auto byte : scheme) {
        const auto code_point = static_cast<char32_t>(static_cast<unsigned char>(byte));
        const bool ok = is_ascii_alnum(code_point) || byte == '+' || byte == '-' || byte == '.';
        if (!ok) {
            return false;
        }
    }
    return !scheme.empty();
}

}  // namespace

auto parse_link_schemes(const std::string_view csv) -> std::vector<std::string> {
    std::vector<std::string> schemes;
    std::size_t at = 0U;
    while (at <= csv.size()) {
        const auto comma = csv.find(',', at);
        auto item = csv.substr(at, comma == std::string_view::npos ? std::string_view::npos : comma - at);
        const auto first = item.find_first_not_of(" \t");
        const auto last = item.find_last_not_of(" \t");
        if (first != std::string_view::npos) {
            schemes.push_back(ascii_lowered(item.substr(first, last - first + 1U)));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        at = comma + 1U;
    }
    return schemes;
}

auto find_link_at(const grid::Row &row, GridCellPos point, const LinkScanOptions &options,
                  const std::optional<std::u32string> &osc8_target) -> std::optional<LinkHit> {
    const auto columns = row.columns();
    if (columns == 0U || point.column >= columns) {
        return std::nullopt;
    }
    const auto column = base_column(row, point.column);
    const auto link_id = row.hyperlink(column);
    if (link_id != grid::kNoHyperlink && osc8_target.has_value() && !osc8_target->empty()) {
        // OSC 8 侧交进来的是码点串，这里编一次成 UTF-8：此后提示条、确认框与落盘的域名单同持一份编码。
        std::string target;
        static_cast<void>(term::encode_utf8(*osc8_target, target));
        // 白名单同闸（判据 5）：OSC 8 的目标是会话给的，不能因为它「是程序声明的」就免检。
        // 不过闸即**不可点**，也不落回启发式——落回就是改用屏上文字猜目标，正是稿 D1② 要防的形态。
        if (!scheme_allowed(target, options)) {
            return std::nullopt;
        }
        std::size_t first = column;
        while (first > 0U && row.hyperlink(base_column(row, first - 1U)) == link_id) {
            --first;
        }
        std::size_t last = column;
        while (last + 1U < columns && row.hyperlink(base_column(row, last + 1U)) == link_id) {
            ++last;
        }
        return LinkHit{RowSpan{point.row, first, last + 1U}, std::move(target), LinkSource::Osc8};
    }

    // 到这里只有两种情形：这一格压根没有标识，或标识已被淘汰（裁决 7.21② 那一档）——后者按
    // 「不可点」处理后将同一段文字交给启发式重扫，因为屏幕上那串本身仍可能就是一条约好的 URL。
    if (!is_url_code_point(code_point_at(row, column))) {
        return std::nullopt;
    }
    std::size_t first = column;
    while (first > 0U && is_url_code_point(code_point_at(row, first - 1U))) {
        --first;
    }
    std::size_t last = column;
    while (last + 1U < columns && is_url_code_point(code_point_at(row, last + 1U))) {
        ++last;
    }
    const auto start = find_url_start(row, first, last + 1U, options);
    if (!start.has_value()) {
        return std::nullopt;
    }
    const auto end = trim_tail(row, start->column, last + 1U);
    if (end <= start->column + start->prefix_length) {
        return std::nullopt;  // 修剪之后只剩 `scheme://`：没有 host 的串不认作链接
    }
    auto target = text_of(row, start->column, end);
    if (start->implied_http) {
        target.insert(0, "http://");  // 前缀是猜的，故确认框的来源副行必须写明「纯文本启发式识别」（稿 D2①）
    }
    return LinkHit{RowSpan{point.row, start->column, end}, std::move(target), LinkSource::Heuristic};
}

auto TrustedLinkHosts::contains(const std::string_view host) const -> bool {
    return std::ranges::find(hosts_, host) != hosts_.end();
}

auto TrustedLinkHosts::add(std::string_view host) -> bool {
    if (host.empty()) {
        return false;  // 「记住」一枚解析不出来的域，就是 fail-closed 要挡的那件事本身
    }
    const auto at = std::ranges::lower_bound(hosts_, std::string{host});
    if (at != hosts_.end() && *at == host) {
        return false;
    }
    hosts_.insert(at, std::string{host});
    notify();
    return true;
}

auto TrustedLinkHosts::load(std::vector<std::string> hosts) -> void {
    hosts_ = std::move(hosts);
    std::ranges::sort(hosts_);
    hosts_.erase(std::unique(hosts_.begin(), hosts_.end()), hosts_.end());
}

auto TrustedLinkHosts::set_change_sink(ChangeSink sink) -> void { on_change_ = std::move(sink); }

auto TrustedLinkHosts::hosts() const noexcept -> std::span<const std::string> {
    return std::span<const std::string>{hosts_};
}

auto TrustedLinkHosts::notify() -> void {
    if (on_change_) {
        on_change_(hosts());
    }
}

auto plan_link_open(LinkSource source, LinkConfirmPolicy osc8_policy, LinkConfirmPolicy heuristic_policy,
                    const TrustedLinkHosts &trusted, std::string_view target) -> LinkOpenPlan {
    auto host = link_host_of(target);
    if (!host.has_value()) {
        return LinkOpenPlan{LinkOpenIntent::Refuse, {}, LinkOpenReason::HostUnparsable};
    }
    const auto policy = source == LinkSource::Osc8 ? osc8_policy : heuristic_policy;
    switch (policy) {
        case LinkConfirmPolicy::Never:
            return LinkOpenPlan{LinkOpenIntent::Open, std::move(*host), LinkOpenReason::None};
        case LinkConfirmPolicy::AskAlways:
            // 不读名单：这一档要的就是「每次都要看一眼」，把名单读进来反而让它变成 `AskOnce`。
            return LinkOpenPlan{LinkOpenIntent::Confirm, std::move(*host), LinkOpenReason::None};
        case LinkConfirmPolicy::AskOnce:
            return LinkOpenPlan{trusted.contains(*host) ? LinkOpenIntent::Open : LinkOpenIntent::Confirm,
                                std::move(*host), LinkOpenReason::None};
    }
    // 三档枚举已全部覆盖，这里只为让「控制流不得落到函数尾」那条警告闭嘴；取不到即 fail-closed。
    return LinkOpenPlan{LinkOpenIntent::Refuse, {}, LinkOpenReason::HostUnparsable};
}

auto link_host_of(std::string_view target) -> std::optional<std::string> {
    const auto at = target.find(kSchemeSeparator);
    if (at == std::string_view::npos || at == 0U) {
        return std::nullopt;
    }
    const auto scheme = ascii_lowered(target.substr(0, at));
    if (!is_scheme_body(scheme)) {
        return std::nullopt;
    }
    auto rest = target.substr(at + kSchemeSeparator.size());
    const auto authority_end = rest.find_first_of("/?#");
    auto authority = rest.substr(0, authority_end == std::string_view::npos ? rest.size() : authority_end);
    const auto userinfo = authority.rfind('@');
    if (userinfo != std::string_view::npos) {
        authority = authority.substr(userinfo + 1U);  // 信任按机器判，不按「以谁的身份登上去」判
    }

    std::string host;
    std::string_view port;
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        host = std::string{authority.substr(0, close + 1U)};  // 连方括号一起记，否则 `[::1]:80` 会切成半截
        auto after = authority.substr(close + 1U);
        if (!after.empty()) {
            if (after.front() != ':') {
                return std::nullopt;
            }
            port = after.substr(1U);
        }
    } else {
        const auto colon = authority.find(':');
        if (colon == std::string_view::npos) {
            host = std::string{authority};
        } else {
            host = std::string{authority.substr(0, colon)};
            port = authority.substr(colon + 1U);
        }
    }
    if (host.empty()) {
        return std::nullopt;
    }
    host = ascii_lowered(host);
    if (!port.empty()) {
        if (!all_ascii_digits(port)) {
            return std::nullopt;  // 端口不是全数字 ⇒ 这串不是我们认得的那种 URL，交出去就是猜
        }
        if (port != default_port(scheme)) {
            host.push_back(':');
            host.append(port);
        }
    }
    return scheme + "://" + host;
}

}  // namespace borealis::ui
