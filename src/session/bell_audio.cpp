// ============================================================
// 可听铃声落地实现（src/session/bell_audio.cpp）
// ------------------------------------------------------------
// 全仓唯一触达 `aurora::AudioContext` 的编译单元：图、设备与样本都在这一文件，框架头不出
// 这里（架构 §2.3）。样本形态与节流理由见裁决 7.159。
// ============================================================

#include "session/bell_audio.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "aurora/core/log.h"
#include "aurora/media/audio.h"

namespace borealis::session {

namespace {

constexpr int kToneSampleRate = 48000;
constexpr double kToneSeconds = 0.12;
constexpr double kToneFrequencyHz = 880.0;
constexpr double kToneAmplitude = 0.22;
constexpr double kToneAttackSeconds = 0.004;
constexpr double kToneDecaySeconds = 0.035;
constexpr double kToneReleaseSeconds = 0.012;
constexpr double kPi = 3.14159265358979323846;

/// @brief 预烘 16-bit PCM 单声道铃声：4 ms 起音 + 指数衰减 + 12 ms 收尾淡出。
///
/// 收尾必须回到零：硬切在设备侧就是一声爆音；指数衰减单独用也会在 120 ms 处留一个
/// 可闻的台阶。起音的 4 ms 同理，避免波形从半周期处突然开始。
[[nodiscard]] auto make_tone() -> std::vector<std::int16_t> {
    const auto frames = static_cast<std::size_t>(kToneSampleRate * kToneSeconds);
    auto samples = std::vector<std::int16_t>{};
    samples.reserve(frames);
    for (std::size_t index = 0; index < frames; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(kToneSampleRate);
        const double attack = std::min(1.0, t / kToneAttackSeconds);
        const double decay = std::exp(-t / kToneDecaySeconds);
        const double release = std::min(1.0, std::max(0.0, (kToneSeconds - t) / kToneReleaseSeconds));
        const double envelope = kToneAmplitude * attack * decay * release;
        const double value = std::sin(2.0 * kPi * kToneFrequencyHz * t) * envelope;
        samples.push_back(static_cast<std::int16_t>(std::lround(value * 32767.0)));
    }
    return samples;
}

}  // namespace

struct BellAudio::Impl {
    explicit Impl(std::unique_ptr<aurora::AudioDeviceBackend> backend)
        : context(std::move(backend)), source(context.create_stream_source()), tone(make_tone()) {
        if (context.silent()) {
            // 缺省构建（未开 AURORA_ENABLE_AUDIO）与「有后端但设备起不来」同走这一档：
            // 用户选了可听档却听不到，必须留一条可诊断的痕，但只留一次。
            AURORA_LOG_WARN("session", "audible bell unavailable: audio device is silent");
            return;
        }
        const auto connected = context.connect(source, context.destination());
        if (!connected.ok()) {
            AURORA_LOG_WARN("session", "audible bell unavailable: audio graph connect failed: ",
                            connected.error().message);
            warned = true;
        }
    }

    aurora::AudioContext context;
    std::shared_ptr<aurora::AudioStreamSourceNode> source;
    std::vector<std::int16_t> tone;
    bool warned = false;
};

BellAudio::BellAudio() : impl_(std::make_unique<Impl>(nullptr)) {}

BellAudio::BellAudio(std::unique_ptr<aurora::AudioDeviceBackend> backend)
    : impl_(std::make_unique<Impl>(std::move(backend))) {}

BellAudio::~BellAudio() = default;

auto BellAudio::ring() -> bool {
    if (impl_->context.silent()) {
        if (!impl_->warned) {
            AURORA_LOG_WARN("session", "audible bell requested while the audio device is silent");
            impl_->warned = true;
        }
        return false;
    }
    const auto pushed = impl_->source->push(impl_->tone, kToneSampleRate, 1);
    if (!pushed.ok()) {
        if (!impl_->warned) {
            AURORA_LOG_WARN("session", "audible bell push failed: ", pushed.error().message);
            impl_->warned = true;
        }
        return false;
    }
    return true;
}

auto BellAudio::silent() const -> bool {
    return impl_->context.silent();
}

}  // namespace borealis::session
