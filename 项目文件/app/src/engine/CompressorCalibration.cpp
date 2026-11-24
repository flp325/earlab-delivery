#include "CompressorCalibration.h"

#include <cmath>

namespace earlab
{

float calibrateThresholdForTargetGr (CompressorProcessor& compressor,
                                     const juce::AudioBuffer<float>& source,
                                     double sampleRate,
                                     const CompressorProcessor::Params& baseParams,
                                     float targetGrDb)
{
    if (source.getNumSamples() <= 0 || source.getNumChannels() <= 0 || sampleRate <= 0.0)
        return baseParams.thresholdDb;

    const int maxSamples = juce::jmin (source.getNumSamples(),
                                       static_cast<int> (sampleRate * 5.0));

    juce::AudioBuffer<float> working (source.getNumChannels(), maxSamples);

    float lo = -60.0f;
    float hi = 0.0f;
    CompressorProcessor::Params params = baseParams;

    for (int iteration = 0; iteration < 12; ++iteration)
    {
        const float mid = 0.5f * (lo + hi);
        params.thresholdDb = mid;

        compressor.setParameters (params);
        compressor.reset();
        compressor.resetMetering();

        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            working.copyFrom (ch, 0, source, ch, 0, maxSamples);

        compressor.process (working);

        const float measuredGr = compressor.getAverageGainReductionDb();

        if (measuredGr < targetGrDb)
            hi = mid;   // 压缩不够，阈值继续往下探
        else
            lo = mid;
    }

    params.thresholdDb = 0.5f * (lo + hi);
    compressor.setParameters (params);
    compressor.reset();

    return params.thresholdDb;
}

} // namespace earlab
