/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（搜索的前后跳转、居中滚动与关闭）+ include/borealis/ui/search.h 的游标消费腿
/// 测试说明: 断言的是**视口侧的滚动与状态处置**，而不是匹配算式与游标算式本身（区间的形状、双宽整字符、
///           上限截断与首尾相连由 `utest_search` 逐条覆盖，`itest_search_scan` 覆盖扫描的调用纪律）。
///           值得单独证的有七件事：
///           ① 目标行**已在可见窗内**时画面完全不动（判据 D1-b）——这条是「不重新居中」，若实现无条件
///              居中，连按 `Enter` 会在每一格上把画面往回拽一次，正是视觉稿点名要避免的手感；
///           ② 目标行在窗外时把**内核的 `offset_y`** 写成「目标行 − ⌊rows/2⌋」并钳在 `[0, max_offset]`
///              内（判据 D2-a/D2-b），且下一帧 `reproject` 的「距底恒定」保持的是这个**新**值而不是旧值
///              ——只断跳转后那一瞬的读数抓不到「写的是临时偏移、下一帧被换源抹掉」；
///           ③ 顶部那一档的钳位（目标行不足半屏时窗顶落在 0 而不是回绕到底）；
///           ④ 连按 `Enter` 到表尾回绕到表头，回绕后的那一跳照样是一次真滚动（判据 D1-a 与 D2-a 的交接：
///              首尾相连归 `SearchMatches::advance`，本件只管「跳完之后看得见」）；
///           ⑤ 关闭清匹配表与全部高亮，却**保留查询文本与回看位置**（判据 D7 / D2-c），并把已扫条件记回
///              空于是重开那一帧 `search_pending_submit()` 为真（判据 F1-c 的「不自动重扫」落成 B5 的
///              「按 Enter 搜索」而不是上一份结果的计数）；
///           ⑥ 关闭发生在**打字与结算之间**时不留脏：本帧不重扫、高亮不复活（`close_search()` 里那句
///              脏标志清算的唯一证人——少了它，关掉浮层之后画面会自己亮起来）；
///           ⑦ 「表达式非法」随关闭一并清掉（判据 B4 是这一次搜索的错，不是浮层的永久状态）。
///
///           「落在中间那一格」这句在本件以**距底行数**为据：`window_top` ＝ 总行数 − rows − 距底，而
///           跳转写的是 `offset_y` ＝ 窗顶，两式相减即判据 D2-b 的那个唯一输入，故一个数就能把「居中」
///           与「贴顶 / 贴底」分开（像素侧的高亮确实落在第 12 行归 `itest_render_viewport`）。查询命中的
///           内容一律经**真实字节流**投进网格，而不是注入网格快照。本套件全不经绘制，故没有无头窗口与
///           帧缓冲，也不需要 `AURORA_BACKEND_HEADLESS`：帧序由 `on_frame()` 手工驱动（未挂父时
///           `mark_needs_paint()` 只是自标脏，不会走空）。界面腿**不**另配时间断言（AGENTS.md §4.4 第 21 条）。

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aurora/aurora.h"
#include "borealis/grid/storage.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/terminal.h"
#include "borealis/term/width.h"
#include "borealis/ui/search.h"
#include "framework/aurora_test.h"
// 私有头（裁决 D1①），故按相对路径取用而不给整个测试目标开 `src/` 含目录。
#include "../../src/ui/terminal_view.h"

namespace borealis::test_cases::itest_search_viewport {

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::RowSpan;
using borealis::ui::SearchDirection;
using borealis::ui::SearchMatches;
using borealis::ui::SearchQuery;
using borealis::ui::TerminalView;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kRows = 24U;
constexpr std::size_t kHalfRows = kRows / 2U;  ///< 居中算式里的那半个视口（判据 D2-b）
constexpr std::size_t kTotalLines = 60U;       ///< 一屏 24 行 + 可回看 36 行

/// @brief 匹配标记：只由显式投出的那几行携带，填充行 `row<k>` 结构上不含它。
constexpr std::string_view kNeedle{"qz7k"};
const std::u32string kNeedleQuery{U"qz7k"};

UnicodeWidthPolicy width_policy;

[[nodiscard]] auto holds(std::initializer_list<std::size_t> values, std::size_t value) -> bool {
    for (const std::size_t item : values) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

/// @brief 传输连接替身：只负责把字节投进会话。
///
/// 写出方向在本套件没有观测者（键入与上报的证人各在 `itest_key_input` 与 `itest_mouse_report`），
/// 故不留记录成员——留了就是没人读的第二个现场。
class FakeConnection final : public Connection {
  public:
    auto start(ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> bytes) -> void override { (void)bytes; }

    auto resize(Size) -> void override {}

    auto close() -> void override { alive_ = false; }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    auto deliver(std::string_view bytes) -> void {
        std::vector<std::byte> raw;
        raw.reserve(bytes.size());
        for (const char c : bytes) {
            raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        events_->on_bytes(raw);
    }

  private:
    ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

/// @brief 驱动台：会话 + 替身连接 + 视口控件。
///
/// scrollback 一律给足（`kTotalLines` 远小于其上限），于是每一次跳转都落在真历史上，钳位与居中
/// 两条判据都不会被「存储里没有可回看的行」这一档短路掉。
class Harness {
  public:
    explicit Harness()
        : session_(std::make_unique<Session>(own_connection(), kNominalSize, kTotalLines, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, TerminalView::Appearance{},
                                                TerminalView::InteractionOptions{})) {
        session_->start();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投一行文本：行间以 `\r\n` 分隔，且**最后一行不带换行符**（带了它就多一次隐式上滚，
    ///        用例里的行号就得跟着一次没人记账的位移算）。
    auto println(std::string_view text) -> void {
        std::string bytes;
        if (line_open_) {
            bytes = "\r\n";
        }
        bytes.append(text);
        line_open_ = true;
        connection_->deliver(bytes);
    }

    /// @brief 排帧到队列见底：一次 `on_frame` 即一轮「取脏 → 按距底重取可见窗 → 结算扫描 → 折算位移」。
    auto frame() -> void {
        do {
            view_->on_frame();
        } while (session_->has_damage());
    }

    /// @brief 投 @p count 行：行号落在 @p needle_rows 的那几行内容是标记串，其余是 `row<行号>`。
    auto feed(std::size_t count, std::initializer_list<std::size_t> needle_rows) -> void {
        for (std::size_t row = 0; row < count; ++row) {
            const bool marked = holds(needle_rows, row);
            println(marked ? std::string_view{kNeedle} : "row" + std::to_string(row));
        }
    }

    auto set_query(SearchQuery query) -> void { view_->set_search_query(std::move(query)); }
    auto submit() -> void { view_->submit_search_query(); }
    auto jump(SearchDirection direction) -> void { view_->advance_search(direction); }
    auto close() -> void { view_->close_search(); }

    [[nodiscard]] auto scans() const noexcept -> std::size_t { return view_->search_scan_count(); }
    [[nodiscard]] auto matches() const noexcept -> const SearchMatches * { return view_->search_matches(); }
    [[nodiscard]] auto invalid() const noexcept -> bool { return view_->search_pattern_invalid(); }
    [[nodiscard]] auto pending() const noexcept -> bool { return view_->search_pending_submit(); }
    [[nodiscard]] auto query() const noexcept -> const SearchQuery & { return view_->search_query(); }

    /// @brief 回看位置距底的行数（0 = 贴底）：本件唯一可判的滚动观测点。
    [[nodiscard]] auto back() const -> std::size_t { return view_->scrollback_rows_from_bottom(); }

    /// @brief 游标当下指着的那一段匹配的行号；游标为空时回空值。
    [[nodiscard]] auto landed_row() const -> std::optional<std::size_t> {
        if (const SearchMatches *table = matches(); table != nullptr) {
            if (const std::optional<RowSpan> hit = table->current(); hit.has_value()) {
                return hit->row;
            }
        }
        return std::nullopt;
    }

    /// @brief 游标的表内下标（「回绕到表头」那句判据的观测点）。
    [[nodiscard]] auto cursor_index() const -> std::optional<std::size_t> {
        return matches() != nullptr ? matches()->cursor() : std::nullopt;
    }

  private:
    [[nodiscard]] auto own_connection() -> std::unique_ptr<Connection> {
        auto owned = std::make_unique<FakeConnection>();
        connection_ = owned.get();
        return owned;
    }

    FakeConnection *connection_ = nullptr;  ///< 非拥有，会话持有。
    std::unique_ptr<Session> session_;
    std::shared_ptr<TerminalView> view_;
    bool line_open_ = false;
};

/// @brief 贴底态的窗顶行号（可见窗是第 `kTotalLines - kRows`..`kTotalLines - 1` 行）。
constexpr std::size_t kBottomWindowTop = kTotalLines - kRows;

/// @brief 判据 D2-b 的居中档读数：把 @p storage_row 滚到竖直居中之后的**距底行数**。
///
/// 两条既有算式的合成：窗顶 = 目标行 − ⌊rows/2⌋（居中），而距底 = max_offset − 窗顶（内核定义）。
/// 写成算式而不是抄一个数，是为了让「取错半档」与「写成贴顶」两种实现各给出不同的预期。
[[nodiscard]] constexpr auto centered_back(std::size_t storage_row) -> std::size_t {
    return kBottomWindowTop - (storage_row - kHalfRows);
}

}  // namespace

AURORA_TEST_CASE(a_jump_to_a_match_already_on_screen_leaves_the_view_alone) {
    // 判据 D1-b：目标行已在可见窗内时画面**完全不动**，只把那一格升到全色档（档位的像素侧在绘制套件）。
    Harness h;
    h.feed(kTotalLines, {40U});
    h.frame();
    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_REQUIRE_EQ(h.matches()->count(), 1U);
    AURORA_TEST_REQUIRE(!h.landed_row().has_value());  // 新表还没有游标：第一次 Enter 从表头起算
    AURORA_TEST_CHECK_EQ(h.back(), 0U);                // 贴底

    h.jump(SearchDirection::Forward);
    // 第 40 行本来就在窗内（36..59 的第 4 格），故距底一格都不动。
    AURORA_TEST_CHECK_EQ(h.landed_row(), std::optional<std::size_t>{40U});
    AURORA_TEST_CHECK_EQ(h.cursor_index(), std::optional<std::size_t>{0U});
    AURORA_TEST_CHECK_EQ(h.back(), 0U);

    h.frame();  // 换源一帧：动没动过画面在这里才算定局
    AURORA_TEST_CHECK_EQ(h.back(), 0U);

    // 只有一条匹配时 `Shift+Enter` 回绕回来还是那一格，画面依旧不动。
    h.jump(SearchDirection::Backward);
    AURORA_TEST_CHECK_EQ(h.landed_row(), std::optional<std::size_t>{40U});
    AURORA_TEST_CHECK_EQ(h.back(), 0U);
}

AURORA_TEST_CASE(a_jump_out_of_view_rolls_the_landed_row_to_the_middle_cell) {
    // 判据 D2-a / D2-b：窗外的目标行滚到竖直居中，且这是**内核状态**的一次改变而不是临时偏移。
    Harness h;
    h.feed(kTotalLines, {30U});
    h.frame();
    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.matches()->count(), 1U);
    AURORA_TEST_CHECK_EQ(h.back(), 0U);  // 贴底，可见窗 36..59 而目标行 30 在其上方

    h.jump(SearchDirection::Forward);
    // 居中：窗顶 = 30 − 12 = 18，于是距底 = (60 − 24) − 18 = 18。这一句同时把「贴顶」(36) 与
    // 「贴底」(0) 两种实现分开，也把 `⌊rows/2⌋` 取错一档分开（取 11 或 13 都给出另一个距底数）。
    AURORA_TEST_CHECK_EQ(h.landed_row(), std::optional<std::size_t>{30U});
    AURORA_TEST_CHECK_EQ(h.back(), centered_back(30U));
    // 判据 D2-b 的「上下各有 ⌊rows/2⌋ 行上下文」由窗顶 18 直接给出：上面第 0..11 格共 12 行、
    // 下面第 13..23 格共 11 行（视口偶数行，故下方少一格）。

    // 真滚动的证人：下一帧 `reproject` 的「距底恒定」保持的是这个**新**值而不是跳转前的旧值。
    h.frame();
    AURORA_TEST_CHECK_EQ(h.back(), centered_back(30U));
}

AURORA_TEST_CASE(jumps_wrap_at_the_end_and_clamp_at_the_storage_top) {
    Harness h;
    h.feed(kTotalLines, {3U, 45U});
    h.frame();
    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.matches()->count(), 2U);

    // 第一跳落到表头的第 3 行：居中算式给出 3 − 12 < 0，钳位档把窗顶落在 0 而不是回绕。
    h.jump(SearchDirection::Forward);
    h.frame();
    AURORA_TEST_CHECK_EQ(h.landed_row(), std::optional<std::size_t>{3U});
    AURORA_TEST_CHECK_EQ(h.cursor_index(), std::optional<std::size_t>{0U});
    AURORA_TEST_CHECK_EQ(h.back(), kBottomWindowTop);  // 窗顶 0 ⇒ 距底恰为 max_offset

    // 第二跳是表尾的第 45 行：此时窗顶 0，故仍在窗外，居中后窗顶 33、距底 3。
    h.jump(SearchDirection::Forward);
    h.frame();
    AURORA_TEST_CHECK_EQ(h.landed_row(), std::optional<std::size_t>{45U});
    AURORA_TEST_CHECK_EQ(h.cursor_index(), std::optional<std::size_t>{1U});
    AURORA_TEST_CHECK_EQ(h.back(), 3U);

    // 第三跳撞到表尾回绕到表头（判据 D1-a 的首尾相连），回绕那一跳照样是一次真滚动而不是停在原地。
    h.jump(SearchDirection::Forward);
    h.frame();
    AURORA_TEST_CHECK_EQ(h.landed_row(), std::optional<std::size_t>{3U});
    AURORA_TEST_CHECK_EQ(h.cursor_index(), std::optional<std::size_t>{0U});
    AURORA_TEST_CHECK_EQ(h.back(), kBottomWindowTop);

    // 往回绕一次：从表头退到表尾，画面跟着回到第 45 行居中那一档。
    h.jump(SearchDirection::Backward);
    h.frame();
    AURORA_TEST_CHECK_EQ(h.landed_row(), std::optional<std::size_t>{45U});
    AURORA_TEST_CHECK_EQ(h.cursor_index(), std::optional<std::size_t>{1U});
    AURORA_TEST_CHECK_EQ(h.back(), 3U);
}

AURORA_TEST_CASE(closing_keeps_the_query_and_the_view_but_clears_everything_else) {
    // 判据 D7 / D2-c / F1-c 三条压在同一个入口上：清表与高亮、留文本与回看、重开不自动重扫。
    Harness h;
    h.feed(kTotalLines, {30U});
    h.frame();
    h.set_query(SearchQuery{.text = kNeedleQuery, .case_sensitive = true});
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.scans(), 1U);
    h.jump(SearchDirection::Forward);
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.back(), centered_back(30U));  // 非贴底，「保留回看位置」才有得可保

    h.close();
    AURORA_TEST_CHECK_NULL(h.matches());            // 表清掉，故高亮一次不剩（判据 D7）
    AURORA_TEST_CHECK_FALSE(h.invalid());
    AURORA_TEST_CHECK_EQ(h.back(), centered_back(30U));  // 判据 D2-c：回看位置不动
    // 文本与两档逐字留着（判据 F1-c 的前半句，也是浮层重开时要读回的那份）。
    AURORA_TEST_CHECK(h.query() == (SearchQuery{.text = kNeedleQuery, .case_sensitive = true}));
    // 已扫条件记回空：于是重开那一帧显示 B5 的「按 Enter 搜索」而不是「共 1 个」。
    AURORA_TEST_CHECK(h.pending());

    // 重开不自动重扫：连排两帧既不换表也不动计数。
    h.frame();
    h.frame();
    AURORA_TEST_CHECK_NULL(h.matches());
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);

    // 用户按 Enter 才扫（判据 F1-c 的后半句），且扫出来的还是同一行、回看位置也没被动过。
    h.submit();
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 2U);
    AURORA_TEST_CHECK_FALSE(h.pending());
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(h.matches()->count(), 1U);
    AURORA_TEST_CHECK_EQ(h.back(), centered_back(30U));
    AURORA_TEST_CHECK(!h.landed_row().has_value());  // 新表不带游标：跳转仍是显式动作

    // 重开之后第一次 Enter 落在同一格上：那一行此刻已在可见窗内，故画面不动（判据 D1-b 续用）。
    h.jump(SearchDirection::Forward);
    AURORA_TEST_CHECK_EQ(h.landed_row(), std::optional<std::size_t>{30U});
    AURORA_TEST_CHECK_EQ(h.back(), centered_back(30U));
}

AURORA_TEST_CASE(closing_between_typing_and_the_frame_leaves_no_pending_highlight) {
    // 判据 D7 的另一档：关闭发生在「打字之后、本帧结算之前」。少了 `close_search()` 里的脏标志清算，
    // 这一帧就会把已经关掉的高亮又扫出来。
    Harness h;
    h.feed(kTotalLines, {30U, 45U});
    h.frame();
    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.scans(), 1U);

    h.set_query(SearchQuery{.text = U"row"});  // 字面量档每键置脏，而本帧先不排
    h.close();
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);       // 没有第二次扫描
    AURORA_TEST_CHECK_NULL(h.matches());       // 也没有复活的高亮
    // 文本仍按关闭那一刻的样子留着：`std::u32string` 没有可读的失败打印形态，故整份条件一次比。
    AURORA_TEST_CHECK(h.query() == SearchQuery{.text = U"row"});
}

AURORA_TEST_CASE(a_jump_with_nothing_to_land_on_never_moves_the_view) {
    // 两档「没有下一个」：从未扫过（表为空指针）与扫过但零匹配（表在而游标为空）。浮层把它们显示成
    // B0 / B2，本入口则既不滚动也不崩，更不额外催一次扫描。
    Harness h;
    h.feed(kTotalLines, {30U});
    h.frame();
    AURORA_TEST_CHECK_EQ(h.back(), 0U);
    h.jump(SearchDirection::Forward);
    AURORA_TEST_CHECK_EQ(h.back(), 0U);
    AURORA_TEST_CHECK_EQ(h.scans(), 0U);

    h.set_query(SearchQuery{.text = U"zzz"});  // 网格里没有这串
    h.frame();
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_REQUIRE_EQ(h.matches()->count(), 0U);
    AURORA_TEST_CHECK(!h.landed_row().has_value());

    h.jump(SearchDirection::Forward);
    h.jump(SearchDirection::Backward);
    AURORA_TEST_CHECK_EQ(h.back(), 0U);
    AURORA_TEST_CHECK(!h.landed_row().has_value());
}

AURORA_TEST_CASE(closing_clears_the_bad_pattern_flag_with_the_table) {
    // 判据 B4 的「表达式非法」是这一次搜索的错，不是浮层的永久状态：关闭要把它一并清掉，
    // 否则重开时先看到一句没有来由的报错。
    Harness h;
    h.feed(kTotalLines, {30U});
    h.frame();

    h.set_query(SearchQuery{.text = U"[a", .regex = true});
    h.submit();
    h.frame();
    AURORA_TEST_REQUIRE(h.invalid());
    AURORA_TEST_CHECK_NULL(h.matches());

    h.close();
    AURORA_TEST_CHECK_FALSE(h.invalid());

    // 改对之后照常出表，且标志不再粘住。
    h.set_query(SearchQuery{.text = U"row[0-9]+", .regex = true});
    h.submit();
    h.frame();
    AURORA_TEST_CHECK_FALSE(h.invalid());
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(h.matches()->count(), kTotalLines - 1U);
}

}  // namespace borealis::test_cases::itest_search_viewport
