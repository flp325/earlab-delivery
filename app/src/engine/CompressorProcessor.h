#pragma once

#include "TrainableProcessor.h"

#include <atomic>

namespace earlab
{

/** 前馈式峰值压缩器，立体声联动，带软拐点；用于动态训练的内置插件。 */
class CompressorProcessor final : public ITrainableProcessor
{
public:
    CompressorProcessor() = default;

    struct Params
    {
        float thresholdDb = -30.0f;
        float ratio = 4.0f;
        float attackMs = 10.0f;
        float releaseMs = 100.0f;
        float kneeDb = 6.0f;
        float makeupDb = 0.0f;
    };

    juce::String getName() const override { return "EarLab Comp"; }
    int getLatencySamples() const override { return 0; }

    void prepare (double sampleRate, int maximumBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer) override;

    void setParameters (const Params& newParams);
    void setNeutral();

    [[nodiscard]] Params getParameters() const;

    /** 最近一个处理块内的最大增益衰减（正数，dB）。 */
    [[nodiscard]] float getGainReductionDb() const noexcept { return lastGrDb.load(); }

    /** 自上次 resetMetering 以来的平均增益衰减（正数，dB），用于离线校准。 */
    [[nodiscard]] float getAverageGainReductionDb() const noexcept;
    void resetMetering() noexcept;

private:
    double sr = 48000.0;
    int channels = 2;

    float envelopeGr = 0.0f;
    float attackCoeff = 0.01f;
    float releaseCoeff = 0.001f;

    std::atomic<float> thresholdDb { -30.0f };
    std::atomic<float> ratio { 4.0f };
    std::atomic<float> kneeDb { 6.0f };
    std::atomic<float> makeupDb { 0.0f };
    std::atomic<float> attackCoeffAtomic { 0.01f };
    std::atomic<float> releaseCoeffAtomic { 0.001f };
    std::atomic<float> attackMsAtomic { 10.0f };
    std::atomic<float> releaseMsAtomic { 100.0f };

    std::atomic<float> lastGrDb { 0.0f };
    double grSum = 0.0;
    long long grCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorProcessor)
};

} // namespace earlab
