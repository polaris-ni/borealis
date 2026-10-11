// ============================================================
// 会话实现（src/session/session.cpp）
// ------------------------------------------------------------
// 跨线程协调的唯一落点：读线程在这里把字节变成网格内容与脏行提交，主线程按帧取用。
// 锁的纪律有两条（架构 §3.4）——临界区内不做 IO，出锁后才推队列（主线程的取用顺序是
// 「先取队列、再读网格」，反过来即成反向锁序）。
// ============================================================

#include "borealis/session/session.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "conn/serial_model.h"  // 行尾字节序列的唯一一份表（裁决 7.110 D4① 的「档位表只有一份」）

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
                 std::shared_ptr<const term::WidthPolicy> width_policy, term::TerminalDefaults defaults,
                 term::SessionEncoding encoding, std::optional<term::LineEnding> line_ending)
    : connection_{std::move(connection)},
      terminal_{size.columns, size.rows, scrollback_limit, std::move(width_policy), defaults},
      encoding_{std::move(encoding)},
      line_ending_{line_ending} {
    decoder_ = term::make_session_decoder(encoding_.name);
    // 提示卡片的名字口径在构造期折一次（裁决 7.104 的 D6②）：与上一行的解码器同源，
    // 故「卡片报的腿」与「真正在跑的腿」不可能分叉，填 GB2312 的会话报的是 UTF-8。
    encoding_leg_ = term::resolve_encoding_name(encoding_.name);
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

auto Session::close() -> void {
    connection_->close();
    // 连接侧 join 了读线程之后才停笔：反过来会让「末段 flush」与仍在途的一轮 append 抢同一把
    // io_mutex_，而末段完整性正是 `SPEC.FEAT.CONN.11` 要的那条判据（标签关闭 ⇒ 末段已落盘）。
    disable_logging();
}

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

auto Session::send_bytes(std::span<const std::byte> bytes) -> void { write_out(bytes); }

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
    return decoder_->stats();
}

auto Session::unrepresentable_count() const -> std::size_t {
    const std::lock_guard lock{mutex_};
    return unrepresentable_total_;
}

auto Session::take_unrepresentable_notice() -> std::optional<UnrepresentableNotice> {
    // 取走语义与 BEL / 剪贴板同构（架构 §3.2）：锁内只留存，视图在帧边界取走后进绘制。
    const std::lock_guard lock{mutex_};
    auto notice = std::move(notice_);
    notice_.reset();
    return notice;
}

auto Session::enable_logging(LogSpec spec) -> LogOutcome {
    // 未记录时先答「已在记录中」再建文件：反过来会让一次多余的点按在磁盘上留下一份零字节文件，
    // 而需求那句「取消 ⇒ 什么都不创建」靠的就是「没确认就别碰文件系统」。
    {
        const std::lock_guard lock{mutex_};
        if (log_ && log_->is_logging()) {
            return LogOutcome::AlreadyActive;
        }
    }
    // 构造期做一次建文件 IO，且**不在锁内**（架构 §3.4）：开启动作来自 UI 线程的显式点击，
    // 与 confirm / 正则编译同在那一刻（裁决 7.116⑤），量级是一次 open，不是每帧的 IO。
    auto candidate = std::make_shared<SessionLog>(std::move(spec));
    if (!candidate->is_open()) {
        return LogOutcome::CannotOpen;  // 父目录缺失或权限不足：会话保持未记录，界面不得显示「记录中」
    }
    std::shared_ptr<SessionLog> previous;
    {
        const std::lock_guard lock{mutex_};
        previous = std::move(log_);  // 上一次记录留下的旧件：出锁后随本函数返回析构（停笔 + 关文件）
        log_ = std::move(candidate);
    }
    return LogOutcome::Started;
}

auto Session::disable_logging() -> void {
    std::shared_ptr<SessionLog> target;
    {
        const std::lock_guard lock{mutex_};
        if (!log_ || !log_->is_logging()) {
            return;  // 幂等：未记录（含已因写失败自我禁用）时连末段 flush 都不做
        }
        target = log_;  // 只停笔不销毁：读线程可能正握着同一份副本在 append
    }
    target->stop();  // 出锁才做 IO：末段 flush 是文件系统动作
}

auto Session::logging_enabled() const -> bool {
    const std::lock_guard lock{mutex_};
    return log_ && log_->is_logging();
}

auto Session::take_logging_notice() -> std::optional<std::string> {
    std::shared_ptr<SessionLog> target;
    {
        const std::lock_guard lock{mutex_};
        target = log_;
    }
    // 取走语义与 BEL / 剪贴板 / 不可表示字符同构（架构 §3.2）：锁内只留存，弹窗是主线程的 IO。
    return target ? target->take_write_failure() : std::nullopt;
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

auto Session::set_clipboard_gate(const term::ClipboardGate *gate) noexcept -> void {
    // 取锁只为那个指针本身：读线程在 `feed` 期间现读它，主线程裸写就是数据竞争（裁决 D2① 的
    // 「改档即刻生效」靠的是闸门内部那枚 atomic，而不是这条赋值）。
    const std::lock_guard lock{mutex_};
    terminal_.set_clipboard_gate(gate);
}

auto Session::take_clipboard_reads() -> std::vector<term::ClipboardReadRequest> {
    // 同一套取走语义：锁内只换走队列，读剪贴板（POSIX 腿是一次子进程往返）与询问框都在主线程。
    const std::lock_guard lock{mutex_};
    return terminal_.take_clipboard_reads();
}

auto Session::answer_clipboard_read(std::u32string_view response) -> void {
    // 不持锁、不进 `pending_responses_`：那条缓冲的冲出点是读线程下一次喂入，空闲会话里点了
    // 允许就会永远发不出去。`flush` 内部只在需要上弦提示时才取锁（架构 §3.4）。
    flush(response);
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
    std::shared_ptr<SessionLog> log_target;
    {
        const std::lock_guard lock{mutex_};
        decode_buffer_.clear();
        BufferSink sink{decode_buffer_};
        if (end_of_stream) {
            decoder_->finish(sink);  // 半截序列按替换字符收尾，会话最后一行不凭空消失
        } else {
            decoder_->feed(bytes, sink);
        }
        terminal_.feed(decode_buffer_);
        // 会话日志吃的是**同一份**入向码点（`SPEC.FEAT.CONN.11`，裁决 7.116 D2①）：既不是网格
        // 屏行（没有软换行标志，还原会把远端一行长输出记成多条换行），也不是解码前的字节。
        // 锁内只拷贝、不做 IO（架构 §3.4），落盘在出锁之后同轮进行（D5①）。
        // WHY 这条腿不会跑到主线程上：唯一在主线程喂字节的反例是设置页预览会话（稿 §0 的 F4），
        // 而装配层从不给它注入 `LogSpec`，钩子未注入即无入口（F17）——将来加「预览也记日志」
        // 就是在主线程写盘，别改这里，去改装配层那条注入。
        if (log_ && log_->is_logging()) {
            log_target = log_;  // 取副本出锁再用：UI 线程可能在这之间停笔换件，副本保证不悬垂
            log_pending_.assign(decode_buffer_.begin(), decode_buffer_.end());
        }
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
    if (log_target) {
        log_target->append(log_pending_);  // 排在唤醒之后：一次写盘的耗时不该压在帧调度的前面
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
    // 编码名与策略档都是构造期值、此后只读，故这里取 `encoding_` 不需要持锁；写连接也不在锁内
    // （架构 §3.4：临界区内不做 IO）。
    const auto encoded = term::encode_for_encoding(text, encoding_.name, encoding_.unrepresentable);
    write_out({reinterpret_cast<const std::byte *>(encoded.bytes.data()), encoded.bytes.size()});
    if (encoded.unrepresentable == 0U) {
        return;
    }
    const std::lock_guard lock{mutex_};
    unrepresentable_total_ += encoded.unrepresentable;
    // 一次性提示上弦（裁决 7.104 的 D1①）：本会话对象只上弦一次，取走后也不再上——粘贴分块会让
    // 一次键入产出若干批，每批都上弦就退化成刷屏。累计代价另有一条账（面板第四行读 count）。
    if (!notice_armed_) {
        notice_armed_ = true;
        notice_ = UnrepresentableNotice{
            .count = encoded.unrepresentable, .leg = std::string{encoding_leg_}};
    }
}

auto Session::write_out(std::span<const std::byte> bytes) -> void {
    // 未给行尾档（本地/SSH 两腿的全部既有构造点）与 CR 档都是原样下发：键表产的本来就是一个 CR
    // （裁决 7.110 D11①「`term::encode_key` 的字节表逐字节不动」的另一半）。
    if (!line_ending_.has_value() || *line_ending_ == term::LineEnding::Cr) {
        connection_->write(bytes);
        return;
    }
    const auto target = conn::line_ending_bytes(*line_ending_);
    auto out = std::vector<std::byte>{};
    out.reserve(bytes.size());
    auto folded = false;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto byte = bytes[index];
        // 只折**单独一个 CR**：紧跟 LF 的那个 CR 属于一对 CRLF，原样留着。否则 Windows 剪贴板与
        // bracketed 粘贴里的 CRLF 会被改成两个行尾，而 `term::plan_paste` 明写那两种形态逐字节不动。
        // 折的是**编码后**的字节：0x0D 不会是 UTF-8 的续字节（≥0x80），也不是 GB18030 多字节序列的
        // 任一后续字节（落在 0x30–0x39 / 0x40–0x7E / 0x80–0xFE），故不存在把半个汉字改断的情形。
        const auto paired_with_lf =
            index + 1U < bytes.size() && bytes[index + 1U] == std::byte{0x0A};
        if (byte != std::byte{0x0D} || paired_with_lf) {
            out.push_back(byte);
            continue;
        }
        folded = true;
        for (const char c : target) {
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }
    }
    // 一批里没有可折的 CR 就不换缓冲：绝大多数按键（含全部 CSI/SS3 转义序列）走这条零分配路。
    connection_->write(folded ? std::span<const std::byte>{out.data(), out.size()} : bytes);
}

}  // namespace borealis::session
