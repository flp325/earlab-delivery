#include "MultiBandEq.h"

#include <cmath>

namespace earlab
{

namespace
{
float finiteClamp (float value, float low, float high, float fallback)
{
    return std::isfinite (value) ? juce::jlimit (low, high, value) : fallback;
}

double validRate (double rate)
{
    return std::isfinite (rate) && rate >= 1000.0 ? rate : 48000.0;
}

MultiBandEqProcessor::Band makePeak (float frequency, float gainDb, float q)
{
    MultiBandEqProcessor::Band band;
    band.frequency = juce::jlimit (20.0f, 20000.0f, frequency);
    band.gainDb = juce::jlimit (-24.0f, 24.0f, gainDb);
    band.q = juce::jlimit (0.1f, 18.0f, q);
    band.enabled = true;
    return band;
}
} // namespace

MultiBandEqProcessor::Settings::Settings()
{
    // 全默认叠在 1 kHz 会让 8 个节点重合，只能拖到最上面那一个；
    // 铺开之后每个空闲段都有自己的位置，点哪儿都能新建
    static const float defaultFrequencies[numBands] =
    {
        100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f, 12500.0f
    };

    for (int band = 0; band < numBands; ++band)
        bands[band].frequency = defaultFrequencies[band];
}

void MultiBandEqProcessor::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    juce::ignoreUnused (maximumBlockSize);

    sr = validRate (sampleRate);
    channels = juce::jmax (1, numChannels);
    channelState.assign (static_cast<size_t> (channels), ChannelState {});

    rebuildCoefficients();
    reset();
}

void MultiBandEqProcessor::reset()
{
    for (auto& state : channelState)
        state = ChannelState {};
}

void MultiBandEqProcessor::applySettings (const Settings& newSettings)
{
    settings = newSettings;
    for (auto& band : settings.bands)
    {
        band.frequency = finiteClamp (band.frequency, 20.0f, 20000.0f, 1000.0f);
        band.gainDb = finiteClamp (band.gainDb, -24.0f, 24.0f, 0.0f);
        band.q = finiteClamp (band.q, 0.1f, 18.0f, 1.0f);
    }
    settings.hpHz = finiteClamp (settings.hpHz, 10.0f, 20000.0f, 30.0f);
    settings.lpHz = finiteClamp (settings.lpHz, 10.0f, 20000.0f, 18000.0f);
    rebuildCoefficients();
}

MultiBandEqProcessor::Settings MultiBandEqProcessor::getSettings() const
{
    return settings;
}

void MultiBandEqProcessor::setSingleBand (float frequencyHz, float gainDb, float q)
{
    Settings next = settings;   // 保留高低切与各段频率，其余参数归零

    for (auto& band : next.bands)
    {
        band.gainDb = 0.0f;
        band.q = 1.0f;
        band.enabled = false;
    }

    next.bands[0] = makePeak (frequencyHz, gainDb, q);
    next.bands[0].enabled = gainDb != 0.0f;

    applySettings (next);
}

void MultiBandEqProcessor::setHighLowPassEnabled (bool shouldBeEnabled)
{
    if (settings.hpEnabled == shouldBeEnabled && settings.lpEnabled == shouldBeEnabled)
        return;

    Settings next = settings;
    next.hpEnabled = shouldBeEnabled;
    next.lpEnabled = shouldBeEnabled;
    applySettings (next);
}

void MultiBandEqProcessor::rebuildCoefficients()
{
    coefficients.publish (makeCoefficients (sr));
}

MultiBandEqProcessor::CoeffSet MultiBandEqProcessor::makeCoefficients (double rate) const
{
    CoeffSet set;
    for (int band = 0; band < numBands; ++band)
    {
        const auto& b = settings.bands[band];
        Biquad& c = set.bands[band];

        set.bandOn[band] = b.enabled && std::abs (b.gainDb) >= 0.01f;

        if (! set.bandOn[band])
        {
            c = Biquad {};
            continue;
        }

        const double A = std::pow (10.0, static_cast<double> (b.gainDb) / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (20.0, juce::jmin (20000.0, rate * 0.45), static_cast<double> (b.frequency)) / rate;
        const double alpha = std::sin (w0) / (2.0 * static_cast<double> (b.q));
        const double cosw = std::cos (w0);
        const double a0 = 1.0 + alpha / A;

        c.b0 = (1.0 + alpha * A) / a0;
        c.b1 = (-2.0 * cosw) / a0;
        c.b2 = (1.0 - alpha * A) / a0;
        c.a1 = (-2.0 * cosw) / a0;
        c.a2 = (1.0 - alpha / A) / a0;
    }

    // 二阶高低切（12 dB/oct）
    set.hpOn = settings.hpEnabled;
    set.lpOn = settings.lpEnabled;

    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi
                              * juce::jlimit (10.0, rate * 0.45, static_cast<double> (settings.hpHz)) / rate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;

        set.hp.b0 = (1.0 + cosw) / 2.0 / a0;
        set.hp.b1 = -(1.0 + cosw) / a0;
        set.hp.b2 = (1.0 + cosw) / 2.0 / a0;
        set.hp.a1 = (-2.0 * cosw) / a0;
        set.hp.a2 = (1.0 - alpha) / a0;
    }

    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi
                              * juce::jlimit (10.0, rate * 0.45, static_cast<double> (settings.lpHz)) / rate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * 0.70710678);
        const double a0 = 1.0 + alpha;

        set.lp.b0 = (1.0 - cosw) / 2.0 / a0;
        set.lp.b1 = (1.0 - cosw) / a0;
        set.lp.b2 = (1.0 - cosw) / 2.0 / a0;
        set.lp.a1 = (-2.0 * cosw) / a0;
        set.lp.a2 = (1.0 - alpha) / a0;
    }

    return set;
}

void MultiBandEqProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumSamples() <= 0)
        return;

    // 块首取一次快照：之后不再读 settings 等共享状态
    const CoeffSet& c = coefficients.consume();
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), channels);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& state = channelState[static_cast<size_t> (ch)];

        for (int i = 0; i < numSamples; ++i)
        {
            double x = static_cast<double> (data[i]);

            for (int band = 0; band < numBands; ++band)
            {
                if (! c.bandOn[band])
                    continue;

                const auto& bq = c.bands[band];
                const int slot = band;
                const double y = bq.b0 * x + state.z1[slot];
                state.z1[slot] = bq.b1 * x - bq.a1 * y + state.z2[slot];
                state.z2[slot] = bq.b2 * x - bq.a2 * y;
                x = y;
            }

            if (c.hpOn)
            {
                constexpr int slot = numBands;
                const auto& bq = c.hp;
                const double y = bq.b0 * x + state.z1[slot];
                state.z1[slot] = bq.b1 * x - bq.a1 * y + state.z2[slot];
                state.z2[slot] = bq.b2 * x - bq.a2 * y;
                x = y;
            }

            if (c.lpOn)
            {
                constexpr int slot = numBands + 1;
                const auto& bq = c.lp;
                const double y = bq.b0 * x + state.z1[slot];
                state.z1[slot] = bq.b1 * x - bq.a1 * y + state.z2[slot];
                state.z2[slot] = bq.b2 * x - bq.a2 * y;
                x = y;
            }

            data[i] = static_cast<float> (x);
        }
    }
}

std::vector<float> MultiBandEqProcessor::getMagnitudeDb (const std::vector<float>& frequencies,
                                                          double sampleRate) const
{
    std::vector<float> result;
    result.reserve (frequencies.size());

    const double rate = validRate (sampleRate > 0.0 ? sampleRate : sr);
    const auto c = makeCoefficients (rate);

    const auto magnitudeOf = [] (const Biquad& c, double w)
    {
        const double cosw = std::cos (w);
        const double cos2w = std::cos (2.0 * w);
        const double sinw = std::sin (w);
        const double sin2w = std::sin (2.0 * w);

        const double numRe = c.b0 + c.b1 * cosw + c.b2 * cos2w;
        const double numIm = -(c.b1 * sinw + c.b2 * sin2w);
        const double denRe = 1.0 + c.a1 * cosw + c.a2 * cos2w;
        const double denIm = -(c.a1 * sinw + c.a2 * sin2w);

        const double num = std::sqrt (numRe * numRe + numIm * numIm);
        const double den = std::sqrt (denRe * denRe + denIm * denIm + 1.0e-18);
        return num / den;
    };

    for (const float frequency : frequencies)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi
                             * juce::jlimit (10.0, rate * 0.49, static_cast<double> (frequency)) / rate;

        double magnitude = 1.0;

        for (int band = 0; band < numBands; ++band)
            if (c.bandOn[band])
                magnitude *= magnitudeOf (c.bands[band], w);
        if (c.hpOn)
            magnitude *= magnitudeOf (c.hp, w);
        if (c.lpOn)
            magnitude *= magnitudeOf (c.lp, w);

        result.push_back (static_cast<float> (20.0 * std::log10 (magnitude + 1.0e-9)));
    }

    return result;
}

} // namespace earlab
