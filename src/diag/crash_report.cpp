#include "borealis/diag/crash_report.h"

// 版本号由 `src/CMakeLists.txt` 以编译定义注入；独立编译（如工具、文档摘录）时回落 unknown。
#ifndef BOREALIS_VERSION
#define BOREALIS_VERSION "unknown"
#endif

namespace borealis::diag {
namespace {

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "crash facts publication must stay lock-free for the signal context");

void emit_text(CrashEmitter emit, void *context, const char *text) noexcept {
    std::size_t size = 0;
    while (text[size] != '\0') {
        ++size;
    }
    emit(context, text, size);
}

void emit_u64(CrashEmitter emit, void *context, std::uint64_t value) noexcept {
    // 最多 20 位十进制（UINT64_MAX）；两个缓冲都在栈上，信号上下文里不碰堆。
    char reversed[20] = {};
    std::size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    char text[20] = {};
    for (std::size_t i = 0; i < count; ++i) {
        text[i] = reversed[count - 1U - i];
    }
    emit(context, text, count);
}

void emit_line_u64(CrashEmitter emit, void *context, const char *key, std::uint64_t value) noexcept {
    emit_text(emit, context, key);
    emit_u64(emit, context, value);
    emit_text(emit, context, "\n");
}

[[nodiscard]] auto kind_name(conn::ConnectionType kind) noexcept -> const char * {
    switch (kind) {
        case conn::ConnectionType::Local:
            return "local";
        case conn::ConnectionType::Ssh:
            return "ssh";
        case conn::ConnectionType::Serial:
            return "serial";
        case conn::ConnectionType::Telnet:
            return "telnet";
    }
    return "unknown";
}

}  // namespace

auto product_version() noexcept -> const char * { return BOREALIS_VERSION; }

void CrashFactsStore::publish(const CrashFactsSnapshot &snapshot) noexcept {
    const std::uint32_t next = 1U - published_.load(std::memory_order_relaxed);
    CrashFactsSnapshot &target = buffers_[next];
    target = snapshot;
    if (target.session_count > kMaxCrashSessions) {
        target.session_count = kMaxCrashSessions;
    }
    published_.store(next, std::memory_order_release);
}

auto CrashFactsStore::latest() const noexcept -> CrashFactsSnapshot {
    return buffers_[published_.load(std::memory_order_acquire)];
}

void emit_crash_report(CrashEmitter emit, void *context, const CrashFactsSnapshot &facts,
                       const char *trigger_name, std::uint64_t trigger_code,
                       std::uint64_t crash_epoch) noexcept {
    if (emit == nullptr) {
        return;
    }
    emit_text(emit, context, "borealis crash report v1\n");
    emit_text(emit, context, "version=");
    emit_text(emit, context, product_version());
    emit_text(emit, context, "\n");
    emit_text(emit, context, "trigger=");
    emit_text(emit, context, trigger_name == nullptr ? "unknown" : trigger_name);
    emit_text(emit, context, "\n");
    emit_line_u64(emit, context, "trigger_code=", trigger_code);
    emit_line_u64(emit, context, "crashed_at_epoch=", crash_epoch);
    emit_line_u64(emit, context, "last_publish_epoch=", facts.last_publish_epoch);
    emit_line_u64(emit, context, "windows=", facts.windows);
    emit_line_u64(emit, context, "tabs=", facts.tabs);
    emit_line_u64(emit, context, "panes=", facts.panes);
    emit_line_u64(emit, context, "sessions=", static_cast<std::uint64_t>(facts.session_count));
    const std::size_t count =
        facts.session_count < kMaxCrashSessions ? facts.session_count : kMaxCrashSessions;
    for (std::size_t i = 0; i < count; ++i) {
        const CrashSessionFact &fact = facts.sessions[i];
        emit_text(emit, context, "session tab=");
        emit_u64(emit, context, fact.tab);
        emit_text(emit, context, " pane=");
        emit_u64(emit, context, fact.pane);
        emit_text(emit, context, " kind=");
        emit_text(emit, context, kind_name(fact.kind));
        emit_text(emit, context, " alive=");
        emit_text(emit, context, fact.alive ? "1" : "0");
        emit_text(emit, context, "\n");
    }
}

}  // namespace borealis::diag
