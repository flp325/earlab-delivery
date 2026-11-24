#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>
#include <vector>

namespace earlab
{

/** 训练链里可替换的处理单元。内置插件与第三方插件最终都实现这个接口。 */
class ITrainableProcessor
{
public:
    virtual ~ITrainableProcessor() = default;

    virtual juce::String getName() const = 0;
    virtual void prepare (double sampleRate, int maximumBlockSize, int numChannels) = 0;
    virtual void reset() = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;
    virtual int getLatencySamples() const = 0;
};

/**
    A1 的内置湿声处理器：单段峰值 EQ。

    系数在 UI 线程算好，用双缓冲 + 原子索引交给音频线程，
    音频线程只读不分配，满足实时约束。
*/
class PeakEqProcessor final : public ITrainableProcessor
{
public:
    juce::String getName() const override { return "EarLab Peak EQ"; }
    int getLatencySamples() const override { return 0; }

    void prepare (double newSampleRate, int maximumBlockSize, int numChannels) override
    {
        juce::ignoreUnused (maximumBlockSize);

        sampleRate  = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        channels    = juce::jmax (1, numChannels);
        channelState.assign (static_cast<size_t> (channels), ChannelState {});

        updateCoefficients();
        reset();
    }

    void reset() override
    {
        for (auto& s : channelState)
            s = ChannelState {};
    }

    void setParameters (float frequencyHz, float gainDb, float qFactor)
    {
        frequency = juce::jlimit (20.0f, 20000.0f, frequencyHz);
        gain      = juce::jlimit (-24.0f, 24.0f, gainDb);
        q         = juce::jlimit (0.1f, 18.0f, qFactor);

        updateCoefficients();
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        const Coeffs c = coefficients[static_cast<size_t> (active.load (std::memory_order_acquire))];

        const int numSamples = buffer.getNumSamples();
        const int numCh      = juce::jmin (buffer.getNumChannels(), channels);

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& st   = channelState[static_cast<size_t> (ch)];

            for (int i = 0; i < numSamples; ++i)
            {
                const double x = static_cast<double> (data[i]);
                const double y = c.b0 * x + st.z1;
                st.z1 = c.b1 * x - c.a1 * y + st.z2;
                st.z2 = c.b2 * x - c.a2 * y;
                data[i] = static_cast<float> (y);
            }
        }
    }

private:
    struct Coeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    struct ChannelState
    {
        double z1 = 0.0, z2 = 0.0;
    };

    void updateCoefficients()
    {
        // RBJ 峰值滤波器（峰值增益 ±24 dB，Q 0.1–18）
        const double A     = std::pow (10.0, static_cast<double> (gain) / 40.0);
        const double w0    = 2.0 * juce::MathConstants<double>::pi * static_cast<double> (frequency) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * static_cast<double> (q));
        const double cosw  = std::cos (w0);
        const double a0    = 1.0 + alpha / A;

        const int next = 1 - active.load (std::memory_order_relaxed);
        Coeffs& c = coefficients[static_cast<size_t> (next)];

        c.b0 = (1.0 + alpha * A) / a0;
        c.b1 = (-2.0 * cosw) / a0;
        c.b2 = (1.0 - alpha * A) / a0;
        c.a1 = (-2.0 * cosw) / a0;
        c.a2 = (1.0 - alpha / A) / a0;

        active.store (next, std::memory_order_release);
    }

    double sampleRate = 48000.0;
    int channels = 2;
    float frequency = 1000.0f, gain = 0.0f, q = 1.0f;

    Coeffs coefficients[2];
    std::atomic<int> active { 0 };
    std::vector<ChannelState> channelState;
};

/** 测试与占位用的简单增益单元。 */
class GainProcessor final : public ITrainableProcessor
{
public:
    explicit GainProcessor (float gainDb = 0.0f) : gain (juce::Decibels::decibelsToGain (gainDb)) {}

    juce::String getName() const override { return "EarLab Gain"; }
    void prepare (double, int, int) override {}
    void reset() override {}

    void process (juce::AudioBuffer<float>& buffer) override
    {
        buffer.applyGain (gain);
    }

    int getLatencySamples() const override { return 0; }

private:
    float gain = 1.0f;
};

} // namespace earlab
