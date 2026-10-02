// ============================================================
// 上屏层吞吐基准（tools/bench/render_throughput.cpp）
// ------------------------------------------------------------
// `SPEC.NF.PERF.02` 的验收手段，形态由裁决 7.23③ 定死：HeadlessSurface + 灌注回放，量三条
// ——滚动一帧的成本、全屏重绘一帧的成本、`cat` 10 MB 期间的帧时分布。
//
// 度量主体是**上屏层自身的每帧成本**（脏行过滤、run 切分、色合成、光标三段式），不是对端的
// 产出速度：无头后端没有 vsync，故帧率取「帧时倒数」而非事件循环的真实节拍。于是本程序给的
// 是「这层最多能跑多少帧」的上界，门禁据此判「45 fps 够不够得着」与「相对基线是否劣化」。
// idle 跳帧（框架在无脏时整帧跳过）不进样本，否则空转帧会把帧时拖向 0。
//
// 本文件不是测试用例（`AGENTS.md` §4.4 第 18 条禁测试自定义 `main()`；基准要读命令行、要出
// JSON，不是断言体）。回归门禁在 `tools/check/check_perf_gates.ps1`，基线在
// `tools/check/perf_baseline.json`；分工是「本程序只出数，判红绿的是脚本」。
// ============================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "borealis/config/settings.h"
#include "borealis/grid/storage.h"
#include "borealis/session/connection.h"
#include "borealis/session/session.h"
#include "borealis/term/width.h"

#include "aurora/aurora.h"
#include "aurora/core/log.h"
#include "aurora/event/focus.h"
// 私有头按相对路径取用（裁决 D1①：类声明不进 `include/borealis/`，故不给 tools/ 开 `src/` 含
// 目录），与 `tests/integration/itest_render_viewport.cpp` 同形态。
#include "../../src/ui/terminal_view.h"

namespace {

namespace au = aurora;
namespace cfg = borealis::config;
namespace grid = borealis::grid;
namespace sess = borealis::session;
namespace term = borealis::term;
namespace ui = borealis::ui;

constexpr std::size_t kDefaultFeedBytes = 10U * 1024U * 1024U;  ///< `SPEC.NF.PERF.02` 的 10 MB。
constexpr int kDefaultScrollFrames = 400;
constexpr int kDefaultRedrawRuns = 9;
constexpr int kScrollWarmupFrames = 40;
constexpr std::size_t kFeedChunkBytes = 64U * 1024U;

/// @brief 默认窗口尺寸（`src/main.cpp` 的 `WindowOptions::size`）：需求原文的「默认窗口尺寸」。
constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 640;

term::UnicodeWidthPolicy g_width_policy;

/// @brief 灌注用连接替身：只把外部投来的字节转给会话，不触达任何真实传输层。
///
/// 与集成用例的替身同形，差别是这里的 `deliver` 由**后台线程**调用（真机上 `on_bytes` 就发生在
/// 读线程），主线程只排帧——帧时样本里因此不含解码与状态机成本，正是裁决 7.23③ 要单独锁的那层。
class FeedConnection final : public sess::Connection {
  public:
    auto start(sess::ConnectionEvents &events) -> void override {
        events_ = &events;
        alive_ = true;
    }

    auto write(std::span<const std::byte> /*bytes*/) -> void override {}

    auto resize(sess::Size /*size*/) -> void override {}

    auto close() -> void override { alive_ = false; }

    [[nodiscard]] auto alive() const noexcept -> bool override { return alive_; }

    /// @brief 把一段原始字节当作「对端已发出」交给会话。
    auto deliver(std::string_view bytes) -> void {
        std::vector<std::byte> raw;
        raw.reserve(bytes.size());
        for (const char c : bytes) {
            raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
        events_->on_bytes(raw);
    }

  private:
    sess::ConnectionEvents *events_ = nullptr;
    bool alive_ = false;
};

/// @brief 行内容生成器：固定种子，故两次跑出的字节流逐位相同（基线才可复现的前提）。
///
/// 素材必须混四类，否则测到的只是最快路径：纯文本行走 run 合并，带 SGR 的行切出色带与多 run，
/// 粗体下划线行走装饰笔形，CJK 行走双宽占位与缺字回退（后三者都是 `layout_row` + `paint_row`
/// 的成本项）。
class LineGen {
  public:
    /// @brief 造一行的字节流（含结尾的 CR LF），内容宽度按 @p columns 截齐。
    [[nodiscard]] auto next_row(std::size_t columns) -> std::string {
        state_ = state_ * 1103515245U + 12345U;
        const std::uint32_t kind = (state_ >> 16U) % 10U;
        const std::size_t width = columns > 8U ? columns - 8U : 8U;
        std::string line;
        line.reserve(columns + 8U);
        if (kind == 0U) {
            line += "\x1b[38;5;210m";  // 256 色档：走色合成立方体而非表色
            append_fill(&line, width, '+');
            line += "\x1b[0m";
        } else if (kind == 1U) {
            line += "\x1b[1m\x1b[4m";  // 粗体 + 单下划线：bold-is-bright 与装饰线笔形
            append_fill(&line, width, '#');
            line += "\x1b[0m";
        } else if (kind == 2U) {
            // CJK-LITERAL: 上屏素材 - 双宽占位与缺字回退链是每帧成本的一部分，纯 ASCII 行测不到
            line += "吞吐基准 网格对齐 ░▒▓ 回看";  // /utf-8 下即 UTF-8 字节串
            append_fill(&line, width, '.');
        } else {
            append_fill(&line, width, static_cast<char>('a' + (state_ % 26U)));
        }
        line += "\r\n";
        return line;
    }

  private:
    auto append_fill(std::string *line, std::size_t width, char fill) -> void {
        std::size_t written = 0;
        std::size_t index = 0;
        while (written < width) {
            const std::string word = std::string(1U, fill) + std::to_string(index % 10U) + " ";
            const std::size_t take = std::min(word.size(), width - written);
            line->append(word, 0U, take);
            written += take;
            ++index;
        }
    }

    std::uint32_t state_ = 20261002U;
};

using Clock = std::chrono::steady_clock;

[[nodiscard]] auto elapsed_ms(Clock::time_point from) -> double {
    return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

/// @brief 一组帧时样本的统计量。
struct FrameStats {
    std::size_t frames = 0;
    double mean_ms = 0.0;
    double p95_ms = 0.0;
    double max_ms = 0.0;
    double fps = 0.0;     ///< 1000 / mean_ms
    double wall_ms = 0.0;  ///< 本轮墙钟总时长（含喂字节侧的等待）
};

[[nodiscard]] auto summarize(std::vector<double> samples, double wall_ms) -> FrameStats {
    FrameStats out;
    out.wall_ms = wall_ms;
    out.frames = samples.size();
    if (samples.empty()) {
        return out;
    }
    double sum = 0.0;
    for (const double value : samples) {
        sum += value;
    }
    out.mean_ms = sum / static_cast<double>(samples.size());
    std::sort(samples.begin(), samples.end());
    const std::size_t p95_index = std::min(samples.size() - 1U, samples.size() * 95U / 100U);
    out.p95_ms = samples[p95_index];
    out.max_ms = samples.back();
    out.fps = out.mean_ms > 0.0 ? 1000.0 / out.mean_ms : 0.0;
    return out;
}

[[nodiscard]] auto median_of(std::vector<double> samples) -> double {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t mid = samples.size() / 2U;
    return samples.size() % 2U == 0U ? 0.5 * (samples[mid - 1U] + samples[mid]) : samples[mid];
}

[[nodiscard]] auto fmt(int precision, double value) -> std::string {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

/// @brief 驱动台：默认配置装配（Dracula、14pt、4 dp 内边距），无头窗口 + 视口控件 + 会话。
///
/// 用 `cfg::Settings{}` 而非手搭调色板：基准要量的是用户实际跑的那套参数，手搭值会让基线对不上
/// 任何真实场景。焦点按 `src/main.cpp` 的做法显式给到视口，于是光标走聚焦的块形三段式而非失焦
/// 描边——后者更便宜，测出来的帧时不代表真实成本。
class Rig {
  public:
    Rig()
        : window_(make_window()),
          feed_owned_(std::make_unique<FeedConnection>()),
          feed_(feed_owned_.get()),
          session_(std::move(feed_owned_), sess::Size{80U, 24U}, settings_.terminal.scrollback_limit,
                   g_width_policy),
          view_(std::make_shared<ui::TerminalView>(
              session_, settings_.appearance.palette,
              au::Font{.family = settings_.appearance.font_family,
                       .size_pt = static_cast<float>(settings_.appearance.font_size_pt),
                       .weight = 400},
              settings_.appearance.viewport_padding_dp,
              std::chrono::milliseconds{settings_.appearance.cursor_blink_period_ms})),
          root_(std::static_pointer_cast<au::Widget>(view_)) {
        session_.start();
        focus_.set_root(&root_.widget());
        focus_.set_focus(view_.get());
        (void)view_->on_frame();
        (void)window_.present_root(root_);  // 首帧做布局，行列数在此由控件派生并下发
        session_.read(
            [this](grid::Storage &grid, const term::Cursor &, const term::TermModes &) -> void {
                rows_ = grid.visible_rows();
                columns_ = grid.columns();
            });
    }

    Rig(const Rig &) = delete;
    auto operator=(const Rig &) -> Rig & = delete;

    /// @brief 排一帧并计时；无脏时框架整帧跳过，这类帧返回负值让调用方剔除。
    [[nodiscard]] auto render_frame() -> double {
        const auto start = Clock::now();
        view_->on_frame();
        (void)window_.present_root(root_);
        const double ms = elapsed_ms(start);
        return window_.is_idle_frame() ? -1.0 : ms;
    }

    /// @brief 排到队列见底；样本只收非 idle 帧。
    auto render_until_quiet(std::vector<double> *samples) -> void {
        do {
            const double ms = render_frame();
            if (ms >= 0.0 && samples != nullptr) {
                samples->push_back(ms);
            }
        } while (session_.has_damage());
    }

    auto deliver(std::string_view bytes) -> void { feed_->deliver(bytes); }

    [[nodiscard]] auto has_damage() const -> bool { return session_.has_damage(); }

    [[nodiscard]] auto rows() const noexcept -> std::size_t { return rows_; }
    [[nodiscard]] auto columns() const noexcept -> std::size_t { return columns_; }

    /// @brief 强制下一帧全量重排重绘（框架测试 seam），即「整屏重画一次」的成本。
    auto force_full_redraw() -> void { window_.force_full_redraw(); }

  private:
    [[nodiscard]] static auto make_window() -> au::Window {
        auto surface = std::make_unique<au::HeadlessSurface>();
        (void)surface->begin_frame(kWindowWidth, kWindowHeight);
        return au::Window{std::move(surface)};
    }

    cfg::Settings settings_{};
    au::Window window_;
    std::unique_ptr<FeedConnection> feed_owned_;
    FeedConnection *feed_ = nullptr;  ///< 非拥有；所有权已随 `feed_owned_` 交给会话。
    sess::Session session_;
    std::shared_ptr<ui::TerminalView> view_;
    au::Node root_;
    au::FocusManager focus_;
    std::size_t rows_ = 0;
    std::size_t columns_ = 0;
};

/// @brief 滚动场景：每帧喂一行，测「一行进来要画多久」。
///
/// 这是输入响应侧的下界成本（脏区只有一行），单独给数是因为它与 `cat` 场景的差距正是
/// 「脏行过滤」在起作用——两者同涨即说明帧成本的整体水位上移，而非某一层的局部回归。
[[nodiscard]] auto bench_scroll(Rig &rig, int frames, LineGen &gen) -> FrameStats {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    const auto wall = Clock::now();
    for (int i = 0; i < frames + kScrollWarmupFrames; ++i) {
        rig.deliver(gen.next_row(rig.columns()));
        const double ms = rig.render_frame();
        if (i >= kScrollWarmupFrames && ms >= 0.0) {
            samples.push_back(ms);
        }
    }
    return summarize(std::move(samples), elapsed_ms(wall));
}

/// @brief 全屏重绘场景：清屏 + 写满整屏 + `force_full_redraw`，测这一帧的耗时（取中位数）。
///
/// 对应需求原文的「全屏重绘（如 `vim` 首帧）≤ 100 ms」：一次整屏换源的可见代价，从投字节到
/// 画面见底，含队列排空所需的全部帧。
[[nodiscard]] auto bench_full_redraw(Rig &rig, int runs, LineGen &gen) -> double {
    std::vector<double> runs_ms;
    runs_ms.reserve(static_cast<std::size_t>(runs));
    for (int run = 0; run < runs; ++run) {
        std::string screen;
        screen += "\x1b[2J\x1b[H";
        for (std::size_t row = 0; row < rig.rows(); ++row) {
            screen += gen.next_row(rig.columns());
        }
        const auto start = Clock::now();
        rig.deliver(screen);
        rig.force_full_redraw();
        std::vector<double> frames;
        rig.render_until_quiet(&frames);
        runs_ms.push_back(elapsed_ms(start));
    }
    return median_of(std::move(runs_ms));
}

/// @brief `cat` 场景的产出：帧时分布 + 实际灌入字节数（吞吐的分子）。
struct CatResult {
    FrameStats stats;
    std::size_t bytes = 0;
};

/// @brief `cat` 场景：后台线程以最大速率灌 @p total_bytes，主线程只排帧，测帧时分布。
///
/// 灌完即停，主线程排到队列见底；墙钟时长给出吞吐（MB/s），帧时样本给出「画得动多少帧」的
/// fps（均值倒数）。「不掉帧」的判据取 p95 帧时不越过 45 fps 的预算（1000/45 ≈ 22.2 ms）而非
/// 平均值——均值好看而周期性长帧照样掉帧。
[[nodiscard]] auto bench_cat(Rig &rig, std::size_t total_bytes, LineGen &gen) -> CatResult {
    CatResult result;
    std::vector<double> samples;
    std::atomic<bool> done{false};
    std::size_t delivered = 0;

    std::thread producer([&rig, &gen, &done, &delivered, total_bytes]() -> void {
        std::string chunk;
        chunk.reserve(kFeedChunkBytes + 256U);
        while (delivered < total_bytes) {
            while (chunk.size() < kFeedChunkBytes) {
                chunk += gen.next_row(rig.columns());
            }
            const std::size_t take = std::min(chunk.size(), total_bytes - delivered);
            rig.deliver(std::string_view{chunk}.substr(0U, take));
            chunk.erase(0U, take);
            delivered += take;
        }
        done.store(true, std::memory_order_release);
    });

    const auto wall = Clock::now();
    while (!done.load(std::memory_order_acquire) || rig.has_damage()) {
        // 无脏就不排帧：生产上是「提交入队才唤醒一帧」，轮询排帧会和灌字节的线程抢会话锁，
        // 把被测的灌入侧拖慢，测出来的帧时就不只代表上屏层了。
        if (!rig.has_damage()) {
            std::this_thread::yield();
            continue;
        }
        const double ms = rig.render_frame();
        if (ms >= 0.0) {
            samples.push_back(ms);
        }
    }
    producer.join();
    result.bytes = delivered;
    result.stats = summarize(std::move(samples), elapsed_ms(wall));
    return result;
}
struct Options {
    std::size_t feed_bytes = kDefaultFeedBytes;
    int scroll_frames = kDefaultScrollFrames;
    int redraw_runs = kDefaultRedrawRuns;
    std::string json_path;
    bool want_help = false;
};

/// @brief 解析结果：失败即返回 false 并给出退出码。
[[nodiscard]] auto parse_args(int argc, char **argv, Options *opts) -> bool {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value_of = [&arg](std::string_view key) -> std::string_view {
            return arg.starts_with(key) ? arg.substr(key.size()) : std::string_view{};
        };
        if (const auto v = value_of("--bytes="); !v.empty()) {
            opts->feed_bytes = static_cast<std::size_t>(std::stoull(std::string{v}));
        } else if (const auto v = value_of("--frames="); !v.empty()) {
            opts->scroll_frames = std::stoi(std::string{v});
        } else if (const auto v = value_of("--runs="); !v.empty()) {
            opts->redraw_runs = std::stoi(std::string{v});
        } else if (const auto v = value_of("--json="); !v.empty()) {
            opts->json_path = std::string{v};
        } else if (arg == "--help") {
            opts->want_help = true;
            return true;
        } else {
            AURORA_LOG_ERROR("bench", "unknown argument: ", arg);
            return false;
        }
    }
    return true;
}

auto write_json(const std::string &path, const Rig &rig, const FrameStats &scroll, double full_redraw_ms,
                const CatResult &cat, double cat_mb_per_s) -> void {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        AURORA_LOG_ERROR("bench", "cannot open json output path");
        return;
    }
    const FrameStats &stats = cat.stats;
    file << "{\n"
         << "  \"scenario\": \"borealis_render_throughput\",\n"
         << "  \"backend\": \"headless_software_painter\",\n"
         << "  \"window_dp\": [" << kWindowWidth << ", " << kWindowHeight << "],\n"
         << "  \"grid\": [" << rig.rows() << ", " << rig.columns() << "],\n"
         << "  \"feed_bytes\": " << cat.bytes << ",\n"
         << "  \"metrics\": {\n"
         << "    \"scroll_frame_ms_mean\": " << fmt(6, scroll.mean_ms) << ",\n"
         << "    \"scroll_frame_ms_p95\": " << fmt(6, scroll.p95_ms) << ",\n"
         << "    \"scroll_fps\": " << fmt(3, scroll.fps) << ",\n"
         << "    \"full_redraw_ms\": " << fmt(3, full_redraw_ms) << ",\n"
         << "    \"cat_frame_ms_mean\": " << fmt(6, stats.mean_ms) << ",\n"
         << "    \"cat_frame_ms_p95\": " << fmt(6, stats.p95_ms) << ",\n"
         << "    \"cat_fps\": " << fmt(3, stats.fps) << ",\n"
         << "    \"cat_mb_per_s\": " << fmt(3, cat_mb_per_s) << ",\n"
         << "    \"cat_frames\": " << stats.frames << ",\n"
         << "    \"cat_wall_ms\": " << fmt(3, stats.wall_ms) << "\n"
         << "  }\n"
         << "}\n";
}

auto print_help() -> void {
    AURORA_LOG_RAW("bench",
                   "usage: borealis_bench [--bytes=N] [--frames=N] [--runs=N] [--json=PATH]\n"
                   "  render throughput benchmark for SPEC.NF.PERF.02 (headless software painter)\n"
                   "  prints metrics only; pass/fail is tools/check/check_perf_gates.ps1\n");
}

}  // namespace

auto main(int argc, char **argv) -> int {
    Options opts;
    if (!parse_args(argc, argv, &opts)) {
        print_help();
        return 2;
    }
    if (opts.want_help) {
        print_help();
        return 0;
    }

    LineGen gen;
    Rig rig;
    if (rig.rows() == 0U || rig.columns() == 0U) {
        AURORA_LOG_ERROR("bench", "grid not sized by first layout; nothing to measure");
        return 1;
    }

    const FrameStats scroll = bench_scroll(rig, opts.scroll_frames, gen);
    const double full_redraw_ms = bench_full_redraw(rig, opts.redraw_runs, gen);
    const CatResult cat = bench_cat(rig, opts.feed_bytes, gen);
    const double cat_mb_per_s = cat.stats.wall_ms > 0.0
                                    ? (static_cast<double>(cat.bytes) / (1024.0 * 1024.0)) /
                                          (cat.stats.wall_ms / 1000.0)
                                    : 0.0;

    AURORA_LOG_RAW("bench", "borealis render throughput benchmark (SPEC.NF.PERF.02)\n");
    AURORA_LOG_RAW("bench", "| window | ", kWindowWidth, " x ", kWindowHeight, " dp | grid | ", rig.rows(),
                   " rows x ", rig.columns(), " cols |\n");
    AURORA_LOG_RAW("bench", "| scroll frame (", opts.scroll_frames, " frames) | ", fmt(3, scroll.mean_ms),
                   " ms mean | ", fmt(3, scroll.p95_ms), " ms p95 | ", fmt(1, scroll.fps), " fps |\n");
    AURORA_LOG_RAW("bench", "| full redraw (", opts.redraw_runs, " runs) | ", fmt(3, full_redraw_ms),
                   " ms median |\n");
    AURORA_LOG_RAW("bench", "| cat ", fmt(1, static_cast<double>(opts.feed_bytes) / (1024.0 * 1024.0)),
                   " MB | ", fmt(1, cat_mb_per_s), " MB/s | ", cat.stats.frames, " frames | ",
                   fmt(3, cat.stats.mean_ms), " ms mean | ", fmt(3, cat.stats.p95_ms), " ms p95 | ",
                   fmt(1, cat.stats.fps), " fps |\n");
    AURORA_LOG_RAW("bench",
                   "(benchmark only - not a CTest assertion; idle frames excluded; timings carry "
                   "environment jitter, gate on relative baseline via tools/check/check_perf_gates.ps1)\n");

    if (!opts.json_path.empty()) {
        write_json(opts.json_path, rig, scroll, full_redraw_ms, cat, cat_mb_per_s);
        AURORA_LOG_RAW("bench", "json written: ", opts.json_path, "\n");
    }
    return 0;
}
