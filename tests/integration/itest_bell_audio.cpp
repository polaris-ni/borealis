/// 测试类型: integration
/// 目标单元: src/session/bell_audio.h + src/session/bell_audio.cpp（真 Aurora 音频图 + 假设备）
/// 测试说明: 用注入的假设备后端替掉真实声卡，走一遍 `BellAudio` 的完整出声链路
///           （`AudioContext` → `AudioStreamSourceNode` → destination → render_block），验证
///           （`SPEC.FEAT.WS.04`，裁决 7.159）：设备被真正 start、ring 入队成功、渲染出的样本
///           峰值在可听且不削顶的区间、mono 被复制到双声道、120 ms 之后回到精确静音、设备起不来
///           时静默降级而不是崩溃或抛错、以及同一出口可以再次响铃（推流腿不是一次性缓冲源）。
///           假设备不起线程，由本用例手动拉块——判据确定性，不依赖声卡与桌面音频会话。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "aurora/media/audio.h"
#include "framework/aurora_test.h"
#include "session/bell_audio.h"

namespace borealis::test_cases::itest_bell_audio {

namespace {

/// @brief 假音频设备：记住渲染回调，由用例手动拉块；可注入 start 失败。
class RecordingDevice final : public aurora::AudioDeviceBackend {
  public:
    explicit RecordingDevice(bool fail_start = false) : fail_start_(fail_start) {}

    [[nodiscard]] auto format() const -> aurora::AudioDeviceFormat override {
        return aurora::AudioDeviceFormat{48000, 2};
    }
    auto start(RenderFn render_block) -> bool override {
        if (fail_start_) {
            return false;
        }
        render_ = std::move(render_block);
        started_ = true;
        return true;
    }
    auto stop() -> void override {
        started_ = false;
        render_ = nullptr;
    }

    /// @brief 拉一块交错立体声样本（假设备线程的手动替身）。
    auto render(int frames) -> std::vector<float> {
        auto buffer = std::vector<float>(static_cast<std::size_t>(frames) * 2U, 0.0F);
        if (render_ != nullptr) {
            render_(buffer.data(), frames);
        }
        return buffer;
    }

    [[nodiscard]] auto started() const -> bool { return started_; }

  private:
    bool fail_start_;
    bool started_ = false;
    RenderFn render_;
};

constexpr std::size_t kChannels = 2U;
constexpr int kBlockFrames = 512;
constexpr int kBlocks = 16;  // 8192 帧，覆盖 5760 帧铃声并留出尾部余量

[[nodiscard]] auto peak_of(std::span<const float> interleaved, std::size_t frames) -> float {
    float peak = 0.0F;
    const std::size_t samples = std::min(interleaved.size(), frames * kChannels);
    for (std::size_t index = 0; index < samples; ++index) {
        peak = std::max(peak, std::abs(interleaved[index]));
    }
    return peak;
}

/// @brief 拉够 kBlocks 块，把交错样本拼成一条。
[[nodiscard]] auto render_all(RecordingDevice &device) -> std::vector<float> {
    auto collected = std::vector<float>{};
    collected.reserve(static_cast<std::size_t>(kBlocks * kBlockFrames) * kChannels);
    for (int block = 0; block < kBlocks; ++block) {
        const auto chunk = device.render(kBlockFrames);
        collected.insert(collected.end(), chunk.begin(), chunk.end());
    }
    return collected;
}

}  // namespace

AURORA_TEST_CASE(ringing_device_produces_bounded_stereo_tone_then_exact_silence) {
    auto device = std::make_unique<RecordingDevice>();
    auto *raw = device.get();
    session::BellAudio bell{std::move(device)};

    AURORA_TEST_REQUIRE_MSG(raw->started(), "audio context did not start the device");
    AURORA_TEST_REQUIRE_MSG(!bell.silent(), "injected device must not be silent");
    AURORA_TEST_REQUIRE_MSG(bell.ring(), "first ring must enqueue");

    const auto collected = render_all(*raw);
    AURORA_TEST_REQUIRE_EQ(collected.size(),
                           static_cast<std::size_t>(kBlocks * kBlockFrames) * kChannels);

    // 峰值：0.22 的设计幅度经 16-bit 量化与 float 归一后约 0.2199。下界证明「真出了声」，
    // 上界证明没有削顶（> 1.0 会被设备侧硬限幅）。
    const float peak = peak_of(collected, 5760U);
    AURORA_TEST_CHECK_MSG(peak > 0.05F, "tone peak is too quiet to hear");
    AURORA_TEST_CHECK_MSG(peak < 0.3F, "tone peak exceeds the intended amplitude");

    // mono 源按节点契约复制到左右两声道：两列逐样本相等。
    bool stereo_matched = true;
    for (std::size_t frame = 0; frame < 5760U; ++frame) {
        stereo_matched = stereo_matched &&
                         collected[frame * kChannels] == collected[frame * kChannels + 1U];
    }
    AURORA_TEST_CHECK_MSG(stereo_matched, "mono tone was not duplicated to both channels");

    // 120 ms 之后必须精确归零：尾部留 240 帧（5 ms）给重采样相位，之后一个非零样本都不许有。
    bool tail_silent = true;
    for (std::size_t frame = 6000U; frame < static_cast<std::size_t>(kBlocks * kBlockFrames);
         ++frame) {
        tail_silent = tail_silent && collected[frame * kChannels] == 0.0F &&
                      collected[frame * kChannels + 1U] == 0.0F;
    }
    AURORA_TEST_CHECK_MSG(tail_silent, "tone did not return to silence after 120 ms");
}

AURORA_TEST_CASE(same_outlet_rings_again_after_the_first_tone_is_consumed) {
    auto device = std::make_unique<RecordingDevice>();
    auto *raw = device.get();
    session::BellAudio bell{std::move(device)};
    AURORA_TEST_REQUIRE_MSG(bell.ring(), "first ring must enqueue");
    static_cast<void>(render_all(*raw));

    // 推流腿的第二声：缓冲源 `start()` 的重播会重置渲染相位，这里断言同一条出口可复用。
    AURORA_TEST_REQUIRE_MSG(bell.ring(), "second ring must enqueue after the ring drained");
    const auto collected = render_all(*raw);
    AURORA_TEST_CHECK_MSG(peak_of(collected, 5760U) > 0.05F, "second tone was not audible");
}

AURORA_TEST_CASE(device_start_failure_degrades_to_silent_instead_of_failing) {
    auto device = std::make_unique<RecordingDevice>(true);
    session::BellAudio bell{std::move(device)};
    AURORA_TEST_CHECK_MSG(bell.silent(), "failed device start must land in silent mode");
    // 铃声是增强而非会话事件的失败点：投不出去返回 false，不抛、不拦调用方。
    AURORA_TEST_CHECK_MSG(!bell.ring(), "silent device must refuse the ring");
    AURORA_TEST_CHECK_MSG(!bell.ring(), "silent device keeps refusing without side effects");
}

}  // namespace borealis::test_cases::itest_bell_audio
