#pragma once

// ============================================================
// 终端内搜索的匹配表（include/borealis/ui/search.h）
// ------------------------------------------------------------
// `SPEC.FEAT.INTERACT.04` 里与浮层无关的那半：把一份查询扫成逐行的匹配列区间表，并给出
// 前后跳转的游标、匹配计数与「新输出把内容顶走」之后的行号折算。Ctrl+F 输入框、两枚开关的
// 呈现、高亮落笔与滚动到当前匹配都在界面腿，本件刻意不知道像素与控件。
//
// 不含 Aurora 类型（AGENTS.md §4.4 第 20 条）：匹配区间的正确性——整字符不被切成半格、
// 行尾填充不算内容、正则档的零宽匹配不产高亮、上限档的截断要说得出口——必须能脱开界面全量单测。
//
// 行号坐标沿用**存储行序**（`grid::Storage::line` 的索引，0 为最顶）：一次扫描覆盖 scrollback
// 与视口的全部行，于是高亮天生跨可见区，只能按权威网格的行序存；副本行序随回看偏移变，按它存
// 就会在滚动时漂（`ui::selection.h` 的同一条理由）。顶边位移的折算与 `translate_selection_rows`
// 共用 `grid::Storage::dropped_lines()` 这一唯一输入（裁决 7.78③）。
//
// 两档扫描的**成本差一个数量级**，这决定了判据只在字面量档成立（实测见裁决 7.78①）：字面量档
// 在格子的码点空间直扫、零分配；正则档要先把每行折成引擎可吃的 UTF-16 序列（本工具链的
// `std::basic_regex` 只支持 `char` 与 `wchar_t`，`char32_t` 的 `regex_traits` 是未定义类型），
// 故宽匹配式的耗时可到百倍以上。界面腿据此把正则档的重扫钉在 Enter 提交那一刻，不随每键重扫。
// ============================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "borealis/grid/storage.h"
#include "borealis/ui/selection.h"

namespace borealis::ui {

/// @brief 一次搜索的查询条件（浮层里那三个控件的状态：文本 + 两枚开关）。
///
/// 文本是码点序列而非 UTF-8：字面量档据此可直接与格子内容逐码点比（`word_span_at` 里那条
/// 「按字节比会把 `é` 的续字节当成 `©`」的同因），正则档也免去先解码一次。
struct SearchQuery {
    std::u32string text;         ///< 查询原文；空串即「还没打字」，不产匹配也不报错。
    bool case_sensitive{false};  ///< 大小写敏感。实测本工具链两档都只折 ASCII 字母（裁决 7.78②）。
    bool regex{false};           ///< 正则档：`text` 按 ECMAScript 语法编译。

    /// @brief 逐字段全等比较（「查询没变就不必重扫」的判据）。
    [[nodiscard]] auto operator==(const SearchQuery &other) const noexcept -> bool = default;
};

/// @brief 跳转方向（`Enter` 与 `Shift+Enter`）。
enum class SearchDirection : std::uint8_t {
    Forward,   ///< 往下一个匹配。
    Backward,  ///< 往上一个匹配。
};

/// @brief 匹配表上限（条）。
///
/// 会话字节流是不可信输入：一个宽匹配式在 100,000 行上可以命中上百万次，无上限的表就是
/// 无上限的常驻内存与扫描时间（时间门禁那 200 ms 也一并破掉）。到顶即**停止扫描**并置
/// `truncated`，浮层据此把计数呈现成下限档（「10000+」）而不是谎报总数（裁决 7.78④）。
inline constexpr std::size_t kMaxSearchMatches = 10000;

/// @brief 一次扫描的结果：按 (行, 列) 升序的匹配区间表 + 当前游标 + 是否触到上限。
///
/// 每个区间是**整格对齐**的闭开区间：区间右界把双宽字符的延续格算进去，左界恒是基础格，
/// 于是一个双宽字符要么整字符被匹配，要么完全不匹配（裁决 7.32② 同口径）。
///
/// 游标是**表内下标**而非行号：表会变（重扫、折算丢弃出界匹配），下标跟着表走才不会指错格。
/// 空表是合法状态（没搜到），此时游标为空、`advance` 回空值。
class SearchMatches final {
  public:
    /// @brief 匹配条数（即浮层「匹配计数」显示的那个数，触顶时另有 `truncated`）。
    [[nodiscard]] auto count() const noexcept -> std::size_t { return spans_.size(); }

    /// @brief 是否因触到 `kMaxSearchMatches` 而停止扫描。
    ///
    /// 为真时 `count()` 是**下限**而非总数：表外还有多少匹配无人知道，浮层不得显示成「共 N 个」。
    /// 会话字节流是不可信输入，故本件只保证「有界」，与 `grid::Row::kMaxCombiningMarksPerCell`
    /// 那条零宽标记上限同一口径。
    [[nodiscard]] auto truncated() const noexcept -> bool { return truncated_; }

    /// @brief 全部匹配区间，按 (行, 列) 升序。
    ///
    /// 出**视图**而非快照：绘制侧每帧按可见行过滤，拷一份上限档的表就是每帧 240 KB。
    /// 视图的失效点是 `translate_rows` 与 owning 对象的析构，故逐帧现取即可。
    [[nodiscard]] auto spans() const noexcept -> std::span<const RowSpan> { return spans_; }

    /// @brief 当前游标的表内下标；未落在任何匹配上时为空（新结果、或当前匹配已被顶出存储顶端）。
    [[nodiscard]] auto cursor() const noexcept -> std::optional<std::size_t> { return cursor_; }

    /// @brief 当前游标指着的那一段匹配；游标为空时回空值。
    [[nodiscard]] auto current() const noexcept -> std::optional<RowSpan>;

    /// @brief 把游标移到一个匹配上并返回它（第一次 `Enter` 从表头起算）。
    ///
    /// 刻意**首尾相连**而非撞到端点就停：`Enter` 连按找下一个是搜索框的基本手感，停在末位就
    /// 再也绕不回前面那几个（`TabStrip::select_relative` 同一条理由，裁决 7.43）。
    /// @param direction 前 / 后。
    /// @return 落点的匹配区间；空表回空值且游标不变。
    [[nodiscard]] auto advance(SearchDirection direction) -> std::optional<RowSpan>;

    /// @brief 把整张表折算到「存储顶边前移了 @p rows_up 行」之后的同一份内容上。
    ///
    /// 与 `translate_selection_rows` 同一算式、同一输入（两次 `dropped_lines()` 读数之差），
    /// 差别只在丢弃：**逐条丢弃**被推出存储顶端的匹配（一条匹配就是一段高亮，留着它只会指着
    /// 另一行内容），而游标所指那条被丢弃时游标归空而不是硬指下一段——那一段用户并没选中。
    /// 下标随之减去被丢弃的前置条数，故游标始终跟着它当初指着的那一段匹配。
    /// 右界（越出存储底端）不校验：行数不是本调用的入参，绘制侧按可见行过滤自然不会命中它。
    /// @param rows_up 内容整体上移的行数；负数表示整体下移（顶部补空白、无历史时的上滚）。
    auto translate_rows(std::int64_t rows_up) -> void;

  private:
    /// @brief 由扫描结果构造：`search()` 是本类唯一的产出点，故不开公开构造——区间序与游标的
    ///        一致性由那一次扫描建立，别让调用方拼出一张自相矛盾的表。
    explicit SearchMatches(std::vector<RowSpan> spans, bool truncated) noexcept;

    friend auto search(const grid::Storage &storage, const SearchQuery &query)
        -> std::optional<SearchMatches>;

    std::vector<RowSpan> spans_;
    std::optional<std::size_t> cursor_;
    bool truncated_ = false;
};

/// @brief 扫一份权威网格，给出全部匹配。
///
/// 扫描范围是 `storage` 的**全部行**（scrollback + 视口），故高亮天生跨可见区；一次扫描的
/// 成本随行数线性增长，字面量档在 100,000 行上限网格上是毫秒级（判据 `SPEC.FEAT.INTERACT.04`
/// 的 200 ms P95 即钉在这一档，验收形态见裁决 7.78⑤）。
///
/// 两档的匹配文本口径一致，且都与复制腿同源：跳过双宽延续格（字符只在基础格出现一次）、
/// **不含行尾连续空白填充**（未写入的列全是 `U+0020`，算进内容会让「匹配计数」在每条终端
/// 输出上都爆出一串无意义的高亮）、零宽匹配不产区间（正则 `^`、`x*` 之类处处成立）。
/// 大小写不敏感在两档都**只折 ASCII 字母**（实测：`std::wregex` 的 `icase` 不折 `Ä`/`ä`，
/// 字面量档的折叠式同理），故非 ASCII 字母的大小写折叠本件不承诺（判据边界见裁决 7.78②）。
/// @param storage 权威网格（调用方须持会话锁访问，与 `copy_text` 同一条临界区纪律）。
/// @param query 查询条件。
/// @return 匹配表；`std::nullopt` **当且仅当**正则档的 `text` 编译失败（此时浮层报「表达式非法」
///         而不是「0 个匹配」，并保留上一份结果）。
[[nodiscard]] auto search(const grid::Storage &storage, const SearchQuery &query)
    -> std::optional<SearchMatches>;

}  // namespace borealis::ui
