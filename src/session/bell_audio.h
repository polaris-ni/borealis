#pragma once

// ============================================================
// session/bell_audio.h — 可听铃声出口（私有头，含框架类型）
// ------------------------------------------------------------
// 全仓唯一触达 Aurora 音频设备的 TU（`SPEC.FEAT.WS.04`，裁决 7.159）。走框架明示安全的
// 推流腿（`AudioStreamSourceNode::push`，UI 线程生产者 / 设备线程消费者），不用
// `AudioBufferSourceNode::start()` 重播——后者重置渲染相位而 `play_pos_` 未与设备线程同步。
//
// 设备不可用（未编译后端 / 无设备 / 初始化失败）静默降级：`ring()` 返回 false 并只留一条
// WARN，角标仍由策略腿点亮——铃声是增强，不能成为会话事件的失败点。
// ============================================================

#include <memory>

namespace aurora {

class AudioDeviceBackend;

}  // namespace aurora

namespace borealis::session {

/// @brief BEL 的可听铃声出口（`SPEC.FEAT.WS.04`）。
///
/// 构造即建音频图（设备线程随之启动）。装配层只在「可听档」时才构造，缺省视觉档零音频
/// 设备开销；运行期改档到可听时，首次响铃会付一次设备初始化。
///
/// @note Thread: 构造/析构/`ring`/`silent` 均为主线程
/// @note Side-effects: 持有音频设备；`ring` 推入一段 PCM
/// @note Rebuildable: no
class BellAudio {
  public:
    /// @brief 用编译期默认设备后端构造（未开 `AURORA_ENABLE_AUDIO` 时为静默模式）。
    BellAudio();
    /// @brief 注入设备后端构造（测试用：假后端记录样本）；nullptr 同默认构造。
    /// @param backend 设备后端（所有权转入）。
    explicit BellAudio(std::unique_ptr<aurora::AudioDeviceBackend> backend);
    ~BellAudio();
    BellAudio(const BellAudio &) = delete;
    auto operator=(const BellAudio &) -> BellAudio & = delete;
    BellAudio(BellAudio &&) = delete;
    auto operator=(BellAudio &&) -> BellAudio & = delete;

    /// @brief 推入一声 880 Hz / 120 ms 的铃声。
    /// @return 已入队为 true；设备静默或推入失败为 false（诊断只留一次）。
    auto ring() -> bool;
    /// @brief 设备是否处于静默模式（无设备/初始化失败）。
    /// @return `AudioContext::silent()`。
    [[nodiscard]] auto silent() const -> bool;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace borealis::session
