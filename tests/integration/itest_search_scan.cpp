/// 测试类型: integration
/// 目标单元: src/ui/terminal_view.cpp（搜索的扫描接缝与两档节流）+ include/borealis/ui/search.h 的消费腿
/// 测试说明: 断言的是**视口的调用纪律**而非匹配算式本身（区间形状、双宽整字符、上限截断与零宽匹配
///           由 `utest_search` 逐条覆盖）。值得单独证的有九件事：
///           ① 字面量档每键置脏，但**一轮排帧只结算一次**（判据 §5「一轮只扫一次」以扫描计数为据，
///              不看像素——计数是这一条唯一可判的观测点）；
///           ② 正则档打字**不扫**、`Enter` 提交才扫（裁决 7.78① 的实测代价：宽匹配式在 100,000 行上
///              一次 1623.5 ms，挂在每次按键之后就是冻结单线程 UI，违 AGENTS.md §4.5 第 25 条）；
///           ③ 空文本不发扫描、只清表（判据 A2-a），且清表不进计数——否则「一轮只扫一次」那条判据
///              就测成了「有没有人按过键」；
///           ④ 非法表达式保留旧表并报「表达式非法」，同时**不再催一次 Enter**（判据 B4 优先于 B5 的
///              物理根据：用户已经按过 Enter 了）；且下一次合法提交要把这标志清掉，不是粘住的；
///           ⑤ 扫描吃的是**权威网格**而非可见区副本——scrollback 里的匹配也进表（副本只有视口那 24 行，
///              故「扫副本」那种实现在此处结构上给 0 个匹配，这一条是它的唯一证人）；
///           ⑥ 新输出把内容顶走时**不重扫**，表按 `dropped_lines()` 的行号增量折算（与选区共用同一次
///              快照、零新增锁，裁决 7.78③），且折算后的行号与「按同一条件立刻重扫一次」的结论逐条相符
///              ——只断前者抓不到「折算量算错但恰好自洽」；
///           ⑦ 大小写档翻转是一次真重扫而不是对既有表做过滤（折叠只及 ASCII 那条已由纯逻辑件覆盖）；
///           ⑧ 条件一字未动时排若干帧不再扫（`search_dirty_` 的清算与同值早退两处各在这一条上有证人）；
///           ⑨ 重扫与「把顶边推走的新输出」落在**同一帧**时只折算一次——扫描的基准读数必须在副本并入
///              之后现取，取早了会把同一次位移既算进扫描结果又算进折算增量（该调用次序在先写 ⑥ 时
///              抓不到，因为那两条动作分处两帧）。
///
///           查询条件命中的内容一律经**真实字节流**投进网格（`row<k>` 与标记串逐行投出），而不是注入
///           网格快照，否则测的是接线假象。本套件全不经绘制，故没有无头窗口与帧缓冲，也不需要
///           `AURORA_BACKEND_HEADLESS`：帧序由 `on_frame()` 手工驱动（未挂父时 `mark_needs_paint()`
///           只是自标脏，不会走空）。界面腿**不**另配时间断言（AGENTS.md §4.4 第 21 条）——200 ms 那条
///           判据落在 `tools/bench` 的第四场景与 `tools/check` 的 B-8 档。

#include <cstddef>
#include <initializer_list>
#include <memory>
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

namespace borealis::test_cases::itest_search_scan {

namespace {

using borealis::grid::Storage;
using borealis::session::Connection;
using borealis::session::ConnectionEvents;
using borealis::session::Session;
using borealis::session::Size;
using borealis::term::UnicodeWidthPolicy;
using borealis::ui::RowSpan;
using borealis::ui::SearchMatches;
using borealis::ui::SearchQuery;
using borealis::ui::TerminalView;

constexpr Size kNominalSize{80U, 24U};
constexpr std::size_t kRows = 24U;

/// @brief 匹配标记：只由显式投出的那几行携带，填充行 `row<k>` 结构上不含它。
/// 查询侧另存一份 UTF-32 形态（`SearchQuery::text` 的坐标空间），两份逐字同值。
constexpr std::string_view kNeedle{"qz7k"};
const std::u32string kNeedleQuery{U"qz7k"};

UnicodeWidthPolicy width_policy;

/// @brief 传输连接替身：只负责把字节投进会话。
///
/// 写出方向在本套件没有观测者（键入链路与上报分流的证人各在 `itest_key_input` 与
/// `itest_mouse_report`），故不留记录成员——留了就是没人读的第二个现场。
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

/// @brief 驱动台：会话 + 替身连接 + 视口控件；@p scrollback 由用例定（⑤ 要历史，⑥ 要溢出）。
class Harness {
  public:
    explicit Harness(std::size_t scrollback)
        : session_(std::make_unique<Session>(own_connection(), kNominalSize, scrollback, width_policy)),
          view_(std::make_shared<TerminalView>(*session_, TerminalView::Appearance{},
                                                TerminalView::InteractionOptions{})) {
        session_->start();
    }

    Harness(const Harness &) = delete;
    auto operator=(const Harness &) -> Harness & = delete;
    Harness(Harness &&) = delete;
    auto operator=(Harness &&) -> Harness & = delete;

    /// @brief 投一行文本：行间以 `\r\n` 分隔，且**最后一行不带换行符**——带了它，第 24 行也会触发一次
    ///        上滚，用例里的行号就得跟着一次隐式位移算。
    auto println(std::string_view text) -> void {
        std::string bytes;
        if (line_open_) {
            bytes = "\r\n";
        }
        bytes.append(text);
        line_open_ = true;
        connection_->deliver(bytes);
    }

    /// @brief 排帧到队列见底：一次 `on_frame` 即一轮「取脏 → 并入副本 → 结算扫描 → 折算位移」。
    auto frame() -> void {
        do {
            view_->on_frame();
        } while (session_->has_damage());
    }

    auto set_query(SearchQuery query) -> void { view_->set_search_query(std::move(query)); }
    auto submit() -> void { view_->submit_search_query(); }
    [[nodiscard]] auto scans() const noexcept -> std::size_t { return view_->search_scan_count(); }
    [[nodiscard]] auto matches() const noexcept -> const SearchMatches * { return view_->search_matches(); }
    [[nodiscard]] auto invalid() const noexcept -> bool { return view_->search_pattern_invalid(); }
    [[nodiscard]] auto pending() const noexcept -> bool { return view_->search_pending_submit(); }

    /// @brief 当前表的行号序列：升序且逐条比对用（条数另由 `count()` 守，越界即在此转红）。
    [[nodiscard]] auto rows() const -> std::vector<std::size_t> {
        std::vector<std::size_t> out;
        if (const SearchMatches *table = matches(); table != nullptr) {
            for (const RowSpan &span : table->spans()) {
                out.push_back(span.row);
            }
        }
        return out;
    }

    /// @brief 只读核对权威网格的现存行数（⑤ 的前提：历史确实存住了，标记行确实不在可见区那 24 行里）。
    [[nodiscard]] auto total_lines() -> std::size_t {
        std::size_t out = 0;
        session_->read(
            [&out](Storage &grid, const term::Cursor &, const term::TermModes &) { out = grid.total_lines(); });
        return out;
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

[[nodiscard]] auto holds(std::initializer_list<std::size_t> values, std::size_t value) -> bool {
    for (const std::size_t item : values) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

/// @brief 投 @p count 行：行号落在 @p needle_rows 的那几行内容是标记串，其余是 `row<行号>`。
auto feed_lines(Harness &h, std::size_t count, std::initializer_list<std::size_t> needle_rows) -> void {
    for (std::size_t row = 0; row < count; ++row) {
        const std::string text = holds(needle_rows, row) ? std::string{kNeedle} : "row" + std::to_string(row);
        h.println(text);
    }
}

}  // namespace

AURORA_TEST_CASE(a_literal_query_scans_once_when_the_frame_settles) {
    Harness h{kRows};
    feed_lines(h, kRows, {5});
    h.frame();
    // 还没打字就没有任何扫描：空条件不产表也不进计数（判据 A2-a 的前半）。
    AURORA_TEST_REQUIRE_EQ(h.scans(), 0U);
    AURORA_TEST_CHECK_NULL(h.matches());

    // 打字四键：条件每键都变、每键都置脏，而扫描只在帧边界结算。
    h.set_query(SearchQuery{.text = U"q"});
    h.set_query(SearchQuery{.text = U"qz"});
    h.set_query(SearchQuery{.text = U"qz7"});
    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    // 结算的是**最后**那份条件，而不是第一次置脏时的那份。
    AURORA_TEST_CHECK_EQ(h.matches()->count(), 1U);
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{5U});
    AURORA_TEST_CHECK_FALSE(h.pending());
}

AURORA_TEST_CASE(a_regex_query_waits_for_submit_and_scans_then) {
    Harness h{kRows};
    feed_lines(h, kRows, {5});
    h.frame();

    h.set_query(SearchQuery{.text = U"row", .regex = true});
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 0U);
    AURORA_TEST_CHECK(h.pending());
    AURORA_TEST_CHECK_NULL(h.matches());

    h.set_query(SearchQuery{.text = U"row[0-9]+", .regex = true});
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 0U);
    AURORA_TEST_CHECK(h.pending());

    h.submit();  // Enter
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);
    AURORA_TEST_CHECK_FALSE(h.pending());
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    // 24 行里 23 行是 `row<k>`，标记行不匹配。这一句同时是「打字那两次没扫」的差分证人：
    // 若打字也扫，计数就是 3 而不是 1。
    AURORA_TEST_CHECK_EQ(h.matches()->count(), kRows - 1U);
}

AURORA_TEST_CASE(an_empty_query_clears_the_table_without_scanning) {
    Harness h{kRows};
    feed_lines(h, kRows, {5});
    h.frame();

    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.scans(), 1U);
    AURORA_TEST_REQUIRE(h.matches() != nullptr);

    // 只换档位而文本未变：正则档不扫，旧表原样留着。
    h.set_query(SearchQuery{.text = kNeedleQuery, .regex = true});
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(h.matches()->count(), 1U);

    // 把文本清空：两档都要在这一帧清掉高亮，而清表不算一次扫描（判据 A2-a）。
    h.set_query(SearchQuery{.regex = true});
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);
    AURORA_TEST_CHECK_NULL(h.matches());
    // 空串显示占位「—」而不是「按 Enter 搜索」。
    AURORA_TEST_CHECK_FALSE(h.pending());
}

AURORA_TEST_CASE(an_invalid_pattern_keeps_the_previous_table_and_says_so) {
    Harness h{kRows};
    feed_lines(h, kRows, {5});
    h.frame();

    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.scans(), 1U);

    h.set_query(SearchQuery{.text = U"[a", .regex = true});
    h.submit();
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 2U);
    AURORA_TEST_CHECK(h.invalid());
    // 「表达式非法」与「0 个匹配」是两档：旧表与行号原样留着（判据 B4）。
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(h.matches()->count(), 1U);
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{5U});
    // 已经按过 Enter，不能再提示「按 Enter 应用」——B5 让位给 B4。
    AURORA_TEST_CHECK_FALSE(h.pending());

    // 标志不粘住：下一次合法提交把它清掉并换新表。
    h.set_query(SearchQuery{.text = U"row", .regex = true});
    h.submit();
    h.frame();
    AURORA_TEST_CHECK_FALSE(h.invalid());
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(h.matches()->count(), kRows - 1U);
}

AURORA_TEST_CASE(the_scan_covers_scrollback_rows_the_visible_mirror_does_not_hold) {
    Harness h{1000U};
    feed_lines(h, 60U, {3});
    h.frame();
    // 前提：历史确实存住了，而标记行在可见区之外（副本只有视口那 24 行，屏上是第 36..59 行）。
    AURORA_TEST_REQUIRE_EQ(h.total_lines(), 60U);

    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    // 「扫副本」那种实现此处结构上给 0 个匹配，故这一句是本条的唯一证人（裁决 7.78⑥ 的执行侧）。
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(h.matches()->count(), 1U);
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{3U});
    AURORA_TEST_REQUIRE_EQ(h.matches()->spans().size(), 1U);
    const RowSpan &span = h.matches()->spans()[0];
    AURORA_TEST_CHECK_EQ(span.first_column, 0U);
    AURORA_TEST_CHECK_EQ(span.last_column, kNeedle.size());
}

AURORA_TEST_CASE(new_output_translates_the_match_rows_without_rescanning) {
    // scrollback 取 0：每次上滚都溢出，`dropped_lines()` 的增量就等于行数位移。
    Harness h{0U};
    feed_lines(h, kRows, {2, 15});
    h.frame();

    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.scans(), 1U);
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{2U, 15U});

    for (std::size_t row = kRows; row < kRows + 5U; ++row) {
        h.println("row" + std::to_string(row));
    }
    h.frame();
    // 新输出把内容顶走时不重扫，只按位移增量折算（与选区跟走同一条路径、同一份快照）。
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);
    // 第 2 行那条已被推出存储顶端而丢弃，第 15 行那条跟着内容前移到第 10 行。
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{10U});

    // 折算后的行号与「按同一条件立刻重扫一次」的结论逐条相符：只断上一句抓不到折算量算错。
    h.submit();
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 2U);
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{10U});
}

AURORA_TEST_CASE(a_rescan_that_lands_with_new_output_translates_exactly_once) {
    // ⑥ 判的是「先扫后折算」两个动作分处两帧；本条判的是它们落在**同一帧**时的先后：扫描吃的是
    // 此刻的权威网格，而基准读数由副本带出，故必须在副本并入之后现取——取早了就会把这一次位移
    // 既算进扫描结果、又算进折算增量（裁决 7.78⑥ 的调用次序，判据文 §5 的「一轮只扫一次」）。
    Harness h{0U};
    feed_lines(h, kRows, {});  // 先把 24 行填满且末行未终结，此时顶边还没动
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.total_lines(), kRows);

    h.set_query(SearchQuery{.text = kNeedleQuery});  // 置脏，但先不排帧
    h.println(kNeedle);                              // 这一行落下即把顶边推走一格
    h.frame();                                       // 同一帧：既重扫，又并新增

    AURORA_TEST_CHECK_EQ(h.scans(), 1U);
    AURORA_TEST_REQUIRE_EQ(h.total_lines(), kRows);
    // scrollback 取 0 时存储里只剩视口那 24 行，最新一行恒在第 23 行——该预期与滚动次数无关。
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(h.matches()->count(), 1U);
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{23U});
}

AURORA_TEST_CASE(flipping_the_case_switch_is_a_real_rescan) {
    Harness h{kRows};
    h.println("QZ7K");
    for (std::size_t row = 1U; row < 5U; ++row) {
        h.println("row" + std::to_string(row));
    }
    h.println(kNeedle);
    for (std::size_t row = 6U; row < kRows; ++row) {
        h.println("row" + std::to_string(row));
    }
    h.frame();

    h.set_query(SearchQuery{.text = kNeedleQuery});  // 缺省即大小写不敏感
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.scans(), 1U);
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{0U, 5U});

    h.set_query(SearchQuery{.text = kNeedleQuery, .case_sensitive = true});
    h.frame();
    // 翻转开关是一次真重扫而不是对既有表做过滤（判据 D4 里两枚开关与 `Enter` 同一条通道）。
    AURORA_TEST_CHECK_EQ(h.scans(), 2U);
    AURORA_TEST_CHECK_EQ(h.rows(), std::vector<std::size_t>{5U});
}

AURORA_TEST_CASE(an_unchanged_query_never_rescans_across_frames) {
    Harness h{kRows};
    feed_lines(h, kRows, {5});
    h.frame();

    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_REQUIRE_EQ(h.scans(), 1U);

    // 连着排帧：脏标志在结算时已清掉，没有第二次扫描。
    h.frame();
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);

    // 交进逐字段相同的条件：同值早退，连脏都不置。
    h.set_query(SearchQuery{.text = kNeedleQuery});
    h.frame();
    AURORA_TEST_CHECK_EQ(h.scans(), 1U);
    AURORA_TEST_REQUIRE(h.matches() != nullptr);
    AURORA_TEST_CHECK_EQ(h.matches()->count(), 1U);
}

}  // namespace borealis::test_cases::itest_search_scan
