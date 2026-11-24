#include "CompressorProcessor.h"

#include <cmath>

namespace earlab
{

namespace
{
float timeToCoefficient (float milliseconds, double sampleRate)
{
    const double samples = juce::jmax (1.0, static_cast<double> (milliseconds) * 0.001 * sampleRate);
    return static_cast<float> (1.0 - std::exp (-1.0 / samples));
}
} // namespace

void CompressorProcessor::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    juce::ignoreUnused (maximumBlockSize);

    sr       = sampleRate > 0.0 ? sampleRate : 48000.0;
    channels = juce::jmax (1, numChannels);

    // Time constants are expressed in milliseconds and must follow device rate changes.
    setParameters (getParameters());
    reset();
}

void CompressorProcessor::reset()
{
    envelopeGr = 0.0f;
    resetMetering();
}

void CompressorProcessor::setParameters (const Params& p)
{
    thresholdDb.store (p.thresholdDb);
    ratio.store (juce::jmax (1.0f, p.ratio));
    kneeDb.store (juce::jmax (0.0f, p.kneeDb));
    makeupDb.store (p.makeupDb);

    attackCoeffAtomic.store (timeToCoefficient (p.attackMs, sr));
    releaseCoeffAtomic.store (timeToCoefficient (p.releaseMs, sr));
    attackMsAtomic.store (p.attackMs);
    releaseMsAtomic.store (p.releaseMs);
}

void CompressorProcessor::setNeutral()
{
    Params p;
    p.ratio = 1.0f;
    p.thresholdDb = 0.0f;
    p.kneeDb = 0.0f;
    p.makeupDb = 0.0f;
    setParameters (p);
}

CompressorProcessor::Params CompressorProcessor::getParameters() const
{
    Params p;
    p.thresholdDb = thresholdDb.load();
    p.ratio       = ratio.load();
    p.kneeDb      = kneeDb.load();
    p.makeupDb    = makeupDb.load();
    p.attackMs    = attackMsAtomic.load();
    p.releaseMs   = releaseMsAtomic.load();
    return p;
}

void CompressorProcessor::resetMetering() noexcept
{
    grSum = 0.0;
    grCount = 0;
    lastGrDb.store (0.0f);
}

float CompressorProcessor::getAverageGainReductionDb() const noexcept
{
    return grCount > 0 ? static_cast<float> (grSum / static_cast<double> (grCount)) : 0.0f;
}

void CompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin (buffer.getNumChannels(), channels);

    if (numSamples <= 0 || numCh <= 0)
        return;

    const float thr    = thresholdDb.load();
    const float rat    = ratio.load();
    const float knee   = kneeDb.load();
    const float makeup = makeupDb.load();
    const float attack = attackCoeffAtomic.load();
    const float release = releaseCoeffAtomic.load();
    const float slope  = 1.0f - 1.0f / rat;
    const float halfKnee = knee * 0.5f;

    // The neutral chain is the common EQ-training path. Avoid two logarithmic/
    // exponential conversions per sample when there is no gain envelope to release.
    if (rat == 1.0f && envelopeGr == 0.0f)
    {
        const float gain = juce::Decibels::decibelsToGain (makeup);
        for (int ch = 0; ch < numCh; ++ch)
            buffer.applyGain (ch, 0, numSamples, gain);
        lastGrDb.store (0.0f, std::memory_order_relaxed);
        grCount += numSamples;
        return;
    }

    float maxGr = 0.0f;
    double blockSum = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

        const float levelDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        const float over    = levelDb - thr;

        float targetGr = 0.0f;

        if (over >= halfKnee)
            targetGr = over * slope;
        else if (over > -halfKnee && knee > 0.0f)
        {
            const float t = over + halfKnee;
            targetGr = slope * t * t / (2.0f * knee);
        }

        const float coeff = targetGr > envelopeGr ? attack : release;
        envelopeGr += (targetGr - envelopeGr) * coeff;

        const float gain = juce::Decibels::decibelsToGain (-envelopeGr + makeup);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);

        maxGr = juce::jmax (maxGr, envelopeGr);
        blockSum += envelopeGr;
    }

    lastGrDb.store (maxGr);
    grSum += blockSum;
    grCount += numSamples;
}

} // namespace earlab
