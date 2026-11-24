#include "DualChain.h"

#include <algorithm>
#include <cmath>

namespace earlab
{

void DualChain::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    sr           = sampleRate > 0.0 ? sampleRate : 48000.0;
    channels     = juce::jmax (1, numChannels);
    maxBlockSize = juce::jmax (1, maximumBlockSize);

    wetBuffer.setSize (channels, maxBlockSize);

    // 链路负责初始化处理器，避免调用方漏调 prepare 造成空缓冲
    if (proc != nullptr)
        proc->prepare (sr, maxBlockSize, channels);

    // 每条链路各自做旋转，保证与湿路对齐
    currentLatency = getLatencySamples();

    // 10 ms 交叉淡化
    mixStep = static_cast<float> (1.0 / (0.01 * sr));

    rebuildDelayLine();
    reset();
}

void DualChain::rebuildDelayLine()
{
    dryDelaySize = juce::jmax (1, currentLatency);
    dryWritePos  = 0;

    // 只用 fill 已有容量，避免在音频/播放期间反复分配
    if (static_cast<int> (dryDelay.size()) != channels)
        dryDelay.assign (static_cast<size_t> (channels), {});

    for (auto& line : dryDelay)
    {
        line.resize (static_cast<size_t> (dryDelaySize), 0.0f);
        std::fill (line.begin(), line.end(), 0.0f);
    }
}

void DualChain::reset()
{
    wetBuffer.clear();

    for (auto& line : dryDelay)
        std::fill (line.begin(), line.end(), 0.0f);

    dryWritePos = 0;
    mixGain.store (getSource() == Source::wet ? 1.0f : 0.0f, std::memory_order_relaxed);
}

void DualChain::setProcessor (ITrainableProcessor* processor)
{
    proc = processor;
    currentLatency = getLatencySamples();
    rebuildDelayLine();
}

void DualChain::setTrims (float dryTrimDb, float wetTrimDb)
{
    dryGain.store (juce::Decibels::decibelsToGain (dryTrimDb), std::memory_order_relaxed);
    wetGain.store (juce::Decibels::decibelsToGain (wetTrimDb), std::memory_order_relaxed);
}

void DualChain::setSource (Source newSource, bool immediate)
{
    target.store (static_cast<int> (newSource), std::memory_order_relaxed);

    if (immediate)
        mixGain.store (newSource == Source::wet ? 1.0f : 0.0f, std::memory_order_relaxed);
}

DualChain::Source DualChain::getSource() const noexcept
{
    return static_cast<Source> (target.load (std::memory_order_relaxed));
}

bool DualChain::isCrossfading() const noexcept
{
    const float goal = getSource() == Source::wet ? 1.0f : 0.0f;
    return std::abs (mixGain.load (std::memory_order_relaxed) - goal) > 1.0e-6f;
}

int DualChain::getLatencySamples() const
{
    return proc != nullptr ? juce::jmax (0, proc->getLatencySamples()) : 0;
}

void DualChain::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin (buffer.getNumChannels(), channels);

    if (numSamples <= 0 || numCh <= 0)
        return;

    // 防御：主机块大小超过 prepare 时的上限，宁可静音也不要越界写
    if (numSamples > wetBuffer.getNumSamples())
        return;

    // 1) 湿路：复制输入后交给处理器
    for (int ch = 0; ch < numCh; ++ch)
        wetBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    if (proc != nullptr)
    {
        juce::AudioBuffer<float> view (wetBuffer.getArrayOfWritePointers(), numCh, 0, numSamples);
        proc->process (view);
    }

    // 2) 干路：按处理器延迟做样本级对齐
    if (currentLatency > 0 && dryDelaySize == currentLatency)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                auto& line = dryDelay[static_cast<size_t> (ch)];

                const float in = d[i];
                d[i] = line[static_cast<size_t> (dryWritePos)];
                line[static_cast<size_t> (dryWritePos)] = in;
            }

            dryWritePos = (dryWritePos + 1) % dryDelaySize;
        }
    }

    // 3) 线性交叉淡化（相关信号的幅度不会在切换中途抬升）
    const float goal = getSource() == Source::wet ? 1.0f : 0.0f;
    const float gDryTarget = dryGain.load (std::memory_order_relaxed);
    const float gWetTarget = wetGain.load (std::memory_order_relaxed);

    float mix = mixGain.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        if (mix < goal)      mix = juce::jmin (goal, mix + mixStep);
        else if (mix > goal) mix = juce::jmax (goal, mix - mixStep);

        const float gDry = (1.0f - mix) * gDryTarget;
        const float gWet = mix * gWetTarget;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[i] = d[i] * gDry + wetBuffer.getSample (ch, i) * gWet;
        }
    }

    mixGain.store (mix, std::memory_order_relaxed);
}

} // namespace earlab
