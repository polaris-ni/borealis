// ============================================================
// 原始字节留痕环（src/session/raw_tap.cpp）
// ------------------------------------------------------------
// `SPEC.FEAT.CONN.05` 延后子项的采集腿落地件（稿 `codespec/UI_SERIAL_MONITOR.draft.md` D3①／D4①）。
// 本件不认识编码、行尾与协议，只做三件事：按到达序存、按字节逐出、把丢弃数攒起来。
//
// 两处刻意的设计：
//   ⑴ `record` 只做一次 `assign` 拷贝，不取时钟、不格式化、不解析（F4：32 KiB 拷贝 0.604 µs，
//      武装态每批半微秒；F1 那条「锁内/采集期只留值、不做格式化」的既有纪律）。
//   ⑵ 上限按**字节**而不是按条数：F3 说一次入向 run 的上界是 32 KiB，而慢速设备可以 10 字节一批，
//      按条数封顶就等于让设备节奏决定能看多少（D4①）。
// ============================================================

#include "borealis/session/raw_tap.h"

#include <algorithm>
#include <utility>

namespace borealis::session {

RawTap::RawTap(std::size_t max_bytes) : max_bytes_(max_bytes) {
    // 0 上限等于「什么都不留」，与本件的语义不符（留痕环至少容得下一条 run）；钳到 1 而不是
    // 拒绝构造：装配层永远传缺省值，走到这里的只有用例的编排档，钳住它就不必为一条非法入参
    // 再开一档错误码。
    max_bytes_ = std::max<std::size_t>(max_bytes_, 1U);
}

auto RawTap::record(RawDirection direction, std::int64_t timestamp_ms,
                    std::span<const std::byte> bytes) -> void {
    if (bytes.empty()) {
        return;  // 零字节的 run 不进环：行区多一条空行只会被读成「设备发了什么」
    }
    std::lock_guard lock{mutex_};
    if (bytes.size() > max_bytes_) {
        // 单段超上限：整条拒收并计丢弃。截一半留存就是伪造设备节奏（头注的口径 ⑵）。
        dropped_bytes_ += bytes.size();
        ++revision_;
        return;
    }
    auto run = RawRun{};
    run.direction = direction;
    run.timestamp_ms = timestamp_ms;
    run.bytes.assign(bytes.begin(), bytes.end());
    held_bytes_ += run.bytes.size();
    runs_.push_back(std::move(run));
    // 不变量恢复：新条自己已容得下（上一条分支挡掉了超上限），故被逐出的只会是更早的条。
    while (held_bytes_ > max_bytes_ && runs_.size() > 1U) {
        held_bytes_ -= runs_.front().bytes.size();
        dropped_bytes_ += runs_.front().bytes.size();
        runs_.pop_front();
    }
    ++revision_;
}

auto RawTap::snapshot() const -> std::vector<RawRun> {
    std::lock_guard lock{mutex_};
    return {runs_.begin(), runs_.end()};  // 到达序，最旧在前；值拷贝，交出后与环再无关系
}

auto RawTap::clear() -> void {
    std::lock_guard lock{mutex_};
    runs_.clear();
    held_bytes_ = 0;
    dropped_bytes_ = 0;
    ++revision_;
}

auto RawTap::total_bytes() const -> std::size_t {
    std::lock_guard lock{mutex_};
    return held_bytes_;
}

auto RawTap::run_count() const -> std::size_t {
    std::lock_guard lock{mutex_};
    return runs_.size();
}

auto RawTap::dropped_bytes() const -> std::size_t {
    std::lock_guard lock{mutex_};
    return dropped_bytes_;
}

auto RawTap::revision() const -> std::uint64_t {
    std::lock_guard lock{mutex_};
    return revision_;
}

}  // namespace borealis::session
