#pragma once

#include "TrainableProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <vector>

namespace earlab
{

/**
    干湿双链：两路常驻并行，靠交叉淡化切换，而不是 bypass 插件。

    - 湿路：输入 -> 处理器
    - 干路：延迟 getLatencySamples() 个样本，与湿路样本级对齐
    - 两路各自带等响 trim，切换过程中响度不变

    交叉淡化用线性增益而不是等功率：干湿两路来自同一素材、强相关，
    等功率淡化在中点会把幅度抬到 √2 倍（约 +3 dB），正好落在用户做 A/B 判断的瞬间。
*/
class DualChain
{
public:
    enum class Source { dry = 0, wet = 1 };

    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset();

    void setProcessor (ITrainableProcessor* processor);
    void setTrims (float dryTrimDb, float wetTrimDb);
    void setSource (Source newSource, bool immediate = false);

    void process (juce::AudioBuffer<float>& buffer);

    [[nodiscard]] Source getSource() const noexcept;
    [[nodiscard]] bool isCrossfading() const noexcept;
    [[nodiscard]] float getDryGain() const noexcept { return dryGain.load (std::memory_order_relaxed); }
    [[nodiscard]] float getWetGain() const noexcept { return wetGain.load (std::memory_order_relaxed); }
    [[nodiscard]] int getLatencySamples() const;
    [[nodiscard]] int getCurrentLatency() const noexcept { return currentLatency; }

private:
    void rebuildDelayLine();

    ITrainableProcessor* proc = nullptr;

    double sr = 48000.0;
    int channels = 2;
    int maxBlockSize = 512;
    int currentLatency = 0;

    juce::AudioBuffer<float> wetBuffer;
    std::vector<std::vector<float>> dryDelay;
    int dryDelaySize = 1;
    int dryWritePos  = 0;

    // 消息线程写、音频线程读：只用原子量，避免数据竞争
    std::atomic<float> dryGain { 1.0f };
    std::atomic<float> wetGain { 1.0f };
    std::atomic<int>   target { static_cast<int> (Source::dry) };
    std::atomic<float> mixGain { 0.0f };

    float mixStep = 1.0f / 480.0f;
};

} // namespace earlab
