// ============================================================
// 会话实现（src/session/session.cpp）
// ------------------------------------------------------------
// 跨线程协调的唯一落点：读线程在这里把字节变成网格内容与脏行提交，主线程按帧取用。
// 锁的纪律有两条（架构 §3.4）——临界区内不做 IO，出锁后才推队列（主线程的取用顺序是
// 「先取队列、再读网格」，反过来即成反向锁序）。
// ============================================================

#include "borealis/session/session.h"

#include <utility>

namespace borealis::session {

namespace {

/// @brief 把解码器的逐码点回调收拢成一段码点缓冲：状态机按批喂入（`Terminal::feed` 的口径）。
class BufferSink final : public term::CodePointSink {
  public:
    explicit BufferSink(std::u32string &buffer) : buffer_{buffer} {}

    auto on_code_point(char32_t cp) -> void override { buffer_.push_back(cp); }

  private:
    std::u32string &buffer_;
};

}  // namespace

Session::Session(std::unique_ptr<Connection> connection, Size size, std::size_t scrollback_limit,
                 std::shared_ptr<const term::WidthPolicy> width_policy, term::TerminalDefaults defaults)
    : connection_{std::move(connection)},
      terminal_{size.columns, size.rows, scrollback_limit, std::move(width_policy), defaults} {
    terminal_.set_response_sink(this);
    // 能力接口在构造期认一次（架构 §7.2）：会话层自此只知道「这条腿有没有重连控制面」，
    // 不知道它是 SSH 还是别的——每次取用再 cast 既无必要也要把类型知识留在热路径上。
    reconnect_control_ = dynamic_cast<ReconnectControl *>(connection_.get());
    // BEL 回调在锁内只登记事件，不 IO（架构 §3.4）；具体落地由装配层每帧取走标记。
    terminal_.set_bell_callback([this]() -> void {
        // 空实现：BEL 触发时不需要在锁内做任何事，主线程会每帧调用 take_bell_triggered() 查询。
    });
}

Session::~Session() {
    // 析构前必须断开：连接的读线程回调持有本对象，连接比会话活得久即悬垂。
    connection_->close();
}

auto Session::start() -> void { connection_->start(*this); }

auto Session::close() -> void { connection_->close(); }

auto Session::set_frame_wake(std::function<void()> wake) -> void {
    const std::lock_guard lock{mutex_};
    frame_wake_ = std::move(wake);
}

auto Session::wake_frame() -> void {
    std::function<void()> wake;
    {
        const std::lock_guard lock{mutex_};
        wake = frame_wake_;  // 取副本出锁再调：句柄是跨线程 post，锁内不做 IO（架构 §3.4）
    }
    if (wake) {
        wake();
    }
}

auto Session::send_text(std::u32string_view text) -> void { flush(text); }

auto Session::send_bytes(std::span<const std::byte> bytes) -> void { connection_->write(bytes); }

auto Session::resize(Size size) -> void {
    if (size.columns == 0 || size.rows == 0) {
        return;  // 状态机会自行忽略无效尺寸，但整屏脏与连接侧下发是副作用，不能跟着做一遍
    }
    {
        const std::lock_guard lock{mutex_};
        terminal_.resize(size.columns, size.rows);
        // 尺寸变更必是整屏脏；由本函数当场落地成提交，否则要等读线程下次喂入才通知主线程。
        terminal_.clear_full_screen_dirty();
    }
    damage_queue_.push_full_screen();
    wake_frame();
    connection_->resize(size);
}

auto Session::alive() const -> bool { return connection_->alive(); }

auto Session::reconnect_progress() const -> std::optional<ReconnectProgress> {
    const std::lock_guard lock{mutex_};
    return reconnect_progress_;
}

auto Session::reconnect_control() const noexcept -> ReconnectControl * { return reconnect_control_; }

auto Session::on_reconnect_progress(const ReconnectProgress &progress) -> void {
    {
        const std::lock_guard lock{mutex_};
        reconnect_progress_ = progress;
    }
    // 状态变更即唤醒：本事件没有网格提交，走不成「有提交才排帧」那条路（见 on_closed 同处注释）。
    wake_frame();
}

auto Session::decode_stats() const -> term::DecodeStats {
    const std::lock_guard lock{mutex_};
    return decoder_.stats();
}

auto Session::parse_stats() const -> vt::ParseStats {
    const std::lock_guard lock{mutex_};
    return terminal_.parse_stats();
}

auto Session::osc_state() const -> term::OscState {
    const std::lock_guard lock{mutex_};
    return terminal_.osc_state();
}

auto Session::hyperlink_target(grid::HyperlinkId link_id) const -> std::optional<std::u32string> {
    const std::lock_guard lock{mutex_};
    return terminal_.hyperlink_target(link_id);
}

auto Session::take_clipboard_write() -> std::optional<std::u32string> {
    // 与查询应答同一套「锁内留存、锁外 IO」：这里取出的是状态机在 feed 期间攒下的待写文本。
    const std::lock_guard lock{mutex_};
    return terminal_.take_clipboard_write();
}

auto Session::take_bell_triggered() -> bool {
    const std::lock_guard lock{mutex_};
    return terminal_.take_bell_triggered();
}

auto Session::on_bytes(std::span<const std::byte> bytes) -> void { ingest(bytes, false); }

auto Session::on_closed() -> void {
    ingest({}, true);
    // 连接终止是一次**状态变更**而不只是一轮批量输入：`SPEC.FEAT.WS.05` 的 dead-session 浮层
    // 判据正是这个边沿，若只沿「有提交才唤醒」那条路径排帧，进程干净退出（无残留半截序列且最后一次
    // flush 已排在上一帧的边界之后）就成了无提交的一轮，浮层要等到下一次别的唤醒才显形——那已经
    // 是「过了一晚上终于看到进程早挂了」的形态。头注那句「一轮没有产出任何提交就不唤醒」讲的是
    // 批量输入这一路径，本条是其例外：连接生死本身就是要让主线程看一眼的变化。
    wake_frame();
}

auto Session::ingest(std::span<const std::byte> bytes, bool end_of_stream) -> void {
    std::vector<Damage> damage;
    std::u32string responses;
    {
        const std::lock_guard lock{mutex_};
        decode_buffer_.clear();
        BufferSink sink{decode_buffer_};
        if (end_of_stream) {
            decoder_.finish(sink);  // 半截序列按替换字符收尾，会话最后一行不凭空消失
        } else {
            decoder_.feed(bytes, sink);
        }
        terminal_.feed(decode_buffer_);
        damage = collect_damage();
        responses.swap(pending_responses_);
    }
    if (!responses.empty()) {
        flush(responses);
    }
    for (const auto &entry : damage) {
        if (entry.full_screen) {
            damage_queue_.push_full_screen();
        } else {
            damage_queue_.push_rows(entry.first_row, entry.last_row);
        }
    }
    if (!damage.empty()) {
        wake_frame();  // 一轮批量输入只唤醒一次，且必须在队列已成型之后（否则唤醒的那帧取不到提交）
    }
}

auto Session::collect_damage() -> std::vector<Damage> {
    if (terminal_.full_screen_dirty()) {
        terminal_.clear_full_screen_dirty();
        // 整屏位移后行号与内容的对应关系变了，行级增量表达不出「平移了一屏」（架构 §3.4）。
        return {Damage{0, 0, true}};
    }
    auto &grid = terminal_.active_grid();
    const std::size_t rows = grid.visible_rows();
    std::vector<Damage> damage;
    std::size_t run_left = 0;
    std::size_t run_right = 0;
    bool in_run = false;
    for (std::size_t row = 0; row < rows; ++row) {
        if (grid.visible_line(row).dirty()) {
            run_left = in_run ? run_left : row;
            run_right = row;
            in_run = true;
            continue;
        }
        if (in_run) {
            damage.push_back(Damage{run_left, run_right, false});
            in_run = false;
        }
    }
    if (in_run) {
        damage.push_back(Damage{run_left, run_right, false});
    }
    return damage;
}

auto Session::on_response(std::u32string_view response) -> void {
    // 只在持锁的 feed 上下文里被回调，故这里直接追加，不再取锁。
    pending_responses_.append(response);
}

auto Session::flush(std::u32string_view text) -> void {
    // TODO(SPEC.FEAT.TERM.09): 非 UTF-8 会话（串口 GB18030，裁决 7.6）的发送方向编码与「不可
    // 表示字符」可配策略（默认替换 + 一次性提示）随串口族落地；当前只有本地/SSH 的默认编码。
    std::string bytes;
    bytes.reserve(text.size());
    static_cast<void>(term::encode_utf8(text, bytes));  // 不可表示码点计数在发送策略落地后才有人消费（上面 TODO）
    connection_->write({reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()});
}

}  // namespace borealis::session
