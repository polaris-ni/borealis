#include "borealis/session/bell.h"

#include "borealis/config/settings.h"

namespace borealis::session {

auto BellPolicy::decide(config::BellMode mode, std::chrono::steady_clock::time_point now) -> BellAction {
    switch (mode) {
        case config::BellMode::Off:
            return BellAction::None;
        case config::BellMode::Visual:
            return BellAction::Badge;
        case config::BellMode::Audible:
            break;
    }
    // 锚点只在真响铃时推进：被节流的那几次 BEL 不该把间隔越推越远（否则连响十次之后，
    // 最后一次之后的 200 ms 内仍是静音，用户的直觉是「上一次响铃之后」。）
    if (last_tone_.has_value() && (now - last_tone_.value()) < min_tone_interval_) {
        return BellAction::Badge;
    }
    last_tone_ = now;
    return BellAction::BadgeAndTone;
}

}  // namespace borealis::session
