#include "LoudnessMatcher.h"

#include "../dsp/LoudnessMeter.h"

#include <cmath>

namespace earlab
{

LoudnessMatch computeLoudnessMatch (ITrainableProcessor& processor,
                                    const juce::AudioBuffer<float>& source,
                                    double sampleRate,
                                    float targetLufs)
{
    LoudnessMatch result;

    if (source.getNumSamples() <= 0 || source.getNumChannels() <= 0 || sampleRate <= 0.0)
        return result;

    // 只取前 10 秒：足够稳定，同时把消息线程上的离线渲染控制在可接受范围。
    // P1-1：即使 10 s（48 kHz 下 48 万样本）也远超插件的 prepare 块，
    // PluginTrainableProcessor 必须按 maxBlock 分块处理。
    constexpr double maxWindowSeconds = 10.0;
    const int maxSamples = juce::jmin (source.getNumSamples(),
                                       static_cast<int> (sampleRate * maxWindowSeconds));

    juce::AudioBuffer<float> dry (source.getNumChannels(), maxSamples);
    juce::AudioBuffer<float> wet (source.getNumChannels(), maxSamples);

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        dry.copyFrom (ch, 0, source, ch, 0, maxSamples);
        wet.copyFrom (ch, 0, source, ch, 0, maxSamples);
    }

    processor.reset();
    processor.process (wet);
    processor.reset();

    result.dryLufs = dsp::LoudnessMeter::measureIntegrated (dry, sampleRate, dry.getNumChannels());
    result.wetLufs = dsp::LoudnessMeter::measureIntegrated (wet, sampleRate, wet.getNumChannels());

    if (! std::isfinite (result.dryLufs) || ! std::isfinite (result.wetLufs))
        return result;

    result.dryTrimDb = juce::jlimit (-30.0f, 30.0f,
                                     targetLufs - static_cast<float> (result.dryLufs));
    result.wetTrimDb = juce::jlimit (-30.0f, 30.0f,
                                     targetLufs - static_cast<float> (result.wetLufs));
    result.valid = true;

    return result;
}

} // namespace earlab
